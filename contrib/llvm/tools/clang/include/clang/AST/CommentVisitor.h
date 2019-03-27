//===- CommentVisitor.h - Visitor for Comment subclasses --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_COMMENTVISITOR_H
#define LLVM_CLANG_AST_COMMENTVISITOR_H

#include "clang/AST/Comment.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {
namespace comments {
template <template <typename> class Ptr, typename ImplClass,
          typename RetTy = void, class... ParamTys>
class CommentVisitorBase {
public:
#define PTR(CLASS) typename Ptr<CLASS>::type
#define DISPATCH(NAME, CLASS)                                                  \
  return static_cast<ImplClass *>(this)->visit##NAME(                          \
      static_cast<PTR(CLASS)>(C), std::forward<ParamTys>(P)...)

  RetTy visit(PTR(Comment) C, ParamTys... P) {
    if (!C)
      return RetTy();

    switch (C->getCommentKind()) {
    default: llvm_unreachable("Unknown comment kind!");
#define ABSTRACT_COMMENT(COMMENT)
#define COMMENT(CLASS, PARENT) \
    case Comment::CLASS##Kind: DISPATCH(CLASS, CLASS);
#include "clang/AST/CommentNodes.inc"
#undef ABSTRACT_COMMENT
#undef COMMENT
    }
  }

  // If the derived class does not implement a certain Visit* method, fall back
  // on Visit* method for the superclass.
#define ABSTRACT_COMMENT(COMMENT) COMMENT
#define COMMENT(CLASS, PARENT)                                                 \
  RetTy visit##CLASS(PTR(CLASS) C, ParamTys... P) { DISPATCH(PARENT, PARENT); }
#include "clang/AST/CommentNodes.inc"
#undef ABSTRACT_COMMENT
#undef COMMENT

  RetTy visitComment(PTR(Comment) C, ParamTys... P) { return RetTy(); }

#undef PTR
#undef DISPATCH
};

template <typename ImplClass, typename RetTy = void, class... ParamTys>
class CommentVisitor : public CommentVisitorBase<std::add_pointer, ImplClass,
                                                 RetTy, ParamTys...> {};

template <typename ImplClass, typename RetTy = void, class... ParamTys>
class ConstCommentVisitor
    : public CommentVisitorBase<llvm::make_const_ptr, ImplClass, RetTy,
                                ParamTys...> {};

} // namespace comments
} // namespace clang

#endif // LLVM_CLANG_AST_COMMENTVISITOR_H
