/*===-- sync_fetch_and_nand_4.S - -----------------------------------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __sync_fetch_and_nand_4 function for the ARM
 * architecture.
 *
 *===----------------------------------------------------------------------===*/

#include "sync-ops.h"

#define nand_4(rD, rN, rM)  bic rD, rN, rM

SYNC_OP_4(nand_4)

NO_EXEC_STACK_DIRECTIVE

