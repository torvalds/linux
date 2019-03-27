/*===-- sync_fetch_and_or_8.S - -------------------------------------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __sync_fetch_and_or_8 function for the ARM
 * architecture.
 *
 *===----------------------------------------------------------------------===*/

#include "sync-ops.h"

#if __ARM_ARCH_PROFILE != 'M'
#define or_8(rD_LO, rD_HI, rN_LO, rN_HI, rM_LO, rM_HI) \
    orr rD_LO, rN_LO, rM_LO ; \
    orr rD_HI, rN_HI, rM_HI

SYNC_OP_8(or_8)
#endif

NO_EXEC_STACK_DIRECTIVE

