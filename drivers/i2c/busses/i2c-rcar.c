// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Renesas R-Car I2C unit
 *
 * Copyright (C) 2014-19 Wolfram Sang <wsa@sang-engineering.com>
 * Copyright (C) 2011-2019 Renesas Electronics Corporation
 *
 * Copyright (C) 2012-14 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is based on the drivers/i2c/busses/i2c-sh7760.c
 * (c) 2005-2008 MSC Vertriebsges.m.b.H, Manuel Lauss <mlau@msc-ge.com>
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

/* register offsets */
#define ICSCR	0x00	/* slave ctrl */
#define ICMCR	0x04	/* master ctrl */
#define ICSSR	0x08	/* slave status */
#define ICMSR	0x0C	/* master status */
#define ICSIER	0x10	/* slave irq enable */
#define ICMIER	0x14	/* master irq enable */
#define ICCCR	0x18	/* clock dividers */
#define ICSAR	0x1C	/* slave address */
#define ICMAR	0x20	/* master address */
#define ICRXTX	0x24	/* data port */
#define ICFBSCR	0x38	/* first bit setup cycle (Gen3) */
#define ICDMAER	0x3c	/* DMA enable (Gen3) */

/* ICSCR */
#define SDBS	(1 << 3)	/* slave data buffer select */
#define SIE	(1 << 2)	/* slave interface enable */
#define GCAE	(1 << 1)	/* general call address enable */
#define FNA	(1 << 0)	/* forced non acknowledgment */

/* ICMCR */
#define MDBS	(1 << 7)	/* non-fifo mode switch */
#define FSCL	(1 << 6)	/* override SCL pin */
#define FSDA	(1 << 5)	/* override SDA pin */
#define OBPC	(1 << 4)	/* override pins */
#define MIE	(1 << 3)	/* master if enable */
#define TSBE	(1 << 2)
#define FSB	(1 << 1)	/* force stop bit */
#define ESG	(1 << 0)	/* enable start bit gen */

/* ICSSR (also for ICSIER) */
#define GCAR	(1 << 6)	/* general call received */
#define STM	(1 << 5)	/* slave transmit mode */
#define SSR	(1 << 4)	/* stop received */
#define SDE	(1 << 3)	/* slave data empty */
#define SDT	(1 << 2)	/* slave data transmitted */
#define SDR	(1 << 1)	/* slave data received */
#define SAR	(1 << 0)	/* slave addr received */

/* ICMSR (also for ICMIE) */
#define MNR	(1 << 6)	/* nack received */
#define MAL	(1 << 5)	/* arbitration lost */
#define MST	(1 << 4)	/* sent a stop */
#define MDE	(1 << 3)
#define MDT	(1 << 2)
#define MDR	(1 << 1)
#define MAT	(1 << 0)	/* slave addr xfer done */

/* ICDMAER */
#define RSDMAE	(1 << 3)	/* DMA Slave Received Enable */
#define TSDMAE	(1 << 2)	/* DMA Slave Transmitted Enable */
#define RMDMAE	(1 << 1)	/* DMA Master Received Enable */
#define TMDMAE	(1 << 0)	/* DMA Master Transmitted Enable */

/* ICFBSCR */
#define TCYC17	0x0f		/* 17*Tcyc delay 1st bit between SDA and SCL */

#define RCAR_MIN_DMA_LEN	8

#define RCAR_BUS_PHASE_START	(MDBS | MIE | ESG)
#define RCAR_BUS_PHASE_DATA	(MDBS | MIE)
#define RCAR_BUS_MASK_DATA	(~(ESG | FSB) & 0xFF)
#define RCAR_BUS_PHASE_STOP	(MDBS | MIE | FSB)

#define RCAR_IRQ_SEND	(MNR | MAL | MST | MAT | MDE)
#define RCAR_IRQ_RECV	(MNR | MAL | MST | MAT | MDR)
#define RCAR_IRQ_STOP	(MST)

#define RCAR_IRQ_ACK_SEND	(~(MAT | MDE) & 0x7F)
#define RCAR_IRQ_ACK_RECV	(~(MAT | MDR) & 0x7F)

#define ID_LAST_MSG	(1 << 0)
#define ID_FIRST_MSG	(1 << 1)
#define ID_DONE		(1 << 2)
#define ID_ARBLOST	(1 << 3)
#define ID_NACK		(1 << 4)
/* persistent flags */
#define ID_P_HOST_NOTIFY	BIT(28)
#define ID_P_REP_AFTER_RD	BIT(29)
#define ID_P_NO_RXDMA		BIT(30) /* HW forbids RXDMA sometimes */
#define ID_P_PM_BLOCKED		BIT(31)
#define ID_P_MASK		GENMASK(31, 28)

enum rcar_i2c_type {
	I2C_RCAR_GEN1,
	I2C_RCAR_GEN2,
	I2C_RCAR_GEN3,
};

struct rcar_i2c_priv {
	void __iomem *io;
	struct i2c_adapter adap;
	struct i2c_msg *msg;
	int msgs_left;
	struct clk *clk;

	wait_queue_head_t wait;

	int pos;
	u32 icccr;
	u32 flags;
	u8 recovery_icmcr;	/* protected by adapter lock */
	enum rcar_i2c_type devtype;
	struct i2c_client *slave;

	struct resource *res;
	struct dma_chan *dma_tx;
	struct dma_chan *dma_rx;
	struct scatterlist sg;
	enum dma_data_direction dma_direction;

	struct reset_control *rstc;
	int irq;

	struct i2c_client *host_notify_client;
};

#define rcar_i2c_priv_to_dev(p)		((p)->adap.dev.parent)
#define rcar_i2c_is_recv(p)		((p)->msg->flags & I2C_M_RD)

static void rcar_i2c_write(struct rcar_i2c_priv *priv, int reg, u32 val)
{
	writel(val, priv->io + reg);
}

static u32 rcar_i2c_read(struct rcar_i2c_priv *priv, int reg)
{
	return readl(priv->io + reg);
}

static int rcar_i2c_get_scl(struct i2c_adapter *adap)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);

	return !!(rcar_i2c_read(priv, ICMCR) & FSCL);

};

static void rcar_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);

	if (val)
		priv->recovery_icmcr |= FSCL;
	else
		priv->recovery_icmcr &= ~FSCL;

	rcar_i2c_write(priv, ICMCR, priv->recovery_icmcr);
};

static void rcar_i2c_set_sda(struct i2c_adapter *adap, int val)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);

	if (val)
		priv->recovery_icmcr |= FSDA;
	else
		priv->recovery_icmcr &= ~FSDA;

	rcar_i2c_write(priv, ICMCR, priv->recovery_icmcr);
};

static int rcar_i2c_get_bus_free(struct i2c_adapter *adap)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);

	return !(rcar_i2c_read(priv, ICMCR) & FSDA);

};

static struct i2c_bus_recovery_info rcar_i2c_bri = {
	.get_scl = rcar_i2c_get_scl,
	.set_scl = rcar_i2c_set_scl,
	.set_sda = rcar_i2c_set_sda,
	.get_bus_free = rcar_i2c_get_bus_free,
	.recover_bus = i2c_generic_scl_recovery,
};
static void rcar_i2c_init(struct rcar_i2c_priv *priv)
{
	/* reset master mode */
	rcar_i2c_write(priv, ICMIER, 0);
	rcar_i2c_write(priv, ICMCR, MDBS);
	rcar_i2c_write(priv, ICMSR, 0);
	/* start clock */
	rcar_i2c_write(priv, ICCCR, priv->icccr);

	if (priv->devtype == I2C_RCAR_GEN3)
		rcar_i2c_write(priv, ICFBSCR, TCYC17);

}

static int rcar_i2c_bus_barrier(struct rcar_i2c_priv *priv)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(priv->io + ICMCR, val, !(val & FSDA), 10,
				 priv->adap.timeout);
	if (ret) {
		/* Waiting did not help, try to recover */
		priv->recovery_icmcr = MDBS | OBPC | FSDA | FSCL;
		ret = i2c_recover_bus(&priv->adap);
	}

	return ret;
}

static int rcar_i2c_clock_calculate(struct rcar_i2c_priv *priv)
{
	u32 scgd, cdf, round, ick, sum, scl, cdf_width;
	unsigned long rate;
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	struct i2c_timings t = {
		.bus_freq_hz		= I2C_MAX_STANDARD_MODE_FREQ,
		.scl_fall_ns		= 35,
		.scl_rise_ns		= 200,
		.scl_int_delay_ns	= 50,
	};

	/* Fall back to previously used values if not supplied */
	i2c_parse_fw_timings(dev, &t, false);

	switch (priv->devtype) {
	case I2C_RCAR_GEN1:
		cdf_width = 2;
		break;
	case I2C_RCAR_GEN2:
	case I2C_RCAR_GEN3:
		cdf_width = 3;
		break;
	default:
		dev_err(dev, "device type error\n");
		return -EIO;
	}

	/*
	 * calculate SCL clock
	 * see
	 *	ICCCR
	 *
	 * ick	= clkp / (1 + CDF)
	 * SCL	= ick / (20 + SCGD * 8 + F[(ticf + tr + intd) * ick])
	 *
	 * ick  : I2C internal clock < 20 MHz
	 * ticf : I2C SCL falling time
	 * tr   : I2C SCL rising  time
	 * intd : LSI internal delay
	 * clkp : peripheral_clk
	 * F[]  : integer up-valuation
	 */
	rate = clk_get_rate(priv->clk);
	cdf = rate / 20000000;
	if (cdf >= 1U << cdf_width) {
		dev_err(dev, "Input clock %lu too high\n", rate);
		return -EIO;
	}
	ick = rate / (cdf + 1);

	/*
	 * it is impossible to calculate large scale
	 * number on u32. separate it
	 *
	 * F[(ticf + tr + intd) * ick] with sum = (ticf + tr + intd)
	 *  = F[sum * ick / 1000000000]
	 *  = F[(ick / 1000000) * sum / 1000]
	 */
	sum = t.scl_fall_ns + t.scl_rise_ns + t.scl_int_delay_ns;
	round = (ick + 500000) / 1000000 * sum;
	round = (round + 500) / 1000;

	/*
	 * SCL	= ick / (20 + SCGD * 8 + F[(ticf + tr + intd) * ick])
	 *
	 * Calculation result (= SCL) should be less than
	 * bus_speed for hardware safety
	 *
	 * We could use something along the lines of
	 *	div = ick / (bus_speed + 1) + 1;
	 *	scgd = (div - 20 - round + 7) / 8;
	 *	scl = ick / (20 + (scgd * 8) + round);
	 * (not fully verified) but that would get pretty involved
	 */
	for (scgd = 0; scgd < 0x40; scgd++) {
		scl = ick / (20 + (scgd * 8) + round);
		if (scl <= t.bus_freq_hz)
			goto scgd_find;
	}
	dev_err(dev, "it is impossible to calculate best SCL\n");
	return -EIO;

scgd_find:
	dev_dbg(dev, "clk %d/%d(%lu), round %u, CDF:0x%x, SCGD: 0x%x\n",
		scl, t.bus_freq_hz, rate, round, cdf, scgd);

	/* keep icccr value */
	priv->icccr = scgd << cdf_width | cdf;

	return 0;
}

static void rcar_i2c_prepare_msg(struct rcar_i2c_priv *priv)
{
	int read = !!rcar_i2c_is_recv(priv);

	priv->pos = 0;
	if (priv->msgs_left == 1)
		priv->flags |= ID_LAST_MSG;

	rcar_i2c_write(priv, ICMAR, i2c_8bit_addr_from_msg(priv->msg));
	/*
	 * We don't have a test case but the HW engineers say that the write order
	 * of ICMSR and ICMCR depends on whether we issue START or REP_START. Since
	 * it didn't cause a drawback for me, let's rather be safe than sorry.
	 */
	if (priv->flags & ID_FIRST_MSG) {
		rcar_i2c_write(priv, ICMSR, 0);
		rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_START);
	} else {
		if (priv->flags & ID_P_REP_AFTER_RD)
			priv->flags &= ~ID_P_REP_AFTER_RD;
		else
			rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_START);
		rcar_i2c_write(priv, ICMSR, 0);
	}
	rcar_i2c_write(priv, ICMIER, read ? RCAR_IRQ_RECV : RCAR_IRQ_SEND);
}

static void rcar_i2c_next_msg(struct rcar_i2c_priv *priv)
{
	priv->msg++;
	priv->msgs_left--;
	priv->flags &= ID_P_MASK;
	rcar_i2c_prepare_msg(priv);
}

static void rcar_i2c_dma_unmap(struct rcar_i2c_priv *priv)
{
	struct dma_chan *chan = priv->dma_direction == DMA_FROM_DEVICE
		? priv->dma_rx : priv->dma_tx;

	dma_unmap_single(chan->device->dev, sg_dma_address(&priv->sg),
			 sg_dma_len(&priv->sg), priv->dma_direction);

	/* Gen3 can only do one RXDMA per transfer and we just completed it */
	if (priv->devtype == I2C_RCAR_GEN3 &&
	    priv->dma_direction == DMA_FROM_DEVICE)
		priv->flags |= ID_P_NO_RXDMA;

	priv->dma_direction = DMA_NONE;

	/* Disable DMA Master Received/Transmitted, must be last! */
	rcar_i2c_write(priv, ICDMAER, 0);
}

static void rcar_i2c_cleanup_dma(struct rcar_i2c_priv *priv)
{
	if (priv->dma_direction == DMA_NONE)
		return;
	else if (priv->dma_direction == DMA_FROM_DEVICE)
		dmaengine_terminate_all(priv->dma_rx);
	else if (priv->dma_direction == DMA_TO_DEVICE)
		dmaengine_terminate_all(priv->dma_tx);

	rcar_i2c_dma_unmap(priv);
}

static void rcar_i2c_dma_callback(void *data)
{
	struct rcar_i2c_priv *priv = data;

	priv->pos += sg_dma_len(&priv->sg);

	rcar_i2c_dma_unmap(priv);
}

static bool rcar_i2c_dma(struct rcar_i2c_priv *priv)
{
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	struct i2c_msg *msg = priv->msg;
	bool read = msg->flags & I2C_M_RD;
	enum dma_data_direction dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct dma_chan *chan = read ? priv->dma_rx : priv->dma_tx;
	struct dma_async_tx_descriptor *txdesc;
	dma_addr_t dma_addr;
	dma_cookie_t cookie;
	unsigned char *buf;
	int len;

	/* Do various checks to see if DMA is feasible at all */
	if (IS_ERR(chan) || msg->len < RCAR_MIN_DMA_LEN ||
	    !(msg->flags & I2C_M_DMA_SAFE) || (read && priv->flags & ID_P_NO_RXDMA))
		return false;

	if (read) {
		/*
		 * The last two bytes needs to be fetched using PIO in
		 * order for the STOP phase to work.
		 */
		buf = priv->msg->buf;
		len = priv->msg->len - 2;
	} else {
		/*
		 * First byte in message was sent using PIO.
		 */
		buf = priv->msg->buf + 1;
		len = priv->msg->len - 1;
	}

	dma_addr = dma_map_single(chan->device->dev, buf, len, dir);
	if (dma_mapping_error(chan->device->dev, dma_addr)) {
		dev_dbg(dev, "dma map failed, using PIO\n");
		return false;
	}

	sg_dma_len(&priv->sg) = len;
	sg_dma_address(&priv->sg) = dma_addr;

	priv->dma_direction = dir;

	txdesc = dmaengine_prep_slave_sg(chan, &priv->sg, 1,
					 read ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc) {
		dev_dbg(dev, "dma prep slave sg failed, using PIO\n");
		rcar_i2c_cleanup_dma(priv);
		return false;
	}

	txdesc->callback = rcar_i2c_dma_callback;
	txdesc->callback_param = priv;

	cookie = dmaengine_submit(txdesc);
	if (dma_submit_error(cookie)) {
		dev_dbg(dev, "submitting dma failed, using PIO\n");
		rcar_i2c_cleanup_dma(priv);
		return false;
	}

	/* Enable DMA Master Received/Transmitted */
	if (read)
		rcar_i2c_write(priv, ICDMAER, RMDMAE);
	else
		rcar_i2c_write(priv, ICDMAER, TMDMAE);

	dma_async_issue_pending(chan);
	return true;
}

static void rcar_i2c_irq_send(struct rcar_i2c_priv *priv, u32 msr)
{
	struct i2c_msg *msg = priv->msg;

	/* FIXME: sometimes, unknown interrupt happened. Do nothing */
	if (!(msr & MDE))
		return;

	/* Check if DMA can be enabled and take over */
	if (priv->pos == 1 && rcar_i2c_dma(priv))
		return;

	if (priv->pos < msg->len) {
		/*
		 * Prepare next data to ICRXTX register.
		 * This data will go to _SHIFT_ register.
		 *
		 *    *
		 * [ICRXTX] -> [SHIFT] -> [I2C bus]
		 */
		rcar_i2c_write(priv, ICRXTX, msg->buf[priv->pos]);
		priv->pos++;
	} else {
		/*
		 * The last data was pushed to ICRXTX on _PREV_ empty irq.
		 * It is on _SHIFT_ register, and will sent to I2C bus.
		 *
		 *		  *
		 * [ICRXTX] -> [SHIFT] -> [I2C bus]
		 */

		if (priv->flags & ID_LAST_MSG) {
			/*
			 * If current msg is the _LAST_ msg,
			 * prepare stop condition here.
			 * ID_DONE will be set on STOP irq.
			 */
			rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_STOP);
		} else {
			rcar_i2c_next_msg(priv);
			return;
		}
	}

	rcar_i2c_write(priv, ICMSR, RCAR_IRQ_ACK_SEND);
}

static void rcar_i2c_irq_recv(struct rcar_i2c_priv *priv, u32 msr)
{
	struct i2c_msg *msg = priv->msg;

	/* FIXME: sometimes, unknown interrupt happened. Do nothing */
	if (!(msr & MDR))
		return;

	if (msr & MAT) {
		/*
		 * Address transfer phase finished, but no data at this point.
		 * Try to use DMA to receive data.
		 */
		rcar_i2c_dma(priv);
	} else if (priv->pos < msg->len) {
		/* get received data */
		msg->buf[priv->pos] = rcar_i2c_read(priv, ICRXTX);
		priv->pos++;
	}

	/* If next received data is the _LAST_, go to new phase. */
	if (priv->pos + 1 == msg->len) {
		if (priv->flags & ID_LAST_MSG) {
			rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_STOP);
		} else {
			rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_START);
			priv->flags |= ID_P_REP_AFTER_RD;
		}
	}

	if (priv->pos == msg->len && !(priv->flags & ID_LAST_MSG))
		rcar_i2c_next_msg(priv);
	else
		rcar_i2c_write(priv, ICMSR, RCAR_IRQ_ACK_RECV);
}

static bool rcar_i2c_slave_irq(struct rcar_i2c_priv *priv)
{
	u32 ssr_raw, ssr_filtered;
	u8 value;

	ssr_raw = rcar_i2c_read(priv, ICSSR) & 0xff;
	ssr_filtered = ssr_raw & rcar_i2c_read(priv, ICSIER);

	if (!ssr_filtered)
		return false;

	/* address detected */
	if (ssr_filtered & SAR) {
		/* read or write request */
		if (ssr_raw & STM) {
			i2c_slave_event(priv->slave, I2C_SLAVE_READ_REQUESTED, &value);
			rcar_i2c_write(priv, ICRXTX, value);
			rcar_i2c_write(priv, ICSIER, SDE | SSR | SAR);
		} else {
			i2c_slave_event(priv->slave, I2C_SLAVE_WRITE_REQUESTED, &value);
			rcar_i2c_read(priv, ICRXTX);	/* dummy read */
			rcar_i2c_write(priv, ICSIER, SDR | SSR | SAR);
		}

		/* Clear SSR, too, because of old STOPs to other clients than us */
		rcar_i2c_write(priv, ICSSR, ~(SAR | SSR) & 0xff);
	}

	/* master sent stop */
	if (ssr_filtered & SSR) {
		i2c_slave_event(priv->slave, I2C_SLAVE_STOP, &value);
		rcar_i2c_write(priv, ICSCR, SIE | SDBS); /* clear our NACK */
		rcar_i2c_write(priv, ICSIER, SAR);
		rcar_i2c_write(priv, ICSSR, ~SSR & 0xff);
	}

	/* master wants to write to us */
	if (ssr_filtered & SDR) {
		int ret;

		value = rcar_i2c_read(priv, ICRXTX);
		ret = i2c_slave_event(priv->slave, I2C_SLAVE_WRITE_RECEIVED, &value);
		/* Send NACK in case of error */
		rcar_i2c_write(priv, ICSCR, SIE | SDBS | (ret < 0 ? FNA : 0));
		rcar_i2c_write(priv, ICSSR, ~SDR & 0xff);
	}

	/* master wants to read from us */
	if (ssr_filtered & SDE) {
		i2c_slave_event(priv->slave, I2C_SLAVE_READ_PROCESSED, &value);
		rcar_i2c_write(priv, ICRXTX, value);
		rcar_i2c_write(priv, ICSSR, ~SDE & 0xff);
	}

	return true;
}

/*
 * This driver has a lock-free design because there are IP cores (at least
 * R-Car Gen2) which have an inherent race condition in their hardware design.
 * There, we need to clear RCAR_BUS_MASK_DATA bits as soon as possible after
 * the interrupt was generated, otherwise an unwanted repeated message gets
 * generated. It turned out that taking a spinlock at the beginning of the ISR
 * was already causing repeated messages. Thus, this driver was converted to
 * the now lockless behaviour. Please keep this in mind when hacking the driver.
 */
static irqreturn_t rcar_i2c_irq(int irq, void *ptr)
{
	struct rcar_i2c_priv *priv = ptr;
	u32 msr, val;

	/* Clear START or STOP immediately, except for REPSTART after read */
	if (likely(!(priv->flags & ID_P_REP_AFTER_RD))) {
		val = rcar_i2c_read(priv, ICMCR);
		rcar_i2c_write(priv, ICMCR, val & RCAR_BUS_MASK_DATA);
	}

	msr = rcar_i2c_read(priv, ICMSR);

	/* Only handle interrupts that are currently enabled */
	msr &= rcar_i2c_read(priv, ICMIER);
	if (!msr) {
		if (rcar_i2c_slave_irq(priv))
			return IRQ_HANDLED;

		return IRQ_NONE;
	}

	/* Arbitration lost */
	if (msr & MAL) {
		priv->flags |= ID_DONE | ID_ARBLOST;
		goto out;
	}

	/* Nack */
	if (msr & MNR) {
		/* HW automatically sends STOP after received NACK */
		rcar_i2c_write(priv, ICMIER, RCAR_IRQ_STOP);
		priv->flags |= ID_NACK;
		goto out;
	}

	/* Stop */
	if (msr & MST) {
		priv->msgs_left--; /* The last message also made it */
		priv->flags |= ID_DONE;
		goto out;
	}

	if (rcar_i2c_is_recv(priv))
		rcar_i2c_irq_recv(priv, msr);
	else
		rcar_i2c_irq_send(priv, msr);

out:
	if (priv->flags & ID_DONE) {
		rcar_i2c_write(priv, ICMIER, 0);
		rcar_i2c_write(priv, ICMSR, 0);
		wake_up(&priv->wait);
	}

	return IRQ_HANDLED;
}

static struct dma_chan *rcar_i2c_request_dma_chan(struct device *dev,
					enum dma_transfer_direction dir,
					dma_addr_t port_addr)
{
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	char *chan_name = dir == DMA_MEM_TO_DEV ? "tx" : "rx";
	int ret;

	chan = dma_request_chan(dev, chan_name);
	if (IS_ERR(chan)) {
		dev_dbg(dev, "request_channel failed for %s (%ld)\n",
			chan_name, PTR_ERR(chan));
		return chan;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;
	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = port_addr;
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	} else {
		cfg.src_addr = port_addr;
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_dbg(dev, "slave_config failed for %s (%d)\n",
			chan_name, ret);
		dma_release_channel(chan);
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "got DMA channel for %s\n", chan_name);
	return chan;
}

static void rcar_i2c_request_dma(struct rcar_i2c_priv *priv,
				 struct i2c_msg *msg)
{
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	bool read;
	struct dma_chan *chan;
	enum dma_transfer_direction dir;

	read = msg->flags & I2C_M_RD;

	chan = read ? priv->dma_rx : priv->dma_tx;
	if (PTR_ERR(chan) != -EPROBE_DEFER)
		return;

	dir = read ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;
	chan = rcar_i2c_request_dma_chan(dev, dir, priv->res->start + ICRXTX);

	if (read)
		priv->dma_rx = chan;
	else
		priv->dma_tx = chan;
}

static void rcar_i2c_release_dma(struct rcar_i2c_priv *priv)
{
	if (!IS_ERR(priv->dma_tx)) {
		dma_release_channel(priv->dma_tx);
		priv->dma_tx = ERR_PTR(-EPROBE_DEFER);
	}

	if (!IS_ERR(priv->dma_rx)) {
		dma_release_channel(priv->dma_rx);
		priv->dma_rx = ERR_PTR(-EPROBE_DEFER);
	}
}

/* I2C is a special case, we need to poll the status of a reset */
static int rcar_i2c_do_reset(struct rcar_i2c_priv *priv)
{
	int ret;

	ret = reset_control_reset(priv->rstc);
	if (ret)
		return ret;

	return read_poll_timeout_atomic(reset_control_status, ret, ret == 0, 1,
					100, false, priv->rstc);
}

static int rcar_i2c_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs,
				int num)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	int i, ret;
	long time_left;

	pm_runtime_get_sync(dev);

	/* Check bus state before init otherwise bus busy info will be lost */
	ret = rcar_i2c_bus_barrier(priv);
	if (ret < 0)
		goto out;

	/* Gen3 needs a reset before allowing RXDMA once */
	if (priv->devtype == I2C_RCAR_GEN3) {
		priv->flags |= ID_P_NO_RXDMA;
		if (!IS_ERR(priv->rstc)) {
			ret = rcar_i2c_do_reset(priv);
			if (ret == 0)
				priv->flags &= ~ID_P_NO_RXDMA;
		}
	}

	rcar_i2c_init(priv);

	for (i = 0; i < num; i++)
		rcar_i2c_request_dma(priv, msgs + i);

	/* init first message */
	priv->msg = msgs;
	priv->msgs_left = num;
	priv->flags = (priv->flags & ID_P_MASK) | ID_FIRST_MSG;
	rcar_i2c_prepare_msg(priv);

	time_left = wait_event_timeout(priv->wait, priv->flags & ID_DONE,
				     num * adap->timeout);

	/* cleanup DMA if it couldn't complete properly due to an error */
	if (priv->dma_direction != DMA_NONE)
		rcar_i2c_cleanup_dma(priv);

	if (!time_left) {
		rcar_i2c_init(priv);
		ret = -ETIMEDOUT;
	} else if (priv->flags & ID_NACK) {
		ret = -ENXIO;
	} else if (priv->flags & ID_ARBLOST) {
		ret = -EAGAIN;
	} else {
		ret = num - priv->msgs_left; /* The number of transfer */
	}
out:
	pm_runtime_put(dev);

	if (ret < 0 && ret != -ENXIO)
		dev_err(dev, "error %d : %x\n", ret, priv->flags);

	return ret;
}

static int rcar_reg_slave(struct i2c_client *slave)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(slave->adapter);

	if (priv->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	/* Keep device active for slave address detection logic */
	pm_runtime_get_sync(rcar_i2c_priv_to_dev(priv));

	priv->slave = slave;
	rcar_i2c_write(priv, ICSAR, slave->addr);
	rcar_i2c_write(priv, ICSSR, 0);
	rcar_i2c_write(priv, ICSIER, SAR);
	rcar_i2c_write(priv, ICSCR, SIE | SDBS);

	return 0;
}

static int rcar_unreg_slave(struct i2c_client *slave)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(slave->adapter);

	WARN_ON(!priv->slave);

	/* ensure no irq is running before clearing ptr */
	disable_irq(priv->irq);
	rcar_i2c_write(priv, ICSIER, 0);
	rcar_i2c_write(priv, ICSSR, 0);
	enable_irq(priv->irq);
	rcar_i2c_write(priv, ICSCR, SDBS);
	rcar_i2c_write(priv, ICSAR, 0); /* Gen2: must be 0 if not using slave */

	priv->slave = NULL;

	pm_runtime_put(rcar_i2c_priv_to_dev(priv));

	return 0;
}

static u32 rcar_i2c_func(struct i2c_adapter *adap)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);

	/*
	 * This HW can't do:
	 * I2C_SMBUS_QUICK (setting FSB during START didn't work)
	 * I2C_M_NOSTART (automatically sends address after START)
	 * I2C_M_IGNORE_NAK (automatically sends STOP after NAK)
	 */
	u32 func = I2C_FUNC_I2C | I2C_FUNC_SLAVE |
		   (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);

	if (priv->flags & ID_P_HOST_NOTIFY)
		func |= I2C_FUNC_SMBUS_HOST_NOTIFY;

	return func;
}

static const struct i2c_algorithm rcar_i2c_algo = {
	.master_xfer	= rcar_i2c_master_xfer,
	.functionality	= rcar_i2c_func,
	.reg_slave	= rcar_reg_slave,
	.unreg_slave	= rcar_unreg_slave,
};

static const struct i2c_adapter_quirks rcar_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static const struct of_device_id rcar_i2c_dt_ids[] = {
	{ .compatible = "renesas,i2c-r8a7778", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,i2c-r8a7779", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,i2c-r8a7790", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7791", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7792", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7793", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7794", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7795", .data = (void *)I2C_RCAR_GEN3 },
	{ .compatible = "renesas,i2c-r8a7796", .data = (void *)I2C_RCAR_GEN3 },
	{ .compatible = "renesas,i2c-rcar", .data = (void *)I2C_RCAR_GEN1 },	/* Deprecated */
	{ .compatible = "renesas,rcar-gen1-i2c", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,rcar-gen2-i2c", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,rcar-gen3-i2c", .data = (void *)I2C_RCAR_GEN3 },
	{},
};
MODULE_DEVICE_TABLE(of, rcar_i2c_dt_ids);

static int rcar_i2c_probe(struct platform_device *pdev)
{
	struct rcar_i2c_priv *priv;
	struct i2c_adapter *adap;
	struct device *dev = &pdev->dev;
	int ret;

	/* Otherwise logic will break because some bytes must always use PIO */
	BUILD_BUG_ON_MSG(RCAR_MIN_DMA_LEN < 3, "Invalid min DMA length");

	priv = devm_kzalloc(dev, sizeof(struct rcar_i2c_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "cannot get clock\n");
		return PTR_ERR(priv->clk);
	}

	priv->io = devm_platform_get_and_ioremap_resource(pdev, 0, &priv->res);
	if (IS_ERR(priv->io))
		return PTR_ERR(priv->io);

	priv->devtype = (enum rcar_i2c_type)of_device_get_match_data(dev);
	init_waitqueue_head(&priv->wait);

	adap = &priv->adap;
	adap->nr = pdev->id;
	adap->algo = &rcar_i2c_algo;
	adap->class = I2C_CLASS_DEPRECATED;
	adap->retries = 3;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;
	adap->bus_recovery_info = &rcar_i2c_bri;
	adap->quirks = &rcar_i2c_quirks;
	i2c_set_adapdata(adap, priv);
	strlcpy(adap->name, pdev->name, sizeof(adap->name));

	/* Init DMA */
	sg_init_table(&priv->sg, 1);
	priv->dma_direction = DMA_NONE;
	priv->dma_rx = priv->dma_tx = ERR_PTR(-EPROBE_DEFER);

	/* Activate device for clock calculation */
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	ret = rcar_i2c_clock_calculate(priv);
	if (ret < 0)
		goto out_pm_put;

	rcar_i2c_write(priv, ICSAR, 0); /* Gen2: must be 0 if not using slave */

	if (priv->devtype == I2C_RCAR_GEN3) {
		priv->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
		if (!IS_ERR(priv->rstc)) {
			ret = reset_control_status(priv->rstc);
			if (ret < 0)
				priv->rstc = ERR_PTR(-ENOTSUPP);
		}
	}

	/* Stay always active when multi-master to keep arbitration working */
	if (of_property_read_bool(dev->of_node, "multi-master"))
		priv->flags |= ID_P_PM_BLOCKED;
	else
		pm_runtime_put(dev);

	if (of_property_read_bool(dev->of_node, "smbus"))
		priv->flags |= ID_P_HOST_NOTIFY;

	priv->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, priv->irq, rcar_i2c_irq, 0, dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "cannot get irq %d\n", priv->irq);
		goto out_pm_disable;
	}

	platform_set_drvdata(pdev, priv);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0)
		goto out_pm_disable;

	if (priv->flags & ID_P_HOST_NOTIFY) {
		priv->host_notify_client = i2c_new_slave_host_notify_device(adap);
		if (IS_ERR(priv->host_notify_client)) {
			ret = PTR_ERR(priv->host_notify_client);
			goto out_del_device;
		}
	}

	dev_info(dev, "probed\n");

	return 0;

 out_del_device:
	i2c_del_adapter(&priv->adap);
 out_pm_put:
	pm_runtime_put(dev);
 out_pm_disable:
	pm_runtime_disable(dev);
	return ret;
}

static int rcar_i2c_remove(struct platform_device *pdev)
{
	struct rcar_i2c_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (priv->host_notify_client)
		i2c_free_slave_host_notify_device(priv->host_notify_client);
	i2c_del_adapter(&priv->adap);
	rcar_i2c_release_dma(priv);
	if (priv->flags & ID_P_PM_BLOCKED)
		pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rcar_i2c_suspend(struct device *dev)
{
	struct rcar_i2c_priv *priv = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&priv->adap);
	return 0;
}

static int rcar_i2c_resume(struct device *dev)
{
	struct rcar_i2c_priv *priv = dev_get_drvdata(dev);

	i2c_mark_adapter_resumed(&priv->adap);
	return 0;
}

static const struct dev_pm_ops rcar_i2c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rcar_i2c_suspend, rcar_i2c_resume)
};

#define DEV_PM_OPS (&rcar_i2c_pm_ops)
#else
#define DEV_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver rcar_i2c_driver = {
	.driver	= {
		.name	= "i2c-rcar",
		.of_match_table = rcar_i2c_dt_ids,
		.pm	= DEV_PM_OPS,
	},
	.probe		= rcar_i2c_probe,
	.remove		= rcar_i2c_remove,
};

module_platform_driver(rcar_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car I2C bus driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
