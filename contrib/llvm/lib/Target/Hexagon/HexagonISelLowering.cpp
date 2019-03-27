//===-- HexagonISelLowering.cpp - Hexagon DAG Lowering Implementation -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces that Hexagon uses to lower LLVM code
// into a selection DAG.
//
//===----------------------------------------------------------------------===//

#include "HexagonISelLowering.h"
#include "Hexagon.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "HexagonTargetObjectFile.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "hexagon-lowering"

static cl::opt<bool> EmitJumpTables("hexagon-emit-jump-tables",
  cl::init(true), cl::Hidden,
  cl::desc("Control jump table emission on Hexagon target"));

static cl::opt<bool> EnableHexSDNodeSched("enable-hexagon-sdnode-sched",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Enable Hexagon SDNode scheduling"));

static cl::opt<bool> EnableFastMath("ffast-math",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Enable Fast Math processing"));

static cl::opt<int> MinimumJumpTables("minimum-jump-tables",
  cl::Hidden, cl::ZeroOrMore, cl::init(5),
  cl::desc("Set minimum jump tables"));

static cl::opt<int> MaxStoresPerMemcpyCL("max-store-memcpy",
  cl::Hidden, cl::ZeroOrMore, cl::init(6),
  cl::desc("Max #stores to inline memcpy"));

static cl::opt<int> MaxStoresPerMemcpyOptSizeCL("max-store-memcpy-Os",
  cl::Hidden, cl::ZeroOrMore, cl::init(4),
  cl::desc("Max #stores to inline memcpy"));

static cl::opt<int> MaxStoresPerMemmoveCL("max-store-memmove",
  cl::Hidden, cl::ZeroOrMore, cl::init(6),
  cl::desc("Max #stores to inline memmove"));

static cl::opt<int> MaxStoresPerMemmoveOptSizeCL("max-store-memmove-Os",
  cl::Hidden, cl::ZeroOrMore, cl::init(4),
  cl::desc("Max #stores to inline memmove"));

static cl::opt<int> MaxStoresPerMemsetCL("max-store-memset",
  cl::Hidden, cl::ZeroOrMore, cl::init(8),
  cl::desc("Max #stores to inline memset"));

static cl::opt<int> MaxStoresPerMemsetOptSizeCL("max-store-memset-Os",
  cl::Hidden, cl::ZeroOrMore, cl::init(4),
  cl::desc("Max #stores to inline memset"));

static cl::opt<bool> AlignLoads("hexagon-align-loads",
  cl::Hidden, cl::init(false),
  cl::desc("Rewrite unaligned loads as a pair of aligned loads"));


namespace {

  class HexagonCCState : public CCState {
    unsigned NumNamedVarArgParams = 0;

  public:
    HexagonCCState(CallingConv::ID CC, bool IsVarArg, MachineFunction &MF,
                   SmallVectorImpl<CCValAssign> &locs, LLVMContext &C,
                   unsigned NumNamedArgs)
        : CCState(CC, IsVarArg, MF, locs, C),
          NumNamedVarArgParams(NumNamedArgs) {}
    unsigned getNumNamedVarArgParams() const { return NumNamedVarArgParams; }
  };

} // end anonymous namespace


// Implement calling convention for Hexagon.

static bool CC_SkipOdd(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                       CCValAssign::LocInfo &LocInfo,
                       ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    Hexagon::R0, Hexagon::R1, Hexagon::R2,
    Hexagon::R3, Hexagon::R4, Hexagon::R5
  };
  const unsigned NumArgRegs = array_lengthof(ArgRegs);
  unsigned RegNum = State.getFirstUnallocated(ArgRegs);

  // RegNum is an index into ArgRegs: skip a register if RegNum is odd.
  if (RegNum != NumArgRegs && RegNum % 2 == 1)
    State.AllocateReg(ArgRegs[RegNum]);

  // Always return false here, as this function only makes sure that the first
  // unallocated register has an even register number and does not actually
  // allocate a register for the current argument.
  return false;
}

#include "HexagonGenCallingConv.inc"


SDValue
HexagonTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG)
      const {
  return SDValue();
}

/// CreateCopyOfByValArgument - Make a copy of an aggregate at address specified
/// by "Src" to address "Dst" of size "Size".  Alignment information is
/// specified by the specific parameter attribute. The copy will be passed as
/// a byval function parameter.  Sometimes what we are copying is the end of a
/// larger object, the part that does not fit in registers.
static SDValue CreateCopyOfByValArgument(SDValue Src, SDValue Dst,
                                         SDValue Chain, ISD::ArgFlagsTy Flags,
                                         SelectionDAG &DAG, const SDLoc &dl) {
  SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), dl, MVT::i32);
  return DAG.getMemcpy(Chain, dl, Dst, Src, SizeNode, Flags.getByValAlign(),
                       /*isVolatile=*/false, /*AlwaysInline=*/false,
                       /*isTailCall=*/false,
                       MachinePointerInfo(), MachinePointerInfo());
}

bool
HexagonTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);

  if (MF.getSubtarget<HexagonSubtarget>().useHVXOps())
    return CCInfo.CheckReturn(Outs, RetCC_Hexagon_HVX);
  return CCInfo.CheckReturn(Outs, RetCC_Hexagon);
}

// LowerReturn - Lower ISD::RET. If a struct is larger than 8 bytes and is
// passed by value, the function prototype is modified to return void and
// the value is stored in memory pointed by a pointer passed by caller.
SDValue
HexagonTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   const SDLoc &dl, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to locations.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze return values of ISD::RET
  if (Subtarget.useHVXOps())
    CCInfo.AnalyzeReturn(Outs, RetCC_Hexagon_HVX);
  else
    CCInfo.AnalyzeReturn(Outs, RetCC_Hexagon);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Flag);

    // Guarantee that all emitted copies are stuck together with flags.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(HexagonISD::RET_FLAG, dl, MVT::Other, RetOps);
}

bool HexagonTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  // If either no tail call or told not to tail call at all, don't.
  auto Attr =
      CI->getParent()->getParent()->getFnAttribute("disable-tail-calls");
  if (!CI->isTailCall() || Attr.getValueAsString() == "true")
    return false;

  return true;
}

unsigned  HexagonTargetLowering::getRegisterByName(const char* RegName, EVT VT,
                                              SelectionDAG &DAG) const {
  // Just support r19, the linux kernel uses it.
  unsigned Reg = StringSwitch<unsigned>(RegName)
                     .Case("r19", Hexagon::R19)
                     .Default(0);
  if (Reg)
    return Reg;

  report_fatal_error("Invalid register name global variable");
}

/// LowerCallResult - Lower the result values of an ISD::CALL into the
/// appropriate copies out of appropriate physical registers.  This assumes that
/// Chain/Glue are the input chain/glue to use, and that TheCall is the call
/// being lowered. Returns a SDNode with the same number of values as the
/// ISD::CALL.
SDValue HexagonTargetLowering::LowerCallResult(
    SDValue Chain, SDValue Glue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    const SmallVectorImpl<SDValue> &OutVals, SDValue Callee) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;

  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  if (Subtarget.useHVXOps())
    CCInfo.AnalyzeCallResult(Ins, RetCC_Hexagon_HVX);
  else
    CCInfo.AnalyzeCallResult(Ins, RetCC_Hexagon);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    SDValue RetVal;
    if (RVLocs[i].getValVT() == MVT::i1) {
      // Return values of type MVT::i1 require special handling. The reason
      // is that MVT::i1 is associated with the PredRegs register class, but
      // values of that type are still returned in R0. Generate an explicit
      // copy into a predicate register from R0, and treat the value of the
      // predicate register as the call result.
      auto &MRI = DAG.getMachineFunction().getRegInfo();
      SDValue FR0 = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                                       MVT::i32, Glue);
      // FR0 = (Value, Chain, Glue)
      unsigned PredR = MRI.createVirtualRegister(&Hexagon::PredRegsRegClass);
      SDValue TPR = DAG.getCopyToReg(FR0.getValue(1), dl, PredR,
                                     FR0.getValue(0), FR0.getValue(2));
      // TPR = (Chain, Glue)
      // Don't glue this CopyFromReg, because it copies from a virtual
      // register. If it is glued to the call, InstrEmitter will add it
      // as an implicit def to the call (EmitMachineNode).
      RetVal = DAG.getCopyFromReg(TPR.getValue(0), dl, PredR, MVT::i1);
      Glue = TPR.getValue(1);
      Chain = TPR.getValue(0);
    } else {
      RetVal = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                                  RVLocs[i].getValVT(), Glue);
      Glue = RetVal.getValue(2);
      Chain = RetVal.getValue(1);
    }
    InVals.push_back(RetVal.getValue(0));
  }

  return Chain;
}

/// LowerCall - Functions arguments are copied from virtual regs to
/// (physical regs)/(stack frame), CALLSEQ_START and CALLSEQ_END are emitted.
SDValue
HexagonTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool IsVarArg                         = CLI.IsVarArg;
  bool DoesNotReturn                    = CLI.DoesNotReturn;

  bool IsStructRet    = Outs.empty() ? false : Outs[0].Flags.isSRet();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto PtrVT = getPointerTy(MF.getDataLayout());

  unsigned NumParams = CLI.CS.getInstruction()
                        ? CLI.CS.getFunctionType()->getNumParams()
                        : 0;
  if (GlobalAddressSDNode *GAN = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(GAN->getGlobal(), dl, MVT::i32);

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  HexagonCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext(),
                        NumParams);

  if (Subtarget.useHVXOps())
    CCInfo.AnalyzeCallOperands(Outs, CC_Hexagon_HVX);
  else
    CCInfo.AnalyzeCallOperands(Outs, CC_Hexagon);

  auto Attr = MF.getFunction().getFnAttribute("disable-tail-calls");
  if (Attr.getValueAsString() == "true")
    CLI.IsTailCall = false;

  if (CLI.IsTailCall) {
    bool StructAttrFlag = MF.getFunction().hasStructRetAttr();
    CLI.IsTailCall = IsEligibleForTailCallOptimization(Callee, CallConv,
                        IsVarArg, IsStructRet, StructAttrFlag, Outs,
                        OutVals, Ins, DAG);
    for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
      CCValAssign &VA = ArgLocs[i];
      if (VA.isMemLoc()) {
        CLI.IsTailCall = false;
        break;
      }
    }
    LLVM_DEBUG(dbgs() << (CLI.IsTailCall ? "Eligible for Tail Call\n"
                                         : "Argument must be passed on stack. "
                                           "Not eligible for Tail Call\n"));
  }
  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getNextStackOffset();
  SmallVector<std::pair<unsigned, SDValue>, 16> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  const HexagonRegisterInfo &HRI = *Subtarget.getRegisterInfo();
  SDValue StackPtr =
      DAG.getCopyFromReg(Chain, dl, HRI.getStackRegister(), PtrVT);

  bool NeedsArgAlign = false;
  unsigned LargestAlignSeen = 0;
  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    // Record if we need > 8 byte alignment on an argument.
    bool ArgAlign = Subtarget.isHVXVectorType(VA.getValVT());
    NeedsArgAlign |= ArgAlign;

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
      default:
        // Loc info must be one of Full, BCvt, SExt, ZExt, or AExt.
        llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full:
        break;
      case CCValAssign::BCvt:
        Arg = DAG.getBitcast(VA.getLocVT(), Arg);
        break;
      case CCValAssign::SExt:
        Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::ZExt:
        Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::AExt:
        Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
        break;
    }

    if (VA.isMemLoc()) {
      unsigned LocMemOffset = VA.getLocMemOffset();
      SDValue MemAddr = DAG.getConstant(LocMemOffset, dl,
                                        StackPtr.getValueType());
      MemAddr = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, MemAddr);
      if (ArgAlign)
        LargestAlignSeen = std::max(LargestAlignSeen,
                                    VA.getLocVT().getStoreSizeInBits() >> 3);
      if (Flags.isByVal()) {
        // The argument is a struct passed by value. According to LLVM, "Arg"
        // is a pointer.
        MemOpChains.push_back(CreateCopyOfByValArgument(Arg, MemAddr, Chain,
                                                        Flags, DAG, dl));
      } else {
        MachinePointerInfo LocPI = MachinePointerInfo::getStack(
            DAG.getMachineFunction(), LocMemOffset);
        SDValue S = DAG.getStore(Chain, dl, Arg, MemAddr, LocPI);
        MemOpChains.push_back(S);
      }
      continue;
    }

    // Arguments that can be passed on register must be kept at RegsToPass
    // vector.
    if (VA.isRegLoc())
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
  }

  if (NeedsArgAlign && Subtarget.hasV60Ops()) {
    LLVM_DEBUG(dbgs() << "Function needs byte stack align due to call args\n");
    unsigned VecAlign = HRI.getSpillAlignment(Hexagon::HvxVRRegClass);
    LargestAlignSeen = std::max(LargestAlignSeen, VecAlign);
    MFI.ensureMaxAlignment(LargestAlignSeen);
  }
  // Transform all store nodes into one single node because all store
  // nodes are independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  SDValue Glue;
  if (!CLI.IsTailCall) {
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
    Glue = Chain.getValue(1);
  }

  // Build a sequence of copy-to-reg nodes chained together with token
  // chain and flag operands which copy the outgoing args into registers.
  // The Glue is necessary since all emitted instructions must be
  // stuck together.
  if (!CLI.IsTailCall) {
    for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
      Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                               RegsToPass[i].second, Glue);
      Glue = Chain.getValue(1);
    }
  } else {
    // For tail calls lower the arguments to the 'real' stack slot.
    //
    // Force all the incoming stack arguments to be loaded from the stack
    // before any new outgoing arguments are stored to the stack, because the
    // outgoing stack slots may alias the incoming argument stack slots, and
    // the alias isn't otherwise explicit. This is slightly more conservative
    // than necessary, because it means that each store effectively depends
    // on every argument instead of just those arguments it would clobber.
    //
    // Do not flag preceding copytoreg stuff together with the following stuff.
    Glue = SDValue();
    for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
      Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                               RegsToPass[i].second, Glue);
      Glue = Chain.getValue(1);
    }
    Glue = SDValue();
  }

  bool LongCalls = MF.getSubtarget<HexagonSubtarget>().useLongCalls();
  unsigned Flags = LongCalls ? HexagonII::HMOTF_ConstExtended : 0;

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, PtrVT, 0, Flags);
  } else if (ExternalSymbolSDNode *S =
             dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), PtrVT, Flags);
  }

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));
  }

  const uint32_t *Mask = HRI.getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (Glue.getNode())
    Ops.push_back(Glue);

  if (CLI.IsTailCall) {
    MFI.setHasTailCall();
    return DAG.getNode(HexagonISD::TC_RETURN, dl, NodeTys, Ops);
  }

  // Set this here because we need to know this for "hasFP" in frame lowering.
  // The target-independent code calls getFrameRegister before setting it, and
  // getFrameRegister uses hasFP to determine whether the function has FP.
  MFI.setHasCalls(true);

  unsigned OpCode = DoesNotReturn ? HexagonISD::CALLnr : HexagonISD::CALL;
  Chain = DAG.getNode(OpCode, dl, NodeTys, Ops);
  Glue = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, dl, true),
                             DAG.getIntPtrConstant(0, dl, true), Glue, dl);
  Glue = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, Glue, CallConv, IsVarArg, Ins, dl, DAG,
                         InVals, OutVals, Callee);
}

/// Returns true by value, base pointer and offset pointer and addressing
/// mode by reference if this node can be combined with a load / store to
/// form a post-indexed load / store.
bool HexagonTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
      SDValue &Base, SDValue &Offset, ISD::MemIndexedMode &AM,
      SelectionDAG &DAG) const {
  LSBaseSDNode *LSN = dyn_cast<LSBaseSDNode>(N);
  if (!LSN)
    return false;
  EVT VT = LSN->getMemoryVT();
  if (!VT.isSimple())
    return false;
  bool IsLegalType = VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32 ||
                     VT == MVT::i64 || VT == MVT::f32 || VT == MVT::f64 ||
                     VT == MVT::v2i16 || VT == MVT::v2i32 || VT == MVT::v4i8 ||
                     VT == MVT::v4i16 || VT == MVT::v8i8 ||
                     Subtarget.isHVXVectorType(VT.getSimpleVT());
  if (!IsLegalType)
    return false;

  if (Op->getOpcode() != ISD::ADD)
    return false;
  Base = Op->getOperand(0);
  Offset = Op->getOperand(1);
  if (!isa<ConstantSDNode>(Offset.getNode()))
    return false;
  AM = ISD::POST_INC;

  int32_t V = cast<ConstantSDNode>(Offset.getNode())->getSExtValue();
  return Subtarget.getInstrInfo()->isValidAutoIncImm(VT, V);
}

SDValue
HexagonTargetLowering::LowerINLINEASM(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();
  const HexagonRegisterInfo &HRI = *Subtarget.getRegisterInfo();
  unsigned LR = HRI.getRARegister();

  if (Op.getOpcode() != ISD::INLINEASM || HMFI.hasClobberLR())
    return Op;

  unsigned NumOps = Op.getNumOperands();
  if (Op.getOperand(NumOps-1).getValueType() == MVT::Glue)
    --NumOps;  // Ignore the flag operand.

  for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
    unsigned Flags = cast<ConstantSDNode>(Op.getOperand(i))->getZExtValue();
    unsigned NumVals = InlineAsm::getNumOperandRegisters(Flags);
    ++i;  // Skip the ID value.

    switch (InlineAsm::getKind(Flags)) {
      default:
        llvm_unreachable("Bad flags!");
      case InlineAsm::Kind_RegUse:
      case InlineAsm::Kind_Imm:
      case InlineAsm::Kind_Mem:
        i += NumVals;
        break;
      case InlineAsm::Kind_Clobber:
      case InlineAsm::Kind_RegDef:
      case InlineAsm::Kind_RegDefEarlyClobber: {
        for (; NumVals; --NumVals, ++i) {
          unsigned Reg = cast<RegisterSDNode>(Op.getOperand(i))->getReg();
          if (Reg != LR)
            continue;
          HMFI.setHasClobberLR(true);
          return Op;
        }
        break;
      }
    }
  }

  return Op;
}

// Need to transform ISD::PREFETCH into something that doesn't inherit
// all of the properties of ISD::PREFETCH, specifically SDNPMayLoad and
// SDNPMayStore.
SDValue HexagonTargetLowering::LowerPREFETCH(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Addr = Op.getOperand(1);
  // Lower it to DCFETCH($reg, #0).  A "pat" will try to merge the offset in,
  // if the "reg" is fed by an "add".
  SDLoc DL(Op);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  return DAG.getNode(HexagonISD::DCFETCH, DL, MVT::Other, Chain, Addr, Zero);
}

// Custom-handle ISD::READCYCLECOUNTER because the target-independent SDNode
// is marked as having side-effects, while the register read on Hexagon does
// not have any. TableGen refuses to accept the direct pattern from that node
// to the A4_tfrcpp.
SDValue HexagonTargetLowering::LowerREADCYCLECOUNTER(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDLoc dl(Op);
  SDVTList VTs = DAG.getVTList(MVT::i32, MVT::Other);
  return DAG.getNode(HexagonISD::READCYCLE, dl, VTs, Chain);
}

SDValue HexagonTargetLowering::LowerINTRINSIC_VOID(SDValue Op,
      SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(1))->getZExtValue();
  // Lower the hexagon_prefetch builtin to DCFETCH, as above.
  if (IntNo == Intrinsic::hexagon_prefetch) {
    SDValue Addr = Op.getOperand(2);
    SDLoc DL(Op);
    SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
    return DAG.getNode(HexagonISD::DCFETCH, DL, MVT::Other, Chain, Addr, Zero);
  }
  return SDValue();
}

SDValue
HexagonTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  SDValue Align = Op.getOperand(2);
  SDLoc dl(Op);

  ConstantSDNode *AlignConst = dyn_cast<ConstantSDNode>(Align);
  assert(AlignConst && "Non-constant Align in LowerDYNAMIC_STACKALLOC");

  unsigned A = AlignConst->getSExtValue();
  auto &HFI = *Subtarget.getFrameLowering();
  // "Zero" means natural stack alignment.
  if (A == 0)
    A = HFI.getStackAlignment();

  LLVM_DEBUG({
    dbgs () << __func__ << " Align: " << A << " Size: ";
    Size.getNode()->dump(&DAG);
    dbgs() << "\n";
  });

  SDValue AC = DAG.getConstant(A, dl, MVT::i32);
  SDVTList VTs = DAG.getVTList(MVT::i32, MVT::Other);
  SDValue AA = DAG.getNode(HexagonISD::ALLOCA, dl, VTs, Chain, Size, AC);

  DAG.ReplaceAllUsesOfValueWith(Op, AA);
  return AA;
}

SDValue HexagonTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  HexagonCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext(),
                        MF.getFunction().getFunctionType()->getNumParams());

  if (Subtarget.useHVXOps())
    CCInfo.AnalyzeFormalArguments(Ins, CC_Hexagon_HVX);
  else
    CCInfo.AnalyzeFormalArguments(Ins, CC_Hexagon);

  // For LLVM, in the case when returning a struct by value (>8byte),
  // the first argument is a pointer that points to the location on caller's
  // stack where the return value will be stored. For Hexagon, the location on
  // caller's stack is passed only when the struct size is smaller than (and
  // equal to) 8 bytes. If not, no address will be passed into callee and
  // callee return the result direclty through R0/R1.

  auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    bool ByVal = Flags.isByVal();

    // Arguments passed in registers:
    // 1. 32- and 64-bit values and HVX vectors are passed directly,
    // 2. Large structs are passed via an address, and the address is
    //    passed in a register.
    if (VA.isRegLoc() && ByVal && Flags.getByValSize() <= 8)
      llvm_unreachable("ByValSize must be bigger than 8 bytes");

    bool InReg = VA.isRegLoc() &&
                 (!ByVal || (ByVal && Flags.getByValSize() > 8));

    if (InReg) {
      MVT RegVT = VA.getLocVT();
      if (VA.getLocInfo() == CCValAssign::BCvt)
        RegVT = VA.getValVT();

      const TargetRegisterClass *RC = getRegClassFor(RegVT);
      unsigned VReg = MRI.createVirtualRegister(RC);
      SDValue Copy = DAG.getCopyFromReg(Chain, dl, VReg, RegVT);

      // Treat values of type MVT::i1 specially: they are passed in
      // registers of type i32, but they need to remain as values of
      // type i1 for consistency of the argument lowering.
      if (VA.getValVT() == MVT::i1) {
        assert(RegVT.getSizeInBits() <= 32);
        SDValue T = DAG.getNode(ISD::AND, dl, RegVT,
                                Copy, DAG.getConstant(1, dl, RegVT));
        Copy = DAG.getSetCC(dl, MVT::i1, T, DAG.getConstant(0, dl, RegVT),
                            ISD::SETNE);
      } else {
#ifndef NDEBUG
        unsigned RegSize = RegVT.getSizeInBits();
        assert(RegSize == 32 || RegSize == 64 ||
               Subtarget.isHVXVectorType(RegVT));
#endif
      }
      InVals.push_back(Copy);
      MRI.addLiveIn(VA.getLocReg(), VReg);
    } else {
      assert(VA.isMemLoc() && "Argument should be passed in memory");

      // If it's a byval parameter, then we need to compute the
      // "real" size, not the size of the pointer.
      unsigned ObjSize = Flags.isByVal()
                            ? Flags.getByValSize()
                            : VA.getLocVT().getStoreSizeInBits() / 8;

      // Create the frame index object for this incoming parameter.
      int Offset = HEXAGON_LRFP_SIZE + VA.getLocMemOffset();
      int FI = MFI.CreateFixedObject(ObjSize, Offset, true);
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);

      if (Flags.isByVal()) {
        // If it's a pass-by-value aggregate, then do not dereference the stack
        // location. Instead, we should generate a reference to the stack
        // location.
        InVals.push_back(FIN);
      } else {
        SDValue L = DAG.getLoad(VA.getValVT(), dl, Chain, FIN,
                                MachinePointerInfo::getFixedStack(MF, FI, 0));
        InVals.push_back(L);
      }
    }
  }


  if (IsVarArg) {
    // This will point to the next argument passed via stack.
    int Offset = HEXAGON_LRFP_SIZE + CCInfo.getNextStackOffset();
    int FI = MFI.CreateFixedObject(Hexagon_PointerSize, Offset, true);
    HMFI.setVarArgsFrameIndex(FI);
  }

  return Chain;
}

SDValue
HexagonTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  // VASTART stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  MachineFunction &MF = DAG.getMachineFunction();
  HexagonMachineFunctionInfo *QFI = MF.getInfo<HexagonMachineFunctionInfo>();
  SDValue Addr = DAG.getFrameIndex(QFI->getVarArgsFrameIndex(), MVT::i32);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), SDLoc(Op), Addr, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue HexagonTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  MVT ResTy = ty(Op);
  MVT OpTy = ty(LHS);

  if (OpTy == MVT::v2i16 || OpTy == MVT::v4i8) {
    MVT ElemTy = OpTy.getVectorElementType();
    assert(ElemTy.isScalarInteger());
    MVT WideTy = MVT::getVectorVT(MVT::getIntegerVT(2*ElemTy.getSizeInBits()),
                                  OpTy.getVectorNumElements());
    return DAG.getSetCC(dl, ResTy,
                        DAG.getSExtOrTrunc(LHS, SDLoc(LHS), WideTy),
                        DAG.getSExtOrTrunc(RHS, SDLoc(RHS), WideTy), CC);
  }

  // Treat all other vector types as legal.
  if (ResTy.isVector())
    return Op;

  // Comparisons of short integers should use sign-extend, not zero-extend,
  // since we can represent small negative values in the compare instructions.
  // The LLVM default is to use zero-extend arbitrarily in these cases.
  auto isSExtFree = [this](SDValue N) {
    switch (N.getOpcode()) {
      case ISD::TRUNCATE: {
        // A sign-extend of a truncate of a sign-extend is free.
        SDValue Op = N.getOperand(0);
        if (Op.getOpcode() != ISD::AssertSext)
          return false;
        EVT OrigTy = cast<VTSDNode>(Op.getOperand(1))->getVT();
        unsigned ThisBW = ty(N).getSizeInBits();
        unsigned OrigBW = OrigTy.getSizeInBits();
        // The type that was sign-extended to get the AssertSext must be
        // narrower than the type of N (so that N has still the same value
        // as the original).
        return ThisBW >= OrigBW;
      }
      case ISD::LOAD:
        // We have sign-extended loads.
        return true;
    }
    return false;
  };

  if (OpTy == MVT::i8 || OpTy == MVT::i16) {
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS);
    bool IsNegative = C && C->getAPIntValue().isNegative();
    if (IsNegative || isSExtFree(LHS) || isSExtFree(RHS))
      return DAG.getSetCC(dl, ResTy,
                          DAG.getSExtOrTrunc(LHS, SDLoc(LHS), MVT::i32),
                          DAG.getSExtOrTrunc(RHS, SDLoc(RHS), MVT::i32), CC);
  }

  return SDValue();
}

SDValue
HexagonTargetLowering::LowerVSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDValue PredOp = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1), Op2 = Op.getOperand(2);
  EVT OpVT = Op1.getValueType();
  SDLoc DL(Op);

  if (OpVT == MVT::v2i16) {
    SDValue X1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::v2i32, Op1);
    SDValue X2 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::v2i32, Op2);
    SDValue SL = DAG.getNode(ISD::VSELECT, DL, MVT::v2i32, PredOp, X1, X2);
    SDValue TR = DAG.getNode(ISD::TRUNCATE, DL, MVT::v2i16, SL);
    return TR;
  }

  return SDValue();
}

static Constant *convert_i1_to_i8(const Constant *ConstVal) {
  SmallVector<Constant *, 128> NewConst;
  const ConstantVector *CV = dyn_cast<ConstantVector>(ConstVal);
  if (!CV)
    return nullptr;

  LLVMContext &Ctx = ConstVal->getContext();
  IRBuilder<> IRB(Ctx);
  unsigned NumVectorElements = CV->getNumOperands();
  assert(isPowerOf2_32(NumVectorElements) &&
         "conversion only supported for pow2 VectorSize!");

  for (unsigned i = 0; i < NumVectorElements / 8; ++i) {
    uint8_t x = 0;
    for (unsigned j = 0; j < 8; ++j) {
      uint8_t y = CV->getOperand(i * 8 + j)->getUniqueInteger().getZExtValue();
      x |= y << (7 - j);
    }
    assert((x == 0 || x == 255) && "Either all 0's or all 1's expected!");
    NewConst.push_back(IRB.getInt8(x));
  }
  return ConstantVector::get(NewConst);
}

SDValue
HexagonTargetLowering::LowerConstantPool(SDValue Op, SelectionDAG &DAG) const {
  EVT ValTy = Op.getValueType();
  ConstantPoolSDNode *CPN = cast<ConstantPoolSDNode>(Op);
  Constant *CVal = nullptr;
  bool isVTi1Type = false;
  if (const Constant *ConstVal = dyn_cast<Constant>(CPN->getConstVal())) {
    Type *CValTy = ConstVal->getType();
    if (CValTy->isVectorTy() &&
        CValTy->getVectorElementType()->isIntegerTy(1)) {
      CVal = convert_i1_to_i8(ConstVal);
      isVTi1Type = (CVal != nullptr);
    }
  }
  unsigned Align = CPN->getAlignment();
  bool IsPositionIndependent = isPositionIndependent();
  unsigned char TF = IsPositionIndependent ? HexagonII::MO_PCREL : 0;

  unsigned Offset = 0;
  SDValue T;
  if (CPN->isMachineConstantPoolEntry())
    T = DAG.getTargetConstantPool(CPN->getMachineCPVal(), ValTy, Align, Offset,
                                  TF);
  else if (isVTi1Type)
    T = DAG.getTargetConstantPool(CVal, ValTy, Align, Offset, TF);
  else
    T = DAG.getTargetConstantPool(CPN->getConstVal(), ValTy, Align, Offset, TF);

  assert(cast<ConstantPoolSDNode>(T)->getTargetFlags() == TF &&
         "Inconsistent target flag encountered");

  if (IsPositionIndependent)
    return DAG.getNode(HexagonISD::AT_PCREL, SDLoc(Op), ValTy, T);
  return DAG.getNode(HexagonISD::CP, SDLoc(Op), ValTy, T);
}

SDValue
HexagonTargetLowering::LowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  int Idx = cast<JumpTableSDNode>(Op)->getIndex();
  if (isPositionIndependent()) {
    SDValue T = DAG.getTargetJumpTable(Idx, VT, HexagonII::MO_PCREL);
    return DAG.getNode(HexagonISD::AT_PCREL, SDLoc(Op), VT, T);
  }

  SDValue T = DAG.getTargetJumpTable(Idx, VT);
  return DAG.getNode(HexagonISD::JT, SDLoc(Op), VT, T);
}

SDValue
HexagonTargetLowering::LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const {
  const HexagonRegisterInfo &HRI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  if (Depth) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(4, dl, MVT::i32);
    return DAG.getLoad(VT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return LR, which contains the return address. Mark it an implicit live-in.
  unsigned Reg = MF.addLiveIn(HRI.getRARegister(), getRegClassFor(MVT::i32));
  return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg, VT);
}

SDValue
HexagonTargetLowering::LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
  const HexagonRegisterInfo &HRI = *Subtarget.getRegisterInfo();
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                                         HRI.getFrameRegister(), VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue
HexagonTargetLowering::LowerATOMIC_FENCE(SDValue Op, SelectionDAG& DAG) const {
  SDLoc dl(Op);
  return DAG.getNode(HexagonISD::BARRIER, dl, MVT::Other, Op.getOperand(0));
}

SDValue
HexagonTargetLowering::LowerGLOBALADDRESS(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  auto *GAN = cast<GlobalAddressSDNode>(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout());
  auto *GV = GAN->getGlobal();
  int64_t Offset = GAN->getOffset();

  auto &HLOF = *HTM.getObjFileLowering();
  Reloc::Model RM = HTM.getRelocationModel();

  if (RM == Reloc::Static) {
    SDValue GA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, Offset);
    const GlobalObject *GO = GV->getBaseObject();
    if (GO && Subtarget.useSmallData() && HLOF.isGlobalInSmallSection(GO, HTM))
      return DAG.getNode(HexagonISD::CONST32_GP, dl, PtrVT, GA);
    return DAG.getNode(HexagonISD::CONST32, dl, PtrVT, GA);
  }

  bool UsePCRel = getTargetMachine().shouldAssumeDSOLocal(*GV->getParent(), GV);
  if (UsePCRel) {
    SDValue GA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, Offset,
                                            HexagonII::MO_PCREL);
    return DAG.getNode(HexagonISD::AT_PCREL, dl, PtrVT, GA);
  }

  // Use GOT index.
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(PtrVT);
  SDValue GA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, HexagonII::MO_GOT);
  SDValue Off = DAG.getConstant(Offset, dl, MVT::i32);
  return DAG.getNode(HexagonISD::AT_GOT, dl, PtrVT, GOT, GA, Off);
}

// Specifies that for loads and stores VT can be promoted to PromotedLdStVT.
SDValue
HexagonTargetLowering::LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const {
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  SDLoc dl(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  Reloc::Model RM = HTM.getRelocationModel();
  if (RM == Reloc::Static) {
    SDValue A = DAG.getTargetBlockAddress(BA, PtrVT);
    return DAG.getNode(HexagonISD::CONST32_GP, dl, PtrVT, A);
  }

  SDValue A = DAG.getTargetBlockAddress(BA, PtrVT, 0, HexagonII::MO_PCREL);
  return DAG.getNode(HexagonISD::AT_PCREL, dl, PtrVT, A);
}

SDValue
HexagonTargetLowering::LowerGLOBAL_OFFSET_TABLE(SDValue Op, SelectionDAG &DAG)
      const {
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue GOTSym = DAG.getTargetExternalSymbol(HEXAGON_GOT_SYM_NAME, PtrVT,
                                               HexagonII::MO_PCREL);
  return DAG.getNode(HexagonISD::AT_PCREL, SDLoc(Op), PtrVT, GOTSym);
}

SDValue
HexagonTargetLowering::GetDynamicTLSAddr(SelectionDAG &DAG, SDValue Chain,
      GlobalAddressSDNode *GA, SDValue Glue, EVT PtrVT, unsigned ReturnReg,
      unsigned char OperandFlags) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDLoc dl(GA);
  SDValue TGA = DAG.getTargetGlobalAddress(GA->getGlobal(), dl,
                                           GA->getValueType(0),
                                           GA->getOffset(),
                                           OperandFlags);
  // Create Operands for the call.The Operands should have the following:
  // 1. Chain SDValue
  // 2. Callee which in this case is the Global address value.
  // 3. Registers live into the call.In this case its R0, as we
  //    have just one argument to be passed.
  // 4. Glue.
  // Note: The order is important.

  const auto &HRI = *Subtarget.getRegisterInfo();
  const uint32_t *Mask = HRI.getCallPreservedMask(MF, CallingConv::C);
  assert(Mask && "Missing call preserved mask for calling convention");
  SDValue Ops[] = { Chain, TGA, DAG.getRegister(Hexagon::R0, PtrVT),
                    DAG.getRegisterMask(Mask), Glue };
  Chain = DAG.getNode(HexagonISD::CALL, dl, NodeTys, Ops);

  // Inform MFI that function has calls.
  MFI.setAdjustsStack(true);

  Glue = Chain.getValue(1);
  return DAG.getCopyFromReg(Chain, dl, ReturnReg, PtrVT, Glue);
}

//
// Lower using the intial executable model for TLS addresses
//
SDValue
HexagonTargetLowering::LowerToTLSInitialExecModel(GlobalAddressSDNode *GA,
      SelectionDAG &DAG) const {
  SDLoc dl(GA);
  int64_t Offset = GA->getOffset();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Get the thread pointer.
  SDValue TP = DAG.getCopyFromReg(DAG.getEntryNode(), dl, Hexagon::UGP, PtrVT);

  bool IsPositionIndependent = isPositionIndependent();
  unsigned char TF =
      IsPositionIndependent ? HexagonII::MO_IEGOT : HexagonII::MO_IE;

  // First generate the TLS symbol address
  SDValue TGA = DAG.getTargetGlobalAddress(GA->getGlobal(), dl, PtrVT,
                                           Offset, TF);

  SDValue Sym = DAG.getNode(HexagonISD::CONST32, dl, PtrVT, TGA);

  if (IsPositionIndependent) {
    // Generate the GOT pointer in case of position independent code
    SDValue GOT = LowerGLOBAL_OFFSET_TABLE(Sym, DAG);

    // Add the TLS Symbol address to GOT pointer.This gives
    // GOT relative relocation for the symbol.
    Sym = DAG.getNode(ISD::ADD, dl, PtrVT, GOT, Sym);
  }

  // Load the offset value for TLS symbol.This offset is relative to
  // thread pointer.
  SDValue LoadOffset =
      DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Sym, MachinePointerInfo());

  // Address of the thread local variable is the add of thread
  // pointer and the offset of the variable.
  return DAG.getNode(ISD::ADD, dl, PtrVT, TP, LoadOffset);
}

//
// Lower using the local executable model for TLS addresses
//
SDValue
HexagonTargetLowering::LowerToTLSLocalExecModel(GlobalAddressSDNode *GA,
      SelectionDAG &DAG) const {
  SDLoc dl(GA);
  int64_t Offset = GA->getOffset();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Get the thread pointer.
  SDValue TP = DAG.getCopyFromReg(DAG.getEntryNode(), dl, Hexagon::UGP, PtrVT);
  // Generate the TLS symbol address
  SDValue TGA = DAG.getTargetGlobalAddress(GA->getGlobal(), dl, PtrVT, Offset,
                                           HexagonII::MO_TPREL);
  SDValue Sym = DAG.getNode(HexagonISD::CONST32, dl, PtrVT, TGA);

  // Address of the thread local variable is the add of thread
  // pointer and the offset of the variable.
  return DAG.getNode(ISD::ADD, dl, PtrVT, TP, Sym);
}

//
// Lower using the general dynamic model for TLS addresses
//
SDValue
HexagonTargetLowering::LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
      SelectionDAG &DAG) const {
  SDLoc dl(GA);
  int64_t Offset = GA->getOffset();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // First generate the TLS symbol address
  SDValue TGA = DAG.getTargetGlobalAddress(GA->getGlobal(), dl, PtrVT, Offset,
                                           HexagonII::MO_GDGOT);

  // Then, generate the GOT pointer
  SDValue GOT = LowerGLOBAL_OFFSET_TABLE(TGA, DAG);

  // Add the TLS symbol and the GOT pointer
  SDValue Sym = DAG.getNode(HexagonISD::CONST32, dl, PtrVT, TGA);
  SDValue Chain = DAG.getNode(ISD::ADD, dl, PtrVT, GOT, Sym);

  // Copy over the argument to R0
  SDValue InFlag;
  Chain = DAG.getCopyToReg(DAG.getEntryNode(), dl, Hexagon::R0, Chain, InFlag);
  InFlag = Chain.getValue(1);

  unsigned Flags =
      static_cast<const HexagonSubtarget &>(DAG.getSubtarget()).useLongCalls()
          ? HexagonII::MO_GDPLT | HexagonII::HMOTF_ConstExtended
          : HexagonII::MO_GDPLT;

  return GetDynamicTLSAddr(DAG, Chain, GA, InFlag, PtrVT,
                           Hexagon::R0, Flags);
}

//
// Lower TLS addresses.
//
// For now for dynamic models, we only support the general dynamic model.
//
SDValue
HexagonTargetLowering::LowerGlobalTLSAddress(SDValue Op,
      SelectionDAG &DAG) const {
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);

  switch (HTM.getTLSModel(GA->getGlobal())) {
    case TLSModel::GeneralDynamic:
    case TLSModel::LocalDynamic:
      return LowerToTLSGeneralDynamicModel(GA, DAG);
    case TLSModel::InitialExec:
      return LowerToTLSInitialExecModel(GA, DAG);
    case TLSModel::LocalExec:
      return LowerToTLSLocalExecModel(GA, DAG);
  }
  llvm_unreachable("Bogus TLS model");
}

//===----------------------------------------------------------------------===//
// TargetLowering Implementation
//===----------------------------------------------------------------------===//

HexagonTargetLowering::HexagonTargetLowering(const TargetMachine &TM,
                                             const HexagonSubtarget &ST)
    : TargetLowering(TM), HTM(static_cast<const HexagonTargetMachine&>(TM)),
      Subtarget(ST) {
  auto &HRI = *Subtarget.getRegisterInfo();

  setPrefLoopAlignment(4);
  setPrefFunctionAlignment(4);
  setMinFunctionAlignment(2);
  setStackPointerRegisterToSaveRestore(HRI.getStackRegister());
  setBooleanContents(TargetLoweringBase::UndefinedBooleanContent);
  setBooleanVectorContents(TargetLoweringBase::UndefinedBooleanContent);

  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(32);

  if (EnableHexSDNodeSched)
    setSchedulingPreference(Sched::VLIW);
  else
    setSchedulingPreference(Sched::Source);

  // Limits for inline expansion of memcpy/memmove
  MaxStoresPerMemcpy = MaxStoresPerMemcpyCL;
  MaxStoresPerMemcpyOptSize = MaxStoresPerMemcpyOptSizeCL;
  MaxStoresPerMemmove = MaxStoresPerMemmoveCL;
  MaxStoresPerMemmoveOptSize = MaxStoresPerMemmoveOptSizeCL;
  MaxStoresPerMemset = MaxStoresPerMemsetCL;
  MaxStoresPerMemsetOptSize = MaxStoresPerMemsetOptSizeCL;

  //
  // Set up register classes.
  //

  addRegisterClass(MVT::i1,    &Hexagon::PredRegsRegClass);
  addRegisterClass(MVT::v2i1,  &Hexagon::PredRegsRegClass);  // bbbbaaaa
  addRegisterClass(MVT::v4i1,  &Hexagon::PredRegsRegClass);  // ddccbbaa
  addRegisterClass(MVT::v8i1,  &Hexagon::PredRegsRegClass);  // hgfedcba
  addRegisterClass(MVT::i32,   &Hexagon::IntRegsRegClass);
  addRegisterClass(MVT::v2i16, &Hexagon::IntRegsRegClass);
  addRegisterClass(MVT::v4i8,  &Hexagon::IntRegsRegClass);
  addRegisterClass(MVT::i64,   &Hexagon::DoubleRegsRegClass);
  addRegisterClass(MVT::v8i8,  &Hexagon::DoubleRegsRegClass);
  addRegisterClass(MVT::v4i16, &Hexagon::DoubleRegsRegClass);
  addRegisterClass(MVT::v2i32, &Hexagon::DoubleRegsRegClass);

  addRegisterClass(MVT::f32, &Hexagon::IntRegsRegClass);
  addRegisterClass(MVT::f64, &Hexagon::DoubleRegsRegClass);

  //
  // Handling of scalar operations.
  //
  // All operations default to "legal", except:
  // - indexed loads and stores (pre-/post-incremented),
  // - ANY_EXTEND_VECTOR_INREG, ATOMIC_CMP_SWAP_WITH_SUCCESS, CONCAT_VECTORS,
  //   ConstantFP, DEBUGTRAP, FCEIL, FCOPYSIGN, FEXP, FEXP2, FFLOOR, FGETSIGN,
  //   FLOG, FLOG2, FLOG10, FMAXNUM, FMINNUM, FNEARBYINT, FRINT, FROUND, TRAP,
  //   FTRUNC, PREFETCH, SIGN_EXTEND_VECTOR_INREG, ZERO_EXTEND_VECTOR_INREG,
  // which default to "expand" for at least one type.

  // Misc operations.
  setOperationAction(ISD::ConstantFP,           MVT::f32,   Legal);
  setOperationAction(ISD::ConstantFP,           MVT::f64,   Legal);
  setOperationAction(ISD::TRAP,                 MVT::Other, Legal);
  setOperationAction(ISD::ConstantPool,         MVT::i32,   Custom);
  setOperationAction(ISD::JumpTable,            MVT::i32,   Custom);
  setOperationAction(ISD::BUILD_PAIR,           MVT::i64,   Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG,    MVT::i1,    Expand);
  setOperationAction(ISD::INLINEASM,            MVT::Other, Custom);
  setOperationAction(ISD::PREFETCH,             MVT::Other, Custom);
  setOperationAction(ISD::READCYCLECOUNTER,     MVT::i64,   Custom);
  setOperationAction(ISD::INTRINSIC_VOID,       MVT::Other, Custom);
  setOperationAction(ISD::EH_RETURN,            MVT::Other, Custom);
  setOperationAction(ISD::GLOBAL_OFFSET_TABLE,  MVT::i32,   Custom);
  setOperationAction(ISD::GlobalTLSAddress,     MVT::i32,   Custom);
  setOperationAction(ISD::ATOMIC_FENCE,         MVT::Other, Custom);

  // Custom legalize GlobalAddress nodes into CONST32.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i8,  Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i32, Custom);

  // Hexagon needs to optimize cases with negative constants.
  setOperationAction(ISD::SETCC, MVT::i8,    Custom);
  setOperationAction(ISD::SETCC, MVT::i16,   Custom);
  setOperationAction(ISD::SETCC, MVT::v4i8,  Custom);
  setOperationAction(ISD::SETCC, MVT::v2i16, Custom);

  // VASTART needs to be custom lowered to use the VarArgsFrameIndex.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND,   MVT::Other, Expand);
  setOperationAction(ISD::VAARG,   MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,  MVT::Other, Expand);

  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);

  if (EmitJumpTables)
    setMinimumJumpTableEntries(MinimumJumpTables);
  else
    setMinimumJumpTableEntries(std::numeric_limits<int>::max());
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  setOperationAction(ISD::ABS, MVT::i32, Legal);
  setOperationAction(ISD::ABS, MVT::i64, Legal);

  // Hexagon has A4_addp_c and A4_subp_c that take and generate a carry bit,
  // but they only operate on i64.
  for (MVT VT : MVT::integer_valuetypes()) {
    setOperationAction(ISD::UADDO,    VT, Expand);
    setOperationAction(ISD::USUBO,    VT, Expand);
    setOperationAction(ISD::SADDO,    VT, Expand);
    setOperationAction(ISD::SSUBO,    VT, Expand);
    setOperationAction(ISD::ADDCARRY, VT, Expand);
    setOperationAction(ISD::SUBCARRY, VT, Expand);
  }
  setOperationAction(ISD::ADDCARRY, MVT::i64, Custom);
  setOperationAction(ISD::SUBCARRY, MVT::i64, Custom);

  setOperationAction(ISD::CTLZ, MVT::i8,  Promote);
  setOperationAction(ISD::CTLZ, MVT::i16, Promote);
  setOperationAction(ISD::CTTZ, MVT::i8,  Promote);
  setOperationAction(ISD::CTTZ, MVT::i16, Promote);

  // Popcount can count # of 1s in i64 but returns i32.
  setOperationAction(ISD::CTPOP, MVT::i8,  Promote);
  setOperationAction(ISD::CTPOP, MVT::i16, Promote);
  setOperationAction(ISD::CTPOP, MVT::i32, Promote);
  setOperationAction(ISD::CTPOP, MVT::i64, Legal);

  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);
  setOperationAction(ISD::BSWAP, MVT::i32, Legal);
  setOperationAction(ISD::BSWAP, MVT::i64, Legal);

  setOperationAction(ISD::FSHL, MVT::i32, Legal);
  setOperationAction(ISD::FSHL, MVT::i64, Legal);
  setOperationAction(ISD::FSHR, MVT::i32, Legal);
  setOperationAction(ISD::FSHR, MVT::i64, Legal);

  for (unsigned IntExpOp :
       {ISD::SDIV,      ISD::UDIV,      ISD::SREM,      ISD::UREM,
        ISD::SDIVREM,   ISD::UDIVREM,   ISD::ROTL,      ISD::ROTR,
        ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS,
        ISD::SMUL_LOHI, ISD::UMUL_LOHI}) {
    for (MVT VT : MVT::integer_valuetypes())
      setOperationAction(IntExpOp, VT, Expand);
  }

  for (unsigned FPExpOp :
       {ISD::FDIV, ISD::FREM, ISD::FSQRT, ISD::FSIN, ISD::FCOS, ISD::FSINCOS,
        ISD::FPOW, ISD::FCOPYSIGN}) {
    for (MVT VT : MVT::fp_valuetypes())
      setOperationAction(FPExpOp, VT, Expand);
  }

  // No extending loads from i32.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i32, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i32, Expand);
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i32, Expand);
  }
  // Turn FP truncstore into trunc + store.
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  // Turn FP extload into load/fpextend.
  for (MVT VT : MVT::fp_valuetypes())
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f32, Expand);

  // Expand BR_CC and SELECT_CC for all integer and fp types.
  for (MVT VT : MVT::integer_valuetypes()) {
    setOperationAction(ISD::BR_CC,     VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
  }
  for (MVT VT : MVT::fp_valuetypes()) {
    setOperationAction(ISD::BR_CC,     VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
  }
  setOperationAction(ISD::BR_CC, MVT::Other, Expand);

  //
  // Handling of vector operations.
  //

  // Set the action for vector operations to "expand", then override it with
  // either "custom" or "legal" for specific cases.
  static const unsigned VectExpOps[] = {
    // Integer arithmetic:
    ISD::ADD,     ISD::SUB,     ISD::MUL,     ISD::SDIV,      ISD::UDIV,
    ISD::SREM,    ISD::UREM,    ISD::SDIVREM, ISD::UDIVREM,   ISD::SADDO,
    ISD::UADDO,   ISD::SSUBO,   ISD::USUBO,   ISD::SMUL_LOHI, ISD::UMUL_LOHI,
    // Logical/bit:
    ISD::AND,     ISD::OR,      ISD::XOR,     ISD::ROTL,    ISD::ROTR,
    ISD::CTPOP,   ISD::CTLZ,    ISD::CTTZ,
    // Floating point arithmetic/math functions:
    ISD::FADD,    ISD::FSUB,    ISD::FMUL,    ISD::FMA,     ISD::FDIV,
    ISD::FREM,    ISD::FNEG,    ISD::FABS,    ISD::FSQRT,   ISD::FSIN,
    ISD::FCOS,    ISD::FPOW,    ISD::FLOG,    ISD::FLOG2,
    ISD::FLOG10,  ISD::FEXP,    ISD::FEXP2,   ISD::FCEIL,   ISD::FTRUNC,
    ISD::FRINT,   ISD::FNEARBYINT,            ISD::FROUND,  ISD::FFLOOR,
    ISD::FMINNUM, ISD::FMAXNUM, ISD::FSINCOS,
    // Misc:
    ISD::BR_CC,   ISD::SELECT_CC,             ISD::ConstantPool,
    // Vector:
    ISD::BUILD_VECTOR,          ISD::SCALAR_TO_VECTOR,
    ISD::EXTRACT_VECTOR_ELT,    ISD::INSERT_VECTOR_ELT,
    ISD::EXTRACT_SUBVECTOR,     ISD::INSERT_SUBVECTOR,
    ISD::CONCAT_VECTORS,        ISD::VECTOR_SHUFFLE
  };

  for (MVT VT : MVT::vector_valuetypes()) {
    for (unsigned VectExpOp : VectExpOps)
      setOperationAction(VectExpOp, VT, Expand);

    // Expand all extending loads and truncating stores:
    for (MVT TargetVT : MVT::vector_valuetypes()) {
      if (TargetVT == VT)
        continue;
      setLoadExtAction(ISD::EXTLOAD, TargetVT, VT, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, TargetVT, VT, Expand);
      setLoadExtAction(ISD::SEXTLOAD, TargetVT, VT, Expand);
      setTruncStoreAction(VT, TargetVT, Expand);
    }

    // Normalize all inputs to SELECT to be vectors of i32.
    if (VT.getVectorElementType() != MVT::i32) {
      MVT VT32 = MVT::getVectorVT(MVT::i32, VT.getSizeInBits()/32);
      setOperationAction(ISD::SELECT, VT, Promote);
      AddPromotedToType(ISD::SELECT, VT, VT32);
    }
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::SHL, VT, Custom);
    setOperationAction(ISD::SRL, VT, Custom);
  }

  // Extending loads from (native) vectors of i8 into (native) vectors of i16
  // are legal.
  setLoadExtAction(ISD::EXTLOAD,  MVT::v2i16, MVT::v2i8, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::v2i16, MVT::v2i8, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::v2i16, MVT::v2i8, Legal);
  setLoadExtAction(ISD::EXTLOAD,  MVT::v4i16, MVT::v4i8, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::v4i16, MVT::v4i8, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::v4i16, MVT::v4i8, Legal);

  // Types natively supported:
  for (MVT NativeVT : {MVT::v8i1, MVT::v4i1, MVT::v2i1, MVT::v4i8,
                       MVT::v8i8, MVT::v2i16, MVT::v4i16, MVT::v2i32}) {
    setOperationAction(ISD::BUILD_VECTOR,       NativeVT, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, NativeVT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT,  NativeVT, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR,  NativeVT, Custom);
    setOperationAction(ISD::INSERT_SUBVECTOR,   NativeVT, Custom);
    setOperationAction(ISD::CONCAT_VECTORS,     NativeVT, Custom);

    setOperationAction(ISD::ADD, NativeVT, Legal);
    setOperationAction(ISD::SUB, NativeVT, Legal);
    setOperationAction(ISD::MUL, NativeVT, Legal);
    setOperationAction(ISD::AND, NativeVT, Legal);
    setOperationAction(ISD::OR,  NativeVT, Legal);
    setOperationAction(ISD::XOR, NativeVT, Legal);
  }

  // Custom lower unaligned loads.
  // Also, for both loads and stores, verify the alignment of the address
  // in case it is a compile-time constant. This is a usability feature to
  // provide a meaningful error message to users.
  for (MVT VT : {MVT::i16, MVT::i32, MVT::v4i8, MVT::i64, MVT::v8i8,
                 MVT::v2i16, MVT::v4i16, MVT::v2i32}) {
    setOperationAction(ISD::LOAD,  VT, Custom);
    setOperationAction(ISD::STORE, VT, Custom);
  }

  for (MVT VT : {MVT::v2i16, MVT::v4i8, MVT::v2i32, MVT::v4i16, MVT::v2i32}) {
    setCondCodeAction(ISD::SETLT,  VT, Expand);
    setCondCodeAction(ISD::SETLE,  VT, Expand);
    setCondCodeAction(ISD::SETULT, VT, Expand);
    setCondCodeAction(ISD::SETULE, VT, Expand);
  }

  // Custom-lower bitcasts from i8 to v8i1.
  setOperationAction(ISD::BITCAST,        MVT::i8,    Custom);
  setOperationAction(ISD::SETCC,          MVT::v2i16, Custom);
  setOperationAction(ISD::VSELECT,        MVT::v2i16, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v4i8,  Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v4i16, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v8i8,  Custom);

  // V5+.
  setOperationAction(ISD::FMA,  MVT::f64, Expand);
  setOperationAction(ISD::FADD, MVT::f64, Expand);
  setOperationAction(ISD::FSUB, MVT::f64, Expand);
  setOperationAction(ISD::FMUL, MVT::f64, Expand);

  setOperationAction(ISD::FMINNUM, MVT::f32, Legal);
  setOperationAction(ISD::FMAXNUM, MVT::f32, Legal);

  setOperationAction(ISD::FP_TO_UINT, MVT::i1,  Promote);
  setOperationAction(ISD::FP_TO_UINT, MVT::i8,  Promote);
  setOperationAction(ISD::FP_TO_UINT, MVT::i16, Promote);
  setOperationAction(ISD::FP_TO_SINT, MVT::i1,  Promote);
  setOperationAction(ISD::FP_TO_SINT, MVT::i8,  Promote);
  setOperationAction(ISD::FP_TO_SINT, MVT::i16, Promote);
  setOperationAction(ISD::UINT_TO_FP, MVT::i1,  Promote);
  setOperationAction(ISD::UINT_TO_FP, MVT::i8,  Promote);
  setOperationAction(ISD::UINT_TO_FP, MVT::i16, Promote);
  setOperationAction(ISD::SINT_TO_FP, MVT::i1,  Promote);
  setOperationAction(ISD::SINT_TO_FP, MVT::i8,  Promote);
  setOperationAction(ISD::SINT_TO_FP, MVT::i16, Promote);

  // Handling of indexed loads/stores: default is "expand".
  //
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32, MVT::i64, MVT::f32, MVT::f64,
                 MVT::v2i16, MVT::v2i32, MVT::v4i8, MVT::v4i16, MVT::v8i8}) {
    setIndexedLoadAction(ISD::POST_INC, VT, Legal);
    setIndexedStoreAction(ISD::POST_INC, VT, Legal);
  }

  // Subtarget-specific operation actions.
  //
  if (Subtarget.hasV60Ops()) {
    setOperationAction(ISD::ROTL, MVT::i32, Legal);
    setOperationAction(ISD::ROTL, MVT::i64, Legal);
    setOperationAction(ISD::ROTR, MVT::i32, Legal);
    setOperationAction(ISD::ROTR, MVT::i64, Legal);
  }
  if (Subtarget.hasV66Ops()) {
    setOperationAction(ISD::FADD, MVT::f64, Legal);
    setOperationAction(ISD::FSUB, MVT::f64, Legal);
  }

  if (Subtarget.useHVXOps())
    initializeHVXLowering();

  computeRegisterProperties(&HRI);

  //
  // Library calls for unsupported operations
  //
  bool FastMath  = EnableFastMath;

  setLibcallName(RTLIB::SDIV_I32, "__hexagon_divsi3");
  setLibcallName(RTLIB::SDIV_I64, "__hexagon_divdi3");
  setLibcallName(RTLIB::UDIV_I32, "__hexagon_udivsi3");
  setLibcallName(RTLIB::UDIV_I64, "__hexagon_udivdi3");
  setLibcallName(RTLIB::SREM_I32, "__hexagon_modsi3");
  setLibcallName(RTLIB::SREM_I64, "__hexagon_moddi3");
  setLibcallName(RTLIB::UREM_I32, "__hexagon_umodsi3");
  setLibcallName(RTLIB::UREM_I64, "__hexagon_umoddi3");

  setLibcallName(RTLIB::SINTTOFP_I128_F64, "__hexagon_floattidf");
  setLibcallName(RTLIB::SINTTOFP_I128_F32, "__hexagon_floattisf");
  setLibcallName(RTLIB::FPTOUINT_F32_I128, "__hexagon_fixunssfti");
  setLibcallName(RTLIB::FPTOUINT_F64_I128, "__hexagon_fixunsdfti");
  setLibcallName(RTLIB::FPTOSINT_F32_I128, "__hexagon_fixsfti");
  setLibcallName(RTLIB::FPTOSINT_F64_I128, "__hexagon_fixdfti");

  // This is the only fast library function for sqrtd.
  if (FastMath)
    setLibcallName(RTLIB::SQRT_F64, "__hexagon_fast2_sqrtdf2");

  // Prefix is: nothing  for "slow-math",
  //            "fast2_" for V5+ fast-math double-precision
  // (actually, keep fast-math and fast-math2 separate for now)
  if (FastMath) {
    setLibcallName(RTLIB::ADD_F64, "__hexagon_fast_adddf3");
    setLibcallName(RTLIB::SUB_F64, "__hexagon_fast_subdf3");
    setLibcallName(RTLIB::MUL_F64, "__hexagon_fast_muldf3");
    setLibcallName(RTLIB::DIV_F64, "__hexagon_fast_divdf3");
    setLibcallName(RTLIB::DIV_F32, "__hexagon_fast_divsf3");
  } else {
    setLibcallName(RTLIB::ADD_F64, "__hexagon_adddf3");
    setLibcallName(RTLIB::SUB_F64, "__hexagon_subdf3");
    setLibcallName(RTLIB::MUL_F64, "__hexagon_muldf3");
    setLibcallName(RTLIB::DIV_F64, "__hexagon_divdf3");
    setLibcallName(RTLIB::DIV_F32, "__hexagon_divsf3");
  }

  if (FastMath)
    setLibcallName(RTLIB::SQRT_F32, "__hexagon_fast2_sqrtf");
  else
    setLibcallName(RTLIB::SQRT_F32, "__hexagon_sqrtf");

  // These cause problems when the shift amount is non-constant.
  setLibcallName(RTLIB::SHL_I128, nullptr);
  setLibcallName(RTLIB::SRL_I128, nullptr);
  setLibcallName(RTLIB::SRA_I128, nullptr);
}

const char* HexagonTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((HexagonISD::NodeType)Opcode) {
  case HexagonISD::ADDC:          return "HexagonISD::ADDC";
  case HexagonISD::SUBC:          return "HexagonISD::SUBC";
  case HexagonISD::ALLOCA:        return "HexagonISD::ALLOCA";
  case HexagonISD::AT_GOT:        return "HexagonISD::AT_GOT";
  case HexagonISD::AT_PCREL:      return "HexagonISD::AT_PCREL";
  case HexagonISD::BARRIER:       return "HexagonISD::BARRIER";
  case HexagonISD::CALL:          return "HexagonISD::CALL";
  case HexagonISD::CALLnr:        return "HexagonISD::CALLnr";
  case HexagonISD::CALLR:         return "HexagonISD::CALLR";
  case HexagonISD::COMBINE:       return "HexagonISD::COMBINE";
  case HexagonISD::CONST32_GP:    return "HexagonISD::CONST32_GP";
  case HexagonISD::CONST32:       return "HexagonISD::CONST32";
  case HexagonISD::CP:            return "HexagonISD::CP";
  case HexagonISD::DCFETCH:       return "HexagonISD::DCFETCH";
  case HexagonISD::EH_RETURN:     return "HexagonISD::EH_RETURN";
  case HexagonISD::TSTBIT:        return "HexagonISD::TSTBIT";
  case HexagonISD::EXTRACTU:      return "HexagonISD::EXTRACTU";
  case HexagonISD::INSERT:        return "HexagonISD::INSERT";
  case HexagonISD::JT:            return "HexagonISD::JT";
  case HexagonISD::RET_FLAG:      return "HexagonISD::RET_FLAG";
  case HexagonISD::TC_RETURN:     return "HexagonISD::TC_RETURN";
  case HexagonISD::VASL:          return "HexagonISD::VASL";
  case HexagonISD::VASR:          return "HexagonISD::VASR";
  case HexagonISD::VLSR:          return "HexagonISD::VLSR";
  case HexagonISD::VSPLAT:        return "HexagonISD::VSPLAT";
  case HexagonISD::VEXTRACTW:     return "HexagonISD::VEXTRACTW";
  case HexagonISD::VINSERTW0:     return "HexagonISD::VINSERTW0";
  case HexagonISD::VROR:          return "HexagonISD::VROR";
  case HexagonISD::READCYCLE:     return "HexagonISD::READCYCLE";
  case HexagonISD::VZERO:         return "HexagonISD::VZERO";
  case HexagonISD::VSPLATW:       return "HexagonISD::VSPLATW";
  case HexagonISD::D2P:           return "HexagonISD::D2P";
  case HexagonISD::P2D:           return "HexagonISD::P2D";
  case HexagonISD::V2Q:           return "HexagonISD::V2Q";
  case HexagonISD::Q2V:           return "HexagonISD::Q2V";
  case HexagonISD::QCAT:          return "HexagonISD::QCAT";
  case HexagonISD::QTRUE:         return "HexagonISD::QTRUE";
  case HexagonISD::QFALSE:        return "HexagonISD::QFALSE";
  case HexagonISD::TYPECAST:      return "HexagonISD::TYPECAST";
  case HexagonISD::VALIGN:        return "HexagonISD::VALIGN";
  case HexagonISD::VALIGNADDR:    return "HexagonISD::VALIGNADDR";
  case HexagonISD::OP_END:        break;
  }
  return nullptr;
}

void
HexagonTargetLowering::validateConstPtrAlignment(SDValue Ptr, const SDLoc &dl,
      unsigned NeedAlign) const {
  auto *CA = dyn_cast<ConstantSDNode>(Ptr);
  if (!CA)
    return;
  unsigned Addr = CA->getZExtValue();
  unsigned HaveAlign = Addr != 0 ? 1u << countTrailingZeros(Addr) : NeedAlign;
  if (HaveAlign < NeedAlign) {
    std::string ErrMsg;
    raw_string_ostream O(ErrMsg);
    O << "Misaligned constant address: " << format_hex(Addr, 10)
      << " has alignment " << HaveAlign
      << ", but the memory access requires " << NeedAlign;
    if (DebugLoc DL = dl.getDebugLoc())
      DL.print(O << ", at ");
    report_fatal_error(O.str());
  }
}

// Bit-reverse Load Intrinsic: Check if the instruction is a bit reverse load
// intrinsic.
static bool isBrevLdIntrinsic(const Value *Inst) {
  unsigned ID = cast<IntrinsicInst>(Inst)->getIntrinsicID();
  return (ID == Intrinsic::hexagon_L2_loadrd_pbr ||
          ID == Intrinsic::hexagon_L2_loadri_pbr ||
          ID == Intrinsic::hexagon_L2_loadrh_pbr ||
          ID == Intrinsic::hexagon_L2_loadruh_pbr ||
          ID == Intrinsic::hexagon_L2_loadrb_pbr ||
          ID == Intrinsic::hexagon_L2_loadrub_pbr);
}

// Bit-reverse Load Intrinsic :Crawl up and figure out the object from previous
// instruction. So far we only handle bitcast, extract value and bit reverse
// load intrinsic instructions. Should we handle CGEP ?
static Value *getBrevLdObject(Value *V) {
  if (Operator::getOpcode(V) == Instruction::ExtractValue ||
      Operator::getOpcode(V) == Instruction::BitCast)
    V = cast<Operator>(V)->getOperand(0);
  else if (isa<IntrinsicInst>(V) && isBrevLdIntrinsic(V))
    V = cast<Instruction>(V)->getOperand(0);
  return V;
}

// Bit-reverse Load Intrinsic: For a PHI Node return either an incoming edge or
// a back edge. If the back edge comes from the intrinsic itself, the incoming
// edge is returned.
static Value *returnEdge(const PHINode *PN, Value *IntrBaseVal) {
  const BasicBlock *Parent = PN->getParent();
  int Idx = -1;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
    BasicBlock *Blk = PN->getIncomingBlock(i);
    // Determine if the back edge is originated from intrinsic.
    if (Blk == Parent) {
      Value *BackEdgeVal = PN->getIncomingValue(i);
      Value *BaseVal;
      // Loop over till we return the same Value or we hit the IntrBaseVal.
      do {
        BaseVal = BackEdgeVal;
        BackEdgeVal = getBrevLdObject(BackEdgeVal);
      } while ((BaseVal != BackEdgeVal) && (IntrBaseVal != BackEdgeVal));
      // If the getBrevLdObject returns IntrBaseVal, we should return the
      // incoming edge.
      if (IntrBaseVal == BackEdgeVal)
        continue;
      Idx = i;
      break;
    } else // Set the node to incoming edge.
      Idx = i;
  }
  assert(Idx >= 0 && "Unexpected index to incoming argument in PHI");
  return PN->getIncomingValue(Idx);
}

// Bit-reverse Load Intrinsic: Figure out the underlying object the base
// pointer points to, for the bit-reverse load intrinsic. Setting this to
// memoperand might help alias analysis to figure out the dependencies.
static Value *getUnderLyingObjectForBrevLdIntr(Value *V) {
  Value *IntrBaseVal = V;
  Value *BaseVal;
  // Loop over till we return the same Value, implies we either figure out
  // the object or we hit a PHI
  do {
    BaseVal = V;
    V = getBrevLdObject(V);
  } while (BaseVal != V);

  // Identify the object from PHINode.
  if (const PHINode *PN = dyn_cast<PHINode>(V))
    return returnEdge(PN, IntrBaseVal);
  // For non PHI nodes, the object is the last value returned by getBrevLdObject
  else
    return V;
}

/// Given an intrinsic, checks if on the target the intrinsic will need to map
/// to a MemIntrinsicNode (touches memory). If this is the case, it returns
/// true and store the intrinsic information into the IntrinsicInfo that was
/// passed to the function.
bool HexagonTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                               const CallInst &I,
                                               MachineFunction &MF,
                                               unsigned Intrinsic) const {
  switch (Intrinsic) {
  case Intrinsic::hexagon_L2_loadrd_pbr:
  case Intrinsic::hexagon_L2_loadri_pbr:
  case Intrinsic::hexagon_L2_loadrh_pbr:
  case Intrinsic::hexagon_L2_loadruh_pbr:
  case Intrinsic::hexagon_L2_loadrb_pbr:
  case Intrinsic::hexagon_L2_loadrub_pbr: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    auto &Cont = I.getCalledFunction()->getParent()->getContext();
    // The intrinsic function call is of the form { ElTy, i8* }
    // @llvm.hexagon.L2.loadXX.pbr(i8*, i32). The pointer and memory access type
    // should be derived from ElTy.
    Type *ElTy = I.getCalledFunction()->getReturnType()->getStructElementType(0);
    Info.memVT = MVT::getVT(ElTy);
    llvm::Value *BasePtrVal = I.getOperand(0);
    Info.ptrVal = getUnderLyingObjectForBrevLdIntr(BasePtrVal);
    // The offset value comes through Modifier register. For now, assume the
    // offset is 0.
    Info.offset = 0;
    Info.align = DL.getABITypeAlignment(Info.memVT.getTypeForEVT(Cont));
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::hexagon_V6_vgathermw:
  case Intrinsic::hexagon_V6_vgathermw_128B:
  case Intrinsic::hexagon_V6_vgathermh:
  case Intrinsic::hexagon_V6_vgathermh_128B:
  case Intrinsic::hexagon_V6_vgathermhw:
  case Intrinsic::hexagon_V6_vgathermhw_128B:
  case Intrinsic::hexagon_V6_vgathermwq:
  case Intrinsic::hexagon_V6_vgathermwq_128B:
  case Intrinsic::hexagon_V6_vgathermhq:
  case Intrinsic::hexagon_V6_vgathermhq_128B:
  case Intrinsic::hexagon_V6_vgathermhwq:
  case Intrinsic::hexagon_V6_vgathermhwq_128B: {
    const Module &M = *I.getParent()->getParent()->getParent();
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Type *VecTy = I.getArgOperand(1)->getType();
    Info.memVT = MVT::getVT(VecTy);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = M.getDataLayout().getTypeAllocSizeInBits(VecTy) / 8;
    Info.flags = MachineMemOperand::MOLoad |
                 MachineMemOperand::MOStore |
                 MachineMemOperand::MOVolatile;
    return true;
  }
  default:
    break;
  }
  return false;
}

bool HexagonTargetLowering::isTruncateFree(Type *Ty1, Type *Ty2) const {
  return isTruncateFree(EVT::getEVT(Ty1), EVT::getEVT(Ty2));
}

bool HexagonTargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (!VT1.isSimple() || !VT2.isSimple())
    return false;
  return VT1.getSimpleVT() == MVT::i64 && VT2.getSimpleVT() == MVT::i32;
}

bool HexagonTargetLowering::isFMAFasterThanFMulAndFAdd(EVT VT) const {
  return isOperationLegalOrCustom(ISD::FMA, VT);
}

// Should we expand the build vector with shuffles?
bool HexagonTargetLowering::shouldExpandBuildVectorWithShuffles(EVT VT,
      unsigned DefinedValues) const {
  return false;
}

bool HexagonTargetLowering::isShuffleMaskLegal(ArrayRef<int> Mask,
                                               EVT VT) const {
  return true;
}

TargetLoweringBase::LegalizeTypeAction
HexagonTargetLowering::getPreferredVectorAction(MVT VT) const {
  if (VT.getVectorNumElements() == 1)
    return TargetLoweringBase::TypeScalarizeVector;

  // Always widen vectors of i1.
  MVT ElemTy = VT.getVectorElementType();
  if (ElemTy == MVT::i1)
    return TargetLoweringBase::TypeWidenVector;

  if (Subtarget.useHVXOps()) {
    // If the size of VT is at least half of the vector length,
    // widen the vector. Note: the threshold was not selected in
    // any scientific way.
    ArrayRef<MVT> Tys = Subtarget.getHVXElementTypes();
    if (llvm::find(Tys, ElemTy) != Tys.end()) {
      unsigned HwWidth = 8*Subtarget.getVectorLength();
      unsigned VecWidth = VT.getSizeInBits();
      if (VecWidth >= HwWidth/2 && VecWidth < HwWidth)
        return TargetLoweringBase::TypeWidenVector;
    }
  }
  return TargetLoweringBase::TypeSplitVector;
}

std::pair<SDValue, int>
HexagonTargetLowering::getBaseAndOffset(SDValue Addr) const {
  if (Addr.getOpcode() == ISD::ADD) {
    SDValue Op1 = Addr.getOperand(1);
    if (auto *CN = dyn_cast<const ConstantSDNode>(Op1.getNode()))
      return { Addr.getOperand(0), CN->getSExtValue() };
  }
  return { Addr, 0 };
}

// Lower a vector shuffle (V1, V2, V3).  V1 and V2 are the two vectors
// to select data from, V3 is the permutation.
SDValue
HexagonTargetLowering::LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG)
      const {
  const auto *SVN = cast<ShuffleVectorSDNode>(Op);
  ArrayRef<int> AM = SVN->getMask();
  assert(AM.size() <= 8 && "Unexpected shuffle mask");
  unsigned VecLen = AM.size();

  MVT VecTy = ty(Op);
  assert(!Subtarget.isHVXVectorType(VecTy, true) &&
         "HVX shuffles should be legal");
  assert(VecTy.getSizeInBits() <= 64 && "Unexpected vector length");

  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  const SDLoc &dl(Op);

  // If the inputs are not the same as the output, bail. This is not an
  // error situation, but complicates the handling and the default expansion
  // (into BUILD_VECTOR) should be adequate.
  if (ty(Op0) != VecTy || ty(Op1) != VecTy)
    return SDValue();

  // Normalize the mask so that the first non-negative index comes from
  // the first operand.
  SmallVector<int,8> Mask(AM.begin(), AM.end());
  unsigned F = llvm::find_if(AM, [](int M) { return M >= 0; }) - AM.data();
  if (F == AM.size())
    return DAG.getUNDEF(VecTy);
  if (AM[F] >= int(VecLen)) {
    ShuffleVectorSDNode::commuteMask(Mask);
    std::swap(Op0, Op1);
  }

  // Express the shuffle mask in terms of bytes.
  SmallVector<int,8> ByteMask;
  unsigned ElemBytes = VecTy.getVectorElementType().getSizeInBits() / 8;
  for (unsigned i = 0, e = Mask.size(); i != e; ++i) {
    int M = Mask[i];
    if (M < 0) {
      for (unsigned j = 0; j != ElemBytes; ++j)
        ByteMask.push_back(-1);
    } else {
      for (unsigned j = 0; j != ElemBytes; ++j)
        ByteMask.push_back(M*ElemBytes + j);
    }
  }
  assert(ByteMask.size() <= 8);

  // All non-undef (non-negative) indexes are well within [0..127], so they
  // fit in a single byte. Build two 64-bit words:
  // - MaskIdx where each byte is the corresponding index (for non-negative
  //   indexes), and 0xFF for negative indexes, and
  // - MaskUnd that has 0xFF for each negative index.
  uint64_t MaskIdx = 0;
  uint64_t MaskUnd = 0;
  for (unsigned i = 0, e = ByteMask.size(); i != e; ++i) {
    unsigned S = 8*i;
    uint64_t M = ByteMask[i] & 0xFF;
    if (M == 0xFF)
      MaskUnd |= M << S;
    MaskIdx |= M << S;
  }

  if (ByteMask.size() == 4) {
    // Identity.
    if (MaskIdx == (0x03020100 | MaskUnd))
      return Op0;
    // Byte swap.
    if (MaskIdx == (0x00010203 | MaskUnd)) {
      SDValue T0 = DAG.getBitcast(MVT::i32, Op0);
      SDValue T1 = DAG.getNode(ISD::BSWAP, dl, MVT::i32, T0);
      return DAG.getBitcast(VecTy, T1);
    }

    // Byte packs.
    SDValue Concat10 = DAG.getNode(HexagonISD::COMBINE, dl,
                                   typeJoin({ty(Op1), ty(Op0)}), {Op1, Op0});
    if (MaskIdx == (0x06040200 | MaskUnd))
      return getInstr(Hexagon::S2_vtrunehb, dl, VecTy, {Concat10}, DAG);
    if (MaskIdx == (0x07050301 | MaskUnd))
      return getInstr(Hexagon::S2_vtrunohb, dl, VecTy, {Concat10}, DAG);

    SDValue Concat01 = DAG.getNode(HexagonISD::COMBINE, dl,
                                   typeJoin({ty(Op0), ty(Op1)}), {Op0, Op1});
    if (MaskIdx == (0x02000604 | MaskUnd))
      return getInstr(Hexagon::S2_vtrunehb, dl, VecTy, {Concat01}, DAG);
    if (MaskIdx == (0x03010705 | MaskUnd))
      return getInstr(Hexagon::S2_vtrunohb, dl, VecTy, {Concat01}, DAG);
  }

  if (ByteMask.size() == 8) {
    // Identity.
    if (MaskIdx == (0x0706050403020100ull | MaskUnd))
      return Op0;
    // Byte swap.
    if (MaskIdx == (0x0001020304050607ull | MaskUnd)) {
      SDValue T0 = DAG.getBitcast(MVT::i64, Op0);
      SDValue T1 = DAG.getNode(ISD::BSWAP, dl, MVT::i64, T0);
      return DAG.getBitcast(VecTy, T1);
    }

    // Halfword picks.
    if (MaskIdx == (0x0d0c050409080100ull | MaskUnd))
      return getInstr(Hexagon::S2_shuffeh, dl, VecTy, {Op1, Op0}, DAG);
    if (MaskIdx == (0x0f0e07060b0a0302ull | MaskUnd))
      return getInstr(Hexagon::S2_shuffoh, dl, VecTy, {Op1, Op0}, DAG);
    if (MaskIdx == (0x0d0c090805040100ull | MaskUnd))
      return getInstr(Hexagon::S2_vtrunewh, dl, VecTy, {Op1, Op0}, DAG);
    if (MaskIdx == (0x0f0e0b0a07060302ull | MaskUnd))
      return getInstr(Hexagon::S2_vtrunowh, dl, VecTy, {Op1, Op0}, DAG);
    if (MaskIdx == (0x0706030205040100ull | MaskUnd)) {
      VectorPair P = opSplit(Op0, dl, DAG);
      return getInstr(Hexagon::S2_packhl, dl, VecTy, {P.second, P.first}, DAG);
    }

    // Byte packs.
    if (MaskIdx == (0x0e060c040a020800ull | MaskUnd))
      return getInstr(Hexagon::S2_shuffeb, dl, VecTy, {Op1, Op0}, DAG);
    if (MaskIdx == (0x0f070d050b030901ull | MaskUnd))
      return getInstr(Hexagon::S2_shuffob, dl, VecTy, {Op1, Op0}, DAG);
  }

  return SDValue();
}

// Create a Hexagon-specific node for shifting a vector by an integer.
SDValue
HexagonTargetLowering::getVectorShiftByInt(SDValue Op, SelectionDAG &DAG)
      const {
  if (auto *BVN = dyn_cast<BuildVectorSDNode>(Op.getOperand(1).getNode())) {
    if (SDValue S = BVN->getSplatValue()) {
      unsigned NewOpc;
      switch (Op.getOpcode()) {
        case ISD::SHL:
          NewOpc = HexagonISD::VASL;
          break;
        case ISD::SRA:
          NewOpc = HexagonISD::VASR;
          break;
        case ISD::SRL:
          NewOpc = HexagonISD::VLSR;
          break;
        default:
          llvm_unreachable("Unexpected shift opcode");
      }
      return DAG.getNode(NewOpc, SDLoc(Op), ty(Op), Op.getOperand(0), S);
    }
  }

  return SDValue();
}

SDValue
HexagonTargetLowering::LowerVECTOR_SHIFT(SDValue Op, SelectionDAG &DAG) const {
  return getVectorShiftByInt(Op, DAG);
}

SDValue
HexagonTargetLowering::LowerROTL(SDValue Op, SelectionDAG &DAG) const {
  if (isa<ConstantSDNode>(Op.getOperand(1).getNode()))
    return Op;
  return SDValue();
}

SDValue
HexagonTargetLowering::LowerBITCAST(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT InpTy = ty(InpV);
  assert(ResTy.getSizeInBits() == InpTy.getSizeInBits());
  const SDLoc &dl(Op);

  // Handle conversion from i8 to v8i1.
  if (ResTy == MVT::v8i1) {
    SDValue Sc = DAG.getBitcast(tyScalar(InpTy), InpV);
    SDValue Ext = DAG.getZExtOrTrunc(Sc, dl, MVT::i32);
    return getInstr(Hexagon::C2_tfrrp, dl, ResTy, Ext, DAG);
  }

  return SDValue();
}

bool
HexagonTargetLowering::getBuildVectorConstInts(ArrayRef<SDValue> Values,
      MVT VecTy, SelectionDAG &DAG,
      MutableArrayRef<ConstantInt*> Consts) const {
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();
  IntegerType *IntTy = IntegerType::get(*DAG.getContext(), ElemWidth);
  bool AllConst = true;

  for (unsigned i = 0, e = Values.size(); i != e; ++i) {
    SDValue V = Values[i];
    if (V.isUndef()) {
      Consts[i] = ConstantInt::get(IntTy, 0);
      continue;
    }
    // Make sure to always cast to IntTy.
    if (auto *CN = dyn_cast<ConstantSDNode>(V.getNode())) {
      const ConstantInt *CI = CN->getConstantIntValue();
      Consts[i] = ConstantInt::get(IntTy, CI->getValue().getSExtValue());
    } else if (auto *CN = dyn_cast<ConstantFPSDNode>(V.getNode())) {
      const ConstantFP *CF = CN->getConstantFPValue();
      APInt A = CF->getValueAPF().bitcastToAPInt();
      Consts[i] = ConstantInt::get(IntTy, A.getZExtValue());
    } else {
      AllConst = false;
    }
  }
  return AllConst;
}

SDValue
HexagonTargetLowering::buildVector32(ArrayRef<SDValue> Elem, const SDLoc &dl,
                                     MVT VecTy, SelectionDAG &DAG) const {
  MVT ElemTy = VecTy.getVectorElementType();
  assert(VecTy.getVectorNumElements() == Elem.size());

  SmallVector<ConstantInt*,4> Consts(Elem.size());
  bool AllConst = getBuildVectorConstInts(Elem, VecTy, DAG, Consts);

  unsigned First, Num = Elem.size();
  for (First = 0; First != Num; ++First)
    if (!isUndef(Elem[First]))
      break;
  if (First == Num)
    return DAG.getUNDEF(VecTy);

  if (AllConst &&
      llvm::all_of(Consts, [](ConstantInt *CI) { return CI->isZero(); }))
    return getZero(dl, VecTy, DAG);

  if (ElemTy == MVT::i16) {
    assert(Elem.size() == 2);
    if (AllConst) {
      uint32_t V = (Consts[0]->getZExtValue() & 0xFFFF) |
                   Consts[1]->getZExtValue() << 16;
      return DAG.getBitcast(MVT::v2i16, DAG.getConstant(V, dl, MVT::i32));
    }
    SDValue N = getInstr(Hexagon::A2_combine_ll, dl, MVT::i32,
                         {Elem[1], Elem[0]}, DAG);
    return DAG.getBitcast(MVT::v2i16, N);
  }

  if (ElemTy == MVT::i8) {
    // First try generating a constant.
    if (AllConst) {
      int32_t V = (Consts[0]->getZExtValue() & 0xFF) |
                  (Consts[1]->getZExtValue() & 0xFF) << 8 |
                  (Consts[1]->getZExtValue() & 0xFF) << 16 |
                  Consts[2]->getZExtValue() << 24;
      return DAG.getBitcast(MVT::v4i8, DAG.getConstant(V, dl, MVT::i32));
    }

    // Then try splat.
    bool IsSplat = true;
    for (unsigned i = 0; i != Num; ++i) {
      if (i == First)
        continue;
      if (Elem[i] == Elem[First] || isUndef(Elem[i]))
        continue;
      IsSplat = false;
      break;
    }
    if (IsSplat) {
      // Legalize the operand to VSPLAT.
      SDValue Ext = DAG.getZExtOrTrunc(Elem[First], dl, MVT::i32);
      return DAG.getNode(HexagonISD::VSPLAT, dl, VecTy, Ext);
    }

    // Generate
    //   (zxtb(Elem[0]) | (zxtb(Elem[1]) << 8)) |
    //   (zxtb(Elem[2]) | (zxtb(Elem[3]) << 8)) << 16
    assert(Elem.size() == 4);
    SDValue Vs[4];
    for (unsigned i = 0; i != 4; ++i) {
      Vs[i] = DAG.getZExtOrTrunc(Elem[i], dl, MVT::i32);
      Vs[i] = DAG.getZeroExtendInReg(Vs[i], dl, MVT::i8);
    }
    SDValue S8 = DAG.getConstant(8, dl, MVT::i32);
    SDValue T0 = DAG.getNode(ISD::SHL, dl, MVT::i32, {Vs[1], S8});
    SDValue T1 = DAG.getNode(ISD::SHL, dl, MVT::i32, {Vs[3], S8});
    SDValue B0 = DAG.getNode(ISD::OR, dl, MVT::i32, {Vs[0], T0});
    SDValue B1 = DAG.getNode(ISD::OR, dl, MVT::i32, {Vs[2], T1});

    SDValue R = getInstr(Hexagon::A2_combine_ll, dl, MVT::i32, {B1, B0}, DAG);
    return DAG.getBitcast(MVT::v4i8, R);
  }

#ifndef NDEBUG
  dbgs() << "VecTy: " << EVT(VecTy).getEVTString() << '\n';
#endif
  llvm_unreachable("Unexpected vector element type");
}

SDValue
HexagonTargetLowering::buildVector64(ArrayRef<SDValue> Elem, const SDLoc &dl,
                                     MVT VecTy, SelectionDAG &DAG) const {
  MVT ElemTy = VecTy.getVectorElementType();
  assert(VecTy.getVectorNumElements() == Elem.size());

  SmallVector<ConstantInt*,8> Consts(Elem.size());
  bool AllConst = getBuildVectorConstInts(Elem, VecTy, DAG, Consts);

  unsigned First, Num = Elem.size();
  for (First = 0; First != Num; ++First)
    if (!isUndef(Elem[First]))
      break;
  if (First == Num)
    return DAG.getUNDEF(VecTy);

  if (AllConst &&
      llvm::all_of(Consts, [](ConstantInt *CI) { return CI->isZero(); }))
    return getZero(dl, VecTy, DAG);

  // First try splat if possible.
  if (ElemTy == MVT::i16) {
    bool IsSplat = true;
    for (unsigned i = 0; i != Num; ++i) {
      if (i == First)
        continue;
      if (Elem[i] == Elem[First] || isUndef(Elem[i]))
        continue;
      IsSplat = false;
      break;
    }
    if (IsSplat) {
      // Legalize the operand to VSPLAT.
      SDValue Ext = DAG.getZExtOrTrunc(Elem[First], dl, MVT::i32);
      return DAG.getNode(HexagonISD::VSPLAT, dl, VecTy, Ext);
    }
  }

  // Then try constant.
  if (AllConst) {
    uint64_t Val = 0;
    unsigned W = ElemTy.getSizeInBits();
    uint64_t Mask = (ElemTy == MVT::i8)  ? 0xFFull
                  : (ElemTy == MVT::i16) ? 0xFFFFull : 0xFFFFFFFFull;
    for (unsigned i = 0; i != Num; ++i)
      Val = (Val << W) | (Consts[Num-1-i]->getZExtValue() & Mask);
    SDValue V0 = DAG.getConstant(Val, dl, MVT::i64);
    return DAG.getBitcast(VecTy, V0);
  }

  // Build two 32-bit vectors and concatenate.
  MVT HalfTy = MVT::getVectorVT(ElemTy, Num/2);
  SDValue L = (ElemTy == MVT::i32)
                ? Elem[0]
                : buildVector32(Elem.take_front(Num/2), dl, HalfTy, DAG);
  SDValue H = (ElemTy == MVT::i32)
                ? Elem[1]
                : buildVector32(Elem.drop_front(Num/2), dl, HalfTy, DAG);
  return DAG.getNode(HexagonISD::COMBINE, dl, VecTy, {H, L});
}

SDValue
HexagonTargetLowering::extractVector(SDValue VecV, SDValue IdxV,
                                     const SDLoc &dl, MVT ValTy, MVT ResTy,
                                     SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  assert(!ValTy.isVector() ||
         VecTy.getVectorElementType() == ValTy.getVectorElementType());
  unsigned VecWidth = VecTy.getSizeInBits();
  unsigned ValWidth = ValTy.getSizeInBits();
  unsigned ElemWidth = VecTy.getVectorElementType().getSizeInBits();
  assert((VecWidth % ElemWidth) == 0);
  auto *IdxN = dyn_cast<ConstantSDNode>(IdxV);

  // Special case for v{8,4,2}i1 (the only boolean vectors legal in Hexagon
  // without any coprocessors).
  if (ElemWidth == 1) {
    assert(VecWidth == VecTy.getVectorNumElements() && "Sanity failure");
    assert(VecWidth == 8 || VecWidth == 4 || VecWidth == 2);
    // Check if this is an extract of the lowest bit.
    if (IdxN) {
      // Extracting the lowest bit is a no-op, but it changes the type,
      // so it must be kept as an operation to avoid errors related to
      // type mismatches.
      if (IdxN->isNullValue() && ValTy.getSizeInBits() == 1)
        return DAG.getNode(HexagonISD::TYPECAST, dl, MVT::i1, VecV);
    }

    // If the value extracted is a single bit, use tstbit.
    if (ValWidth == 1) {
      SDValue A0 = getInstr(Hexagon::C2_tfrpr, dl, MVT::i32, {VecV}, DAG);
      SDValue M0 = DAG.getConstant(8 / VecWidth, dl, MVT::i32);
      SDValue I0 = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, M0);
      return DAG.getNode(HexagonISD::TSTBIT, dl, MVT::i1, A0, I0);
    }

    // Each bool vector (v2i1, v4i1, v8i1) always occupies 8 bits in
    // a predicate register. The elements of the vector are repeated
    // in the register (if necessary) so that the total number is 8.
    // The extracted subvector will need to be expanded in such a way.
    unsigned Scale = VecWidth / ValWidth;

    // Generate (p2d VecV) >> 8*Idx to move the interesting bytes to
    // position 0.
    assert(ty(IdxV) == MVT::i32);
    unsigned VecRep = 8 / VecWidth;
    SDValue S0 = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                             DAG.getConstant(8*VecRep, dl, MVT::i32));
    SDValue T0 = DAG.getNode(HexagonISD::P2D, dl, MVT::i64, VecV);
    SDValue T1 = DAG.getNode(ISD::SRL, dl, MVT::i64, T0, S0);
    while (Scale > 1) {
      // The longest possible subvector is at most 32 bits, so it is always
      // contained in the low subregister.
      T1 = DAG.getTargetExtractSubreg(Hexagon::isub_lo, dl, MVT::i32, T1);
      T1 = expandPredicate(T1, dl, DAG);
      Scale /= 2;
    }

    return DAG.getNode(HexagonISD::D2P, dl, ResTy, T1);
  }

  assert(VecWidth == 32 || VecWidth == 64);

  // Cast everything to scalar integer types.
  MVT ScalarTy = tyScalar(VecTy);
  VecV = DAG.getBitcast(ScalarTy, VecV);

  SDValue WidthV = DAG.getConstant(ValWidth, dl, MVT::i32);
  SDValue ExtV;

  if (IdxN) {
    unsigned Off = IdxN->getZExtValue() * ElemWidth;
    if (VecWidth == 64 && ValWidth == 32) {
      assert(Off == 0 || Off == 32);
      unsigned SubIdx = Off == 0 ? Hexagon::isub_lo : Hexagon::isub_hi;
      ExtV = DAG.getTargetExtractSubreg(SubIdx, dl, MVT::i32, VecV);
    } else if (Off == 0 && (ValWidth % 8) == 0) {
      ExtV = DAG.getZeroExtendInReg(VecV, dl, tyScalar(ValTy));
    } else {
      SDValue OffV = DAG.getConstant(Off, dl, MVT::i32);
      // The return type of EXTRACTU must be the same as the type of the
      // input vector.
      ExtV = DAG.getNode(HexagonISD::EXTRACTU, dl, ScalarTy,
                         {VecV, WidthV, OffV});
    }
  } else {
    if (ty(IdxV) != MVT::i32)
      IdxV = DAG.getZExtOrTrunc(IdxV, dl, MVT::i32);
    SDValue OffV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                               DAG.getConstant(ElemWidth, dl, MVT::i32));
    ExtV = DAG.getNode(HexagonISD::EXTRACTU, dl, ScalarTy,
                       {VecV, WidthV, OffV});
  }

  // Cast ExtV to the requested result type.
  ExtV = DAG.getZExtOrTrunc(ExtV, dl, tyScalar(ResTy));
  ExtV = DAG.getBitcast(ResTy, ExtV);
  return ExtV;
}

SDValue
HexagonTargetLowering::insertVector(SDValue VecV, SDValue ValV, SDValue IdxV,
                                    const SDLoc &dl, MVT ValTy,
                                    SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  if (VecTy.getVectorElementType() == MVT::i1) {
    MVT ValTy = ty(ValV);
    assert(ValTy.getVectorElementType() == MVT::i1);
    SDValue ValR = DAG.getNode(HexagonISD::P2D, dl, MVT::i64, ValV);
    unsigned VecLen = VecTy.getVectorNumElements();
    unsigned Scale = VecLen / ValTy.getVectorNumElements();
    assert(Scale > 1);

    for (unsigned R = Scale; R > 1; R /= 2) {
      ValR = contractPredicate(ValR, dl, DAG);
      ValR = DAG.getNode(HexagonISD::COMBINE, dl, MVT::i64,
                         DAG.getUNDEF(MVT::i32), ValR);
    }
    // The longest possible subvector is at most 32 bits, so it is always
    // contained in the low subregister.
    ValR = DAG.getTargetExtractSubreg(Hexagon::isub_lo, dl, MVT::i32, ValR);

    unsigned ValBytes = 64 / Scale;
    SDValue Width = DAG.getConstant(ValBytes*8, dl, MVT::i32);
    SDValue Idx = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                              DAG.getConstant(8, dl, MVT::i32));
    SDValue VecR = DAG.getNode(HexagonISD::P2D, dl, MVT::i64, VecV);
    SDValue Ins = DAG.getNode(HexagonISD::INSERT, dl, MVT::i32,
                              {VecR, ValR, Width, Idx});
    return DAG.getNode(HexagonISD::D2P, dl, VecTy, Ins);
  }

  unsigned VecWidth = VecTy.getSizeInBits();
  unsigned ValWidth = ValTy.getSizeInBits();
  assert(VecWidth == 32 || VecWidth == 64);
  assert((VecWidth % ValWidth) == 0);

  // Cast everything to scalar integer types.
  MVT ScalarTy = MVT::getIntegerVT(VecWidth);
  // The actual type of ValV may be different than ValTy (which is related
  // to the vector type).
  unsigned VW = ty(ValV).getSizeInBits();
  ValV = DAG.getBitcast(MVT::getIntegerVT(VW), ValV);
  VecV = DAG.getBitcast(ScalarTy, VecV);
  if (VW != VecWidth)
    ValV = DAG.getAnyExtOrTrunc(ValV, dl, ScalarTy);

  SDValue WidthV = DAG.getConstant(ValWidth, dl, MVT::i32);
  SDValue InsV;

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(IdxV)) {
    unsigned W = C->getZExtValue() * ValWidth;
    SDValue OffV = DAG.getConstant(W, dl, MVT::i32);
    InsV = DAG.getNode(HexagonISD::INSERT, dl, ScalarTy,
                       {VecV, ValV, WidthV, OffV});
  } else {
    if (ty(IdxV) != MVT::i32)
      IdxV = DAG.getZExtOrTrunc(IdxV, dl, MVT::i32);
    SDValue OffV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, WidthV);
    InsV = DAG.getNode(HexagonISD::INSERT, dl, ScalarTy,
                       {VecV, ValV, WidthV, OffV});
  }

  return DAG.getNode(ISD::BITCAST, dl, VecTy, InsV);
}

SDValue
HexagonTargetLowering::expandPredicate(SDValue Vec32, const SDLoc &dl,
                                       SelectionDAG &DAG) const {
  assert(ty(Vec32).getSizeInBits() == 32);
  if (isUndef(Vec32))
    return DAG.getUNDEF(MVT::i64);
  return getInstr(Hexagon::S2_vsxtbh, dl, MVT::i64, {Vec32}, DAG);
}

SDValue
HexagonTargetLowering::contractPredicate(SDValue Vec64, const SDLoc &dl,
                                         SelectionDAG &DAG) const {
  assert(ty(Vec64).getSizeInBits() == 64);
  if (isUndef(Vec64))
    return DAG.getUNDEF(MVT::i32);
  return getInstr(Hexagon::S2_vtrunehb, dl, MVT::i32, {Vec64}, DAG);
}

SDValue
HexagonTargetLowering::getZero(const SDLoc &dl, MVT Ty, SelectionDAG &DAG)
      const {
  if (Ty.isVector()) {
    assert(Ty.isInteger() && "Only integer vectors are supported here");
    unsigned W = Ty.getSizeInBits();
    if (W <= 64)
      return DAG.getBitcast(Ty, DAG.getConstant(0, dl, MVT::getIntegerVT(W)));
    return DAG.getNode(HexagonISD::VZERO, dl, Ty);
  }

  if (Ty.isInteger())
    return DAG.getConstant(0, dl, Ty);
  if (Ty.isFloatingPoint())
    return DAG.getConstantFP(0.0, dl, Ty);
  llvm_unreachable("Invalid type for zero");
}

SDValue
HexagonTargetLowering::LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const {
  MVT VecTy = ty(Op);
  unsigned BW = VecTy.getSizeInBits();
  const SDLoc &dl(Op);
  SmallVector<SDValue,8> Ops;
  for (unsigned i = 0, e = Op.getNumOperands(); i != e; ++i)
    Ops.push_back(Op.getOperand(i));

  if (BW == 32)
    return buildVector32(Ops, dl, VecTy, DAG);
  if (BW == 64)
    return buildVector64(Ops, dl, VecTy, DAG);

  if (VecTy == MVT::v8i1 || VecTy == MVT::v4i1 || VecTy == MVT::v2i1) {
    // For each i1 element in the resulting predicate register, put 1
    // shifted by the index of the element into a general-purpose register,
    // then or them together and transfer it back into a predicate register.
    SDValue Rs[8];
    SDValue Z = getZero(dl, MVT::i32, DAG);
    // Always produce 8 bits, repeat inputs if necessary.
    unsigned Rep = 8 / VecTy.getVectorNumElements();
    for (unsigned i = 0; i != 8; ++i) {
      SDValue S = DAG.getConstant(1ull << i, dl, MVT::i32);
      Rs[i] = DAG.getSelect(dl, MVT::i32, Ops[i/Rep], S, Z);
    }
    for (ArrayRef<SDValue> A(Rs); A.size() != 1; A = A.drop_back(A.size()/2)) {
      for (unsigned i = 0, e = A.size()/2; i != e; ++i)
        Rs[i] = DAG.getNode(ISD::OR, dl, MVT::i32, Rs[2*i], Rs[2*i+1]);
    }
    // Move the value directly to a predicate register.
    return getInstr(Hexagon::C2_tfrrp, dl, VecTy, {Rs[0]}, DAG);
  }

  return SDValue();
}

SDValue
HexagonTargetLowering::LowerCONCAT_VECTORS(SDValue Op,
                                           SelectionDAG &DAG) const {
  MVT VecTy = ty(Op);
  const SDLoc &dl(Op);
  if (VecTy.getSizeInBits() == 64) {
    assert(Op.getNumOperands() == 2);
    return DAG.getNode(HexagonISD::COMBINE, dl, VecTy, Op.getOperand(1),
                       Op.getOperand(0));
  }

  MVT ElemTy = VecTy.getVectorElementType();
  if (ElemTy == MVT::i1) {
    assert(VecTy == MVT::v2i1 || VecTy == MVT::v4i1 || VecTy == MVT::v8i1);
    MVT OpTy = ty(Op.getOperand(0));
    // Scale is how many times the operands need to be contracted to match
    // the representation in the target register.
    unsigned Scale = VecTy.getVectorNumElements() / OpTy.getVectorNumElements();
    assert(Scale == Op.getNumOperands() && Scale > 1);

    // First, convert all bool vectors to integers, then generate pairwise
    // inserts to form values of doubled length. Up until there are only
    // two values left to concatenate, all of these values will fit in a
    // 32-bit integer, so keep them as i32 to use 32-bit inserts.
    SmallVector<SDValue,4> Words[2];
    unsigned IdxW = 0;

    for (SDValue P : Op.getNode()->op_values()) {
      SDValue W = DAG.getNode(HexagonISD::P2D, dl, MVT::i64, P);
      for (unsigned R = Scale; R > 1; R /= 2) {
        W = contractPredicate(W, dl, DAG);
        W = DAG.getNode(HexagonISD::COMBINE, dl, MVT::i64,
                        DAG.getUNDEF(MVT::i32), W);
      }
      W = DAG.getTargetExtractSubreg(Hexagon::isub_lo, dl, MVT::i32, W);
      Words[IdxW].push_back(W);
    }

    while (Scale > 2) {
      SDValue WidthV = DAG.getConstant(64 / Scale, dl, MVT::i32);
      Words[IdxW ^ 1].clear();

      for (unsigned i = 0, e = Words[IdxW].size(); i != e; i += 2) {
        SDValue W0 = Words[IdxW][i], W1 = Words[IdxW][i+1];
        // Insert W1 into W0 right next to the significant bits of W0.
        SDValue T = DAG.getNode(HexagonISD::INSERT, dl, MVT::i32,
                                {W0, W1, WidthV, WidthV});
        Words[IdxW ^ 1].push_back(T);
      }
      IdxW ^= 1;
      Scale /= 2;
    }

    // Another sanity check. At this point there should only be two words
    // left, and Scale should be 2.
    assert(Scale == 2 && Words[IdxW].size() == 2);

    SDValue WW = DAG.getNode(HexagonISD::COMBINE, dl, MVT::i64,
                             Words[IdxW][1], Words[IdxW][0]);
    return DAG.getNode(HexagonISD::D2P, dl, VecTy, WW);
  }

  return SDValue();
}

SDValue
HexagonTargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDValue Vec = Op.getOperand(0);
  MVT ElemTy = ty(Vec).getVectorElementType();
  return extractVector(Vec, Op.getOperand(1), SDLoc(Op), ElemTy, ty(Op), DAG);
}

SDValue
HexagonTargetLowering::LowerEXTRACT_SUBVECTOR(SDValue Op,
                                              SelectionDAG &DAG) const {
  return extractVector(Op.getOperand(0), Op.getOperand(1), SDLoc(Op),
                       ty(Op), ty(Op), DAG);
}

SDValue
HexagonTargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                              SelectionDAG &DAG) const {
  return insertVector(Op.getOperand(0), Op.getOperand(1), Op.getOperand(2),
                      SDLoc(Op), ty(Op).getVectorElementType(), DAG);
}

SDValue
HexagonTargetLowering::LowerINSERT_SUBVECTOR(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDValue ValV = Op.getOperand(1);
  return insertVector(Op.getOperand(0), ValV, Op.getOperand(2),
                      SDLoc(Op), ty(ValV), DAG);
}

bool
HexagonTargetLowering::allowTruncateForTailCall(Type *Ty1, Type *Ty2) const {
  // Assuming the caller does not have either a signext or zeroext modifier, and
  // only one value is accepted, any reasonable truncation is allowed.
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;

  // FIXME: in principle up to 64-bit could be made safe, but it would be very
  // fragile at the moment: any support for multiple value returns would be
  // liable to disallow tail calls involving i64 -> iN truncation in many cases.
  return Ty1->getPrimitiveSizeInBits() <= 32;
}

SDValue
HexagonTargetLowering::LowerLoad(SDValue Op, SelectionDAG &DAG) const {
  LoadSDNode *LN = cast<LoadSDNode>(Op.getNode());
  unsigned ClaimAlign = LN->getAlignment();
  validateConstPtrAlignment(LN->getBasePtr(), SDLoc(Op), ClaimAlign);
  // Call LowerUnalignedLoad for all loads, it recognizes loads that
  // don't need extra aligning.
  return LowerUnalignedLoad(Op, DAG);
}

SDValue
HexagonTargetLowering::LowerStore(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *SN = cast<StoreSDNode>(Op.getNode());
  unsigned ClaimAlign = SN->getAlignment();
  SDValue Ptr = SN->getBasePtr();
  const SDLoc &dl(Op);
  validateConstPtrAlignment(Ptr, dl, ClaimAlign);

  MVT StoreTy = SN->getMemoryVT().getSimpleVT();
  unsigned NeedAlign = Subtarget.getTypeAlignment(StoreTy);
  if (ClaimAlign < NeedAlign)
    return expandUnalignedStore(SN, DAG);
  return Op;
}

SDValue
HexagonTargetLowering::LowerUnalignedLoad(SDValue Op, SelectionDAG &DAG)
      const {
  LoadSDNode *LN = cast<LoadSDNode>(Op.getNode());
  MVT LoadTy = ty(Op);
  unsigned NeedAlign = Subtarget.getTypeAlignment(LoadTy);
  unsigned HaveAlign = LN->getAlignment();
  if (HaveAlign >= NeedAlign)
    return Op;

  const SDLoc &dl(Op);
  const DataLayout &DL = DAG.getDataLayout();
  LLVMContext &Ctx = *DAG.getContext();
  unsigned AS = LN->getAddressSpace();

  // If the load aligning is disabled or the load can be broken up into two
  // smaller legal loads, do the default (target-independent) expansion.
  bool DoDefault = false;
  // Handle it in the default way if this is an indexed load.
  if (!LN->isUnindexed())
    DoDefault = true;

  if (!AlignLoads) {
    if (allowsMemoryAccess(Ctx, DL, LN->getMemoryVT(), AS, HaveAlign))
      return Op;
    DoDefault = true;
  }
  if (!DoDefault && 2*HaveAlign == NeedAlign) {
    // The PartTy is the equivalent of "getLoadableTypeOfSize(HaveAlign)".
    MVT PartTy = HaveAlign <= 8 ? MVT::getIntegerVT(8*HaveAlign)
                                : MVT::getVectorVT(MVT::i8, HaveAlign);
    DoDefault = allowsMemoryAccess(Ctx, DL, PartTy, AS, HaveAlign);
  }
  if (DoDefault) {
    std::pair<SDValue, SDValue> P = expandUnalignedLoad(LN, DAG);
    return DAG.getMergeValues({P.first, P.second}, dl);
  }

  // The code below generates two loads, both aligned as NeedAlign, and
  // with the distance of NeedAlign between them. For that to cover the
  // bits that need to be loaded (and without overlapping), the size of
  // the loads should be equal to NeedAlign. This is true for all loadable
  // types, but add an assertion in case something changes in the future.
  assert(LoadTy.getSizeInBits() == 8*NeedAlign);

  unsigned LoadLen = NeedAlign;
  SDValue Base = LN->getBasePtr();
  SDValue Chain = LN->getChain();
  auto BO = getBaseAndOffset(Base);
  unsigned BaseOpc = BO.first.getOpcode();
  if (BaseOpc == HexagonISD::VALIGNADDR && BO.second % LoadLen == 0)
    return Op;

  if (BO.second % LoadLen != 0) {
    BO.first = DAG.getNode(ISD::ADD, dl, MVT::i32, BO.first,
                           DAG.getConstant(BO.second % LoadLen, dl, MVT::i32));
    BO.second -= BO.second % LoadLen;
  }
  SDValue BaseNoOff = (BaseOpc != HexagonISD::VALIGNADDR)
      ? DAG.getNode(HexagonISD::VALIGNADDR, dl, MVT::i32, BO.first,
                    DAG.getConstant(NeedAlign, dl, MVT::i32))
      : BO.first;
  SDValue Base0 = DAG.getMemBasePlusOffset(BaseNoOff, BO.second, dl);
  SDValue Base1 = DAG.getMemBasePlusOffset(BaseNoOff, BO.second+LoadLen, dl);

  MachineMemOperand *WideMMO = nullptr;
  if (MachineMemOperand *MMO = LN->getMemOperand()) {
    MachineFunction &MF = DAG.getMachineFunction();
    WideMMO = MF.getMachineMemOperand(MMO->getPointerInfo(), MMO->getFlags(),
                    2*LoadLen, LoadLen, MMO->getAAInfo(), MMO->getRanges(),
                    MMO->getSyncScopeID(), MMO->getOrdering(),
                    MMO->getFailureOrdering());
  }

  SDValue Load0 = DAG.getLoad(LoadTy, dl, Chain, Base0, WideMMO);
  SDValue Load1 = DAG.getLoad(LoadTy, dl, Chain, Base1, WideMMO);

  SDValue Aligned = DAG.getNode(HexagonISD::VALIGN, dl, LoadTy,
                                {Load1, Load0, BaseNoOff.getOperand(0)});
  SDValue NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                 Load0.getValue(1), Load1.getValue(1));
  SDValue M = DAG.getMergeValues({Aligned, NewChain}, dl);
  return M;
}

SDValue
HexagonTargetLowering::LowerAddSubCarry(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  unsigned Opc = Op.getOpcode();
  SDValue X = Op.getOperand(0), Y = Op.getOperand(1), C = Op.getOperand(2);

  if (Opc == ISD::ADDCARRY)
    return DAG.getNode(HexagonISD::ADDC, dl, Op.getNode()->getVTList(),
                       { X, Y, C });

  EVT CarryTy = C.getValueType();
  SDValue SubC = DAG.getNode(HexagonISD::SUBC, dl, Op.getNode()->getVTList(),
                             { X, Y, DAG.getLogicalNOT(dl, C, CarryTy) });
  SDValue Out[] = { SubC.getValue(0),
                    DAG.getLogicalNOT(dl, SubC.getValue(1), CarryTy) };
  return DAG.getMergeValues(Out, dl);
}

SDValue
HexagonTargetLowering::LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain     = Op.getOperand(0);
  SDValue Offset    = Op.getOperand(1);
  SDValue Handler   = Op.getOperand(2);
  SDLoc dl(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Mark function as containing a call to EH_RETURN.
  HexagonMachineFunctionInfo *FuncInfo =
    DAG.getMachineFunction().getInfo<HexagonMachineFunctionInfo>();
  FuncInfo->setHasEHReturn();

  unsigned OffsetReg = Hexagon::R28;

  SDValue StoreAddr =
      DAG.getNode(ISD::ADD, dl, PtrVT, DAG.getRegister(Hexagon::R30, PtrVT),
                  DAG.getIntPtrConstant(4, dl));
  Chain = DAG.getStore(Chain, dl, Handler, StoreAddr, MachinePointerInfo());
  Chain = DAG.getCopyToReg(Chain, dl, OffsetReg, Offset);

  // Not needed we already use it as explict input to EH_RETURN.
  // MF.getRegInfo().addLiveOut(OffsetReg);

  return DAG.getNode(HexagonISD::EH_RETURN, dl, MVT::Other, Chain);
}

SDValue
HexagonTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();

  // Handle INLINEASM first.
  if (Opc == ISD::INLINEASM)
    return LowerINLINEASM(Op, DAG);

  if (isHvxOperation(Op)) {
    // If HVX lowering returns nothing, try the default lowering.
    if (SDValue V = LowerHvxOperation(Op, DAG))
      return V;
  }

  switch (Opc) {
    default:
#ifndef NDEBUG
      Op.getNode()->dumpr(&DAG);
      if (Opc > HexagonISD::OP_BEGIN && Opc < HexagonISD::OP_END)
        errs() << "Error: check for a non-legal type in this operation\n";
#endif
      llvm_unreachable("Should not custom lower this!");
    case ISD::CONCAT_VECTORS:       return LowerCONCAT_VECTORS(Op, DAG);
    case ISD::INSERT_SUBVECTOR:     return LowerINSERT_SUBVECTOR(Op, DAG);
    case ISD::INSERT_VECTOR_ELT:    return LowerINSERT_VECTOR_ELT(Op, DAG);
    case ISD::EXTRACT_SUBVECTOR:    return LowerEXTRACT_SUBVECTOR(Op, DAG);
    case ISD::EXTRACT_VECTOR_ELT:   return LowerEXTRACT_VECTOR_ELT(Op, DAG);
    case ISD::BUILD_VECTOR:         return LowerBUILD_VECTOR(Op, DAG);
    case ISD::VECTOR_SHUFFLE:       return LowerVECTOR_SHUFFLE(Op, DAG);
    case ISD::BITCAST:              return LowerBITCAST(Op, DAG);
    case ISD::LOAD:                 return LowerLoad(Op, DAG);
    case ISD::STORE:                return LowerStore(Op, DAG);
    case ISD::ADDCARRY:
    case ISD::SUBCARRY:             return LowerAddSubCarry(Op, DAG);
    case ISD::SRA:
    case ISD::SHL:
    case ISD::SRL:                  return LowerVECTOR_SHIFT(Op, DAG);
    case ISD::ROTL:                 return LowerROTL(Op, DAG);
    case ISD::ConstantPool:         return LowerConstantPool(Op, DAG);
    case ISD::JumpTable:            return LowerJumpTable(Op, DAG);
    case ISD::EH_RETURN:            return LowerEH_RETURN(Op, DAG);
    case ISD::RETURNADDR:           return LowerRETURNADDR(Op, DAG);
    case ISD::FRAMEADDR:            return LowerFRAMEADDR(Op, DAG);
    case ISD::GlobalTLSAddress:     return LowerGlobalTLSAddress(Op, DAG);
    case ISD::ATOMIC_FENCE:         return LowerATOMIC_FENCE(Op, DAG);
    case ISD::GlobalAddress:        return LowerGLOBALADDRESS(Op, DAG);
    case ISD::BlockAddress:         return LowerBlockAddress(Op, DAG);
    case ISD::GLOBAL_OFFSET_TABLE:  return LowerGLOBAL_OFFSET_TABLE(Op, DAG);
    case ISD::VASTART:              return LowerVASTART(Op, DAG);
    case ISD::DYNAMIC_STACKALLOC:   return LowerDYNAMIC_STACKALLOC(Op, DAG);
    case ISD::SETCC:                return LowerSETCC(Op, DAG);
    case ISD::VSELECT:              return LowerVSELECT(Op, DAG);
    case ISD::INTRINSIC_WO_CHAIN:   return LowerINTRINSIC_WO_CHAIN(Op, DAG);
    case ISD::INTRINSIC_VOID:       return LowerINTRINSIC_VOID(Op, DAG);
    case ISD::PREFETCH:             return LowerPREFETCH(Op, DAG);
    case ISD::READCYCLECOUNTER:     return LowerREADCYCLECOUNTER(Op, DAG);
      break;
  }

  return SDValue();
}

void
HexagonTargetLowering::LowerOperationWrapper(SDNode *N,
                                             SmallVectorImpl<SDValue> &Results,
                                             SelectionDAG &DAG) const {
  // We are only custom-lowering stores to verify the alignment of the
  // address if it is a compile-time constant. Since a store can be modified
  // during type-legalization (the value being stored may need legalization),
  // return empty Results here to indicate that we don't really make any
  // changes in the custom lowering.
  if (N->getOpcode() != ISD::STORE)
    return TargetLowering::LowerOperationWrapper(N, Results, DAG);
}

void
HexagonTargetLowering::ReplaceNodeResults(SDNode *N,
                                          SmallVectorImpl<SDValue> &Results,
                                          SelectionDAG &DAG) const {
  const SDLoc &dl(N);
  switch (N->getOpcode()) {
    case ISD::SRL:
    case ISD::SRA:
    case ISD::SHL:
      return;
    case ISD::BITCAST:
      // Handle a bitcast from v8i1 to i8.
      if (N->getValueType(0) == MVT::i8) {
        SDValue P = getInstr(Hexagon::C2_tfrpr, dl, MVT::i32,
                             N->getOperand(0), DAG);
        Results.push_back(P);
      }
      break;
  }
}

/// Returns relocation base for the given PIC jumptable.
SDValue
HexagonTargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                SelectionDAG &DAG) const {
  int Idx = cast<JumpTableSDNode>(Table)->getIndex();
  EVT VT = Table.getValueType();
  SDValue T = DAG.getTargetJumpTable(Idx, VT, HexagonII::MO_PCREL);
  return DAG.getNode(HexagonISD::AT_PCREL, SDLoc(Table), VT, T);
}

//===----------------------------------------------------------------------===//
// Inline Assembly Support
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
HexagonTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
      case 'q':
      case 'v':
        if (Subtarget.useHVXOps())
          return C_RegisterClass;
        break;
      case 'a':
        return C_RegisterClass;
      default:
        break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass*>
HexagonTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {

  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':   // R0-R31
      switch (VT.SimpleTy) {
      default:
        return {0u, nullptr};
      case MVT::i1:
      case MVT::i8:
      case MVT::i16:
      case MVT::i32:
      case MVT::f32:
        return {0u, &Hexagon::IntRegsRegClass};
      case MVT::i64:
      case MVT::f64:
        return {0u, &Hexagon::DoubleRegsRegClass};
      }
      break;
    case 'a': // M0-M1
      if (VT != MVT::i32)
        return {0u, nullptr};
      return {0u, &Hexagon::ModRegsRegClass};
    case 'q': // q0-q3
      switch (VT.getSizeInBits()) {
      default:
        return {0u, nullptr};
      case 512:
      case 1024:
        return {0u, &Hexagon::HvxQRRegClass};
      }
      break;
    case 'v': // V0-V31
      switch (VT.getSizeInBits()) {
      default:
        return {0u, nullptr};
      case 512:
        return {0u, &Hexagon::HvxVRRegClass};
      case 1024:
        if (Subtarget.hasV60Ops() && Subtarget.useHVX128BOps())
          return {0u, &Hexagon::HvxVRRegClass};
        return {0u, &Hexagon::HvxWRRegClass};
      case 2048:
        return {0u, &Hexagon::HvxWRRegClass};
      }
      break;
    default:
      return {0u, nullptr};
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// isFPImmLegal - Returns true if the target can instruction select the
/// specified FP immediate natively. If false, the legalizer will
/// materialize the FP immediate as a load from a constant pool.
bool HexagonTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  return true;
}

/// isLegalAddressingMode - Return true if the addressing mode represented by
/// AM is legal for this target, for a load/store of the specified type.
bool HexagonTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                  const AddrMode &AM, Type *Ty,
                                                  unsigned AS, Instruction *I) const {
  if (Ty->isSized()) {
    // When LSR detects uses of the same base address to access different
    // types (e.g. unions), it will assume a conservative type for these
    // uses:
    //   LSR Use: Kind=Address of void in addrspace(4294967295), ...
    // The type Ty passed here would then be "void". Skip the alignment
    // checks, but do not return false right away, since that confuses
    // LSR into crashing.
    unsigned A = DL.getABITypeAlignment(Ty);
    // The base offset must be a multiple of the alignment.
    if ((AM.BaseOffs % A) != 0)
      return false;
    // The shifted offset must fit in 11 bits.
    if (!isInt<11>(AM.BaseOffs >> Log2_32(A)))
      return false;
  }

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  int Scale = AM.Scale;
  if (Scale < 0)
    Scale = -Scale;
  switch (Scale) {
  case 0:  // No scale reg, "r+i", "r", or just "i".
    break;
  default: // No scaled addressing mode.
    return false;
  }
  return true;
}

/// Return true if folding a constant offset with the given GlobalAddress is
/// legal.  It is frequently not legal in PIC relocation models.
bool HexagonTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA)
      const {
  return HTM.getRelocationModel() == Reloc::Static;
}

/// isLegalICmpImmediate - Return true if the specified immediate is legal
/// icmp immediate, that is the target has icmp instructions which can compare
/// a register against the immediate without having to materialize the
/// immediate into a register.
bool HexagonTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return Imm >= -512 && Imm <= 511;
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization. Targets which want to do tail call
/// optimization should implement this function.
bool HexagonTargetLowering::IsEligibleForTailCallOptimization(
                                 SDValue Callee,
                                 CallingConv::ID CalleeCC,
                                 bool IsVarArg,
                                 bool IsCalleeStructRet,
                                 bool IsCallerStructRet,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SmallVectorImpl<ISD::InputArg> &Ins,
                                 SelectionDAG& DAG) const {
  const Function &CallerF = DAG.getMachineFunction().getFunction();
  CallingConv::ID CallerCC = CallerF.getCallingConv();
  bool CCMatch = CallerCC == CalleeCC;

  // ***************************************************************************
  //  Look for obvious safe cases to perform tail call optimization that do not
  //  require ABI changes.
  // ***************************************************************************

  // If this is a tail call via a function pointer, then don't do it!
  if (!isa<GlobalAddressSDNode>(Callee) &&
      !isa<ExternalSymbolSDNode>(Callee)) {
    return false;
  }

  // Do not optimize if the calling conventions do not match and the conventions
  // used are not C or Fast.
  if (!CCMatch) {
    bool R = (CallerCC == CallingConv::C || CallerCC == CallingConv::Fast);
    bool E = (CalleeCC == CallingConv::C || CalleeCC == CallingConv::Fast);
    // If R & E, then ok.
    if (!R || !E)
      return false;
  }

  // Do not tail call optimize vararg calls.
  if (IsVarArg)
    return false;

  // Also avoid tail call optimization if either caller or callee uses struct
  // return semantics.
  if (IsCalleeStructRet || IsCallerStructRet)
    return false;

  // In addition to the cases above, we also disable Tail Call Optimization if
  // the calling convention code that at least one outgoing argument needs to
  // go on the stack. We cannot check that here because at this point that
  // information is not available.
  return true;
}

/// Returns the target specific optimal type for load and store operations as
/// a result of memset, memcpy, and memmove lowering.
///
/// If DstAlign is zero that means it's safe to destination alignment can
/// satisfy any constraint. Similarly if SrcAlign is zero it means there isn't
/// a need to check it against alignment requirement, probably because the
/// source does not need to be loaded. If 'IsMemset' is true, that means it's
/// expanding a memset. If 'ZeroMemset' is true, that means it's a memset of
/// zero. 'MemcpyStrSrc' indicates whether the memcpy source is constant so it
/// does not need to be loaded.  It returns EVT::Other if the type should be
/// determined using generic target-independent logic.
EVT HexagonTargetLowering::getOptimalMemOpType(uint64_t Size,
      unsigned DstAlign, unsigned SrcAlign, bool IsMemset, bool ZeroMemset,
      bool MemcpyStrSrc, MachineFunction &MF) const {

  auto Aligned = [](unsigned GivenA, unsigned MinA) -> bool {
    return (GivenA % MinA) == 0;
  };

  if (Size >= 8 && Aligned(DstAlign, 8) && (IsMemset || Aligned(SrcAlign, 8)))
    return MVT::i64;
  if (Size >= 4 && Aligned(DstAlign, 4) && (IsMemset || Aligned(SrcAlign, 4)))
    return MVT::i32;
  if (Size >= 2 && Aligned(DstAlign, 2) && (IsMemset || Aligned(SrcAlign, 2)))
    return MVT::i16;

  return MVT::Other;
}

bool HexagonTargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
      unsigned AS, unsigned Align, bool *Fast) const {
  if (Fast)
    *Fast = false;
  return Subtarget.isHVXVectorType(VT.getSimpleVT());
}

std::pair<const TargetRegisterClass*, uint8_t>
HexagonTargetLowering::findRepresentativeClass(const TargetRegisterInfo *TRI,
      MVT VT) const {
  if (Subtarget.isHVXVectorType(VT, true)) {
    unsigned BitWidth = VT.getSizeInBits();
    unsigned VecWidth = Subtarget.getVectorLength() * 8;

    if (VT.getVectorElementType() == MVT::i1)
      return std::make_pair(&Hexagon::HvxQRRegClass, 1);
    if (BitWidth == VecWidth)
      return std::make_pair(&Hexagon::HvxVRRegClass, 1);
    assert(BitWidth == 2 * VecWidth);
    return std::make_pair(&Hexagon::HvxWRRegClass, 1);
  }

  return TargetLowering::findRepresentativeClass(TRI, VT);
}

bool HexagonTargetLowering::shouldReduceLoadWidth(SDNode *Load,
      ISD::LoadExtType ExtTy, EVT NewVT) const {
  // TODO: This may be worth removing. Check regression tests for diffs.
  if (!TargetLoweringBase::shouldReduceLoadWidth(Load, ExtTy, NewVT))
    return false;

  auto *L = cast<LoadSDNode>(Load);
  std::pair<SDValue,int> BO = getBaseAndOffset(L->getBasePtr());
  // Small-data object, do not shrink.
  if (BO.first.getOpcode() == HexagonISD::CONST32_GP)
    return false;
  if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(BO.first)) {
    auto &HTM = static_cast<const HexagonTargetMachine&>(getTargetMachine());
    const auto *GO = dyn_cast_or_null<const GlobalObject>(GA->getGlobal());
    return !GO || !HTM.getObjFileLowering()->isGlobalInSmallSection(GO, HTM);
  }
  return true;
}

Value *HexagonTargetLowering::emitLoadLinked(IRBuilder<> &Builder, Value *Addr,
      AtomicOrdering Ord) const {
  BasicBlock *BB = Builder.GetInsertBlock();
  Module *M = BB->getParent()->getParent();
  Type *Ty = cast<PointerType>(Addr->getType())->getElementType();
  unsigned SZ = Ty->getPrimitiveSizeInBits();
  assert((SZ == 32 || SZ == 64) && "Only 32/64-bit atomic loads supported");
  Intrinsic::ID IntID = (SZ == 32) ? Intrinsic::hexagon_L2_loadw_locked
                                   : Intrinsic::hexagon_L4_loadd_locked;
  Value *Fn = Intrinsic::getDeclaration(M, IntID);
  return Builder.CreateCall(Fn, Addr, "larx");
}

/// Perform a store-conditional operation to Addr. Return the status of the
/// store. This should be 0 if the store succeeded, non-zero otherwise.
Value *HexagonTargetLowering::emitStoreConditional(IRBuilder<> &Builder,
      Value *Val, Value *Addr, AtomicOrdering Ord) const {
  BasicBlock *BB = Builder.GetInsertBlock();
  Module *M = BB->getParent()->getParent();
  Type *Ty = Val->getType();
  unsigned SZ = Ty->getPrimitiveSizeInBits();
  assert((SZ == 32 || SZ == 64) && "Only 32/64-bit atomic stores supported");
  Intrinsic::ID IntID = (SZ == 32) ? Intrinsic::hexagon_S2_storew_locked
                                   : Intrinsic::hexagon_S4_stored_locked;
  Value *Fn = Intrinsic::getDeclaration(M, IntID);
  Value *Call = Builder.CreateCall(Fn, {Addr, Val}, "stcx");
  Value *Cmp = Builder.CreateICmpEQ(Call, Builder.getInt32(0), "");
  Value *Ext = Builder.CreateZExt(Cmp, Type::getInt32Ty(M->getContext()));
  return Ext;
}

TargetLowering::AtomicExpansionKind
HexagonTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  // Do not expand loads and stores that don't exceed 64 bits.
  return LI->getType()->getPrimitiveSizeInBits() > 64
             ? AtomicExpansionKind::LLOnly
             : AtomicExpansionKind::None;
}

bool HexagonTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  // Do not expand loads and stores that don't exceed 64 bits.
  return SI->getValueOperand()->getType()->getPrimitiveSizeInBits() > 64;
}

TargetLowering::AtomicExpansionKind
HexagonTargetLowering::shouldExpandAtomicCmpXchgInIR(
    AtomicCmpXchgInst *AI) const {
  const DataLayout &DL = AI->getModule()->getDataLayout();
  unsigned Size = DL.getTypeStoreSize(AI->getCompareOperand()->getType());
  if (Size >= 4 && Size <= 8)
    return AtomicExpansionKind::LLSC;
  return AtomicExpansionKind::None;
}
