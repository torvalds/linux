// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common parts of the Xilinx Spartan6 and 7 Series FPGA manager drivers.
 *
 * Copyright (C) 2017 DENX Software Engineering
 *
 * Anatolij Gustschin <agust@denx.de>
 */

#include "xilinx-core.h"

#include <linux/delay.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

static int get_done_gpio(struct fpga_manager *mgr)
{
	struct xilinx_fpga_core *core = mgr->priv;
	int ret;

	ret = gpiod_get_value(core->done);
	if (ret < 0)
		dev_err(&mgr->dev, "Error reading DONE (%d)\n", ret);

	return ret;
}

static enum fpga_mgr_states xilinx_core_state(struct fpga_manager *mgr)
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
	struct xilinx_fpga_core *core = mgr->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	if (core->init_b) {
		while (time_before(jiffies, timeout)) {
			int ret = gpiod_get_value(core->init_b);

			if (ret == value)
				return 0;

			if (ret < 0) {
				dev_err(&mgr->dev,
					"Error reading INIT_B (%d)\n", ret);
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

static int xilinx_core_write_init(struct fpga_manager *mgr,
				  struct fpga_image_info *info, const char *buf,
				  size_t count)
{
	struct xilinx_fpga_core *core = mgr->priv;
	int err;

	if (info->flags & FPGA_MGR_PARTIAL_RECONFIG) {
		dev_err(&mgr->dev, "Partial reconfiguration not supported\n");
		return -EINVAL;
	}

	gpiod_set_value(core->prog_b, 1);

	err = wait_for_init_b(mgr, 1, 1); /* min is 500 ns */
	if (err) {
		gpiod_set_value(core->prog_b, 0);
		return err;
	}

	gpiod_set_value(core->prog_b, 0);

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

static int xilinx_core_write(struct fpga_manager *mgr, const char *buf,
			     size_t count)
{
	struct xilinx_fpga_core *core = mgr->priv;

	return core->write(core, buf, count);
}

static int xilinx_core_write_complete(struct fpga_manager *mgr,
				      struct fpga_image_info *info)
{
	struct xilinx_fpga_core *core = mgr->priv;
	unsigned long timeout =
		jiffies + usecs_to_jiffies(info->config_complete_timeout_us);
	bool expired = false;
	int done;
	int ret;
	const char padding[1] = { 0xff };

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

		ret = core->write(core, padding, sizeof(padding));
		if (ret)
			return ret;

		if (done)
			return 0;
	}

	if (core->init_b) {
		ret = gpiod_get_value(core->init_b);

		if (ret < 0) {
			dev_err(&mgr->dev, "Error reading INIT_B (%d)\n", ret);
			return ret;
		}

		dev_err(&mgr->dev,
			ret ? "CRC error or invalid device\n" :
			      "Missing sync word or incomplete bitstream\n");
	} else {
		dev_err(&mgr->dev, "Timeout after config data transfer\n");
	}

	return -ETIMEDOUT;
}

static inline struct gpio_desc *
xilinx_core_devm_gpiod_get(struct device *dev, const char *con_id,
			   const char *legacy_con_id, enum gpiod_flags flags)
{
	struct gpio_desc *desc;

	desc = devm_gpiod_get(dev, con_id, flags);
	if (IS_ERR(desc) && PTR_ERR(desc) == -ENOENT &&
	    of_device_is_compatible(dev->of_node, "xlnx,fpga-slave-serial"))
		desc = devm_gpiod_get(dev, legacy_con_id, flags);

	return desc;
}

static const struct fpga_manager_ops xilinx_core_ops = {
	.state = xilinx_core_state,
	.write_init = xilinx_core_write_init,
	.write = xilinx_core_write,
	.write_complete = xilinx_core_write_complete,
};

int xilinx_core_probe(struct xilinx_fpga_core *core)
{
	struct fpga_manager *mgr;

	if (!core || !core->dev || !core->write)
		return -EINVAL;

	/* PROGRAM_B is active low */
	core->prog_b = xilinx_core_devm_gpiod_get(core->dev, "prog", "prog_b",
						  GPIOD_OUT_LOW);
	if (IS_ERR(core->prog_b))
		return dev_err_probe(core->dev, PTR_ERR(core->prog_b),
				     "Failed to get PROGRAM_B gpio\n");

	core->init_b = xilinx_core_devm_gpiod_get(core->dev, "init", "init-b",
						  GPIOD_IN);
	if (IS_ERR(core->init_b))
		return dev_err_probe(core->dev, PTR_ERR(core->init_b),
				     "Failed to get INIT_B gpio\n");

	core->done = devm_gpiod_get(core->dev, "done", GPIOD_IN);
	if (IS_ERR(core->done))
		return dev_err_probe(core->dev, PTR_ERR(core->done),
				     "Failed to get DONE gpio\n");

	mgr = devm_fpga_mgr_register(core->dev,
				     "Xilinx Slave Serial FPGA Manager",
				     &xilinx_core_ops, core);
	return PTR_ERR_OR_ZERO(mgr);
}
EXPORT_SYMBOL_GPL(xilinx_core_probe);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_DESCRIPTION("Xilinx 7 Series FPGA manager core");
