//===-- BPFISelLowering.cpp - BPF DAG Lowering Implementation  ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that BPF uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "BPFISelLowering.h"
#include "BPF.h"
#include "BPFSubtarget.h"
#include "BPFTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "bpf-lower"

static cl::opt<bool> BPFExpandMemcpyInOrder("bpf-expand-memcpy-in-order",
  cl::Hidden, cl::init(false),
  cl::desc("Expand memcpy into load/store pairs in order"));

static void fail(const SDLoc &DL, SelectionDAG &DAG, const Twine &Msg) {
  MachineFunction &MF = DAG.getMachineFunction();
  DAG.getContext()->diagnose(
      DiagnosticInfoUnsupported(MF.getFunction(), Msg, DL.getDebugLoc()));
}

static void fail(const SDLoc &DL, SelectionDAG &DAG, const char *Msg,
                 SDValue Val) {
  MachineFunction &MF = DAG.getMachineFunction();
  std::string Str;
  raw_string_ostream OS(Str);
  OS << Msg;
  Val->print(OS);
  OS.flush();
  DAG.getContext()->diagnose(
      DiagnosticInfoUnsupported(MF.getFunction(), Str, DL.getDebugLoc()));
}

BPFTargetLowering::BPFTargetLowering(const TargetMachine &TM,
                                     const BPFSubtarget &STI)
    : TargetLowering(TM) {

  // Set up the register classes.
  addRegisterClass(MVT::i64, &BPF::GPRRegClass);
  if (STI.getHasAlu32())
    addRegisterClass(MVT::i32, &BPF::GPR32RegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(BPF::R11);

  setOperationAction(ISD::BR_CC, MVT::i64, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  for (auto VT : { MVT::i32, MVT::i64 }) {
    if (VT == MVT::i32 && !STI.getHasAlu32())
      continue;

    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::SHL_PARTS, VT, Expand);
    setOperationAction(ISD::SRL_PARTS, VT, Expand);
    setOperationAction(ISD::SRA_PARTS, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);

    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }

  if (STI.getHasAlu32()) {
    setOperationAction(ISD::BSWAP, MVT::i32, Promote);
    setOperationAction(ISD::BR_CC, MVT::i32, Promote);
  }

  setOperationAction(ISD::CTTZ, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ, MVT::i64, Custom);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i64, Custom);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Expand);

  // Extended load operations for i1 types must be promoted
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);

    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i16, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i32, Expand);
  }

  setBooleanContents(ZeroOrOneBooleanContent);

  // Function alignments (log2)
  setMinFunctionAlignment(3);
  setPrefFunctionAlignment(3);

  if (BPFExpandMemcpyInOrder) {
    // LLVM generic code will try to expand memcpy into load/store pairs at this
    // stage which is before quite a few IR optimization passes, therefore the
    // loads and stores could potentially be moved apart from each other which
    // will cause trouble to memcpy pattern matcher inside kernel eBPF JIT
    // compilers.
    //
    // When -bpf-expand-memcpy-in-order specified, we want to defer the expand
    // of memcpy to later stage in IR optimization pipeline so those load/store
    // pairs won't be touched and could be kept in order. Hence, we set
    // MaxStoresPerMem* to zero to disable the generic getMemcpyLoadsAndStores
    // code path, and ask LLVM to use target expander EmitTargetCodeForMemcpy.
    MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 0;
    MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 0;
    MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 0;
  } else {
    // inline memcpy() for kernel to see explicit copy
    unsigned CommonMaxStores =
      STI.getSelectionDAGInfo()->getCommonMaxStoresPerMemFunc();

    MaxStoresPerMemset = MaxStoresPerMemsetOptSize = CommonMaxStores;
    MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = CommonMaxStores;
    MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = CommonMaxStores;
  }

  // CPU/Feature control
  HasAlu32 = STI.getHasAlu32();
  HasJmpExt = STI.getHasJmpExt();
}

bool BPFTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  return false;
}

std::pair<unsigned, const TargetRegisterClass *>
BPFTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                StringRef Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1)
    // GCC Constraint Letters
    switch (Constraint[0]) {
    case 'r': // GENERAL_REGS
      return std::make_pair(0U, &BPF::GPRRegClass);
    default:
      break;
    }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

SDValue BPFTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  default:
    llvm_unreachable("unimplemented operand");
  }
}

// Calling Convention Implementation
#include "BPFGenCallingConv.inc"

SDValue BPFTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    break;
  }

  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, getHasAlu32() ? CC_BPF32 : CC_BPF64);

  for (auto &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      // Arguments passed in registers
      EVT RegVT = VA.getLocVT();
      MVT::SimpleValueType SimpleTy = RegVT.getSimpleVT().SimpleTy;
      switch (SimpleTy) {
      default: {
        errs() << "LowerFormalArguments Unhandled argument type: "
               << RegVT.getEVTString() << '\n';
        llvm_unreachable(0);
      }
      case MVT::i32:
      case MVT::i64:
        unsigned VReg = RegInfo.createVirtualRegister(SimpleTy == MVT::i64 ?
                                                      &BPF::GPRRegClass :
                                                      &BPF::GPR32RegClass);
        RegInfo.addLiveIn(VA.getLocReg(), VReg);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, RegVT);

        // If this is an value that has been promoted to wider types, insert an
        // assert[sz]ext to capture this, then truncate to the right size.
        if (VA.getLocInfo() == CCValAssign::SExt)
          ArgValue = DAG.getNode(ISD::AssertSext, DL, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          ArgValue = DAG.getNode(ISD::AssertZext, DL, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));

        if (VA.getLocInfo() != CCValAssign::Full)
          ArgValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgValue);

        InVals.push_back(ArgValue);

	break;
      }
    } else {
      fail(DL, DAG, "defined with too many args");
      InVals.push_back(DAG.getConstant(0, DL, VA.getLocVT()));
    }
  }

  if (IsVarArg || MF.getFunction().hasStructRetAttr()) {
    fail(DL, DAG, "functions with VarArgs or StructRet are not supported");
  }

  return Chain;
}

const unsigned BPFTargetLowering::MaxArgs = 5;

SDValue BPFTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  auto &Outs = CLI.Outs;
  auto &OutVals = CLI.OutVals;
  auto &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();

  // BPF target does not support tail call optimization.
  IsTailCall = false;

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::Fast:
  case CallingConv::C:
    break;
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeCallOperands(Outs, getHasAlu32() ? CC_BPF32 : CC_BPF64);

  unsigned NumBytes = CCInfo.getNextStackOffset();

  if (Outs.size() > MaxArgs)
    fail(CLI.DL, DAG, "too many args to ", Callee);

  for (auto &Arg : Outs) {
    ISD::ArgFlagsTy Flags = Arg.Flags;
    if (!Flags.isByVal())
      continue;

    fail(CLI.DL, DAG, "pass by value not supported ", Callee);
  }

  auto PtrVT = getPointerTy(MF.getDataLayout());
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);

  SmallVector<std::pair<unsigned, SDValue>, MaxArgs> RegsToPass;

  // Walk arg assignments
  for (unsigned i = 0,
                e = std::min(static_cast<unsigned>(ArgLocs.size()), MaxArgs);
       i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    }

    // Push arguments into RegsToPass vector
    if (VA.isRegLoc())
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    else
      llvm_unreachable("call arg pass bug");
  }

  SDValue InFlag;

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InFlag in
  // necessary since all emitted instructions must be stuck together.
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, CLI.DL, Reg.first, Reg.second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), CLI.DL, PtrVT,
                                        G->getOffset(), 0);
  } else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, 0);
    fail(CLI.DL, DAG, Twine("A call to built-in function '"
                            + StringRef(E->getSymbol())
                            + "' is not supported."));
  }

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain = DAG.getNode(BPFISD::CALL, CLI.DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(
      Chain, DAG.getConstant(NumBytes, CLI.DL, PtrVT, true),
      DAG.getConstant(0, CLI.DL, PtrVT, true), InFlag, CLI.DL);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, IsVarArg, Ins, CLI.DL, DAG,
                         InVals);
}

SDValue
BPFTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &DL, SelectionDAG &DAG) const {
  unsigned Opc = BPFISD::RET_FLAG;

  // CCValAssign - represent the assignment of the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;
  MachineFunction &MF = DAG.getMachineFunction();

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());

  if (MF.getFunction().getReturnType()->isAggregateType()) {
    fail(DL, DAG, "only integer returns supported");
    return DAG.getNode(Opc, DL, MVT::Other, Chain);
  }

  // Analize return values.
  CCInfo.AnalyzeReturn(Outs, getHasAlu32() ? RetCC_BPF32 : RetCC_BPF64);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Flag);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain; // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(Opc, DL, MVT::Other, RetOps);
}

SDValue BPFTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());

  if (Ins.size() >= 2) {
    fail(DL, DAG, "only small returns supported");
    for (unsigned i = 0, e = Ins.size(); i != e; ++i)
      InVals.push_back(DAG.getConstant(0, DL, Ins[i].VT));
    return DAG.getCopyFromReg(Chain, DL, 1, Ins[0].VT, InFlag).getValue(1);
  }

  CCInfo.AnalyzeCallResult(Ins, getHasAlu32() ? RetCC_BPF32 : RetCC_BPF64);

  // Copy all of the result registers out of their specified physreg.
  for (auto &Val : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, DL, Val.getLocReg(),
                               Val.getValVT(), InFlag).getValue(1);
    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

static void NegateCC(SDValue &LHS, SDValue &RHS, ISD::CondCode &CC) {
  switch (CC) {
  default:
    break;
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETLT:
  case ISD::SETLE:
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
    break;
  }
}

SDValue BPFTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  if (!getHasJmpExt())
    NegateCC(LHS, RHS, CC);

  return DAG.getNode(BPFISD::BR_CC, DL, Op.getValueType(), Chain, LHS, RHS,
                     DAG.getConstant(CC, DL, MVT::i64), Dest);
}

SDValue BPFTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  if (!getHasJmpExt())
    NegateCC(LHS, RHS, CC);

  SDValue TargetCC = DAG.getConstant(CC, DL, LHS.getValueType());
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};

  return DAG.getNode(BPFISD::SELECT_CC, DL, VTs, Ops);
}

const char *BPFTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((BPFISD::NodeType)Opcode) {
  case BPFISD::FIRST_NUMBER:
    break;
  case BPFISD::RET_FLAG:
    return "BPFISD::RET_FLAG";
  case BPFISD::CALL:
    return "BPFISD::CALL";
  case BPFISD::SELECT_CC:
    return "BPFISD::SELECT_CC";
  case BPFISD::BR_CC:
    return "BPFISD::BR_CC";
  case BPFISD::Wrapper:
    return "BPFISD::Wrapper";
  case BPFISD::MEMCPY:
    return "BPFISD::MEMCPY";
  }
  return nullptr;
}

SDValue BPFTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  auto N = cast<GlobalAddressSDNode>(Op);
  assert(N->getOffset() == 0 && "Invalid offset for global address");

  SDLoc DL(Op);
  const GlobalValue *GV = N->getGlobal();
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i64);

  return DAG.getNode(BPFISD::Wrapper, DL, MVT::i64, GA);
}

unsigned
BPFTargetLowering::EmitSubregExt(MachineInstr &MI, MachineBasicBlock *BB,
                                 unsigned Reg, bool isSigned) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  const TargetRegisterClass *RC = getRegClassFor(MVT::i64);
  int RShiftOp = isSigned ? BPF::SRA_ri : BPF::SRL_ri;
  MachineFunction *F = BB->getParent();
  DebugLoc DL = MI.getDebugLoc();

  MachineRegisterInfo &RegInfo = F->getRegInfo();
  unsigned PromotedReg0 = RegInfo.createVirtualRegister(RC);
  unsigned PromotedReg1 = RegInfo.createVirtualRegister(RC);
  unsigned PromotedReg2 = RegInfo.createVirtualRegister(RC);
  BuildMI(BB, DL, TII.get(BPF::MOV_32_64), PromotedReg0).addReg(Reg);
  BuildMI(BB, DL, TII.get(BPF::SLL_ri), PromotedReg1)
    .addReg(PromotedReg0).addImm(32);
  BuildMI(BB, DL, TII.get(RShiftOp), PromotedReg2)
    .addReg(PromotedReg1).addImm(32);

  return PromotedReg2;
}

MachineBasicBlock *
BPFTargetLowering::EmitInstrWithCustomInserterMemcpy(MachineInstr &MI,
                                                     MachineBasicBlock *BB)
                                                     const {
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineInstrBuilder MIB(*MF, MI);
  unsigned ScratchReg;

  // This function does custom insertion during lowering BPFISD::MEMCPY which
  // only has two register operands from memcpy semantics, the copy source
  // address and the copy destination address.
  //
  // Because we will expand BPFISD::MEMCPY into load/store pairs, we will need
  // a third scratch register to serve as the destination register of load and
  // source register of store.
  //
  // The scratch register here is with the Define | Dead | EarlyClobber flags.
  // The EarlyClobber flag has the semantic property that the operand it is
  // attached to is clobbered before the rest of the inputs are read. Hence it
  // must be unique among the operands to the instruction. The Define flag is
  // needed to coerce the machine verifier that an Undef value isn't a problem
  // as we anyway is loading memory into it. The Dead flag is needed as the
  // value in scratch isn't supposed to be used by any other instruction.
  ScratchReg = MRI.createVirtualRegister(&BPF::GPRRegClass);
  MIB.addReg(ScratchReg,
             RegState::Define | RegState::Dead | RegState::EarlyClobber);

  return BB;
}

MachineBasicBlock *
BPFTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();
  bool isSelectRROp = (Opc == BPF::Select ||
                       Opc == BPF::Select_64_32 ||
                       Opc == BPF::Select_32 ||
                       Opc == BPF::Select_32_64);

  bool isMemcpyOp = Opc == BPF::MEMCPY;

#ifndef NDEBUG
  bool isSelectRIOp = (Opc == BPF::Select_Ri ||
                       Opc == BPF::Select_Ri_64_32 ||
                       Opc == BPF::Select_Ri_32 ||
                       Opc == BPF::Select_Ri_32_64);


  assert((isSelectRROp || isSelectRIOp || isMemcpyOp) &&
         "Unexpected instr type to insert");
#endif

  if (isMemcpyOp)
    return EmitInstrWithCustomInserterMemcpy(MI, BB);

  bool is32BitCmp = (Opc == BPF::Select_32 ||
                     Opc == BPF::Select_32_64 ||
                     Opc == BPF::Select_Ri_32 ||
                     Opc == BPF::Select_Ri_32_64);

  // To "insert" a SELECT instruction, we actually have to insert the diamond
  // control-flow pattern.  The incoming instruction knows the destination vreg
  // to set, the condition code register to branch on, the true/false values to
  // select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  // ThisMBB:
  // ...
  //  TrueVal = ...
  //  jmp_XX r1, r2 goto Copy1MBB
  //  fallthrough --> Copy0MBB
  MachineBasicBlock *ThisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *Copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *Copy1MBB = F->CreateMachineBasicBlock(LLVM_BB);

  F->insert(I, Copy0MBB);
  F->insert(I, Copy1MBB);
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi node for the select.
  Copy1MBB->splice(Copy1MBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
  Copy1MBB->transferSuccessorsAndUpdatePHIs(BB);
  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(Copy0MBB);
  BB->addSuccessor(Copy1MBB);

  // Insert Branch if Flag
  int CC = MI.getOperand(3).getImm();
  int NewCC;
  switch (CC) {
  case ISD::SETGT:
    NewCC = isSelectRROp ? BPF::JSGT_rr : BPF::JSGT_ri;
    break;
  case ISD::SETUGT:
    NewCC = isSelectRROp ? BPF::JUGT_rr : BPF::JUGT_ri;
    break;
  case ISD::SETGE:
    NewCC = isSelectRROp ? BPF::JSGE_rr : BPF::JSGE_ri;
    break;
  case ISD::SETUGE:
    NewCC = isSelectRROp ? BPF::JUGE_rr : BPF::JUGE_ri;
    break;
  case ISD::SETEQ:
    NewCC = isSelectRROp ? BPF::JEQ_rr : BPF::JEQ_ri;
    break;
  case ISD::SETNE:
    NewCC = isSelectRROp ? BPF::JNE_rr : BPF::JNE_ri;
    break;
  case ISD::SETLT:
    NewCC = isSelectRROp ? BPF::JSLT_rr : BPF::JSLT_ri;
    break;
  case ISD::SETULT:
    NewCC = isSelectRROp ? BPF::JULT_rr : BPF::JULT_ri;
    break;
  case ISD::SETLE:
    NewCC = isSelectRROp ? BPF::JSLE_rr : BPF::JSLE_ri;
    break;
  case ISD::SETULE:
    NewCC = isSelectRROp ? BPF::JULE_rr : BPF::JULE_ri;
    break;
  default:
    report_fatal_error("unimplemented select CondCode " + Twine(CC));
  }

  unsigned LHS = MI.getOperand(1).getReg();
  bool isSignedCmp = (CC == ISD::SETGT ||
                      CC == ISD::SETGE ||
                      CC == ISD::SETLT ||
                      CC == ISD::SETLE);

  // eBPF at the moment only has 64-bit comparison. Any 32-bit comparison need
  // to be promoted, however if the 32-bit comparison operands are destination
  // registers then they are implicitly zero-extended already, there is no
  // need of explicit zero-extend sequence for them.
  //
  // We simply do extension for all situations in this method, but we will
  // try to remove those unnecessary in BPFMIPeephole pass.
  if (is32BitCmp)
    LHS = EmitSubregExt(MI, BB, LHS, isSignedCmp);

  if (isSelectRROp) {
    unsigned RHS = MI.getOperand(2).getReg();

    if (is32BitCmp)
      RHS = EmitSubregExt(MI, BB, RHS, isSignedCmp);

    BuildMI(BB, DL, TII.get(NewCC)).addReg(LHS).addReg(RHS).addMBB(Copy1MBB);
  } else {
    int64_t imm32 = MI.getOperand(2).getImm();
    // sanity check before we build J*_ri instruction.
    assert (isInt<32>(imm32));
    BuildMI(BB, DL, TII.get(NewCC))
        .addReg(LHS).addImm(imm32).addMBB(Copy1MBB);
  }

  // Copy0MBB:
  //  %FalseValue = ...
  //  # fallthrough to Copy1MBB
  BB = Copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(Copy1MBB);

  // Copy1MBB:
  //  %Result = phi [ %FalseValue, Copy0MBB ], [ %TrueValue, ThisMBB ]
  // ...
  BB = Copy1MBB;
  BuildMI(*BB, BB->begin(), DL, TII.get(BPF::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(5).getReg())
      .addMBB(Copy0MBB)
      .addReg(MI.getOperand(4).getReg())
      .addMBB(ThisMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

EVT BPFTargetLowering::getSetCCResultType(const DataLayout &, LLVMContext &,
                                          EVT VT) const {
  return getHasAlu32() ? MVT::i32 : MVT::i64;
}

MVT BPFTargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                              EVT VT) const {
  return (getHasAlu32() && VT == MVT::i32) ? MVT::i32 : MVT::i64;
}
