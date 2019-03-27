//===- MCDwarf.h - Machine Code Dwarf support -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCDwarfFile to support the dwarf
// .file directive and the .loc directive.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDWARF_H
#define LLVM_MC_MCDWARF_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCSection.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

template <typename T> class ArrayRef;
class MCAsmBackend;
class MCContext;
class MCDwarfLineStr;
class MCObjectStreamer;
class MCStreamer;
class MCSymbol;
class raw_ostream;
class SMLoc;
class SourceMgr;

/// Instances of this class represent the name of the dwarf
/// .file directive and its associated dwarf file number in the MC file,
/// and MCDwarfFile's are created and uniqued by the MCContext class where
/// the file number for each is its index into the vector of DwarfFiles (note
/// index 0 is not used and not a valid dwarf file number).
struct MCDwarfFile {
  // The base name of the file without its directory path.
  std::string Name;

  // The index into the list of directory names for this file name.
  unsigned DirIndex;

  /// The MD5 checksum, if there is one. Non-owning pointer to data allocated
  /// in MCContext.
  MD5::MD5Result *Checksum = nullptr;

  /// The source code of the file. Non-owning reference to data allocated in
  /// MCContext.
  Optional<StringRef> Source;
};

/// Instances of this class represent the information from a
/// dwarf .loc directive.
class MCDwarfLoc {
  uint32_t FileNum;
  uint32_t Line;
  uint16_t Column;
  // Flags (see #define's below)
  uint8_t Flags;
  uint8_t Isa;
  uint32_t Discriminator;

// Flag that indicates the initial value of the is_stmt_start flag.
#define DWARF2_LINE_DEFAULT_IS_STMT 1

#define DWARF2_FLAG_IS_STMT (1 << 0)
#define DWARF2_FLAG_BASIC_BLOCK (1 << 1)
#define DWARF2_FLAG_PROLOGUE_END (1 << 2)
#define DWARF2_FLAG_EPILOGUE_BEGIN (1 << 3)

private: // MCContext manages these
  friend class MCContext;
  friend class MCDwarfLineEntry;

  MCDwarfLoc(unsigned fileNum, unsigned line, unsigned column, unsigned flags,
             unsigned isa, unsigned discriminator)
      : FileNum(fileNum), Line(line), Column(column), Flags(flags), Isa(isa),
        Discriminator(discriminator) {}

  // Allow the default copy constructor and assignment operator to be used
  // for an MCDwarfLoc object.

public:
  /// Get the FileNum of this MCDwarfLoc.
  unsigned getFileNum() const { return FileNum; }

  /// Get the Line of this MCDwarfLoc.
  unsigned getLine() const { return Line; }

  /// Get the Column of this MCDwarfLoc.
  unsigned getColumn() const { return Column; }

  /// Get the Flags of this MCDwarfLoc.
  unsigned getFlags() const { return Flags; }

  /// Get the Isa of this MCDwarfLoc.
  unsigned getIsa() const { return Isa; }

  /// Get the Discriminator of this MCDwarfLoc.
  unsigned getDiscriminator() const { return Discriminator; }

  /// Set the FileNum of this MCDwarfLoc.
  void setFileNum(unsigned fileNum) { FileNum = fileNum; }

  /// Set the Line of this MCDwarfLoc.
  void setLine(unsigned line) { Line = line; }

  /// Set the Column of this MCDwarfLoc.
  void setColumn(unsigned column) {
    assert(column <= UINT16_MAX);
    Column = column;
  }

  /// Set the Flags of this MCDwarfLoc.
  void setFlags(unsigned flags) {
    assert(flags <= UINT8_MAX);
    Flags = flags;
  }

  /// Set the Isa of this MCDwarfLoc.
  void setIsa(unsigned isa) {
    assert(isa <= UINT8_MAX);
    Isa = isa;
  }

  /// Set the Discriminator of this MCDwarfLoc.
  void setDiscriminator(unsigned discriminator) {
    Discriminator = discriminator;
  }
};

/// Instances of this class represent the line information for
/// the dwarf line table entries.  Which is created after a machine
/// instruction is assembled and uses an address from a temporary label
/// created at the current address in the current section and the info from
/// the last .loc directive seen as stored in the context.
class MCDwarfLineEntry : public MCDwarfLoc {
  MCSymbol *Label;

private:
  // Allow the default copy constructor and assignment operator to be used
  // for an MCDwarfLineEntry object.

public:
  // Constructor to create an MCDwarfLineEntry given a symbol and the dwarf loc.
  MCDwarfLineEntry(MCSymbol *label, const MCDwarfLoc loc)
      : MCDwarfLoc(loc), Label(label) {}

  MCSymbol *getLabel() const { return Label; }

  // This is called when an instruction is assembled into the specified
  // section and if there is information from the last .loc directive that
  // has yet to have a line entry made for it is made.
  static void Make(MCObjectStreamer *MCOS, MCSection *Section);
};

/// Instances of this class represent the line information for a compile
/// unit where machine instructions have been assembled after seeing .loc
/// directives.  This is the information used to build the dwarf line
/// table for a section.
class MCLineSection {
public:
  // Add an entry to this MCLineSection's line entries.
  void addLineEntry(const MCDwarfLineEntry &LineEntry, MCSection *Sec) {
    MCLineDivisions[Sec].push_back(LineEntry);
  }

  using MCDwarfLineEntryCollection = std::vector<MCDwarfLineEntry>;
  using iterator = MCDwarfLineEntryCollection::iterator;
  using const_iterator = MCDwarfLineEntryCollection::const_iterator;
  using MCLineDivisionMap = MapVector<MCSection *, MCDwarfLineEntryCollection>;

private:
  // A collection of MCDwarfLineEntry for each section.
  MCLineDivisionMap MCLineDivisions;

public:
  // Returns the collection of MCDwarfLineEntry for a given Compile Unit ID.
  const MCLineDivisionMap &getMCLineEntries() const {
    return MCLineDivisions;
  }
};

struct MCDwarfLineTableParams {
  /// First special line opcode - leave room for the standard opcodes.
  /// Note: If you want to change this, you'll have to update the
  /// "StandardOpcodeLengths" table that is emitted in
  /// \c Emit().
  uint8_t DWARF2LineOpcodeBase = 13;
  /// Minimum line offset in a special line info. opcode.  The value
  /// -5 was chosen to give a reasonable range of values.
  int8_t DWARF2LineBase = -5;
  /// Range of line offsets in a special line info. opcode.
  uint8_t DWARF2LineRange = 14;
};

struct MCDwarfLineTableHeader {
  MCSymbol *Label = nullptr;
  SmallVector<std::string, 3> MCDwarfDirs;
  SmallVector<MCDwarfFile, 3> MCDwarfFiles;
  StringMap<unsigned> SourceIdMap;
  std::string CompilationDir;
  MCDwarfFile RootFile;
  bool HasSource = false;
private:
  bool HasAllMD5 = true;
  bool HasAnyMD5 = false;

public:
  MCDwarfLineTableHeader() = default;

  Expected<unsigned> tryGetFile(StringRef &Directory, StringRef &FileName,
                                MD5::MD5Result *Checksum,
                                Optional<StringRef> &Source,
                                unsigned FileNumber = 0);
  std::pair<MCSymbol *, MCSymbol *>
  Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
       Optional<MCDwarfLineStr> &LineStr) const;
  std::pair<MCSymbol *, MCSymbol *>
  Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
       ArrayRef<char> SpecialOpcodeLengths,
       Optional<MCDwarfLineStr> &LineStr) const;
  void resetMD5Usage() {
    HasAllMD5 = true;
    HasAnyMD5 = false;
  }
  void trackMD5Usage(bool MD5Used) {
    HasAllMD5 &= MD5Used;
    HasAnyMD5 |= MD5Used;
  }
  bool isMD5UsageConsistent() const {
    return MCDwarfFiles.empty() || (HasAllMD5 == HasAnyMD5);
  }

private:
  void emitV2FileDirTables(MCStreamer *MCOS) const;
  void emitV5FileDirTables(MCStreamer *MCOS, Optional<MCDwarfLineStr> &LineStr,
                           StringRef CtxCompilationDir) const;
};

class MCDwarfDwoLineTable {
  MCDwarfLineTableHeader Header;

public:
  void maybeSetRootFile(StringRef Directory, StringRef FileName,
                        MD5::MD5Result *Checksum, Optional<StringRef> Source) {
    if (!Header.RootFile.Name.empty())
      return;
    Header.CompilationDir = Directory;
    Header.RootFile.Name = FileName;
    Header.RootFile.DirIndex = 0;
    Header.RootFile.Checksum = Checksum;
    Header.RootFile.Source = Source;
    Header.trackMD5Usage(Checksum);
    Header.HasSource = Source.hasValue();
  }

  unsigned getFile(StringRef Directory, StringRef FileName,
                   MD5::MD5Result *Checksum, Optional<StringRef> Source) {
    return cantFail(Header.tryGetFile(Directory, FileName, Checksum, Source));
  }

  void Emit(MCStreamer &MCOS, MCDwarfLineTableParams Params,
            MCSection *Section) const;
};

class MCDwarfLineTable {
  MCDwarfLineTableHeader Header;
  MCLineSection MCLineSections;

public:
  // This emits the Dwarf file and the line tables for all Compile Units.
  static void Emit(MCObjectStreamer *MCOS, MCDwarfLineTableParams Params);

  // This emits the Dwarf file and the line tables for a given Compile Unit.
  void EmitCU(MCObjectStreamer *MCOS, MCDwarfLineTableParams Params,
              Optional<MCDwarfLineStr> &LineStr) const;

  Expected<unsigned> tryGetFile(StringRef &Directory, StringRef &FileName,
                                MD5::MD5Result *Checksum,
                                Optional<StringRef> Source,
                                unsigned FileNumber = 0);
  unsigned getFile(StringRef &Directory, StringRef &FileName,
                   MD5::MD5Result *Checksum, Optional<StringRef> &Source,
                   unsigned FileNumber = 0) {
    return cantFail(tryGetFile(Directory, FileName, Checksum, Source,
                               FileNumber));
  }

  void setRootFile(StringRef Directory, StringRef FileName,
                   MD5::MD5Result *Checksum, Optional<StringRef> Source) {
    Header.CompilationDir = Directory;
    Header.RootFile.Name = FileName;
    Header.RootFile.DirIndex = 0;
    Header.RootFile.Checksum = Checksum;
    Header.RootFile.Source = Source;
    Header.trackMD5Usage(Checksum);
    Header.HasSource = Source.hasValue();
  }

  void resetRootFile() {
    assert(Header.MCDwarfFiles.empty());
    Header.RootFile.Name.clear();
    Header.resetMD5Usage();
    Header.HasSource = false;
  }

  bool hasRootFile() const { return !Header.RootFile.Name.empty(); }

  // Report whether MD5 usage has been consistent (all-or-none).
  bool isMD5UsageConsistent() const { return Header.isMD5UsageConsistent(); }

  MCSymbol *getLabel() const {
    return Header.Label;
  }

  void setLabel(MCSymbol *Label) {
    Header.Label = Label;
  }

  const SmallVectorImpl<std::string> &getMCDwarfDirs() const {
    return Header.MCDwarfDirs;
  }

  SmallVectorImpl<std::string> &getMCDwarfDirs() {
    return Header.MCDwarfDirs;
  }

  const SmallVectorImpl<MCDwarfFile> &getMCDwarfFiles() const {
    return Header.MCDwarfFiles;
  }

  SmallVectorImpl<MCDwarfFile> &getMCDwarfFiles() {
    return Header.MCDwarfFiles;
  }

  const MCLineSection &getMCLineSections() const {
    return MCLineSections;
  }
  MCLineSection &getMCLineSections() {
    return MCLineSections;
  }
};

class MCDwarfLineAddr {
public:
  /// Utility function to encode a Dwarf pair of LineDelta and AddrDeltas.
  static void Encode(MCContext &Context, MCDwarfLineTableParams Params,
                     int64_t LineDelta, uint64_t AddrDelta, raw_ostream &OS);

  /// Utility function to encode a Dwarf pair of LineDelta and AddrDeltas using
  /// fixed length operands.
  static bool FixedEncode(MCContext &Context,
                          MCDwarfLineTableParams Params,
                          int64_t LineDelta, uint64_t AddrDelta,
                          raw_ostream &OS, uint32_t *Offset, uint32_t *Size);

  /// Utility function to emit the encoding to a streamer.
  static void Emit(MCStreamer *MCOS, MCDwarfLineTableParams Params,
                   int64_t LineDelta, uint64_t AddrDelta);
};

class MCGenDwarfInfo {
public:
  //
  // When generating dwarf for assembly source files this emits the Dwarf
  // sections.
  //
  static void Emit(MCStreamer *MCOS);
};

// When generating dwarf for assembly source files this is the info that is
// needed to be gathered for each symbol that will have a dwarf label.
class MCGenDwarfLabelEntry {
private:
  // Name of the symbol without a leading underbar, if any.
  StringRef Name;
  // The dwarf file number this symbol is in.
  unsigned FileNumber;
  // The line number this symbol is at.
  unsigned LineNumber;
  // The low_pc for the dwarf label is taken from this symbol.
  MCSymbol *Label;

public:
  MCGenDwarfLabelEntry(StringRef name, unsigned fileNumber, unsigned lineNumber,
                       MCSymbol *label)
      : Name(name), FileNumber(fileNumber), LineNumber(lineNumber),
        Label(label) {}

  StringRef getName() const { return Name; }
  unsigned getFileNumber() const { return FileNumber; }
  unsigned getLineNumber() const { return LineNumber; }
  MCSymbol *getLabel() const { return Label; }

  // This is called when label is created when we are generating dwarf for
  // assembly source files.
  static void Make(MCSymbol *Symbol, MCStreamer *MCOS, SourceMgr &SrcMgr,
                   SMLoc &Loc);
};

class MCCFIInstruction {
public:
  enum OpType {
    OpSameValue,
    OpRememberState,
    OpRestoreState,
    OpOffset,
    OpDefCfaRegister,
    OpDefCfaOffset,
    OpDefCfa,
    OpRelOffset,
    OpAdjustCfaOffset,
    OpEscape,
    OpRestore,
    OpUndefined,
    OpRegister,
    OpWindowSave,
    OpNegateRAState,
    OpGnuArgsSize
  };

private:
  OpType Operation;
  MCSymbol *Label;
  unsigned Register;
  union {
    int Offset;
    unsigned Register2;
  };
  std::vector<char> Values;

  MCCFIInstruction(OpType Op, MCSymbol *L, unsigned R, int O, StringRef V)
      : Operation(Op), Label(L), Register(R), Offset(O),
        Values(V.begin(), V.end()) {
    assert(Op != OpRegister);
  }

  MCCFIInstruction(OpType Op, MCSymbol *L, unsigned R1, unsigned R2)
      : Operation(Op), Label(L), Register(R1), Register2(R2) {
    assert(Op == OpRegister);
  }

public:
  /// .cfi_def_cfa defines a rule for computing CFA as: take address from
  /// Register and add Offset to it.
  static MCCFIInstruction createDefCfa(MCSymbol *L, unsigned Register,
                                       int Offset) {
    return MCCFIInstruction(OpDefCfa, L, Register, -Offset, "");
  }

  /// .cfi_def_cfa_register modifies a rule for computing CFA. From now
  /// on Register will be used instead of the old one. Offset remains the same.
  static MCCFIInstruction createDefCfaRegister(MCSymbol *L, unsigned Register) {
    return MCCFIInstruction(OpDefCfaRegister, L, Register, 0, "");
  }

  /// .cfi_def_cfa_offset modifies a rule for computing CFA. Register
  /// remains the same, but offset is new. Note that it is the absolute offset
  /// that will be added to a defined register to the compute CFA address.
  static MCCFIInstruction createDefCfaOffset(MCSymbol *L, int Offset) {
    return MCCFIInstruction(OpDefCfaOffset, L, 0, -Offset, "");
  }

  /// .cfi_adjust_cfa_offset Same as .cfi_def_cfa_offset, but
  /// Offset is a relative value that is added/subtracted from the previous
  /// offset.
  static MCCFIInstruction createAdjustCfaOffset(MCSymbol *L, int Adjustment) {
    return MCCFIInstruction(OpAdjustCfaOffset, L, 0, Adjustment, "");
  }

  /// .cfi_offset Previous value of Register is saved at offset Offset
  /// from CFA.
  static MCCFIInstruction createOffset(MCSymbol *L, unsigned Register,
                                       int Offset) {
    return MCCFIInstruction(OpOffset, L, Register, Offset, "");
  }

  /// .cfi_rel_offset Previous value of Register is saved at offset
  /// Offset from the current CFA register. This is transformed to .cfi_offset
  /// using the known displacement of the CFA register from the CFA.
  static MCCFIInstruction createRelOffset(MCSymbol *L, unsigned Register,
                                          int Offset) {
    return MCCFIInstruction(OpRelOffset, L, Register, Offset, "");
  }

  /// .cfi_register Previous value of Register1 is saved in
  /// register Register2.
  static MCCFIInstruction createRegister(MCSymbol *L, unsigned Register1,
                                         unsigned Register2) {
    return MCCFIInstruction(OpRegister, L, Register1, Register2);
  }

  /// .cfi_window_save SPARC register window is saved.
  static MCCFIInstruction createWindowSave(MCSymbol *L) {
    return MCCFIInstruction(OpWindowSave, L, 0, 0, "");
  }

  /// .cfi_negate_ra_state AArch64 negate RA state.
  static MCCFIInstruction createNegateRAState(MCSymbol *L) {
    return MCCFIInstruction(OpNegateRAState, L, 0, 0, "");
  }

  /// .cfi_restore says that the rule for Register is now the same as it
  /// was at the beginning of the function, after all initial instructions added
  /// by .cfi_startproc were executed.
  static MCCFIInstruction createRestore(MCSymbol *L, unsigned Register) {
    return MCCFIInstruction(OpRestore, L, Register, 0, "");
  }

  /// .cfi_undefined From now on the previous value of Register can't be
  /// restored anymore.
  static MCCFIInstruction createUndefined(MCSymbol *L, unsigned Register) {
    return MCCFIInstruction(OpUndefined, L, Register, 0, "");
  }

  /// .cfi_same_value Current value of Register is the same as in the
  /// previous frame. I.e., no restoration is needed.
  static MCCFIInstruction createSameValue(MCSymbol *L, unsigned Register) {
    return MCCFIInstruction(OpSameValue, L, Register, 0, "");
  }

  /// .cfi_remember_state Save all current rules for all registers.
  static MCCFIInstruction createRememberState(MCSymbol *L) {
    return MCCFIInstruction(OpRememberState, L, 0, 0, "");
  }

  /// .cfi_restore_state Restore the previously saved state.
  static MCCFIInstruction createRestoreState(MCSymbol *L) {
    return MCCFIInstruction(OpRestoreState, L, 0, 0, "");
  }

  /// .cfi_escape Allows the user to add arbitrary bytes to the unwind
  /// info.
  static MCCFIInstruction createEscape(MCSymbol *L, StringRef Vals) {
    return MCCFIInstruction(OpEscape, L, 0, 0, Vals);
  }

  /// A special wrapper for .cfi_escape that indicates GNU_ARGS_SIZE
  static MCCFIInstruction createGnuArgsSize(MCSymbol *L, int Size) {
    return MCCFIInstruction(OpGnuArgsSize, L, 0, Size, "");
  }

  OpType getOperation() const { return Operation; }
  MCSymbol *getLabel() const { return Label; }

  unsigned getRegister() const {
    assert(Operation == OpDefCfa || Operation == OpOffset ||
           Operation == OpRestore || Operation == OpUndefined ||
           Operation == OpSameValue || Operation == OpDefCfaRegister ||
           Operation == OpRelOffset || Operation == OpRegister);
    return Register;
  }

  unsigned getRegister2() const {
    assert(Operation == OpRegister);
    return Register2;
  }

  int getOffset() const {
    assert(Operation == OpDefCfa || Operation == OpOffset ||
           Operation == OpRelOffset || Operation == OpDefCfaOffset ||
           Operation == OpAdjustCfaOffset || Operation == OpGnuArgsSize);
    return Offset;
  }

  StringRef getValues() const {
    assert(Operation == OpEscape);
    return StringRef(&Values[0], Values.size());
  }
};

struct MCDwarfFrameInfo {
  MCDwarfFrameInfo() = default;

  MCSymbol *Begin = nullptr;
  MCSymbol *End = nullptr;
  const MCSymbol *Personality = nullptr;
  const MCSymbol *Lsda = nullptr;
  std::vector<MCCFIInstruction> Instructions;
  unsigned CurrentCfaRegister = 0;
  unsigned PersonalityEncoding = 0;
  unsigned LsdaEncoding = 0;
  uint32_t CompactUnwindEncoding = 0;
  bool IsSignalFrame = false;
  bool IsSimple = false;
  unsigned RAReg = static_cast<unsigned>(INT_MAX);
  bool IsBKeyFrame = false;
};

class MCDwarfFrameEmitter {
public:
  //
  // This emits the frame info section.
  //
  static void Emit(MCObjectStreamer &streamer, MCAsmBackend *MAB, bool isEH);
  static void EmitAdvanceLoc(MCObjectStreamer &Streamer, uint64_t AddrDelta);
  static void EncodeAdvanceLoc(MCContext &Context, uint64_t AddrDelta,
                               raw_ostream &OS);
};

} // end namespace llvm

#endif // LLVM_MC_MCDWARF_H
