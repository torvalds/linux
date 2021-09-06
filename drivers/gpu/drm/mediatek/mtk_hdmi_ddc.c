// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#define SIF1_CLOK		(288)
#define DDC_DDCMCTL0		(0x0)
#define DDCM_ODRAIN			BIT(31)
#define DDCM_CLK_DIV_OFFSET		(16)
#define DDCM_CLK_DIV_MASK		(0xfff << 16)
#define DDCM_CS_STATUS			BIT(4)
#define DDCM_SCL_STATE			BIT(3)
#define DDCM_SDA_STATE			BIT(2)
#define DDCM_SM0EN			BIT(1)
#define DDCM_SCL_STRECH			BIT(0)
#define DDC_DDCMCTL1		(0x4)
#define DDCM_ACK_OFFSET			(16)
#define DDCM_ACK_MASK			(0xff << 16)
#define DDCM_PGLEN_OFFSET		(8)
#define DDCM_PGLEN_MASK			(0x7 << 8)
#define DDCM_SIF_MODE_OFFSET		(4)
#define DDCM_SIF_MODE_MASK		(0x7 << 4)
#define DDCM_START			(0x1)
#define DDCM_WRITE_DATA			(0x2)
#define DDCM_STOP			(0x3)
#define DDCM_READ_DATA_NO_ACK		(0x4)
#define DDCM_READ_DATA_ACK		(0x5)
#define DDCM_TRI			BIT(0)
#define DDC_DDCMD0		(0x8)
#define DDCM_DATA3			(0xff << 24)
#define DDCM_DATA2			(0xff << 16)
#define DDCM_DATA1			(0xff << 8)
#define DDCM_DATA0			(0xff << 0)
#define DDC_DDCMD1		(0xc)
#define DDCM_DATA7			(0xff << 24)
#define DDCM_DATA6			(0xff << 16)
#define DDCM_DATA5			(0xff << 8)
#define DDCM_DATA4			(0xff << 0)

struct mtk_hdmi_ddc {
	struct i2c_adapter adap;
	struct clk *clk;
	void __iomem *regs;
};

static inline void sif_set_bit(struct mtk_hdmi_ddc *ddc, unsigned int offset,
			       unsigned int val)
{
	writel(readl(ddc->regs + offset) | val, ddc->regs + offset);
}

static inline void sif_clr_bit(struct mtk_hdmi_ddc *ddc, unsigned int offset,
			       unsigned int val)
{
	writel(readl(ddc->regs + offset) & ~val, ddc->regs + offset);
}

static inline bool sif_bit_is_set(struct mtk_hdmi_ddc *ddc, unsigned int offset,
				  unsigned int val)
{
	return (readl(ddc->regs + offset) & val) == val;
}

static inline void sif_write_mask(struct mtk_hdmi_ddc *ddc, unsigned int offset,
				  unsigned int mask, unsigned int shift,
				  unsigned int val)
{
	unsigned int tmp;

	tmp = readl(ddc->regs + offset);
	tmp &= ~mask;
	tmp |= (val << shift) & mask;
	writel(tmp, ddc->regs + offset);
}

static inline unsigned int sif_read_mask(struct mtk_hdmi_ddc *ddc,
					 unsigned int offset, unsigned int mask,
					 unsigned int shift)
{
	return (readl(ddc->regs + offset) & mask) >> shift;
}

static void ddcm_trigger_mode(struct mtk_hdmi_ddc *ddc, int mode)
{
	u32 val;

	sif_write_mask(ddc, DDC_DDCMCTL1, DDCM_SIF_MODE_MASK,
		       DDCM_SIF_MODE_OFFSET, mode);
	sif_set_bit(ddc, DDC_DDCMCTL1, DDCM_TRI);
	readl_poll_timeout(ddc->regs + DDC_DDCMCTL1, val,
			   (val & DDCM_TRI) != DDCM_TRI, 4, 20000);
}

static int mtk_hdmi_ddc_read_msg(struct mtk_hdmi_ddc *ddc, struct i2c_msg *msg)
{
	struct device *dev = ddc->adap.dev.parent;
	u32 remain_count, ack_count, ack_final, read_count, temp_count;
	u32 index = 0;
	u32 ack;
	int i;

	ddcm_trigger_mode(ddc, DDCM_START);
	sif_write_mask(ddc, DDC_DDCMD0, 0xff, 0, (msg->addr << 1) | 0x01);
	sif_write_mask(ddc, DDC_DDCMCTL1, DDCM_PGLEN_MASK, DDCM_PGLEN_OFFSET,
		       0x00);
	ddcm_trigger_mode(ddc, DDCM_WRITE_DATA);
	ack = sif_read_mask(ddc, DDC_DDCMCTL1, DDCM_ACK_MASK, DDCM_ACK_OFFSET);
	dev_dbg(dev, "ack = 0x%x\n", ack);
	if (ack != 0x01) {
		dev_err(dev, "i2c ack err!\n");
		return -ENXIO;
	}

	remain_count = msg->len;
	ack_count = (msg->len - 1) / 8;
	ack_final = 0;

	while (remain_count > 0) {
		if (ack_count > 0) {
			read_count = 8;
			ack_final = 0;
			ack_count--;
		} else {
			read_count = remain_count;
			ack_final = 1;
		}

		sif_write_mask(ddc, DDC_DDCMCTL1, DDCM_PGLEN_MASK,
			       DDCM_PGLEN_OFFSET, read_count - 1);
		ddcm_trigger_mode(ddc, (ack_final == 1) ?
				  DDCM_READ_DATA_NO_ACK :
				  DDCM_READ_DATA_ACK);

		ack = sif_read_mask(ddc, DDC_DDCMCTL1, DDCM_ACK_MASK,
				    DDCM_ACK_OFFSET);
		temp_count = 0;
		while (((ack & (1 << temp_count)) != 0) && (temp_count < 8))
			temp_count++;
		if (((ack_final == 1) && (temp_count != (read_count - 1))) ||
		    ((ack_final == 0) && (temp_count != read_count))) {
			dev_err(dev, "Address NACK! ACK(0x%x)\n", ack);
			break;
		}

		for (i = read_count; i >= 1; i--) {
			int shift;
			int offset;

			if (i > 4) {
				offset = DDC_DDCMD1;
				shift = (i - 5) * 8;
			} else {
				offset = DDC_DDCMD0;
				shift = (i - 1) * 8;
			}

			msg->buf[index + i - 1] = sif_read_mask(ddc, offset,
								0xff << shift,
								shift);
		}

		remain_count -= read_count;
		index += read_count;
	}

	return 0;
}

static int mtk_hdmi_ddc_write_msg(struct mtk_hdmi_ddc *ddc, struct i2c_msg *msg)
{
	struct device *dev = ddc->adap.dev.parent;
	u32 ack;

	ddcm_trigger_mode(ddc, DDCM_START);
	sif_write_mask(ddc, DDC_DDCMD0, DDCM_DATA0, 0, msg->addr << 1);
	sif_write_mask(ddc, DDC_DDCMD0, DDCM_DATA1, 8, msg->buf[0]);
	sif_write_mask(ddc, DDC_DDCMCTL1, DDCM_PGLEN_MASK, DDCM_PGLEN_OFFSET,
		       0x1);
	ddcm_trigger_mode(ddc, DDCM_WRITE_DATA);

	ack = sif_read_mask(ddc, DDC_DDCMCTL1, DDCM_ACK_MASK, DDCM_ACK_OFFSET);
	dev_dbg(dev, "ack = %d\n", ack);

	if (ack != 0x03) {
		dev_err(dev, "i2c ack err!\n");
		return -EIO;
	}

	return 0;
}

static int mtk_hdmi_ddc_xfer(struct i2c_adapter *adapter,
			     struct i2c_msg *msgs, int num)
{
	struct mtk_hdmi_ddc *ddc = adapter->algo_data;
	struct device *dev = adapter->dev.parent;
	int ret;
	int i;

	if (!ddc) {
		dev_err(dev, "invalid arguments\n");
		return -EINVAL;
	}

	sif_set_bit(ddc, DDC_DDCMCTL0, DDCM_SCL_STRECH);
	sif_set_bit(ddc, DDC_DDCMCTL0, DDCM_SM0EN);
	sif_clr_bit(ddc, DDC_DDCMCTL0, DDCM_ODRAIN);

	if (sif_bit_is_set(ddc, DDC_DDCMCTL1, DDCM_TRI)) {
		dev_err(dev, "ddc line is busy!\n");
		return -EBUSY;
	}

	sif_write_mask(ddc, DDC_DDCMCTL0, DDCM_CLK_DIV_MASK,
		       DDCM_CLK_DIV_OFFSET, SIF1_CLOK);

	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];

		dev_dbg(dev, "i2c msg, adr:0x%x, flags:%d, len :0x%x\n",
			msg->addr, msg->flags, msg->len);

		if (msg->flags & I2C_M_RD)
			ret = mtk_hdmi_ddc_read_msg(ddc, msg);
		else
			ret = mtk_hdmi_ddc_write_msg(ddc, msg);
		if (ret < 0)
			goto xfer_end;
	}

	ddcm_trigger_mode(ddc, DDCM_STOP);

	return i;

xfer_end:
	ddcm_trigger_mode(ddc, DDCM_STOP);
	dev_err(dev, "ddc failed!\n");
	return ret;
}

static u32 mtk_hdmi_ddc_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mtk_hdmi_ddc_algorithm = {
	.master_xfer = mtk_hdmi_ddc_xfer,
	.functionality = mtk_hdmi_ddc_func,
};

static int mtk_hdmi_ddc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_hdmi_ddc *ddc;
	struct resource *mem;
	int ret;

	ddc = devm_kzalloc(dev, sizeof(struct mtk_hdmi_ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	ddc->clk = devm_clk_get(dev, "ddc-i2c");
	if (IS_ERR(ddc->clk)) {
		dev_err(dev, "get ddc_clk failed: %p ,\n", ddc->clk);
		return PTR_ERR(ddc->clk);
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ddc->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(ddc->regs))
		return PTR_ERR(ddc->regs);

	ret = clk_prepare_enable(ddc->clk);
	if (ret) {
		dev_err(dev, "enable ddc clk failed!\n");
		return ret;
	}

	strlcpy(ddc->adap.name, "mediatek-hdmi-ddc", sizeof(ddc->adap.name));
	ddc->adap.owner = THIS_MODULE;
	ddc->adap.class = I2C_CLASS_DDC;
	ddc->adap.algo = &mtk_hdmi_ddc_algorithm;
	ddc->adap.retries = 3;
	ddc->adap.dev.of_node = dev->of_node;
	ddc->adap.algo_data = ddc;
	ddc->adap.dev.parent = &pdev->dev;

	ret = i2c_add_adapter(&ddc->adap);
	if (ret < 0) {
		dev_err(dev, "failed to add bus to i2c core\n");
		goto err_clk_disable;
	}

	platform_set_drvdata(pdev, ddc);

	dev_dbg(dev, "ddc->adap: %p\n", &ddc->adap);
	dev_dbg(dev, "ddc->clk: %p\n", ddc->clk);
	dev_dbg(dev, "physical adr: %pa, end: %pa\n", &mem->start,
		&mem->end);

	return 0;

err_clk_disable:
	clk_disable_unprepare(ddc->clk);
	return ret;
}

static int mtk_hdmi_ddc_remove(struct platform_device *pdev)
{
	struct mtk_hdmi_ddc *ddc = platform_get_drvdata(pdev);

	i2c_del_adapter(&ddc->adap);
	clk_disable_unprepare(ddc->clk);

	return 0;
}

static const struct of_device_id mtk_hdmi_ddc_match[] = {
	{ .compatible = "mediatek,mt8173-hdmi-ddc", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_hdmi_ddc_match);

struct platform_driver mtk_hdmi_ddc_driver = {
	.probe = mtk_hdmi_ddc_probe,
	.remove = mtk_hdmi_ddc_remove,
	.driver = {
		.name = "mediatek-hdmi-ddc",
		.of_match_table = mtk_hdmi_ddc_match,
	},
};

MODULE_AUTHOR("Jie Qiu <jie.qiu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI DDC Driver");
MODULE_LICENSE("GPL v2");
