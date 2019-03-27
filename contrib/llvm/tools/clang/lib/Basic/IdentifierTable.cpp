//===- IdentifierTable.cpp - Hash table for identifier lookup -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the IdentifierInfo, IdentifierVisitor, and
// IdentifierTable interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace clang;

//===----------------------------------------------------------------------===//
// IdentifierTable Implementation
//===----------------------------------------------------------------------===//

IdentifierIterator::~IdentifierIterator() = default;

IdentifierInfoLookup::~IdentifierInfoLookup() = default;

namespace {

/// A simple identifier lookup iterator that represents an
/// empty sequence of identifiers.
class EmptyLookupIterator : public IdentifierIterator
{
public:
  StringRef Next() override { return StringRef(); }
};

} // namespace

IdentifierIterator *IdentifierInfoLookup::getIdentifiers() {
  return new EmptyLookupIterator();
}

IdentifierTable::IdentifierTable(IdentifierInfoLookup *ExternalLookup)
    : HashTable(8192), // Start with space for 8K identifiers.
      ExternalLookup(ExternalLookup) {}

IdentifierTable::IdentifierTable(const LangOptions &LangOpts,
                                 IdentifierInfoLookup *ExternalLookup)
    : IdentifierTable(ExternalLookup) {
  // Populate the identifier table with info about keywords for the current
  // language.
  AddKeywords(LangOpts);
}

//===----------------------------------------------------------------------===//
// Language Keyword Implementation
//===----------------------------------------------------------------------===//

// Constants for TokenKinds.def
namespace {

  enum {
    KEYC99        = 0x1,
    KEYCXX        = 0x2,
    KEYCXX11      = 0x4,
    KEYGNU        = 0x8,
    KEYMS         = 0x10,
    BOOLSUPPORT   = 0x20,
    KEYALTIVEC    = 0x40,
    KEYNOCXX      = 0x80,
    KEYBORLAND    = 0x100,
    KEYOPENCLC    = 0x200,
    KEYC11        = 0x400,
    KEYNOMS18     = 0x800,
    KEYNOOPENCL   = 0x1000,
    WCHARSUPPORT  = 0x2000,
    HALFSUPPORT   = 0x4000,
    CHAR8SUPPORT  = 0x8000,
    KEYCONCEPTS   = 0x10000,
    KEYOBJC       = 0x20000,
    KEYZVECTOR    = 0x40000,
    KEYCOROUTINES = 0x80000,
    KEYMODULES    = 0x100000,
    KEYCXX2A      = 0x200000,
    KEYOPENCLCXX  = 0x400000,
    KEYALLCXX = KEYCXX | KEYCXX11 | KEYCXX2A,
    KEYALL = (0xffffff & ~KEYNOMS18 &
              ~KEYNOOPENCL) // KEYNOMS18 and KEYNOOPENCL are used to exclude.
  };

  /// How a keyword is treated in the selected standard.
  enum KeywordStatus {
    KS_Disabled,    // Disabled
    KS_Extension,   // Is an extension
    KS_Enabled,     // Enabled
    KS_Future       // Is a keyword in future standard
  };

} // namespace

/// Translates flags as specified in TokenKinds.def into keyword status
/// in the given language standard.
static KeywordStatus getKeywordStatus(const LangOptions &LangOpts,
                                      unsigned Flags) {
  if (Flags == KEYALL) return KS_Enabled;
  if (LangOpts.CPlusPlus && (Flags & KEYCXX)) return KS_Enabled;
  if (LangOpts.CPlusPlus11 && (Flags & KEYCXX11)) return KS_Enabled;
  if (LangOpts.CPlusPlus2a && (Flags & KEYCXX2A)) return KS_Enabled;
  if (LangOpts.C99 && (Flags & KEYC99)) return KS_Enabled;
  if (LangOpts.GNUKeywords && (Flags & KEYGNU)) return KS_Extension;
  if (LangOpts.MicrosoftExt && (Flags & KEYMS)) return KS_Extension;
  if (LangOpts.Borland && (Flags & KEYBORLAND)) return KS_Extension;
  if (LangOpts.Bool && (Flags & BOOLSUPPORT)) return KS_Enabled;
  if (LangOpts.Half && (Flags & HALFSUPPORT)) return KS_Enabled;
  if (LangOpts.WChar && (Flags & WCHARSUPPORT)) return KS_Enabled;
  if (LangOpts.Char8 && (Flags & CHAR8SUPPORT)) return KS_Enabled;
  if (LangOpts.AltiVec && (Flags & KEYALTIVEC)) return KS_Enabled;
  if (LangOpts.ZVector && (Flags & KEYZVECTOR)) return KS_Enabled;
  if (LangOpts.OpenCL && !LangOpts.OpenCLCPlusPlus && (Flags & KEYOPENCLC))
    return KS_Enabled;
  if (LangOpts.OpenCLCPlusPlus && (Flags & KEYOPENCLCXX)) return KS_Enabled;
  if (!LangOpts.CPlusPlus && (Flags & KEYNOCXX)) return KS_Enabled;
  if (LangOpts.C11 && (Flags & KEYC11)) return KS_Enabled;
  // We treat bridge casts as objective-C keywords so we can warn on them
  // in non-arc mode.
  if (LangOpts.ObjC && (Flags & KEYOBJC)) return KS_Enabled;
  if (LangOpts.ConceptsTS && (Flags & KEYCONCEPTS)) return KS_Enabled;
  if (LangOpts.CoroutinesTS && (Flags & KEYCOROUTINES)) return KS_Enabled;
  if (LangOpts.ModulesTS && (Flags & KEYMODULES)) return KS_Enabled;
  if (LangOpts.CPlusPlus && (Flags & KEYALLCXX)) return KS_Future;
  return KS_Disabled;
}

/// AddKeyword - This method is used to associate a token ID with specific
/// identifiers because they are language keywords.  This causes the lexer to
/// automatically map matching identifiers to specialized token codes.
static void AddKeyword(StringRef Keyword,
                       tok::TokenKind TokenCode, unsigned Flags,
                       const LangOptions &LangOpts, IdentifierTable &Table) {
  KeywordStatus AddResult = getKeywordStatus(LangOpts, Flags);

  // Don't add this keyword under MSVCCompat.
  if (LangOpts.MSVCCompat && (Flags & KEYNOMS18) &&
      !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
    return;

  // Don't add this keyword under OpenCL.
  if (LangOpts.OpenCL && (Flags & KEYNOOPENCL))
    return;

  // Don't add this keyword if disabled in this language.
  if (AddResult == KS_Disabled) return;

  IdentifierInfo &Info =
      Table.get(Keyword, AddResult == KS_Future ? tok::identifier : TokenCode);
  Info.setIsExtensionToken(AddResult == KS_Extension);
  Info.setIsFutureCompatKeyword(AddResult == KS_Future);
}

/// AddCXXOperatorKeyword - Register a C++ operator keyword alternative
/// representations.
static void AddCXXOperatorKeyword(StringRef Keyword,
                                  tok::TokenKind TokenCode,
                                  IdentifierTable &Table) {
  IdentifierInfo &Info = Table.get(Keyword, TokenCode);
  Info.setIsCPlusPlusOperatorKeyword();
}

/// AddObjCKeyword - Register an Objective-C \@keyword like "class" "selector"
/// or "property".
static void AddObjCKeyword(StringRef Name,
                           tok::ObjCKeywordKind ObjCID,
                           IdentifierTable &Table) {
  Table.get(Name).setObjCKeywordID(ObjCID);
}

/// AddKeywords - Add all keywords to the symbol table.
///
void IdentifierTable::AddKeywords(const LangOptions &LangOpts) {
  // Add keywords and tokens for the current language.
#define KEYWORD(NAME, FLAGS) \
  AddKeyword(StringRef(#NAME), tok::kw_ ## NAME,  \
             FLAGS, LangOpts, *this);
#define ALIAS(NAME, TOK, FLAGS) \
  AddKeyword(StringRef(NAME), tok::kw_ ## TOK,  \
             FLAGS, LangOpts, *this);
#define CXX_KEYWORD_OPERATOR(NAME, ALIAS) \
  if (LangOpts.CXXOperatorNames)          \
    AddCXXOperatorKeyword(StringRef(#NAME), tok::ALIAS, *this);
#define OBJC_AT_KEYWORD(NAME)  \
  if (LangOpts.ObjC)           \
    AddObjCKeyword(StringRef(#NAME), tok::objc_##NAME, *this);
#define TESTING_KEYWORD(NAME, FLAGS)
#include "clang/Basic/TokenKinds.def"

  if (LangOpts.ParseUnknownAnytype)
    AddKeyword("__unknown_anytype", tok::kw___unknown_anytype, KEYALL,
               LangOpts, *this);

  if (LangOpts.DeclSpecKeyword)
    AddKeyword("__declspec", tok::kw___declspec, KEYALL, LangOpts, *this);

  // Add the '_experimental_modules_import' contextual keyword.
  get("import").setModulesImport(true);
}

/// Checks if the specified token kind represents a keyword in the
/// specified language.
/// \returns Status of the keyword in the language.
static KeywordStatus getTokenKwStatus(const LangOptions &LangOpts,
                                      tok::TokenKind K) {
  switch (K) {
#define KEYWORD(NAME, FLAGS) \
  case tok::kw_##NAME: return getKeywordStatus(LangOpts, FLAGS);
#include "clang/Basic/TokenKinds.def"
  default: return KS_Disabled;
  }
}

/// Returns true if the identifier represents a keyword in the
/// specified language.
bool IdentifierInfo::isKeyword(const LangOptions &LangOpts) const {
  switch (getTokenKwStatus(LangOpts, getTokenID())) {
  case KS_Enabled:
  case KS_Extension:
    return true;
  default:
    return false;
  }
}

/// Returns true if the identifier represents a C++ keyword in the
/// specified language.
bool IdentifierInfo::isCPlusPlusKeyword(const LangOptions &LangOpts) const {
  if (!LangOpts.CPlusPlus || !isKeyword(LangOpts))
    return false;
  // This is a C++ keyword if this identifier is not a keyword when checked
  // using LangOptions without C++ support.
  LangOptions LangOptsNoCPP = LangOpts;
  LangOptsNoCPP.CPlusPlus = false;
  LangOptsNoCPP.CPlusPlus11 = false;
  LangOptsNoCPP.CPlusPlus2a = false;
  return !isKeyword(LangOptsNoCPP);
}

tok::PPKeywordKind IdentifierInfo::getPPKeywordID() const {
  // We use a perfect hash function here involving the length of the keyword,
  // the first and third character.  For preprocessor ID's there are no
  // collisions (if there were, the switch below would complain about duplicate
  // case values).  Note that this depends on 'if' being null terminated.

#define HASH(LEN, FIRST, THIRD) \
  (LEN << 5) + (((FIRST-'a') + (THIRD-'a')) & 31)
#define CASE(LEN, FIRST, THIRD, NAME) \
  case HASH(LEN, FIRST, THIRD): \
    return memcmp(Name, #NAME, LEN) ? tok::pp_not_keyword : tok::pp_ ## NAME

  unsigned Len = getLength();
  if (Len < 2) return tok::pp_not_keyword;
  const char *Name = getNameStart();
  switch (HASH(Len, Name[0], Name[2])) {
  default: return tok::pp_not_keyword;
  CASE( 2, 'i', '\0', if);
  CASE( 4, 'e', 'i', elif);
  CASE( 4, 'e', 's', else);
  CASE( 4, 'l', 'n', line);
  CASE( 4, 's', 'c', sccs);
  CASE( 5, 'e', 'd', endif);
  CASE( 5, 'e', 'r', error);
  CASE( 5, 'i', 'e', ident);
  CASE( 5, 'i', 'd', ifdef);
  CASE( 5, 'u', 'd', undef);

  CASE( 6, 'a', 's', assert);
  CASE( 6, 'd', 'f', define);
  CASE( 6, 'i', 'n', ifndef);
  CASE( 6, 'i', 'p', import);
  CASE( 6, 'p', 'a', pragma);

  CASE( 7, 'd', 'f', defined);
  CASE( 7, 'i', 'c', include);
  CASE( 7, 'w', 'r', warning);

  CASE( 8, 'u', 'a', unassert);
  CASE(12, 'i', 'c', include_next);

  CASE(14, '_', 'p', __public_macro);

  CASE(15, '_', 'p', __private_macro);

  CASE(16, '_', 'i', __include_macros);
#undef CASE
#undef HASH
  }
}

//===----------------------------------------------------------------------===//
// Stats Implementation
//===----------------------------------------------------------------------===//

/// PrintStats - Print statistics about how well the identifier table is doing
/// at hashing identifiers.
void IdentifierTable::PrintStats() const {
  unsigned NumBuckets = HashTable.getNumBuckets();
  unsigned NumIdentifiers = HashTable.getNumItems();
  unsigned NumEmptyBuckets = NumBuckets-NumIdentifiers;
  unsigned AverageIdentifierSize = 0;
  unsigned MaxIdentifierLength = 0;

  // TODO: Figure out maximum times an identifier had to probe for -stats.
  for (llvm::StringMap<IdentifierInfo*, llvm::BumpPtrAllocator>::const_iterator
       I = HashTable.begin(), E = HashTable.end(); I != E; ++I) {
    unsigned IdLen = I->getKeyLength();
    AverageIdentifierSize += IdLen;
    if (MaxIdentifierLength < IdLen)
      MaxIdentifierLength = IdLen;
  }

  fprintf(stderr, "\n*** Identifier Table Stats:\n");
  fprintf(stderr, "# Identifiers:   %d\n", NumIdentifiers);
  fprintf(stderr, "# Empty Buckets: %d\n", NumEmptyBuckets);
  fprintf(stderr, "Hash density (#identifiers per bucket): %f\n",
          NumIdentifiers/(double)NumBuckets);
  fprintf(stderr, "Ave identifier length: %f\n",
          (AverageIdentifierSize/(double)NumIdentifiers));
  fprintf(stderr, "Max identifier length: %d\n", MaxIdentifierLength);

  // Compute statistics about the memory allocated for identifiers.
  HashTable.getAllocator().PrintStats();
}

//===----------------------------------------------------------------------===//
// SelectorTable Implementation
//===----------------------------------------------------------------------===//

unsigned llvm::DenseMapInfo<clang::Selector>::getHashValue(clang::Selector S) {
  return DenseMapInfo<void*>::getHashValue(S.getAsOpaquePtr());
}

namespace clang {

/// One of these variable length records is kept for each
/// selector containing more than one keyword. We use a folding set
/// to unique aggregate names (keyword selectors in ObjC parlance). Access to
/// this class is provided strictly through Selector.
class alignas(IdentifierInfoAlignment) MultiKeywordSelector
    : public detail::DeclarationNameExtra,
      public llvm::FoldingSetNode {
  MultiKeywordSelector(unsigned nKeys) : DeclarationNameExtra(nKeys) {}

public:
  // Constructor for keyword selectors.
  MultiKeywordSelector(unsigned nKeys, IdentifierInfo **IIV)
      : DeclarationNameExtra(nKeys) {
    assert((nKeys > 1) && "not a multi-keyword selector");

    // Fill in the trailing keyword array.
    IdentifierInfo **KeyInfo = reinterpret_cast<IdentifierInfo **>(this + 1);
    for (unsigned i = 0; i != nKeys; ++i)
      KeyInfo[i] = IIV[i];
  }

  // getName - Derive the full selector name and return it.
  std::string getName() const;

  using DeclarationNameExtra::getNumArgs;

  using keyword_iterator = IdentifierInfo *const *;

  keyword_iterator keyword_begin() const {
    return reinterpret_cast<keyword_iterator>(this + 1);
  }

  keyword_iterator keyword_end() const {
    return keyword_begin() + getNumArgs();
  }

  IdentifierInfo *getIdentifierInfoForSlot(unsigned i) const {
    assert(i < getNumArgs() && "getIdentifierInfoForSlot(): illegal index");
    return keyword_begin()[i];
  }

  static void Profile(llvm::FoldingSetNodeID &ID, keyword_iterator ArgTys,
                      unsigned NumArgs) {
    ID.AddInteger(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      ID.AddPointer(ArgTys[i]);
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, keyword_begin(), getNumArgs());
  }
};

} // namespace clang.

unsigned Selector::getNumArgs() const {
  unsigned IIF = getIdentifierInfoFlag();
  if (IIF <= ZeroArg)
    return 0;
  if (IIF == OneArg)
    return 1;
  // We point to a MultiKeywordSelector.
  MultiKeywordSelector *SI = getMultiKeywordSelector();
  return SI->getNumArgs();
}

IdentifierInfo *Selector::getIdentifierInfoForSlot(unsigned argIndex) const {
  if (getIdentifierInfoFlag() < MultiArg) {
    assert(argIndex == 0 && "illegal keyword index");
    return getAsIdentifierInfo();
  }

  // We point to a MultiKeywordSelector.
  MultiKeywordSelector *SI = getMultiKeywordSelector();
  return SI->getIdentifierInfoForSlot(argIndex);
}

StringRef Selector::getNameForSlot(unsigned int argIndex) const {
  IdentifierInfo *II = getIdentifierInfoForSlot(argIndex);
  return II ? II->getName() : StringRef();
}

std::string MultiKeywordSelector::getName() const {
  SmallString<256> Str;
  llvm::raw_svector_ostream OS(Str);
  for (keyword_iterator I = keyword_begin(), E = keyword_end(); I != E; ++I) {
    if (*I)
      OS << (*I)->getName();
    OS << ':';
  }

  return OS.str();
}

std::string Selector::getAsString() const {
  if (InfoPtr == 0)
    return "<null selector>";

  if (getIdentifierInfoFlag() < MultiArg) {
    IdentifierInfo *II = getAsIdentifierInfo();

    if (getNumArgs() == 0) {
      assert(II && "If the number of arguments is 0 then II is guaranteed to "
                   "not be null.");
      return II->getName();
    }

    if (!II)
      return ":";

    return II->getName().str() + ":";
  }

  // We have a multiple keyword selector.
  return getMultiKeywordSelector()->getName();
}

void Selector::print(llvm::raw_ostream &OS) const {
  OS << getAsString();
}

LLVM_DUMP_METHOD void Selector::dump() const { print(llvm::errs()); }

/// Interpreting the given string using the normal CamelCase
/// conventions, determine whether the given string starts with the
/// given "word", which is assumed to end in a lowercase letter.
static bool startsWithWord(StringRef name, StringRef word) {
  if (name.size() < word.size()) return false;
  return ((name.size() == word.size() || !isLowercase(name[word.size()])) &&
          name.startswith(word));
}

ObjCMethodFamily Selector::getMethodFamilyImpl(Selector sel) {
  IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return OMF_None;

  StringRef name = first->getName();
  if (sel.isUnarySelector()) {
    if (name == "autorelease") return OMF_autorelease;
    if (name == "dealloc") return OMF_dealloc;
    if (name == "finalize") return OMF_finalize;
    if (name == "release") return OMF_release;
    if (name == "retain") return OMF_retain;
    if (name == "retainCount") return OMF_retainCount;
    if (name == "self") return OMF_self;
    if (name == "initialize") return OMF_initialize;
  }

  if (name == "performSelector" || name == "performSelectorInBackground" ||
      name == "performSelectorOnMainThread")
    return OMF_performSelector;

  // The other method families may begin with a prefix of underscores.
  while (!name.empty() && name.front() == '_')
    name = name.substr(1);

  if (name.empty()) return OMF_None;
  switch (name.front()) {
  case 'a':
    if (startsWithWord(name, "alloc")) return OMF_alloc;
    break;
  case 'c':
    if (startsWithWord(name, "copy")) return OMF_copy;
    break;
  case 'i':
    if (startsWithWord(name, "init")) return OMF_init;
    break;
  case 'm':
    if (startsWithWord(name, "mutableCopy")) return OMF_mutableCopy;
    break;
  case 'n':
    if (startsWithWord(name, "new")) return OMF_new;
    break;
  default:
    break;
  }

  return OMF_None;
}

ObjCInstanceTypeFamily Selector::getInstTypeMethodFamily(Selector sel) {
  IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return OIT_None;

  StringRef name = first->getName();

  if (name.empty()) return OIT_None;
  switch (name.front()) {
    case 'a':
      if (startsWithWord(name, "array")) return OIT_Array;
      break;
    case 'd':
      if (startsWithWord(name, "default")) return OIT_ReturnsSelf;
      if (startsWithWord(name, "dictionary")) return OIT_Dictionary;
      break;
    case 's':
      if (startsWithWord(name, "shared")) return OIT_ReturnsSelf;
      if (startsWithWord(name, "standard")) return OIT_Singleton;
      break;
    case 'i':
      if (startsWithWord(name, "init")) return OIT_Init;
      break;
    default:
      break;
  }
  return OIT_None;
}

ObjCStringFormatFamily Selector::getStringFormatFamilyImpl(Selector sel) {
  IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return SFF_None;

  StringRef name = first->getName();

  switch (name.front()) {
    case 'a':
      if (name == "appendFormat") return SFF_NSString;
      break;

    case 'i':
      if (name == "initWithFormat") return SFF_NSString;
      break;

    case 'l':
      if (name == "localizedStringWithFormat") return SFF_NSString;
      break;

    case 's':
      if (name == "stringByAppendingFormat" ||
          name == "stringWithFormat") return SFF_NSString;
      break;
  }
  return SFF_None;
}

namespace {

struct SelectorTableImpl {
  llvm::FoldingSet<MultiKeywordSelector> Table;
  llvm::BumpPtrAllocator Allocator;
};

} // namespace

static SelectorTableImpl &getSelectorTableImpl(void *P) {
  return *static_cast<SelectorTableImpl*>(P);
}

SmallString<64>
SelectorTable::constructSetterName(StringRef Name) {
  SmallString<64> SetterName("set");
  SetterName += Name;
  SetterName[3] = toUppercase(SetterName[3]);
  return SetterName;
}

Selector
SelectorTable::constructSetterSelector(IdentifierTable &Idents,
                                       SelectorTable &SelTable,
                                       const IdentifierInfo *Name) {
  IdentifierInfo *SetterName =
    &Idents.get(constructSetterName(Name->getName()));
  return SelTable.getUnarySelector(SetterName);
}

std::string SelectorTable::getPropertyNameFromSetterSelector(Selector Sel) {
  StringRef Name = Sel.getNameForSlot(0);
  assert(Name.startswith("set") && "invalid setter name");
  return (Twine(toLowercase(Name[3])) + Name.drop_front(4)).str();
}

size_t SelectorTable::getTotalMemory() const {
  SelectorTableImpl &SelTabImpl = getSelectorTableImpl(Impl);
  return SelTabImpl.Allocator.getTotalMemory();
}

Selector SelectorTable::getSelector(unsigned nKeys, IdentifierInfo **IIV) {
  if (nKeys < 2)
    return Selector(IIV[0], nKeys);

  SelectorTableImpl &SelTabImpl = getSelectorTableImpl(Impl);

  // Unique selector, to guarantee there is one per name.
  llvm::FoldingSetNodeID ID;
  MultiKeywordSelector::Profile(ID, IIV, nKeys);

  void *InsertPos = nullptr;
  if (MultiKeywordSelector *SI =
        SelTabImpl.Table.FindNodeOrInsertPos(ID, InsertPos))
    return Selector(SI);

  // MultiKeywordSelector objects are not allocated with new because they have a
  // variable size array (for parameter types) at the end of them.
  unsigned Size = sizeof(MultiKeywordSelector) + nKeys*sizeof(IdentifierInfo *);
  MultiKeywordSelector *SI =
      (MultiKeywordSelector *)SelTabImpl.Allocator.Allocate(
          Size, alignof(MultiKeywordSelector));
  new (SI) MultiKeywordSelector(nKeys, IIV);
  SelTabImpl.Table.InsertNode(SI, InsertPos);
  return Selector(SI);
}

SelectorTable::SelectorTable() {
  Impl = new SelectorTableImpl();
}

SelectorTable::~SelectorTable() {
  delete &getSelectorTableImpl(Impl);
}

const char *clang::getOperatorSpelling(OverloadedOperatorKind Operator) {
  switch (Operator) {
  case OO_None:
  case NUM_OVERLOADED_OPERATORS:
    return nullptr;

#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
  case OO_##Name: return Spelling;
#include "clang/Basic/OperatorKinds.def"
  }

  llvm_unreachable("Invalid OverloadedOperatorKind!");
}

StringRef clang::getNullabilitySpelling(NullabilityKind kind,
                                        bool isContextSensitive) {
  switch (kind) {
  case NullabilityKind::NonNull:
    return isContextSensitive ? "nonnull" : "_Nonnull";

  case NullabilityKind::Nullable:
    return isContextSensitive ? "nullable" : "_Nullable";

  case NullabilityKind::Unspecified:
    return isContextSensitive ? "null_unspecified" : "_Null_unspecified";
  }
  llvm_unreachable("Unknown nullability kind.");
}
