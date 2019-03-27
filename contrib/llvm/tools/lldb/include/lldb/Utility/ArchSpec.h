//===-- ArchSpec.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ARCHSPEC_H
#define LLDB_UTILITY_ARCHSPEC_H

#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ArchSpec ArchSpec.h "lldb/Utility/ArchSpec.h" An architecture
/// specification class.
///
/// A class designed to be created from a cpu type and subtype, a
/// string representation, or an llvm::Triple.  Keeping all of the conversions
/// of strings to architecture enumeration values confined to this class
/// allows new architecture support to be added easily.
//----------------------------------------------------------------------
class ArchSpec {
public:
  enum MIPSSubType {
    eMIPSSubType_unknown,
    eMIPSSubType_mips32,
    eMIPSSubType_mips32r2,
    eMIPSSubType_mips32r6,
    eMIPSSubType_mips32el,
    eMIPSSubType_mips32r2el,
    eMIPSSubType_mips32r6el,
    eMIPSSubType_mips64,
    eMIPSSubType_mips64r2,
    eMIPSSubType_mips64r6,
    eMIPSSubType_mips64el,
    eMIPSSubType_mips64r2el,
    eMIPSSubType_mips64r6el,
  };

  // Masks for the ases word of an ABI flags structure.
  enum MIPSASE {
    eMIPSAse_dsp = 0x00000001,       // DSP ASE
    eMIPSAse_dspr2 = 0x00000002,     // DSP R2 ASE
    eMIPSAse_eva = 0x00000004,       // Enhanced VA Scheme
    eMIPSAse_mcu = 0x00000008,       // MCU (MicroController) ASE
    eMIPSAse_mdmx = 0x00000010,      // MDMX ASE
    eMIPSAse_mips3d = 0x00000020,    // MIPS-3D ASE
    eMIPSAse_mt = 0x00000040,        // MT ASE
    eMIPSAse_smartmips = 0x00000080, // SmartMIPS ASE
    eMIPSAse_virt = 0x00000100,      // VZ ASE
    eMIPSAse_msa = 0x00000200,       // MSA ASE
    eMIPSAse_mips16 = 0x00000400,    // MIPS16 ASE
    eMIPSAse_micromips = 0x00000800, // MICROMIPS ASE
    eMIPSAse_xpa = 0x00001000,       // XPA ASE
    eMIPSAse_mask = 0x00001fff,
    eMIPSABI_O32 = 0x00002000,
    eMIPSABI_N32 = 0x00004000,
    eMIPSABI_N64 = 0x00008000,
    eMIPSABI_O64 = 0x00020000,
    eMIPSABI_EABI32 = 0x00040000,
    eMIPSABI_EABI64 = 0x00080000,
    eMIPSABI_mask = 0x000ff000
  };

  // MIPS Floating point ABI Values
  enum MIPS_ABI_FP {
    eMIPS_ABI_FP_ANY = 0x00000000,
    eMIPS_ABI_FP_DOUBLE = 0x00100000, // hard float / -mdouble-float
    eMIPS_ABI_FP_SINGLE = 0x00200000, // hard float / -msingle-float
    eMIPS_ABI_FP_SOFT = 0x00300000,   // soft float
    eMIPS_ABI_FP_OLD_64 = 0x00400000, // -mips32r2 -mfp64
    eMIPS_ABI_FP_XX = 0x00500000,     // -mfpxx
    eMIPS_ABI_FP_64 = 0x00600000,     // -mips32r2 -mfp64
    eMIPS_ABI_FP_64A = 0x00700000,    // -mips32r2 -mfp64 -mno-odd-spreg
    eMIPS_ABI_FP_mask = 0x00700000
  };

  // ARM specific e_flags
  enum ARMeflags {
    eARM_abi_soft_float = 0x00000200,
    eARM_abi_hard_float = 0x00000400
  };

  enum Core {
    eCore_arm_generic,
    eCore_arm_armv4,
    eCore_arm_armv4t,
    eCore_arm_armv5,
    eCore_arm_armv5e,
    eCore_arm_armv5t,
    eCore_arm_armv6,
    eCore_arm_armv6m,
    eCore_arm_armv7,
    eCore_arm_armv7f,
    eCore_arm_armv7s,
    eCore_arm_armv7k,
    eCore_arm_armv7m,
    eCore_arm_armv7em,
    eCore_arm_xscale,

    eCore_thumb,
    eCore_thumbv4t,
    eCore_thumbv5,
    eCore_thumbv5e,
    eCore_thumbv6,
    eCore_thumbv6m,
    eCore_thumbv7,
    eCore_thumbv7s,
    eCore_thumbv7k,
    eCore_thumbv7f,
    eCore_thumbv7m,
    eCore_thumbv7em,
    eCore_arm_arm64,
    eCore_arm_armv8,
    eCore_arm_aarch64,

    eCore_mips32,
    eCore_mips32r2,
    eCore_mips32r3,
    eCore_mips32r5,
    eCore_mips32r6,
    eCore_mips32el,
    eCore_mips32r2el,
    eCore_mips32r3el,
    eCore_mips32r5el,
    eCore_mips32r6el,
    eCore_mips64,
    eCore_mips64r2,
    eCore_mips64r3,
    eCore_mips64r5,
    eCore_mips64r6,
    eCore_mips64el,
    eCore_mips64r2el,
    eCore_mips64r3el,
    eCore_mips64r5el,
    eCore_mips64r6el,

    eCore_ppc_generic,
    eCore_ppc_ppc601,
    eCore_ppc_ppc602,
    eCore_ppc_ppc603,
    eCore_ppc_ppc603e,
    eCore_ppc_ppc603ev,
    eCore_ppc_ppc604,
    eCore_ppc_ppc604e,
    eCore_ppc_ppc620,
    eCore_ppc_ppc750,
    eCore_ppc_ppc7400,
    eCore_ppc_ppc7450,
    eCore_ppc_ppc970,

    eCore_ppc64le_generic,
    eCore_ppc64_generic,
    eCore_ppc64_ppc970_64,

    eCore_s390x_generic,

    eCore_sparc_generic,

    eCore_sparc9_generic,

    eCore_x86_32_i386,
    eCore_x86_32_i486,
    eCore_x86_32_i486sx,
    eCore_x86_32_i686,

    eCore_x86_64_x86_64,
    eCore_x86_64_x86_64h, // Haswell enabled x86_64
    eCore_hexagon_generic,
    eCore_hexagon_hexagonv4,
    eCore_hexagon_hexagonv5,

    eCore_uknownMach32,
    eCore_uknownMach64,

    eCore_kalimba3,
    eCore_kalimba4,
    eCore_kalimba5,

    kNumCores,

    kCore_invalid,
    // The following constants are used for wildcard matching only
    kCore_any,
    kCore_arm_any,
    kCore_ppc_any,
    kCore_ppc64_any,
    kCore_x86_32_any,
    kCore_x86_64_any,
    kCore_hexagon_any,

    kCore_arm_first = eCore_arm_generic,
    kCore_arm_last = eCore_arm_xscale,

    kCore_thumb_first = eCore_thumb,
    kCore_thumb_last = eCore_thumbv7em,

    kCore_ppc_first = eCore_ppc_generic,
    kCore_ppc_last = eCore_ppc_ppc970,

    kCore_ppc64_first = eCore_ppc64_generic,
    kCore_ppc64_last = eCore_ppc64_ppc970_64,

    kCore_x86_32_first = eCore_x86_32_i386,
    kCore_x86_32_last = eCore_x86_32_i686,

    kCore_x86_64_first = eCore_x86_64_x86_64,
    kCore_x86_64_last = eCore_x86_64_x86_64h,

    kCore_hexagon_first = eCore_hexagon_generic,
    kCore_hexagon_last = eCore_hexagon_hexagonv5,

    kCore_kalimba_first = eCore_kalimba3,
    kCore_kalimba_last = eCore_kalimba5,

    kCore_mips32_first = eCore_mips32,
    kCore_mips32_last = eCore_mips32r6,

    kCore_mips32el_first = eCore_mips32el,
    kCore_mips32el_last = eCore_mips32r6el,

    kCore_mips64_first = eCore_mips64,
    kCore_mips64_last = eCore_mips64r6,

    kCore_mips64el_first = eCore_mips64el,
    kCore_mips64el_last = eCore_mips64r6el,

    kCore_mips_first = eCore_mips32,
    kCore_mips_last = eCore_mips64r6el

  };

  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Default constructor that initializes the object with invalid cpu type
  /// and subtype values.
  //------------------------------------------------------------------
  ArchSpec();

  //------------------------------------------------------------------
  /// Constructor over triple.
  ///
  /// Constructs an ArchSpec with properties consistent with the given Triple.
  //------------------------------------------------------------------
  explicit ArchSpec(const llvm::Triple &triple);
  explicit ArchSpec(const char *triple_cstr);
  explicit ArchSpec(llvm::StringRef triple_str);
  //------------------------------------------------------------------
  /// Constructor over architecture name.
  ///
  /// Constructs an ArchSpec with properties consistent with the given object
  /// type and architecture name.
  //------------------------------------------------------------------
  explicit ArchSpec(ArchitectureType arch_type, uint32_t cpu_type,
                    uint32_t cpu_subtype);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~ArchSpec();

  //------------------------------------------------------------------
  /// Assignment operator.
  ///
  /// @param[in] rhs another ArchSpec object to copy.
  ///
  /// @return A const reference to this object.
  //------------------------------------------------------------------
  const ArchSpec &operator=(const ArchSpec &rhs);

  //---------------------------------------------------------------------------
  /// Returns true if the OS, vendor and environment fields of the triple are
  /// unset. The triple is expected to be normalized
  /// (llvm::Triple::normalize).
  //---------------------------------------------------------------------------
  static bool ContainsOnlyArch(const llvm::Triple &normalized_triple);

  static void ListSupportedArchNames(StringList &list);
  static size_t AutoComplete(CompletionRequest &request);

  //------------------------------------------------------------------
  /// Returns a static string representing the current architecture.
  ///
  /// @return A static string corresponding to the current
  ///         architecture.
  //------------------------------------------------------------------
  const char *GetArchitectureName() const;

  //-----------------------------------------------------------------
  /// if MIPS architecture return true.
  ///
  ///  @return a boolean value.
  //-----------------------------------------------------------------
  bool IsMIPS() const;

  //------------------------------------------------------------------
  /// Returns a string representing current architecture as a target CPU for
  /// tools like compiler, disassembler etc.
  ///
  /// @return A string representing target CPU for the current
  ///         architecture.
  //------------------------------------------------------------------
  std::string GetClangTargetCPU() const;

  //------------------------------------------------------------------
  /// Return a string representing target application ABI.
  ///
  /// @return A string representing target application ABI.
  //------------------------------------------------------------------
  std::string GetTargetABI() const;

  //------------------------------------------------------------------
  /// Clears the object state.
  ///
  /// Clears the object state back to a default invalid state.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Returns the size in bytes of an address of the current architecture.
  ///
  /// @return The byte size of an address of the current architecture.
  //------------------------------------------------------------------
  uint32_t GetAddressByteSize() const;

  //------------------------------------------------------------------
  /// Returns a machine family for the current architecture.
  ///
  /// @return An LLVM arch type.
  //------------------------------------------------------------------
  llvm::Triple::ArchType GetMachine() const;

  //------------------------------------------------------------------
  /// Returns the distribution id of the architecture.
  ///
  /// This will be something like "ubuntu", "fedora", etc. on Linux.
  ///
  /// @return A ConstString ref containing the distribution id,
  ///         potentially empty.
  //------------------------------------------------------------------
  const ConstString &GetDistributionId() const;

  //------------------------------------------------------------------
  /// Set the distribution id of the architecture.
  ///
  /// This will be something like "ubuntu", "fedora", etc. on Linux. This
  /// should be the same value returned by HostInfo::GetDistributionId ().
  ///------------------------------------------------------------------
  void SetDistributionId(const char *distribution_id);

  //------------------------------------------------------------------
  /// Tests if this ArchSpec is valid.
  ///
  /// @return True if the current architecture is valid, false
  ///         otherwise.
  //------------------------------------------------------------------
  bool IsValid() const {
    return m_core >= eCore_arm_generic && m_core < kNumCores;
  }
  explicit operator bool() const { return IsValid(); }

  bool TripleVendorWasSpecified() const {
    return !m_triple.getVendorName().empty();
  }

  bool TripleVendorIsUnspecifiedUnknown() const {
    return m_triple.getVendor() == llvm::Triple::UnknownVendor &&
           m_triple.getVendorName().empty();
  }

  bool TripleOSWasSpecified() const { return !m_triple.getOSName().empty(); }

  bool TripleEnvironmentWasSpecified() const {
    return !m_triple.getEnvironmentName().empty();
  }

  bool TripleOSIsUnspecifiedUnknown() const {
    return m_triple.getOS() == llvm::Triple::UnknownOS &&
           m_triple.getOSName().empty();
  }

  //------------------------------------------------------------------
  /// Merges fields from another ArchSpec into this ArchSpec.
  ///
  /// This will use the supplied ArchSpec to fill in any fields of the triple
  /// in this ArchSpec which were unspecified.  This can be used to refine a
  /// generic ArchSpec with a more specific one. For example, if this
  /// ArchSpec's triple is something like i386-unknown-unknown-unknown, and we
  /// have a triple which is x64-pc-windows-msvc, then merging that triple
  /// into this one will result in the triple i386-pc-windows-msvc.
  ///
  //------------------------------------------------------------------
  void MergeFrom(const ArchSpec &other);

  //------------------------------------------------------------------
  /// Change the architecture object type, CPU type and OS type.
  ///
  /// @param[in] arch_type The object type of this ArchSpec.
  ///
  /// @param[in] cpu The required CPU type.
  ///
  /// @param[in] os The optional OS type
  /// The default value of 0 was chosen to from the ELF spec value
  /// ELFOSABI_NONE.  ELF is the only one using this parameter.  If another
  /// format uses this parameter and 0 does not work, use a value over
  /// 255 because in the ELF header this is value is only a byte.
  ///
  /// @return True if the object, and CPU were successfully set.
  ///
  /// As a side effect, the vendor value is usually set to unknown. The
  /// exceptions are
  ///   aarch64-apple-ios
  ///   arm-apple-ios
  ///   thumb-apple-ios
  ///   x86-apple-
  ///   x86_64-apple-
  ///
  /// As a side effect, the os value is usually set to unknown The exceptions
  /// are
  ///   *-*-aix
  ///   aarch64-apple-ios
  ///   arm-apple-ios
  ///   thumb-apple-ios
  ///   powerpc-apple-darwin
  ///   *-*-freebsd
  ///   *-*-linux
  ///   *-*-netbsd
  ///   *-*-openbsd
  ///   *-*-solaris
  //------------------------------------------------------------------
  bool SetArchitecture(ArchitectureType arch_type, uint32_t cpu, uint32_t sub,
                       uint32_t os = 0);

  //------------------------------------------------------------------
  /// Returns the byte order for the architecture specification.
  ///
  /// @return The endian enumeration for the current endianness of
  ///     the architecture specification
  //------------------------------------------------------------------
  lldb::ByteOrder GetByteOrder() const;

  //------------------------------------------------------------------
  /// Sets this ArchSpec's byte order.
  ///
  /// In the common case there is no need to call this method as the byte
  /// order can almost always be determined by the architecture. However, many
  /// CPU's are bi-endian (ARM, Alpha, PowerPC, etc) and the default/assumed
  /// byte order may be incorrect.
  //------------------------------------------------------------------
  void SetByteOrder(lldb::ByteOrder byte_order) { m_byte_order = byte_order; }

  uint32_t GetMinimumOpcodeByteSize() const;

  uint32_t GetMaximumOpcodeByteSize() const;

  Core GetCore() const { return m_core; }

  uint32_t GetMachOCPUType() const;

  uint32_t GetMachOCPUSubType() const;

  //------------------------------------------------------------------
  /// Architecture data byte width accessor
  ///
  /// @return the size in 8-bit (host) bytes of a minimum addressable unit
  /// from the Architecture's data bus
  //------------------------------------------------------------------
  uint32_t GetDataByteSize() const;

  //------------------------------------------------------------------
  /// Architecture code byte width accessor
  ///
  /// @return the size in 8-bit (host) bytes of a minimum addressable unit
  /// from the Architecture's code bus
  //------------------------------------------------------------------
  uint32_t GetCodeByteSize() const;

  //------------------------------------------------------------------
  /// Architecture triple accessor.
  ///
  /// @return A triple describing this ArchSpec.
  //------------------------------------------------------------------
  llvm::Triple &GetTriple() { return m_triple; }

  //------------------------------------------------------------------
  /// Architecture triple accessor.
  ///
  /// @return A triple describing this ArchSpec.
  //------------------------------------------------------------------
  const llvm::Triple &GetTriple() const { return m_triple; }

  void DumpTriple(Stream &s) const;

  //------------------------------------------------------------------
  /// Architecture triple setter.
  ///
  /// Configures this ArchSpec according to the given triple.  If the triple
  /// has unknown components in all of the vendor, OS, and the optional
  /// environment field (i.e. "i386-unknown-unknown") then default values are
  /// taken from the host.  Architecture and environment components are used
  /// to further resolve the CPU type and subtype, endian characteristics,
  /// etc.
  ///
  /// @return A triple describing this ArchSpec.
  //------------------------------------------------------------------
  bool SetTriple(const llvm::Triple &triple);

  bool SetTriple(llvm::StringRef triple_str);

  //------------------------------------------------------------------
  /// Returns the default endianness of the architecture.
  ///
  /// @return The endian enumeration for the default endianness of
  ///         the architecture.
  //------------------------------------------------------------------
  lldb::ByteOrder GetDefaultEndian() const;

  //------------------------------------------------------------------
  /// Returns true if 'char' is a signed type by default in the architecture
  /// false otherwise
  ///
  /// @return True if 'char' is a signed type by default on the
  ///         architecture and false otherwise.
  //------------------------------------------------------------------
  bool CharIsSignedByDefault() const;

  //------------------------------------------------------------------
  /// Compare an ArchSpec to another ArchSpec, requiring an exact cpu type
  /// match between them. e.g. armv7s is not an exact match with armv7 - this
  /// would return false
  ///
  /// @return true if the two ArchSpecs match.
  //------------------------------------------------------------------
  bool IsExactMatch(const ArchSpec &rhs) const;

  //------------------------------------------------------------------
  /// Compare an ArchSpec to another ArchSpec, requiring a compatible cpu type
  /// match between them. e.g. armv7s is compatible with armv7 - this method
  /// would return true
  ///
  /// @return true if the two ArchSpecs are compatible
  //------------------------------------------------------------------
  bool IsCompatibleMatch(const ArchSpec &rhs) const;

  bool IsFullySpecifiedTriple() const;

  void PiecewiseTripleCompare(const ArchSpec &other, bool &arch_different,
                              bool &vendor_different, bool &os_different,
                              bool &os_version_different,
                              bool &env_different) const;

  //------------------------------------------------------------------
  /// Detect whether this architecture uses thumb code exclusively
  ///
  /// Some embedded ARM chips (e.g. the ARM Cortex M0-7 line) can only execute
  /// the Thumb instructions, never Arm.  We should normally pick up
  /// arm/thumbness from their the processor status bits (cpsr/xpsr) or hints
  /// on each function - but when doing bare-boards low level debugging
  /// (especially common with these embedded processors), we may not have
  /// those things easily accessible.
  ///
  /// @return true if this is an arm ArchSpec which can only execute Thumb
  ///         instructions
  //------------------------------------------------------------------
  bool IsAlwaysThumbInstructions() const;

  uint32_t GetFlags() const { return m_flags; }

  void SetFlags(uint32_t flags) { m_flags = flags; }

  void SetFlags(std::string elf_abi);

protected:
  bool IsEqualTo(const ArchSpec &rhs, bool exact_match) const;
  void UpdateCore();

  llvm::Triple m_triple;
  Core m_core = kCore_invalid;
  lldb::ByteOrder m_byte_order = lldb::eByteOrderInvalid;

  // Additional arch flags which we cannot get from triple and core For MIPS
  // these are application specific extensions like micromips, mips16 etc.
  uint32_t m_flags = 0;

  ConstString m_distribution_id;

  // Called when m_def or m_entry are changed.  Fills in all remaining members
  // with default values.
  void CoreUpdated(bool update_triple);
};

//------------------------------------------------------------------
/// @fn bool operator< (const ArchSpec& lhs, const ArchSpec& rhs) Less than
/// operator.
///
/// Tests two ArchSpec objects to see if \a lhs is less than \a rhs.
///
/// @param[in] lhs The Left Hand Side ArchSpec object to compare. @param[in]
/// rhs The Left Hand Side ArchSpec object to compare.
///
/// @return true if \a lhs is less than \a rhs
//------------------------------------------------------------------
bool operator<(const ArchSpec &lhs, const ArchSpec &rhs);
bool operator==(const ArchSpec &lhs, const ArchSpec &rhs);

bool ParseMachCPUDashSubtypeTriple(llvm::StringRef triple_str, ArchSpec &arch);

} // namespace lldb_private

#endif // #ifndef LLDB_UTILITY_ARCHSPEC_H
