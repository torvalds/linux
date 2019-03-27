//===-- XCore.td - Describe the XCore Target Machine -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the top level entry point for the XCore target.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Descriptions
//===----------------------------------------------------------------------===//

include "XCoreRegisterInfo.td"
include "XCoreInstrInfo.td"
include "XCoreCallingConv.td"

def XCoreInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// XCore processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"generic",      []>;
def : Proc<"xs1b-generic", []>;

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def XCore : Target {
  // Pull in Instruction Info:
  let InstructionSet = XCoreInstrInfo;
}
