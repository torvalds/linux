/*
 * Copyright (c) 2016, Linaro Ltd.  All rights reserved.
 * Daniel Lezcano <daniel.lezcano@linaro.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/clockchips.h>

extern struct of_device_id __clkevt_of_table[];

static const struct of_device_id __clkevt_of_table_sentinel
	__used __section(__clkevt_of_table_end);

int __init clockevent_probe(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	of_init_fn_1_ret init_func;
	int ret, clockevents = 0;

	for_each_matching_node_and_match(np, __clkevt_of_table, &match) {
		if (!of_device_is_available(np))
			continue;

		init_func = match->data;

		ret = init_func(np);
		if (ret) {
			pr_warn("Failed to initialize '%s' (%d)\n",
				np->name, ret);
			continue;
		}

		clockevents++;
	}

	if (!clockevents) {
		pr_crit("%s: no matching clockevent found\n", __func__);
		return -ENODEV;
	}

	return 0;
}
