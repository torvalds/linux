//===- HexagonMCDuplexInfo.cpp - Instruction bundle checking --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements duplexing of instructions to reduce code size
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;
using namespace Hexagon;

#define DEBUG_TYPE "hexagon-mcduplex-info"

// pair table of subInstructions with opcodes
static const std::pair<unsigned, unsigned> opcodeData[] = {
    std::make_pair((unsigned)SA1_addi, 0),
    std::make_pair((unsigned)SA1_addrx, 6144),
    std::make_pair((unsigned)SA1_addsp, 3072),
    std::make_pair((unsigned)SA1_and1, 4608),
    std::make_pair((unsigned)SA1_clrf, 6768),
    std::make_pair((unsigned)SA1_clrfnew, 6736),
    std::make_pair((unsigned)SA1_clrt, 6752),
    std::make_pair((unsigned)SA1_clrtnew, 6720),
    std::make_pair((unsigned)SA1_cmpeqi, 6400),
    std::make_pair((unsigned)SA1_combine0i, 7168),
    std::make_pair((unsigned)SA1_combine1i, 7176),
    std::make_pair((unsigned)SA1_combine2i, 7184),
    std::make_pair((unsigned)SA1_combine3i, 7192),
    std::make_pair((unsigned)SA1_combinerz, 7432),
    std::make_pair((unsigned)SA1_combinezr, 7424),
    std::make_pair((unsigned)SA1_dec, 4864),
    std::make_pair((unsigned)SA1_inc, 4352),
    std::make_pair((unsigned)SA1_seti, 2048),
    std::make_pair((unsigned)SA1_setin1, 6656),
    std::make_pair((unsigned)SA1_sxtb, 5376),
    std::make_pair((unsigned)SA1_sxth, 5120),
    std::make_pair((unsigned)SA1_tfr, 4096),
    std::make_pair((unsigned)SA1_zxtb, 5888),
    std::make_pair((unsigned)SA1_zxth, 5632),
    std::make_pair((unsigned)SL1_loadri_io, 0),
    std::make_pair((unsigned)SL1_loadrub_io, 4096),
    std::make_pair((unsigned)SL2_deallocframe, 7936),
    std::make_pair((unsigned)SL2_jumpr31, 8128),
    std::make_pair((unsigned)SL2_jumpr31_f, 8133),
    std::make_pair((unsigned)SL2_jumpr31_fnew, 8135),
    std::make_pair((unsigned)SL2_jumpr31_t, 8132),
    std::make_pair((unsigned)SL2_jumpr31_tnew, 8134),
    std::make_pair((unsigned)SL2_loadrb_io, 4096),
    std::make_pair((unsigned)SL2_loadrd_sp, 7680),
    std::make_pair((unsigned)SL2_loadrh_io, 0),
    std::make_pair((unsigned)SL2_loadri_sp, 7168),
    std::make_pair((unsigned)SL2_loadruh_io, 2048),
    std::make_pair((unsigned)SL2_return, 8000),
    std::make_pair((unsigned)SL2_return_f, 8005),
    std::make_pair((unsigned)SL2_return_fnew, 8007),
    std::make_pair((unsigned)SL2_return_t, 8004),
    std::make_pair((unsigned)SL2_return_tnew, 8006),
    std::make_pair((unsigned)SS1_storeb_io, 4096),
    std::make_pair((unsigned)SS1_storew_io, 0),
    std::make_pair((unsigned)SS2_allocframe, 7168),
    std::make_pair((unsigned)SS2_storebi0, 4608),
    std::make_pair((unsigned)SS2_storebi1, 4864),
    std::make_pair((unsigned)SS2_stored_sp, 2560),
    std::make_pair((unsigned)SS2_storeh_io, 0),
    std::make_pair((unsigned)SS2_storew_sp, 2048),
    std::make_pair((unsigned)SS2_storewi0, 4096),
    std::make_pair((unsigned)SS2_storewi1, 4352)};

bool HexagonMCInstrInfo::isDuplexPairMatch(unsigned Ga, unsigned Gb) {
  switch (Ga) {
  case HexagonII::HSIG_None:
  default:
    return false;
  case HexagonII::HSIG_L1:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_L2:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_S1:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_S1 || Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_S2:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_S1 || Gb == HexagonII::HSIG_S2 ||
            Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_A:
    return (Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_Compound:
    return (Gb == HexagonII::HSIG_Compound);
  }
  return false;
}

unsigned HexagonMCInstrInfo::iClassOfDuplexPair(unsigned Ga, unsigned Gb) {
  switch (Ga) {
  case HexagonII::HSIG_None:
  default:
    break;
  case HexagonII::HSIG_L1:
    switch (Gb) {
    default:
      break;
    case HexagonII::HSIG_L1:
      return 0;
    case HexagonII::HSIG_A:
      return 0x4;
    }
    break;
  case HexagonII::HSIG_L2:
    switch (Gb) {
    default:
      break;
    case HexagonII::HSIG_L1:
      return 0x1;
    case HexagonII::HSIG_L2:
      return 0x2;
    case HexagonII::HSIG_A:
      return 0x5;
    }
    break;
  case HexagonII::HSIG_S1:
    switch (Gb) {
    default:
      break;
    case HexagonII::HSIG_L1:
      return 0x8;
    case HexagonII::HSIG_L2:
      return 0x9;
    case HexagonII::HSIG_S1:
      return 0xA;
    case HexagonII::HSIG_A:
      return 0x6;
    }
    break;
  case HexagonII::HSIG_S2:
    switch (Gb) {
    default:
      break;
    case HexagonII::HSIG_L1:
      return 0xC;
    case HexagonII::HSIG_L2:
      return 0xD;
    case HexagonII::HSIG_S1:
      return 0xB;
    case HexagonII::HSIG_S2:
      return 0xE;
    case HexagonII::HSIG_A:
      return 0x7;
    }
    break;
  case HexagonII::HSIG_A:
    switch (Gb) {
    default:
      break;
    case HexagonII::HSIG_A:
      return 0x3;
    }
    break;
  case HexagonII::HSIG_Compound:
    switch (Gb) {
    case HexagonII::HSIG_Compound:
      return 0xFFFFFFFF;
    }
    break;
  }
  return 0xFFFFFFFF;
}

unsigned HexagonMCInstrInfo::getDuplexCandidateGroup(MCInst const &MCI) {
  unsigned DstReg, PredReg, SrcReg, Src1Reg, Src2Reg;

  switch (MCI.getOpcode()) {
  default:
    return HexagonII::HSIG_None;
  //
  // Group L1:
  //
  // Rd = memw(Rs+#u4:2)
  // Rd = memub(Rs+#u4:0)
  case Hexagon::L2_loadri_io:
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    // Special case this one from Group L2.
    // Rd = memw(r29+#u5:2)
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg)) {
      if (HexagonMCInstrInfo::isIntReg(SrcReg) &&
          Hexagon::R29 == SrcReg && inRange<5, 2>(MCI, 2)) {
        return HexagonII::HSIG_L2;
      }
      // Rd = memw(Rs+#u4:2)
      if (HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
          inRange<4, 2>(MCI, 2)) {
        return HexagonII::HSIG_L1;
      }
    }
    break;
  case Hexagon::L2_loadrub_io:
    // Rd = memub(Rs+#u4:0)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        inRange<4>(MCI, 2)) {
      return HexagonII::HSIG_L1;
    }
    break;
  //
  // Group L2:
  //
  // Rd = memh/memuh(Rs+#u3:1)
  // Rd = memb(Rs+#u3:0)
  // Rd = memw(r29+#u5:2) - Handled above.
  // Rdd = memd(r29+#u5:3)
  // deallocframe
  // [if ([!]p0[.new])] dealloc_return
  // [if ([!]p0[.new])] jumpr r31
  case Hexagon::L2_loadrh_io:
  case Hexagon::L2_loadruh_io:
    // Rd = memh/memuh(Rs+#u3:1)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        inRange<3, 1>(MCI, 2)) {
      return HexagonII::HSIG_L2;
    }
    break;
  case Hexagon::L2_loadrb_io:
    // Rd = memb(Rs+#u3:0)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        inRange<3>(MCI, 2)) {
      return HexagonII::HSIG_L2;
    }
    break;
  case Hexagon::L2_loadrd_io:
    // Rdd = memd(r29+#u5:3)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isDblRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntReg(SrcReg) && Hexagon::R29 == SrcReg &&
        inRange<5, 3>(MCI, 2)) {
      return HexagonII::HSIG_L2;
    }
    break;

  case Hexagon::L4_return:
  case Hexagon::L2_deallocframe:
    return HexagonII::HSIG_L2;

  case Hexagon::EH_RETURN_JMPR:
  case Hexagon::J2_jumpr:
  case Hexagon::PS_jmpret:
    // jumpr r31
    // Actual form JMPR implicit-def %pc, implicit %r31, implicit internal %r0.
    DstReg = MCI.getOperand(0).getReg();
    if (Hexagon::R31 == DstReg)
      return HexagonII::HSIG_L2;
    break;

  case Hexagon::J2_jumprt:
  case Hexagon::J2_jumprf:
  case Hexagon::J2_jumprtnew:
  case Hexagon::J2_jumprfnew:
  case Hexagon::J2_jumprtnewpt:
  case Hexagon::J2_jumprfnewpt:
  case Hexagon::PS_jmprett:
  case Hexagon::PS_jmpretf:
  case Hexagon::PS_jmprettnew:
  case Hexagon::PS_jmpretfnew:
  case Hexagon::PS_jmprettnewpt:
  case Hexagon::PS_jmpretfnewpt:
    DstReg = MCI.getOperand(1).getReg();
    SrcReg = MCI.getOperand(0).getReg();
    // [if ([!]p0[.new])] jumpr r31
    if ((HexagonMCInstrInfo::isPredReg(SrcReg) && (Hexagon::P0 == SrcReg)) &&
        (Hexagon::R31 == DstReg)) {
      return HexagonII::HSIG_L2;
    }
    break;
  case Hexagon::L4_return_t:
  case Hexagon::L4_return_f:
  case Hexagon::L4_return_tnew_pnt:
  case Hexagon::L4_return_fnew_pnt:
  case Hexagon::L4_return_tnew_pt:
  case Hexagon::L4_return_fnew_pt:
    // [if ([!]p0[.new])] dealloc_return
    SrcReg = MCI.getOperand(1).getReg();
    if (Hexagon::P0 == SrcReg) {
      return HexagonII::HSIG_L2;
    }
    break;
  //
  // Group S1:
  //
  // memw(Rs+#u4:2) = Rt
  // memb(Rs+#u4:0) = Rt
  case Hexagon::S2_storeri_io:
    // Special case this one from Group S2.
    // memw(r29+#u5:2) = Rt
    Src1Reg = MCI.getOperand(0).getReg();
    Src2Reg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isIntReg(Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg) &&
        Hexagon::R29 == Src1Reg && inRange<5, 2>(MCI, 1)) {
      return HexagonII::HSIG_S2;
    }
    // memw(Rs+#u4:2) = Rt
    if (HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg) &&
        inRange<4, 2>(MCI, 1)) {
      return HexagonII::HSIG_S1;
    }
    break;
  case Hexagon::S2_storerb_io:
    // memb(Rs+#u4:0) = Rt
    Src1Reg = MCI.getOperand(0).getReg();
    Src2Reg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg) &&
        inRange<4>(MCI, 1)) {
      return HexagonII::HSIG_S1;
    }
    break;
  //
  // Group S2:
  //
  // memh(Rs+#u3:1) = Rt
  // memw(r29+#u5:2) = Rt
  // memd(r29+#s6:3) = Rtt
  // memw(Rs+#u4:2) = #U1
  // memb(Rs+#u4) = #U1
  // allocframe(#u5:3)
  case Hexagon::S2_storerh_io:
    // memh(Rs+#u3:1) = Rt
    Src1Reg = MCI.getOperand(0).getReg();
    Src2Reg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg) &&
        inRange<3, 1>(MCI, 1)) {
      return HexagonII::HSIG_S2;
    }
    break;
  case Hexagon::S2_storerd_io:
    // memd(r29+#s6:3) = Rtt
    Src1Reg = MCI.getOperand(0).getReg();
    Src2Reg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isDblRegForSubInst(Src2Reg) &&
        HexagonMCInstrInfo::isIntReg(Src1Reg) && Hexagon::R29 == Src1Reg &&
        inSRange<6, 3>(MCI, 1)) {
      return HexagonII::HSIG_S2;
    }
    break;
  case Hexagon::S4_storeiri_io:
    // memw(Rs+#u4:2) = #U1
    Src1Reg = MCI.getOperand(0).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        inRange<4, 2>(MCI, 1) && inRange<1>(MCI, 2)) {
      return HexagonII::HSIG_S2;
    }
    break;
  case Hexagon::S4_storeirb_io:
    // memb(Rs+#u4) = #U1
    Src1Reg = MCI.getOperand(0).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        inRange<4>(MCI, 1) && inRange<1>(MCI, 2)) {
      return HexagonII::HSIG_S2;
    }
    break;
  case Hexagon::S2_allocframe:
    if (inRange<5, 3>(MCI, 2))
      return HexagonII::HSIG_S2;
    break;
  //
  // Group A:
  //
  // Rx = add(Rx,#s7)
  // Rd = Rs
  // Rd = #u6
  // Rd = #-1
  // if ([!]P0[.new]) Rd = #0
  // Rd = add(r29,#u6:2)
  // Rx = add(Rx,Rs)
  // P0 = cmp.eq(Rs,#u2)
  // Rdd = combine(#0,Rs)
  // Rdd = combine(Rs,#0)
  // Rdd = combine(#u2,#U2)
  // Rd = add(Rs,#1)
  // Rd = add(Rs,#-1)
  // Rd = sxth/sxtb/zxtb/zxth(Rs)
  // Rd = and(Rs,#1)
  case Hexagon::A2_addi:
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg)) {
      // Rd = add(r29,#u6:2)
      if (HexagonMCInstrInfo::isIntReg(SrcReg) && Hexagon::R29 == SrcReg &&
          inRange<6, 2>(MCI, 2)) {
        return HexagonII::HSIG_A;
      }
      // Rx = add(Rx,#s7)
      if (DstReg == SrcReg) {
        return HexagonII::HSIG_A;
      }
      // Rd = add(Rs,#1)
      // Rd = add(Rs,#-1)
      if (HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
          (minConstant(MCI, 2) == 1 || minConstant(MCI, 2) == -1)) {
        return HexagonII::HSIG_A;
      }
    }
    break;
  case Hexagon::A2_add:
    // Rx = add(Rx,Rs)
    DstReg = MCI.getOperand(0).getReg();
    Src1Reg = MCI.getOperand(1).getReg();
    Src2Reg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) && (DstReg == Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_andir:
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        (minConstant(MCI, 2) == 1 || minConstant(MCI, 2) == 255)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_tfr:
    // Rd = Rs
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_tfrsi:
    DstReg = MCI.getOperand(0).getReg();

    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::C2_cmoveit:
  case Hexagon::C2_cmovenewit:
  case Hexagon::C2_cmoveif:
  case Hexagon::C2_cmovenewif:
    // if ([!]P0[.new]) Rd = #0
    // Actual form:
    // %r16 = C2_cmovenewit internal %p0, 0, implicit undef %r16;
    DstReg = MCI.getOperand(0).getReg();  // Rd
    PredReg = MCI.getOperand(1).getReg(); // P0
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        Hexagon::P0 == PredReg && minConstant(MCI, 2) == 0) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::C2_cmpeqi:
    // P0 = cmp.eq(Rs,#u2)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (Hexagon::P0 == DstReg &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        inRange<2>(MCI, 2)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_combineii:
  case Hexagon::A4_combineii:
    // Rdd = combine(#u2,#U2)
    DstReg = MCI.getOperand(0).getReg();
    if (HexagonMCInstrInfo::isDblRegForSubInst(DstReg) &&
        inRange<2>(MCI, 1) && inRange<2>(MCI, 2)) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A4_combineri:
    // Rdd = combine(Rs,#0)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isDblRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        minConstant(MCI, 2) == 0) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A4_combineir:
    // Rdd = combine(#0,Rs)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(2).getReg();
    if (HexagonMCInstrInfo::isDblRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        minConstant(MCI, 1) == 0) {
      return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_sxtb:
  case Hexagon::A2_sxth:
  case Hexagon::A2_zxtb:
  case Hexagon::A2_zxth:
    // Rd = sxth/sxtb/zxtb/zxth(Rs)
    DstReg = MCI.getOperand(0).getReg();
    SrcReg = MCI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg)) {
      return HexagonII::HSIG_A;
    }
    break;
  }

  return HexagonII::HSIG_None;
}

bool HexagonMCInstrInfo::subInstWouldBeExtended(MCInst const &potentialDuplex) {
  unsigned DstReg, SrcReg;
  switch (potentialDuplex.getOpcode()) {
  case Hexagon::A2_addi:
    // testing for case of: Rx = add(Rx,#s7)
    DstReg = potentialDuplex.getOperand(0).getReg();
    SrcReg = potentialDuplex.getOperand(1).getReg();
    if (DstReg == SrcReg && HexagonMCInstrInfo::isIntRegForSubInst(DstReg)) {
      int64_t Value;
      if (!potentialDuplex.getOperand(2).getExpr()->evaluateAsAbsolute(Value))
        return true;
      if (!isShiftedInt<7, 0>(Value))
        return true;
    }
    break;
  case Hexagon::A2_tfrsi:
    DstReg = potentialDuplex.getOperand(0).getReg();

    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg)) {
      int64_t Value;
      if (!potentialDuplex.getOperand(1).getExpr()->evaluateAsAbsolute(Value))
        return true;
      // Check for case of Rd = #-1.
      if (Value == -1)
        return false;
      // Check for case of Rd = #u6.
      if (!isShiftedUInt<6, 0>(Value))
        return true;
    }
    break;
  default:
    break;
  }
  return false;
}

/// non-Symmetrical. See if these two instructions are fit for duplex pair.
bool HexagonMCInstrInfo::isOrderedDuplexPair(MCInstrInfo const &MCII,
                                             MCInst const &MIa, bool ExtendedA,
                                             MCInst const &MIb, bool ExtendedB,
                                             bool bisReversable,
                                             MCSubtargetInfo const &STI) {
  // Slot 1 cannot be extended in duplexes PRM 10.5
  if (ExtendedA)
    return false;
  // Only A2_addi and A2_tfrsi can be extended in duplex form PRM 10.5
  if (ExtendedB) {
    unsigned Opcode = MIb.getOpcode();
    if ((Opcode != Hexagon::A2_addi) && (Opcode != Hexagon::A2_tfrsi))
      return false;
  }
  unsigned MIaG = HexagonMCInstrInfo::getDuplexCandidateGroup(MIa),
           MIbG = HexagonMCInstrInfo::getDuplexCandidateGroup(MIb);

  static std::map<unsigned, unsigned> subinstOpcodeMap(std::begin(opcodeData),
                                                       std::end(opcodeData));

  // If a duplex contains 2 insns in the same group, the insns must be
  // ordered such that the numerically smaller opcode is in slot 1.
  if ((MIaG != HexagonII::HSIG_None) && (MIaG == MIbG) && bisReversable) {
    MCInst SubInst0 = HexagonMCInstrInfo::deriveSubInst(MIa);
    MCInst SubInst1 = HexagonMCInstrInfo::deriveSubInst(MIb);

    unsigned zeroedSubInstS0 =
        subinstOpcodeMap.find(SubInst0.getOpcode())->second;
    unsigned zeroedSubInstS1 =
        subinstOpcodeMap.find(SubInst1.getOpcode())->second;

    if (zeroedSubInstS0 < zeroedSubInstS1)
      // subinstS0 (maps to slot 0) must be greater than
      // subinstS1 (maps to slot 1)
      return false;
  }

  // allocframe must always be in slot 0
  if (MIb.getOpcode() == Hexagon::S2_allocframe)
    return false;

  if ((MIaG != HexagonII::HSIG_None) && (MIbG != HexagonII::HSIG_None)) {
    // Prevent 2 instructions with extenders from duplexing
    // Note that MIb (slot1) can be extended and MIa (slot0)
    //   can never be extended
    if (subInstWouldBeExtended(MIa))
      return false;

    // If duplexing produces an extender, but the original did not
    //   have an extender, do not duplex.
    if (subInstWouldBeExtended(MIb) && !ExtendedB)
      return false;
  }

  // If jumpr r31 appears, it must be in slot 0, and never slot 1 (MIb).
  if (MIbG == HexagonII::HSIG_L2) {
    if ((MIb.getNumOperands() > 1) && MIb.getOperand(1).isReg() &&
        (MIb.getOperand(1).getReg() == Hexagon::R31))
      return false;
    if ((MIb.getNumOperands() > 0) && MIb.getOperand(0).isReg() &&
        (MIb.getOperand(0).getReg() == Hexagon::R31))
      return false;
  }

  if (STI.getCPU().equals_lower("hexagonv5") ||
      STI.getCPU().equals_lower("hexagonv55") ||
      STI.getCPU().equals_lower("hexagonv60")) {
    // If a store appears, it must be in slot 0 (MIa) 1st, and then slot 1 (MIb);
    //   therefore, not duplexable if slot 1 is a store, and slot 0 is not.
    if ((MIbG == HexagonII::HSIG_S1) || (MIbG == HexagonII::HSIG_S2)) {
      if ((MIaG != HexagonII::HSIG_S1) && (MIaG != HexagonII::HSIG_S2))
        return false;
    }
  }

  return (isDuplexPairMatch(MIaG, MIbG));
}

/// Symmetrical. See if these two instructions are fit for duplex pair.
bool HexagonMCInstrInfo::isDuplexPair(MCInst const &MIa, MCInst const &MIb) {
  unsigned MIaG = getDuplexCandidateGroup(MIa),
           MIbG = getDuplexCandidateGroup(MIb);
  return (isDuplexPairMatch(MIaG, MIbG) || isDuplexPairMatch(MIbG, MIaG));
}

inline static void addOps(MCInst &subInstPtr, MCInst const &Inst,
                          unsigned opNum) {
  if (Inst.getOperand(opNum).isReg()) {
    switch (Inst.getOperand(opNum).getReg()) {
    default:
      llvm_unreachable("Not Duplexable Register");
      break;
    case Hexagon::R0:
    case Hexagon::R1:
    case Hexagon::R2:
    case Hexagon::R3:
    case Hexagon::R4:
    case Hexagon::R5:
    case Hexagon::R6:
    case Hexagon::R7:
    case Hexagon::D0:
    case Hexagon::D1:
    case Hexagon::D2:
    case Hexagon::D3:
    case Hexagon::R16:
    case Hexagon::R17:
    case Hexagon::R18:
    case Hexagon::R19:
    case Hexagon::R20:
    case Hexagon::R21:
    case Hexagon::R22:
    case Hexagon::R23:
    case Hexagon::D8:
    case Hexagon::D9:
    case Hexagon::D10:
    case Hexagon::D11:
    case Hexagon::P0:
      subInstPtr.addOperand(Inst.getOperand(opNum));
      break;
    }
  } else
    subInstPtr.addOperand(Inst.getOperand(opNum));
}

MCInst HexagonMCInstrInfo::deriveSubInst(MCInst const &Inst) {
  MCInst Result;
  bool Absolute;
  int64_t Value;
  switch (Inst.getOpcode()) {
  default:
    // dbgs() << "opcode: "<< Inst->getOpcode() << "\n";
    llvm_unreachable("Unimplemented subinstruction \n");
    break;
  case Hexagon::A2_addi:
    Absolute = Inst.getOperand(2).getExpr()->evaluateAsAbsolute(Value);
    if (Absolute) {
      if (Value == 1) {
        Result.setOpcode(Hexagon::SA1_inc);
        addOps(Result, Inst, 0);
        addOps(Result, Inst, 1);
        break;
      } //  1,2 SUBInst $Rd = add($Rs, #1)
      if (Value == -1) {
        Result.setOpcode(Hexagon::SA1_dec);
        addOps(Result, Inst, 0);
        addOps(Result, Inst, 1);
        addOps(Result, Inst, 2);
        break;
      } //  1,2 SUBInst $Rd = add($Rs,#-1)
      if (Inst.getOperand(1).getReg() == Hexagon::R29) {
        Result.setOpcode(Hexagon::SA1_addsp);
        addOps(Result, Inst, 0);
        addOps(Result, Inst, 2);
        break;
      } //  1,3 SUBInst $Rd = add(r29, #$u6_2)
    }
    Result.setOpcode(Hexagon::SA1_addi);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rx = add($Rx, #$s7)
  case Hexagon::A2_add:
    Result.setOpcode(Hexagon::SA1_addrx);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rx = add($_src_, $Rs)
  case Hexagon::S2_allocframe:
    Result.setOpcode(Hexagon::SS2_allocframe);
    addOps(Result, Inst, 2);
    break; //    1 SUBInst allocframe(#$u5_3)
  case Hexagon::A2_andir:
    if (minConstant(Inst, 2) == 255) {
      Result.setOpcode(Hexagon::SA1_zxtb);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //    1,2    $Rd = and($Rs, #255)
    } else {
      Result.setOpcode(Hexagon::SA1_and1);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //    1,2 SUBInst $Rd = and($Rs, #1)
    }
  case Hexagon::C2_cmpeqi:
    Result.setOpcode(Hexagon::SA1_cmpeqi);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    2,3 SUBInst p0 = cmp.eq($Rs, #$u2)
  case Hexagon::A4_combineii:
  case Hexagon::A2_combineii:
    Absolute = Inst.getOperand(1).getExpr()->evaluateAsAbsolute(Value);
    assert(Absolute);(void)Absolute;
    if (Value == 1) {
      Result.setOpcode(Hexagon::SA1_combine1i);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 2);
      break; //  1,3 SUBInst $Rdd = combine(#1, #$u2)
    }
    if (Value == 3) {
      Result.setOpcode(Hexagon::SA1_combine3i);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 2);
      break; //  1,3 SUBInst $Rdd = combine(#3, #$u2)
    }
    if (Value == 0) {
      Result.setOpcode(Hexagon::SA1_combine0i);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 2);
      break; //  1,3 SUBInst $Rdd = combine(#0, #$u2)
    }
    if (Value == 2) {
      Result.setOpcode(Hexagon::SA1_combine2i);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 2);
      break; //  1,3 SUBInst $Rdd = combine(#2, #$u2)
    }
    break;
  case Hexagon::A4_combineir:
    Result.setOpcode(Hexagon::SA1_combinezr);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 2);
    break; //    1,3 SUBInst $Rdd = combine(#0, $Rs)
  case Hexagon::A4_combineri:
    Result.setOpcode(Hexagon::SA1_combinerz);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //    1,2 SUBInst $Rdd = combine($Rs, #0)
  case Hexagon::L4_return_tnew_pnt:
  case Hexagon::L4_return_tnew_pt:
    Result.setOpcode(Hexagon::SL2_return_tnew);
    break; //    none  SUBInst if (p0.new) dealloc_return:nt
  case Hexagon::L4_return_fnew_pnt:
  case Hexagon::L4_return_fnew_pt:
    Result.setOpcode(Hexagon::SL2_return_fnew);
    break; //    none  SUBInst if (!p0.new) dealloc_return:nt
  case Hexagon::L4_return_f:
    Result.setOpcode(Hexagon::SL2_return_f);
    break; //    none  SUBInst if (!p0) dealloc_return
  case Hexagon::L4_return_t:
    Result.setOpcode(Hexagon::SL2_return_t);
    break; //    none  SUBInst if (p0) dealloc_return
  case Hexagon::L4_return:
    Result.setOpcode(Hexagon::SL2_return);
    break; //    none  SUBInst dealloc_return
  case Hexagon::L2_deallocframe:
    Result.setOpcode(Hexagon::SL2_deallocframe);
    break; //    none  SUBInst deallocframe
  case Hexagon::EH_RETURN_JMPR:
  case Hexagon::J2_jumpr:
  case Hexagon::PS_jmpret:
    Result.setOpcode(Hexagon::SL2_jumpr31);
    break; //    none  SUBInst jumpr r31
  case Hexagon::J2_jumprf:
  case Hexagon::PS_jmpretf:
    Result.setOpcode(Hexagon::SL2_jumpr31_f);
    break; //    none  SUBInst if (!p0) jumpr r31
  case Hexagon::J2_jumprfnew:
  case Hexagon::J2_jumprfnewpt:
  case Hexagon::PS_jmpretfnewpt:
  case Hexagon::PS_jmpretfnew:
    Result.setOpcode(Hexagon::SL2_jumpr31_fnew);
    break; //    none  SUBInst if (!p0.new) jumpr:nt r31
  case Hexagon::J2_jumprt:
  case Hexagon::PS_jmprett:
    Result.setOpcode(Hexagon::SL2_jumpr31_t);
    break; //    none  SUBInst if (p0) jumpr r31
  case Hexagon::J2_jumprtnew:
  case Hexagon::J2_jumprtnewpt:
  case Hexagon::PS_jmprettnewpt:
  case Hexagon::PS_jmprettnew:
    Result.setOpcode(Hexagon::SL2_jumpr31_tnew);
    break; //    none  SUBInst if (p0.new) jumpr:nt r31
  case Hexagon::L2_loadrb_io:
    Result.setOpcode(Hexagon::SL2_loadrb_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rd = memb($Rs + #$u3_0)
  case Hexagon::L2_loadrd_io:
    Result.setOpcode(Hexagon::SL2_loadrd_sp);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 2);
    break; //    1,3 SUBInst $Rdd = memd(r29 + #$u5_3)
  case Hexagon::L2_loadrh_io:
    Result.setOpcode(Hexagon::SL2_loadrh_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rd = memh($Rs + #$u3_1)
  case Hexagon::L2_loadrub_io:
    Result.setOpcode(Hexagon::SL1_loadrub_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rd = memub($Rs + #$u4_0)
  case Hexagon::L2_loadruh_io:
    Result.setOpcode(Hexagon::SL2_loadruh_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst $Rd = memuh($Rs + #$u3_1)
  case Hexagon::L2_loadri_io:
    if (Inst.getOperand(1).getReg() == Hexagon::R29) {
      Result.setOpcode(Hexagon::SL2_loadri_sp);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 2);
      break; //  2 1,3 SUBInst $Rd = memw(r29 + #$u5_2)
    } else {
      Result.setOpcode(Hexagon::SL1_loadri_io);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      addOps(Result, Inst, 2);
      break; //    1,2,3 SUBInst $Rd = memw($Rs + #$u4_2)
    }
  case Hexagon::S4_storeirb_io:
    Absolute = Inst.getOperand(2).getExpr()->evaluateAsAbsolute(Value);
    assert(Absolute);(void)Absolute;
    if (Value == 0) {
      Result.setOpcode(Hexagon::SS2_storebi0);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //    1,2 SUBInst memb($Rs + #$u4_0)=#0
    } else if (Value == 1) {
      Result.setOpcode(Hexagon::SS2_storebi1);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //  2 1,2 SUBInst memb($Rs + #$u4_0)=#1
    }
    break;
  case Hexagon::S2_storerb_io:
    Result.setOpcode(Hexagon::SS1_storeb_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst memb($Rs + #$u4_0) = $Rt
  case Hexagon::S2_storerd_io:
    Result.setOpcode(Hexagon::SS2_stored_sp);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    2,3 SUBInst memd(r29 + #$s6_3) = $Rtt
  case Hexagon::S2_storerh_io:
    Result.setOpcode(Hexagon::SS2_storeh_io);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    addOps(Result, Inst, 2);
    break; //    1,2,3 SUBInst memb($Rs + #$u4_0) = $Rt
  case Hexagon::S4_storeiri_io:
    Absolute = Inst.getOperand(2).getExpr()->evaluateAsAbsolute(Value);
    assert(Absolute);(void)Absolute;
    if (Value == 0) {
      Result.setOpcode(Hexagon::SS2_storewi0);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //  3 1,2 SUBInst memw($Rs + #$u4_2)=#0
    } else if (Value == 1) {
      Result.setOpcode(Hexagon::SS2_storewi1);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //  3 1,2 SUBInst memw($Rs + #$u4_2)=#1
    } else if (Inst.getOperand(0).getReg() == Hexagon::R29) {
      Result.setOpcode(Hexagon::SS2_storew_sp);
      addOps(Result, Inst, 1);
      addOps(Result, Inst, 2);
      break; //  1 2,3 SUBInst memw(r29 + #$u5_2) = $Rt
    }
    break;
  case Hexagon::S2_storeri_io:
    if (Inst.getOperand(0).getReg() == Hexagon::R29) {
      Result.setOpcode(Hexagon::SS2_storew_sp);
      addOps(Result, Inst, 1);
      addOps(Result, Inst, 2); //  1,2,3 SUBInst memw(sp + #$u5_2) = $Rt
    } else {
      Result.setOpcode(Hexagon::SS1_storew_io);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      addOps(Result, Inst, 2); //  1,2,3 SUBInst memw($Rs + #$u4_2) = $Rt
    }
    break;
  case Hexagon::A2_sxtb:
    Result.setOpcode(Hexagon::SA1_sxtb);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  1,2 SUBInst $Rd = sxtb($Rs)
  case Hexagon::A2_sxth:
    Result.setOpcode(Hexagon::SA1_sxth);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  1,2 SUBInst $Rd = sxth($Rs)
  case Hexagon::A2_tfr:
    Result.setOpcode(Hexagon::SA1_tfr);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  1,2 SUBInst $Rd = $Rs
  case Hexagon::C2_cmovenewif:
    Result.setOpcode(Hexagon::SA1_clrfnew);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  2 SUBInst if (!p0.new) $Rd = #0
  case Hexagon::C2_cmovenewit:
    Result.setOpcode(Hexagon::SA1_clrtnew);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  2 SUBInst if (p0.new) $Rd = #0
  case Hexagon::C2_cmoveif:
    Result.setOpcode(Hexagon::SA1_clrf);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  2 SUBInst if (!p0) $Rd = #0
  case Hexagon::C2_cmoveit:
    Result.setOpcode(Hexagon::SA1_clrt);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //  2 SUBInst if (p0) $Rd = #0
  case Hexagon::A2_tfrsi:
    Absolute = Inst.getOperand(1).getExpr()->evaluateAsAbsolute(Value);
    if (Absolute && Value == -1) {
      Result.setOpcode(Hexagon::SA1_setin1);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //  2 1 SUBInst $Rd = #-1
    } else {
      Result.setOpcode(Hexagon::SA1_seti);
      addOps(Result, Inst, 0);
      addOps(Result, Inst, 1);
      break; //    1,2 SUBInst $Rd = #$u6
    }
  case Hexagon::A2_zxtb:
    Result.setOpcode(Hexagon::SA1_zxtb);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //    1,2    $Rd = and($Rs, #255)

  case Hexagon::A2_zxth:
    Result.setOpcode(Hexagon::SA1_zxth);
    addOps(Result, Inst, 0);
    addOps(Result, Inst, 1);
    break; //    1,2 SUBInst $Rd = zxth($Rs)
  }
  return Result;
}

static bool isStoreInst(unsigned opCode) {
  switch (opCode) {
  case Hexagon::S2_storeri_io:
  case Hexagon::S2_storerb_io:
  case Hexagon::S2_storerh_io:
  case Hexagon::S2_storerd_io:
  case Hexagon::S4_storeiri_io:
  case Hexagon::S4_storeirb_io:
  case Hexagon::S2_allocframe:
    return true;
  default:
    return false;
  }
}

SmallVector<DuplexCandidate, 8>
HexagonMCInstrInfo::getDuplexPossibilties(MCInstrInfo const &MCII,
                                          MCSubtargetInfo const &STI,
                                          MCInst const &MCB) {
  assert(isBundle(MCB));
  SmallVector<DuplexCandidate, 8> duplexToTry;
  // Use an "order matters" version of isDuplexPair.
  unsigned numInstrInPacket = MCB.getNumOperands();

  for (unsigned distance = 1; distance < numInstrInPacket; ++distance) {
    for (unsigned j = HexagonMCInstrInfo::bundleInstructionsOffset,
                  k = j + distance;
         (j < numInstrInPacket) && (k < numInstrInPacket); ++j, ++k) {

      // Check if reversible.
      bool bisReversable = true;
      if (isStoreInst(MCB.getOperand(j).getInst()->getOpcode()) &&
          isStoreInst(MCB.getOperand(k).getInst()->getOpcode())) {
        LLVM_DEBUG(dbgs() << "skip out of order write pair: " << k << "," << j
                          << "\n");
        bisReversable = false;
      }
      if (HexagonMCInstrInfo::isMemReorderDisabled(MCB)) // }:mem_noshuf
        bisReversable = false;

      // Try in order.
      if (isOrderedDuplexPair(
              MCII, *MCB.getOperand(k).getInst(),
              HexagonMCInstrInfo::hasExtenderForIndex(MCB, k - 1),
              *MCB.getOperand(j).getInst(),
              HexagonMCInstrInfo::hasExtenderForIndex(MCB, j - 1),
              bisReversable, STI)) {
        // Get iClass.
        unsigned iClass = iClassOfDuplexPair(
            getDuplexCandidateGroup(*MCB.getOperand(k).getInst()),
            getDuplexCandidateGroup(*MCB.getOperand(j).getInst()));

        // Save off pairs for duplex checking.
        duplexToTry.push_back(DuplexCandidate(j, k, iClass));
        LLVM_DEBUG(dbgs() << "adding pair: " << j << "," << k << ":"
                          << MCB.getOperand(j).getInst()->getOpcode() << ","
                          << MCB.getOperand(k).getInst()->getOpcode() << "\n");
        continue;
      } else {
        LLVM_DEBUG(dbgs() << "skipping pair: " << j << "," << k << ":"
                          << MCB.getOperand(j).getInst()->getOpcode() << ","
                          << MCB.getOperand(k).getInst()->getOpcode() << "\n");
      }

      // Try reverse.
      if (bisReversable) {
        if (isOrderedDuplexPair(
                MCII, *MCB.getOperand(j).getInst(),
                HexagonMCInstrInfo::hasExtenderForIndex(MCB, j - 1),
                *MCB.getOperand(k).getInst(),
                HexagonMCInstrInfo::hasExtenderForIndex(MCB, k - 1),
                bisReversable, STI)) {
          // Get iClass.
          unsigned iClass = iClassOfDuplexPair(
              getDuplexCandidateGroup(*MCB.getOperand(j).getInst()),
              getDuplexCandidateGroup(*MCB.getOperand(k).getInst()));

          // Save off pairs for duplex checking.
          duplexToTry.push_back(DuplexCandidate(k, j, iClass));
          LLVM_DEBUG(dbgs()
                     << "adding pair:" << k << "," << j << ":"
                     << MCB.getOperand(j).getInst()->getOpcode() << ","
                     << MCB.getOperand(k).getInst()->getOpcode() << "\n");
        } else {
          LLVM_DEBUG(dbgs()
                     << "skipping pair: " << k << "," << j << ":"
                     << MCB.getOperand(j).getInst()->getOpcode() << ","
                     << MCB.getOperand(k).getInst()->getOpcode() << "\n");
        }
      }
    }
  }
  return duplexToTry;
}
