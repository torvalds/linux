//===- OrcRemoteTargetClient.h - Orc Remote-target Client -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the OrcRemoteTargetClient class and helpers. This class
// can be used to communicate over an RawByteChannel with an
// OrcRemoteTargetServer instance to support remote-JITing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETCLIENT_H
#define LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETCLIENT_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/OrcRemoteTargetRPCAPI.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#define DEBUG_TYPE "orc-remote"

namespace llvm {
namespace orc {
namespace remote {

/// This class provides utilities (including memory manager, indirect stubs
/// manager, and compile callback manager types) that support remote JITing
/// in ORC.
///
/// Each of the utility classes talks to a JIT server (an instance of the
/// OrcRemoteTargetServer class) via an RPC system (see RPCUtils.h) to carry out
/// its actions.
class OrcRemoteTargetClient
    : public rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel> {
public:
  /// Remote-mapped RuntimeDyld-compatible memory manager.
  class RemoteRTDyldMemoryManager : public RuntimeDyld::MemoryManager {
    friend class OrcRemoteTargetClient;

  public:
    ~RemoteRTDyldMemoryManager() {
      Client.destroyRemoteAllocator(Id);
      LLVM_DEBUG(dbgs() << "Destroyed remote allocator " << Id << "\n");
    }

    RemoteRTDyldMemoryManager(const RemoteRTDyldMemoryManager &) = delete;
    RemoteRTDyldMemoryManager &
    operator=(const RemoteRTDyldMemoryManager &) = delete;
    RemoteRTDyldMemoryManager(RemoteRTDyldMemoryManager &&) = default;
    RemoteRTDyldMemoryManager &operator=(RemoteRTDyldMemoryManager &&) = delete;

    uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID,
                                 StringRef SectionName) override {
      Unmapped.back().CodeAllocs.emplace_back(Size, Alignment);
      uint8_t *Alloc = reinterpret_cast<uint8_t *>(
          Unmapped.back().CodeAllocs.back().getLocalAddress());
      LLVM_DEBUG(dbgs() << "Allocator " << Id << " allocated code for "
                        << SectionName << ": " << Alloc << " (" << Size
                        << " bytes, alignment " << Alignment << ")\n");
      return Alloc;
    }

    uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID, StringRef SectionName,
                                 bool IsReadOnly) override {
      if (IsReadOnly) {
        Unmapped.back().RODataAllocs.emplace_back(Size, Alignment);
        uint8_t *Alloc = reinterpret_cast<uint8_t *>(
            Unmapped.back().RODataAllocs.back().getLocalAddress());
        LLVM_DEBUG(dbgs() << "Allocator " << Id << " allocated ro-data for "
                          << SectionName << ": " << Alloc << " (" << Size
                          << " bytes, alignment " << Alignment << ")\n");
        return Alloc;
      } // else...

      Unmapped.back().RWDataAllocs.emplace_back(Size, Alignment);
      uint8_t *Alloc = reinterpret_cast<uint8_t *>(
          Unmapped.back().RWDataAllocs.back().getLocalAddress());
      LLVM_DEBUG(dbgs() << "Allocator " << Id << " allocated rw-data for "
                        << SectionName << ": " << Alloc << " (" << Size
                        << " bytes, alignment " << Alignment << ")\n");
      return Alloc;
    }

    void reserveAllocationSpace(uintptr_t CodeSize, uint32_t CodeAlign,
                                uintptr_t RODataSize, uint32_t RODataAlign,
                                uintptr_t RWDataSize,
                                uint32_t RWDataAlign) override {
      Unmapped.push_back(ObjectAllocs());

      LLVM_DEBUG(dbgs() << "Allocator " << Id << " reserved:\n");

      if (CodeSize != 0) {
        Unmapped.back().RemoteCodeAddr =
            Client.reserveMem(Id, CodeSize, CodeAlign);

        LLVM_DEBUG(
            dbgs() << "  code: "
                   << format("0x%016" PRIx64, Unmapped.back().RemoteCodeAddr)
                   << " (" << CodeSize << " bytes, alignment " << CodeAlign
                   << ")\n");
      }

      if (RODataSize != 0) {
        Unmapped.back().RemoteRODataAddr =
            Client.reserveMem(Id, RODataSize, RODataAlign);

        LLVM_DEBUG(
            dbgs() << "  ro-data: "
                   << format("0x%016" PRIx64, Unmapped.back().RemoteRODataAddr)
                   << " (" << RODataSize << " bytes, alignment " << RODataAlign
                   << ")\n");
      }

      if (RWDataSize != 0) {
        Unmapped.back().RemoteRWDataAddr =
            Client.reserveMem(Id, RWDataSize, RWDataAlign);

        LLVM_DEBUG(
            dbgs() << "  rw-data: "
                   << format("0x%016" PRIx64, Unmapped.back().RemoteRWDataAddr)
                   << " (" << RWDataSize << " bytes, alignment " << RWDataAlign
                   << ")\n");
      }
    }

    bool needsToReserveAllocationSpace() override { return true; }

    void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                          size_t Size) override {
      UnfinalizedEHFrames.push_back({LoadAddr, Size});
    }

    void deregisterEHFrames() override {
      for (auto &Frame : RegisteredEHFrames) {
        // FIXME: Add error poll.
        Client.deregisterEHFrames(Frame.Addr, Frame.Size);
      }
    }

    void notifyObjectLoaded(RuntimeDyld &Dyld,
                            const object::ObjectFile &Obj) override {
      LLVM_DEBUG(dbgs() << "Allocator " << Id << " applied mappings:\n");
      for (auto &ObjAllocs : Unmapped) {
        mapAllocsToRemoteAddrs(Dyld, ObjAllocs.CodeAllocs,
                               ObjAllocs.RemoteCodeAddr);
        mapAllocsToRemoteAddrs(Dyld, ObjAllocs.RODataAllocs,
                               ObjAllocs.RemoteRODataAddr);
        mapAllocsToRemoteAddrs(Dyld, ObjAllocs.RWDataAllocs,
                               ObjAllocs.RemoteRWDataAddr);
        Unfinalized.push_back(std::move(ObjAllocs));
      }
      Unmapped.clear();
    }

    bool finalizeMemory(std::string *ErrMsg = nullptr) override {
      LLVM_DEBUG(dbgs() << "Allocator " << Id << " finalizing:\n");

      for (auto &ObjAllocs : Unfinalized) {
        if (copyAndProtect(ObjAllocs.CodeAllocs, ObjAllocs.RemoteCodeAddr,
                           sys::Memory::MF_READ | sys::Memory::MF_EXEC))
          return true;

        if (copyAndProtect(ObjAllocs.RODataAllocs, ObjAllocs.RemoteRODataAddr,
                           sys::Memory::MF_READ))
          return true;

        if (copyAndProtect(ObjAllocs.RWDataAllocs, ObjAllocs.RemoteRWDataAddr,
                           sys::Memory::MF_READ | sys::Memory::MF_WRITE))
          return true;
      }
      Unfinalized.clear();

      for (auto &EHFrame : UnfinalizedEHFrames) {
        if (auto Err = Client.registerEHFrames(EHFrame.Addr, EHFrame.Size)) {
          // FIXME: Replace this once finalizeMemory can return an Error.
          handleAllErrors(std::move(Err), [&](ErrorInfoBase &EIB) {
            if (ErrMsg) {
              raw_string_ostream ErrOut(*ErrMsg);
              EIB.log(ErrOut);
            }
          });
          return false;
        }
      }
      RegisteredEHFrames = std::move(UnfinalizedEHFrames);
      UnfinalizedEHFrames = {};

      return false;
    }

  private:
    class Alloc {
    public:
      Alloc(uint64_t Size, unsigned Align)
          : Size(Size), Align(Align), Contents(new char[Size + Align - 1]) {}

      Alloc(const Alloc &) = delete;
      Alloc &operator=(const Alloc &) = delete;
      Alloc(Alloc &&) = default;
      Alloc &operator=(Alloc &&) = default;

      uint64_t getSize() const { return Size; }

      unsigned getAlign() const { return Align; }

      char *getLocalAddress() const {
        uintptr_t LocalAddr = reinterpret_cast<uintptr_t>(Contents.get());
        LocalAddr = alignTo(LocalAddr, Align);
        return reinterpret_cast<char *>(LocalAddr);
      }

      void setRemoteAddress(JITTargetAddress RemoteAddr) {
        this->RemoteAddr = RemoteAddr;
      }

      JITTargetAddress getRemoteAddress() const { return RemoteAddr; }

    private:
      uint64_t Size;
      unsigned Align;
      std::unique_ptr<char[]> Contents;
      JITTargetAddress RemoteAddr = 0;
    };

    struct ObjectAllocs {
      ObjectAllocs() = default;
      ObjectAllocs(const ObjectAllocs &) = delete;
      ObjectAllocs &operator=(const ObjectAllocs &) = delete;
      ObjectAllocs(ObjectAllocs &&) = default;
      ObjectAllocs &operator=(ObjectAllocs &&) = default;

      JITTargetAddress RemoteCodeAddr = 0;
      JITTargetAddress RemoteRODataAddr = 0;
      JITTargetAddress RemoteRWDataAddr = 0;
      std::vector<Alloc> CodeAllocs, RODataAllocs, RWDataAllocs;
    };

    RemoteRTDyldMemoryManager(OrcRemoteTargetClient &Client,
                              ResourceIdMgr::ResourceId Id)
        : Client(Client), Id(Id) {
      LLVM_DEBUG(dbgs() << "Created remote allocator " << Id << "\n");
    }

    // Maps all allocations in Allocs to aligned blocks
    void mapAllocsToRemoteAddrs(RuntimeDyld &Dyld, std::vector<Alloc> &Allocs,
                                JITTargetAddress NextAddr) {
      for (auto &Alloc : Allocs) {
        NextAddr = alignTo(NextAddr, Alloc.getAlign());
        Dyld.mapSectionAddress(Alloc.getLocalAddress(), NextAddr);
        LLVM_DEBUG(
            dbgs() << "     " << static_cast<void *>(Alloc.getLocalAddress())
                   << " -> " << format("0x%016" PRIx64, NextAddr) << "\n");
        Alloc.setRemoteAddress(NextAddr);

        // Only advance NextAddr if it was non-null to begin with,
        // otherwise leave it as null.
        if (NextAddr)
          NextAddr += Alloc.getSize();
      }
    }

    // Copies data for each alloc in the list, then set permissions on the
    // segment.
    bool copyAndProtect(const std::vector<Alloc> &Allocs,
                        JITTargetAddress RemoteSegmentAddr,
                        unsigned Permissions) {
      if (RemoteSegmentAddr) {
        assert(!Allocs.empty() && "No sections in allocated segment");

        for (auto &Alloc : Allocs) {
          LLVM_DEBUG(dbgs() << "  copying section: "
                            << static_cast<void *>(Alloc.getLocalAddress())
                            << " -> "
                            << format("0x%016" PRIx64, Alloc.getRemoteAddress())
                            << " (" << Alloc.getSize() << " bytes)\n";);

          if (Client.writeMem(Alloc.getRemoteAddress(), Alloc.getLocalAddress(),
                              Alloc.getSize()))
            return true;
        }

        LLVM_DEBUG(dbgs() << "  setting "
                          << (Permissions & sys::Memory::MF_READ ? 'R' : '-')
                          << (Permissions & sys::Memory::MF_WRITE ? 'W' : '-')
                          << (Permissions & sys::Memory::MF_EXEC ? 'X' : '-')
                          << " permissions on block: "
                          << format("0x%016" PRIx64, RemoteSegmentAddr)
                          << "\n");
        if (Client.setProtections(Id, RemoteSegmentAddr, Permissions))
          return true;
      }
      return false;
    }

    OrcRemoteTargetClient &Client;
    ResourceIdMgr::ResourceId Id;
    std::vector<ObjectAllocs> Unmapped;
    std::vector<ObjectAllocs> Unfinalized;

    struct EHFrame {
      JITTargetAddress Addr;
      uint64_t Size;
    };
    std::vector<EHFrame> UnfinalizedEHFrames;
    std::vector<EHFrame> RegisteredEHFrames;
  };

  /// Remote indirect stubs manager.
  class RemoteIndirectStubsManager : public IndirectStubsManager {
  public:
    RemoteIndirectStubsManager(OrcRemoteTargetClient &Client,
                               ResourceIdMgr::ResourceId Id)
        : Client(Client), Id(Id) {}

    ~RemoteIndirectStubsManager() override {
      Client.destroyIndirectStubsManager(Id);
    }

    Error createStub(StringRef StubName, JITTargetAddress StubAddr,
                     JITSymbolFlags StubFlags) override {
      if (auto Err = reserveStubs(1))
        return Err;

      return createStubInternal(StubName, StubAddr, StubFlags);
    }

    Error createStubs(const StubInitsMap &StubInits) override {
      if (auto Err = reserveStubs(StubInits.size()))
        return Err;

      for (auto &Entry : StubInits)
        if (auto Err = createStubInternal(Entry.first(), Entry.second.first,
                                          Entry.second.second))
          return Err;

      return Error::success();
    }

    JITEvaluatedSymbol findStub(StringRef Name, bool ExportedStubsOnly) override {
      auto I = StubIndexes.find(Name);
      if (I == StubIndexes.end())
        return nullptr;
      auto Key = I->second.first;
      auto Flags = I->second.second;
      auto StubSymbol = JITEvaluatedSymbol(getStubAddr(Key), Flags);
      if (ExportedStubsOnly && !StubSymbol.getFlags().isExported())
        return nullptr;
      return StubSymbol;
    }

    JITEvaluatedSymbol findPointer(StringRef Name) override {
      auto I = StubIndexes.find(Name);
      if (I == StubIndexes.end())
        return nullptr;
      auto Key = I->second.first;
      auto Flags = I->second.second;
      return JITEvaluatedSymbol(getPtrAddr(Key), Flags);
    }

    Error updatePointer(StringRef Name, JITTargetAddress NewAddr) override {
      auto I = StubIndexes.find(Name);
      assert(I != StubIndexes.end() && "No stub pointer for symbol");
      auto Key = I->second.first;
      return Client.writePointer(getPtrAddr(Key), NewAddr);
    }

  private:
    struct RemoteIndirectStubsInfo {
      JITTargetAddress StubBase;
      JITTargetAddress PtrBase;
      unsigned NumStubs;
    };

    using StubKey = std::pair<uint16_t, uint16_t>;

    Error reserveStubs(unsigned NumStubs) {
      if (NumStubs <= FreeStubs.size())
        return Error::success();

      unsigned NewStubsRequired = NumStubs - FreeStubs.size();
      JITTargetAddress StubBase;
      JITTargetAddress PtrBase;
      unsigned NumStubsEmitted;

      if (auto StubInfoOrErr = Client.emitIndirectStubs(Id, NewStubsRequired))
        std::tie(StubBase, PtrBase, NumStubsEmitted) = *StubInfoOrErr;
      else
        return StubInfoOrErr.takeError();

      unsigned NewBlockId = RemoteIndirectStubsInfos.size();
      RemoteIndirectStubsInfos.push_back({StubBase, PtrBase, NumStubsEmitted});

      for (unsigned I = 0; I < NumStubsEmitted; ++I)
        FreeStubs.push_back(std::make_pair(NewBlockId, I));

      return Error::success();
    }

    Error createStubInternal(StringRef StubName, JITTargetAddress InitAddr,
                             JITSymbolFlags StubFlags) {
      auto Key = FreeStubs.back();
      FreeStubs.pop_back();
      StubIndexes[StubName] = std::make_pair(Key, StubFlags);
      return Client.writePointer(getPtrAddr(Key), InitAddr);
    }

    JITTargetAddress getStubAddr(StubKey K) {
      assert(RemoteIndirectStubsInfos[K.first].StubBase != 0 &&
             "Missing stub address");
      return RemoteIndirectStubsInfos[K.first].StubBase +
             K.second * Client.getIndirectStubSize();
    }

    JITTargetAddress getPtrAddr(StubKey K) {
      assert(RemoteIndirectStubsInfos[K.first].PtrBase != 0 &&
             "Missing pointer address");
      return RemoteIndirectStubsInfos[K.first].PtrBase +
             K.second * Client.getPointerSize();
    }

    OrcRemoteTargetClient &Client;
    ResourceIdMgr::ResourceId Id;
    std::vector<RemoteIndirectStubsInfo> RemoteIndirectStubsInfos;
    std::vector<StubKey> FreeStubs;
    StringMap<std::pair<StubKey, JITSymbolFlags>> StubIndexes;
  };

  class RemoteTrampolinePool : public TrampolinePool {
  public:
    RemoteTrampolinePool(OrcRemoteTargetClient &Client) : Client(Client) {}

    Expected<JITTargetAddress> getTrampoline() override {
      std::lock_guard<std::mutex> Lock(RTPMutex);
      if (AvailableTrampolines.empty()) {
        if (auto Err = grow())
          return std::move(Err);
      }
      assert(!AvailableTrampolines.empty() && "Failed to grow trampoline pool");
      auto TrampolineAddr = AvailableTrampolines.back();
      AvailableTrampolines.pop_back();
      return TrampolineAddr;
    }

  private:
    Error grow() {
      JITTargetAddress BlockAddr = 0;
      uint32_t NumTrampolines = 0;
      if (auto TrampolineInfoOrErr = Client.emitTrampolineBlock())
        std::tie(BlockAddr, NumTrampolines) = *TrampolineInfoOrErr;
      else
        return TrampolineInfoOrErr.takeError();

      uint32_t TrampolineSize = Client.getTrampolineSize();
      for (unsigned I = 0; I < NumTrampolines; ++I)
        this->AvailableTrampolines.push_back(BlockAddr + (I * TrampolineSize));

      return Error::success();
    }

    std::mutex RTPMutex;
    OrcRemoteTargetClient &Client;
    std::vector<JITTargetAddress> AvailableTrampolines;
  };

  /// Remote compile callback manager.
  class RemoteCompileCallbackManager : public JITCompileCallbackManager {
  public:
    RemoteCompileCallbackManager(OrcRemoteTargetClient &Client,
                                 ExecutionSession &ES,
                                 JITTargetAddress ErrorHandlerAddress)
        : JITCompileCallbackManager(
              llvm::make_unique<RemoteTrampolinePool>(Client), ES,
              ErrorHandlerAddress) {}
  };

  /// Create an OrcRemoteTargetClient.
  /// Channel is the ChannelT instance to communicate on. It is assumed that
  /// the channel is ready to be read from and written to.
  static Expected<std::unique_ptr<OrcRemoteTargetClient>>
  Create(rpc::RawByteChannel &Channel, ExecutionSession &ES) {
    Error Err = Error::success();
    auto Client = std::unique_ptr<OrcRemoteTargetClient>(
        new OrcRemoteTargetClient(Channel, ES, Err));
    if (Err)
      return std::move(Err);
    return std::move(Client);
  }

  /// Call the int(void) function at the given address in the target and return
  /// its result.
  Expected<int> callIntVoid(JITTargetAddress Addr) {
    LLVM_DEBUG(dbgs() << "Calling int(*)(void) "
                      << format("0x%016" PRIx64, Addr) << "\n");
    return callB<exec::CallIntVoid>(Addr);
  }

  /// Call the int(int, char*[]) function at the given address in the target and
  /// return its result.
  Expected<int> callMain(JITTargetAddress Addr,
                         const std::vector<std::string> &Args) {
    LLVM_DEBUG(dbgs() << "Calling int(*)(int, char*[]) "
                      << format("0x%016" PRIx64, Addr) << "\n");
    return callB<exec::CallMain>(Addr, Args);
  }

  /// Call the void() function at the given address in the target and wait for
  /// it to finish.
  Error callVoidVoid(JITTargetAddress Addr) {
    LLVM_DEBUG(dbgs() << "Calling void(*)(void) "
                      << format("0x%016" PRIx64, Addr) << "\n");
    return callB<exec::CallVoidVoid>(Addr);
  }

  /// Create an RCMemoryManager which will allocate its memory on the remote
  /// target.
  Expected<std::unique_ptr<RemoteRTDyldMemoryManager>>
  createRemoteMemoryManager() {
    auto Id = AllocatorIds.getNext();
    if (auto Err = callB<mem::CreateRemoteAllocator>(Id))
      return std::move(Err);
    return std::unique_ptr<RemoteRTDyldMemoryManager>(
        new RemoteRTDyldMemoryManager(*this, Id));
  }

  /// Create an RCIndirectStubsManager that will allocate stubs on the remote
  /// target.
  Expected<std::unique_ptr<RemoteIndirectStubsManager>>
  createIndirectStubsManager() {
    auto Id = IndirectStubOwnerIds.getNext();
    if (auto Err = callB<stubs::CreateIndirectStubsOwner>(Id))
      return std::move(Err);
    return llvm::make_unique<RemoteIndirectStubsManager>(*this, Id);
  }

  Expected<RemoteCompileCallbackManager &>
  enableCompileCallbacks(JITTargetAddress ErrorHandlerAddress) {
    assert(!CallbackManager && "CallbackManager already obtained");

    // Emit the resolver block on the JIT server.
    if (auto Err = callB<stubs::EmitResolverBlock>())
      return std::move(Err);

    // Create the callback manager.
    CallbackManager.emplace(*this, ES, ErrorHandlerAddress);
    RemoteCompileCallbackManager &Mgr = *CallbackManager;
    return Mgr;
  }

  /// Search for symbols in the remote process. Note: This should be used by
  /// symbol resolvers *after* they've searched the local symbol table in the
  /// JIT stack.
  Expected<JITTargetAddress> getSymbolAddress(StringRef Name) {
    return callB<utils::GetSymbolAddress>(Name);
  }

  /// Get the triple for the remote target.
  const std::string &getTargetTriple() const { return RemoteTargetTriple; }

  Error terminateSession() { return callB<utils::TerminateSession>(); }

private:
  OrcRemoteTargetClient(rpc::RawByteChannel &Channel, ExecutionSession &ES,
                        Error &Err)
      : rpc::SingleThreadedRPCEndpoint<rpc::RawByteChannel>(Channel, true),
        ES(ES) {
    ErrorAsOutParameter EAO(&Err);

    addHandler<utils::RequestCompile>(
        [this](JITTargetAddress Addr) -> JITTargetAddress {
          if (CallbackManager)
            return CallbackManager->executeCompileCallback(Addr);
          return 0;
        });

    if (auto RIOrErr = callB<utils::GetRemoteInfo>()) {
      std::tie(RemoteTargetTriple, RemotePointerSize, RemotePageSize,
               RemoteTrampolineSize, RemoteIndirectStubSize) = *RIOrErr;
      Err = Error::success();
    } else
      Err = RIOrErr.takeError();
  }

  void deregisterEHFrames(JITTargetAddress Addr, uint32_t Size) {
    if (auto Err = callB<eh::RegisterEHFrames>(Addr, Size))
      ES.reportError(std::move(Err));
  }

  void destroyRemoteAllocator(ResourceIdMgr::ResourceId Id) {
    if (auto Err = callB<mem::DestroyRemoteAllocator>(Id)) {
      // FIXME: This will be triggered by a removeModuleSet call: Propagate
      //        error return up through that.
      llvm_unreachable("Failed to destroy remote allocator.");
      AllocatorIds.release(Id);
    }
  }

  void destroyIndirectStubsManager(ResourceIdMgr::ResourceId Id) {
    IndirectStubOwnerIds.release(Id);
    if (auto Err = callB<stubs::DestroyIndirectStubsOwner>(Id))
      ES.reportError(std::move(Err));
  }

  Expected<std::tuple<JITTargetAddress, JITTargetAddress, uint32_t>>
  emitIndirectStubs(ResourceIdMgr::ResourceId Id, uint32_t NumStubsRequired) {
    return callB<stubs::EmitIndirectStubs>(Id, NumStubsRequired);
  }

  Expected<std::tuple<JITTargetAddress, uint32_t>> emitTrampolineBlock() {
    return callB<stubs::EmitTrampolineBlock>();
  }

  uint32_t getIndirectStubSize() const { return RemoteIndirectStubSize; }
  uint32_t getPageSize() const { return RemotePageSize; }
  uint32_t getPointerSize() const { return RemotePointerSize; }

  uint32_t getTrampolineSize() const { return RemoteTrampolineSize; }

  Expected<std::vector<uint8_t>> readMem(char *Dst, JITTargetAddress Src,
                                         uint64_t Size) {
    return callB<mem::ReadMem>(Src, Size);
  }

  Error registerEHFrames(JITTargetAddress &RAddr, uint32_t Size) {
    // FIXME: Duplicate error and report it via ReportError too?
    return callB<eh::RegisterEHFrames>(RAddr, Size);
  }

  JITTargetAddress reserveMem(ResourceIdMgr::ResourceId Id, uint64_t Size,
                              uint32_t Align) {
    if (auto AddrOrErr = callB<mem::ReserveMem>(Id, Size, Align))
      return *AddrOrErr;
    else {
      ES.reportError(AddrOrErr.takeError());
      return 0;
    }
  }

  bool setProtections(ResourceIdMgr::ResourceId Id,
                      JITTargetAddress RemoteSegAddr, unsigned ProtFlags) {
    if (auto Err = callB<mem::SetProtections>(Id, RemoteSegAddr, ProtFlags)) {
      ES.reportError(std::move(Err));
      return true;
    } else
      return false;
  }

  bool writeMem(JITTargetAddress Addr, const char *Src, uint64_t Size) {
    if (auto Err = callB<mem::WriteMem>(DirectBufferWriter(Src, Addr, Size))) {
      ES.reportError(std::move(Err));
      return true;
    } else
      return false;
  }

  Error writePointer(JITTargetAddress Addr, JITTargetAddress PtrVal) {
    return callB<mem::WritePtr>(Addr, PtrVal);
  }

  static Error doNothing() { return Error::success(); }

  ExecutionSession &ES;
  std::function<void(Error)> ReportError;
  std::string RemoteTargetTriple;
  uint32_t RemotePointerSize = 0;
  uint32_t RemotePageSize = 0;
  uint32_t RemoteTrampolineSize = 0;
  uint32_t RemoteIndirectStubSize = 0;
  ResourceIdMgr AllocatorIds, IndirectStubOwnerIds;
  Optional<RemoteCompileCallbackManager> CallbackManager;
};

} // end namespace remote
} // end namespace orc
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_EXECUTIONENGINE_ORC_ORCREMOTETARGETCLIENT_H
