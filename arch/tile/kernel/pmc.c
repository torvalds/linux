/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/interrupt.h>

#include <asm/processor.h>
#include <asm/pmc.h>

perf_irq_t perf_irq = NULL;
int handle_perf_interrupt(struct pt_regs *regs, int fault)
{
	int retval;

	if (!perf_irq)
		panic("Unexpected PERF_COUNT interrupt %d\n", fault);

	nmi_enter();
	retval = perf_irq(regs, fault);
	nmi_exit();
	return retval;
}

/* Reserve PMC hardware if it is available. */
perf_irq_t reserve_pmc_hardware(perf_irq_t new_perf_irq)
{
	return cmpxchg(&perf_irq, NULL, new_perf_irq);
}
EXPORT_SYMBOL(reserve_pmc_hardware);

/* Release PMC hardware. */
void release_pmc_hardware(void)
{
	perf_irq = NULL;
}
EXPORT_SYMBOL(release_pmc_hardware);


/*
 * Get current overflow status of each performance counter,
 * and auxiliary performance counter.
 */
unsigned long
pmc_get_overflow(void)
{
	unsigned long status;

	/*
	 * merge base+aux into a single vector
	 */
	status = __insn_mfspr(SPR_PERF_COUNT_STS);
	status |= __insn_mfspr(SPR_AUX_PERF_COUNT_STS) << TILE_BASE_COUNTERS;
	return status;
}

/*
 * Clear the status bit for the corresponding counter, if written
 * with a one.
 */
void
pmc_ack_overflow(unsigned long status)
{
	/*
	 * clear overflow status by writing ones
	 */
	__insn_mtspr(SPR_PERF_COUNT_STS, status);
	__insn_mtspr(SPR_AUX_PERF_COUNT_STS, status >> TILE_BASE_COUNTERS);
}

/*
 * The perf count interrupts are masked and unmasked explicitly,
 * and only here.  The normal irq_enable() does not enable them,
 * and irq_disable() does not disable them.  That lets these
 * routines drive the perf count interrupts orthogonally.
 *
 * We also mask the perf count interrupts on entry to the perf count
 * interrupt handler in assembly code, and by default unmask them
 * again (with interrupt critical section protection) just before
 * returning from the interrupt.  If the perf count handler returns
 * a non-zero error code, then we don't re-enable them before returning.
 *
 * For Pro, we rely on both interrupts being in the same word to update
 * them atomically so we never have one enabled and one disabled.
 */

#if CHIP_HAS_SPLIT_INTR_MASK()
# if INT_PERF_COUNT < 32 || INT_AUX_PERF_COUNT < 32
#  error Fix assumptions about which word PERF_COUNT interrupts are in
# endif
#endif

static inline unsigned long long pmc_mask(void)
{
	unsigned long long mask = 1ULL << INT_PERF_COUNT;
	mask |= 1ULL << INT_AUX_PERF_COUNT;
	return mask;
}

void unmask_pmc_interrupts(void)
{
	interrupt_mask_reset_mask(pmc_mask());
}

void mask_pmc_interrupts(void)
{
	interrupt_mask_set_mask(pmc_mask());
}
