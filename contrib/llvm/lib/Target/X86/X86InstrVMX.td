//===-- X86InstrVMX.td - VMX Instruction Set Extension -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel VMX instruction
// set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VMX instructions

let SchedRW = [WriteSystem] in {
// 66 0F 38 80
def INVEPT32 : I<0x80, MRMSrcMem, (outs), (ins GR32:$src1, i128mem:$src2),
               "invept\t{$src2, $src1|$src1, $src2}", []>, T8PD,
               Requires<[Not64BitMode]>;
def INVEPT64 : I<0x80, MRMSrcMem, (outs), (ins GR64:$src1, i128mem:$src2),
               "invept\t{$src2, $src1|$src1, $src2}", []>, T8PD,
               Requires<[In64BitMode]>;

// 66 0F 38 81
def INVVPID32 : I<0x81, MRMSrcMem, (outs), (ins GR32:$src1, i128mem:$src2),
                "invvpid\t{$src2, $src1|$src1, $src2}", []>, T8PD,
                Requires<[Not64BitMode]>;
def INVVPID64 : I<0x81, MRMSrcMem, (outs), (ins GR64:$src1, i128mem:$src2),
                "invvpid\t{$src2, $src1|$src1, $src2}", []>, T8PD,
                Requires<[In64BitMode]>;

// 0F 01 C1
def VMCALL : I<0x01, MRM_C1, (outs), (ins), "vmcall", []>, TB;
def VMCLEARm : I<0xC7, MRM6m, (outs), (ins i64mem:$vmcs),
  "vmclear\t$vmcs", []>, PD;

// OF 01 D4
def VMFUNC : I<0x01, MRM_D4, (outs), (ins), "vmfunc", []>, TB;

// 0F 01 C2
def VMLAUNCH : I<0x01, MRM_C2, (outs), (ins), "vmlaunch", []>, TB;

// 0F 01 C3
def VMRESUME : I<0x01, MRM_C3, (outs), (ins), "vmresume", []>, TB;
def VMPTRLDm : I<0xC7, MRM6m, (outs), (ins i64mem:$vmcs),
  "vmptrld\t$vmcs", []>, PS;
def VMPTRSTm : I<0xC7, MRM7m, (outs), (ins i64mem:$vmcs),
  "vmptrst\t$vmcs", []>, PS;
def VMREAD64rr : I<0x78, MRMDestReg, (outs GR64:$dst), (ins GR64:$src),
  "vmread{q}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[In64BitMode]>,
  NotMemoryFoldable;
def VMREAD32rr : I<0x78, MRMDestReg, (outs GR32:$dst), (ins GR32:$src),
  "vmread{l}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[Not64BitMode]>,
  NotMemoryFoldable;

let mayStore = 1 in {
def VMREAD64mr : I<0x78, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
  "vmread{q}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[In64BitMode]>,
  NotMemoryFoldable;
def VMREAD32mr : I<0x78, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
  "vmread{l}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[Not64BitMode]>,
  NotMemoryFoldable;
} // mayStore

def VMWRITE64rr : I<0x79, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
  "vmwrite{q}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[In64BitMode]>,
  NotMemoryFoldable;
def VMWRITE32rr : I<0x79, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
  "vmwrite{l}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[Not64BitMode]>,
  NotMemoryFoldable;

let mayLoad = 1 in {
def VMWRITE64rm : I<0x79, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
  "vmwrite{q}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[In64BitMode]>,
  NotMemoryFoldable;
def VMWRITE32rm : I<0x79, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
  "vmwrite{l}\t{$src, $dst|$dst, $src}", []>, PS, Requires<[Not64BitMode]>,
  NotMemoryFoldable;
} // mayLoad

// 0F 01 C4
def VMXOFF : I<0x01, MRM_C4, (outs), (ins), "vmxoff", []>, TB;
def VMXON : I<0xC7, MRM6m, (outs), (ins i64mem:$vmxon),
  "vmxon\t$vmxon", []>, XS;
} // SchedRW
