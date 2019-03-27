//===- ARMAsmParser.cpp - Parse ARM assembly to MCInst instructions -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARMFeatures.h"
#include "InstPrinter/ARMInstPrinter.h"
#include "Utils/ARMBaseInfo.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMMCExpr.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCAsmParserUtils.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/ARMEHABI.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "asm-parser"

using namespace llvm;

namespace {

enum class ImplicitItModeTy { Always, Never, ARMOnly, ThumbOnly };

static cl::opt<ImplicitItModeTy> ImplicitItMode(
    "arm-implicit-it", cl::init(ImplicitItModeTy::ARMOnly),
    cl::desc("Allow conditional instructions outdside of an IT block"),
    cl::values(clEnumValN(ImplicitItModeTy::Always, "always",
                          "Accept in both ISAs, emit implicit ITs in Thumb"),
               clEnumValN(ImplicitItModeTy::Never, "never",
                          "Warn in ARM, reject in Thumb"),
               clEnumValN(ImplicitItModeTy::ARMOnly, "arm",
                          "Accept in ARM, reject in Thumb"),
               clEnumValN(ImplicitItModeTy::ThumbOnly, "thumb",
                          "Warn in ARM, emit implicit ITs in Thumb")));

static cl::opt<bool> AddBuildAttributes("arm-add-build-attributes",
                                        cl::init(false));

enum VectorLaneTy { NoLanes, AllLanes, IndexedLane };

class UnwindContext {
  using Locs = SmallVector<SMLoc, 4>;

  MCAsmParser &Parser;
  Locs FnStartLocs;
  Locs CantUnwindLocs;
  Locs PersonalityLocs;
  Locs PersonalityIndexLocs;
  Locs HandlerDataLocs;
  int FPReg;

public:
  UnwindContext(MCAsmParser &P) : Parser(P), FPReg(ARM::SP) {}

  bool hasFnStart() const { return !FnStartLocs.empty(); }
  bool cantUnwind() const { return !CantUnwindLocs.empty(); }
  bool hasHandlerData() const { return !HandlerDataLocs.empty(); }

  bool hasPersonality() const {
    return !(PersonalityLocs.empty() && PersonalityIndexLocs.empty());
  }

  void recordFnStart(SMLoc L) { FnStartLocs.push_back(L); }
  void recordCantUnwind(SMLoc L) { CantUnwindLocs.push_back(L); }
  void recordPersonality(SMLoc L) { PersonalityLocs.push_back(L); }
  void recordHandlerData(SMLoc L) { HandlerDataLocs.push_back(L); }
  void recordPersonalityIndex(SMLoc L) { PersonalityIndexLocs.push_back(L); }

  void saveFPReg(int Reg) { FPReg = Reg; }
  int getFPReg() const { return FPReg; }

  void emitFnStartLocNotes() const {
    for (Locs::const_iterator FI = FnStartLocs.begin(), FE = FnStartLocs.end();
         FI != FE; ++FI)
      Parser.Note(*FI, ".fnstart was specified here");
  }

  void emitCantUnwindLocNotes() const {
    for (Locs::const_iterator UI = CantUnwindLocs.begin(),
                              UE = CantUnwindLocs.end(); UI != UE; ++UI)
      Parser.Note(*UI, ".cantunwind was specified here");
  }

  void emitHandlerDataLocNotes() const {
    for (Locs::const_iterator HI = HandlerDataLocs.begin(),
                              HE = HandlerDataLocs.end(); HI != HE; ++HI)
      Parser.Note(*HI, ".handlerdata was specified here");
  }

  void emitPersonalityLocNotes() const {
    for (Locs::const_iterator PI = PersonalityLocs.begin(),
                              PE = PersonalityLocs.end(),
                              PII = PersonalityIndexLocs.begin(),
                              PIE = PersonalityIndexLocs.end();
         PI != PE || PII != PIE;) {
      if (PI != PE && (PII == PIE || PI->getPointer() < PII->getPointer()))
        Parser.Note(*PI++, ".personality was specified here");
      else if (PII != PIE && (PI == PE || PII->getPointer() < PI->getPointer()))
        Parser.Note(*PII++, ".personalityindex was specified here");
      else
        llvm_unreachable(".personality and .personalityindex cannot be "
                         "at the same location");
    }
  }

  void reset() {
    FnStartLocs = Locs();
    CantUnwindLocs = Locs();
    PersonalityLocs = Locs();
    HandlerDataLocs = Locs();
    PersonalityIndexLocs = Locs();
    FPReg = ARM::SP;
  }
};

class ARMAsmParser : public MCTargetAsmParser {
  const MCRegisterInfo *MRI;
  UnwindContext UC;

  ARMTargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<ARMTargetStreamer &>(TS);
  }

  // Map of register aliases registers via the .req directive.
  StringMap<unsigned> RegisterReqs;

  bool NextSymbolIsThumb;

  bool useImplicitITThumb() const {
    return ImplicitItMode == ImplicitItModeTy::Always ||
           ImplicitItMode == ImplicitItModeTy::ThumbOnly;
  }

  bool useImplicitITARM() const {
    return ImplicitItMode == ImplicitItModeTy::Always ||
           ImplicitItMode == ImplicitItModeTy::ARMOnly;
  }

  struct {
    ARMCC::CondCodes Cond;    // Condition for IT block.
    unsigned Mask:4;          // Condition mask for instructions.
                              // Starting at first 1 (from lsb).
                              //   '1'  condition as indicated in IT.
                              //   '0'  inverse of condition (else).
                              // Count of instructions in IT block is
                              // 4 - trailingzeroes(mask)
                              // Note that this does not have the same encoding
                              // as in the IT instruction, which also depends
                              // on the low bit of the condition code.

    unsigned CurPosition;     // Current position in parsing of IT
                              // block. In range [0,4], with 0 being the IT
                              // instruction itself. Initialized according to
                              // count of instructions in block.  ~0U if no
                              // active IT block.

    bool IsExplicit;          // true  - The IT instruction was present in the
                              //         input, we should not modify it.
                              // false - The IT instruction was added
                              //         implicitly, we can extend it if that
                              //         would be legal.
  } ITState;

  SmallVector<MCInst, 4> PendingConditionalInsts;

  void flushPendingInstructions(MCStreamer &Out) override {
    if (!inImplicitITBlock()) {
      assert(PendingConditionalInsts.size() == 0);
      return;
    }

    // Emit the IT instruction
    unsigned Mask = getITMaskEncoding();
    MCInst ITInst;
    ITInst.setOpcode(ARM::t2IT);
    ITInst.addOperand(MCOperand::createImm(ITState.Cond));
    ITInst.addOperand(MCOperand::createImm(Mask));
    Out.EmitInstruction(ITInst, getSTI());

    // Emit the conditonal instructions
    assert(PendingConditionalInsts.size() <= 4);
    for (const MCInst &Inst : PendingConditionalInsts) {
      Out.EmitInstruction(Inst, getSTI());
    }
    PendingConditionalInsts.clear();

    // Clear the IT state
    ITState.Mask = 0;
    ITState.CurPosition = ~0U;
  }

  bool inITBlock() { return ITState.CurPosition != ~0U; }
  bool inExplicitITBlock() { return inITBlock() && ITState.IsExplicit; }
  bool inImplicitITBlock() { return inITBlock() && !ITState.IsExplicit; }

  bool lastInITBlock() {
    return ITState.CurPosition == 4 - countTrailingZeros(ITState.Mask);
  }

  void forwardITPosition() {
    if (!inITBlock()) return;
    // Move to the next instruction in the IT block, if there is one. If not,
    // mark the block as done, except for implicit IT blocks, which we leave
    // open until we find an instruction that can't be added to it.
    unsigned TZ = countTrailingZeros(ITState.Mask);
    if (++ITState.CurPosition == 5 - TZ && ITState.IsExplicit)
      ITState.CurPosition = ~0U; // Done with the IT block after this.
  }

  // Rewind the state of the current IT block, removing the last slot from it.
  void rewindImplicitITPosition() {
    assert(inImplicitITBlock());
    assert(ITState.CurPosition > 1);
    ITState.CurPosition--;
    unsigned TZ = countTrailingZeros(ITState.Mask);
    unsigned NewMask = 0;
    NewMask |= ITState.Mask & (0xC << TZ);
    NewMask |= 0x2 << TZ;
    ITState.Mask = NewMask;
  }

  // Rewind the state of the current IT block, removing the last slot from it.
  // If we were at the first slot, this closes the IT block.
  void discardImplicitITBlock() {
    assert(inImplicitITBlock());
    assert(ITState.CurPosition == 1);
    ITState.CurPosition = ~0U;
  }

  // Return the low-subreg of a given Q register.
  unsigned getDRegFromQReg(unsigned QReg) const {
    return MRI->getSubReg(QReg, ARM::dsub_0);
  }

  // Get the encoding of the IT mask, as it will appear in an IT instruction.
  unsigned getITMaskEncoding() {
    assert(inITBlock());
    unsigned Mask = ITState.Mask;
    unsigned TZ = countTrailingZeros(Mask);
    if ((ITState.Cond & 1) == 0) {
      assert(Mask && TZ <= 3 && "illegal IT mask value!");
      Mask ^= (0xE << TZ) & 0xF;
    }
    return Mask;
  }

  // Get the condition code corresponding to the current IT block slot.
  ARMCC::CondCodes currentITCond() {
    unsigned MaskBit;
    if (ITState.CurPosition == 1)
      MaskBit = 1;
    else
      MaskBit = (ITState.Mask >> (5 - ITState.CurPosition)) & 1;

    return MaskBit ? ITState.Cond : ARMCC::getOppositeCondition(ITState.Cond);
  }

  // Invert the condition of the current IT block slot without changing any
  // other slots in the same block.
  void invertCurrentITCondition() {
    if (ITState.CurPosition == 1) {
      ITState.Cond = ARMCC::getOppositeCondition(ITState.Cond);
    } else {
      ITState.Mask ^= 1 << (5 - ITState.CurPosition);
    }
  }

  // Returns true if the current IT block is full (all 4 slots used).
  bool isITBlockFull() {
    return inITBlock() && (ITState.Mask & 1);
  }

  // Extend the current implicit IT block to have one more slot with the given
  // condition code.
  void extendImplicitITBlock(ARMCC::CondCodes Cond) {
    assert(inImplicitITBlock());
    assert(!isITBlockFull());
    assert(Cond == ITState.Cond ||
           Cond == ARMCC::getOppositeCondition(ITState.Cond));
    unsigned TZ = countTrailingZeros(ITState.Mask);
    unsigned NewMask = 0;
    // Keep any existing condition bits.
    NewMask |= ITState.Mask & (0xE << TZ);
    // Insert the new condition bit.
    NewMask |= (Cond == ITState.Cond) << TZ;
    // Move the trailing 1 down one bit.
    NewMask |= 1 << (TZ - 1);
    ITState.Mask = NewMask;
  }

  // Create a new implicit IT block with a dummy condition code.
  void startImplicitITBlock() {
    assert(!inITBlock());
    ITState.Cond = ARMCC::AL;
    ITState.Mask = 8;
    ITState.CurPosition = 1;
    ITState.IsExplicit = false;
  }

  // Create a new explicit IT block with the given condition and mask. The mask
  // should be in the parsed format, with a 1 implying 't', regardless of the
  // low bit of the condition.
  void startExplicitITBlock(ARMCC::CondCodes Cond, unsigned Mask) {
    assert(!inITBlock());
    ITState.Cond = Cond;
    ITState.Mask = Mask;
    ITState.CurPosition = 0;
    ITState.IsExplicit = true;
  }

  void Note(SMLoc L, const Twine &Msg, SMRange Range = None) {
    return getParser().Note(L, Msg, Range);
  }

  bool Warning(SMLoc L, const Twine &Msg, SMRange Range = None) {
    return getParser().Warning(L, Msg, Range);
  }

  bool Error(SMLoc L, const Twine &Msg, SMRange Range = None) {
    return getParser().Error(L, Msg, Range);
  }

  bool validatetLDMRegList(const MCInst &Inst, const OperandVector &Operands,
                           unsigned ListNo, bool IsARPop = false);
  bool validatetSTMRegList(const MCInst &Inst, const OperandVector &Operands,
                           unsigned ListNo);

  int tryParseRegister();
  bool tryParseRegisterWithWriteBack(OperandVector &);
  int tryParseShiftRegister(OperandVector &);
  bool parseRegisterList(OperandVector &);
  bool parseMemory(OperandVector &);
  bool parseOperand(OperandVector &, StringRef Mnemonic);
  bool parsePrefix(ARMMCExpr::VariantKind &RefKind);
  bool parseMemRegOffsetShift(ARM_AM::ShiftOpc &ShiftType,
                              unsigned &ShiftAmount);
  bool parseLiteralValues(unsigned Size, SMLoc L);
  bool parseDirectiveThumb(SMLoc L);
  bool parseDirectiveARM(SMLoc L);
  bool parseDirectiveThumbFunc(SMLoc L);
  bool parseDirectiveCode(SMLoc L);
  bool parseDirectiveSyntax(SMLoc L);
  bool parseDirectiveReq(StringRef Name, SMLoc L);
  bool parseDirectiveUnreq(SMLoc L);
  bool parseDirectiveArch(SMLoc L);
  bool parseDirectiveEabiAttr(SMLoc L);
  bool parseDirectiveCPU(SMLoc L);
  bool parseDirectiveFPU(SMLoc L);
  bool parseDirectiveFnStart(SMLoc L);
  bool parseDirectiveFnEnd(SMLoc L);
  bool parseDirectiveCantUnwind(SMLoc L);
  bool parseDirectivePersonality(SMLoc L);
  bool parseDirectiveHandlerData(SMLoc L);
  bool parseDirectiveSetFP(SMLoc L);
  bool parseDirectivePad(SMLoc L);
  bool parseDirectiveRegSave(SMLoc L, bool IsVector);
  bool parseDirectiveInst(SMLoc L, char Suffix = '\0');
  bool parseDirectiveLtorg(SMLoc L);
  bool parseDirectiveEven(SMLoc L);
  bool parseDirectivePersonalityIndex(SMLoc L);
  bool parseDirectiveUnwindRaw(SMLoc L);
  bool parseDirectiveTLSDescSeq(SMLoc L);
  bool parseDirectiveMovSP(SMLoc L);
  bool parseDirectiveObjectArch(SMLoc L);
  bool parseDirectiveArchExtension(SMLoc L);
  bool parseDirectiveAlign(SMLoc L);
  bool parseDirectiveThumbSet(SMLoc L);

  StringRef splitMnemonic(StringRef Mnemonic, unsigned &PredicationCode,
                          bool &CarrySetting, unsigned &ProcessorIMod,
                          StringRef &ITMask);
  void getMnemonicAcceptInfo(StringRef Mnemonic, StringRef FullInst,
                             bool &CanAcceptCarrySet,
                             bool &CanAcceptPredicationCode);

  void tryConvertingToTwoOperandForm(StringRef Mnemonic, bool CarrySetting,
                                     OperandVector &Operands);
  bool isThumb() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().getFeatureBits()[ARM::ModeThumb];
  }

  bool isThumbOne() const {
    return isThumb() && !getSTI().getFeatureBits()[ARM::FeatureThumb2];
  }

  bool isThumbTwo() const {
    return isThumb() && getSTI().getFeatureBits()[ARM::FeatureThumb2];
  }

  bool hasThumb() const {
    return getSTI().getFeatureBits()[ARM::HasV4TOps];
  }

  bool hasThumb2() const {
    return getSTI().getFeatureBits()[ARM::FeatureThumb2];
  }

  bool hasV6Ops() const {
    return getSTI().getFeatureBits()[ARM::HasV6Ops];
  }

  bool hasV6T2Ops() const {
    return getSTI().getFeatureBits()[ARM::HasV6T2Ops];
  }

  bool hasV6MOps() const {
    return getSTI().getFeatureBits()[ARM::HasV6MOps];
  }

  bool hasV7Ops() const {
    return getSTI().getFeatureBits()[ARM::HasV7Ops];
  }

  bool hasV8Ops() const {
    return getSTI().getFeatureBits()[ARM::HasV8Ops];
  }

  bool hasV8MBaseline() const {
    return getSTI().getFeatureBits()[ARM::HasV8MBaselineOps];
  }

  bool hasV8MMainline() const {
    return getSTI().getFeatureBits()[ARM::HasV8MMainlineOps];
  }

  bool has8MSecExt() const {
    return getSTI().getFeatureBits()[ARM::Feature8MSecExt];
  }

  bool hasARM() const {
    return !getSTI().getFeatureBits()[ARM::FeatureNoARM];
  }

  bool hasDSP() const {
    return getSTI().getFeatureBits()[ARM::FeatureDSP];
  }

  bool hasD16() const {
    return getSTI().getFeatureBits()[ARM::FeatureD16];
  }

  bool hasV8_1aOps() const {
    return getSTI().getFeatureBits()[ARM::HasV8_1aOps];
  }

  bool hasRAS() const {
    return getSTI().getFeatureBits()[ARM::FeatureRAS];
  }

  void SwitchMode() {
    MCSubtargetInfo &STI = copySTI();
    uint64_t FB = ComputeAvailableFeatures(STI.ToggleFeature(ARM::ModeThumb));
    setAvailableFeatures(FB);
  }

  void FixModeAfterArchChange(bool WasThumb, SMLoc Loc);

  bool isMClass() const {
    return getSTI().getFeatureBits()[ARM::FeatureMClass];
  }

  /// @name Auto-generated Match Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "ARMGenAsmMatcher.inc"

  /// }

  OperandMatchResultTy parseITCondCode(OperandVector &);
  OperandMatchResultTy parseCoprocNumOperand(OperandVector &);
  OperandMatchResultTy parseCoprocRegOperand(OperandVector &);
  OperandMatchResultTy parseCoprocOptionOperand(OperandVector &);
  OperandMatchResultTy parseMemBarrierOptOperand(OperandVector &);
  OperandMatchResultTy parseTraceSyncBarrierOptOperand(OperandVector &);
  OperandMatchResultTy parseInstSyncBarrierOptOperand(OperandVector &);
  OperandMatchResultTy parseProcIFlagsOperand(OperandVector &);
  OperandMatchResultTy parseMSRMaskOperand(OperandVector &);
  OperandMatchResultTy parseBankedRegOperand(OperandVector &);
  OperandMatchResultTy parsePKHImm(OperandVector &O, StringRef Op, int Low,
                                   int High);
  OperandMatchResultTy parsePKHLSLImm(OperandVector &O) {
    return parsePKHImm(O, "lsl", 0, 31);
  }
  OperandMatchResultTy parsePKHASRImm(OperandVector &O) {
    return parsePKHImm(O, "asr", 1, 32);
  }
  OperandMatchResultTy parseSetEndImm(OperandVector &);
  OperandMatchResultTy parseShifterImm(OperandVector &);
  OperandMatchResultTy parseRotImm(OperandVector &);
  OperandMatchResultTy parseModImm(OperandVector &);
  OperandMatchResultTy parseBitfield(OperandVector &);
  OperandMatchResultTy parsePostIdxReg(OperandVector &);
  OperandMatchResultTy parseAM3Offset(OperandVector &);
  OperandMatchResultTy parseFPImm(OperandVector &);
  OperandMatchResultTy parseVectorList(OperandVector &);
  OperandMatchResultTy parseVectorLane(VectorLaneTy &LaneKind, unsigned &Index,
                                       SMLoc &EndLoc);

  // Asm Match Converter Methods
  void cvtThumbMultiply(MCInst &Inst, const OperandVector &);
  void cvtThumbBranches(MCInst &Inst, const OperandVector &);

  bool validateInstruction(MCInst &Inst, const OperandVector &Ops);
  bool processInstruction(MCInst &Inst, const OperandVector &Ops, MCStreamer &Out);
  bool shouldOmitCCOutOperand(StringRef Mnemonic, OperandVector &Operands);
  bool shouldOmitPredicateOperand(StringRef Mnemonic, OperandVector &Operands);
  bool isITBlockTerminator(MCInst &Inst) const;
  void fixupGNULDRDAlias(StringRef Mnemonic, OperandVector &Operands);
  bool validateLDRDSTRD(MCInst &Inst, const OperandVector &Operands,
                        bool Load, bool ARMMode, bool Writeback);

public:
  enum ARMMatchResultTy {
    Match_RequiresITBlock = FIRST_TARGET_MATCH_RESULT_TY,
    Match_RequiresNotITBlock,
    Match_RequiresV6,
    Match_RequiresThumb2,
    Match_RequiresV8,
    Match_RequiresFlagSetting,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "ARMGenAsmMatcher.inc"

  };

  ARMAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
               const MCInstrInfo &MII, const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, STI, MII), UC(Parser) {
    MCAsmParserExtension::Initialize(Parser);

    // Cache the MCRegisterInfo.
    MRI = getContext().getRegisterInfo();

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));

    // Add build attributes based on the selected target.
    if (AddBuildAttributes)
      getTargetStreamer().emitTargetAttributes(STI);

    // Not in an ITBlock to start with.
    ITState.CurPosition = ~0U;

    NextSymbolIsThumb = false;
  }

  // Implementation of the MCTargetAsmParser interface:
  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool ParseDirective(AsmToken DirectiveID) override;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;
  unsigned checkTargetMatchPredicate(MCInst &Inst) override;

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  unsigned MatchInstruction(OperandVector &Operands, MCInst &Inst,
                            SmallVectorImpl<NearMissInfo> &NearMisses,
                            bool MatchingInlineAsm, bool &EmitInITBlock,
                            MCStreamer &Out);

  struct NearMissMessage {
    SMLoc Loc;
    SmallString<128> Message;
  };

  const char *getCustomOperandDiag(ARMMatchResultTy MatchError);

  void FilterNearMisses(SmallVectorImpl<NearMissInfo> &NearMissesIn,
                        SmallVectorImpl<NearMissMessage> &NearMissesOut,
                        SMLoc IDLoc, OperandVector &Operands);
  void ReportNearMisses(SmallVectorImpl<NearMissInfo> &NearMisses, SMLoc IDLoc,
                        OperandVector &Operands);

  void doBeforeLabelEmit(MCSymbol *Symbol) override;

  void onLabelParsed(MCSymbol *Symbol) override;
};

/// ARMOperand - Instances of this class represent a parsed ARM machine
/// operand.
class ARMOperand : public MCParsedAsmOperand {
  enum KindTy {
    k_CondCode,
    k_CCOut,
    k_ITCondMask,
    k_CoprocNum,
    k_CoprocReg,
    k_CoprocOption,
    k_Immediate,
    k_MemBarrierOpt,
    k_InstSyncBarrierOpt,
    k_TraceSyncBarrierOpt,
    k_Memory,
    k_PostIndexRegister,
    k_MSRMask,
    k_BankedReg,
    k_ProcIFlags,
    k_VectorIndex,
    k_Register,
    k_RegisterList,
    k_DPRRegisterList,
    k_SPRRegisterList,
    k_VectorList,
    k_VectorListAllLanes,
    k_VectorListIndexed,
    k_ShiftedRegister,
    k_ShiftedImmediate,
    k_ShifterImmediate,
    k_RotateImmediate,
    k_ModifiedImmediate,
    k_ConstantPoolImmediate,
    k_BitfieldDescriptor,
    k_Token,
  } Kind;

  SMLoc StartLoc, EndLoc, AlignmentLoc;
  SmallVector<unsigned, 8> Registers;

  struct CCOp {
    ARMCC::CondCodes Val;
  };

  struct CopOp {
    unsigned Val;
  };

  struct CoprocOptionOp {
    unsigned Val;
  };

  struct ITMaskOp {
    unsigned Mask:4;
  };

  struct MBOptOp {
    ARM_MB::MemBOpt Val;
  };

  struct ISBOptOp {
    ARM_ISB::InstSyncBOpt Val;
  };

  struct TSBOptOp {
    ARM_TSB::TraceSyncBOpt Val;
  };

  struct IFlagsOp {
    ARM_PROC::IFlags Val;
  };

  struct MMaskOp {
    unsigned Val;
  };

  struct BankedRegOp {
    unsigned Val;
  };

  struct TokOp {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNum;
  };

  // A vector register list is a sequential list of 1 to 4 registers.
  struct VectorListOp {
    unsigned RegNum;
    unsigned Count;
    unsigned LaneIndex;
    bool isDoubleSpaced;
  };

  struct VectorIndexOp {
    unsigned Val;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  /// Combined record for all forms of ARM address expressions.
  struct MemoryOp {
    unsigned BaseRegNum;
    // Offset is in OffsetReg or OffsetImm. If both are zero, no offset
    // was specified.
    const MCConstantExpr *OffsetImm;  // Offset immediate value
    unsigned OffsetRegNum;    // Offset register num, when OffsetImm == NULL
    ARM_AM::ShiftOpc ShiftType; // Shift type for OffsetReg
    unsigned ShiftImm;        // shift for OffsetReg.
    unsigned Alignment;       // 0 = no alignment specified
    // n = alignment in bytes (2, 4, 8, 16, or 32)
    unsigned isNegative : 1;  // Negated OffsetReg? (~'U' bit)
  };

  struct PostIdxRegOp {
    unsigned RegNum;
    bool isAdd;
    ARM_AM::ShiftOpc ShiftTy;
    unsigned ShiftImm;
  };

  struct ShifterImmOp {
    bool isASR;
    unsigned Imm;
  };

  struct RegShiftedRegOp {
    ARM_AM::ShiftOpc ShiftTy;
    unsigned SrcReg;
    unsigned ShiftReg;
    unsigned ShiftImm;
  };

  struct RegShiftedImmOp {
    ARM_AM::ShiftOpc ShiftTy;
    unsigned SrcReg;
    unsigned ShiftImm;
  };

  struct RotImmOp {
    unsigned Imm;
  };

  struct ModImmOp {
    unsigned Bits;
    unsigned Rot;
  };

  struct BitfieldOp {
    unsigned LSB;
    unsigned Width;
  };

  union {
    struct CCOp CC;
    struct CopOp Cop;
    struct CoprocOptionOp CoprocOption;
    struct MBOptOp MBOpt;
    struct ISBOptOp ISBOpt;
    struct TSBOptOp TSBOpt;
    struct ITMaskOp ITMask;
    struct IFlagsOp IFlags;
    struct MMaskOp MMask;
    struct BankedRegOp BankedReg;
    struct TokOp Tok;
    struct RegOp Reg;
    struct VectorListOp VectorList;
    struct VectorIndexOp VectorIndex;
    struct ImmOp Imm;
    struct MemoryOp Memory;
    struct PostIdxRegOp PostIdxReg;
    struct ShifterImmOp ShifterImm;
    struct RegShiftedRegOp RegShiftedReg;
    struct RegShiftedImmOp RegShiftedImm;
    struct RotImmOp RotImm;
    struct ModImmOp ModImm;
    struct BitfieldOp Bitfield;
  };

public:
  ARMOperand(KindTy K) : MCParsedAsmOperand(), Kind(K) {}

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }

  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  /// getLocRange - Get the range between the first and last token of this
  /// operand.
  SMRange getLocRange() const { return SMRange(StartLoc, EndLoc); }

  /// getAlignmentLoc - Get the location of the Alignment token of this operand.
  SMLoc getAlignmentLoc() const {
    assert(Kind == k_Memory && "Invalid access!");
    return AlignmentLoc;
  }

  ARMCC::CondCodes getCondCode() const {
    assert(Kind == k_CondCode && "Invalid access!");
    return CC.Val;
  }

  unsigned getCoproc() const {
    assert((Kind == k_CoprocNum || Kind == k_CoprocReg) && "Invalid access!");
    return Cop.Val;
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  unsigned getReg() const override {
    assert((Kind == k_Register || Kind == k_CCOut) && "Invalid access!");
    return Reg.RegNum;
  }

  const SmallVectorImpl<unsigned> &getRegList() const {
    assert((Kind == k_RegisterList || Kind == k_DPRRegisterList ||
            Kind == k_SPRRegisterList) && "Invalid access!");
    return Registers;
  }

  const MCExpr *getImm() const {
    assert(isImm() && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getConstantPoolImm() const {
    assert(isConstantPoolImm() && "Invalid access!");
    return Imm.Val;
  }

  unsigned getVectorIndex() const {
    assert(Kind == k_VectorIndex && "Invalid access!");
    return VectorIndex.Val;
  }

  ARM_MB::MemBOpt getMemBarrierOpt() const {
    assert(Kind == k_MemBarrierOpt && "Invalid access!");
    return MBOpt.Val;
  }

  ARM_ISB::InstSyncBOpt getInstSyncBarrierOpt() const {
    assert(Kind == k_InstSyncBarrierOpt && "Invalid access!");
    return ISBOpt.Val;
  }

  ARM_TSB::TraceSyncBOpt getTraceSyncBarrierOpt() const {
    assert(Kind == k_TraceSyncBarrierOpt && "Invalid access!");
    return TSBOpt.Val;
  }

  ARM_PROC::IFlags getProcIFlags() const {
    assert(Kind == k_ProcIFlags && "Invalid access!");
    return IFlags.Val;
  }

  unsigned getMSRMask() const {
    assert(Kind == k_MSRMask && "Invalid access!");
    return MMask.Val;
  }

  unsigned getBankedReg() const {
    assert(Kind == k_BankedReg && "Invalid access!");
    return BankedReg.Val;
  }

  bool isCoprocNum() const { return Kind == k_CoprocNum; }
  bool isCoprocReg() const { return Kind == k_CoprocReg; }
  bool isCoprocOption() const { return Kind == k_CoprocOption; }
  bool isCondCode() const { return Kind == k_CondCode; }
  bool isCCOut() const { return Kind == k_CCOut; }
  bool isITMask() const { return Kind == k_ITCondMask; }
  bool isITCondCode() const { return Kind == k_CondCode; }
  bool isImm() const override {
    return Kind == k_Immediate;
  }

  bool isARMBranchTarget() const {
    if (!isImm()) return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm()))
      return CE->getValue() % 4 == 0;
    return true;
  }


  bool isThumbBranchTarget() const {
    if (!isImm()) return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm()))
      return CE->getValue() % 2 == 0;
    return true;
  }

  // checks whether this operand is an unsigned offset which fits is a field
  // of specified width and scaled by a specific number of bits
  template<unsigned width, unsigned scale>
  bool isUnsignedOffset() const {
    if (!isImm()) return false;
    if (isa<MCSymbolRefExpr>(Imm.Val)) return true;
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Align = 1LL << scale;
      int64_t Max = Align * ((1LL << width) - 1);
      return ((Val % Align) == 0) && (Val >= 0) && (Val <= Max);
    }
    return false;
  }

  // checks whether this operand is an signed offset which fits is a field
  // of specified width and scaled by a specific number of bits
  template<unsigned width, unsigned scale>
  bool isSignedOffset() const {
    if (!isImm()) return false;
    if (isa<MCSymbolRefExpr>(Imm.Val)) return true;
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Align = 1LL << scale;
      int64_t Max = Align * ((1LL << (width-1)) - 1);
      int64_t Min = -Align * (1LL << (width-1));
      return ((Val % Align) == 0) && (Val >= Min) && (Val <= Max);
    }
    return false;
  }

  // checks whether this operand is a memory operand computed as an offset
  // applied to PC. the offset may have 8 bits of magnitude and is represented
  // with two bits of shift. textually it may be either [pc, #imm], #imm or
  // relocable expression...
  bool isThumbMemPC() const {
    int64_t Val = 0;
    if (isImm()) {
      if (isa<MCSymbolRefExpr>(Imm.Val)) return true;
      const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val);
      if (!CE) return false;
      Val = CE->getValue();
    }
    else if (isMem()) {
      if(!Memory.OffsetImm || Memory.OffsetRegNum) return false;
      if(Memory.BaseRegNum != ARM::PC) return false;
      Val = Memory.OffsetImm->getValue();
    }
    else return false;
    return ((Val % 4) == 0) && (Val >= 0) && (Val <= 1020);
  }

  bool isFPImm() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int Val = ARM_AM::getFP32Imm(APInt(32, CE->getValue()));
    return Val != -1;
  }

  template<int64_t N, int64_t M>
  bool isImmediate() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return Value >= N && Value <= M;
  }

  template<int64_t N, int64_t M>
  bool isImmediateS4() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return ((Value & 3) == 0) && Value >= N && Value <= M;
  }

  bool isFBits16() const {
    return isImmediate<0, 17>();
  }
  bool isFBits32() const {
    return isImmediate<1, 33>();
  }
  bool isImm8s4() const {
    return isImmediateS4<-1020, 1020>();
  }
  bool isImm0_1020s4() const {
    return isImmediateS4<0, 1020>();
  }
  bool isImm0_508s4() const {
    return isImmediateS4<0, 508>();
  }
  bool isImm0_508s4Neg() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = -CE->getValue();
    // explicitly exclude zero. we want that to use the normal 0_508 version.
    return ((Value & 3) == 0) && Value > 0 && Value <= 508;
  }

  bool isImm0_4095Neg() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    // isImm0_4095Neg is used with 32-bit immediates only.
    // 32-bit immediates are zero extended to 64-bit when parsed,
    // thus simple -CE->getValue() results in a big negative number,
    // not a small positive number as intended
    if ((CE->getValue() >> 32) > 0) return false;
    uint32_t Value = -static_cast<uint32_t>(CE->getValue());
    return Value > 0 && Value < 4096;
  }

  bool isImm0_7() const {
    return isImmediate<0, 7>();
  }

  bool isImm1_16() const {
    return isImmediate<1, 16>();
  }

  bool isImm1_32() const {
    return isImmediate<1, 32>();
  }

  bool isImm8_255() const {
    return isImmediate<8, 255>();
  }

  bool isImm256_65535Expr() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // If it's not a constant expression, it'll generate a fixup and be
    // handled later.
    if (!CE) return true;
    int64_t Value = CE->getValue();
    return Value >= 256 && Value < 65536;
  }

  bool isImm0_65535Expr() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // If it's not a constant expression, it'll generate a fixup and be
    // handled later.
    if (!CE) return true;
    int64_t Value = CE->getValue();
    return Value >= 0 && Value < 65536;
  }

  bool isImm24bit() const {
    return isImmediate<0, 0xffffff + 1>();
  }

  bool isImmThumbSR() const {
    return isImmediate<1, 33>();
  }

  bool isPKHLSLImm() const {
    return isImmediate<0, 32>();
  }

  bool isPKHASRImm() const {
    return isImmediate<0, 33>();
  }

  bool isAdrLabel() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup.
    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;

    // If it is a constant, it must fit into a modified immediate encoding.
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return (ARM_AM::getSOImmVal(Value) != -1 ||
            ARM_AM::getSOImmVal(-Value) != -1);
  }

  bool isT2SOImm() const {
    // If we have an immediate that's not a constant, treat it as an expression
    // needing a fixup.
    if (isImm() && !isa<MCConstantExpr>(getImm())) {
      // We want to avoid matching :upper16: and :lower16: as we want these
      // expressions to match in isImm0_65535Expr()
      const ARMMCExpr *ARM16Expr = dyn_cast<ARMMCExpr>(getImm());
      return (!ARM16Expr || (ARM16Expr->getKind() != ARMMCExpr::VK_ARM_HI16 &&
                             ARM16Expr->getKind() != ARMMCExpr::VK_ARM_LO16));
    }
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return ARM_AM::getT2SOImmVal(Value) != -1;
  }

  bool isT2SOImmNot() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return ARM_AM::getT2SOImmVal(Value) == -1 &&
      ARM_AM::getT2SOImmVal(~Value) != -1;
  }

  bool isT2SOImmNeg() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    // Only use this when not representable as a plain so_imm.
    return ARM_AM::getT2SOImmVal(Value) == -1 &&
      ARM_AM::getT2SOImmVal(-Value) != -1;
  }

  bool isSetEndImm() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return Value == 1 || Value == 0;
  }

  bool isReg() const override { return Kind == k_Register; }
  bool isRegList() const { return Kind == k_RegisterList; }
  bool isDPRRegList() const { return Kind == k_DPRRegisterList; }
  bool isSPRRegList() const { return Kind == k_SPRRegisterList; }
  bool isToken() const override { return Kind == k_Token; }
  bool isMemBarrierOpt() const { return Kind == k_MemBarrierOpt; }
  bool isInstSyncBarrierOpt() const { return Kind == k_InstSyncBarrierOpt; }
  bool isTraceSyncBarrierOpt() const { return Kind == k_TraceSyncBarrierOpt; }
  bool isMem() const override {
    if (Kind != k_Memory)
      return false;
    if (Memory.BaseRegNum &&
        !ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Memory.BaseRegNum))
      return false;
    if (Memory.OffsetRegNum &&
        !ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Memory.OffsetRegNum))
      return false;
    return true;
  }
  bool isShifterImm() const { return Kind == k_ShifterImmediate; }
  bool isRegShiftedReg() const {
    return Kind == k_ShiftedRegister &&
           ARMMCRegisterClasses[ARM::GPRRegClassID].contains(
               RegShiftedReg.SrcReg) &&
           ARMMCRegisterClasses[ARM::GPRRegClassID].contains(
               RegShiftedReg.ShiftReg);
  }
  bool isRegShiftedImm() const {
    return Kind == k_ShiftedImmediate &&
           ARMMCRegisterClasses[ARM::GPRRegClassID].contains(
               RegShiftedImm.SrcReg);
  }
  bool isRotImm() const { return Kind == k_RotateImmediate; }
  bool isModImm() const { return Kind == k_ModifiedImmediate; }

  bool isModImmNot() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return ARM_AM::getSOImmVal(~Value) != -1;
  }

  bool isModImmNeg() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return ARM_AM::getSOImmVal(Value) == -1 &&
      ARM_AM::getSOImmVal(-Value) != -1;
  }

  bool isThumbModImmNeg1_7() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int32_t Value = -(int32_t)CE->getValue();
    return 0 < Value && Value < 8;
  }

  bool isThumbModImmNeg8_255() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int32_t Value = -(int32_t)CE->getValue();
    return 7 < Value && Value < 256;
  }

  bool isConstantPoolImm() const { return Kind == k_ConstantPoolImmediate; }
  bool isBitfield() const { return Kind == k_BitfieldDescriptor; }
  bool isPostIdxRegShifted() const {
    return Kind == k_PostIndexRegister &&
           ARMMCRegisterClasses[ARM::GPRRegClassID].contains(PostIdxReg.RegNum);
  }
  bool isPostIdxReg() const {
    return isPostIdxRegShifted() && PostIdxReg.ShiftTy == ARM_AM::no_shift;
  }
  bool isMemNoOffset(bool alignOK = false, unsigned Alignment = 0) const {
    if (!isMem())
      return false;
    // No offset of any kind.
    return Memory.OffsetRegNum == 0 && Memory.OffsetImm == nullptr &&
     (alignOK || Memory.Alignment == Alignment);
  }
  bool isMemPCRelImm12() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Base register must be PC.
    if (Memory.BaseRegNum != ARM::PC)
      return false;
    // Immediate offset in range [-4095, 4095].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val > -4096 && Val < 4096) ||
           (Val == std::numeric_limits<int32_t>::min());
  }

  bool isAlignedMemory() const {
    return isMemNoOffset(true);
  }

  bool isAlignedMemoryNone() const {
    return isMemNoOffset(false, 0);
  }

  bool isDupAlignedMemoryNone() const {
    return isMemNoOffset(false, 0);
  }

  bool isAlignedMemory16() const {
    if (isMemNoOffset(false, 2)) // alignment in bytes for 16-bits is 2.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isDupAlignedMemory16() const {
    if (isMemNoOffset(false, 2)) // alignment in bytes for 16-bits is 2.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isAlignedMemory32() const {
    if (isMemNoOffset(false, 4)) // alignment in bytes for 32-bits is 4.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isDupAlignedMemory32() const {
    if (isMemNoOffset(false, 4)) // alignment in bytes for 32-bits is 4.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isAlignedMemory64() const {
    if (isMemNoOffset(false, 8)) // alignment in bytes for 64-bits is 8.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isDupAlignedMemory64() const {
    if (isMemNoOffset(false, 8)) // alignment in bytes for 64-bits is 8.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isAlignedMemory64or128() const {
    if (isMemNoOffset(false, 8)) // alignment in bytes for 64-bits is 8.
      return true;
    if (isMemNoOffset(false, 16)) // alignment in bytes for 128-bits is 16.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isDupAlignedMemory64or128() const {
    if (isMemNoOffset(false, 8)) // alignment in bytes for 64-bits is 8.
      return true;
    if (isMemNoOffset(false, 16)) // alignment in bytes for 128-bits is 16.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isAlignedMemory64or128or256() const {
    if (isMemNoOffset(false, 8)) // alignment in bytes for 64-bits is 8.
      return true;
    if (isMemNoOffset(false, 16)) // alignment in bytes for 128-bits is 16.
      return true;
    if (isMemNoOffset(false, 32)) // alignment in bytes for 256-bits is 32.
      return true;
    return isMemNoOffset(false, 0);
  }

  bool isAddrMode2() const {
    if (!isMem() || Memory.Alignment != 0) return false;
    // Check for register offset.
    if (Memory.OffsetRegNum) return true;
    // Immediate offset in range [-4095, 4095].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val > -4096 && Val < 4096;
  }

  bool isAM2OffsetImm() const {
    if (!isImm()) return false;
    // Immediate offset in range [-4095, 4095].
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Val = CE->getValue();
    return (Val == std::numeric_limits<int32_t>::min()) ||
           (Val > -4096 && Val < 4096);
  }

  bool isAddrMode3() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;
    if (!isMem() || Memory.Alignment != 0) return false;
    // No shifts are legal for AM3.
    if (Memory.ShiftType != ARM_AM::no_shift) return false;
    // Check for register offset.
    if (Memory.OffsetRegNum) return true;
    // Immediate offset in range [-255, 255].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    // The #-0 offset is encoded as std::numeric_limits<int32_t>::min(), and we
    // have to check for this too.
    return (Val > -256 && Val < 256) ||
           Val == std::numeric_limits<int32_t>::min();
  }

  bool isAM3Offset() const {
    if (isPostIdxReg())
      return true;
    if (!isImm())
      return false;
    // Immediate offset in range [-255, 255].
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Val = CE->getValue();
    // Special case, #-0 is std::numeric_limits<int32_t>::min().
    return (Val > -256 && Val < 256) ||
           Val == std::numeric_limits<int32_t>::min();
  }

  bool isAddrMode5() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;
    if (!isMem() || Memory.Alignment != 0) return false;
    // Check for register offset.
    if (Memory.OffsetRegNum) return false;
    // Immediate offset in range [-1020, 1020] and a multiple of 4.
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val >= -1020 && Val <= 1020 && ((Val & 3) == 0)) ||
      Val == std::numeric_limits<int32_t>::min();
  }

  bool isAddrMode5FP16() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;
    if (!isMem() || Memory.Alignment != 0) return false;
    // Check for register offset.
    if (Memory.OffsetRegNum) return false;
    // Immediate offset in range [-510, 510] and a multiple of 2.
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val >= -510 && Val <= 510 && ((Val & 1) == 0)) ||
           Val == std::numeric_limits<int32_t>::min();
  }

  bool isMemTBB() const {
    if (!isMem() || !Memory.OffsetRegNum || Memory.isNegative ||
        Memory.ShiftType != ARM_AM::no_shift || Memory.Alignment != 0)
      return false;
    return true;
  }

  bool isMemTBH() const {
    if (!isMem() || !Memory.OffsetRegNum || Memory.isNegative ||
        Memory.ShiftType != ARM_AM::lsl || Memory.ShiftImm != 1 ||
        Memory.Alignment != 0 )
      return false;
    return true;
  }

  bool isMemRegOffset() const {
    if (!isMem() || !Memory.OffsetRegNum || Memory.Alignment != 0)
      return false;
    return true;
  }

  bool isT2MemRegOffset() const {
    if (!isMem() || !Memory.OffsetRegNum || Memory.isNegative ||
        Memory.Alignment != 0 || Memory.BaseRegNum == ARM::PC)
      return false;
    // Only lsl #{0, 1, 2, 3} allowed.
    if (Memory.ShiftType == ARM_AM::no_shift)
      return true;
    if (Memory.ShiftType != ARM_AM::lsl || Memory.ShiftImm > 3)
      return false;
    return true;
  }

  bool isMemThumbRR() const {
    // Thumb reg+reg addressing is simple. Just two registers, a base and
    // an offset. No shifts, negations or any other complicating factors.
    if (!isMem() || !Memory.OffsetRegNum || Memory.isNegative ||
        Memory.ShiftType != ARM_AM::no_shift || Memory.Alignment != 0)
      return false;
    return isARMLowRegister(Memory.BaseRegNum) &&
      (!Memory.OffsetRegNum || isARMLowRegister(Memory.OffsetRegNum));
  }

  bool isMemThumbRIs4() const {
    if (!isMem() || Memory.OffsetRegNum != 0 ||
        !isARMLowRegister(Memory.BaseRegNum) || Memory.Alignment != 0)
      return false;
    // Immediate offset, multiple of 4 in range [0, 124].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val <= 124 && (Val % 4) == 0;
  }

  bool isMemThumbRIs2() const {
    if (!isMem() || Memory.OffsetRegNum != 0 ||
        !isARMLowRegister(Memory.BaseRegNum) || Memory.Alignment != 0)
      return false;
    // Immediate offset, multiple of 4 in range [0, 62].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val <= 62 && (Val % 2) == 0;
  }

  bool isMemThumbRIs1() const {
    if (!isMem() || Memory.OffsetRegNum != 0 ||
        !isARMLowRegister(Memory.BaseRegNum) || Memory.Alignment != 0)
      return false;
    // Immediate offset in range [0, 31].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val <= 31;
  }

  bool isMemThumbSPI() const {
    if (!isMem() || Memory.OffsetRegNum != 0 ||
        Memory.BaseRegNum != ARM::SP || Memory.Alignment != 0)
      return false;
    // Immediate offset, multiple of 4 in range [0, 1020].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val <= 1020 && (Val % 4) == 0;
  }

  bool isMemImm8s4Offset() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Immediate offset a multiple of 4 in range [-1020, 1020].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    // Special case, #-0 is std::numeric_limits<int32_t>::min().
    return (Val >= -1020 && Val <= 1020 && (Val & 3) == 0) ||
           Val == std::numeric_limits<int32_t>::min();
  }

  bool isMemImm0_1020s4Offset() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Immediate offset a multiple of 4 in range [0, 1020].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val <= 1020 && (Val & 3) == 0;
  }

  bool isMemImm8Offset() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Base reg of PC isn't allowed for these encodings.
    if (Memory.BaseRegNum == ARM::PC) return false;
    // Immediate offset in range [-255, 255].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val == std::numeric_limits<int32_t>::min()) ||
           (Val > -256 && Val < 256);
  }

  bool isMemPosImm8Offset() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Immediate offset in range [0, 255].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return Val >= 0 && Val < 256;
  }

  bool isMemNegImm8Offset() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Base reg of PC isn't allowed for these encodings.
    if (Memory.BaseRegNum == ARM::PC) return false;
    // Immediate offset in range [-255, -1].
    if (!Memory.OffsetImm) return false;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val == std::numeric_limits<int32_t>::min()) ||
           (Val > -256 && Val < 0);
  }

  bool isMemUImm12Offset() const {
    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Immediate offset in range [0, 4095].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val >= 0 && Val < 4096);
  }

  bool isMemImm12Offset() const {
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.

    if (isImm() && !isa<MCConstantExpr>(getImm()))
      return true;

    if (!isMem() || Memory.OffsetRegNum != 0 || Memory.Alignment != 0)
      return false;
    // Immediate offset in range [-4095, 4095].
    if (!Memory.OffsetImm) return true;
    int64_t Val = Memory.OffsetImm->getValue();
    return (Val > -4096 && Val < 4096) ||
           (Val == std::numeric_limits<int32_t>::min());
  }

  bool isConstPoolAsmImm() const {
    // Delay processing of Constant Pool Immediate, this will turn into
    // a constant. Match no other operand
    return (isConstantPoolImm());
  }

  bool isPostIdxImm8() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Val = CE->getValue();
    return (Val > -256 && Val < 256) ||
           (Val == std::numeric_limits<int32_t>::min());
  }

  bool isPostIdxImm8s4() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Val = CE->getValue();
    return ((Val & 3) == 0 && Val >= -1020 && Val <= 1020) ||
           (Val == std::numeric_limits<int32_t>::min());
  }

  bool isMSRMask() const { return Kind == k_MSRMask; }
  bool isBankedReg() const { return Kind == k_BankedReg; }
  bool isProcIFlags() const { return Kind == k_ProcIFlags; }

  // NEON operands.
  bool isSingleSpacedVectorList() const {
    return Kind == k_VectorList && !VectorList.isDoubleSpaced;
  }

  bool isDoubleSpacedVectorList() const {
    return Kind == k_VectorList && VectorList.isDoubleSpaced;
  }

  bool isVecListOneD() const {
    if (!isSingleSpacedVectorList()) return false;
    return VectorList.Count == 1;
  }

  bool isVecListDPair() const {
    if (!isSingleSpacedVectorList()) return false;
    return (ARMMCRegisterClasses[ARM::DPairRegClassID]
              .contains(VectorList.RegNum));
  }

  bool isVecListThreeD() const {
    if (!isSingleSpacedVectorList()) return false;
    return VectorList.Count == 3;
  }

  bool isVecListFourD() const {
    if (!isSingleSpacedVectorList()) return false;
    return VectorList.Count == 4;
  }

  bool isVecListDPairSpaced() const {
    if (Kind != k_VectorList) return false;
    if (isSingleSpacedVectorList()) return false;
    return (ARMMCRegisterClasses[ARM::DPairSpcRegClassID]
              .contains(VectorList.RegNum));
  }

  bool isVecListThreeQ() const {
    if (!isDoubleSpacedVectorList()) return false;
    return VectorList.Count == 3;
  }

  bool isVecListFourQ() const {
    if (!isDoubleSpacedVectorList()) return false;
    return VectorList.Count == 4;
  }

  bool isSingleSpacedVectorAllLanes() const {
    return Kind == k_VectorListAllLanes && !VectorList.isDoubleSpaced;
  }

  bool isDoubleSpacedVectorAllLanes() const {
    return Kind == k_VectorListAllLanes && VectorList.isDoubleSpaced;
  }

  bool isVecListOneDAllLanes() const {
    if (!isSingleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 1;
  }

  bool isVecListDPairAllLanes() const {
    if (!isSingleSpacedVectorAllLanes()) return false;
    return (ARMMCRegisterClasses[ARM::DPairRegClassID]
              .contains(VectorList.RegNum));
  }

  bool isVecListDPairSpacedAllLanes() const {
    if (!isDoubleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 2;
  }

  bool isVecListThreeDAllLanes() const {
    if (!isSingleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 3;
  }

  bool isVecListThreeQAllLanes() const {
    if (!isDoubleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 3;
  }

  bool isVecListFourDAllLanes() const {
    if (!isSingleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 4;
  }

  bool isVecListFourQAllLanes() const {
    if (!isDoubleSpacedVectorAllLanes()) return false;
    return VectorList.Count == 4;
  }

  bool isSingleSpacedVectorIndexed() const {
    return Kind == k_VectorListIndexed && !VectorList.isDoubleSpaced;
  }

  bool isDoubleSpacedVectorIndexed() const {
    return Kind == k_VectorListIndexed && VectorList.isDoubleSpaced;
  }

  bool isVecListOneDByteIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 1 && VectorList.LaneIndex <= 7;
  }

  bool isVecListOneDHWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 1 && VectorList.LaneIndex <= 3;
  }

  bool isVecListOneDWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 1 && VectorList.LaneIndex <= 1;
  }

  bool isVecListTwoDByteIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 2 && VectorList.LaneIndex <= 7;
  }

  bool isVecListTwoDHWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 2 && VectorList.LaneIndex <= 3;
  }

  bool isVecListTwoQWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 2 && VectorList.LaneIndex <= 1;
  }

  bool isVecListTwoQHWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 2 && VectorList.LaneIndex <= 3;
  }

  bool isVecListTwoDWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 2 && VectorList.LaneIndex <= 1;
  }

  bool isVecListThreeDByteIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 3 && VectorList.LaneIndex <= 7;
  }

  bool isVecListThreeDHWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 3 && VectorList.LaneIndex <= 3;
  }

  bool isVecListThreeQWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 3 && VectorList.LaneIndex <= 1;
  }

  bool isVecListThreeQHWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 3 && VectorList.LaneIndex <= 3;
  }

  bool isVecListThreeDWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 3 && VectorList.LaneIndex <= 1;
  }

  bool isVecListFourDByteIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 4 && VectorList.LaneIndex <= 7;
  }

  bool isVecListFourDHWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 4 && VectorList.LaneIndex <= 3;
  }

  bool isVecListFourQWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 4 && VectorList.LaneIndex <= 1;
  }

  bool isVecListFourQHWordIndexed() const {
    if (!isDoubleSpacedVectorIndexed()) return false;
    return VectorList.Count == 4 && VectorList.LaneIndex <= 3;
  }

  bool isVecListFourDWordIndexed() const {
    if (!isSingleSpacedVectorIndexed()) return false;
    return VectorList.Count == 4 && VectorList.LaneIndex <= 1;
  }

  bool isVectorIndex8() const {
    if (Kind != k_VectorIndex) return false;
    return VectorIndex.Val < 8;
  }

  bool isVectorIndex16() const {
    if (Kind != k_VectorIndex) return false;
    return VectorIndex.Val < 4;
  }

  bool isVectorIndex32() const {
    if (Kind != k_VectorIndex) return false;
    return VectorIndex.Val < 2;
  }
  bool isVectorIndex64() const {
    if (Kind != k_VectorIndex) return false;
    return VectorIndex.Val < 1;
  }

  bool isNEONi8splat() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    int64_t Value = CE->getValue();
    // i8 value splatted across 8 bytes. The immediate is just the 8 byte
    // value.
    return Value >= 0 && Value < 256;
  }

  bool isNEONi16splat() const {
    if (isNEONByteReplicate(2))
      return false; // Leave that for bytes replication and forbid by default.
    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    unsigned Value = CE->getValue();
    return ARM_AM::isNEONi16splat(Value);
  }

  bool isNEONi16splatNot() const {
    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    unsigned Value = CE->getValue();
    return ARM_AM::isNEONi16splat(~Value & 0xffff);
  }

  bool isNEONi32splat() const {
    if (isNEONByteReplicate(4))
      return false; // Leave that for bytes replication and forbid by default.
    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    unsigned Value = CE->getValue();
    return ARM_AM::isNEONi32splat(Value);
  }

  bool isNEONi32splatNot() const {
    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    unsigned Value = CE->getValue();
    return ARM_AM::isNEONi32splat(~Value);
  }

  static bool isValidNEONi32vmovImm(int64_t Value) {
    // i32 value with set bits only in one byte X000, 0X00, 00X0, or 000X,
    // for VMOV/VMVN only, 00Xf or 0Xff are also accepted.
    return ((Value & 0xffffffffffffff00) == 0) ||
           ((Value & 0xffffffffffff00ff) == 0) ||
           ((Value & 0xffffffffff00ffff) == 0) ||
           ((Value & 0xffffffff00ffffff) == 0) ||
           ((Value & 0xffffffffffff00ff) == 0xff) ||
           ((Value & 0xffffffffff00ffff) == 0xffff);
  }

  bool isNEONReplicate(unsigned Width, unsigned NumElems, bool Inv) const {
    assert((Width == 8 || Width == 16 || Width == 32) &&
           "Invalid element width");
    assert(NumElems * Width <= 64 && "Invalid result width");

    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE)
      return false;
    int64_t Value = CE->getValue();
    if (!Value)
      return false; // Don't bother with zero.
    if (Inv)
      Value = ~Value;

    uint64_t Mask = (1ull << Width) - 1;
    uint64_t Elem = Value & Mask;
    if (Width == 16 && (Elem & 0x00ff) != 0 && (Elem & 0xff00) != 0)
      return false;
    if (Width == 32 && !isValidNEONi32vmovImm(Elem))
      return false;

    for (unsigned i = 1; i < NumElems; ++i) {
      Value >>= Width;
      if ((Value & Mask) != Elem)
        return false;
    }
    return true;
  }

  bool isNEONByteReplicate(unsigned NumBytes) const {
    return isNEONReplicate(8, NumBytes, false);
  }

  static void checkNeonReplicateArgs(unsigned FromW, unsigned ToW) {
    assert((FromW == 8 || FromW == 16 || FromW == 32) &&
           "Invalid source width");
    assert((ToW == 16 || ToW == 32 || ToW == 64) &&
           "Invalid destination width");
    assert(FromW < ToW && "ToW is not less than FromW");
  }

  template<unsigned FromW, unsigned ToW>
  bool isNEONmovReplicate() const {
    checkNeonReplicateArgs(FromW, ToW);
    if (ToW == 64 && isNEONi64splat())
      return false;
    return isNEONReplicate(FromW, ToW / FromW, false);
  }

  template<unsigned FromW, unsigned ToW>
  bool isNEONinvReplicate() const {
    checkNeonReplicateArgs(FromW, ToW);
    return isNEONReplicate(FromW, ToW / FromW, true);
  }

  bool isNEONi32vmov() const {
    if (isNEONByteReplicate(4))
      return false; // Let it to be classified as byte-replicate case.
    if (!isImm())
      return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE)
      return false;
    return isValidNEONi32vmovImm(CE->getValue());
  }

  bool isNEONi32vmovNeg() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    return isValidNEONi32vmovImm(~CE->getValue());
  }

  bool isNEONi64splat() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    // Must be a constant.
    if (!CE) return false;
    uint64_t Value = CE->getValue();
    // i64 value with each byte being either 0 or 0xff.
    for (unsigned i = 0; i < 8; ++i, Value >>= 8)
      if ((Value & 0xff) != 0 && (Value & 0xff) != 0xff) return false;
    return true;
  }

  template<int64_t Angle, int64_t Remainder>
  bool isComplexRotation() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    return (Value % Angle == Remainder && Value <= 270);
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addARMBranchTargetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addThumbBranchTargetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addCondCodeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getCondCode())));
    unsigned RegNum = getCondCode() == ARMCC::AL ? 0: ARM::CPSR;
    Inst.addOperand(MCOperand::createReg(RegNum));
  }

  void addCoprocNumOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getCoproc()));
  }

  void addCoprocRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getCoproc()));
  }

  void addCoprocOptionOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(CoprocOption.Val));
  }

  void addITMaskOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(ITMask.Mask));
  }

  void addITCondCodeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getCondCode())));
  }

  void addCCOutOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addRegShiftedRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands!");
    assert(isRegShiftedReg() &&
           "addRegShiftedRegOperands() on non-RegShiftedReg!");
    Inst.addOperand(MCOperand::createReg(RegShiftedReg.SrcReg));
    Inst.addOperand(MCOperand::createReg(RegShiftedReg.ShiftReg));
    Inst.addOperand(MCOperand::createImm(
      ARM_AM::getSORegOpc(RegShiftedReg.ShiftTy, RegShiftedReg.ShiftImm)));
  }

  void addRegShiftedImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    assert(isRegShiftedImm() &&
           "addRegShiftedImmOperands() on non-RegShiftedImm!");
    Inst.addOperand(MCOperand::createReg(RegShiftedImm.SrcReg));
    // Shift of #32 is encoded as 0 where permitted
    unsigned Imm = (RegShiftedImm.ShiftImm == 32 ? 0 : RegShiftedImm.ShiftImm);
    Inst.addOperand(MCOperand::createImm(
      ARM_AM::getSORegOpc(RegShiftedImm.ShiftTy, Imm)));
  }

  void addShifterImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm((ShifterImm.isASR << 5) |
                                         ShifterImm.Imm));
  }

  void addRegListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const SmallVectorImpl<unsigned> &RegList = getRegList();
    for (SmallVectorImpl<unsigned>::const_iterator
           I = RegList.begin(), E = RegList.end(); I != E; ++I)
      Inst.addOperand(MCOperand::createReg(*I));
  }

  void addDPRRegListOperands(MCInst &Inst, unsigned N) const {
    addRegListOperands(Inst, N);
  }

  void addSPRRegListOperands(MCInst &Inst, unsigned N) const {
    addRegListOperands(Inst, N);
  }

  void addRotImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // Encoded as val>>3. The printer handles display as 8, 16, 24.
    Inst.addOperand(MCOperand::createImm(RotImm.Imm >> 3));
  }

  void addModImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    // Support for fixups (MCFixup)
    if (isImm())
      return addImmOperands(Inst, N);

    Inst.addOperand(MCOperand::createImm(ModImm.Bits | (ModImm.Rot << 7)));
  }

  void addModImmNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    uint32_t Enc = ARM_AM::getSOImmVal(~CE->getValue());
    Inst.addOperand(MCOperand::createImm(Enc));
  }

  void addModImmNegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    uint32_t Enc = ARM_AM::getSOImmVal(-CE->getValue());
    Inst.addOperand(MCOperand::createImm(Enc));
  }

  void addThumbModImmNeg8_255Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    uint32_t Val = -CE->getValue();
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addThumbModImmNeg1_7Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    uint32_t Val = -CE->getValue();
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addBitfieldOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // Munge the lsb/width into a bitfield mask.
    unsigned lsb = Bitfield.LSB;
    unsigned width = Bitfield.Width;
    // Make a 32-bit mask w/ the referenced bits clear and all other bits set.
    uint32_t Mask = ~(((uint32_t)0xffffffff >> lsb) << (32 - width) >>
                      (32 - (lsb + width)));
    Inst.addOperand(MCOperand::createImm(Mask));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addFBits16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(16 - CE->getValue()));
  }

  void addFBits32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(32 - CE->getValue()));
  }

  void addFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    int Val = ARM_AM::getFP32Imm(APInt(32, CE->getValue()));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addImm8s4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // FIXME: We really want to scale the value here, but the LDRD/STRD
    // instruction don't encode operands that way yet.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue()));
  }

  void addImm0_1020s4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate is scaled by four in the encoding and is stored
    // in the MCInst as such. Lop off the low two bits here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() / 4));
  }

  void addImm0_508s4NegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate is scaled by four in the encoding and is stored
    // in the MCInst as such. Lop off the low two bits here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(-(CE->getValue() / 4)));
  }

  void addImm0_508s4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate is scaled by four in the encoding and is stored
    // in the MCInst as such. Lop off the low two bits here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() / 4));
  }

  void addImm1_16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The constant encodes as the immediate-1, and we store in the instruction
    // the bits as encoded, so subtract off one here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() - 1));
  }

  void addImm1_32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The constant encodes as the immediate-1, and we store in the instruction
    // the bits as encoded, so subtract off one here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() - 1));
  }

  void addImmThumbSROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The constant encodes as the immediate, except for 32, which encodes as
    // zero.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Imm = CE->getValue();
    Inst.addOperand(MCOperand::createImm((Imm == 32 ? 0 : Imm)));
  }

  void addPKHASRImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // An ASR value of 32 encodes as 0, so that's how we want to add it to
    // the instruction as well.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    int Val = CE->getValue();
    Inst.addOperand(MCOperand::createImm(Val == 32 ? 0 : Val));
  }

  void addT2SOImmNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The operand is actually a t2_so_imm, but we have its bitwise
    // negation in the assembly source, so twiddle it here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(~(uint32_t)CE->getValue()));
  }

  void addT2SOImmNegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The operand is actually a t2_so_imm, but we have its
    // negation in the assembly source, so twiddle it here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(-(uint32_t)CE->getValue()));
  }

  void addImm0_4095NegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The operand is actually an imm0_4095, but we have its
    // negation in the assembly source, so twiddle it here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(-(uint32_t)CE->getValue()));
  }

  void addUnsignedOffset_b8s2Operands(MCInst &Inst, unsigned N) const {
    if(const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm())) {
      Inst.addOperand(MCOperand::createImm(CE->getValue() >> 2));
      return;
    }

    const MCSymbolRefExpr *SR = dyn_cast<MCSymbolRefExpr>(Imm.Val);
    assert(SR && "Unknown value type!");
    Inst.addOperand(MCOperand::createExpr(SR));
  }

  void addThumbMemPCOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    if (isImm()) {
      const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
      if (CE) {
        Inst.addOperand(MCOperand::createImm(CE->getValue()));
        return;
      }

      const MCSymbolRefExpr *SR = dyn_cast<MCSymbolRefExpr>(Imm.Val);

      assert(SR && "Unknown value type!");
      Inst.addOperand(MCOperand::createExpr(SR));
      return;
    }

    assert(isMem()  && "Unknown value type!");
    assert(isa<MCConstantExpr>(Memory.OffsetImm) && "Unknown value type!");
    Inst.addOperand(MCOperand::createImm(Memory.OffsetImm->getValue()));
  }

  void addMemBarrierOptOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getMemBarrierOpt())));
  }

  void addInstSyncBarrierOptOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getInstSyncBarrierOpt())));
  }

  void addTraceSyncBarrierOptOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getTraceSyncBarrierOpt())));
  }

  void addMemNoOffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
  }

  void addMemPCRelImm12Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    int32_t Imm = Memory.OffsetImm->getValue();
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addAdrLabelOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(isImm() && "Not an immediate!");

    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup.
    if (!isa<MCConstantExpr>(getImm())) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      return;
    }

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    int Val = CE->getValue();
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAlignedMemoryOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Memory.Alignment));
  }

  void addDupAlignedMemoryNoneOperands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemoryNoneOperands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemory16Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addDupAlignedMemory16Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemory32Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addDupAlignedMemory32Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemory64Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addDupAlignedMemory64Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemory64or128Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addDupAlignedMemory64or128Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAlignedMemory64or128or256Operands(MCInst &Inst, unsigned N) const {
    addAlignedMemoryOperands(Inst, N);
  }

  void addAddrMode2Operands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands!");
    int32_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    if (!Memory.OffsetRegNum) {
      ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
      // Special case for #-0
      if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
      if (Val < 0) Val = -Val;
      Val = ARM_AM::getAM2Opc(AddSub, Val, ARM_AM::no_shift);
    } else {
      // For register offset, we encode the shift type and negation flag
      // here.
      Val = ARM_AM::getAM2Opc(Memory.isNegative ? ARM_AM::sub : ARM_AM::add,
                              Memory.ShiftImm, Memory.ShiftType);
    }
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAM2OffsetImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert(CE && "non-constant AM2OffsetImm operand!");
    int32_t Val = CE->getValue();
    ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
    // Special case for #-0
    if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
    if (Val < 0) Val = -Val;
    Val = ARM_AM::getAM2Opc(AddSub, Val, ARM_AM::no_shift);
    Inst.addOperand(MCOperand::createReg(0));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAddrMode3Operands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands!");
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm()) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      Inst.addOperand(MCOperand::createReg(0));
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    int32_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    if (!Memory.OffsetRegNum) {
      ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
      // Special case for #-0
      if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
      if (Val < 0) Val = -Val;
      Val = ARM_AM::getAM3Opc(AddSub, Val);
    } else {
      // For register offset, we encode the shift type and negation flag
      // here.
      Val = ARM_AM::getAM3Opc(Memory.isNegative ? ARM_AM::sub : ARM_AM::add, 0);
    }
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAM3OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (Kind == k_PostIndexRegister) {
      int32_t Val =
        ARM_AM::getAM3Opc(PostIdxReg.isAdd ? ARM_AM::add : ARM_AM::sub, 0);
      Inst.addOperand(MCOperand::createReg(PostIdxReg.RegNum));
      Inst.addOperand(MCOperand::createImm(Val));
      return;
    }

    // Constant offset.
    const MCConstantExpr *CE = static_cast<const MCConstantExpr*>(getImm());
    int32_t Val = CE->getValue();
    ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
    // Special case for #-0
    if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
    if (Val < 0) Val = -Val;
    Val = ARM_AM::getAM3Opc(AddSub, Val);
    Inst.addOperand(MCOperand::createReg(0));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAddrMode5Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm()) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    // The lower two bits are always zero and as such are not encoded.
    int32_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() / 4 : 0;
    ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
    // Special case for #-0
    if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
    if (Val < 0) Val = -Val;
    Val = ARM_AM::getAM5Opc(AddSub, Val);
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addAddrMode5FP16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm()) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    // The lower bit is always zero and as such is not encoded.
    int32_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() / 2 : 0;
    ARM_AM::AddrOpc AddSub = Val < 0 ? ARM_AM::sub : ARM_AM::add;
    // Special case for #-0
    if (Val == std::numeric_limits<int32_t>::min()) Val = 0;
    if (Val < 0) Val = -Val;
    Val = ARM_AM::getAM5FP16Opc(AddSub, Val);
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemImm8s4OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // If we have an immediate that's not a constant, treat it as a label
    // reference needing a fixup. If it is a constant, it's something else
    // and we reject it.
    if (isImm()) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    int64_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemImm0_1020s4OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // The lower two bits are always zero and as such are not encoded.
    int32_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() / 4 : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemImm8OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    int64_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemPosImm8OffsetOperands(MCInst &Inst, unsigned N) const {
    addMemImm8OffsetOperands(Inst, N);
  }

  void addMemNegImm8OffsetOperands(MCInst &Inst, unsigned N) const {
    addMemImm8OffsetOperands(Inst, N);
  }

  void addMemUImm12OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // If this is an immediate, it's a label reference.
    if (isImm()) {
      addExpr(Inst, getImm());
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    // Otherwise, it's a normal memory reg+offset.
    int64_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemImm12OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    // If this is an immediate, it's a label reference.
    if (isImm()) {
      addExpr(Inst, getImm());
      Inst.addOperand(MCOperand::createImm(0));
      return;
    }

    // Otherwise, it's a normal memory reg+offset.
    int64_t Val = Memory.OffsetImm ? Memory.OffsetImm->getValue() : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addConstPoolAsmImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // This is container for the immediate that we will create the constant
    // pool from
    addExpr(Inst, getConstantPoolImm());
    return;
  }

  void addMemTBBOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
  }

  void addMemTBHOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
  }

  void addMemRegOffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands!");
    unsigned Val =
      ARM_AM::getAM2Opc(Memory.isNegative ? ARM_AM::sub : ARM_AM::add,
                        Memory.ShiftImm, Memory.ShiftType);
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addT2MemRegOffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 3 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
    Inst.addOperand(MCOperand::createImm(Memory.ShiftImm));
  }

  void addMemThumbRROperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createReg(Memory.OffsetRegNum));
  }

  void addMemThumbRIs4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    int64_t Val = Memory.OffsetImm ? (Memory.OffsetImm->getValue() / 4) : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemThumbRIs2Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    int64_t Val = Memory.OffsetImm ? (Memory.OffsetImm->getValue() / 2) : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemThumbRIs1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    int64_t Val = Memory.OffsetImm ? (Memory.OffsetImm->getValue()) : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addMemThumbSPIOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    int64_t Val = Memory.OffsetImm ? (Memory.OffsetImm->getValue() / 4) : 0;
    Inst.addOperand(MCOperand::createReg(Memory.BaseRegNum));
    Inst.addOperand(MCOperand::createImm(Val));
  }

  void addPostIdxImm8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert(CE && "non-constant post-idx-imm8 operand!");
    int Imm = CE->getValue();
    bool isAdd = Imm >= 0;
    if (Imm == std::numeric_limits<int32_t>::min()) Imm = 0;
    Imm = (Imm < 0 ? -Imm : Imm) | (int)isAdd << 8;
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addPostIdxImm8s4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert(CE && "non-constant post-idx-imm8s4 operand!");
    int Imm = CE->getValue();
    bool isAdd = Imm >= 0;
    if (Imm == std::numeric_limits<int32_t>::min()) Imm = 0;
    // Immediate is scaled by 4.
    Imm = ((Imm < 0 ? -Imm : Imm) / 4) | (int)isAdd << 8;
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addPostIdxRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(PostIdxReg.RegNum));
    Inst.addOperand(MCOperand::createImm(PostIdxReg.isAdd));
  }

  void addPostIdxRegShiftedOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(PostIdxReg.RegNum));
    // The sign, shift type, and shift amount are encoded in a single operand
    // using the AM2 encoding helpers.
    ARM_AM::AddrOpc opc = PostIdxReg.isAdd ? ARM_AM::add : ARM_AM::sub;
    unsigned Imm = ARM_AM::getAM2Opc(opc, PostIdxReg.ShiftImm,
                                     PostIdxReg.ShiftTy);
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addMSRMaskOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getMSRMask())));
  }

  void addBankedRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getBankedReg())));
  }

  void addProcIFlagsOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(unsigned(getProcIFlags())));
  }

  void addVecListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(VectorList.RegNum));
  }

  void addVecListIndexedOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(VectorList.RegNum));
    Inst.addOperand(MCOperand::createImm(VectorList.LaneIndex));
  }

  void addVectorIndex8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndex16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndex32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndex64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addNEONi8splatOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    // Mask in that this is an i8 splat.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() | 0xe00));
  }

  void addNEONi16splatOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = CE->getValue();
    Value = ARM_AM::encodeNEONi16splat(Value);
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONi16splatNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = CE->getValue();
    Value = ARM_AM::encodeNEONi16splat(~Value & 0xffff);
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONi32splatOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = CE->getValue();
    Value = ARM_AM::encodeNEONi32splat(Value);
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONi32splatNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = CE->getValue();
    Value = ARM_AM::encodeNEONi32splat(~Value);
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONi8ReplicateOperands(MCInst &Inst, bool Inv) const {
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert((Inst.getOpcode() == ARM::VMOVv8i8 ||
            Inst.getOpcode() == ARM::VMOVv16i8) &&
          "All instructions that wants to replicate non-zero byte "
          "always must be replaced with VMOVv8i8 or VMOVv16i8.");
    unsigned Value = CE->getValue();
    if (Inv)
      Value = ~Value;
    unsigned B = Value & 0xff;
    B |= 0xe00; // cmode = 0b1110
    Inst.addOperand(MCOperand::createImm(B));
  }

  void addNEONinvi8ReplicateOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addNEONi8ReplicateOperands(Inst, true);
  }

  static unsigned encodeNeonVMOVImmediate(unsigned Value) {
    if (Value >= 256 && Value <= 0xffff)
      Value = (Value >> 8) | ((Value & 0xff) ? 0xc00 : 0x200);
    else if (Value > 0xffff && Value <= 0xffffff)
      Value = (Value >> 16) | ((Value & 0xff) ? 0xd00 : 0x400);
    else if (Value > 0xffffff)
      Value = (Value >> 24) | 0x600;
    return Value;
  }

  void addNEONi32vmovOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = encodeNeonVMOVImmediate(CE->getValue());
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONvmovi8ReplicateOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addNEONi8ReplicateOperands(Inst, false);
  }

  void addNEONvmovi16ReplicateOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert((Inst.getOpcode() == ARM::VMOVv4i16 ||
            Inst.getOpcode() == ARM::VMOVv8i16 ||
            Inst.getOpcode() == ARM::VMVNv4i16 ||
            Inst.getOpcode() == ARM::VMVNv8i16) &&
          "All instructions that want to replicate non-zero half-word "
          "always must be replaced with V{MOV,MVN}v{4,8}i16.");
    uint64_t Value = CE->getValue();
    unsigned Elem = Value & 0xffff;
    if (Elem >= 256)
      Elem = (Elem >> 8) | 0x200;
    Inst.addOperand(MCOperand::createImm(Elem));
  }

  void addNEONi32vmovNegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    unsigned Value = encodeNeonVMOVImmediate(~CE->getValue());
    Inst.addOperand(MCOperand::createImm(Value));
  }

  void addNEONvmovi32ReplicateOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    assert((Inst.getOpcode() == ARM::VMOVv2i32 ||
            Inst.getOpcode() == ARM::VMOVv4i32 ||
            Inst.getOpcode() == ARM::VMVNv2i32 ||
            Inst.getOpcode() == ARM::VMVNv4i32) &&
          "All instructions that want to replicate non-zero word "
          "always must be replaced with V{MOV,MVN}v{2,4}i32.");
    uint64_t Value = CE->getValue();
    unsigned Elem = encodeNeonVMOVImmediate(Value & 0xffffffff);
    Inst.addOperand(MCOperand::createImm(Elem));
  }

  void addNEONi64splatOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The immediate encodes the type of constant as well as the value.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    unsigned Imm = 0;
    for (unsigned i = 0; i < 8; ++i, Value >>= 8) {
      Imm |= (Value & 1) << i;
    }
    Inst.addOperand(MCOperand::createImm(Imm | 0x1e00));
  }

  void addComplexRotationEvenOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(CE->getValue() / 90));
  }

  void addComplexRotationOddOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm((CE->getValue() - 90) / 180));
  }

  void print(raw_ostream &OS) const override;

  static std::unique_ptr<ARMOperand> CreateITMask(unsigned Mask, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_ITCondMask);
    Op->ITMask.Mask = Mask;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateCondCode(ARMCC::CondCodes CC,
                                                    SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_CondCode);
    Op->CC.Val = CC;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateCoprocNum(unsigned CopVal, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_CoprocNum);
    Op->Cop.Val = CopVal;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateCoprocReg(unsigned CopVal, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_CoprocReg);
    Op->Cop.Val = CopVal;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateCoprocOption(unsigned Val, SMLoc S,
                                                        SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_CoprocOption);
    Op->Cop.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateCCOut(unsigned RegNum, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_CCOut);
    Op->Reg.RegNum = RegNum;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateToken(StringRef Str, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateReg(unsigned RegNum, SMLoc S,
                                               SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_Register);
    Op->Reg.RegNum = RegNum;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateShiftedRegister(ARM_AM::ShiftOpc ShTy, unsigned SrcReg,
                        unsigned ShiftReg, unsigned ShiftImm, SMLoc S,
                        SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_ShiftedRegister);
    Op->RegShiftedReg.ShiftTy = ShTy;
    Op->RegShiftedReg.SrcReg = SrcReg;
    Op->RegShiftedReg.ShiftReg = ShiftReg;
    Op->RegShiftedReg.ShiftImm = ShiftImm;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateShiftedImmediate(ARM_AM::ShiftOpc ShTy, unsigned SrcReg,
                         unsigned ShiftImm, SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_ShiftedImmediate);
    Op->RegShiftedImm.ShiftTy = ShTy;
    Op->RegShiftedImm.SrcReg = SrcReg;
    Op->RegShiftedImm.ShiftImm = ShiftImm;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateShifterImm(bool isASR, unsigned Imm,
                                                      SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_ShifterImmediate);
    Op->ShifterImm.isASR = isASR;
    Op->ShifterImm.Imm = Imm;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateRotImm(unsigned Imm, SMLoc S,
                                                  SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_RotateImmediate);
    Op->RotImm.Imm = Imm;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateModImm(unsigned Bits, unsigned Rot,
                                                  SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_ModifiedImmediate);
    Op->ModImm.Bits = Bits;
    Op->ModImm.Rot = Rot;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateConstantPoolImm(const MCExpr *Val, SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_ConstantPoolImmediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateBitfield(unsigned LSB, unsigned Width, SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_BitfieldDescriptor);
    Op->Bitfield.LSB = LSB;
    Op->Bitfield.Width = Width;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateRegList(SmallVectorImpl<std::pair<unsigned, unsigned>> &Regs,
                SMLoc StartLoc, SMLoc EndLoc) {
    assert(Regs.size() > 0 && "RegList contains no registers?");
    KindTy Kind = k_RegisterList;

    if (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Regs.front().second))
      Kind = k_DPRRegisterList;
    else if (ARMMCRegisterClasses[ARM::SPRRegClassID].
             contains(Regs.front().second))
      Kind = k_SPRRegisterList;

    // Sort based on the register encoding values.
    array_pod_sort(Regs.begin(), Regs.end());

    auto Op = make_unique<ARMOperand>(Kind);
    for (SmallVectorImpl<std::pair<unsigned, unsigned>>::const_iterator
           I = Regs.begin(), E = Regs.end(); I != E; ++I)
      Op->Registers.push_back(I->second);
    Op->StartLoc = StartLoc;
    Op->EndLoc = EndLoc;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateVectorList(unsigned RegNum,
                                                      unsigned Count,
                                                      bool isDoubleSpaced,
                                                      SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_VectorList);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.isDoubleSpaced = isDoubleSpaced;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateVectorListAllLanes(unsigned RegNum, unsigned Count, bool isDoubleSpaced,
                           SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_VectorListAllLanes);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.isDoubleSpaced = isDoubleSpaced;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateVectorListIndexed(unsigned RegNum, unsigned Count, unsigned Index,
                          bool isDoubleSpaced, SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_VectorListIndexed);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.LaneIndex = Index;
    Op->VectorList.isDoubleSpaced = isDoubleSpaced;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateVectorIndex(unsigned Idx, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<ARMOperand>(k_VectorIndex);
    Op->VectorIndex.Val = Idx;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                               SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateMem(unsigned BaseRegNum, const MCConstantExpr *OffsetImm,
            unsigned OffsetRegNum, ARM_AM::ShiftOpc ShiftType,
            unsigned ShiftImm, unsigned Alignment, bool isNegative, SMLoc S,
            SMLoc E, SMLoc AlignmentLoc = SMLoc()) {
    auto Op = make_unique<ARMOperand>(k_Memory);
    Op->Memory.BaseRegNum = BaseRegNum;
    Op->Memory.OffsetImm = OffsetImm;
    Op->Memory.OffsetRegNum = OffsetRegNum;
    Op->Memory.ShiftType = ShiftType;
    Op->Memory.ShiftImm = ShiftImm;
    Op->Memory.Alignment = Alignment;
    Op->Memory.isNegative = isNegative;
    Op->StartLoc = S;
    Op->EndLoc = E;
    Op->AlignmentLoc = AlignmentLoc;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreatePostIdxReg(unsigned RegNum, bool isAdd, ARM_AM::ShiftOpc ShiftTy,
                   unsigned ShiftImm, SMLoc S, SMLoc E) {
    auto Op = make_unique<ARMOperand>(k_PostIndexRegister);
    Op->PostIdxReg.RegNum = RegNum;
    Op->PostIdxReg.isAdd = isAdd;
    Op->PostIdxReg.ShiftTy = ShiftTy;
    Op->PostIdxReg.ShiftImm = ShiftImm;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateMemBarrierOpt(ARM_MB::MemBOpt Opt,
                                                         SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_MemBarrierOpt);
    Op->MBOpt.Val = Opt;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateInstSyncBarrierOpt(ARM_ISB::InstSyncBOpt Opt, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_InstSyncBarrierOpt);
    Op->ISBOpt.Val = Opt;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand>
  CreateTraceSyncBarrierOpt(ARM_TSB::TraceSyncBOpt Opt, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_TraceSyncBarrierOpt);
    Op->TSBOpt.Val = Opt;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateProcIFlags(ARM_PROC::IFlags IFlags,
                                                      SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_ProcIFlags);
    Op->IFlags.Val = IFlags;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateMSRMask(unsigned MMask, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_MSRMask);
    Op->MMask.Val = MMask;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<ARMOperand> CreateBankedReg(unsigned Reg, SMLoc S) {
    auto Op = make_unique<ARMOperand>(k_BankedReg);
    Op->BankedReg.Val = Reg;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }
};

} // end anonymous namespace.

void ARMOperand::print(raw_ostream &OS) const {
  auto RegName = [](unsigned Reg) {
    if (Reg)
      return ARMInstPrinter::getRegisterName(Reg);
    else
      return "noreg";
  };

  switch (Kind) {
  case k_CondCode:
    OS << "<ARMCC::" << ARMCondCodeToString(getCondCode()) << ">";
    break;
  case k_CCOut:
    OS << "<ccout " << RegName(getReg()) << ">";
    break;
  case k_ITCondMask: {
    static const char *const MaskStr[] = {
      "(invalid)", "(teee)", "(tee)", "(teet)",
      "(te)",      "(tete)", "(tet)", "(tett)",
      "(t)",       "(ttee)", "(tte)", "(ttet)",
      "(tt)",      "(ttte)", "(ttt)", "(tttt)"
    };
    assert((ITMask.Mask & 0xf) == ITMask.Mask);
    OS << "<it-mask " << MaskStr[ITMask.Mask] << ">";
    break;
  }
  case k_CoprocNum:
    OS << "<coprocessor number: " << getCoproc() << ">";
    break;
  case k_CoprocReg:
    OS << "<coprocessor register: " << getCoproc() << ">";
    break;
  case k_CoprocOption:
    OS << "<coprocessor option: " << CoprocOption.Val << ">";
    break;
  case k_MSRMask:
    OS << "<mask: " << getMSRMask() << ">";
    break;
  case k_BankedReg:
    OS << "<banked reg: " << getBankedReg() << ">";
    break;
  case k_Immediate:
    OS << *getImm();
    break;
  case k_MemBarrierOpt:
    OS << "<ARM_MB::" << MemBOptToString(getMemBarrierOpt(), false) << ">";
    break;
  case k_InstSyncBarrierOpt:
    OS << "<ARM_ISB::" << InstSyncBOptToString(getInstSyncBarrierOpt()) << ">";
    break;
  case k_TraceSyncBarrierOpt:
    OS << "<ARM_TSB::" << TraceSyncBOptToString(getTraceSyncBarrierOpt()) << ">";
    break;
  case k_Memory:
    OS << "<memory";
    if (Memory.BaseRegNum)
      OS << " base:" << RegName(Memory.BaseRegNum);
    if (Memory.OffsetImm)
      OS << " offset-imm:" << *Memory.OffsetImm;
    if (Memory.OffsetRegNum)
      OS << " offset-reg:" << (Memory.isNegative ? "-" : "")
         << RegName(Memory.OffsetRegNum);
    if (Memory.ShiftType != ARM_AM::no_shift) {
      OS << " shift-type:" << ARM_AM::getShiftOpcStr(Memory.ShiftType);
      OS << " shift-imm:" << Memory.ShiftImm;
    }
    if (Memory.Alignment)
      OS << " alignment:" << Memory.Alignment;
    OS << ">";
    break;
  case k_PostIndexRegister:
    OS << "post-idx register " << (PostIdxReg.isAdd ? "" : "-")
       << RegName(PostIdxReg.RegNum);
    if (PostIdxReg.ShiftTy != ARM_AM::no_shift)
      OS << ARM_AM::getShiftOpcStr(PostIdxReg.ShiftTy) << " "
         << PostIdxReg.ShiftImm;
    OS << ">";
    break;
  case k_ProcIFlags: {
    OS << "<ARM_PROC::";
    unsigned IFlags = getProcIFlags();
    for (int i=2; i >= 0; --i)
      if (IFlags & (1 << i))
        OS << ARM_PROC::IFlagsToString(1 << i);
    OS << ">";
    break;
  }
  case k_Register:
    OS << "<register " << RegName(getReg()) << ">";
    break;
  case k_ShifterImmediate:
    OS << "<shift " << (ShifterImm.isASR ? "asr" : "lsl")
       << " #" << ShifterImm.Imm << ">";
    break;
  case k_ShiftedRegister:
    OS << "<so_reg_reg " << RegName(RegShiftedReg.SrcReg) << " "
       << ARM_AM::getShiftOpcStr(RegShiftedReg.ShiftTy) << " "
       << RegName(RegShiftedReg.ShiftReg) << ">";
    break;
  case k_ShiftedImmediate:
    OS << "<so_reg_imm " << RegName(RegShiftedImm.SrcReg) << " "
       << ARM_AM::getShiftOpcStr(RegShiftedImm.ShiftTy) << " #"
       << RegShiftedImm.ShiftImm << ">";
    break;
  case k_RotateImmediate:
    OS << "<ror " << " #" << (RotImm.Imm * 8) << ">";
    break;
  case k_ModifiedImmediate:
    OS << "<mod_imm #" << ModImm.Bits << ", #"
       <<  ModImm.Rot << ")>";
    break;
  case k_ConstantPoolImmediate:
    OS << "<constant_pool_imm #" << *getConstantPoolImm();
    break;
  case k_BitfieldDescriptor:
    OS << "<bitfield " << "lsb: " << Bitfield.LSB
       << ", width: " << Bitfield.Width << ">";
    break;
  case k_RegisterList:
  case k_DPRRegisterList:
  case k_SPRRegisterList: {
    OS << "<register_list ";

    const SmallVectorImpl<unsigned> &RegList = getRegList();
    for (SmallVectorImpl<unsigned>::const_iterator
           I = RegList.begin(), E = RegList.end(); I != E; ) {
      OS << RegName(*I);
      if (++I < E) OS << ", ";
    }

    OS << ">";
    break;
  }
  case k_VectorList:
    OS << "<vector_list " << VectorList.Count << " * "
       << RegName(VectorList.RegNum) << ">";
    break;
  case k_VectorListAllLanes:
    OS << "<vector_list(all lanes) " << VectorList.Count << " * "
       << RegName(VectorList.RegNum) << ">";
    break;
  case k_VectorListIndexed:
    OS << "<vector_list(lane " << VectorList.LaneIndex << ") "
       << VectorList.Count << " * " << RegName(VectorList.RegNum) << ">";
    break;
  case k_Token:
    OS << "'" << getToken() << "'";
    break;
  case k_VectorIndex:
    OS << "<vectorindex " << getVectorIndex() << ">";
    break;
  }
}

/// @name Auto-generated Match Functions
/// {

static unsigned MatchRegisterName(StringRef Name);

/// }

bool ARMAsmParser::ParseRegister(unsigned &RegNo,
                                 SMLoc &StartLoc, SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  RegNo = tryParseRegister();

  return (RegNo == (unsigned)-1);
}

/// Try to parse a register name.  The token must be an Identifier when called,
/// and if it is a register name the token is eaten and the register number is
/// returned.  Otherwise return -1.
int ARMAsmParser::tryParseRegister() {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) return -1;

  std::string lowerCase = Tok.getString().lower();
  unsigned RegNum = MatchRegisterName(lowerCase);
  if (!RegNum) {
    RegNum = StringSwitch<unsigned>(lowerCase)
      .Case("r13", ARM::SP)
      .Case("r14", ARM::LR)
      .Case("r15", ARM::PC)
      .Case("ip", ARM::R12)
      // Additional register name aliases for 'gas' compatibility.
      .Case("a1", ARM::R0)
      .Case("a2", ARM::R1)
      .Case("a3", ARM::R2)
      .Case("a4", ARM::R3)
      .Case("v1", ARM::R4)
      .Case("v2", ARM::R5)
      .Case("v3", ARM::R6)
      .Case("v4", ARM::R7)
      .Case("v5", ARM::R8)
      .Case("v6", ARM::R9)
      .Case("v7", ARM::R10)
      .Case("v8", ARM::R11)
      .Case("sb", ARM::R9)
      .Case("sl", ARM::R10)
      .Case("fp", ARM::R11)
      .Default(0);
  }
  if (!RegNum) {
    // Check for aliases registered via .req. Canonicalize to lower case.
    // That's more consistent since register names are case insensitive, and
    // it's how the original entry was passed in from MC/MCParser/AsmParser.
    StringMap<unsigned>::const_iterator Entry = RegisterReqs.find(lowerCase);
    // If no match, return failure.
    if (Entry == RegisterReqs.end())
      return -1;
    Parser.Lex(); // Eat identifier token.
    return Entry->getValue();
  }

  // Some FPUs only have 16 D registers, so D16-D31 are invalid
  if (hasD16() && RegNum >= ARM::D16 && RegNum <= ARM::D31)
    return -1;

  Parser.Lex(); // Eat identifier token.

  return RegNum;
}

// Try to parse a shifter  (e.g., "lsl <amt>"). On success, return 0.
// If a recoverable error occurs, return 1. If an irrecoverable error
// occurs, return -1. An irrecoverable error is one where tokens have been
// consumed in the process of trying to parse the shifter (i.e., when it is
// indeed a shifter operand, but malformed).
int ARMAsmParser::tryParseShiftRegister(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return -1;

  std::string lowerCase = Tok.getString().lower();
  ARM_AM::ShiftOpc ShiftTy = StringSwitch<ARM_AM::ShiftOpc>(lowerCase)
      .Case("asl", ARM_AM::lsl)
      .Case("lsl", ARM_AM::lsl)
      .Case("lsr", ARM_AM::lsr)
      .Case("asr", ARM_AM::asr)
      .Case("ror", ARM_AM::ror)
      .Case("rrx", ARM_AM::rrx)
      .Default(ARM_AM::no_shift);

  if (ShiftTy == ARM_AM::no_shift)
    return 1;

  Parser.Lex(); // Eat the operator.

  // The source register for the shift has already been added to the
  // operand list, so we need to pop it off and combine it into the shifted
  // register operand instead.
  std::unique_ptr<ARMOperand> PrevOp(
      (ARMOperand *)Operands.pop_back_val().release());
  if (!PrevOp->isReg())
    return Error(PrevOp->getStartLoc(), "shift must be of a register");
  int SrcReg = PrevOp->getReg();

  SMLoc EndLoc;
  int64_t Imm = 0;
  int ShiftReg = 0;
  if (ShiftTy == ARM_AM::rrx) {
    // RRX Doesn't have an explicit shift amount. The encoder expects
    // the shift register to be the same as the source register. Seems odd,
    // but OK.
    ShiftReg = SrcReg;
  } else {
    // Figure out if this is shifted by a constant or a register (for non-RRX).
    if (Parser.getTok().is(AsmToken::Hash) ||
        Parser.getTok().is(AsmToken::Dollar)) {
      Parser.Lex(); // Eat hash.
      SMLoc ImmLoc = Parser.getTok().getLoc();
      const MCExpr *ShiftExpr = nullptr;
      if (getParser().parseExpression(ShiftExpr, EndLoc)) {
        Error(ImmLoc, "invalid immediate shift value");
        return -1;
      }
      // The expression must be evaluatable as an immediate.
      const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ShiftExpr);
      if (!CE) {
        Error(ImmLoc, "invalid immediate shift value");
        return -1;
      }
      // Range check the immediate.
      // lsl, ror: 0 <= imm <= 31
      // lsr, asr: 0 <= imm <= 32
      Imm = CE->getValue();
      if (Imm < 0 ||
          ((ShiftTy == ARM_AM::lsl || ShiftTy == ARM_AM::ror) && Imm > 31) ||
          ((ShiftTy == ARM_AM::lsr || ShiftTy == ARM_AM::asr) && Imm > 32)) {
        Error(ImmLoc, "immediate shift value out of range");
        return -1;
      }
      // shift by zero is a nop. Always send it through as lsl.
      // ('as' compatibility)
      if (Imm == 0)
        ShiftTy = ARM_AM::lsl;
    } else if (Parser.getTok().is(AsmToken::Identifier)) {
      SMLoc L = Parser.getTok().getLoc();
      EndLoc = Parser.getTok().getEndLoc();
      ShiftReg = tryParseRegister();
      if (ShiftReg == -1) {
        Error(L, "expected immediate or register in shift operand");
        return -1;
      }
    } else {
      Error(Parser.getTok().getLoc(),
            "expected immediate or register in shift operand");
      return -1;
    }
  }

  if (ShiftReg && ShiftTy != ARM_AM::rrx)
    Operands.push_back(ARMOperand::CreateShiftedRegister(ShiftTy, SrcReg,
                                                         ShiftReg, Imm,
                                                         S, EndLoc));
  else
    Operands.push_back(ARMOperand::CreateShiftedImmediate(ShiftTy, SrcReg, Imm,
                                                          S, EndLoc));

  return 0;
}

/// Try to parse a register name.  The token must be an Identifier when called.
/// If it's a register, an AsmOperand is created. Another AsmOperand is created
/// if there is a "writeback". 'true' if it's not a register.
///
/// TODO this is likely to change to allow different register types and or to
/// parse for a specific register type.
bool ARMAsmParser::tryParseRegisterWithWriteBack(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc RegStartLoc = Parser.getTok().getLoc();
  SMLoc RegEndLoc = Parser.getTok().getEndLoc();
  int RegNo = tryParseRegister();
  if (RegNo == -1)
    return true;

  Operands.push_back(ARMOperand::CreateReg(RegNo, RegStartLoc, RegEndLoc));

  const AsmToken &ExclaimTok = Parser.getTok();
  if (ExclaimTok.is(AsmToken::Exclaim)) {
    Operands.push_back(ARMOperand::CreateToken(ExclaimTok.getString(),
                                               ExclaimTok.getLoc()));
    Parser.Lex(); // Eat exclaim token
    return false;
  }

  // Also check for an index operand. This is only legal for vector registers,
  // but that'll get caught OK in operand matching, so we don't need to
  // explicitly filter everything else out here.
  if (Parser.getTok().is(AsmToken::LBrac)) {
    SMLoc SIdx = Parser.getTok().getLoc();
    Parser.Lex(); // Eat left bracket token.

    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return true;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return TokError("immediate value expected for vector index");

    if (Parser.getTok().isNot(AsmToken::RBrac))
      return Error(Parser.getTok().getLoc(), "']' expected");

    SMLoc E = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat right bracket token.

    Operands.push_back(ARMOperand::CreateVectorIndex(MCE->getValue(),
                                                     SIdx, E,
                                                     getContext()));
  }

  return false;
}

/// MatchCoprocessorOperandName - Try to parse an coprocessor related
/// instruction with a symbolic operand name.
/// We accept "crN" syntax for GAS compatibility.
/// <operand-name> ::= <prefix><number>
/// If CoprocOp is 'c', then:
///   <prefix> ::= c | cr
/// If CoprocOp is 'p', then :
///   <prefix> ::= p
/// <number> ::= integer in range [0, 15]
static int MatchCoprocessorOperandName(StringRef Name, char CoprocOp) {
  // Use the same layout as the tablegen'erated register name matcher. Ugly,
  // but efficient.
  if (Name.size() < 2 || Name[0] != CoprocOp)
    return -1;
  Name = (Name[1] == 'r') ? Name.drop_front(2) : Name.drop_front();

  switch (Name.size()) {
  default: return -1;
  case 1:
    switch (Name[0]) {
    default:  return -1;
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    }
  case 2:
    if (Name[0] != '1')
      return -1;
    switch (Name[1]) {
    default:  return -1;
    // CP10 and CP11 are VFP/NEON and so vector instructions should be used.
    // However, old cores (v5/v6) did use them in that way.
    case '0': return 10;
    case '1': return 11;
    case '2': return 12;
    case '3': return 13;
    case '4': return 14;
    case '5': return 15;
    }
  }
}

/// parseITCondCode - Try to parse a condition code for an IT instruction.
OperandMatchResultTy
ARMAsmParser::parseITCondCode(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return MatchOperand_NoMatch;
  unsigned CC = ARMCondCodeFromString(Tok.getString());
  if (CC == ~0U)
    return MatchOperand_NoMatch;
  Parser.Lex(); // Eat the token.

  Operands.push_back(ARMOperand::CreateCondCode(ARMCC::CondCodes(CC), S));

  return MatchOperand_Success;
}

/// parseCoprocNumOperand - Try to parse an coprocessor number operand. The
/// token must be an Identifier when called, and if it is a coprocessor
/// number, the token is eaten and the operand is added to the operand list.
OperandMatchResultTy
ARMAsmParser::parseCoprocNumOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  int Num = MatchCoprocessorOperandName(Tok.getString(), 'p');
  if (Num == -1)
    return MatchOperand_NoMatch;
  // ARMv7 and v8 don't allow cp10/cp11 due to VFP/NEON specific instructions
  if ((hasV7Ops() || hasV8Ops()) && (Num == 10 || Num == 11))
    return MatchOperand_NoMatch;

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(ARMOperand::CreateCoprocNum(Num, S));
  return MatchOperand_Success;
}

/// parseCoprocRegOperand - Try to parse an coprocessor register operand. The
/// token must be an Identifier when called, and if it is a coprocessor
/// number, the token is eaten and the operand is added to the operand list.
OperandMatchResultTy
ARMAsmParser::parseCoprocRegOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  int Reg = MatchCoprocessorOperandName(Tok.getString(), 'c');
  if (Reg == -1)
    return MatchOperand_NoMatch;

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(ARMOperand::CreateCoprocReg(Reg, S));
  return MatchOperand_Success;
}

/// parseCoprocOptionOperand - Try to parse an coprocessor option operand.
/// coproc_option : '{' imm0_255 '}'
OperandMatchResultTy
ARMAsmParser::parseCoprocOptionOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();

  // If this isn't a '{', this isn't a coprocessor immediate operand.
  if (Parser.getTok().isNot(AsmToken::LCurly))
    return MatchOperand_NoMatch;
  Parser.Lex(); // Eat the '{'

  const MCExpr *Expr;
  SMLoc Loc = Parser.getTok().getLoc();
  if (getParser().parseExpression(Expr)) {
    Error(Loc, "illegal expression");
    return MatchOperand_ParseFail;
  }
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
  if (!CE || CE->getValue() < 0 || CE->getValue() > 255) {
    Error(Loc, "coprocessor option must be an immediate in range [0, 255]");
    return MatchOperand_ParseFail;
  }
  int Val = CE->getValue();

  // Check for and consume the closing '}'
  if (Parser.getTok().isNot(AsmToken::RCurly))
    return MatchOperand_ParseFail;
  SMLoc E = Parser.getTok().getEndLoc();
  Parser.Lex(); // Eat the '}'

  Operands.push_back(ARMOperand::CreateCoprocOption(Val, S, E));
  return MatchOperand_Success;
}

// For register list parsing, we need to map from raw GPR register numbering
// to the enumeration values. The enumeration values aren't sorted by
// register number due to our using "sp", "lr" and "pc" as canonical names.
static unsigned getNextRegister(unsigned Reg) {
  // If this is a GPR, we need to do it manually, otherwise we can rely
  // on the sort ordering of the enumeration since the other reg-classes
  // are sane.
  if (!ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Reg))
    return Reg + 1;
  switch(Reg) {
  default: llvm_unreachable("Invalid GPR number!");
  case ARM::R0:  return ARM::R1;  case ARM::R1:  return ARM::R2;
  case ARM::R2:  return ARM::R3;  case ARM::R3:  return ARM::R4;
  case ARM::R4:  return ARM::R5;  case ARM::R5:  return ARM::R6;
  case ARM::R6:  return ARM::R7;  case ARM::R7:  return ARM::R8;
  case ARM::R8:  return ARM::R9;  case ARM::R9:  return ARM::R10;
  case ARM::R10: return ARM::R11; case ARM::R11: return ARM::R12;
  case ARM::R12: return ARM::SP;  case ARM::SP:  return ARM::LR;
  case ARM::LR:  return ARM::PC;  case ARM::PC:  return ARM::R0;
  }
}

/// Parse a register list.
bool ARMAsmParser::parseRegisterList(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::LCurly))
    return TokError("Token is not a Left Curly Brace");
  SMLoc S = Parser.getTok().getLoc();
  Parser.Lex(); // Eat '{' token.
  SMLoc RegLoc = Parser.getTok().getLoc();

  // Check the first register in the list to see what register class
  // this is a list of.
  int Reg = tryParseRegister();
  if (Reg == -1)
    return Error(RegLoc, "register expected");

  // The reglist instructions have at most 16 registers, so reserve
  // space for that many.
  int EReg = 0;
  SmallVector<std::pair<unsigned, unsigned>, 16> Registers;

  // Allow Q regs and just interpret them as the two D sub-registers.
  if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(Reg)) {
    Reg = getDRegFromQReg(Reg);
    EReg = MRI->getEncodingValue(Reg);
    Registers.push_back(std::pair<unsigned, unsigned>(EReg, Reg));
    ++Reg;
  }
  const MCRegisterClass *RC;
  if (ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Reg))
    RC = &ARMMCRegisterClasses[ARM::GPRRegClassID];
  else if (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Reg))
    RC = &ARMMCRegisterClasses[ARM::DPRRegClassID];
  else if (ARMMCRegisterClasses[ARM::SPRRegClassID].contains(Reg))
    RC = &ARMMCRegisterClasses[ARM::SPRRegClassID];
  else
    return Error(RegLoc, "invalid register in register list");

  // Store the register.
  EReg = MRI->getEncodingValue(Reg);
  Registers.push_back(std::pair<unsigned, unsigned>(EReg, Reg));

  // This starts immediately after the first register token in the list,
  // so we can see either a comma or a minus (range separator) as a legal
  // next token.
  while (Parser.getTok().is(AsmToken::Comma) ||
         Parser.getTok().is(AsmToken::Minus)) {
    if (Parser.getTok().is(AsmToken::Minus)) {
      Parser.Lex(); // Eat the minus.
      SMLoc AfterMinusLoc = Parser.getTok().getLoc();
      int EndReg = tryParseRegister();
      if (EndReg == -1)
        return Error(AfterMinusLoc, "register expected");
      // Allow Q regs and just interpret them as the two D sub-registers.
      if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(EndReg))
        EndReg = getDRegFromQReg(EndReg) + 1;
      // If the register is the same as the start reg, there's nothing
      // more to do.
      if (Reg == EndReg)
        continue;
      // The register must be in the same register class as the first.
      if (!RC->contains(EndReg))
        return Error(AfterMinusLoc, "invalid register in register list");
      // Ranges must go from low to high.
      if (MRI->getEncodingValue(Reg) > MRI->getEncodingValue(EndReg))
        return Error(AfterMinusLoc, "bad range in register list");

      // Add all the registers in the range to the register list.
      while (Reg != EndReg) {
        Reg = getNextRegister(Reg);
        EReg = MRI->getEncodingValue(Reg);
        Registers.push_back(std::pair<unsigned, unsigned>(EReg, Reg));
      }
      continue;
    }
    Parser.Lex(); // Eat the comma.
    RegLoc = Parser.getTok().getLoc();
    int OldReg = Reg;
    const AsmToken RegTok = Parser.getTok();
    Reg = tryParseRegister();
    if (Reg == -1)
      return Error(RegLoc, "register expected");
    // Allow Q regs and just interpret them as the two D sub-registers.
    bool isQReg = false;
    if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(Reg)) {
      Reg = getDRegFromQReg(Reg);
      isQReg = true;
    }
    // The register must be in the same register class as the first.
    if (!RC->contains(Reg))
      return Error(RegLoc, "invalid register in register list");
    // List must be monotonically increasing.
    if (MRI->getEncodingValue(Reg) < MRI->getEncodingValue(OldReg)) {
      if (ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Reg))
        Warning(RegLoc, "register list not in ascending order");
      else
        return Error(RegLoc, "register list not in ascending order");
    }
    if (MRI->getEncodingValue(Reg) == MRI->getEncodingValue(OldReg)) {
      Warning(RegLoc, "duplicated register (" + RegTok.getString() +
              ") in register list");
      continue;
    }
    // VFP register lists must also be contiguous.
    if (RC != &ARMMCRegisterClasses[ARM::GPRRegClassID] &&
        Reg != OldReg + 1)
      return Error(RegLoc, "non-contiguous register range");
    EReg = MRI->getEncodingValue(Reg);
    Registers.push_back(std::pair<unsigned, unsigned>(EReg, Reg));
    if (isQReg) {
      EReg = MRI->getEncodingValue(++Reg);
      Registers.push_back(std::pair<unsigned, unsigned>(EReg, Reg));
    }
  }

  if (Parser.getTok().isNot(AsmToken::RCurly))
    return Error(Parser.getTok().getLoc(), "'}' expected");
  SMLoc E = Parser.getTok().getEndLoc();
  Parser.Lex(); // Eat '}' token.

  // Push the register list operand.
  Operands.push_back(ARMOperand::CreateRegList(Registers, S, E));

  // The ARM system instruction variants for LDM/STM have a '^' token here.
  if (Parser.getTok().is(AsmToken::Caret)) {
    Operands.push_back(ARMOperand::CreateToken("^",Parser.getTok().getLoc()));
    Parser.Lex(); // Eat '^' token.
  }

  return false;
}

// Helper function to parse the lane index for vector lists.
OperandMatchResultTy ARMAsmParser::
parseVectorLane(VectorLaneTy &LaneKind, unsigned &Index, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  Index = 0; // Always return a defined index value.
  if (Parser.getTok().is(AsmToken::LBrac)) {
    Parser.Lex(); // Eat the '['.
    if (Parser.getTok().is(AsmToken::RBrac)) {
      // "Dn[]" is the 'all lanes' syntax.
      LaneKind = AllLanes;
      EndLoc = Parser.getTok().getEndLoc();
      Parser.Lex(); // Eat the ']'.
      return MatchOperand_Success;
    }

    // There's an optional '#' token here. Normally there wouldn't be, but
    // inline assemble puts one in, and it's friendly to accept that.
    if (Parser.getTok().is(AsmToken::Hash))
      Parser.Lex(); // Eat '#' or '$'.

    const MCExpr *LaneIndex;
    SMLoc Loc = Parser.getTok().getLoc();
    if (getParser().parseExpression(LaneIndex)) {
      Error(Loc, "illegal expression");
      return MatchOperand_ParseFail;
    }
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(LaneIndex);
    if (!CE) {
      Error(Loc, "lane index must be empty or an integer");
      return MatchOperand_ParseFail;
    }
    if (Parser.getTok().isNot(AsmToken::RBrac)) {
      Error(Parser.getTok().getLoc(), "']' expected");
      return MatchOperand_ParseFail;
    }
    EndLoc = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat the ']'.
    int64_t Val = CE->getValue();

    // FIXME: Make this range check context sensitive for .8, .16, .32.
    if (Val < 0 || Val > 7) {
      Error(Parser.getTok().getLoc(), "lane index out of range");
      return MatchOperand_ParseFail;
    }
    Index = Val;
    LaneKind = IndexedLane;
    return MatchOperand_Success;
  }
  LaneKind = NoLanes;
  return MatchOperand_Success;
}

// parse a vector register list
OperandMatchResultTy
ARMAsmParser::parseVectorList(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  VectorLaneTy LaneKind;
  unsigned LaneIndex;
  SMLoc S = Parser.getTok().getLoc();
  // As an extension (to match gas), support a plain D register or Q register
  // (without encosing curly braces) as a single or double entry list,
  // respectively.
  if (Parser.getTok().is(AsmToken::Identifier)) {
    SMLoc E = Parser.getTok().getEndLoc();
    int Reg = tryParseRegister();
    if (Reg == -1)
      return MatchOperand_NoMatch;
    if (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Reg)) {
      OperandMatchResultTy Res = parseVectorLane(LaneKind, LaneIndex, E);
      if (Res != MatchOperand_Success)
        return Res;
      switch (LaneKind) {
      case NoLanes:
        Operands.push_back(ARMOperand::CreateVectorList(Reg, 1, false, S, E));
        break;
      case AllLanes:
        Operands.push_back(ARMOperand::CreateVectorListAllLanes(Reg, 1, false,
                                                                S, E));
        break;
      case IndexedLane:
        Operands.push_back(ARMOperand::CreateVectorListIndexed(Reg, 1,
                                                               LaneIndex,
                                                               false, S, E));
        break;
      }
      return MatchOperand_Success;
    }
    if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(Reg)) {
      Reg = getDRegFromQReg(Reg);
      OperandMatchResultTy Res = parseVectorLane(LaneKind, LaneIndex, E);
      if (Res != MatchOperand_Success)
        return Res;
      switch (LaneKind) {
      case NoLanes:
        Reg = MRI->getMatchingSuperReg(Reg, ARM::dsub_0,
                                   &ARMMCRegisterClasses[ARM::DPairRegClassID]);
        Operands.push_back(ARMOperand::CreateVectorList(Reg, 2, false, S, E));
        break;
      case AllLanes:
        Reg = MRI->getMatchingSuperReg(Reg, ARM::dsub_0,
                                   &ARMMCRegisterClasses[ARM::DPairRegClassID]);
        Operands.push_back(ARMOperand::CreateVectorListAllLanes(Reg, 2, false,
                                                                S, E));
        break;
      case IndexedLane:
        Operands.push_back(ARMOperand::CreateVectorListIndexed(Reg, 2,
                                                               LaneIndex,
                                                               false, S, E));
        break;
      }
      return MatchOperand_Success;
    }
    Error(S, "vector register expected");
    return MatchOperand_ParseFail;
  }

  if (Parser.getTok().isNot(AsmToken::LCurly))
    return MatchOperand_NoMatch;

  Parser.Lex(); // Eat '{' token.
  SMLoc RegLoc = Parser.getTok().getLoc();

  int Reg = tryParseRegister();
  if (Reg == -1) {
    Error(RegLoc, "register expected");
    return MatchOperand_ParseFail;
  }
  unsigned Count = 1;
  int Spacing = 0;
  unsigned FirstReg = Reg;
  // The list is of D registers, but we also allow Q regs and just interpret
  // them as the two D sub-registers.
  if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(Reg)) {
    FirstReg = Reg = getDRegFromQReg(Reg);
    Spacing = 1; // double-spacing requires explicit D registers, otherwise
                 // it's ambiguous with four-register single spaced.
    ++Reg;
    ++Count;
  }

  SMLoc E;
  if (parseVectorLane(LaneKind, LaneIndex, E) != MatchOperand_Success)
    return MatchOperand_ParseFail;

  while (Parser.getTok().is(AsmToken::Comma) ||
         Parser.getTok().is(AsmToken::Minus)) {
    if (Parser.getTok().is(AsmToken::Minus)) {
      if (!Spacing)
        Spacing = 1; // Register range implies a single spaced list.
      else if (Spacing == 2) {
        Error(Parser.getTok().getLoc(),
              "sequential registers in double spaced list");
        return MatchOperand_ParseFail;
      }
      Parser.Lex(); // Eat the minus.
      SMLoc AfterMinusLoc = Parser.getTok().getLoc();
      int EndReg = tryParseRegister();
      if (EndReg == -1) {
        Error(AfterMinusLoc, "register expected");
        return MatchOperand_ParseFail;
      }
      // Allow Q regs and just interpret them as the two D sub-registers.
      if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(EndReg))
        EndReg = getDRegFromQReg(EndReg) + 1;
      // If the register is the same as the start reg, there's nothing
      // more to do.
      if (Reg == EndReg)
        continue;
      // The register must be in the same register class as the first.
      if (!ARMMCRegisterClasses[ARM::DPRRegClassID].contains(EndReg)) {
        Error(AfterMinusLoc, "invalid register in register list");
        return MatchOperand_ParseFail;
      }
      // Ranges must go from low to high.
      if (Reg > EndReg) {
        Error(AfterMinusLoc, "bad range in register list");
        return MatchOperand_ParseFail;
      }
      // Parse the lane specifier if present.
      VectorLaneTy NextLaneKind;
      unsigned NextLaneIndex;
      if (parseVectorLane(NextLaneKind, NextLaneIndex, E) !=
          MatchOperand_Success)
        return MatchOperand_ParseFail;
      if (NextLaneKind != LaneKind || LaneIndex != NextLaneIndex) {
        Error(AfterMinusLoc, "mismatched lane index in register list");
        return MatchOperand_ParseFail;
      }

      // Add all the registers in the range to the register list.
      Count += EndReg - Reg;
      Reg = EndReg;
      continue;
    }
    Parser.Lex(); // Eat the comma.
    RegLoc = Parser.getTok().getLoc();
    int OldReg = Reg;
    Reg = tryParseRegister();
    if (Reg == -1) {
      Error(RegLoc, "register expected");
      return MatchOperand_ParseFail;
    }
    // vector register lists must be contiguous.
    // It's OK to use the enumeration values directly here rather, as the
    // VFP register classes have the enum sorted properly.
    //
    // The list is of D registers, but we also allow Q regs and just interpret
    // them as the two D sub-registers.
    if (ARMMCRegisterClasses[ARM::QPRRegClassID].contains(Reg)) {
      if (!Spacing)
        Spacing = 1; // Register range implies a single spaced list.
      else if (Spacing == 2) {
        Error(RegLoc,
              "invalid register in double-spaced list (must be 'D' register')");
        return MatchOperand_ParseFail;
      }
      Reg = getDRegFromQReg(Reg);
      if (Reg != OldReg + 1) {
        Error(RegLoc, "non-contiguous register range");
        return MatchOperand_ParseFail;
      }
      ++Reg;
      Count += 2;
      // Parse the lane specifier if present.
      VectorLaneTy NextLaneKind;
      unsigned NextLaneIndex;
      SMLoc LaneLoc = Parser.getTok().getLoc();
      if (parseVectorLane(NextLaneKind, NextLaneIndex, E) !=
          MatchOperand_Success)
        return MatchOperand_ParseFail;
      if (NextLaneKind != LaneKind || LaneIndex != NextLaneIndex) {
        Error(LaneLoc, "mismatched lane index in register list");
        return MatchOperand_ParseFail;
      }
      continue;
    }
    // Normal D register.
    // Figure out the register spacing (single or double) of the list if
    // we don't know it already.
    if (!Spacing)
      Spacing = 1 + (Reg == OldReg + 2);

    // Just check that it's contiguous and keep going.
    if (Reg != OldReg + Spacing) {
      Error(RegLoc, "non-contiguous register range");
      return MatchOperand_ParseFail;
    }
    ++Count;
    // Parse the lane specifier if present.
    VectorLaneTy NextLaneKind;
    unsigned NextLaneIndex;
    SMLoc EndLoc = Parser.getTok().getLoc();
    if (parseVectorLane(NextLaneKind, NextLaneIndex, E) != MatchOperand_Success)
      return MatchOperand_ParseFail;
    if (NextLaneKind != LaneKind || LaneIndex != NextLaneIndex) {
      Error(EndLoc, "mismatched lane index in register list");
      return MatchOperand_ParseFail;
    }
  }

  if (Parser.getTok().isNot(AsmToken::RCurly)) {
    Error(Parser.getTok().getLoc(), "'}' expected");
    return MatchOperand_ParseFail;
  }
  E = Parser.getTok().getEndLoc();
  Parser.Lex(); // Eat '}' token.

  switch (LaneKind) {
  case NoLanes:
    // Two-register operands have been converted to the
    // composite register classes.
    if (Count == 2) {
      const MCRegisterClass *RC = (Spacing == 1) ?
        &ARMMCRegisterClasses[ARM::DPairRegClassID] :
        &ARMMCRegisterClasses[ARM::DPairSpcRegClassID];
      FirstReg = MRI->getMatchingSuperReg(FirstReg, ARM::dsub_0, RC);
    }
    Operands.push_back(ARMOperand::CreateVectorList(FirstReg, Count,
                                                    (Spacing == 2), S, E));
    break;
  case AllLanes:
    // Two-register operands have been converted to the
    // composite register classes.
    if (Count == 2) {
      const MCRegisterClass *RC = (Spacing == 1) ?
        &ARMMCRegisterClasses[ARM::DPairRegClassID] :
        &ARMMCRegisterClasses[ARM::DPairSpcRegClassID];
      FirstReg = MRI->getMatchingSuperReg(FirstReg, ARM::dsub_0, RC);
    }
    Operands.push_back(ARMOperand::CreateVectorListAllLanes(FirstReg, Count,
                                                            (Spacing == 2),
                                                            S, E));
    break;
  case IndexedLane:
    Operands.push_back(ARMOperand::CreateVectorListIndexed(FirstReg, Count,
                                                           LaneIndex,
                                                           (Spacing == 2),
                                                           S, E));
    break;
  }
  return MatchOperand_Success;
}

/// parseMemBarrierOptOperand - Try to parse DSB/DMB data barrier options.
OperandMatchResultTy
ARMAsmParser::parseMemBarrierOptOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  unsigned Opt;

  if (Tok.is(AsmToken::Identifier)) {
    StringRef OptStr = Tok.getString();

    Opt = StringSwitch<unsigned>(OptStr.slice(0, OptStr.size()).lower())
      .Case("sy",    ARM_MB::SY)
      .Case("st",    ARM_MB::ST)
      .Case("ld",    ARM_MB::LD)
      .Case("sh",    ARM_MB::ISH)
      .Case("ish",   ARM_MB::ISH)
      .Case("shst",  ARM_MB::ISHST)
      .Case("ishst", ARM_MB::ISHST)
      .Case("ishld", ARM_MB::ISHLD)
      .Case("nsh",   ARM_MB::NSH)
      .Case("un",    ARM_MB::NSH)
      .Case("nshst", ARM_MB::NSHST)
      .Case("nshld", ARM_MB::NSHLD)
      .Case("unst",  ARM_MB::NSHST)
      .Case("osh",   ARM_MB::OSH)
      .Case("oshst", ARM_MB::OSHST)
      .Case("oshld", ARM_MB::OSHLD)
      .Default(~0U);

    // ishld, oshld, nshld and ld are only available from ARMv8.
    if (!hasV8Ops() && (Opt == ARM_MB::ISHLD || Opt == ARM_MB::OSHLD ||
                        Opt == ARM_MB::NSHLD || Opt == ARM_MB::LD))
      Opt = ~0U;

    if (Opt == ~0U)
      return MatchOperand_NoMatch;

    Parser.Lex(); // Eat identifier token.
  } else if (Tok.is(AsmToken::Hash) ||
             Tok.is(AsmToken::Dollar) ||
             Tok.is(AsmToken::Integer)) {
    if (Parser.getTok().isNot(AsmToken::Integer))
      Parser.Lex(); // Eat '#' or '$'.
    SMLoc Loc = Parser.getTok().getLoc();

    const MCExpr *MemBarrierID;
    if (getParser().parseExpression(MemBarrierID)) {
      Error(Loc, "illegal expression");
      return MatchOperand_ParseFail;
    }

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(MemBarrierID);
    if (!CE) {
      Error(Loc, "constant expression expected");
      return MatchOperand_ParseFail;
    }

    int Val = CE->getValue();
    if (Val & ~0xf) {
      Error(Loc, "immediate value out of range");
      return MatchOperand_ParseFail;
    }

    Opt = ARM_MB::RESERVED_0 + Val;
  } else
    return MatchOperand_ParseFail;

  Operands.push_back(ARMOperand::CreateMemBarrierOpt((ARM_MB::MemBOpt)Opt, S));
  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parseTraceSyncBarrierOptOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();

  if (Tok.isNot(AsmToken::Identifier))
     return MatchOperand_NoMatch;

  if (!Tok.getString().equals_lower("csync"))
    return MatchOperand_NoMatch;

  Parser.Lex(); // Eat identifier token.

  Operands.push_back(ARMOperand::CreateTraceSyncBarrierOpt(ARM_TSB::CSYNC, S));
  return MatchOperand_Success;
}

/// parseInstSyncBarrierOptOperand - Try to parse ISB inst sync barrier options.
OperandMatchResultTy
ARMAsmParser::parseInstSyncBarrierOptOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  unsigned Opt;

  if (Tok.is(AsmToken::Identifier)) {
    StringRef OptStr = Tok.getString();

    if (OptStr.equals_lower("sy"))
      Opt = ARM_ISB::SY;
    else
      return MatchOperand_NoMatch;

    Parser.Lex(); // Eat identifier token.
  } else if (Tok.is(AsmToken::Hash) ||
             Tok.is(AsmToken::Dollar) ||
             Tok.is(AsmToken::Integer)) {
    if (Parser.getTok().isNot(AsmToken::Integer))
      Parser.Lex(); // Eat '#' or '$'.
    SMLoc Loc = Parser.getTok().getLoc();

    const MCExpr *ISBarrierID;
    if (getParser().parseExpression(ISBarrierID)) {
      Error(Loc, "illegal expression");
      return MatchOperand_ParseFail;
    }

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ISBarrierID);
    if (!CE) {
      Error(Loc, "constant expression expected");
      return MatchOperand_ParseFail;
    }

    int Val = CE->getValue();
    if (Val & ~0xf) {
      Error(Loc, "immediate value out of range");
      return MatchOperand_ParseFail;
    }

    Opt = ARM_ISB::RESERVED_0 + Val;
  } else
    return MatchOperand_ParseFail;

  Operands.push_back(ARMOperand::CreateInstSyncBarrierOpt(
          (ARM_ISB::InstSyncBOpt)Opt, S));
  return MatchOperand_Success;
}


/// parseProcIFlagsOperand - Try to parse iflags from CPS instruction.
OperandMatchResultTy
ARMAsmParser::parseProcIFlagsOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return MatchOperand_NoMatch;
  StringRef IFlagsStr = Tok.getString();

  // An iflags string of "none" is interpreted to mean that none of the AIF
  // bits are set.  Not a terribly useful instruction, but a valid encoding.
  unsigned IFlags = 0;
  if (IFlagsStr != "none") {
        for (int i = 0, e = IFlagsStr.size(); i != e; ++i) {
      unsigned Flag = StringSwitch<unsigned>(IFlagsStr.substr(i, 1).lower())
        .Case("a", ARM_PROC::A)
        .Case("i", ARM_PROC::I)
        .Case("f", ARM_PROC::F)
        .Default(~0U);

      // If some specific iflag is already set, it means that some letter is
      // present more than once, this is not acceptable.
      if (Flag == ~0U || (IFlags & Flag))
        return MatchOperand_NoMatch;

      IFlags |= Flag;
    }
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(ARMOperand::CreateProcIFlags((ARM_PROC::IFlags)IFlags, S));
  return MatchOperand_Success;
}

/// parseMSRMaskOperand - Try to parse mask flags from MSR instruction.
OperandMatchResultTy
ARMAsmParser::parseMSRMaskOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();

  if (Tok.is(AsmToken::Integer)) {
    int64_t Val = Tok.getIntVal();
    if (Val > 255 || Val < 0) {
      return MatchOperand_NoMatch;
    }
    unsigned SYSmvalue = Val & 0xFF;
    Parser.Lex();
    Operands.push_back(ARMOperand::CreateMSRMask(SYSmvalue, S));
    return MatchOperand_Success;
  }

  if (!Tok.is(AsmToken::Identifier))
    return MatchOperand_NoMatch;
  StringRef Mask = Tok.getString();

  if (isMClass()) {
    auto TheReg = ARMSysReg::lookupMClassSysRegByName(Mask.lower());
    if (!TheReg || !TheReg->hasRequiredFeatures(getSTI().getFeatureBits()))
      return MatchOperand_NoMatch;

    unsigned SYSmvalue = TheReg->Encoding & 0xFFF;

    Parser.Lex(); // Eat identifier token.
    Operands.push_back(ARMOperand::CreateMSRMask(SYSmvalue, S));
    return MatchOperand_Success;
  }

  // Split spec_reg from flag, example: CPSR_sxf => "CPSR" and "sxf"
  size_t Start = 0, Next = Mask.find('_');
  StringRef Flags = "";
  std::string SpecReg = Mask.slice(Start, Next).lower();
  if (Next != StringRef::npos)
    Flags = Mask.slice(Next+1, Mask.size());

  // FlagsVal contains the complete mask:
  // 3-0: Mask
  // 4: Special Reg (cpsr, apsr => 0; spsr => 1)
  unsigned FlagsVal = 0;

  if (SpecReg == "apsr") {
    FlagsVal = StringSwitch<unsigned>(Flags)
    .Case("nzcvq",  0x8) // same as CPSR_f
    .Case("g",      0x4) // same as CPSR_s
    .Case("nzcvqg", 0xc) // same as CPSR_fs
    .Default(~0U);

    if (FlagsVal == ~0U) {
      if (!Flags.empty())
        return MatchOperand_NoMatch;
      else
        FlagsVal = 8; // No flag
    }
  } else if (SpecReg == "cpsr" || SpecReg == "spsr") {
    // cpsr_all is an alias for cpsr_fc, as is plain cpsr.
    if (Flags == "all" || Flags == "")
      Flags = "fc";
    for (int i = 0, e = Flags.size(); i != e; ++i) {
      unsigned Flag = StringSwitch<unsigned>(Flags.substr(i, 1))
      .Case("c", 1)
      .Case("x", 2)
      .Case("s", 4)
      .Case("f", 8)
      .Default(~0U);

      // If some specific flag is already set, it means that some letter is
      // present more than once, this is not acceptable.
      if (Flag == ~0U || (FlagsVal & Flag))
        return MatchOperand_NoMatch;
      FlagsVal |= Flag;
    }
  } else // No match for special register.
    return MatchOperand_NoMatch;

  // Special register without flags is NOT equivalent to "fc" flags.
  // NOTE: This is a divergence from gas' behavior.  Uncommenting the following
  // two lines would enable gas compatibility at the expense of breaking
  // round-tripping.
  //
  // if (!FlagsVal)
  //  FlagsVal = 0x9;

  // Bit 4: Special Reg (cpsr, apsr => 0; spsr => 1)
  if (SpecReg == "spsr")
    FlagsVal |= 16;

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(ARMOperand::CreateMSRMask(FlagsVal, S));
  return MatchOperand_Success;
}

/// parseBankedRegOperand - Try to parse a banked register (e.g. "lr_irq") for
/// use in the MRS/MSR instructions added to support virtualization.
OperandMatchResultTy
ARMAsmParser::parseBankedRegOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return MatchOperand_NoMatch;
  StringRef RegName = Tok.getString();

  auto TheReg = ARMBankedReg::lookupBankedRegByName(RegName.lower());
  if (!TheReg)
    return MatchOperand_NoMatch;
  unsigned Encoding = TheReg->Encoding;

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(ARMOperand::CreateBankedReg(Encoding, S));
  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parsePKHImm(OperandVector &Operands, StringRef Op, int Low,
                          int High) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) {
    Error(Parser.getTok().getLoc(), Op + " operand expected.");
    return MatchOperand_ParseFail;
  }
  StringRef ShiftName = Tok.getString();
  std::string LowerOp = Op.lower();
  std::string UpperOp = Op.upper();
  if (ShiftName != LowerOp && ShiftName != UpperOp) {
    Error(Parser.getTok().getLoc(), Op + " operand expected.");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat shift type token.

  // There must be a '#' and a shift amount.
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar)) {
    Error(Parser.getTok().getLoc(), "'#' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.

  const MCExpr *ShiftAmount;
  SMLoc Loc = Parser.getTok().getLoc();
  SMLoc EndLoc;
  if (getParser().parseExpression(ShiftAmount, EndLoc)) {
    Error(Loc, "illegal expression");
    return MatchOperand_ParseFail;
  }
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ShiftAmount);
  if (!CE) {
    Error(Loc, "constant expression expected");
    return MatchOperand_ParseFail;
  }
  int Val = CE->getValue();
  if (Val < Low || Val > High) {
    Error(Loc, "immediate value out of range");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(ARMOperand::CreateImm(CE, Loc, EndLoc));

  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parseSetEndImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();
  if (Tok.isNot(AsmToken::Identifier)) {
    Error(S, "'be' or 'le' operand expected");
    return MatchOperand_ParseFail;
  }
  int Val = StringSwitch<int>(Tok.getString().lower())
    .Case("be", 1)
    .Case("le", 0)
    .Default(-1);
  Parser.Lex(); // Eat the token.

  if (Val == -1) {
    Error(S, "'be' or 'le' operand expected");
    return MatchOperand_ParseFail;
  }
  Operands.push_back(ARMOperand::CreateImm(MCConstantExpr::create(Val,
                                                                  getContext()),
                                           S, Tok.getEndLoc()));
  return MatchOperand_Success;
}

/// parseShifterImm - Parse the shifter immediate operand for SSAT/USAT
/// instructions. Legal values are:
///     lsl #n  'n' in [0,31]
///     asr #n  'n' in [1,32]
///             n == 32 encoded as n == 0.
OperandMatchResultTy
ARMAsmParser::parseShifterImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();
  if (Tok.isNot(AsmToken::Identifier)) {
    Error(S, "shift operator 'asr' or 'lsl' expected");
    return MatchOperand_ParseFail;
  }
  StringRef ShiftName = Tok.getString();
  bool isASR;
  if (ShiftName == "lsl" || ShiftName == "LSL")
    isASR = false;
  else if (ShiftName == "asr" || ShiftName == "ASR")
    isASR = true;
  else {
    Error(S, "shift operator 'asr' or 'lsl' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat the operator.

  // A '#' and a shift amount.
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar)) {
    Error(Parser.getTok().getLoc(), "'#' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.
  SMLoc ExLoc = Parser.getTok().getLoc();

  const MCExpr *ShiftAmount;
  SMLoc EndLoc;
  if (getParser().parseExpression(ShiftAmount, EndLoc)) {
    Error(ExLoc, "malformed shift expression");
    return MatchOperand_ParseFail;
  }
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ShiftAmount);
  if (!CE) {
    Error(ExLoc, "shift amount must be an immediate");
    return MatchOperand_ParseFail;
  }

  int64_t Val = CE->getValue();
  if (isASR) {
    // Shift amount must be in [1,32]
    if (Val < 1 || Val > 32) {
      Error(ExLoc, "'asr' shift amount must be in range [1,32]");
      return MatchOperand_ParseFail;
    }
    // asr #32 encoded as asr #0, but is not allowed in Thumb2 mode.
    if (isThumb() && Val == 32) {
      Error(ExLoc, "'asr #32' shift amount not allowed in Thumb mode");
      return MatchOperand_ParseFail;
    }
    if (Val == 32) Val = 0;
  } else {
    // Shift amount must be in [1,32]
    if (Val < 0 || Val > 31) {
      Error(ExLoc, "'lsr' shift amount must be in range [0,31]");
      return MatchOperand_ParseFail;
    }
  }

  Operands.push_back(ARMOperand::CreateShifterImm(isASR, Val, S, EndLoc));

  return MatchOperand_Success;
}

/// parseRotImm - Parse the shifter immediate operand for SXTB/UXTB family
/// of instructions. Legal values are:
///     ror #n  'n' in {0, 8, 16, 24}
OperandMatchResultTy
ARMAsmParser::parseRotImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();
  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;
  StringRef ShiftName = Tok.getString();
  if (ShiftName != "ror" && ShiftName != "ROR")
    return MatchOperand_NoMatch;
  Parser.Lex(); // Eat the operator.

  // A '#' and a rotate amount.
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar)) {
    Error(Parser.getTok().getLoc(), "'#' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.
  SMLoc ExLoc = Parser.getTok().getLoc();

  const MCExpr *ShiftAmount;
  SMLoc EndLoc;
  if (getParser().parseExpression(ShiftAmount, EndLoc)) {
    Error(ExLoc, "malformed rotate expression");
    return MatchOperand_ParseFail;
  }
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ShiftAmount);
  if (!CE) {
    Error(ExLoc, "rotate amount must be an immediate");
    return MatchOperand_ParseFail;
  }

  int64_t Val = CE->getValue();
  // Shift amount must be in {0, 8, 16, 24} (0 is undocumented extension)
  // normally, zero is represented in asm by omitting the rotate operand
  // entirely.
  if (Val != 8 && Val != 16 && Val != 24 && Val != 0) {
    Error(ExLoc, "'ror' rotate amount must be 8, 16, or 24");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(ARMOperand::CreateRotImm(Val, S, EndLoc));

  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parseModImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  MCAsmLexer &Lexer = getLexer();
  int64_t Imm1, Imm2;

  SMLoc S = Parser.getTok().getLoc();

  // 1) A mod_imm operand can appear in the place of a register name:
  //   add r0, #mod_imm
  //   add r0, r0, #mod_imm
  // to correctly handle the latter, we bail out as soon as we see an
  // identifier.
  //
  // 2) Similarly, we do not want to parse into complex operands:
  //   mov r0, #mod_imm
  //   mov r0, :lower16:(_foo)
  if (Parser.getTok().is(AsmToken::Identifier) ||
      Parser.getTok().is(AsmToken::Colon))
    return MatchOperand_NoMatch;

  // Hash (dollar) is optional as per the ARMARM
  if (Parser.getTok().is(AsmToken::Hash) ||
      Parser.getTok().is(AsmToken::Dollar)) {
    // Avoid parsing into complex operands (#:)
    if (Lexer.peekTok().is(AsmToken::Colon))
      return MatchOperand_NoMatch;

    // Eat the hash (dollar)
    Parser.Lex();
  }

  SMLoc Sx1, Ex1;
  Sx1 = Parser.getTok().getLoc();
  const MCExpr *Imm1Exp;
  if (getParser().parseExpression(Imm1Exp, Ex1)) {
    Error(Sx1, "malformed expression");
    return MatchOperand_ParseFail;
  }

  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm1Exp);

  if (CE) {
    // Immediate must fit within 32-bits
    Imm1 = CE->getValue();
    int Enc = ARM_AM::getSOImmVal(Imm1);
    if (Enc != -1 && Parser.getTok().is(AsmToken::EndOfStatement)) {
      // We have a match!
      Operands.push_back(ARMOperand::CreateModImm((Enc & 0xFF),
                                                  (Enc & 0xF00) >> 7,
                                                  Sx1, Ex1));
      return MatchOperand_Success;
    }

    // We have parsed an immediate which is not for us, fallback to a plain
    // immediate. This can happen for instruction aliases. For an example,
    // ARMInstrInfo.td defines the alias [mov <-> mvn] which can transform
    // a mov (mvn) with a mod_imm_neg/mod_imm_not operand into the opposite
    // instruction with a mod_imm operand. The alias is defined such that the
    // parser method is shared, that's why we have to do this here.
    if (Parser.getTok().is(AsmToken::EndOfStatement)) {
      Operands.push_back(ARMOperand::CreateImm(Imm1Exp, Sx1, Ex1));
      return MatchOperand_Success;
    }
  } else {
    // Operands like #(l1 - l2) can only be evaluated at a later stage (via an
    // MCFixup). Fallback to a plain immediate.
    Operands.push_back(ARMOperand::CreateImm(Imm1Exp, Sx1, Ex1));
    return MatchOperand_Success;
  }

  // From this point onward, we expect the input to be a (#bits, #rot) pair
  if (Parser.getTok().isNot(AsmToken::Comma)) {
    Error(Sx1, "expected modified immediate operand: #[0, 255], #even[0-30]");
    return MatchOperand_ParseFail;
  }

  if (Imm1 & ~0xFF) {
    Error(Sx1, "immediate operand must a number in the range [0, 255]");
    return MatchOperand_ParseFail;
  }

  // Eat the comma
  Parser.Lex();

  // Repeat for #rot
  SMLoc Sx2, Ex2;
  Sx2 = Parser.getTok().getLoc();

  // Eat the optional hash (dollar)
  if (Parser.getTok().is(AsmToken::Hash) ||
      Parser.getTok().is(AsmToken::Dollar))
    Parser.Lex();

  const MCExpr *Imm2Exp;
  if (getParser().parseExpression(Imm2Exp, Ex2)) {
    Error(Sx2, "malformed expression");
    return MatchOperand_ParseFail;
  }

  CE = dyn_cast<MCConstantExpr>(Imm2Exp);

  if (CE) {
    Imm2 = CE->getValue();
    if (!(Imm2 & ~0x1E)) {
      // We have a match!
      Operands.push_back(ARMOperand::CreateModImm(Imm1, Imm2, S, Ex2));
      return MatchOperand_Success;
    }
    Error(Sx2, "immediate operand must an even number in the range [0, 30]");
    return MatchOperand_ParseFail;
  } else {
    Error(Sx2, "constant expression expected");
    return MatchOperand_ParseFail;
  }
}

OperandMatchResultTy
ARMAsmParser::parseBitfield(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = Parser.getTok().getLoc();
  // The bitfield descriptor is really two operands, the LSB and the width.
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar)) {
    Error(Parser.getTok().getLoc(), "'#' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.

  const MCExpr *LSBExpr;
  SMLoc E = Parser.getTok().getLoc();
  if (getParser().parseExpression(LSBExpr)) {
    Error(E, "malformed immediate expression");
    return MatchOperand_ParseFail;
  }
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(LSBExpr);
  if (!CE) {
    Error(E, "'lsb' operand must be an immediate");
    return MatchOperand_ParseFail;
  }

  int64_t LSB = CE->getValue();
  // The LSB must be in the range [0,31]
  if (LSB < 0 || LSB > 31) {
    Error(E, "'lsb' operand must be in the range [0,31]");
    return MatchOperand_ParseFail;
  }
  E = Parser.getTok().getLoc();

  // Expect another immediate operand.
  if (Parser.getTok().isNot(AsmToken::Comma)) {
    Error(Parser.getTok().getLoc(), "too few operands");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar)) {
    Error(Parser.getTok().getLoc(), "'#' expected");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat hash token.

  const MCExpr *WidthExpr;
  SMLoc EndLoc;
  if (getParser().parseExpression(WidthExpr, EndLoc)) {
    Error(E, "malformed immediate expression");
    return MatchOperand_ParseFail;
  }
  CE = dyn_cast<MCConstantExpr>(WidthExpr);
  if (!CE) {
    Error(E, "'width' operand must be an immediate");
    return MatchOperand_ParseFail;
  }

  int64_t Width = CE->getValue();
  // The LSB must be in the range [1,32-lsb]
  if (Width < 1 || Width > 32 - LSB) {
    Error(E, "'width' operand must be in the range [1,32-lsb]");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(ARMOperand::CreateBitfield(LSB, Width, S, EndLoc));

  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parsePostIdxReg(OperandVector &Operands) {
  // Check for a post-index addressing register operand. Specifically:
  // postidx_reg := '+' register {, shift}
  //              | '-' register {, shift}
  //              | register {, shift}

  // This method must return MatchOperand_NoMatch without consuming any tokens
  // in the case where there is no match, as other alternatives take other
  // parse methods.
  MCAsmParser &Parser = getParser();
  AsmToken Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();
  bool haveEaten = false;
  bool isAdd = true;
  if (Tok.is(AsmToken::Plus)) {
    Parser.Lex(); // Eat the '+' token.
    haveEaten = true;
  } else if (Tok.is(AsmToken::Minus)) {
    Parser.Lex(); // Eat the '-' token.
    isAdd = false;
    haveEaten = true;
  }

  SMLoc E = Parser.getTok().getEndLoc();
  int Reg = tryParseRegister();
  if (Reg == -1) {
    if (!haveEaten)
      return MatchOperand_NoMatch;
    Error(Parser.getTok().getLoc(), "register expected");
    return MatchOperand_ParseFail;
  }

  ARM_AM::ShiftOpc ShiftTy = ARM_AM::no_shift;
  unsigned ShiftImm = 0;
  if (Parser.getTok().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the ','.
    if (parseMemRegOffsetShift(ShiftTy, ShiftImm))
      return MatchOperand_ParseFail;

    // FIXME: Only approximates end...may include intervening whitespace.
    E = Parser.getTok().getLoc();
  }

  Operands.push_back(ARMOperand::CreatePostIdxReg(Reg, isAdd, ShiftTy,
                                                  ShiftImm, S, E));

  return MatchOperand_Success;
}

OperandMatchResultTy
ARMAsmParser::parseAM3Offset(OperandVector &Operands) {
  // Check for a post-index addressing register operand. Specifically:
  // am3offset := '+' register
  //              | '-' register
  //              | register
  //              | # imm
  //              | # + imm
  //              | # - imm

  // This method must return MatchOperand_NoMatch without consuming any tokens
  // in the case where there is no match, as other alternatives take other
  // parse methods.
  MCAsmParser &Parser = getParser();
  AsmToken Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();

  // Do immediates first, as we always parse those if we have a '#'.
  if (Parser.getTok().is(AsmToken::Hash) ||
      Parser.getTok().is(AsmToken::Dollar)) {
    Parser.Lex(); // Eat '#' or '$'.
    // Explicitly look for a '-', as we need to encode negative zero
    // differently.
    bool isNegative = Parser.getTok().is(AsmToken::Minus);
    const MCExpr *Offset;
    SMLoc E;
    if (getParser().parseExpression(Offset, E))
      return MatchOperand_ParseFail;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Offset);
    if (!CE) {
      Error(S, "constant expression expected");
      return MatchOperand_ParseFail;
    }
    // Negative zero is encoded as the flag value
    // std::numeric_limits<int32_t>::min().
    int32_t Val = CE->getValue();
    if (isNegative && Val == 0)
      Val = std::numeric_limits<int32_t>::min();

    Operands.push_back(
      ARMOperand::CreateImm(MCConstantExpr::create(Val, getContext()), S, E));

    return MatchOperand_Success;
  }

  bool haveEaten = false;
  bool isAdd = true;
  if (Tok.is(AsmToken::Plus)) {
    Parser.Lex(); // Eat the '+' token.
    haveEaten = true;
  } else if (Tok.is(AsmToken::Minus)) {
    Parser.Lex(); // Eat the '-' token.
    isAdd = false;
    haveEaten = true;
  }

  Tok = Parser.getTok();
  int Reg = tryParseRegister();
  if (Reg == -1) {
    if (!haveEaten)
      return MatchOperand_NoMatch;
    Error(Tok.getLoc(), "register expected");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(ARMOperand::CreatePostIdxReg(Reg, isAdd, ARM_AM::no_shift,
                                                  0, S, Tok.getEndLoc()));

  return MatchOperand_Success;
}

/// Convert parsed operands to MCInst.  Needed here because this instruction
/// only has two register operands, but multiplication is commutative so
/// assemblers should accept both "mul rD, rN, rD" and "mul rD, rD, rN".
void ARMAsmParser::cvtThumbMultiply(MCInst &Inst,
                                    const OperandVector &Operands) {
  ((ARMOperand &)*Operands[3]).addRegOperands(Inst, 1);
  ((ARMOperand &)*Operands[1]).addCCOutOperands(Inst, 1);
  // If we have a three-operand form, make sure to set Rn to be the operand
  // that isn't the same as Rd.
  unsigned RegOp = 4;
  if (Operands.size() == 6 &&
      ((ARMOperand &)*Operands[4]).getReg() ==
          ((ARMOperand &)*Operands[3]).getReg())
    RegOp = 5;
  ((ARMOperand &)*Operands[RegOp]).addRegOperands(Inst, 1);
  Inst.addOperand(Inst.getOperand(0));
  ((ARMOperand &)*Operands[2]).addCondCodeOperands(Inst, 2);
}

void ARMAsmParser::cvtThumbBranches(MCInst &Inst,
                                    const OperandVector &Operands) {
  int CondOp = -1, ImmOp = -1;
  switch(Inst.getOpcode()) {
    case ARM::tB:
    case ARM::tBcc:  CondOp = 1; ImmOp = 2; break;

    case ARM::t2B:
    case ARM::t2Bcc: CondOp = 1; ImmOp = 3; break;

    default: llvm_unreachable("Unexpected instruction in cvtThumbBranches");
  }
  // first decide whether or not the branch should be conditional
  // by looking at it's location relative to an IT block
  if(inITBlock()) {
    // inside an IT block we cannot have any conditional branches. any
    // such instructions needs to be converted to unconditional form
    switch(Inst.getOpcode()) {
      case ARM::tBcc: Inst.setOpcode(ARM::tB); break;
      case ARM::t2Bcc: Inst.setOpcode(ARM::t2B); break;
    }
  } else {
    // outside IT blocks we can only have unconditional branches with AL
    // condition code or conditional branches with non-AL condition code
    unsigned Cond = static_cast<ARMOperand &>(*Operands[CondOp]).getCondCode();
    switch(Inst.getOpcode()) {
      case ARM::tB:
      case ARM::tBcc:
        Inst.setOpcode(Cond == ARMCC::AL ? ARM::tB : ARM::tBcc);
        break;
      case ARM::t2B:
      case ARM::t2Bcc:
        Inst.setOpcode(Cond == ARMCC::AL ? ARM::t2B : ARM::t2Bcc);
        break;
    }
  }

  // now decide on encoding size based on branch target range
  switch(Inst.getOpcode()) {
    // classify tB as either t2B or t1B based on range of immediate operand
    case ARM::tB: {
      ARMOperand &op = static_cast<ARMOperand &>(*Operands[ImmOp]);
      if (!op.isSignedOffset<11, 1>() && isThumb() && hasV8MBaseline())
        Inst.setOpcode(ARM::t2B);
      break;
    }
    // classify tBcc as either t2Bcc or t1Bcc based on range of immediate operand
    case ARM::tBcc: {
      ARMOperand &op = static_cast<ARMOperand &>(*Operands[ImmOp]);
      if (!op.isSignedOffset<8, 1>() && isThumb() && hasV8MBaseline())
        Inst.setOpcode(ARM::t2Bcc);
      break;
    }
  }
  ((ARMOperand &)*Operands[ImmOp]).addImmOperands(Inst, 1);
  ((ARMOperand &)*Operands[CondOp]).addCondCodeOperands(Inst, 2);
}

/// Parse an ARM memory expression, return false if successful else return true
/// or an error.  The first token must be a '[' when called.
bool ARMAsmParser::parseMemory(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S, E;
  if (Parser.getTok().isNot(AsmToken::LBrac))
    return TokError("Token is not a Left Bracket");
  S = Parser.getTok().getLoc();
  Parser.Lex(); // Eat left bracket token.

  const AsmToken &BaseRegTok = Parser.getTok();
  int BaseRegNum = tryParseRegister();
  if (BaseRegNum == -1)
    return Error(BaseRegTok.getLoc(), "register expected");

  // The next token must either be a comma, a colon or a closing bracket.
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Colon) && !Tok.is(AsmToken::Comma) &&
      !Tok.is(AsmToken::RBrac))
    return Error(Tok.getLoc(), "malformed memory operand");

  if (Tok.is(AsmToken::RBrac)) {
    E = Tok.getEndLoc();
    Parser.Lex(); // Eat right bracket token.

    Operands.push_back(ARMOperand::CreateMem(BaseRegNum, nullptr, 0,
                                             ARM_AM::no_shift, 0, 0, false,
                                             S, E));

    // If there's a pre-indexing writeback marker, '!', just add it as a token
    // operand. It's rather odd, but syntactically valid.
    if (Parser.getTok().is(AsmToken::Exclaim)) {
      Operands.push_back(ARMOperand::CreateToken("!",Parser.getTok().getLoc()));
      Parser.Lex(); // Eat the '!'.
    }

    return false;
  }

  assert((Tok.is(AsmToken::Colon) || Tok.is(AsmToken::Comma)) &&
         "Lost colon or comma in memory operand?!");
  if (Tok.is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the comma.
  }

  // If we have a ':', it's an alignment specifier.
  if (Parser.getTok().is(AsmToken::Colon)) {
    Parser.Lex(); // Eat the ':'.
    E = Parser.getTok().getLoc();
    SMLoc AlignmentLoc = Tok.getLoc();

    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
     return true;

    // The expression has to be a constant. Memory references with relocations
    // don't come through here, as they use the <label> forms of the relevant
    // instructions.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
    if (!CE)
      return Error (E, "constant expression expected");

    unsigned Align = 0;
    switch (CE->getValue()) {
    default:
      return Error(E,
                   "alignment specifier must be 16, 32, 64, 128, or 256 bits");
    case 16:  Align = 2; break;
    case 32:  Align = 4; break;
    case 64:  Align = 8; break;
    case 128: Align = 16; break;
    case 256: Align = 32; break;
    }

    // Now we should have the closing ']'
    if (Parser.getTok().isNot(AsmToken::RBrac))
      return Error(Parser.getTok().getLoc(), "']' expected");
    E = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat right bracket token.

    // Don't worry about range checking the value here. That's handled by
    // the is*() predicates.
    Operands.push_back(ARMOperand::CreateMem(BaseRegNum, nullptr, 0,
                                             ARM_AM::no_shift, 0, Align,
                                             false, S, E, AlignmentLoc));

    // If there's a pre-indexing writeback marker, '!', just add it as a token
    // operand.
    if (Parser.getTok().is(AsmToken::Exclaim)) {
      Operands.push_back(ARMOperand::CreateToken("!",Parser.getTok().getLoc()));
      Parser.Lex(); // Eat the '!'.
    }

    return false;
  }

  // If we have a '#', it's an immediate offset, else assume it's a register
  // offset. Be friendly and also accept a plain integer (without a leading
  // hash) for gas compatibility.
  if (Parser.getTok().is(AsmToken::Hash) ||
      Parser.getTok().is(AsmToken::Dollar) ||
      Parser.getTok().is(AsmToken::Integer)) {
    if (Parser.getTok().isNot(AsmToken::Integer))
      Parser.Lex(); // Eat '#' or '$'.
    E = Parser.getTok().getLoc();

    bool isNegative = getParser().getTok().is(AsmToken::Minus);
    const MCExpr *Offset;
    if (getParser().parseExpression(Offset))
     return true;

    // The expression has to be a constant. Memory references with relocations
    // don't come through here, as they use the <label> forms of the relevant
    // instructions.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Offset);
    if (!CE)
      return Error (E, "constant expression expected");

    // If the constant was #-0, represent it as
    // std::numeric_limits<int32_t>::min().
    int32_t Val = CE->getValue();
    if (isNegative && Val == 0)
      CE = MCConstantExpr::create(std::numeric_limits<int32_t>::min(),
                                  getContext());

    // Now we should have the closing ']'
    if (Parser.getTok().isNot(AsmToken::RBrac))
      return Error(Parser.getTok().getLoc(), "']' expected");
    E = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat right bracket token.

    // Don't worry about range checking the value here. That's handled by
    // the is*() predicates.
    Operands.push_back(ARMOperand::CreateMem(BaseRegNum, CE, 0,
                                             ARM_AM::no_shift, 0, 0,
                                             false, S, E));

    // If there's a pre-indexing writeback marker, '!', just add it as a token
    // operand.
    if (Parser.getTok().is(AsmToken::Exclaim)) {
      Operands.push_back(ARMOperand::CreateToken("!",Parser.getTok().getLoc()));
      Parser.Lex(); // Eat the '!'.
    }

    return false;
  }

  // The register offset is optionally preceded by a '+' or '-'
  bool isNegative = false;
  if (Parser.getTok().is(AsmToken::Minus)) {
    isNegative = true;
    Parser.Lex(); // Eat the '-'.
  } else if (Parser.getTok().is(AsmToken::Plus)) {
    // Nothing to do.
    Parser.Lex(); // Eat the '+'.
  }

  E = Parser.getTok().getLoc();
  int OffsetRegNum = tryParseRegister();
  if (OffsetRegNum == -1)
    return Error(E, "register expected");

  // If there's a shift operator, handle it.
  ARM_AM::ShiftOpc ShiftType = ARM_AM::no_shift;
  unsigned ShiftImm = 0;
  if (Parser.getTok().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the ','.
    if (parseMemRegOffsetShift(ShiftType, ShiftImm))
      return true;
  }

  // Now we should have the closing ']'
  if (Parser.getTok().isNot(AsmToken::RBrac))
    return Error(Parser.getTok().getLoc(), "']' expected");
  E = Parser.getTok().getEndLoc();
  Parser.Lex(); // Eat right bracket token.

  Operands.push_back(ARMOperand::CreateMem(BaseRegNum, nullptr, OffsetRegNum,
                                           ShiftType, ShiftImm, 0, isNegative,
                                           S, E));

  // If there's a pre-indexing writeback marker, '!', just add it as a token
  // operand.
  if (Parser.getTok().is(AsmToken::Exclaim)) {
    Operands.push_back(ARMOperand::CreateToken("!",Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the '!'.
  }

  return false;
}

/// parseMemRegOffsetShift - one of these two:
///   ( lsl | lsr | asr | ror ) , # shift_amount
///   rrx
/// return true if it parses a shift otherwise it returns false.
bool ARMAsmParser::parseMemRegOffsetShift(ARM_AM::ShiftOpc &St,
                                          unsigned &Amount) {
  MCAsmParser &Parser = getParser();
  SMLoc Loc = Parser.getTok().getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return Error(Loc, "illegal shift operator");
  StringRef ShiftName = Tok.getString();
  if (ShiftName == "lsl" || ShiftName == "LSL" ||
      ShiftName == "asl" || ShiftName == "ASL")
    St = ARM_AM::lsl;
  else if (ShiftName == "lsr" || ShiftName == "LSR")
    St = ARM_AM::lsr;
  else if (ShiftName == "asr" || ShiftName == "ASR")
    St = ARM_AM::asr;
  else if (ShiftName == "ror" || ShiftName == "ROR")
    St = ARM_AM::ror;
  else if (ShiftName == "rrx" || ShiftName == "RRX")
    St = ARM_AM::rrx;
  else
    return Error(Loc, "illegal shift operator");
  Parser.Lex(); // Eat shift type token.

  // rrx stands alone.
  Amount = 0;
  if (St != ARM_AM::rrx) {
    Loc = Parser.getTok().getLoc();
    // A '#' and a shift amount.
    const AsmToken &HashTok = Parser.getTok();
    if (HashTok.isNot(AsmToken::Hash) &&
        HashTok.isNot(AsmToken::Dollar))
      return Error(HashTok.getLoc(), "'#' expected");
    Parser.Lex(); // Eat hash token.

    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    // Range check the immediate.
    // lsl, ror: 0 <= imm <= 31
    // lsr, asr: 0 <= imm <= 32
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
    if (!CE)
      return Error(Loc, "shift amount must be an immediate");
    int64_t Imm = CE->getValue();
    if (Imm < 0 ||
        ((St == ARM_AM::lsl || St == ARM_AM::ror) && Imm > 31) ||
        ((St == ARM_AM::lsr || St == ARM_AM::asr) && Imm > 32))
      return Error(Loc, "immediate shift value out of range");
    // If <ShiftTy> #0, turn it into a no_shift.
    if (Imm == 0)
      St = ARM_AM::lsl;
    // For consistency, treat lsr #32 and asr #32 as having immediate value 0.
    if (Imm == 32)
      Imm = 0;
    Amount = Imm;
  }

  return false;
}

/// parseFPImm - A floating point immediate expression operand.
OperandMatchResultTy
ARMAsmParser::parseFPImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  // Anything that can accept a floating point constant as an operand
  // needs to go through here, as the regular parseExpression is
  // integer only.
  //
  // This routine still creates a generic Immediate operand, containing
  // a bitcast of the 64-bit floating point value. The various operands
  // that accept floats can check whether the value is valid for them
  // via the standard is*() predicates.

  SMLoc S = Parser.getTok().getLoc();

  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar))
    return MatchOperand_NoMatch;

  // Disambiguate the VMOV forms that can accept an FP immediate.
  // vmov.f32 <sreg>, #imm
  // vmov.f64 <dreg>, #imm
  // vmov.f32 <dreg>, #imm  @ vector f32x2
  // vmov.f32 <qreg>, #imm  @ vector f32x4
  //
  // There are also the NEON VMOV instructions which expect an
  // integer constant. Make sure we don't try to parse an FPImm
  // for these:
  // vmov.i{8|16|32|64} <dreg|qreg>, #imm
  ARMOperand &TyOp = static_cast<ARMOperand &>(*Operands[2]);
  bool isVmovf = TyOp.isToken() &&
                 (TyOp.getToken() == ".f32" || TyOp.getToken() == ".f64" ||
                  TyOp.getToken() == ".f16");
  ARMOperand &Mnemonic = static_cast<ARMOperand &>(*Operands[0]);
  bool isFconst = Mnemonic.isToken() && (Mnemonic.getToken() == "fconstd" ||
                                         Mnemonic.getToken() == "fconsts");
  if (!(isVmovf || isFconst))
    return MatchOperand_NoMatch;

  Parser.Lex(); // Eat '#' or '$'.

  // Handle negation, as that still comes through as a separate token.
  bool isNegative = false;
  if (Parser.getTok().is(AsmToken::Minus)) {
    isNegative = true;
    Parser.Lex();
  }
  const AsmToken &Tok = Parser.getTok();
  SMLoc Loc = Tok.getLoc();
  if (Tok.is(AsmToken::Real) && isVmovf) {
    APFloat RealVal(APFloat::IEEEsingle(), Tok.getString());
    uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
    // If we had a '-' in front, toggle the sign bit.
    IntVal ^= (uint64_t)isNegative << 31;
    Parser.Lex(); // Eat the token.
    Operands.push_back(ARMOperand::CreateImm(
          MCConstantExpr::create(IntVal, getContext()),
          S, Parser.getTok().getLoc()));
    return MatchOperand_Success;
  }
  // Also handle plain integers. Instructions which allow floating point
  // immediates also allow a raw encoded 8-bit value.
  if (Tok.is(AsmToken::Integer) && isFconst) {
    int64_t Val = Tok.getIntVal();
    Parser.Lex(); // Eat the token.
    if (Val > 255 || Val < 0) {
      Error(Loc, "encoded floating point value out of range");
      return MatchOperand_ParseFail;
    }
    float RealVal = ARM_AM::getFPImmFloat(Val);
    Val = APFloat(RealVal).bitcastToAPInt().getZExtValue();

    Operands.push_back(ARMOperand::CreateImm(
        MCConstantExpr::create(Val, getContext()), S,
        Parser.getTok().getLoc()));
    return MatchOperand_Success;
  }

  Error(Loc, "invalid floating point immediate");
  return MatchOperand_ParseFail;
}

/// Parse a arm instruction operand.  For now this parses the operand regardless
/// of the mnemonic.
bool ARMAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  MCAsmParser &Parser = getParser();
  SMLoc S, E;

  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  OperandMatchResultTy ResTy = MatchOperandParserImpl(Operands, Mnemonic);
  if (ResTy == MatchOperand_Success)
    return false;
  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy == MatchOperand_ParseFail)
    return true;

  switch (getLexer().getKind()) {
  default:
    Error(Parser.getTok().getLoc(), "unexpected token in operand");
    return true;
  case AsmToken::Identifier: {
    // If we've seen a branch mnemonic, the next operand must be a label.  This
    // is true even if the label is a register name.  So "br r1" means branch to
    // label "r1".
    bool ExpectLabel = Mnemonic == "b" || Mnemonic == "bl";
    if (!ExpectLabel) {
      if (!tryParseRegisterWithWriteBack(Operands))
        return false;
      int Res = tryParseShiftRegister(Operands);
      if (Res == 0) // success
        return false;
      else if (Res == -1) // irrecoverable error
        return true;
      // If this is VMRS, check for the apsr_nzcv operand.
      if (Mnemonic == "vmrs" &&
          Parser.getTok().getString().equals_lower("apsr_nzcv")) {
        S = Parser.getTok().getLoc();
        Parser.Lex();
        Operands.push_back(ARMOperand::CreateToken("APSR_nzcv", S));
        return false;
      }
    }

    // Fall though for the Identifier case that is not a register or a
    // special name.
    LLVM_FALLTHROUGH;
  }
  case AsmToken::LParen:  // parenthesized expressions like (_strcmp-4)
  case AsmToken::Integer: // things like 1f and 2b as a branch targets
  case AsmToken::String:  // quoted label names.
  case AsmToken::Dot: {   // . as a branch target
    // This was not a register so parse other operands that start with an
    // identifier (like labels) as expressions and create them as immediates.
    const MCExpr *IdVal;
    S = Parser.getTok().getLoc();
    if (getParser().parseExpression(IdVal))
      return true;
    E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    Operands.push_back(ARMOperand::CreateImm(IdVal, S, E));
    return false;
  }
  case AsmToken::LBrac:
    return parseMemory(Operands);
  case AsmToken::LCurly:
    return parseRegisterList(Operands);
  case AsmToken::Dollar:
  case AsmToken::Hash:
    // #42 -> immediate.
    S = Parser.getTok().getLoc();
    Parser.Lex();

    if (Parser.getTok().isNot(AsmToken::Colon)) {
      bool isNegative = Parser.getTok().is(AsmToken::Minus);
      const MCExpr *ImmVal;
      if (getParser().parseExpression(ImmVal))
        return true;
      const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ImmVal);
      if (CE) {
        int32_t Val = CE->getValue();
        if (isNegative && Val == 0)
          ImmVal = MCConstantExpr::create(std::numeric_limits<int32_t>::min(),
                                          getContext());
      }
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      Operands.push_back(ARMOperand::CreateImm(ImmVal, S, E));

      // There can be a trailing '!' on operands that we want as a separate
      // '!' Token operand. Handle that here. For example, the compatibility
      // alias for 'srsdb sp!, #imm' is 'srsdb #imm!'.
      if (Parser.getTok().is(AsmToken::Exclaim)) {
        Operands.push_back(ARMOperand::CreateToken(Parser.getTok().getString(),
                                                   Parser.getTok().getLoc()));
        Parser.Lex(); // Eat exclaim token
      }
      return false;
    }
    // w/ a ':' after the '#', it's just like a plain ':'.
    LLVM_FALLTHROUGH;

  case AsmToken::Colon: {
    S = Parser.getTok().getLoc();
    // ":lower16:" and ":upper16:" expression prefixes
    // FIXME: Check it's an expression prefix,
    // e.g. (FOO - :lower16:BAR) isn't legal.
    ARMMCExpr::VariantKind RefKind;
    if (parsePrefix(RefKind))
      return true;

    const MCExpr *SubExprVal;
    if (getParser().parseExpression(SubExprVal))
      return true;

    const MCExpr *ExprVal = ARMMCExpr::create(RefKind, SubExprVal,
                                              getContext());
    E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    Operands.push_back(ARMOperand::CreateImm(ExprVal, S, E));
    return false;
  }
  case AsmToken::Equal: {
    S = Parser.getTok().getLoc();
    if (Mnemonic != "ldr") // only parse for ldr pseudo (e.g. ldr r0, =val)
      return Error(S, "unexpected token in operand");
    Parser.Lex(); // Eat '='
    const MCExpr *SubExprVal;
    if (getParser().parseExpression(SubExprVal))
      return true;
    E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

    // execute-only: we assume that assembly programmers know what they are
    // doing and allow literal pool creation here
    Operands.push_back(ARMOperand::CreateConstantPoolImm(SubExprVal, S, E));
    return false;
  }
  }
}

// parsePrefix - Parse ARM 16-bit relocations expression prefix, i.e.
//  :lower16: and :upper16:.
bool ARMAsmParser::parsePrefix(ARMMCExpr::VariantKind &RefKind) {
  MCAsmParser &Parser = getParser();
  RefKind = ARMMCExpr::VK_ARM_None;

  // consume an optional '#' (GNU compatibility)
  if (getLexer().is(AsmToken::Hash))
    Parser.Lex();

  // :lower16: and :upper16: modifiers
  assert(getLexer().is(AsmToken::Colon) && "expected a :");
  Parser.Lex(); // Eat ':'

  if (getLexer().isNot(AsmToken::Identifier)) {
    Error(Parser.getTok().getLoc(), "expected prefix identifier in operand");
    return true;
  }

  enum {
    COFF = (1 << MCObjectFileInfo::IsCOFF),
    ELF = (1 << MCObjectFileInfo::IsELF),
    MACHO = (1 << MCObjectFileInfo::IsMachO),
    WASM = (1 << MCObjectFileInfo::IsWasm),
  };
  static const struct PrefixEntry {
    const char *Spelling;
    ARMMCExpr::VariantKind VariantKind;
    uint8_t SupportedFormats;
  } PrefixEntries[] = {
    { "lower16", ARMMCExpr::VK_ARM_LO16, COFF | ELF | MACHO },
    { "upper16", ARMMCExpr::VK_ARM_HI16, COFF | ELF | MACHO },
  };

  StringRef IDVal = Parser.getTok().getIdentifier();

  const auto &Prefix =
      std::find_if(std::begin(PrefixEntries), std::end(PrefixEntries),
                   [&IDVal](const PrefixEntry &PE) {
                      return PE.Spelling == IDVal;
                   });
  if (Prefix == std::end(PrefixEntries)) {
    Error(Parser.getTok().getLoc(), "unexpected prefix in operand");
    return true;
  }

  uint8_t CurrentFormat;
  switch (getContext().getObjectFileInfo()->getObjectFileType()) {
  case MCObjectFileInfo::IsMachO:
    CurrentFormat = MACHO;
    break;
  case MCObjectFileInfo::IsELF:
    CurrentFormat = ELF;
    break;
  case MCObjectFileInfo::IsCOFF:
    CurrentFormat = COFF;
    break;
  case MCObjectFileInfo::IsWasm:
    CurrentFormat = WASM;
    break;
  }

  if (~Prefix->SupportedFormats & CurrentFormat) {
    Error(Parser.getTok().getLoc(),
          "cannot represent relocation in the current file format");
    return true;
  }

  RefKind = Prefix->VariantKind;
  Parser.Lex();

  if (getLexer().isNot(AsmToken::Colon)) {
    Error(Parser.getTok().getLoc(), "unexpected token after prefix");
    return true;
  }
  Parser.Lex(); // Eat the last ':'

  return false;
}

/// Given a mnemonic, split out possible predication code and carry
/// setting letters to form a canonical mnemonic and flags.
//
// FIXME: Would be nice to autogen this.
// FIXME: This is a bit of a maze of special cases.
StringRef ARMAsmParser::splitMnemonic(StringRef Mnemonic,
                                      unsigned &PredicationCode,
                                      bool &CarrySetting,
                                      unsigned &ProcessorIMod,
                                      StringRef &ITMask) {
  PredicationCode = ARMCC::AL;
  CarrySetting = false;
  ProcessorIMod = 0;

  // Ignore some mnemonics we know aren't predicated forms.
  //
  // FIXME: Would be nice to autogen this.
  if ((Mnemonic == "movs" && isThumb()) ||
      Mnemonic == "teq"   || Mnemonic == "vceq"   || Mnemonic == "svc"   ||
      Mnemonic == "mls"   || Mnemonic == "smmls"  || Mnemonic == "vcls"  ||
      Mnemonic == "vmls"  || Mnemonic == "vnmls"  || Mnemonic == "vacge" ||
      Mnemonic == "vcge"  || Mnemonic == "vclt"   || Mnemonic == "vacgt" ||
      Mnemonic == "vaclt" || Mnemonic == "vacle"  || Mnemonic == "hlt" ||
      Mnemonic == "vcgt"  || Mnemonic == "vcle"   || Mnemonic == "smlal" ||
      Mnemonic == "umaal" || Mnemonic == "umlal"  || Mnemonic == "vabal" ||
      Mnemonic == "vmlal" || Mnemonic == "vpadal" || Mnemonic == "vqdmlal" ||
      Mnemonic == "fmuls" || Mnemonic == "vmaxnm" || Mnemonic == "vminnm" ||
      Mnemonic == "vcvta" || Mnemonic == "vcvtn"  || Mnemonic == "vcvtp" ||
      Mnemonic == "vcvtm" || Mnemonic == "vrinta" || Mnemonic == "vrintn" ||
      Mnemonic == "vrintp" || Mnemonic == "vrintm" || Mnemonic == "hvc" ||
      Mnemonic.startswith("vsel") || Mnemonic == "vins" || Mnemonic == "vmovx" ||
      Mnemonic == "bxns"  || Mnemonic == "blxns" ||
      Mnemonic == "vudot" || Mnemonic == "vsdot" ||
      Mnemonic == "vcmla" || Mnemonic == "vcadd" ||
      Mnemonic == "vfmal" || Mnemonic == "vfmsl")
    return Mnemonic;

  // First, split out any predication code. Ignore mnemonics we know aren't
  // predicated but do have a carry-set and so weren't caught above.
  if (Mnemonic != "adcs" && Mnemonic != "bics" && Mnemonic != "movs" &&
      Mnemonic != "muls" && Mnemonic != "smlals" && Mnemonic != "smulls" &&
      Mnemonic != "umlals" && Mnemonic != "umulls" && Mnemonic != "lsls" &&
      Mnemonic != "sbcs" && Mnemonic != "rscs") {
    unsigned CC = ARMCondCodeFromString(Mnemonic.substr(Mnemonic.size()-2));
    if (CC != ~0U) {
      Mnemonic = Mnemonic.slice(0, Mnemonic.size() - 2);
      PredicationCode = CC;
    }
  }

  // Next, determine if we have a carry setting bit. We explicitly ignore all
  // the instructions we know end in 's'.
  if (Mnemonic.endswith("s") &&
      !(Mnemonic == "cps" || Mnemonic == "mls" ||
        Mnemonic == "mrs" || Mnemonic == "smmls" || Mnemonic == "vabs" ||
        Mnemonic == "vcls" || Mnemonic == "vmls" || Mnemonic == "vmrs" ||
        Mnemonic == "vnmls" || Mnemonic == "vqabs" || Mnemonic == "vrecps" ||
        Mnemonic == "vrsqrts" || Mnemonic == "srs" || Mnemonic == "flds" ||
        Mnemonic == "fmrs" || Mnemonic == "fsqrts" || Mnemonic == "fsubs" ||
        Mnemonic == "fsts" || Mnemonic == "fcpys" || Mnemonic == "fdivs" ||
        Mnemonic == "fmuls" || Mnemonic == "fcmps" || Mnemonic == "fcmpzs" ||
        Mnemonic == "vfms" || Mnemonic == "vfnms" || Mnemonic == "fconsts" ||
        Mnemonic == "bxns" || Mnemonic == "blxns" ||
        (Mnemonic == "movs" && isThumb()))) {
    Mnemonic = Mnemonic.slice(0, Mnemonic.size() - 1);
    CarrySetting = true;
  }

  // The "cps" instruction can have a interrupt mode operand which is glued into
  // the mnemonic. Check if this is the case, split it and parse the imod op
  if (Mnemonic.startswith("cps")) {
    // Split out any imod code.
    unsigned IMod =
      StringSwitch<unsigned>(Mnemonic.substr(Mnemonic.size()-2, 2))
      .Case("ie", ARM_PROC::IE)
      .Case("id", ARM_PROC::ID)
      .Default(~0U);
    if (IMod != ~0U) {
      Mnemonic = Mnemonic.slice(0, Mnemonic.size()-2);
      ProcessorIMod = IMod;
    }
  }

  // The "it" instruction has the condition mask on the end of the mnemonic.
  if (Mnemonic.startswith("it")) {
    ITMask = Mnemonic.slice(2, Mnemonic.size());
    Mnemonic = Mnemonic.slice(0, 2);
  }

  return Mnemonic;
}

/// Given a canonical mnemonic, determine if the instruction ever allows
/// inclusion of carry set or predication code operands.
//
// FIXME: It would be nice to autogen this.
void ARMAsmParser::getMnemonicAcceptInfo(StringRef Mnemonic, StringRef FullInst,
                                         bool &CanAcceptCarrySet,
                                         bool &CanAcceptPredicationCode) {
  CanAcceptCarrySet =
      Mnemonic == "and" || Mnemonic == "lsl" || Mnemonic == "lsr" ||
      Mnemonic == "rrx" || Mnemonic == "ror" || Mnemonic == "sub" ||
      Mnemonic == "add" || Mnemonic == "adc" || Mnemonic == "mul" ||
      Mnemonic == "bic" || Mnemonic == "asr" || Mnemonic == "orr" ||
      Mnemonic == "mvn" || Mnemonic == "rsb" || Mnemonic == "rsc" ||
      Mnemonic == "orn" || Mnemonic == "sbc" || Mnemonic == "eor" ||
      Mnemonic == "neg" || Mnemonic == "vfm" || Mnemonic == "vfnm" ||
      (!isThumb() &&
       (Mnemonic == "smull" || Mnemonic == "mov" || Mnemonic == "mla" ||
        Mnemonic == "smlal" || Mnemonic == "umlal" || Mnemonic == "umull"));

  if (Mnemonic == "bkpt" || Mnemonic == "cbnz" || Mnemonic == "setend" ||
      Mnemonic == "cps" || Mnemonic == "it" || Mnemonic == "cbz" ||
      Mnemonic == "trap" || Mnemonic == "hlt" || Mnemonic == "udf" ||
      Mnemonic.startswith("crc32") || Mnemonic.startswith("cps") ||
      Mnemonic.startswith("vsel") || Mnemonic == "vmaxnm" ||
      Mnemonic == "vminnm" || Mnemonic == "vcvta" || Mnemonic == "vcvtn" ||
      Mnemonic == "vcvtp" || Mnemonic == "vcvtm" || Mnemonic == "vrinta" ||
      Mnemonic == "vrintn" || Mnemonic == "vrintp" || Mnemonic == "vrintm" ||
      Mnemonic.startswith("aes") || Mnemonic == "hvc" || Mnemonic == "setpan" ||
      Mnemonic.startswith("sha1") || Mnemonic.startswith("sha256") ||
      (FullInst.startswith("vmull") && FullInst.endswith(".p64")) ||
      Mnemonic == "vmovx" || Mnemonic == "vins" ||
      Mnemonic == "vudot" || Mnemonic == "vsdot" ||
      Mnemonic == "vcmla" || Mnemonic == "vcadd" ||
      Mnemonic == "vfmal" || Mnemonic == "vfmsl" ||
      Mnemonic == "sb"    || Mnemonic == "ssbb"  ||
      Mnemonic == "pssbb") {
    // These mnemonics are never predicable
    CanAcceptPredicationCode = false;
  } else if (!isThumb()) {
    // Some instructions are only predicable in Thumb mode
    CanAcceptPredicationCode =
        Mnemonic != "cdp2" && Mnemonic != "clrex" && Mnemonic != "mcr2" &&
        Mnemonic != "mcrr2" && Mnemonic != "mrc2" && Mnemonic != "mrrc2" &&
        Mnemonic != "dmb" && Mnemonic != "dfb" && Mnemonic != "dsb" &&
        Mnemonic != "isb" && Mnemonic != "pld" && Mnemonic != "pli" &&
        Mnemonic != "pldw" && Mnemonic != "ldc2" && Mnemonic != "ldc2l" &&
        Mnemonic != "stc2" && Mnemonic != "stc2l" &&
        Mnemonic != "tsb" &&
        !Mnemonic.startswith("rfe") && !Mnemonic.startswith("srs");
  } else if (isThumbOne()) {
    if (hasV6MOps())
      CanAcceptPredicationCode = Mnemonic != "movs";
    else
      CanAcceptPredicationCode = Mnemonic != "nop" && Mnemonic != "movs";
  } else
    CanAcceptPredicationCode = true;
}

// Some Thumb instructions have two operand forms that are not
// available as three operand, convert to two operand form if possible.
//
// FIXME: We would really like to be able to tablegen'erate this.
void ARMAsmParser::tryConvertingToTwoOperandForm(StringRef Mnemonic,
                                                 bool CarrySetting,
                                                 OperandVector &Operands) {
  if (Operands.size() != 6)
    return;

  const auto &Op3 = static_cast<ARMOperand &>(*Operands[3]);
        auto &Op4 = static_cast<ARMOperand &>(*Operands[4]);
  if (!Op3.isReg() || !Op4.isReg())
    return;

  auto Op3Reg = Op3.getReg();
  auto Op4Reg = Op4.getReg();

  // For most Thumb2 cases we just generate the 3 operand form and reduce
  // it in processInstruction(), but the 3 operand form of ADD (t2ADDrr)
  // won't accept SP or PC so we do the transformation here taking care
  // with immediate range in the 'add sp, sp #imm' case.
  auto &Op5 = static_cast<ARMOperand &>(*Operands[5]);
  if (isThumbTwo()) {
    if (Mnemonic != "add")
      return;
    bool TryTransform = Op3Reg == ARM::PC || Op4Reg == ARM::PC ||
                        (Op5.isReg() && Op5.getReg() == ARM::PC);
    if (!TryTransform) {
      TryTransform = (Op3Reg == ARM::SP || Op4Reg == ARM::SP ||
                      (Op5.isReg() && Op5.getReg() == ARM::SP)) &&
                     !(Op3Reg == ARM::SP && Op4Reg == ARM::SP &&
                       Op5.isImm() && !Op5.isImm0_508s4());
    }
    if (!TryTransform)
      return;
  } else if (!isThumbOne())
    return;

  if (!(Mnemonic == "add" || Mnemonic == "sub" || Mnemonic == "and" ||
        Mnemonic == "eor" || Mnemonic == "lsl" || Mnemonic == "lsr" ||
        Mnemonic == "asr" || Mnemonic == "adc" || Mnemonic == "sbc" ||
        Mnemonic == "ror" || Mnemonic == "orr" || Mnemonic == "bic"))
    return;

  // If first 2 operands of a 3 operand instruction are the same
  // then transform to 2 operand version of the same instruction
  // e.g. 'adds r0, r0, #1' transforms to 'adds r0, #1'
  bool Transform = Op3Reg == Op4Reg;

  // For communtative operations, we might be able to transform if we swap
  // Op4 and Op5.  The 'ADD Rdm, SP, Rdm' form is already handled specially
  // as tADDrsp.
  const ARMOperand *LastOp = &Op5;
  bool Swap = false;
  if (!Transform && Op5.isReg() && Op3Reg == Op5.getReg() &&
      ((Mnemonic == "add" && Op4Reg != ARM::SP) ||
       Mnemonic == "and" || Mnemonic == "eor" ||
       Mnemonic == "adc" || Mnemonic == "orr")) {
    Swap = true;
    LastOp = &Op4;
    Transform = true;
  }

  // If both registers are the same then remove one of them from
  // the operand list, with certain exceptions.
  if (Transform) {
    // Don't transform 'adds Rd, Rd, Rm' or 'sub{s} Rd, Rd, Rm' because the
    // 2 operand forms don't exist.
    if (((Mnemonic == "add" && CarrySetting) || Mnemonic == "sub") &&
        LastOp->isReg())
      Transform = false;

    // Don't transform 'add/sub{s} Rd, Rd, #imm' if the immediate fits into
    // 3-bits because the ARMARM says not to.
    if ((Mnemonic == "add" || Mnemonic == "sub") && LastOp->isImm0_7())
      Transform = false;
  }

  if (Transform) {
    if (Swap)
      std::swap(Op4, Op5);
    Operands.erase(Operands.begin() + 3);
  }
}

bool ARMAsmParser::shouldOmitCCOutOperand(StringRef Mnemonic,
                                          OperandVector &Operands) {
  // FIXME: This is all horribly hacky. We really need a better way to deal
  // with optional operands like this in the matcher table.

  // The 'mov' mnemonic is special. One variant has a cc_out operand, while
  // another does not. Specifically, the MOVW instruction does not. So we
  // special case it here and remove the defaulted (non-setting) cc_out
  // operand if that's the instruction we're trying to match.
  //
  // We do this as post-processing of the explicit operands rather than just
  // conditionally adding the cc_out in the first place because we need
  // to check the type of the parsed immediate operand.
  if (Mnemonic == "mov" && Operands.size() > 4 && !isThumb() &&
      !static_cast<ARMOperand &>(*Operands[4]).isModImm() &&
      static_cast<ARMOperand &>(*Operands[4]).isImm0_65535Expr() &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0)
    return true;

  // Register-register 'add' for thumb does not have a cc_out operand
  // when there are only two register operands.
  if (isThumb() && Mnemonic == "add" && Operands.size() == 5 &&
      static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0)
    return true;
  // Register-register 'add' for thumb does not have a cc_out operand
  // when it's an ADD Rdm, SP, {Rdm|#imm0_255} instruction. We do
  // have to check the immediate range here since Thumb2 has a variant
  // that can handle a different range and has a cc_out operand.
  if (((isThumb() && Mnemonic == "add") ||
       (isThumbTwo() && Mnemonic == "sub")) &&
      Operands.size() == 6 && static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).getReg() == ARM::SP &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0 &&
      ((Mnemonic == "add" && static_cast<ARMOperand &>(*Operands[5]).isReg()) ||
       static_cast<ARMOperand &>(*Operands[5]).isImm0_1020s4()))
    return true;
  // For Thumb2, add/sub immediate does not have a cc_out operand for the
  // imm0_4095 variant. That's the least-preferred variant when
  // selecting via the generic "add" mnemonic, so to know that we
  // should remove the cc_out operand, we have to explicitly check that
  // it's not one of the other variants. Ugh.
  if (isThumbTwo() && (Mnemonic == "add" || Mnemonic == "sub") &&
      Operands.size() == 6 && static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      static_cast<ARMOperand &>(*Operands[5]).isImm()) {
    // Nest conditions rather than one big 'if' statement for readability.
    //
    // If both registers are low, we're in an IT block, and the immediate is
    // in range, we should use encoding T1 instead, which has a cc_out.
    if (inITBlock() &&
        isARMLowRegister(static_cast<ARMOperand &>(*Operands[3]).getReg()) &&
        isARMLowRegister(static_cast<ARMOperand &>(*Operands[4]).getReg()) &&
        static_cast<ARMOperand &>(*Operands[5]).isImm0_7())
      return false;
    // Check against T3. If the second register is the PC, this is an
    // alternate form of ADR, which uses encoding T4, so check for that too.
    if (static_cast<ARMOperand &>(*Operands[4]).getReg() != ARM::PC &&
        static_cast<ARMOperand &>(*Operands[5]).isT2SOImm())
      return false;

    // Otherwise, we use encoding T4, which does not have a cc_out
    // operand.
    return true;
  }

  // The thumb2 multiply instruction doesn't have a CCOut register, so
  // if we have a "mul" mnemonic in Thumb mode, check if we'll be able to
  // use the 16-bit encoding or not.
  if (isThumbTwo() && Mnemonic == "mul" && Operands.size() == 6 &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0 &&
      static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      static_cast<ARMOperand &>(*Operands[5]).isReg() &&
      // If the registers aren't low regs, the destination reg isn't the
      // same as one of the source regs, or the cc_out operand is zero
      // outside of an IT block, we have to use the 32-bit encoding, so
      // remove the cc_out operand.
      (!isARMLowRegister(static_cast<ARMOperand &>(*Operands[3]).getReg()) ||
       !isARMLowRegister(static_cast<ARMOperand &>(*Operands[4]).getReg()) ||
       !isARMLowRegister(static_cast<ARMOperand &>(*Operands[5]).getReg()) ||
       !inITBlock() || (static_cast<ARMOperand &>(*Operands[3]).getReg() !=
                            static_cast<ARMOperand &>(*Operands[5]).getReg() &&
                        static_cast<ARMOperand &>(*Operands[3]).getReg() !=
                            static_cast<ARMOperand &>(*Operands[4]).getReg())))
    return true;

  // Also check the 'mul' syntax variant that doesn't specify an explicit
  // destination register.
  if (isThumbTwo() && Mnemonic == "mul" && Operands.size() == 5 &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0 &&
      static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      // If the registers aren't low regs  or the cc_out operand is zero
      // outside of an IT block, we have to use the 32-bit encoding, so
      // remove the cc_out operand.
      (!isARMLowRegister(static_cast<ARMOperand &>(*Operands[3]).getReg()) ||
       !isARMLowRegister(static_cast<ARMOperand &>(*Operands[4]).getReg()) ||
       !inITBlock()))
    return true;

  // Register-register 'add/sub' for thumb does not have a cc_out operand
  // when it's an ADD/SUB SP, #imm. Be lenient on count since there's also
  // the "add/sub SP, SP, #imm" version. If the follow-up operands aren't
  // right, this will result in better diagnostics (which operand is off)
  // anyway.
  if (isThumb() && (Mnemonic == "add" || Mnemonic == "sub") &&
      (Operands.size() == 5 || Operands.size() == 6) &&
      static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[3]).getReg() == ARM::SP &&
      static_cast<ARMOperand &>(*Operands[1]).getReg() == 0 &&
      (static_cast<ARMOperand &>(*Operands[4]).isImm() ||
       (Operands.size() == 6 &&
        static_cast<ARMOperand &>(*Operands[5]).isImm())))
    return true;

  return false;
}

bool ARMAsmParser::shouldOmitPredicateOperand(StringRef Mnemonic,
                                              OperandVector &Operands) {
  // VRINT{Z, X} have a predicate operand in VFP, but not in NEON
  unsigned RegIdx = 3;
  if ((Mnemonic == "vrintz" || Mnemonic == "vrintx") &&
      (static_cast<ARMOperand &>(*Operands[2]).getToken() == ".f32" ||
       static_cast<ARMOperand &>(*Operands[2]).getToken() == ".f16")) {
    if (static_cast<ARMOperand &>(*Operands[3]).isToken() &&
        (static_cast<ARMOperand &>(*Operands[3]).getToken() == ".f32" ||
         static_cast<ARMOperand &>(*Operands[3]).getToken() == ".f16"))
      RegIdx = 4;

    if (static_cast<ARMOperand &>(*Operands[RegIdx]).isReg() &&
        (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(
             static_cast<ARMOperand &>(*Operands[RegIdx]).getReg()) ||
         ARMMCRegisterClasses[ARM::QPRRegClassID].contains(
             static_cast<ARMOperand &>(*Operands[RegIdx]).getReg())))
      return true;
  }
  return false;
}

static bool isDataTypeToken(StringRef Tok) {
  return Tok == ".8" || Tok == ".16" || Tok == ".32" || Tok == ".64" ||
    Tok == ".i8" || Tok == ".i16" || Tok == ".i32" || Tok == ".i64" ||
    Tok == ".u8" || Tok == ".u16" || Tok == ".u32" || Tok == ".u64" ||
    Tok == ".s8" || Tok == ".s16" || Tok == ".s32" || Tok == ".s64" ||
    Tok == ".p8" || Tok == ".p16" || Tok == ".f32" || Tok == ".f64" ||
    Tok == ".f" || Tok == ".d";
}

// FIXME: This bit should probably be handled via an explicit match class
// in the .td files that matches the suffix instead of having it be
// a literal string token the way it is now.
static bool doesIgnoreDataTypeSuffix(StringRef Mnemonic, StringRef DT) {
  return Mnemonic.startswith("vldm") || Mnemonic.startswith("vstm");
}

static void applyMnemonicAliases(StringRef &Mnemonic, uint64_t Features,
                                 unsigned VariantID);

// The GNU assembler has aliases of ldrd and strd with the second register
// omitted. We don't have a way to do that in tablegen, so fix it up here.
//
// We have to be careful to not emit an invalid Rt2 here, because the rest of
// the assmebly parser could then generate confusing diagnostics refering to
// it. If we do find anything that prevents us from doing the transformation we
// bail out, and let the assembly parser report an error on the instruction as
// it is written.
void ARMAsmParser::fixupGNULDRDAlias(StringRef Mnemonic,
                                     OperandVector &Operands) {
  if (Mnemonic != "ldrd" && Mnemonic != "strd")
    return;
  if (Operands.size() < 4)
    return;

  ARMOperand &Op2 = static_cast<ARMOperand &>(*Operands[2]);
  ARMOperand &Op3 = static_cast<ARMOperand &>(*Operands[3]);

  if (!Op2.isReg())
    return;
  if (!Op3.isMem())
    return;

  const MCRegisterClass &GPR = MRI->getRegClass(ARM::GPRRegClassID);
  if (!GPR.contains(Op2.getReg()))
    return;

  unsigned RtEncoding = MRI->getEncodingValue(Op2.getReg());
  if (!isThumb() && (RtEncoding & 1)) {
    // In ARM mode, the registers must be from an aligned pair, this
    // restriction does not apply in Thumb mode.
    return;
  }
  if (Op2.getReg() == ARM::PC)
    return;
  unsigned PairedReg = GPR.getRegister(RtEncoding + 1);
  if (!PairedReg || PairedReg == ARM::PC ||
      (PairedReg == ARM::SP && !hasV8Ops()))
    return;

  Operands.insert(
      Operands.begin() + 3,
      ARMOperand::CreateReg(PairedReg, Op2.getStartLoc(), Op2.getEndLoc()));
}

/// Parse an arm instruction mnemonic followed by its operands.
bool ARMAsmParser::ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                    SMLoc NameLoc, OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  // Apply mnemonic aliases before doing anything else, as the destination
  // mnemonic may include suffices and we want to handle them normally.
  // The generic tblgen'erated code does this later, at the start of
  // MatchInstructionImpl(), but that's too late for aliases that include
  // any sort of suffix.
  uint64_t AvailableFeatures = getAvailableFeatures();
  unsigned AssemblerDialect = getParser().getAssemblerDialect();
  applyMnemonicAliases(Name, AvailableFeatures, AssemblerDialect);

  // First check for the ARM-specific .req directive.
  if (Parser.getTok().is(AsmToken::Identifier) &&
      Parser.getTok().getIdentifier() == ".req") {
    parseDirectiveReq(Name, NameLoc);
    // We always return 'error' for this, as we're done with this
    // statement and don't need to match the 'instruction."
    return true;
  }

  // Create the leading tokens for the mnemonic, split by '.' characters.
  size_t Start = 0, Next = Name.find('.');
  StringRef Mnemonic = Name.slice(Start, Next);

  // Split out the predication code and carry setting flag from the mnemonic.
  unsigned PredicationCode;
  unsigned ProcessorIMod;
  bool CarrySetting;
  StringRef ITMask;
  Mnemonic = splitMnemonic(Mnemonic, PredicationCode, CarrySetting,
                           ProcessorIMod, ITMask);

  // In Thumb1, only the branch (B) instruction can be predicated.
  if (isThumbOne() && PredicationCode != ARMCC::AL && Mnemonic != "b") {
    return Error(NameLoc, "conditional execution not supported in Thumb1");
  }

  Operands.push_back(ARMOperand::CreateToken(Mnemonic, NameLoc));

  // Handle the IT instruction ITMask. Convert it to a bitmask. This
  // is the mask as it will be for the IT encoding if the conditional
  // encoding has a '1' as it's bit0 (i.e. 't' ==> '1'). In the case
  // where the conditional bit0 is zero, the instruction post-processing
  // will adjust the mask accordingly.
  if (Mnemonic == "it") {
    SMLoc Loc = SMLoc::getFromPointer(NameLoc.getPointer() + 2);
    if (ITMask.size() > 3) {
      return Error(Loc, "too many conditions on IT instruction");
    }
    unsigned Mask = 8;
    for (unsigned i = ITMask.size(); i != 0; --i) {
      char pos = ITMask[i - 1];
      if (pos != 't' && pos != 'e') {
        return Error(Loc, "illegal IT block condition mask '" + ITMask + "'");
      }
      Mask >>= 1;
      if (ITMask[i - 1] == 't')
        Mask |= 8;
    }
    Operands.push_back(ARMOperand::CreateITMask(Mask, Loc));
  }

  // FIXME: This is all a pretty gross hack. We should automatically handle
  // optional operands like this via tblgen.

  // Next, add the CCOut and ConditionCode operands, if needed.
  //
  // For mnemonics which can ever incorporate a carry setting bit or predication
  // code, our matching model involves us always generating CCOut and
  // ConditionCode operands to match the mnemonic "as written" and then we let
  // the matcher deal with finding the right instruction or generating an
  // appropriate error.
  bool CanAcceptCarrySet, CanAcceptPredicationCode;
  getMnemonicAcceptInfo(Mnemonic, Name, CanAcceptCarrySet, CanAcceptPredicationCode);

  // If we had a carry-set on an instruction that can't do that, issue an
  // error.
  if (!CanAcceptCarrySet && CarrySetting) {
    return Error(NameLoc, "instruction '" + Mnemonic +
                 "' can not set flags, but 's' suffix specified");
  }
  // If we had a predication code on an instruction that can't do that, issue an
  // error.
  if (!CanAcceptPredicationCode && PredicationCode != ARMCC::AL) {
    return Error(NameLoc, "instruction '" + Mnemonic +
                 "' is not predicable, but condition code specified");
  }

  // Add the carry setting operand, if necessary.
  if (CanAcceptCarrySet) {
    SMLoc Loc = SMLoc::getFromPointer(NameLoc.getPointer() + Mnemonic.size());
    Operands.push_back(ARMOperand::CreateCCOut(CarrySetting ? ARM::CPSR : 0,
                                               Loc));
  }

  // Add the predication code operand, if necessary.
  if (CanAcceptPredicationCode) {
    SMLoc Loc = SMLoc::getFromPointer(NameLoc.getPointer() + Mnemonic.size() +
                                      CarrySetting);
    Operands.push_back(ARMOperand::CreateCondCode(
                         ARMCC::CondCodes(PredicationCode), Loc));
  }

  // Add the processor imod operand, if necessary.
  if (ProcessorIMod) {
    Operands.push_back(ARMOperand::CreateImm(
          MCConstantExpr::create(ProcessorIMod, getContext()),
                                 NameLoc, NameLoc));
  } else if (Mnemonic == "cps" && isMClass()) {
    return Error(NameLoc, "instruction 'cps' requires effect for M-class");
  }

  // Add the remaining tokens in the mnemonic.
  while (Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    StringRef ExtraToken = Name.slice(Start, Next);

    // Some NEON instructions have an optional datatype suffix that is
    // completely ignored. Check for that.
    if (isDataTypeToken(ExtraToken) &&
        doesIgnoreDataTypeSuffix(Mnemonic, ExtraToken))
      continue;

    // For for ARM mode generate an error if the .n qualifier is used.
    if (ExtraToken == ".n" && !isThumb()) {
      SMLoc Loc = SMLoc::getFromPointer(NameLoc.getPointer() + Start);
      return Error(Loc, "instruction with .n (narrow) qualifier not allowed in "
                   "arm mode");
    }

    // The .n qualifier is always discarded as that is what the tables
    // and matcher expect.  In ARM mode the .w qualifier has no effect,
    // so discard it to avoid errors that can be caused by the matcher.
    if (ExtraToken != ".n" && (isThumb() || ExtraToken != ".w")) {
      SMLoc Loc = SMLoc::getFromPointer(NameLoc.getPointer() + Start);
      Operands.push_back(ARMOperand::CreateToken(ExtraToken, Loc));
    }
  }

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (parseOperand(Operands, Mnemonic)) {
      return true;
    }

    while (parseOptionalToken(AsmToken::Comma)) {
      // Parse and remember the operand.
      if (parseOperand(Operands, Mnemonic)) {
        return true;
      }
    }
  }

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  tryConvertingToTwoOperandForm(Mnemonic, CarrySetting, Operands);

  // Some instructions, mostly Thumb, have forms for the same mnemonic that
  // do and don't have a cc_out optional-def operand. With some spot-checks
  // of the operand list, we can figure out which variant we're trying to
  // parse and adjust accordingly before actually matching. We shouldn't ever
  // try to remove a cc_out operand that was explicitly set on the
  // mnemonic, of course (CarrySetting == true). Reason number #317 the
  // table driven matcher doesn't fit well with the ARM instruction set.
  if (!CarrySetting && shouldOmitCCOutOperand(Mnemonic, Operands))
    Operands.erase(Operands.begin() + 1);

  // Some instructions have the same mnemonic, but don't always
  // have a predicate. Distinguish them here and delete the
  // predicate if needed.
  if (PredicationCode == ARMCC::AL &&
      shouldOmitPredicateOperand(Mnemonic, Operands))
    Operands.erase(Operands.begin() + 1);

  // ARM mode 'blx' need special handling, as the register operand version
  // is predicable, but the label operand version is not. So, we can't rely
  // on the Mnemonic based checking to correctly figure out when to put
  // a k_CondCode operand in the list. If we're trying to match the label
  // version, remove the k_CondCode operand here.
  if (!isThumb() && Mnemonic == "blx" && Operands.size() == 3 &&
      static_cast<ARMOperand &>(*Operands[2]).isImm())
    Operands.erase(Operands.begin() + 1);

  // Adjust operands of ldrexd/strexd to MCK_GPRPair.
  // ldrexd/strexd require even/odd GPR pair. To enforce this constraint,
  // a single GPRPair reg operand is used in the .td file to replace the two
  // GPRs. However, when parsing from asm, the two GRPs cannot be automatically
  // expressed as a GPRPair, so we have to manually merge them.
  // FIXME: We would really like to be able to tablegen'erate this.
  if (!isThumb() && Operands.size() > 4 &&
      (Mnemonic == "ldrexd" || Mnemonic == "strexd" || Mnemonic == "ldaexd" ||
       Mnemonic == "stlexd")) {
    bool isLoad = (Mnemonic == "ldrexd" || Mnemonic == "ldaexd");
    unsigned Idx = isLoad ? 2 : 3;
    ARMOperand &Op1 = static_cast<ARMOperand &>(*Operands[Idx]);
    ARMOperand &Op2 = static_cast<ARMOperand &>(*Operands[Idx + 1]);

    const MCRegisterClass& MRC = MRI->getRegClass(ARM::GPRRegClassID);
    // Adjust only if Op1 and Op2 are GPRs.
    if (Op1.isReg() && Op2.isReg() && MRC.contains(Op1.getReg()) &&
        MRC.contains(Op2.getReg())) {
      unsigned Reg1 = Op1.getReg();
      unsigned Reg2 = Op2.getReg();
      unsigned Rt = MRI->getEncodingValue(Reg1);
      unsigned Rt2 = MRI->getEncodingValue(Reg2);

      // Rt2 must be Rt + 1 and Rt must be even.
      if (Rt + 1 != Rt2 || (Rt & 1)) {
        return Error(Op2.getStartLoc(),
                     isLoad ? "destination operands must be sequential"
                            : "source operands must be sequential");
      }
      unsigned NewReg = MRI->getMatchingSuperReg(Reg1, ARM::gsub_0,
          &(MRI->getRegClass(ARM::GPRPairRegClassID)));
      Operands[Idx] =
          ARMOperand::CreateReg(NewReg, Op1.getStartLoc(), Op2.getEndLoc());
      Operands.erase(Operands.begin() + Idx + 1);
    }
  }

  // GNU Assembler extension (compatibility).
  fixupGNULDRDAlias(Mnemonic, Operands);

  // FIXME: As said above, this is all a pretty gross hack.  This instruction
  // does not fit with other "subs" and tblgen.
  // Adjust operands of B9.3.19 SUBS PC, LR, #imm (Thumb2) system instruction
  // so the Mnemonic is the original name "subs" and delete the predicate
  // operand so it will match the table entry.
  if (isThumbTwo() && Mnemonic == "sub" && Operands.size() == 6 &&
      static_cast<ARMOperand &>(*Operands[3]).isReg() &&
      static_cast<ARMOperand &>(*Operands[3]).getReg() == ARM::PC &&
      static_cast<ARMOperand &>(*Operands[4]).isReg() &&
      static_cast<ARMOperand &>(*Operands[4]).getReg() == ARM::LR &&
      static_cast<ARMOperand &>(*Operands[5]).isImm()) {
    Operands.front() = ARMOperand::CreateToken(Name, NameLoc);
    Operands.erase(Operands.begin() + 1);
  }
  return false;
}

// Validate context-sensitive operand constraints.

// return 'true' if register list contains non-low GPR registers,
// 'false' otherwise. If Reg is in the register list or is HiReg, set
// 'containsReg' to true.
static bool checkLowRegisterList(const MCInst &Inst, unsigned OpNo,
                                 unsigned Reg, unsigned HiReg,
                                 bool &containsReg) {
  containsReg = false;
  for (unsigned i = OpNo; i < Inst.getNumOperands(); ++i) {
    unsigned OpReg = Inst.getOperand(i).getReg();
    if (OpReg == Reg)
      containsReg = true;
    // Anything other than a low register isn't legal here.
    if (!isARMLowRegister(OpReg) && (!HiReg || OpReg != HiReg))
      return true;
  }
  return false;
}

// Check if the specified regisgter is in the register list of the inst,
// starting at the indicated operand number.
static bool listContainsReg(const MCInst &Inst, unsigned OpNo, unsigned Reg) {
  for (unsigned i = OpNo, e = Inst.getNumOperands(); i < e; ++i) {
    unsigned OpReg = Inst.getOperand(i).getReg();
    if (OpReg == Reg)
      return true;
  }
  return false;
}

// Return true if instruction has the interesting property of being
// allowed in IT blocks, but not being predicable.
static bool instIsBreakpoint(const MCInst &Inst) {
    return Inst.getOpcode() == ARM::tBKPT ||
           Inst.getOpcode() == ARM::BKPT ||
           Inst.getOpcode() == ARM::tHLT ||
           Inst.getOpcode() == ARM::HLT;
}

bool ARMAsmParser::validatetLDMRegList(const MCInst &Inst,
                                       const OperandVector &Operands,
                                       unsigned ListNo, bool IsARPop) {
  const ARMOperand &Op = static_cast<const ARMOperand &>(*Operands[ListNo]);
  bool HasWritebackToken = Op.isToken() && Op.getToken() == "!";

  bool ListContainsSP = listContainsReg(Inst, ListNo, ARM::SP);
  bool ListContainsLR = listContainsReg(Inst, ListNo, ARM::LR);
  bool ListContainsPC = listContainsReg(Inst, ListNo, ARM::PC);

  if (!IsARPop && ListContainsSP)
    return Error(Operands[ListNo + HasWritebackToken]->getStartLoc(),
                 "SP may not be in the register list");
  else if (ListContainsPC && ListContainsLR)
    return Error(Operands[ListNo + HasWritebackToken]->getStartLoc(),
                 "PC and LR may not be in the register list simultaneously");
  return false;
}

bool ARMAsmParser::validatetSTMRegList(const MCInst &Inst,
                                       const OperandVector &Operands,
                                       unsigned ListNo) {
  const ARMOperand &Op = static_cast<const ARMOperand &>(*Operands[ListNo]);
  bool HasWritebackToken = Op.isToken() && Op.getToken() == "!";

  bool ListContainsSP = listContainsReg(Inst, ListNo, ARM::SP);
  bool ListContainsPC = listContainsReg(Inst, ListNo, ARM::PC);

  if (ListContainsSP && ListContainsPC)
    return Error(Operands[ListNo + HasWritebackToken]->getStartLoc(),
                 "SP and PC may not be in the register list");
  else if (ListContainsSP)
    return Error(Operands[ListNo + HasWritebackToken]->getStartLoc(),
                 "SP may not be in the register list");
  else if (ListContainsPC)
    return Error(Operands[ListNo + HasWritebackToken]->getStartLoc(),
                 "PC may not be in the register list");
  return false;
}

bool ARMAsmParser::validateLDRDSTRD(MCInst &Inst,
                                    const OperandVector &Operands,
                                    bool Load, bool ARMMode, bool Writeback) {
  unsigned RtIndex = Load || !Writeback ? 0 : 1;
  unsigned Rt = MRI->getEncodingValue(Inst.getOperand(RtIndex).getReg());
  unsigned Rt2 = MRI->getEncodingValue(Inst.getOperand(RtIndex + 1).getReg());

  if (ARMMode) {
    // Rt can't be R14.
    if (Rt == 14)
      return Error(Operands[3]->getStartLoc(),
                  "Rt can't be R14");

    // Rt must be even-numbered.
    if ((Rt & 1) == 1)
      return Error(Operands[3]->getStartLoc(),
                   "Rt must be even-numbered");

    // Rt2 must be Rt + 1.
    if (Rt2 != Rt + 1) {
      if (Load)
        return Error(Operands[3]->getStartLoc(),
                     "destination operands must be sequential");
      else
        return Error(Operands[3]->getStartLoc(),
                     "source operands must be sequential");
    }

    // FIXME: Diagnose m == 15
    // FIXME: Diagnose ldrd with m == t || m == t2.
  }

  if (!ARMMode && Load) {
    if (Rt2 == Rt)
      return Error(Operands[3]->getStartLoc(),
                   "destination operands can't be identical");
  }

  if (Writeback) {
    unsigned Rn = MRI->getEncodingValue(Inst.getOperand(3).getReg());

    if (Rn == Rt || Rn == Rt2) {
      if (Load)
        return Error(Operands[3]->getStartLoc(),
                     "base register needs to be different from destination "
                     "registers");
      else
        return Error(Operands[3]->getStartLoc(),
                     "source register and base register can't be identical");
    }

    // FIXME: Diagnose ldrd/strd with writeback and n == 15.
    // (Except the immediate form of ldrd?)
  }

  return false;
}


// FIXME: We would really like to be able to tablegen'erate this.
bool ARMAsmParser::validateInstruction(MCInst &Inst,
                                       const OperandVector &Operands) {
  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());
  SMLoc Loc = Operands[0]->getStartLoc();

  // Check the IT block state first.
  // NOTE: BKPT and HLT instructions have the interesting property of being
  // allowed in IT blocks, but not being predicable. They just always execute.
  if (inITBlock() && !instIsBreakpoint(Inst)) {
    // The instruction must be predicable.
    if (!MCID.isPredicable())
      return Error(Loc, "instructions in IT block must be predicable");
    ARMCC::CondCodes Cond = ARMCC::CondCodes(
        Inst.getOperand(MCID.findFirstPredOperandIdx()).getImm());
    if (Cond != currentITCond()) {
      // Find the condition code Operand to get its SMLoc information.
      SMLoc CondLoc;
      for (unsigned I = 1; I < Operands.size(); ++I)
        if (static_cast<ARMOperand &>(*Operands[I]).isCondCode())
          CondLoc = Operands[I]->getStartLoc();
      return Error(CondLoc, "incorrect condition in IT block; got '" +
                                StringRef(ARMCondCodeToString(Cond)) +
                                "', but expected '" +
                                ARMCondCodeToString(currentITCond()) + "'");
    }
  // Check for non-'al' condition codes outside of the IT block.
  } else if (isThumbTwo() && MCID.isPredicable() &&
             Inst.getOperand(MCID.findFirstPredOperandIdx()).getImm() !=
             ARMCC::AL && Inst.getOpcode() != ARM::tBcc &&
             Inst.getOpcode() != ARM::t2Bcc) {
    return Error(Loc, "predicated instructions must be in IT block");
  } else if (!isThumb() && !useImplicitITARM() && MCID.isPredicable() &&
             Inst.getOperand(MCID.findFirstPredOperandIdx()).getImm() !=
                 ARMCC::AL) {
    return Warning(Loc, "predicated instructions should be in IT block");
  }

  // PC-setting instructions in an IT block, but not the last instruction of
  // the block, are UNPREDICTABLE.
  if (inExplicitITBlock() && !lastInITBlock() && isITBlockTerminator(Inst)) {
    return Error(Loc, "instruction must be outside of IT block or the last instruction in an IT block");
  }

  const unsigned Opcode = Inst.getOpcode();
  switch (Opcode) {
  case ARM::t2IT: {
    // Encoding is unpredictable if it ever results in a notional 'NV'
    // predicate. Since we don't parse 'NV' directly this means an 'AL'
    // predicate with an "else" mask bit.
    unsigned Cond = Inst.getOperand(0).getImm();
    unsigned Mask = Inst.getOperand(1).getImm();

    // Mask hasn't been modified to the IT instruction encoding yet so
    // conditions only allowing a 't' are a block of 1s starting at bit 3
    // followed by all 0s. Easiest way is to just list the 4 possibilities.
    if (Cond == ARMCC::AL && Mask != 8 && Mask != 12 && Mask != 14 &&
        Mask != 15)
      return Error(Loc, "unpredictable IT predicate sequence");
    break;
  }
  case ARM::LDRD:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/true, /*ARMMode*/true,
                         /*Writeback*/false))
      return true;
    break;
  case ARM::LDRD_PRE:
  case ARM::LDRD_POST:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/true, /*ARMMode*/true,
                         /*Writeback*/true))
      return true;
    break;
  case ARM::t2LDRDi8:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/true, /*ARMMode*/false,
                         /*Writeback*/false))
      return true;
    break;
  case ARM::t2LDRD_PRE:
  case ARM::t2LDRD_POST:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/true, /*ARMMode*/false,
                         /*Writeback*/true))
      return true;
    break;
  case ARM::t2BXJ: {
    const unsigned RmReg = Inst.getOperand(0).getReg();
    // Rm = SP is no longer unpredictable in v8-A
    if (RmReg == ARM::SP && !hasV8Ops())
      return Error(Operands[2]->getStartLoc(),
                   "r13 (SP) is an unpredictable operand to BXJ");
    return false;
  }
  case ARM::STRD:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/false, /*ARMMode*/true,
                         /*Writeback*/false))
      return true;
    break;
  case ARM::STRD_PRE:
  case ARM::STRD_POST:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/false, /*ARMMode*/true,
                         /*Writeback*/true))
      return true;
    break;
  case ARM::t2STRD_PRE:
  case ARM::t2STRD_POST:
    if (validateLDRDSTRD(Inst, Operands, /*Load*/false, /*ARMMode*/false,
                         /*Writeback*/true))
      return true;
    break;
  case ARM::STR_PRE_IMM:
  case ARM::STR_PRE_REG:
  case ARM::t2STR_PRE:
  case ARM::STR_POST_IMM:
  case ARM::STR_POST_REG:
  case ARM::t2STR_POST:
  case ARM::STRH_PRE:
  case ARM::t2STRH_PRE:
  case ARM::STRH_POST:
  case ARM::t2STRH_POST:
  case ARM::STRB_PRE_IMM:
  case ARM::STRB_PRE_REG:
  case ARM::t2STRB_PRE:
  case ARM::STRB_POST_IMM:
  case ARM::STRB_POST_REG:
  case ARM::t2STRB_POST: {
    // Rt must be different from Rn.
    const unsigned Rt = MRI->getEncodingValue(Inst.getOperand(1).getReg());
    const unsigned Rn = MRI->getEncodingValue(Inst.getOperand(2).getReg());

    if (Rt == Rn)
      return Error(Operands[3]->getStartLoc(),
                   "source register and base register can't be identical");
    return false;
  }
  case ARM::LDR_PRE_IMM:
  case ARM::LDR_PRE_REG:
  case ARM::t2LDR_PRE:
  case ARM::LDR_POST_IMM:
  case ARM::LDR_POST_REG:
  case ARM::t2LDR_POST:
  case ARM::LDRH_PRE:
  case ARM::t2LDRH_PRE:
  case ARM::LDRH_POST:
  case ARM::t2LDRH_POST:
  case ARM::LDRSH_PRE:
  case ARM::t2LDRSH_PRE:
  case ARM::LDRSH_POST:
  case ARM::t2LDRSH_POST:
  case ARM::LDRB_PRE_IMM:
  case ARM::LDRB_PRE_REG:
  case ARM::t2LDRB_PRE:
  case ARM::LDRB_POST_IMM:
  case ARM::LDRB_POST_REG:
  case ARM::t2LDRB_POST:
  case ARM::LDRSB_PRE:
  case ARM::t2LDRSB_PRE:
  case ARM::LDRSB_POST:
  case ARM::t2LDRSB_POST: {
    // Rt must be different from Rn.
    const unsigned Rt = MRI->getEncodingValue(Inst.getOperand(0).getReg());
    const unsigned Rn = MRI->getEncodingValue(Inst.getOperand(2).getReg());

    if (Rt == Rn)
      return Error(Operands[3]->getStartLoc(),
                   "destination register and base register can't be identical");
    return false;
  }
  case ARM::SBFX:
  case ARM::t2SBFX:
  case ARM::UBFX:
  case ARM::t2UBFX: {
    // Width must be in range [1, 32-lsb].
    unsigned LSB = Inst.getOperand(2).getImm();
    unsigned Widthm1 = Inst.getOperand(3).getImm();
    if (Widthm1 >= 32 - LSB)
      return Error(Operands[5]->getStartLoc(),
                   "bitfield width must be in range [1,32-lsb]");
    return false;
  }
  // Notionally handles ARM::tLDMIA_UPD too.
  case ARM::tLDMIA: {
    // If we're parsing Thumb2, the .w variant is available and handles
    // most cases that are normally illegal for a Thumb1 LDM instruction.
    // We'll make the transformation in processInstruction() if necessary.
    //
    // Thumb LDM instructions are writeback iff the base register is not
    // in the register list.
    unsigned Rn = Inst.getOperand(0).getReg();
    bool HasWritebackToken =
        (static_cast<ARMOperand &>(*Operands[3]).isToken() &&
         static_cast<ARMOperand &>(*Operands[3]).getToken() == "!");
    bool ListContainsBase;
    if (checkLowRegisterList(Inst, 3, Rn, 0, ListContainsBase) && !isThumbTwo())
      return Error(Operands[3 + HasWritebackToken]->getStartLoc(),
                   "registers must be in range r0-r7");
    // If we should have writeback, then there should be a '!' token.
    if (!ListContainsBase && !HasWritebackToken && !isThumbTwo())
      return Error(Operands[2]->getStartLoc(),
                   "writeback operator '!' expected");
    // If we should not have writeback, there must not be a '!'. This is
    // true even for the 32-bit wide encodings.
    if (ListContainsBase && HasWritebackToken)
      return Error(Operands[3]->getStartLoc(),
                   "writeback operator '!' not allowed when base register "
                   "in register list");

    if (validatetLDMRegList(Inst, Operands, 3))
      return true;
    break;
  }
  case ARM::LDMIA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::LDMDA_UPD:
    // ARM variants loading and updating the same register are only officially
    // UNPREDICTABLE on v7 upwards. Goodness knows what they did before.
    if (!hasV7Ops())
      break;
    if (listContainsReg(Inst, 3, Inst.getOperand(0).getReg()))
      return Error(Operands.back()->getStartLoc(),
                   "writeback register not allowed in register list");
    break;
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
    if (validatetLDMRegList(Inst, Operands, 3))
      return true;
    break;
  case ARM::t2STMIA:
  case ARM::t2STMDB:
    if (validatetSTMRegList(Inst, Operands, 3))
      return true;
    break;
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    if (listContainsReg(Inst, 3, Inst.getOperand(0).getReg()))
      return Error(Operands.back()->getStartLoc(),
                   "writeback register not allowed in register list");

    if (Opcode == ARM::t2LDMIA_UPD || Opcode == ARM::t2LDMDB_UPD) {
      if (validatetLDMRegList(Inst, Operands, 3))
        return true;
    } else {
      if (validatetSTMRegList(Inst, Operands, 3))
        return true;
    }
    break;

  case ARM::sysLDMIA_UPD:
  case ARM::sysLDMDA_UPD:
  case ARM::sysLDMDB_UPD:
  case ARM::sysLDMIB_UPD:
    if (!listContainsReg(Inst, 3, ARM::PC))
      return Error(Operands[4]->getStartLoc(),
                   "writeback register only allowed on system LDM "
                   "if PC in register-list");
    break;
  case ARM::sysSTMIA_UPD:
  case ARM::sysSTMDA_UPD:
  case ARM::sysSTMDB_UPD:
  case ARM::sysSTMIB_UPD:
    return Error(Operands[2]->getStartLoc(),
                 "system STM cannot have writeback register");
  case ARM::tMUL:
    // The second source operand must be the same register as the destination
    // operand.
    //
    // In this case, we must directly check the parsed operands because the
    // cvtThumbMultiply() function is written in such a way that it guarantees
    // this first statement is always true for the new Inst.  Essentially, the
    // destination is unconditionally copied into the second source operand
    // without checking to see if it matches what we actually parsed.
    if (Operands.size() == 6 && (((ARMOperand &)*Operands[3]).getReg() !=
                                 ((ARMOperand &)*Operands[5]).getReg()) &&
        (((ARMOperand &)*Operands[3]).getReg() !=
         ((ARMOperand &)*Operands[4]).getReg())) {
      return Error(Operands[3]->getStartLoc(),
                   "destination register must match source register");
    }
    break;

  // Like for ldm/stm, push and pop have hi-reg handling version in Thumb2,
  // so only issue a diagnostic for thumb1. The instructions will be
  // switched to the t2 encodings in processInstruction() if necessary.
  case ARM::tPOP: {
    bool ListContainsBase;
    if (checkLowRegisterList(Inst, 2, 0, ARM::PC, ListContainsBase) &&
        !isThumbTwo())
      return Error(Operands[2]->getStartLoc(),
                   "registers must be in range r0-r7 or pc");
    if (validatetLDMRegList(Inst, Operands, 2, !isMClass()))
      return true;
    break;
  }
  case ARM::tPUSH: {
    bool ListContainsBase;
    if (checkLowRegisterList(Inst, 2, 0, ARM::LR, ListContainsBase) &&
        !isThumbTwo())
      return Error(Operands[2]->getStartLoc(),
                   "registers must be in range r0-r7 or lr");
    if (validatetSTMRegList(Inst, Operands, 2))
      return true;
    break;
  }
  case ARM::tSTMIA_UPD: {
    bool ListContainsBase, InvalidLowList;
    InvalidLowList = checkLowRegisterList(Inst, 4, Inst.getOperand(0).getReg(),
                                          0, ListContainsBase);
    if (InvalidLowList && !isThumbTwo())
      return Error(Operands[4]->getStartLoc(),
                   "registers must be in range r0-r7");

    // This would be converted to a 32-bit stm, but that's not valid if the
    // writeback register is in the list.
    if (InvalidLowList && ListContainsBase)
      return Error(Operands[4]->getStartLoc(),
                   "writeback operator '!' not allowed when base register "
                   "in register list");

    if (validatetSTMRegList(Inst, Operands, 4))
      return true;
    break;
  }
  case ARM::tADDrSP:
    // If the non-SP source operand and the destination operand are not the
    // same, we need thumb2 (for the wide encoding), or we have an error.
    if (!isThumbTwo() &&
        Inst.getOperand(0).getReg() != Inst.getOperand(2).getReg()) {
      return Error(Operands[4]->getStartLoc(),
                   "source register must be the same as destination");
    }
    break;

  // Final range checking for Thumb unconditional branch instructions.
  case ARM::tB:
    if (!(static_cast<ARMOperand &>(*Operands[2])).isSignedOffset<11, 1>())
      return Error(Operands[2]->getStartLoc(), "branch target out of range");
    break;
  case ARM::t2B: {
    int op = (Operands[2]->isImm()) ? 2 : 3;
    if (!static_cast<ARMOperand &>(*Operands[op]).isSignedOffset<24, 1>())
      return Error(Operands[op]->getStartLoc(), "branch target out of range");
    break;
  }
  // Final range checking for Thumb conditional branch instructions.
  case ARM::tBcc:
    if (!static_cast<ARMOperand &>(*Operands[2]).isSignedOffset<8, 1>())
      return Error(Operands[2]->getStartLoc(), "branch target out of range");
    break;
  case ARM::t2Bcc: {
    int Op = (Operands[2]->isImm()) ? 2 : 3;
    if (!static_cast<ARMOperand &>(*Operands[Op]).isSignedOffset<20, 1>())
      return Error(Operands[Op]->getStartLoc(), "branch target out of range");
    break;
  }
  case ARM::tCBZ:
  case ARM::tCBNZ: {
    if (!static_cast<ARMOperand &>(*Operands[2]).isUnsignedOffset<6, 1>())
      return Error(Operands[2]->getStartLoc(), "branch target out of range");
    break;
  }
  case ARM::MOVi16:
  case ARM::MOVTi16:
  case ARM::t2MOVi16:
  case ARM::t2MOVTi16:
    {
    // We want to avoid misleadingly allowing something like "mov r0, <symbol>"
    // especially when we turn it into a movw and the expression <symbol> does
    // not have a :lower16: or :upper16 as part of the expression.  We don't
    // want the behavior of silently truncating, which can be unexpected and
    // lead to bugs that are difficult to find since this is an easy mistake
    // to make.
    int i = (Operands[3]->isImm()) ? 3 : 4;
    ARMOperand &Op = static_cast<ARMOperand &>(*Operands[i]);
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Op.getImm());
    if (CE) break;
    const MCExpr *E = dyn_cast<MCExpr>(Op.getImm());
    if (!E) break;
    const ARMMCExpr *ARM16Expr = dyn_cast<ARMMCExpr>(E);
    if (!ARM16Expr || (ARM16Expr->getKind() != ARMMCExpr::VK_ARM_HI16 &&
                       ARM16Expr->getKind() != ARMMCExpr::VK_ARM_LO16))
      return Error(
          Op.getStartLoc(),
          "immediate expression for mov requires :lower16: or :upper16");
    break;
  }
  case ARM::HINT:
  case ARM::t2HINT: {
    unsigned Imm8 = Inst.getOperand(0).getImm();
    unsigned Pred = Inst.getOperand(1).getImm();
    // ESB is not predicable (pred must be AL). Without the RAS extension, this
    // behaves as any other unallocated hint.
    if (Imm8 == 0x10 && Pred != ARMCC::AL && hasRAS())
      return Error(Operands[1]->getStartLoc(), "instruction 'esb' is not "
                                               "predicable, but condition "
                                               "code specified");
    if (Imm8 == 0x14 && Pred != ARMCC::AL)
      return Error(Operands[1]->getStartLoc(), "instruction 'csdb' is not "
                                               "predicable, but condition "
                                               "code specified");
    break;
  }
  case ARM::DSB:
  case ARM::t2DSB: {

    if (Inst.getNumOperands() < 2)
      break;

    unsigned Option = Inst.getOperand(0).getImm();
    unsigned Pred = Inst.getOperand(1).getImm();

    // SSBB and PSSBB (DSB #0|#4) are not predicable (pred must be AL).
    if (Option == 0 && Pred != ARMCC::AL)
      return Error(Operands[1]->getStartLoc(),
                   "instruction 'ssbb' is not predicable, but condition code "
                   "specified");
    if (Option == 4 && Pred != ARMCC::AL)
      return Error(Operands[1]->getStartLoc(),
                   "instruction 'pssbb' is not predicable, but condition code "
                   "specified");
    break;
  }
  case ARM::VMOVRRS: {
    // Source registers must be sequential.
    const unsigned Sm = MRI->getEncodingValue(Inst.getOperand(2).getReg());
    const unsigned Sm1 = MRI->getEncodingValue(Inst.getOperand(3).getReg());
    if (Sm1 != Sm + 1)
      return Error(Operands[5]->getStartLoc(),
                   "source operands must be sequential");
    break;
  }
  case ARM::VMOVSRR: {
    // Destination registers must be sequential.
    const unsigned Sm = MRI->getEncodingValue(Inst.getOperand(0).getReg());
    const unsigned Sm1 = MRI->getEncodingValue(Inst.getOperand(1).getReg());
    if (Sm1 != Sm + 1)
      return Error(Operands[3]->getStartLoc(),
                   "destination operands must be sequential");
    break;
  }
  case ARM::VLDMDIA:
  case ARM::VSTMDIA: {
    ARMOperand &Op = static_cast<ARMOperand&>(*Operands[3]);
    auto &RegList = Op.getRegList();
    if (RegList.size() < 1 || RegList.size() > 16)
      return Error(Operands[3]->getStartLoc(),
                   "list of registers must be at least 1 and at most 16");
    break;
  }
  }

  return false;
}

static unsigned getRealVSTOpcode(unsigned Opc, unsigned &Spacing) {
  switch(Opc) {
  default: llvm_unreachable("unexpected opcode!");
  // VST1LN
  case ARM::VST1LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VST1LNd8_UPD;
  case ARM::VST1LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VST1LNd16_UPD;
  case ARM::VST1LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VST1LNd32_UPD;
  case ARM::VST1LNdWB_register_Asm_8:  Spacing = 1; return ARM::VST1LNd8_UPD;
  case ARM::VST1LNdWB_register_Asm_16: Spacing = 1; return ARM::VST1LNd16_UPD;
  case ARM::VST1LNdWB_register_Asm_32: Spacing = 1; return ARM::VST1LNd32_UPD;
  case ARM::VST1LNdAsm_8:  Spacing = 1; return ARM::VST1LNd8;
  case ARM::VST1LNdAsm_16: Spacing = 1; return ARM::VST1LNd16;
  case ARM::VST1LNdAsm_32: Spacing = 1; return ARM::VST1LNd32;

  // VST2LN
  case ARM::VST2LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VST2LNd8_UPD;
  case ARM::VST2LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VST2LNd16_UPD;
  case ARM::VST2LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VST2LNd32_UPD;
  case ARM::VST2LNqWB_fixed_Asm_16: Spacing = 2; return ARM::VST2LNq16_UPD;
  case ARM::VST2LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VST2LNq32_UPD;

  case ARM::VST2LNdWB_register_Asm_8:  Spacing = 1; return ARM::VST2LNd8_UPD;
  case ARM::VST2LNdWB_register_Asm_16: Spacing = 1; return ARM::VST2LNd16_UPD;
  case ARM::VST2LNdWB_register_Asm_32: Spacing = 1; return ARM::VST2LNd32_UPD;
  case ARM::VST2LNqWB_register_Asm_16: Spacing = 2; return ARM::VST2LNq16_UPD;
  case ARM::VST2LNqWB_register_Asm_32: Spacing = 2; return ARM::VST2LNq32_UPD;

  case ARM::VST2LNdAsm_8:  Spacing = 1; return ARM::VST2LNd8;
  case ARM::VST2LNdAsm_16: Spacing = 1; return ARM::VST2LNd16;
  case ARM::VST2LNdAsm_32: Spacing = 1; return ARM::VST2LNd32;
  case ARM::VST2LNqAsm_16: Spacing = 2; return ARM::VST2LNq16;
  case ARM::VST2LNqAsm_32: Spacing = 2; return ARM::VST2LNq32;

  // VST3LN
  case ARM::VST3LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VST3LNd8_UPD;
  case ARM::VST3LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VST3LNd16_UPD;
  case ARM::VST3LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VST3LNd32_UPD;
  case ARM::VST3LNqWB_fixed_Asm_16: Spacing = 1; return ARM::VST3LNq16_UPD;
  case ARM::VST3LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VST3LNq32_UPD;
  case ARM::VST3LNdWB_register_Asm_8:  Spacing = 1; return ARM::VST3LNd8_UPD;
  case ARM::VST3LNdWB_register_Asm_16: Spacing = 1; return ARM::VST3LNd16_UPD;
  case ARM::VST3LNdWB_register_Asm_32: Spacing = 1; return ARM::VST3LNd32_UPD;
  case ARM::VST3LNqWB_register_Asm_16: Spacing = 2; return ARM::VST3LNq16_UPD;
  case ARM::VST3LNqWB_register_Asm_32: Spacing = 2; return ARM::VST3LNq32_UPD;
  case ARM::VST3LNdAsm_8:  Spacing = 1; return ARM::VST3LNd8;
  case ARM::VST3LNdAsm_16: Spacing = 1; return ARM::VST3LNd16;
  case ARM::VST3LNdAsm_32: Spacing = 1; return ARM::VST3LNd32;
  case ARM::VST3LNqAsm_16: Spacing = 2; return ARM::VST3LNq16;
  case ARM::VST3LNqAsm_32: Spacing = 2; return ARM::VST3LNq32;

  // VST3
  case ARM::VST3dWB_fixed_Asm_8:  Spacing = 1; return ARM::VST3d8_UPD;
  case ARM::VST3dWB_fixed_Asm_16: Spacing = 1; return ARM::VST3d16_UPD;
  case ARM::VST3dWB_fixed_Asm_32: Spacing = 1; return ARM::VST3d32_UPD;
  case ARM::VST3qWB_fixed_Asm_8:  Spacing = 2; return ARM::VST3q8_UPD;
  case ARM::VST3qWB_fixed_Asm_16: Spacing = 2; return ARM::VST3q16_UPD;
  case ARM::VST3qWB_fixed_Asm_32: Spacing = 2; return ARM::VST3q32_UPD;
  case ARM::VST3dWB_register_Asm_8:  Spacing = 1; return ARM::VST3d8_UPD;
  case ARM::VST3dWB_register_Asm_16: Spacing = 1; return ARM::VST3d16_UPD;
  case ARM::VST3dWB_register_Asm_32: Spacing = 1; return ARM::VST3d32_UPD;
  case ARM::VST3qWB_register_Asm_8:  Spacing = 2; return ARM::VST3q8_UPD;
  case ARM::VST3qWB_register_Asm_16: Spacing = 2; return ARM::VST3q16_UPD;
  case ARM::VST3qWB_register_Asm_32: Spacing = 2; return ARM::VST3q32_UPD;
  case ARM::VST3dAsm_8:  Spacing = 1; return ARM::VST3d8;
  case ARM::VST3dAsm_16: Spacing = 1; return ARM::VST3d16;
  case ARM::VST3dAsm_32: Spacing = 1; return ARM::VST3d32;
  case ARM::VST3qAsm_8:  Spacing = 2; return ARM::VST3q8;
  case ARM::VST3qAsm_16: Spacing = 2; return ARM::VST3q16;
  case ARM::VST3qAsm_32: Spacing = 2; return ARM::VST3q32;

  // VST4LN
  case ARM::VST4LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VST4LNd8_UPD;
  case ARM::VST4LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VST4LNd16_UPD;
  case ARM::VST4LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VST4LNd32_UPD;
  case ARM::VST4LNqWB_fixed_Asm_16: Spacing = 1; return ARM::VST4LNq16_UPD;
  case ARM::VST4LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VST4LNq32_UPD;
  case ARM::VST4LNdWB_register_Asm_8:  Spacing = 1; return ARM::VST4LNd8_UPD;
  case ARM::VST4LNdWB_register_Asm_16: Spacing = 1; return ARM::VST4LNd16_UPD;
  case ARM::VST4LNdWB_register_Asm_32: Spacing = 1; return ARM::VST4LNd32_UPD;
  case ARM::VST4LNqWB_register_Asm_16: Spacing = 2; return ARM::VST4LNq16_UPD;
  case ARM::VST4LNqWB_register_Asm_32: Spacing = 2; return ARM::VST4LNq32_UPD;
  case ARM::VST4LNdAsm_8:  Spacing = 1; return ARM::VST4LNd8;
  case ARM::VST4LNdAsm_16: Spacing = 1; return ARM::VST4LNd16;
  case ARM::VST4LNdAsm_32: Spacing = 1; return ARM::VST4LNd32;
  case ARM::VST4LNqAsm_16: Spacing = 2; return ARM::VST4LNq16;
  case ARM::VST4LNqAsm_32: Spacing = 2; return ARM::VST4LNq32;

  // VST4
  case ARM::VST4dWB_fixed_Asm_8:  Spacing = 1; return ARM::VST4d8_UPD;
  case ARM::VST4dWB_fixed_Asm_16: Spacing = 1; return ARM::VST4d16_UPD;
  case ARM::VST4dWB_fixed_Asm_32: Spacing = 1; return ARM::VST4d32_UPD;
  case ARM::VST4qWB_fixed_Asm_8:  Spacing = 2; return ARM::VST4q8_UPD;
  case ARM::VST4qWB_fixed_Asm_16: Spacing = 2; return ARM::VST4q16_UPD;
  case ARM::VST4qWB_fixed_Asm_32: Spacing = 2; return ARM::VST4q32_UPD;
  case ARM::VST4dWB_register_Asm_8:  Spacing = 1; return ARM::VST4d8_UPD;
  case ARM::VST4dWB_register_Asm_16: Spacing = 1; return ARM::VST4d16_UPD;
  case ARM::VST4dWB_register_Asm_32: Spacing = 1; return ARM::VST4d32_UPD;
  case ARM::VST4qWB_register_Asm_8:  Spacing = 2; return ARM::VST4q8_UPD;
  case ARM::VST4qWB_register_Asm_16: Spacing = 2; return ARM::VST4q16_UPD;
  case ARM::VST4qWB_register_Asm_32: Spacing = 2; return ARM::VST4q32_UPD;
  case ARM::VST4dAsm_8:  Spacing = 1; return ARM::VST4d8;
  case ARM::VST4dAsm_16: Spacing = 1; return ARM::VST4d16;
  case ARM::VST4dAsm_32: Spacing = 1; return ARM::VST4d32;
  case ARM::VST4qAsm_8:  Spacing = 2; return ARM::VST4q8;
  case ARM::VST4qAsm_16: Spacing = 2; return ARM::VST4q16;
  case ARM::VST4qAsm_32: Spacing = 2; return ARM::VST4q32;
  }
}

static unsigned getRealVLDOpcode(unsigned Opc, unsigned &Spacing) {
  switch(Opc) {
  default: llvm_unreachable("unexpected opcode!");
  // VLD1LN
  case ARM::VLD1LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD1LNd8_UPD;
  case ARM::VLD1LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD1LNd16_UPD;
  case ARM::VLD1LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD1LNd32_UPD;
  case ARM::VLD1LNdWB_register_Asm_8:  Spacing = 1; return ARM::VLD1LNd8_UPD;
  case ARM::VLD1LNdWB_register_Asm_16: Spacing = 1; return ARM::VLD1LNd16_UPD;
  case ARM::VLD1LNdWB_register_Asm_32: Spacing = 1; return ARM::VLD1LNd32_UPD;
  case ARM::VLD1LNdAsm_8:  Spacing = 1; return ARM::VLD1LNd8;
  case ARM::VLD1LNdAsm_16: Spacing = 1; return ARM::VLD1LNd16;
  case ARM::VLD1LNdAsm_32: Spacing = 1; return ARM::VLD1LNd32;

  // VLD2LN
  case ARM::VLD2LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD2LNd8_UPD;
  case ARM::VLD2LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD2LNd16_UPD;
  case ARM::VLD2LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD2LNd32_UPD;
  case ARM::VLD2LNqWB_fixed_Asm_16: Spacing = 1; return ARM::VLD2LNq16_UPD;
  case ARM::VLD2LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VLD2LNq32_UPD;
  case ARM::VLD2LNdWB_register_Asm_8:  Spacing = 1; return ARM::VLD2LNd8_UPD;
  case ARM::VLD2LNdWB_register_Asm_16: Spacing = 1; return ARM::VLD2LNd16_UPD;
  case ARM::VLD2LNdWB_register_Asm_32: Spacing = 1; return ARM::VLD2LNd32_UPD;
  case ARM::VLD2LNqWB_register_Asm_16: Spacing = 2; return ARM::VLD2LNq16_UPD;
  case ARM::VLD2LNqWB_register_Asm_32: Spacing = 2; return ARM::VLD2LNq32_UPD;
  case ARM::VLD2LNdAsm_8:  Spacing = 1; return ARM::VLD2LNd8;
  case ARM::VLD2LNdAsm_16: Spacing = 1; return ARM::VLD2LNd16;
  case ARM::VLD2LNdAsm_32: Spacing = 1; return ARM::VLD2LNd32;
  case ARM::VLD2LNqAsm_16: Spacing = 2; return ARM::VLD2LNq16;
  case ARM::VLD2LNqAsm_32: Spacing = 2; return ARM::VLD2LNq32;

  // VLD3DUP
  case ARM::VLD3DUPdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD3DUPd8_UPD;
  case ARM::VLD3DUPdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD3DUPd16_UPD;
  case ARM::VLD3DUPdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD3DUPd32_UPD;
  case ARM::VLD3DUPqWB_fixed_Asm_8: Spacing = 1; return ARM::VLD3DUPq8_UPD;
  case ARM::VLD3DUPqWB_fixed_Asm_16: Spacing = 2; return ARM::VLD3DUPq16_UPD;
  case ARM::VLD3DUPqWB_fixed_Asm_32: Spacing = 2; return ARM::VLD3DUPq32_UPD;
  case ARM::VLD3DUPdWB_register_Asm_8:  Spacing = 1; return ARM::VLD3DUPd8_UPD;
  case ARM::VLD3DUPdWB_register_Asm_16: Spacing = 1; return ARM::VLD3DUPd16_UPD;
  case ARM::VLD3DUPdWB_register_Asm_32: Spacing = 1; return ARM::VLD3DUPd32_UPD;
  case ARM::VLD3DUPqWB_register_Asm_8: Spacing = 2; return ARM::VLD3DUPq8_UPD;
  case ARM::VLD3DUPqWB_register_Asm_16: Spacing = 2; return ARM::VLD3DUPq16_UPD;
  case ARM::VLD3DUPqWB_register_Asm_32: Spacing = 2; return ARM::VLD3DUPq32_UPD;
  case ARM::VLD3DUPdAsm_8:  Spacing = 1; return ARM::VLD3DUPd8;
  case ARM::VLD3DUPdAsm_16: Spacing = 1; return ARM::VLD3DUPd16;
  case ARM::VLD3DUPdAsm_32: Spacing = 1; return ARM::VLD3DUPd32;
  case ARM::VLD3DUPqAsm_8: Spacing = 2; return ARM::VLD3DUPq8;
  case ARM::VLD3DUPqAsm_16: Spacing = 2; return ARM::VLD3DUPq16;
  case ARM::VLD3DUPqAsm_32: Spacing = 2; return ARM::VLD3DUPq32;

  // VLD3LN
  case ARM::VLD3LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD3LNd8_UPD;
  case ARM::VLD3LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD3LNd16_UPD;
  case ARM::VLD3LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD3LNd32_UPD;
  case ARM::VLD3LNqWB_fixed_Asm_16: Spacing = 1; return ARM::VLD3LNq16_UPD;
  case ARM::VLD3LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VLD3LNq32_UPD;
  case ARM::VLD3LNdWB_register_Asm_8:  Spacing = 1; return ARM::VLD3LNd8_UPD;
  case ARM::VLD3LNdWB_register_Asm_16: Spacing = 1; return ARM::VLD3LNd16_UPD;
  case ARM::VLD3LNdWB_register_Asm_32: Spacing = 1; return ARM::VLD3LNd32_UPD;
  case ARM::VLD3LNqWB_register_Asm_16: Spacing = 2; return ARM::VLD3LNq16_UPD;
  case ARM::VLD3LNqWB_register_Asm_32: Spacing = 2; return ARM::VLD3LNq32_UPD;
  case ARM::VLD3LNdAsm_8:  Spacing = 1; return ARM::VLD3LNd8;
  case ARM::VLD3LNdAsm_16: Spacing = 1; return ARM::VLD3LNd16;
  case ARM::VLD3LNdAsm_32: Spacing = 1; return ARM::VLD3LNd32;
  case ARM::VLD3LNqAsm_16: Spacing = 2; return ARM::VLD3LNq16;
  case ARM::VLD3LNqAsm_32: Spacing = 2; return ARM::VLD3LNq32;

  // VLD3
  case ARM::VLD3dWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD3d8_UPD;
  case ARM::VLD3dWB_fixed_Asm_16: Spacing = 1; return ARM::VLD3d16_UPD;
  case ARM::VLD3dWB_fixed_Asm_32: Spacing = 1; return ARM::VLD3d32_UPD;
  case ARM::VLD3qWB_fixed_Asm_8:  Spacing = 2; return ARM::VLD3q8_UPD;
  case ARM::VLD3qWB_fixed_Asm_16: Spacing = 2; return ARM::VLD3q16_UPD;
  case ARM::VLD3qWB_fixed_Asm_32: Spacing = 2; return ARM::VLD3q32_UPD;
  case ARM::VLD3dWB_register_Asm_8:  Spacing = 1; return ARM::VLD3d8_UPD;
  case ARM::VLD3dWB_register_Asm_16: Spacing = 1; return ARM::VLD3d16_UPD;
  case ARM::VLD3dWB_register_Asm_32: Spacing = 1; return ARM::VLD3d32_UPD;
  case ARM::VLD3qWB_register_Asm_8:  Spacing = 2; return ARM::VLD3q8_UPD;
  case ARM::VLD3qWB_register_Asm_16: Spacing = 2; return ARM::VLD3q16_UPD;
  case ARM::VLD3qWB_register_Asm_32: Spacing = 2; return ARM::VLD3q32_UPD;
  case ARM::VLD3dAsm_8:  Spacing = 1; return ARM::VLD3d8;
  case ARM::VLD3dAsm_16: Spacing = 1; return ARM::VLD3d16;
  case ARM::VLD3dAsm_32: Spacing = 1; return ARM::VLD3d32;
  case ARM::VLD3qAsm_8:  Spacing = 2; return ARM::VLD3q8;
  case ARM::VLD3qAsm_16: Spacing = 2; return ARM::VLD3q16;
  case ARM::VLD3qAsm_32: Spacing = 2; return ARM::VLD3q32;

  // VLD4LN
  case ARM::VLD4LNdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD4LNd8_UPD;
  case ARM::VLD4LNdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD4LNd16_UPD;
  case ARM::VLD4LNdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD4LNd32_UPD;
  case ARM::VLD4LNqWB_fixed_Asm_16: Spacing = 2; return ARM::VLD4LNq16_UPD;
  case ARM::VLD4LNqWB_fixed_Asm_32: Spacing = 2; return ARM::VLD4LNq32_UPD;
  case ARM::VLD4LNdWB_register_Asm_8:  Spacing = 1; return ARM::VLD4LNd8_UPD;
  case ARM::VLD4LNdWB_register_Asm_16: Spacing = 1; return ARM::VLD4LNd16_UPD;
  case ARM::VLD4LNdWB_register_Asm_32: Spacing = 1; return ARM::VLD4LNd32_UPD;
  case ARM::VLD4LNqWB_register_Asm_16: Spacing = 2; return ARM::VLD4LNq16_UPD;
  case ARM::VLD4LNqWB_register_Asm_32: Spacing = 2; return ARM::VLD4LNq32_UPD;
  case ARM::VLD4LNdAsm_8:  Spacing = 1; return ARM::VLD4LNd8;
  case ARM::VLD4LNdAsm_16: Spacing = 1; return ARM::VLD4LNd16;
  case ARM::VLD4LNdAsm_32: Spacing = 1; return ARM::VLD4LNd32;
  case ARM::VLD4LNqAsm_16: Spacing = 2; return ARM::VLD4LNq16;
  case ARM::VLD4LNqAsm_32: Spacing = 2; return ARM::VLD4LNq32;

  // VLD4DUP
  case ARM::VLD4DUPdWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD4DUPd8_UPD;
  case ARM::VLD4DUPdWB_fixed_Asm_16: Spacing = 1; return ARM::VLD4DUPd16_UPD;
  case ARM::VLD4DUPdWB_fixed_Asm_32: Spacing = 1; return ARM::VLD4DUPd32_UPD;
  case ARM::VLD4DUPqWB_fixed_Asm_8: Spacing = 1; return ARM::VLD4DUPq8_UPD;
  case ARM::VLD4DUPqWB_fixed_Asm_16: Spacing = 1; return ARM::VLD4DUPq16_UPD;
  case ARM::VLD4DUPqWB_fixed_Asm_32: Spacing = 2; return ARM::VLD4DUPq32_UPD;
  case ARM::VLD4DUPdWB_register_Asm_8:  Spacing = 1; return ARM::VLD4DUPd8_UPD;
  case ARM::VLD4DUPdWB_register_Asm_16: Spacing = 1; return ARM::VLD4DUPd16_UPD;
  case ARM::VLD4DUPdWB_register_Asm_32: Spacing = 1; return ARM::VLD4DUPd32_UPD;
  case ARM::VLD4DUPqWB_register_Asm_8: Spacing = 2; return ARM::VLD4DUPq8_UPD;
  case ARM::VLD4DUPqWB_register_Asm_16: Spacing = 2; return ARM::VLD4DUPq16_UPD;
  case ARM::VLD4DUPqWB_register_Asm_32: Spacing = 2; return ARM::VLD4DUPq32_UPD;
  case ARM::VLD4DUPdAsm_8:  Spacing = 1; return ARM::VLD4DUPd8;
  case ARM::VLD4DUPdAsm_16: Spacing = 1; return ARM::VLD4DUPd16;
  case ARM::VLD4DUPdAsm_32: Spacing = 1; return ARM::VLD4DUPd32;
  case ARM::VLD4DUPqAsm_8: Spacing = 2; return ARM::VLD4DUPq8;
  case ARM::VLD4DUPqAsm_16: Spacing = 2; return ARM::VLD4DUPq16;
  case ARM::VLD4DUPqAsm_32: Spacing = 2; return ARM::VLD4DUPq32;

  // VLD4
  case ARM::VLD4dWB_fixed_Asm_8:  Spacing = 1; return ARM::VLD4d8_UPD;
  case ARM::VLD4dWB_fixed_Asm_16: Spacing = 1; return ARM::VLD4d16_UPD;
  case ARM::VLD4dWB_fixed_Asm_32: Spacing = 1; return ARM::VLD4d32_UPD;
  case ARM::VLD4qWB_fixed_Asm_8:  Spacing = 2; return ARM::VLD4q8_UPD;
  case ARM::VLD4qWB_fixed_Asm_16: Spacing = 2; return ARM::VLD4q16_UPD;
  case ARM::VLD4qWB_fixed_Asm_32: Spacing = 2; return ARM::VLD4q32_UPD;
  case ARM::VLD4dWB_register_Asm_8:  Spacing = 1; return ARM::VLD4d8_UPD;
  case ARM::VLD4dWB_register_Asm_16: Spacing = 1; return ARM::VLD4d16_UPD;
  case ARM::VLD4dWB_register_Asm_32: Spacing = 1; return ARM::VLD4d32_UPD;
  case ARM::VLD4qWB_register_Asm_8:  Spacing = 2; return ARM::VLD4q8_UPD;
  case ARM::VLD4qWB_register_Asm_16: Spacing = 2; return ARM::VLD4q16_UPD;
  case ARM::VLD4qWB_register_Asm_32: Spacing = 2; return ARM::VLD4q32_UPD;
  case ARM::VLD4dAsm_8:  Spacing = 1; return ARM::VLD4d8;
  case ARM::VLD4dAsm_16: Spacing = 1; return ARM::VLD4d16;
  case ARM::VLD4dAsm_32: Spacing = 1; return ARM::VLD4d32;
  case ARM::VLD4qAsm_8:  Spacing = 2; return ARM::VLD4q8;
  case ARM::VLD4qAsm_16: Spacing = 2; return ARM::VLD4q16;
  case ARM::VLD4qAsm_32: Spacing = 2; return ARM::VLD4q32;
  }
}

bool ARMAsmParser::processInstruction(MCInst &Inst,
                                      const OperandVector &Operands,
                                      MCStreamer &Out) {
  // Check if we have the wide qualifier, because if it's present we
  // must avoid selecting a 16-bit thumb instruction.
  bool HasWideQualifier = false;
  for (auto &Op : Operands) {
    ARMOperand &ARMOp = static_cast<ARMOperand&>(*Op);
    if (ARMOp.isToken() && ARMOp.getToken() == ".w") {
      HasWideQualifier = true;
      break;
    }
  }

  switch (Inst.getOpcode()) {
  // Alias for alternate form of 'ldr{,b}t Rt, [Rn], #imm' instruction.
  case ARM::LDRT_POST:
  case ARM::LDRBT_POST: {
    const unsigned Opcode =
      (Inst.getOpcode() == ARM::LDRT_POST) ? ARM::LDRT_POST_IMM
                                           : ARM::LDRBT_POST_IMM;
    MCInst TmpInst;
    TmpInst.setOpcode(Opcode);
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(1));
    TmpInst.addOperand(Inst.getOperand(1));
    TmpInst.addOperand(MCOperand::createReg(0));
    TmpInst.addOperand(MCOperand::createImm(0));
    TmpInst.addOperand(Inst.getOperand(2));
    TmpInst.addOperand(Inst.getOperand(3));
    Inst = TmpInst;
    return true;
  }
  // Alias for alternate form of 'str{,b}t Rt, [Rn], #imm' instruction.
  case ARM::STRT_POST:
  case ARM::STRBT_POST: {
    const unsigned Opcode =
      (Inst.getOpcode() == ARM::STRT_POST) ? ARM::STRT_POST_IMM
                                           : ARM::STRBT_POST_IMM;
    MCInst TmpInst;
    TmpInst.setOpcode(Opcode);
    TmpInst.addOperand(Inst.getOperand(1));
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(1));
    TmpInst.addOperand(MCOperand::createReg(0));
    TmpInst.addOperand(MCOperand::createImm(0));
    TmpInst.addOperand(Inst.getOperand(2));
    TmpInst.addOperand(Inst.getOperand(3));
    Inst = TmpInst;
    return true;
  }
  // Alias for alternate form of 'ADR Rd, #imm' instruction.
  case ARM::ADDri: {
    if (Inst.getOperand(1).getReg() != ARM::PC ||
        Inst.getOperand(5).getReg() != 0 ||
        !(Inst.getOperand(2).isExpr() || Inst.getOperand(2).isImm()))
      return false;
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::ADR);
    TmpInst.addOperand(Inst.getOperand(0));
    if (Inst.getOperand(2).isImm()) {
      // Immediate (mod_imm) will be in its encoded form, we must unencode it
      // before passing it to the ADR instruction.
      unsigned Enc = Inst.getOperand(2).getImm();
      TmpInst.addOperand(MCOperand::createImm(
        ARM_AM::rotr32(Enc & 0xFF, (Enc & 0xF00) >> 7)));
    } else {
      // Turn PC-relative expression into absolute expression.
      // Reading PC provides the start of the current instruction + 8 and
      // the transform to adr is biased by that.
      MCSymbol *Dot = getContext().createTempSymbol();
      Out.EmitLabel(Dot);
      const MCExpr *OpExpr = Inst.getOperand(2).getExpr();
      const MCExpr *InstPC = MCSymbolRefExpr::create(Dot,
                                                     MCSymbolRefExpr::VK_None,
                                                     getContext());
      const MCExpr *Const8 = MCConstantExpr::create(8, getContext());
      const MCExpr *ReadPC = MCBinaryExpr::createAdd(InstPC, Const8,
                                                     getContext());
      const MCExpr *FixupAddr = MCBinaryExpr::createAdd(ReadPC, OpExpr,
                                                        getContext());
      TmpInst.addOperand(MCOperand::createExpr(FixupAddr));
    }
    TmpInst.addOperand(Inst.getOperand(3));
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }
  // Aliases for alternate PC+imm syntax of LDR instructions.
  case ARM::t2LDRpcrel:
    // Select the narrow version if the immediate will fit.
    if (Inst.getOperand(1).getImm() > 0 &&
        Inst.getOperand(1).getImm() <= 0xff &&
        !HasWideQualifier)
      Inst.setOpcode(ARM::tLDRpci);
    else
      Inst.setOpcode(ARM::t2LDRpci);
    return true;
  case ARM::t2LDRBpcrel:
    Inst.setOpcode(ARM::t2LDRBpci);
    return true;
  case ARM::t2LDRHpcrel:
    Inst.setOpcode(ARM::t2LDRHpci);
    return true;
  case ARM::t2LDRSBpcrel:
    Inst.setOpcode(ARM::t2LDRSBpci);
    return true;
  case ARM::t2LDRSHpcrel:
    Inst.setOpcode(ARM::t2LDRSHpci);
    return true;
  case ARM::LDRConstPool:
  case ARM::tLDRConstPool:
  case ARM::t2LDRConstPool: {
    // Pseudo instruction ldr rt, =immediate is converted to a
    // MOV rt, immediate if immediate is known and representable
    // otherwise we create a constant pool entry that we load from.
    MCInst TmpInst;
    if (Inst.getOpcode() == ARM::LDRConstPool)
      TmpInst.setOpcode(ARM::LDRi12);
    else if (Inst.getOpcode() == ARM::tLDRConstPool)
      TmpInst.setOpcode(ARM::tLDRpci);
    else if (Inst.getOpcode() == ARM::t2LDRConstPool)
      TmpInst.setOpcode(ARM::t2LDRpci);
    const ARMOperand &PoolOperand =
      (HasWideQualifier ?
       static_cast<ARMOperand &>(*Operands[4]) :
       static_cast<ARMOperand &>(*Operands[3]));
    const MCExpr *SubExprVal = PoolOperand.getConstantPoolImm();
    // If SubExprVal is a constant we may be able to use a MOV
    if (isa<MCConstantExpr>(SubExprVal) &&
        Inst.getOperand(0).getReg() != ARM::PC &&
        Inst.getOperand(0).getReg() != ARM::SP) {
      int64_t Value =
        (int64_t) (cast<MCConstantExpr>(SubExprVal))->getValue();
      bool UseMov  = true;
      bool MovHasS = true;
      if (Inst.getOpcode() == ARM::LDRConstPool) {
        // ARM Constant
        if (ARM_AM::getSOImmVal(Value) != -1) {
          Value = ARM_AM::getSOImmVal(Value);
          TmpInst.setOpcode(ARM::MOVi);
        }
        else if (ARM_AM::getSOImmVal(~Value) != -1) {
          Value = ARM_AM::getSOImmVal(~Value);
          TmpInst.setOpcode(ARM::MVNi);
        }
        else if (hasV6T2Ops() &&
                 Value >=0 && Value < 65536) {
          TmpInst.setOpcode(ARM::MOVi16);
          MovHasS = false;
        }
        else
          UseMov = false;
      }
      else {
        // Thumb/Thumb2 Constant
        if (hasThumb2() &&
            ARM_AM::getT2SOImmVal(Value) != -1)
          TmpInst.setOpcode(ARM::t2MOVi);
        else if (hasThumb2() &&
                 ARM_AM::getT2SOImmVal(~Value) != -1) {
          TmpInst.setOpcode(ARM::t2MVNi);
          Value = ~Value;
        }
        else if (hasV8MBaseline() &&
                 Value >=0 && Value < 65536) {
          TmpInst.setOpcode(ARM::t2MOVi16);
          MovHasS = false;
        }
        else
          UseMov = false;
      }
      if (UseMov) {
        TmpInst.addOperand(Inst.getOperand(0));           // Rt
        TmpInst.addOperand(MCOperand::createImm(Value));  // Immediate
        TmpInst.addOperand(Inst.getOperand(2));           // CondCode
        TmpInst.addOperand(Inst.getOperand(3));           // CondCode
        if (MovHasS)
          TmpInst.addOperand(MCOperand::createReg(0));    // S
        Inst = TmpInst;
        return true;
      }
    }
    // No opportunity to use MOV/MVN create constant pool
    const MCExpr *CPLoc =
      getTargetStreamer().addConstantPoolEntry(SubExprVal,
                                               PoolOperand.getStartLoc());
    TmpInst.addOperand(Inst.getOperand(0));           // Rt
    TmpInst.addOperand(MCOperand::createExpr(CPLoc)); // offset to constpool
    if (TmpInst.getOpcode() == ARM::LDRi12)
      TmpInst.addOperand(MCOperand::createImm(0));    // unused offset
    TmpInst.addOperand(Inst.getOperand(2));           // CondCode
    TmpInst.addOperand(Inst.getOperand(3));           // CondCode
    Inst = TmpInst;
    return true;
  }
  // Handle NEON VST complex aliases.
  case ARM::VST1LNdWB_register_Asm_8:
  case ARM::VST1LNdWB_register_Asm_16:
  case ARM::VST1LNdWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST2LNdWB_register_Asm_8:
  case ARM::VST2LNdWB_register_Asm_16:
  case ARM::VST2LNdWB_register_Asm_32:
  case ARM::VST2LNqWB_register_Asm_16:
  case ARM::VST2LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST3LNdWB_register_Asm_8:
  case ARM::VST3LNdWB_register_Asm_16:
  case ARM::VST3LNdWB_register_Asm_32:
  case ARM::VST3LNqWB_register_Asm_16:
  case ARM::VST3LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST4LNdWB_register_Asm_8:
  case ARM::VST4LNdWB_register_Asm_16:
  case ARM::VST4LNdWB_register_Asm_32:
  case ARM::VST4LNqWB_register_Asm_16:
  case ARM::VST4LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST1LNdWB_fixed_Asm_8:
  case ARM::VST1LNdWB_fixed_Asm_16:
  case ARM::VST1LNdWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST2LNdWB_fixed_Asm_8:
  case ARM::VST2LNdWB_fixed_Asm_16:
  case ARM::VST2LNdWB_fixed_Asm_32:
  case ARM::VST2LNqWB_fixed_Asm_16:
  case ARM::VST2LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST3LNdWB_fixed_Asm_8:
  case ARM::VST3LNdWB_fixed_Asm_16:
  case ARM::VST3LNdWB_fixed_Asm_32:
  case ARM::VST3LNqWB_fixed_Asm_16:
  case ARM::VST3LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST4LNdWB_fixed_Asm_8:
  case ARM::VST4LNdWB_fixed_Asm_16:
  case ARM::VST4LNdWB_fixed_Asm_32:
  case ARM::VST4LNqWB_fixed_Asm_16:
  case ARM::VST4LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST1LNdAsm_8:
  case ARM::VST1LNdAsm_16:
  case ARM::VST1LNdAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST2LNdAsm_8:
  case ARM::VST2LNdAsm_16:
  case ARM::VST2LNdAsm_32:
  case ARM::VST2LNqAsm_16:
  case ARM::VST2LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST3LNdAsm_8:
  case ARM::VST3LNdAsm_16:
  case ARM::VST3LNdAsm_32:
  case ARM::VST3LNqAsm_16:
  case ARM::VST3LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST4LNdAsm_8:
  case ARM::VST4LNdAsm_16:
  case ARM::VST4LNdAsm_32:
  case ARM::VST4LNqAsm_16:
  case ARM::VST4LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // Handle NEON VLD complex aliases.
  case ARM::VLD1LNdWB_register_Asm_8:
  case ARM::VLD1LNdWB_register_Asm_16:
  case ARM::VLD1LNdWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD2LNdWB_register_Asm_8:
  case ARM::VLD2LNdWB_register_Asm_16:
  case ARM::VLD2LNdWB_register_Asm_32:
  case ARM::VLD2LNqWB_register_Asm_16:
  case ARM::VLD2LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3LNdWB_register_Asm_8:
  case ARM::VLD3LNdWB_register_Asm_16:
  case ARM::VLD3LNdWB_register_Asm_32:
  case ARM::VLD3LNqWB_register_Asm_16:
  case ARM::VLD3LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4LNdWB_register_Asm_8:
  case ARM::VLD4LNdWB_register_Asm_16:
  case ARM::VLD4LNdWB_register_Asm_32:
  case ARM::VLD4LNqWB_register_Asm_16:
  case ARM::VLD4LNqWB_register_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(4)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(5)); // CondCode
    TmpInst.addOperand(Inst.getOperand(6));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD1LNdWB_fixed_Asm_8:
  case ARM::VLD1LNdWB_fixed_Asm_16:
  case ARM::VLD1LNdWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD2LNdWB_fixed_Asm_8:
  case ARM::VLD2LNdWB_fixed_Asm_16:
  case ARM::VLD2LNdWB_fixed_Asm_32:
  case ARM::VLD2LNqWB_fixed_Asm_16:
  case ARM::VLD2LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3LNdWB_fixed_Asm_8:
  case ARM::VLD3LNdWB_fixed_Asm_16:
  case ARM::VLD3LNdWB_fixed_Asm_32:
  case ARM::VLD3LNqWB_fixed_Asm_16:
  case ARM::VLD3LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4LNdWB_fixed_Asm_8:
  case ARM::VLD4LNdWB_fixed_Asm_16:
  case ARM::VLD4LNdWB_fixed_Asm_32:
  case ARM::VLD4LNqWB_fixed_Asm_16:
  case ARM::VLD4LNqWB_fixed_Asm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD1LNdAsm_8:
  case ARM::VLD1LNdAsm_16:
  case ARM::VLD1LNdAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD2LNdAsm_8:
  case ARM::VLD2LNdAsm_16:
  case ARM::VLD2LNdAsm_32:
  case ARM::VLD2LNqAsm_16:
  case ARM::VLD2LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3LNdAsm_8:
  case ARM::VLD3LNdAsm_16:
  case ARM::VLD3LNdAsm_32:
  case ARM::VLD3LNqAsm_16:
  case ARM::VLD3LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4LNdAsm_8:
  case ARM::VLD4LNdAsm_16:
  case ARM::VLD4LNdAsm_32:
  case ARM::VLD4LNqAsm_16:
  case ARM::VLD4LNqAsm_32: {
    MCInst TmpInst;
    // Shuffle the operands around so the lane index operand is in the
    // right place.
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(2)); // Rn
    TmpInst.addOperand(Inst.getOperand(3)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Tied operand src (== Vd)
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // lane
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VLD3DUP single 3-element structure to all lanes instructions.
  case ARM::VLD3DUPdAsm_8:
  case ARM::VLD3DUPdAsm_16:
  case ARM::VLD3DUPdAsm_32:
  case ARM::VLD3DUPqAsm_8:
  case ARM::VLD3DUPqAsm_16:
  case ARM::VLD3DUPqAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3DUPdWB_fixed_Asm_8:
  case ARM::VLD3DUPdWB_fixed_Asm_16:
  case ARM::VLD3DUPdWB_fixed_Asm_32:
  case ARM::VLD3DUPqWB_fixed_Asm_8:
  case ARM::VLD3DUPqWB_fixed_Asm_16:
  case ARM::VLD3DUPqWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3DUPdWB_register_Asm_8:
  case ARM::VLD3DUPdWB_register_Asm_16:
  case ARM::VLD3DUPdWB_register_Asm_32:
  case ARM::VLD3DUPqWB_register_Asm_8:
  case ARM::VLD3DUPqWB_register_Asm_16:
  case ARM::VLD3DUPqWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VLD3 multiple 3-element structure instructions.
  case ARM::VLD3dAsm_8:
  case ARM::VLD3dAsm_16:
  case ARM::VLD3dAsm_32:
  case ARM::VLD3qAsm_8:
  case ARM::VLD3qAsm_16:
  case ARM::VLD3qAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3dWB_fixed_Asm_8:
  case ARM::VLD3dWB_fixed_Asm_16:
  case ARM::VLD3dWB_fixed_Asm_32:
  case ARM::VLD3qWB_fixed_Asm_8:
  case ARM::VLD3qWB_fixed_Asm_16:
  case ARM::VLD3qWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD3dWB_register_Asm_8:
  case ARM::VLD3dWB_register_Asm_16:
  case ARM::VLD3dWB_register_Asm_32:
  case ARM::VLD3qWB_register_Asm_8:
  case ARM::VLD3qWB_register_Asm_16:
  case ARM::VLD3qWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VLD4DUP single 3-element structure to all lanes instructions.
  case ARM::VLD4DUPdAsm_8:
  case ARM::VLD4DUPdAsm_16:
  case ARM::VLD4DUPdAsm_32:
  case ARM::VLD4DUPqAsm_8:
  case ARM::VLD4DUPqAsm_16:
  case ARM::VLD4DUPqAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4DUPdWB_fixed_Asm_8:
  case ARM::VLD4DUPdWB_fixed_Asm_16:
  case ARM::VLD4DUPdWB_fixed_Asm_32:
  case ARM::VLD4DUPqWB_fixed_Asm_8:
  case ARM::VLD4DUPqWB_fixed_Asm_16:
  case ARM::VLD4DUPqWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4DUPdWB_register_Asm_8:
  case ARM::VLD4DUPdWB_register_Asm_16:
  case ARM::VLD4DUPdWB_register_Asm_32:
  case ARM::VLD4DUPqWB_register_Asm_8:
  case ARM::VLD4DUPqWB_register_Asm_16:
  case ARM::VLD4DUPqWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VLD4 multiple 4-element structure instructions.
  case ARM::VLD4dAsm_8:
  case ARM::VLD4dAsm_16:
  case ARM::VLD4dAsm_32:
  case ARM::VLD4qAsm_8:
  case ARM::VLD4qAsm_16:
  case ARM::VLD4qAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4dWB_fixed_Asm_8:
  case ARM::VLD4dWB_fixed_Asm_16:
  case ARM::VLD4dWB_fixed_Asm_32:
  case ARM::VLD4qWB_fixed_Asm_8:
  case ARM::VLD4qWB_fixed_Asm_16:
  case ARM::VLD4qWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VLD4dWB_register_Asm_8:
  case ARM::VLD4dWB_register_Asm_16:
  case ARM::VLD4dWB_register_Asm_32:
  case ARM::VLD4qWB_register_Asm_8:
  case ARM::VLD4qWB_register_Asm_16:
  case ARM::VLD4qWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVLDOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VST3 multiple 3-element structure instructions.
  case ARM::VST3dAsm_8:
  case ARM::VST3dAsm_16:
  case ARM::VST3dAsm_32:
  case ARM::VST3qAsm_8:
  case ARM::VST3qAsm_16:
  case ARM::VST3qAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST3dWB_fixed_Asm_8:
  case ARM::VST3dWB_fixed_Asm_16:
  case ARM::VST3dWB_fixed_Asm_32:
  case ARM::VST3qWB_fixed_Asm_8:
  case ARM::VST3qWB_fixed_Asm_16:
  case ARM::VST3qWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST3dWB_register_Asm_8:
  case ARM::VST3dWB_register_Asm_16:
  case ARM::VST3dWB_register_Asm_32:
  case ARM::VST3qWB_register_Asm_8:
  case ARM::VST3qWB_register_Asm_16:
  case ARM::VST3qWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // VST4 multiple 3-element structure instructions.
  case ARM::VST4dAsm_8:
  case ARM::VST4dAsm_16:
  case ARM::VST4dAsm_32:
  case ARM::VST4qAsm_8:
  case ARM::VST4qAsm_16:
  case ARM::VST4qAsm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST4dWB_fixed_Asm_8:
  case ARM::VST4dWB_fixed_Asm_16:
  case ARM::VST4dWB_fixed_Asm_32:
  case ARM::VST4qWB_fixed_Asm_8:
  case ARM::VST4qWB_fixed_Asm_16:
  case ARM::VST4qWB_fixed_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(MCOperand::createReg(0)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }

  case ARM::VST4dWB_register_Asm_8:
  case ARM::VST4dWB_register_Asm_16:
  case ARM::VST4dWB_register_Asm_32:
  case ARM::VST4qWB_register_Asm_8:
  case ARM::VST4qWB_register_Asm_16:
  case ARM::VST4qWB_register_Asm_32: {
    MCInst TmpInst;
    unsigned Spacing;
    TmpInst.setOpcode(getRealVSTOpcode(Inst.getOpcode(), Spacing));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(1)); // Rn_wb == tied Rn
    TmpInst.addOperand(Inst.getOperand(2)); // alignment
    TmpInst.addOperand(Inst.getOperand(3)); // Rm
    TmpInst.addOperand(Inst.getOperand(0)); // Vd
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 2));
    TmpInst.addOperand(MCOperand::createReg(Inst.getOperand(0).getReg() +
                                            Spacing * 3));
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    Inst = TmpInst;
    return true;
  }

  // Handle encoding choice for the shift-immediate instructions.
  case ARM::t2LSLri:
  case ARM::t2LSRri:
  case ARM::t2ASRri:
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        Inst.getOperand(5).getReg() == (inITBlock() ? 0 : ARM::CPSR) &&
        !HasWideQualifier) {
      unsigned NewOpc;
      switch (Inst.getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case ARM::t2LSLri: NewOpc = ARM::tLSLri; break;
      case ARM::t2LSRri: NewOpc = ARM::tLSRri; break;
      case ARM::t2ASRri: NewOpc = ARM::tASRri; break;
      }
      // The Thumb1 operands aren't in the same order. Awesome, eh?
      MCInst TmpInst;
      TmpInst.setOpcode(NewOpc);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(5));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(2));
      TmpInst.addOperand(Inst.getOperand(3));
      TmpInst.addOperand(Inst.getOperand(4));
      Inst = TmpInst;
      return true;
    }
    return false;

  // Handle the Thumb2 mode MOV complex aliases.
  case ARM::t2MOVsr:
  case ARM::t2MOVSsr: {
    // Which instruction to expand to depends on the CCOut operand and
    // whether we're in an IT block if the register operands are low
    // registers.
    bool isNarrow = false;
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        isARMLowRegister(Inst.getOperand(2).getReg()) &&
        Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg() &&
        inITBlock() == (Inst.getOpcode() == ARM::t2MOVsr) &&
        !HasWideQualifier)
      isNarrow = true;
    MCInst TmpInst;
    unsigned newOpc;
    switch(ARM_AM::getSORegShOp(Inst.getOperand(3).getImm())) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM_AM::asr: newOpc = isNarrow ? ARM::tASRrr : ARM::t2ASRrr; break;
    case ARM_AM::lsr: newOpc = isNarrow ? ARM::tLSRrr : ARM::t2LSRrr; break;
    case ARM_AM::lsl: newOpc = isNarrow ? ARM::tLSLrr : ARM::t2LSLrr; break;
    case ARM_AM::ror: newOpc = isNarrow ? ARM::tROR   : ARM::t2RORrr; break;
    }
    TmpInst.setOpcode(newOpc);
    TmpInst.addOperand(Inst.getOperand(0)); // Rd
    if (isNarrow)
      TmpInst.addOperand(MCOperand::createReg(
          Inst.getOpcode() == ARM::t2MOVSsr ? ARM::CPSR : 0));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // Rm
    TmpInst.addOperand(Inst.getOperand(4)); // CondCode
    TmpInst.addOperand(Inst.getOperand(5));
    if (!isNarrow)
      TmpInst.addOperand(MCOperand::createReg(
          Inst.getOpcode() == ARM::t2MOVSsr ? ARM::CPSR : 0));
    Inst = TmpInst;
    return true;
  }
  case ARM::t2MOVsi:
  case ARM::t2MOVSsi: {
    // Which instruction to expand to depends on the CCOut operand and
    // whether we're in an IT block if the register operands are low
    // registers.
    bool isNarrow = false;
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        inITBlock() == (Inst.getOpcode() == ARM::t2MOVsi) &&
        !HasWideQualifier)
      isNarrow = true;
    MCInst TmpInst;
    unsigned newOpc;
    unsigned Shift = ARM_AM::getSORegShOp(Inst.getOperand(2).getImm());
    unsigned Amount = ARM_AM::getSORegOffset(Inst.getOperand(2).getImm());
    bool isMov = false;
    // MOV rd, rm, LSL #0 is actually a MOV instruction
    if (Shift == ARM_AM::lsl && Amount == 0) {
      isMov = true;
      // The 16-bit encoding of MOV rd, rm, LSL #N is explicitly encoding T2 of
      // MOV (register) in the ARMv8-A and ARMv8-M manuals, and immediate 0 is
      // unpredictable in an IT block so the 32-bit encoding T3 has to be used
      // instead.
      if (inITBlock()) {
        isNarrow = false;
      }
      newOpc = isNarrow ? ARM::tMOVSr : ARM::t2MOVr;
    } else {
      switch(Shift) {
      default: llvm_unreachable("unexpected opcode!");
      case ARM_AM::asr: newOpc = isNarrow ? ARM::tASRri : ARM::t2ASRri; break;
      case ARM_AM::lsr: newOpc = isNarrow ? ARM::tLSRri : ARM::t2LSRri; break;
      case ARM_AM::lsl: newOpc = isNarrow ? ARM::tLSLri : ARM::t2LSLri; break;
      case ARM_AM::ror: newOpc = ARM::t2RORri; isNarrow = false; break;
      case ARM_AM::rrx: isNarrow = false; newOpc = ARM::t2RRX; break;
      }
    }
    if (Amount == 32) Amount = 0;
    TmpInst.setOpcode(newOpc);
    TmpInst.addOperand(Inst.getOperand(0)); // Rd
    if (isNarrow && !isMov)
      TmpInst.addOperand(MCOperand::createReg(
          Inst.getOpcode() == ARM::t2MOVSsi ? ARM::CPSR : 0));
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    if (newOpc != ARM::t2RRX && !isMov)
      TmpInst.addOperand(MCOperand::createImm(Amount));
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    if (!isNarrow)
      TmpInst.addOperand(MCOperand::createReg(
          Inst.getOpcode() == ARM::t2MOVSsi ? ARM::CPSR : 0));
    Inst = TmpInst;
    return true;
  }
  // Handle the ARM mode MOV complex aliases.
  case ARM::ASRr:
  case ARM::LSRr:
  case ARM::LSLr:
  case ARM::RORr: {
    ARM_AM::ShiftOpc ShiftTy;
    switch(Inst.getOpcode()) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM::ASRr: ShiftTy = ARM_AM::asr; break;
    case ARM::LSRr: ShiftTy = ARM_AM::lsr; break;
    case ARM::LSLr: ShiftTy = ARM_AM::lsl; break;
    case ARM::RORr: ShiftTy = ARM_AM::ror; break;
    }
    unsigned Shifter = ARM_AM::getSORegOpc(ShiftTy, 0);
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::MOVsr);
    TmpInst.addOperand(Inst.getOperand(0)); // Rd
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(Inst.getOperand(2)); // Rm
    TmpInst.addOperand(MCOperand::createImm(Shifter)); // Shift value and ty
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    TmpInst.addOperand(Inst.getOperand(5)); // cc_out
    Inst = TmpInst;
    return true;
  }
  case ARM::ASRi:
  case ARM::LSRi:
  case ARM::LSLi:
  case ARM::RORi: {
    ARM_AM::ShiftOpc ShiftTy;
    switch(Inst.getOpcode()) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM::ASRi: ShiftTy = ARM_AM::asr; break;
    case ARM::LSRi: ShiftTy = ARM_AM::lsr; break;
    case ARM::LSLi: ShiftTy = ARM_AM::lsl; break;
    case ARM::RORi: ShiftTy = ARM_AM::ror; break;
    }
    // A shift by zero is a plain MOVr, not a MOVsi.
    unsigned Amt = Inst.getOperand(2).getImm();
    unsigned Opc = Amt == 0 ? ARM::MOVr : ARM::MOVsi;
    // A shift by 32 should be encoded as 0 when permitted
    if (Amt == 32 && (ShiftTy == ARM_AM::lsr || ShiftTy == ARM_AM::asr))
      Amt = 0;
    unsigned Shifter = ARM_AM::getSORegOpc(ShiftTy, Amt);
    MCInst TmpInst;
    TmpInst.setOpcode(Opc);
    TmpInst.addOperand(Inst.getOperand(0)); // Rd
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    if (Opc == ARM::MOVsi)
      TmpInst.addOperand(MCOperand::createImm(Shifter)); // Shift value and ty
    TmpInst.addOperand(Inst.getOperand(3)); // CondCode
    TmpInst.addOperand(Inst.getOperand(4));
    TmpInst.addOperand(Inst.getOperand(5)); // cc_out
    Inst = TmpInst;
    return true;
  }
  case ARM::RRXi: {
    unsigned Shifter = ARM_AM::getSORegOpc(ARM_AM::rrx, 0);
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::MOVsi);
    TmpInst.addOperand(Inst.getOperand(0)); // Rd
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(MCOperand::createImm(Shifter)); // Shift value and ty
    TmpInst.addOperand(Inst.getOperand(2)); // CondCode
    TmpInst.addOperand(Inst.getOperand(3));
    TmpInst.addOperand(Inst.getOperand(4)); // cc_out
    Inst = TmpInst;
    return true;
  }
  case ARM::t2LDMIA_UPD: {
    // If this is a load of a single register, then we should use
    // a post-indexed LDR instruction instead, per the ARM ARM.
    if (Inst.getNumOperands() != 5)
      return false;
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::t2LDR_POST);
    TmpInst.addOperand(Inst.getOperand(4)); // Rt
    TmpInst.addOperand(Inst.getOperand(0)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(MCOperand::createImm(4));
    TmpInst.addOperand(Inst.getOperand(2)); // CondCode
    TmpInst.addOperand(Inst.getOperand(3));
    Inst = TmpInst;
    return true;
  }
  case ARM::t2STMDB_UPD: {
    // If this is a store of a single register, then we should use
    // a pre-indexed STR instruction instead, per the ARM ARM.
    if (Inst.getNumOperands() != 5)
      return false;
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::t2STR_PRE);
    TmpInst.addOperand(Inst.getOperand(0)); // Rn_wb
    TmpInst.addOperand(Inst.getOperand(4)); // Rt
    TmpInst.addOperand(Inst.getOperand(1)); // Rn
    TmpInst.addOperand(MCOperand::createImm(-4));
    TmpInst.addOperand(Inst.getOperand(2)); // CondCode
    TmpInst.addOperand(Inst.getOperand(3));
    Inst = TmpInst;
    return true;
  }
  case ARM::LDMIA_UPD:
    // If this is a load of a single register via a 'pop', then we should use
    // a post-indexed LDR instruction instead, per the ARM ARM.
    if (static_cast<ARMOperand &>(*Operands[0]).getToken() == "pop" &&
        Inst.getNumOperands() == 5) {
      MCInst TmpInst;
      TmpInst.setOpcode(ARM::LDR_POST_IMM);
      TmpInst.addOperand(Inst.getOperand(4)); // Rt
      TmpInst.addOperand(Inst.getOperand(0)); // Rn_wb
      TmpInst.addOperand(Inst.getOperand(1)); // Rn
      TmpInst.addOperand(MCOperand::createReg(0));  // am2offset
      TmpInst.addOperand(MCOperand::createImm(4));
      TmpInst.addOperand(Inst.getOperand(2)); // CondCode
      TmpInst.addOperand(Inst.getOperand(3));
      Inst = TmpInst;
      return true;
    }
    break;
  case ARM::STMDB_UPD:
    // If this is a store of a single register via a 'push', then we should use
    // a pre-indexed STR instruction instead, per the ARM ARM.
    if (static_cast<ARMOperand &>(*Operands[0]).getToken() == "push" &&
        Inst.getNumOperands() == 5) {
      MCInst TmpInst;
      TmpInst.setOpcode(ARM::STR_PRE_IMM);
      TmpInst.addOperand(Inst.getOperand(0)); // Rn_wb
      TmpInst.addOperand(Inst.getOperand(4)); // Rt
      TmpInst.addOperand(Inst.getOperand(1)); // addrmode_imm12
      TmpInst.addOperand(MCOperand::createImm(-4));
      TmpInst.addOperand(Inst.getOperand(2)); // CondCode
      TmpInst.addOperand(Inst.getOperand(3));
      Inst = TmpInst;
    }
    break;
  case ARM::t2ADDri12:
    // If the immediate fits for encoding T3 (t2ADDri) and the generic "add"
    // mnemonic was used (not "addw"), encoding T3 is preferred.
    if (static_cast<ARMOperand &>(*Operands[0]).getToken() != "add" ||
        ARM_AM::getT2SOImmVal(Inst.getOperand(2).getImm()) == -1)
      break;
    Inst.setOpcode(ARM::t2ADDri);
    Inst.addOperand(MCOperand::createReg(0)); // cc_out
    break;
  case ARM::t2SUBri12:
    // If the immediate fits for encoding T3 (t2SUBri) and the generic "sub"
    // mnemonic was used (not "subw"), encoding T3 is preferred.
    if (static_cast<ARMOperand &>(*Operands[0]).getToken() != "sub" ||
        ARM_AM::getT2SOImmVal(Inst.getOperand(2).getImm()) == -1)
      break;
    Inst.setOpcode(ARM::t2SUBri);
    Inst.addOperand(MCOperand::createReg(0)); // cc_out
    break;
  case ARM::tADDi8:
    // If the immediate is in the range 0-7, we want tADDi3 iff Rd was
    // explicitly specified. From the ARM ARM: "Encoding T1 is preferred
    // to encoding T2 if <Rd> is specified and encoding T2 is preferred
    // to encoding T1 if <Rd> is omitted."
    if ((unsigned)Inst.getOperand(3).getImm() < 8 && Operands.size() == 6) {
      Inst.setOpcode(ARM::tADDi3);
      return true;
    }
    break;
  case ARM::tSUBi8:
    // If the immediate is in the range 0-7, we want tADDi3 iff Rd was
    // explicitly specified. From the ARM ARM: "Encoding T1 is preferred
    // to encoding T2 if <Rd> is specified and encoding T2 is preferred
    // to encoding T1 if <Rd> is omitted."
    if ((unsigned)Inst.getOperand(3).getImm() < 8 && Operands.size() == 6) {
      Inst.setOpcode(ARM::tSUBi3);
      return true;
    }
    break;
  case ARM::t2ADDri:
  case ARM::t2SUBri: {
    // If the destination and first source operand are the same, and
    // the flags are compatible with the current IT status, use encoding T2
    // instead of T3. For compatibility with the system 'as'. Make sure the
    // wide encoding wasn't explicit.
    if (Inst.getOperand(0).getReg() != Inst.getOperand(1).getReg() ||
        !isARMLowRegister(Inst.getOperand(0).getReg()) ||
        (Inst.getOperand(2).isImm() &&
         (unsigned)Inst.getOperand(2).getImm() > 255) ||
        Inst.getOperand(5).getReg() != (inITBlock() ? 0 : ARM::CPSR) ||
        HasWideQualifier)
      break;
    MCInst TmpInst;
    TmpInst.setOpcode(Inst.getOpcode() == ARM::t2ADDri ?
                      ARM::tADDi8 : ARM::tSUBi8);
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(5));
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(2));
    TmpInst.addOperand(Inst.getOperand(3));
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }
  case ARM::t2ADDrr: {
    // If the destination and first source operand are the same, and
    // there's no setting of the flags, use encoding T2 instead of T3.
    // Note that this is only for ADD, not SUB. This mirrors the system
    // 'as' behaviour.  Also take advantage of ADD being commutative.
    // Make sure the wide encoding wasn't explicit.
    bool Swap = false;
    auto DestReg = Inst.getOperand(0).getReg();
    bool Transform = DestReg == Inst.getOperand(1).getReg();
    if (!Transform && DestReg == Inst.getOperand(2).getReg()) {
      Transform = true;
      Swap = true;
    }
    if (!Transform ||
        Inst.getOperand(5).getReg() != 0 ||
        HasWideQualifier)
      break;
    MCInst TmpInst;
    TmpInst.setOpcode(ARM::tADDhirr);
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(0));
    TmpInst.addOperand(Inst.getOperand(Swap ? 1 : 2));
    TmpInst.addOperand(Inst.getOperand(3));
    TmpInst.addOperand(Inst.getOperand(4));
    Inst = TmpInst;
    return true;
  }
  case ARM::tADDrSP:
    // If the non-SP source operand and the destination operand are not the
    // same, we need to use the 32-bit encoding if it's available.
    if (Inst.getOperand(0).getReg() != Inst.getOperand(2).getReg()) {
      Inst.setOpcode(ARM::t2ADDrr);
      Inst.addOperand(MCOperand::createReg(0)); // cc_out
      return true;
    }
    break;
  case ARM::tB:
    // A Thumb conditional branch outside of an IT block is a tBcc.
    if (Inst.getOperand(1).getImm() != ARMCC::AL && !inITBlock()) {
      Inst.setOpcode(ARM::tBcc);
      return true;
    }
    break;
  case ARM::t2B:
    // A Thumb2 conditional branch outside of an IT block is a t2Bcc.
    if (Inst.getOperand(1).getImm() != ARMCC::AL && !inITBlock()){
      Inst.setOpcode(ARM::t2Bcc);
      return true;
    }
    break;
  case ARM::t2Bcc:
    // If the conditional is AL or we're in an IT block, we really want t2B.
    if (Inst.getOperand(1).getImm() == ARMCC::AL || inITBlock()) {
      Inst.setOpcode(ARM::t2B);
      return true;
    }
    break;
  case ARM::tBcc:
    // If the conditional is AL, we really want tB.
    if (Inst.getOperand(1).getImm() == ARMCC::AL) {
      Inst.setOpcode(ARM::tB);
      return true;
    }
    break;
  case ARM::tLDMIA: {
    // If the register list contains any high registers, or if the writeback
    // doesn't match what tLDMIA can do, we need to use the 32-bit encoding
    // instead if we're in Thumb2. Otherwise, this should have generated
    // an error in validateInstruction().
    unsigned Rn = Inst.getOperand(0).getReg();
    bool hasWritebackToken =
        (static_cast<ARMOperand &>(*Operands[3]).isToken() &&
         static_cast<ARMOperand &>(*Operands[3]).getToken() == "!");
    bool listContainsBase;
    if (checkLowRegisterList(Inst, 3, Rn, 0, listContainsBase) ||
        (!listContainsBase && !hasWritebackToken) ||
        (listContainsBase && hasWritebackToken)) {
      // 16-bit encoding isn't sufficient. Switch to the 32-bit version.
      assert(isThumbTwo());
      Inst.setOpcode(hasWritebackToken ? ARM::t2LDMIA_UPD : ARM::t2LDMIA);
      // If we're switching to the updating version, we need to insert
      // the writeback tied operand.
      if (hasWritebackToken)
        Inst.insert(Inst.begin(),
                    MCOperand::createReg(Inst.getOperand(0).getReg()));
      return true;
    }
    break;
  }
  case ARM::tSTMIA_UPD: {
    // If the register list contains any high registers, we need to use
    // the 32-bit encoding instead if we're in Thumb2. Otherwise, this
    // should have generated an error in validateInstruction().
    unsigned Rn = Inst.getOperand(0).getReg();
    bool listContainsBase;
    if (checkLowRegisterList(Inst, 4, Rn, 0, listContainsBase)) {
      // 16-bit encoding isn't sufficient. Switch to the 32-bit version.
      assert(isThumbTwo());
      Inst.setOpcode(ARM::t2STMIA_UPD);
      return true;
    }
    break;
  }
  case ARM::tPOP: {
    bool listContainsBase;
    // If the register list contains any high registers, we need to use
    // the 32-bit encoding instead if we're in Thumb2. Otherwise, this
    // should have generated an error in validateInstruction().
    if (!checkLowRegisterList(Inst, 2, 0, ARM::PC, listContainsBase))
      return false;
    assert(isThumbTwo());
    Inst.setOpcode(ARM::t2LDMIA_UPD);
    // Add the base register and writeback operands.
    Inst.insert(Inst.begin(), MCOperand::createReg(ARM::SP));
    Inst.insert(Inst.begin(), MCOperand::createReg(ARM::SP));
    return true;
  }
  case ARM::tPUSH: {
    bool listContainsBase;
    if (!checkLowRegisterList(Inst, 2, 0, ARM::LR, listContainsBase))
      return false;
    assert(isThumbTwo());
    Inst.setOpcode(ARM::t2STMDB_UPD);
    // Add the base register and writeback operands.
    Inst.insert(Inst.begin(), MCOperand::createReg(ARM::SP));
    Inst.insert(Inst.begin(), MCOperand::createReg(ARM::SP));
    return true;
  }
  case ARM::t2MOVi:
    // If we can use the 16-bit encoding and the user didn't explicitly
    // request the 32-bit variant, transform it here.
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        (Inst.getOperand(1).isImm() &&
         (unsigned)Inst.getOperand(1).getImm() <= 255) &&
        Inst.getOperand(4).getReg() == (inITBlock() ? 0 : ARM::CPSR) &&
        !HasWideQualifier) {
      // The operands aren't in the same order for tMOVi8...
      MCInst TmpInst;
      TmpInst.setOpcode(ARM::tMOVi8);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(4));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(2));
      TmpInst.addOperand(Inst.getOperand(3));
      Inst = TmpInst;
      return true;
    }
    break;

  case ARM::t2MOVr:
    // If we can use the 16-bit encoding and the user didn't explicitly
    // request the 32-bit variant, transform it here.
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        Inst.getOperand(2).getImm() == ARMCC::AL &&
        Inst.getOperand(4).getReg() == ARM::CPSR &&
        !HasWideQualifier) {
      // The operands aren't the same for tMOV[S]r... (no cc_out)
      MCInst TmpInst;
      TmpInst.setOpcode(Inst.getOperand(4).getReg() ? ARM::tMOVSr : ARM::tMOVr);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(2));
      TmpInst.addOperand(Inst.getOperand(3));
      Inst = TmpInst;
      return true;
    }
    break;

  case ARM::t2SXTH:
  case ARM::t2SXTB:
  case ARM::t2UXTH:
  case ARM::t2UXTB:
    // If we can use the 16-bit encoding and the user didn't explicitly
    // request the 32-bit variant, transform it here.
    if (isARMLowRegister(Inst.getOperand(0).getReg()) &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        Inst.getOperand(2).getImm() == 0 &&
        !HasWideQualifier) {
      unsigned NewOpc;
      switch (Inst.getOpcode()) {
      default: llvm_unreachable("Illegal opcode!");
      case ARM::t2SXTH: NewOpc = ARM::tSXTH; break;
      case ARM::t2SXTB: NewOpc = ARM::tSXTB; break;
      case ARM::t2UXTH: NewOpc = ARM::tUXTH; break;
      case ARM::t2UXTB: NewOpc = ARM::tUXTB; break;
      }
      // The operands aren't the same for thumb1 (no rotate operand).
      MCInst TmpInst;
      TmpInst.setOpcode(NewOpc);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(3));
      TmpInst.addOperand(Inst.getOperand(4));
      Inst = TmpInst;
      return true;
    }
    break;

  case ARM::MOVsi: {
    ARM_AM::ShiftOpc SOpc = ARM_AM::getSORegShOp(Inst.getOperand(2).getImm());
    // rrx shifts and asr/lsr of #32 is encoded as 0
    if (SOpc == ARM_AM::rrx || SOpc == ARM_AM::asr || SOpc == ARM_AM::lsr)
      return false;
    if (ARM_AM::getSORegOffset(Inst.getOperand(2).getImm()) == 0) {
      // Shifting by zero is accepted as a vanilla 'MOVr'
      MCInst TmpInst;
      TmpInst.setOpcode(ARM::MOVr);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(3));
      TmpInst.addOperand(Inst.getOperand(4));
      TmpInst.addOperand(Inst.getOperand(5));
      Inst = TmpInst;
      return true;
    }
    return false;
  }
  case ARM::ANDrsi:
  case ARM::ORRrsi:
  case ARM::EORrsi:
  case ARM::BICrsi:
  case ARM::SUBrsi:
  case ARM::ADDrsi: {
    unsigned newOpc;
    ARM_AM::ShiftOpc SOpc = ARM_AM::getSORegShOp(Inst.getOperand(3).getImm());
    if (SOpc == ARM_AM::rrx) return false;
    switch (Inst.getOpcode()) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM::ANDrsi: newOpc = ARM::ANDrr; break;
    case ARM::ORRrsi: newOpc = ARM::ORRrr; break;
    case ARM::EORrsi: newOpc = ARM::EORrr; break;
    case ARM::BICrsi: newOpc = ARM::BICrr; break;
    case ARM::SUBrsi: newOpc = ARM::SUBrr; break;
    case ARM::ADDrsi: newOpc = ARM::ADDrr; break;
    }
    // If the shift is by zero, use the non-shifted instruction definition.
    // The exception is for right shifts, where 0 == 32
    if (ARM_AM::getSORegOffset(Inst.getOperand(3).getImm()) == 0 &&
        !(SOpc == ARM_AM::lsr || SOpc == ARM_AM::asr)) {
      MCInst TmpInst;
      TmpInst.setOpcode(newOpc);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(2));
      TmpInst.addOperand(Inst.getOperand(4));
      TmpInst.addOperand(Inst.getOperand(5));
      TmpInst.addOperand(Inst.getOperand(6));
      Inst = TmpInst;
      return true;
    }
    return false;
  }
  case ARM::ITasm:
  case ARM::t2IT: {
    MCOperand &MO = Inst.getOperand(1);
    unsigned Mask = MO.getImm();
    ARMCC::CondCodes Cond = ARMCC::CondCodes(Inst.getOperand(0).getImm());

    // Set up the IT block state according to the IT instruction we just
    // matched.
    assert(!inITBlock() && "nested IT blocks?!");
    startExplicitITBlock(Cond, Mask);
    MO.setImm(getITMaskEncoding());
    break;
  }
  case ARM::t2LSLrr:
  case ARM::t2LSRrr:
  case ARM::t2ASRrr:
  case ARM::t2SBCrr:
  case ARM::t2RORrr:
  case ARM::t2BICrr:
    // Assemblers should use the narrow encodings of these instructions when permissible.
    if ((isARMLowRegister(Inst.getOperand(1).getReg()) &&
         isARMLowRegister(Inst.getOperand(2).getReg())) &&
        Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg() &&
        Inst.getOperand(5).getReg() == (inITBlock() ? 0 : ARM::CPSR) &&
        !HasWideQualifier) {
      unsigned NewOpc;
      switch (Inst.getOpcode()) {
        default: llvm_unreachable("unexpected opcode");
        case ARM::t2LSLrr: NewOpc = ARM::tLSLrr; break;
        case ARM::t2LSRrr: NewOpc = ARM::tLSRrr; break;
        case ARM::t2ASRrr: NewOpc = ARM::tASRrr; break;
        case ARM::t2SBCrr: NewOpc = ARM::tSBC; break;
        case ARM::t2RORrr: NewOpc = ARM::tROR; break;
        case ARM::t2BICrr: NewOpc = ARM::tBIC; break;
      }
      MCInst TmpInst;
      TmpInst.setOpcode(NewOpc);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(5));
      TmpInst.addOperand(Inst.getOperand(1));
      TmpInst.addOperand(Inst.getOperand(2));
      TmpInst.addOperand(Inst.getOperand(3));
      TmpInst.addOperand(Inst.getOperand(4));
      Inst = TmpInst;
      return true;
    }
    return false;

  case ARM::t2ANDrr:
  case ARM::t2EORrr:
  case ARM::t2ADCrr:
  case ARM::t2ORRrr:
    // Assemblers should use the narrow encodings of these instructions when permissible.
    // These instructions are special in that they are commutable, so shorter encodings
    // are available more often.
    if ((isARMLowRegister(Inst.getOperand(1).getReg()) &&
         isARMLowRegister(Inst.getOperand(2).getReg())) &&
        (Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg() ||
         Inst.getOperand(0).getReg() == Inst.getOperand(2).getReg()) &&
        Inst.getOperand(5).getReg() == (inITBlock() ? 0 : ARM::CPSR) &&
        !HasWideQualifier) {
      unsigned NewOpc;
      switch (Inst.getOpcode()) {
        default: llvm_unreachable("unexpected opcode");
        case ARM::t2ADCrr: NewOpc = ARM::tADC; break;
        case ARM::t2ANDrr: NewOpc = ARM::tAND; break;
        case ARM::t2EORrr: NewOpc = ARM::tEOR; break;
        case ARM::t2ORRrr: NewOpc = ARM::tORR; break;
      }
      MCInst TmpInst;
      TmpInst.setOpcode(NewOpc);
      TmpInst.addOperand(Inst.getOperand(0));
      TmpInst.addOperand(Inst.getOperand(5));
      if (Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg()) {
        TmpInst.addOperand(Inst.getOperand(1));
        TmpInst.addOperand(Inst.getOperand(2));
      } else {
        TmpInst.addOperand(Inst.getOperand(2));
        TmpInst.addOperand(Inst.getOperand(1));
      }
      TmpInst.addOperand(Inst.getOperand(3));
      TmpInst.addOperand(Inst.getOperand(4));
      Inst = TmpInst;
      return true;
    }
    return false;
  }
  return false;
}

unsigned ARMAsmParser::checkTargetMatchPredicate(MCInst &Inst) {
  // 16-bit thumb arithmetic instructions either require or preclude the 'S'
  // suffix depending on whether they're in an IT block or not.
  unsigned Opc = Inst.getOpcode();
  const MCInstrDesc &MCID = MII.get(Opc);
  if (MCID.TSFlags & ARMII::ThumbArithFlagSetting) {
    assert(MCID.hasOptionalDef() &&
           "optionally flag setting instruction missing optional def operand");
    assert(MCID.NumOperands == Inst.getNumOperands() &&
           "operand count mismatch!");
    // Find the optional-def operand (cc_out).
    unsigned OpNo;
    for (OpNo = 0;
         !MCID.OpInfo[OpNo].isOptionalDef() && OpNo < MCID.NumOperands;
         ++OpNo)
      ;
    // If we're parsing Thumb1, reject it completely.
    if (isThumbOne() && Inst.getOperand(OpNo).getReg() != ARM::CPSR)
      return Match_RequiresFlagSetting;
    // If we're parsing Thumb2, which form is legal depends on whether we're
    // in an IT block.
    if (isThumbTwo() && Inst.getOperand(OpNo).getReg() != ARM::CPSR &&
        !inITBlock())
      return Match_RequiresITBlock;
    if (isThumbTwo() && Inst.getOperand(OpNo).getReg() == ARM::CPSR &&
        inITBlock())
      return Match_RequiresNotITBlock;
    // LSL with zero immediate is not allowed in an IT block
    if (Opc == ARM::tLSLri && Inst.getOperand(3).getImm() == 0 && inITBlock())
      return Match_RequiresNotITBlock;
  } else if (isThumbOne()) {
    // Some high-register supporting Thumb1 encodings only allow both registers
    // to be from r0-r7 when in Thumb2.
    if (Opc == ARM::tADDhirr && !hasV6MOps() &&
        isARMLowRegister(Inst.getOperand(1).getReg()) &&
        isARMLowRegister(Inst.getOperand(2).getReg()))
      return Match_RequiresThumb2;
    // Others only require ARMv6 or later.
    else if (Opc == ARM::tMOVr && !hasV6Ops() &&
             isARMLowRegister(Inst.getOperand(0).getReg()) &&
             isARMLowRegister(Inst.getOperand(1).getReg()))
      return Match_RequiresV6;
  }

  // Before ARMv8 the rules for when SP is allowed in t2MOVr are more complex
  // than the loop below can handle, so it uses the GPRnopc register class and
  // we do SP handling here.
  if (Opc == ARM::t2MOVr && !hasV8Ops())
  {
    // SP as both source and destination is not allowed
    if (Inst.getOperand(0).getReg() == ARM::SP &&
        Inst.getOperand(1).getReg() == ARM::SP)
      return Match_RequiresV8;
    // When flags-setting SP as either source or destination is not allowed
    if (Inst.getOperand(4).getReg() == ARM::CPSR &&
        (Inst.getOperand(0).getReg() == ARM::SP ||
         Inst.getOperand(1).getReg() == ARM::SP))
      return Match_RequiresV8;
  }

  // Use of SP for VMRS/VMSR is only allowed in ARM mode with the exception of
  // ARMv8-A.
  if ((Inst.getOpcode() == ARM::VMRS || Inst.getOpcode() == ARM::VMSR) &&
      Inst.getOperand(0).getReg() == ARM::SP && (isThumb() && !hasV8Ops()))
    return Match_InvalidOperand;

  for (unsigned I = 0; I < MCID.NumOperands; ++I)
    if (MCID.OpInfo[I].RegClass == ARM::rGPRRegClassID) {
      // rGPRRegClass excludes PC, and also excluded SP before ARMv8
      if ((Inst.getOperand(I).getReg() == ARM::SP) && !hasV8Ops())
        return Match_RequiresV8;
      else if (Inst.getOperand(I).getReg() == ARM::PC)
        return Match_InvalidOperand;
    }

  return Match_Success;
}

namespace llvm {

template <> inline bool IsCPSRDead<MCInst>(const MCInst *Instr) {
  return true; // In an assembly source, no need to second-guess
}

} // end namespace llvm

// Returns true if Inst is unpredictable if it is in and IT block, but is not
// the last instruction in the block.
bool ARMAsmParser::isITBlockTerminator(MCInst &Inst) const {
  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());

  // All branch & call instructions terminate IT blocks with the exception of
  // SVC.
  if (MCID.isTerminator() || (MCID.isCall() && Inst.getOpcode() != ARM::tSVC) ||
      MCID.isReturn() || MCID.isBranch() || MCID.isIndirectBranch())
    return true;

  // Any arithmetic instruction which writes to the PC also terminates the IT
  // block.
  if (MCID.hasDefOfPhysReg(Inst, ARM::PC, *MRI))
    return true;

  return false;
}

unsigned ARMAsmParser::MatchInstruction(OperandVector &Operands, MCInst &Inst,
                                          SmallVectorImpl<NearMissInfo> &NearMisses,
                                          bool MatchingInlineAsm,
                                          bool &EmitInITBlock,
                                          MCStreamer &Out) {
  // If we can't use an implicit IT block here, just match as normal.
  if (inExplicitITBlock() || !isThumbTwo() || !useImplicitITThumb())
    return MatchInstructionImpl(Operands, Inst, &NearMisses, MatchingInlineAsm);

  // Try to match the instruction in an extension of the current IT block (if
  // there is one).
  if (inImplicitITBlock()) {
    extendImplicitITBlock(ITState.Cond);
    if (MatchInstructionImpl(Operands, Inst, nullptr, MatchingInlineAsm) ==
            Match_Success) {
      // The match succeded, but we still have to check that the instruction is
      // valid in this implicit IT block.
      const MCInstrDesc &MCID = MII.get(Inst.getOpcode());
      if (MCID.isPredicable()) {
        ARMCC::CondCodes InstCond =
            (ARMCC::CondCodes)Inst.getOperand(MCID.findFirstPredOperandIdx())
                .getImm();
        ARMCC::CondCodes ITCond = currentITCond();
        if (InstCond == ITCond) {
          EmitInITBlock = true;
          return Match_Success;
        } else if (InstCond == ARMCC::getOppositeCondition(ITCond)) {
          invertCurrentITCondition();
          EmitInITBlock = true;
          return Match_Success;
        }
      }
    }
    rewindImplicitITPosition();
  }

  // Finish the current IT block, and try to match outside any IT block.
  flushPendingInstructions(Out);
  unsigned PlainMatchResult =
      MatchInstructionImpl(Operands, Inst, &NearMisses, MatchingInlineAsm);
  if (PlainMatchResult == Match_Success) {
    const MCInstrDesc &MCID = MII.get(Inst.getOpcode());
    if (MCID.isPredicable()) {
      ARMCC::CondCodes InstCond =
          (ARMCC::CondCodes)Inst.getOperand(MCID.findFirstPredOperandIdx())
              .getImm();
      // Some forms of the branch instruction have their own condition code
      // fields, so can be conditionally executed without an IT block.
      if (Inst.getOpcode() == ARM::tBcc || Inst.getOpcode() == ARM::t2Bcc) {
        EmitInITBlock = false;
        return Match_Success;
      }
      if (InstCond == ARMCC::AL) {
        EmitInITBlock = false;
        return Match_Success;
      }
    } else {
      EmitInITBlock = false;
      return Match_Success;
    }
  }

  // Try to match in a new IT block. The matcher doesn't check the actual
  // condition, so we create an IT block with a dummy condition, and fix it up
  // once we know the actual condition.
  startImplicitITBlock();
  if (MatchInstructionImpl(Operands, Inst, nullptr, MatchingInlineAsm) ==
      Match_Success) {
    const MCInstrDesc &MCID = MII.get(Inst.getOpcode());
    if (MCID.isPredicable()) {
      ITState.Cond =
          (ARMCC::CondCodes)Inst.getOperand(MCID.findFirstPredOperandIdx())
              .getImm();
      EmitInITBlock = true;
      return Match_Success;
    }
  }
  discardImplicitITBlock();

  // If none of these succeed, return the error we got when trying to match
  // outside any IT blocks.
  EmitInITBlock = false;
  return PlainMatchResult;
}

static std::string ARMMnemonicSpellCheck(StringRef S, uint64_t FBS,
                                         unsigned VariantID = 0);

static const char *getSubtargetFeatureName(uint64_t Val);
bool ARMAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out, uint64_t &ErrorInfo,
                                           bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult;
  bool PendConditionalInstruction = false;

  SmallVector<NearMissInfo, 4> NearMisses;
  MatchResult = MatchInstruction(Operands, Inst, NearMisses, MatchingInlineAsm,
                                 PendConditionalInstruction, Out);

  switch (MatchResult) {
  case Match_Success:
    LLVM_DEBUG(dbgs() << "Parsed as: ";
               Inst.dump_pretty(dbgs(), MII.getName(Inst.getOpcode()));
               dbgs() << "\n");

    // Context sensitive operand constraints aren't handled by the matcher,
    // so check them here.
    if (validateInstruction(Inst, Operands)) {
      // Still progress the IT block, otherwise one wrong condition causes
      // nasty cascading errors.
      forwardITPosition();
      return true;
    }

    { // processInstruction() updates inITBlock state, we need to save it away
      bool wasInITBlock = inITBlock();

      // Some instructions need post-processing to, for example, tweak which
      // encoding is selected. Loop on it while changes happen so the
      // individual transformations can chain off each other. E.g.,
      // tPOP(r8)->t2LDMIA_UPD(sp,r8)->t2STR_POST(sp,r8)
      while (processInstruction(Inst, Operands, Out))
        LLVM_DEBUG(dbgs() << "Changed to: ";
                   Inst.dump_pretty(dbgs(), MII.getName(Inst.getOpcode()));
                   dbgs() << "\n");

      // Only after the instruction is fully processed, we can validate it
      if (wasInITBlock && hasV8Ops() && isThumb() &&
          !isV8EligibleForIT(&Inst)) {
        Warning(IDLoc, "deprecated instruction in IT block");
      }
    }

    // Only move forward at the very end so that everything in validate
    // and process gets a consistent answer about whether we're in an IT
    // block.
    forwardITPosition();

    // ITasm is an ARM mode pseudo-instruction that just sets the ITblock and
    // doesn't actually encode.
    if (Inst.getOpcode() == ARM::ITasm)
      return false;

    Inst.setLoc(IDLoc);
    if (PendConditionalInstruction) {
      PendingConditionalInsts.push_back(Inst);
      if (isITBlockFull() || isITBlockTerminator(Inst))
        flushPendingInstructions(Out);
    } else {
      Out.EmitInstruction(Inst, getSTI());
    }
    return false;
  case Match_NearMisses:
    ReportNearMisses(NearMisses, IDLoc, Operands);
    return true;
  case Match_MnemonicFail: {
    uint64_t FBS = ComputeAvailableFeatures(getSTI().getFeatureBits());
    std::string Suggestion = ARMMnemonicSpellCheck(
      ((ARMOperand &)*Operands[0]).getToken(), FBS);
    return Error(IDLoc, "invalid instruction" + Suggestion,
                 ((ARMOperand &)*Operands[0]).getLocRange());
  }
  }

  llvm_unreachable("Implement any new match types added!");
}

/// parseDirective parses the arm specific directives
bool ARMAsmParser::ParseDirective(AsmToken DirectiveID) {
  const MCObjectFileInfo::Environment Format =
    getContext().getObjectFileInfo()->getObjectFileType();
  bool IsMachO = Format == MCObjectFileInfo::IsMachO;
  bool IsCOFF = Format == MCObjectFileInfo::IsCOFF;

  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal == ".word")
    parseLiteralValues(4, DirectiveID.getLoc());
  else if (IDVal == ".short" || IDVal == ".hword")
    parseLiteralValues(2, DirectiveID.getLoc());
  else if (IDVal == ".thumb")
    parseDirectiveThumb(DirectiveID.getLoc());
  else if (IDVal == ".arm")
    parseDirectiveARM(DirectiveID.getLoc());
  else if (IDVal == ".thumb_func")
    parseDirectiveThumbFunc(DirectiveID.getLoc());
  else if (IDVal == ".code")
    parseDirectiveCode(DirectiveID.getLoc());
  else if (IDVal == ".syntax")
    parseDirectiveSyntax(DirectiveID.getLoc());
  else if (IDVal == ".unreq")
    parseDirectiveUnreq(DirectiveID.getLoc());
  else if (IDVal == ".fnend")
    parseDirectiveFnEnd(DirectiveID.getLoc());
  else if (IDVal == ".cantunwind")
    parseDirectiveCantUnwind(DirectiveID.getLoc());
  else if (IDVal == ".personality")
    parseDirectivePersonality(DirectiveID.getLoc());
  else if (IDVal == ".handlerdata")
    parseDirectiveHandlerData(DirectiveID.getLoc());
  else if (IDVal == ".setfp")
    parseDirectiveSetFP(DirectiveID.getLoc());
  else if (IDVal == ".pad")
    parseDirectivePad(DirectiveID.getLoc());
  else if (IDVal == ".save")
    parseDirectiveRegSave(DirectiveID.getLoc(), false);
  else if (IDVal == ".vsave")
    parseDirectiveRegSave(DirectiveID.getLoc(), true);
  else if (IDVal == ".ltorg" || IDVal == ".pool")
    parseDirectiveLtorg(DirectiveID.getLoc());
  else if (IDVal == ".even")
    parseDirectiveEven(DirectiveID.getLoc());
  else if (IDVal == ".personalityindex")
    parseDirectivePersonalityIndex(DirectiveID.getLoc());
  else if (IDVal == ".unwind_raw")
    parseDirectiveUnwindRaw(DirectiveID.getLoc());
  else if (IDVal == ".movsp")
    parseDirectiveMovSP(DirectiveID.getLoc());
  else if (IDVal == ".arch_extension")
    parseDirectiveArchExtension(DirectiveID.getLoc());
  else if (IDVal == ".align")
    return parseDirectiveAlign(DirectiveID.getLoc()); // Use Generic on failure.
  else if (IDVal == ".thumb_set")
    parseDirectiveThumbSet(DirectiveID.getLoc());
  else if (IDVal == ".inst")
    parseDirectiveInst(DirectiveID.getLoc());
  else if (IDVal == ".inst.n")
    parseDirectiveInst(DirectiveID.getLoc(), 'n');
  else if (IDVal == ".inst.w")
    parseDirectiveInst(DirectiveID.getLoc(), 'w');
  else if (!IsMachO && !IsCOFF) {
    if (IDVal == ".arch")
      parseDirectiveArch(DirectiveID.getLoc());
    else if (IDVal == ".cpu")
      parseDirectiveCPU(DirectiveID.getLoc());
    else if (IDVal == ".eabi_attribute")
      parseDirectiveEabiAttr(DirectiveID.getLoc());
    else if (IDVal == ".fpu")
      parseDirectiveFPU(DirectiveID.getLoc());
    else if (IDVal == ".fnstart")
      parseDirectiveFnStart(DirectiveID.getLoc());
    else if (IDVal == ".object_arch")
      parseDirectiveObjectArch(DirectiveID.getLoc());
    else if (IDVal == ".tlsdescseq")
      parseDirectiveTLSDescSeq(DirectiveID.getLoc());
    else
      return true;
  } else
    return true;
  return false;
}

/// parseLiteralValues
///  ::= .hword expression [, expression]*
///  ::= .short expression [, expression]*
///  ::= .word expression [, expression]*
bool ARMAsmParser::parseLiteralValues(unsigned Size, SMLoc L) {
  auto parseOne = [&]() -> bool {
    const MCExpr *Value;
    if (getParser().parseExpression(Value))
      return true;
    getParser().getStreamer().EmitValue(Value, Size, L);
    return false;
  };
  return (parseMany(parseOne));
}

/// parseDirectiveThumb
///  ::= .thumb
bool ARMAsmParser::parseDirectiveThumb(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive") ||
      check(!hasThumb(), L, "target does not support Thumb mode"))
    return true;

  if (!isThumb())
    SwitchMode();

  getParser().getStreamer().EmitAssemblerFlag(MCAF_Code16);
  return false;
}

/// parseDirectiveARM
///  ::= .arm
bool ARMAsmParser::parseDirectiveARM(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive") ||
      check(!hasARM(), L, "target does not support ARM mode"))
    return true;

  if (isThumb())
    SwitchMode();
  getParser().getStreamer().EmitAssemblerFlag(MCAF_Code32);
  return false;
}

void ARMAsmParser::doBeforeLabelEmit(MCSymbol *Symbol) {
  // We need to flush the current implicit IT block on a label, because it is
  // not legal to branch into an IT block.
  flushPendingInstructions(getStreamer());
}

void ARMAsmParser::onLabelParsed(MCSymbol *Symbol) {
  if (NextSymbolIsThumb) {
    getParser().getStreamer().EmitThumbFunc(Symbol);
    NextSymbolIsThumb = false;
  }
}

/// parseDirectiveThumbFunc
///  ::= .thumbfunc symbol_name
bool ARMAsmParser::parseDirectiveThumbFunc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  const auto Format = getContext().getObjectFileInfo()->getObjectFileType();
  bool IsMachO = Format == MCObjectFileInfo::IsMachO;

  // Darwin asm has (optionally) function name after .thumb_func direction
  // ELF doesn't

  if (IsMachO) {
    if (Parser.getTok().is(AsmToken::Identifier) ||
        Parser.getTok().is(AsmToken::String)) {
      MCSymbol *Func = getParser().getContext().getOrCreateSymbol(
          Parser.getTok().getIdentifier());
      getParser().getStreamer().EmitThumbFunc(Func);
      Parser.Lex();
      if (parseToken(AsmToken::EndOfStatement,
                     "unexpected token in '.thumb_func' directive"))
        return true;
      return false;
    }
  }

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.thumb_func' directive"))
    return true;

  NextSymbolIsThumb = true;
  return false;
}

/// parseDirectiveSyntax
///  ::= .syntax unified | divided
bool ARMAsmParser::parseDirectiveSyntax(SMLoc L) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) {
    Error(L, "unexpected token in .syntax directive");
    return false;
  }

  StringRef Mode = Tok.getString();
  Parser.Lex();
  if (check(Mode == "divided" || Mode == "DIVIDED", L,
            "'.syntax divided' arm assembly not supported") ||
      check(Mode != "unified" && Mode != "UNIFIED", L,
            "unrecognized syntax mode in .syntax directive") ||
      parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;

  // TODO tell the MC streamer the mode
  // getParser().getStreamer().Emit???();
  return false;
}

/// parseDirectiveCode
///  ::= .code 16 | 32
bool ARMAsmParser::parseDirectiveCode(SMLoc L) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Integer))
    return Error(L, "unexpected token in .code directive");
  int64_t Val = Parser.getTok().getIntVal();
  if (Val != 16 && Val != 32) {
    Error(L, "invalid operand to .code directive");
    return false;
  }
  Parser.Lex();

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;

  if (Val == 16) {
    if (!hasThumb())
      return Error(L, "target does not support Thumb mode");

    if (!isThumb())
      SwitchMode();
    getParser().getStreamer().EmitAssemblerFlag(MCAF_Code16);
  } else {
    if (!hasARM())
      return Error(L, "target does not support ARM mode");

    if (isThumb())
      SwitchMode();
    getParser().getStreamer().EmitAssemblerFlag(MCAF_Code32);
  }

  return false;
}

/// parseDirectiveReq
///  ::= name .req registername
bool ARMAsmParser::parseDirectiveReq(StringRef Name, SMLoc L) {
  MCAsmParser &Parser = getParser();
  Parser.Lex(); // Eat the '.req' token.
  unsigned Reg;
  SMLoc SRegLoc, ERegLoc;
  if (check(ParseRegister(Reg, SRegLoc, ERegLoc), SRegLoc,
            "register name expected") ||
      parseToken(AsmToken::EndOfStatement,
                 "unexpected input in .req directive."))
    return true;

  if (RegisterReqs.insert(std::make_pair(Name, Reg)).first->second != Reg)
    return Error(SRegLoc,
                 "redefinition of '" + Name + "' does not match original.");

  return false;
}

/// parseDirectiveUneq
///  ::= .unreq registername
bool ARMAsmParser::parseDirectiveUnreq(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier))
    return Error(L, "unexpected input in .unreq directive.");
  RegisterReqs.erase(Parser.getTok().getIdentifier().lower());
  Parser.Lex(); // Eat the identifier.
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected input in '.unreq' directive"))
    return true;
  return false;
}

// After changing arch/CPU, try to put the ARM/Thumb mode back to what it was
// before, if supported by the new target, or emit mapping symbols for the mode
// switch.
void ARMAsmParser::FixModeAfterArchChange(bool WasThumb, SMLoc Loc) {
  if (WasThumb != isThumb()) {
    if (WasThumb && hasThumb()) {
      // Stay in Thumb mode
      SwitchMode();
    } else if (!WasThumb && hasARM()) {
      // Stay in ARM mode
      SwitchMode();
    } else {
      // Mode switch forced, because the new arch doesn't support the old mode.
      getParser().getStreamer().EmitAssemblerFlag(isThumb() ? MCAF_Code16
                                                            : MCAF_Code32);
      // Warn about the implcit mode switch. GAS does not switch modes here,
      // but instead stays in the old mode, reporting an error on any following
      // instructions as the mode does not exist on the target.
      Warning(Loc, Twine("new target does not support ") +
                       (WasThumb ? "thumb" : "arm") + " mode, switching to " +
                       (!WasThumb ? "thumb" : "arm") + " mode");
    }
  }
}

/// parseDirectiveArch
///  ::= .arch token
bool ARMAsmParser::parseDirectiveArch(SMLoc L) {
  StringRef Arch = getParser().parseStringToEndOfStatement().trim();
  ARM::ArchKind ID = ARM::parseArch(Arch);

  if (ID == ARM::ArchKind::INVALID)
    return Error(L, "Unknown arch name");

  bool WasThumb = isThumb();
  Triple T;
  MCSubtargetInfo &STI = copySTI();
  STI.setDefaultFeatures("", ("+" + ARM::getArchName(ID)).str());
  setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  FixModeAfterArchChange(WasThumb, L);

  getTargetStreamer().emitArch(ID);
  return false;
}

/// parseDirectiveEabiAttr
///  ::= .eabi_attribute int, int [, "str"]
///  ::= .eabi_attribute Tag_name, int [, "str"]
bool ARMAsmParser::parseDirectiveEabiAttr(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t Tag;
  SMLoc TagLoc;
  TagLoc = Parser.getTok().getLoc();
  if (Parser.getTok().is(AsmToken::Identifier)) {
    StringRef Name = Parser.getTok().getIdentifier();
    Tag = ARMBuildAttrs::AttrTypeFromString(Name);
    if (Tag == -1) {
      Error(TagLoc, "attribute name not recognised: " + Name);
      return false;
    }
    Parser.Lex();
  } else {
    const MCExpr *AttrExpr;

    TagLoc = Parser.getTok().getLoc();
    if (Parser.parseExpression(AttrExpr))
      return true;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(AttrExpr);
    if (check(!CE, TagLoc, "expected numeric constant"))
      return true;

    Tag = CE->getValue();
  }

  if (Parser.parseToken(AsmToken::Comma, "comma expected"))
    return true;

  StringRef StringValue = "";
  bool IsStringValue = false;

  int64_t IntegerValue = 0;
  bool IsIntegerValue = false;

  if (Tag == ARMBuildAttrs::CPU_raw_name || Tag == ARMBuildAttrs::CPU_name)
    IsStringValue = true;
  else if (Tag == ARMBuildAttrs::compatibility) {
    IsStringValue = true;
    IsIntegerValue = true;
  } else if (Tag < 32 || Tag % 2 == 0)
    IsIntegerValue = true;
  else if (Tag % 2 == 1)
    IsStringValue = true;
  else
    llvm_unreachable("invalid tag type");

  if (IsIntegerValue) {
    const MCExpr *ValueExpr;
    SMLoc ValueExprLoc = Parser.getTok().getLoc();
    if (Parser.parseExpression(ValueExpr))
      return true;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(ValueExpr);
    if (!CE)
      return Error(ValueExprLoc, "expected numeric constant");
    IntegerValue = CE->getValue();
  }

  if (Tag == ARMBuildAttrs::compatibility) {
    if (Parser.parseToken(AsmToken::Comma, "comma expected"))
      return true;
  }

  if (IsStringValue) {
    if (Parser.getTok().isNot(AsmToken::String))
      return Error(Parser.getTok().getLoc(), "bad string constant");

    StringValue = Parser.getTok().getStringContents();
    Parser.Lex();
  }

  if (Parser.parseToken(AsmToken::EndOfStatement,
                        "unexpected token in '.eabi_attribute' directive"))
    return true;

  if (IsIntegerValue && IsStringValue) {
    assert(Tag == ARMBuildAttrs::compatibility);
    getTargetStreamer().emitIntTextAttribute(Tag, IntegerValue, StringValue);
  } else if (IsIntegerValue)
    getTargetStreamer().emitAttribute(Tag, IntegerValue);
  else if (IsStringValue)
    getTargetStreamer().emitTextAttribute(Tag, StringValue);
  return false;
}

/// parseDirectiveCPU
///  ::= .cpu str
bool ARMAsmParser::parseDirectiveCPU(SMLoc L) {
  StringRef CPU = getParser().parseStringToEndOfStatement().trim();
  getTargetStreamer().emitTextAttribute(ARMBuildAttrs::CPU_name, CPU);

  // FIXME: This is using table-gen data, but should be moved to
  // ARMTargetParser once that is table-gen'd.
  if (!getSTI().isCPUStringValid(CPU))
    return Error(L, "Unknown CPU name");

  bool WasThumb = isThumb();
  MCSubtargetInfo &STI = copySTI();
  STI.setDefaultFeatures(CPU, "");
  setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  FixModeAfterArchChange(WasThumb, L);

  return false;
}

/// parseDirectiveFPU
///  ::= .fpu str
bool ARMAsmParser::parseDirectiveFPU(SMLoc L) {
  SMLoc FPUNameLoc = getTok().getLoc();
  StringRef FPU = getParser().parseStringToEndOfStatement().trim();

  unsigned ID = ARM::parseFPU(FPU);
  std::vector<StringRef> Features;
  if (!ARM::getFPUFeatures(ID, Features))
    return Error(FPUNameLoc, "Unknown FPU name");

  MCSubtargetInfo &STI = copySTI();
  for (auto Feature : Features)
    STI.ApplyFeatureFlag(Feature);
  setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));

  getTargetStreamer().emitFPU(ID);
  return false;
}

/// parseDirectiveFnStart
///  ::= .fnstart
bool ARMAsmParser::parseDirectiveFnStart(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.fnstart' directive"))
    return true;

  if (UC.hasFnStart()) {
    Error(L, ".fnstart starts before the end of previous one");
    UC.emitFnStartLocNotes();
    return true;
  }

  // Reset the unwind directives parser state
  UC.reset();

  getTargetStreamer().emitFnStart();

  UC.recordFnStart(L);
  return false;
}

/// parseDirectiveFnEnd
///  ::= .fnend
bool ARMAsmParser::parseDirectiveFnEnd(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.fnend' directive"))
    return true;
  // Check the ordering of unwind directives
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .fnend directive");

  // Reset the unwind directives parser state
  getTargetStreamer().emitFnEnd();

  UC.reset();
  return false;
}

/// parseDirectiveCantUnwind
///  ::= .cantunwind
bool ARMAsmParser::parseDirectiveCantUnwind(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.cantunwind' directive"))
    return true;

  UC.recordCantUnwind(L);
  // Check the ordering of unwind directives
  if (check(!UC.hasFnStart(), L, ".fnstart must precede .cantunwind directive"))
    return true;

  if (UC.hasHandlerData()) {
    Error(L, ".cantunwind can't be used with .handlerdata directive");
    UC.emitHandlerDataLocNotes();
    return true;
  }
  if (UC.hasPersonality()) {
    Error(L, ".cantunwind can't be used with .personality directive");
    UC.emitPersonalityLocNotes();
    return true;
  }

  getTargetStreamer().emitCantUnwind();
  return false;
}

/// parseDirectivePersonality
///  ::= .personality name
bool ARMAsmParser::parseDirectivePersonality(SMLoc L) {
  MCAsmParser &Parser = getParser();
  bool HasExistingPersonality = UC.hasPersonality();

  // Parse the name of the personality routine
  if (Parser.getTok().isNot(AsmToken::Identifier))
    return Error(L, "unexpected input in .personality directive.");
  StringRef Name(Parser.getTok().getIdentifier());
  Parser.Lex();

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.personality' directive"))
    return true;

  UC.recordPersonality(L);

  // Check the ordering of unwind directives
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .personality directive");
  if (UC.cantUnwind()) {
    Error(L, ".personality can't be used with .cantunwind directive");
    UC.emitCantUnwindLocNotes();
    return true;
  }
  if (UC.hasHandlerData()) {
    Error(L, ".personality must precede .handlerdata directive");
    UC.emitHandlerDataLocNotes();
    return true;
  }
  if (HasExistingPersonality) {
    Error(L, "multiple personality directives");
    UC.emitPersonalityLocNotes();
    return true;
  }

  MCSymbol *PR = getParser().getContext().getOrCreateSymbol(Name);
  getTargetStreamer().emitPersonality(PR);
  return false;
}

/// parseDirectiveHandlerData
///  ::= .handlerdata
bool ARMAsmParser::parseDirectiveHandlerData(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.handlerdata' directive"))
    return true;

  UC.recordHandlerData(L);
  // Check the ordering of unwind directives
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .personality directive");
  if (UC.cantUnwind()) {
    Error(L, ".handlerdata can't be used with .cantunwind directive");
    UC.emitCantUnwindLocNotes();
    return true;
  }

  getTargetStreamer().emitHandlerData();
  return false;
}

/// parseDirectiveSetFP
///  ::= .setfp fpreg, spreg [, offset]
bool ARMAsmParser::parseDirectiveSetFP(SMLoc L) {
  MCAsmParser &Parser = getParser();
  // Check the ordering of unwind directives
  if (check(!UC.hasFnStart(), L, ".fnstart must precede .setfp directive") ||
      check(UC.hasHandlerData(), L,
            ".setfp must precede .handlerdata directive"))
    return true;

  // Parse fpreg
  SMLoc FPRegLoc = Parser.getTok().getLoc();
  int FPReg = tryParseRegister();

  if (check(FPReg == -1, FPRegLoc, "frame pointer register expected") ||
      Parser.parseToken(AsmToken::Comma, "comma expected"))
    return true;

  // Parse spreg
  SMLoc SPRegLoc = Parser.getTok().getLoc();
  int SPReg = tryParseRegister();
  if (check(SPReg == -1, SPRegLoc, "stack pointer register expected") ||
      check(SPReg != ARM::SP && SPReg != UC.getFPReg(), SPRegLoc,
            "register should be either $sp or the latest fp register"))
    return true;

  // Update the frame pointer register
  UC.saveFPReg(FPReg);

  // Parse offset
  int64_t Offset = 0;
  if (Parser.parseOptionalToken(AsmToken::Comma)) {
    if (Parser.getTok().isNot(AsmToken::Hash) &&
        Parser.getTok().isNot(AsmToken::Dollar))
      return Error(Parser.getTok().getLoc(), "'#' expected");
    Parser.Lex(); // skip hash token.

    const MCExpr *OffsetExpr;
    SMLoc ExLoc = Parser.getTok().getLoc();
    SMLoc EndLoc;
    if (getParser().parseExpression(OffsetExpr, EndLoc))
      return Error(ExLoc, "malformed setfp offset");
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(OffsetExpr);
    if (check(!CE, ExLoc, "setfp offset must be an immediate"))
      return true;
    Offset = CE->getValue();
  }

  if (Parser.parseToken(AsmToken::EndOfStatement))
    return true;

  getTargetStreamer().emitSetFP(static_cast<unsigned>(FPReg),
                                static_cast<unsigned>(SPReg), Offset);
  return false;
}

/// parseDirective
///  ::= .pad offset
bool ARMAsmParser::parseDirectivePad(SMLoc L) {
  MCAsmParser &Parser = getParser();
  // Check the ordering of unwind directives
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .pad directive");
  if (UC.hasHandlerData())
    return Error(L, ".pad must precede .handlerdata directive");

  // Parse the offset
  if (Parser.getTok().isNot(AsmToken::Hash) &&
      Parser.getTok().isNot(AsmToken::Dollar))
    return Error(Parser.getTok().getLoc(), "'#' expected");
  Parser.Lex(); // skip hash token.

  const MCExpr *OffsetExpr;
  SMLoc ExLoc = Parser.getTok().getLoc();
  SMLoc EndLoc;
  if (getParser().parseExpression(OffsetExpr, EndLoc))
    return Error(ExLoc, "malformed pad offset");
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(OffsetExpr);
  if (!CE)
    return Error(ExLoc, "pad offset must be an immediate");

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.pad' directive"))
    return true;

  getTargetStreamer().emitPad(CE->getValue());
  return false;
}

/// parseDirectiveRegSave
///  ::= .save  { registers }
///  ::= .vsave { registers }
bool ARMAsmParser::parseDirectiveRegSave(SMLoc L, bool IsVector) {
  // Check the ordering of unwind directives
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .save or .vsave directives");
  if (UC.hasHandlerData())
    return Error(L, ".save or .vsave must precede .handlerdata directive");

  // RAII object to make sure parsed operands are deleted.
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 1> Operands;

  // Parse the register list
  if (parseRegisterList(Operands) ||
      parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;
  ARMOperand &Op = (ARMOperand &)*Operands[0];
  if (!IsVector && !Op.isRegList())
    return Error(L, ".save expects GPR registers");
  if (IsVector && !Op.isDPRRegList())
    return Error(L, ".vsave expects DPR registers");

  getTargetStreamer().emitRegSave(Op.getRegList(), IsVector);
  return false;
}

/// parseDirectiveInst
///  ::= .inst opcode [, ...]
///  ::= .inst.n opcode [, ...]
///  ::= .inst.w opcode [, ...]
bool ARMAsmParser::parseDirectiveInst(SMLoc Loc, char Suffix) {
  int Width = 4;

  if (isThumb()) {
    switch (Suffix) {
    case 'n':
      Width = 2;
      break;
    case 'w':
      break;
    default:
      Width = 0;
      break;
    }
  } else {
    if (Suffix)
      return Error(Loc, "width suffixes are invalid in ARM mode");
  }

  auto parseOne = [&]() -> bool {
    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    const MCConstantExpr *Value = dyn_cast_or_null<MCConstantExpr>(Expr);
    if (!Value) {
      return Error(Loc, "expected constant expression");
    }

    char CurSuffix = Suffix;
    switch (Width) {
    case 2:
      if (Value->getValue() > 0xffff)
        return Error(Loc, "inst.n operand is too big, use inst.w instead");
      break;
    case 4:
      if (Value->getValue() > 0xffffffff)
        return Error(Loc, StringRef(Suffix ? "inst.w" : "inst") +
                              " operand is too big");
      break;
    case 0:
      // Thumb mode, no width indicated. Guess from the opcode, if possible.
      if (Value->getValue() < 0xe800)
        CurSuffix = 'n';
      else if (Value->getValue() >= 0xe8000000)
        CurSuffix = 'w';
      else
        return Error(Loc, "cannot determine Thumb instruction size, "
                          "use inst.n/inst.w instead");
      break;
    default:
      llvm_unreachable("only supported widths are 2 and 4");
    }

    getTargetStreamer().emitInst(Value->getValue(), CurSuffix);
    return false;
  };

  if (parseOptionalToken(AsmToken::EndOfStatement))
    return Error(Loc, "expected expression following directive");
  if (parseMany(parseOne))
    return true;
  return false;
}

/// parseDirectiveLtorg
///  ::= .ltorg | .pool
bool ARMAsmParser::parseDirectiveLtorg(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;
  getTargetStreamer().emitCurrentConstantPool();
  return false;
}

bool ARMAsmParser::parseDirectiveEven(SMLoc L) {
  const MCSection *Section = getStreamer().getCurrentSectionOnly();

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;

  if (!Section) {
    getStreamer().InitSections(false);
    Section = getStreamer().getCurrentSectionOnly();
  }

  assert(Section && "must have section to emit alignment");
  if (Section->UseCodeAlign())
    getStreamer().EmitCodeAlignment(2);
  else
    getStreamer().EmitValueToAlignment(2);

  return false;
}

/// parseDirectivePersonalityIndex
///   ::= .personalityindex index
bool ARMAsmParser::parseDirectivePersonalityIndex(SMLoc L) {
  MCAsmParser &Parser = getParser();
  bool HasExistingPersonality = UC.hasPersonality();

  const MCExpr *IndexExpression;
  SMLoc IndexLoc = Parser.getTok().getLoc();
  if (Parser.parseExpression(IndexExpression) ||
      parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.personalityindex' directive")) {
    return true;
  }

  UC.recordPersonalityIndex(L);

  if (!UC.hasFnStart()) {
    return Error(L, ".fnstart must precede .personalityindex directive");
  }
  if (UC.cantUnwind()) {
    Error(L, ".personalityindex cannot be used with .cantunwind");
    UC.emitCantUnwindLocNotes();
    return true;
  }
  if (UC.hasHandlerData()) {
    Error(L, ".personalityindex must precede .handlerdata directive");
    UC.emitHandlerDataLocNotes();
    return true;
  }
  if (HasExistingPersonality) {
    Error(L, "multiple personality directives");
    UC.emitPersonalityLocNotes();
    return true;
  }

  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(IndexExpression);
  if (!CE)
    return Error(IndexLoc, "index must be a constant number");
  if (CE->getValue() < 0 || CE->getValue() >= ARM::EHABI::NUM_PERSONALITY_INDEX)
    return Error(IndexLoc,
                 "personality routine index should be in range [0-3]");

  getTargetStreamer().emitPersonalityIndex(CE->getValue());
  return false;
}

/// parseDirectiveUnwindRaw
///   ::= .unwind_raw offset, opcode [, opcode...]
bool ARMAsmParser::parseDirectiveUnwindRaw(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t StackOffset;
  const MCExpr *OffsetExpr;
  SMLoc OffsetLoc = getLexer().getLoc();

  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .unwind_raw directives");
  if (getParser().parseExpression(OffsetExpr))
    return Error(OffsetLoc, "expected expression");

  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(OffsetExpr);
  if (!CE)
    return Error(OffsetLoc, "offset must be a constant");

  StackOffset = CE->getValue();

  if (Parser.parseToken(AsmToken::Comma, "expected comma"))
    return true;

  SmallVector<uint8_t, 16> Opcodes;

  auto parseOne = [&]() -> bool {
    const MCExpr *OE;
    SMLoc OpcodeLoc = getLexer().getLoc();
    if (check(getLexer().is(AsmToken::EndOfStatement) ||
                  Parser.parseExpression(OE),
              OpcodeLoc, "expected opcode expression"))
      return true;
    const MCConstantExpr *OC = dyn_cast<MCConstantExpr>(OE);
    if (!OC)
      return Error(OpcodeLoc, "opcode value must be a constant");
    const int64_t Opcode = OC->getValue();
    if (Opcode & ~0xff)
      return Error(OpcodeLoc, "invalid opcode");
    Opcodes.push_back(uint8_t(Opcode));
    return false;
  };

  // Must have at least 1 element
  SMLoc OpcodeLoc = getLexer().getLoc();
  if (parseOptionalToken(AsmToken::EndOfStatement))
    return Error(OpcodeLoc, "expected opcode expression");
  if (parseMany(parseOne))
    return true;

  getTargetStreamer().emitUnwindRaw(StackOffset, Opcodes);
  return false;
}

/// parseDirectiveTLSDescSeq
///   ::= .tlsdescseq tls-variable
bool ARMAsmParser::parseDirectiveTLSDescSeq(SMLoc L) {
  MCAsmParser &Parser = getParser();

  if (getLexer().isNot(AsmToken::Identifier))
    return TokError("expected variable after '.tlsdescseq' directive");

  const MCSymbolRefExpr *SRE =
    MCSymbolRefExpr::create(Parser.getTok().getIdentifier(),
                            MCSymbolRefExpr::VK_ARM_TLSDESCSEQ, getContext());
  Lex();

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.tlsdescseq' directive"))
    return true;

  getTargetStreamer().AnnotateTLSDescriptorSequence(SRE);
  return false;
}

/// parseDirectiveMovSP
///  ::= .movsp reg [, #offset]
bool ARMAsmParser::parseDirectiveMovSP(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (!UC.hasFnStart())
    return Error(L, ".fnstart must precede .movsp directives");
  if (UC.getFPReg() != ARM::SP)
    return Error(L, "unexpected .movsp directive");

  SMLoc SPRegLoc = Parser.getTok().getLoc();
  int SPReg = tryParseRegister();
  if (SPReg == -1)
    return Error(SPRegLoc, "register expected");
  if (SPReg == ARM::SP || SPReg == ARM::PC)
    return Error(SPRegLoc, "sp and pc are not permitted in .movsp directive");

  int64_t Offset = 0;
  if (Parser.parseOptionalToken(AsmToken::Comma)) {
    if (Parser.parseToken(AsmToken::Hash, "expected #constant"))
      return true;

    const MCExpr *OffsetExpr;
    SMLoc OffsetLoc = Parser.getTok().getLoc();

    if (Parser.parseExpression(OffsetExpr))
      return Error(OffsetLoc, "malformed offset expression");

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(OffsetExpr);
    if (!CE)
      return Error(OffsetLoc, "offset must be an immediate constant");

    Offset = CE->getValue();
  }

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.movsp' directive"))
    return true;

  getTargetStreamer().emitMovSP(SPReg, Offset);
  UC.saveFPReg(SPReg);

  return false;
}

/// parseDirectiveObjectArch
///   ::= .object_arch name
bool ARMAsmParser::parseDirectiveObjectArch(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (getLexer().isNot(AsmToken::Identifier))
    return Error(getLexer().getLoc(), "unexpected token");

  StringRef Arch = Parser.getTok().getString();
  SMLoc ArchLoc = Parser.getTok().getLoc();
  Lex();

  ARM::ArchKind ID = ARM::parseArch(Arch);

  if (ID == ARM::ArchKind::INVALID)
    return Error(ArchLoc, "unknown architecture '" + Arch + "'");
  if (parseToken(AsmToken::EndOfStatement))
    return true;

  getTargetStreamer().emitObjectArch(ID);
  return false;
}

/// parseDirectiveAlign
///   ::= .align
bool ARMAsmParser::parseDirectiveAlign(SMLoc L) {
  // NOTE: if this is not the end of the statement, fall back to the target
  // agnostic handling for this directive which will correctly handle this.
  if (parseOptionalToken(AsmToken::EndOfStatement)) {
    // '.align' is target specifically handled to mean 2**2 byte alignment.
    const MCSection *Section = getStreamer().getCurrentSectionOnly();
    assert(Section && "must have section to emit alignment");
    if (Section->UseCodeAlign())
      getStreamer().EmitCodeAlignment(4, 0);
    else
      getStreamer().EmitValueToAlignment(4, 0, 1, 0);
    return false;
  }
  return true;
}

/// parseDirectiveThumbSet
///  ::= .thumb_set name, value
bool ARMAsmParser::parseDirectiveThumbSet(SMLoc L) {
  MCAsmParser &Parser = getParser();

  StringRef Name;
  if (check(Parser.parseIdentifier(Name),
            "expected identifier after '.thumb_set'") ||
      parseToken(AsmToken::Comma, "expected comma after name '" + Name + "'"))
    return true;

  MCSymbol *Sym;
  const MCExpr *Value;
  if (MCParserUtils::parseAssignmentExpression(Name, /* allow_redef */ true,
                                               Parser, Sym, Value))
    return true;

  getTargetStreamer().emitThumbSet(Sym, Value);
  return false;
}

/// Force static initialization.
extern "C" void LLVMInitializeARMAsmParser() {
  RegisterMCAsmParser<ARMAsmParser> X(getTheARMLETarget());
  RegisterMCAsmParser<ARMAsmParser> Y(getTheARMBETarget());
  RegisterMCAsmParser<ARMAsmParser> A(getTheThumbLETarget());
  RegisterMCAsmParser<ARMAsmParser> B(getTheThumbBETarget());
}

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "ARMGenAsmMatcher.inc"

// Some diagnostics need to vary with subtarget features, so they are handled
// here. For example, the DPR class has either 16 or 32 registers, depending
// on the FPU available.
const char *
ARMAsmParser::getCustomOperandDiag(ARMMatchResultTy MatchError) {
  switch (MatchError) {
  // rGPR contains sp starting with ARMv8.
  case Match_rGPR:
    return hasV8Ops() ? "operand must be a register in range [r0, r14]"
                      : "operand must be a register in range [r0, r12] or r14";
  // DPR contains 16 registers for some FPUs, and 32 for others.
  case Match_DPR:
    return hasD16() ? "operand must be a register in range [d0, d15]"
                    : "operand must be a register in range [d0, d31]";
  case Match_DPR_RegList:
    return hasD16() ? "operand must be a list of registers in range [d0, d15]"
                    : "operand must be a list of registers in range [d0, d31]";

  // For all other diags, use the static string from tablegen.
  default:
    return getMatchKindDiag(MatchError);
  }
}

// Process the list of near-misses, throwing away ones we don't want to report
// to the user, and converting the rest to a source location and string that
// should be reported.
void
ARMAsmParser::FilterNearMisses(SmallVectorImpl<NearMissInfo> &NearMissesIn,
                               SmallVectorImpl<NearMissMessage> &NearMissesOut,
                               SMLoc IDLoc, OperandVector &Operands) {
  // TODO: If operand didn't match, sub in a dummy one and run target
  // predicate, so that we can avoid reporting near-misses that are invalid?
  // TODO: Many operand types dont have SuperClasses set, so we report
  // redundant ones.
  // TODO: Some operands are superclasses of registers (e.g.
  // MCK_RegShiftedImm), we don't have any way to represent that currently.
  // TODO: This is not all ARM-specific, can some of it be factored out?

  // Record some information about near-misses that we have already seen, so
  // that we can avoid reporting redundant ones. For example, if there are
  // variants of an instruction that take 8- and 16-bit immediates, we want
  // to only report the widest one.
  std::multimap<unsigned, unsigned> OperandMissesSeen;
  SmallSet<uint64_t, 4> FeatureMissesSeen;
  bool ReportedTooFewOperands = false;

  // Process the near-misses in reverse order, so that we see more general ones
  // first, and so can avoid emitting more specific ones.
  for (NearMissInfo &I : reverse(NearMissesIn)) {
    switch (I.getKind()) {
    case NearMissInfo::NearMissOperand: {
      SMLoc OperandLoc =
          ((ARMOperand &)*Operands[I.getOperandIndex()]).getStartLoc();
      const char *OperandDiag =
          getCustomOperandDiag((ARMMatchResultTy)I.getOperandError());

      // If we have already emitted a message for a superclass, don't also report
      // the sub-class. We consider all operand classes that we don't have a
      // specialised diagnostic for to be equal for the propose of this check,
      // so that we don't report the generic error multiple times on the same
      // operand.
      unsigned DupCheckMatchClass = OperandDiag ? I.getOperandClass() : ~0U;
      auto PrevReports = OperandMissesSeen.equal_range(I.getOperandIndex());
      if (std::any_of(PrevReports.first, PrevReports.second,
                      [DupCheckMatchClass](
                          const std::pair<unsigned, unsigned> Pair) {
            if (DupCheckMatchClass == ~0U || Pair.second == ~0U)
              return Pair.second == DupCheckMatchClass;
            else
              return isSubclass((MatchClassKind)DupCheckMatchClass,
                                (MatchClassKind)Pair.second);
          }))
        break;
      OperandMissesSeen.insert(
          std::make_pair(I.getOperandIndex(), DupCheckMatchClass));

      NearMissMessage Message;
      Message.Loc = OperandLoc;
      if (OperandDiag) {
        Message.Message = OperandDiag;
      } else if (I.getOperandClass() == InvalidMatchClass) {
        Message.Message = "too many operands for instruction";
      } else {
        Message.Message = "invalid operand for instruction";
        LLVM_DEBUG(
            dbgs() << "Missing diagnostic string for operand class "
                   << getMatchClassName((MatchClassKind)I.getOperandClass())
                   << I.getOperandClass() << ", error " << I.getOperandError()
                   << ", opcode " << MII.getName(I.getOpcode()) << "\n");
      }
      NearMissesOut.emplace_back(Message);
      break;
    }
    case NearMissInfo::NearMissFeature: {
      uint64_t MissingFeatures = I.getFeatures();
      // Don't report the same set of features twice.
      if (FeatureMissesSeen.count(MissingFeatures))
        break;
      FeatureMissesSeen.insert(MissingFeatures);

      // Special case: don't report a feature set which includes arm-mode for
      // targets that don't have ARM mode.
      if ((MissingFeatures & Feature_IsARM) && !hasARM())
        break;
      // Don't report any near-misses that both require switching instruction
      // set, and adding other subtarget features.
      if (isThumb() && (MissingFeatures & Feature_IsARM) &&
          (MissingFeatures & ~Feature_IsARM))
        break;
      if (!isThumb() && (MissingFeatures & Feature_IsThumb) &&
          (MissingFeatures & ~Feature_IsThumb))
        break;
      if (!isThumb() && (MissingFeatures & Feature_IsThumb2) &&
          (MissingFeatures & ~(Feature_IsThumb2 | Feature_IsThumb)))
        break;
      if (isMClass() && (MissingFeatures & Feature_HasNEON))
        break;

      NearMissMessage Message;
      Message.Loc = IDLoc;
      raw_svector_ostream OS(Message.Message);

      OS << "instruction requires:";
      uint64_t Mask = 1;
      for (unsigned MaskPos = 0; MaskPos < (sizeof(MissingFeatures) * 8 - 1);
           ++MaskPos) {
        if (MissingFeatures & Mask) {
          OS << " " << getSubtargetFeatureName(MissingFeatures & Mask);
        }
        Mask <<= 1;
      }
      NearMissesOut.emplace_back(Message);

      break;
    }
    case NearMissInfo::NearMissPredicate: {
      NearMissMessage Message;
      Message.Loc = IDLoc;
      switch (I.getPredicateError()) {
      case Match_RequiresNotITBlock:
        Message.Message = "flag setting instruction only valid outside IT block";
        break;
      case Match_RequiresITBlock:
        Message.Message = "instruction only valid inside IT block";
        break;
      case Match_RequiresV6:
        Message.Message = "instruction variant requires ARMv6 or later";
        break;
      case Match_RequiresThumb2:
        Message.Message = "instruction variant requires Thumb2";
        break;
      case Match_RequiresV8:
        Message.Message = "instruction variant requires ARMv8 or later";
        break;
      case Match_RequiresFlagSetting:
        Message.Message = "no flag-preserving variant of this instruction available";
        break;
      case Match_InvalidOperand:
        Message.Message = "invalid operand for instruction";
        break;
      default:
        llvm_unreachable("Unhandled target predicate error");
        break;
      }
      NearMissesOut.emplace_back(Message);
      break;
    }
    case NearMissInfo::NearMissTooFewOperands: {
      if (!ReportedTooFewOperands) {
        SMLoc EndLoc = ((ARMOperand &)*Operands.back()).getEndLoc();
        NearMissesOut.emplace_back(NearMissMessage{
            EndLoc, StringRef("too few operands for instruction")});
        ReportedTooFewOperands = true;
      }
      break;
    }
    case NearMissInfo::NoNearMiss:
      // This should never leave the matcher.
      llvm_unreachable("not a near-miss");
      break;
    }
  }
}

void ARMAsmParser::ReportNearMisses(SmallVectorImpl<NearMissInfo> &NearMisses,
                                    SMLoc IDLoc, OperandVector &Operands) {
  SmallVector<NearMissMessage, 4> Messages;
  FilterNearMisses(NearMisses, Messages, IDLoc, Operands);

  if (Messages.size() == 0) {
    // No near-misses were found, so the best we can do is "invalid
    // instruction".
    Error(IDLoc, "invalid instruction");
  } else if (Messages.size() == 1) {
    // One near miss was found, report it as the sole error.
    Error(Messages[0].Loc, Messages[0].Message);
  } else {
    // More than one near miss, so report a generic "invalid instruction"
    // error, followed by notes for each of the near-misses.
    Error(IDLoc, "invalid instruction, any one of the following would fix this:");
    for (auto &M : Messages) {
      Note(M.Loc, M.Message);
    }
  }
}

// FIXME: This structure should be moved inside ARMTargetParser
// when we start to table-generate them, and we can use the ARM
// flags below, that were generated by table-gen.
static const struct {
  const unsigned Kind;
  const uint64_t ArchCheck;
  const FeatureBitset Features;
} Extensions[] = {
  { ARM::AEK_CRC, Feature_HasV8, {ARM::FeatureCRC} },
  { ARM::AEK_CRYPTO,  Feature_HasV8,
    {ARM::FeatureCrypto, ARM::FeatureNEON, ARM::FeatureFPARMv8} },
  { ARM::AEK_FP, Feature_HasV8, {ARM::FeatureFPARMv8} },
  { (ARM::AEK_HWDIVTHUMB | ARM::AEK_HWDIVARM), Feature_HasV7 | Feature_IsNotMClass,
    {ARM::FeatureHWDivThumb, ARM::FeatureHWDivARM} },
  { ARM::AEK_MP, Feature_HasV7 | Feature_IsNotMClass, {ARM::FeatureMP} },
  { ARM::AEK_SIMD, Feature_HasV8, {ARM::FeatureNEON, ARM::FeatureFPARMv8} },
  { ARM::AEK_SEC, Feature_HasV6K, {ARM::FeatureTrustZone} },
  // FIXME: Only available in A-class, isel not predicated
  { ARM::AEK_VIRT, Feature_HasV7, {ARM::FeatureVirtualization} },
  { ARM::AEK_FP16, Feature_HasV8_2a, {ARM::FeatureFPARMv8, ARM::FeatureFullFP16} },
  { ARM::AEK_RAS, Feature_HasV8, {ARM::FeatureRAS} },
  // FIXME: Unsupported extensions.
  { ARM::AEK_OS, Feature_None, {} },
  { ARM::AEK_IWMMXT, Feature_None, {} },
  { ARM::AEK_IWMMXT2, Feature_None, {} },
  { ARM::AEK_MAVERICK, Feature_None, {} },
  { ARM::AEK_XSCALE, Feature_None, {} },
};

/// parseDirectiveArchExtension
///   ::= .arch_extension [no]feature
bool ARMAsmParser::parseDirectiveArchExtension(SMLoc L) {
  MCAsmParser &Parser = getParser();

  if (getLexer().isNot(AsmToken::Identifier))
    return Error(getLexer().getLoc(), "expected architecture extension name");

  StringRef Name = Parser.getTok().getString();
  SMLoc ExtLoc = Parser.getTok().getLoc();
  Lex();

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.arch_extension' directive"))
    return true;

  bool EnableFeature = true;
  if (Name.startswith_lower("no")) {
    EnableFeature = false;
    Name = Name.substr(2);
  }
  unsigned FeatureKind = ARM::parseArchExt(Name);
  if (FeatureKind == ARM::AEK_INVALID)
    return Error(ExtLoc, "unknown architectural extension: " + Name);

  for (const auto &Extension : Extensions) {
    if (Extension.Kind != FeatureKind)
      continue;

    if (Extension.Features.none())
      return Error(ExtLoc, "unsupported architectural extension: " + Name);

    if ((getAvailableFeatures() & Extension.ArchCheck) != Extension.ArchCheck)
      return Error(ExtLoc, "architectural extension '" + Name +
                               "' is not "
                               "allowed for the current base architecture");

    MCSubtargetInfo &STI = copySTI();
    FeatureBitset ToggleFeatures = EnableFeature
      ? (~STI.getFeatureBits() & Extension.Features)
      : ( STI.getFeatureBits() & Extension.Features);

    uint64_t Features =
        ComputeAvailableFeatures(STI.ToggleFeature(ToggleFeatures));
    setAvailableFeatures(Features);
    return false;
  }

  return Error(ExtLoc, "unknown architectural extension: " + Name);
}

// Define this matcher function after the auto-generated include so we
// have the match class enum definitions.
unsigned ARMAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                  unsigned Kind) {
  ARMOperand &Op = static_cast<ARMOperand &>(AsmOp);
  // If the kind is a token for a literal immediate, check if our asm
  // operand matches. This is for InstAliases which have a fixed-value
  // immediate in the syntax.
  switch (Kind) {
  default: break;
  case MCK__35_0:
    if (Op.isImm())
      if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Op.getImm()))
        if (CE->getValue() == 0)
          return Match_Success;
    break;
  case MCK_ModImm:
    if (Op.isImm()) {
      const MCExpr *SOExpr = Op.getImm();
      int64_t Value;
      if (!SOExpr->evaluateAsAbsolute(Value))
        return Match_Success;
      assert((Value >= std::numeric_limits<int32_t>::min() &&
              Value <= std::numeric_limits<uint32_t>::max()) &&
             "expression value must be representable in 32 bits");
    }
    break;
  case MCK_rGPR:
    if (hasV8Ops() && Op.isReg() && Op.getReg() == ARM::SP)
      return Match_Success;
    return Match_rGPR;
  case MCK_GPRPair:
    if (Op.isReg() &&
        MRI->getRegClass(ARM::GPRRegClassID).contains(Op.getReg()))
      return Match_Success;
    break;
  }
  return Match_InvalidOperand;
}
