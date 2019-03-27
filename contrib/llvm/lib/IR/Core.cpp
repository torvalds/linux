//===-- Core.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the common infrastructure (including the C bindings)
// for libLLVMCore.a, which implements the LLVM intermediate representation.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <system_error>

using namespace llvm;

#define DEBUG_TYPE "ir"

void llvm::initializeCore(PassRegistry &Registry) {
  initializeDominatorTreeWrapperPassPass(Registry);
  initializePrintModulePassWrapperPass(Registry);
  initializePrintFunctionPassWrapperPass(Registry);
  initializePrintBasicBlockPassPass(Registry);
  initializeSafepointIRVerifierPass(Registry);
  initializeVerifierLegacyPassPass(Registry);
}

void LLVMInitializeCore(LLVMPassRegistryRef R) {
  initializeCore(*unwrap(R));
}

void LLVMShutdown() {
  llvm_shutdown();
}

/*===-- Error handling ----------------------------------------------------===*/

char *LLVMCreateMessage(const char *Message) {
  return strdup(Message);
}

void LLVMDisposeMessage(char *Message) {
  free(Message);
}


/*===-- Operations on contexts --------------------------------------------===*/

static ManagedStatic<LLVMContext> GlobalContext;

LLVMContextRef LLVMContextCreate() {
  return wrap(new LLVMContext());
}

LLVMContextRef LLVMGetGlobalContext() { return wrap(&*GlobalContext); }

void LLVMContextSetDiagnosticHandler(LLVMContextRef C,
                                     LLVMDiagnosticHandler Handler,
                                     void *DiagnosticContext) {
  unwrap(C)->setDiagnosticHandlerCallBack(
      LLVM_EXTENSION reinterpret_cast<DiagnosticHandler::DiagnosticHandlerTy>(
          Handler),
      DiagnosticContext);
}

LLVMDiagnosticHandler LLVMContextGetDiagnosticHandler(LLVMContextRef C) {
  return LLVM_EXTENSION reinterpret_cast<LLVMDiagnosticHandler>(
      unwrap(C)->getDiagnosticHandlerCallBack());
}

void *LLVMContextGetDiagnosticContext(LLVMContextRef C) {
  return unwrap(C)->getDiagnosticContext();
}

void LLVMContextSetYieldCallback(LLVMContextRef C, LLVMYieldCallback Callback,
                                 void *OpaqueHandle) {
  auto YieldCallback =
    LLVM_EXTENSION reinterpret_cast<LLVMContext::YieldCallbackTy>(Callback);
  unwrap(C)->setYieldCallback(YieldCallback, OpaqueHandle);
}

LLVMBool LLVMContextShouldDiscardValueNames(LLVMContextRef C) {
  return unwrap(C)->shouldDiscardValueNames();
}

void LLVMContextSetDiscardValueNames(LLVMContextRef C, LLVMBool Discard) {
  unwrap(C)->setDiscardValueNames(Discard);
}

void LLVMContextDispose(LLVMContextRef C) {
  delete unwrap(C);
}

unsigned LLVMGetMDKindIDInContext(LLVMContextRef C, const char *Name,
                                  unsigned SLen) {
  return unwrap(C)->getMDKindID(StringRef(Name, SLen));
}

unsigned LLVMGetMDKindID(const char *Name, unsigned SLen) {
  return LLVMGetMDKindIDInContext(LLVMGetGlobalContext(), Name, SLen);
}

#define GET_ATTR_KIND_FROM_NAME
#include "AttributesCompatFunc.inc"

unsigned LLVMGetEnumAttributeKindForName(const char *Name, size_t SLen) {
  return getAttrKindFromName(StringRef(Name, SLen));
}

unsigned LLVMGetLastEnumAttributeKind(void) {
  return Attribute::AttrKind::EndAttrKinds;
}

LLVMAttributeRef LLVMCreateEnumAttribute(LLVMContextRef C, unsigned KindID,
                                         uint64_t Val) {
  return wrap(Attribute::get(*unwrap(C), (Attribute::AttrKind)KindID, Val));
}

unsigned LLVMGetEnumAttributeKind(LLVMAttributeRef A) {
  return unwrap(A).getKindAsEnum();
}

uint64_t LLVMGetEnumAttributeValue(LLVMAttributeRef A) {
  auto Attr = unwrap(A);
  if (Attr.isEnumAttribute())
    return 0;
  return Attr.getValueAsInt();
}

LLVMAttributeRef LLVMCreateStringAttribute(LLVMContextRef C,
                                           const char *K, unsigned KLength,
                                           const char *V, unsigned VLength) {
  return wrap(Attribute::get(*unwrap(C), StringRef(K, KLength),
                             StringRef(V, VLength)));
}

const char *LLVMGetStringAttributeKind(LLVMAttributeRef A,
                                       unsigned *Length) {
  auto S = unwrap(A).getKindAsString();
  *Length = S.size();
  return S.data();
}

const char *LLVMGetStringAttributeValue(LLVMAttributeRef A,
                                        unsigned *Length) {
  auto S = unwrap(A).getValueAsString();
  *Length = S.size();
  return S.data();
}

LLVMBool LLVMIsEnumAttribute(LLVMAttributeRef A) {
  auto Attr = unwrap(A);
  return Attr.isEnumAttribute() || Attr.isIntAttribute();
}

LLVMBool LLVMIsStringAttribute(LLVMAttributeRef A) {
  return unwrap(A).isStringAttribute();
}

char *LLVMGetDiagInfoDescription(LLVMDiagnosticInfoRef DI) {
  std::string MsgStorage;
  raw_string_ostream Stream(MsgStorage);
  DiagnosticPrinterRawOStream DP(Stream);

  unwrap(DI)->print(DP);
  Stream.flush();

  return LLVMCreateMessage(MsgStorage.c_str());
}

LLVMDiagnosticSeverity LLVMGetDiagInfoSeverity(LLVMDiagnosticInfoRef DI) {
    LLVMDiagnosticSeverity severity;

    switch(unwrap(DI)->getSeverity()) {
    default:
      severity = LLVMDSError;
      break;
    case DS_Warning:
      severity = LLVMDSWarning;
      break;
    case DS_Remark:
      severity = LLVMDSRemark;
      break;
    case DS_Note:
      severity = LLVMDSNote;
      break;
    }

    return severity;
}

/*===-- Operations on modules ---------------------------------------------===*/

LLVMModuleRef LLVMModuleCreateWithName(const char *ModuleID) {
  return wrap(new Module(ModuleID, *GlobalContext));
}

LLVMModuleRef LLVMModuleCreateWithNameInContext(const char *ModuleID,
                                                LLVMContextRef C) {
  return wrap(new Module(ModuleID, *unwrap(C)));
}

void LLVMDisposeModule(LLVMModuleRef M) {
  delete unwrap(M);
}

const char *LLVMGetModuleIdentifier(LLVMModuleRef M, size_t *Len) {
  auto &Str = unwrap(M)->getModuleIdentifier();
  *Len = Str.length();
  return Str.c_str();
}

void LLVMSetModuleIdentifier(LLVMModuleRef M, const char *Ident, size_t Len) {
  unwrap(M)->setModuleIdentifier(StringRef(Ident, Len));
}

const char *LLVMGetSourceFileName(LLVMModuleRef M, size_t *Len) {
  auto &Str = unwrap(M)->getSourceFileName();
  *Len = Str.length();
  return Str.c_str();
}

void LLVMSetSourceFileName(LLVMModuleRef M, const char *Name, size_t Len) {
  unwrap(M)->setSourceFileName(StringRef(Name, Len));
}

/*--.. Data layout .........................................................--*/
const char *LLVMGetDataLayoutStr(LLVMModuleRef M) {
  return unwrap(M)->getDataLayoutStr().c_str();
}

const char *LLVMGetDataLayout(LLVMModuleRef M) {
  return LLVMGetDataLayoutStr(M);
}

void LLVMSetDataLayout(LLVMModuleRef M, const char *DataLayoutStr) {
  unwrap(M)->setDataLayout(DataLayoutStr);
}

/*--.. Target triple .......................................................--*/
const char * LLVMGetTarget(LLVMModuleRef M) {
  return unwrap(M)->getTargetTriple().c_str();
}

void LLVMSetTarget(LLVMModuleRef M, const char *Triple) {
  unwrap(M)->setTargetTriple(Triple);
}

/*--.. Module flags ........................................................--*/
struct LLVMOpaqueModuleFlagEntry {
  LLVMModuleFlagBehavior Behavior;
  const char *Key;
  size_t KeyLen;
  LLVMMetadataRef Metadata;
};

static Module::ModFlagBehavior
map_to_llvmModFlagBehavior(LLVMModuleFlagBehavior Behavior) {
  switch (Behavior) {
  case LLVMModuleFlagBehaviorError:
    return Module::ModFlagBehavior::Error;
  case LLVMModuleFlagBehaviorWarning:
    return Module::ModFlagBehavior::Warning;
  case LLVMModuleFlagBehaviorRequire:
    return Module::ModFlagBehavior::Require;
  case LLVMModuleFlagBehaviorOverride:
    return Module::ModFlagBehavior::Override;
  case LLVMModuleFlagBehaviorAppend:
    return Module::ModFlagBehavior::Append;
  case LLVMModuleFlagBehaviorAppendUnique:
    return Module::ModFlagBehavior::AppendUnique;
  }
  llvm_unreachable("Unknown LLVMModuleFlagBehavior");
}

static LLVMModuleFlagBehavior
map_from_llvmModFlagBehavior(Module::ModFlagBehavior Behavior) {
  switch (Behavior) {
  case Module::ModFlagBehavior::Error:
    return LLVMModuleFlagBehaviorError;
  case Module::ModFlagBehavior::Warning:
    return LLVMModuleFlagBehaviorWarning;
  case Module::ModFlagBehavior::Require:
    return LLVMModuleFlagBehaviorRequire;
  case Module::ModFlagBehavior::Override:
    return LLVMModuleFlagBehaviorOverride;
  case Module::ModFlagBehavior::Append:
    return LLVMModuleFlagBehaviorAppend;
  case Module::ModFlagBehavior::AppendUnique:
    return LLVMModuleFlagBehaviorAppendUnique;
  default:
    llvm_unreachable("Unhandled Flag Behavior");
  }
}

LLVMModuleFlagEntry *LLVMCopyModuleFlagsMetadata(LLVMModuleRef M, size_t *Len) {
  SmallVector<Module::ModuleFlagEntry, 8> MFEs;
  unwrap(M)->getModuleFlagsMetadata(MFEs);

  LLVMOpaqueModuleFlagEntry *Result = static_cast<LLVMOpaqueModuleFlagEntry *>(
      safe_malloc(MFEs.size() * sizeof(LLVMOpaqueModuleFlagEntry)));
  for (unsigned i = 0; i < MFEs.size(); ++i) {
    const auto &ModuleFlag = MFEs[i];
    Result[i].Behavior = map_from_llvmModFlagBehavior(ModuleFlag.Behavior);
    Result[i].Key = ModuleFlag.Key->getString().data();
    Result[i].KeyLen = ModuleFlag.Key->getString().size();
    Result[i].Metadata = wrap(ModuleFlag.Val);
  }
  *Len = MFEs.size();
  return Result;
}

void LLVMDisposeModuleFlagsMetadata(LLVMModuleFlagEntry *Entries) {
  free(Entries);
}

LLVMModuleFlagBehavior
LLVMModuleFlagEntriesGetFlagBehavior(LLVMModuleFlagEntry *Entries,
                                     unsigned Index) {
  LLVMOpaqueModuleFlagEntry MFE =
      static_cast<LLVMOpaqueModuleFlagEntry>(Entries[Index]);
  return MFE.Behavior;
}

const char *LLVMModuleFlagEntriesGetKey(LLVMModuleFlagEntry *Entries,
                                        unsigned Index, size_t *Len) {
  LLVMOpaqueModuleFlagEntry MFE =
      static_cast<LLVMOpaqueModuleFlagEntry>(Entries[Index]);
  *Len = MFE.KeyLen;
  return MFE.Key;
}

LLVMMetadataRef LLVMModuleFlagEntriesGetMetadata(LLVMModuleFlagEntry *Entries,
                                                 unsigned Index) {
  LLVMOpaqueModuleFlagEntry MFE =
      static_cast<LLVMOpaqueModuleFlagEntry>(Entries[Index]);
  return MFE.Metadata;
}

LLVMMetadataRef LLVMGetModuleFlag(LLVMModuleRef M,
                                  const char *Key, size_t KeyLen) {
  return wrap(unwrap(M)->getModuleFlag({Key, KeyLen}));
}

void LLVMAddModuleFlag(LLVMModuleRef M, LLVMModuleFlagBehavior Behavior,
                       const char *Key, size_t KeyLen,
                       LLVMMetadataRef Val) {
  unwrap(M)->addModuleFlag(map_to_llvmModFlagBehavior(Behavior),
                           {Key, KeyLen}, unwrap(Val));
}

/*--.. Printing modules ....................................................--*/

void LLVMDumpModule(LLVMModuleRef M) {
  unwrap(M)->print(errs(), nullptr,
                   /*ShouldPreserveUseListOrder=*/false, /*IsForDebug=*/true);
}

LLVMBool LLVMPrintModuleToFile(LLVMModuleRef M, const char *Filename,
                               char **ErrorMessage) {
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::F_Text);
  if (EC) {
    *ErrorMessage = strdup(EC.message().c_str());
    return true;
  }

  unwrap(M)->print(dest, nullptr);

  dest.close();

  if (dest.has_error()) {
    std::string E = "Error printing to file: " + dest.error().message();
    *ErrorMessage = strdup(E.c_str());
    return true;
  }

  return false;
}

char *LLVMPrintModuleToString(LLVMModuleRef M) {
  std::string buf;
  raw_string_ostream os(buf);

  unwrap(M)->print(os, nullptr);
  os.flush();

  return strdup(buf.c_str());
}

/*--.. Operations on inline assembler ......................................--*/
void LLVMSetModuleInlineAsm2(LLVMModuleRef M, const char *Asm, size_t Len) {
  unwrap(M)->setModuleInlineAsm(StringRef(Asm, Len));
}

void LLVMSetModuleInlineAsm(LLVMModuleRef M, const char *Asm) {
  unwrap(M)->setModuleInlineAsm(StringRef(Asm));
}

void LLVMAppendModuleInlineAsm(LLVMModuleRef M, const char *Asm, size_t Len) {
  unwrap(M)->appendModuleInlineAsm(StringRef(Asm, Len));
}

const char *LLVMGetModuleInlineAsm(LLVMModuleRef M, size_t *Len) {
  auto &Str = unwrap(M)->getModuleInlineAsm();
  *Len = Str.length();
  return Str.c_str();
}

LLVMValueRef LLVMGetInlineAsm(LLVMTypeRef Ty,
                              char *AsmString, size_t AsmStringSize,
                              char *Constraints, size_t ConstraintsSize,
                              LLVMBool HasSideEffects, LLVMBool IsAlignStack,
                              LLVMInlineAsmDialect Dialect) {
  InlineAsm::AsmDialect AD;
  switch (Dialect) {
  case LLVMInlineAsmDialectATT:
    AD = InlineAsm::AD_ATT;
    break;
  case LLVMInlineAsmDialectIntel:
    AD = InlineAsm::AD_Intel;
    break;
  }
  return wrap(InlineAsm::get(unwrap<FunctionType>(Ty),
                             StringRef(AsmString, AsmStringSize),
                             StringRef(Constraints, ConstraintsSize),
                             HasSideEffects, IsAlignStack, AD));
}


/*--.. Operations on module contexts ......................................--*/
LLVMContextRef LLVMGetModuleContext(LLVMModuleRef M) {
  return wrap(&unwrap(M)->getContext());
}


/*===-- Operations on types -----------------------------------------------===*/

/*--.. Operations on all types (mostly) ....................................--*/

LLVMTypeKind LLVMGetTypeKind(LLVMTypeRef Ty) {
  switch (unwrap(Ty)->getTypeID()) {
  case Type::VoidTyID:
    return LLVMVoidTypeKind;
  case Type::HalfTyID:
    return LLVMHalfTypeKind;
  case Type::FloatTyID:
    return LLVMFloatTypeKind;
  case Type::DoubleTyID:
    return LLVMDoubleTypeKind;
  case Type::X86_FP80TyID:
    return LLVMX86_FP80TypeKind;
  case Type::FP128TyID:
    return LLVMFP128TypeKind;
  case Type::PPC_FP128TyID:
    return LLVMPPC_FP128TypeKind;
  case Type::LabelTyID:
    return LLVMLabelTypeKind;
  case Type::MetadataTyID:
    return LLVMMetadataTypeKind;
  case Type::IntegerTyID:
    return LLVMIntegerTypeKind;
  case Type::FunctionTyID:
    return LLVMFunctionTypeKind;
  case Type::StructTyID:
    return LLVMStructTypeKind;
  case Type::ArrayTyID:
    return LLVMArrayTypeKind;
  case Type::PointerTyID:
    return LLVMPointerTypeKind;
  case Type::VectorTyID:
    return LLVMVectorTypeKind;
  case Type::X86_MMXTyID:
    return LLVMX86_MMXTypeKind;
  case Type::TokenTyID:
    return LLVMTokenTypeKind;
  }
  llvm_unreachable("Unhandled TypeID.");
}

LLVMBool LLVMTypeIsSized(LLVMTypeRef Ty)
{
    return unwrap(Ty)->isSized();
}

LLVMContextRef LLVMGetTypeContext(LLVMTypeRef Ty) {
  return wrap(&unwrap(Ty)->getContext());
}

void LLVMDumpType(LLVMTypeRef Ty) {
  return unwrap(Ty)->print(errs(), /*IsForDebug=*/true);
}

char *LLVMPrintTypeToString(LLVMTypeRef Ty) {
  std::string buf;
  raw_string_ostream os(buf);

  if (unwrap(Ty))
    unwrap(Ty)->print(os);
  else
    os << "Printing <null> Type";

  os.flush();

  return strdup(buf.c_str());
}

/*--.. Operations on integer types .........................................--*/

LLVMTypeRef LLVMInt1TypeInContext(LLVMContextRef C)  {
  return (LLVMTypeRef) Type::getInt1Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt8TypeInContext(LLVMContextRef C)  {
  return (LLVMTypeRef) Type::getInt8Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt16TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt16Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt32TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt32Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt64TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt64Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt128TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt128Ty(*unwrap(C));
}
LLVMTypeRef LLVMIntTypeInContext(LLVMContextRef C, unsigned NumBits) {
  return wrap(IntegerType::get(*unwrap(C), NumBits));
}

LLVMTypeRef LLVMInt1Type(void)  {
  return LLVMInt1TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt8Type(void)  {
  return LLVMInt8TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt16Type(void) {
  return LLVMInt16TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt32Type(void) {
  return LLVMInt32TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt64Type(void) {
  return LLVMInt64TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt128Type(void) {
  return LLVMInt128TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMIntType(unsigned NumBits) {
  return LLVMIntTypeInContext(LLVMGetGlobalContext(), NumBits);
}

unsigned LLVMGetIntTypeWidth(LLVMTypeRef IntegerTy) {
  return unwrap<IntegerType>(IntegerTy)->getBitWidth();
}

/*--.. Operations on real types ............................................--*/

LLVMTypeRef LLVMHalfTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getHalfTy(*unwrap(C));
}
LLVMTypeRef LLVMFloatTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getFloatTy(*unwrap(C));
}
LLVMTypeRef LLVMDoubleTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getDoubleTy(*unwrap(C));
}
LLVMTypeRef LLVMX86FP80TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getX86_FP80Ty(*unwrap(C));
}
LLVMTypeRef LLVMFP128TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getFP128Ty(*unwrap(C));
}
LLVMTypeRef LLVMPPCFP128TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getPPC_FP128Ty(*unwrap(C));
}
LLVMTypeRef LLVMX86MMXTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getX86_MMXTy(*unwrap(C));
}

LLVMTypeRef LLVMHalfType(void) {
  return LLVMHalfTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMFloatType(void) {
  return LLVMFloatTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMDoubleType(void) {
  return LLVMDoubleTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMX86FP80Type(void) {
  return LLVMX86FP80TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMFP128Type(void) {
  return LLVMFP128TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMPPCFP128Type(void) {
  return LLVMPPCFP128TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMX86MMXType(void) {
  return LLVMX86MMXTypeInContext(LLVMGetGlobalContext());
}

/*--.. Operations on function types ........................................--*/

LLVMTypeRef LLVMFunctionType(LLVMTypeRef ReturnType,
                             LLVMTypeRef *ParamTypes, unsigned ParamCount,
                             LLVMBool IsVarArg) {
  ArrayRef<Type*> Tys(unwrap(ParamTypes), ParamCount);
  return wrap(FunctionType::get(unwrap(ReturnType), Tys, IsVarArg != 0));
}

LLVMBool LLVMIsFunctionVarArg(LLVMTypeRef FunctionTy) {
  return unwrap<FunctionType>(FunctionTy)->isVarArg();
}

LLVMTypeRef LLVMGetReturnType(LLVMTypeRef FunctionTy) {
  return wrap(unwrap<FunctionType>(FunctionTy)->getReturnType());
}

unsigned LLVMCountParamTypes(LLVMTypeRef FunctionTy) {
  return unwrap<FunctionType>(FunctionTy)->getNumParams();
}

void LLVMGetParamTypes(LLVMTypeRef FunctionTy, LLVMTypeRef *Dest) {
  FunctionType *Ty = unwrap<FunctionType>(FunctionTy);
  for (FunctionType::param_iterator I = Ty->param_begin(),
                                    E = Ty->param_end(); I != E; ++I)
    *Dest++ = wrap(*I);
}

/*--.. Operations on struct types ..........................................--*/

LLVMTypeRef LLVMStructTypeInContext(LLVMContextRef C, LLVMTypeRef *ElementTypes,
                           unsigned ElementCount, LLVMBool Packed) {
  ArrayRef<Type*> Tys(unwrap(ElementTypes), ElementCount);
  return wrap(StructType::get(*unwrap(C), Tys, Packed != 0));
}

LLVMTypeRef LLVMStructType(LLVMTypeRef *ElementTypes,
                           unsigned ElementCount, LLVMBool Packed) {
  return LLVMStructTypeInContext(LLVMGetGlobalContext(), ElementTypes,
                                 ElementCount, Packed);
}

LLVMTypeRef LLVMStructCreateNamed(LLVMContextRef C, const char *Name)
{
  return wrap(StructType::create(*unwrap(C), Name));
}

const char *LLVMGetStructName(LLVMTypeRef Ty)
{
  StructType *Type = unwrap<StructType>(Ty);
  if (!Type->hasName())
    return nullptr;
  return Type->getName().data();
}

void LLVMStructSetBody(LLVMTypeRef StructTy, LLVMTypeRef *ElementTypes,
                       unsigned ElementCount, LLVMBool Packed) {
  ArrayRef<Type*> Tys(unwrap(ElementTypes), ElementCount);
  unwrap<StructType>(StructTy)->setBody(Tys, Packed != 0);
}

unsigned LLVMCountStructElementTypes(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->getNumElements();
}

void LLVMGetStructElementTypes(LLVMTypeRef StructTy, LLVMTypeRef *Dest) {
  StructType *Ty = unwrap<StructType>(StructTy);
  for (StructType::element_iterator I = Ty->element_begin(),
                                    E = Ty->element_end(); I != E; ++I)
    *Dest++ = wrap(*I);
}

LLVMTypeRef LLVMStructGetTypeAtIndex(LLVMTypeRef StructTy, unsigned i) {
  StructType *Ty = unwrap<StructType>(StructTy);
  return wrap(Ty->getTypeAtIndex(i));
}

LLVMBool LLVMIsPackedStruct(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->isPacked();
}

LLVMBool LLVMIsOpaqueStruct(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->isOpaque();
}

LLVMBool LLVMIsLiteralStruct(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->isLiteral();
}

LLVMTypeRef LLVMGetTypeByName(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getTypeByName(Name));
}

/*--.. Operations on array, pointer, and vector types (sequence types) .....--*/

void LLVMGetSubtypes(LLVMTypeRef Tp, LLVMTypeRef *Arr) {
    int i = 0;
    for (auto *T : unwrap(Tp)->subtypes()) {
        Arr[i] = wrap(T);
        i++;
    }
}

LLVMTypeRef LLVMArrayType(LLVMTypeRef ElementType, unsigned ElementCount) {
  return wrap(ArrayType::get(unwrap(ElementType), ElementCount));
}

LLVMTypeRef LLVMPointerType(LLVMTypeRef ElementType, unsigned AddressSpace) {
  return wrap(PointerType::get(unwrap(ElementType), AddressSpace));
}

LLVMTypeRef LLVMVectorType(LLVMTypeRef ElementType, unsigned ElementCount) {
  return wrap(VectorType::get(unwrap(ElementType), ElementCount));
}

LLVMTypeRef LLVMGetElementType(LLVMTypeRef WrappedTy) {
  auto *Ty = unwrap<Type>(WrappedTy);
  if (auto *PTy = dyn_cast<PointerType>(Ty))
    return wrap(PTy->getElementType());
  return wrap(cast<SequentialType>(Ty)->getElementType());
}

unsigned LLVMGetNumContainedTypes(LLVMTypeRef Tp) {
    return unwrap(Tp)->getNumContainedTypes();
}

unsigned LLVMGetArrayLength(LLVMTypeRef ArrayTy) {
  return unwrap<ArrayType>(ArrayTy)->getNumElements();
}

unsigned LLVMGetPointerAddressSpace(LLVMTypeRef PointerTy) {
  return unwrap<PointerType>(PointerTy)->getAddressSpace();
}

unsigned LLVMGetVectorSize(LLVMTypeRef VectorTy) {
  return unwrap<VectorType>(VectorTy)->getNumElements();
}

/*--.. Operations on other types ...........................................--*/

LLVMTypeRef LLVMVoidTypeInContext(LLVMContextRef C)  {
  return wrap(Type::getVoidTy(*unwrap(C)));
}
LLVMTypeRef LLVMLabelTypeInContext(LLVMContextRef C) {
  return wrap(Type::getLabelTy(*unwrap(C)));
}
LLVMTypeRef LLVMTokenTypeInContext(LLVMContextRef C) {
  return wrap(Type::getTokenTy(*unwrap(C)));
}
LLVMTypeRef LLVMMetadataTypeInContext(LLVMContextRef C) {
  return wrap(Type::getMetadataTy(*unwrap(C)));
}

LLVMTypeRef LLVMVoidType(void)  {
  return LLVMVoidTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMLabelType(void) {
  return LLVMLabelTypeInContext(LLVMGetGlobalContext());
}

/*===-- Operations on values ----------------------------------------------===*/

/*--.. Operations on all values ............................................--*/

LLVMTypeRef LLVMTypeOf(LLVMValueRef Val) {
  return wrap(unwrap(Val)->getType());
}

LLVMValueKind LLVMGetValueKind(LLVMValueRef Val) {
    switch(unwrap(Val)->getValueID()) {
#define HANDLE_VALUE(Name) \
  case Value::Name##Val: \
    return LLVM##Name##ValueKind;
#include "llvm/IR/Value.def"
  default:
    return LLVMInstructionValueKind;
  }
}

const char *LLVMGetValueName2(LLVMValueRef Val, size_t *Length) {
  auto *V = unwrap(Val);
  *Length = V->getName().size();
  return V->getName().data();
}

void LLVMSetValueName2(LLVMValueRef Val, const char *Name, size_t NameLen) {
  unwrap(Val)->setName(StringRef(Name, NameLen));
}

const char *LLVMGetValueName(LLVMValueRef Val) {
  return unwrap(Val)->getName().data();
}

void LLVMSetValueName(LLVMValueRef Val, const char *Name) {
  unwrap(Val)->setName(Name);
}

void LLVMDumpValue(LLVMValueRef Val) {
  unwrap(Val)->print(errs(), /*IsForDebug=*/true);
}

char* LLVMPrintValueToString(LLVMValueRef Val) {
  std::string buf;
  raw_string_ostream os(buf);

  if (unwrap(Val))
    unwrap(Val)->print(os);
  else
    os << "Printing <null> Value";

  os.flush();

  return strdup(buf.c_str());
}

void LLVMReplaceAllUsesWith(LLVMValueRef OldVal, LLVMValueRef NewVal) {
  unwrap(OldVal)->replaceAllUsesWith(unwrap(NewVal));
}

int LLVMHasMetadata(LLVMValueRef Inst) {
  return unwrap<Instruction>(Inst)->hasMetadata();
}

LLVMValueRef LLVMGetMetadata(LLVMValueRef Inst, unsigned KindID) {
  auto *I = unwrap<Instruction>(Inst);
  assert(I && "Expected instruction");
  if (auto *MD = I->getMetadata(KindID))
    return wrap(MetadataAsValue::get(I->getContext(), MD));
  return nullptr;
}

// MetadataAsValue uses a canonical format which strips the actual MDNode for
// MDNode with just a single constant value, storing just a ConstantAsMetadata
// This undoes this canonicalization, reconstructing the MDNode.
static MDNode *extractMDNode(MetadataAsValue *MAV) {
  Metadata *MD = MAV->getMetadata();
  assert((isa<MDNode>(MD) || isa<ConstantAsMetadata>(MD)) &&
      "Expected a metadata node or a canonicalized constant");

  if (MDNode *N = dyn_cast<MDNode>(MD))
    return N;

  return MDNode::get(MAV->getContext(), MD);
}

void LLVMSetMetadata(LLVMValueRef Inst, unsigned KindID, LLVMValueRef Val) {
  MDNode *N = Val ? extractMDNode(unwrap<MetadataAsValue>(Val)) : nullptr;

  unwrap<Instruction>(Inst)->setMetadata(KindID, N);
}

struct LLVMOpaqueValueMetadataEntry {
  unsigned Kind;
  LLVMMetadataRef Metadata;
};

using MetadataEntries = SmallVectorImpl<std::pair<unsigned, MDNode *>>;
static LLVMValueMetadataEntry *
llvm_getMetadata(size_t *NumEntries,
                 llvm::function_ref<void(MetadataEntries &)> AccessMD) {
  SmallVector<std::pair<unsigned, MDNode *>, 8> MVEs;
  AccessMD(MVEs);

  LLVMOpaqueValueMetadataEntry *Result =
  static_cast<LLVMOpaqueValueMetadataEntry *>(
                                              safe_malloc(MVEs.size() * sizeof(LLVMOpaqueValueMetadataEntry)));
  for (unsigned i = 0; i < MVEs.size(); ++i) {
    const auto &ModuleFlag = MVEs[i];
    Result[i].Kind = ModuleFlag.first;
    Result[i].Metadata = wrap(ModuleFlag.second);
  }
  *NumEntries = MVEs.size();
  return Result;
}

LLVMValueMetadataEntry *
LLVMInstructionGetAllMetadataOtherThanDebugLoc(LLVMValueRef Value,
                                               size_t *NumEntries) {
  return llvm_getMetadata(NumEntries, [&Value](MetadataEntries &Entries) {
    unwrap<Instruction>(Value)->getAllMetadata(Entries);
  });
}

/*--.. Conversion functions ................................................--*/

#define LLVM_DEFINE_VALUE_CAST(name)                                       \
  LLVMValueRef LLVMIsA##name(LLVMValueRef Val) {                           \
    return wrap(static_cast<Value*>(dyn_cast_or_null<name>(unwrap(Val)))); \
  }

LLVM_FOR_EACH_VALUE_SUBCLASS(LLVM_DEFINE_VALUE_CAST)

LLVMValueRef LLVMIsAMDNode(LLVMValueRef Val) {
  if (auto *MD = dyn_cast_or_null<MetadataAsValue>(unwrap(Val)))
    if (isa<MDNode>(MD->getMetadata()) ||
        isa<ValueAsMetadata>(MD->getMetadata()))
      return Val;
  return nullptr;
}

LLVMValueRef LLVMIsAMDString(LLVMValueRef Val) {
  if (auto *MD = dyn_cast_or_null<MetadataAsValue>(unwrap(Val)))
    if (isa<MDString>(MD->getMetadata()))
      return Val;
  return nullptr;
}

/*--.. Operations on Uses ..................................................--*/
LLVMUseRef LLVMGetFirstUse(LLVMValueRef Val) {
  Value *V = unwrap(Val);
  Value::use_iterator I = V->use_begin();
  if (I == V->use_end())
    return nullptr;
  return wrap(&*I);
}

LLVMUseRef LLVMGetNextUse(LLVMUseRef U) {
  Use *Next = unwrap(U)->getNext();
  if (Next)
    return wrap(Next);
  return nullptr;
}

LLVMValueRef LLVMGetUser(LLVMUseRef U) {
  return wrap(unwrap(U)->getUser());
}

LLVMValueRef LLVMGetUsedValue(LLVMUseRef U) {
  return wrap(unwrap(U)->get());
}

/*--.. Operations on Users .................................................--*/

static LLVMValueRef getMDNodeOperandImpl(LLVMContext &Context, const MDNode *N,
                                         unsigned Index) {
  Metadata *Op = N->getOperand(Index);
  if (!Op)
    return nullptr;
  if (auto *C = dyn_cast<ConstantAsMetadata>(Op))
    return wrap(C->getValue());
  return wrap(MetadataAsValue::get(Context, Op));
}

LLVMValueRef LLVMGetOperand(LLVMValueRef Val, unsigned Index) {
  Value *V = unwrap(Val);
  if (auto *MD = dyn_cast<MetadataAsValue>(V)) {
    if (auto *L = dyn_cast<ValueAsMetadata>(MD->getMetadata())) {
      assert(Index == 0 && "Function-local metadata can only have one operand");
      return wrap(L->getValue());
    }
    return getMDNodeOperandImpl(V->getContext(),
                                cast<MDNode>(MD->getMetadata()), Index);
  }

  return wrap(cast<User>(V)->getOperand(Index));
}

LLVMUseRef LLVMGetOperandUse(LLVMValueRef Val, unsigned Index) {
  Value *V = unwrap(Val);
  return wrap(&cast<User>(V)->getOperandUse(Index));
}

void LLVMSetOperand(LLVMValueRef Val, unsigned Index, LLVMValueRef Op) {
  unwrap<User>(Val)->setOperand(Index, unwrap(Op));
}

int LLVMGetNumOperands(LLVMValueRef Val) {
  Value *V = unwrap(Val);
  if (isa<MetadataAsValue>(V))
    return LLVMGetMDNodeNumOperands(Val);

  return cast<User>(V)->getNumOperands();
}

/*--.. Operations on constants of any type .................................--*/

LLVMValueRef LLVMConstNull(LLVMTypeRef Ty) {
  return wrap(Constant::getNullValue(unwrap(Ty)));
}

LLVMValueRef LLVMConstAllOnes(LLVMTypeRef Ty) {
  return wrap(Constant::getAllOnesValue(unwrap(Ty)));
}

LLVMValueRef LLVMGetUndef(LLVMTypeRef Ty) {
  return wrap(UndefValue::get(unwrap(Ty)));
}

LLVMBool LLVMIsConstant(LLVMValueRef Ty) {
  return isa<Constant>(unwrap(Ty));
}

LLVMBool LLVMIsNull(LLVMValueRef Val) {
  if (Constant *C = dyn_cast<Constant>(unwrap(Val)))
    return C->isNullValue();
  return false;
}

LLVMBool LLVMIsUndef(LLVMValueRef Val) {
  return isa<UndefValue>(unwrap(Val));
}

LLVMValueRef LLVMConstPointerNull(LLVMTypeRef Ty) {
  return wrap(ConstantPointerNull::get(unwrap<PointerType>(Ty)));
}

/*--.. Operations on metadata nodes ........................................--*/

LLVMValueRef LLVMMDStringInContext(LLVMContextRef C, const char *Str,
                                   unsigned SLen) {
  LLVMContext &Context = *unwrap(C);
  return wrap(MetadataAsValue::get(
      Context, MDString::get(Context, StringRef(Str, SLen))));
}

LLVMValueRef LLVMMDString(const char *Str, unsigned SLen) {
  return LLVMMDStringInContext(LLVMGetGlobalContext(), Str, SLen);
}

LLVMValueRef LLVMMDNodeInContext(LLVMContextRef C, LLVMValueRef *Vals,
                                 unsigned Count) {
  LLVMContext &Context = *unwrap(C);
  SmallVector<Metadata *, 8> MDs;
  for (auto *OV : makeArrayRef(Vals, Count)) {
    Value *V = unwrap(OV);
    Metadata *MD;
    if (!V)
      MD = nullptr;
    else if (auto *C = dyn_cast<Constant>(V))
      MD = ConstantAsMetadata::get(C);
    else if (auto *MDV = dyn_cast<MetadataAsValue>(V)) {
      MD = MDV->getMetadata();
      assert(!isa<LocalAsMetadata>(MD) && "Unexpected function-local metadata "
                                          "outside of direct argument to call");
    } else {
      // This is function-local metadata.  Pretend to make an MDNode.
      assert(Count == 1 &&
             "Expected only one operand to function-local metadata");
      return wrap(MetadataAsValue::get(Context, LocalAsMetadata::get(V)));
    }

    MDs.push_back(MD);
  }
  return wrap(MetadataAsValue::get(Context, MDNode::get(Context, MDs)));
}

LLVMValueRef LLVMMDNode(LLVMValueRef *Vals, unsigned Count) {
  return LLVMMDNodeInContext(LLVMGetGlobalContext(), Vals, Count);
}

LLVMValueRef LLVMMetadataAsValue(LLVMContextRef C, LLVMMetadataRef MD) {
  return wrap(MetadataAsValue::get(*unwrap(C), unwrap(MD)));
}

LLVMMetadataRef LLVMValueAsMetadata(LLVMValueRef Val) {
  auto *V = unwrap(Val);
  if (auto *C = dyn_cast<Constant>(V))
    return wrap(ConstantAsMetadata::get(C));
  if (auto *MAV = dyn_cast<MetadataAsValue>(V))
    return wrap(MAV->getMetadata());
  return wrap(ValueAsMetadata::get(V));
}

const char *LLVMGetMDString(LLVMValueRef V, unsigned *Length) {
  if (const auto *MD = dyn_cast<MetadataAsValue>(unwrap(V)))
    if (const MDString *S = dyn_cast<MDString>(MD->getMetadata())) {
      *Length = S->getString().size();
      return S->getString().data();
    }
  *Length = 0;
  return nullptr;
}

unsigned LLVMGetMDNodeNumOperands(LLVMValueRef V) {
  auto *MD = cast<MetadataAsValue>(unwrap(V));
  if (isa<ValueAsMetadata>(MD->getMetadata()))
    return 1;
  return cast<MDNode>(MD->getMetadata())->getNumOperands();
}

LLVMNamedMDNodeRef LLVMGetFirstNamedMetadata(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::named_metadata_iterator I = Mod->named_metadata_begin();
  if (I == Mod->named_metadata_end())
    return nullptr;
  return wrap(&*I);
}

LLVMNamedMDNodeRef LLVMGetLastNamedMetadata(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::named_metadata_iterator I = Mod->named_metadata_end();
  if (I == Mod->named_metadata_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMNamedMDNodeRef LLVMGetNextNamedMetadata(LLVMNamedMDNodeRef NMD) {
  NamedMDNode *NamedNode = unwrap<NamedMDNode>(NMD);
  Module::named_metadata_iterator I(NamedNode);
  if (++I == NamedNode->getParent()->named_metadata_end())
    return nullptr;
  return wrap(&*I);
}

LLVMNamedMDNodeRef LLVMGetPreviousNamedMetadata(LLVMNamedMDNodeRef NMD) {
  NamedMDNode *NamedNode = unwrap<NamedMDNode>(NMD);
  Module::named_metadata_iterator I(NamedNode);
  if (I == NamedNode->getParent()->named_metadata_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMNamedMDNodeRef LLVMGetNamedMetadata(LLVMModuleRef M,
                                        const char *Name, size_t NameLen) {
  return wrap(unwrap(M)->getNamedMetadata(StringRef(Name, NameLen)));
}

LLVMNamedMDNodeRef LLVMGetOrInsertNamedMetadata(LLVMModuleRef M,
                                                const char *Name, size_t NameLen) {
  return wrap(unwrap(M)->getOrInsertNamedMetadata({Name, NameLen}));
}

const char *LLVMGetNamedMetadataName(LLVMNamedMDNodeRef NMD, size_t *NameLen) {
  NamedMDNode *NamedNode = unwrap<NamedMDNode>(NMD);
  *NameLen = NamedNode->getName().size();
  return NamedNode->getName().data();
}

void LLVMGetMDNodeOperands(LLVMValueRef V, LLVMValueRef *Dest) {
  auto *MD = cast<MetadataAsValue>(unwrap(V));
  if (auto *MDV = dyn_cast<ValueAsMetadata>(MD->getMetadata())) {
    *Dest = wrap(MDV->getValue());
    return;
  }
  const auto *N = cast<MDNode>(MD->getMetadata());
  const unsigned numOperands = N->getNumOperands();
  LLVMContext &Context = unwrap(V)->getContext();
  for (unsigned i = 0; i < numOperands; i++)
    Dest[i] = getMDNodeOperandImpl(Context, N, i);
}

unsigned LLVMGetNamedMetadataNumOperands(LLVMModuleRef M, const char *Name) {
  if (NamedMDNode *N = unwrap(M)->getNamedMetadata(Name)) {
    return N->getNumOperands();
  }
  return 0;
}

void LLVMGetNamedMetadataOperands(LLVMModuleRef M, const char *Name,
                                  LLVMValueRef *Dest) {
  NamedMDNode *N = unwrap(M)->getNamedMetadata(Name);
  if (!N)
    return;
  LLVMContext &Context = unwrap(M)->getContext();
  for (unsigned i=0;i<N->getNumOperands();i++)
    Dest[i] = wrap(MetadataAsValue::get(Context, N->getOperand(i)));
}

void LLVMAddNamedMetadataOperand(LLVMModuleRef M, const char *Name,
                                 LLVMValueRef Val) {
  NamedMDNode *N = unwrap(M)->getOrInsertNamedMetadata(Name);
  if (!N)
    return;
  if (!Val)
    return;
  N->addOperand(extractMDNode(unwrap<MetadataAsValue>(Val)));
}

const char *LLVMGetDebugLocDirectory(LLVMValueRef Val, unsigned *Length) {
  if (!Length) return nullptr;
  StringRef S;
  if (const auto *I = unwrap<Instruction>(Val)) {
    S = I->getDebugLoc()->getDirectory();
  } else if (const auto *GV = unwrap<GlobalVariable>(Val)) {
    SmallVector<DIGlobalVariableExpression *, 1> GVEs;
    GV->getDebugInfo(GVEs);
    if (GVEs.size())
      if (const DIGlobalVariable *DGV = GVEs[0]->getVariable())
        S = DGV->getDirectory();
  } else if (const auto *F = unwrap<Function>(Val)) {
    if (const DISubprogram *DSP = F->getSubprogram())
      S = DSP->getDirectory();
  } else {
    assert(0 && "Expected Instruction, GlobalVariable or Function");
    return nullptr;
  }
  *Length = S.size();
  return S.data();
}

const char *LLVMGetDebugLocFilename(LLVMValueRef Val, unsigned *Length) {
  if (!Length) return nullptr;
  StringRef S;
  if (const auto *I = unwrap<Instruction>(Val)) {
    S = I->getDebugLoc()->getFilename();
  } else if (const auto *GV = unwrap<GlobalVariable>(Val)) {
    SmallVector<DIGlobalVariableExpression *, 1> GVEs;
    GV->getDebugInfo(GVEs);
    if (GVEs.size())
      if (const DIGlobalVariable *DGV = GVEs[0]->getVariable())
        S = DGV->getFilename();
  } else if (const auto *F = unwrap<Function>(Val)) {
    if (const DISubprogram *DSP = F->getSubprogram())
      S = DSP->getFilename();
  } else {
    assert(0 && "Expected Instruction, GlobalVariable or Function");
    return nullptr;
  }
  *Length = S.size();
  return S.data();
}

unsigned LLVMGetDebugLocLine(LLVMValueRef Val) {
  unsigned L = 0;
  if (const auto *I = unwrap<Instruction>(Val)) {
    L = I->getDebugLoc()->getLine();
  } else if (const auto *GV = unwrap<GlobalVariable>(Val)) {
    SmallVector<DIGlobalVariableExpression *, 1> GVEs;
    GV->getDebugInfo(GVEs);
    if (GVEs.size())
      if (const DIGlobalVariable *DGV = GVEs[0]->getVariable())
        L = DGV->getLine();
  } else if (const auto *F = unwrap<Function>(Val)) {
    if (const DISubprogram *DSP = F->getSubprogram())
      L = DSP->getLine();
  } else {
    assert(0 && "Expected Instruction, GlobalVariable or Function");
    return -1;
  }
  return L;
}

unsigned LLVMGetDebugLocColumn(LLVMValueRef Val) {
  unsigned C = 0;
  if (const auto *I = unwrap<Instruction>(Val))
    if (const auto &L = I->getDebugLoc())
      C = L->getColumn();
  return C;
}

/*--.. Operations on scalar constants ......................................--*/

LLVMValueRef LLVMConstInt(LLVMTypeRef IntTy, unsigned long long N,
                          LLVMBool SignExtend) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), N, SignExtend != 0));
}

LLVMValueRef LLVMConstIntOfArbitraryPrecision(LLVMTypeRef IntTy,
                                              unsigned NumWords,
                                              const uint64_t Words[]) {
    IntegerType *Ty = unwrap<IntegerType>(IntTy);
    return wrap(ConstantInt::get(Ty->getContext(),
                                 APInt(Ty->getBitWidth(),
                                       makeArrayRef(Words, NumWords))));
}

LLVMValueRef LLVMConstIntOfString(LLVMTypeRef IntTy, const char Str[],
                                  uint8_t Radix) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), StringRef(Str),
                               Radix));
}

LLVMValueRef LLVMConstIntOfStringAndSize(LLVMTypeRef IntTy, const char Str[],
                                         unsigned SLen, uint8_t Radix) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), StringRef(Str, SLen),
                               Radix));
}

LLVMValueRef LLVMConstReal(LLVMTypeRef RealTy, double N) {
  return wrap(ConstantFP::get(unwrap(RealTy), N));
}

LLVMValueRef LLVMConstRealOfString(LLVMTypeRef RealTy, const char *Text) {
  return wrap(ConstantFP::get(unwrap(RealTy), StringRef(Text)));
}

LLVMValueRef LLVMConstRealOfStringAndSize(LLVMTypeRef RealTy, const char Str[],
                                          unsigned SLen) {
  return wrap(ConstantFP::get(unwrap(RealTy), StringRef(Str, SLen)));
}

unsigned long long LLVMConstIntGetZExtValue(LLVMValueRef ConstantVal) {
  return unwrap<ConstantInt>(ConstantVal)->getZExtValue();
}

long long LLVMConstIntGetSExtValue(LLVMValueRef ConstantVal) {
  return unwrap<ConstantInt>(ConstantVal)->getSExtValue();
}

double LLVMConstRealGetDouble(LLVMValueRef ConstantVal, LLVMBool *LosesInfo) {
  ConstantFP *cFP = unwrap<ConstantFP>(ConstantVal) ;
  Type *Ty = cFP->getType();

  if (Ty->isFloatTy()) {
    *LosesInfo = false;
    return cFP->getValueAPF().convertToFloat();
  }

  if (Ty->isDoubleTy()) {
    *LosesInfo = false;
    return cFP->getValueAPF().convertToDouble();
  }

  bool APFLosesInfo;
  APFloat APF = cFP->getValueAPF();
  APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &APFLosesInfo);
  *LosesInfo = APFLosesInfo;
  return APF.convertToDouble();
}

/*--.. Operations on composite constants ...................................--*/

LLVMValueRef LLVMConstStringInContext(LLVMContextRef C, const char *Str,
                                      unsigned Length,
                                      LLVMBool DontNullTerminate) {
  /* Inverted the sense of AddNull because ', 0)' is a
     better mnemonic for null termination than ', 1)'. */
  return wrap(ConstantDataArray::getString(*unwrap(C), StringRef(Str, Length),
                                           DontNullTerminate == 0));
}

LLVMValueRef LLVMConstString(const char *Str, unsigned Length,
                             LLVMBool DontNullTerminate) {
  return LLVMConstStringInContext(LLVMGetGlobalContext(), Str, Length,
                                  DontNullTerminate);
}

LLVMValueRef LLVMGetElementAsConstant(LLVMValueRef C, unsigned idx) {
  return wrap(unwrap<ConstantDataSequential>(C)->getElementAsConstant(idx));
}

LLVMBool LLVMIsConstantString(LLVMValueRef C) {
  return unwrap<ConstantDataSequential>(C)->isString();
}

const char *LLVMGetAsString(LLVMValueRef C, size_t *Length) {
  StringRef Str = unwrap<ConstantDataSequential>(C)->getAsString();
  *Length = Str.size();
  return Str.data();
}

LLVMValueRef LLVMConstArray(LLVMTypeRef ElementTy,
                            LLVMValueRef *ConstantVals, unsigned Length) {
  ArrayRef<Constant*> V(unwrap<Constant>(ConstantVals, Length), Length);
  return wrap(ConstantArray::get(ArrayType::get(unwrap(ElementTy), Length), V));
}

LLVMValueRef LLVMConstStructInContext(LLVMContextRef C,
                                      LLVMValueRef *ConstantVals,
                                      unsigned Count, LLVMBool Packed) {
  Constant **Elements = unwrap<Constant>(ConstantVals, Count);
  return wrap(ConstantStruct::getAnon(*unwrap(C), makeArrayRef(Elements, Count),
                                      Packed != 0));
}

LLVMValueRef LLVMConstStruct(LLVMValueRef *ConstantVals, unsigned Count,
                             LLVMBool Packed) {
  return LLVMConstStructInContext(LLVMGetGlobalContext(), ConstantVals, Count,
                                  Packed);
}

LLVMValueRef LLVMConstNamedStruct(LLVMTypeRef StructTy,
                                  LLVMValueRef *ConstantVals,
                                  unsigned Count) {
  Constant **Elements = unwrap<Constant>(ConstantVals, Count);
  StructType *Ty = cast<StructType>(unwrap(StructTy));

  return wrap(ConstantStruct::get(Ty, makeArrayRef(Elements, Count)));
}

LLVMValueRef LLVMConstVector(LLVMValueRef *ScalarConstantVals, unsigned Size) {
  return wrap(ConstantVector::get(makeArrayRef(
                            unwrap<Constant>(ScalarConstantVals, Size), Size)));
}

/*-- Opcode mapping */

static LLVMOpcode map_to_llvmopcode(int opcode)
{
    switch (opcode) {
      default: llvm_unreachable("Unhandled Opcode.");
#define HANDLE_INST(num, opc, clas) case num: return LLVM##opc;
#include "llvm/IR/Instruction.def"
#undef HANDLE_INST
    }
}

static int map_from_llvmopcode(LLVMOpcode code)
{
    switch (code) {
#define HANDLE_INST(num, opc, clas) case LLVM##opc: return num;
#include "llvm/IR/Instruction.def"
#undef HANDLE_INST
    }
    llvm_unreachable("Unhandled Opcode.");
}

/*--.. Constant expressions ................................................--*/

LLVMOpcode LLVMGetConstOpcode(LLVMValueRef ConstantVal) {
  return map_to_llvmopcode(unwrap<ConstantExpr>(ConstantVal)->getOpcode());
}

LLVMValueRef LLVMAlignOf(LLVMTypeRef Ty) {
  return wrap(ConstantExpr::getAlignOf(unwrap(Ty)));
}

LLVMValueRef LLVMSizeOf(LLVMTypeRef Ty) {
  return wrap(ConstantExpr::getSizeOf(unwrap(Ty)));
}

LLVMValueRef LLVMConstNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNSWNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNSWNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNUWNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNUWNeg(unwrap<Constant>(ConstantVal)));
}


LLVMValueRef LLVMConstFNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getFNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNot(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNot(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAdd(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWAdd(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWAdd(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWAdd(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWAdd(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFAdd(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSub(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWSub(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWSub(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWSub(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWSub(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFSub(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getMul(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWMul(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWMul(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWMul(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWMul(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFMul(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstUDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getUDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstExactUDiv(LLVMValueRef LHSConstant,
                                LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getExactUDiv(unwrap<Constant>(LHSConstant),
                                         unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstExactSDiv(LLVMValueRef LHSConstant,
                                LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getExactSDiv(unwrap<Constant>(LHSConstant),
                                         unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstURem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getURem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSRem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSRem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFRem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFRem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstAnd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAnd(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstOr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getOr(unwrap<Constant>(LHSConstant),
                                  unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstXor(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getXor(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstICmp(LLVMIntPredicate Predicate,
                           LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getICmp(Predicate,
                                    unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFCmp(LLVMRealPredicate Predicate,
                           LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFCmp(Predicate,
                                    unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstShl(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getShl(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstLShr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getLShr(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstAShr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAShr(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstGEP(LLVMValueRef ConstantVal,
                          LLVMValueRef *ConstantIndices, unsigned NumIndices) {
  ArrayRef<Constant *> IdxList(unwrap<Constant>(ConstantIndices, NumIndices),
                               NumIndices);
  Constant *Val = unwrap<Constant>(ConstantVal);
  Type *Ty =
      cast<PointerType>(Val->getType()->getScalarType())->getElementType();
  return wrap(ConstantExpr::getGetElementPtr(Ty, Val, IdxList));
}

LLVMValueRef LLVMConstInBoundsGEP(LLVMValueRef ConstantVal,
                                  LLVMValueRef *ConstantIndices,
                                  unsigned NumIndices) {
  ArrayRef<Constant *> IdxList(unwrap<Constant>(ConstantIndices, NumIndices),
                               NumIndices);
  Constant *Val = unwrap<Constant>(ConstantVal);
  Type *Ty =
      cast<PointerType>(Val->getType()->getScalarType())->getElementType();
  return wrap(ConstantExpr::getInBoundsGetElementPtr(Ty, Val, IdxList));
}

LLVMValueRef LLVMConstTrunc(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getTrunc(unwrap<Constant>(ConstantVal),
                                     unwrap(ToType)));
}

LLVMValueRef LLVMConstSExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSExt(unwrap<Constant>(ConstantVal),
                                    unwrap(ToType)));
}

LLVMValueRef LLVMConstZExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getZExt(unwrap<Constant>(ConstantVal),
                                    unwrap(ToType)));
}

LLVMValueRef LLVMConstFPTrunc(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPTrunc(unwrap<Constant>(ConstantVal),
                                       unwrap(ToType)));
}

LLVMValueRef LLVMConstFPExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPExtend(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstUIToFP(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getUIToFP(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstSIToFP(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSIToFP(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstFPToUI(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPToUI(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstFPToSI(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPToSI(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstPtrToInt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getPtrToInt(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstIntToPtr(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getIntToPtr(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstBitCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getBitCast(unwrap<Constant>(ConstantVal),
                                       unwrap(ToType)));
}

LLVMValueRef LLVMConstAddrSpaceCast(LLVMValueRef ConstantVal,
                                    LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getAddrSpaceCast(unwrap<Constant>(ConstantVal),
                                             unwrap(ToType)));
}

LLVMValueRef LLVMConstZExtOrBitCast(LLVMValueRef ConstantVal,
                                    LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getZExtOrBitCast(unwrap<Constant>(ConstantVal),
                                             unwrap(ToType)));
}

LLVMValueRef LLVMConstSExtOrBitCast(LLVMValueRef ConstantVal,
                                    LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSExtOrBitCast(unwrap<Constant>(ConstantVal),
                                             unwrap(ToType)));
}

LLVMValueRef LLVMConstTruncOrBitCast(LLVMValueRef ConstantVal,
                                     LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getTruncOrBitCast(unwrap<Constant>(ConstantVal),
                                              unwrap(ToType)));
}

LLVMValueRef LLVMConstPointerCast(LLVMValueRef ConstantVal,
                                  LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getPointerCast(unwrap<Constant>(ConstantVal),
                                           unwrap(ToType)));
}

LLVMValueRef LLVMConstIntCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType,
                              LLVMBool isSigned) {
  return wrap(ConstantExpr::getIntegerCast(unwrap<Constant>(ConstantVal),
                                           unwrap(ToType), isSigned));
}

LLVMValueRef LLVMConstFPCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPCast(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstSelect(LLVMValueRef ConstantCondition,
                             LLVMValueRef ConstantIfTrue,
                             LLVMValueRef ConstantIfFalse) {
  return wrap(ConstantExpr::getSelect(unwrap<Constant>(ConstantCondition),
                                      unwrap<Constant>(ConstantIfTrue),
                                      unwrap<Constant>(ConstantIfFalse)));
}

LLVMValueRef LLVMConstExtractElement(LLVMValueRef VectorConstant,
                                     LLVMValueRef IndexConstant) {
  return wrap(ConstantExpr::getExtractElement(unwrap<Constant>(VectorConstant),
                                              unwrap<Constant>(IndexConstant)));
}

LLVMValueRef LLVMConstInsertElement(LLVMValueRef VectorConstant,
                                    LLVMValueRef ElementValueConstant,
                                    LLVMValueRef IndexConstant) {
  return wrap(ConstantExpr::getInsertElement(unwrap<Constant>(VectorConstant),
                                         unwrap<Constant>(ElementValueConstant),
                                             unwrap<Constant>(IndexConstant)));
}

LLVMValueRef LLVMConstShuffleVector(LLVMValueRef VectorAConstant,
                                    LLVMValueRef VectorBConstant,
                                    LLVMValueRef MaskConstant) {
  return wrap(ConstantExpr::getShuffleVector(unwrap<Constant>(VectorAConstant),
                                             unwrap<Constant>(VectorBConstant),
                                             unwrap<Constant>(MaskConstant)));
}

LLVMValueRef LLVMConstExtractValue(LLVMValueRef AggConstant, unsigned *IdxList,
                                   unsigned NumIdx) {
  return wrap(ConstantExpr::getExtractValue(unwrap<Constant>(AggConstant),
                                            makeArrayRef(IdxList, NumIdx)));
}

LLVMValueRef LLVMConstInsertValue(LLVMValueRef AggConstant,
                                  LLVMValueRef ElementValueConstant,
                                  unsigned *IdxList, unsigned NumIdx) {
  return wrap(ConstantExpr::getInsertValue(unwrap<Constant>(AggConstant),
                                         unwrap<Constant>(ElementValueConstant),
                                           makeArrayRef(IdxList, NumIdx)));
}

LLVMValueRef LLVMConstInlineAsm(LLVMTypeRef Ty, const char *AsmString,
                                const char *Constraints,
                                LLVMBool HasSideEffects,
                                LLVMBool IsAlignStack) {
  return wrap(InlineAsm::get(dyn_cast<FunctionType>(unwrap(Ty)), AsmString,
                             Constraints, HasSideEffects, IsAlignStack));
}

LLVMValueRef LLVMBlockAddress(LLVMValueRef F, LLVMBasicBlockRef BB) {
  return wrap(BlockAddress::get(unwrap<Function>(F), unwrap(BB)));
}

/*--.. Operations on global variables, functions, and aliases (globals) ....--*/

LLVMModuleRef LLVMGetGlobalParent(LLVMValueRef Global) {
  return wrap(unwrap<GlobalValue>(Global)->getParent());
}

LLVMBool LLVMIsDeclaration(LLVMValueRef Global) {
  return unwrap<GlobalValue>(Global)->isDeclaration();
}

LLVMLinkage LLVMGetLinkage(LLVMValueRef Global) {
  switch (unwrap<GlobalValue>(Global)->getLinkage()) {
  case GlobalValue::ExternalLinkage:
    return LLVMExternalLinkage;
  case GlobalValue::AvailableExternallyLinkage:
    return LLVMAvailableExternallyLinkage;
  case GlobalValue::LinkOnceAnyLinkage:
    return LLVMLinkOnceAnyLinkage;
  case GlobalValue::LinkOnceODRLinkage:
    return LLVMLinkOnceODRLinkage;
  case GlobalValue::WeakAnyLinkage:
    return LLVMWeakAnyLinkage;
  case GlobalValue::WeakODRLinkage:
    return LLVMWeakODRLinkage;
  case GlobalValue::AppendingLinkage:
    return LLVMAppendingLinkage;
  case GlobalValue::InternalLinkage:
    return LLVMInternalLinkage;
  case GlobalValue::PrivateLinkage:
    return LLVMPrivateLinkage;
  case GlobalValue::ExternalWeakLinkage:
    return LLVMExternalWeakLinkage;
  case GlobalValue::CommonLinkage:
    return LLVMCommonLinkage;
  }

  llvm_unreachable("Invalid GlobalValue linkage!");
}

void LLVMSetLinkage(LLVMValueRef Global, LLVMLinkage Linkage) {
  GlobalValue *GV = unwrap<GlobalValue>(Global);

  switch (Linkage) {
  case LLVMExternalLinkage:
    GV->setLinkage(GlobalValue::ExternalLinkage);
    break;
  case LLVMAvailableExternallyLinkage:
    GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
    break;
  case LLVMLinkOnceAnyLinkage:
    GV->setLinkage(GlobalValue::LinkOnceAnyLinkage);
    break;
  case LLVMLinkOnceODRLinkage:
    GV->setLinkage(GlobalValue::LinkOnceODRLinkage);
    break;
  case LLVMLinkOnceODRAutoHideLinkage:
    LLVM_DEBUG(
        errs() << "LLVMSetLinkage(): LLVMLinkOnceODRAutoHideLinkage is no "
                  "longer supported.");
    break;
  case LLVMWeakAnyLinkage:
    GV->setLinkage(GlobalValue::WeakAnyLinkage);
    break;
  case LLVMWeakODRLinkage:
    GV->setLinkage(GlobalValue::WeakODRLinkage);
    break;
  case LLVMAppendingLinkage:
    GV->setLinkage(GlobalValue::AppendingLinkage);
    break;
  case LLVMInternalLinkage:
    GV->setLinkage(GlobalValue::InternalLinkage);
    break;
  case LLVMPrivateLinkage:
    GV->setLinkage(GlobalValue::PrivateLinkage);
    break;
  case LLVMLinkerPrivateLinkage:
    GV->setLinkage(GlobalValue::PrivateLinkage);
    break;
  case LLVMLinkerPrivateWeakLinkage:
    GV->setLinkage(GlobalValue::PrivateLinkage);
    break;
  case LLVMDLLImportLinkage:
    LLVM_DEBUG(
        errs()
        << "LLVMSetLinkage(): LLVMDLLImportLinkage is no longer supported.");
    break;
  case LLVMDLLExportLinkage:
    LLVM_DEBUG(
        errs()
        << "LLVMSetLinkage(): LLVMDLLExportLinkage is no longer supported.");
    break;
  case LLVMExternalWeakLinkage:
    GV->setLinkage(GlobalValue::ExternalWeakLinkage);
    break;
  case LLVMGhostLinkage:
    LLVM_DEBUG(
        errs() << "LLVMSetLinkage(): LLVMGhostLinkage is no longer supported.");
    break;
  case LLVMCommonLinkage:
    GV->setLinkage(GlobalValue::CommonLinkage);
    break;
  }
}

const char *LLVMGetSection(LLVMValueRef Global) {
  // Using .data() is safe because of how GlobalObject::setSection is
  // implemented.
  return unwrap<GlobalValue>(Global)->getSection().data();
}

void LLVMSetSection(LLVMValueRef Global, const char *Section) {
  unwrap<GlobalObject>(Global)->setSection(Section);
}

LLVMVisibility LLVMGetVisibility(LLVMValueRef Global) {
  return static_cast<LLVMVisibility>(
    unwrap<GlobalValue>(Global)->getVisibility());
}

void LLVMSetVisibility(LLVMValueRef Global, LLVMVisibility Viz) {
  unwrap<GlobalValue>(Global)
    ->setVisibility(static_cast<GlobalValue::VisibilityTypes>(Viz));
}

LLVMDLLStorageClass LLVMGetDLLStorageClass(LLVMValueRef Global) {
  return static_cast<LLVMDLLStorageClass>(
      unwrap<GlobalValue>(Global)->getDLLStorageClass());
}

void LLVMSetDLLStorageClass(LLVMValueRef Global, LLVMDLLStorageClass Class) {
  unwrap<GlobalValue>(Global)->setDLLStorageClass(
      static_cast<GlobalValue::DLLStorageClassTypes>(Class));
}

LLVMUnnamedAddr LLVMGetUnnamedAddress(LLVMValueRef Global) {
  switch (unwrap<GlobalValue>(Global)->getUnnamedAddr()) {
  case GlobalVariable::UnnamedAddr::None:
    return LLVMNoUnnamedAddr;
  case GlobalVariable::UnnamedAddr::Local:
    return LLVMLocalUnnamedAddr;
  case GlobalVariable::UnnamedAddr::Global:
    return LLVMGlobalUnnamedAddr;
  }
  llvm_unreachable("Unknown UnnamedAddr kind!");
}

void LLVMSetUnnamedAddress(LLVMValueRef Global, LLVMUnnamedAddr UnnamedAddr) {
  GlobalValue *GV = unwrap<GlobalValue>(Global);

  switch (UnnamedAddr) {
  case LLVMNoUnnamedAddr:
    return GV->setUnnamedAddr(GlobalVariable::UnnamedAddr::None);
  case LLVMLocalUnnamedAddr:
    return GV->setUnnamedAddr(GlobalVariable::UnnamedAddr::Local);
  case LLVMGlobalUnnamedAddr:
    return GV->setUnnamedAddr(GlobalVariable::UnnamedAddr::Global);
  }
}

LLVMBool LLVMHasUnnamedAddr(LLVMValueRef Global) {
  return unwrap<GlobalValue>(Global)->hasGlobalUnnamedAddr();
}

void LLVMSetUnnamedAddr(LLVMValueRef Global, LLVMBool HasUnnamedAddr) {
  unwrap<GlobalValue>(Global)->setUnnamedAddr(
      HasUnnamedAddr ? GlobalValue::UnnamedAddr::Global
                     : GlobalValue::UnnamedAddr::None);
}

LLVMTypeRef LLVMGlobalGetValueType(LLVMValueRef Global) {
  return wrap(unwrap<GlobalValue>(Global)->getValueType());
}

/*--.. Operations on global variables, load and store instructions .........--*/

unsigned LLVMGetAlignment(LLVMValueRef V) {
  Value *P = unwrap<Value>(V);
  if (GlobalValue *GV = dyn_cast<GlobalValue>(P))
    return GV->getAlignment();
  if (AllocaInst *AI = dyn_cast<AllocaInst>(P))
    return AI->getAlignment();
  if (LoadInst *LI = dyn_cast<LoadInst>(P))
    return LI->getAlignment();
  if (StoreInst *SI = dyn_cast<StoreInst>(P))
    return SI->getAlignment();

  llvm_unreachable(
      "only GlobalValue, AllocaInst, LoadInst and StoreInst have alignment");
}

void LLVMSetAlignment(LLVMValueRef V, unsigned Bytes) {
  Value *P = unwrap<Value>(V);
  if (GlobalObject *GV = dyn_cast<GlobalObject>(P))
    GV->setAlignment(Bytes);
  else if (AllocaInst *AI = dyn_cast<AllocaInst>(P))
    AI->setAlignment(Bytes);
  else if (LoadInst *LI = dyn_cast<LoadInst>(P))
    LI->setAlignment(Bytes);
  else if (StoreInst *SI = dyn_cast<StoreInst>(P))
    SI->setAlignment(Bytes);
  else
    llvm_unreachable(
        "only GlobalValue, AllocaInst, LoadInst and StoreInst have alignment");
}

LLVMValueMetadataEntry *LLVMGlobalCopyAllMetadata(LLVMValueRef Value,
                                                  size_t *NumEntries) {
  return llvm_getMetadata(NumEntries, [&Value](MetadataEntries &Entries) {
    if (Instruction *Instr = dyn_cast<Instruction>(unwrap(Value))) {
      Instr->getAllMetadata(Entries);
    } else {
      unwrap<GlobalObject>(Value)->getAllMetadata(Entries);
    }
  });
}

unsigned LLVMValueMetadataEntriesGetKind(LLVMValueMetadataEntry *Entries,
                                         unsigned Index) {
  LLVMOpaqueValueMetadataEntry MVE =
      static_cast<LLVMOpaqueValueMetadataEntry>(Entries[Index]);
  return MVE.Kind;
}

LLVMMetadataRef
LLVMValueMetadataEntriesGetMetadata(LLVMValueMetadataEntry *Entries,
                                    unsigned Index) {
  LLVMOpaqueValueMetadataEntry MVE =
      static_cast<LLVMOpaqueValueMetadataEntry>(Entries[Index]);
  return MVE.Metadata;
}

void LLVMDisposeValueMetadataEntries(LLVMValueMetadataEntry *Entries) {
  free(Entries);
}

void LLVMGlobalSetMetadata(LLVMValueRef Global, unsigned Kind,
                           LLVMMetadataRef MD) {
  unwrap<GlobalObject>(Global)->setMetadata(Kind, unwrap<MDNode>(MD));
}

void LLVMGlobalEraseMetadata(LLVMValueRef Global, unsigned Kind) {
  unwrap<GlobalObject>(Global)->eraseMetadata(Kind);
}

void LLVMGlobalClearMetadata(LLVMValueRef Global) {
  unwrap<GlobalObject>(Global)->clearMetadata();
}

/*--.. Operations on global variables ......................................--*/

LLVMValueRef LLVMAddGlobal(LLVMModuleRef M, LLVMTypeRef Ty, const char *Name) {
  return wrap(new GlobalVariable(*unwrap(M), unwrap(Ty), false,
                                 GlobalValue::ExternalLinkage, nullptr, Name));
}

LLVMValueRef LLVMAddGlobalInAddressSpace(LLVMModuleRef M, LLVMTypeRef Ty,
                                         const char *Name,
                                         unsigned AddressSpace) {
  return wrap(new GlobalVariable(*unwrap(M), unwrap(Ty), false,
                                 GlobalValue::ExternalLinkage, nullptr, Name,
                                 nullptr, GlobalVariable::NotThreadLocal,
                                 AddressSpace));
}

LLVMValueRef LLVMGetNamedGlobal(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getNamedGlobal(Name));
}

LLVMValueRef LLVMGetFirstGlobal(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::global_iterator I = Mod->global_begin();
  if (I == Mod->global_end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetLastGlobal(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::global_iterator I = Mod->global_end();
  if (I == Mod->global_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMGetNextGlobal(LLVMValueRef GlobalVar) {
  GlobalVariable *GV = unwrap<GlobalVariable>(GlobalVar);
  Module::global_iterator I(GV);
  if (++I == GV->getParent()->global_end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetPreviousGlobal(LLVMValueRef GlobalVar) {
  GlobalVariable *GV = unwrap<GlobalVariable>(GlobalVar);
  Module::global_iterator I(GV);
  if (I == GV->getParent()->global_begin())
    return nullptr;
  return wrap(&*--I);
}

void LLVMDeleteGlobal(LLVMValueRef GlobalVar) {
  unwrap<GlobalVariable>(GlobalVar)->eraseFromParent();
}

LLVMValueRef LLVMGetInitializer(LLVMValueRef GlobalVar) {
  GlobalVariable* GV = unwrap<GlobalVariable>(GlobalVar);
  if ( !GV->hasInitializer() )
    return nullptr;
  return wrap(GV->getInitializer());
}

void LLVMSetInitializer(LLVMValueRef GlobalVar, LLVMValueRef ConstantVal) {
  unwrap<GlobalVariable>(GlobalVar)
    ->setInitializer(unwrap<Constant>(ConstantVal));
}

LLVMBool LLVMIsThreadLocal(LLVMValueRef GlobalVar) {
  return unwrap<GlobalVariable>(GlobalVar)->isThreadLocal();
}

void LLVMSetThreadLocal(LLVMValueRef GlobalVar, LLVMBool IsThreadLocal) {
  unwrap<GlobalVariable>(GlobalVar)->setThreadLocal(IsThreadLocal != 0);
}

LLVMBool LLVMIsGlobalConstant(LLVMValueRef GlobalVar) {
  return unwrap<GlobalVariable>(GlobalVar)->isConstant();
}

void LLVMSetGlobalConstant(LLVMValueRef GlobalVar, LLVMBool IsConstant) {
  unwrap<GlobalVariable>(GlobalVar)->setConstant(IsConstant != 0);
}

LLVMThreadLocalMode LLVMGetThreadLocalMode(LLVMValueRef GlobalVar) {
  switch (unwrap<GlobalVariable>(GlobalVar)->getThreadLocalMode()) {
  case GlobalVariable::NotThreadLocal:
    return LLVMNotThreadLocal;
  case GlobalVariable::GeneralDynamicTLSModel:
    return LLVMGeneralDynamicTLSModel;
  case GlobalVariable::LocalDynamicTLSModel:
    return LLVMLocalDynamicTLSModel;
  case GlobalVariable::InitialExecTLSModel:
    return LLVMInitialExecTLSModel;
  case GlobalVariable::LocalExecTLSModel:
    return LLVMLocalExecTLSModel;
  }

  llvm_unreachable("Invalid GlobalVariable thread local mode");
}

void LLVMSetThreadLocalMode(LLVMValueRef GlobalVar, LLVMThreadLocalMode Mode) {
  GlobalVariable *GV = unwrap<GlobalVariable>(GlobalVar);

  switch (Mode) {
  case LLVMNotThreadLocal:
    GV->setThreadLocalMode(GlobalVariable::NotThreadLocal);
    break;
  case LLVMGeneralDynamicTLSModel:
    GV->setThreadLocalMode(GlobalVariable::GeneralDynamicTLSModel);
    break;
  case LLVMLocalDynamicTLSModel:
    GV->setThreadLocalMode(GlobalVariable::LocalDynamicTLSModel);
    break;
  case LLVMInitialExecTLSModel:
    GV->setThreadLocalMode(GlobalVariable::InitialExecTLSModel);
    break;
  case LLVMLocalExecTLSModel:
    GV->setThreadLocalMode(GlobalVariable::LocalExecTLSModel);
    break;
  }
}

LLVMBool LLVMIsExternallyInitialized(LLVMValueRef GlobalVar) {
  return unwrap<GlobalVariable>(GlobalVar)->isExternallyInitialized();
}

void LLVMSetExternallyInitialized(LLVMValueRef GlobalVar, LLVMBool IsExtInit) {
  unwrap<GlobalVariable>(GlobalVar)->setExternallyInitialized(IsExtInit);
}

/*--.. Operations on aliases ......................................--*/

LLVMValueRef LLVMAddAlias(LLVMModuleRef M, LLVMTypeRef Ty, LLVMValueRef Aliasee,
                          const char *Name) {
  auto *PTy = cast<PointerType>(unwrap(Ty));
  return wrap(GlobalAlias::create(PTy->getElementType(), PTy->getAddressSpace(),
                                  GlobalValue::ExternalLinkage, Name,
                                  unwrap<Constant>(Aliasee), unwrap(M)));
}

LLVMValueRef LLVMGetNamedGlobalAlias(LLVMModuleRef M,
                                     const char *Name, size_t NameLen) {
  return wrap(unwrap(M)->getNamedAlias(Name));
}

LLVMValueRef LLVMGetFirstGlobalAlias(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::alias_iterator I = Mod->alias_begin();
  if (I == Mod->alias_end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetLastGlobalAlias(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::alias_iterator I = Mod->alias_end();
  if (I == Mod->alias_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMGetNextGlobalAlias(LLVMValueRef GA) {
  GlobalAlias *Alias = unwrap<GlobalAlias>(GA);
  Module::alias_iterator I(Alias);
  if (++I == Alias->getParent()->alias_end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetPreviousGlobalAlias(LLVMValueRef GA) {
  GlobalAlias *Alias = unwrap<GlobalAlias>(GA);
  Module::alias_iterator I(Alias);
  if (I == Alias->getParent()->alias_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMAliasGetAliasee(LLVMValueRef Alias) {
  return wrap(unwrap<GlobalAlias>(Alias)->getAliasee());
}

void LLVMAliasSetAliasee(LLVMValueRef Alias, LLVMValueRef Aliasee) {
  unwrap<GlobalAlias>(Alias)->setAliasee(unwrap<Constant>(Aliasee));
}

/*--.. Operations on functions .............................................--*/

LLVMValueRef LLVMAddFunction(LLVMModuleRef M, const char *Name,
                             LLVMTypeRef FunctionTy) {
  return wrap(Function::Create(unwrap<FunctionType>(FunctionTy),
                               GlobalValue::ExternalLinkage, Name, unwrap(M)));
}

LLVMValueRef LLVMGetNamedFunction(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getFunction(Name));
}

LLVMValueRef LLVMGetFirstFunction(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::iterator I = Mod->begin();
  if (I == Mod->end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetLastFunction(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::iterator I = Mod->end();
  if (I == Mod->begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMGetNextFunction(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Module::iterator I(Func);
  if (++I == Func->getParent()->end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetPreviousFunction(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Module::iterator I(Func);
  if (I == Func->getParent()->begin())
    return nullptr;
  return wrap(&*--I);
}

void LLVMDeleteFunction(LLVMValueRef Fn) {
  unwrap<Function>(Fn)->eraseFromParent();
}

LLVMBool LLVMHasPersonalityFn(LLVMValueRef Fn) {
  return unwrap<Function>(Fn)->hasPersonalityFn();
}

LLVMValueRef LLVMGetPersonalityFn(LLVMValueRef Fn) {
  return wrap(unwrap<Function>(Fn)->getPersonalityFn());
}

void LLVMSetPersonalityFn(LLVMValueRef Fn, LLVMValueRef PersonalityFn) {
  unwrap<Function>(Fn)->setPersonalityFn(unwrap<Constant>(PersonalityFn));
}

unsigned LLVMGetIntrinsicID(LLVMValueRef Fn) {
  if (Function *F = dyn_cast<Function>(unwrap(Fn)))
    return F->getIntrinsicID();
  return 0;
}

static Intrinsic::ID llvm_map_to_intrinsic_id(unsigned ID) {
  assert(ID < llvm::Intrinsic::num_intrinsics && "Intrinsic ID out of range");
  return llvm::Intrinsic::ID(ID);
}

LLVMValueRef LLVMGetIntrinsicDeclaration(LLVMModuleRef Mod,
                                         unsigned ID,
                                         LLVMTypeRef *ParamTypes,
                                         size_t ParamCount) {
  ArrayRef<Type*> Tys(unwrap(ParamTypes), ParamCount);
  auto IID = llvm_map_to_intrinsic_id(ID);
  return wrap(llvm::Intrinsic::getDeclaration(unwrap(Mod), IID, Tys));
}

const char *LLVMIntrinsicGetName(unsigned ID, size_t *NameLength) {
  auto IID = llvm_map_to_intrinsic_id(ID);
  auto Str = llvm::Intrinsic::getName(IID);
  *NameLength = Str.size();
  return Str.data();
}

LLVMTypeRef LLVMIntrinsicGetType(LLVMContextRef Ctx, unsigned ID,
                                 LLVMTypeRef *ParamTypes, size_t ParamCount) {
  auto IID = llvm_map_to_intrinsic_id(ID);
  ArrayRef<Type*> Tys(unwrap(ParamTypes), ParamCount);
  return wrap(llvm::Intrinsic::getType(*unwrap(Ctx), IID, Tys));
}

const char *LLVMIntrinsicCopyOverloadedName(unsigned ID,
                                            LLVMTypeRef *ParamTypes,
                                            size_t ParamCount,
                                            size_t *NameLength) {
  auto IID = llvm_map_to_intrinsic_id(ID);
  ArrayRef<Type*> Tys(unwrap(ParamTypes), ParamCount);
  auto Str = llvm::Intrinsic::getName(IID, Tys);
  *NameLength = Str.length();
  return strdup(Str.c_str());
}

LLVMBool LLVMIntrinsicIsOverloaded(unsigned ID) {
  auto IID = llvm_map_to_intrinsic_id(ID);
  return llvm::Intrinsic::isOverloaded(IID);
}

unsigned LLVMGetFunctionCallConv(LLVMValueRef Fn) {
  return unwrap<Function>(Fn)->getCallingConv();
}

void LLVMSetFunctionCallConv(LLVMValueRef Fn, unsigned CC) {
  return unwrap<Function>(Fn)->setCallingConv(
    static_cast<CallingConv::ID>(CC));
}

const char *LLVMGetGC(LLVMValueRef Fn) {
  Function *F = unwrap<Function>(Fn);
  return F->hasGC()? F->getGC().c_str() : nullptr;
}

void LLVMSetGC(LLVMValueRef Fn, const char *GC) {
  Function *F = unwrap<Function>(Fn);
  if (GC)
    F->setGC(GC);
  else
    F->clearGC();
}

void LLVMAddAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                             LLVMAttributeRef A) {
  unwrap<Function>(F)->addAttribute(Idx, unwrap(A));
}

unsigned LLVMGetAttributeCountAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx) {
  auto AS = unwrap<Function>(F)->getAttributes().getAttributes(Idx);
  return AS.getNumAttributes();
}

void LLVMGetAttributesAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                              LLVMAttributeRef *Attrs) {
  auto AS = unwrap<Function>(F)->getAttributes().getAttributes(Idx);
  for (auto A : AS)
    *Attrs++ = wrap(A);
}

LLVMAttributeRef LLVMGetEnumAttributeAtIndex(LLVMValueRef F,
                                             LLVMAttributeIndex Idx,
                                             unsigned KindID) {
  return wrap(unwrap<Function>(F)->getAttribute(Idx,
                                                (Attribute::AttrKind)KindID));
}

LLVMAttributeRef LLVMGetStringAttributeAtIndex(LLVMValueRef F,
                                               LLVMAttributeIndex Idx,
                                               const char *K, unsigned KLen) {
  return wrap(unwrap<Function>(F)->getAttribute(Idx, StringRef(K, KLen)));
}

void LLVMRemoveEnumAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                                    unsigned KindID) {
  unwrap<Function>(F)->removeAttribute(Idx, (Attribute::AttrKind)KindID);
}

void LLVMRemoveStringAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                                      const char *K, unsigned KLen) {
  unwrap<Function>(F)->removeAttribute(Idx, StringRef(K, KLen));
}

void LLVMAddTargetDependentFunctionAttr(LLVMValueRef Fn, const char *A,
                                        const char *V) {
  Function *Func = unwrap<Function>(Fn);
  Attribute Attr = Attribute::get(Func->getContext(), A, V);
  Func->addAttribute(AttributeList::FunctionIndex, Attr);
}

/*--.. Operations on parameters ............................................--*/

unsigned LLVMCountParams(LLVMValueRef FnRef) {
  // This function is strictly redundant to
  //   LLVMCountParamTypes(LLVMGetElementType(LLVMTypeOf(FnRef)))
  return unwrap<Function>(FnRef)->arg_size();
}

void LLVMGetParams(LLVMValueRef FnRef, LLVMValueRef *ParamRefs) {
  Function *Fn = unwrap<Function>(FnRef);
  for (Function::arg_iterator I = Fn->arg_begin(),
                              E = Fn->arg_end(); I != E; I++)
    *ParamRefs++ = wrap(&*I);
}

LLVMValueRef LLVMGetParam(LLVMValueRef FnRef, unsigned index) {
  Function *Fn = unwrap<Function>(FnRef);
  return wrap(&Fn->arg_begin()[index]);
}

LLVMValueRef LLVMGetParamParent(LLVMValueRef V) {
  return wrap(unwrap<Argument>(V)->getParent());
}

LLVMValueRef LLVMGetFirstParam(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::arg_iterator I = Func->arg_begin();
  if (I == Func->arg_end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetLastParam(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::arg_iterator I = Func->arg_end();
  if (I == Func->arg_begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMGetNextParam(LLVMValueRef Arg) {
  Argument *A = unwrap<Argument>(Arg);
  Function *Fn = A->getParent();
  if (A->getArgNo() + 1 >= Fn->arg_size())
    return nullptr;
  return wrap(&Fn->arg_begin()[A->getArgNo() + 1]);
}

LLVMValueRef LLVMGetPreviousParam(LLVMValueRef Arg) {
  Argument *A = unwrap<Argument>(Arg);
  if (A->getArgNo() == 0)
    return nullptr;
  return wrap(&A->getParent()->arg_begin()[A->getArgNo() - 1]);
}

void LLVMSetParamAlignment(LLVMValueRef Arg, unsigned align) {
  Argument *A = unwrap<Argument>(Arg);
  A->addAttr(Attribute::getWithAlignment(A->getContext(), align));
}

/*--.. Operations on basic blocks ..........................................--*/

LLVMValueRef LLVMBasicBlockAsValue(LLVMBasicBlockRef BB) {
  return wrap(static_cast<Value*>(unwrap(BB)));
}

LLVMBool LLVMValueIsBasicBlock(LLVMValueRef Val) {
  return isa<BasicBlock>(unwrap(Val));
}

LLVMBasicBlockRef LLVMValueAsBasicBlock(LLVMValueRef Val) {
  return wrap(unwrap<BasicBlock>(Val));
}

const char *LLVMGetBasicBlockName(LLVMBasicBlockRef BB) {
  return unwrap(BB)->getName().data();
}

LLVMValueRef LLVMGetBasicBlockParent(LLVMBasicBlockRef BB) {
  return wrap(unwrap(BB)->getParent());
}

LLVMValueRef LLVMGetBasicBlockTerminator(LLVMBasicBlockRef BB) {
  return wrap(unwrap(BB)->getTerminator());
}

unsigned LLVMCountBasicBlocks(LLVMValueRef FnRef) {
  return unwrap<Function>(FnRef)->size();
}

void LLVMGetBasicBlocks(LLVMValueRef FnRef, LLVMBasicBlockRef *BasicBlocksRefs){
  Function *Fn = unwrap<Function>(FnRef);
  for (BasicBlock &BB : *Fn)
    *BasicBlocksRefs++ = wrap(&BB);
}

LLVMBasicBlockRef LLVMGetEntryBasicBlock(LLVMValueRef Fn) {
  return wrap(&unwrap<Function>(Fn)->getEntryBlock());
}

LLVMBasicBlockRef LLVMGetFirstBasicBlock(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::iterator I = Func->begin();
  if (I == Func->end())
    return nullptr;
  return wrap(&*I);
}

LLVMBasicBlockRef LLVMGetLastBasicBlock(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::iterator I = Func->end();
  if (I == Func->begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMBasicBlockRef LLVMGetNextBasicBlock(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  Function::iterator I(Block);
  if (++I == Block->getParent()->end())
    return nullptr;
  return wrap(&*I);
}

LLVMBasicBlockRef LLVMGetPreviousBasicBlock(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  Function::iterator I(Block);
  if (I == Block->getParent()->begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMBasicBlockRef LLVMCreateBasicBlockInContext(LLVMContextRef C,
                                                const char *Name) {
  return wrap(llvm::BasicBlock::Create(*unwrap(C), Name));
}

LLVMBasicBlockRef LLVMAppendBasicBlockInContext(LLVMContextRef C,
                                                LLVMValueRef FnRef,
                                                const char *Name) {
  return wrap(BasicBlock::Create(*unwrap(C), Name, unwrap<Function>(FnRef)));
}

LLVMBasicBlockRef LLVMAppendBasicBlock(LLVMValueRef FnRef, const char *Name) {
  return LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), FnRef, Name);
}

LLVMBasicBlockRef LLVMInsertBasicBlockInContext(LLVMContextRef C,
                                                LLVMBasicBlockRef BBRef,
                                                const char *Name) {
  BasicBlock *BB = unwrap(BBRef);
  return wrap(BasicBlock::Create(*unwrap(C), Name, BB->getParent(), BB));
}

LLVMBasicBlockRef LLVMInsertBasicBlock(LLVMBasicBlockRef BBRef,
                                       const char *Name) {
  return LLVMInsertBasicBlockInContext(LLVMGetGlobalContext(), BBRef, Name);
}

void LLVMDeleteBasicBlock(LLVMBasicBlockRef BBRef) {
  unwrap(BBRef)->eraseFromParent();
}

void LLVMRemoveBasicBlockFromParent(LLVMBasicBlockRef BBRef) {
  unwrap(BBRef)->removeFromParent();
}

void LLVMMoveBasicBlockBefore(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos) {
  unwrap(BB)->moveBefore(unwrap(MovePos));
}

void LLVMMoveBasicBlockAfter(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos) {
  unwrap(BB)->moveAfter(unwrap(MovePos));
}

/*--.. Operations on instructions ..........................................--*/

LLVMBasicBlockRef LLVMGetInstructionParent(LLVMValueRef Inst) {
  return wrap(unwrap<Instruction>(Inst)->getParent());
}

LLVMValueRef LLVMGetFirstInstruction(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  BasicBlock::iterator I = Block->begin();
  if (I == Block->end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetLastInstruction(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  BasicBlock::iterator I = Block->end();
  if (I == Block->begin())
    return nullptr;
  return wrap(&*--I);
}

LLVMValueRef LLVMGetNextInstruction(LLVMValueRef Inst) {
  Instruction *Instr = unwrap<Instruction>(Inst);
  BasicBlock::iterator I(Instr);
  if (++I == Instr->getParent()->end())
    return nullptr;
  return wrap(&*I);
}

LLVMValueRef LLVMGetPreviousInstruction(LLVMValueRef Inst) {
  Instruction *Instr = unwrap<Instruction>(Inst);
  BasicBlock::iterator I(Instr);
  if (I == Instr->getParent()->begin())
    return nullptr;
  return wrap(&*--I);
}

void LLVMInstructionRemoveFromParent(LLVMValueRef Inst) {
  unwrap<Instruction>(Inst)->removeFromParent();
}

void LLVMInstructionEraseFromParent(LLVMValueRef Inst) {
  unwrap<Instruction>(Inst)->eraseFromParent();
}

LLVMIntPredicate LLVMGetICmpPredicate(LLVMValueRef Inst) {
  if (ICmpInst *I = dyn_cast<ICmpInst>(unwrap(Inst)))
    return (LLVMIntPredicate)I->getPredicate();
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(unwrap(Inst)))
    if (CE->getOpcode() == Instruction::ICmp)
      return (LLVMIntPredicate)CE->getPredicate();
  return (LLVMIntPredicate)0;
}

LLVMRealPredicate LLVMGetFCmpPredicate(LLVMValueRef Inst) {
  if (FCmpInst *I = dyn_cast<FCmpInst>(unwrap(Inst)))
    return (LLVMRealPredicate)I->getPredicate();
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(unwrap(Inst)))
    if (CE->getOpcode() == Instruction::FCmp)
      return (LLVMRealPredicate)CE->getPredicate();
  return (LLVMRealPredicate)0;
}

LLVMOpcode LLVMGetInstructionOpcode(LLVMValueRef Inst) {
  if (Instruction *C = dyn_cast<Instruction>(unwrap(Inst)))
    return map_to_llvmopcode(C->getOpcode());
  return (LLVMOpcode)0;
}

LLVMValueRef LLVMInstructionClone(LLVMValueRef Inst) {
  if (Instruction *C = dyn_cast<Instruction>(unwrap(Inst)))
    return wrap(C->clone());
  return nullptr;
}

LLVMValueRef LLVMIsATerminatorInst(LLVMValueRef Inst) {
  Instruction *I = dyn_cast<Instruction>(unwrap(Inst));
  return (I && I->isTerminator()) ? wrap(I) : nullptr;
}

unsigned LLVMGetNumArgOperands(LLVMValueRef Instr) {
  if (FuncletPadInst *FPI = dyn_cast<FuncletPadInst>(unwrap(Instr))) {
    return FPI->getNumArgOperands();
  }
  return unwrap<CallBase>(Instr)->getNumArgOperands();
}

/*--.. Call and invoke instructions ........................................--*/

unsigned LLVMGetInstructionCallConv(LLVMValueRef Instr) {
  return unwrap<CallBase>(Instr)->getCallingConv();
}

void LLVMSetInstructionCallConv(LLVMValueRef Instr, unsigned CC) {
  return unwrap<CallBase>(Instr)->setCallingConv(
      static_cast<CallingConv::ID>(CC));
}

void LLVMSetInstrParamAlignment(LLVMValueRef Instr, unsigned index,
                                unsigned align) {
  auto *Call = unwrap<CallBase>(Instr);
  Attribute AlignAttr = Attribute::getWithAlignment(Call->getContext(), align);
  Call->addAttribute(index, AlignAttr);
}

void LLVMAddCallSiteAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                              LLVMAttributeRef A) {
  unwrap<CallBase>(C)->addAttribute(Idx, unwrap(A));
}

unsigned LLVMGetCallSiteAttributeCount(LLVMValueRef C,
                                       LLVMAttributeIndex Idx) {
  auto *Call = unwrap<CallBase>(C);
  auto AS = Call->getAttributes().getAttributes(Idx);
  return AS.getNumAttributes();
}

void LLVMGetCallSiteAttributes(LLVMValueRef C, LLVMAttributeIndex Idx,
                               LLVMAttributeRef *Attrs) {
  auto *Call = unwrap<CallBase>(C);
  auto AS = Call->getAttributes().getAttributes(Idx);
  for (auto A : AS)
    *Attrs++ = wrap(A);
}

LLVMAttributeRef LLVMGetCallSiteEnumAttribute(LLVMValueRef C,
                                              LLVMAttributeIndex Idx,
                                              unsigned KindID) {
  return wrap(
      unwrap<CallBase>(C)->getAttribute(Idx, (Attribute::AttrKind)KindID));
}

LLVMAttributeRef LLVMGetCallSiteStringAttribute(LLVMValueRef C,
                                                LLVMAttributeIndex Idx,
                                                const char *K, unsigned KLen) {
  return wrap(unwrap<CallBase>(C)->getAttribute(Idx, StringRef(K, KLen)));
}

void LLVMRemoveCallSiteEnumAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                                     unsigned KindID) {
  unwrap<CallBase>(C)->removeAttribute(Idx, (Attribute::AttrKind)KindID);
}

void LLVMRemoveCallSiteStringAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                                       const char *K, unsigned KLen) {
  unwrap<CallBase>(C)->removeAttribute(Idx, StringRef(K, KLen));
}

LLVMValueRef LLVMGetCalledValue(LLVMValueRef Instr) {
  return wrap(unwrap<CallBase>(Instr)->getCalledValue());
}

LLVMTypeRef LLVMGetCalledFunctionType(LLVMValueRef Instr) {
  return wrap(unwrap<CallBase>(Instr)->getFunctionType());
}

/*--.. Operations on call instructions (only) ..............................--*/

LLVMBool LLVMIsTailCall(LLVMValueRef Call) {
  return unwrap<CallInst>(Call)->isTailCall();
}

void LLVMSetTailCall(LLVMValueRef Call, LLVMBool isTailCall) {
  unwrap<CallInst>(Call)->setTailCall(isTailCall);
}

/*--.. Operations on invoke instructions (only) ............................--*/

LLVMBasicBlockRef LLVMGetNormalDest(LLVMValueRef Invoke) {
  return wrap(unwrap<InvokeInst>(Invoke)->getNormalDest());
}

LLVMBasicBlockRef LLVMGetUnwindDest(LLVMValueRef Invoke) {
  if (CleanupReturnInst *CRI = dyn_cast<CleanupReturnInst>(unwrap(Invoke))) {
    return wrap(CRI->getUnwindDest());
  } else if (CatchSwitchInst *CSI = dyn_cast<CatchSwitchInst>(unwrap(Invoke))) {
    return wrap(CSI->getUnwindDest());
  }
  return wrap(unwrap<InvokeInst>(Invoke)->getUnwindDest());
}

void LLVMSetNormalDest(LLVMValueRef Invoke, LLVMBasicBlockRef B) {
  unwrap<InvokeInst>(Invoke)->setNormalDest(unwrap(B));
}

void LLVMSetUnwindDest(LLVMValueRef Invoke, LLVMBasicBlockRef B) {
  if (CleanupReturnInst *CRI = dyn_cast<CleanupReturnInst>(unwrap(Invoke))) {
    return CRI->setUnwindDest(unwrap(B));
  } else if (CatchSwitchInst *CSI = dyn_cast<CatchSwitchInst>(unwrap(Invoke))) {
    return CSI->setUnwindDest(unwrap(B));
  }
  unwrap<InvokeInst>(Invoke)->setUnwindDest(unwrap(B));
}

/*--.. Operations on terminators ...........................................--*/

unsigned LLVMGetNumSuccessors(LLVMValueRef Term) {
  return unwrap<Instruction>(Term)->getNumSuccessors();
}

LLVMBasicBlockRef LLVMGetSuccessor(LLVMValueRef Term, unsigned i) {
  return wrap(unwrap<Instruction>(Term)->getSuccessor(i));
}

void LLVMSetSuccessor(LLVMValueRef Term, unsigned i, LLVMBasicBlockRef block) {
  return unwrap<Instruction>(Term)->setSuccessor(i, unwrap(block));
}

/*--.. Operations on branch instructions (only) ............................--*/

LLVMBool LLVMIsConditional(LLVMValueRef Branch) {
  return unwrap<BranchInst>(Branch)->isConditional();
}

LLVMValueRef LLVMGetCondition(LLVMValueRef Branch) {
  return wrap(unwrap<BranchInst>(Branch)->getCondition());
}

void LLVMSetCondition(LLVMValueRef Branch, LLVMValueRef Cond) {
  return unwrap<BranchInst>(Branch)->setCondition(unwrap(Cond));
}

/*--.. Operations on switch instructions (only) ............................--*/

LLVMBasicBlockRef LLVMGetSwitchDefaultDest(LLVMValueRef Switch) {
  return wrap(unwrap<SwitchInst>(Switch)->getDefaultDest());
}

/*--.. Operations on alloca instructions (only) ............................--*/

LLVMTypeRef LLVMGetAllocatedType(LLVMValueRef Alloca) {
  return wrap(unwrap<AllocaInst>(Alloca)->getAllocatedType());
}

/*--.. Operations on gep instructions (only) ...............................--*/

LLVMBool LLVMIsInBounds(LLVMValueRef GEP) {
  return unwrap<GetElementPtrInst>(GEP)->isInBounds();
}

void LLVMSetIsInBounds(LLVMValueRef GEP, LLVMBool InBounds) {
  return unwrap<GetElementPtrInst>(GEP)->setIsInBounds(InBounds);
}

/*--.. Operations on phi nodes .............................................--*/

void LLVMAddIncoming(LLVMValueRef PhiNode, LLVMValueRef *IncomingValues,
                     LLVMBasicBlockRef *IncomingBlocks, unsigned Count) {
  PHINode *PhiVal = unwrap<PHINode>(PhiNode);
  for (unsigned I = 0; I != Count; ++I)
    PhiVal->addIncoming(unwrap(IncomingValues[I]), unwrap(IncomingBlocks[I]));
}

unsigned LLVMCountIncoming(LLVMValueRef PhiNode) {
  return unwrap<PHINode>(PhiNode)->getNumIncomingValues();
}

LLVMValueRef LLVMGetIncomingValue(LLVMValueRef PhiNode, unsigned Index) {
  return wrap(unwrap<PHINode>(PhiNode)->getIncomingValue(Index));
}

LLVMBasicBlockRef LLVMGetIncomingBlock(LLVMValueRef PhiNode, unsigned Index) {
  return wrap(unwrap<PHINode>(PhiNode)->getIncomingBlock(Index));
}

/*--.. Operations on extractvalue and insertvalue nodes ....................--*/

unsigned LLVMGetNumIndices(LLVMValueRef Inst) {
  auto *I = unwrap(Inst);
  if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
    return GEP->getNumIndices();
  if (auto *EV = dyn_cast<ExtractValueInst>(I))
    return EV->getNumIndices();
  if (auto *IV = dyn_cast<InsertValueInst>(I))
    return IV->getNumIndices();
  if (auto *CE = dyn_cast<ConstantExpr>(I))
    return CE->getIndices().size();
  llvm_unreachable(
    "LLVMGetNumIndices applies only to extractvalue and insertvalue!");
}

const unsigned *LLVMGetIndices(LLVMValueRef Inst) {
  auto *I = unwrap(Inst);
  if (auto *EV = dyn_cast<ExtractValueInst>(I))
    return EV->getIndices().data();
  if (auto *IV = dyn_cast<InsertValueInst>(I))
    return IV->getIndices().data();
  if (auto *CE = dyn_cast<ConstantExpr>(I))
    return CE->getIndices().data();
  llvm_unreachable(
    "LLVMGetIndices applies only to extractvalue and insertvalue!");
}


/*===-- Instruction builders ----------------------------------------------===*/

LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef C) {
  return wrap(new IRBuilder<>(*unwrap(C)));
}

LLVMBuilderRef LLVMCreateBuilder(void) {
  return LLVMCreateBuilderInContext(LLVMGetGlobalContext());
}

void LLVMPositionBuilder(LLVMBuilderRef Builder, LLVMBasicBlockRef Block,
                         LLVMValueRef Instr) {
  BasicBlock *BB = unwrap(Block);
  auto I = Instr ? unwrap<Instruction>(Instr)->getIterator() : BB->end();
  unwrap(Builder)->SetInsertPoint(BB, I);
}

void LLVMPositionBuilderBefore(LLVMBuilderRef Builder, LLVMValueRef Instr) {
  Instruction *I = unwrap<Instruction>(Instr);
  unwrap(Builder)->SetInsertPoint(I->getParent(), I->getIterator());
}

void LLVMPositionBuilderAtEnd(LLVMBuilderRef Builder, LLVMBasicBlockRef Block) {
  BasicBlock *BB = unwrap(Block);
  unwrap(Builder)->SetInsertPoint(BB);
}

LLVMBasicBlockRef LLVMGetInsertBlock(LLVMBuilderRef Builder) {
   return wrap(unwrap(Builder)->GetInsertBlock());
}

void LLVMClearInsertionPosition(LLVMBuilderRef Builder) {
  unwrap(Builder)->ClearInsertionPoint();
}

void LLVMInsertIntoBuilder(LLVMBuilderRef Builder, LLVMValueRef Instr) {
  unwrap(Builder)->Insert(unwrap<Instruction>(Instr));
}

void LLVMInsertIntoBuilderWithName(LLVMBuilderRef Builder, LLVMValueRef Instr,
                                   const char *Name) {
  unwrap(Builder)->Insert(unwrap<Instruction>(Instr), Name);
}

void LLVMDisposeBuilder(LLVMBuilderRef Builder) {
  delete unwrap(Builder);
}

/*--.. Metadata builders ...................................................--*/

void LLVMSetCurrentDebugLocation(LLVMBuilderRef Builder, LLVMValueRef L) {
  MDNode *Loc =
      L ? cast<MDNode>(unwrap<MetadataAsValue>(L)->getMetadata()) : nullptr;
  unwrap(Builder)->SetCurrentDebugLocation(DebugLoc(Loc));
}

LLVMValueRef LLVMGetCurrentDebugLocation(LLVMBuilderRef Builder) {
  LLVMContext &Context = unwrap(Builder)->getContext();
  return wrap(MetadataAsValue::get(
      Context, unwrap(Builder)->getCurrentDebugLocation().getAsMDNode()));
}

void LLVMSetInstDebugLocation(LLVMBuilderRef Builder, LLVMValueRef Inst) {
  unwrap(Builder)->SetInstDebugLocation(unwrap<Instruction>(Inst));
}


/*--.. Instruction builders ................................................--*/

LLVMValueRef LLVMBuildRetVoid(LLVMBuilderRef B) {
  return wrap(unwrap(B)->CreateRetVoid());
}

LLVMValueRef LLVMBuildRet(LLVMBuilderRef B, LLVMValueRef V) {
  return wrap(unwrap(B)->CreateRet(unwrap(V)));
}

LLVMValueRef LLVMBuildAggregateRet(LLVMBuilderRef B, LLVMValueRef *RetVals,
                                   unsigned N) {
  return wrap(unwrap(B)->CreateAggregateRet(unwrap(RetVals), N));
}

LLVMValueRef LLVMBuildBr(LLVMBuilderRef B, LLVMBasicBlockRef Dest) {
  return wrap(unwrap(B)->CreateBr(unwrap(Dest)));
}

LLVMValueRef LLVMBuildCondBr(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Else) {
  return wrap(unwrap(B)->CreateCondBr(unwrap(If), unwrap(Then), unwrap(Else)));
}

LLVMValueRef LLVMBuildSwitch(LLVMBuilderRef B, LLVMValueRef V,
                             LLVMBasicBlockRef Else, unsigned NumCases) {
  return wrap(unwrap(B)->CreateSwitch(unwrap(V), unwrap(Else), NumCases));
}

LLVMValueRef LLVMBuildIndirectBr(LLVMBuilderRef B, LLVMValueRef Addr,
                                 unsigned NumDests) {
  return wrap(unwrap(B)->CreateIndirectBr(unwrap(Addr), NumDests));
}

LLVMValueRef LLVMBuildInvoke(LLVMBuilderRef B, LLVMValueRef Fn,
                             LLVMValueRef *Args, unsigned NumArgs,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
                             const char *Name) {
  Value *V = unwrap(Fn);
  FunctionType *FnT =
      cast<FunctionType>(cast<PointerType>(V->getType())->getElementType());

  return wrap(
      unwrap(B)->CreateInvoke(FnT, unwrap(Fn), unwrap(Then), unwrap(Catch),
                              makeArrayRef(unwrap(Args), NumArgs), Name));
}

LLVMValueRef LLVMBuildInvoke2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Fn,
                              LLVMValueRef *Args, unsigned NumArgs,
                              LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
                              const char *Name) {
  return wrap(unwrap(B)->CreateInvoke(
      unwrap<FunctionType>(Ty), unwrap(Fn), unwrap(Then), unwrap(Catch),
      makeArrayRef(unwrap(Args), NumArgs), Name));
}

LLVMValueRef LLVMBuildLandingPad(LLVMBuilderRef B, LLVMTypeRef Ty,
                                 LLVMValueRef PersFn, unsigned NumClauses,
                                 const char *Name) {
  // The personality used to live on the landingpad instruction, but now it
  // lives on the parent function. For compatibility, take the provided
  // personality and put it on the parent function.
  if (PersFn)
    unwrap(B)->GetInsertBlock()->getParent()->setPersonalityFn(
        cast<Function>(unwrap(PersFn)));
  return wrap(unwrap(B)->CreateLandingPad(unwrap(Ty), NumClauses, Name));
}

LLVMValueRef LLVMBuildCatchPad(LLVMBuilderRef B, LLVMValueRef ParentPad,
                               LLVMValueRef *Args, unsigned NumArgs,
                               const char *Name) {
  return wrap(unwrap(B)->CreateCatchPad(unwrap(ParentPad),
                                        makeArrayRef(unwrap(Args), NumArgs),
                                        Name));
}

LLVMValueRef LLVMBuildCleanupPad(LLVMBuilderRef B, LLVMValueRef ParentPad,
                                 LLVMValueRef *Args, unsigned NumArgs,
                                 const char *Name) {
  if (ParentPad == nullptr) {
    Type *Ty = Type::getTokenTy(unwrap(B)->getContext());
    ParentPad = wrap(Constant::getNullValue(Ty));
  }
  return wrap(unwrap(B)->CreateCleanupPad(unwrap(ParentPad),
                                          makeArrayRef(unwrap(Args), NumArgs),
                                          Name));
}

LLVMValueRef LLVMBuildResume(LLVMBuilderRef B, LLVMValueRef Exn) {
  return wrap(unwrap(B)->CreateResume(unwrap(Exn)));
}

LLVMValueRef LLVMBuildCatchSwitch(LLVMBuilderRef B, LLVMValueRef ParentPad,
                                  LLVMBasicBlockRef UnwindBB,
                                  unsigned NumHandlers, const char *Name) {
  if (ParentPad == nullptr) {
    Type *Ty = Type::getTokenTy(unwrap(B)->getContext());
    ParentPad = wrap(Constant::getNullValue(Ty));
  }
  return wrap(unwrap(B)->CreateCatchSwitch(unwrap(ParentPad), unwrap(UnwindBB),
                                           NumHandlers, Name));
}

LLVMValueRef LLVMBuildCatchRet(LLVMBuilderRef B, LLVMValueRef CatchPad,
                               LLVMBasicBlockRef BB) {
  return wrap(unwrap(B)->CreateCatchRet(unwrap<CatchPadInst>(CatchPad),
                                        unwrap(BB)));
}

LLVMValueRef LLVMBuildCleanupRet(LLVMBuilderRef B, LLVMValueRef CatchPad,
                                 LLVMBasicBlockRef BB) {
  return wrap(unwrap(B)->CreateCleanupRet(unwrap<CleanupPadInst>(CatchPad),
                                          unwrap(BB)));
}

LLVMValueRef LLVMBuildUnreachable(LLVMBuilderRef B) {
  return wrap(unwrap(B)->CreateUnreachable());
}

void LLVMAddCase(LLVMValueRef Switch, LLVMValueRef OnVal,
                 LLVMBasicBlockRef Dest) {
  unwrap<SwitchInst>(Switch)->addCase(unwrap<ConstantInt>(OnVal), unwrap(Dest));
}

void LLVMAddDestination(LLVMValueRef IndirectBr, LLVMBasicBlockRef Dest) {
  unwrap<IndirectBrInst>(IndirectBr)->addDestination(unwrap(Dest));
}

unsigned LLVMGetNumClauses(LLVMValueRef LandingPad) {
  return unwrap<LandingPadInst>(LandingPad)->getNumClauses();
}

LLVMValueRef LLVMGetClause(LLVMValueRef LandingPad, unsigned Idx) {
  return wrap(unwrap<LandingPadInst>(LandingPad)->getClause(Idx));
}

void LLVMAddClause(LLVMValueRef LandingPad, LLVMValueRef ClauseVal) {
  unwrap<LandingPadInst>(LandingPad)->
    addClause(cast<Constant>(unwrap(ClauseVal)));
}

LLVMBool LLVMIsCleanup(LLVMValueRef LandingPad) {
  return unwrap<LandingPadInst>(LandingPad)->isCleanup();
}

void LLVMSetCleanup(LLVMValueRef LandingPad, LLVMBool Val) {
  unwrap<LandingPadInst>(LandingPad)->setCleanup(Val);
}

void LLVMAddHandler(LLVMValueRef CatchSwitch, LLVMBasicBlockRef Dest) {
  unwrap<CatchSwitchInst>(CatchSwitch)->addHandler(unwrap(Dest));
}

unsigned LLVMGetNumHandlers(LLVMValueRef CatchSwitch) {
  return unwrap<CatchSwitchInst>(CatchSwitch)->getNumHandlers();
}

void LLVMGetHandlers(LLVMValueRef CatchSwitch, LLVMBasicBlockRef *Handlers) {
  CatchSwitchInst *CSI = unwrap<CatchSwitchInst>(CatchSwitch);
  for (CatchSwitchInst::handler_iterator I = CSI->handler_begin(),
                                         E = CSI->handler_end(); I != E; ++I)
    *Handlers++ = wrap(*I);
}

LLVMValueRef LLVMGetParentCatchSwitch(LLVMValueRef CatchPad) {
  return wrap(unwrap<CatchPadInst>(CatchPad)->getCatchSwitch());
}

void LLVMSetParentCatchSwitch(LLVMValueRef CatchPad, LLVMValueRef CatchSwitch) {
  unwrap<CatchPadInst>(CatchPad)
    ->setCatchSwitch(unwrap<CatchSwitchInst>(CatchSwitch));
}

/*--.. Funclets ...........................................................--*/

LLVMValueRef LLVMGetArgOperand(LLVMValueRef Funclet, unsigned i) {
  return wrap(unwrap<FuncletPadInst>(Funclet)->getArgOperand(i));
}

void LLVMSetArgOperand(LLVMValueRef Funclet, unsigned i, LLVMValueRef value) {
  unwrap<FuncletPadInst>(Funclet)->setArgOperand(i, unwrap(value));
}

/*--.. Arithmetic ..........................................................--*/

LLVMValueRef LLVMBuildAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildUDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateUDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildExactUDiv(LLVMBuilderRef B, LLVMValueRef LHS,
                                LLVMValueRef RHS, const char *Name) {
  return wrap(unwrap(B)->CreateExactUDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateSDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildExactSDiv(LLVMBuilderRef B, LLVMValueRef LHS,
                                LLVMValueRef RHS, const char *Name) {
  return wrap(unwrap(B)->CreateExactSDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildURem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateURem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateSRem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFRem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildShl(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateShl(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildLShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateLShr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildAShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateAShr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildAnd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateAnd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildOr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                         const char *Name) {
  return wrap(unwrap(B)->CreateOr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildXor(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateXor(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildBinOp(LLVMBuilderRef B, LLVMOpcode Op,
                            LLVMValueRef LHS, LLVMValueRef RHS,
                            const char *Name) {
  return wrap(unwrap(B)->CreateBinOp(Instruction::BinaryOps(map_from_llvmopcode(Op)), unwrap(LHS),
                                     unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNSWNeg(LLVMBuilderRef B, LLVMValueRef V,
                             const char *Name) {
  return wrap(unwrap(B)->CreateNSWNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNUWNeg(LLVMBuilderRef B, LLVMValueRef V,
                             const char *Name) {
  return wrap(unwrap(B)->CreateNUWNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildFNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateFNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNot(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateNot(unwrap(V), Name));
}

/*--.. Memory ..............................................................--*/

LLVMValueRef LLVMBuildMalloc(LLVMBuilderRef B, LLVMTypeRef Ty,
                             const char *Name) {
  Type* ITy = Type::getInt32Ty(unwrap(B)->GetInsertBlock()->getContext());
  Constant* AllocSize = ConstantExpr::getSizeOf(unwrap(Ty));
  AllocSize = ConstantExpr::getTruncOrBitCast(AllocSize, ITy);
  Instruction* Malloc = CallInst::CreateMalloc(unwrap(B)->GetInsertBlock(),
                                               ITy, unwrap(Ty), AllocSize,
                                               nullptr, nullptr, "");
  return wrap(unwrap(B)->Insert(Malloc, Twine(Name)));
}

LLVMValueRef LLVMBuildArrayMalloc(LLVMBuilderRef B, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name) {
  Type* ITy = Type::getInt32Ty(unwrap(B)->GetInsertBlock()->getContext());
  Constant* AllocSize = ConstantExpr::getSizeOf(unwrap(Ty));
  AllocSize = ConstantExpr::getTruncOrBitCast(AllocSize, ITy);
  Instruction* Malloc = CallInst::CreateMalloc(unwrap(B)->GetInsertBlock(),
                                               ITy, unwrap(Ty), AllocSize,
                                               unwrap(Val), nullptr, "");
  return wrap(unwrap(B)->Insert(Malloc, Twine(Name)));
}

LLVMValueRef LLVMBuildMemSet(LLVMBuilderRef B, LLVMValueRef Ptr, 
                             LLVMValueRef Val, LLVMValueRef Len,
                             unsigned Align) {
  return wrap(unwrap(B)->CreateMemSet(unwrap(Ptr), unwrap(Val), unwrap(Len), Align));
}

LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef B, 
                             LLVMValueRef Dst, unsigned DstAlign,
                             LLVMValueRef Src, unsigned SrcAlign,
                             LLVMValueRef Size) {
  return wrap(unwrap(B)->CreateMemCpy(unwrap(Dst), DstAlign,
                                      unwrap(Src), SrcAlign,
                                      unwrap(Size)));
}

LLVMValueRef LLVMBuildMemMove(LLVMBuilderRef B,
                              LLVMValueRef Dst, unsigned DstAlign,
                              LLVMValueRef Src, unsigned SrcAlign,
                              LLVMValueRef Size) {
  return wrap(unwrap(B)->CreateMemMove(unwrap(Dst), DstAlign,
                                       unwrap(Src), SrcAlign,
                                       unwrap(Size)));
}

LLVMValueRef LLVMBuildAlloca(LLVMBuilderRef B, LLVMTypeRef Ty,
                             const char *Name) {
  return wrap(unwrap(B)->CreateAlloca(unwrap(Ty), nullptr, Name));
}

LLVMValueRef LLVMBuildArrayAlloca(LLVMBuilderRef B, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name) {
  return wrap(unwrap(B)->CreateAlloca(unwrap(Ty), unwrap(Val), Name));
}

LLVMValueRef LLVMBuildFree(LLVMBuilderRef B, LLVMValueRef PointerVal) {
  return wrap(unwrap(B)->Insert(
     CallInst::CreateFree(unwrap(PointerVal), unwrap(B)->GetInsertBlock())));
}

LLVMValueRef LLVMBuildLoad(LLVMBuilderRef B, LLVMValueRef PointerVal,
                           const char *Name) {
  Value *V = unwrap(PointerVal);
  PointerType *Ty = cast<PointerType>(V->getType());

  return wrap(unwrap(B)->CreateLoad(Ty->getElementType(), V, Name));
}

LLVMValueRef LLVMBuildLoad2(LLVMBuilderRef B, LLVMTypeRef Ty,
                            LLVMValueRef PointerVal, const char *Name) {
  return wrap(unwrap(B)->CreateLoad(unwrap(Ty), unwrap(PointerVal), Name));
}

LLVMValueRef LLVMBuildStore(LLVMBuilderRef B, LLVMValueRef Val,
                            LLVMValueRef PointerVal) {
  return wrap(unwrap(B)->CreateStore(unwrap(Val), unwrap(PointerVal)));
}

static AtomicOrdering mapFromLLVMOrdering(LLVMAtomicOrdering Ordering) {
  switch (Ordering) {
    case LLVMAtomicOrderingNotAtomic: return AtomicOrdering::NotAtomic;
    case LLVMAtomicOrderingUnordered: return AtomicOrdering::Unordered;
    case LLVMAtomicOrderingMonotonic: return AtomicOrdering::Monotonic;
    case LLVMAtomicOrderingAcquire: return AtomicOrdering::Acquire;
    case LLVMAtomicOrderingRelease: return AtomicOrdering::Release;
    case LLVMAtomicOrderingAcquireRelease:
      return AtomicOrdering::AcquireRelease;
    case LLVMAtomicOrderingSequentiallyConsistent:
      return AtomicOrdering::SequentiallyConsistent;
  }

  llvm_unreachable("Invalid LLVMAtomicOrdering value!");
}

static LLVMAtomicOrdering mapToLLVMOrdering(AtomicOrdering Ordering) {
  switch (Ordering) {
    case AtomicOrdering::NotAtomic: return LLVMAtomicOrderingNotAtomic;
    case AtomicOrdering::Unordered: return LLVMAtomicOrderingUnordered;
    case AtomicOrdering::Monotonic: return LLVMAtomicOrderingMonotonic;
    case AtomicOrdering::Acquire: return LLVMAtomicOrderingAcquire;
    case AtomicOrdering::Release: return LLVMAtomicOrderingRelease;
    case AtomicOrdering::AcquireRelease:
      return LLVMAtomicOrderingAcquireRelease;
    case AtomicOrdering::SequentiallyConsistent:
      return LLVMAtomicOrderingSequentiallyConsistent;
  }

  llvm_unreachable("Invalid AtomicOrdering value!");
}

// TODO: Should this and other atomic instructions support building with
// "syncscope"?
LLVMValueRef LLVMBuildFence(LLVMBuilderRef B, LLVMAtomicOrdering Ordering,
                            LLVMBool isSingleThread, const char *Name) {
  return wrap(
    unwrap(B)->CreateFence(mapFromLLVMOrdering(Ordering),
                           isSingleThread ? SyncScope::SingleThread
                                          : SyncScope::System,
                           Name));
}

LLVMValueRef LLVMBuildGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                          LLVMValueRef *Indices, unsigned NumIndices,
                          const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  Value *Val = unwrap(Pointer);
  Type *Ty =
      cast<PointerType>(Val->getType()->getScalarType())->getElementType();
  return wrap(unwrap(B)->CreateGEP(Ty, Val, IdxList, Name));
}

LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                           LLVMValueRef Pointer, LLVMValueRef *Indices,
                           unsigned NumIndices, const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  return wrap(unwrap(B)->CreateGEP(unwrap(Ty), unwrap(Pointer), IdxList, Name));
}

LLVMValueRef LLVMBuildInBoundsGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                                  LLVMValueRef *Indices, unsigned NumIndices,
                                  const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  Value *Val = unwrap(Pointer);
  Type *Ty =
      cast<PointerType>(Val->getType()->getScalarType())->getElementType();
  return wrap(unwrap(B)->CreateInBoundsGEP(Ty, Val, IdxList, Name));
}

LLVMValueRef LLVMBuildInBoundsGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                                   LLVMValueRef Pointer, LLVMValueRef *Indices,
                                   unsigned NumIndices, const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  return wrap(
      unwrap(B)->CreateInBoundsGEP(unwrap(Ty), unwrap(Pointer), IdxList, Name));
}

LLVMValueRef LLVMBuildStructGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                                unsigned Idx, const char *Name) {
  Value *Val = unwrap(Pointer);
  Type *Ty =
      cast<PointerType>(Val->getType()->getScalarType())->getElementType();
  return wrap(unwrap(B)->CreateStructGEP(Ty, Val, Idx, Name));
}

LLVMValueRef LLVMBuildStructGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                                 LLVMValueRef Pointer, unsigned Idx,
                                 const char *Name) {
  return wrap(
      unwrap(B)->CreateStructGEP(unwrap(Ty), unwrap(Pointer), Idx, Name));
}

LLVMValueRef LLVMBuildGlobalString(LLVMBuilderRef B, const char *Str,
                                   const char *Name) {
  return wrap(unwrap(B)->CreateGlobalString(Str, Name));
}

LLVMValueRef LLVMBuildGlobalStringPtr(LLVMBuilderRef B, const char *Str,
                                      const char *Name) {
  return wrap(unwrap(B)->CreateGlobalStringPtr(Str, Name));
}

LLVMBool LLVMGetVolatile(LLVMValueRef MemAccessInst) {
  Value *P = unwrap<Value>(MemAccessInst);
  if (LoadInst *LI = dyn_cast<LoadInst>(P))
    return LI->isVolatile();
  return cast<StoreInst>(P)->isVolatile();
}

void LLVMSetVolatile(LLVMValueRef MemAccessInst, LLVMBool isVolatile) {
  Value *P = unwrap<Value>(MemAccessInst);
  if (LoadInst *LI = dyn_cast<LoadInst>(P))
    return LI->setVolatile(isVolatile);
  return cast<StoreInst>(P)->setVolatile(isVolatile);
}

LLVMAtomicOrdering LLVMGetOrdering(LLVMValueRef MemAccessInst) {
  Value *P = unwrap<Value>(MemAccessInst);
  AtomicOrdering O;
  if (LoadInst *LI = dyn_cast<LoadInst>(P))
    O = LI->getOrdering();
  else
    O = cast<StoreInst>(P)->getOrdering();
  return mapToLLVMOrdering(O);
}

void LLVMSetOrdering(LLVMValueRef MemAccessInst, LLVMAtomicOrdering Ordering) {
  Value *P = unwrap<Value>(MemAccessInst);
  AtomicOrdering O = mapFromLLVMOrdering(Ordering);

  if (LoadInst *LI = dyn_cast<LoadInst>(P))
    return LI->setOrdering(O);
  return cast<StoreInst>(P)->setOrdering(O);
}

/*--.. Casts ...............................................................--*/

LLVMValueRef LLVMBuildTrunc(LLVMBuilderRef B, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateTrunc(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildZExt(LLVMBuilderRef B, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateZExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildSExt(LLVMBuilderRef B, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPToUI(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPToUI(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPToSI(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPToSI(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildUIToFP(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateUIToFP(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildSIToFP(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSIToFP(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPTrunc(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPTrunc(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPExt(LLVMBuilderRef B, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildPtrToInt(LLVMBuilderRef B, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreatePtrToInt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildIntToPtr(LLVMBuilderRef B, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateIntToPtr(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateBitCast(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildAddrSpaceCast(LLVMBuilderRef B, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateAddrSpaceCast(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildZExtOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateZExtOrBitCast(unwrap(Val), unwrap(DestTy),
                                             Name));
}

LLVMValueRef LLVMBuildSExtOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSExtOrBitCast(unwrap(Val), unwrap(DestTy),
                                             Name));
}

LLVMValueRef LLVMBuildTruncOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                     LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateTruncOrBitCast(unwrap(Val), unwrap(DestTy),
                                              Name));
}

LLVMValueRef LLVMBuildCast(LLVMBuilderRef B, LLVMOpcode Op, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateCast(Instruction::CastOps(map_from_llvmopcode(Op)), unwrap(Val),
                                    unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildPointerCast(LLVMBuilderRef B, LLVMValueRef Val,
                                  LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreatePointerCast(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildIntCast2(LLVMBuilderRef B, LLVMValueRef Val,
                               LLVMTypeRef DestTy, LLVMBool IsSigned,
                               const char *Name) {
  return wrap(
      unwrap(B)->CreateIntCast(unwrap(Val), unwrap(DestTy), IsSigned, Name));
}

LLVMValueRef LLVMBuildIntCast(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateIntCast(unwrap(Val), unwrap(DestTy),
                                       /*isSigned*/true, Name));
}

LLVMValueRef LLVMBuildFPCast(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPCast(unwrap(Val), unwrap(DestTy), Name));
}

/*--.. Comparisons .........................................................--*/

LLVMValueRef LLVMBuildICmp(LLVMBuilderRef B, LLVMIntPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateICmp(static_cast<ICmpInst::Predicate>(Op),
                                    unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFCmp(LLVMBuilderRef B, LLVMRealPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFCmp(static_cast<FCmpInst::Predicate>(Op),
                                    unwrap(LHS), unwrap(RHS), Name));
}

/*--.. Miscellaneous instructions ..........................................--*/

LLVMValueRef LLVMBuildPhi(LLVMBuilderRef B, LLVMTypeRef Ty, const char *Name) {
  return wrap(unwrap(B)->CreatePHI(unwrap(Ty), 0, Name));
}

LLVMValueRef LLVMBuildCall(LLVMBuilderRef B, LLVMValueRef Fn,
                           LLVMValueRef *Args, unsigned NumArgs,
                           const char *Name) {
  Value *V = unwrap(Fn);
  FunctionType *FnT =
      cast<FunctionType>(cast<PointerType>(V->getType())->getElementType());

  return wrap(unwrap(B)->CreateCall(FnT, unwrap(Fn),
                                    makeArrayRef(unwrap(Args), NumArgs), Name));
}

LLVMValueRef LLVMBuildCall2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Fn,
                            LLVMValueRef *Args, unsigned NumArgs,
                            const char *Name) {
  FunctionType *FTy = unwrap<FunctionType>(Ty);
  return wrap(unwrap(B)->CreateCall(FTy, unwrap(Fn),
                                    makeArrayRef(unwrap(Args), NumArgs), Name));
}

LLVMValueRef LLVMBuildSelect(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMValueRef Then, LLVMValueRef Else,
                             const char *Name) {
  return wrap(unwrap(B)->CreateSelect(unwrap(If), unwrap(Then), unwrap(Else),
                                      Name));
}

LLVMValueRef LLVMBuildVAArg(LLVMBuilderRef B, LLVMValueRef List,
                            LLVMTypeRef Ty, const char *Name) {
  return wrap(unwrap(B)->CreateVAArg(unwrap(List), unwrap(Ty), Name));
}

LLVMValueRef LLVMBuildExtractElement(LLVMBuilderRef B, LLVMValueRef VecVal,
                                      LLVMValueRef Index, const char *Name) {
  return wrap(unwrap(B)->CreateExtractElement(unwrap(VecVal), unwrap(Index),
                                              Name));
}

LLVMValueRef LLVMBuildInsertElement(LLVMBuilderRef B, LLVMValueRef VecVal,
                                    LLVMValueRef EltVal, LLVMValueRef Index,
                                    const char *Name) {
  return wrap(unwrap(B)->CreateInsertElement(unwrap(VecVal), unwrap(EltVal),
                                             unwrap(Index), Name));
}

LLVMValueRef LLVMBuildShuffleVector(LLVMBuilderRef B, LLVMValueRef V1,
                                    LLVMValueRef V2, LLVMValueRef Mask,
                                    const char *Name) {
  return wrap(unwrap(B)->CreateShuffleVector(unwrap(V1), unwrap(V2),
                                             unwrap(Mask), Name));
}

LLVMValueRef LLVMBuildExtractValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                   unsigned Index, const char *Name) {
  return wrap(unwrap(B)->CreateExtractValue(unwrap(AggVal), Index, Name));
}

LLVMValueRef LLVMBuildInsertValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                  LLVMValueRef EltVal, unsigned Index,
                                  const char *Name) {
  return wrap(unwrap(B)->CreateInsertValue(unwrap(AggVal), unwrap(EltVal),
                                           Index, Name));
}

LLVMValueRef LLVMBuildIsNull(LLVMBuilderRef B, LLVMValueRef Val,
                             const char *Name) {
  return wrap(unwrap(B)->CreateIsNull(unwrap(Val), Name));
}

LLVMValueRef LLVMBuildIsNotNull(LLVMBuilderRef B, LLVMValueRef Val,
                                const char *Name) {
  return wrap(unwrap(B)->CreateIsNotNull(unwrap(Val), Name));
}

LLVMValueRef LLVMBuildPtrDiff(LLVMBuilderRef B, LLVMValueRef LHS,
                              LLVMValueRef RHS, const char *Name) {
  return wrap(unwrap(B)->CreatePtrDiff(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildAtomicRMW(LLVMBuilderRef B,LLVMAtomicRMWBinOp op,
                               LLVMValueRef PTR, LLVMValueRef Val,
                               LLVMAtomicOrdering ordering,
                               LLVMBool singleThread) {
  AtomicRMWInst::BinOp intop;
  switch (op) {
    case LLVMAtomicRMWBinOpXchg: intop = AtomicRMWInst::Xchg; break;
    case LLVMAtomicRMWBinOpAdd: intop = AtomicRMWInst::Add; break;
    case LLVMAtomicRMWBinOpSub: intop = AtomicRMWInst::Sub; break;
    case LLVMAtomicRMWBinOpAnd: intop = AtomicRMWInst::And; break;
    case LLVMAtomicRMWBinOpNand: intop = AtomicRMWInst::Nand; break;
    case LLVMAtomicRMWBinOpOr: intop = AtomicRMWInst::Or; break;
    case LLVMAtomicRMWBinOpXor: intop = AtomicRMWInst::Xor; break;
    case LLVMAtomicRMWBinOpMax: intop = AtomicRMWInst::Max; break;
    case LLVMAtomicRMWBinOpMin: intop = AtomicRMWInst::Min; break;
    case LLVMAtomicRMWBinOpUMax: intop = AtomicRMWInst::UMax; break;
    case LLVMAtomicRMWBinOpUMin: intop = AtomicRMWInst::UMin; break;
  }
  return wrap(unwrap(B)->CreateAtomicRMW(intop, unwrap(PTR), unwrap(Val),
    mapFromLLVMOrdering(ordering), singleThread ? SyncScope::SingleThread
                                                : SyncScope::System));
}

LLVMValueRef LLVMBuildAtomicCmpXchg(LLVMBuilderRef B, LLVMValueRef Ptr,
                                    LLVMValueRef Cmp, LLVMValueRef New,
                                    LLVMAtomicOrdering SuccessOrdering,
                                    LLVMAtomicOrdering FailureOrdering,
                                    LLVMBool singleThread) {

  return wrap(unwrap(B)->CreateAtomicCmpXchg(unwrap(Ptr), unwrap(Cmp),
                unwrap(New), mapFromLLVMOrdering(SuccessOrdering),
                mapFromLLVMOrdering(FailureOrdering),
                singleThread ? SyncScope::SingleThread : SyncScope::System));
}


LLVMBool LLVMIsAtomicSingleThread(LLVMValueRef AtomicInst) {
  Value *P = unwrap<Value>(AtomicInst);

  if (AtomicRMWInst *I = dyn_cast<AtomicRMWInst>(P))
    return I->getSyncScopeID() == SyncScope::SingleThread;
  return cast<AtomicCmpXchgInst>(P)->getSyncScopeID() ==
             SyncScope::SingleThread;
}

void LLVMSetAtomicSingleThread(LLVMValueRef AtomicInst, LLVMBool NewValue) {
  Value *P = unwrap<Value>(AtomicInst);
  SyncScope::ID SSID = NewValue ? SyncScope::SingleThread : SyncScope::System;

  if (AtomicRMWInst *I = dyn_cast<AtomicRMWInst>(P))
    return I->setSyncScopeID(SSID);
  return cast<AtomicCmpXchgInst>(P)->setSyncScopeID(SSID);
}

LLVMAtomicOrdering LLVMGetCmpXchgSuccessOrdering(LLVMValueRef CmpXchgInst)  {
  Value *P = unwrap<Value>(CmpXchgInst);
  return mapToLLVMOrdering(cast<AtomicCmpXchgInst>(P)->getSuccessOrdering());
}

void LLVMSetCmpXchgSuccessOrdering(LLVMValueRef CmpXchgInst,
                                   LLVMAtomicOrdering Ordering) {
  Value *P = unwrap<Value>(CmpXchgInst);
  AtomicOrdering O = mapFromLLVMOrdering(Ordering);

  return cast<AtomicCmpXchgInst>(P)->setSuccessOrdering(O);
}

LLVMAtomicOrdering LLVMGetCmpXchgFailureOrdering(LLVMValueRef CmpXchgInst)  {
  Value *P = unwrap<Value>(CmpXchgInst);
  return mapToLLVMOrdering(cast<AtomicCmpXchgInst>(P)->getFailureOrdering());
}

void LLVMSetCmpXchgFailureOrdering(LLVMValueRef CmpXchgInst,
                                   LLVMAtomicOrdering Ordering) {
  Value *P = unwrap<Value>(CmpXchgInst);
  AtomicOrdering O = mapFromLLVMOrdering(Ordering);

  return cast<AtomicCmpXchgInst>(P)->setFailureOrdering(O);
}

/*===-- Module providers --------------------------------------------------===*/

LLVMModuleProviderRef
LLVMCreateModuleProviderForExistingModule(LLVMModuleRef M) {
  return reinterpret_cast<LLVMModuleProviderRef>(M);
}

void LLVMDisposeModuleProvider(LLVMModuleProviderRef MP) {
  delete unwrap(MP);
}


/*===-- Memory buffers ----------------------------------------------------===*/

LLVMBool LLVMCreateMemoryBufferWithContentsOfFile(
    const char *Path,
    LLVMMemoryBufferRef *OutMemBuf,
    char **OutMessage) {

  ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr = MemoryBuffer::getFile(Path);
  if (std::error_code EC = MBOrErr.getError()) {
    *OutMessage = strdup(EC.message().c_str());
    return 1;
  }
  *OutMemBuf = wrap(MBOrErr.get().release());
  return 0;
}

LLVMBool LLVMCreateMemoryBufferWithSTDIN(LLVMMemoryBufferRef *OutMemBuf,
                                         char **OutMessage) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr = MemoryBuffer::getSTDIN();
  if (std::error_code EC = MBOrErr.getError()) {
    *OutMessage = strdup(EC.message().c_str());
    return 1;
  }
  *OutMemBuf = wrap(MBOrErr.get().release());
  return 0;
}

LLVMMemoryBufferRef LLVMCreateMemoryBufferWithMemoryRange(
    const char *InputData,
    size_t InputDataLength,
    const char *BufferName,
    LLVMBool RequiresNullTerminator) {

  return wrap(MemoryBuffer::getMemBuffer(StringRef(InputData, InputDataLength),
                                         StringRef(BufferName),
                                         RequiresNullTerminator).release());
}

LLVMMemoryBufferRef LLVMCreateMemoryBufferWithMemoryRangeCopy(
    const char *InputData,
    size_t InputDataLength,
    const char *BufferName) {

  return wrap(
      MemoryBuffer::getMemBufferCopy(StringRef(InputData, InputDataLength),
                                     StringRef(BufferName)).release());
}

const char *LLVMGetBufferStart(LLVMMemoryBufferRef MemBuf) {
  return unwrap(MemBuf)->getBufferStart();
}

size_t LLVMGetBufferSize(LLVMMemoryBufferRef MemBuf) {
  return unwrap(MemBuf)->getBufferSize();
}

void LLVMDisposeMemoryBuffer(LLVMMemoryBufferRef MemBuf) {
  delete unwrap(MemBuf);
}

/*===-- Pass Registry -----------------------------------------------------===*/

LLVMPassRegistryRef LLVMGetGlobalPassRegistry(void) {
  return wrap(PassRegistry::getPassRegistry());
}

/*===-- Pass Manager ------------------------------------------------------===*/

LLVMPassManagerRef LLVMCreatePassManager() {
  return wrap(new legacy::PassManager());
}

LLVMPassManagerRef LLVMCreateFunctionPassManagerForModule(LLVMModuleRef M) {
  return wrap(new legacy::FunctionPassManager(unwrap(M)));
}

LLVMPassManagerRef LLVMCreateFunctionPassManager(LLVMModuleProviderRef P) {
  return LLVMCreateFunctionPassManagerForModule(
                                            reinterpret_cast<LLVMModuleRef>(P));
}

LLVMBool LLVMRunPassManager(LLVMPassManagerRef PM, LLVMModuleRef M) {
  return unwrap<legacy::PassManager>(PM)->run(*unwrap(M));
}

LLVMBool LLVMInitializeFunctionPassManager(LLVMPassManagerRef FPM) {
  return unwrap<legacy::FunctionPassManager>(FPM)->doInitialization();
}

LLVMBool LLVMRunFunctionPassManager(LLVMPassManagerRef FPM, LLVMValueRef F) {
  return unwrap<legacy::FunctionPassManager>(FPM)->run(*unwrap<Function>(F));
}

LLVMBool LLVMFinalizeFunctionPassManager(LLVMPassManagerRef FPM) {
  return unwrap<legacy::FunctionPassManager>(FPM)->doFinalization();
}

void LLVMDisposePassManager(LLVMPassManagerRef PM) {
  delete unwrap(PM);
}

/*===-- Threading ------------------------------------------------------===*/

LLVMBool LLVMStartMultithreaded() {
  return LLVMIsMultithreaded();
}

void LLVMStopMultithreaded() {
}

LLVMBool LLVMIsMultithreaded() {
  return llvm_is_multithreaded();
}
