//===-- UDPSocket.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/UDPSocket.h"

#include "lldb/Host/Config.h"
#include "lldb/Utility/Log.h"

#ifndef LLDB_DISABLE_POSIX
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <memory>

using namespace lldb;
using namespace lldb_private;

namespace {

const int kDomain = AF_INET;
const int kType = SOCK_DGRAM;

static const char *g_not_supported_error = "Not supported";
}

UDPSocket::UDPSocket(NativeSocket socket) : Socket(ProtocolUdp, true, true) {
  m_socket = socket;
}

UDPSocket::UDPSocket(bool should_close, bool child_processes_inherit)
    : Socket(ProtocolUdp, should_close, child_processes_inherit) {}

size_t UDPSocket::Send(const void *buf, const size_t num_bytes) {
  return ::sendto(m_socket, static_cast<const char *>(buf), num_bytes, 0,
                  m_sockaddr, m_sockaddr.GetLength());
}

Status UDPSocket::Connect(llvm::StringRef name) {
  return Status("%s", g_not_supported_error);
}

Status UDPSocket::Listen(llvm::StringRef name, int backlog) {
  return Status("%s", g_not_supported_error);
}

Status UDPSocket::Accept(Socket *&socket) {
  return Status("%s", g_not_supported_error);
}

Status UDPSocket::Connect(llvm::StringRef name, bool child_processes_inherit,
                          Socket *&socket) {
  std::unique_ptr<UDPSocket> final_socket;

  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_CONNECTION));
  if (log)
    log->Printf("UDPSocket::%s (host/port = %s)", __FUNCTION__, name.data());

  Status error;
  std::string host_str;
  std::string port_str;
  int32_t port = INT32_MIN;
  if (!DecodeHostAndPort(name, host_str, port_str, port, &error))
    return error;

  // At this point we have setup the receive port, now we need to setup the UDP
  // send socket

  struct addrinfo hints;
  struct addrinfo *service_info_list = nullptr;

  ::memset(&hints, 0, sizeof(hints));
  hints.ai_family = kDomain;
  hints.ai_socktype = kType;
  int err = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints,
                          &service_info_list);
  if (err != 0) {
    error.SetErrorStringWithFormat(
#if defined(_MSC_VER) && defined(UNICODE)
        "getaddrinfo(%s, %s, &hints, &info) returned error %i (%S)",
#else
        "getaddrinfo(%s, %s, &hints, &info) returned error %i (%s)",
#endif
        host_str.c_str(), port_str.c_str(), err, gai_strerror(err));
    return error;
  }

  for (struct addrinfo *service_info_ptr = service_info_list;
       service_info_ptr != nullptr;
       service_info_ptr = service_info_ptr->ai_next) {
    auto send_fd = CreateSocket(
        service_info_ptr->ai_family, service_info_ptr->ai_socktype,
        service_info_ptr->ai_protocol, child_processes_inherit, error);
    if (error.Success()) {
      final_socket.reset(new UDPSocket(send_fd));
      final_socket->m_sockaddr = service_info_ptr;
      break;
    } else
      continue;
  }

  ::freeaddrinfo(service_info_list);

  if (!final_socket)
    return error;

  SocketAddress bind_addr;

  // Only bind to the loopback address if we are expecting a connection from
  // localhost to avoid any firewall issues.
  const bool bind_addr_success = (host_str == "127.0.0.1" || host_str == "localhost")
                                     ? bind_addr.SetToLocalhost(kDomain, port)
                                     : bind_addr.SetToAnyAddress(kDomain, port);

  if (!bind_addr_success) {
    error.SetErrorString("Failed to get hostspec to bind for");
    return error;
  }

  bind_addr.SetPort(0); // Let the source port # be determined dynamically

  err = ::bind(final_socket->GetNativeSocket(), bind_addr, bind_addr.GetLength());

  struct sockaddr_in source_info;
  socklen_t address_len = sizeof (struct sockaddr_in);
  err = ::getsockname(final_socket->GetNativeSocket(), (struct sockaddr *) &source_info, &address_len);

  socket = final_socket.release();
  error.Clear();
  return error;
}
