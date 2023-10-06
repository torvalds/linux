// SPDX-License-Identifier: GPL-2.0-only
/*
 *  i2c_adap_pxa.c
 *
 *  I2C adapter for the PXA I2C bus access.
 *
 *  Copyright (C) 2002 Intrinsyc Software Inc.
 *  Copyright (C) 2004-2005 Deep Blue Solutions Ltd.
 *
 *  History:
 *    Apr 2002: Initial version [CS]
 *    Jun 2002: Properly separated algo/adap [FB]
 *    Jan 2003: Fixed several bugs concerning interrupt handling [Kai-Uwe Bloem]
 *    Jan 2003: added limited signal handling [Kai-Uwe Bloem]
 *    Sep 2004: Major rework to ensure efficient bus handling [RMK]
 *    Dec 2004: Added support for PXA27x and slave device probing [Liam Girdwood]
 *    Feb 2005: Rework slave mode handling [RMK]
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/property.h>
#include <linux/slab.h>

/* I2C register field definitions */
#define IBMR_SDAS	(1 << 0)
#define IBMR_SCLS	(1 << 1)

#define ICR_START	(1 << 0)	   /* start bit */
#define ICR_STOP	(1 << 1)	   /* stop bit */
#define ICR_ACKNAK	(1 << 2)	   /* send ACK(0) or NAK(1) */
#define ICR_TB		(1 << 3)	   /* transfer byte bit */
#define ICR_MA		(1 << 4)	   /* master abort */
#define ICR_SCLE	(1 << 5)	   /* master clock enable */
#define ICR_IUE		(1 << 6)	   /* unit enable */
#define ICR_GCD		(1 << 7)	   /* general call disable */
#define ICR_ITEIE	(1 << 8)	   /* enable tx interrupts */
#define ICR_IRFIE	(1 << 9)	   /* enable rx interrupts */
#define ICR_BEIE	(1 << 10)	   /* enable bus error ints */
#define ICR_SSDIE	(1 << 11)	   /* slave STOP detected int enable */
#define ICR_ALDIE	(1 << 12)	   /* enable arbitration interrupt */
#define ICR_SADIE	(1 << 13)	   /* slave address detected int enable */
#define ICR_UR		(1 << 14)	   /* unit reset */
#define ICR_FM		(1 << 15)	   /* fast mode */
#define ICR_HS		(1 << 16)	   /* High Speed mode */
#define ICR_A3700_FM	(1 << 16)	   /* fast mode for armada-3700 */
#define ICR_A3700_HS	(1 << 17)	   /* high speed mode for armada-3700 */
#define ICR_GPIOEN	(1 << 19)	   /* enable GPIO mode for SCL in HS */

#define ISR_RWM		(1 << 0)	   /* read/write mode */
#define ISR_ACKNAK	(1 << 1)	   /* ack/nak status */
#define ISR_UB		(1 << 2)	   /* unit busy */
#define ISR_IBB		(1 << 3)	   /* bus busy */
#define ISR_SSD		(1 << 4)	   /* slave stop detected */
#define ISR_ALD		(1 << 5)	   /* arbitration loss detected */
#define ISR_ITE		(1 << 6)	   /* tx buffer empty */
#define ISR_IRF		(1 << 7)	   /* rx buffer full */
#define ISR_GCAD	(1 << 8)	   /* general call address detected */
#define ISR_SAD		(1 << 9)	   /* slave address detected */
#define ISR_BED		(1 << 10)	   /* bus error no ACK/NAK */

#define ILCR_SLV_SHIFT		0
#define ILCR_SLV_MASK		(0x1FF << ILCR_SLV_SHIFT)
#define ILCR_FLV_SHIFT		9
#define ILCR_FLV_MASK		(0x1FF << ILCR_FLV_SHIFT)
#define ILCR_HLVL_SHIFT		18
#define ILCR_HLVL_MASK		(0x1FF << ILCR_HLVL_SHIFT)
#define ILCR_HLVH_SHIFT		27
#define ILCR_HLVH_MASK		(0x1F << ILCR_HLVH_SHIFT)

#define IWCR_CNT_SHIFT		0
#define IWCR_CNT_MASK		(0x1F << IWCR_CNT_SHIFT)
#define IWCR_HS_CNT1_SHIFT	5
#define IWCR_HS_CNT1_MASK	(0x1F << IWCR_HS_CNT1_SHIFT)
#define IWCR_HS_CNT2_SHIFT	10
#define IWCR_HS_CNT2_MASK	(0x1F << IWCR_HS_CNT2_SHIFT)

/* need a longer timeout if we're dealing with the fact we may well be
 * looking at a multi-master environment
 */
#define DEF_TIMEOUT             32

#define NO_SLAVE		(-ENXIO)
#define BUS_ERROR               (-EREMOTEIO)
#define XFER_NAKED              (-ECONNREFUSED)
#define I2C_RETRY               (-2000) /* an error has occurred retry transmit */

/* ICR initialize bit values
 *
 * 15 FM     0 (100 kHz operation)
 * 14 UR     0 (No unit reset)
 * 13 SADIE  0 (Disables the unit from interrupting on slave addresses
 *              matching its slave address)
 * 12 ALDIE  0 (Disables the unit from interrupt when it loses arbitration
 *              in master mode)
 * 11 SSDIE  0 (Disables interrupts from a slave stop detected, in slave mode)
 * 10 BEIE   1 (Enable interrupts from detected bus errors, no ACK sent)
 *  9 IRFIE  1 (Enable interrupts from full buffer received)
 *  8 ITEIE  1 (Enables the I2C unit to interrupt when transmit buffer empty)
 *  7 GCD    1 (Disables i2c unit response to general call messages as a slave)
 *  6 IUE    0 (Disable unit until we change settings)
 *  5 SCLE   1 (Enables the i2c clock output for master mode (drives SCL)
 *  4 MA     0 (Only send stop with the ICR stop bit)
 *  3 TB     0 (We are not transmitting a byte initially)
 *  2 ACKNAK 0 (Send an ACK after the unit receives a byte)
 *  1 STOP   0 (Do not send a STOP)
 *  0 START  0 (Do not send a START)
 */
#define I2C_ICR_INIT	(ICR_BEIE | ICR_IRFIE | ICR_ITEIE | ICR_GCD | ICR_SCLE)

/* I2C status register init values
 *
 * 10 BED    1 (Clear bus error detected)
 *  9 SAD    1 (Clear slave address detected)
 *  7 IRF    1 (Clear IDBR Receive Full)
 *  6 ITE    1 (Clear IDBR Transmit Empty)
 *  5 ALD    1 (Clear Arbitration Loss Detected)
 *  4 SSD    1 (Clear Slave Stop Detected)
 */
#define I2C_ISR_INIT	0x7FF  /* status register init */

struct pxa_reg_layout {
	u32 ibmr;
	u32 idbr;
	u32 icr;
	u32 isr;
	u32 isar;
	u32 ilcr;
	u32 iwcr;
	u32 fm;
	u32 hs;
};

enum pxa_i2c_types {
	REGS_PXA2XX,
	REGS_PXA3XX,
	REGS_CE4100,
	REGS_PXA910,
	REGS_A3700,
};

/* I2C register layout definitions */
static struct pxa_reg_layout pxa_reg_layout[] = {
	[REGS_PXA2XX] = {
		.ibmr =	0x00,
		.idbr =	0x08,
		.icr =	0x10,
		.isr =	0x18,
		.isar =	0x20,
		.fm = ICR_FM,
		.hs = ICR_HS,
	},
	[REGS_PXA3XX] = {
		.ibmr =	0x00,
		.idbr =	0x04,
		.icr =	0x08,
		.isr =	0x0c,
		.isar =	0x10,
		.fm = ICR_FM,
		.hs = ICR_HS,
	},
	[REGS_CE4100] = {
		.ibmr =	0x14,
		.idbr =	0x0c,
		.icr =	0x00,
		.isr =	0x04,
		/* no isar register */
		.fm = ICR_FM,
		.hs = ICR_HS,
	},
	[REGS_PXA910] = {
		.ibmr = 0x00,
		.idbr = 0x08,
		.icr =	0x10,
		.isr =	0x18,
		.isar = 0x20,
		.ilcr = 0x28,
		.iwcr = 0x30,
		.fm = ICR_FM,
		.hs = ICR_HS,
	},
	[REGS_A3700] = {
		.ibmr =	0x00,
		.idbr =	0x04,
		.icr =	0x08,
		.isr =	0x0c,
		.isar =	0x10,
		.fm = ICR_A3700_FM,
		.hs = ICR_A3700_HS,
	},
};

static const struct of_device_id i2c_pxa_dt_ids[] = {
	{ .compatible = "mrvl,pxa-i2c", .data = (void *)REGS_PXA2XX },
	{ .compatible = "mrvl,pwri2c", .data = (void *)REGS_PXA3XX },
	{ .compatible = "mrvl,mmp-twsi", .data = (void *)REGS_PXA910 },
	{ .compatible = "marvell,armada-3700-i2c", .data = (void *)REGS_A3700 },
	{}
};
MODULE_DEVICE_TABLE(of, i2c_pxa_dt_ids);

static const struct platform_device_id i2c_pxa_id_table[] = {
	{ "pxa2xx-i2c",		REGS_PXA2XX },
	{ "pxa3xx-pwri2c",	REGS_PXA3XX },
	{ "ce4100-i2c",		REGS_CE4100 },
	{ "pxa910-i2c",		REGS_PXA910 },
	{ "armada-3700-i2c",	REGS_A3700  },
	{ },
};
MODULE_DEVICE_TABLE(platform, i2c_pxa_id_table);

struct pxa_i2c {
	spinlock_t		lock;
	wait_queue_head_t	wait;
	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;
	unsigned int		slave_addr;
	unsigned int		req_slave_addr;

	struct i2c_adapter	adap;
	struct clk		*clk;
#ifdef CONFIG_I2C_PXA_SLAVE
	struct i2c_client	*slave;
#endif

	unsigned int		irqlogidx;
	u32			isrlog[32];
	u32			icrlog[32];

	void __iomem		*reg_base;
	void __iomem		*reg_ibmr;
	void __iomem		*reg_idbr;
	void __iomem		*reg_icr;
	void __iomem		*reg_isr;
	void __iomem		*reg_isar;
	void __iomem		*reg_ilcr;
	void __iomem		*reg_iwcr;

	unsigned long		iobase;
	unsigned long		iosize;

	int			irq;
	unsigned int		use_pio :1;
	unsigned int		fast_mode :1;
	unsigned int		high_mode:1;
	unsigned char		master_code;
	unsigned long		rate;
	bool			highmode_enter;
	u32			fm_mask;
	u32			hs_mask;

	struct i2c_bus_recovery_info recovery;
};

#define _IBMR(i2c)	((i2c)->reg_ibmr)
#define _IDBR(i2c)	((i2c)->reg_idbr)
#define _ICR(i2c)	((i2c)->reg_icr)
#define _ISR(i2c)	((i2c)->reg_isr)
#define _ISAR(i2c)	((i2c)->reg_isar)
#define _ILCR(i2c)	((i2c)->reg_ilcr)
#define _IWCR(i2c)	((i2c)->reg_iwcr)

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
#define PXA_BIT(m, s, u)	{ .mask = m, .set = s, .unset = u }

static inline void
decode_bits(const char *prefix, const struct bits *bits, int num, u32 val)
{
	printk("%s %08x:", prefix, val);
	while (num--) {
		const char *str = val & bits->mask ? bits->set : bits->unset;
		if (str)
			pr_cont(" %s", str);
		bits++;
	}
	pr_cont("\n");
}

static const struct bits isr_bits[] = {
	PXA_BIT(ISR_RWM,	"RX",		"TX"),
	PXA_BIT(ISR_ACKNAK,	"NAK",		"ACK"),
	PXA_BIT(ISR_UB,		"Bsy",		"Rdy"),
	PXA_BIT(ISR_IBB,	"BusBsy",	"BusRdy"),
	PXA_BIT(ISR_SSD,	"SlaveStop",	NULL),
	PXA_BIT(ISR_ALD,	"ALD",		NULL),
	PXA_BIT(ISR_ITE,	"TxEmpty",	NULL),
	PXA_BIT(ISR_IRF,	"RxFull",	NULL),
	PXA_BIT(ISR_GCAD,	"GenCall",	NULL),
	PXA_BIT(ISR_SAD,	"SlaveAddr",	NULL),
	PXA_BIT(ISR_BED,	"BusErr",	NULL),
};

static void decode_ISR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ISR", isr_bits, ARRAY_SIZE(isr_bits), val);
}

static const struct bits icr_bits[] = {
	PXA_BIT(ICR_START,  "START",	NULL),
	PXA_BIT(ICR_STOP,   "STOP",	NULL),
	PXA_BIT(ICR_ACKNAK, "ACKNAK",	NULL),
	PXA_BIT(ICR_TB,     "TB",	NULL),
	PXA_BIT(ICR_MA,     "MA",	NULL),
	PXA_BIT(ICR_SCLE,   "SCLE",	"scle"),
	PXA_BIT(ICR_IUE,    "IUE",	"iue"),
	PXA_BIT(ICR_GCD,    "GCD",	NULL),
	PXA_BIT(ICR_ITEIE,  "ITEIE",	NULL),
	PXA_BIT(ICR_IRFIE,  "IRFIE",	NULL),
	PXA_BIT(ICR_BEIE,   "BEIE",	NULL),
	PXA_BIT(ICR_SSDIE,  "SSDIE",	NULL),
	PXA_BIT(ICR_ALDIE,  "ALDIE",	NULL),
	PXA_BIT(ICR_SADIE,  "SADIE",	NULL),
	PXA_BIT(ICR_UR,     "UR",		"ur"),
};

#ifdef CONFIG_I2C_PXA_SLAVE
static void decode_ICR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ICR", icr_bits, ARRAY_SIZE(icr_bits), val);
}
#endif

static unsigned int i2c_debug = DEBUG;

static void i2c_pxa_show_state(struct pxa_i2c *i2c, int lno, const char *fname)
{
	dev_dbg(&i2c->adap.dev, "state:%s:%d: ISR=%08x, ICR=%08x, IBMR=%02x\n", fname, lno,
		readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));
}

#define show_state(i2c) i2c_pxa_show_state(i2c, __LINE__, __func__)

static void i2c_pxa_scream_blue_murder(struct pxa_i2c *i2c, const char *why)
{
	unsigned int i;
	struct device *dev = &i2c->adap.dev;

	dev_err(dev, "slave_0x%x error: %s\n",
		i2c->req_slave_addr >> 1, why);
	dev_err(dev, "msg_num: %d msg_idx: %d msg_ptr: %d\n",
		i2c->msg_num, i2c->msg_idx, i2c->msg_ptr);
	dev_err(dev, "IBMR: %08x IDBR: %08x ICR: %08x ISR: %08x\n",
		readl(_IBMR(i2c)), readl(_IDBR(i2c)), readl(_ICR(i2c)),
		readl(_ISR(i2c)));
	dev_err(dev, "log:");
	for (i = 0; i < i2c->irqlogidx; i++)
		pr_cont(" [%03x:%05x]", i2c->isrlog[i], i2c->icrlog[i]);
	pr_cont("\n");
}

#else /* ifdef DEBUG */

#define i2c_debug	0

#define show_state(i2c) do { } while (0)
#define decode_ISR(val) do { } while (0)
#define decode_ICR(val) do { } while (0)
#define i2c_pxa_scream_blue_murder(i2c, why) do { } while (0)

#endif /* ifdef DEBUG / else */

static void i2c_pxa_master_complete(struct pxa_i2c *i2c, int ret);

static inline int i2c_pxa_is_slavemode(struct pxa_i2c *i2c)
{
	return !(readl(_ICR(i2c)) & ICR_SCLE);
}

static void i2c_pxa_abort(struct pxa_i2c *i2c)
{
	int i = 250;

	if (i2c_pxa_is_slavemode(i2c)) {
		dev_dbg(&i2c->adap.dev, "%s: called in slave mode\n", __func__);
		return;
	}

	while ((i > 0) && (readl(_IBMR(i2c)) & IBMR_SDAS) == 0) {
		unsigned long icr = readl(_ICR(i2c));

		icr &= ~ICR_START;
		icr |= ICR_ACKNAK | ICR_STOP | ICR_TB;

		writel(icr, _ICR(i2c));

		show_state(i2c);

		mdelay(1);
		i --;
	}

	writel(readl(_ICR(i2c)) & ~(ICR_MA | ICR_START | ICR_STOP),
	       _ICR(i2c));
}

static int i2c_pxa_wait_bus_not_busy(struct pxa_i2c *i2c)
{
	int timeout = DEF_TIMEOUT;
	u32 isr;

	while (1) {
		isr = readl(_ISR(i2c));
		if (!(isr & (ISR_IBB | ISR_UB)))
			return 0;

		if (isr & ISR_SAD)
			timeout += 4;

		if (!timeout--)
			break;

		msleep(2);
		show_state(i2c);
	}

	show_state(i2c);

	return I2C_RETRY;
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
		if ((readl(_ISR(i2c)) & (ISR_UB | ISR_IBB)) == 0 &&
		    readl(_IBMR(i2c)) == (IBMR_SCLS | IBMR_SDAS)) {
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

static void i2c_pxa_do_reset(struct pxa_i2c *i2c)
{
	/* reset according to 9.8 */
	writel(ICR_UR, _ICR(i2c));
	writel(I2C_ISR_INIT, _ISR(i2c));
	writel(readl(_ICR(i2c)) & ~ICR_UR, _ICR(i2c));

	if (i2c->reg_isar && IS_ENABLED(CONFIG_I2C_PXA_SLAVE))
		writel(i2c->slave_addr, _ISAR(i2c));

	/* set control register values */
	writel(I2C_ICR_INIT | (i2c->fast_mode ? i2c->fm_mask : 0), _ICR(i2c));
	writel(readl(_ICR(i2c)) | (i2c->high_mode ? i2c->hs_mask : 0), _ICR(i2c));

#ifdef CONFIG_I2C_PXA_SLAVE
	dev_info(&i2c->adap.dev, "Enabling slave mode\n");
	writel(readl(_ICR(i2c)) | ICR_SADIE | ICR_ALDIE | ICR_SSDIE, _ICR(i2c));
#endif

	i2c_pxa_set_slave(i2c, 0);
}

static void i2c_pxa_enable(struct pxa_i2c *i2c)
{
	/* enable unit */
	writel(readl(_ICR(i2c)) | ICR_IUE, _ICR(i2c));
	udelay(100);
}

static void i2c_pxa_reset(struct pxa_i2c *i2c)
{
	pr_debug("Resetting I2C Controller Unit\n");

	/* abort any transfer currently under way */
	i2c_pxa_abort(i2c);
	i2c_pxa_do_reset(i2c);
	i2c_pxa_enable(i2c);
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
		u8 byte = 0;

		if (i2c->slave != NULL)
			i2c_slave_event(i2c->slave, I2C_SLAVE_READ_PROCESSED,
					&byte);

		writel(byte, _IDBR(i2c));
		writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));   /* allow next byte */
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	u8 byte = readl(_IDBR(i2c));

	if (i2c->slave != NULL)
		i2c_slave_event(i2c->slave, I2C_SLAVE_WRITE_RECEIVED, &byte);

	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "SAD, mode is slave-%cx\n",
		       (isr & ISR_RWM) ? 'r' : 't');

	if (i2c->slave != NULL) {
		if (isr & ISR_RWM) {
			u8 byte = 0;

			i2c_slave_event(i2c->slave, I2C_SLAVE_READ_REQUESTED,
					&byte);
			writel(byte, _IDBR(i2c));
		} else {
			i2c_slave_event(i2c->slave, I2C_SLAVE_WRITE_REQUESTED,
					NULL);
		}
	}

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	writel(readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP), _ICR(i2c));
	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));

	timeout = 0x10000;

	while (1) {
		if ((readl(_IBMR(i2c)) & IBMR_SCLS) == IBMR_SCLS)
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
		i2c_slave_event(i2c->slave, I2C_SLAVE_STOP, NULL);

	if (i2c_debug > 2)
		dev_dbg(&i2c->adap.dev, "ISR: SSD (Slave Stop) acked\n");

	/*
	 * If we have a master-mode message waiting,
	 * kick it off now that the slave has completed.
	 */
	if (i2c->msg)
		i2c_pxa_master_complete(i2c, I2C_RETRY);
}

static int i2c_pxa_slave_reg(struct i2c_client *slave)
{
	struct pxa_i2c *i2c = slave->adapter->algo_data;

	if (i2c->slave)
		return -EBUSY;

	if (!i2c->reg_isar)
		return -EAFNOSUPPORT;

	i2c->slave = slave;
	i2c->slave_addr = slave->addr;

	writel(i2c->slave_addr, _ISAR(i2c));

	return 0;
}

static int i2c_pxa_slave_unreg(struct i2c_client *slave)
{
	struct pxa_i2c *i2c = slave->adapter->algo_data;

	WARN_ON(!i2c->slave);

	i2c->slave_addr = I2C_PXA_SLAVE_ADDR;
	writel(i2c->slave_addr, _ISAR(i2c));

	i2c->slave = NULL;

	return 0;
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
		if ((readl(_IBMR(i2c)) & IBMR_SCLS) == IBMR_SCLS)
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

static inline void i2c_pxa_start_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/*
	 * Step 1: target slave address into IDBR
	 */
	i2c->req_slave_addr = i2c_8bit_addr_from_msg(i2c->msg);
	writel(i2c->req_slave_addr, _IDBR(i2c));

	/*
	 * Step 2: initiate the write.
	 */
	icr = readl(_ICR(i2c)) & ~(ICR_STOP | ICR_ALDIE);
	writel(icr | ICR_START | ICR_TB, _ICR(i2c));
}

static inline void i2c_pxa_stop_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/* Clear the START, STOP, ACK, TB and MA flags */
	icr = readl(_ICR(i2c));
	icr &= ~(ICR_START | ICR_STOP | ICR_ACKNAK | ICR_TB | ICR_MA);
	writel(icr, _ICR(i2c));
}

/*
 * PXA I2C send master code
 * 1. Load master code to IDBR and send it.
 *    Note for HS mode, set ICR [GPIOEN].
 * 2. Wait until win arbitration.
 */
static int i2c_pxa_send_mastercode(struct pxa_i2c *i2c)
{
	u32 icr;
	long timeout;

	spin_lock_irq(&i2c->lock);
	i2c->highmode_enter = true;
	writel(i2c->master_code, _IDBR(i2c));

	icr = readl(_ICR(i2c)) & ~(ICR_STOP | ICR_ALDIE);
	icr |= ICR_GPIOEN | ICR_START | ICR_TB | ICR_ITEIE;
	writel(icr, _ICR(i2c));

	spin_unlock_irq(&i2c->lock);
	timeout = wait_event_timeout(i2c->wait,
			i2c->highmode_enter == false, HZ * 1);

	i2c->highmode_enter = false;

	return (timeout == 0) ? I2C_RETRY : 0;
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
	if (!i2c->use_pio)
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

	if ((isr & ISR_BED) &&
		(!((i2c->msg->flags & I2C_M_IGNORE_NAK) &&
			(isr & ISR_ACKNAK)))) {
		int ret = BUS_ERROR;

		/*
		 * I2C bus error - either the device NAK'd us, or
		 * something more serious happened.  If we were NAK'd
		 * on the initial address phase, we can retry.
		 */
		if (isr & ISR_ACKNAK) {
			if (i2c->msg_ptr == 0 && i2c->msg_idx == 0)
				ret = NO_SLAVE;
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
		 * If this is the last byte of the last message or last byte
		 * of any message with I2C_M_STOP (e.g. SCCB), send a STOP.
		 */
		if ((i2c->msg_ptr == i2c->msg->len) &&
			((i2c->msg->flags & I2C_M_STOP) ||
			(i2c->msg_idx == i2c->msg_num - 1)))
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
		i2c->req_slave_addr = i2c_8bit_addr_from_msg(i2c->msg);
		writel(i2c->req_slave_addr, _IDBR(i2c));

		/*
		 * And trigger a repeated start, and send the byte.
		 */
		icr &= ~ICR_ALDIE;
		icr |= ICR_START | ICR_TB;
	} else {
		if (i2c->msg->len == 0)
			icr |= ICR_MA;
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

#define VALID_INT_SOURCE	(ISR_SSD | ISR_ALD | ISR_ITE | ISR_IRF | \
				ISR_SAD | ISR_BED)
static irqreturn_t i2c_pxa_handler(int this_irq, void *dev_id)
{
	struct pxa_i2c *i2c = dev_id;
	u32 isr = readl(_ISR(i2c));

	if (!(isr & VALID_INT_SOURCE))
		return IRQ_NONE;

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
	writel(isr & VALID_INT_SOURCE, _ISR(i2c));

	if (isr & ISR_SAD)
		i2c_pxa_slave_start(i2c, isr);
	if (isr & ISR_SSD)
		i2c_pxa_slave_stop(i2c);

	if (i2c_pxa_is_slavemode(i2c)) {
		if (isr & ISR_ITE)
			i2c_pxa_slave_txempty(i2c, isr);
		if (isr & ISR_IRF)
			i2c_pxa_slave_rxfull(i2c, isr);
	} else if (i2c->msg && (!i2c->highmode_enter)) {
		if (isr & ISR_ITE)
			i2c_pxa_irq_txempty(i2c, isr);
		if (isr & ISR_IRF)
			i2c_pxa_irq_rxfull(i2c, isr);
	} else if ((isr & ISR_ITE) && i2c->highmode_enter) {
		i2c->highmode_enter = false;
		wake_up(&i2c->wait);
	} else {
		i2c_pxa_scream_blue_murder(i2c, "spurious irq");
	}

	return IRQ_HANDLED;
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
		i2c_recover_bus(&i2c->adap);
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

	if (i2c->high_mode) {
		ret = i2c_pxa_send_mastercode(i2c);
		if (ret) {
			dev_err(&i2c->adap.dev, "i2c_pxa_send_mastercode timeout\n");
			goto out;
			}
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

	if (!timeout && i2c->msg_num) {
		i2c_pxa_scream_blue_murder(i2c, "timeout with active message");
		i2c_recover_bus(&i2c->adap);
		ret = I2C_RETRY;
	}

 out:
	return ret;
}

static int i2c_pxa_internal_xfer(struct pxa_i2c *i2c,
				 struct i2c_msg *msgs, int num,
				 int (*xfer)(struct pxa_i2c *,
					     struct i2c_msg *, int num))
{
	int ret, i;

	for (i = 0; ; ) {
		ret = xfer(i2c, msgs, num);
		if (ret != I2C_RETRY && ret != NO_SLAVE)
			goto out;
		if (++i >= i2c->adap.retries)
			break;

		if (i2c_debug)
			dev_dbg(&i2c->adap.dev, "Retrying transmission\n");
		udelay(100);
	}
	if (ret != NO_SLAVE)
		i2c_pxa_scream_blue_murder(i2c, "exhausted retries");
	ret = -EREMOTEIO;
 out:
	i2c_pxa_set_slave(i2c, ret);
	return ret;
}

static int i2c_pxa_xfer(struct i2c_adapter *adap,
			struct i2c_msg msgs[], int num)
{
	struct pxa_i2c *i2c = adap->algo_data;

	return i2c_pxa_internal_xfer(i2c, msgs, num, i2c_pxa_do_xfer);
}

static u32 i2c_pxa_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
		I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_NOSTART;
}

static const struct i2c_algorithm i2c_pxa_algorithm = {
	.master_xfer	= i2c_pxa_xfer,
	.functionality	= i2c_pxa_functionality,
#ifdef CONFIG_I2C_PXA_SLAVE
	.reg_slave	= i2c_pxa_slave_reg,
	.unreg_slave	= i2c_pxa_slave_unreg,
#endif
};

/* Non-interrupt mode support */
static int i2c_pxa_pio_set_master(struct pxa_i2c *i2c)
{
	/* make timeout the same as for interrupt based functions */
	long timeout = 2 * DEF_TIMEOUT;

	/*
	 * Wait for the bus to become free.
	 */
	while (timeout-- && readl(_ISR(i2c)) & (ISR_IBB | ISR_UB))
		udelay(1000);

	if (timeout < 0) {
		show_state(i2c);
		dev_err(&i2c->adap.dev,
			"i2c_pxa: timeout waiting for bus free (set_master)\n");
		return I2C_RETRY;
	}

	/*
	 * Set master mode.
	 */
	writel(readl(_ICR(i2c)) | ICR_SCLE, _ICR(i2c));

	return 0;
}

static int i2c_pxa_do_pio_xfer(struct pxa_i2c *i2c,
			       struct i2c_msg *msg, int num)
{
	unsigned long timeout = 500000; /* 5 seconds */
	int ret = 0;

	ret = i2c_pxa_pio_set_master(i2c);
	if (ret)
		goto out;

	i2c->msg = msg;
	i2c->msg_num = num;
	i2c->msg_idx = 0;
	i2c->msg_ptr = 0;
	i2c->irqlogidx = 0;

	i2c_pxa_start_message(i2c);

	while (i2c->msg_num > 0 && --timeout) {
		i2c_pxa_handler(0, i2c);
		udelay(10);
	}

	i2c_pxa_stop_message(i2c);

	/*
	 * We place the return code in i2c->msg_idx.
	 */
	ret = i2c->msg_idx;

out:
	if (timeout == 0) {
		i2c_pxa_scream_blue_murder(i2c, "timeout (do_pio_xfer)");
		ret = I2C_RETRY;
	}

	return ret;
}

static int i2c_pxa_pio_xfer(struct i2c_adapter *adap,
			    struct i2c_msg msgs[], int num)
{
	struct pxa_i2c *i2c = adap->algo_data;

	/* If the I2C controller is disabled we need to reset it
	  (probably due to a suspend/resume destroying state). We do
	  this here as we can then avoid worrying about resuming the
	  controller before its users. */
	if (!(readl(_ICR(i2c)) & ICR_IUE))
		i2c_pxa_reset(i2c);

	return i2c_pxa_internal_xfer(i2c, msgs, num, i2c_pxa_do_pio_xfer);
}

static const struct i2c_algorithm i2c_pxa_pio_algorithm = {
	.master_xfer	= i2c_pxa_pio_xfer,
	.functionality	= i2c_pxa_functionality,
#ifdef CONFIG_I2C_PXA_SLAVE
	.reg_slave	= i2c_pxa_slave_reg,
	.unreg_slave	= i2c_pxa_slave_unreg,
#endif
};

static int i2c_pxa_probe_dt(struct platform_device *pdev, struct pxa_i2c *i2c,
			    enum pxa_i2c_types *i2c_types)
{
	struct device_node *np = pdev->dev.of_node;

	if (!pdev->dev.of_node)
		return 1;

	/* For device tree we always use the dynamic or alias-assigned ID */
	i2c->adap.nr = -1;

	i2c->use_pio = of_property_read_bool(np, "mrvl,i2c-polling");
	i2c->fast_mode = of_property_read_bool(np, "mrvl,i2c-fast-mode");

	*i2c_types = (enum pxa_i2c_types)device_get_match_data(&pdev->dev);

	return 0;
}

static int i2c_pxa_probe_pdata(struct platform_device *pdev,
			       struct pxa_i2c *i2c,
			       enum pxa_i2c_types *i2c_types)
{
	struct i2c_pxa_platform_data *plat = dev_get_platdata(&pdev->dev);
	const struct platform_device_id *id = platform_get_device_id(pdev);

	*i2c_types = id->driver_data;
	if (plat) {
		i2c->use_pio = plat->use_pio;
		i2c->fast_mode = plat->fast_mode;
		i2c->high_mode = plat->high_mode;
		i2c->master_code = plat->master_code;
		if (!i2c->master_code)
			i2c->master_code = 0xe;
		i2c->rate = plat->rate;
	}
	return 0;
}

static void i2c_pxa_prepare_recovery(struct i2c_adapter *adap)
{
	struct pxa_i2c *i2c = adap->algo_data;
	u32 ibmr = readl(_IBMR(i2c));

	/*
	 * Program the GPIOs to reflect the current I2C bus state while
	 * we transition to recovery; this avoids glitching the bus.
	 */
	gpiod_set_value(i2c->recovery.scl_gpiod, ibmr & IBMR_SCLS);
	gpiod_set_value(i2c->recovery.sda_gpiod, ibmr & IBMR_SDAS);
}

static void i2c_pxa_unprepare_recovery(struct i2c_adapter *adap)
{
	struct pxa_i2c *i2c = adap->algo_data;
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;
	u32 isr;

	/*
	 * The bus should now be free. Clear up the I2C controller before
	 * handing control of the bus back to avoid the bus changing state.
	 */
	isr = readl(_ISR(i2c));
	if (isr & (ISR_UB | ISR_IBB)) {
		dev_dbg(&i2c->adap.dev,
			"recovery: resetting controller, ISR=0x%08x\n", isr);
		i2c_pxa_do_reset(i2c);
	}

	WARN_ON(pinctrl_select_state(bri->pinctrl, bri->pins_default));

	dev_dbg(&i2c->adap.dev, "recovery: IBMR 0x%08x ISR 0x%08x\n",
	        readl(_IBMR(i2c)), readl(_ISR(i2c)));

	i2c_pxa_enable(i2c);
}

static int i2c_pxa_init_recovery(struct pxa_i2c *i2c)
{
	struct i2c_bus_recovery_info *bri = &i2c->recovery;
	struct device *dev = i2c->adap.dev.parent;

	/*
	 * When slave mode is enabled, we are not the only master on the bus.
	 * Bus recovery can only be performed when we are the master, which
	 * we can't be certain of. Therefore, when slave mode is enabled, do
	 * not configure bus recovery.
	 */
	if (IS_ENABLED(CONFIG_I2C_PXA_SLAVE))
		return 0;

	bri->pinctrl = devm_pinctrl_get(dev);
	if (PTR_ERR(bri->pinctrl) == -ENODEV) {
		bri->pinctrl = NULL;
		return 0;
	}
	if (IS_ERR(bri->pinctrl))
		return PTR_ERR(bri->pinctrl);

	bri->prepare_recovery = i2c_pxa_prepare_recovery;
	bri->unprepare_recovery = i2c_pxa_unprepare_recovery;

	i2c->adap.bus_recovery_info = bri;

	return 0;
}

static int i2c_pxa_probe(struct platform_device *dev)
{
	struct i2c_pxa_platform_data *plat = dev_get_platdata(&dev->dev);
	enum pxa_i2c_types i2c_type;
	struct pxa_i2c *i2c;
	struct resource *res;
	int ret, irq;

	i2c = devm_kzalloc(&dev->dev, sizeof(struct pxa_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	/* Default adapter num to device id; i2c_pxa_probe_dt can override. */
	i2c->adap.nr = dev->id;
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.retries = 5;
	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &dev->dev;
#ifdef CONFIG_OF
	i2c->adap.dev.of_node = dev->dev.of_node;
#endif

	i2c->reg_base = devm_platform_get_and_ioremap_resource(dev, 0, &res);
	if (IS_ERR(i2c->reg_base))
		return PTR_ERR(i2c->reg_base);

	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return irq;

	ret = i2c_pxa_init_recovery(i2c);
	if (ret)
		return ret;

	ret = i2c_pxa_probe_dt(dev, i2c, &i2c_type);
	if (ret > 0)
		ret = i2c_pxa_probe_pdata(dev, i2c, &i2c_type);
	if (ret < 0)
		return ret;

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	strscpy(i2c->adap.name, "pxa_i2c-i2c", sizeof(i2c->adap.name));

	i2c->clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(i2c->clk))
		return dev_err_probe(&dev->dev, PTR_ERR(i2c->clk),
				     "failed to get the clk\n");

	i2c->reg_ibmr = i2c->reg_base + pxa_reg_layout[i2c_type].ibmr;
	i2c->reg_idbr = i2c->reg_base + pxa_reg_layout[i2c_type].idbr;
	i2c->reg_icr = i2c->reg_base + pxa_reg_layout[i2c_type].icr;
	i2c->reg_isr = i2c->reg_base + pxa_reg_layout[i2c_type].isr;
	i2c->fm_mask = pxa_reg_layout[i2c_type].fm;
	i2c->hs_mask = pxa_reg_layout[i2c_type].hs;

	if (i2c_type != REGS_CE4100)
		i2c->reg_isar = i2c->reg_base + pxa_reg_layout[i2c_type].isar;

	if (i2c_type == REGS_PXA910) {
		i2c->reg_ilcr = i2c->reg_base + pxa_reg_layout[i2c_type].ilcr;
		i2c->reg_iwcr = i2c->reg_base + pxa_reg_layout[i2c_type].iwcr;
	}

	i2c->iobase = res->start;
	i2c->iosize = resource_size(res);

	i2c->irq = irq;

	i2c->slave_addr = I2C_PXA_SLAVE_ADDR;
	i2c->highmode_enter = false;

	if (plat) {
		i2c->adap.class = plat->class;
	}

	if (i2c->high_mode) {
		if (i2c->rate) {
			clk_set_rate(i2c->clk, i2c->rate);
			pr_info("i2c: <%s> set rate to %ld\n",
				i2c->adap.name, clk_get_rate(i2c->clk));
		} else
			pr_warn("i2c: <%s> clock rate not set\n",
				i2c->adap.name);
	}

	clk_prepare_enable(i2c->clk);

	if (i2c->use_pio) {
		i2c->adap.algo = &i2c_pxa_pio_algorithm;
	} else {
		i2c->adap.algo = &i2c_pxa_algorithm;
		ret = devm_request_irq(&dev->dev, irq, i2c_pxa_handler,
				IRQF_SHARED | IRQF_NO_SUSPEND,
				dev_name(&dev->dev), i2c);
		if (ret) {
			dev_err(&dev->dev, "failed to request irq: %d\n", ret);
			goto ereqirq;
		}
	}

	i2c_pxa_reset(i2c);

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0)
		goto ereqirq;

	platform_set_drvdata(dev, i2c);

#ifdef CONFIG_I2C_PXA_SLAVE
	dev_info(&i2c->adap.dev, " PXA I2C adapter, slave address %d\n",
		i2c->slave_addr);
#else
	dev_info(&i2c->adap.dev, " PXA I2C adapter\n");
#endif
	return 0;

ereqirq:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static void i2c_pxa_remove(struct platform_device *dev)
{
	struct pxa_i2c *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);

	clk_disable_unprepare(i2c->clk);
}

static int i2c_pxa_suspend_noirq(struct device *dev)
{
	struct pxa_i2c *i2c = dev_get_drvdata(dev);

	clk_disable(i2c->clk);

	return 0;
}

static int i2c_pxa_resume_noirq(struct device *dev)
{
	struct pxa_i2c *i2c = dev_get_drvdata(dev);

	clk_enable(i2c->clk);
	i2c_pxa_reset(i2c);

	return 0;
}

static const struct dev_pm_ops i2c_pxa_dev_pm_ops = {
	.suspend_noirq = i2c_pxa_suspend_noirq,
	.resume_noirq = i2c_pxa_resume_noirq,
};

static struct platform_driver i2c_pxa_driver = {
	.probe		= i2c_pxa_probe,
	.remove_new	= i2c_pxa_remove,
	.driver		= {
		.name	= "pxa2xx-i2c",
		.pm	= pm_sleep_ptr(&i2c_pxa_dev_pm_ops),
		.of_match_table = i2c_pxa_dt_ids,
	},
	.id_table	= i2c_pxa_id_table,
};

static int __init i2c_adap_pxa_init(void)
{
	return platform_driver_register(&i2c_pxa_driver);
}

static void __exit i2c_adap_pxa_exit(void)
{
	platform_driver_unregister(&i2c_pxa_driver);
}

MODULE_LICENSE("GPL");

subsys_initcall(i2c_adap_pxa_init);
module_exit(i2c_adap_pxa_exit);
