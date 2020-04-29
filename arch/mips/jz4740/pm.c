// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *	JZ4740 SoC power management support
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/suspend.h>

static int jz4740_pm_enter(suspend_state_t state)
{
	__asm__(".set\tmips3\n\t"
		"wait\n\t"
		".set\tmips0");



	return 0;
}

static const struct platform_suspend_ops jz4740_pm_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= jz4740_pm_enter,
};

static int __init jz4740_pm_init(void)
{
	suspend_set_ops(&jz4740_pm_ops);
	return 0;

}
late_initcall(jz4740_pm_init);
