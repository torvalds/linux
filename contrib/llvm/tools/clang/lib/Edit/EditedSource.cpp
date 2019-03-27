//===- EditedSource.cpp - Collection of source edits ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Edit/EditedSource.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Edit/Commit.h"
#include "clang/Edit/EditsReceiver.h"
#include "clang/Edit/FileOffset.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>

using namespace clang;
using namespace edit;

void EditsReceiver::remove(CharSourceRange range) {
  replace(range, StringRef());
}

void EditedSource::deconstructMacroArgLoc(SourceLocation Loc,
                                          SourceLocation &ExpansionLoc,
                                          MacroArgUse &ArgUse) {
  assert(SourceMgr.isMacroArgExpansion(Loc));
  SourceLocation DefArgLoc =
      SourceMgr.getImmediateExpansionRange(Loc).getBegin();
  SourceLocation ImmediateExpansionLoc =
      SourceMgr.getImmediateExpansionRange(DefArgLoc).getBegin();
  ExpansionLoc = ImmediateExpansionLoc;
  while (SourceMgr.isMacroBodyExpansion(ExpansionLoc))
    ExpansionLoc =
        SourceMgr.getImmediateExpansionRange(ExpansionLoc).getBegin();
  SmallString<20> Buf;
  StringRef ArgName = Lexer::getSpelling(SourceMgr.getSpellingLoc(DefArgLoc),
                                         Buf, SourceMgr, LangOpts);
  ArgUse = MacroArgUse{nullptr, SourceLocation(), SourceLocation()};
  if (!ArgName.empty())
    ArgUse = {&IdentTable.get(ArgName), ImmediateExpansionLoc,
              SourceMgr.getSpellingLoc(DefArgLoc)};
}

void EditedSource::startingCommit() {}

void EditedSource::finishedCommit() {
  for (auto &ExpArg : CurrCommitMacroArgExps) {
    SourceLocation ExpLoc;
    MacroArgUse ArgUse;
    std::tie(ExpLoc, ArgUse) = ExpArg;
    auto &ArgUses = ExpansionToArgMap[ExpLoc.getRawEncoding()];
    if (std::find(ArgUses.begin(), ArgUses.end(), ArgUse) == ArgUses.end())
      ArgUses.push_back(ArgUse);
  }
  CurrCommitMacroArgExps.clear();
}

StringRef EditedSource::copyString(const Twine &twine) {
  SmallString<128> Data;
  return copyString(twine.toStringRef(Data));
}

bool EditedSource::canInsertInOffset(SourceLocation OrigLoc, FileOffset Offs) {
  FileEditsTy::iterator FA = getActionForOffset(Offs);
  if (FA != FileEdits.end()) {
    if (FA->first != Offs)
      return false; // position has been removed.
  }

  if (SourceMgr.isMacroArgExpansion(OrigLoc)) {
    SourceLocation ExpLoc;
    MacroArgUse ArgUse;
    deconstructMacroArgLoc(OrigLoc, ExpLoc, ArgUse);
    auto I = ExpansionToArgMap.find(ExpLoc.getRawEncoding());
    if (I != ExpansionToArgMap.end() &&
        find_if(I->second, [&](const MacroArgUse &U) {
          return ArgUse.Identifier == U.Identifier &&
                 std::tie(ArgUse.ImmediateExpansionLoc, ArgUse.UseLoc) !=
                     std::tie(U.ImmediateExpansionLoc, U.UseLoc);
        }) != I->second.end()) {
      // Trying to write in a macro argument input that has already been
      // written by a previous commit for another expansion of the same macro
      // argument name. For example:
      //
      // \code
      //   #define MAC(x) ((x)+(x))
      //   MAC(a)
      // \endcode
      //
      // A commit modified the macro argument 'a' due to the first '(x)'
      // expansion inside the macro definition, and a subsequent commit tried
      // to modify 'a' again for the second '(x)' expansion. The edits of the
      // second commit will be rejected.
      return false;
    }
  }
  return true;
}

bool EditedSource::commitInsert(SourceLocation OrigLoc,
                                FileOffset Offs, StringRef text,
                                bool beforePreviousInsertions) {
  if (!canInsertInOffset(OrigLoc, Offs))
    return false;
  if (text.empty())
    return true;

  if (SourceMgr.isMacroArgExpansion(OrigLoc)) {
    MacroArgUse ArgUse;
    SourceLocation ExpLoc;
    deconstructMacroArgLoc(OrigLoc, ExpLoc, ArgUse);
    if (ArgUse.Identifier)
      CurrCommitMacroArgExps.emplace_back(ExpLoc, ArgUse);
  }

  FileEdit &FA = FileEdits[Offs];
  if (FA.Text.empty()) {
    FA.Text = copyString(text);
    return true;
  }

  if (beforePreviousInsertions)
    FA.Text = copyString(Twine(text) + FA.Text);
  else
    FA.Text = copyString(Twine(FA.Text) + text);

  return true;
}

bool EditedSource::commitInsertFromRange(SourceLocation OrigLoc,
                                   FileOffset Offs,
                                   FileOffset InsertFromRangeOffs, unsigned Len,
                                   bool beforePreviousInsertions) {
  if (Len == 0)
    return true;

  SmallString<128> StrVec;
  FileOffset BeginOffs = InsertFromRangeOffs;
  FileOffset EndOffs = BeginOffs.getWithOffset(Len);
  FileEditsTy::iterator I = FileEdits.upper_bound(BeginOffs);
  if (I != FileEdits.begin())
    --I;

  for (; I != FileEdits.end(); ++I) {
    FileEdit &FA = I->second;
    FileOffset B = I->first;
    FileOffset E = B.getWithOffset(FA.RemoveLen);

    if (BeginOffs == B)
      break;

    if (BeginOffs < E) {
      if (BeginOffs > B) {
        BeginOffs = E;
        ++I;
      }
      break;
    }
  }

  for (; I != FileEdits.end() && EndOffs > I->first; ++I) {
    FileEdit &FA = I->second;
    FileOffset B = I->first;
    FileOffset E = B.getWithOffset(FA.RemoveLen);

    if (BeginOffs < B) {
      bool Invalid = false;
      StringRef text = getSourceText(BeginOffs, B, Invalid);
      if (Invalid)
        return false;
      StrVec += text;
    }
    StrVec += FA.Text;
    BeginOffs = E;
  }

  if (BeginOffs < EndOffs) {
    bool Invalid = false;
    StringRef text = getSourceText(BeginOffs, EndOffs, Invalid);
    if (Invalid)
      return false;
    StrVec += text;
  }

  return commitInsert(OrigLoc, Offs, StrVec, beforePreviousInsertions);
}

void EditedSource::commitRemove(SourceLocation OrigLoc,
                                FileOffset BeginOffs, unsigned Len) {
  if (Len == 0)
    return;

  FileOffset EndOffs = BeginOffs.getWithOffset(Len);
  FileEditsTy::iterator I = FileEdits.upper_bound(BeginOffs);
  if (I != FileEdits.begin())
    --I;

  for (; I != FileEdits.end(); ++I) {
    FileEdit &FA = I->second;
    FileOffset B = I->first;
    FileOffset E = B.getWithOffset(FA.RemoveLen);

    if (BeginOffs < E)
      break;
  }

  FileOffset TopBegin, TopEnd;
  FileEdit *TopFA = nullptr;

  if (I == FileEdits.end()) {
    FileEditsTy::iterator
      NewI = FileEdits.insert(I, std::make_pair(BeginOffs, FileEdit()));
    NewI->second.RemoveLen = Len;
    return;
  }

  FileEdit &FA = I->second;
  FileOffset B = I->first;
  FileOffset E = B.getWithOffset(FA.RemoveLen);
  if (BeginOffs < B) {
    FileEditsTy::iterator
      NewI = FileEdits.insert(I, std::make_pair(BeginOffs, FileEdit()));
    TopBegin = BeginOffs;
    TopEnd = EndOffs;
    TopFA = &NewI->second;
    TopFA->RemoveLen = Len;
  } else {
    TopBegin = B;
    TopEnd = E;
    TopFA = &I->second;
    if (TopEnd >= EndOffs)
      return;
    unsigned diff = EndOffs.getOffset() - TopEnd.getOffset();
    TopEnd = EndOffs;
    TopFA->RemoveLen += diff;
    if (B == BeginOffs)
      TopFA->Text = StringRef();
    ++I;
  }

  while (I != FileEdits.end()) {
    FileEdit &FA = I->second;
    FileOffset B = I->first;
    FileOffset E = B.getWithOffset(FA.RemoveLen);

    if (B >= TopEnd)
      break;

    if (E <= TopEnd) {
      FileEdits.erase(I++);
      continue;
    }

    if (B < TopEnd) {
      unsigned diff = E.getOffset() - TopEnd.getOffset();
      TopEnd = E;
      TopFA->RemoveLen += diff;
      FileEdits.erase(I);
    }

    break;
  }
}

bool EditedSource::commit(const Commit &commit) {
  if (!commit.isCommitable())
    return false;

  struct CommitRAII {
    EditedSource &Editor;

    CommitRAII(EditedSource &Editor) : Editor(Editor) {
      Editor.startingCommit();
    }

    ~CommitRAII() {
      Editor.finishedCommit();
    }
  } CommitRAII(*this);

  for (edit::Commit::edit_iterator
         I = commit.edit_begin(), E = commit.edit_end(); I != E; ++I) {
    const edit::Commit::Edit &edit = *I;
    switch (edit.Kind) {
    case edit::Commit::Act_Insert:
      commitInsert(edit.OrigLoc, edit.Offset, edit.Text, edit.BeforePrev);
      break;
    case edit::Commit::Act_InsertFromRange:
      commitInsertFromRange(edit.OrigLoc, edit.Offset,
                            edit.InsertFromRangeOffs, edit.Length,
                            edit.BeforePrev);
      break;
    case edit::Commit::Act_Remove:
      commitRemove(edit.OrigLoc, edit.Offset, edit.Length);
      break;
    }
  }

  return true;
}

// Returns true if it is ok to make the two given characters adjacent.
static bool canBeJoined(char left, char right, const LangOptions &LangOpts) {
  // FIXME: Should use TokenConcatenation to make sure we don't allow stuff like
  // making two '<' adjacent.
  return !(Lexer::isIdentifierBodyChar(left, LangOpts) &&
           Lexer::isIdentifierBodyChar(right, LangOpts));
}

/// Returns true if it is ok to eliminate the trailing whitespace between
/// the given characters.
static bool canRemoveWhitespace(char left, char beforeWSpace, char right,
                                const LangOptions &LangOpts) {
  if (!canBeJoined(left, right, LangOpts))
    return false;
  if (isWhitespace(left) || isWhitespace(right))
    return true;
  if (canBeJoined(beforeWSpace, right, LangOpts))
    return false; // the whitespace was intentional, keep it.
  return true;
}

/// Check the range that we are going to remove and:
/// -Remove any trailing whitespace if possible.
/// -Insert a space if removing the range is going to mess up the source tokens.
static void adjustRemoval(const SourceManager &SM, const LangOptions &LangOpts,
                          SourceLocation Loc, FileOffset offs,
                          unsigned &len, StringRef &text) {
  assert(len && text.empty());
  SourceLocation BeginTokLoc = Lexer::GetBeginningOfToken(Loc, SM, LangOpts);
  if (BeginTokLoc != Loc)
    return; // the range is not at the beginning of a token, keep the range.

  bool Invalid = false;
  StringRef buffer = SM.getBufferData(offs.getFID(), &Invalid);
  if (Invalid)
    return;

  unsigned begin = offs.getOffset();
  unsigned end = begin + len;

  // Do not try to extend the removal if we're at the end of the buffer already.
  if (end == buffer.size())
    return;

  assert(begin < buffer.size() && end < buffer.size() && "Invalid range!");

  // FIXME: Remove newline.

  if (begin == 0) {
    if (buffer[end] == ' ')
      ++len;
    return;
  }

  if (buffer[end] == ' ') {
    assert((end + 1 != buffer.size() || buffer.data()[end + 1] == 0) &&
           "buffer not zero-terminated!");
    if (canRemoveWhitespace(/*left=*/buffer[begin-1],
                            /*beforeWSpace=*/buffer[end-1],
                            /*right=*/buffer.data()[end + 1], // zero-terminated
                            LangOpts))
      ++len;
    return;
  }

  if (!canBeJoined(buffer[begin-1], buffer[end], LangOpts))
    text = " ";
}

static void applyRewrite(EditsReceiver &receiver,
                         StringRef text, FileOffset offs, unsigned len,
                         const SourceManager &SM, const LangOptions &LangOpts,
                         bool shouldAdjustRemovals) {
  assert(offs.getFID().isValid());
  SourceLocation Loc = SM.getLocForStartOfFile(offs.getFID());
  Loc = Loc.getLocWithOffset(offs.getOffset());
  assert(Loc.isFileID());

  if (text.empty() && shouldAdjustRemovals)
    adjustRemoval(SM, LangOpts, Loc, offs, len, text);

  CharSourceRange range = CharSourceRange::getCharRange(Loc,
                                                     Loc.getLocWithOffset(len));

  if (text.empty()) {
    assert(len);
    receiver.remove(range);
    return;
  }

  if (len)
    receiver.replace(range, text);
  else
    receiver.insert(Loc, text);
}

void EditedSource::applyRewrites(EditsReceiver &receiver,
                                 bool shouldAdjustRemovals) {
  SmallString<128> StrVec;
  FileOffset CurOffs, CurEnd;
  unsigned CurLen;

  if (FileEdits.empty())
    return;

  FileEditsTy::iterator I = FileEdits.begin();
  CurOffs = I->first;
  StrVec = I->second.Text;
  CurLen = I->second.RemoveLen;
  CurEnd = CurOffs.getWithOffset(CurLen);
  ++I;

  for (FileEditsTy::iterator E = FileEdits.end(); I != E; ++I) {
    FileOffset offs = I->first;
    FileEdit act = I->second;
    assert(offs >= CurEnd);

    if (offs == CurEnd) {
      StrVec += act.Text;
      CurLen += act.RemoveLen;
      CurEnd.getWithOffset(act.RemoveLen);
      continue;
    }

    applyRewrite(receiver, StrVec, CurOffs, CurLen, SourceMgr, LangOpts,
                 shouldAdjustRemovals);
    CurOffs = offs;
    StrVec = act.Text;
    CurLen = act.RemoveLen;
    CurEnd = CurOffs.getWithOffset(CurLen);
  }

  applyRewrite(receiver, StrVec, CurOffs, CurLen, SourceMgr, LangOpts,
               shouldAdjustRemovals);
}

void EditedSource::clearRewrites() {
  FileEdits.clear();
  StrAlloc.Reset();
}

StringRef EditedSource::getSourceText(FileOffset BeginOffs, FileOffset EndOffs,
                                      bool &Invalid) {
  assert(BeginOffs.getFID() == EndOffs.getFID());
  assert(BeginOffs <= EndOffs);
  SourceLocation BLoc = SourceMgr.getLocForStartOfFile(BeginOffs.getFID());
  BLoc = BLoc.getLocWithOffset(BeginOffs.getOffset());
  assert(BLoc.isFileID());
  SourceLocation
    ELoc = BLoc.getLocWithOffset(EndOffs.getOffset() - BeginOffs.getOffset());
  return Lexer::getSourceText(CharSourceRange::getCharRange(BLoc, ELoc),
                              SourceMgr, LangOpts, &Invalid);
}

EditedSource::FileEditsTy::iterator
EditedSource::getActionForOffset(FileOffset Offs) {
  FileEditsTy::iterator I = FileEdits.upper_bound(Offs);
  if (I == FileEdits.begin())
    return FileEdits.end();
  --I;
  FileEdit &FA = I->second;
  FileOffset B = I->first;
  FileOffset E = B.getWithOffset(FA.RemoveLen);
  if (Offs >= B && Offs < E)
    return I;

  return FileEdits.end();
}
