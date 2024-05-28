// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Spartan6 and 7 Series SelectMAP interface driver
 *
 * (C) 2024 Charles Perry <charles.perry@savoirfairelinux.com>
 *
 * Manage Xilinx FPGA firmware loaded over the SelectMAP configuration
 * interface.
 */

#include "xilinx-core.h"

#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

struct xilinx_selectmap_conf {
	struct xilinx_fpga_core core;
	void __iomem *base;
};

#define to_xilinx_selectmap_conf(obj) \
	container_of(obj, struct xilinx_selectmap_conf, core)

static int xilinx_selectmap_write(struct xilinx_fpga_core *core,
				  const char *buf, size_t count)
{
	struct xilinx_selectmap_conf *conf = to_xilinx_selectmap_conf(core);
	size_t i;

	for (i = 0; i < count; ++i)
		writeb(buf[i], conf->base);

	return 0;
}

static int xilinx_selectmap_probe(struct platform_device *pdev)
{
	struct xilinx_selectmap_conf *conf;
	struct gpio_desc *gpio;
	void __iomem *base;

	conf = devm_kzalloc(&pdev->dev, sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return -ENOMEM;

	conf->core.dev = &pdev->dev;
	conf->core.write = xilinx_selectmap_write;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(base))
		return dev_err_probe(&pdev->dev, PTR_ERR(base),
				     "ioremap error\n");
	conf->base = base;

	/* CSI_B is active low */
	gpio = devm_gpiod_get_optional(&pdev->dev, "csi", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpio),
				     "Failed to get CSI_B gpio\n");

	/* RDWR_B is active low */
	gpio = devm_gpiod_get_optional(&pdev->dev, "rdwr", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpio),
				     "Failed to get RDWR_B gpio\n");

	return xilinx_core_probe(&conf->core);
}

static const struct of_device_id xlnx_selectmap_of_match[] = {
	{ .compatible = "xlnx,fpga-xc7s-selectmap", }, // Spartan-7
	{ .compatible = "xlnx,fpga-xc7a-selectmap", }, // Artix-7
	{ .compatible = "xlnx,fpga-xc7k-selectmap", }, // Kintex-7
	{ .compatible = "xlnx,fpga-xc7v-selectmap", }, // Virtex-7
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_selectmap_of_match);

static struct platform_driver xilinx_selectmap_driver = {
	.driver = {
		.name = "xilinx-selectmap",
		.of_match_table = xlnx_selectmap_of_match,
	},
	.probe  = xilinx_selectmap_probe,
};

module_platform_driver(xilinx_selectmap_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Charles Perry <charles.perry@savoirfairelinux.com>");
MODULE_DESCRIPTION("Load Xilinx FPGA firmware over SelectMap");
