//===------ OrcError.h - Reject symbol lookup requests ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//   Define an error category, error codes, and helper utilities for Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCERROR_H
#define LLVM_EXECUTIONENGINE_ORC_ORCERROR_H

#include "llvm/Support/Error.h"
#include <system_error>

namespace llvm {
namespace orc {

enum class OrcErrorCode : int {
  // RPC Errors
  UnknownORCError = 1,
  DuplicateDefinition,
  JITSymbolNotFound,
  RemoteAllocatorDoesNotExist,
  RemoteAllocatorIdAlreadyInUse,
  RemoteMProtectAddrUnrecognized,
  RemoteIndirectStubsOwnerDoesNotExist,
  RemoteIndirectStubsOwnerIdAlreadyInUse,
  RPCConnectionClosed,
  RPCCouldNotNegotiateFunction,
  RPCResponseAbandoned,
  UnexpectedRPCCall,
  UnexpectedRPCResponse,
  UnknownErrorCodeFromRemote,
  UnknownResourceHandle
};

std::error_code orcError(OrcErrorCode ErrCode);

class DuplicateDefinition : public ErrorInfo<DuplicateDefinition> {
public:
  static char ID;

  DuplicateDefinition(std::string SymbolName);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const std::string &getSymbolName() const;
private:
  std::string SymbolName;
};

class JITSymbolNotFound : public ErrorInfo<JITSymbolNotFound> {
public:
  static char ID;

  JITSymbolNotFound(std::string SymbolName);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const std::string &getSymbolName() const;
private:
  std::string SymbolName;
};

} // End namespace orc.
} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_ORCERROR_H
