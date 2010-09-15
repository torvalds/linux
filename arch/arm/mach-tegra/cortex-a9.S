/*
 * arch/arm/mach-tegra/cortex-a9.S
 *
 * CPU state save & restore routines for CPU hotplug
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/domain.h>
#include <asm/ptrace.h>
#include <asm/cache.h>
#include <asm/vfpmacros.h>
#include <asm/hardware/cache-l2x0.h>

#include <mach/iomap.h>
#include <mach/io.h>

/*	.section ".cpuinit.text", "ax"*/

#define TTB_FLAGS 0x6A	@ IRGN_WBWA, OC_RGN_WBWA, S, NOS

/*
 * spooled CPU context is 1KB / CPU
 */
#define CTX_SP		0
#define CTX_CPSR	4
#define CTX_SPSR	8
#define CTX_CPACR	12
#define CTX_CSSELR	16
#define CTX_SCTLR	20
#define CTX_ACTLR	24
#define CTX_PCTLR	28

#define CTX_FPEXC	32
#define CTX_FPSCR	36
#define CTX_DIAGNOSTIC	40

#define CTX_TTBR0	48
#define CTX_TTBR1	52
#define CTX_TTBCR	56
#define CTX_DACR	60
#define CTX_PAR		64
#define CTX_PRRR	68
#define CTX_NMRR	72
#define CTX_VBAR	76
#define CTX_CONTEXTIDR	80
#define CTX_TPIDRURW	84
#define CTX_TPIDRURO	88
#define CTX_TPIDRPRW	92

#define CTX_SVC_SP	0
#define CTX_SVC_LR	-1	@ stored on stack
#define CTX_SVC_SPSR	8

#define CTX_SYS_SP	96
#define CTX_SYS_LR	100

#define CTX_ABT_SPSR	112
#define CTX_ABT_SP	116
#define CTX_ABT_LR	120

#define CTX_UND_SPSR	128
#define CTX_UND_SP	132
#define CTX_UND_LR	136

#define CTX_IRQ_SPSR	144
#define CTX_IRQ_SP	148
#define CTX_IRQ_LR	152

#define CTX_FIQ_SPSR	160
#define CTX_FIQ_R8	164
#define CTX_FIQ_R9	168
#define CTX_FIQ_R10	172
#define CTX_FIQ_R11	178
#define CTX_FIQ_R12	180
#define CTX_FIQ_SP	184
#define CTX_FIQ_LR	188

/* context only relevant for master cpu */
#ifdef CONFIG_CACHE_L2X0
#define CTX_L2_CTRL	224
#define CTX_L2_AUX	228
#define CTX_L2_TAG_CTRL	232
#define CTX_L2_DAT_CTRL	236
#define CTX_L2_PREFETCH 240
#endif

#define CTX_VFP_REGS	256
#define CTX_VFP_SIZE	(32 * 8)

#define CTX_CP14_REGS	512
#define CTS_CP14_DSCR	512
#define CTX_CP14_WFAR	516
#define CTX_CP14_VCR	520
#define CTX_CP14_CLAIM	524

/* Each of the folowing is 2 32-bit registers */
#define CTS_CP14_BKPT_0	528
#define CTS_CP14_BKPT_1	536
#define CTS_CP14_BKPT_2	544
#define CTS_CP14_BKPT_3	552
#define CTS_CP14_BKPT_4	560
#define CTS_CP14_BKPT_5	568

/* Each of the folowing is 2 32-bit registers */
#define CTS_CP14_WPT_0	576
#define CTS_CP14_WPT_1	584
#define CTS_CP14_WPT_2	592
#define CTS_CP14_WPT_3	600

#include "power.h"
#include "power-macros.S"

.macro	ctx_ptr, rd, tmp
	cpu_id	\tmp
	mov32	\rd, tegra_context_area
	ldr	\rd, [\rd]
	add	\rd, \rd, \tmp, lsl #(CONTEXT_SIZE_BYTES_SHIFT)
.endm

.macro	translate, pa, va, tmp
	mov	\tmp, #0x1000
	sub	\tmp, \tmp, #1
	bic	\pa, \va, \tmp
	mcr	p15, 0, \pa, c7, c8, 1
	mrc	p15, 0, \pa, c7, c4, 0
	bic	\pa, \pa, \tmp
	and	\tmp, \va, \tmp
	orr	\pa, \pa, \tmp
.endm

/*
 *	__cortex_a9_save(unsigned int mode)
 *
 *	 spools out the volatile processor state to memory, so that
 *	 the CPU may be safely powered down. does not preserve:
 *	 - CP15 c0 registers (except cache size select 2,c0/c0,0)
 *	 - CP15 c1 secure registers (c1/c1, 0-3)
 *	 - CP15 c5 fault status registers (c5/c0 0&1, c5/c1 0&1)
 *	 - CP15 c6 fault address registers (c6/c0 0&2)
 *	 - CP15 c9 performance monitor registers (c9/c12 0-5,
 *	     c9/c13 0-2, c9/c14 0-2)
 *	 - CP15 c10 TLB lockdown register (c10/c0, 0)
 *	 - CP15 c12 MVBAR (c12/c0, 1)
 *	 - CP15 c15 TLB lockdown registers
 */
	.align L1_CACHE_SHIFT
ENTRY(__cortex_a9_save)
	mrs	r3, cpsr
	cps	0x13			@ save off svc registers
	mov	r1, sp
	stmfd	sp!, {r3-r12, lr}

	bic	r2, sp, #(L1_CACHE_BYTES-1)

1:	mcr	p15, 0, r2, c7, c14, 1	@ clean out dirty stack cachelines
	add	r2, r2, #L1_CACHE_BYTES
	cmp	r2, r1
	ble	1b
	dsb

	ctx_ptr	r8, r9
	mov	r12, r0

	/* zero-out context area */
	mov	r9, r8
	add	r10, r8, #(CONTEXT_SIZE_BYTES)
	mov	r0, #0
	mov	r1, #0
	mov	r2, #0
	mov	r3, #0
	mov	r4, #0
	mov	r5, #0
	mov	r6, #0
	mov	r7, #0
2:	stmia	r9!, {r0-r7}
	cmp	r9, r10
	blo	2b

	mov	r0, sp
	mov	sp, r12			@ sp holds the power mode
	mrs	r1, cpsr
	mrs	r2, spsr

	mrc	p15, 0, r3, c1, c0, 2	@ cpacr
	stmia	r8, {r0-r3}
	mrc	p15, 2, r0, c0, c0, 0	@ csselr
	mrc	p15, 0, r1, c1, c0, 0	@ sctlr
	mrc	p15, 0, r2, c1, c0, 1	@ actlr
	mrc	p15, 0, r4, c15, c0, 0	@ pctlr
	add	r9, r8, #CTX_CSSELR
	stmia	r9, {r0-r2, r4}

#ifdef CONFIG_VFPv3
	orr	r2, r3, #0xF00000
	mcr	p15, 0, r2, c1, c0, 2	@ enable access to FPU
	VFPFMRX	r2, FPEXC
	str	r2, [r8, #CTX_FPEXC]
	mov	r1, #0x40000000		@ enable access to FPU
	VFPFMXR	FPEXC, r1
	VFPFMRX	r1, FPSCR
	str	r1, [r8, #CTX_FPSCR]
	isb
	add	r9, r8, #CTX_VFP_REGS

	VFPFSTMIA r9, r12	@ save out (16 or 32)*8B of FPU registers
	VFPFMXR	FPEXC, r2
	mrc	p15, 0, r3, c1, c0, 2	@ restore original FPEXC/CPACR
#endif
	mrc	p15, 0, r0, c15, c0, 1	@ diag
	str	r0, [r8, #CTX_DIAGNOSTIC]

	add	r9, r8, #CTX_TTBR0
	mrc	p15, 0, r0, c2, c0, 0	@ TTBR0
	mrc	p15, 0, r1, c2, c0, 1	@ TTBR1
	mrc	p15, 0, r2, c2, c0, 2	@ TTBCR
	mrc	p15, 0, r3, c3, c0, 0	@ domain access control reg
	mrc	p15, 0, r4, c7, c4, 0	@ PAR
	mrc	p15, 0, r5, c10, c2, 0	@ PRRR
	mrc	p15, 0, r6, c10, c2, 1	@ NMRR
	mrc	p15, 0, r7, c12, c0, 0	@ VBAR
	stmia	r9!, {r0-r7}
	mrc	p15, 0, r0, c13, c0, 1	@ CONTEXTIDR
	mrc	p15, 0, r1, c13, c0, 2	@ TPIDRURW
	mrc	p15, 0, r2, c13, c0, 3	@ TPIDRURO
	mrc	p15, 0, r3, c13, c0, 4	@ TPIDRPRW
	stmia	r9, {r0-r3}

	cps	0x1f			@ SYS mode
	add	r9, r8, #CTX_SYS_SP
	stmia	r9, {sp,lr}

	cps	0x17			@ Abort mode
	mrs	r12, spsr
	add	r9, r8, #CTX_ABT_SPSR
	stmia	r9, {r12,sp,lr}

	cps	0x12			@ IRQ mode
	mrs	r12, spsr
	add	r9, r8, #CTX_IRQ_SPSR
	stmia	r9, {r12,sp,lr}

	cps	0x1b			@ Undefined mode
	mrs	r12, spsr
	add	r9, r8, #CTX_UND_SPSR
	stmia	r9, {r12,sp,lr}

	mov	r0, r8
	add	r1, r8, #CTX_FIQ_SPSR
	cps	0x11			@ FIQ mode
	mrs	r7, spsr
	stmia	r1, {r7-r12,sp,lr}

	cps	0x13			@ back to SVC
	mov	r8, r0

	/* Save CP14 debug controller context */
	add	r9, r8, #CTX_CP14_REGS
	mrc     p14, 0, r0, c0, c1, 0	@ DSCR
	mrc	p14, 0, r1, c0, c6, 0	@ WFAR
	mrc	p14, 0, r2, c0, c7, 0	@ VCR
	mrc	p14, 0, r3, c7, c9, 6	@ CLAIM
	stmia	r9, {r0-r3}

	add	r9, r8, #CTS_CP14_BKPT_0
	mrc	p14, 0, r2, c0, c0, 4
	mrc	p14, 0, r3, c0, c0, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0
	mrc	p14, 0, r2, c0, c1, 4
	mrc	p14, 0, r3, c0, c1, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0
	mrc	p14, 0, r2, c0, c2, 4
	mrc	p14, 0, r3, c0, c2, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0
	mrc	p14, 0, r2, c0, c3, 4
	mrc	p14, 0, r3, c0, c3, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0
	mrc	p14, 0, r2, c0, c4, 4
	mrc	p14, 0, r3, c0, c4, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0
	mrc	p14, 0, r2, c0, c5, 4
	mrc	p14, 0, r3, c0, c5, 5
	stmia	r9!, {r2-r3}     	@ BRKPT_0

	add	r9, r8, #CTS_CP14_WPT_0
	mrc	p14, 0, r2, c0, c0, 6
	mrc	p14, 0, r3, c0, c0, 7
	stmia	r9!, {r2-r3}     	@ WPT_0
	mrc	p14, 0, r2, c0, c1, 6
	mrc	p14, 0, r3, c0, c1, 7
	stmia	r9!, {r2-r3}     	@ WPT_0
	mrc	p14, 0, r2, c0, c2, 6
	mrc	p14, 0, r3, c0, c2, 7
	stmia	r9!, {r2-r3}     	@ WPT_0
	mrc	p14, 0, r2, c0, c3, 6
	mrc	p14, 0, r3, c0, c3, 7
	stmia	r9!, {r2-r3}     	@ WPT_0

#ifdef CONFIG_CACHE_L2X0
	cpu_id	r4
	cmp	r4, #0
	bne	__cortex_a9_save_clean_cache
	mov32	r4, (TEGRA_ARM_PL310_BASE-IO_CPU_PHYS+IO_CPU_VIRT)
	add	r9, r8, #CTX_L2_CTRL
	ldr	r0, [r4, #L2X0_CTRL]
	ldr	r1, [r4, #L2X0_AUX_CTRL]
	ldr	r2, [r4, #L2X0_TAG_LATENCY_CTRL]
	ldr	r3, [r4, #L2X0_DATA_LATENCY_CTRL]
	ldr	r4, [r4, #L2X0_PREFETCH_OFFSET]
	stmia	r9, {r0-r4}
#endif


__cortex_a9_save_clean_cache:
	mov	r10, r8
	add	r9, r10, #(CONTEXT_SIZE_BYTES)
	add	r9, r9, #(L1_CACHE_BYTES-1)
	bic	r10, r10, #(L1_CACHE_BYTES-1)
	bic	r9, r9, #(L1_CACHE_BYTES-1)

3:	mcr	p15, 0, r10, c7, c10, 1
	add	r10, r10, #L1_CACHE_BYTES
	cmp	r10, r9
	blo	3b
	dsb

	translate r10, r8, r1

	mov	r0, #0
	mcr	p15, 0, r0, c1, c0, 1	@ exit coherency
	isb
	cpu_id	r0
	mov32	r1, (TEGRA_ARM_PERIF_BASE-IO_CPU_PHYS+IO_CPU_VIRT+0xC)
	mov	r3, r0, lsl #2
	mov	r2, #0xf
	mov	r2, r2, lsl r3
	str	r2, [r1]		@ invalidate SCU tags for CPU

	cmp	r0, #0
	bne	__put_cpu_in_reset
	mov	r8, r10
	b	__tear_down_master
ENDPROC(__cortex_a9_save)

/*
 *	__cortex_a9_restore
 *
 *	 reloads the volatile CPU state from the context area
 *	 the MMU should already be enabled using the secondary_data
 *	 page tables for cpu_up before this function is called, and the
 *	 CPU should be coherent with the SMP complex
 */
	.align L1_CACHE_SHIFT
ENTRY(__cortex_a9_restore)
	cps	0x13
	ctx_ptr	r0, r9

	cps	0x11			@ FIQ mode
	add	r1, r0, #CTX_FIQ_SPSR
	ldmia	r1, {r7-r12,sp,lr}
	msr	spsr_fsxc, r7

	cps	0x12			@ IRQ mode
	add	r1, r0, #CTX_IRQ_SPSR
	ldmia	r1, {r12, sp, lr}
	msr	spsr_fsxc, r12

	cps	0x17			@ abort mode
	add	r1, r0, #CTX_ABT_SPSR
	ldmia	r1, {r12, sp, lr}
	msr	spsr_fsxc, r12

	cps	0x1f			@ SYS mode
	add	r1, r0, #CTX_SYS_SP
	ldmia	r1, {sp, lr}

	cps	0x1b			@ Undefined mode
	add	r1, r0, #CTX_UND_SPSR
	ldmia	r1, {r12, sp, lr}
	msr	spsr_fsxc, r12

	cps	0x13			@ back to SVC
	mov	r8, r0

	add	r9, r8, #CTX_CSSELR
	ldmia	r9, {r0-r3}

	mcr	p15, 2, r0, c0, c0, 0	@ csselr
	mcr	p15, 0, r1, c1, c0, 0	@ sctlr
	mcr	p15, 0, r2, c1, c0, 1	@ actlr
	mcr	p15, 0, r3, c15, c0, 0	@ pctlr

	add	r9, r8, #CTX_TTBR0
	ldmia	r9!, {r0-r7}

	mcr	p15, 0, r4, c7, c4, 0	@ PAR
	mcr	p15, 0, r7, c12, c0, 0	@ VBAR
	mcr	p15, 0, r3, c3, c0, 0	@ domain access control reg
	isb
	mcr	p15, 0, r2, c2, c0, 2	@ TTBCR
	isb
	mcr	p15, 0, r5, c10, c2, 0	@ PRRR
	isb
	mcr	p15, 0, r6, c10, c2, 1	@ NMRR
	isb

	ldmia	r9, {r4-r7}

	mcr	p15, 0, r5, c13, c0, 2	@ TPIDRURW
	mcr	p15, 0, r6, c13, c0, 3	@ TPIDRURO
	mcr	p15, 0, r7, c13, c0, 4	@ TPIDRPRW

	ldmia	r8, {r5-r7, lr}

	/* perform context switch to previous context */
	mov	r9, #0
	mcr	p15, 0, r9, c13, c0, 1	@ set reserved context
	isb
	mcr	p15, 0, r0, c2, c0, 0	@ TTBR0
	isb
	mcr	p15, 0, r4, c13, c0, 1	@ CONTEXTIDR
	isb
	mcr	p15, 0, r1, c2, c0, 1	@ TTBR1
	isb

	mov	r4, #0
	mcr	p15, 0, r4, c8, c3, 0	@ invalidate TLB
	mcr	p15, 0, r4, c7, c5, 6	@ flush BTAC
	mcr	p15, 0, r4, c7, c5, 0	@ flush instruction cache
	dsb
	isb

	mov	sp, r5
	msr	cpsr_cxsf, r6
	msr	spsr_cxsf, r7

	/* Restore CP14 debug controller context */
	add	r9, r8, #CTX_CP14_REGS
	ldmia	r9, {r0-r3}
	mcr	p14, 0, r1, c0, c6, 0	@ WFAR
	mcr	p14, 0, r2, c0, c7, 0	@ VCR
	mcr	p14, 0, r3, c7, c8, 6	@ CLAIM

	add	r9, r8, #CTS_CP14_BKPT_0
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c0, 4
	mcr	p14, 0, r3, c0, c0, 5
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c1, 4
	mcr	p14, 0, r3, c0, c1, 5
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c2, 4
	mcr	p14, 0, r3, c0, c2, 5
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c3, 4
	mcr	p14, 0, r3, c0, c3, 5
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c4, 4
	mcr	p14, 0, r3, c0, c4, 5
	ldmia	r9!, {r2-r3}		@ BRKPT_0
	mcr	p14, 0, r2, c0, c5, 4
	mcr	p14, 0, r3, c0, c5, 5

	add	r9, r8, #CTS_CP14_WPT_0
	ldmia	r9!, {r2-r3}		@ WPT_0
	mcr	p14, 0, r2, c0, c0, 6
	mcr	p14, 0, r3, c0, c0, 7
	ldmia	r9!, {r2-r3}		@ WPT_0
	mcr	p14, 0, r2, c0, c1, 6
	mcr	p14, 0, r3, c0, c1, 7
	ldmia	r9!, {r2-r3}		@ WPT_0
	mcr	p14, 0, r2, c0, c2, 6
	mcr	p14, 0, r3, c0, c2, 7
	ldmia	r9!, {r2-r3}		@ WPT_0
	mcr	p14, 0, r2, c0, c3, 6
	mcr	p14, 0, r3, c0, c3, 7
	isb
	mcr	p14, 0, r0, c0, c2, 2	@ DSCR
	isb

#ifdef CONFIG_VFPv3
	orr	r4, lr, #0xF00000
	mcr	p15, 0, r4, c1, c0, 2	@ enable coproc access
	mov	r5, #0x40000000
	VFPFMXR	FPEXC, r5		@ enable FPU access
	add	r9, r8, #CTX_VFP_REGS
	add	r7, r8, #CTX_FPEXC
	VFPFLDMIA r9, r10
	ldmia	r7, {r0, r4}
	VFPFMXR	FPSCR, r4
	VFPFMXR	FPEXC, r0
#endif
	mcr	p15, 0, lr, c1, c0, 2	@ cpacr (loaded before VFP)

	ldr	r9, [r8, #CTX_DIAGNOSTIC]
	mcr	p15, 0, r9, c15, c0, 1	@ diag

	/* finally, restore the stack and return */
	ldmfd	sp!, {r3-r12, lr}
	msr	cpsr_fsxc, r3		@ restore original processor mode
        isb
	mov	pc, lr
ENDPROC(__cortex_a9_restore)

/*
 *	__cortex_a9_l2x0_restart(bool invalidate)
 *
 *	 Reconfigures the L2 cache following a power event.
 */
	.align L1_CACHE_SHIFT
ENTRY(__cortex_a9_l2x0_restart)
#ifdef CONFIG_CACHE_L2X0
	ctx_ptr	r8, r9
	mov32	r9, (TEGRA_ARM_PL310_BASE-IO_CPU_PHYS+IO_CPU_VIRT)
	add	r10, r8, #CTX_L2_CTRL
	ldmia	r10, {r3-r7}
	str	r5, [r9, #L2X0_TAG_LATENCY_CTRL]
	str	r6, [r9, #L2X0_DATA_LATENCY_CTRL]
	str	r7, [r9, #L2X0_PREFETCH_OFFSET]
	str	r4, [r9, #L2X0_AUX_CTRL]
	cmp	r0, #0

	beq	__reenable_l2x0

	mov	r0, #0xff
	str	r0, [r9, #L2X0_INV_WAY]
1:	ldr	r1, [r9, #L2X0_INV_WAY]
	tst	r1, r0
	bne	1b
	mov	r0, #0
	str	r0, [r9, #L2X0_CACHE_SYNC]
__reenable_l2x0:
	mov	r5, #0
	mcr	p15, 0, r5, c8, c3, 0	@ invalidate TLB
	mcr	p15, 0, r5, c7, c5, 6	@ flush BTAC
	mcr	p15, 0, r5, c7, c5, 0	@ flush instruction cache
	dsb
	isb
	str	r3, [r9, #L2X0_CTRL]
#endif
	b	__cortex_a9_restore


	.align L1_CACHE_SHIFT
ENTRY(__shut_off_mmu)
	mrc	p15, 0, r3, c1, c0, 0
	movw	r2, #(1<<12) | (1<<11) | (1<<2) | (1<<0)
	bic	r3, r3, r2
	dsb
	mcr	p15, 0, r3, c1, c0, 0
	isb
	bx	r9
ENDPROC(__shut_off_mmu)

/*
 *	__invalidate_l1
 *
 *	  Invalidates the L1 data cache (no clean) during initial boot of
 *	  a secondary processor
 *
 *	  Corrupted registers: r0-r6
 */
__invalidate_l1:
	mov	r0, #0
	mcr	p15, 2, r0, c0, c0, 0
	mrc	p15, 1, r0, c0, c0, 0

	movw	r1, #0x7fff
	and	r2, r1, r0, lsr #13

	movw	r1, #0x3ff

	and	r3, r1, r0, lsr #3  @ NumWays - 1
	add	r2, r2, #1	@ NumSets

	and	r0, r0, #0x7
	add	r0, r0, #4	@ SetShift

	clz	r1, r3		@ WayShift
	add	r4, r3, #1	@ NumWays
1:	sub	r2, r2, #1	@ NumSets--
	mov	r3, r4		@ Temp = NumWays
2:	subs    r3, r3, #1	@ Temp--
	mov	r5, r3, lsl r1
	mov	r6, r2, lsl r0
	orr	r5, r5, r6	@ Reg = (Temp<<WayShift)|(NumSets<<SetShift)
	mcr	p15, 0, r5, c7, c6, 2
	bgt	2b
	cmp	r2, #0
	bgt	1b
	dsb
	isb
	bx	lr
ENDPROC(__invalidate_l1)

/*
 *	__invalidate_cpu_state
 *
 *	 Invalidates volatile CPU state (SCU tags, caches, branch address
 *	 arrays, exclusive monitor, etc.) so that they can be safely enabled
 *	 instruction caching and branch predicition enabled as early as
 *	 possible to improve performance
 */
ENTRY(__invalidate_cpu_state)
	clrex
	mov	r0, #0
	mcr	p15, 0, r0, c1, c0, 1	@ disable SMP, prefetch, broadcast
	isb
	mcr	p15, 0, r0, c7, c5, 0	@ invalidate BTAC, i-cache
	mcr	p15, 0, r0, c7, c5, 6	@ invalidate branch pred array
	mcr	p15, 0, r0, c8, c7, 0	@ invalidate unified TLB
	dsb
	isb

	cpu_id	r0
	cmp	r0, #0
	mov32	r1, (TEGRA_ARM_PERIF_BASE + 0xC)
	movne	r0, r0, lsl #2
	movne	r2, #0xf
	movne	r2, r2, lsl r0
	strne	r2, [r1]		@ invalidate SCU tags for CPU

	dsb
	mov	r0, #0x1800
	mcr	p15, 0, r0, c1, c0, 0	@ enable branch prediction, i-cache
	isb
	b	__invalidate_l1		@ invalidate data cache
ENDPROC(__invalidate_cpu_state)

/*
 *	__return_to_virtual(unsigned long pgdir, void (*ctx_restore)(void))
 *
 *	  Restores a CPU to the world of virtual addressing, using the
 *	  specified page tables (which must ensure that a VA=PA mapping
 *	  exists for the __enable_mmu function), and then jumps to
 *	  ctx_restore to restore CPU context and return control to the OS
 */
	.align L1_CACHE_SHIFT
ENTRY(__return_to_virtual)
	orr	r8, r0, #TTB_FLAGS
	mov	lr, r1			@ "return" to ctx_restore
	mov	r3, #0
	mcr	p15, 0, r3, c2, c0, 2	@ TTB control register

	mcr	p15, 0, r8, c2, c0, 1	@ load TTBR1

	mov	r0, #0x1f
	mcr	p15, 0, r0, c3, c0, 0	@ domain access register

	mov32	r0, 0xff0a89a8
#ifdef CONFIG_SMP
	mov32	r1, 0xc0e0c4e0
#else
	mov32	r1, 0x40e044e0
#endif
	mcr	p15, 0, r0, c10, c2, 0	@ PRRR
	mcr	p15, 0, r1, c10, c2, 1	@ NMRR
	mrc	p15, 0, r0, c1, c0, 0
	mov32	r1, 0x0120c302
	bic	r0, r0, r1
	mov32	r1, 0x10c03c7d
	orr	r0, r0, r1

#ifdef CONFIG_ALIGNMENT_TRAP
	orr	r0, r0, #0x2
#else
	bic	r0, r0, #0x2
#endif
	mov	r1, #(domain_val(DOMAIN_USER, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_TABLE, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_IO, DOMAIN_CLIENT))
	mcr	p15, 0, r1, c3, c0, 0	@ domain access register
	mcr	p15, 0, r8, c2, c0, 0	@ TTBR0
	b	__turn_mmu_on_again
	andeq	r0, r0, r0
	andeq	r0, r0, r0
	andeq	r0, r0, r0
	andeq	r0, r0, r0
ENDPROC(__return_to_virtual)

/*
 *	__turn_mmu_on_again
 *
 *	  does exactly what it advertises: turns the MMU on, again
 *	  jumps to the *virtual* address lr after the MMU is enabled.
 */
	.align	L1_CACHE_SHIFT
__turn_mmu_on_again:
	mov	r0, r0
	mcr	p15, 0, r0, c1, c0, 0
	mrc	p15, 0, r3, c0, c0, 0
	mov	r3, r3
	mov	r3, lr
	bx	lr
ENDPROC(__turn_mmu_on_again)
