/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 *
 * Author: Baoyou Xie <baoyou.xie@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define REG_CMD				0x04
#define REG_DEVADDR_H			0x0C
#define REG_DEVADDR_L			0x10
#define REG_CLK_DIV_FS			0x14
#define REG_CLK_DIV_HS			0x18
#define REG_WRCONF			0x1C
#define REG_RDCONF			0x20
#define REG_DATA			0x24
#define REG_STAT			0x28

#define I2C_STOP			0
#define I2C_MASTER			BIT(0)
#define I2C_ADDR_MODE_TEN		BIT(1)
#define I2C_IRQ_MSK_ENABLE		BIT(3)
#define I2C_RW_READ			BIT(4)
#define I2C_CMB_RW_EN			BIT(5)
#define I2C_START			BIT(6)

#define I2C_ADDR_LOW_MASK		GENMASK(6, 0)
#define I2C_ADDR_LOW_SHIFT		0
#define I2C_ADDR_HI_MASK		GENMASK(2, 0)
#define I2C_ADDR_HI_SHIFT		7

#define I2C_WFIFO_RESET			BIT(7)
#define I2C_RFIFO_RESET			BIT(7)

#define I2C_IRQ_ACK_CLEAR		BIT(7)
#define I2C_INT_MASK			GENMASK(6, 0)

#define I2C_TRANS_DONE			BIT(0)
#define I2C_SR_EDEVICE			BIT(1)
#define I2C_SR_EDATA			BIT(2)

#define I2C_FIFO_MAX			16

#define I2C_TIMEOUT			msecs_to_jiffies(1000)

#define DEV(i2c)			((i2c)->adap.dev.parent)

struct zx2967_i2c {
	struct i2c_adapter	adap;
	struct clk		*clk;
	struct completion	complete;
	u32			clk_freq;
	void __iomem		*reg_base;
	size_t			residue;
	int			irq;
	int			msg_rd;
	u8			*cur_trans;
	u8			access_cnt;
	int			error;
};

static void zx2967_i2c_writel(struct zx2967_i2c *i2c,
			      u32 val, unsigned long reg)
{
	writel_relaxed(val, i2c->reg_base + reg);
}

static u32 zx2967_i2c_readl(struct zx2967_i2c *i2c, unsigned long reg)
{
	return readl_relaxed(i2c->reg_base + reg);
}

static void zx2967_i2c_writesb(struct zx2967_i2c *i2c,
			       void *data, unsigned long reg, int len)
{
	writesb(i2c->reg_base + reg, data, len);
}

static void zx2967_i2c_readsb(struct zx2967_i2c *i2c,
			      void *data, unsigned long reg, int len)
{
	readsb(i2c->reg_base + reg, data, len);
}

static void zx2967_i2c_start_ctrl(struct zx2967_i2c *i2c)
{
	u32 status;
	u32 ctl;

	status = zx2967_i2c_readl(i2c, REG_STAT);
	status |= I2C_IRQ_ACK_CLEAR;
	zx2967_i2c_writel(i2c, status, REG_STAT);

	ctl = zx2967_i2c_readl(i2c, REG_CMD);
	if (i2c->msg_rd)
		ctl |= I2C_RW_READ;
	else
		ctl &= ~I2C_RW_READ;
	ctl &= ~I2C_CMB_RW_EN;
	ctl |= I2C_START;
	zx2967_i2c_writel(i2c, ctl, REG_CMD);
}

static void zx2967_i2c_flush_fifos(struct zx2967_i2c *i2c)
{
	u32 offset;
	u32 val;

	if (i2c->msg_rd) {
		offset = REG_RDCONF;
		val = I2C_RFIFO_RESET;
	} else {
		offset = REG_WRCONF;
		val = I2C_WFIFO_RESET;
	}

	val |= zx2967_i2c_readl(i2c, offset);
	zx2967_i2c_writel(i2c, val, offset);
}

static int zx2967_i2c_empty_rx_fifo(struct zx2967_i2c *i2c, u32 size)
{
	u8 val[I2C_FIFO_MAX] = {0};
	int i;

	if (size > I2C_FIFO_MAX) {
		dev_err(DEV(i2c), "fifo size %d over the max value %d\n",
			size, I2C_FIFO_MAX);
		return -EINVAL;
	}

	zx2967_i2c_readsb(i2c, val, REG_DATA, size);
	for (i = 0; i < size; i++) {
		*i2c->cur_trans++ = val[i];
		i2c->residue--;
	}

	barrier();

	return 0;
}

static int zx2967_i2c_fill_tx_fifo(struct zx2967_i2c *i2c)
{
	size_t residue = i2c->residue;
	u8 *buf = i2c->cur_trans;

	if (residue == 0) {
		dev_err(DEV(i2c), "residue is %d\n", (int)residue);
		return -EINVAL;
	}

	if (residue <= I2C_FIFO_MAX) {
		zx2967_i2c_writesb(i2c, buf, REG_DATA, residue);

		/* Again update before writing to FIFO to make sure isr sees. */
		i2c->residue = 0;
		i2c->cur_trans = NULL;
	} else {
		zx2967_i2c_writesb(i2c, buf, REG_DATA, I2C_FIFO_MAX);
		i2c->residue -= I2C_FIFO_MAX;
		i2c->cur_trans += I2C_FIFO_MAX;
	}

	barrier();

	return 0;
}

static int zx2967_i2c_reset_hardware(struct zx2967_i2c *i2c)
{
	u32 val;
	u32 clk_div;

	val = I2C_MASTER | I2C_IRQ_MSK_ENABLE;
	zx2967_i2c_writel(i2c, val, REG_CMD);

	clk_div = clk_get_rate(i2c->clk) / i2c->clk_freq - 1;
	zx2967_i2c_writel(i2c, clk_div, REG_CLK_DIV_FS);
	zx2967_i2c_writel(i2c, clk_div, REG_CLK_DIV_HS);

	zx2967_i2c_writel(i2c, I2C_FIFO_MAX - 1, REG_WRCONF);
	zx2967_i2c_writel(i2c, I2C_FIFO_MAX - 1, REG_RDCONF);
	zx2967_i2c_writel(i2c, 1, REG_RDCONF);

	zx2967_i2c_flush_fifos(i2c);

	return 0;
}

static void zx2967_i2c_isr_clr(struct zx2967_i2c *i2c)
{
	u32 status;

	status = zx2967_i2c_readl(i2c, REG_STAT);
	status |= I2C_IRQ_ACK_CLEAR;
	zx2967_i2c_writel(i2c, status, REG_STAT);
}

static irqreturn_t zx2967_i2c_isr(int irq, void *dev_id)
{
	u32 status;
	struct zx2967_i2c *i2c = (struct zx2967_i2c *)dev_id;

	status = zx2967_i2c_readl(i2c, REG_STAT) & I2C_INT_MASK;
	zx2967_i2c_isr_clr(i2c);

	if (status & I2C_SR_EDEVICE)
		i2c->error = -ENXIO;
	else if (status & I2C_SR_EDATA)
		i2c->error = -EIO;
	else if (status & I2C_TRANS_DONE)
		i2c->error = 0;
	else
		goto done;

	complete(&i2c->complete);
done:
	return IRQ_HANDLED;
}

static void zx2967_set_addr(struct zx2967_i2c *i2c, u16 addr)
{
	u16 val;

	val = (addr >> I2C_ADDR_LOW_SHIFT) & I2C_ADDR_LOW_MASK;
	zx2967_i2c_writel(i2c, val, REG_DEVADDR_L);

	val = (addr >> I2C_ADDR_HI_SHIFT) & I2C_ADDR_HI_MASK;
	zx2967_i2c_writel(i2c, val, REG_DEVADDR_H);
	if (val)
		val = zx2967_i2c_readl(i2c, REG_CMD) | I2C_ADDR_MODE_TEN;
	else
		val = zx2967_i2c_readl(i2c, REG_CMD) & ~I2C_ADDR_MODE_TEN;
	zx2967_i2c_writel(i2c, val, REG_CMD);
}

static int zx2967_i2c_xfer_bytes(struct zx2967_i2c *i2c, u32 bytes)
{
	unsigned long time_left;
	int rd = i2c->msg_rd;
	int ret;

	reinit_completion(&i2c->complete);

	if (rd) {
		zx2967_i2c_writel(i2c, bytes - 1, REG_RDCONF);
	} else {
		ret = zx2967_i2c_fill_tx_fifo(i2c);
		if (ret)
			return ret;
	}

	zx2967_i2c_start_ctrl(i2c);

	time_left = wait_for_completion_timeout(&i2c->complete,
						I2C_TIMEOUT);
	if (time_left == 0)
		return -ETIMEDOUT;

	if (i2c->error)
		return i2c->error;

	return rd ? zx2967_i2c_empty_rx_fifo(i2c, bytes) : 0;
}

static int zx2967_i2c_xfer_msg(struct zx2967_i2c *i2c,
			       struct i2c_msg *msg)
{
	int ret;
	int i;

	zx2967_i2c_flush_fifos(i2c);

	i2c->cur_trans = msg->buf;
	i2c->residue = msg->len;
	i2c->access_cnt = msg->len / I2C_FIFO_MAX;
	i2c->msg_rd = msg->flags & I2C_M_RD;

	for (i = 0; i < i2c->access_cnt; i++) {
		ret = zx2967_i2c_xfer_bytes(i2c, I2C_FIFO_MAX);
		if (ret)
			return ret;
	}

	if (i2c->residue > 0) {
		ret = zx2967_i2c_xfer_bytes(i2c, i2c->residue);
		if (ret)
			return ret;
	}

	i2c->residue = 0;
	i2c->access_cnt = 0;

	return 0;
}

static int zx2967_i2c_xfer(struct i2c_adapter *adap,
			   struct i2c_msg *msgs, int num)
{
	struct zx2967_i2c *i2c = i2c_get_adapdata(adap);
	int ret;
	int i;

	zx2967_set_addr(i2c, msgs->addr);

	for (i = 0; i < num; i++) {
		ret = zx2967_i2c_xfer_msg(i2c, &msgs[i]);
		if (ret)
			return ret;
	}

	return num;
}

static void
zx2967_smbus_xfer_prepare(struct zx2967_i2c *i2c, u16 addr,
			  char read_write, u8 command, int size,
			  union i2c_smbus_data *data)
{
	u32 val;

	val = zx2967_i2c_readl(i2c, REG_RDCONF);
	val |= I2C_RFIFO_RESET;
	zx2967_i2c_writel(i2c, val, REG_RDCONF);
	zx2967_set_addr(i2c, addr);
	val = zx2967_i2c_readl(i2c, REG_CMD);
	val &= ~I2C_RW_READ;
	zx2967_i2c_writel(i2c, val, REG_CMD);

	switch (size) {
	case I2C_SMBUS_BYTE:
		zx2967_i2c_writel(i2c, command, REG_DATA);
		break;
	case I2C_SMBUS_BYTE_DATA:
		zx2967_i2c_writel(i2c, command, REG_DATA);
		if (read_write == I2C_SMBUS_WRITE)
			zx2967_i2c_writel(i2c, data->byte, REG_DATA);
		break;
	case I2C_SMBUS_WORD_DATA:
		zx2967_i2c_writel(i2c, command, REG_DATA);
		if (read_write == I2C_SMBUS_WRITE) {
			zx2967_i2c_writel(i2c, (data->word >> 8), REG_DATA);
			zx2967_i2c_writel(i2c, (data->word & 0xff),
					  REG_DATA);
		}
		break;
	}
}

static int zx2967_smbus_xfer_read(struct zx2967_i2c *i2c, int size,
				  union i2c_smbus_data *data)
{
	unsigned long time_left;
	u8 buf[2];
	u32 val;

	reinit_completion(&i2c->complete);

	val = zx2967_i2c_readl(i2c, REG_CMD);
	val |= I2C_CMB_RW_EN;
	zx2967_i2c_writel(i2c, val, REG_CMD);

	val = zx2967_i2c_readl(i2c, REG_CMD);
	val |= I2C_START;
	zx2967_i2c_writel(i2c, val, REG_CMD);

	time_left = wait_for_completion_timeout(&i2c->complete,
						I2C_TIMEOUT);
	if (time_left == 0)
		return -ETIMEDOUT;

	if (i2c->error)
		return i2c->error;

	switch (size) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		val = zx2967_i2c_readl(i2c, REG_DATA);
		data->byte = val;
		break;
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		buf[0] = zx2967_i2c_readl(i2c, REG_DATA);
		buf[1] = zx2967_i2c_readl(i2c, REG_DATA);
		data->word = (buf[0] << 8) | buf[1];
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int zx2967_smbus_xfer_write(struct zx2967_i2c *i2c)
{
	unsigned long time_left;
	u32 val;

	reinit_completion(&i2c->complete);
	val = zx2967_i2c_readl(i2c, REG_CMD);
	val |= I2C_START;
	zx2967_i2c_writel(i2c, val, REG_CMD);

	time_left = wait_for_completion_timeout(&i2c->complete,
						I2C_TIMEOUT);
	if (time_left == 0)
		return -ETIMEDOUT;

	if (i2c->error)
		return i2c->error;

	return 0;
}

static int zx2967_smbus_xfer(struct i2c_adapter *adap, u16 addr,
			     unsigned short flags, char read_write,
			     u8 command, int size, union i2c_smbus_data *data)
{
	struct zx2967_i2c *i2c = i2c_get_adapdata(adap);

	if (size == I2C_SMBUS_QUICK)
		read_write = I2C_SMBUS_WRITE;

	switch (size) {
	case I2C_SMBUS_QUICK:
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
	case I2C_SMBUS_WORD_DATA:
		zx2967_smbus_xfer_prepare(i2c, addr, read_write,
					  command, size, data);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (read_write == I2C_SMBUS_READ)
		return zx2967_smbus_xfer_read(i2c, size, data);

	return zx2967_smbus_xfer_write(i2c);
}

static u32 zx2967_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C |
	       I2C_FUNC_SMBUS_QUICK |
	       I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA |
	       I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA |
	       I2C_FUNC_SMBUS_PROC_CALL |
	       I2C_FUNC_SMBUS_I2C_BLOCK;
}

static int __maybe_unused zx2967_i2c_suspend(struct device *dev)
{
	struct zx2967_i2c *i2c = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&i2c->adap);
	clk_disable_unprepare(i2c->clk);

	return 0;
}

static int __maybe_unused zx2967_i2c_resume(struct device *dev)
{
	struct zx2967_i2c *i2c = dev_get_drvdata(dev);

	clk_prepare_enable(i2c->clk);
	i2c_mark_adapter_resumed(&i2c->adap);

	return 0;
}

static SIMPLE_DEV_PM_OPS(zx2967_i2c_dev_pm_ops,
			 zx2967_i2c_suspend, zx2967_i2c_resume);

static const struct i2c_algorithm zx2967_i2c_algo = {
	.master_xfer = zx2967_i2c_xfer,
	.smbus_xfer = zx2967_smbus_xfer,
	.functionality = zx2967_i2c_func,
};

static const struct i2c_adapter_quirks zx2967_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static const struct of_device_id zx2967_i2c_of_match[] = {
	{ .compatible = "zte,zx296718-i2c", },
	{ },
};
MODULE_DEVICE_TABLE(of, zx2967_i2c_of_match);

static int zx2967_i2c_probe(struct platform_device *pdev)
{
	struct zx2967_i2c *i2c;
	void __iomem *reg_base;
	struct resource *res;
	struct clk *clk;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "missing controller clock");
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable i2c_clk\n");
		return ret;
	}

	ret = device_property_read_u32(&pdev->dev, "clock-frequency",
				       &i2c->clk_freq);
	if (ret) {
		dev_err(&pdev->dev, "missing clock-frequency");
		return ret;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	i2c->irq = ret;
	i2c->reg_base = reg_base;
	i2c->clk = clk;

	init_completion(&i2c->complete);
	platform_set_drvdata(pdev, i2c);

	ret = zx2967_i2c_reset_hardware(i2c);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize i2c controller\n");
		goto err_clk_unprepare;
	}

	ret = devm_request_irq(&pdev->dev, i2c->irq,
			zx2967_i2c_isr, 0, dev_name(&pdev->dev), i2c);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %i\n", i2c->irq);
		goto err_clk_unprepare;
	}

	i2c_set_adapdata(&i2c->adap, i2c);
	strlcpy(i2c->adap.name, "zx2967 i2c adapter",
		sizeof(i2c->adap.name));
	i2c->adap.algo = &zx2967_i2c_algo;
	i2c->adap.quirks = &zx2967_i2c_quirks;
	i2c->adap.nr = pdev->id;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret)
		goto err_clk_unprepare;

	return 0;

err_clk_unprepare:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int zx2967_i2c_remove(struct platform_device *pdev)
{
	struct zx2967_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	clk_disable_unprepare(i2c->clk);

	return 0;
}

static struct platform_driver zx2967_i2c_driver = {
	.probe	= zx2967_i2c_probe,
	.remove	= zx2967_i2c_remove,
	.driver	= {
		.name  = "zx2967_i2c",
		.of_match_table = zx2967_i2c_of_match,
		.pm = &zx2967_i2c_dev_pm_ops,
	},
};
module_platform_driver(zx2967_i2c_driver);

MODULE_AUTHOR("Baoyou Xie <baoyou.xie@linaro.org>");
MODULE_DESCRIPTION("ZTE ZX2967 I2C Bus Controller driver");
MODULE_LICENSE("GPL v2");
