/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Based on localtimer.c, Copyright (C) 2002 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/of.h>
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
