//===- DebugInfoMetadata.cpp - Implement debug info metadata --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the debug info Metadata classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DebugInfoMetadata.h"
#include "LLVMContextImpl.h"
#include "MetadataImpl.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <numeric>

using namespace llvm;

DILocation::DILocation(LLVMContext &C, StorageType Storage, unsigned Line,
                       unsigned Column, ArrayRef<Metadata *> MDs,
                       bool ImplicitCode)
    : MDNode(C, DILocationKind, Storage, MDs) {
  assert((MDs.size() == 1 || MDs.size() == 2) &&
         "Expected a scope and optional inlined-at");

  // Set line and column.
  assert(Column < (1u << 16) && "Expected 16-bit column");

  SubclassData32 = Line;
  SubclassData16 = Column;

  setImplicitCode(ImplicitCode);
}

static void adjustColumn(unsigned &Column) {
  // Set to unknown on overflow.  We only have 16 bits to play with here.
  if (Column >= (1u << 16))
    Column = 0;
}

DILocation *DILocation::getImpl(LLVMContext &Context, unsigned Line,
                                unsigned Column, Metadata *Scope,
                                Metadata *InlinedAt, bool ImplicitCode,
                                StorageType Storage, bool ShouldCreate) {
  // Fixup column.
  adjustColumn(Column);

  if (Storage == Uniqued) {
    if (auto *N = getUniqued(Context.pImpl->DILocations,
                             DILocationInfo::KeyTy(Line, Column, Scope,
                                                   InlinedAt, ImplicitCode)))
      return N;
    if (!ShouldCreate)
      return nullptr;
  } else {
    assert(ShouldCreate && "Expected non-uniqued nodes to always be created");
  }

  SmallVector<Metadata *, 2> Ops;
  Ops.push_back(Scope);
  if (InlinedAt)
    Ops.push_back(InlinedAt);
  return storeImpl(new (Ops.size()) DILocation(Context, Storage, Line, Column,
                                               Ops, ImplicitCode),
                   Storage, Context.pImpl->DILocations);
}

const DILocation *DILocation::getMergedLocation(const DILocation *LocA,
                                                const DILocation *LocB) {
  if (!LocA || !LocB)
    return nullptr;

  if (LocA == LocB)
    return LocA;

  SmallPtrSet<DILocation *, 5> InlinedLocationsA;
  for (DILocation *L = LocA->getInlinedAt(); L; L = L->getInlinedAt())
    InlinedLocationsA.insert(L);
  SmallSet<std::pair<DIScope *, DILocation *>, 5> Locations;
  DIScope *S = LocA->getScope();
  DILocation *L = LocA->getInlinedAt();
  while (S) {
    Locations.insert(std::make_pair(S, L));
    S = S->getScope().resolve();
    if (!S && L) {
      S = L->getScope();
      L = L->getInlinedAt();
    }
  }
  const DILocation *Result = LocB;
  S = LocB->getScope();
  L = LocB->getInlinedAt();
  while (S) {
    if (Locations.count(std::make_pair(S, L)))
      break;
    S = S->getScope().resolve();
    if (!S && L) {
      S = L->getScope();
      L = L->getInlinedAt();
    }
  }

  // If the two locations are irreconsilable, just pick one. This is misleading,
  // but on the other hand, it's a "line 0" location.
  if (!S || !isa<DILocalScope>(S))
    S = LocA->getScope();
  return DILocation::get(Result->getContext(), 0, 0, S, L);
}

Optional<unsigned> DILocation::encodeDiscriminator(unsigned BD, unsigned DF, unsigned CI) {
  SmallVector<unsigned, 3> Components = {BD, DF, CI};
  uint64_t RemainingWork = 0U;
  // We use RemainingWork to figure out if we have no remaining components to
  // encode. For example: if BD != 0 but DF == 0 && CI == 0, we don't need to
  // encode anything for the latter 2.
  // Since any of the input components is at most 32 bits, their sum will be
  // less than 34 bits, and thus RemainingWork won't overflow.
  RemainingWork = std::accumulate(Components.begin(), Components.end(), RemainingWork);

  int I = 0;
  unsigned Ret = 0;
  unsigned NextBitInsertionIndex = 0;
  while (RemainingWork > 0) {
    unsigned C = Components[I++];
    RemainingWork -= C;
    unsigned EC = encodeComponent(C);
    Ret |= (EC << NextBitInsertionIndex);
    NextBitInsertionIndex += encodingBits(C);
  }

  // Encoding may be unsuccessful because of overflow. We determine success by
  // checking equivalence of components before & after encoding. Alternatively,
  // we could determine Success during encoding, but the current alternative is
  // simpler.
  unsigned TBD, TDF, TCI = 0;
  decodeDiscriminator(Ret, TBD, TDF, TCI);
  if (TBD == BD && TDF == DF && TCI == CI)
    return Ret;
  return None;
}

void DILocation::decodeDiscriminator(unsigned D, unsigned &BD, unsigned &DF,
                                     unsigned &CI) {
  BD = getUnsignedFromPrefixEncoding(D);
  DF = getUnsignedFromPrefixEncoding(getNextComponentInDiscriminator(D));
  CI = getUnsignedFromPrefixEncoding(
      getNextComponentInDiscriminator(getNextComponentInDiscriminator(D)));
}


DINode::DIFlags DINode::getFlag(StringRef Flag) {
  return StringSwitch<DIFlags>(Flag)
#define HANDLE_DI_FLAG(ID, NAME) .Case("DIFlag" #NAME, Flag##NAME)
#include "llvm/IR/DebugInfoFlags.def"
      .Default(DINode::FlagZero);
}

StringRef DINode::getFlagString(DIFlags Flag) {
  switch (Flag) {
#define HANDLE_DI_FLAG(ID, NAME)                                               \
  case Flag##NAME:                                                             \
    return "DIFlag" #NAME;
#include "llvm/IR/DebugInfoFlags.def"
  }
  return "";
}

DINode::DIFlags DINode::splitFlags(DIFlags Flags,
                                   SmallVectorImpl<DIFlags> &SplitFlags) {
  // Flags that are packed together need to be specially handled, so
  // that, for example, we emit "DIFlagPublic" and not
  // "DIFlagPrivate | DIFlagProtected".
  if (DIFlags A = Flags & FlagAccessibility) {
    if (A == FlagPrivate)
      SplitFlags.push_back(FlagPrivate);
    else if (A == FlagProtected)
      SplitFlags.push_back(FlagProtected);
    else
      SplitFlags.push_back(FlagPublic);
    Flags &= ~A;
  }
  if (DIFlags R = Flags & FlagPtrToMemberRep) {
    if (R == FlagSingleInheritance)
      SplitFlags.push_back(FlagSingleInheritance);
    else if (R == FlagMultipleInheritance)
      SplitFlags.push_back(FlagMultipleInheritance);
    else
      SplitFlags.push_back(FlagVirtualInheritance);
    Flags &= ~R;
  }
  if ((Flags & FlagIndirectVirtualBase) == FlagIndirectVirtualBase) {
    Flags &= ~FlagIndirectVirtualBase;
    SplitFlags.push_back(FlagIndirectVirtualBase);
  }

#define HANDLE_DI_FLAG(ID, NAME)                                               \
  if (DIFlags Bit = Flags & Flag##NAME) {                                      \
    SplitFlags.push_back(Bit);                                                 \
    Flags &= ~Bit;                                                             \
  }
#include "llvm/IR/DebugInfoFlags.def"
  return Flags;
}

DIScopeRef DIScope::getScope() const {
  if (auto *T = dyn_cast<DIType>(this))
    return T->getScope();

  if (auto *SP = dyn_cast<DISubprogram>(this))
    return SP->getScope();

  if (auto *LB = dyn_cast<DILexicalBlockBase>(this))
    return LB->getScope();

  if (auto *NS = dyn_cast<DINamespace>(this))
    return NS->getScope();

  if (auto *M = dyn_cast<DIModule>(this))
    return M->getScope();

  assert((isa<DIFile>(this) || isa<DICompileUnit>(this)) &&
         "Unhandled type of scope.");
  return nullptr;
}

StringRef DIScope::getName() const {
  if (auto *T = dyn_cast<DIType>(this))
    return T->getName();
  if (auto *SP = dyn_cast<DISubprogram>(this))
    return SP->getName();
  if (auto *NS = dyn_cast<DINamespace>(this))
    return NS->getName();
  if (auto *M = dyn_cast<DIModule>(this))
    return M->getName();
  assert((isa<DILexicalBlockBase>(this) || isa<DIFile>(this) ||
          isa<DICompileUnit>(this)) &&
         "Unhandled type of scope.");
  return "";
}

#ifndef NDEBUG
static bool isCanonical(const MDString *S) {
  return !S || !S->getString().empty();
}
#endif

GenericDINode *GenericDINode::getImpl(LLVMContext &Context, unsigned Tag,
                                      MDString *Header,
                                      ArrayRef<Metadata *> DwarfOps,
                                      StorageType Storage, bool ShouldCreate) {
  unsigned Hash = 0;
  if (Storage == Uniqued) {
    GenericDINodeInfo::KeyTy Key(Tag, Header, DwarfOps);
    if (auto *N = getUniqued(Context.pImpl->GenericDINodes, Key))
      return N;
    if (!ShouldCreate)
      return nullptr;
    Hash = Key.getHash();
  } else {
    assert(ShouldCreate && "Expected non-uniqued nodes to always be created");
  }

  // Use a nullptr for empty headers.
  assert(isCanonical(Header) && "Expected canonical MDString");
  Metadata *PreOps[] = {Header};
  return storeImpl(new (DwarfOps.size() + 1) GenericDINode(
                       Context, Storage, Hash, Tag, PreOps, DwarfOps),
                   Storage, Context.pImpl->GenericDINodes);
}

void GenericDINode::recalculateHash() {
  setHash(GenericDINodeInfo::KeyTy::calculateHash(this));
}

#define UNWRAP_ARGS_IMPL(...) __VA_ARGS__
#define UNWRAP_ARGS(ARGS) UNWRAP_ARGS_IMPL ARGS
#define DEFINE_GETIMPL_LOOKUP(CLASS, ARGS)                                     \
  do {                                                                         \
    if (Storage == Uniqued) {                                                  \
      if (auto *N = getUniqued(Context.pImpl->CLASS##s,                        \
                               CLASS##Info::KeyTy(UNWRAP_ARGS(ARGS))))         \
        return N;                                                              \
      if (!ShouldCreate)                                                       \
        return nullptr;                                                        \
    } else {                                                                   \
      assert(ShouldCreate &&                                                   \
             "Expected non-uniqued nodes to always be created");               \
    }                                                                          \
  } while (false)
#define DEFINE_GETIMPL_STORE(CLASS, ARGS, OPS)                                 \
  return storeImpl(new (array_lengthof(OPS))                                   \
                       CLASS(Context, Storage, UNWRAP_ARGS(ARGS), OPS),        \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_NO_OPS(CLASS, ARGS)                               \
  return storeImpl(new (0u) CLASS(Context, Storage, UNWRAP_ARGS(ARGS)),        \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(CLASS, OPS)                   \
  return storeImpl(new (array_lengthof(OPS)) CLASS(Context, Storage, OPS),     \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_N(CLASS, ARGS, OPS, NUM_OPS)                      \
  return storeImpl(new (NUM_OPS)                                               \
                       CLASS(Context, Storage, UNWRAP_ARGS(ARGS), OPS),        \
                   Storage, Context.pImpl->CLASS##s)

DISubrange *DISubrange::getImpl(LLVMContext &Context, int64_t Count, int64_t Lo,
                                StorageType Storage, bool ShouldCreate) {
  auto *CountNode = ConstantAsMetadata::get(
      ConstantInt::getSigned(Type::getInt64Ty(Context), Count));
  return getImpl(Context, CountNode, Lo, Storage, ShouldCreate);
}

DISubrange *DISubrange::getImpl(LLVMContext &Context, Metadata *CountNode,
                                int64_t Lo, StorageType Storage,
                                bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DISubrange, (CountNode, Lo));
  Metadata *Ops[] = { CountNode };
  DEFINE_GETIMPL_STORE(DISubrange, (CountNode, Lo), Ops);
}

DIEnumerator *DIEnumerator::getImpl(LLVMContext &Context, int64_t Value,
                                    bool IsUnsigned, MDString *Name,
                                    StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIEnumerator, (Value, IsUnsigned, Name));
  Metadata *Ops[] = {Name};
  DEFINE_GETIMPL_STORE(DIEnumerator, (Value, IsUnsigned), Ops);
}

DIBasicType *DIBasicType::getImpl(LLVMContext &Context, unsigned Tag,
                                  MDString *Name, uint64_t SizeInBits,
                                  uint32_t AlignInBits, unsigned Encoding,
                                  DIFlags Flags, StorageType Storage,
                                  bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIBasicType,
                        (Tag, Name, SizeInBits, AlignInBits, Encoding, Flags));
  Metadata *Ops[] = {nullptr, nullptr, Name};
  DEFINE_GETIMPL_STORE(DIBasicType, (Tag, SizeInBits, AlignInBits, Encoding,
                      Flags), Ops);
}

Optional<DIBasicType::Signedness> DIBasicType::getSignedness() const {
  switch (getEncoding()) {
  case dwarf::DW_ATE_signed:
  case dwarf::DW_ATE_signed_char:
    return Signedness::Signed;
  case dwarf::DW_ATE_unsigned:
  case dwarf::DW_ATE_unsigned_char:
    return Signedness::Unsigned;
  default:
    return None;
  }
}

DIDerivedType *DIDerivedType::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
    unsigned Line, Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits,
    Optional<unsigned> DWARFAddressSpace, DIFlags Flags, Metadata *ExtraData,
    StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIDerivedType,
                        (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                         AlignInBits, OffsetInBits, DWARFAddressSpace, Flags,
                         ExtraData));
  Metadata *Ops[] = {File, Scope, Name, BaseType, ExtraData};
  DEFINE_GETIMPL_STORE(
      DIDerivedType, (Tag, Line, SizeInBits, AlignInBits, OffsetInBits,
                      DWARFAddressSpace, Flags), Ops);
}

DICompositeType *DICompositeType::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
    unsigned Line, Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags,
    Metadata *Elements, unsigned RuntimeLang, Metadata *VTableHolder,
    Metadata *TemplateParams, MDString *Identifier, Metadata *Discriminator,
    StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");

  // Keep this in sync with buildODRType.
  DEFINE_GETIMPL_LOOKUP(
      DICompositeType, (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                        AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
                        VTableHolder, TemplateParams, Identifier, Discriminator));
  Metadata *Ops[] = {File,     Scope,        Name,           BaseType,
                     Elements, VTableHolder, TemplateParams, Identifier,
                     Discriminator};
  DEFINE_GETIMPL_STORE(DICompositeType, (Tag, Line, RuntimeLang, SizeInBits,
                                         AlignInBits, OffsetInBits, Flags),
                       Ops);
}

DICompositeType *DICompositeType::buildODRType(
    LLVMContext &Context, MDString &Identifier, unsigned Tag, MDString *Name,
    Metadata *File, unsigned Line, Metadata *Scope, Metadata *BaseType,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
    Metadata *VTableHolder, Metadata *TemplateParams, Metadata *Discriminator) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  auto *&CT = (*Context.pImpl->DITypeMap)[&Identifier];
  if (!CT)
    return CT = DICompositeType::getDistinct(
               Context, Tag, Name, File, Line, Scope, BaseType, SizeInBits,
               AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
               VTableHolder, TemplateParams, &Identifier, Discriminator);

  // Only mutate CT if it's a forward declaration and the new operands aren't.
  assert(CT->getRawIdentifier() == &Identifier && "Wrong ODR identifier?");
  if (!CT->isForwardDecl() || (Flags & DINode::FlagFwdDecl))
    return CT;

  // Mutate CT in place.  Keep this in sync with getImpl.
  CT->mutate(Tag, Line, RuntimeLang, SizeInBits, AlignInBits, OffsetInBits,
             Flags);
  Metadata *Ops[] = {File,     Scope,        Name,           BaseType,
                     Elements, VTableHolder, TemplateParams, &Identifier,
                     Discriminator};
  assert((std::end(Ops) - std::begin(Ops)) == (int)CT->getNumOperands() &&
         "Mismatched number of operands");
  for (unsigned I = 0, E = CT->getNumOperands(); I != E; ++I)
    if (Ops[I] != CT->getOperand(I))
      CT->setOperand(I, Ops[I]);
  return CT;
}

DICompositeType *DICompositeType::getODRType(
    LLVMContext &Context, MDString &Identifier, unsigned Tag, MDString *Name,
    Metadata *File, unsigned Line, Metadata *Scope, Metadata *BaseType,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
    Metadata *VTableHolder, Metadata *TemplateParams, Metadata *Discriminator) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  auto *&CT = (*Context.pImpl->DITypeMap)[&Identifier];
  if (!CT)
    CT = DICompositeType::getDistinct(
        Context, Tag, Name, File, Line, Scope, BaseType, SizeInBits,
        AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang, VTableHolder,
        TemplateParams, &Identifier, Discriminator);
  return CT;
}

DICompositeType *DICompositeType::getODRTypeIfExists(LLVMContext &Context,
                                                     MDString &Identifier) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  return Context.pImpl->DITypeMap->lookup(&Identifier);
}

DISubroutineType *DISubroutineType::getImpl(LLVMContext &Context, DIFlags Flags,
                                            uint8_t CC, Metadata *TypeArray,
                                            StorageType Storage,
                                            bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DISubroutineType, (Flags, CC, TypeArray));
  Metadata *Ops[] = {nullptr, nullptr, nullptr, TypeArray};
  DEFINE_GETIMPL_STORE(DISubroutineType, (Flags, CC), Ops);
}

// FIXME: Implement this string-enum correspondence with a .def file and macros,
// so that the association is explicit rather than implied.
static const char *ChecksumKindName[DIFile::CSK_Last] = {
  "CSK_MD5",
  "CSK_SHA1"
};

StringRef DIFile::getChecksumKindAsString(ChecksumKind CSKind) {
  assert(CSKind <= DIFile::CSK_Last && "Invalid checksum kind");
  // The first space was originally the CSK_None variant, which is now
  // obsolete, but the space is still reserved in ChecksumKind, so we account
  // for it here.
  return ChecksumKindName[CSKind - 1];
}

Optional<DIFile::ChecksumKind> DIFile::getChecksumKind(StringRef CSKindStr) {
  return StringSwitch<Optional<DIFile::ChecksumKind>>(CSKindStr)
      .Case("CSK_MD5", DIFile::CSK_MD5)
      .Case("CSK_SHA1", DIFile::CSK_SHA1)
      .Default(None);
}

DIFile *DIFile::getImpl(LLVMContext &Context, MDString *Filename,
                        MDString *Directory,
                        Optional<DIFile::ChecksumInfo<MDString *>> CS,
                        Optional<MDString *> Source, StorageType Storage,
                        bool ShouldCreate) {
  assert(isCanonical(Filename) && "Expected canonical MDString");
  assert(isCanonical(Directory) && "Expected canonical MDString");
  assert((!CS || isCanonical(CS->Value)) && "Expected canonical MDString");
  assert((!Source || isCanonical(*Source)) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIFile, (Filename, Directory, CS, Source));
  Metadata *Ops[] = {Filename, Directory, CS ? CS->Value : nullptr,
                     Source.getValueOr(nullptr)};
  DEFINE_GETIMPL_STORE(DIFile, (CS, Source), Ops);
}

DICompileUnit *DICompileUnit::getImpl(
    LLVMContext &Context, unsigned SourceLanguage, Metadata *File,
    MDString *Producer, bool IsOptimized, MDString *Flags,
    unsigned RuntimeVersion, MDString *SplitDebugFilename,
    unsigned EmissionKind, Metadata *EnumTypes, Metadata *RetainedTypes,
    Metadata *GlobalVariables, Metadata *ImportedEntities, Metadata *Macros,
    uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
    unsigned NameTableKind, bool RangesBaseAddress, StorageType Storage,
    bool ShouldCreate) {
  assert(Storage != Uniqued && "Cannot unique DICompileUnit");
  assert(isCanonical(Producer) && "Expected canonical MDString");
  assert(isCanonical(Flags) && "Expected canonical MDString");
  assert(isCanonical(SplitDebugFilename) && "Expected canonical MDString");

  Metadata *Ops[] = {
      File,      Producer,      Flags,           SplitDebugFilename,
      EnumTypes, RetainedTypes, GlobalVariables, ImportedEntities,
      Macros};
  return storeImpl(new (array_lengthof(Ops)) DICompileUnit(
                       Context, Storage, SourceLanguage, IsOptimized,
                       RuntimeVersion, EmissionKind, DWOId, SplitDebugInlining,
                       DebugInfoForProfiling, NameTableKind, RangesBaseAddress,
                       Ops),
                   Storage);
}

Optional<DICompileUnit::DebugEmissionKind>
DICompileUnit::getEmissionKind(StringRef Str) {
  return StringSwitch<Optional<DebugEmissionKind>>(Str)
      .Case("NoDebug", NoDebug)
      .Case("FullDebug", FullDebug)
      .Case("LineTablesOnly", LineTablesOnly)
      .Case("DebugDirectivesOnly", DebugDirectivesOnly)
      .Default(None);
}

Optional<DICompileUnit::DebugNameTableKind>
DICompileUnit::getNameTableKind(StringRef Str) {
  return StringSwitch<Optional<DebugNameTableKind>>(Str)
      .Case("Default", DebugNameTableKind::Default)
      .Case("GNU", DebugNameTableKind::GNU)
      .Case("None", DebugNameTableKind::None)
      .Default(None);
}

const char *DICompileUnit::emissionKindString(DebugEmissionKind EK) {
  switch (EK) {
  case NoDebug:        return "NoDebug";
  case FullDebug:      return "FullDebug";
  case LineTablesOnly: return "LineTablesOnly";
  case DebugDirectivesOnly: return "DebugDirectivesOnly";
  }
  return nullptr;
}

const char *DICompileUnit::nameTableKindString(DebugNameTableKind NTK) {
  switch (NTK) {
  case DebugNameTableKind::Default:
    return nullptr;
  case DebugNameTableKind::GNU:
    return "GNU";
  case DebugNameTableKind::None:
    return "None";
  }
  return nullptr;
}

DISubprogram *DILocalScope::getSubprogram() const {
  if (auto *Block = dyn_cast<DILexicalBlockBase>(this))
    return Block->getScope()->getSubprogram();
  return const_cast<DISubprogram *>(cast<DISubprogram>(this));
}

DILocalScope *DILocalScope::getNonLexicalBlockFileScope() const {
  if (auto *File = dyn_cast<DILexicalBlockFile>(this))
    return File->getScope()->getNonLexicalBlockFileScope();
  return const_cast<DILocalScope *>(this);
}

DISubprogram::DISPFlags DISubprogram::getFlag(StringRef Flag) {
  return StringSwitch<DISPFlags>(Flag)
#define HANDLE_DISP_FLAG(ID, NAME) .Case("DISPFlag" #NAME, SPFlag##NAME)
#include "llvm/IR/DebugInfoFlags.def"
      .Default(SPFlagZero);
}

StringRef DISubprogram::getFlagString(DISPFlags Flag) {
  switch (Flag) {
  // Appease a warning.
  case SPFlagVirtuality:
    return "";
#define HANDLE_DISP_FLAG(ID, NAME)                                             \
  case SPFlag##NAME:                                                           \
    return "DISPFlag" #NAME;
#include "llvm/IR/DebugInfoFlags.def"
  }
  return "";
}

DISubprogram::DISPFlags
DISubprogram::splitFlags(DISPFlags Flags,
                         SmallVectorImpl<DISPFlags> &SplitFlags) {
  // Multi-bit fields can require special handling. In our case, however, the
  // only multi-bit field is virtuality, and all its values happen to be
  // single-bit values, so the right behavior just falls out.
#define HANDLE_DISP_FLAG(ID, NAME)                                             \
  if (DISPFlags Bit = Flags & SPFlag##NAME) {                                  \
    SplitFlags.push_back(Bit);                                                 \
    Flags &= ~Bit;                                                             \
  }
#include "llvm/IR/DebugInfoFlags.def"
  return Flags;
}

DISubprogram *DISubprogram::getImpl(
    LLVMContext &Context, Metadata *Scope, MDString *Name,
    MDString *LinkageName, Metadata *File, unsigned Line, Metadata *Type,
    unsigned ScopeLine, Metadata *ContainingType, unsigned VirtualIndex,
    int ThisAdjustment, DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
    Metadata *TemplateParams, Metadata *Declaration, Metadata *RetainedNodes,
    Metadata *ThrownTypes, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(LinkageName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DISubprogram,
                        (Scope, Name, LinkageName, File, Line, Type, ScopeLine,
                         ContainingType, VirtualIndex, ThisAdjustment, Flags,
                         SPFlags, Unit, TemplateParams, Declaration,
                         RetainedNodes, ThrownTypes));
  SmallVector<Metadata *, 11> Ops = {
      File,        Scope,         Name,           LinkageName,    Type,       Unit,
      Declaration, RetainedNodes, ContainingType, TemplateParams, ThrownTypes};
  if (!ThrownTypes) {
    Ops.pop_back();
    if (!TemplateParams) {
      Ops.pop_back();
      if (!ContainingType)
        Ops.pop_back();
    }
  }
  DEFINE_GETIMPL_STORE_N(
      DISubprogram,
      (Line, ScopeLine, VirtualIndex, ThisAdjustment, Flags, SPFlags), Ops,
      Ops.size());
}

bool DISubprogram::describes(const Function *F) const {
  assert(F && "Invalid function");
  if (F->getSubprogram() == this)
    return true;
  StringRef Name = getLinkageName();
  if (Name.empty())
    Name = getName();
  return F->getName() == Name;
}

DILexicalBlock *DILexicalBlock::getImpl(LLVMContext &Context, Metadata *Scope,
                                        Metadata *File, unsigned Line,
                                        unsigned Column, StorageType Storage,
                                        bool ShouldCreate) {
  // Fixup column.
  adjustColumn(Column);

  assert(Scope && "Expected scope");
  DEFINE_GETIMPL_LOOKUP(DILexicalBlock, (Scope, File, Line, Column));
  Metadata *Ops[] = {File, Scope};
  DEFINE_GETIMPL_STORE(DILexicalBlock, (Line, Column), Ops);
}

DILexicalBlockFile *DILexicalBlockFile::getImpl(LLVMContext &Context,
                                                Metadata *Scope, Metadata *File,
                                                unsigned Discriminator,
                                                StorageType Storage,
                                                bool ShouldCreate) {
  assert(Scope && "Expected scope");
  DEFINE_GETIMPL_LOOKUP(DILexicalBlockFile, (Scope, File, Discriminator));
  Metadata *Ops[] = {File, Scope};
  DEFINE_GETIMPL_STORE(DILexicalBlockFile, (Discriminator), Ops);
}

DINamespace *DINamespace::getImpl(LLVMContext &Context, Metadata *Scope,
                                  MDString *Name, bool ExportSymbols,
                                  StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DINamespace, (Scope, Name, ExportSymbols));
  // The nullptr is for DIScope's File operand. This should be refactored.
  Metadata *Ops[] = {nullptr, Scope, Name};
  DEFINE_GETIMPL_STORE(DINamespace, (ExportSymbols), Ops);
}

DIModule *DIModule::getImpl(LLVMContext &Context, Metadata *Scope,
                            MDString *Name, MDString *ConfigurationMacros,
                            MDString *IncludePath, MDString *ISysRoot,
                            StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(
      DIModule, (Scope, Name, ConfigurationMacros, IncludePath, ISysRoot));
  Metadata *Ops[] = {Scope, Name, ConfigurationMacros, IncludePath, ISysRoot};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DIModule, Ops);
}

DITemplateTypeParameter *DITemplateTypeParameter::getImpl(LLVMContext &Context,
                                                          MDString *Name,
                                                          Metadata *Type,
                                                          StorageType Storage,
                                                          bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DITemplateTypeParameter, (Name, Type));
  Metadata *Ops[] = {Name, Type};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DITemplateTypeParameter, Ops);
}

DITemplateValueParameter *DITemplateValueParameter::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *Type,
    Metadata *Value, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DITemplateValueParameter, (Tag, Name, Type, Value));
  Metadata *Ops[] = {Name, Type, Value};
  DEFINE_GETIMPL_STORE(DITemplateValueParameter, (Tag), Ops);
}

DIGlobalVariable *
DIGlobalVariable::getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
                          MDString *LinkageName, Metadata *File, unsigned Line,
                          Metadata *Type, bool IsLocalToUnit, bool IsDefinition,
                          Metadata *StaticDataMemberDeclaration,
                          Metadata *TemplateParams, uint32_t AlignInBits,
                          StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(LinkageName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIGlobalVariable, (Scope, Name, LinkageName, File, Line,
                                           Type, IsLocalToUnit, IsDefinition,
                                           StaticDataMemberDeclaration,
                                           TemplateParams, AlignInBits));
  Metadata *Ops[] = {Scope,
                     Name,
                     File,
                     Type,
                     Name,
                     LinkageName,
                     StaticDataMemberDeclaration,
                     TemplateParams};
  DEFINE_GETIMPL_STORE(DIGlobalVariable,
                       (Line, IsLocalToUnit, IsDefinition, AlignInBits), Ops);
}

DILocalVariable *DILocalVariable::getImpl(LLVMContext &Context, Metadata *Scope,
                                          MDString *Name, Metadata *File,
                                          unsigned Line, Metadata *Type,
                                          unsigned Arg, DIFlags Flags,
                                          uint32_t AlignInBits,
                                          StorageType Storage,
                                          bool ShouldCreate) {
  // 64K ought to be enough for any frontend.
  assert(Arg <= UINT16_MAX && "Expected argument number to fit in 16-bits");

  assert(Scope && "Expected scope");
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DILocalVariable,
                        (Scope, Name, File, Line, Type, Arg, Flags,
                         AlignInBits));
  Metadata *Ops[] = {Scope, Name, File, Type};
  DEFINE_GETIMPL_STORE(DILocalVariable, (Line, Arg, Flags, AlignInBits), Ops);
}

Optional<uint64_t> DIVariable::getSizeInBits() const {
  // This is used by the Verifier so be mindful of broken types.
  const Metadata *RawType = getRawType();
  while (RawType) {
    // Try to get the size directly.
    if (auto *T = dyn_cast<DIType>(RawType))
      if (uint64_t Size = T->getSizeInBits())
        return Size;

    if (auto *DT = dyn_cast<DIDerivedType>(RawType)) {
      // Look at the base type.
      RawType = DT->getRawBaseType();
      continue;
    }

    // Missing type or size.
    break;
  }

  // Fail gracefully.
  return None;
}

DILabel *DILabel::getImpl(LLVMContext &Context, Metadata *Scope,
                          MDString *Name, Metadata *File, unsigned Line,
                          StorageType Storage,
                          bool ShouldCreate) {
  assert(Scope && "Expected scope");
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DILabel,
                        (Scope, Name, File, Line));
  Metadata *Ops[] = {Scope, Name, File};
  DEFINE_GETIMPL_STORE(DILabel, (Line), Ops);
}

DIExpression *DIExpression::getImpl(LLVMContext &Context,
                                    ArrayRef<uint64_t> Elements,
                                    StorageType Storage, bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIExpression, (Elements));
  DEFINE_GETIMPL_STORE_NO_OPS(DIExpression, (Elements));
}

unsigned DIExpression::ExprOperand::getSize() const {
  switch (getOp()) {
  case dwarf::DW_OP_LLVM_fragment:
    return 3;
  case dwarf::DW_OP_constu:
  case dwarf::DW_OP_plus_uconst:
    return 2;
  default:
    return 1;
  }
}

bool DIExpression::isValid() const {
  for (auto I = expr_op_begin(), E = expr_op_end(); I != E; ++I) {
    // Check that there's space for the operand.
    if (I->get() + I->getSize() > E->get())
      return false;

    // Check that the operand is valid.
    switch (I->getOp()) {
    default:
      return false;
    case dwarf::DW_OP_LLVM_fragment:
      // A fragment operator must appear at the end.
      return I->get() + I->getSize() == E->get();
    case dwarf::DW_OP_stack_value: {
      // Must be the last one or followed by a DW_OP_LLVM_fragment.
      if (I->get() + I->getSize() == E->get())
        break;
      auto J = I;
      if ((++J)->getOp() != dwarf::DW_OP_LLVM_fragment)
        return false;
      break;
    }
    case dwarf::DW_OP_swap: {
      // Must be more than one implicit element on the stack.

      // FIXME: A better way to implement this would be to add a local variable
      // that keeps track of the stack depth and introduce something like a
      // DW_LLVM_OP_implicit_location as a placeholder for the location this
      // DIExpression is attached to, or else pass the number of implicit stack
      // elements into isValid.
      if (getNumElements() == 1)
        return false;
      break;
    }
    case dwarf::DW_OP_constu:
    case dwarf::DW_OP_plus_uconst:
    case dwarf::DW_OP_plus:
    case dwarf::DW_OP_minus:
    case dwarf::DW_OP_mul:
    case dwarf::DW_OP_div:
    case dwarf::DW_OP_mod:
    case dwarf::DW_OP_or:
    case dwarf::DW_OP_and:
    case dwarf::DW_OP_xor:
    case dwarf::DW_OP_shl:
    case dwarf::DW_OP_shr:
    case dwarf::DW_OP_shra:
    case dwarf::DW_OP_deref:
    case dwarf::DW_OP_xderef:
    case dwarf::DW_OP_lit0:
    case dwarf::DW_OP_not:
    case dwarf::DW_OP_dup:
      break;
    }
  }
  return true;
}

Optional<DIExpression::FragmentInfo>
DIExpression::getFragmentInfo(expr_op_iterator Start, expr_op_iterator End) {
  for (auto I = Start; I != End; ++I)
    if (I->getOp() == dwarf::DW_OP_LLVM_fragment) {
      DIExpression::FragmentInfo Info = {I->getArg(1), I->getArg(0)};
      return Info;
    }
  return None;
}

void DIExpression::appendOffset(SmallVectorImpl<uint64_t> &Ops,
                                int64_t Offset) {
  if (Offset > 0) {
    Ops.push_back(dwarf::DW_OP_plus_uconst);
    Ops.push_back(Offset);
  } else if (Offset < 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    Ops.push_back(-Offset);
    Ops.push_back(dwarf::DW_OP_minus);
  }
}

bool DIExpression::extractIfOffset(int64_t &Offset) const {
  if (getNumElements() == 0) {
    Offset = 0;
    return true;
  }

  if (getNumElements() == 2 && Elements[0] == dwarf::DW_OP_plus_uconst) {
    Offset = Elements[1];
    return true;
  }

  if (getNumElements() == 3 && Elements[0] == dwarf::DW_OP_constu) {
    if (Elements[2] == dwarf::DW_OP_plus) {
      Offset = Elements[1];
      return true;
    }
    if (Elements[2] == dwarf::DW_OP_minus) {
      Offset = -Elements[1];
      return true;
    }
  }

  return false;
}

DIExpression *DIExpression::prepend(const DIExpression *Expr, bool DerefBefore,
                                    int64_t Offset, bool DerefAfter,
                                    bool StackValue) {
  SmallVector<uint64_t, 8> Ops;
  if (DerefBefore)
    Ops.push_back(dwarf::DW_OP_deref);

  appendOffset(Ops, Offset);
  if (DerefAfter)
    Ops.push_back(dwarf::DW_OP_deref);

  return prependOpcodes(Expr, Ops, StackValue);
}

DIExpression *DIExpression::prependOpcodes(const DIExpression *Expr,
                                           SmallVectorImpl<uint64_t> &Ops,
                                           bool StackValue) {
  assert(Expr && "Can't prepend ops to this expression");

  // If there are no ops to prepend, do not even add the DW_OP_stack_value.
  if (Ops.empty())
    StackValue = false;
  for (auto Op : Expr->expr_ops()) {
    // A DW_OP_stack_value comes at the end, but before a DW_OP_LLVM_fragment.
    if (StackValue) {
      if (Op.getOp() == dwarf::DW_OP_stack_value)
        StackValue = false;
      else if (Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
        Ops.push_back(dwarf::DW_OP_stack_value);
        StackValue = false;
      }
    }
    Op.appendToVector(Ops);
  }
  if (StackValue)
    Ops.push_back(dwarf::DW_OP_stack_value);
  return DIExpression::get(Expr->getContext(), Ops);
}

DIExpression *DIExpression::append(const DIExpression *Expr,
                                   ArrayRef<uint64_t> Ops) {
  assert(Expr && !Ops.empty() && "Can't append ops to this expression");

  // Copy Expr's current op list.
  SmallVector<uint64_t, 16> NewOps;
  for (auto Op : Expr->expr_ops()) {
    // Append new opcodes before DW_OP_{stack_value, LLVM_fragment}.
    if (Op.getOp() == dwarf::DW_OP_stack_value ||
        Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
      NewOps.append(Ops.begin(), Ops.end());

      // Ensure that the new opcodes are only appended once.
      Ops = None;
    }
    Op.appendToVector(NewOps);
  }

  NewOps.append(Ops.begin(), Ops.end());
  return DIExpression::get(Expr->getContext(), NewOps);
}

DIExpression *DIExpression::appendToStack(const DIExpression *Expr,
                                          ArrayRef<uint64_t> Ops) {
  assert(Expr && !Ops.empty() && "Can't append ops to this expression");
  assert(none_of(Ops,
                 [](uint64_t Op) {
                   return Op == dwarf::DW_OP_stack_value ||
                          Op == dwarf::DW_OP_LLVM_fragment;
                 }) &&
         "Can't append this op");

  // Append a DW_OP_deref after Expr's current op list if it's non-empty and
  // has no DW_OP_stack_value.
  //
  // Match .* DW_OP_stack_value (DW_OP_LLVM_fragment A B)?.
  Optional<FragmentInfo> FI = Expr->getFragmentInfo();
  unsigned DropUntilStackValue = FI.hasValue() ? 3 : 0;
  ArrayRef<uint64_t> ExprOpsBeforeFragment =
      Expr->getElements().drop_back(DropUntilStackValue);
  bool NeedsDeref = (Expr->getNumElements() > DropUntilStackValue) &&
                    (ExprOpsBeforeFragment.back() != dwarf::DW_OP_stack_value);
  bool NeedsStackValue = NeedsDeref || ExprOpsBeforeFragment.empty();

  // Append a DW_OP_deref after Expr's current op list if needed, then append
  // the new ops, and finally ensure that a single DW_OP_stack_value is present.
  SmallVector<uint64_t, 16> NewOps;
  if (NeedsDeref)
    NewOps.push_back(dwarf::DW_OP_deref);
  NewOps.append(Ops.begin(), Ops.end());
  if (NeedsStackValue)
    NewOps.push_back(dwarf::DW_OP_stack_value);
  return DIExpression::append(Expr, NewOps);
}

Optional<DIExpression *> DIExpression::createFragmentExpression(
    const DIExpression *Expr, unsigned OffsetInBits, unsigned SizeInBits) {
  SmallVector<uint64_t, 8> Ops;
  // Copy over the expression, but leave off any trailing DW_OP_LLVM_fragment.
  if (Expr) {
    for (auto Op : Expr->expr_ops()) {
      switch (Op.getOp()) {
      default: break;
      case dwarf::DW_OP_plus:
      case dwarf::DW_OP_minus:
        // We can't safely split arithmetic into multiple fragments because we
        // can't express carry-over between fragments.
        //
        // FIXME: We *could* preserve the lowest fragment of a constant offset
        // operation if the offset fits into SizeInBits.
        return None;
      case dwarf::DW_OP_LLVM_fragment: {
        // Make the new offset point into the existing fragment.
        uint64_t FragmentOffsetInBits = Op.getArg(0);
        uint64_t FragmentSizeInBits = Op.getArg(1);
        (void)FragmentSizeInBits;
        assert((OffsetInBits + SizeInBits <= FragmentSizeInBits) &&
               "new fragment outside of original fragment");
        OffsetInBits += FragmentOffsetInBits;
        continue;
      }
      }
      Op.appendToVector(Ops);
    }
  }
  Ops.push_back(dwarf::DW_OP_LLVM_fragment);
  Ops.push_back(OffsetInBits);
  Ops.push_back(SizeInBits);
  return DIExpression::get(Expr->getContext(), Ops);
}

bool DIExpression::isConstant() const {
  // Recognize DW_OP_constu C DW_OP_stack_value (DW_OP_LLVM_fragment Len Ofs)?.
  if (getNumElements() != 3 && getNumElements() != 6)
    return false;
  if (getElement(0) != dwarf::DW_OP_constu ||
      getElement(2) != dwarf::DW_OP_stack_value)
    return false;
  if (getNumElements() == 6 && getElement(3) != dwarf::DW_OP_LLVM_fragment)
    return false;
  return true;
}

DIGlobalVariableExpression *
DIGlobalVariableExpression::getImpl(LLVMContext &Context, Metadata *Variable,
                                    Metadata *Expression, StorageType Storage,
                                    bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIGlobalVariableExpression, (Variable, Expression));
  Metadata *Ops[] = {Variable, Expression};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DIGlobalVariableExpression, Ops);
}

DIObjCProperty *DIObjCProperty::getImpl(
    LLVMContext &Context, MDString *Name, Metadata *File, unsigned Line,
    MDString *GetterName, MDString *SetterName, unsigned Attributes,
    Metadata *Type, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(GetterName) && "Expected canonical MDString");
  assert(isCanonical(SetterName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIObjCProperty, (Name, File, Line, GetterName,
                                         SetterName, Attributes, Type));
  Metadata *Ops[] = {Name, File, GetterName, SetterName, Type};
  DEFINE_GETIMPL_STORE(DIObjCProperty, (Line, Attributes), Ops);
}

DIImportedEntity *DIImportedEntity::getImpl(LLVMContext &Context, unsigned Tag,
                                            Metadata *Scope, Metadata *Entity,
                                            Metadata *File, unsigned Line,
                                            MDString *Name, StorageType Storage,
                                            bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIImportedEntity,
                        (Tag, Scope, Entity, File, Line, Name));
  Metadata *Ops[] = {Scope, Entity, Name, File};
  DEFINE_GETIMPL_STORE(DIImportedEntity, (Tag, Line), Ops);
}

DIMacro *DIMacro::getImpl(LLVMContext &Context, unsigned MIType,
                          unsigned Line, MDString *Name, MDString *Value,
                          StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIMacro, (MIType, Line, Name, Value));
  Metadata *Ops[] = { Name, Value };
  DEFINE_GETIMPL_STORE(DIMacro, (MIType, Line), Ops);
}

DIMacroFile *DIMacroFile::getImpl(LLVMContext &Context, unsigned MIType,
                                  unsigned Line, Metadata *File,
                                  Metadata *Elements, StorageType Storage,
                                  bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIMacroFile,
                        (MIType, Line, File, Elements));
  Metadata *Ops[] = { File, Elements };
  DEFINE_GETIMPL_STORE(DIMacroFile, (MIType, Line), Ops);
}
