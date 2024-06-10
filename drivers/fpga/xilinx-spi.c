// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Spartan6 and 7 Series Slave Serial SPI Driver
 *
 * Copyright (C) 2017 DENX Software Engineering
 *
 * Anatolij Gustschin <agust@denx.de>
 *
 * Manage Xilinx FPGA firmware that is loaded over SPI using
 * the slave serial configuration interface.
 */

#include "xilinx-core.h"

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

static int xilinx_spi_write(struct xilinx_fpga_core *core, const char *buf,
			    size_t count)
{
	struct spi_device *spi = to_spi_device(core->dev);
	const char *fw_data = buf;
	const char *fw_data_end = fw_data + count;

	while (fw_data < fw_data_end) {
		size_t remaining, stride;
		int ret;

		remaining = fw_data_end - fw_data;
		stride = min_t(size_t, remaining, SZ_4K);

		ret = spi_write(spi, fw_data, stride);
		if (ret) {
			dev_err(core->dev, "SPI error in firmware write: %d\n",
				ret);
			return ret;
		}
		fw_data += stride;
	}

	return 0;
}

static int xilinx_spi_probe(struct spi_device *spi)
{
	struct xilinx_fpga_core *core;

	core = devm_kzalloc(&spi->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = &spi->dev;
	core->write = xilinx_spi_write;

	return xilinx_core_probe(core);
}

#ifdef CONFIG_OF
static const struct of_device_id xlnx_spi_of_match[] = {
	{
		.compatible = "xlnx,fpga-slave-serial",
	},
	{}
};
MODULE_DEVICE_TABLE(of, xlnx_spi_of_match);
#endif

static struct spi_driver xilinx_slave_spi_driver = {
	.driver = {
		.name = "xlnx-slave-spi",
		.of_match_table = of_match_ptr(xlnx_spi_of_match),
	},
	.probe = xilinx_spi_probe,
};

module_spi_driver(xilinx_slave_spi_driver)

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_DESCRIPTION("Load Xilinx FPGA firmware over SPI");
