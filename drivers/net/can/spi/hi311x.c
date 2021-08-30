// SPDX-License-Identifier: GPL-2.0-only
/* CAN bus driver for Holt HI3110 CAN Controller with SPI Interface
 *
 * Copyright(C) Timesys Corporation 2016
 *
 * Based on Microchip 251x CAN Controller (mcp251x) Linux kernel driver
 * Copyright 2009 Christian Pellegrin EVOL S.r.l.
 * Copyright 2007 Raymarine UK, Ltd. All Rights Reserved.
 * Copyright 2006 Arcom Control Systems Ltd.
 *
 * Based on CAN bus driver for the CCAN controller written by
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix
 * - Simon Kallweit, intefo AG
 * Copyright 2007
 */

#include <linux/can/core.h>
#include <linux/can/dev.h>
#include <linux/can/led.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define HI3110_MASTER_RESET 0x56
#define HI3110_READ_CTRL0 0xD2
#define HI3110_READ_CTRL1 0xD4
#define HI3110_READ_STATF 0xE2
#define HI3110_WRITE_CTRL0 0x14
#define HI3110_WRITE_CTRL1 0x16
#define HI3110_WRITE_INTE 0x1C
#define HI3110_WRITE_BTR0 0x18
#define HI3110_WRITE_BTR1 0x1A
#define HI3110_READ_BTR0 0xD6
#define HI3110_READ_BTR1 0xD8
#define HI3110_READ_INTF 0xDE
#define HI3110_READ_ERR 0xDC
#define HI3110_READ_FIFO_WOTIME 0x48
#define HI3110_WRITE_FIFO 0x12
#define HI3110_READ_MESSTAT 0xDA
#define HI3110_READ_REC 0xEA
#define HI3110_READ_TEC 0xEC

#define HI3110_CTRL0_MODE_MASK (7 << 5)
#define HI3110_CTRL0_NORMAL_MODE (0 << 5)
#define HI3110_CTRL0_LOOPBACK_MODE (1 << 5)
#define HI3110_CTRL0_MONITOR_MODE (2 << 5)
#define HI3110_CTRL0_SLEEP_MODE (3 << 5)
#define HI3110_CTRL0_INIT_MODE (4 << 5)

#define HI3110_CTRL1_TXEN BIT(7)

#define HI3110_INT_RXTMP BIT(7)
#define HI3110_INT_RXFIFO BIT(6)
#define HI3110_INT_TXCPLT BIT(5)
#define HI3110_INT_BUSERR BIT(4)
#define HI3110_INT_MCHG BIT(3)
#define HI3110_INT_WAKEUP BIT(2)
#define HI3110_INT_F1MESS BIT(1)
#define HI3110_INT_F0MESS BIT(0)

#define HI3110_ERR_BUSOFF BIT(7)
#define HI3110_ERR_TXERRP BIT(6)
#define HI3110_ERR_RXERRP BIT(5)
#define HI3110_ERR_BITERR BIT(4)
#define HI3110_ERR_FRMERR BIT(3)
#define HI3110_ERR_CRCERR BIT(2)
#define HI3110_ERR_ACKERR BIT(1)
#define HI3110_ERR_STUFERR BIT(0)
#define HI3110_ERR_PROTOCOL_MASK (0x1F)
#define HI3110_ERR_PASSIVE_MASK (0x60)

#define HI3110_STAT_RXFMTY BIT(1)
#define HI3110_STAT_BUSOFF BIT(2)
#define HI3110_STAT_ERRP BIT(3)
#define HI3110_STAT_ERRW BIT(4)
#define HI3110_STAT_TXMTY BIT(7)

#define HI3110_BTR0_SJW_SHIFT 6
#define HI3110_BTR0_BRP_SHIFT 0

#define HI3110_BTR1_SAMP_3PERBIT (1 << 7)
#define HI3110_BTR1_SAMP_1PERBIT (0 << 7)
#define HI3110_BTR1_TSEG2_SHIFT 4
#define HI3110_BTR1_TSEG1_SHIFT 0

#define HI3110_FIFO_WOTIME_TAG_OFF 0
#define HI3110_FIFO_WOTIME_ID_OFF 1
#define HI3110_FIFO_WOTIME_DLC_OFF 5
#define HI3110_FIFO_WOTIME_DAT_OFF 6

#define HI3110_FIFO_WOTIME_TAG_IDE BIT(7)
#define HI3110_FIFO_WOTIME_ID_RTR BIT(0)

#define HI3110_FIFO_TAG_OFF 0
#define HI3110_FIFO_ID_OFF 1
#define HI3110_FIFO_STD_DLC_OFF 3
#define HI3110_FIFO_STD_DATA_OFF 4
#define HI3110_FIFO_EXT_DLC_OFF 5
#define HI3110_FIFO_EXT_DATA_OFF 6

#define HI3110_CAN_MAX_DATA_LEN 8
#define HI3110_RX_BUF_LEN 15
#define HI3110_TX_STD_BUF_LEN 12
#define HI3110_TX_EXT_BUF_LEN 14
#define HI3110_CAN_FRAME_MAX_BITS 128
#define HI3110_EFF_FLAGS 0x18 /* IDE + SRR */

#define HI3110_TX_ECHO_SKB_MAX 1

#define HI3110_OST_DELAY_MS (10)

#define DEVICE_NAME "hi3110"

static const struct can_bittiming_const hi3110_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 2,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

enum hi3110_model {
	CAN_HI3110_HI3110 = 0x3110,
};

struct hi3110_priv {
	struct can_priv can;
	struct net_device *net;
	struct spi_device *spi;
	enum hi3110_model model;

	struct mutex hi3110_lock; /* SPI device lock */

	u8 *spi_tx_buf;
	u8 *spi_rx_buf;

	struct sk_buff *tx_skb;
	int tx_len;

	struct workqueue_struct *wq;
	struct work_struct tx_work;
	struct work_struct restart_work;

	int force_quit;
	int after_suspend;
#define HI3110_AFTER_SUSPEND_UP 1
#define HI3110_AFTER_SUSPEND_DOWN 2
#define HI3110_AFTER_SUSPEND_POWER 4
#define HI3110_AFTER_SUSPEND_RESTART 8
	int restart_tx;
	struct regulator *power;
	struct regulator *transceiver;
	struct clk *clk;
};

static void hi3110_clean(struct net_device *net)
{
	struct hi3110_priv *priv = netdev_priv(net);

	if (priv->tx_skb || priv->tx_len)
		net->stats.tx_errors++;
	dev_kfree_skb(priv->tx_skb);
	if (priv->tx_len)
		can_free_echo_skb(priv->net, 0, NULL);
	priv->tx_skb = NULL;
	priv->tx_len = 0;
}

/* Note about handling of error return of hi3110_spi_trans: accessing
 * registers via SPI is not really different conceptually than using
 * normal I/O assembler instructions, although it's much more
 * complicated from a practical POV. So it's not advisable to always
 * check the return value of this function. Imagine that every
 * read{b,l}, write{b,l} and friends would be bracketed in "if ( < 0)
 * error();", it would be a great mess (well there are some situation
 * when exception handling C++ like could be useful after all). So we
 * just check that transfers are OK at the beginning of our
 * conversation with the chip and to avoid doing really nasty things
 * (like injecting bogus packets in the network stack).
 */
static int hi3110_spi_trans(struct spi_device *spi, int len)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	struct spi_transfer t = {
		.tx_buf = priv->spi_tx_buf,
		.rx_buf = priv->spi_rx_buf,
		.len = len,
		.cs_change = 0,
	};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spi_sync(spi, &m);

	if (ret)
		dev_err(&spi->dev, "spi transfer failed: ret = %d\n", ret);
	return ret;
}

static int hi3110_cmd(struct spi_device *spi, u8 command)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = command;
	dev_dbg(&spi->dev, "hi3110_cmd: %02X\n", command);

	return hi3110_spi_trans(spi, 1);
}

static u8 hi3110_read(struct spi_device *spi, u8 command)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	u8 val = 0;

	priv->spi_tx_buf[0] = command;
	hi3110_spi_trans(spi, 2);
	val = priv->spi_rx_buf[1];

	return val;
}

static void hi3110_write(struct spi_device *spi, u8 reg, u8 val)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = reg;
	priv->spi_tx_buf[1] = val;
	hi3110_spi_trans(spi, 2);
}

static void hi3110_hw_tx_frame(struct spi_device *spi, u8 *buf, int len)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = HI3110_WRITE_FIFO;
	memcpy(priv->spi_tx_buf + 1, buf, len);
	hi3110_spi_trans(spi, len + 1);
}

static void hi3110_hw_tx(struct spi_device *spi, struct can_frame *frame)
{
	u8 buf[HI3110_TX_EXT_BUF_LEN];

	buf[HI3110_FIFO_TAG_OFF] = 0;

	if (frame->can_id & CAN_EFF_FLAG) {
		/* Extended frame */
		buf[HI3110_FIFO_ID_OFF] = (frame->can_id & CAN_EFF_MASK) >> 21;
		buf[HI3110_FIFO_ID_OFF + 1] =
			(((frame->can_id & CAN_EFF_MASK) >> 13) & 0xe0) |
			HI3110_EFF_FLAGS |
			(((frame->can_id & CAN_EFF_MASK) >> 15) & 0x07);
		buf[HI3110_FIFO_ID_OFF + 2] =
			(frame->can_id & CAN_EFF_MASK) >> 7;
		buf[HI3110_FIFO_ID_OFF + 3] =
			((frame->can_id & CAN_EFF_MASK) << 1) |
			((frame->can_id & CAN_RTR_FLAG) ? 1 : 0);

		buf[HI3110_FIFO_EXT_DLC_OFF] = frame->len;

		memcpy(buf + HI3110_FIFO_EXT_DATA_OFF,
		       frame->data, frame->len);

		hi3110_hw_tx_frame(spi, buf, HI3110_TX_EXT_BUF_LEN -
				   (HI3110_CAN_MAX_DATA_LEN - frame->len));
	} else {
		/* Standard frame */
		buf[HI3110_FIFO_ID_OFF] =   (frame->can_id & CAN_SFF_MASK) >> 3;
		buf[HI3110_FIFO_ID_OFF + 1] =
			((frame->can_id & CAN_SFF_MASK) << 5) |
			((frame->can_id & CAN_RTR_FLAG) ? (1 << 4) : 0);

		buf[HI3110_FIFO_STD_DLC_OFF] = frame->len;

		memcpy(buf + HI3110_FIFO_STD_DATA_OFF,
		       frame->data, frame->len);

		hi3110_hw_tx_frame(spi, buf, HI3110_TX_STD_BUF_LEN -
				   (HI3110_CAN_MAX_DATA_LEN - frame->len));
	}
}

static void hi3110_hw_rx_frame(struct spi_device *spi, u8 *buf)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = HI3110_READ_FIFO_WOTIME;
	hi3110_spi_trans(spi, HI3110_RX_BUF_LEN);
	memcpy(buf, priv->spi_rx_buf + 1, HI3110_RX_BUF_LEN - 1);
}

static void hi3110_hw_rx(struct spi_device *spi)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	struct sk_buff *skb;
	struct can_frame *frame;
	u8 buf[HI3110_RX_BUF_LEN - 1];

	skb = alloc_can_skb(priv->net, &frame);
	if (!skb) {
		priv->net->stats.rx_dropped++;
		return;
	}

	hi3110_hw_rx_frame(spi, buf);
	if (buf[HI3110_FIFO_WOTIME_TAG_OFF] & HI3110_FIFO_WOTIME_TAG_IDE) {
		/* IDE is recessive (1), indicating extended 29-bit frame */
		frame->can_id = CAN_EFF_FLAG;
		frame->can_id |=
			(buf[HI3110_FIFO_WOTIME_ID_OFF] << 21) |
			(((buf[HI3110_FIFO_WOTIME_ID_OFF + 1] & 0xE0) >> 5) << 18) |
			((buf[HI3110_FIFO_WOTIME_ID_OFF + 1] & 0x07) << 15) |
			(buf[HI3110_FIFO_WOTIME_ID_OFF + 2] << 7) |
			(buf[HI3110_FIFO_WOTIME_ID_OFF + 3] >> 1);
	} else {
		/* IDE is dominant (0), frame indicating standard 11-bit */
		frame->can_id =
			(buf[HI3110_FIFO_WOTIME_ID_OFF] << 3) |
			((buf[HI3110_FIFO_WOTIME_ID_OFF + 1] & 0xE0) >> 5);
	}

	/* Data length */
	frame->len = can_cc_dlc2len(buf[HI3110_FIFO_WOTIME_DLC_OFF] & 0x0F);

	if (buf[HI3110_FIFO_WOTIME_ID_OFF + 3] & HI3110_FIFO_WOTIME_ID_RTR)
		frame->can_id |= CAN_RTR_FLAG;
	else
		memcpy(frame->data, buf + HI3110_FIFO_WOTIME_DAT_OFF,
		       frame->len);

	priv->net->stats.rx_packets++;
	priv->net->stats.rx_bytes += frame->len;

	can_led_event(priv->net, CAN_LED_EVENT_RX);

	netif_rx_ni(skb);
}

static void hi3110_hw_sleep(struct spi_device *spi)
{
	hi3110_write(spi, HI3110_WRITE_CTRL0, HI3110_CTRL0_SLEEP_MODE);
}

static netdev_tx_t hi3110_hard_start_xmit(struct sk_buff *skb,
					  struct net_device *net)
{
	struct hi3110_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;

	if (priv->tx_skb || priv->tx_len) {
		dev_err(&spi->dev, "hard_xmit called while tx busy\n");
		return NETDEV_TX_BUSY;
	}

	if (can_dropped_invalid_skb(net, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(net);
	priv->tx_skb = skb;
	queue_work(priv->wq, &priv->tx_work);

	return NETDEV_TX_OK;
}

static int hi3110_do_set_mode(struct net_device *net, enum can_mode mode)
{
	struct hi3110_priv *priv = netdev_priv(net);

	switch (mode) {
	case CAN_MODE_START:
		hi3110_clean(net);
		/* We have to delay work since SPI I/O may sleep */
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		priv->restart_tx = 1;
		if (priv->can.restart_ms == 0)
			priv->after_suspend = HI3110_AFTER_SUSPEND_RESTART;
		queue_work(priv->wq, &priv->restart_work);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int hi3110_get_berr_counter(const struct net_device *net,
				   struct can_berr_counter *bec)
{
	struct hi3110_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;

	mutex_lock(&priv->hi3110_lock);
	bec->txerr = hi3110_read(spi, HI3110_READ_TEC);
	bec->rxerr = hi3110_read(spi, HI3110_READ_REC);
	mutex_unlock(&priv->hi3110_lock);

	return 0;
}

static int hi3110_set_normal_mode(struct spi_device *spi)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	u8 reg = 0;

	hi3110_write(spi, HI3110_WRITE_INTE, HI3110_INT_BUSERR |
		     HI3110_INT_RXFIFO | HI3110_INT_TXCPLT);

	/* Enable TX */
	hi3110_write(spi, HI3110_WRITE_CTRL1, HI3110_CTRL1_TXEN);

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		reg = HI3110_CTRL0_LOOPBACK_MODE;
	else if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		reg = HI3110_CTRL0_MONITOR_MODE;
	else
		reg = HI3110_CTRL0_NORMAL_MODE;

	hi3110_write(spi, HI3110_WRITE_CTRL0, reg);

	/* Wait for the device to enter the mode */
	mdelay(HI3110_OST_DELAY_MS);
	reg = hi3110_read(spi, HI3110_READ_CTRL0);
	if ((reg & HI3110_CTRL0_MODE_MASK) != reg)
		return -EBUSY;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;
}

static int hi3110_do_set_bittiming(struct net_device *net)
{
	struct hi3110_priv *priv = netdev_priv(net);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct spi_device *spi = priv->spi;

	hi3110_write(spi, HI3110_WRITE_BTR0,
		     ((bt->sjw - 1) << HI3110_BTR0_SJW_SHIFT) |
		     ((bt->brp - 1) << HI3110_BTR0_BRP_SHIFT));

	hi3110_write(spi, HI3110_WRITE_BTR1,
		     (priv->can.ctrlmode &
		      CAN_CTRLMODE_3_SAMPLES ?
		      HI3110_BTR1_SAMP_3PERBIT : HI3110_BTR1_SAMP_1PERBIT) |
		     ((bt->phase_seg1 + bt->prop_seg - 1)
		      << HI3110_BTR1_TSEG1_SHIFT) |
		     ((bt->phase_seg2 - 1) << HI3110_BTR1_TSEG2_SHIFT));

	dev_dbg(&spi->dev, "BT: 0x%02x 0x%02x\n",
		hi3110_read(spi, HI3110_READ_BTR0),
		hi3110_read(spi, HI3110_READ_BTR1));

	return 0;
}

static int hi3110_setup(struct net_device *net)
{
	hi3110_do_set_bittiming(net);
	return 0;
}

static int hi3110_hw_reset(struct spi_device *spi)
{
	u8 reg;
	int ret;

	/* Wait for oscillator startup timer after power up */
	mdelay(HI3110_OST_DELAY_MS);

	ret = hi3110_cmd(spi, HI3110_MASTER_RESET);
	if (ret)
		return ret;

	/* Wait for oscillator startup timer after reset */
	mdelay(HI3110_OST_DELAY_MS);

	reg = hi3110_read(spi, HI3110_READ_CTRL0);
	if ((reg & HI3110_CTRL0_MODE_MASK) != HI3110_CTRL0_INIT_MODE)
		return -ENODEV;

	/* As per the datasheet it appears the error flags are
	 * not cleared on reset. Explicitly clear them by performing a read
	 */
	hi3110_read(spi, HI3110_READ_ERR);

	return 0;
}

static int hi3110_hw_probe(struct spi_device *spi)
{
	u8 statf;

	hi3110_hw_reset(spi);

	/* Confirm correct operation by checking against reset values
	 * in datasheet
	 */
	statf = hi3110_read(spi, HI3110_READ_STATF);

	dev_dbg(&spi->dev, "statf: %02X\n", statf);

	if (statf != 0x82)
		return -ENODEV;

	return 0;
}

static int hi3110_power_enable(struct regulator *reg, int enable)
{
	if (IS_ERR_OR_NULL(reg))
		return 0;

	if (enable)
		return regulator_enable(reg);
	else
		return regulator_disable(reg);
}

static int hi3110_stop(struct net_device *net)
{
	struct hi3110_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;

	close_candev(net);

	priv->force_quit = 1;
	free_irq(spi->irq, priv);
	destroy_workqueue(priv->wq);
	priv->wq = NULL;

	mutex_lock(&priv->hi3110_lock);

	/* Disable transmit, interrupts and clear flags */
	hi3110_write(spi, HI3110_WRITE_CTRL1, 0x0);
	hi3110_write(spi, HI3110_WRITE_INTE, 0x0);
	hi3110_read(spi, HI3110_READ_INTF);

	hi3110_clean(net);

	hi3110_hw_sleep(spi);

	hi3110_power_enable(priv->transceiver, 0);

	priv->can.state = CAN_STATE_STOPPED;

	mutex_unlock(&priv->hi3110_lock);

	can_led_event(net, CAN_LED_EVENT_STOP);

	return 0;
}

static void hi3110_tx_work_handler(struct work_struct *ws)
{
	struct hi3110_priv *priv = container_of(ws, struct hi3110_priv,
						tx_work);
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;
	struct can_frame *frame;

	mutex_lock(&priv->hi3110_lock);
	if (priv->tx_skb) {
		if (priv->can.state == CAN_STATE_BUS_OFF) {
			hi3110_clean(net);
		} else {
			frame = (struct can_frame *)priv->tx_skb->data;
			hi3110_hw_tx(spi, frame);
			priv->tx_len = 1 + frame->len;
			can_put_echo_skb(priv->tx_skb, net, 0, 0);
			priv->tx_skb = NULL;
		}
	}
	mutex_unlock(&priv->hi3110_lock);
}

static void hi3110_restart_work_handler(struct work_struct *ws)
{
	struct hi3110_priv *priv = container_of(ws, struct hi3110_priv,
						restart_work);
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;

	mutex_lock(&priv->hi3110_lock);
	if (priv->after_suspend) {
		hi3110_hw_reset(spi);
		hi3110_setup(net);
		if (priv->after_suspend & HI3110_AFTER_SUSPEND_RESTART) {
			hi3110_set_normal_mode(spi);
		} else if (priv->after_suspend & HI3110_AFTER_SUSPEND_UP) {
			netif_device_attach(net);
			hi3110_clean(net);
			hi3110_set_normal_mode(spi);
			netif_wake_queue(net);
		} else {
			hi3110_hw_sleep(spi);
		}
		priv->after_suspend = 0;
		priv->force_quit = 0;
	}

	if (priv->restart_tx) {
		priv->restart_tx = 0;
		hi3110_hw_reset(spi);
		hi3110_setup(net);
		hi3110_clean(net);
		hi3110_set_normal_mode(spi);
		netif_wake_queue(net);
	}
	mutex_unlock(&priv->hi3110_lock);
}

static irqreturn_t hi3110_can_ist(int irq, void *dev_id)
{
	struct hi3110_priv *priv = dev_id;
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;

	mutex_lock(&priv->hi3110_lock);

	while (!priv->force_quit) {
		enum can_state new_state;
		u8 intf, eflag, statf;

		while (!(HI3110_STAT_RXFMTY &
			 (statf = hi3110_read(spi, HI3110_READ_STATF)))) {
			hi3110_hw_rx(spi);
		}

		intf = hi3110_read(spi, HI3110_READ_INTF);
		eflag = hi3110_read(spi, HI3110_READ_ERR);
		/* Update can state */
		if (eflag & HI3110_ERR_BUSOFF)
			new_state = CAN_STATE_BUS_OFF;
		else if (eflag & HI3110_ERR_PASSIVE_MASK)
			new_state = CAN_STATE_ERROR_PASSIVE;
		else if (statf & HI3110_STAT_ERRW)
			new_state = CAN_STATE_ERROR_WARNING;
		else
			new_state = CAN_STATE_ERROR_ACTIVE;

		if (new_state != priv->can.state) {
			struct can_frame *cf;
			struct sk_buff *skb;
			enum can_state rx_state, tx_state;
			u8 rxerr, txerr;

			skb = alloc_can_err_skb(net, &cf);
			if (!skb)
				break;

			txerr = hi3110_read(spi, HI3110_READ_TEC);
			rxerr = hi3110_read(spi, HI3110_READ_REC);
			cf->data[6] = txerr;
			cf->data[7] = rxerr;
			tx_state = txerr >= rxerr ? new_state : 0;
			rx_state = txerr <= rxerr ? new_state : 0;
			can_change_state(net, cf, tx_state, rx_state);
			netif_rx_ni(skb);

			if (new_state == CAN_STATE_BUS_OFF) {
				can_bus_off(net);
				if (priv->can.restart_ms == 0) {
					priv->force_quit = 1;
					hi3110_hw_sleep(spi);
					break;
				}
			}
		}

		/* Update bus errors */
		if ((intf & HI3110_INT_BUSERR) &&
		    (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)) {
			struct can_frame *cf;
			struct sk_buff *skb;

			/* Check for protocol errors */
			if (eflag & HI3110_ERR_PROTOCOL_MASK) {
				skb = alloc_can_err_skb(net, &cf);
				if (!skb)
					break;

				cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
				priv->can.can_stats.bus_error++;
				priv->net->stats.rx_errors++;
				if (eflag & HI3110_ERR_BITERR)
					cf->data[2] |= CAN_ERR_PROT_BIT;
				else if (eflag & HI3110_ERR_FRMERR)
					cf->data[2] |= CAN_ERR_PROT_FORM;
				else if (eflag & HI3110_ERR_STUFERR)
					cf->data[2] |= CAN_ERR_PROT_STUFF;
				else if (eflag & HI3110_ERR_CRCERR)
					cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
				else if (eflag & HI3110_ERR_ACKERR)
					cf->data[3] |= CAN_ERR_PROT_LOC_ACK;

				cf->data[6] = hi3110_read(spi, HI3110_READ_TEC);
				cf->data[7] = hi3110_read(spi, HI3110_READ_REC);
				netdev_dbg(priv->net, "Bus Error\n");
				netif_rx_ni(skb);
			}
		}

		if (priv->tx_len && statf & HI3110_STAT_TXMTY) {
			net->stats.tx_packets++;
			net->stats.tx_bytes += priv->tx_len - 1;
			can_led_event(net, CAN_LED_EVENT_TX);
			if (priv->tx_len) {
				can_get_echo_skb(net, 0, NULL);
				priv->tx_len = 0;
			}
			netif_wake_queue(net);
		}

		if (intf == 0)
			break;
	}
	mutex_unlock(&priv->hi3110_lock);
	return IRQ_HANDLED;
}

static int hi3110_open(struct net_device *net)
{
	struct hi3110_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;
	unsigned long flags = IRQF_ONESHOT | IRQF_TRIGGER_HIGH;
	int ret;

	ret = open_candev(net);
	if (ret)
		return ret;

	mutex_lock(&priv->hi3110_lock);
	hi3110_power_enable(priv->transceiver, 1);

	priv->force_quit = 0;
	priv->tx_skb = NULL;
	priv->tx_len = 0;

	ret = request_threaded_irq(spi->irq, NULL, hi3110_can_ist,
				   flags, DEVICE_NAME, priv);
	if (ret) {
		dev_err(&spi->dev, "failed to acquire irq %d\n", spi->irq);
		goto out_close;
	}

	priv->wq = alloc_workqueue("hi3110_wq", WQ_FREEZABLE | WQ_MEM_RECLAIM,
				   0);
	if (!priv->wq) {
		ret = -ENOMEM;
		goto out_free_irq;
	}
	INIT_WORK(&priv->tx_work, hi3110_tx_work_handler);
	INIT_WORK(&priv->restart_work, hi3110_restart_work_handler);

	ret = hi3110_hw_reset(spi);
	if (ret)
		goto out_free_wq;

	ret = hi3110_setup(net);
	if (ret)
		goto out_free_wq;

	ret = hi3110_set_normal_mode(spi);
	if (ret)
		goto out_free_wq;

	can_led_event(net, CAN_LED_EVENT_OPEN);
	netif_wake_queue(net);
	mutex_unlock(&priv->hi3110_lock);

	return 0;

 out_free_wq:
	destroy_workqueue(priv->wq);
 out_free_irq:
	free_irq(spi->irq, priv);
	hi3110_hw_sleep(spi);
 out_close:
	hi3110_power_enable(priv->transceiver, 0);
	close_candev(net);
	mutex_unlock(&priv->hi3110_lock);
	return ret;
}

static const struct net_device_ops hi3110_netdev_ops = {
	.ndo_open = hi3110_open,
	.ndo_stop = hi3110_stop,
	.ndo_start_xmit = hi3110_hard_start_xmit,
};

static const struct of_device_id hi3110_of_match[] = {
	{
		.compatible	= "holt,hi3110",
		.data		= (void *)CAN_HI3110_HI3110,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, hi3110_of_match);

static const struct spi_device_id hi3110_id_table[] = {
	{
		.name		= "hi3110",
		.driver_data	= (kernel_ulong_t)CAN_HI3110_HI3110,
	},
	{ }
};
MODULE_DEVICE_TABLE(spi, hi3110_id_table);

static int hi3110_can_probe(struct spi_device *spi)
{
	const struct of_device_id *of_id = of_match_device(hi3110_of_match,
							   &spi->dev);
	struct net_device *net;
	struct hi3110_priv *priv;
	struct clk *clk;
	int freq, ret;

	clk = devm_clk_get(&spi->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&spi->dev, "no CAN clock source defined\n");
		return PTR_ERR(clk);
	}
	freq = clk_get_rate(clk);

	/* Sanity check */
	if (freq > 40000000)
		return -ERANGE;

	/* Allocate can/net device */
	net = alloc_candev(sizeof(struct hi3110_priv), HI3110_TX_ECHO_SKB_MAX);
	if (!net)
		return -ENOMEM;

	if (!IS_ERR(clk)) {
		ret = clk_prepare_enable(clk);
		if (ret)
			goto out_free;
	}

	net->netdev_ops = &hi3110_netdev_ops;
	net->flags |= IFF_ECHO;

	priv = netdev_priv(net);
	priv->can.bittiming_const = &hi3110_bittiming_const;
	priv->can.do_set_mode = hi3110_do_set_mode;
	priv->can.do_get_berr_counter = hi3110_get_berr_counter;
	priv->can.clock.freq = freq / 2;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
		CAN_CTRLMODE_LOOPBACK |
		CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_BERR_REPORTING;

	if (of_id)
		priv->model = (enum hi3110_model)(uintptr_t)of_id->data;
	else
		priv->model = spi_get_device_id(spi)->driver_data;
	priv->net = net;
	priv->clk = clk;

	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		goto out_clk;

	priv->power = devm_regulator_get_optional(&spi->dev, "vdd");
	priv->transceiver = devm_regulator_get_optional(&spi->dev, "xceiver");
	if ((PTR_ERR(priv->power) == -EPROBE_DEFER) ||
	    (PTR_ERR(priv->transceiver) == -EPROBE_DEFER)) {
		ret = -EPROBE_DEFER;
		goto out_clk;
	}

	ret = hi3110_power_enable(priv->power, 1);
	if (ret)
		goto out_clk;

	priv->spi = spi;
	mutex_init(&priv->hi3110_lock);

	priv->spi_tx_buf = devm_kzalloc(&spi->dev, HI3110_RX_BUF_LEN,
					GFP_KERNEL);
	if (!priv->spi_tx_buf) {
		ret = -ENOMEM;
		goto error_probe;
	}
	priv->spi_rx_buf = devm_kzalloc(&spi->dev, HI3110_RX_BUF_LEN,
					GFP_KERNEL);

	if (!priv->spi_rx_buf) {
		ret = -ENOMEM;
		goto error_probe;
	}

	SET_NETDEV_DEV(net, &spi->dev);

	ret = hi3110_hw_probe(spi);
	if (ret) {
		if (ret == -ENODEV)
			dev_err(&spi->dev, "Cannot initialize %x. Wrong wiring?\n",
				priv->model);
		goto error_probe;
	}
	hi3110_hw_sleep(spi);

	ret = register_candev(net);
	if (ret)
		goto error_probe;

	devm_can_led_init(net);
	netdev_info(net, "%x successfully initialized.\n", priv->model);

	return 0;

 error_probe:
	hi3110_power_enable(priv->power, 0);

 out_clk:
	if (!IS_ERR(clk))
		clk_disable_unprepare(clk);

 out_free:
	free_candev(net);

	dev_err(&spi->dev, "Probe failed, err=%d\n", -ret);
	return ret;
}

static int hi3110_can_remove(struct spi_device *spi)
{
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	struct net_device *net = priv->net;

	unregister_candev(net);

	hi3110_power_enable(priv->power, 0);

	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);

	free_candev(net);

	return 0;
}

static int __maybe_unused hi3110_can_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct hi3110_priv *priv = spi_get_drvdata(spi);
	struct net_device *net = priv->net;

	priv->force_quit = 1;
	disable_irq(spi->irq);

	/* Note: at this point neither IST nor workqueues are running.
	 * open/stop cannot be called anyway so locking is not needed
	 */
	if (netif_running(net)) {
		netif_device_detach(net);

		hi3110_hw_sleep(spi);
		hi3110_power_enable(priv->transceiver, 0);
		priv->after_suspend = HI3110_AFTER_SUSPEND_UP;
	} else {
		priv->after_suspend = HI3110_AFTER_SUSPEND_DOWN;
	}

	if (!IS_ERR_OR_NULL(priv->power)) {
		regulator_disable(priv->power);
		priv->after_suspend |= HI3110_AFTER_SUSPEND_POWER;
	}

	return 0;
}

static int __maybe_unused hi3110_can_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct hi3110_priv *priv = spi_get_drvdata(spi);

	if (priv->after_suspend & HI3110_AFTER_SUSPEND_POWER)
		hi3110_power_enable(priv->power, 1);

	if (priv->after_suspend & HI3110_AFTER_SUSPEND_UP) {
		hi3110_power_enable(priv->transceiver, 1);
		queue_work(priv->wq, &priv->restart_work);
	} else {
		priv->after_suspend = 0;
	}

	priv->force_quit = 0;
	enable_irq(spi->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(hi3110_can_pm_ops, hi3110_can_suspend, hi3110_can_resume);

static struct spi_driver hi3110_can_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = hi3110_of_match,
		.pm = &hi3110_can_pm_ops,
	},
	.id_table = hi3110_id_table,
	.probe = hi3110_can_probe,
	.remove = hi3110_can_remove,
};

module_spi_driver(hi3110_can_driver);

MODULE_AUTHOR("Akshay Bhat <akshay.bhat@timesys.com>");
MODULE_AUTHOR("Casey Fitzpatrick <casey.fitzpatrick@timesys.com>");
MODULE_DESCRIPTION("Holt HI-3110 CAN driver");
MODULE_LICENSE("GPL v2");
