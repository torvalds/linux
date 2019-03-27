//===----- CGCUDANV.cpp - Interface to NVIDIA CUDA Runtime ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides a class for CUDA code generation targeting the NVIDIA CUDA
// runtime library.
//
//===----------------------------------------------------------------------===//

#include "CGCUDARuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/Decl.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Format.h"

using namespace clang;
using namespace CodeGen;

namespace {
constexpr unsigned CudaFatMagic = 0x466243b1;
constexpr unsigned HIPFatMagic = 0x48495046; // "HIPF"

class CGNVCUDARuntime : public CGCUDARuntime {

private:
  llvm::IntegerType *IntTy, *SizeTy;
  llvm::Type *VoidTy;
  llvm::PointerType *CharPtrTy, *VoidPtrTy, *VoidPtrPtrTy;

  /// Convenience reference to LLVM Context
  llvm::LLVMContext &Context;
  /// Convenience reference to the current module
  llvm::Module &TheModule;
  /// Keeps track of kernel launch stubs emitted in this module
  llvm::SmallVector<llvm::Function *, 16> EmittedKernels;
  llvm::SmallVector<std::pair<llvm::GlobalVariable *, unsigned>, 16> DeviceVars;
  /// Keeps track of variable containing handle of GPU binary. Populated by
  /// ModuleCtorFunction() and used to create corresponding cleanup calls in
  /// ModuleDtorFunction()
  llvm::GlobalVariable *GpuBinaryHandle = nullptr;
  /// Whether we generate relocatable device code.
  bool RelocatableDeviceCode;

  llvm::Constant *getSetupArgumentFn() const;
  llvm::Constant *getLaunchFn() const;

  llvm::FunctionType *getRegisterGlobalsFnTy() const;
  llvm::FunctionType *getCallbackFnTy() const;
  llvm::FunctionType *getRegisterLinkedBinaryFnTy() const;
  std::string addPrefixToName(StringRef FuncName) const;
  std::string addUnderscoredPrefixToName(StringRef FuncName) const;

  /// Creates a function to register all kernel stubs generated in this module.
  llvm::Function *makeRegisterGlobalsFn();

  /// Helper function that generates a constant string and returns a pointer to
  /// the start of the string.  The result of this function can be used anywhere
  /// where the C code specifies const char*.
  llvm::Constant *makeConstantString(const std::string &Str,
                                     const std::string &Name = "",
                                     const std::string &SectionName = "",
                                     unsigned Alignment = 0) {
    llvm::Constant *Zeros[] = {llvm::ConstantInt::get(SizeTy, 0),
                               llvm::ConstantInt::get(SizeTy, 0)};
    auto ConstStr = CGM.GetAddrOfConstantCString(Str, Name.c_str());
    llvm::GlobalVariable *GV =
        cast<llvm::GlobalVariable>(ConstStr.getPointer());
    if (!SectionName.empty()) {
      GV->setSection(SectionName);
      // Mark the address as used which make sure that this section isn't
      // merged and we will really have it in the object file.
      GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
    }
    if (Alignment)
      GV->setAlignment(Alignment);

    return llvm::ConstantExpr::getGetElementPtr(ConstStr.getElementType(),
                                                ConstStr.getPointer(), Zeros);
  }

  /// Helper function that generates an empty dummy function returning void.
  llvm::Function *makeDummyFunction(llvm::FunctionType *FnTy) {
    assert(FnTy->getReturnType()->isVoidTy() &&
           "Can only generate dummy functions returning void!");
    llvm::Function *DummyFunc = llvm::Function::Create(
        FnTy, llvm::GlobalValue::InternalLinkage, "dummy", &TheModule);

    llvm::BasicBlock *DummyBlock =
        llvm::BasicBlock::Create(Context, "", DummyFunc);
    CGBuilderTy FuncBuilder(CGM, Context);
    FuncBuilder.SetInsertPoint(DummyBlock);
    FuncBuilder.CreateRetVoid();

    return DummyFunc;
  }

  void emitDeviceStubBody(CodeGenFunction &CGF, FunctionArgList &Args);

public:
  CGNVCUDARuntime(CodeGenModule &CGM);

  void emitDeviceStub(CodeGenFunction &CGF, FunctionArgList &Args) override;
  void registerDeviceVar(llvm::GlobalVariable &Var, unsigned Flags) override {
    DeviceVars.push_back(std::make_pair(&Var, Flags));
  }

  /// Creates module constructor function
  llvm::Function *makeModuleCtorFunction() override;
  /// Creates module destructor function
  llvm::Function *makeModuleDtorFunction() override;
};

}

std::string CGNVCUDARuntime::addPrefixToName(StringRef FuncName) const {
  if (CGM.getLangOpts().HIP)
    return ((Twine("hip") + Twine(FuncName)).str());
  return ((Twine("cuda") + Twine(FuncName)).str());
}
std::string
CGNVCUDARuntime::addUnderscoredPrefixToName(StringRef FuncName) const {
  if (CGM.getLangOpts().HIP)
    return ((Twine("__hip") + Twine(FuncName)).str());
  return ((Twine("__cuda") + Twine(FuncName)).str());
}

CGNVCUDARuntime::CGNVCUDARuntime(CodeGenModule &CGM)
    : CGCUDARuntime(CGM), Context(CGM.getLLVMContext()),
      TheModule(CGM.getModule()),
      RelocatableDeviceCode(CGM.getLangOpts().GPURelocatableDeviceCode) {
  CodeGen::CodeGenTypes &Types = CGM.getTypes();
  ASTContext &Ctx = CGM.getContext();

  IntTy = CGM.IntTy;
  SizeTy = CGM.SizeTy;
  VoidTy = CGM.VoidTy;

  CharPtrTy = llvm::PointerType::getUnqual(Types.ConvertType(Ctx.CharTy));
  VoidPtrTy = cast<llvm::PointerType>(Types.ConvertType(Ctx.VoidPtrTy));
  VoidPtrPtrTy = VoidPtrTy->getPointerTo();
}

llvm::Constant *CGNVCUDARuntime::getSetupArgumentFn() const {
  // cudaError_t cudaSetupArgument(void *, size_t, size_t)
  llvm::Type *Params[] = {VoidPtrTy, SizeTy, SizeTy};
  return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(IntTy, Params, false),
      addPrefixToName("SetupArgument"));
}

llvm::Constant *CGNVCUDARuntime::getLaunchFn() const {
  if (CGM.getLangOpts().HIP) {
    // hipError_t hipLaunchByPtr(char *);
    return CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(IntTy, CharPtrTy, false), "hipLaunchByPtr");
  } else {
    // cudaError_t cudaLaunch(char *);
    return CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(IntTy, CharPtrTy, false), "cudaLaunch");
  }
}

llvm::FunctionType *CGNVCUDARuntime::getRegisterGlobalsFnTy() const {
  return llvm::FunctionType::get(VoidTy, VoidPtrPtrTy, false);
}

llvm::FunctionType *CGNVCUDARuntime::getCallbackFnTy() const {
  return llvm::FunctionType::get(VoidTy, VoidPtrTy, false);
}

llvm::FunctionType *CGNVCUDARuntime::getRegisterLinkedBinaryFnTy() const {
  auto CallbackFnTy = getCallbackFnTy();
  auto RegisterGlobalsFnTy = getRegisterGlobalsFnTy();
  llvm::Type *Params[] = {RegisterGlobalsFnTy->getPointerTo(), VoidPtrTy,
                          VoidPtrTy, CallbackFnTy->getPointerTo()};
  return llvm::FunctionType::get(VoidTy, Params, false);
}

void CGNVCUDARuntime::emitDeviceStub(CodeGenFunction &CGF,
                                     FunctionArgList &Args) {
  EmittedKernels.push_back(CGF.CurFn);
  emitDeviceStubBody(CGF, Args);
}

void CGNVCUDARuntime::emitDeviceStubBody(CodeGenFunction &CGF,
                                         FunctionArgList &Args) {
  // Emit a call to cudaSetupArgument for each arg in Args.
  llvm::Constant *cudaSetupArgFn = getSetupArgumentFn();
  llvm::BasicBlock *EndBlock = CGF.createBasicBlock("setup.end");
  CharUnits Offset = CharUnits::Zero();
  for (const VarDecl *A : Args) {
    CharUnits TyWidth, TyAlign;
    std::tie(TyWidth, TyAlign) =
        CGM.getContext().getTypeInfoInChars(A->getType());
    Offset = Offset.alignTo(TyAlign);
    llvm::Value *Args[] = {
        CGF.Builder.CreatePointerCast(CGF.GetAddrOfLocalVar(A).getPointer(),
                                      VoidPtrTy),
        llvm::ConstantInt::get(SizeTy, TyWidth.getQuantity()),
        llvm::ConstantInt::get(SizeTy, Offset.getQuantity()),
    };
    llvm::CallSite CS = CGF.EmitRuntimeCallOrInvoke(cudaSetupArgFn, Args);
    llvm::Constant *Zero = llvm::ConstantInt::get(IntTy, 0);
    llvm::Value *CSZero = CGF.Builder.CreateICmpEQ(CS.getInstruction(), Zero);
    llvm::BasicBlock *NextBlock = CGF.createBasicBlock("setup.next");
    CGF.Builder.CreateCondBr(CSZero, NextBlock, EndBlock);
    CGF.EmitBlock(NextBlock);
    Offset += TyWidth;
  }

  // Emit the call to cudaLaunch
  llvm::Constant *cudaLaunchFn = getLaunchFn();
  llvm::Value *Arg = CGF.Builder.CreatePointerCast(CGF.CurFn, CharPtrTy);
  CGF.EmitRuntimeCallOrInvoke(cudaLaunchFn, Arg);
  CGF.EmitBranch(EndBlock);

  CGF.EmitBlock(EndBlock);
}

/// Creates a function that sets up state on the host side for CUDA objects that
/// have a presence on both the host and device sides. Specifically, registers
/// the host side of kernel functions and device global variables with the CUDA
/// runtime.
/// \code
/// void __cuda_register_globals(void** GpuBinaryHandle) {
///    __cudaRegisterFunction(GpuBinaryHandle,Kernel0,...);
///    ...
///    __cudaRegisterFunction(GpuBinaryHandle,KernelM,...);
///    __cudaRegisterVar(GpuBinaryHandle, GlobalVar0, ...);
///    ...
///    __cudaRegisterVar(GpuBinaryHandle, GlobalVarN, ...);
/// }
/// \endcode
llvm::Function *CGNVCUDARuntime::makeRegisterGlobalsFn() {
  // No need to register anything
  if (EmittedKernels.empty() && DeviceVars.empty())
    return nullptr;

  llvm::Function *RegisterKernelsFunc = llvm::Function::Create(
      getRegisterGlobalsFnTy(), llvm::GlobalValue::InternalLinkage,
      addUnderscoredPrefixToName("_register_globals"), &TheModule);
  llvm::BasicBlock *EntryBB =
      llvm::BasicBlock::Create(Context, "entry", RegisterKernelsFunc);
  CGBuilderTy Builder(CGM, Context);
  Builder.SetInsertPoint(EntryBB);

  // void __cudaRegisterFunction(void **, const char *, char *, const char *,
  //                             int, uint3*, uint3*, dim3*, dim3*, int*)
  llvm::Type *RegisterFuncParams[] = {
      VoidPtrPtrTy, CharPtrTy, CharPtrTy, CharPtrTy, IntTy,
      VoidPtrTy,    VoidPtrTy, VoidPtrTy, VoidPtrTy, IntTy->getPointerTo()};
  llvm::Constant *RegisterFunc = CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(IntTy, RegisterFuncParams, false),
      addUnderscoredPrefixToName("RegisterFunction"));

  // Extract GpuBinaryHandle passed as the first argument passed to
  // __cuda_register_globals() and generate __cudaRegisterFunction() call for
  // each emitted kernel.
  llvm::Argument &GpuBinaryHandlePtr = *RegisterKernelsFunc->arg_begin();
  for (llvm::Function *Kernel : EmittedKernels) {
    llvm::Constant *KernelName = makeConstantString(Kernel->getName());
    llvm::Constant *NullPtr = llvm::ConstantPointerNull::get(VoidPtrTy);
    llvm::Value *Args[] = {
        &GpuBinaryHandlePtr, Builder.CreateBitCast(Kernel, VoidPtrTy),
        KernelName, KernelName, llvm::ConstantInt::get(IntTy, -1), NullPtr,
        NullPtr, NullPtr, NullPtr,
        llvm::ConstantPointerNull::get(IntTy->getPointerTo())};
    Builder.CreateCall(RegisterFunc, Args);
  }

  // void __cudaRegisterVar(void **, char *, char *, const char *,
  //                        int, int, int, int)
  llvm::Type *RegisterVarParams[] = {VoidPtrPtrTy, CharPtrTy, CharPtrTy,
                                     CharPtrTy,    IntTy,     IntTy,
                                     IntTy,        IntTy};
  llvm::Constant *RegisterVar = CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(IntTy, RegisterVarParams, false),
      addUnderscoredPrefixToName("RegisterVar"));
  for (auto &Pair : DeviceVars) {
    llvm::GlobalVariable *Var = Pair.first;
    unsigned Flags = Pair.second;
    llvm::Constant *VarName = makeConstantString(Var->getName());
    uint64_t VarSize =
        CGM.getDataLayout().getTypeAllocSize(Var->getValueType());
    llvm::Value *Args[] = {
        &GpuBinaryHandlePtr,
        Builder.CreateBitCast(Var, VoidPtrTy),
        VarName,
        VarName,
        llvm::ConstantInt::get(IntTy, (Flags & ExternDeviceVar) ? 1 : 0),
        llvm::ConstantInt::get(IntTy, VarSize),
        llvm::ConstantInt::get(IntTy, (Flags & ConstantDeviceVar) ? 1 : 0),
        llvm::ConstantInt::get(IntTy, 0)};
    Builder.CreateCall(RegisterVar, Args);
  }

  Builder.CreateRetVoid();
  return RegisterKernelsFunc;
}

/// Creates a global constructor function for the module:
///
/// For CUDA:
/// \code
/// void __cuda_module_ctor(void*) {
///     Handle = __cudaRegisterFatBinary(GpuBinaryBlob);
///     __cuda_register_globals(Handle);
/// }
/// \endcode
///
/// For HIP:
/// \code
/// void __hip_module_ctor(void*) {
///     if (__hip_gpubin_handle == 0) {
///         __hip_gpubin_handle  = __hipRegisterFatBinary(GpuBinaryBlob);
///         __hip_register_globals(__hip_gpubin_handle);
///     }
/// }
/// \endcode
llvm::Function *CGNVCUDARuntime::makeModuleCtorFunction() {
  bool IsHIP = CGM.getLangOpts().HIP;
  // No need to generate ctors/dtors if there is no GPU binary.
  StringRef CudaGpuBinaryFileName = CGM.getCodeGenOpts().CudaGpuBinaryFileName;
  if (CudaGpuBinaryFileName.empty() && !IsHIP)
    return nullptr;

  // void __{cuda|hip}_register_globals(void* handle);
  llvm::Function *RegisterGlobalsFunc = makeRegisterGlobalsFn();
  // We always need a function to pass in as callback. Create a dummy
  // implementation if we don't need to register anything.
  if (RelocatableDeviceCode && !RegisterGlobalsFunc)
    RegisterGlobalsFunc = makeDummyFunction(getRegisterGlobalsFnTy());

  // void ** __{cuda|hip}RegisterFatBinary(void *);
  llvm::Constant *RegisterFatbinFunc = CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(VoidPtrPtrTy, VoidPtrTy, false),
      addUnderscoredPrefixToName("RegisterFatBinary"));
  // struct { int magic, int version, void * gpu_binary, void * dont_care };
  llvm::StructType *FatbinWrapperTy =
      llvm::StructType::get(IntTy, IntTy, VoidPtrTy, VoidPtrTy);

  // Register GPU binary with the CUDA runtime, store returned handle in a
  // global variable and save a reference in GpuBinaryHandle to be cleaned up
  // in destructor on exit. Then associate all known kernels with the GPU binary
  // handle so CUDA runtime can figure out what to call on the GPU side.
  std::unique_ptr<llvm::MemoryBuffer> CudaGpuBinary = nullptr;
  if (!CudaGpuBinaryFileName.empty()) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> CudaGpuBinaryOrErr =
        llvm::MemoryBuffer::getFileOrSTDIN(CudaGpuBinaryFileName);
    if (std::error_code EC = CudaGpuBinaryOrErr.getError()) {
      CGM.getDiags().Report(diag::err_cannot_open_file)
          << CudaGpuBinaryFileName << EC.message();
      return nullptr;
    }
    CudaGpuBinary = std::move(CudaGpuBinaryOrErr.get());
  }

  llvm::Function *ModuleCtorFunc = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, VoidPtrTy, false),
      llvm::GlobalValue::InternalLinkage,
      addUnderscoredPrefixToName("_module_ctor"), &TheModule);
  llvm::BasicBlock *CtorEntryBB =
      llvm::BasicBlock::Create(Context, "entry", ModuleCtorFunc);
  CGBuilderTy CtorBuilder(CGM, Context);

  CtorBuilder.SetInsertPoint(CtorEntryBB);

  const char *FatbinConstantName;
  const char *FatbinSectionName;
  const char *ModuleIDSectionName;
  StringRef ModuleIDPrefix;
  llvm::Constant *FatBinStr;
  unsigned FatMagic;
  if (IsHIP) {
    FatbinConstantName = ".hip_fatbin";
    FatbinSectionName = ".hipFatBinSegment";

    ModuleIDSectionName = "__hip_module_id";
    ModuleIDPrefix = "__hip_";

    if (CudaGpuBinary) {
      // If fatbin is available from early finalization, create a string
      // literal containing the fat binary loaded from the given file.
      FatBinStr = makeConstantString(CudaGpuBinary->getBuffer(), "",
                                     FatbinConstantName, 8);
    } else {
      // If fatbin is not available, create an external symbol
      // __hip_fatbin in section .hip_fatbin. The external symbol is supposed
      // to contain the fat binary but will be populated somewhere else,
      // e.g. by lld through link script.
      FatBinStr = new llvm::GlobalVariable(
        CGM.getModule(), CGM.Int8Ty,
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage, nullptr,
        "__hip_fatbin", nullptr,
        llvm::GlobalVariable::NotThreadLocal);
      cast<llvm::GlobalVariable>(FatBinStr)->setSection(FatbinConstantName);
    }

    FatMagic = HIPFatMagic;
  } else {
    if (RelocatableDeviceCode)
      FatbinConstantName = CGM.getTriple().isMacOSX()
                               ? "__NV_CUDA,__nv_relfatbin"
                               : "__nv_relfatbin";
    else
      FatbinConstantName =
          CGM.getTriple().isMacOSX() ? "__NV_CUDA,__nv_fatbin" : ".nv_fatbin";
    // NVIDIA's cuobjdump looks for fatbins in this section.
    FatbinSectionName =
        CGM.getTriple().isMacOSX() ? "__NV_CUDA,__fatbin" : ".nvFatBinSegment";

    ModuleIDSectionName = CGM.getTriple().isMacOSX()
                              ? "__NV_CUDA,__nv_module_id"
                              : "__nv_module_id";
    ModuleIDPrefix = "__nv_";

    // For CUDA, create a string literal containing the fat binary loaded from
    // the given file.
    FatBinStr = makeConstantString(CudaGpuBinary->getBuffer(), "",
                                   FatbinConstantName, 8);
    FatMagic = CudaFatMagic;
  }

  // Create initialized wrapper structure that points to the loaded GPU binary
  ConstantInitBuilder Builder(CGM);
  auto Values = Builder.beginStruct(FatbinWrapperTy);
  // Fatbin wrapper magic.
  Values.addInt(IntTy, FatMagic);
  // Fatbin version.
  Values.addInt(IntTy, 1);
  // Data.
  Values.add(FatBinStr);
  // Unused in fatbin v1.
  Values.add(llvm::ConstantPointerNull::get(VoidPtrTy));
  llvm::GlobalVariable *FatbinWrapper = Values.finishAndCreateGlobal(
      addUnderscoredPrefixToName("_fatbin_wrapper"), CGM.getPointerAlign(),
      /*constant*/ true);
  FatbinWrapper->setSection(FatbinSectionName);

  // There is only one HIP fat binary per linked module, however there are
  // multiple constructor functions. Make sure the fat binary is registered
  // only once. The constructor functions are executed by the dynamic loader
  // before the program gains control. The dynamic loader cannot execute the
  // constructor functions concurrently since doing that would not guarantee
  // thread safety of the loaded program. Therefore we can assume sequential
  // execution of constructor functions here.
  if (IsHIP) {
    auto Linkage = CudaGpuBinary ? llvm::GlobalValue::InternalLinkage :
        llvm::GlobalValue::LinkOnceAnyLinkage;
    llvm::BasicBlock *IfBlock =
        llvm::BasicBlock::Create(Context, "if", ModuleCtorFunc);
    llvm::BasicBlock *ExitBlock =
        llvm::BasicBlock::Create(Context, "exit", ModuleCtorFunc);
    // The name, size, and initialization pattern of this variable is part
    // of HIP ABI.
    GpuBinaryHandle = new llvm::GlobalVariable(
        TheModule, VoidPtrPtrTy, /*isConstant=*/false,
        Linkage,
        /*Initializer=*/llvm::ConstantPointerNull::get(VoidPtrPtrTy),
        "__hip_gpubin_handle");
    GpuBinaryHandle->setAlignment(CGM.getPointerAlign().getQuantity());
    // Prevent the weak symbol in different shared libraries being merged.
    if (Linkage != llvm::GlobalValue::InternalLinkage)
      GpuBinaryHandle->setVisibility(llvm::GlobalValue::HiddenVisibility);
    Address GpuBinaryAddr(
        GpuBinaryHandle,
        CharUnits::fromQuantity(GpuBinaryHandle->getAlignment()));
    {
      auto HandleValue = CtorBuilder.CreateLoad(GpuBinaryAddr);
      llvm::Constant *Zero =
          llvm::Constant::getNullValue(HandleValue->getType());
      llvm::Value *EQZero = CtorBuilder.CreateICmpEQ(HandleValue, Zero);
      CtorBuilder.CreateCondBr(EQZero, IfBlock, ExitBlock);
    }
    {
      CtorBuilder.SetInsertPoint(IfBlock);
      // GpuBinaryHandle = __hipRegisterFatBinary(&FatbinWrapper);
      llvm::CallInst *RegisterFatbinCall = CtorBuilder.CreateCall(
          RegisterFatbinFunc,
          CtorBuilder.CreateBitCast(FatbinWrapper, VoidPtrTy));
      CtorBuilder.CreateStore(RegisterFatbinCall, GpuBinaryAddr);
      CtorBuilder.CreateBr(ExitBlock);
    }
    {
      CtorBuilder.SetInsertPoint(ExitBlock);
      // Call __hip_register_globals(GpuBinaryHandle);
      if (RegisterGlobalsFunc) {
        auto HandleValue = CtorBuilder.CreateLoad(GpuBinaryAddr);
        CtorBuilder.CreateCall(RegisterGlobalsFunc, HandleValue);
      }
    }
  } else if (!RelocatableDeviceCode) {
    // Register binary with CUDA runtime. This is substantially different in
    // default mode vs. separate compilation!
    // GpuBinaryHandle = __cudaRegisterFatBinary(&FatbinWrapper);
    llvm::CallInst *RegisterFatbinCall = CtorBuilder.CreateCall(
        RegisterFatbinFunc,
        CtorBuilder.CreateBitCast(FatbinWrapper, VoidPtrTy));
    GpuBinaryHandle = new llvm::GlobalVariable(
        TheModule, VoidPtrPtrTy, false, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantPointerNull::get(VoidPtrPtrTy), "__cuda_gpubin_handle");
    GpuBinaryHandle->setAlignment(CGM.getPointerAlign().getQuantity());
    CtorBuilder.CreateAlignedStore(RegisterFatbinCall, GpuBinaryHandle,
                                   CGM.getPointerAlign());

    // Call __cuda_register_globals(GpuBinaryHandle);
    if (RegisterGlobalsFunc)
      CtorBuilder.CreateCall(RegisterGlobalsFunc, RegisterFatbinCall);
  } else {
    // Generate a unique module ID.
    SmallString<64> ModuleID;
    llvm::raw_svector_ostream OS(ModuleID);
    OS << ModuleIDPrefix << llvm::format("%" PRIx64, FatbinWrapper->getGUID());
    llvm::Constant *ModuleIDConstant =
        makeConstantString(ModuleID.str(), "", ModuleIDSectionName, 32);

    // Create an alias for the FatbinWrapper that nvcc will look for.
    llvm::GlobalAlias::create(llvm::GlobalValue::ExternalLinkage,
                              Twine("__fatbinwrap") + ModuleID, FatbinWrapper);

    // void __cudaRegisterLinkedBinary%ModuleID%(void (*)(void *), void *,
    // void *, void (*)(void **))
    SmallString<128> RegisterLinkedBinaryName("__cudaRegisterLinkedBinary");
    RegisterLinkedBinaryName += ModuleID;
    llvm::Constant *RegisterLinkedBinaryFunc = CGM.CreateRuntimeFunction(
        getRegisterLinkedBinaryFnTy(), RegisterLinkedBinaryName);

    assert(RegisterGlobalsFunc && "Expecting at least dummy function!");
    llvm::Value *Args[] = {RegisterGlobalsFunc,
                           CtorBuilder.CreateBitCast(FatbinWrapper, VoidPtrTy),
                           ModuleIDConstant,
                           makeDummyFunction(getCallbackFnTy())};
    CtorBuilder.CreateCall(RegisterLinkedBinaryFunc, Args);
  }

  // Create destructor and register it with atexit() the way NVCC does it. Doing
  // it during regular destructor phase worked in CUDA before 9.2 but results in
  // double-free in 9.2.
  if (llvm::Function *CleanupFn = makeModuleDtorFunction()) {
    // extern "C" int atexit(void (*f)(void));
    llvm::FunctionType *AtExitTy =
        llvm::FunctionType::get(IntTy, CleanupFn->getType(), false);
    llvm::Constant *AtExitFunc =
        CGM.CreateRuntimeFunction(AtExitTy, "atexit", llvm::AttributeList(),
                                  /*Local=*/true);
    CtorBuilder.CreateCall(AtExitFunc, CleanupFn);
  }

  CtorBuilder.CreateRetVoid();
  return ModuleCtorFunc;
}

/// Creates a global destructor function that unregisters the GPU code blob
/// registered by constructor.
///
/// For CUDA:
/// \code
/// void __cuda_module_dtor(void*) {
///     __cudaUnregisterFatBinary(Handle);
/// }
/// \endcode
///
/// For HIP:
/// \code
/// void __hip_module_dtor(void*) {
///     if (__hip_gpubin_handle) {
///         __hipUnregisterFatBinary(__hip_gpubin_handle);
///         __hip_gpubin_handle = 0;
///     }
/// }
/// \endcode
llvm::Function *CGNVCUDARuntime::makeModuleDtorFunction() {
  // No need for destructor if we don't have a handle to unregister.
  if (!GpuBinaryHandle)
    return nullptr;

  // void __cudaUnregisterFatBinary(void ** handle);
  llvm::Constant *UnregisterFatbinFunc = CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(VoidTy, VoidPtrPtrTy, false),
      addUnderscoredPrefixToName("UnregisterFatBinary"));

  llvm::Function *ModuleDtorFunc = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, VoidPtrTy, false),
      llvm::GlobalValue::InternalLinkage,
      addUnderscoredPrefixToName("_module_dtor"), &TheModule);

  llvm::BasicBlock *DtorEntryBB =
      llvm::BasicBlock::Create(Context, "entry", ModuleDtorFunc);
  CGBuilderTy DtorBuilder(CGM, Context);
  DtorBuilder.SetInsertPoint(DtorEntryBB);

  Address GpuBinaryAddr(GpuBinaryHandle, CharUnits::fromQuantity(
                                             GpuBinaryHandle->getAlignment()));
  auto HandleValue = DtorBuilder.CreateLoad(GpuBinaryAddr);
  // There is only one HIP fat binary per linked module, however there are
  // multiple destructor functions. Make sure the fat binary is unregistered
  // only once.
  if (CGM.getLangOpts().HIP) {
    llvm::BasicBlock *IfBlock =
        llvm::BasicBlock::Create(Context, "if", ModuleDtorFunc);
    llvm::BasicBlock *ExitBlock =
        llvm::BasicBlock::Create(Context, "exit", ModuleDtorFunc);
    llvm::Constant *Zero = llvm::Constant::getNullValue(HandleValue->getType());
    llvm::Value *NEZero = DtorBuilder.CreateICmpNE(HandleValue, Zero);
    DtorBuilder.CreateCondBr(NEZero, IfBlock, ExitBlock);

    DtorBuilder.SetInsertPoint(IfBlock);
    DtorBuilder.CreateCall(UnregisterFatbinFunc, HandleValue);
    DtorBuilder.CreateStore(Zero, GpuBinaryAddr);
    DtorBuilder.CreateBr(ExitBlock);

    DtorBuilder.SetInsertPoint(ExitBlock);
  } else {
    DtorBuilder.CreateCall(UnregisterFatbinFunc, HandleValue);
  }
  DtorBuilder.CreateRetVoid();
  return ModuleDtorFunc;
}

CGCUDARuntime *CodeGen::CreateNVCUDARuntime(CodeGenModule &CGM) {
  return new CGNVCUDARuntime(CGM);
}
