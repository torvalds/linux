// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * loongson-specific suspend support
 *
 *  Copyright (C) 2009 Lemote Inc.
 *  Author: Wu Zhangjin <wuzhangjin@gmail.com>
 */
#include <linux/suspend.h>
#include <linux/pm.h>

#include <asm/mipsregs.h>

#include <loongson.h>

asmlinkage void loongson_lefi_sleep(unsigned long sleep_addr);

static int lefi_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		pm_set_suspend_via_firmware();
		loongson_lefi_sleep(loongson_sysconf.suspend_addr);
		pm_set_resume_via_firmware();
		return 0;
	default:
		return -EINVAL;
	}
}

static int lefi_pm_valid_state(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		return !!loongson_sysconf.suspend_addr;
	default:
		return 0;
	}
}

static const struct platform_suspend_ops lefi_pm_ops = {
	.valid	= lefi_pm_valid_state,
	.enter	= lefi_pm_enter,
};

static int __init loongson_pm_init(void)
{
	if (loongson_sysconf.fw_interface == LOONGSON_LEFI)
		suspend_set_ops(&lefi_pm_ops);

	return 0;
}
arch_initcall(loongson_pm_init);
