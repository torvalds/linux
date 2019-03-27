//===- X86ISelDAGToDAG.cpp - A DAG pattern matching inst selector for X86 -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a DAG pattern matching instruction selector for X86,
// converting from a legalized dag to a X86 dag.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <stdint.h>
using namespace llvm;

#define DEBUG_TYPE "x86-isel"

STATISTIC(NumLoadMoved, "Number of loads moved below TokenFactor");

static cl::opt<bool> AndImmShrink("x86-and-imm-shrink", cl::init(true),
    cl::desc("Enable setting constant bits to reduce size of mask immediates"),
    cl::Hidden);

//===----------------------------------------------------------------------===//
//                      Pattern Matcher Implementation
//===----------------------------------------------------------------------===//

namespace {
  /// This corresponds to X86AddressMode, but uses SDValue's instead of register
  /// numbers for the leaves of the matched tree.
  struct X86ISelAddressMode {
    enum {
      RegBase,
      FrameIndexBase
    } BaseType;

    // This is really a union, discriminated by BaseType!
    SDValue Base_Reg;
    int Base_FrameIndex;

    unsigned Scale;
    SDValue IndexReg;
    int32_t Disp;
    SDValue Segment;
    const GlobalValue *GV;
    const Constant *CP;
    const BlockAddress *BlockAddr;
    const char *ES;
    MCSymbol *MCSym;
    int JT;
    unsigned Align;    // CP alignment.
    unsigned char SymbolFlags;  // X86II::MO_*

    X86ISelAddressMode()
        : BaseType(RegBase), Base_FrameIndex(0), Scale(1), IndexReg(), Disp(0),
          Segment(), GV(nullptr), CP(nullptr), BlockAddr(nullptr), ES(nullptr),
          MCSym(nullptr), JT(-1), Align(0), SymbolFlags(X86II::MO_NO_FLAG) {}

    bool hasSymbolicDisplacement() const {
      return GV != nullptr || CP != nullptr || ES != nullptr ||
             MCSym != nullptr || JT != -1 || BlockAddr != nullptr;
    }

    bool hasBaseOrIndexReg() const {
      return BaseType == FrameIndexBase ||
             IndexReg.getNode() != nullptr || Base_Reg.getNode() != nullptr;
    }

    /// Return true if this addressing mode is already RIP-relative.
    bool isRIPRelative() const {
      if (BaseType != RegBase) return false;
      if (RegisterSDNode *RegNode =
            dyn_cast_or_null<RegisterSDNode>(Base_Reg.getNode()))
        return RegNode->getReg() == X86::RIP;
      return false;
    }

    void setBaseReg(SDValue Reg) {
      BaseType = RegBase;
      Base_Reg = Reg;
    }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    void dump(SelectionDAG *DAG = nullptr) {
      dbgs() << "X86ISelAddressMode " << this << '\n';
      dbgs() << "Base_Reg ";
      if (Base_Reg.getNode())
        Base_Reg.getNode()->dump(DAG);
      else
        dbgs() << "nul\n";
      if (BaseType == FrameIndexBase)
        dbgs() << " Base.FrameIndex " << Base_FrameIndex << '\n';
      dbgs() << " Scale " << Scale << '\n'
             << "IndexReg ";
      if (IndexReg.getNode())
        IndexReg.getNode()->dump(DAG);
      else
        dbgs() << "nul\n";
      dbgs() << " Disp " << Disp << '\n'
             << "GV ";
      if (GV)
        GV->dump();
      else
        dbgs() << "nul";
      dbgs() << " CP ";
      if (CP)
        CP->dump();
      else
        dbgs() << "nul";
      dbgs() << '\n'
             << "ES ";
      if (ES)
        dbgs() << ES;
      else
        dbgs() << "nul";
      dbgs() << " MCSym ";
      if (MCSym)
        dbgs() << MCSym;
      else
        dbgs() << "nul";
      dbgs() << " JT" << JT << " Align" << Align << '\n';
    }
#endif
  };
}

namespace {
  //===--------------------------------------------------------------------===//
  /// ISel - X86-specific code to select X86 machine instructions for
  /// SelectionDAG operations.
  ///
  class X86DAGToDAGISel final : public SelectionDAGISel {
    /// Keep a pointer to the X86Subtarget around so that we can
    /// make the right decision when generating code for different targets.
    const X86Subtarget *Subtarget;

    /// If true, selector should try to optimize for code size instead of
    /// performance.
    bool OptForSize;

    /// If true, selector should try to optimize for minimum code size.
    bool OptForMinSize;

    /// Disable direct TLS access through segment registers.
    bool IndirectTlsSegRefs;

  public:
    explicit X86DAGToDAGISel(X86TargetMachine &tm, CodeGenOpt::Level OptLevel)
        : SelectionDAGISel(tm, OptLevel), OptForSize(false),
          OptForMinSize(false) {}

    StringRef getPassName() const override {
      return "X86 DAG->DAG Instruction Selection";
    }

    bool runOnMachineFunction(MachineFunction &MF) override {
      // Reset the subtarget each time through.
      Subtarget = &MF.getSubtarget<X86Subtarget>();
      IndirectTlsSegRefs = MF.getFunction().hasFnAttribute(
                             "indirect-tls-seg-refs");
      SelectionDAGISel::runOnMachineFunction(MF);
      return true;
    }

    void EmitFunctionEntryCode() override;

    bool IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const override;

    void PreprocessISelDAG() override;
    void PostprocessISelDAG() override;

// Include the pieces autogenerated from the target description.
#include "X86GenDAGISel.inc"

  private:
    void Select(SDNode *N) override;

    bool foldOffsetIntoAddress(uint64_t Offset, X86ISelAddressMode &AM);
    bool matchLoadInAddress(LoadSDNode *N, X86ISelAddressMode &AM);
    bool matchWrapper(SDValue N, X86ISelAddressMode &AM);
    bool matchAddress(SDValue N, X86ISelAddressMode &AM);
    bool matchVectorAddress(SDValue N, X86ISelAddressMode &AM);
    bool matchAdd(SDValue N, X86ISelAddressMode &AM, unsigned Depth);
    bool matchAddressRecursively(SDValue N, X86ISelAddressMode &AM,
                                 unsigned Depth);
    bool matchAddressBase(SDValue N, X86ISelAddressMode &AM);
    bool selectAddr(SDNode *Parent, SDValue N, SDValue &Base,
                    SDValue &Scale, SDValue &Index, SDValue &Disp,
                    SDValue &Segment);
    bool selectVectorAddr(SDNode *Parent, SDValue N, SDValue &Base,
                          SDValue &Scale, SDValue &Index, SDValue &Disp,
                          SDValue &Segment);
    bool selectMOV64Imm32(SDValue N, SDValue &Imm);
    bool selectLEAAddr(SDValue N, SDValue &Base,
                       SDValue &Scale, SDValue &Index, SDValue &Disp,
                       SDValue &Segment);
    bool selectLEA64_32Addr(SDValue N, SDValue &Base,
                            SDValue &Scale, SDValue &Index, SDValue &Disp,
                            SDValue &Segment);
    bool selectTLSADDRAddr(SDValue N, SDValue &Base,
                           SDValue &Scale, SDValue &Index, SDValue &Disp,
                           SDValue &Segment);
    bool selectScalarSSELoad(SDNode *Root, SDNode *Parent, SDValue N,
                             SDValue &Base, SDValue &Scale,
                             SDValue &Index, SDValue &Disp,
                             SDValue &Segment,
                             SDValue &NodeWithChain);
    bool selectRelocImm(SDValue N, SDValue &Op);

    bool tryFoldLoad(SDNode *Root, SDNode *P, SDValue N,
                     SDValue &Base, SDValue &Scale,
                     SDValue &Index, SDValue &Disp,
                     SDValue &Segment);

    // Convenience method where P is also root.
    bool tryFoldLoad(SDNode *P, SDValue N,
                     SDValue &Base, SDValue &Scale,
                     SDValue &Index, SDValue &Disp,
                     SDValue &Segment) {
      return tryFoldLoad(P, P, N, Base, Scale, Index, Disp, Segment);
    }

    /// Implement addressing mode selection for inline asm expressions.
    bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                      unsigned ConstraintID,
                                      std::vector<SDValue> &OutOps) override;

    void emitSpecialCodeForMain();

    inline void getAddressOperands(X86ISelAddressMode &AM, const SDLoc &DL,
                                   SDValue &Base, SDValue &Scale,
                                   SDValue &Index, SDValue &Disp,
                                   SDValue &Segment) {
      Base = (AM.BaseType == X86ISelAddressMode::FrameIndexBase)
                 ? CurDAG->getTargetFrameIndex(
                       AM.Base_FrameIndex,
                       TLI->getPointerTy(CurDAG->getDataLayout()))
                 : AM.Base_Reg;
      Scale = getI8Imm(AM.Scale, DL);
      Index = AM.IndexReg;
      // These are 32-bit even in 64-bit mode since RIP-relative offset
      // is 32-bit.
      if (AM.GV)
        Disp = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(),
                                              MVT::i32, AM.Disp,
                                              AM.SymbolFlags);
      else if (AM.CP)
        Disp = CurDAG->getTargetConstantPool(AM.CP, MVT::i32,
                                             AM.Align, AM.Disp, AM.SymbolFlags);
      else if (AM.ES) {
        assert(!AM.Disp && "Non-zero displacement is ignored with ES.");
        Disp = CurDAG->getTargetExternalSymbol(AM.ES, MVT::i32, AM.SymbolFlags);
      } else if (AM.MCSym) {
        assert(!AM.Disp && "Non-zero displacement is ignored with MCSym.");
        assert(AM.SymbolFlags == 0 && "oo");
        Disp = CurDAG->getMCSymbol(AM.MCSym, MVT::i32);
      } else if (AM.JT != -1) {
        assert(!AM.Disp && "Non-zero displacement is ignored with JT.");
        Disp = CurDAG->getTargetJumpTable(AM.JT, MVT::i32, AM.SymbolFlags);
      } else if (AM.BlockAddr)
        Disp = CurDAG->getTargetBlockAddress(AM.BlockAddr, MVT::i32, AM.Disp,
                                             AM.SymbolFlags);
      else
        Disp = CurDAG->getTargetConstant(AM.Disp, DL, MVT::i32);

      if (AM.Segment.getNode())
        Segment = AM.Segment;
      else
        Segment = CurDAG->getRegister(0, MVT::i32);
    }

    // Utility function to determine whether we should avoid selecting
    // immediate forms of instructions for better code size or not.
    // At a high level, we'd like to avoid such instructions when
    // we have similar constants used within the same basic block
    // that can be kept in a register.
    //
    bool shouldAvoidImmediateInstFormsForSize(SDNode *N) const {
      uint32_t UseCount = 0;

      // Do not want to hoist if we're not optimizing for size.
      // TODO: We'd like to remove this restriction.
      // See the comment in X86InstrInfo.td for more info.
      if (!OptForSize)
        return false;

      // Walk all the users of the immediate.
      for (SDNode::use_iterator UI = N->use_begin(),
           UE = N->use_end(); (UI != UE) && (UseCount < 2); ++UI) {

        SDNode *User = *UI;

        // This user is already selected. Count it as a legitimate use and
        // move on.
        if (User->isMachineOpcode()) {
          UseCount++;
          continue;
        }

        // We want to count stores of immediates as real uses.
        if (User->getOpcode() == ISD::STORE &&
            User->getOperand(1).getNode() == N) {
          UseCount++;
          continue;
        }

        // We don't currently match users that have > 2 operands (except
        // for stores, which are handled above)
        // Those instruction won't match in ISEL, for now, and would
        // be counted incorrectly.
        // This may change in the future as we add additional instruction
        // types.
        if (User->getNumOperands() != 2)
          continue;

        // Immediates that are used for offsets as part of stack
        // manipulation should be left alone. These are typically
        // used to indicate SP offsets for argument passing and
        // will get pulled into stores/pushes (implicitly).
        if (User->getOpcode() == X86ISD::ADD ||
            User->getOpcode() == ISD::ADD    ||
            User->getOpcode() == X86ISD::SUB ||
            User->getOpcode() == ISD::SUB) {

          // Find the other operand of the add/sub.
          SDValue OtherOp = User->getOperand(0);
          if (OtherOp.getNode() == N)
            OtherOp = User->getOperand(1);

          // Don't count if the other operand is SP.
          RegisterSDNode *RegNode;
          if (OtherOp->getOpcode() == ISD::CopyFromReg &&
              (RegNode = dyn_cast_or_null<RegisterSDNode>(
                 OtherOp->getOperand(1).getNode())))
            if ((RegNode->getReg() == X86::ESP) ||
                (RegNode->getReg() == X86::RSP))
              continue;
        }

        // ... otherwise, count this and move on.
        UseCount++;
      }

      // If we have more than 1 use, then recommend for hoisting.
      return (UseCount > 1);
    }

    /// Return a target constant with the specified value of type i8.
    inline SDValue getI8Imm(unsigned Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i8);
    }

    /// Return a target constant with the specified value, of type i32.
    inline SDValue getI32Imm(unsigned Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i32);
    }

    /// Return a target constant with the specified value, of type i64.
    inline SDValue getI64Imm(uint64_t Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i64);
    }

    SDValue getExtractVEXTRACTImmediate(SDNode *N, unsigned VecWidth,
                                        const SDLoc &DL) {
      assert((VecWidth == 128 || VecWidth == 256) && "Unexpected vector width");
      uint64_t Index = N->getConstantOperandVal(1);
      MVT VecVT = N->getOperand(0).getSimpleValueType();
      return getI8Imm((Index * VecVT.getScalarSizeInBits()) / VecWidth, DL);
    }

    SDValue getInsertVINSERTImmediate(SDNode *N, unsigned VecWidth,
                                      const SDLoc &DL) {
      assert((VecWidth == 128 || VecWidth == 256) && "Unexpected vector width");
      uint64_t Index = N->getConstantOperandVal(2);
      MVT VecVT = N->getSimpleValueType(0);
      return getI8Imm((Index * VecVT.getScalarSizeInBits()) / VecWidth, DL);
    }

    /// Return an SDNode that returns the value of the global base register.
    /// Output instructions required to initialize the global base register,
    /// if necessary.
    SDNode *getGlobalBaseReg();

    /// Return a reference to the TargetMachine, casted to the target-specific
    /// type.
    const X86TargetMachine &getTargetMachine() const {
      return static_cast<const X86TargetMachine &>(TM);
    }

    /// Return a reference to the TargetInstrInfo, casted to the target-specific
    /// type.
    const X86InstrInfo *getInstrInfo() const {
      return Subtarget->getInstrInfo();
    }

    /// Address-mode matching performs shift-of-and to and-of-shift
    /// reassociation in order to expose more scaled addressing
    /// opportunities.
    bool ComplexPatternFuncMutatesDAG() const override {
      return true;
    }

    bool isSExtAbsoluteSymbolRef(unsigned Width, SDNode *N) const;

    /// Returns whether this is a relocatable immediate in the range
    /// [-2^Width .. 2^Width-1].
    template <unsigned Width> bool isSExtRelocImm(SDNode *N) const {
      if (auto *CN = dyn_cast<ConstantSDNode>(N))
        return isInt<Width>(CN->getSExtValue());
      return isSExtAbsoluteSymbolRef(Width, N);
    }

    // Indicates we should prefer to use a non-temporal load for this load.
    bool useNonTemporalLoad(LoadSDNode *N) const {
      if (!N->isNonTemporal())
        return false;

      unsigned StoreSize = N->getMemoryVT().getStoreSize();

      if (N->getAlignment() < StoreSize)
        return false;

      switch (StoreSize) {
      default: llvm_unreachable("Unsupported store size");
      case 4:
      case 8:
        return false;
      case 16:
        return Subtarget->hasSSE41();
      case 32:
        return Subtarget->hasAVX2();
      case 64:
        return Subtarget->hasAVX512();
      }
    }

    bool foldLoadStoreIntoMemOperand(SDNode *Node);
    MachineSDNode *matchBEXTRFromAndImm(SDNode *Node);
    bool matchBitExtract(SDNode *Node);
    bool shrinkAndImmediate(SDNode *N);
    bool isMaskZeroExtended(SDNode *N) const;
    bool tryShiftAmountMod(SDNode *N);

    MachineSDNode *emitPCMPISTR(unsigned ROpc, unsigned MOpc, bool MayFoldLoad,
                                const SDLoc &dl, MVT VT, SDNode *Node);
    MachineSDNode *emitPCMPESTR(unsigned ROpc, unsigned MOpc, bool MayFoldLoad,
                                const SDLoc &dl, MVT VT, SDNode *Node,
                                SDValue &InFlag);

    bool tryOptimizeRem8Extend(SDNode *N);

    bool onlyUsesZeroFlag(SDValue Flags) const;
    bool hasNoSignFlagUses(SDValue Flags) const;
    bool hasNoCarryFlagUses(SDValue Flags) const;
  };
}


// Returns true if this masked compare can be implemented legally with this
// type.
static bool isLegalMaskCompare(SDNode *N, const X86Subtarget *Subtarget) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == X86ISD::CMPM || Opcode == ISD::SETCC ||
      Opcode == X86ISD::CMPM_RND || Opcode == X86ISD::VFPCLASS) {
    // We can get 256-bit 8 element types here without VLX being enabled. When
    // this happens we will use 512-bit operations and the mask will not be
    // zero extended.
    EVT OpVT = N->getOperand(0).getValueType();
    if (OpVT.is256BitVector() || OpVT.is128BitVector())
      return Subtarget->hasVLX();

    return true;
  }
  // Scalar opcodes use 128 bit registers, but aren't subject to the VLX check.
  if (Opcode == X86ISD::VFPCLASSS || Opcode == X86ISD::FSETCCM ||
      Opcode == X86ISD::FSETCCM_RND)
    return true;

  return false;
}

// Returns true if we can assume the writer of the mask has zero extended it
// for us.
bool X86DAGToDAGISel::isMaskZeroExtended(SDNode *N) const {
  // If this is an AND, check if we have a compare on either side. As long as
  // one side guarantees the mask is zero extended, the AND will preserve those
  // zeros.
  if (N->getOpcode() == ISD::AND)
    return isLegalMaskCompare(N->getOperand(0).getNode(), Subtarget) ||
           isLegalMaskCompare(N->getOperand(1).getNode(), Subtarget);

  return isLegalMaskCompare(N, Subtarget);
}

bool
X86DAGToDAGISel::IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const {
  if (OptLevel == CodeGenOpt::None) return false;

  if (!N.hasOneUse())
    return false;

  if (N.getOpcode() != ISD::LOAD)
    return true;

  // Don't fold non-temporal loads if we have an instruction for them.
  if (useNonTemporalLoad(cast<LoadSDNode>(N)))
    return false;

  // If N is a load, do additional profitability checks.
  if (U == Root) {
    switch (U->getOpcode()) {
    default: break;
    case X86ISD::ADD:
    case X86ISD::ADC:
    case X86ISD::SUB:
    case X86ISD::SBB:
    case X86ISD::AND:
    case X86ISD::XOR:
    case X86ISD::OR:
    case ISD::ADD:
    case ISD::ADDCARRY:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR: {
      SDValue Op1 = U->getOperand(1);

      // If the other operand is a 8-bit immediate we should fold the immediate
      // instead. This reduces code size.
      // e.g.
      // movl 4(%esp), %eax
      // addl $4, %eax
      // vs.
      // movl $4, %eax
      // addl 4(%esp), %eax
      // The former is 2 bytes shorter. In case where the increment is 1, then
      // the saving can be 4 bytes (by using incl %eax).
      if (ConstantSDNode *Imm = dyn_cast<ConstantSDNode>(Op1)) {
        if (Imm->getAPIntValue().isSignedIntN(8))
          return false;

        // If this is a 64-bit AND with an immediate that fits in 32-bits,
        // prefer using the smaller and over folding the load. This is needed to
        // make sure immediates created by shrinkAndImmediate are always folded.
        // Ideally we would narrow the load during DAG combine and get the
        // best of both worlds.
        if (U->getOpcode() == ISD::AND &&
            Imm->getAPIntValue().getBitWidth() == 64 &&
            Imm->getAPIntValue().isIntN(32))
          return false;
      }

      // If the other operand is a TLS address, we should fold it instead.
      // This produces
      // movl    %gs:0, %eax
      // leal    i@NTPOFF(%eax), %eax
      // instead of
      // movl    $i@NTPOFF, %eax
      // addl    %gs:0, %eax
      // if the block also has an access to a second TLS address this will save
      // a load.
      // FIXME: This is probably also true for non-TLS addresses.
      if (Op1.getOpcode() == X86ISD::Wrapper) {
        SDValue Val = Op1.getOperand(0);
        if (Val.getOpcode() == ISD::TargetGlobalTLSAddress)
          return false;
      }

      // Don't fold load if this matches the BTS/BTR/BTC patterns.
      // BTS: (or X, (shl 1, n))
      // BTR: (and X, (rotl -2, n))
      // BTC: (xor X, (shl 1, n))
      if (U->getOpcode() == ISD::OR || U->getOpcode() == ISD::XOR) {
        if (U->getOperand(0).getOpcode() == ISD::SHL &&
            isOneConstant(U->getOperand(0).getOperand(0)))
          return false;

        if (U->getOperand(1).getOpcode() == ISD::SHL &&
            isOneConstant(U->getOperand(1).getOperand(0)))
          return false;
      }
      if (U->getOpcode() == ISD::AND) {
        SDValue U0 = U->getOperand(0);
        SDValue U1 = U->getOperand(1);
        if (U0.getOpcode() == ISD::ROTL) {
          auto *C = dyn_cast<ConstantSDNode>(U0.getOperand(0));
          if (C && C->getSExtValue() == -2)
            return false;
        }

        if (U1.getOpcode() == ISD::ROTL) {
          auto *C = dyn_cast<ConstantSDNode>(U1.getOperand(0));
          if (C && C->getSExtValue() == -2)
            return false;
        }
      }

      break;
    }
    case ISD::SHL:
    case ISD::SRA:
    case ISD::SRL:
      // Don't fold a load into a shift by immediate. The BMI2 instructions
      // support folding a load, but not an immediate. The legacy instructions
      // support folding an immediate, but can't fold a load. Folding an
      // immediate is preferable to folding a load.
      if (isa<ConstantSDNode>(U->getOperand(1)))
        return false;

      break;
    }
  }

  // Prevent folding a load if this can implemented with an insert_subreg or
  // a move that implicitly zeroes.
  if (Root->getOpcode() == ISD::INSERT_SUBVECTOR &&
      isNullConstant(Root->getOperand(2)) &&
      (Root->getOperand(0).isUndef() ||
       ISD::isBuildVectorAllZeros(Root->getOperand(0).getNode())))
    return false;

  return true;
}

/// Replace the original chain operand of the call with
/// load's chain operand and move load below the call's chain operand.
static void moveBelowOrigChain(SelectionDAG *CurDAG, SDValue Load,
                               SDValue Call, SDValue OrigChain) {
  SmallVector<SDValue, 8> Ops;
  SDValue Chain = OrigChain.getOperand(0);
  if (Chain.getNode() == Load.getNode())
    Ops.push_back(Load.getOperand(0));
  else {
    assert(Chain.getOpcode() == ISD::TokenFactor &&
           "Unexpected chain operand");
    for (unsigned i = 0, e = Chain.getNumOperands(); i != e; ++i)
      if (Chain.getOperand(i).getNode() == Load.getNode())
        Ops.push_back(Load.getOperand(0));
      else
        Ops.push_back(Chain.getOperand(i));
    SDValue NewChain =
      CurDAG->getNode(ISD::TokenFactor, SDLoc(Load), MVT::Other, Ops);
    Ops.clear();
    Ops.push_back(NewChain);
  }
  Ops.append(OrigChain->op_begin() + 1, OrigChain->op_end());
  CurDAG->UpdateNodeOperands(OrigChain.getNode(), Ops);
  CurDAG->UpdateNodeOperands(Load.getNode(), Call.getOperand(0),
                             Load.getOperand(1), Load.getOperand(2));

  Ops.clear();
  Ops.push_back(SDValue(Load.getNode(), 1));
  Ops.append(Call->op_begin() + 1, Call->op_end());
  CurDAG->UpdateNodeOperands(Call.getNode(), Ops);
}

/// Return true if call address is a load and it can be
/// moved below CALLSEQ_START and the chains leading up to the call.
/// Return the CALLSEQ_START by reference as a second output.
/// In the case of a tail call, there isn't a callseq node between the call
/// chain and the load.
static bool isCalleeLoad(SDValue Callee, SDValue &Chain, bool HasCallSeq) {
  // The transformation is somewhat dangerous if the call's chain was glued to
  // the call. After MoveBelowOrigChain the load is moved between the call and
  // the chain, this can create a cycle if the load is not folded. So it is
  // *really* important that we are sure the load will be folded.
  if (Callee.getNode() == Chain.getNode() || !Callee.hasOneUse())
    return false;
  LoadSDNode *LD = dyn_cast<LoadSDNode>(Callee.getNode());
  if (!LD ||
      LD->isVolatile() ||
      LD->getAddressingMode() != ISD::UNINDEXED ||
      LD->getExtensionType() != ISD::NON_EXTLOAD)
    return false;

  // Now let's find the callseq_start.
  while (HasCallSeq && Chain.getOpcode() != ISD::CALLSEQ_START) {
    if (!Chain.hasOneUse())
      return false;
    Chain = Chain.getOperand(0);
  }

  if (!Chain.getNumOperands())
    return false;
  // Since we are not checking for AA here, conservatively abort if the chain
  // writes to memory. It's not safe to move the callee (a load) across a store.
  if (isa<MemSDNode>(Chain.getNode()) &&
      cast<MemSDNode>(Chain.getNode())->writeMem())
    return false;
  if (Chain.getOperand(0).getNode() == Callee.getNode())
    return true;
  if (Chain.getOperand(0).getOpcode() == ISD::TokenFactor &&
      Callee.getValue(1).isOperandOf(Chain.getOperand(0).getNode()) &&
      Callee.getValue(1).hasOneUse())
    return true;
  return false;
}

void X86DAGToDAGISel::PreprocessISelDAG() {
  // OptFor[Min]Size are used in pattern predicates that isel is matching.
  OptForSize = MF->getFunction().optForSize();
  OptForMinSize = MF->getFunction().optForMinSize();
  assert((!OptForMinSize || OptForSize) && "OptForMinSize implies OptForSize");

  for (SelectionDAG::allnodes_iterator I = CurDAG->allnodes_begin(),
       E = CurDAG->allnodes_end(); I != E; ) {
    SDNode *N = &*I++; // Preincrement iterator to avoid invalidation issues.

    // If this is a target specific AND node with no flag usages, turn it back
    // into ISD::AND to enable test instruction matching.
    if (N->getOpcode() == X86ISD::AND && !N->hasAnyUseOfValue(1)) {
      SDValue Res = CurDAG->getNode(ISD::AND, SDLoc(N), N->getValueType(0),
                                    N->getOperand(0), N->getOperand(1));
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      ++I;
      CurDAG->DeleteNode(N);
      continue;
    }

    if (OptLevel != CodeGenOpt::None &&
        // Only do this when the target can fold the load into the call or
        // jmp.
        !Subtarget->useRetpolineIndirectCalls() &&
        ((N->getOpcode() == X86ISD::CALL && !Subtarget->slowTwoMemOps()) ||
         (N->getOpcode() == X86ISD::TC_RETURN &&
          (Subtarget->is64Bit() ||
           !getTargetMachine().isPositionIndependent())))) {
      /// Also try moving call address load from outside callseq_start to just
      /// before the call to allow it to be folded.
      ///
      ///     [Load chain]
      ///         ^
      ///         |
      ///       [Load]
      ///       ^    ^
      ///       |    |
      ///      /      \--
      ///     /          |
      ///[CALLSEQ_START] |
      ///     ^          |
      ///     |          |
      /// [LOAD/C2Reg]   |
      ///     |          |
      ///      \        /
      ///       \      /
      ///       [CALL]
      bool HasCallSeq = N->getOpcode() == X86ISD::CALL;
      SDValue Chain = N->getOperand(0);
      SDValue Load  = N->getOperand(1);
      if (!isCalleeLoad(Load, Chain, HasCallSeq))
        continue;
      moveBelowOrigChain(CurDAG, Load, SDValue(N, 0), Chain);
      ++NumLoadMoved;
      continue;
    }

    // Lower fpround and fpextend nodes that target the FP stack to be store and
    // load to the stack.  This is a gross hack.  We would like to simply mark
    // these as being illegal, but when we do that, legalize produces these when
    // it expands calls, then expands these in the same legalize pass.  We would
    // like dag combine to be able to hack on these between the call expansion
    // and the node legalization.  As such this pass basically does "really
    // late" legalization of these inline with the X86 isel pass.
    // FIXME: This should only happen when not compiled with -O0.
    if (N->getOpcode() != ISD::FP_ROUND && N->getOpcode() != ISD::FP_EXTEND)
      continue;

    MVT SrcVT = N->getOperand(0).getSimpleValueType();
    MVT DstVT = N->getSimpleValueType(0);

    // If any of the sources are vectors, no fp stack involved.
    if (SrcVT.isVector() || DstVT.isVector())
      continue;

    // If the source and destination are SSE registers, then this is a legal
    // conversion that should not be lowered.
    const X86TargetLowering *X86Lowering =
        static_cast<const X86TargetLowering *>(TLI);
    bool SrcIsSSE = X86Lowering->isScalarFPTypeInSSEReg(SrcVT);
    bool DstIsSSE = X86Lowering->isScalarFPTypeInSSEReg(DstVT);
    if (SrcIsSSE && DstIsSSE)
      continue;

    if (!SrcIsSSE && !DstIsSSE) {
      // If this is an FPStack extension, it is a noop.
      if (N->getOpcode() == ISD::FP_EXTEND)
        continue;
      // If this is a value-preserving FPStack truncation, it is a noop.
      if (N->getConstantOperandVal(1))
        continue;
    }

    // Here we could have an FP stack truncation or an FPStack <-> SSE convert.
    // FPStack has extload and truncstore.  SSE can fold direct loads into other
    // operations.  Based on this, decide what we want to do.
    MVT MemVT;
    if (N->getOpcode() == ISD::FP_ROUND)
      MemVT = DstVT;  // FP_ROUND must use DstVT, we can't do a 'trunc load'.
    else
      MemVT = SrcIsSSE ? SrcVT : DstVT;

    SDValue MemTmp = CurDAG->CreateStackTemporary(MemVT);
    SDLoc dl(N);

    // FIXME: optimize the case where the src/dest is a load or store?
    SDValue Store =
        CurDAG->getTruncStore(CurDAG->getEntryNode(), dl, N->getOperand(0),
                              MemTmp, MachinePointerInfo(), MemVT);
    SDValue Result = CurDAG->getExtLoad(ISD::EXTLOAD, dl, DstVT, Store, MemTmp,
                                        MachinePointerInfo(), MemVT);

    // We're about to replace all uses of the FP_ROUND/FP_EXTEND with the
    // extload we created.  This will cause general havok on the dag because
    // anything below the conversion could be folded into other existing nodes.
    // To avoid invalidating 'I', back it up to the convert node.
    --I;
    CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Result);

    // Now that we did that, the node is dead.  Increment the iterator to the
    // next node to process, then delete N.
    ++I;
    CurDAG->DeleteNode(N);
  }
}

// Look for a redundant movzx/movsx that can occur after an 8-bit divrem.
bool X86DAGToDAGISel::tryOptimizeRem8Extend(SDNode *N) {
  unsigned Opc = N->getMachineOpcode();
  if (Opc != X86::MOVZX32rr8 && Opc != X86::MOVSX32rr8 &&
      Opc != X86::MOVSX64rr8)
    return false;

  SDValue N0 = N->getOperand(0);

  // We need to be extracting the lower bit of an extend.
  if (!N0.isMachineOpcode() ||
      N0.getMachineOpcode() != TargetOpcode::EXTRACT_SUBREG ||
      N0.getConstantOperandVal(1) != X86::sub_8bit)
    return false;

  // We're looking for either a movsx or movzx to match the original opcode.
  unsigned ExpectedOpc = Opc == X86::MOVZX32rr8 ? X86::MOVZX32rr8_NOREX
                                                : X86::MOVSX32rr8_NOREX;
  SDValue N00 = N0.getOperand(0);
  if (!N00.isMachineOpcode() || N00.getMachineOpcode() != ExpectedOpc)
    return false;

  if (Opc == X86::MOVSX64rr8) {
    // If we had a sign extend from 8 to 64 bits. We still need to go from 32
    // to 64.
    MachineSDNode *Extend = CurDAG->getMachineNode(X86::MOVSX64rr32, SDLoc(N),
                                                   MVT::i64, N00);
    ReplaceUses(N, Extend);
  } else {
    // Ok we can drop this extend and just use the original extend.
    ReplaceUses(N, N00.getNode());
  }

  return true;
}

void X86DAGToDAGISel::PostprocessISelDAG() {
  // Skip peepholes at -O0.
  if (TM.getOptLevel() == CodeGenOpt::None)
    return;

  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  bool MadeChange = false;
  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    // Skip dead nodes and any non-machine opcodes.
    if (N->use_empty() || !N->isMachineOpcode())
      continue;

    if (tryOptimizeRem8Extend(N)) {
      MadeChange = true;
      continue;
    }

    // Look for a TESTrr+ANDrr pattern where both operands of the test are
    // the same. Rewrite to remove the AND.
    unsigned Opc = N->getMachineOpcode();
    if ((Opc == X86::TEST8rr || Opc == X86::TEST16rr ||
         Opc == X86::TEST32rr || Opc == X86::TEST64rr) &&
        N->getOperand(0) == N->getOperand(1) &&
        N->isOnlyUserOf(N->getOperand(0).getNode()) &&
        N->getOperand(0).isMachineOpcode()) {
      SDValue And = N->getOperand(0);
      unsigned N0Opc = And.getMachineOpcode();
      if (N0Opc == X86::AND8rr || N0Opc == X86::AND16rr ||
          N0Opc == X86::AND32rr || N0Opc == X86::AND64rr) {
        MachineSDNode *Test = CurDAG->getMachineNode(Opc, SDLoc(N),
                                                     MVT::i32,
                                                     And.getOperand(0),
                                                     And.getOperand(1));
        ReplaceUses(N, Test);
        MadeChange = true;
        continue;
      }
      if (N0Opc == X86::AND8rm || N0Opc == X86::AND16rm ||
          N0Opc == X86::AND32rm || N0Opc == X86::AND64rm) {
        unsigned NewOpc;
        switch (N0Opc) {
        case X86::AND8rm:  NewOpc = X86::TEST8mr; break;
        case X86::AND16rm: NewOpc = X86::TEST16mr; break;
        case X86::AND32rm: NewOpc = X86::TEST32mr; break;
        case X86::AND64rm: NewOpc = X86::TEST64mr; break;
        }

        // Need to swap the memory and register operand.
        SDValue Ops[] = { And.getOperand(1),
                          And.getOperand(2),
                          And.getOperand(3),
                          And.getOperand(4),
                          And.getOperand(5),
                          And.getOperand(0),
                          And.getOperand(6)  /* Chain */ };
        MachineSDNode *Test = CurDAG->getMachineNode(NewOpc, SDLoc(N),
                                                     MVT::i32, MVT::Other, Ops);
        ReplaceUses(N, Test);
        MadeChange = true;
        continue;
      }
    }

    // Look for a KAND+KORTEST and turn it into KTEST if only the zero flag is
    // used. We're doing this late so we can prefer to fold the AND into masked
    // comparisons. Doing that can be better for the live range of the mask
    // register.
    if ((Opc == X86::KORTESTBrr || Opc == X86::KORTESTWrr ||
         Opc == X86::KORTESTDrr || Opc == X86::KORTESTQrr) &&
        N->getOperand(0) == N->getOperand(1) &&
        N->isOnlyUserOf(N->getOperand(0).getNode()) &&
        N->getOperand(0).isMachineOpcode() &&
        onlyUsesZeroFlag(SDValue(N, 0))) {
      SDValue And = N->getOperand(0);
      unsigned N0Opc = And.getMachineOpcode();
      // KANDW is legal with AVX512F, but KTESTW requires AVX512DQ. The other
      // KAND instructions and KTEST use the same ISA feature.
      if (N0Opc == X86::KANDBrr ||
          (N0Opc == X86::KANDWrr && Subtarget->hasDQI()) ||
          N0Opc == X86::KANDDrr || N0Opc == X86::KANDQrr) {
        unsigned NewOpc;
        switch (Opc) {
        default: llvm_unreachable("Unexpected opcode!");
        case X86::KORTESTBrr: NewOpc = X86::KTESTBrr; break;
        case X86::KORTESTWrr: NewOpc = X86::KTESTWrr; break;
        case X86::KORTESTDrr: NewOpc = X86::KTESTDrr; break;
        case X86::KORTESTQrr: NewOpc = X86::KTESTQrr; break;
        }
        MachineSDNode *KTest = CurDAG->getMachineNode(NewOpc, SDLoc(N),
                                                      MVT::i32,
                                                      And.getOperand(0),
                                                      And.getOperand(1));
        ReplaceUses(N, KTest);
        MadeChange = true;
        continue;
      }
    }

    // Attempt to remove vectors moves that were inserted to zero upper bits.
    if (Opc != TargetOpcode::SUBREG_TO_REG)
      continue;

    unsigned SubRegIdx = N->getConstantOperandVal(2);
    if (SubRegIdx != X86::sub_xmm && SubRegIdx != X86::sub_ymm)
      continue;

    SDValue Move = N->getOperand(1);
    if (!Move.isMachineOpcode())
      continue;

    // Make sure its one of the move opcodes we recognize.
    switch (Move.getMachineOpcode()) {
    default:
      continue;
    case X86::VMOVAPDrr:       case X86::VMOVUPDrr:
    case X86::VMOVAPSrr:       case X86::VMOVUPSrr:
    case X86::VMOVDQArr:       case X86::VMOVDQUrr:
    case X86::VMOVAPDYrr:      case X86::VMOVUPDYrr:
    case X86::VMOVAPSYrr:      case X86::VMOVUPSYrr:
    case X86::VMOVDQAYrr:      case X86::VMOVDQUYrr:
    case X86::VMOVAPDZ128rr:   case X86::VMOVUPDZ128rr:
    case X86::VMOVAPSZ128rr:   case X86::VMOVUPSZ128rr:
    case X86::VMOVDQA32Z128rr: case X86::VMOVDQU32Z128rr:
    case X86::VMOVDQA64Z128rr: case X86::VMOVDQU64Z128rr:
    case X86::VMOVAPDZ256rr:   case X86::VMOVUPDZ256rr:
    case X86::VMOVAPSZ256rr:   case X86::VMOVUPSZ256rr:
    case X86::VMOVDQA32Z256rr: case X86::VMOVDQU32Z256rr:
    case X86::VMOVDQA64Z256rr: case X86::VMOVDQU64Z256rr:
      break;
    }

    SDValue In = Move.getOperand(0);
    if (!In.isMachineOpcode() ||
        In.getMachineOpcode() <= TargetOpcode::GENERIC_OP_END)
      continue;

    // Make sure the instruction has a VEX, XOP, or EVEX prefix. This covers
    // the SHA instructions which use a legacy encoding.
    uint64_t TSFlags = getInstrInfo()->get(In.getMachineOpcode()).TSFlags;
    if ((TSFlags & X86II::EncodingMask) != X86II::VEX &&
        (TSFlags & X86II::EncodingMask) != X86II::EVEX &&
        (TSFlags & X86II::EncodingMask) != X86II::XOP)
      continue;

    // Producing instruction is another vector instruction. We can drop the
    // move.
    CurDAG->UpdateNodeOperands(N, N->getOperand(0), In, N->getOperand(2));
    MadeChange = true;
  }

  if (MadeChange)
    CurDAG->RemoveDeadNodes();
}


/// Emit any code that needs to be executed only in the main function.
void X86DAGToDAGISel::emitSpecialCodeForMain() {
  if (Subtarget->isTargetCygMing()) {
    TargetLowering::ArgListTy Args;
    auto &DL = CurDAG->getDataLayout();

    TargetLowering::CallLoweringInfo CLI(*CurDAG);
    CLI.setChain(CurDAG->getRoot())
        .setCallee(CallingConv::C, Type::getVoidTy(*CurDAG->getContext()),
                   CurDAG->getExternalSymbol("__main", TLI->getPointerTy(DL)),
                   std::move(Args));
    const TargetLowering &TLI = CurDAG->getTargetLoweringInfo();
    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    CurDAG->setRoot(Result.second);
  }
}

void X86DAGToDAGISel::EmitFunctionEntryCode() {
  // If this is main, emit special code for main.
  const Function &F = MF->getFunction();
  if (F.hasExternalLinkage() && F.getName() == "main")
    emitSpecialCodeForMain();
}

static bool isDispSafeForFrameIndex(int64_t Val) {
  // On 64-bit platforms, we can run into an issue where a frame index
  // includes a displacement that, when added to the explicit displacement,
  // will overflow the displacement field. Assuming that the frame index
  // displacement fits into a 31-bit integer  (which is only slightly more
  // aggressive than the current fundamental assumption that it fits into
  // a 32-bit integer), a 31-bit disp should always be safe.
  return isInt<31>(Val);
}

bool X86DAGToDAGISel::foldOffsetIntoAddress(uint64_t Offset,
                                            X86ISelAddressMode &AM) {
  // If there's no offset to fold, we don't need to do any work.
  if (Offset == 0)
    return false;

  // Cannot combine ExternalSymbol displacements with integer offsets.
  if (AM.ES || AM.MCSym)
    return true;

  int64_t Val = AM.Disp + Offset;
  CodeModel::Model M = TM.getCodeModel();
  if (Subtarget->is64Bit()) {
    if (!X86::isOffsetSuitableForCodeModel(Val, M,
                                           AM.hasSymbolicDisplacement()))
      return true;
    // In addition to the checks required for a register base, check that
    // we do not try to use an unsafe Disp with a frame index.
    if (AM.BaseType == X86ISelAddressMode::FrameIndexBase &&
        !isDispSafeForFrameIndex(Val))
      return true;
  }
  AM.Disp = Val;
  return false;

}

bool X86DAGToDAGISel::matchLoadInAddress(LoadSDNode *N, X86ISelAddressMode &AM){
  SDValue Address = N->getOperand(1);

  // load gs:0 -> GS segment register.
  // load fs:0 -> FS segment register.
  //
  // This optimization is valid because the GNU TLS model defines that
  // gs:0 (or fs:0 on X86-64) contains its own address.
  // For more information see http://people.redhat.com/drepper/tls.pdf
  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Address))
    if (C->getSExtValue() == 0 && AM.Segment.getNode() == nullptr &&
        !IndirectTlsSegRefs &&
        (Subtarget->isTargetGlibc() || Subtarget->isTargetAndroid() ||
         Subtarget->isTargetFuchsia()))
      switch (N->getPointerInfo().getAddrSpace()) {
      case 256:
        AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
        return false;
      case 257:
        AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
        return false;
      // Address space 258 is not handled here, because it is not used to
      // address TLS areas.
      }

  return true;
}

/// Try to match X86ISD::Wrapper and X86ISD::WrapperRIP nodes into an addressing
/// mode. These wrap things that will resolve down into a symbol reference.
/// If no match is possible, this returns true, otherwise it returns false.
bool X86DAGToDAGISel::matchWrapper(SDValue N, X86ISelAddressMode &AM) {
  // If the addressing mode already has a symbol as the displacement, we can
  // never match another symbol.
  if (AM.hasSymbolicDisplacement())
    return true;

  bool IsRIPRelTLS = false;
  bool IsRIPRel = N.getOpcode() == X86ISD::WrapperRIP;
  if (IsRIPRel) {
    SDValue Val = N.getOperand(0);
    if (Val.getOpcode() == ISD::TargetGlobalTLSAddress)
      IsRIPRelTLS = true;
  }

  // We can't use an addressing mode in the 64-bit large code model.
  // Global TLS addressing is an exception. In the medium code model,
  // we use can use a mode when RIP wrappers are present.
  // That signifies access to globals that are known to be "near",
  // such as the GOT itself.
  CodeModel::Model M = TM.getCodeModel();
  if (Subtarget->is64Bit() &&
      ((M == CodeModel::Large && !IsRIPRelTLS) ||
       (M == CodeModel::Medium && !IsRIPRel)))
    return true;

  // Base and index reg must be 0 in order to use %rip as base.
  if (IsRIPRel && AM.hasBaseOrIndexReg())
    return true;

  // Make a local copy in case we can't do this fold.
  X86ISelAddressMode Backup = AM;

  int64_t Offset = 0;
  SDValue N0 = N.getOperand(0);
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(N0)) {
    AM.GV = G->getGlobal();
    AM.SymbolFlags = G->getTargetFlags();
    Offset = G->getOffset();
  } else if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(N0)) {
    AM.CP = CP->getConstVal();
    AM.Align = CP->getAlignment();
    AM.SymbolFlags = CP->getTargetFlags();
    Offset = CP->getOffset();
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(N0)) {
    AM.ES = S->getSymbol();
    AM.SymbolFlags = S->getTargetFlags();
  } else if (auto *S = dyn_cast<MCSymbolSDNode>(N0)) {
    AM.MCSym = S->getMCSymbol();
  } else if (JumpTableSDNode *J = dyn_cast<JumpTableSDNode>(N0)) {
    AM.JT = J->getIndex();
    AM.SymbolFlags = J->getTargetFlags();
  } else if (BlockAddressSDNode *BA = dyn_cast<BlockAddressSDNode>(N0)) {
    AM.BlockAddr = BA->getBlockAddress();
    AM.SymbolFlags = BA->getTargetFlags();
    Offset = BA->getOffset();
  } else
    llvm_unreachable("Unhandled symbol reference node.");

  if (foldOffsetIntoAddress(Offset, AM)) {
    AM = Backup;
    return true;
  }

  if (IsRIPRel)
    AM.setBaseReg(CurDAG->getRegister(X86::RIP, MVT::i64));

  // Commit the changes now that we know this fold is safe.
  return false;
}

/// Add the specified node to the specified addressing mode, returning true if
/// it cannot be done. This just pattern matches for the addressing mode.
bool X86DAGToDAGISel::matchAddress(SDValue N, X86ISelAddressMode &AM) {
  if (matchAddressRecursively(N, AM, 0))
    return true;

  // Post-processing: Convert lea(,%reg,2) to lea(%reg,%reg), which has
  // a smaller encoding and avoids a scaled-index.
  if (AM.Scale == 2 &&
      AM.BaseType == X86ISelAddressMode::RegBase &&
      AM.Base_Reg.getNode() == nullptr) {
    AM.Base_Reg = AM.IndexReg;
    AM.Scale = 1;
  }

  // Post-processing: Convert foo to foo(%rip), even in non-PIC mode,
  // because it has a smaller encoding.
  // TODO: Which other code models can use this?
  if (TM.getCodeModel() == CodeModel::Small &&
      Subtarget->is64Bit() &&
      AM.Scale == 1 &&
      AM.BaseType == X86ISelAddressMode::RegBase &&
      AM.Base_Reg.getNode() == nullptr &&
      AM.IndexReg.getNode() == nullptr &&
      AM.SymbolFlags == X86II::MO_NO_FLAG &&
      AM.hasSymbolicDisplacement())
    AM.Base_Reg = CurDAG->getRegister(X86::RIP, MVT::i64);

  return false;
}

bool X86DAGToDAGISel::matchAdd(SDValue N, X86ISelAddressMode &AM,
                               unsigned Depth) {
  // Add an artificial use to this node so that we can keep track of
  // it if it gets CSE'd with a different node.
  HandleSDNode Handle(N);

  X86ISelAddressMode Backup = AM;
  if (!matchAddressRecursively(N.getOperand(0), AM, Depth+1) &&
      !matchAddressRecursively(Handle.getValue().getOperand(1), AM, Depth+1))
    return false;
  AM = Backup;

  // Try again after commuting the operands.
  if (!matchAddressRecursively(Handle.getValue().getOperand(1), AM, Depth+1) &&
      !matchAddressRecursively(Handle.getValue().getOperand(0), AM, Depth+1))
    return false;
  AM = Backup;

  // If we couldn't fold both operands into the address at the same time,
  // see if we can just put each operand into a register and fold at least
  // the add.
  if (AM.BaseType == X86ISelAddressMode::RegBase &&
      !AM.Base_Reg.getNode() &&
      !AM.IndexReg.getNode()) {
    N = Handle.getValue();
    AM.Base_Reg = N.getOperand(0);
    AM.IndexReg = N.getOperand(1);
    AM.Scale = 1;
    return false;
  }
  N = Handle.getValue();
  return true;
}

// Insert a node into the DAG at least before the Pos node's position. This
// will reposition the node as needed, and will assign it a node ID that is <=
// the Pos node's ID. Note that this does *not* preserve the uniqueness of node
// IDs! The selection DAG must no longer depend on their uniqueness when this
// is used.
static void insertDAGNode(SelectionDAG &DAG, SDValue Pos, SDValue N) {
  if (N->getNodeId() == -1 ||
      (SelectionDAGISel::getUninvalidatedNodeId(N.getNode()) >
       SelectionDAGISel::getUninvalidatedNodeId(Pos.getNode()))) {
    DAG.RepositionNode(Pos->getIterator(), N.getNode());
    // Mark Node as invalid for pruning as after this it may be a successor to a
    // selected node but otherwise be in the same position of Pos.
    // Conservatively mark it with the same -abs(Id) to assure node id
    // invariant is preserved.
    N->setNodeId(Pos->getNodeId());
    SelectionDAGISel::InvalidateNodeId(N.getNode());
  }
}

// Transform "(X >> (8-C1)) & (0xff << C1)" to "((X >> 8) & 0xff) << C1" if
// safe. This allows us to convert the shift and and into an h-register
// extract and a scaled index. Returns false if the simplification is
// performed.
static bool foldMaskAndShiftToExtract(SelectionDAG &DAG, SDValue N,
                                      uint64_t Mask,
                                      SDValue Shift, SDValue X,
                                      X86ISelAddressMode &AM) {
  if (Shift.getOpcode() != ISD::SRL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)) ||
      !Shift.hasOneUse())
    return true;

  int ScaleLog = 8 - Shift.getConstantOperandVal(1);
  if (ScaleLog <= 0 || ScaleLog >= 4 ||
      Mask != (0xffu << ScaleLog))
    return true;

  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  SDValue Eight = DAG.getConstant(8, DL, MVT::i8);
  SDValue NewMask = DAG.getConstant(0xff, DL, VT);
  SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, X, Eight);
  SDValue And = DAG.getNode(ISD::AND, DL, VT, Srl, NewMask);
  SDValue ShlCount = DAG.getConstant(ScaleLog, DL, MVT::i8);
  SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, And, ShlCount);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, Eight);
  insertDAGNode(DAG, N, Srl);
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, And);
  insertDAGNode(DAG, N, ShlCount);
  insertDAGNode(DAG, N, Shl);
  DAG.ReplaceAllUsesWith(N, Shl);
  AM.IndexReg = And;
  AM.Scale = (1 << ScaleLog);
  return false;
}

// Transforms "(X << C1) & C2" to "(X & (C2>>C1)) << C1" if safe and if this
// allows us to fold the shift into this addressing mode. Returns false if the
// transform succeeded.
static bool foldMaskedShiftToScaledMask(SelectionDAG &DAG, SDValue N,
                                        uint64_t Mask,
                                        SDValue Shift, SDValue X,
                                        X86ISelAddressMode &AM) {
  if (Shift.getOpcode() != ISD::SHL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)))
    return true;

  // Not likely to be profitable if either the AND or SHIFT node has more
  // than one use (unless all uses are for address computation). Besides,
  // isel mechanism requires their node ids to be reused.
  if (!N.hasOneUse() || !Shift.hasOneUse())
    return true;

  // Verify that the shift amount is something we can fold.
  unsigned ShiftAmt = Shift.getConstantOperandVal(1);
  if (ShiftAmt != 1 && ShiftAmt != 2 && ShiftAmt != 3)
    return true;

  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  SDValue NewMask = DAG.getConstant(Mask >> ShiftAmt, DL, VT);
  SDValue NewAnd = DAG.getNode(ISD::AND, DL, VT, X, NewMask);
  SDValue NewShift = DAG.getNode(ISD::SHL, DL, VT, NewAnd, Shift.getOperand(1));

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, NewAnd);
  insertDAGNode(DAG, N, NewShift);
  DAG.ReplaceAllUsesWith(N, NewShift);

  AM.Scale = 1 << ShiftAmt;
  AM.IndexReg = NewAnd;
  return false;
}

// Implement some heroics to detect shifts of masked values where the mask can
// be replaced by extending the shift and undoing that in the addressing mode
// scale. Patterns such as (shl (srl x, c1), c2) are canonicalized into (and
// (srl x, SHIFT), MASK) by DAGCombines that don't know the shl can be done in
// the addressing mode. This results in code such as:
//
//   int f(short *y, int *lookup_table) {
//     ...
//     return *y + lookup_table[*y >> 11];
//   }
//
// Turning into:
//   movzwl (%rdi), %eax
//   movl %eax, %ecx
//   shrl $11, %ecx
//   addl (%rsi,%rcx,4), %eax
//
// Instead of:
//   movzwl (%rdi), %eax
//   movl %eax, %ecx
//   shrl $9, %ecx
//   andl $124, %rcx
//   addl (%rsi,%rcx), %eax
//
// Note that this function assumes the mask is provided as a mask *after* the
// value is shifted. The input chain may or may not match that, but computing
// such a mask is trivial.
static bool foldMaskAndShiftToScale(SelectionDAG &DAG, SDValue N,
                                    uint64_t Mask,
                                    SDValue Shift, SDValue X,
                                    X86ISelAddressMode &AM) {
  if (Shift.getOpcode() != ISD::SRL || !Shift.hasOneUse() ||
      !isa<ConstantSDNode>(Shift.getOperand(1)))
    return true;

  unsigned ShiftAmt = Shift.getConstantOperandVal(1);
  unsigned MaskLZ = countLeadingZeros(Mask);
  unsigned MaskTZ = countTrailingZeros(Mask);

  // The amount of shift we're trying to fit into the addressing mode is taken
  // from the trailing zeros of the mask.
  unsigned AMShiftAmt = MaskTZ;

  // There is nothing we can do here unless the mask is removing some bits.
  // Also, the addressing mode can only represent shifts of 1, 2, or 3 bits.
  if (AMShiftAmt <= 0 || AMShiftAmt > 3) return true;

  // We also need to ensure that mask is a continuous run of bits.
  if (countTrailingOnes(Mask >> MaskTZ) + MaskTZ + MaskLZ != 64) return true;

  // Scale the leading zero count down based on the actual size of the value.
  // Also scale it down based on the size of the shift.
  unsigned ScaleDown = (64 - X.getSimpleValueType().getSizeInBits()) + ShiftAmt;
  if (MaskLZ < ScaleDown)
    return true;
  MaskLZ -= ScaleDown;

  // The final check is to ensure that any masked out high bits of X are
  // already known to be zero. Otherwise, the mask has a semantic impact
  // other than masking out a couple of low bits. Unfortunately, because of
  // the mask, zero extensions will be removed from operands in some cases.
  // This code works extra hard to look through extensions because we can
  // replace them with zero extensions cheaply if necessary.
  bool ReplacingAnyExtend = false;
  if (X.getOpcode() == ISD::ANY_EXTEND) {
    unsigned ExtendBits = X.getSimpleValueType().getSizeInBits() -
                          X.getOperand(0).getSimpleValueType().getSizeInBits();
    // Assume that we'll replace the any-extend with a zero-extend, and
    // narrow the search to the extended value.
    X = X.getOperand(0);
    MaskLZ = ExtendBits > MaskLZ ? 0 : MaskLZ - ExtendBits;
    ReplacingAnyExtend = true;
  }
  APInt MaskedHighBits =
    APInt::getHighBitsSet(X.getSimpleValueType().getSizeInBits(), MaskLZ);
  KnownBits Known = DAG.computeKnownBits(X);
  if (MaskedHighBits != Known.Zero) return true;

  // We've identified a pattern that can be transformed into a single shift
  // and an addressing mode. Make it so.
  MVT VT = N.getSimpleValueType();
  if (ReplacingAnyExtend) {
    assert(X.getValueType() != VT);
    // We looked through an ANY_EXTEND node, insert a ZERO_EXTEND.
    SDValue NewX = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(X), VT, X);
    insertDAGNode(DAG, N, NewX);
    X = NewX;
  }
  SDLoc DL(N);
  SDValue NewSRLAmt = DAG.getConstant(ShiftAmt + AMShiftAmt, DL, MVT::i8);
  SDValue NewSRL = DAG.getNode(ISD::SRL, DL, VT, X, NewSRLAmt);
  SDValue NewSHLAmt = DAG.getConstant(AMShiftAmt, DL, MVT::i8);
  SDValue NewSHL = DAG.getNode(ISD::SHL, DL, VT, NewSRL, NewSHLAmt);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewSRLAmt);
  insertDAGNode(DAG, N, NewSRL);
  insertDAGNode(DAG, N, NewSHLAmt);
  insertDAGNode(DAG, N, NewSHL);
  DAG.ReplaceAllUsesWith(N, NewSHL);

  AM.Scale = 1 << AMShiftAmt;
  AM.IndexReg = NewSRL;
  return false;
}

// Transform "(X >> SHIFT) & (MASK << C1)" to
// "((X >> (SHIFT + C1)) & (MASK)) << C1". Everything before the SHL will be
// matched to a BEXTR later. Returns false if the simplification is performed.
static bool foldMaskedShiftToBEXTR(SelectionDAG &DAG, SDValue N,
                                   uint64_t Mask,
                                   SDValue Shift, SDValue X,
                                   X86ISelAddressMode &AM,
                                   const X86Subtarget &Subtarget) {
  if (Shift.getOpcode() != ISD::SRL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)) ||
      !Shift.hasOneUse() || !N.hasOneUse())
    return true;

  // Only do this if BEXTR will be matched by matchBEXTRFromAndImm.
  if (!Subtarget.hasTBM() &&
      !(Subtarget.hasBMI() && Subtarget.hasFastBEXTR()))
    return true;

  // We need to ensure that mask is a continuous run of bits.
  if (!isShiftedMask_64(Mask)) return true;

  unsigned ShiftAmt = Shift.getConstantOperandVal(1);

  // The amount of shift we're trying to fit into the addressing mode is taken
  // from the trailing zeros of the mask.
  unsigned AMShiftAmt = countTrailingZeros(Mask);

  // There is nothing we can do here unless the mask is removing some bits.
  // Also, the addressing mode can only represent shifts of 1, 2, or 3 bits.
  if (AMShiftAmt <= 0 || AMShiftAmt > 3) return true;

  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  SDValue NewSRLAmt = DAG.getConstant(ShiftAmt + AMShiftAmt, DL, MVT::i8);
  SDValue NewSRL = DAG.getNode(ISD::SRL, DL, VT, X, NewSRLAmt);
  SDValue NewMask = DAG.getConstant(Mask >> AMShiftAmt, DL, VT);
  SDValue NewAnd = DAG.getNode(ISD::AND, DL, VT, NewSRL, NewMask);
  SDValue NewSHLAmt = DAG.getConstant(AMShiftAmt, DL, MVT::i8);
  SDValue NewSHL = DAG.getNode(ISD::SHL, DL, VT, NewAnd, NewSHLAmt);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewSRLAmt);
  insertDAGNode(DAG, N, NewSRL);
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, NewAnd);
  insertDAGNode(DAG, N, NewSHLAmt);
  insertDAGNode(DAG, N, NewSHL);
  DAG.ReplaceAllUsesWith(N, NewSHL);

  AM.Scale = 1 << AMShiftAmt;
  AM.IndexReg = NewAnd;
  return false;
}

bool X86DAGToDAGISel::matchAddressRecursively(SDValue N, X86ISelAddressMode &AM,
                                              unsigned Depth) {
  SDLoc dl(N);
  LLVM_DEBUG({
    dbgs() << "MatchAddress: ";
    AM.dump(CurDAG);
  });
  // Limit recursion.
  if (Depth > 5)
    return matchAddressBase(N, AM);

  // If this is already a %rip relative address, we can only merge immediates
  // into it.  Instead of handling this in every case, we handle it here.
  // RIP relative addressing: %rip + 32-bit displacement!
  if (AM.isRIPRelative()) {
    // FIXME: JumpTable and ExternalSymbol address currently don't like
    // displacements.  It isn't very important, but this should be fixed for
    // consistency.
    if (!(AM.ES || AM.MCSym) && AM.JT != -1)
      return true;

    if (ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(N))
      if (!foldOffsetIntoAddress(Cst->getSExtValue(), AM))
        return false;
    return true;
  }

  switch (N.getOpcode()) {
  default: break;
  case ISD::LOCAL_RECOVER: {
    if (!AM.hasSymbolicDisplacement() && AM.Disp == 0)
      if (const auto *ESNode = dyn_cast<MCSymbolSDNode>(N.getOperand(0))) {
        // Use the symbol and don't prefix it.
        AM.MCSym = ESNode->getMCSymbol();
        return false;
      }
    break;
  }
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (!foldOffsetIntoAddress(Val, AM))
      return false;
    break;
  }

  case X86ISD::Wrapper:
  case X86ISD::WrapperRIP:
    if (!matchWrapper(N, AM))
      return false;
    break;

  case ISD::LOAD:
    if (!matchLoadInAddress(cast<LoadSDNode>(N), AM))
      return false;
    break;

  case ISD::FrameIndex:
    if (AM.BaseType == X86ISelAddressMode::RegBase &&
        AM.Base_Reg.getNode() == nullptr &&
        (!Subtarget->is64Bit() || isDispSafeForFrameIndex(AM.Disp))) {
      AM.BaseType = X86ISelAddressMode::FrameIndexBase;
      AM.Base_FrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
      return false;
    }
    break;

  case ISD::SHL:
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1)
      break;

    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      unsigned Val = CN->getZExtValue();
      // Note that we handle x<<1 as (,x,2) rather than (x,x) here so
      // that the base operand remains free for further matching. If
      // the base doesn't end up getting used, a post-processing step
      // in MatchAddress turns (,x,2) into (x,x), which is cheaper.
      if (Val == 1 || Val == 2 || Val == 3) {
        AM.Scale = 1 << Val;
        SDValue ShVal = N.getOperand(0);

        // Okay, we know that we have a scale by now.  However, if the scaled
        // value is an add of something and a constant, we can fold the
        // constant into the disp field here.
        if (CurDAG->isBaseWithConstantOffset(ShVal)) {
          AM.IndexReg = ShVal.getOperand(0);
          ConstantSDNode *AddVal = cast<ConstantSDNode>(ShVal.getOperand(1));
          uint64_t Disp = (uint64_t)AddVal->getSExtValue() << Val;
          if (!foldOffsetIntoAddress(Disp, AM))
            return false;
        }

        AM.IndexReg = ShVal;
        return false;
      }
    }
    break;

  case ISD::SRL: {
    // Scale must not be used already.
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1) break;

    SDValue And = N.getOperand(0);
    if (And.getOpcode() != ISD::AND) break;
    SDValue X = And.getOperand(0);

    // We only handle up to 64-bit values here as those are what matter for
    // addressing mode optimizations.
    if (X.getSimpleValueType().getSizeInBits() > 64) break;

    // The mask used for the transform is expected to be post-shift, but we
    // found the shift first so just apply the shift to the mask before passing
    // it down.
    if (!isa<ConstantSDNode>(N.getOperand(1)) ||
        !isa<ConstantSDNode>(And.getOperand(1)))
      break;
    uint64_t Mask = And.getConstantOperandVal(1) >> N.getConstantOperandVal(1);

    // Try to fold the mask and shift into the scale, and return false if we
    // succeed.
    if (!foldMaskAndShiftToScale(*CurDAG, N, Mask, N, X, AM))
      return false;
    break;
  }

  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
    // A mul_lohi where we need the low part can be folded as a plain multiply.
    if (N.getResNo() != 0) break;
    LLVM_FALLTHROUGH;
  case ISD::MUL:
  case X86ISD::MUL_IMM:
    // X*[3,5,9] -> X+X*[2,4,8]
    if (AM.BaseType == X86ISelAddressMode::RegBase &&
        AM.Base_Reg.getNode() == nullptr &&
        AM.IndexReg.getNode() == nullptr) {
      if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N.getOperand(1)))
        if (CN->getZExtValue() == 3 || CN->getZExtValue() == 5 ||
            CN->getZExtValue() == 9) {
          AM.Scale = unsigned(CN->getZExtValue())-1;

          SDValue MulVal = N.getOperand(0);
          SDValue Reg;

          // Okay, we know that we have a scale by now.  However, if the scaled
          // value is an add of something and a constant, we can fold the
          // constant into the disp field here.
          if (MulVal.getNode()->getOpcode() == ISD::ADD && MulVal.hasOneUse() &&
              isa<ConstantSDNode>(MulVal.getOperand(1))) {
            Reg = MulVal.getOperand(0);
            ConstantSDNode *AddVal =
              cast<ConstantSDNode>(MulVal.getOperand(1));
            uint64_t Disp = AddVal->getSExtValue() * CN->getZExtValue();
            if (foldOffsetIntoAddress(Disp, AM))
              Reg = N.getOperand(0);
          } else {
            Reg = N.getOperand(0);
          }

          AM.IndexReg = AM.Base_Reg = Reg;
          return false;
        }
    }
    break;

  case ISD::SUB: {
    // Given A-B, if A can be completely folded into the address and
    // the index field with the index field unused, use -B as the index.
    // This is a win if a has multiple parts that can be folded into
    // the address. Also, this saves a mov if the base register has
    // other uses, since it avoids a two-address sub instruction, however
    // it costs an additional mov if the index register has other uses.

    // Add an artificial use to this node so that we can keep track of
    // it if it gets CSE'd with a different node.
    HandleSDNode Handle(N);

    // Test if the LHS of the sub can be folded.
    X86ISelAddressMode Backup = AM;
    if (matchAddressRecursively(N.getOperand(0), AM, Depth+1)) {
      AM = Backup;
      break;
    }
    // Test if the index field is free for use.
    if (AM.IndexReg.getNode() || AM.isRIPRelative()) {
      AM = Backup;
      break;
    }

    int Cost = 0;
    SDValue RHS = Handle.getValue().getOperand(1);
    // If the RHS involves a register with multiple uses, this
    // transformation incurs an extra mov, due to the neg instruction
    // clobbering its operand.
    if (!RHS.getNode()->hasOneUse() ||
        RHS.getNode()->getOpcode() == ISD::CopyFromReg ||
        RHS.getNode()->getOpcode() == ISD::TRUNCATE ||
        RHS.getNode()->getOpcode() == ISD::ANY_EXTEND ||
        (RHS.getNode()->getOpcode() == ISD::ZERO_EXTEND &&
         RHS.getOperand(0).getValueType() == MVT::i32))
      ++Cost;
    // If the base is a register with multiple uses, this
    // transformation may save a mov.
    // FIXME: Don't rely on DELETED_NODEs.
    if ((AM.BaseType == X86ISelAddressMode::RegBase && AM.Base_Reg.getNode() &&
         AM.Base_Reg->getOpcode() != ISD::DELETED_NODE &&
         !AM.Base_Reg.getNode()->hasOneUse()) ||
        AM.BaseType == X86ISelAddressMode::FrameIndexBase)
      --Cost;
    // If the folded LHS was interesting, this transformation saves
    // address arithmetic.
    if ((AM.hasSymbolicDisplacement() && !Backup.hasSymbolicDisplacement()) +
        ((AM.Disp != 0) && (Backup.Disp == 0)) +
        (AM.Segment.getNode() && !Backup.Segment.getNode()) >= 2)
      --Cost;
    // If it doesn't look like it may be an overall win, don't do it.
    if (Cost >= 0) {
      AM = Backup;
      break;
    }

    // Ok, the transformation is legal and appears profitable. Go for it.
    SDValue Zero = CurDAG->getConstant(0, dl, N.getValueType());
    SDValue Neg = CurDAG->getNode(ISD::SUB, dl, N.getValueType(), Zero, RHS);
    AM.IndexReg = Neg;
    AM.Scale = 1;

    // Insert the new nodes into the topological ordering.
    insertDAGNode(*CurDAG, Handle.getValue(), Zero);
    insertDAGNode(*CurDAG, Handle.getValue(), Neg);
    return false;
  }

  case ISD::ADD:
    if (!matchAdd(N, AM, Depth))
      return false;
    break;

  case ISD::OR:
    // We want to look through a transform in InstCombine and DAGCombiner that
    // turns 'add' into 'or', so we can treat this 'or' exactly like an 'add'.
    // Example: (or (and x, 1), (shl y, 3)) --> (add (and x, 1), (shl y, 3))
    // An 'lea' can then be used to match the shift (multiply) and add:
    // and $1, %esi
    // lea (%rsi, %rdi, 8), %rax
    if (CurDAG->haveNoCommonBitsSet(N.getOperand(0), N.getOperand(1)) &&
        !matchAdd(N, AM, Depth))
      return false;
    break;

  case ISD::AND: {
    // Perform some heroic transforms on an and of a constant-count shift
    // with a constant to enable use of the scaled offset field.

    // Scale must not be used already.
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1) break;

    SDValue Shift = N.getOperand(0);
    if (Shift.getOpcode() != ISD::SRL && Shift.getOpcode() != ISD::SHL) break;
    SDValue X = Shift.getOperand(0);

    // We only handle up to 64-bit values here as those are what matter for
    // addressing mode optimizations.
    if (X.getSimpleValueType().getSizeInBits() > 64) break;

    if (!isa<ConstantSDNode>(N.getOperand(1)))
      break;
    uint64_t Mask = N.getConstantOperandVal(1);

    // Try to fold the mask and shift into an extract and scale.
    if (!foldMaskAndShiftToExtract(*CurDAG, N, Mask, Shift, X, AM))
      return false;

    // Try to fold the mask and shift directly into the scale.
    if (!foldMaskAndShiftToScale(*CurDAG, N, Mask, Shift, X, AM))
      return false;

    // Try to swap the mask and shift to place shifts which can be done as
    // a scale on the outside of the mask.
    if (!foldMaskedShiftToScaledMask(*CurDAG, N, Mask, Shift, X, AM))
      return false;

    // Try to fold the mask and shift into BEXTR and scale.
    if (!foldMaskedShiftToBEXTR(*CurDAG, N, Mask, Shift, X, AM, *Subtarget))
      return false;

    break;
  }
  }

  return matchAddressBase(N, AM);
}

/// Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool X86DAGToDAGISel::matchAddressBase(SDValue N, X86ISelAddressMode &AM) {
  // Is the base register already occupied?
  if (AM.BaseType != X86ISelAddressMode::RegBase || AM.Base_Reg.getNode()) {
    // If so, check to see if the scale index register is set.
    if (!AM.IndexReg.getNode()) {
      AM.IndexReg = N;
      AM.Scale = 1;
      return false;
    }

    // Otherwise, we cannot select it.
    return true;
  }

  // Default, generate it as a register.
  AM.BaseType = X86ISelAddressMode::RegBase;
  AM.Base_Reg = N;
  return false;
}

/// Helper for selectVectorAddr. Handles things that can be folded into a
/// gather scatter address. The index register and scale should have already
/// been handled.
bool X86DAGToDAGISel::matchVectorAddress(SDValue N, X86ISelAddressMode &AM) {
  // TODO: Support other operations.
  switch (N.getOpcode()) {
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (!foldOffsetIntoAddress(Val, AM))
      return false;
    break;
  }
  case X86ISD::Wrapper:
    if (!matchWrapper(N, AM))
      return false;
    break;
  }

  return matchAddressBase(N, AM);
}

bool X86DAGToDAGISel::selectVectorAddr(SDNode *Parent, SDValue N, SDValue &Base,
                                       SDValue &Scale, SDValue &Index,
                                       SDValue &Disp, SDValue &Segment) {
  X86ISelAddressMode AM;
  auto *Mgs = cast<X86MaskedGatherScatterSDNode>(Parent);
  AM.IndexReg = Mgs->getIndex();
  AM.Scale = cast<ConstantSDNode>(Mgs->getScale())->getZExtValue();

  unsigned AddrSpace = cast<MemSDNode>(Parent)->getPointerInfo().getAddrSpace();
  // AddrSpace 256 -> GS, 257 -> FS, 258 -> SS.
  if (AddrSpace == 256)
    AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
  if (AddrSpace == 257)
    AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
  if (AddrSpace == 258)
    AM.Segment = CurDAG->getRegister(X86::SS, MVT::i16);

  // Try to match into the base and displacement fields.
  if (matchVectorAddress(N, AM))
    return false;

  MVT VT = N.getSimpleValueType();
  if (AM.BaseType == X86ISelAddressMode::RegBase) {
    if (!AM.Base_Reg.getNode())
      AM.Base_Reg = CurDAG->getRegister(0, VT);
  }

  getAddressOperands(AM, SDLoc(N), Base, Scale, Index, Disp, Segment);
  return true;
}

/// Returns true if it is able to pattern match an addressing mode.
/// It returns the operands which make up the maximal addressing mode it can
/// match by reference.
///
/// Parent is the parent node of the addr operand that is being matched.  It
/// is always a load, store, atomic node, or null.  It is only null when
/// checking memory operands for inline asm nodes.
bool X86DAGToDAGISel::selectAddr(SDNode *Parent, SDValue N, SDValue &Base,
                                 SDValue &Scale, SDValue &Index,
                                 SDValue &Disp, SDValue &Segment) {
  X86ISelAddressMode AM;

  if (Parent &&
      // This list of opcodes are all the nodes that have an "addr:$ptr" operand
      // that are not a MemSDNode, and thus don't have proper addrspace info.
      Parent->getOpcode() != ISD::INTRINSIC_W_CHAIN && // unaligned loads, fixme
      Parent->getOpcode() != ISD::INTRINSIC_VOID && // nontemporal stores
      Parent->getOpcode() != X86ISD::TLSCALL && // Fixme
      Parent->getOpcode() != X86ISD::EH_SJLJ_SETJMP && // setjmp
      Parent->getOpcode() != X86ISD::EH_SJLJ_LONGJMP) { // longjmp
    unsigned AddrSpace =
      cast<MemSDNode>(Parent)->getPointerInfo().getAddrSpace();
    // AddrSpace 256 -> GS, 257 -> FS, 258 -> SS.
    if (AddrSpace == 256)
      AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
    if (AddrSpace == 257)
      AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
    if (AddrSpace == 258)
      AM.Segment = CurDAG->getRegister(X86::SS, MVT::i16);
  }

  if (matchAddress(N, AM))
    return false;

  MVT VT = N.getSimpleValueType();
  if (AM.BaseType == X86ISelAddressMode::RegBase) {
    if (!AM.Base_Reg.getNode())
      AM.Base_Reg = CurDAG->getRegister(0, VT);
  }

  if (!AM.IndexReg.getNode())
    AM.IndexReg = CurDAG->getRegister(0, VT);

  getAddressOperands(AM, SDLoc(N), Base, Scale, Index, Disp, Segment);
  return true;
}

// We can only fold a load if all nodes between it and the root node have a
// single use. If there are additional uses, we could end up duplicating the
// load.
static bool hasSingleUsesFromRoot(SDNode *Root, SDNode *User) {
  while (User != Root) {
    if (!User->hasOneUse())
      return false;
    User = *User->use_begin();
  }

  return true;
}

/// Match a scalar SSE load. In particular, we want to match a load whose top
/// elements are either undef or zeros. The load flavor is derived from the
/// type of N, which is either v4f32 or v2f64.
///
/// We also return:
///   PatternChainNode: this is the matched node that has a chain input and
///   output.
bool X86DAGToDAGISel::selectScalarSSELoad(SDNode *Root, SDNode *Parent,
                                          SDValue N, SDValue &Base,
                                          SDValue &Scale, SDValue &Index,
                                          SDValue &Disp, SDValue &Segment,
                                          SDValue &PatternNodeWithChain) {
  if (!hasSingleUsesFromRoot(Root, Parent))
    return false;

  // We can allow a full vector load here since narrowing a load is ok.
  if (ISD::isNON_EXTLoad(N.getNode())) {
    PatternNodeWithChain = N;
    if (IsProfitableToFold(PatternNodeWithChain, N.getNode(), Root) &&
        IsLegalToFold(PatternNodeWithChain, Parent, Root, OptLevel)) {
      LoadSDNode *LD = cast<LoadSDNode>(PatternNodeWithChain);
      return selectAddr(LD, LD->getBasePtr(), Base, Scale, Index, Disp,
                        Segment);
    }
  }

  // We can also match the special zero extended load opcode.
  if (N.getOpcode() == X86ISD::VZEXT_LOAD) {
    PatternNodeWithChain = N;
    if (IsProfitableToFold(PatternNodeWithChain, N.getNode(), Root) &&
        IsLegalToFold(PatternNodeWithChain, Parent, Root, OptLevel)) {
      auto *MI = cast<MemIntrinsicSDNode>(PatternNodeWithChain);
      return selectAddr(MI, MI->getBasePtr(), Base, Scale, Index, Disp,
                        Segment);
    }
  }

  // Need to make sure that the SCALAR_TO_VECTOR and load are both only used
  // once. Otherwise the load might get duplicated and the chain output of the
  // duplicate load will not be observed by all dependencies.
  if (N.getOpcode() == ISD::SCALAR_TO_VECTOR && N.getNode()->hasOneUse()) {
    PatternNodeWithChain = N.getOperand(0);
    if (ISD::isNON_EXTLoad(PatternNodeWithChain.getNode()) &&
        IsProfitableToFold(PatternNodeWithChain, N.getNode(), Root) &&
        IsLegalToFold(PatternNodeWithChain, N.getNode(), Root, OptLevel)) {
      LoadSDNode *LD = cast<LoadSDNode>(PatternNodeWithChain);
      return selectAddr(LD, LD->getBasePtr(), Base, Scale, Index, Disp,
                        Segment);
    }
  }

  // Also handle the case where we explicitly require zeros in the top
  // elements.  This is a vector shuffle from the zero vector.
  if (N.getOpcode() == X86ISD::VZEXT_MOVL && N.getNode()->hasOneUse() &&
      // Check to see if the top elements are all zeros (or bitcast of zeros).
      N.getOperand(0).getOpcode() == ISD::SCALAR_TO_VECTOR &&
      N.getOperand(0).getNode()->hasOneUse()) {
    PatternNodeWithChain = N.getOperand(0).getOperand(0);
    if (ISD::isNON_EXTLoad(PatternNodeWithChain.getNode()) &&
        IsProfitableToFold(PatternNodeWithChain, N.getNode(), Root) &&
        IsLegalToFold(PatternNodeWithChain, N.getNode(), Root, OptLevel)) {
      // Okay, this is a zero extending load.  Fold it.
      LoadSDNode *LD = cast<LoadSDNode>(PatternNodeWithChain);
      return selectAddr(LD, LD->getBasePtr(), Base, Scale, Index, Disp,
                        Segment);
    }
  }

  return false;
}


bool X86DAGToDAGISel::selectMOV64Imm32(SDValue N, SDValue &Imm) {
  if (const ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N)) {
    uint64_t ImmVal = CN->getZExtValue();
    if (!isUInt<32>(ImmVal))
      return false;

    Imm = CurDAG->getTargetConstant(ImmVal, SDLoc(N), MVT::i64);
    return true;
  }

  // In static codegen with small code model, we can get the address of a label
  // into a register with 'movl'
  if (N->getOpcode() != X86ISD::Wrapper)
    return false;

  N = N.getOperand(0);

  // At least GNU as does not accept 'movl' for TPOFF relocations.
  // FIXME: We could use 'movl' when we know we are targeting MC.
  if (N->getOpcode() == ISD::TargetGlobalTLSAddress)
    return false;

  Imm = N;
  if (N->getOpcode() != ISD::TargetGlobalAddress)
    return TM.getCodeModel() == CodeModel::Small;

  Optional<ConstantRange> CR =
      cast<GlobalAddressSDNode>(N)->getGlobal()->getAbsoluteSymbolRange();
  if (!CR)
    return TM.getCodeModel() == CodeModel::Small;

  return CR->getUnsignedMax().ult(1ull << 32);
}

bool X86DAGToDAGISel::selectLEA64_32Addr(SDValue N, SDValue &Base,
                                         SDValue &Scale, SDValue &Index,
                                         SDValue &Disp, SDValue &Segment) {
  // Save the debug loc before calling selectLEAAddr, in case it invalidates N.
  SDLoc DL(N);

  if (!selectLEAAddr(N, Base, Scale, Index, Disp, Segment))
    return false;

  RegisterSDNode *RN = dyn_cast<RegisterSDNode>(Base);
  if (RN && RN->getReg() == 0)
    Base = CurDAG->getRegister(0, MVT::i64);
  else if (Base.getValueType() == MVT::i32 && !dyn_cast<FrameIndexSDNode>(Base)) {
    // Base could already be %rip, particularly in the x32 ABI.
    Base = SDValue(CurDAG->getMachineNode(
                       TargetOpcode::SUBREG_TO_REG, DL, MVT::i64,
                       CurDAG->getTargetConstant(0, DL, MVT::i64),
                       Base,
                       CurDAG->getTargetConstant(X86::sub_32bit, DL, MVT::i32)),
                   0);
  }

  RN = dyn_cast<RegisterSDNode>(Index);
  if (RN && RN->getReg() == 0)
    Index = CurDAG->getRegister(0, MVT::i64);
  else {
    assert(Index.getValueType() == MVT::i32 &&
           "Expect to be extending 32-bit registers for use in LEA");
    Index = SDValue(CurDAG->getMachineNode(
                        TargetOpcode::SUBREG_TO_REG, DL, MVT::i64,
                        CurDAG->getTargetConstant(0, DL, MVT::i64),
                        Index,
                        CurDAG->getTargetConstant(X86::sub_32bit, DL,
                                                  MVT::i32)),
                    0);
  }

  return true;
}

/// Calls SelectAddr and determines if the maximal addressing
/// mode it matches can be cost effectively emitted as an LEA instruction.
bool X86DAGToDAGISel::selectLEAAddr(SDValue N,
                                    SDValue &Base, SDValue &Scale,
                                    SDValue &Index, SDValue &Disp,
                                    SDValue &Segment) {
  X86ISelAddressMode AM;

  // Save the DL and VT before calling matchAddress, it can invalidate N.
  SDLoc DL(N);
  MVT VT = N.getSimpleValueType();

  // Set AM.Segment to prevent MatchAddress from using one. LEA doesn't support
  // segments.
  SDValue Copy = AM.Segment;
  SDValue T = CurDAG->getRegister(0, MVT::i32);
  AM.Segment = T;
  if (matchAddress(N, AM))
    return false;
  assert (T == AM.Segment);
  AM.Segment = Copy;

  unsigned Complexity = 0;
  if (AM.BaseType == X86ISelAddressMode::RegBase)
    if (AM.Base_Reg.getNode())
      Complexity = 1;
    else
      AM.Base_Reg = CurDAG->getRegister(0, VT);
  else if (AM.BaseType == X86ISelAddressMode::FrameIndexBase)
    Complexity = 4;

  if (AM.IndexReg.getNode())
    Complexity++;
  else
    AM.IndexReg = CurDAG->getRegister(0, VT);

  // Don't match just leal(,%reg,2). It's cheaper to do addl %reg, %reg, or with
  // a simple shift.
  if (AM.Scale > 1)
    Complexity++;

  // FIXME: We are artificially lowering the criteria to turn ADD %reg, $GA
  // to a LEA. This is determined with some experimentation but is by no means
  // optimal (especially for code size consideration). LEA is nice because of
  // its three-address nature. Tweak the cost function again when we can run
  // convertToThreeAddress() at register allocation time.
  if (AM.hasSymbolicDisplacement()) {
    // For X86-64, always use LEA to materialize RIP-relative addresses.
    if (Subtarget->is64Bit())
      Complexity = 4;
    else
      Complexity += 2;
  }

  if (AM.Disp && (AM.Base_Reg.getNode() || AM.IndexReg.getNode()))
    Complexity++;

  // If it isn't worth using an LEA, reject it.
  if (Complexity <= 2)
    return false;

  getAddressOperands(AM, DL, Base, Scale, Index, Disp, Segment);
  return true;
}

/// This is only run on TargetGlobalTLSAddress nodes.
bool X86DAGToDAGISel::selectTLSADDRAddr(SDValue N, SDValue &Base,
                                        SDValue &Scale, SDValue &Index,
                                        SDValue &Disp, SDValue &Segment) {
  assert(N.getOpcode() == ISD::TargetGlobalTLSAddress);
  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(N);

  X86ISelAddressMode AM;
  AM.GV = GA->getGlobal();
  AM.Disp += GA->getOffset();
  AM.Base_Reg = CurDAG->getRegister(0, N.getValueType());
  AM.SymbolFlags = GA->getTargetFlags();

  if (N.getValueType() == MVT::i32) {
    AM.Scale = 1;
    AM.IndexReg = CurDAG->getRegister(X86::EBX, MVT::i32);
  } else {
    AM.IndexReg = CurDAG->getRegister(0, MVT::i64);
  }

  getAddressOperands(AM, SDLoc(N), Base, Scale, Index, Disp, Segment);
  return true;
}

bool X86DAGToDAGISel::selectRelocImm(SDValue N, SDValue &Op) {
  if (auto *CN = dyn_cast<ConstantSDNode>(N)) {
    Op = CurDAG->getTargetConstant(CN->getAPIntValue(), SDLoc(CN),
                                   N.getValueType());
    return true;
  }

  // Keep track of the original value type and whether this value was
  // truncated. If we see a truncation from pointer type to VT that truncates
  // bits that are known to be zero, we can use a narrow reference.
  EVT VT = N.getValueType();
  bool WasTruncated = false;
  if (N.getOpcode() == ISD::TRUNCATE) {
    WasTruncated = true;
    N = N.getOperand(0);
  }

  if (N.getOpcode() != X86ISD::Wrapper)
    return false;

  // We can only use non-GlobalValues as immediates if they were not truncated,
  // as we do not have any range information. If we have a GlobalValue and the
  // address was not truncated, we can select it as an operand directly.
  unsigned Opc = N.getOperand(0)->getOpcode();
  if (Opc != ISD::TargetGlobalAddress || !WasTruncated) {
    Op = N.getOperand(0);
    // We can only select the operand directly if we didn't have to look past a
    // truncate.
    return !WasTruncated;
  }

  // Check that the global's range fits into VT.
  auto *GA = cast<GlobalAddressSDNode>(N.getOperand(0));
  Optional<ConstantRange> CR = GA->getGlobal()->getAbsoluteSymbolRange();
  if (!CR || CR->getUnsignedMax().uge(1ull << VT.getSizeInBits()))
    return false;

  // Okay, we can use a narrow reference.
  Op = CurDAG->getTargetGlobalAddress(GA->getGlobal(), SDLoc(N), VT,
                                      GA->getOffset(), GA->getTargetFlags());
  return true;
}

bool X86DAGToDAGISel::tryFoldLoad(SDNode *Root, SDNode *P, SDValue N,
                                  SDValue &Base, SDValue &Scale,
                                  SDValue &Index, SDValue &Disp,
                                  SDValue &Segment) {
  if (!ISD::isNON_EXTLoad(N.getNode()) ||
      !IsProfitableToFold(N, P, Root) ||
      !IsLegalToFold(N, P, Root, OptLevel))
    return false;

  return selectAddr(N.getNode(),
                    N.getOperand(1), Base, Scale, Index, Disp, Segment);
}

/// Return an SDNode that returns the value of the global base register.
/// Output instructions required to initialize the global base register,
/// if necessary.
SDNode *X86DAGToDAGISel::getGlobalBaseReg() {
  unsigned GlobalBaseReg = getInstrInfo()->getGlobalBaseReg(MF);
  auto &DL = MF->getDataLayout();
  return CurDAG->getRegister(GlobalBaseReg, TLI->getPointerTy(DL)).getNode();
}

bool X86DAGToDAGISel::isSExtAbsoluteSymbolRef(unsigned Width, SDNode *N) const {
  if (N->getOpcode() == ISD::TRUNCATE)
    N = N->getOperand(0).getNode();
  if (N->getOpcode() != X86ISD::Wrapper)
    return false;

  auto *GA = dyn_cast<GlobalAddressSDNode>(N->getOperand(0));
  if (!GA)
    return false;

  Optional<ConstantRange> CR = GA->getGlobal()->getAbsoluteSymbolRange();
  return CR && CR->getSignedMin().sge(-1ull << Width) &&
         CR->getSignedMax().slt(1ull << Width);
}

static X86::CondCode getCondFromOpc(unsigned Opc) {
  X86::CondCode CC = X86::COND_INVALID;
  if (CC == X86::COND_INVALID)
    CC = X86::getCondFromBranchOpc(Opc);
  if (CC == X86::COND_INVALID)
    CC = X86::getCondFromSETOpc(Opc);
  if (CC == X86::COND_INVALID)
    CC = X86::getCondFromCMovOpc(Opc);

  return CC;
}

/// Test whether the given X86ISD::CMP node has any users that use a flag
/// other than ZF.
bool X86DAGToDAGISel::onlyUsesZeroFlag(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;
    // Only examine CopyToReg uses that copy to EFLAGS.
    if (UI->getOpcode() != ISD::CopyToReg ||
        cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
      return false;
    // Examine each user of the CopyToReg use.
    for (SDNode::use_iterator FlagUI = UI->use_begin(),
           FlagUE = UI->use_end(); FlagUI != FlagUE; ++FlagUI) {
      // Only examine the Flag result.
      if (FlagUI.getUse().getResNo() != 1) continue;
      // Anything unusual: assume conservatively.
      if (!FlagUI->isMachineOpcode()) return false;
      // Examine the condition code of the user.
      X86::CondCode CC = getCondFromOpc(FlagUI->getMachineOpcode());

      switch (CC) {
      // Comparisons which only use the zero flag.
      case X86::COND_E: case X86::COND_NE:
        continue;
      // Anything else: assume conservatively.
      default:
        return false;
      }
    }
  }
  return true;
}

/// Test whether the given X86ISD::CMP node has any uses which require the SF
/// flag to be accurate.
bool X86DAGToDAGISel::hasNoSignFlagUses(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;
    // Only examine CopyToReg uses that copy to EFLAGS.
    if (UI->getOpcode() != ISD::CopyToReg ||
        cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
      return false;
    // Examine each user of the CopyToReg use.
    for (SDNode::use_iterator FlagUI = UI->use_begin(),
           FlagUE = UI->use_end(); FlagUI != FlagUE; ++FlagUI) {
      // Only examine the Flag result.
      if (FlagUI.getUse().getResNo() != 1) continue;
      // Anything unusual: assume conservatively.
      if (!FlagUI->isMachineOpcode()) return false;
      // Examine the condition code of the user.
      X86::CondCode CC = getCondFromOpc(FlagUI->getMachineOpcode());

      switch (CC) {
      // Comparisons which don't examine the SF flag.
      case X86::COND_A: case X86::COND_AE:
      case X86::COND_B: case X86::COND_BE:
      case X86::COND_E: case X86::COND_NE:
      case X86::COND_O: case X86::COND_NO:
      case X86::COND_P: case X86::COND_NP:
        continue;
      // Anything else: assume conservatively.
      default:
        return false;
      }
    }
  }
  return true;
}

static bool mayUseCarryFlag(X86::CondCode CC) {
  switch (CC) {
  // Comparisons which don't examine the CF flag.
  case X86::COND_O: case X86::COND_NO:
  case X86::COND_E: case X86::COND_NE:
  case X86::COND_S: case X86::COND_NS:
  case X86::COND_P: case X86::COND_NP:
  case X86::COND_L: case X86::COND_GE:
  case X86::COND_G: case X86::COND_LE:
    return false;
  // Anything else: assume conservatively.
  default:
    return true;
  }
}

/// Test whether the given node which sets flags has any uses which require the
/// CF flag to be accurate.
 bool X86DAGToDAGISel::hasNoCarryFlagUses(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;

    unsigned UIOpc = UI->getOpcode();

    if (UIOpc == ISD::CopyToReg) {
      // Only examine CopyToReg uses that copy to EFLAGS.
      if (cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
        return false;
      // Examine each user of the CopyToReg use.
      for (SDNode::use_iterator FlagUI = UI->use_begin(), FlagUE = UI->use_end();
           FlagUI != FlagUE; ++FlagUI) {
        // Only examine the Flag result.
        if (FlagUI.getUse().getResNo() != 1)
          continue;
        // Anything unusual: assume conservatively.
        if (!FlagUI->isMachineOpcode())
          return false;
        // Examine the condition code of the user.
        X86::CondCode CC = getCondFromOpc(FlagUI->getMachineOpcode());

        if (mayUseCarryFlag(CC))
          return false;
      }

      // This CopyToReg is ok. Move on to the next user.
      continue;
    }

    // This might be an unselected node. So look for the pre-isel opcodes that
    // use flags.
    unsigned CCOpNo;
    switch (UIOpc) {
    default:
      // Something unusual. Be conservative.
      return false;
    case X86ISD::SETCC:       CCOpNo = 0; break;
    case X86ISD::SETCC_CARRY: CCOpNo = 0; break;
    case X86ISD::CMOV:        CCOpNo = 2; break;
    case X86ISD::BRCOND:      CCOpNo = 2; break;
    }

    X86::CondCode CC = (X86::CondCode)UI->getConstantOperandVal(CCOpNo);
    if (mayUseCarryFlag(CC))
      return false;
  }
  return true;
}

/// Check whether or not the chain ending in StoreNode is suitable for doing
/// the {load; op; store} to modify transformation.
static bool isFusableLoadOpStorePattern(StoreSDNode *StoreNode,
                                        SDValue StoredVal, SelectionDAG *CurDAG,
                                        unsigned LoadOpNo,
                                        LoadSDNode *&LoadNode,
                                        SDValue &InputChain) {
  // Is the stored value result 0 of the operation?
  if (StoredVal.getResNo() != 0) return false;

  // Are there other uses of the operation other than the store?
  if (!StoredVal.getNode()->hasNUsesOfValue(1, 0)) return false;

  // Is the store non-extending and non-indexed?
  if (!ISD::isNormalStore(StoreNode) || StoreNode->isNonTemporal())
    return false;

  SDValue Load = StoredVal->getOperand(LoadOpNo);
  // Is the stored value a non-extending and non-indexed load?
  if (!ISD::isNormalLoad(Load.getNode())) return false;

  // Return LoadNode by reference.
  LoadNode = cast<LoadSDNode>(Load);

  // Is store the only read of the loaded value?
  if (!Load.hasOneUse())
    return false;

  // Is the address of the store the same as the load?
  if (LoadNode->getBasePtr() != StoreNode->getBasePtr() ||
      LoadNode->getOffset() != StoreNode->getOffset())
    return false;

  bool FoundLoad = false;
  SmallVector<SDValue, 4> ChainOps;
  SmallVector<const SDNode *, 4> LoopWorklist;
  SmallPtrSet<const SDNode *, 16> Visited;
  const unsigned int Max = 1024;

  //  Visualization of Load-Op-Store fusion:
  // -------------------------
  // Legend:
  //    *-lines = Chain operand dependencies.
  //    |-lines = Normal operand dependencies.
  //    Dependencies flow down and right. n-suffix references multiple nodes.
  //
  //        C                        Xn  C
  //        *                         *  *
  //        *                          * *
  //  Xn  A-LD    Yn                    TF         Yn
  //   *    * \   |                       *        |
  //    *   *  \  |                        *       |
  //     *  *   \ |             =>       A--LD_OP_ST
  //      * *    \|                                 \
  //       TF    OP                                  \
  //         *   | \                                  Zn
  //          *  |  \
  //         A-ST    Zn
  //

  // This merge induced dependences from: #1: Xn -> LD, OP, Zn
  //                                      #2: Yn -> LD
  //                                      #3: ST -> Zn

  // Ensure the transform is safe by checking for the dual
  // dependencies to make sure we do not induce a loop.

  // As LD is a predecessor to both OP and ST we can do this by checking:
  //  a). if LD is a predecessor to a member of Xn or Yn.
  //  b). if a Zn is a predecessor to ST.

  // However, (b) can only occur through being a chain predecessor to
  // ST, which is the same as Zn being a member or predecessor of Xn,
  // which is a subset of LD being a predecessor of Xn. So it's
  // subsumed by check (a).

  SDValue Chain = StoreNode->getChain();

  // Gather X elements in ChainOps.
  if (Chain == Load.getValue(1)) {
    FoundLoad = true;
    ChainOps.push_back(Load.getOperand(0));
  } else if (Chain.getOpcode() == ISD::TokenFactor) {
    for (unsigned i = 0, e = Chain.getNumOperands(); i != e; ++i) {
      SDValue Op = Chain.getOperand(i);
      if (Op == Load.getValue(1)) {
        FoundLoad = true;
        // Drop Load, but keep its chain. No cycle check necessary.
        ChainOps.push_back(Load.getOperand(0));
        continue;
      }
      LoopWorklist.push_back(Op.getNode());
      ChainOps.push_back(Op);
    }
  }

  if (!FoundLoad)
    return false;

  // Worklist is currently Xn. Add Yn to worklist.
  for (SDValue Op : StoredVal->ops())
    if (Op.getNode() != LoadNode)
      LoopWorklist.push_back(Op.getNode());

  // Check (a) if Load is a predecessor to Xn + Yn
  if (SDNode::hasPredecessorHelper(Load.getNode(), Visited, LoopWorklist, Max,
                                   true))
    return false;

  InputChain =
      CurDAG->getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ChainOps);
  return true;
}

// Change a chain of {load; op; store} of the same value into a simple op
// through memory of that value, if the uses of the modified value and its
// address are suitable.
//
// The tablegen pattern memory operand pattern is currently not able to match
// the case where the EFLAGS on the original operation are used.
//
// To move this to tablegen, we'll need to improve tablegen to allow flags to
// be transferred from a node in the pattern to the result node, probably with
// a new keyword. For example, we have this
// def DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst), "dec{q}\t$dst",
//  [(store (add (loadi64 addr:$dst), -1), addr:$dst),
//   (implicit EFLAGS)]>;
// but maybe need something like this
// def DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst), "dec{q}\t$dst",
//  [(store (add (loadi64 addr:$dst), -1), addr:$dst),
//   (transferrable EFLAGS)]>;
//
// Until then, we manually fold these and instruction select the operation
// here.
bool X86DAGToDAGISel::foldLoadStoreIntoMemOperand(SDNode *Node) {
  StoreSDNode *StoreNode = cast<StoreSDNode>(Node);
  SDValue StoredVal = StoreNode->getOperand(1);
  unsigned Opc = StoredVal->getOpcode();

  // Before we try to select anything, make sure this is memory operand size
  // and opcode we can handle. Note that this must match the code below that
  // actually lowers the opcodes.
  EVT MemVT = StoreNode->getMemoryVT();
  if (MemVT != MVT::i64 && MemVT != MVT::i32 && MemVT != MVT::i16 &&
      MemVT != MVT::i8)
    return false;

  bool IsCommutable = false;
  switch (Opc) {
  default:
    return false;
  case X86ISD::SUB:
  case X86ISD::SBB:
    break;
  case X86ISD::ADD:
  case X86ISD::ADC:
  case X86ISD::AND:
  case X86ISD::OR:
  case X86ISD::XOR:
    IsCommutable = true;
    break;
  }

  unsigned LoadOpNo = 0;
  LoadSDNode *LoadNode = nullptr;
  SDValue InputChain;
  if (!isFusableLoadOpStorePattern(StoreNode, StoredVal, CurDAG, LoadOpNo,
                                   LoadNode, InputChain)) {
    if (!IsCommutable)
      return false;

    // This operation is commutable, try the other operand.
    LoadOpNo = 1;
    if (!isFusableLoadOpStorePattern(StoreNode, StoredVal, CurDAG, LoadOpNo,
                                     LoadNode, InputChain))
      return false;
  }

  SDValue Base, Scale, Index, Disp, Segment;
  if (!selectAddr(LoadNode, LoadNode->getBasePtr(), Base, Scale, Index, Disp,
                  Segment))
    return false;

  auto SelectOpcode = [&](unsigned Opc64, unsigned Opc32, unsigned Opc16,
                          unsigned Opc8) {
    switch (MemVT.getSimpleVT().SimpleTy) {
    case MVT::i64:
      return Opc64;
    case MVT::i32:
      return Opc32;
    case MVT::i16:
      return Opc16;
    case MVT::i8:
      return Opc8;
    default:
      llvm_unreachable("Invalid size!");
    }
  };

  MachineSDNode *Result;
  switch (Opc) {
  case X86ISD::ADD:
  case X86ISD::SUB:
    // Try to match inc/dec.
    if (!Subtarget->slowIncDec() ||
        CurDAG->getMachineFunction().getFunction().optForSize()) {
      bool IsOne = isOneConstant(StoredVal.getOperand(1));
      bool IsNegOne = isAllOnesConstant(StoredVal.getOperand(1));
      // ADD/SUB with 1/-1 and carry flag isn't used can use inc/dec.
      if ((IsOne || IsNegOne) && hasNoCarryFlagUses(StoredVal.getValue(1))) {
        unsigned NewOpc = 
          ((Opc == X86ISD::ADD) == IsOne)
              ? SelectOpcode(X86::INC64m, X86::INC32m, X86::INC16m, X86::INC8m)
              : SelectOpcode(X86::DEC64m, X86::DEC32m, X86::DEC16m, X86::DEC8m);
        const SDValue Ops[] = {Base, Scale, Index, Disp, Segment, InputChain};
        Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32,
                                        MVT::Other, Ops);
        break;
      }
    }
    LLVM_FALLTHROUGH;
  case X86ISD::ADC:
  case X86ISD::SBB:
  case X86ISD::AND:
  case X86ISD::OR:
  case X86ISD::XOR: {
    auto SelectRegOpcode = [SelectOpcode](unsigned Opc) {
      switch (Opc) {
      case X86ISD::ADD:
        return SelectOpcode(X86::ADD64mr, X86::ADD32mr, X86::ADD16mr,
                            X86::ADD8mr);
      case X86ISD::ADC:
        return SelectOpcode(X86::ADC64mr, X86::ADC32mr, X86::ADC16mr,
                            X86::ADC8mr);
      case X86ISD::SUB:
        return SelectOpcode(X86::SUB64mr, X86::SUB32mr, X86::SUB16mr,
                            X86::SUB8mr);
      case X86ISD::SBB:
        return SelectOpcode(X86::SBB64mr, X86::SBB32mr, X86::SBB16mr,
                            X86::SBB8mr);
      case X86ISD::AND:
        return SelectOpcode(X86::AND64mr, X86::AND32mr, X86::AND16mr,
                            X86::AND8mr);
      case X86ISD::OR:
        return SelectOpcode(X86::OR64mr, X86::OR32mr, X86::OR16mr, X86::OR8mr);
      case X86ISD::XOR:
        return SelectOpcode(X86::XOR64mr, X86::XOR32mr, X86::XOR16mr,
                            X86::XOR8mr);
      default:
        llvm_unreachable("Invalid opcode!");
      }
    };
    auto SelectImm8Opcode = [SelectOpcode](unsigned Opc) {
      switch (Opc) {
      case X86ISD::ADD:
        return SelectOpcode(X86::ADD64mi8, X86::ADD32mi8, X86::ADD16mi8, 0);
      case X86ISD::ADC:
        return SelectOpcode(X86::ADC64mi8, X86::ADC32mi8, X86::ADC16mi8, 0);
      case X86ISD::SUB:
        return SelectOpcode(X86::SUB64mi8, X86::SUB32mi8, X86::SUB16mi8, 0);
      case X86ISD::SBB:
        return SelectOpcode(X86::SBB64mi8, X86::SBB32mi8, X86::SBB16mi8, 0);
      case X86ISD::AND:
        return SelectOpcode(X86::AND64mi8, X86::AND32mi8, X86::AND16mi8, 0);
      case X86ISD::OR:
        return SelectOpcode(X86::OR64mi8, X86::OR32mi8, X86::OR16mi8, 0);
      case X86ISD::XOR:
        return SelectOpcode(X86::XOR64mi8, X86::XOR32mi8, X86::XOR16mi8, 0);
      default:
        llvm_unreachable("Invalid opcode!");
      }
    };
    auto SelectImmOpcode = [SelectOpcode](unsigned Opc) {
      switch (Opc) {
      case X86ISD::ADD:
        return SelectOpcode(X86::ADD64mi32, X86::ADD32mi, X86::ADD16mi,
                            X86::ADD8mi);
      case X86ISD::ADC:
        return SelectOpcode(X86::ADC64mi32, X86::ADC32mi, X86::ADC16mi,
                            X86::ADC8mi);
      case X86ISD::SUB:
        return SelectOpcode(X86::SUB64mi32, X86::SUB32mi, X86::SUB16mi,
                            X86::SUB8mi);
      case X86ISD::SBB:
        return SelectOpcode(X86::SBB64mi32, X86::SBB32mi, X86::SBB16mi,
                            X86::SBB8mi);
      case X86ISD::AND:
        return SelectOpcode(X86::AND64mi32, X86::AND32mi, X86::AND16mi,
                            X86::AND8mi);
      case X86ISD::OR:
        return SelectOpcode(X86::OR64mi32, X86::OR32mi, X86::OR16mi,
                            X86::OR8mi);
      case X86ISD::XOR:
        return SelectOpcode(X86::XOR64mi32, X86::XOR32mi, X86::XOR16mi,
                            X86::XOR8mi);
      default:
        llvm_unreachable("Invalid opcode!");
      }
    };

    unsigned NewOpc = SelectRegOpcode(Opc);
    SDValue Operand = StoredVal->getOperand(1-LoadOpNo);

    // See if the operand is a constant that we can fold into an immediate
    // operand.
    if (auto *OperandC = dyn_cast<ConstantSDNode>(Operand)) {
      auto OperandV = OperandC->getAPIntValue();

      // Check if we can shrink the operand enough to fit in an immediate (or
      // fit into a smaller immediate) by negating it and switching the
      // operation.
      if ((Opc == X86ISD::ADD || Opc == X86ISD::SUB) &&
          ((MemVT != MVT::i8 && OperandV.getMinSignedBits() > 8 &&
            (-OperandV).getMinSignedBits() <= 8) ||
           (MemVT == MVT::i64 && OperandV.getMinSignedBits() > 32 &&
            (-OperandV).getMinSignedBits() <= 32)) &&
          hasNoCarryFlagUses(StoredVal.getValue(1))) {
        OperandV = -OperandV;
        Opc = Opc == X86ISD::ADD ? X86ISD::SUB : X86ISD::ADD;
      }

      // First try to fit this into an Imm8 operand. If it doesn't fit, then try
      // the larger immediate operand.
      if (MemVT != MVT::i8 && OperandV.getMinSignedBits() <= 8) {
        Operand = CurDAG->getTargetConstant(OperandV, SDLoc(Node), MemVT);
        NewOpc = SelectImm8Opcode(Opc);
      } else if (OperandV.getActiveBits() <= MemVT.getSizeInBits() &&
                 (MemVT != MVT::i64 || OperandV.getMinSignedBits() <= 32)) {
        Operand = CurDAG->getTargetConstant(OperandV, SDLoc(Node), MemVT);
        NewOpc = SelectImmOpcode(Opc);
      }
    }

    if (Opc == X86ISD::ADC || Opc == X86ISD::SBB) {
      SDValue CopyTo =
          CurDAG->getCopyToReg(InputChain, SDLoc(Node), X86::EFLAGS,
                               StoredVal.getOperand(2), SDValue());

      const SDValue Ops[] = {Base,    Scale,   Index,  Disp,
                             Segment, Operand, CopyTo, CopyTo.getValue(1)};
      Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32, MVT::Other,
                                      Ops);
    } else {
      const SDValue Ops[] = {Base,    Scale,   Index,     Disp,
                             Segment, Operand, InputChain};
      Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32, MVT::Other,
                                      Ops);
    }
    break;
  }
  default:
    llvm_unreachable("Invalid opcode!");
  }

  MachineMemOperand *MemOps[] = {StoreNode->getMemOperand(),
                                 LoadNode->getMemOperand()};
  CurDAG->setNodeMemRefs(Result, MemOps);

  // Update Load Chain uses as well.
  ReplaceUses(SDValue(LoadNode, 1), SDValue(Result, 1));
  ReplaceUses(SDValue(StoreNode, 0), SDValue(Result, 1));
  ReplaceUses(SDValue(StoredVal.getNode(), 1), SDValue(Result, 0));
  CurDAG->RemoveDeadNode(Node);
  return true;
}

// See if this is an  X & Mask  that we can match to BEXTR/BZHI.
// Where Mask is one of the following patterns:
//   a) x &  (1 << nbits) - 1
//   b) x & ~(-1 << nbits)
//   c) x &  (-1 >> (32 - y))
//   d) x << (32 - y) >> (32 - y)
bool X86DAGToDAGISel::matchBitExtract(SDNode *Node) {
  assert(
      (Node->getOpcode() == ISD::AND || Node->getOpcode() == ISD::SRL) &&
      "Should be either an and-mask, or right-shift after clearing high bits.");

  // BEXTR is BMI instruction, BZHI is BMI2 instruction. We need at least one.
  if (!Subtarget->hasBMI() && !Subtarget->hasBMI2())
    return false;

  MVT NVT = Node->getSimpleValueType(0);

  // Only supported for 32 and 64 bits.
  if (NVT != MVT::i32 && NVT != MVT::i64)
    return false;

  unsigned Size = NVT.getSizeInBits();

  SDValue NBits;

  // If we have BMI2's BZHI, we are ok with muti-use patterns.
  // Else, if we only have BMI1's BEXTR, we require one-use.
  const bool CanHaveExtraUses = Subtarget->hasBMI2();
  auto checkUses = [CanHaveExtraUses](SDValue Op, unsigned NUses) {
    return CanHaveExtraUses ||
           Op.getNode()->hasNUsesOfValue(NUses, Op.getResNo());
  };
  auto checkOneUse = [checkUses](SDValue Op) { return checkUses(Op, 1); };
  auto checkTwoUse = [checkUses](SDValue Op) { return checkUses(Op, 2); };

  // a) x & ((1 << nbits) + (-1))
  auto matchPatternA = [&checkOneUse, &NBits](SDValue Mask) -> bool {
    // Match `add`. Must only have one use!
    if (Mask->getOpcode() != ISD::ADD || !checkOneUse(Mask))
      return false;
    // We should be adding all-ones constant (i.e. subtracting one.)
    if (!isAllOnesConstant(Mask->getOperand(1)))
      return false;
    // Match `1 << nbits`. Must only have one use!
    SDValue M0 = Mask->getOperand(0);
    if (M0->getOpcode() != ISD::SHL || !checkOneUse(M0))
      return false;
    if (!isOneConstant(M0->getOperand(0)))
      return false;
    NBits = M0->getOperand(1);
    return true;
  };

  // b) x & ~(-1 << nbits)
  auto matchPatternB = [&checkOneUse, &NBits](SDValue Mask) -> bool {
    // Match `~()`. Must only have one use!
    if (!isBitwiseNot(Mask) || !checkOneUse(Mask))
      return false;
    // Match `-1 << nbits`. Must only have one use!
    SDValue M0 = Mask->getOperand(0);
    if (M0->getOpcode() != ISD::SHL || !checkOneUse(M0))
      return false;
    if (!isAllOnesConstant(M0->getOperand(0)))
      return false;
    NBits = M0->getOperand(1);
    return true;
  };

  // Match potentially-truncated (bitwidth - y)
  auto matchShiftAmt = [checkOneUse, Size, &NBits](SDValue ShiftAmt) {
    // Skip over a truncate of the shift amount.
    if (ShiftAmt.getOpcode() == ISD::TRUNCATE) {
      ShiftAmt = ShiftAmt.getOperand(0);
      // The trunc should have been the only user of the real shift amount.
      if (!checkOneUse(ShiftAmt))
        return false;
    }
    // Match the shift amount as: (bitwidth - y). It should go away, too.
    if (ShiftAmt.getOpcode() != ISD::SUB)
      return false;
    auto V0 = dyn_cast<ConstantSDNode>(ShiftAmt.getOperand(0));
    if (!V0 || V0->getZExtValue() != Size)
      return false;
    NBits = ShiftAmt.getOperand(1);
    return true;
  };

  // c) x &  (-1 >> (32 - y))
  auto matchPatternC = [&checkOneUse, matchShiftAmt](SDValue Mask) -> bool {
    // Match `l>>`. Must only have one use!
    if (Mask.getOpcode() != ISD::SRL || !checkOneUse(Mask))
      return false;
    // We should be shifting all-ones constant.
    if (!isAllOnesConstant(Mask.getOperand(0)))
      return false;
    SDValue M1 = Mask.getOperand(1);
    // The shift amount should not be used externally.
    if (!checkOneUse(M1))
      return false;
    return matchShiftAmt(M1);
  };

  SDValue X;

  // d) x << (32 - y) >> (32 - y)
  auto matchPatternD = [&checkOneUse, &checkTwoUse, matchShiftAmt,
                        &X](SDNode *Node) -> bool {
    if (Node->getOpcode() != ISD::SRL)
      return false;
    SDValue N0 = Node->getOperand(0);
    if (N0->getOpcode() != ISD::SHL || !checkOneUse(N0))
      return false;
    SDValue N1 = Node->getOperand(1);
    SDValue N01 = N0->getOperand(1);
    // Both of the shifts must be by the exact same value.
    // There should not be any uses of the shift amount outside of the pattern.
    if (N1 != N01 || !checkTwoUse(N1))
      return false;
    if (!matchShiftAmt(N1))
      return false;
    X = N0->getOperand(0);
    return true;
  };

  auto matchLowBitMask = [&matchPatternA, &matchPatternB,
                          &matchPatternC](SDValue Mask) -> bool {
    // FIXME: pattern c.
    return matchPatternA(Mask) || matchPatternB(Mask) || matchPatternC(Mask);
  };

  if (Node->getOpcode() == ISD::AND) {
    X = Node->getOperand(0);
    SDValue Mask = Node->getOperand(1);

    if (matchLowBitMask(Mask)) {
      // Great.
    } else {
      std::swap(X, Mask);
      if (!matchLowBitMask(Mask))
        return false;
    }
  } else if (!matchPatternD(Node))
    return false;

  SDLoc DL(Node);

  // If we do *NOT* have BMI2, let's find out if the if the 'X' is *logically*
  // shifted (potentially with one-use trunc inbetween),
  // and if so look past one-use truncation.
  MVT XVT = NVT;
  if (!Subtarget->hasBMI2() && X.getOpcode() == ISD::TRUNCATE &&
      X.hasOneUse() && X.getOperand(0).getOpcode() == ISD::SRL) {
    assert(NVT == MVT::i32 && "Expected target valuetype to be i32");
    X = X.getOperand(0);
    XVT = X.getSimpleValueType();
    assert(XVT == MVT::i64 && "Expected truncation from i64");
  }

  SDValue OrigNBits = NBits;
  if (NBits.getValueType() != XVT) {
    // Truncate the shift amount.
    NBits = CurDAG->getNode(ISD::TRUNCATE, DL, MVT::i8, NBits);
    insertDAGNode(*CurDAG, OrigNBits, NBits);

    // Insert 8-bit NBits into lowest 8 bits of XVT-sized (32 or 64-bit)
    // register. All the other bits are undefined, we do not care about them.
    SDValue ImplDef =
        SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, XVT), 0);
    insertDAGNode(*CurDAG, OrigNBits, ImplDef);
    NBits =
        CurDAG->getTargetInsertSubreg(X86::sub_8bit, DL, XVT, ImplDef, NBits);
    insertDAGNode(*CurDAG, OrigNBits, NBits);
  }

  if (Subtarget->hasBMI2()) {
    // Great, just emit the the BZHI..
    SDValue Extract = CurDAG->getNode(X86ISD::BZHI, DL, XVT, X, NBits);
    ReplaceNode(Node, Extract.getNode());
    SelectCode(Extract.getNode());
    return true;
  }

  // Else, emitting BEXTR requires one more step.
  // The 'control' of BEXTR has the pattern of:
  // [15...8 bit][ 7...0 bit] location
  // [ bit count][     shift] name
  // I.e. 0b000000011'00000001 means  (x >> 0b1) & 0b11

  // Shift NBits left by 8 bits, thus producing 'control'.
  // This makes the low 8 bits to be zero.
  SDValue C8 = CurDAG->getConstant(8, DL, MVT::i8);
  SDValue Control = CurDAG->getNode(ISD::SHL, DL, XVT, NBits, C8);
  insertDAGNode(*CurDAG, OrigNBits, Control);

  // If the 'X' is *logically* shifted, we can fold that shift into 'control'.
  if (X.getOpcode() == ISD::SRL) {
    SDValue ShiftAmt = X.getOperand(1);
    X = X.getOperand(0);

    assert(ShiftAmt.getValueType() == MVT::i8 &&
           "Expected shift amount to be i8");

    // Now, *zero*-extend the shift amount. The bits 8...15 *must* be zero!
    SDValue OrigShiftAmt = ShiftAmt;
    ShiftAmt = CurDAG->getNode(ISD::ZERO_EXTEND, DL, XVT, ShiftAmt);
    insertDAGNode(*CurDAG, OrigShiftAmt, ShiftAmt);

    // And now 'or' these low 8 bits of shift amount into the 'control'.
    Control = CurDAG->getNode(ISD::OR, DL, XVT, Control, ShiftAmt);
    insertDAGNode(*CurDAG, OrigNBits, Control);
  }

  // And finally, form the BEXTR itself.
  SDValue Extract = CurDAG->getNode(X86ISD::BEXTR, DL, XVT, X, Control);

  // The 'X' was originally truncated. Do that now.
  if (XVT != NVT) {
    insertDAGNode(*CurDAG, OrigNBits, Extract);
    Extract = CurDAG->getNode(ISD::TRUNCATE, DL, NVT, Extract);
  }

  ReplaceNode(Node, Extract.getNode());
  SelectCode(Extract.getNode());

  return true;
}

// See if this is an (X >> C1) & C2 that we can match to BEXTR/BEXTRI.
MachineSDNode *X86DAGToDAGISel::matchBEXTRFromAndImm(SDNode *Node) {
  MVT NVT = Node->getSimpleValueType(0);
  SDLoc dl(Node);

  SDValue N0 = Node->getOperand(0);
  SDValue N1 = Node->getOperand(1);

  // If we have TBM we can use an immediate for the control. If we have BMI
  // we should only do this if the BEXTR instruction is implemented well.
  // Otherwise moving the control into a register makes this more costly.
  // TODO: Maybe load folding, greater than 32-bit masks, or a guarantee of LICM
  // hoisting the move immediate would make it worthwhile with a less optimal
  // BEXTR?
  if (!Subtarget->hasTBM() &&
      !(Subtarget->hasBMI() && Subtarget->hasFastBEXTR()))
    return nullptr;

  // Must have a shift right.
  if (N0->getOpcode() != ISD::SRL && N0->getOpcode() != ISD::SRA)
    return nullptr;

  // Shift can't have additional users.
  if (!N0->hasOneUse())
    return nullptr;

  // Only supported for 32 and 64 bits.
  if (NVT != MVT::i32 && NVT != MVT::i64)
    return nullptr;

  // Shift amount and RHS of and must be constant.
  ConstantSDNode *MaskCst = dyn_cast<ConstantSDNode>(N1);
  ConstantSDNode *ShiftCst = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  if (!MaskCst || !ShiftCst)
    return nullptr;

  // And RHS must be a mask.
  uint64_t Mask = MaskCst->getZExtValue();
  if (!isMask_64(Mask))
    return nullptr;

  uint64_t Shift = ShiftCst->getZExtValue();
  uint64_t MaskSize = countPopulation(Mask);

  // Don't interfere with something that can be handled by extracting AH.
  // TODO: If we are able to fold a load, BEXTR might still be better than AH.
  if (Shift == 8 && MaskSize == 8)
    return nullptr;

  // Make sure we are only using bits that were in the original value, not
  // shifted in.
  if (Shift + MaskSize > NVT.getSizeInBits())
    return nullptr;

  SDValue New = CurDAG->getTargetConstant(Shift | (MaskSize << 8), dl, NVT);
  unsigned ROpc = NVT == MVT::i64 ? X86::BEXTRI64ri : X86::BEXTRI32ri;
  unsigned MOpc = NVT == MVT::i64 ? X86::BEXTRI64mi : X86::BEXTRI32mi;

  // BMI requires the immediate to placed in a register.
  if (!Subtarget->hasTBM()) {
    ROpc = NVT == MVT::i64 ? X86::BEXTR64rr : X86::BEXTR32rr;
    MOpc = NVT == MVT::i64 ? X86::BEXTR64rm : X86::BEXTR32rm;
    unsigned NewOpc = NVT == MVT::i64 ? X86::MOV32ri64 : X86::MOV32ri;
    New = SDValue(CurDAG->getMachineNode(NewOpc, dl, NVT, New), 0);
  }

  MachineSDNode *NewNode;
  SDValue Input = N0->getOperand(0);
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (tryFoldLoad(Node, N0.getNode(), Input, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, New, Input.getOperand(0) };
    SDVTList VTs = CurDAG->getVTList(NVT, MVT::Other);
    NewNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    // Update the chain.
    ReplaceUses(Input.getValue(1), SDValue(NewNode, 1));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(NewNode, {cast<LoadSDNode>(Input)->getMemOperand()});
  } else {
    NewNode = CurDAG->getMachineNode(ROpc, dl, NVT, Input, New);
  }

  return NewNode;
}

// Emit a PCMISTR(I/M) instruction.
MachineSDNode *X86DAGToDAGISel::emitPCMPISTR(unsigned ROpc, unsigned MOpc,
                                             bool MayFoldLoad, const SDLoc &dl,
                                             MVT VT, SDNode *Node) {
  SDValue N0 = Node->getOperand(0);
  SDValue N1 = Node->getOperand(1);
  SDValue Imm = Node->getOperand(2);
  const ConstantInt *Val = cast<ConstantSDNode>(Imm)->getConstantIntValue();
  Imm = CurDAG->getTargetConstant(*Val, SDLoc(Node), Imm.getValueType());

  // Try to fold a load. No need to check alignment.
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (MayFoldLoad && tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = { N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                      N1.getOperand(0) };
    SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Other);
    MachineSDNode *CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    // Update the chain.
    ReplaceUses(N1.getValue(1), SDValue(CNode, 2));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    return CNode;
  }

  SDValue Ops[] = { N0, N1, Imm };
  SDVTList VTs = CurDAG->getVTList(VT, MVT::i32);
  MachineSDNode *CNode = CurDAG->getMachineNode(ROpc, dl, VTs, Ops);
  return CNode;
}

// Emit a PCMESTR(I/M) instruction. Also return the Glue result in case we need
// to emit a second instruction after this one. This is needed since we have two
// copyToReg nodes glued before this and we need to continue that glue through.
MachineSDNode *X86DAGToDAGISel::emitPCMPESTR(unsigned ROpc, unsigned MOpc,
                                             bool MayFoldLoad, const SDLoc &dl,
                                             MVT VT, SDNode *Node,
                                             SDValue &InFlag) {
  SDValue N0 = Node->getOperand(0);
  SDValue N2 = Node->getOperand(2);
  SDValue Imm = Node->getOperand(4);
  const ConstantInt *Val = cast<ConstantSDNode>(Imm)->getConstantIntValue();
  Imm = CurDAG->getTargetConstant(*Val, SDLoc(Node), Imm.getValueType());

  // Try to fold a load. No need to check alignment.
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (MayFoldLoad && tryFoldLoad(Node, N2, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = { N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                      N2.getOperand(0), InFlag };
    SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Other, MVT::Glue);
    MachineSDNode *CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    InFlag = SDValue(CNode, 3);
    // Update the chain.
    ReplaceUses(N2.getValue(1), SDValue(CNode, 2));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N2)->getMemOperand()});
    return CNode;
  }

  SDValue Ops[] = { N0, N2, Imm, InFlag };
  SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Glue);
  MachineSDNode *CNode = CurDAG->getMachineNode(ROpc, dl, VTs, Ops);
  InFlag = SDValue(CNode, 2);
  return CNode;
}

bool X86DAGToDAGISel::tryShiftAmountMod(SDNode *N) {
  EVT VT = N->getValueType(0);

  // Only handle scalar shifts.
  if (VT.isVector())
    return false;

  // Narrower shifts only mask to 5 bits in hardware.
  unsigned Size = VT == MVT::i64 ? 64 : 32;

  SDValue OrigShiftAmt = N->getOperand(1);
  SDValue ShiftAmt = OrigShiftAmt;
  SDLoc DL(N);

  // Skip over a truncate of the shift amount.
  if (ShiftAmt->getOpcode() == ISD::TRUNCATE)
    ShiftAmt = ShiftAmt->getOperand(0);

  // This function is called after X86DAGToDAGISel::matchBitExtract(),
  // so we are not afraid that we might mess up BZHI/BEXTR pattern.

  SDValue NewShiftAmt;
  if (ShiftAmt->getOpcode() == ISD::ADD || ShiftAmt->getOpcode() == ISD::SUB) {
    SDValue Add0 = ShiftAmt->getOperand(0);
    SDValue Add1 = ShiftAmt->getOperand(1);
    // If we are shifting by X+/-N where N == 0 mod Size, then just shift by X
    // to avoid the ADD/SUB.
    if (isa<ConstantSDNode>(Add1) &&
        cast<ConstantSDNode>(Add1)->getZExtValue() % Size == 0) {
      NewShiftAmt = Add0;
    // If we are shifting by N-X where N == 0 mod Size, then just shift by -X to
    // generate a NEG instead of a SUB of a constant.
    } else if (ShiftAmt->getOpcode() == ISD::SUB &&
               isa<ConstantSDNode>(Add0) &&
               cast<ConstantSDNode>(Add0)->getZExtValue() != 0 &&
               cast<ConstantSDNode>(Add0)->getZExtValue() % Size == 0) {
      // Insert a negate op.
      // TODO: This isn't guaranteed to replace the sub if there is a logic cone
      // that uses it that's not a shift.
      EVT SubVT = ShiftAmt.getValueType();
      SDValue Zero = CurDAG->getConstant(0, DL, SubVT);
      SDValue Neg = CurDAG->getNode(ISD::SUB, DL, SubVT, Zero, Add1);
      NewShiftAmt = Neg;

      // Insert these operands into a valid topological order so they can
      // get selected independently.
      insertDAGNode(*CurDAG, OrigShiftAmt, Zero);
      insertDAGNode(*CurDAG, OrigShiftAmt, Neg);
    } else
      return false;
  } else
    return false;

  if (NewShiftAmt.getValueType() != MVT::i8) {
    // Need to truncate the shift amount.
    NewShiftAmt = CurDAG->getNode(ISD::TRUNCATE, DL, MVT::i8, NewShiftAmt);
    // Add to a correct topological ordering.
    insertDAGNode(*CurDAG, OrigShiftAmt, NewShiftAmt);
  }

  // Insert a new mask to keep the shift amount legal. This should be removed
  // by isel patterns.
  NewShiftAmt = CurDAG->getNode(ISD::AND, DL, MVT::i8, NewShiftAmt,
                                CurDAG->getConstant(Size - 1, DL, MVT::i8));
  // Place in a correct topological ordering.
  insertDAGNode(*CurDAG, OrigShiftAmt, NewShiftAmt);

  SDNode *UpdatedNode = CurDAG->UpdateNodeOperands(N, N->getOperand(0),
                                                   NewShiftAmt);
  if (UpdatedNode != N) {
    // If we found an existing node, we should replace ourselves with that node
    // and wait for it to be selected after its other users.
    ReplaceNode(N, UpdatedNode);
    return true;
  }

  // If the original shift amount is now dead, delete it so that we don't run
  // it through isel.
  if (OrigShiftAmt.getNode()->use_empty())
    CurDAG->RemoveDeadNode(OrigShiftAmt.getNode());

  // Now that we've optimized the shift amount, defer to normal isel to get
  // load folding and legacy vs BMI2 selection without repeating it here.
  SelectCode(N);
  return true;
}

/// If the high bits of an 'and' operand are known zero, try setting the
/// high bits of an 'and' constant operand to produce a smaller encoding by
/// creating a small, sign-extended negative immediate rather than a large
/// positive one. This reverses a transform in SimplifyDemandedBits that
/// shrinks mask constants by clearing bits. There is also a possibility that
/// the 'and' mask can be made -1, so the 'and' itself is unnecessary. In that
/// case, just replace the 'and'. Return 'true' if the node is replaced.
bool X86DAGToDAGISel::shrinkAndImmediate(SDNode *And) {
  // i8 is unshrinkable, i16 should be promoted to i32, and vector ops don't
  // have immediate operands.
  MVT VT = And->getSimpleValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  auto *And1C = dyn_cast<ConstantSDNode>(And->getOperand(1));
  if (!And1C)
    return false;

  // Bail out if the mask constant is already negative. It's can't shrink more.
  // If the upper 32 bits of a 64 bit mask are all zeros, we have special isel
  // patterns to use a 32-bit and instead of a 64-bit and by relying on the
  // implicit zeroing of 32 bit ops. So we should check if the lower 32 bits
  // are negative too.
  APInt MaskVal = And1C->getAPIntValue();
  unsigned MaskLZ = MaskVal.countLeadingZeros();
  if (!MaskLZ || (VT == MVT::i64 && MaskLZ == 32))
    return false;

  // Don't extend into the upper 32 bits of a 64 bit mask.
  if (VT == MVT::i64 && MaskLZ >= 32) {
    MaskLZ -= 32;
    MaskVal = MaskVal.trunc(32);
  }

  SDValue And0 = And->getOperand(0);
  APInt HighZeros = APInt::getHighBitsSet(MaskVal.getBitWidth(), MaskLZ);
  APInt NegMaskVal = MaskVal | HighZeros;

  // If a negative constant would not allow a smaller encoding, there's no need
  // to continue. Only change the constant when we know it's a win.
  unsigned MinWidth = NegMaskVal.getMinSignedBits();
  if (MinWidth > 32 || (MinWidth > 8 && MaskVal.getMinSignedBits() <= 32))
    return false;

  // Extend masks if we truncated above.
  if (VT == MVT::i64 && MaskVal.getBitWidth() < 64) {
    NegMaskVal = NegMaskVal.zext(64);
    HighZeros = HighZeros.zext(64);
  }

  // The variable operand must be all zeros in the top bits to allow using the
  // new, negative constant as the mask.
  if (!CurDAG->MaskedValueIsZero(And0, HighZeros))
    return false;

  // Check if the mask is -1. In that case, this is an unnecessary instruction
  // that escaped earlier analysis.
  if (NegMaskVal.isAllOnesValue()) {
    ReplaceNode(And, And0.getNode());
    return true;
  }

  // A negative mask allows a smaller encoding. Create a new 'and' node.
  SDValue NewMask = CurDAG->getConstant(NegMaskVal, SDLoc(And), VT);
  SDValue NewAnd = CurDAG->getNode(ISD::AND, SDLoc(And), VT, And0, NewMask);
  ReplaceNode(And, NewAnd.getNode());
  SelectCode(NewAnd.getNode());
  return true;
}

void X86DAGToDAGISel::Select(SDNode *Node) {
  MVT NVT = Node->getSimpleValueType(0);
  unsigned Opcode = Node->getOpcode();
  SDLoc dl(Node);

  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(dbgs() << "== "; Node->dump(CurDAG); dbgs() << '\n');
    Node->setNodeId(-1);
    return;   // Already selected.
  }

  switch (Opcode) {
  default: break;
  case ISD::BRIND: {
    if (Subtarget->isTargetNaCl())
      // NaCl has its own pass where jmp %r32 are converted to jmp %r64. We
      // leave the instruction alone.
      break;
    if (Subtarget->isTarget64BitILP32()) {
      // Converts a 32-bit register to a 64-bit, zero-extended version of
      // it. This is needed because x86-64 can do many things, but jmp %r32
      // ain't one of them.
      const SDValue &Target = Node->getOperand(1);
      assert(Target.getSimpleValueType() == llvm::MVT::i32);
      SDValue ZextTarget = CurDAG->getZExtOrTrunc(Target, dl, EVT(MVT::i64));
      SDValue Brind = CurDAG->getNode(ISD::BRIND, dl, MVT::Other,
                                      Node->getOperand(0), ZextTarget);
      ReplaceNode(Node, Brind.getNode());
      SelectCode(ZextTarget.getNode());
      SelectCode(Brind.getNode());
      return;
    }
    break;
  }
  case X86ISD::GlobalBaseReg:
    ReplaceNode(Node, getGlobalBaseReg());
    return;

  case ISD::BITCAST:
    // Just drop all 128/256/512-bit bitcasts.
    if (NVT.is512BitVector() || NVT.is256BitVector() || NVT.is128BitVector() ||
        NVT == MVT::f128) {
      ReplaceUses(SDValue(Node, 0), Node->getOperand(0));
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    break;

  case X86ISD::BLENDV: {
    // BLENDV selects like a regular VSELECT.
    SDValue VSelect = CurDAG->getNode(
        ISD::VSELECT, SDLoc(Node), Node->getValueType(0), Node->getOperand(0),
        Node->getOperand(1), Node->getOperand(2));
    ReplaceNode(Node, VSelect.getNode());
    SelectCode(VSelect.getNode());
    // We already called ReplaceUses.
    return;
  }

  case ISD::SRL:
    if (matchBitExtract(Node))
      return;
    LLVM_FALLTHROUGH;
  case ISD::SRA:
  case ISD::SHL:
    if (tryShiftAmountMod(Node))
      return;
    break;

  case ISD::AND:
    if (MachineSDNode *NewNode = matchBEXTRFromAndImm(Node)) {
      ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 0));
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    if (matchBitExtract(Node))
      return;
    if (AndImmShrink && shrinkAndImmediate(Node))
      return;

    LLVM_FALLTHROUGH;
  case ISD::OR:
  case ISD::XOR: {

    // For operations of the form (x << C1) op C2, check if we can use a smaller
    // encoding for C2 by transforming it into (x op (C2>>C1)) << C1.
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    if (N0->getOpcode() != ISD::SHL || !N0->hasOneUse())
      break;

    // i8 is unshrinkable, i16 should be promoted to i32.
    if (NVT != MVT::i32 && NVT != MVT::i64)
      break;

    ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(N1);
    ConstantSDNode *ShlCst = dyn_cast<ConstantSDNode>(N0->getOperand(1));
    if (!Cst || !ShlCst)
      break;

    int64_t Val = Cst->getSExtValue();
    uint64_t ShlVal = ShlCst->getZExtValue();

    // Make sure that we don't change the operation by removing bits.
    // This only matters for OR and XOR, AND is unaffected.
    uint64_t RemovedBitsMask = (1ULL << ShlVal) - 1;
    if (Opcode != ISD::AND && (Val & RemovedBitsMask) != 0)
      break;

    unsigned ShlOp, AddOp, Op;
    MVT CstVT = NVT;

    // Check the minimum bitwidth for the new constant.
    // TODO: AND32ri is the same as AND64ri32 with zext imm.
    // TODO: MOV32ri+OR64r is cheaper than MOV64ri64+OR64rr
    // TODO: Using 16 and 8 bit operations is also possible for or32 & xor32.
    if (!isInt<8>(Val) && isInt<8>(Val >> ShlVal))
      CstVT = MVT::i8;
    else if (!isInt<32>(Val) && isInt<32>(Val >> ShlVal))
      CstVT = MVT::i32;

    // Bail if there is no smaller encoding.
    if (NVT == CstVT)
      break;

    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i32:
      assert(CstVT == MVT::i8);
      ShlOp = X86::SHL32ri;
      AddOp = X86::ADD32rr;

      switch (Opcode) {
      default: llvm_unreachable("Impossible opcode");
      case ISD::AND: Op = X86::AND32ri8; break;
      case ISD::OR:  Op =  X86::OR32ri8; break;
      case ISD::XOR: Op = X86::XOR32ri8; break;
      }
      break;
    case MVT::i64:
      assert(CstVT == MVT::i8 || CstVT == MVT::i32);
      ShlOp = X86::SHL64ri;
      AddOp = X86::ADD64rr;

      switch (Opcode) {
      default: llvm_unreachable("Impossible opcode");
      case ISD::AND: Op = CstVT==MVT::i8? X86::AND64ri8 : X86::AND64ri32; break;
      case ISD::OR:  Op = CstVT==MVT::i8?  X86::OR64ri8 :  X86::OR64ri32; break;
      case ISD::XOR: Op = CstVT==MVT::i8? X86::XOR64ri8 : X86::XOR64ri32; break;
      }
      break;
    }

    // Emit the smaller op and the shift.
    SDValue NewCst = CurDAG->getTargetConstant(Val >> ShlVal, dl, CstVT);
    SDNode *New = CurDAG->getMachineNode(Op, dl, NVT, N0->getOperand(0),NewCst);
    if (ShlVal == 1)
      CurDAG->SelectNodeTo(Node, AddOp, NVT, SDValue(New, 0),
                           SDValue(New, 0));
    else
      CurDAG->SelectNodeTo(Node, ShlOp, NVT, SDValue(New, 0),
                           getI8Imm(ShlVal, dl));
    return;
  }
  case X86ISD::SMUL:
    // i16/i32/i64 are handled with isel patterns.
    if (NVT != MVT::i8)
      break;
    LLVM_FALLTHROUGH;
  case X86ISD::UMUL: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned LoReg, ROpc, MOpc;
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i8:
      LoReg = X86::AL;
      ROpc = Opcode == X86ISD::SMUL ? X86::IMUL8r : X86::MUL8r;
      MOpc = Opcode == X86ISD::SMUL ? X86::IMUL8m : X86::MUL8m;
      break;
    case MVT::i16:
      LoReg = X86::AX;
      ROpc = X86::MUL16r;
      MOpc = X86::MUL16m;
      break;
    case MVT::i32:
      LoReg = X86::EAX;
      ROpc = X86::MUL32r;
      MOpc = X86::MUL32m;
      break;
    case MVT::i64:
      LoReg = X86::RAX;
      ROpc = X86::MUL64r;
      MOpc = X86::MUL64m;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool FoldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    // Multiply is commmutative.
    if (!FoldedLoad) {
      FoldedLoad = tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
      if (FoldedLoad)
        std::swap(N0, N1);
    }

    SDValue InFlag = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, LoReg,
                                          N0, SDValue()).getValue(1);

    MachineSDNode *CNode;
    if (FoldedLoad) {
      // i16/i32/i64 use an instruction that produces a low and high result even
      // though only the low result is used.
      SDVTList VTs;
      if (NVT == MVT::i8)
        VTs = CurDAG->getVTList(NVT, MVT::i32, MVT::Other);
      else
        VTs = CurDAG->getVTList(NVT, NVT, MVT::i32, MVT::Other);

      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InFlag };
      CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);

      // Update the chain.
      ReplaceUses(N1.getValue(1), SDValue(CNode, NVT == MVT::i8 ? 2 : 3));
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      // i16/i32/i64 use an instruction that produces a low and high result even
      // though only the low result is used.
      SDVTList VTs;
      if (NVT == MVT::i8)
        VTs = CurDAG->getVTList(NVT, MVT::i32);
      else
        VTs = CurDAG->getVTList(NVT, NVT, MVT::i32);

      CNode = CurDAG->getMachineNode(ROpc, dl, VTs, {N1, InFlag});
    }

    ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    ReplaceUses(SDValue(Node, 1), SDValue(CNode, NVT == MVT::i8 ? 1 : 2));
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned Opc, MOpc;
    bool isSigned = Opcode == ISD::SMUL_LOHI;
    if (!isSigned) {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i32: Opc = X86::MUL32r; MOpc = X86::MUL32m; break;
      case MVT::i64: Opc = X86::MUL64r; MOpc = X86::MUL64m; break;
      }
    } else {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i32: Opc = X86::IMUL32r; MOpc = X86::IMUL32m; break;
      case MVT::i64: Opc = X86::IMUL64r; MOpc = X86::IMUL64m; break;
      }
    }

    unsigned SrcReg, LoReg, HiReg;
    switch (Opc) {
    default: llvm_unreachable("Unknown MUL opcode!");
    case X86::IMUL32r:
    case X86::MUL32r:
      SrcReg = LoReg = X86::EAX; HiReg = X86::EDX;
      break;
    case X86::IMUL64r:
    case X86::MUL64r:
      SrcReg = LoReg = X86::RAX; HiReg = X86::RDX;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool foldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    // Multiply is commmutative.
    if (!foldedLoad) {
      foldedLoad = tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
      if (foldedLoad)
        std::swap(N0, N1);
    }

    SDValue InFlag = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, SrcReg,
                                          N0, SDValue()).getValue(1);
    if (foldedLoad) {
      SDValue Chain;
      MachineSDNode *CNode = nullptr;
      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InFlag };
      SDVTList VTs = CurDAG->getVTList(MVT::Other, MVT::Glue);
      CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
      Chain = SDValue(CNode, 0);
      InFlag = SDValue(CNode, 1);

      // Update the chain.
      ReplaceUses(N1.getValue(1), Chain);
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      SDValue Ops[] = { N1, InFlag };
      SDVTList VTs = CurDAG->getVTList(MVT::Glue);
      SDNode *CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
      InFlag = SDValue(CNode, 0);
    }

    // Copy the low half of the result, if it is needed.
    if (!SDValue(Node, 0).use_empty()) {
      assert(LoReg && "Register for low half is not defined!");
      SDValue ResLo = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl, LoReg,
                                             NVT, InFlag);
      InFlag = ResLo.getValue(2);
      ReplaceUses(SDValue(Node, 0), ResLo);
      LLVM_DEBUG(dbgs() << "=> "; ResLo.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the high half of the result, if it is needed.
    if (!SDValue(Node, 1).use_empty()) {
      assert(HiReg && "Register for high half is not defined!");
      SDValue ResHi = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl, HiReg,
                                             NVT, InFlag);
      InFlag = ResHi.getValue(2);
      ReplaceUses(SDValue(Node, 1), ResHi);
      LLVM_DEBUG(dbgs() << "=> "; ResHi.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }

    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::SDIVREM:
  case ISD::UDIVREM: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned Opc, MOpc;
    bool isSigned = Opcode == ISD::SDIVREM;
    if (!isSigned) {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i8:  Opc = X86::DIV8r;  MOpc = X86::DIV8m;  break;
      case MVT::i16: Opc = X86::DIV16r; MOpc = X86::DIV16m; break;
      case MVT::i32: Opc = X86::DIV32r; MOpc = X86::DIV32m; break;
      case MVT::i64: Opc = X86::DIV64r; MOpc = X86::DIV64m; break;
      }
    } else {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i8:  Opc = X86::IDIV8r;  MOpc = X86::IDIV8m;  break;
      case MVT::i16: Opc = X86::IDIV16r; MOpc = X86::IDIV16m; break;
      case MVT::i32: Opc = X86::IDIV32r; MOpc = X86::IDIV32m; break;
      case MVT::i64: Opc = X86::IDIV64r; MOpc = X86::IDIV64m; break;
      }
    }

    unsigned LoReg, HiReg, ClrReg;
    unsigned SExtOpcode;
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i8:
      LoReg = X86::AL;  ClrReg = HiReg = X86::AH;
      SExtOpcode = X86::CBW;
      break;
    case MVT::i16:
      LoReg = X86::AX;  HiReg = X86::DX;
      ClrReg = X86::DX;
      SExtOpcode = X86::CWD;
      break;
    case MVT::i32:
      LoReg = X86::EAX; ClrReg = HiReg = X86::EDX;
      SExtOpcode = X86::CDQ;
      break;
    case MVT::i64:
      LoReg = X86::RAX; ClrReg = HiReg = X86::RDX;
      SExtOpcode = X86::CQO;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool foldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    bool signBitIsZero = CurDAG->SignBitIsZero(N0);

    SDValue InFlag;
    if (NVT == MVT::i8 && (!isSigned || signBitIsZero)) {
      // Special case for div8, just use a move with zero extension to AX to
      // clear the upper 8 bits (AH).
      SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Chain;
      MachineSDNode *Move;
      if (tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
        SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N0.getOperand(0) };
        Move = CurDAG->getMachineNode(X86::MOVZX32rm8, dl, MVT::i32,
                                      MVT::Other, Ops);
        Chain = SDValue(Move, 1);
        ReplaceUses(N0.getValue(1), Chain);
        // Record the mem-refs
        CurDAG->setNodeMemRefs(Move, {cast<LoadSDNode>(N0)->getMemOperand()});
      } else {
        Move = CurDAG->getMachineNode(X86::MOVZX32rr8, dl, MVT::i32, N0);
        Chain = CurDAG->getEntryNode();
      }
      Chain  = CurDAG->getCopyToReg(Chain, dl, X86::EAX, SDValue(Move, 0),
                                    SDValue());
      InFlag = Chain.getValue(1);
    } else {
      InFlag =
        CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl,
                             LoReg, N0, SDValue()).getValue(1);
      if (isSigned && !signBitIsZero) {
        // Sign extend the low part into the high part.
        InFlag =
          SDValue(CurDAG->getMachineNode(SExtOpcode, dl, MVT::Glue, InFlag),0);
      } else {
        // Zero out the high part, effectively zero extending the input.
        SDValue ClrNode = SDValue(CurDAG->getMachineNode(X86::MOV32r0, dl, NVT), 0);
        switch (NVT.SimpleTy) {
        case MVT::i16:
          ClrNode =
              SDValue(CurDAG->getMachineNode(
                          TargetOpcode::EXTRACT_SUBREG, dl, MVT::i16, ClrNode,
                          CurDAG->getTargetConstant(X86::sub_16bit, dl,
                                                    MVT::i32)),
                      0);
          break;
        case MVT::i32:
          break;
        case MVT::i64:
          ClrNode =
              SDValue(CurDAG->getMachineNode(
                          TargetOpcode::SUBREG_TO_REG, dl, MVT::i64,
                          CurDAG->getTargetConstant(0, dl, MVT::i64), ClrNode,
                          CurDAG->getTargetConstant(X86::sub_32bit, dl,
                                                    MVT::i32)),
                      0);
          break;
        default:
          llvm_unreachable("Unexpected division source");
        }

        InFlag = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, ClrReg,
                                      ClrNode, InFlag).getValue(1);
      }
    }

    if (foldedLoad) {
      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InFlag };
      MachineSDNode *CNode =
        CurDAG->getMachineNode(MOpc, dl, MVT::Other, MVT::Glue, Ops);
      InFlag = SDValue(CNode, 1);
      // Update the chain.
      ReplaceUses(N1.getValue(1), SDValue(CNode, 0));
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      InFlag =
        SDValue(CurDAG->getMachineNode(Opc, dl, MVT::Glue, N1, InFlag), 0);
    }

    // Prevent use of AH in a REX instruction by explicitly copying it to
    // an ABCD_L register.
    //
    // The current assumption of the register allocator is that isel
    // won't generate explicit references to the GR8_ABCD_H registers. If
    // the allocator and/or the backend get enhanced to be more robust in
    // that regard, this can be, and should be, removed.
    if (HiReg == X86::AH && !SDValue(Node, 1).use_empty()) {
      SDValue AHCopy = CurDAG->getRegister(X86::AH, MVT::i8);
      unsigned AHExtOpcode =
          isSigned ? X86::MOVSX32rr8_NOREX : X86::MOVZX32rr8_NOREX;

      SDNode *RNode = CurDAG->getMachineNode(AHExtOpcode, dl, MVT::i32,
                                             MVT::Glue, AHCopy, InFlag);
      SDValue Result(RNode, 0);
      InFlag = SDValue(RNode, 1);

      Result =
          CurDAG->getTargetExtractSubreg(X86::sub_8bit, dl, MVT::i8, Result);

      ReplaceUses(SDValue(Node, 1), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the division (low) result, if it is needed.
    if (!SDValue(Node, 0).use_empty()) {
      SDValue Result = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl,
                                                LoReg, NVT, InFlag);
      InFlag = Result.getValue(2);
      ReplaceUses(SDValue(Node, 0), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the remainder (high) result, if it is needed.
    if (!SDValue(Node, 1).use_empty()) {
      SDValue Result = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl,
                                              HiReg, NVT, InFlag);
      InFlag = Result.getValue(2);
      ReplaceUses(SDValue(Node, 1), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case X86ISD::CMP: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    // Optimizations for TEST compares.
    if (!isNullConstant(N1))
      break;

    // Save the original VT of the compare.
    MVT CmpVT = N0.getSimpleValueType();

    // If we are comparing (and (shr X, C, Mask) with 0, emit a BEXTR followed
    // by a test instruction. The test should be removed later by
    // analyzeCompare if we are using only the zero flag.
    // TODO: Should we check the users and use the BEXTR flags directly?
    if (N0.getOpcode() == ISD::AND && N0.hasOneUse()) {
      if (MachineSDNode *NewNode = matchBEXTRFromAndImm(N0.getNode())) {
        unsigned TestOpc = CmpVT == MVT::i64 ? X86::TEST64rr
                                             : X86::TEST32rr;
        SDValue BEXTR = SDValue(NewNode, 0);
        NewNode = CurDAG->getMachineNode(TestOpc, dl, MVT::i32, BEXTR, BEXTR);
        ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 0));
        CurDAG->RemoveDeadNode(Node);
        return;
      }
    }

    // We can peek through truncates, but we need to be careful below.
    if (N0.getOpcode() == ISD::TRUNCATE && N0.hasOneUse())
      N0 = N0.getOperand(0);

    // Look for (X86cmp (and $op, $imm), 0) and see if we can convert it to
    // use a smaller encoding.
    // Look past the truncate if CMP is the only use of it.
    if (N0.getOpcode() == ISD::AND &&
        N0.getNode()->hasOneUse() &&
        N0.getValueType() != MVT::i8) {
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
      if (!C) break;
      uint64_t Mask = C->getZExtValue();

      // Check if we can replace AND+IMM64 with a shift. This is possible for
      // masks/ like 0xFF000000 or 0x00FFFFFF and if we care only about the zero
      // flag.
      if (CmpVT == MVT::i64 && !isInt<32>(Mask) &&
          onlyUsesZeroFlag(SDValue(Node, 0))) {
        if (isMask_64(~Mask)) {
          unsigned TrailingZeros = countTrailingZeros(Mask);
          SDValue Imm = CurDAG->getTargetConstant(TrailingZeros, dl, MVT::i64);
          SDValue Shift =
            SDValue(CurDAG->getMachineNode(X86::SHR64ri, dl, MVT::i64,
                                           N0.getOperand(0), Imm), 0);
          MachineSDNode *Test = CurDAG->getMachineNode(X86::TEST64rr, dl,
                                                       MVT::i32, Shift, Shift);
          ReplaceNode(Node, Test);
          return;
        }
        if (isMask_64(Mask)) {
          unsigned LeadingZeros = countLeadingZeros(Mask);
          SDValue Imm = CurDAG->getTargetConstant(LeadingZeros, dl, MVT::i64);
          SDValue Shift =
            SDValue(CurDAG->getMachineNode(X86::SHL64ri, dl, MVT::i64,
                                           N0.getOperand(0), Imm), 0);
          MachineSDNode *Test = CurDAG->getMachineNode(X86::TEST64rr, dl,
                                                       MVT::i32, Shift, Shift);
          ReplaceNode(Node, Test);
          return;
        }
      }

      MVT VT;
      int SubRegOp;
      unsigned ROpc, MOpc;

      // For each of these checks we need to be careful if the sign flag is
      // being used. It is only safe to use the sign flag in two conditions,
      // either the sign bit in the shrunken mask is zero or the final test
      // size is equal to the original compare size.

      if (isUInt<8>(Mask) &&
          (!(Mask & 0x80) || CmpVT == MVT::i8 ||
           hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, convert "testl %eax, $8" to "testb %al, $8"
        VT = MVT::i8;
        SubRegOp = X86::sub_8bit;
        ROpc = X86::TEST8ri;
        MOpc = X86::TEST8mi;
      } else if (OptForMinSize && isUInt<16>(Mask) &&
                 (!(Mask & 0x8000) || CmpVT == MVT::i16 ||
                  hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, "testl %eax, $32776" to "testw %ax, $32776".
        // NOTE: We only want to form TESTW instructions if optimizing for
        // min size. Otherwise we only save one byte and possibly get a length
        // changing prefix penalty in the decoders.
        VT = MVT::i16;
        SubRegOp = X86::sub_16bit;
        ROpc = X86::TEST16ri;
        MOpc = X86::TEST16mi;
      } else if (isUInt<32>(Mask) && N0.getValueType() != MVT::i16 &&
                 ((!(Mask & 0x80000000) &&
                   // Without minsize 16-bit Cmps can get here so we need to
                   // be sure we calculate the correct sign flag if needed.
                   (CmpVT != MVT::i16 || !(Mask & 0x8000))) ||
                  CmpVT == MVT::i32 ||
                  hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, "testq %rax, $268468232" to "testl %eax, $268468232".
        // NOTE: We only want to run that transform if N0 is 32 or 64 bits.
        // Otherwize, we find ourselves in a position where we have to do
        // promotion. If previous passes did not promote the and, we assume
        // they had a good reason not to and do not promote here.
        VT = MVT::i32;
        SubRegOp = X86::sub_32bit;
        ROpc = X86::TEST32ri;
        MOpc = X86::TEST32mi;
      } else {
        // No eligible transformation was found.
        break;
      }

      // FIXME: We should be able to fold loads here.

      SDValue Imm = CurDAG->getTargetConstant(Mask, dl, VT);
      SDValue Reg = N0.getOperand(0);

      // Emit a testl or testw.
      MachineSDNode *NewNode;
      SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
      if (tryFoldLoad(Node, N0.getNode(), Reg, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
        SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                          Reg.getOperand(0) };
        NewNode = CurDAG->getMachineNode(MOpc, dl, MVT::i32, MVT::Other, Ops);
        // Update the chain.
        ReplaceUses(Reg.getValue(1), SDValue(NewNode, 1));
        // Record the mem-refs
        CurDAG->setNodeMemRefs(NewNode,
                               {cast<LoadSDNode>(Reg)->getMemOperand()});
      } else {
        // Extract the subregister if necessary.
        if (N0.getValueType() != VT)
          Reg = CurDAG->getTargetExtractSubreg(SubRegOp, dl, VT, Reg);

        NewNode = CurDAG->getMachineNode(ROpc, dl, MVT::i32, Reg, Imm);
      }
      // Replace CMP with TEST.
      ReplaceNode(Node, NewNode);
      return;
    }
    break;
  }
  case X86ISD::PCMPISTR: {
    if (!Subtarget->hasSSE42())
      break;

    bool NeedIndex = !SDValue(Node, 0).use_empty();
    bool NeedMask = !SDValue(Node, 1).use_empty();
    // We can't fold a load if we are going to make two instructions.
    bool MayFoldLoad = !NeedIndex || !NeedMask;

    MachineSDNode *CNode;
    if (NeedMask) {
      unsigned ROpc = Subtarget->hasAVX() ? X86::VPCMPISTRMrr : X86::PCMPISTRMrr;
      unsigned MOpc = Subtarget->hasAVX() ? X86::VPCMPISTRMrm : X86::PCMPISTRMrm;
      CNode = emitPCMPISTR(ROpc, MOpc, MayFoldLoad, dl, MVT::v16i8, Node);
      ReplaceUses(SDValue(Node, 1), SDValue(CNode, 0));
    }
    if (NeedIndex || !NeedMask) {
      unsigned ROpc = Subtarget->hasAVX() ? X86::VPCMPISTRIrr : X86::PCMPISTRIrr;
      unsigned MOpc = Subtarget->hasAVX() ? X86::VPCMPISTRIrm : X86::PCMPISTRIrm;
      CNode = emitPCMPISTR(ROpc, MOpc, MayFoldLoad, dl, MVT::i32, Node);
      ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    }

    // Connect the flag usage to the last instruction created.
    ReplaceUses(SDValue(Node, 2), SDValue(CNode, 1));
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case X86ISD::PCMPESTR: {
    if (!Subtarget->hasSSE42())
      break;

    // Copy the two implicit register inputs.
    SDValue InFlag = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EAX,
                                          Node->getOperand(1),
                                          SDValue()).getValue(1);
    InFlag = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EDX,
                                  Node->getOperand(3), InFlag).getValue(1);

    bool NeedIndex = !SDValue(Node, 0).use_empty();
    bool NeedMask = !SDValue(Node, 1).use_empty();
    // We can't fold a load if we are going to make two instructions.
    bool MayFoldLoad = !NeedIndex || !NeedMask;

    MachineSDNode *CNode;
    if (NeedMask) {
      unsigned ROpc = Subtarget->hasAVX() ? X86::VPCMPESTRMrr : X86::PCMPESTRMrr;
      unsigned MOpc = Subtarget->hasAVX() ? X86::VPCMPESTRMrm : X86::PCMPESTRMrm;
      CNode = emitPCMPESTR(ROpc, MOpc, MayFoldLoad, dl, MVT::v16i8, Node,
                           InFlag);
      ReplaceUses(SDValue(Node, 1), SDValue(CNode, 0));
    }
    if (NeedIndex || !NeedMask) {
      unsigned ROpc = Subtarget->hasAVX() ? X86::VPCMPESTRIrr : X86::PCMPESTRIrr;
      unsigned MOpc = Subtarget->hasAVX() ? X86::VPCMPESTRIrm : X86::PCMPESTRIrm;
      CNode = emitPCMPESTR(ROpc, MOpc, MayFoldLoad, dl, MVT::i32, Node, InFlag);
      ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    }
    // Connect the flag usage to the last instruction created.
    ReplaceUses(SDValue(Node, 2), SDValue(CNode, 1));
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::STORE:
    if (foldLoadStoreIntoMemOperand(Node))
      return;
    break;
  }

  SelectCode(Node);
}

bool X86DAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                             std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1, Op2, Op3, Op4;
  switch (ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::Constraint_i:
    // FIXME: It seems strange that 'i' is needed here since it's supposed to
    //        be an immediate and not a memory constraint.
    LLVM_FALLTHROUGH;
  case InlineAsm::Constraint_o: // offsetable        ??
  case InlineAsm::Constraint_v: // not offsetable    ??
  case InlineAsm::Constraint_m: // memory
  case InlineAsm::Constraint_X:
    if (!selectAddr(nullptr, Op, Op0, Op1, Op2, Op3, Op4))
      return true;
    break;
  }

  OutOps.push_back(Op0);
  OutOps.push_back(Op1);
  OutOps.push_back(Op2);
  OutOps.push_back(Op3);
  OutOps.push_back(Op4);
  return false;
}

/// This pass converts a legalized DAG into a X86-specific DAG,
/// ready for instruction scheduling.
FunctionPass *llvm::createX86ISelDag(X86TargetMachine &TM,
                                     CodeGenOpt::Level OptLevel) {
  return new X86DAGToDAGISel(TM, OptLevel);
}
