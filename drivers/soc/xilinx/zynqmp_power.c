// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajan.vaja@xilinx.com>
 */

#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include <linux/firmware/xlnx-zynqmp.h>

enum pm_suspend_mode {
	PM_SUSPEND_MODE_FIRST = 0,
	PM_SUSPEND_MODE_STD = PM_SUSPEND_MODE_FIRST,
	PM_SUSPEND_MODE_POWER_OFF,
};

#define PM_SUSPEND_MODE_FIRST	PM_SUSPEND_MODE_STD

static const char *const suspend_modes[] = {
	[PM_SUSPEND_MODE_STD] = "standard",
	[PM_SUSPEND_MODE_POWER_OFF] = "power-off",
};

static enum pm_suspend_mode suspend_mode = PM_SUSPEND_MODE_STD;
static const struct zynqmp_eemi_ops *eemi_ops;

enum pm_api_cb_id {
	PM_INIT_SUSPEND_CB = 30,
	PM_ACKNOWLEDGE_CB,
	PM_NOTIFY_CB,
};

static void zynqmp_pm_get_callback_data(u32 *buf)
{
	zynqmp_pm_invoke_fn(GET_CALLBACK_DATA, 0, 0, 0, 0, buf);
}

static irqreturn_t zynqmp_pm_isr(int irq, void *data)
{
	u32 payload[CB_PAYLOAD_SIZE];

	zynqmp_pm_get_callback_data(payload);

	/* First element is callback API ID, others are callback arguments */
	if (payload[0] == PM_INIT_SUSPEND_CB) {
		switch (payload[1]) {
		case SUSPEND_SYSTEM_SHUTDOWN:
			orderly_poweroff(true);
			break;
		case SUSPEND_POWER_REQUEST:
			pm_suspend(PM_SUSPEND_MEM);
			break;
		default:
			pr_err("%s Unsupported InitSuspendCb reason "
				"code %d\n", __func__, payload[1]);
		}
	}

	return IRQ_HANDLED;
}

static ssize_t suspend_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	char *s = buf;
	int md;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md]) {
			if (md == suspend_mode)
				s += sprintf(s, "[%s] ", suspend_modes[md]);
			else
				s += sprintf(s, "%s ", suspend_modes[md]);
		}

	/* Convert last space to newline */
	if (s != buf)
		*(s - 1) = '\n';
	return (s - buf);
}

static ssize_t suspend_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int md, ret = -EINVAL;

	if (!eemi_ops->set_suspend_mode)
		return ret;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md] &&
		    sysfs_streq(suspend_modes[md], buf)) {
			ret = 0;
			break;
		}

	if (!ret && md != suspend_mode) {
		ret = eemi_ops->set_suspend_mode(md);
		if (likely(!ret))
			suspend_mode = md;
	}

	return ret ? ret : count;
}

static DEVICE_ATTR_RW(suspend_mode);

static int zynqmp_pm_probe(struct platform_device *pdev)
{
	int ret, irq;
	u32 pm_api_version;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	if (!eemi_ops->get_api_version || !eemi_ops->init_finalize)
		return -ENXIO;

	eemi_ops->init_finalize();
	eemi_ops->get_api_version(&pm_api_version);

	/* Check PM API version number */
	if (pm_api_version < ZYNQMP_PM_VERSION)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENXIO;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, zynqmp_pm_isr,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(&pdev->dev), &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "devm_request_threaded_irq '%d' failed "
			"with %d\n", irq, ret);
		return ret;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);
	if (ret) {
		dev_err(&pdev->dev, "unable to create sysfs interface\n");
		return ret;
	}

	return 0;
}

static int zynqmp_pm_remove(struct platform_device *pdev)
{
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);

	return 0;
}

static const struct of_device_id pm_of_match[] = {
	{ .compatible = "xlnx,zynqmp-power", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, pm_of_match);

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe = zynqmp_pm_probe,
	.remove = zynqmp_pm_remove,
	.driver = {
		.name = "zynqmp_power",
		.of_match_table = pm_of_match,
	},
};
module_platform_driver(zynqmp_pm_platform_driver);
