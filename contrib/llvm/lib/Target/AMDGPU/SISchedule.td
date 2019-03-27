//===-- SISchedule.td - SI Scheduling definitons -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MachineModel definitions for Southern Islands (SI)
//
//===----------------------------------------------------------------------===//

def : PredicateProlog<[{
  const SIInstrInfo *TII =
    static_cast<const SIInstrInfo*>(SchedModel->getInstrInfo());
  (void)TII;
}]>;

def WriteBranch : SchedWrite;
def WriteExport : SchedWrite;
def WriteLDS    : SchedWrite;
def WriteSALU   : SchedWrite;
def WriteSMEM   : SchedWrite;
def WriteVMEM   : SchedWrite;
def WriteBarrier : SchedWrite;

// Vector ALU instructions
def Write32Bit         : SchedWrite;
def WriteQuarterRate32 : SchedWrite;
def WriteFullOrQuarterRate32 : SchedWrite;

def WriteFloatFMA   : SchedWrite;

// Slow quarter rate f64 instruction.
def WriteDouble : SchedWrite;

// half rate f64 instruction (same as v_add_f64)
def WriteDoubleAdd  : SchedWrite;

// Half rate 64-bit instructions.
def Write64Bit : SchedWrite;

// FIXME: Should there be a class for instructions which are VALU
// instructions and have VALU rates, but write to the SALU (i.e. VOPC
// instructions)

class SISchedMachineModel : SchedMachineModel {
  let CompleteModel = 0;
  // MicroOpBufferSize = 1 means that instructions will always be added
  // the ready queue when they become available.  This exposes them
  // to the register pressure analysis.
  let MicroOpBufferSize = 1;
  let IssueWidth = 1;
  let PostRAScheduler = 1;

  // FIXME:Approximate 2 * branch cost.  Try to hack around bad
  // early-ifcvt heuristics. These need improvement to avoid the OOE
  // heuristics.
  int MispredictPenalty = 20;
}

def SIFullSpeedModel : SISchedMachineModel;
def SIQuarterSpeedModel : SISchedMachineModel;

// XXX: Are the resource counts correct?
def HWBranch : ProcResource<1> {
  let BufferSize = 1;
}
def HWExport : ProcResource<1> {
  let BufferSize = 7; // Taken from S_WAITCNT
}
def HWLGKM   : ProcResource<1> {
  let BufferSize = 31;  // Taken from S_WAITCNT
}
def HWSALU   : ProcResource<1> {
  let BufferSize = 1;
}
def HWVMEM   : ProcResource<1> {
  let BufferSize = 15;  // Taken from S_WAITCNT
}
def HWVALU   : ProcResource<1> {
  let BufferSize = 1;
}

class HWWriteRes<SchedWrite write, list<ProcResourceKind> resources,
                 int latency> : WriteRes<write, resources> {
  let Latency = latency;
}

class HWVALUWriteRes<SchedWrite write, int latency> :
  HWWriteRes<write, [HWVALU], latency>;


// The latency numbers are taken from AMD Accelerated Parallel Processing
// guide. They may not be accurate.

// The latency values are 1 / (operations / cycle) / 4.
multiclass SICommonWriteRes {

  def : HWWriteRes<WriteBranch,  [HWBranch], 8>;
  def : HWWriteRes<WriteExport,  [HWExport], 4>;
  def : HWWriteRes<WriteLDS,     [HWLGKM],   5>; // Can be between 2 and 64
  def : HWWriteRes<WriteSALU,    [HWSALU],   1>;
  def : HWWriteRes<WriteSMEM,    [HWLGKM],   5>;
  def : HWWriteRes<WriteVMEM,    [HWVMEM],   80>;
  def : HWWriteRes<WriteBarrier, [HWBranch], 500>; // XXX: Guessed ???

  def : HWVALUWriteRes<Write32Bit,         1>;
  def : HWVALUWriteRes<Write64Bit,         2>;
  def : HWVALUWriteRes<WriteQuarterRate32, 4>;
}

def PredIsVGPR32Copy : SchedPredicate<[{TII->isVGPRCopy(*MI) && TII->getOpSize(*MI, 0) <= 32}]>;
def PredIsVGPR64Copy : SchedPredicate<[{TII->isVGPRCopy(*MI) && TII->getOpSize(*MI, 0) > 32}]>;
def WriteCopy : SchedWriteVariant<[
    SchedVar<PredIsVGPR32Copy, [Write32Bit]>,
    SchedVar<PredIsVGPR64Copy, [Write64Bit]>,
    SchedVar<NoSchedPred, [WriteSALU]>]>;

let SchedModel = SIFullSpeedModel in {

defm : SICommonWriteRes;

def : HWVALUWriteRes<WriteFloatFMA,   1>;
def : HWVALUWriteRes<WriteDouble,     4>;
def : HWVALUWriteRes<WriteDoubleAdd,  2>;

def : InstRW<[WriteCopy], (instrs COPY)>;

} // End SchedModel = SIFullSpeedModel

let SchedModel = SIQuarterSpeedModel in {

defm : SICommonWriteRes;

def : HWVALUWriteRes<WriteFloatFMA, 16>;
def : HWVALUWriteRes<WriteDouble,   16>;
def : HWVALUWriteRes<WriteDoubleAdd, 8>;

def : InstRW<[WriteCopy], (instrs COPY)>;

}  // End SchedModel = SIQuarterSpeedModel
