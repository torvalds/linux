//===--- NSAPI.h - NSFoundation APIs ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_NSAPI_H
#define LLVM_CLANG_AST_NSAPI_H

#include "clang/Basic/IdentifierTable.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"

namespace clang {
  class ASTContext;
  class ObjCInterfaceDecl;
  class QualType;
  class Expr;

// Provides info and caches identifiers/selectors for NSFoundation API.
class NSAPI {
public:
  explicit NSAPI(ASTContext &Ctx);

  ASTContext &getASTContext() const { return Ctx; }

  enum NSClassIdKindKind {
    ClassId_NSObject,
    ClassId_NSString,
    ClassId_NSArray,
    ClassId_NSMutableArray,
    ClassId_NSDictionary,
    ClassId_NSMutableDictionary,
    ClassId_NSNumber,
    ClassId_NSMutableSet,
    ClassId_NSMutableOrderedSet,
    ClassId_NSValue
  };
  static const unsigned NumClassIds = 10;

  enum NSStringMethodKind {
    NSStr_stringWithString,
    NSStr_stringWithUTF8String,
    NSStr_stringWithCStringEncoding,
    NSStr_stringWithCString,
    NSStr_initWithString,
    NSStr_initWithUTF8String
  };
  static const unsigned NumNSStringMethods = 6;

  IdentifierInfo *getNSClassId(NSClassIdKindKind K) const;

  /// The Objective-C NSString selectors.
  Selector getNSStringSelector(NSStringMethodKind MK) const;

  /// Return NSStringMethodKind if \param Sel is such a selector.
  Optional<NSStringMethodKind> getNSStringMethodKind(Selector Sel) const;

  /// Returns true if the expression \param E is a reference of
  /// "NSUTF8StringEncoding" enum constant.
  bool isNSUTF8StringEncodingConstant(const Expr *E) const {
    return isObjCEnumerator(E, "NSUTF8StringEncoding", NSUTF8StringEncodingId);
  }

  /// Returns true if the expression \param E is a reference of
  /// "NSASCIIStringEncoding" enum constant.
  bool isNSASCIIStringEncodingConstant(const Expr *E) const {
    return isObjCEnumerator(E, "NSASCIIStringEncoding",NSASCIIStringEncodingId);
  }

  /// Enumerates the NSArray/NSMutableArray methods used to generate
  /// literals and to apply some checks.
  enum NSArrayMethodKind {
    NSArr_array,
    NSArr_arrayWithArray,
    NSArr_arrayWithObject,
    NSArr_arrayWithObjects,
    NSArr_arrayWithObjectsCount,
    NSArr_initWithArray,
    NSArr_initWithObjects,
    NSArr_objectAtIndex,
    NSMutableArr_replaceObjectAtIndex,
    NSMutableArr_addObject,
    NSMutableArr_insertObjectAtIndex,
    NSMutableArr_setObjectAtIndexedSubscript
  };
  static const unsigned NumNSArrayMethods = 12;

  /// The Objective-C NSArray selectors.
  Selector getNSArraySelector(NSArrayMethodKind MK) const;

  /// Return NSArrayMethodKind if \p Sel is such a selector.
  Optional<NSArrayMethodKind> getNSArrayMethodKind(Selector Sel);

  /// Enumerates the NSDictionary/NSMutableDictionary methods used
  /// to generate literals and to apply some checks.
  enum NSDictionaryMethodKind {
    NSDict_dictionary,
    NSDict_dictionaryWithDictionary,
    NSDict_dictionaryWithObjectForKey,
    NSDict_dictionaryWithObjectsForKeys,
    NSDict_dictionaryWithObjectsForKeysCount,
    NSDict_dictionaryWithObjectsAndKeys,
    NSDict_initWithDictionary,
    NSDict_initWithObjectsAndKeys,
    NSDict_initWithObjectsForKeys,
    NSDict_objectForKey,
    NSMutableDict_setObjectForKey,
    NSMutableDict_setObjectForKeyedSubscript,
    NSMutableDict_setValueForKey
  };
  static const unsigned NumNSDictionaryMethods = 13;

  /// The Objective-C NSDictionary selectors.
  Selector getNSDictionarySelector(NSDictionaryMethodKind MK) const;

  /// Return NSDictionaryMethodKind if \p Sel is such a selector.
  Optional<NSDictionaryMethodKind> getNSDictionaryMethodKind(Selector Sel);

  /// Enumerates the NSMutableSet/NSOrderedSet methods used
  /// to apply some checks.
  enum NSSetMethodKind {
    NSMutableSet_addObject,
    NSOrderedSet_insertObjectAtIndex,
    NSOrderedSet_setObjectAtIndex,
    NSOrderedSet_setObjectAtIndexedSubscript,
    NSOrderedSet_replaceObjectAtIndexWithObject
  };
  static const unsigned NumNSSetMethods = 5;

  /// The Objective-C NSSet selectors.
  Selector getNSSetSelector(NSSetMethodKind MK) const;

  /// Return NSSetMethodKind if \p Sel is such a selector.
  Optional<NSSetMethodKind> getNSSetMethodKind(Selector Sel);

  /// Returns selector for "objectForKeyedSubscript:".
  Selector getObjectForKeyedSubscriptSelector() const {
    return getOrInitSelector(StringRef("objectForKeyedSubscript"),
                             objectForKeyedSubscriptSel);
  }

  /// Returns selector for "objectAtIndexedSubscript:".
  Selector getObjectAtIndexedSubscriptSelector() const {
    return getOrInitSelector(StringRef("objectAtIndexedSubscript"),
                             objectAtIndexedSubscriptSel);
  }

  /// Returns selector for "setObject:forKeyedSubscript".
  Selector getSetObjectForKeyedSubscriptSelector() const {
    StringRef Ids[] = { "setObject", "forKeyedSubscript" };
    return getOrInitSelector(Ids, setObjectForKeyedSubscriptSel);
  }

  /// Returns selector for "setObject:atIndexedSubscript".
  Selector getSetObjectAtIndexedSubscriptSelector() const {
    StringRef Ids[] = { "setObject", "atIndexedSubscript" };
    return getOrInitSelector(Ids, setObjectAtIndexedSubscriptSel);
  }

  /// Returns selector for "isEqual:".
  Selector getIsEqualSelector() const {
    return getOrInitSelector(StringRef("isEqual"), isEqualSel);
  }

  Selector getNewSelector() const {
    return getOrInitNullarySelector("new", NewSel);
  }

  Selector getInitSelector() const {
    return getOrInitNullarySelector("init", InitSel);
  }

  /// Enumerates the NSNumber methods used to generate literals.
  enum NSNumberLiteralMethodKind {
    NSNumberWithChar,
    NSNumberWithUnsignedChar,
    NSNumberWithShort,
    NSNumberWithUnsignedShort,
    NSNumberWithInt,
    NSNumberWithUnsignedInt,
    NSNumberWithLong,
    NSNumberWithUnsignedLong,
    NSNumberWithLongLong,
    NSNumberWithUnsignedLongLong,
    NSNumberWithFloat,
    NSNumberWithDouble,
    NSNumberWithBool,
    NSNumberWithInteger,
    NSNumberWithUnsignedInteger
  };
  static const unsigned NumNSNumberLiteralMethods = 15;

  /// The Objective-C NSNumber selectors used to create NSNumber literals.
  /// \param Instance if true it will return the selector for the init* method
  /// otherwise it will return the selector for the number* method.
  Selector getNSNumberLiteralSelector(NSNumberLiteralMethodKind MK,
                                      bool Instance) const;

  bool isNSNumberLiteralSelector(NSNumberLiteralMethodKind MK,
                                 Selector Sel) const {
    return Sel == getNSNumberLiteralSelector(MK, false) ||
           Sel == getNSNumberLiteralSelector(MK, true);
  }

  /// Return NSNumberLiteralMethodKind if \p Sel is such a selector.
  Optional<NSNumberLiteralMethodKind>
      getNSNumberLiteralMethodKind(Selector Sel) const;

  /// Determine the appropriate NSNumber factory method kind for a
  /// literal of the given type.
  Optional<NSNumberLiteralMethodKind>
      getNSNumberFactoryMethodKind(QualType T) const;

  /// Returns true if \param T is a typedef of "BOOL" in objective-c.
  bool isObjCBOOLType(QualType T) const;
  /// Returns true if \param T is a typedef of "NSInteger" in objective-c.
  bool isObjCNSIntegerType(QualType T) const;
  /// Returns true if \param T is a typedef of "NSUInteger" in objective-c.
  bool isObjCNSUIntegerType(QualType T) const;
  /// Returns one of NSIntegral typedef names if \param T is a typedef
  /// of that name in objective-c.
  StringRef GetNSIntegralKind(QualType T) const;

  /// Returns \c true if \p Id is currently defined as a macro.
  bool isMacroDefined(StringRef Id) const;

  /// Returns \c true if \p InterfaceDecl is subclass of \p NSClassKind
  bool isSubclassOfNSClass(ObjCInterfaceDecl *InterfaceDecl,
                           NSClassIdKindKind NSClassKind) const;

private:
  bool isObjCTypedef(QualType T, StringRef name, IdentifierInfo *&II) const;
  bool isObjCEnumerator(const Expr *E,
                        StringRef name, IdentifierInfo *&II) const;
  Selector getOrInitSelector(ArrayRef<StringRef> Ids, Selector &Sel) const;
  Selector getOrInitNullarySelector(StringRef Id, Selector &Sel) const;

  ASTContext &Ctx;

  mutable IdentifierInfo *ClassIds[NumClassIds];

  mutable Selector NSStringSelectors[NumNSStringMethods];

  /// The selectors for Objective-C NSArray methods.
  mutable Selector NSArraySelectors[NumNSArrayMethods];

  /// The selectors for Objective-C NSDictionary methods.
  mutable Selector NSDictionarySelectors[NumNSDictionaryMethods];

  /// The selectors for Objective-C NSSet methods.
  mutable Selector NSSetSelectors[NumNSSetMethods];

  /// The Objective-C NSNumber selectors used to create NSNumber literals.
  mutable Selector NSNumberClassSelectors[NumNSNumberLiteralMethods];
  mutable Selector NSNumberInstanceSelectors[NumNSNumberLiteralMethods];

  mutable Selector objectForKeyedSubscriptSel, objectAtIndexedSubscriptSel,
                   setObjectForKeyedSubscriptSel,setObjectAtIndexedSubscriptSel,
                   isEqualSel, InitSel, NewSel;

  mutable IdentifierInfo *BOOLId, *NSIntegerId, *NSUIntegerId;
  mutable IdentifierInfo *NSASCIIStringEncodingId, *NSUTF8StringEncodingId;
};

}  // end namespace clang

#endif // LLVM_CLANG_AST_NSAPI_H
