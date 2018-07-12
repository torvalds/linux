// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cobalt I2C functions
 *
 *  Derived from cx18-i2c.c
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#include "cobalt-driver.h"
#include "cobalt-i2c.h"

struct cobalt_i2c_regs {
	/* Clock prescaler register lo-byte */
	u8 prerlo;
	u8 dummy0[3];
	/* Clock prescaler register high-byte */
	u8 prerhi;
	u8 dummy1[3];
	/* Control register */
	u8 ctr;
	u8 dummy2[3];
	/* Transmit/Receive register */
	u8 txr_rxr;
	u8 dummy3[3];
	/* Command and Status register */
	u8 cr_sr;
	u8 dummy4[3];
};

/* CTR[7:0] - Control register */

/* I2C Core enable bit */
#define M00018_CTR_BITMAP_EN_MSK	(1 << 7)

/* I2C Core interrupt enable bit */
#define M00018_CTR_BITMAP_IEN_MSK	(1 << 6)

/* CR[7:0] - Command register */

/* I2C start condition */
#define M00018_CR_BITMAP_STA_MSK	(1 << 7)

/* I2C stop condition */
#define M00018_CR_BITMAP_STO_MSK	(1 << 6)

/* I2C read from slave */
#define M00018_CR_BITMAP_RD_MSK		(1 << 5)

/* I2C write to slave */
#define M00018_CR_BITMAP_WR_MSK		(1 << 4)

/* I2C ack */
#define M00018_CR_BITMAP_ACK_MSK	(1 << 3)

/* I2C Interrupt ack */
#define M00018_CR_BITMAP_IACK_MSK	(1 << 0)

/* SR[7:0] - Status register */

/* Receive acknowledge from slave */
#define M00018_SR_BITMAP_RXACK_MSK	(1 << 7)

/* Busy, I2C bus busy (as defined by start / stop bits) */
#define M00018_SR_BITMAP_BUSY_MSK	(1 << 6)

/* Arbitration lost - core lost arbitration */
#define M00018_SR_BITMAP_AL_MSK		(1 << 5)

/* Transfer in progress */
#define M00018_SR_BITMAP_TIP_MSK	(1 << 1)

/* Interrupt flag */
#define M00018_SR_BITMAP_IF_MSK		(1 << 0)

/* Frequency, in Hz */
#define I2C_FREQUENCY			400000
#define ALT_CPU_FREQ			83333333

static struct cobalt_i2c_regs __iomem *
cobalt_i2c_regs(struct cobalt *cobalt, unsigned idx)
{
	switch (idx) {
	case 0:
	default:
		return (struct cobalt_i2c_regs __iomem *)
			(cobalt->bar1 + COBALT_I2C_0_BASE);
	case 1:
		return (struct cobalt_i2c_regs __iomem *)
			(cobalt->bar1 + COBALT_I2C_1_BASE);
	case 2:
		return (struct cobalt_i2c_regs __iomem *)
			(cobalt->bar1 + COBALT_I2C_2_BASE);
	case 3:
		return (struct cobalt_i2c_regs __iomem *)
			(cobalt->bar1 + COBALT_I2C_3_BASE);
	case 4:
		return (struct cobalt_i2c_regs __iomem *)
			(cobalt->bar1 + COBALT_I2C_HSMA_BASE);
	}
}

/* Do low-level i2c byte transfer.
 * Returns -1 in case of an error or 0 otherwise.
 */
static int cobalt_tx_bytes(struct cobalt_i2c_regs __iomem *regs,
		struct i2c_adapter *adap, bool start, bool stop,
		u8 *data, u16 len)
{
	unsigned long start_time;
	int status;
	int cmd;
	int i;

	for (i = 0; i < len; i++) {
		/* Setup data */
		iowrite8(data[i], &regs->txr_rxr);

		/* Setup command */
		if (i == 0 && start != 0) {
			/* Write + Start */
			cmd = M00018_CR_BITMAP_WR_MSK |
			      M00018_CR_BITMAP_STA_MSK;
		} else if (i == len - 1 && stop != 0) {
			/* Write + Stop */
			cmd = M00018_CR_BITMAP_WR_MSK |
			      M00018_CR_BITMAP_STO_MSK;
		} else {
			/* Write only */
			cmd = M00018_CR_BITMAP_WR_MSK;
		}

		/* Execute command */
		iowrite8(cmd, &regs->cr_sr);

		/* Wait for transfer to complete (TIP = 0) */
		start_time = jiffies;
		status = ioread8(&regs->cr_sr);
		while (status & M00018_SR_BITMAP_TIP_MSK) {
			if (time_after(jiffies, start_time + adap->timeout))
				return -ETIMEDOUT;
			cond_resched();
			status = ioread8(&regs->cr_sr);
		}

		/* Verify ACK */
		if (status & M00018_SR_BITMAP_RXACK_MSK) {
			/* NO ACK! */
			return -EIO;
		}

		/* Verify arbitration */
		if (status & M00018_SR_BITMAP_AL_MSK) {
			/* Arbitration lost! */
			return -EIO;
		}
	}
	return 0;
}

/* Do low-level i2c byte read.
 * Returns -1 in case of an error or 0 otherwise.
 */
static int cobalt_rx_bytes(struct cobalt_i2c_regs __iomem *regs,
		struct i2c_adapter *adap, bool start, bool stop,
		u8 *data, u16 len)
{
	unsigned long start_time;
	int status;
	int cmd;
	int i;

	for (i = 0; i < len; i++) {
		/* Setup command */
		if (i == 0 && start != 0) {
			/* Read + Start */
			cmd = M00018_CR_BITMAP_RD_MSK |
			      M00018_CR_BITMAP_STA_MSK;
		} else if (i == len - 1 && stop != 0) {
			/* Read + Stop */
			cmd = M00018_CR_BITMAP_RD_MSK |
			      M00018_CR_BITMAP_STO_MSK;
		} else {
			/* Read only */
			cmd = M00018_CR_BITMAP_RD_MSK;
		}

		/* Last byte to read, no ACK */
		if (i == len - 1)
			cmd |= M00018_CR_BITMAP_ACK_MSK;

		/* Execute command */
		iowrite8(cmd, &regs->cr_sr);

		/* Wait for transfer to complete (TIP = 0) */
		start_time = jiffies;
		status = ioread8(&regs->cr_sr);
		while (status & M00018_SR_BITMAP_TIP_MSK) {
			if (time_after(jiffies, start_time + adap->timeout))
				return -ETIMEDOUT;
			cond_resched();
			status = ioread8(&regs->cr_sr);
		}

		/* Verify arbitration */
		if (status & M00018_SR_BITMAP_AL_MSK) {
			/* Arbitration lost! */
			return -EIO;
		}

		/* Store data */
		data[i] = ioread8(&regs->txr_rxr);
	}
	return 0;
}

/* Generate stop condition on i2c bus.
 * The m00018 stop isn't doing the right thing (wrong timing).
 * So instead send a start condition, 8 zeroes and a stop condition.
 */
static int cobalt_stop(struct cobalt_i2c_regs __iomem *regs,
		struct i2c_adapter *adap)
{
	u8 data = 0;

	return cobalt_tx_bytes(regs, adap, true, true, &data, 1);
}

static int cobalt_xfer(struct i2c_adapter *adap,
			struct i2c_msg msgs[], int num)
{
	struct cobalt_i2c_data *data = adap->algo_data;
	struct cobalt_i2c_regs __iomem *regs = data->regs;
	struct i2c_msg *pmsg;
	unsigned short flags;
	int ret = 0;
	int i, j;

	for (i = 0; i < num; i++) {
		int stop = (i == num - 1);

		pmsg = &msgs[i];
		flags = pmsg->flags;

		if (!(pmsg->flags & I2C_M_NOSTART)) {
			u8 addr = pmsg->addr << 1;

			if (flags & I2C_M_RD)
				addr |= 1;
			if (flags & I2C_M_REV_DIR_ADDR)
				addr ^= 1;
			for (j = 0; j < adap->retries; j++) {
				ret = cobalt_tx_bytes(regs, adap, true, false,
						      &addr, 1);
				if (!ret)
					break;
				cobalt_stop(regs, adap);
			}
			if (ret < 0)
				return ret;
			ret = 0;
		}
		if (pmsg->flags & I2C_M_RD) {
			/* read bytes into buffer */
			ret = cobalt_rx_bytes(regs, adap, false, stop,
					pmsg->buf, pmsg->len);
			if (ret < 0)
				goto bailout;
		} else {
			/* write bytes from buffer */
			ret = cobalt_tx_bytes(regs, adap, false, stop,
					pmsg->buf, pmsg->len);
			if (ret < 0)
				goto bailout;
		}
	}
	ret = i;

bailout:
	if (ret < 0)
		cobalt_stop(regs, adap);
	return ret;
}

static u32 cobalt_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

/* template for i2c-bit-algo */
static const struct i2c_adapter cobalt_i2c_adap_template = {
	.name = "cobalt i2c driver",
	.algo = NULL,                   /* set by i2c-algo-bit */
	.algo_data = NULL,              /* filled from template */
	.owner = THIS_MODULE,
};

static const struct i2c_algorithm cobalt_algo = {
	.master_xfer	= cobalt_xfer,
	.functionality	= cobalt_func,
};

/* init + register i2c algo-bit adapter */
int cobalt_i2c_init(struct cobalt *cobalt)
{
	int i, err;
	int status;
	int prescale;
	unsigned long start_time;

	cobalt_dbg(1, "i2c init\n");

	/* Define I2C clock prescaler */
	prescale = ((ALT_CPU_FREQ) / (5 * I2C_FREQUENCY)) - 1;

	for (i = 0; i < COBALT_NUM_ADAPTERS; i++) {
		struct cobalt_i2c_regs __iomem *regs =
			cobalt_i2c_regs(cobalt, i);
		struct i2c_adapter *adap = &cobalt->i2c_adap[i];

		/* Disable I2C */
		iowrite8(M00018_CTR_BITMAP_EN_MSK, &regs->cr_sr);
		iowrite8(0, &regs->ctr);
		iowrite8(0, &regs->cr_sr);

		start_time = jiffies;
		do {
			if (time_after(jiffies, start_time + HZ)) {
				if (cobalt_ignore_err) {
					adap->dev.parent = NULL;
					return 0;
				}
				return -ETIMEDOUT;
			}
			status = ioread8(&regs->cr_sr);
		} while (status & M00018_SR_BITMAP_TIP_MSK);

		/* Disable I2C */
		iowrite8(0, &regs->ctr);
		iowrite8(0, &regs->cr_sr);

		/* Calculate i2c prescaler */
		iowrite8(prescale & 0xff, &regs->prerlo);
		iowrite8((prescale >> 8) & 0xff, &regs->prerhi);
		/* Enable I2C, interrupts disabled */
		iowrite8(M00018_CTR_BITMAP_EN_MSK, &regs->ctr);
		/* Setup algorithm for adapter */
		cobalt->i2c_data[i].cobalt = cobalt;
		cobalt->i2c_data[i].regs = regs;
		*adap = cobalt_i2c_adap_template;
		adap->algo = &cobalt_algo;
		adap->algo_data = &cobalt->i2c_data[i];
		adap->retries = 3;
		sprintf(adap->name + strlen(adap->name),
				" #%d-%d", cobalt->instance, i);
		i2c_set_adapdata(adap, &cobalt->v4l2_dev);
		adap->dev.parent = &cobalt->pci_dev->dev;
		err = i2c_add_adapter(adap);
		if (err) {
			if (cobalt_ignore_err) {
				adap->dev.parent = NULL;
				return 0;
			}
			while (i--)
				i2c_del_adapter(&cobalt->i2c_adap[i]);
			return err;
		}
		cobalt_info("registered bus %s\n", adap->name);
	}
	return 0;
}

void cobalt_i2c_exit(struct cobalt *cobalt)
{
	int i;

	cobalt_dbg(1, "i2c exit\n");

	for (i = 0; i < COBALT_NUM_ADAPTERS; i++) {
		cobalt_err("unregistered bus %s\n", cobalt->i2c_adap[i].name);
		i2c_del_adapter(&cobalt->i2c_adap[i]);
	}
}
