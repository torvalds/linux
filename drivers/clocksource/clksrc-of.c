/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/clocksource.h>

extern struct of_device_id __clksrc_of_table[];

static const struct of_device_id __clksrc_of_table_sentinel
	__used __section(__clksrc_of_table_end);

void __init clocksource_of_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	clocksource_of_init_fn init_func;

	for_each_matching_node_and_match(np, __clksrc_of_table, &match) {
		if (!of_device_is_available(np))
			continue;

		init_func = match->data;
		init_func(np);
		of_node_put(np);
	}
}
