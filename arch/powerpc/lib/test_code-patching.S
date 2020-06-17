/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 IBM Corporation
 */
#include <asm/ppc-opcode.h>

	.text

#define globl(x)		\
	.globl x;	\
x:

globl(code_patching_test1)
	nop
	nop
globl(end_code_patching_test1)

globl(code_patching_test1_expected)
	.long OP_PREFIX << 26
	.long 0x0000000
