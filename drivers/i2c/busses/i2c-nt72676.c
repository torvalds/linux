// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Novatek Microelectronics Corp.
 * Author: Ben Huang <ben_huang@novatek.com.tw>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define I2C_REG_CTRL			0x00
#define I2C_REG_CLK			0x04
#define I2C_REG_ACK			0x08
#define I2C_REG_SIZE			0x0C
#define I2C_REG_FIFO1			0x10
#define I2C_REG_SUBADDR			0x20
#define I2C_REG_PINGPONG		0x24
#define I2C_REG_INTR			0x28
#define I2C_REG_FIFO2			0x2C
#define I2C_REG_DUTY			0x3C
#define I2C_CLR_FIFO1			BIT(7)
#define I2C_CLR_FIFO2			BIT(23)
#define I2C_BUF_LITTLE_ENDIAN		BIT(13)
#define I2C_BUSY			BIT(1)
#define I2C_ENABLE			BIT(2)
#define I2C_REPEAT_ENABLE		BIT(7)
#define I2C_READ_OPERATION		BIT(8)
#define I2C_NACK			BIT(24)
#define I2C_CLOCK_DUTY_ENABLE		BIT(21)
#define I2C_CLOCK_STRETCH_ENABLE	BIT(27)
#define I2C_MASTER_CLK_STRETCH_ENABLE	BIT(28)
#define I2C_TRIGGER			BIT(0)
#define I2C_IRQ_CLK_STR_TIMEOUT		BIT(12)
#define I2C_IRQ_NACK			BIT(11)
#define I2C_IRQ_RX_FULL			BIT(10)
#define I2C_IRQ_TX_EMPTY		BIT(9)
#define I2C_IRQ_FINISH			BIT(8)
#define I2C_IRQ_FLAG			(I2C_IRQ_CLK_STR_TIMEOUT |\
					 I2C_IRQ_NACK | \
					 I2C_IRQ_RX_FULL | \
					 I2C_IRQ_TX_EMPTY | \
					 I2C_IRQ_FINISH)
#define I2C_IRQ_CLEAR_ALL		GENMASK(20, 16)
#define I2C_IRQ_ENABLE_SETTING		GENMASK(4, 0)
#define I2C_SUBADDR_ENABLE		BIT(6)
#define I2C_16BITSUBADDR_ENABLE		BIT(16)
#define I2C_24BITSUBADDR_ENABLE		BIT(17)
#define I2C_32BITSUBADDR_ENABLE		BIT(18)
#define I2C_ACK_CTRL_COUNTER		0x000001E0
#define I2C_TX_EMPTY_FIFO1		BIT(6)
#define I2C_TX_EMPTY_FIFO2		BIT(22)
#define I2C_RX_FULL_FIFO1		BIT(5)
#define I2C_RX_FULL_FIFO2		BIT(21)

#define STBC_REG_PSWD			0x0204
#define STBC_REG_KEYPASS		0x0208
#define STBC_REG_I2C_SWITCH		0x0220
#define STBC_PSWD_DATA1			0x72682
#define STBC_PSWD_DATA2			0x28627
#define STBC_KEYPASS_ENABLE		BIT(0)
#define STBC_AGPIO_SWITCH_TO_CPU	BIT(14)

#define FIFO_CHUNK_SIZE			16
#define FIFO_WORD_BYTES			4

enum {
	SUBADDR_DISABLE = 0,
	SUBADDR_8BITS,
	SUBADDR_16BITS,
	SUBADDR_24BITS,
	SUBADDR_32BITS,
};

#define SUBADDR_MAX_LEN			SUBADDR_32BITS

enum {
	FIFO_1 = 0,
	FIFO_2,
	FIFO_ALL
};

struct nvt_i2c_bus {
	void __iomem *base;
	struct i2c_adapter adapter;
	struct device *dev;
	struct completion msg_complete;
	struct clk *clk;
	unsigned long input_clk_rate;
	unsigned int bus_clk_rate;
	bool stbc_i2c;
	struct regmap *stbc_regmap;
	int irq;
	/* lock for msg and reg protection */
	spinlock_t lock;
	/* used for xfer, protected by lock */
	struct i2c_msg *current_msg;
	int subaddr_mode;
	int remaining;
	int write_ptr;
	int read_ptr;
	int fifo_idx;
	int error_code;
};

static void nt72676_i2c_writel(u32 val, struct nvt_i2c_bus *i2c, unsigned int reg)
{
	writel(val, i2c->base + reg);
}

static u32 nt72676_i2c_readl(struct nvt_i2c_bus *i2c, unsigned int reg)
{
	return readl(i2c->base + reg);
}

static int nvt_i2c_stbc_auth(struct nvt_i2c_bus *i2c)
{
	int ret;

	if (!i2c->stbc_i2c)
		return 0;

	ret = regmap_write(i2c->stbc_regmap, STBC_REG_PSWD, STBC_PSWD_DATA1);
	if (ret)
		return ret;

	ret = regmap_write(i2c->stbc_regmap, STBC_REG_PSWD, STBC_PSWD_DATA2);
	if (ret)
		return ret;

	ret = regmap_write(i2c->stbc_regmap, STBC_REG_KEYPASS, STBC_KEYPASS_ENABLE);
	if (ret)
		return ret;

	return regmap_update_bits(i2c->stbc_regmap, STBC_REG_I2C_SWITCH,
				  STBC_AGPIO_SWITCH_TO_CPU, STBC_AGPIO_SWITCH_TO_CPU);
}

static void nvt_i2c_set_clk(struct nvt_i2c_bus *i2c)
{
	unsigned int clk_div = 0;
	unsigned int duty = 0;

	clk_div = i2c->input_clk_rate / i2c->bus_clk_rate;
	nt72676_i2c_writel(clk_div << 1, i2c, I2C_REG_CLK);

	duty = (clk_div * 9 + 10) / 20;
	nt72676_i2c_writel(duty << 16, i2c, I2C_REG_DUTY);

	nt72676_i2c_writel(I2C_ACK_CTRL_COUNTER, i2c, I2C_REG_ACK);
}

static int nvt_i2c_set_subaddr(struct nvt_i2c_bus *i2c,
			       const struct i2c_msg *msgs,
			       int num)
{
	unsigned int i, subaddr = 0;

	/* Sub-address can only be a write with maximum size of 4 bytes. */
	if (WARN_ON(num != 2))
		return -EINVAL;
	if (!msgs || !msgs[0].buf ||
	    msgs[0].len > i2c->adapter.quirks->max_comb_1st_msg_len ||
	    (msgs[0].flags & I2C_M_RD) || !(msgs[1].flags & I2C_M_RD) ||
	    msgs[0].addr != msgs[1].addr) {
		dev_dbg(i2c->dev, "Invalid sub-address format\n");
		return -EOPNOTSUPP;
	}

	i2c->subaddr_mode = msgs[0].len;
	for (i = 0; i < msgs[0].len; i++)
		subaddr |= (unsigned int)msgs[0].buf[i] << (8 * (msgs[0].len - 1 - i));
	nt72676_i2c_writel(subaddr, i2c, I2C_REG_SUBADDR);

	return 0;
}

static int nvt_i2c_init(struct nvt_i2c_bus *i2c)
{
	if (i2c->stbc_i2c) {
		int ret = nvt_i2c_stbc_auth(i2c);

		if (ret)
			return ret;
	}
	nt72676_i2c_writel(I2C_IRQ_CLEAR_ALL, i2c, I2C_REG_INTR);
	nvt_i2c_set_clk(i2c);
	nt72676_i2c_writel(I2C_BUF_LITTLE_ENDIAN, i2c, I2C_REG_PINGPONG);

	return 0;
}

static void nvt_i2c_reset(struct nvt_i2c_bus *i2c)
{
	nt72676_i2c_writel(I2C_IRQ_CLEAR_ALL, i2c, I2C_REG_INTR);
	if (i2c->error_code) {
		nt72676_i2c_writel(nt72676_i2c_readl(i2c, I2C_REG_CTRL) & ~I2C_ENABLE,
				   i2c, I2C_REG_CTRL);
		nt72676_i2c_writel(nt72676_i2c_readl(i2c, I2C_REG_CTRL) | I2C_ENABLE,
				   i2c, I2C_REG_CTRL);
	}
}

static void nvt_i2c_release(struct nvt_i2c_bus *i2c)
{
	nt72676_i2c_writel(nt72676_i2c_readl(i2c, I2C_REG_CTRL) & ~I2C_ENABLE,
			   i2c, I2C_REG_CTRL);
	nt72676_i2c_writel(I2C_IRQ_CLEAR_ALL, i2c, I2C_REG_INTR);
}

static int nvt_i2c_suspend(struct device *dev)
{
	struct nvt_i2c_bus *i2c = dev_get_drvdata(dev);
	unsigned long flags;

	i2c_mark_adapter_suspended(&i2c->adapter);

	spin_lock_irqsave(&i2c->lock, flags);
	i2c->current_msg = NULL;
	nvt_i2c_release(i2c);
	spin_unlock_irqrestore(&i2c->lock, flags);

	return 0;
}

static int nvt_i2c_resume(struct device *dev)
{
	struct nvt_i2c_bus *i2c = dev_get_drvdata(dev);
	int ret = nvt_i2c_init(i2c);

	if (ret) {
		dev_err(i2c->dev, "Failed to resume\n");
		return ret;
	}

	i2c_mark_adapter_resumed(&i2c->adapter);

	return 0;
}

static void nvt_i2c_clear_fifo(struct nvt_i2c_bus *i2c, unsigned int which)
{
	unsigned int regval = nt72676_i2c_readl(i2c, I2C_REG_PINGPONG);

	switch (which) {
	case FIFO_1:
		regval |= I2C_CLR_FIFO1;
		break;
	case FIFO_2:
		regval |= I2C_CLR_FIFO2;
		break;
	case FIFO_ALL:
		regval |= I2C_CLR_FIFO1 | I2C_CLR_FIFO2;
		break;
	default:
		break;
	}
	nt72676_i2c_writel(regval, i2c, I2C_REG_PINGPONG);
}

static void nvt_i2c_write_fifo(struct nvt_i2c_bus *i2c,
			       unsigned int fifo_reg,
			       const unsigned char *buf,
			       unsigned int buf_offset,
			       unsigned int length)
{
	unsigned int reg_idx = 0, copy_bytes = 0, j = 0, value = 0;

	while (length > 0) {
		value = 0;
		copy_bytes = min(FIFO_WORD_BYTES, length);
		for (j = 0; j < copy_bytes; j++)
			value |= ((unsigned int)buf[buf_offset + j]) << (j * 8);

		nt72676_i2c_writel(value, i2c, fifo_reg + reg_idx * 4);
		buf_offset += copy_bytes;
		length -= copy_bytes;
		reg_idx++;
	}
}

static void nvt_i2c_read_fifo(struct nvt_i2c_bus *i2c,
			      unsigned int fifo_reg,
			      unsigned char *buf,
			      unsigned int buf_offset,
			      unsigned int length)
{
	unsigned int reg_idx = 0, copy_bytes = 0, j = 0, value = 0;

	while (length > 0) {
		value = nt72676_i2c_readl(i2c, fifo_reg + reg_idx * 4);
		copy_bytes = min(FIFO_WORD_BYTES, length);
		for (j = 0; j < copy_bytes; j++)
			buf[buf_offset + j] = (unsigned char)(value >> (j * 8));

		buf_offset += copy_bytes;
		length -= copy_bytes;
		reg_idx++;
	}
}

static void nvt_i2c_handle(struct nvt_i2c_bus *i2c, struct i2c_msg *msg, bool is_read)
{
	unsigned int bytes, fiforeg;

	if (WARN_ON(!msg->buf))
		return;

	bytes = min(FIFO_CHUNK_SIZE, i2c->remaining);
	fiforeg = i2c->fifo_idx == FIFO_1 ? I2C_REG_FIFO1 : I2C_REG_FIFO2;

	if (is_read) {
		nvt_i2c_read_fifo(i2c, fiforeg, msg->buf, i2c->read_ptr, bytes);
		i2c->read_ptr += bytes;
	} else {
		nvt_i2c_write_fifo(i2c, fiforeg, msg->buf, i2c->write_ptr, bytes);
		i2c->write_ptr += bytes;
	}
	nvt_i2c_clear_fifo(i2c, i2c->fifo_idx);
	i2c->remaining -= bytes;
	i2c->fifo_idx ^= 1;
}

static irqreturn_t nvt_i2c_isr(int irq, void *dev_id)
{
	struct nvt_i2c_bus *i2c = dev_id;
	unsigned int status, clr = 0;
	struct i2c_msg *msg;
	int do_complete = 0;

	spin_lock(&i2c->lock);
	status = nt72676_i2c_readl(i2c, I2C_REG_INTR);
	/* IRQ from other I2C, ignored */
	if (!(status & I2C_IRQ_FLAG)) {
		spin_unlock(&i2c->lock);
		return IRQ_NONE;
	}

	msg = i2c->current_msg;
	if (!msg) {
		nt72676_i2c_writel(I2C_IRQ_CLEAR_ALL, i2c, I2C_REG_INTR);
		spin_unlock(&i2c->lock);
		return IRQ_HANDLED;
	}

	if (status & I2C_IRQ_CLK_STR_TIMEOUT) {
		i2c->error_code = -ETIMEDOUT;
		clr |= I2C_IRQ_CLK_STR_TIMEOUT << 8;
	} else if (status & I2C_IRQ_NACK) {
		i2c->error_code = -ENXIO;
		clr |= I2C_IRQ_NACK << 8;
	} else if (status & I2C_IRQ_RX_FULL) {
		if (i2c->remaining > 0)
			nvt_i2c_handle(i2c, msg, true);
		clr |= I2C_IRQ_RX_FULL << 8;
	} else if (status & I2C_IRQ_TX_EMPTY) {
		if (i2c->remaining > 0)
			nvt_i2c_handle(i2c, msg, false);
		clr |= I2C_IRQ_TX_EMPTY << 8;
	} else if (status & I2C_IRQ_FINISH) {
		if (i2c->remaining > 0 && (msg->flags & I2C_M_RD))
			nvt_i2c_handle(i2c, msg, true);
		clr |= I2C_IRQ_FINISH << 8;
		do_complete = 1;
	}
	if (i2c->error_code)
		do_complete = 1;

	nt72676_i2c_writel(status | clr, i2c, I2C_REG_INTR);
	if (do_complete)
		complete(&i2c->msg_complete);
	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}

static void nvt_i2c_ctrl_init(struct nvt_i2c_bus *i2c)
{
	int i = 0;

	nt72676_i2c_writel(0, i2c, I2C_REG_CTRL);
	for (i = 0; i < 4; i++) {
		nt72676_i2c_writel(0, i2c, I2C_REG_FIFO1 + i * 4);
		nt72676_i2c_writel(0, i2c, I2C_REG_FIFO2 + i * 4);
	}
	nvt_i2c_clear_fifo(i2c, FIFO_ALL);
	nt72676_i2c_writel(0, i2c, I2C_REG_SUBADDR);
	i2c->subaddr_mode = SUBADDR_DISABLE;
}

static int nvt_i2c_check_msg(const struct i2c_msg *msg)
{
	if (!msg || !msg->buf || !msg->len)
		return -EINVAL;

	return 0;
}

static void nvt_i2c_prepare_xfer(struct nvt_i2c_bus *i2c, struct i2c_msg *msg)
{
	reinit_completion(&i2c->msg_complete);
	i2c->remaining   = msg->len;
	i2c->current_msg = msg;
	i2c->write_ptr   = 0;
	i2c->read_ptr    = 0;
	i2c->error_code  = 0;
	i2c->fifo_idx    = FIFO_1;
}

static int nvt_i2c_write(struct nvt_i2c_bus *i2c, struct i2c_msg *msg)
{
	int ret, offset = 0, write_bytes, fifo_num;
	unsigned int ctrl_mask = 0;
	unsigned long flags;

	ret = nvt_i2c_check_msg(msg);
	if (ret)
		return ret;

	spin_lock_irqsave(&i2c->lock, flags);
	nvt_i2c_prepare_xfer(i2c, msg);

	nt72676_i2c_writel((msg->len * 8) << 8, i2c, I2C_REG_SIZE);

	/*  Write FIFO data first */
	for (fifo_num = FIFO_1; fifo_num < FIFO_ALL && offset < msg->len; fifo_num++) {
		write_bytes = min(FIFO_CHUNK_SIZE, msg->len - offset);
		nvt_i2c_write_fifo(i2c, fifo_num == FIFO_1 ? I2C_REG_FIFO1 : I2C_REG_FIFO2,
				   msg->buf, offset, write_bytes);
		offset += write_bytes;
	}
	i2c->write_ptr = offset;
	i2c->remaining = msg->len - offset;
	i2c->fifo_idx = FIFO_1;

	/* Enable interrupt */
	nt72676_i2c_writel(I2C_IRQ_ENABLE_SETTING | I2C_IRQ_CLEAR_ALL,
			   i2c, I2C_REG_INTR);

	/* Trigger */
	switch (i2c->subaddr_mode) {
	case SUBADDR_8BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE;
		break;
	case SUBADDR_16BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_16BITSUBADDR_ENABLE;
		break;
	case SUBADDR_24BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_24BITSUBADDR_ENABLE;
		break;
	case SUBADDR_32BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_32BITSUBADDR_ENABLE;
		break;
	default:
		break;
	}
	ctrl_mask |= (((msg->addr << 1) << 8) | I2C_ENABLE |
			I2C_CLOCK_DUTY_ENABLE | I2C_CLOCK_STRETCH_ENABLE |
			I2C_MASTER_CLK_STRETCH_ENABLE | I2C_TRIGGER);
	nt72676_i2c_writel(ctrl_mask, i2c, I2C_REG_CTRL);
	spin_unlock_irqrestore(&i2c->lock, flags);

	ret = wait_for_completion_timeout(&i2c->msg_complete, i2c->adapter.timeout);

	spin_lock_irqsave(&i2c->lock, flags);
	if (!ret)
		i2c->error_code = -ETIMEDOUT;
	nvt_i2c_reset(i2c);
	ret = i2c->error_code;
	i2c->current_msg = NULL;
	spin_unlock_irqrestore(&i2c->lock, flags);

	if (ret)
		dev_dbg(i2c->dev, "Write failed (err:%d); SA[0x%X]\n",
			i2c->error_code, msg->addr);

	return ret;
}

static int nvt_i2c_read(struct nvt_i2c_bus *i2c, struct i2c_msg *msg)
{
	unsigned int ctrl_mask = 0;
	unsigned long flags;
	int ret;

	ret = nvt_i2c_check_msg(msg);
	if (ret)
		return ret;

	spin_lock_irqsave(&i2c->lock, flags);
	nvt_i2c_prepare_xfer(i2c, msg);

	nt72676_i2c_writel((msg->len * 8) << 8, i2c, I2C_REG_SIZE);

	/* Enable interrupt */
	nt72676_i2c_writel(I2C_IRQ_ENABLE_SETTING | I2C_IRQ_CLEAR_ALL,
			   i2c, I2C_REG_INTR);

	/* Trigger */
	switch (i2c->subaddr_mode) {
	case SUBADDR_8BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE;
		break;
	case SUBADDR_16BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_16BITSUBADDR_ENABLE;
		break;
	case SUBADDR_24BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_24BITSUBADDR_ENABLE;
		break;
	case SUBADDR_32BITS:
		ctrl_mask |= I2C_SUBADDR_ENABLE | I2C_32BITSUBADDR_ENABLE;
		break;
	default:
		break;
	}
	ctrl_mask |= (((msg->addr << 1) << 8) | I2C_ENABLE |
			I2C_REPEAT_ENABLE | I2C_READ_OPERATION |
			I2C_CLOCK_DUTY_ENABLE | I2C_CLOCK_STRETCH_ENABLE |
			I2C_MASTER_CLK_STRETCH_ENABLE | I2C_TRIGGER);
	nt72676_i2c_writel(ctrl_mask, i2c, I2C_REG_CTRL);
	spin_unlock_irqrestore(&i2c->lock, flags);

	ret = wait_for_completion_timeout(&i2c->msg_complete, i2c->adapter.timeout);

	spin_lock_irqsave(&i2c->lock, flags);
	if (!ret)
		i2c->error_code = -ETIMEDOUT;
	nvt_i2c_reset(i2c);
	ret = i2c->error_code;
	i2c->current_msg = NULL;
	spin_unlock_irqrestore(&i2c->lock, flags);

	if (ret)
		dev_dbg(i2c->dev, "Read failed (err:%d); SA[0x%X]\n",
			i2c->error_code, msg->addr);

	return ret;
}

static int nvt_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg msgs[],
			int num)
{
	struct nvt_i2c_bus *i2c = i2c_get_adapdata(adap);
	struct i2c_msg *msg = &msgs[0];
	int ret = 0;

	nvt_i2c_ctrl_init(i2c);

	if (num == 2) {
		ret = nvt_i2c_set_subaddr(i2c, msgs, num);
		if (ret)
			return ret;
		msg = &msgs[1];
	}

	if (msg->flags & I2C_M_RD)
		ret = nvt_i2c_read(i2c, msg);
	else
		ret = nvt_i2c_write(i2c, msg);

	if (ret < 0)
		return ret;
	return num;
}

static u32 nvt_i2c_func(struct i2c_adapter *adap)
{
	return (I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL) & ~I2C_FUNC_SMBUS_QUICK;
}

static const struct i2c_algorithm nvt_i2c_algo = {
	.master_xfer = nvt_i2c_xfer,
	.functionality = nvt_i2c_func,
};

static int nvt_i2c_parse_dts(struct nvt_i2c_bus *i2c)
{
	struct device *dev = i2c->dev;
	struct device_node *np = dev->of_node;
	int ret;

	/* read DTS(novatek,stbc-syscon) for STBC I2C */
	i2c->stbc_i2c = of_property_present(dev->of_node, "novatek,stbc-syscon");
	if (i2c->stbc_i2c) {
		i2c->stbc_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
								   "novatek,stbc-syscon");
		if (IS_ERR(i2c->stbc_regmap))
			return dev_err_probe(dev, PTR_ERR(i2c->stbc_regmap),
					     "Failed to get STBC syscon\n");
	}

	/* read DTS(clock-frequency) */
	ret = of_property_read_u32(np, "clock-frequency", &i2c->bus_clk_rate);
	if (ret || !i2c->bus_clk_rate) {
		dev_info(dev, "Not set dtb clock-frequency, set default 100kHz\n");
		i2c->bus_clk_rate = I2C_MAX_STANDARD_MODE_FREQ;
	}

	return 0;
}

static const struct of_device_id nvt_i2c_of_match[] = {
	{ .compatible = "novatek,nt72676-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, nvt_i2c_of_match);

static const struct i2c_adapter_quirks nvt_i2c_quirks = {
	.flags = I2C_AQ_COMB_WRITE_THEN_READ | I2C_AQ_NO_ZERO_LEN,
	.max_num_msgs = 2,
	.max_write_len = 4096,
	.max_read_len = 4096,
	.max_comb_1st_msg_len = SUBADDR_MAX_LEN,
	.max_comb_2nd_msg_len = 4096,
};

static int nvt_i2c_probe(struct platform_device *pdev)
{
	struct nvt_i2c_bus *i2c;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	spin_lock_init(&i2c->lock);
	init_completion(&i2c->msg_complete);
	i2c->dev = &pdev->dev;

	ret = nvt_i2c_parse_dts(i2c);
	if (ret)
		return ret;

	i2c->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c->clk),
				     "devm_clk_get_enabled fail\n");

	i2c->input_clk_rate = clk_get_rate(i2c->clk);
	if (!i2c->input_clk_rate)
		return dev_err_probe(&pdev->dev, -EINVAL, "Invalid input clock rate\n");

	if (i2c->input_clk_rate < i2c->bus_clk_rate)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Input clock rate %lu is lower than bus frequency %u\n",
				     i2c->input_clk_rate, i2c->bus_clk_rate);

	i2c->irq = platform_get_irq(pdev, 0);
	if (i2c->irq < 0)
		return i2c->irq;

	ret = nvt_i2c_init(i2c);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "nvt_i2c_init fail\n");

	ret = devm_request_irq(&pdev->dev, i2c->irq, nvt_i2c_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), i2c);
	if (ret) {
		nvt_i2c_release(i2c);
		return dev_err_probe(&pdev->dev, ret, "devm_request_irq fail\n");
	}

	/* Setup I2C adapter */
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.algo = &nvt_i2c_algo;
	i2c->adapter.dev.of_node = pdev->dev.of_node;
	i2c->adapter.dev.parent = &pdev->dev;
	i2c->adapter.quirks = &nvt_i2c_quirks;
	i2c->adapter.timeout = 3 * HZ;
	strscpy(i2c->adapter.name, dev_name(&pdev->dev), sizeof(i2c->adapter.name));
	i2c_set_adapdata(&i2c->adapter, i2c);

	ret = i2c_add_adapter(&i2c->adapter);
	if (ret) {
		nvt_i2c_release(i2c);
		return dev_err_probe(&pdev->dev, ret, "Failed to add adapter\n");
	}

	platform_set_drvdata(pdev, i2c);

	return 0;
}

static void nvt_i2c_remove(struct platform_device *pdev)
{
	struct nvt_i2c_bus *i2c = platform_get_drvdata(pdev);
	unsigned long flags;

	i2c_del_adapter(&i2c->adapter);
	spin_lock_irqsave(&i2c->lock, flags);
	nvt_i2c_release(i2c);
	spin_unlock_irqrestore(&i2c->lock, flags);
}

static const struct dev_pm_ops nvt_i2c_pm_ops = {
	.resume_early = nvt_i2c_resume,
	.suspend_late = nvt_i2c_suspend,
};

static struct platform_driver nvt_i2c_driver = {
	.probe = nvt_i2c_probe,
	.remove = nvt_i2c_remove,
	.driver = {
		.name = "nt72676_i2c",
		.pm = &nvt_i2c_pm_ops,
		.of_match_table = of_match_ptr(nvt_i2c_of_match),
	},
};
module_platform_driver(nvt_i2c_driver);

MODULE_DESCRIPTION("Novatek NT72676 SoC I2C Bus Driver");
MODULE_AUTHOR("Ben Huang <ben_huang@novatek.com.tw>");
MODULE_LICENSE("GPL");
