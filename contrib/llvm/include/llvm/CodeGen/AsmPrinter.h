//===- llvm/CodeGen/AsmPrinter.h - AsmPrinter Framework ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the base class for target specific
// asm writers.  This class primarily handles common functionality used by
// all asm writers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTER_H
#define LLVM_CODEGEN_ASMPRINTER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class AsmPrinterHandler;
class BasicBlock;
class BlockAddress;
class Constant;
class ConstantArray;
class DataLayout;
class DIE;
class DIEAbbrev;
class DwarfDebug;
class GCMetadataPrinter;
class GCStrategy;
class GlobalIndirectSymbol;
class GlobalObject;
class GlobalValue;
class GlobalVariable;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineJumpTableInfo;
class MachineLoopInfo;
class MachineModuleInfo;
class MachineOptimizationRemarkEmitter;
class MCAsmInfo;
class MCCFIInstruction;
struct MCCodePaddingContext;
class MCContext;
class MCExpr;
class MCInst;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCTargetOptions;
class MDNode;
class Module;
class raw_ostream;
class StackMaps;
class TargetLoweringObjectFile;
class TargetMachine;

/// This class is intended to be used as a driving class for all asm writers.
class AsmPrinter : public MachineFunctionPass {
public:
  /// Target machine description.
  TargetMachine &TM;

  /// Target Asm Printer information.
  const MCAsmInfo *MAI;

  /// This is the context for the output file that we are streaming. This owns
  /// all of the global MC-related objects for the generated translation unit.
  MCContext &OutContext;

  /// This is the MCStreamer object for the file we are generating. This
  /// contains the transient state for the current translation unit that we are
  /// generating (such as the current section etc).
  std::unique_ptr<MCStreamer> OutStreamer;

  /// The current machine function.
  MachineFunction *MF = nullptr;

  /// This is a pointer to the current MachineModuleInfo.
  MachineModuleInfo *MMI = nullptr;

  /// This is a pointer to the current MachineLoopInfo.
  MachineDominatorTree *MDT = nullptr;

  /// This is a pointer to the current MachineLoopInfo.
  MachineLoopInfo *MLI = nullptr;

  /// Optimization remark emitter.
  MachineOptimizationRemarkEmitter *ORE;

  /// The symbol for the current function. This is recalculated at the beginning
  /// of each call to runOnMachineFunction().
  MCSymbol *CurrentFnSym = nullptr;

  /// The symbol used to represent the start of the current function for the
  /// purpose of calculating its size (e.g. using the .size directive). By
  /// default, this is equal to CurrentFnSym.
  MCSymbol *CurrentFnSymForSize = nullptr;

  /// Map global GOT equivalent MCSymbols to GlobalVariables and keep track of
  /// its number of uses by other globals.
  using GOTEquivUsePair = std::pair<const GlobalVariable *, unsigned>;
  MapVector<const MCSymbol *, GOTEquivUsePair> GlobalGOTEquivs;

  /// Enable print [latency:throughput] in output.
  bool EnablePrintSchedInfo = false;

private:
  MCSymbol *CurrentFnBegin = nullptr;
  MCSymbol *CurrentFnEnd = nullptr;
  MCSymbol *CurExceptionSym = nullptr;

  // The garbage collection metadata printer table.
  void *GCMetadataPrinters = nullptr; // Really a DenseMap.

  /// Emit comments in assembly output if this is true.
  bool VerboseAsm;

  static char ID;

protected:
  /// Protected struct HandlerInfo and Handlers permit target extended
  /// AsmPrinter adds their own handlers.
  struct HandlerInfo {
    AsmPrinterHandler *Handler;
    const char *TimerName;
    const char *TimerDescription;
    const char *TimerGroupName;
    const char *TimerGroupDescription;

    HandlerInfo(AsmPrinterHandler *Handler, const char *TimerName,
                const char *TimerDescription, const char *TimerGroupName,
                const char *TimerGroupDescription)
        : Handler(Handler), TimerName(TimerName),
          TimerDescription(TimerDescription), TimerGroupName(TimerGroupName),
          TimerGroupDescription(TimerGroupDescription) {}
  };

  /// A vector of all debug/EH info emitters we should use. This vector
  /// maintains ownership of the emitters.
  SmallVector<HandlerInfo, 1> Handlers;

public:
  struct SrcMgrDiagInfo {
    SourceMgr SrcMgr;
    std::vector<const MDNode *> LocInfos;
    LLVMContext::InlineAsmDiagHandlerTy DiagHandler;
    void *DiagContext;
  };

private:
  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineDominatorTree> OwnedMDT;

  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineLoopInfo> OwnedMLI;

  /// Structure for generating diagnostics for inline assembly. Only initialised
  /// when necessary.
  mutable std::unique_ptr<SrcMgrDiagInfo> DiagInfo;

  /// If the target supports dwarf debug info, this pointer is non-null.
  DwarfDebug *DD = nullptr;

  /// If the current module uses dwarf CFI annotations strictly for debugging.
  bool isCFIMoveForDebugging = false;

protected:
  explicit AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer);

public:
  ~AsmPrinter() override;

  DwarfDebug *getDwarfDebug() { return DD; }
  DwarfDebug *getDwarfDebug() const { return DD; }

  uint16_t getDwarfVersion() const;
  void setDwarfVersion(uint16_t Version);

  bool isPositionIndependent() const;

  /// Return true if assembly output should contain comments.
  bool isVerbose() const { return VerboseAsm; }

  /// Return a unique ID for the current function.
  unsigned getFunctionNumber() const;

  /// Return symbol for the function pseudo stack if the stack frame is not a
  /// register based.
  virtual const MCSymbol *getFunctionFrameSymbol() const { return nullptr; }

  MCSymbol *getFunctionBegin() const { return CurrentFnBegin; }
  MCSymbol *getFunctionEnd() const { return CurrentFnEnd; }
  MCSymbol *getCurExceptionSym();

  /// Return information about object file lowering.
  const TargetLoweringObjectFile &getObjFileLowering() const;

  /// Return information about data layout.
  const DataLayout &getDataLayout() const;

  /// Return the pointer size from the TargetMachine
  unsigned getPointerSize() const;

  /// Return information about subtarget.
  const MCSubtargetInfo &getSubtargetInfo() const;

  void EmitToStreamer(MCStreamer &S, const MCInst &Inst);

  /// Return the current section we are emitting to.
  const MCSection *getCurrentSection() const;

  void getNameWithPrefix(SmallVectorImpl<char> &Name,
                         const GlobalValue *GV) const;

  MCSymbol *getSymbol(const GlobalValue *GV) const;

  //===------------------------------------------------------------------===//
  // XRay instrumentation implementation.
  //===------------------------------------------------------------------===//
public:
  // This describes the kind of sled we're storing in the XRay table.
  enum class SledKind : uint8_t {
    FUNCTION_ENTER = 0,
    FUNCTION_EXIT = 1,
    TAIL_CALL = 2,
    LOG_ARGS_ENTER = 3,
    CUSTOM_EVENT = 4,
    TYPED_EVENT = 5,
  };

  // The table will contain these structs that point to the sled, the function
  // containing the sled, and what kind of sled (and whether they should always
  // be instrumented). We also use a version identifier that the runtime can use
  // to decide what to do with the sled, depending on the version of the sled.
  struct XRayFunctionEntry {
    const MCSymbol *Sled;
    const MCSymbol *Function;
    SledKind Kind;
    bool AlwaysInstrument;
    const class Function *Fn;
    uint8_t Version;

    void emit(int, MCStreamer *, const MCSymbol *) const;
  };

  // All the sleds to be emitted.
  SmallVector<XRayFunctionEntry, 4> Sleds;

  // A unique ID used for ELF sections associated with a particular function.
  unsigned XRayFnUniqueID = 0;

  // Helper function to record a given XRay sled.
  void recordSled(MCSymbol *Sled, const MachineInstr &MI, SledKind Kind,
                  uint8_t Version = 0);

  /// Emit a table with all XRay instrumentation points.
  void emitXRayTable();

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  /// Record analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Set up the AsmPrinter when we are working on a new module. If your pass
  /// overrides this, it must make sure to explicitly call this implementation.
  bool doInitialization(Module &M) override;

  /// Shut down the asmprinter. If you override this in your pass, you must make
  /// sure to call it explicitly.
  bool doFinalization(Module &M) override;

  /// Emit the specified function out to the OutStreamer.
  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    EmitFunctionBody();
    return false;
  }

  //===------------------------------------------------------------------===//
  // Coarse grained IR lowering routines.
  //===------------------------------------------------------------------===//

  /// This should be called when a new MachineFunction is being processed from
  /// runOnMachineFunction.
  void SetupMachineFunction(MachineFunction &MF);

  /// This method emits the body and trailer for a function.
  void EmitFunctionBody();

  void emitCFIInstruction(const MachineInstr &MI);

  void emitFrameAlloc(const MachineInstr &MI);

  void emitStackSizeSection(const MachineFunction &MF);

  enum CFIMoveType { CFI_M_None, CFI_M_EH, CFI_M_Debug };
  CFIMoveType needsCFIMoves() const;

  /// Returns false if needsCFIMoves() == CFI_M_EH for any function
  /// in the module.
  bool needsOnlyDebugCFIMoves() const { return isCFIMoveForDebugging; }

  bool needsSEHMoves();

  /// Print to the current output stream assembly representations of the
  /// constants in the constant pool MCP. This is used to print out constants
  /// which have been "spilled to memory" by the code generator.
  virtual void EmitConstantPool();

  /// Print assembly representations of the jump tables used by the current
  /// function to the current output stream.
  virtual void EmitJumpTableInfo();

  /// Emit the specified global variable to the .s file.
  virtual void EmitGlobalVariable(const GlobalVariable *GV);

  /// Check to see if the specified global is a special global used by LLVM. If
  /// so, emit it and return true, otherwise do nothing and return false.
  bool EmitSpecialLLVMGlobal(const GlobalVariable *GV);

  /// Emit an alignment directive to the specified power of two boundary. For
  /// example, if you pass in 3 here, you will get an 8 byte alignment. If a
  /// global value is specified, and if that global has an explicit alignment
  /// requested, it will override the alignment request if required for
  /// correctness.
  void EmitAlignment(unsigned NumBits, const GlobalObject *GV = nullptr) const;

  /// Lower the specified LLVM Constant to an MCExpr.
  virtual const MCExpr *lowerConstant(const Constant *CV);

  /// Print a general LLVM constant to the .s file.
  void EmitGlobalConstant(const DataLayout &DL, const Constant *CV);

  /// Unnamed constant global variables solely contaning a pointer to
  /// another globals variable act like a global variable "proxy", or GOT
  /// equivalents, i.e., it's only used to hold the address of the latter. One
  /// optimization is to replace accesses to these proxies by using the GOT
  /// entry for the final global instead. Hence, we select GOT equivalent
  /// candidates among all the module global variables, avoid emitting them
  /// unnecessarily and finally replace references to them by pc relative
  /// accesses to GOT entries.
  void computeGlobalGOTEquivs(Module &M);

  /// Constant expressions using GOT equivalent globals may not be
  /// eligible for PC relative GOT entry conversion, in such cases we need to
  /// emit the proxies we previously omitted in EmitGlobalVariable.
  void emitGlobalGOTEquivs();

  /// Emit the stack maps.
  void emitStackMaps(StackMaps &SM);

  //===------------------------------------------------------------------===//
  // Overridable Hooks
  //===------------------------------------------------------------------===//

  // Targets can, or in the case of EmitInstruction, must implement these to
  // customize output.

  /// This virtual method can be overridden by targets that want to emit
  /// something at the start of their file.
  virtual void EmitStartOfAsmFile(Module &) {}

  /// This virtual method can be overridden by targets that want to emit
  /// something at the end of their file.
  virtual void EmitEndOfAsmFile(Module &) {}

  /// Targets can override this to emit stuff before the first basic block in
  /// the function.
  virtual void EmitFunctionBodyStart() {}

  /// Targets can override this to emit stuff after the last basic block in the
  /// function.
  virtual void EmitFunctionBodyEnd() {}

  /// Targets can override this to emit stuff at the start of a basic block.
  /// By default, this method prints the label for the specified
  /// MachineBasicBlock, an alignment (if present) and a comment describing it
  /// if appropriate.
  virtual void EmitBasicBlockStart(const MachineBasicBlock &MBB) const;

  /// Targets can override this to emit stuff at the end of a basic block.
  virtual void EmitBasicBlockEnd(const MachineBasicBlock &MBB);

  /// Targets should implement this to emit instructions.
  virtual void EmitInstruction(const MachineInstr *) {
    llvm_unreachable("EmitInstruction not implemented");
  }

  /// Return the symbol for the specified constant pool entry.
  virtual MCSymbol *GetCPISymbol(unsigned CPID) const;

  virtual void EmitFunctionEntryLabel();

  virtual void EmitMachineConstantPoolValue(MachineConstantPoolValue *MCPV);

  /// Targets can override this to change how global constants that are part of
  /// a C++ static/global constructor list are emitted.
  virtual void EmitXXStructor(const DataLayout &DL, const Constant *CV) {
    EmitGlobalConstant(DL, CV);
  }

  /// Return true if the basic block has exactly one predecessor and the control
  /// transfer mechanism between the predecessor and this block is a
  /// fall-through.
  virtual bool
  isBlockOnlyReachableByFallthrough(const MachineBasicBlock *MBB) const;

  /// Targets can override this to customize the output of IMPLICIT_DEF
  /// instructions in verbose mode.
  virtual void emitImplicitDef(const MachineInstr *MI) const;

  //===------------------------------------------------------------------===//
  // Symbol Lowering Routines.
  //===------------------------------------------------------------------===//

  MCSymbol *createTempSymbol(const Twine &Name) const;

  /// Return the MCSymbol for a private symbol with global value name as its
  /// base, with the specified suffix.
  MCSymbol *getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                         StringRef Suffix) const;

  /// Return the MCSymbol for the specified ExternalSymbol.
  MCSymbol *GetExternalSymbolSymbol(StringRef Sym) const;

  /// Return the symbol for the specified jump table entry.
  MCSymbol *GetJTISymbol(unsigned JTID, bool isLinkerPrivate = false) const;

  /// Return the symbol for the specified jump table .set
  /// FIXME: privatize to AsmPrinter.
  MCSymbol *GetJTSetSymbol(unsigned UID, unsigned MBBID) const;

  /// Return the MCSymbol used to satisfy BlockAddress uses of the specified
  /// basic block.
  MCSymbol *GetBlockAddressSymbol(const BlockAddress *BA) const;
  MCSymbol *GetBlockAddressSymbol(const BasicBlock *BB) const;

  //===------------------------------------------------------------------===//
  // Emission Helper Routines.
  //===------------------------------------------------------------------===//

  /// This is just convenient handler for printing offsets.
  void printOffset(int64_t Offset, raw_ostream &OS) const;

  /// Emit a byte directive and value.
  void emitInt8(int Value) const;

  /// Emit a short directive and value.
  void emitInt16(int Value) const;

  /// Emit a long directive and value.
  void emitInt32(int Value) const;

  /// Emit a long long directive and value.
  void emitInt64(uint64_t Value) const;

  /// Emit something like ".long Hi-Lo" where the size in bytes of the directive
  /// is specified by Size and Hi/Lo specify the labels.  This implicitly uses
  /// .set if it is available.
  void EmitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                           unsigned Size) const;

  /// Emit something like ".uleb128 Hi-Lo".
  void EmitLabelDifferenceAsULEB128(const MCSymbol *Hi,
                                    const MCSymbol *Lo) const;

  /// Emit something like ".long Label+Offset" where the size in bytes of the
  /// directive is specified by Size and Label specifies the label.  This
  /// implicitly uses .set if it is available.
  void EmitLabelPlusOffset(const MCSymbol *Label, uint64_t Offset,
                           unsigned Size, bool IsSectionRelative = false) const;

  /// Emit something like ".long Label" where the size in bytes of the directive
  /// is specified by Size and Label specifies the label.
  void EmitLabelReference(const MCSymbol *Label, unsigned Size,
                          bool IsSectionRelative = false) const {
    EmitLabelPlusOffset(Label, 0, Size, IsSectionRelative);
  }

  /// Emit something like ".long Label + Offset".
  void EmitDwarfOffset(const MCSymbol *Label, uint64_t Offset) const;

  //===------------------------------------------------------------------===//
  // Dwarf Emission Helper Routines
  //===------------------------------------------------------------------===//

  /// Emit the specified signed leb128 value.
  void EmitSLEB128(int64_t Value, const char *Desc = nullptr) const;

  /// Emit the specified unsigned leb128 value.
  void EmitULEB128(uint64_t Value, const char *Desc = nullptr) const;

  /// Emit a .byte 42 directive that corresponds to an encoding.  If verbose
  /// assembly output is enabled, we output comments describing the encoding.
  /// Desc is a string saying what the encoding is specifying (e.g. "LSDA").
  void EmitEncodingByte(unsigned Val, const char *Desc = nullptr) const;

  /// Return the size of the encoding in bytes.
  unsigned GetSizeOfEncodedValue(unsigned Encoding) const;

  /// Emit reference to a ttype global with a specified encoding.
  void EmitTTypeReference(const GlobalValue *GV, unsigned Encoding) const;

  /// Emit a reference to a symbol for use in dwarf. Different object formats
  /// represent this in different ways. Some use a relocation others encode
  /// the label offset in its section.
  void emitDwarfSymbolReference(const MCSymbol *Label,
                                bool ForceOffset = false) const;

  /// Emit the 4-byte offset of a string from the start of its section.
  ///
  /// When possible, emit a DwarfStringPool section offset without any
  /// relocations, and without using the symbol.  Otherwise, defers to \a
  /// emitDwarfSymbolReference().
  void emitDwarfStringOffset(DwarfStringPoolEntry S) const;

  /// Emit the 4-byte offset of a string from the start of its section.
  void emitDwarfStringOffset(DwarfStringPoolEntryRef S) const {
    emitDwarfStringOffset(S.getEntry());
  }

  /// Get the value for DW_AT_APPLE_isa. Zero if no isa encoding specified.
  virtual unsigned getISAEncoding() { return 0; }

  /// Emit the directive and value for debug thread local expression
  ///
  /// \p Value - The value to emit.
  /// \p Size - The size of the integer (in bytes) to emit.
  virtual void EmitDebugValue(const MCExpr *Value, unsigned Size) const;

  //===------------------------------------------------------------------===//
  // Dwarf Lowering Routines
  //===------------------------------------------------------------------===//

  /// Emit frame instruction to describe the layout of the frame.
  void emitCFIInstruction(const MCCFIInstruction &Inst) const;

  /// Emit Dwarf abbreviation table.
  template <typename T> void emitDwarfAbbrevs(const T &Abbrevs) const {
    // For each abbreviation.
    for (const auto &Abbrev : Abbrevs)
      emitDwarfAbbrev(*Abbrev);

    // Mark end of abbreviations.
    EmitULEB128(0, "EOM(3)");
  }

  void emitDwarfAbbrev(const DIEAbbrev &Abbrev) const;

  /// Recursively emit Dwarf DIE tree.
  void emitDwarfDIE(const DIE &Die) const;

  //===------------------------------------------------------------------===//
  // Inline Asm Support
  //===------------------------------------------------------------------===//

  // These are hooks that targets can override to implement inline asm
  // support.  These should probably be moved out of AsmPrinter someday.

  /// Print information related to the specified machine instr that is
  /// independent of the operand, and may be independent of the instr itself.
  /// This can be useful for portably encoding the comment character or other
  /// bits of target-specific knowledge into the asmstrings.  The syntax used is
  /// ${:comment}.  Targets can override this to add support for their own
  /// strange codes.
  virtual void PrintSpecial(const MachineInstr *MI, raw_ostream &OS,
                            const char *Code) const;

  /// Print the specified operand of MI, an INLINEASM instruction, using the
  /// specified assembler variant.  Targets should override this to format as
  /// appropriate.  This method can return true if the operand is erroneous.
  virtual bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                               unsigned AsmVariant, const char *ExtraCode,
                               raw_ostream &OS);

  /// Print the specified operand of MI, an INLINEASM instruction, using the
  /// specified assembler variant as an address. Targets should override this to
  /// format as appropriate.  This method can return true if the operand is
  /// erroneous.
  virtual bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                     unsigned AsmVariant, const char *ExtraCode,
                                     raw_ostream &OS);

  /// Let the target do anything it needs to do before emitting inlineasm.
  /// \p StartInfo - the subtarget info before parsing inline asm
  virtual void emitInlineAsmStart() const;

  /// Let the target do anything it needs to do after emitting inlineasm.
  /// This callback can be used restore the original mode in case the
  /// inlineasm contains directives to switch modes.
  /// \p StartInfo - the original subtarget info before inline asm
  /// \p EndInfo   - the final subtarget info after parsing the inline asm,
  ///                or NULL if the value is unknown.
  virtual void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                const MCSubtargetInfo *EndInfo) const;

private:
  /// Private state for PrintSpecial()
  // Assign a unique ID to this machine instruction.
  mutable const MachineInstr *LastMI = nullptr;
  mutable unsigned LastFn = 0;
  mutable unsigned Counter = ~0U;

  /// This method emits the header for the current function.
  virtual void EmitFunctionHeader();

  /// Emit a blob of inline asm to the output streamer.
  void
  EmitInlineAsm(StringRef Str, const MCSubtargetInfo &STI,
                const MCTargetOptions &MCOptions,
                const MDNode *LocMDNode = nullptr,
                InlineAsm::AsmDialect AsmDialect = InlineAsm::AD_ATT) const;

  /// This method formats and emits the specified machine instruction that is an
  /// inline asm.
  void EmitInlineAsm(const MachineInstr *MI) const;

  /// Add inline assembly info to the diagnostics machinery, so we can
  /// emit file and position info. Returns SrcMgr memory buffer position.
  unsigned addInlineAsmDiagBuffer(StringRef AsmStr,
                                  const MDNode *LocMDNode) const;

  //===------------------------------------------------------------------===//
  // Internal Implementation Details
  //===------------------------------------------------------------------===//

  /// This emits visibility information about symbol, if this is supported by
  /// the target.
  void EmitVisibility(MCSymbol *Sym, unsigned Visibility,
                      bool IsDefinition = true) const;

  void EmitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const;

  void EmitJumpTableEntry(const MachineJumpTableInfo *MJTI,
                          const MachineBasicBlock *MBB, unsigned uid) const;
  void EmitLLVMUsedList(const ConstantArray *InitList);
  /// Emit llvm.ident metadata in an '.ident' directive.
  void EmitModuleIdents(Module &M);
  /// Emit bytes for llvm.commandline metadata.
  void EmitModuleCommandLines(Module &M);
  void EmitXXStructorList(const DataLayout &DL, const Constant *List,
                          bool isCtor);

  GCMetadataPrinter *GetOrCreateGCPrinter(GCStrategy &S);
  /// Emit GlobalAlias or GlobalIFunc.
  void emitGlobalIndirectSymbol(Module &M, const GlobalIndirectSymbol &GIS);
  void setupCodePaddingContext(const MachineBasicBlock &MBB,
                               MCCodePaddingContext &Context) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_ASMPRINTER_H
