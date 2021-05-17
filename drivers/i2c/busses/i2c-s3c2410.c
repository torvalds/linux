// SPDX-License-Identifier: GPL-2.0-or-later
/* linux/drivers/i2c/busses/i2c-s3c2410.c
 *
 * Copyright (C) 2004,2005,2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 I2C Controller
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
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <asm/irq.h>

#include <linux/platform_data/i2c-s3c2410.h>

/* see s3c2410x user guide, v1.1, section 9 (p447) for more info */

#define S3C2410_IICCON			0x00
#define S3C2410_IICSTAT			0x04
#define S3C2410_IICADD			0x08
#define S3C2410_IICDS			0x0C
#define S3C2440_IICLC			0x10

#define S3C2410_IICCON_ACKEN		(1 << 7)
#define S3C2410_IICCON_TXDIV_16		(0 << 6)
#define S3C2410_IICCON_TXDIV_512	(1 << 6)
#define S3C2410_IICCON_IRQEN		(1 << 5)
#define S3C2410_IICCON_IRQPEND		(1 << 4)
#define S3C2410_IICCON_SCALE(x)		((x) & 0xf)
#define S3C2410_IICCON_SCALEMASK	(0xf)

#define S3C2410_IICSTAT_MASTER_RX	(2 << 6)
#define S3C2410_IICSTAT_MASTER_TX	(3 << 6)
#define S3C2410_IICSTAT_SLAVE_RX	(0 << 6)
#define S3C2410_IICSTAT_SLAVE_TX	(1 << 6)
#define S3C2410_IICSTAT_MODEMASK	(3 << 6)

#define S3C2410_IICSTAT_START		(1 << 5)
#define S3C2410_IICSTAT_BUSBUSY		(1 << 5)
#define S3C2410_IICSTAT_TXRXEN		(1 << 4)
#define S3C2410_IICSTAT_ARBITR		(1 << 3)
#define S3C2410_IICSTAT_ASSLAVE		(1 << 2)
#define S3C2410_IICSTAT_ADDR0		(1 << 1)
#define S3C2410_IICSTAT_LASTBIT		(1 << 0)

#define S3C2410_IICLC_SDA_DELAY0	(0 << 0)
#define S3C2410_IICLC_SDA_DELAY5	(1 << 0)
#define S3C2410_IICLC_SDA_DELAY10	(2 << 0)
#define S3C2410_IICLC_SDA_DELAY15	(3 << 0)
#define S3C2410_IICLC_SDA_DELAY_MASK	(3 << 0)

#define S3C2410_IICLC_FILTER_ON		(1 << 2)

/* Treat S3C2410 as baseline hardware, anything else is supported via quirks */
#define QUIRK_S3C2440		(1 << 0)
#define QUIRK_HDMIPHY		(1 << 1)
#define QUIRK_NO_GPIO		(1 << 2)
#define QUIRK_POLL		(1 << 3)

/* Max time to wait for bus to become idle after a xfer (in us) */
#define S3C2410_IDLE_TIMEOUT	5000

/* Exynos5 Sysreg offset */
#define EXYNOS5_SYS_I2C_CFG	0x0234

/* i2c controller state */
enum s3c24xx_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct s3c24xx_i2c {
	wait_queue_head_t	wait;
	kernel_ulong_t		quirks;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	unsigned int		tx_setup;
	unsigned int		irq;

	enum s3c24xx_i2c_state	state;
	unsigned long		clkrate;

	void __iomem		*regs;
	struct clk		*clk;
	struct device		*dev;
	struct i2c_adapter	adap;

	struct s3c2410_platform_i2c	*pdata;
	struct gpio_desc	*gpios[2];
	struct pinctrl          *pctrl;
#if defined(CONFIG_ARM_S3C24XX_CPUFREQ)
	struct notifier_block	freq_transition;
#endif
	struct regmap		*sysreg;
	unsigned int		sys_i2c_cfg;
};

static const struct platform_device_id s3c24xx_driver_ids[] = {
	{
		.name		= "s3c2410-i2c",
		.driver_data	= 0,
	}, {
		.name		= "s3c2440-i2c",
		.driver_data	= QUIRK_S3C2440,
	}, {
		.name		= "s3c2440-hdmiphy-i2c",
		.driver_data	= QUIRK_S3C2440 | QUIRK_HDMIPHY | QUIRK_NO_GPIO,
	}, { },
};
MODULE_DEVICE_TABLE(platform, s3c24xx_driver_ids);

static int i2c_s3c_irq_nextbyte(struct s3c24xx_i2c *i2c, unsigned long iicstat);

#ifdef CONFIG_OF
static const struct of_device_id s3c24xx_i2c_match[] = {
	{ .compatible = "samsung,s3c2410-i2c", .data = (void *)0 },
	{ .compatible = "samsung,s3c2440-i2c", .data = (void *)QUIRK_S3C2440 },
	{ .compatible = "samsung,s3c2440-hdmiphy-i2c",
	  .data = (void *)(QUIRK_S3C2440 | QUIRK_HDMIPHY | QUIRK_NO_GPIO) },
	{ .compatible = "samsung,exynos5-sata-phy-i2c",
	  .data = (void *)(QUIRK_S3C2440 | QUIRK_POLL | QUIRK_NO_GPIO) },
	{},
};
MODULE_DEVICE_TABLE(of, s3c24xx_i2c_match);
#endif

/*
 * Get controller type either from device tree or platform device variant.
 */
static inline kernel_ulong_t s3c24xx_get_device_quirks(struct platform_device *pdev)
{
	if (pdev->dev.of_node)
		return (kernel_ulong_t)of_device_get_match_data(&pdev->dev);

	return platform_get_device_id(pdev)->driver_data;
}

/*
 * Complete the message and wake up the caller, using the given return code,
 * or zero to mean ok.
 */
static inline void s3c24xx_i2c_master_complete(struct s3c24xx_i2c *i2c, int ret)
{
	dev_dbg(i2c->dev, "master_complete %d\n", ret);

	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;

	if (!(i2c->quirks & QUIRK_POLL))
		wake_up(&i2c->wait);
}

static inline void s3c24xx_i2c_disable_ack(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp & ~S3C2410_IICCON_ACKEN, i2c->regs + S3C2410_IICCON);
}

static inline void s3c24xx_i2c_enable_ack(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp | S3C2410_IICCON_ACKEN, i2c->regs + S3C2410_IICCON);
}

/* irq enable/disable functions */
static inline void s3c24xx_i2c_disable_irq(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp & ~S3C2410_IICCON_IRQEN, i2c->regs + S3C2410_IICCON);
}

static inline void s3c24xx_i2c_enable_irq(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp | S3C2410_IICCON_IRQEN, i2c->regs + S3C2410_IICCON);
}

static bool is_ack(struct s3c24xx_i2c *i2c)
{
	int tries;

	for (tries = 50; tries; --tries) {
		if (readl(i2c->regs + S3C2410_IICCON)
			& S3C2410_IICCON_IRQPEND) {
			if (!(readl(i2c->regs + S3C2410_IICSTAT)
				& S3C2410_IICSTAT_LASTBIT))
				return true;
		}
		usleep_range(1000, 2000);
	}
	dev_err(i2c->dev, "ack was not received\n");
	return false;
}

/*
 * put the start of a message onto the bus
 */
static void s3c24xx_i2c_message_start(struct s3c24xx_i2c *i2c,
				      struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;
	unsigned long stat;
	unsigned long iiccon;

	stat = 0;
	stat |=  S3C2410_IICSTAT_TXRXEN;

	if (msg->flags & I2C_M_RD) {
		stat |= S3C2410_IICSTAT_MASTER_RX;
		addr |= 1;
	} else
		stat |= S3C2410_IICSTAT_MASTER_TX;

	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	/* todo - check for whether ack wanted or not */
	s3c24xx_i2c_enable_ack(i2c);

	iiccon = readl(i2c->regs + S3C2410_IICCON);
	writel(stat, i2c->regs + S3C2410_IICSTAT);

	dev_dbg(i2c->dev, "START: %08lx to IICSTAT, %02x to DS\n", stat, addr);
	writeb(addr, i2c->regs + S3C2410_IICDS);

	/*
	 * delay here to ensure the data byte has gotten onto the bus
	 * before the transaction is started
	 */
	ndelay(i2c->tx_setup);

	dev_dbg(i2c->dev, "iiccon, %08lx\n", iiccon);
	writel(iiccon, i2c->regs + S3C2410_IICCON);

	stat |= S3C2410_IICSTAT_START;
	writel(stat, i2c->regs + S3C2410_IICSTAT);

	if (i2c->quirks & QUIRK_POLL) {
		while ((i2c->msg_num != 0) && is_ack(i2c)) {
			i2c_s3c_irq_nextbyte(i2c, stat);
			stat = readl(i2c->regs + S3C2410_IICSTAT);

			if (stat & S3C2410_IICSTAT_ARBITR)
				dev_err(i2c->dev, "deal with arbitration loss\n");
		}
	}
}

static inline void s3c24xx_i2c_stop(struct s3c24xx_i2c *i2c, int ret)
{
	unsigned long iicstat = readl(i2c->regs + S3C2410_IICSTAT);

	dev_dbg(i2c->dev, "STOP\n");

	/*
	 * The datasheet says that the STOP sequence should be:
	 *  1) I2CSTAT.5 = 0	- Clear BUSY (or 'generate STOP')
	 *  2) I2CCON.4 = 0	- Clear IRQPEND
	 *  3) Wait until the stop condition takes effect.
	 *  4*) I2CSTAT.4 = 0	- Clear TXRXEN
	 *
	 * Where, step "4*" is only for buses with the "HDMIPHY" quirk.
	 *
	 * However, after much experimentation, it appears that:
	 * a) normal buses automatically clear BUSY and transition from
	 *    Master->Slave when they complete generating a STOP condition.
	 *    Therefore, step (3) can be done in doxfer() by polling I2CCON.4
	 *    after starting the STOP generation here.
	 * b) HDMIPHY bus does neither, so there is no way to do step 3.
	 *    There is no indication when this bus has finished generating
	 *    STOP.
	 *
	 * In fact, we have found that as soon as the IRQPEND bit is cleared in
	 * step 2, the HDMIPHY bus generates the STOP condition, and then
	 * immediately starts transferring another data byte, even though the
	 * bus is supposedly stopped.  This is presumably because the bus is
	 * still in "Master" mode, and its BUSY bit is still set.
	 *
	 * To avoid these extra post-STOP transactions on HDMI phy devices, we
	 * just disable Serial Output on the bus (I2CSTAT.4 = 0) directly,
	 * instead of first generating a proper STOP condition.  This should
	 * float SDA & SCK terminating the transfer.  Subsequent transfers
	 *  start with a proper START condition, and proceed normally.
	 *
	 * The HDMIPHY bus is an internal bus that always has exactly two
	 * devices, the host as Master and the HDMIPHY device as the slave.
	 * Skipping the STOP condition has been tested on this bus and works.
	 */
	if (i2c->quirks & QUIRK_HDMIPHY) {
		/* Stop driving the I2C pins */
		iicstat &= ~S3C2410_IICSTAT_TXRXEN;
	} else {
		/* stop the transfer */
		iicstat &= ~S3C2410_IICSTAT_START;
	}
	writel(iicstat, i2c->regs + S3C2410_IICSTAT);

	i2c->state = STATE_STOP;

	s3c24xx_i2c_master_complete(i2c, ret);
	s3c24xx_i2c_disable_irq(i2c);
}

/*
 * helper functions to determine the current state in the set of
 * messages we are sending
 */

/*
 * returns TRUE if the current message is the last in the set
 */
static inline int is_lastmsg(struct s3c24xx_i2c *i2c)
{
	return i2c->msg_idx >= (i2c->msg_num - 1);
}

/*
 * returns TRUE if we this is the last byte in the current message
 */
static inline int is_msglast(struct s3c24xx_i2c *i2c)
{
	/*
	 * msg->len is always 1 for the first byte of smbus block read.
	 * Actual length will be read from slave. More bytes will be
	 * read according to the length then.
	 */
	if (i2c->msg->flags & I2C_M_RECV_LEN && i2c->msg->len == 1)
		return 0;

	return i2c->msg_ptr == i2c->msg->len-1;
}

/*
 * returns TRUE if we reached the end of the current message
 */
static inline int is_msgend(struct s3c24xx_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

/*
 * process an interrupt and work out what to do
 */
static int i2c_s3c_irq_nextbyte(struct s3c24xx_i2c *i2c, unsigned long iicstat)
{
	unsigned long tmp;
	unsigned char byte;
	int ret = 0;

	switch (i2c->state) {

	case STATE_IDLE:
		dev_err(i2c->dev, "%s: called in STATE_IDLE\n", __func__);
		goto out;

	case STATE_STOP:
		dev_err(i2c->dev, "%s: called in STATE_STOP\n", __func__);
		s3c24xx_i2c_disable_irq(i2c);
		goto out_ack;

	case STATE_START:
		/*
		 * last thing we did was send a start condition on the
		 * bus, or started a new i2c message
		 */
		if (iicstat & S3C2410_IICSTAT_LASTBIT &&
		    !(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			/* ack was not received... */
			dev_dbg(i2c->dev, "ack was not received\n");
			s3c24xx_i2c_stop(i2c, -ENXIO);
			goto out_ack;
		}

		if (i2c->msg->flags & I2C_M_RD)
			i2c->state = STATE_READ;
		else
			i2c->state = STATE_WRITE;

		/*
		 * Terminate the transfer if there is nothing to do
		 * as this is used by the i2c probe to find devices.
		 */
		if (is_lastmsg(i2c) && i2c->msg->len == 0) {
			s3c24xx_i2c_stop(i2c, 0);
			goto out_ack;
		}

		if (i2c->state == STATE_READ)
			goto prepare_read;

		/*
		 * fall through to the write state, as we will need to
		 * send a byte as well
		 */
		fallthrough;
	case STATE_WRITE:
		/*
		 * we are writing data to the device... check for the
		 * end of the message, and if so, work out what to do
		 */
		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			if (iicstat & S3C2410_IICSTAT_LASTBIT) {
				dev_dbg(i2c->dev, "WRITE: No Ack\n");

				s3c24xx_i2c_stop(i2c, -ECONNREFUSED);
				goto out_ack;
			}
		}

 retry_write:

		if (!is_msgend(i2c)) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
			writeb(byte, i2c->regs + S3C2410_IICDS);

			/*
			 * delay after writing the byte to allow the
			 * data setup time on the bus, as writing the
			 * data to the register causes the first bit
			 * to appear on SDA, and SCL will change as
			 * soon as the interrupt is acknowledged
			 */
			ndelay(i2c->tx_setup);

		} else if (!is_lastmsg(i2c)) {
			/* we need to go to the next i2c message */

			dev_dbg(i2c->dev, "WRITE: Next Message\n");

			i2c->msg_ptr = 0;
			i2c->msg_idx++;
			i2c->msg++;

			/* check to see if we need to do another message */
			if (i2c->msg->flags & I2C_M_NOSTART) {

				if (i2c->msg->flags & I2C_M_RD) {
					/*
					 * cannot do this, the controller
					 * forces us to send a new START
					 * when we change direction
					 */
					s3c24xx_i2c_stop(i2c, -EINVAL);
				}

				goto retry_write;
			} else {
				/* send the new start */
				s3c24xx_i2c_message_start(i2c, i2c->msg);
				i2c->state = STATE_START;
			}

		} else {
			/* send stop */
			s3c24xx_i2c_stop(i2c, 0);
		}
		break;

	case STATE_READ:
		/*
		 * we have a byte of data in the data register, do
		 * something with it, and then work out whether we are
		 * going to do any more read/write
		 */
		byte = readb(i2c->regs + S3C2410_IICDS);
		i2c->msg->buf[i2c->msg_ptr++] = byte;

		/* Add actual length to read for smbus block read */
		if (i2c->msg->flags & I2C_M_RECV_LEN && i2c->msg->len == 1)
			i2c->msg->len += byte;
 prepare_read:
		if (is_msglast(i2c)) {
			/* last byte of buffer */

			if (is_lastmsg(i2c))
				s3c24xx_i2c_disable_ack(i2c);

		} else if (is_msgend(i2c)) {
			/*
			 * ok, we've read the entire buffer, see if there
			 * is anything else we need to do
			 */
			if (is_lastmsg(i2c)) {
				/* last message, send stop and complete */
				dev_dbg(i2c->dev, "READ: Send Stop\n");

				s3c24xx_i2c_stop(i2c, 0);
			} else {
				/* go to the next transfer */
				dev_dbg(i2c->dev, "READ: Next Transfer\n");

				i2c->msg_ptr = 0;
				i2c->msg_idx++;
				i2c->msg++;
			}
		}

		break;
	}

	/* acknowlegde the IRQ and get back on with the work */

 out_ack:
	tmp = readl(i2c->regs + S3C2410_IICCON);
	tmp &= ~S3C2410_IICCON_IRQPEND;
	writel(tmp, i2c->regs + S3C2410_IICCON);
 out:
	return ret;
}

/*
 * top level IRQ servicing routine
 */
static irqreturn_t s3c24xx_i2c_irq(int irqno, void *dev_id)
{
	struct s3c24xx_i2c *i2c = dev_id;
	unsigned long status;
	unsigned long tmp;

	status = readl(i2c->regs + S3C2410_IICSTAT);

	if (status & S3C2410_IICSTAT_ARBITR) {
		/* deal with arbitration loss */
		dev_err(i2c->dev, "deal with arbitration loss\n");
	}

	if (i2c->state == STATE_IDLE) {
		dev_dbg(i2c->dev, "IRQ: error i2c->state == IDLE\n");

		tmp = readl(i2c->regs + S3C2410_IICCON);
		tmp &= ~S3C2410_IICCON_IRQPEND;
		writel(tmp, i2c->regs +  S3C2410_IICCON);
		goto out;
	}

	/*
	 * pretty much this leaves us with the fact that we've
	 * transmitted or received whatever byte we last sent
	 */
	i2c_s3c_irq_nextbyte(i2c, status);

 out:
	return IRQ_HANDLED;
}

/*
 * Disable the bus so that we won't get any interrupts from now on, or try
 * to drive any lines. This is the default state when we don't have
 * anything to send/receive.
 *
 * If there is an event on the bus, or we have a pre-existing event at
 * kernel boot time, we may not notice the event and the I2C controller
 * will lock the bus with the I2C clock line low indefinitely.
 */
static inline void s3c24xx_i2c_disable_bus(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;

	/* Stop driving the I2C pins */
	tmp = readl(i2c->regs + S3C2410_IICSTAT);
	tmp &= ~S3C2410_IICSTAT_TXRXEN;
	writel(tmp, i2c->regs + S3C2410_IICSTAT);

	/* We don't expect any interrupts now, and don't want send acks */
	tmp = readl(i2c->regs + S3C2410_IICCON);
	tmp &= ~(S3C2410_IICCON_IRQEN | S3C2410_IICCON_IRQPEND |
		S3C2410_IICCON_ACKEN);
	writel(tmp, i2c->regs + S3C2410_IICCON);
}


/*
 * get the i2c bus for a master transaction
 */
static int s3c24xx_i2c_set_master(struct s3c24xx_i2c *i2c)
{
	unsigned long iicstat;
	int timeout = 400;

	while (timeout-- > 0) {
		iicstat = readl(i2c->regs + S3C2410_IICSTAT);

		if (!(iicstat & S3C2410_IICSTAT_BUSBUSY))
			return 0;

		msleep(1);
	}

	return -ETIMEDOUT;
}

/*
 * wait for the i2c bus to become idle.
 */
static void s3c24xx_i2c_wait_idle(struct s3c24xx_i2c *i2c)
{
	unsigned long iicstat;
	ktime_t start, now;
	unsigned long delay;
	int spins;

	/* ensure the stop has been through the bus */

	dev_dbg(i2c->dev, "waiting for bus idle\n");

	start = now = ktime_get();

	/*
	 * Most of the time, the bus is already idle within a few usec of the
	 * end of a transaction.  However, really slow i2c devices can stretch
	 * the clock, delaying STOP generation.
	 *
	 * On slower SoCs this typically happens within a very small number of
	 * instructions so busy wait briefly to avoid scheduling overhead.
	 */
	spins = 3;
	iicstat = readl(i2c->regs + S3C2410_IICSTAT);
	while ((iicstat & S3C2410_IICSTAT_START) && --spins) {
		cpu_relax();
		iicstat = readl(i2c->regs + S3C2410_IICSTAT);
	}

	/*
	 * If we do get an appreciable delay as a compromise between idle
	 * detection latency for the normal, fast case, and system load in the
	 * slow device case, use an exponential back off in the polling loop,
	 * up to 1/10th of the total timeout, then continue to poll at a
	 * constant rate up to the timeout.
	 */
	delay = 1;
	while ((iicstat & S3C2410_IICSTAT_START) &&
	       ktime_us_delta(now, start) < S3C2410_IDLE_TIMEOUT) {
		usleep_range(delay, 2 * delay);
		if (delay < S3C2410_IDLE_TIMEOUT / 10)
			delay <<= 1;
		now = ktime_get();
		iicstat = readl(i2c->regs + S3C2410_IICSTAT);
	}

	if (iicstat & S3C2410_IICSTAT_START)
		dev_warn(i2c->dev, "timeout waiting for bus idle\n");
}

/*
 * this starts an i2c transfer
 */
static int s3c24xx_i2c_doxfer(struct s3c24xx_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	int ret;

	ret = s3c24xx_i2c_set_master(i2c);
	if (ret != 0) {
		dev_err(i2c->dev, "cannot get bus (error %d)\n", ret);
		ret = -EAGAIN;
		goto out;
	}

	i2c->msg     = msgs;
	i2c->msg_num = num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;
	i2c->state   = STATE_START;

	s3c24xx_i2c_enable_irq(i2c);
	s3c24xx_i2c_message_start(i2c, msgs);

	if (i2c->quirks & QUIRK_POLL) {
		ret = i2c->msg_idx;

		if (ret != num)
			dev_dbg(i2c->dev, "incomplete xfer (%d)\n", ret);

		goto out;
	}

	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, HZ * 5);

	ret = i2c->msg_idx;

	/*
	 * Having these next two as dev_err() makes life very
	 * noisy when doing an i2cdetect
	 */
	if (timeout == 0)
		dev_dbg(i2c->dev, "timeout\n");
	else if (ret != num)
		dev_dbg(i2c->dev, "incomplete xfer (%d)\n", ret);

	/* For QUIRK_HDMIPHY, bus is already disabled */
	if (i2c->quirks & QUIRK_HDMIPHY)
		goto out;

	s3c24xx_i2c_wait_idle(i2c);

	s3c24xx_i2c_disable_bus(i2c);

 out:
	i2c->state = STATE_IDLE;

	return ret;
}

/*
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
 */
static int s3c24xx_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct s3c24xx_i2c *i2c = (struct s3c24xx_i2c *)adap->algo_data;
	int retry;
	int ret;

	ret = clk_enable(i2c->clk);
	if (ret)
		return ret;

	for (retry = 0; retry < adap->retries; retry++) {

		ret = s3c24xx_i2c_doxfer(i2c, msgs, num);

		if (ret != -EAGAIN) {
			clk_disable(i2c->clk);
			return ret;
		}

		dev_dbg(i2c->dev, "Retrying transmission (%d)\n", retry);

		udelay(100);
	}

	clk_disable(i2c->clk);
	return -EREMOTEIO;
}

/* declare our i2c functionality */
static u32 s3c24xx_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL_ALL | I2C_FUNC_NOSTART |
		I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */
static const struct i2c_algorithm s3c24xx_i2c_algorithm = {
	.master_xfer		= s3c24xx_i2c_xfer,
	.functionality		= s3c24xx_i2c_func,
};

/*
 * return the divisor settings for a given frequency
 */
static int s3c24xx_i2c_calcdivisor(unsigned long clkin, unsigned int wanted,
				   unsigned int *div1, unsigned int *divs)
{
	unsigned int calc_divs = clkin / wanted;
	unsigned int calc_div1;

	if (calc_divs > (16*16))
		calc_div1 = 512;
	else
		calc_div1 = 16;

	calc_divs += calc_div1-1;
	calc_divs /= calc_div1;

	if (calc_divs == 0)
		calc_divs = 1;
	if (calc_divs > 17)
		calc_divs = 17;

	*divs = calc_divs;
	*div1 = calc_div1;

	return clkin / (calc_divs * calc_div1);
}

/*
 * work out a divisor for the user requested frequency setting,
 * either by the requested frequency, or scanning the acceptable
 * range of frequencies until something is found
 */
static int s3c24xx_i2c_clockrate(struct s3c24xx_i2c *i2c, unsigned int *got)
{
	struct s3c2410_platform_i2c *pdata = i2c->pdata;
	unsigned long clkin = clk_get_rate(i2c->clk);
	unsigned int divs, div1;
	unsigned long target_frequency;
	u32 iiccon;
	int freq;

	i2c->clkrate = clkin;
	clkin /= 1000;	/* clkin now in KHz */

	dev_dbg(i2c->dev, "pdata desired frequency %lu\n", pdata->frequency);

	target_frequency = pdata->frequency ?: I2C_MAX_STANDARD_MODE_FREQ;

	target_frequency /= 1000; /* Target frequency now in KHz */

	freq = s3c24xx_i2c_calcdivisor(clkin, target_frequency, &div1, &divs);

	if (freq > target_frequency) {
		dev_err(i2c->dev,
			"Unable to achieve desired frequency %luKHz."	\
			" Lowest achievable %dKHz\n", target_frequency, freq);
		return -EINVAL;
	}

	*got = freq;

	iiccon = readl(i2c->regs + S3C2410_IICCON);
	iiccon &= ~(S3C2410_IICCON_SCALEMASK | S3C2410_IICCON_TXDIV_512);
	iiccon |= (divs-1);

	if (div1 == 512)
		iiccon |= S3C2410_IICCON_TXDIV_512;

	if (i2c->quirks & QUIRK_POLL)
		iiccon |= S3C2410_IICCON_SCALE(2);

	writel(iiccon, i2c->regs + S3C2410_IICCON);

	if (i2c->quirks & QUIRK_S3C2440) {
		unsigned long sda_delay;

		if (pdata->sda_delay) {
			sda_delay = clkin * pdata->sda_delay;
			sda_delay = DIV_ROUND_UP(sda_delay, 1000000);
			sda_delay = DIV_ROUND_UP(sda_delay, 5);
			if (sda_delay > 3)
				sda_delay = 3;
			sda_delay |= S3C2410_IICLC_FILTER_ON;
		} else
			sda_delay = 0;

		dev_dbg(i2c->dev, "IICLC=%08lx\n", sda_delay);
		writel(sda_delay, i2c->regs + S3C2440_IICLC);
	}

	return 0;
}

#if defined(CONFIG_ARM_S3C24XX_CPUFREQ)

#define freq_to_i2c(_n) container_of(_n, struct s3c24xx_i2c, freq_transition)

static int s3c24xx_i2c_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	struct s3c24xx_i2c *i2c = freq_to_i2c(nb);
	unsigned int got;
	int delta_f;
	int ret;

	delta_f = clk_get_rate(i2c->clk) - i2c->clkrate;

	/* if we're post-change and the input clock has slowed down
	 * or at pre-change and the clock is about to speed up, then
	 * adjust our clock rate. <0 is slow, >0 speedup.
	 */

	if ((val == CPUFREQ_POSTCHANGE && delta_f < 0) ||
	    (val == CPUFREQ_PRECHANGE && delta_f > 0)) {
		i2c_lock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);
		ret = s3c24xx_i2c_clockrate(i2c, &got);
		i2c_unlock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);

		if (ret < 0)
			dev_err(i2c->dev, "cannot find frequency (%d)\n", ret);
		else
			dev_info(i2c->dev, "setting freq %d\n", got);
	}

	return 0;
}

static inline int s3c24xx_i2c_register_cpufreq(struct s3c24xx_i2c *i2c)
{
	i2c->freq_transition.notifier_call = s3c24xx_i2c_cpufreq_transition;

	return cpufreq_register_notifier(&i2c->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void s3c24xx_i2c_deregister_cpufreq(struct s3c24xx_i2c *i2c)
{
	cpufreq_unregister_notifier(&i2c->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int s3c24xx_i2c_register_cpufreq(struct s3c24xx_i2c *i2c)
{
	return 0;
}

static inline void s3c24xx_i2c_deregister_cpufreq(struct s3c24xx_i2c *i2c)
{
}
#endif

#ifdef CONFIG_OF
static int s3c24xx_i2c_parse_dt_gpio(struct s3c24xx_i2c *i2c)
{
	int i;

	if (i2c->quirks & QUIRK_NO_GPIO)
		return 0;

	for (i = 0; i < 2; i++) {
		i2c->gpios[i] = devm_gpiod_get_index(i2c->dev, NULL,
						     i, GPIOD_ASIS);
		if (IS_ERR(i2c->gpios[i])) {
			dev_err(i2c->dev, "i2c gpio invalid at index %d\n", i);
			return -EINVAL;
		}
	}
	return 0;
}

#else
static int s3c24xx_i2c_parse_dt_gpio(struct s3c24xx_i2c *i2c)
{
	return 0;
}
#endif

/*
 * initialise the controller, set the IO lines and frequency
 */
static int s3c24xx_i2c_init(struct s3c24xx_i2c *i2c)
{
	struct s3c2410_platform_i2c *pdata;
	unsigned int freq;

	/* get the plafrom data */

	pdata = i2c->pdata;

	/* write slave address */

	writeb(pdata->slave_addr, i2c->regs + S3C2410_IICADD);

	dev_info(i2c->dev, "slave address 0x%02x\n", pdata->slave_addr);

	writel(0, i2c->regs + S3C2410_IICCON);
	writel(0, i2c->regs + S3C2410_IICSTAT);

	/* we need to work out the divisors for the clock... */

	if (s3c24xx_i2c_clockrate(i2c, &freq) != 0) {
		dev_err(i2c->dev, "cannot meet bus frequency required\n");
		return -EINVAL;
	}

	/* todo - check that the i2c lines aren't being dragged anywhere */

	dev_info(i2c->dev, "bus frequency set to %d KHz\n", freq);
	dev_dbg(i2c->dev, "S3C2410_IICCON=0x%02x\n",
		readl(i2c->regs + S3C2410_IICCON));

	return 0;
}

#ifdef CONFIG_OF
/*
 * Parse the device tree node and retreive the platform data.
 */
static void
s3c24xx_i2c_parse_dt(struct device_node *np, struct s3c24xx_i2c *i2c)
{
	struct s3c2410_platform_i2c *pdata = i2c->pdata;
	int id;

	if (!np)
		return;

	pdata->bus_num = -1; /* i2c bus number is dynamically assigned */
	of_property_read_u32(np, "samsung,i2c-sda-delay", &pdata->sda_delay);
	of_property_read_u32(np, "samsung,i2c-slave-addr", &pdata->slave_addr);
	of_property_read_u32(np, "samsung,i2c-max-bus-freq",
				(u32 *)&pdata->frequency);
	/*
	 * Exynos5's legacy i2c controller and new high speed i2c
	 * controller have muxed interrupt sources. By default the
	 * interrupts for 4-channel HS-I2C controller are enabled.
	 * If nodes for first four channels of legacy i2c controller
	 * are available then re-configure the interrupts via the
	 * system register.
	 */
	id = of_alias_get_id(np, "i2c");
	i2c->sysreg = syscon_regmap_lookup_by_phandle(np,
			"samsung,sysreg-phandle");
	if (IS_ERR(i2c->sysreg))
		return;

	regmap_update_bits(i2c->sysreg, EXYNOS5_SYS_I2C_CFG, BIT(id), 0);
}
#else
static void
s3c24xx_i2c_parse_dt(struct device_node *np, struct s3c24xx_i2c *i2c) { }
#endif

static int s3c24xx_i2c_probe(struct platform_device *pdev)
{
	struct s3c24xx_i2c *i2c;
	struct s3c2410_platform_i2c *pdata = NULL;
	struct resource *res;
	int ret;

	if (!pdev->dev.of_node) {
		pdata = dev_get_platdata(&pdev->dev);
		if (!pdata) {
			dev_err(&pdev->dev, "no platform data\n");
			return -EINVAL;
		}
	}

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct s3c24xx_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!i2c->pdata)
		return -ENOMEM;

	i2c->quirks = s3c24xx_get_device_quirks(pdev);
	i2c->sysreg = ERR_PTR(-ENOENT);
	if (pdata)
		memcpy(i2c->pdata, pdata, sizeof(*pdata));
	else
		s3c24xx_i2c_parse_dt(pdev->dev.of_node, i2c);

	strlcpy(i2c->adap.name, "s3c2410-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &s3c24xx_i2c_algorithm;
	i2c->adap.retries = 2;
	i2c->adap.class = I2C_CLASS_DEPRECATED;
	i2c->tx_setup = 50;

	init_waitqueue_head(&i2c->wait);

	/* find the clock and enable it */
	i2c->dev = &pdev->dev;
	i2c->clk = devm_clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return -ENOENT;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->regs = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	dev_dbg(&pdev->dev, "registers %p (%p)\n",
		i2c->regs, res);

	/* setup info block for the i2c core */
	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->pctrl = devm_pinctrl_get_select_default(i2c->dev);

	/* inititalise the i2c gpio lines */
	if (i2c->pdata->cfg_gpio)
		i2c->pdata->cfg_gpio(to_platform_device(i2c->dev));
	else if (IS_ERR(i2c->pctrl) && s3c24xx_i2c_parse_dt_gpio(i2c))
		return -EINVAL;

	/* initialise the i2c controller */
	ret = clk_prepare_enable(i2c->clk);
	if (ret) {
		dev_err(&pdev->dev, "I2C clock enable failed\n");
		return ret;
	}

	ret = s3c24xx_i2c_init(i2c);
	clk_disable(i2c->clk);
	if (ret != 0) {
		dev_err(&pdev->dev, "I2C controller init failed\n");
		clk_unprepare(i2c->clk);
		return ret;
	}

	/*
	 * find the IRQ for this unit (note, this relies on the init call to
	 * ensure no current IRQs pending
	 */
	if (!(i2c->quirks & QUIRK_POLL)) {
		i2c->irq = ret = platform_get_irq(pdev, 0);
		if (ret <= 0) {
			dev_err(&pdev->dev, "cannot find IRQ\n");
			clk_unprepare(i2c->clk);
			return ret;
		}

		ret = devm_request_irq(&pdev->dev, i2c->irq, s3c24xx_i2c_irq,
				       0, dev_name(&pdev->dev), i2c);
		if (ret != 0) {
			dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
			clk_unprepare(i2c->clk);
			return ret;
		}
	}

	ret = s3c24xx_i2c_register_cpufreq(i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register cpufreq notifier\n");
		clk_unprepare(i2c->clk);
		return ret;
	}

	/*
	 * Note, previous versions of the driver used i2c_add_adapter()
	 * to add the bus at any number. We now pass the bus number via
	 * the platform data, so if unset it will now default to always
	 * being bus 0.
	 */
	i2c->adap.nr = i2c->pdata->bus_num;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	platform_set_drvdata(pdev, i2c);

	pm_runtime_enable(&pdev->dev);

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		s3c24xx_i2c_deregister_cpufreq(i2c);
		clk_unprepare(i2c->clk);
		return ret;
	}

	dev_info(&pdev->dev, "%s: S3C I2C adapter\n", dev_name(&i2c->adap.dev));
	return 0;
}

static int s3c24xx_i2c_remove(struct platform_device *pdev)
{
	struct s3c24xx_i2c *i2c = platform_get_drvdata(pdev);

	clk_unprepare(i2c->clk);

	pm_runtime_disable(&pdev->dev);

	s3c24xx_i2c_deregister_cpufreq(i2c);

	i2c_del_adapter(&i2c->adap);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int s3c24xx_i2c_suspend_noirq(struct device *dev)
{
	struct s3c24xx_i2c *i2c = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&i2c->adap);

	if (!IS_ERR(i2c->sysreg))
		regmap_read(i2c->sysreg, EXYNOS5_SYS_I2C_CFG, &i2c->sys_i2c_cfg);

	return 0;
}

static int s3c24xx_i2c_resume_noirq(struct device *dev)
{
	struct s3c24xx_i2c *i2c = dev_get_drvdata(dev);
	int ret;

	if (!IS_ERR(i2c->sysreg))
		regmap_write(i2c->sysreg, EXYNOS5_SYS_I2C_CFG, i2c->sys_i2c_cfg);

	ret = clk_enable(i2c->clk);
	if (ret)
		return ret;
	s3c24xx_i2c_init(i2c);
	clk_disable(i2c->clk);
	i2c_mark_adapter_resumed(&i2c->adap);

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops s3c24xx_i2c_dev_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(s3c24xx_i2c_suspend_noirq,
				      s3c24xx_i2c_resume_noirq)
};

#define S3C24XX_DEV_PM_OPS (&s3c24xx_i2c_dev_pm_ops)
#else
#define S3C24XX_DEV_PM_OPS NULL
#endif

static struct platform_driver s3c24xx_i2c_driver = {
	.probe		= s3c24xx_i2c_probe,
	.remove		= s3c24xx_i2c_remove,
	.id_table	= s3c24xx_driver_ids,
	.driver		= {
		.name	= "s3c-i2c",
		.pm	= S3C24XX_DEV_PM_OPS,
		.of_match_table = of_match_ptr(s3c24xx_i2c_match),
	},
};

static int __init i2c_adap_s3c_init(void)
{
	return platform_driver_register(&s3c24xx_i2c_driver);
}
subsys_initcall(i2c_adap_s3c_init);

static void __exit i2c_adap_s3c_exit(void)
{
	platform_driver_unregister(&s3c24xx_i2c_driver);
}
module_exit(i2c_adap_s3c_exit);

MODULE_DESCRIPTION("S3C24XX I2C Bus driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
