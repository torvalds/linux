//===- SourceMgr.cpp - Manager for Simple Source Buffers & Diagnostics ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SourceMgr class.  This class is used as a simple
// substrate for diagnostics, #include handling, and other low level things for
// simple parsers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SourceMgr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>

using namespace llvm;

static const size_t TabStop = 8;

unsigned SourceMgr::AddIncludeFile(const std::string &Filename,
                                   SMLoc IncludeLoc,
                                   std::string &IncludedFile) {
  IncludedFile = Filename;
  ErrorOr<std::unique_ptr<MemoryBuffer>> NewBufOrErr =
    MemoryBuffer::getFile(IncludedFile);

  // If the file didn't exist directly, see if it's in an include path.
  for (unsigned i = 0, e = IncludeDirectories.size(); i != e && !NewBufOrErr;
       ++i) {
    IncludedFile =
        IncludeDirectories[i] + sys::path::get_separator().data() + Filename;
    NewBufOrErr = MemoryBuffer::getFile(IncludedFile);
  }

  if (!NewBufOrErr)
    return 0;

  return AddNewSourceBuffer(std::move(*NewBufOrErr), IncludeLoc);
}

unsigned SourceMgr::FindBufferContainingLoc(SMLoc Loc) const {
  for (unsigned i = 0, e = Buffers.size(); i != e; ++i)
    if (Loc.getPointer() >= Buffers[i].Buffer->getBufferStart() &&
        // Use <= here so that a pointer to the null at the end of the buffer
        // is included as part of the buffer.
        Loc.getPointer() <= Buffers[i].Buffer->getBufferEnd())
      return i + 1;
  return 0;
}

template <typename T>
unsigned SourceMgr::SrcBuffer::getLineNumber(const char *Ptr) const {

  // Ensure OffsetCache is allocated and populated with offsets of all the
  // '\n' bytes.
  std::vector<T> *Offsets = nullptr;
  if (OffsetCache.isNull()) {
    Offsets = new std::vector<T>();
    OffsetCache = Offsets;
    size_t Sz = Buffer->getBufferSize();
    assert(Sz <= std::numeric_limits<T>::max());
    StringRef S = Buffer->getBuffer();
    for (size_t N = 0; N < Sz; ++N) {
      if (S[N] == '\n') {
        Offsets->push_back(static_cast<T>(N));
      }
    }
  } else {
    Offsets = OffsetCache.get<std::vector<T> *>();
  }

  const char *BufStart = Buffer->getBufferStart();
  assert(Ptr >= BufStart && Ptr <= Buffer->getBufferEnd());
  ptrdiff_t PtrDiff = Ptr - BufStart;
  assert(PtrDiff >= 0 && static_cast<size_t>(PtrDiff) <= std::numeric_limits<T>::max());
  T PtrOffset = static_cast<T>(PtrDiff);

  // std::lower_bound returns the first EOL offset that's not-less-than
  // PtrOffset, meaning the EOL that _ends the line_ that PtrOffset is on
  // (including if PtrOffset refers to the EOL itself). If there's no such
  // EOL, returns end().
  auto EOL = std::lower_bound(Offsets->begin(), Offsets->end(), PtrOffset);

  // Lines count from 1, so add 1 to the distance from the 0th line.
  return (1 + (EOL - Offsets->begin()));
}

SourceMgr::SrcBuffer::SrcBuffer(SourceMgr::SrcBuffer &&Other)
  : Buffer(std::move(Other.Buffer)),
    OffsetCache(Other.OffsetCache),
    IncludeLoc(Other.IncludeLoc) {
  Other.OffsetCache = nullptr;
}

SourceMgr::SrcBuffer::~SrcBuffer() {
  if (!OffsetCache.isNull()) {
    if (OffsetCache.is<std::vector<uint8_t>*>())
      delete OffsetCache.get<std::vector<uint8_t>*>();
    else if (OffsetCache.is<std::vector<uint16_t>*>())
      delete OffsetCache.get<std::vector<uint16_t>*>();
    else if (OffsetCache.is<std::vector<uint32_t>*>())
      delete OffsetCache.get<std::vector<uint32_t>*>();
    else
      delete OffsetCache.get<std::vector<uint64_t>*>();
    OffsetCache = nullptr;
  }
}

std::pair<unsigned, unsigned>
SourceMgr::getLineAndColumn(SMLoc Loc, unsigned BufferID) const {
  if (!BufferID)
    BufferID = FindBufferContainingLoc(Loc);
  assert(BufferID && "Invalid Location!");

  auto &SB = getBufferInfo(BufferID);
  const char *Ptr = Loc.getPointer();

  size_t Sz = SB.Buffer->getBufferSize();
  unsigned LineNo;
  if (Sz <= std::numeric_limits<uint8_t>::max())
    LineNo = SB.getLineNumber<uint8_t>(Ptr);
  else if (Sz <= std::numeric_limits<uint16_t>::max())
    LineNo = SB.getLineNumber<uint16_t>(Ptr);
  else if (Sz <= std::numeric_limits<uint32_t>::max())
    LineNo = SB.getLineNumber<uint32_t>(Ptr);
  else
    LineNo = SB.getLineNumber<uint64_t>(Ptr);

  const char *BufStart = SB.Buffer->getBufferStart();
  size_t NewlineOffs = StringRef(BufStart, Ptr-BufStart).find_last_of("\n\r");
  if (NewlineOffs == StringRef::npos) NewlineOffs = ~(size_t)0;
  return std::make_pair(LineNo, Ptr-BufStart-NewlineOffs);
}

void SourceMgr::PrintIncludeStack(SMLoc IncludeLoc, raw_ostream &OS) const {
  if (IncludeLoc == SMLoc()) return;  // Top of stack.

  unsigned CurBuf = FindBufferContainingLoc(IncludeLoc);
  assert(CurBuf && "Invalid or unspecified location!");

  PrintIncludeStack(getBufferInfo(CurBuf).IncludeLoc, OS);

  OS << "Included from "
     << getBufferInfo(CurBuf).Buffer->getBufferIdentifier()
     << ":" << FindLineNumber(IncludeLoc, CurBuf) << ":\n";
}

SMDiagnostic SourceMgr::GetMessage(SMLoc Loc, SourceMgr::DiagKind Kind,
                                   const Twine &Msg,
                                   ArrayRef<SMRange> Ranges,
                                   ArrayRef<SMFixIt> FixIts) const {
  // First thing to do: find the current buffer containing the specified
  // location to pull out the source line.
  SmallVector<std::pair<unsigned, unsigned>, 4> ColRanges;
  std::pair<unsigned, unsigned> LineAndCol;
  StringRef BufferID = "<unknown>";
  std::string LineStr;

  if (Loc.isValid()) {
    unsigned CurBuf = FindBufferContainingLoc(Loc);
    assert(CurBuf && "Invalid or unspecified location!");

    const MemoryBuffer *CurMB = getMemoryBuffer(CurBuf);
    BufferID = CurMB->getBufferIdentifier();

    // Scan backward to find the start of the line.
    const char *LineStart = Loc.getPointer();
    const char *BufStart = CurMB->getBufferStart();
    while (LineStart != BufStart && LineStart[-1] != '\n' &&
           LineStart[-1] != '\r')
      --LineStart;

    // Get the end of the line.
    const char *LineEnd = Loc.getPointer();
    const char *BufEnd = CurMB->getBufferEnd();
    while (LineEnd != BufEnd && LineEnd[0] != '\n' && LineEnd[0] != '\r')
      ++LineEnd;
    LineStr = std::string(LineStart, LineEnd);

    // Convert any ranges to column ranges that only intersect the line of the
    // location.
    for (unsigned i = 0, e = Ranges.size(); i != e; ++i) {
      SMRange R = Ranges[i];
      if (!R.isValid()) continue;

      // If the line doesn't contain any part of the range, then ignore it.
      if (R.Start.getPointer() > LineEnd || R.End.getPointer() < LineStart)
        continue;

      // Ignore pieces of the range that go onto other lines.
      if (R.Start.getPointer() < LineStart)
        R.Start = SMLoc::getFromPointer(LineStart);
      if (R.End.getPointer() > LineEnd)
        R.End = SMLoc::getFromPointer(LineEnd);

      // Translate from SMLoc ranges to column ranges.
      // FIXME: Handle multibyte characters.
      ColRanges.push_back(std::make_pair(R.Start.getPointer()-LineStart,
                                         R.End.getPointer()-LineStart));
    }

    LineAndCol = getLineAndColumn(Loc, CurBuf);
  }

  return SMDiagnostic(*this, Loc, BufferID, LineAndCol.first,
                      LineAndCol.second-1, Kind, Msg.str(),
                      LineStr, ColRanges, FixIts);
}

void SourceMgr::PrintMessage(raw_ostream &OS, const SMDiagnostic &Diagnostic,
                             bool ShowColors) const {
  // Report the message with the diagnostic handler if present.
  if (DiagHandler) {
    DiagHandler(Diagnostic, DiagContext);
    return;
  }

  if (Diagnostic.getLoc().isValid()) {
    unsigned CurBuf = FindBufferContainingLoc(Diagnostic.getLoc());
    assert(CurBuf && "Invalid or unspecified location!");
    PrintIncludeStack(getBufferInfo(CurBuf).IncludeLoc, OS);
  }

  Diagnostic.print(nullptr, OS, ShowColors);
}

void SourceMgr::PrintMessage(raw_ostream &OS, SMLoc Loc,
                             SourceMgr::DiagKind Kind,
                             const Twine &Msg, ArrayRef<SMRange> Ranges,
                             ArrayRef<SMFixIt> FixIts, bool ShowColors) const {
  PrintMessage(OS, GetMessage(Loc, Kind, Msg, Ranges, FixIts), ShowColors);
}

void SourceMgr::PrintMessage(SMLoc Loc, SourceMgr::DiagKind Kind,
                             const Twine &Msg, ArrayRef<SMRange> Ranges,
                             ArrayRef<SMFixIt> FixIts, bool ShowColors) const {
  PrintMessage(errs(), Loc, Kind, Msg, Ranges, FixIts, ShowColors);
}

//===----------------------------------------------------------------------===//
// SMDiagnostic Implementation
//===----------------------------------------------------------------------===//

SMDiagnostic::SMDiagnostic(const SourceMgr &sm, SMLoc L, StringRef FN,
                           int Line, int Col, SourceMgr::DiagKind Kind,
                           StringRef Msg, StringRef LineStr,
                           ArrayRef<std::pair<unsigned,unsigned>> Ranges,
                           ArrayRef<SMFixIt> Hints)
  : SM(&sm), Loc(L), Filename(FN), LineNo(Line), ColumnNo(Col), Kind(Kind),
    Message(Msg), LineContents(LineStr), Ranges(Ranges.vec()),
    FixIts(Hints.begin(), Hints.end()) {
  llvm::sort(FixIts);
}

static void buildFixItLine(std::string &CaretLine, std::string &FixItLine,
                           ArrayRef<SMFixIt> FixIts, ArrayRef<char> SourceLine){
  if (FixIts.empty())
    return;

  const char *LineStart = SourceLine.begin();
  const char *LineEnd = SourceLine.end();

  size_t PrevHintEndCol = 0;

  for (ArrayRef<SMFixIt>::iterator I = FixIts.begin(), E = FixIts.end();
       I != E; ++I) {
    // If the fixit contains a newline or tab, ignore it.
    if (I->getText().find_first_of("\n\r\t") != StringRef::npos)
      continue;

    SMRange R = I->getRange();

    // If the line doesn't contain any part of the range, then ignore it.
    if (R.Start.getPointer() > LineEnd || R.End.getPointer() < LineStart)
      continue;

    // Translate from SMLoc to column.
    // Ignore pieces of the range that go onto other lines.
    // FIXME: Handle multibyte characters in the source line.
    unsigned FirstCol;
    if (R.Start.getPointer() < LineStart)
      FirstCol = 0;
    else
      FirstCol = R.Start.getPointer() - LineStart;

    // If we inserted a long previous hint, push this one forwards, and add
    // an extra space to show that this is not part of the previous
    // completion. This is sort of the best we can do when two hints appear
    // to overlap.
    //
    // Note that if this hint is located immediately after the previous
    // hint, no space will be added, since the location is more important.
    unsigned HintCol = FirstCol;
    if (HintCol < PrevHintEndCol)
      HintCol = PrevHintEndCol + 1;

    // FIXME: This assertion is intended to catch unintended use of multibyte
    // characters in fixits. If we decide to do this, we'll have to track
    // separate byte widths for the source and fixit lines.
    assert((size_t)sys::locale::columnWidth(I->getText()) ==
           I->getText().size());

    // This relies on one byte per column in our fixit hints.
    unsigned LastColumnModified = HintCol + I->getText().size();
    if (LastColumnModified > FixItLine.size())
      FixItLine.resize(LastColumnModified, ' ');

    std::copy(I->getText().begin(), I->getText().end(),
              FixItLine.begin() + HintCol);

    PrevHintEndCol = LastColumnModified;

    // For replacements, mark the removal range with '~'.
    // FIXME: Handle multibyte characters in the source line.
    unsigned LastCol;
    if (R.End.getPointer() >= LineEnd)
      LastCol = LineEnd - LineStart;
    else
      LastCol = R.End.getPointer() - LineStart;

    std::fill(&CaretLine[FirstCol], &CaretLine[LastCol], '~');
  }
}

static void printSourceLine(raw_ostream &S, StringRef LineContents) {
  // Print out the source line one character at a time, so we can expand tabs.
  for (unsigned i = 0, e = LineContents.size(), OutCol = 0; i != e; ++i) {
    size_t NextTab = LineContents.find('\t', i);
    // If there were no tabs left, print the rest, we are done.
    if (NextTab == StringRef::npos) {
      S << LineContents.drop_front(i);
      break;
    }

    // Otherwise, print from i to NextTab.
    S << LineContents.slice(i, NextTab);
    OutCol += NextTab - i;
    i = NextTab;

    // If we have a tab, emit at least one space, then round up to 8 columns.
    do {
      S << ' ';
      ++OutCol;
    } while ((OutCol % TabStop) != 0);
  }
  S << '\n';
}

static bool isNonASCII(char c) {
  return c & 0x80;
}

void SMDiagnostic::print(const char *ProgName, raw_ostream &OS,
                         bool ShowColors, bool ShowKindLabel) const {
  {
    WithColor S(OS, raw_ostream::SAVEDCOLOR, true, false, !ShowColors);

    if (ProgName && ProgName[0])
      S << ProgName << ": ";

    if (!Filename.empty()) {
      if (Filename == "-")
        S << "<stdin>";
      else
        S << Filename;

      if (LineNo != -1) {
        S << ':' << LineNo;
        if (ColumnNo != -1)
          S << ':' << (ColumnNo + 1);
      }
      S << ": ";
    }
  }

  if (ShowKindLabel) {
    switch (Kind) {
    case SourceMgr::DK_Error:
      WithColor::error(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Warning:
      WithColor::warning(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Note:
      WithColor::note(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Remark:
      WithColor::remark(OS, "", !ShowColors);
      break;
    }
  }

  WithColor(OS, raw_ostream::SAVEDCOLOR, true, false, !ShowColors)
      << Message << '\n';

  if (LineNo == -1 || ColumnNo == -1)
    return;

  // FIXME: If there are multibyte or multi-column characters in the source, all
  // our ranges will be wrong. To do this properly, we'll need a byte-to-column
  // map like Clang's TextDiagnostic. For now, we'll just handle tabs by
  // expanding them later, and bail out rather than show incorrect ranges and
  // misaligned fixits for any other odd characters.
  if (find_if(LineContents, isNonASCII) != LineContents.end()) {
    printSourceLine(OS, LineContents);
    return;
  }
  size_t NumColumns = LineContents.size();

  // Build the line with the caret and ranges.
  std::string CaretLine(NumColumns+1, ' ');

  // Expand any ranges.
  for (unsigned r = 0, e = Ranges.size(); r != e; ++r) {
    std::pair<unsigned, unsigned> R = Ranges[r];
    std::fill(&CaretLine[R.first],
              &CaretLine[std::min((size_t)R.second, CaretLine.size())],
              '~');
  }

  // Add any fix-its.
  // FIXME: Find the beginning of the line properly for multibyte characters.
  std::string FixItInsertionLine;
  buildFixItLine(CaretLine, FixItInsertionLine, FixIts,
                 makeArrayRef(Loc.getPointer() - ColumnNo,
                              LineContents.size()));

  // Finally, plop on the caret.
  if (unsigned(ColumnNo) <= NumColumns)
    CaretLine[ColumnNo] = '^';
  else
    CaretLine[NumColumns] = '^';

  // ... and remove trailing whitespace so the output doesn't wrap for it.  We
  // know that the line isn't completely empty because it has the caret in it at
  // least.
  CaretLine.erase(CaretLine.find_last_not_of(' ')+1);

  printSourceLine(OS, LineContents);

  {
    WithColor S(OS, raw_ostream::GREEN, true, false, !ShowColors);

    // Print out the caret line, matching tabs in the source line.
    for (unsigned i = 0, e = CaretLine.size(), OutCol = 0; i != e; ++i) {
      if (i >= LineContents.size() || LineContents[i] != '\t') {
        S << CaretLine[i];
        ++OutCol;
        continue;
      }

      // Okay, we have a tab.  Insert the appropriate number of characters.
      do {
        S << CaretLine[i];
        ++OutCol;
      } while ((OutCol % TabStop) != 0);
    }
    S << '\n';
  }

  // Print out the replacement line, matching tabs in the source line.
  if (FixItInsertionLine.empty())
    return;

  for (size_t i = 0, e = FixItInsertionLine.size(), OutCol = 0; i < e; ++i) {
    if (i >= LineContents.size() || LineContents[i] != '\t') {
      OS << FixItInsertionLine[i];
      ++OutCol;
      continue;
    }

    // Okay, we have a tab.  Insert the appropriate number of characters.
    do {
      OS << FixItInsertionLine[i];
      // FIXME: This is trying not to break up replacements, but then to re-sync
      // with the tabs between replacements. This will fail, though, if two
      // fix-it replacements are exactly adjacent, or if a fix-it contains a
      // space. Really we should be precomputing column widths, which we'll
      // need anyway for multibyte chars.
      if (FixItInsertionLine[i] != ' ')
        ++i;
      ++OutCol;
    } while (((OutCol % TabStop) != 0) && i != e);
  }
  OS << '\n';
}
