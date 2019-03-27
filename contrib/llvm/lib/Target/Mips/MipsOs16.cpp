//===---- MipsOs16.cpp for Mips Option -Os16                       --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an optimization phase for the MIPS target.
//
//===----------------------------------------------------------------------===//

#include "Mips.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mips-os16"

static cl::opt<std::string> Mips32FunctionMask(
  "mips32-function-mask",
  cl::init(""),
  cl::desc("Force function to be mips32"),
  cl::Hidden);

namespace {
  class MipsOs16 : public ModulePass {
  public:
    static char ID;

    MipsOs16() : ModulePass(ID) {}

    StringRef getPassName() const override { return "MIPS Os16 Optimization"; }

    bool runOnModule(Module &M) override;
  };

  char MipsOs16::ID = 0;
}

// Figure out if we need float point based on the function signature.
// We need to move variables in and/or out of floating point
// registers because of the ABI
//
static  bool needsFPFromSig(Function &F) {
  Type* RetType = F.getReturnType();
  switch (RetType->getTypeID()) {
  case Type::FloatTyID:
  case Type::DoubleTyID:
    return true;
  default:
    ;
  }
  if (F.arg_size() >=1) {
    Argument &Arg = *F.arg_begin();
    switch (Arg.getType()->getTypeID()) {
    case Type::FloatTyID:
    case Type::DoubleTyID:
      return true;
    default:
      ;
    }
  }
  return false;
}

// Figure out if the function will need floating point operations
//
static bool needsFP(Function &F) {
  if (needsFPFromSig(F))
    return true;
  for (Function::const_iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
    for (BasicBlock::const_iterator I = BB->begin(), E = BB->end();
         I != E; ++I) {
      const Instruction &Inst = *I;
      switch (Inst.getOpcode()) {
      case Instruction::FAdd:
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
      case Instruction::FRem:
      case Instruction::FPToUI:
      case Instruction::FPToSI:
      case Instruction::UIToFP:
      case Instruction::SIToFP:
      case Instruction::FPTrunc:
      case Instruction::FPExt:
      case Instruction::FCmp:
        return true;
      default:
        ;
      }
      if (const CallInst *CI = dyn_cast<CallInst>(I)) {
        LLVM_DEBUG(dbgs() << "Working on call"
                          << "\n");
        Function &F_ =  *CI->getCalledFunction();
        if (needsFPFromSig(F_))
          return true;
      }
    }
  return false;
}


bool MipsOs16::runOnModule(Module &M) {
  bool usingMask = Mips32FunctionMask.length() > 0;
  bool doneUsingMask = false; // this will make it stop repeating

  LLVM_DEBUG(dbgs() << "Run on Module MipsOs16 \n"
                    << Mips32FunctionMask << "\n");
  if (usingMask)
    LLVM_DEBUG(dbgs() << "using mask \n" << Mips32FunctionMask << "\n");

  unsigned int functionIndex = 0;
  bool modified = false;

  for (auto &F : M) {
    if (F.isDeclaration())
      continue;

    LLVM_DEBUG(dbgs() << "Working on " << F.getName() << "\n");
    if (usingMask) {
      if (!doneUsingMask) {
        if (functionIndex == Mips32FunctionMask.length())
          functionIndex = 0;
        switch (Mips32FunctionMask[functionIndex]) {
        case '1':
          LLVM_DEBUG(dbgs() << "mask forced mips32: " << F.getName() << "\n");
          F.addFnAttr("nomips16");
          break;
        case '.':
          doneUsingMask = true;
          break;
        default:
          break;
        }
        functionIndex++;
      }
    }
    else {
      if (needsFP(F)) {
        LLVM_DEBUG(dbgs() << "os16 forced mips32: " << F.getName() << "\n");
        F.addFnAttr("nomips16");
      }
      else {
        LLVM_DEBUG(dbgs() << "os16 forced mips16: " << F.getName() << "\n");
        F.addFnAttr("mips16");
      }
    }
  }

  return modified;
}

ModulePass *llvm::createMipsOs16Pass() { return new MipsOs16(); }
