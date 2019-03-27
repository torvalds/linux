//===-- DomainSocket.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/DomainSocket.h"

#include "llvm/Support/FileSystem.h"

#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace lldb;
using namespace lldb_private;

#ifdef __ANDROID__
// Android does not have SUN_LEN
#ifndef SUN_LEN
#define SUN_LEN(ptr)                                                           \
  (offsetof(struct sockaddr_un, sun_path) + strlen((ptr)->sun_path))
#endif
#endif // #ifdef __ANDROID__

namespace {

const int kDomain = AF_UNIX;
const int kType = SOCK_STREAM;

bool SetSockAddr(llvm::StringRef name, const size_t name_offset,
                 sockaddr_un *saddr_un, socklen_t &saddr_un_len) {
  if (name.size() + name_offset > sizeof(saddr_un->sun_path))
    return false;

  memset(saddr_un, 0, sizeof(*saddr_un));
  saddr_un->sun_family = kDomain;

  memcpy(saddr_un->sun_path + name_offset, name.data(), name.size());

  // For domain sockets we can use SUN_LEN in order to calculate size of
  // sockaddr_un, but for abstract sockets we have to calculate size manually
  // because of leading null symbol.
  if (name_offset == 0)
    saddr_un_len = SUN_LEN(saddr_un);
  else
    saddr_un_len =
        offsetof(struct sockaddr_un, sun_path) + name_offset + name.size();

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
  saddr_un->sun_len = saddr_un_len;
#endif

  return true;
}
} // namespace

DomainSocket::DomainSocket(bool should_close, bool child_processes_inherit)
    : Socket(ProtocolUnixDomain, should_close, child_processes_inherit) {}

DomainSocket::DomainSocket(SocketProtocol protocol,
                           bool child_processes_inherit)
    : Socket(protocol, true, child_processes_inherit) {}

DomainSocket::DomainSocket(NativeSocket socket,
                           const DomainSocket &listen_socket)
    : Socket(ProtocolUnixDomain, listen_socket.m_should_close_fd,
             listen_socket.m_child_processes_inherit) {
  m_socket = socket;
}

Status DomainSocket::Connect(llvm::StringRef name) {
  sockaddr_un saddr_un;
  socklen_t saddr_un_len;
  if (!SetSockAddr(name, GetNameOffset(), &saddr_un, saddr_un_len))
    return Status("Failed to set socket address");

  Status error;
  m_socket = CreateSocket(kDomain, kType, 0, m_child_processes_inherit, error);
  if (error.Fail())
    return error;
  if (::connect(GetNativeSocket(), (struct sockaddr *)&saddr_un, saddr_un_len) <
      0)
    SetLastError(error);

  return error;
}

Status DomainSocket::Listen(llvm::StringRef name, int backlog) {
  sockaddr_un saddr_un;
  socklen_t saddr_un_len;
  if (!SetSockAddr(name, GetNameOffset(), &saddr_un, saddr_un_len))
    return Status("Failed to set socket address");

  DeleteSocketFile(name);

  Status error;
  m_socket = CreateSocket(kDomain, kType, 0, m_child_processes_inherit, error);
  if (error.Fail())
    return error;
  if (::bind(GetNativeSocket(), (struct sockaddr *)&saddr_un, saddr_un_len) ==
      0)
    if (::listen(GetNativeSocket(), backlog) == 0)
      return error;

  SetLastError(error);
  return error;
}

Status DomainSocket::Accept(Socket *&socket) {
  Status error;
  auto conn_fd = AcceptSocket(GetNativeSocket(), nullptr, nullptr,
                              m_child_processes_inherit, error);
  if (error.Success())
    socket = new DomainSocket(conn_fd, *this);

  return error;
}

size_t DomainSocket::GetNameOffset() const { return 0; }

void DomainSocket::DeleteSocketFile(llvm::StringRef name) {
  llvm::sys::fs::remove(name);
}
