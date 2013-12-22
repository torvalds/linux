/**
 * i2c-exynos5.c - Samsung Exynos5 I2C Controller Driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

/*
 * HSI2C controller from Samsung supports 2 modes of operation
 * 1. Auto mode: Where in master automatically controls the whole transaction
 * 2. Manual mode: Software controls the transaction by issuing commands
 *    START, READ, WRITE, STOP, RESTART in I2C_MANUAL_CMD register.
 *
 * Operation mode can be selected by setting AUTO_MODE bit in I2C_CONF register
 *
 * Special bits are available for both modes of operation to set commands
 * and for checking transfer status
 */

/* Register Map */
#define HSI2C_CTL		0x00
#define HSI2C_FIFO_CTL		0x04
#define HSI2C_TRAILIG_CTL	0x08
#define HSI2C_CLK_CTL		0x0C
#define HSI2C_CLK_SLOT		0x10
#define HSI2C_INT_ENABLE	0x20
#define HSI2C_INT_STATUS	0x24
#define HSI2C_ERR_STATUS	0x2C
#define HSI2C_FIFO_STATUS	0x30
#define HSI2C_TX_DATA		0x34
#define HSI2C_RX_DATA		0x38
#define HSI2C_CONF		0x40
#define HSI2C_AUTO_CONF		0x44
#define HSI2C_TIMEOUT		0x48
#define HSI2C_MANUAL_CMD	0x4C
#define HSI2C_TRANS_STATUS	0x50
#define HSI2C_TIMING_HS1	0x54
#define HSI2C_TIMING_HS2	0x58
#define HSI2C_TIMING_HS3	0x5C
#define HSI2C_TIMING_FS1	0x60
#define HSI2C_TIMING_FS2	0x64
#define HSI2C_TIMING_FS3	0x68
#define HSI2C_TIMING_SLA	0x6C
#define HSI2C_ADDR		0x70

/* I2C_CTL Register bits */
#define HSI2C_FUNC_MODE_I2C			(1u << 0)
#define HSI2C_MASTER				(1u << 3)
#define HSI2C_RXCHON				(1u << 6)
#define HSI2C_TXCHON				(1u << 7)
#define HSI2C_SW_RST				(1u << 31)

/* I2C_FIFO_CTL Register bits */
#define HSI2C_RXFIFO_EN				(1u << 0)
#define HSI2C_TXFIFO_EN				(1u << 1)
#define HSI2C_RXFIFO_TRIGGER_LEVEL(x)		((x) << 4)
#define HSI2C_TXFIFO_TRIGGER_LEVEL(x)		((x) << 16)

/* As per user manual FIFO max depth is 64bytes */
#define HSI2C_FIFO_MAX				0x40
/* default trigger levels for Tx and Rx FIFOs */
#define HSI2C_DEF_TXFIFO_LVL			(HSI2C_FIFO_MAX - 0x30)
#define HSI2C_DEF_RXFIFO_LVL			(HSI2C_FIFO_MAX - 0x10)

/* I2C_TRAILING_CTL Register bits */
#define HSI2C_TRAILING_COUNT			(0xf)

/* I2C_INT_EN Register bits */
#define HSI2C_INT_TX_ALMOSTEMPTY_EN		(1u << 0)
#define HSI2C_INT_RX_ALMOSTFULL_EN		(1u << 1)
#define HSI2C_INT_TRAILING_EN			(1u << 6)
#define HSI2C_INT_I2C_EN			(1u << 9)

/* I2C_INT_STAT Register bits */
#define HSI2C_INT_TX_ALMOSTEMPTY		(1u << 0)
#define HSI2C_INT_RX_ALMOSTFULL			(1u << 1)
#define HSI2C_INT_TX_UNDERRUN			(1u << 2)
#define HSI2C_INT_TX_OVERRUN			(1u << 3)
#define HSI2C_INT_RX_UNDERRUN			(1u << 4)
#define HSI2C_INT_RX_OVERRUN			(1u << 5)
#define HSI2C_INT_TRAILING			(1u << 6)
#define HSI2C_INT_I2C				(1u << 9)

/* I2C_FIFO_STAT Register bits */
#define HSI2C_RX_FIFO_EMPTY			(1u << 24)
#define HSI2C_RX_FIFO_FULL			(1u << 23)
#define HSI2C_RX_FIFO_LVL(x)			((x >> 16) & 0x7f)
#define HSI2C_TX_FIFO_EMPTY			(1u << 8)
#define HSI2C_TX_FIFO_FULL			(1u << 7)
#define HSI2C_TX_FIFO_LVL(x)			((x >> 0) & 0x7f)

/* I2C_CONF Register bits */
#define HSI2C_AUTO_MODE				(1u << 31)
#define HSI2C_10BIT_ADDR_MODE			(1u << 30)
#define HSI2C_HS_MODE				(1u << 29)

/* I2C_AUTO_CONF Register bits */
#define HSI2C_READ_WRITE			(1u << 16)
#define HSI2C_STOP_AFTER_TRANS			(1u << 17)
#define HSI2C_MASTER_RUN			(1u << 31)

/* I2C_TIMEOUT Register bits */
#define HSI2C_TIMEOUT_EN			(1u << 31)
#define HSI2C_TIMEOUT_MASK			0xff

/* I2C_TRANS_STATUS register bits */
#define HSI2C_MASTER_BUSY			(1u << 17)
#define HSI2C_SLAVE_BUSY			(1u << 16)
#define HSI2C_TIMEOUT_AUTO			(1u << 4)
#define HSI2C_NO_DEV				(1u << 3)
#define HSI2C_NO_DEV_ACK			(1u << 2)
#define HSI2C_TRANS_ABORT			(1u << 1)
#define HSI2C_TRANS_DONE			(1u << 0)

/* I2C_ADDR register bits */
#define HSI2C_SLV_ADDR_SLV(x)			((x & 0x3ff) << 0)
#define HSI2C_SLV_ADDR_MAS(x)			((x & 0x3ff) << 10)
#define HSI2C_MASTER_ID(x)			((x & 0xff) << 24)
#define MASTER_ID(x)				((x & 0x7) + 0x08)

/*
 * Controller operating frequency, timing values for operation
 * are calculated against this frequency
 */
#define HSI2C_HS_TX_CLOCK	1000000
#define HSI2C_FS_TX_CLOCK	100000
#define HSI2C_HIGH_SPD		1
#define HSI2C_FAST_SPD		0

#define EXYNOS5_I2C_TIMEOUT (msecs_to_jiffies(1000))

struct exynos5_i2c {
	struct i2c_adapter	adap;
	unsigned int		suspended:1;

	struct i2c_msg		*msg;
	struct completion	msg_complete;
	unsigned int		msg_ptr;

	unsigned int		irq;

	void __iomem		*regs;
	struct clk		*clk;
	struct device		*dev;
	int			state;

	spinlock_t		lock;		/* IRQ synchronization */

	/*
	 * Since the TRANS_DONE bit is cleared on read, and we may read it
	 * either during an IRQ or after a transaction, keep track of its
	 * state here.
	 */
	int			trans_done;

	/* Controller operating frequency */
	unsigned int		fs_clock;
	unsigned int		hs_clock;

	/*
	 * HSI2C Controller can operate in
	 * 1. High speed upto 3.4Mbps
	 * 2. Fast speed upto 1Mbps
	 */
	int			speed_mode;
};

static const struct of_device_id exynos5_i2c_match[] = {
	{ .compatible = "samsung,exynos5-hsi2c" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos5_i2c_match);

static void exynos5_i2c_clr_pend_irq(struct exynos5_i2c *i2c)
{
	writel(readl(i2c->regs + HSI2C_INT_STATUS),
				i2c->regs + HSI2C_INT_STATUS);
}

/*
 * exynos5_i2c_set_timing: updates the registers with appropriate
 * timing values calculated
 *
 * Returns 0 on success, -EINVAL if the cycle length cannot
 * be calculated.
 */
static int exynos5_i2c_set_timing(struct exynos5_i2c *i2c, int mode)
{
	u32 i2c_timing_s1;
	u32 i2c_timing_s2;
	u32 i2c_timing_s3;
	u32 i2c_timing_sla;
	unsigned int t_start_su, t_start_hd;
	unsigned int t_stop_su;
	unsigned int t_data_su, t_data_hd;
	unsigned int t_scl_l, t_scl_h;
	unsigned int t_sr_release;
	unsigned int t_ftl_cycle;
	unsigned int clkin = clk_get_rate(i2c->clk);
	unsigned int div, utemp0 = 0, utemp1 = 0, clk_cycle;
	unsigned int op_clk = (mode == HSI2C_HIGH_SPD) ?
				i2c->hs_clock : i2c->fs_clock;

	/*
	 * FPCLK / FI2C =
	 * (CLK_DIV + 1) * (TSCLK_L + TSCLK_H + 2) + 8 + 2 * FLT_CYCLE
	 * utemp0 = (CLK_DIV + 1) * (TSCLK_L + TSCLK_H + 2)
	 * utemp1 = (TSCLK_L + TSCLK_H + 2)
	 */
	t_ftl_cycle = (readl(i2c->regs + HSI2C_CONF) >> 16) & 0x7;
	utemp0 = (clkin / op_clk) - 8 - 2 * t_ftl_cycle;

	/* CLK_DIV max is 256 */
	for (div = 0; div < 256; div++) {
		utemp1 = utemp0 / (div + 1);

		/*
		 * SCL_L and SCL_H each has max value of 255
		 * Hence, For the clk_cycle to the have right value
		 * utemp1 has to be less then 512 and more than 4.
		 */
		if ((utemp1 < 512) && (utemp1 > 4)) {
			clk_cycle = utemp1 - 2;
			break;
		} else if (div == 255) {
			dev_warn(i2c->dev, "Failed to calculate divisor");
			return -EINVAL;
		}
	}

	t_scl_l = clk_cycle / 2;
	t_scl_h = clk_cycle / 2;
	t_start_su = t_scl_l;
	t_start_hd = t_scl_l;
	t_stop_su = t_scl_l;
	t_data_su = t_scl_l / 2;
	t_data_hd = t_scl_l / 2;
	t_sr_release = clk_cycle;

	i2c_timing_s1 = t_start_su << 24 | t_start_hd << 16 | t_stop_su << 8;
	i2c_timing_s2 = t_data_su << 24 | t_scl_l << 8 | t_scl_h << 0;
	i2c_timing_s3 = div << 16 | t_sr_release << 0;
	i2c_timing_sla = t_data_hd << 0;

	dev_dbg(i2c->dev, "tSTART_SU: %X, tSTART_HD: %X, tSTOP_SU: %X\n",
		t_start_su, t_start_hd, t_stop_su);
	dev_dbg(i2c->dev, "tDATA_SU: %X, tSCL_L: %X, tSCL_H: %X\n",
		t_data_su, t_scl_l, t_scl_h);
	dev_dbg(i2c->dev, "nClkDiv: %X, tSR_RELEASE: %X\n",
		div, t_sr_release);
	dev_dbg(i2c->dev, "tDATA_HD: %X\n", t_data_hd);

	if (mode == HSI2C_HIGH_SPD) {
		writel(i2c_timing_s1, i2c->regs + HSI2C_TIMING_HS1);
		writel(i2c_timing_s2, i2c->regs + HSI2C_TIMING_HS2);
		writel(i2c_timing_s3, i2c->regs + HSI2C_TIMING_HS3);
	} else {
		writel(i2c_timing_s1, i2c->regs + HSI2C_TIMING_FS1);
		writel(i2c_timing_s2, i2c->regs + HSI2C_TIMING_FS2);
		writel(i2c_timing_s3, i2c->regs + HSI2C_TIMING_FS3);
	}
	writel(i2c_timing_sla, i2c->regs + HSI2C_TIMING_SLA);

	return 0;
}

static int exynos5_hsi2c_clock_setup(struct exynos5_i2c *i2c)
{
	/*
	 * Configure the Fast speed timing values
	 * Even the High Speed mode initially starts with Fast mode
	 */
	if (exynos5_i2c_set_timing(i2c, HSI2C_FAST_SPD)) {
		dev_err(i2c->dev, "HSI2C FS Clock set up failed\n");
		return -EINVAL;
	}

	/* configure the High speed timing values */
	if (i2c->speed_mode == HSI2C_HIGH_SPD) {
		if (exynos5_i2c_set_timing(i2c, HSI2C_HIGH_SPD)) {
			dev_err(i2c->dev, "HSI2C HS Clock set up failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * exynos5_i2c_init: configures the controller for I2C functionality
 * Programs I2C controller for Master mode operation
 */
static void exynos5_i2c_init(struct exynos5_i2c *i2c)
{
	u32 i2c_conf = readl(i2c->regs + HSI2C_CONF);
	u32 i2c_timeout = readl(i2c->regs + HSI2C_TIMEOUT);

	/* Clear to disable Timeout */
	i2c_timeout &= ~HSI2C_TIMEOUT_EN;
	writel(i2c_timeout, i2c->regs + HSI2C_TIMEOUT);

	writel((HSI2C_FUNC_MODE_I2C | HSI2C_MASTER),
					i2c->regs + HSI2C_CTL);
	writel(HSI2C_TRAILING_COUNT, i2c->regs + HSI2C_TRAILIG_CTL);

	if (i2c->speed_mode == HSI2C_HIGH_SPD) {
		writel(HSI2C_MASTER_ID(MASTER_ID(i2c->adap.nr)),
					i2c->regs + HSI2C_ADDR);
		i2c_conf |= HSI2C_HS_MODE;
	}

	writel(i2c_conf | HSI2C_AUTO_MODE, i2c->regs + HSI2C_CONF);
}

static void exynos5_i2c_reset(struct exynos5_i2c *i2c)
{
	u32 i2c_ctl;

	/* Set and clear the bit for reset */
	i2c_ctl = readl(i2c->regs + HSI2C_CTL);
	i2c_ctl |= HSI2C_SW_RST;
	writel(i2c_ctl, i2c->regs + HSI2C_CTL);

	i2c_ctl = readl(i2c->regs + HSI2C_CTL);
	i2c_ctl &= ~HSI2C_SW_RST;
	writel(i2c_ctl, i2c->regs + HSI2C_CTL);

	/* We don't expect calculations to fail during the run */
	exynos5_hsi2c_clock_setup(i2c);
	/* Initialize the configure registers */
	exynos5_i2c_init(i2c);
}

/*
 * exynos5_i2c_irq: top level IRQ servicing routine
 *
 * INT_STATUS registers gives the interrupt details. Further,
 * FIFO_STATUS or TRANS_STATUS registers are to be check for detailed
 * state of the bus.
 */
static irqreturn_t exynos5_i2c_irq(int irqno, void *dev_id)
{
	struct exynos5_i2c *i2c = dev_id;
	u32 fifo_level, int_status, fifo_status, trans_status;
	unsigned char byte;
	int len = 0;

	i2c->state = -EINVAL;

	spin_lock(&i2c->lock);

	int_status = readl(i2c->regs + HSI2C_INT_STATUS);
	writel(int_status, i2c->regs + HSI2C_INT_STATUS);
	fifo_status = readl(i2c->regs + HSI2C_FIFO_STATUS);

	/* handle interrupt related to the transfer status */
	if (int_status & HSI2C_INT_I2C) {
		trans_status = readl(i2c->regs + HSI2C_TRANS_STATUS);
		if (trans_status & HSI2C_NO_DEV_ACK) {
			dev_dbg(i2c->dev, "No ACK from device\n");
			i2c->state = -ENXIO;
			goto stop;
		} else if (trans_status & HSI2C_NO_DEV) {
			dev_dbg(i2c->dev, "No device\n");
			i2c->state = -ENXIO;
			goto stop;
		} else if (trans_status & HSI2C_TRANS_ABORT) {
			dev_dbg(i2c->dev, "Deal with arbitration lose\n");
			i2c->state = -EAGAIN;
			goto stop;
		} else if (trans_status & HSI2C_TIMEOUT_AUTO) {
			dev_dbg(i2c->dev, "Accessing device timed out\n");
			i2c->state = -EAGAIN;
			goto stop;
		} else if (trans_status & HSI2C_TRANS_DONE) {
			i2c->trans_done = 1;
			i2c->state = 0;
		}
	}

	if ((i2c->msg->flags & I2C_M_RD) && (int_status &
			(HSI2C_INT_TRAILING | HSI2C_INT_RX_ALMOSTFULL))) {
		fifo_status = readl(i2c->regs + HSI2C_FIFO_STATUS);
		fifo_level = HSI2C_RX_FIFO_LVL(fifo_status);
		len = min(fifo_level, i2c->msg->len - i2c->msg_ptr);

		while (len > 0) {
			byte = (unsigned char)
				readl(i2c->regs + HSI2C_RX_DATA);
			i2c->msg->buf[i2c->msg_ptr++] = byte;
			len--;
		}
		i2c->state = 0;
	} else if (int_status & HSI2C_INT_TX_ALMOSTEMPTY) {
		fifo_status = readl(i2c->regs + HSI2C_FIFO_STATUS);
		fifo_level = HSI2C_TX_FIFO_LVL(fifo_status);

		len = HSI2C_FIFO_MAX - fifo_level;
		if (len > (i2c->msg->len - i2c->msg_ptr))
			len = i2c->msg->len - i2c->msg_ptr;

		while (len > 0) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
			writel(byte, i2c->regs + HSI2C_TX_DATA);
			len--;
		}
		i2c->state = 0;
	}

 stop:
	if ((i2c->trans_done && (i2c->msg->len == i2c->msg_ptr)) ||
	    (i2c->state < 0)) {
		writel(0, i2c->regs + HSI2C_INT_ENABLE);
		exynos5_i2c_clr_pend_irq(i2c);
		complete(&i2c->msg_complete);
	}

	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}

/*
 * exynos5_i2c_wait_bus_idle
 *
 * Wait for the bus to go idle, indicated by the MASTER_BUSY bit being
 * cleared.
 *
 * Returns -EBUSY if the bus cannot be bought to idle
 */
static int exynos5_i2c_wait_bus_idle(struct exynos5_i2c *i2c)
{
	unsigned long stop_time;
	u32 trans_status;

	/* wait for 100 milli seconds for the bus to be idle */
	stop_time = jiffies + msecs_to_jiffies(100) + 1;
	do {
		trans_status = readl(i2c->regs + HSI2C_TRANS_STATUS);
		if (!(trans_status & HSI2C_MASTER_BUSY))
			return 0;

		usleep_range(50, 200);
	} while (time_before(jiffies, stop_time));

	return -EBUSY;
}

/*
 * exynos5_i2c_message_start: Configures the bus and starts the xfer
 * i2c: struct exynos5_i2c pointer for the current bus
 * stop: Enables stop after transfer if set. Set for last transfer of
 *       in the list of messages.
 *
 * Configures the bus for read/write function
 * Sets chip address to talk to, message length to be sent.
 * Enables appropriate interrupts and sends start xfer command.
 */
static void exynos5_i2c_message_start(struct exynos5_i2c *i2c, int stop)
{
	u32 i2c_ctl;
	u32 int_en = HSI2C_INT_I2C_EN;
	u32 i2c_auto_conf = 0;
	u32 fifo_ctl;
	unsigned long flags;

	i2c_ctl = readl(i2c->regs + HSI2C_CTL);
	i2c_ctl &= ~(HSI2C_TXCHON | HSI2C_RXCHON);
	fifo_ctl = HSI2C_RXFIFO_EN | HSI2C_TXFIFO_EN;

	if (i2c->msg->flags & I2C_M_RD) {
		i2c_ctl |= HSI2C_RXCHON;

		i2c_auto_conf = HSI2C_READ_WRITE;

		fifo_ctl |= HSI2C_RXFIFO_TRIGGER_LEVEL(HSI2C_DEF_TXFIFO_LVL);
		int_en |= (HSI2C_INT_RX_ALMOSTFULL_EN |
			HSI2C_INT_TRAILING_EN);
	} else {
		i2c_ctl |= HSI2C_TXCHON;

		fifo_ctl |= HSI2C_TXFIFO_TRIGGER_LEVEL(HSI2C_DEF_RXFIFO_LVL);
		int_en |= HSI2C_INT_TX_ALMOSTEMPTY_EN;
	}

	writel(HSI2C_SLV_ADDR_MAS(i2c->msg->addr), i2c->regs + HSI2C_ADDR);

	writel(fifo_ctl, i2c->regs + HSI2C_FIFO_CTL);
	writel(i2c_ctl, i2c->regs + HSI2C_CTL);


	/*
	 * Enable interrupts before starting the transfer so that we don't
	 * miss any INT_I2C interrupts.
	 */
	spin_lock_irqsave(&i2c->lock, flags);
	writel(int_en, i2c->regs + HSI2C_INT_ENABLE);

	if (stop == 1)
		i2c_auto_conf |= HSI2C_STOP_AFTER_TRANS;
	i2c_auto_conf |= i2c->msg->len;
	i2c_auto_conf |= HSI2C_MASTER_RUN;
	writel(i2c_auto_conf, i2c->regs + HSI2C_AUTO_CONF);
	spin_unlock_irqrestore(&i2c->lock, flags);
}

static int exynos5_i2c_xfer_msg(struct exynos5_i2c *i2c,
			      struct i2c_msg *msgs, int stop)
{
	unsigned long timeout;
	int ret;

	i2c->msg = msgs;
	i2c->msg_ptr = 0;
	i2c->trans_done = 0;

	reinit_completion(&i2c->msg_complete);

	exynos5_i2c_message_start(i2c, stop);

	timeout = wait_for_completion_timeout(&i2c->msg_complete,
					      EXYNOS5_I2C_TIMEOUT);
	if (timeout == 0)
		ret = -ETIMEDOUT;
	else
		ret = i2c->state;

	/*
	 * If this is the last message to be transfered (stop == 1)
	 * Then check if the bus can be brought back to idle.
	 */
	if (ret == 0 && stop)
		ret = exynos5_i2c_wait_bus_idle(i2c);

	if (ret < 0) {
		exynos5_i2c_reset(i2c);
		if (ret == -ETIMEDOUT)
			dev_warn(i2c->dev, "%s timeout\n",
				 (msgs->flags & I2C_M_RD) ? "rx" : "tx");
	}

	/* Return the state as in interrupt routine */
	return ret;
}

static int exynos5_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct exynos5_i2c *i2c = (struct exynos5_i2c *)adap->algo_data;
	int i = 0, ret = 0, stop = 0;

	if (i2c->suspended) {
		dev_err(i2c->dev, "HS-I2C is not initialzed.\n");
		return -EIO;
	}

	clk_prepare_enable(i2c->clk);

	for (i = 0; i < num; i++, msgs++) {
		stop = (i == num - 1);

		ret = exynos5_i2c_xfer_msg(i2c, msgs, stop);

		if (ret < 0)
			goto out;
	}

	if (i == num) {
		ret = num;
	} else {
		/* Only one message, cannot access the device */
		if (i == 1)
			ret = -EREMOTEIO;
		else
			ret = i;

		dev_warn(i2c->dev, "xfer message failed\n");
	}

 out:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static u32 exynos5_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm exynos5_i2c_algorithm = {
	.master_xfer		= exynos5_i2c_xfer,
	.functionality		= exynos5_i2c_func,
};

static int exynos5_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct exynos5_i2c *i2c;
	struct resource *mem;
	unsigned int op_clock;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct exynos5_i2c), GFP_KERNEL);
	if (!i2c) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(np, "clock-frequency", &op_clock)) {
		i2c->speed_mode = HSI2C_FAST_SPD;
		i2c->fs_clock = HSI2C_FS_TX_CLOCK;
	} else {
		if (op_clock >= HSI2C_HS_TX_CLOCK) {
			i2c->speed_mode = HSI2C_HIGH_SPD;
			i2c->fs_clock = HSI2C_FS_TX_CLOCK;
			i2c->hs_clock = op_clock;
		} else {
			i2c->speed_mode = HSI2C_FAST_SPD;
			i2c->fs_clock = op_clock;
		}
	}

	strlcpy(i2c->adap.name, "exynos5-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.algo    = &exynos5_i2c_algorithm;
	i2c->adap.retries = 3;

	i2c->dev = &pdev->dev;
	i2c->clk = devm_clk_get(&pdev->dev, "hsi2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return -ENOENT;
	}

	clk_prepare_enable(i2c->clk);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(i2c->regs)) {
		ret = PTR_ERR(i2c->regs);
		goto err_clk;
	}

	i2c->adap.dev.of_node = np;
	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	/* Clear pending interrupts from u-boot or misc causes */
	exynos5_i2c_clr_pend_irq(i2c);

	spin_lock_init(&i2c->lock);
	init_completion(&i2c->msg_complete);

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		dev_err(&pdev->dev, "cannot find HS-I2C IRQ\n");
		ret = -EINVAL;
		goto err_clk;
	}

	ret = devm_request_irq(&pdev->dev, i2c->irq, exynos5_i2c_irq,
				IRQF_NO_SUSPEND | IRQF_ONESHOT,
				dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot request HS-I2C IRQ %d\n", i2c->irq);
		goto err_clk;
	}

	ret = exynos5_hsi2c_clock_setup(i2c);
	if (ret)
		goto err_clk;

	exynos5_i2c_init(i2c);

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, i2c);

 err_clk:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int exynos5_i2c_remove(struct platform_device *pdev)
{
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);

	return 0;
}

static int exynos5_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 1;

	return 0;
}

static int exynos5_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);
	int ret = 0;

	clk_prepare_enable(i2c->clk);

	ret = exynos5_hsi2c_clock_setup(i2c);
	if (ret) {
		clk_disable_unprepare(i2c->clk);
		return ret;
	}

	exynos5_i2c_init(i2c);
	clk_disable_unprepare(i2c->clk);
	i2c->suspended = 0;

	return 0;
}

static SIMPLE_DEV_PM_OPS(exynos5_i2c_dev_pm_ops, exynos5_i2c_suspend_noirq,
			 exynos5_i2c_resume_noirq);

static struct platform_driver exynos5_i2c_driver = {
	.probe		= exynos5_i2c_probe,
	.remove		= exynos5_i2c_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "exynos5-hsi2c",
		.pm	= &exynos5_i2c_dev_pm_ops,
		.of_match_table = exynos5_i2c_match,
	},
};

module_platform_driver(exynos5_i2c_driver);

MODULE_DESCRIPTION("Exynos5 HS-I2C Bus driver");
MODULE_AUTHOR("Naveen Krishna Chatradhi, <ch.naveen@samsung.com>");
MODULE_AUTHOR("Taekgyun Ko, <taeggyun.ko@samsung.com>");
MODULE_LICENSE("GPL v2");
