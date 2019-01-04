/*
 * Xilinx Spartan6 Slave Serial SPI Driver
 *
 * Copyright (C) 2017 DENX Software Engineering
 *
 * Anatolij Gustschin <agust@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Manage Xilinx FPGA firmware that is loaded over SPI using
 * the slave serial configuration interface.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/sizes.h>

struct xilinx_spi_conf {
	struct spi_device *spi;
	struct gpio_desc *prog_b;
	struct gpio_desc *done;
};

static enum fpga_mgr_states xilinx_spi_state(struct fpga_manager *mgr)
{
	struct xilinx_spi_conf *conf = mgr->priv;

	if (!gpiod_get_value(conf->done))
		return FPGA_MGR_STATE_RESET;

	return FPGA_MGR_STATE_UNKNOWN;
}

static int xilinx_spi_write_init(struct fpga_manager *mgr,
				 struct fpga_image_info *info,
				 const char *buf, size_t count)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	const size_t prog_latency_7500us = 7500;
	const size_t prog_pulse_1us = 1;

	if (info->flags & FPGA_MGR_PARTIAL_RECONFIG) {
		dev_err(&mgr->dev, "Partial reconfiguration not supported.\n");
		return -EINVAL;
	}

	gpiod_set_value(conf->prog_b, 1);

	udelay(prog_pulse_1us); /* min is 500 ns */

	gpiod_set_value(conf->prog_b, 0);

	if (gpiod_get_value(conf->done)) {
		dev_err(&mgr->dev, "Unexpected DONE pin state...\n");
		return -EIO;
	}

	/* program latency */
	usleep_range(prog_latency_7500us, prog_latency_7500us + 100);
	return 0;
}

static int xilinx_spi_write(struct fpga_manager *mgr, const char *buf,
			    size_t count)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	const char *fw_data = buf;
	const char *fw_data_end = fw_data + count;

	while (fw_data < fw_data_end) {
		size_t remaining, stride;
		int ret;

		remaining = fw_data_end - fw_data;
		stride = min_t(size_t, remaining, SZ_4K);

		ret = spi_write(conf->spi, fw_data, stride);
		if (ret) {
			dev_err(&mgr->dev, "SPI error in firmware write: %d\n",
				ret);
			return ret;
		}
		fw_data += stride;
	}

	return 0;
}

static int xilinx_spi_apply_cclk_cycles(struct xilinx_spi_conf *conf)
{
	struct spi_device *spi = conf->spi;
	const u8 din_data[1] = { 0xff };
	int ret;

	ret = spi_write(conf->spi, din_data, sizeof(din_data));
	if (ret)
		dev_err(&spi->dev, "applying CCLK cycles failed: %d\n", ret);

	return ret;
}

static int xilinx_spi_write_complete(struct fpga_manager *mgr,
				     struct fpga_image_info *info)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	unsigned long timeout;
	int ret;

	if (gpiod_get_value(conf->done))
		return xilinx_spi_apply_cclk_cycles(conf);

	timeout = jiffies + usecs_to_jiffies(info->config_complete_timeout_us);

	while (time_before(jiffies, timeout)) {

		ret = xilinx_spi_apply_cclk_cycles(conf);
		if (ret)
			return ret;

		if (gpiod_get_value(conf->done))
			return xilinx_spi_apply_cclk_cycles(conf);
	}

	dev_err(&mgr->dev, "Timeout after config data transfer.\n");
	return -ETIMEDOUT;
}

static const struct fpga_manager_ops xilinx_spi_ops = {
	.state = xilinx_spi_state,
	.write_init = xilinx_spi_write_init,
	.write = xilinx_spi_write,
	.write_complete = xilinx_spi_write_complete,
};

static int xilinx_spi_probe(struct spi_device *spi)
{
	struct xilinx_spi_conf *conf;
	struct fpga_manager *mgr;

	conf = devm_kzalloc(&spi->dev, sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return -ENOMEM;

	conf->spi = spi;

	/* PROGRAM_B is active low */
	conf->prog_b = devm_gpiod_get(&spi->dev, "prog_b", GPIOD_OUT_LOW);
	if (IS_ERR(conf->prog_b)) {
		dev_err(&spi->dev, "Failed to get PROGRAM_B gpio: %ld\n",
			PTR_ERR(conf->prog_b));
		return PTR_ERR(conf->prog_b);
	}

	conf->done = devm_gpiod_get(&spi->dev, "done", GPIOD_IN);
	if (IS_ERR(conf->done)) {
		dev_err(&spi->dev, "Failed to get DONE gpio: %ld\n",
			PTR_ERR(conf->done));
		return PTR_ERR(conf->done);
	}

	mgr = devm_fpga_mgr_create(&spi->dev,
				   "Xilinx Slave Serial FPGA Manager",
				   &xilinx_spi_ops, conf);
	if (!mgr)
		return -ENOMEM;

	spi_set_drvdata(spi, mgr);

	return fpga_mgr_register(mgr);
}

static int xilinx_spi_remove(struct spi_device *spi)
{
	struct fpga_manager *mgr = spi_get_drvdata(spi);

	fpga_mgr_unregister(mgr);

	return 0;
}

static const struct of_device_id xlnx_spi_of_match[] = {
	{ .compatible = "xlnx,fpga-slave-serial", },
	{}
};
MODULE_DEVICE_TABLE(of, xlnx_spi_of_match);

static struct spi_driver xilinx_slave_spi_driver = {
	.driver = {
		.name = "xlnx-slave-spi",
		.of_match_table = of_match_ptr(xlnx_spi_of_match),
	},
	.probe = xilinx_spi_probe,
	.remove = xilinx_spi_remove,
};

module_spi_driver(xilinx_slave_spi_driver)

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_DESCRIPTION("Load Xilinx FPGA firmware over SPI");
