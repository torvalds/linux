//===--- MicroMipsInstrFormats.td - microMIPS Inst Defs -*- tablegen -*----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files describes the defintions of the microMIPSr3 instructions.
//
//===----------------------------------------------------------------------===//

def addrimm11 : ComplexPattern<iPTR, 2, "selectIntAddr11MM", [frameindex]>;
def addrimm12 : ComplexPattern<iPTR, 2, "selectIntAddr12MM", [frameindex]>;
def addrimm16 : ComplexPattern<iPTR, 2, "selectIntAddr16MM", [frameindex]>;
def addrimm4lsl2 : ComplexPattern<iPTR, 2, "selectIntAddrLSL2MM", [frameindex]>;

def simm9_addiusp : Operand<i32> {
  let EncoderMethod = "getSImm9AddiuspValue";
  let DecoderMethod = "DecodeSimm9SP";
}

def uimm3_shift : Operand<i32> {
  let EncoderMethod = "getUImm3Mod8Encoding";
  let DecoderMethod = "DecodePOOL16BEncodedField";
}

def simm3_lsa2 : Operand<i32> {
  let EncoderMethod = "getSImm3Lsa2Value";
  let DecoderMethod = "DecodeAddiur2Simm7";
}

def uimm4_andi : Operand<i32> {
  let EncoderMethod = "getUImm4AndValue";
  let DecoderMethod = "DecodeANDI16Imm";
}

def immSExtAddiur2 : ImmLeaf<i32, [{return Imm == 1 || Imm == -1 ||
                                           ((Imm % 4 == 0) &&
                                            Imm < 28 && Imm > 0);}]>;

def immSExtAddius5 : ImmLeaf<i32, [{return Imm >= -8 && Imm <= 7;}]>;

def immZExtAndi16 : ImmLeaf<i32,
  [{return (Imm == 128 || (Imm >= 1 && Imm <= 4) || Imm == 7 || Imm == 8 ||
            Imm == 15 || Imm == 16 || Imm == 31 || Imm == 32 || Imm == 63 ||
            Imm == 64 || Imm == 255 || Imm == 32768 || Imm == 65535 );}]>;

def immZExt2Shift : ImmLeaf<i32, [{return Imm >= 1 && Imm <= 8;}]>;

def immLi16 : ImmLeaf<i32, [{return Imm >= -1 && Imm <= 126;}]>;

def MicroMipsMemGPRMM16AsmOperand : AsmOperandClass {
  let Name = "MicroMipsMem";
  let RenderMethod = "addMicroMipsMemOperands";
  let ParserMethod = "parseMemOperand";
  let PredicateMethod = "isMemWithGRPMM16Base";
}

// Define the classes of pointers used by microMIPS.
// The numbers must match those in MipsRegisterInfo::MipsPtrClass.
def ptr_gpr16mm_rc : PointerLikeRegClass<1>;
def ptr_sp_rc : PointerLikeRegClass<2>;
def ptr_gp_rc : PointerLikeRegClass<3>;

class mem_mm_4_generic : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_gpr16mm_rc, simm4);
  let OperandType = "OPERAND_MEMORY";
  let ParserMatchClass = MicroMipsMemGPRMM16AsmOperand;
}

def mem_mm_4 : mem_mm_4_generic {
  let EncoderMethod = "getMemEncodingMMImm4";
}

def mem_mm_4_lsl1 : mem_mm_4_generic {
  let EncoderMethod = "getMemEncodingMMImm4Lsl1";
}

def mem_mm_4_lsl2 : mem_mm_4_generic {
  let EncoderMethod = "getMemEncodingMMImm4Lsl2";
}

def MicroMipsMemSPAsmOperand : AsmOperandClass {
  let Name = "MicroMipsMemSP";
  let RenderMethod = "addMemOperands";
  let ParserMethod = "parseMemOperand";
  let PredicateMethod = "isMemWithUimmWordAlignedOffsetSP<7>";
}

def MicroMipsMemGPAsmOperand : AsmOperandClass {
  let Name = "MicroMipsMemGP";
  let RenderMethod = "addMemOperands";
  let ParserMethod = "parseMemOperand";
  let PredicateMethod = "isMemWithSimmWordAlignedOffsetGP<9>";
}

def mem_mm_sp_imm5_lsl2 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_sp_rc:$base, simm5:$offset);
  let OperandType = "OPERAND_MEMORY";
  let ParserMatchClass = MicroMipsMemSPAsmOperand;
  let EncoderMethod = "getMemEncodingMMSPImm5Lsl2";
}

def mem_mm_gp_simm7_lsl2 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_gp_rc:$base, simm7_lsl2:$offset);
  let OperandType = "OPERAND_MEMORY";
  let ParserMatchClass = MicroMipsMemGPAsmOperand;
  let EncoderMethod = "getMemEncodingMMGPImm7Lsl2";
}

def mem_mm_9 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_rc, simm9);
  let EncoderMethod = "getMemEncodingMMImm9";
  let ParserMatchClass = MipsMemSimm9AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

def mem_mm_11 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops GPR32, simm11);
  let EncoderMethod = "getMemEncodingMMImm11";
  let ParserMatchClass = MipsMemSimm11AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

def mem_mm_12 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_rc, simm12);
  let EncoderMethod = "getMemEncodingMMImm12";
  let ParserMatchClass = MipsMemAsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

def mem_mm_16 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_rc, simm16);
  let EncoderMethod = "getMemEncodingMMImm16";
  let DecoderMethod = "DecodeMemMMImm16";
  let ParserMatchClass = MipsMemSimm16AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

def MipsMemUimm4AsmOperand : AsmOperandClass {
  let Name = "MemOffsetUimm4";
  let SuperClasses = [MipsMemAsmOperand];
  let RenderMethod = "addMemOperands";
  let ParserMethod = "parseMemOperand";
  let PredicateMethod = "isMemWithUimmOffsetSP<6>";
}

def mem_mm_4sp : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops ptr_sp_rc, uimm8);
  let EncoderMethod = "getMemEncodingMMImm4sp";
  let ParserMatchClass = MipsMemUimm4AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

def jmptarget_mm : Operand<OtherVT> {
  let EncoderMethod = "getJumpTargetOpValueMM";
}

def calltarget_mm : Operand<iPTR> {
  let EncoderMethod = "getJumpTargetOpValueMM";
}

def brtarget7_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget7OpValueMM";
  let OperandType   = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget7MM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def brtarget10_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValueMMPC10";
  let OperandType   = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget10MM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def brtarget_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValueMM";
  let OperandType   = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTargetMM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def simm23_lsl2 : Operand<i32> {
  let EncoderMethod = "getSimm23Lsl2Encoding";
  let DecoderMethod = "DecodeSimm23Lsl2";
}

class CompactBranchMM<string opstr, DAGOperand opnd, PatFrag cond_op,
                      RegisterOperand RO> :
  InstSE<(outs), (ins RO:$rs, opnd:$offset),
         !strconcat(opstr, "\t$rs, $offset"), [], II_BCCZC, FrmI> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 0;
  let Defs = [AT];
}

let canFoldAsLoad = 1 in
class LoadLeftRightMM<string opstr, SDNode OpNode, RegisterOperand RO,
                      Operand MemOpnd, InstrItinClass Itin> :
  InstSE<(outs RO:$rt), (ins MemOpnd:$addr, RO:$src),
         !strconcat(opstr, "\t$rt, $addr"),
         [(set RO:$rt, (OpNode addrimm12:$addr, RO:$src))],
         Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMImm12";
  string Constraints = "$src = $rt";
  let BaseOpcode = opstr;
  bit mayLoad = 1;
  bit mayStore = 0;
}

class StoreLeftRightMM<string opstr, SDNode OpNode, RegisterOperand RO,
                       Operand MemOpnd, InstrItinClass Itin>:
  InstSE<(outs), (ins RO:$rt, MemOpnd:$addr),
         !strconcat(opstr, "\t$rt, $addr"),
         [(OpNode RO:$rt, addrimm12:$addr)], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMImm12";
  let BaseOpcode = opstr;
  bit mayLoad = 0;
  bit mayStore = 1;
}

class MovePMM16<string opstr, RegisterOperand RO1, RegisterOperand RO2,
                RegisterOperand RO3> :
MicroMipsInst16<(outs RO1:$rd1, RO2:$rd2), (ins RO3:$rs, RO3:$rt),
                 !strconcat(opstr, "\t$rd1, $rd2, $rs, $rt"), [],
                 NoItinerary, FrmR> {
  let isReMaterializable = 1;
  let isMoveReg = 1;
  let DecoderMethod = "DecodeMovePOperands";
}

class StorePairMM<string opstr, ComplexPattern Addr = addr>
    :  InstSE<(outs), (ins GPR32Opnd:$rt, GPR32Opnd:$rt2, mem_simm12:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], II_SWP, FrmI, opstr> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayStore = 1;
  let AsmMatchConverter = "ConvertXWPOperands";
}

class LoadPairMM<string opstr, ComplexPattern Addr = addr>
    : InstSE<(outs GPR32Opnd:$rt, GPR32Opnd:$rt2), (ins mem_simm12:$addr),
          !strconcat(opstr, "\t$rt, $addr"), [], II_LWP, FrmI, opstr> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayLoad = 1;
  let AsmMatchConverter = "ConvertXWPOperands";
}

class LLBaseMM<string opstr, RegisterOperand RO> :
  InstSE<(outs RO:$rt), (ins mem_mm_12:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], II_LL, FrmI> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayLoad = 1;
}

class LLEBaseMM<string opstr, RegisterOperand RO> :
  InstSE<(outs RO:$rt), (ins mem_simm9:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], II_LLE, FrmI> {
  let DecoderMethod = "DecodeMemMMImm9";
  string BaseOpcode = opstr;
  let mayLoad = 1;
}

class SCBaseMM<string opstr, RegisterOperand RO> :
  InstSE<(outs RO:$dst), (ins RO:$rt, mem_mm_12:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], II_SC, FrmI> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayStore = 1;
  let Constraints = "$rt = $dst";
}

class SCEBaseMM<string opstr, RegisterOperand RO> :
  InstSE<(outs RO:$dst), (ins RO:$rt, mem_simm9:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], II_SCE, FrmI> {
  let DecoderMethod = "DecodeMemMMImm9";
  string BaseOpcode = opstr;
  let mayStore = 1;
  let Constraints = "$rt = $dst";
}

class LoadMM<string opstr, DAGOperand RO, SDPatternOperator OpNode = null_frag,
             InstrItinClass Itin = NoItinerary, DAGOperand MO = mem_mm_12> :
  InstSE<(outs RO:$rt), (ins MO:$addr),
         !strconcat(opstr, "\t$rt, $addr"),
         [(set RO:$rt, (OpNode addrimm12:$addr))], Itin, FrmI, opstr> {
  let DecoderMethod = "DecodeMemMMImm12";
  let canFoldAsLoad = 1;
  let mayLoad = 1;
}

class ArithRMM16<string opstr, RegisterOperand RO, bit isComm = 0,
                 InstrItinClass Itin = NoItinerary,
                 SDPatternOperator OpNode = null_frag> :
  MicroMipsInst16<(outs RO:$rd), (ins RO:$rs, RO:$rt),
                  !strconcat(opstr, "\t$rd, $rs, $rt"),
                  [(set RO:$rd, (OpNode RO:$rs, RO:$rt))], Itin, FrmR> {
  let isCommutable = isComm;
}

class AndImmMM16<string opstr, RegisterOperand RO,
                 InstrItinClass Itin = NoItinerary> :
  MicroMipsInst16<(outs RO:$rd), (ins RO:$rs, uimm4_andi:$imm),
                  !strconcat(opstr, "\t$rd, $rs, $imm"), [], Itin, FrmI>;

class LogicRMM16<string opstr, RegisterOperand RO,
                 InstrItinClass Itin = NoItinerary,
                 SDPatternOperator OpNode = null_frag> :
  MicroMipsInst16<(outs RO:$dst), (ins RO:$rs, RO:$rt),
         !strconcat(opstr, "\t$rt, $rs"),
         [(set RO:$dst, (OpNode RO:$rs, RO:$rt))], Itin, FrmR> {
  let isCommutable = 1;
  let Constraints = "$rt = $dst";
}

class NotMM16<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs RO:$rt), (ins RO:$rs),
         !strconcat(opstr, "\t$rt, $rs"),
         [(set RO:$rt, (not RO:$rs))], II_NOT, FrmR>;

class ShiftIMM16<string opstr, Operand ImmOpnd, RegisterOperand RO,
                 InstrItinClass Itin = NoItinerary> :
  MicroMipsInst16<(outs RO:$rd), (ins RO:$rt, ImmOpnd:$shamt),
                  !strconcat(opstr, "\t$rd, $rt, $shamt"), [], Itin, FrmR>;

class LoadMM16<string opstr, DAGOperand RO, SDPatternOperator OpNode,
               InstrItinClass Itin, Operand MemOpnd> :
  MicroMipsInst16<(outs RO:$rt), (ins MemOpnd:$addr),
                  !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMImm4";
  let canFoldAsLoad = 1;
  let mayLoad = 1;
}

class StoreMM16<string opstr, DAGOperand RTOpnd, DAGOperand RO,
                SDPatternOperator OpNode, InstrItinClass Itin,
                Operand MemOpnd> :
  MicroMipsInst16<(outs), (ins RTOpnd:$rt, MemOpnd:$addr),
                  !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMImm4";
  let mayStore = 1;
}

class LoadSPMM16<string opstr, DAGOperand RO, InstrItinClass Itin,
                 Operand MemOpnd> :
  MicroMipsInst16<(outs RO:$rt), (ins MemOpnd:$offset),
                  !strconcat(opstr, "\t$rt, $offset"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMSPImm5Lsl2";
  let canFoldAsLoad = 1;
  let mayLoad = 1;
}

class StoreSPMM16<string opstr, DAGOperand RO, InstrItinClass Itin,
                  Operand MemOpnd> :
  MicroMipsInst16<(outs), (ins RO:$rt, MemOpnd:$offset),
                  !strconcat(opstr, "\t$rt, $offset"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMSPImm5Lsl2";
  let mayStore = 1;
}

class LoadGPMM16<string opstr, DAGOperand RO, InstrItinClass Itin,
                 Operand MemOpnd> :
  MicroMipsInst16<(outs RO:$rt), (ins MemOpnd:$offset),
                  !strconcat(opstr, "\t$rt, $offset"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMGPImm7Lsl2";
  let canFoldAsLoad = 1;
  let mayLoad = 1;
}

class AddImmUR2<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs RO:$rd), (ins RO:$rs, simm3_lsa2:$imm),
                  !strconcat(opstr, "\t$rd, $rs, $imm"),
                  [], II_ADDIU, FrmR> {
  let isCommutable = 1;
}

class AddImmUS5<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs RO:$dst), (ins RO:$rd, simm4:$imm),
                  !strconcat(opstr, "\t$rd, $imm"), [], II_ADDIU, FrmR> {
  let Constraints = "$rd = $dst";
}

class AddImmUR1SP<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs RO:$rd), (ins uimm6_lsl2:$imm),
                  !strconcat(opstr, "\t$rd, $imm"), [], II_ADDIU, FrmR>;

class AddImmUSP<string opstr> :
  MicroMipsInst16<(outs), (ins simm9_addiusp:$imm),
                  !strconcat(opstr, "\t$imm"), [], II_ADDIU, FrmI>;

class MoveFromHILOMM<string opstr, RegisterOperand RO, Register UseReg> :
      MicroMipsInst16<(outs RO:$rd), (ins), !strconcat(opstr, "\t$rd"),
  [], II_MFHI_MFLO, FrmR> {
  let Uses = [UseReg];
  let hasSideEffects = 0;
  let isMoveReg = 1;
}

class MoveMM16<string opstr, RegisterOperand RO>
    :  MicroMipsInst16<(outs RO:$rd), (ins RO:$rs),
                       !strconcat(opstr, "\t$rd, $rs"), [], II_MOVE, FrmR> {
  let isReMaterializable = 1;
  let isMoveReg = 1;
}

class LoadImmMM16<string opstr, Operand Od, RegisterOperand RO> :
  MicroMipsInst16<(outs RO:$rd), (ins Od:$imm),
                  !strconcat(opstr, "\t$rd, $imm"), [], II_LI, FrmI> {
  let isReMaterializable = 1;
}

// 16-bit Jump and Link (Call)
class JumpLinkRegMM16<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
           [(MipsJmpLink RO:$rs)], II_JALR, FrmR> {
  let isCall = 1;
  let hasDelaySlot = 1;
  let Defs = [RA];
  let hasPostISelHook = 1;
}

// 16-bit Jump Reg
class JumpRegMM16<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
           [], II_JR, FrmR> {
  let hasDelaySlot = 1;
  let isBranch = 1;
  let isIndirectBranch = 1;
}

// Base class for JRADDIUSP instruction.
class JumpRAddiuStackMM16 :
  MicroMipsInst16<(outs), (ins uimm5_lsl2:$imm), "jraddiusp\t$imm",
                  [], II_JRADDIUSP, FrmR> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isBranch = 1;
  let isIndirectBranch = 1;
}

// 16-bit Jump and Link (Call) - Short Delay Slot
class JumpLinkRegSMM16<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
           [], II_JALRS, FrmR> {
  let isCall = 1;
  let hasDelaySlot = 1;
  let Defs = [RA];
}

// 16-bit Jump Register Compact - No delay slot
class JumpRegCMM16<string opstr, RegisterOperand RO> :
  MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
                  [], II_JRC, FrmR> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isBranch = 1;
  let isIndirectBranch = 1;
}

// Break16 and Sdbbp16
class BrkSdbbp16MM<string opstr, InstrItinClass Itin> :
  MicroMipsInst16<(outs), (ins uimm4:$code_),
                  !strconcat(opstr, "\t$code_"),
                  [], Itin, FrmOther>;

class CBranchZeroMM<string opstr, DAGOperand opnd, RegisterOperand RO> :
  MicroMipsInst16<(outs), (ins RO:$rs, opnd:$offset),
                  !strconcat(opstr, "\t$rs, $offset"), [], II_BCCZ, FrmI> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 1;
  let Defs = [AT];
}

// MicroMIPS Jump and Link (Call) - Short Delay Slot
let isCall = 1, hasDelaySlot = 1, Defs = [RA] in {
  class JumpLinkMM<string opstr, DAGOperand opnd> :
    InstSE<(outs), (ins opnd:$target), !strconcat(opstr, "\t$target"),
           [], II_JALS, FrmJ, opstr> {
    let DecoderMethod = "DecodeJumpTargetMM";
  }

  class JumpLinkRegMM<string opstr, RegisterOperand RO>:
    InstSE<(outs RO:$rd), (ins RO:$rs), !strconcat(opstr, "\t$rd, $rs"),
            [], II_JALRS, FrmR>;

  class BranchCompareToZeroLinkMM<string opstr, DAGOperand opnd,
                                  RegisterOperand RO> :
    InstSE<(outs), (ins RO:$rs, opnd:$offset),
           !strconcat(opstr, "\t$rs, $offset"), [], II_BCCZALS, FrmI, opstr>;
}

class LoadWordIndexedScaledMM<string opstr, RegisterOperand RO,
                              SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RO:$rd), (ins PtrRC:$base, PtrRC:$index),
         !strconcat(opstr, "\t$rd, ${index}(${base})"), [], II_LWXS, FrmFI>;

class PrefetchIndexed<string opstr> :
  InstSE<(outs), (ins PtrRC:$base, PtrRC:$index, uimm5:$hint),
         !strconcat(opstr, "\t$hint, ${index}(${base})"), [], II_PREF, FrmOther>;

class AddImmUPC<string opstr, RegisterOperand RO> :
  InstSE<(outs RO:$rs), (ins simm23_lsl2:$imm),
         !strconcat(opstr, "\t$rs, $imm"), [], II_ADDIU, FrmR>;

/// A list of registers used by load/store multiple instructions.
def RegListAsmOperand : AsmOperandClass {
  let Name = "RegList";
  let ParserMethod = "parseRegisterList";
}

def reglist : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = RegListAsmOperand;
  let PrintMethod = "printRegisterList";
  let DecoderMethod = "DecodeRegListOperand";
}

def RegList16AsmOperand : AsmOperandClass {
  let Name = "RegList16";
  let ParserMethod = "parseRegisterList";
  let PredicateMethod = "isRegList16";
  let RenderMethod = "addRegListOperands";
}

def reglist16 : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue16";
  let DecoderMethod = "DecodeRegListOperand16";
  let PrintMethod = "printRegisterList";
  let ParserMatchClass = RegList16AsmOperand;
}

class StoreMultMM<string opstr,
            InstrItinClass Itin = NoItinerary, ComplexPattern Addr = addr> :
  InstSE<(outs), (ins reglist:$rt, mem_mm_12:$addr),
         !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI, opstr> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayStore = 1;
}

class LoadMultMM<string opstr,
            InstrItinClass Itin = NoItinerary, ComplexPattern Addr = addr> :
  InstSE<(outs reglist:$rt), (ins mem_mm_12:$addr),
          !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI, opstr> {
  let DecoderMethod = "DecodeMemMMImm12";
  let mayLoad = 1;
}

class StoreMultMM16<string opstr,
                    InstrItinClass Itin = NoItinerary,
                    ComplexPattern Addr = addr> :
  MicroMipsInst16<(outs), (ins reglist16:$rt, mem_mm_4sp:$addr),
                  !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMReglistImm4Lsl2";
  let mayStore = 1;
}

class LoadMultMM16<string opstr,
                   InstrItinClass Itin = NoItinerary,
                   ComplexPattern Addr = addr> :
  MicroMipsInst16<(outs reglist16:$rt), (ins mem_mm_4sp:$addr),
                  !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI> {
  let DecoderMethod = "DecodeMemMMReglistImm4Lsl2";
  let mayLoad = 1;
}

class UncondBranchMM16<string opstr> :
  MicroMipsInst16<(outs), (ins brtarget10_mm:$offset),
                  !strconcat(opstr, "\t$offset"),
                  [], II_B, FrmI> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let hasDelaySlot = 1;
  let Predicates = [RelocPIC, InMicroMips];
  let Defs = [AT];
}

class HypcallMM<string opstr> :
  InstSE<(outs), (ins uimm10:$code_),
          !strconcat(opstr, "\t$code_"), [], II_HYPCALL, FrmOther> {
  let BaseOpcode = opstr;
}

class TLBINVMM<string opstr, InstrItinClass Itin> :
  InstSE<(outs), (ins), opstr, [], Itin, FrmOther> {
  let BaseOpcode = opstr;
}

class MfCop0MM<string opstr, RegisterOperand DstRC,
               RegisterOperand SrcRC, InstrItinClass Itin> :
  InstSE<(outs DstRC:$rt), (ins SrcRC:$rs, uimm3:$sel),
          !strconcat(opstr, "\t$rt, $rs, $sel"), [], Itin, FrmR> {
  let BaseOpcode = opstr;
}

class MtCop0MM<string opstr, RegisterOperand DstRC,
               RegisterOperand SrcRC, InstrItinClass Itin> :
  InstSE<(outs DstRC:$rs), (ins SrcRC:$rt, uimm3:$sel),
          !strconcat(opstr, "\t$rt, $rs, $sel"), [], Itin, FrmR> {
  let BaseOpcode = opstr;
}

let FastISelShouldIgnore = 1 in {
  def ADDU16_MM : ArithRMM16<"addu16", GPRMM16Opnd, 1, II_ADDU, add>,
      ARITH_FM_MM16<0>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def AND16_MM : LogicRMM16<"and16", GPRMM16Opnd, II_AND, and>,
      LOGIC_FM_MM16<0x2>, ISA_MICROMIPS32_NOT_MIPS32R6;
}

def ANDI16_MM : AndImmMM16<"andi16", GPRMM16Opnd, II_AND>, ANDI_FM_MM16<0x0b>,
                ISA_MICROMIPS32_NOT_MIPS32R6;
def NOT16_MM : NotMM16<"not16", GPRMM16Opnd>, LOGIC_FM_MM16<0x0>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
let FastISelShouldIgnore = 1 in
  def OR16_MM : LogicRMM16<"or16", GPRMM16Opnd, II_OR, or>, LOGIC_FM_MM16<0x3>,
                ISA_MICROMIPS32_NOT_MIPS32R6;
def SLL16_MM : ShiftIMM16<"sll16", uimm3_shift, GPRMM16Opnd, II_SLL>,
    SHIFT_FM_MM16<0>, ISA_MICROMIPS32_NOT_MIPS32R6;
def SRL16_MM : ShiftIMM16<"srl16", uimm3_shift, GPRMM16Opnd, II_SRL>,
    SHIFT_FM_MM16<1>, ISA_MICROMIPS32_NOT_MIPS32R6;

let FastISelShouldIgnore = 1 in {
  def SUBU16_MM : ArithRMM16<"subu16", GPRMM16Opnd, 0, II_SUBU, sub>,
                  ARITH_FM_MM16<1>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def XOR16_MM : LogicRMM16<"xor16", GPRMM16Opnd, II_XOR, xor>,
                 LOGIC_FM_MM16<0x1>, ISA_MICROMIPS32_NOT_MIPS32R6;
}
def LBU16_MM : LoadMM16<"lbu16", GPRMM16Opnd, zextloadi8, II_LBU,
                        mem_mm_4>, LOAD_STORE_FM_MM16<0x02>, ISA_MICROMIPS;
def LHU16_MM : LoadMM16<"lhu16", GPRMM16Opnd, zextloadi16, II_LHU,
                        mem_mm_4_lsl1>, LOAD_STORE_FM_MM16<0x0a>, ISA_MICROMIPS;
def LW16_MM : LoadMM16<"lw16", GPRMM16Opnd, load, II_LW, mem_mm_4_lsl2>,
                      LOAD_STORE_FM_MM16<0x1a>, ISA_MICROMIPS;
def SB16_MM : StoreMM16<"sb16", GPRMM16OpndZero, GPRMM16Opnd, truncstorei8,
                        II_SB, mem_mm_4>, LOAD_STORE_FM_MM16<0x22>,
                        ISA_MICROMIPS32_NOT_MIPS32R6;
def SH16_MM : StoreMM16<"sh16", GPRMM16OpndZero, GPRMM16Opnd, truncstorei16,
                        II_SH, mem_mm_4_lsl1>,
                        LOAD_STORE_FM_MM16<0x2a>, ISA_MICROMIPS32_NOT_MIPS32R6;
def SW16_MM : StoreMM16<"sw16", GPRMM16OpndZero, GPRMM16Opnd, store, II_SW,
                        mem_mm_4_lsl2>, LOAD_STORE_FM_MM16<0x3a>,
                        ISA_MICROMIPS32_NOT_MIPS32R6;
def LWGP_MM : LoadGPMM16<"lw", GPRMM16Opnd, II_LW, mem_mm_gp_simm7_lsl2>,
                         LOAD_GP_FM_MM16<0x19>, ISA_MICROMIPS;
def LWSP_MM : LoadSPMM16<"lw", GPR32Opnd, II_LW, mem_mm_sp_imm5_lsl2>,
              LOAD_STORE_SP_FM_MM16<0x12>, ISA_MICROMIPS;
def SWSP_MM : StoreSPMM16<"sw", GPR32Opnd, II_SW, mem_mm_sp_imm5_lsl2>,
              LOAD_STORE_SP_FM_MM16<0x32>, ISA_MICROMIPS32_NOT_MIPS32R6;
def ADDIUR1SP_MM : AddImmUR1SP<"addiur1sp", GPRMM16Opnd>, ADDIUR1SP_FM_MM16,
                   ISA_MICROMIPS;
def ADDIUR2_MM : AddImmUR2<"addiur2", GPRMM16Opnd>, ADDIUR2_FM_MM16,
                 ISA_MICROMIPS;
def ADDIUS5_MM : AddImmUS5<"addius5", GPR32Opnd>, ADDIUS5_FM_MM16,
                 ISA_MICROMIPS;
def ADDIUSP_MM : AddImmUSP<"addiusp">, ADDIUSP_FM_MM16, ISA_MICROMIPS;
def MFHI16_MM : MoveFromHILOMM<"mfhi16", GPR32Opnd, AC0>,
                MFHILO_FM_MM16<0x10>, ISA_MICROMIPS32_NOT_MIPS32R6;
def MFLO16_MM : MoveFromHILOMM<"mflo16", GPR32Opnd, AC0>,
                MFHILO_FM_MM16<0x12>, ISA_MICROMIPS32_NOT_MIPS32R6;
def MOVE16_MM : MoveMM16<"move", GPR32Opnd>, MOVE_FM_MM16<0x03>,
                ISA_MICROMIPS32_NOT_MIPS32R6;
def MOVEP_MM : MovePMM16<"movep", GPRMM16OpndMovePPairFirst,
                         GPRMM16OpndMovePPairSecond, GPRMM16OpndMoveP>,
               MOVEP_FM_MM16, ISA_MICROMIPS32_NOT_MIPS32R6;
def LI16_MM : LoadImmMM16<"li16", li16_imm, GPRMM16Opnd>, LI_FM_MM16,
              IsAsCheapAsAMove, ISA_MICROMIPS32_NOT_MIPS32R6;
def JALR16_MM : JumpLinkRegMM16<"jalr", GPR32Opnd>, JALR_FM_MM16<0x0e>,
                ISA_MICROMIPS32_NOT_MIPS32R6;
def JALRS16_MM : JumpLinkRegSMM16<"jalrs16", GPR32Opnd>, JALR_FM_MM16<0x0f>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
def JRC16_MM : JumpRegCMM16<"jrc", GPR32Opnd>, JALR_FM_MM16<0x0d>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
def JRADDIUSP : JumpRAddiuStackMM16, JRADDIUSP_FM_MM16<0x18>,
                ISA_MICROMIPS32_NOT_MIPS32R6;
def JR16_MM : JumpRegMM16<"jr16", GPR32Opnd>, JALR_FM_MM16<0x0c>,
              ISA_MICROMIPS32_NOT_MIPS32R6;
def BEQZ16_MM : CBranchZeroMM<"beqz16", brtarget7_mm, GPRMM16Opnd>,
                BEQNEZ_FM_MM16<0x23>, ISA_MICROMIPS32_NOT_MIPS32R6;
def BNEZ16_MM : CBranchZeroMM<"bnez16", brtarget7_mm, GPRMM16Opnd>,
                BEQNEZ_FM_MM16<0x2b>, ISA_MICROMIPS32_NOT_MIPS32R6;
def B16_MM : UncondBranchMM16<"b16">, B16_FM, ISA_MICROMIPS32_NOT_MIPS32R6;
def BREAK16_MM : BrkSdbbp16MM<"break16", II_BREAK>, BRKSDBBP16_FM_MM<0x28>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
def SDBBP16_MM : BrkSdbbp16MM<"sdbbp16", II_SDBBP>, BRKSDBBP16_FM_MM<0x2C>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;

let DecoderNamespace = "MicroMips" in {
  /// Load and Store Instructions - multiple
  def SWM16_MM : StoreMultMM16<"swm16", II_SWM>, LWM_FM_MM16<0x5>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def LWM16_MM : LoadMultMM16<"lwm16", II_LWM>, LWM_FM_MM16<0x4>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def CFC2_MM : InstSE<(outs GPR32Opnd:$rt), (ins COP2Opnd:$impl),
                       "cfc2\t$rt, $impl", [], II_CFC2, FrmFR, "cfc2">,
                POOL32A_CFTC2_FM_MM<0b1100110100>, ISA_MICROMIPS;
  def CTC2_MM : InstSE<(outs COP2Opnd:$impl), (ins GPR32Opnd:$rt),
                       "ctc2\t$rt, $impl", [], II_CTC2, FrmFR, "ctc2">,
                POOL32A_CFTC2_FM_MM<0b1101110100>, ISA_MICROMIPS;
}

class WaitMM<string opstr> :
  InstSE<(outs), (ins uimm10:$code_), !strconcat(opstr, "\t$code_"), [],
         II_WAIT, FrmOther, opstr>;

let DecoderNamespace = "MicroMips" in {
  /// Compact Branch Instructions
  def BEQZC_MM : CompactBranchMM<"beqzc", brtarget_mm, seteq, GPR32Opnd>,
                 COMPACT_BRANCH_FM_MM<0x7>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BNEZC_MM : CompactBranchMM<"bnezc", brtarget_mm, setne, GPR32Opnd>,
                 COMPACT_BRANCH_FM_MM<0x5>, ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Arithmetic Instructions (ALU Immediate)
  def ADDiu_MM : MMRel, ArithLogicI<"addiu", simm16, GPR32Opnd, II_ADDIU>,
                 ADDI_FM_MM<0xc>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def ADDi_MM  : MMRel, ArithLogicI<"addi", simm16, GPR32Opnd, II_ADDI>,
                 ADDI_FM_MM<0x4>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SLTi_MM  : MMRel, SetCC_I<"slti", setlt, simm16, immSExt16, GPR32Opnd>,
                 SLTI_FM_MM<0x24>, ISA_MICROMIPS;
  def SLTiu_MM : MMRel, SetCC_I<"sltiu", setult, simm16, immSExt16, GPR32Opnd>,
                 SLTI_FM_MM<0x2c>, ISA_MICROMIPS;
  def ANDi_MM  : MMRel, ArithLogicI<"andi", uimm16, GPR32Opnd, II_ANDI>,
                 ADDI_FM_MM<0x34>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def ORi_MM   : MMRel, ArithLogicI<"ori", uimm16, GPR32Opnd, II_ORI, immZExt16,
                                    or>, ADDI_FM_MM<0x14>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def XORi_MM  : MMRel, ArithLogicI<"xori", uimm16, GPR32Opnd, II_XORI,
                                    immZExt16, xor>, ADDI_FM_MM<0x1c>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def LUi_MM   : MMRel, LoadUpper<"lui", GPR32Opnd, uimm16_relaxed>, LUI_FM_MM,
                 ISA_MICROMIPS32_NOT_MIPS32R6;

  def LEA_ADDiu_MM : MMRel, EffectiveAddress<"addiu", GPR32Opnd>,
                     LW_FM_MM<0xc>, ISA_MICROMIPS;

  /// Arithmetic Instructions (3-Operand, R-Type)
  def ADDu_MM  : MMRel, ArithLogicR<"addu", GPR32Opnd, 1, II_ADDU, add>,
                 ADD_FM_MM<0, 0x150>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SUBu_MM  : MMRel, ArithLogicR<"subu", GPR32Opnd, 0, II_SUBU, sub>,
                 ADD_FM_MM<0, 0x1d0>, ISA_MICROMIPS32_NOT_MIPS32R6;
  let Defs = [HI0, LO0] in
    def MUL_MM   : MMRel, ArithLogicR<"mul", GPR32Opnd, 1, II_MUL, mul>,
                   ADD_FM_MM<0, 0x210>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def ADD_MM   : MMRel, ArithLogicR<"add", GPR32Opnd, 1, II_ADD>,
                 ADD_FM_MM<0, 0x110>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SUB_MM   : MMRel, ArithLogicR<"sub", GPR32Opnd, 0, II_SUB>,
                 ADD_FM_MM<0, 0x190>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SLT_MM   : MMRel, SetCC_R<"slt", setlt, GPR32Opnd>, ADD_FM_MM<0, 0x350>,
                 ISA_MICROMIPS;
  def SLTu_MM  : MMRel, SetCC_R<"sltu", setult, GPR32Opnd>,
                 ADD_FM_MM<0, 0x390>, ISA_MICROMIPS;
  def AND_MM   : MMRel, ArithLogicR<"and", GPR32Opnd, 1, II_AND, and>,
                 ADD_FM_MM<0, 0x250>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def OR_MM    : MMRel, ArithLogicR<"or", GPR32Opnd, 1, II_OR, or>,
                 ADD_FM_MM<0, 0x290>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def XOR_MM   : MMRel, ArithLogicR<"xor", GPR32Opnd, 1, II_XOR, xor>,
                 ADD_FM_MM<0, 0x310>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def NOR_MM   : MMRel, LogicNOR<"nor", GPR32Opnd>, ADD_FM_MM<0, 0x2d0>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def MULT_MM  : MMRel, Mult<"mult", II_MULT, GPR32Opnd, [HI0, LO0]>,
                 MULT_FM_MM<0x22c>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def MULTu_MM : MMRel, Mult<"multu", II_MULTU, GPR32Opnd, [HI0, LO0]>,
                 MULT_FM_MM<0x26c>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SDIV_MM  : MMRel, Div<"div", II_DIV, GPR32Opnd, [HI0, LO0]>,
                 MULT_FM_MM<0x2ac>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def UDIV_MM  : MMRel, Div<"divu", II_DIVU, GPR32Opnd, [HI0, LO0]>,
                 MULT_FM_MM<0x2ec>, ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Arithmetic Instructions with PC and Immediate
  def ADDIUPC_MM : AddImmUPC<"addiupc", GPRMM16Opnd>, ADDIUPC_FM_MM,
                   ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Shift Instructions
  def SLL_MM   : MMRel, shift_rotate_imm<"sll", uimm5, GPR32Opnd, II_SLL>,
                 SRA_FM_MM<0, 0>, ISA_MICROMIPS;
  def SRL_MM   : MMRel, shift_rotate_imm<"srl", uimm5, GPR32Opnd, II_SRL>,
                 SRA_FM_MM<0x40, 0>, ISA_MICROMIPS;
  def SRA_MM   : MMRel, shift_rotate_imm<"sra", uimm5, GPR32Opnd, II_SRA>,
                 SRA_FM_MM<0x80, 0>, ISA_MICROMIPS;
  def SLLV_MM  : MMRel, shift_rotate_reg<"sllv", GPR32Opnd, II_SLLV>,
                 SRLV_FM_MM<0x10, 0>, ISA_MICROMIPS;
  def SRLV_MM  : MMRel, shift_rotate_reg<"srlv", GPR32Opnd, II_SRLV>,
                 SRLV_FM_MM<0x50, 0>, ISA_MICROMIPS;
  def SRAV_MM  : MMRel, shift_rotate_reg<"srav", GPR32Opnd, II_SRAV>,
                 SRLV_FM_MM<0x90, 0>, ISA_MICROMIPS;
  def ROTR_MM  : MMRel, shift_rotate_imm<"rotr", uimm5, GPR32Opnd, II_ROTR>,
                 SRA_FM_MM<0xc0, 0>, ISA_MICROMIPS {
    list<dag> Pattern = [(set GPR32Opnd:$rd,
                          (rotr GPR32Opnd:$rt, immZExt5:$shamt))];
  }
  def ROTRV_MM : MMRel, shift_rotate_reg<"rotrv", GPR32Opnd, II_ROTRV>,
                 SRLV_FM_MM<0xd0, 0>, ISA_MICROMIPS {
    list<dag> Pattern = [(set GPR32Opnd:$rd,
                          (rotr GPR32Opnd:$rt, GPR32Opnd:$rs))];
  }

  /// Load and Store Instructions - aligned
  let DecoderMethod = "DecodeMemMMImm16" in {
    def LB_MM  : LoadMemory<"lb", GPR32Opnd, mem_mm_16, sextloadi8, II_LB>,
                 MMRel, LW_FM_MM<0x7>, ISA_MICROMIPS;
    def LBu_MM : LoadMemory<"lbu", GPR32Opnd, mem_mm_16, zextloadi8, II_LBU>,
                 MMRel, LW_FM_MM<0x5>, ISA_MICROMIPS;
    def LH_MM  : LoadMemory<"lh", GPR32Opnd, mem_simmptr, sextloadi16, II_LH,
                            addrDefault>, MMRel, LW_FM_MM<0xf>, ISA_MICROMIPS;
    def LHu_MM : LoadMemory<"lhu", GPR32Opnd, mem_simmptr, zextloadi16, II_LHU>,
                 MMRel, LW_FM_MM<0xd>, ISA_MICROMIPS;
    def LW_MM  : Load<"lw", GPR32Opnd, null_frag, II_LW>, MMRel, LW_FM_MM<0x3f>,
                 ISA_MICROMIPS;
    def SB_MM  : Store<"sb", GPR32Opnd, truncstorei8, II_SB>, MMRel,
                 LW_FM_MM<0x6>, ISA_MICROMIPS;
    def SH_MM  : Store<"sh", GPR32Opnd, truncstorei16, II_SH>, MMRel,
                 LW_FM_MM<0xe>, ISA_MICROMIPS;
    def SW_MM  : Store<"sw", GPR32Opnd, null_frag, II_SW>, MMRel,
                 LW_FM_MM<0x3e>, ISA_MICROMIPS;
  }
}
let DecoderNamespace = "MicroMips" in {
  let DecoderMethod = "DecodeMemMMImm9" in {
    def LBE_MM  : MMRel, Load<"lbe", GPR32Opnd, null_frag, II_LBE>,
                  POOL32C_LHUE_FM_MM<0x18, 0x6, 0x4>, ISA_MICROMIPS, ASE_EVA;
    def LBuE_MM : MMRel, Load<"lbue", GPR32Opnd, null_frag, II_LBUE>,
                  POOL32C_LHUE_FM_MM<0x18, 0x6, 0x0>, ISA_MICROMIPS, ASE_EVA;
    def LHE_MM  : MMRel, LoadMemory<"lhe", GPR32Opnd, mem_simm9, null_frag,
                                    II_LHE>,
                  POOL32C_LHUE_FM_MM<0x18, 0x6, 0x5>, ISA_MICROMIPS, ASE_EVA;
    def LHuE_MM : MMRel, LoadMemory<"lhue", GPR32Opnd, mem_simm9, null_frag,
                                    II_LHUE>,
                  POOL32C_LHUE_FM_MM<0x18, 0x6, 0x1>, ISA_MICROMIPS, ASE_EVA;
    def LWE_MM  : MMRel, LoadMemory<"lwe", GPR32Opnd, mem_simm9, null_frag,
                                    II_LWE>,
                  POOL32C_LHUE_FM_MM<0x18, 0x6, 0x7>, ISA_MICROMIPS, ASE_EVA;
    def SBE_MM  : MMRel, StoreMemory<"sbe", GPR32Opnd, mem_simm9, null_frag,
                                     II_SBE>,
                  POOL32C_LHUE_FM_MM<0x18, 0xa, 0x4>, ISA_MICROMIPS, ASE_EVA;
    def SHE_MM  : MMRel, StoreMemory<"she", GPR32Opnd, mem_simm9, null_frag,
                                     II_SHE>,
                  POOL32C_LHUE_FM_MM<0x18, 0xa, 0x5>, ISA_MICROMIPS, ASE_EVA;
    def SWE_MM  : MMRel, StoreMemory<"swe", GPR32Opnd, mem_simm9, null_frag,
                                     II_SWE>,
                  POOL32C_LHUE_FM_MM<0x18, 0xa, 0x7>, ISA_MICROMIPS, ASE_EVA;
    def LWLE_MM : MMRel, LoadLeftRightMM<"lwle", MipsLWL, GPR32Opnd, mem_mm_9,
                                         II_LWLE>,
                  POOL32C_STEVA_LDEVA_FM_MM<0x6, 0x2>,
                  ISA_MICROMIPS32_NOT_MIPS32R6, ASE_EVA;
    def LWRE_MM : MMRel, LoadLeftRightMM<"lwre", MipsLWR, GPR32Opnd, mem_mm_9,
                                         II_LWRE>,
                  POOL32C_STEVA_LDEVA_FM_MM<0x6, 0x3>,
                  ISA_MICROMIPS32_NOT_MIPS32R6, ASE_EVA;
    def SWLE_MM : MMRel, StoreLeftRightMM<"swle", MipsSWL, GPR32Opnd, mem_mm_9,
                                          II_SWLE>,
                  POOL32C_STEVA_LDEVA_FM_MM<0xa, 0x0>,
                  ISA_MICROMIPS32_NOT_MIPS32R6, ASE_EVA;
    def SWRE_MM : MMRel, StoreLeftRightMM<"swre", MipsSWR, GPR32Opnd, mem_mm_9,
                                          II_SWRE>,
                  POOL32C_STEVA_LDEVA_FM_MM<0xa, 0x1>,
                  ISA_MICROMIPS32_NOT_MIPS32R6, ASE_EVA;
  }

  def LWXS_MM : LoadWordIndexedScaledMM<"lwxs", GPR32Opnd>, LWXS_FM_MM<0x118>,
                ISA_MICROMIPS;

  /// Load and Store Instructions - unaligned
  def LWL_MM : MMRel, LoadLeftRightMM<"lwl", MipsLWL, GPR32Opnd, mem_mm_12,
                                      II_LWL>, LWL_FM_MM<0x0>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
  def LWR_MM : MMRel, LoadLeftRightMM<"lwr", MipsLWR, GPR32Opnd, mem_mm_12,
                                      II_LWR>, LWL_FM_MM<0x1>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
  def SWL_MM : MMRel, StoreLeftRightMM<"swl", MipsSWL, GPR32Opnd, mem_mm_12,
                                       II_SWL>, LWL_FM_MM<0x8>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
  def SWR_MM : MMRel, StoreLeftRightMM<"swr", MipsSWR, GPR32Opnd, mem_mm_12,
                                       II_SWR>, LWL_FM_MM<0x9>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
}
let DecoderNamespace = "MicroMips" in {
  /// Load and Store Instructions - multiple
  def SWM32_MM  : StoreMultMM<"swm32", II_SWM>, LWM_FM_MM<0xd>, ISA_MICROMIPS;
  def LWM32_MM  : LoadMultMM<"lwm32", II_LWM>, LWM_FM_MM<0x5>, ISA_MICROMIPS;

  /// Load and Store Pair Instructions
  def SWP_MM  : StorePairMM<"swp">, LWM_FM_MM<0x9>, ISA_MICROMIPS;
  def LWP_MM  : LoadPairMM<"lwp">, LWM_FM_MM<0x1>, ISA_MICROMIPS;

  /// Load and Store multiple pseudo Instructions
  class LoadWordMultMM<string instr_asm > :
    MipsAsmPseudoInst<(outs reglist:$rt), (ins mem_mm_12:$addr),
                      !strconcat(instr_asm, "\t$rt, $addr")> ;

  class StoreWordMultMM<string instr_asm > :
    MipsAsmPseudoInst<(outs), (ins reglist:$rt, mem_mm_12:$addr),
                      !strconcat(instr_asm, "\t$rt, $addr")> ;


  def SWM_MM  : StoreWordMultMM<"swm">, ISA_MICROMIPS;
  def LWM_MM  : LoadWordMultMM<"lwm">, ISA_MICROMIPS;

  /// Move Conditional
  def MOVZ_I_MM : MMRel, CMov_I_I_FT<"movz", GPR32Opnd, GPR32Opnd,
                                     II_MOVZ>, ADD_FM_MM<0, 0x58>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVN_I_MM : MMRel, CMov_I_I_FT<"movn", GPR32Opnd, GPR32Opnd,
                                     II_MOVN>, ADD_FM_MM<0, 0x18>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVT_I_MM : MMRel, CMov_F_I_FT<"movt", GPR32Opnd, II_MOVT, MipsCMovFP_T>,
                  CMov_F_I_FM_MM<0x25>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVF_I_MM : MMRel, CMov_F_I_FT<"movf", GPR32Opnd, II_MOVF, MipsCMovFP_F>,
                  CMov_F_I_FM_MM<0x5>, ISA_MICROMIPS32_NOT_MIPS32R6;
  /// Move to/from HI/LO
  def MTHI_MM : MMRel, MoveToLOHI<"mthi", GPR32Opnd, [HI0]>,
                MTLO_FM_MM<0x0b5>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def MTLO_MM : MMRel, MoveToLOHI<"mtlo", GPR32Opnd, [LO0]>,
                MTLO_FM_MM<0x0f5>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def MFHI_MM : MMRel, MoveFromLOHI<"mfhi", GPR32Opnd, AC0>,
                MFLO_FM_MM<0x035>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def MFLO_MM : MMRel, MoveFromLOHI<"mflo", GPR32Opnd, AC0>,
                MFLO_FM_MM<0x075>, ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Multiply Add/Sub Instructions
  def MADD_MM  : MMRel, MArithR<"madd", II_MADD, 1>, MULT_FM_MM<0x32c>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def MADDU_MM : MMRel, MArithR<"maddu", II_MADDU, 1>, MULT_FM_MM<0x36c>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def MSUB_MM  : MMRel, MArithR<"msub", II_MSUB>, MULT_FM_MM<0x3ac>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def MSUBU_MM : MMRel, MArithR<"msubu", II_MSUBU>, MULT_FM_MM<0x3ec>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Count Leading
  def CLZ_MM : MMRel, CountLeading0<"clz", GPR32Opnd, II_CLZ>, CLO_FM_MM<0x16c>,
               ISA_MICROMIPS;
  def CLO_MM : MMRel, CountLeading1<"clo", GPR32Opnd, II_CLO>, CLO_FM_MM<0x12c>,
               ISA_MICROMIPS;

  /// Sign Ext In Register Instructions.
  def SEB_MM : MMRel, SignExtInReg<"seb", i8, GPR32Opnd, II_SEB>,
               SEB_FM_MM<0x0ac>, ISA_MICROMIPS;
  def SEH_MM : MMRel, SignExtInReg<"seh", i16, GPR32Opnd, II_SEH>,
               SEB_FM_MM<0x0ec>, ISA_MICROMIPS;

  /// Word Swap Bytes Within Halfwords
  def WSBH_MM : MMRel, SubwordSwap<"wsbh", GPR32Opnd, II_WSBH>,
                SEB_FM_MM<0x1ec>, ISA_MICROMIPS;
  // TODO: Add '0 < pos+size <= 32' constraint check to ext instruction
  def EXT_MM : MMRel, ExtBase<"ext", GPR32Opnd, uimm5, uimm5_plus1, immZExt5,
                              immZExt5Plus1, MipsExt>, EXT_FM_MM<0x2c>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
  def INS_MM : MMRel, InsBase<"ins", GPR32Opnd, uimm5, uimm5_inssize_plus1,
                              immZExt5, immZExt5Plus1>,
               EXT_FM_MM<0x0c>, ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Jump Instructions
  let DecoderMethod = "DecodeJumpTargetMM" in
    def J_MM          : MMRel, JumpFJ<jmptarget_mm, "j", br, bb, "j">,
                        J_FM_MM<0x35>, AdditionalRequires<[RelocNotPIC]>,
                        IsBranch, ISA_MICROMIPS32_NOT_MIPS32R6;

  let DecoderMethod = "DecodeJumpTargetMM" in {
    def JAL_MM      : MMRel, JumpLink<"jal", calltarget_mm>, J_FM_MM<0x3d>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
    def JALX_MM     : MMRel, JumpLink<"jalx", calltarget>, J_FM_MM<0x3c>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  }
  def JR_MM : MMRel, IndirectBranch<"jr", GPR32Opnd>, JR_FM_MM<0x3c>,
              ISA_MICROMIPS32_NOT_MIPS32R6;
  def JALR_MM : JumpLinkReg<"jalr", GPR32Opnd>, JALR_FM_MM<0x03c>,
                ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Jump Instructions - Short Delay Slot
  def JALS_MM   : JumpLinkMM<"jals", calltarget_mm>, J_FM_MM<0x1d>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
  def JALRS_MM  : JumpLinkRegMM<"jalrs", GPR32Opnd>, JALR_FM_MM<0x13c>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Branch Instructions
  def BEQ_MM  : MMRel, CBranch<"beq", brtarget_mm, seteq, GPR32Opnd>,
                BEQ_FM_MM<0x25>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BNE_MM  : MMRel, CBranch<"bne", brtarget_mm, setne, GPR32Opnd>,
                BEQ_FM_MM<0x2d>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BGEZ_MM : MMRel, CBranchZero<"bgez", brtarget_mm, setge, GPR32Opnd>,
                BGEZ_FM_MM<0x2>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BGTZ_MM : MMRel, CBranchZero<"bgtz", brtarget_mm, setgt, GPR32Opnd>,
                BGEZ_FM_MM<0x6>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BLEZ_MM : MMRel, CBranchZero<"blez", brtarget_mm, setle, GPR32Opnd>,
                BGEZ_FM_MM<0x4>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BLTZ_MM : MMRel, CBranchZero<"bltz", brtarget_mm, setlt, GPR32Opnd>,
                BGEZ_FM_MM<0x0>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BGEZAL_MM : MMRel, BGEZAL_FT<"bgezal", brtarget_mm, GPR32Opnd>,
                  BGEZAL_FM_MM<0x03>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BLTZAL_MM : MMRel, BGEZAL_FT<"bltzal", brtarget_mm, GPR32Opnd>,
                  BGEZAL_FM_MM<0x01>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BAL_BR_MM : BAL_BR_Pseudo<BGEZAL_MM, brtarget_mm>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Branch Instructions - Short Delay Slot
  def BGEZALS_MM : BranchCompareToZeroLinkMM<"bgezals", brtarget_mm,
                                             GPR32Opnd>, BGEZAL_FM_MM<0x13>,
                   ISA_MICROMIPS32_NOT_MIPS32R6;
  def BLTZALS_MM : BranchCompareToZeroLinkMM<"bltzals", brtarget_mm,
                                             GPR32Opnd>, BGEZAL_FM_MM<0x11>,
                   ISA_MICROMIPS32_NOT_MIPS32R6;
  def B_MM    : UncondBranch<BEQ_MM, brtarget_mm>, IsBranch,
                ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Control Instructions
  def SYNC_MM    : MMRel, SYNC_FT<"sync">, SYNC_FM_MM, ISA_MICROMIPS;
  let DecoderMethod = "DecodeSyncI_MM" in
    def SYNCI_MM   : MMRel, SYNCI_FT<"synci", mem_mm_16>, SYNCI_FM_MM,
                     ISA_MICROMIPS32_NOT_MIPS32R6;
  def BREAK_MM   : MMRel, BRK_FT<"break">, BRK_FM_MM, ISA_MICROMIPS;
  def SYSCALL_MM : MMRel, SYS_FT<"syscall", uimm10, II_SYSCALL>, SYS_FM_MM,
                   ISA_MICROMIPS;
  def WAIT_MM    : MMRel, WaitMM<"wait">, WAIT_FM_MM, ISA_MICROMIPS;
  def ERET_MM    : MMRel, ER_FT<"eret", II_ERET>, ER_FM_MM<0x3cd>,
                   ISA_MICROMIPS;
  def DERET_MM   : MMRel, ER_FT<"deret", II_DERET>, ER_FM_MM<0x38d>,
                   ISA_MICROMIPS;
  def EI_MM      : MMRel, DEI_FT<"ei", GPR32Opnd, II_EI>, EI_FM_MM<0x15d>,
                   ISA_MICROMIPS;
  def DI_MM      : MMRel, DEI_FT<"di", GPR32Opnd, II_DI>, EI_FM_MM<0x11d>,
                   ISA_MICROMIPS;
  def TRAP_MM    : TrapBase<BREAK_MM>, ISA_MICROMIPS;

  /// Trap Instructions
  def TEQ_MM  : MMRel, TEQ_FT<"teq", GPR32Opnd, uimm4, II_TEQ>, TEQ_FM_MM<0x0>,
                ISA_MICROMIPS;
  def TGE_MM  : MMRel, TEQ_FT<"tge", GPR32Opnd, uimm4, II_TGE>, TEQ_FM_MM<0x08>,
                ISA_MICROMIPS;
  def TGEU_MM : MMRel, TEQ_FT<"tgeu", GPR32Opnd, uimm4, II_TGEU>,
                TEQ_FM_MM<0x10>, ISA_MICROMIPS;
  def TLT_MM  : MMRel, TEQ_FT<"tlt", GPR32Opnd, uimm4, II_TLT>, TEQ_FM_MM<0x20>,
                ISA_MICROMIPS;
  def TLTU_MM : MMRel, TEQ_FT<"tltu", GPR32Opnd, uimm4, II_TLTU>,
                TEQ_FM_MM<0x28>, ISA_MICROMIPS;
  def TNE_MM  : MMRel, TEQ_FT<"tne", GPR32Opnd, uimm4, II_TNE>, TEQ_FM_MM<0x30>,
                ISA_MICROMIPS;

  def TEQI_MM  : MMRel, TEQI_FT<"teqi", GPR32Opnd, II_TEQI>, TEQI_FM_MM<0x0e>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def TGEI_MM  : MMRel, TEQI_FT<"tgei", GPR32Opnd, II_TGEI>, TEQI_FM_MM<0x09>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def TGEIU_MM : MMRel, TEQI_FT<"tgeiu", GPR32Opnd, II_TGEIU>,
                 TEQI_FM_MM<0x0b>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def TLTI_MM  : MMRel, TEQI_FT<"tlti", GPR32Opnd, II_TLTI>, TEQI_FM_MM<0x08>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
  def TLTIU_MM : MMRel, TEQI_FT<"tltiu", GPR32Opnd, II_TTLTIU>,
                 TEQI_FM_MM<0x0a>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def TNEI_MM  : MMRel, TEQI_FT<"tnei", GPR32Opnd, II_TNEI>, TEQI_FM_MM<0x0c>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;

  /// Load-linked, Store-conditional
  def LL_MM : LLBaseMM<"ll", GPR32Opnd>, LL_FM_MM<0x3>,
              ISA_MICROMIPS32_NOT_MIPS32R6;
  def SC_MM : SCBaseMM<"sc", GPR32Opnd>, LL_FM_MM<0xb>,
              ISA_MICROMIPS32_NOT_MIPS32R6;

  def LLE_MM : MMRel, LLEBaseMM<"lle", GPR32Opnd>, LLE_FM_MM<0x6>,
               ISA_MICROMIPS, ASE_EVA;
  def SCE_MM : MMRel, SCEBaseMM<"sce", GPR32Opnd>, LLE_FM_MM<0xA>,
               ISA_MICROMIPS, ASE_EVA;

  let DecoderMethod = "DecodeCacheOpMM" in {
    def CACHE_MM : MMRel, CacheOp<"cache", mem_mm_12, II_CACHE>,
                   CACHE_PREF_FM_MM<0x08, 0x6>, ISA_MICROMIPS32_NOT_MIPS32R6;
    def PREF_MM  : MMRel, CacheOp<"pref", mem_mm_12, II_PREF>,
                   CACHE_PREF_FM_MM<0x18, 0x2>, ISA_MICROMIPS32_NOT_MIPS32R6;
  }

  let DecoderMethod = "DecodePrefeOpMM" in {
    def PREFE_MM  : MMRel, CacheOp<"prefe", mem_mm_9, II_PREFE>,
                    CACHE_PREFE_FM_MM<0x18, 0x2>, ISA_MICROMIPS, ASE_EVA;
    def CACHEE_MM : MMRel, CacheOp<"cachee", mem_mm_9, II_CACHEE>,
                    CACHE_PREFE_FM_MM<0x18, 0x3>, ISA_MICROMIPS, ASE_EVA;
  }
  def SSNOP_MM : MMRel, Barrier<"ssnop", II_SSNOP>, BARRIER_FM_MM<0x1>,
                 ISA_MICROMIPS;
  def EHB_MM   : MMRel, Barrier<"ehb", II_EHB>, BARRIER_FM_MM<0x3>,
                 ISA_MICROMIPS;
  def PAUSE_MM : MMRel, Barrier<"pause", II_PAUSE>, BARRIER_FM_MM<0x5>,
                 ISA_MICROMIPS;

  def TLBP_MM : MMRel, TLB<"tlbp", II_TLBP>, COP0_TLB_FM_MM<0x0d>,
                ISA_MICROMIPS;
  def TLBR_MM : MMRel, TLB<"tlbr", II_TLBR>, COP0_TLB_FM_MM<0x4d>,
                ISA_MICROMIPS;
  def TLBWI_MM : MMRel, TLB<"tlbwi", II_TLBWI>, COP0_TLB_FM_MM<0x8d>,
                 ISA_MICROMIPS;
  def TLBWR_MM : MMRel, TLB<"tlbwr", II_TLBWR>, COP0_TLB_FM_MM<0xcd>,
                 ISA_MICROMIPS;

  def SDBBP_MM : MMRel, SYS_FT<"sdbbp", uimm10, II_SDBBP>, SDBBP_FM_MM,
                 ISA_MICROMIPS;

  def PREFX_MM : PrefetchIndexed<"prefx">, POOL32F_PREFX_FM_MM<0x15, 0x1A0>,
                 ISA_MICROMIPS32_NOT_MIPS32R6;
}

let AdditionalPredicates = [NotDSP] in {
  def PseudoMULT_MM : MultDivPseudo<MULT, ACC64, GPR32Opnd, MipsMult, II_MULT>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMULTu_MM : MultDivPseudo<MULTu, ACC64, GPR32Opnd, MipsMultu, II_MULTU>,
                       ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMFHI_MM : PseudoMFLOHI<GPR32, ACC64, MipsMFHI>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMFLO_MM : PseudoMFLOHI<GPR32, ACC64, MipsMFLO>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMTLOHI_MM : PseudoMTLOHI<ACC64, GPR32>,
                        ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMADD_MM : MAddSubPseudo<MADD, MipsMAdd, II_MADD>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMADDU_MM : MAddSubPseudo<MADDU, MipsMAddu, II_MADDU>,
                       ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMSUB_MM : MAddSubPseudo<MSUB, MipsMSub, II_MSUB>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;
  def PseudoMSUBU_MM : MAddSubPseudo<MSUBU, MipsMSubu, II_MSUBU>,
                       ISA_MICROMIPS32_NOT_MIPS32R6;
}

def TAILCALL_MM : TailCall<J_MM, jmptarget_mm>, ISA_MIPS1_NOT_32R6_64R6;

def TAILCALLREG_MM  : TailCallReg<JRC16_MM, GPR32Opnd>,
                      ISA_MICROMIPS32_NOT_MIPS32R6;

def PseudoIndirectBranch_MM : PseudoIndirectBranchBase<JR_MM, GPR32Opnd>,
                              ISA_MICROMIPS32_NOT_MIPS32R6;

let DecoderNamespace = "MicroMips" in {
  def RDHWR_MM : MMRel, R6MMR6Rel, ReadHardware<GPR32Opnd, HWRegsOpnd>,
                 RDHWR_FM_MM, ISA_MICROMIPS32_NOT_MIPS32R6;
  def LWU_MM : MMRel, LoadMM<"lwu", GPR32Opnd, zextloadi32, II_LWU,
                             mem_simm12>, LL_FM_MM<0xe>,
               ISA_MICROMIPS32_NOT_MIPS32R6;
}

let DecoderNamespace = "MicroMips" in {
  def MFGC0_MM    : MMRel, MfCop0MM<"mfgc0", GPR32Opnd, COP0Opnd, II_MFGC0>,
                    POOL32A_MFTC0_FM_MM<0b10011, 0b111100>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
  def MFHGC0_MM   : MMRel, MfCop0MM<"mfhgc0", GPR32Opnd, COP0Opnd, II_MFHGC0>,
                    POOL32A_MFTC0_FM_MM<0b10011, 0b110100>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
  def MTGC0_MM    : MMRel, MtCop0MM<"mtgc0", COP0Opnd, GPR32Opnd, II_MTGC0>,
                    POOL32A_MFTC0_FM_MM<0b11011, 0b111100>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
  def MTHGC0_MM   : MMRel, MtCop0MM<"mthgc0", COP0Opnd, GPR32Opnd, II_MTHGC0>,
                    POOL32A_MFTC0_FM_MM<0b11011, 0b110100>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
  def HYPCALL_MM  : MMRel, HypcallMM<"hypcall">, POOL32A_HYPCALL_FM_MM,
                    ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGINV_MM  : MMRel, TLBINVMM<"tlbginv", II_TLBGINV>,
                    POOL32A_TLBINV_FM_MM<0x105>, ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGINVF_MM : MMRel, TLBINVMM<"tlbginvf", II_TLBGINVF>,
                    POOL32A_TLBINV_FM_MM<0x145>, ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGP_MM    : MMRel, TLBINVMM<"tlbgp", II_TLBGP>,
                    POOL32A_TLBINV_FM_MM<0x5>, ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGR_MM    : MMRel, TLBINVMM<"tlbgr", II_TLBGR>,
                    POOL32A_TLBINV_FM_MM<0x45>, ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGWI_MM   : MMRel, TLBINVMM<"tlbgwi", II_TLBGWI>,
                    POOL32A_TLBINV_FM_MM<0x85>, ISA_MICROMIPS32R5, ASE_VIRT;
  def TLBGWR_MM   : MMRel, TLBINVMM<"tlbgwr", II_TLBGWR>,
                    POOL32A_TLBINV_FM_MM<0xc5>, ISA_MICROMIPS32R5, ASE_VIRT;
}

//===----------------------------------------------------------------------===//
// MicroMips arbitrary patterns that map to one or more instructions
//===----------------------------------------------------------------------===//

defm : MipsHiLoRelocs<LUi_MM, ADDiu_MM, ZERO, GPR32Opnd>, ISA_MICROMIPS;

def : MipsPat<(MipsGotHi tglobaladdr:$in), (LUi_MM tglobaladdr:$in)>,
      ISA_MICROMIPS;
def : MipsPat<(MipsGotHi texternalsym:$in), (LUi_MM texternalsym:$in)>,
      ISA_MICROMIPS;

def : MipsPat<(MipsTlsHi tglobaltlsaddr:$in), (LUi_MM tglobaltlsaddr:$in)>,
      ISA_MICROMIPS;

// gp_rel relocs
def : MipsPat<(add GPR32:$gp, (MipsGPRel tglobaladdr:$in)),
              (ADDiu_MM GPR32:$gp, tglobaladdr:$in)>, ISA_MICROMIPS;
def : MipsPat<(add GPR32:$gp, (MipsGPRel tconstpool:$in)),
              (ADDiu_MM GPR32:$gp, tconstpool:$in)>, ISA_MICROMIPS;

def : WrapperPat<tglobaladdr, ADDiu_MM, GPR32>, ISA_MICROMIPS;
def : WrapperPat<tconstpool, ADDiu_MM, GPR32>, ISA_MICROMIPS;
def : WrapperPat<texternalsym, ADDiu_MM, GPR32>, ISA_MICROMIPS;
def : WrapperPat<tblockaddress, ADDiu_MM, GPR32>, ISA_MICROMIPS;
def : WrapperPat<tjumptable, ADDiu_MM, GPR32>, ISA_MICROMIPS;
def : WrapperPat<tglobaltlsaddr, ADDiu_MM, GPR32>, ISA_MICROMIPS;

def : MipsPat<(atomic_load_8 addr:$a), (LB_MM addr:$a)>, ISA_MICROMIPS;
def : MipsPat<(atomic_load_16 addr:$a), (LH_MM addr:$a)>, ISA_MICROMIPS;
def : MipsPat<(atomic_load_32 addr:$a), (LW_MM addr:$a)>, ISA_MICROMIPS;

def : MipsPat<(i32 immLi16:$imm),
              (LI16_MM immLi16:$imm)>, ISA_MICROMIPS;

defm :  MaterializeImms<i32, ZERO, ADDiu_MM, LUi_MM, ORi_MM>, ISA_MICROMIPS;

def : MipsPat<(not GPRMM16:$in),
              (NOT16_MM GPRMM16:$in)>, ISA_MICROMIPS;
def : MipsPat<(not GPR32:$in),
              (NOR_MM GPR32Opnd:$in, ZERO)>, ISA_MICROMIPS;

def : MipsPat<(add GPRMM16:$src, immSExtAddiur2:$imm),
              (ADDIUR2_MM GPRMM16:$src, immSExtAddiur2:$imm)>, ISA_MICROMIPS;
def : MipsPat<(add GPR32:$src, immSExtAddius5:$imm),
              (ADDIUS5_MM GPR32:$src, immSExtAddius5:$imm)>, ISA_MICROMIPS;
def : MipsPat<(add GPR32:$src, immSExt16:$imm),
              (ADDiu_MM GPR32:$src, immSExt16:$imm)>, ISA_MICROMIPS;

def : MipsPat<(and GPRMM16:$src, immZExtAndi16:$imm),
              (ANDI16_MM GPRMM16:$src, immZExtAndi16:$imm)>, ISA_MICROMIPS;
def : MipsPat<(and GPR32:$src, immZExt16:$imm),
              (ANDi_MM GPR32:$src, immZExt16:$imm)>, ISA_MICROMIPS;

def : MipsPat<(shl GPRMM16:$src, immZExt2Shift:$imm),
              (SLL16_MM GPRMM16:$src, immZExt2Shift:$imm)>, ISA_MICROMIPS;
def : MipsPat<(shl GPR32:$src, immZExt5:$imm),
              (SLL_MM GPR32:$src, immZExt5:$imm)>, ISA_MICROMIPS;
def : MipsPat<(shl GPR32:$lhs, GPR32:$rhs),
              (SLLV_MM GPR32:$lhs, GPR32:$rhs)>, ISA_MICROMIPS;

def : MipsPat<(srl GPRMM16:$src, immZExt2Shift:$imm),
              (SRL16_MM GPRMM16:$src, immZExt2Shift:$imm)>, ISA_MICROMIPS;
def : MipsPat<(srl GPR32:$src, immZExt5:$imm),
              (SRL_MM GPR32:$src, immZExt5:$imm)>, ISA_MICROMIPS;
def : MipsPat<(srl GPR32:$lhs, GPR32:$rhs),
              (SRLV_MM GPR32:$lhs, GPR32:$rhs)>, ISA_MICROMIPS;

def : MipsPat<(sra GPR32:$src, immZExt5:$imm),
              (SRA_MM GPR32:$src, immZExt5:$imm)>, ISA_MICROMIPS;
def : MipsPat<(sra GPR32:$lhs, GPR32:$rhs),
              (SRAV_MM GPR32:$lhs, GPR32:$rhs)>, ISA_MICROMIPS;

def : MipsPat<(store GPRMM16:$src, addrimm4lsl2:$addr),
              (SW16_MM GPRMM16:$src, addrimm4lsl2:$addr)>, ISA_MICROMIPS;
def : MipsPat<(store GPR32:$src, addr:$addr),
              (SW_MM GPR32:$src, addr:$addr)>, ISA_MICROMIPS;

def : MipsPat<(load addrimm4lsl2:$addr),
              (LW16_MM addrimm4lsl2:$addr)>, ISA_MICROMIPS;
def : MipsPat<(load addr:$addr),
              (LW_MM addr:$addr)>, ISA_MICROMIPS;
def : MipsPat<(subc GPR32:$lhs, GPR32:$rhs),
              (SUBu_MM GPR32:$lhs, GPR32:$rhs)>, ISA_MICROMIPS;

def : MipsPat<(i32 (extloadi1  addr:$src)), (LBu_MM addr:$src)>,
      ISA_MICROMIPS;

def : MipsPat<(i32 (extloadi8  addr:$src)), (LBu_MM addr:$src)>,
      ISA_MICROMIPS;

def : MipsPat<(i32 (extloadi16 addr:$src)), (LHu_MM addr:$src)>,
      ISA_MICROMIPS;

let AddedComplexity = 40 in
  def : MipsPat<(i32 (sextloadi16 addrRegImm:$a)),
                (LH_MM addrRegImm:$a)>, ISA_MICROMIPS;


def : MipsPat<(bswap GPR32:$rt), (ROTR_MM (WSBH_MM GPR32:$rt), 16)>,
      ISA_MICROMIPS;

def : MipsPat<(MipsJmpLink (i32 texternalsym:$dst)),
              (JAL_MM texternalsym:$dst)>, ISA_MICROMIPS32_NOT_MIPS32R6;
def : MipsPat<(MipsTailCall (iPTR tglobaladdr:$dst)),
              (TAILCALL_MM tglobaladdr:$dst)>, ISA_MICROMIPS32_NOT_MIPS32R6;
def : MipsPat<(MipsTailCall (iPTR texternalsym:$dst)),
              (TAILCALL_MM texternalsym:$dst)>, ISA_MICROMIPS32_NOT_MIPS32R6;

defm : BrcondPats<GPR32, BEQ_MM, BEQ_MM, BNE_MM, SLT_MM, SLTu_MM, SLTi_MM,
                  SLTiu_MM, ZERO>, ISA_MICROMIPS32_NOT_MIPS32R6;

def : MipsPat<(brcond (i32 (setlt i32:$lhs, 1)), bb:$dst),
              (BLEZ_MM i32:$lhs, bb:$dst)>, ISA_MICROMIPS32_NOT_MIPS32R6;
def : MipsPat<(brcond (i32 (setgt i32:$lhs, -1)), bb:$dst),
              (BGEZ_MM i32:$lhs, bb:$dst)>, ISA_MICROMIPS32_NOT_MIPS32R6;

defm : SeteqPats<GPR32, SLTiu_MM, XOR_MM, SLTu_MM, ZERO>, ISA_MICROMIPS;
defm : SetlePats<GPR32, XORi_MM, SLT_MM, SLTu_MM>, ISA_MICROMIPS;
defm : SetgtPats<GPR32, SLT_MM, SLTu_MM>, ISA_MICROMIPS;
defm : SetgePats<GPR32, XORi_MM, SLT_MM, SLTu_MM>, ISA_MICROMIPS;
defm : SetgeImmPats<GPR32, XORi_MM, SLTi_MM, SLTiu_MM>, ISA_MICROMIPS;

// Select patterns

// Instantiation of conditional move patterns.
defm : MovzPats0<GPR32, GPR32, MOVZ_I_MM, SLT_MM, SLTu_MM, SLTi_MM, SLTiu_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;
defm : MovzPats1<GPR32, GPR32, MOVZ_I_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;
defm : MovzPats2<GPR32, GPR32, MOVZ_I_MM, XORi_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;


defm : MovnPats<GPR32, GPR32, MOVN_I_MM, XOR_MM>, INSN_MIPS4_32_NOT_32R6_64R6;

// Instantiation of conditional move patterns.
defm : MovzPats0<GPR32, GPR32, MOVZ_I_MM, SLT_MM, SLTu_MM, SLTi_MM, SLTiu_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;
defm : MovzPats1<GPR32, GPR32, MOVZ_I_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;
defm : MovzPats2<GPR32, GPR32, MOVZ_I_MM, XORi_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;

defm : MovnPats<GPR32, GPR32, MOVN_I_MM, XOR_MM>, ISA_MICROMIPS32_NOT_MIPS32R6;

//===----------------------------------------------------------------------===//
// MicroMips instruction aliases
//===----------------------------------------------------------------------===//

class UncondBranchMMPseudo<string opstr> :
  MipsAsmPseudoInst<(outs), (ins brtarget_mm:$offset),
                    !strconcat(opstr, "\t$offset")>;

def B_MM_Pseudo : UncondBranchMMPseudo<"b">, ISA_MICROMIPS;

let EncodingPredicates = [InMicroMips] in {
  def SDIV_MM_Pseudo : MultDivPseudo<SDIV_MM, ACC64, GPR32Opnd, MipsDivRem,
                                     II_DIV, 0, 1, 1>, ISA_MIPS1_NOT_32R6_64R6;
  def UDIV_MM_Pseudo : MultDivPseudo<UDIV_MM, ACC64, GPR32Opnd, MipsDivRemU,
                                     II_DIVU, 0, 1, 1>, ISA_MIPS1_NOT_32R6_64R6;

  def : MipsInstAlias<"wait", (WAIT_MM 0x0), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"nop", (SLL_MM ZERO, ZERO, 0), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"nop", (MOVE16_MM ZERO, ZERO), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"ei", (EI_MM ZERO), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"di", (DI_MM ZERO), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"neg $rt, $rs",
                      (SUB_MM GPR32Opnd:$rt, ZERO, GPR32Opnd:$rs), 1>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"neg $rt",
                      (SUB_MM GPR32Opnd:$rt, ZERO, GPR32Opnd:$rt), 1>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"negu $rt, $rs",
                      (SUBu_MM GPR32Opnd:$rt, ZERO, GPR32Opnd:$rs), 1>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"negu $rt",
                      (SUBu_MM GPR32Opnd:$rt, ZERO, GPR32Opnd:$rt), 1>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"teq $rs, $rt",
                      (TEQ_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<"tge $rs, $rt",
                      (TGE_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<"tgeu $rs, $rt",
                      (TGEU_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<"tlt $rs, $rt",
                      (TLT_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<"tltu $rs, $rt",
                      (TLTU_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<"tne $rs, $rt",
                      (TNE_MM GPR32Opnd:$rs, GPR32Opnd:$rt, 0), 1>;
  def : MipsInstAlias<
          "sgt $rd, $rs, $rt",
          (SLT_MM GPR32Opnd:$rd, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<
          "sgt $rs, $rt",
          (SLT_MM GPR32Opnd:$rs, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<
          "sgtu $rd, $rs, $rt",
          (SLTu_MM GPR32Opnd:$rd, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<
          "sgtu $rs, $rt",
          (SLTu_MM GPR32Opnd:$rs, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<"sll $rd, $rt, $rs",
                      (SLLV_MM GPR32Opnd:$rd, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<"sra $rd, $rt, $rs",
                      (SRAV_MM GPR32Opnd:$rd, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<"srl $rd, $rt, $rs",
                      (SRLV_MM GPR32Opnd:$rd, GPR32Opnd:$rt, GPR32Opnd:$rs), 0>;
  def : MipsInstAlias<"sll $rd, $rt",
                      (SLLV_MM GPR32Opnd:$rd, GPR32Opnd:$rd, GPR32Opnd:$rt), 0>;
  def : MipsInstAlias<"sra $rd, $rt",
                      (SRAV_MM GPR32Opnd:$rd, GPR32Opnd:$rd, GPR32Opnd:$rt), 0>;
  def : MipsInstAlias<"srl $rd, $rt",
                      (SRLV_MM GPR32Opnd:$rd, GPR32Opnd:$rd, GPR32Opnd:$rt), 0>;
  def : MipsInstAlias<"sll $rd, $shamt",
                      (SLL_MM GPR32Opnd:$rd, GPR32Opnd:$rd, uimm5:$shamt), 0>;
  def : MipsInstAlias<"sra $rd, $shamt",
                      (SRA_MM GPR32Opnd:$rd, GPR32Opnd:$rd, uimm5:$shamt), 0>;
  def : MipsInstAlias<"srl $rd, $shamt",
                      (SRL_MM GPR32Opnd:$rd, GPR32Opnd:$rd, uimm5:$shamt), 0>;
  def : MipsInstAlias<"rotr $rt, $imm",
                      (ROTR_MM GPR32Opnd:$rt, GPR32Opnd:$rt, uimm5:$imm), 0>;
  def : MipsInstAlias<"syscall", (SYSCALL_MM 0), 1>, ISA_MICROMIPS;

  def : MipsInstAlias<"sync", (SYNC_MM 0), 1>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"add", ADDi_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"addu", ADDiu_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"and", ANDi_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"or", ORi_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"xor", XORi_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"slt", SLTi_MM>, ISA_MICROMIPS;

  defm : OneOrTwoOperandMacroImmediateAlias<"sltu", SLTiu_MM>, ISA_MICROMIPS;

  def : MipsInstAlias<"not $rt, $rs",
                      (NOR_MM GPR32Opnd:$rt, GPR32Opnd:$rs, ZERO), 0>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"not $rt",
                      (NOR_MM GPR32Opnd:$rt, GPR32Opnd:$rt, ZERO), 0>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
  def : MipsInstAlias<"bnez $rs,$offset",
                      (BNE_MM GPR32Opnd:$rs, ZERO, brtarget:$offset), 0>,
        ISA_MICROMIPS;
  def : MipsInstAlias<"beqz $rs,$offset",
                      (BEQ_MM GPR32Opnd:$rs, ZERO, brtarget:$offset), 0>,
        ISA_MICROMIPS;
  def : MipsInstAlias<"seh $rd", (SEH_MM GPR32Opnd:$rd, GPR32Opnd:$rd), 0>,
                     ISA_MICROMIPS;
  def : MipsInstAlias<"seb $rd", (SEB_MM GPR32Opnd:$rd, GPR32Opnd:$rd), 0>,
                     ISA_MICROMIPS;
  def : MipsInstAlias<"break", (BREAK_MM 0, 0), 1>, ISA_MICROMIPS;
  def : MipsInstAlias<"break $imm", (BREAK_MM uimm10:$imm, 0), 1>,
        ISA_MICROMIPS;
  def : MipsInstAlias<"bal $offset", (BGEZAL_MM ZERO, brtarget_mm:$offset), 1>,
        ISA_MICROMIPS32_NOT_MIPS32R6;

  def : MipsInstAlias<"j $rs", (JR_MM GPR32Opnd:$rs), 0>,
        ISA_MICROMIPS32_NOT_MIPS32R6;
}
def : MipsInstAlias<"rdhwr $rt, $rs",
                    (RDHWR_MM GPR32Opnd:$rt, HWRegsOpnd:$rs, 0), 1>,
      ISA_MICROMIPS32_NOT_MIPS32R6;

def : MipsInstAlias<"hypcall", (HYPCALL_MM 0), 1>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
def : MipsInstAlias<"mfgc0 $rt, $rs",
                    (MFGC0_MM GPR32Opnd:$rt, COP0Opnd:$rs, 0), 0>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
def : MipsInstAlias<"mfhgc0 $rt, $rs",
                    (MFHGC0_MM GPR32Opnd:$rt, COP0Opnd:$rs, 0), 0>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
def : MipsInstAlias<"mtgc0 $rt, $rs",
                    (MTGC0_MM COP0Opnd:$rs, GPR32Opnd:$rt, 0), 0>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
def : MipsInstAlias<"mthgc0 $rt, $rs",
                    (MTHGC0_MM COP0Opnd:$rs, GPR32Opnd:$rt, 0), 0>,
                    ISA_MICROMIPS32R5, ASE_VIRT;
