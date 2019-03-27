//======- X86RetpolineThunks.cpp - Construct retpoline thunks for x86  --=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Pass that injects an MI thunk implementing a "retpoline". This is
/// a RET-implemented trampoline that is used to lower indirect calls in a way
/// that prevents speculation on some x86 processors and can be used to mitigate
/// security vulnerabilities due to targeted speculative execution and side
/// channels such as CVE-2017-5715.
///
/// TODO(chandlerc): All of this code could use better comments and
/// documentation.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "x86-retpoline-thunks"

static const char ThunkNamePrefix[] = "__llvm_retpoline_";
static const char R11ThunkName[]    = "__llvm_retpoline_r11";
static const char EAXThunkName[]    = "__llvm_retpoline_eax";
static const char ECXThunkName[]    = "__llvm_retpoline_ecx";
static const char EDXThunkName[]    = "__llvm_retpoline_edx";
static const char EDIThunkName[]    = "__llvm_retpoline_edi";

namespace {
class X86RetpolineThunks : public MachineFunctionPass {
public:
  static char ID;

  X86RetpolineThunks() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Retpoline Thunks"; }

  bool doInitialization(Module &M) override;
  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.addRequired<MachineModuleInfo>();
    AU.addPreserved<MachineModuleInfo>();
  }

private:
  MachineModuleInfo *MMI;
  const TargetMachine *TM;
  bool Is64Bit;
  const X86Subtarget *STI;
  const X86InstrInfo *TII;

  bool InsertedThunks;

  void createThunkFunction(Module &M, StringRef Name);
  void insertRegReturnAddrClobber(MachineBasicBlock &MBB, unsigned Reg);
  void populateThunk(MachineFunction &MF, unsigned Reg);
};

} // end anonymous namespace

FunctionPass *llvm::createX86RetpolineThunksPass() {
  return new X86RetpolineThunks();
}

char X86RetpolineThunks::ID = 0;

bool X86RetpolineThunks::doInitialization(Module &M) {
  InsertedThunks = false;
  return false;
}

bool X86RetpolineThunks::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << getPassName() << '\n');

  TM = &MF.getTarget();;
  STI = &MF.getSubtarget<X86Subtarget>();
  TII = STI->getInstrInfo();
  Is64Bit = TM->getTargetTriple().getArch() == Triple::x86_64;

  MMI = &getAnalysis<MachineModuleInfo>();
  Module &M = const_cast<Module &>(*MMI->getModule());

  // If this function is not a thunk, check to see if we need to insert
  // a thunk.
  if (!MF.getName().startswith(ThunkNamePrefix)) {
    // If we've already inserted a thunk, nothing else to do.
    if (InsertedThunks)
      return false;

    // Only add a thunk if one of the functions has the retpoline feature
    // enabled in its subtarget, and doesn't enable external thunks.
    // FIXME: Conditionalize on indirect calls so we don't emit a thunk when
    // nothing will end up calling it.
    // FIXME: It's a little silly to look at every function just to enumerate
    // the subtargets, but eventually we'll want to look at them for indirect
    // calls, so maybe this is OK.
    if ((!STI->useRetpolineIndirectCalls() &&
         !STI->useRetpolineIndirectBranches()) ||
        STI->useRetpolineExternalThunk())
      return false;

    // Otherwise, we need to insert the thunk.
    // WARNING: This is not really a well behaving thing to do in a function
    // pass. We extract the module and insert a new function (and machine
    // function) directly into the module.
    if (Is64Bit)
      createThunkFunction(M, R11ThunkName);
    else
      for (StringRef Name :
           {EAXThunkName, ECXThunkName, EDXThunkName, EDIThunkName})
        createThunkFunction(M, Name);
    InsertedThunks = true;
    return true;
  }

  // If this *is* a thunk function, we need to populate it with the correct MI.
  if (Is64Bit) {
    assert(MF.getName() == "__llvm_retpoline_r11" &&
           "Should only have an r11 thunk on 64-bit targets");

    // __llvm_retpoline_r11:
    //   callq .Lr11_call_target
    // .Lr11_capture_spec:
    //   pause
    //   lfence
    //   jmp .Lr11_capture_spec
    // .align 16
    // .Lr11_call_target:
    //   movq %r11, (%rsp)
    //   retq
    populateThunk(MF, X86::R11);
  } else {
    // For 32-bit targets we need to emit a collection of thunks for various
    // possible scratch registers as well as a fallback that uses EDI, which is
    // normally callee saved.
    //   __llvm_retpoline_eax:
    //         calll .Leax_call_target
    //   .Leax_capture_spec:
    //         pause
    //         jmp .Leax_capture_spec
    //   .align 16
    //   .Leax_call_target:
    //         movl %eax, (%esp)  # Clobber return addr
    //         retl
    //
    //   __llvm_retpoline_ecx:
    //   ... # Same setup
    //         movl %ecx, (%esp)
    //         retl
    //
    //   __llvm_retpoline_edx:
    //   ... # Same setup
    //         movl %edx, (%esp)
    //         retl
    //
    //   __llvm_retpoline_edi:
    //   ... # Same setup
    //         movl %edi, (%esp)
    //         retl
    if (MF.getName() == EAXThunkName)
      populateThunk(MF, X86::EAX);
    else if (MF.getName() == ECXThunkName)
      populateThunk(MF, X86::ECX);
    else if (MF.getName() == EDXThunkName)
      populateThunk(MF, X86::EDX);
    else if (MF.getName() == EDIThunkName)
      populateThunk(MF, X86::EDI);
    else
      llvm_unreachable("Invalid thunk name on x86-32!");
  }

  return true;
}

void X86RetpolineThunks::createThunkFunction(Module &M, StringRef Name) {
  assert(Name.startswith(ThunkNamePrefix) &&
         "Created a thunk with an unexpected prefix!");

  LLVMContext &Ctx = M.getContext();
  auto Type = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F =
      Function::Create(Type, GlobalValue::LinkOnceODRLinkage, Name, &M);
  F->setVisibility(GlobalValue::HiddenVisibility);
  F->setComdat(M.getOrInsertComdat(Name));

  // Add Attributes so that we don't create a frame, unwind information, or
  // inline.
  AttrBuilder B;
  B.addAttribute(llvm::Attribute::NoUnwind);
  B.addAttribute(llvm::Attribute::Naked);
  F->addAttributes(llvm::AttributeList::FunctionIndex, B);

  // Populate our function a bit so that we can verify.
  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(Entry);

  Builder.CreateRetVoid();

  // MachineFunctions/MachineBasicBlocks aren't created automatically for the
  // IR-level constructs we already made. Create them and insert them into the
  // module.
  MachineFunction &MF = MMI->getOrCreateMachineFunction(*F);
  MachineBasicBlock *EntryMBB = MF.CreateMachineBasicBlock(Entry);

  // Insert EntryMBB into MF. It's not in the module until we do this.
  MF.insert(MF.end(), EntryMBB);
}

void X86RetpolineThunks::insertRegReturnAddrClobber(MachineBasicBlock &MBB,
                                                    unsigned Reg) {
  const unsigned MovOpc = Is64Bit ? X86::MOV64mr : X86::MOV32mr;
  const unsigned SPReg = Is64Bit ? X86::RSP : X86::ESP;
  addRegOffset(BuildMI(&MBB, DebugLoc(), TII->get(MovOpc)), SPReg, false, 0)
      .addReg(Reg);
}

void X86RetpolineThunks::populateThunk(MachineFunction &MF,
                                       unsigned Reg) {
  // Set MF properties. We never use vregs...
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);

  // Grab the entry MBB and erase any other blocks. O0 codegen appears to
  // generate two bbs for the entry block.
  MachineBasicBlock *Entry = &MF.front();
  Entry->clear();
  while (MF.size() > 1)
    MF.erase(std::next(MF.begin()));

  MachineBasicBlock *CaptureSpec = MF.CreateMachineBasicBlock(Entry->getBasicBlock());
  MachineBasicBlock *CallTarget = MF.CreateMachineBasicBlock(Entry->getBasicBlock());
  MCSymbol *TargetSym = MF.getContext().createTempSymbol();
  MF.push_back(CaptureSpec);
  MF.push_back(CallTarget);

  const unsigned CallOpc = Is64Bit ? X86::CALL64pcrel32 : X86::CALLpcrel32;
  const unsigned RetOpc = Is64Bit ? X86::RETQ : X86::RETL;

  Entry->addLiveIn(Reg);
  BuildMI(Entry, DebugLoc(), TII->get(CallOpc)).addSym(TargetSym);

  // The MIR verifier thinks that the CALL in the entry block will fall through
  // to CaptureSpec, so mark it as the successor. Technically, CaptureTarget is
  // the successor, but the MIR verifier doesn't know how to cope with that.
  Entry->addSuccessor(CaptureSpec);

  // In the capture loop for speculation, we want to stop the processor from
  // speculating as fast as possible. On Intel processors, the PAUSE instruction
  // will block speculation without consuming any execution resources. On AMD
  // processors, the PAUSE instruction is (essentially) a nop, so we also use an
  // LFENCE instruction which they have advised will stop speculation as well
  // with minimal resource utilization. We still end the capture with a jump to
  // form an infinite loop to fully guarantee that no matter what implementation
  // of the x86 ISA, speculating this code path never escapes.
  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::PAUSE));
  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::LFENCE));
  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::JMP_1)).addMBB(CaptureSpec);
  CaptureSpec->setHasAddressTaken();
  CaptureSpec->addSuccessor(CaptureSpec);

  CallTarget->addLiveIn(Reg);
  CallTarget->setHasAddressTaken();
  CallTarget->setAlignment(4);
  insertRegReturnAddrClobber(*CallTarget, Reg);
  CallTarget->back().setPreInstrSymbol(MF, TargetSym);
  BuildMI(CallTarget, DebugLoc(), TII->get(RetOpc));
}
