/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/vdso/vdso.h>

#include <asm/isa-rev.h>

#include <linux/elfnote.h>
#include <linux/version.h>

ELFNOTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFNOTE_END

/*
 * The .MIPS.abiflags section must be defined with the FP ABI flags set
 * to 'any' to be able to link with both old and new libraries.
 * Newer toolchains are capable of automatically generating this, but we want
 * to work with older toolchains as well. Therefore, we define the contents of
 * this section here (under different names), and then genvdso will patch
 * it to have the correct name and type.
 *
 * We base the .MIPS.abiflags section on preprocessor definitions rather than
 * CONFIG_* because we need to match the particular ABI we are building the
 * VDSO for.
 *
 * See https://dmz-portal.mips.com/wiki/MIPS_O32_ABI_-_FR0_and_FR1_Interlinking
 * for the .MIPS.abiflags section description.
 */

	.section .mips_abiflags, "a"
	.align 3
__mips_abiflags:
	.hword	0		/* version */
	.byte	__mips		/* isa_level */

	/* isa_rev */
	.byte	MIPS_ISA_REV

	/* gpr_size */
#ifdef __mips64
	.byte	2		/* AFL_REG_64 */
#else
	.byte	1		/* AFL_REG_32 */
#endif

	/* cpr1_size */
#if (MIPS_ISA_REV >= 6) || defined(__mips64)
	.byte	2		/* AFL_REG_64 */
#else
	.byte	1		/* AFL_REG_32 */
#endif

	.byte	0		/* cpr2_size (AFL_REG_NONE) */
	.byte	0		/* fp_abi (Val_GNU_MIPS_ABI_FP_ANY) */
	.word	0		/* isa_ext */
	.word	0		/* ases */
	.word	0		/* flags1 */
	.word	0		/* flags2 */
