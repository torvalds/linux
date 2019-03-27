//===-- SIInstrFormats.td - SI Instruction Encodings ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// SI Instruction format definitions.
//
//===----------------------------------------------------------------------===//

def isGCN : Predicate<"Subtarget->getGeneration() "
                      ">= AMDGPUSubtarget::SOUTHERN_ISLANDS">,
            AssemblerPredicate<"FeatureGCN">;
def isSI : Predicate<"Subtarget->getGeneration() "
                      "== AMDGPUSubtarget::SOUTHERN_ISLANDS">,
           AssemblerPredicate<"FeatureSouthernIslands">;


class InstSI <dag outs, dag ins, string asm = "",
              list<dag> pattern = []> :
  AMDGPUInst<outs, ins, asm, pattern>, GCNPredicateControl {
  let SubtargetPredicate = isGCN;

  // Low bits - basic encoding information.
  field bit SALU = 0;
  field bit VALU = 0;

  // SALU instruction formats.
  field bit SOP1 = 0;
  field bit SOP2 = 0;
  field bit SOPC = 0;
  field bit SOPK = 0;
  field bit SOPP = 0;

  // VALU instruction formats.
  field bit VOP1 = 0;
  field bit VOP2 = 0;
  field bit VOPC = 0;
  field bit VOP3 = 0;
  field bit VOP3P = 0;
  field bit VINTRP = 0;
  field bit SDWA = 0;
  field bit DPP = 0;

  // Memory instruction formats.
  field bit MUBUF = 0;
  field bit MTBUF = 0;
  field bit SMRD = 0;
  field bit MIMG = 0;
  field bit EXP = 0;
  field bit FLAT = 0;
  field bit DS = 0;

  // Pseudo instruction formats.
  field bit VGPRSpill = 0;
  field bit SGPRSpill = 0;

  // High bits - other information.
  field bit VM_CNT = 0;
  field bit EXP_CNT = 0;
  field bit LGKM_CNT = 0;

  // Whether WQM _must_ be enabled for this instruction.
  field bit WQM = 0;

  // Whether WQM _must_ be disabled for this instruction.
  field bit DisableWQM = 0;

  field bit Gather4 = 0;

  // Most sopk treat the immediate as a signed 16-bit, however some
  // use it as unsigned.
  field bit SOPKZext = 0;

  // This is an s_store_dword* instruction that requires a cache flush
  // on wave termination. It is necessary to distinguish from mayStore
  // SMEM instructions like the cache flush ones.
  field bit ScalarStore = 0;

  // Whether the operands can be ignored when computing the
  // instruction size.
  field bit FixedSize = 0;

  // This bit tells the assembler to use the 32-bit encoding in case it
  // is unable to infer the encoding from the operands.
  field bit VOPAsmPrefer32Bit = 0;

  // This bit indicates that this is a VOP3 opcode which supports op_sel
  // modifier (gfx9 only).
  field bit VOP3_OPSEL = 0;

  // Is it possible for this instruction to be atomic?
  field bit maybeAtomic = 0;

  // This bit indicates that this is a VI instruction which is renamed
  // in GFX9. Required for correct mapping from pseudo to MC.
  field bit renamedInGFX9 = 0;

  // This bit indicates that this has a floating point result type, so
  // the clamp modifier has floating point semantics.
  field bit FPClamp = 0;

  // This bit indicates that instruction may support integer clamping
  // which depends on GPU features.
  field bit IntClamp = 0;

  // This field indicates that the clamp applies to the low component
  // of a packed output register.
  field bit ClampLo = 0;

  // This field indicates that the clamp applies to the high component
  // of a packed output register.
  field bit ClampHi = 0;

  // This bit indicates that this is a packed VOP3P instruction
  field bit IsPacked = 0;

  // This bit indicates that this is a D16 buffer instruction.
  field bit D16Buf = 0;

  // This bit indicates that this uses the floating point double precision
  // rounding mode flags
  field bit FPDPRounding = 0;

  // These need to be kept in sync with the enum in SIInstrFlags.
  let TSFlags{0} = SALU;
  let TSFlags{1} = VALU;

  let TSFlags{2} = SOP1;
  let TSFlags{3} = SOP2;
  let TSFlags{4} = SOPC;
  let TSFlags{5} = SOPK;
  let TSFlags{6} = SOPP;

  let TSFlags{7} = VOP1;
  let TSFlags{8} = VOP2;
  let TSFlags{9} = VOPC;
  let TSFlags{10} = VOP3;
  let TSFlags{12} = VOP3P;

  let TSFlags{13} = VINTRP;
  let TSFlags{14} = SDWA;
  let TSFlags{15} = DPP;

  let TSFlags{16} = MUBUF;
  let TSFlags{17} = MTBUF;
  let TSFlags{18} = SMRD;
  let TSFlags{19} = MIMG;
  let TSFlags{20} = EXP;
  let TSFlags{21} = FLAT;
  let TSFlags{22} = DS;

  let TSFlags{23} = VGPRSpill;
  let TSFlags{24} = SGPRSpill;

  let TSFlags{32} = VM_CNT;
  let TSFlags{33} = EXP_CNT;
  let TSFlags{34} = LGKM_CNT;

  let TSFlags{35} = WQM;
  let TSFlags{36} = DisableWQM;
  let TSFlags{37} = Gather4;

  let TSFlags{38} = SOPKZext;
  let TSFlags{39} = ScalarStore;
  let TSFlags{40} = FixedSize;
  let TSFlags{41} = VOPAsmPrefer32Bit;
  let TSFlags{42} = VOP3_OPSEL;

  let TSFlags{43} = maybeAtomic;
  let TSFlags{44} = renamedInGFX9;

  let TSFlags{45} = FPClamp;
  let TSFlags{46} = IntClamp;
  let TSFlags{47} = ClampLo;
  let TSFlags{48} = ClampHi;

  let TSFlags{49} = IsPacked;

  let TSFlags{50} = D16Buf;

  let TSFlags{51} = FPDPRounding;

  let SchedRW = [Write32Bit];

  field bits<1> DisableSIDecoder = 0;
  field bits<1> DisableVIDecoder = 0;
  field bits<1> DisableDecoder = 0;

  let isAsmParserOnly = !if(!eq(DisableDecoder{0}, {0}), 0, 1);
  let AsmVariantName = AMDGPUAsmVariants.Default;

  // Avoid changing source registers in a way that violates constant bus read limitations.
  let hasExtraSrcRegAllocReq = !if(VOP1,1,!if(VOP2,1,!if(VOP3,1,!if(VOPC,1,!if(SDWA,1, !if(VALU,1,0))))));
}

class PseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : InstSI<outs, ins, asm, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class SPseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : PseudoInstSI<outs, ins, pattern, asm> {
  let SALU = 1;
}

class VPseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : PseudoInstSI<outs, ins, pattern, asm> {
  let VALU = 1;
  let Uses = [EXEC];
}

class CFPseudoInstSI<dag outs, dag ins, list<dag> pattern = [],
  bit UseExec = 0, bit DefExec = 0> :
  SPseudoInstSI<outs, ins, pattern> {

  let Uses = !if(UseExec, [EXEC], []);
  let Defs = !if(DefExec, [EXEC, SCC], [SCC]);
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
}

class Enc32 {
  field bits<32> Inst;
  int Size = 4;
}

class Enc64 {
  field bits<64> Inst;
  int Size = 8;
}

class VOPDstOperand <RegisterClass rc> : RegisterOperand <rc, "printVOPDst">;

class VINTRPe <bits<2> op> : Enc32 {
  bits<8> vdst;
  bits<8> vsrc;
  bits<2> attrchan;
  bits<6> attr;

  let Inst{7-0} = vsrc;
  let Inst{9-8} = attrchan;
  let Inst{15-10} = attr;
  let Inst{17-16} = op;
  let Inst{25-18} = vdst;
  let Inst{31-26} = 0x32; // encoding
}

class MIMGe <bits<7> op> : Enc64 {
  bits<8> vdata;
  bits<4> dmask;
  bits<1> unorm;
  bits<1> glc;
  bits<1> da;
  bits<1> r128;
  bits<1> tfe;
  bits<1> lwe;
  bits<1> slc;
  bit d16;
  bits<8> vaddr;
  bits<7> srsrc;
  bits<7> ssamp;

  let Inst{11-8} = dmask;
  let Inst{12} = unorm;
  let Inst{13} = glc;
  let Inst{14} = da;
  let Inst{15} = r128;
  let Inst{16} = tfe;
  let Inst{17} = lwe;
  let Inst{24-18} = op;
  let Inst{25} = slc;
  let Inst{31-26} = 0x3c;
  let Inst{39-32} = vaddr;
  let Inst{47-40} = vdata;
  let Inst{52-48} = srsrc{6-2};
  let Inst{57-53} = ssamp{6-2};
  let Inst{63} = d16;
}

class EXPe : Enc64 {
  bits<4> en;
  bits<6> tgt;
  bits<1> compr;
  bits<1> done;
  bits<1> vm;
  bits<8> src0;
  bits<8> src1;
  bits<8> src2;
  bits<8> src3;

  let Inst{3-0} = en;
  let Inst{9-4} = tgt;
  let Inst{10} = compr;
  let Inst{11} = done;
  let Inst{12} = vm;
  let Inst{31-26} = 0x3e;
  let Inst{39-32} = src0;
  let Inst{47-40} = src1;
  let Inst{55-48} = src2;
  let Inst{63-56} = src3;
}

let Uses = [EXEC] in {

class VINTRPCommon <dag outs, dag ins, string asm, list<dag> pattern> :
    InstSI <outs, ins, asm, pattern> {
  let VINTRP = 1;
  // VINTRP instructions read parameter values from LDS, but these parameter
  // values are stored outside of the LDS memory that is allocated to the
  // shader for general purpose use.
  //
  // While it may be possible for ds_read/ds_write instructions to access
  // the parameter values in LDS, this would essentially be an out-of-bounds
  // memory access which we consider to be undefined behavior.
  //
  // So even though these instructions read memory, this memory is outside the
  // addressable memory space for the shader, and we consider these instructions
  // to be readnone.
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let VALU = 1;
}

class EXPCommon<dag outs, dag ins, string asm, list<dag> pattern> :
  InstSI<outs, ins, asm, pattern> {
  let EXP = 1;
  let EXP_CNT = 1;
  let mayLoad = 0; // Set to 1 if done bit is set.
  let mayStore = 1;
  let UseNamedOperandTable = 1;
  let Uses = [EXEC];
  let SchedRW = [WriteExport];
}

} // End Uses = [EXEC]
