//===-- RenderScriptx86ABIFixups.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <set>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Pass.h"

#include "lldb/Target/Process.h"
#include "lldb/Utility/Log.h"

using namespace lldb_private;
namespace {

bool isRSAPICall(llvm::Module &module, llvm::CallInst *call_inst) {
  // TODO get the list of renderscript modules from lldb and check if
  // this llvm::Module calls into any of them.
  (void)module;
  const auto func_name = call_inst->getCalledFunction()->getName();
  if (func_name.startswith("llvm") || func_name.startswith("lldb"))
    return false;

  if (call_inst->getCalledFunction()->isIntrinsic())
    return false;

  return true;
}

bool isRSLargeReturnCall(llvm::Module &module, llvm::CallInst *call_inst) {
  // i686 and x86_64 returns for large vectors in the RenderScript API are not
  // handled as normal register pairs, but as a hidden sret type. This is not
  // reflected in the debug info or mangled symbol name, and the android ABI
  // for x86 and x86_64, (as well as the emulators) specifies there is no AVX,
  // so bcc generates an sret function because we cannot natively return
  // 256 bit vectors.
  // This function simply checks whether a function has a > 128bit return type.
  // It is perhaps an unreliable heuristic, and relies on bcc not generating
  // AVX code, so if the android ABI one day provides for AVX, this function
  // may go out of fashion.
  (void)module;
  if (!call_inst || !call_inst->getCalledFunction())
    return false;

  return call_inst->getCalledFunction()
             ->getReturnType()
             ->getPrimitiveSizeInBits() > 128;
}

bool isRSAllocationPtrTy(const llvm::Type *type) {
  if (!type->isPointerTy())
    return false;
  auto ptr_type = type->getPointerElementType();

  return ptr_type->isStructTy() &&
         ptr_type->getStructName().startswith("struct.rs_allocation");
}

bool isRSAllocationTyCallSite(llvm::Module &module, llvm::CallInst *call_inst) {
  (void)module;
  if (!call_inst->hasByValArgument())
    return false;
  for (const auto &param : call_inst->operand_values())
    if (isRSAllocationPtrTy(param->getType()))
      return true;
  return false;
}

llvm::FunctionType *cloneToStructRetFnTy(llvm::CallInst *call_inst) {
  // on x86 StructReturn functions return a pointer to the return value, rather
  // than the return value itself
  // [ref](http://www.agner.org/optimize/calling_conventions.pdf section 6). We
  // create a return type by getting the pointer type of the old return type,
  // and inserting a new initial argument of pointer type of the original
  // return type.
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_LANGUAGE | LIBLLDB_LOG_EXPRESSIONS));

  assert(call_inst && "no CallInst");
  llvm::Function *orig = call_inst->getCalledFunction();
  assert(orig && "CallInst has no called function");
  llvm::FunctionType *orig_type = orig->getFunctionType();
  auto name = orig->getName();
  if (log)
    log->Printf("%s - cloning to StructRet function for '%s'", __FUNCTION__,
                name.str().c_str());

  unsigned num_params = orig_type->getNumParams();
  std::vector<llvm::Type *> new_params{num_params + 1, nullptr};
  std::vector<llvm::Type *> params{orig_type->param_begin(),
                                   orig_type->param_end()};

  // This may not work if the function is somehow declared void as llvm is
  // strongly typed and represents void* with i8*
  assert(!orig_type->getReturnType()->isVoidTy() &&
         "Cannot add StructRet attribute to void function");
  llvm::PointerType *return_type_ptr_type =
      llvm::PointerType::getUnqual(orig->getReturnType());
  assert(return_type_ptr_type &&
         "failed to get function return type PointerType");
  if (!return_type_ptr_type)
    return nullptr;

  if (log)
    log->Printf("%s - return type pointer type for StructRet clone @ '0x%p':\n",
                __FUNCTION__, (void *)return_type_ptr_type);
  // put the sret pointer argument in place at the beginning of the
  // argument list.
  params.emplace(params.begin(), return_type_ptr_type);
  assert(params.size() == num_params + 1);
  return llvm::FunctionType::get(return_type_ptr_type, params,
                                 orig->isVarArg());
}

bool findRSCallSites(llvm::Module &module,
                     std::set<llvm::CallInst *> &rs_callsites,
                     bool (*predicate)(llvm::Module &, llvm::CallInst *)) {
  bool found = false;

  for (auto &func : module.getFunctionList())
    for (auto &block : func.getBasicBlockList())
      for (auto &inst : block) {
        llvm::CallInst *call_inst =
            llvm::dyn_cast_or_null<llvm::CallInst>(&inst);
        if (!call_inst || !call_inst->getCalledFunction())
          // This is not the call-site you are looking for...
          continue;
        if (isRSAPICall(module, call_inst) && predicate(module, call_inst)) {
          rs_callsites.insert(call_inst);
          found = true;
        }
      }
  return found;
}

bool fixupX86StructRetCalls(llvm::Module &module) {
  bool changed = false;
  // changing a basic block while iterating over it seems to have some
  // undefined behaviour going on so we find all RS callsites first, then fix
  // them up after consuming the iterator.
  std::set<llvm::CallInst *> rs_callsites;
  if (!findRSCallSites(module, rs_callsites, isRSLargeReturnCall))
    return false;

  for (auto call_inst : rs_callsites) {
    llvm::FunctionType *new_func_type = cloneToStructRetFnTy(call_inst);
    assert(new_func_type &&
           "failed to clone functionType for Renderscript ABI fixup");

    llvm::CallSite call_site(call_inst);
    llvm::Function *func = call_inst->getCalledFunction();
    assert(func && "cannot resolve function in RenderScriptRuntime");
    // Copy the original call arguments
    std::vector<llvm::Value *> new_call_args(call_site.arg_begin(),
                                             call_site.arg_end());

    // Allocate enough space to store the return value of the original function
    // we pass a pointer to this allocation as the StructRet param, and then
    // copy its value into the lldb return value
    const llvm::DataLayout &DL = module.getDataLayout();
    llvm::AllocaInst *return_value_alloc = new llvm::AllocaInst(
      func->getReturnType(), DL.getAllocaAddrSpace(), "var_vector_return_alloc",
      call_inst);
    // use the new allocation as the new first argument
    new_call_args.emplace(new_call_args.begin(),
                          llvm::cast<llvm::Value>(return_value_alloc));
    llvm::PointerType *new_func_ptr_type =
        llvm::PointerType::get(new_func_type, 0);
    // Create the type cast from the old function type to the new one
    llvm::Constant *new_func_cast = llvm::ConstantExpr::getCast(
        llvm::Instruction::BitCast, func, new_func_ptr_type);
    // create an allocation for a new function pointer
    llvm::AllocaInst *new_func_ptr =
        new llvm::AllocaInst(new_func_ptr_type, DL.getAllocaAddrSpace(),
                             "new_func_ptr", call_inst);
    // store the new_func_cast to the newly allocated space
    (new llvm::StoreInst(new_func_cast, new_func_ptr, call_inst))
        ->setName("new_func_ptr_load_cast");
    // load the new function address ready for a jump
    llvm::LoadInst *new_func_addr_load =
        new llvm::LoadInst(new_func_ptr, "load_func_pointer", call_inst);
    // and create a callinstruction from it
    llvm::CallInst *new_call_inst = llvm::CallInst::Create(
        new_func_addr_load, new_call_args, "new_func_call", call_inst);
    new_call_inst->setCallingConv(call_inst->getCallingConv());
    new_call_inst->setTailCall(call_inst->isTailCall());
    llvm::LoadInst *lldb_save_result_address =
        new llvm::LoadInst(return_value_alloc, "save_return_val", call_inst);

    // Now remove the old broken call
    call_inst->replaceAllUsesWith(lldb_save_result_address);
    call_inst->eraseFromParent();
    changed = true;
  }
  return changed;
}

bool fixupRSAllocationStructByValCalls(llvm::Module &module) {
  // On x86_64, calls to functions in the RS runtime that take an
  // `rs_allocation` type argument are actually handled as by-ref params by
  // bcc, but appear to be passed by value by lldb (the callsite all use
  // `struct byval`). On x86_64 Linux, struct arguments are transferred in
  // registers if the struct size is no bigger than 128bits
  // [ref](http://www.agner.org/optimize/calling_conventions.pdf) section 7.1
  // "Passing and returning objects" otherwise passed on the stack. an object
  // of type `rs_allocation` is actually 256bits, so should be passed on the
  // stack. However, code generated by bcc actually treats formal params of
  // type `rs_allocation` as `rs_allocation *` so we need to convert the
  // calling convention to pass by reference, and remove any hint of byval from
  // formal parameters.
  bool changed = false;
  std::set<llvm::CallInst *> rs_callsites;
  if (!findRSCallSites(module, rs_callsites, isRSAllocationTyCallSite))
    return false;

  std::set<llvm::Function *> rs_functions;

  // for all call instructions
  for (auto call_inst : rs_callsites) {
    // add the called function to a set so that we can strip its byval
    // attributes in another pass
    rs_functions.insert(call_inst->getCalledFunction());

    // get the function attributes
    llvm::AttributeList call_attribs = call_inst->getAttributes();

    // iterate over the argument attributes
    for (unsigned I = call_attribs.index_begin(); I != call_attribs.index_end();
         I++) {
      // if this argument is passed by val
      if (call_attribs.hasAttribute(I, llvm::Attribute::ByVal)) {
        // strip away the byval attribute
        call_inst->removeAttribute(I, llvm::Attribute::ByVal);
        changed = true;
      }
    }
  }

  // for all called function decls
  for (auto func : rs_functions) {
    // inspect all of the arguments in the call
    for (auto &arg : func->args()) {
      if (arg.hasByValAttr()) {
        arg.removeAttr(llvm::Attribute::ByVal);
        changed = true;
      }
    }
  }
  return changed;
}
} // end anonymous namespace

namespace lldb_private {
namespace lldb_renderscript {

bool fixupX86FunctionCalls(llvm::Module &module) {
  return fixupX86StructRetCalls(module);
}

bool fixupX86_64FunctionCalls(llvm::Module &module) {
  bool changed = false;
  changed |= fixupX86StructRetCalls(module);
  changed |= fixupRSAllocationStructByValCalls(module);
  return changed;
}

} // end namespace lldb_renderscript
} // end namespace lldb_private
