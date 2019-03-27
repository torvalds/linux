//===-- DomainSocket.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DomainSocket_h_
#define liblldb_DomainSocket_h_

#include "lldb/Host/Socket.h"

namespace lldb_private {
class DomainSocket : public Socket {
public:
  DomainSocket(bool should_close, bool child_processes_inherit);

  Status Connect(llvm::StringRef name) override;
  Status Listen(llvm::StringRef name, int backlog) override;
  Status Accept(Socket *&socket) override;

protected:
  DomainSocket(SocketProtocol protocol, bool child_processes_inherit);

  virtual size_t GetNameOffset() const;
  virtual void DeleteSocketFile(llvm::StringRef name);

private:
  DomainSocket(NativeSocket socket, const DomainSocket &listen_socket);
};
}

#endif // ifndef liblldb_DomainSocket_h_
