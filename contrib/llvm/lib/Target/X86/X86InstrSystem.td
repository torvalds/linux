//===-- X86InstrSystem.td - System Instructions ------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 instructions that are generally used in
// privileged modes.  These are not typically used by the compiler, but are
// supported for the assembler and disassembler.
//
//===----------------------------------------------------------------------===//

let SchedRW = [WriteSystem] in {
let Defs = [RAX, RDX] in
  def RDTSC : I<0x31, RawFrm, (outs), (ins), "rdtsc", [(X86rdtsc)]>, TB;

let Defs = [RAX, RCX, RDX] in
  def RDTSCP : I<0x01, MRM_F9, (outs), (ins), "rdtscp", [(X86rdtscp)]>, TB;

// CPU flow control instructions

let mayLoad = 1, mayStore = 0, hasSideEffects = 1, isTrap = 1 in {
  def TRAP    : I<0x0B, RawFrm, (outs), (ins), "ud2", [(trap)]>, TB;
  def UD2B    : I<0xB9, RawFrm, (outs), (ins), "ud2b", []>, TB;
}

def HLT : I<0xF4, RawFrm, (outs), (ins), "hlt", []>;
def RSM : I<0xAA, RawFrm, (outs), (ins), "rsm", []>, TB;

// Interrupt and SysCall Instructions.
let Uses = [EFLAGS] in
  def INTO : I<0xce, RawFrm, (outs), (ins), "into", []>, Requires<[Not64BitMode]>;

def INT3 : I<0xcc, RawFrm, (outs), (ins), "int3", [(int_x86_int (i8 3))]>;
} // SchedRW

// The long form of "int $3" turns into int3 as a size optimization.
// FIXME: This doesn't work because InstAlias can't match immediate constants.
//def : InstAlias<"int\t$3", (INT3)>;

let SchedRW = [WriteSystem] in {

def INT : Ii8<0xcd, RawFrm, (outs), (ins u8imm:$trap), "int\t$trap",
              [(int_x86_int imm:$trap)]>;


def SYSCALL  : I<0x05, RawFrm, (outs), (ins), "syscall", []>, TB;
def SYSRET   : I<0x07, RawFrm, (outs), (ins), "sysret{l}", []>, TB;
def SYSRET64 :RI<0x07, RawFrm, (outs), (ins), "sysretq", []>, TB,
               Requires<[In64BitMode]>;

def SYSENTER : I<0x34, RawFrm, (outs), (ins), "sysenter", []>, TB;

def SYSEXIT   : I<0x35, RawFrm, (outs), (ins), "sysexit{l}", []>, TB;
def SYSEXIT64 :RI<0x35, RawFrm, (outs), (ins), "sysexitq", []>, TB,
                  Requires<[In64BitMode]>;
} // SchedRW

def : Pat<(debugtrap),
          (INT3)>, Requires<[NotPS4]>;
def : Pat<(debugtrap),
          (INT (i8 0x41))>, Requires<[IsPS4]>;

//===----------------------------------------------------------------------===//
//  Input/Output Instructions.
//
let SchedRW = [WriteSystem] in {
let Defs = [AL], Uses = [DX] in
def IN8rr  : I<0xEC, RawFrm, (outs), (ins), "in{b}\t{%dx, %al|al, dx}", []>;
let Defs = [AX], Uses = [DX] in
def IN16rr : I<0xED, RawFrm, (outs), (ins), "in{w}\t{%dx, %ax|ax, dx}", []>,
               OpSize16;
let Defs = [EAX], Uses = [DX] in
def IN32rr : I<0xED, RawFrm, (outs), (ins), "in{l}\t{%dx, %eax|eax, dx}", []>,
               OpSize32;

let Defs = [AL] in
def IN8ri  : Ii8<0xE4, RawFrm, (outs), (ins u8imm:$port),
                 "in{b}\t{$port, %al|al, $port}", []>;
let Defs = [AX] in
def IN16ri : Ii8<0xE5, RawFrm, (outs), (ins u8imm:$port),
                 "in{w}\t{$port, %ax|ax, $port}", []>, OpSize16;
let Defs = [EAX] in
def IN32ri : Ii8<0xE5, RawFrm, (outs), (ins u8imm:$port),
                 "in{l}\t{$port, %eax|eax, $port}", []>, OpSize32;

let Uses = [DX, AL] in
def OUT8rr  : I<0xEE, RawFrm, (outs), (ins), "out{b}\t{%al, %dx|dx, al}", []>;
let Uses = [DX, AX] in
def OUT16rr : I<0xEF, RawFrm, (outs), (ins), "out{w}\t{%ax, %dx|dx, ax}", []>,
                OpSize16;
let Uses = [DX, EAX] in
def OUT32rr : I<0xEF, RawFrm, (outs), (ins), "out{l}\t{%eax, %dx|dx, eax}", []>,
                OpSize32;

let Uses = [AL] in
def OUT8ir  : Ii8<0xE6, RawFrm, (outs), (ins u8imm:$port),
                   "out{b}\t{%al, $port|$port, al}", []>;
let Uses = [AX] in
def OUT16ir : Ii8<0xE7, RawFrm, (outs), (ins u8imm:$port),
                   "out{w}\t{%ax, $port|$port, ax}", []>, OpSize16;
let Uses = [EAX] in
def OUT32ir : Ii8<0xE7, RawFrm, (outs), (ins u8imm:$port),
                  "out{l}\t{%eax, $port|$port, eax}", []>, OpSize32;

} // SchedRW

//===----------------------------------------------------------------------===//
// Moves to and from debug registers

let SchedRW = [WriteSystem] in {
def MOV32rd : I<0x21, MRMDestReg, (outs GR32:$dst), (ins DEBUG_REG:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[Not64BitMode]>;
def MOV64rd : I<0x21, MRMDestReg, (outs GR64:$dst), (ins DEBUG_REG:$src),
                "mov{q}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[In64BitMode]>;

def MOV32dr : I<0x23, MRMSrcReg, (outs DEBUG_REG:$dst), (ins GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[Not64BitMode]>;
def MOV64dr : I<0x23, MRMSrcReg, (outs DEBUG_REG:$dst), (ins GR64:$src),
                "mov{q}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Moves to and from control registers

let SchedRW = [WriteSystem] in {
def MOV32rc : I<0x20, MRMDestReg, (outs GR32:$dst), (ins CONTROL_REG:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[Not64BitMode]>;
def MOV64rc : I<0x20, MRMDestReg, (outs GR64:$dst), (ins CONTROL_REG:$src),
                "mov{q}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[In64BitMode]>;

def MOV32cr : I<0x22, MRMSrcReg, (outs CONTROL_REG:$dst), (ins GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[Not64BitMode]>;
def MOV64cr : I<0x22, MRMSrcReg, (outs CONTROL_REG:$dst), (ins GR64:$src),
                "mov{q}\t{$src, $dst|$dst, $src}", []>, TB,
                Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Segment override instruction prefixes

let SchedRW = [WriteNop] in {
def CS_PREFIX : I<0x2E, RawFrm, (outs), (ins), "cs", []>;
def SS_PREFIX : I<0x36, RawFrm, (outs), (ins), "ss", []>;
def DS_PREFIX : I<0x3E, RawFrm, (outs), (ins), "ds", []>;
def ES_PREFIX : I<0x26, RawFrm, (outs), (ins), "es", []>;
def FS_PREFIX : I<0x64, RawFrm, (outs), (ins), "fs", []>;
def GS_PREFIX : I<0x65, RawFrm, (outs), (ins), "gs", []>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Moves to and from segment registers.
//

let SchedRW = [WriteMove] in {
def MOV16rs : I<0x8C, MRMDestReg, (outs GR16:$dst), (ins SEGMENT_REG:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32rs : I<0x8C, MRMDestReg, (outs GR32:$dst), (ins SEGMENT_REG:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOV64rs : RI<0x8C, MRMDestReg, (outs GR64:$dst), (ins SEGMENT_REG:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}", []>;
let mayStore = 1 in {
def MOV16ms : I<0x8C, MRMDestMem, (outs), (ins i16mem:$dst, SEGMENT_REG:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>;
}
def MOV16sr : I<0x8E, MRMSrcReg, (outs SEGMENT_REG:$dst), (ins GR16:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32sr : I<0x8E, MRMSrcReg, (outs SEGMENT_REG:$dst), (ins GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOV64sr : RI<0x8E, MRMSrcReg, (outs SEGMENT_REG:$dst), (ins GR64:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}", []>;
let mayLoad = 1 in {
def MOV16sm : I<0x8E, MRMSrcMem, (outs SEGMENT_REG:$dst), (ins i16mem:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Segmentation support instructions.

let SchedRW = [WriteSystem] in {
def SWAPGS : I<0x01, MRM_F8, (outs), (ins), "swapgs", []>, TB;

let mayLoad = 1 in
def LAR16rm : I<0x02, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                "lar{w}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize16, NotMemoryFoldable;
def LAR16rr : I<0x02, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                "lar{w}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize16, NotMemoryFoldable;

// i16mem operand in LAR32rm and GR32 operand in LAR32rr is not a typo.
let mayLoad = 1 in
def LAR32rm : I<0x02, MRMSrcMem, (outs GR32:$dst), (ins i16mem:$src),
                "lar{l}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize32, NotMemoryFoldable;
def LAR32rr : I<0x02, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                "lar{l}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize32, NotMemoryFoldable;
// i16mem operand in LAR64rm and GR32 operand in LAR64rr is not a typo.
let mayLoad = 1 in
def LAR64rm : RI<0x02, MRMSrcMem, (outs GR64:$dst), (ins i16mem:$src),
                 "lar{q}\t{$src, $dst|$dst, $src}", []>, TB, NotMemoryFoldable;
def LAR64rr : RI<0x02, MRMSrcReg, (outs GR64:$dst), (ins GR32:$src),
                 "lar{q}\t{$src, $dst|$dst, $src}", []>, TB, NotMemoryFoldable;

// i16mem operand in LSL32rm and GR32 operand in LSL32rr is not a typo.
let mayLoad = 1 in
def LSL16rm : I<0x03, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                "lsl{w}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize16, NotMemoryFoldable;
def LSL16rr : I<0x03, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                "lsl{w}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize16, NotMemoryFoldable;
// i16mem operand in LSL64rm and GR32 operand in LSL64rr is not a typo.
let mayLoad = 1 in
def LSL32rm : I<0x03, MRMSrcMem, (outs GR32:$dst), (ins i16mem:$src),
                "lsl{l}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize32, NotMemoryFoldable;
def LSL32rr : I<0x03, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                "lsl{l}\t{$src, $dst|$dst, $src}", []>, TB,
                OpSize32, NotMemoryFoldable;
let mayLoad = 1 in
def LSL64rm : RI<0x03, MRMSrcMem, (outs GR64:$dst), (ins i16mem:$src),
                 "lsl{q}\t{$src, $dst|$dst, $src}", []>, TB, NotMemoryFoldable;
def LSL64rr : RI<0x03, MRMSrcReg, (outs GR64:$dst), (ins GR32:$src),
                 "lsl{q}\t{$src, $dst|$dst, $src}", []>, TB, NotMemoryFoldable;

def INVLPG : I<0x01, MRM7m, (outs), (ins i8mem:$addr), "invlpg\t$addr", []>, TB;

def STR16r : I<0x00, MRM1r, (outs GR16:$dst), (ins),
               "str{w}\t$dst", []>, TB, OpSize16;
def STR32r : I<0x00, MRM1r, (outs GR32:$dst), (ins),
               "str{l}\t$dst", []>, TB, OpSize32;
def STR64r : RI<0x00, MRM1r, (outs GR64:$dst), (ins),
                "str{q}\t$dst", []>, TB;
let mayStore = 1 in
def STRm   : I<0x00, MRM1m, (outs), (ins i16mem:$dst), "str{w}\t$dst", []>, TB;

def LTRr : I<0x00, MRM3r, (outs), (ins GR16:$src), "ltr{w}\t$src", []>, TB, NotMemoryFoldable;
let mayLoad = 1 in
def LTRm : I<0x00, MRM3m, (outs), (ins i16mem:$src), "ltr{w}\t$src", []>, TB, NotMemoryFoldable;

def PUSHCS16 : I<0x0E, RawFrm, (outs), (ins), "push{w}\t{%cs|cs}", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def PUSHCS32 : I<0x0E, RawFrm, (outs), (ins), "push{l}\t{%cs|cs}", []>,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHSS16 : I<0x16, RawFrm, (outs), (ins), "push{w}\t{%ss|ss}", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def PUSHSS32 : I<0x16, RawFrm, (outs), (ins), "push{l}\t{%ss|ss}", []>,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHDS16 : I<0x1E, RawFrm, (outs), (ins), "push{w}\t{%ds|ds}", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def PUSHDS32 : I<0x1E, RawFrm, (outs), (ins), "push{l}\t{%ds|ds}", []>,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHES16 : I<0x06, RawFrm, (outs), (ins), "push{w}\t{%es|es}", []>,
                 OpSize16, Requires<[Not64BitMode]>;
def PUSHES32 : I<0x06, RawFrm, (outs), (ins), "push{l}\t{%es|es}", []>,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHFS16 : I<0xa0, RawFrm, (outs), (ins), "push{w}\t{%fs|fs}", []>,
                 OpSize16, TB;
def PUSHFS32 : I<0xa0, RawFrm, (outs), (ins), "push{l}\t{%fs|fs}", []>, TB,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHGS16 : I<0xa8, RawFrm, (outs), (ins), "push{w}\t{%gs|gs}", []>,
                 OpSize16, TB;
def PUSHGS32 : I<0xa8, RawFrm, (outs), (ins), "push{l}\t{%gs|gs}", []>, TB,
                 OpSize32, Requires<[Not64BitMode]>;
def PUSHFS64 : I<0xa0, RawFrm, (outs), (ins), "push{q}\t{%fs|fs}", []>, TB,
                 OpSize32, Requires<[In64BitMode]>;
def PUSHGS64 : I<0xa8, RawFrm, (outs), (ins), "push{q}\t{%gs|gs}", []>, TB,
                 OpSize32, Requires<[In64BitMode]>;

// No "pop cs" instruction.
def POPSS16 : I<0x17, RawFrm, (outs), (ins), "pop{w}\t{%ss|ss}", []>,
              OpSize16, Requires<[Not64BitMode]>;
def POPSS32 : I<0x17, RawFrm, (outs), (ins), "pop{l}\t{%ss|ss}", []>,
              OpSize32, Requires<[Not64BitMode]>;

def POPDS16 : I<0x1F, RawFrm, (outs), (ins), "pop{w}\t{%ds|ds}", []>,
              OpSize16, Requires<[Not64BitMode]>;
def POPDS32 : I<0x1F, RawFrm, (outs), (ins), "pop{l}\t{%ds|ds}", []>,
              OpSize32, Requires<[Not64BitMode]>;

def POPES16 : I<0x07, RawFrm, (outs), (ins), "pop{w}\t{%es|es}", []>,
              OpSize16, Requires<[Not64BitMode]>;
def POPES32 : I<0x07, RawFrm, (outs), (ins), "pop{l}\t{%es|es}", []>,
              OpSize32, Requires<[Not64BitMode]>;

def POPFS16 : I<0xa1, RawFrm, (outs), (ins), "pop{w}\t{%fs|fs}", []>,
                OpSize16, TB;
def POPFS32 : I<0xa1, RawFrm, (outs), (ins), "pop{l}\t{%fs|fs}", []>, TB,
                OpSize32, Requires<[Not64BitMode]>;
def POPFS64 : I<0xa1, RawFrm, (outs), (ins), "pop{q}\t{%fs|fs}", []>, TB,
                OpSize32, Requires<[In64BitMode]>;

def POPGS16 : I<0xa9, RawFrm, (outs), (ins), "pop{w}\t{%gs|gs}", []>,
                OpSize16, TB;
def POPGS32 : I<0xa9, RawFrm, (outs), (ins), "pop{l}\t{%gs|gs}", []>, TB,
                OpSize32, Requires<[Not64BitMode]>;
def POPGS64 : I<0xa9, RawFrm, (outs), (ins), "pop{q}\t{%gs|gs}", []>, TB,
                OpSize32, Requires<[In64BitMode]>;

def LDS16rm : I<0xc5, MRMSrcMem, (outs GR16:$dst), (ins opaquemem:$src),
                "lds{w}\t{$src, $dst|$dst, $src}", []>, OpSize16,
                Requires<[Not64BitMode]>;
def LDS32rm : I<0xc5, MRMSrcMem, (outs GR32:$dst), (ins opaquemem:$src),
                "lds{l}\t{$src, $dst|$dst, $src}", []>, OpSize32,
                Requires<[Not64BitMode]>;

def LSS16rm : I<0xb2, MRMSrcMem, (outs GR16:$dst), (ins opaquemem:$src),
                "lss{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16;
def LSS32rm : I<0xb2, MRMSrcMem, (outs GR32:$dst), (ins opaquemem:$src),
                "lss{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32;
def LSS64rm : RI<0xb2, MRMSrcMem, (outs GR64:$dst), (ins opaquemem:$src),
                 "lss{q}\t{$src, $dst|$dst, $src}", []>, TB;

def LES16rm : I<0xc4, MRMSrcMem, (outs GR16:$dst), (ins opaquemem:$src),
                "les{w}\t{$src, $dst|$dst, $src}", []>, OpSize16,
                Requires<[Not64BitMode]>;
def LES32rm : I<0xc4, MRMSrcMem, (outs GR32:$dst), (ins opaquemem:$src),
                "les{l}\t{$src, $dst|$dst, $src}", []>, OpSize32,
                Requires<[Not64BitMode]>;

def LFS16rm : I<0xb4, MRMSrcMem, (outs GR16:$dst), (ins opaquemem:$src),
                "lfs{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16;
def LFS32rm : I<0xb4, MRMSrcMem, (outs GR32:$dst), (ins opaquemem:$src),
                "lfs{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32;
def LFS64rm : RI<0xb4, MRMSrcMem, (outs GR64:$dst), (ins opaquemem:$src),
                 "lfs{q}\t{$src, $dst|$dst, $src}", []>, TB;

def LGS16rm : I<0xb5, MRMSrcMem, (outs GR16:$dst), (ins opaquemem:$src),
                "lgs{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16;
def LGS32rm : I<0xb5, MRMSrcMem, (outs GR32:$dst), (ins opaquemem:$src),
                "lgs{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32;

def LGS64rm : RI<0xb5, MRMSrcMem, (outs GR64:$dst), (ins opaquemem:$src),
                 "lgs{q}\t{$src, $dst|$dst, $src}", []>, TB;

def VERRr : I<0x00, MRM4r, (outs), (ins GR16:$seg), "verr\t$seg", []>, TB, NotMemoryFoldable;
def VERWr : I<0x00, MRM5r, (outs), (ins GR16:$seg), "verw\t$seg", []>, TB, NotMemoryFoldable;
let mayLoad = 1 in {
def VERRm : I<0x00, MRM4m, (outs), (ins i16mem:$seg), "verr\t$seg", []>, TB, NotMemoryFoldable;
def VERWm : I<0x00, MRM5m, (outs), (ins i16mem:$seg), "verw\t$seg", []>, TB, NotMemoryFoldable;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Descriptor-table support instructions

let SchedRW = [WriteSystem] in {
def SGDT16m : I<0x01, MRM0m, (outs), (ins opaquemem:$dst),
                "sgdtw\t$dst", []>, TB, OpSize16, Requires<[Not64BitMode]>;
def SGDT32m : I<0x01, MRM0m, (outs), (ins opaquemem:$dst),
                "sgdt{l|d}\t$dst", []>, OpSize32, TB, Requires <[Not64BitMode]>;
def SGDT64m : I<0x01, MRM0m, (outs), (ins opaquemem:$dst),
                "sgdt{q}\t$dst", []>, TB, Requires <[In64BitMode]>;
def SIDT16m : I<0x01, MRM1m, (outs), (ins opaquemem:$dst),
                "sidtw\t$dst", []>, TB, OpSize16, Requires<[Not64BitMode]>;
def SIDT32m : I<0x01, MRM1m, (outs), (ins opaquemem:$dst),
                "sidt{l|d}\t$dst", []>, OpSize32, TB, Requires <[Not64BitMode]>;
def SIDT64m : I<0x01, MRM1m, (outs), (ins opaquemem:$dst),
                "sidt{q}\t$dst", []>, TB, Requires <[In64BitMode]>;
def SLDT16r : I<0x00, MRM0r, (outs GR16:$dst), (ins),
                "sldt{w}\t$dst", []>, TB, OpSize16;
let mayStore = 1 in
def SLDT16m : I<0x00, MRM0m, (outs), (ins i16mem:$dst),
                "sldt{w}\t$dst", []>, TB;
def SLDT32r : I<0x00, MRM0r, (outs GR32:$dst), (ins),
                "sldt{l}\t$dst", []>, OpSize32, TB;

// LLDT is not interpreted specially in 64-bit mode because there is no sign
//   extension.
def SLDT64r : RI<0x00, MRM0r, (outs GR64:$dst), (ins),
                 "sldt{q}\t$dst", []>, TB, Requires<[In64BitMode]>;

def LGDT16m : I<0x01, MRM2m, (outs), (ins opaquemem:$src),
                "lgdtw\t$src", []>, TB, OpSize16, Requires<[Not64BitMode]>;
def LGDT32m : I<0x01, MRM2m, (outs), (ins opaquemem:$src),
                "lgdt{l|d}\t$src", []>, OpSize32, TB, Requires<[Not64BitMode]>;
def LGDT64m : I<0x01, MRM2m, (outs), (ins opaquemem:$src),
                "lgdt{q}\t$src", []>, TB, Requires<[In64BitMode]>;
def LIDT16m : I<0x01, MRM3m, (outs), (ins opaquemem:$src),
                "lidtw\t$src", []>, TB, OpSize16, Requires<[Not64BitMode]>;
def LIDT32m : I<0x01, MRM3m, (outs), (ins opaquemem:$src),
                "lidt{l|d}\t$src", []>, OpSize32, TB, Requires<[Not64BitMode]>;
def LIDT64m : I<0x01, MRM3m, (outs), (ins opaquemem:$src),
                "lidt{q}\t$src", []>, TB, Requires<[In64BitMode]>;
def LLDT16r : I<0x00, MRM2r, (outs), (ins GR16:$src),
                "lldt{w}\t$src", []>, TB, NotMemoryFoldable;
let mayLoad = 1 in
def LLDT16m : I<0x00, MRM2m, (outs), (ins i16mem:$src),
                "lldt{w}\t$src", []>, TB, NotMemoryFoldable;
} // SchedRW

//===----------------------------------------------------------------------===//
// Specialized register support
let SchedRW = [WriteSystem] in {
let Uses = [EAX, ECX, EDX] in
def WRMSR : I<0x30, RawFrm, (outs), (ins), "wrmsr", []>, TB;
let Defs = [EAX, EDX], Uses = [ECX] in
def RDMSR : I<0x32, RawFrm, (outs), (ins), "rdmsr", []>, TB;

let Defs = [RAX, RDX], Uses = [ECX] in
  def RDPMC : I<0x33, RawFrm, (outs), (ins), "rdpmc", [(X86rdpmc)]>, TB;

def SMSW16r : I<0x01, MRM4r, (outs GR16:$dst), (ins),
                "smsw{w}\t$dst", []>, OpSize16, TB;
def SMSW32r : I<0x01, MRM4r, (outs GR32:$dst), (ins),
                "smsw{l}\t$dst", []>, OpSize32, TB;
// no m form encodable; use SMSW16m
def SMSW64r : RI<0x01, MRM4r, (outs GR64:$dst), (ins),
                 "smsw{q}\t$dst", []>, TB;

// For memory operands, there is only a 16-bit form
def SMSW16m : I<0x01, MRM4m, (outs), (ins i16mem:$dst),
                "smsw{w}\t$dst", []>, TB;

def LMSW16r : I<0x01, MRM6r, (outs), (ins GR16:$src),
                "lmsw{w}\t$src", []>, TB, NotMemoryFoldable;
let mayLoad = 1 in
def LMSW16m : I<0x01, MRM6m, (outs), (ins i16mem:$src),
                "lmsw{w}\t$src", []>, TB, NotMemoryFoldable;

let Defs = [EAX, EBX, ECX, EDX], Uses = [EAX, ECX] in
  def CPUID : I<0xA2, RawFrm, (outs), (ins), "cpuid", []>, TB;
} // SchedRW

//===----------------------------------------------------------------------===//
// Cache instructions
let SchedRW = [WriteSystem] in {
def INVD : I<0x08, RawFrm, (outs), (ins), "invd", []>, TB;
def WBINVD : I<0x09, RawFrm, (outs), (ins), "wbinvd", [(int_x86_wbinvd)]>, TB;

// wbnoinvd is like wbinvd, except without invalidation
// encoding: like wbinvd + an 0xF3 prefix
def WBNOINVD : I<0x09, RawFrm, (outs), (ins), "wbnoinvd",
                 [(int_x86_wbnoinvd)]>, XS,
                 Requires<[HasWBNOINVD]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// CET instructions
// Use with caution, availability is not predicated on features.
let SchedRW = [WriteSystem] in {
  let Uses = [SSP] in {
    let Defs = [SSP] in {
      def INCSSPD : I<0xAE, MRM5r, (outs), (ins GR32:$src), "incsspd\t$src",
                       [(int_x86_incsspd GR32:$src)]>, XS;
      def INCSSPQ : RI<0xAE, MRM5r, (outs), (ins GR64:$src), "incsspq\t$src",
                       [(int_x86_incsspq GR64:$src)]>, XS;
    } // Defs SSP

    let Constraints = "$src = $dst" in {
      def RDSSPD : I<0x1E, MRM1r, (outs GR32:$dst), (ins GR32:$src),
                     "rdsspd\t$dst",
                     [(set GR32:$dst, (int_x86_rdsspd GR32:$src))]>, XS;
      def RDSSPQ : RI<0x1E, MRM1r, (outs GR64:$dst), (ins GR64:$src),
                     "rdsspq\t$dst",
                     [(set GR64:$dst, (int_x86_rdsspq GR64:$src))]>, XS;
    }

    let Defs = [SSP] in {
      def SAVEPREVSSP : I<0x01, MRM_EA, (outs), (ins), "saveprevssp",
                       [(int_x86_saveprevssp)]>, XS;
      def RSTORSSP : I<0x01, MRM5m, (outs), (ins i32mem:$src),
                       "rstorssp\t$src",
                       [(int_x86_rstorssp addr:$src)]>, XS;
    } // Defs SSP
  } // Uses SSP

  def WRSSD : I<0xF6, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                "wrssd\t{$src, $dst|$dst, $src}",
                [(int_x86_wrssd GR32:$src, addr:$dst)]>, T8PS;
  def WRSSQ : RI<0xF6, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                 "wrssq\t{$src, $dst|$dst, $src}",
                 [(int_x86_wrssq GR64:$src, addr:$dst)]>, T8PS;
  def WRUSSD : I<0xF5, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                 "wrussd\t{$src, $dst|$dst, $src}",
                 [(int_x86_wrussd GR32:$src, addr:$dst)]>, T8PD;
  def WRUSSQ : RI<0xF5, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                  "wrussq\t{$src, $dst|$dst, $src}",
                  [(int_x86_wrussq GR64:$src, addr:$dst)]>, T8PD;

  let Defs = [SSP] in {
    let Uses = [SSP] in {
        def SETSSBSY : I<0x01, MRM_E8, (outs), (ins), "setssbsy",
                         [(int_x86_setssbsy)]>, XS;
    } // Uses SSP

    def CLRSSBSY : I<0xAE, MRM6m, (outs), (ins i32mem:$src),
                     "clrssbsy\t$src",
                     [(int_x86_clrssbsy addr:$src)]>, XS;
  } // Defs SSP
} // SchedRW

let SchedRW = [WriteSystem] in {
    def ENDBR64 : I<0x1E, MRM_FA, (outs), (ins), "endbr64", []>, XS;
    def ENDBR32 : I<0x1E, MRM_FB, (outs), (ins), "endbr32", []>, XS;
} // SchedRW

//===----------------------------------------------------------------------===//
// XSAVE instructions
let SchedRW = [WriteSystem] in {
let Predicates = [HasXSAVE] in {
let Defs = [EDX, EAX], Uses = [ECX] in
  def XGETBV : I<0x01, MRM_D0, (outs), (ins), "xgetbv", []>, TB;

let Uses = [EDX, EAX, ECX] in
  def XSETBV : I<0x01, MRM_D1, (outs), (ins),
                "xsetbv",
                [(int_x86_xsetbv ECX, EDX, EAX)]>, TB;

} // HasXSAVE

let Uses = [EDX, EAX] in {
def XSAVE : I<0xAE, MRM4m, (outs), (ins opaquemem:$dst),
              "xsave\t$dst",
              [(int_x86_xsave addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVE]>;
def XSAVE64 : RI<0xAE, MRM4m, (outs), (ins opaquemem:$dst),
                 "xsave64\t$dst",
                 [(int_x86_xsave64 addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVE, In64BitMode]>;
def XRSTOR : I<0xAE, MRM5m, (outs), (ins opaquemem:$dst),
               "xrstor\t$dst",
               [(int_x86_xrstor addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVE]>;
def XRSTOR64 : RI<0xAE, MRM5m, (outs), (ins opaquemem:$dst),
                  "xrstor64\t$dst",
                  [(int_x86_xrstor64 addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVE, In64BitMode]>;
def XSAVEOPT : I<0xAE, MRM6m, (outs), (ins opaquemem:$dst),
                 "xsaveopt\t$dst",
                 [(int_x86_xsaveopt addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVEOPT]>;
def XSAVEOPT64 : RI<0xAE, MRM6m, (outs), (ins opaquemem:$dst),
                    "xsaveopt64\t$dst",
                    [(int_x86_xsaveopt64 addr:$dst, EDX, EAX)]>, PS, Requires<[HasXSAVEOPT, In64BitMode]>;
def XSAVEC : I<0xC7, MRM4m, (outs), (ins opaquemem:$dst),
               "xsavec\t$dst",
               [(int_x86_xsavec addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVEC]>;
def XSAVEC64 : RI<0xC7, MRM4m, (outs), (ins opaquemem:$dst),
                 "xsavec64\t$dst",
                 [(int_x86_xsavec64 addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVEC, In64BitMode]>;
def XSAVES : I<0xC7, MRM5m, (outs), (ins opaquemem:$dst),
               "xsaves\t$dst",
               [(int_x86_xsaves addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVES]>;
def XSAVES64 : RI<0xC7, MRM5m, (outs), (ins opaquemem:$dst),
                  "xsaves64\t$dst",
                  [(int_x86_xsaves64 addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVE, In64BitMode]>;
def XRSTORS : I<0xC7, MRM3m, (outs), (ins opaquemem:$dst),
                "xrstors\t$dst",
                [(int_x86_xrstors addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVES]>;
def XRSTORS64 : RI<0xC7, MRM3m, (outs), (ins opaquemem:$dst),
                   "xrstors64\t$dst",
                   [(int_x86_xrstors64 addr:$dst, EDX, EAX)]>, TB, Requires<[HasXSAVES, In64BitMode]>;
} // Uses
} // SchedRW

//===----------------------------------------------------------------------===//
// VIA PadLock crypto instructions
let Defs = [RAX, RDI], Uses = [RDX, RDI], SchedRW = [WriteSystem] in
  def XSTORE : I<0xa7, MRM_C0, (outs), (ins), "xstore", []>, TB;

def : InstAlias<"xstorerng", (XSTORE)>;

let SchedRW = [WriteSystem] in {
let Defs = [RSI, RDI], Uses = [RBX, RDX, RSI, RDI] in {
  def XCRYPTECB : I<0xa7, MRM_C8, (outs), (ins), "xcryptecb", []>, TB;
  def XCRYPTCBC : I<0xa7, MRM_D0, (outs), (ins), "xcryptcbc", []>, TB;
  def XCRYPTCTR : I<0xa7, MRM_D8, (outs), (ins), "xcryptctr", []>, TB;
  def XCRYPTCFB : I<0xa7, MRM_E0, (outs), (ins), "xcryptcfb", []>, TB;
  def XCRYPTOFB : I<0xa7, MRM_E8, (outs), (ins), "xcryptofb", []>, TB;
}

let Defs = [RAX, RSI, RDI], Uses = [RAX, RSI, RDI] in {
  def XSHA1 : I<0xa6, MRM_C8, (outs), (ins), "xsha1", []>, TB;
  def XSHA256 : I<0xa6, MRM_D0, (outs), (ins), "xsha256", []>, TB;
}
let Defs = [RAX, RDX, RSI], Uses = [RAX, RSI] in
  def MONTMUL : I<0xa6, MRM_C0, (outs), (ins), "montmul", []>, TB;
} // SchedRW

//==-----------------------------------------------------------------------===//
// PKU  - enable protection key
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
  def WRPKRU : PseudoI<(outs), (ins GR32:$src),
                [(int_x86_wrpkru GR32:$src)]>;
  def RDPKRU : PseudoI<(outs GR32:$dst), (ins),
                [(set GR32:$dst, (int_x86_rdpkru))]>;
}

let SchedRW = [WriteSystem] in {
let Defs = [EAX, EDX], Uses = [ECX] in
  def RDPKRUr : I<0x01, MRM_EE, (outs), (ins), "rdpkru", []>, TB;
let Uses = [EAX, ECX, EDX] in
  def WRPKRUr : I<0x01, MRM_EF, (outs), (ins), "wrpkru", []>, TB;
} // SchedRW

//===----------------------------------------------------------------------===//
// FS/GS Base Instructions
let Predicates = [HasFSGSBase, In64BitMode], SchedRW = [WriteSystem] in {
  def RDFSBASE : I<0xAE, MRM0r, (outs GR32:$dst), (ins),
                   "rdfsbase{l}\t$dst",
                   [(set GR32:$dst, (int_x86_rdfsbase_32))]>, XS;
  def RDFSBASE64 : RI<0xAE, MRM0r, (outs GR64:$dst), (ins),
                     "rdfsbase{q}\t$dst",
                     [(set GR64:$dst, (int_x86_rdfsbase_64))]>, XS;
  def RDGSBASE : I<0xAE, MRM1r, (outs GR32:$dst), (ins),
                   "rdgsbase{l}\t$dst",
                   [(set GR32:$dst, (int_x86_rdgsbase_32))]>, XS;
  def RDGSBASE64 : RI<0xAE, MRM1r, (outs GR64:$dst), (ins),
                     "rdgsbase{q}\t$dst",
                     [(set GR64:$dst, (int_x86_rdgsbase_64))]>, XS;
  def WRFSBASE : I<0xAE, MRM2r, (outs), (ins GR32:$src),
                   "wrfsbase{l}\t$src",
                   [(int_x86_wrfsbase_32 GR32:$src)]>, XS;
  def WRFSBASE64 : RI<0xAE, MRM2r, (outs), (ins GR64:$src),
                      "wrfsbase{q}\t$src",
                      [(int_x86_wrfsbase_64 GR64:$src)]>, XS;
  def WRGSBASE : I<0xAE, MRM3r, (outs), (ins GR32:$src),
                   "wrgsbase{l}\t$src",
                   [(int_x86_wrgsbase_32 GR32:$src)]>, XS;
  def WRGSBASE64 : RI<0xAE, MRM3r, (outs), (ins GR64:$src),
                      "wrgsbase{q}\t$src",
                      [(int_x86_wrgsbase_64 GR64:$src)]>, XS;
}

//===----------------------------------------------------------------------===//
// INVPCID Instruction
let SchedRW = [WriteSystem] in {
def INVPCID32 : I<0x82, MRMSrcMem, (outs), (ins GR32:$src1, i128mem:$src2),
                  "invpcid\t{$src2, $src1|$src1, $src2}",
                  [(int_x86_invpcid GR32:$src1, addr:$src2)]>, T8PD,
                  Requires<[Not64BitMode, HasINVPCID]>;
def INVPCID64 : I<0x82, MRMSrcMem, (outs), (ins GR64:$src1, i128mem:$src2),
                  "invpcid\t{$src2, $src1|$src1, $src2}", []>, T8PD,
                  Requires<[In64BitMode, HasINVPCID]>;
} // SchedRW

let Predicates = [In64BitMode, HasINVPCID] in {
  // The instruction can only use a 64 bit register as the register argument
  // in 64 bit mode, while the intrinsic only accepts a 32 bit argument
  // corresponding to it.
  // The accepted values for now are 0,1,2,3 anyways (see Intel SDM -- INVCPID
  // type),/ so it doesn't hurt us that one can't supply a 64 bit value here.
  def : Pat<(int_x86_invpcid GR32:$src1, addr:$src2),
            (INVPCID64
              (SUBREG_TO_REG (i64 0), (MOV32rr GR32:$src1), sub_32bit),
              addr:$src2)>;
}


//===----------------------------------------------------------------------===//
// SMAP Instruction
let Defs = [EFLAGS], SchedRW = [WriteSystem] in {
  def CLAC : I<0x01, MRM_CA, (outs), (ins), "clac", []>, TB;
  def STAC : I<0x01, MRM_CB, (outs), (ins), "stac", []>, TB;
}

//===----------------------------------------------------------------------===//
// SMX Instruction
let SchedRW = [WriteSystem] in {
let Uses = [RAX, RBX, RCX, RDX], Defs = [RAX, RBX, RCX] in {
  def GETSEC : I<0x37, RawFrm, (outs), (ins), "getsec", []>, TB;
} // Uses, Defs
} // SchedRW

//===----------------------------------------------------------------------===//
// TS flag control instruction.
let SchedRW = [WriteSystem] in {
def CLTS : I<0x06, RawFrm, (outs), (ins), "clts", []>, TB;
}

//===----------------------------------------------------------------------===//
// IF (inside EFLAGS) management instructions.
let SchedRW = [WriteSystem], Uses = [EFLAGS], Defs = [EFLAGS] in {
def CLI : I<0xFA, RawFrm, (outs), (ins), "cli", []>;
def STI : I<0xFB, RawFrm, (outs), (ins), "sti", []>;
}

//===----------------------------------------------------------------------===//
// RDPID Instruction
let SchedRW = [WriteSystem] in {
def RDPID32 : I<0xC7, MRM7r, (outs GR32:$dst), (ins),
                "rdpid\t$dst", [(set GR32:$dst, (int_x86_rdpid))]>, XS,
                Requires<[Not64BitMode, HasRDPID]>;
def RDPID64 : I<0xC7, MRM7r, (outs GR64:$dst), (ins), "rdpid\t$dst", []>, XS,
                Requires<[In64BitMode, HasRDPID]>;
} // SchedRW

let Predicates = [In64BitMode, HasRDPID] in {
  // Due to silly instruction definition, we have to compensate for the
  // instruction outputing a 64-bit register.
  def : Pat<(int_x86_rdpid),
            (EXTRACT_SUBREG (RDPID64), sub_32bit)>;
}


//===----------------------------------------------------------------------===//
// PTWRITE Instruction - Write Data to a Processor Trace Packet
let SchedRW = [WriteSystem] in {
def PTWRITEm: I<0xAE, MRM4m, (outs), (ins i32mem:$dst),
                "ptwrite{l}\t$dst", [(int_x86_ptwrite32 (loadi32 addr:$dst))]>, XS,
                Requires<[HasPTWRITE]>;
def PTWRITE64m : RI<0xAE, MRM4m, (outs), (ins i64mem:$dst),
                    "ptwrite{q}\t$dst", [(int_x86_ptwrite64 (loadi64 addr:$dst))]>, XS,
                    Requires<[In64BitMode, HasPTWRITE]>;

def PTWRITEr : I<0xAE, MRM4r, (outs), (ins GR32:$dst),
                 "ptwrite{l}\t$dst", [(int_x86_ptwrite32 GR32:$dst)]>, XS,
                    Requires<[HasPTWRITE]>;
def PTWRITE64r : RI<0xAE, MRM4r, (outs), (ins GR64:$dst),
                    "ptwrite{q}\t$dst", [(int_x86_ptwrite64 GR64:$dst)]>, XS,
                    Requires<[In64BitMode, HasPTWRITE]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Platform Configuration instruction

// From ISA docs:
//  "This instruction is used to execute functions for configuring platform
//   features.
//   EAX: Leaf function to be invoked.
//   RBX/RCX/RDX: Leaf-specific purpose."
//  "Successful execution of the leaf clears RAX (set to zero) and ZF, CF, PF,
//   AF, OF, and SF are cleared. In case of failure, the failure reason is
//   indicated in RAX with ZF set to 1 and CF, PF, AF, OF, and SF are cleared."
// Thus all these mentioned registers are considered clobbered.

let SchedRW = [WriteSystem] in {
let Uses = [RAX, RBX, RCX, RDX], Defs = [RAX, RBX, RCX, RDX, EFLAGS] in
    def PCONFIG : I<0x01, MRM_C5, (outs), (ins), "pconfig", []>, TB,
                  Requires<[HasPCONFIG]>;
} // SchedRW
