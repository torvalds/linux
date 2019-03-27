//===-- SafeMachO.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef liblldb_SafeMachO_h_
#define liblldb_SafeMachO_h_

// This header file is required to work around collisions between the defines
// in mach/machine.h, and enum members of the same name in llvm's MachO.h.  If
// you want to use llvm/Support/MachO.h, use this file instead. The caveats
// are: 1) You can only use the MachO.h enums, you can't use the defines.  That
// won't make a difference since the values
//    are the same.
// 2) If you need any header file that relies on mach/machine.h, you must
// include that first. 3) This isn't a total solution, it doesn't undef every
// define that MachO.h has borrowed from various system headers,
//    only the ones that come from mach/machine.h because that is the one we
//    ended up pulling in from various places.
//

#undef CPU_ARCH_MASK
#undef CPU_ARCH_ABI64

#undef CPU_TYPE_ANY
#undef CPU_TYPE_X86
#undef CPU_TYPE_I386
#undef CPU_TYPE_X86_64
#undef CPU_TYPE_MC98000
#undef CPU_TYPE_ARM
#undef CPU_TYPE_ARM64
#undef CPU_TYPE_SPARC
#undef CPU_TYPE_POWERPC
#undef CPU_TYPE_POWERPC64

#undef CPU_SUB_TYPE_MASK
#undef CPU_SUB_TYPE_LIB64

#undef CPU_SUBTYPE_MULTIPLE

#undef CPU_SUBTYPE_I386_ALL
#undef CPU_SUBTYPE_386
#undef CPU_SUBTYPE_486
#undef CPU_SUBTYPE_486SX
#undef CPU_SUBTYPE_586
#undef CPU_SUBTYPE_PENT
#undef CPU_SUBTYPE_PENTPRO
#undef CPU_SUBTYPE_PENTII_M3
#undef CPU_SUBTYPE_PENTII_M5
#undef CPU_SUBTYPE_CELERON
#undef CPU_SUBTYPE_CELERON_MOBILE
#undef CPU_SUBTYPE_PENTIUM_3
#undef CPU_SUBTYPE_PENTIUM_3_M
#undef CPU_SUBTYPE_PENTIUM_3_XEON
#undef CPU_SUBTYPE_PENTIUM_M
#undef CPU_SUBTYPE_PENTIUM_4
#undef CPU_SUBTYPE_PENTIUM_4_M
#undef CPU_SUBTYPE_ITANIUM
#undef CPU_SUBTYPE_ITANIUM_2
#undef CPU_SUBTYPE_XEON
#undef CPU_SUBTYPE_XEON_MP

#undef CPU_SUBTYPE_X86_ALL
#undef CPU_SUBTYPE_X86_64_ALL
#undef CPU_SUBTYPE_X86_ARCH1
#undef CPU_SUBTYPE_X86_64_H

#undef CPU_SUBTYPE_INTEL
#undef CPU_SUBTYPE_INTEL_FAMILY
#undef CPU_SUBTYPE_INTEL_FAMILY_MAX
#undef CPU_SUBTYPE_INTEL_MODEL
#undef CPU_SUBTYPE_INTEL_MODEL_ALL

#undef CPU_SUBTYPE_ARM
#undef CPU_SUBTYPE_ARM_ALL
#undef CPU_SUBTYPE_ARM_V4T
#undef CPU_SUBTYPE_ARM_V6
#undef CPU_SUBTYPE_ARM_V5
#undef CPU_SUBTYPE_ARM_V5TEJ
#undef CPU_SUBTYPE_ARM_XSCALE
#undef CPU_SUBTYPE_ARM_V7

#undef CPU_SUBTYPE_ARM_V7S
#undef CPU_SUBTYPE_ARM_V7K
#undef CPU_SUBTYPE_ARM_V6M
#undef CPU_SUBTYPE_ARM_V7M
#undef CPU_SUBTYPE_ARM_V7EM

#undef CPU_SUBTYPE_ARM64_ALL

#undef CPU_SUBTYPE_SPARC_ALL

#undef CPU_SUBTYPE_POWERPC
#undef CPU_SUBTYPE_POWERPC_ALL
#undef CPU_SUBTYPE_POWERPC_601
#undef CPU_SUBTYPE_POWERPC_602
#undef CPU_SUBTYPE_POWERPC_603
#undef CPU_SUBTYPE_POWERPC_603e
#undef CPU_SUBTYPE_POWERPC_603ev
#undef CPU_SUBTYPE_POWERPC_604
#undef CPU_SUBTYPE_POWERPC_604e
#undef CPU_SUBTYPE_POWERPC_620
#undef CPU_SUBTYPE_POWERPC_750
#undef CPU_SUBTYPE_POWERPC_7400
#undef CPU_SUBTYPE_POWERPC_7450
#undef CPU_SUBTYPE_POWERPC_970

#undef CPU_SUBTYPE_MC980000_ALL
#undef CPU_SUBTYPE_MC98601

#include "llvm/BinaryFormat/MachO.h"

#endif // liblldb_SafeMachO_h_
