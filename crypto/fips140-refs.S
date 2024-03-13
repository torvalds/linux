/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 *
 * This file contains the variable definitions that will be used by the FIPS140
 * s/w module to access the RELA sections in the ELF image. These are used to
 * apply the relocations applied by the module loader in reverse, so that we
 * can reconstruct the image that was used to derive the HMAC used by the
 * integrity check.
 *
 * The first .long of each entry will be populated by the module loader based
 * on the actual placement of the respective RELA section in memory. The second
 * .long carries the RELA entry count, and is populated by the host tool that
 * also generates the HMAC of the contents of .text and .rodata.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.section	".init.rodata", "a"

	.align	2
	.globl	fips140_rela_text
fips140_rela_text:
	.weak	__sec_rela_text
	.long	__sec_rela_text - .
	.long	0

	.globl	fips140_rela_rodata
fips140_rela_rodata:
	.weak	__sec_rela_rodata
	.long	__sec_rela_rodata - .
	.long	0
