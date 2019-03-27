/*===-- sync_fetch_and_or_4.S - -------------------------------------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __sync_fetch_and_or_4 function for the ARM
 * architecture.
 *
 *===----------------------------------------------------------------------===*/

#include "sync-ops.h"

#define or_4(rD, rN, rM)  orr rD, rN, rM

SYNC_OP_4(or_4)

NO_EXEC_STACK_DIRECTIVE

