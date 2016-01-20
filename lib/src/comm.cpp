#include "comm.hpp"

#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <assert.h>

#include <arpa/inet.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "log.hpp"

namespace fcwt {

const char* const server_ipv4_addr = "192.168.0.1";

sock::sock(int fd)
: sockfd(fd)
{
}

sock::~sock()
{ 
  if (sockfd > 0)
  {
    close(sockfd);
  }
}

sock::sock(sock&& other)
: sockfd(other.sockfd)
{
  other.sockfd = 0;
}
  
sock& sock::operator=(sock&& other)
{
  sock tmp(std::move(other));
  swap(tmp);
  return *this;
}

void sock::swap(sock& other)
{
  int tmp = other.sockfd;
  other.sockfd = sockfd;
  sockfd = tmp;
}

sock connect_to_camera(int port)
{
  // TODO: proper error handling

  const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    fatal_error("Failed to create socket\n");

  fcntl(sockfd, F_SETFL, O_NONBLOCK); // for timeout

  sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  inet_pton(AF_INET, server_ipv4_addr, &sa.sin_addr);
  connect(sockfd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));

  // timeout handling
  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(sockfd, &fdset);
  struct timeval tv = {};
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1)
  {
      int so_error = 0;
      socklen_t len = sizeof so_error;
      getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

      if (so_error == 0)
      {
        printf("Connection esatablished %s:%d (%d)\n", server_ipv4_addr, port, sockfd);
        fcntl(sockfd, F_SETFL, 0);
        return sockfd;
      }
  }
  
  printf("Failed to connect\n");
  close(sockfd);
  return 0;
}

uint32_t to_fuji_size_prefix(uint32_t sizeBytes) {
    // TODO, 0x endianess
    return sizeBytes;
}

uint32_t from_fuji_size_prefix(uint32_t sizeBytes) {
    // TODO, 0x endianess
    return sizeBytes;
}

void send_data(int sockfd, void const* data, size_t sizeBytes) {
    bool retry = false;
    do {
        ssize_t const result = write(sockfd, data, sizeBytes);
        if (result < 0) {
            if (errno == EINTR)
                retry = true;
            else
                fatal_error("Failed to send data from socket\n");
        }
    } while (retry);
}

void receive_data(int sockfd, void* data, size_t sizeBytes) {
    while (sizeBytes > 0) {
        ssize_t const result = read(sockfd, data, sizeBytes);
        if (result < 0) {
            if (errno != EINTR)
                fatal_error("Failed to read data from socket\n");
        } else {
            sizeBytes -= result;
            data = static_cast<char*>(data) + result;
         }
    }
}

void fuji_send(int sockfd, void const* data, uint32_t sizeBytes)
{
    uint32_t const size = to_fuji_size_prefix(sizeBytes + sizeof(uint32_t));
    send_data(sockfd, &size, sizeof(uint32_t));
    send_data(sockfd, data, sizeBytes);
}

size_t fuji_receive(int sockfd, void* data, uint32_t sizeBytes)
{
  uint32_t size = 0;
  receive_data(sockfd, &size, sizeof(size));
  size = from_fuji_size_prefix(size);
  if (size < sizeof(size)) {
    LOG_WARN("fuji_receive, 0x invalid message");
    return 0;
  }
  size -= sizeof(size);
  receive_data(sockfd, data, std::min(sizeBytes, size));
  return size;
}

} // namespace fcwt
