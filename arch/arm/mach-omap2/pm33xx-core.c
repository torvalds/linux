// SPDX-License-Identifier: GPL-2.0
/*
 * AM33XX Arch Power Management Routines
 *
 * Copyright (C) 2016-2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Dave Gerlach
 */

#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <linux/errno.h>
#include <linux/platform_data/pm33xx.h>

#include "cm33xx.h"
#include "common.h"
#include "control.h"
#include "clockdomain.h"
#include "iomap.h"
#include "omap_hwmod.h"
#include "pm.h"
#include "powerdomain.h"
#include "prm33xx.h"
#include "soc.h"
#include "sram.h"

static struct powerdomain *cefuse_pwrdm, *gfx_pwrdm, *per_pwrdm, *mpu_pwrdm;
static struct clockdomain *gfx_l4ls_clkdm;
static void __iomem *scu_base;

static int __init am43xx_map_scu(void)
{
	scu_base = ioremap(scu_a9_get_base(), SZ_256);

	if (!scu_base)
		return -ENOMEM;

	return 0;
}

static int amx3_common_init(void)
{
	gfx_pwrdm = pwrdm_lookup("gfx_pwrdm");
	per_pwrdm = pwrdm_lookup("per_pwrdm");
	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");

	if ((!gfx_pwrdm) || (!per_pwrdm) || (!mpu_pwrdm))
		return -ENODEV;

	(void)clkdm_for_each(omap_pm_clkdms_setup, NULL);

	/* CEFUSE domain can be turned off post bootup */
	cefuse_pwrdm = pwrdm_lookup("cefuse_pwrdm");
	if (cefuse_pwrdm)
		omap_set_pwrdm_state(cefuse_pwrdm, PWRDM_POWER_OFF);
	else
		pr_err("PM: Failed to get cefuse_pwrdm\n");

	return 0;
}

static int am33xx_suspend_init(void)
{
	int ret;

	gfx_l4ls_clkdm = clkdm_lookup("gfx_l4ls_gfx_clkdm");

	if (!gfx_l4ls_clkdm) {
		pr_err("PM: Cannot lookup gfx_l4ls_clkdm clockdomains\n");
		return -ENODEV;
	}

	ret = amx3_common_init();

	return ret;
}

static int am43xx_suspend_init(void)
{
	int ret = 0;

	ret = am43xx_map_scu();
	if (ret) {
		pr_err("PM: Could not ioremap SCU\n");
		return ret;
	}

	ret = amx3_common_init();

	return ret;
}

static void amx3_pre_suspend_common(void)
{
	omap_set_pwrdm_state(gfx_pwrdm, PWRDM_POWER_OFF);
}

static void amx3_post_suspend_common(void)
{
	int status;
	/*
	 * Because gfx_pwrdm is the only one under MPU control,
	 * comment on transition status
	 */
	status = pwrdm_read_pwrst(gfx_pwrdm);
	if (status != PWRDM_POWER_OFF)
		pr_err("PM: GFX domain did not transition: %x\n", status);
}

static int am33xx_suspend(unsigned int state, int (*fn)(unsigned long))
{
	int ret = 0;

	amx3_pre_suspend_common();
	ret = cpu_suspend(0, fn);
	amx3_post_suspend_common();

	/*
	 * BUG: GFX_L4LS clock domain needs to be woken up to
	 * ensure thet L4LS clock domain does not get stuck in
	 * transition. If that happens L3 module does not get
	 * disabled, thereby leading to PER power domain
	 * transition failing
	 */

	clkdm_wakeup(gfx_l4ls_clkdm);
	clkdm_sleep(gfx_l4ls_clkdm);

	return ret;
}

static int am43xx_suspend(unsigned int state, int (*fn)(unsigned long))
{
	int ret = 0;

	amx3_pre_suspend_common();
	scu_power_mode(scu_base, SCU_PM_POWEROFF);
	ret = cpu_suspend(0, fn);
	scu_power_mode(scu_base, SCU_PM_NORMAL);
	amx3_post_suspend_common();

	return ret;
}

static struct am33xx_pm_sram_addr *amx3_get_sram_addrs(void)
{
	if (soc_is_am33xx())
		return &am33xx_pm_sram;
	else if (soc_is_am437x())
		return &am43xx_pm_sram;
	else
		return NULL;
}

static struct am33xx_pm_platform_data am33xx_ops = {
	.init = am33xx_suspend_init,
	.soc_suspend = am33xx_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
};

static struct am33xx_pm_platform_data am43xx_ops = {
	.init = am43xx_suspend_init,
	.soc_suspend = am43xx_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
};

static struct am33xx_pm_platform_data *am33xx_pm_get_pdata(void)
{
	if (soc_is_am33xx())
		return &am33xx_ops;
	else if (soc_is_am437x())
		return &am43xx_ops;
	else
		return NULL;
}

void __init amx3_common_pm_init(void)
{
	struct am33xx_pm_platform_data *pdata;
	struct platform_device_info devinfo;

	pdata = am33xx_pm_get_pdata();

	memset(&devinfo, 0, sizeof(devinfo));
	devinfo.name = "pm33xx";
	devinfo.data = pdata;
	devinfo.size_data = sizeof(*pdata);
	devinfo.id = -1;
	platform_device_register_full(&devinfo);
}
