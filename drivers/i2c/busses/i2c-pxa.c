/*
 *  i2c_adap_pxa.c
 *
 *  I2C adapter for the PXA I2C bus access.
 *
 *  Copyright (C) 2002 Intrinsyc Software Inc.
 *  Copyright (C) 2004-2005 Deep Blue Solutions Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  History:
 *    Apr 2002: Initial version [CS]
 *    Jun 2002: Properly seperated algo/adap [FB]
 *    Jan 2003: Fixed several bugs concerning interrupt handling [Kai-Uwe Bloem]
 *    Jan 2003: added limited signal handling [Kai-Uwe Bloem]
 *    Sep 2004: Major rework to ensure efficient bus handling [RMK]
 *    Dec 2004: Added support for PXA27x and slave device probing [Liam Girdwood]
 *    Feb 2005: Rework slave mode handling [RMK]
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/i2c-pxa.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/i2c.h>
#include <asm/arch/pxa-regs.h>

struct pxa_i2c {
	spinlock_t		lock;
	wait_queue_head_t	wait;
	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;
	unsigned int		slave_addr;

	struct i2c_adapter	adap;
#ifdef CONFIG_I2C_PXA_SLAVE
	struct i2c_slave_client *slave;
#endif

	unsigned int		irqlogidx;
	u32			isrlog[32];
	u32			icrlog[32];

	void __iomem		*reg_base;

	unsigned long		iobase;
	unsigned long		iosize;

	int			irq;
};

#define _IBMR(i2c)	((i2c)->reg_base + 0)
#define _IDBR(i2c)	((i2c)->reg_base + 8)
#define _ICR(i2c)	((i2c)->reg_base + 0x10)
#define _ISR(i2c)	((i2c)->reg_base + 0x18)
#define _ISAR(i2c)	((i2c)->reg_base + 0x20)

/*
 * I2C Slave mode address
 */
#define I2C_PXA_SLAVE_ADDR      0x1

#ifdef DEBUG

struct bits {
	u32	mask;
	const char *set;
	const char *unset;
};
#define BIT(m, s, u)	{ .mask = m, .set = s, .unset = u }

static inline void
decode_bits(const char *prefix, const struct bits *bits, int num, u32 val)
{
	printk("%s %08x: ", prefix, val);
	while (num--) {
		const char *str = val & bits->mask ? bits->set : bits->unset;
		if (str)
			printk("%s ", str);
		bits++;
	}
}

static const struct bits isr_bits[] = {
	BIT(ISR_RWM,	"RX",		"TX"),
	BIT(ISR_ACKNAK,	"NAK",		"ACK"),
	BIT(ISR_UB,	"Bsy",		"Rdy"),
	BIT(ISR_IBB,	"BusBsy",	"BusRdy"),
	BIT(ISR_SSD,	"SlaveStop",	NULL),
	BIT(ISR_ALD,	"ALD",		NULL),
	BIT(ISR_ITE,	"TxEmpty",	NULL),
	BIT(ISR_IRF,	"RxFull",	NULL),
	BIT(ISR_GCAD,	"GenCall",	NULL),
	BIT(ISR_SAD,	"SlaveAddr",	NULL),
	BIT(ISR_BED,	"BusErr",	NULL),
};

static void decode_ISR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ISR", isr_bits, ARRAY_SIZE(isr_bits), val);
	printk("\n");
}

static const struct bits icr_bits[] = {
	BIT(ICR_START,  "START",	NULL),
	BIT(ICR_STOP,   "STOP",		NULL),
	BIT(ICR_ACKNAK, "ACKNAK",	NULL),
	BIT(ICR_TB,     "TB",		NULL),
	BIT(ICR_MA,     "MA",		NULL),
	BIT(ICR_SCLE,   "SCLE",		"scle"),
	BIT(ICR_IUE,    "IUE",		"iue"),
	BIT(ICR_GCD,    "GCD",		NULL),
	BIT(ICR_ITEIE,  "ITEIE",	NULL),
	BIT(ICR_IRFIE,  "IRFIE",	NULL),
	BIT(ICR_BEIE,   "BEIE",		NULL),
	BIT(ICR_SSDIE,  "SSDIE",	NULL),
	BIT(ICR_ALDIE,  "ALDIE",	NULL),
	BIT(ICR_SADIE,  "SADIE",	NULL),
	BIT(ICR_UR,     "UR",		"ur"),
};

static void decode_ICR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ICR", icr_bits, ARRAY_SIZE(icr_bits), val);
	printk("\n");
}

static unsigned int i2c_debug = DEBUG;

static void i2c_pxa_show_state(struct pxa_i2c *i2c, int lno, const char *fname)
{
	dev_dbg(&i2c->adap.dev, "state:%s:%d: ISR=%08x, ICR=%08x, IBMR=%02x\n", fname, lno,
		readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));
}

#define show_state(i2c) i2c_pxa_show_state(i2c, __LINE__, __FUNCTION__)
#else
#define i2c_debug	0

#define show_state(i2c) do { } while (0)
#define decode_ISR(val) do { } while (0)
#define decode_ICR(val) do { } while (0)
#endif

#define eedbg(lvl, x...) do { if ((lvl) < 1) { printk(KERN_DEBUG "" x); } } while(0)

static void i2c_pxa_master_complete(struct pxa_i2c *i2c, int ret);

static void i2c_pxa_scream_blue_murder(struct pxa_i2c *i2c, const char *why)
{
	unsigned int i;
	printk("i2c: error: %s\n", why);
	printk("i2c: msg_num: %d msg_idx: %d msg_ptr: %d\n",
		i2c->msg_num, i2c->msg_idx, i2c->msg_ptr);
	printk("i2c: ICR: %08x ISR: %08x\n"
	       "i2c: log: ", readl(_ICR(i2c)), readl(_ISR(i2c)));
	for (i = 0; i < i2c->irqlogidx; i++)
		printk("[%08x:%08x] ", i2c->isrlog[i], i2c->icrlog[i]);
	printk("\n");
}

static inline int i2c_pxa_is_slavemode(struct pxa_i2c *i2c)
{
	return !(readl(_ICR(i2c)) & ICR_SCLE);
}

static void i2c_pxa_abort(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ/4;

	if (i2c_pxa_is_slavemode(i2c)) {
		dev_dbg(&i2c->adap.dev, "%s: called in slave mode\n", __func__);
		return;
	}

	while (time_before(jiffies, timeout) && (readl(_IBMR(i2c)) & 0x1) == 0) {
		unsigned long icr = readl(_ICR(i2c));

		icr &= ~ICR_START;
		icr |= ICR_ACKNAK | ICR_STOP | ICR_TB;

		writel(icr, _ICR(i2c));

		show_state(i2c);

		msleep(1);
	}

	writel(readl(_ICR(i2c)) & ~(ICR_MA | ICR_START | ICR_STOP),
	       _ICR(i2c));
}

static int i2c_pxa_wait_bus_not_busy(struct pxa_i2c *i2c)
{
	int timeout = DEF_TIMEOUT;

	while (timeout-- && readl(_ISR(i2c)) & (ISR_IBB | ISR_UB)) {
		if ((readl(_ISR(i2c)) & ISR_SAD) != 0)
			timeout += 4;

		msleep(2);
		show_state(i2c);
	}

	if (timeout <= 0)
		show_state(i2c);

	return timeout <= 0 ? I2C_RETRY : 0;
}

static int i2c_pxa_wait_master(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ*4;

	while (time_before(jiffies, timeout)) {
		if (i2c_debug > 1)
			dev_dbg(&i2c->adap.dev, "%s: %ld: ISR=%08x, ICR=%08x, IBMR=%02x\n",
				__func__, (long)jiffies, readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));

		if (readl(_ISR(i2c)) & ISR_SAD) {
			if (i2c_debug > 0)
				dev_dbg(&i2c->adap.dev, "%s: Slave detected\n", __func__);
			goto out;
		}

		/* wait for unit and bus being not busy, and we also do a
		 * quick check of the i2c lines themselves to ensure they've
		 * gone high...
		 */
		if ((readl(_ISR(i2c)) & (ISR_UB | ISR_IBB)) == 0 && readl(_IBMR(i2c)) == 3) {
			if (i2c_debug > 0)
				dev_dbg(&i2c->adap.dev, "%s: done\n", __func__);
			return 1;
		}

		msleep(1);
	}

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "%s: did not free\n", __func__);
 out:
	return 0;
}

static int i2c_pxa_set_master(struct pxa_i2c *i2c)
{
	if (i2c_debug)
		dev_dbg(&i2c->adap.dev, "setting to bus master\n");

	if ((readl(_ISR(i2c)) & (ISR_UB | ISR_IBB)) != 0) {
		dev_dbg(&i2c->adap.dev, "%s: unit is busy\n", __func__);
		if (!i2c_pxa_wait_master(i2c)) {
			dev_dbg(&i2c->adap.dev, "%s: error: unit busy\n", __func__);
			return I2C_RETRY;
		}
	}

	writel(readl(_ICR(i2c)) | ICR_SCLE, _ICR(i2c));
	return 0;
}

#ifdef CONFIG_I2C_PXA_SLAVE
static int i2c_pxa_wait_slave(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ*1;

	/* wait for stop */

	show_state(i2c);

	while (time_before(jiffies, timeout)) {
		if (i2c_debug > 1)
			dev_dbg(&i2c->adap.dev, "%s: %ld: ISR=%08x, ICR=%08x, IBMR=%02x\n",
				__func__, (long)jiffies, readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));

		if ((readl(_ISR(i2c)) & (ISR_UB|ISR_IBB)) == 0 ||
		    (readl(_ISR(i2c)) & ISR_SAD) != 0 ||
		    (readl(_ICR(i2c)) & ICR_SCLE) == 0) {
			if (i2c_debug > 1)
				dev_dbg(&i2c->adap.dev, "%s: done\n", __func__);
			return 1;
		}

		msleep(1);
	}

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "%s: did not free\n", __func__);
	return 0;
}

/*
 * clear the hold on the bus, and take of anything else
 * that has been configured
 */
static void i2c_pxa_set_slave(struct pxa_i2c *i2c, int errcode)
{
	show_state(i2c);

	if (errcode < 0) {
		udelay(100);   /* simple delay */
	} else {
		/* we need to wait for the stop condition to end */

		/* if we where in stop, then clear... */
		if (readl(_ICR(i2c)) & ICR_STOP) {
			udelay(100);
			writel(readl(_ICR(i2c)) & ~ICR_STOP, _ICR(i2c));
		}

		if (!i2c_pxa_wait_slave(i2c)) {
			dev_err(&i2c->adap.dev, "%s: wait timedout\n",
				__func__);
			return;
		}
	}

	writel(readl(_ICR(i2c)) & ~(ICR_STOP|ICR_ACKNAK|ICR_MA), _ICR(i2c));
	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));

	if (i2c_debug) {
		dev_dbg(&i2c->adap.dev, "ICR now %08x, ISR %08x\n", readl(_ICR(i2c)), readl(_ISR(i2c)));
		decode_ICR(readl(_ICR(i2c)));
	}
}
#else
#define i2c_pxa_set_slave(i2c, err)	do { } while (0)
#endif

static void i2c_pxa_reset(struct pxa_i2c *i2c)
{
	pr_debug("Resetting I2C Controller Unit\n");

	/* abort any transfer currently under way */
	i2c_pxa_abort(i2c);

	/* reset according to 9.8 */
	writel(ICR_UR, _ICR(i2c));
	writel(I2C_ISR_INIT, _ISR(i2c));
	writel(readl(_ICR(i2c)) & ~ICR_UR, _ICR(i2c));

	writel(i2c->slave_addr, _ISAR(i2c));

	/* set control register values */
	writel(I2C_ICR_INIT, _ICR(i2c));

#ifdef CONFIG_I2C_PXA_SLAVE
	dev_info(&i2c->adap.dev, "Enabling slave mode\n");
	writel(readl(_ICR(i2c)) | ICR_SADIE | ICR_ALDIE | ICR_SSDIE, _ICR(i2c));
#endif

	i2c_pxa_set_slave(i2c, 0);

	/* enable unit */
	writel(readl(_ICR(i2c)) | ICR_IUE, _ICR(i2c));
	udelay(100);
}


#ifdef CONFIG_I2C_PXA_SLAVE
/*
 * PXA I2C Slave mode
 */

static void i2c_pxa_slave_txempty(struct pxa_i2c *i2c, u32 isr)
{
	if (isr & ISR_BED) {
		/* what should we do here? */
	} else {
		int ret = 0;

		if (i2c->slave != NULL)
			ret = i2c->slave->read(i2c->slave->data);

		writel(ret, _IDBR(i2c));
		writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));   /* allow next byte */
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	unsigned int byte = readl(_IDBR(i2c));

	if (i2c->slave != NULL)
		i2c->slave->write(i2c->slave->data, byte);

	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "SAD, mode is slave-%cx\n",
		       (isr & ISR_RWM) ? 'r' : 't');

	if (i2c->slave != NULL)
		i2c->slave->event(i2c->slave->data,
				 (isr & ISR_RWM) ? I2C_SLAVE_EVENT_START_READ : I2C_SLAVE_EVENT_START_WRITE);

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	writel(readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP), _ICR(i2c));
	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));

	timeout = 0x10000;

	while (1) {
		if ((readl(_IBMR(i2c)) & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));
}

static void i2c_pxa_slave_stop(struct pxa_i2c *i2c)
{
	if (i2c_debug > 2)
		dev_dbg(&i2c->adap.dev, "ISR: SSD (Slave Stop)\n");

	if (i2c->slave != NULL)
		i2c->slave->event(i2c->slave->data, I2C_SLAVE_EVENT_STOP);

	if (i2c_debug > 2)
		dev_dbg(&i2c->adap.dev, "ISR: SSD (Slave Stop) acked\n");

	/*
	 * If we have a master-mode message waiting,
	 * kick it off now that the slave has completed.
	 */
	if (i2c->msg)
		i2c_pxa_master_complete(i2c, I2C_RETRY);
}
#else
static void i2c_pxa_slave_txempty(struct pxa_i2c *i2c, u32 isr)
{
	if (isr & ISR_BED) {
		/* what should we do here? */
	} else {
		writel(0, _IDBR(i2c));
		writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	writel(readl(_ICR(i2c)) | ICR_TB | ICR_ACKNAK, _ICR(i2c));
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	writel(readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP), _ICR(i2c));
	writel(readl(_ICR(i2c)) | ICR_TB | ICR_ACKNAK, _ICR(i2c));

	timeout = 0x10000;

	while (1) {
		if ((readl(_IBMR(i2c)) & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));
}

static void i2c_pxa_slave_stop(struct pxa_i2c *i2c)
{
	if (i2c->msg)
		i2c_pxa_master_complete(i2c, I2C_RETRY);
}
#endif

/*
 * PXA I2C Master mode
 */

static inline unsigned int i2c_pxa_addr_byte(struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;

	if (msg->flags & I2C_M_RD)
		addr |= 1;

	return addr;
}

static inline void i2c_pxa_start_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/*
	 * Step 1: target slave address into IDBR
	 */
	writel(i2c_pxa_addr_byte(i2c->msg), _IDBR(i2c));

	/*
	 * Step 2: initiate the write.
	 */
	icr = readl(_ICR(i2c)) & ~(ICR_STOP | ICR_ALDIE);
	writel(icr | ICR_START | ICR_TB, _ICR(i2c));
}

static inline void i2c_pxa_stop_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/*
	 * Clear the STOP and ACK flags
	 */
	icr = readl(_ICR(i2c));
	icr &= ~(ICR_STOP | ICR_ACKNAK);
	writel(icr, _IRC(i2c));
}

/*
 * We are protected by the adapter bus mutex.
 */
static int i2c_pxa_do_xfer(struct pxa_i2c *i2c, struct i2c_msg *msg, int num)
{
	long timeout;
	int ret;

	/*
	 * Wait for the bus to become free.
	 */
	ret = i2c_pxa_wait_bus_not_busy(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa: timeout waiting for bus free\n");
		goto out;
	}

	/*
	 * Set master mode.
	 */
	ret = i2c_pxa_set_master(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa_set_master: error %d\n", ret);
		goto out;
	}

	spin_lock_irq(&i2c->lock);

	i2c->msg = msg;
	i2c->msg_num = num;
	i2c->msg_idx = 0;
	i2c->msg_ptr = 0;
	i2c->irqlogidx = 0;

	i2c_pxa_start_message(i2c);

	spin_unlock_irq(&i2c->lock);

	/*
	 * The rest of the processing occurs in the interrupt handler.
	 */
	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, HZ * 5);
	i2c_pxa_stop_message(i2c);

	/*
	 * We place the return code in i2c->msg_idx.
	 */
	ret = i2c->msg_idx;

	if (timeout == 0)
		i2c_pxa_scream_blue_murder(i2c, "timeout");

 out:
	return ret;
}

/*
 * i2c_pxa_master_complete - complete the message and wake up.
 */
static void i2c_pxa_master_complete(struct pxa_i2c *i2c, int ret)
{
	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx ++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;
	wake_up(&i2c->wait);
}

static void i2c_pxa_irq_txempty(struct pxa_i2c *i2c, u32 isr)
{
	u32 icr = readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

 again:
	/*
	 * If ISR_ALD is set, we lost arbitration.
	 */
	if (isr & ISR_ALD) {
		/*
		 * Do we need to do anything here?  The PXA docs
		 * are vague about what happens.
		 */
		i2c_pxa_scream_blue_murder(i2c, "ALD set");

		/*
		 * We ignore this error.  We seem to see spurious ALDs
		 * for seemingly no reason.  If we handle them as I think
		 * they should, we end up causing an I2C error, which
		 * is painful for some systems.
		 */
		return; /* ignore */
	}

	if (isr & ISR_BED) {
		int ret = BUS_ERROR;

		/*
		 * I2C bus error - either the device NAK'd us, or
		 * something more serious happened.  If we were NAK'd
		 * on the initial address phase, we can retry.
		 */
		if (isr & ISR_ACKNAK) {
			if (i2c->msg_ptr == 0 && i2c->msg_idx == 0)
				ret = I2C_RETRY;
			else
				ret = XFER_NAKED;
		}
		i2c_pxa_master_complete(i2c, ret);
	} else if (isr & ISR_RWM) {
		/*
		 * Read mode.  We have just sent the address byte, and
		 * now we must initiate the transfer.
		 */
		if (i2c->msg_ptr == i2c->msg->len - 1 &&
		    i2c->msg_idx == i2c->msg_num - 1)
			icr |= ICR_STOP | ICR_ACKNAK;

		icr |= ICR_ALDIE | ICR_TB;
	} else if (i2c->msg_ptr < i2c->msg->len) {
		/*
		 * Write mode.  Write the next data byte.
		 */
		writel(i2c->msg->buf[i2c->msg_ptr++], _IDBR(i2c));

		icr |= ICR_ALDIE | ICR_TB;

		/*
		 * If this is the last byte of the last message, send
		 * a STOP.
		 */
		if (i2c->msg_ptr == i2c->msg->len &&
		    i2c->msg_idx == i2c->msg_num - 1)
			icr |= ICR_STOP;
	} else if (i2c->msg_idx < i2c->msg_num - 1) {
		/*
		 * Next segment of the message.
		 */
		i2c->msg_ptr = 0;
		i2c->msg_idx ++;
		i2c->msg++;

		/*
		 * If we aren't doing a repeated start and address,
		 * go back and try to send the next byte.  Note that
		 * we do not support switching the R/W direction here.
		 */
		if (i2c->msg->flags & I2C_M_NOSTART)
			goto again;

		/*
		 * Write the next address.
		 */
		writel(i2c_pxa_addr_byte(i2c->msg), _IDBR(i2c));

		/*
		 * And trigger a repeated start, and send the byte.
		 */
		icr &= ~ICR_ALDIE;
		icr |= ICR_START | ICR_TB;
	} else {
		if (i2c->msg->len == 0) {
			/*
			 * Device probes have a message length of zero
			 * and need the bus to be reset before it can
			 * be used again.
			 */
			i2c_pxa_reset(i2c);
		}
		i2c_pxa_master_complete(i2c, 0);
	}

	i2c->icrlog[i2c->irqlogidx-1] = icr;

	writel(icr, _ICR(i2c));
	show_state(i2c);
}

static void i2c_pxa_irq_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	u32 icr = readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

	/*
	 * Read the byte.
	 */
	i2c->msg->buf[i2c->msg_ptr++] = readl(_IDBR(i2c));

	if (i2c->msg_ptr < i2c->msg->len) {
		/*
		 * If this is the last byte of the last
		 * message, send a STOP.
		 */
		if (i2c->msg_ptr == i2c->msg->len - 1)
			icr |= ICR_STOP | ICR_ACKNAK;

		icr |= ICR_ALDIE | ICR_TB;
	} else {
		i2c_pxa_master_complete(i2c, 0);
	}

	i2c->icrlog[i2c->irqlogidx-1] = icr;

	writel(icr, _ICR(i2c));
}

static irqreturn_t i2c_pxa_handler(int this_irq, void *dev_id)
{
	struct pxa_i2c *i2c = dev_id;
	u32 isr = readl(_ISR(i2c));

	if (i2c_debug > 2 && 0) {
		dev_dbg(&i2c->adap.dev, "%s: ISR=%08x, ICR=%08x, IBMR=%02x\n",
			__func__, isr, readl(_ICR(i2c)), readl(_IBMR(i2c)));
		decode_ISR(isr);
	}

	if (i2c->irqlogidx < ARRAY_SIZE(i2c->isrlog))
		i2c->isrlog[i2c->irqlogidx++] = isr;

	show_state(i2c);

	/*
	 * Always clear all pending IRQs.
	 */
	writel(isr & (ISR_SSD|ISR_ALD|ISR_ITE|ISR_IRF|ISR_SAD|ISR_BED), _ISR(i2c));

	if (isr & ISR_SAD)
		i2c_pxa_slave_start(i2c, isr);
	if (isr & ISR_SSD)
		i2c_pxa_slave_stop(i2c);

	if (i2c_pxa_is_slavemode(i2c)) {
		if (isr & ISR_ITE)
			i2c_pxa_slave_txempty(i2c, isr);
		if (isr & ISR_IRF)
			i2c_pxa_slave_rxfull(i2c, isr);
	} else if (i2c->msg) {
		if (isr & ISR_ITE)
			i2c_pxa_irq_txempty(i2c, isr);
		if (isr & ISR_IRF)
			i2c_pxa_irq_rxfull(i2c, isr);
	} else {
		i2c_pxa_scream_blue_murder(i2c, "spurious irq");
	}

	return IRQ_HANDLED;
}


static int i2c_pxa_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct pxa_i2c *i2c = adap->algo_data;
	int ret, i;

	/* If the I2C controller is disabled we need to reset it (probably due
 	   to a suspend/resume destroying state). We do this here as we can then
 	   avoid worrying about resuming the controller before its users. */
	if (!(readl(_ICR(i2c)) & ICR_IUE))
		i2c_pxa_reset(i2c);

	for (i = adap->retries; i >= 0; i--) {
		ret = i2c_pxa_do_xfer(i2c, msgs, num);
		if (ret != I2C_RETRY)
			goto out;

		if (i2c_debug)
			dev_dbg(&adap->dev, "Retrying transmission\n");
		udelay(100);
	}
	i2c_pxa_scream_blue_murder(i2c, "exhausted retries");
	ret = -EREMOTEIO;
 out:
	i2c_pxa_set_slave(i2c, ret);
	return ret;
}

static u32 i2c_pxa_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm i2c_pxa_algorithm = {
	.master_xfer	= i2c_pxa_xfer,
	.functionality	= i2c_pxa_functionality,
};

static struct pxa_i2c i2c_pxa = {
	.lock	= __SPIN_LOCK_UNLOCKED(i2c_pxa.lock),
	.adap	= {
		.owner		= THIS_MODULE,
		.algo		= &i2c_pxa_algorithm,
		.name		= "pxa2xx-i2c.0",
		.retries	= 5,
	},
};

#define res_len(r)		((r)->end - (r)->start + 1)
static int i2c_pxa_probe(struct platform_device *dev)
{
	struct pxa_i2c *i2c = &i2c_pxa;
	struct resource *res;
#ifdef CONFIG_I2C_PXA_SLAVE
	struct i2c_pxa_platform_data *plat = dev->dev.platform_data;
#endif
	int ret;
	int irq;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(dev, 0);
	if (res == NULL || irq < 0)
		return -ENODEV;

	if (!request_mem_region(res->start, res_len(res), res->name))
		return -ENOMEM;

	i2c = kmalloc(sizeof(struct pxa_i2c), GFP_KERNEL);
	if (!i2c) {
		ret = -ENOMEM;
		goto emalloc;
	}

	memcpy(i2c, &i2c_pxa, sizeof(struct pxa_i2c));
	init_waitqueue_head(&i2c->wait);
	i2c->adap.name[strlen(i2c->adap.name) - 1] = '0' + dev->id % 10;

	i2c->reg_base = ioremap(res->start, res_len(res));
	if (!i2c->reg_base) {
		ret = -EIO;
		goto eremap;
	}

	i2c->iobase = res->start;
	i2c->iosize = res_len(res);

	i2c->irq = irq;

	i2c->slave_addr = I2C_PXA_SLAVE_ADDR;

#ifdef CONFIG_I2C_PXA_SLAVE
	if (plat) {
		i2c->slave_addr = plat->slave_addr;
		i2c->slave = plat->slave;
	}
#endif

	switch (dev->id) {
	case 0:
#ifdef CONFIG_PXA27x
		pxa_gpio_mode(GPIO117_I2CSCL_MD);
		pxa_gpio_mode(GPIO118_I2CSDA_MD);
#endif
		pxa_set_cken(CKEN14_I2C, 1);
		break;
#ifdef CONFIG_PXA27x
	case 1:
		local_irq_disable();
		PCFR |= PCFR_PI2CEN;
		local_irq_enable();
		pxa_set_cken(CKEN15_PWRI2C, 1);
#endif
	}

	ret = request_irq(irq, i2c_pxa_handler, IRQF_DISABLED,
			  i2c->adap.name, i2c);
	if (ret)
		goto ereqirq;


	i2c_pxa_reset(i2c);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &dev->dev;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		printk(KERN_INFO "I2C: Failed to add bus\n");
		goto eadapt;
	}

	platform_set_drvdata(dev, i2c);

#ifdef CONFIG_I2C_PXA_SLAVE
	printk(KERN_INFO "I2C: %s: PXA I2C adapter, slave address %d\n",
	       i2c->adap.dev.bus_id, i2c->slave_addr);
#else
	printk(KERN_INFO "I2C: %s: PXA I2C adapter\n",
	       i2c->adap.dev.bus_id);
#endif
	return 0;

eadapt:
	free_irq(irq, i2c);
ereqirq:
	switch (dev->id) {
	case 0:
		pxa_set_cken(CKEN14_I2C, 0);
		break;
#ifdef CONFIG_PXA27x
	case 1:
		pxa_set_cken(CKEN15_PWRI2C, 0);
		local_irq_disable();
		PCFR &= ~PCFR_PI2CEN;
		local_irq_enable();
#endif
	}
eremap:
	kfree(i2c);
emalloc:
	release_mem_region(res->start, res_len(res));
	return ret;
}

static int i2c_pxa_remove(struct platform_device *dev)
{
	struct pxa_i2c *i2c = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq, i2c);
	switch (dev->id) {
	case 0:
		pxa_set_cken(CKEN14_I2C, 0);
		break;
#ifdef CONFIG_PXA27x
	case 1:
		pxa_set_cken(CKEN15_PWRI2C, 0);
		local_irq_disable();
		PCFR &= ~PCFR_PI2CEN;
		local_irq_enable();
#endif
	}
	release_mem_region(i2c->iobase, i2c->iosize);
	kfree(i2c);

	return 0;
}

static struct platform_driver i2c_pxa_driver = {
	.probe		= i2c_pxa_probe,
	.remove		= i2c_pxa_remove,
	.driver		= {
		.name	= "pxa2xx-i2c",
	},
};

static int __init i2c_adap_pxa_init(void)
{
	return platform_driver_register(&i2c_pxa_driver);
}

static void i2c_adap_pxa_exit(void)
{
	return platform_driver_unregister(&i2c_pxa_driver);
}

MODULE_LICENSE("GPL");

module_init(i2c_adap_pxa_init);
module_exit(i2c_adap_pxa_exit);
