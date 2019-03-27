//===--- DIBuilder.cpp - Debug Information Builder ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the DIBuilder.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using namespace llvm::dwarf;

cl::opt<bool>
    UseDbgAddr("use-dbg-addr",
               llvm::cl::desc("Use llvm.dbg.addr for all local variables"),
               cl::init(false), cl::Hidden);

DIBuilder::DIBuilder(Module &m, bool AllowUnresolvedNodes, DICompileUnit *CU)
  : M(m), VMContext(M.getContext()), CUNode(CU),
      DeclareFn(nullptr), ValueFn(nullptr), LabelFn(nullptr),
      AllowUnresolvedNodes(AllowUnresolvedNodes) {}

void DIBuilder::trackIfUnresolved(MDNode *N) {
  if (!N)
    return;
  if (N->isResolved())
    return;

  assert(AllowUnresolvedNodes && "Cannot handle unresolved nodes");
  UnresolvedNodes.emplace_back(N);
}

void DIBuilder::finalizeSubprogram(DISubprogram *SP) {
  MDTuple *Temp = SP->getRetainedNodes().get();
  if (!Temp || !Temp->isTemporary())
    return;

  SmallVector<Metadata *, 16> RetainedNodes;

  auto PV = PreservedVariables.find(SP);
  if (PV != PreservedVariables.end())
    RetainedNodes.append(PV->second.begin(), PV->second.end());

  auto PL = PreservedLabels.find(SP);
  if (PL != PreservedLabels.end())
    RetainedNodes.append(PL->second.begin(), PL->second.end());

  DINodeArray Node = getOrCreateArray(RetainedNodes);

  TempMDTuple(Temp)->replaceAllUsesWith(Node.get());
}

void DIBuilder::finalize() {
  if (!CUNode) {
    assert(!AllowUnresolvedNodes &&
           "creating type nodes without a CU is not supported");
    return;
  }

  CUNode->replaceEnumTypes(MDTuple::get(VMContext, AllEnumTypes));

  SmallVector<Metadata *, 16> RetainValues;
  // Declarations and definitions of the same type may be retained. Some
  // clients RAUW these pairs, leaving duplicates in the retained types
  // list. Use a set to remove the duplicates while we transform the
  // TrackingVHs back into Values.
  SmallPtrSet<Metadata *, 16> RetainSet;
  for (unsigned I = 0, E = AllRetainTypes.size(); I < E; I++)
    if (RetainSet.insert(AllRetainTypes[I]).second)
      RetainValues.push_back(AllRetainTypes[I]);

  if (!RetainValues.empty())
    CUNode->replaceRetainedTypes(MDTuple::get(VMContext, RetainValues));

  DISubprogramArray SPs = MDTuple::get(VMContext, AllSubprograms);
  for (auto *SP : SPs)
    finalizeSubprogram(SP);
  for (auto *N : RetainValues)
    if (auto *SP = dyn_cast<DISubprogram>(N))
      finalizeSubprogram(SP);

  if (!AllGVs.empty())
    CUNode->replaceGlobalVariables(MDTuple::get(VMContext, AllGVs));

  if (!AllImportedModules.empty())
    CUNode->replaceImportedEntities(MDTuple::get(
        VMContext, SmallVector<Metadata *, 16>(AllImportedModules.begin(),
                                               AllImportedModules.end())));

  for (const auto &I : AllMacrosPerParent) {
    // DIMacroNode's with nullptr parent are DICompileUnit direct children.
    if (!I.first) {
      CUNode->replaceMacros(MDTuple::get(VMContext, I.second.getArrayRef()));
      continue;
    }
    // Otherwise, it must be a temporary DIMacroFile that need to be resolved.
    auto *TMF = cast<DIMacroFile>(I.first);
    auto *MF = DIMacroFile::get(VMContext, dwarf::DW_MACINFO_start_file,
                                TMF->getLine(), TMF->getFile(),
                                getOrCreateMacroArray(I.second.getArrayRef()));
    replaceTemporary(llvm::TempDIMacroNode(TMF), MF);
  }

  // Now that all temp nodes have been replaced or deleted, resolve remaining
  // cycles.
  for (const auto &N : UnresolvedNodes)
    if (N && !N->isResolved())
      N->resolveCycles();
  UnresolvedNodes.clear();

  // Can't handle unresolved nodes anymore.
  AllowUnresolvedNodes = false;
}

/// If N is compile unit return NULL otherwise return N.
static DIScope *getNonCompileUnitScope(DIScope *N) {
  if (!N || isa<DICompileUnit>(N))
    return nullptr;
  return cast<DIScope>(N);
}

DICompileUnit *DIBuilder::createCompileUnit(
    unsigned Lang, DIFile *File, StringRef Producer, bool isOptimized,
    StringRef Flags, unsigned RunTimeVer, StringRef SplitName,
    DICompileUnit::DebugEmissionKind Kind, uint64_t DWOId,
    bool SplitDebugInlining, bool DebugInfoForProfiling,
    DICompileUnit::DebugNameTableKind NameTableKind, bool RangesBaseAddress) {

  assert(((Lang <= dwarf::DW_LANG_Fortran08 && Lang >= dwarf::DW_LANG_C89) ||
          (Lang <= dwarf::DW_LANG_hi_user && Lang >= dwarf::DW_LANG_lo_user)) &&
         "Invalid Language tag");

  assert(!CUNode && "Can only make one compile unit per DIBuilder instance");
  CUNode = DICompileUnit::getDistinct(
      VMContext, Lang, File, Producer, isOptimized, Flags, RunTimeVer,
      SplitName, Kind, nullptr, nullptr, nullptr, nullptr, nullptr, DWOId,
      SplitDebugInlining, DebugInfoForProfiling, NameTableKind,
      RangesBaseAddress);

  // Create a named metadata so that it is easier to find cu in a module.
  NamedMDNode *NMD = M.getOrInsertNamedMetadata("llvm.dbg.cu");
  NMD->addOperand(CUNode);
  trackIfUnresolved(CUNode);
  return CUNode;
}

static DIImportedEntity *
createImportedModule(LLVMContext &C, dwarf::Tag Tag, DIScope *Context,
                     Metadata *NS, DIFile *File, unsigned Line, StringRef Name,
                     SmallVectorImpl<TrackingMDNodeRef> &AllImportedModules) {
  if (Line)
    assert(File && "Source location has line number but no file");
  unsigned EntitiesCount = C.pImpl->DIImportedEntitys.size();
  auto *M =
      DIImportedEntity::get(C, Tag, Context, DINodeRef(NS), File, Line, Name);
  if (EntitiesCount < C.pImpl->DIImportedEntitys.size())
    // A new Imported Entity was just added to the context.
    // Add it to the Imported Modules list.
    AllImportedModules.emplace_back(M);
  return M;
}

DIImportedEntity *DIBuilder::createImportedModule(DIScope *Context,
                                                  DINamespace *NS, DIFile *File,
                                                  unsigned Line) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_module,
                                Context, NS, File, Line, StringRef(),
                                AllImportedModules);
}

DIImportedEntity *DIBuilder::createImportedModule(DIScope *Context,
                                                  DIImportedEntity *NS,
                                                  DIFile *File, unsigned Line) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_module,
                                Context, NS, File, Line, StringRef(),
                                AllImportedModules);
}

DIImportedEntity *DIBuilder::createImportedModule(DIScope *Context, DIModule *M,
                                                  DIFile *File, unsigned Line) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_module,
                                Context, M, File, Line, StringRef(),
                                AllImportedModules);
}

DIImportedEntity *DIBuilder::createImportedDeclaration(DIScope *Context,
                                                       DINode *Decl,
                                                       DIFile *File,
                                                       unsigned Line,
                                                       StringRef Name) {
  // Make sure to use the unique identifier based metadata reference for
  // types that have one.
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_declaration,
                                Context, Decl, File, Line, Name,
                                AllImportedModules);
}

DIFile *DIBuilder::createFile(StringRef Filename, StringRef Directory,
                              Optional<DIFile::ChecksumInfo<StringRef>> CS,
                              Optional<StringRef> Source) {
  return DIFile::get(VMContext, Filename, Directory, CS, Source);
}

DIMacro *DIBuilder::createMacro(DIMacroFile *Parent, unsigned LineNumber,
                                unsigned MacroType, StringRef Name,
                                StringRef Value) {
  assert(!Name.empty() && "Unable to create macro without name");
  assert((MacroType == dwarf::DW_MACINFO_undef ||
          MacroType == dwarf::DW_MACINFO_define) &&
         "Unexpected macro type");
  auto *M = DIMacro::get(VMContext, MacroType, LineNumber, Name, Value);
  AllMacrosPerParent[Parent].insert(M);
  return M;
}

DIMacroFile *DIBuilder::createTempMacroFile(DIMacroFile *Parent,
                                            unsigned LineNumber, DIFile *File) {
  auto *MF = DIMacroFile::getTemporary(VMContext, dwarf::DW_MACINFO_start_file,
                                       LineNumber, File, DIMacroNodeArray())
                 .release();
  AllMacrosPerParent[Parent].insert(MF);
  // Add the new temporary DIMacroFile to the macro per parent map as a parent.
  // This is needed to assure DIMacroFile with no children to have an entry in
  // the map. Otherwise, it will not be resolved in DIBuilder::finalize().
  AllMacrosPerParent.insert({MF, {}});
  return MF;
}

DIEnumerator *DIBuilder::createEnumerator(StringRef Name, int64_t Val,
                                          bool IsUnsigned) {
  assert(!Name.empty() && "Unable to create enumerator without name");
  return DIEnumerator::get(VMContext, Val, IsUnsigned, Name);
}

DIBasicType *DIBuilder::createUnspecifiedType(StringRef Name) {
  assert(!Name.empty() && "Unable to create type without name");
  return DIBasicType::get(VMContext, dwarf::DW_TAG_unspecified_type, Name);
}

DIBasicType *DIBuilder::createNullPtrType() {
  return createUnspecifiedType("decltype(nullptr)");
}

DIBasicType *DIBuilder::createBasicType(StringRef Name, uint64_t SizeInBits,
                                        unsigned Encoding,
                                        DINode::DIFlags Flags) {
  assert(!Name.empty() && "Unable to create type without name");
  return DIBasicType::get(VMContext, dwarf::DW_TAG_base_type, Name, SizeInBits,
                          0, Encoding, Flags);
}

DIDerivedType *DIBuilder::createQualifiedType(unsigned Tag, DIType *FromTy) {
  return DIDerivedType::get(VMContext, Tag, "", nullptr, 0, nullptr, FromTy, 0,
                            0, 0, None, DINode::FlagZero);
}

DIDerivedType *DIBuilder::createPointerType(
    DIType *PointeeTy,
    uint64_t SizeInBits,
    uint32_t AlignInBits,
    Optional<unsigned> DWARFAddressSpace,
    StringRef Name) {
  // FIXME: Why is there a name here?
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_pointer_type, Name,
                            nullptr, 0, nullptr, PointeeTy, SizeInBits,
                            AlignInBits, 0, DWARFAddressSpace,
                            DINode::FlagZero);
}

DIDerivedType *DIBuilder::createMemberPointerType(DIType *PointeeTy,
                                                  DIType *Base,
                                                  uint64_t SizeInBits,
                                                  uint32_t AlignInBits,
                                                  DINode::DIFlags Flags) {
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_ptr_to_member_type, "",
                            nullptr, 0, nullptr, PointeeTy, SizeInBits,
                            AlignInBits, 0, None, Flags, Base);
}

DIDerivedType *DIBuilder::createReferenceType(
    unsigned Tag, DIType *RTy,
    uint64_t SizeInBits,
    uint32_t AlignInBits,
    Optional<unsigned> DWARFAddressSpace) {
  assert(RTy && "Unable to create reference type");
  return DIDerivedType::get(VMContext, Tag, "", nullptr, 0, nullptr, RTy,
                            SizeInBits, AlignInBits, 0, DWARFAddressSpace,
                            DINode::FlagZero);
}

DIDerivedType *DIBuilder::createTypedef(DIType *Ty, StringRef Name,
                                        DIFile *File, unsigned LineNo,
                                        DIScope *Context) {
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_typedef, Name, File,
                            LineNo, getNonCompileUnitScope(Context), Ty, 0, 0,
                            0, None, DINode::FlagZero);
}

DIDerivedType *DIBuilder::createFriend(DIType *Ty, DIType *FriendTy) {
  assert(Ty && "Invalid type!");
  assert(FriendTy && "Invalid friend type!");
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_friend, "", nullptr, 0, Ty,
                            FriendTy, 0, 0, 0, None, DINode::FlagZero);
}

DIDerivedType *DIBuilder::createInheritance(DIType *Ty, DIType *BaseTy,
                                            uint64_t BaseOffset,
                                            uint32_t VBPtrOffset,
                                            DINode::DIFlags Flags) {
  assert(Ty && "Unable to create inheritance");
  Metadata *ExtraData = ConstantAsMetadata::get(
      ConstantInt::get(IntegerType::get(VMContext, 32), VBPtrOffset));
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_inheritance, "", nullptr,
                            0, Ty, BaseTy, 0, 0, BaseOffset, None,
                            Flags, ExtraData);
}

DIDerivedType *DIBuilder::createMemberType(DIScope *Scope, StringRef Name,
                                           DIFile *File, unsigned LineNumber,
                                           uint64_t SizeInBits,
                                           uint32_t AlignInBits,
                                           uint64_t OffsetInBits,
                                           DINode::DIFlags Flags, DIType *Ty) {
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_member, Name, File,
                            LineNumber, getNonCompileUnitScope(Scope), Ty,
                            SizeInBits, AlignInBits, OffsetInBits, None, Flags);
}

static ConstantAsMetadata *getConstantOrNull(Constant *C) {
  if (C)
    return ConstantAsMetadata::get(C);
  return nullptr;
}

DIDerivedType *DIBuilder::createVariantMemberType(
    DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    Constant *Discriminant, DINode::DIFlags Flags, DIType *Ty) {
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_member, Name, File,
                            LineNumber, getNonCompileUnitScope(Scope), Ty,
                            SizeInBits, AlignInBits, OffsetInBits, None, Flags,
                            getConstantOrNull(Discriminant));
}

DIDerivedType *DIBuilder::createBitFieldMemberType(
    DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint64_t OffsetInBits, uint64_t StorageOffsetInBits,
    DINode::DIFlags Flags, DIType *Ty) {
  Flags |= DINode::FlagBitField;
  return DIDerivedType::get(
      VMContext, dwarf::DW_TAG_member, Name, File, LineNumber,
      getNonCompileUnitScope(Scope), Ty, SizeInBits, /* AlignInBits */ 0,
      OffsetInBits, None, Flags,
      ConstantAsMetadata::get(ConstantInt::get(IntegerType::get(VMContext, 64),
                                               StorageOffsetInBits)));
}

DIDerivedType *
DIBuilder::createStaticMemberType(DIScope *Scope, StringRef Name, DIFile *File,
                                  unsigned LineNumber, DIType *Ty,
                                  DINode::DIFlags Flags, llvm::Constant *Val,
                                  uint32_t AlignInBits) {
  Flags |= DINode::FlagStaticMember;
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_member, Name, File,
                            LineNumber, getNonCompileUnitScope(Scope), Ty, 0,
                            AlignInBits, 0, None, Flags,
                            getConstantOrNull(Val));
}

DIDerivedType *
DIBuilder::createObjCIVar(StringRef Name, DIFile *File, unsigned LineNumber,
                          uint64_t SizeInBits, uint32_t AlignInBits,
                          uint64_t OffsetInBits, DINode::DIFlags Flags,
                          DIType *Ty, MDNode *PropertyNode) {
  return DIDerivedType::get(VMContext, dwarf::DW_TAG_member, Name, File,
                            LineNumber, getNonCompileUnitScope(File), Ty,
                            SizeInBits, AlignInBits, OffsetInBits, None, Flags,
                            PropertyNode);
}

DIObjCProperty *
DIBuilder::createObjCProperty(StringRef Name, DIFile *File, unsigned LineNumber,
                              StringRef GetterName, StringRef SetterName,
                              unsigned PropertyAttributes, DIType *Ty) {
  return DIObjCProperty::get(VMContext, Name, File, LineNumber, GetterName,
                             SetterName, PropertyAttributes, Ty);
}

DITemplateTypeParameter *
DIBuilder::createTemplateTypeParameter(DIScope *Context, StringRef Name,
                                       DIType *Ty) {
  assert((!Context || isa<DICompileUnit>(Context)) && "Expected compile unit");
  return DITemplateTypeParameter::get(VMContext, Name, Ty);
}

static DITemplateValueParameter *
createTemplateValueParameterHelper(LLVMContext &VMContext, unsigned Tag,
                                   DIScope *Context, StringRef Name, DIType *Ty,
                                   Metadata *MD) {
  assert((!Context || isa<DICompileUnit>(Context)) && "Expected compile unit");
  return DITemplateValueParameter::get(VMContext, Tag, Name, Ty, MD);
}

DITemplateValueParameter *
DIBuilder::createTemplateValueParameter(DIScope *Context, StringRef Name,
                                        DIType *Ty, Constant *Val) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_template_value_parameter, Context, Name, Ty,
      getConstantOrNull(Val));
}

DITemplateValueParameter *
DIBuilder::createTemplateTemplateParameter(DIScope *Context, StringRef Name,
                                           DIType *Ty, StringRef Val) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_GNU_template_template_param, Context, Name, Ty,
      MDString::get(VMContext, Val));
}

DITemplateValueParameter *
DIBuilder::createTemplateParameterPack(DIScope *Context, StringRef Name,
                                       DIType *Ty, DINodeArray Val) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_GNU_template_parameter_pack, Context, Name, Ty,
      Val.get());
}

DICompositeType *DIBuilder::createClassType(
    DIScope *Context, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    DINode::DIFlags Flags, DIType *DerivedFrom, DINodeArray Elements,
    DIType *VTableHolder, MDNode *TemplateParams, StringRef UniqueIdentifier) {
  assert((!Context || isa<DIScope>(Context)) &&
         "createClassType should be called with a valid Context");

  auto *R = DICompositeType::get(
      VMContext, dwarf::DW_TAG_structure_type, Name, File, LineNumber,
      getNonCompileUnitScope(Context), DerivedFrom, SizeInBits, AlignInBits,
      OffsetInBits, Flags, Elements, 0, VTableHolder,
      cast_or_null<MDTuple>(TemplateParams), UniqueIdentifier);
  trackIfUnresolved(R);
  return R;
}

DICompositeType *DIBuilder::createStructType(
    DIScope *Context, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, DINode::DIFlags Flags,
    DIType *DerivedFrom, DINodeArray Elements, unsigned RunTimeLang,
    DIType *VTableHolder, StringRef UniqueIdentifier) {
  auto *R = DICompositeType::get(
      VMContext, dwarf::DW_TAG_structure_type, Name, File, LineNumber,
      getNonCompileUnitScope(Context), DerivedFrom, SizeInBits, AlignInBits, 0,
      Flags, Elements, RunTimeLang, VTableHolder, nullptr, UniqueIdentifier);
  trackIfUnresolved(R);
  return R;
}

DICompositeType *DIBuilder::createUnionType(
    DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, DINode::DIFlags Flags,
    DINodeArray Elements, unsigned RunTimeLang, StringRef UniqueIdentifier) {
  auto *R = DICompositeType::get(
      VMContext, dwarf::DW_TAG_union_type, Name, File, LineNumber,
      getNonCompileUnitScope(Scope), nullptr, SizeInBits, AlignInBits, 0, Flags,
      Elements, RunTimeLang, nullptr, nullptr, UniqueIdentifier);
  trackIfUnresolved(R);
  return R;
}

DICompositeType *DIBuilder::createVariantPart(
    DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, DINode::DIFlags Flags,
    DIDerivedType *Discriminator, DINodeArray Elements, StringRef UniqueIdentifier) {
  auto *R = DICompositeType::get(
      VMContext, dwarf::DW_TAG_variant_part, Name, File, LineNumber,
      getNonCompileUnitScope(Scope), nullptr, SizeInBits, AlignInBits, 0, Flags,
      Elements, 0, nullptr, nullptr, UniqueIdentifier, Discriminator);
  trackIfUnresolved(R);
  return R;
}

DISubroutineType *DIBuilder::createSubroutineType(DITypeRefArray ParameterTypes,
                                                  DINode::DIFlags Flags,
                                                  unsigned CC) {
  return DISubroutineType::get(VMContext, Flags, CC, ParameterTypes);
}

DICompositeType *DIBuilder::createEnumerationType(
    DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, DINodeArray Elements,
    DIType *UnderlyingType, StringRef UniqueIdentifier, bool IsScoped) {
  auto *CTy = DICompositeType::get(
      VMContext, dwarf::DW_TAG_enumeration_type, Name, File, LineNumber,
      getNonCompileUnitScope(Scope), UnderlyingType, SizeInBits, AlignInBits, 0,
      IsScoped ? DINode::FlagEnumClass : DINode::FlagZero, Elements, 0, nullptr,
      nullptr, UniqueIdentifier);
  AllEnumTypes.push_back(CTy);
  trackIfUnresolved(CTy);
  return CTy;
}

DICompositeType *DIBuilder::createArrayType(uint64_t Size,
                                            uint32_t AlignInBits, DIType *Ty,
                                            DINodeArray Subscripts) {
  auto *R = DICompositeType::get(VMContext, dwarf::DW_TAG_array_type, "",
                                 nullptr, 0, nullptr, Ty, Size, AlignInBits, 0,
                                 DINode::FlagZero, Subscripts, 0, nullptr);
  trackIfUnresolved(R);
  return R;
}

DICompositeType *DIBuilder::createVectorType(uint64_t Size,
                                             uint32_t AlignInBits, DIType *Ty,
                                             DINodeArray Subscripts) {
  auto *R = DICompositeType::get(VMContext, dwarf::DW_TAG_array_type, "",
                                 nullptr, 0, nullptr, Ty, Size, AlignInBits, 0,
                                 DINode::FlagVector, Subscripts, 0, nullptr);
  trackIfUnresolved(R);
  return R;
}

DISubprogram *DIBuilder::createArtificialSubprogram(DISubprogram *SP) {
  auto NewSP = SP->cloneWithFlags(SP->getFlags() | DINode::FlagArtificial);
  return MDNode::replaceWithDistinct(std::move(NewSP));
}

static DIType *createTypeWithFlags(const DIType *Ty,
                                   DINode::DIFlags FlagsToSet) {
  auto NewTy = Ty->cloneWithFlags(Ty->getFlags() | FlagsToSet);
  return MDNode::replaceWithUniqued(std::move(NewTy));
}

DIType *DIBuilder::createArtificialType(DIType *Ty) {
  // FIXME: Restrict this to the nodes where it's valid.
  if (Ty->isArtificial())
    return Ty;
  return createTypeWithFlags(Ty, DINode::FlagArtificial);
}

DIType *DIBuilder::createObjectPointerType(DIType *Ty) {
  // FIXME: Restrict this to the nodes where it's valid.
  if (Ty->isObjectPointer())
    return Ty;
  DINode::DIFlags Flags = DINode::FlagObjectPointer | DINode::FlagArtificial;
  return createTypeWithFlags(Ty, Flags);
}

void DIBuilder::retainType(DIScope *T) {
  assert(T && "Expected non-null type");
  assert((isa<DIType>(T) || (isa<DISubprogram>(T) &&
                             cast<DISubprogram>(T)->isDefinition() == false)) &&
         "Expected type or subprogram declaration");
  AllRetainTypes.emplace_back(T);
}

DIBasicType *DIBuilder::createUnspecifiedParameter() { return nullptr; }

DICompositeType *
DIBuilder::createForwardDecl(unsigned Tag, StringRef Name, DIScope *Scope,
                             DIFile *F, unsigned Line, unsigned RuntimeLang,
                             uint64_t SizeInBits, uint32_t AlignInBits,
                             StringRef UniqueIdentifier) {
  // FIXME: Define in terms of createReplaceableForwardDecl() by calling
  // replaceWithUniqued().
  auto *RetTy = DICompositeType::get(
      VMContext, Tag, Name, F, Line, getNonCompileUnitScope(Scope), nullptr,
      SizeInBits, AlignInBits, 0, DINode::FlagFwdDecl, nullptr, RuntimeLang,
      nullptr, nullptr, UniqueIdentifier);
  trackIfUnresolved(RetTy);
  return RetTy;
}

DICompositeType *DIBuilder::createReplaceableCompositeType(
    unsigned Tag, StringRef Name, DIScope *Scope, DIFile *F, unsigned Line,
    unsigned RuntimeLang, uint64_t SizeInBits, uint32_t AlignInBits,
    DINode::DIFlags Flags, StringRef UniqueIdentifier) {
  auto *RetTy =
      DICompositeType::getTemporary(
          VMContext, Tag, Name, F, Line, getNonCompileUnitScope(Scope), nullptr,
          SizeInBits, AlignInBits, 0, Flags, nullptr, RuntimeLang, nullptr,
          nullptr, UniqueIdentifier)
          .release();
  trackIfUnresolved(RetTy);
  return RetTy;
}

DINodeArray DIBuilder::getOrCreateArray(ArrayRef<Metadata *> Elements) {
  return MDTuple::get(VMContext, Elements);
}

DIMacroNodeArray
DIBuilder::getOrCreateMacroArray(ArrayRef<Metadata *> Elements) {
  return MDTuple::get(VMContext, Elements);
}

DITypeRefArray DIBuilder::getOrCreateTypeArray(ArrayRef<Metadata *> Elements) {
  SmallVector<llvm::Metadata *, 16> Elts;
  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    if (Elements[i] && isa<MDNode>(Elements[i]))
      Elts.push_back(cast<DIType>(Elements[i]));
    else
      Elts.push_back(Elements[i]);
  }
  return DITypeRefArray(MDNode::get(VMContext, Elts));
}

DISubrange *DIBuilder::getOrCreateSubrange(int64_t Lo, int64_t Count) {
  return DISubrange::get(VMContext, Count, Lo);
}

DISubrange *DIBuilder::getOrCreateSubrange(int64_t Lo, Metadata *CountNode) {
  return DISubrange::get(VMContext, CountNode, Lo);
}

static void checkGlobalVariableScope(DIScope *Context) {
#ifndef NDEBUG
  if (auto *CT =
          dyn_cast_or_null<DICompositeType>(getNonCompileUnitScope(Context)))
    assert(CT->getIdentifier().empty() &&
           "Context of a global variable should not be a type with identifier");
#endif
}

DIGlobalVariableExpression *DIBuilder::createGlobalVariableExpression(
    DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *F,
    unsigned LineNumber, DIType *Ty, bool isLocalToUnit, DIExpression *Expr,
    MDNode *Decl, MDTuple *templateParams, uint32_t AlignInBits) {
  checkGlobalVariableScope(Context);

  auto *GV = DIGlobalVariable::getDistinct(
      VMContext, cast_or_null<DIScope>(Context), Name, LinkageName, F,
      LineNumber, Ty, isLocalToUnit, true, cast_or_null<DIDerivedType>(Decl),
      templateParams, AlignInBits);
  if (!Expr)
    Expr = createExpression();
  auto *N = DIGlobalVariableExpression::get(VMContext, GV, Expr);
  AllGVs.push_back(N);
  return N;
}

DIGlobalVariable *DIBuilder::createTempGlobalVariableFwdDecl(
    DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *F,
    unsigned LineNumber, DIType *Ty, bool isLocalToUnit, MDNode *Decl,
    MDTuple *templateParams, uint32_t AlignInBits) {
  checkGlobalVariableScope(Context);

  return DIGlobalVariable::getTemporary(
             VMContext, cast_or_null<DIScope>(Context), Name, LinkageName, F,
             LineNumber, Ty, isLocalToUnit, false,
             cast_or_null<DIDerivedType>(Decl), templateParams, AlignInBits)
      .release();
}

static DILocalVariable *createLocalVariable(
    LLVMContext &VMContext,
    DenseMap<MDNode *, SmallVector<TrackingMDNodeRef, 1>> &PreservedVariables,
    DIScope *Scope, StringRef Name, unsigned ArgNo, DIFile *File,
    unsigned LineNo, DIType *Ty, bool AlwaysPreserve, DINode::DIFlags Flags,
    uint32_t AlignInBits) {
  // FIXME: Why getNonCompileUnitScope()?
  // FIXME: Why is "!Context" okay here?
  // FIXME: Why doesn't this check for a subprogram or lexical block (AFAICT
  // the only valid scopes)?
  DIScope *Context = getNonCompileUnitScope(Scope);

  auto *Node =
      DILocalVariable::get(VMContext, cast_or_null<DILocalScope>(Context), Name,
                           File, LineNo, Ty, ArgNo, Flags, AlignInBits);
  if (AlwaysPreserve) {
    // The optimizer may remove local variables. If there is an interest
    // to preserve variable info in such situation then stash it in a
    // named mdnode.
    DISubprogram *Fn = getDISubprogram(Scope);
    assert(Fn && "Missing subprogram for local variable");
    PreservedVariables[Fn].emplace_back(Node);
  }
  return Node;
}

DILocalVariable *DIBuilder::createAutoVariable(DIScope *Scope, StringRef Name,
                                               DIFile *File, unsigned LineNo,
                                               DIType *Ty, bool AlwaysPreserve,
                                               DINode::DIFlags Flags,
                                               uint32_t AlignInBits) {
  return createLocalVariable(VMContext, PreservedVariables, Scope, Name,
                             /* ArgNo */ 0, File, LineNo, Ty, AlwaysPreserve,
                             Flags, AlignInBits);
}

DILocalVariable *DIBuilder::createParameterVariable(
    DIScope *Scope, StringRef Name, unsigned ArgNo, DIFile *File,
    unsigned LineNo, DIType *Ty, bool AlwaysPreserve, DINode::DIFlags Flags) {
  assert(ArgNo && "Expected non-zero argument number for parameter");
  return createLocalVariable(VMContext, PreservedVariables, Scope, Name, ArgNo,
                             File, LineNo, Ty, AlwaysPreserve, Flags,
                             /* AlignInBits */0);
}

DILabel *DIBuilder::createLabel(
    DIScope *Scope, StringRef Name, DIFile *File,
    unsigned LineNo, bool AlwaysPreserve) {
  DIScope *Context = getNonCompileUnitScope(Scope);

  auto *Node =
      DILabel::get(VMContext, cast_or_null<DILocalScope>(Context), Name,
                   File, LineNo);

  if (AlwaysPreserve) {
    /// The optimizer may remove labels. If there is an interest
    /// to preserve label info in such situation then append it to
    /// the list of retained nodes of the DISubprogram.
    DISubprogram *Fn = getDISubprogram(Scope);
    assert(Fn && "Missing subprogram for label");
    PreservedLabels[Fn].emplace_back(Node);
  }
  return Node;
}

DIExpression *DIBuilder::createExpression(ArrayRef<uint64_t> Addr) {
  return DIExpression::get(VMContext, Addr);
}

DIExpression *DIBuilder::createExpression(ArrayRef<int64_t> Signed) {
  // TODO: Remove the callers of this signed version and delete.
  SmallVector<uint64_t, 8> Addr(Signed.begin(), Signed.end());
  return createExpression(Addr);
}

template <class... Ts>
static DISubprogram *getSubprogram(bool IsDistinct, Ts &&... Args) {
  if (IsDistinct)
    return DISubprogram::getDistinct(std::forward<Ts>(Args)...);
  return DISubprogram::get(std::forward<Ts>(Args)...);
}

DISubprogram *DIBuilder::createFunction(
    DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *File,
    unsigned LineNo, DISubroutineType *Ty, unsigned ScopeLine,
    DINode::DIFlags Flags, DISubprogram::DISPFlags SPFlags,
    DITemplateParameterArray TParams, DISubprogram *Decl,
    DITypeArray ThrownTypes) {
  bool IsDefinition = SPFlags & DISubprogram::SPFlagDefinition;
  auto *Node = getSubprogram(
      /*IsDistinct=*/IsDefinition, VMContext, getNonCompileUnitScope(Context),
      Name, LinkageName, File, LineNo, Ty, ScopeLine, nullptr, 0, 0, Flags,
      SPFlags, IsDefinition ? CUNode : nullptr, TParams, Decl,
      MDTuple::getTemporary(VMContext, None).release(), ThrownTypes);

  if (IsDefinition)
    AllSubprograms.push_back(Node);
  trackIfUnresolved(Node);
  return Node;
}

DISubprogram *DIBuilder::createTempFunctionFwdDecl(
    DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *File,
    unsigned LineNo, DISubroutineType *Ty, unsigned ScopeLine,
    DINode::DIFlags Flags, DISubprogram::DISPFlags SPFlags,
    DITemplateParameterArray TParams, DISubprogram *Decl,
    DITypeArray ThrownTypes) {
  bool IsDefinition = SPFlags & DISubprogram::SPFlagDefinition;
  return DISubprogram::getTemporary(VMContext, getNonCompileUnitScope(Context),
                                    Name, LinkageName, File, LineNo, Ty,
                                    ScopeLine, nullptr, 0, 0, Flags, SPFlags,
                                    IsDefinition ? CUNode : nullptr, TParams,
                                    Decl, nullptr, ThrownTypes)
      .release();
}

DISubprogram *DIBuilder::createMethod(
    DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *F,
    unsigned LineNo, DISubroutineType *Ty, unsigned VIndex, int ThisAdjustment,
    DIType *VTableHolder, DINode::DIFlags Flags,
    DISubprogram::DISPFlags SPFlags, DITemplateParameterArray TParams,
    DITypeArray ThrownTypes) {
  assert(getNonCompileUnitScope(Context) &&
         "Methods should have both a Context and a context that isn't "
         "the compile unit.");
  // FIXME: Do we want to use different scope/lines?
  bool IsDefinition = SPFlags & DISubprogram::SPFlagDefinition;
  auto *SP = getSubprogram(
      /*IsDistinct=*/IsDefinition, VMContext, cast<DIScope>(Context), Name,
      LinkageName, F, LineNo, Ty, LineNo, VTableHolder, VIndex, ThisAdjustment,
      Flags, SPFlags, IsDefinition ? CUNode : nullptr, TParams, nullptr,
      nullptr, ThrownTypes);

  if (IsDefinition)
    AllSubprograms.push_back(SP);
  trackIfUnresolved(SP);
  return SP;
}

DINamespace *DIBuilder::createNameSpace(DIScope *Scope, StringRef Name,
                                        bool ExportSymbols) {

  // It is okay to *not* make anonymous top-level namespaces distinct, because
  // all nodes that have an anonymous namespace as their parent scope are
  // guaranteed to be unique and/or are linked to their containing
  // DICompileUnit. This decision is an explicit tradeoff of link time versus
  // memory usage versus code simplicity and may get revisited in the future.
  return DINamespace::get(VMContext, getNonCompileUnitScope(Scope), Name,
                          ExportSymbols);
}

DIModule *DIBuilder::createModule(DIScope *Scope, StringRef Name,
                                  StringRef ConfigurationMacros,
                                  StringRef IncludePath,
                                  StringRef ISysRoot) {
 return DIModule::get(VMContext, getNonCompileUnitScope(Scope), Name,
                      ConfigurationMacros, IncludePath, ISysRoot);
}

DILexicalBlockFile *DIBuilder::createLexicalBlockFile(DIScope *Scope,
                                                      DIFile *File,
                                                      unsigned Discriminator) {
  return DILexicalBlockFile::get(VMContext, Scope, File, Discriminator);
}

DILexicalBlock *DIBuilder::createLexicalBlock(DIScope *Scope, DIFile *File,
                                              unsigned Line, unsigned Col) {
  // Make these distinct, to avoid merging two lexical blocks on the same
  // file/line/column.
  return DILexicalBlock::getDistinct(VMContext, getNonCompileUnitScope(Scope),
                                     File, Line, Col);
}

Instruction *DIBuilder::insertDeclare(Value *Storage, DILocalVariable *VarInfo,
                                      DIExpression *Expr, const DILocation *DL,
                                      Instruction *InsertBefore) {
  return insertDeclare(Storage, VarInfo, Expr, DL, InsertBefore->getParent(),
                       InsertBefore);
}

Instruction *DIBuilder::insertDeclare(Value *Storage, DILocalVariable *VarInfo,
                                      DIExpression *Expr, const DILocation *DL,
                                      BasicBlock *InsertAtEnd) {
  // If this block already has a terminator then insert this intrinsic before
  // the terminator. Otherwise, put it at the end of the block.
  Instruction *InsertBefore = InsertAtEnd->getTerminator();
  return insertDeclare(Storage, VarInfo, Expr, DL, InsertAtEnd, InsertBefore);
}

Instruction *DIBuilder::insertLabel(DILabel *LabelInfo, const DILocation *DL,
                                    Instruction *InsertBefore) {
  return insertLabel(
      LabelInfo, DL, InsertBefore ? InsertBefore->getParent() : nullptr,
      InsertBefore);
}

Instruction *DIBuilder::insertLabel(DILabel *LabelInfo, const DILocation *DL,
                                    BasicBlock *InsertAtEnd) {
  return insertLabel(LabelInfo, DL, InsertAtEnd, nullptr);
}

Instruction *DIBuilder::insertDbgValueIntrinsic(Value *V,
                                                DILocalVariable *VarInfo,
                                                DIExpression *Expr,
                                                const DILocation *DL,
                                                Instruction *InsertBefore) {
  return insertDbgValueIntrinsic(
      V, VarInfo, Expr, DL, InsertBefore ? InsertBefore->getParent() : nullptr,
      InsertBefore);
}

Instruction *DIBuilder::insertDbgValueIntrinsic(Value *V,
                                                DILocalVariable *VarInfo,
                                                DIExpression *Expr,
                                                const DILocation *DL,
                                                BasicBlock *InsertAtEnd) {
  return insertDbgValueIntrinsic(V, VarInfo, Expr, DL, InsertAtEnd, nullptr);
}

/// Return an IRBuilder for inserting dbg.declare and dbg.value intrinsics. This
/// abstracts over the various ways to specify an insert position.
static IRBuilder<> getIRBForDbgInsertion(const DILocation *DL,
                                         BasicBlock *InsertBB,
                                         Instruction *InsertBefore) {
  IRBuilder<> B(DL->getContext());
  if (InsertBefore)
    B.SetInsertPoint(InsertBefore);
  else if (InsertBB)
    B.SetInsertPoint(InsertBB);
  B.SetCurrentDebugLocation(DL);
  return B;
}

static Value *getDbgIntrinsicValueImpl(LLVMContext &VMContext, Value *V) {
  assert(V && "no value passed to dbg intrinsic");
  return MetadataAsValue::get(VMContext, ValueAsMetadata::get(V));
}

static Function *getDeclareIntrin(Module &M) {
  return Intrinsic::getDeclaration(&M, UseDbgAddr ? Intrinsic::dbg_addr
                                                  : Intrinsic::dbg_declare);
}

Instruction *DIBuilder::insertDeclare(Value *Storage, DILocalVariable *VarInfo,
                                      DIExpression *Expr, const DILocation *DL,
                                      BasicBlock *InsertBB, Instruction *InsertBefore) {
  assert(VarInfo && "empty or invalid DILocalVariable* passed to dbg.declare");
  assert(DL && "Expected debug loc");
  assert(DL->getScope()->getSubprogram() ==
             VarInfo->getScope()->getSubprogram() &&
         "Expected matching subprograms");
  if (!DeclareFn)
    DeclareFn = getDeclareIntrin(M);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, Storage),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};

  IRBuilder<> B = getIRBForDbgInsertion(DL, InsertBB, InsertBefore);
  return B.CreateCall(DeclareFn, Args);
}

Instruction *DIBuilder::insertDbgValueIntrinsic(
    Value *V, DILocalVariable *VarInfo, DIExpression *Expr,
    const DILocation *DL, BasicBlock *InsertBB, Instruction *InsertBefore) {
  assert(V && "no value passed to dbg.value");
  assert(VarInfo && "empty or invalid DILocalVariable* passed to dbg.value");
  assert(DL && "Expected debug loc");
  assert(DL->getScope()->getSubprogram() ==
             VarInfo->getScope()->getSubprogram() &&
         "Expected matching subprograms");
  if (!ValueFn)
    ValueFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_value);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, V),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};

  IRBuilder<> B = getIRBForDbgInsertion(DL, InsertBB, InsertBefore);
  return B.CreateCall(ValueFn, Args);
}

Instruction *DIBuilder::insertLabel(
    DILabel *LabelInfo, const DILocation *DL,
    BasicBlock *InsertBB, Instruction *InsertBefore) {
  assert(LabelInfo && "empty or invalid DILabel* passed to dbg.label");
  assert(DL && "Expected debug loc");
  assert(DL->getScope()->getSubprogram() ==
             LabelInfo->getScope()->getSubprogram() &&
         "Expected matching subprograms");
  if (!LabelFn)
    LabelFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_label);

  trackIfUnresolved(LabelInfo);
  Value *Args[] = {MetadataAsValue::get(VMContext, LabelInfo)};

  IRBuilder<> B = getIRBForDbgInsertion(DL, InsertBB, InsertBefore);
  return B.CreateCall(LabelFn, Args);
}

void DIBuilder::replaceVTableHolder(DICompositeType *&T,
                                    DIType *VTableHolder) {
  {
    TypedTrackingMDRef<DICompositeType> N(T);
    N->replaceVTableHolder(VTableHolder);
    T = N.get();
  }

  // If this didn't create a self-reference, just return.
  if (T != VTableHolder)
    return;

  // Look for unresolved operands.  T will drop RAUW support, orphaning any
  // cycles underneath it.
  if (T->isResolved())
    for (const MDOperand &O : T->operands())
      if (auto *N = dyn_cast_or_null<MDNode>(O))
        trackIfUnresolved(N);
}

void DIBuilder::replaceArrays(DICompositeType *&T, DINodeArray Elements,
                              DINodeArray TParams) {
  {
    TypedTrackingMDRef<DICompositeType> N(T);
    if (Elements)
      N->replaceElements(Elements);
    if (TParams)
      N->replaceTemplateParams(DITemplateParameterArray(TParams));
    T = N.get();
  }

  // If T isn't resolved, there's no problem.
  if (!T->isResolved())
    return;

  // If T is resolved, it may be due to a self-reference cycle.  Track the
  // arrays explicitly if they're unresolved, or else the cycles will be
  // orphaned.
  if (Elements)
    trackIfUnresolved(Elements.get());
  if (TParams)
    trackIfUnresolved(TParams.get());
}
