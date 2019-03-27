//===-- X86InstrShiftRotate.td - Shift and Rotate Instrs ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the shift and rotate instructions.
//
//===----------------------------------------------------------------------===//

// FIXME: Someone needs to smear multipattern goodness all over this file.

let Defs = [EFLAGS] in {

let Constraints = "$src1 = $dst", SchedRW = [WriteShift] in {
let Uses = [CL], SchedRW = [WriteShiftCL] in {
def SHL8rCL  : I<0xD2, MRM4r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "shl{b}\t{%cl, $dst|$dst, cl}",
                 [(set GR8:$dst, (shl GR8:$src1, CL))]>;
def SHL16rCL : I<0xD3, MRM4r, (outs GR16:$dst), (ins GR16:$src1),
                 "shl{w}\t{%cl, $dst|$dst, cl}",
                 [(set GR16:$dst, (shl GR16:$src1, CL))]>, OpSize16;
def SHL32rCL : I<0xD3, MRM4r, (outs GR32:$dst), (ins GR32:$src1),
                 "shl{l}\t{%cl, $dst|$dst, cl}",
                 [(set GR32:$dst, (shl GR32:$src1, CL))]>, OpSize32;
def SHL64rCL : RI<0xD3, MRM4r, (outs GR64:$dst), (ins GR64:$src1),
                  "shl{q}\t{%cl, $dst|$dst, cl}",
                  [(set GR64:$dst, (shl GR64:$src1, CL))]>;
} // Uses = [CL], SchedRW

def SHL8ri   : Ii8<0xC0, MRM4r, (outs GR8 :$dst), (ins GR8 :$src1, u8imm:$src2),
                   "shl{b}\t{$src2, $dst|$dst, $src2}",
                   [(set GR8:$dst, (shl GR8:$src1, (i8 imm:$src2)))]>;

let isConvertibleToThreeAddress = 1 in {   // Can transform into LEA.
def SHL16ri  : Ii8<0xC1, MRM4r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$src2),
                   "shl{w}\t{$src2, $dst|$dst, $src2}",
                   [(set GR16:$dst, (shl GR16:$src1, (i8 imm:$src2)))]>,
                   OpSize16;
def SHL32ri  : Ii8<0xC1, MRM4r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$src2),
                   "shl{l}\t{$src2, $dst|$dst, $src2}",
                   [(set GR32:$dst, (shl GR32:$src1, (i8 imm:$src2)))]>,
                   OpSize32;
def SHL64ri  : RIi8<0xC1, MRM4r, (outs GR64:$dst),
                    (ins GR64:$src1, u8imm:$src2),
                    "shl{q}\t{$src2, $dst|$dst, $src2}",
                    [(set GR64:$dst, (shl GR64:$src1, (i8 imm:$src2)))]>;
} // isConvertibleToThreeAddress = 1

// NOTE: We don't include patterns for shifts of a register by one, because
// 'add reg,reg' is cheaper (and we have a Pat pattern for shift-by-one).
let hasSideEffects = 0 in {
def SHL8r1   : I<0xD0, MRM4r, (outs GR8:$dst), (ins GR8:$src1),
                 "shl{b}\t$dst", []>;
def SHL16r1  : I<0xD1, MRM4r, (outs GR16:$dst), (ins GR16:$src1),
                 "shl{w}\t$dst", []>, OpSize16;
def SHL32r1  : I<0xD1, MRM4r, (outs GR32:$dst), (ins GR32:$src1),
                 "shl{l}\t$dst", []>, OpSize32;
def SHL64r1  : RI<0xD1, MRM4r, (outs GR64:$dst), (ins GR64:$src1),
                 "shl{q}\t$dst", []>;
} // hasSideEffects = 0
} // Constraints = "$src = $dst", SchedRW

// FIXME: Why do we need an explicit "Uses = [CL]" when the instr has a pattern
// using CL?
let Uses = [CL], SchedRW = [WriteShiftCLLd, WriteRMW] in {
def SHL8mCL  : I<0xD2, MRM4m, (outs), (ins i8mem :$dst),
                 "shl{b}\t{%cl, $dst|$dst, cl}",
                 [(store (shl (loadi8 addr:$dst), CL), addr:$dst)]>;
def SHL16mCL : I<0xD3, MRM4m, (outs), (ins i16mem:$dst),
                 "shl{w}\t{%cl, $dst|$dst, cl}",
                 [(store (shl (loadi16 addr:$dst), CL), addr:$dst)]>,
                 OpSize16;
def SHL32mCL : I<0xD3, MRM4m, (outs), (ins i32mem:$dst),
                 "shl{l}\t{%cl, $dst|$dst, cl}",
                 [(store (shl (loadi32 addr:$dst), CL), addr:$dst)]>,
                 OpSize32;
def SHL64mCL : RI<0xD3, MRM4m, (outs), (ins i64mem:$dst),
                  "shl{q}\t{%cl, $dst|$dst, cl}",
                  [(store (shl (loadi64 addr:$dst), CL), addr:$dst)]>,
                  Requires<[In64BitMode]>;
}

let SchedRW = [WriteShiftLd, WriteRMW] in {
def SHL8mi   : Ii8<0xC0, MRM4m, (outs), (ins i8mem :$dst, u8imm:$src),
                   "shl{b}\t{$src, $dst|$dst, $src}",
                [(store (shl (loadi8 addr:$dst), (i8 imm:$src)), addr:$dst)]>;
def SHL16mi  : Ii8<0xC1, MRM4m, (outs), (ins i16mem:$dst, u8imm:$src),
                   "shl{w}\t{$src, $dst|$dst, $src}",
               [(store (shl (loadi16 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize16;
def SHL32mi  : Ii8<0xC1, MRM4m, (outs), (ins i32mem:$dst, u8imm:$src),
                   "shl{l}\t{$src, $dst|$dst, $src}",
               [(store (shl (loadi32 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize32;
def SHL64mi : RIi8<0xC1, MRM4m, (outs), (ins i64mem:$dst, u8imm:$src),
                  "shl{q}\t{$src, $dst|$dst, $src}",
                  [(store (shl (loadi64 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                  Requires<[In64BitMode]>;

// Shift by 1
def SHL8m1   : I<0xD0, MRM4m, (outs), (ins i8mem :$dst),
                 "shl{b}\t$dst",
                [(store (shl (loadi8 addr:$dst), (i8 1)), addr:$dst)]>;
def SHL16m1  : I<0xD1, MRM4m, (outs), (ins i16mem:$dst),
                 "shl{w}\t$dst",
                 [(store (shl (loadi16 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize16;
def SHL32m1  : I<0xD1, MRM4m, (outs), (ins i32mem:$dst),
                 "shl{l}\t$dst",
                 [(store (shl (loadi32 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize32;
def SHL64m1 : RI<0xD1, MRM4m, (outs), (ins i64mem:$dst),
                  "shl{q}\t$dst",
                 [(store (shl (loadi64 addr:$dst), (i8 1)), addr:$dst)]>,
                 Requires<[In64BitMode]>;
} // SchedRW

let Constraints = "$src1 = $dst", SchedRW = [WriteShift] in {
let Uses = [CL], SchedRW = [WriteShiftCL] in {
def SHR8rCL  : I<0xD2, MRM5r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "shr{b}\t{%cl, $dst|$dst, cl}",
                 [(set GR8:$dst, (srl GR8:$src1, CL))]>;
def SHR16rCL : I<0xD3, MRM5r, (outs GR16:$dst), (ins GR16:$src1),
                 "shr{w}\t{%cl, $dst|$dst, cl}",
                 [(set GR16:$dst, (srl GR16:$src1, CL))]>, OpSize16;
def SHR32rCL : I<0xD3, MRM5r, (outs GR32:$dst), (ins GR32:$src1),
                 "shr{l}\t{%cl, $dst|$dst, cl}",
                 [(set GR32:$dst, (srl GR32:$src1, CL))]>, OpSize32;
def SHR64rCL : RI<0xD3, MRM5r, (outs GR64:$dst), (ins GR64:$src1),
                  "shr{q}\t{%cl, $dst|$dst, cl}",
                  [(set GR64:$dst, (srl GR64:$src1, CL))]>;
}

def SHR8ri   : Ii8<0xC0, MRM5r, (outs GR8:$dst), (ins GR8:$src1, u8imm:$src2),
                   "shr{b}\t{$src2, $dst|$dst, $src2}",
                   [(set GR8:$dst, (srl GR8:$src1, (i8 imm:$src2)))]>;
def SHR16ri  : Ii8<0xC1, MRM5r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$src2),
                   "shr{w}\t{$src2, $dst|$dst, $src2}",
                   [(set GR16:$dst, (srl GR16:$src1, (i8 imm:$src2)))]>,
                   OpSize16;
def SHR32ri  : Ii8<0xC1, MRM5r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$src2),
                   "shr{l}\t{$src2, $dst|$dst, $src2}",
                   [(set GR32:$dst, (srl GR32:$src1, (i8 imm:$src2)))]>,
                   OpSize32;
def SHR64ri : RIi8<0xC1, MRM5r, (outs GR64:$dst), (ins GR64:$src1, u8imm:$src2),
                  "shr{q}\t{$src2, $dst|$dst, $src2}",
                  [(set GR64:$dst, (srl GR64:$src1, (i8 imm:$src2)))]>;

// Shift right by 1
def SHR8r1   : I<0xD0, MRM5r, (outs GR8:$dst), (ins GR8:$src1),
                 "shr{b}\t$dst",
                 [(set GR8:$dst, (srl GR8:$src1, (i8 1)))]>;
def SHR16r1  : I<0xD1, MRM5r, (outs GR16:$dst), (ins GR16:$src1),
                 "shr{w}\t$dst",
                 [(set GR16:$dst, (srl GR16:$src1, (i8 1)))]>, OpSize16;
def SHR32r1  : I<0xD1, MRM5r, (outs GR32:$dst), (ins GR32:$src1),
                 "shr{l}\t$dst",
                 [(set GR32:$dst, (srl GR32:$src1, (i8 1)))]>, OpSize32;
def SHR64r1  : RI<0xD1, MRM5r, (outs GR64:$dst), (ins GR64:$src1),
                 "shr{q}\t$dst",
                 [(set GR64:$dst, (srl GR64:$src1, (i8 1)))]>;
} // Constraints = "$src = $dst", SchedRW


let Uses = [CL], SchedRW = [WriteShiftCLLd, WriteRMW] in {
def SHR8mCL  : I<0xD2, MRM5m, (outs), (ins i8mem :$dst),
                 "shr{b}\t{%cl, $dst|$dst, cl}",
                 [(store (srl (loadi8 addr:$dst), CL), addr:$dst)]>;
def SHR16mCL : I<0xD3, MRM5m, (outs), (ins i16mem:$dst),
                 "shr{w}\t{%cl, $dst|$dst, cl}",
                 [(store (srl (loadi16 addr:$dst), CL), addr:$dst)]>,
                 OpSize16;
def SHR32mCL : I<0xD3, MRM5m, (outs), (ins i32mem:$dst),
                 "shr{l}\t{%cl, $dst|$dst, cl}",
                 [(store (srl (loadi32 addr:$dst), CL), addr:$dst)]>,
                 OpSize32;
def SHR64mCL : RI<0xD3, MRM5m, (outs), (ins i64mem:$dst),
                  "shr{q}\t{%cl, $dst|$dst, cl}",
                  [(store (srl (loadi64 addr:$dst), CL), addr:$dst)]>,
                  Requires<[In64BitMode]>;
}

let SchedRW = [WriteShiftLd, WriteRMW] in {
def SHR8mi   : Ii8<0xC0, MRM5m, (outs), (ins i8mem :$dst, u8imm:$src),
                   "shr{b}\t{$src, $dst|$dst, $src}",
                [(store (srl (loadi8 addr:$dst), (i8 imm:$src)), addr:$dst)]>;
def SHR16mi  : Ii8<0xC1, MRM5m, (outs), (ins i16mem:$dst, u8imm:$src),
                   "shr{w}\t{$src, $dst|$dst, $src}",
               [(store (srl (loadi16 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize16;
def SHR32mi  : Ii8<0xC1, MRM5m, (outs), (ins i32mem:$dst, u8imm:$src),
                   "shr{l}\t{$src, $dst|$dst, $src}",
               [(store (srl (loadi32 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize32;
def SHR64mi : RIi8<0xC1, MRM5m, (outs), (ins i64mem:$dst, u8imm:$src),
                  "shr{q}\t{$src, $dst|$dst, $src}",
                 [(store (srl (loadi64 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                 Requires<[In64BitMode]>;

// Shift by 1
def SHR8m1   : I<0xD0, MRM5m, (outs), (ins i8mem :$dst),
                 "shr{b}\t$dst",
                 [(store (srl (loadi8 addr:$dst), (i8 1)), addr:$dst)]>;
def SHR16m1  : I<0xD1, MRM5m, (outs), (ins i16mem:$dst),
                 "shr{w}\t$dst",
                 [(store (srl (loadi16 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize16;
def SHR32m1  : I<0xD1, MRM5m, (outs), (ins i32mem:$dst),
                 "shr{l}\t$dst",
                 [(store (srl (loadi32 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize32;
def SHR64m1 : RI<0xD1, MRM5m, (outs), (ins i64mem:$dst),
                  "shr{q}\t$dst",
                 [(store (srl (loadi64 addr:$dst), (i8 1)), addr:$dst)]>,
                 Requires<[In64BitMode]>;
} // SchedRW

let Constraints = "$src1 = $dst", SchedRW = [WriteShift] in {
let Uses = [CL], SchedRW = [WriteShiftCL] in {
def SAR8rCL  : I<0xD2, MRM7r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "sar{b}\t{%cl, $dst|$dst, cl}",
                 [(set GR8:$dst, (sra GR8:$src1, CL))]>;
def SAR16rCL : I<0xD3, MRM7r, (outs GR16:$dst), (ins GR16:$src1),
                 "sar{w}\t{%cl, $dst|$dst, cl}",
                 [(set GR16:$dst, (sra GR16:$src1, CL))]>,
                 OpSize16;
def SAR32rCL : I<0xD3, MRM7r, (outs GR32:$dst), (ins GR32:$src1),
                 "sar{l}\t{%cl, $dst|$dst, cl}",
                 [(set GR32:$dst, (sra GR32:$src1, CL))]>,
                 OpSize32;
def SAR64rCL : RI<0xD3, MRM7r, (outs GR64:$dst), (ins GR64:$src1),
                 "sar{q}\t{%cl, $dst|$dst, cl}",
                 [(set GR64:$dst, (sra GR64:$src1, CL))]>;
}

def SAR8ri   : Ii8<0xC0, MRM7r, (outs GR8 :$dst), (ins GR8 :$src1, u8imm:$src2),
                   "sar{b}\t{$src2, $dst|$dst, $src2}",
                   [(set GR8:$dst, (sra GR8:$src1, (i8 imm:$src2)))]>;
def SAR16ri  : Ii8<0xC1, MRM7r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$src2),
                   "sar{w}\t{$src2, $dst|$dst, $src2}",
                   [(set GR16:$dst, (sra GR16:$src1, (i8 imm:$src2)))]>,
                   OpSize16;
def SAR32ri  : Ii8<0xC1, MRM7r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$src2),
                   "sar{l}\t{$src2, $dst|$dst, $src2}",
                   [(set GR32:$dst, (sra GR32:$src1, (i8 imm:$src2)))]>,
                   OpSize32;
def SAR64ri  : RIi8<0xC1, MRM7r, (outs GR64:$dst),
                    (ins GR64:$src1, u8imm:$src2),
                    "sar{q}\t{$src2, $dst|$dst, $src2}",
                    [(set GR64:$dst, (sra GR64:$src1, (i8 imm:$src2)))]>;

// Shift by 1
def SAR8r1   : I<0xD0, MRM7r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "sar{b}\t$dst",
                 [(set GR8:$dst, (sra GR8:$src1, (i8 1)))]>;
def SAR16r1  : I<0xD1, MRM7r, (outs GR16:$dst), (ins GR16:$src1),
                 "sar{w}\t$dst",
                 [(set GR16:$dst, (sra GR16:$src1, (i8 1)))]>, OpSize16;
def SAR32r1  : I<0xD1, MRM7r, (outs GR32:$dst), (ins GR32:$src1),
                 "sar{l}\t$dst",
                 [(set GR32:$dst, (sra GR32:$src1, (i8 1)))]>, OpSize32;
def SAR64r1  : RI<0xD1, MRM7r, (outs GR64:$dst), (ins GR64:$src1),
                  "sar{q}\t$dst",
                  [(set GR64:$dst, (sra GR64:$src1, (i8 1)))]>;
} // Constraints = "$src = $dst", SchedRW


let Uses = [CL], SchedRW = [WriteShiftCLLd, WriteRMW] in {
def SAR8mCL  : I<0xD2, MRM7m, (outs), (ins i8mem :$dst),
                 "sar{b}\t{%cl, $dst|$dst, cl}",
                 [(store (sra (loadi8 addr:$dst), CL), addr:$dst)]>;
def SAR16mCL : I<0xD3, MRM7m, (outs), (ins i16mem:$dst),
                 "sar{w}\t{%cl, $dst|$dst, cl}",
                 [(store (sra (loadi16 addr:$dst), CL), addr:$dst)]>,
                 OpSize16;
def SAR32mCL : I<0xD3, MRM7m, (outs), (ins i32mem:$dst),
                 "sar{l}\t{%cl, $dst|$dst, cl}",
                 [(store (sra (loadi32 addr:$dst), CL), addr:$dst)]>,
                 OpSize32;
def SAR64mCL : RI<0xD3, MRM7m, (outs), (ins i64mem:$dst),
                 "sar{q}\t{%cl, $dst|$dst, cl}",
                 [(store (sra (loadi64 addr:$dst), CL), addr:$dst)]>,
                 Requires<[In64BitMode]>;
}

let SchedRW = [WriteShiftLd, WriteRMW] in {
def SAR8mi   : Ii8<0xC0, MRM7m, (outs), (ins i8mem :$dst, u8imm:$src),
                   "sar{b}\t{$src, $dst|$dst, $src}",
                [(store (sra (loadi8 addr:$dst), (i8 imm:$src)), addr:$dst)]>;
def SAR16mi  : Ii8<0xC1, MRM7m, (outs), (ins i16mem:$dst, u8imm:$src),
                   "sar{w}\t{$src, $dst|$dst, $src}",
               [(store (sra (loadi16 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize16;
def SAR32mi  : Ii8<0xC1, MRM7m, (outs), (ins i32mem:$dst, u8imm:$src),
                   "sar{l}\t{$src, $dst|$dst, $src}",
               [(store (sra (loadi32 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
               OpSize32;
def SAR64mi  : RIi8<0xC1, MRM7m, (outs), (ins i64mem:$dst, u8imm:$src),
                    "sar{q}\t{$src, $dst|$dst, $src}",
                 [(store (sra (loadi64 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                 Requires<[In64BitMode]>;

// Shift by 1
def SAR8m1   : I<0xD0, MRM7m, (outs), (ins i8mem :$dst),
                 "sar{b}\t$dst",
                [(store (sra (loadi8 addr:$dst), (i8 1)), addr:$dst)]>;
def SAR16m1  : I<0xD1, MRM7m, (outs), (ins i16mem:$dst),
                 "sar{w}\t$dst",
               [(store (sra (loadi16 addr:$dst), (i8 1)), addr:$dst)]>,
               OpSize16;
def SAR32m1  : I<0xD1, MRM7m, (outs), (ins i32mem:$dst),
                 "sar{l}\t$dst",
               [(store (sra (loadi32 addr:$dst), (i8 1)), addr:$dst)]>,
               OpSize32;
def SAR64m1 : RI<0xD1, MRM7m, (outs), (ins i64mem:$dst),
                  "sar{q}\t$dst",
                 [(store (sra (loadi64 addr:$dst), (i8 1)), addr:$dst)]>,
                 Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Rotate instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 0 in {
let Constraints = "$src1 = $dst", SchedRW = [WriteRotate] in {

let Uses = [CL, EFLAGS], SchedRW = [WriteRotateCL] in {
def RCL8rCL : I<0xD2, MRM2r, (outs GR8:$dst), (ins GR8:$src1),
                "rcl{b}\t{%cl, $dst|$dst, cl}", []>;
def RCL16rCL : I<0xD3, MRM2r, (outs GR16:$dst), (ins GR16:$src1),
                 "rcl{w}\t{%cl, $dst|$dst, cl}", []>, OpSize16;
def RCL32rCL : I<0xD3, MRM2r, (outs GR32:$dst), (ins GR32:$src1),
                 "rcl{l}\t{%cl, $dst|$dst, cl}", []>, OpSize32;
def RCL64rCL : RI<0xD3, MRM2r, (outs GR64:$dst), (ins GR64:$src1),
                  "rcl{q}\t{%cl, $dst|$dst, cl}", []>;
} // Uses = [CL, EFLAGS]

let Uses = [EFLAGS] in {
def RCL8r1 : I<0xD0, MRM2r, (outs GR8:$dst), (ins GR8:$src1),
               "rcl{b}\t$dst", []>;
def RCL8ri : Ii8<0xC0, MRM2r, (outs GR8:$dst), (ins GR8:$src1, u8imm:$cnt),
                 "rcl{b}\t{$cnt, $dst|$dst, $cnt}", []>;
def RCL16r1 : I<0xD1, MRM2r, (outs GR16:$dst), (ins GR16:$src1),
                "rcl{w}\t$dst", []>, OpSize16;
def RCL16ri : Ii8<0xC1, MRM2r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$cnt),
                  "rcl{w}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize16;
def RCL32r1 : I<0xD1, MRM2r, (outs GR32:$dst), (ins GR32:$src1),
                "rcl{l}\t$dst", []>, OpSize32;
def RCL32ri : Ii8<0xC1, MRM2r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$cnt),
                  "rcl{l}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize32;
def RCL64r1 : RI<0xD1, MRM2r, (outs GR64:$dst), (ins GR64:$src1),
                 "rcl{q}\t$dst", []>;
def RCL64ri : RIi8<0xC1, MRM2r, (outs GR64:$dst), (ins GR64:$src1, u8imm:$cnt),
                   "rcl{q}\t{$cnt, $dst|$dst, $cnt}", []>;
} // Uses = [EFLAGS]

let Uses = [CL, EFLAGS], SchedRW = [WriteRotateCL] in {
def RCR8rCL : I<0xD2, MRM3r, (outs GR8:$dst), (ins GR8:$src1),
                "rcr{b}\t{%cl, $dst|$dst, cl}", []>;
def RCR16rCL : I<0xD3, MRM3r, (outs GR16:$dst), (ins GR16:$src1),
                 "rcr{w}\t{%cl, $dst|$dst, cl}", []>, OpSize16;
def RCR32rCL : I<0xD3, MRM3r, (outs GR32:$dst), (ins GR32:$src1),
                 "rcr{l}\t{%cl, $dst|$dst, cl}", []>, OpSize32;
def RCR64rCL : RI<0xD3, MRM3r, (outs GR64:$dst), (ins GR64:$src1),
                  "rcr{q}\t{%cl, $dst|$dst, cl}", []>;
} // Uses = [CL, EFLAGS]

let Uses = [EFLAGS] in {
def RCR8r1 : I<0xD0, MRM3r, (outs GR8:$dst), (ins GR8:$src1),
               "rcr{b}\t$dst", []>;
def RCR8ri : Ii8<0xC0, MRM3r, (outs GR8:$dst), (ins GR8:$src1, u8imm:$cnt),
                 "rcr{b}\t{$cnt, $dst|$dst, $cnt}", []>;
def RCR16r1 : I<0xD1, MRM3r, (outs GR16:$dst), (ins GR16:$src1),
                "rcr{w}\t$dst", []>, OpSize16;
def RCR16ri : Ii8<0xC1, MRM3r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$cnt),
                  "rcr{w}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize16;
def RCR32r1 : I<0xD1, MRM3r, (outs GR32:$dst), (ins GR32:$src1),
                "rcr{l}\t$dst", []>, OpSize32;
def RCR32ri : Ii8<0xC1, MRM3r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$cnt),
                  "rcr{l}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize32;
def RCR64r1 : RI<0xD1, MRM3r, (outs GR64:$dst), (ins GR64:$src1),
                 "rcr{q}\t$dst", []>;
def RCR64ri : RIi8<0xC1, MRM3r, (outs GR64:$dst), (ins GR64:$src1, u8imm:$cnt),
                   "rcr{q}\t{$cnt, $dst|$dst, $cnt}", []>;
} // Uses = [EFLAGS]

} // Constraints = "$src = $dst"

let SchedRW = [WriteRotateLd, WriteRMW], mayStore = 1 in {
let Uses = [EFLAGS] in {
def RCL8m1 : I<0xD0, MRM2m, (outs), (ins i8mem:$dst),
               "rcl{b}\t$dst", []>;
def RCL8mi : Ii8<0xC0, MRM2m, (outs), (ins i8mem:$dst, u8imm:$cnt),
                 "rcl{b}\t{$cnt, $dst|$dst, $cnt}", []>;
def RCL16m1 : I<0xD1, MRM2m, (outs), (ins i16mem:$dst),
                "rcl{w}\t$dst", []>, OpSize16;
def RCL16mi : Ii8<0xC1, MRM2m, (outs), (ins i16mem:$dst, u8imm:$cnt),
                  "rcl{w}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize16;
def RCL32m1 : I<0xD1, MRM2m, (outs), (ins i32mem:$dst),
                "rcl{l}\t$dst", []>, OpSize32;
def RCL32mi : Ii8<0xC1, MRM2m, (outs), (ins i32mem:$dst, u8imm:$cnt),
                  "rcl{l}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize32;
def RCL64m1 : RI<0xD1, MRM2m, (outs), (ins i64mem:$dst),
                 "rcl{q}\t$dst", []>, Requires<[In64BitMode]>;
def RCL64mi : RIi8<0xC1, MRM2m, (outs), (ins i64mem:$dst, u8imm:$cnt),
                   "rcl{q}\t{$cnt, $dst|$dst, $cnt}", []>,
                   Requires<[In64BitMode]>;

def RCR8m1 : I<0xD0, MRM3m, (outs), (ins i8mem:$dst),
               "rcr{b}\t$dst", []>;
def RCR8mi : Ii8<0xC0, MRM3m, (outs), (ins i8mem:$dst, u8imm:$cnt),
                 "rcr{b}\t{$cnt, $dst|$dst, $cnt}", []>;
def RCR16m1 : I<0xD1, MRM3m, (outs), (ins i16mem:$dst),
                "rcr{w}\t$dst", []>, OpSize16;
def RCR16mi : Ii8<0xC1, MRM3m, (outs), (ins i16mem:$dst, u8imm:$cnt),
                  "rcr{w}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize16;
def RCR32m1 : I<0xD1, MRM3m, (outs), (ins i32mem:$dst),
                "rcr{l}\t$dst", []>, OpSize32;
def RCR32mi : Ii8<0xC1, MRM3m, (outs), (ins i32mem:$dst, u8imm:$cnt),
                  "rcr{l}\t{$cnt, $dst|$dst, $cnt}", []>, OpSize32;
def RCR64m1 : RI<0xD1, MRM3m, (outs), (ins i64mem:$dst),
                 "rcr{q}\t$dst", []>, Requires<[In64BitMode]>;
def RCR64mi : RIi8<0xC1, MRM3m, (outs), (ins i64mem:$dst, u8imm:$cnt),
                   "rcr{q}\t{$cnt, $dst|$dst, $cnt}", []>,
                   Requires<[In64BitMode]>;
} // Uses = [EFLAGS]

let Uses = [CL, EFLAGS], SchedRW = [WriteRotateCLLd, WriteRMW] in {
def RCL8mCL : I<0xD2, MRM2m, (outs), (ins i8mem:$dst),
                "rcl{b}\t{%cl, $dst|$dst, cl}", []>;
def RCL16mCL : I<0xD3, MRM2m, (outs), (ins i16mem:$dst),
                 "rcl{w}\t{%cl, $dst|$dst, cl}", []>, OpSize16;
def RCL32mCL : I<0xD3, MRM2m, (outs), (ins i32mem:$dst),
                 "rcl{l}\t{%cl, $dst|$dst, cl}", []>, OpSize32;
def RCL64mCL : RI<0xD3, MRM2m, (outs), (ins i64mem:$dst),
                  "rcl{q}\t{%cl, $dst|$dst, cl}", []>,
                  Requires<[In64BitMode]>;

def RCR8mCL : I<0xD2, MRM3m, (outs), (ins i8mem:$dst),
                "rcr{b}\t{%cl, $dst|$dst, cl}", []>;
def RCR16mCL : I<0xD3, MRM3m, (outs), (ins i16mem:$dst),
                 "rcr{w}\t{%cl, $dst|$dst, cl}", []>, OpSize16;
def RCR32mCL : I<0xD3, MRM3m, (outs), (ins i32mem:$dst),
                 "rcr{l}\t{%cl, $dst|$dst, cl}", []>, OpSize32;
def RCR64mCL : RI<0xD3, MRM3m, (outs), (ins i64mem:$dst),
                  "rcr{q}\t{%cl, $dst|$dst, cl}", []>,
                  Requires<[In64BitMode]>;
} // Uses = [CL, EFLAGS]
} // SchedRW
} // hasSideEffects = 0

let Constraints = "$src1 = $dst", SchedRW = [WriteRotate] in {
// FIXME: provide shorter instructions when imm8 == 1
let Uses = [CL], SchedRW = [WriteRotateCL] in {
def ROL8rCL  : I<0xD2, MRM0r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "rol{b}\t{%cl, $dst|$dst, cl}",
                 [(set GR8:$dst, (rotl GR8:$src1, CL))]>;
def ROL16rCL : I<0xD3, MRM0r, (outs GR16:$dst), (ins GR16:$src1),
                 "rol{w}\t{%cl, $dst|$dst, cl}",
                 [(set GR16:$dst, (rotl GR16:$src1, CL))]>, OpSize16;
def ROL32rCL : I<0xD3, MRM0r, (outs GR32:$dst), (ins GR32:$src1),
                 "rol{l}\t{%cl, $dst|$dst, cl}",
                 [(set GR32:$dst, (rotl GR32:$src1, CL))]>, OpSize32;
def ROL64rCL : RI<0xD3, MRM0r, (outs GR64:$dst), (ins GR64:$src1),
                  "rol{q}\t{%cl, $dst|$dst, cl}",
                  [(set GR64:$dst, (rotl GR64:$src1, CL))]>;
}

def ROL8ri   : Ii8<0xC0, MRM0r, (outs GR8 :$dst), (ins GR8 :$src1, u8imm:$src2),
                   "rol{b}\t{$src2, $dst|$dst, $src2}",
                   [(set GR8:$dst, (rotl GR8:$src1, (i8 imm:$src2)))]>;
def ROL16ri  : Ii8<0xC1, MRM0r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$src2),
                   "rol{w}\t{$src2, $dst|$dst, $src2}",
                   [(set GR16:$dst, (rotl GR16:$src1, (i8 imm:$src2)))]>, OpSize16;
def ROL32ri  : Ii8<0xC1, MRM0r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$src2),
                   "rol{l}\t{$src2, $dst|$dst, $src2}",
                   [(set GR32:$dst, (rotl GR32:$src1, (i8 imm:$src2)))]>, OpSize32;
def ROL64ri  : RIi8<0xC1, MRM0r, (outs GR64:$dst),
                    (ins GR64:$src1, u8imm:$src2),
                    "rol{q}\t{$src2, $dst|$dst, $src2}",
                    [(set GR64:$dst, (rotl GR64:$src1, (i8 imm:$src2)))]>;

// Rotate by 1
def ROL8r1   : I<0xD0, MRM0r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "rol{b}\t$dst",
                 [(set GR8:$dst, (rotl GR8:$src1, (i8 1)))]>;
def ROL16r1  : I<0xD1, MRM0r, (outs GR16:$dst), (ins GR16:$src1),
                 "rol{w}\t$dst",
                 [(set GR16:$dst, (rotl GR16:$src1, (i8 1)))]>, OpSize16;
def ROL32r1  : I<0xD1, MRM0r, (outs GR32:$dst), (ins GR32:$src1),
                 "rol{l}\t$dst",
                 [(set GR32:$dst, (rotl GR32:$src1, (i8 1)))]>, OpSize32;
def ROL64r1  : RI<0xD1, MRM0r, (outs GR64:$dst), (ins GR64:$src1),
                  "rol{q}\t$dst",
                  [(set GR64:$dst, (rotl GR64:$src1, (i8 1)))]>;
} // Constraints = "$src = $dst", SchedRW

let Uses = [CL], SchedRW = [WriteRotateCLLd, WriteRMW] in {
def ROL8mCL  : I<0xD2, MRM0m, (outs), (ins i8mem :$dst),
                 "rol{b}\t{%cl, $dst|$dst, cl}",
                 [(store (rotl (loadi8 addr:$dst), CL), addr:$dst)]>;
def ROL16mCL : I<0xD3, MRM0m, (outs), (ins i16mem:$dst),
                 "rol{w}\t{%cl, $dst|$dst, cl}",
                 [(store (rotl (loadi16 addr:$dst), CL), addr:$dst)]>, OpSize16;
def ROL32mCL : I<0xD3, MRM0m, (outs), (ins i32mem:$dst),
                 "rol{l}\t{%cl, $dst|$dst, cl}",
                 [(store (rotl (loadi32 addr:$dst), CL), addr:$dst)]>, OpSize32;
def ROL64mCL :  RI<0xD3, MRM0m, (outs), (ins i64mem:$dst),
                   "rol{q}\t{%cl, $dst|$dst, cl}",
                   [(store (rotl (loadi64 addr:$dst), CL), addr:$dst)]>,
                   Requires<[In64BitMode]>;
}

let SchedRW = [WriteRotateLd, WriteRMW] in {
def ROL8mi   : Ii8<0xC0, MRM0m, (outs), (ins i8mem :$dst, u8imm:$src1),
                   "rol{b}\t{$src1, $dst|$dst, $src1}",
               [(store (rotl (loadi8 addr:$dst), (i8 imm:$src1)), addr:$dst)]>;
def ROL16mi  : Ii8<0xC1, MRM0m, (outs), (ins i16mem:$dst, u8imm:$src1),
                   "rol{w}\t{$src1, $dst|$dst, $src1}",
              [(store (rotl (loadi16 addr:$dst), (i8 imm:$src1)), addr:$dst)]>,
              OpSize16;
def ROL32mi  : Ii8<0xC1, MRM0m, (outs), (ins i32mem:$dst, u8imm:$src1),
                   "rol{l}\t{$src1, $dst|$dst, $src1}",
              [(store (rotl (loadi32 addr:$dst), (i8 imm:$src1)), addr:$dst)]>,
              OpSize32;
def ROL64mi  : RIi8<0xC1, MRM0m, (outs), (ins i64mem:$dst, u8imm:$src1),
                    "rol{q}\t{$src1, $dst|$dst, $src1}",
                [(store (rotl (loadi64 addr:$dst), (i8 imm:$src1)), addr:$dst)]>,
                Requires<[In64BitMode]>;

// Rotate by 1
def ROL8m1   : I<0xD0, MRM0m, (outs), (ins i8mem :$dst),
                 "rol{b}\t$dst",
                 [(store (rotl (loadi8 addr:$dst), (i8 1)), addr:$dst)]>;
def ROL16m1  : I<0xD1, MRM0m, (outs), (ins i16mem:$dst),
                 "rol{w}\t$dst",
                 [(store (rotl (loadi16 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize16;
def ROL32m1  : I<0xD1, MRM0m, (outs), (ins i32mem:$dst),
                 "rol{l}\t$dst",
                 [(store (rotl (loadi32 addr:$dst), (i8 1)), addr:$dst)]>,
                 OpSize32;
def ROL64m1  : RI<0xD1, MRM0m, (outs), (ins i64mem:$dst),
                 "rol{q}\t$dst",
                 [(store (rotl (loadi64 addr:$dst), (i8 1)), addr:$dst)]>,
                 Requires<[In64BitMode]>;
} // SchedRW

let Constraints = "$src1 = $dst", SchedRW = [WriteRotate] in {
let Uses = [CL], SchedRW = [WriteRotateCL] in {
def ROR8rCL  : I<0xD2, MRM1r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "ror{b}\t{%cl, $dst|$dst, cl}",
                 [(set GR8:$dst, (rotr GR8:$src1, CL))]>;
def ROR16rCL : I<0xD3, MRM1r, (outs GR16:$dst), (ins GR16:$src1),
                 "ror{w}\t{%cl, $dst|$dst, cl}",
                 [(set GR16:$dst, (rotr GR16:$src1, CL))]>, OpSize16;
def ROR32rCL : I<0xD3, MRM1r, (outs GR32:$dst), (ins GR32:$src1),
                 "ror{l}\t{%cl, $dst|$dst, cl}",
                 [(set GR32:$dst, (rotr GR32:$src1, CL))]>, OpSize32;
def ROR64rCL : RI<0xD3, MRM1r, (outs GR64:$dst), (ins GR64:$src1),
                  "ror{q}\t{%cl, $dst|$dst, cl}",
                  [(set GR64:$dst, (rotr GR64:$src1, CL))]>;
}

def ROR8ri   : Ii8<0xC0, MRM1r, (outs GR8 :$dst), (ins GR8 :$src1, u8imm:$src2),
                   "ror{b}\t{$src2, $dst|$dst, $src2}",
                   [(set GR8:$dst, (rotr GR8:$src1, (i8 relocImm:$src2)))]>;
def ROR16ri  : Ii8<0xC1, MRM1r, (outs GR16:$dst), (ins GR16:$src1, u8imm:$src2),
                   "ror{w}\t{$src2, $dst|$dst, $src2}",
                   [(set GR16:$dst, (rotr GR16:$src1, (i8 relocImm:$src2)))]>,
                   OpSize16;
def ROR32ri  : Ii8<0xC1, MRM1r, (outs GR32:$dst), (ins GR32:$src1, u8imm:$src2),
                   "ror{l}\t{$src2, $dst|$dst, $src2}",
                   [(set GR32:$dst, (rotr GR32:$src1, (i8 relocImm:$src2)))]>,
                   OpSize32;
def ROR64ri  : RIi8<0xC1, MRM1r, (outs GR64:$dst),
                    (ins GR64:$src1, u8imm:$src2),
                    "ror{q}\t{$src2, $dst|$dst, $src2}",
                    [(set GR64:$dst, (rotr GR64:$src1, (i8 relocImm:$src2)))]>;

// Rotate by 1
def ROR8r1   : I<0xD0, MRM1r, (outs GR8 :$dst), (ins GR8 :$src1),
                 "ror{b}\t$dst",
                 [(set GR8:$dst, (rotl GR8:$src1, (i8 7)))]>;
def ROR16r1  : I<0xD1, MRM1r, (outs GR16:$dst), (ins GR16:$src1),
                 "ror{w}\t$dst",
                 [(set GR16:$dst, (rotl GR16:$src1, (i8 15)))]>, OpSize16;
def ROR32r1  : I<0xD1, MRM1r, (outs GR32:$dst), (ins GR32:$src1),
                 "ror{l}\t$dst",
                 [(set GR32:$dst, (rotl GR32:$src1, (i8 31)))]>, OpSize32;
def ROR64r1  : RI<0xD1, MRM1r, (outs GR64:$dst), (ins GR64:$src1),
                  "ror{q}\t$dst",
                  [(set GR64:$dst, (rotl GR64:$src1, (i8 63)))]>;
} // Constraints = "$src = $dst", SchedRW

let Uses = [CL], SchedRW = [WriteRotateCLLd, WriteRMW] in {
def ROR8mCL  : I<0xD2, MRM1m, (outs), (ins i8mem :$dst),
                 "ror{b}\t{%cl, $dst|$dst, cl}",
                 [(store (rotr (loadi8 addr:$dst), CL), addr:$dst)]>;
def ROR16mCL : I<0xD3, MRM1m, (outs), (ins i16mem:$dst),
                 "ror{w}\t{%cl, $dst|$dst, cl}",
                 [(store (rotr (loadi16 addr:$dst), CL), addr:$dst)]>, OpSize16;
def ROR32mCL : I<0xD3, MRM1m, (outs), (ins i32mem:$dst),
                 "ror{l}\t{%cl, $dst|$dst, cl}",
                 [(store (rotr (loadi32 addr:$dst), CL), addr:$dst)]>, OpSize32;
def ROR64mCL : RI<0xD3, MRM1m, (outs), (ins i64mem:$dst),
                  "ror{q}\t{%cl, $dst|$dst, cl}",
                  [(store (rotr (loadi64 addr:$dst), CL), addr:$dst)]>,
                  Requires<[In64BitMode]>;
}

let SchedRW = [WriteRotateLd, WriteRMW] in {
def ROR8mi   : Ii8<0xC0, MRM1m, (outs), (ins i8mem :$dst, u8imm:$src),
                   "ror{b}\t{$src, $dst|$dst, $src}",
                   [(store (rotr (loadi8 addr:$dst), (i8 imm:$src)), addr:$dst)]>;
def ROR16mi  : Ii8<0xC1, MRM1m, (outs), (ins i16mem:$dst, u8imm:$src),
                   "ror{w}\t{$src, $dst|$dst, $src}",
                   [(store (rotr (loadi16 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                   OpSize16;
def ROR32mi  : Ii8<0xC1, MRM1m, (outs), (ins i32mem:$dst, u8imm:$src),
                   "ror{l}\t{$src, $dst|$dst, $src}",
                   [(store (rotr (loadi32 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                   OpSize32;
def ROR64mi  : RIi8<0xC1, MRM1m, (outs), (ins i64mem:$dst, u8imm:$src),
                    "ror{q}\t{$src, $dst|$dst, $src}",
                    [(store (rotr (loadi64 addr:$dst), (i8 imm:$src)), addr:$dst)]>,
                    Requires<[In64BitMode]>;

// Rotate by 1
def ROR8m1   : I<0xD0, MRM1m, (outs), (ins i8mem :$dst),
                 "ror{b}\t$dst",
                 [(store (rotl (loadi8 addr:$dst), (i8 7)), addr:$dst)]>;
def ROR16m1  : I<0xD1, MRM1m, (outs), (ins i16mem:$dst),
                 "ror{w}\t$dst",
                 [(store (rotl (loadi16 addr:$dst), (i8 15)), addr:$dst)]>,
                 OpSize16;
def ROR32m1  : I<0xD1, MRM1m, (outs), (ins i32mem:$dst),
                 "ror{l}\t$dst",
                 [(store (rotl (loadi32 addr:$dst), (i8 31)), addr:$dst)]>,
                 OpSize32;
def ROR64m1  : RI<0xD1, MRM1m, (outs), (ins i64mem:$dst),
                 "ror{q}\t$dst",
                 [(store (rotl (loadi64 addr:$dst), (i8 63)), addr:$dst)]>,
                 Requires<[In64BitMode]>;
} // SchedRW


//===----------------------------------------------------------------------===//
// Double shift instructions (generalizations of rotate)
//===----------------------------------------------------------------------===//

let Constraints = "$src1 = $dst" in {

let Uses = [CL], SchedRW = [WriteSHDrrcl] in {
def SHLD16rrCL : I<0xA5, MRMDestReg, (outs GR16:$dst),
                   (ins GR16:$src1, GR16:$src2),
                   "shld{w}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(set GR16:$dst, (X86shld GR16:$src1, GR16:$src2, CL))]>,
                   TB, OpSize16;
def SHRD16rrCL : I<0xAD, MRMDestReg, (outs GR16:$dst),
                   (ins GR16:$src1, GR16:$src2),
                   "shrd{w}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(set GR16:$dst, (X86shrd GR16:$src1, GR16:$src2, CL))]>,
                   TB, OpSize16;
def SHLD32rrCL : I<0xA5, MRMDestReg, (outs GR32:$dst),
                   (ins GR32:$src1, GR32:$src2),
                   "shld{l}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(set GR32:$dst, (X86shld GR32:$src1, GR32:$src2, CL))]>,
                   TB, OpSize32;
def SHRD32rrCL : I<0xAD, MRMDestReg, (outs GR32:$dst),
                   (ins GR32:$src1, GR32:$src2),
                   "shrd{l}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(set GR32:$dst, (X86shrd GR32:$src1, GR32:$src2, CL))]>,
                   TB, OpSize32;
def SHLD64rrCL : RI<0xA5, MRMDestReg, (outs GR64:$dst),
                    (ins GR64:$src1, GR64:$src2),
                    "shld{q}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                    [(set GR64:$dst, (X86shld GR64:$src1, GR64:$src2, CL))]>,
                    TB;
def SHRD64rrCL : RI<0xAD, MRMDestReg, (outs GR64:$dst),
                    (ins GR64:$src1, GR64:$src2),
                    "shrd{q}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                    [(set GR64:$dst, (X86shrd GR64:$src1, GR64:$src2, CL))]>,
                    TB;
} // SchedRW

let isCommutable = 1, SchedRW = [WriteSHDrri] in {  // These instructions commute to each other.
def SHLD16rri8 : Ii8<0xA4, MRMDestReg,
                     (outs GR16:$dst),
                     (ins GR16:$src1, GR16:$src2, u8imm:$src3),
                     "shld{w}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                     [(set GR16:$dst, (X86shld GR16:$src1, GR16:$src2,
                                      (i8 imm:$src3)))]>,
                     TB, OpSize16;
def SHRD16rri8 : Ii8<0xAC, MRMDestReg,
                     (outs GR16:$dst),
                     (ins GR16:$src1, GR16:$src2, u8imm:$src3),
                     "shrd{w}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                     [(set GR16:$dst, (X86shrd GR16:$src1, GR16:$src2,
                                      (i8 imm:$src3)))]>,
                     TB, OpSize16;
def SHLD32rri8 : Ii8<0xA4, MRMDestReg,
                     (outs GR32:$dst),
                     (ins GR32:$src1, GR32:$src2, u8imm:$src3),
                     "shld{l}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                     [(set GR32:$dst, (X86shld GR32:$src1, GR32:$src2,
                                      (i8 imm:$src3)))]>,
                 TB, OpSize32;
def SHRD32rri8 : Ii8<0xAC, MRMDestReg,
                     (outs GR32:$dst),
                     (ins GR32:$src1, GR32:$src2, u8imm:$src3),
                     "shrd{l}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                     [(set GR32:$dst, (X86shrd GR32:$src1, GR32:$src2,
                                      (i8 imm:$src3)))]>,
                 TB, OpSize32;
def SHLD64rri8 : RIi8<0xA4, MRMDestReg,
                      (outs GR64:$dst),
                      (ins GR64:$src1, GR64:$src2, u8imm:$src3),
                      "shld{q}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                      [(set GR64:$dst, (X86shld GR64:$src1, GR64:$src2,
                                       (i8 imm:$src3)))]>,
                 TB;
def SHRD64rri8 : RIi8<0xAC, MRMDestReg,
                      (outs GR64:$dst),
                      (ins GR64:$src1, GR64:$src2, u8imm:$src3),
                      "shrd{q}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                      [(set GR64:$dst, (X86shrd GR64:$src1, GR64:$src2,
                                       (i8 imm:$src3)))]>,
                 TB;
} // SchedRW
} // Constraints = "$src = $dst"

let Uses = [CL], SchedRW = [WriteSHDmrcl] in {
def SHLD16mrCL : I<0xA5, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src2),
                   "shld{w}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(store (X86shld (loadi16 addr:$dst), GR16:$src2, CL),
                     addr:$dst)]>, TB, OpSize16;
def SHRD16mrCL : I<0xAD, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src2),
                  "shrd{w}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                  [(store (X86shrd (loadi16 addr:$dst), GR16:$src2, CL),
                    addr:$dst)]>, TB, OpSize16;

def SHLD32mrCL : I<0xA5, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src2),
                   "shld{l}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                   [(store (X86shld (loadi32 addr:$dst), GR32:$src2, CL),
                     addr:$dst)]>, TB, OpSize32;
def SHRD32mrCL : I<0xAD, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src2),
                  "shrd{l}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                  [(store (X86shrd (loadi32 addr:$dst), GR32:$src2, CL),
                    addr:$dst)]>, TB, OpSize32;

def SHLD64mrCL : RI<0xA5, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src2),
                    "shld{q}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                    [(store (X86shld (loadi64 addr:$dst), GR64:$src2, CL),
                      addr:$dst)]>, TB;
def SHRD64mrCL : RI<0xAD, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src2),
                    "shrd{q}\t{%cl, $src2, $dst|$dst, $src2, cl}",
                    [(store (X86shrd (loadi64 addr:$dst), GR64:$src2, CL),
                      addr:$dst)]>, TB;
} // SchedRW

let SchedRW = [WriteSHDmri] in {
def SHLD16mri8 : Ii8<0xA4, MRMDestMem,
                    (outs), (ins i16mem:$dst, GR16:$src2, u8imm:$src3),
                    "shld{w}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    [(store (X86shld (loadi16 addr:$dst), GR16:$src2,
                                      (i8 imm:$src3)), addr:$dst)]>,
                    TB, OpSize16;
def SHRD16mri8 : Ii8<0xAC, MRMDestMem,
                     (outs), (ins i16mem:$dst, GR16:$src2, u8imm:$src3),
                     "shrd{w}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    [(store (X86shrd (loadi16 addr:$dst), GR16:$src2,
                                      (i8 imm:$src3)), addr:$dst)]>,
                     TB, OpSize16;

def SHLD32mri8 : Ii8<0xA4, MRMDestMem,
                    (outs), (ins i32mem:$dst, GR32:$src2, u8imm:$src3),
                    "shld{l}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    [(store (X86shld (loadi32 addr:$dst), GR32:$src2,
                                      (i8 imm:$src3)), addr:$dst)]>,
                    TB, OpSize32;
def SHRD32mri8 : Ii8<0xAC, MRMDestMem,
                     (outs), (ins i32mem:$dst, GR32:$src2, u8imm:$src3),
                     "shrd{l}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                     [(store (X86shrd (loadi32 addr:$dst), GR32:$src2,
                                       (i8 imm:$src3)), addr:$dst)]>,
                     TB, OpSize32;

def SHLD64mri8 : RIi8<0xA4, MRMDestMem,
                      (outs), (ins i64mem:$dst, GR64:$src2, u8imm:$src3),
                      "shld{q}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                      [(store (X86shld (loadi64 addr:$dst), GR64:$src2,
                                       (i8 imm:$src3)), addr:$dst)]>,
                 TB;
def SHRD64mri8 : RIi8<0xAC, MRMDestMem,
                      (outs), (ins i64mem:$dst, GR64:$src2, u8imm:$src3),
                      "shrd{q}\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                      [(store (X86shrd (loadi64 addr:$dst), GR64:$src2,
                                       (i8 imm:$src3)), addr:$dst)]>,
                 TB;
} // SchedRW

} // Defs = [EFLAGS]

// Sandy Bridge and newer Intel processors support faster rotates using
// SHLD to avoid a partial flag update on the normal rotate instructions.
let Predicates = [HasFastSHLDRotate], AddedComplexity = 5 in {
  def : Pat<(rotl GR32:$src, (i8 imm:$shamt)),
            (SHLD32rri8 GR32:$src, GR32:$src, imm:$shamt)>;
  def : Pat<(rotl GR64:$src, (i8 imm:$shamt)),
            (SHLD64rri8 GR64:$src, GR64:$src, imm:$shamt)>;
}

def ROT32L2R_imm8  : SDNodeXForm<imm, [{
  // Convert a ROTL shamt to a ROTR shamt on 32-bit integer.
  return getI8Imm(32 - N->getZExtValue(), SDLoc(N));
}]>;

def ROT64L2R_imm8  : SDNodeXForm<imm, [{
  // Convert a ROTL shamt to a ROTR shamt on 64-bit integer.
  return getI8Imm(64 - N->getZExtValue(), SDLoc(N));
}]>;

// NOTE: We use WriteShift for these rotates as they avoid the stalls
// of many of the older x86 rotate instructions.
multiclass bmi_rotate<string asm, RegisterClass RC, X86MemOperand x86memop> {
let hasSideEffects = 0 in {
  def ri : Ii8<0xF0, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, u8imm:$src2),
               !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               []>, TAXD, VEX, Sched<[WriteShift]>;
  let mayLoad = 1 in
  def mi : Ii8<0xF0, MRMSrcMem, (outs RC:$dst),
               (ins x86memop:$src1, u8imm:$src2),
               !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               []>, TAXD, VEX, Sched<[WriteShiftLd]>;
}
}

multiclass bmi_shift<string asm, RegisterClass RC, X86MemOperand x86memop> {
let hasSideEffects = 0 in {
  def rr : I<0xF7, MRMSrcReg4VOp3, (outs RC:$dst), (ins RC:$src1, RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"), []>,
             VEX, Sched<[WriteShift]>;
  let mayLoad = 1 in
  def rm : I<0xF7, MRMSrcMem4VOp3,
             (outs RC:$dst), (ins x86memop:$src1, RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"), []>,
             VEX, Sched<[WriteShift.Folded,
                         // x86memop:$src1
                         ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                         ReadDefault,
                         // RC:$src2
                         WriteShift.ReadAfterFold]>;
}
}

let Predicates = [HasBMI2] in {
  defm RORX32 : bmi_rotate<"rorx{l}", GR32, i32mem>;
  defm RORX64 : bmi_rotate<"rorx{q}", GR64, i64mem>, VEX_W;
  defm SARX32 : bmi_shift<"sarx{l}", GR32, i32mem>, T8XS;
  defm SARX64 : bmi_shift<"sarx{q}", GR64, i64mem>, T8XS, VEX_W;
  defm SHRX32 : bmi_shift<"shrx{l}", GR32, i32mem>, T8XD;
  defm SHRX64 : bmi_shift<"shrx{q}", GR64, i64mem>, T8XD, VEX_W;
  defm SHLX32 : bmi_shift<"shlx{l}", GR32, i32mem>, T8PD;
  defm SHLX64 : bmi_shift<"shlx{q}", GR64, i64mem>, T8PD, VEX_W;

  // Prefer RORX which is non-destructive and doesn't update EFLAGS.
  let AddedComplexity = 10 in {
    def : Pat<(rotl GR32:$src, (i8 imm:$shamt)),
              (RORX32ri GR32:$src, (ROT32L2R_imm8 imm:$shamt))>;
    def : Pat<(rotl GR64:$src, (i8 imm:$shamt)),
              (RORX64ri GR64:$src, (ROT64L2R_imm8 imm:$shamt))>;
  }

  def : Pat<(rotl (loadi32 addr:$src), (i8 imm:$shamt)),
            (RORX32mi addr:$src, (ROT32L2R_imm8 imm:$shamt))>;
  def : Pat<(rotl (loadi64 addr:$src), (i8 imm:$shamt)),
            (RORX64mi addr:$src, (ROT64L2R_imm8 imm:$shamt))>;

  // Prefer SARX/SHRX/SHLX over SAR/SHR/SHL with variable shift BUT not
  // immedidate shift, i.e. the following code is considered better
  //
  //  mov %edi, %esi
  //  shl $imm, %esi
  //  ... %edi, ...
  //
  // than
  //
  //  movb $imm, %sil
  //  shlx %sil, %edi, %esi
  //  ... %edi, ...
  //
  let AddedComplexity = 1 in {
    def : Pat<(sra GR32:$src1, GR8:$src2),
              (SARX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(sra GR64:$src1, GR8:$src2),
              (SARX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

    def : Pat<(srl GR32:$src1, GR8:$src2),
              (SHRX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(srl GR64:$src1, GR8:$src2),
              (SHRX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

    def : Pat<(shl GR32:$src1, GR8:$src2),
              (SHLX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(shl GR64:$src1, GR8:$src2),
              (SHLX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  }

  // We prefer to use
  //  mov (%ecx), %esi
  //  shl $imm, $esi
  //
  // over
  //
  //  movb $imm, %al
  //  shlx %al, (%ecx), %esi
  //
  // This priority is enforced by IsProfitableToFoldLoad.
  def : Pat<(sra (loadi32 addr:$src1), GR8:$src2),
            (SARX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(sra (loadi64 addr:$src1), GR8:$src2),
            (SARX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  def : Pat<(srl (loadi32 addr:$src1), GR8:$src2),
            (SHRX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(srl (loadi64 addr:$src1), GR8:$src2),
            (SHRX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  def : Pat<(shl (loadi32 addr:$src1), GR8:$src2),
            (SHLX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(shl (loadi64 addr:$src1), GR8:$src2),
            (SHLX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
}
