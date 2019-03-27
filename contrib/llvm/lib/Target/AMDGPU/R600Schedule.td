//===-- R600Schedule.td - R600 Scheduling definitions ------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// R600 has a VLIW architecture.  On pre-cayman cards there are 5 instruction
// slots ALU.X, ALU.Y, ALU.Z, ALU.W, and TRANS.  For cayman cards, the TRANS
// slot has been removed.
//
//===----------------------------------------------------------------------===//


def ALU_X : FuncUnit;
def ALU_Y : FuncUnit;
def ALU_Z : FuncUnit;
def ALU_W : FuncUnit;
def TRANS : FuncUnit;

def AnyALU : InstrItinClass;
def VecALU : InstrItinClass;
def TransALU : InstrItinClass;
def XALU : InstrItinClass;

def R600_VLIW5_Itin : ProcessorItineraries <
  [ALU_X, ALU_Y, ALU_Z, ALU_W, TRANS, ALU_NULL],
  [],
  [
    InstrItinData<AnyALU, [InstrStage<1, [ALU_X, ALU_Y, ALU_Z, ALU_W, TRANS]>]>,
    InstrItinData<VecALU, [InstrStage<1, [ALU_X, ALU_Y, ALU_Z, ALU_W]>]>,
    InstrItinData<TransALU, [InstrStage<1, [TRANS]>]>,
    InstrItinData<XALU, [InstrStage<1, [ALU_X]>]>,
    InstrItinData<NullALU, [InstrStage<1, [ALU_NULL]>]>
  ]
>;

def R600_VLIW4_Itin : ProcessorItineraries <
  [ALU_X, ALU_Y, ALU_Z, ALU_W, ALU_NULL],
  [],
  [
    InstrItinData<AnyALU, [InstrStage<1, [ALU_X, ALU_Y, ALU_Z, ALU_W]>]>,
    InstrItinData<VecALU, [InstrStage<1, [ALU_X, ALU_Y, ALU_Z, ALU_W]>]>,
    InstrItinData<TransALU, [InstrStage<1, [ALU_NULL]>]>,
    InstrItinData<NullALU, [InstrStage<1, [ALU_NULL]>]>
  ]
>;
