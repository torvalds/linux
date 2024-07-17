/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_UACCESS_ASM_H__
#define __ASM_UACCESS_ASM_H__

#include <asm/asm-offsets.h>
#include <asm/domain.h>
#include <asm/page.h>
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

#if defined(CONFIG_CPU_SW_DOMAIN_PAN)

	.macro	uaccess_disable, tmp, isb=1
	/*
	 * Whenever we re-enter userspace, the domains should always be
	 * set appropriately.
	 */
	mov	\tmp, #DACR_UACCESS_DISABLE
	mcr	p15, 0, \tmp, c3, c0, 0		@ Set domain register
	.if	\isb
	instr_sync
	.endif
	.endm

	.macro	uaccess_enable, tmp, isb=1
	/*
	 * Whenever we re-enter userspace, the domains should always be
	 * set appropriately.
	 */
	mov	\tmp, #DACR_UACCESS_ENABLE
	mcr	p15, 0, \tmp, c3, c0, 0
	.if	\isb
	instr_sync
	.endif
	.endm

#elif defined(CONFIG_CPU_TTBR0_PAN)

	.macro	uaccess_disable, tmp, isb=1
	/*
	 * Disable TTBR0 page table walks (EDP0 = 1), use the reserved ASID
	 * from TTBR1 (A1 = 1) and enable TTBR1 page table walks for kernel
	 * addresses by reducing TTBR0 range to 32MB (T0SZ = 7).
	 */
	mrc	p15, 0, \tmp, c2, c0, 2		@ read TTBCR
	orr	\tmp, \tmp, #TTBCR_EPD0 | TTBCR_T0SZ_MASK
	orr	\tmp, \tmp, #TTBCR_A1
	mcr	p15, 0, \tmp, c2, c0, 2		@ write TTBCR
	.if	\isb
	instr_sync
	.endif
	.endm

	.macro	uaccess_enable, tmp, isb=1
	/*
	 * Enable TTBR0 page table walks (T0SZ = 0, EDP0 = 0) and ASID from
	 * TTBR0 (A1 = 0).
	 */
	mrc	p15, 0, \tmp, c2, c0, 2		@ read TTBCR
	bic	\tmp, \tmp, #TTBCR_EPD0 | TTBCR_T0SZ_MASK
	bic	\tmp, \tmp, #TTBCR_A1
	mcr	p15, 0, \tmp, c2, c0, 2		@ write TTBCR
	.if	\isb
	instr_sync
	.endif
	.endm

#else

	.macro	uaccess_disable, tmp, isb=1
	.endm

	.macro	uaccess_enable, tmp, isb=1
	.endm

#endif

#if defined(CONFIG_CPU_SW_DOMAIN_PAN) || defined(CONFIG_CPU_USE_DOMAINS)
#define DACR(x...)	x
#else
#define DACR(x...)
#endif

#ifdef CONFIG_CPU_TTBR0_PAN
#define PAN(x...)	x
#else
#define PAN(x...)
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
 DACR(	mrc	p15, 0, \tmp0, c3, c0, 0)
 DACR(	str	\tmp0, [sp, #SVC_DACR])
 PAN(	mrc	p15, 0, \tmp0, c2, c0, 2)
 PAN(	str	\tmp0, [sp, #SVC_TTBCR])
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
 DACR(	ldr	\tmp0, [sp, #SVC_DACR])
 DACR(	mcr	p15, 0, \tmp0, c3, c0, 0)
 PAN(	ldr	\tmp0, [sp, #SVC_TTBCR])
 PAN(	mcr	p15, 0, \tmp0, c2, c0, 2)
	.endm

#undef DACR
#undef PAN

#endif /* __ASM_UACCESS_ASM_H__ */
