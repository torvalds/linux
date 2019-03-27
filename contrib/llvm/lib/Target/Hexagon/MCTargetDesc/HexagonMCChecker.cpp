//===----- HexagonMCChecker.cpp - Instruction bundle checking -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the checking of insns inside a bundle according to the
// packet constraint rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCChecker.h"
#include "Hexagon.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>

using namespace llvm;

static cl::opt<bool>
    RelaxNVChecks("relax-nv-checks", cl::init(false), cl::ZeroOrMore,
                  cl::Hidden, cl::desc("Relax checks of new-value validity"));

const HexagonMCChecker::PredSense
    HexagonMCChecker::Unconditional(Hexagon::NoRegister, false);

void HexagonMCChecker::init() {
  // Initialize read-only registers set.
  ReadOnly.insert(Hexagon::PC);
  ReadOnly.insert(Hexagon::C9_8);

  // Figure out the loop-registers definitions.
  if (HexagonMCInstrInfo::isInnerLoop(MCB)) {
    Defs[Hexagon::SA0].insert(Unconditional); // FIXME: define or change SA0?
    Defs[Hexagon::LC0].insert(Unconditional);
  }
  if (HexagonMCInstrInfo::isOuterLoop(MCB)) {
    Defs[Hexagon::SA1].insert(Unconditional); // FIXME: define or change SA0?
    Defs[Hexagon::LC1].insert(Unconditional);
  }

  if (HexagonMCInstrInfo::isBundle(MCB))
    // Unfurl a bundle.
    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
      MCInst const &Inst = *I.getInst();
      if (HexagonMCInstrInfo::isDuplex(MCII, Inst)) {
        init(*Inst.getOperand(0).getInst());
        init(*Inst.getOperand(1).getInst());
      } else
        init(Inst);
    }
  else
    init(MCB);
}

void HexagonMCChecker::initReg(MCInst const &MCI, unsigned R, unsigned &PredReg,
                               bool &isTrue) {
  if (HexagonMCInstrInfo::isPredicated(MCII, MCI) && isPredicateRegister(R)) {
    // Note an used predicate register.
    PredReg = R;
    isTrue = HexagonMCInstrInfo::isPredicatedTrue(MCII, MCI);

    // Note use of new predicate register.
    if (HexagonMCInstrInfo::isPredicatedNew(MCII, MCI))
      NewPreds.insert(PredReg);
  } else
    // Note register use.  Super-registers are not tracked directly,
    // but their components.
    for (MCRegAliasIterator SRI(R, &RI, !MCSubRegIterator(R, &RI).isValid());
         SRI.isValid(); ++SRI)
      if (!MCSubRegIterator(*SRI, &RI).isValid())
        // Skip super-registers used indirectly.
        Uses.insert(*SRI);
}

void HexagonMCChecker::init(MCInst const &MCI) {
  const MCInstrDesc &MCID = HexagonMCInstrInfo::getDesc(MCII, MCI);
  unsigned PredReg = Hexagon::NoRegister;
  bool isTrue = false;

  // Get used registers.
  for (unsigned i = MCID.getNumDefs(); i < MCID.getNumOperands(); ++i)
    if (MCI.getOperand(i).isReg())
      initReg(MCI, MCI.getOperand(i).getReg(), PredReg, isTrue);
  for (unsigned i = 0; i < MCID.getNumImplicitUses(); ++i)
    initReg(MCI, MCID.getImplicitUses()[i], PredReg, isTrue);

  // Get implicit register definitions.
  if (const MCPhysReg *ImpDef = MCID.getImplicitDefs())
    for (; *ImpDef; ++ImpDef) {
      unsigned R = *ImpDef;

      if (Hexagon::R31 != R && MCID.isCall())
        // Any register other than the LR and the PC are actually volatile ones
        // as defined by the ABI, not modified implicitly by the call insn.
        continue;
      if (Hexagon::PC == R)
        // Branches are the only insns that can change the PC,
        // otherwise a read-only register.
        continue;

      if (Hexagon::USR_OVF == R)
        // Many insns change the USR implicitly, but only one or another flag.
        // The instruction table models the USR.OVF flag, which can be
        // implicitly modified more than once, but cannot be modified in the
        // same packet with an instruction that modifies is explicitly. Deal
        // with such situations individually.
        SoftDefs.insert(R);
      else if (isPredicateRegister(R) &&
               HexagonMCInstrInfo::isPredicateLate(MCII, MCI))
        // Include implicit late predicates.
        LatePreds.insert(R);
      else
        Defs[R].insert(PredSense(PredReg, isTrue));
    }

  // Figure out explicit register definitions.
  for (unsigned i = 0; i < MCID.getNumDefs(); ++i) {
    unsigned R = MCI.getOperand(i).getReg(), S = Hexagon::NoRegister;
    // USR has subregisters (while C8 does not for technical reasons), so
    // reset R to USR, since we know how to handle multiple defs of USR,
    // taking into account its subregisters.
    if (R == Hexagon::C8)
      R = Hexagon::USR;

    // Note register definitions, direct ones as well as indirect side-effects.
    // Super-registers are not tracked directly, but their components.
    for (MCRegAliasIterator SRI(R, &RI, !MCSubRegIterator(R, &RI).isValid());
         SRI.isValid(); ++SRI) {
      if (MCSubRegIterator(*SRI, &RI).isValid())
        // Skip super-registers defined indirectly.
        continue;

      if (R == *SRI) {
        if (S == R)
          // Avoid scoring the defined register multiple times.
          continue;
        else
          // Note that the defined register has already been scored.
          S = R;
      }

      if (Hexagon::P3_0 != R && Hexagon::P3_0 == *SRI)
        // P3:0 is a special case, since multiple predicate register definitions
        // in a packet is allowed as the equivalent of their logical "and".
        // Only an explicit definition of P3:0 is noted as such; if a
        // side-effect, then note as a soft definition.
        SoftDefs.insert(*SRI);
      else if (HexagonMCInstrInfo::isPredicateLate(MCII, MCI) &&
               isPredicateRegister(*SRI))
        // Some insns produce predicates too late to be used in the same packet.
        LatePreds.insert(*SRI);
      else if (i == 0 && HexagonMCInstrInfo::getType(MCII, MCI) ==
                             HexagonII::TypeCVI_VM_TMP_LD)
        // Temporary loads should be used in the same packet, but don't commit
        // results, so it should be disregarded if another insn changes the same
        // register.
        // TODO: relies on the impossibility of a current and a temporary loads
        // in the same packet.
        TmpDefs.insert(*SRI);
      else if (i <= 1 && HexagonMCInstrInfo::hasNewValue2(MCII, MCI))
        // vshuff(Vx, Vy, Rx) <- Vx(0) and Vy(1) are both source and
        // destination registers with this instruction. same for vdeal(Vx,Vy,Rx)
        Uses.insert(*SRI);
      else
        Defs[*SRI].insert(PredSense(PredReg, isTrue));
    }
  }

  // Figure out definitions of new predicate registers.
  if (HexagonMCInstrInfo::isPredicatedNew(MCII, MCI))
    for (unsigned i = MCID.getNumDefs(); i < MCID.getNumOperands(); ++i)
      if (MCI.getOperand(i).isReg()) {
        unsigned P = MCI.getOperand(i).getReg();

        if (isPredicateRegister(P))
          NewPreds.insert(P);
      }
}

HexagonMCChecker::HexagonMCChecker(MCContext &Context, MCInstrInfo const &MCII,
                                   MCSubtargetInfo const &STI, MCInst &mcb,
                                   MCRegisterInfo const &ri, bool ReportErrors)
    : Context(Context), MCB(mcb), RI(ri), MCII(MCII), STI(STI),
      ReportErrors(ReportErrors) {
  init();
}

HexagonMCChecker::HexagonMCChecker(HexagonMCChecker const &Other,
                                   MCSubtargetInfo const &STI,
                                   bool CopyReportErrors)
    : Context(Other.Context), MCB(Other.MCB), RI(Other.RI), MCII(Other.MCII),
      STI(STI), ReportErrors(CopyReportErrors ? Other.ReportErrors : false) {}

bool HexagonMCChecker::check(bool FullCheck) {
  bool chkP = checkPredicates();
  bool chkNV = checkNewValues();
  bool chkR = checkRegisters();
  bool chkRRO = checkRegistersReadOnly();
  checkRegisterCurDefs();
  bool chkS = checkSolo();
  bool chkSh = true;
  if (FullCheck)
    chkSh = checkShuffle();
  bool chkSl = true;
  if (FullCheck)
    chkSl = checkSlots();
  bool chkAXOK = checkAXOK();
  bool chkCofMax1 = checkCOFMax1();
  bool chkHWLoop = checkHWLoop();
  bool chk = chkP && chkNV && chkR && chkRRO && chkS && chkSh && chkSl &&
             chkAXOK && chkCofMax1 && chkHWLoop;

  return chk;
}

static bool isDuplexAGroup(unsigned Opcode) {
  switch (Opcode) {
  case Hexagon::SA1_addi:
  case Hexagon::SA1_addrx:
  case Hexagon::SA1_addsp:
  case Hexagon::SA1_and1:
  case Hexagon::SA1_clrf:
  case Hexagon::SA1_clrfnew:
  case Hexagon::SA1_clrt:
  case Hexagon::SA1_clrtnew:
  case Hexagon::SA1_cmpeqi:
  case Hexagon::SA1_combine0i:
  case Hexagon::SA1_combine1i:
  case Hexagon::SA1_combine2i:
  case Hexagon::SA1_combine3i:
  case Hexagon::SA1_combinerz:
  case Hexagon::SA1_combinezr:
  case Hexagon::SA1_dec:
  case Hexagon::SA1_inc:
  case Hexagon::SA1_seti:
  case Hexagon::SA1_setin1:
  case Hexagon::SA1_sxtb:
  case Hexagon::SA1_sxth:
  case Hexagon::SA1_tfr:
  case Hexagon::SA1_zxtb:
  case Hexagon::SA1_zxth:
    return true;
    break;
  default:
    return false;
  }
}

static bool isNeitherAnorX(MCInstrInfo const &MCII, MCInst const &ID) {
  unsigned Result = 0;
  unsigned Type = HexagonMCInstrInfo::getType(MCII, ID);
  if (Type == HexagonII::TypeDUPLEX) {
    unsigned subInst0Opcode = ID.getOperand(0).getInst()->getOpcode();
    unsigned subInst1Opcode = ID.getOperand(1).getInst()->getOpcode();
    Result += !isDuplexAGroup(subInst0Opcode);
    Result += !isDuplexAGroup(subInst1Opcode);
  } else
    Result +=
        Type != HexagonII::TypeALU32_2op && Type != HexagonII::TypeALU32_3op &&
        Type != HexagonII::TypeALU32_ADDI && Type != HexagonII::TypeS_2op &&
        Type != HexagonII::TypeS_3op &&
        (Type != HexagonII::TypeALU64 || HexagonMCInstrInfo::isFloat(MCII, ID));
  return Result != 0;
}

bool HexagonMCChecker::checkAXOK() {
  MCInst const *HasSoloAXInst = nullptr;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::isSoloAX(MCII, I)) {
      HasSoloAXInst = &I;
    }
  }
  if (!HasSoloAXInst)
    return true;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (&I != HasSoloAXInst && isNeitherAnorX(MCII, I)) {
      reportError(
          HasSoloAXInst->getLoc(),
          Twine("Instruction can only be in a packet with ALU or non-FPU XTYPE "
                "instructions"));
      reportError(I.getLoc(),
                  Twine("Not an ALU or non-FPU XTYPE instruction"));
      return false;
    }
  }
  return true;
}

void HexagonMCChecker::reportBranchErrors() {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, I);
    if (Desc.isBranch() || Desc.isCall() || Desc.isReturn())
      reportNote(I.getLoc(), "Branching instruction");
  }
}

bool HexagonMCChecker::checkHWLoop() {
  if (!HexagonMCInstrInfo::isInnerLoop(MCB) &&
      !HexagonMCInstrInfo::isOuterLoop(MCB))
    return true;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, I);
    if (Desc.isBranch() || Desc.isCall() || Desc.isReturn()) {
      reportError(MCB.getLoc(),
                  "Branches cannot be in a packet with hardware loops");
      reportBranchErrors();
      return false;
    }
  }
  return true;
}

bool HexagonMCChecker::checkCOFMax1() {
  SmallVector<MCInst const *, 2> BranchLocations;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, I);
    if (Desc.isBranch() || Desc.isCall() || Desc.isReturn())
      BranchLocations.push_back(&I);
  }
  for (unsigned J = 0, N = BranchLocations.size(); J < N; ++J) {
    MCInst const &I = *BranchLocations[J];
    if (HexagonMCInstrInfo::isCofMax1(MCII, I)) {
      bool Relax1 = HexagonMCInstrInfo::isCofRelax1(MCII, I);
      bool Relax2 = HexagonMCInstrInfo::isCofRelax2(MCII, I);
      if (N > 1 && !Relax1 && !Relax2) {
        reportError(I.getLoc(),
                    "Instruction may not be in a packet with other branches");
        reportBranchErrors();
        return false;
      }
      if (N > 1 && J == 0 && !Relax1) {
        reportError(I.getLoc(),
                    "Instruction may not be the first branch in packet");
        reportBranchErrors();
        return false;
      }
      if (N > 1 && J == 1 && !Relax2) {
        reportError(I.getLoc(),
                    "Instruction may not be the second branch in packet");
        reportBranchErrors();
        return false;
      }
    }
  }
  return true;
}

bool HexagonMCChecker::checkSlots() {
  unsigned slotsUsed = 0;
  for (auto HMI : HexagonMCInstrInfo::bundleInstructions(MCB)) {
    MCInst const &MCI = *HMI.getInst();
    if (HexagonMCInstrInfo::isImmext(MCI))
      continue;
    if (HexagonMCInstrInfo::isDuplex(MCII, MCI))
      slotsUsed += 2;
    else
      ++slotsUsed;
  }

  if (slotsUsed > HEXAGON_PACKET_SIZE) {
    reportError("invalid instruction packet: out of slots");
    return false;
  }
  return true;
}

// Check legal use of predicate registers.
bool HexagonMCChecker::checkPredicates() {
  // Check for proper use of new predicate registers.
  for (const auto &I : NewPreds) {
    unsigned P = I;

    if (!Defs.count(P) || LatePreds.count(P)) {
      // Error out if the new predicate register is not defined,
      // or defined "late"
      // (e.g., "{ if (p3.new)... ; p3 = sp1loop0(#r7:2, Rs) }").
      reportErrorNewValue(P);
      return false;
    }
  }

  // Check for proper use of auto-anded of predicate registers.
  for (const auto &I : LatePreds) {
    unsigned P = I;

    if (LatePreds.count(P) > 1 || Defs.count(P)) {
      // Error out if predicate register defined "late" multiple times or
      // defined late and regularly defined
      // (e.g., "{ p3 = sp1loop0(...); p3 = cmp.eq(...) }".
      reportErrorRegisters(P);
      return false;
    }
  }

  return true;
}

// Check legal use of new values.
bool HexagonMCChecker::checkNewValues() {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (!HexagonMCInstrInfo::isNewValue(MCII, I))
      continue;
    auto Consumer = HexagonMCInstrInfo::predicateInfo(MCII, I);
    bool Branch = HexagonMCInstrInfo::getDesc(MCII, I).isBranch();
    MCOperand const &Op = HexagonMCInstrInfo::getNewValueOperand(MCII, I);
    assert(Op.isReg());
    auto Producer = registerProducer(Op.getReg(), Consumer);
    if (std::get<0>(Producer) == nullptr) {
      reportError(I.getLoc(), "New value register consumer has no producer");
      return false;
    }
    if (!RelaxNVChecks) {
      // Checks that statically prove correct new value consumption
      if (std::get<2>(Producer).isPredicated() &&
          (!Consumer.isPredicated() ||
           llvm::HexagonMCInstrInfo::getType(MCII, I) == HexagonII::TypeNCJ)) {
        reportNote(
            std::get<0>(Producer)->getLoc(),
            "Register producer is predicated and consumer is unconditional");
        reportError(I.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
      if (std::get<2>(Producer).Register != Hexagon::NoRegister &&
          std::get<2>(Producer).Register != Consumer.Register) {
        reportNote(std::get<0>(Producer)->getLoc(),
                   "Register producer does not use the same predicate "
                   "register as the consumer");
        reportError(I.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
    }
    if (std::get<2>(Producer).Register == Consumer.Register &&
        Consumer.PredicatedTrue != std::get<2>(Producer).PredicatedTrue) {
      reportNote(
          std::get<0>(Producer)->getLoc(),
          "Register producer has the opposite predicate sense as consumer");
      reportError(I.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }
    MCInstrDesc const &Desc =
        HexagonMCInstrInfo::getDesc(MCII, *std::get<0>(Producer));
    if (Desc.OpInfo[std::get<1>(Producer)].RegClass ==
        Hexagon::DoubleRegsRegClassID) {
      reportNote(std::get<0>(Producer)->getLoc(),
                 "Double registers cannot be new-value producers");
      reportError(I.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }
    if ((Desc.mayLoad() && std::get<1>(Producer) == 1) ||
        (Desc.mayStore() && std::get<1>(Producer) == 0)) {
      unsigned Mode =
          HexagonMCInstrInfo::getAddrMode(MCII, *std::get<0>(Producer));
      StringRef ModeError;
      if (Mode == HexagonII::AbsoluteSet)
        ModeError = "Absolute-set";
      if (Mode == HexagonII::PostInc)
        ModeError = "Auto-increment";
      if (!ModeError.empty()) {
        reportNote(std::get<0>(Producer)->getLoc(),
                   ModeError + " registers cannot be a new-value "
                               "producer");
        reportError(I.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
    }
    if (Branch && HexagonMCInstrInfo::isFloat(MCII, *std::get<0>(Producer))) {
      reportNote(std::get<0>(Producer)->getLoc(),
                 "FPU instructions cannot be new-value producers for jumps");
      reportError(I.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }
  }
  return true;
}

bool HexagonMCChecker::checkRegistersReadOnly() {
  for (auto I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
    MCInst const &Inst = *I.getInst();
    unsigned Defs = HexagonMCInstrInfo::getDesc(MCII, Inst).getNumDefs();
    for (unsigned j = 0; j < Defs; ++j) {
      MCOperand const &Operand = Inst.getOperand(j);
      assert(Operand.isReg() && "Def is not a register");
      unsigned Register = Operand.getReg();
      if (ReadOnly.find(Register) != ReadOnly.end()) {
        reportError(Inst.getLoc(), "Cannot write to read-only register `" +
                                       Twine(RI.getName(Register)) + "'");
        return false;
      }
    }
  }
  return true;
}

bool HexagonMCChecker::registerUsed(unsigned Register) {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB))
    for (unsigned j = HexagonMCInstrInfo::getDesc(MCII, I).getNumDefs(),
                  n = I.getNumOperands();
         j < n; ++j) {
      MCOperand const &Operand = I.getOperand(j);
      if (Operand.isReg() && Operand.getReg() == Register)
        return true;
    }
  return false;
}

std::tuple<MCInst const *, unsigned, HexagonMCInstrInfo::PredicateInfo>
HexagonMCChecker::registerProducer(
    unsigned Register, HexagonMCInstrInfo::PredicateInfo ConsumerPredicate) {
  std::tuple<MCInst const *, unsigned, HexagonMCInstrInfo::PredicateInfo>
      WrongSense;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, I);
    auto ProducerPredicate = HexagonMCInstrInfo::predicateInfo(MCII, I);
    for (unsigned J = 0, N = Desc.getNumDefs(); J < N; ++J)
      for (auto K = MCRegAliasIterator(I.getOperand(J).getReg(), &RI, true);
           K.isValid(); ++K)
        if (*K == Register) {
          if (RelaxNVChecks ||
              (ProducerPredicate.Register == ConsumerPredicate.Register &&
               (ProducerPredicate.Register == Hexagon::NoRegister ||
                ProducerPredicate.PredicatedTrue ==
                    ConsumerPredicate.PredicatedTrue)))
            return std::make_tuple(&I, J, ProducerPredicate);
          std::get<0>(WrongSense) = &I;
          std::get<1>(WrongSense) = J;
          std::get<2>(WrongSense) = ProducerPredicate;
        }
    if (Register == Hexagon::VTMP && HexagonMCInstrInfo::hasTmpDst(MCII, I))
      return std::make_tuple(&I, 0, HexagonMCInstrInfo::PredicateInfo());
  }
  return WrongSense;
}

void HexagonMCChecker::checkRegisterCurDefs() {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::isCVINew(MCII, I) &&
        HexagonMCInstrInfo::getDesc(MCII, I).mayLoad()) {
      unsigned Register = I.getOperand(0).getReg();
      if (!registerUsed(Register))
        reportWarning("Register `" + Twine(RI.getName(Register)) +
                      "' used with `.cur' "
                      "but not used in the same packet");
    }
  }
}

// Check for legal register uses and definitions.
bool HexagonMCChecker::checkRegisters() {
  // Check for proper register definitions.
  for (const auto &I : Defs) {
    unsigned R = I.first;

    if (isLoopRegister(R) && Defs.count(R) > 1 &&
        (HexagonMCInstrInfo::isInnerLoop(MCB) ||
         HexagonMCInstrInfo::isOuterLoop(MCB))) {
      // Error out for definitions of loop registers at the end of a loop.
      reportError("loop-setup and some branch instructions "
                  "cannot be in the same packet");
      return false;
    }
    if (SoftDefs.count(R)) {
      // Error out for explicit changes to registers also weakly defined
      // (e.g., "{ usr = r0; r0 = sfadd(...) }").
      unsigned UsrR = Hexagon::USR; // Silence warning about mixed types in ?:.
      unsigned BadR = RI.isSubRegister(Hexagon::USR, R) ? UsrR : R;
      reportErrorRegisters(BadR);
      return false;
    }
    if (!isPredicateRegister(R) && Defs[R].size() > 1) {
      // Check for multiple register definitions.
      PredSet &PM = Defs[R];

      // Check for multiple unconditional register definitions.
      if (PM.count(Unconditional)) {
        // Error out on an unconditional change when there are any other
        // changes, conditional or not.
        unsigned UsrR = Hexagon::USR;
        unsigned BadR = RI.isSubRegister(Hexagon::USR, R) ? UsrR : R;
        reportErrorRegisters(BadR);
        return false;
      }
      // Check for multiple conditional register definitions.
      for (const auto &J : PM) {
        PredSense P = J;

        // Check for multiple uses of the same condition.
        if (PM.count(P) > 1) {
          // Error out on conditional changes based on the same predicate
          // (e.g., "{ if (!p0) r0 =...; if (!p0) r0 =... }").
          reportErrorRegisters(R);
          return false;
        }
        // Check for the use of the complementary condition.
        P.second = !P.second;
        if (PM.count(P) && PM.size() > 2) {
          // Error out on conditional changes based on the same predicate
          // multiple times
          // (e.g., "if (p0) r0 =...; if (!p0) r0 =... }; if (!p0) r0 =...").
          reportErrorRegisters(R);
          return false;
        }
      }
    }
  }

  // Check for use of temporary definitions.
  for (const auto &I : TmpDefs) {
    unsigned R = I;

    if (!Uses.count(R)) {
      // special case for vhist
      bool vHistFound = false;
      for (auto const &HMI : HexagonMCInstrInfo::bundleInstructions(MCB)) {
        if (HexagonMCInstrInfo::getType(MCII, *HMI.getInst()) ==
            HexagonII::TypeCVI_HIST) {
          vHistFound = true; // vhist() implicitly uses ALL REGxx.tmp
          break;
        }
      }
      // Warn on an unused temporary definition.
      if (!vHistFound) {
        reportWarning("register `" + Twine(RI.getName(R)) +
                      "' used with `.tmp' but not used in the same packet");
        return true;
      }
    }
  }

  return true;
}

// Check for legal use of solo insns.
bool HexagonMCChecker::checkSolo() {
  if (HexagonMCInstrInfo::bundleSize(MCB) > 1)
    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
      if (HexagonMCInstrInfo::isSolo(MCII, I)) {
        reportError(I.getLoc(), "Instruction is marked `isSolo' and "
                                "cannot have other instructions in "
                                "the same packet");
        return false;
      }
    }

  return true;
}

bool HexagonMCChecker::checkShuffle() {
  HexagonMCShuffler MCSDX(Context, ReportErrors, MCII, STI, MCB);
  return MCSDX.check();
}

void HexagonMCChecker::compoundRegisterMap(unsigned &Register) {
  switch (Register) {
  default:
    break;
  case Hexagon::R15:
    Register = Hexagon::R23;
    break;
  case Hexagon::R14:
    Register = Hexagon::R22;
    break;
  case Hexagon::R13:
    Register = Hexagon::R21;
    break;
  case Hexagon::R12:
    Register = Hexagon::R20;
    break;
  case Hexagon::R11:
    Register = Hexagon::R19;
    break;
  case Hexagon::R10:
    Register = Hexagon::R18;
    break;
  case Hexagon::R9:
    Register = Hexagon::R17;
    break;
  case Hexagon::R8:
    Register = Hexagon::R16;
    break;
  }
}

void HexagonMCChecker::reportErrorRegisters(unsigned Register) {
  reportError("register `" + Twine(RI.getName(Register)) +
              "' modified more than once");
}

void HexagonMCChecker::reportErrorNewValue(unsigned Register) {
  reportError("register `" + Twine(RI.getName(Register)) +
              "' used with `.new' "
              "but not validly modified in the same packet");
}

void HexagonMCChecker::reportError(Twine const &Msg) {
  reportError(MCB.getLoc(), Msg);
}

void HexagonMCChecker::reportError(SMLoc Loc, Twine const &Msg) {
  if (ReportErrors)
    Context.reportError(Loc, Msg);
}

void HexagonMCChecker::reportNote(SMLoc Loc, llvm::Twine const &Msg) {
  if (ReportErrors) {
    auto SM = Context.getSourceManager();
    if (SM)
      SM->PrintMessage(Loc, SourceMgr::DK_Note, Msg);
  }
}

void HexagonMCChecker::reportWarning(Twine const &Msg) {
  if (ReportErrors) {
    auto SM = Context.getSourceManager();
    if (SM)
      SM->PrintMessage(MCB.getLoc(), SourceMgr::DK_Warning, Msg);
  }
}
