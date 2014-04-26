/*
 * AT86RF230/RF231 driver
 *
 * Copyright (C) 2009-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include <linux/spi/at86rf230.h>
#include <linux/skbuff.h>
#include <linux/of_gpio.h>

#include <net/mac802154.h>
#include <net/wpan-phy.h>

struct at86rf230_local {
	struct spi_device *spi;

	u8 part;
	u8 vers;

	u8 buf[2];
	struct mutex bmux;

	struct work_struct irqwork;
	struct completion tx_complete;

	struct ieee802154_dev *dev;

	spinlock_t lock;
	bool irq_busy;
	bool is_tx;
	bool tx_aret;

	int rssi_base_val;
};

static bool is_rf212(struct at86rf230_local *local)
{
	return local->part == 7;
}

#define	RG_TRX_STATUS	(0x01)
#define	SR_TRX_STATUS		0x01, 0x1f, 0
#define	SR_RESERVED_01_3	0x01, 0x20, 5
#define	SR_CCA_STATUS		0x01, 0x40, 6
#define	SR_CCA_DONE		0x01, 0x80, 7
#define	RG_TRX_STATE	(0x02)
#define	SR_TRX_CMD		0x02, 0x1f, 0
#define	SR_TRAC_STATUS		0x02, 0xe0, 5
#define	RG_TRX_CTRL_0	(0x03)
#define	SR_CLKM_CTRL		0x03, 0x07, 0
#define	SR_CLKM_SHA_SEL		0x03, 0x08, 3
#define	SR_PAD_IO_CLKM		0x03, 0x30, 4
#define	SR_PAD_IO		0x03, 0xc0, 6
#define	RG_TRX_CTRL_1	(0x04)
#define	SR_IRQ_POLARITY		0x04, 0x01, 0
#define	SR_IRQ_MASK_MODE	0x04, 0x02, 1
#define	SR_SPI_CMD_MODE		0x04, 0x0c, 2
#define	SR_RX_BL_CTRL		0x04, 0x10, 4
#define	SR_TX_AUTO_CRC_ON	0x04, 0x20, 5
#define	SR_IRQ_2_EXT_EN		0x04, 0x40, 6
#define	SR_PA_EXT_EN		0x04, 0x80, 7
#define	RG_PHY_TX_PWR	(0x05)
#define	SR_TX_PWR		0x05, 0x0f, 0
#define	SR_PA_LT		0x05, 0x30, 4
#define	SR_PA_BUF_LT		0x05, 0xc0, 6
#define	RG_PHY_RSSI	(0x06)
#define	SR_RSSI			0x06, 0x1f, 0
#define	SR_RND_VALUE		0x06, 0x60, 5
#define	SR_RX_CRC_VALID		0x06, 0x80, 7
#define	RG_PHY_ED_LEVEL	(0x07)
#define	SR_ED_LEVEL		0x07, 0xff, 0
#define	RG_PHY_CC_CCA	(0x08)
#define	SR_CHANNEL		0x08, 0x1f, 0
#define	SR_CCA_MODE		0x08, 0x60, 5
#define	SR_CCA_REQUEST		0x08, 0x80, 7
#define	RG_CCA_THRES	(0x09)
#define	SR_CCA_ED_THRES		0x09, 0x0f, 0
#define	SR_RESERVED_09_1	0x09, 0xf0, 4
#define	RG_RX_CTRL	(0x0a)
#define	SR_PDT_THRES		0x0a, 0x0f, 0
#define	SR_RESERVED_0a_1	0x0a, 0xf0, 4
#define	RG_SFD_VALUE	(0x0b)
#define	SR_SFD_VALUE		0x0b, 0xff, 0
#define	RG_TRX_CTRL_2	(0x0c)
#define	SR_OQPSK_DATA_RATE	0x0c, 0x03, 0
#define	SR_SUB_MODE		0x0c, 0x04, 2
#define	SR_BPSK_QPSK		0x0c, 0x08, 3
#define	SR_OQPSK_SUB1_RC_EN	0x0c, 0x10, 4
#define	SR_RESERVED_0c_5	0x0c, 0x60, 5
#define	SR_RX_SAFE_MODE		0x0c, 0x80, 7
#define	RG_ANT_DIV	(0x0d)
#define	SR_ANT_CTRL		0x0d, 0x03, 0
#define	SR_ANT_EXT_SW_EN	0x0d, 0x04, 2
#define	SR_ANT_DIV_EN		0x0d, 0x08, 3
#define	SR_RESERVED_0d_2	0x0d, 0x70, 4
#define	SR_ANT_SEL		0x0d, 0x80, 7
#define	RG_IRQ_MASK	(0x0e)
#define	SR_IRQ_MASK		0x0e, 0xff, 0
#define	RG_IRQ_STATUS	(0x0f)
#define	SR_IRQ_0_PLL_LOCK	0x0f, 0x01, 0
#define	SR_IRQ_1_PLL_UNLOCK	0x0f, 0x02, 1
#define	SR_IRQ_2_RX_START	0x0f, 0x04, 2
#define	SR_IRQ_3_TRX_END	0x0f, 0x08, 3
#define	SR_IRQ_4_CCA_ED_DONE	0x0f, 0x10, 4
#define	SR_IRQ_5_AMI		0x0f, 0x20, 5
#define	SR_IRQ_6_TRX_UR		0x0f, 0x40, 6
#define	SR_IRQ_7_BAT_LOW	0x0f, 0x80, 7
#define	RG_VREG_CTRL	(0x10)
#define	SR_RESERVED_10_6	0x10, 0x03, 0
#define	SR_DVDD_OK		0x10, 0x04, 2
#define	SR_DVREG_EXT		0x10, 0x08, 3
#define	SR_RESERVED_10_3	0x10, 0x30, 4
#define	SR_AVDD_OK		0x10, 0x40, 6
#define	SR_AVREG_EXT		0x10, 0x80, 7
#define	RG_BATMON	(0x11)
#define	SR_BATMON_VTH		0x11, 0x0f, 0
#define	SR_BATMON_HR		0x11, 0x10, 4
#define	SR_BATMON_OK		0x11, 0x20, 5
#define	SR_RESERVED_11_1	0x11, 0xc0, 6
#define	RG_XOSC_CTRL	(0x12)
#define	SR_XTAL_TRIM		0x12, 0x0f, 0
#define	SR_XTAL_MODE		0x12, 0xf0, 4
#define	RG_RX_SYN	(0x15)
#define	SR_RX_PDT_LEVEL		0x15, 0x0f, 0
#define	SR_RESERVED_15_2	0x15, 0x70, 4
#define	SR_RX_PDT_DIS		0x15, 0x80, 7
#define	RG_XAH_CTRL_1	(0x17)
#define	SR_RESERVED_17_8	0x17, 0x01, 0
#define	SR_AACK_PROM_MODE	0x17, 0x02, 1
#define	SR_AACK_ACK_TIME	0x17, 0x04, 2
#define	SR_RESERVED_17_5	0x17, 0x08, 3
#define	SR_AACK_UPLD_RES_FT	0x17, 0x10, 4
#define	SR_AACK_FLTR_RES_FT	0x17, 0x20, 5
#define	SR_CSMA_LBT_MODE	0x17, 0x40, 6
#define	SR_RESERVED_17_1	0x17, 0x80, 7
#define	RG_FTN_CTRL	(0x18)
#define	SR_RESERVED_18_2	0x18, 0x7f, 0
#define	SR_FTN_START		0x18, 0x80, 7
#define	RG_PLL_CF	(0x1a)
#define	SR_RESERVED_1a_2	0x1a, 0x7f, 0
#define	SR_PLL_CF_START		0x1a, 0x80, 7
#define	RG_PLL_DCU	(0x1b)
#define	SR_RESERVED_1b_3	0x1b, 0x3f, 0
#define	SR_RESERVED_1b_2	0x1b, 0x40, 6
#define	SR_PLL_DCU_START	0x1b, 0x80, 7
#define	RG_PART_NUM	(0x1c)
#define	SR_PART_NUM		0x1c, 0xff, 0
#define	RG_VERSION_NUM	(0x1d)
#define	SR_VERSION_NUM		0x1d, 0xff, 0
#define	RG_MAN_ID_0	(0x1e)
#define	SR_MAN_ID_0		0x1e, 0xff, 0
#define	RG_MAN_ID_1	(0x1f)
#define	SR_MAN_ID_1		0x1f, 0xff, 0
#define	RG_SHORT_ADDR_0	(0x20)
#define	SR_SHORT_ADDR_0		0x20, 0xff, 0
#define	RG_SHORT_ADDR_1	(0x21)
#define	SR_SHORT_ADDR_1		0x21, 0xff, 0
#define	RG_PAN_ID_0	(0x22)
#define	SR_PAN_ID_0		0x22, 0xff, 0
#define	RG_PAN_ID_1	(0x23)
#define	SR_PAN_ID_1		0x23, 0xff, 0
#define	RG_IEEE_ADDR_0	(0x24)
#define	SR_IEEE_ADDR_0		0x24, 0xff, 0
#define	RG_IEEE_ADDR_1	(0x25)
#define	SR_IEEE_ADDR_1		0x25, 0xff, 0
#define	RG_IEEE_ADDR_2	(0x26)
#define	SR_IEEE_ADDR_2		0x26, 0xff, 0
#define	RG_IEEE_ADDR_3	(0x27)
#define	SR_IEEE_ADDR_3		0x27, 0xff, 0
#define	RG_IEEE_ADDR_4	(0x28)
#define	SR_IEEE_ADDR_4		0x28, 0xff, 0
#define	RG_IEEE_ADDR_5	(0x29)
#define	SR_IEEE_ADDR_5		0x29, 0xff, 0
#define	RG_IEEE_ADDR_6	(0x2a)
#define	SR_IEEE_ADDR_6		0x2a, 0xff, 0
#define	RG_IEEE_ADDR_7	(0x2b)
#define	SR_IEEE_ADDR_7		0x2b, 0xff, 0
#define	RG_XAH_CTRL_0	(0x2c)
#define	SR_SLOTTED_OPERATION	0x2c, 0x01, 0
#define	SR_MAX_CSMA_RETRIES	0x2c, 0x0e, 1
#define	SR_MAX_FRAME_RETRIES	0x2c, 0xf0, 4
#define	RG_CSMA_SEED_0	(0x2d)
#define	SR_CSMA_SEED_0		0x2d, 0xff, 0
#define	RG_CSMA_SEED_1	(0x2e)
#define	SR_CSMA_SEED_1		0x2e, 0x07, 0
#define	SR_AACK_I_AM_COORD	0x2e, 0x08, 3
#define	SR_AACK_DIS_ACK		0x2e, 0x10, 4
#define	SR_AACK_SET_PD		0x2e, 0x20, 5
#define	SR_AACK_FVN_MODE	0x2e, 0xc0, 6
#define	RG_CSMA_BE	(0x2f)
#define	SR_MIN_BE		0x2f, 0x0f, 0
#define	SR_MAX_BE		0x2f, 0xf0, 4

#define CMD_REG		0x80
#define CMD_REG_MASK	0x3f
#define CMD_WRITE	0x40
#define CMD_FB		0x20

#define IRQ_BAT_LOW	(1 << 7)
#define IRQ_TRX_UR	(1 << 6)
#define IRQ_AMI		(1 << 5)
#define IRQ_CCA_ED	(1 << 4)
#define IRQ_TRX_END	(1 << 3)
#define IRQ_RX_START	(1 << 2)
#define IRQ_PLL_UNL	(1 << 1)
#define IRQ_PLL_LOCK	(1 << 0)

#define IRQ_ACTIVE_HIGH	0
#define IRQ_ACTIVE_LOW	1

#define STATE_P_ON		0x00	/* BUSY */
#define STATE_BUSY_RX		0x01
#define STATE_BUSY_TX		0x02
#define STATE_FORCE_TRX_OFF	0x03
#define STATE_FORCE_TX_ON	0x04	/* IDLE */
/* 0x05 */				/* INVALID_PARAMETER */
#define STATE_RX_ON		0x06
/* 0x07 */				/* SUCCESS */
#define STATE_TRX_OFF		0x08
#define STATE_TX_ON		0x09
/* 0x0a - 0x0e */			/* 0x0a - UNSUPPORTED_ATTRIBUTE */
#define STATE_SLEEP		0x0F
#define STATE_PREP_DEEP_SLEEP	0x10
#define STATE_BUSY_RX_AACK	0x11
#define STATE_BUSY_TX_ARET	0x12
#define STATE_RX_AACK_ON	0x16
#define STATE_TX_ARET_ON	0x19
#define STATE_RX_ON_NOCLK	0x1C
#define STATE_RX_AACK_ON_NOCLK	0x1D
#define STATE_BUSY_RX_AACK_NOCLK 0x1E
#define STATE_TRANSITION_IN_PROGRESS 0x1F

static int
__at86rf230_detect_device(struct spi_device *spi, u16 *man_id, u8 *part,
		u8 *version)
{
	u8 data[4];
	u8 *buf = kmalloc(2, GFP_KERNEL);
	int status;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf	= buf,
		.rx_buf	= buf,
	};
	u8 reg;

	if (!buf)
		return -ENOMEM;

	for (reg = RG_PART_NUM; reg <= RG_MAN_ID_1; reg++) {
		buf[0] = (reg & CMD_REG_MASK) | CMD_REG;
		buf[1] = 0xff;
		dev_vdbg(&spi->dev, "buf[0] = %02x\n", buf[0]);
		spi_message_init(&msg);
		spi_message_add_tail(&xfer, &msg);

		status = spi_sync(spi, &msg);
		dev_vdbg(&spi->dev, "status = %d\n", status);
		if (msg.status)
			status = msg.status;

		dev_vdbg(&spi->dev, "status = %d\n", status);
		dev_vdbg(&spi->dev, "buf[0] = %02x\n", buf[0]);
		dev_vdbg(&spi->dev, "buf[1] = %02x\n", buf[1]);

		if (status == 0)
			data[reg - RG_PART_NUM] = buf[1];
		else
			break;
	}

	if (status == 0) {
		*part = data[0];
		*version = data[1];
		*man_id = (data[3] << 8) | data[2];
	}

	kfree(buf);

	return status;
}

static int
__at86rf230_write(struct at86rf230_local *lp, u8 addr, u8 data)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf	= buf,
	};

	buf[0] = (addr & CMD_REG_MASK) | CMD_REG | CMD_WRITE;
	buf[1] = data;
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	status = spi_sync(lp->spi, &msg);
	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	if (msg.status)
		status = msg.status;

	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);

	return status;
}

static int
__at86rf230_read_subreg(struct at86rf230_local *lp,
			u8 addr, u8 mask, int shift, u8 *data)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf	= buf,
		.rx_buf	= buf,
	};

	buf[0] = (addr & CMD_REG_MASK) | CMD_REG;
	buf[1] = 0xff;
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	status = spi_sync(lp->spi, &msg);
	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	if (msg.status)
		status = msg.status;

	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);

	if (status == 0)
		*data = (buf[1] & mask) >> shift;

	return status;
}

static int
at86rf230_read_subreg(struct at86rf230_local *lp,
		      u8 addr, u8 mask, int shift, u8 *data)
{
	int status;

	mutex_lock(&lp->bmux);
	status = __at86rf230_read_subreg(lp, addr, mask, shift, data);
	mutex_unlock(&lp->bmux);

	return status;
}

static int
at86rf230_write_subreg(struct at86rf230_local *lp,
		       u8 addr, u8 mask, int shift, u8 data)
{
	int status;
	u8 val;

	mutex_lock(&lp->bmux);
	status = __at86rf230_read_subreg(lp, addr, 0xff, 0, &val);
	if (status)
		goto out;

	val &= ~mask;
	val |= (data << shift) & mask;

	status = __at86rf230_write(lp, addr, val);
out:
	mutex_unlock(&lp->bmux);

	return status;
}

static int
at86rf230_write_fbuf(struct at86rf230_local *lp, u8 *data, u8 len)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer_head = {
		.len		= 2,
		.tx_buf		= buf,

	};
	struct spi_transfer xfer_buf = {
		.len		= len,
		.tx_buf		= data,
	};

	mutex_lock(&lp->bmux);
	buf[0] = CMD_WRITE | CMD_FB;
	buf[1] = len + 2; /* 2 bytes for CRC that isn't written */

	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head, &msg);
	spi_message_add_tail(&xfer_buf, &msg);

	status = spi_sync(lp->spi, &msg);
	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	if (msg.status)
		status = msg.status;

	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);

	mutex_unlock(&lp->bmux);
	return status;
}

static int
at86rf230_read_fbuf(struct at86rf230_local *lp, u8 *data, u8 *len, u8 *lqi)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer_head = {
		.len		= 2,
		.tx_buf		= buf,
		.rx_buf		= buf,
	};
	struct spi_transfer xfer_head1 = {
		.len		= 2,
		.tx_buf		= buf,
		.rx_buf		= buf,
	};
	struct spi_transfer xfer_buf = {
		.len		= 0,
		.rx_buf		= data,
	};

	mutex_lock(&lp->bmux);

	buf[0] = CMD_FB;
	buf[1] = 0x00;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head, &msg);

	status = spi_sync(lp->spi, &msg);
	dev_vdbg(&lp->spi->dev, "status = %d\n", status);

	xfer_buf.len = *(buf + 1) + 1;
	*len = buf[1];

	buf[0] = CMD_FB;
	buf[1] = 0x00;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head1, &msg);
	spi_message_add_tail(&xfer_buf, &msg);

	status = spi_sync(lp->spi, &msg);

	if (msg.status)
		status = msg.status;

	dev_vdbg(&lp->spi->dev, "status = %d\n", status);
	dev_vdbg(&lp->spi->dev, "buf[0] = %02x\n", buf[0]);
	dev_vdbg(&lp->spi->dev, "buf[1] = %02x\n", buf[1]);

	if (status) {
		if (lqi && (*len > lp->buf[1]))
			*lqi = data[lp->buf[1]];
	}
	mutex_unlock(&lp->bmux);

	return status;
}

static int
at86rf230_ed(struct ieee802154_dev *dev, u8 *level)
{
	might_sleep();
	BUG_ON(!level);
	*level = 0xbe;
	return 0;
}

static int
at86rf230_state(struct ieee802154_dev *dev, int state)
{
	struct at86rf230_local *lp = dev->priv;
	int rc;
	u8 val;
	u8 desired_status;

	might_sleep();

	if (state == STATE_FORCE_TX_ON)
		desired_status = STATE_TX_ON;
	else if (state == STATE_FORCE_TRX_OFF)
		desired_status = STATE_TRX_OFF;
	else
		desired_status = state;

	do {
		rc = at86rf230_read_subreg(lp, SR_TRX_STATUS, &val);
		if (rc)
			goto err;
	} while (val == STATE_TRANSITION_IN_PROGRESS);

	if (val == desired_status)
		return 0;

	/* state is equal to phy states */
	rc = at86rf230_write_subreg(lp, SR_TRX_CMD, state);
	if (rc)
		goto err;

	do {
		rc = at86rf230_read_subreg(lp, SR_TRX_STATUS, &val);
		if (rc)
			goto err;
	} while (val == STATE_TRANSITION_IN_PROGRESS);


	if (val == desired_status ||
	    (desired_status == STATE_RX_ON && val == STATE_BUSY_RX) ||
	    (desired_status == STATE_RX_AACK_ON && val == STATE_BUSY_RX_AACK))
		return 0;

	pr_err("unexpected state change: %d, asked for %d\n", val, state);
	return -EBUSY;

err:
	pr_err("error: %d\n", rc);
	return rc;
}

static int
at86rf230_start(struct ieee802154_dev *dev)
{
	struct at86rf230_local *lp = dev->priv;
	u8 rc;

	rc = at86rf230_write_subreg(lp, SR_RX_SAFE_MODE, 1);
	if (rc)
		return rc;

	rc = at86rf230_state(dev, STATE_TX_ON);
	if (rc)
		return rc;

	return at86rf230_state(dev, STATE_RX_AACK_ON);
}

static void
at86rf230_stop(struct ieee802154_dev *dev)
{
	at86rf230_state(dev, STATE_FORCE_TRX_OFF);
}

static int
at86rf230_set_channel(struct at86rf230_local *lp, int page, int channel)
{
	lp->rssi_base_val = -91;

	return at86rf230_write_subreg(lp, SR_CHANNEL, channel);
}

static int
at86rf212_set_channel(struct at86rf230_local *lp, int page, int channel)
{
	int rc;

	if (channel == 0)
		rc = at86rf230_write_subreg(lp, SR_SUB_MODE, 0);
	else
		rc = at86rf230_write_subreg(lp, SR_SUB_MODE, 1);
	if (rc < 0)
		return rc;

	if (page == 0) {
		rc = at86rf230_write_subreg(lp, SR_BPSK_QPSK, 0);
		lp->rssi_base_val = -100;
	} else {
		rc = at86rf230_write_subreg(lp, SR_BPSK_QPSK, 1);
		lp->rssi_base_val = -98;
	}
	if (rc < 0)
		return rc;

	return at86rf230_write_subreg(lp, SR_CHANNEL, channel);
}

static int
at86rf230_channel(struct ieee802154_dev *dev, int page, int channel)
{
	struct at86rf230_local *lp = dev->priv;
	int rc;

	might_sleep();

	if (page < 0 || page > 31 ||
	    !(lp->dev->phy->channels_supported[page] & BIT(channel))) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (is_rf212(lp))
		rc = at86rf212_set_channel(lp, page, channel);
	else
		rc = at86rf230_set_channel(lp, page, channel);
	if (rc < 0)
		return rc;

	msleep(1); /* Wait for PLL */
	dev->phy->current_channel = channel;
	dev->phy->current_page = page;

	return 0;
}

static int
at86rf230_xmit(struct ieee802154_dev *dev, struct sk_buff *skb)
{
	struct at86rf230_local *lp = dev->priv;
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);
	if  (lp->irq_busy) {
		spin_unlock_irqrestore(&lp->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&lp->lock, flags);

	might_sleep();

	rc = at86rf230_state(dev, STATE_FORCE_TX_ON);
	if (rc)
		goto err;

	spin_lock_irqsave(&lp->lock, flags);
	lp->is_tx = 1;
	reinit_completion(&lp->tx_complete);
	spin_unlock_irqrestore(&lp->lock, flags);

	rc = at86rf230_write_fbuf(lp, skb->data, skb->len);
	if (rc)
		goto err_rx;

	if (lp->tx_aret) {
		rc = at86rf230_write_subreg(lp, SR_TRX_CMD, STATE_TX_ARET_ON);
		if (rc)
			goto err_rx;
	}

	rc = at86rf230_write_subreg(lp, SR_TRX_CMD, STATE_BUSY_TX);
	if (rc)
		goto err_rx;

	rc = wait_for_completion_interruptible(&lp->tx_complete);
	if (rc < 0)
		goto err_rx;

	return at86rf230_start(dev);
err_rx:
	at86rf230_start(dev);
err:
	pr_err("error: %d\n", rc);

	spin_lock_irqsave(&lp->lock, flags);
	lp->is_tx = 0;
	spin_unlock_irqrestore(&lp->lock, flags);

	return rc;
}

static int at86rf230_rx(struct at86rf230_local *lp)
{
	u8 len = 128, lqi = 0;
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);

	if (!skb)
		return -ENOMEM;

	if (at86rf230_read_fbuf(lp, skb_put(skb, len), &len, &lqi))
		goto err;

	if (len < 2)
		goto err;

	skb_trim(skb, len - 2); /* We do not put CRC into the frame */

	ieee802154_rx_irqsafe(lp->dev, skb, lqi);

	dev_dbg(&lp->spi->dev, "READ_FBUF: %d %x\n", len, lqi);

	return 0;
err:
	pr_debug("received frame is too small\n");

	kfree_skb(skb);
	return -EINVAL;
}

static int
at86rf230_set_hw_addr_filt(struct ieee802154_dev *dev,
			   struct ieee802154_hw_addr_filt *filt,
			   unsigned long changed)
{
	struct at86rf230_local *lp = dev->priv;

	if (changed & IEEE802515_AFILT_SADDR_CHANGED) {
		u16 addr = le16_to_cpu(filt->short_addr);

		dev_vdbg(&lp->spi->dev,
			"at86rf230_set_hw_addr_filt called for saddr\n");
		__at86rf230_write(lp, RG_SHORT_ADDR_0, addr);
		__at86rf230_write(lp, RG_SHORT_ADDR_1, addr >> 8);
	}

	if (changed & IEEE802515_AFILT_PANID_CHANGED) {
		u16 pan = le16_to_cpu(filt->pan_id);

		dev_vdbg(&lp->spi->dev,
			"at86rf230_set_hw_addr_filt called for pan id\n");
		__at86rf230_write(lp, RG_PAN_ID_0, pan);
		__at86rf230_write(lp, RG_PAN_ID_1, pan >> 8);
	}

	if (changed & IEEE802515_AFILT_IEEEADDR_CHANGED) {
		u8 i, addr[8];

		memcpy(addr, &filt->ieee_addr, 8);
		dev_vdbg(&lp->spi->dev,
			"at86rf230_set_hw_addr_filt called for IEEE addr\n");
		for (i = 0; i < 8; i++)
			__at86rf230_write(lp, RG_IEEE_ADDR_0 + i, addr[i]);
	}

	if (changed & IEEE802515_AFILT_PANC_CHANGED) {
		dev_vdbg(&lp->spi->dev,
			"at86rf230_set_hw_addr_filt called for panc change\n");
		if (filt->pan_coord)
			at86rf230_write_subreg(lp, SR_AACK_I_AM_COORD, 1);
		else
			at86rf230_write_subreg(lp, SR_AACK_I_AM_COORD, 0);
	}

	return 0;
}

static int
at86rf212_set_txpower(struct ieee802154_dev *dev, int db)
{
	struct at86rf230_local *lp = dev->priv;

	/* typical maximum output is 5dBm with RG_PHY_TX_PWR 0x60, lower five
	 * bits decrease power in 1dB steps. 0x60 represents extra PA gain of
	 * 0dB.
	 * thus, supported values for db range from -26 to 5, for 31dB of
	 * reduction to 0dB of reduction.
	 */
	if (db > 5 || db < -26)
		return -EINVAL;

	db = -(db - 5);

	return __at86rf230_write(lp, RG_PHY_TX_PWR, 0x60 | db);
}

static int
at86rf212_set_lbt(struct ieee802154_dev *dev, bool on)
{
	struct at86rf230_local *lp = dev->priv;

	return at86rf230_write_subreg(lp, SR_CSMA_LBT_MODE, on);
}

static int
at86rf212_set_cca_mode(struct ieee802154_dev *dev, u8 mode)
{
	struct at86rf230_local *lp = dev->priv;

	return at86rf230_write_subreg(lp, SR_CCA_MODE, mode);
}

static int
at86rf212_set_cca_ed_level(struct ieee802154_dev *dev, s32 level)
{
	struct at86rf230_local *lp = dev->priv;
	int desens_steps;

	if (level < lp->rssi_base_val || level > 30)
		return -EINVAL;

	desens_steps = (level - lp->rssi_base_val) * 100 / 207;

	return at86rf230_write_subreg(lp, SR_CCA_ED_THRES, desens_steps);
}

static int
at86rf212_set_csma_params(struct ieee802154_dev *dev, u8 min_be, u8 max_be,
			  u8 retries)
{
	struct at86rf230_local *lp = dev->priv;
	int rc;

	if (min_be > max_be || max_be > 8 || retries > 5)
		return -EINVAL;

	rc = at86rf230_write_subreg(lp, SR_MIN_BE, min_be);
	if (rc)
		return rc;

	rc = at86rf230_write_subreg(lp, SR_MAX_BE, max_be);
	if (rc)
		return rc;

	return at86rf230_write_subreg(lp, SR_MAX_CSMA_RETRIES, retries);
}

static int
at86rf212_set_frame_retries(struct ieee802154_dev *dev, s8 retries)
{
	struct at86rf230_local *lp = dev->priv;
	int rc = 0;

	if (retries < -1 || retries > 15)
		return -EINVAL;

	lp->tx_aret = retries >= 0;

	if (retries >= 0)
		rc = at86rf230_write_subreg(lp, SR_MAX_FRAME_RETRIES, retries);

	return rc;
}

static struct ieee802154_ops at86rf230_ops = {
	.owner = THIS_MODULE,
	.xmit = at86rf230_xmit,
	.ed = at86rf230_ed,
	.set_channel = at86rf230_channel,
	.start = at86rf230_start,
	.stop = at86rf230_stop,
	.set_hw_addr_filt = at86rf230_set_hw_addr_filt,
};

static struct ieee802154_ops at86rf212_ops = {
	.owner = THIS_MODULE,
	.xmit = at86rf230_xmit,
	.ed = at86rf230_ed,
	.set_channel = at86rf230_channel,
	.start = at86rf230_start,
	.stop = at86rf230_stop,
	.set_hw_addr_filt = at86rf230_set_hw_addr_filt,
	.set_txpower = at86rf212_set_txpower,
	.set_lbt = at86rf212_set_lbt,
	.set_cca_mode = at86rf212_set_cca_mode,
	.set_cca_ed_level = at86rf212_set_cca_ed_level,
	.set_csma_params = at86rf212_set_csma_params,
	.set_frame_retries = at86rf212_set_frame_retries,
};

static void at86rf230_irqwork(struct work_struct *work)
{
	struct at86rf230_local *lp =
		container_of(work, struct at86rf230_local, irqwork);
	u8 status = 0, val;
	int rc;
	unsigned long flags;

	rc = at86rf230_read_subreg(lp, RG_IRQ_STATUS, 0xff, 0, &val);
	status |= val;

	status &= ~IRQ_PLL_LOCK; /* ignore */
	status &= ~IRQ_RX_START; /* ignore */
	status &= ~IRQ_AMI; /* ignore */
	status &= ~IRQ_TRX_UR; /* FIXME: possibly handle ???*/

	if (status & IRQ_TRX_END) {
		status &= ~IRQ_TRX_END;
		spin_lock_irqsave(&lp->lock, flags);
		if (lp->is_tx) {
			lp->is_tx = 0;
			spin_unlock_irqrestore(&lp->lock, flags);
			complete(&lp->tx_complete);
		} else {
			spin_unlock_irqrestore(&lp->lock, flags);
			at86rf230_rx(lp);
		}
	}

	spin_lock_irqsave(&lp->lock, flags);
	lp->irq_busy = 0;
	spin_unlock_irqrestore(&lp->lock, flags);
}

static void at86rf230_irqwork_level(struct work_struct *work)
{
	struct at86rf230_local *lp =
		container_of(work, struct at86rf230_local, irqwork);

	at86rf230_irqwork(work);

	enable_irq(lp->spi->irq);
}

static irqreturn_t at86rf230_isr(int irq, void *data)
{
	struct at86rf230_local *lp = data;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);
	lp->irq_busy = 1;
	spin_unlock_irqrestore(&lp->lock, flags);

	schedule_work(&lp->irqwork);

	return IRQ_HANDLED;
}

static irqreturn_t at86rf230_isr_level(int irq, void *data)
{
	disable_irq_nosync(irq);

	return at86rf230_isr(irq, data);
}

static int at86rf230_hw_init(struct at86rf230_local *lp)
{
	int rc, irq_pol, irq_type;
	u8 dvdd;
	u8 csma_seed[2];

	rc = at86rf230_write_subreg(lp, SR_TRX_CMD, STATE_FORCE_TRX_OFF);
	if (rc)
		return rc;

	irq_type = irq_get_trigger_type(lp->spi->irq);
	/* configure irq polarity, defaults to high active */
	if (irq_type & (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW))
		irq_pol = IRQ_ACTIVE_LOW;
	else
		irq_pol = IRQ_ACTIVE_HIGH;

	rc = at86rf230_write_subreg(lp, SR_IRQ_POLARITY, irq_pol);
	if (rc)
		return rc;

	rc = at86rf230_write_subreg(lp, SR_IRQ_MASK, IRQ_TRX_END);
	if (rc)
		return rc;

	get_random_bytes(csma_seed, ARRAY_SIZE(csma_seed));
	rc = at86rf230_write_subreg(lp, SR_CSMA_SEED_0, csma_seed[0]);
	if (rc)
		return rc;
	rc = at86rf230_write_subreg(lp, SR_CSMA_SEED_1, csma_seed[1]);
	if (rc)
		return rc;

	/* CLKM changes are applied immediately */
	rc = at86rf230_write_subreg(lp, SR_CLKM_SHA_SEL, 0x00);
	if (rc)
		return rc;

	/* Turn CLKM Off */
	rc = at86rf230_write_subreg(lp, SR_CLKM_CTRL, 0x00);
	if (rc)
		return rc;
	/* Wait the next SLEEP cycle */
	msleep(100);

	rc = at86rf230_read_subreg(lp, SR_DVDD_OK, &dvdd);
	if (rc)
		return rc;
	if (!dvdd) {
		dev_err(&lp->spi->dev, "DVDD error\n");
		return -EINVAL;
	}

	return 0;
}

static struct at86rf230_platform_data *
at86rf230_get_pdata(struct spi_device *spi)
{
	struct at86rf230_platform_data *pdata;

	if (!IS_ENABLED(CONFIG_OF) || !spi->dev.of_node)
		return spi->dev.platform_data;

	pdata = devm_kzalloc(&spi->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		goto done;

	pdata->rstn = of_get_named_gpio(spi->dev.of_node, "reset-gpio", 0);
	pdata->slp_tr = of_get_named_gpio(spi->dev.of_node, "sleep-gpio", 0);

	spi->dev.platform_data = pdata;
done:
	return pdata;
}

static int at86rf230_probe(struct spi_device *spi)
{
	struct at86rf230_platform_data *pdata;
	struct ieee802154_dev *dev;
	struct at86rf230_local *lp;
	u16 man_id = 0;
	u8 part = 0, version = 0, status;
	irq_handler_t irq_handler;
	work_func_t irq_worker;
	int rc, irq_type;
	const char *chip;
	struct ieee802154_ops *ops = NULL;

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ specified\n");
		return -EINVAL;
	}

	pdata = at86rf230_get_pdata(spi);
	if (!pdata) {
		dev_err(&spi->dev, "no platform_data\n");
		return -EINVAL;
	}

	if (gpio_is_valid(pdata->rstn)) {
		rc = devm_gpio_request_one(&spi->dev, pdata->rstn,
					   GPIOF_OUT_INIT_HIGH, "rstn");
		if (rc)
			return rc;
	}

	if (gpio_is_valid(pdata->slp_tr)) {
		rc = devm_gpio_request_one(&spi->dev, pdata->slp_tr,
					   GPIOF_OUT_INIT_LOW, "slp_tr");
		if (rc)
			return rc;
	}

	/* Reset */
	if (gpio_is_valid(pdata->rstn)) {
		udelay(1);
		gpio_set_value(pdata->rstn, 0);
		udelay(1);
		gpio_set_value(pdata->rstn, 1);
		usleep_range(120, 240);
	}

	rc = __at86rf230_detect_device(spi, &man_id, &part, &version);
	if (rc < 0)
		return rc;

	if (man_id != 0x001f) {
		dev_err(&spi->dev, "Non-Atmel dev found (MAN_ID %02x %02x)\n",
			man_id >> 8, man_id & 0xFF);
		return -EINVAL;
	}

	switch (part) {
	case 2:
		chip = "at86rf230";
		/* FIXME: should be easy to support; */
		break;
	case 3:
		chip = "at86rf231";
		ops = &at86rf230_ops;
		break;
	case 7:
		chip = "at86rf212";
		if (version == 1)
			ops = &at86rf212_ops;
		break;
	case 11:
		chip = "at86rf233";
		ops = &at86rf230_ops;
		break;
	default:
		chip = "UNKNOWN";
		break;
	}

	dev_info(&spi->dev, "Detected %s chip version %d\n", chip, version);
	if (!ops)
		return -ENOTSUPP;

	dev = ieee802154_alloc_device(sizeof(*lp), ops);
	if (!dev)
		return -ENOMEM;

	lp = dev->priv;
	lp->dev = dev;
	lp->part = part;
	lp->vers = version;

	lp->spi = spi;

	dev->parent = &spi->dev;
	dev->extra_tx_headroom = 0;
	dev->flags = IEEE802154_HW_OMIT_CKSUM | IEEE802154_HW_AACK;

	irq_type = irq_get_trigger_type(spi->irq);
	if (irq_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		irq_worker = at86rf230_irqwork;
		irq_handler = at86rf230_isr;
	} else {
		irq_worker = at86rf230_irqwork_level;
		irq_handler = at86rf230_isr_level;
	}

	mutex_init(&lp->bmux);
	INIT_WORK(&lp->irqwork, irq_worker);
	spin_lock_init(&lp->lock);
	init_completion(&lp->tx_complete);

	spi_set_drvdata(spi, lp);

	if (is_rf212(lp)) {
		dev->phy->channels_supported[0] = 0x00007FF;
		dev->phy->channels_supported[2] = 0x00007FF;
	} else {
		dev->phy->channels_supported[0] = 0x7FFF800;
	}

	rc = at86rf230_hw_init(lp);
	if (rc)
		goto err_hw_init;

	/* Read irq status register to reset irq line */
	rc = at86rf230_read_subreg(lp, RG_IRQ_STATUS, 0xff, 0, &status);
	if (rc)
		goto err_hw_init;

	rc = devm_request_irq(&spi->dev, spi->irq, irq_handler, IRQF_SHARED,
			      dev_name(&spi->dev), lp);
	if (rc)
		goto err_hw_init;

	rc = ieee802154_register_device(lp->dev);
	if (rc)
		goto err_hw_init;

	return rc;

err_hw_init:
	flush_work(&lp->irqwork);
	mutex_destroy(&lp->bmux);
	ieee802154_free_device(lp->dev);

	return rc;
}

static int at86rf230_remove(struct spi_device *spi)
{
	struct at86rf230_local *lp = spi_get_drvdata(spi);

	/* mask all at86rf230 irq's */
	at86rf230_write_subreg(lp, SR_IRQ_MASK, 0);
	ieee802154_unregister_device(lp->dev);
	flush_work(&lp->irqwork);
	mutex_destroy(&lp->bmux);
	ieee802154_free_device(lp->dev);
	dev_dbg(&spi->dev, "unregistered at86rf230\n");

	return 0;
}

static const struct of_device_id at86rf230_of_match[] = {
	{ .compatible = "atmel,at86rf230", },
	{ .compatible = "atmel,at86rf231", },
	{ .compatible = "atmel,at86rf233", },
	{ .compatible = "atmel,at86rf212", },
	{ },
};
MODULE_DEVICE_TABLE(of, at86rf230_of_match);

static const struct spi_device_id at86rf230_device_id[] = {
	{ .name = "at86rf230", },
	{ .name = "at86rf231", },
	{ .name = "at86rf233", },
	{ .name = "at86rf212", },
	{ },
};
MODULE_DEVICE_TABLE(spi, at86rf230_device_id);

static struct spi_driver at86rf230_driver = {
	.id_table = at86rf230_device_id,
	.driver = {
		.of_match_table = of_match_ptr(at86rf230_of_match),
		.name	= "at86rf230",
		.owner	= THIS_MODULE,
	},
	.probe      = at86rf230_probe,
	.remove     = at86rf230_remove,
};

module_spi_driver(at86rf230_driver);

MODULE_DESCRIPTION("AT86RF230 Transceiver Driver");
MODULE_LICENSE("GPL v2");
