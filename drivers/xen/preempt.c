/*
 * Preemptible hypercalls
 *
 * Copyright (C) 2014 Citrix Systems R&D ltd.
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <xen/xen-ops.h>

#ifndef CONFIG_PREEMPT

/*
 * Some hypercalls issued by the toolstack can take many 10s of
 * seconds. Allow tasks running hypercalls via the privcmd driver to
 * be voluntarily preempted even if full kernel preemption is
 * disabled.
 *
 * Such preemptible hypercalls are bracketed by
 * xen_preemptible_hcall_begin() and xen_preemptible_hcall_end()
 * calls.
 */

DEFINE_PER_CPU(bool, xen_in_preemptible_hcall);
EXPORT_SYMBOL_GPL(xen_in_preemptible_hcall);

asmlinkage __visible void xen_maybe_preempt_hcall(void)
{
	if (unlikely(__this_cpu_read(xen_in_preemptible_hcall)
		     && need_resched())) {
		/*
		 * Clear flag as we may be rescheduled on a different
		 * cpu.
		 */
		__this_cpu_write(xen_in_preemptible_hcall, false);
		local_irq_enable();
		cond_resched();
		local_irq_disable();
		__this_cpu_write(xen_in_preemptible_hcall, true);
	}
}
#endif /* CONFIG_PREEMPT */
