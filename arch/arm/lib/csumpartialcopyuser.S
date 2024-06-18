/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/csumpartialcopyuser.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * 27/03/03 Ian Molton Clean up CONFIG_CPU
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>

		.text

#if defined(CONFIG_CPU_SW_DOMAIN_PAN)

		.macro	save_regs
		mrc	p15, 0, ip, c3, c0, 0
		stmfd	sp!, {r1, r2, r4 - r8, ip, lr}
		uaccess_enable ip
		.endm

		.macro	load_regs
		ldmfd	sp!, {r1, r2, r4 - r8, ip, lr}
		mcr	p15, 0, ip, c3, c0, 0
		ret	lr
		.endm

#elif defined(CONFIG_CPU_TTBR0_PAN)

		.macro	save_regs
		mrc	p15, 0, ip, c2, c0, 2		@ read TTBCR
		stmfd	sp!, {r1, r2, r4 - r8, ip, lr}
		uaccess_enable ip
		.endm

		.macro	load_regs
		ldmfd	sp!, {r1, r2, r4 - r8, ip, lr}
		mcr	p15, 0, ip, c2, c0, 2		@ restore TTBCR
		ret	lr
		.endm

#else

		.macro	save_regs
		stmfd	sp!, {r1, r2, r4 - r8, lr}
		.endm

		.macro	load_regs
		ldmfd	sp!, {r1, r2, r4 - r8, pc}
		.endm

#endif

		.macro	load1b,	reg1
		ldrusr	\reg1, r0, 1
		.endm

		.macro	load2b, reg1, reg2
		ldrusr	\reg1, r0, 1
		ldrusr	\reg2, r0, 1
		.endm

		.macro	load1l, reg1
		ldrusr	\reg1, r0, 4
		.endm

		.macro	load2l, reg1, reg2
		ldrusr	\reg1, r0, 4
		ldrusr	\reg2, r0, 4
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
		ldrusr	\reg1, r0, 4
		ldrusr	\reg2, r0, 4
		ldrusr	\reg3, r0, 4
		ldrusr	\reg4, r0, 4
		.endm

/*
 * unsigned int
 * csum_partial_copy_from_user(const char *src, char *dst, int len)
 *  r0 = src, r1 = dst, r2 = len
 *  Returns : r0 = checksum or 0
 */

#define FN_ENTRY	ENTRY(csum_partial_copy_from_user)
#define FN_EXIT		ENDPROC(csum_partial_copy_from_user)

#include "csumpartialcopygeneric.S"

/*
 * We report fault by returning 0 csum - impossible in normal case, since
 * we start with 0xffffffff for initial sum.
 */
		.pushsection .text.fixup,"ax"
		.align	4
9001:		mov	r0, #0
		load_regs
		.popsection
