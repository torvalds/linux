//===- lib/Linker/IRMover.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Linker/IRMover.h"
#include "LinkDiagnosticInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Support/Error.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <utility>
using namespace llvm;

//===----------------------------------------------------------------------===//
// TypeMap implementation.
//===----------------------------------------------------------------------===//

namespace {
class TypeMapTy : public ValueMapTypeRemapper {
  /// This is a mapping from a source type to a destination type to use.
  DenseMap<Type *, Type *> MappedTypes;

  /// When checking to see if two subgraphs are isomorphic, we speculatively
  /// add types to MappedTypes, but keep track of them here in case we need to
  /// roll back.
  SmallVector<Type *, 16> SpeculativeTypes;

  SmallVector<StructType *, 16> SpeculativeDstOpaqueTypes;

  /// This is a list of non-opaque structs in the source module that are mapped
  /// to an opaque struct in the destination module.
  SmallVector<StructType *, 16> SrcDefinitionsToResolve;

  /// This is the set of opaque types in the destination modules who are
  /// getting a body from the source module.
  SmallPtrSet<StructType *, 16> DstResolvedOpaqueTypes;

public:
  TypeMapTy(IRMover::IdentifiedStructTypeSet &DstStructTypesSet)
      : DstStructTypesSet(DstStructTypesSet) {}

  IRMover::IdentifiedStructTypeSet &DstStructTypesSet;
  /// Indicate that the specified type in the destination module is conceptually
  /// equivalent to the specified type in the source module.
  void addTypeMapping(Type *DstTy, Type *SrcTy);

  /// Produce a body for an opaque type in the dest module from a type
  /// definition in the source module.
  void linkDefinedTypeBodies();

  /// Return the mapped type to use for the specified input type from the
  /// source module.
  Type *get(Type *SrcTy);
  Type *get(Type *SrcTy, SmallPtrSet<StructType *, 8> &Visited);

  void finishType(StructType *DTy, StructType *STy, ArrayRef<Type *> ETypes);

  FunctionType *get(FunctionType *T) {
    return cast<FunctionType>(get((Type *)T));
  }

private:
  Type *remapType(Type *SrcTy) override { return get(SrcTy); }

  bool areTypesIsomorphic(Type *DstTy, Type *SrcTy);
};
}

void TypeMapTy::addTypeMapping(Type *DstTy, Type *SrcTy) {
  assert(SpeculativeTypes.empty());
  assert(SpeculativeDstOpaqueTypes.empty());

  // Check to see if these types are recursively isomorphic and establish a
  // mapping between them if so.
  if (!areTypesIsomorphic(DstTy, SrcTy)) {
    // Oops, they aren't isomorphic.  Just discard this request by rolling out
    // any speculative mappings we've established.
    for (Type *Ty : SpeculativeTypes)
      MappedTypes.erase(Ty);

    SrcDefinitionsToResolve.resize(SrcDefinitionsToResolve.size() -
                                   SpeculativeDstOpaqueTypes.size());
    for (StructType *Ty : SpeculativeDstOpaqueTypes)
      DstResolvedOpaqueTypes.erase(Ty);
  } else {
    // SrcTy and DstTy are recursively ismorphic. We clear names of SrcTy
    // and all its descendants to lower amount of renaming in LLVM context
    // Renaming occurs because we load all source modules to the same context
    // and declaration with existing name gets renamed (i.e Foo -> Foo.42).
    // As a result we may get several different types in the destination
    // module, which are in fact the same.
    for (Type *Ty : SpeculativeTypes)
      if (auto *STy = dyn_cast<StructType>(Ty))
        if (STy->hasName())
          STy->setName("");
  }
  SpeculativeTypes.clear();
  SpeculativeDstOpaqueTypes.clear();
}

/// Recursively walk this pair of types, returning true if they are isomorphic,
/// false if they are not.
bool TypeMapTy::areTypesIsomorphic(Type *DstTy, Type *SrcTy) {
  // Two types with differing kinds are clearly not isomorphic.
  if (DstTy->getTypeID() != SrcTy->getTypeID())
    return false;

  // If we have an entry in the MappedTypes table, then we have our answer.
  Type *&Entry = MappedTypes[SrcTy];
  if (Entry)
    return Entry == DstTy;

  // Two identical types are clearly isomorphic.  Remember this
  // non-speculatively.
  if (DstTy == SrcTy) {
    Entry = DstTy;
    return true;
  }

  // Okay, we have two types with identical kinds that we haven't seen before.

  // If this is an opaque struct type, special case it.
  if (StructType *SSTy = dyn_cast<StructType>(SrcTy)) {
    // Mapping an opaque type to any struct, just keep the dest struct.
    if (SSTy->isOpaque()) {
      Entry = DstTy;
      SpeculativeTypes.push_back(SrcTy);
      return true;
    }

    // Mapping a non-opaque source type to an opaque dest.  If this is the first
    // type that we're mapping onto this destination type then we succeed.  Keep
    // the dest, but fill it in later. If this is the second (different) type
    // that we're trying to map onto the same opaque type then we fail.
    if (cast<StructType>(DstTy)->isOpaque()) {
      // We can only map one source type onto the opaque destination type.
      if (!DstResolvedOpaqueTypes.insert(cast<StructType>(DstTy)).second)
        return false;
      SrcDefinitionsToResolve.push_back(SSTy);
      SpeculativeTypes.push_back(SrcTy);
      SpeculativeDstOpaqueTypes.push_back(cast<StructType>(DstTy));
      Entry = DstTy;
      return true;
    }
  }

  // If the number of subtypes disagree between the two types, then we fail.
  if (SrcTy->getNumContainedTypes() != DstTy->getNumContainedTypes())
    return false;

  // Fail if any of the extra properties (e.g. array size) of the type disagree.
  if (isa<IntegerType>(DstTy))
    return false; // bitwidth disagrees.
  if (PointerType *PT = dyn_cast<PointerType>(DstTy)) {
    if (PT->getAddressSpace() != cast<PointerType>(SrcTy)->getAddressSpace())
      return false;
  } else if (FunctionType *FT = dyn_cast<FunctionType>(DstTy)) {
    if (FT->isVarArg() != cast<FunctionType>(SrcTy)->isVarArg())
      return false;
  } else if (StructType *DSTy = dyn_cast<StructType>(DstTy)) {
    StructType *SSTy = cast<StructType>(SrcTy);
    if (DSTy->isLiteral() != SSTy->isLiteral() ||
        DSTy->isPacked() != SSTy->isPacked())
      return false;
  } else if (auto *DSeqTy = dyn_cast<SequentialType>(DstTy)) {
    if (DSeqTy->getNumElements() !=
        cast<SequentialType>(SrcTy)->getNumElements())
      return false;
  }

  // Otherwise, we speculate that these two types will line up and recursively
  // check the subelements.
  Entry = DstTy;
  SpeculativeTypes.push_back(SrcTy);

  for (unsigned I = 0, E = SrcTy->getNumContainedTypes(); I != E; ++I)
    if (!areTypesIsomorphic(DstTy->getContainedType(I),
                            SrcTy->getContainedType(I)))
      return false;

  // If everything seems to have lined up, then everything is great.
  return true;
}

void TypeMapTy::linkDefinedTypeBodies() {
  SmallVector<Type *, 16> Elements;
  for (StructType *SrcSTy : SrcDefinitionsToResolve) {
    StructType *DstSTy = cast<StructType>(MappedTypes[SrcSTy]);
    assert(DstSTy->isOpaque());

    // Map the body of the source type over to a new body for the dest type.
    Elements.resize(SrcSTy->getNumElements());
    for (unsigned I = 0, E = Elements.size(); I != E; ++I)
      Elements[I] = get(SrcSTy->getElementType(I));

    DstSTy->setBody(Elements, SrcSTy->isPacked());
    DstStructTypesSet.switchToNonOpaque(DstSTy);
  }
  SrcDefinitionsToResolve.clear();
  DstResolvedOpaqueTypes.clear();
}

void TypeMapTy::finishType(StructType *DTy, StructType *STy,
                           ArrayRef<Type *> ETypes) {
  DTy->setBody(ETypes, STy->isPacked());

  // Steal STy's name.
  if (STy->hasName()) {
    SmallString<16> TmpName = STy->getName();
    STy->setName("");
    DTy->setName(TmpName);
  }

  DstStructTypesSet.addNonOpaque(DTy);
}

Type *TypeMapTy::get(Type *Ty) {
  SmallPtrSet<StructType *, 8> Visited;
  return get(Ty, Visited);
}

Type *TypeMapTy::get(Type *Ty, SmallPtrSet<StructType *, 8> &Visited) {
  // If we already have an entry for this type, return it.
  Type **Entry = &MappedTypes[Ty];
  if (*Entry)
    return *Entry;

  // These are types that LLVM itself will unique.
  bool IsUniqued = !isa<StructType>(Ty) || cast<StructType>(Ty)->isLiteral();

  if (!IsUniqued) {
    StructType *STy = cast<StructType>(Ty);
    // This is actually a type from the destination module, this can be reached
    // when this type is loaded in another module, added to DstStructTypesSet,
    // and then we reach the same type in another module where it has not been
    // added to MappedTypes. (PR37684)
    if (STy->getContext().isODRUniquingDebugTypes() && !STy->isOpaque() &&
        DstStructTypesSet.hasType(STy))
      return *Entry = STy;

#ifndef NDEBUG
    for (auto &Pair : MappedTypes) {
      assert(!(Pair.first != Ty && Pair.second == Ty) &&
             "mapping to a source type");
    }
#endif

    if (!Visited.insert(STy).second) {
      StructType *DTy = StructType::create(Ty->getContext());
      return *Entry = DTy;
    }
  }

  // If this is not a recursive type, then just map all of the elements and
  // then rebuild the type from inside out.
  SmallVector<Type *, 4> ElementTypes;

  // If there are no element types to map, then the type is itself.  This is
  // true for the anonymous {} struct, things like 'float', integers, etc.
  if (Ty->getNumContainedTypes() == 0 && IsUniqued)
    return *Entry = Ty;

  // Remap all of the elements, keeping track of whether any of them change.
  bool AnyChange = false;
  ElementTypes.resize(Ty->getNumContainedTypes());
  for (unsigned I = 0, E = Ty->getNumContainedTypes(); I != E; ++I) {
    ElementTypes[I] = get(Ty->getContainedType(I), Visited);
    AnyChange |= ElementTypes[I] != Ty->getContainedType(I);
  }

  // If we found our type while recursively processing stuff, just use it.
  Entry = &MappedTypes[Ty];
  if (*Entry) {
    if (auto *DTy = dyn_cast<StructType>(*Entry)) {
      if (DTy->isOpaque()) {
        auto *STy = cast<StructType>(Ty);
        finishType(DTy, STy, ElementTypes);
      }
    }
    return *Entry;
  }

  // If all of the element types mapped directly over and the type is not
  // a named struct, then the type is usable as-is.
  if (!AnyChange && IsUniqued)
    return *Entry = Ty;

  // Otherwise, rebuild a modified type.
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("unknown derived type to remap");
  case Type::ArrayTyID:
    return *Entry = ArrayType::get(ElementTypes[0],
                                   cast<ArrayType>(Ty)->getNumElements());
  case Type::VectorTyID:
    return *Entry = VectorType::get(ElementTypes[0],
                                    cast<VectorType>(Ty)->getNumElements());
  case Type::PointerTyID:
    return *Entry = PointerType::get(ElementTypes[0],
                                     cast<PointerType>(Ty)->getAddressSpace());
  case Type::FunctionTyID:
    return *Entry = FunctionType::get(ElementTypes[0],
                                      makeArrayRef(ElementTypes).slice(1),
                                      cast<FunctionType>(Ty)->isVarArg());
  case Type::StructTyID: {
    auto *STy = cast<StructType>(Ty);
    bool IsPacked = STy->isPacked();
    if (IsUniqued)
      return *Entry = StructType::get(Ty->getContext(), ElementTypes, IsPacked);

    // If the type is opaque, we can just use it directly.
    if (STy->isOpaque()) {
      DstStructTypesSet.addOpaque(STy);
      return *Entry = Ty;
    }

    if (StructType *OldT =
            DstStructTypesSet.findNonOpaque(ElementTypes, IsPacked)) {
      STy->setName("");
      return *Entry = OldT;
    }

    if (!AnyChange) {
      DstStructTypesSet.addNonOpaque(STy);
      return *Entry = Ty;
    }

    StructType *DTy = StructType::create(Ty->getContext());
    finishType(DTy, STy, ElementTypes);
    return *Entry = DTy;
  }
  }
}

LinkDiagnosticInfo::LinkDiagnosticInfo(DiagnosticSeverity Severity,
                                       const Twine &Msg)
    : DiagnosticInfo(DK_Linker, Severity), Msg(Msg) {}
void LinkDiagnosticInfo::print(DiagnosticPrinter &DP) const { DP << Msg; }

//===----------------------------------------------------------------------===//
// IRLinker implementation.
//===----------------------------------------------------------------------===//

namespace {
class IRLinker;

/// Creates prototypes for functions that are lazily linked on the fly. This
/// speeds up linking for modules with many/ lazily linked functions of which
/// few get used.
class GlobalValueMaterializer final : public ValueMaterializer {
  IRLinker &TheIRLinker;

public:
  GlobalValueMaterializer(IRLinker &TheIRLinker) : TheIRLinker(TheIRLinker) {}
  Value *materialize(Value *V) override;
};

class LocalValueMaterializer final : public ValueMaterializer {
  IRLinker &TheIRLinker;

public:
  LocalValueMaterializer(IRLinker &TheIRLinker) : TheIRLinker(TheIRLinker) {}
  Value *materialize(Value *V) override;
};

/// Type of the Metadata map in \a ValueToValueMapTy.
typedef DenseMap<const Metadata *, TrackingMDRef> MDMapT;

/// This is responsible for keeping track of the state used for moving data
/// from SrcM to DstM.
class IRLinker {
  Module &DstM;
  std::unique_ptr<Module> SrcM;

  /// See IRMover::move().
  std::function<void(GlobalValue &, IRMover::ValueAdder)> AddLazyFor;

  TypeMapTy TypeMap;
  GlobalValueMaterializer GValMaterializer;
  LocalValueMaterializer LValMaterializer;

  /// A metadata map that's shared between IRLinker instances.
  MDMapT &SharedMDs;

  /// Mapping of values from what they used to be in Src, to what they are now
  /// in DstM.  ValueToValueMapTy is a ValueMap, which involves some overhead
  /// due to the use of Value handles which the Linker doesn't actually need,
  /// but this allows us to reuse the ValueMapper code.
  ValueToValueMapTy ValueMap;
  ValueToValueMapTy AliasValueMap;

  DenseSet<GlobalValue *> ValuesToLink;
  std::vector<GlobalValue *> Worklist;

  void maybeAdd(GlobalValue *GV) {
    if (ValuesToLink.insert(GV).second)
      Worklist.push_back(GV);
  }

  /// Whether we are importing globals for ThinLTO, as opposed to linking the
  /// source module. If this flag is set, it means that we can rely on some
  /// other object file to define any non-GlobalValue entities defined by the
  /// source module. This currently causes us to not link retained types in
  /// debug info metadata and module inline asm.
  bool IsPerformingImport;

  /// Set to true when all global value body linking is complete (including
  /// lazy linking). Used to prevent metadata linking from creating new
  /// references.
  bool DoneLinkingBodies = false;

  /// The Error encountered during materialization. We use an Optional here to
  /// avoid needing to manage an unconsumed success value.
  Optional<Error> FoundError;
  void setError(Error E) {
    if (E)
      FoundError = std::move(E);
  }

  /// Most of the errors produced by this module are inconvertible StringErrors.
  /// This convenience function lets us return one of those more easily.
  Error stringErr(const Twine &T) {
    return make_error<StringError>(T, inconvertibleErrorCode());
  }

  /// Entry point for mapping values and alternate context for mapping aliases.
  ValueMapper Mapper;
  unsigned AliasMCID;

  /// Handles cloning of a global values from the source module into
  /// the destination module, including setting the attributes and visibility.
  GlobalValue *copyGlobalValueProto(const GlobalValue *SGV, bool ForDefinition);

  void emitWarning(const Twine &Message) {
    SrcM->getContext().diagnose(LinkDiagnosticInfo(DS_Warning, Message));
  }

  /// Given a global in the source module, return the global in the
  /// destination module that is being linked to, if any.
  GlobalValue *getLinkedToGlobal(const GlobalValue *SrcGV) {
    // If the source has no name it can't link.  If it has local linkage,
    // there is no name match-up going on.
    if (!SrcGV->hasName() || SrcGV->hasLocalLinkage())
      return nullptr;

    // Otherwise see if we have a match in the destination module's symtab.
    GlobalValue *DGV = DstM.getNamedValue(SrcGV->getName());
    if (!DGV)
      return nullptr;

    // If we found a global with the same name in the dest module, but it has
    // internal linkage, we are really not doing any linkage here.
    if (DGV->hasLocalLinkage())
      return nullptr;

    // Otherwise, we do in fact link to the destination global.
    return DGV;
  }

  void computeTypeMapping();

  Expected<Constant *> linkAppendingVarProto(GlobalVariable *DstGV,
                                             const GlobalVariable *SrcGV);

  /// Given the GlobaValue \p SGV in the source module, and the matching
  /// GlobalValue \p DGV (if any), return true if the linker will pull \p SGV
  /// into the destination module.
  ///
  /// Note this code may call the client-provided \p AddLazyFor.
  bool shouldLink(GlobalValue *DGV, GlobalValue &SGV);
  Expected<Constant *> linkGlobalValueProto(GlobalValue *GV, bool ForAlias);

  Error linkModuleFlagsMetadata();

  void linkGlobalVariable(GlobalVariable &Dst, GlobalVariable &Src);
  Error linkFunctionBody(Function &Dst, Function &Src);
  void linkAliasBody(GlobalAlias &Dst, GlobalAlias &Src);
  Error linkGlobalValueBody(GlobalValue &Dst, GlobalValue &Src);

  /// Functions that take care of cloning a specific global value type
  /// into the destination module.
  GlobalVariable *copyGlobalVariableProto(const GlobalVariable *SGVar);
  Function *copyFunctionProto(const Function *SF);
  GlobalValue *copyGlobalAliasProto(const GlobalAlias *SGA);

  /// When importing for ThinLTO, prevent importing of types listed on
  /// the DICompileUnit that we don't need a copy of in the importing
  /// module.
  void prepareCompileUnitsForImport();
  void linkNamedMDNodes();

public:
  IRLinker(Module &DstM, MDMapT &SharedMDs,
           IRMover::IdentifiedStructTypeSet &Set, std::unique_ptr<Module> SrcM,
           ArrayRef<GlobalValue *> ValuesToLink,
           std::function<void(GlobalValue &, IRMover::ValueAdder)> AddLazyFor,
           bool IsPerformingImport)
      : DstM(DstM), SrcM(std::move(SrcM)), AddLazyFor(std::move(AddLazyFor)),
        TypeMap(Set), GValMaterializer(*this), LValMaterializer(*this),
        SharedMDs(SharedMDs), IsPerformingImport(IsPerformingImport),
        Mapper(ValueMap, RF_MoveDistinctMDs | RF_IgnoreMissingLocals, &TypeMap,
               &GValMaterializer),
        AliasMCID(Mapper.registerAlternateMappingContext(AliasValueMap,
                                                         &LValMaterializer)) {
    ValueMap.getMDMap() = std::move(SharedMDs);
    for (GlobalValue *GV : ValuesToLink)
      maybeAdd(GV);
    if (IsPerformingImport)
      prepareCompileUnitsForImport();
  }
  ~IRLinker() { SharedMDs = std::move(*ValueMap.getMDMap()); }

  Error run();
  Value *materialize(Value *V, bool ForAlias);
};
}

/// The LLVM SymbolTable class autorenames globals that conflict in the symbol
/// table. This is good for all clients except for us. Go through the trouble
/// to force this back.
static void forceRenaming(GlobalValue *GV, StringRef Name) {
  // If the global doesn't force its name or if it already has the right name,
  // there is nothing for us to do.
  if (GV->hasLocalLinkage() || GV->getName() == Name)
    return;

  Module *M = GV->getParent();

  // If there is a conflict, rename the conflict.
  if (GlobalValue *ConflictGV = M->getNamedValue(Name)) {
    GV->takeName(ConflictGV);
    ConflictGV->setName(Name); // This will cause ConflictGV to get renamed
    assert(ConflictGV->getName() != Name && "forceRenaming didn't work");
  } else {
    GV->setName(Name); // Force the name back
  }
}

Value *GlobalValueMaterializer::materialize(Value *SGV) {
  return TheIRLinker.materialize(SGV, false);
}

Value *LocalValueMaterializer::materialize(Value *SGV) {
  return TheIRLinker.materialize(SGV, true);
}

Value *IRLinker::materialize(Value *V, bool ForAlias) {
  auto *SGV = dyn_cast<GlobalValue>(V);
  if (!SGV)
    return nullptr;

  Expected<Constant *> NewProto = linkGlobalValueProto(SGV, ForAlias);
  if (!NewProto) {
    setError(NewProto.takeError());
    return nullptr;
  }
  if (!*NewProto)
    return nullptr;

  GlobalValue *New = dyn_cast<GlobalValue>(*NewProto);
  if (!New)
    return *NewProto;

  // If we already created the body, just return.
  if (auto *F = dyn_cast<Function>(New)) {
    if (!F->isDeclaration())
      return New;
  } else if (auto *V = dyn_cast<GlobalVariable>(New)) {
    if (V->hasInitializer() || V->hasAppendingLinkage())
      return New;
  } else {
    auto *A = cast<GlobalAlias>(New);
    if (A->getAliasee())
      return New;
  }

  // When linking a global for an alias, it will always be linked. However we
  // need to check if it was not already scheduled to satisfy a reference from a
  // regular global value initializer. We know if it has been schedule if the
  // "New" GlobalValue that is mapped here for the alias is the same as the one
  // already mapped. If there is an entry in the ValueMap but the value is
  // different, it means that the value already had a definition in the
  // destination module (linkonce for instance), but we need a new definition
  // for the alias ("New" will be different.
  if (ForAlias && ValueMap.lookup(SGV) == New)
    return New;

  if (ForAlias || shouldLink(New, *SGV))
    setError(linkGlobalValueBody(*New, *SGV));

  return New;
}

/// Loop through the global variables in the src module and merge them into the
/// dest module.
GlobalVariable *IRLinker::copyGlobalVariableProto(const GlobalVariable *SGVar) {
  // No linking to be performed or linking from the source: simply create an
  // identical version of the symbol over in the dest module... the
  // initializer will be filled in later by LinkGlobalInits.
  GlobalVariable *NewDGV =
      new GlobalVariable(DstM, TypeMap.get(SGVar->getValueType()),
                         SGVar->isConstant(), GlobalValue::ExternalLinkage,
                         /*init*/ nullptr, SGVar->getName(),
                         /*insertbefore*/ nullptr, SGVar->getThreadLocalMode(),
                         SGVar->getType()->getAddressSpace());
  NewDGV->setAlignment(SGVar->getAlignment());
  NewDGV->copyAttributesFrom(SGVar);
  return NewDGV;
}

/// Link the function in the source module into the destination module if
/// needed, setting up mapping information.
Function *IRLinker::copyFunctionProto(const Function *SF) {
  // If there is no linkage to be performed or we are linking from the source,
  // bring SF over.
  auto *F =
      Function::Create(TypeMap.get(SF->getFunctionType()),
                       GlobalValue::ExternalLinkage, SF->getName(), &DstM);
  F->copyAttributesFrom(SF);
  return F;
}

/// Set up prototypes for any aliases that come over from the source module.
GlobalValue *IRLinker::copyGlobalAliasProto(const GlobalAlias *SGA) {
  // If there is no linkage to be performed or we're linking from the source,
  // bring over SGA.
  auto *Ty = TypeMap.get(SGA->getValueType());
  auto *GA =
      GlobalAlias::create(Ty, SGA->getType()->getPointerAddressSpace(),
                          GlobalValue::ExternalLinkage, SGA->getName(), &DstM);
  GA->copyAttributesFrom(SGA);
  return GA;
}

GlobalValue *IRLinker::copyGlobalValueProto(const GlobalValue *SGV,
                                            bool ForDefinition) {
  GlobalValue *NewGV;
  if (auto *SGVar = dyn_cast<GlobalVariable>(SGV)) {
    NewGV = copyGlobalVariableProto(SGVar);
  } else if (auto *SF = dyn_cast<Function>(SGV)) {
    NewGV = copyFunctionProto(SF);
  } else {
    if (ForDefinition)
      NewGV = copyGlobalAliasProto(cast<GlobalAlias>(SGV));
    else if (SGV->getValueType()->isFunctionTy())
      NewGV =
          Function::Create(cast<FunctionType>(TypeMap.get(SGV->getValueType())),
                           GlobalValue::ExternalLinkage, SGV->getName(), &DstM);
    else
      NewGV = new GlobalVariable(
          DstM, TypeMap.get(SGV->getValueType()),
          /*isConstant*/ false, GlobalValue::ExternalLinkage,
          /*init*/ nullptr, SGV->getName(),
          /*insertbefore*/ nullptr, SGV->getThreadLocalMode(),
          SGV->getType()->getAddressSpace());
  }

  if (ForDefinition)
    NewGV->setLinkage(SGV->getLinkage());
  else if (SGV->hasExternalWeakLinkage())
    NewGV->setLinkage(GlobalValue::ExternalWeakLinkage);

  if (auto *NewGO = dyn_cast<GlobalObject>(NewGV)) {
    // Metadata for global variables and function declarations is copied eagerly.
    if (isa<GlobalVariable>(SGV) || SGV->isDeclaration())
      NewGO->copyMetadata(cast<GlobalObject>(SGV), 0);
  }

  // Remove these copied constants in case this stays a declaration, since
  // they point to the source module. If the def is linked the values will
  // be mapped in during linkFunctionBody.
  if (auto *NewF = dyn_cast<Function>(NewGV)) {
    NewF->setPersonalityFn(nullptr);
    NewF->setPrefixData(nullptr);
    NewF->setPrologueData(nullptr);
  }

  return NewGV;
}

static StringRef getTypeNamePrefix(StringRef Name) {
  size_t DotPos = Name.rfind('.');
  return (DotPos == 0 || DotPos == StringRef::npos || Name.back() == '.' ||
          !isdigit(static_cast<unsigned char>(Name[DotPos + 1])))
             ? Name
             : Name.substr(0, DotPos);
}

/// Loop over all of the linked values to compute type mappings.  For example,
/// if we link "extern Foo *x" and "Foo *x = NULL", then we have two struct
/// types 'Foo' but one got renamed when the module was loaded into the same
/// LLVMContext.
void IRLinker::computeTypeMapping() {
  for (GlobalValue &SGV : SrcM->globals()) {
    GlobalValue *DGV = getLinkedToGlobal(&SGV);
    if (!DGV)
      continue;

    if (!DGV->hasAppendingLinkage() || !SGV.hasAppendingLinkage()) {
      TypeMap.addTypeMapping(DGV->getType(), SGV.getType());
      continue;
    }

    // Unify the element type of appending arrays.
    ArrayType *DAT = cast<ArrayType>(DGV->getValueType());
    ArrayType *SAT = cast<ArrayType>(SGV.getValueType());
    TypeMap.addTypeMapping(DAT->getElementType(), SAT->getElementType());
  }

  for (GlobalValue &SGV : *SrcM)
    if (GlobalValue *DGV = getLinkedToGlobal(&SGV))
      TypeMap.addTypeMapping(DGV->getType(), SGV.getType());

  for (GlobalValue &SGV : SrcM->aliases())
    if (GlobalValue *DGV = getLinkedToGlobal(&SGV))
      TypeMap.addTypeMapping(DGV->getType(), SGV.getType());

  // Incorporate types by name, scanning all the types in the source module.
  // At this point, the destination module may have a type "%foo = { i32 }" for
  // example.  When the source module got loaded into the same LLVMContext, if
  // it had the same type, it would have been renamed to "%foo.42 = { i32 }".
  std::vector<StructType *> Types = SrcM->getIdentifiedStructTypes();
  for (StructType *ST : Types) {
    if (!ST->hasName())
      continue;

    if (TypeMap.DstStructTypesSet.hasType(ST)) {
      // This is actually a type from the destination module.
      // getIdentifiedStructTypes() can have found it by walking debug info
      // metadata nodes, some of which get linked by name when ODR Type Uniquing
      // is enabled on the Context, from the source to the destination module.
      continue;
    }

    auto STTypePrefix = getTypeNamePrefix(ST->getName());
    if (STTypePrefix.size()== ST->getName().size())
      continue;

    // Check to see if the destination module has a struct with the prefix name.
    StructType *DST = DstM.getTypeByName(STTypePrefix);
    if (!DST)
      continue;

    // Don't use it if this actually came from the source module. They're in
    // the same LLVMContext after all. Also don't use it unless the type is
    // actually used in the destination module. This can happen in situations
    // like this:
    //
    //      Module A                         Module B
    //      --------                         --------
    //   %Z = type { %A }                %B = type { %C.1 }
    //   %A = type { %B.1, [7 x i8] }    %C.1 = type { i8* }
    //   %B.1 = type { %C }              %A.2 = type { %B.3, [5 x i8] }
    //   %C = type { i8* }               %B.3 = type { %C.1 }
    //
    // When we link Module B with Module A, the '%B' in Module B is
    // used. However, that would then use '%C.1'. But when we process '%C.1',
    // we prefer to take the '%C' version. So we are then left with both
    // '%C.1' and '%C' being used for the same types. This leads to some
    // variables using one type and some using the other.
    if (TypeMap.DstStructTypesSet.hasType(DST))
      TypeMap.addTypeMapping(DST, ST);
  }

  // Now that we have discovered all of the type equivalences, get a body for
  // any 'opaque' types in the dest module that are now resolved.
  TypeMap.linkDefinedTypeBodies();
}

static void getArrayElements(const Constant *C,
                             SmallVectorImpl<Constant *> &Dest) {
  unsigned NumElements = cast<ArrayType>(C->getType())->getNumElements();

  for (unsigned i = 0; i != NumElements; ++i)
    Dest.push_back(C->getAggregateElement(i));
}

/// If there were any appending global variables, link them together now.
Expected<Constant *>
IRLinker::linkAppendingVarProto(GlobalVariable *DstGV,
                                const GlobalVariable *SrcGV) {
  Type *EltTy = cast<ArrayType>(TypeMap.get(SrcGV->getValueType()))
                    ->getElementType();

  // FIXME: This upgrade is done during linking to support the C API.  Once the
  // old form is deprecated, we should move this upgrade to
  // llvm::UpgradeGlobalVariable() and simplify the logic here and in
  // Mapper::mapAppendingVariable() in ValueMapper.cpp.
  StringRef Name = SrcGV->getName();
  bool IsNewStructor = false;
  bool IsOldStructor = false;
  if (Name == "llvm.global_ctors" || Name == "llvm.global_dtors") {
    if (cast<StructType>(EltTy)->getNumElements() == 3)
      IsNewStructor = true;
    else
      IsOldStructor = true;
  }

  PointerType *VoidPtrTy = Type::getInt8Ty(SrcGV->getContext())->getPointerTo();
  if (IsOldStructor) {
    auto &ST = *cast<StructType>(EltTy);
    Type *Tys[3] = {ST.getElementType(0), ST.getElementType(1), VoidPtrTy};
    EltTy = StructType::get(SrcGV->getContext(), Tys, false);
  }

  uint64_t DstNumElements = 0;
  if (DstGV) {
    ArrayType *DstTy = cast<ArrayType>(DstGV->getValueType());
    DstNumElements = DstTy->getNumElements();

    if (!SrcGV->hasAppendingLinkage() || !DstGV->hasAppendingLinkage())
      return stringErr(
          "Linking globals named '" + SrcGV->getName() +
          "': can only link appending global with another appending "
          "global!");

    // Check to see that they two arrays agree on type.
    if (EltTy != DstTy->getElementType())
      return stringErr("Appending variables with different element types!");
    if (DstGV->isConstant() != SrcGV->isConstant())
      return stringErr("Appending variables linked with different const'ness!");

    if (DstGV->getAlignment() != SrcGV->getAlignment())
      return stringErr(
          "Appending variables with different alignment need to be linked!");

    if (DstGV->getVisibility() != SrcGV->getVisibility())
      return stringErr(
          "Appending variables with different visibility need to be linked!");

    if (DstGV->hasGlobalUnnamedAddr() != SrcGV->hasGlobalUnnamedAddr())
      return stringErr(
          "Appending variables with different unnamed_addr need to be linked!");

    if (DstGV->getSection() != SrcGV->getSection())
      return stringErr(
          "Appending variables with different section name need to be linked!");
  }

  SmallVector<Constant *, 16> SrcElements;
  getArrayElements(SrcGV->getInitializer(), SrcElements);

  if (IsNewStructor) {
    auto It = remove_if(SrcElements, [this](Constant *E) {
      auto *Key =
          dyn_cast<GlobalValue>(E->getAggregateElement(2)->stripPointerCasts());
      if (!Key)
        return false;
      GlobalValue *DGV = getLinkedToGlobal(Key);
      return !shouldLink(DGV, *Key);
    });
    SrcElements.erase(It, SrcElements.end());
  }
  uint64_t NewSize = DstNumElements + SrcElements.size();
  ArrayType *NewType = ArrayType::get(EltTy, NewSize);

  // Create the new global variable.
  GlobalVariable *NG = new GlobalVariable(
      DstM, NewType, SrcGV->isConstant(), SrcGV->getLinkage(),
      /*init*/ nullptr, /*name*/ "", DstGV, SrcGV->getThreadLocalMode(),
      SrcGV->getType()->getAddressSpace());

  NG->copyAttributesFrom(SrcGV);
  forceRenaming(NG, SrcGV->getName());

  Constant *Ret = ConstantExpr::getBitCast(NG, TypeMap.get(SrcGV->getType()));

  Mapper.scheduleMapAppendingVariable(*NG,
                                      DstGV ? DstGV->getInitializer() : nullptr,
                                      IsOldStructor, SrcElements);

  // Replace any uses of the two global variables with uses of the new
  // global.
  if (DstGV) {
    DstGV->replaceAllUsesWith(ConstantExpr::getBitCast(NG, DstGV->getType()));
    DstGV->eraseFromParent();
  }

  return Ret;
}

bool IRLinker::shouldLink(GlobalValue *DGV, GlobalValue &SGV) {
  if (ValuesToLink.count(&SGV) || SGV.hasLocalLinkage())
    return true;

  if (DGV && !DGV->isDeclarationForLinker())
    return false;

  if (SGV.isDeclaration() || DoneLinkingBodies)
    return false;

  // Callback to the client to give a chance to lazily add the Global to the
  // list of value to link.
  bool LazilyAdded = false;
  AddLazyFor(SGV, [this, &LazilyAdded](GlobalValue &GV) {
    maybeAdd(&GV);
    LazilyAdded = true;
  });
  return LazilyAdded;
}

Expected<Constant *> IRLinker::linkGlobalValueProto(GlobalValue *SGV,
                                                    bool ForAlias) {
  GlobalValue *DGV = getLinkedToGlobal(SGV);

  bool ShouldLink = shouldLink(DGV, *SGV);

  // just missing from map
  if (ShouldLink) {
    auto I = ValueMap.find(SGV);
    if (I != ValueMap.end())
      return cast<Constant>(I->second);

    I = AliasValueMap.find(SGV);
    if (I != AliasValueMap.end())
      return cast<Constant>(I->second);
  }

  if (!ShouldLink && ForAlias)
    DGV = nullptr;

  // Handle the ultra special appending linkage case first.
  assert(!DGV || SGV->hasAppendingLinkage() == DGV->hasAppendingLinkage());
  if (SGV->hasAppendingLinkage())
    return linkAppendingVarProto(cast_or_null<GlobalVariable>(DGV),
                                 cast<GlobalVariable>(SGV));

  GlobalValue *NewGV;
  if (DGV && !ShouldLink) {
    NewGV = DGV;
  } else {
    // If we are done linking global value bodies (i.e. we are performing
    // metadata linking), don't link in the global value due to this
    // reference, simply map it to null.
    if (DoneLinkingBodies)
      return nullptr;

    NewGV = copyGlobalValueProto(SGV, ShouldLink || ForAlias);
    if (ShouldLink || !ForAlias)
      forceRenaming(NewGV, SGV->getName());
  }

  // Overloaded intrinsics have overloaded types names as part of their
  // names. If we renamed overloaded types we should rename the intrinsic
  // as well.
  if (Function *F = dyn_cast<Function>(NewGV))
    if (auto Remangled = Intrinsic::remangleIntrinsicFunction(F))
      NewGV = Remangled.getValue();

  if (ShouldLink || ForAlias) {
    if (const Comdat *SC = SGV->getComdat()) {
      if (auto *GO = dyn_cast<GlobalObject>(NewGV)) {
        Comdat *DC = DstM.getOrInsertComdat(SC->getName());
        DC->setSelectionKind(SC->getSelectionKind());
        GO->setComdat(DC);
      }
    }
  }

  if (!ShouldLink && ForAlias)
    NewGV->setLinkage(GlobalValue::InternalLinkage);

  Constant *C = NewGV;
  // Only create a bitcast if necessary. In particular, with
  // DebugTypeODRUniquing we may reach metadata in the destination module
  // containing a GV from the source module, in which case SGV will be
  // the same as DGV and NewGV, and TypeMap.get() will assert since it
  // assumes it is being invoked on a type in the source module.
  if (DGV && NewGV != SGV) {
    C = ConstantExpr::getPointerBitCastOrAddrSpaceCast(
      NewGV, TypeMap.get(SGV->getType()));
  }

  if (DGV && NewGV != DGV) {
    DGV->replaceAllUsesWith(
      ConstantExpr::getPointerBitCastOrAddrSpaceCast(NewGV, DGV->getType()));
    DGV->eraseFromParent();
  }

  return C;
}

/// Update the initializers in the Dest module now that all globals that may be
/// referenced are in Dest.
void IRLinker::linkGlobalVariable(GlobalVariable &Dst, GlobalVariable &Src) {
  // Figure out what the initializer looks like in the dest module.
  Mapper.scheduleMapGlobalInitializer(Dst, *Src.getInitializer());
}

/// Copy the source function over into the dest function and fix up references
/// to values. At this point we know that Dest is an external function, and
/// that Src is not.
Error IRLinker::linkFunctionBody(Function &Dst, Function &Src) {
  assert(Dst.isDeclaration() && !Src.isDeclaration());

  // Materialize if needed.
  if (Error Err = Src.materialize())
    return Err;

  // Link in the operands without remapping.
  if (Src.hasPrefixData())
    Dst.setPrefixData(Src.getPrefixData());
  if (Src.hasPrologueData())
    Dst.setPrologueData(Src.getPrologueData());
  if (Src.hasPersonalityFn())
    Dst.setPersonalityFn(Src.getPersonalityFn());

  // Copy over the metadata attachments without remapping.
  Dst.copyMetadata(&Src, 0);

  // Steal arguments and splice the body of Src into Dst.
  Dst.stealArgumentListFrom(Src);
  Dst.getBasicBlockList().splice(Dst.end(), Src.getBasicBlockList());

  // Everything has been moved over.  Remap it.
  Mapper.scheduleRemapFunction(Dst);
  return Error::success();
}

void IRLinker::linkAliasBody(GlobalAlias &Dst, GlobalAlias &Src) {
  Mapper.scheduleMapGlobalAliasee(Dst, *Src.getAliasee(), AliasMCID);
}

Error IRLinker::linkGlobalValueBody(GlobalValue &Dst, GlobalValue &Src) {
  if (auto *F = dyn_cast<Function>(&Src))
    return linkFunctionBody(cast<Function>(Dst), *F);
  if (auto *GVar = dyn_cast<GlobalVariable>(&Src)) {
    linkGlobalVariable(cast<GlobalVariable>(Dst), *GVar);
    return Error::success();
  }
  linkAliasBody(cast<GlobalAlias>(Dst), cast<GlobalAlias>(Src));
  return Error::success();
}

void IRLinker::prepareCompileUnitsForImport() {
  NamedMDNode *SrcCompileUnits = SrcM->getNamedMetadata("llvm.dbg.cu");
  if (!SrcCompileUnits)
    return;
  // When importing for ThinLTO, prevent importing of types listed on
  // the DICompileUnit that we don't need a copy of in the importing
  // module. They will be emitted by the originating module.
  for (unsigned I = 0, E = SrcCompileUnits->getNumOperands(); I != E; ++I) {
    auto *CU = cast<DICompileUnit>(SrcCompileUnits->getOperand(I));
    assert(CU && "Expected valid compile unit");
    // Enums, macros, and retained types don't need to be listed on the
    // imported DICompileUnit. This means they will only be imported
    // if reached from the mapped IR. Do this by setting their value map
    // entries to nullptr, which will automatically prevent their importing
    // when reached from the DICompileUnit during metadata mapping.
    ValueMap.MD()[CU->getRawEnumTypes()].reset(nullptr);
    ValueMap.MD()[CU->getRawMacros()].reset(nullptr);
    ValueMap.MD()[CU->getRawRetainedTypes()].reset(nullptr);
    // The original definition (or at least its debug info - if the variable is
    // internalized an optimized away) will remain in the source module, so
    // there's no need to import them.
    // If LLVM ever does more advanced optimizations on global variables
    // (removing/localizing write operations, for instance) that can track
    // through debug info, this decision may need to be revisited - but do so
    // with care when it comes to debug info size. Emitting small CUs containing
    // only a few imported entities into every destination module may be very
    // size inefficient.
    ValueMap.MD()[CU->getRawGlobalVariables()].reset(nullptr);

    // Imported entities only need to be mapped in if they have local
    // scope, as those might correspond to an imported entity inside a
    // function being imported (any locally scoped imported entities that
    // don't end up referenced by an imported function will not be emitted
    // into the object). Imported entities not in a local scope
    // (e.g. on the namespace) only need to be emitted by the originating
    // module. Create a list of the locally scoped imported entities, and
    // replace the source CUs imported entity list with the new list, so
    // only those are mapped in.
    // FIXME: Locally-scoped imported entities could be moved to the
    // functions they are local to instead of listing them on the CU, and
    // we would naturally only link in those needed by function importing.
    SmallVector<TrackingMDNodeRef, 4> AllImportedModules;
    bool ReplaceImportedEntities = false;
    for (auto *IE : CU->getImportedEntities()) {
      DIScope *Scope = IE->getScope();
      assert(Scope && "Invalid Scope encoding!");
      if (isa<DILocalScope>(Scope))
        AllImportedModules.emplace_back(IE);
      else
        ReplaceImportedEntities = true;
    }
    if (ReplaceImportedEntities) {
      if (!AllImportedModules.empty())
        CU->replaceImportedEntities(MDTuple::get(
            CU->getContext(),
            SmallVector<Metadata *, 16>(AllImportedModules.begin(),
                                        AllImportedModules.end())));
      else
        // If there were no local scope imported entities, we can map
        // the whole list to nullptr.
        ValueMap.MD()[CU->getRawImportedEntities()].reset(nullptr);
    }
  }
}

/// Insert all of the named MDNodes in Src into the Dest module.
void IRLinker::linkNamedMDNodes() {
  const NamedMDNode *SrcModFlags = SrcM->getModuleFlagsMetadata();
  for (const NamedMDNode &NMD : SrcM->named_metadata()) {
    // Don't link module flags here. Do them separately.
    if (&NMD == SrcModFlags)
      continue;
    NamedMDNode *DestNMD = DstM.getOrInsertNamedMetadata(NMD.getName());
    // Add Src elements into Dest node.
    for (const MDNode *Op : NMD.operands())
      DestNMD->addOperand(Mapper.mapMDNode(*Op));
  }
}

/// Merge the linker flags in Src into the Dest module.
Error IRLinker::linkModuleFlagsMetadata() {
  // If the source module has no module flags, we are done.
  const NamedMDNode *SrcModFlags = SrcM->getModuleFlagsMetadata();
  if (!SrcModFlags)
    return Error::success();

  // If the destination module doesn't have module flags yet, then just copy
  // over the source module's flags.
  NamedMDNode *DstModFlags = DstM.getOrInsertModuleFlagsMetadata();
  if (DstModFlags->getNumOperands() == 0) {
    for (unsigned I = 0, E = SrcModFlags->getNumOperands(); I != E; ++I)
      DstModFlags->addOperand(SrcModFlags->getOperand(I));

    return Error::success();
  }

  // First build a map of the existing module flags and requirements.
  DenseMap<MDString *, std::pair<MDNode *, unsigned>> Flags;
  SmallSetVector<MDNode *, 16> Requirements;
  for (unsigned I = 0, E = DstModFlags->getNumOperands(); I != E; ++I) {
    MDNode *Op = DstModFlags->getOperand(I);
    ConstantInt *Behavior = mdconst::extract<ConstantInt>(Op->getOperand(0));
    MDString *ID = cast<MDString>(Op->getOperand(1));

    if (Behavior->getZExtValue() == Module::Require) {
      Requirements.insert(cast<MDNode>(Op->getOperand(2)));
    } else {
      Flags[ID] = std::make_pair(Op, I);
    }
  }

  // Merge in the flags from the source module, and also collect its set of
  // requirements.
  for (unsigned I = 0, E = SrcModFlags->getNumOperands(); I != E; ++I) {
    MDNode *SrcOp = SrcModFlags->getOperand(I);
    ConstantInt *SrcBehavior =
        mdconst::extract<ConstantInt>(SrcOp->getOperand(0));
    MDString *ID = cast<MDString>(SrcOp->getOperand(1));
    MDNode *DstOp;
    unsigned DstIndex;
    std::tie(DstOp, DstIndex) = Flags.lookup(ID);
    unsigned SrcBehaviorValue = SrcBehavior->getZExtValue();

    // If this is a requirement, add it and continue.
    if (SrcBehaviorValue == Module::Require) {
      // If the destination module does not already have this requirement, add
      // it.
      if (Requirements.insert(cast<MDNode>(SrcOp->getOperand(2)))) {
        DstModFlags->addOperand(SrcOp);
      }
      continue;
    }

    // If there is no existing flag with this ID, just add it.
    if (!DstOp) {
      Flags[ID] = std::make_pair(SrcOp, DstModFlags->getNumOperands());
      DstModFlags->addOperand(SrcOp);
      continue;
    }

    // Otherwise, perform a merge.
    ConstantInt *DstBehavior =
        mdconst::extract<ConstantInt>(DstOp->getOperand(0));
    unsigned DstBehaviorValue = DstBehavior->getZExtValue();

    auto overrideDstValue = [&]() {
      DstModFlags->setOperand(DstIndex, SrcOp);
      Flags[ID].first = SrcOp;
    };

    // If either flag has override behavior, handle it first.
    if (DstBehaviorValue == Module::Override) {
      // Diagnose inconsistent flags which both have override behavior.
      if (SrcBehaviorValue == Module::Override &&
          SrcOp->getOperand(2) != DstOp->getOperand(2))
        return stringErr("linking module flags '" + ID->getString() +
                         "': IDs have conflicting override values");
      continue;
    } else if (SrcBehaviorValue == Module::Override) {
      // Update the destination flag to that of the source.
      overrideDstValue();
      continue;
    }

    // Diagnose inconsistent merge behavior types.
    if (SrcBehaviorValue != DstBehaviorValue)
      return stringErr("linking module flags '" + ID->getString() +
                       "': IDs have conflicting behaviors");

    auto replaceDstValue = [&](MDNode *New) {
      Metadata *FlagOps[] = {DstOp->getOperand(0), ID, New};
      MDNode *Flag = MDNode::get(DstM.getContext(), FlagOps);
      DstModFlags->setOperand(DstIndex, Flag);
      Flags[ID].first = Flag;
    };

    // Perform the merge for standard behavior types.
    switch (SrcBehaviorValue) {
    case Module::Require:
    case Module::Override:
      llvm_unreachable("not possible");
    case Module::Error: {
      // Emit an error if the values differ.
      if (SrcOp->getOperand(2) != DstOp->getOperand(2))
        return stringErr("linking module flags '" + ID->getString() +
                         "': IDs have conflicting values");
      continue;
    }
    case Module::Warning: {
      // Emit a warning if the values differ.
      if (SrcOp->getOperand(2) != DstOp->getOperand(2)) {
        std::string str;
        raw_string_ostream(str)
            << "linking module flags '" << ID->getString()
            << "': IDs have conflicting values ('" << *SrcOp->getOperand(2)
            << "' from " << SrcM->getModuleIdentifier() << " with '"
            << *DstOp->getOperand(2) << "' from " << DstM.getModuleIdentifier()
            << ')';
        emitWarning(str);
      }
      continue;
    }
    case Module::Max: {
      ConstantInt *DstValue =
          mdconst::extract<ConstantInt>(DstOp->getOperand(2));
      ConstantInt *SrcValue =
          mdconst::extract<ConstantInt>(SrcOp->getOperand(2));
      if (SrcValue->getZExtValue() > DstValue->getZExtValue())
        overrideDstValue();
      break;
    }
    case Module::Append: {
      MDNode *DstValue = cast<MDNode>(DstOp->getOperand(2));
      MDNode *SrcValue = cast<MDNode>(SrcOp->getOperand(2));
      SmallVector<Metadata *, 8> MDs;
      MDs.reserve(DstValue->getNumOperands() + SrcValue->getNumOperands());
      MDs.append(DstValue->op_begin(), DstValue->op_end());
      MDs.append(SrcValue->op_begin(), SrcValue->op_end());

      replaceDstValue(MDNode::get(DstM.getContext(), MDs));
      break;
    }
    case Module::AppendUnique: {
      SmallSetVector<Metadata *, 16> Elts;
      MDNode *DstValue = cast<MDNode>(DstOp->getOperand(2));
      MDNode *SrcValue = cast<MDNode>(SrcOp->getOperand(2));
      Elts.insert(DstValue->op_begin(), DstValue->op_end());
      Elts.insert(SrcValue->op_begin(), SrcValue->op_end());

      replaceDstValue(MDNode::get(DstM.getContext(),
                                  makeArrayRef(Elts.begin(), Elts.end())));
      break;
    }
    }
  }

  // Check all of the requirements.
  for (unsigned I = 0, E = Requirements.size(); I != E; ++I) {
    MDNode *Requirement = Requirements[I];
    MDString *Flag = cast<MDString>(Requirement->getOperand(0));
    Metadata *ReqValue = Requirement->getOperand(1);

    MDNode *Op = Flags[Flag].first;
    if (!Op || Op->getOperand(2) != ReqValue)
      return stringErr("linking module flags '" + Flag->getString() +
                       "': does not have the required value");
  }
  return Error::success();
}

/// Return InlineAsm adjusted with target-specific directives if required.
/// For ARM and Thumb, we have to add directives to select the appropriate ISA
/// to support mixing module-level inline assembly from ARM and Thumb modules.
static std::string adjustInlineAsm(const std::string &InlineAsm,
                                   const Triple &Triple) {
  if (Triple.getArch() == Triple::thumb || Triple.getArch() == Triple::thumbeb)
    return ".text\n.balign 2\n.thumb\n" + InlineAsm;
  if (Triple.getArch() == Triple::arm || Triple.getArch() == Triple::armeb)
    return ".text\n.balign 4\n.arm\n" + InlineAsm;
  return InlineAsm;
}

Error IRLinker::run() {
  // Ensure metadata materialized before value mapping.
  if (SrcM->getMaterializer())
    if (Error Err = SrcM->getMaterializer()->materializeMetadata())
      return Err;

  // Inherit the target data from the source module if the destination module
  // doesn't have one already.
  if (DstM.getDataLayout().isDefault())
    DstM.setDataLayout(SrcM->getDataLayout());

  if (SrcM->getDataLayout() != DstM.getDataLayout()) {
    emitWarning("Linking two modules of different data layouts: '" +
                SrcM->getModuleIdentifier() + "' is '" +
                SrcM->getDataLayoutStr() + "' whereas '" +
                DstM.getModuleIdentifier() + "' is '" +
                DstM.getDataLayoutStr() + "'\n");
  }

  // Copy the target triple from the source to dest if the dest's is empty.
  if (DstM.getTargetTriple().empty() && !SrcM->getTargetTriple().empty())
    DstM.setTargetTriple(SrcM->getTargetTriple());

  Triple SrcTriple(SrcM->getTargetTriple()), DstTriple(DstM.getTargetTriple());

  if (!SrcM->getTargetTriple().empty()&&
      !SrcTriple.isCompatibleWith(DstTriple))
    emitWarning("Linking two modules of different target triples: " +
                SrcM->getModuleIdentifier() + "' is '" +
                SrcM->getTargetTriple() + "' whereas '" +
                DstM.getModuleIdentifier() + "' is '" + DstM.getTargetTriple() +
                "'\n");

  DstM.setTargetTriple(SrcTriple.merge(DstTriple));

  // Append the module inline asm string.
  if (!IsPerformingImport && !SrcM->getModuleInlineAsm().empty()) {
    std::string SrcModuleInlineAsm = adjustInlineAsm(SrcM->getModuleInlineAsm(),
                                                     SrcTriple);
    if (DstM.getModuleInlineAsm().empty())
      DstM.setModuleInlineAsm(SrcModuleInlineAsm);
    else
      DstM.setModuleInlineAsm(DstM.getModuleInlineAsm() + "\n" +
                              SrcModuleInlineAsm);
  }

  // Loop over all of the linked values to compute type mappings.
  computeTypeMapping();

  std::reverse(Worklist.begin(), Worklist.end());
  while (!Worklist.empty()) {
    GlobalValue *GV = Worklist.back();
    Worklist.pop_back();

    // Already mapped.
    if (ValueMap.find(GV) != ValueMap.end() ||
        AliasValueMap.find(GV) != AliasValueMap.end())
      continue;

    assert(!GV->isDeclaration());
    Mapper.mapValue(*GV);
    if (FoundError)
      return std::move(*FoundError);
  }

  // Note that we are done linking global value bodies. This prevents
  // metadata linking from creating new references.
  DoneLinkingBodies = true;
  Mapper.addFlags(RF_NullMapMissingGlobalValues);

  // Remap all of the named MDNodes in Src into the DstM module. We do this
  // after linking GlobalValues so that MDNodes that reference GlobalValues
  // are properly remapped.
  linkNamedMDNodes();

  // Merge the module flags into the DstM module.
  return linkModuleFlagsMetadata();
}

IRMover::StructTypeKeyInfo::KeyTy::KeyTy(ArrayRef<Type *> E, bool P)
    : ETypes(E), IsPacked(P) {}

IRMover::StructTypeKeyInfo::KeyTy::KeyTy(const StructType *ST)
    : ETypes(ST->elements()), IsPacked(ST->isPacked()) {}

bool IRMover::StructTypeKeyInfo::KeyTy::operator==(const KeyTy &That) const {
  return IsPacked == That.IsPacked && ETypes == That.ETypes;
}

bool IRMover::StructTypeKeyInfo::KeyTy::operator!=(const KeyTy &That) const {
  return !this->operator==(That);
}

StructType *IRMover::StructTypeKeyInfo::getEmptyKey() {
  return DenseMapInfo<StructType *>::getEmptyKey();
}

StructType *IRMover::StructTypeKeyInfo::getTombstoneKey() {
  return DenseMapInfo<StructType *>::getTombstoneKey();
}

unsigned IRMover::StructTypeKeyInfo::getHashValue(const KeyTy &Key) {
  return hash_combine(hash_combine_range(Key.ETypes.begin(), Key.ETypes.end()),
                      Key.IsPacked);
}

unsigned IRMover::StructTypeKeyInfo::getHashValue(const StructType *ST) {
  return getHashValue(KeyTy(ST));
}

bool IRMover::StructTypeKeyInfo::isEqual(const KeyTy &LHS,
                                         const StructType *RHS) {
  if (RHS == getEmptyKey() || RHS == getTombstoneKey())
    return false;
  return LHS == KeyTy(RHS);
}

bool IRMover::StructTypeKeyInfo::isEqual(const StructType *LHS,
                                         const StructType *RHS) {
  if (RHS == getEmptyKey() || RHS == getTombstoneKey())
    return LHS == RHS;
  return KeyTy(LHS) == KeyTy(RHS);
}

void IRMover::IdentifiedStructTypeSet::addNonOpaque(StructType *Ty) {
  assert(!Ty->isOpaque());
  NonOpaqueStructTypes.insert(Ty);
}

void IRMover::IdentifiedStructTypeSet::switchToNonOpaque(StructType *Ty) {
  assert(!Ty->isOpaque());
  NonOpaqueStructTypes.insert(Ty);
  bool Removed = OpaqueStructTypes.erase(Ty);
  (void)Removed;
  assert(Removed);
}

void IRMover::IdentifiedStructTypeSet::addOpaque(StructType *Ty) {
  assert(Ty->isOpaque());
  OpaqueStructTypes.insert(Ty);
}

StructType *
IRMover::IdentifiedStructTypeSet::findNonOpaque(ArrayRef<Type *> ETypes,
                                                bool IsPacked) {
  IRMover::StructTypeKeyInfo::KeyTy Key(ETypes, IsPacked);
  auto I = NonOpaqueStructTypes.find_as(Key);
  return I == NonOpaqueStructTypes.end() ? nullptr : *I;
}

bool IRMover::IdentifiedStructTypeSet::hasType(StructType *Ty) {
  if (Ty->isOpaque())
    return OpaqueStructTypes.count(Ty);
  auto I = NonOpaqueStructTypes.find(Ty);
  return I == NonOpaqueStructTypes.end() ? false : *I == Ty;
}

IRMover::IRMover(Module &M) : Composite(M) {
  TypeFinder StructTypes;
  StructTypes.run(M, /* OnlyNamed */ false);
  for (StructType *Ty : StructTypes) {
    if (Ty->isOpaque())
      IdentifiedStructTypes.addOpaque(Ty);
    else
      IdentifiedStructTypes.addNonOpaque(Ty);
  }
  // Self-map metadatas in the destination module. This is needed when
  // DebugTypeODRUniquing is enabled on the LLVMContext, since metadata in the
  // destination module may be reached from the source module.
  for (auto *MD : StructTypes.getVisitedMetadata()) {
    SharedMDs[MD].reset(const_cast<MDNode *>(MD));
  }
}

Error IRMover::move(
    std::unique_ptr<Module> Src, ArrayRef<GlobalValue *> ValuesToLink,
    std::function<void(GlobalValue &, ValueAdder Add)> AddLazyFor,
    bool IsPerformingImport) {
  IRLinker TheIRLinker(Composite, SharedMDs, IdentifiedStructTypes,
                       std::move(Src), ValuesToLink, std::move(AddLazyFor),
                       IsPerformingImport);
  Error E = TheIRLinker.run();
  Composite.dropTriviallyDeadConstantArrays();
  return E;
}
