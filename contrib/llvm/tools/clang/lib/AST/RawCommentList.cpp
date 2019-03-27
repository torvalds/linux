//===--- RawCommentList.cpp - Processing raw comments -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/RawCommentList.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Comment.h"
#include "clang/AST/CommentBriefParser.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentLexer.h"
#include "clang/AST/CommentParser.h"
#include "clang/AST/CommentSema.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;

namespace {
/// Get comment kind and bool describing if it is a trailing comment.
std::pair<RawComment::CommentKind, bool> getCommentKind(StringRef Comment,
                                                        bool ParseAllComments) {
  const size_t MinCommentLength = ParseAllComments ? 2 : 3;
  if ((Comment.size() < MinCommentLength) || Comment[0] != '/')
    return std::make_pair(RawComment::RCK_Invalid, false);

  RawComment::CommentKind K;
  if (Comment[1] == '/') {
    if (Comment.size() < 3)
      return std::make_pair(RawComment::RCK_OrdinaryBCPL, false);

    if (Comment[2] == '/')
      K = RawComment::RCK_BCPLSlash;
    else if (Comment[2] == '!')
      K = RawComment::RCK_BCPLExcl;
    else
      return std::make_pair(RawComment::RCK_OrdinaryBCPL, false);
  } else {
    assert(Comment.size() >= 4);

    // Comment lexer does not understand escapes in comment markers, so pretend
    // that this is not a comment.
    if (Comment[1] != '*' ||
        Comment[Comment.size() - 2] != '*' ||
        Comment[Comment.size() - 1] != '/')
      return std::make_pair(RawComment::RCK_Invalid, false);

    if (Comment[2] == '*')
      K = RawComment::RCK_JavaDoc;
    else if (Comment[2] == '!')
      K = RawComment::RCK_Qt;
    else
      return std::make_pair(RawComment::RCK_OrdinaryC, false);
  }
  const bool TrailingComment = (Comment.size() > 3) && (Comment[3] == '<');
  return std::make_pair(K, TrailingComment);
}

bool mergedCommentIsTrailingComment(StringRef Comment) {
  return (Comment.size() > 3) && (Comment[3] == '<');
}

/// Returns true if R1 and R2 both have valid locations that start on the same
/// column.
bool commentsStartOnSameColumn(const SourceManager &SM, const RawComment &R1,
                               const RawComment &R2) {
  SourceLocation L1 = R1.getBeginLoc();
  SourceLocation L2 = R2.getBeginLoc();
  bool Invalid = false;
  unsigned C1 = SM.getPresumedColumnNumber(L1, &Invalid);
  if (!Invalid) {
    unsigned C2 = SM.getPresumedColumnNumber(L2, &Invalid);
    return !Invalid && (C1 == C2);
  }
  return false;
}
} // unnamed namespace

/// Determines whether there is only whitespace in `Buffer` between `P`
/// and the previous line.
/// \param Buffer The buffer to search in.
/// \param P The offset from the beginning of `Buffer` to start from.
/// \return true if all of the characters in `Buffer` ranging from the closest
/// line-ending character before `P` (or the beginning of `Buffer`) to `P - 1`
/// are whitespace.
static bool onlyWhitespaceOnLineBefore(const char *Buffer, unsigned P) {
  // Search backwards until we see linefeed or carriage return.
  for (unsigned I = P; I != 0; --I) {
    char C = Buffer[I - 1];
    if (isVerticalWhitespace(C))
      return true;
    if (!isHorizontalWhitespace(C))
      return false;
  }
  // We hit the beginning of the buffer.
  return true;
}

/// Returns whether `K` is an ordinary comment kind.
static bool isOrdinaryKind(RawComment::CommentKind K) {
  return (K == RawComment::RCK_OrdinaryBCPL) ||
         (K == RawComment::RCK_OrdinaryC);
}

RawComment::RawComment(const SourceManager &SourceMgr, SourceRange SR,
                       const CommentOptions &CommentOpts, bool Merged) :
    Range(SR), RawTextValid(false), BriefTextValid(false),
    IsAttached(false), IsTrailingComment(false),
    IsAlmostTrailingComment(false) {
  // Extract raw comment text, if possible.
  if (SR.getBegin() == SR.getEnd() || getRawText(SourceMgr).empty()) {
    Kind = RCK_Invalid;
    return;
  }

  // Guess comment kind.
  std::pair<CommentKind, bool> K =
      getCommentKind(RawText, CommentOpts.ParseAllComments);

  // Guess whether an ordinary comment is trailing.
  if (CommentOpts.ParseAllComments && isOrdinaryKind(K.first)) {
    FileID BeginFileID;
    unsigned BeginOffset;
    std::tie(BeginFileID, BeginOffset) =
        SourceMgr.getDecomposedLoc(Range.getBegin());
    if (BeginOffset != 0) {
      bool Invalid = false;
      const char *Buffer =
          SourceMgr.getBufferData(BeginFileID, &Invalid).data();
      IsTrailingComment |=
          (!Invalid && !onlyWhitespaceOnLineBefore(Buffer, BeginOffset));
    }
  }

  if (!Merged) {
    Kind = K.first;
    IsTrailingComment |= K.second;

    IsAlmostTrailingComment = RawText.startswith("//<") ||
                                 RawText.startswith("/*<");
  } else {
    Kind = RCK_Merged;
    IsTrailingComment =
        IsTrailingComment || mergedCommentIsTrailingComment(RawText);
  }
}

StringRef RawComment::getRawTextSlow(const SourceManager &SourceMgr) const {
  FileID BeginFileID;
  FileID EndFileID;
  unsigned BeginOffset;
  unsigned EndOffset;

  std::tie(BeginFileID, BeginOffset) =
      SourceMgr.getDecomposedLoc(Range.getBegin());
  std::tie(EndFileID, EndOffset) = SourceMgr.getDecomposedLoc(Range.getEnd());

  const unsigned Length = EndOffset - BeginOffset;
  if (Length < 2)
    return StringRef();

  // The comment can't begin in one file and end in another.
  assert(BeginFileID == EndFileID);

  bool Invalid = false;
  const char *BufferStart = SourceMgr.getBufferData(BeginFileID,
                                                    &Invalid).data();
  if (Invalid)
    return StringRef();

  return StringRef(BufferStart + BeginOffset, Length);
}

const char *RawComment::extractBriefText(const ASTContext &Context) const {
  // Lazily initialize RawText using the accessor before using it.
  (void)getRawText(Context.getSourceManager());

  // Since we will be copying the resulting text, all allocations made during
  // parsing are garbage after resulting string is formed.  Thus we can use
  // a separate allocator for all temporary stuff.
  llvm::BumpPtrAllocator Allocator;

  comments::Lexer L(Allocator, Context.getDiagnostics(),
                    Context.getCommentCommandTraits(),
                    Range.getBegin(),
                    RawText.begin(), RawText.end());
  comments::BriefParser P(L, Context.getCommentCommandTraits());

  const std::string Result = P.Parse();
  const unsigned BriefTextLength = Result.size();
  char *BriefTextPtr = new (Context) char[BriefTextLength + 1];
  memcpy(BriefTextPtr, Result.c_str(), BriefTextLength + 1);
  BriefText = BriefTextPtr;
  BriefTextValid = true;

  return BriefTextPtr;
}

comments::FullComment *RawComment::parse(const ASTContext &Context,
                                         const Preprocessor *PP,
                                         const Decl *D) const {
  // Lazily initialize RawText using the accessor before using it.
  (void)getRawText(Context.getSourceManager());

  comments::Lexer L(Context.getAllocator(), Context.getDiagnostics(),
                    Context.getCommentCommandTraits(),
                    getSourceRange().getBegin(),
                    RawText.begin(), RawText.end());
  comments::Sema S(Context.getAllocator(), Context.getSourceManager(),
                   Context.getDiagnostics(),
                   Context.getCommentCommandTraits(),
                   PP);
  S.setDecl(D);
  comments::Parser P(L, S, Context.getAllocator(), Context.getSourceManager(),
                     Context.getDiagnostics(),
                     Context.getCommentCommandTraits());

  return P.parseFullComment();
}

static bool onlyWhitespaceBetween(SourceManager &SM,
                                  SourceLocation Loc1, SourceLocation Loc2,
                                  unsigned MaxNewlinesAllowed) {
  std::pair<FileID, unsigned> Loc1Info = SM.getDecomposedLoc(Loc1);
  std::pair<FileID, unsigned> Loc2Info = SM.getDecomposedLoc(Loc2);

  // Question does not make sense if locations are in different files.
  if (Loc1Info.first != Loc2Info.first)
    return false;

  bool Invalid = false;
  const char *Buffer = SM.getBufferData(Loc1Info.first, &Invalid).data();
  if (Invalid)
    return false;

  unsigned NumNewlines = 0;
  assert(Loc1Info.second <= Loc2Info.second && "Loc1 after Loc2!");
  // Look for non-whitespace characters and remember any newlines seen.
  for (unsigned I = Loc1Info.second; I != Loc2Info.second; ++I) {
    switch (Buffer[I]) {
    default:
      return false;
    case ' ':
    case '\t':
    case '\f':
    case '\v':
      break;
    case '\r':
    case '\n':
      ++NumNewlines;

      // Check if we have found more than the maximum allowed number of
      // newlines.
      if (NumNewlines > MaxNewlinesAllowed)
        return false;

      // Collapse \r\n and \n\r into a single newline.
      if (I + 1 != Loc2Info.second &&
          (Buffer[I + 1] == '\n' || Buffer[I + 1] == '\r') &&
          Buffer[I] != Buffer[I + 1])
        ++I;
      break;
    }
  }

  return true;
}

void RawCommentList::addComment(const RawComment &RC,
                                const CommentOptions &CommentOpts,
                                llvm::BumpPtrAllocator &Allocator) {
  if (RC.isInvalid())
    return;

  // Check if the comments are not in source order.
  while (!Comments.empty() &&
         !SourceMgr.isBeforeInTranslationUnit(Comments.back()->getBeginLoc(),
                                              RC.getBeginLoc())) {
    // If they are, just pop a few last comments that don't fit.
    // This happens if an \#include directive contains comments.
    Comments.pop_back();
  }

  // Ordinary comments are not interesting for us.
  if (RC.isOrdinary() && !CommentOpts.ParseAllComments)
    return;

  // If this is the first Doxygen comment, save it (because there isn't
  // anything to merge it with).
  if (Comments.empty()) {
    Comments.push_back(new (Allocator) RawComment(RC));
    return;
  }

  const RawComment &C1 = *Comments.back();
  const RawComment &C2 = RC;

  // Merge comments only if there is only whitespace between them.
  // Can't merge trailing and non-trailing comments unless the second is
  // non-trailing ordinary in the same column, as in the case:
  //   int x; // documents x
  //          // more text
  // versus:
  //   int x; // documents x
  //   int y; // documents y
  // or:
  //   int x; // documents x
  //   // documents y
  //   int y;
  // Merge comments if they are on same or consecutive lines.
  if ((C1.isTrailingComment() == C2.isTrailingComment() ||
       (C1.isTrailingComment() && !C2.isTrailingComment() &&
        isOrdinaryKind(C2.getKind()) &&
        commentsStartOnSameColumn(SourceMgr, C1, C2))) &&
      onlyWhitespaceBetween(SourceMgr, C1.getEndLoc(), C2.getBeginLoc(),
                            /*MaxNewlinesAllowed=*/1)) {
    SourceRange MergedRange(C1.getBeginLoc(), C2.getEndLoc());
    *Comments.back() = RawComment(SourceMgr, MergedRange, CommentOpts, true);
  } else {
    Comments.push_back(new (Allocator) RawComment(RC));
  }
}

void RawCommentList::addDeserializedComments(ArrayRef<RawComment *> DeserializedComments) {
  std::vector<RawComment *> MergedComments;
  MergedComments.reserve(Comments.size() + DeserializedComments.size());

  std::merge(Comments.begin(), Comments.end(),
             DeserializedComments.begin(), DeserializedComments.end(),
             std::back_inserter(MergedComments),
             BeforeThanCompare<RawComment>(SourceMgr));
  std::swap(Comments, MergedComments);
}

std::string RawComment::getFormattedText(const SourceManager &SourceMgr,
                                         DiagnosticsEngine &Diags) const {
  llvm::StringRef CommentText = getRawText(SourceMgr);
  if (CommentText.empty())
    return "";

  llvm::BumpPtrAllocator Allocator;
  // We do not parse any commands, so CommentOptions are ignored by
  // comments::Lexer. Therefore, we just use default-constructed options.
  CommentOptions DefOpts;
  comments::CommandTraits EmptyTraits(Allocator, DefOpts);
  comments::Lexer L(Allocator, Diags, EmptyTraits, getSourceRange().getBegin(),
                    CommentText.begin(), CommentText.end(),
                    /*ParseCommands=*/false);

  std::string Result;
  // A column number of the first non-whitespace token in the comment text.
  // We skip whitespace up to this column, but keep the whitespace after this
  // column. IndentColumn is calculated when lexing the first line and reused
  // for the rest of lines.
  unsigned IndentColumn = 0;

  // Processes one line of the comment and adds it to the result.
  // Handles skipping the indent at the start of the line.
  // Returns false when eof is reached and true otherwise.
  auto LexLine = [&](bool IsFirstLine) -> bool {
    comments::Token Tok;
    // Lex the first token on the line. We handle it separately, because we to
    // fix up its indentation.
    L.lex(Tok);
    if (Tok.is(comments::tok::eof))
      return false;
    if (Tok.is(comments::tok::newline)) {
      Result += "\n";
      return true;
    }
    llvm::StringRef TokText = L.getSpelling(Tok, SourceMgr);
    bool LocInvalid = false;
    unsigned TokColumn =
        SourceMgr.getSpellingColumnNumber(Tok.getLocation(), &LocInvalid);
    assert(!LocInvalid && "getFormattedText for invalid location");

    // Amount of leading whitespace in TokText.
    size_t WhitespaceLen = TokText.find_first_not_of(" \t");
    if (WhitespaceLen == StringRef::npos)
      WhitespaceLen = TokText.size();
    // Remember the amount of whitespace we skipped in the first line to remove
    // indent up to that column in the following lines.
    if (IsFirstLine)
      IndentColumn = TokColumn + WhitespaceLen;

    // Amount of leading whitespace we actually want to skip.
    // For the first line we skip all the whitespace.
    // For the rest of the lines, we skip whitespace up to IndentColumn.
    unsigned SkipLen =
        IsFirstLine
            ? WhitespaceLen
            : std::min<size_t>(
                  WhitespaceLen,
                  std::max<int>(static_cast<int>(IndentColumn) - TokColumn, 0));
    llvm::StringRef Trimmed = TokText.drop_front(SkipLen);
    Result += Trimmed;
    // Lex all tokens in the rest of the line.
    for (L.lex(Tok); Tok.isNot(comments::tok::eof); L.lex(Tok)) {
      if (Tok.is(comments::tok::newline)) {
        Result += "\n";
        return true;
      }
      Result += L.getSpelling(Tok, SourceMgr);
    }
    // We've reached the end of file token.
    return false;
  };

  auto DropTrailingNewLines = [](std::string &Str) {
    while (Str.back() == '\n')
      Str.pop_back();
  };

  // Process first line separately to remember indent for the following lines.
  if (!LexLine(/*IsFirstLine=*/true)) {
    DropTrailingNewLines(Result);
    return Result;
  }
  // Process the rest of the lines.
  while (LexLine(/*IsFirstLine=*/false))
    ;
  DropTrailingNewLines(Result);
  return Result;
}
