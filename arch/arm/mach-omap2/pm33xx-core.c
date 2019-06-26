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
#include <linux/clk.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/wkup_m3_ipc.h>
#include <linux/of.h>
#include <linux/rtc.h>

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
static struct omap_hwmod *rtc_oh;

static int am43xx_map_scu(void)
{
	scu_base = ioremap(scu_a9_get_base(), SZ_256);

	if (!scu_base)
		return -ENOMEM;

	return 0;
}

static int am33xx_check_off_mode_enable(void)
{
	if (enable_off_mode)
		pr_warn("WARNING: This platform does not support off-mode, entering DeepSleep suspend.\n");

	/* off mode not supported on am335x so return 0 always */
	return 0;
}

static int am43xx_check_off_mode_enable(void)
{
	/*
	 * Check for am437x-gp-evm which has the right Hardware design to
	 * support this mode reliably.
	 */
	if (of_machine_is_compatible("ti,am437x-gp-evm") && enable_off_mode)
		return enable_off_mode;
	else if (enable_off_mode)
		pr_warn("WARNING: This platform does not support off-mode, entering DeepSleep suspend.\n");

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
	if (!cefuse_pwrdm)
		pr_err("PM: Failed to get cefuse_pwrdm\n");
	else if (omap_type() != OMAP2_DEVICE_TYPE_GP)
		pr_info("PM: Leaving EFUSE power domain active\n");
	else
		omap_set_pwrdm_state(cefuse_pwrdm, PWRDM_POWER_OFF);

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

static int am33xx_suspend(unsigned int state, int (*fn)(unsigned long),
			  unsigned long args)
{
	int ret = 0;

	amx3_pre_suspend_common();
	ret = cpu_suspend(args, fn);
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

static int am43xx_suspend(unsigned int state, int (*fn)(unsigned long),
			  unsigned long args)
{
	int ret = 0;

	amx3_pre_suspend_common();
	scu_power_mode(scu_base, SCU_PM_POWEROFF);
	ret = cpu_suspend(args, fn);
	scu_power_mode(scu_base, SCU_PM_NORMAL);

	if (!am43xx_check_off_mode_enable())
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

void __iomem *am43xx_get_rtc_base_addr(void)
{
	rtc_oh = omap_hwmod_lookup("rtc");

	return omap_hwmod_get_mpu_rt_va(rtc_oh);
}

static void am43xx_save_context(void)
{
}

static void am33xx_save_context(void)
{
	omap_intc_save_context();
}

static void am33xx_restore_context(void)
{
	omap_intc_restore_context();
}

static void am43xx_restore_context(void)
{
	/*
	 * HACK: restore dpll_per_clkdcoldo register contents, to avoid
	 * breaking suspend-resume
	 */
	writel_relaxed(0x0, AM33XX_L4_WK_IO_ADDRESS(0x44df2e14));
}

static void am43xx_prepare_rtc_suspend(void)
{
	omap_hwmod_enable(rtc_oh);
}

static void am43xx_prepare_rtc_resume(void)
{
	omap_hwmod_idle(rtc_oh);
}

static struct am33xx_pm_platform_data am33xx_ops = {
	.init = am33xx_suspend_init,
	.soc_suspend = am33xx_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
	.save_context = am33xx_save_context,
	.restore_context = am33xx_restore_context,
	.prepare_rtc_suspend = am43xx_prepare_rtc_suspend,
	.prepare_rtc_resume = am43xx_prepare_rtc_resume,
	.check_off_mode_enable = am33xx_check_off_mode_enable,
	.get_rtc_base_addr = am43xx_get_rtc_base_addr,
};

static struct am33xx_pm_platform_data am43xx_ops = {
	.init = am43xx_suspend_init,
	.soc_suspend = am43xx_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
	.save_context = am43xx_save_context,
	.restore_context = am43xx_restore_context,
	.prepare_rtc_suspend = am43xx_prepare_rtc_suspend,
	.prepare_rtc_resume = am43xx_prepare_rtc_resume,
	.check_off_mode_enable = am43xx_check_off_mode_enable,
	.get_rtc_base_addr = am43xx_get_rtc_base_addr,
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

int __init amx3_common_pm_init(void)
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

	return 0;
}
