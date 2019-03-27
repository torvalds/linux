//===---------------- OrcError.cpp - Error codes for ORC ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Error codes for ORC.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/OrcError.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;
using namespace llvm::orc;

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class OrcErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "orc"; }

  std::string message(int condition) const override {
    switch (static_cast<OrcErrorCode>(condition)) {
    case OrcErrorCode::UnknownORCError:
      return "Unknown ORC error";
    case OrcErrorCode::DuplicateDefinition:
      return "Duplicate symbol definition";
    case OrcErrorCode::JITSymbolNotFound:
      return "JIT symbol not found";
    case OrcErrorCode::RemoteAllocatorDoesNotExist:
      return "Remote allocator does not exist";
    case OrcErrorCode::RemoteAllocatorIdAlreadyInUse:
      return "Remote allocator Id already in use";
    case OrcErrorCode::RemoteMProtectAddrUnrecognized:
      return "Remote mprotect call references unallocated memory";
    case OrcErrorCode::RemoteIndirectStubsOwnerDoesNotExist:
      return "Remote indirect stubs owner does not exist";
    case OrcErrorCode::RemoteIndirectStubsOwnerIdAlreadyInUse:
      return "Remote indirect stubs owner Id already in use";
    case OrcErrorCode::RPCConnectionClosed:
      return "RPC connection closed";
    case OrcErrorCode::RPCCouldNotNegotiateFunction:
      return "Could not negotiate RPC function";
    case OrcErrorCode::RPCResponseAbandoned:
      return "RPC response abandoned";
    case OrcErrorCode::UnexpectedRPCCall:
      return "Unexpected RPC call";
    case OrcErrorCode::UnexpectedRPCResponse:
      return "Unexpected RPC response";
    case OrcErrorCode::UnknownErrorCodeFromRemote:
      return "Unknown error returned from remote RPC function "
             "(Use StringError to get error message)";
    case OrcErrorCode::UnknownResourceHandle:
      return "Unknown resource handle";
    }
    llvm_unreachable("Unhandled error code");
  }
};

static ManagedStatic<OrcErrorCategory> OrcErrCat;
}

namespace llvm {
namespace orc {

char DuplicateDefinition::ID = 0;
char JITSymbolNotFound::ID = 0;

std::error_code orcError(OrcErrorCode ErrCode) {
  typedef std::underlying_type<OrcErrorCode>::type UT;
  return std::error_code(static_cast<UT>(ErrCode), *OrcErrCat);
}


DuplicateDefinition::DuplicateDefinition(std::string SymbolName)
  : SymbolName(std::move(SymbolName)) {}

std::error_code DuplicateDefinition::convertToErrorCode() const {
  return orcError(OrcErrorCode::DuplicateDefinition);
}

void DuplicateDefinition::log(raw_ostream &OS) const {
  OS << "Duplicate definition of symbol '" << SymbolName << "'";
}

const std::string &DuplicateDefinition::getSymbolName() const {
  return SymbolName;
}

JITSymbolNotFound::JITSymbolNotFound(std::string SymbolName)
  : SymbolName(std::move(SymbolName)) {}

std::error_code JITSymbolNotFound::convertToErrorCode() const {
  typedef std::underlying_type<OrcErrorCode>::type UT;
  return std::error_code(static_cast<UT>(OrcErrorCode::JITSymbolNotFound),
                         *OrcErrCat);
}

void JITSymbolNotFound::log(raw_ostream &OS) const {
  OS << "Could not find symbol '" << SymbolName << "'";
}

const std::string &JITSymbolNotFound::getSymbolName() const {
  return SymbolName;
}

}
}
