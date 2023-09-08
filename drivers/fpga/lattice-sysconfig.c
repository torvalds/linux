// SPDX-License-Identifier: GPL-2.0
/*
 * Lattice FPGA sysCONFIG interface functions independent of port type.
 */

#include <linux/delay.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>

#include "lattice-sysconfig.h"

static int sysconfig_cmd_write(struct sysconfig_priv *priv, const void *buf,
			       size_t buf_len)
{
	return priv->command_transfer(priv, buf, buf_len, NULL, 0);
}

static int sysconfig_cmd_read(struct sysconfig_priv *priv, const void *tx_buf,
			      size_t tx_len, void *rx_buf, size_t rx_len)
{
	return priv->command_transfer(priv, tx_buf, tx_len, rx_buf, rx_len);
}

static int sysconfig_read_busy(struct sysconfig_priv *priv)
{
	const u8 lsc_check_busy[] = SYSCONFIG_LSC_CHECK_BUSY;
	u8 busy;
	int ret;

	ret = sysconfig_cmd_read(priv, lsc_check_busy, sizeof(lsc_check_busy),
				 &busy, sizeof(busy));

	return ret ? : busy;
}

static int sysconfig_poll_busy(struct sysconfig_priv *priv)
{
	int ret, busy;

	ret = read_poll_timeout(sysconfig_read_busy, busy, busy <= 0,
				SYSCONFIG_POLL_INTERVAL_US,
				SYSCONFIG_POLL_BUSY_TIMEOUT_US, false, priv);

	return ret ? : busy;
}

static int sysconfig_read_status(struct sysconfig_priv *priv, u32 *status)
{
	const u8 lsc_read_status[] = SYSCONFIG_LSC_READ_STATUS;
	__be32 device_status;
	int ret;

	ret = sysconfig_cmd_read(priv, lsc_read_status, sizeof(lsc_read_status),
				 &device_status, sizeof(device_status));
	if (ret)
		return ret;

	*status = be32_to_cpu(device_status);

	return 0;
}

static int sysconfig_poll_status(struct sysconfig_priv *priv, u32 *status)
{
	int ret = sysconfig_poll_busy(priv);

	if (ret)
		return ret;

	return sysconfig_read_status(priv, status);
}

static int sysconfig_poll_gpio(struct gpio_desc *gpio, bool is_active)
{
	int ret, val;

	ret = read_poll_timeout(gpiod_get_value, val,
				val < 0 || !!val == is_active,
				SYSCONFIG_POLL_INTERVAL_US,
				SYSCONFIG_POLL_GPIO_TIMEOUT_US, false, gpio);

	if (val < 0)
		return val;

	return ret;
}

static int sysconfig_gpio_refresh(struct sysconfig_priv *priv)
{
	struct gpio_desc *program = priv->program;
	struct gpio_desc *init = priv->init;
	struct gpio_desc *done = priv->done;
	int ret;

	/* Enter init mode */
	gpiod_set_value(program, 1);

	ret = sysconfig_poll_gpio(init, true);
	if (!ret)
		ret = sysconfig_poll_gpio(done, false);

	if (ret)
		return ret;

	/* Enter program mode */
	gpiod_set_value(program, 0);

	return sysconfig_poll_gpio(init, false);
}

static int sysconfig_lsc_refresh(struct sysconfig_priv *priv)
{
	static const u8 lsc_refresh[] = SYSCONFIG_LSC_REFRESH;
	int ret;

	ret = sysconfig_cmd_write(priv, lsc_refresh, sizeof(lsc_refresh));
	if (ret)
		return ret;

	usleep_range(4000, 8000);

	return 0;
}

static int sysconfig_refresh(struct sysconfig_priv *priv)
{
	struct gpio_desc *program = priv->program;
	struct gpio_desc *init = priv->init;
	struct gpio_desc *done = priv->done;

	if (program && init && done)
		return sysconfig_gpio_refresh(priv);

	return sysconfig_lsc_refresh(priv);
}

static int sysconfig_isc_enable(struct sysconfig_priv *priv)
{
	u8 isc_enable[] = SYSCONFIG_ISC_ENABLE;
	u32 status;
	int ret;

	ret = sysconfig_cmd_write(priv, isc_enable, sizeof(isc_enable));
	if (ret)
		return ret;

	ret = sysconfig_poll_status(priv, &status);
	if (ret)
		return ret;

	if (status & SYSCONFIG_STATUS_FAIL)
		return -EFAULT;

	return 0;
}

static int sysconfig_isc_erase(struct sysconfig_priv *priv)
{
	u8 isc_erase[] = SYSCONFIG_ISC_ERASE;
	u32 status;
	int ret;

	ret = sysconfig_cmd_write(priv, isc_erase, sizeof(isc_erase));
	if (ret)
		return ret;

	ret = sysconfig_poll_status(priv, &status);
	if (ret)
		return ret;

	if (status & SYSCONFIG_STATUS_FAIL)
		return -EFAULT;

	return 0;
}

static int sysconfig_isc_init(struct sysconfig_priv *priv)
{
	int ret = sysconfig_isc_enable(priv);

	if (ret)
		return ret;

	return sysconfig_isc_erase(priv);
}

static int sysconfig_lsc_init_addr(struct sysconfig_priv *priv)
{
	const u8 lsc_init_addr[] = SYSCONFIG_LSC_INIT_ADDR;

	return sysconfig_cmd_write(priv, lsc_init_addr, sizeof(lsc_init_addr));
}

static int sysconfig_burst_write_init(struct sysconfig_priv *priv)
{
	return priv->bitstream_burst_write_init(priv);
}

static int sysconfig_burst_write_complete(struct sysconfig_priv *priv)
{
	return priv->bitstream_burst_write_complete(priv);
}

static int sysconfig_bitstream_burst_write(struct sysconfig_priv *priv,
					   const char *buf, size_t count)
{
	int ret = priv->bitstream_burst_write(priv, buf, count);

	if (ret)
		sysconfig_burst_write_complete(priv);

	return ret;
}

static int sysconfig_isc_disable(struct sysconfig_priv *priv)
{
	const u8 isc_disable[] = SYSCONFIG_ISC_DISABLE;

	return sysconfig_cmd_write(priv, isc_disable, sizeof(isc_disable));
}

static void sysconfig_cleanup(struct sysconfig_priv *priv)
{
	sysconfig_isc_erase(priv);
	sysconfig_refresh(priv);
}

static int sysconfig_isc_finish(struct sysconfig_priv *priv)
{
	struct gpio_desc *done_gpio = priv->done;
	u32 status;
	int ret;

	if (done_gpio) {
		ret = sysconfig_isc_disable(priv);
		if (ret)
			return ret;

		return sysconfig_poll_gpio(done_gpio, true);
	}

	ret = sysconfig_poll_status(priv, &status);
	if (ret)
		return ret;

	if ((status & SYSCONFIG_STATUS_DONE) &&
	    !(status & SYSCONFIG_STATUS_BUSY) &&
	    !(status & SYSCONFIG_STATUS_ERR))
		return sysconfig_isc_disable(priv);

	return -EFAULT;
}

static enum fpga_mgr_states sysconfig_ops_state(struct fpga_manager *mgr)
{
	struct sysconfig_priv *priv = mgr->priv;
	struct gpio_desc *done = priv->done;
	u32 status;
	int ret;

	if (done && (gpiod_get_value(done) > 0))
		return FPGA_MGR_STATE_OPERATING;

	ret = sysconfig_read_status(priv, &status);
	if (!ret && (status & SYSCONFIG_STATUS_DONE))
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static int sysconfig_ops_write_init(struct fpga_manager *mgr,
				    struct fpga_image_info *info,
				    const char *buf, size_t count)
{
	struct sysconfig_priv *priv = mgr->priv;
	struct device *dev = &mgr->dev;
	int ret;

	if (info->flags & FPGA_MGR_PARTIAL_RECONFIG) {
		dev_err(dev, "Partial reconfiguration is not supported\n");
		return -EOPNOTSUPP;
	}

	/* Enter program mode */
	ret = sysconfig_refresh(priv);
	if (ret) {
		dev_err(dev, "Failed to go to program mode\n");
		return ret;
	}

	/* Enter ISC mode */
	ret = sysconfig_isc_init(priv);
	if (ret) {
		dev_err(dev, "Failed to go to ISC mode\n");
		return ret;
	}

	/* Initialize the Address Shift Register */
	ret = sysconfig_lsc_init_addr(priv);
	if (ret) {
		dev_err(dev,
			"Failed to initialize the Address Shift Register\n");
		return ret;
	}

	/* Prepare for bitstream burst write */
	ret = sysconfig_burst_write_init(priv);
	if (ret)
		dev_err(dev, "Failed to prepare for bitstream burst write\n");

	return ret;
}

static int sysconfig_ops_write(struct fpga_manager *mgr, const char *buf,
			       size_t count)
{
	return sysconfig_bitstream_burst_write(mgr->priv, buf, count);
}

static int sysconfig_ops_write_complete(struct fpga_manager *mgr,
					struct fpga_image_info *info)
{
	struct sysconfig_priv *priv = mgr->priv;
	struct device *dev = &mgr->dev;
	int ret;

	ret = sysconfig_burst_write_complete(priv);
	if (!ret)
		ret = sysconfig_poll_busy(priv);

	if (ret) {
		dev_err(dev, "Error while waiting bitstream write to finish\n");
		goto fail;
	}

	ret = sysconfig_isc_finish(priv);

fail:
	if (ret)
		sysconfig_cleanup(priv);

	return ret;
}

static const struct fpga_manager_ops sysconfig_fpga_mgr_ops = {
	.state = sysconfig_ops_state,
	.write_init = sysconfig_ops_write_init,
	.write = sysconfig_ops_write,
	.write_complete = sysconfig_ops_write_complete,
};

int sysconfig_probe(struct sysconfig_priv *priv)
{
	struct gpio_desc *program, *init, *done;
	struct device *dev = priv->dev;
	struct fpga_manager *mgr;

	if (!dev)
		return -ENODEV;

	if (!priv->command_transfer ||
	    !priv->bitstream_burst_write_init ||
	    !priv->bitstream_burst_write ||
	    !priv->bitstream_burst_write_complete) {
		dev_err(dev, "Essential callback is missing\n");
		return -EINVAL;
	}

	program = devm_gpiod_get_optional(dev, "program", GPIOD_OUT_LOW);
	if (IS_ERR(program))
		return dev_err_probe(dev, PTR_ERR(program),
				     "Failed to get PROGRAM GPIO\n");

	init = devm_gpiod_get_optional(dev, "init", GPIOD_IN);
	if (IS_ERR(init))
		return dev_err_probe(dev, PTR_ERR(init),
				     "Failed to get INIT GPIO\n");

	done = devm_gpiod_get_optional(dev, "done", GPIOD_IN);
	if (IS_ERR(done))
		return dev_err_probe(dev, PTR_ERR(done),
				     "Failed to get DONE GPIO\n");

	priv->program = program;
	priv->init = init;
	priv->done = done;

	mgr = devm_fpga_mgr_register(dev, "Lattice sysCONFIG FPGA Manager",
				     &sysconfig_fpga_mgr_ops, priv);

	return PTR_ERR_OR_ZERO(mgr);
}
EXPORT_SYMBOL(sysconfig_probe);

MODULE_DESCRIPTION("Lattice sysCONFIG FPGA Manager Core");
MODULE_LICENSE("GPL");
