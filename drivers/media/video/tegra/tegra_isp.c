/*
 * drivers/media/video/tegra/isp.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/regulator/consumer.h>
#include <media/tegra_isp.h>

/* Eventually this should handle all clock and reset calls for the isp, vi,
 * vi_sensor, and csi modules, replacing nvrm and nvos completely for camera
 */
#define TEGRA_ISP_NAME "tegra_isp"

static struct regulator *tegra_isp_regulator_csi;

static int tegra_isp_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TEGRA_ISP_IOCTL_CSI_POWER_ON:
		regulator_enable(tegra_isp_regulator_csi);
		break;
	case TEGRA_ISP_IOCTL_CSI_POWER_OFF:
		regulator_disable(tegra_isp_regulator_csi);
		break;
	default:
		pr_err("%s: Unknoown tegra_isp ioctl.\n", TEGRA_ISP_NAME);
	}
	return 0;
}

static const struct file_operations tegra_isp_fops = {
	.owner = THIS_MODULE,
	.ioctl = tegra_isp_ioctl,
};

static struct miscdevice tegra_isp_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = TEGRA_ISP_NAME,
	.fops = &tegra_isp_fops,
};

static int tegra_isp_probe(struct platform_device *pdev)
{
	int err;

	pr_info("%s: probe\n", TEGRA_ISP_NAME);
	tegra_isp_regulator_csi = regulator_get(NULL, "vcsi");
	if (IS_ERR(tegra_isp_regulator_csi)) {
		pr_err("%s: Couldn't get regulator vcsi\n", TEGRA_ISP_NAME);
		return PTR_ERR(tegra_isp_regulator_csi);
	}

	err = misc_register(&tegra_isp_device);
	if (err) {
		pr_err("%s: Unable to register misc device!\n", TEGRA_ISP_NAME);
		regulator_put(tegra_isp_regulator_csi);
		return err;
	}

	/* XXX FIX ME -- SHOULDN'T BE ALWAYS ON! */
	regulator_enable(tegra_isp_regulator_csi);
	return 0;
}

static int tegra_isp_remove(struct platform_device *pdev)
{
	/* XXX FIX ME -- SHOULDN'T BE ALWAYS ON! */
	regulator_disable(tegra_isp_regulator_csi);
	misc_deregister(&tegra_isp_device);
	return 0;
}

static struct platform_driver tegra_isp_driver = {
	.probe = tegra_isp_probe,
	.remove = tegra_isp_remove,
	.driver = { .name = TEGRA_ISP_NAME }
};

static int __init tegra_isp_init(void)
{
	return platform_driver_register(&tegra_isp_driver);
}

static void __exit tegra_isp_exit(void)
{
	platform_driver_unregister(&tegra_isp_driver);
}

module_init(tegra_isp_init);
module_exit(tegra_isp_exit);

