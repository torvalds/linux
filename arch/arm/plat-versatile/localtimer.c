/*
 *  linux/arch/arm/plat-versatile/localtimer.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/smp_twd.h>
#include <asm/localtimer.h>
#include <mach/irqs.h>

const static struct of_device_id twd_of_match[] __initconst = {
       { .compatible = "arm,cortex-a9-twd-timer",      },
       { .compatible = "arm,cortex-a5-twd-timer",      },
       { .compatible = "arm,arm11mp-twd-timer",        },
       { },
};

/*
 * Setup the local clock events for a CPU.
 */
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
#if defined(CONFIG_OF)
	static int dt_node_probed;

	/* Look for TWD node only once */
	if (!dt_node_probed) {
		struct device_node *node = of_find_matching_node(NULL,
				twd_of_match);

		if (node)
			twd_base = of_iomap(node, 0);

		dt_node_probed = 1;
	}
#endif
	if (!twd_base)
		return -ENXIO;

	evt->irq = IRQ_LOCALTIMER;
	twd_timer_setup(evt);
	return 0;
}
