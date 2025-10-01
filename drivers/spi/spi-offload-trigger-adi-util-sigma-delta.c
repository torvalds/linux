// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Analog Devices Inc.
 * Copyright (C) 2025 BayLibre, SAS
 */

#include <linux/clk.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/offload/types.h>
#include <linux/types.h>

static bool adi_util_sigma_delta_match(struct spi_offload_trigger *trigger,
				       enum spi_offload_trigger_type type,
				       u64 *args, u32 nargs)
{
	return type == SPI_OFFLOAD_TRIGGER_DATA_READY && nargs == 0;
}

static const struct spi_offload_trigger_ops adi_util_sigma_delta_ops = {
	.match = adi_util_sigma_delta_match,
};

static int adi_util_sigma_delta_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_offload_trigger_info info = {
		.fwnode = dev_fwnode(dev),
		.ops = &adi_util_sigma_delta_ops,
	};
	struct clk *clk;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get clock\n");

	return devm_spi_offload_trigger_register(dev, &info);
}

static const struct of_device_id adi_util_sigma_delta_of_match_table[] = {
	{ .compatible = "adi,util-sigma-delta-spi", },
	{ }
};
MODULE_DEVICE_TABLE(of, adi_util_sigma_delta_of_match_table);

static struct platform_driver adi_util_sigma_delta_driver = {
	.probe  = adi_util_sigma_delta_probe,
	.driver = {
		.name = "adi-util-sigma-delta-spi",
		.of_match_table = adi_util_sigma_delta_of_match_table,
	},
};
module_platform_driver(adi_util_sigma_delta_driver);

MODULE_AUTHOR("David Lechner <dlechner@baylibre.com>");
MODULE_DESCRIPTION("ADI Sigma-Delta SPI offload trigger utility driver");
MODULE_LICENSE("GPL");
