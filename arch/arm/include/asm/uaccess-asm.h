/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_UACCESS_ASM_H__
#define __ASM_UACCESS_ASM_H__

#include <asm/asm-offsets.h>
#include <asm/domain.h>
#include <asm/memory.h>
#include <asm/thread_info.h>

	.macro	csdb
#ifdef CONFIG_THUMB2_KERNEL
	.inst.w	0xf3af8014
#else
	.inst	0xe320f014
#endif
	.endm

	.macro check_uaccess, addr:req, size:req, limit:req, tmp:req, bad:req
#ifndef CONFIG_CPU_USE_DOMAINS
	adds	\tmp, \addr, #\size - 1
	sbcscc	\tmp, \tmp, \limit
	bcs	\bad
#ifdef CONFIG_CPU_SPECTRE
	movcs	\addr, #0
	csdb
#endif
#endif
	.endm

	.macro uaccess_mask_range_ptr, addr:req, size:req, limit:req, tmp:req
#ifdef CONFIG_CPU_SPECTRE
	sub	\tmp, \limit, #1
	subs	\tmp, \tmp, \addr	@ tmp = limit - 1 - addr
	addhs	\tmp, \tmp, #1		@ if (tmp >= 0) {
	subshs	\tmp, \tmp, \size	@ tmp = limit - (addr + size) }
	movlo	\addr, #0		@ if (tmp < 0) addr = NULL
	csdb
#endif
	.endm

	.macro	uaccess_disable, tmp, isb=1
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	/*
	 * Whenever we re-enter userspace, the domains should always be
	 * set appropriately.
	 */
	mov	\tmp, #DACR_UACCESS_DISABLE
	mcr	p15, 0, \tmp, c3, c0, 0		@ Set domain register
	.if	\isb
	instr_sync
	.endif
#endif
	.endm

	.macro	uaccess_enable, tmp, isb=1
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	/*
	 * Whenever we re-enter userspace, the domains should always be
	 * set appropriately.
	 */
	mov	\tmp, #DACR_UACCESS_ENABLE
	mcr	p15, 0, \tmp, c3, c0, 0
	.if	\isb
	instr_sync
	.endif
#endif
	.endm

	.macro	uaccess_save, tmp
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	mrc	p15, 0, \tmp, c3, c0, 0
	str	\tmp, [sp, #SVC_DACR]
#endif
	.endm

	.macro	uaccess_restore
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	ldr	r0, [sp, #SVC_DACR]
	mcr	p15, 0, r0, c3, c0, 0
#endif
	.endm

	/*
	 * Save the address limit on entry to a privileged exception and
	 * if using PAN, save and disable usermode access.
	 */
	.macro	uaccess_entry, tsk, tmp0, tmp1, tmp2, disable
	ldr	\tmp0, [\tsk, #TI_ADDR_LIMIT]
	mov	\tmp1, #TASK_SIZE
	str	\tmp1, [\tsk, #TI_ADDR_LIMIT]
	str	\tmp0, [sp, #SVC_ADDR_LIMIT]
	uaccess_save \tmp0
	.if \disable
	uaccess_disable \tmp0
	.endif
	.endm

	/* Restore the user access state previously saved by uaccess_entry */
	.macro	uaccess_exit, tsk, tmp0, tmp1
	ldr	\tmp1, [sp, #SVC_ADDR_LIMIT]
	uaccess_restore
	str	\tmp1, [\tsk, #TI_ADDR_LIMIT]
	.endm

#endif /* __ASM_UACCESS_ASM_H__ */
