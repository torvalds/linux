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
	struct gpio_desc *init_b;
	struct gpio_desc *done;
};

static int get_done_gpio(struct fpga_manager *mgr)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	int ret;

	ret = gpiod_get_value(conf->done);

	if (ret < 0)
		dev_err(&mgr->dev, "Error reading DONE (%d)\n", ret);

	return ret;
}

static enum fpga_mgr_states xilinx_spi_state(struct fpga_manager *mgr)
{
	if (!get_done_gpio(mgr))
		return FPGA_MGR_STATE_RESET;

	return FPGA_MGR_STATE_UNKNOWN;
}

/**
 * wait_for_init_b - wait for the INIT_B pin to have a given state, or wait
 * a given delay if the pin is unavailable
 *
 * @mgr:        The FPGA manager object
 * @value:      Value INIT_B to wait for (1 = asserted = low)
 * @alt_udelay: Delay to wait if the INIT_B GPIO is not available
 *
 * Returns 0 when the INIT_B GPIO reached the given state or -ETIMEDOUT if
 * too much time passed waiting for that. If no INIT_B GPIO is available
 * then always return 0.
 */
static int wait_for_init_b(struct fpga_manager *mgr, int value,
			   unsigned long alt_udelay)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	if (conf->init_b) {
		while (time_before(jiffies, timeout)) {
			int ret = gpiod_get_value(conf->init_b);

			if (ret == value)
				return 0;

			if (ret < 0) {
				dev_err(&mgr->dev, "Error reading INIT_B (%d)\n", ret);
				return ret;
			}

			usleep_range(100, 400);
		}

		dev_err(&mgr->dev, "Timeout waiting for INIT_B to %s\n",
			value ? "assert" : "deassert");
		return -ETIMEDOUT;
	}

	udelay(alt_udelay);

	return 0;
}

static int xilinx_spi_write_init(struct fpga_manager *mgr,
				 struct fpga_image_info *info,
				 const char *buf, size_t count)
{
	struct xilinx_spi_conf *conf = mgr->priv;
	int err;

	if (info->flags & FPGA_MGR_PARTIAL_RECONFIG) {
		dev_err(&mgr->dev, "Partial reconfiguration not supported\n");
		return -EINVAL;
	}

	gpiod_set_value(conf->prog_b, 1);

	err = wait_for_init_b(mgr, 1, 1); /* min is 500 ns */
	if (err) {
		gpiod_set_value(conf->prog_b, 0);
		return err;
	}

	gpiod_set_value(conf->prog_b, 0);

	err = wait_for_init_b(mgr, 0, 0);
	if (err)
		return err;

	if (get_done_gpio(mgr)) {
		dev_err(&mgr->dev, "Unexpected DONE pin state...\n");
		return -EIO;
	}

	/* program latency */
	usleep_range(7500, 7600);
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
	unsigned long timeout = jiffies + usecs_to_jiffies(info->config_complete_timeout_us);
	bool expired = false;
	int done;
	int ret;

	/*
	 * This loop is carefully written such that if the driver is
	 * scheduled out for more than 'timeout', we still check for DONE
	 * before giving up and we apply 8 extra CCLK cycles in all cases.
	 */
	while (!expired) {
		expired = time_after(jiffies, timeout);

		done = get_done_gpio(mgr);
		if (done < 0)
			return done;

		ret = xilinx_spi_apply_cclk_cycles(conf);
		if (ret)
			return ret;

		if (done)
			return 0;
	}

	if (conf->init_b) {
		ret = gpiod_get_value(conf->init_b);

		if (ret < 0) {
			dev_err(&mgr->dev, "Error reading INIT_B (%d)\n", ret);
			return ret;
		}

		dev_err(&mgr->dev,
			ret ? "CRC error or invalid device\n"
			: "Missing sync word or incomplete bitstream\n");
	} else {
		dev_err(&mgr->dev, "Timeout after config data transfer\n");
	}

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
	if (IS_ERR(conf->prog_b))
		return dev_err_probe(&spi->dev, PTR_ERR(conf->prog_b),
				     "Failed to get PROGRAM_B gpio\n");

	conf->init_b = devm_gpiod_get_optional(&spi->dev, "init-b", GPIOD_IN);
	if (IS_ERR(conf->init_b))
		return dev_err_probe(&spi->dev, PTR_ERR(conf->init_b),
				     "Failed to get INIT_B gpio\n");

	conf->done = devm_gpiod_get(&spi->dev, "done", GPIOD_IN);
	if (IS_ERR(conf->done))
		return dev_err_probe(&spi->dev, PTR_ERR(conf->done),
				     "Failed to get DONE gpio\n");

	mgr = devm_fpga_mgr_register(&spi->dev,
				     "Xilinx Slave Serial FPGA Manager",
				     &xilinx_spi_ops, conf);
	return PTR_ERR_OR_ZERO(mgr);
}

#ifdef CONFIG_OF
static const struct of_device_id xlnx_spi_of_match[] = {
	{ .compatible = "xlnx,fpga-slave-serial", },
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
