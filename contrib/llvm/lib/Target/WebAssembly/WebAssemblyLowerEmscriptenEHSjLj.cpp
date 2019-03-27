//=== WebAssemblyLowerEmscriptenEHSjLj.cpp - Lower exceptions for Emscripten =//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file lowers exception-related instructions and setjmp/longjmp
/// function calls in order to use Emscripten's JavaScript try and catch
/// mechanism.
///
/// To handle exceptions and setjmp/longjmps, this scheme relies on JavaScript's
/// try and catch syntax and relevant exception-related libraries implemented
/// in JavaScript glue code that will be produced by Emscripten. This is similar
/// to the current Emscripten asm.js exception handling in fastcomp. For
/// fastcomp's EH / SjLj scheme, see these files in fastcomp LLVM branch:
/// (Location: https://github.com/kripken/emscripten-fastcomp)
/// lib/Target/JSBackend/NaCl/LowerEmExceptionsPass.cpp
/// lib/Target/JSBackend/NaCl/LowerEmSetjmp.cpp
/// lib/Target/JSBackend/JSBackend.cpp
/// lib/Target/JSBackend/CallHandlers.h
///
/// * Exception handling
/// This pass lowers invokes and landingpads into library functions in JS glue
/// code. Invokes are lowered into function wrappers called invoke wrappers that
/// exist in JS side, which wraps the original function call with JS try-catch.
/// If an exception occurred, cxa_throw() function in JS side sets some
/// variables (see below) so we can check whether an exception occurred from
/// wasm code and handle it appropriately.
///
/// * Setjmp-longjmp handling
/// This pass lowers setjmp to a reasonably-performant approach for emscripten.
/// The idea is that each block with a setjmp is broken up into two parts: the
/// part containing setjmp and the part right after the setjmp. The latter part
/// is either reached from the setjmp, or later from a longjmp. To handle the
/// longjmp, all calls that might longjmp are also called using invoke wrappers
/// and thus JS / try-catch. JS longjmp() function also sets some variables so
/// we can check / whether a longjmp occurred from wasm code. Each block with a
/// function call that might longjmp is also split up after the longjmp call.
/// After the longjmp call, we check whether a longjmp occurred, and if it did,
/// which setjmp it corresponds to, and jump to the right post-setjmp block.
/// We assume setjmp-longjmp handling always run after EH handling, which means
/// we don't expect any exception-related instructions when SjLj runs.
/// FIXME Currently this scheme does not support indirect call of setjmp,
/// because of the limitation of the scheme itself. fastcomp does not support it
/// either.
///
/// In detail, this pass does following things:
///
/// 1) Assumes the existence of global variables: __THREW__, __threwValue
///    __THREW__ and __threwValue will be set in invoke wrappers
///    in JS glue code. For what invoke wrappers are, refer to 3). These
///    variables are used for both exceptions and setjmp/longjmps.
///    __THREW__ indicates whether an exception or a longjmp occurred or not. 0
///    means nothing occurred, 1 means an exception occurred, and other numbers
///    mean a longjmp occurred. In the case of longjmp, __threwValue variable
///    indicates the corresponding setjmp buffer the longjmp corresponds to.
///
/// * Exception handling
///
/// 2) We assume the existence of setThrew and setTempRet0/getTempRet0 functions
///    at link time.
///    The global variables in 1) will exist in wasm address space,
///    but their values should be set in JS code, so these functions
///    as interfaces to JS glue code. These functions are equivalent to the
///    following JS functions, which actually exist in asm.js version of JS
///    library.
///
///    function setThrew(threw, value) {
///      if (__THREW__ == 0) {
///        __THREW__ = threw;
///        __threwValue = value;
///      }
///    }
//
///    setTempRet0 is called from __cxa_find_matching_catch() in JS glue code.
///
///    In exception handling, getTempRet0 indicates the type of an exception
///    caught, and in setjmp/longjmp, it means the second argument to longjmp
///    function.
///
/// 3) Lower
///      invoke @func(arg1, arg2) to label %invoke.cont unwind label %lpad
///    into
///      __THREW__ = 0;
///      call @__invoke_SIG(func, arg1, arg2)
///      %__THREW__.val = __THREW__;
///      __THREW__ = 0;
///      if (%__THREW__.val == 1)
///        goto %lpad
///      else
///         goto %invoke.cont
///    SIG is a mangled string generated based on the LLVM IR-level function
///    signature. After LLVM IR types are lowered to the target wasm types,
///    the names for these wrappers will change based on wasm types as well,
///    as in invoke_vi (function takes an int and returns void). The bodies of
///    these wrappers will be generated in JS glue code, and inside those
///    wrappers we use JS try-catch to generate actual exception effects. It
///    also calls the original callee function. An example wrapper in JS code
///    would look like this:
///      function invoke_vi(index,a1) {
///        try {
///          Module["dynCall_vi"](index,a1); // This calls original callee
///        } catch(e) {
///          if (typeof e !== 'number' && e !== 'longjmp') throw e;
///          asm["setThrew"](1, 0); // setThrew is called here
///        }
///      }
///    If an exception is thrown, __THREW__ will be set to true in a wrapper,
///    so we can jump to the right BB based on this value.
///
/// 4) Lower
///      %val = landingpad catch c1 catch c2 catch c3 ...
///      ... use %val ...
///    into
///      %fmc = call @__cxa_find_matching_catch_N(c1, c2, c3, ...)
///      %val = {%fmc, getTempRet0()}
///      ... use %val ...
///    Here N is a number calculated based on the number of clauses.
///    setTempRet0 is called from __cxa_find_matching_catch() in JS glue code.
///
/// 5) Lower
///      resume {%a, %b}
///    into
///      call @__resumeException(%a)
///    where __resumeException() is a function in JS glue code.
///
/// 6) Lower
///      call @llvm.eh.typeid.for(type) (intrinsic)
///    into
///      call @llvm_eh_typeid_for(type)
///    llvm_eh_typeid_for function will be generated in JS glue code.
///
/// * Setjmp / Longjmp handling
///
/// In case calls to longjmp() exists
///
/// 1) Lower
///      longjmp(buf, value)
///    into
///      emscripten_longjmp_jmpbuf(buf, value)
///    emscripten_longjmp_jmpbuf will be lowered to emscripten_longjmp later.
///
/// In case calls to setjmp() exists
///
/// 2) In the function entry that calls setjmp, initialize setjmpTable and
///    sejmpTableSize as follows:
///      setjmpTableSize = 4;
///      setjmpTable = (int *) malloc(40);
///      setjmpTable[0] = 0;
///    setjmpTable and setjmpTableSize are used in saveSetjmp() function in JS
///    code.
///
/// 3) Lower
///      setjmp(buf)
///    into
///      setjmpTable = saveSetjmp(buf, label, setjmpTable, setjmpTableSize);
///      setjmpTableSize = getTempRet0();
///    For each dynamic setjmp call, setjmpTable stores its ID (a number which
///    is incrementally assigned from 0) and its label (a unique number that
///    represents each callsite of setjmp). When we need more entries in
///    setjmpTable, it is reallocated in saveSetjmp() in JS code and it will
///    return the new table address, and assign the new table size in
///    setTempRet0(). saveSetjmp also stores the setjmp's ID into the buffer
///    buf. A BB with setjmp is split into two after setjmp call in order to
///    make the post-setjmp BB the possible destination of longjmp BB.
///
///
/// 4) Lower every call that might longjmp into
///      __THREW__ = 0;
///      call @__invoke_SIG(func, arg1, arg2)
///      %__THREW__.val = __THREW__;
///      __THREW__ = 0;
///      if (%__THREW__.val != 0 & __threwValue != 0) {
///        %label = testSetjmp(mem[%__THREW__.val], setjmpTable,
///                            setjmpTableSize);
///        if (%label == 0)
///          emscripten_longjmp(%__THREW__.val, __threwValue);
///        setTempRet0(__threwValue);
///      } else {
///        %label = -1;
///      }
///      longjmp_result = getTempRet0();
///      switch label {
///        label 1: goto post-setjmp BB 1
///        label 2: goto post-setjmp BB 2
///        ...
///        default: goto splitted next BB
///      }
///    testSetjmp examines setjmpTable to see if there is a matching setjmp
///    call. After calling an invoke wrapper, if a longjmp occurred, __THREW__
///    will be the address of matching jmp_buf buffer and __threwValue be the
///    second argument to longjmp. mem[__THREW__.val] is a setjmp ID that is
///    stored in saveSetjmp. testSetjmp returns a setjmp label, a unique ID to
///    each setjmp callsite. Label 0 means this longjmp buffer does not
///    correspond to one of the setjmp callsites in this function, so in this
///    case we just chain the longjmp to the caller. (Here we call
///    emscripten_longjmp, which is different from emscripten_longjmp_jmpbuf.
///    emscripten_longjmp_jmpbuf takes jmp_buf as its first argument, while
///    emscripten_longjmp takes an int. Both of them will eventually be lowered
///    to emscripten_longjmp in s2wasm, but here we need two signatures - we
///    can't translate an int value to a jmp_buf.)
///    Label -1 means no longjmp occurred. Otherwise we jump to the right
///    post-setjmp BB based on the label.
///
///===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-lower-em-ehsjlj"

static cl::list<std::string>
    EHWhitelist("emscripten-cxx-exceptions-whitelist",
                cl::desc("The list of function names in which Emscripten-style "
                         "exception handling is enabled (see emscripten "
                         "EMSCRIPTEN_CATCHING_WHITELIST options)"),
                cl::CommaSeparated);

namespace {
class WebAssemblyLowerEmscriptenEHSjLj final : public ModulePass {
  static const char *ResumeFName;
  static const char *EHTypeIDFName;
  static const char *EmLongjmpFName;
  static const char *EmLongjmpJmpbufFName;
  static const char *SaveSetjmpFName;
  static const char *TestSetjmpFName;
  static const char *FindMatchingCatchPrefix;
  static const char *InvokePrefix;

  bool EnableEH;   // Enable exception handling
  bool EnableSjLj; // Enable setjmp/longjmp handling

  GlobalVariable *ThrewGV;
  GlobalVariable *ThrewValueGV;
  Function *GetTempRet0Func;
  Function *SetTempRet0Func;
  Function *ResumeF;
  Function *EHTypeIDF;
  Function *EmLongjmpF;
  Function *EmLongjmpJmpbufF;
  Function *SaveSetjmpF;
  Function *TestSetjmpF;

  // __cxa_find_matching_catch_N functions.
  // Indexed by the number of clauses in an original landingpad instruction.
  DenseMap<int, Function *> FindMatchingCatches;
  // Map of <function signature string, invoke_ wrappers>
  StringMap<Function *> InvokeWrappers;
  // Set of whitelisted function names for exception handling
  std::set<std::string> EHWhitelistSet;

  StringRef getPassName() const override {
    return "WebAssembly Lower Emscripten Exceptions";
  }

  bool runEHOnFunction(Function &F);
  bool runSjLjOnFunction(Function &F);
  Function *getFindMatchingCatch(Module &M, unsigned NumClauses);

  template <typename CallOrInvoke> Value *wrapInvoke(CallOrInvoke *CI);
  void wrapTestSetjmp(BasicBlock *BB, Instruction *InsertPt, Value *Threw,
                      Value *SetjmpTable, Value *SetjmpTableSize, Value *&Label,
                      Value *&LongjmpResult, BasicBlock *&EndBB);
  template <typename CallOrInvoke> Function *getInvokeWrapper(CallOrInvoke *CI);

  bool areAllExceptionsAllowed() const { return EHWhitelistSet.empty(); }
  bool canLongjmp(Module &M, const Value *Callee) const;

  void rebuildSSA(Function &F);

public:
  static char ID;

  WebAssemblyLowerEmscriptenEHSjLj(bool EnableEH = true, bool EnableSjLj = true)
      : ModulePass(ID), EnableEH(EnableEH), EnableSjLj(EnableSjLj),
        ThrewGV(nullptr), ThrewValueGV(nullptr), GetTempRet0Func(nullptr),
        SetTempRet0Func(nullptr), ResumeF(nullptr), EHTypeIDF(nullptr),
        EmLongjmpF(nullptr), EmLongjmpJmpbufF(nullptr), SaveSetjmpF(nullptr),
        TestSetjmpF(nullptr) {
    EHWhitelistSet.insert(EHWhitelist.begin(), EHWhitelist.end());
  }
  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
  }
};
} // End anonymous namespace

const char *WebAssemblyLowerEmscriptenEHSjLj::ResumeFName = "__resumeException";
const char *WebAssemblyLowerEmscriptenEHSjLj::EHTypeIDFName =
    "llvm_eh_typeid_for";
const char *WebAssemblyLowerEmscriptenEHSjLj::EmLongjmpFName =
    "emscripten_longjmp";
const char *WebAssemblyLowerEmscriptenEHSjLj::EmLongjmpJmpbufFName =
    "emscripten_longjmp_jmpbuf";
const char *WebAssemblyLowerEmscriptenEHSjLj::SaveSetjmpFName = "saveSetjmp";
const char *WebAssemblyLowerEmscriptenEHSjLj::TestSetjmpFName = "testSetjmp";
const char *WebAssemblyLowerEmscriptenEHSjLj::FindMatchingCatchPrefix =
    "__cxa_find_matching_catch_";
const char *WebAssemblyLowerEmscriptenEHSjLj::InvokePrefix = "__invoke_";

char WebAssemblyLowerEmscriptenEHSjLj::ID = 0;
INITIALIZE_PASS(WebAssemblyLowerEmscriptenEHSjLj, DEBUG_TYPE,
                "WebAssembly Lower Emscripten Exceptions / Setjmp / Longjmp",
                false, false)

ModulePass *llvm::createWebAssemblyLowerEmscriptenEHSjLj(bool EnableEH,
                                                         bool EnableSjLj) {
  return new WebAssemblyLowerEmscriptenEHSjLj(EnableEH, EnableSjLj);
}

static bool canThrow(const Value *V) {
  if (const auto *F = dyn_cast<const Function>(V)) {
    // Intrinsics cannot throw
    if (F->isIntrinsic())
      return false;
    StringRef Name = F->getName();
    // leave setjmp and longjmp (mostly) alone, we process them properly later
    if (Name == "setjmp" || Name == "longjmp")
      return false;
    return !F->doesNotThrow();
  }
  // not a function, so an indirect call - can throw, we can't tell
  return true;
}

// Get a global variable with the given name.  If it doesn't exist declare it,
// which will generate an import and asssumes that it will exist at link time.
static GlobalVariable *getGlobalVariableI32(Module &M, IRBuilder<> &IRB,
                                            const char *Name) {
  if (M.getNamedGlobal(Name))
    report_fatal_error(Twine("variable name is reserved: ") + Name);

  return new GlobalVariable(M, IRB.getInt32Ty(), false,
                            GlobalValue::ExternalLinkage, nullptr, Name);
}

// Simple function name mangler.
// This function simply takes LLVM's string representation of parameter types
// and concatenate them with '_'. There are non-alphanumeric characters but llc
// is ok with it, and we need to postprocess these names after the lowering
// phase anyway.
static std::string getSignature(FunctionType *FTy) {
  std::string Sig;
  raw_string_ostream OS(Sig);
  OS << *FTy->getReturnType();
  for (Type *ParamTy : FTy->params())
    OS << "_" << *ParamTy;
  if (FTy->isVarArg())
    OS << "_...";
  Sig = OS.str();
  Sig.erase(remove_if(Sig, isspace), Sig.end());
  // When s2wasm parses .s file, a comma means the end of an argument. So a
  // mangled function name can contain any character but a comma.
  std::replace(Sig.begin(), Sig.end(), ',', '.');
  return Sig;
}

// Returns __cxa_find_matching_catch_N function, where N = NumClauses + 2.
// This is because a landingpad instruction contains two more arguments, a
// personality function and a cleanup bit, and __cxa_find_matching_catch_N
// functions are named after the number of arguments in the original landingpad
// instruction.
Function *
WebAssemblyLowerEmscriptenEHSjLj::getFindMatchingCatch(Module &M,
                                                       unsigned NumClauses) {
  if (FindMatchingCatches.count(NumClauses))
    return FindMatchingCatches[NumClauses];
  PointerType *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
  SmallVector<Type *, 16> Args(NumClauses, Int8PtrTy);
  FunctionType *FTy = FunctionType::get(Int8PtrTy, Args, false);
  Function *F =
      Function::Create(FTy, GlobalValue::ExternalLinkage,
                       FindMatchingCatchPrefix + Twine(NumClauses + 2), &M);
  FindMatchingCatches[NumClauses] = F;
  return F;
}

// Generate invoke wrapper seqence with preamble and postamble
// Preamble:
// __THREW__ = 0;
// Postamble:
// %__THREW__.val = __THREW__; __THREW__ = 0;
// Returns %__THREW__.val, which indicates whether an exception is thrown (or
// whether longjmp occurred), for future use.
template <typename CallOrInvoke>
Value *WebAssemblyLowerEmscriptenEHSjLj::wrapInvoke(CallOrInvoke *CI) {
  LLVMContext &C = CI->getModule()->getContext();

  // If we are calling a function that is noreturn, we must remove that
  // attribute. The code we insert here does expect it to return, after we
  // catch the exception.
  if (CI->doesNotReturn()) {
    if (auto *F = dyn_cast<Function>(CI->getCalledValue()))
      F->removeFnAttr(Attribute::NoReturn);
    CI->removeAttribute(AttributeList::FunctionIndex, Attribute::NoReturn);
  }

  IRBuilder<> IRB(C);
  IRB.SetInsertPoint(CI);

  // Pre-invoke
  // __THREW__ = 0;
  IRB.CreateStore(IRB.getInt32(0), ThrewGV);

  // Invoke function wrapper in JavaScript
  SmallVector<Value *, 16> Args;
  // Put the pointer to the callee as first argument, so it can be called
  // within the invoke wrapper later
  Args.push_back(CI->getCalledValue());
  Args.append(CI->arg_begin(), CI->arg_end());
  CallInst *NewCall = IRB.CreateCall(getInvokeWrapper(CI), Args);
  NewCall->takeName(CI);
  NewCall->setCallingConv(CI->getCallingConv());
  NewCall->setDebugLoc(CI->getDebugLoc());

  // Because we added the pointer to the callee as first argument, all
  // argument attribute indices have to be incremented by one.
  SmallVector<AttributeSet, 8> ArgAttributes;
  const AttributeList &InvokeAL = CI->getAttributes();

  // No attributes for the callee pointer.
  ArgAttributes.push_back(AttributeSet());
  // Copy the argument attributes from the original
  for (unsigned i = 0, e = CI->getNumArgOperands(); i < e; ++i)
    ArgAttributes.push_back(InvokeAL.getParamAttributes(i));

  // Reconstruct the AttributesList based on the vector we constructed.
  AttributeList NewCallAL =
      AttributeList::get(C, InvokeAL.getFnAttributes(),
                         InvokeAL.getRetAttributes(), ArgAttributes);
  NewCall->setAttributes(NewCallAL);

  CI->replaceAllUsesWith(NewCall);

  // Post-invoke
  // %__THREW__.val = __THREW__; __THREW__ = 0;
  Value *Threw = IRB.CreateLoad(ThrewGV, ThrewGV->getName() + ".val");
  IRB.CreateStore(IRB.getInt32(0), ThrewGV);
  return Threw;
}

// Get matching invoke wrapper based on callee signature
template <typename CallOrInvoke>
Function *WebAssemblyLowerEmscriptenEHSjLj::getInvokeWrapper(CallOrInvoke *CI) {
  Module *M = CI->getModule();
  SmallVector<Type *, 16> ArgTys;
  Value *Callee = CI->getCalledValue();
  FunctionType *CalleeFTy;
  if (auto *F = dyn_cast<Function>(Callee))
    CalleeFTy = F->getFunctionType();
  else {
    auto *CalleeTy = cast<PointerType>(Callee->getType())->getElementType();
    CalleeFTy = dyn_cast<FunctionType>(CalleeTy);
  }

  std::string Sig = getSignature(CalleeFTy);
  if (InvokeWrappers.find(Sig) != InvokeWrappers.end())
    return InvokeWrappers[Sig];

  // Put the pointer to the callee as first argument
  ArgTys.push_back(PointerType::getUnqual(CalleeFTy));
  // Add argument types
  ArgTys.append(CalleeFTy->param_begin(), CalleeFTy->param_end());

  FunctionType *FTy = FunctionType::get(CalleeFTy->getReturnType(), ArgTys,
                                        CalleeFTy->isVarArg());
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                 InvokePrefix + Sig, M);
  InvokeWrappers[Sig] = F;
  return F;
}

bool WebAssemblyLowerEmscriptenEHSjLj::canLongjmp(Module &M,
                                                  const Value *Callee) const {
  if (auto *CalleeF = dyn_cast<Function>(Callee))
    if (CalleeF->isIntrinsic())
      return false;

  // The reason we include malloc/free here is to exclude the malloc/free
  // calls generated in setjmp prep / cleanup routines.
  Function *SetjmpF = M.getFunction("setjmp");
  Function *MallocF = M.getFunction("malloc");
  Function *FreeF = M.getFunction("free");
  if (Callee == SetjmpF || Callee == MallocF || Callee == FreeF)
    return false;

  // There are functions in JS glue code
  if (Callee == ResumeF || Callee == EHTypeIDF || Callee == SaveSetjmpF ||
      Callee == TestSetjmpF)
    return false;

  // __cxa_find_matching_catch_N functions cannot longjmp
  if (Callee->getName().startswith(FindMatchingCatchPrefix))
    return false;

  // Exception-catching related functions
  Function *BeginCatchF = M.getFunction("__cxa_begin_catch");
  Function *EndCatchF = M.getFunction("__cxa_end_catch");
  Function *AllocExceptionF = M.getFunction("__cxa_allocate_exception");
  Function *ThrowF = M.getFunction("__cxa_throw");
  Function *TerminateF = M.getFunction("__clang_call_terminate");
  if (Callee == BeginCatchF || Callee == EndCatchF ||
      Callee == AllocExceptionF || Callee == ThrowF || Callee == TerminateF ||
      Callee == GetTempRet0Func || Callee == SetTempRet0Func)
    return false;

  // Otherwise we don't know
  return true;
}

// Generate testSetjmp function call seqence with preamble and postamble.
// The code this generates is equivalent to the following JavaScript code:
// if (%__THREW__.val != 0 & threwValue != 0) {
//   %label = _testSetjmp(mem[%__THREW__.val], setjmpTable, setjmpTableSize);
//   if (%label == 0)
//     emscripten_longjmp(%__THREW__.val, threwValue);
//   setTempRet0(threwValue);
// } else {
//   %label = -1;
// }
// %longjmp_result = getTempRet0();
//
// As output parameters. returns %label, %longjmp_result, and the BB the last
// instruction (%longjmp_result = ...) is in.
void WebAssemblyLowerEmscriptenEHSjLj::wrapTestSetjmp(
    BasicBlock *BB, Instruction *InsertPt, Value *Threw, Value *SetjmpTable,
    Value *SetjmpTableSize, Value *&Label, Value *&LongjmpResult,
    BasicBlock *&EndBB) {
  Function *F = BB->getParent();
  LLVMContext &C = BB->getModule()->getContext();
  IRBuilder<> IRB(C);
  IRB.SetInsertPoint(InsertPt);

  // if (%__THREW__.val != 0 & threwValue != 0)
  IRB.SetInsertPoint(BB);
  BasicBlock *ThenBB1 = BasicBlock::Create(C, "if.then1", F);
  BasicBlock *ElseBB1 = BasicBlock::Create(C, "if.else1", F);
  BasicBlock *EndBB1 = BasicBlock::Create(C, "if.end", F);
  Value *ThrewCmp = IRB.CreateICmpNE(Threw, IRB.getInt32(0));
  Value *ThrewValue =
      IRB.CreateLoad(ThrewValueGV, ThrewValueGV->getName() + ".val");
  Value *ThrewValueCmp = IRB.CreateICmpNE(ThrewValue, IRB.getInt32(0));
  Value *Cmp1 = IRB.CreateAnd(ThrewCmp, ThrewValueCmp, "cmp1");
  IRB.CreateCondBr(Cmp1, ThenBB1, ElseBB1);

  // %label = _testSetjmp(mem[%__THREW__.val], _setjmpTable, _setjmpTableSize);
  // if (%label == 0)
  IRB.SetInsertPoint(ThenBB1);
  BasicBlock *ThenBB2 = BasicBlock::Create(C, "if.then2", F);
  BasicBlock *EndBB2 = BasicBlock::Create(C, "if.end2", F);
  Value *ThrewInt = IRB.CreateIntToPtr(Threw, Type::getInt32PtrTy(C),
                                       Threw->getName() + ".i32p");
  Value *LoadedThrew =
      IRB.CreateLoad(ThrewInt, ThrewInt->getName() + ".loaded");
  Value *ThenLabel = IRB.CreateCall(
      TestSetjmpF, {LoadedThrew, SetjmpTable, SetjmpTableSize}, "label");
  Value *Cmp2 = IRB.CreateICmpEQ(ThenLabel, IRB.getInt32(0));
  IRB.CreateCondBr(Cmp2, ThenBB2, EndBB2);

  // emscripten_longjmp(%__THREW__.val, threwValue);
  IRB.SetInsertPoint(ThenBB2);
  IRB.CreateCall(EmLongjmpF, {Threw, ThrewValue});
  IRB.CreateUnreachable();

  // setTempRet0(threwValue);
  IRB.SetInsertPoint(EndBB2);
  IRB.CreateCall(SetTempRet0Func, ThrewValue);
  IRB.CreateBr(EndBB1);

  IRB.SetInsertPoint(ElseBB1);
  IRB.CreateBr(EndBB1);

  // longjmp_result = getTempRet0();
  IRB.SetInsertPoint(EndBB1);
  PHINode *LabelPHI = IRB.CreatePHI(IRB.getInt32Ty(), 2, "label");
  LabelPHI->addIncoming(ThenLabel, EndBB2);

  LabelPHI->addIncoming(IRB.getInt32(-1), ElseBB1);

  // Output parameter assignment
  Label = LabelPHI;
  EndBB = EndBB1;
  LongjmpResult = IRB.CreateCall(GetTempRet0Func, None, "longjmp_result");
}

void WebAssemblyLowerEmscriptenEHSjLj::rebuildSSA(Function &F) {
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
  DT.recalculate(F); // CFG has been changed
  SSAUpdater SSA;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      for (auto UI = I.use_begin(), UE = I.use_end(); UI != UE;) {
        Use &U = *UI;
        ++UI;
        SSA.Initialize(I.getType(), I.getName());
        SSA.AddAvailableValue(&BB, &I);
        Instruction *User = cast<Instruction>(U.getUser());
        if (User->getParent() == &BB)
          continue;

        if (PHINode *UserPN = dyn_cast<PHINode>(User))
          if (UserPN->getIncomingBlock(U) == &BB)
            continue;

        if (DT.dominates(&I, User))
          continue;
        SSA.RewriteUseAfterInsertions(U);
      }
    }
  }
}

bool WebAssemblyLowerEmscriptenEHSjLj::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Lower Emscripten EH & SjLj **********\n");

  LLVMContext &C = M.getContext();
  IRBuilder<> IRB(C);

  Function *SetjmpF = M.getFunction("setjmp");
  Function *LongjmpF = M.getFunction("longjmp");
  bool SetjmpUsed = SetjmpF && !SetjmpF->use_empty();
  bool LongjmpUsed = LongjmpF && !LongjmpF->use_empty();
  bool DoSjLj = EnableSjLj && (SetjmpUsed || LongjmpUsed);

  // Declare (or get) global variables __THREW__, __threwValue, and
  // getTempRet0/setTempRet0 function which are used in common for both
  // exception handling and setjmp/longjmp handling
  ThrewGV = getGlobalVariableI32(M, IRB, "__THREW__");
  ThrewValueGV = getGlobalVariableI32(M, IRB, "__threwValue");
  GetTempRet0Func =
      Function::Create(FunctionType::get(IRB.getInt32Ty(), false),
                       GlobalValue::ExternalLinkage, "getTempRet0", &M);
  SetTempRet0Func = Function::Create(
      FunctionType::get(IRB.getVoidTy(), IRB.getInt32Ty(), false),
      GlobalValue::ExternalLinkage, "setTempRet0", &M);
  GetTempRet0Func->setDoesNotThrow();
  SetTempRet0Func->setDoesNotThrow();

  bool Changed = false;

  // Exception handling
  if (EnableEH) {
    // Register __resumeException function
    FunctionType *ResumeFTy =
        FunctionType::get(IRB.getVoidTy(), IRB.getInt8PtrTy(), false);
    ResumeF = Function::Create(ResumeFTy, GlobalValue::ExternalLinkage,
                               ResumeFName, &M);

    // Register llvm_eh_typeid_for function
    FunctionType *EHTypeIDTy =
        FunctionType::get(IRB.getInt32Ty(), IRB.getInt8PtrTy(), false);
    EHTypeIDF = Function::Create(EHTypeIDTy, GlobalValue::ExternalLinkage,
                                 EHTypeIDFName, &M);

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      Changed |= runEHOnFunction(F);
    }
  }

  // Setjmp/longjmp handling
  if (DoSjLj) {
    Changed = true; // We have setjmp or longjmp somewhere

    if (LongjmpF) {
      // Replace all uses of longjmp with emscripten_longjmp_jmpbuf, which is
      // defined in JS code
      EmLongjmpJmpbufF = Function::Create(LongjmpF->getFunctionType(),
                                          GlobalValue::ExternalLinkage,
                                          EmLongjmpJmpbufFName, &M);

      LongjmpF->replaceAllUsesWith(EmLongjmpJmpbufF);
    }

    if (SetjmpF) {
      // Register saveSetjmp function
      FunctionType *SetjmpFTy = SetjmpF->getFunctionType();
      SmallVector<Type *, 4> Params = {SetjmpFTy->getParamType(0),
                                       IRB.getInt32Ty(), Type::getInt32PtrTy(C),
                                       IRB.getInt32Ty()};
      FunctionType *FTy =
          FunctionType::get(Type::getInt32PtrTy(C), Params, false);
      SaveSetjmpF = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                     SaveSetjmpFName, &M);

      // Register testSetjmp function
      Params = {IRB.getInt32Ty(), Type::getInt32PtrTy(C), IRB.getInt32Ty()};
      FTy = FunctionType::get(IRB.getInt32Ty(), Params, false);
      TestSetjmpF = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                     TestSetjmpFName, &M);

      FTy = FunctionType::get(IRB.getVoidTy(),
                              {IRB.getInt32Ty(), IRB.getInt32Ty()}, false);
      EmLongjmpF = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                    EmLongjmpFName, &M);

      // Only traverse functions that uses setjmp in order not to insert
      // unnecessary prep / cleanup code in every function
      SmallPtrSet<Function *, 8> SetjmpUsers;
      for (User *U : SetjmpF->users()) {
        auto *UI = cast<Instruction>(U);
        SetjmpUsers.insert(UI->getFunction());
      }
      for (Function *F : SetjmpUsers)
        runSjLjOnFunction(*F);
    }
  }

  if (!Changed) {
    // Delete unused global variables and functions
    if (ResumeF)
      ResumeF->eraseFromParent();
    if (EHTypeIDF)
      EHTypeIDF->eraseFromParent();
    if (EmLongjmpF)
      EmLongjmpF->eraseFromParent();
    if (SaveSetjmpF)
      SaveSetjmpF->eraseFromParent();
    if (TestSetjmpF)
      TestSetjmpF->eraseFromParent();
    return false;
  }

  return true;
}

bool WebAssemblyLowerEmscriptenEHSjLj::runEHOnFunction(Function &F) {
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);
  bool Changed = false;
  SmallVector<Instruction *, 64> ToErase;
  SmallPtrSet<LandingPadInst *, 32> LandingPads;
  bool AllowExceptions =
      areAllExceptionsAllowed() || EHWhitelistSet.count(F.getName());

  for (BasicBlock &BB : F) {
    auto *II = dyn_cast<InvokeInst>(BB.getTerminator());
    if (!II)
      continue;
    Changed = true;
    LandingPads.insert(II->getLandingPadInst());
    IRB.SetInsertPoint(II);

    bool NeedInvoke = AllowExceptions && canThrow(II->getCalledValue());
    if (NeedInvoke) {
      // Wrap invoke with invoke wrapper and generate preamble/postamble
      Value *Threw = wrapInvoke(II);
      ToErase.push_back(II);

      // Insert a branch based on __THREW__ variable
      Value *Cmp = IRB.CreateICmpEQ(Threw, IRB.getInt32(1), "cmp");
      IRB.CreateCondBr(Cmp, II->getUnwindDest(), II->getNormalDest());

    } else {
      // This can't throw, and we don't need this invoke, just replace it with a
      // call+branch
      SmallVector<Value *, 16> Args(II->arg_begin(), II->arg_end());
      CallInst *NewCall = IRB.CreateCall(II->getCalledValue(), Args);
      NewCall->takeName(II);
      NewCall->setCallingConv(II->getCallingConv());
      NewCall->setDebugLoc(II->getDebugLoc());
      NewCall->setAttributes(II->getAttributes());
      II->replaceAllUsesWith(NewCall);
      ToErase.push_back(II);

      IRB.CreateBr(II->getNormalDest());

      // Remove any PHI node entries from the exception destination
      II->getUnwindDest()->removePredecessor(&BB);
    }
  }

  // Process resume instructions
  for (BasicBlock &BB : F) {
    // Scan the body of the basic block for resumes
    for (Instruction &I : BB) {
      auto *RI = dyn_cast<ResumeInst>(&I);
      if (!RI)
        continue;

      // Split the input into legal values
      Value *Input = RI->getValue();
      IRB.SetInsertPoint(RI);
      Value *Low = IRB.CreateExtractValue(Input, 0, "low");
      // Create a call to __resumeException function
      IRB.CreateCall(ResumeF, {Low});
      // Add a terminator to the block
      IRB.CreateUnreachable();
      ToErase.push_back(RI);
    }
  }

  // Process llvm.eh.typeid.for intrinsics
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      const Function *Callee = CI->getCalledFunction();
      if (!Callee)
        continue;
      if (Callee->getIntrinsicID() != Intrinsic::eh_typeid_for)
        continue;

      IRB.SetInsertPoint(CI);
      CallInst *NewCI =
          IRB.CreateCall(EHTypeIDF, CI->getArgOperand(0), "typeid");
      CI->replaceAllUsesWith(NewCI);
      ToErase.push_back(CI);
    }
  }

  // Look for orphan landingpads, can occur in blocks with no predecessors
  for (BasicBlock &BB : F) {
    Instruction *I = BB.getFirstNonPHI();
    if (auto *LPI = dyn_cast<LandingPadInst>(I))
      LandingPads.insert(LPI);
  }

  // Handle all the landingpad for this function together, as multiple invokes
  // may share a single lp
  for (LandingPadInst *LPI : LandingPads) {
    IRB.SetInsertPoint(LPI);
    SmallVector<Value *, 16> FMCArgs;
    for (unsigned i = 0, e = LPI->getNumClauses(); i < e; ++i) {
      Constant *Clause = LPI->getClause(i);
      // As a temporary workaround for the lack of aggregate varargs support
      // in the interface between JS and wasm, break out filter operands into
      // their component elements.
      if (LPI->isFilter(i)) {
        auto *ATy = cast<ArrayType>(Clause->getType());
        for (unsigned j = 0, e = ATy->getNumElements(); j < e; ++j) {
          Value *EV = IRB.CreateExtractValue(Clause, makeArrayRef(j), "filter");
          FMCArgs.push_back(EV);
        }
      } else
        FMCArgs.push_back(Clause);
    }

    // Create a call to __cxa_find_matching_catch_N function
    Function *FMCF = getFindMatchingCatch(M, FMCArgs.size());
    CallInst *FMCI = IRB.CreateCall(FMCF, FMCArgs, "fmc");
    Value *Undef = UndefValue::get(LPI->getType());
    Value *Pair0 = IRB.CreateInsertValue(Undef, FMCI, 0, "pair0");
    Value *TempRet0 = IRB.CreateCall(GetTempRet0Func, None, "tempret0");
    Value *Pair1 = IRB.CreateInsertValue(Pair0, TempRet0, 1, "pair1");

    LPI->replaceAllUsesWith(Pair1);
    ToErase.push_back(LPI);
  }

  // Erase everything we no longer need in this function
  for (Instruction *I : ToErase)
    I->eraseFromParent();

  return Changed;
}

bool WebAssemblyLowerEmscriptenEHSjLj::runSjLjOnFunction(Function &F) {
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);
  SmallVector<Instruction *, 64> ToErase;
  // Vector of %setjmpTable values
  std::vector<Instruction *> SetjmpTableInsts;
  // Vector of %setjmpTableSize values
  std::vector<Instruction *> SetjmpTableSizeInsts;

  // Setjmp preparation

  // This instruction effectively means %setjmpTableSize = 4.
  // We create this as an instruction intentionally, and we don't want to fold
  // this instruction to a constant 4, because this value will be used in
  // SSAUpdater.AddAvailableValue(...) later.
  BasicBlock &EntryBB = F.getEntryBlock();
  BinaryOperator *SetjmpTableSize = BinaryOperator::Create(
      Instruction::Add, IRB.getInt32(4), IRB.getInt32(0), "setjmpTableSize",
      &*EntryBB.getFirstInsertionPt());
  // setjmpTable = (int *) malloc(40);
  Instruction *SetjmpTable = CallInst::CreateMalloc(
      SetjmpTableSize, IRB.getInt32Ty(), IRB.getInt32Ty(), IRB.getInt32(40),
      nullptr, nullptr, "setjmpTable");
  // setjmpTable[0] = 0;
  IRB.SetInsertPoint(SetjmpTableSize);
  IRB.CreateStore(IRB.getInt32(0), SetjmpTable);
  SetjmpTableInsts.push_back(SetjmpTable);
  SetjmpTableSizeInsts.push_back(SetjmpTableSize);

  // Setjmp transformation
  std::vector<PHINode *> SetjmpRetPHIs;
  Function *SetjmpF = M.getFunction("setjmp");
  for (User *U : SetjmpF->users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      report_fatal_error("Does not support indirect calls to setjmp");

    BasicBlock *BB = CI->getParent();
    if (BB->getParent() != &F) // in other function
      continue;

    // The tail is everything right after the call, and will be reached once
    // when setjmp is called, and later when longjmp returns to the setjmp
    BasicBlock *Tail = SplitBlock(BB, CI->getNextNode());
    // Add a phi to the tail, which will be the output of setjmp, which
    // indicates if this is the first call or a longjmp back. The phi directly
    // uses the right value based on where we arrive from
    IRB.SetInsertPoint(Tail->getFirstNonPHI());
    PHINode *SetjmpRet = IRB.CreatePHI(IRB.getInt32Ty(), 2, "setjmp.ret");

    // setjmp initial call returns 0
    SetjmpRet->addIncoming(IRB.getInt32(0), BB);
    // The proper output is now this, not the setjmp call itself
    CI->replaceAllUsesWith(SetjmpRet);
    // longjmp returns to the setjmp will add themselves to this phi
    SetjmpRetPHIs.push_back(SetjmpRet);

    // Fix call target
    // Our index in the function is our place in the array + 1 to avoid index
    // 0, because index 0 means the longjmp is not ours to handle.
    IRB.SetInsertPoint(CI);
    Value *Args[] = {CI->getArgOperand(0), IRB.getInt32(SetjmpRetPHIs.size()),
                     SetjmpTable, SetjmpTableSize};
    Instruction *NewSetjmpTable =
        IRB.CreateCall(SaveSetjmpF, Args, "setjmpTable");
    Instruction *NewSetjmpTableSize =
        IRB.CreateCall(GetTempRet0Func, None, "setjmpTableSize");
    SetjmpTableInsts.push_back(NewSetjmpTable);
    SetjmpTableSizeInsts.push_back(NewSetjmpTableSize);
    ToErase.push_back(CI);
  }

  // Update each call that can longjmp so it can return to a setjmp where
  // relevant.

  // Because we are creating new BBs while processing and don't want to make
  // all these newly created BBs candidates again for longjmp processing, we
  // first make the vector of candidate BBs.
  std::vector<BasicBlock *> BBs;
  for (BasicBlock &BB : F)
    BBs.push_back(&BB);

  // BBs.size() will change within the loop, so we query it every time
  for (unsigned i = 0; i < BBs.size(); i++) {
    BasicBlock *BB = BBs[i];
    for (Instruction &I : *BB) {
      assert(!isa<InvokeInst>(&I));
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;

      const Value *Callee = CI->getCalledValue();
      if (!canLongjmp(M, Callee))
        continue;

      Value *Threw = nullptr;
      BasicBlock *Tail;
      if (Callee->getName().startswith(InvokePrefix)) {
        // If invoke wrapper has already been generated for this call in
        // previous EH phase, search for the load instruction
        // %__THREW__.val = __THREW__;
        // in postamble after the invoke wrapper call
        LoadInst *ThrewLI = nullptr;
        StoreInst *ThrewResetSI = nullptr;
        for (auto I = std::next(BasicBlock::iterator(CI)), IE = BB->end();
             I != IE; ++I) {
          if (auto *LI = dyn_cast<LoadInst>(I))
            if (auto *GV = dyn_cast<GlobalVariable>(LI->getPointerOperand()))
              if (GV == ThrewGV) {
                Threw = ThrewLI = LI;
                break;
              }
        }
        // Search for the store instruction after the load above
        // __THREW__ = 0;
        for (auto I = std::next(BasicBlock::iterator(ThrewLI)), IE = BB->end();
             I != IE; ++I) {
          if (auto *SI = dyn_cast<StoreInst>(I))
            if (auto *GV = dyn_cast<GlobalVariable>(SI->getPointerOperand()))
              if (GV == ThrewGV && SI->getValueOperand() == IRB.getInt32(0)) {
                ThrewResetSI = SI;
                break;
              }
        }
        assert(Threw && ThrewLI && "Cannot find __THREW__ load after invoke");
        assert(ThrewResetSI && "Cannot find __THREW__ store after invoke");
        Tail = SplitBlock(BB, ThrewResetSI->getNextNode());

      } else {
        // Wrap call with invoke wrapper and generate preamble/postamble
        Threw = wrapInvoke(CI);
        ToErase.push_back(CI);
        Tail = SplitBlock(BB, CI->getNextNode());
      }

      // We need to replace the terminator in Tail - SplitBlock makes BB go
      // straight to Tail, we need to check if a longjmp occurred, and go to the
      // right setjmp-tail if so
      ToErase.push_back(BB->getTerminator());

      // Generate a function call to testSetjmp function and preamble/postamble
      // code to figure out (1) whether longjmp occurred (2) if longjmp
      // occurred, which setjmp it corresponds to
      Value *Label = nullptr;
      Value *LongjmpResult = nullptr;
      BasicBlock *EndBB = nullptr;
      wrapTestSetjmp(BB, CI, Threw, SetjmpTable, SetjmpTableSize, Label,
                     LongjmpResult, EndBB);
      assert(Label && LongjmpResult && EndBB);

      // Create switch instruction
      IRB.SetInsertPoint(EndBB);
      SwitchInst *SI = IRB.CreateSwitch(Label, Tail, SetjmpRetPHIs.size());
      // -1 means no longjmp happened, continue normally (will hit the default
      // switch case). 0 means a longjmp that is not ours to handle, needs a
      // rethrow. Otherwise the index is the same as the index in P+1 (to avoid
      // 0).
      for (unsigned i = 0; i < SetjmpRetPHIs.size(); i++) {
        SI->addCase(IRB.getInt32(i + 1), SetjmpRetPHIs[i]->getParent());
        SetjmpRetPHIs[i]->addIncoming(LongjmpResult, EndBB);
      }

      // We are splitting the block here, and must continue to find other calls
      // in the block - which is now split. so continue to traverse in the Tail
      BBs.push_back(Tail);
    }
  }

  // Erase everything we no longer need in this function
  for (Instruction *I : ToErase)
    I->eraseFromParent();

  // Free setjmpTable buffer before each return instruction
  for (BasicBlock &BB : F) {
    Instruction *TI = BB.getTerminator();
    if (isa<ReturnInst>(TI))
      CallInst::CreateFree(SetjmpTable, TI);
  }

  // Every call to saveSetjmp can change setjmpTable and setjmpTableSize
  // (when buffer reallocation occurs)
  // entry:
  //   setjmpTableSize = 4;
  //   setjmpTable = (int *) malloc(40);
  //   setjmpTable[0] = 0;
  // ...
  // somebb:
  //   setjmpTable = saveSetjmp(buf, label, setjmpTable, setjmpTableSize);
  //   setjmpTableSize = getTempRet0();
  // So we need to make sure the SSA for these variables is valid so that every
  // saveSetjmp and testSetjmp calls have the correct arguments.
  SSAUpdater SetjmpTableSSA;
  SSAUpdater SetjmpTableSizeSSA;
  SetjmpTableSSA.Initialize(Type::getInt32PtrTy(C), "setjmpTable");
  SetjmpTableSizeSSA.Initialize(Type::getInt32Ty(C), "setjmpTableSize");
  for (Instruction *I : SetjmpTableInsts)
    SetjmpTableSSA.AddAvailableValue(I->getParent(), I);
  for (Instruction *I : SetjmpTableSizeInsts)
    SetjmpTableSizeSSA.AddAvailableValue(I->getParent(), I);

  for (auto UI = SetjmpTable->use_begin(), UE = SetjmpTable->use_end();
       UI != UE;) {
    // Grab the use before incrementing the iterator.
    Use &U = *UI;
    // Increment the iterator before removing the use from the list.
    ++UI;
    if (Instruction *I = dyn_cast<Instruction>(U.getUser()))
      if (I->getParent() != &EntryBB)
        SetjmpTableSSA.RewriteUse(U);
  }
  for (auto UI = SetjmpTableSize->use_begin(), UE = SetjmpTableSize->use_end();
       UI != UE;) {
    Use &U = *UI;
    ++UI;
    if (Instruction *I = dyn_cast<Instruction>(U.getUser()))
      if (I->getParent() != &EntryBB)
        SetjmpTableSizeSSA.RewriteUse(U);
  }

  // Finally, our modifications to the cfg can break dominance of SSA variables.
  // For example, in this code,
  // if (x()) { .. setjmp() .. }
  // if (y()) { .. longjmp() .. }
  // We must split the longjmp block, and it can jump into the block splitted
  // from setjmp one. But that means that when we split the setjmp block, it's
  // first part no longer dominates its second part - there is a theoretically
  // possible control flow path where x() is false, then y() is true and we
  // reach the second part of the setjmp block, without ever reaching the first
  // part. So, we rebuild SSA form here.
  rebuildSSA(F);
  return true;
}
