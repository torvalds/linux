//===- lib/MC/MCAsmStreamer.cpp - Text Assembly Output ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetRegistry.h"
#include <cctype>

using namespace llvm;

namespace {

class MCAsmStreamer final : public MCStreamer {
  std::unique_ptr<formatted_raw_ostream> OSOwner;
  formatted_raw_ostream &OS;
  const MCAsmInfo *MAI;
  std::unique_ptr<MCInstPrinter> InstPrinter;
  std::unique_ptr<MCAssembler> Assembler;

  SmallString<128> ExplicitCommentToEmit;
  SmallString<128> CommentToEmit;
  raw_svector_ostream CommentStream;
  raw_null_ostream NullStream;

  unsigned IsVerboseAsm : 1;
  unsigned ShowInst : 1;
  unsigned UseDwarfDirectory : 1;

  void EmitRegisterName(int64_t Register);
  void EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame) override;
  void EmitCFIEndProcImpl(MCDwarfFrameInfo &Frame) override;

public:
  MCAsmStreamer(MCContext &Context, std::unique_ptr<formatted_raw_ostream> os,
                bool isVerboseAsm, bool useDwarfDirectory,
                MCInstPrinter *printer, std::unique_ptr<MCCodeEmitter> emitter,
                std::unique_ptr<MCAsmBackend> asmbackend, bool showInst)
      : MCStreamer(Context), OSOwner(std::move(os)), OS(*OSOwner),
        MAI(Context.getAsmInfo()), InstPrinter(printer),
        Assembler(llvm::make_unique<MCAssembler>(
            Context, std::move(asmbackend), std::move(emitter),
            (asmbackend) ? asmbackend->createObjectWriter(NullStream)
                         : nullptr)),
        CommentStream(CommentToEmit), IsVerboseAsm(isVerboseAsm),
        ShowInst(showInst), UseDwarfDirectory(useDwarfDirectory) {
    assert(InstPrinter);
    if (IsVerboseAsm)
        InstPrinter->setCommentStream(CommentStream);
  }

  MCAssembler &getAssembler() { return *Assembler; }
  MCAssembler *getAssemblerPtr() override { return nullptr; }

  inline void EmitEOL() {
    // Dump Explicit Comments here.
    emitExplicitComments();
    // If we don't have any comments, just emit a \n.
    if (!IsVerboseAsm) {
      OS << '\n';
      return;
    }
    EmitCommentsAndEOL();
  }

  void EmitSyntaxDirective() override;

  void EmitCommentsAndEOL();

  /// Return true if this streamer supports verbose assembly at all.
  bool isVerboseAsm() const override { return IsVerboseAsm; }

  /// Do we support EmitRawText?
  bool hasRawTextSupport() const override { return true; }

  /// Add a comment that can be emitted to the generated .s file to make the
  /// output of the compiler more readable. This only affects the MCAsmStreamer
  /// and only when verbose assembly output is enabled.
  void AddComment(const Twine &T, bool EOL = true) override;

  /// Add a comment showing the encoding of an instruction.
  /// If PrintSchedInfo is true, then the comment sched:[x:y] will be added to
  /// the output if supported by the target.
  void AddEncodingComment(const MCInst &Inst, const MCSubtargetInfo &,
                          bool PrintSchedInfo);

  /// Return a raw_ostream that comments can be written to.
  /// Unlike AddComment, you are required to terminate comments with \n if you
  /// use this method.
  raw_ostream &GetCommentOS() override {
    if (!IsVerboseAsm)
      return nulls();  // Discard comments unless in verbose asm mode.
    return CommentStream;
  }

  void emitRawComment(const Twine &T, bool TabPrefix = true) override;

  void addExplicitComment(const Twine &T) override;
  void emitExplicitComments() override;

  /// Emit a blank line to a .s file to pretty it up.
  void AddBlankLine() override {
    EmitEOL();
  }

  /// @name MCStreamer Interface
  /// @{

  void ChangeSection(MCSection *Section, const MCExpr *Subsection) override;

  void emitELFSymverDirective(StringRef AliasName,
                              const MCSymbol *Aliasee) override;

  void EmitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) override;
  void EmitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;

  void EmitAssemblerFlag(MCAssemblerFlag Flag) override;
  void EmitLinkerOptions(ArrayRef<std::string> Options) override;
  void EmitDataRegion(MCDataRegionType Kind) override;
  void EmitVersionMin(MCVersionMinType Kind, unsigned Major, unsigned Minor,
                      unsigned Update, VersionTuple SDKVersion) override;
  void EmitBuildVersion(unsigned Platform, unsigned Major, unsigned Minor,
                        unsigned Update, VersionTuple SDKVersion) override;
  void EmitThumbFunc(MCSymbol *Func) override;

  void EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  void EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) override;
  bool EmitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;

  void EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) override;
  void BeginCOFFSymbolDef(const MCSymbol *Symbol) override;
  void EmitCOFFSymbolStorageClass(int StorageClass) override;
  void EmitCOFFSymbolType(int Type) override;
  void EndCOFFSymbolDef() override;
  void EmitCOFFSafeSEH(MCSymbol const *Symbol) override;
  void EmitCOFFSymbolIndex(MCSymbol const *Symbol) override;
  void EmitCOFFSectionIndex(MCSymbol const *Symbol) override;
  void EmitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset) override;
  void EmitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset) override;
  void emitELFSize(MCSymbol *Symbol, const MCExpr *Value) override;
  void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        unsigned ByteAlignment) override;

  /// Emit a local common (.lcomm) symbol.
  ///
  /// @param Symbol - The common symbol to emit.
  /// @param Size - The size of the common symbol.
  /// @param ByteAlignment - The alignment of the common symbol in bytes.
  void EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             unsigned ByteAlignment) override;

  void EmitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, unsigned ByteAlignment = 0,
                    SMLoc Loc = SMLoc()) override;

  void EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                      unsigned ByteAlignment = 0) override;

  void EmitBinaryData(StringRef Data) override;

  void EmitBytes(StringRef Data) override;

  void EmitValueImpl(const MCExpr *Value, unsigned Size,
                     SMLoc Loc = SMLoc()) override;
  void EmitIntValue(uint64_t Value, unsigned Size) override;

  void EmitULEB128Value(const MCExpr *Value) override;

  void EmitSLEB128Value(const MCExpr *Value) override;

  void EmitDTPRel32Value(const MCExpr *Value) override;
  void EmitDTPRel64Value(const MCExpr *Value) override;
  void EmitTPRel32Value(const MCExpr *Value) override;
  void EmitTPRel64Value(const MCExpr *Value) override;

  void EmitGPRel64Value(const MCExpr *Value) override;

  void EmitGPRel32Value(const MCExpr *Value) override;

  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc = SMLoc()) override;

  void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                SMLoc Loc = SMLoc()) override;

  void EmitValueToAlignment(unsigned ByteAlignment, int64_t Value = 0,
                            unsigned ValueSize = 1,
                            unsigned MaxBytesToEmit = 0) override;

  void EmitCodeAlignment(unsigned ByteAlignment,
                         unsigned MaxBytesToEmit = 0) override;

  void emitValueToOffset(const MCExpr *Offset,
                         unsigned char Value,
                         SMLoc Loc) override;

  void EmitFileDirective(StringRef Filename) override;
  Expected<unsigned> tryEmitDwarfFileDirective(unsigned FileNo,
                                               StringRef Directory,
                                               StringRef Filename,
                                               MD5::MD5Result *Checksum = 0,
                                               Optional<StringRef> Source = None,
                                               unsigned CUID = 0) override;
  void emitDwarfFile0Directive(StringRef Directory, StringRef Filename,
                               MD5::MD5Result *Checksum,
                               Optional<StringRef> Source,
                               unsigned CUID = 0) override;
  void EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                             unsigned Column, unsigned Flags,
                             unsigned Isa, unsigned Discriminator,
                             StringRef FileName) override;
  MCSymbol *getDwarfLineTableSymbol(unsigned CUID) override;

  bool EmitCVFileDirective(unsigned FileNo, StringRef Filename,
                           ArrayRef<uint8_t> Checksum,
                           unsigned ChecksumKind) override;
  bool EmitCVFuncIdDirective(unsigned FuncId) override;
  bool EmitCVInlineSiteIdDirective(unsigned FunctionId, unsigned IAFunc,
                                   unsigned IAFile, unsigned IALine,
                                   unsigned IACol, SMLoc Loc) override;
  void EmitCVLocDirective(unsigned FunctionId, unsigned FileNo, unsigned Line,
                          unsigned Column, bool PrologueEnd, bool IsStmt,
                          StringRef FileName, SMLoc Loc) override;
  void EmitCVLinetableDirective(unsigned FunctionId, const MCSymbol *FnStart,
                                const MCSymbol *FnEnd) override;
  void EmitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                      unsigned SourceFileId,
                                      unsigned SourceLineNum,
                                      const MCSymbol *FnStartSym,
                                      const MCSymbol *FnEndSym) override;
  void EmitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion) override;
  void EmitCVStringTableDirective() override;
  void EmitCVFileChecksumsDirective() override;
  void EmitCVFileChecksumOffsetDirective(unsigned FileNo) override;
  void EmitCVFPOData(const MCSymbol *ProcSym, SMLoc L) override;

  void EmitIdent(StringRef IdentString) override;
  void EmitCFIBKeyFrame() override;
  void EmitCFISections(bool EH, bool Debug) override;
  void EmitCFIDefCfa(int64_t Register, int64_t Offset) override;
  void EmitCFIDefCfaOffset(int64_t Offset) override;
  void EmitCFIDefCfaRegister(int64_t Register) override;
  void EmitCFIOffset(int64_t Register, int64_t Offset) override;
  void EmitCFIPersonality(const MCSymbol *Sym, unsigned Encoding) override;
  void EmitCFILsda(const MCSymbol *Sym, unsigned Encoding) override;
  void EmitCFIRememberState() override;
  void EmitCFIRestoreState() override;
  void EmitCFIRestore(int64_t Register) override;
  void EmitCFISameValue(int64_t Register) override;
  void EmitCFIRelOffset(int64_t Register, int64_t Offset) override;
  void EmitCFIAdjustCfaOffset(int64_t Adjustment) override;
  void EmitCFIEscape(StringRef Values) override;
  void EmitCFIGnuArgsSize(int64_t Size) override;
  void EmitCFISignalFrame() override;
  void EmitCFIUndefined(int64_t Register) override;
  void EmitCFIRegister(int64_t Register1, int64_t Register2) override;
  void EmitCFIWindowSave() override;
  void EmitCFINegateRAState() override;
  void EmitCFIReturnColumn(int64_t Register) override;

  void EmitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc) override;
  void EmitWinCFIEndProc(SMLoc Loc) override;
  void EmitWinCFIFuncletOrFuncEnd(SMLoc Loc) override;
  void EmitWinCFIStartChained(SMLoc Loc) override;
  void EmitWinCFIEndChained(SMLoc Loc) override;
  void EmitWinCFIPushReg(unsigned Register, SMLoc Loc) override;
  void EmitWinCFISetFrame(unsigned Register, unsigned Offset,
                          SMLoc Loc) override;
  void EmitWinCFIAllocStack(unsigned Size, SMLoc Loc) override;
  void EmitWinCFISaveReg(unsigned Register, unsigned Offset,
                         SMLoc Loc) override;
  void EmitWinCFISaveXMM(unsigned Register, unsigned Offset,
                         SMLoc Loc) override;
  void EmitWinCFIPushFrame(bool Code, SMLoc Loc) override;
  void EmitWinCFIEndProlog(SMLoc Loc) override;

  void EmitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                        SMLoc Loc) override;
  void EmitWinEHHandlerData(SMLoc Loc) override;

  void emitCGProfileEntry(const MCSymbolRefExpr *From,
                          const MCSymbolRefExpr *To, uint64_t Count) override;

  void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                       bool PrintSchedInfo) override;

  void EmitBundleAlignMode(unsigned AlignPow2) override;
  void EmitBundleLock(bool AlignToEnd) override;
  void EmitBundleUnlock() override;

  bool EmitRelocDirective(const MCExpr &Offset, StringRef Name,
                          const MCExpr *Expr, SMLoc Loc,
                          const MCSubtargetInfo &STI) override;

  void EmitAddrsig() override;
  void EmitAddrsigSym(const MCSymbol *Sym) override;

  /// If this file is backed by an assembly streamer, this dumps the specified
  /// string in the output .s file. This capability is indicated by the
  /// hasRawTextSupport() predicate.
  void EmitRawTextImpl(StringRef String) override;

  void FinishImpl() override;
};

} // end anonymous namespace.

void MCAsmStreamer::AddComment(const Twine &T, bool EOL) {
  if (!IsVerboseAsm) return;

  T.toVector(CommentToEmit);

  if (EOL)
    CommentToEmit.push_back('\n'); // Place comment in a new line.
}

void MCAsmStreamer::EmitCommentsAndEOL() {
  if (CommentToEmit.empty() && CommentStream.GetNumBytesInBuffer() == 0) {
    OS << '\n';
    return;
  }

  StringRef Comments = CommentToEmit;

  assert(Comments.back() == '\n' &&
         "Comment array not newline terminated");
  do {
    // Emit a line of comments.
    OS.PadToColumn(MAI->getCommentColumn());
    size_t Position = Comments.find('\n');
    OS << MAI->getCommentString() << ' ' << Comments.substr(0, Position) <<'\n';

    Comments = Comments.substr(Position+1);
  } while (!Comments.empty());

  CommentToEmit.clear();
}

static inline int64_t truncateToSize(int64_t Value, unsigned Bytes) {
  assert(Bytes > 0 && Bytes <= 8 && "Invalid size!");
  return Value & ((uint64_t) (int64_t) -1 >> (64 - Bytes * 8));
}

void MCAsmStreamer::emitRawComment(const Twine &T, bool TabPrefix) {
  if (TabPrefix)
    OS << '\t';
  OS << MAI->getCommentString() << T;
  EmitEOL();
}

void MCAsmStreamer::addExplicitComment(const Twine &T) {
  StringRef c = T.getSingleStringRef();
  if (c.equals(StringRef(MAI->getSeparatorString())))
    return;
  if (c.startswith(StringRef("//"))) {
    ExplicitCommentToEmit.append("\t");
    ExplicitCommentToEmit.append(MAI->getCommentString());
    // drop //
    ExplicitCommentToEmit.append(c.slice(2, c.size()).str());
  } else if (c.startswith(StringRef("/*"))) {
    size_t p = 2, len = c.size() - 2;
    // emit each line in comment as separate newline.
    do {
      size_t newp = std::min(len, c.find_first_of("\r\n", p));
      ExplicitCommentToEmit.append("\t");
      ExplicitCommentToEmit.append(MAI->getCommentString());
      ExplicitCommentToEmit.append(c.slice(p, newp).str());
      // If we have another line in this comment add line
      if (newp < len)
        ExplicitCommentToEmit.append("\n");
      p = newp + 1;
    } while (p < len);
  } else if (c.startswith(StringRef(MAI->getCommentString()))) {
    ExplicitCommentToEmit.append("\t");
    ExplicitCommentToEmit.append(c.str());
  } else if (c.front() == '#') {

    ExplicitCommentToEmit.append("\t");
    ExplicitCommentToEmit.append(MAI->getCommentString());
    ExplicitCommentToEmit.append(c.slice(1, c.size()).str());
  } else
    assert(false && "Unexpected Assembly Comment");
  // full line comments immediately output
  if (c.back() == '\n')
    emitExplicitComments();
}

void MCAsmStreamer::emitExplicitComments() {
  StringRef Comments = ExplicitCommentToEmit;
  if (!Comments.empty())
    OS << Comments;
  ExplicitCommentToEmit.clear();
}

void MCAsmStreamer::ChangeSection(MCSection *Section,
                                  const MCExpr *Subsection) {
  assert(Section && "Cannot switch to a null section!");
  if (MCTargetStreamer *TS = getTargetStreamer()) {
    TS->changeSection(getCurrentSectionOnly(), Section, Subsection, OS);
  } else {
    Section->PrintSwitchToSection(
        *MAI, getContext().getObjectFileInfo()->getTargetTriple(), OS,
        Subsection);
  }
}

void MCAsmStreamer::emitELFSymverDirective(StringRef AliasName,
                                           const MCSymbol *Aliasee) {
  OS << ".symver ";
  Aliasee->print(OS, MAI);
  OS << ", " << AliasName;
  EmitEOL();
}

void MCAsmStreamer::EmitLabel(MCSymbol *Symbol, SMLoc Loc) {
  MCStreamer::EmitLabel(Symbol, Loc);

  Symbol->print(OS, MAI);
  OS << MAI->getLabelSuffix();

  EmitEOL();
}

void MCAsmStreamer::EmitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) {
  StringRef str = MCLOHIdToName(Kind);

#ifndef NDEBUG
  int NbArgs = MCLOHIdToNbArgs(Kind);
  assert(NbArgs != -1 && ((size_t)NbArgs) == Args.size() && "Malformed LOH!");
  assert(str != "" && "Invalid LOH name");
#endif

  OS << "\t" << MCLOHDirectiveName() << " " << str << "\t";
  bool IsFirst = true;
  for (const MCSymbol *Arg : Args) {
    if (!IsFirst)
      OS << ", ";
    IsFirst = false;
    Arg->print(OS, MAI);
  }
  EmitEOL();
}

void MCAsmStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {
  switch (Flag) {
  case MCAF_SyntaxUnified:         OS << "\t.syntax unified"; break;
  case MCAF_SubsectionsViaSymbols: OS << ".subsections_via_symbols"; break;
  case MCAF_Code16:                OS << '\t'<< MAI->getCode16Directive();break;
  case MCAF_Code32:                OS << '\t'<< MAI->getCode32Directive();break;
  case MCAF_Code64:                OS << '\t'<< MAI->getCode64Directive();break;
  }
  EmitEOL();
}

void MCAsmStreamer::EmitLinkerOptions(ArrayRef<std::string> Options) {
  assert(!Options.empty() && "At least one option is required!");
  OS << "\t.linker_option \"" << Options[0] << '"';
  for (ArrayRef<std::string>::iterator it = Options.begin() + 1,
         ie = Options.end(); it != ie; ++it) {
    OS << ", " << '"' << *it << '"';
  }
  EmitEOL();
}

void MCAsmStreamer::EmitDataRegion(MCDataRegionType Kind) {
  if (!MAI->doesSupportDataRegionDirectives())
    return;
  switch (Kind) {
  case MCDR_DataRegion:            OS << "\t.data_region"; break;
  case MCDR_DataRegionJT8:         OS << "\t.data_region jt8"; break;
  case MCDR_DataRegionJT16:        OS << "\t.data_region jt16"; break;
  case MCDR_DataRegionJT32:        OS << "\t.data_region jt32"; break;
  case MCDR_DataRegionEnd:         OS << "\t.end_data_region"; break;
  }
  EmitEOL();
}

static const char *getVersionMinDirective(MCVersionMinType Type) {
  switch (Type) {
  case MCVM_WatchOSVersionMin: return ".watchos_version_min";
  case MCVM_TvOSVersionMin:    return ".tvos_version_min";
  case MCVM_IOSVersionMin:     return ".ios_version_min";
  case MCVM_OSXVersionMin:     return ".macosx_version_min";
  }
  llvm_unreachable("Invalid MC version min type");
}

static void EmitSDKVersionSuffix(raw_ostream &OS,
                                 const VersionTuple &SDKVersion) {
  if (SDKVersion.empty())
    return;
  OS << '\t' << "sdk_version " << SDKVersion.getMajor();
  if (auto Minor = SDKVersion.getMinor()) {
    OS << ", " << *Minor;
    if (auto Subminor = SDKVersion.getSubminor()) {
      OS << ", " << *Subminor;
    }
  }
}

void MCAsmStreamer::EmitVersionMin(MCVersionMinType Type, unsigned Major,
                                   unsigned Minor, unsigned Update,
                                   VersionTuple SDKVersion) {
  OS << '\t' << getVersionMinDirective(Type) << ' ' << Major << ", " << Minor;
  if (Update)
    OS << ", " << Update;
  EmitSDKVersionSuffix(OS, SDKVersion);
  EmitEOL();
}

static const char *getPlatformName(MachO::PlatformType Type) {
  switch (Type) {
  case MachO::PLATFORM_MACOS:            return "macos";
  case MachO::PLATFORM_IOS:              return "ios";
  case MachO::PLATFORM_TVOS:             return "tvos";
  case MachO::PLATFORM_WATCHOS:          return "watchos";
  case MachO::PLATFORM_BRIDGEOS:         return "bridgeos";
  case MachO::PLATFORM_IOSSIMULATOR:     return "iossimulator";
  case MachO::PLATFORM_TVOSSIMULATOR:    return "tvossimulator";
  case MachO::PLATFORM_WATCHOSSIMULATOR: return "watchossimulator";
  }
  llvm_unreachable("Invalid Mach-O platform type");
}

void MCAsmStreamer::EmitBuildVersion(unsigned Platform, unsigned Major,
                                     unsigned Minor, unsigned Update,
                                     VersionTuple SDKVersion) {
  const char *PlatformName = getPlatformName((MachO::PlatformType)Platform);
  OS << "\t.build_version " << PlatformName << ", " << Major << ", " << Minor;
  if (Update)
    OS << ", " << Update;
  EmitSDKVersionSuffix(OS, SDKVersion);
  EmitEOL();
}

void MCAsmStreamer::EmitThumbFunc(MCSymbol *Func) {
  // This needs to emit to a temporary string to get properly quoted
  // MCSymbols when they have spaces in them.
  OS << "\t.thumb_func";
  // Only Mach-O hasSubsectionsViaSymbols()
  if (MAI->hasSubsectionsViaSymbols()) {
    OS << '\t';
    Func->print(OS, MAI);
  }
  EmitEOL();
}

void MCAsmStreamer::EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  // Do not emit a .set on inlined target assignments.
  bool EmitSet = true;
  if (auto *E = dyn_cast<MCTargetExpr>(Value))
    if (E->inlineAssignedExpr())
      EmitSet = false;
  if (EmitSet) {
    OS << ".set ";
    Symbol->print(OS, MAI);
    OS << ", ";
    Value->print(OS, MAI);

    EmitEOL();
  }

  MCStreamer::EmitAssignment(Symbol, Value);
}

void MCAsmStreamer::EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {
  OS << ".weakref ";
  Alias->print(OS, MAI);
  OS << ", ";
  Symbol->print(OS, MAI);
  EmitEOL();
}

bool MCAsmStreamer::EmitSymbolAttribute(MCSymbol *Symbol,
                                        MCSymbolAttr Attribute) {
  switch (Attribute) {
  case MCSA_Invalid: llvm_unreachable("Invalid symbol attribute");
  case MCSA_ELF_TypeFunction:    /// .type _foo, STT_FUNC  # aka @function
  case MCSA_ELF_TypeIndFunction: /// .type _foo, STT_GNU_IFUNC
  case MCSA_ELF_TypeObject:      /// .type _foo, STT_OBJECT  # aka @object
  case MCSA_ELF_TypeTLS:         /// .type _foo, STT_TLS     # aka @tls_object
  case MCSA_ELF_TypeCommon:      /// .type _foo, STT_COMMON  # aka @common
  case MCSA_ELF_TypeNoType:      /// .type _foo, STT_NOTYPE  # aka @notype
  case MCSA_ELF_TypeGnuUniqueObject:  /// .type _foo, @gnu_unique_object
    if (!MAI->hasDotTypeDotSizeDirective())
      return false; // Symbol attribute not supported
    OS << "\t.type\t";
    Symbol->print(OS, MAI);
    OS << ',' << ((MAI->getCommentString()[0] != '@') ? '@' : '%');
    switch (Attribute) {
    default: return false;
    case MCSA_ELF_TypeFunction:    OS << "function"; break;
    case MCSA_ELF_TypeIndFunction: OS << "gnu_indirect_function"; break;
    case MCSA_ELF_TypeObject:      OS << "object"; break;
    case MCSA_ELF_TypeTLS:         OS << "tls_object"; break;
    case MCSA_ELF_TypeCommon:      OS << "common"; break;
    case MCSA_ELF_TypeNoType:      OS << "notype"; break;
    case MCSA_ELF_TypeGnuUniqueObject: OS << "gnu_unique_object"; break;
    }
    EmitEOL();
    return true;
  case MCSA_Global: // .globl/.global
    OS << MAI->getGlobalDirective();
    break;
  case MCSA_Hidden:         OS << "\t.hidden\t";          break;
  case MCSA_IndirectSymbol: OS << "\t.indirect_symbol\t"; break;
  case MCSA_Internal:       OS << "\t.internal\t";        break;
  case MCSA_LazyReference:  OS << "\t.lazy_reference\t";  break;
  case MCSA_Local:          OS << "\t.local\t";           break;
  case MCSA_NoDeadStrip:
    if (!MAI->hasNoDeadStrip())
      return false;
    OS << "\t.no_dead_strip\t";
    break;
  case MCSA_SymbolResolver: OS << "\t.symbol_resolver\t"; break;
  case MCSA_AltEntry:       OS << "\t.alt_entry\t";       break;
  case MCSA_PrivateExtern:
    OS << "\t.private_extern\t";
    break;
  case MCSA_Protected:      OS << "\t.protected\t";       break;
  case MCSA_Reference:      OS << "\t.reference\t";       break;
  case MCSA_Weak:           OS << MAI->getWeakDirective(); break;
  case MCSA_WeakDefinition:
    OS << "\t.weak_definition\t";
    break;
      // .weak_reference
  case MCSA_WeakReference:  OS << MAI->getWeakRefDirective(); break;
  case MCSA_WeakDefAutoPrivate: OS << "\t.weak_def_can_be_hidden\t"; break;
  }

  Symbol->print(OS, MAI);
  EmitEOL();

  return true;
}

void MCAsmStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  OS << ".desc" << ' ';
  Symbol->print(OS, MAI);
  OS << ',' << DescValue;
  EmitEOL();
}

void MCAsmStreamer::EmitSyntaxDirective() {
  if (MAI->getAssemblerDialect() == 1) {
    OS << "\t.intel_syntax noprefix";
    EmitEOL();
  }
  // FIXME: Currently emit unprefix'ed registers.
  // The intel_syntax directive has one optional argument
  // with may have a value of prefix or noprefix.
}

void MCAsmStreamer::BeginCOFFSymbolDef(const MCSymbol *Symbol) {
  OS << "\t.def\t ";
  Symbol->print(OS, MAI);
  OS << ';';
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSymbolStorageClass (int StorageClass) {
  OS << "\t.scl\t" << StorageClass << ';';
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSymbolType (int Type) {
  OS << "\t.type\t" << Type << ';';
  EmitEOL();
}

void MCAsmStreamer::EndCOFFSymbolDef() {
  OS << "\t.endef";
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSafeSEH(MCSymbol const *Symbol) {
  OS << "\t.safeseh\t";
  Symbol->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSymbolIndex(MCSymbol const *Symbol) {
  OS << "\t.symidx\t";
  Symbol->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSectionIndex(MCSymbol const *Symbol) {
  OS << "\t.secidx\t";
  Symbol->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset) {
  OS << "\t.secrel32\t";
  Symbol->print(OS, MAI);
  if (Offset != 0)
    OS << '+' << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset) {
  OS << "\t.rva\t";
  Symbol->print(OS, MAI);
  if (Offset > 0)
    OS << '+' << Offset;
  else if (Offset < 0)
    OS << '-' << -Offset;
  EmitEOL();
}

void MCAsmStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  assert(MAI->hasDotTypeDotSizeDirective());
  OS << "\t.size\t";
  Symbol->print(OS, MAI);
  OS << ", ";
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     unsigned ByteAlignment) {
  OS << "\t.comm\t";
  Symbol->print(OS, MAI);
  OS << ',' << Size;

  if (ByteAlignment != 0) {
    if (MAI->getCOMMDirectiveAlignmentIsInBytes())
      OS << ',' << ByteAlignment;
    else
      OS << ',' << Log2_32(ByteAlignment);
  }
  EmitEOL();
}

void MCAsmStreamer::EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                          unsigned ByteAlign) {
  OS << "\t.lcomm\t";
  Symbol->print(OS, MAI);
  OS << ',' << Size;

  if (ByteAlign > 1) {
    switch (MAI->getLCOMMDirectiveAlignmentType()) {
    case LCOMM::NoAlignment:
      llvm_unreachable("alignment not supported on .lcomm!");
    case LCOMM::ByteAlignment:
      OS << ',' << ByteAlign;
      break;
    case LCOMM::Log2Alignment:
      assert(isPowerOf2_32(ByteAlign) && "alignment must be a power of 2");
      OS << ',' << Log2_32(ByteAlign);
      break;
    }
  }
  EmitEOL();
}

void MCAsmStreamer::EmitZerofill(MCSection *Section, MCSymbol *Symbol,
                                 uint64_t Size, unsigned ByteAlignment,
                                 SMLoc Loc) {
  if (Symbol)
    AssignFragment(Symbol, &Section->getDummyFragment());

  // Note: a .zerofill directive does not switch sections.
  OS << ".zerofill ";

  assert(Section->getVariant() == MCSection::SV_MachO &&
         ".zerofill is a Mach-O specific directive");
  // This is a mach-o specific directive.

  const MCSectionMachO *MOSection = ((const MCSectionMachO*)Section);
  OS << MOSection->getSegmentName() << "," << MOSection->getSectionName();

  if (Symbol) {
    OS << ',';
    Symbol->print(OS, MAI);
    OS << ',' << Size;
    if (ByteAlignment != 0)
      OS << ',' << Log2_32(ByteAlignment);
  }
  EmitEOL();
}

// .tbss sym, size, align
// This depends that the symbol has already been mangled from the original,
// e.g. _a.
void MCAsmStreamer::EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, unsigned ByteAlignment) {
  AssignFragment(Symbol, &Section->getDummyFragment());

  assert(Symbol && "Symbol shouldn't be NULL!");
  // Instead of using the Section we'll just use the shortcut.

  assert(Section->getVariant() == MCSection::SV_MachO &&
         ".zerofill is a Mach-O specific directive");
  // This is a mach-o specific directive and section.

  OS << ".tbss ";
  Symbol->print(OS, MAI);
  OS << ", " << Size;

  // Output align if we have it.  We default to 1 so don't bother printing
  // that.
  if (ByteAlignment > 1) OS << ", " << Log2_32(ByteAlignment);

  EmitEOL();
}

static inline char toOctal(int X) { return (X&7)+'0'; }

static void PrintQuotedString(StringRef Data, raw_ostream &OS) {
  OS << '"';

  for (unsigned i = 0, e = Data.size(); i != e; ++i) {
    unsigned char C = Data[i];
    if (C == '"' || C == '\\') {
      OS << '\\' << (char)C;
      continue;
    }

    if (isPrint((unsigned char)C)) {
      OS << (char)C;
      continue;
    }

    switch (C) {
      case '\b': OS << "\\b"; break;
      case '\f': OS << "\\f"; break;
      case '\n': OS << "\\n"; break;
      case '\r': OS << "\\r"; break;
      case '\t': OS << "\\t"; break;
      default:
        OS << '\\';
        OS << toOctal(C >> 6);
        OS << toOctal(C >> 3);
        OS << toOctal(C >> 0);
        break;
    }
  }

  OS << '"';
}

void MCAsmStreamer::EmitBytes(StringRef Data) {
  assert(getCurrentSectionOnly() &&
         "Cannot emit contents before setting section!");
  if (Data.empty()) return;

  // If only single byte is provided or no ascii or asciz directives is
  // supported, emit as vector of 8bits data.
  if (Data.size() == 1 ||
      !(MAI->getAscizDirective() || MAI->getAsciiDirective())) {
    if (MCTargetStreamer *TS = getTargetStreamer()) {
      TS->emitRawBytes(Data);
    } else {
      const char *Directive = MAI->getData8bitsDirective();
      for (const unsigned char C : Data.bytes()) {
        OS << Directive << (unsigned)C;
        EmitEOL();
      }
    }
    return;
  }

  // If the data ends with 0 and the target supports .asciz, use it, otherwise
  // use .ascii
  if (MAI->getAscizDirective() && Data.back() == 0) {
    OS << MAI->getAscizDirective();
    Data = Data.substr(0, Data.size()-1);
  } else {
    OS << MAI->getAsciiDirective();
  }

  PrintQuotedString(Data, OS);
  EmitEOL();
}

void MCAsmStreamer::EmitBinaryData(StringRef Data) {
  // This is binary data. Print it in a grid of hex bytes for readability.
  const size_t Cols = 4;
  for (size_t I = 0, EI = alignTo(Data.size(), Cols); I < EI; I += Cols) {
    size_t J = I, EJ = std::min(I + Cols, Data.size());
    assert(EJ > 0);
    OS << MAI->getData8bitsDirective();
    for (; J < EJ - 1; ++J)
      OS << format("0x%02x", uint8_t(Data[J])) << ", ";
    OS << format("0x%02x", uint8_t(Data[J]));
    EmitEOL();
  }
}

void MCAsmStreamer::EmitIntValue(uint64_t Value, unsigned Size) {
  EmitValue(MCConstantExpr::create(Value, getContext()), Size);
}

void MCAsmStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size,
                                  SMLoc Loc) {
  assert(Size <= 8 && "Invalid size");
  assert(getCurrentSectionOnly() &&
         "Cannot emit contents before setting section!");
  const char *Directive = nullptr;
  switch (Size) {
  default: break;
  case 1: Directive = MAI->getData8bitsDirective();  break;
  case 2: Directive = MAI->getData16bitsDirective(); break;
  case 4: Directive = MAI->getData32bitsDirective(); break;
  case 8: Directive = MAI->getData64bitsDirective(); break;
  }

  if (!Directive) {
    int64_t IntValue;
    if (!Value->evaluateAsAbsolute(IntValue))
      report_fatal_error("Don't know how to emit this value.");

    // We couldn't handle the requested integer size so we fallback by breaking
    // the request down into several, smaller, integers.
    // Since sizes greater or equal to "Size" are invalid, we use the greatest
    // power of 2 that is less than "Size" as our largest piece of granularity.
    bool IsLittleEndian = MAI->isLittleEndian();
    for (unsigned Emitted = 0; Emitted != Size;) {
      unsigned Remaining = Size - Emitted;
      // The size of our partial emission must be a power of two less than
      // Size.
      unsigned EmissionSize = PowerOf2Floor(std::min(Remaining, Size - 1));
      // Calculate the byte offset of our partial emission taking into account
      // the endianness of the target.
      unsigned ByteOffset =
          IsLittleEndian ? Emitted : (Remaining - EmissionSize);
      uint64_t ValueToEmit = IntValue >> (ByteOffset * 8);
      // We truncate our partial emission to fit within the bounds of the
      // emission domain.  This produces nicer output and silences potential
      // truncation warnings when round tripping through another assembler.
      uint64_t Shift = 64 - EmissionSize * 8;
      assert(Shift < static_cast<uint64_t>(
                         std::numeric_limits<unsigned long long>::digits) &&
             "undefined behavior");
      ValueToEmit &= ~0ULL >> Shift;
      EmitIntValue(ValueToEmit, EmissionSize);
      Emitted += EmissionSize;
    }
    return;
  }

  assert(Directive && "Invalid size for machine code value!");
  OS << Directive;
  if (MCTargetStreamer *TS = getTargetStreamer()) {
    TS->emitValue(Value);
  } else {
    Value->print(OS, MAI);
    EmitEOL();
  }
}

void MCAsmStreamer::EmitULEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue)) {
    EmitULEB128IntValue(IntValue);
    return;
  }
  OS << "\t.uleb128 ";
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitSLEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue)) {
    EmitSLEB128IntValue(IntValue);
    return;
  }
  OS << "\t.sleb128 ";
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitDTPRel64Value(const MCExpr *Value) {
  assert(MAI->getDTPRel64Directive() != nullptr);
  OS << MAI->getDTPRel64Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitDTPRel32Value(const MCExpr *Value) {
  assert(MAI->getDTPRel32Directive() != nullptr);
  OS << MAI->getDTPRel32Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitTPRel64Value(const MCExpr *Value) {
  assert(MAI->getTPRel64Directive() != nullptr);
  OS << MAI->getTPRel64Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitTPRel32Value(const MCExpr *Value) {
  assert(MAI->getTPRel32Directive() != nullptr);
  OS << MAI->getTPRel32Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitGPRel64Value(const MCExpr *Value) {
  assert(MAI->getGPRel64Directive() != nullptr);
  OS << MAI->getGPRel64Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitGPRel32Value(const MCExpr *Value) {
  assert(MAI->getGPRel32Directive() != nullptr);
  OS << MAI->getGPRel32Directive();
  Value->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                             SMLoc Loc) {
  int64_t IntNumBytes;
  if (NumBytes.evaluateAsAbsolute(IntNumBytes) && IntNumBytes == 0)
    return;

  if (const char *ZeroDirective = MAI->getZeroDirective()) {
    // FIXME: Emit location directives
    OS << ZeroDirective;
    NumBytes.print(OS, MAI);
    if (FillValue != 0)
      OS << ',' << (int)FillValue;
    EmitEOL();
    return;
  }

  MCStreamer::emitFill(NumBytes, FillValue);
}

void MCAsmStreamer::emitFill(const MCExpr &NumValues, int64_t Size,
                             int64_t Expr, SMLoc Loc) {
  // FIXME: Emit location directives
  OS << "\t.fill\t";
  NumValues.print(OS, MAI);
  OS << ", " << Size << ", 0x";
  OS.write_hex(truncateToSize(Expr, 4));
  EmitEOL();
}

void MCAsmStreamer::EmitValueToAlignment(unsigned ByteAlignment, int64_t Value,
                                         unsigned ValueSize,
                                         unsigned MaxBytesToEmit) {
  // Some assemblers don't support non-power of two alignments, so we always
  // emit alignments as a power of two if possible.
  if (isPowerOf2_32(ByteAlignment)) {
    switch (ValueSize) {
    default:
      llvm_unreachable("Invalid size for machine code value!");
    case 1:
      OS << "\t.p2align\t";
      break;
    case 2:
      OS << ".p2alignw ";
      break;
    case 4:
      OS << ".p2alignl ";
      break;
    case 8:
      llvm_unreachable("Unsupported alignment size!");
    }

    OS << Log2_32(ByteAlignment);

    if (Value || MaxBytesToEmit) {
      OS << ", 0x";
      OS.write_hex(truncateToSize(Value, ValueSize));

      if (MaxBytesToEmit)
        OS << ", " << MaxBytesToEmit;
    }
    EmitEOL();
    return;
  }

  // Non-power of two alignment.  This is not widely supported by assemblers.
  // FIXME: Parameterize this based on MAI.
  switch (ValueSize) {
  default: llvm_unreachable("Invalid size for machine code value!");
  case 1: OS << ".balign";  break;
  case 2: OS << ".balignw"; break;
  case 4: OS << ".balignl"; break;
  case 8: llvm_unreachable("Unsupported alignment size!");
  }

  OS << ' ' << ByteAlignment;
  OS << ", " << truncateToSize(Value, ValueSize);
  if (MaxBytesToEmit)
    OS << ", " << MaxBytesToEmit;
  EmitEOL();
}

void MCAsmStreamer::EmitCodeAlignment(unsigned ByteAlignment,
                                      unsigned MaxBytesToEmit) {
  // Emit with a text fill value.
  EmitValueToAlignment(ByteAlignment, MAI->getTextAlignFillValue(),
                       1, MaxBytesToEmit);
}

void MCAsmStreamer::emitValueToOffset(const MCExpr *Offset,
                                      unsigned char Value,
                                      SMLoc Loc) {
  // FIXME: Verify that Offset is associated with the current section.
  OS << ".org ";
  Offset->print(OS, MAI);
  OS << ", " << (unsigned)Value;
  EmitEOL();
}

void MCAsmStreamer::EmitFileDirective(StringRef Filename) {
  assert(MAI->hasSingleParameterDotFile());
  OS << "\t.file\t";
  PrintQuotedString(Filename, OS);
  EmitEOL();
}

static void printDwarfFileDirective(unsigned FileNo, StringRef Directory,
                                    StringRef Filename,
                                    MD5::MD5Result *Checksum,
                                    Optional<StringRef> Source,
                                    bool UseDwarfDirectory,
                                    raw_svector_ostream &OS) {
  SmallString<128> FullPathName;

  if (!UseDwarfDirectory && !Directory.empty()) {
    if (sys::path::is_absolute(Filename))
      Directory = "";
    else {
      FullPathName = Directory;
      sys::path::append(FullPathName, Filename);
      Directory = "";
      Filename = FullPathName;
    }
  }

  OS << "\t.file\t" << FileNo << ' ';
  if (!Directory.empty()) {
    PrintQuotedString(Directory, OS);
    OS << ' ';
  }
  PrintQuotedString(Filename, OS);
  if (Checksum)
    OS << " md5 0x" << Checksum->digest();
  if (Source) {
    OS << " source ";
    PrintQuotedString(*Source, OS);
  }
}

Expected<unsigned> MCAsmStreamer::tryEmitDwarfFileDirective(
    unsigned FileNo, StringRef Directory, StringRef Filename,
    MD5::MD5Result *Checksum, Optional<StringRef> Source, unsigned CUID) {
  assert(CUID == 0 && "multiple CUs not supported by MCAsmStreamer");

  MCDwarfLineTable &Table = getContext().getMCDwarfLineTable(CUID);
  unsigned NumFiles = Table.getMCDwarfFiles().size();
  Expected<unsigned> FileNoOrErr =
      Table.tryGetFile(Directory, Filename, Checksum, Source, FileNo);
  if (!FileNoOrErr)
    return FileNoOrErr.takeError();
  FileNo = FileNoOrErr.get();
  if (NumFiles == Table.getMCDwarfFiles().size())
    return FileNo;

  SmallString<128> Str;
  raw_svector_ostream OS1(Str);
  printDwarfFileDirective(FileNo, Directory, Filename, Checksum, Source,
                          UseDwarfDirectory, OS1);

  if (MCTargetStreamer *TS = getTargetStreamer())
    TS->emitDwarfFileDirective(OS1.str());
  else
    EmitRawText(OS1.str());

  return FileNo;
}

void MCAsmStreamer::emitDwarfFile0Directive(StringRef Directory,
                                            StringRef Filename,
                                            MD5::MD5Result *Checksum,
                                            Optional<StringRef> Source,
                                            unsigned CUID) {
  assert(CUID == 0);
  // .file 0 is new for DWARF v5.
  if (getContext().getDwarfVersion() < 5)
    return;
  // Inform MCDwarf about the root file.
  getContext().setMCLineTableRootFile(CUID, Directory, Filename, Checksum,
                                      Source);

  SmallString<128> Str;
  raw_svector_ostream OS1(Str);
  printDwarfFileDirective(0, Directory, Filename, Checksum, Source,
                          UseDwarfDirectory, OS1);

  if (MCTargetStreamer *TS = getTargetStreamer())
    TS->emitDwarfFileDirective(OS1.str());
  else
    EmitRawText(OS1.str());
}

void MCAsmStreamer::EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                          unsigned Column, unsigned Flags,
                                          unsigned Isa,
                                          unsigned Discriminator,
                                          StringRef FileName) {
  OS << "\t.loc\t" << FileNo << " " << Line << " " << Column;
  if (MAI->supportsExtendedDwarfLocDirective()) {
    if (Flags & DWARF2_FLAG_BASIC_BLOCK)
      OS << " basic_block";
    if (Flags & DWARF2_FLAG_PROLOGUE_END)
      OS << " prologue_end";
    if (Flags & DWARF2_FLAG_EPILOGUE_BEGIN)
      OS << " epilogue_begin";

    unsigned OldFlags = getContext().getCurrentDwarfLoc().getFlags();
    if ((Flags & DWARF2_FLAG_IS_STMT) != (OldFlags & DWARF2_FLAG_IS_STMT)) {
      OS << " is_stmt ";

      if (Flags & DWARF2_FLAG_IS_STMT)
        OS << "1";
      else
        OS << "0";
    }

    if (Isa)
      OS << " isa " << Isa;
    if (Discriminator)
      OS << " discriminator " << Discriminator;
  }

  if (IsVerboseAsm) {
    OS.PadToColumn(MAI->getCommentColumn());
    OS << MAI->getCommentString() << ' ' << FileName << ':'
       << Line << ':' << Column;
  }
  EmitEOL();
  this->MCStreamer::EmitDwarfLocDirective(FileNo, Line, Column, Flags,
                                          Isa, Discriminator, FileName);
}

MCSymbol *MCAsmStreamer::getDwarfLineTableSymbol(unsigned CUID) {
  // Always use the zeroth line table, since asm syntax only supports one line
  // table for now.
  return MCStreamer::getDwarfLineTableSymbol(0);
}

bool MCAsmStreamer::EmitCVFileDirective(unsigned FileNo, StringRef Filename,
                                        ArrayRef<uint8_t> Checksum,
                                        unsigned ChecksumKind) {
  if (!getContext().getCVContext().addFile(*this, FileNo, Filename, Checksum,
                                           ChecksumKind))
    return false;

  OS << "\t.cv_file\t" << FileNo << ' ';
  PrintQuotedString(Filename, OS);

  if (!ChecksumKind) {
    EmitEOL();
    return true;
  }

  OS << ' ';
  PrintQuotedString(toHex(Checksum), OS);
  OS << ' ' << ChecksumKind;

  EmitEOL();
  return true;
}

bool MCAsmStreamer::EmitCVFuncIdDirective(unsigned FuncId) {
  OS << "\t.cv_func_id " << FuncId << '\n';
  return MCStreamer::EmitCVFuncIdDirective(FuncId);
}

bool MCAsmStreamer::EmitCVInlineSiteIdDirective(unsigned FunctionId,
                                                unsigned IAFunc,
                                                unsigned IAFile,
                                                unsigned IALine, unsigned IACol,
                                                SMLoc Loc) {
  OS << "\t.cv_inline_site_id " << FunctionId << " within " << IAFunc
     << " inlined_at " << IAFile << ' ' << IALine << ' ' << IACol << '\n';
  return MCStreamer::EmitCVInlineSiteIdDirective(FunctionId, IAFunc, IAFile,
                                                 IALine, IACol, Loc);
}

void MCAsmStreamer::EmitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                       unsigned Line, unsigned Column,
                                       bool PrologueEnd, bool IsStmt,
                                       StringRef FileName, SMLoc Loc) {
  // Validate the directive.
  if (!checkCVLocSection(FunctionId, FileNo, Loc))
    return;

  OS << "\t.cv_loc\t" << FunctionId << " " << FileNo << " " << Line << " "
     << Column;
  if (PrologueEnd)
    OS << " prologue_end";

  if (IsStmt)
    OS << " is_stmt 1";

  if (IsVerboseAsm) {
    OS.PadToColumn(MAI->getCommentColumn());
    OS << MAI->getCommentString() << ' ' << FileName << ':' << Line << ':'
       << Column;
  }
  EmitEOL();
}

void MCAsmStreamer::EmitCVLinetableDirective(unsigned FunctionId,
                                             const MCSymbol *FnStart,
                                             const MCSymbol *FnEnd) {
  OS << "\t.cv_linetable\t" << FunctionId << ", ";
  FnStart->print(OS, MAI);
  OS << ", ";
  FnEnd->print(OS, MAI);
  EmitEOL();
  this->MCStreamer::EmitCVLinetableDirective(FunctionId, FnStart, FnEnd);
}

void MCAsmStreamer::EmitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                                   unsigned SourceFileId,
                                                   unsigned SourceLineNum,
                                                   const MCSymbol *FnStartSym,
                                                   const MCSymbol *FnEndSym) {
  OS << "\t.cv_inline_linetable\t" << PrimaryFunctionId << ' ' << SourceFileId
     << ' ' << SourceLineNum << ' ';
  FnStartSym->print(OS, MAI);
  OS << ' ';
  FnEndSym->print(OS, MAI);
  EmitEOL();
  this->MCStreamer::EmitCVInlineLinetableDirective(
      PrimaryFunctionId, SourceFileId, SourceLineNum, FnStartSym, FnEndSym);
}

void MCAsmStreamer::EmitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {
  OS << "\t.cv_def_range\t";
  for (std::pair<const MCSymbol *, const MCSymbol *> Range : Ranges) {
    OS << ' ';
    Range.first->print(OS, MAI);
    OS << ' ';
    Range.second->print(OS, MAI);
  }
  OS << ", ";
  PrintQuotedString(FixedSizePortion, OS);
  EmitEOL();
  this->MCStreamer::EmitCVDefRangeDirective(Ranges, FixedSizePortion);
}

void MCAsmStreamer::EmitCVStringTableDirective() {
  OS << "\t.cv_stringtable";
  EmitEOL();
}

void MCAsmStreamer::EmitCVFileChecksumsDirective() {
  OS << "\t.cv_filechecksums";
  EmitEOL();
}

void MCAsmStreamer::EmitCVFileChecksumOffsetDirective(unsigned FileNo) {
  OS << "\t.cv_filechecksumoffset\t" << FileNo;
  EmitEOL();
}

void MCAsmStreamer::EmitCVFPOData(const MCSymbol *ProcSym, SMLoc L) {
  OS << "\t.cv_fpo_data\t";
  ProcSym->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitIdent(StringRef IdentString) {
  assert(MAI->hasIdentDirective() && ".ident directive not supported");
  OS << "\t.ident\t";
  PrintQuotedString(IdentString, OS);
  EmitEOL();
}

void MCAsmStreamer::EmitCFISections(bool EH, bool Debug) {
  MCStreamer::EmitCFISections(EH, Debug);
  OS << "\t.cfi_sections ";
  if (EH) {
    OS << ".eh_frame";
    if (Debug)
      OS << ", .debug_frame";
  } else if (Debug) {
    OS << ".debug_frame";
  }

  EmitEOL();
}

void MCAsmStreamer::EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame) {
  OS << "\t.cfi_startproc";
  if (Frame.IsSimple)
    OS << " simple";
  EmitEOL();
}

void MCAsmStreamer::EmitCFIEndProcImpl(MCDwarfFrameInfo &Frame) {
  MCStreamer::EmitCFIEndProcImpl(Frame);
  OS << "\t.cfi_endproc";
  EmitEOL();
}

void MCAsmStreamer::EmitRegisterName(int64_t Register) {
  if (!MAI->useDwarfRegNumForCFI()) {
    // User .cfi_* directives can use arbitrary DWARF register numbers, not
    // just ones that map to LLVM register numbers and have known names.
    // Fall back to using the original number directly if no name is known.
    const MCRegisterInfo *MRI = getContext().getRegisterInfo();
    int LLVMRegister = MRI->getLLVMRegNumFromEH(Register);
    if (LLVMRegister != -1) {
      InstPrinter->printRegName(OS, LLVMRegister);
      return;
    }
  }
  OS << Register;
}

void MCAsmStreamer::EmitCFIDefCfa(int64_t Register, int64_t Offset) {
  MCStreamer::EmitCFIDefCfa(Register, Offset);
  OS << "\t.cfi_def_cfa ";
  EmitRegisterName(Register);
  OS << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIDefCfaOffset(int64_t Offset) {
  MCStreamer::EmitCFIDefCfaOffset(Offset);
  OS << "\t.cfi_def_cfa_offset " << Offset;
  EmitEOL();
}

static void PrintCFIEscape(llvm::formatted_raw_ostream &OS, StringRef Values) {
  OS << "\t.cfi_escape ";
  if (!Values.empty()) {
    size_t e = Values.size() - 1;
    for (size_t i = 0; i < e; ++i)
      OS << format("0x%02x", uint8_t(Values[i])) << ", ";
    OS << format("0x%02x", uint8_t(Values[e]));
  }
}

void MCAsmStreamer::EmitCFIEscape(StringRef Values) {
  MCStreamer::EmitCFIEscape(Values);
  PrintCFIEscape(OS, Values);
  EmitEOL();
}

void MCAsmStreamer::EmitCFIGnuArgsSize(int64_t Size) {
  MCStreamer::EmitCFIGnuArgsSize(Size);

  uint8_t Buffer[16] = { dwarf::DW_CFA_GNU_args_size };
  unsigned Len = encodeULEB128(Size, Buffer + 1) + 1;

  PrintCFIEscape(OS, StringRef((const char *)&Buffer[0], Len));
  EmitEOL();
}

void MCAsmStreamer::EmitCFIDefCfaRegister(int64_t Register) {
  MCStreamer::EmitCFIDefCfaRegister(Register);
  OS << "\t.cfi_def_cfa_register ";
  EmitRegisterName(Register);
  EmitEOL();
}

void MCAsmStreamer::EmitCFIOffset(int64_t Register, int64_t Offset) {
  this->MCStreamer::EmitCFIOffset(Register, Offset);
  OS << "\t.cfi_offset ";
  EmitRegisterName(Register);
  OS << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIPersonality(const MCSymbol *Sym,
                                       unsigned Encoding) {
  MCStreamer::EmitCFIPersonality(Sym, Encoding);
  OS << "\t.cfi_personality " << Encoding << ", ";
  Sym->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCFILsda(const MCSymbol *Sym, unsigned Encoding) {
  MCStreamer::EmitCFILsda(Sym, Encoding);
  OS << "\t.cfi_lsda " << Encoding << ", ";
  Sym->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitCFIRememberState() {
  MCStreamer::EmitCFIRememberState();
  OS << "\t.cfi_remember_state";
  EmitEOL();
}

void MCAsmStreamer::EmitCFIRestoreState() {
  MCStreamer::EmitCFIRestoreState();
  OS << "\t.cfi_restore_state";
  EmitEOL();
}

void MCAsmStreamer::EmitCFIRestore(int64_t Register) {
  MCStreamer::EmitCFIRestore(Register);
  OS << "\t.cfi_restore ";
  EmitRegisterName(Register);
  EmitEOL();
}

void MCAsmStreamer::EmitCFISameValue(int64_t Register) {
  MCStreamer::EmitCFISameValue(Register);
  OS << "\t.cfi_same_value ";
  EmitRegisterName(Register);
  EmitEOL();
}

void MCAsmStreamer::EmitCFIRelOffset(int64_t Register, int64_t Offset) {
  MCStreamer::EmitCFIRelOffset(Register, Offset);
  OS << "\t.cfi_rel_offset ";
  EmitRegisterName(Register);
  OS << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIAdjustCfaOffset(int64_t Adjustment) {
  MCStreamer::EmitCFIAdjustCfaOffset(Adjustment);
  OS << "\t.cfi_adjust_cfa_offset " << Adjustment;
  EmitEOL();
}

void MCAsmStreamer::EmitCFISignalFrame() {
  MCStreamer::EmitCFISignalFrame();
  OS << "\t.cfi_signal_frame";
  EmitEOL();
}

void MCAsmStreamer::EmitCFIUndefined(int64_t Register) {
  MCStreamer::EmitCFIUndefined(Register);
  OS << "\t.cfi_undefined " << Register;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIRegister(int64_t Register1, int64_t Register2) {
  MCStreamer::EmitCFIRegister(Register1, Register2);
  OS << "\t.cfi_register " << Register1 << ", " << Register2;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIWindowSave() {
  MCStreamer::EmitCFIWindowSave();
  OS << "\t.cfi_window_save";
  EmitEOL();
}

void MCAsmStreamer::EmitCFINegateRAState() {
  MCStreamer::EmitCFINegateRAState();
  OS << "\t.cfi_negate_ra_state";
  EmitEOL();
}

void MCAsmStreamer::EmitCFIReturnColumn(int64_t Register) {
  MCStreamer::EmitCFIReturnColumn(Register);
  OS << "\t.cfi_return_column " << Register;
  EmitEOL();
}

void MCAsmStreamer::EmitCFIBKeyFrame() {
  MCStreamer::EmitCFIBKeyFrame();
  OS << "\t.cfi_b_key_frame";
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc) {
  MCStreamer::EmitWinCFIStartProc(Symbol, Loc);

  OS << ".seh_proc ";
  Symbol->print(OS, MAI);
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIEndProc(SMLoc Loc) {
  MCStreamer::EmitWinCFIEndProc(Loc);

  OS << "\t.seh_endproc";
  EmitEOL();
}

// TODO: Implement
void MCAsmStreamer::EmitWinCFIFuncletOrFuncEnd(SMLoc Loc) {
}

void MCAsmStreamer::EmitWinCFIStartChained(SMLoc Loc) {
  MCStreamer::EmitWinCFIStartChained(Loc);

  OS << "\t.seh_startchained";
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIEndChained(SMLoc Loc) {
  MCStreamer::EmitWinCFIEndChained(Loc);

  OS << "\t.seh_endchained";
  EmitEOL();
}

void MCAsmStreamer::EmitWinEHHandler(const MCSymbol *Sym, bool Unwind,
                                     bool Except, SMLoc Loc) {
  MCStreamer::EmitWinEHHandler(Sym, Unwind, Except, Loc);

  OS << "\t.seh_handler ";
  Sym->print(OS, MAI);
  if (Unwind)
    OS << ", @unwind";
  if (Except)
    OS << ", @except";
  EmitEOL();
}

void MCAsmStreamer::EmitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::EmitWinEHHandlerData(Loc);

  // Switch sections. Don't call SwitchSection directly, because that will
  // cause the section switch to be visible in the emitted assembly.
  // We only do this so the section switch that terminates the handler
  // data block is visible.
  WinEH::FrameInfo *CurFrame = getCurrentWinFrameInfo();
  MCSection *TextSec = &CurFrame->Function->getSection();
  MCSection *XData = getAssociatedXDataSection(TextSec);
  SwitchSectionNoChange(XData);

  OS << "\t.seh_handlerdata";
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIPushReg(unsigned Register, SMLoc Loc) {
  MCStreamer::EmitWinCFIPushReg(Register, Loc);

  OS << "\t.seh_pushreg " << Register;
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFISetFrame(unsigned Register, unsigned Offset,
                                       SMLoc Loc) {
  MCStreamer::EmitWinCFISetFrame(Register, Offset, Loc);

  OS << "\t.seh_setframe " << Register << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIAllocStack(unsigned Size, SMLoc Loc) {
  MCStreamer::EmitWinCFIAllocStack(Size, Loc);

  OS << "\t.seh_stackalloc " << Size;
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFISaveReg(unsigned Register, unsigned Offset,
                                      SMLoc Loc) {
  MCStreamer::EmitWinCFISaveReg(Register, Offset, Loc);

  OS << "\t.seh_savereg " << Register << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFISaveXMM(unsigned Register, unsigned Offset,
                                      SMLoc Loc) {
  MCStreamer::EmitWinCFISaveXMM(Register, Offset, Loc);

  OS << "\t.seh_savexmm " << Register << ", " << Offset;
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIPushFrame(bool Code, SMLoc Loc) {
  MCStreamer::EmitWinCFIPushFrame(Code, Loc);

  OS << "\t.seh_pushframe";
  if (Code)
    OS << " @code";
  EmitEOL();
}

void MCAsmStreamer::EmitWinCFIEndProlog(SMLoc Loc) {
  MCStreamer::EmitWinCFIEndProlog(Loc);

  OS << "\t.seh_endprologue";
  EmitEOL();
}

void MCAsmStreamer::emitCGProfileEntry(const MCSymbolRefExpr *From,
                                       const MCSymbolRefExpr *To,
                                       uint64_t Count) {
  OS << "\t.cg_profile ";
  From->getSymbol().print(OS, MAI);
  OS << ", ";
  To->getSymbol().print(OS, MAI);
  OS << ", " << Count;
  EmitEOL();
}

void MCAsmStreamer::AddEncodingComment(const MCInst &Inst,
                                       const MCSubtargetInfo &STI,
                                       bool PrintSchedInfo) {
  raw_ostream &OS = GetCommentOS();
  SmallString<256> Code;
  SmallVector<MCFixup, 4> Fixups;
  raw_svector_ostream VecOS(Code);

  // If we have no code emitter, don't emit code.
  if (!getAssembler().getEmitterPtr())
    return;

  getAssembler().getEmitter().encodeInstruction(Inst, VecOS, Fixups, STI);

  // If we are showing fixups, create symbolic markers in the encoded
  // representation. We do this by making a per-bit map to the fixup item index,
  // then trying to display it as nicely as possible.
  SmallVector<uint8_t, 64> FixupMap;
  FixupMap.resize(Code.size() * 8);
  for (unsigned i = 0, e = Code.size() * 8; i != e; ++i)
    FixupMap[i] = 0;

  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    MCFixup &F = Fixups[i];
    const MCFixupKindInfo &Info =
        getAssembler().getBackend().getFixupKindInfo(F.getKind());
    for (unsigned j = 0; j != Info.TargetSize; ++j) {
      unsigned Index = F.getOffset() * 8 + Info.TargetOffset + j;
      assert(Index < Code.size() * 8 && "Invalid offset in fixup!");
      FixupMap[Index] = 1 + i;
    }
  }

  // FIXME: Note the fixup comments for Thumb2 are completely bogus since the
  // high order halfword of a 32-bit Thumb2 instruction is emitted first.
  OS << "encoding: [";
  for (unsigned i = 0, e = Code.size(); i != e; ++i) {
    if (i)
      OS << ',';

    // See if all bits are the same map entry.
    uint8_t MapEntry = FixupMap[i * 8 + 0];
    for (unsigned j = 1; j != 8; ++j) {
      if (FixupMap[i * 8 + j] == MapEntry)
        continue;

      MapEntry = uint8_t(~0U);
      break;
    }

    if (MapEntry != uint8_t(~0U)) {
      if (MapEntry == 0) {
        OS << format("0x%02x", uint8_t(Code[i]));
      } else {
        if (Code[i]) {
          // FIXME: Some of the 8 bits require fix up.
          OS << format("0x%02x", uint8_t(Code[i])) << '\''
             << char('A' + MapEntry - 1) << '\'';
        } else
          OS << char('A' + MapEntry - 1);
      }
    } else {
      // Otherwise, write out in binary.
      OS << "0b";
      for (unsigned j = 8; j--;) {
        unsigned Bit = (Code[i] >> j) & 1;

        unsigned FixupBit;
        if (MAI->isLittleEndian())
          FixupBit = i * 8 + j;
        else
          FixupBit = i * 8 + (7-j);

        if (uint8_t MapEntry = FixupMap[FixupBit]) {
          assert(Bit == 0 && "Encoder wrote into fixed up bit!");
          OS << char('A' + MapEntry - 1);
        } else
          OS << Bit;
      }
    }
  }
  OS << "]";
  // If we are not going to add fixup or schedule comments after this point
  // then we have to end the current comment line with "\n".
  if (Fixups.size() || !PrintSchedInfo)
    OS << "\n";

  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    MCFixup &F = Fixups[i];
    const MCFixupKindInfo &Info =
        getAssembler().getBackend().getFixupKindInfo(F.getKind());
    OS << "  fixup " << char('A' + i) << " - " << "offset: " << F.getOffset()
       << ", value: " << *F.getValue() << ", kind: " << Info.Name << "\n";
  }
}

void MCAsmStreamer::EmitInstruction(const MCInst &Inst,
                                    const MCSubtargetInfo &STI,
                                    bool PrintSchedInfo) {
  assert(getCurrentSectionOnly() &&
         "Cannot emit contents before setting section!");

  // Show the encoding in a comment if we have a code emitter.
  AddEncodingComment(Inst, STI, PrintSchedInfo);

  // Show the MCInst if enabled.
  if (ShowInst) {
    if (PrintSchedInfo)
      GetCommentOS() << "\n";
    Inst.dump_pretty(GetCommentOS(), InstPrinter.get(), "\n ");
    GetCommentOS() << "\n";
  }

  if(getTargetStreamer())
    getTargetStreamer()->prettyPrintAsm(*InstPrinter, OS, Inst, STI);
  else
    InstPrinter->printInst(&Inst, OS, "", STI);

  if (PrintSchedInfo) {
    std::string SI = STI.getSchedInfoStr(Inst);
    if (!SI.empty())
      GetCommentOS() << SI;
  }

  StringRef Comments = CommentToEmit;
  if (Comments.size() && Comments.back() != '\n')
    GetCommentOS() << "\n";

  EmitEOL();
}

void MCAsmStreamer::EmitBundleAlignMode(unsigned AlignPow2) {
  OS << "\t.bundle_align_mode " << AlignPow2;
  EmitEOL();
}

void MCAsmStreamer::EmitBundleLock(bool AlignToEnd) {
  OS << "\t.bundle_lock";
  if (AlignToEnd)
    OS << " align_to_end";
  EmitEOL();
}

void MCAsmStreamer::EmitBundleUnlock() {
  OS << "\t.bundle_unlock";
  EmitEOL();
}

bool MCAsmStreamer::EmitRelocDirective(const MCExpr &Offset, StringRef Name,
                                       const MCExpr *Expr, SMLoc,
                                       const MCSubtargetInfo &STI) {
  OS << "\t.reloc ";
  Offset.print(OS, MAI);
  OS << ", " << Name;
  if (Expr) {
    OS << ", ";
    Expr->print(OS, MAI);
  }
  EmitEOL();
  return false;
}

void MCAsmStreamer::EmitAddrsig() {
  OS << "\t.addrsig";
  EmitEOL();
}

void MCAsmStreamer::EmitAddrsigSym(const MCSymbol *Sym) {
  OS << "\t.addrsig_sym ";
  Sym->print(OS, MAI);
  EmitEOL();
}

/// EmitRawText - If this file is backed by an assembly streamer, this dumps
/// the specified string in the output .s file.  This capability is
/// indicated by the hasRawTextSupport() predicate.
void MCAsmStreamer::EmitRawTextImpl(StringRef String) {
  if (!String.empty() && String.back() == '\n')
    String = String.substr(0, String.size()-1);
  OS << String;
  EmitEOL();
}

void MCAsmStreamer::FinishImpl() {
  // If we are generating dwarf for assembly source files dump out the sections.
  if (getContext().getGenDwarfForAssembly())
    MCGenDwarfInfo::Emit(this);

  // Emit the label for the line table, if requested - since the rest of the
  // line table will be defined by .loc/.file directives, and not emitted
  // directly, the label is the only work required here.
  auto &Tables = getContext().getMCDwarfLineTables();
  if (!Tables.empty()) {
    assert(Tables.size() == 1 && "asm output only supports one line table");
    if (auto *Label = Tables.begin()->second.getLabel()) {
      SwitchSection(getContext().getObjectFileInfo()->getDwarfLineSection());
      EmitLabel(Label);
    }
  }
}

MCStreamer *llvm::createAsmStreamer(MCContext &Context,
                                    std::unique_ptr<formatted_raw_ostream> OS,
                                    bool isVerboseAsm, bool useDwarfDirectory,
                                    MCInstPrinter *IP,
                                    std::unique_ptr<MCCodeEmitter> &&CE,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    bool ShowInst) {
  return new MCAsmStreamer(Context, std::move(OS), isVerboseAsm,
                           useDwarfDirectory, IP, std::move(CE), std::move(MAB),
                           ShowInst);
}
