/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ASM_UACCESS_H
#define __ASM_ASM_UACCESS_H

#include <asm/alternative-macros.h>
#include <asm/asm-extable.h>
#include <asm/assembler.h>
#include <asm/kernel-pgtable.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

/*
 * User access enabling/disabling macros.
 */
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	.macro	__uaccess_ttbr0_disable, tmp1
	mrs	\tmp1, ttbr1_el1			// swapper_pg_dir
	bic	\tmp1, \tmp1, #TTBR_ASID_MASK
	sub	\tmp1, \tmp1, #RESERVED_SWAPPER_OFFSET	// reserved_pg_dir
	msr	ttbr0_el1, \tmp1			// set reserved TTBR0_EL1
	add	\tmp1, \tmp1, #RESERVED_SWAPPER_OFFSET
	msr	ttbr1_el1, \tmp1		// set reserved ASID
	isb
	.endm

	.macro	__uaccess_ttbr0_enable, tmp1, tmp2
	get_current_task \tmp1
	ldr	\tmp1, [\tmp1, #TSK_TI_TTBR0]	// load saved TTBR0_EL1
	mrs	\tmp2, ttbr1_el1
	extr    \tmp2, \tmp2, \tmp1, #48
	ror     \tmp2, \tmp2, #16
	msr	ttbr1_el1, \tmp2		// set the active ASID
	msr	ttbr0_el1, \tmp1		// set the non-PAN TTBR0_EL1
	isb
	.endm

	.macro	uaccess_ttbr0_disable, tmp1, tmp2
alternative_if_not ARM64_HAS_PAN
	save_and_disable_irq \tmp2		// avoid preemption
	__uaccess_ttbr0_disable \tmp1
	restore_irq \tmp2
alternative_else_nop_endif
	.endm

	.macro	uaccess_ttbr0_enable, tmp1, tmp2, tmp3
alternative_if_not ARM64_HAS_PAN
	save_and_disable_irq \tmp3		// avoid preemption
	__uaccess_ttbr0_enable \tmp1, \tmp2
	restore_irq \tmp3
alternative_else_nop_endif
	.endm
#else
	.macro	uaccess_ttbr0_disable, tmp1, tmp2
	.endm

	.macro	uaccess_ttbr0_enable, tmp1, tmp2, tmp3
	.endm
#endif

#define USER(l, x...)				\
9999:	x;					\
	_asm_extable_uaccess	9999b, l

#define USER_CPY(l, uaccess_is_write, x...)	\
9999:	x;					\
	_asm_extable_uaccess_cpy 9999b, l, uaccess_is_write

/*
 * Generate the assembly for LDTR/STTR with exception table entries.
 * This is complicated as there is no post-increment or pair versions of the
 * unprivileged instructions, and USER() only works for single instructions.
 */
	.macro user_ldp l, reg1, reg2, addr, post_inc
8888:		ldtr	\reg1, [\addr];
8889:		ldtr	\reg2, [\addr, #8];
		add	\addr, \addr, \post_inc;

		_asm_extable_uaccess	8888b, \l;
		_asm_extable_uaccess	8889b, \l;
	.endm

	.macro user_stp l, reg1, reg2, addr, post_inc
8888:		sttr	\reg1, [\addr];
8889:		sttr	\reg2, [\addr, #8];
		add	\addr, \addr, \post_inc;

		_asm_extable_uaccess	8888b,\l;
		_asm_extable_uaccess	8889b,\l;
	.endm

	.macro user_ldst l, inst, reg, addr, post_inc
8888:		\inst		\reg, [\addr];
		add		\addr, \addr, \post_inc;

		_asm_extable_uaccess	8888b, \l;
	.endm
#endif
