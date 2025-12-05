// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek HDMI v2 Display Data Channel Driver
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2021 BayLibre, SAS
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <drm/drm_edid.h>

#include "mtk_hdmi_common.h"
#include "mtk_hdmi_regs_v2.h"

#define DDC2_DLY_CNT 572 /* BIM=208M/(v*4) = 90Khz */
#define DDC2_DLY_CNT_EDID 832 /* BIM=208M/(v*4) = 62.5Khz */
#define SI2C_ADDR_READ 0xf4
#define SCDC_I2C_SLAVE_ADDRESS 0x54

struct mtk_hdmi_ddc {
	struct device *dev;
	struct regmap *regs;
	struct clk *clk;
	struct i2c_adapter adap;
};

static int mtk_ddc_check_and_rise_low_bus(struct mtk_hdmi_ddc *ddc)
{
	u32 val;

	regmap_read(ddc->regs, HDCP2X_DDCM_STATUS, &val);
	if (val & DDC_I2C_BUS_LOW) {
		regmap_update_bits(ddc->regs, DDC_CTRL, DDC_CTRL_CMD,
				   FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_CLOCK_SCL));
		usleep_range(250, 300);
	}

	if (val & DDC_I2C_NO_ACK) {
		u32 ddc_ctrl, hpd_ddc_ctrl, hpd_ddc_status;

		regmap_read(ddc->regs, DDC_CTRL, &ddc_ctrl);
		regmap_read(ddc->regs, HPD_DDC_CTRL, &hpd_ddc_ctrl);
		regmap_read(ddc->regs, HPD_DDC_STATUS, &hpd_ddc_status);
	}

	if (val & DDC_I2C_NO_ACK)
		return -EIO;

	return 0;
}

static int mtk_ddcm_write_hdmi(struct mtk_hdmi_ddc *ddc, u16 addr_id,
			       u16 offset_id, u16 data_cnt, u8 *wr_data)
{
	u32 val;
	int ret, i;

	/* Don't allow transfer with a size over than the transfer fifo size
	 * (16 byte)
	 */
	if (data_cnt > 16) {
		dev_err(ddc->dev, "Invalid DDCM write request\n");
		return -EINVAL;
	}

	/* If down, rise bus for write operation */
	mtk_ddc_check_and_rise_low_bus(ddc);

	regmap_update_bits(ddc->regs, HPD_DDC_CTRL, HPD_DDC_DELAY_CNT,
			   FIELD_PREP(HPD_DDC_DELAY_CNT, DDC2_DLY_CNT));

	/* In case there is no payload data, just do a single write for the
	 * address only
	 */
	if (wr_data) {
		/* Fill transfer fifo with payload data */
		for (i = 0; i < data_cnt; i++) {
			regmap_write(ddc->regs, SI2C_CTRL,
				     FIELD_PREP(SI2C_ADDR, SI2C_ADDR_READ) |
				     FIELD_PREP(SI2C_WDATA, wr_data[i]) |
				     SI2C_WR);
		}
	}
	regmap_write(ddc->regs, DDC_CTRL,
		     FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_SEQ_WRITE) |
		     FIELD_PREP(DDC_CTRL_DIN_CNT, wr_data == NULL ? 0 : data_cnt) |
		     FIELD_PREP(DDC_CTRL_OFFSET, offset_id) |
		     FIELD_PREP(DDC_CTRL_ADDR, addr_id));
	usleep_range(1000, 1250);

	ret = regmap_read_poll_timeout(ddc->regs, HPD_DDC_STATUS, val,
				       !(val & DDC_I2C_IN_PROG), 500, 1000);
	if (ret) {
		dev_err(ddc->dev, "DDC I2C write timeout\n");

		/* Abort transfer if it is still in progress */
		regmap_update_bits(ddc->regs, DDC_CTRL, DDC_CTRL_CMD,
				   FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_ABORT_XFER));

		return ret;
	}

	/* The I2C bus might be down after WR operation: rise it again */
	ret = mtk_ddc_check_and_rise_low_bus(ddc);
	if (ret) {
		dev_err(ddc->dev, "Error during write operation: No ACK\n");
		return ret;
	}

	return 0;
}

static int mtk_ddcm_read_hdmi(struct mtk_hdmi_ddc *ddc, u16 uc_dev,
			      u8 addr, u8 *puc_value, u16 data_cnt)
{
	u16 dly_cnt, i, uc_idx;
	u32 rem, temp_length, uc_read_count, val;
	u64 loop_counter;
	int ret;

	mtk_ddc_check_and_rise_low_bus(ddc);

	regmap_update_bits(ddc->regs, DDC_CTRL, DDC_CTRL_CMD,
			   FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_CLEAR_FIFO));

	if (data_cnt >= 16) {
		temp_length = 16;
		loop_counter = data_cnt;

		rem = do_div(loop_counter, temp_length);
		if (rem)
			loop_counter++;
	} else {
		temp_length = data_cnt;
		loop_counter = 1;
	}

	if (uc_dev >= DDC_ADDR)
		dly_cnt = DDC2_DLY_CNT_EDID;
	else
		dly_cnt = DDC2_DLY_CNT;

	regmap_update_bits(ddc->regs, HPD_DDC_CTRL, HPD_DDC_DELAY_CNT,
			   FIELD_PREP(HPD_DDC_DELAY_CNT, dly_cnt));

	for (i = 0; i < loop_counter; i++) {
		rem = data_cnt % 16;

		if (i > 0 && i == (loop_counter - 1) && rem)
			temp_length = rem;

		/* 0x51 - 0x53: Flow control */
		if (uc_dev > DDC_ADDR && uc_dev <= 0x53) {
			regmap_update_bits(ddc->regs, SCDC_CTRL, SCDC_DDC_SEGMENT,
					   FIELD_PREP(SCDC_DDC_SEGMENT, uc_dev - DDC_ADDR));

			regmap_write(ddc->regs, DDC_CTRL,
				     FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_ENH_READ_NOACK) |
				     FIELD_PREP(DDC_CTRL_DIN_CNT, temp_length) |
				     FIELD_PREP(DDC_CTRL_OFFSET, addr + i * temp_length) |
				     FIELD_PREP(DDC_CTRL_ADDR, DDC_ADDR));
		} else {
			u16 offset;

			if (addr != 0x43)
				offset = i * 16;
			else
				offset = 0;

			regmap_write(ddc->regs, DDC_CTRL,
				     FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_SEQ_READ_NOACK) |
				     FIELD_PREP(DDC_CTRL_DIN_CNT, temp_length) |
				     FIELD_PREP(DDC_CTRL_OFFSET, addr + offset) |
				     FIELD_PREP(DDC_CTRL_ADDR, uc_dev));
		}
		usleep_range(5000, 5500);

		ret = regmap_read_poll_timeout(ddc->regs, HPD_DDC_STATUS, val,
					       !(val & DDC_I2C_IN_PROG), 1000,
					       500 * (temp_length + 5));
		if (ret) {
			dev_err(ddc->dev, "Timeout waiting for DDC I2C\n");

			/* Abort transfer if it is still in progress */
			regmap_update_bits(ddc->regs, DDC_CTRL, DDC_CTRL_CMD,
					   FIELD_PREP(DDC_CTRL_CMD, DDC_CMD_ABORT_XFER));

			return ret;
		}

		ret = mtk_ddc_check_and_rise_low_bus(ddc);
		if (ret) {
			dev_err(ddc->dev, "Error during read operation: No ACK\n");
			return ret;
		}

		for (uc_idx = 0; uc_idx < temp_length; uc_idx++) {
			unsigned int read_idx = i * 16 + uc_idx;

			regmap_write(ddc->regs, SI2C_CTRL,
				     FIELD_PREP(SI2C_ADDR, SI2C_ADDR_READ) |
				     SI2C_RD);

			regmap_read(ddc->regs, HPD_DDC_STATUS, &val);
			puc_value[read_idx] = FIELD_GET(DDC_DATA_OUT, val);

			regmap_write(ddc->regs, SI2C_CTRL,
				     FIELD_PREP(SI2C_ADDR, SI2C_ADDR_READ) |
				     SI2C_CONFIRM_READ);

			/*
			 * If HDMI IP gets reset during EDID read, DDC read
			 * operation will fail and its delay counter will be
			 * reset to 400.
			 */
			regmap_read(ddc->regs, HPD_DDC_CTRL, &val);
			if (FIELD_GET(HPD_DDC_DELAY_CNT, val) < DDC2_DLY_CNT)
				return 0;

			uc_read_count = read_idx + 1;
		}
	}
	if (uc_read_count > U8_MAX)
		dev_warn(ddc->dev, "Invalid read data count %u\n", uc_read_count);

	return uc_read_count;
}

static int mtk_hdmi_fg_ddc_data_read(struct mtk_hdmi_ddc *ddc, u16 b_dev,
				     u8 data_addr, u16 data_cnt, u8 *pr_data)
{
	int read_data_cnt;
	u16 req_data_cnt;

	if (!data_cnt) {
		dev_err(ddc->dev, "Invalid DDCM read request\n");
		return -EINVAL;
	}

	req_data_cnt = U8_MAX - data_addr + 1;
	if (req_data_cnt > data_cnt)
		req_data_cnt = data_cnt;

	regmap_set_bits(ddc->regs, HDCP2X_POL_CTRL, HDCP2X_DIS_POLL_EN);

	read_data_cnt = mtk_ddcm_read_hdmi(ddc, b_dev, data_addr, pr_data, req_data_cnt);

	if (read_data_cnt < 0)
		return read_data_cnt;
	else if (read_data_cnt != req_data_cnt)
		return -EINVAL;

	return 0;
}

static int mtk_hdmi_ddc_fg_data_write(struct mtk_hdmi_ddc *ddc, u16 b_dev,
				      u8 data_addr, u16 data_cnt, u8 *pr_data)
{
	regmap_set_bits(ddc->regs, HDCP2X_POL_CTRL, HDCP2X_DIS_POLL_EN);

	return mtk_ddcm_write_hdmi(ddc, b_dev, data_addr, data_cnt, pr_data);
}

static int mtk_hdmi_ddc_v2_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct mtk_hdmi_ddc *ddc;
	u8 offset = 0;
	int i, ret;

	ddc = adapter->algo_data;

	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (!msg->buf) {
			dev_err(ddc->dev, "No message buffer\n");
			return -EINVAL;
		}

		if (msg->flags & I2C_M_RD) {
			/*
			 * The underlying DDC hardware always issues a write request
			 * that assigns the read offset as part of the read operation,
			 * therefore, use the `offset` value assigned in the previous
			 * write request from drm_edid
			 */
			ret = mtk_hdmi_fg_ddc_data_read(ddc, msg->addr, offset,
							msg->len, &msg->buf[0]);
			if (ret)
				return ret;
		} else {
			/*
			 * The HW needs the data offset, found in buf[0], in the
			 * DDC_CTRL register, and each byte of data, starting at
			 * buf[1], goes in the SI2C_WDATA register.
			 */
			ret = mtk_hdmi_ddc_fg_data_write(ddc, msg->addr, msg->buf[0],
							 msg->len - 1, &msg->buf[1]);
			if (ret)
				return ret;

			/*
			 * Store the offset value requested by drm_edid or by
			 * scdc to use in subsequent read requests.
			 */
			if ((msg->addr == DDC_ADDR || msg->addr == SCDC_I2C_SLAVE_ADDRESS) &&
			    msg->len == 1) {
				offset = msg->buf[0];
			}
		}
	}

	return i;
}

static u32 mtk_hdmi_ddc_v2_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mtk_hdmi_ddc_v2_algorithm = {
	.master_xfer = mtk_hdmi_ddc_v2_xfer,
	.functionality = mtk_hdmi_ddc_v2_func,
};

static int mtk_hdmi_ddc_v2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_hdmi_ddc *ddc;
	int ret;

	ddc = devm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	ddc->dev = dev;
	ddc->regs = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR_OR_NULL(ddc->regs))
		return dev_err_probe(dev,
				     IS_ERR(ddc->regs) ? PTR_ERR(ddc->regs) : -EINVAL,
				     "Cannot get regmap\n");

	ddc->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(ddc->clk))
		return dev_err_probe(dev, PTR_ERR(ddc->clk), "Cannot get DDC clock\n");

	strscpy(ddc->adap.name, "mediatek-hdmi-ddc-v2", sizeof(ddc->adap.name));
	ddc->adap.owner = THIS_MODULE;
	ddc->adap.algo = &mtk_hdmi_ddc_v2_algorithm;
	ddc->adap.retries = 3;
	ddc->adap.dev.of_node = dev->of_node;
	ddc->adap.algo_data = ddc;
	ddc->adap.dev.parent = &pdev->dev;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Cannot enable Runtime PM\n");

	pm_runtime_get_sync(dev);

	ret = devm_i2c_add_adapter(dev, &ddc->adap);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot add DDC I2C adapter\n");

	platform_set_drvdata(pdev, ddc);
	return 0;
}

static const struct of_device_id mtk_hdmi_ddc_v2_match[] = {
	{ .compatible = "mediatek,mt8195-hdmi-ddc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_hdmi_ddc_v2_match);

struct platform_driver mtk_hdmi_ddc_v2_driver = {
	.probe = mtk_hdmi_ddc_v2_probe,
	.driver = {
		.name = "mediatek-hdmi-ddc-v2",
		.of_match_table = mtk_hdmi_ddc_v2_match,
	},
};
module_platform_driver(mtk_hdmi_ddc_v2_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Can Zeng <can.zeng@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMIv2 DDC Driver");
MODULE_LICENSE("GPL");
