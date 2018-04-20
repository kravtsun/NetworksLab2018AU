#ifndef SOCKET_HPP
#define SOCKET_HPP

#ifdef _WIN32
    #include <WinSock2.h>
    #include <Windows.h>
#else
    #include <netdb.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <string.h>
#endif

#include <stddef.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <ext/stdio_filebuf.h>
#include <iostream>

namespace net {

struct socket_ops
{
    static void init()
    {}

    static int send(int fd, const char *buf, size_t size)
    {
        return (int)::send(fd, buf, size, MSG_NOSIGNAL);
    }

    static int recv(int fd, char *buf, size_t size)
    {
        return (int)::recv(fd, buf, size, MSG_NOSIGNAL);
    }

    static void shutdown(int fd)
    {
        ::shutdown(fd, SHUT_RDWR);
    }

    static int close(int fd)
    {
        shutdown(fd);
        return ::close(fd);
    }

    static int create_inet()
    {
        return ::socket(AF_INET, SOCK_STREAM, 0);
    }
};

struct network_exception: public std::exception
{
    int ret;

    network_exception()
        : ret(errno)
    {}

    const char* what() const noexcept override
    {
        return strerror(ret);
    }
};


struct socket
{
    int sock;
    uint16_t portno;

    explicit socket(int fd)
    {
        portno = 0;
        sock = fd;
    }

    explicit socket()
    {
        portno = 0;
        sock = socket_ops::create_inet();
        if (sock < 0)
            throw network_exception();
    }

    socket(socket&& rhs)
    {
        sock = -1;
        std::swap(rhs.sock, sock);
    }

    void connect(const char *hostname, uint16_t port)
    {
        sockaddr_in addr = {};
        hostent *server;
        int ret;

        portno = port;
        server = gethostbyname(hostname);
        if (server == nullptr)
            throw network_exception();

        memset((char *)&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        memcpy((void *) &addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
        addr.sin_port = htons(portno);

        ret = ::connect(sock, (struct sockaddr *) &addr, sizeof(addr));
        if (ret < 0)
            throw network_exception();
    }

    void listen(uint16_t port)
    {
        int ret;
        sockaddr_in addr = {};

        portno = port;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(portno);

        ret = ::bind(sock, (struct sockaddr *) &addr, sizeof(addr));
        if (ret < 0)
            throw network_exception();

        ret = ::listen(sock, 5);
        if (ret < 0)
            throw network_exception();
    }

    socket accept()
    {
        sockaddr_in addr = {};
        socklen_t len = sizeof(addr);

        int client = ::accept(sock, (struct sockaddr *) &addr, &len);
        if (client < 0)
            throw network_exception();

        return socket(client);
    }

    int send(const char *buffer, size_t size)
    {
        int n = socket_ops::send(sock, buffer, size);
        if (n <= 0)
            throw network_exception();

        return n;
    }

    void send_all(const char *buffer, size_t size)
    {
        size_t off = 0;

        while (off != size)
            off += send(buffer + off, size - off);
    }

    int recv(char *buffer, size_t size)
    {
        int n = socket_ops::recv(sock, buffer, size);
        if (n <= 0)
            throw network_exception();

        return n;
    }

    void recv_all(char *buffer, size_t size)
    {
        size_t off = 0;

        while (off != size)
            off += recv(buffer + off, size - off);
    }

    void close()
    {
        socket_ops::shutdown(sock);
    }

    ~socket()
    {
        if (sock >= 0)
            socket_ops::close(sock);
        sock = -1;
    }

    bool operator==(const socket& rhs) const
    {
        return sock == rhs.sock;
    }

    std::unique_ptr<char[]> read_alloc(int size)
    {
        std::unique_ptr<char[]> ptr(new char[size]);
        recv_all(ptr.get(), size);
        return std::move(ptr);
    }
};

struct socket_stream
{
private:
    net::socket sock;
    __gnu_cxx::stdio_filebuf<char> fbuf;

public:
    std::iostream stream;

    socket_stream(socket_stream&& ss)
        : sock(std::move(ss.sock))
        , fbuf(std::move(ss.fbuf))
        , stream(&fbuf)
    {}

    explicit socket_stream(net::socket&& _sock)
            : sock(std::move(_sock))
            , fbuf(sock.sock, std::ios::in | std::ios::out)
            , stream(&fbuf)
    {}

    void check_conn() const {
        if (!stream)
            throw net::network_exception();
    }

    ~socket_stream() {
        sock.close(); // force d-ctr order
    }
};

} // ::net
#endif // SOCKET_HPP
