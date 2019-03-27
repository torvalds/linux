//===-- ArchSpec.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ArchSpec.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-defines.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Host.h"

using namespace lldb;
using namespace lldb_private;

static bool cores_match(const ArchSpec::Core core1, const ArchSpec::Core core2,
                        bool try_inverse, bool enforce_exact_match);

namespace lldb_private {

struct CoreDefinition {
  ByteOrder default_byte_order;
  uint32_t addr_byte_size;
  uint32_t min_opcode_byte_size;
  uint32_t max_opcode_byte_size;
  llvm::Triple::ArchType machine;
  ArchSpec::Core core;
  const char *const name;
};

} // namespace lldb_private

// This core information can be looked using the ArchSpec::Core as the index
static const CoreDefinition g_core_definitions[] = {
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_generic,
     "arm"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv4,
     "armv4"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv4t,
     "armv4t"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv5,
     "armv5"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv5e,
     "armv5e"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv5t,
     "armv5t"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv6,
     "armv6"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv6m,
     "armv6m"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7,
     "armv7"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7f,
     "armv7f"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7s,
     "armv7s"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7k,
     "armv7k"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7m,
     "armv7m"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_armv7em,
     "armv7em"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::arm, ArchSpec::eCore_arm_xscale,
     "xscale"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumb,
     "thumb"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv4t,
     "thumbv4t"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv5,
     "thumbv5"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv5e,
     "thumbv5e"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv6,
     "thumbv6"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv6m,
     "thumbv6m"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7,
     "thumbv7"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7f,
     "thumbv7f"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7s,
     "thumbv7s"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7k,
     "thumbv7k"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7m,
     "thumbv7m"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::thumb, ArchSpec::eCore_thumbv7em,
     "thumbv7em"},
    {eByteOrderLittle, 8, 4, 4, llvm::Triple::aarch64,
     ArchSpec::eCore_arm_arm64, "arm64"},
    {eByteOrderLittle, 8, 4, 4, llvm::Triple::aarch64,
     ArchSpec::eCore_arm_armv8, "armv8"},
    {eByteOrderLittle, 8, 4, 4, llvm::Triple::aarch64,
     ArchSpec::eCore_arm_aarch64, "aarch64"},

    // mips32, mips32r2, mips32r3, mips32r5, mips32r6
    {eByteOrderBig, 4, 2, 4, llvm::Triple::mips, ArchSpec::eCore_mips32,
     "mips"},
    {eByteOrderBig, 4, 2, 4, llvm::Triple::mips, ArchSpec::eCore_mips32r2,
     "mipsr2"},
    {eByteOrderBig, 4, 2, 4, llvm::Triple::mips, ArchSpec::eCore_mips32r3,
     "mipsr3"},
    {eByteOrderBig, 4, 2, 4, llvm::Triple::mips, ArchSpec::eCore_mips32r5,
     "mipsr5"},
    {eByteOrderBig, 4, 2, 4, llvm::Triple::mips, ArchSpec::eCore_mips32r6,
     "mipsr6"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::mipsel, ArchSpec::eCore_mips32el,
     "mipsel"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::mipsel,
     ArchSpec::eCore_mips32r2el, "mipsr2el"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::mipsel,
     ArchSpec::eCore_mips32r3el, "mipsr3el"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::mipsel,
     ArchSpec::eCore_mips32r5el, "mipsr5el"},
    {eByteOrderLittle, 4, 2, 4, llvm::Triple::mipsel,
     ArchSpec::eCore_mips32r6el, "mipsr6el"},

    // mips64, mips64r2, mips64r3, mips64r5, mips64r6
    {eByteOrderBig, 8, 2, 4, llvm::Triple::mips64, ArchSpec::eCore_mips64,
     "mips64"},
    {eByteOrderBig, 8, 2, 4, llvm::Triple::mips64, ArchSpec::eCore_mips64r2,
     "mips64r2"},
    {eByteOrderBig, 8, 2, 4, llvm::Triple::mips64, ArchSpec::eCore_mips64r3,
     "mips64r3"},
    {eByteOrderBig, 8, 2, 4, llvm::Triple::mips64, ArchSpec::eCore_mips64r5,
     "mips64r5"},
    {eByteOrderBig, 8, 2, 4, llvm::Triple::mips64, ArchSpec::eCore_mips64r6,
     "mips64r6"},
    {eByteOrderLittle, 8, 2, 4, llvm::Triple::mips64el,
     ArchSpec::eCore_mips64el, "mips64el"},
    {eByteOrderLittle, 8, 2, 4, llvm::Triple::mips64el,
     ArchSpec::eCore_mips64r2el, "mips64r2el"},
    {eByteOrderLittle, 8, 2, 4, llvm::Triple::mips64el,
     ArchSpec::eCore_mips64r3el, "mips64r3el"},
    {eByteOrderLittle, 8, 2, 4, llvm::Triple::mips64el,
     ArchSpec::eCore_mips64r5el, "mips64r5el"},
    {eByteOrderLittle, 8, 2, 4, llvm::Triple::mips64el,
     ArchSpec::eCore_mips64r6el, "mips64r6el"},

    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_generic,
     "powerpc"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc601,
     "ppc601"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc602,
     "ppc602"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc603,
     "ppc603"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc603e,
     "ppc603e"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc603ev,
     "ppc603ev"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc604,
     "ppc604"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc604e,
     "ppc604e"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc620,
     "ppc620"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc750,
     "ppc750"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc7400,
     "ppc7400"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc7450,
     "ppc7450"},
    {eByteOrderBig, 4, 4, 4, llvm::Triple::ppc, ArchSpec::eCore_ppc_ppc970,
     "ppc970"},

    {eByteOrderLittle, 8, 4, 4, llvm::Triple::ppc64le,
     ArchSpec::eCore_ppc64le_generic, "powerpc64le"},
    {eByteOrderBig, 8, 4, 4, llvm::Triple::ppc64, ArchSpec::eCore_ppc64_generic,
     "powerpc64"},
    {eByteOrderBig, 8, 4, 4, llvm::Triple::ppc64,
     ArchSpec::eCore_ppc64_ppc970_64, "ppc970-64"},

    {eByteOrderBig, 8, 2, 6, llvm::Triple::systemz,
     ArchSpec::eCore_s390x_generic, "s390x"},

    {eByteOrderLittle, 4, 4, 4, llvm::Triple::sparc,
     ArchSpec::eCore_sparc_generic, "sparc"},
    {eByteOrderLittle, 8, 4, 4, llvm::Triple::sparcv9,
     ArchSpec::eCore_sparc9_generic, "sparcv9"},

    {eByteOrderLittle, 4, 1, 15, llvm::Triple::x86, ArchSpec::eCore_x86_32_i386,
     "i386"},
    {eByteOrderLittle, 4, 1, 15, llvm::Triple::x86, ArchSpec::eCore_x86_32_i486,
     "i486"},
    {eByteOrderLittle, 4, 1, 15, llvm::Triple::x86,
     ArchSpec::eCore_x86_32_i486sx, "i486sx"},
    {eByteOrderLittle, 4, 1, 15, llvm::Triple::x86, ArchSpec::eCore_x86_32_i686,
     "i686"},

    {eByteOrderLittle, 8, 1, 15, llvm::Triple::x86_64,
     ArchSpec::eCore_x86_64_x86_64, "x86_64"},
    {eByteOrderLittle, 8, 1, 15, llvm::Triple::x86_64,
     ArchSpec::eCore_x86_64_x86_64h, "x86_64h"},
    {eByteOrderLittle, 4, 4, 4, llvm::Triple::hexagon,
     ArchSpec::eCore_hexagon_generic, "hexagon"},
    {eByteOrderLittle, 4, 4, 4, llvm::Triple::hexagon,
     ArchSpec::eCore_hexagon_hexagonv4, "hexagonv4"},
    {eByteOrderLittle, 4, 4, 4, llvm::Triple::hexagon,
     ArchSpec::eCore_hexagon_hexagonv5, "hexagonv5"},

    {eByteOrderLittle, 4, 4, 4, llvm::Triple::UnknownArch,
     ArchSpec::eCore_uknownMach32, "unknown-mach-32"},
    {eByteOrderLittle, 8, 4, 4, llvm::Triple::UnknownArch,
     ArchSpec::eCore_uknownMach64, "unknown-mach-64"},

    {eByteOrderBig, 4, 1, 1, llvm::Triple::kalimba, ArchSpec::eCore_kalimba3,
     "kalimba3"},
    {eByteOrderLittle, 4, 1, 1, llvm::Triple::kalimba, ArchSpec::eCore_kalimba4,
     "kalimba4"},
    {eByteOrderLittle, 4, 1, 1, llvm::Triple::kalimba, ArchSpec::eCore_kalimba5,
     "kalimba5"}};

// Ensure that we have an entry in the g_core_definitions for each core. If you
// comment out an entry above, you will need to comment out the corresponding
// ArchSpec::Core enumeration.
static_assert(sizeof(g_core_definitions) / sizeof(CoreDefinition) ==
                  ArchSpec::kNumCores,
              "make sure we have one core definition for each core");

struct ArchDefinitionEntry {
  ArchSpec::Core core;
  uint32_t cpu;
  uint32_t sub;
  uint32_t cpu_mask;
  uint32_t sub_mask;
};

struct ArchDefinition {
  ArchitectureType type;
  size_t num_entries;
  const ArchDefinitionEntry *entries;
  const char *name;
};

void ArchSpec::ListSupportedArchNames(StringList &list) {
  for (uint32_t i = 0; i < llvm::array_lengthof(g_core_definitions); ++i)
    list.AppendString(g_core_definitions[i].name);
}

size_t ArchSpec::AutoComplete(CompletionRequest &request) {
  if (!request.GetCursorArgumentPrefix().empty()) {
    for (uint32_t i = 0; i < llvm::array_lengthof(g_core_definitions); ++i) {
      if (NameMatches(g_core_definitions[i].name, NameMatch::StartsWith,
                      request.GetCursorArgumentPrefix()))
        request.AddCompletion(g_core_definitions[i].name);
    }
  } else {
    StringList matches;
    ListSupportedArchNames(matches);
    request.AddCompletions(matches);
  }
  return request.GetNumberOfMatches();
}

#define CPU_ANY (UINT32_MAX)

//===----------------------------------------------------------------------===//
// A table that gets searched linearly for matches. This table is used to
// convert cpu type and subtypes to architecture names, and to convert
// architecture names to cpu types and subtypes. The ordering is important and
// allows the precedence to be set when the table is built.
#define SUBTYPE_MASK 0x00FFFFFFu

static const ArchDefinitionEntry g_macho_arch_entries[] = {
    {ArchSpec::eCore_arm_generic, llvm::MachO::CPU_TYPE_ARM, CPU_ANY,
     UINT32_MAX, UINT32_MAX},
    {ArchSpec::eCore_arm_generic, llvm::MachO::CPU_TYPE_ARM, 0, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv4, llvm::MachO::CPU_TYPE_ARM, 5, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv4t, llvm::MachO::CPU_TYPE_ARM, 5, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv6, llvm::MachO::CPU_TYPE_ARM, 6, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv6m, llvm::MachO::CPU_TYPE_ARM, 14, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv5, llvm::MachO::CPU_TYPE_ARM, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv5e, llvm::MachO::CPU_TYPE_ARM, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv5t, llvm::MachO::CPU_TYPE_ARM, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_xscale, llvm::MachO::CPU_TYPE_ARM, 8, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7, llvm::MachO::CPU_TYPE_ARM, 9, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7f, llvm::MachO::CPU_TYPE_ARM, 10, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7s, llvm::MachO::CPU_TYPE_ARM, 11, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7k, llvm::MachO::CPU_TYPE_ARM, 12, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7m, llvm::MachO::CPU_TYPE_ARM, 15, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_armv7em, llvm::MachO::CPU_TYPE_ARM, 16, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_arm64, llvm::MachO::CPU_TYPE_ARM64, 1, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_arm64, llvm::MachO::CPU_TYPE_ARM64, 0, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_arm64, llvm::MachO::CPU_TYPE_ARM64, 13, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_arm_arm64, llvm::MachO::CPU_TYPE_ARM64, CPU_ANY,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_thumb, llvm::MachO::CPU_TYPE_ARM, 0, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv4t, llvm::MachO::CPU_TYPE_ARM, 5, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv5, llvm::MachO::CPU_TYPE_ARM, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv5e, llvm::MachO::CPU_TYPE_ARM, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv6, llvm::MachO::CPU_TYPE_ARM, 6, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv6m, llvm::MachO::CPU_TYPE_ARM, 14, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7, llvm::MachO::CPU_TYPE_ARM, 9, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7f, llvm::MachO::CPU_TYPE_ARM, 10, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7s, llvm::MachO::CPU_TYPE_ARM, 11, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7k, llvm::MachO::CPU_TYPE_ARM, 12, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7m, llvm::MachO::CPU_TYPE_ARM, 15, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_thumbv7em, llvm::MachO::CPU_TYPE_ARM, 16, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_generic, llvm::MachO::CPU_TYPE_POWERPC, CPU_ANY,
     UINT32_MAX, UINT32_MAX},
    {ArchSpec::eCore_ppc_generic, llvm::MachO::CPU_TYPE_POWERPC, 0, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc601, llvm::MachO::CPU_TYPE_POWERPC, 1, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc602, llvm::MachO::CPU_TYPE_POWERPC, 2, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc603, llvm::MachO::CPU_TYPE_POWERPC, 3, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc603e, llvm::MachO::CPU_TYPE_POWERPC, 4, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc603ev, llvm::MachO::CPU_TYPE_POWERPC, 5, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc604, llvm::MachO::CPU_TYPE_POWERPC, 6, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc604e, llvm::MachO::CPU_TYPE_POWERPC, 7, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc620, llvm::MachO::CPU_TYPE_POWERPC, 8, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc750, llvm::MachO::CPU_TYPE_POWERPC, 9, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc7400, llvm::MachO::CPU_TYPE_POWERPC, 10, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc7450, llvm::MachO::CPU_TYPE_POWERPC, 11, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc_ppc970, llvm::MachO::CPU_TYPE_POWERPC, 100, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_ppc64_generic, llvm::MachO::CPU_TYPE_POWERPC64, 0,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_ppc64le_generic, llvm::MachO::CPU_TYPE_POWERPC64, CPU_ANY,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_ppc64_ppc970_64, llvm::MachO::CPU_TYPE_POWERPC64, 100,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_x86_32_i386, llvm::MachO::CPU_TYPE_I386, 3, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_x86_32_i486, llvm::MachO::CPU_TYPE_I386, 4, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_x86_32_i486sx, llvm::MachO::CPU_TYPE_I386, 0x84,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_x86_32_i386, llvm::MachO::CPU_TYPE_I386, CPU_ANY,
     UINT32_MAX, UINT32_MAX},
    {ArchSpec::eCore_x86_64_x86_64, llvm::MachO::CPU_TYPE_X86_64, 3, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_x86_64_x86_64, llvm::MachO::CPU_TYPE_X86_64, 4, UINT32_MAX,
     SUBTYPE_MASK},
    {ArchSpec::eCore_x86_64_x86_64h, llvm::MachO::CPU_TYPE_X86_64, 8,
     UINT32_MAX, SUBTYPE_MASK},
    {ArchSpec::eCore_x86_64_x86_64, llvm::MachO::CPU_TYPE_X86_64, CPU_ANY,
     UINT32_MAX, UINT32_MAX},
    // Catch any unknown mach architectures so we can always use the object and
    // symbol mach-o files
    {ArchSpec::eCore_uknownMach32, 0, 0, 0xFF000000u, 0x00000000u},
    {ArchSpec::eCore_uknownMach64, llvm::MachO::CPU_ARCH_ABI64, 0, 0xFF000000u,
     0x00000000u}};

static const ArchDefinition g_macho_arch_def = {
    eArchTypeMachO, llvm::array_lengthof(g_macho_arch_entries),
    g_macho_arch_entries, "mach-o"};

//===----------------------------------------------------------------------===//
// A table that gets searched linearly for matches. This table is used to
// convert cpu type and subtypes to architecture names, and to convert
// architecture names to cpu types and subtypes. The ordering is important and
// allows the precedence to be set when the table is built.
static const ArchDefinitionEntry g_elf_arch_entries[] = {
    {ArchSpec::eCore_sparc_generic, llvm::ELF::EM_SPARC, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // Sparc
    {ArchSpec::eCore_x86_32_i386, llvm::ELF::EM_386, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // Intel 80386
    {ArchSpec::eCore_x86_32_i486, llvm::ELF::EM_IAMCU, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // Intel MCU // FIXME: is this correct?
    {ArchSpec::eCore_ppc_generic, llvm::ELF::EM_PPC, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // PowerPC
    {ArchSpec::eCore_ppc64le_generic, llvm::ELF::EM_PPC64, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // PowerPC64le
    {ArchSpec::eCore_ppc64_generic, llvm::ELF::EM_PPC64, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // PowerPC64
    {ArchSpec::eCore_arm_generic, llvm::ELF::EM_ARM, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // ARM
    {ArchSpec::eCore_arm_aarch64, llvm::ELF::EM_AARCH64, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // ARM64
    {ArchSpec::eCore_s390x_generic, llvm::ELF::EM_S390, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // SystemZ
    {ArchSpec::eCore_sparc9_generic, llvm::ELF::EM_SPARCV9,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // SPARC V9
    {ArchSpec::eCore_x86_64_x86_64, llvm::ELF::EM_X86_64, LLDB_INVALID_CPUTYPE,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // AMD64
    {ArchSpec::eCore_mips32, llvm::ELF::EM_MIPS, ArchSpec::eMIPSSubType_mips32,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32
    {ArchSpec::eCore_mips32r2, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips32r2, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32r2
    {ArchSpec::eCore_mips32r6, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips32r6, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32r6
    {ArchSpec::eCore_mips32el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips32el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32el
    {ArchSpec::eCore_mips32r2el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips32r2el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32r2el
    {ArchSpec::eCore_mips32r6el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips32r6el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips32r6el
    {ArchSpec::eCore_mips64, llvm::ELF::EM_MIPS, ArchSpec::eMIPSSubType_mips64,
     0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64
    {ArchSpec::eCore_mips64r2, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips64r2, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64r2
    {ArchSpec::eCore_mips64r6, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips64r6, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64r6
    {ArchSpec::eCore_mips64el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips64el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64el
    {ArchSpec::eCore_mips64r2el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips64r2el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64r2el
    {ArchSpec::eCore_mips64r6el, llvm::ELF::EM_MIPS,
     ArchSpec::eMIPSSubType_mips64r6el, 0xFFFFFFFFu, 0xFFFFFFFFu}, // mips64r6el
    {ArchSpec::eCore_hexagon_generic, llvm::ELF::EM_HEXAGON,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // HEXAGON
    {ArchSpec::eCore_kalimba3, llvm::ELF::EM_CSR_KALIMBA,
     llvm::Triple::KalimbaSubArch_v3, 0xFFFFFFFFu, 0xFFFFFFFFu}, // KALIMBA
    {ArchSpec::eCore_kalimba4, llvm::ELF::EM_CSR_KALIMBA,
     llvm::Triple::KalimbaSubArch_v4, 0xFFFFFFFFu, 0xFFFFFFFFu}, // KALIMBA
    {ArchSpec::eCore_kalimba5, llvm::ELF::EM_CSR_KALIMBA,
     llvm::Triple::KalimbaSubArch_v5, 0xFFFFFFFFu, 0xFFFFFFFFu} // KALIMBA
};

static const ArchDefinition g_elf_arch_def = {
    eArchTypeELF,
    llvm::array_lengthof(g_elf_arch_entries),
    g_elf_arch_entries,
    "elf",
};

static const ArchDefinitionEntry g_coff_arch_entries[] = {
    {ArchSpec::eCore_x86_32_i386, llvm::COFF::IMAGE_FILE_MACHINE_I386,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // Intel 80x86
    {ArchSpec::eCore_ppc_generic, llvm::COFF::IMAGE_FILE_MACHINE_POWERPC,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // PowerPC
    {ArchSpec::eCore_ppc_generic, llvm::COFF::IMAGE_FILE_MACHINE_POWERPCFP,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // PowerPC (with FPU)
    {ArchSpec::eCore_arm_generic, llvm::COFF::IMAGE_FILE_MACHINE_ARM,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // ARM
    {ArchSpec::eCore_arm_armv7, llvm::COFF::IMAGE_FILE_MACHINE_ARMNT,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // ARMv7
    {ArchSpec::eCore_thumb, llvm::COFF::IMAGE_FILE_MACHINE_THUMB,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu}, // ARMv7
    {ArchSpec::eCore_x86_64_x86_64, llvm::COFF::IMAGE_FILE_MACHINE_AMD64,
     LLDB_INVALID_CPUTYPE, 0xFFFFFFFFu, 0xFFFFFFFFu} // AMD64
};

static const ArchDefinition g_coff_arch_def = {
    eArchTypeCOFF,
    llvm::array_lengthof(g_coff_arch_entries),
    g_coff_arch_entries,
    "pe-coff",
};

//===----------------------------------------------------------------------===//
// Table of all ArchDefinitions
static const ArchDefinition *g_arch_definitions[] = {
    &g_macho_arch_def, &g_elf_arch_def, &g_coff_arch_def};

static const size_t k_num_arch_definitions =
    llvm::array_lengthof(g_arch_definitions);

//===----------------------------------------------------------------------===//
// Static helper functions.

// Get the architecture definition for a given object type.
static const ArchDefinition *FindArchDefinition(ArchitectureType arch_type) {
  for (unsigned int i = 0; i < k_num_arch_definitions; ++i) {
    const ArchDefinition *def = g_arch_definitions[i];
    if (def->type == arch_type)
      return def;
  }
  return nullptr;
}

// Get an architecture definition by name.
static const CoreDefinition *FindCoreDefinition(llvm::StringRef name) {
  for (unsigned int i = 0; i < llvm::array_lengthof(g_core_definitions); ++i) {
    if (name.equals_lower(g_core_definitions[i].name))
      return &g_core_definitions[i];
  }
  return nullptr;
}

static inline const CoreDefinition *FindCoreDefinition(ArchSpec::Core core) {
  if (core < llvm::array_lengthof(g_core_definitions))
    return &g_core_definitions[core];
  return nullptr;
}

// Get a definition entry by cpu type and subtype.
static const ArchDefinitionEntry *
FindArchDefinitionEntry(const ArchDefinition *def, uint32_t cpu, uint32_t sub) {
  if (def == nullptr)
    return nullptr;

  const ArchDefinitionEntry *entries = def->entries;
  for (size_t i = 0; i < def->num_entries; ++i) {
    if (entries[i].cpu == (cpu & entries[i].cpu_mask))
      if (entries[i].sub == (sub & entries[i].sub_mask))
        return &entries[i];
  }
  return nullptr;
}

static const ArchDefinitionEntry *
FindArchDefinitionEntry(const ArchDefinition *def, ArchSpec::Core core) {
  if (def == nullptr)
    return nullptr;

  const ArchDefinitionEntry *entries = def->entries;
  for (size_t i = 0; i < def->num_entries; ++i) {
    if (entries[i].core == core)
      return &entries[i];
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Constructors and destructors.

ArchSpec::ArchSpec() {}

ArchSpec::ArchSpec(const char *triple_cstr) {
  if (triple_cstr)
    SetTriple(triple_cstr);
}

ArchSpec::ArchSpec(llvm::StringRef triple_str) { SetTriple(triple_str); }

ArchSpec::ArchSpec(const llvm::Triple &triple) { SetTriple(triple); }

ArchSpec::ArchSpec(ArchitectureType arch_type, uint32_t cpu, uint32_t subtype) {
  SetArchitecture(arch_type, cpu, subtype);
}

ArchSpec::~ArchSpec() = default;

//===----------------------------------------------------------------------===//
// Assignment and initialization.

const ArchSpec &ArchSpec::operator=(const ArchSpec &rhs) {
  if (this != &rhs) {
    m_triple = rhs.m_triple;
    m_core = rhs.m_core;
    m_byte_order = rhs.m_byte_order;
    m_distribution_id = rhs.m_distribution_id;
    m_flags = rhs.m_flags;
  }
  return *this;
}

void ArchSpec::Clear() {
  m_triple = llvm::Triple();
  m_core = kCore_invalid;
  m_byte_order = eByteOrderInvalid;
  m_distribution_id.Clear();
  m_flags = 0;
}

//===----------------------------------------------------------------------===//
// Predicates.

const char *ArchSpec::GetArchitectureName() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def)
    return core_def->name;
  return "unknown";
}

bool ArchSpec::IsMIPS() const {
  const llvm::Triple::ArchType machine = GetMachine();
  return machine == llvm::Triple::mips || machine == llvm::Triple::mipsel ||
         machine == llvm::Triple::mips64 || machine == llvm::Triple::mips64el;
}

std::string ArchSpec::GetTargetABI() const {

  std::string abi;

  if (IsMIPS()) {
    switch (GetFlags() & ArchSpec::eMIPSABI_mask) {
    case ArchSpec::eMIPSABI_N64:
      abi = "n64";
      return abi;
    case ArchSpec::eMIPSABI_N32:
      abi = "n32";
      return abi;
    case ArchSpec::eMIPSABI_O32:
      abi = "o32";
      return abi;
    default:
      return abi;
    }
  }
  return abi;
}

void ArchSpec::SetFlags(std::string elf_abi) {

  uint32_t flag = GetFlags();
  if (IsMIPS()) {
    if (elf_abi == "n64")
      flag |= ArchSpec::eMIPSABI_N64;
    else if (elf_abi == "n32")
      flag |= ArchSpec::eMIPSABI_N32;
    else if (elf_abi == "o32")
      flag |= ArchSpec::eMIPSABI_O32;
  }
  SetFlags(flag);
}

std::string ArchSpec::GetClangTargetCPU() const {
  std::string cpu;
  const llvm::Triple::ArchType machine = GetMachine();

  if (machine == llvm::Triple::mips || machine == llvm::Triple::mipsel ||
      machine == llvm::Triple::mips64 || machine == llvm::Triple::mips64el) {
    switch (m_core) {
    case ArchSpec::eCore_mips32:
    case ArchSpec::eCore_mips32el:
      cpu = "mips32";
      break;
    case ArchSpec::eCore_mips32r2:
    case ArchSpec::eCore_mips32r2el:
      cpu = "mips32r2";
      break;
    case ArchSpec::eCore_mips32r3:
    case ArchSpec::eCore_mips32r3el:
      cpu = "mips32r3";
      break;
    case ArchSpec::eCore_mips32r5:
    case ArchSpec::eCore_mips32r5el:
      cpu = "mips32r5";
      break;
    case ArchSpec::eCore_mips32r6:
    case ArchSpec::eCore_mips32r6el:
      cpu = "mips32r6";
      break;
    case ArchSpec::eCore_mips64:
    case ArchSpec::eCore_mips64el:
      cpu = "mips64";
      break;
    case ArchSpec::eCore_mips64r2:
    case ArchSpec::eCore_mips64r2el:
      cpu = "mips64r2";
      break;
    case ArchSpec::eCore_mips64r3:
    case ArchSpec::eCore_mips64r3el:
      cpu = "mips64r3";
      break;
    case ArchSpec::eCore_mips64r5:
    case ArchSpec::eCore_mips64r5el:
      cpu = "mips64r5";
      break;
    case ArchSpec::eCore_mips64r6:
    case ArchSpec::eCore_mips64r6el:
      cpu = "mips64r6";
      break;
    default:
      break;
    }
  }
  return cpu;
}

uint32_t ArchSpec::GetMachOCPUType() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def) {
    const ArchDefinitionEntry *arch_def =
        FindArchDefinitionEntry(&g_macho_arch_def, core_def->core);
    if (arch_def) {
      return arch_def->cpu;
    }
  }
  return LLDB_INVALID_CPUTYPE;
}

uint32_t ArchSpec::GetMachOCPUSubType() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def) {
    const ArchDefinitionEntry *arch_def =
        FindArchDefinitionEntry(&g_macho_arch_def, core_def->core);
    if (arch_def) {
      return arch_def->sub;
    }
  }
  return LLDB_INVALID_CPUTYPE;
}

uint32_t ArchSpec::GetDataByteSize() const {
  switch (m_core) {
  case eCore_kalimba3:
    return 4;
  case eCore_kalimba4:
    return 1;
  case eCore_kalimba5:
    return 4;
  default:
    return 1;
  }
  return 1;
}

uint32_t ArchSpec::GetCodeByteSize() const {
  switch (m_core) {
  case eCore_kalimba3:
    return 4;
  case eCore_kalimba4:
    return 1;
  case eCore_kalimba5:
    return 1;
  default:
    return 1;
  }
  return 1;
}

llvm::Triple::ArchType ArchSpec::GetMachine() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def)
    return core_def->machine;

  return llvm::Triple::UnknownArch;
}

const ConstString &ArchSpec::GetDistributionId() const {
  return m_distribution_id;
}

void ArchSpec::SetDistributionId(const char *distribution_id) {
  m_distribution_id.SetCString(distribution_id);
}

uint32_t ArchSpec::GetAddressByteSize() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def) {
    if (core_def->machine == llvm::Triple::mips64 ||
        core_def->machine == llvm::Triple::mips64el) {
      // For N32/O32 applications Address size is 4 bytes.
      if (m_flags & (eMIPSABI_N32 | eMIPSABI_O32))
        return 4;
    }
    return core_def->addr_byte_size;
  }
  return 0;
}

ByteOrder ArchSpec::GetDefaultEndian() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def)
    return core_def->default_byte_order;
  return eByteOrderInvalid;
}

bool ArchSpec::CharIsSignedByDefault() const {
  switch (m_triple.getArch()) {
  default:
    return true;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return m_triple.isOSDarwin() || m_triple.isOSWindows();

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
    return m_triple.isOSDarwin();

  case llvm::Triple::ppc64le:
  case llvm::Triple::systemz:
  case llvm::Triple::xcore:
  case llvm::Triple::arc:
    return false;
  }
}

lldb::ByteOrder ArchSpec::GetByteOrder() const {
  if (m_byte_order == eByteOrderInvalid)
    return GetDefaultEndian();
  return m_byte_order;
}

//===----------------------------------------------------------------------===//
// Mutators.

bool ArchSpec::SetTriple(const llvm::Triple &triple) {
  m_triple = triple;
  UpdateCore();
  return IsValid();
}

bool lldb_private::ParseMachCPUDashSubtypeTriple(llvm::StringRef triple_str,
                                                 ArchSpec &arch) {
  // Accept "12-10" or "12.10" as cpu type/subtype
  if (triple_str.empty())
    return false;

  size_t pos = triple_str.find_first_of("-.");
  if (pos == llvm::StringRef::npos)
    return false;

  llvm::StringRef cpu_str = triple_str.substr(0, pos);
  llvm::StringRef remainder = triple_str.substr(pos + 1);
  if (cpu_str.empty() || remainder.empty())
    return false;

  llvm::StringRef sub_str;
  llvm::StringRef vendor;
  llvm::StringRef os;
  std::tie(sub_str, remainder) = remainder.split('-');
  std::tie(vendor, os) = remainder.split('-');

  uint32_t cpu = 0;
  uint32_t sub = 0;
  if (cpu_str.getAsInteger(10, cpu) || sub_str.getAsInteger(10, sub))
    return false;

  if (!arch.SetArchitecture(eArchTypeMachO, cpu, sub))
    return false;
  if (!vendor.empty() && !os.empty()) {
    arch.GetTriple().setVendorName(vendor);
    arch.GetTriple().setOSName(os);
  }

  return true;
}

bool ArchSpec::SetTriple(llvm::StringRef triple) {
  if (triple.empty()) {
    Clear();
    return false;
  }

  if (ParseMachCPUDashSubtypeTriple(triple, *this))
    return true;

  SetTriple(llvm::Triple(llvm::Triple::normalize(triple)));
  return IsValid();
}

bool ArchSpec::ContainsOnlyArch(const llvm::Triple &normalized_triple) {
  return !normalized_triple.getArchName().empty() &&
         normalized_triple.getOSName().empty() &&
         normalized_triple.getVendorName().empty() &&
         normalized_triple.getEnvironmentName().empty();
}

void ArchSpec::MergeFrom(const ArchSpec &other) {
  if (TripleVendorIsUnspecifiedUnknown() &&
      !other.TripleVendorIsUnspecifiedUnknown())
    GetTriple().setVendor(other.GetTriple().getVendor());
  if (TripleOSIsUnspecifiedUnknown() && !other.TripleOSIsUnspecifiedUnknown())
    GetTriple().setOS(other.GetTriple().getOS());
  if (GetTriple().getArch() == llvm::Triple::UnknownArch) {
    GetTriple().setArch(other.GetTriple().getArch());

    // MachO unknown64 isn't really invalid as the debugger can still obtain
    // information from the binary, e.g. line tables. As such, we don't update
    // the core here.
    if (other.GetCore() != eCore_uknownMach64)
      UpdateCore();
  }
  if (GetTriple().getEnvironment() == llvm::Triple::UnknownEnvironment &&
      !TripleVendorWasSpecified()) {
    if (other.TripleVendorWasSpecified())
      GetTriple().setEnvironment(other.GetTriple().getEnvironment());
  }
  // If this and other are both arm ArchSpecs and this ArchSpec is a generic
  // "some kind of arm" spec but the other ArchSpec is a specific arm core,
  // adopt the specific arm core.
  if (GetTriple().getArch() == llvm::Triple::arm &&
      other.GetTriple().getArch() == llvm::Triple::arm &&
      IsCompatibleMatch(other) && GetCore() == ArchSpec::eCore_arm_generic &&
      other.GetCore() != ArchSpec::eCore_arm_generic) {
    m_core = other.GetCore();
    CoreUpdated(true);
  }
  if (GetFlags() == 0) {
    SetFlags(other.GetFlags());
  }
}

bool ArchSpec::SetArchitecture(ArchitectureType arch_type, uint32_t cpu,
                               uint32_t sub, uint32_t os) {
  m_core = kCore_invalid;
  bool update_triple = true;
  const ArchDefinition *arch_def = FindArchDefinition(arch_type);
  if (arch_def) {
    const ArchDefinitionEntry *arch_def_entry =
        FindArchDefinitionEntry(arch_def, cpu, sub);
    if (arch_def_entry) {
      const CoreDefinition *core_def = FindCoreDefinition(arch_def_entry->core);
      if (core_def) {
        m_core = core_def->core;
        update_triple = false;
        // Always use the architecture name because it might be more
        // descriptive than the architecture enum ("armv7" ->
        // llvm::Triple::arm).
        m_triple.setArchName(llvm::StringRef(core_def->name));
        if (arch_type == eArchTypeMachO) {
          m_triple.setVendor(llvm::Triple::Apple);

          // Don't set the OS.  It could be simulator, macosx, ios, watchos,
          // tvos, bridgeos.  We could get close with the cpu type - but we 
          // can't get it right all of the time.  Better to leave this unset 
          // so other sections of code will set it when they have more 
          // information. NB: don't call m_triple.setOS (llvm::Triple::UnknownOS).  
          // That sets the OSName to "unknown" and the 
          // ArchSpec::TripleVendorWasSpecified() method says that any OSName 
          // setting means it was specified.
        } else if (arch_type == eArchTypeELF) {
          switch (os) {
          case llvm::ELF::ELFOSABI_AIX:
            m_triple.setOS(llvm::Triple::OSType::AIX);
            break;
          case llvm::ELF::ELFOSABI_FREEBSD:
            m_triple.setOS(llvm::Triple::OSType::FreeBSD);
            break;
          case llvm::ELF::ELFOSABI_GNU:
            m_triple.setOS(llvm::Triple::OSType::Linux);
            break;
          case llvm::ELF::ELFOSABI_NETBSD:
            m_triple.setOS(llvm::Triple::OSType::NetBSD);
            break;
          case llvm::ELF::ELFOSABI_OPENBSD:
            m_triple.setOS(llvm::Triple::OSType::OpenBSD);
            break;
          case llvm::ELF::ELFOSABI_SOLARIS:
            m_triple.setOS(llvm::Triple::OSType::Solaris);
            break;
          }
        } else if (arch_type == eArchTypeCOFF && os == llvm::Triple::Win32) {
          m_triple.setVendor(llvm::Triple::PC);
          m_triple.setOS(llvm::Triple::Win32);
        } else {
          m_triple.setVendor(llvm::Triple::UnknownVendor);
          m_triple.setOS(llvm::Triple::UnknownOS);
        }
        // Fall back onto setting the machine type if the arch by name
        // failed...
        if (m_triple.getArch() == llvm::Triple::UnknownArch)
          m_triple.setArch(core_def->machine);
      }
    } else {
      Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_TARGET | LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_PLATFORM));
      if (log)
        log->Printf("Unable to find a core definition for cpu 0x%" PRIx32 " sub %" PRId32, cpu, sub);
    }
  }
  CoreUpdated(update_triple);
  return IsValid();
}

uint32_t ArchSpec::GetMinimumOpcodeByteSize() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def)
    return core_def->min_opcode_byte_size;
  return 0;
}

uint32_t ArchSpec::GetMaximumOpcodeByteSize() const {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def)
    return core_def->max_opcode_byte_size;
  return 0;
}

bool ArchSpec::IsExactMatch(const ArchSpec &rhs) const {
  return IsEqualTo(rhs, true);
}

bool ArchSpec::IsCompatibleMatch(const ArchSpec &rhs) const {
  return IsEqualTo(rhs, false);
}

static bool IsCompatibleEnvironment(llvm::Triple::EnvironmentType lhs,
                                    llvm::Triple::EnvironmentType rhs) {
  if (lhs == rhs)
    return true;

  // If any of the environment is unknown then they are compatible
  if (lhs == llvm::Triple::UnknownEnvironment ||
      rhs == llvm::Triple::UnknownEnvironment)
    return true;

  // If one of the environment is Android and the other one is EABI then they
  // are considered to be compatible. This is required as a workaround for
  // shared libraries compiled for Android without the NOTE section indicating
  // that they are using the Android ABI.
  if ((lhs == llvm::Triple::Android && rhs == llvm::Triple::EABI) ||
      (rhs == llvm::Triple::Android && lhs == llvm::Triple::EABI) ||
      (lhs == llvm::Triple::GNUEABI && rhs == llvm::Triple::EABI) ||
      (rhs == llvm::Triple::GNUEABI && lhs == llvm::Triple::EABI) ||
      (lhs == llvm::Triple::GNUEABIHF && rhs == llvm::Triple::EABIHF) ||
      (rhs == llvm::Triple::GNUEABIHF && lhs == llvm::Triple::EABIHF))
    return true;

  return false;
}

bool ArchSpec::IsEqualTo(const ArchSpec &rhs, bool exact_match) const {
  // explicitly ignoring m_distribution_id in this method.

  if (GetByteOrder() != rhs.GetByteOrder())
    return false;

  const ArchSpec::Core lhs_core = GetCore();
  const ArchSpec::Core rhs_core = rhs.GetCore();

  const bool core_match = cores_match(lhs_core, rhs_core, true, exact_match);

  if (core_match) {
    const llvm::Triple &lhs_triple = GetTriple();
    const llvm::Triple &rhs_triple = rhs.GetTriple();

    const llvm::Triple::VendorType lhs_triple_vendor = lhs_triple.getVendor();
    const llvm::Triple::VendorType rhs_triple_vendor = rhs_triple.getVendor();
    if (lhs_triple_vendor != rhs_triple_vendor) {
      const bool rhs_vendor_specified = rhs.TripleVendorWasSpecified();
      const bool lhs_vendor_specified = TripleVendorWasSpecified();
      // Both architectures had the vendor specified, so if they aren't equal
      // then we return false
      if (rhs_vendor_specified && lhs_vendor_specified)
        return false;

      // Only fail if both vendor types are not unknown
      if (lhs_triple_vendor != llvm::Triple::UnknownVendor &&
          rhs_triple_vendor != llvm::Triple::UnknownVendor)
        return false;
    }

    const llvm::Triple::OSType lhs_triple_os = lhs_triple.getOS();
    const llvm::Triple::OSType rhs_triple_os = rhs_triple.getOS();
    if (lhs_triple_os != rhs_triple_os) {
      const bool rhs_os_specified = rhs.TripleOSWasSpecified();
      const bool lhs_os_specified = TripleOSWasSpecified();
      // Both architectures had the OS specified, so if they aren't equal then
      // we return false
      if (rhs_os_specified && lhs_os_specified)
        return false;

      // Only fail if both os types are not unknown
      if (lhs_triple_os != llvm::Triple::UnknownOS &&
          rhs_triple_os != llvm::Triple::UnknownOS)
        return false;
    }

    const llvm::Triple::EnvironmentType lhs_triple_env =
        lhs_triple.getEnvironment();
    const llvm::Triple::EnvironmentType rhs_triple_env =
        rhs_triple.getEnvironment();

    return IsCompatibleEnvironment(lhs_triple_env, rhs_triple_env);
  }
  return false;
}

void ArchSpec::UpdateCore() {
  llvm::StringRef arch_name(m_triple.getArchName());
  const CoreDefinition *core_def = FindCoreDefinition(arch_name);
  if (core_def) {
    m_core = core_def->core;
    // Set the byte order to the default byte order for an architecture. This
    // can be modified if needed for cases when cores handle both big and
    // little endian
    m_byte_order = core_def->default_byte_order;
  } else {
    Clear();
  }
}

//===----------------------------------------------------------------------===//
// Helper methods.

void ArchSpec::CoreUpdated(bool update_triple) {
  const CoreDefinition *core_def = FindCoreDefinition(m_core);
  if (core_def) {
    if (update_triple)
      m_triple = llvm::Triple(core_def->name, "unknown", "unknown");
    m_byte_order = core_def->default_byte_order;
  } else {
    if (update_triple)
      m_triple = llvm::Triple();
    m_byte_order = eByteOrderInvalid;
  }
}

//===----------------------------------------------------------------------===//
// Operators.

static bool cores_match(const ArchSpec::Core core1, const ArchSpec::Core core2,
                        bool try_inverse, bool enforce_exact_match) {
  if (core1 == core2)
    return true;

  switch (core1) {
  case ArchSpec::kCore_any:
    return true;

  case ArchSpec::eCore_arm_generic:
    if (enforce_exact_match)
      break;
    LLVM_FALLTHROUGH;
  case ArchSpec::kCore_arm_any:
    if (core2 >= ArchSpec::kCore_arm_first && core2 <= ArchSpec::kCore_arm_last)
      return true;
    if (core2 >= ArchSpec::kCore_thumb_first &&
        core2 <= ArchSpec::kCore_thumb_last)
      return true;
    if (core2 == ArchSpec::kCore_arm_any)
      return true;
    break;

  case ArchSpec::kCore_x86_32_any:
    if ((core2 >= ArchSpec::kCore_x86_32_first &&
         core2 <= ArchSpec::kCore_x86_32_last) ||
        (core2 == ArchSpec::kCore_x86_32_any))
      return true;
    break;

  case ArchSpec::kCore_x86_64_any:
    if ((core2 >= ArchSpec::kCore_x86_64_first &&
         core2 <= ArchSpec::kCore_x86_64_last) ||
        (core2 == ArchSpec::kCore_x86_64_any))
      return true;
    break;

  case ArchSpec::kCore_ppc_any:
    if ((core2 >= ArchSpec::kCore_ppc_first &&
         core2 <= ArchSpec::kCore_ppc_last) ||
        (core2 == ArchSpec::kCore_ppc_any))
      return true;
    break;

  case ArchSpec::kCore_ppc64_any:
    if ((core2 >= ArchSpec::kCore_ppc64_first &&
         core2 <= ArchSpec::kCore_ppc64_last) ||
        (core2 == ArchSpec::kCore_ppc64_any))
      return true;
    break;

  case ArchSpec::eCore_arm_armv6m:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_generic)
        return true;
      try_inverse = false;
      if (core2 == ArchSpec::eCore_arm_armv7)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv6m)
        return true;
    }
    break;

  case ArchSpec::kCore_hexagon_any:
    if ((core2 >= ArchSpec::kCore_hexagon_first &&
         core2 <= ArchSpec::kCore_hexagon_last) ||
        (core2 == ArchSpec::kCore_hexagon_any))
      return true;
    break;

  // v. https://en.wikipedia.org/wiki/ARM_Cortex-M#Silicon_customization
  // Cortex-M0 - ARMv6-M - armv6m Cortex-M3 - ARMv7-M - armv7m Cortex-M4 -
  // ARMv7E-M - armv7em
  case ArchSpec::eCore_arm_armv7em:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_generic)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv7m)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv6m)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv7)
        return true;
      try_inverse = true;
    }
    break;

  // v. https://en.wikipedia.org/wiki/ARM_Cortex-M#Silicon_customization
  // Cortex-M0 - ARMv6-M - armv6m Cortex-M3 - ARMv7-M - armv7m Cortex-M4 -
  // ARMv7E-M - armv7em
  case ArchSpec::eCore_arm_armv7m:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_generic)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv6m)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv7)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv7em)
        return true;
      try_inverse = true;
    }
    break;

  case ArchSpec::eCore_arm_armv7f:
  case ArchSpec::eCore_arm_armv7k:
  case ArchSpec::eCore_arm_armv7s:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_generic)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv7)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_x86_64_x86_64h:
    if (!enforce_exact_match) {
      try_inverse = false;
      if (core2 == ArchSpec::eCore_x86_64_x86_64)
        return true;
    }
    break;

  case ArchSpec::eCore_arm_armv8:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_arm64)
        return true;
      if (core2 == ArchSpec::eCore_arm_aarch64)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_arm_aarch64:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_arm64)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv8)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_arm_arm64:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_arm_aarch64)
        return true;
      if (core2 == ArchSpec::eCore_arm_armv8)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips32:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32_first &&
          core2 <= ArchSpec::kCore_mips32_last)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips32el:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32el_first &&
          core2 <= ArchSpec::kCore_mips32el_last)
        return true;
      try_inverse = true;
    }
    break;

  case ArchSpec::eCore_mips64:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32_first &&
          core2 <= ArchSpec::kCore_mips32_last)
        return true;
      if (core2 >= ArchSpec::kCore_mips64_first &&
          core2 <= ArchSpec::kCore_mips64_last)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips64el:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32el_first &&
          core2 <= ArchSpec::kCore_mips32el_last)
        return true;
      if (core2 >= ArchSpec::kCore_mips64el_first &&
          core2 <= ArchSpec::kCore_mips64el_last)
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips64r2:
  case ArchSpec::eCore_mips64r3:
  case ArchSpec::eCore_mips64r5:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32_first && core2 <= (core1 - 10))
        return true;
      if (core2 >= ArchSpec::kCore_mips64_first && core2 <= (core1 - 1))
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips64r2el:
  case ArchSpec::eCore_mips64r3el:
  case ArchSpec::eCore_mips64r5el:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32el_first && core2 <= (core1 - 10))
        return true;
      if (core2 >= ArchSpec::kCore_mips64el_first && core2 <= (core1 - 1))
        return true;
      try_inverse = false;
    }
    break;

  case ArchSpec::eCore_mips32r2:
  case ArchSpec::eCore_mips32r3:
  case ArchSpec::eCore_mips32r5:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32_first && core2 <= core1)
        return true;
    }
    break;

  case ArchSpec::eCore_mips32r2el:
  case ArchSpec::eCore_mips32r3el:
  case ArchSpec::eCore_mips32r5el:
    if (!enforce_exact_match) {
      if (core2 >= ArchSpec::kCore_mips32el_first && core2 <= core1)
        return true;
    }
    break;

  case ArchSpec::eCore_mips32r6:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_mips32 || core2 == ArchSpec::eCore_mips32r6)
        return true;
    }
    break;

  case ArchSpec::eCore_mips32r6el:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_mips32el ||
          core2 == ArchSpec::eCore_mips32r6el)
        return true;
    }
    break;

  case ArchSpec::eCore_mips64r6:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_mips32 || core2 == ArchSpec::eCore_mips32r6)
        return true;
      if (core2 == ArchSpec::eCore_mips64 || core2 == ArchSpec::eCore_mips64r6)
        return true;
    }
    break;

  case ArchSpec::eCore_mips64r6el:
    if (!enforce_exact_match) {
      if (core2 == ArchSpec::eCore_mips32el ||
          core2 == ArchSpec::eCore_mips32r6el)
        return true;
      if (core2 == ArchSpec::eCore_mips64el ||
          core2 == ArchSpec::eCore_mips64r6el)
        return true;
    }
    break;

  default:
    break;
  }
  if (try_inverse)
    return cores_match(core2, core1, false, enforce_exact_match);
  return false;
}

bool lldb_private::operator<(const ArchSpec &lhs, const ArchSpec &rhs) {
  const ArchSpec::Core lhs_core = lhs.GetCore();
  const ArchSpec::Core rhs_core = rhs.GetCore();
  return lhs_core < rhs_core;
}


bool lldb_private::operator==(const ArchSpec &lhs, const ArchSpec &rhs) {
  return lhs.GetCore() == rhs.GetCore();
}

bool ArchSpec::IsFullySpecifiedTriple() const {
  const auto &user_specified_triple = GetTriple();

  bool user_triple_fully_specified = false;

  if ((user_specified_triple.getOS() != llvm::Triple::UnknownOS) ||
      TripleOSWasSpecified()) {
    if ((user_specified_triple.getVendor() != llvm::Triple::UnknownVendor) ||
        TripleVendorWasSpecified()) {
      const unsigned unspecified = 0;
      if (user_specified_triple.getOSMajorVersion() != unspecified) {
        user_triple_fully_specified = true;
      }
    }
  }

  return user_triple_fully_specified;
}

void ArchSpec::PiecewiseTripleCompare(
    const ArchSpec &other, bool &arch_different, bool &vendor_different,
    bool &os_different, bool &os_version_different, bool &env_different) const {
  const llvm::Triple &me(GetTriple());
  const llvm::Triple &them(other.GetTriple());

  arch_different = (me.getArch() != them.getArch());

  vendor_different = (me.getVendor() != them.getVendor());

  os_different = (me.getOS() != them.getOS());

  os_version_different = (me.getOSMajorVersion() != them.getOSMajorVersion());

  env_different = (me.getEnvironment() != them.getEnvironment());
}

bool ArchSpec::IsAlwaysThumbInstructions() const {
  std::string Status;
  if (GetTriple().getArch() == llvm::Triple::arm ||
      GetTriple().getArch() == llvm::Triple::thumb) {
    // v. https://en.wikipedia.org/wiki/ARM_Cortex-M
    //
    // Cortex-M0 through Cortex-M7 are ARM processor cores which can only
    // execute thumb instructions.  We map the cores to arch names like this:
    //
    // Cortex-M0, Cortex-M0+, Cortex-M1:  armv6m Cortex-M3: armv7m Cortex-M4,
    // Cortex-M7: armv7em

    if (GetCore() == ArchSpec::Core::eCore_arm_armv7m ||
        GetCore() == ArchSpec::Core::eCore_arm_armv7em ||
        GetCore() == ArchSpec::Core::eCore_arm_armv6m ||
        GetCore() == ArchSpec::Core::eCore_thumbv7m ||
        GetCore() == ArchSpec::Core::eCore_thumbv7em ||
        GetCore() == ArchSpec::Core::eCore_thumbv6m) {
      return true;
    }
  }
  return false;
}

void ArchSpec::DumpTriple(Stream &s) const {
  const llvm::Triple &triple = GetTriple();
  llvm::StringRef arch_str = triple.getArchName();
  llvm::StringRef vendor_str = triple.getVendorName();
  llvm::StringRef os_str = triple.getOSName();
  llvm::StringRef environ_str = triple.getEnvironmentName();

  s.Printf("%s-%s-%s", arch_str.empty() ? "*" : arch_str.str().c_str(),
           vendor_str.empty() ? "*" : vendor_str.str().c_str(),
           os_str.empty() ? "*" : os_str.str().c_str());

  if (!environ_str.empty())
    s.Printf("-%s", environ_str.str().c_str());
}
