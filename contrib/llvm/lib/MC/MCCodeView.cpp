//===- MCCodeView.h - Machine Code CodeView support -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Holds state from .cv_file and .cv_loc directives for later emission.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCCodeView.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;
using namespace llvm::codeview;

CodeViewContext::CodeViewContext() {}

CodeViewContext::~CodeViewContext() {
  // If someone inserted strings into the string table but never actually
  // emitted them somewhere, clean up the fragment.
  if (!InsertedStrTabFragment)
    delete StrTabFragment;
}

/// This is a valid number for use with .cv_loc if we've already seen a .cv_file
/// for it.
bool CodeViewContext::isValidFileNumber(unsigned FileNumber) const {
  unsigned Idx = FileNumber - 1;
  if (Idx < Files.size())
    return Files[Idx].Assigned;
  return false;
}

bool CodeViewContext::addFile(MCStreamer &OS, unsigned FileNumber,
                              StringRef Filename,
                              ArrayRef<uint8_t> ChecksumBytes,
                              uint8_t ChecksumKind) {
  assert(FileNumber > 0);
  auto FilenameOffset = addToStringTable(Filename);
  Filename = FilenameOffset.first;
  unsigned Idx = FileNumber - 1;
  if (Idx >= Files.size())
    Files.resize(Idx + 1);

  if (Filename.empty())
    Filename = "<stdin>";

  if (Files[Idx].Assigned)
    return false;

  FilenameOffset = addToStringTable(Filename);
  Filename = FilenameOffset.first;
  unsigned Offset = FilenameOffset.second;

  auto ChecksumOffsetSymbol =
      OS.getContext().createTempSymbol("checksum_offset", false);
  Files[Idx].StringTableOffset = Offset;
  Files[Idx].ChecksumTableOffset = ChecksumOffsetSymbol;
  Files[Idx].Assigned = true;
  Files[Idx].Checksum = ChecksumBytes;
  Files[Idx].ChecksumKind = ChecksumKind;

  return true;
}

MCCVFunctionInfo *CodeViewContext::getCVFunctionInfo(unsigned FuncId) {
  if (FuncId >= Functions.size())
    return nullptr;
  if (Functions[FuncId].isUnallocatedFunctionInfo())
    return nullptr;
  return &Functions[FuncId];
}

bool CodeViewContext::recordFunctionId(unsigned FuncId) {
  if (FuncId >= Functions.size())
    Functions.resize(FuncId + 1);

  // Return false if this function info was already allocated.
  if (!Functions[FuncId].isUnallocatedFunctionInfo())
    return false;

  // Mark this as an allocated normal function, and leave the rest alone.
  Functions[FuncId].ParentFuncIdPlusOne = MCCVFunctionInfo::FunctionSentinel;
  return true;
}

bool CodeViewContext::recordInlinedCallSiteId(unsigned FuncId, unsigned IAFunc,
                                              unsigned IAFile, unsigned IALine,
                                              unsigned IACol) {
  if (FuncId >= Functions.size())
    Functions.resize(FuncId + 1);

  // Return false if this function info was already allocated.
  if (!Functions[FuncId].isUnallocatedFunctionInfo())
    return false;

  MCCVFunctionInfo::LineInfo InlinedAt;
  InlinedAt.File = IAFile;
  InlinedAt.Line = IALine;
  InlinedAt.Col = IACol;

  // Mark this as an inlined call site and record call site line info.
  MCCVFunctionInfo *Info = &Functions[FuncId];
  Info->ParentFuncIdPlusOne = IAFunc + 1;
  Info->InlinedAt = InlinedAt;

  // Walk up the call chain adding this function id to the InlinedAtMap of all
  // transitive callers until we hit a real function.
  while (Info->isInlinedCallSite()) {
    InlinedAt = Info->InlinedAt;
    Info = getCVFunctionInfo(Info->getParentFuncId());
    Info->InlinedAtMap[FuncId] = InlinedAt;
  }

  return true;
}

void CodeViewContext::recordCVLoc(MCContext &Ctx, const MCSymbol *Label,
                                  unsigned FunctionId, unsigned FileNo,
                                  unsigned Line, unsigned Column,
                                  bool PrologueEnd, bool IsStmt) {
  addLineEntry(MCCVLoc{
      Label, FunctionId, FileNo, Line, Column, PrologueEnd, IsStmt});
}

MCDataFragment *CodeViewContext::getStringTableFragment() {
  if (!StrTabFragment) {
    StrTabFragment = new MCDataFragment();
    // Start a new string table out with a null byte.
    StrTabFragment->getContents().push_back('\0');
  }
  return StrTabFragment;
}

std::pair<StringRef, unsigned> CodeViewContext::addToStringTable(StringRef S) {
  SmallVectorImpl<char> &Contents = getStringTableFragment()->getContents();
  auto Insertion =
      StringTable.insert(std::make_pair(S, unsigned(Contents.size())));
  // Return the string from the table, since it is stable.
  std::pair<StringRef, unsigned> Ret =
      std::make_pair(Insertion.first->first(), Insertion.first->second);
  if (Insertion.second) {
    // The string map key is always null terminated.
    Contents.append(Ret.first.begin(), Ret.first.end() + 1);
  }
  return Ret;
}

unsigned CodeViewContext::getStringTableOffset(StringRef S) {
  // A string table offset of zero is always the empty string.
  if (S.empty())
    return 0;
  auto I = StringTable.find(S);
  assert(I != StringTable.end());
  return I->second;
}

void CodeViewContext::emitStringTable(MCObjectStreamer &OS) {
  MCContext &Ctx = OS.getContext();
  MCSymbol *StringBegin = Ctx.createTempSymbol("strtab_begin", false),
           *StringEnd = Ctx.createTempSymbol("strtab_end", false);

  OS.EmitIntValue(unsigned(DebugSubsectionKind::StringTable), 4);
  OS.emitAbsoluteSymbolDiff(StringEnd, StringBegin, 4);
  OS.EmitLabel(StringBegin);

  // Put the string table data fragment here, if we haven't already put it
  // somewhere else. If somebody wants two string tables in their .s file, one
  // will just be empty.
  if (!InsertedStrTabFragment) {
    OS.insert(getStringTableFragment());
    InsertedStrTabFragment = true;
  }

  OS.EmitValueToAlignment(4, 0);

  OS.EmitLabel(StringEnd);
}

void CodeViewContext::emitFileChecksums(MCObjectStreamer &OS) {
  // Do nothing if there are no file checksums. Microsoft's linker rejects empty
  // CodeView substreams.
  if (Files.empty())
    return;

  MCContext &Ctx = OS.getContext();
  MCSymbol *FileBegin = Ctx.createTempSymbol("filechecksums_begin", false),
           *FileEnd = Ctx.createTempSymbol("filechecksums_end", false);

  OS.EmitIntValue(unsigned(DebugSubsectionKind::FileChecksums), 4);
  OS.emitAbsoluteSymbolDiff(FileEnd, FileBegin, 4);
  OS.EmitLabel(FileBegin);

  unsigned CurrentOffset = 0;

  // Emit an array of FileChecksum entries. We index into this table using the
  // user-provided file number.  Each entry may be a variable number of bytes
  // determined by the checksum kind and size.
  for (auto File : Files) {
    OS.EmitAssignment(File.ChecksumTableOffset,
                      MCConstantExpr::create(CurrentOffset, Ctx));
    CurrentOffset += 4; // String table offset.
    if (!File.ChecksumKind) {
      CurrentOffset +=
          4; // One byte each for checksum size and kind, then align to 4 bytes.
    } else {
      CurrentOffset += 2; // One byte each for checksum size and kind.
      CurrentOffset += File.Checksum.size();
      CurrentOffset = alignTo(CurrentOffset, 4);
    }

    OS.EmitIntValue(File.StringTableOffset, 4);

    if (!File.ChecksumKind) {
      // There is no checksum.  Therefore zero the next two fields and align
      // back to 4 bytes.
      OS.EmitIntValue(0, 4);
      continue;
    }
    OS.EmitIntValue(static_cast<uint8_t>(File.Checksum.size()), 1);
    OS.EmitIntValue(File.ChecksumKind, 1);
    OS.EmitBytes(toStringRef(File.Checksum));
    OS.EmitValueToAlignment(4);
  }

  OS.EmitLabel(FileEnd);

  ChecksumOffsetsAssigned = true;
}

// Output checksum table offset of the given file number.  It is possible that
// not all files have been registered yet, and so the offset cannot be
// calculated.  In this case a symbol representing the offset is emitted, and
// the value of this symbol will be fixed up at a later time.
void CodeViewContext::emitFileChecksumOffset(MCObjectStreamer &OS,
                                             unsigned FileNo) {
  unsigned Idx = FileNo - 1;

  if (Idx >= Files.size())
    Files.resize(Idx + 1);

  if (ChecksumOffsetsAssigned) {
    OS.EmitSymbolValue(Files[Idx].ChecksumTableOffset, 4);
    return;
  }

  const MCSymbolRefExpr *SRE =
      MCSymbolRefExpr::create(Files[Idx].ChecksumTableOffset, OS.getContext());

  OS.EmitValueImpl(SRE, 4);
}

void CodeViewContext::addLineEntry(const MCCVLoc &LineEntry) {
  size_t Offset = MCCVLines.size();
  auto I = MCCVLineStartStop.insert(
      {LineEntry.getFunctionId(), {Offset, Offset + 1}});
  if (!I.second)
    I.first->second.second = Offset + 1;
  MCCVLines.push_back(LineEntry);
}

std::vector<MCCVLoc>
CodeViewContext::getFunctionLineEntries(unsigned FuncId) {
  std::vector<MCCVLoc> FilteredLines;
  auto I = MCCVLineStartStop.find(FuncId);
  if (I != MCCVLineStartStop.end()) {
    MCCVFunctionInfo *SiteInfo = getCVFunctionInfo(FuncId);
    for (size_t Idx = I->second.first, End = I->second.second; Idx != End;
         ++Idx) {
      unsigned LocationFuncId = MCCVLines[Idx].getFunctionId();
      if (LocationFuncId == FuncId) {
        // This was a .cv_loc directly for FuncId, so record it.
        FilteredLines.push_back(MCCVLines[Idx]);
      } else {
        // Check if the current location is inlined in this function. If it is,
        // synthesize a statement .cv_loc at the original inlined call site.
        auto I = SiteInfo->InlinedAtMap.find(LocationFuncId);
        if (I != SiteInfo->InlinedAtMap.end()) {
          MCCVFunctionInfo::LineInfo &IA = I->second;
          // Only add the location if it differs from the previous location.
          // Large inlined calls will have many .cv_loc entries and we only need
          // one line table entry in the parent function.
          if (FilteredLines.empty() ||
              FilteredLines.back().getFileNum() != IA.File ||
              FilteredLines.back().getLine() != IA.Line ||
              FilteredLines.back().getColumn() != IA.Col) {
            FilteredLines.push_back(MCCVLoc(
                MCCVLines[Idx].getLabel(),
                FuncId, IA.File, IA.Line, IA.Col, false, false));
          }
        }
      }
    }
  }
  return FilteredLines;
}

std::pair<size_t, size_t> CodeViewContext::getLineExtent(unsigned FuncId) {
  auto I = MCCVLineStartStop.find(FuncId);
  // Return an empty extent if there are no cv_locs for this function id.
  if (I == MCCVLineStartStop.end())
    return {~0ULL, 0};
  return I->second;
}

ArrayRef<MCCVLoc> CodeViewContext::getLinesForExtent(size_t L, size_t R) {
  if (R <= L)
    return None;
  if (L >= MCCVLines.size())
    return None;
  return makeArrayRef(&MCCVLines[L], R - L);
}

void CodeViewContext::emitLineTableForFunction(MCObjectStreamer &OS,
                                               unsigned FuncId,
                                               const MCSymbol *FuncBegin,
                                               const MCSymbol *FuncEnd) {
  MCContext &Ctx = OS.getContext();
  MCSymbol *LineBegin = Ctx.createTempSymbol("linetable_begin", false),
           *LineEnd = Ctx.createTempSymbol("linetable_end", false);

  OS.EmitIntValue(unsigned(DebugSubsectionKind::Lines), 4);
  OS.emitAbsoluteSymbolDiff(LineEnd, LineBegin, 4);
  OS.EmitLabel(LineBegin);
  OS.EmitCOFFSecRel32(FuncBegin, /*Offset=*/0);
  OS.EmitCOFFSectionIndex(FuncBegin);

  // Actual line info.
  std::vector<MCCVLoc> Locs = getFunctionLineEntries(FuncId);
  bool HaveColumns = any_of(Locs, [](const MCCVLoc &LineEntry) {
    return LineEntry.getColumn() != 0;
  });
  OS.EmitIntValue(HaveColumns ? int(LF_HaveColumns) : 0, 2);
  OS.emitAbsoluteSymbolDiff(FuncEnd, FuncBegin, 4);

  for (auto I = Locs.begin(), E = Locs.end(); I != E;) {
    // Emit a file segment for the run of locations that share a file id.
    unsigned CurFileNum = I->getFileNum();
    auto FileSegEnd =
        std::find_if(I, E, [CurFileNum](const MCCVLoc &Loc) {
          return Loc.getFileNum() != CurFileNum;
        });
    unsigned EntryCount = FileSegEnd - I;
    OS.AddComment(
        "Segment for file '" +
        Twine(getStringTableFragment()
                  ->getContents()[Files[CurFileNum - 1].StringTableOffset]) +
        "' begins");
    OS.EmitCVFileChecksumOffsetDirective(CurFileNum);
    OS.EmitIntValue(EntryCount, 4);
    uint32_t SegmentSize = 12;
    SegmentSize += 8 * EntryCount;
    if (HaveColumns)
      SegmentSize += 4 * EntryCount;
    OS.EmitIntValue(SegmentSize, 4);

    for (auto J = I; J != FileSegEnd; ++J) {
      OS.emitAbsoluteSymbolDiff(J->getLabel(), FuncBegin, 4);
      unsigned LineData = J->getLine();
      if (J->isStmt())
        LineData |= LineInfo::StatementFlag;
      OS.EmitIntValue(LineData, 4);
    }
    if (HaveColumns) {
      for (auto J = I; J != FileSegEnd; ++J) {
        OS.EmitIntValue(J->getColumn(), 2);
        OS.EmitIntValue(0, 2);
      }
    }
    I = FileSegEnd;
  }
  OS.EmitLabel(LineEnd);
}

static bool compressAnnotation(uint32_t Data, SmallVectorImpl<char> &Buffer) {
  if (isUInt<7>(Data)) {
    Buffer.push_back(Data);
    return true;
  }

  if (isUInt<14>(Data)) {
    Buffer.push_back((Data >> 8) | 0x80);
    Buffer.push_back(Data & 0xff);
    return true;
  }

  if (isUInt<29>(Data)) {
    Buffer.push_back((Data >> 24) | 0xC0);
    Buffer.push_back((Data >> 16) & 0xff);
    Buffer.push_back((Data >> 8) & 0xff);
    Buffer.push_back(Data & 0xff);
    return true;
  }

  return false;
}

static bool compressAnnotation(BinaryAnnotationsOpCode Annotation,
                               SmallVectorImpl<char> &Buffer) {
  return compressAnnotation(static_cast<uint32_t>(Annotation), Buffer);
}

static uint32_t encodeSignedNumber(uint32_t Data) {
  if (Data >> 31)
    return ((-Data) << 1) | 1;
  return Data << 1;
}

void CodeViewContext::emitInlineLineTableForFunction(MCObjectStreamer &OS,
                                                     unsigned PrimaryFunctionId,
                                                     unsigned SourceFileId,
                                                     unsigned SourceLineNum,
                                                     const MCSymbol *FnStartSym,
                                                     const MCSymbol *FnEndSym) {
  // Create and insert a fragment into the current section that will be encoded
  // later.
  new MCCVInlineLineTableFragment(PrimaryFunctionId, SourceFileId,
                                  SourceLineNum, FnStartSym, FnEndSym,
                                  OS.getCurrentSectionOnly());
}

MCFragment *CodeViewContext::emitDefRange(
    MCObjectStreamer &OS,
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {
  // Create and insert a fragment into the current section that will be encoded
  // later.
  return new MCCVDefRangeFragment(Ranges, FixedSizePortion,
                           OS.getCurrentSectionOnly());
}

static unsigned computeLabelDiff(MCAsmLayout &Layout, const MCSymbol *Begin,
                                 const MCSymbol *End) {
  MCContext &Ctx = Layout.getAssembler().getContext();
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  const MCExpr *BeginRef = MCSymbolRefExpr::create(Begin, Variant, Ctx),
               *EndRef = MCSymbolRefExpr::create(End, Variant, Ctx);
  const MCExpr *AddrDelta =
      MCBinaryExpr::create(MCBinaryExpr::Sub, EndRef, BeginRef, Ctx);
  int64_t Result;
  bool Success = AddrDelta->evaluateKnownAbsolute(Result, Layout);
  assert(Success && "failed to evaluate label difference as absolute");
  (void)Success;
  assert(Result >= 0 && "negative label difference requested");
  assert(Result < UINT_MAX && "label difference greater than 2GB");
  return unsigned(Result);
}

void CodeViewContext::encodeInlineLineTable(MCAsmLayout &Layout,
                                            MCCVInlineLineTableFragment &Frag) {
  size_t LocBegin;
  size_t LocEnd;
  std::tie(LocBegin, LocEnd) = getLineExtent(Frag.SiteFuncId);

  // Include all child inline call sites in our .cv_loc extent.
  MCCVFunctionInfo *SiteInfo = getCVFunctionInfo(Frag.SiteFuncId);
  for (auto &KV : SiteInfo->InlinedAtMap) {
    unsigned ChildId = KV.first;
    auto Extent = getLineExtent(ChildId);
    LocBegin = std::min(LocBegin, Extent.first);
    LocEnd = std::max(LocEnd, Extent.second);
  }

  if (LocBegin >= LocEnd)
    return;
  ArrayRef<MCCVLoc> Locs = getLinesForExtent(LocBegin, LocEnd);
  if (Locs.empty())
    return;

  // Check that the locations are all in the same section.
#ifndef NDEBUG
  const MCSection *FirstSec = &Locs.front().getLabel()->getSection();
  for (const MCCVLoc &Loc : Locs) {
    if (&Loc.getLabel()->getSection() != FirstSec) {
      errs() << ".cv_loc " << Loc.getFunctionId() << ' ' << Loc.getFileNum()
             << ' ' << Loc.getLine() << ' ' << Loc.getColumn()
             << " is in the wrong section\n";
      llvm_unreachable(".cv_loc crosses sections");
    }
  }
#endif

  // Make an artificial start location using the function start and the inlinee
  // lines start location information. All deltas start relative to this
  // location.
  MCCVLoc StartLoc = Locs.front();
  StartLoc.setLabel(Frag.getFnStartSym());
  StartLoc.setFileNum(Frag.StartFileId);
  StartLoc.setLine(Frag.StartLineNum);
  bool HaveOpenRange = false;

  const MCSymbol *LastLabel = Frag.getFnStartSym();
  MCCVFunctionInfo::LineInfo LastSourceLoc, CurSourceLoc;
  LastSourceLoc.File = Frag.StartFileId;
  LastSourceLoc.Line = Frag.StartLineNum;

  SmallVectorImpl<char> &Buffer = Frag.getContents();
  Buffer.clear(); // Clear old contents if we went through relaxation.
  for (const MCCVLoc &Loc : Locs) {
    // Exit early if our line table would produce an oversized InlineSiteSym
    // record. Account for the ChangeCodeLength annotation emitted after the
    // loop ends.
    constexpr uint32_t InlineSiteSize = 12;
    constexpr uint32_t AnnotationSize = 8;
    size_t MaxBufferSize = MaxRecordLength - InlineSiteSize - AnnotationSize;
    if (Buffer.size() >= MaxBufferSize)
      break;

    if (Loc.getFunctionId() == Frag.SiteFuncId) {
      CurSourceLoc.File = Loc.getFileNum();
      CurSourceLoc.Line = Loc.getLine();
    } else {
      auto I = SiteInfo->InlinedAtMap.find(Loc.getFunctionId());
      if (I != SiteInfo->InlinedAtMap.end()) {
        // This .cv_loc is from a child inline call site. Use the source
        // location of the inlined call site instead of the .cv_loc directive
        // source location.
        CurSourceLoc = I->second;
      } else {
        // We've hit a cv_loc not attributed to this inline call site. Use this
        // label to end the PC range.
        if (HaveOpenRange) {
          unsigned Length = computeLabelDiff(Layout, LastLabel, Loc.getLabel());
          compressAnnotation(BinaryAnnotationsOpCode::ChangeCodeLength, Buffer);
          compressAnnotation(Length, Buffer);
          LastLabel = Loc.getLabel();
        }
        HaveOpenRange = false;
        continue;
      }
    }

    // Skip this .cv_loc if we have an open range and this isn't a meaningful
    // source location update. The current table format does not support column
    // info, so we can skip updates for those.
    if (HaveOpenRange && CurSourceLoc.File == LastSourceLoc.File &&
        CurSourceLoc.Line == LastSourceLoc.Line)
      continue;

    HaveOpenRange = true;

    if (CurSourceLoc.File != LastSourceLoc.File) {
      unsigned FileOffset = static_cast<const MCConstantExpr *>(
                                Files[CurSourceLoc.File - 1]
                                    .ChecksumTableOffset->getVariableValue())
                                ->getValue();
      compressAnnotation(BinaryAnnotationsOpCode::ChangeFile, Buffer);
      compressAnnotation(FileOffset, Buffer);
    }

    int LineDelta = CurSourceLoc.Line - LastSourceLoc.Line;
    unsigned EncodedLineDelta = encodeSignedNumber(LineDelta);
    unsigned CodeDelta = computeLabelDiff(Layout, LastLabel, Loc.getLabel());
    if (CodeDelta == 0 && LineDelta != 0) {
      compressAnnotation(BinaryAnnotationsOpCode::ChangeLineOffset, Buffer);
      compressAnnotation(EncodedLineDelta, Buffer);
    } else if (EncodedLineDelta < 0x8 && CodeDelta <= 0xf) {
      // The ChangeCodeOffsetAndLineOffset combination opcode is used when the
      // encoded line delta uses 3 or fewer set bits and the code offset fits
      // in one nibble.
      unsigned Operand = (EncodedLineDelta << 4) | CodeDelta;
      compressAnnotation(BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset,
                         Buffer);
      compressAnnotation(Operand, Buffer);
    } else {
      // Otherwise use the separate line and code deltas.
      if (LineDelta != 0) {
        compressAnnotation(BinaryAnnotationsOpCode::ChangeLineOffset, Buffer);
        compressAnnotation(EncodedLineDelta, Buffer);
      }
      compressAnnotation(BinaryAnnotationsOpCode::ChangeCodeOffset, Buffer);
      compressAnnotation(CodeDelta, Buffer);
    }

    LastLabel = Loc.getLabel();
    LastSourceLoc = CurSourceLoc;
  }

  assert(HaveOpenRange);

  unsigned EndSymLength =
      computeLabelDiff(Layout, LastLabel, Frag.getFnEndSym());
  unsigned LocAfterLength = ~0U;
  ArrayRef<MCCVLoc> LocAfter = getLinesForExtent(LocEnd, LocEnd + 1);
  if (!LocAfter.empty()) {
    // Only try to compute this difference if we're in the same section.
    const MCCVLoc &Loc = LocAfter[0];
    if (&Loc.getLabel()->getSection() == &LastLabel->getSection())
      LocAfterLength = computeLabelDiff(Layout, LastLabel, Loc.getLabel());
  }

  compressAnnotation(BinaryAnnotationsOpCode::ChangeCodeLength, Buffer);
  compressAnnotation(std::min(EndSymLength, LocAfterLength), Buffer);
}

void CodeViewContext::encodeDefRange(MCAsmLayout &Layout,
                                     MCCVDefRangeFragment &Frag) {
  MCContext &Ctx = Layout.getAssembler().getContext();
  SmallVectorImpl<char> &Contents = Frag.getContents();
  Contents.clear();
  SmallVectorImpl<MCFixup> &Fixups = Frag.getFixups();
  Fixups.clear();
  raw_svector_ostream OS(Contents);

  // Compute all the sizes up front.
  SmallVector<std::pair<unsigned, unsigned>, 4> GapAndRangeSizes;
  const MCSymbol *LastLabel = nullptr;
  for (std::pair<const MCSymbol *, const MCSymbol *> Range : Frag.getRanges()) {
    unsigned GapSize =
        LastLabel ? computeLabelDiff(Layout, LastLabel, Range.first) : 0;
    unsigned RangeSize = computeLabelDiff(Layout, Range.first, Range.second);
    GapAndRangeSizes.push_back({GapSize, RangeSize});
    LastLabel = Range.second;
  }

  // Write down each range where the variable is defined.
  for (size_t I = 0, E = Frag.getRanges().size(); I != E;) {
    // If the range size of multiple consecutive ranges is under the max,
    // combine the ranges and emit some gaps.
    const MCSymbol *RangeBegin = Frag.getRanges()[I].first;
    unsigned RangeSize = GapAndRangeSizes[I].second;
    size_t J = I + 1;
    for (; J != E; ++J) {
      unsigned GapAndRangeSize = GapAndRangeSizes[J].first + GapAndRangeSizes[J].second;
      if (RangeSize + GapAndRangeSize > MaxDefRange)
        break;
      RangeSize += GapAndRangeSize;
    }
    unsigned NumGaps = J - I - 1;

    support::endian::Writer LEWriter(OS, support::little);

    unsigned Bias = 0;
    // We must split the range into chunks of MaxDefRange, this is a fundamental
    // limitation of the file format.
    do {
      uint16_t Chunk = std::min((uint32_t)MaxDefRange, RangeSize);

      const MCSymbolRefExpr *SRE = MCSymbolRefExpr::create(RangeBegin, Ctx);
      const MCBinaryExpr *BE =
          MCBinaryExpr::createAdd(SRE, MCConstantExpr::create(Bias, Ctx), Ctx);
      MCValue Res;
      BE->evaluateAsRelocatable(Res, &Layout, /*Fixup=*/nullptr);

      // Each record begins with a 2-byte number indicating how large the record
      // is.
      StringRef FixedSizePortion = Frag.getFixedSizePortion();
      // Our record is a fixed sized prefix and a LocalVariableAddrRange that we
      // are artificially constructing.
      size_t RecordSize = FixedSizePortion.size() +
                          sizeof(LocalVariableAddrRange) + 4 * NumGaps;
      // Write out the record size.
      LEWriter.write<uint16_t>(RecordSize);
      // Write out the fixed size prefix.
      OS << FixedSizePortion;
      // Make space for a fixup that will eventually have a section relative
      // relocation pointing at the offset where the variable becomes live.
      Fixups.push_back(MCFixup::create(Contents.size(), BE, FK_SecRel_4));
      LEWriter.write<uint32_t>(0); // Fixup for code start.
      // Make space for a fixup that will record the section index for the code.
      Fixups.push_back(MCFixup::create(Contents.size(), BE, FK_SecRel_2));
      LEWriter.write<uint16_t>(0); // Fixup for section index.
      // Write down the range's extent.
      LEWriter.write<uint16_t>(Chunk);

      // Move on to the next range.
      Bias += Chunk;
      RangeSize -= Chunk;
    } while (RangeSize > 0);

    // Emit the gaps afterwards.
    assert((NumGaps == 0 || Bias <= MaxDefRange) &&
           "large ranges should not have gaps");
    unsigned GapStartOffset = GapAndRangeSizes[I].second;
    for (++I; I != J; ++I) {
      unsigned GapSize, RangeSize;
      assert(I < GapAndRangeSizes.size());
      std::tie(GapSize, RangeSize) = GapAndRangeSizes[I];
      LEWriter.write<uint16_t>(GapStartOffset);
      LEWriter.write<uint16_t>(GapSize);
      GapStartOffset += GapSize + RangeSize;
    }
  }
}
