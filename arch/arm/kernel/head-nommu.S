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
#include <linux/errno.h>

#include <asm/assembler.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/memory.h>
#include <asm/cp15.h>
#include <asm/thread_info.h>
#include <asm/v7m.h>
#include <asm/mpu.h>
#include <asm/page.h>

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

 THUMB(	badr	r9, 1f		)	@ Kernel is always entered in ARM.
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

#ifdef CONFIG_ARM_MPU
	/* Calculate the size of a region covering just the kernel */
	ldr	r5, =PLAT_PHYS_OFFSET		@ Region start: PHYS_OFFSET
	ldr     r6, =(_end)			@ Cover whole kernel
	sub	r6, r6, r5			@ Minimum size of region to map
	clz	r6, r6				@ Region size must be 2^N...
	rsb	r6, r6, #31			@ ...so round up region size
	lsl	r6, r6, #MPU_RSR_SZ		@ Put size in right field
	orr	r6, r6, #(1 << MPU_RSR_EN)	@ Set region enabled bit
	bl	__setup_mpu
#endif

	badr	lr, 1f				@ return (PIC) address
	ldr	r12, [r10, #PROCINFO_INITFUNC]
	add	r12, r12, r10
	ret	r12
1:	bl	__after_proc_init
	b	__mmap_switched
ENDPROC(stext)

#ifdef CONFIG_SMP
	.text
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

	ldr	r7, __secondary_data

#ifdef CONFIG_ARM_MPU
	/* Use MPU region info supplied by __cpu_up */
	ldr	r6, [r7]			@ get secondary_data.mpu_rgn_info
	bl      __secondary_setup_mpu		@ Initialize the MPU
#endif

	badr	lr, 1f				@ return (PIC) address
	ldr	r12, [r10, #PROCINFO_INITFUNC]
	add	r12, r12, r10
	ret	r12
1:	bl	__after_proc_init
	ldr	sp, [r7, #12]			@ set up the stack pointer
	mov	fp, #0
	b	secondary_start_kernel
ENDPROC(secondary_startup)

	.type	__secondary_data, %object
__secondary_data:
	.long	secondary_data
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
	mcr	p15, 0, r0, c1, c0, 0		@ write control reg
#elif defined (CONFIG_CPU_V7M)
	/* For V7M systems we want to modify the CCR similarly to the SCTLR */
#ifdef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #V7M_SCB_CCR_DC
#endif
#ifdef CONFIG_CPU_BPREDICT_DISABLE
	bic	r0, r0, #V7M_SCB_CCR_BP
#endif
#ifdef CONFIG_CPU_ICACHE_DISABLE
	bic	r0, r0, #V7M_SCB_CCR_IC
#endif
	movw	r3, #:lower16:(BASEADDR_V7M_SCB + V7M_SCB_CCR)
	movt	r3, #:upper16:(BASEADDR_V7M_SCB + V7M_SCB_CCR)
	str	r0, [r3]
#endif /* CONFIG_CPU_CP15 elif CONFIG_CPU_V7M */
	ret	lr
ENDPROC(__after_proc_init)
	.ltorg

#ifdef CONFIG_ARM_MPU


#ifndef CONFIG_CPU_V7M
/* Set which MPU region should be programmed */
.macro set_region_nr tmp, rgnr, unused
	mov	\tmp, \rgnr			@ Use static region numbers
	mcr	p15, 0, \tmp, c6, c2, 0		@ Write RGNR
.endm

/* Setup a single MPU region, either D or I side (D-side for unified) */
.macro setup_region bar, acr, sr, side = MPU_DATA_SIDE, unused
	mcr	p15, 0, \bar, c6, c1, (0 + \side)	@ I/DRBAR
	mcr	p15, 0, \acr, c6, c1, (4 + \side)	@ I/DRACR
	mcr	p15, 0, \sr, c6, c1, (2 + \side)		@ I/DRSR
.endm
#else
.macro set_region_nr tmp, rgnr, base
	mov	\tmp, \rgnr
	str     \tmp, [\base, #MPU_RNR]
.endm

.macro setup_region bar, acr, sr, unused, base
	lsl     \acr, \acr, #16
	orr     \acr, \acr, \sr
	str     \bar, [\base, #MPU_RBAR]
	str     \acr, [\base, #MPU_RASR]
.endm

#endif
/*
 * Setup the MPU and initial MPU Regions. We create the following regions:
 * Region 0: Use this for probing the MPU details, so leave disabled.
 * Region 1: Background region - covers the whole of RAM as strongly ordered
 * Region 2: Normal, Shared, cacheable for RAM. From PHYS_OFFSET, size from r6
 * Region 3: Normal, shared, inaccessible from PL0 to protect the vectors page
 *
 * r6: Value to be written to DRSR (and IRSR if required) for MPU_RAM_REGION
*/

ENTRY(__setup_mpu)

	/* Probe for v7 PMSA compliance */
M_CLASS(movw	r12, #:lower16:BASEADDR_V7M_SCB)
M_CLASS(movt	r12, #:upper16:BASEADDR_V7M_SCB)

AR_CLASS(mrc	p15, 0, r0, c0, c1, 4)		@ Read ID_MMFR0
M_CLASS(ldr	r0, [r12, 0x50])
	and	r0, r0, #(MMFR0_PMSA)		@ PMSA field
	teq	r0, #(MMFR0_PMSAv7)		@ PMSA v7
	bxne	lr

	/* Determine whether the D/I-side memory map is unified. We set the
	 * flags here and continue to use them for the rest of this function */
AR_CLASS(mrc	p15, 0, r0, c0, c0, 4)		@ MPUIR
M_CLASS(ldr    r0, [r12, #MPU_TYPE])
	ands	r5, r0, #MPUIR_DREGION_SZMASK	@ 0 size d region => No MPU
	bxeq	lr
	tst	r0, #MPUIR_nU			@ MPUIR_nU = 0 for unified

	/* Setup second region first to free up r6 */
	set_region_nr r0, #MPU_RAM_REGION, r12
	isb
	/* Full access from PL0, PL1, shared for CONFIG_SMP, cacheable */
	ldr	r0, =PLAT_PHYS_OFFSET		@ RAM starts at PHYS_OFFSET
	ldr	r5,=(MPU_AP_PL1RW_PL0RW | MPU_RGN_NORMAL)

	setup_region r0, r5, r6, MPU_DATA_SIDE, r12	@ PHYS_OFFSET, shared, enabled
	beq	1f					@ Memory-map not unified
	setup_region r0, r5, r6, MPU_INSTR_SIDE, r12	@ PHYS_OFFSET, shared, enabled
1:	isb

	/* First/background region */
	set_region_nr r0, #MPU_BG_REGION, r12
	isb
	/* Execute Never,  strongly ordered, inaccessible to PL0, rw PL1  */
	mov	r0, #0				@ BG region starts at 0x0
	ldr	r5,=(MPU_ACR_XN | MPU_RGN_STRONGLY_ORDERED | MPU_AP_PL1RW_PL0NA)
	mov	r6, #MPU_RSR_ALL_MEM		@ 4GB region, enabled

	setup_region r0, r5, r6, MPU_DATA_SIDE, r12	@ 0x0, BG region, enabled
	beq	2f					@ Memory-map not unified
	setup_region r0, r5, r6, MPU_INSTR_SIDE r12	@ 0x0, BG region, enabled
2:	isb

#ifdef CONFIG_XIP_KERNEL
	set_region_nr r0, #MPU_ROM_REGION, r12
	isb

	ldr	r5,=(MPU_AP_PL1RO_PL0NA | MPU_RGN_NORMAL)

	ldr	r0, =CONFIG_XIP_PHYS_ADDR		@ ROM start
	ldr     r6, =(_exiprom)				@ ROM end
	sub	r6, r6, r0				@ Minimum size of region to map
	clz	r6, r6					@ Region size must be 2^N...
	rsb	r6, r6, #31				@ ...so round up region size
	lsl	r6, r6, #MPU_RSR_SZ			@ Put size in right field
	orr	r6, r6, #(1 << MPU_RSR_EN)		@ Set region enabled bit

	setup_region r0, r5, r6, MPU_DATA_SIDE, r12	@ XIP_PHYS_ADDR, shared, enabled
	beq	3f					@ Memory-map not unified
	setup_region r0, r5, r6, MPU_INSTR_SIDE, r12	@ XIP_PHYS_ADDR, shared, enabled
3:	isb
#endif

	/* Enable the MPU */
AR_CLASS(mrc	p15, 0, r0, c1, c0, 0)		@ Read SCTLR
AR_CLASS(bic	r0, r0, #CR_BR)			@ Disable the 'default mem-map'
AR_CLASS(orr	r0, r0, #CR_M)			@ Set SCTRL.M (MPU on)
AR_CLASS(mcr	p15, 0, r0, c1, c0, 0)		@ Enable MPU

M_CLASS(ldr	r0, [r12, #MPU_CTRL])
M_CLASS(bic	r0, #MPU_CTRL_PRIVDEFENA)
M_CLASS(orr	r0, #MPU_CTRL_ENABLE)
M_CLASS(str	r0, [r12, #MPU_CTRL])
	isb

	ret	lr
ENDPROC(__setup_mpu)

#ifdef CONFIG_SMP
/*
 * r6: pointer at mpu_rgn_info
 */

ENTRY(__secondary_setup_mpu)
	/* Probe for v7 PMSA compliance */
	mrc	p15, 0, r0, c0, c1, 4		@ Read ID_MMFR0
	and	r0, r0, #(MMFR0_PMSA)		@ PMSA field
	teq	r0, #(MMFR0_PMSAv7)		@ PMSA v7
	bne	__error_p

	/* Determine whether the D/I-side memory map is unified. We set the
	 * flags here and continue to use them for the rest of this function */
	mrc	p15, 0, r0, c0, c0, 4		@ MPUIR
	ands	r5, r0, #MPUIR_DREGION_SZMASK	@ 0 size d region => No MPU
	beq	__error_p

	ldr	r4, [r6, #MPU_RNG_INFO_USED]
	mov	r5, #MPU_RNG_SIZE
	add	r3, r6, #MPU_RNG_INFO_RNGS
	mla	r3, r4, r5, r3

1:
	tst	r0, #MPUIR_nU			@ MPUIR_nU = 0 for unified
	sub	r3, r3, #MPU_RNG_SIZE
	sub	r4, r4, #1

	set_region_nr r0, r4
	isb

	ldr	r0, [r3, #MPU_RGN_DRBAR]
	ldr	r6, [r3, #MPU_RGN_DRSR]
	ldr	r5, [r3, #MPU_RGN_DRACR]

	setup_region r0, r5, r6, MPU_DATA_SIDE
	beq	2f
	setup_region r0, r5, r6, MPU_INSTR_SIDE
2:	isb

	mrc	p15, 0, r0, c0, c0, 4		@ Reevaluate the MPUIR
	cmp	r4, #0
	bgt	1b

	/* Enable the MPU */
	mrc	p15, 0, r0, c1, c0, 0		@ Read SCTLR
	bic	r0, r0, #CR_BR			@ Disable the 'default mem-map'
	orr	r0, r0, #CR_M			@ Set SCTRL.M (MPU on)
	mcr	p15, 0, r0, c1, c0, 0		@ Enable MPU
	isb

	ret	lr
ENDPROC(__secondary_setup_mpu)

#endif /* CONFIG_SMP */
#endif /* CONFIG_ARM_MPU */
#include "head-common.S"
