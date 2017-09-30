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
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/suspend.h>
#include <dt-bindings/input/input.h>

#define PM_INVALID_GPIO	0xffff

static const struct of_device_id pm_match_table[] = {
	{ .compatible = "rockchip,pm-rk322x",},
	{ .compatible = "rockchip,pm-rk3288",},
	{ .compatible = "rockchip,pm-rk3368",},
	{ .compatible = "rockchip,pm-rk3399",},
	{ },
};

#define MAX_PWRKEY_NUMS		20
#define MAX_NUM_KEYS		60

struct rkxx_remote_key_table {
	int scancode;
	int keycode;
};

static int parse_ir_pwrkeys(unsigned int *pwrkey, int size, int *nkey)
{
	struct device_node *node;
	struct device_node *child_node;
	struct rkxx_remote_key_table key_table[MAX_NUM_KEYS];
	int i;
	int len = 0, nbuttons;
	int num = 0;
	u32 usercode, scancode;

	for_each_node_by_name(node, "pwm") {
		for_each_child_of_node(node, child_node) {
			if (of_property_read_u32(child_node,
						 "rockchip,usercode",
						 &usercode))
				break;

			if (of_get_property(child_node,
					    "rockchip,key_table",
					    &len) == NULL ||
			    len <= 0)
				break;

			len = len < sizeof(key_table) ? len : sizeof(key_table);
			len /= sizeof(u32);
			if (of_property_read_u32_array(child_node,
						       "rockchip,key_table",
						       (u32 *)key_table,
						       len))
				break;

			nbuttons = len / 2;
			for (i = 0; i < nbuttons && num < size; ++i) {
				if (key_table[i].keycode == KEY_POWER) {
					scancode = key_table[i].scancode;
					pr_debug("usercode=%x, key=%x\n",
						 usercode, scancode);
					pwrkey[num] = (usercode & 0xffff) << 16;
					pwrkey[num] |= (scancode & 0xff) << 8;
					++num;
				}
			}
		}
	}

	*nkey = num;

	return num ? 0 : -1;
}

static void rockchip_pm_virt_pwroff_prepare(void)
{
	int error;
	int i, nkey;
	u32 power_key[MAX_PWRKEY_NUMS];

	if ((parse_ir_pwrkeys(power_key, ARRAY_SIZE(power_key), &nkey))) {
		pr_err("Parse ir powerkey code failed!\n");
		return;
	}

	for (i = 0; i < nkey; ++i)
		sip_smc_set_suspend_mode(VIRTUAL_POWEROFF, 1, power_key[i]);

	regulator_suspend_prepare(PM_SUSPEND_MEM);

	error = disable_nonboot_cpus();
	if (error) {
		pr_err("Disable nonboot cpus failed!\n");
		return;
	}

	sip_smc_set_suspend_mode(VIRTUAL_POWEROFF, 0, 1);
	sip_smc_virtual_poweroff();
}

static int __init pm_config_init(struct platform_device *pdev)
{
	const struct of_device_id *match_id;
	struct device_node *node;
	u32 mode_config = 0;
	u32 wakeup_config = 0;
	u32 pwm_regulator_config = 0;
	int gpio_temp[10];
	u32 sleep_debug_en = 0;
	u32 apios_suspend = 0;
	u32 virtual_poweroff_en = 0;
	enum of_gpio_flags flags;
	int i = 0;
	int length;
	int pin_num;

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
			/* get the real pin number */
			pin_num = gpio_temp[i] - ARCH_GPIO_BASE;
			sip_smc_set_suspend_mode(GPIO_POWER_CONFIG,
						 i,
						 pin_num);
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

	if (!of_property_read_u32_array(node,
					"rockchip,virtual-poweroff",
					&virtual_poweroff_en, 1) &&
	    virtual_poweroff_en)
		pm_power_off_prepare = rockchip_pm_virt_pwroff_prepare;

	return 0;
}

static struct platform_driver pm_driver = {
	.driver = {
		.name = "rockchip-pm",
		.of_match_table = pm_match_table,
	},
};

static int __init rockchip_pm_drv_register(void)
{
	return platform_driver_probe(&pm_driver, pm_config_init);
}
subsys_initcall(rockchip_pm_drv_register);
