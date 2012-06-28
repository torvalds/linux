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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
#include <linux/of_i2c.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/i2c-omap.h>
#include <linux/pm_runtime.h>

/* I2C controller revisions */
#define OMAP_I2C_OMAP1_REV_2		0x20

/* I2C controller revisions present on specific hardware */
#define OMAP_I2C_REV_ON_2430		0x36
#define OMAP_I2C_REV_ON_3430_3530	0x3C
#define OMAP_I2C_REV_ON_3630_4430	0x40

/* timeout waiting for the controller to respond */
#define OMAP_I2C_TIMEOUT (msecs_to_jiffies(1000))

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
#define OMAP_I2C_STAT_AD0	(1 << 8)	/* Address zero */
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
#ifdef DEBUG
#define OMAP_I2C_SYSTEST_ST_EN		(1 << 15)	/* System test enable */
#define OMAP_I2C_SYSTEST_FREE		(1 << 14)	/* Free running mode */
#define OMAP_I2C_SYSTEST_TMODE_MASK	(3 << 12)	/* Test mode select */
#define OMAP_I2C_SYSTEST_TMODE_SHIFT	(12)		/* Test mode select */
#define OMAP_I2C_SYSTEST_SCL_I		(1 << 3)	/* SCL line sense in */
#define OMAP_I2C_SYSTEST_SCL_O		(1 << 2)	/* SCL line drive out */
#define OMAP_I2C_SYSTEST_SDA_I		(1 << 1)	/* SDA line sense in */
#define OMAP_I2C_SYSTEST_SDA_O		(1 << 0)	/* SDA line drive out */
#endif

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
	u32			dtrev;		/* extra revision from DT */
	u32			flags;
	u16			cmd_err;
	u8			*buf;
	u8			*regs;
	size_t			buf_len;
	struct i2c_adapter	adapter;
	u8			fifo_size;	/* use as flag and value
						 * fifo_size==0 implies no fifo
						 * if set, should be trsh+1
						 */
	u8			rev;
	unsigned		b_hw:1;		/* bad h/w fixes */
	u16			iestate;	/* Saved interrupt register */
	u16			pscstate;
	u16			scllstate;
	u16			sclhstate;
	u16			bufstate;
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

static inline void omap_i2c_write_reg(struct omap_i2c_dev *i2c_dev,
				      int reg, u16 val)
{
	__raw_writew(val, i2c_dev->base +
			(i2c_dev->regs[reg] << i2c_dev->reg_shift));
}

static inline u16 omap_i2c_read_reg(struct omap_i2c_dev *i2c_dev, int reg)
{
	return __raw_readw(i2c_dev->base +
				(i2c_dev->regs[reg] << i2c_dev->reg_shift));
}

static int omap_i2c_init(struct omap_i2c_dev *dev)
{
	u16 psc = 0, scll = 0, sclh = 0, buf = 0;
	u16 fsscll = 0, fssclh = 0, hsscll = 0, hssclh = 0;
	unsigned long fclk_rate = 12000000;
	unsigned long timeout;
	unsigned long internal_clk = 0;
	struct clk *fclk;

	if (dev->rev >= OMAP_I2C_OMAP1_REV_2) {
		/* Disable I2C controller before soft reset */
		omap_i2c_write_reg(dev, OMAP_I2C_CON_REG,
			omap_i2c_read_reg(dev, OMAP_I2C_CON_REG) &
				~(OMAP_I2C_CON_EN));

		omap_i2c_write_reg(dev, OMAP_I2C_SYSC_REG, SYSC_SOFTRESET_MASK);
		/* For some reason we need to set the EN bit before the
		 * reset done bit gets set. */
		timeout = jiffies + OMAP_I2C_TIMEOUT;
		omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);
		while (!(omap_i2c_read_reg(dev, OMAP_I2C_SYSS_REG) &
			 SYSS_RESETDONE_MASK)) {
			if (time_after(jiffies, timeout)) {
				dev_warn(dev->dev, "timeout waiting "
						"for controller reset\n");
				return -ETIMEDOUT;
			}
			msleep(1);
		}

		/* SYSC register is cleared by the reset; rewrite it */
		if (dev->rev == OMAP_I2C_REV_ON_2430) {

			omap_i2c_write_reg(dev, OMAP_I2C_SYSC_REG,
					   SYSC_AUTOIDLE_MASK);

		} else if (dev->rev >= OMAP_I2C_REV_ON_3430_3530) {
			dev->syscstate = SYSC_AUTOIDLE_MASK;
			dev->syscstate |= SYSC_ENAWAKEUP_MASK;
			dev->syscstate |= (SYSC_IDLEMODE_SMART <<
			      __ffs(SYSC_SIDLEMODE_MASK));
			dev->syscstate |= (SYSC_CLOCKACTIVITY_FCLK <<
			      __ffs(SYSC_CLOCKACTIVITY_MASK));

			omap_i2c_write_reg(dev, OMAP_I2C_SYSC_REG,
							dev->syscstate);
			/*
			 * Enabling all wakup sources to stop I2C freezing on
			 * WFI instruction.
			 * REVISIT: Some wkup sources might not be needed.
			 */
			dev->westate = OMAP_I2C_WE_ALL;
			omap_i2c_write_reg(dev, OMAP_I2C_WE_REG,
							dev->westate);
		}
	}
	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, 0);

	if (dev->flags & OMAP_I2C_FLAG_ALWAYS_ARMXOR_CLK) {
		/*
		 * The I2C functional clock is the armxor_ck, so there's
		 * no need to get "armxor_ck" separately.  Now, if OMAP2420
		 * always returns 12MHz for the functional clock, we can
		 * do this bit unconditionally.
		 */
		fclk = clk_get(dev->dev, "fck");
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

	if (!(dev->flags & OMAP_I2C_FLAG_SIMPLE_CLOCK)) {

		/*
		 * HSI2C controller internal clk rate should be 19.2 Mhz for
		 * HS and for all modes on 2430. On 34xx we can use lower rate
		 * to get longer filter period for better noise suppression.
		 * The filter is iclk (fclk for HS) period.
		 */
		if (dev->speed > 400 ||
			       dev->flags & OMAP_I2C_FLAG_FORCE_19200_INT_CLK)
			internal_clk = 19200;
		else if (dev->speed > 100)
			internal_clk = 9600;
		else
			internal_clk = 4000;
		fclk = clk_get(dev->dev, "fck");
		fclk_rate = clk_get_rate(fclk) / 1000;
		clk_put(fclk);

		/* Compute prescaler divisor */
		psc = fclk_rate / internal_clk;
		psc = psc - 1;

		/* If configured for High Speed */
		if (dev->speed > 400) {
			unsigned long scl;

			/* For first phase of HS mode */
			scl = internal_clk / 400;
			fsscll = scl - (scl / 3) - 7;
			fssclh = (scl / 3) - 5;

			/* For second phase of HS mode */
			scl = fclk_rate / dev->speed;
			hsscll = scl - (scl / 3) - 7;
			hssclh = (scl / 3) - 5;
		} else if (dev->speed > 100) {
			unsigned long scl;

			/* Fast mode */
			scl = internal_clk / dev->speed;
			fsscll = scl - (scl / 3) - 7;
			fssclh = (scl / 3) - 5;
		} else {
			/* Standard mode */
			fsscll = internal_clk / (dev->speed * 2) - 7;
			fssclh = internal_clk / (dev->speed * 2) - 5;
		}
		scll = (hsscll << OMAP_I2C_SCLL_HSSCLL) | fsscll;
		sclh = (hssclh << OMAP_I2C_SCLH_HSSCLH) | fssclh;
	} else {
		/* Program desired operating rate */
		fclk_rate /= (psc + 1) * 1000;
		if (psc > 2)
			psc = 2;
		scll = fclk_rate / (dev->speed * 2) - 7 + psc;
		sclh = fclk_rate / (dev->speed * 2) - 7 + psc;
	}

	/* Setup clock prescaler to obtain approx 12MHz I2C module clock: */
	omap_i2c_write_reg(dev, OMAP_I2C_PSC_REG, psc);

	/* SCL low and high time values */
	omap_i2c_write_reg(dev, OMAP_I2C_SCLL_REG, scll);
	omap_i2c_write_reg(dev, OMAP_I2C_SCLH_REG, sclh);

	if (dev->fifo_size) {
		/* Note: setup required fifo size - 1. RTRSH and XTRSH */
		buf = (dev->fifo_size - 1) << 8 | OMAP_I2C_BUF_RXFIF_CLR |
			(dev->fifo_size - 1) | OMAP_I2C_BUF_TXFIF_CLR;
		omap_i2c_write_reg(dev, OMAP_I2C_BUF_REG, buf);
	}

	/* Take the I2C module out of reset: */
	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);

	/* Enable interrupts */
	dev->iestate = (OMAP_I2C_IE_XRDY | OMAP_I2C_IE_RRDY |
			OMAP_I2C_IE_ARDY | OMAP_I2C_IE_NACK |
			OMAP_I2C_IE_AL)  | ((dev->fifo_size) ?
				(OMAP_I2C_IE_RDR | OMAP_I2C_IE_XDR) : 0);
	omap_i2c_write_reg(dev, OMAP_I2C_IE_REG, dev->iestate);
	if (dev->flags & OMAP_I2C_FLAG_RESET_REGS_POSTIDLE) {
		dev->pscstate = psc;
		dev->scllstate = scll;
		dev->sclhstate = sclh;
		dev->bufstate = buf;
	}
	return 0;
}

/*
 * Waiting on Bus Busy
 */
static int omap_i2c_wait_for_bb(struct omap_i2c_dev *dev)
{
	unsigned long timeout;

	timeout = jiffies + OMAP_I2C_TIMEOUT;
	while (omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG) & OMAP_I2C_STAT_BB) {
		if (time_after(jiffies, timeout)) {
			dev_warn(dev->dev, "timeout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	return 0;
}

/*
 * Low level master read/write transaction.
 */
static int omap_i2c_xfer_msg(struct i2c_adapter *adap,
			     struct i2c_msg *msg, int stop)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	unsigned long timeout;
	u16 w;

	dev_dbg(dev->dev, "addr: 0x%04x, len: %d, flags: 0x%x, stop: %d\n",
		msg->addr, msg->len, msg->flags, stop);

	if (msg->len == 0)
		return -EINVAL;

	omap_i2c_write_reg(dev, OMAP_I2C_SA_REG, msg->addr);

	/* REVISIT: Could the STB bit of I2C_CON be used with probing? */
	dev->buf = msg->buf;
	dev->buf_len = msg->len;

	omap_i2c_write_reg(dev, OMAP_I2C_CNT_REG, dev->buf_len);

	/* Clear the FIFO Buffers */
	w = omap_i2c_read_reg(dev, OMAP_I2C_BUF_REG);
	w |= OMAP_I2C_BUF_RXFIF_CLR | OMAP_I2C_BUF_TXFIF_CLR;
	omap_i2c_write_reg(dev, OMAP_I2C_BUF_REG, w);

	INIT_COMPLETION(dev->cmd_complete);
	dev->cmd_err = 0;

	w = OMAP_I2C_CON_EN | OMAP_I2C_CON_MST | OMAP_I2C_CON_STT;

	/* High speed configuration */
	if (dev->speed > 400)
		w |= OMAP_I2C_CON_OPMODE_HS;

	if (msg->flags & I2C_M_TEN)
		w |= OMAP_I2C_CON_XA;
	if (!(msg->flags & I2C_M_RD))
		w |= OMAP_I2C_CON_TRX;

	if (!dev->b_hw && stop)
		w |= OMAP_I2C_CON_STP;

	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, w);

	/*
	 * Don't write stt and stp together on some hardware.
	 */
	if (dev->b_hw && stop) {
		unsigned long delay = jiffies + OMAP_I2C_TIMEOUT;
		u16 con = omap_i2c_read_reg(dev, OMAP_I2C_CON_REG);
		while (con & OMAP_I2C_CON_STT) {
			con = omap_i2c_read_reg(dev, OMAP_I2C_CON_REG);

			/* Let the user know if i2c is in a bad state */
			if (time_after(jiffies, delay)) {
				dev_err(dev->dev, "controller timed out "
				"waiting for start condition to finish\n");
				return -ETIMEDOUT;
			}
			cpu_relax();
		}

		w |= OMAP_I2C_CON_STP;
		w &= ~OMAP_I2C_CON_STT;
		omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, w);
	}

	/*
	 * REVISIT: We should abort the transfer on signals, but the bus goes
	 * into arbitration and we're currently unable to recover from it.
	 */
	timeout = wait_for_completion_timeout(&dev->cmd_complete,
						OMAP_I2C_TIMEOUT);
	dev->buf_len = 0;
	if (timeout == 0) {
		dev_err(dev->dev, "controller timed out\n");
		omap_i2c_init(dev);
		return -ETIMEDOUT;
	}

	if (likely(!dev->cmd_err))
		return 0;

	/* We have an error */
	if (dev->cmd_err & (OMAP_I2C_STAT_AL | OMAP_I2C_STAT_ROVR |
			    OMAP_I2C_STAT_XUDF)) {
		omap_i2c_init(dev);
		return -EIO;
	}

	if (dev->cmd_err & OMAP_I2C_STAT_NACK) {
		if (msg->flags & I2C_M_IGNORE_NAK)
			return 0;
		if (stop) {
			w = omap_i2c_read_reg(dev, OMAP_I2C_CON_REG);
			w |= OMAP_I2C_CON_STP;
			omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, w);
		}
		return -EREMOTEIO;
	}
	return -EIO;
}


/*
 * Prepare controller for a transaction and call omap_i2c_xfer_msg
 * to do the work during IRQ processing.
 */
static int
omap_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct omap_i2c_dev *dev = i2c_get_adapdata(adap);
	int i;
	int r;

	r = pm_runtime_get_sync(dev->dev);
	if (IS_ERR_VALUE(r))
		return r;

	r = omap_i2c_wait_for_bb(dev);
	if (r < 0)
		goto out;

	if (dev->set_mpu_wkup_lat != NULL)
		dev->set_mpu_wkup_lat(dev->dev, dev->latency);

	for (i = 0; i < num; i++) {
		r = omap_i2c_xfer_msg(adap, &msgs[i], (i == (num - 1)));
		if (r != 0)
			break;
	}

	if (dev->set_mpu_wkup_lat != NULL)
		dev->set_mpu_wkup_lat(dev->dev, -1);

	if (r == 0)
		r = num;

	omap_i2c_wait_for_bb(dev);
out:
	pm_runtime_put(dev->dev);
	return r;
}

static u32
omap_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static inline void
omap_i2c_complete_cmd(struct omap_i2c_dev *dev, u16 err)
{
	dev->cmd_err |= err;
	complete(&dev->cmd_complete);
}

static inline void
omap_i2c_ack_stat(struct omap_i2c_dev *dev, u16 stat)
{
	omap_i2c_write_reg(dev, OMAP_I2C_STAT_REG, stat);
}

static inline void i2c_omap_errata_i207(struct omap_i2c_dev *dev, u16 stat)
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
		omap_i2c_ack_stat(dev, OMAP_I2C_STAT_RDR);

		/* Step 2: */
		if (!(omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG)
						& OMAP_I2C_STAT_BB)) {

			/* Step 3: */
			if (omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG)
						& OMAP_I2C_STAT_RDR) {
				omap_i2c_ack_stat(dev, OMAP_I2C_STAT_RDR);
				dev_dbg(dev->dev, "RDR when bus is busy.\n");
			}

		}
	}
}

/* rev1 devices are apparently only on some 15xx */
#ifdef CONFIG_ARCH_OMAP15XX

static irqreturn_t
omap_i2c_omap1_isr(int this_irq, void *dev_id)
{
	struct omap_i2c_dev *dev = dev_id;
	u16 iv, w;

	if (pm_runtime_suspended(dev->dev))
		return IRQ_NONE;

	iv = omap_i2c_read_reg(dev, OMAP_I2C_IV_REG);
	switch (iv) {
	case 0x00:	/* None */
		break;
	case 0x01:	/* Arbitration lost */
		dev_err(dev->dev, "Arbitration lost\n");
		omap_i2c_complete_cmd(dev, OMAP_I2C_STAT_AL);
		break;
	case 0x02:	/* No acknowledgement */
		omap_i2c_complete_cmd(dev, OMAP_I2C_STAT_NACK);
		omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, OMAP_I2C_CON_STP);
		break;
	case 0x03:	/* Register access ready */
		omap_i2c_complete_cmd(dev, 0);
		break;
	case 0x04:	/* Receive data ready */
		if (dev->buf_len) {
			w = omap_i2c_read_reg(dev, OMAP_I2C_DATA_REG);
			*dev->buf++ = w;
			dev->buf_len--;
			if (dev->buf_len) {
				*dev->buf++ = w >> 8;
				dev->buf_len--;
			}
		} else
			dev_err(dev->dev, "RRDY IRQ while no data requested\n");
		break;
	case 0x05:	/* Transmit data ready */
		if (dev->buf_len) {
			w = *dev->buf++;
			dev->buf_len--;
			if (dev->buf_len) {
				w |= *dev->buf++ << 8;
				dev->buf_len--;
			}
			omap_i2c_write_reg(dev, OMAP_I2C_DATA_REG, w);
		} else
			dev_err(dev->dev, "XRDY IRQ while no data to send\n");
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
static int errata_omap3_i462(struct omap_i2c_dev *dev, u16 *stat, int *err)
{
	unsigned long timeout = 10000;

	while (--timeout && !(*stat & OMAP_I2C_STAT_XUDF)) {
		if (*stat & (OMAP_I2C_STAT_NACK | OMAP_I2C_STAT_AL)) {
			omap_i2c_ack_stat(dev, *stat & (OMAP_I2C_STAT_XRDY |
							OMAP_I2C_STAT_XDR));
			return -ETIMEDOUT;
		}

		cpu_relax();
		*stat = omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG);
	}

	if (!timeout) {
		dev_err(dev->dev, "timeout waiting on XUDF bit\n");
		return 0;
	}

	*err |= OMAP_I2C_STAT_XUDF;
	return 0;
}

static irqreturn_t
omap_i2c_isr(int this_irq, void *dev_id)
{
	struct omap_i2c_dev *dev = dev_id;
	u16 bits;
	u16 stat, w;
	int err, count = 0;

	if (pm_runtime_suspended(dev->dev))
		return IRQ_NONE;

	bits = omap_i2c_read_reg(dev, OMAP_I2C_IE_REG);
	while ((stat = (omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG))) & bits) {
		dev_dbg(dev->dev, "IRQ (ISR = 0x%04x)\n", stat);
		if (count++ == 100) {
			dev_warn(dev->dev, "Too much work in one IRQ\n");
			break;
		}

		err = 0;
complete:
		/*
		 * Ack the stat in one go, but [R/X]DR and [R/X]RDY should be
		 * acked after the data operation is complete.
		 * Ref: TRM SWPU114Q Figure 18-31
		 */
		omap_i2c_write_reg(dev, OMAP_I2C_STAT_REG, stat &
				~(OMAP_I2C_STAT_RRDY | OMAP_I2C_STAT_RDR |
				OMAP_I2C_STAT_XRDY | OMAP_I2C_STAT_XDR));

		if (stat & OMAP_I2C_STAT_NACK)
			err |= OMAP_I2C_STAT_NACK;

		if (stat & OMAP_I2C_STAT_AL) {
			dev_err(dev->dev, "Arbitration lost\n");
			err |= OMAP_I2C_STAT_AL;
		}
		/*
		 * ProDB0017052: Clear ARDY bit twice
		 */
		if (stat & (OMAP_I2C_STAT_ARDY | OMAP_I2C_STAT_NACK |
					OMAP_I2C_STAT_AL)) {
			omap_i2c_ack_stat(dev, stat &
				(OMAP_I2C_STAT_RRDY | OMAP_I2C_STAT_RDR |
				OMAP_I2C_STAT_XRDY | OMAP_I2C_STAT_XDR |
				OMAP_I2C_STAT_ARDY));
			omap_i2c_complete_cmd(dev, err);
			return IRQ_HANDLED;
		}
		if (stat & (OMAP_I2C_STAT_RRDY | OMAP_I2C_STAT_RDR)) {
			u8 num_bytes = 1;

			if (dev->errata & I2C_OMAP_ERRATA_I207)
				i2c_omap_errata_i207(dev, stat);

			if (dev->fifo_size) {
				if (stat & OMAP_I2C_STAT_RRDY)
					num_bytes = dev->fifo_size;
				else    /* read RXSTAT on RDR interrupt */
					num_bytes = (omap_i2c_read_reg(dev,
							OMAP_I2C_BUFSTAT_REG)
							>> 8) & 0x3F;
			}
			while (num_bytes) {
				num_bytes--;
				w = omap_i2c_read_reg(dev, OMAP_I2C_DATA_REG);
				if (dev->buf_len) {
					*dev->buf++ = w;
					dev->buf_len--;
					/*
					 * Data reg in 2430, omap3 and
					 * omap4 is 8 bit wide
					 */
					if (dev->flags &
						 OMAP_I2C_FLAG_16BIT_DATA_REG) {
						if (dev->buf_len) {
							*dev->buf++ = w >> 8;
							dev->buf_len--;
						}
					}
				} else {
					if (stat & OMAP_I2C_STAT_RRDY)
						dev_err(dev->dev,
							"RRDY IRQ while no data"
								" requested\n");
					if (stat & OMAP_I2C_STAT_RDR)
						dev_err(dev->dev,
							"RDR IRQ while no data"
								" requested\n");
					break;
				}
			}
			omap_i2c_ack_stat(dev,
				stat & (OMAP_I2C_STAT_RRDY | OMAP_I2C_STAT_RDR));
			continue;
		}
		if (stat & (OMAP_I2C_STAT_XRDY | OMAP_I2C_STAT_XDR)) {
			u8 num_bytes = 1;
			if (dev->fifo_size) {
				if (stat & OMAP_I2C_STAT_XRDY)
					num_bytes = dev->fifo_size;
				else    /* read TXSTAT on XDR interrupt */
					num_bytes = omap_i2c_read_reg(dev,
							OMAP_I2C_BUFSTAT_REG)
							& 0x3F;
			}
			while (num_bytes) {
				num_bytes--;
				w = 0;
				if (dev->buf_len) {
					w = *dev->buf++;
					dev->buf_len--;
					/*
					 * Data reg in 2430, omap3 and
					 * omap4 is 8 bit wide
					 */
					if (dev->flags &
						 OMAP_I2C_FLAG_16BIT_DATA_REG) {
						if (dev->buf_len) {
							w |= *dev->buf++ << 8;
							dev->buf_len--;
						}
					}
				} else {
					if (stat & OMAP_I2C_STAT_XRDY)
						dev_err(dev->dev,
							"XRDY IRQ while no "
							"data to send\n");
					if (stat & OMAP_I2C_STAT_XDR)
						dev_err(dev->dev,
							"XDR IRQ while no "
							"data to send\n");
					break;
				}

				if ((dev->errata & I2C_OMAP_ERRATA_I462) &&
				    errata_omap3_i462(dev, &stat, &err))
					goto complete;

				omap_i2c_write_reg(dev, OMAP_I2C_DATA_REG, w);
			}
			omap_i2c_ack_stat(dev,
				stat & (OMAP_I2C_STAT_XRDY | OMAP_I2C_STAT_XDR));
			continue;
		}
		if (stat & OMAP_I2C_STAT_ROVR) {
			dev_err(dev->dev, "Receive overrun\n");
			dev->cmd_err |= OMAP_I2C_STAT_ROVR;
		}
		if (stat & OMAP_I2C_STAT_XUDF) {
			dev_err(dev->dev, "Transmit underflow\n");
			dev->cmd_err |= OMAP_I2C_STAT_XUDF;
		}
	}

	return count ? IRQ_HANDLED : IRQ_NONE;
}

static const struct i2c_algorithm omap_i2c_algo = {
	.master_xfer	= omap_i2c_xfer,
	.functionality	= omap_i2c_func,
};

#ifdef CONFIG_OF
static struct omap_i2c_bus_platform_data omap3_pdata = {
	.rev = OMAP_I2C_IP_VERSION_1,
	.flags = OMAP_I2C_FLAG_APPLY_ERRATA_I207 |
		 OMAP_I2C_FLAG_RESET_REGS_POSTIDLE |
		 OMAP_I2C_FLAG_BUS_SHIFT_2,
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
	{ },
};
MODULE_DEVICE_TABLE(of, omap_i2c_of_match);
#endif

static int __devinit
omap_i2c_probe(struct platform_device *pdev)
{
	struct omap_i2c_dev	*dev;
	struct i2c_adapter	*adap;
	struct resource		*mem, *irq, *ioarea;
	struct omap_i2c_bus_platform_data *pdata = pdev->dev.platform_data;
	struct device_node	*node = pdev->dev.of_node;
	const struct of_device_id *match;
	irq_handler_t isr;
	int r;

	/* NOTE: driver uses the static register mapping */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -ENODEV;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "I2C region already claimed\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct omap_i2c_dev), GFP_KERNEL);
	if (!dev) {
		r = -ENOMEM;
		goto err_release_region;
	}

	match = of_match_device(of_match_ptr(omap_i2c_of_match), &pdev->dev);
	if (match) {
		u32 freq = 100000; /* default to 100000 Hz */

		pdata = match->data;
		dev->dtrev = pdata->rev;
		dev->flags = pdata->flags;

		of_property_read_u32(node, "clock-frequency", &freq);
		/* convert DT freq value in Hz into kHz for speed */
		dev->speed = freq / 1000;
	} else if (pdata != NULL) {
		dev->speed = pdata->clkrate;
		dev->flags = pdata->flags;
		dev->set_mpu_wkup_lat = pdata->set_mpu_wkup_lat;
		dev->dtrev = pdata->rev;
	}

	dev->dev = &pdev->dev;
	dev->irq = irq->start;
	dev->base = ioremap(mem->start, resource_size(mem));
	if (!dev->base) {
		r = -ENOMEM;
		goto err_free_mem;
	}

	platform_set_drvdata(pdev, dev);
	init_completion(&dev->cmd_complete);

	dev->reg_shift = (dev->flags >> OMAP_I2C_FLAG_BUS_SHIFT__SHIFT) & 3;

	if (dev->dtrev == OMAP_I2C_IP_VERSION_2)
		dev->regs = (u8 *)reg_map_ip_v2;
	else
		dev->regs = (u8 *)reg_map_ip_v1;

	pm_runtime_enable(dev->dev);
	r = pm_runtime_get_sync(dev->dev);
	if (IS_ERR_VALUE(r))
		goto err_free_mem;

	dev->rev = omap_i2c_read_reg(dev, OMAP_I2C_REV_REG) & 0xff;

	dev->errata = 0;

	if (dev->flags & OMAP_I2C_FLAG_APPLY_ERRATA_I207)
		dev->errata |= I2C_OMAP_ERRATA_I207;

	if (dev->rev <= OMAP_I2C_REV_ON_3430_3530)
		dev->errata |= I2C_OMAP_ERRATA_I462;

	if (!(dev->flags & OMAP_I2C_FLAG_NO_FIFO)) {
		u16 s;

		/* Set up the fifo size - Get total size */
		s = (omap_i2c_read_reg(dev, OMAP_I2C_BUFSTAT_REG) >> 14) & 0x3;
		dev->fifo_size = 0x8 << s;

		/*
		 * Set up notification threshold as half the total available
		 * size. This is to ensure that we can handle the status on int
		 * call back latencies.
		 */

		dev->fifo_size = (dev->fifo_size / 2);

		if (dev->rev >= OMAP_I2C_REV_ON_3630_4430)
			dev->b_hw = 0; /* Disable hardware fixes */
		else
			dev->b_hw = 1; /* Enable hardware fixes */

		/* calculate wakeup latency constraint for MPU */
		if (dev->set_mpu_wkup_lat != NULL)
			dev->latency = (1000000 * dev->fifo_size) /
				       (1000 * dev->speed / 8);
	}

	/* reset ASAP, clearing any IRQs */
	omap_i2c_init(dev);

	isr = (dev->rev < OMAP_I2C_OMAP1_REV_2) ? omap_i2c_omap1_isr :
								   omap_i2c_isr;
	r = request_irq(dev->irq, isr, IRQF_NO_SUSPEND, pdev->name, dev);

	if (r) {
		dev_err(dev->dev, "failure requesting irq %i\n", dev->irq);
		goto err_unuse_clocks;
	}

	dev_info(dev->dev, "bus %d rev%d.%d.%d at %d kHz\n", pdev->id,
		 dev->dtrev, dev->rev >> 4, dev->rev & 0xf, dev->speed);

	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	strlcpy(adap->name, "OMAP I2C adapter", sizeof(adap->name));
	adap->algo = &omap_i2c_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	/* i2c device drivers may be active on return from add_adapter() */
	adap->nr = pdev->id;
	r = i2c_add_numbered_adapter(adap);
	if (r) {
		dev_err(dev->dev, "failure adding adapter\n");
		goto err_free_irq;
	}

	of_i2c_register_devices(adap);

	pm_runtime_put(dev->dev);

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
err_unuse_clocks:
	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, 0);
	pm_runtime_put(dev->dev);
	iounmap(dev->base);
	pm_runtime_disable(&pdev->dev);
err_free_mem:
	platform_set_drvdata(pdev, NULL);
	kfree(dev);
err_release_region:
	release_mem_region(mem->start, resource_size(mem));

	return r;
}

static int __devexit omap_i2c_remove(struct platform_device *pdev)
{
	struct omap_i2c_dev	*dev = platform_get_drvdata(pdev);
	struct resource		*mem;
	int ret;

	platform_set_drvdata(pdev, NULL);

	free_irq(dev->irq, dev);
	i2c_del_adapter(&dev->adapter);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (IS_ERR_VALUE(ret))
		return ret;

	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, 0);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	iounmap(dev->base);
	kfree(dev);
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_RUNTIME
static int omap_i2c_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_i2c_dev *_dev = platform_get_drvdata(pdev);
	u16 iv;

	_dev->iestate = omap_i2c_read_reg(_dev, OMAP_I2C_IE_REG);

	omap_i2c_write_reg(_dev, OMAP_I2C_IE_REG, 0);

	if (_dev->rev < OMAP_I2C_OMAP1_REV_2) {
		iv = omap_i2c_read_reg(_dev, OMAP_I2C_IV_REG); /* Read clears */
	} else {
		omap_i2c_write_reg(_dev, OMAP_I2C_STAT_REG, _dev->iestate);

		/* Flush posted write */
		omap_i2c_read_reg(_dev, OMAP_I2C_STAT_REG);
	}

	return 0;
}

static int omap_i2c_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_i2c_dev *_dev = platform_get_drvdata(pdev);

	if (_dev->flags & OMAP_I2C_FLAG_RESET_REGS_POSTIDLE) {
		omap_i2c_write_reg(_dev, OMAP_I2C_CON_REG, 0);
		omap_i2c_write_reg(_dev, OMAP_I2C_PSC_REG, _dev->pscstate);
		omap_i2c_write_reg(_dev, OMAP_I2C_SCLL_REG, _dev->scllstate);
		omap_i2c_write_reg(_dev, OMAP_I2C_SCLH_REG, _dev->sclhstate);
		omap_i2c_write_reg(_dev, OMAP_I2C_BUF_REG, _dev->bufstate);
		omap_i2c_write_reg(_dev, OMAP_I2C_SYSC_REG, _dev->syscstate);
		omap_i2c_write_reg(_dev, OMAP_I2C_WE_REG, _dev->westate);
		omap_i2c_write_reg(_dev, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);
	}

	/*
	 * Don't write to this register if the IE state is 0 as it can
	 * cause deadlock.
	 */
	if (_dev->iestate)
		omap_i2c_write_reg(_dev, OMAP_I2C_IE_REG, _dev->iestate);

	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static struct dev_pm_ops omap_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_i2c_runtime_suspend,
			   omap_i2c_runtime_resume, NULL)
};
#define OMAP_I2C_PM_OPS (&omap_i2c_pm_ops)
#else
#define OMAP_I2C_PM_OPS NULL
#endif /* CONFIG_PM */

static struct platform_driver omap_i2c_driver = {
	.probe		= omap_i2c_probe,
	.remove		= __devexit_p(omap_i2c_remove),
	.driver		= {
		.name	= "omap_i2c",
		.owner	= THIS_MODULE,
		.pm	= OMAP_I2C_PM_OPS,
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
