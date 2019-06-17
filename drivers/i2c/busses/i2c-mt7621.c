// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/i2c/busses/i2c-mt7621.c
 *
 * Copyright (C) 2013 Steven Liu <steven_liu@mediatek.com>
 * Copyright (C) 2016 Michael Lee <igvtee@gmail.com>
 * Copyright (C) 2018 Jan Breuer <jan.breuer@jaybee.cz>
 *
 * Improve driver for i2cdetect from i2c-tools to detect i2c devices on the bus.
 * (C) 2014 Sittisak <sittisaks@hotmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>

#define REG_SM0CFG2_REG		0x28
#define REG_SM0CTL0_REG		0x40
#define REG_SM0CTL1_REG		0x44
#define REG_SM0D0_REG		0x50
#define REG_SM0D1_REG		0x54
#define REG_PINTEN_REG		0x5c
#define REG_PINTST_REG		0x60
#define REG_PINTCL_REG		0x64

/* REG_SM0CFG2_REG */
#define SM0CFG2_IS_AUTOMODE	BIT(0)

/* REG_SM0CTL0_REG */
#define SM0CTL0_ODRAIN		BIT(31)
#define SM0CTL0_CLK_DIV_MASK	(0x7ff << 16)
#define SM0CTL0_CLK_DIV_MAX	0x7ff
#define SM0CTL0_CS_STATUS       BIT(4)
#define SM0CTL0_SCL_STATE       BIT(3)
#define SM0CTL0_SDA_STATE       BIT(2)
#define SM0CTL0_EN              BIT(1)
#define SM0CTL0_SCL_STRETCH     BIT(0)

/* REG_SM0CTL1_REG */
#define SM0CTL1_ACK_MASK	(0xff << 16)
#define SM0CTL1_PGLEN_MASK	(0x7 << 8)
#define SM0CTL1_PGLEN(x)	((((x) - 1) << 8) & SM0CTL1_PGLEN_MASK)
#define SM0CTL1_READ		(5 << 4)
#define SM0CTL1_READ_LAST	(4 << 4)
#define SM0CTL1_STOP		(3 << 4)
#define SM0CTL1_WRITE		(2 << 4)
#define SM0CTL1_START		(1 << 4)
#define SM0CTL1_MODE_MASK	(0x7 << 4)
#define SM0CTL1_TRI		BIT(0)

/* timeout waiting for I2C devices to respond */
#define TIMEOUT_MS		1000

struct mtk_i2c {
	void __iomem *base;
	struct device *dev;
	struct i2c_adapter adap;
	u32 bus_freq;
	u32 clk_div;
	u32 flags;
	struct clk *clk;
};

static int mtk_i2c_wait_idle(struct mtk_i2c *i2c)
{
	int ret;
	u32 val;

	ret = readl_relaxed_poll_timeout(i2c->base + REG_SM0CTL1_REG,
					 val, !(val & SM0CTL1_TRI),
					 10, TIMEOUT_MS * 1000);
	if (ret)
		dev_dbg(i2c->dev, "idle err(%d)\n", ret);

	return ret;
}

static void mtk_i2c_reset(struct mtk_i2c *i2c)
{
	int ret;

	ret = device_reset(i2c->adap.dev.parent);
	if (ret)
		dev_err(i2c->dev, "I2C reset failed!\n");

	/*
	 * Don't set SM0CTL0_ODRAIN as its bit meaning is inverted. To
	 * configure open-drain mode, this bit needs to be cleared.
	 */
	iowrite32(((i2c->clk_div << 16) & SM0CTL0_CLK_DIV_MASK) | SM0CTL0_EN |
		  SM0CTL0_SCL_STRETCH, i2c->base + REG_SM0CTL0_REG);
	iowrite32(0, i2c->base + REG_SM0CFG2_REG);
}

static void mtk_i2c_dump_reg(struct mtk_i2c *i2c)
{
	dev_dbg(i2c->dev,
		"SM0CFG2 %08x, SM0CTL0 %08x, SM0CTL1 %08x, SM0D0 %08x, SM0D1 %08x\n",
		ioread32(i2c->base + REG_SM0CFG2_REG),
		ioread32(i2c->base + REG_SM0CTL0_REG),
		ioread32(i2c->base + REG_SM0CTL1_REG),
		ioread32(i2c->base + REG_SM0D0_REG),
		ioread32(i2c->base + REG_SM0D1_REG));
}

static int mtk_i2c_check_ack(struct mtk_i2c *i2c, u32 expected)
{
	u32 ack = readl_relaxed(i2c->base + REG_SM0CTL1_REG);
	u32 ack_expected = (expected << 16) & SM0CTL1_ACK_MASK;

	return ((ack & ack_expected) == ack_expected) ? 0 : -ENXIO;
}

static int mtk_i2c_master_start(struct mtk_i2c *i2c)
{
	iowrite32(SM0CTL1_START | SM0CTL1_TRI, i2c->base + REG_SM0CTL1_REG);
	return mtk_i2c_wait_idle(i2c);
}

static int mtk_i2c_master_stop(struct mtk_i2c *i2c)
{
	iowrite32(SM0CTL1_STOP | SM0CTL1_TRI, i2c->base + REG_SM0CTL1_REG);
	return mtk_i2c_wait_idle(i2c);
}

static int mtk_i2c_master_cmd(struct mtk_i2c *i2c, u32 cmd, int page_len)
{
	iowrite32(cmd | SM0CTL1_TRI | SM0CTL1_PGLEN(page_len),
		  i2c->base + REG_SM0CTL1_REG);
	return mtk_i2c_wait_idle(i2c);
}

static int mtk_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			       int num)
{
	struct mtk_i2c *i2c;
	struct i2c_msg *pmsg;
	u16 addr;
	int i, j, ret, len, page_len;
	u32 cmd;
	u32 data[2];

	i2c = i2c_get_adapdata(adap);

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];

		/* wait hardware idle */
		ret = mtk_i2c_wait_idle(i2c);
		if (ret)
			goto err_timeout;

		/* start sequence */
		ret = mtk_i2c_master_start(i2c);
		if (ret)
			goto err_timeout;

		/* write address */
		if (pmsg->flags & I2C_M_TEN) {
			/* 10 bits address */
			addr = 0xf0 | ((pmsg->addr >> 7) & 0x06);
			addr |= (pmsg->addr & 0xff) << 8;
			if (pmsg->flags & I2C_M_RD)
				addr |= 1;
			iowrite32(addr, i2c->base + REG_SM0D0_REG);
			ret = mtk_i2c_master_cmd(i2c, SM0CTL1_WRITE, 2);
			if (ret)
				goto err_timeout;
		} else {
			/* 7 bits address */
			addr = i2c_8bit_addr_from_msg(pmsg);
			iowrite32(addr, i2c->base + REG_SM0D0_REG);
			ret = mtk_i2c_master_cmd(i2c, SM0CTL1_WRITE, 1);
			if (ret)
				goto err_timeout;
		}

		/* check address ACK */
		if (!(pmsg->flags & I2C_M_IGNORE_NAK)) {
			ret = mtk_i2c_check_ack(i2c, BIT(0));
			if (ret)
				goto err_ack;
		}

		/* transfer data */
		for (len = pmsg->len, j = 0; len > 0; len -= 8, j += 8) {
			page_len = (len >= 8) ? 8 : len;

			if (pmsg->flags & I2C_M_RD) {
				cmd = (len > 8) ?
					SM0CTL1_READ : SM0CTL1_READ_LAST;
			} else {
				memcpy(data, &pmsg->buf[j], page_len);
				iowrite32(data[0], i2c->base + REG_SM0D0_REG);
				iowrite32(data[1], i2c->base + REG_SM0D1_REG);
				cmd = SM0CTL1_WRITE;
			}

			ret = mtk_i2c_master_cmd(i2c, cmd, page_len);
			if (ret)
				goto err_timeout;

			if (pmsg->flags & I2C_M_RD) {
				data[0] = ioread32(i2c->base + REG_SM0D0_REG);
				data[1] = ioread32(i2c->base + REG_SM0D1_REG);
				memcpy(&pmsg->buf[j], data, page_len);
			} else {
				if (!(pmsg->flags & I2C_M_IGNORE_NAK)) {
					ret = mtk_i2c_check_ack(i2c,
								(1 << page_len)
								- 1);
					if (ret)
						goto err_ack;
				}
			}
		}
	}

	ret = mtk_i2c_master_stop(i2c);
	if (ret)
		goto err_timeout;

	/* the return value is number of executed messages */
	return i;

err_ack:
	ret = mtk_i2c_master_stop(i2c);
	if (ret)
		goto err_timeout;
	return -ENXIO;

err_timeout:
	mtk_i2c_dump_reg(i2c);
	mtk_i2c_reset(i2c);
	return ret;
}

static u32 mtk_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm mtk_i2c_algo = {
	.master_xfer	= mtk_i2c_master_xfer,
	.functionality	= mtk_i2c_func,
};

static const struct of_device_id i2c_mtk_dt_ids[] = {
	{ .compatible = "mediatek,mt7621-i2c" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, i2c_mtk_dt_ids);

static void mtk_i2c_init(struct mtk_i2c *i2c)
{
	i2c->clk_div = clk_get_rate(i2c->clk) / i2c->bus_freq - 1;
	if (i2c->clk_div < 99)
		i2c->clk_div = 99;
	if (i2c->clk_div > SM0CTL0_CLK_DIV_MAX)
		i2c->clk_div = SM0CTL0_CLK_DIV_MAX;

	mtk_i2c_reset(i2c);
}

static int mtk_i2c_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mtk_i2c *i2c;
	struct i2c_adapter *adap;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct mtk_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		return PTR_ERR(i2c->clk);
	}
	ret = clk_prepare_enable(i2c->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock\n");
		return ret;
	}

	i2c->dev = &pdev->dev;

	if (of_property_read_u32(pdev->dev.of_node, "clock-frequency",
				 &i2c->bus_freq))
		i2c->bus_freq = 100000;

	if (i2c->bus_freq == 0) {
		dev_warn(i2c->dev, "clock-frequency 0 not supported\n");
		return -EINVAL;
	}

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->algo = &mtk_i2c_algo;
	adap->retries = 3;
	adap->dev.parent = &pdev->dev;
	i2c_set_adapdata(adap, i2c);
	adap->dev.of_node = pdev->dev.of_node;
	strlcpy(adap->name, dev_name(&pdev->dev), sizeof(adap->name));

	platform_set_drvdata(pdev, i2c);

	mtk_i2c_init(i2c);

	ret = i2c_add_adapter(adap);
	if (ret < 0)
		return ret;

	dev_info(&pdev->dev, "clock %u kHz\n", i2c->bus_freq / 1000);

	return ret;
}

static int mtk_i2c_remove(struct platform_device *pdev)
{
	struct mtk_i2c *i2c = platform_get_drvdata(pdev);

	clk_disable_unprepare(i2c->clk);
	i2c_del_adapter(&i2c->adap);

	return 0;
}

static struct platform_driver mtk_i2c_driver = {
	.probe		= mtk_i2c_probe,
	.remove		= mtk_i2c_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "i2c-mt7621",
		.of_match_table = i2c_mtk_dt_ids,
	},
};

module_platform_driver(mtk_i2c_driver);

MODULE_AUTHOR("Steven Liu");
MODULE_DESCRIPTION("MT7621 I2C host driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:MT7621-I2C");
