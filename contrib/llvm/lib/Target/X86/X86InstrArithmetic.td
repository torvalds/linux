//===-- X86InstrArithmetic.td - Integer Arithmetic Instrs --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the integer arithmetic instructions in the X86
// architecture.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// LEA - Load Effective Address
let SchedRW = [WriteLEA] in {
let hasSideEffects = 0 in
def LEA16r   : I<0x8D, MRMSrcMem,
                 (outs GR16:$dst), (ins anymem:$src),
                 "lea{w}\t{$src|$dst}, {$dst|$src}", []>, OpSize16;
let isReMaterializable = 1 in
def LEA32r   : I<0x8D, MRMSrcMem,
                 (outs GR32:$dst), (ins anymem:$src),
                 "lea{l}\t{$src|$dst}, {$dst|$src}",
                 [(set GR32:$dst, lea32addr:$src)]>,
                 OpSize32, Requires<[Not64BitMode]>;

def LEA64_32r : I<0x8D, MRMSrcMem,
                  (outs GR32:$dst), (ins lea64_32mem:$src),
                  "lea{l}\t{$src|$dst}, {$dst|$src}",
                  [(set GR32:$dst, lea64_32addr:$src)]>,
                  OpSize32, Requires<[In64BitMode]>;

let isReMaterializable = 1 in
def LEA64r   : RI<0x8D, MRMSrcMem, (outs GR64:$dst), (ins lea64mem:$src),
                  "lea{q}\t{$src|$dst}, {$dst|$src}",
                  [(set GR64:$dst, lea64addr:$src)]>;
} // SchedRW

//===----------------------------------------------------------------------===//
//  Fixed-Register Multiplication and Division Instructions.
//

// SchedModel info for instruction that loads one value and gets the second
// (and possibly third) value from a register.
// This is used for instructions that put the memory operands before other
// uses.
class SchedLoadReg<X86FoldableSchedWrite Sched> : Sched<[Sched.Folded,
  // Memory operand.
  ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
  // Register reads (implicit or explicit).
  Sched.ReadAfterFold, Sched.ReadAfterFold]>;

// Extra precision multiplication

// AL is really implied by AX, but the registers in Defs must match the
// SDNode results (i8, i32).
// AL,AH = AL*GR8
let Defs = [AL,EFLAGS,AX], Uses = [AL] in
def MUL8r  : I<0xF6, MRM4r, (outs),  (ins GR8:$src), "mul{b}\t$src",
               // FIXME: Used for 8-bit mul, ignore result upper 8 bits.
               // This probably ought to be moved to a def : Pat<> if the
               // syntax can be accepted.
               [(set AL, (mul AL, GR8:$src)),
                (implicit EFLAGS)]>, Sched<[WriteIMul8]>;
// AX,DX = AX*GR16
let Defs = [AX,DX,EFLAGS], Uses = [AX], hasSideEffects = 0 in
def MUL16r : I<0xF7, MRM4r, (outs),  (ins GR16:$src),
               "mul{w}\t$src",
               []>, OpSize16, Sched<[WriteIMul16]>;
// EAX,EDX = EAX*GR32
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX], hasSideEffects = 0 in
def MUL32r : I<0xF7, MRM4r, (outs),  (ins GR32:$src),
               "mul{l}\t$src",
               [/*(set EAX, EDX, EFLAGS, (X86umul_flag EAX, GR32:$src))*/]>,
               OpSize32, Sched<[WriteIMul32]>;
// RAX,RDX = RAX*GR64
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX], hasSideEffects = 0 in
def MUL64r : RI<0xF7, MRM4r, (outs), (ins GR64:$src),
                "mul{q}\t$src",
                [/*(set RAX, RDX, EFLAGS, (X86umul_flag RAX, GR64:$src))*/]>,
                Sched<[WriteIMul64]>;
// AL,AH = AL*[mem8]
let Defs = [AL,EFLAGS,AX], Uses = [AL] in
def MUL8m  : I<0xF6, MRM4m, (outs), (ins i8mem :$src),
               "mul{b}\t$src",
               // FIXME: Used for 8-bit mul, ignore result upper 8 bits.
               // This probably ought to be moved to a def : Pat<> if the
               // syntax can be accepted.
               [(set AL, (mul AL, (loadi8 addr:$src))),
                (implicit EFLAGS)]>, SchedLoadReg<WriteIMul8>;
// AX,DX = AX*[mem16]
let mayLoad = 1, hasSideEffects = 0 in {
let Defs = [AX,DX,EFLAGS], Uses = [AX] in
def MUL16m : I<0xF7, MRM4m, (outs), (ins i16mem:$src),
               "mul{w}\t$src", []>, OpSize16, SchedLoadReg<WriteIMul16>;
// EAX,EDX = EAX*[mem32]
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX] in
def MUL32m : I<0xF7, MRM4m, (outs), (ins i32mem:$src),
              "mul{l}\t$src", []>, OpSize32, SchedLoadReg<WriteIMul32>;
// RAX,RDX = RAX*[mem64]
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX] in
def MUL64m : RI<0xF7, MRM4m, (outs), (ins i64mem:$src),
                "mul{q}\t$src", []>, SchedLoadReg<WriteIMul64>,
                Requires<[In64BitMode]>;
}

let hasSideEffects = 0 in {
// AL,AH = AL*GR8
let Defs = [AL,EFLAGS,AX], Uses = [AL] in
def IMUL8r  : I<0xF6, MRM5r, (outs),  (ins GR8:$src), "imul{b}\t$src", []>,
                Sched<[WriteIMul8]>;
// AX,DX = AX*GR16
let Defs = [AX,DX,EFLAGS], Uses = [AX] in
def IMUL16r : I<0xF7, MRM5r, (outs),  (ins GR16:$src), "imul{w}\t$src", []>,
                OpSize16, Sched<[WriteIMul16]>;
// EAX,EDX = EAX*GR32
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX] in
def IMUL32r : I<0xF7, MRM5r, (outs),  (ins GR32:$src), "imul{l}\t$src", []>,
                OpSize32, Sched<[WriteIMul32]>;
// RAX,RDX = RAX*GR64
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX] in
def IMUL64r : RI<0xF7, MRM5r, (outs), (ins GR64:$src), "imul{q}\t$src", []>,
                 Sched<[WriteIMul64]>;

let mayLoad = 1 in {
// AL,AH = AL*[mem8]
let Defs = [AL,EFLAGS,AX], Uses = [AL] in
def IMUL8m  : I<0xF6, MRM5m, (outs), (ins i8mem :$src),
                "imul{b}\t$src", []>, SchedLoadReg<WriteIMul8>;
// AX,DX = AX*[mem16]
let Defs = [AX,DX,EFLAGS], Uses = [AX] in
def IMUL16m : I<0xF7, MRM5m, (outs), (ins i16mem:$src),
                "imul{w}\t$src", []>, OpSize16, SchedLoadReg<WriteIMul16>;
// EAX,EDX = EAX*[mem32]
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX] in
def IMUL32m : I<0xF7, MRM5m, (outs), (ins i32mem:$src),
                "imul{l}\t$src", []>, OpSize32, SchedLoadReg<WriteIMul32>;
// RAX,RDX = RAX*[mem64]
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX] in
def IMUL64m : RI<0xF7, MRM5m, (outs), (ins i64mem:$src),
                 "imul{q}\t$src", []>, SchedLoadReg<WriteIMul64>,
                 Requires<[In64BitMode]>;
}
} // hasSideEffects


let Defs = [EFLAGS] in {
let Constraints = "$src1 = $dst" in {

let isCommutable = 1 in {
// X = IMUL Y, Z --> X = IMUL Z, Y
// Register-Register Signed Integer Multiply
def IMUL16rr : I<0xAF, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src1,GR16:$src2),
                 "imul{w}\t{$src2, $dst|$dst, $src2}",
                 [(set GR16:$dst, EFLAGS,
                       (X86smul_flag GR16:$src1, GR16:$src2))]>,
                 Sched<[WriteIMul16Reg]>, TB, OpSize16;
def IMUL32rr : I<0xAF, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src1,GR32:$src2),
                 "imul{l}\t{$src2, $dst|$dst, $src2}",
                 [(set GR32:$dst, EFLAGS,
                       (X86smul_flag GR32:$src1, GR32:$src2))]>,
                 Sched<[WriteIMul32Reg]>, TB, OpSize32;
def IMUL64rr : RI<0xAF, MRMSrcReg, (outs GR64:$dst),
                                   (ins GR64:$src1, GR64:$src2),
                  "imul{q}\t{$src2, $dst|$dst, $src2}",
                  [(set GR64:$dst, EFLAGS,
                        (X86smul_flag GR64:$src1, GR64:$src2))]>,
                  Sched<[WriteIMul64Reg]>, TB;
} // isCommutable

// Register-Memory Signed Integer Multiply
def IMUL16rm : I<0xAF, MRMSrcMem, (outs GR16:$dst),
                                  (ins GR16:$src1, i16mem:$src2),
                 "imul{w}\t{$src2, $dst|$dst, $src2}",
                 [(set GR16:$dst, EFLAGS,
                       (X86smul_flag GR16:$src1, (loadi16 addr:$src2)))]>,
                 Sched<[WriteIMul16Reg.Folded, WriteIMul16Reg.ReadAfterFold]>, TB, OpSize16;
def IMUL32rm : I<0xAF, MRMSrcMem, (outs GR32:$dst),
                 (ins GR32:$src1, i32mem:$src2),
                 "imul{l}\t{$src2, $dst|$dst, $src2}",
                 [(set GR32:$dst, EFLAGS,
                       (X86smul_flag GR32:$src1, (loadi32 addr:$src2)))]>,
                 Sched<[WriteIMul32Reg.Folded, WriteIMul32Reg.ReadAfterFold]>, TB, OpSize32;
def IMUL64rm : RI<0xAF, MRMSrcMem, (outs GR64:$dst),
                                   (ins GR64:$src1, i64mem:$src2),
                  "imul{q}\t{$src2, $dst|$dst, $src2}",
                  [(set GR64:$dst, EFLAGS,
                        (X86smul_flag GR64:$src1, (loadi64 addr:$src2)))]>,
                  Sched<[WriteIMul64Reg.Folded, WriteIMul32Reg.ReadAfterFold]>, TB;
} // Constraints = "$src1 = $dst"

} // Defs = [EFLAGS]

// Surprisingly enough, these are not two address instructions!
let Defs = [EFLAGS] in {
// Register-Integer Signed Integer Multiply
def IMUL16rri  : Ii16<0x69, MRMSrcReg,                      // GR16 = GR16*I16
                      (outs GR16:$dst), (ins GR16:$src1, i16imm:$src2),
                      "imul{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR16:$dst, EFLAGS,
                            (X86smul_flag GR16:$src1, imm:$src2))]>,
                      Sched<[WriteIMul16Imm]>, OpSize16;
def IMUL16rri8 : Ii8<0x6B, MRMSrcReg,                       // GR16 = GR16*I8
                     (outs GR16:$dst), (ins GR16:$src1, i16i8imm:$src2),
                     "imul{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     [(set GR16:$dst, EFLAGS,
                           (X86smul_flag GR16:$src1, i16immSExt8:$src2))]>,
                     Sched<[WriteIMul16Imm]>, OpSize16;
def IMUL32rri  : Ii32<0x69, MRMSrcReg,                      // GR32 = GR32*I32
                      (outs GR32:$dst), (ins GR32:$src1, i32imm:$src2),
                      "imul{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR32:$dst, EFLAGS,
                            (X86smul_flag GR32:$src1, imm:$src2))]>,
                      Sched<[WriteIMul32Imm]>, OpSize32;
def IMUL32rri8 : Ii8<0x6B, MRMSrcReg,                       // GR32 = GR32*I8
                     (outs GR32:$dst), (ins GR32:$src1, i32i8imm:$src2),
                     "imul{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     [(set GR32:$dst, EFLAGS,
                           (X86smul_flag GR32:$src1, i32immSExt8:$src2))]>,
                     Sched<[WriteIMul32Imm]>, OpSize32;
def IMUL64rri32 : RIi32S<0x69, MRMSrcReg,                    // GR64 = GR64*I32
                         (outs GR64:$dst), (ins GR64:$src1, i64i32imm:$src2),
                         "imul{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set GR64:$dst, EFLAGS,
                             (X86smul_flag GR64:$src1, i64immSExt32:$src2))]>,
                         Sched<[WriteIMul64Imm]>;
def IMUL64rri8 : RIi8<0x6B, MRMSrcReg,                      // GR64 = GR64*I8
                      (outs GR64:$dst), (ins GR64:$src1, i64i8imm:$src2),
                      "imul{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR64:$dst, EFLAGS,
                            (X86smul_flag GR64:$src1, i64immSExt8:$src2))]>,
                      Sched<[WriteIMul64Imm]>;

// Memory-Integer Signed Integer Multiply
def IMUL16rmi  : Ii16<0x69, MRMSrcMem,                     // GR16 = [mem16]*I16
                      (outs GR16:$dst), (ins i16mem:$src1, i16imm:$src2),
                      "imul{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR16:$dst, EFLAGS,
                            (X86smul_flag (loadi16 addr:$src1), imm:$src2))]>,
                      Sched<[WriteIMul16Imm.Folded]>, OpSize16;
def IMUL16rmi8 : Ii8<0x6B, MRMSrcMem,                       // GR16 = [mem16]*I8
                     (outs GR16:$dst), (ins i16mem:$src1, i16i8imm :$src2),
                     "imul{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     [(set GR16:$dst, EFLAGS,
                           (X86smul_flag (loadi16 addr:$src1),
                                         i16immSExt8:$src2))]>,
                     Sched<[WriteIMul16Imm.Folded]>, OpSize16;
def IMUL32rmi  : Ii32<0x69, MRMSrcMem,                     // GR32 = [mem32]*I32
                      (outs GR32:$dst), (ins i32mem:$src1, i32imm:$src2),
                      "imul{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR32:$dst, EFLAGS,
                            (X86smul_flag (loadi32 addr:$src1), imm:$src2))]>,
                      Sched<[WriteIMul32Imm.Folded]>, OpSize32;
def IMUL32rmi8 : Ii8<0x6B, MRMSrcMem,                       // GR32 = [mem32]*I8
                     (outs GR32:$dst), (ins i32mem:$src1, i32i8imm: $src2),
                     "imul{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     [(set GR32:$dst, EFLAGS,
                           (X86smul_flag (loadi32 addr:$src1),
                                         i32immSExt8:$src2))]>,
                     Sched<[WriteIMul32Imm.Folded]>, OpSize32;
def IMUL64rmi32 : RIi32S<0x69, MRMSrcMem,                   // GR64 = [mem64]*I32
                         (outs GR64:$dst), (ins i64mem:$src1, i64i32imm:$src2),
                         "imul{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set GR64:$dst, EFLAGS,
                              (X86smul_flag (loadi64 addr:$src1),
                                            i64immSExt32:$src2))]>,
                         Sched<[WriteIMul64Imm.Folded]>;
def IMUL64rmi8 : RIi8<0x6B, MRMSrcMem,                      // GR64 = [mem64]*I8
                      (outs GR64:$dst), (ins i64mem:$src1, i64i8imm: $src2),
                      "imul{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set GR64:$dst, EFLAGS,
                            (X86smul_flag (loadi64 addr:$src1),
                                          i64immSExt8:$src2))]>,
                      Sched<[WriteIMul64Imm.Folded]>;
} // Defs = [EFLAGS]

// unsigned division/remainder
let hasSideEffects = 1 in { // so that we don't speculatively execute
let Defs = [AL,AH,EFLAGS], Uses = [AX] in
def DIV8r  : I<0xF6, MRM6r, (outs),  (ins GR8:$src),    // AX/r8 = AL,AH
               "div{b}\t$src", []>, Sched<[WriteDiv8]>;
let Defs = [AX,DX,EFLAGS], Uses = [AX,DX] in
def DIV16r : I<0xF7, MRM6r, (outs),  (ins GR16:$src),   // DX:AX/r16 = AX,DX
               "div{w}\t$src", []>, Sched<[WriteDiv16]>, OpSize16;
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX,EDX] in
def DIV32r : I<0xF7, MRM6r, (outs),  (ins GR32:$src),   // EDX:EAX/r32 = EAX,EDX
               "div{l}\t$src", []>, Sched<[WriteDiv32]>, OpSize32;
// RDX:RAX/r64 = RAX,RDX
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX,RDX] in
def DIV64r : RI<0xF7, MRM6r, (outs), (ins GR64:$src),
                "div{q}\t$src", []>, Sched<[WriteDiv64]>;

let mayLoad = 1 in {
let Defs = [AL,AH,EFLAGS], Uses = [AX] in
def DIV8m  : I<0xF6, MRM6m, (outs), (ins i8mem:$src),   // AX/[mem8] = AL,AH
               "div{b}\t$src", []>, SchedLoadReg<WriteDiv8>;
let Defs = [AX,DX,EFLAGS], Uses = [AX,DX] in
def DIV16m : I<0xF7, MRM6m, (outs), (ins i16mem:$src),  // DX:AX/[mem16] = AX,DX
               "div{w}\t$src", []>, OpSize16, SchedLoadReg<WriteDiv16>;
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX,EDX] in    // EDX:EAX/[mem32] = EAX,EDX
def DIV32m : I<0xF7, MRM6m, (outs), (ins i32mem:$src),
               "div{l}\t$src", []>, SchedLoadReg<WriteDiv32>, OpSize32;
// RDX:RAX/[mem64] = RAX,RDX
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX,RDX] in
def DIV64m : RI<0xF7, MRM6m, (outs), (ins i64mem:$src),
                "div{q}\t$src", []>, SchedLoadReg<WriteDiv64>,
                Requires<[In64BitMode]>;
}

// Signed division/remainder.
let Defs = [AL,AH,EFLAGS], Uses = [AX] in
def IDIV8r : I<0xF6, MRM7r, (outs),  (ins GR8:$src),    // AX/r8 = AL,AH
               "idiv{b}\t$src", []>, Sched<[WriteIDiv8]>;
let Defs = [AX,DX,EFLAGS], Uses = [AX,DX] in
def IDIV16r: I<0xF7, MRM7r, (outs),  (ins GR16:$src),   // DX:AX/r16 = AX,DX
               "idiv{w}\t$src", []>, Sched<[WriteIDiv16]>, OpSize16;
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX,EDX] in
def IDIV32r: I<0xF7, MRM7r, (outs),  (ins GR32:$src),   // EDX:EAX/r32 = EAX,EDX
               "idiv{l}\t$src", []>, Sched<[WriteIDiv32]>, OpSize32;
// RDX:RAX/r64 = RAX,RDX
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX,RDX] in
def IDIV64r: RI<0xF7, MRM7r, (outs), (ins GR64:$src),
                "idiv{q}\t$src", []>, Sched<[WriteIDiv64]>;

let mayLoad = 1 in {
let Defs = [AL,AH,EFLAGS], Uses = [AX] in
def IDIV8m : I<0xF6, MRM7m, (outs), (ins i8mem:$src),   // AX/[mem8] = AL,AH
               "idiv{b}\t$src", []>, SchedLoadReg<WriteIDiv8>;
let Defs = [AX,DX,EFLAGS], Uses = [AX,DX] in
def IDIV16m: I<0xF7, MRM7m, (outs), (ins i16mem:$src),  // DX:AX/[mem16] = AX,DX
               "idiv{w}\t$src", []>, OpSize16, SchedLoadReg<WriteIDiv16>;
let Defs = [EAX,EDX,EFLAGS], Uses = [EAX,EDX] in    // EDX:EAX/[mem32] = EAX,EDX
def IDIV32m: I<0xF7, MRM7m, (outs), (ins i32mem:$src),
               "idiv{l}\t$src", []>, OpSize32, SchedLoadReg<WriteIDiv32>;
let Defs = [RAX,RDX,EFLAGS], Uses = [RAX,RDX] in // RDX:RAX/[mem64] = RAX,RDX
def IDIV64m: RI<0xF7, MRM7m, (outs), (ins i64mem:$src),
                "idiv{q}\t$src", []>, SchedLoadReg<WriteIDiv64>,
                Requires<[In64BitMode]>;
}
} // hasSideEffects = 0

//===----------------------------------------------------------------------===//
//  Two address Instructions.
//

// unary instructions
let CodeSize = 2 in {
let Defs = [EFLAGS] in {
let Constraints = "$src1 = $dst", SchedRW = [WriteALU] in {
def NEG8r  : I<0xF6, MRM3r, (outs GR8 :$dst), (ins GR8 :$src1),
               "neg{b}\t$dst",
               [(set GR8:$dst, (ineg GR8:$src1)),
                (implicit EFLAGS)]>;
def NEG16r : I<0xF7, MRM3r, (outs GR16:$dst), (ins GR16:$src1),
               "neg{w}\t$dst",
               [(set GR16:$dst, (ineg GR16:$src1)),
                (implicit EFLAGS)]>, OpSize16;
def NEG32r : I<0xF7, MRM3r, (outs GR32:$dst), (ins GR32:$src1),
               "neg{l}\t$dst",
               [(set GR32:$dst, (ineg GR32:$src1)),
                (implicit EFLAGS)]>, OpSize32;
def NEG64r : RI<0xF7, MRM3r, (outs GR64:$dst), (ins GR64:$src1), "neg{q}\t$dst",
                [(set GR64:$dst, (ineg GR64:$src1)),
                 (implicit EFLAGS)]>;
} // Constraints = "$src1 = $dst", SchedRW

// Read-modify-write negate.
let SchedRW = [WriteALURMW] in {
def NEG8m  : I<0xF6, MRM3m, (outs), (ins i8mem :$dst),
               "neg{b}\t$dst",
               [(store (ineg (loadi8 addr:$dst)), addr:$dst),
                (implicit EFLAGS)]>;
def NEG16m : I<0xF7, MRM3m, (outs), (ins i16mem:$dst),
               "neg{w}\t$dst",
               [(store (ineg (loadi16 addr:$dst)), addr:$dst),
                (implicit EFLAGS)]>, OpSize16;
def NEG32m : I<0xF7, MRM3m, (outs), (ins i32mem:$dst),
               "neg{l}\t$dst",
               [(store (ineg (loadi32 addr:$dst)), addr:$dst),
                (implicit EFLAGS)]>, OpSize32;
def NEG64m : RI<0xF7, MRM3m, (outs), (ins i64mem:$dst), "neg{q}\t$dst",
                [(store (ineg (loadi64 addr:$dst)), addr:$dst),
                 (implicit EFLAGS)]>,
                Requires<[In64BitMode]>;
} // SchedRW
} // Defs = [EFLAGS]


// Note: NOT does not set EFLAGS!

let Constraints = "$src1 = $dst", SchedRW = [WriteALU] in {
def NOT8r  : I<0xF6, MRM2r, (outs GR8 :$dst), (ins GR8 :$src1),
               "not{b}\t$dst",
               [(set GR8:$dst, (not GR8:$src1))]>;
def NOT16r : I<0xF7, MRM2r, (outs GR16:$dst), (ins GR16:$src1),
               "not{w}\t$dst",
               [(set GR16:$dst, (not GR16:$src1))]>, OpSize16;
def NOT32r : I<0xF7, MRM2r, (outs GR32:$dst), (ins GR32:$src1),
               "not{l}\t$dst",
               [(set GR32:$dst, (not GR32:$src1))]>, OpSize32;
def NOT64r : RI<0xF7, MRM2r, (outs GR64:$dst), (ins GR64:$src1), "not{q}\t$dst",
                [(set GR64:$dst, (not GR64:$src1))]>;
} // Constraints = "$src1 = $dst", SchedRW

let SchedRW = [WriteALURMW] in {
def NOT8m  : I<0xF6, MRM2m, (outs), (ins i8mem :$dst),
               "not{b}\t$dst",
               [(store (not (loadi8 addr:$dst)), addr:$dst)]>;
def NOT16m : I<0xF7, MRM2m, (outs), (ins i16mem:$dst),
               "not{w}\t$dst",
               [(store (not (loadi16 addr:$dst)), addr:$dst)]>,
               OpSize16;
def NOT32m : I<0xF7, MRM2m, (outs), (ins i32mem:$dst),
               "not{l}\t$dst",
               [(store (not (loadi32 addr:$dst)), addr:$dst)]>,
               OpSize32;
def NOT64m : RI<0xF7, MRM2m, (outs), (ins i64mem:$dst), "not{q}\t$dst",
                [(store (not (loadi64 addr:$dst)), addr:$dst)]>,
                Requires<[In64BitMode]>;
} // SchedRW
} // CodeSize

def X86add_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86add_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def X86sub_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86sub_flag node:$lhs, node:$rhs), [{
  // Only use DEC if the result is used.
  return !SDValue(N, 0).use_empty() && hasNoCarryFlagUses(SDValue(N, 1));
}]>;

// TODO: inc/dec is slow for P4, but fast for Pentium-M.
let Defs = [EFLAGS] in {
let Constraints = "$src1 = $dst", SchedRW = [WriteALU] in {
let CodeSize = 2 in
def INC8r  : I<0xFE, MRM0r, (outs GR8 :$dst), (ins GR8 :$src1),
               "inc{b}\t$dst",
               [(set GR8:$dst, EFLAGS, (X86add_flag_nocf GR8:$src1, 1))]>;
let isConvertibleToThreeAddress = 1, CodeSize = 2 in { // Can xform into LEA.
def INC16r : I<0xFF, MRM0r, (outs GR16:$dst), (ins GR16:$src1),
               "inc{w}\t$dst",
               [(set GR16:$dst, EFLAGS, (X86add_flag_nocf GR16:$src1, 1))]>,
               OpSize16;
def INC32r : I<0xFF, MRM0r, (outs GR32:$dst), (ins GR32:$src1),
               "inc{l}\t$dst",
               [(set GR32:$dst, EFLAGS, (X86add_flag_nocf GR32:$src1, 1))]>,
               OpSize32;
def INC64r : RI<0xFF, MRM0r, (outs GR64:$dst), (ins GR64:$src1), "inc{q}\t$dst",
                [(set GR64:$dst, EFLAGS, (X86add_flag_nocf GR64:$src1, 1))]>;
} // isConvertibleToThreeAddress = 1, CodeSize = 2

// Short forms only valid in 32-bit mode. Selected during MCInst lowering.
let CodeSize = 1, hasSideEffects = 0 in {
def INC16r_alt : I<0x40, AddRegFrm, (outs GR16:$dst), (ins GR16:$src1),
                   "inc{w}\t$dst", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def INC32r_alt : I<0x40, AddRegFrm, (outs GR32:$dst), (ins GR32:$src1),
                   "inc{l}\t$dst", []>,
                 OpSize32, Requires<[Not64BitMode]>;
} // CodeSize = 1, hasSideEffects = 0
} // Constraints = "$src1 = $dst", SchedRW

let CodeSize = 2, SchedRW = [WriteALURMW] in {
let Predicates = [UseIncDec] in {
  def INC8m  : I<0xFE, MRM0m, (outs), (ins i8mem :$dst), "inc{b}\t$dst",
               [(store (add (loadi8 addr:$dst), 1), addr:$dst),
                (implicit EFLAGS)]>;
  def INC16m : I<0xFF, MRM0m, (outs), (ins i16mem:$dst), "inc{w}\t$dst",
               [(store (add (loadi16 addr:$dst), 1), addr:$dst),
                (implicit EFLAGS)]>, OpSize16;
  def INC32m : I<0xFF, MRM0m, (outs), (ins i32mem:$dst), "inc{l}\t$dst",
               [(store (add (loadi32 addr:$dst), 1), addr:$dst),
                (implicit EFLAGS)]>, OpSize32;
} // Predicates
let Predicates = [UseIncDec, In64BitMode] in {
  def INC64m : RI<0xFF, MRM0m, (outs), (ins i64mem:$dst), "inc{q}\t$dst",
                  [(store (add (loadi64 addr:$dst), 1), addr:$dst),
                   (implicit EFLAGS)]>;
} // Predicates
} // CodeSize = 2, SchedRW

let Constraints = "$src1 = $dst", SchedRW = [WriteALU] in {
let CodeSize = 2 in
def DEC8r  : I<0xFE, MRM1r, (outs GR8 :$dst), (ins GR8 :$src1),
               "dec{b}\t$dst",
               [(set GR8:$dst, EFLAGS, (X86sub_flag_nocf GR8:$src1, 1))]>;
let isConvertibleToThreeAddress = 1, CodeSize = 2 in { // Can xform into LEA.
def DEC16r : I<0xFF, MRM1r, (outs GR16:$dst), (ins GR16:$src1),
               "dec{w}\t$dst",
               [(set GR16:$dst, EFLAGS, (X86sub_flag_nocf GR16:$src1, 1))]>,
               OpSize16;
def DEC32r : I<0xFF, MRM1r, (outs GR32:$dst), (ins GR32:$src1),
               "dec{l}\t$dst",
               [(set GR32:$dst, EFLAGS, (X86sub_flag_nocf GR32:$src1, 1))]>,
               OpSize32;
def DEC64r : RI<0xFF, MRM1r, (outs GR64:$dst), (ins GR64:$src1), "dec{q}\t$dst",
                [(set GR64:$dst, EFLAGS, (X86sub_flag_nocf GR64:$src1, 1))]>;
} // isConvertibleToThreeAddress = 1, CodeSize = 2

// Short forms only valid in 32-bit mode. Selected during MCInst lowering.
let CodeSize = 1, hasSideEffects = 0 in {
def DEC16r_alt : I<0x48, AddRegFrm, (outs GR16:$dst), (ins GR16:$src1),
                   "dec{w}\t$dst", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def DEC32r_alt : I<0x48, AddRegFrm, (outs GR32:$dst), (ins GR32:$src1),
                   "dec{l}\t$dst", []>,
                 OpSize32, Requires<[Not64BitMode]>;
} // CodeSize = 1, hasSideEffects = 0
} // Constraints = "$src1 = $dst", SchedRW


let CodeSize = 2, SchedRW = [WriteALURMW] in {
let Predicates = [UseIncDec] in {
  def DEC8m  : I<0xFE, MRM1m, (outs), (ins i8mem :$dst), "dec{b}\t$dst",
               [(store (add (loadi8 addr:$dst), -1), addr:$dst),
                (implicit EFLAGS)]>;
  def DEC16m : I<0xFF, MRM1m, (outs), (ins i16mem:$dst), "dec{w}\t$dst",
               [(store (add (loadi16 addr:$dst), -1), addr:$dst),
                (implicit EFLAGS)]>, OpSize16;
  def DEC32m : I<0xFF, MRM1m, (outs), (ins i32mem:$dst), "dec{l}\t$dst",
               [(store (add (loadi32 addr:$dst), -1), addr:$dst),
                (implicit EFLAGS)]>, OpSize32;
} // Predicates
let Predicates = [UseIncDec, In64BitMode] in {
  def DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst), "dec{q}\t$dst",
                  [(store (add (loadi64 addr:$dst), -1), addr:$dst),
                   (implicit EFLAGS)]>;
} // Predicates
} // CodeSize = 2, SchedRW
} // Defs = [EFLAGS]

/// X86TypeInfo - This is a bunch of information that describes relevant X86
/// information about value types.  For example, it can tell you what the
/// register class and preferred load to use.
class X86TypeInfo<ValueType vt, string instrsuffix, RegisterClass regclass,
                  PatFrag loadnode, X86MemOperand memoperand, ImmType immkind,
                  Operand immoperand, SDPatternOperator immoperator,
                  Operand imm8operand, SDPatternOperator imm8operator,
                  bit hasOddOpcode, OperandSize opSize,
                  bit hasREX_WPrefix> {
  /// VT - This is the value type itself.
  ValueType VT = vt;

  /// InstrSuffix - This is the suffix used on instructions with this type.  For
  /// example, i8 -> "b", i16 -> "w", i32 -> "l", i64 -> "q".
  string InstrSuffix = instrsuffix;

  /// RegClass - This is the register class associated with this type.  For
  /// example, i8 -> GR8, i16 -> GR16, i32 -> GR32, i64 -> GR64.
  RegisterClass RegClass = regclass;

  /// LoadNode - This is the load node associated with this type.  For
  /// example, i8 -> loadi8, i16 -> loadi16, i32 -> loadi32, i64 -> loadi64.
  PatFrag LoadNode = loadnode;

  /// MemOperand - This is the memory operand associated with this type.  For
  /// example, i8 -> i8mem, i16 -> i16mem, i32 -> i32mem, i64 -> i64mem.
  X86MemOperand MemOperand = memoperand;

  /// ImmEncoding - This is the encoding of an immediate of this type.  For
  /// example, i8 -> Imm8, i16 -> Imm16, i32 -> Imm32.  Note that i64 -> Imm32
  /// since the immediate fields of i64 instructions is a 32-bit sign extended
  /// value.
  ImmType ImmEncoding = immkind;

  /// ImmOperand - This is the operand kind of an immediate of this type.  For
  /// example, i8 -> i8imm, i16 -> i16imm, i32 -> i32imm.  Note that i64 ->
  /// i64i32imm since the immediate fields of i64 instructions is a 32-bit sign
  /// extended value.
  Operand ImmOperand = immoperand;

  /// ImmOperator - This is the operator that should be used to match an
  /// immediate of this kind in a pattern (e.g. imm, or i64immSExt32).
  SDPatternOperator ImmOperator = immoperator;

  /// Imm8Operand - This is the operand kind to use for an imm8 of this type.
  /// For example, i8 -> <invalid>, i16 -> i16i8imm, i32 -> i32i8imm.  This is
  /// only used for instructions that have a sign-extended imm8 field form.
  Operand Imm8Operand = imm8operand;

  /// Imm8Operator - This is the operator that should be used to match an 8-bit
  /// sign extended immediate of this kind in a pattern (e.g. imm16immSExt8).
  SDPatternOperator Imm8Operator = imm8operator;

  /// HasOddOpcode - This bit is true if the instruction should have an odd (as
  /// opposed to even) opcode.  Operations on i8 are usually even, operations on
  /// other datatypes are odd.
  bit HasOddOpcode = hasOddOpcode;

  /// OpSize - Selects whether the instruction needs a 0x66 prefix based on
  /// 16-bit vs 32-bit mode. i8/i64 set this to OpSizeFixed. i16 sets this
  /// to Opsize16. i32 sets this to OpSize32.
  OperandSize OpSize = opSize;

  /// HasREX_WPrefix - This bit is set to true if the instruction should have
  /// the 0x40 REX prefix.  This is set for i64 types.
  bit HasREX_WPrefix = hasREX_WPrefix;
}

def invalid_node : SDNode<"<<invalid_node>>", SDTIntLeaf,[],"<<invalid_node>>">;


def Xi8  : X86TypeInfo<i8, "b", GR8, loadi8, i8mem,
                       Imm8, i8imm, imm8_su, i8imm, invalid_node,
                       0, OpSizeFixed, 0>;
def Xi16 : X86TypeInfo<i16, "w", GR16, loadi16, i16mem,
                       Imm16, i16imm, imm16_su, i16i8imm, i16immSExt8_su,
                       1, OpSize16, 0>;
def Xi32 : X86TypeInfo<i32, "l", GR32, loadi32, i32mem,
                       Imm32, i32imm, imm32_su, i32i8imm, i32immSExt8_su,
                       1, OpSize32, 0>;
def Xi64 : X86TypeInfo<i64, "q", GR64, loadi64, i64mem,
                       Imm32S, i64i32imm, i64immSExt32_su, i64i8imm, i64immSExt8_su,
                       1, OpSizeFixed, 1>;

/// ITy - This instruction base class takes the type info for the instruction.
/// Using this, it:
/// 1. Concatenates together the instruction mnemonic with the appropriate
///    suffix letter, a tab, and the arguments.
/// 2. Infers whether the instruction should have a 0x66 prefix byte.
/// 3. Infers whether the instruction should have a 0x40 REX_W prefix.
/// 4. Infers whether the low bit of the opcode should be 0 (for i8 operations)
///    or 1 (for i16,i32,i64 operations).
class ITy<bits<8> opcode, Format f, X86TypeInfo typeinfo, dag outs, dag ins,
          string mnemonic, string args, list<dag> pattern>
  : I<{opcode{7}, opcode{6}, opcode{5}, opcode{4},
       opcode{3}, opcode{2}, opcode{1}, typeinfo.HasOddOpcode },
      f, outs, ins,
      !strconcat(mnemonic, "{", typeinfo.InstrSuffix, "}\t", args), pattern> {

  // Infer instruction prefixes from type info.
  let OpSize = typeinfo.OpSize;
  let hasREX_WPrefix  = typeinfo.HasREX_WPrefix;
}

// BinOpRR - Instructions like "add reg, reg, reg".
class BinOpRR<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              dag outlist, X86FoldableSchedWrite sched, list<dag> pattern>
  : ITy<opcode, MRMDestReg, typeinfo, outlist,
        (ins typeinfo.RegClass:$src1, typeinfo.RegClass:$src2),
        mnemonic, "{$src2, $src1|$src1, $src2}", pattern>,
    Sched<[sched]>;

// BinOpRR_F - Instructions like "cmp reg, Reg", where the pattern has
// just a EFLAGS as a result.
class BinOpRR_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                SDPatternOperator opnode>
  : BinOpRR<opcode, mnemonic, typeinfo, (outs), WriteALU,
            [(set EFLAGS,
                  (opnode typeinfo.RegClass:$src1, typeinfo.RegClass:$src2))]>;

// BinOpRR_RF - Instructions like "add reg, reg, reg", where the pattern has
// both a regclass and EFLAGS as a result.
class BinOpRR_RF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                 SDNode opnode>
  : BinOpRR<opcode, mnemonic, typeinfo, (outs typeinfo.RegClass:$dst), WriteALU,
            [(set typeinfo.RegClass:$dst, EFLAGS,
                  (opnode typeinfo.RegClass:$src1, typeinfo.RegClass:$src2))]>;

// BinOpRR_RFF - Instructions like "adc reg, reg, reg", where the pattern has
// both a regclass and EFLAGS as a result, and has EFLAGS as input.
class BinOpRR_RFF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  SDNode opnode>
  : BinOpRR<opcode, mnemonic, typeinfo, (outs typeinfo.RegClass:$dst), WriteADC,
            [(set typeinfo.RegClass:$dst, EFLAGS,
                  (opnode typeinfo.RegClass:$src1, typeinfo.RegClass:$src2,
                          EFLAGS))]>;

// BinOpRR_Rev - Instructions like "add reg, reg, reg" (reversed encoding).
class BinOpRR_Rev<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  X86FoldableSchedWrite sched = WriteALU>
  : ITy<opcode, MRMSrcReg, typeinfo,
        (outs typeinfo.RegClass:$dst),
        (ins typeinfo.RegClass:$src1, typeinfo.RegClass:$src2),
        mnemonic, "{$src2, $dst|$dst, $src2}", []>,
    Sched<[sched]> {
  // The disassembler should know about this, but not the asmparser.
  let isCodeGenOnly = 1;
  let ForceDisassemble = 1;
  let hasSideEffects = 0;
}

// BinOpRR_RDD_Rev - Instructions like "adc reg, reg, reg" (reversed encoding).
class BinOpRR_RFF_Rev<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo>
  : BinOpRR_Rev<opcode, mnemonic, typeinfo, WriteADC>;

// BinOpRR_F_Rev - Instructions like "cmp reg, reg" (reversed encoding).
class BinOpRR_F_Rev<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo>
  : ITy<opcode, MRMSrcReg, typeinfo, (outs),
        (ins typeinfo.RegClass:$src1, typeinfo.RegClass:$src2),
        mnemonic, "{$src2, $src1|$src1, $src2}", []>,
    Sched<[WriteALU]> {
  // The disassembler should know about this, but not the asmparser.
  let isCodeGenOnly = 1;
  let ForceDisassemble = 1;
  let hasSideEffects = 0;
}

// BinOpRM - Instructions like "add reg, reg, [mem]".
class BinOpRM<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              dag outlist, X86FoldableSchedWrite sched, list<dag> pattern>
  : ITy<opcode, MRMSrcMem, typeinfo, outlist,
        (ins typeinfo.RegClass:$src1, typeinfo.MemOperand:$src2),
        mnemonic, "{$src2, $src1|$src1, $src2}", pattern>,
    Sched<[sched.Folded, sched.ReadAfterFold]>;

// BinOpRM_F - Instructions like "cmp reg, [mem]".
class BinOpRM_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                SDNode opnode>
  : BinOpRM<opcode, mnemonic, typeinfo, (outs), WriteALU,
            [(set EFLAGS,
            (opnode typeinfo.RegClass:$src1, (typeinfo.LoadNode addr:$src2)))]>;

// BinOpRM_RF - Instructions like "add reg, reg, [mem]".
class BinOpRM_RF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                 SDNode opnode>
  : BinOpRM<opcode, mnemonic, typeinfo, (outs typeinfo.RegClass:$dst), WriteALU,
            [(set typeinfo.RegClass:$dst, EFLAGS,
            (opnode typeinfo.RegClass:$src1, (typeinfo.LoadNode addr:$src2)))]>;

// BinOpRM_RFF - Instructions like "adc reg, reg, [mem]".
class BinOpRM_RFF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                 SDNode opnode>
  : BinOpRM<opcode, mnemonic, typeinfo, (outs typeinfo.RegClass:$dst), WriteADC,
            [(set typeinfo.RegClass:$dst, EFLAGS,
            (opnode typeinfo.RegClass:$src1, (typeinfo.LoadNode addr:$src2),
                    EFLAGS))]>;

// BinOpRI - Instructions like "add reg, reg, imm".
class BinOpRI<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              Format f, dag outlist, X86FoldableSchedWrite sched, list<dag> pattern>
  : ITy<opcode, f, typeinfo, outlist,
        (ins typeinfo.RegClass:$src1, typeinfo.ImmOperand:$src2),
        mnemonic, "{$src2, $src1|$src1, $src2}", pattern>,
    Sched<[sched]> {
  let ImmT = typeinfo.ImmEncoding;
}

// BinOpRI_F - Instructions like "cmp reg, imm".
class BinOpRI_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                SDPatternOperator opnode, Format f>
  : BinOpRI<opcode, mnemonic, typeinfo, f, (outs), WriteALU,
            [(set EFLAGS,
                (opnode typeinfo.RegClass:$src1, typeinfo.ImmOperator:$src2))]>;

// BinOpRI_RF - Instructions like "add reg, reg, imm".
class BinOpRI_RF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                 SDNode opnode, Format f>
  : BinOpRI<opcode, mnemonic, typeinfo, f, (outs typeinfo.RegClass:$dst), WriteALU,
            [(set typeinfo.RegClass:$dst, EFLAGS,
                (opnode typeinfo.RegClass:$src1, typeinfo.ImmOperator:$src2))]>;
// BinOpRI_RFF - Instructions like "adc reg, reg, imm".
class BinOpRI_RFF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                 SDNode opnode, Format f>
  : BinOpRI<opcode, mnemonic, typeinfo, f, (outs typeinfo.RegClass:$dst), WriteADC,
            [(set typeinfo.RegClass:$dst, EFLAGS,
                (opnode typeinfo.RegClass:$src1, typeinfo.ImmOperator:$src2,
                        EFLAGS))]>;

// BinOpRI8 - Instructions like "add reg, reg, imm8".
class BinOpRI8<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
               Format f, dag outlist, X86FoldableSchedWrite sched, list<dag> pattern>
  : ITy<opcode, f, typeinfo, outlist,
        (ins typeinfo.RegClass:$src1, typeinfo.Imm8Operand:$src2),
        mnemonic, "{$src2, $src1|$src1, $src2}", pattern>,
    Sched<[sched]> {
  let ImmT = Imm8; // Always 8-bit immediate.
}

// BinOpRI8_F - Instructions like "cmp reg, imm8".
class BinOpRI8_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  SDPatternOperator opnode, Format f>
  : BinOpRI8<opcode, mnemonic, typeinfo, f, (outs), WriteALU,
             [(set EFLAGS,
               (opnode typeinfo.RegClass:$src1, typeinfo.Imm8Operator:$src2))]>;

// BinOpRI8_RF - Instructions like "add reg, reg, imm8".
class BinOpRI8_RF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  SDPatternOperator opnode, Format f>
  : BinOpRI8<opcode, mnemonic, typeinfo, f, (outs typeinfo.RegClass:$dst), WriteALU,
             [(set typeinfo.RegClass:$dst, EFLAGS,
               (opnode typeinfo.RegClass:$src1, typeinfo.Imm8Operator:$src2))]>;

// BinOpRI8_RFF - Instructions like "adc reg, reg, imm8".
class BinOpRI8_RFF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                   SDPatternOperator opnode, Format f>
  : BinOpRI8<opcode, mnemonic, typeinfo, f, (outs typeinfo.RegClass:$dst), WriteADC,
             [(set typeinfo.RegClass:$dst, EFLAGS,
               (opnode typeinfo.RegClass:$src1, typeinfo.Imm8Operator:$src2,
                       EFLAGS))]>;

// BinOpMR - Instructions like "add [mem], reg".
class BinOpMR<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              list<dag> pattern>
  : ITy<opcode, MRMDestMem, typeinfo,
        (outs), (ins typeinfo.MemOperand:$dst, typeinfo.RegClass:$src),
        mnemonic, "{$src, $dst|$dst, $src}", pattern>;

// BinOpMR_RMW - Instructions like "add [mem], reg".
class BinOpMR_RMW<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  SDNode opnode>
  : BinOpMR<opcode, mnemonic, typeinfo,
          [(store (opnode (load addr:$dst), typeinfo.RegClass:$src), addr:$dst),
           (implicit EFLAGS)]>, Sched<[WriteALURMW]>;

// BinOpMR_RMW_FF - Instructions like "adc [mem], reg".
class BinOpMR_RMW_FF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                    SDNode opnode>
  : BinOpMR<opcode, mnemonic, typeinfo,
            [(store (opnode (load addr:$dst), typeinfo.RegClass:$src, EFLAGS),
                    addr:$dst),
             (implicit EFLAGS)]>, Sched<[WriteADCRMW]>;

// BinOpMR_F - Instructions like "cmp [mem], reg".
class BinOpMR_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                SDPatternOperator opnode>
  : BinOpMR<opcode, mnemonic, typeinfo,
            [(set EFLAGS, (opnode (typeinfo.LoadNode addr:$dst),
                                   typeinfo.RegClass:$src))]>,
            Sched<[WriteALU.Folded, ReadDefault, ReadDefault, ReadDefault,
                   ReadDefault, ReadDefault, WriteALU.ReadAfterFold]>;

// BinOpMI - Instructions like "add [mem], imm".
class BinOpMI<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              Format f, list<dag> pattern>
  : ITy<opcode, f, typeinfo,
        (outs), (ins typeinfo.MemOperand:$dst, typeinfo.ImmOperand:$src),
        mnemonic, "{$src, $dst|$dst, $src}", pattern> {
  let ImmT = typeinfo.ImmEncoding;
}

// BinOpMI_RMW - Instructions like "add [mem], imm".
class BinOpMI_RMW<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  SDNode opnode, Format f>
  : BinOpMI<opcode, mnemonic, typeinfo, f,
            [(store (opnode (typeinfo.VT (load addr:$dst)),
                            typeinfo.ImmOperator:$src), addr:$dst),
             (implicit EFLAGS)]>, Sched<[WriteALURMW]>;
// BinOpMI_RMW_FF - Instructions like "adc [mem], imm".
class BinOpMI_RMW_FF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                     SDNode opnode, Format f>
  : BinOpMI<opcode, mnemonic, typeinfo, f,
            [(store (opnode (typeinfo.VT (load addr:$dst)),
                             typeinfo.ImmOperator:$src, EFLAGS), addr:$dst),
             (implicit EFLAGS)]>, Sched<[WriteADCRMW]>;

// BinOpMI_F - Instructions like "cmp [mem], imm".
class BinOpMI_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                SDPatternOperator opnode, Format f>
  : BinOpMI<opcode, mnemonic, typeinfo, f,
            [(set EFLAGS, (opnode (typeinfo.LoadNode addr:$dst),
                                  typeinfo.ImmOperator:$src))]>,
            Sched<[WriteALU.Folded]>;

// BinOpMI8 - Instructions like "add [mem], imm8".
class BinOpMI8<string mnemonic, X86TypeInfo typeinfo,
               Format f, list<dag> pattern>
  : ITy<0x82, f, typeinfo,
        (outs), (ins typeinfo.MemOperand:$dst, typeinfo.Imm8Operand:$src),
        mnemonic, "{$src, $dst|$dst, $src}", pattern> {
  let ImmT = Imm8; // Always 8-bit immediate.
}

// BinOpMI8_RMW - Instructions like "add [mem], imm8".
class BinOpMI8_RMW<string mnemonic, X86TypeInfo typeinfo,
                   SDPatternOperator opnode, Format f>
  : BinOpMI8<mnemonic, typeinfo, f,
             [(store (opnode (load addr:$dst),
                             typeinfo.Imm8Operator:$src), addr:$dst),
              (implicit EFLAGS)]>, Sched<[WriteALURMW]>;

// BinOpMI8_RMW_FF - Instructions like "adc [mem], imm8".
class BinOpMI8_RMW_FF<string mnemonic, X86TypeInfo typeinfo,
                      SDPatternOperator opnode, Format f>
  : BinOpMI8<mnemonic, typeinfo, f,
             [(store (opnode (load addr:$dst),
                             typeinfo.Imm8Operator:$src, EFLAGS), addr:$dst),
              (implicit EFLAGS)]>, Sched<[WriteADCRMW]>;

// BinOpMI8_F - Instructions like "cmp [mem], imm8".
class BinOpMI8_F<string mnemonic, X86TypeInfo typeinfo,
                 SDPatternOperator opnode, Format f>
  : BinOpMI8<mnemonic, typeinfo, f,
             [(set EFLAGS, (opnode (typeinfo.LoadNode addr:$dst),
                                    typeinfo.Imm8Operator:$src))]>,
             Sched<[WriteALU.Folded]>;

// BinOpAI - Instructions like "add %eax, %eax, imm", that imp-def EFLAGS.
class BinOpAI<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
              Register areg, string operands, X86FoldableSchedWrite sched = WriteALU>
  : ITy<opcode, RawFrm, typeinfo,
        (outs), (ins typeinfo.ImmOperand:$src),
        mnemonic, operands, []>, Sched<[sched]> {
  let ImmT = typeinfo.ImmEncoding;
  let Uses = [areg];
  let Defs = [areg, EFLAGS];
  let hasSideEffects = 0;
}

// BinOpAI_RFF - Instructions like "adc %eax, %eax, imm", that implicitly define
// and use EFLAGS.
class BinOpAI_RFF<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                  Register areg, string operands>
  : BinOpAI<opcode, mnemonic, typeinfo, areg, operands, WriteADC> {
  let Uses = [areg, EFLAGS];
}

// BinOpAI_F - Instructions like "cmp %eax, %eax, imm", that imp-def EFLAGS.
class BinOpAI_F<bits<8> opcode, string mnemonic, X86TypeInfo typeinfo,
                Register areg, string operands>
  : BinOpAI<opcode, mnemonic, typeinfo, areg, operands> {
  let Defs = [EFLAGS];
}

/// ArithBinOp_RF - This is an arithmetic binary operator where the pattern is
/// defined with "(set GPR:$dst, EFLAGS, (...".
///
/// It would be nice to get rid of the second and third argument here, but
/// tblgen can't handle dependent type references aggressively enough: PR8330
multiclass ArithBinOp_RF<bits<8> BaseOpc, bits<8> BaseOpc2, bits<8> BaseOpc4,
                         string mnemonic, Format RegMRM, Format MemMRM,
                         SDNode opnodeflag, SDNode opnode,
                         bit CommutableRR, bit ConvertibleToThreeAddress> {
  let Defs = [EFLAGS] in {
    let Constraints = "$src1 = $dst" in {
      let isCommutable = CommutableRR in {
        let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
          def NAME#8rr  : BinOpRR_RF<BaseOpc, mnemonic, Xi8 , opnodeflag>;
          def NAME#16rr : BinOpRR_RF<BaseOpc, mnemonic, Xi16, opnodeflag>;
          def NAME#32rr : BinOpRR_RF<BaseOpc, mnemonic, Xi32, opnodeflag>;
          def NAME#64rr : BinOpRR_RF<BaseOpc, mnemonic, Xi64, opnodeflag>;
        } // isConvertibleToThreeAddress
      } // isCommutable

      def NAME#8rr_REV  : BinOpRR_Rev<BaseOpc2, mnemonic, Xi8>, FoldGenData<NAME#8rr>;
      def NAME#16rr_REV : BinOpRR_Rev<BaseOpc2, mnemonic, Xi16>, FoldGenData<NAME#16rr>;
      def NAME#32rr_REV : BinOpRR_Rev<BaseOpc2, mnemonic, Xi32>, FoldGenData<NAME#32rr>;
      def NAME#64rr_REV : BinOpRR_Rev<BaseOpc2, mnemonic, Xi64>, FoldGenData<NAME#64rr>;

      def NAME#8rm   : BinOpRM_RF<BaseOpc2, mnemonic, Xi8 , opnodeflag>;
      def NAME#16rm  : BinOpRM_RF<BaseOpc2, mnemonic, Xi16, opnodeflag>;
      def NAME#32rm  : BinOpRM_RF<BaseOpc2, mnemonic, Xi32, opnodeflag>;
      def NAME#64rm  : BinOpRM_RF<BaseOpc2, mnemonic, Xi64, opnodeflag>;

      let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
        def NAME#8ri   : BinOpRI_RF<0x80, mnemonic, Xi8 , opnodeflag, RegMRM>;

        // NOTE: These are order specific, we want the ri8 forms to be listed
        // first so that they are slightly preferred to the ri forms.
        def NAME#16ri8 : BinOpRI8_RF<0x82, mnemonic, Xi16, opnodeflag, RegMRM>;
        def NAME#32ri8 : BinOpRI8_RF<0x82, mnemonic, Xi32, opnodeflag, RegMRM>;
        def NAME#64ri8 : BinOpRI8_RF<0x82, mnemonic, Xi64, opnodeflag, RegMRM>;

        def NAME#16ri  : BinOpRI_RF<0x80, mnemonic, Xi16, opnodeflag, RegMRM>;
        def NAME#32ri  : BinOpRI_RF<0x80, mnemonic, Xi32, opnodeflag, RegMRM>;
        def NAME#64ri32: BinOpRI_RF<0x80, mnemonic, Xi64, opnodeflag, RegMRM>;
      }
    } // Constraints = "$src1 = $dst"

    let mayLoad = 1, mayStore = 1 in {
      def NAME#8mr    : BinOpMR_RMW<BaseOpc, mnemonic, Xi8 , opnode>;
      def NAME#16mr   : BinOpMR_RMW<BaseOpc, mnemonic, Xi16, opnode>;
      def NAME#32mr   : BinOpMR_RMW<BaseOpc, mnemonic, Xi32, opnode>;
      def NAME#64mr   : BinOpMR_RMW<BaseOpc, mnemonic, Xi64, opnode>;
    }

    // NOTE: These are order specific, we want the mi8 forms to be listed
    // first so that they are slightly preferred to the mi forms.
    def NAME#16mi8  : BinOpMI8_RMW<mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi8  : BinOpMI8_RMW<mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi8  : BinOpMI8_RMW<mnemonic, Xi64, opnode, MemMRM>;

    def NAME#8mi    : BinOpMI_RMW<0x80, mnemonic, Xi8 , opnode, MemMRM>;
    def NAME#16mi   : BinOpMI_RMW<0x80, mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi   : BinOpMI_RMW<0x80, mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi32 : BinOpMI_RMW<0x80, mnemonic, Xi64, opnode, MemMRM>;

    // These are for the disassembler since 0x82 opcode behaves like 0x80, but
    // not in 64-bit mode.
    let Predicates = [Not64BitMode], isCodeGenOnly = 1, ForceDisassemble = 1,
        hasSideEffects = 0 in {
      let Constraints = "$src1 = $dst" in
        def NAME#8ri8 : BinOpRI8_RF<0x82, mnemonic, Xi8, null_frag, RegMRM>;
      let mayLoad = 1, mayStore = 1 in
        def NAME#8mi8 : BinOpMI8_RMW<mnemonic, Xi8, null_frag, MemMRM>;
    }
  } // Defs = [EFLAGS]

  def NAME#8i8   : BinOpAI<BaseOpc4, mnemonic, Xi8 , AL,
                           "{$src, %al|al, $src}">;
  def NAME#16i16 : BinOpAI<BaseOpc4, mnemonic, Xi16, AX,
                           "{$src, %ax|ax, $src}">;
  def NAME#32i32 : BinOpAI<BaseOpc4, mnemonic, Xi32, EAX,
                           "{$src, %eax|eax, $src}">;
  def NAME#64i32 : BinOpAI<BaseOpc4, mnemonic, Xi64, RAX,
                           "{$src, %rax|rax, $src}">;
}

/// ArithBinOp_RFF - This is an arithmetic binary operator where the pattern is
/// defined with "(set GPR:$dst, EFLAGS, (node LHS, RHS, EFLAGS))" like ADC and
/// SBB.
///
/// It would be nice to get rid of the second and third argument here, but
/// tblgen can't handle dependent type references aggressively enough: PR8330
multiclass ArithBinOp_RFF<bits<8> BaseOpc, bits<8> BaseOpc2, bits<8> BaseOpc4,
                          string mnemonic, Format RegMRM, Format MemMRM,
                          SDNode opnode, bit CommutableRR,
                           bit ConvertibleToThreeAddress> {
  let Uses = [EFLAGS], Defs = [EFLAGS] in {
    let Constraints = "$src1 = $dst" in {
      let isCommutable = CommutableRR in {
        def NAME#8rr  : BinOpRR_RFF<BaseOpc, mnemonic, Xi8 , opnode>;
        let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
          def NAME#16rr : BinOpRR_RFF<BaseOpc, mnemonic, Xi16, opnode>;
          def NAME#32rr : BinOpRR_RFF<BaseOpc, mnemonic, Xi32, opnode>;
          def NAME#64rr : BinOpRR_RFF<BaseOpc, mnemonic, Xi64, opnode>;
        } // isConvertibleToThreeAddress
      } // isCommutable

      def NAME#8rr_REV  : BinOpRR_RFF_Rev<BaseOpc2, mnemonic, Xi8>, FoldGenData<NAME#8rr>;
      def NAME#16rr_REV : BinOpRR_RFF_Rev<BaseOpc2, mnemonic, Xi16>, FoldGenData<NAME#16rr>;
      def NAME#32rr_REV : BinOpRR_RFF_Rev<BaseOpc2, mnemonic, Xi32>, FoldGenData<NAME#32rr>;
      def NAME#64rr_REV : BinOpRR_RFF_Rev<BaseOpc2, mnemonic, Xi64>, FoldGenData<NAME#64rr>;

      def NAME#8rm   : BinOpRM_RFF<BaseOpc2, mnemonic, Xi8 , opnode>;
      def NAME#16rm  : BinOpRM_RFF<BaseOpc2, mnemonic, Xi16, opnode>;
      def NAME#32rm  : BinOpRM_RFF<BaseOpc2, mnemonic, Xi32, opnode>;
      def NAME#64rm  : BinOpRM_RFF<BaseOpc2, mnemonic, Xi64, opnode>;

      def NAME#8ri   : BinOpRI_RFF<0x80, mnemonic, Xi8 , opnode, RegMRM>;

      let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
        // NOTE: These are order specific, we want the ri8 forms to be listed
        // first so that they are slightly preferred to the ri forms.
        def NAME#16ri8 : BinOpRI8_RFF<0x82, mnemonic, Xi16, opnode, RegMRM>;
        def NAME#32ri8 : BinOpRI8_RFF<0x82, mnemonic, Xi32, opnode, RegMRM>;
        def NAME#64ri8 : BinOpRI8_RFF<0x82, mnemonic, Xi64, opnode, RegMRM>;

        def NAME#16ri  : BinOpRI_RFF<0x80, mnemonic, Xi16, opnode, RegMRM>;
        def NAME#32ri  : BinOpRI_RFF<0x80, mnemonic, Xi32, opnode, RegMRM>;
        def NAME#64ri32: BinOpRI_RFF<0x80, mnemonic, Xi64, opnode, RegMRM>;
      }
    } // Constraints = "$src1 = $dst"

    def NAME#8mr    : BinOpMR_RMW_FF<BaseOpc, mnemonic, Xi8 , opnode>;
    def NAME#16mr   : BinOpMR_RMW_FF<BaseOpc, mnemonic, Xi16, opnode>;
    def NAME#32mr   : BinOpMR_RMW_FF<BaseOpc, mnemonic, Xi32, opnode>;
    def NAME#64mr   : BinOpMR_RMW_FF<BaseOpc, mnemonic, Xi64, opnode>;

    // NOTE: These are order specific, we want the mi8 forms to be listed
    // first so that they are slightly preferred to the mi forms.
    def NAME#16mi8  : BinOpMI8_RMW_FF<mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi8  : BinOpMI8_RMW_FF<mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi8  : BinOpMI8_RMW_FF<mnemonic, Xi64, opnode, MemMRM>;

    def NAME#8mi    : BinOpMI_RMW_FF<0x80, mnemonic, Xi8 , opnode, MemMRM>;
    def NAME#16mi   : BinOpMI_RMW_FF<0x80, mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi   : BinOpMI_RMW_FF<0x80, mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi32 : BinOpMI_RMW_FF<0x80, mnemonic, Xi64, opnode, MemMRM>;

    // These are for the disassembler since 0x82 opcode behaves like 0x80, but
    // not in 64-bit mode.
    let Predicates = [Not64BitMode], isCodeGenOnly = 1, ForceDisassemble = 1,
        hasSideEffects = 0 in {
      let Constraints = "$src1 = $dst" in
        def NAME#8ri8 : BinOpRI8_RFF<0x82, mnemonic, Xi8, null_frag, RegMRM>;
      let mayLoad = 1, mayStore = 1 in
        def NAME#8mi8 : BinOpMI8_RMW_FF<mnemonic, Xi8, null_frag, MemMRM>;
    }
  } // Uses = [EFLAGS], Defs = [EFLAGS]

  def NAME#8i8   : BinOpAI_RFF<BaseOpc4, mnemonic, Xi8 , AL,
                               "{$src, %al|al, $src}">;
  def NAME#16i16 : BinOpAI_RFF<BaseOpc4, mnemonic, Xi16, AX,
                               "{$src, %ax|ax, $src}">;
  def NAME#32i32 : BinOpAI_RFF<BaseOpc4, mnemonic, Xi32, EAX,
                               "{$src, %eax|eax, $src}">;
  def NAME#64i32 : BinOpAI_RFF<BaseOpc4, mnemonic, Xi64, RAX,
                               "{$src, %rax|rax, $src}">;
}

/// ArithBinOp_F - This is an arithmetic binary operator where the pattern is
/// defined with "(set EFLAGS, (...".  It would be really nice to find a way
/// to factor this with the other ArithBinOp_*.
///
multiclass ArithBinOp_F<bits<8> BaseOpc, bits<8> BaseOpc2, bits<8> BaseOpc4,
                        string mnemonic, Format RegMRM, Format MemMRM,
                        SDNode opnode,
                        bit CommutableRR, bit ConvertibleToThreeAddress> {
  let Defs = [EFLAGS] in {
    let isCommutable = CommutableRR in {
      def NAME#8rr  : BinOpRR_F<BaseOpc, mnemonic, Xi8 , opnode>;
      let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
        def NAME#16rr : BinOpRR_F<BaseOpc, mnemonic, Xi16, opnode>;
        def NAME#32rr : BinOpRR_F<BaseOpc, mnemonic, Xi32, opnode>;
        def NAME#64rr : BinOpRR_F<BaseOpc, mnemonic, Xi64, opnode>;
      }
    } // isCommutable

    def NAME#8rr_REV  : BinOpRR_F_Rev<BaseOpc2, mnemonic, Xi8>, FoldGenData<NAME#8rr>;
    def NAME#16rr_REV : BinOpRR_F_Rev<BaseOpc2, mnemonic, Xi16>, FoldGenData<NAME#16rr>;
    def NAME#32rr_REV : BinOpRR_F_Rev<BaseOpc2, mnemonic, Xi32>, FoldGenData<NAME#32rr>;
    def NAME#64rr_REV : BinOpRR_F_Rev<BaseOpc2, mnemonic, Xi64>, FoldGenData<NAME#64rr>;

    def NAME#8rm   : BinOpRM_F<BaseOpc2, mnemonic, Xi8 , opnode>;
    def NAME#16rm  : BinOpRM_F<BaseOpc2, mnemonic, Xi16, opnode>;
    def NAME#32rm  : BinOpRM_F<BaseOpc2, mnemonic, Xi32, opnode>;
    def NAME#64rm  : BinOpRM_F<BaseOpc2, mnemonic, Xi64, opnode>;

    def NAME#8ri   : BinOpRI_F<0x80, mnemonic, Xi8 , opnode, RegMRM>;

    let isConvertibleToThreeAddress = ConvertibleToThreeAddress in {
      // NOTE: These are order specific, we want the ri8 forms to be listed
      // first so that they are slightly preferred to the ri forms.
      def NAME#16ri8 : BinOpRI8_F<0x82, mnemonic, Xi16, opnode, RegMRM>;
      def NAME#32ri8 : BinOpRI8_F<0x82, mnemonic, Xi32, opnode, RegMRM>;
      def NAME#64ri8 : BinOpRI8_F<0x82, mnemonic, Xi64, opnode, RegMRM>;

      def NAME#16ri  : BinOpRI_F<0x80, mnemonic, Xi16, opnode, RegMRM>;
      def NAME#32ri  : BinOpRI_F<0x80, mnemonic, Xi32, opnode, RegMRM>;
      def NAME#64ri32: BinOpRI_F<0x80, mnemonic, Xi64, opnode, RegMRM>;
    }

    def NAME#8mr    : BinOpMR_F<BaseOpc, mnemonic, Xi8 , opnode>;
    def NAME#16mr   : BinOpMR_F<BaseOpc, mnemonic, Xi16, opnode>;
    def NAME#32mr   : BinOpMR_F<BaseOpc, mnemonic, Xi32, opnode>;
    def NAME#64mr   : BinOpMR_F<BaseOpc, mnemonic, Xi64, opnode>;

    // NOTE: These are order specific, we want the mi8 forms to be listed
    // first so that they are slightly preferred to the mi forms.
    def NAME#16mi8  : BinOpMI8_F<mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi8  : BinOpMI8_F<mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi8  : BinOpMI8_F<mnemonic, Xi64, opnode, MemMRM>;

    def NAME#8mi    : BinOpMI_F<0x80, mnemonic, Xi8 , opnode, MemMRM>;
    def NAME#16mi   : BinOpMI_F<0x80, mnemonic, Xi16, opnode, MemMRM>;
    def NAME#32mi   : BinOpMI_F<0x80, mnemonic, Xi32, opnode, MemMRM>;
    let Predicates = [In64BitMode] in
    def NAME#64mi32 : BinOpMI_F<0x80, mnemonic, Xi64, opnode, MemMRM>;

    // These are for the disassembler since 0x82 opcode behaves like 0x80, but
    // not in 64-bit mode.
    let Predicates = [Not64BitMode], isCodeGenOnly = 1, ForceDisassemble = 1,
        hasSideEffects = 0 in {
      def NAME#8ri8 : BinOpRI8_F<0x82, mnemonic, Xi8, null_frag, RegMRM>;
      let mayLoad = 1 in
        def NAME#8mi8 : BinOpMI8_F<mnemonic, Xi8, null_frag, MemMRM>;
    }
  } // Defs = [EFLAGS]

  def NAME#8i8   : BinOpAI_F<BaseOpc4, mnemonic, Xi8 , AL,
                             "{$src, %al|al, $src}">;
  def NAME#16i16 : BinOpAI_F<BaseOpc4, mnemonic, Xi16, AX,
                             "{$src, %ax|ax, $src}">;
  def NAME#32i32 : BinOpAI_F<BaseOpc4, mnemonic, Xi32, EAX,
                             "{$src, %eax|eax, $src}">;
  def NAME#64i32 : BinOpAI_F<BaseOpc4, mnemonic, Xi64, RAX,
                             "{$src, %rax|rax, $src}">;
}


defm AND : ArithBinOp_RF<0x20, 0x22, 0x24, "and", MRM4r, MRM4m,
                         X86and_flag, and, 1, 0>;
defm OR  : ArithBinOp_RF<0x08, 0x0A, 0x0C, "or", MRM1r, MRM1m,
                         X86or_flag, or, 1, 0>;
defm XOR : ArithBinOp_RF<0x30, 0x32, 0x34, "xor", MRM6r, MRM6m,
                         X86xor_flag, xor, 1, 0>;
defm ADD : ArithBinOp_RF<0x00, 0x02, 0x04, "add", MRM0r, MRM0m,
                         X86add_flag, add, 1, 1>;
let isCompare = 1 in {
defm SUB : ArithBinOp_RF<0x28, 0x2A, 0x2C, "sub", MRM5r, MRM5m,
                         X86sub_flag, sub, 0, 0>;
}

// Arithmetic.
defm ADC : ArithBinOp_RFF<0x10, 0x12, 0x14, "adc", MRM2r, MRM2m, X86adc_flag,
                          1, 0>;
defm SBB : ArithBinOp_RFF<0x18, 0x1A, 0x1C, "sbb", MRM3r, MRM3m, X86sbb_flag,
                          0, 0>;

let isCompare = 1 in {
defm CMP : ArithBinOp_F<0x38, 0x3A, 0x3C, "cmp", MRM7r, MRM7m, X86cmp, 0, 0>;
}

// Patterns to recognize loads on the LHS of an ADC. We can't make X86adc_flag
// commutable since it has EFLAGs as an input.
def : Pat<(X86adc_flag (loadi8 addr:$src2), GR8:$src1, EFLAGS),
          (ADC8rm GR8:$src1, addr:$src2)>;
def : Pat<(X86adc_flag (loadi16 addr:$src2), GR16:$src1, EFLAGS),
          (ADC16rm GR16:$src1, addr:$src2)>;
def : Pat<(X86adc_flag (loadi32 addr:$src2), GR32:$src1, EFLAGS),
          (ADC32rm GR32:$src1, addr:$src2)>;
def : Pat<(X86adc_flag (loadi64 addr:$src2), GR64:$src1, EFLAGS),
          (ADC64rm GR64:$src1, addr:$src2)>;

// Patterns to recognize RMW ADC with loads in operand 1.
def : Pat<(store (X86adc_flag GR8:$src, (loadi8 addr:$dst), EFLAGS),
                 addr:$dst),
          (ADC8mr addr:$dst, GR8:$src)>;
def : Pat<(store (X86adc_flag GR16:$src, (loadi16 addr:$dst), EFLAGS),
                 addr:$dst),
          (ADC16mr addr:$dst, GR16:$src)>;
def : Pat<(store (X86adc_flag GR32:$src, (loadi32 addr:$dst), EFLAGS),
                 addr:$dst),
          (ADC32mr addr:$dst, GR32:$src)>;
def : Pat<(store (X86adc_flag GR64:$src, (loadi64 addr:$dst), EFLAGS),
                 addr:$dst),
          (ADC64mr addr:$dst, GR64:$src)>;

//===----------------------------------------------------------------------===//
// Semantically, test instructions are similar like AND, except they don't
// generate a result.  From an encoding perspective, they are very different:
// they don't have all the usual imm8 and REV forms, and are encoded into a
// different space.
def X86testpat : PatFrag<(ops node:$lhs, node:$rhs),
                         (X86cmp (and_su node:$lhs, node:$rhs), 0)>;

let isCompare = 1 in {
  let Defs = [EFLAGS] in {
    let isCommutable = 1 in {
      // Avoid selecting these and instead use a test+and. Post processing will
      // combine them. This gives bunch of other patterns that start with
      // and a chance to match.
      def TEST8rr  : BinOpRR_F<0x84, "test", Xi8 , null_frag>;
      def TEST16rr : BinOpRR_F<0x84, "test", Xi16, null_frag>;
      def TEST32rr : BinOpRR_F<0x84, "test", Xi32, null_frag>;
      def TEST64rr : BinOpRR_F<0x84, "test", Xi64, null_frag>;
    } // isCommutable

    let hasSideEffects = 0, mayLoad = 1 in {
    def TEST8mr    : BinOpMR_F<0x84, "test", Xi8 , null_frag>;
    def TEST16mr   : BinOpMR_F<0x84, "test", Xi16, null_frag>;
    def TEST32mr   : BinOpMR_F<0x84, "test", Xi32, null_frag>;
    def TEST64mr   : BinOpMR_F<0x84, "test", Xi64, null_frag>;
    }

    def TEST8ri    : BinOpRI_F<0xF6, "test", Xi8 , X86testpat, MRM0r>;
    def TEST16ri   : BinOpRI_F<0xF6, "test", Xi16, X86testpat, MRM0r>;
    def TEST32ri   : BinOpRI_F<0xF6, "test", Xi32, X86testpat, MRM0r>;
    let Predicates = [In64BitMode] in
    def TEST64ri32 : BinOpRI_F<0xF6, "test", Xi64, X86testpat, MRM0r>;

    def TEST8mi    : BinOpMI_F<0xF6, "test", Xi8 , X86testpat, MRM0m>;
    def TEST16mi   : BinOpMI_F<0xF6, "test", Xi16, X86testpat, MRM0m>;
    def TEST32mi   : BinOpMI_F<0xF6, "test", Xi32, X86testpat, MRM0m>;
    let Predicates = [In64BitMode] in
    def TEST64mi32 : BinOpMI_F<0xF6, "test", Xi64, X86testpat, MRM0m>;
  } // Defs = [EFLAGS]

  def TEST8i8    : BinOpAI_F<0xA8, "test", Xi8 , AL,
                             "{$src, %al|al, $src}">;
  def TEST16i16  : BinOpAI_F<0xA8, "test", Xi16, AX,
                             "{$src, %ax|ax, $src}">;
  def TEST32i32  : BinOpAI_F<0xA8, "test", Xi32, EAX,
                             "{$src, %eax|eax, $src}">;
  def TEST64i32  : BinOpAI_F<0xA8, "test", Xi64, RAX,
                             "{$src, %rax|rax, $src}">;
} // isCompare

//===----------------------------------------------------------------------===//
// ANDN Instruction
//
multiclass bmi_andn<string mnemonic, RegisterClass RC, X86MemOperand x86memop,
                    PatFrag ld_frag> {
  def rr : I<0xF2, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
            !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
            [(set RC:$dst, EFLAGS, (X86and_flag (not RC:$src1), RC:$src2))]>,
            Sched<[WriteALU]>;
  def rm : I<0xF2, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
            !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
            [(set RC:$dst, EFLAGS,
             (X86and_flag (not RC:$src1), (ld_frag addr:$src2)))]>,
           Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>;
}

// Complexity is reduced to give and with immediate a chance to match first.
let Predicates = [HasBMI], Defs = [EFLAGS], AddedComplexity = -6 in {
  defm ANDN32 : bmi_andn<"andn{l}", GR32, i32mem, loadi32>, T8PS, VEX_4V;
  defm ANDN64 : bmi_andn<"andn{q}", GR64, i64mem, loadi64>, T8PS, VEX_4V, VEX_W;
}

let Predicates = [HasBMI], AddedComplexity = -6 in {
  def : Pat<(and (not GR32:$src1), GR32:$src2),
            (ANDN32rr GR32:$src1, GR32:$src2)>;
  def : Pat<(and (not GR64:$src1), GR64:$src2),
            (ANDN64rr GR64:$src1, GR64:$src2)>;
  def : Pat<(and (not GR32:$src1), (loadi32 addr:$src2)),
            (ANDN32rm GR32:$src1, addr:$src2)>;
  def : Pat<(and (not GR64:$src1), (loadi64 addr:$src2)),
            (ANDN64rm GR64:$src1, addr:$src2)>;
}

//===----------------------------------------------------------------------===//
// MULX Instruction
//
multiclass bmi_mulx<string mnemonic, RegisterClass RC, X86MemOperand x86memop,
                    X86FoldableSchedWrite sched> {
let hasSideEffects = 0 in {
  let isCommutable = 1 in
  def rr : I<0xF6, MRMSrcReg, (outs RC:$dst1, RC:$dst2), (ins RC:$src),
             !strconcat(mnemonic, "\t{$src, $dst2, $dst1|$dst1, $dst2, $src}"),
             []>, T8XD, VEX_4V, Sched<[sched, WriteIMulH]>;

  let mayLoad = 1 in
  def rm : I<0xF6, MRMSrcMem, (outs RC:$dst1, RC:$dst2), (ins x86memop:$src),
             !strconcat(mnemonic, "\t{$src, $dst2, $dst1|$dst1, $dst2, $src}"),
             []>, T8XD, VEX_4V, Sched<[sched.Folded, WriteIMulH]>;
}
}

let Predicates = [HasBMI2] in {
  let Uses = [EDX] in
    defm MULX32 : bmi_mulx<"mulx{l}", GR32, i32mem, WriteIMul32>;
  let Uses = [RDX] in
    defm MULX64 : bmi_mulx<"mulx{q}", GR64, i64mem, WriteIMul64>, VEX_W;
}

//===----------------------------------------------------------------------===//
// ADCX and ADOX Instructions
//
// We don't have patterns for these as there is no advantage over ADC for
// most code.
let Predicates = [HasADX], Defs = [EFLAGS], Uses = [EFLAGS],
    Constraints = "$src1 = $dst", hasSideEffects = 0 in {
  let SchedRW = [WriteADC], isCommutable = 1 in {
  def ADCX32rr : I<0xF6, MRMSrcReg, (outs GR32:$dst),
                   (ins GR32:$src1, GR32:$src2),
                   "adcx{l}\t{$src2, $dst|$dst, $src2}", []>, T8PD;
  def ADCX64rr : RI<0xF6, MRMSrcReg, (outs GR64:$dst),
                    (ins GR64:$src1, GR64:$src2),
                    "adcx{q}\t{$src2, $dst|$dst, $src2}", []>, T8PD;

  def ADOX32rr : I<0xF6, MRMSrcReg, (outs GR32:$dst),
                   (ins GR32:$src1, GR32:$src2),
                   "adox{l}\t{$src2, $dst|$dst, $src2}", []>, T8XS;

  def ADOX64rr : RI<0xF6, MRMSrcReg, (outs GR64:$dst),
                    (ins GR64:$src1, GR64:$src2),
                    "adox{q}\t{$src2, $dst|$dst, $src2}", []>, T8XS;
  } // SchedRW

  let mayLoad = 1, SchedRW = [WriteADC.Folded, WriteADC.ReadAfterFold] in {
  def ADCX32rm : I<0xF6, MRMSrcMem, (outs GR32:$dst),
                   (ins GR32:$src1, i32mem:$src2),
                   "adcx{l}\t{$src2, $dst|$dst, $src2}", []>, T8PD;

  def ADCX64rm : RI<0xF6, MRMSrcMem, (outs GR64:$dst),
                    (ins GR64:$src1, i64mem:$src2),
                    "adcx{q}\t{$src2, $dst|$dst, $src2}", []>, T8PD;

  def ADOX32rm : I<0xF6, MRMSrcMem, (outs GR32:$dst),
                   (ins GR32:$src1, i32mem:$src2),
                   "adox{l}\t{$src2, $dst|$dst, $src2}", []>, T8XS;

  def ADOX64rm : RI<0xF6, MRMSrcMem, (outs GR64:$dst),
                    (ins GR64:$src1, i64mem:$src2),
                    "adox{q}\t{$src2, $dst|$dst, $src2}", []>, T8XS;
  } // mayLoad, SchedRW
}
