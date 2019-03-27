//===-- RISCVCallingConv.td - Calling Conventions RISCV ----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the RISCV architecture.
//
//===----------------------------------------------------------------------===//

// The RISC-V calling convention is handled with custom code in
// RISCVISelLowering.cpp (CC_RISCV).

def CSR : CalleeSavedRegs<(add X1, X3, X4, X8, X9, (sequence "X%u", 18, 27))>;

// Needed for implementation of RISCVRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// Interrupt handler needs to save/restore all registers that are used,
// both Caller and Callee saved registers.
def CSR_Interrupt : CalleeSavedRegs<(add X1,
    (sequence "X%u", 3, 9),
    (sequence "X%u", 10, 11),
    (sequence "X%u", 12, 17),
    (sequence "X%u", 18, 27),
    (sequence "X%u", 28, 31))>;

// Same as CSR_Interrupt, but including all 32-bit FP registers.
def CSR_XLEN_F32_Interrupt: CalleeSavedRegs<(add X1,
    (sequence "X%u", 3, 9),
    (sequence "X%u", 10, 11),
    (sequence "X%u", 12, 17),
    (sequence "X%u", 18, 27),
    (sequence "X%u", 28, 31),
    (sequence "F%u_32", 0, 7),
    (sequence "F%u_32", 10, 11),
    (sequence "F%u_32", 12, 17),
    (sequence "F%u_32", 28, 31),
    (sequence "F%u_32", 8, 9),
    (sequence "F%u_32", 18, 27))>;

// Same as CSR_Interrupt, but including all 64-bit FP registers.
def CSR_XLEN_F64_Interrupt: CalleeSavedRegs<(add X1,
    (sequence "X%u", 3, 9),
    (sequence "X%u", 10, 11),
    (sequence "X%u", 12, 17),
    (sequence "X%u", 18, 27),
    (sequence "X%u", 28, 31),
    (sequence "F%u_64", 0, 7),
    (sequence "F%u_64", 10, 11),
    (sequence "F%u_64", 12, 17),
    (sequence "F%u_64", 28, 31),
    (sequence "F%u_64", 8, 9),
    (sequence "F%u_64", 18, 27))>;
