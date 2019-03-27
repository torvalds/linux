//===--- AMDGPUHSAMetadataStreamer.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU HSA Metadata Streamer.
///
//
//===----------------------------------------------------------------------===//

#include "AMDGPUHSAMetadataStreamer.h"
#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "MCTargetDesc/AMDGPUTargetStreamer.h"
#include "SIMachineFunctionInfo.h"
#include "SIProgramInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

static cl::opt<bool> DumpHSAMetadata(
    "amdgpu-dump-hsa-metadata",
    cl::desc("Dump AMDGPU HSA Metadata"));
static cl::opt<bool> VerifyHSAMetadata(
    "amdgpu-verify-hsa-metadata",
    cl::desc("Verify AMDGPU HSA Metadata"));

namespace AMDGPU {
namespace HSAMD {

//===----------------------------------------------------------------------===//
// HSAMetadataStreamerV2
//===----------------------------------------------------------------------===//
void MetadataStreamerV2::dump(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata:\n" << HSAMetadataString << '\n';
}

void MetadataStreamerV2::verify(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata Parser Test: ";

  HSAMD::Metadata FromHSAMetadataString;
  if (fromString(HSAMetadataString, FromHSAMetadataString)) {
    errs() << "FAIL\n";
    return;
  }

  std::string ToHSAMetadataString;
  if (toString(FromHSAMetadataString, ToHSAMetadataString)) {
    errs() << "FAIL\n";
    return;
  }

  errs() << (HSAMetadataString == ToHSAMetadataString ? "PASS" : "FAIL")
         << '\n';
  if (HSAMetadataString != ToHSAMetadataString) {
    errs() << "Original input: " << HSAMetadataString << '\n'
           << "Produced output: " << ToHSAMetadataString << '\n';
  }
}

AccessQualifier
MetadataStreamerV2::getAccessQualifier(StringRef AccQual) const {
  if (AccQual.empty())
    return AccessQualifier::Unknown;

  return StringSwitch<AccessQualifier>(AccQual)
             .Case("read_only",  AccessQualifier::ReadOnly)
             .Case("write_only", AccessQualifier::WriteOnly)
             .Case("read_write", AccessQualifier::ReadWrite)
             .Default(AccessQualifier::Default);
}

AddressSpaceQualifier
MetadataStreamerV2::getAddressSpaceQualifier(
    unsigned AddressSpace) const {
  switch (AddressSpace) {
  case AMDGPUAS::PRIVATE_ADDRESS:
    return AddressSpaceQualifier::Private;
  case AMDGPUAS::GLOBAL_ADDRESS:
    return AddressSpaceQualifier::Global;
  case AMDGPUAS::CONSTANT_ADDRESS:
    return AddressSpaceQualifier::Constant;
  case AMDGPUAS::LOCAL_ADDRESS:
    return AddressSpaceQualifier::Local;
  case AMDGPUAS::FLAT_ADDRESS:
    return AddressSpaceQualifier::Generic;
  case AMDGPUAS::REGION_ADDRESS:
    return AddressSpaceQualifier::Region;
  default:
    return AddressSpaceQualifier::Unknown;
  }
}

ValueKind MetadataStreamerV2::getValueKind(Type *Ty, StringRef TypeQual,
                                           StringRef BaseTypeName) const {
  if (TypeQual.find("pipe") != StringRef::npos)
    return ValueKind::Pipe;

  return StringSwitch<ValueKind>(BaseTypeName)
             .Case("image1d_t", ValueKind::Image)
             .Case("image1d_array_t", ValueKind::Image)
             .Case("image1d_buffer_t", ValueKind::Image)
             .Case("image2d_t", ValueKind::Image)
             .Case("image2d_array_t", ValueKind::Image)
             .Case("image2d_array_depth_t", ValueKind::Image)
             .Case("image2d_array_msaa_t", ValueKind::Image)
             .Case("image2d_array_msaa_depth_t", ValueKind::Image)
             .Case("image2d_depth_t", ValueKind::Image)
             .Case("image2d_msaa_t", ValueKind::Image)
             .Case("image2d_msaa_depth_t", ValueKind::Image)
             .Case("image3d_t", ValueKind::Image)
             .Case("sampler_t", ValueKind::Sampler)
             .Case("queue_t", ValueKind::Queue)
             .Default(isa<PointerType>(Ty) ?
                          (Ty->getPointerAddressSpace() ==
                           AMDGPUAS::LOCAL_ADDRESS ?
                           ValueKind::DynamicSharedPointer :
                           ValueKind::GlobalBuffer) :
                      ValueKind::ByValue);
}

ValueType MetadataStreamerV2::getValueType(Type *Ty, StringRef TypeName) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    auto Signed = !TypeName.startswith("u");
    switch (Ty->getIntegerBitWidth()) {
    case 8:
      return Signed ? ValueType::I8 : ValueType::U8;
    case 16:
      return Signed ? ValueType::I16 : ValueType::U16;
    case 32:
      return Signed ? ValueType::I32 : ValueType::U32;
    case 64:
      return Signed ? ValueType::I64 : ValueType::U64;
    default:
      return ValueType::Struct;
    }
  }
  case Type::HalfTyID:
    return ValueType::F16;
  case Type::FloatTyID:
    return ValueType::F32;
  case Type::DoubleTyID:
    return ValueType::F64;
  case Type::PointerTyID:
    return getValueType(Ty->getPointerElementType(), TypeName);
  case Type::VectorTyID:
    return getValueType(Ty->getVectorElementType(), TypeName);
  default:
    return ValueType::Struct;
  }
}

std::string MetadataStreamerV2::getTypeName(Type *Ty, bool Signed) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    if (!Signed)
      return (Twine('u') + getTypeName(Ty, true)).str();

    auto BitWidth = Ty->getIntegerBitWidth();
    switch (BitWidth) {
    case 8:
      return "char";
    case 16:
      return "short";
    case 32:
      return "int";
    case 64:
      return "long";
    default:
      return (Twine('i') + Twine(BitWidth)).str();
    }
  }
  case Type::HalfTyID:
    return "half";
  case Type::FloatTyID:
    return "float";
  case Type::DoubleTyID:
    return "double";
  case Type::VectorTyID: {
    auto VecTy = cast<VectorType>(Ty);
    auto ElTy = VecTy->getElementType();
    auto NumElements = VecTy->getVectorNumElements();
    return (Twine(getTypeName(ElTy, Signed)) + Twine(NumElements)).str();
  }
  default:
    return "unknown";
  }
}

std::vector<uint32_t>
MetadataStreamerV2::getWorkGroupDimensions(MDNode *Node) const {
  std::vector<uint32_t> Dims;
  if (Node->getNumOperands() != 3)
    return Dims;

  for (auto &Op : Node->operands())
    Dims.push_back(mdconst::extract<ConstantInt>(Op)->getZExtValue());
  return Dims;
}

Kernel::CodeProps::Metadata
MetadataStreamerV2::getHSACodeProps(const MachineFunction &MF,
                                    const SIProgramInfo &ProgramInfo) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  HSAMD::Kernel::CodeProps::Metadata HSACodeProps;
  const Function &F = MF.getFunction();

  assert(F.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
         F.getCallingConv() == CallingConv::SPIR_KERNEL);

  unsigned MaxKernArgAlign;
  HSACodeProps.mKernargSegmentSize = STM.getKernArgSegmentSize(F,
                                                               MaxKernArgAlign);
  HSACodeProps.mGroupSegmentFixedSize = ProgramInfo.LDSSize;
  HSACodeProps.mPrivateSegmentFixedSize = ProgramInfo.ScratchSize;
  HSACodeProps.mKernargSegmentAlign = std::max(MaxKernArgAlign, 4u);
  HSACodeProps.mWavefrontSize = STM.getWavefrontSize();
  HSACodeProps.mNumSGPRs = ProgramInfo.NumSGPR;
  HSACodeProps.mNumVGPRs = ProgramInfo.NumVGPR;
  HSACodeProps.mMaxFlatWorkGroupSize = MFI.getMaxFlatWorkGroupSize();
  HSACodeProps.mIsDynamicCallStack = ProgramInfo.DynamicCallStack;
  HSACodeProps.mIsXNACKEnabled = STM.isXNACKEnabled();
  HSACodeProps.mNumSpilledSGPRs = MFI.getNumSpilledSGPRs();
  HSACodeProps.mNumSpilledVGPRs = MFI.getNumSpilledVGPRs();

  return HSACodeProps;
}

Kernel::DebugProps::Metadata
MetadataStreamerV2::getHSADebugProps(const MachineFunction &MF,
                                     const SIProgramInfo &ProgramInfo) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  HSAMD::Kernel::DebugProps::Metadata HSADebugProps;

  if (!STM.debuggerSupported())
    return HSADebugProps;

  HSADebugProps.mDebuggerABIVersion.push_back(1);
  HSADebugProps.mDebuggerABIVersion.push_back(0);

  if (STM.debuggerEmitPrologue()) {
    HSADebugProps.mPrivateSegmentBufferSGPR =
        ProgramInfo.DebuggerPrivateSegmentBufferSGPR;
    HSADebugProps.mWavefrontPrivateSegmentOffsetSGPR =
        ProgramInfo.DebuggerWavefrontPrivateSegmentOffsetSGPR;
  }

  return HSADebugProps;
}

void MetadataStreamerV2::emitVersion() {
  auto &Version = HSAMetadata.mVersion;

  Version.push_back(VersionMajor);
  Version.push_back(VersionMinor);
}

void MetadataStreamerV2::emitPrintf(const Module &Mod) {
  auto &Printf = HSAMetadata.mPrintf;

  auto Node = Mod.getNamedMetadata("llvm.printf.fmts");
  if (!Node)
    return;

  for (auto Op : Node->operands())
    if (Op->getNumOperands())
      Printf.push_back(cast<MDString>(Op->getOperand(0))->getString());
}

void MetadataStreamerV2::emitKernelLanguage(const Function &Func) {
  auto &Kernel = HSAMetadata.mKernels.back();

  // TODO: What about other languages?
  auto Node = Func.getParent()->getNamedMetadata("opencl.ocl.version");
  if (!Node || !Node->getNumOperands())
    return;
  auto Op0 = Node->getOperand(0);
  if (Op0->getNumOperands() <= 1)
    return;

  Kernel.mLanguage = "OpenCL C";
  Kernel.mLanguageVersion.push_back(
      mdconst::extract<ConstantInt>(Op0->getOperand(0))->getZExtValue());
  Kernel.mLanguageVersion.push_back(
      mdconst::extract<ConstantInt>(Op0->getOperand(1))->getZExtValue());
}

void MetadataStreamerV2::emitKernelAttrs(const Function &Func) {
  auto &Attrs = HSAMetadata.mKernels.back().mAttrs;

  if (auto Node = Func.getMetadata("reqd_work_group_size"))
    Attrs.mReqdWorkGroupSize = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("work_group_size_hint"))
    Attrs.mWorkGroupSizeHint = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("vec_type_hint")) {
    Attrs.mVecTypeHint = getTypeName(
        cast<ValueAsMetadata>(Node->getOperand(0))->getType(),
        mdconst::extract<ConstantInt>(Node->getOperand(1))->getZExtValue());
  }
  if (Func.hasFnAttribute("runtime-handle")) {
    Attrs.mRuntimeHandle =
        Func.getFnAttribute("runtime-handle").getValueAsString().str();
  }
}

void MetadataStreamerV2::emitKernelArgs(const Function &Func) {
  for (auto &Arg : Func.args())
    emitKernelArg(Arg);

  emitHiddenKernelArgs(Func);
}

void MetadataStreamerV2::emitKernelArg(const Argument &Arg) {
  auto Func = Arg.getParent();
  auto ArgNo = Arg.getArgNo();
  const MDNode *Node;

  StringRef Name;
  Node = Func->getMetadata("kernel_arg_name");
  if (Node && ArgNo < Node->getNumOperands())
    Name = cast<MDString>(Node->getOperand(ArgNo))->getString();
  else if (Arg.hasName())
    Name = Arg.getName();

  StringRef TypeName;
  Node = Func->getMetadata("kernel_arg_type");
  if (Node && ArgNo < Node->getNumOperands())
    TypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef BaseTypeName;
  Node = Func->getMetadata("kernel_arg_base_type");
  if (Node && ArgNo < Node->getNumOperands())
    BaseTypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef AccQual;
  if (Arg.getType()->isPointerTy() && Arg.onlyReadsMemory() &&
      Arg.hasNoAliasAttr()) {
    AccQual = "read_only";
  } else {
    Node = Func->getMetadata("kernel_arg_access_qual");
    if (Node && ArgNo < Node->getNumOperands())
      AccQual = cast<MDString>(Node->getOperand(ArgNo))->getString();
  }

  StringRef TypeQual;
  Node = Func->getMetadata("kernel_arg_type_qual");
  if (Node && ArgNo < Node->getNumOperands())
    TypeQual = cast<MDString>(Node->getOperand(ArgNo))->getString();

  Type *Ty = Arg.getType();
  const DataLayout &DL = Func->getParent()->getDataLayout();

  unsigned PointeeAlign = 0;
  if (auto PtrTy = dyn_cast<PointerType>(Ty)) {
    if (PtrTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {
      PointeeAlign = Arg.getParamAlignment();
      if (PointeeAlign == 0)
        PointeeAlign = DL.getABITypeAlignment(PtrTy->getElementType());
    }
  }

  emitKernelArg(DL, Ty, getValueKind(Arg.getType(), TypeQual, BaseTypeName),
                PointeeAlign, Name, TypeName, BaseTypeName, AccQual, TypeQual);
}

void MetadataStreamerV2::emitKernelArg(const DataLayout &DL, Type *Ty,
                                       ValueKind ValueKind,
                                       unsigned PointeeAlign, StringRef Name,
                                       StringRef TypeName,
                                       StringRef BaseTypeName,
                                       StringRef AccQual, StringRef TypeQual) {
  HSAMetadata.mKernels.back().mArgs.push_back(Kernel::Arg::Metadata());
  auto &Arg = HSAMetadata.mKernels.back().mArgs.back();

  Arg.mName = Name;
  Arg.mTypeName = TypeName;
  Arg.mSize = DL.getTypeAllocSize(Ty);
  Arg.mAlign = DL.getABITypeAlignment(Ty);
  Arg.mValueKind = ValueKind;
  Arg.mValueType = getValueType(Ty, BaseTypeName);
  Arg.mPointeeAlign = PointeeAlign;

  if (auto PtrTy = dyn_cast<PointerType>(Ty))
    Arg.mAddrSpaceQual = getAddressSpaceQualifier(PtrTy->getAddressSpace());

  Arg.mAccQual = getAccessQualifier(AccQual);

  // TODO: Emit Arg.mActualAccQual.

  SmallVector<StringRef, 1> SplitTypeQuals;
  TypeQual.split(SplitTypeQuals, " ", -1, false);
  for (StringRef Key : SplitTypeQuals) {
    auto P = StringSwitch<bool*>(Key)
                 .Case("const",    &Arg.mIsConst)
                 .Case("restrict", &Arg.mIsRestrict)
                 .Case("volatile", &Arg.mIsVolatile)
                 .Case("pipe",     &Arg.mIsPipe)
                 .Default(nullptr);
    if (P)
      *P = true;
  }
}

void MetadataStreamerV2::emitHiddenKernelArgs(const Function &Func) {
  int HiddenArgNumBytes =
      getIntegerAttribute(Func, "amdgpu-implicitarg-num-bytes", 0);

  if (!HiddenArgNumBytes)
    return;

  auto &DL = Func.getParent()->getDataLayout();
  auto Int64Ty = Type::getInt64Ty(Func.getContext());

  if (HiddenArgNumBytes >= 8)
    emitKernelArg(DL, Int64Ty, ValueKind::HiddenGlobalOffsetX);
  if (HiddenArgNumBytes >= 16)
    emitKernelArg(DL, Int64Ty, ValueKind::HiddenGlobalOffsetY);
  if (HiddenArgNumBytes >= 24)
    emitKernelArg(DL, Int64Ty, ValueKind::HiddenGlobalOffsetZ);

  auto Int8PtrTy = Type::getInt8PtrTy(Func.getContext(),
                                      AMDGPUAS::GLOBAL_ADDRESS);

  // Emit "printf buffer" argument if printf is used, otherwise emit dummy
  // "none" argument.
  if (HiddenArgNumBytes >= 32) {
    if (Func.getParent()->getNamedMetadata("llvm.printf.fmts"))
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenPrintfBuffer);
    else
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenNone);
  }

  // Emit "default queue" and "completion action" arguments if enqueue kernel is
  // used, otherwise emit dummy "none" arguments.
  if (HiddenArgNumBytes >= 48) {
    if (Func.hasFnAttribute("calls-enqueue-kernel")) {
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenDefaultQueue);
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenCompletionAction);
    } else {
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenNone);
      emitKernelArg(DL, Int8PtrTy, ValueKind::HiddenNone);
    }
  }
}

bool MetadataStreamerV2::emitTo(AMDGPUTargetStreamer &TargetStreamer) {
  return TargetStreamer.EmitHSAMetadata(getHSAMetadata());
}

void MetadataStreamerV2::begin(const Module &Mod) {
  emitVersion();
  emitPrintf(Mod);
}

void MetadataStreamerV2::end() {
  std::string HSAMetadataString;
  if (toString(HSAMetadata, HSAMetadataString))
    return;

  if (DumpHSAMetadata)
    dump(HSAMetadataString);
  if (VerifyHSAMetadata)
    verify(HSAMetadataString);
}

void MetadataStreamerV2::emitKernel(const MachineFunction &MF,
                                    const SIProgramInfo &ProgramInfo) {
  auto &Func = MF.getFunction();
  if (Func.getCallingConv() != CallingConv::AMDGPU_KERNEL)
    return;

  auto CodeProps = getHSACodeProps(MF, ProgramInfo);
  auto DebugProps = getHSADebugProps(MF, ProgramInfo);

  HSAMetadata.mKernels.push_back(Kernel::Metadata());
  auto &Kernel = HSAMetadata.mKernels.back();

  Kernel.mName = Func.getName();
  Kernel.mSymbolName = (Twine(Func.getName()) + Twine("@kd")).str();
  emitKernelLanguage(Func);
  emitKernelAttrs(Func);
  emitKernelArgs(Func);
  HSAMetadata.mKernels.back().mCodeProps = CodeProps;
  HSAMetadata.mKernels.back().mDebugProps = DebugProps;
}

//===----------------------------------------------------------------------===//
// HSAMetadataStreamerV3
//===----------------------------------------------------------------------===//

void MetadataStreamerV3::dump(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata:\n" << HSAMetadataString << '\n';
}

void MetadataStreamerV3::verify(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata Parser Test: ";

  std::shared_ptr<msgpack::Node> FromHSAMetadataString =
      std::make_shared<msgpack::MapNode>();

  yaml::Input YIn(HSAMetadataString);
  YIn >> FromHSAMetadataString;
  if (YIn.error()) {
    errs() << "FAIL\n";
    return;
  }

  std::string ToHSAMetadataString;
  raw_string_ostream StrOS(ToHSAMetadataString);
  yaml::Output YOut(StrOS);
  YOut << FromHSAMetadataString;

  errs() << (HSAMetadataString == StrOS.str() ? "PASS" : "FAIL") << '\n';
  if (HSAMetadataString != ToHSAMetadataString) {
    errs() << "Original input: " << HSAMetadataString << '\n'
           << "Produced output: " << StrOS.str() << '\n';
  }
}

Optional<StringRef>
MetadataStreamerV3::getAccessQualifier(StringRef AccQual) const {
  return StringSwitch<Optional<StringRef>>(AccQual)
      .Case("read_only", StringRef("read_only"))
      .Case("write_only", StringRef("write_only"))
      .Case("read_write", StringRef("read_write"))
      .Default(None);
}

Optional<StringRef>
MetadataStreamerV3::getAddressSpaceQualifier(unsigned AddressSpace) const {
  switch (AddressSpace) {
  case AMDGPUAS::PRIVATE_ADDRESS:
    return StringRef("private");
  case AMDGPUAS::GLOBAL_ADDRESS:
    return StringRef("global");
  case AMDGPUAS::CONSTANT_ADDRESS:
    return StringRef("constant");
  case AMDGPUAS::LOCAL_ADDRESS:
    return StringRef("local");
  case AMDGPUAS::FLAT_ADDRESS:
    return StringRef("generic");
  case AMDGPUAS::REGION_ADDRESS:
    return StringRef("region");
  default:
    return None;
  }
}

StringRef MetadataStreamerV3::getValueKind(Type *Ty, StringRef TypeQual,
                                           StringRef BaseTypeName) const {
  if (TypeQual.find("pipe") != StringRef::npos)
    return "pipe";

  return StringSwitch<StringRef>(BaseTypeName)
      .Case("image1d_t", "image")
      .Case("image1d_array_t", "image")
      .Case("image1d_buffer_t", "image")
      .Case("image2d_t", "image")
      .Case("image2d_array_t", "image")
      .Case("image2d_array_depth_t", "image")
      .Case("image2d_array_msaa_t", "image")
      .Case("image2d_array_msaa_depth_t", "image")
      .Case("image2d_depth_t", "image")
      .Case("image2d_msaa_t", "image")
      .Case("image2d_msaa_depth_t", "image")
      .Case("image3d_t", "image")
      .Case("sampler_t", "sampler")
      .Case("queue_t", "queue")
      .Default(isa<PointerType>(Ty)
                   ? (Ty->getPointerAddressSpace() == AMDGPUAS::LOCAL_ADDRESS
                          ? "dynamic_shared_pointer"
                          : "global_buffer")
                   : "by_value");
}

StringRef MetadataStreamerV3::getValueType(Type *Ty, StringRef TypeName) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    auto Signed = !TypeName.startswith("u");
    switch (Ty->getIntegerBitWidth()) {
    case 8:
      return Signed ? "i8" : "u8";
    case 16:
      return Signed ? "i16" : "u16";
    case 32:
      return Signed ? "i32" : "u32";
    case 64:
      return Signed ? "i64" : "u64";
    default:
      return "struct";
    }
  }
  case Type::HalfTyID:
    return "f16";
  case Type::FloatTyID:
    return "f32";
  case Type::DoubleTyID:
    return "f64";
  case Type::PointerTyID:
    return getValueType(Ty->getPointerElementType(), TypeName);
  case Type::VectorTyID:
    return getValueType(Ty->getVectorElementType(), TypeName);
  default:
    return "struct";
  }
}

std::string MetadataStreamerV3::getTypeName(Type *Ty, bool Signed) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    if (!Signed)
      return (Twine('u') + getTypeName(Ty, true)).str();

    auto BitWidth = Ty->getIntegerBitWidth();
    switch (BitWidth) {
    case 8:
      return "char";
    case 16:
      return "short";
    case 32:
      return "int";
    case 64:
      return "long";
    default:
      return (Twine('i') + Twine(BitWidth)).str();
    }
  }
  case Type::HalfTyID:
    return "half";
  case Type::FloatTyID:
    return "float";
  case Type::DoubleTyID:
    return "double";
  case Type::VectorTyID: {
    auto VecTy = cast<VectorType>(Ty);
    auto ElTy = VecTy->getElementType();
    auto NumElements = VecTy->getVectorNumElements();
    return (Twine(getTypeName(ElTy, Signed)) + Twine(NumElements)).str();
  }
  default:
    return "unknown";
  }
}

std::shared_ptr<msgpack::ArrayNode>
MetadataStreamerV3::getWorkGroupDimensions(MDNode *Node) const {
  auto Dims = std::make_shared<msgpack::ArrayNode>();
  if (Node->getNumOperands() != 3)
    return Dims;

  for (auto &Op : Node->operands())
    Dims->push_back(std::make_shared<msgpack::ScalarNode>(
        mdconst::extract<ConstantInt>(Op)->getZExtValue()));
  return Dims;
}

void MetadataStreamerV3::emitVersion() {
  auto Version = std::make_shared<msgpack::ArrayNode>();
  Version->push_back(std::make_shared<msgpack::ScalarNode>(V3::VersionMajor));
  Version->push_back(std::make_shared<msgpack::ScalarNode>(V3::VersionMinor));
  getRootMetadata("amdhsa.version") = std::move(Version);
}

void MetadataStreamerV3::emitPrintf(const Module &Mod) {
  auto Node = Mod.getNamedMetadata("llvm.printf.fmts");
  if (!Node)
    return;

  auto Printf = std::make_shared<msgpack::ArrayNode>();
  for (auto Op : Node->operands())
    if (Op->getNumOperands())
      Printf->push_back(std::make_shared<msgpack::ScalarNode>(
          cast<MDString>(Op->getOperand(0))->getString()));
  getRootMetadata("amdhsa.printf") = std::move(Printf);
}

void MetadataStreamerV3::emitKernelLanguage(const Function &Func,
                                            msgpack::MapNode &Kern) {
  // TODO: What about other languages?
  auto Node = Func.getParent()->getNamedMetadata("opencl.ocl.version");
  if (!Node || !Node->getNumOperands())
    return;
  auto Op0 = Node->getOperand(0);
  if (Op0->getNumOperands() <= 1)
    return;

  Kern[".language"] = std::make_shared<msgpack::ScalarNode>("OpenCL C");
  auto LanguageVersion = std::make_shared<msgpack::ArrayNode>();
  LanguageVersion->push_back(std::make_shared<msgpack::ScalarNode>(
      mdconst::extract<ConstantInt>(Op0->getOperand(0))->getZExtValue()));
  LanguageVersion->push_back(std::make_shared<msgpack::ScalarNode>(
      mdconst::extract<ConstantInt>(Op0->getOperand(1))->getZExtValue()));
  Kern[".language_version"] = std::move(LanguageVersion);
}

void MetadataStreamerV3::emitKernelAttrs(const Function &Func,
                                         msgpack::MapNode &Kern) {

  if (auto Node = Func.getMetadata("reqd_work_group_size"))
    Kern[".reqd_workgroup_size"] = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("work_group_size_hint"))
    Kern[".workgroup_size_hint"] = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("vec_type_hint")) {
    Kern[".vec_type_hint"] = std::make_shared<msgpack::ScalarNode>(getTypeName(
        cast<ValueAsMetadata>(Node->getOperand(0))->getType(),
        mdconst::extract<ConstantInt>(Node->getOperand(1))->getZExtValue()));
  }
  if (Func.hasFnAttribute("runtime-handle")) {
    Kern[".device_enqueue_symbol"] = std::make_shared<msgpack::ScalarNode>(
        Func.getFnAttribute("runtime-handle").getValueAsString().str());
  }
}

void MetadataStreamerV3::emitKernelArgs(const Function &Func,
                                        msgpack::MapNode &Kern) {
  unsigned Offset = 0;
  auto Args = std::make_shared<msgpack::ArrayNode>();
  for (auto &Arg : Func.args())
    emitKernelArg(Arg, Offset, *Args);

  emitHiddenKernelArgs(Func, Offset, *Args);

  // TODO: What about other languages?
  if (Func.getParent()->getNamedMetadata("opencl.ocl.version")) {
    auto &DL = Func.getParent()->getDataLayout();
    auto Int64Ty = Type::getInt64Ty(Func.getContext());

    emitKernelArg(DL, Int64Ty, "hidden_global_offset_x", Offset, *Args);
    emitKernelArg(DL, Int64Ty, "hidden_global_offset_y", Offset, *Args);
    emitKernelArg(DL, Int64Ty, "hidden_global_offset_z", Offset, *Args);

    auto Int8PtrTy =
        Type::getInt8PtrTy(Func.getContext(), AMDGPUAS::GLOBAL_ADDRESS);

    // Emit "printf buffer" argument if printf is used, otherwise emit dummy
    // "none" argument.
    if (Func.getParent()->getNamedMetadata("llvm.printf.fmts"))
      emitKernelArg(DL, Int8PtrTy, "hidden_printf_buffer", Offset, *Args);
    else
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, *Args);

    // Emit "default queue" and "completion action" arguments if enqueue kernel
    // is used, otherwise emit dummy "none" arguments.
    if (Func.hasFnAttribute("calls-enqueue-kernel")) {
      emitKernelArg(DL, Int8PtrTy, "hidden_default_queue", Offset, *Args);
      emitKernelArg(DL, Int8PtrTy, "hidden_completion_action", Offset, *Args);
    } else {
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, *Args);
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, *Args);
    }
  }

  Kern[".args"] = std::move(Args);
}

void MetadataStreamerV3::emitKernelArg(const Argument &Arg, unsigned &Offset,
                                       msgpack::ArrayNode &Args) {
  auto Func = Arg.getParent();
  auto ArgNo = Arg.getArgNo();
  const MDNode *Node;

  StringRef Name;
  Node = Func->getMetadata("kernel_arg_name");
  if (Node && ArgNo < Node->getNumOperands())
    Name = cast<MDString>(Node->getOperand(ArgNo))->getString();
  else if (Arg.hasName())
    Name = Arg.getName();

  StringRef TypeName;
  Node = Func->getMetadata("kernel_arg_type");
  if (Node && ArgNo < Node->getNumOperands())
    TypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef BaseTypeName;
  Node = Func->getMetadata("kernel_arg_base_type");
  if (Node && ArgNo < Node->getNumOperands())
    BaseTypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef AccQual;
  if (Arg.getType()->isPointerTy() && Arg.onlyReadsMemory() &&
      Arg.hasNoAliasAttr()) {
    AccQual = "read_only";
  } else {
    Node = Func->getMetadata("kernel_arg_access_qual");
    if (Node && ArgNo < Node->getNumOperands())
      AccQual = cast<MDString>(Node->getOperand(ArgNo))->getString();
  }

  StringRef TypeQual;
  Node = Func->getMetadata("kernel_arg_type_qual");
  if (Node && ArgNo < Node->getNumOperands())
    TypeQual = cast<MDString>(Node->getOperand(ArgNo))->getString();

  Type *Ty = Arg.getType();
  const DataLayout &DL = Func->getParent()->getDataLayout();

  unsigned PointeeAlign = 0;
  if (auto PtrTy = dyn_cast<PointerType>(Ty)) {
    if (PtrTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {
      PointeeAlign = Arg.getParamAlignment();
      if (PointeeAlign == 0)
        PointeeAlign = DL.getABITypeAlignment(PtrTy->getElementType());
    }
  }

  emitKernelArg(Func->getParent()->getDataLayout(), Arg.getType(),
                getValueKind(Arg.getType(), TypeQual, BaseTypeName), Offset,
                Args, PointeeAlign, Name, TypeName, BaseTypeName, AccQual,
                TypeQual);
}

void MetadataStreamerV3::emitKernelArg(const DataLayout &DL, Type *Ty,
                                       StringRef ValueKind, unsigned &Offset,
                                       msgpack::ArrayNode &Args,
                                       unsigned PointeeAlign, StringRef Name,
                                       StringRef TypeName,
                                       StringRef BaseTypeName,
                                       StringRef AccQual, StringRef TypeQual) {
  auto ArgPtr = std::make_shared<msgpack::MapNode>();
  auto &Arg = *ArgPtr;

  if (!Name.empty())
    Arg[".name"] = std::make_shared<msgpack::ScalarNode>(Name);
  if (!TypeName.empty())
    Arg[".type_name"] = std::make_shared<msgpack::ScalarNode>(TypeName);
  auto Size = DL.getTypeAllocSize(Ty);
  auto Align = DL.getABITypeAlignment(Ty);
  Arg[".size"] = std::make_shared<msgpack::ScalarNode>(Size);
  Offset = alignTo(Offset, Align);
  Arg[".offset"] = std::make_shared<msgpack::ScalarNode>(Offset);
  Offset += Size;
  Arg[".value_kind"] = std::make_shared<msgpack::ScalarNode>(ValueKind);
  Arg[".value_type"] =
      std::make_shared<msgpack::ScalarNode>(getValueType(Ty, BaseTypeName));
  if (PointeeAlign)
    Arg[".pointee_align"] = std::make_shared<msgpack::ScalarNode>(PointeeAlign);

  if (auto PtrTy = dyn_cast<PointerType>(Ty))
    if (auto Qualifier = getAddressSpaceQualifier(PtrTy->getAddressSpace()))
      Arg[".address_space"] = std::make_shared<msgpack::ScalarNode>(*Qualifier);

  if (auto AQ = getAccessQualifier(AccQual))
    Arg[".access"] = std::make_shared<msgpack::ScalarNode>(*AQ);

  // TODO: Emit Arg[".actual_access"].

  SmallVector<StringRef, 1> SplitTypeQuals;
  TypeQual.split(SplitTypeQuals, " ", -1, false);
  for (StringRef Key : SplitTypeQuals) {
    if (Key == "const")
      Arg[".is_const"] = std::make_shared<msgpack::ScalarNode>(true);
    else if (Key == "restrict")
      Arg[".is_restrict"] = std::make_shared<msgpack::ScalarNode>(true);
    else if (Key == "volatile")
      Arg[".is_volatile"] = std::make_shared<msgpack::ScalarNode>(true);
    else if (Key == "pipe")
      Arg[".is_pipe"] = std::make_shared<msgpack::ScalarNode>(true);
  }

  Args.push_back(std::move(ArgPtr));
}

void MetadataStreamerV3::emitHiddenKernelArgs(const Function &Func,
                                              unsigned &Offset,
                                              msgpack::ArrayNode &Args) {
  int HiddenArgNumBytes =
      getIntegerAttribute(Func, "amdgpu-implicitarg-num-bytes", 0);

  if (!HiddenArgNumBytes)
    return;

  auto &DL = Func.getParent()->getDataLayout();
  auto Int64Ty = Type::getInt64Ty(Func.getContext());

  if (HiddenArgNumBytes >= 8)
    emitKernelArg(DL, Int64Ty, "hidden_global_offset_x", Offset, Args);
  if (HiddenArgNumBytes >= 16)
    emitKernelArg(DL, Int64Ty, "hidden_global_offset_y", Offset, Args);
  if (HiddenArgNumBytes >= 24)
    emitKernelArg(DL, Int64Ty, "hidden_global_offset_z", Offset, Args);

  auto Int8PtrTy =
      Type::getInt8PtrTy(Func.getContext(), AMDGPUAS::GLOBAL_ADDRESS);

  // Emit "printf buffer" argument if printf is used, otherwise emit dummy
  // "none" argument.
  if (HiddenArgNumBytes >= 32) {
    if (Func.getParent()->getNamedMetadata("llvm.printf.fmts"))
      emitKernelArg(DL, Int8PtrTy, "hidden_printf_buffer", Offset, Args);
    else
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, Args);
  }

  // Emit "default queue" and "completion action" arguments if enqueue kernel is
  // used, otherwise emit dummy "none" arguments.
  if (HiddenArgNumBytes >= 48) {
    if (Func.hasFnAttribute("calls-enqueue-kernel")) {
      emitKernelArg(DL, Int8PtrTy, "hidden_default_queue", Offset, Args);
      emitKernelArg(DL, Int8PtrTy, "hidden_completion_action", Offset, Args);
    } else {
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, Args);
      emitKernelArg(DL, Int8PtrTy, "hidden_none", Offset, Args);
    }
  }
}

std::shared_ptr<msgpack::MapNode>
MetadataStreamerV3::getHSAKernelProps(const MachineFunction &MF,
                                      const SIProgramInfo &ProgramInfo) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  const Function &F = MF.getFunction();

  auto HSAKernelProps = std::make_shared<msgpack::MapNode>();
  auto &Kern = *HSAKernelProps;

  unsigned MaxKernArgAlign;
  Kern[".kernarg_segment_size"] = std::make_shared<msgpack::ScalarNode>(
      STM.getKernArgSegmentSize(F, MaxKernArgAlign));
  Kern[".group_segment_fixed_size"] =
      std::make_shared<msgpack::ScalarNode>(ProgramInfo.LDSSize);
  Kern[".private_segment_fixed_size"] =
      std::make_shared<msgpack::ScalarNode>(ProgramInfo.ScratchSize);
  Kern[".kernarg_segment_align"] =
      std::make_shared<msgpack::ScalarNode>(std::max(uint32_t(4), MaxKernArgAlign));
  Kern[".wavefront_size"] =
      std::make_shared<msgpack::ScalarNode>(STM.getWavefrontSize());
  Kern[".sgpr_count"] = std::make_shared<msgpack::ScalarNode>(ProgramInfo.NumSGPR);
  Kern[".vgpr_count"] = std::make_shared<msgpack::ScalarNode>(ProgramInfo.NumVGPR);
  Kern[".max_flat_workgroup_size"] =
      std::make_shared<msgpack::ScalarNode>(MFI.getMaxFlatWorkGroupSize());
  Kern[".sgpr_spill_count"] =
      std::make_shared<msgpack::ScalarNode>(MFI.getNumSpilledSGPRs());
  Kern[".vgpr_spill_count"] =
      std::make_shared<msgpack::ScalarNode>(MFI.getNumSpilledVGPRs());

  return HSAKernelProps;
}

bool MetadataStreamerV3::emitTo(AMDGPUTargetStreamer &TargetStreamer) {
  return TargetStreamer.EmitHSAMetadata(getHSAMetadataRoot(), true);
}

void MetadataStreamerV3::begin(const Module &Mod) {
  emitVersion();
  emitPrintf(Mod);
  getRootMetadata("amdhsa.kernels").reset(new msgpack::ArrayNode());
}

void MetadataStreamerV3::end() {
  std::string HSAMetadataString;
  raw_string_ostream StrOS(HSAMetadataString);
  yaml::Output YOut(StrOS);
  YOut << HSAMetadataRoot;

  if (DumpHSAMetadata)
    dump(StrOS.str());
  if (VerifyHSAMetadata)
    verify(StrOS.str());
}

void MetadataStreamerV3::emitKernel(const MachineFunction &MF,
                                    const SIProgramInfo &ProgramInfo) {
  auto &Func = MF.getFunction();
  auto KernelProps = getHSAKernelProps(MF, ProgramInfo);

  assert(Func.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
         Func.getCallingConv() == CallingConv::SPIR_KERNEL);

  auto &KernelsNode = getRootMetadata("amdhsa.kernels");
  auto Kernels = cast<msgpack::ArrayNode>(KernelsNode.get());

  {
    auto &Kern = *KernelProps;
    Kern[".name"] = std::make_shared<msgpack::ScalarNode>(Func.getName());
    Kern[".symbol"] = std::make_shared<msgpack::ScalarNode>(
        (Twine(Func.getName()) + Twine(".kd")).str());
    emitKernelLanguage(Func, Kern);
    emitKernelAttrs(Func, Kern);
    emitKernelArgs(Func, Kern);
  }

  Kernels->push_back(std::move(KernelProps));
}

} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm
