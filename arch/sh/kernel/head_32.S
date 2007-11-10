/* $Id: head.S,v 1.7 2003/09/01 17:58:19 lethal Exp $
 *
 *  arch/sh/kernel/head.S
 *
 *  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the SH exception handlers and startup code.
 */
#include <linux/linkage.h>
#include <asm/thread_info.h>

#ifdef CONFIG_CPU_SH4A
#define SYNCO()		synco

#define PREFI(label, reg)	\
	mov.l	label, reg;	\
	prefi	@reg
#else
#define SYNCO()
#define PREFI(label, reg)
#endif

	.section	.empty_zero_page, "aw"
ENTRY(empty_zero_page)
	.long	1		/* MOUNT_ROOT_RDONLY */
	.long	0		/* RAMDISK_FLAGS */
	.long	0x0200		/* ORIG_ROOT_DEV */
	.long	1		/* LOADER_TYPE */
	.long	0x00360000	/* INITRD_START */
	.long	0x000a0000	/* INITRD_SIZE */
	.long	0
1:
	.skip	PAGE_SIZE - empty_zero_page - 1b

	.section	.text.head, "ax"

/*
 * Condition at the entry of _stext:
 *
 *   BSC has already been initialized.
 *   INTC may or may not be initialized.
 *   VBR may or may not be initialized.
 *   MMU may or may not be initialized.
 *   Cache may or may not be initialized.
 *   Hardware (including on-chip modules) may or may not be initialized. 
 *
 */
ENTRY(_stext)
	!			Initialize Status Register
	mov.l	1f, r0		! MD=1, RB=0, BL=0, IMASK=0xF
	ldc	r0, sr
	!			Initialize global interrupt mask
#ifdef CONFIG_CPU_HAS_SR_RB
	mov	#0, r0
	ldc	r0, r6_bank
#endif
	
	/*
	 * Prefetch if possible to reduce cache miss penalty.
	 *
	 * We do this early on for SH-4A as a micro-optimization,
	 * as later on we will have speculative execution enabled
	 * and this will become less of an issue.
	 */
	PREFI(5f, r0)
	PREFI(6f, r0)

	!
	mov.l	2f, r0
	mov	r0, r15		! Set initial r15 (stack pointer)
#ifdef CONFIG_CPU_HAS_SR_RB
	mov.l	7f, r0
	ldc	r0, r7_bank	! ... and initial thread_info
#endif
	
	!			Clear BSS area
#ifdef CONFIG_SMP	
	mov.l	3f, r0
	cmp/eq	#0, r0		! skip clear if set to zero
	bt	10f
#endif
	
	mov.l	3f, r1
	add	#4, r1
	mov.l	4f, r2
	mov	#0, r0
9:	cmp/hs	r2, r1
	bf/s	9b		! while (r1 < r2)
	 mov.l	r0,@-r2

10:		
	!			Additional CPU initialization
	mov.l	6f, r0
	jsr	@r0
	 nop

	SYNCO()			! Wait for pending instructions..
	
	!			Start kernel
	mov.l	5f, r0
	jmp	@r0
	 nop

	.balign 4
#if defined(CONFIG_CPU_SH2)
1:	.long	0x000000F0		! IMASK=0xF
#else
1:	.long	0x400080F0		! MD=1, RB=0, BL=0, FD=1, IMASK=0xF
#endif
ENTRY(stack_start)
2:	.long	init_thread_union+THREAD_SIZE
3:	.long	__bss_start
4:	.long	_end
5:	.long	start_kernel
6:	.long	sh_cpu_init
7:	.long	init_thread_union
