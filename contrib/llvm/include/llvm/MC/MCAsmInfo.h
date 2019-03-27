//===-- llvm/MC/MCAsmInfo.h - Asm info --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the basis for target specific
// asm writers.  This class primarily takes care of global printing constants,
// which are used in very similar ways across all targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFO_H
#define LLVM_MC_MCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCTargetOptions.h"
#include <vector>

namespace llvm {

class MCContext;
class MCExpr;
class MCSection;
class MCStreamer;
class MCSymbol;

namespace WinEH {

enum class EncodingType {
  Invalid, /// Invalid
  Alpha,   /// Windows Alpha
  Alpha64, /// Windows AXP64
  ARM,     /// Windows NT (Windows on ARM)
  CE,      /// Windows CE ARM, PowerPC, SH3, SH4
  Itanium, /// Windows x64, Windows Itanium (IA-64)
  X86,     /// Windows x86, uses no CFI, just EH tables
  MIPS = Alpha,
};

} // end namespace WinEH

namespace LCOMM {

enum LCOMMType { NoAlignment, ByteAlignment, Log2Alignment };

} // end namespace LCOMM

/// This class is intended to be used as a base class for asm
/// properties and features specific to the target.
class MCAsmInfo {
protected:
  //===------------------------------------------------------------------===//
  // Properties to be set by the target writer, used to configure asm printer.
  //

  /// Code pointer size in bytes.  Default is 4.
  unsigned CodePointerSize = 4;

  /// Size of the stack slot reserved for callee-saved registers, in bytes.
  /// Default is same as pointer size.
  unsigned CalleeSaveStackSlotSize = 4;

  /// True if target is little endian.  Default is true.
  bool IsLittleEndian = true;

  /// True if target stack grow up.  Default is false.
  bool StackGrowsUp = false;

  /// True if this target has the MachO .subsections_via_symbols directive.
  /// Default is false.
  bool HasSubsectionsViaSymbols = false;

  /// True if this is a MachO target that supports the macho-specific .zerofill
  /// directive for emitting BSS Symbols.  Default is false.
  bool HasMachoZeroFillDirective = false;

  /// True if this is a MachO target that supports the macho-specific .tbss
  /// directive for emitting thread local BSS Symbols.  Default is false.
  bool HasMachoTBSSDirective = false;

  /// True if this is a non-GNU COFF target. The COFF port of the GNU linker
  /// doesn't handle associative comdats in the way that we would like to use
  /// them.
  bool HasCOFFAssociativeComdats = false;

  /// True if this is a non-GNU COFF target. For GNU targets, we don't generate
  /// constants into comdat sections.
  bool HasCOFFComdatConstants = false;

  /// This is the maximum possible length of an instruction, which is needed to
  /// compute the size of an inline asm.  Defaults to 4.
  unsigned MaxInstLength = 4;

  /// Every possible instruction length is a multiple of this value.  Factored
  /// out in .debug_frame and .debug_line.  Defaults to 1.
  unsigned MinInstAlignment = 1;

  /// The '$' token, when not referencing an identifier or constant, refers to
  /// the current PC.  Defaults to false.
  bool DollarIsPC = false;

  /// This string, if specified, is used to separate instructions from each
  /// other when on the same line.  Defaults to ';'
  const char *SeparatorString;

  /// This indicates the comment character used by the assembler.  Defaults to
  /// "#"
  StringRef CommentString;

  /// This is appended to emitted labels.  Defaults to ":"
  const char *LabelSuffix;

  // Print the EH begin symbol with an assignment. Defaults to false.
  bool UseAssignmentForEHBegin = false;

  // Do we need to create a local symbol for .size?
  bool NeedsLocalForSize = false;

  /// This prefix is used for globals like constant pool entries that are
  /// completely private to the .s file and should not have names in the .o
  /// file.  Defaults to "L"
  StringRef PrivateGlobalPrefix;

  /// This prefix is used for labels for basic blocks. Defaults to the same as
  /// PrivateGlobalPrefix.
  StringRef PrivateLabelPrefix;

  /// This prefix is used for symbols that should be passed through the
  /// assembler but be removed by the linker.  This is 'l' on Darwin, currently
  /// used for some ObjC metadata.  The default of "" meast that for this system
  /// a plain private symbol should be used.  Defaults to "".
  StringRef LinkerPrivateGlobalPrefix;

  /// If these are nonempty, they contain a directive to emit before and after
  /// an inline assembly statement.  Defaults to "#APP\n", "#NO_APP\n"
  const char *InlineAsmStart;
  const char *InlineAsmEnd;

  /// These are assembly directives that tells the assembler to interpret the
  /// following instructions differently.  Defaults to ".code16", ".code32",
  /// ".code64".
  const char *Code16Directive;
  const char *Code32Directive;
  const char *Code64Directive;

  /// Which dialect of an assembler variant to use.  Defaults to 0
  unsigned AssemblerDialect = 0;

  /// This is true if the assembler allows @ characters in symbol names.
  /// Defaults to false.
  bool AllowAtInName = false;

  /// If this is true, symbol names with invalid characters will be printed in
  /// quotes.
  bool SupportsQuotedNames = true;

  /// This is true if data region markers should be printed as
  /// ".data_region/.end_data_region" directives. If false, use "$d/$a" labels
  /// instead.
  bool UseDataRegionDirectives = false;

  //===--- Data Emission Directives -------------------------------------===//

  /// This should be set to the directive used to get some number of zero bytes
  /// emitted to the current section.  Common cases are "\t.zero\t" and
  /// "\t.space\t".  If this is set to null, the Data*bitsDirective's will be
  /// used to emit zero bytes.  Defaults to "\t.zero\t"
  const char *ZeroDirective;

  /// This directive allows emission of an ascii string with the standard C
  /// escape characters embedded into it.  If a target doesn't support this, it
  /// can be set to null. Defaults to "\t.ascii\t"
  const char *AsciiDirective;

  /// If not null, this allows for special handling of zero terminated strings
  /// on this target.  This is commonly supported as ".asciz".  If a target
  /// doesn't support this, it can be set to null.  Defaults to "\t.asciz\t"
  const char *AscizDirective;

  /// These directives are used to output some unit of integer data to the
  /// current section.  If a data directive is set to null, smaller data
  /// directives will be used to emit the large sizes.  Defaults to "\t.byte\t",
  /// "\t.short\t", "\t.long\t", "\t.quad\t"
  const char *Data8bitsDirective;
  const char *Data16bitsDirective;
  const char *Data32bitsDirective;
  const char *Data64bitsDirective;

  /// If non-null, a directive that is used to emit a word which should be
  /// relocated as a 64-bit GP-relative offset, e.g. .gpdword on Mips.  Defaults
  /// to nullptr.
  const char *GPRel64Directive = nullptr;

  /// If non-null, a directive that is used to emit a word which should be
  /// relocated as a 32-bit GP-relative offset, e.g. .gpword on Mips or .gprel32
  /// on Alpha.  Defaults to nullptr.
  const char *GPRel32Directive = nullptr;

  /// If non-null, directives that are used to emit a word/dword which should
  /// be relocated as a 32/64-bit DTP/TP-relative offset, e.g. .dtprelword/
  /// .dtpreldword/.tprelword/.tpreldword on Mips.
  const char *DTPRel32Directive = nullptr;
  const char *DTPRel64Directive = nullptr;
  const char *TPRel32Directive = nullptr;
  const char *TPRel64Directive = nullptr;

  /// This is true if this target uses "Sun Style" syntax for section switching
  /// ("#alloc,#write" etc) instead of the normal ELF syntax (,"a,w") in
  /// .section directives.  Defaults to false.
  bool SunStyleELFSectionSwitchSyntax = false;

  /// This is true if this target uses ELF '.section' directive before the
  /// '.bss' one. It's used for PPC/Linux which doesn't support the '.bss'
  /// directive only.  Defaults to false.
  bool UsesELFSectionDirectiveForBSS = false;

  bool NeedsDwarfSectionOffsetDirective = false;

  //===--- Alignment Information ----------------------------------------===//

  /// If this is true (the default) then the asmprinter emits ".align N"
  /// directives, where N is the number of bytes to align to.  Otherwise, it
  /// emits ".align log2(N)", e.g. 3 to align to an 8 byte boundary.  Defaults
  /// to true.
  bool AlignmentIsInBytes = true;

  /// If non-zero, this is used to fill the executable space created as the
  /// result of a alignment directive.  Defaults to 0
  unsigned TextAlignFillValue = 0;

  //===--- Global Variable Emission Directives --------------------------===//

  /// This is the directive used to declare a global entity. Defaults to
  /// ".globl".
  const char *GlobalDirective;

  /// True if the expression
  ///   .long f - g
  /// uses a relocation but it can be suppressed by writing
  ///   a = f - g
  ///   .long a
  bool SetDirectiveSuppressesReloc = false;

  /// False if the assembler requires that we use
  /// \code
  ///   Lc = a - b
  ///   .long Lc
  /// \endcode
  //
  /// instead of
  //
  /// \code
  ///   .long a - b
  /// \endcode
  ///
  ///  Defaults to true.
  bool HasAggressiveSymbolFolding = true;

  /// True is .comm's and .lcomms optional alignment is to be specified in bytes
  /// instead of log2(n).  Defaults to true.
  bool COMMDirectiveAlignmentIsInBytes = true;

  /// Describes if the .lcomm directive for the target supports an alignment
  /// argument and how it is interpreted.  Defaults to NoAlignment.
  LCOMM::LCOMMType LCOMMDirectiveAlignmentType = LCOMM::NoAlignment;

  // True if the target allows .align directives on functions. This is true for
  // most targets, so defaults to true.
  bool HasFunctionAlignment = true;

  /// True if the target has .type and .size directives, this is true for most
  /// ELF targets.  Defaults to true.
  bool HasDotTypeDotSizeDirective = true;

  /// True if the target has a single parameter .file directive, this is true
  /// for ELF targets.  Defaults to true.
  bool HasSingleParameterDotFile = true;

  /// True if the target has a .ident directive, this is true for ELF targets.
  /// Defaults to false.
  bool HasIdentDirective = false;

  /// True if this target supports the MachO .no_dead_strip directive.  Defaults
  /// to false.
  bool HasNoDeadStrip = false;

  /// True if this target supports the MachO .alt_entry directive.  Defaults to
  /// false.
  bool HasAltEntry = false;

  /// Used to declare a global as being a weak symbol. Defaults to ".weak".
  const char *WeakDirective;

  /// This directive, if non-null, is used to declare a global as being a weak
  /// undefined symbol.  Defaults to nullptr.
  const char *WeakRefDirective = nullptr;

  /// True if we have a directive to declare a global as being a weak defined
  /// symbol.  Defaults to false.
  bool HasWeakDefDirective = false;

  /// True if we have a directive to declare a global as being a weak defined
  /// symbol that can be hidden (unexported).  Defaults to false.
  bool HasWeakDefCanBeHiddenDirective = false;

  /// True if we have a .linkonce directive.  This is used on cygwin/mingw.
  /// Defaults to false.
  bool HasLinkOnceDirective = false;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// hidden visibility.  Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare an undefined
  /// symbol as having hidden visibility. Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenDeclarationVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// protected visibility.  Defaults to MCSA_Protected
  MCSymbolAttr ProtectedVisibilityAttr = MCSA_Protected;

  //===--- Dwarf Emission Directives -----------------------------------===//

  /// True if target supports emission of debugging information.  Defaults to
  /// false.
  bool SupportsDebugInformation = false;

  /// Exception handling format for the target.  Defaults to None.
  ExceptionHandling ExceptionsType = ExceptionHandling::None;

  /// Windows exception handling data (.pdata) encoding.  Defaults to Invalid.
  WinEH::EncodingType WinEHEncodingType = WinEH::EncodingType::Invalid;

  /// True if Dwarf2 output generally uses relocations for references to other
  /// .debug_* sections.
  bool DwarfUsesRelocationsAcrossSections = true;

  /// True if DWARF FDE symbol reference relocations should be replaced by an
  /// absolute difference.
  bool DwarfFDESymbolsUseAbsDiff = false;

  /// True if dwarf register numbers are printed instead of symbolic register
  /// names in .cfi_* directives.  Defaults to false.
  bool DwarfRegNumForCFI = false;

  /// True if target uses parens to indicate the symbol variant instead of @.
  /// For example, foo(plt) instead of foo@plt.  Defaults to false.
  bool UseParensForSymbolVariant = false;

  /// True if the target supports flags in ".loc" directive, false if only
  /// location is allowed.
  bool SupportsExtendedDwarfLocDirective = true;

  //===--- Prologue State ----------------------------------------------===//

  std::vector<MCCFIInstruction> InitialFrameState;

  //===--- Integrated Assembler Information ----------------------------===//

  /// Should we use the integrated assembler?
  /// The integrated assembler should be enabled by default (by the
  /// constructors) when failing to parse a valid piece of assembly (inline
  /// or otherwise) is considered a bug. It may then be overridden after
  /// construction (see LLVMTargetMachine::initAsmInfo()).
  bool UseIntegratedAssembler;

  /// Preserve Comments in assembly
  bool PreserveAsmComments;

  /// Compress DWARF debug sections. Defaults to no compression.
  DebugCompressionType CompressDebugSections = DebugCompressionType::None;

  /// True if the integrated assembler should interpret 'a >> b' constant
  /// expressions as logical rather than arithmetic.
  bool UseLogicalShr = true;

  // If true, emit GOTPCRELX/REX_GOTPCRELX instead of GOTPCREL, on
  // X86_64 ELF.
  bool RelaxELFRelocations = true;

  // If true, then the lexer and expression parser will support %neg(),
  // %hi(), and similar unary operators.
  bool HasMipsExpressions = false;

public:
  explicit MCAsmInfo();
  virtual ~MCAsmInfo();

  /// Get the code pointer size in bytes.
  unsigned getCodePointerSize() const { return CodePointerSize; }

  /// Get the callee-saved register stack slot
  /// size in bytes.
  unsigned getCalleeSaveStackSlotSize() const {
    return CalleeSaveStackSlotSize;
  }

  /// True if the target is little endian.
  bool isLittleEndian() const { return IsLittleEndian; }

  /// True if target stack grow up.
  bool isStackGrowthDirectionUp() const { return StackGrowsUp; }

  bool hasSubsectionsViaSymbols() const { return HasSubsectionsViaSymbols; }

  // Data directive accessors.

  const char *getData8bitsDirective() const { return Data8bitsDirective; }
  const char *getData16bitsDirective() const { return Data16bitsDirective; }
  const char *getData32bitsDirective() const { return Data32bitsDirective; }
  const char *getData64bitsDirective() const { return Data64bitsDirective; }
  const char *getGPRel64Directive() const { return GPRel64Directive; }
  const char *getGPRel32Directive() const { return GPRel32Directive; }
  const char *getDTPRel64Directive() const { return DTPRel64Directive; }
  const char *getDTPRel32Directive() const { return DTPRel32Directive; }
  const char *getTPRel64Directive() const { return TPRel64Directive; }
  const char *getTPRel32Directive() const { return TPRel32Directive; }

  /// Targets can implement this method to specify a section to switch to if the
  /// translation unit doesn't have any trampolines that require an executable
  /// stack.
  virtual MCSection *getNonexecutableStackSection(MCContext &Ctx) const {
    return nullptr;
  }

  /// True if the section is atomized using the symbols in it.
  /// This is false if the section is not atomized at all (most ELF sections) or
  /// if it is atomized based on its contents (MachO' __TEXT,__cstring for
  /// example).
  virtual bool isSectionAtomizableBySymbols(const MCSection &Section) const;

  virtual const MCExpr *getExprForPersonalitySymbol(const MCSymbol *Sym,
                                                    unsigned Encoding,
                                                    MCStreamer &Streamer) const;

  virtual const MCExpr *getExprForFDESymbol(const MCSymbol *Sym,
                                            unsigned Encoding,
                                            MCStreamer &Streamer) const;

  /// Return true if the identifier \p Name does not need quotes to be
  /// syntactically correct.
  virtual bool isValidUnquotedName(StringRef Name) const;

  /// Return true if the .section directive should be omitted when
  /// emitting \p SectionName.  For example:
  ///
  /// shouldOmitSectionDirective(".text")
  ///
  /// returns false => .section .text,#alloc,#execinstr
  /// returns true  => .text
  virtual bool shouldOmitSectionDirective(StringRef SectionName) const;

  bool usesSunStyleELFSectionSwitchSyntax() const {
    return SunStyleELFSectionSwitchSyntax;
  }

  bool usesELFSectionDirectiveForBSS() const {
    return UsesELFSectionDirectiveForBSS;
  }

  bool needsDwarfSectionOffsetDirective() const {
    return NeedsDwarfSectionOffsetDirective;
  }

  // Accessors.

  bool hasMachoZeroFillDirective() const { return HasMachoZeroFillDirective; }
  bool hasMachoTBSSDirective() const { return HasMachoTBSSDirective; }
  bool hasCOFFAssociativeComdats() const { return HasCOFFAssociativeComdats; }
  bool hasCOFFComdatConstants() const { return HasCOFFComdatConstants; }
  unsigned getMaxInstLength() const { return MaxInstLength; }
  unsigned getMinInstAlignment() const { return MinInstAlignment; }
  bool getDollarIsPC() const { return DollarIsPC; }
  const char *getSeparatorString() const { return SeparatorString; }

  /// This indicates the column (zero-based) at which asm comments should be
  /// printed.
  unsigned getCommentColumn() const { return 40; }

  StringRef getCommentString() const { return CommentString; }
  const char *getLabelSuffix() const { return LabelSuffix; }

  bool useAssignmentForEHBegin() const { return UseAssignmentForEHBegin; }
  bool needsLocalForSize() const { return NeedsLocalForSize; }
  StringRef getPrivateGlobalPrefix() const { return PrivateGlobalPrefix; }
  StringRef getPrivateLabelPrefix() const { return PrivateLabelPrefix; }

  bool hasLinkerPrivateGlobalPrefix() const {
    return LinkerPrivateGlobalPrefix[0] != '\0';
  }

  StringRef getLinkerPrivateGlobalPrefix() const {
    if (hasLinkerPrivateGlobalPrefix())
      return LinkerPrivateGlobalPrefix;
    return getPrivateGlobalPrefix();
  }

  const char *getInlineAsmStart() const { return InlineAsmStart; }
  const char *getInlineAsmEnd() const { return InlineAsmEnd; }
  const char *getCode16Directive() const { return Code16Directive; }
  const char *getCode32Directive() const { return Code32Directive; }
  const char *getCode64Directive() const { return Code64Directive; }
  unsigned getAssemblerDialect() const { return AssemblerDialect; }
  bool doesAllowAtInName() const { return AllowAtInName; }
  bool supportsNameQuoting() const { return SupportsQuotedNames; }

  bool doesSupportDataRegionDirectives() const {
    return UseDataRegionDirectives;
  }

  const char *getZeroDirective() const { return ZeroDirective; }
  const char *getAsciiDirective() const { return AsciiDirective; }
  const char *getAscizDirective() const { return AscizDirective; }
  bool getAlignmentIsInBytes() const { return AlignmentIsInBytes; }
  unsigned getTextAlignFillValue() const { return TextAlignFillValue; }
  const char *getGlobalDirective() const { return GlobalDirective; }

  bool doesSetDirectiveSuppressReloc() const {
    return SetDirectiveSuppressesReloc;
  }

  bool hasAggressiveSymbolFolding() const { return HasAggressiveSymbolFolding; }

  bool getCOMMDirectiveAlignmentIsInBytes() const {
    return COMMDirectiveAlignmentIsInBytes;
  }

  LCOMM::LCOMMType getLCOMMDirectiveAlignmentType() const {
    return LCOMMDirectiveAlignmentType;
  }

  bool hasFunctionAlignment() const { return HasFunctionAlignment; }
  bool hasDotTypeDotSizeDirective() const { return HasDotTypeDotSizeDirective; }
  bool hasSingleParameterDotFile() const { return HasSingleParameterDotFile; }
  bool hasIdentDirective() const { return HasIdentDirective; }
  bool hasNoDeadStrip() const { return HasNoDeadStrip; }
  bool hasAltEntry() const { return HasAltEntry; }
  const char *getWeakDirective() const { return WeakDirective; }
  const char *getWeakRefDirective() const { return WeakRefDirective; }
  bool hasWeakDefDirective() const { return HasWeakDefDirective; }

  bool hasWeakDefCanBeHiddenDirective() const {
    return HasWeakDefCanBeHiddenDirective;
  }

  bool hasLinkOnceDirective() const { return HasLinkOnceDirective; }

  MCSymbolAttr getHiddenVisibilityAttr() const { return HiddenVisibilityAttr; }

  MCSymbolAttr getHiddenDeclarationVisibilityAttr() const {
    return HiddenDeclarationVisibilityAttr;
  }

  MCSymbolAttr getProtectedVisibilityAttr() const {
    return ProtectedVisibilityAttr;
  }

  bool doesSupportDebugInformation() const { return SupportsDebugInformation; }

  bool doesSupportExceptionHandling() const {
    return ExceptionsType != ExceptionHandling::None;
  }

  ExceptionHandling getExceptionHandlingType() const { return ExceptionsType; }
  WinEH::EncodingType getWinEHEncodingType() const { return WinEHEncodingType; }

  void setExceptionsType(ExceptionHandling EH) {
    ExceptionsType = EH;
  }

  /// Returns true if the exception handling method for the platform uses call
  /// frame information to unwind.
  bool usesCFIForEH() const {
    return (ExceptionsType == ExceptionHandling::DwarfCFI ||
            ExceptionsType == ExceptionHandling::ARM || usesWindowsCFI());
  }

  bool usesWindowsCFI() const {
    return ExceptionsType == ExceptionHandling::WinEH &&
           (WinEHEncodingType != WinEH::EncodingType::Invalid &&
            WinEHEncodingType != WinEH::EncodingType::X86);
  }

  bool doesDwarfUseRelocationsAcrossSections() const {
    return DwarfUsesRelocationsAcrossSections;
  }

  bool doDwarfFDESymbolsUseAbsDiff() const { return DwarfFDESymbolsUseAbsDiff; }
  bool useDwarfRegNumForCFI() const { return DwarfRegNumForCFI; }
  bool useParensForSymbolVariant() const { return UseParensForSymbolVariant; }
  bool supportsExtendedDwarfLocDirective() const {
    return SupportsExtendedDwarfLocDirective;
  }

  void addInitialFrameState(const MCCFIInstruction &Inst) {
    InitialFrameState.push_back(Inst);
  }

  const std::vector<MCCFIInstruction> &getInitialFrameState() const {
    return InitialFrameState;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  bool useIntegratedAssembler() const { return UseIntegratedAssembler; }

  /// Set whether assembly (inline or otherwise) should be parsed.
  virtual void setUseIntegratedAssembler(bool Value) {
    UseIntegratedAssembler = Value;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  bool preserveAsmComments() const { return PreserveAsmComments; }

  /// Set whether assembly (inline or otherwise) should be parsed.
  virtual void setPreserveAsmComments(bool Value) {
    PreserveAsmComments = Value;
  }

  DebugCompressionType compressDebugSections() const {
    return CompressDebugSections;
  }

  void setCompressDebugSections(DebugCompressionType CompressDebugSections) {
    this->CompressDebugSections = CompressDebugSections;
  }

  bool shouldUseLogicalShr() const { return UseLogicalShr; }

  bool canRelaxRelocations() const { return RelaxELFRelocations; }
  void setRelaxELFRelocations(bool V) { RelaxELFRelocations = V; }
  bool hasMipsExpressions() const { return HasMipsExpressions; }
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFO_H
