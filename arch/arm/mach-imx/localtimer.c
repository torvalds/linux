/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/smp_twd.h>

/*
 * Setup the local clock events for a CPU.
 */
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "arm,smp-twd");
	if (!twd_base) {
		twd_base = of_iomap(np, 0);
		WARN_ON(!twd_base);
	}
	evt->irq = irq_of_parse_and_map(np, 0);
	twd_timer_setup(evt);

	return 0;
}
