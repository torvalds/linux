// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip CoreI2C I2C controller driver
 *
 * Copyright (c) 2018-2022 Microchip Corporation. All rights reserved.
 *
 * Author: Daire McNamara <daire.mcnamara@microchip.com>
 * Author: Conor Dooley <conor.dooley@microchip.com>
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define CORE_I2C_CTRL	(0x00)
#define  CTRL_CR0	BIT(0)
#define  CTRL_CR1	BIT(1)
#define  CTRL_AA	BIT(2)
#define  CTRL_SI	BIT(3)
#define  CTRL_STO	BIT(4)
#define  CTRL_STA	BIT(5)
#define  CTRL_ENS1	BIT(6)
#define  CTRL_CR2	BIT(7)

#define STATUS_BUS_ERROR			(0x00)
#define STATUS_M_START_SENT			(0x08)
#define STATUS_M_REPEATED_START_SENT		(0x10)
#define STATUS_M_SLAW_ACK			(0x18)
#define STATUS_M_SLAW_NACK			(0x20)
#define STATUS_M_TX_DATA_ACK			(0x28)
#define STATUS_M_TX_DATA_NACK			(0x30)
#define STATUS_M_ARB_LOST			(0x38)
#define STATUS_M_SLAR_ACK			(0x40)
#define STATUS_M_SLAR_NACK			(0x48)
#define STATUS_M_RX_DATA_ACKED			(0x50)
#define STATUS_M_RX_DATA_NACKED			(0x58)
#define STATUS_S_SLAW_ACKED			(0x60)
#define STATUS_S_ARB_LOST_SLAW_ACKED		(0x68)
#define STATUS_S_GENERAL_CALL_ACKED		(0x70)
#define STATUS_S_ARB_LOST_GENERAL_CALL_ACKED	(0x78)
#define STATUS_S_RX_DATA_ACKED			(0x80)
#define STATUS_S_RX_DATA_NACKED			(0x88)
#define STATUS_S_GENERAL_CALL_RX_DATA_ACKED	(0x90)
#define STATUS_S_GENERAL_CALL_RX_DATA_NACKED	(0x98)
#define STATUS_S_RX_STOP			(0xA0)
#define STATUS_S_SLAR_ACKED			(0xA8)
#define STATUS_S_ARB_LOST_SLAR_ACKED		(0xB0)
#define STATUS_S_TX_DATA_ACK			(0xB8)
#define STATUS_S_TX_DATA_NACK			(0xC0)
#define STATUS_LAST_DATA_ACK			(0xC8)
#define STATUS_M_SMB_MASTER_RESET		(0xD0)
#define STATUS_S_SCL_LOW_TIMEOUT		(0xD8) /* 25 ms */
#define STATUS_NO_STATE_INFO			(0xF8)

#define CORE_I2C_STATUS		(0x04)
#define CORE_I2C_DATA		(0x08)
#define WRITE_BIT		(0x0)
#define READ_BIT		(0x1)
#define SLAVE_ADDR_SHIFT	(1)
#define CORE_I2C_SLAVE0_ADDR	(0x0c)
#define GENERAL_CALL_BIT	(0x0)
#define CORE_I2C_SMBUS		(0x10)
#define SMBALERT_INT_ENB	(0x0)
#define SMBSUS_INT_ENB		(0x1)
#define SMBUS_ENB		(0x2)
#define SMBALERT_NI_STATUS	(0x3)
#define SMBALERT_NO_CTRL	(0x4)
#define SMBSUS_NI_STATUS	(0x5)
#define SMBSUS_NO_CTRL		(0x6)
#define SMBUS_RESET		(0x7)
#define CORE_I2C_FREQ		(0x14)
#define CORE_I2C_GLITCHREG	(0x18)
#define CORE_I2C_SLAVE1_ADDR	(0x1c)

#define PCLK_DIV_960	(CTRL_CR2)
#define PCLK_DIV_256	(0)
#define PCLK_DIV_224	(CTRL_CR0)
#define PCLK_DIV_192	(CTRL_CR1)
#define PCLK_DIV_160	(CTRL_CR0 | CTRL_CR1)
#define PCLK_DIV_120	(CTRL_CR0 | CTRL_CR2)
#define PCLK_DIV_60	(CTRL_CR1 | CTRL_CR2)
#define BCLK_DIV_8	(CTRL_CR0 | CTRL_CR1 | CTRL_CR2)
#define CLK_MASK	(CTRL_CR0 | CTRL_CR1 | CTRL_CR2)

/**
 * struct mchp_corei2c_dev - Microchip CoreI2C device private data
 *
 * @base:		pointer to register struct
 * @dev:		device reference
 * @i2c_clk:		clock reference for i2c input clock
 * @msg_queue:		pointer to the messages requiring sending
 * @buf:		pointer to msg buffer for easier use
 * @msg_complete:	xfer completion object
 * @adapter:		core i2c abstraction
 * @msg_err:		error code for completed message
 * @bus_clk_rate:	current i2c bus clock rate
 * @isr_status:		cached copy of local ISR status
 * @total_num:		total number of messages to be sent/received
 * @current_num:	index of the current message being sent/received
 * @msg_len:		number of bytes transferred in msg
 * @addr:		address of the current slave
 * @restart_needed:	whether or not a repeated start is required after current message
 */
struct mchp_corei2c_dev {
	void __iomem *base;
	struct device *dev;
	struct clk *i2c_clk;
	struct i2c_msg *msg_queue;
	u8 *buf;
	struct completion msg_complete;
	struct i2c_adapter adapter;
	int msg_err;
	int total_num;
	int current_num;
	u32 bus_clk_rate;
	u32 isr_status;
	u16 msg_len;
	u8 addr;
	bool restart_needed;
};

static void mchp_corei2c_core_disable(struct mchp_corei2c_dev *idev)
{
	u8 ctrl = readb(idev->base + CORE_I2C_CTRL);

	ctrl &= ~CTRL_ENS1;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);
}

static void mchp_corei2c_core_enable(struct mchp_corei2c_dev *idev)
{
	u8 ctrl = readb(idev->base + CORE_I2C_CTRL);

	ctrl |= CTRL_ENS1;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);
}

static void mchp_corei2c_reset(struct mchp_corei2c_dev *idev)
{
	mchp_corei2c_core_disable(idev);
	mchp_corei2c_core_enable(idev);
}

static inline void mchp_corei2c_stop(struct mchp_corei2c_dev *idev)
{
	u8 ctrl = readb(idev->base + CORE_I2C_CTRL);

	ctrl |= CTRL_STO;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);
}

static inline int mchp_corei2c_set_divisor(u32 rate,
					   struct mchp_corei2c_dev *idev)
{
	u8 clkval, ctrl;

	if (rate >= 960)
		clkval = PCLK_DIV_960;
	else if (rate >= 256)
		clkval = PCLK_DIV_256;
	else if (rate >= 224)
		clkval = PCLK_DIV_224;
	else if (rate >= 192)
		clkval = PCLK_DIV_192;
	else if (rate >= 160)
		clkval = PCLK_DIV_160;
	else if (rate >= 120)
		clkval = PCLK_DIV_120;
	else if (rate >= 60)
		clkval = PCLK_DIV_60;
	else if (rate >= 8)
		clkval = BCLK_DIV_8;
	else
		return -EINVAL;

	ctrl = readb(idev->base + CORE_I2C_CTRL);
	ctrl &= ~CLK_MASK;
	ctrl |= clkval;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);

	ctrl = readb(idev->base + CORE_I2C_CTRL);
	if ((ctrl & CLK_MASK) != clkval)
		return -EIO;

	return 0;
}

static int mchp_corei2c_init(struct mchp_corei2c_dev *idev)
{
	u32 clk_rate = clk_get_rate(idev->i2c_clk);
	u32 divisor = clk_rate / idev->bus_clk_rate;
	int ret;

	ret = mchp_corei2c_set_divisor(divisor, idev);
	if (ret)
		return ret;

	mchp_corei2c_reset(idev);

	return 0;
}

static void mchp_corei2c_empty_rx(struct mchp_corei2c_dev *idev)
{
	u8 ctrl;

	if (idev->msg_len > 0) {
		*idev->buf++ = readb(idev->base + CORE_I2C_DATA);
		idev->msg_len--;
	}

	if (idev->msg_len <= 1) {
		ctrl = readb(idev->base + CORE_I2C_CTRL);
		ctrl &= ~CTRL_AA;
		writeb(ctrl, idev->base + CORE_I2C_CTRL);
	}
}

static int mchp_corei2c_fill_tx(struct mchp_corei2c_dev *idev)
{
	if (idev->msg_len > 0)
		writeb(*idev->buf++, idev->base + CORE_I2C_DATA);
	idev->msg_len--;

	return 0;
}

static void mchp_corei2c_next_msg(struct mchp_corei2c_dev *idev)
{
	struct i2c_msg *this_msg;
	u8 ctrl;

	if (idev->current_num >= idev->total_num) {
		complete(&idev->msg_complete);
		return;
	}

	/*
	 * If there's been an error, the isr needs to return control
	 * to the "main" part of the driver, so as not to keep sending
	 * messages once it completes and clears the SI bit.
	 */
	if (idev->msg_err) {
		complete(&idev->msg_complete);
		return;
	}

	this_msg = idev->msg_queue++;

	if (idev->current_num < (idev->total_num - 1)) {
		struct i2c_msg *next_msg = idev->msg_queue;

		idev->restart_needed = next_msg->flags & I2C_M_RD;
	} else {
		idev->restart_needed = false;
	}

	idev->addr = i2c_8bit_addr_from_msg(this_msg);
	idev->msg_len = this_msg->len;
	idev->buf = this_msg->buf;

	ctrl = readb(idev->base + CORE_I2C_CTRL);
	ctrl |= CTRL_STA;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);

	idev->current_num++;
}

static irqreturn_t mchp_corei2c_handle_isr(struct mchp_corei2c_dev *idev)
{
	u32 status = idev->isr_status;
	u8 ctrl;
	bool last_byte = false, finished = false;

	if (!idev->buf)
		return IRQ_NONE;

	switch (status) {
	case STATUS_M_START_SENT:
	case STATUS_M_REPEATED_START_SENT:
		ctrl = readb(idev->base + CORE_I2C_CTRL);
		ctrl &= ~CTRL_STA;
		writeb(idev->addr, idev->base + CORE_I2C_DATA);
		writeb(ctrl, idev->base + CORE_I2C_CTRL);
		break;
	case STATUS_M_ARB_LOST:
		idev->msg_err = -EAGAIN;
		finished = true;
		break;
	case STATUS_M_SLAW_ACK:
	case STATUS_M_TX_DATA_ACK:
		if (idev->msg_len > 0) {
			mchp_corei2c_fill_tx(idev);
		} else {
			if (idev->restart_needed)
				finished = true;
			else
				last_byte = true;
		}
		break;
	case STATUS_M_TX_DATA_NACK:
	case STATUS_M_SLAR_NACK:
	case STATUS_M_SLAW_NACK:
		idev->msg_err = -ENXIO;
		last_byte = true;
		break;
	case STATUS_M_SLAR_ACK:
		ctrl = readb(idev->base + CORE_I2C_CTRL);
		if (idev->msg_len == 1u) {
			ctrl &= ~CTRL_AA;
			writeb(ctrl, idev->base + CORE_I2C_CTRL);
		} else {
			ctrl |= CTRL_AA;
			writeb(ctrl, idev->base + CORE_I2C_CTRL);
		}
		if (idev->msg_len < 1u)
			last_byte = true;
		break;
	case STATUS_M_RX_DATA_ACKED:
		mchp_corei2c_empty_rx(idev);
		break;
	case STATUS_M_RX_DATA_NACKED:
		mchp_corei2c_empty_rx(idev);
		if (idev->msg_len == 0)
			last_byte = true;
		break;
	default:
		break;
	}

	/* On the last byte to be transmitted, send STOP */
	if (last_byte)
		mchp_corei2c_stop(idev);

	if (last_byte || finished)
		mchp_corei2c_next_msg(idev);

	return IRQ_HANDLED;
}

static irqreturn_t mchp_corei2c_isr(int irq, void *_dev)
{
	struct mchp_corei2c_dev *idev = _dev;
	irqreturn_t ret = IRQ_NONE;
	u8 ctrl;

	ctrl = readb(idev->base + CORE_I2C_CTRL);
	if (ctrl & CTRL_SI) {
		idev->isr_status = readb(idev->base + CORE_I2C_STATUS);
		ret = mchp_corei2c_handle_isr(idev);
	}

	ctrl = readb(idev->base + CORE_I2C_CTRL);
	ctrl &= ~CTRL_SI;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);

	return ret;
}

static int mchp_corei2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			     int num)
{
	struct mchp_corei2c_dev *idev = i2c_get_adapdata(adap);
	struct i2c_msg *this_msg = msgs;
	unsigned long time_left;
	u8 ctrl;

	mchp_corei2c_core_enable(idev);

	/*
	 * The isr controls the flow of a transfer, this info needs to be saved
	 * to a location that it can access the queue information from.
	 */
	idev->restart_needed = false;
	idev->msg_queue = msgs;
	idev->total_num = num;
	idev->current_num = 0;

	/*
	 * But the first entry to the isr is triggered by the start in this
	 * function, so the first message needs to be "dequeued".
	 */
	idev->addr = i2c_8bit_addr_from_msg(this_msg);
	idev->msg_len = this_msg->len;
	idev->buf = this_msg->buf;
	idev->msg_err = 0;

	if (idev->total_num > 1) {
		struct i2c_msg *next_msg = msgs + 1;

		idev->restart_needed = next_msg->flags & I2C_M_RD;
	}

	idev->current_num++;
	idev->msg_queue++;

	reinit_completion(&idev->msg_complete);

	/*
	 * Send the first start to pass control to the isr
	 */
	ctrl = readb(idev->base + CORE_I2C_CTRL);
	ctrl |= CTRL_STA;
	writeb(ctrl, idev->base + CORE_I2C_CTRL);

	time_left = wait_for_completion_timeout(&idev->msg_complete,
						idev->adapter.timeout);
	if (!time_left)
		return -ETIMEDOUT;

	if (idev->msg_err)
		return idev->msg_err;

	return num;
}

static u32 mchp_corei2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mchp_corei2c_algo = {
	.master_xfer = mchp_corei2c_xfer,
	.functionality = mchp_corei2c_func,
};

static int mchp_corei2c_probe(struct platform_device *pdev)
{
	struct mchp_corei2c_dev *idev;
	struct resource *res;
	int irq, ret;

	idev = devm_kzalloc(&pdev->dev, sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	idev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(idev->base))
		return PTR_ERR(idev->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	idev->i2c_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(idev->i2c_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(idev->i2c_clk),
				     "missing clock\n");

	idev->dev = &pdev->dev;
	init_completion(&idev->msg_complete);

	ret = device_property_read_u32(idev->dev, "clock-frequency",
				       &idev->bus_clk_rate);
	if (ret || !idev->bus_clk_rate) {
		dev_info(&pdev->dev, "default to 100kHz\n");
		idev->bus_clk_rate = 100000;
	}

	if (idev->bus_clk_rate > 400000)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "clock-frequency too high: %d\n",
				     idev->bus_clk_rate);

	/*
	 * This driver supports both the hard peripherals & soft FPGA cores.
	 * The hard peripherals do not have shared IRQs, but we don't have
	 * control over what way the interrupts are wired for the soft cores.
	 */
	ret = devm_request_irq(&pdev->dev, irq, mchp_corei2c_isr, IRQF_SHARED,
			       pdev->name, idev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to claim irq %d\n", irq);

	ret = clk_prepare_enable(idev->i2c_clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to enable clock\n");

	ret = mchp_corei2c_init(idev);
	if (ret) {
		clk_disable_unprepare(idev->i2c_clk);
		return dev_err_probe(&pdev->dev, ret, "failed to program clock divider\n");
	}

	i2c_set_adapdata(&idev->adapter, idev);
	snprintf(idev->adapter.name, sizeof(idev->adapter.name),
		 "Microchip I2C hw bus at %08lx", (unsigned long)res->start);
	idev->adapter.owner = THIS_MODULE;
	idev->adapter.algo = &mchp_corei2c_algo;
	idev->adapter.dev.parent = &pdev->dev;
	idev->adapter.dev.of_node = pdev->dev.of_node;
	idev->adapter.timeout = HZ;

	platform_set_drvdata(pdev, idev);

	ret = i2c_add_adapter(&idev->adapter);
	if (ret) {
		clk_disable_unprepare(idev->i2c_clk);
		return ret;
	}

	dev_info(&pdev->dev, "registered CoreI2C bus driver\n");

	return 0;
}

static void mchp_corei2c_remove(struct platform_device *pdev)
{
	struct mchp_corei2c_dev *idev = platform_get_drvdata(pdev);

	clk_disable_unprepare(idev->i2c_clk);
	i2c_del_adapter(&idev->adapter);
}

static const struct of_device_id mchp_corei2c_of_match[] = {
	{ .compatible = "microchip,mpfs-i2c" },
	{ .compatible = "microchip,corei2c-rtl-v7" },
	{},
};
MODULE_DEVICE_TABLE(of, mchp_corei2c_of_match);

static struct platform_driver mchp_corei2c_driver = {
	.probe = mchp_corei2c_probe,
	.remove_new = mchp_corei2c_remove,
	.driver = {
		.name = "microchip-corei2c",
		.of_match_table = mchp_corei2c_of_match,
	},
};

module_platform_driver(mchp_corei2c_driver);

MODULE_DESCRIPTION("Microchip CoreI2C bus driver");
MODULE_AUTHOR("Daire McNamara <daire.mcnamara@microchip.com>");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_LICENSE("GPL");
