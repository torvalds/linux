//===- IdentifierTable.h - Hash table for identifier lookup -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::IdentifierInfo, clang::IdentifierTable, and
/// clang::Selector interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_IDENTIFIERTABLE_H
#define LLVM_CLANG_BASIC_IDENTIFIERTABLE_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace clang {

class DeclarationName;
class DeclarationNameTable;
class IdentifierInfo;
class LangOptions;
class MultiKeywordSelector;
class SourceLocation;

/// A simple pair of identifier info and location.
using IdentifierLocPair = std::pair<IdentifierInfo *, SourceLocation>;

/// IdentifierInfo and other related classes are aligned to
/// 8 bytes so that DeclarationName can use the lower 3 bits
/// of a pointer to one of these classes.
enum { IdentifierInfoAlignment = 8 };

/// One of these records is kept for each identifier that
/// is lexed.  This contains information about whether the token was \#define'd,
/// is a language keyword, or if it is a front-end token of some sort (e.g. a
/// variable or function name).  The preprocessor keeps this information in a
/// set, and all tok::identifier tokens have a pointer to one of these.
/// It is aligned to 8 bytes because DeclarationName needs the lower 3 bits.
class alignas(IdentifierInfoAlignment) IdentifierInfo {
  friend class IdentifierTable;

  // Front-end token ID or tok::identifier.
  unsigned TokenID : 9;

  // ObjC keyword ('protocol' in '@protocol') or builtin (__builtin_inf).
  // First NUM_OBJC_KEYWORDS values are for Objective-C,
  // the remaining values are for builtins.
  unsigned ObjCOrBuiltinID : 13;

  // True if there is a #define for this.
  unsigned HasMacro : 1;

  // True if there was a #define for this.
  unsigned HadMacro : 1;

  // True if the identifier is a language extension.
  unsigned IsExtension : 1;

  // True if the identifier is a keyword in a newer or proposed Standard.
  unsigned IsFutureCompatKeyword : 1;

  // True if the identifier is poisoned.
  unsigned IsPoisoned : 1;

  // True if the identifier is a C++ operator keyword.
  unsigned IsCPPOperatorKeyword : 1;

  // Internal bit set by the member function RecomputeNeedsHandleIdentifier.
  // See comment about RecomputeNeedsHandleIdentifier for more info.
  unsigned NeedsHandleIdentifier : 1;

  // True if the identifier was loaded (at least partially) from an AST file.
  unsigned IsFromAST : 1;

  // True if the identifier has changed from the definition
  // loaded from an AST file.
  unsigned ChangedAfterLoad : 1;

  // True if the identifier's frontend information has changed from the
  // definition loaded from an AST file.
  unsigned FEChangedAfterLoad : 1;

  // True if revertTokenIDToIdentifier was called.
  unsigned RevertedTokenID : 1;

  // True if there may be additional information about
  // this identifier stored externally.
  unsigned OutOfDate : 1;

  // True if this is the 'import' contextual keyword.
  unsigned IsModulesImport : 1;

  // 29 bits left in a 64-bit word.

  // Managed by the language front-end.
  void *FETokenInfo = nullptr;

  llvm::StringMapEntry<IdentifierInfo *> *Entry = nullptr;

  IdentifierInfo()
      : TokenID(tok::identifier), ObjCOrBuiltinID(0), HasMacro(false),
        HadMacro(false), IsExtension(false), IsFutureCompatKeyword(false),
        IsPoisoned(false), IsCPPOperatorKeyword(false),
        NeedsHandleIdentifier(false), IsFromAST(false), ChangedAfterLoad(false),
        FEChangedAfterLoad(false), RevertedTokenID(false), OutOfDate(false),
        IsModulesImport(false) {}

public:
  IdentifierInfo(const IdentifierInfo &) = delete;
  IdentifierInfo &operator=(const IdentifierInfo &) = delete;
  IdentifierInfo(IdentifierInfo &&) = delete;
  IdentifierInfo &operator=(IdentifierInfo &&) = delete;

  /// Return true if this is the identifier for the specified string.
  ///
  /// This is intended to be used for string literals only: II->isStr("foo").
  template <std::size_t StrLen>
  bool isStr(const char (&Str)[StrLen]) const {
    return getLength() == StrLen-1 &&
           memcmp(getNameStart(), Str, StrLen-1) == 0;
  }

  /// Return true if this is the identifier for the specified StringRef.
  bool isStr(llvm::StringRef Str) const {
    llvm::StringRef ThisStr(getNameStart(), getLength());
    return ThisStr == Str;
  }

  /// Return the beginning of the actual null-terminated string for this
  /// identifier.
  const char *getNameStart() const { return Entry->getKeyData(); }

  /// Efficiently return the length of this identifier info.
  unsigned getLength() const { return Entry->getKeyLength(); }

  /// Return the actual identifier string.
  StringRef getName() const {
    return StringRef(getNameStart(), getLength());
  }

  /// Return true if this identifier is \#defined to some other value.
  /// \note The current definition may be in a module and not currently visible.
  bool hasMacroDefinition() const {
    return HasMacro;
  }
  void setHasMacroDefinition(bool Val) {
    if (HasMacro == Val) return;

    HasMacro = Val;
    if (Val) {
      NeedsHandleIdentifier = true;
      HadMacro = true;
    } else {
      RecomputeNeedsHandleIdentifier();
    }
  }
  /// Returns true if this identifier was \#defined to some value at any
  /// moment. In this case there should be an entry for the identifier in the
  /// macro history table in Preprocessor.
  bool hadMacroDefinition() const {
    return HadMacro;
  }

  /// If this is a source-language token (e.g. 'for'), this API
  /// can be used to cause the lexer to map identifiers to source-language
  /// tokens.
  tok::TokenKind getTokenID() const { return (tok::TokenKind)TokenID; }

  /// True if revertTokenIDToIdentifier() was called.
  bool hasRevertedTokenIDToIdentifier() const { return RevertedTokenID; }

  /// Revert TokenID to tok::identifier; used for GNU libstdc++ 4.2
  /// compatibility.
  ///
  /// TokenID is normally read-only but there are 2 instances where we revert it
  /// to tok::identifier for libstdc++ 4.2. Keep track of when this happens
  /// using this method so we can inform serialization about it.
  void revertTokenIDToIdentifier() {
    assert(TokenID != tok::identifier && "Already at tok::identifier");
    TokenID = tok::identifier;
    RevertedTokenID = true;
  }
  void revertIdentifierToTokenID(tok::TokenKind TK) {
    assert(TokenID == tok::identifier && "Should be at tok::identifier");
    TokenID = TK;
    RevertedTokenID = false;
  }

  /// Return the preprocessor keyword ID for this identifier.
  ///
  /// For example, "define" will return tok::pp_define.
  tok::PPKeywordKind getPPKeywordID() const;

  /// Return the Objective-C keyword ID for the this identifier.
  ///
  /// For example, 'class' will return tok::objc_class if ObjC is enabled.
  tok::ObjCKeywordKind getObjCKeywordID() const {
    if (ObjCOrBuiltinID < tok::NUM_OBJC_KEYWORDS)
      return tok::ObjCKeywordKind(ObjCOrBuiltinID);
    else
      return tok::objc_not_keyword;
  }
  void setObjCKeywordID(tok::ObjCKeywordKind ID) { ObjCOrBuiltinID = ID; }

  /// True if setNotBuiltin() was called.
  bool hasRevertedBuiltin() const {
    return ObjCOrBuiltinID == tok::NUM_OBJC_KEYWORDS;
  }

  /// Revert the identifier to a non-builtin identifier. We do this if
  /// the name of a known builtin library function is used to declare that
  /// function, but an unexpected type is specified.
  void revertBuiltin() {
    setBuiltinID(0);
  }

  /// Return a value indicating whether this is a builtin function.
  ///
  /// 0 is not-built-in. 1+ are specific builtin functions.
  unsigned getBuiltinID() const {
    if (ObjCOrBuiltinID >= tok::NUM_OBJC_KEYWORDS)
      return ObjCOrBuiltinID - tok::NUM_OBJC_KEYWORDS;
    else
      return 0;
  }
  void setBuiltinID(unsigned ID) {
    ObjCOrBuiltinID = ID + tok::NUM_OBJC_KEYWORDS;
    assert(ObjCOrBuiltinID - unsigned(tok::NUM_OBJC_KEYWORDS) == ID
           && "ID too large for field!");
  }

  unsigned getObjCOrBuiltinID() const { return ObjCOrBuiltinID; }
  void setObjCOrBuiltinID(unsigned ID) { ObjCOrBuiltinID = ID; }

  /// get/setExtension - Initialize information about whether or not this
  /// language token is an extension.  This controls extension warnings, and is
  /// only valid if a custom token ID is set.
  bool isExtensionToken() const { return IsExtension; }
  void setIsExtensionToken(bool Val) {
    IsExtension = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// is/setIsFutureCompatKeyword - Initialize information about whether or not
  /// this language token is a keyword in a newer or proposed Standard. This
  /// controls compatibility warnings, and is only true when not parsing the
  /// corresponding Standard. Once a compatibility problem has been diagnosed
  /// with this keyword, the flag will be cleared.
  bool isFutureCompatKeyword() const { return IsFutureCompatKeyword; }
  void setIsFutureCompatKeyword(bool Val) {
    IsFutureCompatKeyword = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// setIsPoisoned - Mark this identifier as poisoned.  After poisoning, the
  /// Preprocessor will emit an error every time this token is used.
  void setIsPoisoned(bool Value = true) {
    IsPoisoned = Value;
    if (Value)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Return true if this token has been poisoned.
  bool isPoisoned() const { return IsPoisoned; }

  /// isCPlusPlusOperatorKeyword/setIsCPlusPlusOperatorKeyword controls whether
  /// this identifier is a C++ alternate representation of an operator.
  void setIsCPlusPlusOperatorKeyword(bool Val = true) {
    IsCPPOperatorKeyword = Val;
  }
  bool isCPlusPlusOperatorKeyword() const { return IsCPPOperatorKeyword; }

  /// Return true if this token is a keyword in the specified language.
  bool isKeyword(const LangOptions &LangOpts) const;

  /// Return true if this token is a C++ keyword in the specified
  /// language.
  bool isCPlusPlusKeyword(const LangOptions &LangOpts) const;

  /// Get and set FETokenInfo. The language front-end is allowed to associate
  /// arbitrary metadata with this token.
  void *getFETokenInfo() const { return FETokenInfo; }
  void setFETokenInfo(void *T) { FETokenInfo = T; }

  /// Return true if the Preprocessor::HandleIdentifier must be called
  /// on a token of this identifier.
  ///
  /// If this returns false, we know that HandleIdentifier will not affect
  /// the token.
  bool isHandleIdentifierCase() const { return NeedsHandleIdentifier; }

  /// Return true if the identifier in its current state was loaded
  /// from an AST file.
  bool isFromAST() const { return IsFromAST; }

  void setIsFromAST() { IsFromAST = true; }

  /// Determine whether this identifier has changed since it was loaded
  /// from an AST file.
  bool hasChangedSinceDeserialization() const {
    return ChangedAfterLoad;
  }

  /// Note that this identifier has changed since it was loaded from
  /// an AST file.
  void setChangedSinceDeserialization() {
    ChangedAfterLoad = true;
  }

  /// Determine whether the frontend token information for this
  /// identifier has changed since it was loaded from an AST file.
  bool hasFETokenInfoChangedSinceDeserialization() const {
    return FEChangedAfterLoad;
  }

  /// Note that the frontend token information for this identifier has
  /// changed since it was loaded from an AST file.
  void setFETokenInfoChangedSinceDeserialization() {
    FEChangedAfterLoad = true;
  }

  /// Determine whether the information for this identifier is out of
  /// date with respect to the external source.
  bool isOutOfDate() const { return OutOfDate; }

  /// Set whether the information for this identifier is out of
  /// date with respect to the external source.
  void setOutOfDate(bool OOD) {
    OutOfDate = OOD;
    if (OOD)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Determine whether this is the contextual keyword \c import.
  bool isModulesImport() const { return IsModulesImport; }

  /// Set whether this identifier is the contextual keyword \c import.
  void setModulesImport(bool I) {
    IsModulesImport = I;
    if (I)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Return true if this identifier is an editor placeholder.
  ///
  /// Editor placeholders are produced by the code-completion engine and are
  /// represented as characters between '<#' and '#>' in the source code. An
  /// example of auto-completed call with a placeholder parameter is shown
  /// below:
  /// \code
  ///   function(<#int x#>);
  /// \endcode
  bool isEditorPlaceholder() const {
    return getName().startswith("<#") && getName().endswith("#>");
  }

  /// Provide less than operator for lexicographical sorting.
  bool operator<(const IdentifierInfo &RHS) const {
    return getName() < RHS.getName();
  }

private:
  /// The Preprocessor::HandleIdentifier does several special (but rare)
  /// things to identifiers of various sorts.  For example, it changes the
  /// \c for keyword token from tok::identifier to tok::for.
  ///
  /// This method is very tied to the definition of HandleIdentifier.  Any
  /// change to it should be reflected here.
  void RecomputeNeedsHandleIdentifier() {
    NeedsHandleIdentifier = isPoisoned() || hasMacroDefinition() ||
                            isExtensionToken() || isFutureCompatKeyword() ||
                            isOutOfDate() || isModulesImport();
  }
};

/// An RAII object for [un]poisoning an identifier within a scope.
///
/// \p II is allowed to be null, in which case objects of this type have
/// no effect.
class PoisonIdentifierRAIIObject {
  IdentifierInfo *const II;
  const bool OldValue;

public:
  PoisonIdentifierRAIIObject(IdentifierInfo *II, bool NewValue)
    : II(II), OldValue(II ? II->isPoisoned() : false) {
    if(II)
      II->setIsPoisoned(NewValue);
  }

  ~PoisonIdentifierRAIIObject() {
    if(II)
      II->setIsPoisoned(OldValue);
  }
};

/// An iterator that walks over all of the known identifiers
/// in the lookup table.
///
/// Since this iterator uses an abstract interface via virtual
/// functions, it uses an object-oriented interface rather than the
/// more standard C++ STL iterator interface. In this OO-style
/// iteration, the single function \c Next() provides dereference,
/// advance, and end-of-sequence checking in a single
/// operation. Subclasses of this iterator type will provide the
/// actual functionality.
class IdentifierIterator {
protected:
  IdentifierIterator() = default;

public:
  IdentifierIterator(const IdentifierIterator &) = delete;
  IdentifierIterator &operator=(const IdentifierIterator &) = delete;

  virtual ~IdentifierIterator();

  /// Retrieve the next string in the identifier table and
  /// advances the iterator for the following string.
  ///
  /// \returns The next string in the identifier table. If there is
  /// no such string, returns an empty \c StringRef.
  virtual StringRef Next() = 0;
};

/// Provides lookups to, and iteration over, IdentiferInfo objects.
class IdentifierInfoLookup {
public:
  virtual ~IdentifierInfoLookup();

  /// Return the IdentifierInfo for the specified named identifier.
  ///
  /// Unlike the version in IdentifierTable, this returns a pointer instead
  /// of a reference.  If the pointer is null then the IdentifierInfo cannot
  /// be found.
  virtual IdentifierInfo* get(StringRef Name) = 0;

  /// Retrieve an iterator into the set of all identifiers
  /// known to this identifier lookup source.
  ///
  /// This routine provides access to all of the identifiers known to
  /// the identifier lookup, allowing access to the contents of the
  /// identifiers without introducing the overhead of constructing
  /// IdentifierInfo objects for each.
  ///
  /// \returns A new iterator into the set of known identifiers. The
  /// caller is responsible for deleting this iterator.
  virtual IdentifierIterator *getIdentifiers();
};

/// Implements an efficient mapping from strings to IdentifierInfo nodes.
///
/// This has no other purpose, but this is an extremely performance-critical
/// piece of the code, as each occurrence of every identifier goes through
/// here when lexed.
class IdentifierTable {
  // Shark shows that using MallocAllocator is *much* slower than using this
  // BumpPtrAllocator!
  using HashTableTy = llvm::StringMap<IdentifierInfo *, llvm::BumpPtrAllocator>;
  HashTableTy HashTable;

  IdentifierInfoLookup* ExternalLookup;

public:
  /// Create the identifier table.
  explicit IdentifierTable(IdentifierInfoLookup *ExternalLookup = nullptr);

  /// Create the identifier table, populating it with info about the
  /// language keywords for the language specified by \p LangOpts.
  explicit IdentifierTable(const LangOptions &LangOpts,
                           IdentifierInfoLookup *ExternalLookup = nullptr);

  /// Set the external identifier lookup mechanism.
  void setExternalIdentifierLookup(IdentifierInfoLookup *IILookup) {
    ExternalLookup = IILookup;
  }

  /// Retrieve the external identifier lookup object, if any.
  IdentifierInfoLookup *getExternalIdentifierLookup() const {
    return ExternalLookup;
  }

  llvm::BumpPtrAllocator& getAllocator() {
    return HashTable.getAllocator();
  }

  /// Return the identifier token info for the specified named
  /// identifier.
  IdentifierInfo &get(StringRef Name) {
    auto &Entry = *HashTable.insert(std::make_pair(Name, nullptr)).first;

    IdentifierInfo *&II = Entry.second;
    if (II) return *II;

    // No entry; if we have an external lookup, look there first.
    if (ExternalLookup) {
      II = ExternalLookup->get(Name);
      if (II)
        return *II;
    }

    // Lookups failed, make a new IdentifierInfo.
    void *Mem = getAllocator().Allocate<IdentifierInfo>();
    II = new (Mem) IdentifierInfo();

    // Make sure getName() knows how to find the IdentifierInfo
    // contents.
    II->Entry = &Entry;

    return *II;
  }

  IdentifierInfo &get(StringRef Name, tok::TokenKind TokenCode) {
    IdentifierInfo &II = get(Name);
    II.TokenID = TokenCode;
    assert(II.TokenID == (unsigned) TokenCode && "TokenCode too large");
    return II;
  }

  /// Gets an IdentifierInfo for the given name without consulting
  ///        external sources.
  ///
  /// This is a version of get() meant for external sources that want to
  /// introduce or modify an identifier. If they called get(), they would
  /// likely end up in a recursion.
  IdentifierInfo &getOwn(StringRef Name) {
    auto &Entry = *HashTable.insert(std::make_pair(Name, nullptr)).first;

    IdentifierInfo *&II = Entry.second;
    if (II)
      return *II;

    // Lookups failed, make a new IdentifierInfo.
    void *Mem = getAllocator().Allocate<IdentifierInfo>();
    II = new (Mem) IdentifierInfo();

    // Make sure getName() knows how to find the IdentifierInfo
    // contents.
    II->Entry = &Entry;

    // If this is the 'import' contextual keyword, mark it as such.
    if (Name.equals("import"))
      II->setModulesImport(true);

    return *II;
  }

  using iterator = HashTableTy::const_iterator;
  using const_iterator = HashTableTy::const_iterator;

  iterator begin() const { return HashTable.begin(); }
  iterator end() const   { return HashTable.end(); }
  unsigned size() const  { return HashTable.size(); }

  /// Print some statistics to stderr that indicate how well the
  /// hashing is doing.
  void PrintStats() const;

  /// Populate the identifier table with info about the language keywords
  /// for the language specified by \p LangOpts.
  void AddKeywords(const LangOptions &LangOpts);
};

/// A family of Objective-C methods.
///
/// These families have no inherent meaning in the language, but are
/// nonetheless central enough in the existing implementations to
/// merit direct AST support.  While, in theory, arbitrary methods can
/// be considered to form families, we focus here on the methods
/// involving allocation and retain-count management, as these are the
/// most "core" and the most likely to be useful to diverse clients
/// without extra information.
///
/// Both selectors and actual method declarations may be classified
/// into families.  Method families may impose additional restrictions
/// beyond their selector name; for example, a method called '_init'
/// that returns void is not considered to be in the 'init' family
/// (but would be if it returned 'id').  It is also possible to
/// explicitly change or remove a method's family.  Therefore the
/// method's family should be considered the single source of truth.
enum ObjCMethodFamily {
  /// No particular method family.
  OMF_None,

  // Selectors in these families may have arbitrary arity, may be
  // written with arbitrary leading underscores, and may have
  // additional CamelCase "words" in their first selector chunk
  // following the family name.
  OMF_alloc,
  OMF_copy,
  OMF_init,
  OMF_mutableCopy,
  OMF_new,

  // These families are singletons consisting only of the nullary
  // selector with the given name.
  OMF_autorelease,
  OMF_dealloc,
  OMF_finalize,
  OMF_release,
  OMF_retain,
  OMF_retainCount,
  OMF_self,
  OMF_initialize,

  // performSelector families
  OMF_performSelector
};

/// Enough bits to store any enumerator in ObjCMethodFamily or
/// InvalidObjCMethodFamily.
enum { ObjCMethodFamilyBitWidth = 4 };

/// An invalid value of ObjCMethodFamily.
enum { InvalidObjCMethodFamily = (1 << ObjCMethodFamilyBitWidth) - 1 };

/// A family of Objective-C methods.
///
/// These are family of methods whose result type is initially 'id', but
/// but are candidate for the result type to be changed to 'instancetype'.
enum ObjCInstanceTypeFamily {
  OIT_None,
  OIT_Array,
  OIT_Dictionary,
  OIT_Singleton,
  OIT_Init,
  OIT_ReturnsSelf
};

enum ObjCStringFormatFamily {
  SFF_None,
  SFF_NSString,
  SFF_CFString
};

/// Smart pointer class that efficiently represents Objective-C method
/// names.
///
/// This class will either point to an IdentifierInfo or a
/// MultiKeywordSelector (which is private). This enables us to optimize
/// selectors that take no arguments and selectors that take 1 argument, which
/// accounts for 78% of all selectors in Cocoa.h.
class Selector {
  friend class Diagnostic;
  friend class SelectorTable; // only the SelectorTable can create these
  friend class DeclarationName; // and the AST's DeclarationName.

  enum IdentifierInfoFlag {
    // Empty selector = 0. Note that these enumeration values must
    // correspond to the enumeration values of DeclarationName::StoredNameKind
    ZeroArg  = 0x01,
    OneArg   = 0x02,
    MultiArg = 0x07,
    ArgFlags = 0x07
  };

  /// A pointer to the MultiKeywordSelector or IdentifierInfo. We use the low
  /// three bits of InfoPtr to store an IdentifierInfoFlag. Note that in any
  /// case IdentifierInfo and MultiKeywordSelector are already aligned to
  /// 8 bytes even on 32 bits archs because of DeclarationName.
  uintptr_t InfoPtr = 0;

  Selector(IdentifierInfo *II, unsigned nArgs) {
    InfoPtr = reinterpret_cast<uintptr_t>(II);
    assert((InfoPtr & ArgFlags) == 0 &&"Insufficiently aligned IdentifierInfo");
    assert(nArgs < 2 && "nArgs not equal to 0/1");
    InfoPtr |= nArgs+1;
  }

  Selector(MultiKeywordSelector *SI) {
    InfoPtr = reinterpret_cast<uintptr_t>(SI);
    assert((InfoPtr & ArgFlags) == 0 &&"Insufficiently aligned IdentifierInfo");
    InfoPtr |= MultiArg;
  }

  IdentifierInfo *getAsIdentifierInfo() const {
    if (getIdentifierInfoFlag() < MultiArg)
      return reinterpret_cast<IdentifierInfo *>(InfoPtr & ~ArgFlags);
    return nullptr;
  }

  MultiKeywordSelector *getMultiKeywordSelector() const {
    return reinterpret_cast<MultiKeywordSelector *>(InfoPtr & ~ArgFlags);
  }

  unsigned getIdentifierInfoFlag() const {
    return InfoPtr & ArgFlags;
  }

  static ObjCMethodFamily getMethodFamilyImpl(Selector sel);

  static ObjCStringFormatFamily getStringFormatFamilyImpl(Selector sel);

public:
  /// The default ctor should only be used when creating data structures that
  ///  will contain selectors.
  Selector() = default;
  explicit Selector(uintptr_t V) : InfoPtr(V) {}

  /// operator==/!= - Indicate whether the specified selectors are identical.
  bool operator==(Selector RHS) const {
    return InfoPtr == RHS.InfoPtr;
  }
  bool operator!=(Selector RHS) const {
    return InfoPtr != RHS.InfoPtr;
  }

  void *getAsOpaquePtr() const {
    return reinterpret_cast<void*>(InfoPtr);
  }

  /// Determine whether this is the empty selector.
  bool isNull() const { return InfoPtr == 0; }

  // Predicates to identify the selector type.
  bool isKeywordSelector() const {
    return getIdentifierInfoFlag() != ZeroArg;
  }

  bool isUnarySelector() const {
    return getIdentifierInfoFlag() == ZeroArg;
  }

  unsigned getNumArgs() const;

  /// Retrieve the identifier at a given position in the selector.
  ///
  /// Note that the identifier pointer returned may be NULL. Clients that only
  /// care about the text of the identifier string, and not the specific,
  /// uniqued identifier pointer, should use \c getNameForSlot(), which returns
  /// an empty string when the identifier pointer would be NULL.
  ///
  /// \param argIndex The index for which we want to retrieve the identifier.
  /// This index shall be less than \c getNumArgs() unless this is a keyword
  /// selector, in which case 0 is the only permissible value.
  ///
  /// \returns the uniqued identifier for this slot, or NULL if this slot has
  /// no corresponding identifier.
  IdentifierInfo *getIdentifierInfoForSlot(unsigned argIndex) const;

  /// Retrieve the name at a given position in the selector.
  ///
  /// \param argIndex The index for which we want to retrieve the name.
  /// This index shall be less than \c getNumArgs() unless this is a keyword
  /// selector, in which case 0 is the only permissible value.
  ///
  /// \returns the name for this slot, which may be the empty string if no
  /// name was supplied.
  StringRef getNameForSlot(unsigned argIndex) const;

  /// Derive the full selector name (e.g. "foo:bar:") and return
  /// it as an std::string.
  std::string getAsString() const;

  /// Prints the full selector name (e.g. "foo:bar:").
  void print(llvm::raw_ostream &OS) const;

  void dump() const;

  /// Derive the conventional family of this method.
  ObjCMethodFamily getMethodFamily() const {
    return getMethodFamilyImpl(*this);
  }

  ObjCStringFormatFamily getStringFormatFamily() const {
    return getStringFormatFamilyImpl(*this);
  }

  static Selector getEmptyMarker() {
    return Selector(uintptr_t(-1));
  }

  static Selector getTombstoneMarker() {
    return Selector(uintptr_t(-2));
  }

  static ObjCInstanceTypeFamily getInstTypeMethodFamily(Selector sel);
};

/// This table allows us to fully hide how we implement
/// multi-keyword caching.
class SelectorTable {
  // Actually a SelectorTableImpl
  void *Impl;

public:
  SelectorTable();
  SelectorTable(const SelectorTable &) = delete;
  SelectorTable &operator=(const SelectorTable &) = delete;
  ~SelectorTable();

  /// Can create any sort of selector.
  ///
  /// \p NumArgs indicates whether this is a no argument selector "foo", a
  /// single argument selector "foo:" or multi-argument "foo:bar:".
  Selector getSelector(unsigned NumArgs, IdentifierInfo **IIV);

  Selector getUnarySelector(IdentifierInfo *ID) {
    return Selector(ID, 1);
  }

  Selector getNullarySelector(IdentifierInfo *ID) {
    return Selector(ID, 0);
  }

  /// Return the total amount of memory allocated for managing selectors.
  size_t getTotalMemory() const;

  /// Return the default setter name for the given identifier.
  ///
  /// This is "set" + \p Name where the initial character of \p Name
  /// has been capitalized.
  static SmallString<64> constructSetterName(StringRef Name);

  /// Return the default setter selector for the given identifier.
  ///
  /// This is "set" + \p Name where the initial character of \p Name
  /// has been capitalized.
  static Selector constructSetterSelector(IdentifierTable &Idents,
                                          SelectorTable &SelTable,
                                          const IdentifierInfo *Name);

  /// Return the property name for the given setter selector.
  static std::string getPropertyNameFromSetterSelector(Selector Sel);
};

namespace detail {

/// DeclarationNameExtra is used as a base of various uncommon special names.
/// This class is needed since DeclarationName has not enough space to store
/// the kind of every possible names. Therefore the kind of common names is
/// stored directly in DeclarationName, and the kind of uncommon names is
/// stored in DeclarationNameExtra. It is aligned to 8 bytes because
/// DeclarationName needs the lower 3 bits to store the kind of common names.
/// DeclarationNameExtra is tightly coupled to DeclarationName and any change
/// here is very likely to require changes in DeclarationName(Table).
class alignas(IdentifierInfoAlignment) DeclarationNameExtra {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

protected:
  /// The kind of "extra" information stored in the DeclarationName. See
  /// @c ExtraKindOrNumArgs for an explanation of how these enumerator values
  /// are used. Note that DeclarationName depends on the numerical values
  /// of the enumerators in this enum. See DeclarationName::StoredNameKind
  /// for more info.
  enum ExtraKind {
    CXXDeductionGuideName,
    CXXLiteralOperatorName,
    CXXUsingDirective,
    ObjCMultiArgSelector
  };

  /// ExtraKindOrNumArgs has one of the following meaning:
  ///  * The kind of an uncommon C++ special name. This DeclarationNameExtra
  ///    is in this case in fact either a CXXDeductionGuideNameExtra or
  ///    a CXXLiteralOperatorIdName.
  ///
  ///  * It may be also name common to C++ using-directives (CXXUsingDirective),
  ///
  ///  * Otherwise it is ObjCMultiArgSelector+NumArgs, where NumArgs is
  ///    the number of arguments in the Objective-C selector, in which
  ///    case the DeclarationNameExtra is also a MultiKeywordSelector.
  unsigned ExtraKindOrNumArgs;

  DeclarationNameExtra(ExtraKind Kind) : ExtraKindOrNumArgs(Kind) {}
  DeclarationNameExtra(unsigned NumArgs)
      : ExtraKindOrNumArgs(ObjCMultiArgSelector + NumArgs) {}

  /// Return the corresponding ExtraKind.
  ExtraKind getKind() const {
    return static_cast<ExtraKind>(ExtraKindOrNumArgs >
                                          (unsigned)ObjCMultiArgSelector
                                      ? (unsigned)ObjCMultiArgSelector
                                      : ExtraKindOrNumArgs);
  }

  /// Return the number of arguments in an ObjC selector. Only valid when this
  /// is indeed an ObjCMultiArgSelector.
  unsigned getNumArgs() const {
    assert(ExtraKindOrNumArgs >= (unsigned)ObjCMultiArgSelector &&
           "getNumArgs called but this is not an ObjC selector!");
    return ExtraKindOrNumArgs - (unsigned)ObjCMultiArgSelector;
  }
};

} // namespace detail

}  // namespace clang

namespace llvm {

/// Define DenseMapInfo so that Selectors can be used as keys in DenseMap and
/// DenseSets.
template <>
struct DenseMapInfo<clang::Selector> {
  static clang::Selector getEmptyKey() {
    return clang::Selector::getEmptyMarker();
  }

  static clang::Selector getTombstoneKey() {
    return clang::Selector::getTombstoneMarker();
  }

  static unsigned getHashValue(clang::Selector S);

  static bool isEqual(clang::Selector LHS, clang::Selector RHS) {
    return LHS == RHS;
  }
};

template <>
struct isPodLike<clang::Selector> { static const bool value = true; };

template<>
struct PointerLikeTypeTraits<clang::Selector> {
  static const void *getAsVoidPointer(clang::Selector P) {
    return P.getAsOpaquePtr();
  }

  static clang::Selector getFromVoidPointer(const void *P) {
    return clang::Selector(reinterpret_cast<uintptr_t>(P));
  }

  enum { NumLowBitsAvailable = 0 };
};

// Provide PointerLikeTypeTraits for IdentifierInfo pointers, which
// are not guaranteed to be 8-byte aligned.
template<>
struct PointerLikeTypeTraits<clang::IdentifierInfo*> {
  static void *getAsVoidPointer(clang::IdentifierInfo* P) {
    return P;
  }

  static clang::IdentifierInfo *getFromVoidPointer(void *P) {
    return static_cast<clang::IdentifierInfo*>(P);
  }

  enum { NumLowBitsAvailable = 1 };
};

template<>
struct PointerLikeTypeTraits<const clang::IdentifierInfo*> {
  static const void *getAsVoidPointer(const clang::IdentifierInfo* P) {
    return P;
  }

  static const clang::IdentifierInfo *getFromVoidPointer(const void *P) {
    return static_cast<const clang::IdentifierInfo*>(P);
  }

  enum { NumLowBitsAvailable = 1 };
};

} // namespace llvm

#endif // LLVM_CLANG_BASIC_IDENTIFIERTABLE_H
