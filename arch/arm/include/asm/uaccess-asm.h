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

#if defined(CONFIG_CPU_SW_DOMAIN_PAN) || defined(CONFIG_CPU_USE_DOMAINS)
#define DACR(x...)	x
#else
#define DACR(x...)
#endif

	/*
	 * Save the address limit on entry to a privileged exception.
	 *
	 * If we are using the DACR for kernel access by the user accessors
	 * (CONFIG_CPU_USE_DOMAINS=y), always reset the DACR kernel domain
	 * back to client mode, whether or not \disable is set.
	 *
	 * If we are using SW PAN, set the DACR user domain to no access
	 * if \disable is set.
	 */
	.macro	uaccess_entry, tsk, tmp0, tmp1, tmp2, disable
	ldr	\tmp1, [\tsk, #TI_ADDR_LIMIT]
	ldr	\tmp2, =TASK_SIZE
	str	\tmp2, [\tsk, #TI_ADDR_LIMIT]
 DACR(	mrc	p15, 0, \tmp0, c3, c0, 0)
 DACR(	str	\tmp0, [sp, #SVC_DACR])
	str	\tmp1, [sp, #SVC_ADDR_LIMIT]
	.if \disable && IS_ENABLED(CONFIG_CPU_SW_DOMAIN_PAN)
	/* kernel=client, user=no access */
	mov	\tmp2, #DACR_UACCESS_DISABLE
	mcr	p15, 0, \tmp2, c3, c0, 0
	instr_sync
	.elseif IS_ENABLED(CONFIG_CPU_USE_DOMAINS)
	/* kernel=client */
	bic	\tmp2, \tmp0, #domain_mask(DOMAIN_KERNEL)
	orr	\tmp2, \tmp2, #domain_val(DOMAIN_KERNEL, DOMAIN_CLIENT)
	mcr	p15, 0, \tmp2, c3, c0, 0
	instr_sync
	.endif
	.endm

	/* Restore the user access state previously saved by uaccess_entry */
	.macro	uaccess_exit, tsk, tmp0, tmp1
	ldr	\tmp1, [sp, #SVC_ADDR_LIMIT]
 DACR(	ldr	\tmp0, [sp, #SVC_DACR])
	str	\tmp1, [\tsk, #TI_ADDR_LIMIT]
 DACR(	mcr	p15, 0, \tmp0, c3, c0, 0)
	.endm

#undef DACR

#endif /* __ASM_UACCESS_ASM_H__ */
