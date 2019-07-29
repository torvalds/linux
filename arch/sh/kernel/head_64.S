/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/head_64.S
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 */

#include <linux/init.h>

#include <asm/page.h>
#include <asm/cache.h>
#include <asm/tlb.h>
#include <cpu/registers.h>
#include <cpu/mmu_context.h>
#include <asm/thread_info.h>

/*
 * MMU defines: TLB boundaries.
 */

#define MMUIR_FIRST	ITLB_FIXED
#define MMUIR_END	ITLB_LAST_VAR_UNRESTRICTED+TLB_STEP
#define MMUIR_STEP	TLB_STEP

#define MMUDR_FIRST	DTLB_FIXED
#define MMUDR_END	DTLB_LAST_VAR_UNRESTRICTED+TLB_STEP
#define MMUDR_STEP	TLB_STEP

/* Safety check : CONFIG_PAGE_OFFSET has to be a multiple of 512Mb */
#if (CONFIG_PAGE_OFFSET & ((1UL<<29)-1))
#error "CONFIG_PAGE_OFFSET must be a multiple of 512Mb"
#endif

/*
 * MMU defines: Fixed TLBs.
 */
/* Deal safely with the case where the base of RAM is not 512Mb aligned */

#define ALIGN_512M_MASK (0xffffffffe0000000)
#define ALIGNED_EFFECTIVE ((CONFIG_PAGE_OFFSET + CONFIG_MEMORY_START) & ALIGN_512M_MASK)
#define ALIGNED_PHYSICAL (CONFIG_MEMORY_START & ALIGN_512M_MASK)

#define MMUIR_TEXT_H	(0x0000000000000003 | ALIGNED_EFFECTIVE)
			/* Enabled, Shared, ASID 0, Eff. Add. 0xA0000000 */

#define MMUIR_TEXT_L	(0x000000000000009a | ALIGNED_PHYSICAL)
			/* 512 Mb, Cacheable, Write-back, execute, Not User, Ph. Add. */

#define MMUDR_CACHED_H	0x0000000000000003 | ALIGNED_EFFECTIVE
			/* Enabled, Shared, ASID 0, Eff. Add. 0xA0000000 */
#define MMUDR_CACHED_L	0x000000000000015a | ALIGNED_PHYSICAL
			/* 512 Mb, Cacheable, Write-back, read/write, Not User, Ph. Add. */

#ifdef CONFIG_CACHE_OFF
#define	ICCR0_INIT_VAL	ICCR0_OFF			/* ICACHE off */
#else
#define	ICCR0_INIT_VAL	ICCR0_ON | ICCR0_ICI		/* ICE + ICI */
#endif
#define	ICCR1_INIT_VAL	ICCR1_NOLOCK			/* No locking */

#if defined (CONFIG_CACHE_OFF)
#define	OCCR0_INIT_VAL	OCCR0_OFF			   /* D-cache: off  */
#elif defined (CONFIG_CACHE_WRITETHROUGH)
#define	OCCR0_INIT_VAL	OCCR0_ON | OCCR0_OCI | OCCR0_WT	   /* D-cache: on,   */
							   /* WT, invalidate */
#elif defined (CONFIG_CACHE_WRITEBACK)
#define	OCCR0_INIT_VAL	OCCR0_ON | OCCR0_OCI | OCCR0_WB	   /* D-cache: on,   */
							   /* WB, invalidate */
#else
#error preprocessor flag CONFIG_CACHE_... not recognized!
#endif

#define	OCCR1_INIT_VAL	OCCR1_NOLOCK			   /* No locking     */

	.section	.empty_zero_page, "aw"
	.global empty_zero_page

empty_zero_page:
	.long	1		/* MOUNT_ROOT_RDONLY */
	.long	0		/* RAMDISK_FLAGS */
	.long	0x0200		/* ORIG_ROOT_DEV */
	.long	1		/* LOADER_TYPE */
	.long	0x00800000	/* INITRD_START */
	.long	0x00800000	/* INITRD_SIZE */
	.long	0

	.text
	.balign 4096,0,4096

	.section	.data, "aw"
	.balign	PAGE_SIZE

	.section	.data, "aw"
	.balign	PAGE_SIZE

	.global mmu_pdtp_cache
mmu_pdtp_cache:
	.space PAGE_SIZE, 0

	.global	fpu_in_use
fpu_in_use:	.quad	0


	__HEAD
	.balign L1_CACHE_BYTES
/*
 * Condition at the entry of __stext:
 * . Reset state:
 *   . SR.FD    = 1		(FPU disabled)
 *   . SR.BL    = 1		(Exceptions disabled)
 *   . SR.MD    = 1		(Privileged Mode)
 *   . SR.MMU   = 0		(MMU Disabled)
 *   . SR.CD    = 0		(CTC User Visible)
 *   . SR.IMASK = Undefined	(Interrupt Mask)
 *
 * Operations supposed to be performed by __stext:
 * . prevent speculative fetch onto device memory while MMU is off
 * . reflect as much as possible SH5 ABI (r15, r26, r27, r18)
 * . first, save CPU state and set it to something harmless
 * . any CPU detection and/or endianness settings (?)
 * . initialize EMI/LMI (but not TMU/RTC/INTC/SCIF): TBD
 * . set initial TLB entries for cached and uncached regions
 *   (no fine granularity paging)
 * . set initial cache state
 * . enable MMU and caches
 * . set CPU to a consistent state
 *   . registers (including stack pointer and current/KCR0)
 *   . NOT expecting to set Exception handling nor VBR/RESVEC/DCR
 *     at this stage. This is all to later Linux initialization steps.
 *   . initialize FPU
 * . clear BSS
 * . jump into start_kernel()
 * . be prepared to hopeless start_kernel() returns.
 *
 */
	.global _stext
_stext:
	/*
	 * Prevent speculative fetch on device memory due to
	 * uninitialized target registers.
	 */
	ptabs/u	ZERO, tr0
	ptabs/u	ZERO, tr1
	ptabs/u	ZERO, tr2
	ptabs/u	ZERO, tr3
	ptabs/u	ZERO, tr4
	ptabs/u	ZERO, tr5
	ptabs/u	ZERO, tr6
	ptabs/u	ZERO, tr7
	synci

	/*
	 * Read/Set CPU state. After this block:
	 * r29 = Initial SR
	 */
	getcon	SR, r29
	movi	SR_HARMLESS, r20
	putcon	r20, SR

	/*
	 * Initialize EMI/LMI. To Be Done.
	 */

	/*
	 * CPU detection and/or endianness settings (?). To Be Done.
	 * Pure PIC code here, please ! Just save state into r30.
         * After this block:
	 * r30 = CPU type/Platform Endianness
	 */

	/*
	 * Set initial TLB entries for cached and uncached regions.
	 * Note: PTA/BLINK is PIC code, PTABS/BLINK isn't !
	 */
	/* Clear ITLBs */
	pta	clear_ITLB, tr1
	movi	MMUIR_FIRST, r21
	movi	MMUIR_END, r22
clear_ITLB:
	putcfg	r21, 0, ZERO		/* Clear MMUIR[n].PTEH.V */
	addi	r21, MMUIR_STEP, r21
        bne	r21, r22, tr1

	/* Clear DTLBs */
	pta	clear_DTLB, tr1
	movi	MMUDR_FIRST, r21
	movi	MMUDR_END, r22
clear_DTLB:
	putcfg	r21, 0, ZERO		/* Clear MMUDR[n].PTEH.V */
	addi	r21, MMUDR_STEP, r21
        bne	r21, r22, tr1

	/* Map one big (512Mb) page for ITLB */
	movi	MMUIR_FIRST, r21
	movi	MMUIR_TEXT_L, r22	/* PTEL first */
	add.l	r22, r63, r22		/* Sign extend */
	putcfg	r21, 1, r22		/* Set MMUIR[0].PTEL */
	movi	MMUIR_TEXT_H, r22	/* PTEH last */
	add.l	r22, r63, r22		/* Sign extend */
	putcfg	r21, 0, r22		/* Set MMUIR[0].PTEH */

	/* Map one big CACHED (512Mb) page for DTLB */
	movi	MMUDR_FIRST, r21
	movi	MMUDR_CACHED_L, r22	/* PTEL first */
	add.l	r22, r63, r22		/* Sign extend */
	putcfg	r21, 1, r22		/* Set MMUDR[0].PTEL */
	movi	MMUDR_CACHED_H, r22	/* PTEH last */
	add.l	r22, r63, r22		/* Sign extend */
	putcfg	r21, 0, r22		/* Set MMUDR[0].PTEH */

	/*
	 * Setup a DTLB translation for SCIF phys.
	 */
	addi    r21, MMUDR_STEP, r21
	movi    0x0a03, r22	/* SCIF phys */
	shori   0x0148, r22
	putcfg  r21, 1, r22	/* PTEL first */
	movi    0xfa03, r22	/* 0xfa030000, fixed SCIF virt */
	shori   0x0003, r22
	putcfg  r21, 0, r22	/* PTEH last */

	/*
	 * Set cache behaviours.
	 */
	/* ICache */
	movi	ICCR_BASE, r21
	movi	ICCR0_INIT_VAL, r22
	movi	ICCR1_INIT_VAL, r23
	putcfg	r21, ICCR_REG0, r22
	putcfg	r21, ICCR_REG1, r23

	/* OCache */
	movi	OCCR_BASE, r21
	movi	OCCR0_INIT_VAL, r22
	movi	OCCR1_INIT_VAL, r23
	putcfg	r21, OCCR_REG0, r22
	putcfg	r21, OCCR_REG1, r23


	/*
	 * Enable Caches and MMU. Do the first non-PIC jump.
         * Now head.S global variables, constants and externs
	 * can be used.
	 */
	getcon	SR, r21
	movi	SR_ENABLE_MMU, r22
	or	r21, r22, r21
	putcon	r21, SSR
	movi	hyperspace, r22
	ori	r22, 1, r22	    /* Make it SHmedia, not required but..*/
	putcon	r22, SPC
	synco
	rte			    /* And now go into the hyperspace ... */
hyperspace:			    /* ... that's the next instruction !  */

	/*
	 * Set CPU to a consistent state.
	 * r31 = FPU support flag
	 * tr0/tr7 in use. Others give a chance to loop somewhere safe
	 */
	movi	start_kernel, r32
	ori	r32, 1, r32

	ptabs	r32, tr0		    /* r32 = _start_kernel address        */
	pta/u	hopeless, tr1
	pta/u	hopeless, tr2
	pta/u	hopeless, tr3
	pta/u	hopeless, tr4
	pta/u	hopeless, tr5
	pta/u	hopeless, tr6
	pta/u	hopeless, tr7
	gettr	tr1, r28			/* r28 = hopeless address */

	/* Set initial stack pointer */
	movi	init_thread_union, SP
	putcon	SP, KCR0		/* Set current to init_task */
	movi	THREAD_SIZE, r22	/* Point to the end */
	add	SP, r22, SP

	/*
	 * Initialize FPU.
	 * Keep FPU flag in r31. After this block:
	 * r31 = FPU flag
	 */
	movi fpu_in_use, r31	/* Temporary */

#ifdef CONFIG_SH_FPU
	getcon	SR, r21
	movi	SR_ENABLE_FPU, r22
	and	r21, r22, r22
	putcon	r22, SR			/* Try to enable */
	getcon	SR, r22
	xor	r21, r22, r21
	shlri	r21, 15, r21		/* Supposedly 0/1 */
	st.q	r31, 0 , r21		/* Set fpu_in_use */
#else
	movi	0, r21
	st.q	r31, 0 , r21		/* Set fpu_in_use */
#endif
	or	r21, ZERO, r31		/* Set FPU flag at last */

#ifndef CONFIG_SH_NO_BSS_INIT
/* Don't clear BSS if running on slow platforms such as an RTL simulation,
   remote memory via SHdebug link, etc.  For these the memory can be guaranteed
   to be all zero on boot anyway. */
	/*
	 * Clear bss
	 */
	pta	clear_quad, tr1
	movi	__bss_start, r22
	movi	_end, r23
clear_quad:
	st.q	r22, 0, ZERO
	addi	r22, 8, r22
	bne	r22, r23, tr1		/* Both quad aligned, see vmlinux.lds.S */
#endif
	pta/u	hopeless, tr1

	/* Say bye to head.S but be prepared to wrongly get back ... */
	blink	tr0, LINK

	/* If we ever get back here through LINK/tr1-tr7 */
	pta/u	hopeless, tr7

hopeless:
	/*
	 * Something's badly wrong here. Loop endlessly,
         * there's nothing more we can do about it.
	 *
	 * Note on hopeless: it can be jumped into invariably
	 * before or after jumping into hyperspace. The only
	 * requirement is to be PIC called (PTA) before and
	 * any way (PTA/PTABS) after. According to Virtual
	 * to Physical mapping a simulator/emulator can easily
	 * tell where we came here from just looking at hopeless
	 * (PC) address.
	 *
	 * For debugging purposes:
	 * (r28) hopeless/loop address
	 * (r29) Original SR
	 * (r30) CPU type/Platform endianness
	 * (r31) FPU Support
	 * (r32) _start_kernel address
	 */
	blink	tr7, ZERO
