/*
 *
 * Copyright (C) 2015 Numascale AS. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clockchips.h>

#include <asm/irq.h>
#include <asm/numachip/numachip.h>
#include <asm/numachip/numachip_csr.h>

static DEFINE_PER_CPU(struct clock_event_device, numachip2_ced);

static cycles_t numachip2_timer_read(struct clocksource *cs)
{
	return numachip2_read64_lcsr(NUMACHIP2_TIMER_NOW);
}

static struct clocksource numachip2_clocksource = {
	.name            = "numachip2",
	.rating          = 295,
	.read            = numachip2_timer_read,
	.mask            = CLOCKSOURCE_MASK(64),
	.flags           = CLOCK_SOURCE_IS_CONTINUOUS,
	.mult            = 1,
	.shift           = 0,
};

static int numachip2_set_next_event(unsigned long delta, struct clock_event_device *ced)
{
	numachip2_write64_lcsr(NUMACHIP2_TIMER_DEADLINE + numachip2_timer(),
		delta);
	return 0;
}

static const struct clock_event_device numachip2_clockevent __initconst = {
	.name            = "numachip2",
	.rating          = 400,
	.set_next_event  = numachip2_set_next_event,
	.features        = CLOCK_EVT_FEAT_ONESHOT,
	.mult            = 1,
	.shift           = 0,
	.min_delta_ns    = 1250,
	.min_delta_ticks = 1250,
	.max_delta_ns    = LONG_MAX,
	.max_delta_ticks = LONG_MAX,
};

static void numachip_timer_interrupt(void)
{
	struct clock_event_device *ced = this_cpu_ptr(&numachip2_ced);

	ced->event_handler(ced);
}

static __init void numachip_timer_each(struct work_struct *work)
{
	unsigned local_apicid = __this_cpu_read(x86_cpu_to_apicid) & 0xff;
	struct clock_event_device *ced = this_cpu_ptr(&numachip2_ced);

	/* Setup IPI vector to local core and relative timing mode */
	numachip2_write64_lcsr(NUMACHIP2_TIMER_INT + numachip2_timer(),
		(3 << 22) | (X86_PLATFORM_IPI_VECTOR << 14) |
		(local_apicid << 6));

	*ced = numachip2_clockevent;
	ced->cpumask = cpumask_of(smp_processor_id());
	clockevents_register_device(ced);
}

static int __init numachip_timer_init(void)
{
	if (numachip_system != 2)
		return -ENODEV;

	/* Reset timer */
	numachip2_write64_lcsr(NUMACHIP2_TIMER_RESET, 0);
	clocksource_register_hz(&numachip2_clocksource, NSEC_PER_SEC);

	/* Setup per-cpu clockevents */
	x86_platform_ipi_callback = numachip_timer_interrupt;
	schedule_on_each_cpu(&numachip_timer_each);

	return 0;
}

arch_initcall(numachip_timer_init);
