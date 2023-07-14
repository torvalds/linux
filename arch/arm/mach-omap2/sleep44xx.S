/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP44xx sleep code.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * 	Santosh Shilimkar <santosh.shilimkar@ti.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/smp_scu.h>
#include <asm/page.h>
#include <asm/hardware/cache-l2x0.h>

#include "omap-secure.h"

#include "common.h"
#include "omap44xx.h"
#include "omap4-sar-layout.h"

	.arch armv7-a

#if defined(CONFIG_SMP) && defined(CONFIG_PM)

	.arch_extension sec
.macro	DO_SMC
	dsb
	smc	#0
	dsb
.endm

#ifdef CONFIG_ARCH_OMAP4

/*
 * =============================
 * == CPU suspend finisher ==
 * =============================
 *
 * void omap4_finish_suspend(unsigned long cpu_state)
 *
 * This function code saves the CPU context and performs the CPU
 * power down sequence. Calling WFI effectively changes the CPU
 * power domains states to the desired target power state.
 *
 * @cpu_state : contains context save state (r0)
 *	0 - No context lost
 * 	1 - CPUx L1 and logic lost: MPUSS CSWR
 * 	2 - CPUx L1 and logic lost + GIC lost: MPUSS OSWR
 *	3 - CPUx L1 and logic lost + GIC + L2 lost: MPUSS OFF
 * @return: This function never returns for CPU OFF and DORMANT power states.
 * Post WFI, CPU transitions to DORMANT or OFF power state and on wake-up
 * from this follows a full CPU reset path via ROM code to CPU restore code.
 * The restore function pointer is stored at CPUx_WAKEUP_NS_PA_ADDR_OFFSET.
 * It returns to the caller for CPU INACTIVE and ON power states or in case
 * CPU failed to transition to targeted OFF/DORMANT state.
 *
 * omap4_finish_suspend() calls v7_flush_dcache_all() which doesn't save
 * stack frame and it expects the caller to take care of it. Hence the entire
 * stack frame is saved to avoid possible stack corruption.
 */
ENTRY(omap4_finish_suspend)
	stmfd	sp!, {r4-r12, lr}
	cmp	r0, #0x0
	beq	do_WFI				@ No lowpower state, jump to WFI

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	bl	omap4_get_sar_ram_base
	ldr	r9, [r0, #OMAP_TYPE_OFFSET]
	cmp	r9, #0x1			@ Check for HS device
	bne	skip_secure_l1_clean
	mov	r0, #SCU_PM_NORMAL
	mov	r1, #0xFF			@ clean seucre L1
	stmfd   r13!, {r4-r12, r14}
	ldr	r12, =OMAP4_MON_SCU_PWR_INDEX
	DO_SMC
	ldmfd   r13!, {r4-r12, r14}
skip_secure_l1_clean:
	bl	v7_flush_dcache_all

	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)		@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	bl	v7_invalidate_l1

	/*
	 * Switch the CPU from Symmetric Multiprocessing (SMP) mode
	 * to AsymmetricMultiprocessing (AMP) mode by programming
	 * the SCU power status to DORMANT or OFF mode.
	 * This enables the CPU to be taken out of coherency by
	 * preventing the CPU from receiving cache, TLB, or BTB
	 * maintenance operations broadcast by other CPUs in the cluster.
	 */
	bl	omap4_get_sar_ram_base
	mov	r8, r0
	ldr	r9, [r8, #OMAP_TYPE_OFFSET]
	cmp	r9, #0x1			@ Check for HS device
	bne	scu_gp_set
	mrc	p15, 0, r0, c0, c0, 5		@ Read MPIDR
	ands	r0, r0, #0x0f
	ldreq	r0, [r8, #SCU_OFFSET0]
	ldrne	r0, [r8, #SCU_OFFSET1]
	mov	r1, #0x00
	stmfd   r13!, {r4-r12, r14}
	ldr	r12, =OMAP4_MON_SCU_PWR_INDEX
	DO_SMC
	ldmfd   r13!, {r4-r12, r14}
	b	skip_scu_gp_set
scu_gp_set:
	mrc	p15, 0, r0, c0, c0, 5		@ Read MPIDR
	ands	r0, r0, #0x0f
	ldreq	r1, [r8, #SCU_OFFSET0]
	ldrne	r1, [r8, #SCU_OFFSET1]
	bl	omap4_get_scu_base
	bl	scu_power_mode
skip_scu_gp_set:
	mrc	p15, 0, r0, c1, c1, 2		@ Read NSACR data
	tst	r0, #(1 << 18)
	mrcne	p15, 0, r0, c1, c0, 1
	bicne	r0, r0, #(1 << 6)		@ Disable SMP bit
	mcrne	p15, 0, r0, c1, c0, 1
	isb
	dsb
#ifdef CONFIG_CACHE_L2X0
	/*
	 * Clean and invalidate the L2 cache.
	 * Common cache-l2x0.c functions can't be used here since it
	 * uses spinlocks. We are out of coherency here with data cache
	 * disabled. The spinlock implementation uses exclusive load/store
	 * instruction which can fail without data cache being enabled.
	 * OMAP4 hardware doesn't support exclusive monitor which can
	 * overcome exclusive access issue. Because of this, CPU can
	 * lead to deadlock.
	 */
	bl	omap4_get_sar_ram_base
	mov	r8, r0
	mrc	p15, 0, r5, c0, c0, 5		@ Read MPIDR
	ands	r5, r5, #0x0f
	ldreq	r0, [r8, #L2X0_SAVE_OFFSET0]	@ Retrieve L2 state from SAR
	ldrne	r0, [r8, #L2X0_SAVE_OFFSET1]	@ memory.
	cmp	r0, #3
	bne	do_WFI
#ifdef CONFIG_PL310_ERRATA_727915
	mov	r0, #0x03
	mov	r12, #OMAP4_MON_L2X0_DBG_CTRL_INDEX
	DO_SMC
#endif
	bl	omap4_get_l2cache_base
	mov	r2, r0
	ldr	r0, =0xffff
	str	r0, [r2, #L2X0_CLEAN_INV_WAY]
wait:
	ldr	r0, [r2, #L2X0_CLEAN_INV_WAY]
	ldr	r1, =0xffff
	ands	r0, r0, r1
	bne	wait
#ifdef CONFIG_PL310_ERRATA_727915
	mov	r0, #0x00
	mov	r12, #OMAP4_MON_L2X0_DBG_CTRL_INDEX
	DO_SMC
#endif
l2x_sync:
	bl	omap4_get_l2cache_base
	mov	r2, r0
	mov	r0, #0x0
	str	r0, [r2, #L2X0_CACHE_SYNC]
sync:
	ldr	r0, [r2, #L2X0_CACHE_SYNC]
	ands	r0, r0, #0x1
	bne	sync
#endif

do_WFI:
	bl	omap_do_wfi

	/*
	 * CPU is here when it failed to enter OFF/DORMANT or
	 * no low power state was attempted.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	tst	r0, #(1 << 2)			@ Check C bit enabled?
	orreq	r0, r0, #(1 << 2)		@ Enable the C bit
	mcreq	p15, 0, r0, c1, c0, 0
	isb

	/*
	 * Ensure the CPU power state is set to NORMAL in
	 * SCU power state so that CPU is back in coherency.
	 * In non-coherent mode CPU can lock-up and lead to
	 * system deadlock.
	 */
	mrc	p15, 0, r0, c1, c0, 1
	tst	r0, #(1 << 6)			@ Check SMP bit enabled?
	orreq	r0, r0, #(1 << 6)
	mcreq	p15, 0, r0, c1, c0, 1
	isb
	bl	omap4_get_sar_ram_base
	mov	r8, r0
	ldr	r9, [r8, #OMAP_TYPE_OFFSET]
	cmp	r9, #0x1			@ Check for HS device
	bne	scu_gp_clear
	mov	r0, #SCU_PM_NORMAL
	mov	r1, #0x00
	stmfd   r13!, {r4-r12, r14}
	ldr	r12, =OMAP4_MON_SCU_PWR_INDEX
	DO_SMC
	ldmfd   r13!, {r4-r12, r14}
	b	skip_scu_gp_clear
scu_gp_clear:
	bl	omap4_get_scu_base
	mov	r1, #SCU_PM_NORMAL
	bl	scu_power_mode
skip_scu_gp_clear:
	isb
	dsb
	ldmfd	sp!, {r4-r12, pc}
ENDPROC(omap4_finish_suspend)

/*
 * ============================
 * == CPU resume entry point ==
 * ============================
 *
 * void omap4_cpu_resume(void)
 *
 * ROM code jumps to this function while waking up from CPU
 * OFF or DORMANT state. Physical address of the function is
 * stored in the SAR RAM while entering to OFF or DORMANT mode.
 * The restore function pointer is stored at CPUx_WAKEUP_NS_PA_ADDR_OFFSET.
 */
ENTRY(omap4_cpu_resume)
	/*
	 * Configure ACTRL and enable NS SMP bit access on CPU1 on HS device.
	 * OMAP44XX EMU/HS devices - CPU0 SMP bit access is enabled in PPA
	 * init and for CPU1, a secure PPA API provided. CPU0 must be ON
	 * while executing NS_SMP API on CPU1 and PPA version must be 1.4.0+.
	 * OMAP443X GP devices- SMP bit isn't accessible.
	 * OMAP446X GP devices - SMP bit access is enabled on both CPUs.
	 */
	ldr	r8, =OMAP44XX_SAR_RAM_BASE
	ldr	r9, [r8, #OMAP_TYPE_OFFSET]
	cmp	r9, #0x1			@ Skip if GP device
	bne	skip_ns_smp_enable
	mrc     p15, 0, r0, c0, c0, 5
	ands    r0, r0, #0x0f
	beq	skip_ns_smp_enable
ppa_actrl_retry:
	mov     r0, #OMAP4_PPA_CPU_ACTRL_SMP_INDEX
	adr	r1, ppa_zero_params_offset
	ldr	r3, [r1]
	add	r3, r3, r1			@ Pointer to ppa_zero_params
	mov	r1, #0x0			@ Process ID
	mov	r2, #0x4			@ Flag
	mov	r6, #0xff
	mov	r12, #0x00			@ Secure Service ID
	DO_SMC
	cmp	r0, #0x0			@ API returns 0 on success.
	beq	enable_smp_bit
	b	ppa_actrl_retry
enable_smp_bit:
	mrc	p15, 0, r0, c1, c0, 1
	tst	r0, #(1 << 6)			@ Check SMP bit enabled?
	orreq	r0, r0, #(1 << 6)
	mcreq	p15, 0, r0, c1, c0, 1
	isb
skip_ns_smp_enable:
#ifdef CONFIG_CACHE_L2X0
	/*
	 * Restore the L2 AUXCTRL and enable the L2 cache.
	 * OMAP4_MON_L2X0_AUXCTRL_INDEX =  Program the L2X0 AUXCTRL
	 * OMAP4_MON_L2X0_CTRL_INDEX =  Enable the L2 using L2X0 CTRL
	 * register r0 contains value to be programmed.
	 * L2 cache is already invalidate by ROM code as part
	 * of MPUSS OFF wakeup path.
	 */
	ldr	r2, =OMAP44XX_L2CACHE_BASE
	ldr	r0, [r2, #L2X0_CTRL]
	and	r0, #0x0f
	cmp	r0, #1
	beq	skip_l2en			@ Skip if already enabled
	ldr	r3, =OMAP44XX_SAR_RAM_BASE
	ldr	r1, [r3, #OMAP_TYPE_OFFSET]
	cmp	r1, #0x1			@ Check for HS device
	bne     set_gp_por
	ldr     r0, =OMAP4_PPA_L2_POR_INDEX
	ldr     r1, =OMAP44XX_SAR_RAM_BASE
	ldr     r4, [r1, #L2X0_PREFETCH_CTRL_OFFSET]
	adr     r1, ppa_por_params_offset
	ldr	r3, [r1]
	add	r3, r3, r1			@ Pointer to ppa_por_params
	str     r4, [r3, #0x04]
	mov	r1, #0x0			@ Process ID
	mov	r2, #0x4			@ Flag
	mov	r6, #0xff
	mov	r12, #0x00			@ Secure Service ID
	DO_SMC
	b	set_aux_ctrl
set_gp_por:
	ldr     r1, =OMAP44XX_SAR_RAM_BASE
	ldr     r0, [r1, #L2X0_PREFETCH_CTRL_OFFSET]
	ldr	r12, =OMAP4_MON_L2X0_PREFETCH_INDEX	@ Setup L2 PREFETCH
	DO_SMC
set_aux_ctrl:
	ldr     r1, =OMAP44XX_SAR_RAM_BASE
	ldr	r0, [r1, #L2X0_AUXCTRL_OFFSET]
	ldr	r12, =OMAP4_MON_L2X0_AUXCTRL_INDEX	@ Setup L2 AUXCTRL
	DO_SMC
	mov	r0, #0x1
	ldr	r12, =OMAP4_MON_L2X0_CTRL_INDEX		@ Enable L2 cache
	DO_SMC
skip_l2en:
#endif

	b	cpu_resume			@ Jump to generic resume
ppa_por_params_offset:
	.long	ppa_por_params - .
ENDPROC(omap4_cpu_resume)
#endif	/* CONFIG_ARCH_OMAP4 */

#endif	/* defined(CONFIG_SMP) && defined(CONFIG_PM) */

ENTRY(omap_do_wfi)
	stmfd	sp!, {lr}
#ifdef CONFIG_OMAP_INTERCONNECT_BARRIER
	/* Drain interconnect write buffers. */
	bl	omap_interconnect_sync
#endif

	/*
	 * Execute an ISB instruction to ensure that all of the
	 * CP15 register changes have been committed.
	 */
	isb

	/*
	 * Execute a barrier instruction to ensure that all cache,
	 * TLB and branch predictor maintenance operations issued
	 * by any CPU in the cluster have completed.
	 */
	dsb
	dmb

	/*
	 * Execute a WFI instruction and wait until the
	 * STANDBYWFI output is asserted to indicate that the
	 * CPU is in idle and low power state. CPU can specualatively
	 * prefetch the instructions so add NOPs after WFI. Sixteen
	 * NOPs as per Cortex-A9 pipeline.
	 */
	wfi					@ Wait For Interrupt
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	ldmfd	sp!, {pc}
ppa_zero_params_offset:
	.long	ppa_zero_params - .
ENDPROC(omap_do_wfi)

	.data
	.align	2
ppa_zero_params:
	.word		0

ppa_por_params:
	.word		1, 0
