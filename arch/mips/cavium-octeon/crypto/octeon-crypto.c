/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2012 Cavium Networks
 */

#include <asm/cop2.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/sched/task_stack.h>

#include "octeon-crypto.h"

/**
 * Enable access to Octeon's COP2 crypto hardware for kernel use. Wrap any
 * crypto operations in calls to octeon_crypto_enable/disable in order to make
 * sure the state of COP2 isn't corrupted if userspace is also performing
 * hardware crypto operations. Allocate the state parameter on the stack.
 * Returns with preemption disabled.
 *
 * @state: Pointer to state structure to store current COP2 state in.
 *
 * Returns: Flags to be passed to octeon_crypto_disable()
 */
unsigned long octeon_crypto_enable(struct octeon_cop2_state *state)
{
	int status;
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);
	status = read_c0_status();
	write_c0_status(status | ST0_CU2);
	if (KSTK_STATUS(current) & ST0_CU2) {
		octeon_cop2_save(&(current->thread.cp2));
		KSTK_STATUS(current) &= ~ST0_CU2;
		status &= ~ST0_CU2;
	} else if (status & ST0_CU2) {
		octeon_cop2_save(state);
	}
	local_irq_restore(flags);
	return status & ST0_CU2;
}
EXPORT_SYMBOL_GPL(octeon_crypto_enable);

/**
 * Disable access to Octeon's COP2 crypto hardware in the kernel. This must be
 * called after an octeon_crypto_enable() before any context switch or return to
 * userspace.
 *
 * @state:	Pointer to COP2 state to restore
 * @flags:	Return value from octeon_crypto_enable()
 */
void octeon_crypto_disable(struct octeon_cop2_state *state,
			   unsigned long crypto_flags)
{
	unsigned long flags;

	local_irq_save(flags);
	if (crypto_flags & ST0_CU2)
		octeon_cop2_restore(state);
	else
		write_c0_status(read_c0_status() & ~ST0_CU2);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL_GPL(octeon_crypto_disable);
