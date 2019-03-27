//===--- Rewriters.h - Rewritings     ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EDIT_REWRITERS_H
#define LLVM_CLANG_EDIT_REWRITERS_H

namespace clang {
  class ObjCMessageExpr;
  class ObjCMethodDecl;
  class ObjCInterfaceDecl;
  class ObjCProtocolDecl;
  class NSAPI;
  class EnumDecl;
  class TypedefDecl;
  class ParentMap;

namespace edit {
  class Commit;

bool rewriteObjCRedundantCallWithLiteral(const ObjCMessageExpr *Msg,
                                         const NSAPI &NS, Commit &commit);

bool rewriteToObjCLiteralSyntax(const ObjCMessageExpr *Msg,
                                const NSAPI &NS, Commit &commit,
                                const ParentMap *PMap);

bool rewriteToObjCSubscriptSyntax(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);

}

}  // end namespace clang

#endif
