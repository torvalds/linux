//===- NVPTX.td - Describe the NVPTX Target Machine -----------*- tblgen -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the NVPTX target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "NVPTXRegisterInfo.td"
include "NVPTXInstrInfo.td"

//===----------------------------------------------------------------------===//
// Subtarget Features.
// - We use the SM version number instead of explicit feature table.
// - Need at least one feature to avoid generating zero sized array by
//   TableGen in NVPTXGenSubtarget.inc.
//===----------------------------------------------------------------------===//

// SM Versions
def SM20 : SubtargetFeature<"sm_20", "SmVersion", "20",
                            "Target SM 2.0">;
def SM21 : SubtargetFeature<"sm_21", "SmVersion", "21",
                            "Target SM 2.1">;
def SM30 : SubtargetFeature<"sm_30", "SmVersion", "30",
                            "Target SM 3.0">;
def SM32 : SubtargetFeature<"sm_32", "SmVersion", "32",
                            "Target SM 3.2">;
def SM35 : SubtargetFeature<"sm_35", "SmVersion", "35",
                            "Target SM 3.5">;
def SM37 : SubtargetFeature<"sm_37", "SmVersion", "37",
                            "Target SM 3.7">;
def SM50 : SubtargetFeature<"sm_50", "SmVersion", "50",
                            "Target SM 5.0">;
def SM52 : SubtargetFeature<"sm_52", "SmVersion", "52",
                            "Target SM 5.2">;
def SM53 : SubtargetFeature<"sm_53", "SmVersion", "53",
                            "Target SM 5.3">;
def SM60 : SubtargetFeature<"sm_60", "SmVersion", "60",
                             "Target SM 6.0">;
def SM61 : SubtargetFeature<"sm_61", "SmVersion", "61",
                             "Target SM 6.1">;
def SM62 : SubtargetFeature<"sm_62", "SmVersion", "62",
                             "Target SM 6.2">;
def SM70 : SubtargetFeature<"sm_70", "SmVersion", "70",
                             "Target SM 7.0">;
def SM72 : SubtargetFeature<"sm_72", "SmVersion", "72",
                             "Target SM 7.2">;
def SM75 : SubtargetFeature<"sm_75", "SmVersion", "75",
                             "Target SM 7.5">;

// PTX Versions
def PTX32 : SubtargetFeature<"ptx32", "PTXVersion", "32",
                             "Use PTX version 3.2">;
def PTX40 : SubtargetFeature<"ptx40", "PTXVersion", "40",
                             "Use PTX version 4.0">;
def PTX41 : SubtargetFeature<"ptx41", "PTXVersion", "41",
                             "Use PTX version 4.1">;
def PTX42 : SubtargetFeature<"ptx42", "PTXVersion", "42",
                             "Use PTX version 4.2">;
def PTX43 : SubtargetFeature<"ptx43", "PTXVersion", "43",
                             "Use PTX version 4.3">;
def PTX50 : SubtargetFeature<"ptx50", "PTXVersion", "50",
                             "Use PTX version 5.0">;
def PTX60 : SubtargetFeature<"ptx60", "PTXVersion", "60",
                             "Use PTX version 6.0">;
def PTX61 : SubtargetFeature<"ptx61", "PTXVersion", "61",
                             "Use PTX version 6.1">;
def PTX63 : SubtargetFeature<"ptx63", "PTXVersion", "63",
                             "Use PTX version 6.3">;

//===----------------------------------------------------------------------===//
// NVPTX supported processors.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"sm_20", [SM20]>;
def : Proc<"sm_21", [SM21]>;
def : Proc<"sm_30", [SM30]>;
def : Proc<"sm_32", [SM32, PTX40]>;
def : Proc<"sm_35", [SM35]>;
def : Proc<"sm_37", [SM37, PTX41]>;
def : Proc<"sm_50", [SM50, PTX40]>;
def : Proc<"sm_52", [SM52, PTX41]>;
def : Proc<"sm_53", [SM53, PTX42]>;
def : Proc<"sm_60", [SM60, PTX50]>;
def : Proc<"sm_61", [SM61, PTX50]>;
def : Proc<"sm_62", [SM62, PTX50]>;
def : Proc<"sm_70", [SM70, PTX60]>;
def : Proc<"sm_72", [SM72, PTX61]>;
def : Proc<"sm_75", [SM75, PTX63]>;

def NVPTXInstrInfo : InstrInfo {
}

def NVPTX : Target {
  let InstructionSet = NVPTXInstrInfo;
}
