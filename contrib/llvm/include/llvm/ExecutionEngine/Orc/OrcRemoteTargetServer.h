//===- OrcRemoteTargetServer.h - Orc Remote-target Server -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the OrcRemoteTargetServer class. It can be used to build a
// JIT server that can execute code sent from an OrcRemoteTargetClient.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETSERVER_H
#define LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETSERVER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/OrcError.h"
#include "llvm/ExecutionEngine/Orc/OrcRemoteTargetRPCAPI.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <vector>

#define DEBUG_TYPE "orc-remote"

namespace llvm {
namespace orc {
namespace remote {

template <typename ChannelT, typename TargetT>
class OrcRemoteTargetServer
    : public rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel> {
public:
  using SymbolLookupFtor =
      std::function<JITTargetAddress(const std::string &Name)>;

  using EHFrameRegistrationFtor =
      std::function<void(uint8_t *Addr, uint32_t Size)>;

  OrcRemoteTargetServer(ChannelT &Channel, SymbolLookupFtor SymbolLookup,
                        EHFrameRegistrationFtor EHFramesRegister,
                        EHFrameRegistrationFtor EHFramesDeregister)
      : rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel>(Channel, true),
        SymbolLookup(std::move(SymbolLookup)),
        EHFramesRegister(std::move(EHFramesRegister)),
        EHFramesDeregister(std::move(EHFramesDeregister)) {
    using ThisT = typename std::remove_reference<decltype(*this)>::type;
    addHandler<exec::CallIntVoid>(*this, &ThisT::handleCallIntVoid);
    addHandler<exec::CallMain>(*this, &ThisT::handleCallMain);
    addHandler<exec::CallVoidVoid>(*this, &ThisT::handleCallVoidVoid);
    addHandler<mem::CreateRemoteAllocator>(*this,
                                           &ThisT::handleCreateRemoteAllocator);
    addHandler<mem::DestroyRemoteAllocator>(
        *this, &ThisT::handleDestroyRemoteAllocator);
    addHandler<mem::ReadMem>(*this, &ThisT::handleReadMem);
    addHandler<mem::ReserveMem>(*this, &ThisT::handleReserveMem);
    addHandler<mem::SetProtections>(*this, &ThisT::handleSetProtections);
    addHandler<mem::WriteMem>(*this, &ThisT::handleWriteMem);
    addHandler<mem::WritePtr>(*this, &ThisT::handleWritePtr);
    addHandler<eh::RegisterEHFrames>(*this, &ThisT::handleRegisterEHFrames);
    addHandler<eh::DeregisterEHFrames>(*this, &ThisT::handleDeregisterEHFrames);
    addHandler<stubs::CreateIndirectStubsOwner>(
        *this, &ThisT::handleCreateIndirectStubsOwner);
    addHandler<stubs::DestroyIndirectStubsOwner>(
        *this, &ThisT::handleDestroyIndirectStubsOwner);
    addHandler<stubs::EmitIndirectStubs>(*this,
                                         &ThisT::handleEmitIndirectStubs);
    addHandler<stubs::EmitResolverBlock>(*this,
                                         &ThisT::handleEmitResolverBlock);
    addHandler<stubs::EmitTrampolineBlock>(*this,
                                           &ThisT::handleEmitTrampolineBlock);
    addHandler<utils::GetSymbolAddress>(*this, &ThisT::handleGetSymbolAddress);
    addHandler<utils::GetRemoteInfo>(*this, &ThisT::handleGetRemoteInfo);
    addHandler<utils::TerminateSession>(*this, &ThisT::handleTerminateSession);
  }

  // FIXME: Remove move/copy ops once MSVC supports synthesizing move ops.
  OrcRemoteTargetServer(const OrcRemoteTargetServer &) = delete;
  OrcRemoteTargetServer &operator=(const OrcRemoteTargetServer &) = delete;

  OrcRemoteTargetServer(OrcRemoteTargetServer &&Other) = default;
  OrcRemoteTargetServer &operator=(OrcRemoteTargetServer &&) = delete;

  Expected<JITTargetAddress> requestCompile(JITTargetAddress TrampolineAddr) {
    return callB<utils::RequestCompile>(TrampolineAddr);
  }

  bool receivedTerminate() const { return TerminateFlag; }

private:
  struct Allocator {
    Allocator() = default;
    Allocator(Allocator &&Other) : Allocs(std::move(Other.Allocs)) {}

    Allocator &operator=(Allocator &&Other) {
      Allocs = std::move(Other.Allocs);
      return *this;
    }

    ~Allocator() {
      for (auto &Alloc : Allocs)
        sys::Memory::releaseMappedMemory(Alloc.second);
    }

    Error allocate(void *&Addr, size_t Size, uint32_t Align) {
      std::error_code EC;
      sys::MemoryBlock MB = sys::Memory::allocateMappedMemory(
          Size, nullptr, sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC);
      if (EC)
        return errorCodeToError(EC);

      Addr = MB.base();
      assert(Allocs.find(MB.base()) == Allocs.end() && "Duplicate alloc");
      Allocs[MB.base()] = std::move(MB);
      return Error::success();
    }

    Error setProtections(void *block, unsigned Flags) {
      auto I = Allocs.find(block);
      if (I == Allocs.end())
        return errorCodeToError(orcError(OrcErrorCode::RemoteMProtectAddrUnrecognized));
      return errorCodeToError(
          sys::Memory::protectMappedMemory(I->second, Flags));
    }

  private:
    std::map<void *, sys::MemoryBlock> Allocs;
  };

  static Error doNothing() { return Error::success(); }

  static JITTargetAddress reenter(void *JITTargetAddr, void *TrampolineAddr) {
    auto T = static_cast<OrcRemoteTargetServer *>(JITTargetAddr);
    auto AddrOrErr = T->requestCompile(static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(TrampolineAddr)));
    // FIXME: Allow customizable failure substitution functions.
    assert(AddrOrErr && "Compile request failed");
    return *AddrOrErr;
  }

  Expected<int32_t> handleCallIntVoid(JITTargetAddress Addr) {
    using IntVoidFnTy = int (*)();

    IntVoidFnTy Fn =
        reinterpret_cast<IntVoidFnTy>(static_cast<uintptr_t>(Addr));

    LLVM_DEBUG(dbgs() << "  Calling " << format("0x%016x", Addr) << "\n");
    int Result = Fn();
    LLVM_DEBUG(dbgs() << "  Result = " << Result << "\n");

    return Result;
  }

  Expected<int32_t> handleCallMain(JITTargetAddress Addr,
                                   std::vector<std::string> Args) {
    using MainFnTy = int (*)(int, const char *[]);

    MainFnTy Fn = reinterpret_cast<MainFnTy>(static_cast<uintptr_t>(Addr));
    int ArgC = Args.size() + 1;
    int Idx = 1;
    std::unique_ptr<const char *[]> ArgV(new const char *[ArgC + 1]);
    ArgV[0] = "<jit process>";
    for (auto &Arg : Args)
      ArgV[Idx++] = Arg.c_str();
    ArgV[ArgC] = 0;
    LLVM_DEBUG(for (int Idx = 0; Idx < ArgC; ++Idx) {
      llvm::dbgs() << "Arg " << Idx << ": " << ArgV[Idx] << "\n";
    });

    LLVM_DEBUG(dbgs() << "  Calling " << format("0x%016x", Addr) << "\n");
    int Result = Fn(ArgC, ArgV.get());
    LLVM_DEBUG(dbgs() << "  Result = " << Result << "\n");

    return Result;
  }

  Error handleCallVoidVoid(JITTargetAddress Addr) {
    using VoidVoidFnTy = void (*)();

    VoidVoidFnTy Fn =
        reinterpret_cast<VoidVoidFnTy>(static_cast<uintptr_t>(Addr));

    LLVM_DEBUG(dbgs() << "  Calling " << format("0x%016x", Addr) << "\n");
    Fn();
    LLVM_DEBUG(dbgs() << "  Complete.\n");

    return Error::success();
  }

  Error handleCreateRemoteAllocator(ResourceIdMgr::ResourceId Id) {
    auto I = Allocators.find(Id);
    if (I != Allocators.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteAllocatorIdAlreadyInUse));
    LLVM_DEBUG(dbgs() << "  Created allocator " << Id << "\n");
    Allocators[Id] = Allocator();
    return Error::success();
  }

  Error handleCreateIndirectStubsOwner(ResourceIdMgr::ResourceId Id) {
    auto I = IndirectStubsOwners.find(Id);
    if (I != IndirectStubsOwners.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteIndirectStubsOwnerIdAlreadyInUse));
    LLVM_DEBUG(dbgs() << "  Create indirect stubs owner " << Id << "\n");
    IndirectStubsOwners[Id] = ISBlockOwnerList();
    return Error::success();
  }

  Error handleDeregisterEHFrames(JITTargetAddress TAddr, uint32_t Size) {
    uint8_t *Addr = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(TAddr));
    LLVM_DEBUG(dbgs() << "  Registering EH frames at "
                      << format("0x%016x", TAddr) << ", Size = " << Size
                      << " bytes\n");
    EHFramesDeregister(Addr, Size);
    return Error::success();
  }

  Error handleDestroyRemoteAllocator(ResourceIdMgr::ResourceId Id) {
    auto I = Allocators.find(Id);
    if (I == Allocators.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteAllocatorDoesNotExist));
    Allocators.erase(I);
    LLVM_DEBUG(dbgs() << "  Destroyed allocator " << Id << "\n");
    return Error::success();
  }

  Error handleDestroyIndirectStubsOwner(ResourceIdMgr::ResourceId Id) {
    auto I = IndirectStubsOwners.find(Id);
    if (I == IndirectStubsOwners.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteIndirectStubsOwnerDoesNotExist));
    IndirectStubsOwners.erase(I);
    return Error::success();
  }

  Expected<std::tuple<JITTargetAddress, JITTargetAddress, uint32_t>>
  handleEmitIndirectStubs(ResourceIdMgr::ResourceId Id,
                          uint32_t NumStubsRequired) {
    LLVM_DEBUG(dbgs() << "  ISMgr " << Id << " request " << NumStubsRequired
                      << " stubs.\n");

    auto StubOwnerItr = IndirectStubsOwners.find(Id);
    if (StubOwnerItr == IndirectStubsOwners.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteIndirectStubsOwnerDoesNotExist));

    typename TargetT::IndirectStubsInfo IS;
    if (auto Err =
            TargetT::emitIndirectStubsBlock(IS, NumStubsRequired, nullptr))
      return std::move(Err);

    JITTargetAddress StubsBase = static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(IS.getStub(0)));
    JITTargetAddress PtrsBase = static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(IS.getPtr(0)));
    uint32_t NumStubsEmitted = IS.getNumStubs();

    auto &BlockList = StubOwnerItr->second;
    BlockList.push_back(std::move(IS));

    return std::make_tuple(StubsBase, PtrsBase, NumStubsEmitted);
  }

  Error handleEmitResolverBlock() {
    std::error_code EC;
    ResolverBlock = sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
        TargetT::ResolverCodeSize, nullptr,
        sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC)
      return errorCodeToError(EC);

    TargetT::writeResolverCode(static_cast<uint8_t *>(ResolverBlock.base()),
                               &reenter, this);

    return errorCodeToError(sys::Memory::protectMappedMemory(
        ResolverBlock.getMemoryBlock(),
        sys::Memory::MF_READ | sys::Memory::MF_EXEC));
  }

  Expected<std::tuple<JITTargetAddress, uint32_t>> handleEmitTrampolineBlock() {
    std::error_code EC;
    auto TrampolineBlock =
        sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
            sys::Process::getPageSize(), nullptr,
            sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC)
      return errorCodeToError(EC);

    uint32_t NumTrampolines =
        (sys::Process::getPageSize() - TargetT::PointerSize) /
        TargetT::TrampolineSize;

    uint8_t *TrampolineMem = static_cast<uint8_t *>(TrampolineBlock.base());
    TargetT::writeTrampolines(TrampolineMem, ResolverBlock.base(),
                              NumTrampolines);

    EC = sys::Memory::protectMappedMemory(TrampolineBlock.getMemoryBlock(),
                                          sys::Memory::MF_READ |
                                              sys::Memory::MF_EXEC);

    TrampolineBlocks.push_back(std::move(TrampolineBlock));

    auto TrampolineBaseAddr = static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(TrampolineMem));

    return std::make_tuple(TrampolineBaseAddr, NumTrampolines);
  }

  Expected<JITTargetAddress> handleGetSymbolAddress(const std::string &Name) {
    JITTargetAddress Addr = SymbolLookup(Name);
    LLVM_DEBUG(dbgs() << "  Symbol '" << Name
                      << "' =  " << format("0x%016x", Addr) << "\n");
    return Addr;
  }

  Expected<std::tuple<std::string, uint32_t, uint32_t, uint32_t, uint32_t>>
  handleGetRemoteInfo() {
    std::string ProcessTriple = sys::getProcessTriple();
    uint32_t PointerSize = TargetT::PointerSize;
    uint32_t PageSize = sys::Process::getPageSize();
    uint32_t TrampolineSize = TargetT::TrampolineSize;
    uint32_t IndirectStubSize = TargetT::IndirectStubsInfo::StubSize;
    LLVM_DEBUG(dbgs() << "  Remote info:\n"
                      << "    triple             = '" << ProcessTriple << "'\n"
                      << "    pointer size       = " << PointerSize << "\n"
                      << "    page size          = " << PageSize << "\n"
                      << "    trampoline size    = " << TrampolineSize << "\n"
                      << "    indirect stub size = " << IndirectStubSize
                      << "\n");
    return std::make_tuple(ProcessTriple, PointerSize, PageSize, TrampolineSize,
                           IndirectStubSize);
  }

  Expected<std::vector<uint8_t>> handleReadMem(JITTargetAddress RSrc,
                                               uint64_t Size) {
    uint8_t *Src = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(RSrc));

    LLVM_DEBUG(dbgs() << "  Reading " << Size << " bytes from "
                      << format("0x%016x", RSrc) << "\n");

    std::vector<uint8_t> Buffer;
    Buffer.resize(Size);
    for (uint8_t *P = Src; Size != 0; --Size)
      Buffer.push_back(*P++);

    return Buffer;
  }

  Error handleRegisterEHFrames(JITTargetAddress TAddr, uint32_t Size) {
    uint8_t *Addr = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(TAddr));
    LLVM_DEBUG(dbgs() << "  Registering EH frames at "
                      << format("0x%016x", TAddr) << ", Size = " << Size
                      << " bytes\n");
    EHFramesRegister(Addr, Size);
    return Error::success();
  }

  Expected<JITTargetAddress> handleReserveMem(ResourceIdMgr::ResourceId Id,
                                              uint64_t Size, uint32_t Align) {
    auto I = Allocators.find(Id);
    if (I == Allocators.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteAllocatorDoesNotExist));
    auto &Allocator = I->second;
    void *LocalAllocAddr = nullptr;
    if (auto Err = Allocator.allocate(LocalAllocAddr, Size, Align))
      return std::move(Err);

    LLVM_DEBUG(dbgs() << "  Allocator " << Id << " reserved " << LocalAllocAddr
                      << " (" << Size << " bytes, alignment " << Align
                      << ")\n");

    JITTargetAddress AllocAddr = static_cast<JITTargetAddress>(
        reinterpret_cast<uintptr_t>(LocalAllocAddr));

    return AllocAddr;
  }

  Error handleSetProtections(ResourceIdMgr::ResourceId Id,
                             JITTargetAddress Addr, uint32_t Flags) {
    auto I = Allocators.find(Id);
    if (I == Allocators.end())
      return errorCodeToError(
               orcError(OrcErrorCode::RemoteAllocatorDoesNotExist));
    auto &Allocator = I->second;
    void *LocalAddr = reinterpret_cast<void *>(static_cast<uintptr_t>(Addr));
    LLVM_DEBUG(dbgs() << "  Allocator " << Id << " set permissions on "
                      << LocalAddr << " to "
                      << (Flags & sys::Memory::MF_READ ? 'R' : '-')
                      << (Flags & sys::Memory::MF_WRITE ? 'W' : '-')
                      << (Flags & sys::Memory::MF_EXEC ? 'X' : '-') << "\n");
    return Allocator.setProtections(LocalAddr, Flags);
  }

  Error handleTerminateSession() {
    TerminateFlag = true;
    return Error::success();
  }

  Error handleWriteMem(DirectBufferWriter DBW) {
    LLVM_DEBUG(dbgs() << "  Writing " << DBW.getSize() << " bytes to "
                      << format("0x%016x", DBW.getDst()) << "\n");
    return Error::success();
  }

  Error handleWritePtr(JITTargetAddress Addr, JITTargetAddress PtrVal) {
    LLVM_DEBUG(dbgs() << "  Writing pointer *" << format("0x%016x", Addr)
                      << " = " << format("0x%016x", PtrVal) << "\n");
    uintptr_t *Ptr =
        reinterpret_cast<uintptr_t *>(static_cast<uintptr_t>(Addr));
    *Ptr = static_cast<uintptr_t>(PtrVal);
    return Error::success();
  }

  SymbolLookupFtor SymbolLookup;
  EHFrameRegistrationFtor EHFramesRegister, EHFramesDeregister;
  std::map<ResourceIdMgr::ResourceId, Allocator> Allocators;
  using ISBlockOwnerList = std::vector<typename TargetT::IndirectStubsInfo>;
  std::map<ResourceIdMgr::ResourceId, ISBlockOwnerList> IndirectStubsOwners;
  sys::OwningMemoryBlock ResolverBlock;
  std::vector<sys::OwningMemoryBlock> TrampolineBlocks;
  bool TerminateFlag = false;
};

} // end namespace remote
} // end namespace orc
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETSERVER_H
