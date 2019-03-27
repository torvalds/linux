//===-- MinidumpTypes.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MinidumpTypes_h_
#define liblldb_MinidumpTypes_h_


#include "lldb/Utility/Status.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"

// C includes
// C++ includes

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms679293(v=vs.85).aspx
// https://chromium.googlesource.com/breakpad/breakpad/

namespace lldb_private {

namespace minidump {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

enum class MinidumpHeaderConstants : uint32_t {
  Signature = 0x504d444d, // 'PMDM'
  Version = 0x0000a793,   // 42899
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Signature)

};

enum class CvSignature : uint32_t {
  Pdb70 = 0x53445352, // RSDS
  ElfBuildId = 0x4270454c, // BpEL (Breakpad/Crashpad minidumps)
};

// Reference:
// https://crashpad.chromium.org/doxygen/structcrashpad_1_1CodeViewRecordPDB70.html
struct CvRecordPdb70 {
  uint8_t Uuid[16];
  llvm::support::ulittle32_t Age;
  // char PDBFileName[];
};
static_assert(sizeof(CvRecordPdb70) == 20,
              "sizeof CvRecordPdb70 is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680394.aspx
enum class MinidumpStreamType : uint32_t {
  Unused = 0,
  Reserved0 = 1,
  Reserved1 = 2,
  ThreadList = 3,
  ModuleList = 4,
  MemoryList = 5,
  Exception = 6,
  SystemInfo = 7,
  ThreadExList = 8,
  Memory64List = 9,
  CommentA = 10,
  CommentW = 11,
  HandleData = 12,
  FunctionTable = 13,
  UnloadedModuleList = 14,
  MiscInfo = 15,
  MemoryInfoList = 16,
  ThreadInfoList = 17,
  HandleOperationList = 18,
  Token = 19,
  JavascriptData = 20,
  SystemMemoryInfo = 21,
  ProcessVMCounters = 22,
  LastReserved = 0x0000ffff,

  /* Breakpad extension types.  0x4767 = "Gg" */
  BreakpadInfo = 0x47670001,
  AssertionInfo = 0x47670002,
  /* These are additional minidump stream values which are specific to
   * the linux breakpad implementation.   */
  LinuxCPUInfo = 0x47670003,    /* /proc/cpuinfo      */
  LinuxProcStatus = 0x47670004, /* /proc/$x/status    */
  LinuxLSBRelease = 0x47670005, /* /etc/lsb-release   */
  LinuxCMDLine = 0x47670006,    /* /proc/$x/cmdline   */
  LinuxEnviron = 0x47670007,    /* /proc/$x/environ   */
  LinuxAuxv = 0x47670008,       /* /proc/$x/auxv      */
  LinuxMaps = 0x47670009,       /* /proc/$x/maps      */
  LinuxDSODebug = 0x4767000A,
  LinuxProcStat = 0x4767000B,   /* /proc/$x/stat      */
  LinuxProcUptime = 0x4767000C, /* uptime             */
  LinuxProcFD = 0x4767000D,     /* /proc/$x/fb        */
};

// for MinidumpSystemInfo.processor_arch
enum class MinidumpCPUArchitecture : uint16_t {
  X86 = 0,         /* PROCESSOR_ARCHITECTURE_INTEL */
  MIPS = 1,        /* PROCESSOR_ARCHITECTURE_MIPS */
  Alpha = 2,       /* PROCESSOR_ARCHITECTURE_ALPHA */
  PPC = 3,         /* PROCESSOR_ARCHITECTURE_PPC */
  SHX = 4,         /* PROCESSOR_ARCHITECTURE_SHX (Super-H) */
  ARM = 5,         /* PROCESSOR_ARCHITECTURE_ARM */
  IA64 = 6,        /* PROCESSOR_ARCHITECTURE_IA64 */
  Alpha64 = 7,     /* PROCESSOR_ARCHITECTURE_ALPHA64 */
  MSIL = 8,        /* PROCESSOR_ARCHITECTURE_MSIL
                                              * (Microsoft Intermediate Language) */
  AMD64 = 9,       /* PROCESSOR_ARCHITECTURE_AMD64 */
  X86Win64 = 10,   /* PROCESSOR_ARCHITECTURE_IA32_ON_WIN64 (WoW64) */
  SPARC = 0x8001,  /* Breakpad-defined value for SPARC */
  PPC64 = 0x8002,  /* Breakpad-defined value for PPC64 */
  ARM64 = 0x8003,  /* Breakpad-defined value for ARM64 */
  MIPS64 = 0x8004, /* Breakpad-defined value for MIPS64 */
  Unknown = 0xffff /* PROCESSOR_ARCHITECTURE_UNKNOWN */
};

// for MinidumpSystemInfo.platform_id
enum class MinidumpOSPlatform : uint32_t {
  Win32S = 0,       /* VER_PLATFORM_WIN32s (Windows 3.1) */
  Win32Windows = 1, /* VER_PLATFORM_WIN32_WINDOWS (Windows 95-98-Me) */
  Win32NT = 2,      /* VER_PLATFORM_WIN32_NT (Windows NT, 2000+) */
  Win32CE = 3,      /* VER_PLATFORM_WIN32_CE, VER_PLATFORM_WIN32_HH
                                  * (Windows CE, Windows Mobile, "Handheld") */

  /* The following values are Breakpad-defined. */
  Unix = 0x8000,    /* Generic Unix-ish */
  MacOSX = 0x8101,  /* Mac OS X/Darwin */
  IOS = 0x8102,     /* iOS */
  Linux = 0x8201,   /* Linux */
  Solaris = 0x8202, /* Solaris */
  Android = 0x8203, /* Android */
  PS3 = 0x8204,     /* PS3 */
  NaCl = 0x8205     /* Native Client (NaCl) */
};

// For MinidumpCPUInfo.arm_cpu_info.elf_hwcaps.
// This matches the Linux kernel definitions from <asm/hwcaps.h>
enum class MinidumpPCPUInformationARMElfHwCaps : uint32_t {
  SWP = (1 << 0),
  Half = (1 << 1),
  Thumb = (1 << 2),
  _26BIT = (1 << 3),
  FastMult = (1 << 4),
  FPA = (1 << 5),
  VFP = (1 << 6),
  EDSP = (1 << 7),
  Java = (1 << 8),
  IWMMXT = (1 << 9),
  Crunch = (1 << 10),
  ThumbEE = (1 << 11),
  Neon = (1 << 12),
  VFPv3 = (1 << 13),
  VFPv3D16 = (1 << 14),
  TLS = (1 << 15),
  VFPv4 = (1 << 16),
  IDIVA = (1 << 17),
  IDIVT = (1 << 18),
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ IDIVT)
};

enum class MinidumpMiscInfoFlags : uint32_t {
  ProcessID = (1 << 0),
  ProcessTimes = (1 << 1),
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ ProcessTimes)
};

template <typename T>
Status consumeObject(llvm::ArrayRef<uint8_t> &Buffer, const T *&Object) {
  Status error;
  if (Buffer.size() < sizeof(T)) {
    error.SetErrorString("Insufficient buffer!");
    return error;
  }

  Object = reinterpret_cast<const T *>(Buffer.data());
  Buffer = Buffer.drop_front(sizeof(T));
  return error;
}

// parse a MinidumpString which is with UTF-16
// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680395(v=vs.85).aspx
llvm::Optional<std::string> parseMinidumpString(llvm::ArrayRef<uint8_t> &data);

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680378(v=vs.85).aspx
struct MinidumpHeader {
  llvm::support::ulittle32_t signature;
  llvm::support::ulittle32_t
      version; // The high 16 bits of version field are implementation specific
  llvm::support::ulittle32_t streams_count;
  llvm::support::ulittle32_t
      stream_directory_rva; // offset of the stream directory
  llvm::support::ulittle32_t checksum;
  llvm::support::ulittle32_t time_date_stamp; // time_t format
  llvm::support::ulittle64_t flags;

  static const MinidumpHeader *Parse(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpHeader) == 32,
              "sizeof MinidumpHeader is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680383.aspx
struct MinidumpLocationDescriptor {
  llvm::support::ulittle32_t data_size;
  llvm::support::ulittle32_t rva;
};
static_assert(sizeof(MinidumpLocationDescriptor) == 8,
              "sizeof MinidumpLocationDescriptor is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680384(v=vs.85).aspx
struct MinidumpMemoryDescriptor {
  llvm::support::ulittle64_t start_of_memory_range;
  MinidumpLocationDescriptor memory;

  static llvm::ArrayRef<MinidumpMemoryDescriptor>
  ParseMemoryList(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpMemoryDescriptor) == 16,
              "sizeof MinidumpMemoryDescriptor is not correct!");

struct MinidumpMemoryDescriptor64 {
  llvm::support::ulittle64_t start_of_memory_range;
  llvm::support::ulittle64_t data_size;

  static std::pair<llvm::ArrayRef<MinidumpMemoryDescriptor64>, uint64_t>
  ParseMemory64List(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpMemoryDescriptor64) == 16,
              "sizeof MinidumpMemoryDescriptor64 is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680365.aspx
struct MinidumpDirectory {
  llvm::support::ulittle32_t stream_type;
  MinidumpLocationDescriptor location;
};
static_assert(sizeof(MinidumpDirectory) == 12,
              "sizeof MinidumpDirectory is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680385(v=vs.85).aspx
struct MinidumpMemoryInfoListHeader {
  llvm::support::ulittle32_t size_of_header;
  llvm::support::ulittle32_t size_of_entry;
  llvm::support::ulittle64_t num_of_entries;
};
static_assert(sizeof(MinidumpMemoryInfoListHeader) == 16,
              "sizeof MinidumpMemoryInfoListHeader is not correct!");

enum class MinidumpMemoryInfoState : uint32_t {
  MemCommit = 0x1000,
  MemFree = 0x10000,
  MemReserve = 0x2000,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ MemFree)
};

enum class MinidumpMemoryInfoType : uint32_t {
  MemImage = 0x1000000,
  MemMapped = 0x40000,
  MemPrivate = 0x20000,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ MemImage)
};

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa366786(v=vs.85).aspx
enum class MinidumpMemoryProtectionContants : uint32_t {
  PageExecute = 0x10,
  PageExecuteRead = 0x20,
  PageExecuteReadWrite = 0x40,
  PageExecuteWriteCopy = 0x80,
  PageNoAccess = 0x01,
  PageReadOnly = 0x02,
  PageReadWrite = 0x04,
  PageWriteCopy = 0x08,
  PageTargetsInvalid = 0x40000000,
  PageTargetsNoUpdate = 0x40000000,

  PageWritable = PageExecuteReadWrite | PageExecuteWriteCopy | PageReadWrite |
                 PageWriteCopy,
  PageExecutable = PageExecute | PageExecuteRead | PageExecuteReadWrite |
                   PageExecuteWriteCopy,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ PageTargetsInvalid)
};

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680386(v=vs.85).aspx
struct MinidumpMemoryInfo {
  llvm::support::ulittle64_t base_address;
  llvm::support::ulittle64_t allocation_base;
  llvm::support::ulittle32_t allocation_protect;
  llvm::support::ulittle32_t alignment1;
  llvm::support::ulittle64_t region_size;
  llvm::support::ulittle32_t state;
  llvm::support::ulittle32_t protect;
  llvm::support::ulittle32_t type;
  llvm::support::ulittle32_t alignment2;

  static std::vector<const MinidumpMemoryInfo *>
  ParseMemoryInfoList(llvm::ArrayRef<uint8_t> &data);

  bool isReadable() const {
    const auto mask = MinidumpMemoryProtectionContants::PageNoAccess;
    return (static_cast<uint32_t>(mask) & protect) == 0;
  }

  bool isWritable() const {
    const auto mask = MinidumpMemoryProtectionContants::PageWritable;
    return (static_cast<uint32_t>(mask) & protect) != 0;
  }

  bool isExecutable() const {
    const auto mask = MinidumpMemoryProtectionContants::PageExecutable;
    return (static_cast<uint32_t>(mask) & protect) != 0;
  }
  
  bool isMapped() const {
    return state != static_cast<uint32_t>(MinidumpMemoryInfoState::MemFree);
  }
};

static_assert(sizeof(MinidumpMemoryInfo) == 48,
              "sizeof MinidumpMemoryInfo is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680517(v=vs.85).aspx
struct MinidumpThread {
  llvm::support::ulittle32_t thread_id;
  llvm::support::ulittle32_t suspend_count;
  llvm::support::ulittle32_t priority_class;
  llvm::support::ulittle32_t priority;
  llvm::support::ulittle64_t teb;
  MinidumpMemoryDescriptor stack;
  MinidumpLocationDescriptor thread_context;

  static const MinidumpThread *Parse(llvm::ArrayRef<uint8_t> &data);

  static llvm::ArrayRef<MinidumpThread>
  ParseThreadList(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpThread) == 48,
              "sizeof MinidumpThread is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680396(v=vs.85).aspx
union MinidumpCPUInfo {
  struct {
    llvm::support::ulittle32_t vendor_id[3];        /* cpuid 0: ebx, edx, ecx */
    llvm::support::ulittle32_t version_information; /* cpuid 1: eax */
    llvm::support::ulittle32_t feature_information; /* cpuid 1: edx */
    llvm::support::ulittle32_t
        amd_extended_cpu_features; /* cpuid 0x80000001, ebx */
  } x86_cpu_info;
  struct {
    llvm::support::ulittle32_t cpuid;
    llvm::support::ulittle32_t elf_hwcaps; /* linux specific, 0 otherwise */
  } arm_cpu_info;
  struct {
    llvm::support::ulittle64_t processor_features[2];
  } other_cpu_info;
};
static_assert(sizeof(MinidumpCPUInfo) == 24,
              "sizeof MinidumpCPUInfo is not correct!");

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680396(v=vs.85).aspx
struct MinidumpSystemInfo {
  llvm::support::ulittle16_t processor_arch;
  llvm::support::ulittle16_t processor_level;
  llvm::support::ulittle16_t processor_revision;

  uint8_t number_of_processors;
  uint8_t product_type;

  llvm::support::ulittle32_t major_version;
  llvm::support::ulittle32_t minor_version;
  llvm::support::ulittle32_t build_number;
  llvm::support::ulittle32_t platform_id;
  llvm::support::ulittle32_t csd_version_rva;

  llvm::support::ulittle16_t suit_mask;
  llvm::support::ulittle16_t reserved2;

  MinidumpCPUInfo cpu;

  static const MinidumpSystemInfo *Parse(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpSystemInfo) == 56,
              "sizeof MinidumpSystemInfo is not correct!");

// TODO misc2, misc3 ?
// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680389(v=vs.85).aspx
struct MinidumpMiscInfo {
  llvm::support::ulittle32_t size;
  // flags1 represents what info in the struct is valid
  llvm::support::ulittle32_t flags1;
  llvm::support::ulittle32_t process_id;
  llvm::support::ulittle32_t process_create_time;
  llvm::support::ulittle32_t process_user_time;
  llvm::support::ulittle32_t process_kernel_time;

  static const MinidumpMiscInfo *Parse(llvm::ArrayRef<uint8_t> &data);

  llvm::Optional<lldb::pid_t> GetPid() const;
};
static_assert(sizeof(MinidumpMiscInfo) == 24,
              "sizeof MinidumpMiscInfo is not correct!");

// The /proc/pid/status is saved as an ascii string in the file
class LinuxProcStatus {
public:
  llvm::StringRef proc_status;
  lldb::pid_t pid;

  static llvm::Optional<LinuxProcStatus> Parse(llvm::ArrayRef<uint8_t> &data);

  lldb::pid_t GetPid() const;

private:
  LinuxProcStatus() = default;
};

// MinidumpModule stuff
struct MinidumpVSFixedFileInfo {
  llvm::support::ulittle32_t signature;
  llvm::support::ulittle32_t struct_version;
  llvm::support::ulittle32_t file_version_hi;
  llvm::support::ulittle32_t file_version_lo;
  llvm::support::ulittle32_t product_version_hi;
  llvm::support::ulittle32_t product_version_lo;
  // file_flags_mask - identifies valid bits in fileFlags
  llvm::support::ulittle32_t file_flags_mask;
  llvm::support::ulittle32_t file_flags;
  llvm::support::ulittle32_t file_os;
  llvm::support::ulittle32_t file_type;
  llvm::support::ulittle32_t file_subtype;
  llvm::support::ulittle32_t file_date_hi;
  llvm::support::ulittle32_t file_date_lo;
};
static_assert(sizeof(MinidumpVSFixedFileInfo) == 52,
              "sizeof MinidumpVSFixedFileInfo is not correct!");

struct MinidumpModule {
  llvm::support::ulittle64_t base_of_image;
  llvm::support::ulittle32_t size_of_image;
  llvm::support::ulittle32_t checksum;
  llvm::support::ulittle32_t time_date_stamp;
  llvm::support::ulittle32_t module_name_rva;
  MinidumpVSFixedFileInfo version_info;
  MinidumpLocationDescriptor CV_record;
  MinidumpLocationDescriptor misc_record;
  llvm::support::ulittle32_t reserved0[2];
  llvm::support::ulittle32_t reserved1[2];

  static const MinidumpModule *Parse(llvm::ArrayRef<uint8_t> &data);

  static llvm::ArrayRef<MinidumpModule>
  ParseModuleList(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpModule) == 108,
              "sizeof MinidumpVSFixedFileInfo is not correct!");

// Exception stuff
struct MinidumpException {
  enum : unsigned {
    ExceptonInfoMaxParams = 15,
    DumpRequested = 0xFFFFFFFF,
  };

  llvm::support::ulittle32_t exception_code;
  llvm::support::ulittle32_t exception_flags;
  llvm::support::ulittle64_t exception_record;
  llvm::support::ulittle64_t exception_address;
  llvm::support::ulittle32_t number_parameters;
  llvm::support::ulittle32_t unused_alignment;
  llvm::support::ulittle64_t exception_information[ExceptonInfoMaxParams];
};
static_assert(sizeof(MinidumpException) == 152,
              "sizeof MinidumpException is not correct!");

struct MinidumpExceptionStream {
  llvm::support::ulittle32_t thread_id;
  llvm::support::ulittle32_t alignment;
  MinidumpException exception_record;
  MinidumpLocationDescriptor thread_context;

  static const MinidumpExceptionStream *Parse(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpExceptionStream) == 168,
              "sizeof MinidumpExceptionStream is not correct!");

} // namespace minidump
} // namespace lldb_private
#endif // liblldb_MinidumpTypes_h_
