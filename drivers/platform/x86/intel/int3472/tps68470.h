/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TI TPS68470 PMIC platform data definition.
 *
 * Copyright (c) 2021 Red Hat Inc.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#ifndef _INTEL_SKL_INT3472_TPS68470_H
#define _INTEL_SKL_INT3472_TPS68470_H

struct gpiod_lookup_table;

struct tps68470_clk_consumer;
struct tps68470_regulator_platform_data;

struct int3472_tps68470_board_data {
	const char *dev_name;
	const struct tps68470_regulator_platform_data *tps68470_regulator_pdata;
	const struct software_node *tps68470_gpio_swnode;
	/*
	 * Optional static clock consumers, used in place of the ACPI _DEP
	 * traversal on platforms where a sensor's _DEP omits the INT3472.
	 */
	unsigned int n_clk_consumers;
	const struct tps68470_clk_consumer *clk_consumers;
	unsigned int n_gpiod_lookups;
	struct gpiod_lookup_table *tps68470_gpio_lookup_tables[];
};

const struct int3472_tps68470_board_data *int3472_tps68470_get_board_data(const char *dev_name);

#endif
