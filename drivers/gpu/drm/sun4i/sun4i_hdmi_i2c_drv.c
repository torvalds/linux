/*
 * Copyright (C) 2016 Maxime Ripard <maxime.ripard@free-electrons.com>
 * Copyright (C) 2017 Chen-Yu Tsai <wens@csie.org>
 * Copyright (C) 2017 Jonathan Liu <net147@gmail.com>
 * Copyright (C) 2017 Olliver Schinagl <oliver@schinagl.nl>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>
#include <linux/types.h>

#include "sun4i_hdmi_ddc_clk.h"
#include "sun4i_hdmi_i2c_drv.h"

#define SUN4I_HDMI_I2C_SPEED_MAX 25000000
#define SUN4I_HDMI_I2C_SPEED_DEFAULT 100000

/* FIFO request bit is set when FIFO level is above RX_THRESHOLD during read */
#define RX_THRESHOLD SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MAX

static const struct sun4i_hdmi_i2c_variant sun4i_legacy_variant = {
	.ddc_clk_reg		= REG_FIELD(SUN4I_HDMI_DDC_CLK_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 6),
	.ddc_clk_pre_divider	= 4,
	.ddc_clk_m_offset	= 1,

	.field_ddc_en		= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 31, 31),
	.field_ddc_start	= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 30, 30),
	.field_ddc_reset	= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 0),
	.field_ddc_addr_reg	= REG_FIELD(SUN4I_HDMI_DDC_ADDR_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 31),
	.field_ddc_slave_addr	= REG_FIELD(SUN4I_HDMI_DDC_ADDR_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 6),
	.field_ddc_int_status	= REG_FIELD(SUN4I_HDMI_DDC_INT_STATUS_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 8),
	.field_ddc_fifo_clear	= REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 31, 31),
	.field_ddc_fifo_rx_thres = REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 4, 7),
	.field_ddc_fifo_tx_thres = REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 3),
	.field_ddc_byte_count	= REG_FIELD(SUN4I_HDMI_DDC_BYTE_COUNT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 9),
	.field_ddc_cmd		= REG_FIELD(SUN4I_HDMI_DDC_CMD_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 2),
	.field_ddc_sda_en	= REG_FIELD(SUN4I_HDMI_DDC_LINE_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 9, 9),
	.field_ddc_sck_en	= REG_FIELD(SUN4I_HDMI_DDC_LINE_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 8, 8),
	.field_ddc_bus_busy	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 10, 10),
	.field_ddc_sda_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 9, 9),
	.field_ddc_sck_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 8, 8),
	.field_ddc_sck_line_ctrl = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				   SUN4I_HDMI_DDC_OFFSET, 3, 3),
	.field_ddc_sck_line_ctrl_en = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				      SUN4I_HDMI_DDC_OFFSET, 2, 2),
	.field_ddc_sda_line_ctrl = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				   SUN4I_HDMI_DDC_OFFSET, 1, 1),
	.field_ddc_sda_line_ctrl_en = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				      SUN4I_HDMI_DDC_OFFSET, 0, 0),

	.ddc_fifo_reg		= SUN4I_HDMI_DDC_FIFO_DATA_REG +
				  SUN4I_HDMI_DDC_OFFSET,
	.ddc_fifo_has_dir	= true,
};

static const struct sun4i_hdmi_i2c_variant sun6i_legacy_variant = {
	.parent_clk_name	= "ddc",
	.ddc_clk_reg		= REG_FIELD(SUN6I_HDMI_DDC_CLK_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 6),
	.ddc_clk_pre_divider	= 1,
	.ddc_clk_m_offset	= 2,

	.field_ddc_en		= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 0),
	.field_ddc_start	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 27, 27),
	.field_ddc_reset	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 31, 31),
	.field_ddc_addr_reg	= REG_FIELD(SUN6I_HDMI_DDC_ADDR_REG +
				  SUN4I_HDMI_DDC_OFFSET, 1, 31),
	.field_ddc_slave_addr	= REG_FIELD(SUN6I_HDMI_DDC_ADDR_REG +
				  SUN4I_HDMI_DDC_OFFSET, 1, 7),
	.field_ddc_int_status	= REG_FIELD(SUN6I_HDMI_DDC_INT_STATUS_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 8),
	.field_ddc_fifo_clear	= REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 18, 18),
	.field_ddc_fifo_rx_thres = REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 4, 7),
	.field_ddc_fifo_tx_thres = REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 3),
	.field_ddc_byte_count	= REG_FIELD(SUN6I_HDMI_DDC_CMD_REG +
				  SUN4I_HDMI_DDC_OFFSET, 16, 25),
	.field_ddc_cmd		= REG_FIELD(SUN6I_HDMI_DDC_CMD_REG +
				  SUN4I_HDMI_DDC_OFFSET, 0, 2),
	.field_ddc_sda_en	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 6, 6),
	.field_ddc_sck_en	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG +
				  SUN4I_HDMI_DDC_OFFSET, 4, 4),
	.field_ddc_bus_busy	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 10, 10),
	.field_ddc_sda_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 9, 9),
	.field_ddc_sck_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG +
				  SUN4I_HDMI_DDC_OFFSET, 8, 8),
	.field_ddc_sck_line_ctrl = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG +
				   SUN4I_HDMI_DDC_OFFSET, 3, 3),
	.field_ddc_sck_line_ctrl_en = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG +
				      SUN4I_HDMI_DDC_OFFSET, 2, 2),
	.field_ddc_sda_line_ctrl = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG +
				   SUN4I_HDMI_DDC_OFFSET, 1, 1),
	.field_ddc_sda_line_ctrl_en = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG +
				   SUN4I_HDMI_DDC_OFFSET, 0, 0),

	.ddc_fifo_reg		= SUN6I_HDMI_DDC_FIFO_DATA_REG +
				  SUN4I_HDMI_DDC_OFFSET,
	.ddc_fifo_thres_incl	= true,
};

static const struct regmap_config sun4i_hdmi_i2c_legacy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x580,
};

static int fifo_transfer(struct sun4i_hdmi_i2c_drv *drv, u8 *buf, int len, bool read)
{
	const u32 mask = SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK |
			 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST |
			 SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE;
	u32 reg;
	unsigned long byte_time_us;

	/*
	 * If threshold is inclusive, then the FIFO may only have
	 * RX_THRESHOLD number of bytes, instead of RX_THRESHOLD + 1.
	 */
	int read_len = RX_THRESHOLD +
		(drv->variant->ddc_fifo_thres_incl ? 0 : 1);

	/*
	 * Limit transfer length by FIFO threshold or FIFO size.
	 * For TX the threshold is for an empty FIFO.
	 */
	len = min_t(int, len, read ? read_len : SUN4I_HDMI_DDC_FIFO_SIZE);

	/*
	 * 1 byte takes 9 clock cycles (8 bits + 1 ACK) times the number of
	 * bytes to be transmitted. One additional 'round-up' byte is added
	 * as a margin.
	 */
	byte_time_us = (len + 1) * 9 * clk_get_rate(drv->ddc_clk) / 10000;

	/* Wait until error, FIFO request bit set or transfer complete */
	if (regmap_field_read_poll_timeout(drv->field_ddc_int_status, reg,
					   reg & mask, byte_time_us, 100000)) {
		dev_err(drv->dev, "DDC bus timeout after %lu us\n", byte_time_us);
		return -ETIMEDOUT;
	}

	if (reg & SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK)
		return -EIO;

	if (read)
		readsb(drv->base + drv->variant->ddc_fifo_reg, buf, len);
	else
		writesb(drv->base + drv->variant->ddc_fifo_reg, buf, len);

	/* Clear FIFO request bit by forcing a write to that bit */
	regmap_field_force_write(drv->field_ddc_int_status,
				 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST);

	return len;
}

static int xfer_msg(struct sun4i_hdmi_i2c_drv *drv, struct i2c_msg *msg)
{
	unsigned int bus_busy;
	int i, len;
	int err;
	u32 reg;

	err = regmap_field_read(drv->field_ddc_bus_busy, &bus_busy);
	if (err)
		return err;

	if (bus_busy) {
		dev_err(drv->dev, "failed to transfer data, bus busy\n");
		return -EAGAIN;
	}

	/* Set FIFO direction */
	if (drv->variant->ddc_fifo_has_dir) {
		reg = readl(drv->base + SUN4I_HDMI_DDC_CTRL_REG);
		reg &= ~SUN4I_HDMI_DDC_CTRL_FIFO_DIR_MASK;
		reg |= (msg->flags & I2C_M_RD) ?
		       SUN4I_HDMI_DDC_CTRL_FIFO_DIR_READ :
		       SUN4I_HDMI_DDC_CTRL_FIFO_DIR_WRITE;
		writel(reg, drv->base + SUN4I_HDMI_DDC_CTRL_REG);
	}

	/* Clear address register (not cleared by soft reset) */
	regmap_field_write(drv->field_ddc_addr_reg, 0);

	/* Set I2C address */
	regmap_field_write(drv->field_ddc_slave_addr, msg->addr);

	/*
	 * Set FIFO RX/TX thresholds and clear FIFO
	 *
	 * If threshold is inclusive, we can set the TX threshold to
	 * 0 instead of 1.
	 */
	regmap_field_write(drv->field_ddc_fifo_tx_thres,
			   drv->variant->ddc_fifo_thres_incl ? 0 : 1);
	regmap_field_write(drv->field_ddc_fifo_rx_thres, RX_THRESHOLD);
	regmap_field_write(drv->field_ddc_fifo_clear, 1);
	if (regmap_field_read_poll_timeout(drv->field_ddc_fifo_clear,
					   reg, !reg, 100, 2000))
		return -EIO;

	/* Set transfer length */
	regmap_field_write(drv->field_ddc_byte_count, msg->len);

	/* Set command */
	regmap_field_write(drv->field_ddc_cmd,
			   msg->flags & I2C_M_RD ?
			   SUN4I_HDMI_DDC_CMD_IMPLICIT_READ :
			   SUN4I_HDMI_DDC_CMD_IMPLICIT_WRITE);

	/* Clear interrupt status bits by forcing a write */
	regmap_field_force_write(drv->field_ddc_int_status,
				 SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK |
				 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST |
				 SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE);

	/* Start command */
	regmap_field_write(drv->field_ddc_start, 1);

	/* Transfer bytes */
	for (i = 0; i < msg->len; i += len) {
		len = fifo_transfer(drv, msg->buf + i, msg->len - i,
				    msg->flags & I2C_M_RD);
		if (len <= 0)
			return len;
	}

	/* Wait for command to finish */
	if (regmap_field_read_poll_timeout(drv->field_ddc_start,
					   reg, !reg, 100, 100000))
		return -EIO;

	/* Check for errors */
	regmap_field_read(drv->field_ddc_int_status, &reg);
	if ((reg & SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK) ||
	    !(reg & SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE)) {
		return -EIO;
	}

	return 0;
}

static int sun4i_hdmi_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct sun4i_hdmi_i2c_drv *drv = i2c_get_adapdata(adap);
	u32 reg;
	int err, i, ret = num;

	for (i = 0; i < num; i++) {
		if (!msgs[i].len)
			return -EINVAL;
		if (msgs[i].len > SUN4I_HDMI_DDC_BYTE_COUNT_MAX)
			return -EINVAL;
	}

	/* DDC clock needs to be enabled for the module to work */
	clk_prepare_enable(drv->ddc_clk);
	err = clk_set_rate(drv->ddc_clk, drv->clock_freq);
	if (err) {
		dev_err(drv->dev, "unable to set HDMI-I2C clock rate\n");
		ret = err;
		goto exit;
	}

	/* Reset I2C controller */
	regmap_field_write(drv->field_ddc_en, 1);
	regmap_field_write(drv->field_ddc_reset, 1);
	if (regmap_field_read_poll_timeout(drv->field_ddc_reset,
					   reg, !reg, 100, 2000)) {
		ret = -EIO;
		goto exit;
	}

	regmap_field_write(drv->field_ddc_sck_en, 1);
	regmap_field_write(drv->field_ddc_sda_en, 1);

	for (i = 0; i < num; i++) {
		err = xfer_msg(drv, &msgs[i]);
		if (err) {
			ret = err;
			break;
		}
	}

exit:
	clk_disable_unprepare(drv->ddc_clk);
	return ret;
}

static u32 sun4i_hdmi_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm sun4i_hdmi_i2c_algorithm = {
	.master_xfer	= sun4i_hdmi_i2c_xfer,
	.functionality	= sun4i_hdmi_i2c_func,
};

static int sun4i_hdmi_i2c_init_regmap_fields(struct sun4i_hdmi_i2c_drv *drv)
{
	drv->field_ddc_en =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_en);
	if (IS_ERR(drv->field_ddc_en))
		return PTR_ERR(drv->field_ddc_en);

	drv->field_ddc_start =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_start);
	if (IS_ERR(drv->field_ddc_start))
		return PTR_ERR(drv->field_ddc_start);

	drv->field_ddc_reset =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_reset);
	if (IS_ERR(drv->field_ddc_reset))
		return PTR_ERR(drv->field_ddc_reset);

	drv->field_ddc_addr_reg =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_addr_reg);
	if (IS_ERR(drv->field_ddc_addr_reg))
		return PTR_ERR(drv->field_ddc_addr_reg);

	drv->field_ddc_slave_addr =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_slave_addr);
	if (IS_ERR(drv->field_ddc_slave_addr))
		return PTR_ERR(drv->field_ddc_slave_addr);

	drv->field_ddc_int_mask =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_int_mask);
	if (IS_ERR(drv->field_ddc_int_mask))
		return PTR_ERR(drv->field_ddc_int_mask);

	drv->field_ddc_int_status =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_int_status);
	if (IS_ERR(drv->field_ddc_int_status))
		return PTR_ERR(drv->field_ddc_int_status);

	drv->field_ddc_fifo_clear =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_fifo_clear);
	if (IS_ERR(drv->field_ddc_fifo_clear))
		return PTR_ERR(drv->field_ddc_fifo_clear);

	drv->field_ddc_fifo_rx_thres =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_fifo_rx_thres);
	if (IS_ERR(drv->field_ddc_fifo_rx_thres))
		return PTR_ERR(drv->field_ddc_fifo_rx_thres);

	drv->field_ddc_fifo_tx_thres =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_fifo_tx_thres);
	if (IS_ERR(drv->field_ddc_fifo_tx_thres))
		return PTR_ERR(drv->field_ddc_fifo_tx_thres);

	drv->field_ddc_byte_count =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_byte_count);
	if (IS_ERR(drv->field_ddc_byte_count))
		return PTR_ERR(drv->field_ddc_byte_count);

	drv->field_ddc_cmd =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_cmd);
	if (IS_ERR(drv->field_ddc_cmd))
		return PTR_ERR(drv->field_ddc_cmd);

	drv->field_ddc_sda_en =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sda_en);
	if (IS_ERR(drv->field_ddc_sda_en))
		return PTR_ERR(drv->field_ddc_sda_en);

	drv->field_ddc_sck_en =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sck_en);
	if (IS_ERR(drv->field_ddc_sck_en))
		return PTR_ERR(drv->field_ddc_sck_en);

	drv->field_ddc_bus_busy =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_bus_busy);
	if (IS_ERR(drv->field_ddc_bus_busy))
		return PTR_ERR(drv->field_ddc_bus_busy);

	drv->field_ddc_sda_state =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sda_state);
	if (IS_ERR(drv->field_ddc_sda_state))
		return PTR_ERR(drv->field_ddc_sda_state);

	drv->field_ddc_sck_state =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sck_state);
	if (IS_ERR(drv->field_ddc_sck_state))
		return PTR_ERR(drv->field_ddc_sck_state);

	drv->field_ddc_sda_line_ctrl =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sda_line_ctrl);
	if (IS_ERR(drv->field_ddc_sda_line_ctrl))
		return PTR_ERR(drv->field_ddc_sda_line_ctrl);

	drv->field_ddc_sck_line_ctrl =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sck_line_ctrl);
	if (IS_ERR(drv->field_ddc_sck_line_ctrl))
		return PTR_ERR(drv->field_ddc_sck_line_ctrl);

	drv->field_ddc_sda_line_ctrl_en =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sda_line_ctrl_en);
	if (IS_ERR(drv->field_ddc_sda_line_ctrl_en))
		return PTR_ERR(drv->field_ddc_sda_line_ctrl_en);

	drv->field_ddc_sck_line_ctrl_en =
		devm_regmap_field_alloc(drv->dev, drv->regmap,
					drv->variant->field_ddc_sck_line_ctrl_en);
	if (IS_ERR(drv->field_ddc_sck_line_ctrl_en))
		return PTR_ERR(drv->field_ddc_sck_line_ctrl_en);

	return 0;
}

struct sun4i_hdmi_i2c_drv
*sun4i_hdmi_i2c_init(struct device *dev, void __iomem *base,
		     const struct of_device_id *of_id_table,
		     const struct regmap_config *regmap_config,
		     struct clk *parent_clk)
{
	struct sun4i_hdmi_i2c_drv *drv;
	const struct of_device_id *of_id;
	struct device_node *node = dev_of_node(dev);
	int ret;

	if ((!dev) || (!base) || (!regmap_config) || (!of_id_table))
		return ERR_PTR(-ENODEV);

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return ERR_PTR(-ENOMEM);

	drv->dev = dev;
	drv->base = base;

	of_id = of_match_device(of_id_table, drv->dev);
	if (!of_id) {
		dev_err(drv->dev, "missing platform data\n");
		return ERR_PTR(-ENODEV);
	}
	drv->variant = of_id->data;
	// TODO: of_dev_get_data

	if (parent_clk) {
		drv->parent_clk = parent_clk;
	} else {
		drv->parent_clk = devm_clk_get(drv->dev,
					       drv->variant->parent_clk_name);
	}
	if (IS_ERR(drv->parent_clk)) {
		if (PTR_ERR(drv->parent_clk) != -EPROBE_DEFER)
			dev_err(drv->dev, "couldn't get the HDMI-I2C clock\n");
		return ERR_CAST(drv->parent_clk);
	}

	ret = of_property_read_u32(node, "clock-frequency",
				   &drv->clock_freq);
	if (ret || (drv->clock_freq > SUN4I_HDMI_I2C_SPEED_MAX))
		drv->clock_freq = SUN4I_HDMI_I2C_SPEED_DEFAULT;

	drv->regmap = devm_regmap_init_mmio(drv->dev, drv->base, regmap_config);
	if (IS_ERR(drv->regmap)) {
		dev_err(drv->dev, "couldn't create HDMI-I2C regmap\n");
		return ERR_CAST(drv->regmap);
	}

	ret = sun4i_hdmi_i2c_init_regmap_fields(drv);
	if (ret) {
		dev_err(drv->dev, "couldn't init HDMI-I2C regmap fields\n");
		return ERR_PTR(ret);
	}

	drv->ddc_clk = sun4i_ddc_create(drv->dev, drv->regmap, drv->variant,
					drv->parent_clk);
	if (IS_ERR(drv->ddc_clk)) {
		dev_err(drv->dev, "couldn't create the HDMI-I2C clock\n");
		return ERR_CAST(drv->ddc_clk);
	}

	// TODO devm_of_clk_add_provider()
	ret = of_clk_add_provider(node, of_clk_src_simple_get, drv->ddc_clk);
	if (ret) {
		dev_err(drv->dev, "couldn't register the HDMI-I2C clock\n");
		return ERR_PTR(ret);
	}

	ret = clk_prepare_enable(drv->ddc_clk);
	if (ret) {
		dev_err(drv->dev, "unable to enable HDMI-I2C clock\n");
		return ERR_PTR(ret);
	}

	i2c_set_adapdata(&drv->adap, drv);
	drv->adap.dev.parent = drv->dev;
	drv->adap.owner = THIS_MODULE;
	drv->adap.class = I2C_CLASS_DDC;
	drv->adap.algo = &sun4i_hdmi_i2c_algorithm;
	drv->adap.dev.of_node = node;
	strlcpy(drv->adap.name, "sun4i_hdmi_i2c adapter", sizeof(drv->adap.name));

	clk_disable_unprepare(drv->ddc_clk);

	ret = i2c_add_adapter(&drv->adap);
	if (ret) {
		dev_err(drv->dev, "unable to create HDMI-I2C adapter\n");
		goto ddc_clk_err;
	}

	return drv;

ddc_clk_err:
	clk_disable_unprepare(drv->ddc_clk);

	return ERR_PTR(ret);
}

static const struct of_device_id sun4i_hdmi_i2c_legacy_of_table[] = {
	{ .compatible = "allwinner,sun4i-a10-hdmi", .data = &sun4i_legacy_variant },
	{ .compatible = "allwinner,sun6i-a31-hdmi", .data = &sun6i_legacy_variant },
	{ /* sentinel */ }
};

struct sun4i_hdmi_i2c_drv *sun4i_hdmi_i2c_setup(struct device *dev,
						void __iomem *base,
						struct clk *clk)
{
	return sun4i_hdmi_i2c_init(dev, base, sun4i_hdmi_i2c_legacy_of_table,
				   &sun4i_hdmi_i2c_legacy_regmap_config, clk);
}

void sun4i_hdmi_i2c_fini(struct sun4i_hdmi_i2c_drv *drv)
{
	struct device_node *node = dev_of_node(drv->dev);

	clk_prepare_enable(drv->ddc_clk);
	i2c_del_adapter(&drv->adap);
	clk_disable_unprepare(drv->ddc_clk);
	of_clk_del_provider(node);
}
