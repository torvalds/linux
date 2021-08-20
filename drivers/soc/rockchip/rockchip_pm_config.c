/*
 * Rockchip Generic power configuration support.
 *
 * Copyright (c) 2017 ROCKCHIP, Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/suspend.h>
#include <dt-bindings/input/input.h>
#include <../drivers/regulator/internal.h>

#define PM_INVALID_GPIO			0xffff
#define MAX_ON_OFF_REG_NUM		30
#define MAX_ON_OFF_REG_PROP_NAME_LEN	60

#if defined(CONFIG_NO_GKI)
enum rk_pm_state {
	RK_PM_MEM = 0,
	RK_PM_MEM_LITE,
	RK_PM_MEM_ULTRA,
	RK_PM_STATE_MAX
};

static struct rk_on_off_regulator_list {
	struct regulator_dev *on_reg_list[MAX_ON_OFF_REG_NUM];
	struct regulator_dev *off_reg_list[MAX_ON_OFF_REG_NUM];
} on_off_regs_list[RK_PM_STATE_MAX];
#endif

static const struct of_device_id pm_match_table[] = {
	{ .compatible = "rockchip,pm-px30",},
	{ .compatible = "rockchip,pm-rk1808",},
	{ .compatible = "rockchip,pm-rk322x",},
	{ .compatible = "rockchip,pm-rk3288",},
	{ .compatible = "rockchip,pm-rk3308",},
	{ .compatible = "rockchip,pm-rk3328",},
	{ .compatible = "rockchip,pm-rk3368",},
	{ .compatible = "rockchip,pm-rk3399",},
	{ .compatible = "rockchip,pm-rk3568",},
	{ .compatible = "rockchip,pm-rv1126",},
	{ },
};

#if defined(CONFIG_NO_GKI)
static void rockchip_pm_virt_pwroff_prepare(void)
{
	int error;

	regulator_suspend_prepare(PM_SUSPEND_MEM);

	error = suspend_disable_secondary_cpus();
	if (error) {
		pr_err("Disable nonboot cpus failed!\n");
		return;
	}

	sip_smc_set_suspend_mode(VIRTUAL_POWEROFF, 0, 1);
	sip_smc_virtual_poweroff();
}

static int parse_on_off_regulator(struct device_node *node, enum rk_pm_state state)
{
	char on_prop_name[MAX_ON_OFF_REG_PROP_NAME_LEN] = {0};
	char off_prop_name[MAX_ON_OFF_REG_PROP_NAME_LEN] = {0};
	int i, j;
	struct device_node *dn;
	struct regulator_dev *reg;
	struct regulator_dev **on_list;
	struct regulator_dev **off_list;

	switch (state) {
	case RK_PM_MEM:
		strncpy(on_prop_name, "rockchip,regulator-on-in-mem",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
		strncpy(off_prop_name, "rockchip,regulator-off-in-mem",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
	break;

	case RK_PM_MEM_LITE:
		strncpy(on_prop_name, "rockchip,regulator-on-in-mem-lite",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
		strncpy(off_prop_name, "rockchip,regulator-off-in-mem-lite",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
	break;

	case RK_PM_MEM_ULTRA:
		strncpy(on_prop_name, "rockchip,regulator-on-in-mem-ultra",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
		strncpy(off_prop_name, "rockchip,regulator-off-in-mem-ultra",
			MAX_ON_OFF_REG_PROP_NAME_LEN);
	break;

	default:
		return 0;
	}

	on_list = on_off_regs_list[state].on_reg_list;
	off_list = on_off_regs_list[state].off_reg_list;

	if (of_find_property(node, on_prop_name, NULL)) {
		for (i = 0, j = 0;
		     (dn = of_parse_phandle(node, on_prop_name, i));
		     i++) {
			reg = of_find_regulator_by_node(dn);
			if (reg == NULL) {
				pr_warn("failed to find regulator %s for %s\n",
					dn->name, on_prop_name);
			} else {
				pr_debug("%s on regulator=%s\n", __func__,
					 reg->desc->name);
				on_list[j++] = reg;
			}
			of_node_put(dn);

			if (j >= MAX_ON_OFF_REG_NUM)
				return 0;
		}
	}

	if (of_find_property(node, off_prop_name, NULL)) {
		for (i = 0, j = 0;
		     (dn = of_parse_phandle(node, off_prop_name, i));
		     i++) {
			reg = of_find_regulator_by_node(dn);
			if (reg == NULL) {
				pr_warn("failed to find regulator %s for %s\n",
					dn->name, off_prop_name);
			} else {
				pr_debug("%s off regulator=%s\n", __func__,
					 reg->desc->name);
				off_list[j++] = reg;
			}
			of_node_put(dn);

			if (j >= MAX_ON_OFF_REG_NUM)
				return 0;
		}
	}

	return 0;
}
#endif

static int pm_config_probe(struct platform_device *pdev)
{
	const struct of_device_id *match_id;
	struct device_node *node;
	u32 mode_config = 0;
	u32 wakeup_config = 0;
	u32 pwm_regulator_config = 0;
	int gpio_temp[10];
	u32 sleep_debug_en = 0;
	u32 apios_suspend = 0;
#if defined(CONFIG_NO_GKI)
	u32 virtual_poweroff_en = 0;
#endif
	enum of_gpio_flags flags;
	int i = 0;
	int length;

	match_id = of_match_node(pm_match_table, pdev->dev.of_node);
	if (!match_id)
		return -ENODEV;

	node = of_find_node_by_name(NULL, "rockchip-suspend");

	if (IS_ERR_OR_NULL(node)) {
		dev_err(&pdev->dev, "%s dev node err\n",  __func__);
		return -ENODEV;
	}

	if (of_property_read_u32_array(node,
				       "rockchip,sleep-mode-config",
				       &mode_config, 1))
		dev_warn(&pdev->dev, "not set sleep mode config\n");
	else
		sip_smc_set_suspend_mode(SUSPEND_MODE_CONFIG, mode_config, 0);

	if (of_property_read_u32_array(node,
				       "rockchip,wakeup-config",
				       &wakeup_config, 1))
		dev_warn(&pdev->dev, "not set wakeup-config\n");
	else
		sip_smc_set_suspend_mode(WKUP_SOURCE_CONFIG, wakeup_config, 0);

	if (of_property_read_u32_array(node,
				       "rockchip,pwm-regulator-config",
				       &pwm_regulator_config, 1))
		dev_warn(&pdev->dev, "not set pwm-regulator-config\n");
	else
		sip_smc_set_suspend_mode(PWM_REGULATOR_CONFIG,
					 pwm_regulator_config,
					 0);

	length = of_gpio_named_count(node, "rockchip,power-ctrl");

	if (length > 0 && length < 10) {
		for (i = 0; i < length; i++) {
			gpio_temp[i] = of_get_named_gpio_flags(node,
							     "rockchip,power-ctrl",
							     i,
							     &flags);
			if (!gpio_is_valid(gpio_temp[i]))
				break;
			sip_smc_set_suspend_mode(GPIO_POWER_CONFIG,
						 i,
						 gpio_temp[i]);
		}
	}
	sip_smc_set_suspend_mode(GPIO_POWER_CONFIG, i, PM_INVALID_GPIO);

	if (!of_property_read_u32_array(node,
					"rockchip,sleep-debug-en",
					&sleep_debug_en, 1))
		sip_smc_set_suspend_mode(SUSPEND_DEBUG_ENABLE,
					 sleep_debug_en,
					 0);

	if (!of_property_read_u32_array(node,
					"rockchip,apios-suspend",
					&apios_suspend, 1))
		sip_smc_set_suspend_mode(APIOS_SUSPEND_CONFIG,
					 apios_suspend,
					 0);

#if defined(CONFIG_NO_GKI)
	if (!of_property_read_u32_array(node,
					"rockchip,virtual-poweroff",
					&virtual_poweroff_en, 1) &&
	    virtual_poweroff_en)
		pm_power_off_prepare = rockchip_pm_virt_pwroff_prepare;

	for (i = RK_PM_MEM; i < RK_PM_STATE_MAX; i++)
		parse_on_off_regulator(node, i);
#endif

	return 0;
}

#if defined(CONFIG_NO_GKI)
static int pm_config_prepare(struct device *dev)
{
	int i;
	suspend_state_t suspend_state = mem_sleep_current;
	enum rk_pm_state state = suspend_state - PM_SUSPEND_MEM;
	struct regulator_dev **on_list;
	struct regulator_dev **off_list;

	sip_smc_set_suspend_mode(LINUX_PM_STATE,
				 suspend_state,
				 0);

	if (state >= RK_PM_STATE_MAX)
		return 0;

	on_list = on_off_regs_list[state].on_reg_list;
	off_list = on_off_regs_list[state].off_reg_list;

	for (i = 0; i < MAX_ON_OFF_REG_NUM && on_list[i]; i++)
		regulator_suspend_enable(on_list[i], PM_SUSPEND_MEM);

	for (i = 0; i < MAX_ON_OFF_REG_NUM && off_list[i]; i++)
		regulator_suspend_disable(off_list[i], PM_SUSPEND_MEM);

	return 0;
}

static const struct dev_pm_ops rockchip_pm_ops = {
	.prepare = pm_config_prepare,
};
#endif

static struct platform_driver pm_driver = {
	.probe = pm_config_probe,
	.driver = {
		.name = "rockchip-pm",
		.of_match_table = pm_match_table,
#if defined(CONFIG_NO_GKI)
		.pm = &rockchip_pm_ops,
#endif
	},
};

static int __init rockchip_pm_drv_register(void)
{
	return platform_driver_register(&pm_driver);
}
late_initcall_sync(rockchip_pm_drv_register);
MODULE_DESCRIPTION("Rockchip suspend mode config");
MODULE_LICENSE("GPL");
