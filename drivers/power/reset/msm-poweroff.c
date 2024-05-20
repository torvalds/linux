// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#ifdef CONFIG_INPUT_QPNP_POWER_ON
#include <linux/input/qpnp-power-on.h>
#endif
#include <linux/qcom_scm.h>

static void __iomem *msm_ps_hold;
static int deassert_pshold(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	qcom_scm_deassert_ps_hold();
	writel(0, msm_ps_hold);
	mdelay(10000);

	return NOTIFY_DONE;
}

static struct notifier_block restart_nb = {
	.notifier_call = deassert_pshold,
	.priority = 200,
};

static void do_msm_poweroff(void)
{
#ifdef CONFIG_INPUT_QPNP_POWER_ON
	qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);
#endif
	deassert_pshold(&restart_nb, 0, NULL);
}

static int msm_restart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *mem;
	int ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	msm_ps_hold = devm_ioremap_resource(dev, mem);
	if (IS_ERR(msm_ps_hold))
		return PTR_ERR(msm_ps_hold);

	ret = register_restart_handler(&restart_nb);
	if (ret)
		dev_err(dev, "failed to register restart handler.\n");

	pm_power_off = do_msm_poweroff;

	return 0;
}

static int msm_restart_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = unregister_restart_handler(&restart_nb);
	if (ret)
		dev_err(dev, "failed to unregister restart handler.\n");

	return 0;
}

static const struct of_device_id of_msm_restart_match[] = {
	{ .compatible = "qcom,pshold", },
	{},
};
MODULE_DEVICE_TABLE(of, of_msm_restart_match);

static struct platform_driver msm_restart_driver = {
	.probe = msm_restart_probe,
	.remove = msm_restart_remove,
	.driver = {
		.name = "msm-restart",
		.of_match_table = of_match_ptr(of_msm_restart_match),
	},
};

static int __init msm_restart_init(void)
{
	return platform_driver_register(&msm_restart_driver);
}
module_init(msm_restart_init);

static __exit void msm_restart_exit(void)
{
	platform_driver_unregister(&msm_restart_driver);
}
module_exit(msm_restart_exit);

MODULE_DESCRIPTION("MSM Poweroff Driver");
MODULE_LICENSE("GPL");
