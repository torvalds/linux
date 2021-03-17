// SPDX-License-Identifier: GPL-2.0
/*
 * AM33XX Arch Power Management Routines
 *
 * Copyright (C) 2016-2018 Texas Instruments Incorporated - https://www.ti.com/
 *	Dave Gerlach
 */

#include <linux/cpuidle.h>
#include <linux/platform_data/pm33xx.h>
#include <asm/cpuidle.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/cpu.h>
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
#include "pm.h"
#include "powerdomain.h"
#include "prm33xx.h"
#include "soc.h"
#include "sram.h"
#include "omap-secure.h"

static struct powerdomain *cefuse_pwrdm, *gfx_pwrdm, *per_pwrdm, *mpu_pwrdm;
static struct clockdomain *gfx_l4ls_clkdm;
static void __iomem *scu_base;

static int (*idle_fn)(u32 wfi_flags);

struct amx3_idle_state {
	int wfi_flags;
};

static struct amx3_idle_state *idle_states;

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

static int amx3_common_init(int (*idle)(u32 wfi_flags))
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

	idle_fn = idle;

	return 0;
}

static int am33xx_suspend_init(int (*idle)(u32 wfi_flags))
{
	int ret;

	gfx_l4ls_clkdm = clkdm_lookup("gfx_l4ls_gfx_clkdm");

	if (!gfx_l4ls_clkdm) {
		pr_err("PM: Cannot lookup gfx_l4ls_clkdm clockdomains\n");
		return -ENODEV;
	}

	ret = amx3_common_init(idle);

	return ret;
}

static int am43xx_suspend_init(int (*idle)(u32 wfi_flags))
{
	int ret = 0;

	ret = am43xx_map_scu();
	if (ret) {
		pr_err("PM: Could not ioremap SCU\n");
		return ret;
	}

	ret = amx3_common_init(idle);

	return ret;
}

static int amx3_suspend_deinit(void)
{
	idle_fn = NULL;
	return 0;
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

	/* Suspend secure side on HS devices */
	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		if (optee_available)
			omap_smccc_smc(AM43xx_PPA_SVC_PM_SUSPEND, 0);
		else
			omap_secure_dispatcher(AM43xx_PPA_SVC_PM_SUSPEND,
					       FLAG_START_CRITICAL,
					       0, 0, 0, 0, 0);
	}

	amx3_pre_suspend_common();
	scu_power_mode(scu_base, SCU_PM_POWEROFF);
	ret = cpu_suspend(args, fn);
	scu_power_mode(scu_base, SCU_PM_NORMAL);

	if (!am43xx_check_off_mode_enable())
		amx3_post_suspend_common();

	/*
	 * Resume secure side on HS devices.
	 *
	 * Note that even on systems with OP-TEE available this resume call is
	 * issued to the ROM. This is because upon waking from suspend the ROM
	 * is restored as the secure monitor. On systems with OP-TEE ROM will
	 * restore OP-TEE during this call.
	 */
	if (omap_type() != OMAP2_DEVICE_TYPE_GP)
		omap_secure_dispatcher(AM43xx_PPA_SVC_PM_RESUME,
				       FLAG_START_CRITICAL,
				       0, 0, 0, 0, 0);

	return ret;
}

static int am33xx_cpu_suspend(int (*fn)(unsigned long), unsigned long args)
{
	int ret = 0;

	if (omap_irq_pending() || need_resched())
		return ret;

	ret = cpu_suspend(args, fn);

	return ret;
}

static int am43xx_cpu_suspend(int (*fn)(unsigned long), unsigned long args)
{
	int ret = 0;

	if (!scu_base)
		return 0;

	scu_power_mode(scu_base, SCU_PM_DORMANT);
	ret = cpu_suspend(args, fn);
	scu_power_mode(scu_base, SCU_PM_NORMAL);

	return ret;
}

static void amx3_begin_suspend(void)
{
	cpu_idle_poll_ctrl(true);
}

static void amx3_finish_suspend(void)
{
	cpu_idle_poll_ctrl(false);
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

static struct am33xx_pm_platform_data am33xx_ops = {
	.init = am33xx_suspend_init,
	.deinit = amx3_suspend_deinit,
	.soc_suspend = am33xx_suspend,
	.cpu_suspend = am33xx_cpu_suspend,
	.begin_suspend = amx3_begin_suspend,
	.finish_suspend = amx3_finish_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
	.save_context = am33xx_save_context,
	.restore_context = am33xx_restore_context,
	.check_off_mode_enable = am33xx_check_off_mode_enable,
};

static struct am33xx_pm_platform_data am43xx_ops = {
	.init = am43xx_suspend_init,
	.deinit = amx3_suspend_deinit,
	.soc_suspend = am43xx_suspend,
	.cpu_suspend = am43xx_cpu_suspend,
	.begin_suspend = amx3_begin_suspend,
	.finish_suspend = amx3_finish_suspend,
	.get_sram_addrs = amx3_get_sram_addrs,
	.save_context = am43xx_save_context,
	.restore_context = am43xx_restore_context,
	.check_off_mode_enable = am43xx_check_off_mode_enable,
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

static int __init amx3_idle_init(struct device_node *cpu_node, int cpu)
{
	struct device_node *state_node;
	struct amx3_idle_state states[CPUIDLE_STATE_MAX];
	int i;
	int state_count = 1;

	for (i = 0; ; i++) {
		state_node = of_parse_phandle(cpu_node, "cpu-idle-states", i);
		if (!state_node)
			break;

		if (!of_device_is_available(state_node))
			continue;

		if (i == CPUIDLE_STATE_MAX) {
			pr_warn("%s: cpuidle states reached max possible\n",
				__func__);
			break;
		}

		states[state_count].wfi_flags = 0;

		if (of_property_read_bool(state_node, "ti,idle-wkup-m3"))
			states[state_count].wfi_flags |= WFI_FLAG_WAKE_M3 |
							 WFI_FLAG_FLUSH_CACHE;

		state_count++;
	}

	idle_states = kcalloc(state_count, sizeof(*idle_states), GFP_KERNEL);
	if (!idle_states)
		return -ENOMEM;

	for (i = 1; i < state_count; i++)
		idle_states[i].wfi_flags = states[i].wfi_flags;

	return 0;
}

static int amx3_idle_enter(unsigned long index)
{
	struct amx3_idle_state *idle_state = &idle_states[index];

	if (!idle_state)
		return -EINVAL;

	if (idle_fn)
		idle_fn(idle_state->wfi_flags);

	return 0;
}

static struct cpuidle_ops amx3_cpuidle_ops __initdata = {
	.init = amx3_idle_init,
	.suspend = amx3_idle_enter,
};

CPUIDLE_METHOD_OF_DECLARE(pm33xx_idle, "ti,am3352", &amx3_cpuidle_ops);
CPUIDLE_METHOD_OF_DECLARE(pm43xx_idle, "ti,am4372", &amx3_cpuidle_ops);
