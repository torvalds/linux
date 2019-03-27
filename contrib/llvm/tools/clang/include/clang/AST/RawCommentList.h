//===--- RawCommentList.h - Classes for processing raw comments -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_RAWCOMMENTLIST_H
#define LLVM_CLANG_AST_RAWCOMMENTLIST_H

#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {

class ASTContext;
class ASTReader;
class Decl;
class Preprocessor;

namespace comments {
  class FullComment;
} // end namespace comments

class RawComment {
public:
  enum CommentKind {
    RCK_Invalid,      ///< Invalid comment
    RCK_OrdinaryBCPL, ///< Any normal BCPL comments
    RCK_OrdinaryC,    ///< Any normal C comment
    RCK_BCPLSlash,    ///< \code /// stuff \endcode
    RCK_BCPLExcl,     ///< \code //! stuff \endcode
    RCK_JavaDoc,      ///< \code /** stuff */ \endcode
    RCK_Qt,           ///< \code /*! stuff */ \endcode, also used by HeaderDoc
    RCK_Merged        ///< Two or more documentation comments merged together
  };

  RawComment() : Kind(RCK_Invalid), IsAlmostTrailingComment(false) { }

  RawComment(const SourceManager &SourceMgr, SourceRange SR,
             const CommentOptions &CommentOpts, bool Merged);

  CommentKind getKind() const LLVM_READONLY {
    return (CommentKind) Kind;
  }

  bool isInvalid() const LLVM_READONLY {
    return Kind == RCK_Invalid;
  }

  bool isMerged() const LLVM_READONLY {
    return Kind == RCK_Merged;
  }

  /// Is this comment attached to any declaration?
  bool isAttached() const LLVM_READONLY {
    return IsAttached;
  }

  void setAttached() {
    IsAttached = true;
  }

  /// Returns true if it is a comment that should be put after a member:
  /// \code ///< stuff \endcode
  /// \code //!< stuff \endcode
  /// \code /**< stuff */ \endcode
  /// \code /*!< stuff */ \endcode
  bool isTrailingComment() const LLVM_READONLY {
    return IsTrailingComment;
  }

  /// Returns true if it is a probable typo:
  /// \code //< stuff \endcode
  /// \code /*< stuff */ \endcode
  bool isAlmostTrailingComment() const LLVM_READONLY {
    return IsAlmostTrailingComment;
  }

  /// Returns true if this comment is not a documentation comment.
  bool isOrdinary() const LLVM_READONLY {
    return ((Kind == RCK_OrdinaryBCPL) || (Kind == RCK_OrdinaryC));
  }

  /// Returns true if this comment any kind of a documentation comment.
  bool isDocumentation() const LLVM_READONLY {
    return !isInvalid() && !isOrdinary();
  }

  /// Returns raw comment text with comment markers.
  StringRef getRawText(const SourceManager &SourceMgr) const {
    if (RawTextValid)
      return RawText;

    RawText = getRawTextSlow(SourceMgr);
    RawTextValid = true;
    return RawText;
  }

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  const char *getBriefText(const ASTContext &Context) const {
    if (BriefTextValid)
      return BriefText;

    return extractBriefText(Context);
  }

  /// Returns sanitized comment text, suitable for presentation in editor UIs.
  /// E.g. will transform:
  ///     // This is a long multiline comment.
  ///     //   Parts of it  might be indented.
  ///     /* The comments styles might be mixed. */
  ///  into
  ///     "This is a long multiline comment.\n"
  ///     "  Parts of it  might be indented.\n"
  ///     "The comments styles might be mixed."
  /// Also removes leading indentation and sanitizes some common cases:
  ///     /* This is a first line.
  ///      *   This is a second line. It is indented.
  ///      * This is a third line. */
  /// and
  ///     /* This is a first line.
  ///          This is a second line. It is indented.
  ///     This is a third line. */
  /// will both turn into:
  ///     "This is a first line.\n"
  ///     "  This is a second line. It is indented.\n"
  ///     "This is a third line."
  std::string getFormattedText(const SourceManager &SourceMgr,
                               DiagnosticsEngine &Diags) const;

  /// Parse the comment, assuming it is attached to decl \c D.
  comments::FullComment *parse(const ASTContext &Context,
                               const Preprocessor *PP, const Decl *D) const;

private:
  SourceRange Range;

  mutable StringRef RawText;
  mutable const char *BriefText;

  mutable bool RawTextValid : 1;   ///< True if RawText is valid
  mutable bool BriefTextValid : 1; ///< True if BriefText is valid

  unsigned Kind : 3;

  /// True if comment is attached to a declaration in ASTContext.
  bool IsAttached : 1;

  bool IsTrailingComment : 1;
  bool IsAlmostTrailingComment : 1;

  /// Constructor for AST deserialization.
  RawComment(SourceRange SR, CommentKind K, bool IsTrailingComment,
             bool IsAlmostTrailingComment) :
    Range(SR), RawTextValid(false), BriefTextValid(false), Kind(K),
    IsAttached(false), IsTrailingComment(IsTrailingComment),
    IsAlmostTrailingComment(IsAlmostTrailingComment)
  { }

  StringRef getRawTextSlow(const SourceManager &SourceMgr) const;

  const char *extractBriefText(const ASTContext &Context) const;

  friend class ASTReader;
};

/// Compare comments' source locations.
template<>
class BeforeThanCompare<RawComment> {
  const SourceManager &SM;

public:
  explicit BeforeThanCompare(const SourceManager &SM) : SM(SM) { }

  bool operator()(const RawComment &LHS, const RawComment &RHS) {
    return SM.isBeforeInTranslationUnit(LHS.getBeginLoc(), RHS.getBeginLoc());
  }

  bool operator()(const RawComment *LHS, const RawComment *RHS) {
    return operator()(*LHS, *RHS);
  }
};

/// This class represents all comments included in the translation unit,
/// sorted in order of appearance in the translation unit.
class RawCommentList {
public:
  RawCommentList(SourceManager &SourceMgr) : SourceMgr(SourceMgr) {}

  void addComment(const RawComment &RC, const CommentOptions &CommentOpts,
                  llvm::BumpPtrAllocator &Allocator);

  ArrayRef<RawComment *> getComments() const {
    return Comments;
  }

private:
  SourceManager &SourceMgr;
  std::vector<RawComment *> Comments;

  void addDeserializedComments(ArrayRef<RawComment *> DeserializedComments);

  friend class ASTReader;
};

} // end namespace clang

#endif
