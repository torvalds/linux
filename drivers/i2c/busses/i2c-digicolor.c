/*
 * I2C bus driver for Conexant Digicolor SoCs
 *
 * Author: Baruch Siach <baruch@tkos.co.il>
 *
 * Copyright (C) 2015 Paradox Innovation Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define DEFAULT_FREQ		100000
#define TIMEOUT_MS		100

#define II_CONTROL		0x0
#define II_CONTROL_LOCAL_RESET	BIT(0)

#define II_CLOCKTIME		0x1

#define II_COMMAND		0x2
#define II_CMD_START		1
#define II_CMD_RESTART		2
#define II_CMD_SEND_ACK		3
#define II_CMD_GET_ACK		6
#define II_CMD_GET_NOACK	7
#define II_CMD_STOP		10
#define II_COMMAND_GO		BIT(7)
#define II_COMMAND_COMPLETION_STATUS(r)	(((r) >> 5) & 3)
#define II_CMD_STATUS_NORMAL	0
#define II_CMD_STATUS_ACK_GOOD	1
#define II_CMD_STATUS_ACK_BAD	2
#define II_CMD_STATUS_ABORT	3

#define II_DATA			0x3
#define II_INTFLAG_CLEAR	0x8
#define II_INTENABLE		0xa

struct dc_i2c {
	struct i2c_adapter	adap;
	struct device		*dev;
	void __iomem		*regs;
	struct clk		*clk;
	unsigned int		frequency;

	struct i2c_msg		*msg;
	unsigned int		msgbuf_ptr;
	int			last;
	spinlock_t		lock;
	struct completion	done;
	int			state;
	int			error;
};

enum {
	STATE_IDLE,
	STATE_START,
	STATE_ADDR,
	STATE_WRITE,
	STATE_READ,
	STATE_STOP,
};

static void dc_i2c_cmd(struct dc_i2c *i2c, u8 cmd)
{
	writeb_relaxed(cmd | II_COMMAND_GO, i2c->regs + II_COMMAND);
}

static u8 dc_i2c_addr_cmd(struct i2c_msg *msg)
{
	u8 addr = (msg->addr & 0x7f) << 1;

	if (msg->flags & I2C_M_RD)
		addr |= 1;

	return addr;
}

static void dc_i2c_data(struct dc_i2c *i2c, u8 data)
{
	writeb_relaxed(data, i2c->regs + II_DATA);
}

static void dc_i2c_write_byte(struct dc_i2c *i2c, u8 byte)
{
	dc_i2c_data(i2c, byte);
	dc_i2c_cmd(i2c, II_CMD_SEND_ACK);
}

static void dc_i2c_write_buf(struct dc_i2c *i2c)
{
	dc_i2c_write_byte(i2c, i2c->msg->buf[i2c->msgbuf_ptr++]);
}

static void dc_i2c_next_read(struct dc_i2c *i2c)
{
	bool last = (i2c->msgbuf_ptr + 1 == i2c->msg->len);

	dc_i2c_cmd(i2c, last ? II_CMD_GET_NOACK : II_CMD_GET_ACK);
}

static void dc_i2c_stop(struct dc_i2c *i2c)
{
	i2c->state = STATE_STOP;
	if (i2c->last)
		dc_i2c_cmd(i2c, II_CMD_STOP);
	else
		complete(&i2c->done);
}

static u8 dc_i2c_read_byte(struct dc_i2c *i2c)
{
	return readb_relaxed(i2c->regs + II_DATA);
}

static void dc_i2c_read_buf(struct dc_i2c *i2c)
{
	i2c->msg->buf[i2c->msgbuf_ptr++] = dc_i2c_read_byte(i2c);
	dc_i2c_next_read(i2c);
}

static void dc_i2c_set_irq(struct dc_i2c *i2c, int enable)
{
	if (enable)
		writeb_relaxed(1, i2c->regs + II_INTFLAG_CLEAR);
	writeb_relaxed(!!enable, i2c->regs + II_INTENABLE);
}

static int dc_i2c_cmd_status(struct dc_i2c *i2c)
{
	u8 cmd = readb_relaxed(i2c->regs + II_COMMAND);

	return II_COMMAND_COMPLETION_STATUS(cmd);
}

static void dc_i2c_start_msg(struct dc_i2c *i2c, int first)
{
	struct i2c_msg *msg = i2c->msg;

	if (!(msg->flags & I2C_M_NOSTART)) {
		i2c->state = STATE_START;
		dc_i2c_cmd(i2c, first ? II_CMD_START : II_CMD_RESTART);
	} else if (msg->flags & I2C_M_RD) {
		i2c->state = STATE_READ;
		dc_i2c_next_read(i2c);
	} else {
		i2c->state = STATE_WRITE;
		dc_i2c_write_buf(i2c);
	}
}

static irqreturn_t dc_i2c_irq(int irq, void *dev_id)
{
	struct dc_i2c *i2c = dev_id;
	int cmd_status = dc_i2c_cmd_status(i2c);
	unsigned long flags;
	u8 addr_cmd;

	writeb_relaxed(1, i2c->regs + II_INTFLAG_CLEAR);

	spin_lock_irqsave(&i2c->lock, flags);

	if (cmd_status == II_CMD_STATUS_ACK_BAD
	    || cmd_status == II_CMD_STATUS_ABORT) {
		i2c->error = -EIO;
		complete(&i2c->done);
		goto out;
	}

	switch (i2c->state) {
	case STATE_START:
		addr_cmd = dc_i2c_addr_cmd(i2c->msg);
		dc_i2c_write_byte(i2c, addr_cmd);
		i2c->state = STATE_ADDR;
		break;
	case STATE_ADDR:
		if (i2c->msg->flags & I2C_M_RD) {
			dc_i2c_next_read(i2c);
			i2c->state = STATE_READ;
			break;
		}
		i2c->state = STATE_WRITE;
		/* fall through */
	case STATE_WRITE:
		if (i2c->msgbuf_ptr < i2c->msg->len)
			dc_i2c_write_buf(i2c);
		else
			dc_i2c_stop(i2c);
		break;
	case STATE_READ:
		if (i2c->msgbuf_ptr < i2c->msg->len)
			dc_i2c_read_buf(i2c);
		else
			dc_i2c_stop(i2c);
		break;
	case STATE_STOP:
		i2c->state = STATE_IDLE;
		complete(&i2c->done);
		break;
	}

out:
	spin_unlock_irqrestore(&i2c->lock, flags);
	return IRQ_HANDLED;
}

static int dc_i2c_xfer_msg(struct dc_i2c *i2c, struct i2c_msg *msg, int first,
			   int last)
{
	unsigned long timeout = msecs_to_jiffies(TIMEOUT_MS);
	unsigned long flags;

	spin_lock_irqsave(&i2c->lock, flags);
	i2c->msg = msg;
	i2c->msgbuf_ptr = 0;
	i2c->last = last;
	i2c->error = 0;

	reinit_completion(&i2c->done);
	dc_i2c_set_irq(i2c, 1);
	dc_i2c_start_msg(i2c, first);
	spin_unlock_irqrestore(&i2c->lock, flags);

	timeout = wait_for_completion_timeout(&i2c->done, timeout);
	dc_i2c_set_irq(i2c, 0);
	if (timeout == 0) {
		i2c->state = STATE_IDLE;
		return -ETIMEDOUT;
	}

	if (i2c->error)
		return i2c->error;

	return 0;
}

static int dc_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct dc_i2c *i2c = adap->algo_data;
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = dc_i2c_xfer_msg(i2c, &msgs[i], i == 0, i == num - 1);
		if (ret)
			return ret;
	}

	return num;
}

static int dc_i2c_init_hw(struct dc_i2c *i2c)
{
	unsigned long clk_rate = clk_get_rate(i2c->clk);
	unsigned int clocktime;

	writeb_relaxed(II_CONTROL_LOCAL_RESET, i2c->regs + II_CONTROL);
	udelay(100);
	writeb_relaxed(0, i2c->regs + II_CONTROL);
	udelay(100);

	clocktime = DIV_ROUND_UP(clk_rate, 64 * i2c->frequency);
	if (clocktime < 1 || clocktime > 0xff) {
		dev_err(i2c->dev, "can't set bus speed of %u Hz\n",
			i2c->frequency);
		return -EINVAL;
	}
	writeb_relaxed(clocktime - 1, i2c->regs + II_CLOCKTIME);

	return 0;
}

static u32 dc_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_NOSTART;
}

static const struct i2c_algorithm dc_i2c_algorithm = {
	.master_xfer	= dc_i2c_xfer,
	.functionality	= dc_i2c_func,
};

static int dc_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dc_i2c *i2c;
	struct resource *r;
	int ret = 0, irq;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct dc_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	if (of_property_read_u32(pdev->dev.of_node, "clock-frequency",
				 &i2c->frequency))
		i2c->frequency = DEFAULT_FREQ;

	i2c->dev = &pdev->dev;
	platform_set_drvdata(pdev, i2c);

	spin_lock_init(&i2c->lock);
	init_completion(&i2c->done);

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk))
		return PTR_ERR(i2c->clk);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, dc_i2c_irq, 0,
			       dev_name(&pdev->dev), i2c);
	if (ret < 0)
		return ret;

	strlcpy(i2c->adap.name, "Conexant Digicolor I2C adapter",
		sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &dc_i2c_algorithm;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = np;
	i2c->adap.algo_data = i2c;

	ret = dc_i2c_init_hw(i2c);
	if (ret)
		return ret;

	ret = clk_prepare_enable(i2c->clk);
	if (ret < 0)
		return ret;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		clk_unprepare(i2c->clk);
		return ret;
	}

	return 0;
}

static int dc_i2c_remove(struct platform_device *pdev)
{
	struct dc_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	clk_disable_unprepare(i2c->clk);

	return 0;
}

static const struct of_device_id dc_i2c_match[] = {
	{ .compatible = "cnxt,cx92755-i2c" },
	{ },
};

static struct platform_driver dc_i2c_driver = {
	.probe   = dc_i2c_probe,
	.remove  = dc_i2c_remove,
	.driver  = {
		.name  = "digicolor-i2c",
		.of_match_table = dc_i2c_match,
	},
};
module_platform_driver(dc_i2c_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Conexant Digicolor I2C master driver");
MODULE_LICENSE("GPL v2");
