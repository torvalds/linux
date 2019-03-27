//===- InstVisitor.h - Instruction visitor templates ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_IR_INSTVISITOR_H
#define LLVM_IR_INSTVISITOR_H

#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

// We operate on opaque instruction classes, so forward declare all instruction
// types now...
//
#define HANDLE_INST(NUM, OPCODE, CLASS)   class CLASS;
#include "llvm/IR/Instruction.def"

#define DELEGATE(CLASS_TO_VISIT) \
  return static_cast<SubClass*>(this)-> \
               visit##CLASS_TO_VISIT(static_cast<CLASS_TO_VISIT&>(I))


/// Base class for instruction visitors
///
/// Instruction visitors are used when you want to perform different actions
/// for different kinds of instructions without having to use lots of casts
/// and a big switch statement (in your code, that is).
///
/// To define your own visitor, inherit from this class, specifying your
/// new type for the 'SubClass' template parameter, and "override" visitXXX
/// functions in your class. I say "override" because this class is defined
/// in terms of statically resolved overloading, not virtual functions.
///
/// For example, here is a visitor that counts the number of malloc
/// instructions processed:
///
///  /// Declare the class.  Note that we derive from InstVisitor instantiated
///  /// with _our new subclasses_ type.
///  ///
///  struct CountAllocaVisitor : public InstVisitor<CountAllocaVisitor> {
///    unsigned Count;
///    CountAllocaVisitor() : Count(0) {}
///
///    void visitAllocaInst(AllocaInst &AI) { ++Count; }
///  };
///
///  And this class would be used like this:
///    CountAllocaVisitor CAV;
///    CAV.visit(function);
///    NumAllocas = CAV.Count;
///
/// The defined has 'visit' methods for Instruction, and also for BasicBlock,
/// Function, and Module, which recursively process all contained instructions.
///
/// Note that if you don't implement visitXXX for some instruction type,
/// the visitXXX method for instruction superclass will be invoked. So
/// if instructions are added in the future, they will be automatically
/// supported, if you handle one of their superclasses.
///
/// The optional second template argument specifies the type that instruction
/// visitation functions should return. If you specify this, you *MUST* provide
/// an implementation of visitInstruction though!.
///
/// Note that this class is specifically designed as a template to avoid
/// virtual function call overhead.  Defining and using an InstVisitor is just
/// as efficient as having your own switch statement over the instruction
/// opcode.
template<typename SubClass, typename RetTy=void>
class InstVisitor {
  //===--------------------------------------------------------------------===//
  // Interface code - This is the public interface of the InstVisitor that you
  // use to visit instructions...
  //

public:
  // Generic visit method - Allow visitation to all instructions in a range
  template<class Iterator>
  void visit(Iterator Start, Iterator End) {
    while (Start != End)
      static_cast<SubClass*>(this)->visit(*Start++);
  }

  // Define visitors for functions and basic blocks...
  //
  void visit(Module &M) {
    static_cast<SubClass*>(this)->visitModule(M);
    visit(M.begin(), M.end());
  }
  void visit(Function &F) {
    static_cast<SubClass*>(this)->visitFunction(F);
    visit(F.begin(), F.end());
  }
  void visit(BasicBlock &BB) {
    static_cast<SubClass*>(this)->visitBasicBlock(BB);
    visit(BB.begin(), BB.end());
  }

  // Forwarding functions so that the user can visit with pointers AND refs.
  void visit(Module       *M)  { visit(*M); }
  void visit(Function     *F)  { visit(*F); }
  void visit(BasicBlock   *BB) { visit(*BB); }
  RetTy visit(Instruction *I)  { return visit(*I); }

  // visit - Finally, code to visit an instruction...
  //
  RetTy visit(Instruction &I) {
    static_assert(std::is_base_of<InstVisitor, SubClass>::value,
                  "Must pass the derived type to this template!");

    switch (I.getOpcode()) {
    default: llvm_unreachable("Unknown instruction type encountered!");
      // Build the switch statement using the Instruction.def file...
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case Instruction::OPCODE: return \
           static_cast<SubClass*>(this)-> \
                      visit##OPCODE(static_cast<CLASS&>(I));
#include "llvm/IR/Instruction.def"
    }
  }

  //===--------------------------------------------------------------------===//
  // Visitation functions... these functions provide default fallbacks in case
  // the user does not specify what to do for a particular instruction type.
  // The default behavior is to generalize the instruction type to its subtype
  // and try visiting the subtype.  All of this should be inlined perfectly,
  // because there are no virtual functions to get in the way.
  //

  // When visiting a module, function or basic block directly, these methods get
  // called to indicate when transitioning into a new unit.
  //
  void visitModule    (Module &M) {}
  void visitFunction  (Function &F) {}
  void visitBasicBlock(BasicBlock &BB) {}

  // Define instruction specific visitor functions that can be overridden to
  // handle SPECIFIC instructions.  These functions automatically define
  // visitMul to proxy to visitBinaryOperator for instance in case the user does
  // not need this generality.
  //
  // These functions can also implement fan-out, when a single opcode and
  // instruction have multiple more specific Instruction subclasses. The Call
  // instruction currently supports this. We implement that by redirecting that
  // instruction to a special delegation helper.
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    RetTy visit##OPCODE(CLASS &I) { \
      if (NUM == Instruction::Call) \
        return delegateCallInst(I); \
      else \
        DELEGATE(CLASS); \
    }
#include "llvm/IR/Instruction.def"

  // Specific Instruction type classes... note that all of the casts are
  // necessary because we use the instruction classes as opaque types...
  //
  RetTy visitICmpInst(ICmpInst &I)                { DELEGATE(CmpInst);}
  RetTy visitFCmpInst(FCmpInst &I)                { DELEGATE(CmpInst);}
  RetTy visitAllocaInst(AllocaInst &I)            { DELEGATE(UnaryInstruction);}
  RetTy visitLoadInst(LoadInst     &I)            { DELEGATE(UnaryInstruction);}
  RetTy visitStoreInst(StoreInst   &I)            { DELEGATE(Instruction);}
  RetTy visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) { DELEGATE(Instruction);}
  RetTy visitAtomicRMWInst(AtomicRMWInst &I)      { DELEGATE(Instruction);}
  RetTy visitFenceInst(FenceInst   &I)            { DELEGATE(Instruction);}
  RetTy visitGetElementPtrInst(GetElementPtrInst &I){ DELEGATE(Instruction);}
  RetTy visitPHINode(PHINode       &I)            { DELEGATE(Instruction);}
  RetTy visitTruncInst(TruncInst &I)              { DELEGATE(CastInst);}
  RetTy visitZExtInst(ZExtInst &I)                { DELEGATE(CastInst);}
  RetTy visitSExtInst(SExtInst &I)                { DELEGATE(CastInst);}
  RetTy visitFPTruncInst(FPTruncInst &I)          { DELEGATE(CastInst);}
  RetTy visitFPExtInst(FPExtInst &I)              { DELEGATE(CastInst);}
  RetTy visitFPToUIInst(FPToUIInst &I)            { DELEGATE(CastInst);}
  RetTy visitFPToSIInst(FPToSIInst &I)            { DELEGATE(CastInst);}
  RetTy visitUIToFPInst(UIToFPInst &I)            { DELEGATE(CastInst);}
  RetTy visitSIToFPInst(SIToFPInst &I)            { DELEGATE(CastInst);}
  RetTy visitPtrToIntInst(PtrToIntInst &I)        { DELEGATE(CastInst);}
  RetTy visitIntToPtrInst(IntToPtrInst &I)        { DELEGATE(CastInst);}
  RetTy visitBitCastInst(BitCastInst &I)          { DELEGATE(CastInst);}
  RetTy visitAddrSpaceCastInst(AddrSpaceCastInst &I) { DELEGATE(CastInst);}
  RetTy visitSelectInst(SelectInst &I)            { DELEGATE(Instruction);}
  RetTy visitVAArgInst(VAArgInst   &I)            { DELEGATE(UnaryInstruction);}
  RetTy visitExtractElementInst(ExtractElementInst &I) { DELEGATE(Instruction);}
  RetTy visitInsertElementInst(InsertElementInst &I) { DELEGATE(Instruction);}
  RetTy visitShuffleVectorInst(ShuffleVectorInst &I) { DELEGATE(Instruction);}
  RetTy visitExtractValueInst(ExtractValueInst &I){ DELEGATE(UnaryInstruction);}
  RetTy visitInsertValueInst(InsertValueInst &I)  { DELEGATE(Instruction); }
  RetTy visitLandingPadInst(LandingPadInst &I)    { DELEGATE(Instruction); }
  RetTy visitFuncletPadInst(FuncletPadInst &I) { DELEGATE(Instruction); }
  RetTy visitCleanupPadInst(CleanupPadInst &I) { DELEGATE(FuncletPadInst); }
  RetTy visitCatchPadInst(CatchPadInst &I)     { DELEGATE(FuncletPadInst); }

  // Handle the special instrinsic instruction classes.
  RetTy visitDbgDeclareInst(DbgDeclareInst &I)    { DELEGATE(DbgVariableIntrinsic);}
  RetTy visitDbgValueInst(DbgValueInst &I)        { DELEGATE(DbgVariableIntrinsic);}
  RetTy visitDbgVariableIntrinsic(DbgVariableIntrinsic &I)
                                                  { DELEGATE(DbgInfoIntrinsic);}
  RetTy visitDbgLabelInst(DbgLabelInst &I)        { DELEGATE(DbgInfoIntrinsic);}
  RetTy visitDbgInfoIntrinsic(DbgInfoIntrinsic &I){ DELEGATE(IntrinsicInst); }
  RetTy visitMemSetInst(MemSetInst &I)            { DELEGATE(MemIntrinsic); }
  RetTy visitMemCpyInst(MemCpyInst &I)            { DELEGATE(MemTransferInst); }
  RetTy visitMemMoveInst(MemMoveInst &I)          { DELEGATE(MemTransferInst); }
  RetTy visitMemTransferInst(MemTransferInst &I)  { DELEGATE(MemIntrinsic); }
  RetTy visitMemIntrinsic(MemIntrinsic &I)        { DELEGATE(IntrinsicInst); }
  RetTy visitVAStartInst(VAStartInst &I)          { DELEGATE(IntrinsicInst); }
  RetTy visitVAEndInst(VAEndInst &I)              { DELEGATE(IntrinsicInst); }
  RetTy visitVACopyInst(VACopyInst &I)            { DELEGATE(IntrinsicInst); }
  RetTy visitIntrinsicInst(IntrinsicInst &I)      { DELEGATE(CallInst); }

  // Call and Invoke are slightly different as they delegate first through
  // a generic CallSite visitor.
  RetTy visitCallInst(CallInst &I) {
    return static_cast<SubClass*>(this)->visitCallSite(&I);
  }
  RetTy visitInvokeInst(InvokeInst &I) {
    return static_cast<SubClass*>(this)->visitCallSite(&I);
  }

  // While terminators don't have a distinct type modeling them, we support
  // intercepting them with dedicated a visitor callback.
  RetTy visitReturnInst(ReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitBranchInst(BranchInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitSwitchInst(SwitchInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitIndirectBrInst(IndirectBrInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitResumeInst(ResumeInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitUnreachableInst(UnreachableInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitCleanupReturnInst(CleanupReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitCatchReturnInst(CatchReturnInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitCatchSwitchInst(CatchSwitchInst &I) {
    return static_cast<SubClass *>(this)->visitTerminator(I);
  }
  RetTy visitTerminator(Instruction &I)    { DELEGATE(Instruction);}

  // Next level propagators: If the user does not overload a specific
  // instruction type, they can overload one of these to get the whole class
  // of instructions...
  //
  RetTy visitCastInst(CastInst &I)                { DELEGATE(UnaryInstruction);}
  RetTy visitUnaryOperator(UnaryOperator &I)      { DELEGATE(UnaryInstruction);}
  RetTy visitBinaryOperator(BinaryOperator &I)    { DELEGATE(Instruction);}
  RetTy visitCmpInst(CmpInst &I)                  { DELEGATE(Instruction);}
  RetTy visitUnaryInstruction(UnaryInstruction &I){ DELEGATE(Instruction);}

  // The next level delegation for `CallBase` is slightly more complex in order
  // to support visiting cases where the call is also a terminator.
  RetTy visitCallBase(CallBase &I) {
    if (isa<InvokeInst>(I))
      return static_cast<SubClass *>(this)->visitTerminator(I);

    DELEGATE(Instruction);
  }

  // Provide a legacy visitor for a 'callsite' that visits both calls and
  // invokes.
  //
  // Prefer overriding the type system based `CallBase` instead.
  RetTy visitCallSite(CallSite CS) {
    assert(CS);
    Instruction &I = *CS.getInstruction();
    DELEGATE(CallBase);
  }

  // If the user wants a 'default' case, they can choose to override this
  // function.  If this function is not overloaded in the user's subclass, then
  // this instruction just gets ignored.
  //
  // Note that you MUST override this function if your return type is not void.
  //
  void visitInstruction(Instruction &I) {}  // Ignore unhandled instructions

private:
  // Special helper function to delegate to CallInst subclass visitors.
  RetTy delegateCallInst(CallInst &I) {
    if (const Function *F = I.getCalledFunction()) {
      switch (F->getIntrinsicID()) {
      default:                     DELEGATE(IntrinsicInst);
      case Intrinsic::dbg_declare: DELEGATE(DbgDeclareInst);
      case Intrinsic::dbg_value:   DELEGATE(DbgValueInst);
      case Intrinsic::dbg_label:   DELEGATE(DbgLabelInst);
      case Intrinsic::memcpy:      DELEGATE(MemCpyInst);
      case Intrinsic::memmove:     DELEGATE(MemMoveInst);
      case Intrinsic::memset:      DELEGATE(MemSetInst);
      case Intrinsic::vastart:     DELEGATE(VAStartInst);
      case Intrinsic::vaend:       DELEGATE(VAEndInst);
      case Intrinsic::vacopy:      DELEGATE(VACopyInst);
      case Intrinsic::not_intrinsic: break;
      }
    }
    DELEGATE(CallInst);
  }

  // An overload that will never actually be called, it is used only from dead
  // code in the dispatching from opcodes to instruction subclasses.
  RetTy delegateCallInst(Instruction &I) {
    llvm_unreachable("delegateCallInst called for non-CallInst");
  }
};

#undef DELEGATE

} // End llvm namespace

#endif
