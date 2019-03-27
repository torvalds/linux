//===-- SocketAddress.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SocketAddress_h_
#define liblldb_SocketAddress_h_

#include <stdint.h>

#ifdef _WIN32
#include "lldb/Host/windows/windows.h"
#include <winsock2.h>
#include <ws2tcpip.h>
typedef ADDRESS_FAMILY sa_family_t;
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#if defined(__FreeBSD__)
#include <sys/types.h>
#endif

#include <string>
#include <vector>

namespace lldb_private {

class SocketAddress {
public:
  //----------------------------------------------------------------------------
  // Static method to get all address information for a host and/or service
  //----------------------------------------------------------------------------
  static std::vector<SocketAddress>
  GetAddressInfo(const char *hostname, const char *servname, int ai_family,
                 int ai_socktype, int ai_protocol, int ai_flags = 0);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  SocketAddress();
  SocketAddress(const struct addrinfo *addr_info);
  SocketAddress(const struct sockaddr &s);
  SocketAddress(const struct sockaddr_in &s);
  SocketAddress(const struct sockaddr_in6 &s);
  SocketAddress(const struct sockaddr_storage &s);
  SocketAddress(const SocketAddress &rhs);
  ~SocketAddress();

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------
  const SocketAddress &operator=(const SocketAddress &rhs);

  const SocketAddress &operator=(const struct addrinfo *addr_info);

  const SocketAddress &operator=(const struct sockaddr &s);

  const SocketAddress &operator=(const struct sockaddr_in &s);

  const SocketAddress &operator=(const struct sockaddr_in6 &s);

  const SocketAddress &operator=(const struct sockaddr_storage &s);

  bool operator==(const SocketAddress &rhs) const;
  bool operator!=(const SocketAddress &rhs) const;

  //------------------------------------------------------------------
  // Clear the contents of this socket address
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  // Get the length for the current socket address family
  //------------------------------------------------------------------
  socklen_t GetLength() const;

  //------------------------------------------------------------------
  // Get the max length for the largest socket address supported.
  //------------------------------------------------------------------
  static socklen_t GetMaxLength();

  //------------------------------------------------------------------
  // Get the socket address family
  //------------------------------------------------------------------
  sa_family_t GetFamily() const;

  //------------------------------------------------------------------
  // Set the socket address family
  //------------------------------------------------------------------
  void SetFamily(sa_family_t family);

  //------------------------------------------------------------------
  // Get the address
  //------------------------------------------------------------------
  std::string GetIPAddress() const;

  //------------------------------------------------------------------
  // Get the port if the socket address for the family has a port
  //------------------------------------------------------------------
  uint16_t GetPort() const;

  //------------------------------------------------------------------
  // Set the port if the socket address for the family has a port. The family
  // must be set correctly prior to calling this function.
  //------------------------------------------------------------------
  bool SetPort(uint16_t port);

  //------------------------------------------------------------------
  // Set the socket address according to the first match from a call to
  // getaddrinfo() (or equivalent functions for systems that don't have
  // getaddrinfo(). If "addr_info_ptr" is not NULL, it will get filled in with
  // the match that was used to populate this socket address.
  //------------------------------------------------------------------
  bool
  getaddrinfo(const char *host,    // Hostname ("foo.bar.com" or "foo" or IP
                                   // address string ("123.234.12.1" or
                                   // "2001:0db8:85a3:0000:0000:8a2e:0370:7334")
              const char *service, // Protocol name ("tcp", "http", etc) or a
                                   // raw port number string ("81")
              int ai_family = PF_UNSPEC, int ai_socktype = 0,
              int ai_protocol = 0, int ai_flags = 0);

  //------------------------------------------------------------------
  // Quick way to set the SocketAddress to localhost given the family. Returns
  // true if successful, false if "family" doesn't support localhost or if
  // "family" is not supported by this class.
  //------------------------------------------------------------------
  bool SetToLocalhost(sa_family_t family, uint16_t port);

  bool SetToAnyAddress(sa_family_t family, uint16_t port);

  //------------------------------------------------------------------
  // Returns true if there is a valid socket address in this object.
  //------------------------------------------------------------------
  bool IsValid() const;

  //------------------------------------------------------------------
  // Returns true if the socket is INADDR_ANY
  //------------------------------------------------------------------
  bool IsAnyAddr() const;

  //------------------------------------------------------------------
  // Returns true if the socket is INADDR_LOOPBACK
  //------------------------------------------------------------------
  bool IsLocalhost() const;

  //------------------------------------------------------------------
  // Direct access to all of the sockaddr structures
  //------------------------------------------------------------------
  struct sockaddr &sockaddr() {
    return m_socket_addr.sa;
  }

  const struct sockaddr &sockaddr() const { return m_socket_addr.sa; }

  struct sockaddr_in &sockaddr_in() {
    return m_socket_addr.sa_ipv4;
  }

  const struct sockaddr_in &sockaddr_in() const {
    return m_socket_addr.sa_ipv4;
  }

  struct sockaddr_in6 &sockaddr_in6() {
    return m_socket_addr.sa_ipv6;
  }

  const struct sockaddr_in6 &sockaddr_in6() const {
    return m_socket_addr.sa_ipv6;
  }

  struct sockaddr_storage &sockaddr_storage() {
    return m_socket_addr.sa_storage;
  }

  const struct sockaddr_storage &sockaddr_storage() const {
    return m_socket_addr.sa_storage;
  }

  //------------------------------------------------------------------
  // Conversion operators to allow getting the contents of this class as a
  // pointer to the appropriate structure. This allows an instance of this
  // class to be used in calls that take one of the sockaddr structure variants
  // without having to manually use the correct accessor function.
  //------------------------------------------------------------------

  operator struct sockaddr *() { return &m_socket_addr.sa; }

  operator const struct sockaddr *() const { return &m_socket_addr.sa; }

  operator struct sockaddr_in *() { return &m_socket_addr.sa_ipv4; }

  operator const struct sockaddr_in *() const { return &m_socket_addr.sa_ipv4; }

  operator struct sockaddr_in6 *() { return &m_socket_addr.sa_ipv6; }

  operator const struct sockaddr_in6 *() const {
    return &m_socket_addr.sa_ipv6;
  }

  operator const struct sockaddr_storage *() const {
    return &m_socket_addr.sa_storage;
  }

  operator struct sockaddr_storage *() { return &m_socket_addr.sa_storage; }

protected:
  typedef union sockaddr_tag {
    struct sockaddr sa;
    struct sockaddr_in sa_ipv4;
    struct sockaddr_in6 sa_ipv6;
    struct sockaddr_storage sa_storage;
  } sockaddr_t;

  //------------------------------------------------------------------
  // Classes that inherit from SocketAddress can see and modify these
  //------------------------------------------------------------------
  sockaddr_t m_socket_addr;
};

} // namespace lldb_private

#endif // liblldb_SocketAddress_h_
