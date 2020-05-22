// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI OMAP I2C master mode driver
 *
 * Copyright (C) 2003 MontaVista Software, Inc.
 * Copyright (C) 2005 Nokia Corporation
 * Copyright (C) 2004 - 2007 Texas Instruments.
 *
 * Originally written by MontaVista Software, Inc.
 * Additional contributions by:
 *	Tony Lindgren <tony@atomide.com>
 *	Imre Deak <imre.deak@nokia.com>
 *	Juha Yrjölä <juha.yrjola@solidboot.com>
 *	Syed Khasim <x0khasim@ti.com>
 *	Nishant Menon <nm@ti.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/platform_data/i2c-omap.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>

/* I2C controller revisions */
#define OMAP_I2C_OMAP1_REV_2		0x20

/* I2C controller revisions present on specific hardware */
#define OMAP_I2C_REV_ON_2430		0x00000036
#define OMAP_I2C_REV_ON_3430_3530	0x0000003C
#define OMAP_I2C_REV_ON_3630		0x00000040
#define OMAP_I2C_REV_ON_4430_PLUS	0x50400002

/* timeout waiting for the controller to respond */
#define OMAP_I2C_TIMEOUT (msecs_to_jiffies(1000))

/* timeout for pm runtime autosuspend */
#define OMAP_I2C_PM_TIMEOUT		1000	/* ms */

/* timeout for making decision on bus free status */
#define OMAP_I2C_BUS_FREE_TIMEOUT (msecs_to_jiffies(10))

/* For OMAP3 I2C_IV has changed to I2C_WE (wakeup enable) */
enum {
	OMAP_I2C_REV_REG = 0,
	OMAP_I2C_IE_REG,
	OMAP_I2C_STAT_REG,
	OMAP_I2C_IV_REG,
	OMAP_I2C_WE_REG,
	OMAP_I2C_SYSS_REG,
	OMAP_I2C_BUF_REG,
	OMAP_I2C_CNT_REG,
	OMAP_I2C_DATA_REG,
	OMAP_I2C_SYSC_REG,
	OMAP_I2C_CON_REG,
	OMAP_I2C_OA_REG,
	OMAP_I2C_SA_REG,
	OMAP_I2C_PSC_REG,
	OMAP_I2C_SCLL_REG,
	OMAP_I2C_SCLH_REG,
	OMAP_I2C_SYSTEST_REG,
	OMAP_I2C_BUFSTAT_REG,
	/* only on OMAP4430 */
	OMAP_I2C_IP_V2_REVNB_LO,
	OMAP_I2C_IP_V2_REVNB_HI,
	OMAP_I2C_IP_V2_IRQSTATUS_RAW,
	OMAP_I2C_IP_V2_IRQENABLE_SET,
	OMAP_I2C_IP_V2_IRQENABLE_CLR,
};

/* I2C Interrupt Enable Register (OMAP_I2C_IE): */
#define OMAP_I2C_IE_XDR		(1 << 14)	/* TX Buffer drain int enable */
#define OMAP_I2C_IE_RDR		(1 << 13)	/* RX Buffer drain int enable */
#define OMAP_I2C_IE_XRDY	(1 << 4)	/* TX data ready int enable */
#define OMAP_I2C_IE_RRDY	(1 << 3)	/* RX data ready int enable */
#define OMAP_I2C_IE_ARDY	(1 << 2)	/* Access ready int enable */
#define OMAP_I2C_IE_NACK	(1 << 1)	/* No ack interrupt enable */
#define OMAP_I2C_IE_AL		(1 << 0)	/* Arbitration lost int ena */

/* I2C Status Register (OMAP_I2C_STAT): */
#define OMAP_I2C_STAT_XDR	(1 << 14)	/* TX Buffer draining */
#define OMAP_I2C_STAT_RDR	(1 << 13)	/* RX Buffer draining */
#define OMAP_I2C_STAT_BB	(1 << 12)	/* Bus busy */
#define OMAP_I2C_STAT_ROVR	(1 << 11)	/* Receive overrun */
#define OMAP_I2C_STAT_XUDF	(1 << 10)	/* Transmit underflow */
#define OMAP_I2C_STAT_AAS	(1 << 9)	/* Address as slave */
#define OMAP_I2C_STAT_BF	(1 << 8)	/* Bus Free */
#define OMAP_I2C_STAT_XRDY	(1 << 4)	/* Transmit data ready */
#define OMAP_I2C_STAT_RRDY	(1 << 3)	/* Receive data ready */
#define OMAP_I2C_STAT_ARDY	(1 << 2)	/* Register access ready */
#define OMAP_I2C_STAT_NACK	(1 << 1)	/* No ack interrupt enable */
#define OMAP_I2C_STAT_AL	(1 << 0)	/* Arbitration lost int ena */

/* I2C WE wakeup enable register */
#define OMAP_I2C_WE_XDR_WE	(1 << 14)	/* TX drain wakup */
#define OMAP_I2C_WE_RDR_WE	(1 << 13)	/* RX drain wakeup */
#define OMAP_I2C_WE_AAS_WE	(1 << 9)	/* Address as slave wakeup*/
#define OMAP_I2C_WE_BF_WE	(1 << 8)	/* Bus free wakeup */
#define OMAP_I2C_WE_STC_WE	(1 << 6)	/* Start condition wakeup */
#define OMAP_I2C_WE_GC_WE	(1 << 5)	/* General call wakeup */
#define OMAP_I2C_WE_DRDY_WE	(1 << 3)	/* TX/RX data ready wakeup */
#define OMAP_I2C_WE_ARDY_WE	(1 << 2)	/* Reg access ready wakeup */
#define OMAP_I2C_WE_NACK_WE	(1 << 1)	/* No acknowledgment wakeup */
#define OMAP_I2C_WE_AL_WE	(1 << 0)	/* Arbitration lost wakeup */

#define OMAP_I2C_WE_ALL		(OMAP_I2C_WE_XDR_WE | OMAP_I2C_WE_RDR_WE | \
				OMAP_I2C_WE_AAS_WE | OMAP_I2C_WE_BF_WE | \
				OMAP_I2C_WE_STC_WE | OMAP_I2C_WE_GC_WE | \
				OMAP_I2C_WE_DRDY_WE | OMAP_I2C_WE_ARDY_WE | \
				OMAP_I2C_WE_NACK_WE | OMAP_I2C_WE_AL_WE)

/* I2C Buffer Configuration Register (OMAP_I2C_BUF): */
#define OMAP_I2C_BUF_RDMA_EN	(1 << 15)	/* RX DMA channel enable */
#define OMAP_I2C_BUF_RXFIF_CLR	(1 << 14)	/* RX FIFO Clear */
#define OMAP_I2C_BUF_XDMA_EN	(1 << 7)	/* TX DMA channel enable */
#define OMAP_I2C_BUF_TXFIF_CLR	(1 << 6)	/* TX FIFO Clear */

/* I2C Configuration Register (OMAP_I2C_CON): */
#define OMAP_I2C_CON_EN		(1 << 15)	/* I2C module enable */
#define OMAP_I2C_CON_BE		(1 << 14)	/* Big endian mode */
#define OMAP_I2C_CON_OPMODE_HS	(1 << 12)	/* High Speed support */
#define OMAP_I2C_CON_STB	(1 << 11)	/* Start byte mode (master) */
#define OMAP_I2C_CON_MST	(1 << 10)	/* Master/slave mode */
#define OMAP_I2C_CON_TRX	(1 << 9)	/* TX/RX mode (master only) */
#define OMAP_I2C_CON_XA		(1 << 8)	/* Expand address */
#define OMAP_I2C_CON_RM		(1 << 2)	/* Repeat mode (master only) */
#define OMAP_I2C_CON_STP	(1 << 1)	/* Stop cond (master only) */
#define OMAP_I2C_CON_STT	(1 << 0)	/* Start condition (master) */

/* I2C SCL time value when Master */
#define OMAP_I2C_SCLL_HSSCLL	8
#define OMAP_I2C_SCLH_HSSCLH	8

/* I2C System Test Register (OMAP_I2C_SYSTEST): */
#define OMAP_I2C_SYSTEST_ST_EN		(1 << 15)	/* System test enable */
#define OMAP_I2C_SYSTEST_FREE		(1 << 14)	/* Free running mode */
#define OMAP_I2C_SYSTEST_TMODE_MASK	(3 << 12)	/* Test mode select */
#define OMAP_I2C_SYSTEST_TMODE_SHIFT	(12)		/* Test mode select */
/* Functional mode */
#define OMAP_I2C_SYSTEST_SCL_I_FUNC	(1 << 8)	/* SCL line input value */
#define OMAP_I2C_SYSTEST_SCL_O_FUNC	(1 << 7)	/* SCL line output value */
#define OMAP_I2C_SYSTEST_SDA_I_FUNC	(1 << 6)	/* SDA line input value */
#define OMAP_I2C_SYSTEST_SDA_O_FUNC	(1 << 5)	/* SDA line output value */
/* SDA/SCL IO mode */
#define OMAP_I2C_SYSTEST_SCL_I		(1 << 3)	/* SCL line sense in */
#define OMAP_I2C_SYSTEST_SCL_O		(1 << 2)	/* SCL line drive out */
#define OMAP_I2C_SYSTEST_SDA_I		(1 << 1)	/* SDA line sense in */
#define OMAP_I2C_SYSTEST_SDA_O		(1 << 0)	/* SDA line drive out */

/* OCP_SYSSTATUS bit definitions */
#define SYSS_RESETDONE_MASK		(1 << 0)

/* OCP_SYSCONFIG bit definitions */
#define SYSC_CLOCKACTIVITY_MASK		(0x3 << 8)
#define SYSC_SIDLEMODE_MASK		(0x3 << 3)
#define SYSC_ENAWAKEUP_MASK		(1 << 2)
#define SYSC_SOFTRESET_MASK		(1 << 1)
#define SYSC_AUTOIDLE_MASK		(1 << 0)

#define SYSC_IDLEMODE_SMART		0x2
#define SYSC_CLOCKACTIVITY_FCLK		0x2

/* Errata definitions */
#define I2C_OMAP_ERRATA_I207		(1 << 0)
#define I2C_OMAP_ERRATA_I462		(1 << 1)

#define OMAP_I2C_IP_V2_INTERRUPTS_MASK	0x6FFF

struct omap_i2c_dev {
	struct device		*dev;
	void __iomem		*base;		/* virtual */
	int			irq;
	int			reg_shift;      /* bit shift for I2C register addresses */
	struct completion	cmd_complete;
	struct resource		*ioarea;
	u32			latency;	/* maximum mpu wkup latency */
	void			(*set_mpu_wkup_lat)(struct device *dev,
						    long latency);
	u32			speed;		/* Speed of bus in kHz */
	u32			flags;
	u16			scheme;
	u16			cmd_err;
	u8			*buf;
	u8			*regs;
	size_t			buf_len;
	struct i2c_adapter	adapter;
	u8			threshold;
	u8			fifo_size;	/* use as flag and value
						 * fifo_size==0 implies no fifo
						 * if set, should be trsh+1
						 */
	u32			rev;
	unsigned		b_hw:1;		/* bad h/w fixes */
	unsigned		bb_valid:1;	/* true when BB-bit reflects
						 * the I2C bus state
						 */
	unsigned		receiver:1;	/* true when we're in receiver mode */
	u16			iestate;	/* Saved interrupt register */
	u16			pscstate;
	u16			scllstate;
	u16			sclhstate;
	u16			syscstate;
	u16			westate;
	u16			errata;
};

static const u8 reg_map_ip_v1[] = {
	[OMAP_I2C_REV_REG] = 0x00,
	[OMAP_I2C_IE_REG] = 0x01,
	[OMAP_I2C_STAT_REG] = 0x02,
	[OMAP_I2C_IV_REG] = 0x03,
	[OMAP_I2C_WE_REG] = 0x03,
	[OMAP_I2C_SYSS_REG] = 0x04,
	[OMAP_I2C_BUF_REG] = 0x05,
	[OMAP_I2C_CNT_REG] = 0x06,
	[OMAP_I2C_DATA_REG] = 0x07,
	[OMAP_I2C_SYSC_REG] = 0x08,
	[OMAP_I2C_CON_REG] = 0x09,
	[OMAP_I2C_OA_REG] = 0x0a,
	[OMAP_I2C_SA_REG] = 0x0b,
	[OMAP_I2C_PSC_REG] = 0x0c,
	[OMAP_I2C_SCLL_REG] = 0x0d,
	[OMAP_I2C_SCLH_REG] = 0x0e,
	[OMAP_I2C_SYSTEST_REG] = 0x0f,
	[OMAP_I2C_BUFSTAT_REG] = 0x10,
};

static const u8 reg_map_ip_v2[] = {
	[OMAP_I2C_REV_REG] = 0x04,
	[OMAP_I2C_IE_REG] = 0x2c,
	[OMAP_I2C_STAT_REG] = 0x28,
	[OMAP_I2C_IV_REG] = 0x34,
	[OMAP_I2C_WE_REG] = 0x34,
	[OMAP_I2C_SYSS_REG] = 0x90,
	[OMAP_I2C_BUF_REG] = 0x94,
	[OMAP_I2C_CNT_REG] = 0x98,
	[OMAP_I2C_DATA_REG] = 0x9c,
	[OMAP_I2C_SYSC_REG] = 0x10,
	[OMAP_I2C_CON_REG] = 0xa4,
	[OMAP_I2C_OA_REG] = 0xa8,
	[OMAP_I2C_SA_REG] = 0xac,
	[OMAP_I2C_PSC_REG] = 0xb0,
	[OMAP_I2C_SCLL_REG] = 0xb4,
	[OMAP_I2C_SCLH_REG] = 0xb8,
	[OMAP_I2C_SYSTEST_REG] = 0xbC,
	[OMAP_I2C_BUFSTAT_REG] = 0xc0,
	[OMAP_I2C_IP_V2_REVNB_LO] = 0x00,
	[OMAP_I2C_IP_V2_REVNB_HI] = 0x04,
	[OMAP_I2C_IP_V2_IRQSTATUS_RAW] = 0x24,
	[OMAP_I2C_IP_V2_IRQENABLE_SET] = 0x2c,
	[OMAP_I2C_IP_V2_IRQENABLE_CLR] = 0x30,
};

static int omap_i2c_xfer_data(struct omap_i2c_dev *omap);

static inline void omap_i2c_write_reg(struct omap_i2c_dev *omap,
				      int reg, u16 val)
{
	writew_relaxed(val, omap->base +
			(omap->regs[reg] << omap->reg_shift));
}

static inline u16 omap_i2c_read_reg(struct omap_i2c_dev *omap, int reg)
{
	return readw_relaxed(omap->base +
				(omap->regs[reg] << omap->reg_shift));
}

static void __omap_i2c_init(struct omap_i2c_dev *omap)
{

	omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, 0);

	/* Setup clock prescaler to obtain approx 12MHz I2C module clock: */
	omap_i2c_write_reg(omap, OMAP_I2C_PSC_REG, omap->pscstate);

	/* SCL low and high time values */
	omap_i2c_write_reg(omap, OMAP_I2C_SCLL_REG, omap->scllstate);
	omap_i2c_write_reg(omap, OMAP_I2C_SCLH_REG, omap->sclhstate);
	if (omap->rev >= OMAP_I2C_REV_ON_3430_3530)
		omap_i2c_write_reg(omap, OMAP_I2C_WE_REG, omap->westate);

	/* Take the I2C module out of reset: */
	omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);

	/*
	 * NOTE: right after setting CON_EN, STAT_BB could be 0 while the
	 * bus is busy. It will be changed to 1 on the next IP FCLK clock.
	 * udelay(1) will be enough to fix that.
	 */

	/*
	 * Don't write to this register if the IE state is 0 as it can
	 * cause deadlock.
	 */
	if (omap->iestate)
		omap_i2c_write_reg(omap, OMAP_I2C_IE_REG, omap->iestate);
}

static int omap_i2c_reset(struct omap_i2c_dev *omap)
{
	unsigned long timeout;
	u16 sysc;

	if (omap->rev >= OMAP_I2C_OMAP1_REV_2) {
		sysc = omap_i2c_read_reg(omap, OMAP_I2C_SYSC_REG);

		/* Disable I2C controller before soft reset */
		omap_i2c_write_reg(omap, OMAP_I2C_CON_REG,
			omap_i2c_read_reg(omap, OMAP_I2C_CON_REG) &
				~(OMAP_I2C_CON_EN));

		omap_i2c_write_reg(omap, OMAP_I2C_SYSC_REG, SYSC_SOFTRESET_MASK);
		/* For some reason we need to set the EN bit before the
		 * reset done bit gets set. */
		timeout = jiffies + OMAP_I2C_TIMEOUT;
		omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);
		while (!(omap_i2c_read_reg(omap, OMAP_I2C_SYSS_REG) &
			 SYSS_RESETDONE_MASK)) {
			if (time_after(jiffies, timeout)) {
				dev_warn(omap->dev, "timeout waiting "
						"for controller reset\n");
				return -ETIMEDOUT;
			}
			msleep(1);
		}

		/* SYSC register is cleared by the reset; rewrite it */
		omap_i2c_write_reg(omap, OMAP_I2C_SYSC_REG, sysc);

		if (omap->rev > OMAP_I2C_REV_ON_3430_3530) {
			/* Schedule I2C-bus monitoring on the next transfer */
			omap->bb_valid = 0;
		}
	}

	return 0;
}

static int omap_i2c_init(struct omap_i2c_dev *omap)
{
	u16 psc = 0, scll = 0, sclh = 0;
	u16 fsscll = 0, fssclh = 0, hsscll = 0, hssclh = 0;
	unsigned long fclk_rate = 12000000;
	unsigned long internal_clk = 0;
	struct clk *fclk;
	int error;

	if (omap->rev >= OMAP_I2C_REV_ON_3430_3530) {
		/*
		 * Enabling all wakup sources to stop I2C freezing on
		 * WFI instruction.
		 * REVISIT: Some wkup sources might not be needed.
		 */
		omap->westate = OMAP_I2C_WE_ALL;
	}

	if (omap->flags & OMAP_I2C_FLAG_ALWAYS_ARMXOR_CLK) {
		/*
		 * The I2C functional clock is the armxor_ck, so there's
		 * no need to get "armxor_ck" separately.  Now, if OMAP2420
		 * always returns 12MHz for the functional clock, we can
		 * do this bit unconditionally.
		 */
		fclk = clk_get(omap->dev, "fck");
		if (IS_ERR(fclk)) {
			error = PTR_ERR(fclk);
			dev_err(omap->dev, "could not get fck: %i\n", error);

			return error;
		}

		fclk_rate = clk_get_rate(fclk);
		clk_put(fclk);

		/* TRM for 5912 says the I2C clock must be prescaled to be
		 * between 7 - 12 MHz. The XOR input clock is typically
		 * 12, 13 or 19.2 MHz. So we should have code that produces:
		 *
		 * XOR MHz	Divider		Prescaler
		 * 12		1		0
		 * 13		2		1
		 * 19.2		2		1
		 */
		if (fclk_rate > 12000000)
			psc = fclk_rate / 12000000;
	}

	if (!(omap->flags & OMAP_I2C_FLAG_SIMPLE_CLOCK)) {

		/*
		 * HSI2C controller internal clk rate should be 19.2 Mhz for
		 * HS and for all modes on 2430. On 34xx we can use lower rate
		 * to get longer filter period for better noise suppression.
		 * The filter is iclk (fclk for HS) period.
		 */
		if (omap->speed > 400 ||
			       omap->flags & OMAP_I2C_FLAG_FORCE_19200_INT_CLK)
			internal_clk = 19200;
		else if (omap->speed > 100)
			internal_clk = 9600;
		else
			internal_clk = 4000;
		fclk = clk_get(omap->dev, "fck");
		if (IS_ERR(fclk)) {
			error = PTR_ERR(fclk);
			dev_err(omap->dev, "could not get fck: %i\n", error);

			return error;
		}
		fclk_rate = clk_get_rate(fclk) / 1000;
		clk_put(fclk);

		/* Compute prescaler divisor */
		psc = fclk_rate / internal_clk;
		psc = psc - 1;

		/* If configured for High Speed */
		if (omap->speed > 400) {
			unsigned long scl;

			/* For first phase of HS mode */
			scl = internal_clk / 400;
			fsscll = scl - (scl / 3) - 7;
			fssclh = (scl / 3) - 5;

			/* For second phase of HS mode */
			scl = fclk_rate / omap->speed;
			hsscll = scl - (scl / 3) - 7;
			hssclh = (scl / 3) - 5;
		} else if (omap->speed > 100) {
			unsigned long scl;

			/* Fast mode */
			scl = internal_clk / omap->speed;
			fsscll = scl - (scl / 3) - 7;
			fssclh = (scl / 3) - 5;
		} else {
			/* Standard mode */
			fsscll = internal_clk / (omap->speed * 2) - 7;
			fssclh = internal_clk / (omap->speed * 2) - 5;
		}
		scll = (hsscll << OMAP_I2C_SCLL_HSSCLL) | fsscll;
		sclh = (hssclh << OMAP_I2C_SCLH_HSSCLH) | fssclh;
	} else {
		/* Program desired operating rate */
		fclk_rate /= (psc + 1) * 1000;
		if (psc > 2)
			psc = 2;
		scll = fclk_rate / (omap->speed * 2) - 7 + psc;
		sclh = fclk_rate / (omap->speed * 2) - 7 + psc;
	}

	omap->iestate = (OMAP_I2C_IE_XRDY | OMAP_I2C_IE_RRDY |
			OMAP_I2C_IE_ARDY | OMAP_I2C_IE_NACK |
			OMAP_I2C_IE_AL)  | ((omap->fifo_size) ?
				(OMAP_I2C_IE_RDR | OMAP_I2C_IE_XDR) : 0);

	omap->pscstate = psc;
	omap->scllstate = scll;
	omap->sclhstate = sclh;

	if (omap->rev <= OMAP_I2C_REV_ON_3430_3530) {
		/* Not implemented */
		omap->bb_valid = 1;
	}

	__omap_i2c_init(omap);

	return 0;
}

/*
 * Try bus recovery, but only if SDA is actually low.
 */
static int omap_i2c_recover_bus(struct omap_i2c_dev *omap)
{
	u16 systest;

	systest = omap_i2c_read_reg(omap, OMAP_I2C_SYSTEST_REG);
	if ((systest & OMAP_I2C_SYSTEST_SCL_I_FUNC) &&
	    (systest & OMAP_I2C_SYSTEST_SDA_I_FUNC))
		return 0; /* bus seems to already be fine */
	if (!(systest & OMAP_I2C_SYSTEST_SCL_I_FUNC))
		return -EBUSY; /* recovery would not fix SCL */
	return i2c_recover_bus(&omap->adapter);
}

/*
 * Waiting on Bus Busy
 */
static int omap_i2c_wait_for_bb(struct omap_i2c_dev *omap)
{
	unsigned long timeout;

	timeout = jiffies + OMAP_I2C_TIMEOUT;
	while (omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG) & OMAP_I2C_STAT_BB) {
		if (time_after(jiffies, timeout))
			return omap_i2c_recover_bus(omap);
		msleep(1);
	}

	return 0;
}

/*
 * Wait while BB-bit doesn't reflect the I2C bus state
 *
 * In a multimaster environment, after IP software reset, BB-bit value doesn't
 * correspond to the current bus state. It may happen what BB-bit will be 0,
 * while the bus is busy due to another I2C master activity.
 * Here are BB-bit values after reset:
 *     SDA   SCL   BB   NOTES
 *       0     0    0   1, 2
 *       1     0    0   1, 2
 *       0     1    1
 *       1     1    0   3
 * Later, if IP detect SDA=0 and SCL=1 (ACK) or SDA 1->0 while SCL=1 (START)
 * combinations on the bus, it set BB-bit to 1.
 * If IP detect SDA 0->1 while SCL=1 (STOP) combination on the bus,
 * it set BB-bit to 0 and BF to 1.
 * BB and BF bits correctly tracks the bus state while IP is suspended
 * BB bit became valid on the next FCLK clock after CON_EN bit set
 *
 * NOTES:
 * 1. Any transfer started when BB=0 and bus is busy wouldn't be
 *    completed by IP and results in controller timeout.
 * 2. Any transfer started when BB=0 and SCL=0 results in IP
 *    starting to drive SDA low. In that case IP corrupt data
 *    on the bus.
 * 3. Any transfer started in the middle of another master's transfer
 *    results in unpredictable results and data corruption
 */
static int omap_i2c_wait_for_bb_valid(struct omap_i2c_dev *omap)
{
	unsigned long bus_free_timeout = 0;
	unsigned long timeout;
	int bus_free = 0;
	u16 stat, systest;

	if (omap->bb_valid)
		return 0;

	timeout = jiffies + OMAP_I2C_TIMEOUT;
	while (1) {
		stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
		/*
		 * We will see BB or BF event in a case IP had detected any
		 * activity on the I2C bus. Now IP correctly tracks the bus
		 * state. BB-bit value is valid.
		 */
		if (stat & (OMAP_I2C_STAT_BB | OMAP_I2C_STAT_BF))
			break;

		/*
		 * Otherwise, we must look signals on the bus to make
		 * the right decision.
		 */
		systest = omap_i2c_read_reg(omap, OMAP_I2C_SYSTEST_REG);
		if ((systest & OMAP_I2C_SYSTEST_SCL_I_FUNC) &&
		    (systest & OMAP_I2C_SYSTEST_SDA_I_FUNC)) {
			if (!bus_free) {
				bus_free_timeout = jiffies +
					OMAP_I2C_BUS_FREE_TIMEOUT;
				bus_free = 1;
			}

			/*
			 * SDA and SCL lines was high for 10 ms without bus
			 * activity detected. The bus is free. Consider
			 * BB-bit value is valid.
			 */
			if (time_after(jiffies, bus_free_timeout))
				break;
		} else {
			bus_free = 0;
		}

		if (time_after(jiffies, timeout)) {
			/*
			 * SDA or SCL were low for the entire timeout without
			 * any activity detected. Most likely, a slave is
			 * locking up the bus with no master driving the clock.
			 */
			dev_warn(omap->dev, "timeout waiting for bus ready\n");
			return omap_i2c_recover_bus(omap);
		}

		msleep(1);
	}

	omap->bb_valid = 1;
	return 0;
}

static void omap_i2c_resize_fifo(struct omap_i2c_dev *omap, u8 size, bool is_rx)
{
	u16		buf;

	if (omap->flags & OMAP_I2C_FLAG_NO_FIFO)
		return;

	/*
	 * Set up notification threshold based on message size. We're doing
	 * this to try and avoid draining feature as much as possible. Whenever
	 * we have big messages to transfer (bigger than our total fifo size)
	 * then we might use draining feature to transfer the remaining bytes.
	 */

	omap->threshold = clamp(size, (u8) 1, omap->fifo_size);

	buf = omap_i2c_read_reg(omap, OMAP_I2C_BUF_REG);

	if (is_rx) {
		/* Clear RX Threshold */
		buf &= ~(0x3f << 8);
		buf |= ((omap->threshold - 1) << 8) | OMAP_I2C_BUF_RXFIF_CLR;
	} else {
		/* Clear TX Threshold */
		buf &= ~0x3f;
		buf |= (omap->threshold - 1) | OMAP_I2C_BUF_TXFIF_CLR;
	}

	omap_i2c_write_reg(omap, OMAP_I2C_BUF_REG, buf);

	if (omap->rev < OMAP_I2C_REV_ON_3630)
		omap->b_hw = 1; /* Enable hardware fixes */

	/* calculate wakeup latency constraint for MPU */
	if (omap->set_mpu_wkup_lat != NULL)
		omap->latency = (1000000 * omap->threshold) /
			(1000 * omap->speed / 8);
}

static void omap_i2c_wait(struct omap_i2c_dev *omap)
{
	u16 stat;
	u16 mask = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);
	int count = 0;

	do {
		stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
		count++;
	} while (!(stat & mask) && count < 5);
}

/*
 * Low level master read/write transaction.
 */
static int omap_i2c_xfer_msg(struct i2c_adapter *adap,
			     struct i2c_msg *msg, int stop, bool polling)
{
	struct omap_i2c_dev *omap = i2c_get_adapdata(adap);
	unsigned long timeout;
	u16 w;
	int ret;

	dev_dbg(omap->dev, "addr: 0x%04x, len: %d, flags: 0x%x, stop: %d\n",
		msg->addr, msg->len, msg->flags, stop);

	omap->receiver = !!(msg->flags & I2C_M_RD);
	omap_i2c_resize_fifo(omap, msg->len, omap->receiver);

	omap_i2c_write_reg(omap, OMAP_I2C_SA_REG, msg->addr);

	/* REVISIT: Could the STB bit of I2C_CON be used with probing? */
	omap->buf = msg->buf;
	omap->buf_len = msg->len;

	/* make sure writes to omap->buf_len are ordered */
	barrier();

	omap_i2c_write_reg(omap, OMAP_I2C_CNT_REG, omap->buf_len);

	/* Clear the FIFO Buffers */
	w = omap_i2c_read_reg(omap, OMAP_I2C_BUF_REG);
	w |= OMAP_I2C_BUF_RXFIF_CLR | OMAP_I2C_BUF_TXFIF_CLR;
	omap_i2c_write_reg(omap, OMAP_I2C_BUF_REG, w);

	if (!polling)
		reinit_completion(&omap->cmd_complete);
	omap->cmd_err = 0;

	w = OMAP_I2C_CON_EN | OMAP_I2C_CON_MST | OMAP_I2C_CON_STT;

	/* High speed configuration */
	if (omap->speed > 400)
		w |= OMAP_I2C_CON_OPMODE_HS;

	if (msg->flags & I2C_M_STOP)
		stop = 1;
	if (msg->flags & I2C_M_TEN)
		w |= OMAP_I2C_CON_XA;
	if (!(msg->flags & I2C_M_RD))
		w |= OMAP_I2C_CON_TRX;

	if (!omap->b_hw && stop)
		w |= OMAP_I2C_CON_STP;
	/*
	 * NOTE: STAT_BB bit could became 1 here if another master occupy
	 * the bus. IP successfully complete transfer when the bus will be
	 * free again (BB reset to 0).
	 */
	omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, w);

	/*
	 * Don't write stt and stp together on some hardware.
	 */
	if (omap->b_hw && stop) {
		unsigned long delay = jiffies + OMAP_I2C_TIMEOUT;
		u16 con = omap_i2c_read_reg(omap, OMAP_I2C_CON_REG);
		while (con & OMAP_I2C_CON_STT) {
			con = omap_i2c_read_reg(omap, OMAP_I2C_CON_REG);

			/* Let the user know if i2c is in a bad state */
			if (time_after(jiffies, delay)) {
				dev_err(omap->dev, "controller timed out "
				"waiting for start condition to finish\n");
				return -ETIMEDOUT;
			}
			cpu_relax();
		}

		w |= OMAP_I2C_CON_STP;
		w &= ~OMAP_I2C_CON_STT;
		omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, w);
	}

	/*
	 * REVISIT: We should abort the transfer on signals, but the bus goes
	 * into arbitration and we're currently unable to recover from it.
	 */
	if (!polling) {
		timeout = wait_for_completion_timeout(&omap->cmd_complete,
						      OMAP_I2C_TIMEOUT);
	} else {
		do {
			omap_i2c_wait(omap);
			ret = omap_i2c_xfer_data(omap);
		} while (ret == -EAGAIN);

		timeout = !ret;
	}

	if (timeout == 0) {
		dev_err(omap->dev, "controller timed out\n");
		omap_i2c_reset(omap);
		__omap_i2c_init(omap);
		return -ETIMEDOUT;
	}

	if (likely(!omap->cmd_err))
		return 0;

	/* We have an error */
	if (omap->cmd_err & (OMAP_I2C_STAT_ROVR | OMAP_I2C_STAT_XUDF)) {
		omap_i2c_reset(omap);
		__omap_i2c_init(omap);
		return -EIO;
	}

	if (omap->cmd_err & OMAP_I2C_STAT_AL)
		return -EAGAIN;

	if (omap->cmd_err & OMAP_I2C_STAT_NACK) {
		if (msg->flags & I2C_M_IGNORE_NAK)
			return 0;

		w = omap_i2c_read_reg(omap, OMAP_I2C_CON_REG);
		w |= OMAP_I2C_CON_STP;
		omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, w);
		return -EREMOTEIO;
	}
	return -EIO;
}


/*
 * Prepare controller for a transaction and call omap_i2c_xfer_msg
 * to do the work during IRQ processing.
 */
static int
omap_i2c_xfer_common(struct i2c_adapter *adap, struct i2c_msg msgs[], int num,
		     bool polling)
{
	struct omap_i2c_dev *omap = i2c_get_adapdata(adap);
	int i;
	int r;

	r = pm_runtime_get_sync(omap->dev);
	if (r < 0)
		goto out;

	r = omap_i2c_wait_for_bb_valid(omap);
	if (r < 0)
		goto out;

	r = omap_i2c_wait_for_bb(omap);
	if (r < 0)
		goto out;

	if (omap->set_mpu_wkup_lat != NULL)
		omap->set_mpu_wkup_lat(omap->dev, omap->latency);

	for (i = 0; i < num; i++) {
		r = omap_i2c_xfer_msg(adap, &msgs[i], (i == (num - 1)),
				      polling);
		if (r != 0)
			break;
	}

	if (r == 0)
		r = num;

	omap_i2c_wait_for_bb(omap);

	if (omap->set_mpu_wkup_lat != NULL)
		omap->set_mpu_wkup_lat(omap->dev, -1);

out:
	pm_runtime_mark_last_busy(omap->dev);
	pm_runtime_put_autosuspend(omap->dev);
	return r;
}

static int
omap_i2c_xfer_irq(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	return omap_i2c_xfer_common(adap, msgs, num, false);
}

static int
omap_i2c_xfer_polling(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	return omap_i2c_xfer_common(adap, msgs, num, true);
}

static u32
omap_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
	       I2C_FUNC_PROTOCOL_MANGLING;
}

static inline void
omap_i2c_complete_cmd(struct omap_i2c_dev *omap, u16 err)
{
	omap->cmd_err |= err;
	complete(&omap->cmd_complete);
}

static inline void
omap_i2c_ack_stat(struct omap_i2c_dev *omap, u16 stat)
{
	omap_i2c_write_reg(omap, OMAP_I2C_STAT_REG, stat);
}

static inline void i2c_omap_errata_i207(struct omap_i2c_dev *omap, u16 stat)
{
	/*
	 * I2C Errata(Errata Nos. OMAP2: 1.67, OMAP3: 1.8)
	 * Not applicable for OMAP4.
	 * Under certain rare conditions, RDR could be set again
	 * when the bus is busy, then ignore the interrupt and
	 * clear the interrupt.
	 */
	if (stat & OMAP_I2C_STAT_RDR) {
		/* Step 1: If RDR is set, clear it */
		omap_i2c_ack_stat(omap, OMAP_I2C_STAT_RDR);

		/* Step 2: */
		if (!(omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG)
						& OMAP_I2C_STAT_BB)) {

			/* Step 3: */
			if (omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG)
						& OMAP_I2C_STAT_RDR) {
				omap_i2c_ack_stat(omap, OMAP_I2C_STAT_RDR);
				dev_dbg(omap->dev, "RDR when bus is busy.\n");
			}

		}
	}
}

/* rev1 devices are apparently only on some 15xx */
#ifdef CONFIG_ARCH_OMAP15XX

static irqreturn_t
omap_i2c_omap1_isr(int this_irq, void *dev_id)
{
	struct omap_i2c_dev *omap = dev_id;
	u16 iv, w;

	if (pm_runtime_suspended(omap->dev))
		return IRQ_NONE;

	iv = omap_i2c_read_reg(omap, OMAP_I2C_IV_REG);
	switch (iv) {
	case 0x00:	/* None */
		break;
	case 0x01:	/* Arbitration lost */
		dev_err(omap->dev, "Arbitration lost\n");
		omap_i2c_complete_cmd(omap, OMAP_I2C_STAT_AL);
		break;
	case 0x02:	/* No acknowledgement */
		omap_i2c_complete_cmd(omap, OMAP_I2C_STAT_NACK);
		omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, OMAP_I2C_CON_STP);
		break;
	case 0x03:	/* Register access ready */
		omap_i2c_complete_cmd(omap, 0);
		break;
	case 0x04:	/* Receive data ready */
		if (omap->buf_len) {
			w = omap_i2c_read_reg(omap, OMAP_I2C_DATA_REG);
			*omap->buf++ = w;
			omap->buf_len--;
			if (omap->buf_len) {
				*omap->buf++ = w >> 8;
				omap->buf_len--;
			}
		} else
			dev_err(omap->dev, "RRDY IRQ while no data requested\n");
		break;
	case 0x05:	/* Transmit data ready */
		if (omap->buf_len) {
			w = *omap->buf++;
			omap->buf_len--;
			if (omap->buf_len) {
				w |= *omap->buf++ << 8;
				omap->buf_len--;
			}
			omap_i2c_write_reg(omap, OMAP_I2C_DATA_REG, w);
		} else
			dev_err(omap->dev, "XRDY IRQ while no data to send\n");
		break;
	default:
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}
#else
#define omap_i2c_omap1_isr		NULL
#endif

/*
 * OMAP3430 Errata i462: When an XRDY/XDR is hit, wait for XUDF before writing
 * data to DATA_REG. Otherwise some data bytes can be lost while transferring
 * them from the memory to the I2C interface.
 */
static int errata_omap3_i462(struct omap_i2c_dev *omap)
{
	unsigned long timeout = 10000;
	u16 stat;

	do {
		stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
		if (stat & OMAP_I2C_STAT_XUDF)
			break;

		if (stat & (OMAP_I2C_STAT_NACK | OMAP_I2C_STAT_AL)) {
			omap_i2c_ack_stat(omap, (OMAP_I2C_STAT_XRDY |
							OMAP_I2C_STAT_XDR));
			if (stat & OMAP_I2C_STAT_NACK) {
				omap->cmd_err |= OMAP_I2C_STAT_NACK;
				omap_i2c_ack_stat(omap, OMAP_I2C_STAT_NACK);
			}

			if (stat & OMAP_I2C_STAT_AL) {
				dev_err(omap->dev, "Arbitration lost\n");
				omap->cmd_err |= OMAP_I2C_STAT_AL;
				omap_i2c_ack_stat(omap, OMAP_I2C_STAT_AL);
			}

			return -EIO;
		}

		cpu_relax();
	} while (--timeout);

	if (!timeout) {
		dev_err(omap->dev, "timeout waiting on XUDF bit\n");
		return 0;
	}

	return 0;
}

static void omap_i2c_receive_data(struct omap_i2c_dev *omap, u8 num_bytes,
		bool is_rdr)
{
	u16		w;

	while (num_bytes--) {
		w = omap_i2c_read_reg(omap, OMAP_I2C_DATA_REG);
		*omap->buf++ = w;
		omap->buf_len--;

		/*
		 * Data reg in 2430, omap3 and
		 * omap4 is 8 bit wide
		 */
		if (omap->flags & OMAP_I2C_FLAG_16BIT_DATA_REG) {
			*omap->buf++ = w >> 8;
			omap->buf_len--;
		}
	}
}

static int omap_i2c_transmit_data(struct omap_i2c_dev *omap, u8 num_bytes,
		bool is_xdr)
{
	u16		w;

	while (num_bytes--) {
		w = *omap->buf++;
		omap->buf_len--;

		/*
		 * Data reg in 2430, omap3 and
		 * omap4 is 8 bit wide
		 */
		if (omap->flags & OMAP_I2C_FLAG_16BIT_DATA_REG) {
			w |= *omap->buf++ << 8;
			omap->buf_len--;
		}

		if (omap->errata & I2C_OMAP_ERRATA_I462) {
			int ret;

			ret = errata_omap3_i462(omap);
			if (ret < 0)
				return ret;
		}

		omap_i2c_write_reg(omap, OMAP_I2C_DATA_REG, w);
	}

	return 0;
}

static irqreturn_t
omap_i2c_isr(int irq, void *dev_id)
{
	struct omap_i2c_dev *omap = dev_id;
	irqreturn_t ret = IRQ_HANDLED;
	u16 mask;
	u16 stat;

	stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
	mask = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);

	if (stat & mask)
		ret = IRQ_WAKE_THREAD;

	return ret;
}

static int omap_i2c_xfer_data(struct omap_i2c_dev *omap)
{
	u16 bits;
	u16 stat;
	int err = 0, count = 0;

	do {
		bits = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);
		stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
		stat &= bits;

		/* If we're in receiver mode, ignore XDR/XRDY */
		if (omap->receiver)
			stat &= ~(OMAP_I2C_STAT_XDR | OMAP_I2C_STAT_XRDY);
		else
			stat &= ~(OMAP_I2C_STAT_RDR | OMAP_I2C_STAT_RRDY);

		if (!stat) {
			/* my work here is done */
			err = -EAGAIN;
			break;
		}

		dev_dbg(omap->dev, "IRQ (ISR = 0x%04x)\n", stat);
		if (count++ == 100) {
			dev_warn(omap->dev, "Too much work in one IRQ\n");
			break;
		}

		if (stat & OMAP_I2C_STAT_NACK) {
			err |= OMAP_I2C_STAT_NACK;
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_NACK);
		}

		if (stat & OMAP_I2C_STAT_AL) {
			dev_err(omap->dev, "Arbitration lost\n");
			err |= OMAP_I2C_STAT_AL;
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_AL);
		}

		/*
		 * ProDB0017052: Clear ARDY bit twice
		 */
		if (stat & OMAP_I2C_STAT_ARDY)
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_ARDY);

		if (stat & (OMAP_I2C_STAT_ARDY | OMAP_I2C_STAT_NACK |
					OMAP_I2C_STAT_AL)) {
			omap_i2c_ack_stat(omap, (OMAP_I2C_STAT_RRDY |
						OMAP_I2C_STAT_RDR |
						OMAP_I2C_STAT_XRDY |
						OMAP_I2C_STAT_XDR |
						OMAP_I2C_STAT_ARDY));
			break;
		}

		if (stat & OMAP_I2C_STAT_RDR) {
			u8 num_bytes = 1;

			if (omap->fifo_size)
				num_bytes = omap->buf_len;

			if (omap->errata & I2C_OMAP_ERRATA_I207) {
				i2c_omap_errata_i207(omap, stat);
				num_bytes = (omap_i2c_read_reg(omap,
					OMAP_I2C_BUFSTAT_REG) >> 8) & 0x3F;
			}

			omap_i2c_receive_data(omap, num_bytes, true);
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_RDR);
			continue;
		}

		if (stat & OMAP_I2C_STAT_RRDY) {
			u8 num_bytes = 1;

			if (omap->threshold)
				num_bytes = omap->threshold;

			omap_i2c_receive_data(omap, num_bytes, false);
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_RRDY);
			continue;
		}

		if (stat & OMAP_I2C_STAT_XDR) {
			u8 num_bytes = 1;
			int ret;

			if (omap->fifo_size)
				num_bytes = omap->buf_len;

			ret = omap_i2c_transmit_data(omap, num_bytes, true);
			if (ret < 0)
				break;

			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_XDR);
			continue;
		}

		if (stat & OMAP_I2C_STAT_XRDY) {
			u8 num_bytes = 1;
			int ret;

			if (omap->threshold)
				num_bytes = omap->threshold;

			ret = omap_i2c_transmit_data(omap, num_bytes, false);
			if (ret < 0)
				break;

			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_XRDY);
			continue;
		}

		if (stat & OMAP_I2C_STAT_ROVR) {
			dev_err(omap->dev, "Receive overrun\n");
			err |= OMAP_I2C_STAT_ROVR;
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_ROVR);
			break;
		}

		if (stat & OMAP_I2C_STAT_XUDF) {
			dev_err(omap->dev, "Transmit underflow\n");
			err |= OMAP_I2C_STAT_XUDF;
			omap_i2c_ack_stat(omap, OMAP_I2C_STAT_XUDF);
			break;
		}
	} while (stat);

	return err;
}

static irqreturn_t
omap_i2c_isr_thread(int this_irq, void *dev_id)
{
	int ret;
	struct omap_i2c_dev *omap = dev_id;

	ret = omap_i2c_xfer_data(omap);
	if (ret != -EAGAIN)
		omap_i2c_complete_cmd(omap, ret);

	return IRQ_HANDLED;
}

static const struct i2c_algorithm omap_i2c_algo = {
	.master_xfer	= omap_i2c_xfer_irq,
	.master_xfer_atomic	= omap_i2c_xfer_polling,
	.functionality	= omap_i2c_func,
};

static const struct i2c_adapter_quirks omap_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

#ifdef CONFIG_OF
static struct omap_i2c_bus_platform_data omap2420_pdata = {
	.rev = OMAP_I2C_IP_VERSION_1,
	.flags = OMAP_I2C_FLAG_NO_FIFO |
			OMAP_I2C_FLAG_SIMPLE_CLOCK |
			OMAP_I2C_FLAG_16BIT_DATA_REG |
			OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_i2c_bus_platform_data omap2430_pdata = {
	.rev = OMAP_I2C_IP_VERSION_1,
	.flags = OMAP_I2C_FLAG_BUS_SHIFT_2 |
			OMAP_I2C_FLAG_FORCE_19200_INT_CLK,
};

static struct omap_i2c_bus_platform_data omap3_pdata = {
	.rev = OMAP_I2C_IP_VERSION_1,
	.flags = OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_i2c_bus_platform_data omap4_pdata = {
	.rev = OMAP_I2C_IP_VERSION_2,
};

static const struct of_device_id omap_i2c_of_match[] = {
	{
		.compatible = "ti,omap4-i2c",
		.data = &omap4_pdata,
	},
	{
		.compatible = "ti,omap3-i2c",
		.data = &omap3_pdata,
	},
	{
		.compatible = "ti,omap2430-i2c",
		.data = &omap2430_pdata,
	},
	{
		.compatible = "ti,omap2420-i2c",
		.data = &omap2420_pdata,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, omap_i2c_of_match);
#endif

#define OMAP_I2C_SCHEME(rev)		((rev & 0xc000) >> 14)

#define OMAP_I2C_REV_SCHEME_0_MAJOR(rev) (rev >> 4)
#define OMAP_I2C_REV_SCHEME_0_MINOR(rev) (rev & 0xf)

#define OMAP_I2C_REV_SCHEME_1_MAJOR(rev) ((rev & 0x0700) >> 7)
#define OMAP_I2C_REV_SCHEME_1_MINOR(rev) (rev & 0x1f)
#define OMAP_I2C_SCHEME_0		0
#define OMAP_I2C_SCHEME_1		1

static int omap_i2c_get_scl(struct i2c_adapter *adap)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	u32 reg;

	reg = omap_i2c_read_reg(dev, OMAP_I2C_SYSTEST_REG);

	return reg & OMAP_I2C_SYSTEST_SCL_I_FUNC;
}

static int omap_i2c_get_sda(struct i2c_adapter *adap)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	u32 reg;

	reg = omap_i2c_read_reg(dev, OMAP_I2C_SYSTEST_REG);

	return reg & OMAP_I2C_SYSTEST_SDA_I_FUNC;
}

static void omap_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	u32 reg;

	reg = omap_i2c_read_reg(dev, OMAP_I2C_SYSTEST_REG);
	if (val)
		reg |= OMAP_I2C_SYSTEST_SCL_O;
	else
		reg &= ~OMAP_I2C_SYSTEST_SCL_O;
	omap_i2c_write_reg(dev, OMAP_I2C_SYSTEST_REG, reg);
}

static void omap_i2c_prepare_recovery(struct i2c_adapter *adap)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	u32 reg;

	reg = omap_i2c_read_reg(dev, OMAP_I2C_SYSTEST_REG);
	/* enable test mode */
	reg |= OMAP_I2C_SYSTEST_ST_EN;
	/* select SDA/SCL IO mode */
	reg |= 3 << OMAP_I2C_SYSTEST_TMODE_SHIFT;
	/* set SCL to high-impedance state (reset value is 0) */
	reg |= OMAP_I2C_SYSTEST_SCL_O;
	/* set SDA to high-impedance state (reset value is 0) */
	reg |= OMAP_I2C_SYSTEST_SDA_O;
	omap_i2c_write_reg(dev, OMAP_I2C_SYSTEST_REG, reg);
}

static void omap_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	u32 reg;

	reg = omap_i2c_read_reg(dev, OMAP_I2C_SYSTEST_REG);
	/* restore reset values */
	reg &= ~OMAP_I2C_SYSTEST_ST_EN;
	reg &= ~OMAP_I2C_SYSTEST_TMODE_MASK;
	reg &= ~OMAP_I2C_SYSTEST_SCL_O;
	reg &= ~OMAP_I2C_SYSTEST_SDA_O;
	omap_i2c_write_reg(dev, OMAP_I2C_SYSTEST_REG, reg);
}

static struct i2c_bus_recovery_info omap_i2c_bus_recovery_info = {
	.get_scl		= omap_i2c_get_scl,
	.get_sda		= omap_i2c_get_sda,
	.set_scl		= omap_i2c_set_scl,
	.prepare_recovery	= omap_i2c_prepare_recovery,
	.unprepare_recovery	= omap_i2c_unprepare_recovery,
	.recover_bus		= i2c_generic_scl_recovery,
};

static int
omap_i2c_probe(struct platform_device *pdev)
{
	struct omap_i2c_dev	*omap;
	struct i2c_adapter	*adap;
	const struct omap_i2c_bus_platform_data *pdata =
		dev_get_platdata(&pdev->dev);
	struct device_node	*node = pdev->dev.of_node;
	const struct of_device_id *match;
	int irq;
	int r;
	u32 rev;
	u16 minor, major;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	omap = devm_kzalloc(&pdev->dev, sizeof(struct omap_i2c_dev), GFP_KERNEL);
	if (!omap)
		return -ENOMEM;

	omap->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(omap->base))
		return PTR_ERR(omap->base);

	match = of_match_device(of_match_ptr(omap_i2c_of_match), &pdev->dev);
	if (match) {
		u32 freq = I2C_MAX_STANDARD_MODE_FREQ;

		pdata = match->data;
		omap->flags = pdata->flags;

		of_property_read_u32(node, "clock-frequency", &freq);
		/* convert DT freq value in Hz into kHz for speed */
		omap->speed = freq / 1000;
	} else if (pdata != NULL) {
		omap->speed = pdata->clkrate;
		omap->flags = pdata->flags;
		omap->set_mpu_wkup_lat = pdata->set_mpu_wkup_lat;
	}

	omap->dev = &pdev->dev;
	omap->irq = irq;

	platform_set_drvdata(pdev, omap);
	init_completion(&omap->cmd_complete);

	omap->reg_shift = (omap->flags >> OMAP_I2C_FLAG_BUS_SHIFT__SHIFT) & 3;

	pm_runtime_enable(omap->dev);
	pm_runtime_set_autosuspend_delay(omap->dev, OMAP_I2C_PM_TIMEOUT);
	pm_runtime_use_autosuspend(omap->dev);

	r = pm_runtime_get_sync(omap->dev);
	if (r < 0)
		goto err_free_mem;

	/*
	 * Read the Rev hi bit-[15:14] ie scheme this is 1 indicates ver2.
	 * On omap1/3/2 Offset 4 is IE Reg the bit [15:14] is 0 at reset.
	 * Also since the omap_i2c_read_reg uses reg_map_ip_* a
	 * readw_relaxed is done.
	 */
	rev = readw_relaxed(omap->base + 0x04);

	omap->scheme = OMAP_I2C_SCHEME(rev);
	switch (omap->scheme) {
	case OMAP_I2C_SCHEME_0:
		omap->regs = (u8 *)reg_map_ip_v1;
		omap->rev = omap_i2c_read_reg(omap, OMAP_I2C_REV_REG);
		minor = OMAP_I2C_REV_SCHEME_0_MAJOR(omap->rev);
		major = OMAP_I2C_REV_SCHEME_0_MAJOR(omap->rev);
		break;
	case OMAP_I2C_SCHEME_1:
		/* FALLTHROUGH */
	default:
		omap->regs = (u8 *)reg_map_ip_v2;
		rev = (rev << 16) |
			omap_i2c_read_reg(omap, OMAP_I2C_IP_V2_REVNB_LO);
		minor = OMAP_I2C_REV_SCHEME_1_MINOR(rev);
		major = OMAP_I2C_REV_SCHEME_1_MAJOR(rev);
		omap->rev = rev;
	}

	omap->errata = 0;

	if (omap->rev >= OMAP_I2C_REV_ON_2430 &&
			omap->rev < OMAP_I2C_REV_ON_4430_PLUS)
		omap->errata |= I2C_OMAP_ERRATA_I207;

	if (omap->rev <= OMAP_I2C_REV_ON_3430_3530)
		omap->errata |= I2C_OMAP_ERRATA_I462;

	if (!(omap->flags & OMAP_I2C_FLAG_NO_FIFO)) {
		u16 s;

		/* Set up the fifo size - Get total size */
		s = (omap_i2c_read_reg(omap, OMAP_I2C_BUFSTAT_REG) >> 14) & 0x3;
		omap->fifo_size = 0x8 << s;

		/*
		 * Set up notification threshold as half the total available
		 * size. This is to ensure that we can handle the status on int
		 * call back latencies.
		 */

		omap->fifo_size = (omap->fifo_size / 2);

		if (omap->rev < OMAP_I2C_REV_ON_3630)
			omap->b_hw = 1; /* Enable hardware fixes */

		/* calculate wakeup latency constraint for MPU */
		if (omap->set_mpu_wkup_lat != NULL)
			omap->latency = (1000000 * omap->fifo_size) /
				       (1000 * omap->speed / 8);
	}

	/* reset ASAP, clearing any IRQs */
	omap_i2c_init(omap);

	if (omap->rev < OMAP_I2C_OMAP1_REV_2)
		r = devm_request_irq(&pdev->dev, omap->irq, omap_i2c_omap1_isr,
				IRQF_NO_SUSPEND, pdev->name, omap);
	else
		r = devm_request_threaded_irq(&pdev->dev, omap->irq,
				omap_i2c_isr, omap_i2c_isr_thread,
				IRQF_NO_SUSPEND | IRQF_ONESHOT,
				pdev->name, omap);

	if (r) {
		dev_err(omap->dev, "failure requesting irq %i\n", omap->irq);
		goto err_unuse_clocks;
	}

	adap = &omap->adapter;
	i2c_set_adapdata(adap, omap);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DEPRECATED;
	strlcpy(adap->name, "OMAP I2C adapter", sizeof(adap->name));
	adap->algo = &omap_i2c_algo;
	adap->quirks = &omap_i2c_quirks;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;
	adap->bus_recovery_info = &omap_i2c_bus_recovery_info;

	/* i2c device drivers may be active on return from add_adapter() */
	adap->nr = pdev->id;
	r = i2c_add_numbered_adapter(adap);
	if (r)
		goto err_unuse_clocks;

	dev_info(omap->dev, "bus %d rev%d.%d at %d kHz\n", adap->nr,
		 major, minor, omap->speed);

	pm_runtime_mark_last_busy(omap->dev);
	pm_runtime_put_autosuspend(omap->dev);

	return 0;

err_unuse_clocks:
	omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, 0);
	pm_runtime_dont_use_autosuspend(omap->dev);
	pm_runtime_put_sync(omap->dev);
	pm_runtime_disable(&pdev->dev);
err_free_mem:

	return r;
}

static int omap_i2c_remove(struct platform_device *pdev)
{
	struct omap_i2c_dev	*omap = platform_get_drvdata(pdev);
	int ret;

	i2c_del_adapter(&omap->adapter);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		return ret;

	omap_i2c_write_reg(omap, OMAP_I2C_CON_REG, 0);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused omap_i2c_runtime_suspend(struct device *dev)
{
	struct omap_i2c_dev *omap = dev_get_drvdata(dev);

	omap->iestate = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);

	if (omap->scheme == OMAP_I2C_SCHEME_0)
		omap_i2c_write_reg(omap, OMAP_I2C_IE_REG, 0);
	else
		omap_i2c_write_reg(omap, OMAP_I2C_IP_V2_IRQENABLE_CLR,
				   OMAP_I2C_IP_V2_INTERRUPTS_MASK);

	if (omap->rev < OMAP_I2C_OMAP1_REV_2) {
		omap_i2c_read_reg(omap, OMAP_I2C_IV_REG); /* Read clears */
	} else {
		omap_i2c_write_reg(omap, OMAP_I2C_STAT_REG, omap->iestate);

		/* Flush posted write */
		omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
	}

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused omap_i2c_runtime_resume(struct device *dev)
{
	struct omap_i2c_dev *omap = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);

	if (!omap->regs)
		return 0;

	__omap_i2c_init(omap);

	return 0;
}

static const struct dev_pm_ops omap_i2c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(omap_i2c_runtime_suspend,
			   omap_i2c_runtime_resume, NULL)
};

static struct platform_driver omap_i2c_driver = {
	.probe		= omap_i2c_probe,
	.remove		= omap_i2c_remove,
	.driver		= {
		.name	= "omap_i2c",
		.pm	= &omap_i2c_pm_ops,
		.of_match_table = of_match_ptr(omap_i2c_of_match),
	},
};

/* I2C may be needed to bring up other drivers */
static int __init
omap_i2c_init_driver(void)
{
	return platform_driver_register(&omap_i2c_driver);
}
subsys_initcall(omap_i2c_init_driver);

static void __exit omap_i2c_exit_driver(void)
{
	platform_driver_unregister(&omap_i2c_driver);
}
module_exit(omap_i2c_exit_driver);

MODULE_AUTHOR("MontaVista Software, Inc. (and others)");
MODULE_DESCRIPTION("TI OMAP I2C bus adapter");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap_i2c");
