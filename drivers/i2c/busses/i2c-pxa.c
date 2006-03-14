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
};

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
	dev_dbg(&i2c->adap.dev, "state:%s:%d: ISR=%08x, ICR=%08x, IBMR=%02x\n", fname, lno, ISR, ICR, IBMR);
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
	       "i2c: log: ", ICR, ISR);
	for (i = 0; i < i2c->irqlogidx; i++)
		printk("[%08x:%08x] ", i2c->isrlog[i], i2c->icrlog[i]);
	printk("\n");
}

static inline int i2c_pxa_is_slavemode(struct pxa_i2c *i2c)
{
	return !(ICR & ICR_SCLE);
}

static void i2c_pxa_abort(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ/4;

	if (i2c_pxa_is_slavemode(i2c)) {
		dev_dbg(&i2c->adap.dev, "%s: called in slave mode\n", __func__);
		return;
	}

	while (time_before(jiffies, timeout) && (IBMR & 0x1) == 0) {
		unsigned long icr = ICR;

		icr &= ~ICR_START;
		icr |= ICR_ACKNAK | ICR_STOP | ICR_TB;

		ICR = icr;

		show_state(i2c);

		msleep(1);
	}

	ICR &= ~(ICR_MA | ICR_START | ICR_STOP);
}

static int i2c_pxa_wait_bus_not_busy(struct pxa_i2c *i2c)
{
	int timeout = DEF_TIMEOUT;

	while (timeout-- && ISR & (ISR_IBB | ISR_UB)) {
		if ((ISR & ISR_SAD) != 0)
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
				__func__, (long)jiffies, ISR, ICR, IBMR);

		if (ISR & ISR_SAD) {
			if (i2c_debug > 0)
				dev_dbg(&i2c->adap.dev, "%s: Slave detected\n", __func__);
			goto out;
		}

		/* wait for unit and bus being not busy, and we also do a
		 * quick check of the i2c lines themselves to ensure they've
		 * gone high...
		 */
		if ((ISR & (ISR_UB | ISR_IBB)) == 0 && IBMR == 3) {
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

	if ((ISR & (ISR_UB | ISR_IBB)) != 0) {
		dev_dbg(&i2c->adap.dev, "%s: unit is busy\n", __func__);
		if (!i2c_pxa_wait_master(i2c)) {
			dev_dbg(&i2c->adap.dev, "%s: error: unit busy\n", __func__);
			return I2C_RETRY;
		}
	}

	ICR |= ICR_SCLE;
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
				__func__, (long)jiffies, ISR, ICR, IBMR);

		if ((ISR & (ISR_UB|ISR_IBB|ISR_SAD)) == ISR_SAD ||
		    (ICR & ICR_SCLE) == 0) {
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
		if (ICR & ICR_STOP) {
			udelay(100);
			ICR &= ~ICR_STOP;
		}

		if (!i2c_pxa_wait_slave(i2c)) {
			dev_err(&i2c->adap.dev, "%s: wait timedout\n",
				__func__);
			return;
		}
	}

	ICR &= ~(ICR_STOP|ICR_ACKNAK|ICR_MA);
	ICR &= ~ICR_SCLE;

	if (i2c_debug) {
		dev_dbg(&i2c->adap.dev, "ICR now %08x, ISR %08x\n", ICR, ISR);
		decode_ICR(ICR);
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
	ICR = ICR_UR;
	ISR = I2C_ISR_INIT;
	ICR &= ~ICR_UR;

	ISAR = i2c->slave_addr;

	/* set control register values */
	ICR = I2C_ICR_INIT;

#ifdef CONFIG_I2C_PXA_SLAVE
	dev_info(&i2c->adap.dev, "Enabling slave mode\n");
	ICR |= ICR_SADIE | ICR_ALDIE | ICR_SSDIE;
#endif

	i2c_pxa_set_slave(i2c, 0);

	/* enable unit */
	ICR |= ICR_IUE;
	udelay(100);
}


#ifdef CONFIG_I2C_PXA_SLAVE
/*
 * I2C EEPROM emulation.
 */
static struct i2c_eeprom_emu eeprom = {
	.size = I2C_EEPROM_EMU_SIZE,
	.watch = LIST_HEAD_INIT(eeprom.watch),
};

struct i2c_eeprom_emu *i2c_pxa_get_eeprom(void)
{
	return &eeprom;
}

int i2c_eeprom_emu_addwatcher(struct i2c_eeprom_emu *emu, void *data,
			      unsigned int addr, unsigned int size,
			      struct i2c_eeprom_emu_watcher *watcher)
{
	struct i2c_eeprom_emu_watch *watch;
	unsigned long flags;

	if (addr + size > emu->size)
		return -EINVAL;

	watch = kmalloc(sizeof(struct i2c_eeprom_emu_watch), GFP_KERNEL);
	if (watch) {
		watch->start = addr;
		watch->end = addr + size - 1;
		watch->ops = watcher;
		watch->data = data;

		local_irq_save(flags);
		list_add(&watch->node, &emu->watch);
		local_irq_restore(flags);
	}

	return watch ? 0 : -ENOMEM;
}

void i2c_eeprom_emu_delwatcher(struct i2c_eeprom_emu *emu, void *data,
			       struct i2c_eeprom_emu_watcher *watcher)
{
	struct i2c_eeprom_emu_watch *watch, *n;
	unsigned long flags;

	list_for_each_entry_safe(watch, n, &emu->watch, node) {
		if (watch->ops == watcher && watch->data == data) {
			local_irq_save(flags);
			list_del(&watch->node);
			local_irq_restore(flags);
			kfree(watch);
		}
	}
}

static void i2c_eeprom_emu_event(void *ptr, i2c_slave_event_t event)
{
	struct i2c_eeprom_emu *emu = ptr;

	eedbg(3, "i2c_eeprom_emu_event: %d\n", event);

	switch (event) {
	case I2C_SLAVE_EVENT_START_WRITE:
		emu->seen_start = 1;
		eedbg(2, "i2c_eeprom: write initiated\n");
		break;

	case I2C_SLAVE_EVENT_START_READ:
		emu->seen_start = 0;
		eedbg(2, "i2c_eeprom: read initiated\n");
		break;

	case I2C_SLAVE_EVENT_STOP:
		emu->seen_start = 0;
		eedbg(2, "i2c_eeprom: received stop\n");
		break;

	default:
		eedbg(0, "i2c_eeprom: unhandled event\n");
		break;
	}
}

static int i2c_eeprom_emu_read(void *ptr)
{
	struct i2c_eeprom_emu *emu = ptr;
	int ret;

	ret = emu->bytes[emu->ptr];
	emu->ptr = (emu->ptr + 1) % emu->size;

	return ret;
}

static void i2c_eeprom_emu_write(void *ptr, unsigned int val)
{
	struct i2c_eeprom_emu *emu = ptr;
	struct i2c_eeprom_emu_watch *watch;

	if (emu->seen_start != 0) {
		eedbg(2, "i2c_eeprom_emu_write: setting ptr %02x\n", val);
		emu->ptr = val;
		emu->seen_start = 0;
		return;
	}

	emu->bytes[emu->ptr] = val;

	eedbg(1, "i2c_eeprom_emu_write: ptr=0x%02x, val=0x%02x\n",
	      emu->ptr, val);

	list_for_each_entry(watch, &emu->watch, node) {
		if (!watch->ops || !watch->ops->write)
			continue;
		if (watch->start <= emu->ptr && watch->end >= emu->ptr)
			watch->ops->write(watch->data, emu->ptr, val);
	}

	emu->ptr = (emu->ptr + 1) % emu->size;
}

struct i2c_slave_client eeprom_client = {
	.data	= &eeprom,
	.event	= i2c_eeprom_emu_event,
	.read	= i2c_eeprom_emu_read,
	.write	= i2c_eeprom_emu_write
};

/*
 * PXA I2C Slave mode
 */

static void i2c_pxa_slave_txempty(struct pxa_i2c *i2c, u32 isr)
{
	if (isr & ISR_BED) {
		/* what should we do here? */
	} else {
		int ret = i2c->slave->read(i2c->slave->data);

		IDBR = ret;
		ICR |= ICR_TB;   /* allow next byte */
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	unsigned int byte = IDBR;

	if (i2c->slave != NULL)
		i2c->slave->write(i2c->slave->data, byte);

	ICR |= ICR_TB;
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
	ICR &= ~(ICR_START|ICR_STOP);
	ICR |= ICR_TB;

	timeout = 0x10000;

	while (1) {
		if ((IBMR & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	ICR &= ~ICR_SCLE;
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
		IDBR = 0;
		ICR |= ICR_TB;
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	ICR |= ICR_TB | ICR_ACKNAK;
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	ICR &= ~(ICR_START|ICR_STOP);
	ICR |= ICR_TB | ICR_ACKNAK;

	timeout = 0x10000;

	while (1) {
		if ((IBMR & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	ICR &= ~ICR_SCLE;
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
	IDBR = i2c_pxa_addr_byte(i2c->msg);

	/*
	 * Step 2: initiate the write.
	 */
	icr = ICR & ~(ICR_STOP | ICR_ALDIE);
	ICR = icr | ICR_START | ICR_TB;
}

/*
 * We are protected by the adapter bus semaphore.
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
	u32 icr = ICR & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

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
		IDBR = i2c->msg->buf[i2c->msg_ptr++];

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
		IDBR = i2c_pxa_addr_byte(i2c->msg);

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

	ICR = icr;
	show_state(i2c);
}

static void i2c_pxa_irq_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	u32 icr = ICR & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

	/*
	 * Read the byte.
	 */
	i2c->msg->buf[i2c->msg_ptr++] = IDBR;

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

	ICR = icr;
}

static irqreturn_t i2c_pxa_handler(int this_irq, void *dev_id, struct pt_regs *regs)
{
	struct pxa_i2c *i2c = dev_id;
	u32 isr = ISR;

	if (i2c_debug > 2 && 0) {
		dev_dbg(&i2c->adap.dev, "%s: ISR=%08x, ICR=%08x, IBMR=%02x\n",
			__func__, isr, ICR, IBMR);
		decode_ISR(isr);
	}

	if (i2c->irqlogidx < ARRAY_SIZE(i2c->isrlog))
		i2c->isrlog[i2c->irqlogidx++] = isr;

	show_state(i2c);

	/*
	 * Always clear all pending IRQs.
	 */
	ISR = isr & (ISR_SSD|ISR_ALD|ISR_ITE|ISR_IRF|ISR_SAD|ISR_BED);

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
	if (!(ICR & ICR_IUE))
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

static struct i2c_algorithm i2c_pxa_algorithm = {
	.master_xfer	= i2c_pxa_xfer,
	.functionality	= i2c_pxa_functionality,
};

static struct pxa_i2c i2c_pxa = {
	.lock	= SPIN_LOCK_UNLOCKED,
	.wait	= __WAIT_QUEUE_HEAD_INITIALIZER(i2c_pxa.wait),
	.adap	= {
		.owner		= THIS_MODULE,
		.algo		= &i2c_pxa_algorithm,
		.name		= "pxa2xx-i2c",
		.retries	= 5,
	},
};

static int i2c_pxa_probe(struct platform_device *dev)
{
	struct pxa_i2c *i2c = &i2c_pxa;
#ifdef CONFIG_I2C_PXA_SLAVE
	struct i2c_pxa_platform_data *plat = dev->dev.platform_data;
#endif
	int ret;

#ifdef CONFIG_PXA27x
	pxa_gpio_mode(GPIO117_I2CSCL_MD);
	pxa_gpio_mode(GPIO118_I2CSDA_MD);
	udelay(100);
#endif

	i2c->slave_addr = I2C_PXA_SLAVE_ADDR;

#ifdef CONFIG_I2C_PXA_SLAVE
	i2c->slave = &eeprom_client;
	if (plat) {
		i2c->slave_addr = plat->slave_addr;
		if (plat->slave)
			i2c->slave = plat->slave;
	}
#endif

	pxa_set_cken(CKEN14_I2C, 1);
	ret = request_irq(IRQ_I2C, i2c_pxa_handler, SA_INTERRUPT,
			  "pxa2xx-i2c", i2c);
	if (ret)
		goto out;

	i2c_pxa_reset(i2c);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &dev->dev;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		printk(KERN_INFO "I2C: Failed to add bus\n");
		goto err_irq;
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

 err_irq:
	free_irq(IRQ_I2C, i2c);
 out:
	return ret;
}

static int i2c_pxa_remove(struct platform_device *dev)
{
	struct pxa_i2c *i2c = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	i2c_del_adapter(&i2c->adap);
	free_irq(IRQ_I2C, i2c);
	pxa_set_cken(CKEN14_I2C, 0);

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
