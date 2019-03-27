/*===-- sync_fetch_and_sub_4.S - ------------------------------------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __sync_fetch_and_sub_4 function for the ARM
 * architecture.
 *
 *===----------------------------------------------------------------------===*/

#include "sync-ops.h"

/* "subs" is 2 bytes shorter than "sub". */
#define sub_4(rD, rN, rM)  sub rD, rN, rM

SYNC_OP_4(sub_4)

NO_EXEC_STACK_DIRECTIVE

