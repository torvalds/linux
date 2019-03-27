//===-- ARMInstrFormats.td - ARM Instruction Formats -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// ARM Instruction Format Definitions.
//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<6> val> {
  bits<6> Value = val;
}

def Pseudo        : Format<0>;
def MulFrm        : Format<1>;
def BrFrm         : Format<2>;
def BrMiscFrm     : Format<3>;

def DPFrm         : Format<4>;
def DPSoRegRegFrm    : Format<5>;

def LdFrm         : Format<6>;
def StFrm         : Format<7>;
def LdMiscFrm     : Format<8>;
def StMiscFrm     : Format<9>;
def LdStMulFrm    : Format<10>;

def LdStExFrm     : Format<11>;

def ArithMiscFrm  : Format<12>;
def SatFrm        : Format<13>;
def ExtFrm        : Format<14>;

def VFPUnaryFrm   : Format<15>;
def VFPBinaryFrm  : Format<16>;
def VFPConv1Frm   : Format<17>;
def VFPConv2Frm   : Format<18>;
def VFPConv3Frm   : Format<19>;
def VFPConv4Frm   : Format<20>;
def VFPConv5Frm   : Format<21>;
def VFPLdStFrm    : Format<22>;
def VFPLdStMulFrm : Format<23>;
def VFPMiscFrm    : Format<24>;

def ThumbFrm      : Format<25>;
def MiscFrm       : Format<26>;

def NGetLnFrm     : Format<27>;
def NSetLnFrm     : Format<28>;
def NDupFrm       : Format<29>;
def NLdStFrm      : Format<30>;
def N1RegModImmFrm: Format<31>;
def N2RegFrm      : Format<32>;
def NVCVTFrm      : Format<33>;
def NVDupLnFrm    : Format<34>;
def N2RegVShLFrm  : Format<35>;
def N2RegVShRFrm  : Format<36>;
def N3RegFrm      : Format<37>;
def N3RegVShFrm   : Format<38>;
def NVExtFrm      : Format<39>;
def NVMulSLFrm    : Format<40>;
def NVTBLFrm      : Format<41>;
def DPSoRegImmFrm  : Format<42>;
def N3RegCplxFrm  : Format<43>;

// Misc flags.

// The instruction has an Rn register operand.
// UnaryDP - Indicates this is a unary data processing instruction, i.e.
// it doesn't have a Rn operand.
class UnaryDP    { bit isUnaryDataProc = 1; }

// Xform16Bit - Indicates this Thumb2 instruction may be transformed into
// a 16-bit Thumb instruction if certain conditions are met.
class Xform16Bit { bit canXformTo16Bit = 1; }

//===----------------------------------------------------------------------===//
// ARM Instruction flags.  These need to match ARMBaseInstrInfo.h.
//

// FIXME: Once the JIT is MC-ized, these can go away.
// Addressing mode.
class AddrMode<bits<5> val> {
  bits<5> Value = val;
}
def AddrModeNone    : AddrMode<0>;
def AddrMode1       : AddrMode<1>;
def AddrMode2       : AddrMode<2>;
def AddrMode3       : AddrMode<3>;
def AddrMode4       : AddrMode<4>;
def AddrMode5       : AddrMode<5>;
def AddrMode6       : AddrMode<6>;
def AddrModeT1_1    : AddrMode<7>;
def AddrModeT1_2    : AddrMode<8>;
def AddrModeT1_4    : AddrMode<9>;
def AddrModeT1_s    : AddrMode<10>;
def AddrModeT2_i12  : AddrMode<11>;
def AddrModeT2_i8   : AddrMode<12>;
def AddrModeT2_so   : AddrMode<13>;
def AddrModeT2_pc   : AddrMode<14>;
def AddrModeT2_i8s4 : AddrMode<15>;
def AddrMode_i12    : AddrMode<16>;
def AddrMode5FP16   : AddrMode<17>;
def AddrModeT2_ldrex : AddrMode<18>;

// Load / store index mode.
class IndexMode<bits<2> val> {
  bits<2> Value = val;
}
def IndexModeNone : IndexMode<0>;
def IndexModePre  : IndexMode<1>;
def IndexModePost : IndexMode<2>;
def IndexModeUpd  : IndexMode<3>;

// Instruction execution domain.
class Domain<bits<3> val> {
  bits<3> Value = val;
}
def GenericDomain : Domain<0>;
def VFPDomain     : Domain<1>; // Instructions in VFP domain only
def NeonDomain    : Domain<2>; // Instructions in Neon domain only
def VFPNeonDomain : Domain<3>; // Instructions in both VFP & Neon domains
def VFPNeonA8Domain : Domain<5>; // Instructions in VFP & Neon under A8

//===----------------------------------------------------------------------===//
// ARM special operands.
//

// ARM imod and iflag operands, used only by the CPS instruction.
def imod_op : Operand<i32> {
  let PrintMethod = "printCPSIMod";
}

def ProcIFlagsOperand : AsmOperandClass {
  let Name = "ProcIFlags";
  let ParserMethod = "parseProcIFlagsOperand";
}
def iflags_op : Operand<i32> {
  let PrintMethod = "printCPSIFlag";
  let ParserMatchClass = ProcIFlagsOperand;
}

// ARM Predicate operand. Default to 14 = always (AL). Second part is CC
// register whose default is 0 (no register).
def CondCodeOperand : AsmOperandClass { let Name = "CondCode"; }
def pred : PredicateOperand<OtherVT, (ops i32imm, i32imm),
                                     (ops (i32 14), (i32 zero_reg))> {
  let PrintMethod = "printPredicateOperand";
  let ParserMatchClass = CondCodeOperand;
  let DecoderMethod = "DecodePredicateOperand";
}

// Selectable predicate operand for CMOV instructions. We can't use a normal
// predicate because the default values interfere with instruction selection. In
// all other respects it is identical though: pseudo-instruction expansion
// relies on the MachineOperands being compatible.
def cmovpred : Operand<i32>, PredicateOp,
               ComplexPattern<i32, 2, "SelectCMOVPred"> {
  let MIOperandInfo = (ops i32imm, i32imm);
  let PrintMethod = "printPredicateOperand";
}

// Conditional code result for instructions whose 's' bit is set, e.g. subs.
def CCOutOperand : AsmOperandClass { let Name = "CCOut"; }
def cc_out : OptionalDefOperand<OtherVT, (ops CCR), (ops (i32 zero_reg))> {
  let EncoderMethod = "getCCOutOpValue";
  let PrintMethod = "printSBitModifierOperand";
  let ParserMatchClass = CCOutOperand;
  let DecoderMethod = "DecodeCCOutOperand";
}

// Same as cc_out except it defaults to setting CPSR.
def s_cc_out : OptionalDefOperand<OtherVT, (ops CCR), (ops (i32 CPSR))> {
  let EncoderMethod = "getCCOutOpValue";
  let PrintMethod = "printSBitModifierOperand";
  let ParserMatchClass = CCOutOperand;
  let DecoderMethod = "DecodeCCOutOperand";
}

// ARM special operands for disassembly only.
//
def SetEndAsmOperand : ImmAsmOperand<0,1> {
  let Name = "SetEndImm";
  let ParserMethod = "parseSetEndImm";
}
def setend_op : Operand<i32> {
  let PrintMethod = "printSetendOperand";
  let ParserMatchClass = SetEndAsmOperand;
}

def MSRMaskOperand : AsmOperandClass {
  let Name = "MSRMask";
  let ParserMethod = "parseMSRMaskOperand";
}
def msr_mask : Operand<i32> {
  let PrintMethod = "printMSRMaskOperand";
  let DecoderMethod = "DecodeMSRMask";
  let ParserMatchClass = MSRMaskOperand;
}

def BankedRegOperand : AsmOperandClass {
  let Name = "BankedReg";
  let ParserMethod = "parseBankedRegOperand";
}
def banked_reg : Operand<i32> {
  let PrintMethod = "printBankedRegOperand";
  let DecoderMethod = "DecodeBankedReg";
  let ParserMatchClass = BankedRegOperand;
}

// Shift Right Immediate - A shift right immediate is encoded differently from
// other shift immediates. The imm6 field is encoded like so:
//
//    Offset    Encoding
//     8        imm6<5:3> = '001', 8 - <imm> is encoded in imm6<2:0>
//     16       imm6<5:4> = '01', 16 - <imm> is encoded in imm6<3:0>
//     32       imm6<5> = '1', 32 - <imm> is encoded in imm6<4:0>
//     64       64 - <imm> is encoded in imm6<5:0>
def shr_imm8_asm_operand : ImmAsmOperand<1,8> { let Name = "ShrImm8"; }
def shr_imm8  : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm <= 8; }]> {
  let EncoderMethod = "getShiftRight8Imm";
  let DecoderMethod = "DecodeShiftRight8Imm";
  let ParserMatchClass = shr_imm8_asm_operand;
}
def shr_imm16_asm_operand : ImmAsmOperand<1,16> { let Name = "ShrImm16"; }
def shr_imm16 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm <= 16; }]> {
  let EncoderMethod = "getShiftRight16Imm";
  let DecoderMethod = "DecodeShiftRight16Imm";
  let ParserMatchClass = shr_imm16_asm_operand;
}
def shr_imm32_asm_operand : ImmAsmOperand<1,32> { let Name = "ShrImm32"; }
def shr_imm32 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm <= 32; }]> {
  let EncoderMethod = "getShiftRight32Imm";
  let DecoderMethod = "DecodeShiftRight32Imm";
  let ParserMatchClass = shr_imm32_asm_operand;
}
def shr_imm64_asm_operand : ImmAsmOperand<1,64> { let Name = "ShrImm64"; }
def shr_imm64 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm <= 64; }]> {
  let EncoderMethod = "getShiftRight64Imm";
  let DecoderMethod = "DecodeShiftRight64Imm";
  let ParserMatchClass = shr_imm64_asm_operand;
}


// ARM Assembler operand for ldr Rd, =expression which generates an offset
// to a constant pool entry or a MOV depending on the value of expression
def const_pool_asm_operand : AsmOperandClass { let Name = "ConstPoolAsmImm"; }
def const_pool_asm_imm : Operand<i32> {
  let ParserMatchClass = const_pool_asm_operand;
}


//===----------------------------------------------------------------------===//
// ARM Assembler alias templates.
//
// Note: When EmitPriority == 1, the alias will be used for printing
class ARMInstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[IsARM]>;
class ARMInstSubst<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>,
        Requires<[IsARM,UseNegativeImmediates]>;
class  tInstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[IsThumb]>;
class  tInstSubst<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>,
        Requires<[IsThumb,UseNegativeImmediates]>;
class t2InstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[IsThumb2]>;
class t2InstSubst<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>,
        Requires<[IsThumb2,UseNegativeImmediates]>;
class VFP2InstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[HasVFP2]>;
class VFP2DPInstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[HasVFP2,HasDPVFP]>;
class VFP3InstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[HasVFP3]>;
class NEONInstAlias<string Asm, dag Result, bit EmitPriority = 0>
      : InstAlias<Asm, Result, EmitPriority>, Requires<[HasNEON]>;


class VFP2MnemonicAlias<string src, string dst> : MnemonicAlias<src, dst>,
          Requires<[HasVFP2]>;
class NEONMnemonicAlias<string src, string dst> : MnemonicAlias<src, dst>,
          Requires<[HasNEON]>;

//===----------------------------------------------------------------------===//
// ARM Instruction templates.
//


class InstTemplate<AddrMode am, int sz, IndexMode im,
                   Format f, Domain d, string cstr, InstrItinClass itin>
  : Instruction {
  let Namespace = "ARM";

  AddrMode AM = am;
  int Size = sz;
  IndexMode IM = im;
  bits<2> IndexModeBits = IM.Value;
  Format F = f;
  bits<6> Form = F.Value;
  Domain D = d;
  bit isUnaryDataProc = 0;
  bit canXformTo16Bit = 0;
  // The instruction is a 16-bit flag setting Thumb instruction. Used
  // by the parser to determine whether to require the 'S' suffix on the
  // mnemonic (when not in an IT block) or preclude it (when in an IT block).
  bit thumbArithFlagSetting = 0;

  // If this is a pseudo instruction, mark it isCodeGenOnly.
  let isCodeGenOnly = !eq(!cast<string>(f), "Pseudo");

  // The layout of TSFlags should be kept in sync with ARMBaseInfo.h.
  let TSFlags{4-0}   = AM.Value;
  let TSFlags{6-5}   = IndexModeBits;
  let TSFlags{12-7} = Form;
  let TSFlags{13}    = isUnaryDataProc;
  let TSFlags{14}    = canXformTo16Bit;
  let TSFlags{17-15} = D.Value;
  let TSFlags{18}    = thumbArithFlagSetting;

  let Constraints = cstr;
  let Itinerary = itin;
}

class Encoding {
  field bits<32> Inst;
  // Mask of bits that cause an encoding to be UNPREDICTABLE.
  // If a bit is set, then if the corresponding bit in the
  // target encoding differs from its value in the "Inst" field,
  // the instruction is UNPREDICTABLE (SoftFail in abstract parlance).
  field bits<32> Unpredictable = 0;
  // SoftFail is the generic name for this field, but we alias it so
  // as to make it more obvious what it means in ARM-land.
  field bits<32> SoftFail = Unpredictable;
}

class InstARM<AddrMode am, int sz, IndexMode im,
              Format f, Domain d, string cstr, InstrItinClass itin>
  : InstTemplate<am, sz, im, f, d, cstr, itin>, Encoding {
  let DecoderNamespace = "ARM";
}

// This Encoding-less class is used by Thumb1 to specify the encoding bits later
// on by adding flavors to specific instructions.
class InstThumb<AddrMode am, int sz, IndexMode im,
                Format f, Domain d, string cstr, InstrItinClass itin>
  : InstTemplate<am, sz, im, f, d, cstr, itin> {
  let DecoderNamespace = "Thumb";
}

// Pseudo-instructions for alternate assembly syntax (never used by codegen).
// These are aliases that require C++ handling to convert to the target
// instruction, while InstAliases can be handled directly by tblgen.
class AsmPseudoInst<string asm, dag iops, dag oops = (outs)>
  : InstTemplate<AddrModeNone, 0, IndexModeNone, Pseudo, GenericDomain,
                 "", NoItinerary> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let Pattern = [];
  let isCodeGenOnly = 0; // So we get asm matcher for it.
  let AsmString = asm;
  let isPseudo = 1;
}

class ARMAsmPseudo<string asm, dag iops, dag oops = (outs)>
  : AsmPseudoInst<asm, iops, oops>, Requires<[IsARM]>;
class tAsmPseudo<string asm, dag iops, dag oops = (outs)>
  : AsmPseudoInst<asm, iops, oops>, Requires<[IsThumb]>;
class t2AsmPseudo<string asm, dag iops, dag oops = (outs)>
  : AsmPseudoInst<asm, iops, oops>, Requires<[IsThumb2]>;
class VFP2AsmPseudo<string asm, dag iops, dag oops = (outs)>
  : AsmPseudoInst<asm, iops, oops>, Requires<[HasVFP2]>;
class NEONAsmPseudo<string asm, dag iops, dag oops = (outs)>
  : AsmPseudoInst<asm, iops, oops>, Requires<[HasNEON]>;

// Pseudo instructions for the code generator.
class PseudoInst<dag oops, dag iops, InstrItinClass itin, list<dag> pattern>
  : InstTemplate<AddrModeNone, 0, IndexModeNone, Pseudo,
                 GenericDomain, "", itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let Pattern = pattern;
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

// PseudoInst that's ARM-mode only.
class ARMPseudoInst<dag oops, dag iops, int sz, InstrItinClass itin,
                    list<dag> pattern>
  : PseudoInst<oops, iops, itin, pattern> {
  let Size = sz;
  list<Predicate> Predicates = [IsARM];
}

// PseudoInst that's Thumb-mode only.
class tPseudoInst<dag oops, dag iops, int sz, InstrItinClass itin,
                    list<dag> pattern>
  : PseudoInst<oops, iops, itin, pattern> {
  let Size = sz;
  list<Predicate> Predicates = [IsThumb];
}

// PseudoInst that's in ARMv8-M baseline (Somewhere between Thumb and Thumb2)
class t2basePseudoInst<dag oops, dag iops, int sz, InstrItinClass itin,
                    list<dag> pattern>
  : PseudoInst<oops, iops, itin, pattern> {
  let Size = sz;
  list<Predicate> Predicates = [IsThumb,HasV8MBaseline];
}

// PseudoInst that's Thumb2-mode only.
class t2PseudoInst<dag oops, dag iops, int sz, InstrItinClass itin,
                    list<dag> pattern>
  : PseudoInst<oops, iops, itin, pattern> {
  let Size = sz;
  list<Predicate> Predicates = [IsThumb2];
}

class ARMPseudoExpand<dag oops, dag iops, int sz,
                      InstrItinClass itin, list<dag> pattern,
                      dag Result>
  : ARMPseudoInst<oops, iops, sz, itin, pattern>,
    PseudoInstExpansion<Result>;

class tPseudoExpand<dag oops, dag iops, int sz,
                    InstrItinClass itin, list<dag> pattern,
                    dag Result>
  : tPseudoInst<oops, iops, sz, itin, pattern>,
    PseudoInstExpansion<Result>;

class t2PseudoExpand<dag oops, dag iops, int sz,
                    InstrItinClass itin, list<dag> pattern,
                    dag Result>
  : t2PseudoInst<oops, iops, sz, itin, pattern>,
    PseudoInstExpansion<Result>;

// Almost all ARM instructions are predicable.
class I<dag oops, dag iops, AddrMode am, int sz,
        IndexMode im, Format f, InstrItinClass itin,
        string opc, string asm, string cstr,
        list<dag> pattern>
  : InstARM<am, sz, im, f, GenericDomain, cstr, itin> {
  bits<4> p;
  let Inst{31-28} = p;
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsARM];
}

// A few are not predicable
class InoP<dag oops, dag iops, AddrMode am, int sz,
           IndexMode im, Format f, InstrItinClass itin,
           string opc, string asm, string cstr,
           list<dag> pattern>
  : InstARM<am, sz, im, f, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = !strconcat(opc, asm);
  let Pattern = pattern;
  let isPredicable = 0;
  list<Predicate> Predicates = [IsARM];
}

// Same as I except it can optionally modify CPSR. Note it's modeled as an input
// operand since by default it's a zero register. It will become an implicit def
// once it's "flipped".
class sI<dag oops, dag iops, AddrMode am, int sz,
         IndexMode im, Format f, InstrItinClass itin,
         string opc, string asm, string cstr,
         list<dag> pattern>
  : InstARM<am, sz, im, f, GenericDomain, cstr, itin> {
  bits<4> p; // Predicate operand
  bits<1> s; // condition-code set flag ('1' if the insn should set the flags)
  let Inst{31-28} = p;
  let Inst{20} = s;

  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p, cc_out:$s));
  let AsmString = !strconcat(opc, "${s}${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsARM];
}

// Special cases
class XI<dag oops, dag iops, AddrMode am, int sz,
         IndexMode im, Format f, InstrItinClass itin,
         string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, im, f, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  list<Predicate> Predicates = [IsARM];
}

class AI<dag oops, dag iops, Format f, InstrItinClass itin,
         string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, f, itin,
      opc, asm, "", pattern>;
class AsI<dag oops, dag iops, Format f, InstrItinClass itin,
          string opc, string asm, list<dag> pattern>
  : sI<oops, iops, AddrModeNone, 4, IndexModeNone, f, itin,
       opc, asm, "", pattern>;
class AXI<dag oops, dag iops, Format f, InstrItinClass itin,
          string asm, list<dag> pattern>
  : XI<oops, iops, AddrModeNone, 4, IndexModeNone, f, itin,
       asm, "", pattern>;
class AXIM<dag oops, dag iops, AddrMode am, Format f, InstrItinClass itin,
          string asm, list<dag> pattern>
  : XI<oops, iops, am, 4, IndexModeNone, f, itin,
       asm, "", pattern>;
class AInoP<dag oops, dag iops, Format f, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : InoP<oops, iops, AddrModeNone, 4, IndexModeNone, f, itin,
         opc, asm, "", pattern>;

// Ctrl flow instructions
class ABI<bits<4> opcod, dag oops, dag iops, InstrItinClass itin,
          string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, BrFrm, itin,
      opc, asm, "", pattern> {
  let Inst{27-24} = opcod;
}
class ABXI<bits<4> opcod, dag oops, dag iops, InstrItinClass itin,
           string asm, list<dag> pattern>
  : XI<oops, iops, AddrModeNone, 4, IndexModeNone, BrFrm, itin,
       asm, "", pattern> {
  let Inst{27-24} = opcod;
}

// BR_JT instructions
class JTI<dag oops, dag iops, InstrItinClass itin,
          string asm, list<dag> pattern>
  : XI<oops, iops, AddrModeNone, 0, IndexModeNone, BrMiscFrm, itin,
       asm, "", pattern>;

class AIldr_ex_or_acq<bits<2> opcod, bits<2> opcod2, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, LdStExFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rt;
  bits<4> addr;
  let Inst{27-23} = 0b00011;
  let Inst{22-21} = opcod;
  let Inst{20}    = 1;
  let Inst{19-16} = addr;
  let Inst{15-12} = Rt;
  let Inst{11-10} = 0b11;
  let Inst{9-8}   = opcod2;
  let Inst{7-0}   = 0b10011111;
}
class AIstr_ex_or_rel<bits<2> opcod, bits<2> opcod2, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, LdStExFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rt;
  bits<4> addr;
  let Inst{27-23} = 0b00011;
  let Inst{22-21} = opcod;
  let Inst{20}    = 0;
  let Inst{19-16} = addr;
  let Inst{11-10} = 0b11;
  let Inst{9-8}   = opcod2;
  let Inst{7-4}   = 0b1001;
  let Inst{3-0}   = Rt;
}
// Atomic load/store instructions
class AIldrex<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIldr_ex_or_acq<opcod, 0b11, oops, iops, itin, opc, asm, pattern>;

class AIstrex<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIstr_ex_or_rel<opcod, 0b11, oops, iops, itin, opc, asm, pattern> {
  bits<4> Rd;
  let Inst{15-12} = Rd;
}

// Exclusive load/store instructions

class AIldaex<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIldr_ex_or_acq<opcod, 0b10, oops, iops, itin, opc, asm, pattern>,
    Requires<[IsARM, HasAcquireRelease, HasV7Clrex]>;

class AIstlex<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIstr_ex_or_rel<opcod, 0b10, oops, iops, itin, opc, asm, pattern>,
    Requires<[IsARM, HasAcquireRelease, HasV7Clrex]> {
  bits<4> Rd;
  let Inst{15-12} = Rd;
}

class AIswp<bit b, dag oops, dag iops, string opc, list<dag> pattern>
  : AI<oops, iops, MiscFrm, NoItinerary, opc, "\t$Rt, $Rt2, $addr", pattern> {
  bits<4> Rt;
  bits<4> Rt2;
  bits<4> addr;
  let Inst{27-23} = 0b00010;
  let Inst{22} = b;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = addr;
  let Inst{15-12} = Rt;
  let Inst{11-4} = 0b00001001;
  let Inst{3-0} = Rt2;

  let Unpredictable{11-8} = 0b1111;
  let DecoderMethod = "DecodeSwap";
}
// Acquire/Release load/store instructions
class AIldracq<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIldr_ex_or_acq<opcod, 0b00, oops, iops, itin, opc, asm, pattern>,
    Requires<[IsARM, HasAcquireRelease]>;

class AIstrrel<bits<2> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : AIstr_ex_or_rel<opcod, 0b00, oops, iops, itin, opc, asm, pattern>,
    Requires<[IsARM, HasAcquireRelease]> {
  let Inst{15-12}   = 0b1111;
}

// addrmode1 instructions
class AI1<bits<4> opcod, dag oops, dag iops, Format f, InstrItinClass itin,
          string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrMode1, 4, IndexModeNone, f, itin,
      opc, asm, "", pattern> {
  let Inst{24-21} = opcod;
  let Inst{27-26} = 0b00;
}
class AsI1<bits<4> opcod, dag oops, dag iops, Format f, InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : sI<oops, iops, AddrMode1, 4, IndexModeNone, f, itin,
       opc, asm, "", pattern> {
  let Inst{24-21} = opcod;
  let Inst{27-26} = 0b00;
}
class AXI1<bits<4> opcod, dag oops, dag iops, Format f, InstrItinClass itin,
           string asm, list<dag> pattern>
  : XI<oops, iops, AddrMode1, 4, IndexModeNone, f, itin,
       asm, "", pattern> {
  let Inst{24-21} = opcod;
  let Inst{27-26} = 0b00;
}

// loads

// LDR/LDRB/STR/STRB/...
class AI2ldst<bits<3> op, bit isLd, bit isByte, dag oops, dag iops, AddrMode am,
             Format f, InstrItinClass itin, string opc, string asm,
             list<dag> pattern>
  : I<oops, iops, am, 4, IndexModeNone, f, itin, opc, asm,
      "", pattern> {
  let Inst{27-25} = op;
  let Inst{24} = 1;  // 24 == P
  // 23 == U
  let Inst{22} = isByte;
  let Inst{21} = 0;  // 21 == W
  let Inst{20} = isLd;
}
// Indexed load/stores
class AI2ldstidx<bit isLd, bit isByte, bit isPre, dag oops, dag iops,
                IndexMode im, Format f, InstrItinClass itin, string opc,
                string asm, string cstr, list<dag> pattern>
  : I<oops, iops, AddrMode2, 4, im, f, itin,
      opc, asm, cstr, pattern> {
  bits<4> Rt;
  let Inst{27-26} = 0b01;
  let Inst{24}    = isPre; // P bit
  let Inst{22}    = isByte; // B bit
  let Inst{21}    = isPre; // W bit
  let Inst{20}    = isLd; // L bit
  let Inst{15-12} = Rt;
}
class AI2stridx_reg<bit isByte, bit isPre, dag oops, dag iops,
                IndexMode im, Format f, InstrItinClass itin, string opc,
                string asm, string cstr, list<dag> pattern>
  : AI2ldstidx<0, isByte, isPre, oops, iops, im, f, itin, opc, asm, cstr,
               pattern> {
  // AM2 store w/ two operands: (GPR, am2offset)
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> Rn;
  let Inst{25} = 1;
  let Inst{23} = offset{12};
  let Inst{19-16} = Rn;
  let Inst{11-5} = offset{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = offset{3-0};
}

class AI2stridx_imm<bit isByte, bit isPre, dag oops, dag iops,
                IndexMode im, Format f, InstrItinClass itin, string opc,
                string asm, string cstr, list<dag> pattern>
  : AI2ldstidx<0, isByte, isPre, oops, iops, im, f, itin, opc, asm, cstr,
               pattern> {
  // AM2 store w/ two operands: (GPR, am2offset)
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> Rn;
  let Inst{25} = 0;
  let Inst{23} = offset{12};
  let Inst{19-16} = Rn;
  let Inst{11-0} = offset{11-0};
}


// FIXME: Merge with the above class when addrmode2 gets used for STR, STRB
// but for now use this class for STRT and STRBT.
class AI2stridxT<bit isByte, bit isPre, dag oops, dag iops,
                IndexMode im, Format f, InstrItinClass itin, string opc,
                string asm, string cstr, list<dag> pattern>
  : AI2ldstidx<0, isByte, isPre, oops, iops, im, f, itin, opc, asm, cstr,
               pattern> {
  // AM2 store w/ two operands: (GPR, am2offset)
  // {17-14}  Rn
  // {13}     1 == Rm, 0 == imm12
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<18> addr;
  let Inst{25} = addr{13};
  let Inst{23} = addr{12};
  let Inst{19-16} = addr{17-14};
  let Inst{11-0} = addr{11-0};
}

// addrmode3 instructions
class AI3ld<bits<4> op, bit op20, dag oops, dag iops, Format f,
            InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrMode3, 4, IndexModeNone, f, itin,
      opc, asm, "", pattern> {
  bits<14> addr;
  bits<4> Rt;
  let Inst{27-25} = 0b000;
  let Inst{24}    = 1;            // P bit
  let Inst{23}    = addr{8};      // U bit
  let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
  let Inst{21}    = 0;            // W bit
  let Inst{20}    = op20;         // L bit
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{15-12} = Rt;           // Rt
  let Inst{11-8}  = addr{7-4};    // imm7_4/zero
  let Inst{7-4}   = op;
  let Inst{3-0}   = addr{3-0};    // imm3_0/Rm

  let DecoderMethod = "DecodeAddrMode3Instruction";
}

class AI3ldstidx<bits<4> op, bit op20, bit isPre, dag oops, dag iops,
                IndexMode im, Format f, InstrItinClass itin, string opc,
                string asm, string cstr, list<dag> pattern>
  : I<oops, iops, AddrMode3, 4, im, f, itin,
      opc, asm, cstr, pattern> {
  bits<4> Rt;
  let Inst{27-25} = 0b000;
  let Inst{24}    = isPre;        // P bit
  let Inst{21}    = isPre;        // W bit
  let Inst{20}    = op20;         // L bit
  let Inst{15-12} = Rt;           // Rt
  let Inst{7-4}   = op;
}

// FIXME: Merge with the above class when addrmode2 gets used for LDR, LDRB
// but for now use this class for LDRSBT, LDRHT, LDSHT.
class AI3ldstidxT<bits<4> op, bit isLoad, dag oops, dag iops,
                  IndexMode im, Format f, InstrItinClass itin, string opc,
                  string asm, string cstr, list<dag> pattern>
  : I<oops, iops, AddrMode3, 4, im, f, itin, opc, asm, cstr, pattern> {
  // {13}     1 == imm8, 0 == Rm
  // {12-9}   Rn
  // {8}      isAdd
  // {7-4}    imm7_4/zero
  // {3-0}    imm3_0/Rm
  bits<4> addr;
  bits<4> Rt;
  let Inst{27-25} = 0b000;
  let Inst{24}    = 0;            // P bit
  let Inst{21}    = 1;
  let Inst{20}    = isLoad;       // L bit
  let Inst{19-16} = addr;         // Rn
  let Inst{15-12} = Rt;           // Rt
  let Inst{7-4}   = op;
}

// stores
class AI3str<bits<4> op, dag oops, dag iops, Format f, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrMode3, 4, IndexModeNone, f, itin,
      opc, asm, "", pattern> {
  bits<14> addr;
  bits<4> Rt;
  let Inst{27-25} = 0b000;
  let Inst{24}    = 1;            // P bit
  let Inst{23}    = addr{8};      // U bit
  let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
  let Inst{21}    = 0;            // W bit
  let Inst{20}    = 0;            // L bit
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{15-12} = Rt;           // Rt
  let Inst{11-8}  = addr{7-4};    // imm7_4/zero
  let Inst{7-4}   = op;
  let Inst{3-0}   = addr{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}

// addrmode4 instructions
class AXI4<dag oops, dag iops, IndexMode im, Format f, InstrItinClass itin,
           string asm, string cstr, list<dag> pattern>
  : XI<oops, iops, AddrMode4, 4, im, f, itin, asm, cstr, pattern> {
  bits<4>  p;
  bits<16> regs;
  bits<4>  Rn;
  let Inst{31-28} = p;
  let Inst{27-25} = 0b100;
  let Inst{22}    = 0; // S bit
  let Inst{19-16} = Rn;
  let Inst{15-0}  = regs;
}

// Unsigned multiply, multiply-accumulate instructions.
class AMul1I<bits<7> opcod, dag oops, dag iops, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, MulFrm, itin,
      opc, asm, "", pattern> {
  let Inst{7-4}   = 0b1001;
  let Inst{20}    = 0; // S bit
  let Inst{27-21} = opcod;
}
class AsMul1I<bits<7> opcod, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, list<dag> pattern>
  : sI<oops, iops, AddrModeNone, 4, IndexModeNone, MulFrm, itin,
       opc, asm, "", pattern> {
  let Inst{7-4}   = 0b1001;
  let Inst{27-21} = opcod;
}

// Most significant word multiply
class AMul2I<bits<7> opcod, bits<4> opc7_4, dag oops, dag iops,
             InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, MulFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  let Inst{7-4}   = opc7_4;
  let Inst{20}    = 1;
  let Inst{27-21} = opcod;
  let Inst{19-16} = Rd;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}
// MSW multiple w/ Ra operand
class AMul2Ia<bits<7> opcod, bits<4> opc7_4, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AMul2I<opcod, opc7_4, oops, iops, itin, opc, asm, pattern> {
  bits<4> Ra;
  let Inst{15-12} = Ra;
}

// SMUL<x><y> / SMULW<y> / SMLA<x><y> / SMLAW<x><y>
class AMulxyIbase<bits<7> opcod, bits<2> bit6_5, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, MulFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rn;
  bits<4> Rm;
  let Inst{4}     = 0;
  let Inst{7}     = 1;
  let Inst{20}    = 0;
  let Inst{27-21} = opcod;
  let Inst{6-5}   = bit6_5;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}
class AMulxyI<bits<7> opcod, bits<2> bit6_5, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AMulxyIbase<opcod, bit6_5, oops, iops, itin, opc, asm, pattern> {
  bits<4> Rd;
  let Inst{19-16} = Rd;
}

// AMulxyI with Ra operand
class AMulxyIa<bits<7> opcod, bits<2> bit6_5, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AMulxyI<opcod, bit6_5, oops, iops, itin, opc, asm, pattern> {
  bits<4> Ra;
  let Inst{15-12} = Ra;
}
// SMLAL*
class AMulxyI64<bits<7> opcod, bits<2> bit6_5, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AMulxyIbase<opcod, bit6_5, oops, iops, itin, opc, asm, pattern> {
  bits<4> RdLo;
  bits<4> RdHi;
  let Inst{19-16} = RdHi;
  let Inst{15-12} = RdLo;
}

// Extend instructions.
class AExtI<bits<8> opcod, dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, ExtFrm, itin,
      opc, asm, "", pattern> {
  // All AExtI instructions have Rd and Rm register operands.
  bits<4> Rd;
  bits<4> Rm;
  let Inst{15-12} = Rd;
  let Inst{3-0}   = Rm;
  let Inst{7-4}   = 0b0111;
  let Inst{9-8}   = 0b00;
  let Inst{27-20} = opcod;

  let Unpredictable{9-8} = 0b11;
}

// Misc Arithmetic instructions.
class AMiscA1I<bits<8> opcod, bits<4> opc7_4, dag oops, dag iops,
               InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, ArithMiscFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rd;
  bits<4> Rm;
  let Inst{27-20} = opcod;
  let Inst{19-16} = 0b1111;
  let Inst{15-12} = Rd;
  let Inst{11-8}  = 0b1111;
  let Inst{7-4}   = opc7_4;
  let Inst{3-0}   = Rm;
}

// Division instructions.
class ADivA1I<bits<3> opcod, dag oops, dag iops,
              InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, ArithMiscFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  let Inst{27-23} = 0b01110;
  let Inst{22-20} = opcod;
  let Inst{19-16} = Rd;
  let Inst{15-12} = 0b1111;
  let Inst{11-8}  = Rm;
  let Inst{7-4}   = 0b0001;
  let Inst{3-0}   = Rn;
}

// PKH instructions
def PKHLSLAsmOperand : ImmAsmOperand<0,31> {
  let Name = "PKHLSLImm";
  let ParserMethod = "parsePKHLSLImm";
}
def pkh_lsl_amt: Operand<i32>, ImmLeaf<i32, [{ return Imm >= 0 && Imm < 32; }]>{
  let PrintMethod = "printPKHLSLShiftImm";
  let ParserMatchClass = PKHLSLAsmOperand;
}
def PKHASRAsmOperand : AsmOperandClass {
  let Name = "PKHASRImm";
  let ParserMethod = "parsePKHASRImm";
}
def pkh_asr_amt: Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm <= 32; }]>{
  let PrintMethod = "printPKHASRShiftImm";
  let ParserMatchClass = PKHASRAsmOperand;
}

class APKHI<bits<8> opcod, bit tb, dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : I<oops, iops, AddrModeNone, 4, IndexModeNone, ArithMiscFrm, itin,
      opc, asm, "", pattern> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  bits<5> sh;
  let Inst{27-20} = opcod;
  let Inst{19-16} = Rn;
  let Inst{15-12} = Rd;
  let Inst{11-7}  = sh;
  let Inst{6}     = tb;
  let Inst{5-4}   = 0b01;
  let Inst{3-0}   = Rm;
}

//===----------------------------------------------------------------------===//

// ARMPat - Same as Pat<>, but requires that the compiler be in ARM mode.
class ARMPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsARM];
}
class ARMV5TPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsARM, HasV5T];
}
class ARMV5TEPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsARM, HasV5TE];
}
// ARMV5MOPat - Same as ARMV5TEPat with UseMulOps.
class ARMV5MOPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsARM, HasV5TE, UseMulOps];
}
class ARMV6Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsARM, HasV6];
}
class VFPPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [HasVFP2];
}
class VFPNoNEONPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [HasVFP2, DontUseNEONForFP];
}
class Thumb2DSPPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsThumb2, HasDSP];
}
class Thumb2DSPMulPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsThumb2, UseMulOps, HasDSP];
}
class FP16Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [HasFP16];
}
class FullFP16Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [HasFullFP16];
}
//===----------------------------------------------------------------------===//
// Thumb Instruction Format Definitions.
//

class ThumbI<dag oops, dag iops, AddrMode am, int sz,
             InstrItinClass itin, string asm, string cstr, list<dag> pattern>
  : InstThumb<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb];
}

// TI - Thumb instruction.
class TI<dag oops, dag iops, InstrItinClass itin, string asm, list<dag> pattern>
  : ThumbI<oops, iops, AddrModeNone, 2, itin, asm, "", pattern>;

// Two-address instructions
class TIt<dag oops, dag iops, InstrItinClass itin, string asm,
          list<dag> pattern>
  : ThumbI<oops, iops, AddrModeNone, 2, itin, asm, "$lhs = $dst",
           pattern>;

// tBL, tBX 32-bit instructions
class TIx2<bits<5> opcod1, bits<2> opcod2, bit opcod3,
           dag oops, dag iops, InstrItinClass itin, string asm,
           list<dag> pattern>
    : ThumbI<oops, iops, AddrModeNone, 4, itin, asm, "", pattern>,
      Encoding {
  let Inst{31-27} = opcod1;
  let Inst{15-14} = opcod2;
  let Inst{12}    = opcod3;
}

// BR_JT instructions
class TJTI<dag oops, dag iops, InstrItinClass itin, string asm,
           list<dag> pattern>
  : ThumbI<oops, iops, AddrModeNone, 0, itin, asm, "", pattern>;

// Thumb1 only
class Thumb1I<dag oops, dag iops, AddrMode am, int sz,
              InstrItinClass itin, string asm, string cstr, list<dag> pattern>
  : InstThumb<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb, IsThumb1Only];
}

class T1I<dag oops, dag iops, InstrItinClass itin,
          string asm, list<dag> pattern>
  : Thumb1I<oops, iops, AddrModeNone, 2, itin, asm, "", pattern>;
class T1Ix2<dag oops, dag iops, InstrItinClass itin,
            string asm, list<dag> pattern>
  : Thumb1I<oops, iops, AddrModeNone, 4, itin, asm, "", pattern>;

// Two-address instructions
class T1It<dag oops, dag iops, InstrItinClass itin,
           string asm, string cstr, list<dag> pattern>
  : Thumb1I<oops, iops, AddrModeNone, 2, itin,
            asm, cstr, pattern>;

// Thumb1 instruction that can either be predicated or set CPSR.
class Thumb1sI<dag oops, dag iops, AddrMode am, int sz,
               InstrItinClass itin,
               string opc, string asm, string cstr, list<dag> pattern>
  : InstThumb<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = !con(oops, (outs s_cc_out:$s));
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${s}${p}", asm);
  let Pattern = pattern;
  let thumbArithFlagSetting = 1;
  list<Predicate> Predicates = [IsThumb, IsThumb1Only];
  let DecoderNamespace = "ThumbSBit";
}

class T1sI<dag oops, dag iops, InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : Thumb1sI<oops, iops, AddrModeNone, 2, itin, opc, asm, "", pattern>;

// Two-address instructions
class T1sIt<dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : Thumb1sI<oops, iops, AddrModeNone, 2, itin, opc, asm,
             "$Rn = $Rdn", pattern>;

// Thumb1 instruction that can be predicated.
class Thumb1pI<dag oops, dag iops, AddrMode am, int sz,
               InstrItinClass itin,
               string opc, string asm, string cstr, list<dag> pattern>
  : InstThumb<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb, IsThumb1Only];
}

class T1pI<dag oops, dag iops, InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : Thumb1pI<oops, iops, AddrModeNone, 2, itin, opc, asm, "", pattern>;

// Two-address instructions
class T1pIt<dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : Thumb1pI<oops, iops, AddrModeNone, 2, itin, opc, asm,
             "$Rn = $Rdn", pattern>;

class T1pIs<dag oops, dag iops,
            InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : Thumb1pI<oops, iops, AddrModeT1_s, 2, itin, opc, asm, "", pattern>;

class Encoding16 : Encoding {
  let Inst{31-16} = 0x0000;
}

// A6.2 16-bit Thumb instruction encoding
class T1Encoding<bits<6> opcode> : Encoding16 {
  let Inst{15-10} = opcode;
}

// A6.2.1 Shift (immediate), add, subtract, move, and compare encoding.
class T1General<bits<5> opcode> : Encoding16 {
  let Inst{15-14} = 0b00;
  let Inst{13-9} = opcode;
}

// A6.2.2 Data-processing encoding.
class T1DataProcessing<bits<4> opcode> : Encoding16 {
  let Inst{15-10} = 0b010000;
  let Inst{9-6} = opcode;
}

// A6.2.3 Special data instructions and branch and exchange encoding.
class T1Special<bits<4> opcode> : Encoding16 {
  let Inst{15-10} = 0b010001;
  let Inst{9-6}   = opcode;
}

// A6.2.4 Load/store single data item encoding.
class T1LoadStore<bits<4> opA, bits<3> opB> : Encoding16 {
  let Inst{15-12} = opA;
  let Inst{11-9}  = opB;
}
class T1LdStSP<bits<3> opB>   : T1LoadStore<0b1001, opB>; // SP relative

class T1BranchCond<bits<4> opcode> : Encoding16 {
  let Inst{15-12} = opcode;
}

// Helper classes to encode Thumb1 loads and stores. For immediates, the
// following bits are used for "opA" (see A6.2.4):
//
//   0b0110 => Immediate, 4 bytes
//   0b1000 => Immediate, 2 bytes
//   0b0111 => Immediate, 1 byte
class T1pILdStEncode<bits<3> opcode, dag oops, dag iops, AddrMode am,
                     InstrItinClass itin, string opc, string asm,
                     list<dag> pattern>
  : Thumb1pI<oops, iops, am, 2, itin, opc, asm, "", pattern>,
    T1LoadStore<0b0101, opcode> {
  bits<3> Rt;
  bits<8> addr;
  let Inst{8-6} = addr{5-3};    // Rm
  let Inst{5-3} = addr{2-0};    // Rn
  let Inst{2-0} = Rt;
}
class T1pILdStEncodeImm<bits<4> opA, bit opB, dag oops, dag iops, AddrMode am,
                        InstrItinClass itin, string opc, string asm,
                        list<dag> pattern>
  : Thumb1pI<oops, iops, am, 2, itin, opc, asm, "", pattern>,
    T1LoadStore<opA, {opB,?,?}> {
  bits<3> Rt;
  bits<8> addr;
  let Inst{10-6} = addr{7-3};   // imm5
  let Inst{5-3}  = addr{2-0};   // Rn
  let Inst{2-0}  = Rt;
}

// A6.2.5 Miscellaneous 16-bit instructions encoding.
class T1Misc<bits<7> opcode> : Encoding16 {
  let Inst{15-12} = 0b1011;
  let Inst{11-5} = opcode;
}

// Thumb2I - Thumb2 instruction. Almost all Thumb2 instructions are predicable.
class Thumb2I<dag oops, dag iops, AddrMode am, int sz,
              InstrItinClass itin,
              string opc, string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb2];
  let DecoderNamespace = "Thumb2";
}

// Same as Thumb2I except it can optionally modify CPSR. Note it's modeled as an
// input operand since by default it's a zero register. It will become an
// implicit def once it's "flipped".
//
// FIXME: This uses unified syntax so {s} comes before {p}. We should make it
// more consistent.
class Thumb2sI<dag oops, dag iops, AddrMode am, int sz,
               InstrItinClass itin,
               string opc, string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  bits<1> s; // condition-code set flag ('1' if the insn should set the flags)
  let Inst{20} = s;

  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p, cc_out:$s));
  let AsmString = !strconcat(opc, "${s}${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb2];
  let DecoderNamespace = "Thumb2";
}

// Special cases
class Thumb2XI<dag oops, dag iops, AddrMode am, int sz,
               InstrItinClass itin,
               string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb2];
  let DecoderNamespace = "Thumb2";
}

class ThumbXI<dag oops, dag iops, AddrMode am, int sz,
              InstrItinClass itin,
              string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, IndexModeNone, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb, IsThumb1Only];
  let DecoderNamespace = "Thumb";
}

class T2I<dag oops, dag iops, InstrItinClass itin,
          string opc, string asm, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeNone, 4, itin, opc, asm, "", pattern>;
class T2Ii12<dag oops, dag iops, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_i12, 4, itin, opc, asm, "",pattern>;
class T2Ii8<dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_i8, 4, itin, opc, asm, "", pattern>;
class T2Iso<dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_so, 4, itin, opc, asm, "", pattern>;
class T2Ipc<dag oops, dag iops, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_pc, 4, itin, opc, asm, "", pattern>;
class T2Ii8s4<bit P, bit W, bit isLoad, dag oops, dag iops, InstrItinClass itin,
              string opc, string asm, string cstr, list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_i8s4, 4, itin, opc, asm, cstr,
            pattern> {
  bits<4> Rt;
  bits<4> Rt2;
  bits<13> addr;
  let Inst{31-25} = 0b1110100;
  let Inst{24}    = P;
  let Inst{23}    = addr{8};
  let Inst{22}    = 1;
  let Inst{21}    = W;
  let Inst{20}    = isLoad;
  let Inst{19-16} = addr{12-9};
  let Inst{15-12} = Rt{3-0};
  let Inst{11-8}  = Rt2{3-0};
  let Inst{7-0}   = addr{7-0};
}
class T2Ii8s4post<bit P, bit W, bit isLoad, dag oops, dag iops,
                  InstrItinClass itin, string opc, string asm, string cstr,
                  list<dag> pattern>
  : Thumb2I<oops, iops, AddrModeT2_i8s4, 4, itin, opc, asm, cstr,
            pattern> {
  bits<4> Rt;
  bits<4> Rt2;
  bits<4> addr;
  bits<9> imm;
  let Inst{31-25} = 0b1110100;
  let Inst{24}    = P;
  let Inst{23}    = imm{8};
  let Inst{22}    = 1;
  let Inst{21}    = W;
  let Inst{20}    = isLoad;
  let Inst{19-16} = addr;
  let Inst{15-12} = Rt{3-0};
  let Inst{11-8}  = Rt2{3-0};
  let Inst{7-0}   = imm{7-0};
}

class T2sI<dag oops, dag iops, InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : Thumb2sI<oops, iops, AddrModeNone, 4, itin, opc, asm, "", pattern>;

class T2XI<dag oops, dag iops, InstrItinClass itin,
           string asm, list<dag> pattern>
  : Thumb2XI<oops, iops, AddrModeNone, 4, itin, asm, "", pattern>;
class T2JTI<dag oops, dag iops, InstrItinClass itin,
            string asm, list<dag> pattern>
  : Thumb2XI<oops, iops, AddrModeNone, 0, itin, asm, "", pattern>;

// Move to/from coprocessor instructions
class T2Cop<bits<4> opc, dag oops, dag iops, string opcstr, string asm,
            list<dag> pattern>
  : T2I <oops, iops, NoItinerary, opcstr, asm, pattern>, Requires<[IsThumb2]> {
  let Inst{31-28} = opc;
}

// Two-address instructions
class T2XIt<dag oops, dag iops, InstrItinClass itin,
            string asm, string cstr, list<dag> pattern>
  : Thumb2XI<oops, iops, AddrModeNone, 4, itin, asm, cstr, pattern>;

// T2Ipreldst - Thumb2 pre-indexed load / store instructions.
class T2Ipreldst<bit signed, bits<2> opcod, bit load, bit pre,
                 dag oops, dag iops,
                 AddrMode am, IndexMode im, InstrItinClass itin,
                 string opc, string asm, string cstr, list<dag> pattern>
  : InstARM<am, 4, im, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb2];
  let DecoderNamespace = "Thumb2";

  bits<4> Rt;
  bits<13> addr;
  let Inst{31-27} = 0b11111;
  let Inst{26-25} = 0b00;
  let Inst{24}    = signed;
  let Inst{23}    = 0;
  let Inst{22-21} = opcod;
  let Inst{20}    = load;
  let Inst{19-16} = addr{12-9};
  let Inst{15-12} = Rt{3-0};
  let Inst{11}    = 1;
  // (P, W) = (1, 1) Pre-indexed or (0, 1) Post-indexed
  let Inst{10}    = pre; // The P bit.
  let Inst{9}     = addr{8}; // Sign bit
  let Inst{8}     = 1; // The W bit.
  let Inst{7-0}   = addr{7-0};

  let DecoderMethod = "DecodeT2LdStPre";
}

// T2Ipostldst - Thumb2 post-indexed load / store instructions.
class T2Ipostldst<bit signed, bits<2> opcod, bit load, bit pre,
                 dag oops, dag iops,
                 AddrMode am, IndexMode im, InstrItinClass itin,
                 string opc, string asm, string cstr, list<dag> pattern>
  : InstARM<am, 4, im, ThumbFrm, GenericDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [IsThumb2];
  let DecoderNamespace = "Thumb2";

  bits<4> Rt;
  bits<4> Rn;
  bits<9> offset;
  let Inst{31-27} = 0b11111;
  let Inst{26-25} = 0b00;
  let Inst{24}    = signed;
  let Inst{23}    = 0;
  let Inst{22-21} = opcod;
  let Inst{20}    = load;
  let Inst{19-16} = Rn;
  let Inst{15-12} = Rt{3-0};
  let Inst{11}    = 1;
  // (P, W) = (1, 1) Pre-indexed or (0, 1) Post-indexed
  let Inst{10}    = pre; // The P bit.
  let Inst{9}     = offset{8}; // Sign bit
  let Inst{8}     = 1; // The W bit.
  let Inst{7-0}   = offset{7-0};

  let DecoderMethod = "DecodeT2LdStPre";
}

// T1Pat - Same as Pat<>, but requires that the compiler be in Thumb1 mode.
class T1Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsThumb, IsThumb1Only];
}

// T2v6Pat - Same as Pat<>, but requires V6T2 Thumb2 mode.
class T2v6Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsThumb2, HasV6T2];
}

// T2Pat - Same as Pat<>, but requires that the compiler be in Thumb2 mode.
class T2Pat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [IsThumb2];
}

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ARM VFP Instruction templates.
//

// Almost all VFP instructions are predicable.
class VFPI<dag oops, dag iops, AddrMode am, int sz,
           IndexMode im, Format f, InstrItinClass itin,
           string opc, string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, im, f, VFPDomain, cstr, itin> {
  bits<4> p;
  let Inst{31-28} = p;
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", asm);
  let Pattern = pattern;
  let PostEncoderMethod = "VFPThumb2PostEncoder";
  let DecoderNamespace = "VFP";
  list<Predicate> Predicates = [HasVFP2];
}

// Special cases
class VFPXI<dag oops, dag iops, AddrMode am, int sz,
            IndexMode im, Format f, InstrItinClass itin,
            string asm, string cstr, list<dag> pattern>
  : InstARM<am, sz, im, f, VFPDomain, cstr, itin> {
  bits<4> p;
  let Inst{31-28} = p;
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = asm;
  let Pattern = pattern;
  let PostEncoderMethod = "VFPThumb2PostEncoder";
  let DecoderNamespace = "VFP";
  list<Predicate> Predicates = [HasVFP2];
}

class VFPAI<dag oops, dag iops, Format f, InstrItinClass itin,
            string opc, string asm, list<dag> pattern>
  : VFPI<oops, iops, AddrModeNone, 4, IndexModeNone, f, itin,
         opc, asm, "", pattern> {
  let PostEncoderMethod = "VFPThumb2PostEncoder";
}

// ARM VFP addrmode5 loads and stores
class ADI5<bits<4> opcod1, bits<2> opcod2, dag oops, dag iops,
           InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : VFPI<oops, iops, AddrMode5, 4, IndexModeNone,
         VFPLdStFrm, itin, opc, asm, "", pattern> {
  // Instruction operands.
  bits<5>  Dd;
  bits<13> addr;

  // Encode instruction operands.
  let Inst{23}    = addr{8};      // U (add = (U == '1'))
  let Inst{22}    = Dd{4};
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{15-12} = Dd{3-0};
  let Inst{7-0}   = addr{7-0};    // imm8

  let Inst{27-24} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision

  // Loads & stores operate on both NEON and VFP pipelines.
  let D = VFPNeonDomain;
}

class ASI5<bits<4> opcod1, bits<2> opcod2, dag oops, dag iops,
           InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : VFPI<oops, iops, AddrMode5, 4, IndexModeNone,
         VFPLdStFrm, itin, opc, asm, "", pattern> {
  // Instruction operands.
  bits<5>  Sd;
  bits<13> addr;

  // Encode instruction operands.
  let Inst{23}    = addr{8};      // U (add = (U == '1'))
  let Inst{22}    = Sd{0};
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{15-12} = Sd{4-1};
  let Inst{7-0}   = addr{7-0};    // imm8

  let Inst{27-24} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision

  // Loads & stores operate on both NEON and VFP pipelines.
  let D = VFPNeonDomain;
}

class AHI5<bits<4> opcod1, bits<2> opcod2, dag oops, dag iops,
           InstrItinClass itin,
           string opc, string asm, list<dag> pattern>
  : VFPI<oops, iops, AddrMode5FP16, 4, IndexModeNone,
         VFPLdStFrm, itin, opc, asm, "", pattern> {
  list<Predicate> Predicates = [HasFullFP16];

  // Instruction operands.
  bits<5>  Sd;
  bits<13> addr;

  // Encode instruction operands.
  let Inst{23}    = addr{8};      // U (add = (U == '1'))
  let Inst{22}    = Sd{0};
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{15-12} = Sd{4-1};
  let Inst{7-0}   = addr{7-0};    // imm8

  let Inst{27-24} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-8}  = 0b1001;     // Half precision

  // Loads & stores operate on both NEON and VFP pipelines.
  let D = VFPNeonDomain;
}

// VFP Load / store multiple pseudo instructions.
class PseudoVFPLdStM<dag oops, dag iops, InstrItinClass itin, string cstr,
                     list<dag> pattern>
  : InstARM<AddrMode4, 4, IndexModeNone, Pseudo, VFPNeonDomain,
            cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let Pattern = pattern;
  list<Predicate> Predicates = [HasVFP2];
}

// Load / store multiple

// Unknown precision
class AXXI4<dag oops, dag iops, IndexMode im,
            string asm, string cstr, list<dag> pattern>
  : VFPXI<oops, iops, AddrMode4, 4, im,
          VFPLdStFrm, NoItinerary, asm, cstr, pattern> {
  // Instruction operands.
  bits<4>  Rn;
  bits<13> regs;

  // Encode instruction operands.
  let Inst{19-16} = Rn;
  let Inst{22}    = 0;
  let Inst{15-12} = regs{11-8};
  let Inst{7-1}   = regs{7-1};

  let Inst{27-25} = 0b110;
  let Inst{11-8}  = 0b1011;
  let Inst{0}     = 1;
}

// Double precision
class AXDI4<dag oops, dag iops, IndexMode im, InstrItinClass itin,
            string asm, string cstr, list<dag> pattern>
  : VFPXI<oops, iops, AddrMode4, 4, im,
          VFPLdStMulFrm, itin, asm, cstr, pattern> {
  // Instruction operands.
  bits<4>  Rn;
  bits<13> regs;

  // Encode instruction operands.
  let Inst{19-16} = Rn;
  let Inst{22}    = regs{12};
  let Inst{15-12} = regs{11-8};
  let Inst{7-1}   = regs{7-1};

  let Inst{27-25} = 0b110;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision
  let Inst{0}     = 0;
}

// Single Precision
class AXSI4<dag oops, dag iops, IndexMode im, InstrItinClass itin,
            string asm, string cstr, list<dag> pattern>
  : VFPXI<oops, iops, AddrMode4, 4, im,
          VFPLdStMulFrm, itin, asm, cstr, pattern> {
  // Instruction operands.
  bits<4> Rn;
  bits<13> regs;

  // Encode instruction operands.
  let Inst{19-16} = Rn;
  let Inst{22}    = regs{8};
  let Inst{15-12} = regs{12-9};
  let Inst{7-0}   = regs{7-0};

  let Inst{27-25} = 0b110;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision
}

// Double precision, unary
class ADuI<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
           bit opcod5, dag oops, dag iops, InstrItinClass itin, string opc,
           string asm, list<dag> pattern>
  : VFPAI<oops, iops, VFPUnaryFrm, itin, opc, asm, pattern> {
  // Instruction operands.
  bits<5> Dd;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;

  let Predicates = [HasVFP2, HasDPVFP];
}

// Double precision, unary, not-predicated
class ADuInp<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
           bit opcod5, dag oops, dag iops, InstrItinClass itin,
           string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone, VFPUnaryFrm, itin, asm, "", pattern> {
  // Instruction operands.
  bits<5> Dd;
  bits<5> Dm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;
}

// Double precision, binary
class ADbI<bits<5> opcod1, bits<2> opcod2, bit op6, bit op4, dag oops,
           dag iops, InstrItinClass itin, string opc, string asm,
           list<dag> pattern>
  : VFPAI<oops, iops, VFPBinaryFrm, itin, opc, asm, pattern> {
  // Instruction operands.
  bits<5> Dd;
  bits<5> Dn;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{19-16} = Dn{3-0};
  let Inst{7}     = Dn{4};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision
  let Inst{6}     = op6;
  let Inst{4}     = op4;

  let Predicates = [HasVFP2, HasDPVFP];
}

// FP, binary, not predicated
class ADbInp<bits<5> opcod1, bits<2> opcod2, bit opcod3, dag oops, dag iops,
           InstrItinClass itin, string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone, VFPBinaryFrm, itin,
          asm, "", pattern>
{
  // Instruction operands.
  bits<5> Dd;
  bits<5> Dn;
  bits<5> Dm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{19-16} = Dn{3-0};
  let Inst{7}     = Dn{4};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1; // double precision
  let Inst{6}     = opcod3;
  let Inst{4}     = 0;

  let Predicates = [HasVFP2, HasDPVFP];
}

// Single precision, unary, predicated
class ASuI<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
           bit opcod5, dag oops, dag iops, InstrItinClass itin, string opc,
           string asm, list<dag> pattern>
  : VFPAI<oops, iops, VFPUnaryFrm, itin, opc, asm, pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;
}

// Single precision, unary, non-predicated
class ASuInp<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
             bit opcod5, dag oops, dag iops, InstrItinClass itin,
             string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone,
          VFPUnaryFrm, itin, asm, "", pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;
}

// Single precision unary, if no NEON. Same as ASuI except not available if
// NEON is enabled.
class ASuIn<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
            bit opcod5, dag oops, dag iops, InstrItinClass itin, string opc,
            string asm, list<dag> pattern>
  : ASuI<opcod1, opcod2, opcod3, opcod4, opcod5, oops, iops, itin, opc, asm,
         pattern> {
  list<Predicate> Predicates = [HasVFP2,DontUseNEONForFP];
}

// Single precision, binary
class ASbI<bits<5> opcod1, bits<2> opcod2, bit op6, bit op4, dag oops, dag iops,
           InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : VFPAI<oops, iops, VFPBinaryFrm, itin, opc, asm, pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sn;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision
  let Inst{6}     = op6;
  let Inst{4}     = op4;
}

// Single precision, binary, not predicated
class ASbInp<bits<5> opcod1, bits<2> opcod2, bit opcod3, dag oops, dag iops,
           InstrItinClass itin, string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone,
          VFPBinaryFrm, itin, asm, "", pattern>
{
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sn;
  bits<5> Sm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0; // Single precision
  let Inst{6}     = opcod3;
  let Inst{4}     = 0;
}

// Single precision binary, if no NEON. Same as ASbI except not available if
// NEON is enabled.
class ASbIn<bits<5> opcod1, bits<2> opcod2, bit op6, bit op4, dag oops,
            dag iops, InstrItinClass itin, string opc, string asm,
            list<dag> pattern>
  : ASbI<opcod1, opcod2, op6, op4, oops, iops, itin, opc, asm, pattern> {
  list<Predicate> Predicates = [HasVFP2,DontUseNEONForFP];

  // Instruction operands.
  bits<5> Sd;
  bits<5> Sn;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};
}

// Half precision, unary, predicated
class AHuI<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
           bit opcod5, dag oops, dag iops, InstrItinClass itin, string opc,
           string asm, list<dag> pattern>
  : VFPAI<oops, iops, VFPUnaryFrm, itin, opc, asm, pattern> {
  list<Predicate> Predicates = [HasFullFP16];

  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-8}  = 0b1001;   // Half precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;
}

// Half precision, unary, non-predicated
class AHuInp<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<2> opcod4,
             bit opcod5, dag oops, dag iops, InstrItinClass itin,
             string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone,
          VFPUnaryFrm, itin, asm, "", pattern> {
  list<Predicate> Predicates = [HasFullFP16];

  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-8}  = 0b1001;   // Half precision
  let Inst{7-6}   = opcod4;
  let Inst{4}     = opcod5;
}

// Half precision, binary
class AHbI<bits<5> opcod1, bits<2> opcod2, bit op6, bit op4, dag oops, dag iops,
           InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : VFPAI<oops, iops, VFPBinaryFrm, itin, opc, asm, pattern> {
  list<Predicate> Predicates = [HasFullFP16];

  // Instruction operands.
  bits<5> Sd;
  bits<5> Sn;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-8}  = 0b1001;   // Half precision
  let Inst{6}     = op6;
  let Inst{4}     = op4;
}

// Half precision, binary, not predicated
class AHbInp<bits<5> opcod1, bits<2> opcod2, bit opcod3, dag oops, dag iops,
           InstrItinClass itin, string asm, list<dag> pattern>
  : VFPXI<oops, iops, AddrModeNone, 4, IndexModeNone,
          VFPBinaryFrm, itin, asm, "", pattern> {
  list<Predicate> Predicates = [HasFullFP16];

  // Instruction operands.
  bits<5> Sd;
  bits<5> Sn;
  bits<5> Sm;

  let Inst{31-28} = 0b1111;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{11-8}  = 0b1001;   // Half precision
  let Inst{6}     = opcod3;
  let Inst{4}     = 0;
}

// VFP conversion instructions
class AVConv1I<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<4> opcod4,
               dag oops, dag iops, InstrItinClass itin, string opc, string asm,
               list<dag> pattern>
  : VFPAI<oops, iops, VFPConv1Frm, itin, opc, asm, pattern> {
  let Inst{27-23} = opcod1;
  let Inst{21-20} = opcod2;
  let Inst{19-16} = opcod3;
  let Inst{11-8}  = opcod4;
  let Inst{6}     = 1;
  let Inst{4}     = 0;
}

// VFP conversion between floating-point and fixed-point
class AVConv1XI<bits<5> op1, bits<2> op2, bits<4> op3, bits<4> op4, bit op5,
                dag oops, dag iops, InstrItinClass itin, string opc, string asm,
                list<dag> pattern>
  : AVConv1I<op1, op2, op3, op4, oops, iops, itin, opc, asm, pattern> {
  bits<5> fbits;
  // size (fixed-point number): sx == 0 ? 16 : 32
  let Inst{7} = op5; // sx
  let Inst{5} = fbits{0};
  let Inst{3-0} = fbits{4-1};
}

// VFP conversion instructions, if no NEON
class AVConv1In<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3, bits<4> opcod4,
                dag oops, dag iops, InstrItinClass itin,
                string opc, string asm, list<dag> pattern>
  : AVConv1I<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
             pattern> {
  list<Predicate> Predicates = [HasVFP2,DontUseNEONForFP];
}

class AVConvXI<bits<8> opcod1, bits<4> opcod2, dag oops, dag iops, Format f,
               InstrItinClass itin,
               string opc, string asm, list<dag> pattern>
  : VFPAI<oops, iops, f, itin, opc, asm, pattern> {
  let Inst{27-20} = opcod1;
  let Inst{11-8}  = opcod2;
  let Inst{4}     = 1;
}

class AVConv2I<bits<8> opcod1, bits<4> opcod2, dag oops, dag iops,
               InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AVConvXI<opcod1, opcod2, oops, iops, VFPConv2Frm, itin, opc, asm, pattern>;

class AVConv3I<bits<8> opcod1, bits<4> opcod2, dag oops, dag iops,
               InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AVConvXI<opcod1, opcod2, oops, iops, VFPConv3Frm, itin, opc, asm, pattern>;

class AVConv4I<bits<8> opcod1, bits<4> opcod2, dag oops, dag iops,
               InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AVConvXI<opcod1, opcod2, oops, iops, VFPConv4Frm, itin, opc, asm, pattern>;

class AVConv5I<bits<8> opcod1, bits<4> opcod2, dag oops, dag iops,
               InstrItinClass itin, string opc, string asm, list<dag> pattern>
  : AVConvXI<opcod1, opcod2, oops, iops, VFPConv5Frm, itin, opc, asm, pattern>;

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ARM NEON Instruction templates.
//

class NeonI<dag oops, dag iops, AddrMode am, IndexMode im, Format f,
            InstrItinClass itin, string opc, string dt, string asm, string cstr,
            list<dag> pattern>
  : InstARM<am, 4, im, f, NeonDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", ".", dt, "\t", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [HasNEON];
  let DecoderNamespace = "NEON";
}

// Same as NeonI except it does not have a "data type" specifier.
class NeonXI<dag oops, dag iops, AddrMode am, IndexMode im, Format f,
             InstrItinClass itin, string opc, string asm, string cstr,
             list<dag> pattern>
  : InstARM<am, 4, im, f, NeonDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", "\t", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [HasNEON];
  let DecoderNamespace = "NEON";
}

// Same as NeonI except it is not predicated
class NeonInp<dag oops, dag iops, AddrMode am, IndexMode im, Format f,
            InstrItinClass itin, string opc, string dt, string asm, string cstr,
            list<dag> pattern>
  : InstARM<am, 4, im, f, NeonDomain, cstr, itin> {
  let OutOperandList = oops;
  let InOperandList = iops;
  let AsmString = !strconcat(opc, ".", dt, "\t", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [HasNEON];
  let DecoderNamespace = "NEON";

  let Inst{31-28} = 0b1111;
}

class NLdSt<bit op23, bits<2> op21_20, bits<4> op11_8, bits<4> op7_4,
            dag oops, dag iops, InstrItinClass itin,
            string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NeonI<oops, iops, AddrMode6, IndexModeNone, NLdStFrm, itin, opc, dt, asm,
          cstr, pattern> {
  let Inst{31-24} = 0b11110100;
  let Inst{23}    = op23;
  let Inst{21-20} = op21_20;
  let Inst{11-8}  = op11_8;
  let Inst{7-4}   = op7_4;

  let PostEncoderMethod = "NEONThumb2LoadStorePostEncoder";
  let DecoderNamespace = "NEONLoadStore";

  bits<5> Vd;
  bits<6> Rn;
  bits<4> Rm;

  let Inst{22}    = Vd{4};
  let Inst{15-12} = Vd{3-0};
  let Inst{19-16} = Rn{3-0};
  let Inst{3-0}   = Rm{3-0};
}

class NLdStLn<bit op23, bits<2> op21_20, bits<4> op11_8, bits<4> op7_4,
            dag oops, dag iops, InstrItinClass itin,
            string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NLdSt<op23, op21_20, op11_8, op7_4, oops, iops, itin, opc,
          dt, asm, cstr, pattern> {
  bits<3> lane;
}

class PseudoNLdSt<dag oops, dag iops, InstrItinClass itin, string cstr>
  : InstARM<AddrMode6, 4, IndexModeNone, Pseudo, NeonDomain, cstr,
            itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  list<Predicate> Predicates = [HasNEON];
}

class PseudoNeonI<dag oops, dag iops, InstrItinClass itin, string cstr,
                  list<dag> pattern>
  : InstARM<AddrModeNone, 4, IndexModeNone, Pseudo, NeonDomain, cstr,
            itin> {
  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let Pattern = pattern;
  list<Predicate> Predicates = [HasNEON];
}

class NDataI<dag oops, dag iops, Format f, InstrItinClass itin,
             string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NeonI<oops, iops, AddrModeNone, IndexModeNone, f, itin, opc, dt, asm, cstr,
          pattern> {
  let Inst{31-25} = 0b1111001;
  let PostEncoderMethod = "NEONThumb2DataIPostEncoder";
  let DecoderNamespace = "NEONData";
}

class NDataXI<dag oops, dag iops, Format f, InstrItinClass itin,
              string opc, string asm, string cstr, list<dag> pattern>
  : NeonXI<oops, iops, AddrModeNone, IndexModeNone, f, itin, opc, asm,
           cstr, pattern> {
  let Inst{31-25} = 0b1111001;
  let PostEncoderMethod = "NEONThumb2DataIPostEncoder";
  let DecoderNamespace = "NEONData";
}

// NEON "one register and a modified immediate" format.
class N1ModImm<bit op23, bits<3> op21_19, bits<4> op11_8, bit op7, bit op6,
               bit op5, bit op4,
               dag oops, dag iops, InstrItinClass itin,
               string opc, string dt, string asm, string cstr,
               list<dag> pattern>
  : NDataI<oops, iops, N1RegModImmFrm, itin, opc, dt, asm, cstr, pattern> {
  let Inst{23}    = op23;
  let Inst{21-19} = op21_19;
  let Inst{11-8}  = op11_8;
  let Inst{7}     = op7;
  let Inst{6}     = op6;
  let Inst{5}     = op5;
  let Inst{4}     = op4;

  // Instruction operands.
  bits<5> Vd;
  bits<13> SIMM;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{24}    = SIMM{7};
  let Inst{18-16} = SIMM{6-4};
  let Inst{3-0}   = SIMM{3-0};
  let DecoderMethod = "DecodeNEONModImmInstruction";
}

// NEON 2 vector register format.
class N2V<bits<2> op24_23, bits<2> op21_20, bits<2> op19_18, bits<2> op17_16,
          bits<5> op11_7, bit op6, bit op4,
          dag oops, dag iops, InstrItinClass itin,
          string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NDataI<oops, iops, N2RegFrm, itin, opc, dt, asm, cstr, pattern> {
  let Inst{24-23} = op24_23;
  let Inst{21-20} = op21_20;
  let Inst{19-18} = op19_18;
  let Inst{17-16} = op17_16;
  let Inst{11-7}  = op11_7;
  let Inst{6}     = op6;
  let Inst{4}     = op4;

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vm;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = Vm{4};
}

// Same as N2V but not predicated.
class N2Vnp<bits<2> op19_18, bits<2> op17_16, bits<3> op10_8, bit op7, bit op6,
            dag oops, dag iops, InstrItinClass itin, string OpcodeStr,
            string Dt, list<dag> pattern>
   : NeonInp<oops, iops, AddrModeNone, IndexModeNone, N2RegFrm, itin,
             OpcodeStr, Dt, "$Vd, $Vm", "", pattern> {
  bits<5> Vd;
  bits<5> Vm;

  // Encode instruction operands
  let Inst{22}    = Vd{4};
  let Inst{15-12} = Vd{3-0};
  let Inst{5}     = Vm{4};
  let Inst{3-0}   = Vm{3-0};

  // Encode constant bits
  let Inst{27-23} = 0b00111;
  let Inst{21-20} = 0b11;
  let Inst{19-18} = op19_18;
  let Inst{17-16} = op17_16;
  let Inst{11} = 0;
  let Inst{10-8} = op10_8;
  let Inst{7} = op7;
  let Inst{6} = op6;
  let Inst{4} = 0;

  let DecoderNamespace = "NEON";
}

// Same as N2V except it doesn't have a datatype suffix.
class N2VX<bits<2> op24_23, bits<2> op21_20, bits<2> op19_18, bits<2> op17_16,
           bits<5> op11_7, bit op6, bit op4,
           dag oops, dag iops, InstrItinClass itin,
           string opc, string asm, string cstr, list<dag> pattern>
  : NDataXI<oops, iops, N2RegFrm, itin, opc, asm, cstr, pattern> {
  let Inst{24-23} = op24_23;
  let Inst{21-20} = op21_20;
  let Inst{19-18} = op19_18;
  let Inst{17-16} = op17_16;
  let Inst{11-7}  = op11_7;
  let Inst{6}     = op6;
  let Inst{4}     = op4;

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vm;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = Vm{4};
}

// NEON 2 vector register with immediate.
class N2VImm<bit op24, bit op23, bits<4> op11_8, bit op7, bit op6, bit op4,
             dag oops, dag iops, Format f, InstrItinClass itin,
             string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NDataI<oops, iops, f, itin, opc, dt, asm, cstr, pattern> {
  let Inst{24}   = op24;
  let Inst{23}   = op23;
  let Inst{11-8} = op11_8;
  let Inst{7}    = op7;
  let Inst{6}    = op6;
  let Inst{4}    = op4;

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vm;
  bits<6> SIMM;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = Vm{4};
  let Inst{21-16} = SIMM{5-0};
}

// NEON 3 vector register format.

class N3VCommon<bit op24, bit op23, bits<2> op21_20, bits<4> op11_8, bit op6,
                bit op4, dag oops, dag iops, Format f, InstrItinClass itin,
                string opc, string dt, string asm, string cstr,
                list<dag> pattern>
  : NDataI<oops, iops, f, itin, opc, dt, asm, cstr, pattern> {
  let Inst{24}    = op24;
  let Inst{23}    = op23;
  let Inst{21-20} = op21_20;
  let Inst{11-8}  = op11_8;
  let Inst{6}     = op6;
  let Inst{4}     = op4;
}

class N3V<bit op24, bit op23, bits<2> op21_20, bits<4> op11_8, bit op6, bit op4,
          dag oops, dag iops, Format f, InstrItinClass itin,
          string opc, string dt, string asm, string cstr, list<dag> pattern>
  : N3VCommon<op24, op23, op21_20, op11_8, op6, op4,
              oops, iops, f, itin, opc, dt, asm, cstr, pattern> {
  // Instruction operands.
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{19-16} = Vn{3-0};
  let Inst{7}     = Vn{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = Vm{4};
}

class N3Vnp<bits<5> op27_23, bits<2> op21_20, bits<4> op11_8, bit op6,
                bit op4, dag oops, dag iops,Format f, InstrItinClass itin,
                string OpcodeStr, string Dt, list<dag> pattern>
  : NeonInp<oops, iops, AddrModeNone, IndexModeNone, f, itin, OpcodeStr,
            Dt, "$Vd, $Vn, $Vm", "", pattern> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  // Encode instruction operands
  let Inst{22} = Vd{4};
  let Inst{15-12} = Vd{3-0};
  let Inst{19-16} = Vn{3-0};
  let Inst{7} = Vn{4};
  let Inst{5} = Vm{4};
  let Inst{3-0} = Vm{3-0};

  // Encode constant bits
  let Inst{27-23} = op27_23;
  let Inst{21-20} = op21_20;
  let Inst{11-8}  = op11_8;
  let Inst{6}     = op6;
  let Inst{4}     = op4;
}

class N3VLane32<bit op24, bit op23, bits<2> op21_20, bits<4> op11_8, bit op6,
                bit op4, dag oops, dag iops, Format f, InstrItinClass itin,
                string opc, string dt, string asm, string cstr,
                list<dag> pattern>
  : N3VCommon<op24, op23, op21_20, op11_8, op6, op4,
              oops, iops, f, itin, opc, dt, asm, cstr, pattern> {

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;
  bit lane;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{19-16} = Vn{3-0};
  let Inst{7}     = Vn{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = lane;
}

class N3VLane16<bit op24, bit op23, bits<2> op21_20, bits<4> op11_8, bit op6,
                bit op4, dag oops, dag iops, Format f, InstrItinClass itin,
                string opc, string dt, string asm, string cstr,
                list<dag> pattern>
  : N3VCommon<op24, op23, op21_20, op11_8, op6, op4,
              oops, iops, f, itin, opc, dt, asm, cstr, pattern> {

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;
  bits<2> lane;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{19-16} = Vn{3-0};
  let Inst{7}     = Vn{4};
  let Inst{2-0}   = Vm{2-0};
  let Inst{5}     = lane{1};
  let Inst{3}     = lane{0};
}

// Same as N3V except it doesn't have a data type suffix.
class N3VX<bit op24, bit op23, bits<2> op21_20, bits<4> op11_8, bit op6,
           bit op4,
           dag oops, dag iops, Format f, InstrItinClass itin,
           string opc, string asm, string cstr, list<dag> pattern>
  : NDataXI<oops, iops, f, itin, opc, asm, cstr, pattern> {
  let Inst{24}    = op24;
  let Inst{23}    = op23;
  let Inst{21-20} = op21_20;
  let Inst{11-8}  = op11_8;
  let Inst{6}     = op6;
  let Inst{4}     = op4;

  // Instruction operands.
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  let Inst{15-12} = Vd{3-0};
  let Inst{22}    = Vd{4};
  let Inst{19-16} = Vn{3-0};
  let Inst{7}     = Vn{4};
  let Inst{3-0}   = Vm{3-0};
  let Inst{5}     = Vm{4};
}

// NEON VMOVs between scalar and core registers.
class NVLaneOp<bits<8> opcod1, bits<4> opcod2, bits<2> opcod3,
               dag oops, dag iops, Format f, InstrItinClass itin,
               string opc, string dt, string asm, list<dag> pattern>
  : InstARM<AddrModeNone, 4, IndexModeNone, f, NeonDomain,
            "", itin> {
  let Inst{27-20} = opcod1;
  let Inst{11-8}  = opcod2;
  let Inst{6-5}   = opcod3;
  let Inst{4}     = 1;
  // A8.6.303, A8.6.328, A8.6.329
  let Inst{3-0}   = 0b0000;

  let OutOperandList = oops;
  let InOperandList = !con(iops, (ins pred:$p));
  let AsmString = !strconcat(opc, "${p}", ".", dt, "\t", asm);
  let Pattern = pattern;
  list<Predicate> Predicates = [HasNEON];

  let PostEncoderMethod = "NEONThumb2DupPostEncoder";
  let DecoderNamespace = "NEONDup";

  bits<5> V;
  bits<4> R;
  bits<4> p;
  bits<4> lane;

  let Inst{31-28} = p{3-0};
  let Inst{7}     = V{4};
  let Inst{19-16} = V{3-0};
  let Inst{15-12} = R{3-0};
}
class NVGetLane<bits<8> opcod1, bits<4> opcod2, bits<2> opcod3,
                dag oops, dag iops, InstrItinClass itin,
                string opc, string dt, string asm, list<dag> pattern>
  : NVLaneOp<opcod1, opcod2, opcod3, oops, iops, NGetLnFrm, itin,
             opc, dt, asm, pattern>;
class NVSetLane<bits<8> opcod1, bits<4> opcod2, bits<2> opcod3,
                dag oops, dag iops, InstrItinClass itin,
                string opc, string dt, string asm, list<dag> pattern>
  : NVLaneOp<opcod1, opcod2, opcod3, oops, iops, NSetLnFrm, itin,
             opc, dt, asm, pattern>;
class NVDup<bits<8> opcod1, bits<4> opcod2, bits<2> opcod3,
            dag oops, dag iops, InstrItinClass itin,
            string opc, string dt, string asm, list<dag> pattern>
  : NVLaneOp<opcod1, opcod2, opcod3, oops, iops, NDupFrm, itin,
             opc, dt, asm, pattern>;

// Vector Duplicate Lane (from scalar to all elements)
class NVDupLane<bits<4> op19_16, bit op6, dag oops, dag iops,
                InstrItinClass itin, string opc, string dt, string asm,
                list<dag> pattern>
  : NDataI<oops, iops, NVDupLnFrm, itin, opc, dt, asm, "", pattern> {
  let Inst{24-23} = 0b11;
  let Inst{21-20} = 0b11;
  let Inst{19-16} = op19_16;
  let Inst{11-7}  = 0b11000;
  let Inst{6}     = op6;
  let Inst{4}     = 0;

  bits<5> Vd;
  bits<5> Vm;

  let Inst{22}     = Vd{4};
  let Inst{15-12} = Vd{3-0};
  let Inst{5}     = Vm{4};
  let Inst{3-0} = Vm{3-0};
}

// NEONFPPat - Same as Pat<>, but requires that the compiler be using NEON
// for single-precision FP.
class NEONFPPat<dag pattern, dag result> : Pat<pattern, result> {
  list<Predicate> Predicates = [HasNEON,UseNEONForFP];
}

// VFP/NEON Instruction aliases for type suffices.
// Note: When EmitPriority == 1, the alias will be used for printing
class VFPDataTypeInstAlias<string opc, string dt, string asm, dag Result, bit EmitPriority = 0> :
  InstAlias<!strconcat(opc, dt, "\t", asm), Result, EmitPriority>, Requires<[HasVFP2]>;

// Note: When EmitPriority == 1, the alias will be used for printing
multiclass VFPDTAnyInstAlias<string opc, string asm, dag Result, bit EmitPriority = 0> {
  def : VFPDataTypeInstAlias<opc, ".8", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".16", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".32", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".64", asm, Result, EmitPriority>;
}

// Note: When EmitPriority == 1, the alias will be used for printing
multiclass NEONDTAnyInstAlias<string opc, string asm, dag Result, bit EmitPriority = 0> {
  let Predicates = [HasNEON] in {
  def : VFPDataTypeInstAlias<opc, ".8", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".16", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".32", asm, Result, EmitPriority>;
  def : VFPDataTypeInstAlias<opc, ".64", asm, Result, EmitPriority>;
}
}

// The same alias classes using AsmPseudo instead, for the more complex
// stuff in NEON that InstAlias can't quite handle.
// Note that we can't use anonymous defm references here like we can
// above, as we care about the ultimate instruction enum names generated, unlike
// for instalias defs.
class NEONDataTypeAsmPseudoInst<string opc, string dt, string asm, dag iops> :
  AsmPseudoInst<!strconcat(opc, dt, "\t", asm), iops>, Requires<[HasNEON]>;

// Extension of NEON 3-vector data processing instructions in coprocessor 8
// encoding space, introduced in ARMv8.3-A.
class N3VCP8<bits<2> op24_23, bits<2> op21_20, bit op6, bit op4,
             dag oops, dag iops, InstrItinClass itin,
             string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NeonInp<oops, iops, AddrModeNone, IndexModeNone, N3RegCplxFrm, itin, opc,
            dt, asm, cstr, pattern> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  let DecoderNamespace = "VFPV8";
  // These have the same encodings in ARM and Thumb2
  let PostEncoderMethod = "";

  let Inst{31-25} = 0b1111110;
  let Inst{24-23} = op24_23;
  let Inst{22}    = Vd{4};
  let Inst{21-20} = op21_20;
  let Inst{19-16} = Vn{3-0};
  let Inst{15-12} = Vd{3-0};
  let Inst{11-8}  = 0b1000;
  let Inst{7}     = Vn{4};
  let Inst{6}     = op6;
  let Inst{5}     = Vm{4};
  let Inst{4}     = op4;
  let Inst{3-0}   = Vm{3-0};
}

// Extension of NEON 2-vector-and-scalar data processing instructions in
// coprocessor 8 encoding space, introduced in ARMv8.3-A.
class N3VLaneCP8<bit op23, bits<2> op21_20, bit op6, bit op4,
             dag oops, dag iops, InstrItinClass itin,
             string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NeonInp<oops, iops, AddrModeNone, IndexModeNone, N3RegCplxFrm, itin, opc,
            dt, asm, cstr, pattern> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  let DecoderNamespace = "VFPV8";
  // These have the same encodings in ARM and Thumb2
  let PostEncoderMethod = "";

  let Inst{31-24} = 0b11111110;
  let Inst{23}    = op23;
  let Inst{22}    = Vd{4};
  let Inst{21-20} = op21_20;
  let Inst{19-16} = Vn{3-0};
  let Inst{15-12} = Vd{3-0};
  let Inst{11-8}  = 0b1000;
  let Inst{7}     = Vn{4};
  let Inst{6}     = op6;
  // Bit 5 set by sub-classes
  let Inst{4}     = op4;
  let Inst{3-0}   = Vm{3-0};
}

// In Armv8.2-A, some NEON instructions are added that encode Vn and Vm
// differently:
//    if Q == ‘1’ then UInt(N:Vn) else UInt(Vn:N);
//    if Q == ‘1’ then UInt(M:Vm) else UInt(Vm:M);
// Class N3VCP8 above describes the Q=1 case, and this class the Q=0 case.
class N3VCP8Q0<bits<2> op24_23, bits<2> op21_20, bit op6, bit op4,
             dag oops, dag iops, InstrItinClass itin,
             string opc, string dt, string asm, string cstr, list<dag> pattern>
  : NeonInp<oops, iops, AddrModeNone, IndexModeNone, N3RegCplxFrm, itin, opc, dt, asm, cstr, pattern> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;

  let DecoderNamespace = "VFPV8";
  // These have the same encodings in ARM and Thumb2
  let PostEncoderMethod = "";

  let Inst{31-25} = 0b1111110;
  let Inst{24-23} = op24_23;
  let Inst{22}    = Vd{4};
  let Inst{21-20} = op21_20;
  let Inst{19-16} = Vn{4-1};
  let Inst{15-12} = Vd{3-0};
  let Inst{11-8}  = 0b1000;
  let Inst{7}     = Vn{0};
  let Inst{6}     = op6;
  let Inst{5}     = Vm{0};
  let Inst{4}     = op4;
  let Inst{3-0}   = Vm{4-1};
}

// Operand types for complex instructions
class ComplexRotationOperand<int Angle, int Remainder, string Type, string Diag>
  : AsmOperandClass {
  let PredicateMethod = "isComplexRotation<" # Angle # ", " # Remainder # ">";
  let DiagnosticString = "complex rotation must be " # Diag;
  let Name = "ComplexRotation" # Type;
}
def complexrotateop : Operand<i32> {
  let ParserMatchClass = ComplexRotationOperand<90, 0, "Even", "0, 90, 180 or 270">;
  let PrintMethod = "printComplexRotationOp<90, 0>";
}
def complexrotateopodd : Operand<i32> {
  let ParserMatchClass = ComplexRotationOperand<180, 90, "Odd", "90 or 270">;
  let PrintMethod = "printComplexRotationOp<180, 90>";
}

// Data type suffix token aliases. Implements Table A7-3 in the ARM ARM.
def : TokenAlias<".s8", ".i8">;
def : TokenAlias<".u8", ".i8">;
def : TokenAlias<".s16", ".i16">;
def : TokenAlias<".u16", ".i16">;
def : TokenAlias<".s32", ".i32">;
def : TokenAlias<".u32", ".i32">;
def : TokenAlias<".s64", ".i64">;
def : TokenAlias<".u64", ".i64">;

def : TokenAlias<".i8", ".8">;
def : TokenAlias<".i16", ".16">;
def : TokenAlias<".i32", ".32">;
def : TokenAlias<".i64", ".64">;

def : TokenAlias<".p8", ".8">;
def : TokenAlias<".p16", ".16">;

def : TokenAlias<".f32", ".32">;
def : TokenAlias<".f64", ".64">;
def : TokenAlias<".f", ".f32">;
def : TokenAlias<".d", ".f64">;
