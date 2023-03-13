// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/mfd/rk808.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/i2c.h>

/*
 * The Odroid Go Ultra has 2 PMICs:
 * - RK818 (manages the battery and USB-C power supply)
 * - RK817
 * Both PMICs feeds power to the S922X SoC, so they must be powered-off in sequence.
 * Vendor does power-off the RK817 first, then the RK818 so here we follow this sequence.
 */

struct odroid_go_ultra_poweroff_data {
	struct device *dev;
	struct device *rk817;
	struct device *rk818;
};

static int odroid_go_ultra_poweroff_prepare(struct sys_off_data *data)
{
	struct odroid_go_ultra_poweroff_data *poweroff_data = data->cb_data;
	struct regmap *rk817, *rk818;
	int ret;

	/* RK817 Regmap */
	rk817 = dev_get_regmap(poweroff_data->rk817, NULL);
	if (!rk817) {
		dev_err(poweroff_data->dev, "failed to get rk817 regmap\n");
		return notifier_from_errno(-EINVAL);
	}

	/* RK818 Regmap */
	rk818 = dev_get_regmap(poweroff_data->rk818, NULL);
	if (!rk818) {
		dev_err(poweroff_data->dev, "failed to get rk818 regmap\n");
		return notifier_from_errno(-EINVAL);
	}

	dev_info(poweroff_data->dev, "Setting PMICs for power off");

	/* RK817 */
	ret = regmap_update_bits(rk817, RK817_SYS_CFG(3), DEV_OFF, DEV_OFF);
	if (ret) {
		dev_err(poweroff_data->dev, "failed to poweroff rk817\n");
		return notifier_from_errno(ret);
	}

	/* RK818 */
	ret = regmap_update_bits(rk818, RK818_DEVCTRL_REG, DEV_OFF, DEV_OFF);
	if (ret) {
		dev_err(poweroff_data->dev, "failed to poweroff rk818\n");
		return notifier_from_errno(ret);
	}

	return NOTIFY_OK;
}

static void odroid_go_ultra_poweroff_put_pmic_device(void *data)
{
	struct device *dev = data;

	put_device(dev);
}

static int odroid_go_ultra_poweroff_get_pmic_device(struct device *dev, const char *compatible,
						    struct device **pmic)
{
	struct device_node *pmic_node;
	struct i2c_client *pmic_client;

	pmic_node = of_find_compatible_node(NULL, NULL, compatible);
	if (!pmic_node)
		return -ENODEV;

	pmic_client = of_find_i2c_device_by_node(pmic_node);
	of_node_put(pmic_node);
	if (!pmic_client)
		return -EPROBE_DEFER;

	*pmic = &pmic_client->dev;

	return devm_add_action_or_reset(dev, odroid_go_ultra_poweroff_put_pmic_device, *pmic);
}

static int odroid_go_ultra_poweroff_probe(struct platform_device *pdev)
{
	struct odroid_go_ultra_poweroff_data *poweroff_data;
	int ret;

	poweroff_data = devm_kzalloc(&pdev->dev, sizeof(*poweroff_data), GFP_KERNEL);
	if (!poweroff_data)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, poweroff_data);

	/* RK818 PMIC Device */
	ret = odroid_go_ultra_poweroff_get_pmic_device(&pdev->dev, "rockchip,rk818",
						       &poweroff_data->rk818);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to get rk818 mfd data\n");

	/* RK817 PMIC Device */
	ret = odroid_go_ultra_poweroff_get_pmic_device(&pdev->dev, "rockchip,rk817",
						       &poweroff_data->rk817);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to get rk817 mfd data\n");

	/* Register as SYS_OFF_MODE_POWER_OFF_PREPARE because regmap_update_bits may sleep */
	ret = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_DEFAULT,
					    odroid_go_ultra_poweroff_prepare,
					    poweroff_data);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register sys-off handler\n");

	dev_info(&pdev->dev, "Registered Power-Off handler\n");

	return 0;
}
static struct platform_device *pdev;

static struct platform_driver odroid_go_ultra_poweroff_driver = {
	.driver = {
		.name	= "odroid-go-ultra-poweroff",
	},
	.probe = odroid_go_ultra_poweroff_probe,
};

static int __init odroid_go_ultra_poweroff_init(void)
{
	int ret;

	/* Only create when running on the Odroid Go Ultra device */
	if (!of_device_is_compatible(of_root, "hardkernel,odroid-go-ultra"))
		return -ENODEV;

	ret = platform_driver_register(&odroid_go_ultra_poweroff_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_resndata(NULL, "odroid-go-ultra-poweroff", -1,
						 NULL, 0, NULL, 0);

	if (IS_ERR(pdev)) {
		platform_driver_unregister(&odroid_go_ultra_poweroff_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}

static void __exit odroid_go_ultra_poweroff_exit(void)
{
	/* Only delete when running on the Odroid Go Ultra device */
	if (!of_device_is_compatible(of_root, "hardkernel,odroid-go-ultra"))
		return;

	platform_device_unregister(pdev);
	platform_driver_unregister(&odroid_go_ultra_poweroff_driver);
}

module_init(odroid_go_ultra_poweroff_init);
module_exit(odroid_go_ultra_poweroff_exit);

MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_DESCRIPTION("Odroid Go Ultra poweroff driver");
MODULE_LICENSE("GPL");
