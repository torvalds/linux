//===------ RemoteObjectLayer.h - Forwards objs to a remote -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Forwards objects to a remote object layer via RPC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_REMOTEOBJECTLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_REMOTEOBJECTLAYER_H

#include "llvm/ExecutionEngine/Orc/OrcRemoteTargetRPCAPI.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include <map>

namespace llvm {
namespace orc {

/// RPC API needed by RemoteObjectClientLayer and RemoteObjectServerLayer.
class RemoteObjectLayerAPI {
public:

  using ObjHandleT = remote::ResourceIdMgr::ResourceId;

protected:

  using RemoteSymbolId = remote::ResourceIdMgr::ResourceId;
  using RemoteSymbol = std::pair<RemoteSymbolId, JITSymbolFlags>;

public:

  using BadSymbolHandleError = remote::ResourceNotFound<RemoteSymbolId>;
  using BadObjectHandleError = remote::ResourceNotFound<ObjHandleT>;

protected:

  static const ObjHandleT InvalidObjectHandleId = 0;
  static const RemoteSymbolId NullSymbolId = 0;

  class AddObject
    : public rpc::Function<AddObject, Expected<ObjHandleT>(std::string)> {
  public:
    static const char *getName() { return "AddObject"; }
  };

  class RemoveObject
    : public rpc::Function<RemoveObject, Error(ObjHandleT)> {
  public:
    static const char *getName() { return "RemoveObject"; }
  };

  class FindSymbol
    : public rpc::Function<FindSymbol, Expected<RemoteSymbol>(std::string,
                                                              bool)> {
  public:
    static const char *getName() { return "FindSymbol"; }
  };

  class FindSymbolIn
    : public rpc::Function<FindSymbolIn,
                           Expected<RemoteSymbol>(ObjHandleT, std::string,
                                                  bool)> {
  public:
    static const char *getName() { return "FindSymbolIn"; }
  };

  class EmitAndFinalize
    : public rpc::Function<EmitAndFinalize,
                           Error(ObjHandleT)> {
  public:
    static const char *getName() { return "EmitAndFinalize"; }
  };

  class Lookup
    : public rpc::Function<Lookup,
                           Expected<RemoteSymbol>(ObjHandleT, std::string)> {
  public:
    static const char *getName() { return "Lookup"; }
  };

  class LookupInLogicalDylib
    : public rpc::Function<LookupInLogicalDylib,
                           Expected<RemoteSymbol>(ObjHandleT, std::string)> {
  public:
    static const char *getName() { return "LookupInLogicalDylib"; }
  };

  class ReleaseRemoteSymbol
    : public rpc::Function<ReleaseRemoteSymbol, Error(RemoteSymbolId)> {
  public:
    static const char *getName() { return "ReleaseRemoteSymbol"; }
  };

  class MaterializeRemoteSymbol
    : public rpc::Function<MaterializeRemoteSymbol,
                           Expected<JITTargetAddress>(RemoteSymbolId)> {
  public:
    static const char *getName() { return "MaterializeRemoteSymbol"; }
  };
};

/// Base class containing common utilities for RemoteObjectClientLayer and
/// RemoteObjectServerLayer.
template <typename RPCEndpoint>
class RemoteObjectLayer : public RemoteObjectLayerAPI {
public:

  RemoteObjectLayer(RPCEndpoint &Remote,
                    std::function<void(Error)> ReportError)
      : Remote(Remote), ReportError(std::move(ReportError)),
        SymbolIdMgr(NullSymbolId + 1) {
    using ThisT = RemoteObjectLayer<RPCEndpoint>;
    Remote.template addHandler<ReleaseRemoteSymbol>(
             *this, &ThisT::handleReleaseRemoteSymbol);
    Remote.template addHandler<MaterializeRemoteSymbol>(
             *this, &ThisT::handleMaterializeRemoteSymbol);
  }

protected:

  /// This class is used as the symbol materializer for JITSymbols returned by
  /// RemoteObjectLayerClient/RemoteObjectLayerServer -- the materializer knows
  /// how to call back to the other RPC endpoint to get the address when
  /// requested.
  class RemoteSymbolMaterializer {
  public:

    /// Construct a RemoteSymbolMaterializer for the given RemoteObjectLayer
    /// with the given Id.
    RemoteSymbolMaterializer(RemoteObjectLayer &C,
                             RemoteSymbolId Id)
      : C(C), Id(Id) {}

    RemoteSymbolMaterializer(const RemoteSymbolMaterializer &Other)
      : C(Other.C), Id(Other.Id) {
      // FIXME: This is a horrible, auto_ptr-style, copy-as-move operation.
      //        It should be removed as soon as LLVM has C++14's generalized
      //        lambda capture (at which point the materializer can be moved
      //        into the lambda in remoteToJITSymbol below).
      const_cast<RemoteSymbolMaterializer&>(Other).Id = 0;
    }

    RemoteSymbolMaterializer&
    operator=(const RemoteSymbolMaterializer&) = delete;

    /// Release the remote symbol.
    ~RemoteSymbolMaterializer() {
      if (Id)
        C.releaseRemoteSymbol(Id);
    }

    /// Materialize the symbol on the remote and get its address.
    Expected<JITTargetAddress> materialize() {
      auto Addr = C.materializeRemoteSymbol(Id);
      Id = 0;
      return Addr;
    }

  private:
    RemoteObjectLayer &C;
    RemoteSymbolId Id;
  };

  /// Convenience function for getting a null remote symbol value.
  RemoteSymbol nullRemoteSymbol() {
    return RemoteSymbol(0, JITSymbolFlags());
  }

  /// Creates a StringError that contains a copy of Err's log message, then
  /// sends that StringError to ReportError.
  ///
  /// This allows us to locally log error messages for errors that will actually
  /// be delivered to the remote.
  Error teeLog(Error Err) {
    return handleErrors(std::move(Err),
                        [this](std::unique_ptr<ErrorInfoBase> EIB) {
                          ReportError(make_error<StringError>(
                                        EIB->message(),
                                        EIB->convertToErrorCode()));
                          return Error(std::move(EIB));
                        });
  }

  Error badRemoteSymbolIdError(RemoteSymbolId Id) {
    return make_error<BadSymbolHandleError>(Id, "Remote JIT Symbol");
  }

  Error badObjectHandleError(ObjHandleT H) {
    return make_error<RemoteObjectLayerAPI::BadObjectHandleError>(
             H, "Bad object handle");
  }

  /// Create a RemoteSymbol wrapping the given JITSymbol.
  Expected<RemoteSymbol> jitSymbolToRemote(JITSymbol Sym) {
    if (Sym) {
      auto Id = SymbolIdMgr.getNext();
      auto Flags = Sym.getFlags();
      assert(!InUseSymbols.count(Id) && "Symbol id already in use");
      InUseSymbols.insert(std::make_pair(Id, std::move(Sym)));
      return RemoteSymbol(Id, Flags);
    } else if (auto Err = Sym.takeError())
      return teeLog(std::move(Err));
    // else...
    return nullRemoteSymbol();
  }

  /// Convert an Expected<RemoteSymbol> to a JITSymbol.
  JITSymbol remoteToJITSymbol(Expected<RemoteSymbol> RemoteSymOrErr) {
    if (RemoteSymOrErr) {
      auto &RemoteSym = *RemoteSymOrErr;
      if (RemoteSym == nullRemoteSymbol())
        return nullptr;
      // else...
      RemoteSymbolMaterializer RSM(*this, RemoteSym.first);
      auto Sym =
        JITSymbol([RSM]() mutable { return RSM.materialize(); },
                  RemoteSym.second);
      return Sym;
    } else
      return RemoteSymOrErr.takeError();
  }

  RPCEndpoint &Remote;
  std::function<void(Error)> ReportError;

private:

  /// Notify the remote to release the given JITSymbol.
  void releaseRemoteSymbol(RemoteSymbolId Id) {
    if (auto Err = Remote.template callB<ReleaseRemoteSymbol>(Id))
      ReportError(std::move(Err));
  }

  /// Notify the remote to materialize the JITSymbol with the given Id and
  /// return its address.
  Expected<JITTargetAddress> materializeRemoteSymbol(RemoteSymbolId Id) {
    return Remote.template callB<MaterializeRemoteSymbol>(Id);
  }

  /// Release the JITSymbol with the given Id.
  Error handleReleaseRemoteSymbol(RemoteSymbolId Id) {
    auto SI = InUseSymbols.find(Id);
    if (SI != InUseSymbols.end()) {
      InUseSymbols.erase(SI);
      return Error::success();
    } else
      return teeLog(badRemoteSymbolIdError(Id));
  }

  /// Run the materializer for the JITSymbol with the given Id and return its
  /// address.
  Expected<JITTargetAddress> handleMaterializeRemoteSymbol(RemoteSymbolId Id) {
    auto SI = InUseSymbols.find(Id);
    if (SI != InUseSymbols.end()) {
      auto AddrOrErr = SI->second.getAddress();
      InUseSymbols.erase(SI);
      SymbolIdMgr.release(Id);
      if (AddrOrErr)
        return *AddrOrErr;
      else
        return teeLog(AddrOrErr.takeError());
    } else {
      return teeLog(badRemoteSymbolIdError(Id));
    }
  }

  remote::ResourceIdMgr SymbolIdMgr;
  std::map<RemoteSymbolId, JITSymbol> InUseSymbols;
};

/// RemoteObjectClientLayer forwards the ORC Object Layer API over an RPC
/// connection.
///
/// This class can be used as the base layer of a JIT stack on the client and
/// will forward operations to a corresponding RemoteObjectServerLayer on the
/// server (which can be composed on top of a "real" object layer like
/// RTDyldObjectLinkingLayer to actually carry out the operations).
///
/// Sending relocatable objects to the server (rather than fully relocated
/// bits) allows JIT'd code to be cached on the server side and re-used in
/// subsequent JIT sessions.
template <typename RPCEndpoint>
class RemoteObjectClientLayer : public RemoteObjectLayer<RPCEndpoint> {
private:

  using AddObject = RemoteObjectLayerAPI::AddObject;
  using RemoveObject = RemoteObjectLayerAPI::RemoveObject;
  using FindSymbol = RemoteObjectLayerAPI::FindSymbol;
  using FindSymbolIn = RemoteObjectLayerAPI::FindSymbolIn;
  using EmitAndFinalize = RemoteObjectLayerAPI::EmitAndFinalize;
  using Lookup = RemoteObjectLayerAPI::Lookup;
  using LookupInLogicalDylib = RemoteObjectLayerAPI::LookupInLogicalDylib;

  using RemoteObjectLayer<RPCEndpoint>::teeLog;
  using RemoteObjectLayer<RPCEndpoint>::badObjectHandleError;
  using RemoteObjectLayer<RPCEndpoint>::remoteToJITSymbol;

public:

  using ObjHandleT = RemoteObjectLayerAPI::ObjHandleT;
  using RemoteSymbol = RemoteObjectLayerAPI::RemoteSymbol;

  using ObjectPtr = std::unique_ptr<MemoryBuffer>;

  /// Create a RemoteObjectClientLayer that communicates with a
  /// RemoteObjectServerLayer instance via the given RPCEndpoint.
  ///
  /// The ReportError functor can be used locally log errors that are intended
  /// to be sent  sent
  RemoteObjectClientLayer(RPCEndpoint &Remote,
                          std::function<void(Error)> ReportError)
      : RemoteObjectLayer<RPCEndpoint>(Remote, std::move(ReportError)) {
    using ThisT = RemoteObjectClientLayer<RPCEndpoint>;
    Remote.template addHandler<Lookup>(*this, &ThisT::lookup);
    Remote.template addHandler<LookupInLogicalDylib>(
            *this, &ThisT::lookupInLogicalDylib);
  }

  /// Add an object to the JIT.
  ///
  /// @return A handle that can be used to refer to the loaded object (for
  ///         symbol searching, finalization, freeing memory, etc.).
  Expected<ObjHandleT>
  addObject(ObjectPtr ObjBuffer,
            std::shared_ptr<LegacyJITSymbolResolver> Resolver) {
    if (auto HandleOrErr =
            this->Remote.template callB<AddObject>(ObjBuffer->getBuffer())) {
      auto &Handle = *HandleOrErr;
      // FIXME: Return an error for this:
      assert(!Resolvers.count(Handle) && "Handle already in use?");
      Resolvers[Handle] = std::move(Resolver);
      return Handle;
    } else
      return HandleOrErr.takeError();
  }

  /// Remove the given object from the JIT.
  Error removeObject(ObjHandleT H) {
    return this->Remote.template callB<RemoveObject>(H);
  }

  /// Search for the given named symbol.
  JITSymbol findSymbol(StringRef Name, bool ExportedSymbolsOnly) {
    return remoteToJITSymbol(
             this->Remote.template callB<FindSymbol>(Name,
                                                     ExportedSymbolsOnly));
  }

  /// Search for the given named symbol within the given context.
  JITSymbol findSymbolIn(ObjHandleT H, StringRef Name, bool ExportedSymbolsOnly) {
    return remoteToJITSymbol(
             this->Remote.template callB<FindSymbolIn>(H, Name,
                                                       ExportedSymbolsOnly));
  }

  /// Immediately emit and finalize the object with the given handle.
  Error emitAndFinalize(ObjHandleT H) {
    return this->Remote.template callB<EmitAndFinalize>(H);
  }

private:

  Expected<RemoteSymbol> lookup(ObjHandleT H, const std::string &Name) {
    auto RI = Resolvers.find(H);
    if (RI != Resolvers.end()) {
      return this->jitSymbolToRemote(RI->second->findSymbol(Name));
    } else
      return teeLog(badObjectHandleError(H));
  }

  Expected<RemoteSymbol> lookupInLogicalDylib(ObjHandleT H,
                                              const std::string &Name) {
    auto RI = Resolvers.find(H);
    if (RI != Resolvers.end())
      return this->jitSymbolToRemote(
               RI->second->findSymbolInLogicalDylib(Name));
    else
      return teeLog(badObjectHandleError(H));
  }

  std::map<remote::ResourceIdMgr::ResourceId,
           std::shared_ptr<LegacyJITSymbolResolver>>
      Resolvers;
};

/// RemoteObjectServerLayer acts as a server and handling RPC calls for the
/// object layer API from the given RPC connection.
///
/// This class can be composed on top of a 'real' object layer (e.g.
/// RTDyldObjectLinkingLayer) to do the actual work of relocating objects
/// and making them executable.
template <typename BaseLayerT, typename RPCEndpoint>
class RemoteObjectServerLayer : public RemoteObjectLayer<RPCEndpoint> {
private:

  using ObjHandleT = RemoteObjectLayerAPI::ObjHandleT;
  using RemoteSymbol = RemoteObjectLayerAPI::RemoteSymbol;

  using AddObject = RemoteObjectLayerAPI::AddObject;
  using RemoveObject = RemoteObjectLayerAPI::RemoveObject;
  using FindSymbol = RemoteObjectLayerAPI::FindSymbol;
  using FindSymbolIn = RemoteObjectLayerAPI::FindSymbolIn;
  using EmitAndFinalize = RemoteObjectLayerAPI::EmitAndFinalize;
  using Lookup = RemoteObjectLayerAPI::Lookup;
  using LookupInLogicalDylib = RemoteObjectLayerAPI::LookupInLogicalDylib;

  using RemoteObjectLayer<RPCEndpoint>::teeLog;
  using RemoteObjectLayer<RPCEndpoint>::badObjectHandleError;
  using RemoteObjectLayer<RPCEndpoint>::remoteToJITSymbol;

public:

  /// Create a RemoteObjectServerLayer with the given base layer (which must be
  /// an object layer), RPC endpoint, and error reporter function.
  RemoteObjectServerLayer(BaseLayerT &BaseLayer,
                          RPCEndpoint &Remote,
                          std::function<void(Error)> ReportError)
    : RemoteObjectLayer<RPCEndpoint>(Remote, std::move(ReportError)),
      BaseLayer(BaseLayer), HandleIdMgr(1) {
    using ThisT = RemoteObjectServerLayer<BaseLayerT, RPCEndpoint>;

    Remote.template addHandler<AddObject>(*this, &ThisT::addObject);
    Remote.template addHandler<RemoveObject>(*this, &ThisT::removeObject);
    Remote.template addHandler<FindSymbol>(*this, &ThisT::findSymbol);
    Remote.template addHandler<FindSymbolIn>(*this, &ThisT::findSymbolIn);
    Remote.template addHandler<EmitAndFinalize>(*this, &ThisT::emitAndFinalize);
  }

private:

  class StringMemoryBuffer : public MemoryBuffer {
  public:
    StringMemoryBuffer(std::string Buffer)
      : Buffer(std::move(Buffer)) {
      init(this->Buffer.data(), this->Buffer.data() + this->Buffer.size(),
           false);
    }

    BufferKind getBufferKind() const override { return MemoryBuffer_Malloc; }
  private:
    std::string Buffer;
  };

  JITSymbol lookup(ObjHandleT Id, const std::string &Name) {
    return remoteToJITSymbol(
             this->Remote.template callB<Lookup>(Id, Name));
  }

  JITSymbol lookupInLogicalDylib(ObjHandleT Id, const std::string &Name) {
    return remoteToJITSymbol(
             this->Remote.template callB<LookupInLogicalDylib>(Id, Name));
  }

  Expected<ObjHandleT> addObject(std::string ObjBuffer) {
    auto Buffer = llvm::make_unique<StringMemoryBuffer>(std::move(ObjBuffer));
    auto Id = HandleIdMgr.getNext();
    assert(!BaseLayerHandles.count(Id) && "Id already in use?");

    auto Resolver = createLambdaResolver(
        [this, Id](const std::string &Name) { return lookup(Id, Name); },
        [this, Id](const std::string &Name) {
          return lookupInLogicalDylib(Id, Name);
        });

    if (auto HandleOrErr =
            BaseLayer.addObject(std::move(Buffer), std::move(Resolver))) {
      BaseLayerHandles[Id] = std::move(*HandleOrErr);
      return Id;
    } else
      return teeLog(HandleOrErr.takeError());
  }

  Error removeObject(ObjHandleT H) {
    auto HI = BaseLayerHandles.find(H);
    if (HI != BaseLayerHandles.end()) {
      if (auto Err = BaseLayer.removeObject(HI->second))
        return teeLog(std::move(Err));
      return Error::success();
    } else
      return teeLog(badObjectHandleError(H));
  }

  Expected<RemoteSymbol> findSymbol(const std::string &Name,
                                    bool ExportedSymbolsOnly) {
    if (auto Sym = BaseLayer.findSymbol(Name, ExportedSymbolsOnly))
      return this->jitSymbolToRemote(std::move(Sym));
    else if (auto Err = Sym.takeError())
      return teeLog(std::move(Err));
    return this->nullRemoteSymbol();
  }

  Expected<RemoteSymbol> findSymbolIn(ObjHandleT H, const std::string &Name,
                                      bool ExportedSymbolsOnly) {
    auto HI = BaseLayerHandles.find(H);
    if (HI != BaseLayerHandles.end()) {
      if (auto Sym = BaseLayer.findSymbolIn(HI->second, Name, ExportedSymbolsOnly))
        return this->jitSymbolToRemote(std::move(Sym));
      else if (auto Err = Sym.takeError())
        return teeLog(std::move(Err));
      return this->nullRemoteSymbol();
    } else
      return teeLog(badObjectHandleError(H));
  }

  Error emitAndFinalize(ObjHandleT H) {
    auto HI = BaseLayerHandles.find(H);
    if (HI != BaseLayerHandles.end()) {
      if (auto Err = BaseLayer.emitAndFinalize(HI->second))
        return teeLog(std::move(Err));
      return Error::success();
    } else
      return teeLog(badObjectHandleError(H));
  }

  BaseLayerT &BaseLayer;
  remote::ResourceIdMgr HandleIdMgr;
  std::map<ObjHandleT, typename BaseLayerT::ObjHandleT> BaseLayerHandles;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_REMOTEOBJECTLAYER_H
