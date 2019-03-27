//===-------------------- Layer.cpp - Layer interfaces --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

IRLayer::IRLayer(ExecutionSession &ES) : ES(ES) {}
IRLayer::~IRLayer() {}

Error IRLayer::add(JITDylib &JD, ThreadSafeModule TSM, VModuleKey K) {
  return JD.define(llvm::make_unique<BasicIRLayerMaterializationUnit>(
      *this, std::move(K), std::move(TSM)));
}

IRMaterializationUnit::IRMaterializationUnit(ExecutionSession &ES,
                                             ThreadSafeModule TSM, VModuleKey K)
    : MaterializationUnit(SymbolFlagsMap(), std::move(K)), TSM(std::move(TSM)) {

  assert(this->TSM && "Module must not be null");

  MangleAndInterner Mangle(ES, this->TSM.getModule()->getDataLayout());
  for (auto &G : this->TSM.getModule()->global_values()) {
    if (G.hasName() && !G.isDeclaration() && !G.hasLocalLinkage() &&
        !G.hasAvailableExternallyLinkage() && !G.hasAppendingLinkage()) {
      auto MangledName = Mangle(G.getName());
      SymbolFlags[MangledName] = JITSymbolFlags::fromGlobalValue(G);
      SymbolToDefinition[MangledName] = &G;
    }
  }
}

IRMaterializationUnit::IRMaterializationUnit(
    ThreadSafeModule TSM, VModuleKey K, SymbolFlagsMap SymbolFlags,
    SymbolNameToDefinitionMap SymbolToDefinition)
    : MaterializationUnit(std::move(SymbolFlags), std::move(K)),
      TSM(std::move(TSM)), SymbolToDefinition(std::move(SymbolToDefinition)) {}

StringRef IRMaterializationUnit::getName() const {
  if (TSM.getModule())
    return TSM.getModule()->getModuleIdentifier();
  return "<null module>";
}

void IRMaterializationUnit::discard(const JITDylib &JD,
                                    const SymbolStringPtr &Name) {
  LLVM_DEBUG(JD.getExecutionSession().runSessionLocked([&]() {
    dbgs() << "In " << JD.getName() << " discarding " << *Name << " from MU@"
           << this << " (" << getName() << ")\n";
  }););

  auto I = SymbolToDefinition.find(Name);
  assert(I != SymbolToDefinition.end() &&
         "Symbol not provided by this MU, or previously discarded");
  assert(!I->second->isDeclaration() &&
         "Discard should only apply to definitions");
  I->second->setLinkage(GlobalValue::AvailableExternallyLinkage);
  SymbolToDefinition.erase(I);
}

BasicIRLayerMaterializationUnit::BasicIRLayerMaterializationUnit(
    IRLayer &L, VModuleKey K, ThreadSafeModule TSM)
    : IRMaterializationUnit(L.getExecutionSession(), std::move(TSM),
                            std::move(K)),
      L(L), K(std::move(K)) {}

void BasicIRLayerMaterializationUnit::materialize(
    MaterializationResponsibility R) {

  // Throw away the SymbolToDefinition map: it's not usable after we hand
  // off the module.
  SymbolToDefinition.clear();

  // If cloneToNewContextOnEmit is set, clone the module now.
  if (L.getCloneToNewContextOnEmit())
    TSM = cloneToNewContext(TSM);

#ifndef NDEBUG
  auto &ES = R.getTargetJITDylib().getExecutionSession();
#endif // NDEBUG

  auto Lock = TSM.getContextLock();
  LLVM_DEBUG(ES.runSessionLocked([&]() {
    dbgs() << "Emitting, for " << R.getTargetJITDylib().getName() << ", "
           << *this << "\n";
  }););
  L.emit(std::move(R), std::move(TSM));
  LLVM_DEBUG(ES.runSessionLocked([&]() {
    dbgs() << "Finished emitting, for " << R.getTargetJITDylib().getName()
           << ", " << *this << "\n";
  }););
}

ObjectLayer::ObjectLayer(ExecutionSession &ES) : ES(ES) {}

ObjectLayer::~ObjectLayer() {}

Error ObjectLayer::add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O,
                       VModuleKey K) {
  auto ObjMU = BasicObjectLayerMaterializationUnit::Create(*this, std::move(K),
                                                           std::move(O));
  if (!ObjMU)
    return ObjMU.takeError();
  return JD.define(std::move(*ObjMU));
}

Expected<std::unique_ptr<BasicObjectLayerMaterializationUnit>>
BasicObjectLayerMaterializationUnit::Create(ObjectLayer &L, VModuleKey K,
                                            std::unique_ptr<MemoryBuffer> O) {
  auto SymbolFlags =
      getObjectSymbolFlags(L.getExecutionSession(), O->getMemBufferRef());

  if (!SymbolFlags)
    return SymbolFlags.takeError();

  return std::unique_ptr<BasicObjectLayerMaterializationUnit>(
      new BasicObjectLayerMaterializationUnit(L, K, std::move(O),
                                              std::move(*SymbolFlags)));
}

BasicObjectLayerMaterializationUnit::BasicObjectLayerMaterializationUnit(
    ObjectLayer &L, VModuleKey K, std::unique_ptr<MemoryBuffer> O,
    SymbolFlagsMap SymbolFlags)
    : MaterializationUnit(std::move(SymbolFlags), std::move(K)), L(L),
      O(std::move(O)) {}

StringRef BasicObjectLayerMaterializationUnit::getName() const {
  if (O)
    return O->getBufferIdentifier();
  return "<null object>";
}

void BasicObjectLayerMaterializationUnit::materialize(
    MaterializationResponsibility R) {
  L.emit(std::move(R), std::move(O));
}

void BasicObjectLayerMaterializationUnit::discard(const JITDylib &JD,
                                                  const SymbolStringPtr &Name) {
  // FIXME: Support object file level discard. This could be done by building a
  //        filter to pass to the object layer along with the object itself.
}

Expected<SymbolFlagsMap> getObjectSymbolFlags(ExecutionSession &ES,
                                              MemoryBufferRef ObjBuffer) {
  auto Obj = object::ObjectFile::createObjectFile(ObjBuffer);

  if (!Obj)
    return Obj.takeError();

  SymbolFlagsMap SymbolFlags;
  for (auto &Sym : (*Obj)->symbols()) {
    // Skip symbols not defined in this object file.
    if (Sym.getFlags() & object::BasicSymbolRef::SF_Undefined)
      continue;

    // Skip symbols that are not global.
    if (!(Sym.getFlags() & object::BasicSymbolRef::SF_Global))
      continue;

    auto Name = Sym.getName();
    if (!Name)
      return Name.takeError();
    auto InternedName = ES.intern(*Name);
    auto SymFlags = JITSymbolFlags::fromObjectSymbol(Sym);
    if (!SymFlags)
      return SymFlags.takeError();
    SymbolFlags[InternedName] = std::move(*SymFlags);
  }

  return SymbolFlags;
}

} // End namespace orc.
} // End namespace llvm.
