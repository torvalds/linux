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
#include <linux/rockchip/rockchip_pm_config.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/suspend.h>
#include <dt-bindings/input/input.h>
#include <../drivers/regulator/internal.h>

#define PM_INVALID_GPIO			0xffff
#define MAX_ON_OFF_REG_NUM		30
#define MAX_ON_OFF_REG_PROP_NAME_LEN	60
#define MAX_CONFIG_PROP_NAME_LEN	60

#define RK_ATAG_MCU_SLP_CORE		0x526b0001
#define RK_ATAG_MCU_SLP_MAX		0x526b00ff
#define RK_ATAG_NONE			0x00000000

enum rk_pm_state {
	RK_PM_MEM = 0,
	RK_PM_MEM_LITE,
	RK_PM_MEM_ULTRA,
	RK_PM_STATE_MAX
};

#ifndef MODULE
static const char * const pm_state_str[RK_PM_STATE_MAX] = {
	[RK_PM_MEM] = "mem",
	[RK_PM_MEM_LITE] = "mem-lite",
	[RK_PM_MEM_ULTRA] = "mem-ultra",
};

static struct rk_on_off_regulator_list {
	struct regulator_dev *on_reg_list[MAX_ON_OFF_REG_NUM];
	struct regulator_dev *off_reg_list[MAX_ON_OFF_REG_NUM];
} on_off_regs_list[RK_PM_STATE_MAX];
#endif

/* rk_tag related defines */
#define sleep_tag_next(t)	\
	((struct rk_sleep_tag *)((__u32 *)(t) + (t)->hdr.size))

struct rk_tag_header {
	u32 size;
	u32 tag;
};

struct rk_sleep_tag {
	struct rk_tag_header hdr;
	u32 params[];
};

struct rk_mcu_sleep_core_tag {
	struct rk_tag_header hdr;
	u32 total_size;
	u32 reserve[13];
};

struct rk_mcu_sleep_tags {
	struct rk_mcu_sleep_core_tag core;
	struct rk_sleep_tag slp_tags;
};

struct rk_sleep_config *sleep_config;

static const struct of_device_id pm_match_table[] = {
	{ .compatible = "rockchip,pm-config",},
	{ .compatible = "rockchip,pm-px30",},
	{ .compatible = "rockchip,pm-rk1808",},
	{ .compatible = "rockchip,pm-rk322x",},
	{ .compatible = "rockchip,pm-rk3288",},
	{ .compatible = "rockchip,pm-rk3308",},
	{ .compatible = "rockchip,pm-rk3328",},
	{ .compatible = "rockchip,pm-rk3368",},
	{ .compatible = "rockchip,pm-rk3399",},
	{ .compatible = "rockchip,pm-rk3528",},
	{ .compatible = "rockchip,pm-rk3562",},
	{ .compatible = "rockchip,pm-rk3568",},
	{ .compatible = "rockchip,pm-rk3588",},
	{ .compatible = "rockchip,pm-rv1126",},
	{ },
};

#ifndef MODULE
enum {
	RK_PM_VIRT_PWROFF_EN = 0,
	RK_PM_VIRT_PWROFF_IRQ_CFG = 1,
	RK_PM_VIRT_PWROFF_MAX,
};

static u32 *virtual_pwroff_irqs;

static void rockchip_pm_virt_pwroff_prepare(void)
{
	int error, i;

	pm_wakeup_clear(0);

	regulator_suspend_prepare(PM_SUSPEND_MEM);

	error = suspend_disable_secondary_cpus();
	if (error) {
		pr_err("Disable nonboot cpus failed!\n");
		return;
	}

	sip_smc_set_suspend_mode(VIRTUAL_POWEROFF, RK_PM_VIRT_PWROFF_EN, 1);

	if (virtual_pwroff_irqs) {
		for (i = 0; virtual_pwroff_irqs[i]; i++) {
			error = sip_smc_set_suspend_mode(VIRTUAL_POWEROFF,
							 RK_PM_VIRT_PWROFF_IRQ_CFG,
							 virtual_pwroff_irqs[i]);
			if (error) {
				pr_err("%s: config virtual_pwroff_irqs[%d] error, overflow or update trust!\n",
				       __func__, i);
				break;
			}
		}
	}

	sip_smc_virtual_poweroff();
}

static int parse_virtual_pwroff_config(struct device_node *node)
{
	int ret = 0, cnt;
	u32 virtual_poweroff_en = 0;

	if (!of_property_read_u32_array(node,
					"rockchip,virtual-poweroff",
					&virtual_poweroff_en, 1) &&
	    virtual_poweroff_en)
		pm_power_off_prepare = rockchip_pm_virt_pwroff_prepare;

	if (!virtual_poweroff_en)
		return 0;

	cnt = of_property_count_u32_elems(node, "rockchip,virtual-poweroff-irqs");
	if (cnt > 0) {
		/* 0 as the last element of virtual_pwroff_irqs */
		virtual_pwroff_irqs = kzalloc((cnt + 1) * sizeof(u32), GFP_KERNEL);
		if (!virtual_pwroff_irqs) {
			ret = -ENOMEM;
			goto out;
		}

		ret = of_property_read_u32_array(node, "rockchip,virtual-poweroff-irqs",
						 virtual_pwroff_irqs, cnt);
		if (ret) {
			pr_err("%s: get rockchip,virtual-poweroff-irqs error\n",
			       __func__);
			goto out;
		}
	}

out:
	return ret;
}

static int parse_sleep_config(struct device_node *node, enum rk_pm_state state)
{
	char mode_prop_name[MAX_CONFIG_PROP_NAME_LEN];
	char wkup_prop_name[MAX_CONFIG_PROP_NAME_LEN];
	struct rk_sleep_config *config;

	if (state == RK_PM_MEM || state >= RK_PM_STATE_MAX)
		return -EINVAL;

	snprintf(mode_prop_name, sizeof(mode_prop_name),
		 "sleep-mode-config-%s", pm_state_str[state]);
	snprintf(wkup_prop_name, sizeof(wkup_prop_name),
		 "wakeup-config-%s", pm_state_str[state]);

	config = &sleep_config[state];

	if (of_property_read_u32_array(node,
				       mode_prop_name,
				       &config->mode_config, 1))
		pr_info("%s not set sleep-mode-config for %s\n",
			node->name, pm_state_str[state]);

	if (of_property_read_u32_array(node,
				       wkup_prop_name,
				       &config->wakeup_config, 1))
		pr_info("%s not set wakeup-config for %s\n",
			node->name, pm_state_str[state]);

	return 0;
}

static int parse_regulator_list(struct device_node *node,
				char *prop_name,
				struct regulator_dev **out_list)
{
	struct device_node *dn;
	struct regulator_dev *reg;
	int i, j;

	if (of_find_property(node, prop_name, NULL)) {
		for (i = 0, j = 0;
		     (dn = of_parse_phandle(node, prop_name, i)) && j < MAX_ON_OFF_REG_NUM;
		     i++) {
			reg = of_find_regulator_by_node(dn);
			if (reg == NULL) {
				pr_warn("failed to find regulator %s for %s\n",
					dn->name, prop_name);
			} else {
				pr_debug("%s %s regulator=%s\n", __func__,
					 prop_name,
					 reg->desc->name);
				out_list[j++] = reg;
			}
			of_node_put(dn);
		}
	}

	return 0;
}

static int parse_on_off_regulator(struct device_node *node, enum rk_pm_state state)
{
	char on_prop_name[MAX_ON_OFF_REG_PROP_NAME_LEN];
	char off_prop_name[MAX_ON_OFF_REG_PROP_NAME_LEN];

	if (state >= RK_PM_STATE_MAX)
		return -EINVAL;

	snprintf(on_prop_name, sizeof(on_prop_name),
		 "rockchip,regulator-on-in-%s", pm_state_str[state]);
	snprintf(off_prop_name, sizeof(off_prop_name),
		 "rockchip,regulator-off-in-%s", pm_state_str[state]);

	parse_regulator_list(node, on_prop_name, on_off_regs_list[state].on_reg_list);
	parse_regulator_list(node, off_prop_name, on_off_regs_list[state].off_reg_list);

	return 0;
}

const struct rk_sleep_config *rockchip_get_cur_sleep_config(void)
{
	suspend_state_t suspend_state = mem_sleep_current;
	enum rk_pm_state state = suspend_state - PM_SUSPEND_MEM;

	if (state >= RK_PM_STATE_MAX)
		return NULL;

	return &sleep_config[state];
}
EXPORT_SYMBOL_GPL(rockchip_get_cur_sleep_config);
#endif

static int parse_mcu_sleep_config(struct device_node *node)
{
	int ret, cnt;
	struct arm_smccc_res res;
	struct device_node *mcu_sleep_node;
	struct device_node *child;
	struct rk_mcu_sleep_tags *config;
	struct rk_sleep_tag *slp_tag;
	char *end;

	mcu_sleep_node = of_find_node_by_name(node, "rockchip-mcu-sleep-cfg");
	if (IS_ERR_OR_NULL(mcu_sleep_node)) {
		ret = -ENODEV;
		goto out;
	}

	cnt = of_get_child_count(mcu_sleep_node);
	if (!cnt) {
		ret = -EINVAL;
		goto free_mcu_mode;
	}

	/*
	 * 4kb for sleep parameters
	 */
	res = sip_smc_request_share_mem(1, SHARE_PAGE_TYPE_SLEEP);
	if (res.a0 != 0) {
		pr_err("%s: no trust memory for mcu_sleep\n", __func__);
		ret = -ENOMEM;
		goto free_mcu_mode;
	}

	/* Initialize core tag */
	memset((void *)res.a1, 0, sizeof(struct rk_mcu_sleep_tags));
	config = (struct rk_mcu_sleep_tags *)res.a1;
	config->core.hdr.tag = RK_ATAG_MCU_SLP_CORE;
	config->core.hdr.size = sizeof(struct rk_mcu_sleep_core_tag) / sizeof(u32);
	config->core.total_size = sizeof(struct rk_mcu_sleep_tags) -
				  sizeof(struct rk_sleep_tag);

	slp_tag = &config->slp_tags;

	/* End point of sleep data  */
	end = (char *)config + PAGE_SIZE - sizeof(struct rk_sleep_tag);

	for_each_available_child_of_node(mcu_sleep_node, child) {
		/* Is overflow? */
		if ((char *)slp_tag->params >= end)
			break;

		ret = of_property_read_u32_array(child, "rockchip,tag",
						 &slp_tag->hdr.tag, 1);
		if (ret ||
		    slp_tag->hdr.tag <= RK_ATAG_MCU_SLP_CORE ||
		    slp_tag->hdr.tag >= RK_ATAG_MCU_SLP_MAX) {
			pr_info("%s: no or invalid rockchip,tag in %s\n",
				__func__, child->name);

			continue;
		}

		cnt = of_property_count_u32_elems(child, "rockchip,params");
		if (cnt > 0) {
			/* Is overflow? */
			if ((char *)(slp_tag->params + cnt) >= end) {
				pr_warn("%s: no more space for rockchip,tag in %s\n",
					__func__, child->name);
				break;
			}

			ret = of_property_read_u32_array(child, "rockchip,params",
							 slp_tag->params, cnt);
			if (ret) {
				pr_err("%s: rockchip,params error in %s\n",
				       __func__, child->name);
				break;
			}

			slp_tag->hdr.size =
				cnt + sizeof(struct rk_tag_header) / sizeof(u32);
		} else if (cnt == 0) {
			slp_tag->hdr.size = 0;
		} else {
			continue;
		}

		config->core.total_size += slp_tag->hdr.size * sizeof(u32);

		slp_tag = sleep_tag_next(slp_tag);
	}

	/* Add none tag.
	 * Compiler will combine the follow code as "str xzr, [x28]", but
	 * "slp->hdr" may not be 8-byte alignment. So we use memset_io instead:
	 * slp_tag->hdr.size = 0;
	 * slp_tag->hdr.tag = RK_ATAG_NONE;
	 */
	memset_io(&slp_tag->hdr, 0, sizeof(slp_tag->hdr));

	config->core.total_size += sizeof(struct rk_sleep_tag);

	ret = 0;

free_mcu_mode:
	of_node_put(mcu_sleep_node);
out:
	return ret;
}

static int parse_io_config(struct device *dev)
{
	int ret = 0, cnt;
	struct device_node *node = dev->of_node;
	struct rk_sleep_config *config = &sleep_config[RK_PM_MEM];

	cnt = of_property_count_u32_elems(node, "rockchip,sleep-io-config");
	if (cnt > 0) {
		/* 0 as the last element of virtual_pwroff_irqs */
		config->sleep_io_config =
			devm_kmalloc_array(dev, cnt, sizeof(u32), GFP_KERNEL);
		if (!config->sleep_io_config) {
			ret = -ENOMEM;
			goto out;
		}

		ret = of_property_read_u32_array(node, "rockchip,sleep-io-config",
						 config->sleep_io_config, cnt);
		if (ret) {
			dev_err(dev, "get rockchip,sleep-io-config error\n");
			goto out;
		}

		config->sleep_io_config_cnt = cnt;
	} else {
		dev_dbg(dev, "not set sleep-pin-config\n");
	}

out:
	return ret;
}

static int pm_config_probe(struct platform_device *pdev)
{
	const struct of_device_id *match_id;
	struct device_node *node;
	struct rk_sleep_config *config;

	enum of_gpio_flags flags;
	int i = 0;
	int length;
	int ret;

	match_id = of_match_node(pm_match_table, pdev->dev.of_node);
	if (!match_id)
		return -ENODEV;

	node = of_find_node_by_name(NULL, "rockchip-suspend");

	if (IS_ERR_OR_NULL(node)) {
		dev_err(&pdev->dev, "%s dev node err\n",  __func__);
		return -ENODEV;
	}

	sleep_config =
		devm_kmalloc_array(&pdev->dev, RK_PM_STATE_MAX,
				   sizeof(*sleep_config), GFP_KERNEL);
	if (!sleep_config)
		return -ENOMEM;

	config = &sleep_config[RK_PM_MEM];

	if (of_property_read_u32_array(node,
				       "rockchip,sleep-mode-config",
				       &config->mode_config, 1))
		dev_warn(&pdev->dev, "not set sleep mode config\n");
	else
		sip_smc_set_suspend_mode(SUSPEND_MODE_CONFIG, config->mode_config, 0);

	if (of_property_read_u32_array(node,
				       "rockchip,wakeup-config",
				       &config->wakeup_config, 1))
		dev_warn(&pdev->dev, "not set wakeup-config\n");
	else
		sip_smc_set_suspend_mode(WKUP_SOURCE_CONFIG, config->wakeup_config, 0);

	if (of_property_read_u32_array(node,
				       "rockchip,pwm-regulator-config",
				       &config->pwm_regulator_config, 1))
		dev_warn(&pdev->dev, "not set pwm-regulator-config\n");
	else
		sip_smc_set_suspend_mode(PWM_REGULATOR_CONFIG,
					 config->pwm_regulator_config,
					 0);

	length = of_gpio_named_count(node, "rockchip,power-ctrl");

	if (length > 0 && length < 10) {
		config->power_ctrl_config_cnt = length;
		config->power_ctrl_config =
			devm_kmalloc_array(&pdev->dev, length,
					   sizeof(u32), GFP_KERNEL);
		if (!config->power_ctrl_config)
			return -ENOMEM;

		for (i = 0; i < length; i++) {
			config->power_ctrl_config[i] =
				of_get_named_gpio_flags(node,
							"rockchip,power-ctrl",
							i,
							&flags);
			if (!gpio_is_valid(config->power_ctrl_config[i]))
				break;
			sip_smc_set_suspend_mode(GPIO_POWER_CONFIG,
						 i,
						 config->power_ctrl_config[i]);
		}
	}
	sip_smc_set_suspend_mode(GPIO_POWER_CONFIG, i, PM_INVALID_GPIO);

	if (!of_property_read_u32_array(node,
					"rockchip,sleep-debug-en",
					&config->sleep_debug_en, 1))
		sip_smc_set_suspend_mode(SUSPEND_DEBUG_ENABLE,
					 config->sleep_debug_en,
					 0);

	if (!of_property_read_u32_array(node,
					"rockchip,apios-suspend",
					&config->apios_suspend, 1))
		sip_smc_set_suspend_mode(APIOS_SUSPEND_CONFIG,
					 config->apios_suspend,
					 0);

	if (!of_property_read_u32_array(node,
					"rockchip,sleep-io-ret-config",
					&config->io_ret_config, 1)) {
		ret = sip_smc_set_suspend_mode(SUSPEND_IO_RET_CONFIG, config->io_ret_config, 0);
		if (ret)
			dev_warn(&pdev->dev,
				 "sleep-io-ret-config failed (%d), check parameters or update trust\n",
				 ret);
	}

	if (!of_property_read_u32_array(node,
					"rockchip,sleep-pin-config",
					config->sleep_pin_config, 2)) {
		ret = sip_smc_set_suspend_mode(SLEEP_PIN_CONFIG,
					       config->sleep_pin_config[0],
					       config->sleep_pin_config[1]);
		if (ret)
			dev_warn(&pdev->dev,
				 "sleep-pin-config failed (%d), check parameters or update trust\n",
				 ret);
	}

	parse_io_config(&pdev->dev);
	parse_mcu_sleep_config(node);

#ifndef MODULE
	parse_virtual_pwroff_config(node);

	for (i = RK_PM_MEM; i < RK_PM_STATE_MAX; i++) {
		parse_sleep_config(node, i);
		parse_on_off_regulator(node, i);
	}
#endif

	return 0;
}

#ifndef MODULE
static int pm_config_prepare(struct device *dev)
{
	int i;
	suspend_state_t suspend_state = mem_sleep_current;
	enum rk_pm_state state = suspend_state - PM_SUSPEND_MEM;
	struct regulator_dev **on_list;
	struct regulator_dev **off_list;
	struct rk_sleep_config *config, *def_config = &sleep_config[RK_PM_MEM];

	sip_smc_set_suspend_mode(LINUX_PM_STATE,
				 suspend_state,
				 0);

	if (state >= RK_PM_STATE_MAX)
		return 0;

	config = &sleep_config[state];

	if (config->mode_config)
		sip_smc_set_suspend_mode(SUSPEND_MODE_CONFIG,
					 config->mode_config, 0);
	else if (def_config->mode_config)
		sip_smc_set_suspend_mode(SUSPEND_MODE_CONFIG,
					 def_config->mode_config, 0);

	if (config->wakeup_config)
		sip_smc_set_suspend_mode(WKUP_SOURCE_CONFIG,
					 config->wakeup_config, 0);
	else if (def_config->wakeup_config)
		sip_smc_set_suspend_mode(WKUP_SOURCE_CONFIG,
					 def_config->wakeup_config, 0);

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
#ifndef MODULE
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
