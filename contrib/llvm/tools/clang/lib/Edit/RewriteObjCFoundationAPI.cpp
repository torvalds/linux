//===--- RewriteObjCFoundationAPI.cpp - Foundation API Rewriter -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Rewrites legacy method calls to modern syntax.
//
//===----------------------------------------------------------------------===//

#include "clang/Edit/Rewriters.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/ParentMap.h"
#include "clang/Edit/Commit.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace edit;

static bool checkForLiteralCreation(const ObjCMessageExpr *Msg,
                                    IdentifierInfo *&ClassId,
                                    const LangOptions &LangOpts) {
  if (!Msg || Msg->isImplicit() || !Msg->getMethodDecl())
    return false;

  const ObjCInterfaceDecl *Receiver = Msg->getReceiverInterface();
  if (!Receiver)
    return false;
  ClassId = Receiver->getIdentifier();

  if (Msg->getReceiverKind() == ObjCMessageExpr::Class)
    return true;

  // When in ARC mode we also convert "[[.. alloc] init]" messages to literals,
  // since the change from +1 to +0 will be handled fine by ARC.
  if (LangOpts.ObjCAutoRefCount) {
    if (Msg->getReceiverKind() == ObjCMessageExpr::Instance) {
      if (const ObjCMessageExpr *Rec = dyn_cast<ObjCMessageExpr>(
                           Msg->getInstanceReceiver()->IgnoreParenImpCasts())) {
        if (Rec->getMethodFamily() == OMF_alloc)
          return true;
      }
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteObjCRedundantCallWithLiteral.
//===----------------------------------------------------------------------===//

bool edit::rewriteObjCRedundantCallWithLiteral(const ObjCMessageExpr *Msg,
                                              const NSAPI &NS, Commit &commit) {
  IdentifierInfo *II = nullptr;
  if (!checkForLiteralCreation(Msg, II, NS.getASTContext().getLangOpts()))
    return false;
  if (Msg->getNumArgs() != 1)
    return false;

  const Expr *Arg = Msg->getArg(0)->IgnoreParenImpCasts();
  Selector Sel = Msg->getSelector();

  if ((isa<ObjCStringLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSString) == II &&
       (NS.getNSStringSelector(NSAPI::NSStr_stringWithString) == Sel ||
        NS.getNSStringSelector(NSAPI::NSStr_initWithString) == Sel))   ||

      (isa<ObjCArrayLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSArray) == II &&
       (NS.getNSArraySelector(NSAPI::NSArr_arrayWithArray) == Sel ||
        NS.getNSArraySelector(NSAPI::NSArr_initWithArray) == Sel))     ||

      (isa<ObjCDictionaryLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSDictionary) == II &&
       (NS.getNSDictionarySelector(
                              NSAPI::NSDict_dictionaryWithDictionary) == Sel ||
        NS.getNSDictionarySelector(NSAPI::NSDict_initWithDictionary) == Sel))) {

    commit.replaceWithInner(Msg->getSourceRange(),
                           Msg->getArg(0)->getSourceRange());
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToObjCSubscriptSyntax.
//===----------------------------------------------------------------------===//

/// Check for classes that accept 'objectForKey:' (or the other selectors
/// that the migrator handles) but return their instances as 'id', resulting
/// in the compiler resolving 'objectForKey:' as the method from NSDictionary.
///
/// When checking if we can convert to subscripting syntax, check whether
/// the receiver is a result of a class method from a hardcoded list of
/// such classes. In such a case return the specific class as the interface
/// of the receiver.
///
/// FIXME: Remove this when these classes start using 'instancetype'.
static const ObjCInterfaceDecl *
maybeAdjustInterfaceForSubscriptingCheck(const ObjCInterfaceDecl *IFace,
                                         const Expr *Receiver,
                                         ASTContext &Ctx) {
  assert(IFace && Receiver);

  // If the receiver has type 'id'...
  if (!Ctx.isObjCIdType(Receiver->getType().getUnqualifiedType()))
    return IFace;

  const ObjCMessageExpr *
    InnerMsg = dyn_cast<ObjCMessageExpr>(Receiver->IgnoreParenCasts());
  if (!InnerMsg)
    return IFace;

  QualType ClassRec;
  switch (InnerMsg->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
  case ObjCMessageExpr::SuperInstance:
    return IFace;

  case ObjCMessageExpr::Class:
    ClassRec = InnerMsg->getClassReceiver();
    break;
  case ObjCMessageExpr::SuperClass:
    ClassRec = InnerMsg->getSuperType();
    break;
  }

  if (ClassRec.isNull())
    return IFace;

  // ...and it is the result of a class message...

  const ObjCObjectType *ObjTy = ClassRec->getAs<ObjCObjectType>();
  if (!ObjTy)
    return IFace;
  const ObjCInterfaceDecl *OID = ObjTy->getInterface();

  // ...and the receiving class is NSMapTable or NSLocale, return that
  // class as the receiving interface.
  if (OID->getName() == "NSMapTable" ||
      OID->getName() == "NSLocale")
    return OID;

  return IFace;
}

static bool canRewriteToSubscriptSyntax(const ObjCInterfaceDecl *&IFace,
                                        const ObjCMessageExpr *Msg,
                                        ASTContext &Ctx,
                                        Selector subscriptSel) {
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;
  IFace = maybeAdjustInterfaceForSubscriptingCheck(IFace, Rec, Ctx);

  if (const ObjCMethodDecl *MD = IFace->lookupInstanceMethod(subscriptSel)) {
    if (!MD->isUnavailable())
      return true;
  }
  return false;
}

static bool subscriptOperatorNeedsParens(const Expr *FullExpr);

static void maybePutParensOnReceiver(const Expr *Receiver, Commit &commit) {
  if (subscriptOperatorNeedsParens(Receiver)) {
    SourceRange RecRange = Receiver->getSourceRange();
    commit.insertWrap("(", RecRange, ")");
  }
}

static bool rewriteToSubscriptGetCommon(const ObjCMessageExpr *Msg,
                                        Commit &commit) {
  if (Msg->getNumArgs() != 1)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange ArgRange = Msg->getArg(0)->getSourceRange();

  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       ArgRange.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(SourceRange(ArgRange.getBegin(), MsgRange.getEnd()),
                         ArgRange);
  commit.insertWrap("[", ArgRange, "]");
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

static bool rewriteToArraySubscriptGet(const ObjCInterfaceDecl *IFace,
                                       const ObjCMessageExpr *Msg,
                                       const NSAPI &NS,
                                       Commit &commit) {
  if (!canRewriteToSubscriptSyntax(IFace, Msg, NS.getASTContext(),
                                   NS.getObjectAtIndexedSubscriptSelector()))
    return false;
  return rewriteToSubscriptGetCommon(Msg, commit);
}

static bool rewriteToDictionarySubscriptGet(const ObjCInterfaceDecl *IFace,
                                            const ObjCMessageExpr *Msg,
                                            const NSAPI &NS,
                                            Commit &commit) {
  if (!canRewriteToSubscriptSyntax(IFace, Msg, NS.getASTContext(),
                                  NS.getObjectForKeyedSubscriptSelector()))
    return false;
  return rewriteToSubscriptGetCommon(Msg, commit);
}

static bool rewriteToArraySubscriptSet(const ObjCInterfaceDecl *IFace,
                                       const ObjCMessageExpr *Msg,
                                       const NSAPI &NS,
                                       Commit &commit) {
  if (!canRewriteToSubscriptSyntax(IFace, Msg, NS.getASTContext(),
                                   NS.getSetObjectAtIndexedSubscriptSelector()))
    return false;

  if (Msg->getNumArgs() != 2)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange Arg0Range = Msg->getArg(0)->getSourceRange();
  SourceRange Arg1Range = Msg->getArg(1)->getSourceRange();

  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       Arg0Range.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(CharSourceRange::getCharRange(Arg0Range.getBegin(),
                                                       Arg1Range.getBegin()),
                         CharSourceRange::getTokenRange(Arg0Range));
  commit.replaceWithInner(SourceRange(Arg1Range.getBegin(), MsgRange.getEnd()),
                         Arg1Range);
  commit.insertWrap("[", CharSourceRange::getCharRange(Arg0Range.getBegin(),
                                                       Arg1Range.getBegin()),
                    "] = ");
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

static bool rewriteToDictionarySubscriptSet(const ObjCInterfaceDecl *IFace,
                                            const ObjCMessageExpr *Msg,
                                            const NSAPI &NS,
                                            Commit &commit) {
  if (!canRewriteToSubscriptSyntax(IFace, Msg, NS.getASTContext(),
                                   NS.getSetObjectForKeyedSubscriptSelector()))
    return false;

  if (Msg->getNumArgs() != 2)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange Arg0Range = Msg->getArg(0)->getSourceRange();
  SourceRange Arg1Range = Msg->getArg(1)->getSourceRange();

  SourceLocation LocBeforeVal = Arg0Range.getBegin();
  commit.insertBefore(LocBeforeVal, "] = ");
  commit.insertFromRange(LocBeforeVal, Arg1Range, /*afterToken=*/false,
                         /*beforePreviousInsertions=*/true);
  commit.insertBefore(LocBeforeVal, "[");
  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       Arg0Range.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(SourceRange(Arg0Range.getBegin(), MsgRange.getEnd()),
                         Arg0Range);
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

bool edit::rewriteToObjCSubscriptSyntax(const ObjCMessageExpr *Msg,
                                        const NSAPI &NS, Commit &commit) {
  if (!Msg || Msg->isImplicit() ||
      Msg->getReceiverKind() != ObjCMessageExpr::Instance)
    return false;
  const ObjCMethodDecl *Method = Msg->getMethodDecl();
  if (!Method)
    return false;

  const ObjCInterfaceDecl *IFace =
      NS.getASTContext().getObjContainingInterface(Method);
  if (!IFace)
    return false;
  Selector Sel = Msg->getSelector();

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_objectAtIndex))
    return rewriteToArraySubscriptGet(IFace, Msg, NS, commit);

  if (Sel == NS.getNSDictionarySelector(NSAPI::NSDict_objectForKey))
    return rewriteToDictionarySubscriptGet(IFace, Msg, NS, commit);

  if (Msg->getNumArgs() != 2)
    return false;

  if (Sel == NS.getNSArraySelector(NSAPI::NSMutableArr_replaceObjectAtIndex))
    return rewriteToArraySubscriptSet(IFace, Msg, NS, commit);

  if (Sel == NS.getNSDictionarySelector(NSAPI::NSMutableDict_setObjectForKey))
    return rewriteToDictionarySubscriptSet(IFace, Msg, NS, commit);

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToObjCLiteralSyntax.
//===----------------------------------------------------------------------===//

static bool rewriteToArrayLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit,
                                  const ParentMap *PMap);
static bool rewriteToDictionaryLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);
static bool rewriteToNumberLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);
static bool rewriteToNumericBoxedExpression(const ObjCMessageExpr *Msg,
                                            const NSAPI &NS, Commit &commit);
static bool rewriteToStringBoxedExpression(const ObjCMessageExpr *Msg,
                                           const NSAPI &NS, Commit &commit);

bool edit::rewriteToObjCLiteralSyntax(const ObjCMessageExpr *Msg,
                                      const NSAPI &NS, Commit &commit,
                                      const ParentMap *PMap) {
  IdentifierInfo *II = nullptr;
  if (!checkForLiteralCreation(Msg, II, NS.getASTContext().getLangOpts()))
    return false;

  if (II == NS.getNSClassId(NSAPI::ClassId_NSArray))
    return rewriteToArrayLiteral(Msg, NS, commit, PMap);
  if (II == NS.getNSClassId(NSAPI::ClassId_NSDictionary))
    return rewriteToDictionaryLiteral(Msg, NS, commit);
  if (II == NS.getNSClassId(NSAPI::ClassId_NSNumber))
    return rewriteToNumberLiteral(Msg, NS, commit);
  if (II == NS.getNSClassId(NSAPI::ClassId_NSString))
    return rewriteToStringBoxedExpression(Msg, NS, commit);

  return false;
}

/// Returns true if the immediate message arguments of \c Msg should not
/// be rewritten because it will interfere with the rewrite of the parent
/// message expression. e.g.
/// \code
///   [NSDictionary dictionaryWithObjects:
///                                 [NSArray arrayWithObjects:@"1", @"2", nil]
///                         forKeys:[NSArray arrayWithObjects:@"A", @"B", nil]];
/// \endcode
/// It will return true for this because we are going to rewrite this directly
/// to a dictionary literal without any array literals.
static bool shouldNotRewriteImmediateMessageArgs(const ObjCMessageExpr *Msg,
                                                 const NSAPI &NS);

//===----------------------------------------------------------------------===//
// rewriteToArrayLiteral.
//===----------------------------------------------------------------------===//

/// Adds an explicit cast to 'id' if the type is not objc object.
static void objectifyExpr(const Expr *E, Commit &commit);

static bool rewriteToArrayLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit,
                                  const ParentMap *PMap) {
  if (PMap) {
    const ObjCMessageExpr *ParentMsg =
        dyn_cast_or_null<ObjCMessageExpr>(PMap->getParentIgnoreParenCasts(Msg));
    if (shouldNotRewriteImmediateMessageArgs(ParentMsg, NS))
      return false;
  }

  Selector Sel = Msg->getSelector();
  SourceRange MsgRange = Msg->getSourceRange();

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_array)) {
    if (Msg->getNumArgs() != 0)
      return false;
    commit.replace(MsgRange, "@[]");
    return true;
  }

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObject)) {
    if (Msg->getNumArgs() != 1)
      return false;
    objectifyExpr(Msg->getArg(0), commit);
    SourceRange ArgRange = Msg->getArg(0)->getSourceRange();
    commit.replaceWithInner(MsgRange, ArgRange);
    commit.insertWrap("@[", ArgRange, "]");
    return true;
  }

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObjects) ||
      Sel == NS.getNSArraySelector(NSAPI::NSArr_initWithObjects)) {
    if (Msg->getNumArgs() == 0)
      return false;
    const Expr *SentinelExpr = Msg->getArg(Msg->getNumArgs() - 1);
    if (!NS.getASTContext().isSentinelNullExpr(SentinelExpr))
      return false;

    for (unsigned i = 0, e = Msg->getNumArgs() - 1; i != e; ++i)
      objectifyExpr(Msg->getArg(i), commit);

    if (Msg->getNumArgs() == 1) {
      commit.replace(MsgRange, "@[]");
      return true;
    }
    SourceRange ArgRange(Msg->getArg(0)->getBeginLoc(),
                         Msg->getArg(Msg->getNumArgs() - 2)->getEndLoc());
    commit.replaceWithInner(MsgRange, ArgRange);
    commit.insertWrap("@[", ArgRange, "]");
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToDictionaryLiteral.
//===----------------------------------------------------------------------===//

/// If \c Msg is an NSArray creation message or literal, this gets the
/// objects that were used to create it.
/// \returns true if it is an NSArray and we got objects, or false otherwise.
static bool getNSArrayObjects(const Expr *E, const NSAPI &NS,
                              SmallVectorImpl<const Expr *> &Objs) {
  if (!E)
    return false;

  E = E->IgnoreParenCasts();
  if (!E)
    return false;

  if (const ObjCMessageExpr *Msg = dyn_cast<ObjCMessageExpr>(E)) {
    IdentifierInfo *Cls = nullptr;
    if (!checkForLiteralCreation(Msg, Cls, NS.getASTContext().getLangOpts()))
      return false;

    if (Cls != NS.getNSClassId(NSAPI::ClassId_NSArray))
      return false;

    Selector Sel = Msg->getSelector();
    if (Sel == NS.getNSArraySelector(NSAPI::NSArr_array))
      return true; // empty array.

    if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObject)) {
      if (Msg->getNumArgs() != 1)
        return false;
      Objs.push_back(Msg->getArg(0));
      return true;
    }

    if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObjects) ||
        Sel == NS.getNSArraySelector(NSAPI::NSArr_initWithObjects)) {
      if (Msg->getNumArgs() == 0)
        return false;
      const Expr *SentinelExpr = Msg->getArg(Msg->getNumArgs() - 1);
      if (!NS.getASTContext().isSentinelNullExpr(SentinelExpr))
        return false;

      for (unsigned i = 0, e = Msg->getNumArgs() - 1; i != e; ++i)
        Objs.push_back(Msg->getArg(i));
      return true;
    }

  } else if (const ObjCArrayLiteral *ArrLit = dyn_cast<ObjCArrayLiteral>(E)) {
    for (unsigned i = 0, e = ArrLit->getNumElements(); i != e; ++i)
      Objs.push_back(ArrLit->getElement(i));
    return true;
  }

  return false;
}

static bool rewriteToDictionaryLiteral(const ObjCMessageExpr *Msg,
                                       const NSAPI &NS, Commit &commit) {
  Selector Sel = Msg->getSelector();
  SourceRange MsgRange = Msg->getSourceRange();

  if (Sel == NS.getNSDictionarySelector(NSAPI::NSDict_dictionary)) {
    if (Msg->getNumArgs() != 0)
      return false;
    commit.replace(MsgRange, "@{}");
    return true;
  }

  if (Sel == NS.getNSDictionarySelector(
                                    NSAPI::NSDict_dictionaryWithObjectForKey)) {
    if (Msg->getNumArgs() != 2)
      return false;

    objectifyExpr(Msg->getArg(0), commit);
    objectifyExpr(Msg->getArg(1), commit);

    SourceRange ValRange = Msg->getArg(0)->getSourceRange();
    SourceRange KeyRange = Msg->getArg(1)->getSourceRange();
    // Insert key before the value.
    commit.insertBefore(ValRange.getBegin(), ": ");
    commit.insertFromRange(ValRange.getBegin(),
                           CharSourceRange::getTokenRange(KeyRange),
                       /*afterToken=*/false, /*beforePreviousInsertions=*/true);
    commit.insertBefore(ValRange.getBegin(), "@{");
    commit.insertAfterToken(ValRange.getEnd(), "}");
    commit.replaceWithInner(MsgRange, ValRange);
    return true;
  }

  if (Sel == NS.getNSDictionarySelector(
                                  NSAPI::NSDict_dictionaryWithObjectsAndKeys) ||
      Sel == NS.getNSDictionarySelector(NSAPI::NSDict_initWithObjectsAndKeys)) {
    if (Msg->getNumArgs() % 2 != 1)
      return false;
    unsigned SentinelIdx = Msg->getNumArgs() - 1;
    const Expr *SentinelExpr = Msg->getArg(SentinelIdx);
    if (!NS.getASTContext().isSentinelNullExpr(SentinelExpr))
      return false;

    if (Msg->getNumArgs() == 1) {
      commit.replace(MsgRange, "@{}");
      return true;
    }

    for (unsigned i = 0; i < SentinelIdx; i += 2) {
      objectifyExpr(Msg->getArg(i), commit);
      objectifyExpr(Msg->getArg(i+1), commit);

      SourceRange ValRange = Msg->getArg(i)->getSourceRange();
      SourceRange KeyRange = Msg->getArg(i+1)->getSourceRange();
      // Insert value after key.
      commit.insertAfterToken(KeyRange.getEnd(), ": ");
      commit.insertFromRange(KeyRange.getEnd(), ValRange, /*afterToken=*/true);
      commit.remove(CharSourceRange::getCharRange(ValRange.getBegin(),
                                                  KeyRange.getBegin()));
    }
    // Range of arguments up until and including the last key.
    // The sentinel and first value are cut off, the value will move after the
    // key.
    SourceRange ArgRange(Msg->getArg(1)->getBeginLoc(),
                         Msg->getArg(SentinelIdx - 1)->getEndLoc());
    commit.insertWrap("@{", ArgRange, "}");
    commit.replaceWithInner(MsgRange, ArgRange);
    return true;
  }

  if (Sel == NS.getNSDictionarySelector(
                                  NSAPI::NSDict_dictionaryWithObjectsForKeys) ||
      Sel == NS.getNSDictionarySelector(NSAPI::NSDict_initWithObjectsForKeys)) {
    if (Msg->getNumArgs() != 2)
      return false;

    SmallVector<const Expr *, 8> Vals;
    if (!getNSArrayObjects(Msg->getArg(0), NS, Vals))
      return false;

    SmallVector<const Expr *, 8> Keys;
    if (!getNSArrayObjects(Msg->getArg(1), NS, Keys))
      return false;

    if (Vals.size() != Keys.size())
      return false;

    if (Vals.empty()) {
      commit.replace(MsgRange, "@{}");
      return true;
    }

    for (unsigned i = 0, n = Vals.size(); i < n; ++i) {
      objectifyExpr(Vals[i], commit);
      objectifyExpr(Keys[i], commit);

      SourceRange ValRange = Vals[i]->getSourceRange();
      SourceRange KeyRange = Keys[i]->getSourceRange();
      // Insert value after key.
      commit.insertAfterToken(KeyRange.getEnd(), ": ");
      commit.insertFromRange(KeyRange.getEnd(), ValRange, /*afterToken=*/true);
    }
    // Range of arguments up until and including the last key.
    // The first value is cut off, the value will move after the key.
    SourceRange ArgRange(Keys.front()->getBeginLoc(), Keys.back()->getEndLoc());
    commit.insertWrap("@{", ArgRange, "}");
    commit.replaceWithInner(MsgRange, ArgRange);
    return true;
  }

  return false;
}

static bool shouldNotRewriteImmediateMessageArgs(const ObjCMessageExpr *Msg,
                                                 const NSAPI &NS) {
  if (!Msg)
    return false;

  IdentifierInfo *II = nullptr;
  if (!checkForLiteralCreation(Msg, II, NS.getASTContext().getLangOpts()))
    return false;

  if (II != NS.getNSClassId(NSAPI::ClassId_NSDictionary))
    return false;

  Selector Sel = Msg->getSelector();
  if (Sel == NS.getNSDictionarySelector(
                                  NSAPI::NSDict_dictionaryWithObjectsForKeys) ||
      Sel == NS.getNSDictionarySelector(NSAPI::NSDict_initWithObjectsForKeys)) {
    if (Msg->getNumArgs() != 2)
      return false;

    SmallVector<const Expr *, 8> Vals;
    if (!getNSArrayObjects(Msg->getArg(0), NS, Vals))
      return false;

    SmallVector<const Expr *, 8> Keys;
    if (!getNSArrayObjects(Msg->getArg(1), NS, Keys))
      return false;

    if (Vals.size() != Keys.size())
      return false;

    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToNumberLiteral.
//===----------------------------------------------------------------------===//

static bool rewriteToCharLiteral(const ObjCMessageExpr *Msg,
                                   const CharacterLiteral *Arg,
                                   const NSAPI &NS, Commit &commit) {
  if (Arg->getKind() != CharacterLiteral::Ascii)
    return false;
  if (NS.isNSNumberLiteralSelector(NSAPI::NSNumberWithChar,
                                   Msg->getSelector())) {
    SourceRange ArgRange = Arg->getSourceRange();
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  return rewriteToNumericBoxedExpression(Msg, NS, commit);
}

static bool rewriteToBoolLiteral(const ObjCMessageExpr *Msg,
                                   const Expr *Arg,
                                   const NSAPI &NS, Commit &commit) {
  if (NS.isNSNumberLiteralSelector(NSAPI::NSNumberWithBool,
                                   Msg->getSelector())) {
    SourceRange ArgRange = Arg->getSourceRange();
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  return rewriteToNumericBoxedExpression(Msg, NS, commit);
}

namespace {

struct LiteralInfo {
  bool Hex, Octal;
  StringRef U, F, L, LL;
  CharSourceRange WithoutSuffRange;
};

}

static bool getLiteralInfo(SourceRange literalRange,
                           bool isFloat, bool isIntZero,
                          ASTContext &Ctx, LiteralInfo &Info) {
  if (literalRange.getBegin().isMacroID() ||
      literalRange.getEnd().isMacroID())
    return false;
  StringRef text = Lexer::getSourceText(
                                  CharSourceRange::getTokenRange(literalRange),
                                  Ctx.getSourceManager(), Ctx.getLangOpts());
  if (text.empty())
    return false;

  Optional<bool> UpperU, UpperL;
  bool UpperF = false;

  struct Suff {
    static bool has(StringRef suff, StringRef &text) {
      if (text.endswith(suff)) {
        text = text.substr(0, text.size()-suff.size());
        return true;
      }
      return false;
    }
  };

  while (1) {
    if (Suff::has("u", text)) {
      UpperU = false;
    } else if (Suff::has("U", text)) {
      UpperU = true;
    } else if (Suff::has("ll", text)) {
      UpperL = false;
    } else if (Suff::has("LL", text)) {
      UpperL = true;
    } else if (Suff::has("l", text)) {
      UpperL = false;
    } else if (Suff::has("L", text)) {
      UpperL = true;
    } else if (isFloat && Suff::has("f", text)) {
      UpperF = false;
    } else if (isFloat && Suff::has("F", text)) {
      UpperF = true;
    } else
      break;
  }

  if (!UpperU.hasValue() && !UpperL.hasValue())
    UpperU = UpperL = true;
  else if (UpperU.hasValue() && !UpperL.hasValue())
    UpperL = UpperU;
  else if (UpperL.hasValue() && !UpperU.hasValue())
    UpperU = UpperL;

  Info.U = *UpperU ? "U" : "u";
  Info.L = *UpperL ? "L" : "l";
  Info.LL = *UpperL ? "LL" : "ll";
  Info.F = UpperF ? "F" : "f";

  Info.Hex = Info.Octal = false;
  if (text.startswith("0x"))
    Info.Hex = true;
  else if (!isFloat && !isIntZero && text.startswith("0"))
    Info.Octal = true;

  SourceLocation B = literalRange.getBegin();
  Info.WithoutSuffRange =
      CharSourceRange::getCharRange(B, B.getLocWithOffset(text.size()));
  return true;
}

static bool rewriteToNumberLiteral(const ObjCMessageExpr *Msg,
                                   const NSAPI &NS, Commit &commit) {
  if (Msg->getNumArgs() != 1)
    return false;

  const Expr *Arg = Msg->getArg(0)->IgnoreParenImpCasts();
  if (const CharacterLiteral *CharE = dyn_cast<CharacterLiteral>(Arg))
    return rewriteToCharLiteral(Msg, CharE, NS, commit);
  if (const ObjCBoolLiteralExpr *BE = dyn_cast<ObjCBoolLiteralExpr>(Arg))
    return rewriteToBoolLiteral(Msg, BE, NS, commit);
  if (const CXXBoolLiteralExpr *BE = dyn_cast<CXXBoolLiteralExpr>(Arg))
    return rewriteToBoolLiteral(Msg, BE, NS, commit);

  const Expr *literalE = Arg;
  if (const UnaryOperator *UOE = dyn_cast<UnaryOperator>(literalE)) {
    if (UOE->getOpcode() == UO_Plus || UOE->getOpcode() == UO_Minus)
      literalE = UOE->getSubExpr();
  }

  // Only integer and floating literals, otherwise try to rewrite to boxed
  // expression.
  if (!isa<IntegerLiteral>(literalE) && !isa<FloatingLiteral>(literalE))
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  ASTContext &Ctx = NS.getASTContext();
  Selector Sel = Msg->getSelector();
  Optional<NSAPI::NSNumberLiteralMethodKind>
    MKOpt = NS.getNSNumberLiteralMethodKind(Sel);
  if (!MKOpt)
    return false;
  NSAPI::NSNumberLiteralMethodKind MK = *MKOpt;

  bool CallIsUnsigned = false, CallIsLong = false, CallIsLongLong = false;
  bool CallIsFloating = false, CallIsDouble = false;

  switch (MK) {
  // We cannot have these calls with int/float literals.
  case NSAPI::NSNumberWithChar:
  case NSAPI::NSNumberWithUnsignedChar:
  case NSAPI::NSNumberWithShort:
  case NSAPI::NSNumberWithUnsignedShort:
  case NSAPI::NSNumberWithBool:
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  case NSAPI::NSNumberWithUnsignedInt:
  case NSAPI::NSNumberWithUnsignedInteger:
    CallIsUnsigned = true;
    LLVM_FALLTHROUGH;
  case NSAPI::NSNumberWithInt:
  case NSAPI::NSNumberWithInteger:
    break;

  case NSAPI::NSNumberWithUnsignedLong:
    CallIsUnsigned = true;
    LLVM_FALLTHROUGH;
  case NSAPI::NSNumberWithLong:
    CallIsLong = true;
    break;

  case NSAPI::NSNumberWithUnsignedLongLong:
    CallIsUnsigned = true;
    LLVM_FALLTHROUGH;
  case NSAPI::NSNumberWithLongLong:
    CallIsLongLong = true;
    break;

  case NSAPI::NSNumberWithDouble:
    CallIsDouble = true;
    LLVM_FALLTHROUGH;
  case NSAPI::NSNumberWithFloat:
    CallIsFloating = true;
    break;
  }

  SourceRange ArgRange = Arg->getSourceRange();
  QualType ArgTy = Arg->getType();
  QualType CallTy = Msg->getArg(0)->getType();

  // Check for the easy case, the literal maps directly to the call.
  if (Ctx.hasSameType(ArgTy, CallTy)) {
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  // We will need to modify the literal suffix to get the same type as the call.
  // Try with boxed expression if it came from a macro.
  if (ArgRange.getBegin().isMacroID())
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  bool LitIsFloat = ArgTy->isFloatingType();
  // For a float passed to integer call, don't try rewriting to objc literal.
  // It is difficult and a very uncommon case anyway.
  // But try with boxed expression.
  if (LitIsFloat && !CallIsFloating)
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  // Try to modify the literal make it the same type as the method call.
  // -Modify the suffix, and/or
  // -Change integer to float

  LiteralInfo LitInfo;
  bool isIntZero = false;
  if (const IntegerLiteral *IntE = dyn_cast<IntegerLiteral>(literalE))
    isIntZero = !IntE->getValue().getBoolValue();
  if (!getLiteralInfo(ArgRange, LitIsFloat, isIntZero, Ctx, LitInfo))
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  // Not easy to do int -> float with hex/octal and uncommon anyway.
  if (!LitIsFloat && CallIsFloating && (LitInfo.Hex || LitInfo.Octal))
    return rewriteToNumericBoxedExpression(Msg, NS, commit);

  SourceLocation LitB = LitInfo.WithoutSuffRange.getBegin();
  SourceLocation LitE = LitInfo.WithoutSuffRange.getEnd();

  commit.replaceWithInner(CharSourceRange::getTokenRange(Msg->getSourceRange()),
                         LitInfo.WithoutSuffRange);
  commit.insert(LitB, "@");

  if (!LitIsFloat && CallIsFloating)
    commit.insert(LitE, ".0");

  if (CallIsFloating) {
    if (!CallIsDouble)
      commit.insert(LitE, LitInfo.F);
  } else {
    if (CallIsUnsigned)
      commit.insert(LitE, LitInfo.U);

    if (CallIsLong)
      commit.insert(LitE, LitInfo.L);
    else if (CallIsLongLong)
      commit.insert(LitE, LitInfo.LL);
  }
  return true;
}

// FIXME: Make determination of operator precedence more general and
// make it broadly available.
static bool subscriptOperatorNeedsParens(const Expr *FullExpr) {
  const Expr* Expr = FullExpr->IgnoreImpCasts();
  if (isa<ArraySubscriptExpr>(Expr) ||
      isa<CallExpr>(Expr) ||
      isa<DeclRefExpr>(Expr) ||
      isa<CXXNamedCastExpr>(Expr) ||
      isa<CXXConstructExpr>(Expr) ||
      isa<CXXThisExpr>(Expr) ||
      isa<CXXTypeidExpr>(Expr) ||
      isa<CXXUnresolvedConstructExpr>(Expr) ||
      isa<ObjCMessageExpr>(Expr) ||
      isa<ObjCPropertyRefExpr>(Expr) ||
      isa<ObjCProtocolExpr>(Expr) ||
      isa<MemberExpr>(Expr) ||
      isa<ObjCIvarRefExpr>(Expr) ||
      isa<ParenExpr>(FullExpr) ||
      isa<ParenListExpr>(Expr) ||
      isa<SizeOfPackExpr>(Expr))
    return false;

  return true;
}
static bool castOperatorNeedsParens(const Expr *FullExpr) {
  const Expr* Expr = FullExpr->IgnoreImpCasts();
  if (isa<ArraySubscriptExpr>(Expr) ||
      isa<CallExpr>(Expr) ||
      isa<DeclRefExpr>(Expr) ||
      isa<CastExpr>(Expr) ||
      isa<CXXNewExpr>(Expr) ||
      isa<CXXConstructExpr>(Expr) ||
      isa<CXXDeleteExpr>(Expr) ||
      isa<CXXNoexceptExpr>(Expr) ||
      isa<CXXPseudoDestructorExpr>(Expr) ||
      isa<CXXScalarValueInitExpr>(Expr) ||
      isa<CXXThisExpr>(Expr) ||
      isa<CXXTypeidExpr>(Expr) ||
      isa<CXXUnresolvedConstructExpr>(Expr) ||
      isa<ObjCMessageExpr>(Expr) ||
      isa<ObjCPropertyRefExpr>(Expr) ||
      isa<ObjCProtocolExpr>(Expr) ||
      isa<MemberExpr>(Expr) ||
      isa<ObjCIvarRefExpr>(Expr) ||
      isa<ParenExpr>(FullExpr) ||
      isa<ParenListExpr>(Expr) ||
      isa<SizeOfPackExpr>(Expr) ||
      isa<UnaryOperator>(Expr))
    return false;

  return true;
}

static void objectifyExpr(const Expr *E, Commit &commit) {
  if (!E) return;

  QualType T = E->getType();
  if (T->isObjCObjectPointerType()) {
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
      if (ICE->getCastKind() != CK_CPointerToObjCPointerCast)
        return;
    } else {
      return;
    }
  } else if (!T->isPointerType()) {
    return;
  }

  SourceRange Range = E->getSourceRange();
  if (castOperatorNeedsParens(E))
    commit.insertWrap("(", Range, ")");
  commit.insertBefore(Range.getBegin(), "(id)");
}

//===----------------------------------------------------------------------===//
// rewriteToNumericBoxedExpression.
//===----------------------------------------------------------------------===//

static bool isEnumConstant(const Expr *E) {
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    if (const ValueDecl *VD = DRE->getDecl())
      return isa<EnumConstantDecl>(VD);

  return false;
}

static bool rewriteToNumericBoxedExpression(const ObjCMessageExpr *Msg,
                                            const NSAPI &NS, Commit &commit) {
  if (Msg->getNumArgs() != 1)
    return false;

  const Expr *Arg = Msg->getArg(0);
  if (Arg->isTypeDependent())
    return false;

  ASTContext &Ctx = NS.getASTContext();
  Selector Sel = Msg->getSelector();
  Optional<NSAPI::NSNumberLiteralMethodKind>
    MKOpt = NS.getNSNumberLiteralMethodKind(Sel);
  if (!MKOpt)
    return false;
  NSAPI::NSNumberLiteralMethodKind MK = *MKOpt;

  const Expr *OrigArg = Arg->IgnoreImpCasts();
  QualType FinalTy = Arg->getType();
  QualType OrigTy = OrigArg->getType();
  uint64_t FinalTySize = Ctx.getTypeSize(FinalTy);
  uint64_t OrigTySize = Ctx.getTypeSize(OrigTy);

  bool isTruncated = FinalTySize < OrigTySize;
  bool needsCast = false;

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg)) {
    switch (ICE->getCastKind()) {
    case CK_LValueToRValue:
    case CK_NoOp:
    case CK_UserDefinedConversion:
      break;

    case CK_IntegralCast: {
      if (MK == NSAPI::NSNumberWithBool && OrigTy->isBooleanType())
        break;
      // Be more liberal with Integer/UnsignedInteger which are very commonly
      // used.
      if ((MK == NSAPI::NSNumberWithInteger ||
           MK == NSAPI::NSNumberWithUnsignedInteger) &&
          !isTruncated) {
        if (OrigTy->getAs<EnumType>() || isEnumConstant(OrigArg))
          break;
        if ((MK==NSAPI::NSNumberWithInteger) == OrigTy->isSignedIntegerType() &&
            OrigTySize >= Ctx.getTypeSize(Ctx.IntTy))
          break;
      }

      needsCast = true;
      break;
    }

    case CK_PointerToBoolean:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_AtomicToNonAtomic:
    case CK_AddressSpaceConversion:
      needsCast = true;
      break;

    case CK_Dependent:
    case CK_BitCast:
    case CK_LValueBitCast:
    case CK_BaseToDerived:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
    case CK_Dynamic:
    case CK_ToUnion:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_NullToPointer:
    case CK_NullToMemberPointer:
    case CK_BaseToDerivedMemberPointer:
    case CK_DerivedToBaseMemberPointer:
    case CK_MemberPointerToBoolean:
    case CK_ReinterpretMemberPointer:
    case CK_ConstructorConversion:
    case CK_IntegralToPointer:
    case CK_PointerToIntegral:
    case CK_ToVoid:
    case CK_VectorSplat:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_ObjCObjectLValueCast:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_NonAtomicToAtomic:
    case CK_CopyAndAutoreleaseBlockObject:
    case CK_BuiltinFnToFnPtr:
    case CK_ZeroToOCLOpaqueType:
    case CK_IntToOCLSampler:
      return false;

    case CK_BooleanToSignedIntegral:
      llvm_unreachable("OpenCL-specific cast in Objective-C?");

    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
      llvm_unreachable("Fixed point types are disabled for Objective-C");
    }
  }

  if (needsCast) {
    DiagnosticsEngine &Diags = Ctx.getDiagnostics();
    // FIXME: Use a custom category name to distinguish migration diagnostics.
    unsigned diagID = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                       "converting to boxing syntax requires casting %0 to %1");
    Diags.Report(Msg->getExprLoc(), diagID) << OrigTy << FinalTy
        << Msg->getSourceRange();
    return false;
  }

  SourceRange ArgRange = OrigArg->getSourceRange();
  commit.replaceWithInner(Msg->getSourceRange(), ArgRange);

  if (isa<ParenExpr>(OrigArg) || isa<IntegerLiteral>(OrigArg))
    commit.insertBefore(ArgRange.getBegin(), "@");
  else
    commit.insertWrap("@(", ArgRange, ")");

  return true;
}

//===----------------------------------------------------------------------===//
// rewriteToStringBoxedExpression.
//===----------------------------------------------------------------------===//

static bool doRewriteToUTF8StringBoxedExpressionHelper(
                                              const ObjCMessageExpr *Msg,
                                              const NSAPI &NS, Commit &commit) {
  const Expr *Arg = Msg->getArg(0);
  if (Arg->isTypeDependent())
    return false;

  ASTContext &Ctx = NS.getASTContext();

  const Expr *OrigArg = Arg->IgnoreImpCasts();
  QualType OrigTy = OrigArg->getType();
  if (OrigTy->isArrayType())
    OrigTy = Ctx.getArrayDecayedType(OrigTy);

  if (const StringLiteral *
        StrE = dyn_cast<StringLiteral>(OrigArg->IgnoreParens())) {
    commit.replaceWithInner(Msg->getSourceRange(), StrE->getSourceRange());
    commit.insert(StrE->getBeginLoc(), "@");
    return true;
  }

  if (const PointerType *PT = OrigTy->getAs<PointerType>()) {
    QualType PointeeType = PT->getPointeeType();
    if (Ctx.hasSameUnqualifiedType(PointeeType, Ctx.CharTy)) {
      SourceRange ArgRange = OrigArg->getSourceRange();
      commit.replaceWithInner(Msg->getSourceRange(), ArgRange);

      if (isa<ParenExpr>(OrigArg) || isa<IntegerLiteral>(OrigArg))
        commit.insertBefore(ArgRange.getBegin(), "@");
      else
        commit.insertWrap("@(", ArgRange, ")");

      return true;
    }
  }

  return false;
}

static bool rewriteToStringBoxedExpression(const ObjCMessageExpr *Msg,
                                           const NSAPI &NS, Commit &commit) {
  Selector Sel = Msg->getSelector();

  if (Sel == NS.getNSStringSelector(NSAPI::NSStr_stringWithUTF8String) ||
      Sel == NS.getNSStringSelector(NSAPI::NSStr_stringWithCString) ||
      Sel == NS.getNSStringSelector(NSAPI::NSStr_initWithUTF8String)) {
    if (Msg->getNumArgs() != 1)
      return false;
    return doRewriteToUTF8StringBoxedExpressionHelper(Msg, NS, commit);
  }

  if (Sel == NS.getNSStringSelector(NSAPI::NSStr_stringWithCStringEncoding)) {
    if (Msg->getNumArgs() != 2)
      return false;

    const Expr *encodingArg = Msg->getArg(1);
    if (NS.isNSUTF8StringEncodingConstant(encodingArg) ||
        NS.isNSASCIIStringEncodingConstant(encodingArg))
      return doRewriteToUTF8StringBoxedExpressionHelper(Msg, NS, commit);
  }

  return false;
}
