//===-- X86Instr3DNow.td - The 3DNow! Instruction Set ------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the 3DNow! instruction set, which extends MMX to support
// floating point and also adds a few more random instructions for good measure.
//
//===----------------------------------------------------------------------===//

class I3DNow<bits<8> o, Format F, dag outs, dag ins, string asm, list<dag> pat>
      : I<o, F, outs, ins, asm, pat>, Requires<[Has3DNow]> {
}

class I3DNow_binop<bits<8> o, Format F, dag ins, string Mnemonic, list<dag> pat>
      : I3DNow<o, F, (outs VR64:$dst), ins,
          !strconcat(Mnemonic, "\t{$src2, $dst|$dst, $src2}"), pat>, ThreeDNow {
  let Constraints = "$src1 = $dst";
}

class I3DNow_conv<bits<8> o, Format F, dag ins, string Mnemonic, list<dag> pat>
      : I3DNow<o, F, (outs VR64:$dst), ins,
          !strconcat(Mnemonic, "\t{$src, $dst|$dst, $src}"), pat>, ThreeDNow;

multiclass I3DNow_binop_rm_int<bits<8> opc, string Mn,
                               X86FoldableSchedWrite sched, bit Commutable = 0,
                               string Ver = ""> {
  let isCommutable = Commutable in
  def rr : I3DNow_binop<opc, MRMSrcReg, (ins VR64:$src1, VR64:$src2), Mn,
    [(set VR64:$dst, (!cast<Intrinsic>(
      !strconcat("int_x86_3dnow", Ver, "_", Mn)) VR64:$src1, VR64:$src2))]>,
      Sched<[sched]>;
  def rm : I3DNow_binop<opc, MRMSrcMem, (ins VR64:$src1, i64mem:$src2), Mn,
    [(set VR64:$dst, (!cast<Intrinsic>(
      !strconcat("int_x86_3dnow", Ver, "_", Mn)) VR64:$src1,
        (bitconvert (load_mmx addr:$src2))))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass I3DNow_conv_rm_int<bits<8> opc, string Mn,
                              X86FoldableSchedWrite sched, string Ver = ""> {
  def rr : I3DNow_conv<opc, MRMSrcReg, (ins VR64:$src), Mn,
    [(set VR64:$dst, (!cast<Intrinsic>(
      !strconcat("int_x86_3dnow", Ver, "_", Mn)) VR64:$src))]>,
      Sched<[sched]>;
  def rm : I3DNow_conv<opc, MRMSrcMem, (ins i64mem:$src), Mn,
    [(set VR64:$dst, (!cast<Intrinsic>(
      !strconcat("int_x86_3dnow", Ver, "_", Mn))
        (bitconvert (load_mmx addr:$src))))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

defm PAVGUSB  : I3DNow_binop_rm_int<0xBF, "pavgusb", SchedWriteVecALU.MMX, 1>;
defm PF2ID    : I3DNow_conv_rm_int<0x1D, "pf2id", WriteCvtPS2I>;
defm PFACC    : I3DNow_binop_rm_int<0xAE, "pfacc", WriteFAdd>;
defm PFADD    : I3DNow_binop_rm_int<0x9E, "pfadd", WriteFAdd, 1>;
defm PFCMPEQ  : I3DNow_binop_rm_int<0xB0, "pfcmpeq", WriteFAdd, 1>;
defm PFCMPGE  : I3DNow_binop_rm_int<0x90, "pfcmpge", WriteFAdd>;
defm PFCMPGT  : I3DNow_binop_rm_int<0xA0, "pfcmpgt", WriteFAdd>;
defm PFMAX    : I3DNow_binop_rm_int<0xA4, "pfmax", WriteFAdd>;
defm PFMIN    : I3DNow_binop_rm_int<0x94, "pfmin", WriteFAdd>;
defm PFMUL    : I3DNow_binop_rm_int<0xB4, "pfmul", WriteFAdd, 1>;
defm PFRCP    : I3DNow_conv_rm_int<0x96, "pfrcp", WriteFAdd>;
defm PFRCPIT1 : I3DNow_binop_rm_int<0xA6, "pfrcpit1", WriteFAdd>;
defm PFRCPIT2 : I3DNow_binop_rm_int<0xB6, "pfrcpit2", WriteFAdd>;
defm PFRSQIT1 : I3DNow_binop_rm_int<0xA7, "pfrsqit1", WriteFAdd>;
defm PFRSQRT  : I3DNow_conv_rm_int<0x97, "pfrsqrt", WriteFAdd>;
defm PFSUB    : I3DNow_binop_rm_int<0x9A, "pfsub", WriteFAdd, 1>;
defm PFSUBR   : I3DNow_binop_rm_int<0xAA, "pfsubr", WriteFAdd, 1>;
defm PI2FD    : I3DNow_conv_rm_int<0x0D, "pi2fd", WriteCvtI2PS>;
defm PMULHRW  : I3DNow_binop_rm_int<0xB7, "pmulhrw", SchedWriteVecIMul.MMX, 1>;

let SchedRW = [WriteEMMS] in
def FEMMS : I3DNow<0x0E, RawFrm, (outs), (ins), "femms",
                   [(int_x86_mmx_femms)]>, TB;

// PREFETCHWT1 is supported we want to use it for everything but T0.
def PrefetchWLevel : PatFrag<(ops), (i32 imm), [{
  return N->getSExtValue() == 3 || !Subtarget->hasPREFETCHWT1();
}]>;

// Use PREFETCHWT1 for NTA, T2, T1.
def PrefetchWT1Level : ImmLeaf<i32, [{
  return Imm < 3;
}]>;

let SchedRW = [WriteLoad] in {
let Predicates = [Has3DNow, NoSSEPrefetch] in
def PREFETCH : I3DNow<0x0D, MRM0m, (outs), (ins i8mem:$addr),
                      "prefetch\t$addr",
                      [(prefetch addr:$addr, imm, imm, (i32 1))]>, TB;

def PREFETCHW : I<0x0D, MRM1m, (outs), (ins i8mem:$addr), "prefetchw\t$addr",
                  [(prefetch addr:$addr, (i32 1), (i32 PrefetchWLevel), (i32 1))]>,
                  TB, Requires<[HasPrefetchW]>;

def PREFETCHWT1 : I<0x0D, MRM2m, (outs), (ins i8mem:$addr), "prefetchwt1\t$addr",
                    [(prefetch addr:$addr, (i32 1), (i32 PrefetchWT1Level), (i32 1))]>,
                    TB, Requires<[HasPREFETCHWT1]>;
}

// "3DNowA" instructions
defm PF2IW    : I3DNow_conv_rm_int<0x1C, "pf2iw", WriteCvtPS2I, "a">;
defm PI2FW    : I3DNow_conv_rm_int<0x0C, "pi2fw", WriteCvtI2PS, "a">;
defm PFNACC   : I3DNow_binop_rm_int<0x8A, "pfnacc", WriteFAdd, 0, "a">;
defm PFPNACC  : I3DNow_binop_rm_int<0x8E, "pfpnacc", WriteFAdd, 0, "a">;
defm PSWAPD   : I3DNow_conv_rm_int<0xBB, "pswapd", SchedWriteShuffle.MMX, "a">;
