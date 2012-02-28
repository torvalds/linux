/*
 *  linux/arch/arm/kernel/head-nommu.S
 *
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (C) 2003-2006 Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Common kernel startup code (non-paged MM)
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/cp15.h>
#include <asm/thread_info.h>
#include <asm/v7m.h>

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
 * r1 = machine nr.
 *
 * See linux/arch/arm/tools/mach-types for the complete list of machine
 * numbers for r1.
 *
 */

	__HEAD

#ifdef CONFIG_CPU_THUMBONLY
	.thumb
ENTRY(stext)
#else
	.arm
ENTRY(stext)

 THUMB(	adr	r9, BSYM(1f)	)	@ Kernel is always entered in ARM.
 THUMB(	bx	r9		)	@ If this is a Thumb-2 kernel,
 THUMB(	.thumb			)	@ switch to Thumb now.
 THUMB(1:			)
#endif

	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, r9 @ ensure svc mode
						@ and irqs disabled
#if defined(CONFIG_CPU_CP15)
	mrc	p15, 0, r9, c0, c0		@ get processor id
#elif defined(CONFIG_CPU_V7M)
	ldr	r9, =BASEADDR_V7M_SCB
	ldr	r9, [r9, V7M_SCB_CPUID]
#else
	ldr	r9, =CONFIG_PROCESSOR_ID
#endif
	bl	__lookup_processor_type		@ r5=procinfo r9=cpuid
	movs	r10, r5				@ invalid processor (r5=0)?
	beq	__error_p				@ yes, error 'p'

	ldr	r13, =__mmap_switched		@ address to jump to after
						@ initialising sctlr
	adr	lr, BSYM(1f)			@ return (PIC) address
 ARM(	add	pc, r10, #PROCINFO_INITFUNC	)
 THUMB(	add	r12, r10, #PROCINFO_INITFUNC	)
 THUMB(	mov	pc, r12				)
 1:	b	__after_proc_init
ENDPROC(stext)

#ifdef CONFIG_SMP
	__CPUINIT
ENTRY(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 *
	 * Ensure that we're in SVC mode, and IRQs are disabled.  Lookup
	 * the processor type - there is no need to check the machine type
	 * as it has already been validated by the primary processor.
	 */
	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, r9
#ifndef CONFIG_CPU_CP15
	ldr	r9, =CONFIG_PROCESSOR_ID
#else
	mrc	p15, 0, r9, c0, c0		@ get processor id
#endif
	bl	__lookup_processor_type		@ r5=procinfo r9=cpuid
	movs	r10, r5				@ invalid processor?
	beq	__error_p			@ yes, error 'p'

	adr	r4, __secondary_data
	ldmia	r4, {r7, r12}
	adr	lr, BSYM(__after_proc_init)	@ return address
	mov	r13, r12			@ __secondary_switched address
 ARM(	add	pc, r10, #PROCINFO_INITFUNC	)
 THUMB(	add	r12, r10, #PROCINFO_INITFUNC	)
 THUMB(	mov	pc, r12				)
ENDPROC(secondary_startup)

ENTRY(__secondary_switched)
	ldr	sp, [r7, #8]			@ set up the stack pointer
	mov	fp, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)

	.type	__secondary_data, %object
__secondary_data:
	.long	secondary_data
	.long	__secondary_switched
#endif /* CONFIG_SMP */

/*
 * Set the Control Register and Read the process ID.
 */
__after_proc_init:
#ifdef CONFIG_CPU_CP15
	/*
	 * CP15 system control register value returned in r0 from
	 * the CPU init function.
	 */
#if defined(CONFIG_ALIGNMENT_TRAP) && __LINUX_ARM_ARCH__ < 6
	orr	r0, r0, #CR_A
#else
	bic	r0, r0, #CR_A
#endif
#ifdef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CR_C
#endif
#ifdef CONFIG_CPU_BPREDICT_DISABLE
	bic	r0, r0, #CR_Z
#endif
#ifdef CONFIG_CPU_ICACHE_DISABLE
	bic	r0, r0, #CR_I
#endif
#ifdef CONFIG_CPU_HIGH_VECTOR
	orr	r0, r0, #CR_V
#else
	bic	r0, r0, #CR_V
#endif
	mcr	p15, 0, r0, c1, c0, 0		@ write control reg
#endif /* CONFIG_CPU_CP15 */
	mov	pc, r13
ENDPROC(__after_proc_init)
	.ltorg

#include "head-common.S"
