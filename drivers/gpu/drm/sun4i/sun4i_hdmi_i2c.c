// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Maxime Ripard <maxime.ripard@free-electrons.com>
 * Copyright (C) 2017 Jonathan Liu <net147@gmail.com>
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>

#include "sun4i_hdmi.h"

#define SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK ( \
	SUN4I_HDMI_DDC_INT_STATUS_ILLEGAL_FIFO_OPERATION | \
	SUN4I_HDMI_DDC_INT_STATUS_DDC_RX_FIFO_UNDERFLOW | \
	SUN4I_HDMI_DDC_INT_STATUS_DDC_TX_FIFO_OVERFLOW | \
	SUN4I_HDMI_DDC_INT_STATUS_ARBITRATION_ERROR | \
	SUN4I_HDMI_DDC_INT_STATUS_ACK_ERROR | \
	SUN4I_HDMI_DDC_INT_STATUS_BUS_ERROR \
)

/* FIFO request bit is set when FIFO level is above RX_THRESHOLD during read */
#define RX_THRESHOLD SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MAX

static int fifo_transfer(struct sun4i_hdmi *hdmi, u8 *buf, int len, bool read)
{
	/*
	 * 1 byte takes 9 clock cycles (8 bits + 1 ACK) = 90 us for 100 kHz
	 * clock. As clock rate is fixed, just round it up to 100 us.
	 */
	const unsigned long byte_time_ns = 100;
	const u32 mask = SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK |
			 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST |
			 SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE;
	u32 reg;
	/*
	 * If threshold is inclusive, then the FIFO may only have
	 * RX_THRESHOLD number of bytes, instead of RX_THRESHOLD + 1.
	 */
	int read_len = RX_THRESHOLD +
		(hdmi->variant->ddc_fifo_thres_incl ? 0 : 1);

	/*
	 * Limit transfer length by FIFO threshold or FIFO size.
	 * For TX the threshold is for an empty FIFO.
	 */
	len = min_t(int, len, read ? read_len : SUN4I_HDMI_DDC_FIFO_SIZE);

	/* Wait until error, FIFO request bit set or transfer complete */
	if (regmap_field_read_poll_timeout(hdmi->field_ddc_int_status, reg,
					   reg & mask, len * byte_time_ns,
					   100000))
		return -ETIMEDOUT;

	if (reg & SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK)
		return -EIO;

	if (read)
		ioread8_rep(hdmi->base + hdmi->variant->ddc_fifo_reg, buf, len);
	else
		iowrite8_rep(hdmi->base + hdmi->variant->ddc_fifo_reg, buf, len);

	/* Clear FIFO request bit by forcing a write to that bit */
	regmap_field_force_write(hdmi->field_ddc_int_status,
				 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST);

	return len;
}

static int xfer_msg(struct sun4i_hdmi *hdmi, struct i2c_msg *msg)
{
	int i, len;
	u32 reg;

	/* Set FIFO direction */
	if (hdmi->variant->ddc_fifo_has_dir) {
		reg = readl(hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);
		reg &= ~SUN4I_HDMI_DDC_CTRL_FIFO_DIR_MASK;
		reg |= (msg->flags & I2C_M_RD) ?
		       SUN4I_HDMI_DDC_CTRL_FIFO_DIR_READ :
		       SUN4I_HDMI_DDC_CTRL_FIFO_DIR_WRITE;
		writel(reg, hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);
	}

	/* Clear address register (not cleared by soft reset) */
	regmap_field_write(hdmi->field_ddc_addr_reg, 0);

	/* Set I2C address */
	regmap_field_write(hdmi->field_ddc_slave_addr, msg->addr);

	/*
	 * Set FIFO RX/TX thresholds and clear FIFO
	 *
	 * If threshold is inclusive, we can set the TX threshold to
	 * 0 instead of 1.
	 */
	regmap_field_write(hdmi->field_ddc_fifo_tx_thres,
			   hdmi->variant->ddc_fifo_thres_incl ? 0 : 1);
	regmap_field_write(hdmi->field_ddc_fifo_rx_thres, RX_THRESHOLD);
	regmap_field_write(hdmi->field_ddc_fifo_clear, 1);
	if (regmap_field_read_poll_timeout(hdmi->field_ddc_fifo_clear,
					   reg, !reg, 100, 2000))
		return -EIO;

	/* Set transfer length */
	regmap_field_write(hdmi->field_ddc_byte_count, msg->len);

	/* Set command */
	regmap_field_write(hdmi->field_ddc_cmd,
			   msg->flags & I2C_M_RD ?
			   SUN4I_HDMI_DDC_CMD_IMPLICIT_READ :
			   SUN4I_HDMI_DDC_CMD_IMPLICIT_WRITE);

	/* Clear interrupt status bits by forcing a write */
	regmap_field_force_write(hdmi->field_ddc_int_status,
				 SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK |
				 SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST |
				 SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE);

	/* Start command */
	regmap_field_write(hdmi->field_ddc_start, 1);

	/* Transfer bytes */
	for (i = 0; i < msg->len; i += len) {
		len = fifo_transfer(hdmi, msg->buf + i, msg->len - i,
				    msg->flags & I2C_M_RD);
		if (len <= 0)
			return len;
	}

	/* Wait for command to finish */
	if (regmap_field_read_poll_timeout(hdmi->field_ddc_start,
					   reg, !reg, 100, 100000))
		return -EIO;

	/* Check for errors */
	regmap_field_read(hdmi->field_ddc_int_status, &reg);
	if ((reg & SUN4I_HDMI_DDC_INT_STATUS_ERROR_MASK) ||
	    !(reg & SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE)) {
		return -EIO;
	}

	return 0;
}

static int sun4i_hdmi_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct sun4i_hdmi *hdmi = i2c_get_adapdata(adap);
	u32 reg;
	int err, i, ret = num;

	for (i = 0; i < num; i++) {
		if (!msgs[i].len)
			return -EINVAL;
		if (msgs[i].len > SUN4I_HDMI_DDC_BYTE_COUNT_MAX)
			return -EINVAL;
	}

	/* DDC clock needs to be enabled for the module to work */
	clk_prepare_enable(hdmi->ddc_clk);
	clk_set_rate(hdmi->ddc_clk, 100000);

	/* Reset I2C controller */
	regmap_field_write(hdmi->field_ddc_en, 1);
	regmap_field_write(hdmi->field_ddc_reset, 1);
	if (regmap_field_read_poll_timeout(hdmi->field_ddc_reset,
					   reg, !reg, 100, 2000)) {
		clk_disable_unprepare(hdmi->ddc_clk);
		return -EIO;
	}

	regmap_field_write(hdmi->field_ddc_sck_en, 1);
	regmap_field_write(hdmi->field_ddc_sda_en, 1);

	for (i = 0; i < num; i++) {
		err = xfer_msg(hdmi, &msgs[i]);
		if (err) {
			ret = err;
			break;
		}
	}

	clk_disable_unprepare(hdmi->ddc_clk);
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

static int sun4i_hdmi_init_regmap_fields(struct sun4i_hdmi *hdmi)
{
	hdmi->field_ddc_en =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_en);
	if (IS_ERR(hdmi->field_ddc_en))
		return PTR_ERR(hdmi->field_ddc_en);

	hdmi->field_ddc_start =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_start);
	if (IS_ERR(hdmi->field_ddc_start))
		return PTR_ERR(hdmi->field_ddc_start);

	hdmi->field_ddc_reset =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_reset);
	if (IS_ERR(hdmi->field_ddc_reset))
		return PTR_ERR(hdmi->field_ddc_reset);

	hdmi->field_ddc_addr_reg =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_addr_reg);
	if (IS_ERR(hdmi->field_ddc_addr_reg))
		return PTR_ERR(hdmi->field_ddc_addr_reg);

	hdmi->field_ddc_slave_addr =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_slave_addr);
	if (IS_ERR(hdmi->field_ddc_slave_addr))
		return PTR_ERR(hdmi->field_ddc_slave_addr);

	hdmi->field_ddc_int_mask =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_int_mask);
	if (IS_ERR(hdmi->field_ddc_int_mask))
		return PTR_ERR(hdmi->field_ddc_int_mask);

	hdmi->field_ddc_int_status =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_int_status);
	if (IS_ERR(hdmi->field_ddc_int_status))
		return PTR_ERR(hdmi->field_ddc_int_status);

	hdmi->field_ddc_fifo_clear =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_fifo_clear);
	if (IS_ERR(hdmi->field_ddc_fifo_clear))
		return PTR_ERR(hdmi->field_ddc_fifo_clear);

	hdmi->field_ddc_fifo_rx_thres =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_fifo_rx_thres);
	if (IS_ERR(hdmi->field_ddc_fifo_rx_thres))
		return PTR_ERR(hdmi->field_ddc_fifo_rx_thres);

	hdmi->field_ddc_fifo_tx_thres =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_fifo_tx_thres);
	if (IS_ERR(hdmi->field_ddc_fifo_tx_thres))
		return PTR_ERR(hdmi->field_ddc_fifo_tx_thres);

	hdmi->field_ddc_byte_count =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_byte_count);
	if (IS_ERR(hdmi->field_ddc_byte_count))
		return PTR_ERR(hdmi->field_ddc_byte_count);

	hdmi->field_ddc_cmd =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_cmd);
	if (IS_ERR(hdmi->field_ddc_cmd))
		return PTR_ERR(hdmi->field_ddc_cmd);

	hdmi->field_ddc_sda_en =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_sda_en);
	if (IS_ERR(hdmi->field_ddc_sda_en))
		return PTR_ERR(hdmi->field_ddc_sda_en);

	hdmi->field_ddc_sck_en =
		devm_regmap_field_alloc(hdmi->dev, hdmi->regmap,
					hdmi->variant->field_ddc_sck_en);
	if (IS_ERR(hdmi->field_ddc_sck_en))
		return PTR_ERR(hdmi->field_ddc_sck_en);

	return 0;
}

int sun4i_hdmi_i2c_create(struct device *dev, struct sun4i_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	int ret = 0;

	ret = sun4i_ddc_create(hdmi, hdmi->ddc_parent_clk);
	if (ret)
		return ret;

	ret = sun4i_hdmi_init_regmap_fields(hdmi);
	if (ret)
		return ret;

	adap = devm_kzalloc(dev, sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DDC;
	adap->algo = &sun4i_hdmi_i2c_algorithm;
	strlcpy(adap->name, "sun4i_hdmi_i2c adapter", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret)
		return ret;

	hdmi->i2c = adap;

	return ret;
}
