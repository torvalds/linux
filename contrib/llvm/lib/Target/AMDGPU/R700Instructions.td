//===-- R700Instructions.td - R700 Instruction defs  -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TableGen definitions for instructions which are:
// - Available to R700 and newer VLIW4/VLIW5 GPUs
// - Available only on R700 family GPUs.
//
//===----------------------------------------------------------------------===//

def isR700 : Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::R700">;

let Predicates = [isR700] in {
  def SIN_r700 : SIN_Common<0x6E>;
  def COS_r700 : COS_Common<0x6F>;
}
