//=-LanaiSchedule.td - Lanai Scheduling Definitions --*- tablegen -*-=========//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def ALU_FU  : FuncUnit;
def LDST_FU : FuncUnit;

def IIC_ALU  : InstrItinClass;
def IIC_LD   : InstrItinClass;
def IIC_ST   : InstrItinClass;
def IIC_LDSW : InstrItinClass;
def IIC_STSW : InstrItinClass;

def LanaiItinerary : ProcessorItineraries<[ALU_FU, LDST_FU],[],[
  InstrItinData<IIC_LD,   [InstrStage<1, [LDST_FU]>]>,
  InstrItinData<IIC_ST,   [InstrStage<1, [LDST_FU]>]>,
  InstrItinData<IIC_LDSW, [InstrStage<2, [LDST_FU]>]>,
  InstrItinData<IIC_STSW, [InstrStage<2, [LDST_FU]>]>,
  InstrItinData<IIC_ALU,  [InstrStage<1, [ALU_FU]>]>
]>;

def LanaiSchedModel : SchedMachineModel {
  // Cycles for loads to access the cache [default = -1]
  let LoadLatency = 2;

  // Max micro-ops that can be buffered for optimized loop dispatch/execution.
  // [default = -1]
  let LoopMicroOpBufferSize = 0;

  // Allow scheduler to assign default model to any unrecognized opcodes.
  // [default = 1]
  let CompleteModel = 0;

  // Max micro-ops that may be scheduled per cycle. [default = 1]
  let IssueWidth = 1;

  // Extra cycles for a mispredicted branch. [default = -1]
  let MispredictPenalty = 10;

  // Enable Post RegAlloc Scheduler pass. [default = 0]
  let PostRAScheduler = 0;

  // Max micro-ops that can be buffered. [default = -1]
  let MicroOpBufferSize = 0;

  // Per-cycle resources tables. [default = NoItineraries]
  let Itineraries = LanaiItinerary;
}

def ALU : ProcResource<1> { let BufferSize = 0; }
def LdSt : ProcResource<1> { let BufferSize = 0; }

def WriteLD   : SchedWrite;
def WriteST   : SchedWrite;
def WriteLDSW : SchedWrite;
def WriteSTSW : SchedWrite;
def WriteALU  : SchedWrite;

let SchedModel = LanaiSchedModel in {
  def : WriteRes<WriteLD, [LdSt]>   { let Latency = 2; }
  def : WriteRes<WriteST, [LdSt]>   { let Latency = 2; }
  def : WriteRes<WriteLDSW, [LdSt]> { let Latency = 2; }
  def : WriteRes<WriteSTSW, [LdSt]> { let Latency = 4; }
  def : WriteRes<WriteALU, [ALU]>   { let Latency = 1; }
}
