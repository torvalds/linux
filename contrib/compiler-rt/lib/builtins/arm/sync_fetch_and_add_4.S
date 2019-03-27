/*===-- sync_fetch_and_add_4.S - ------------------------------------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __sync_fetch_and_add_4 function for the ARM
 * architecture.
 *
 *===----------------------------------------------------------------------===*/

#include "sync-ops.h"

/* "adds" is 2 bytes shorter than "add". */
#define add_4(rD, rN, rM)  add rD, rN, rM

SYNC_OP_4(add_4)

NO_EXEC_STACK_DIRECTIVE

