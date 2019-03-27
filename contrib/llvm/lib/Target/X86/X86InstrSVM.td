//===-- X86InstrSVM.td - SVM Instruction Set Extension -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the AMD SVM instruction
// set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SVM instructions

let SchedRW = [WriteSystem] in {
// 0F 01 D9
def VMMCALL : I<0x01, MRM_D9, (outs), (ins), "vmmcall", []>, TB;

// 0F 01 DC
def STGI : I<0x01, MRM_DC, (outs), (ins), "stgi", []>, TB;

// 0F 01 DD
def CLGI : I<0x01, MRM_DD, (outs), (ins), "clgi", []>, TB;

// 0F 01 DE
let Uses = [EAX] in
def SKINIT : I<0x01, MRM_DE, (outs), (ins), "skinit\t{%eax|eax}", []>, TB;

// 0F 01 D8
let Uses = [EAX] in
def VMRUN32 : I<0x01, MRM_D8, (outs), (ins), "vmrun\t{%eax|eax}", []>, TB,
                Requires<[Not64BitMode]>;
let Uses = [RAX] in
def VMRUN64 : I<0x01, MRM_D8, (outs), (ins), "vmrun\t{%rax|rax}", []>, TB,
                Requires<[In64BitMode]>;

// 0F 01 DA
let Uses = [EAX] in
def VMLOAD32 : I<0x01, MRM_DA, (outs), (ins), "vmload\t{%eax|eax}", []>, TB,
                 Requires<[Not64BitMode]>;
let Uses = [RAX] in
def VMLOAD64 : I<0x01, MRM_DA, (outs), (ins), "vmload\t{%rax|rax}", []>, TB,
                 Requires<[In64BitMode]>;

// 0F 01 DB
let Uses = [EAX] in
def VMSAVE32 : I<0x01, MRM_DB, (outs), (ins), "vmsave\t{%eax|eax}", []>, TB,
                 Requires<[Not64BitMode]>;
let Uses = [RAX] in
def VMSAVE64 : I<0x01, MRM_DB, (outs), (ins), "vmsave\t{%rax|rax}", []>, TB,
                 Requires<[In64BitMode]>;

// 0F 01 DF
let Uses = [EAX, ECX] in
def INVLPGA32 : I<0x01, MRM_DF, (outs), (ins),
                "invlpga\t{%eax, %ecx|eax, ecx}", []>, TB, Requires<[Not64BitMode]>;
let Uses = [RAX, ECX] in
def INVLPGA64 : I<0x01, MRM_DF, (outs), (ins),
                "invlpga\t{%rax, %ecx|rax, ecx}", []>, TB, Requires<[In64BitMode]>;
} // SchedRW
