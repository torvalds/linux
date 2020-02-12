// SPDX-License-Identifier: GPL-2.0-only
/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_irq.h>

#include <asm/time.h>

#include "pic32mzda.h"

static const struct of_device_id pic32_infra_match[] = {
	{ .compatible = "microchip,pic32mzda-infra", },
	{ },
};

#define DEFAULT_CORE_TIMER_INTERRUPT 0

static unsigned int pic32_xlate_core_timer_irq(void)
{
	static struct device_node *node;
	unsigned int irq;

	node = of_find_matching_node(NULL, pic32_infra_match);

	if (WARN_ON(!node))
		goto default_map;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq)
		goto default_map;

	return irq;

default_map:

	return irq_create_mapping(NULL, DEFAULT_CORE_TIMER_INTERRUPT);
}

unsigned int get_c0_compare_int(void)
{
	return pic32_xlate_core_timer_irq();
}

void __init plat_time_init(void)
{
	unsigned long rate = pic32_get_pbclk(7);

	of_clk_init(NULL);

	pr_info("CPU Clock: %ldMHz\n", rate / 1000000);
	mips_hpt_frequency = rate / 2;

	timer_probe();
}
