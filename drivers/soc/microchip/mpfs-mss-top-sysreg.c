// SPDX-License-Identifier: GPL-2.0

#include <linux/array_size.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static const struct mfd_cell mpfs_mss_top_sysreg_devs[] = {
	MFD_CELL_NAME("mpfs-reset"),
};

static int mpfs_mss_top_sysreg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = mfd_add_devices(dev, PLATFORM_DEVID_NONE, mpfs_mss_top_sysreg_devs,
			      ARRAY_SIZE(mpfs_mss_top_sysreg_devs) , NULL, 0, NULL);
	if (ret)
		return ret;

	return devm_of_platform_populate(dev);
}

static const struct of_device_id mpfs_mss_top_sysreg_of_match[] = {
	{ .compatible = "microchip,mpfs-mss-top-sysreg", },
	{},
};
MODULE_DEVICE_TABLE(of, mpfs_mss_top_sysreg_of_match);

static struct platform_driver mpfs_mss_top_sysreg_driver = {
	.driver = {
		.name = "mpfs-mss-top-sysreg",
		.of_match_table = mpfs_mss_top_sysreg_of_match,
	},
	.probe = mpfs_mss_top_sysreg_probe,
};
module_platform_driver(mpfs_mss_top_sysreg_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("PolarFire SoC mss top sysreg driver");
