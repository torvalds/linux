// SPDX-License-Identifier: GPL-2.0-only

#include <linux/init.h>
#include <linux/pm.h>
#include <asm/sbi.h>

static void sbi_power_off(void)
{
	sbi_shutdown();
}

static int __init sbi_init(void)
{
	pm_power_off = sbi_power_off;
	return 0;
}
early_initcall(sbi_init);
