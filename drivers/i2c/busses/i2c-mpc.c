/*
 * (C) Copyright 2003-2004
 * Humboldt Solutions Ltd, adrian@humboldt.co.uk.

 * This is a combined i2c adapter and algorithm driver for the
 * MPC107/Tsi107 PowerPC northbridge and processors that include
 * the same I2C unit (8240, 8245, 85xx).
 *
 * Release 0.8
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/fsl_devices.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#define MPC_I2C_ADDR  0x00
#define MPC_I2C_FDR 	0x04
#define MPC_I2C_CR	0x08
#define MPC_I2C_SR	0x0c
#define MPC_I2C_DR	0x10
#define MPC_I2C_DFSRR 0x14
#define MPC_I2C_REGION 0x20

#define CCR_MEN  0x80
#define CCR_MIEN 0x40
#define CCR_MSTA 0x20
#define CCR_MTX  0x10
#define CCR_TXAK 0x08
#define CCR_RSTA 0x04

#define CSR_MCF  0x80
#define CSR_MAAS 0x40
#define CSR_MBB  0x20
#define CSR_MAL  0x10
#define CSR_SRW  0x04
#define CSR_MIF  0x02
#define CSR_RXAK 0x01

struct mpc_i2c {
	void __iomem *base;
	u32 interrupt;
	wait_queue_head_t queue;
	struct i2c_adapter adap;
	int irq;
	u32 flags;
};

static __inline__ void writeccr(struct mpc_i2c *i2c, u32 x)
{
	writeb(x, i2c->base + MPC_I2C_CR);
}

static irqreturn_t mpc_i2c_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mpc_i2c *i2c = dev_id;
	if (readb(i2c->base + MPC_I2C_SR) & CSR_MIF) {
		/* Read again to allow register to stabilise */
		i2c->interrupt = readb(i2c->base + MPC_I2C_SR);
		writeb(0, i2c->base + MPC_I2C_SR);
		wake_up_interruptible(&i2c->queue);
	}
	return IRQ_HANDLED;
}

static int i2c_wait(struct mpc_i2c *i2c, unsigned timeout, int writing)
{
	unsigned long orig_jiffies = jiffies;
	u32 x;
	int result = 0;

	if (i2c->irq == 0)
	{
		while (!(readb(i2c->base + MPC_I2C_SR) & CSR_MIF)) {
			schedule();
			if (time_after(jiffies, orig_jiffies + timeout)) {
				pr_debug("I2C: timeout\n");
				result = -EIO;
				break;
			}
		}
		x = readb(i2c->base + MPC_I2C_SR);
		writeb(0, i2c->base + MPC_I2C_SR);
	} else {
		/* Interrupt mode */
		result = wait_event_interruptible_timeout(i2c->queue,
			(i2c->interrupt & CSR_MIF), timeout * HZ);

		if (unlikely(result < 0))
			pr_debug("I2C: wait interrupted\n");
		else if (unlikely(!(i2c->interrupt & CSR_MIF))) {
			pr_debug("I2C: wait timeout\n");
			result = -ETIMEDOUT;
		}

		x = i2c->interrupt;
		i2c->interrupt = 0;
	}

	if (result < 0)
		return result;

	if (!(x & CSR_MCF)) {
		pr_debug("I2C: unfinished\n");
		return -EIO;
	}

	if (x & CSR_MAL) {
		pr_debug("I2C: MAL\n");
		return -EIO;
	}

	if (writing && (x & CSR_RXAK)) {
		pr_debug("I2C: No RXAK\n");
		/* generate stop */
		writeccr(i2c, CCR_MEN);
		return -EIO;
	}
	return 0;
}

static void mpc_i2c_setclock(struct mpc_i2c *i2c)
{
	/* Set clock and filters */
	if (i2c->flags & FSL_I2C_DEV_SEPARATE_DFSRR) {
		writeb(0x31, i2c->base + MPC_I2C_FDR);
		writeb(0x10, i2c->base + MPC_I2C_DFSRR);
	} else if (i2c->flags & FSL_I2C_DEV_CLOCK_5200)
		writeb(0x3f, i2c->base + MPC_I2C_FDR);
	else
		writel(0x1031, i2c->base + MPC_I2C_FDR);
}

static void mpc_i2c_start(struct mpc_i2c *i2c)
{
	/* Clear arbitration */
	writeb(0, i2c->base + MPC_I2C_SR);
	/* Start with MEN */
	writeccr(i2c, CCR_MEN);
}

static void mpc_i2c_stop(struct mpc_i2c *i2c)
{
	writeccr(i2c, CCR_MEN);
}

static int mpc_write(struct mpc_i2c *i2c, int target,
		     const u8 * data, int length, int restart)
{
	int i;
	unsigned timeout = i2c->adap.timeout;
	u32 flags = restart ? CCR_RSTA : 0;

	/* Start with MEN */
	if (!restart)
		writeccr(i2c, CCR_MEN);
	/* Start as master */
	writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_MSTA | CCR_MTX | flags);
	/* Write target byte */
	writeb((target << 1), i2c->base + MPC_I2C_DR);

	if (i2c_wait(i2c, timeout, 1) < 0)
		return -1;

	for (i = 0; i < length; i++) {
		/* Write data byte */
		writeb(data[i], i2c->base + MPC_I2C_DR);

		if (i2c_wait(i2c, timeout, 1) < 0)
			return -1;
	}

	return 0;
}

static int mpc_read(struct mpc_i2c *i2c, int target,
		    u8 * data, int length, int restart)
{
	unsigned timeout = i2c->adap.timeout;
	int i;
	u32 flags = restart ? CCR_RSTA : 0;

	/* Start with MEN */
	if (!restart)
		writeccr(i2c, CCR_MEN);
	/* Switch to read - restart */
	writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_MSTA | CCR_MTX | flags);
	/* Write target address byte - this time with the read flag set */
	writeb((target << 1) | 1, i2c->base + MPC_I2C_DR);

	if (i2c_wait(i2c, timeout, 1) < 0)
		return -1;

	if (length) {
		if (length == 1)
			writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_MSTA | CCR_TXAK);
		else
			writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_MSTA);
		/* Dummy read */
		readb(i2c->base + MPC_I2C_DR);
	}

	for (i = 0; i < length; i++) {
		if (i2c_wait(i2c, timeout, 0) < 0)
			return -1;

		/* Generate txack on next to last byte */
		if (i == length - 2)
			writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_MSTA | CCR_TXAK);
		/* Generate stop on last byte */
		if (i == length - 1)
			writeccr(i2c, CCR_MIEN | CCR_MEN | CCR_TXAK);
		data[i] = readb(i2c->base + MPC_I2C_DR);
	}

	return length;
}

static int mpc_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_msg *pmsg;
	int i;
	int ret = 0;
	unsigned long orig_jiffies = jiffies;
	struct mpc_i2c *i2c = i2c_get_adapdata(adap);

	mpc_i2c_start(i2c);

	/* Allow bus up to 1s to become not busy */
	while (readb(i2c->base + MPC_I2C_SR) & CSR_MBB) {
		if (signal_pending(current)) {
			pr_debug("I2C: Interrupted\n");
			return -EINTR;
		}
		if (time_after(jiffies, orig_jiffies + HZ)) {
			pr_debug("I2C: timeout\n");
			return -EIO;
		}
		schedule();
	}

	for (i = 0; ret >= 0 && i < num; i++) {
		pmsg = &msgs[i];
		pr_debug("Doing %s %d bytes to 0x%02x - %d of %d messages\n",
			 pmsg->flags & I2C_M_RD ? "read" : "write",
			 pmsg->len, pmsg->addr, i + 1, num);
		if (pmsg->flags & I2C_M_RD)
			ret =
			    mpc_read(i2c, pmsg->addr, pmsg->buf, pmsg->len, i);
		else
			ret =
			    mpc_write(i2c, pmsg->addr, pmsg->buf, pmsg->len, i);
	}
	mpc_i2c_stop(i2c);
	return (ret < 0) ? ret : num;
}

static u32 mpc_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm mpc_algo = {
	.name = "MPC algorithm",
	.id = I2C_ALGO_MPC107,
	.master_xfer = mpc_xfer,
	.functionality = mpc_functionality,
};

static struct i2c_adapter mpc_ops = {
	.owner = THIS_MODULE,
	.name = "MPC adapter",
	.id = I2C_ALGO_MPC107 | I2C_HW_MPC107,
	.algo = &mpc_algo,
	.class = I2C_CLASS_HWMON,
	.timeout = 1,
	.retries = 1
};

MODULE_AUTHOR("Adrian Cox <adrian@humboldt.co.uk>");
MODULE_DESCRIPTION
    ("I2C-Bus adapter for MPC107 bridge and MPC824x/85xx/52xx processors");
MODULE_LICENSE("GPL");
