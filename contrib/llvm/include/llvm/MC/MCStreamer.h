//===- MCStreamer.h - High-level Streaming Machine Code Output --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCStreamer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSTREAMER_H
#define LLVM_MC_MCSTREAMER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/VersionTuple.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class AssemblerConstantPools;
class formatted_raw_ostream;
class MCAsmBackend;
class MCCodeEmitter;
struct MCCodePaddingContext;
class MCContext;
class MCExpr;
class MCInst;
class MCInstPrinter;
class MCSection;
class MCStreamer;
class MCSymbolRefExpr;
class MCSubtargetInfo;
class raw_ostream;
class Twine;

using MCSectionSubPair = std::pair<MCSection *, const MCExpr *>;

/// Target specific streamer interface. This is used so that targets can
/// implement support for target specific assembly directives.
///
/// If target foo wants to use this, it should implement 3 classes:
/// * FooTargetStreamer : public MCTargetStreamer
/// * FooTargetAsmStreamer : public FooTargetStreamer
/// * FooTargetELFStreamer : public FooTargetStreamer
///
/// FooTargetStreamer should have a pure virtual method for each directive. For
/// example, for a ".bar symbol_name" directive, it should have
/// virtual emitBar(const MCSymbol &Symbol) = 0;
///
/// The FooTargetAsmStreamer and FooTargetELFStreamer classes implement the
/// method. The assembly streamer just prints ".bar symbol_name". The object
/// streamer does whatever is needed to implement .bar in the object file.
///
/// In the assembly printer and parser the target streamer can be used by
/// calling getTargetStreamer and casting it to FooTargetStreamer:
///
/// MCTargetStreamer &TS = OutStreamer.getTargetStreamer();
/// FooTargetStreamer &ATS = static_cast<FooTargetStreamer &>(TS);
///
/// The base classes FooTargetAsmStreamer and FooTargetELFStreamer should
/// *never* be treated differently. Callers should always talk to a
/// FooTargetStreamer.
class MCTargetStreamer {
protected:
  MCStreamer &Streamer;

public:
  MCTargetStreamer(MCStreamer &S);
  virtual ~MCTargetStreamer();

  MCStreamer &getStreamer() { return Streamer; }

  // Allow a target to add behavior to the EmitLabel of MCStreamer.
  virtual void emitLabel(MCSymbol *Symbol);
  // Allow a target to add behavior to the emitAssignment of MCStreamer.
  virtual void emitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  virtual void prettyPrintAsm(MCInstPrinter &InstPrinter, raw_ostream &OS,
                              const MCInst &Inst, const MCSubtargetInfo &STI);

  virtual void emitDwarfFileDirective(StringRef Directive);

  /// Update streamer for a new active section.
  ///
  /// This is called by PopSection and SwitchSection, if the current
  /// section changes.
  virtual void changeSection(const MCSection *CurSection, MCSection *Section,
                             const MCExpr *SubSection, raw_ostream &OS);

  virtual void emitValue(const MCExpr *Value);

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to emit bytes in \p Data as sequence of .byte directives.
  virtual void emitRawBytes(StringRef Data);

  virtual void finish();
};

// FIXME: declared here because it is used from
// lib/CodeGen/AsmPrinter/ARMException.cpp.
class ARMTargetStreamer : public MCTargetStreamer {
public:
  ARMTargetStreamer(MCStreamer &S);
  ~ARMTargetStreamer() override;

  virtual void emitFnStart();
  virtual void emitFnEnd();
  virtual void emitCantUnwind();
  virtual void emitPersonality(const MCSymbol *Personality);
  virtual void emitPersonalityIndex(unsigned Index);
  virtual void emitHandlerData();
  virtual void emitSetFP(unsigned FpReg, unsigned SpReg,
                         int64_t Offset = 0);
  virtual void emitMovSP(unsigned Reg, int64_t Offset = 0);
  virtual void emitPad(int64_t Offset);
  virtual void emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                           bool isVector);
  virtual void emitUnwindRaw(int64_t StackOffset,
                             const SmallVectorImpl<uint8_t> &Opcodes);

  virtual void switchVendor(StringRef Vendor);
  virtual void emitAttribute(unsigned Attribute, unsigned Value);
  virtual void emitTextAttribute(unsigned Attribute, StringRef String);
  virtual void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                                    StringRef StringValue = "");
  virtual void emitFPU(unsigned FPU);
  virtual void emitArch(ARM::ArchKind Arch);
  virtual void emitArchExtension(unsigned ArchExt);
  virtual void emitObjectArch(ARM::ArchKind Arch);
  void emitTargetAttributes(const MCSubtargetInfo &STI);
  virtual void finishAttributeSection();
  virtual void emitInst(uint32_t Inst, char Suffix = '\0');

  virtual void AnnotateTLSDescriptorSequence(const MCSymbolRefExpr *SRE);

  virtual void emitThumbSet(MCSymbol *Symbol, const MCExpr *Value);

  void finish() override;

  /// Reset any state between object emissions, i.e. the equivalent of
  /// MCStreamer's reset method.
  virtual void reset();

  /// Callback used to implement the ldr= pseudo.
  /// Add a new entry to the constant pool for the current section and return an
  /// MCExpr that can be used to refer to the constant pool location.
  const MCExpr *addConstantPoolEntry(const MCExpr *, SMLoc Loc);

  /// Callback used to implemnt the .ltorg directive.
  /// Emit contents of constant pool for the current section.
  void emitCurrentConstantPool();

private:
  std::unique_ptr<AssemblerConstantPools> ConstantPools;
};

/// Streaming machine code generation interface.
///
/// This interface is intended to provide a programatic interface that is very
/// similar to the level that an assembler .s file provides.  It has callbacks
/// to emit bytes, handle directives, etc.  The implementation of this interface
/// retains state to know what the current section is etc.
///
/// There are multiple implementations of this interface: one for writing out
/// a .s file, and implementations that write out .o files of various formats.
///
class MCStreamer {
  MCContext &Context;
  std::unique_ptr<MCTargetStreamer> TargetStreamer;

  std::vector<MCDwarfFrameInfo> DwarfFrameInfos;
  MCDwarfFrameInfo *getCurrentDwarfFrameInfo();

  /// Similar to DwarfFrameInfos, but for SEH unwind info. Chained frames may
  /// refer to each other, so use std::unique_ptr to provide pointer stability.
  std::vector<std::unique_ptr<WinEH::FrameInfo>> WinFrameInfos;

  WinEH::FrameInfo *CurrentWinFrameInfo;

  /// Tracks an index to represent the order a symbol was emitted in.
  /// Zero means we did not emit that symbol.
  DenseMap<const MCSymbol *, unsigned> SymbolOrdering;

  /// This is stack of current and previous section values saved by
  /// PushSection.
  SmallVector<std::pair<MCSectionSubPair, MCSectionSubPair>, 4> SectionStack;

  /// The next unique ID to use when creating a WinCFI-related section (.pdata
  /// or .xdata). This ID ensures that we have a one-to-one mapping from
  /// code section to unwind info section, which MSVC's incremental linker
  /// requires.
  unsigned NextWinCFIID = 0;

  bool UseAssemblerInfoForParsing;

protected:
  MCStreamer(MCContext &Ctx);

  virtual void EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame);
  virtual void EmitCFIEndProcImpl(MCDwarfFrameInfo &CurFrame);

  WinEH::FrameInfo *getCurrentWinFrameInfo() {
    return CurrentWinFrameInfo;
  }

  virtual void EmitWindowsUnwindTables();

  virtual void EmitRawTextImpl(StringRef String);

  /// Returns true if the the .cv_loc directive is in the right section.
  bool checkCVLocSection(unsigned FuncId, unsigned FileNo, SMLoc Loc);

public:
  MCStreamer(const MCStreamer &) = delete;
  MCStreamer &operator=(const MCStreamer &) = delete;
  virtual ~MCStreamer();

  void visitUsedExpr(const MCExpr &Expr);
  virtual void visitUsedSymbol(const MCSymbol &Sym);

  void setTargetStreamer(MCTargetStreamer *TS) {
    TargetStreamer.reset(TS);
  }

  /// State management
  ///
  virtual void reset();

  MCContext &getContext() const { return Context; }

  virtual MCAssembler *getAssemblerPtr() { return nullptr; }

  void setUseAssemblerInfoForParsing(bool v) { UseAssemblerInfoForParsing = v; }
  bool getUseAssemblerInfoForParsing() { return UseAssemblerInfoForParsing; }

  MCTargetStreamer *getTargetStreamer() {
    return TargetStreamer.get();
  }

  /// When emitting an object file, create and emit a real label. When emitting
  /// textual assembly, this should do nothing to avoid polluting our output.
  virtual MCSymbol *EmitCFILabel();

  /// Retreive the current frame info if one is available and it is not yet
  /// closed. Otherwise, issue an error and return null.
  WinEH::FrameInfo *EnsureValidWinFrameInfo(SMLoc Loc);

  unsigned getNumFrameInfos() { return DwarfFrameInfos.size(); }
  ArrayRef<MCDwarfFrameInfo> getDwarfFrameInfos() const {
    return DwarfFrameInfos;
  }

  bool hasUnfinishedDwarfFrameInfo();

  unsigned getNumWinFrameInfos() { return WinFrameInfos.size(); }
  ArrayRef<std::unique_ptr<WinEH::FrameInfo>> getWinFrameInfos() const {
    return WinFrameInfos;
  }

  void generateCompactUnwindEncodings(MCAsmBackend *MAB);

  /// \name Assembly File Formatting.
  /// @{

  /// Return true if this streamer supports verbose assembly and if it is
  /// enabled.
  virtual bool isVerboseAsm() const { return false; }

  /// Return true if this asm streamer supports emitting unformatted text
  /// to the .s file with EmitRawText.
  virtual bool hasRawTextSupport() const { return false; }

  /// Is the integrated assembler required for this streamer to function
  /// correctly?
  virtual bool isIntegratedAssemblerRequired() const { return false; }

  /// Add a textual comment.
  ///
  /// Typically for comments that can be emitted to the generated .s
  /// file if applicable as a QoI issue to make the output of the compiler
  /// more readable.  This only affects the MCAsmStreamer, and only when
  /// verbose assembly output is enabled.
  ///
  /// If the comment includes embedded \n's, they will each get the comment
  /// prefix as appropriate.  The added comment should not end with a \n.
  /// By default, each comment is terminated with an end of line, i.e. the
  /// EOL param is set to true by default. If one prefers not to end the
  /// comment with a new line then the EOL param should be passed
  /// with a false value.
  virtual void AddComment(const Twine &T, bool EOL = true) {}

  /// Return a raw_ostream that comments can be written to. Unlike
  /// AddComment, you are required to terminate comments with \n if you use this
  /// method.
  virtual raw_ostream &GetCommentOS();

  /// Print T and prefix it with the comment string (normally #) and
  /// optionally a tab. This prints the comment immediately, not at the end of
  /// the current line. It is basically a safe version of EmitRawText: since it
  /// only prints comments, the object streamer ignores it instead of asserting.
  virtual void emitRawComment(const Twine &T, bool TabPrefix = true);

  /// Add explicit comment T. T is required to be a valid
  /// comment in the output and does not need to be escaped.
  virtual void addExplicitComment(const Twine &T);

  /// Emit added explicit comments.
  virtual void emitExplicitComments();

  /// AddBlankLine - Emit a blank line to a .s file to pretty it up.
  virtual void AddBlankLine() {}

  /// @}

  /// \name Symbol & Section Management
  /// @{

  /// Return the current section that the streamer is emitting code to.
  MCSectionSubPair getCurrentSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().first;
    return MCSectionSubPair();
  }
  MCSection *getCurrentSectionOnly() const { return getCurrentSection().first; }

  /// Return the previous section that the streamer is emitting code to.
  MCSectionSubPair getPreviousSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().second;
    return MCSectionSubPair();
  }

  /// Returns an index to represent the order a symbol was emitted in.
  /// (zero if we did not emit that symbol)
  unsigned GetSymbolOrder(const MCSymbol *Sym) const {
    return SymbolOrdering.lookup(Sym);
  }

  /// Update streamer for a new active section.
  ///
  /// This is called by PopSection and SwitchSection, if the current
  /// section changes.
  virtual void ChangeSection(MCSection *, const MCExpr *);

  /// Save the current and previous section on the section stack.
  void PushSection() {
    SectionStack.push_back(
        std::make_pair(getCurrentSection(), getPreviousSection()));
  }

  /// Restore the current and previous section from the section stack.
  /// Calls ChangeSection as needed.
  ///
  /// Returns false if the stack was empty.
  bool PopSection() {
    if (SectionStack.size() <= 1)
      return false;
    auto I = SectionStack.end();
    --I;
    MCSectionSubPair OldSection = I->first;
    --I;
    MCSectionSubPair NewSection = I->first;

    if (OldSection != NewSection)
      ChangeSection(NewSection.first, NewSection.second);
    SectionStack.pop_back();
    return true;
  }

  bool SubSection(const MCExpr *Subsection) {
    if (SectionStack.empty())
      return false;

    SwitchSection(SectionStack.back().first.first, Subsection);
    return true;
  }

  /// Set the current section where code is being emitted to \p Section.  This
  /// is required to update CurSection.
  ///
  /// This corresponds to assembler directives like .section, .text, etc.
  virtual void SwitchSection(MCSection *Section,
                             const MCExpr *Subsection = nullptr);

  /// Set the current section where code is being emitted to \p Section.
  /// This is required to update CurSection. This version does not call
  /// ChangeSection.
  void SwitchSectionNoChange(MCSection *Section,
                             const MCExpr *Subsection = nullptr) {
    assert(Section && "Cannot switch to a null section!");
    MCSectionSubPair curSection = SectionStack.back().first;
    SectionStack.back().second = curSection;
    if (MCSectionSubPair(Section, Subsection) != curSection)
      SectionStack.back().first = MCSectionSubPair(Section, Subsection);
  }

  /// Create the default sections and set the initial one.
  virtual void InitSections(bool NoExecStack);

  MCSymbol *endSection(MCSection *Section);

  /// Sets the symbol's section.
  ///
  /// Each emitted symbol will be tracked in the ordering table,
  /// so we can sort on them later.
  void AssignFragment(MCSymbol *Symbol, MCFragment *Fragment);

  /// Emit a label for \p Symbol into the current section.
  ///
  /// This corresponds to an assembler statement such as:
  ///   foo:
  ///
  /// \param Symbol - The symbol to emit. A given symbol should only be
  /// emitted as a label once, and symbols emitted as a label should never be
  /// used in an assignment.
  // FIXME: These emission are non-const because we mutate the symbol to
  // add the section we're emitting it to later.
  virtual void EmitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc());

  virtual void EmitEHSymAttributes(const MCSymbol *Symbol, MCSymbol *EHSymbol);

  /// Note in the output the specified \p Flag.
  virtual void EmitAssemblerFlag(MCAssemblerFlag Flag);

  /// Emit the given list \p Options of strings as linker
  /// options into the output.
  virtual void EmitLinkerOptions(ArrayRef<std::string> Kind) {}

  /// Note in the output the specified region \p Kind.
  virtual void EmitDataRegion(MCDataRegionType Kind) {}

  /// Specify the Mach-O minimum deployment target version.
  virtual void EmitVersionMin(MCVersionMinType Type, unsigned Major,
                              unsigned Minor, unsigned Update,
                              VersionTuple SDKVersion) {}

  /// Emit/Specify Mach-O build version command.
  /// \p Platform should be one of MachO::PlatformType.
  virtual void EmitBuildVersion(unsigned Platform, unsigned Major,
                                unsigned Minor, unsigned Update,
                                VersionTuple SDKVersion) {}

  void EmitVersionForTarget(const Triple &Target,
                            const VersionTuple &SDKVersion);

  /// Note in the output that the specified \p Func is a Thumb mode
  /// function (ARM target only).
  virtual void EmitThumbFunc(MCSymbol *Func);

  /// Emit an assignment of \p Value to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  symbol = value
  ///
  /// The assignment generates no code, but has the side effect of binding the
  /// value in the current context. For the assembly streamer, this prints the
  /// binding into the .s file.
  ///
  /// \param Symbol - The symbol being assigned to.
  /// \param Value - The value for the symbol.
  virtual void EmitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an weak reference from \p Alias to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .weakref alias, symbol
  ///
  /// \param Alias - The alias that is being created.
  /// \param Symbol - The symbol being aliased.
  virtual void EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol);

  /// Add the given \p Attribute to \p Symbol.
  virtual bool EmitSymbolAttribute(MCSymbol *Symbol,
                                   MCSymbolAttr Attribute) = 0;

  /// Set the \p DescValue for the \p Symbol.
  ///
  /// \param Symbol - The symbol to have its n_desc field set.
  /// \param DescValue - The value to set into the n_desc field.
  virtual void EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue);

  /// Start emitting COFF symbol definition
  ///
  /// \param Symbol - The symbol to have its External & Type fields set.
  virtual void BeginCOFFSymbolDef(const MCSymbol *Symbol);

  /// Emit the storage class of the symbol.
  ///
  /// \param StorageClass - The storage class the symbol should have.
  virtual void EmitCOFFSymbolStorageClass(int StorageClass);

  /// Emit the type of the symbol.
  ///
  /// \param Type - A COFF type identifier (see COFF::SymbolType in X86COFF.h)
  virtual void EmitCOFFSymbolType(int Type);

  /// Marks the end of the symbol definition.
  virtual void EndCOFFSymbolDef();

  virtual void EmitCOFFSafeSEH(MCSymbol const *Symbol);

  /// Emits the symbol table index of a Symbol into the current section.
  virtual void EmitCOFFSymbolIndex(MCSymbol const *Symbol);

  /// Emits a COFF section index.
  ///
  /// \param Symbol - Symbol the section number relocation should point to.
  virtual void EmitCOFFSectionIndex(MCSymbol const *Symbol);

  /// Emits a COFF section relative relocation.
  ///
  /// \param Symbol - Symbol the section relative relocation should point to.
  virtual void EmitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset);

  /// Emits a COFF image relative relocation.
  ///
  /// \param Symbol - Symbol the image relative relocation should point to.
  virtual void EmitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset);

  /// Emit an ELF .size directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .size symbol, expression
  virtual void emitELFSize(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an ELF .symver directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .symver _start, foo@@SOME_VERSION
  /// \param AliasName - The versioned alias (i.e. "foo@@SOME_VERSION")
  /// \param Aliasee - The aliased symbol (i.e. "_start")
  virtual void emitELFSymverDirective(StringRef AliasName,
                                      const MCSymbol *Aliasee);

  /// Emit a Linker Optimization Hint (LOH) directive.
  /// \param Args - Arguments of the LOH.
  virtual void EmitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) {}

  /// Emit a common symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the symbol if
  /// non-zero. This must be a power of 2.
  virtual void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                unsigned ByteAlignment) = 0;

  /// Emit a local common (.lcomm) symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the common symbol in bytes.
  virtual void EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     unsigned ByteAlignment);

  /// Emit the zerofill section and an optional symbol.
  ///
  /// \param Section - The zerofill section to create and or to put the symbol
  /// \param Symbol - The zerofill symbol to emit, if non-NULL.
  /// \param Size - The size of the zerofill symbol.
  /// \param ByteAlignment - The alignment of the zerofill symbol if
  /// non-zero. This must be a power of 2 on some targets.
  virtual void EmitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                            uint64_t Size = 0, unsigned ByteAlignment = 0,
                            SMLoc Loc = SMLoc()) = 0;

  /// Emit a thread local bss (.tbss) symbol.
  ///
  /// \param Section - The thread local common section.
  /// \param Symbol - The thread local common symbol to emit.
  /// \param Size - The size of the symbol.
  /// \param ByteAlignment - The alignment of the thread local common symbol
  /// if non-zero.  This must be a power of 2 on some targets.
  virtual void EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                              uint64_t Size, unsigned ByteAlignment = 0);

  /// @}
  /// \name Generating Data
  /// @{

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to implement assembler directives such as .byte, .ascii,
  /// etc.
  virtual void EmitBytes(StringRef Data);

  /// Functionally identical to EmitBytes. When emitting textual assembly, this
  /// method uses .byte directives instead of .ascii or .asciz for readability.
  virtual void EmitBinaryData(StringRef Data);

  /// Emit the expression \p Value into the output as a native
  /// integer of the given \p Size bytes.
  ///
  /// This is used to implement assembler directives such as .word, .quad,
  /// etc.
  ///
  /// \param Value - The value to emit.
  /// \param Size - The size of the integer (in bytes) to emit. This must
  /// match a native machine width.
  /// \param Loc - The location of the expression for error reporting.
  virtual void EmitValueImpl(const MCExpr *Value, unsigned Size,
                             SMLoc Loc = SMLoc());

  void EmitValue(const MCExpr *Value, unsigned Size, SMLoc Loc = SMLoc());

  /// Special case of EmitValue that avoids the client having
  /// to pass in a MCExpr for constant integers.
  virtual void EmitIntValue(uint64_t Value, unsigned Size);

  virtual void EmitULEB128Value(const MCExpr *Value);

  virtual void EmitSLEB128Value(const MCExpr *Value);

  /// Special case of EmitULEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  void EmitULEB128IntValue(uint64_t Value);

  /// Special case of EmitSLEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  void EmitSLEB128IntValue(int64_t Value);

  /// Special case of EmitValue that avoids the client having to pass in
  /// a MCExpr for MCSymbols.
  void EmitSymbolValue(const MCSymbol *Sym, unsigned Size,
                       bool IsSectionRelative = false);

  /// Emit the expression \p Value into the output as a dtprel
  /// (64-bit DTP relative) value.
  ///
  /// This is used to implement assembler directives such as .dtpreldword on
  /// targets that support them.
  virtual void EmitDTPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a dtprel
  /// (32-bit DTP relative) value.
  ///
  /// This is used to implement assembler directives such as .dtprelword on
  /// targets that support them.
  virtual void EmitDTPRel32Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a tprel
  /// (64-bit TP relative) value.
  ///
  /// This is used to implement assembler directives such as .tpreldword on
  /// targets that support them.
  virtual void EmitTPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a tprel
  /// (32-bit TP relative) value.
  ///
  /// This is used to implement assembler directives such as .tprelword on
  /// targets that support them.
  virtual void EmitTPRel32Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a gprel64 (64-bit
  /// GP relative) value.
  ///
  /// This is used to implement assembler directives such as .gpdword on
  /// targets that support them.
  virtual void EmitGPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a gprel32 (32-bit
  /// GP relative) value.
  ///
  /// This is used to implement assembler directives such as .gprel32 on
  /// targets that support them.
  virtual void EmitGPRel32Value(const MCExpr *Value);

  /// Emit NumBytes bytes worth of the value specified by FillValue.
  /// This implements directives such as '.space'.
  void emitFill(uint64_t NumBytes, uint8_t FillValue);

  /// Emit \p Size bytes worth of the value specified by \p FillValue.
  ///
  /// This is used to implement assembler directives such as .space or .skip.
  ///
  /// \param NumBytes - The number of bytes to emit.
  /// \param FillValue - The value to use when filling bytes.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                        SMLoc Loc = SMLoc());

  /// Emit \p NumValues copies of \p Size bytes. Each \p Size bytes is
  /// taken from the lowest order 4 bytes of \p Expr expression.
  ///
  /// This is used to implement assembler directives such as .fill.
  ///
  /// \param NumValues - The number of copies of \p Size bytes to emit.
  /// \param Size - The size (in bytes) of each repeated value.
  /// \param Expr - The expression from which \p Size bytes are used.
  virtual void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                        SMLoc Loc = SMLoc());

  /// Emit NumBytes worth of zeros.
  /// This function properly handles data in virtual sections.
  void EmitZeros(uint64_t NumBytes);

  /// Emit some number of copies of \p Value until the byte alignment \p
  /// ByteAlignment is reached.
  ///
  /// If the number of bytes need to emit for the alignment is not a multiple
  /// of \p ValueSize, then the contents of the emitted fill bytes is
  /// undefined.
  ///
  /// This used to implement the .align assembler directive.
  ///
  /// \param ByteAlignment - The alignment to reach. This must be a power of
  /// two on some targets.
  /// \param Value - The value to use when filling bytes.
  /// \param ValueSize - The size of the integer (in bytes) to emit for
  /// \p Value. This must match a native machine width.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void EmitValueToAlignment(unsigned ByteAlignment, int64_t Value = 0,
                                    unsigned ValueSize = 1,
                                    unsigned MaxBytesToEmit = 0);

  /// Emit nops until the byte alignment \p ByteAlignment is reached.
  ///
  /// This used to align code where the alignment bytes may be executed.  This
  /// can emit different bytes for different sizes to optimize execution.
  ///
  /// \param ByteAlignment - The alignment to reach. This must be a power of
  /// two on some targets.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void EmitCodeAlignment(unsigned ByteAlignment,
                                 unsigned MaxBytesToEmit = 0);

  /// Emit some number of copies of \p Value until the byte offset \p
  /// Offset is reached.
  ///
  /// This is used to implement assembler directives such as .org.
  ///
  /// \param Offset - The offset to reach. This may be an expression, but the
  /// expression must be associated with the current section.
  /// \param Value - The value to use when filling bytes.
  virtual void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                                 SMLoc Loc);

  virtual void
  EmitCodePaddingBasicBlockStart(const MCCodePaddingContext &Context) {}

  virtual void
  EmitCodePaddingBasicBlockEnd(const MCCodePaddingContext &Context) {}

  /// @}

  /// Switch to a new logical file.  This is used to implement the '.file
  /// "foo.c"' assembler directive.
  virtual void EmitFileDirective(StringRef Filename);

  /// Emit the "identifiers" directive.  This implements the
  /// '.ident "version foo"' assembler directive.
  virtual void EmitIdent(StringRef IdentString) {}

  /// Associate a filename with a specified logical file number.  This
  /// implements the DWARF2 '.file 4 "foo.c"' assembler directive.
  unsigned EmitDwarfFileDirective(unsigned FileNo, StringRef Directory,
                                  StringRef Filename,
                                  MD5::MD5Result *Checksum = nullptr,
                                  Optional<StringRef> Source = None,
                                  unsigned CUID = 0) {
    return cantFail(
        tryEmitDwarfFileDirective(FileNo, Directory, Filename, Checksum,
                                  Source, CUID));
  }

  /// Associate a filename with a specified logical file number.
  /// Also associate a directory, optional checksum, and optional source
  /// text with the logical file.  This implements the DWARF2
  /// '.file 4 "dir/foo.c"' assembler directive, and the DWARF5
  /// '.file 4 "dir/foo.c" md5 "..." source "..."' assembler directive.
  virtual Expected<unsigned> tryEmitDwarfFileDirective(
      unsigned FileNo, StringRef Directory, StringRef Filename,
      MD5::MD5Result *Checksum = nullptr, Optional<StringRef> Source = None,
      unsigned CUID = 0);

  /// Specify the "root" file of the compilation, using the ".file 0" extension.
  virtual void emitDwarfFile0Directive(StringRef Directory, StringRef Filename,
                                       MD5::MD5Result *Checksum,
                                       Optional<StringRef> Source,
                                       unsigned CUID = 0);

  virtual void EmitCFIBKeyFrame();

  /// This implements the DWARF2 '.loc fileno lineno ...' assembler
  /// directive.
  virtual void EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                     unsigned Column, unsigned Flags,
                                     unsigned Isa, unsigned Discriminator,
                                     StringRef FileName);

  /// Associate a filename with a specified logical file number, and also
  /// specify that file's checksum information.  This implements the '.cv_file 4
  /// "foo.c"' assembler directive. Returns true on success.
  virtual bool EmitCVFileDirective(unsigned FileNo, StringRef Filename,
                                   ArrayRef<uint8_t> Checksum,
                                   unsigned ChecksumKind);

  /// Introduces a function id for use with .cv_loc.
  virtual bool EmitCVFuncIdDirective(unsigned FunctionId);

  /// Introduces an inline call site id for use with .cv_loc. Includes
  /// extra information for inline line table generation.
  virtual bool EmitCVInlineSiteIdDirective(unsigned FunctionId, unsigned IAFunc,
                                           unsigned IAFile, unsigned IALine,
                                           unsigned IACol, SMLoc Loc);

  /// This implements the CodeView '.cv_loc' assembler directive.
  virtual void EmitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                  unsigned Line, unsigned Column,
                                  bool PrologueEnd, bool IsStmt,
                                  StringRef FileName, SMLoc Loc);

  /// This implements the CodeView '.cv_linetable' assembler directive.
  virtual void EmitCVLinetableDirective(unsigned FunctionId,
                                        const MCSymbol *FnStart,
                                        const MCSymbol *FnEnd);

  /// This implements the CodeView '.cv_inline_linetable' assembler
  /// directive.
  virtual void EmitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                              unsigned SourceFileId,
                                              unsigned SourceLineNum,
                                              const MCSymbol *FnStartSym,
                                              const MCSymbol *FnEndSym);

  /// This implements the CodeView '.cv_def_range' assembler
  /// directive.
  virtual void EmitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion);

  /// This implements the CodeView '.cv_stringtable' assembler directive.
  virtual void EmitCVStringTableDirective() {}

  /// This implements the CodeView '.cv_filechecksums' assembler directive.
  virtual void EmitCVFileChecksumsDirective() {}

  /// This implements the CodeView '.cv_filechecksumoffset' assembler
  /// directive.
  virtual void EmitCVFileChecksumOffsetDirective(unsigned FileNo) {}

  /// This implements the CodeView '.cv_fpo_data' assembler directive.
  virtual void EmitCVFPOData(const MCSymbol *ProcSym, SMLoc Loc = {}) {}

  /// Emit the absolute difference between two symbols.
  ///
  /// \pre Offset of \c Hi is greater than the offset \c Lo.
  virtual void emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                                      unsigned Size);

  /// Emit the absolute difference between two symbols encoded with ULEB128.
  virtual void emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                               const MCSymbol *Lo);

  virtual MCSymbol *getDwarfLineTableSymbol(unsigned CUID);
  virtual void EmitCFISections(bool EH, bool Debug);
  void EmitCFIStartProc(bool IsSimple, SMLoc Loc = SMLoc());
  void EmitCFIEndProc();
  virtual void EmitCFIDefCfa(int64_t Register, int64_t Offset);
  virtual void EmitCFIDefCfaOffset(int64_t Offset);
  virtual void EmitCFIDefCfaRegister(int64_t Register);
  virtual void EmitCFIOffset(int64_t Register, int64_t Offset);
  virtual void EmitCFIPersonality(const MCSymbol *Sym, unsigned Encoding);
  virtual void EmitCFILsda(const MCSymbol *Sym, unsigned Encoding);
  virtual void EmitCFIRememberState();
  virtual void EmitCFIRestoreState();
  virtual void EmitCFISameValue(int64_t Register);
  virtual void EmitCFIRestore(int64_t Register);
  virtual void EmitCFIRelOffset(int64_t Register, int64_t Offset);
  virtual void EmitCFIAdjustCfaOffset(int64_t Adjustment);
  virtual void EmitCFIEscape(StringRef Values);
  virtual void EmitCFIReturnColumn(int64_t Register);
  virtual void EmitCFIGnuArgsSize(int64_t Size);
  virtual void EmitCFISignalFrame();
  virtual void EmitCFIUndefined(int64_t Register);
  virtual void EmitCFIRegister(int64_t Register1, int64_t Register2);
  virtual void EmitCFIWindowSave();
  virtual void EmitCFINegateRAState();

  virtual void EmitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc = SMLoc());
  virtual void EmitWinCFIEndProc(SMLoc Loc = SMLoc());
  /// This is used on platforms, such as Windows on ARM64, that require function
  /// or funclet sizes to be emitted in .xdata before the End marker is emitted
  /// for the frame.  We cannot use the End marker, as it is not set at the
  /// point of emitting .xdata, in order to indicate that the frame is active.
  virtual void EmitWinCFIFuncletOrFuncEnd(SMLoc Loc = SMLoc());
  virtual void EmitWinCFIStartChained(SMLoc Loc = SMLoc());
  virtual void EmitWinCFIEndChained(SMLoc Loc = SMLoc());
  virtual void EmitWinCFIPushReg(unsigned Register, SMLoc Loc = SMLoc());
  virtual void EmitWinCFISetFrame(unsigned Register, unsigned Offset,
                                  SMLoc Loc = SMLoc());
  virtual void EmitWinCFIAllocStack(unsigned Size, SMLoc Loc = SMLoc());
  virtual void EmitWinCFISaveReg(unsigned Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  virtual void EmitWinCFISaveXMM(unsigned Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  virtual void EmitWinCFIPushFrame(bool Code, SMLoc Loc = SMLoc());
  virtual void EmitWinCFIEndProlog(SMLoc Loc = SMLoc());
  virtual void EmitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                                SMLoc Loc = SMLoc());
  virtual void EmitWinEHHandlerData(SMLoc Loc = SMLoc());

  virtual void emitCGProfileEntry(const MCSymbolRefExpr *From,
                                  const MCSymbolRefExpr *To, uint64_t Count);

  /// Get the .pdata section used for the given section. Typically the given
  /// section is either the main .text section or some other COMDAT .text
  /// section, but it may be any section containing code.
  MCSection *getAssociatedPDataSection(const MCSection *TextSec);

  /// Get the .xdata section used for the given section.
  MCSection *getAssociatedXDataSection(const MCSection *TextSec);

  virtual void EmitSyntaxDirective();

  /// Emit a .reloc directive.
  /// Returns true if the relocation could not be emitted because Name is not
  /// known.
  virtual bool EmitRelocDirective(const MCExpr &Offset, StringRef Name,
                                  const MCExpr *Expr, SMLoc Loc,
                                  const MCSubtargetInfo &STI) {
    return true;
  }

  virtual void EmitAddrsig() {}
  virtual void EmitAddrsigSym(const MCSymbol *Sym) {}

  /// Emit the given \p Instruction into the current section.
  /// PrintSchedInfo == true then schedul comment should be added to output
  virtual void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                               bool PrintSchedInfo = false);

  /// Set the bundle alignment mode from now on in the section.
  /// The argument is the power of 2 to which the alignment is set. The
  /// value 0 means turn the bundle alignment off.
  virtual void EmitBundleAlignMode(unsigned AlignPow2);

  /// The following instructions are a bundle-locked group.
  ///
  /// \param AlignToEnd - If true, the bundle-locked group will be aligned to
  ///                     the end of a bundle.
  virtual void EmitBundleLock(bool AlignToEnd);

  /// Ends a bundle-locked group.
  virtual void EmitBundleUnlock();

  /// If this file is backed by a assembly streamer, this dumps the
  /// specified string in the output .s file.  This capability is indicated by
  /// the hasRawTextSupport() predicate.  By default this aborts.
  void EmitRawText(const Twine &String);

  /// Streamer specific finalization.
  virtual void FinishImpl();
  /// Finish emission of machine code.
  void Finish();

  virtual bool mayHaveInstructions(MCSection &Sec) const { return true; }
};

/// Create a dummy machine code streamer, which does nothing. This is useful for
/// timing the assembler front end.
MCStreamer *createNullStreamer(MCContext &Ctx);

/// Create a machine code streamer which will print out assembly for the native
/// target, suitable for compiling with a native assembler.
///
/// \param InstPrint - If given, the instruction printer to use. If not given
/// the MCInst representation will be printed.  This method takes ownership of
/// InstPrint.
///
/// \param CE - If given, a code emitter to use to show the instruction
/// encoding inline with the assembly. This method takes ownership of \p CE.
///
/// \param TAB - If given, a target asm backend to use to show the fixup
/// information in conjunction with encoding information. This method takes
/// ownership of \p TAB.
///
/// \param ShowInst - Whether to show the MCInst representation inline with
/// the assembly.
MCStreamer *createAsmStreamer(MCContext &Ctx,
                              std::unique_ptr<formatted_raw_ostream> OS,
                              bool isVerboseAsm, bool useDwarfDirectory,
                              MCInstPrinter *InstPrint, MCCodeEmitter *CE,
                              MCAsmBackend *TAB, bool ShowInst);

} // end namespace llvm

#endif // LLVM_MC_MCSTREAMER_H
