//===- OrcRemoteTargetRPCAPI.h - Orc Remote-target RPC API ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Orc remote-target RPC API. It should not be used
// directly, but is used by the RemoteTargetClient and RemoteTargetServer
// classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETRPCAPI_H
#define LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETRPCAPI_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/RPCUtils.h"
#include "llvm/ExecutionEngine/Orc/RawByteChannel.h"

namespace llvm {
namespace orc {

namespace remote {

/// Template error for missing resources.
template <typename ResourceIdT>
class ResourceNotFound
  : public ErrorInfo<ResourceNotFound<ResourceIdT>> {
public:
  static char ID;

  ResourceNotFound(ResourceIdT ResourceId,
                   std::string ResourceDescription = "")
    : ResourceId(std::move(ResourceId)),
      ResourceDescription(std::move(ResourceDescription)) {}

  std::error_code convertToErrorCode() const override {
    return orcError(OrcErrorCode::UnknownResourceHandle);
  }

  void log(raw_ostream &OS) const override {
    OS << (ResourceDescription.empty()
             ? "Remote resource with id "
               : ResourceDescription)
       << " " << ResourceId << " not found";
  }

private:
  ResourceIdT ResourceId;
  std::string ResourceDescription;
};

template <typename ResourceIdT>
char ResourceNotFound<ResourceIdT>::ID = 0;

class DirectBufferWriter {
public:
  DirectBufferWriter() = default;
  DirectBufferWriter(const char *Src, JITTargetAddress Dst, uint64_t Size)
      : Src(Src), Dst(Dst), Size(Size) {}

  const char *getSrc() const { return Src; }
  JITTargetAddress getDst() const { return Dst; }
  uint64_t getSize() const { return Size; }

private:
  const char *Src;
  JITTargetAddress Dst;
  uint64_t Size;
};

} // end namespace remote

namespace rpc {

template <>
class RPCTypeName<JITSymbolFlags> {
public:
  static const char *getName() { return "JITSymbolFlags"; }
};

template <typename ChannelT>
class SerializationTraits<ChannelT, JITSymbolFlags> {
public:

  static Error serialize(ChannelT &C, const JITSymbolFlags &Flags) {
    return serializeSeq(C, Flags.getRawFlagsValue(), Flags.getTargetFlags());
  }

  static Error deserialize(ChannelT &C, JITSymbolFlags &Flags) {
    JITSymbolFlags::UnderlyingType JITFlags;
    JITSymbolFlags::TargetFlagsType TargetFlags;
    if (auto Err = deserializeSeq(C, JITFlags, TargetFlags))
      return Err;
    Flags = JITSymbolFlags(static_cast<JITSymbolFlags::FlagNames>(JITFlags),
                           TargetFlags);
    return Error::success();
  }
};

template <> class RPCTypeName<remote::DirectBufferWriter> {
public:
  static const char *getName() { return "DirectBufferWriter"; }
};

template <typename ChannelT>
class SerializationTraits<
    ChannelT, remote::DirectBufferWriter, remote::DirectBufferWriter,
    typename std::enable_if<
        std::is_base_of<RawByteChannel, ChannelT>::value>::type> {
public:
  static Error serialize(ChannelT &C, const remote::DirectBufferWriter &DBW) {
    if (auto EC = serializeSeq(C, DBW.getDst()))
      return EC;
    if (auto EC = serializeSeq(C, DBW.getSize()))
      return EC;
    return C.appendBytes(DBW.getSrc(), DBW.getSize());
  }

  static Error deserialize(ChannelT &C, remote::DirectBufferWriter &DBW) {
    JITTargetAddress Dst;
    if (auto EC = deserializeSeq(C, Dst))
      return EC;
    uint64_t Size;
    if (auto EC = deserializeSeq(C, Size))
      return EC;
    char *Addr = reinterpret_cast<char *>(static_cast<uintptr_t>(Dst));

    DBW = remote::DirectBufferWriter(nullptr, Dst, Size);

    return C.readBytes(Addr, Size);
  }
};

} // end namespace rpc

namespace remote {

class ResourceIdMgr {
public:
  using ResourceId = uint64_t;
  static const ResourceId InvalidId = ~0U;

  ResourceIdMgr() = default;
  explicit ResourceIdMgr(ResourceId FirstValidId)
    : NextId(std::move(FirstValidId)) {}

  ResourceId getNext() {
    if (!FreeIds.empty()) {
      ResourceId I = FreeIds.back();
      FreeIds.pop_back();
      return I;
    }
    assert(NextId + 1 != ~0ULL && "All ids allocated");
    return NextId++;
  }

  void release(ResourceId I) { FreeIds.push_back(I); }

private:
  ResourceId NextId = 1;
  std::vector<ResourceId> FreeIds;
};

/// Registers EH frames on the remote.
namespace eh {

  /// Registers EH frames on the remote.
  class RegisterEHFrames
      : public rpc::Function<RegisterEHFrames,
                             void(JITTargetAddress Addr, uint32_t Size)> {
  public:
    static const char *getName() { return "RegisterEHFrames"; }
  };

  /// Deregisters EH frames on the remote.
  class DeregisterEHFrames
      : public rpc::Function<DeregisterEHFrames,
                             void(JITTargetAddress Addr, uint32_t Size)> {
  public:
    static const char *getName() { return "DeregisterEHFrames"; }
  };

} // end namespace eh

/// RPC functions for executing remote code.
namespace exec {

  /// Call an 'int32_t()'-type function on the remote, returns the called
  /// function's return value.
  class CallIntVoid
      : public rpc::Function<CallIntVoid, int32_t(JITTargetAddress Addr)> {
  public:
    static const char *getName() { return "CallIntVoid"; }
  };

  /// Call an 'int32_t(int32_t, char**)'-type function on the remote, returns the
  /// called function's return value.
  class CallMain
      : public rpc::Function<CallMain, int32_t(JITTargetAddress Addr,
                                               std::vector<std::string> Args)> {
  public:
    static const char *getName() { return "CallMain"; }
  };

  /// Calls a 'void()'-type function on the remote, returns when the called
  /// function completes.
  class CallVoidVoid
      : public rpc::Function<CallVoidVoid, void(JITTargetAddress FnAddr)> {
  public:
    static const char *getName() { return "CallVoidVoid"; }
  };

} // end namespace exec

/// RPC functions for remote memory management / inspection / modification.
namespace mem {

  /// Creates a memory allocator on the remote.
  class CreateRemoteAllocator
      : public rpc::Function<CreateRemoteAllocator,
                             void(ResourceIdMgr::ResourceId AllocatorID)> {
  public:
    static const char *getName() { return "CreateRemoteAllocator"; }
  };

  /// Destroys a remote allocator, freeing any memory allocated by it.
  class DestroyRemoteAllocator
      : public rpc::Function<DestroyRemoteAllocator,
                             void(ResourceIdMgr::ResourceId AllocatorID)> {
  public:
    static const char *getName() { return "DestroyRemoteAllocator"; }
  };

  /// Read a remote memory block.
  class ReadMem
      : public rpc::Function<ReadMem, std::vector<uint8_t>(JITTargetAddress Src,
                                                           uint64_t Size)> {
  public:
    static const char *getName() { return "ReadMem"; }
  };

  /// Reserve a block of memory on the remote via the given allocator.
  class ReserveMem
      : public rpc::Function<ReserveMem,
                             JITTargetAddress(ResourceIdMgr::ResourceId AllocID,
                                              uint64_t Size, uint32_t Align)> {
  public:
    static const char *getName() { return "ReserveMem"; }
  };

  /// Set the memory protection on a memory block.
  class SetProtections
      : public rpc::Function<SetProtections,
                             void(ResourceIdMgr::ResourceId AllocID,
                                  JITTargetAddress Dst, uint32_t ProtFlags)> {
  public:
    static const char *getName() { return "SetProtections"; }
  };

  /// Write to a remote memory block.
  class WriteMem
      : public rpc::Function<WriteMem, void(remote::DirectBufferWriter DB)> {
  public:
    static const char *getName() { return "WriteMem"; }
  };

  /// Write to a remote pointer.
  class WritePtr : public rpc::Function<WritePtr, void(JITTargetAddress Dst,
                                                       JITTargetAddress Val)> {
  public:
    static const char *getName() { return "WritePtr"; }
  };

} // end namespace mem

/// RPC functions for remote stub and trampoline management.
namespace stubs {

  /// Creates an indirect stub owner on the remote.
  class CreateIndirectStubsOwner
      : public rpc::Function<CreateIndirectStubsOwner,
                             void(ResourceIdMgr::ResourceId StubOwnerID)> {
  public:
    static const char *getName() { return "CreateIndirectStubsOwner"; }
  };

  /// RPC function for destroying an indirect stubs owner.
  class DestroyIndirectStubsOwner
      : public rpc::Function<DestroyIndirectStubsOwner,
                             void(ResourceIdMgr::ResourceId StubsOwnerID)> {
  public:
    static const char *getName() { return "DestroyIndirectStubsOwner"; }
  };

  /// EmitIndirectStubs result is (StubsBase, PtrsBase, NumStubsEmitted).
  class EmitIndirectStubs
      : public rpc::Function<
            EmitIndirectStubs,
            std::tuple<JITTargetAddress, JITTargetAddress, uint32_t>(
                ResourceIdMgr::ResourceId StubsOwnerID,
                uint32_t NumStubsRequired)> {
  public:
    static const char *getName() { return "EmitIndirectStubs"; }
  };

  /// RPC function to emit the resolver block and return its address.
  class EmitResolverBlock : public rpc::Function<EmitResolverBlock, void()> {
  public:
    static const char *getName() { return "EmitResolverBlock"; }
  };

  /// EmitTrampolineBlock result is (BlockAddr, NumTrampolines).
  class EmitTrampolineBlock
      : public rpc::Function<EmitTrampolineBlock,
                             std::tuple<JITTargetAddress, uint32_t>()> {
  public:
    static const char *getName() { return "EmitTrampolineBlock"; }
  };

} // end namespace stubs

/// Miscelaneous RPC functions for dealing with remotes.
namespace utils {

  /// GetRemoteInfo result is (Triple, PointerSize, PageSize, TrampolineSize,
  ///                          IndirectStubsSize).
  class GetRemoteInfo
      : public rpc::Function<
            GetRemoteInfo,
            std::tuple<std::string, uint32_t, uint32_t, uint32_t, uint32_t>()> {
  public:
    static const char *getName() { return "GetRemoteInfo"; }
  };

  /// Get the address of a remote symbol.
  class GetSymbolAddress
      : public rpc::Function<GetSymbolAddress,
                             JITTargetAddress(std::string SymbolName)> {
  public:
    static const char *getName() { return "GetSymbolAddress"; }
  };

  /// Request that the host execute a compile callback.
  class RequestCompile
      : public rpc::Function<
            RequestCompile, JITTargetAddress(JITTargetAddress TrampolineAddr)> {
  public:
    static const char *getName() { return "RequestCompile"; }
  };

  /// Notify the remote and terminate the session.
  class TerminateSession : public rpc::Function<TerminateSession, void()> {
  public:
    static const char *getName() { return "TerminateSession"; }
  };

} // namespace utils

class OrcRemoteTargetRPCAPI
    : public rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel> {
public:
  // FIXME: Remove constructors once MSVC supports synthesizing move-ops.
  OrcRemoteTargetRPCAPI(rpc::RawByteChannel &C)
      : rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel>(C, true) {}
};

} // end namespace remote

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETRPCAPI_H
