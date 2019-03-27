//===--- AMDGPUMetadata.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU metadata definitions and in-memory representations.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDGPUMETADATA_H
#define LLVM_SUPPORT_AMDGPUMETADATA_H

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace llvm {
namespace AMDGPU {

//===----------------------------------------------------------------------===//
// HSA metadata.
//===----------------------------------------------------------------------===//
namespace HSAMD {

/// HSA metadata major version.
constexpr uint32_t VersionMajor = 1;
/// HSA metadata minor version.
constexpr uint32_t VersionMinor = 0;

/// HSA metadata beginning assembler directive.
constexpr char AssemblerDirectiveBegin[] = ".amd_amdgpu_hsa_metadata";
/// HSA metadata ending assembler directive.
constexpr char AssemblerDirectiveEnd[] = ".end_amd_amdgpu_hsa_metadata";

/// Access qualifiers.
enum class AccessQualifier : uint8_t {
  Default   = 0,
  ReadOnly  = 1,
  WriteOnly = 2,
  ReadWrite = 3,
  Unknown   = 0xff
};

/// Address space qualifiers.
enum class AddressSpaceQualifier : uint8_t {
  Private  = 0,
  Global   = 1,
  Constant = 2,
  Local    = 3,
  Generic  = 4,
  Region   = 5,
  Unknown  = 0xff
};

/// Value kinds.
enum class ValueKind : uint8_t {
  ByValue                = 0,
  GlobalBuffer           = 1,
  DynamicSharedPointer   = 2,
  Sampler                = 3,
  Image                  = 4,
  Pipe                   = 5,
  Queue                  = 6,
  HiddenGlobalOffsetX    = 7,
  HiddenGlobalOffsetY    = 8,
  HiddenGlobalOffsetZ    = 9,
  HiddenNone             = 10,
  HiddenPrintfBuffer     = 11,
  HiddenDefaultQueue     = 12,
  HiddenCompletionAction = 13,
  Unknown                = 0xff
};

/// Value types.
enum class ValueType : uint8_t {
  Struct  = 0,
  I8      = 1,
  U8      = 2,
  I16     = 3,
  U16     = 4,
  F16     = 5,
  I32     = 6,
  U32     = 7,
  F32     = 8,
  I64     = 9,
  U64     = 10,
  F64     = 11,
  Unknown = 0xff
};

//===----------------------------------------------------------------------===//
// Kernel Metadata.
//===----------------------------------------------------------------------===//
namespace Kernel {

//===----------------------------------------------------------------------===//
// Kernel Attributes Metadata.
//===----------------------------------------------------------------------===//
namespace Attrs {

namespace Key {
/// Key for Kernel::Attr::Metadata::mReqdWorkGroupSize.
constexpr char ReqdWorkGroupSize[] = "ReqdWorkGroupSize";
/// Key for Kernel::Attr::Metadata::mWorkGroupSizeHint.
constexpr char WorkGroupSizeHint[] = "WorkGroupSizeHint";
/// Key for Kernel::Attr::Metadata::mVecTypeHint.
constexpr char VecTypeHint[] = "VecTypeHint";
/// Key for Kernel::Attr::Metadata::mRuntimeHandle.
constexpr char RuntimeHandle[] = "RuntimeHandle";
} // end namespace Key

/// In-memory representation of kernel attributes metadata.
struct Metadata final {
  /// 'reqd_work_group_size' attribute. Optional.
  std::vector<uint32_t> mReqdWorkGroupSize = std::vector<uint32_t>();
  /// 'work_group_size_hint' attribute. Optional.
  std::vector<uint32_t> mWorkGroupSizeHint = std::vector<uint32_t>();
  /// 'vec_type_hint' attribute. Optional.
  std::string mVecTypeHint = std::string();
  /// External symbol created by runtime to store the kernel address
  /// for enqueued blocks.
  std::string mRuntimeHandle = std::string();

  /// Default constructor.
  Metadata() = default;

  /// \returns True if kernel attributes metadata is empty, false otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// \returns True if kernel attributes metadata is not empty, false otherwise.
  bool notEmpty() const {
    return !mReqdWorkGroupSize.empty() || !mWorkGroupSizeHint.empty() ||
           !mVecTypeHint.empty() || !mRuntimeHandle.empty();
  }
};

} // end namespace Attrs

//===----------------------------------------------------------------------===//
// Kernel Argument Metadata.
//===----------------------------------------------------------------------===//
namespace Arg {

namespace Key {
/// Key for Kernel::Arg::Metadata::mName.
constexpr char Name[] = "Name";
/// Key for Kernel::Arg::Metadata::mTypeName.
constexpr char TypeName[] = "TypeName";
/// Key for Kernel::Arg::Metadata::mSize.
constexpr char Size[] = "Size";
/// Key for Kernel::Arg::Metadata::mAlign.
constexpr char Align[] = "Align";
/// Key for Kernel::Arg::Metadata::mValueKind.
constexpr char ValueKind[] = "ValueKind";
/// Key for Kernel::Arg::Metadata::mValueType.
constexpr char ValueType[] = "ValueType";
/// Key for Kernel::Arg::Metadata::mPointeeAlign.
constexpr char PointeeAlign[] = "PointeeAlign";
/// Key for Kernel::Arg::Metadata::mAddrSpaceQual.
constexpr char AddrSpaceQual[] = "AddrSpaceQual";
/// Key for Kernel::Arg::Metadata::mAccQual.
constexpr char AccQual[] = "AccQual";
/// Key for Kernel::Arg::Metadata::mActualAccQual.
constexpr char ActualAccQual[] = "ActualAccQual";
/// Key for Kernel::Arg::Metadata::mIsConst.
constexpr char IsConst[] = "IsConst";
/// Key for Kernel::Arg::Metadata::mIsRestrict.
constexpr char IsRestrict[] = "IsRestrict";
/// Key for Kernel::Arg::Metadata::mIsVolatile.
constexpr char IsVolatile[] = "IsVolatile";
/// Key for Kernel::Arg::Metadata::mIsPipe.
constexpr char IsPipe[] = "IsPipe";
} // end namespace Key

/// In-memory representation of kernel argument metadata.
struct Metadata final {
  /// Name. Optional.
  std::string mName = std::string();
  /// Type name. Optional.
  std::string mTypeName = std::string();
  /// Size in bytes. Required.
  uint32_t mSize = 0;
  /// Alignment in bytes. Required.
  uint32_t mAlign = 0;
  /// Value kind. Required.
  ValueKind mValueKind = ValueKind::Unknown;
  /// Value type. Required.
  ValueType mValueType = ValueType::Unknown;
  /// Pointee alignment in bytes. Optional.
  uint32_t mPointeeAlign = 0;
  /// Address space qualifier. Optional.
  AddressSpaceQualifier mAddrSpaceQual = AddressSpaceQualifier::Unknown;
  /// Access qualifier. Optional.
  AccessQualifier mAccQual = AccessQualifier::Unknown;
  /// Actual access qualifier. Optional.
  AccessQualifier mActualAccQual = AccessQualifier::Unknown;
  /// True if 'const' qualifier is specified. Optional.
  bool mIsConst = false;
  /// True if 'restrict' qualifier is specified. Optional.
  bool mIsRestrict = false;
  /// True if 'volatile' qualifier is specified. Optional.
  bool mIsVolatile = false;
  /// True if 'pipe' qualifier is specified. Optional.
  bool mIsPipe = false;

  /// Default constructor.
  Metadata() = default;
};

} // end namespace Arg

//===----------------------------------------------------------------------===//
// Kernel Code Properties Metadata.
//===----------------------------------------------------------------------===//
namespace CodeProps {

namespace Key {
/// Key for Kernel::CodeProps::Metadata::mKernargSegmentSize.
constexpr char KernargSegmentSize[] = "KernargSegmentSize";
/// Key for Kernel::CodeProps::Metadata::mGroupSegmentFixedSize.
constexpr char GroupSegmentFixedSize[] = "GroupSegmentFixedSize";
/// Key for Kernel::CodeProps::Metadata::mPrivateSegmentFixedSize.
constexpr char PrivateSegmentFixedSize[] = "PrivateSegmentFixedSize";
/// Key for Kernel::CodeProps::Metadata::mKernargSegmentAlign.
constexpr char KernargSegmentAlign[] = "KernargSegmentAlign";
/// Key for Kernel::CodeProps::Metadata::mWavefrontSize.
constexpr char WavefrontSize[] = "WavefrontSize";
/// Key for Kernel::CodeProps::Metadata::mNumSGPRs.
constexpr char NumSGPRs[] = "NumSGPRs";
/// Key for Kernel::CodeProps::Metadata::mNumVGPRs.
constexpr char NumVGPRs[] = "NumVGPRs";
/// Key for Kernel::CodeProps::Metadata::mMaxFlatWorkGroupSize.
constexpr char MaxFlatWorkGroupSize[] = "MaxFlatWorkGroupSize";
/// Key for Kernel::CodeProps::Metadata::mIsDynamicCallStack.
constexpr char IsDynamicCallStack[] = "IsDynamicCallStack";
/// Key for Kernel::CodeProps::Metadata::mIsXNACKEnabled.
constexpr char IsXNACKEnabled[] = "IsXNACKEnabled";
/// Key for Kernel::CodeProps::Metadata::mNumSpilledSGPRs.
constexpr char NumSpilledSGPRs[] = "NumSpilledSGPRs";
/// Key for Kernel::CodeProps::Metadata::mNumSpilledVGPRs.
constexpr char NumSpilledVGPRs[] = "NumSpilledVGPRs";
} // end namespace Key

/// In-memory representation of kernel code properties metadata.
struct Metadata final {
  /// Size in bytes of the kernarg segment memory. Kernarg segment memory
  /// holds the values of the arguments to the kernel. Required.
  uint64_t mKernargSegmentSize = 0;
  /// Size in bytes of the group segment memory required by a workgroup.
  /// This value does not include any dynamically allocated group segment memory
  /// that may be added when the kernel is dispatched. Required.
  uint32_t mGroupSegmentFixedSize = 0;
  /// Size in bytes of the private segment memory required by a workitem.
  /// Private segment memory includes arg, spill and private segments. Required.
  uint32_t mPrivateSegmentFixedSize = 0;
  /// Maximum byte alignment of variables used by the kernel in the
  /// kernarg memory segment. Required.
  uint32_t mKernargSegmentAlign = 0;
  /// Wavefront size. Required.
  uint32_t mWavefrontSize = 0;
  /// Total number of SGPRs used by a wavefront. Optional.
  uint16_t mNumSGPRs = 0;
  /// Total number of VGPRs used by a workitem. Optional.
  uint16_t mNumVGPRs = 0;
  /// Maximum flat work-group size supported by the kernel. Optional.
  uint32_t mMaxFlatWorkGroupSize = 0;
  /// True if the generated machine code is using a dynamically sized
  /// call stack. Optional.
  bool mIsDynamicCallStack = false;
  /// True if the generated machine code is capable of supporting XNACK.
  /// Optional.
  bool mIsXNACKEnabled = false;
  /// Number of SGPRs spilled by a wavefront. Optional.
  uint16_t mNumSpilledSGPRs = 0;
  /// Number of VGPRs spilled by a workitem. Optional.
  uint16_t mNumSpilledVGPRs = 0;

  /// Default constructor.
  Metadata() = default;

  /// \returns True if kernel code properties metadata is empty, false
  /// otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// \returns True if kernel code properties metadata is not empty, false
  /// otherwise.
  bool notEmpty() const {
    return true;
  }
};

} // end namespace CodeProps

//===----------------------------------------------------------------------===//
// Kernel Debug Properties Metadata.
//===----------------------------------------------------------------------===//
namespace DebugProps {

namespace Key {
/// Key for Kernel::DebugProps::Metadata::mDebuggerABIVersion.
constexpr char DebuggerABIVersion[] = "DebuggerABIVersion";
/// Key for Kernel::DebugProps::Metadata::mReservedNumVGPRs.
constexpr char ReservedNumVGPRs[] = "ReservedNumVGPRs";
/// Key for Kernel::DebugProps::Metadata::mReservedFirstVGPR.
constexpr char ReservedFirstVGPR[] = "ReservedFirstVGPR";
/// Key for Kernel::DebugProps::Metadata::mPrivateSegmentBufferSGPR.
constexpr char PrivateSegmentBufferSGPR[] = "PrivateSegmentBufferSGPR";
/// Key for
///     Kernel::DebugProps::Metadata::mWavefrontPrivateSegmentOffsetSGPR.
constexpr char WavefrontPrivateSegmentOffsetSGPR[] =
    "WavefrontPrivateSegmentOffsetSGPR";
} // end namespace Key

/// In-memory representation of kernel debug properties metadata.
struct Metadata final {
  /// Debugger ABI version. Optional.
  std::vector<uint32_t> mDebuggerABIVersion = std::vector<uint32_t>();
  /// Consecutive number of VGPRs reserved for debugger use. Must be 0 if
  /// mDebuggerABIVersion is not set. Optional.
  uint16_t mReservedNumVGPRs = 0;
  /// First fixed VGPR reserved. Must be uint16_t(-1) if
  /// mDebuggerABIVersion is not set or mReservedFirstVGPR is 0. Optional.
  uint16_t mReservedFirstVGPR = uint16_t(-1);
  /// Fixed SGPR of the first of 4 SGPRs used to hold the scratch V# used
  /// for the entire kernel execution. Must be uint16_t(-1) if
  /// mDebuggerABIVersion is not set or SGPR not used or not known. Optional.
  uint16_t mPrivateSegmentBufferSGPR = uint16_t(-1);
  /// Fixed SGPR used to hold the wave scratch offset for the entire
  /// kernel execution. Must be uint16_t(-1) if mDebuggerABIVersion is not set
  /// or SGPR is not used or not known. Optional.
  uint16_t mWavefrontPrivateSegmentOffsetSGPR = uint16_t(-1);

  /// Default constructor.
  Metadata() = default;

  /// \returns True if kernel debug properties metadata is empty, false
  /// otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// \returns True if kernel debug properties metadata is not empty, false
  /// otherwise.
  bool notEmpty() const {
    return !mDebuggerABIVersion.empty();
  }
};

} // end namespace DebugProps

namespace Key {
/// Key for Kernel::Metadata::mName.
constexpr char Name[] = "Name";
/// Key for Kernel::Metadata::mSymbolName.
constexpr char SymbolName[] = "SymbolName";
/// Key for Kernel::Metadata::mLanguage.
constexpr char Language[] = "Language";
/// Key for Kernel::Metadata::mLanguageVersion.
constexpr char LanguageVersion[] = "LanguageVersion";
/// Key for Kernel::Metadata::mAttrs.
constexpr char Attrs[] = "Attrs";
/// Key for Kernel::Metadata::mArgs.
constexpr char Args[] = "Args";
/// Key for Kernel::Metadata::mCodeProps.
constexpr char CodeProps[] = "CodeProps";
/// Key for Kernel::Metadata::mDebugProps.
constexpr char DebugProps[] = "DebugProps";
} // end namespace Key

/// In-memory representation of kernel metadata.
struct Metadata final {
  /// Kernel source name. Required.
  std::string mName = std::string();
  /// Kernel descriptor name. Required.
  std::string mSymbolName = std::string();
  /// Language. Optional.
  std::string mLanguage = std::string();
  /// Language version. Optional.
  std::vector<uint32_t> mLanguageVersion = std::vector<uint32_t>();
  /// Attributes metadata. Optional.
  Attrs::Metadata mAttrs = Attrs::Metadata();
  /// Arguments metadata. Optional.
  std::vector<Arg::Metadata> mArgs = std::vector<Arg::Metadata>();
  /// Code properties metadata. Optional.
  CodeProps::Metadata mCodeProps = CodeProps::Metadata();
  /// Debug properties metadata. Optional.
  DebugProps::Metadata mDebugProps = DebugProps::Metadata();

  /// Default constructor.
  Metadata() = default;
};

} // end namespace Kernel

namespace Key {
/// Key for HSA::Metadata::mVersion.
constexpr char Version[] = "Version";
/// Key for HSA::Metadata::mPrintf.
constexpr char Printf[] = "Printf";
/// Key for HSA::Metadata::mKernels.
constexpr char Kernels[] = "Kernels";
} // end namespace Key

/// In-memory representation of HSA metadata.
struct Metadata final {
  /// HSA metadata version. Required.
  std::vector<uint32_t> mVersion = std::vector<uint32_t>();
  /// Printf metadata. Optional.
  std::vector<std::string> mPrintf = std::vector<std::string>();
  /// Kernels metadata. Required.
  std::vector<Kernel::Metadata> mKernels = std::vector<Kernel::Metadata>();

  /// Default constructor.
  Metadata() = default;
};

/// Converts \p String to \p HSAMetadata.
std::error_code fromString(std::string String, Metadata &HSAMetadata);

/// Converts \p HSAMetadata to \p String.
std::error_code toString(Metadata HSAMetadata, std::string &String);

//===----------------------------------------------------------------------===//
// HSA metadata for v3 code object.
//===----------------------------------------------------------------------===//
namespace V3 {
/// HSA metadata major version.
constexpr uint32_t VersionMajor = 1;
/// HSA metadata minor version.
constexpr uint32_t VersionMinor = 0;

/// HSA metadata beginning assembler directive.
constexpr char AssemblerDirectiveBegin[] = ".amdgpu_metadata";
/// HSA metadata ending assembler directive.
constexpr char AssemblerDirectiveEnd[] = ".end_amdgpu_metadata";
} // end namespace V3

} // end namespace HSAMD

//===----------------------------------------------------------------------===//
// PAL metadata.
//===----------------------------------------------------------------------===//
namespace PALMD {

/// PAL metadata assembler directive.
constexpr char AssemblerDirective[] = ".amd_amdgpu_pal_metadata";

/// PAL metadata keys.
enum Key : uint32_t {
  LS_NUM_USED_VGPRS = 0x10000021,
  HS_NUM_USED_VGPRS = 0x10000022,
  ES_NUM_USED_VGPRS = 0x10000023,
  GS_NUM_USED_VGPRS = 0x10000024,
  VS_NUM_USED_VGPRS = 0x10000025,
  PS_NUM_USED_VGPRS = 0x10000026,
  CS_NUM_USED_VGPRS = 0x10000027,

  LS_NUM_USED_SGPRS = 0x10000028,
  HS_NUM_USED_SGPRS = 0x10000029,
  ES_NUM_USED_SGPRS = 0x1000002a,
  GS_NUM_USED_SGPRS = 0x1000002b,
  VS_NUM_USED_SGPRS = 0x1000002c,
  PS_NUM_USED_SGPRS = 0x1000002d,
  CS_NUM_USED_SGPRS = 0x1000002e,

  LS_SCRATCH_SIZE = 0x10000044,
  HS_SCRATCH_SIZE = 0x10000045,
  ES_SCRATCH_SIZE = 0x10000046,
  GS_SCRATCH_SIZE = 0x10000047,
  VS_SCRATCH_SIZE = 0x10000048,
  PS_SCRATCH_SIZE = 0x10000049,
  CS_SCRATCH_SIZE = 0x1000004a
};

/// PAL metadata represented as a vector.
typedef std::vector<uint32_t> Metadata;

/// Converts \p PALMetadata to \p String.
std::error_code toString(const Metadata &PALMetadata, std::string &String);

} // end namespace PALMD
} // end namespace AMDGPU
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDGPUMETADATA_H
