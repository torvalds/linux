/*
 * I2C bus driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>

/* Meson I2C register map */
#define REG_CTRL		0x00
#define REG_SLAVE_ADDR		0x04
#define REG_TOK_LIST0		0x08
#define REG_TOK_LIST1		0x0c
#define REG_TOK_WDATA0		0x10
#define REG_TOK_WDATA1		0x14
#define REG_TOK_RDATA0		0x18
#define REG_TOK_RDATA1		0x1c

/* Control register fields */
#define REG_CTRL_START		BIT(0)
#define REG_CTRL_ACK_IGNORE	BIT(1)
#define REG_CTRL_STATUS		BIT(2)
#define REG_CTRL_ERROR		BIT(3)
#define REG_CTRL_CLKDIV_SHIFT	12
#define REG_CTRL_CLKDIV_MASK	GENMASK(21, 12)
#define REG_CTRL_CLKDIVEXT_SHIFT 28
#define REG_CTRL_CLKDIVEXT_MASK	GENMASK(29, 28)

#define REG_SLV_ADDR		GENMASK(7, 0)
#define REG_SLV_SDA_FILTER	GENMASK(10, 8)
#define REG_SLV_SCL_FILTER	GENMASK(13, 11)
#define REG_SLV_SCL_LOW		GENMASK(27, 16)
#define REG_SLV_SCL_LOW_EN	BIT(28)

#define I2C_TIMEOUT_MS		500

enum {
	TOKEN_END = 0,
	TOKEN_START,
	TOKEN_SLAVE_ADDR_WRITE,
	TOKEN_SLAVE_ADDR_READ,
	TOKEN_DATA,
	TOKEN_DATA_LAST,
	TOKEN_STOP,
};

enum {
	STATE_IDLE,
	STATE_READ,
	STATE_WRITE,
};

struct meson_i2c_data {
	unsigned char div_factor;
};

/**
 * struct meson_i2c - Meson I2C device private data
 *
 * @adap:	I2C adapter instance
 * @dev:	Pointer to device structure
 * @regs:	Base address of the device memory mapped registers
 * @clk:	Pointer to clock structure
 * @msg:	Pointer to the current I2C message
 * @state:	Current state in the driver state machine
 * @last:	Flag set for the last message in the transfer
 * @count:	Number of bytes to be sent/received in current transfer
 * @pos:	Current position in the send/receive buffer
 * @error:	Flag set when an error is received
 * @lock:	To avoid race conditions between irq handler and xfer code
 * @done:	Completion used to wait for transfer termination
 * @tokens:	Sequence of tokens to be written to the device
 * @num_tokens:	Number of tokens
 * @data:	Pointer to the controlller's platform data
 */
struct meson_i2c {
	struct i2c_adapter	adap;
	struct device		*dev;
	void __iomem		*regs;
	struct clk		*clk;

	struct i2c_msg		*msg;
	int			state;
	bool			last;
	int			count;
	int			pos;
	int			error;

	spinlock_t		lock;
	struct completion	done;
	u32			tokens[2];
	int			num_tokens;

	const struct meson_i2c_data *data;
};

static void meson_i2c_set_mask(struct meson_i2c *i2c, int reg, u32 mask,
			       u32 val)
{
	u32 data;

	data = readl(i2c->regs + reg);
	data &= ~mask;
	data |= val & mask;
	writel(data, i2c->regs + reg);
}

static void meson_i2c_reset_tokens(struct meson_i2c *i2c)
{
	i2c->tokens[0] = 0;
	i2c->tokens[1] = 0;
	i2c->num_tokens = 0;
}

static void meson_i2c_add_token(struct meson_i2c *i2c, int token)
{
	if (i2c->num_tokens < 8)
		i2c->tokens[0] |= (token & 0xf) << (i2c->num_tokens * 4);
	else
		i2c->tokens[1] |= (token & 0xf) << ((i2c->num_tokens % 8) * 4);

	i2c->num_tokens++;
}

static void meson_i2c_set_clk_div(struct meson_i2c *i2c, unsigned int freq)
{
	unsigned long clk_rate = clk_get_rate(i2c->clk);
	unsigned int div;

	div = DIV_ROUND_UP(clk_rate, freq * i2c->data->div_factor);

	/* clock divider has 12 bits */
	if (div >= (1 << 12)) {
		dev_err(i2c->dev, "requested bus frequency too low\n");
		div = (1 << 12) - 1;
	}

	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_CLKDIV_MASK,
			   (div & GENMASK(9, 0)) << REG_CTRL_CLKDIV_SHIFT);

	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_CLKDIVEXT_MASK,
			   (div >> 10) << REG_CTRL_CLKDIVEXT_SHIFT);

	/* Disable HIGH/LOW mode */
	meson_i2c_set_mask(i2c, REG_SLAVE_ADDR, REG_SLV_SCL_LOW_EN, 0);

	dev_dbg(i2c->dev, "%s: clk %lu, freq %u, div %u\n", __func__,
		clk_rate, freq, div);
}

static void meson_i2c_get_data(struct meson_i2c *i2c, char *buf, int len)
{
	u32 rdata0, rdata1;
	int i;

	rdata0 = readl(i2c->regs + REG_TOK_RDATA0);
	rdata1 = readl(i2c->regs + REG_TOK_RDATA1);

	dev_dbg(i2c->dev, "%s: data %08x %08x len %d\n", __func__,
		rdata0, rdata1, len);

	for (i = 0; i < min(4, len); i++)
		*buf++ = (rdata0 >> i * 8) & 0xff;

	for (i = 4; i < min(8, len); i++)
		*buf++ = (rdata1 >> (i - 4) * 8) & 0xff;
}

static void meson_i2c_put_data(struct meson_i2c *i2c, char *buf, int len)
{
	u32 wdata0 = 0, wdata1 = 0;
	int i;

	for (i = 0; i < min(4, len); i++)
		wdata0 |= *buf++ << (i * 8);

	for (i = 4; i < min(8, len); i++)
		wdata1 |= *buf++ << ((i - 4) * 8);

	writel(wdata0, i2c->regs + REG_TOK_WDATA0);
	writel(wdata1, i2c->regs + REG_TOK_WDATA1);

	dev_dbg(i2c->dev, "%s: data %08x %08x len %d\n", __func__,
		wdata0, wdata1, len);
}

static void meson_i2c_prepare_xfer(struct meson_i2c *i2c)
{
	bool write = !(i2c->msg->flags & I2C_M_RD);
	int i;

	i2c->count = min(i2c->msg->len - i2c->pos, 8);

	for (i = 0; i < i2c->count - 1; i++)
		meson_i2c_add_token(i2c, TOKEN_DATA);

	if (i2c->count) {
		if (write || i2c->pos + i2c->count < i2c->msg->len)
			meson_i2c_add_token(i2c, TOKEN_DATA);
		else
			meson_i2c_add_token(i2c, TOKEN_DATA_LAST);
	}

	if (write)
		meson_i2c_put_data(i2c, i2c->msg->buf + i2c->pos, i2c->count);

	if (i2c->last && i2c->pos + i2c->count >= i2c->msg->len)
		meson_i2c_add_token(i2c, TOKEN_STOP);

	writel(i2c->tokens[0], i2c->regs + REG_TOK_LIST0);
	writel(i2c->tokens[1], i2c->regs + REG_TOK_LIST1);
}

static irqreturn_t meson_i2c_irq(int irqno, void *dev_id)
{
	struct meson_i2c *i2c = dev_id;
	unsigned int ctrl;

	spin_lock(&i2c->lock);

	meson_i2c_reset_tokens(i2c);
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_START, 0);
	ctrl = readl(i2c->regs + REG_CTRL);

	dev_dbg(i2c->dev, "irq: state %d, pos %d, count %d, ctrl %08x\n",
		i2c->state, i2c->pos, i2c->count, ctrl);

	if (i2c->state == STATE_IDLE) {
		spin_unlock(&i2c->lock);
		return IRQ_NONE;
	}

	if (ctrl & REG_CTRL_ERROR) {
		/*
		 * The bit is set when the IGNORE_NAK bit is cleared
		 * and the device didn't respond. In this case, the
		 * I2C controller automatically generates a STOP
		 * condition.
		 */
		dev_dbg(i2c->dev, "error bit set\n");
		i2c->error = -ENXIO;
		i2c->state = STATE_IDLE;
		complete(&i2c->done);
		goto out;
	}

	if (i2c->state == STATE_READ && i2c->count)
		meson_i2c_get_data(i2c, i2c->msg->buf + i2c->pos, i2c->count);

	i2c->pos += i2c->count;

	if (i2c->pos >= i2c->msg->len) {
		i2c->state = STATE_IDLE;
		complete(&i2c->done);
		goto out;
	}

	/* Restart the processing */
	meson_i2c_prepare_xfer(i2c);
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_START, REG_CTRL_START);
out:
	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}

static void meson_i2c_do_start(struct meson_i2c *i2c, struct i2c_msg *msg)
{
	int token;

	token = (msg->flags & I2C_M_RD) ? TOKEN_SLAVE_ADDR_READ :
		TOKEN_SLAVE_ADDR_WRITE;


	meson_i2c_set_mask(i2c, REG_SLAVE_ADDR, REG_SLV_ADDR,
			   FIELD_PREP(REG_SLV_ADDR, msg->addr << 1));

	meson_i2c_add_token(i2c, TOKEN_START);
	meson_i2c_add_token(i2c, token);
}

static int meson_i2c_xfer_msg(struct meson_i2c *i2c, struct i2c_msg *msg,
			      int last)
{
	unsigned long time_left, flags;
	int ret = 0;

	i2c->msg = msg;
	i2c->last = last;
	i2c->pos = 0;
	i2c->count = 0;
	i2c->error = 0;

	meson_i2c_reset_tokens(i2c);

	flags = (msg->flags & I2C_M_IGNORE_NAK) ? REG_CTRL_ACK_IGNORE : 0;
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_ACK_IGNORE, flags);

	if (!(msg->flags & I2C_M_NOSTART))
		meson_i2c_do_start(i2c, msg);

	i2c->state = (msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;
	meson_i2c_prepare_xfer(i2c);
	reinit_completion(&i2c->done);

	/* Start the transfer */
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_START, REG_CTRL_START);

	time_left = msecs_to_jiffies(I2C_TIMEOUT_MS);
	time_left = wait_for_completion_timeout(&i2c->done, time_left);

	/*
	 * Protect access to i2c struct and registers from interrupt
	 * handlers triggered by a transfer terminated after the
	 * timeout period
	 */
	spin_lock_irqsave(&i2c->lock, flags);

	/* Abort any active operation */
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_START, 0);

	if (!time_left) {
		i2c->state = STATE_IDLE;
		ret = -ETIMEDOUT;
	}

	if (i2c->error)
		ret = i2c->error;

	spin_unlock_irqrestore(&i2c->lock, flags);

	return ret;
}

static int meson_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			  int num)
{
	struct meson_i2c *i2c = adap->algo_data;
	int i, ret = 0;

	clk_enable(i2c->clk);

	for (i = 0; i < num; i++) {
		ret = meson_i2c_xfer_msg(i2c, msgs + i, i == num - 1);
		if (ret)
			break;
	}

	clk_disable(i2c->clk);

	return ret ?: i;
}

static u32 meson_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm meson_i2c_algorithm = {
	.master_xfer	= meson_i2c_xfer,
	.functionality	= meson_i2c_func,
};

static int meson_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct meson_i2c *i2c;
	struct resource *mem;
	struct i2c_timings timings;
	int irq, ret = 0;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct meson_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c_parse_fw_timings(&pdev->dev, &timings, true);

	i2c->dev = &pdev->dev;
	platform_set_drvdata(pdev, i2c);

	spin_lock_init(&i2c->lock);
	init_completion(&i2c->done);

	i2c->data = (const struct meson_i2c_data *)
		of_device_get_match_data(&pdev->dev);

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "can't get device clock\n");
		return PTR_ERR(i2c->clk);
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "can't find IRQ\n");
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, meson_i2c_irq, 0, NULL, i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't request IRQ\n");
		return ret;
	}

	ret = clk_prepare(i2c->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't prepare clock\n");
		return ret;
	}

	strlcpy(i2c->adap.name, "Meson I2C adapter",
		sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &meson_i2c_algorithm;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = np;
	i2c->adap.algo_data = i2c;

	/*
	 * A transfer is triggered when START bit changes from 0 to 1.
	 * Ensure that the bit is set to 0 after probe
	 */
	meson_i2c_set_mask(i2c, REG_CTRL, REG_CTRL_START, 0);

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		clk_unprepare(i2c->clk);
		return ret;
	}

	/* Disable filtering */
	meson_i2c_set_mask(i2c, REG_SLAVE_ADDR,
			   REG_SLV_SDA_FILTER | REG_SLV_SCL_FILTER, 0);

	meson_i2c_set_clk_div(i2c, timings.bus_freq_hz);

	return 0;
}

static int meson_i2c_remove(struct platform_device *pdev)
{
	struct meson_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	clk_unprepare(i2c->clk);

	return 0;
}

static const struct meson_i2c_data i2c_meson6_data = {
	.div_factor = 4,
};

static const struct meson_i2c_data i2c_gxbb_data = {
	.div_factor = 4,
};

static const struct meson_i2c_data i2c_axg_data = {
	.div_factor = 3,
};

static const struct of_device_id meson_i2c_match[] = {
	{ .compatible = "amlogic,meson6-i2c", .data = &i2c_meson6_data },
	{ .compatible = "amlogic,meson-gxbb-i2c", .data = &i2c_gxbb_data },
	{ .compatible = "amlogic,meson-axg-i2c", .data = &i2c_axg_data },
	{},
};

MODULE_DEVICE_TABLE(of, meson_i2c_match);

static struct platform_driver meson_i2c_driver = {
	.probe   = meson_i2c_probe,
	.remove  = meson_i2c_remove,
	.driver  = {
		.name  = "meson-i2c",
		.of_match_table = meson_i2c_match,
	},
};

module_platform_driver(meson_i2c_driver);

MODULE_DESCRIPTION("Amlogic Meson I2C Bus driver");
MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_LICENSE("GPL v2");
