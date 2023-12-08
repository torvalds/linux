// SPDX-License-Identifier: GPL-2.0-only
/*
 * atusb.c - Driver for the ATUSB IEEE 802.15.4 dongle
 *
 * Written 2013 by Werner Almesberger <werner@almesberger.net>
 *
 * Copyright (c) 2015 - 2016 Stefan Schmidt <stefan@datenfreihafen.org>
 *
 * Based on at86rf230.c and spi_atusb.c.
 * at86rf230.c is
 * Copyright (C) 2009 Siemens AG
 * Written by: Dmitry Eremin-Solenikov <dmitry.baryshkov@siemens.com>
 *
 * spi_atusb.c is
 * Copyright (c) 2011 Richard Sharpe <realrichardsharpe@gmail.com>
 * Copyright (c) 2011 Stefan Schmidt <stefan@datenfreihafen.org>
 * Copyright (c) 2011 Werner Almesberger <werner@almesberger.net>
 *
 * USB initialization is
 * Copyright (c) 2013 Alexander Aring <alex.aring@gmail.com>
 *
 * Busware HUL support is
 * Copyright (c) 2017 Josef Filzmaier <j.filzmaier@gmx.at>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/usb.h>
#include <linux/skbuff.h>

#include <net/cfg802154.h>
#include <net/mac802154.h>

#include "at86rf230.h"
#include "atusb.h"

#define ATUSB_JEDEC_ATMEL	0x1f	/* JEDEC manufacturer ID */

#define ATUSB_NUM_RX_URBS	4	/* allow for a bit of local latency */
#define ATUSB_ALLOC_DELAY_MS	100	/* delay after failed allocation */
#define ATUSB_TX_TIMEOUT_MS	200	/* on the air timeout */

struct atusb {
	struct ieee802154_hw *hw;
	struct usb_device *usb_dev;
	struct atusb_chip_data *data;
	int shutdown;			/* non-zero if shutting down */
	int err;			/* set by first error */

	/* RX variables */
	struct delayed_work work;	/* memory allocations */
	struct usb_anchor idle_urbs;	/* URBs waiting to be submitted */
	struct usb_anchor rx_urbs;	/* URBs waiting for reception */

	/* TX variables */
	struct usb_ctrlrequest tx_dr;
	struct urb *tx_urb;
	struct sk_buff *tx_skb;
	u8 tx_ack_seq;		/* current TX ACK sequence number */

	/* Firmware variable */
	unsigned char fw_ver_maj;	/* Firmware major version number */
	unsigned char fw_ver_min;	/* Firmware minor version number */
	unsigned char fw_hw_type;	/* Firmware hardware type */
};

struct atusb_chip_data {
	u16 t_channel_switch;
	int rssi_base_val;

	int (*set_channel)(struct ieee802154_hw*, u8, u8);
	int (*set_txpower)(struct ieee802154_hw*, s32);
};

static int atusb_write_subreg(struct atusb *atusb, u8 reg, u8 mask,
			      u8 shift, u8 value)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	u8 orig, tmp;
	int ret = 0;

	dev_dbg(&usb_dev->dev, "%s: 0x%02x <- 0x%02x\n", __func__, reg, value);

	ret = usb_control_msg_recv(usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, reg, &orig, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	/* Write the value only into that part of the register which is allowed
	 * by the mask. All other bits stay as before.
	 */
	tmp = orig & ~mask;
	tmp |= (value << shift) & mask;

	if (tmp != orig)
		ret = usb_control_msg_send(usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
					   tmp, reg, NULL, 0, 1000, GFP_KERNEL);

	return ret;
}

static int atusb_read_subreg(struct atusb *lp,
			     unsigned int addr, unsigned int mask,
			     unsigned int shift)
{
	int reg, ret;

	ret = usb_control_msg_recv(lp->usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, addr, &reg, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	reg = (reg & mask) >> shift;

	return reg;
}

static int atusb_get_and_clear_error(struct atusb *atusb)
{
	int err = atusb->err;

	atusb->err = 0;
	return err;
}

/* ----- skb allocation ---------------------------------------------------- */

#define MAX_PSDU	127
#define MAX_RX_XFER	(1 + MAX_PSDU + 2 + 1)	/* PHR+PSDU+CRC+LQI */

#define SKB_ATUSB(skb)	(*(struct atusb **)(skb)->cb)

static void atusb_in(struct urb *urb);

static int atusb_submit_rx_urb(struct atusb *atusb, struct urb *urb)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	struct sk_buff *skb = urb->context;
	int ret;

	if (!skb) {
		skb = alloc_skb(MAX_RX_XFER, GFP_KERNEL);
		if (!skb) {
			dev_warn_ratelimited(&usb_dev->dev,
					     "atusb_in: can't allocate skb\n");
			return -ENOMEM;
		}
		skb_put(skb, MAX_RX_XFER);
		SKB_ATUSB(skb) = atusb;
	}

	usb_fill_bulk_urb(urb, usb_dev, usb_rcvbulkpipe(usb_dev, 1),
			  skb->data, MAX_RX_XFER, atusb_in, skb);
	usb_anchor_urb(urb, &atusb->rx_urbs);

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		usb_unanchor_urb(urb);
		kfree_skb(skb);
		urb->context = NULL;
	}
	return ret;
}

static void atusb_work_urbs(struct work_struct *work)
{
	struct atusb *atusb =
	    container_of(to_delayed_work(work), struct atusb, work);
	struct usb_device *usb_dev = atusb->usb_dev;
	struct urb *urb;
	int ret;

	if (atusb->shutdown)
		return;

	do {
		urb = usb_get_from_anchor(&atusb->idle_urbs);
		if (!urb)
			return;
		ret = atusb_submit_rx_urb(atusb, urb);
	} while (!ret);

	usb_anchor_urb(urb, &atusb->idle_urbs);
	dev_warn_ratelimited(&usb_dev->dev,
			     "atusb_in: can't allocate/submit URB (%d)\n", ret);
	schedule_delayed_work(&atusb->work,
			      msecs_to_jiffies(ATUSB_ALLOC_DELAY_MS) + 1);
}

/* ----- Asynchronous USB -------------------------------------------------- */

static void atusb_tx_done(struct atusb *atusb, u8 seq)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	u8 expect = atusb->tx_ack_seq;

	dev_dbg(&usb_dev->dev, "%s (0x%02x/0x%02x)\n", __func__, seq, expect);
	if (seq == expect) {
		/* TODO check for ifs handling in firmware */
		ieee802154_xmit_complete(atusb->hw, atusb->tx_skb, false);
	} else {
		/* TODO I experience this case when atusb has a tx complete
		 * irq before probing, we should fix the firmware it's an
		 * unlikely case now that seq == expect is then true, but can
		 * happen and fail with a tx_skb = NULL;
		 */
		ieee802154_xmit_hw_error(atusb->hw, atusb->tx_skb);
	}
}

static void atusb_in_good(struct urb *urb)
{
	struct usb_device *usb_dev = urb->dev;
	struct sk_buff *skb = urb->context;
	struct atusb *atusb = SKB_ATUSB(skb);
	u8 len, lqi;

	if (!urb->actual_length) {
		dev_dbg(&usb_dev->dev, "atusb_in: zero-sized URB ?\n");
		return;
	}

	len = *skb->data;

	if (urb->actual_length == 1) {
		atusb_tx_done(atusb, len);
		return;
	}

	if (len + 1 > urb->actual_length - 1) {
		dev_dbg(&usb_dev->dev, "atusb_in: frame len %d+1 > URB %u-1\n",
			len, urb->actual_length);
		return;
	}

	if (!ieee802154_is_valid_psdu_len(len)) {
		dev_dbg(&usb_dev->dev, "atusb_in: frame corrupted\n");
		return;
	}

	lqi = skb->data[len + 1];
	dev_dbg(&usb_dev->dev, "atusb_in: rx len %d lqi 0x%02x\n", len, lqi);
	skb_pull(skb, 1);	/* remove PHR */
	skb_trim(skb, len);	/* get payload only */
	ieee802154_rx_irqsafe(atusb->hw, skb, lqi);
	urb->context = NULL;	/* skb is gone */
}

static void atusb_in(struct urb *urb)
{
	struct usb_device *usb_dev = urb->dev;
	struct sk_buff *skb = urb->context;
	struct atusb *atusb = SKB_ATUSB(skb);

	dev_dbg(&usb_dev->dev, "%s: status %d len %d\n", __func__,
		urb->status, urb->actual_length);
	if (urb->status) {
		if (urb->status == -ENOENT) { /* being killed */
			kfree_skb(skb);
			urb->context = NULL;
			return;
		}
		dev_dbg(&usb_dev->dev, "%s: URB error %d\n", __func__, urb->status);
	} else {
		atusb_in_good(urb);
	}

	usb_anchor_urb(urb, &atusb->idle_urbs);
	if (!atusb->shutdown)
		schedule_delayed_work(&atusb->work, 0);
}

/* ----- URB allocation/deallocation --------------------------------------- */

static void atusb_free_urbs(struct atusb *atusb)
{
	struct urb *urb;

	while (1) {
		urb = usb_get_from_anchor(&atusb->idle_urbs);
		if (!urb)
			break;
		kfree_skb(urb->context);
		usb_free_urb(urb);
	}
}

static int atusb_alloc_urbs(struct atusb *atusb, int n)
{
	struct urb *urb;

	while (n) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			atusb_free_urbs(atusb);
			return -ENOMEM;
		}
		usb_anchor_urb(urb, &atusb->idle_urbs);
		usb_free_urb(urb);
		n--;
	}
	return 0;
}

/* ----- IEEE 802.15.4 interface operations -------------------------------- */

static void atusb_xmit_complete(struct urb *urb)
{
	dev_dbg(&urb->dev->dev, "atusb_xmit urb completed");
}

static int atusb_xmit(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct atusb *atusb = hw->priv;
	struct usb_device *usb_dev = atusb->usb_dev;
	int ret;

	dev_dbg(&usb_dev->dev, "%s (%d)\n", __func__, skb->len);
	atusb->tx_skb = skb;
	atusb->tx_ack_seq++;
	atusb->tx_dr.wIndex = cpu_to_le16(atusb->tx_ack_seq);
	atusb->tx_dr.wLength = cpu_to_le16(skb->len);

	usb_fill_control_urb(atusb->tx_urb, usb_dev,
			     usb_sndctrlpipe(usb_dev, 0),
			     (unsigned char *)&atusb->tx_dr, skb->data,
			     skb->len, atusb_xmit_complete, NULL);
	ret = usb_submit_urb(atusb->tx_urb, GFP_ATOMIC);
	dev_dbg(&usb_dev->dev, "%s done (%d)\n", __func__, ret);
	return ret;
}

static int atusb_ed(struct ieee802154_hw *hw, u8 *level)
{
	WARN_ON(!level);
	*level = 0xbe;
	return 0;
}

static int atusb_set_hw_addr_filt(struct ieee802154_hw *hw,
				  struct ieee802154_hw_addr_filt *filt,
				  unsigned long changed)
{
	struct atusb *atusb = hw->priv;
	struct device *dev = &atusb->usb_dev->dev;

	if (changed & IEEE802154_AFILT_SADDR_CHANGED) {
		u16 addr = le16_to_cpu(filt->short_addr);

		dev_vdbg(dev, "%s called for saddr\n", __func__);
		usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
				     addr, RG_SHORT_ADDR_0, NULL, 0, 1000, GFP_KERNEL);

		usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
				     addr >> 8, RG_SHORT_ADDR_1, NULL, 0, 1000, GFP_KERNEL);
	}

	if (changed & IEEE802154_AFILT_PANID_CHANGED) {
		u16 pan = le16_to_cpu(filt->pan_id);

		dev_vdbg(dev, "%s called for pan id\n", __func__);
		usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
				     pan, RG_PAN_ID_0, NULL, 0, 1000, GFP_KERNEL);

		usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
				     pan >> 8, RG_PAN_ID_1, NULL, 0, 1000, GFP_KERNEL);
	}

	if (changed & IEEE802154_AFILT_IEEEADDR_CHANGED) {
		u8 i, addr[IEEE802154_EXTENDED_ADDR_LEN];

		memcpy(addr, &filt->ieee_addr, IEEE802154_EXTENDED_ADDR_LEN);
		dev_vdbg(dev, "%s called for IEEE addr\n", __func__);
		for (i = 0; i < 8; i++)
			usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
					     addr[i], RG_IEEE_ADDR_0 + i, NULL, 0,
					     1000, GFP_KERNEL);
	}

	if (changed & IEEE802154_AFILT_PANC_CHANGED) {
		dev_vdbg(dev, "%s called for panc change\n", __func__);
		if (filt->pan_coord)
			atusb_write_subreg(atusb, SR_AACK_I_AM_COORD, 1);
		else
			atusb_write_subreg(atusb, SR_AACK_I_AM_COORD, 0);
	}

	return atusb_get_and_clear_error(atusb);
}

static int atusb_start(struct ieee802154_hw *hw)
{
	struct atusb *atusb = hw->priv;
	struct usb_device *usb_dev = atusb->usb_dev;
	int ret;

	dev_dbg(&usb_dev->dev, "%s\n", __func__);
	schedule_delayed_work(&atusb->work, 0);
	usb_control_msg_send(atusb->usb_dev, 0, ATUSB_RX_MODE, ATUSB_REQ_TO_DEV, 1, 0,
			     NULL, 0, 1000, GFP_KERNEL);
	ret = atusb_get_and_clear_error(atusb);
	if (ret < 0)
		usb_kill_anchored_urbs(&atusb->idle_urbs);
	return ret;
}

static void atusb_stop(struct ieee802154_hw *hw)
{
	struct atusb *atusb = hw->priv;
	struct usb_device *usb_dev = atusb->usb_dev;

	dev_dbg(&usb_dev->dev, "%s\n", __func__);
	usb_kill_anchored_urbs(&atusb->idle_urbs);
	usb_control_msg_send(atusb->usb_dev, 0, ATUSB_RX_MODE, ATUSB_REQ_TO_DEV, 0, 0,
			     NULL, 0, 1000, GFP_KERNEL);
	atusb_get_and_clear_error(atusb);
}

#define ATUSB_MAX_TX_POWERS 0xF
static const s32 atusb_powers[ATUSB_MAX_TX_POWERS + 1] = {
	300, 280, 230, 180, 130, 70, 0, -100, -200, -300, -400, -500, -700,
	-900, -1200, -1700,
};

static int
atusb_txpower(struct ieee802154_hw *hw, s32 mbm)
{
	struct atusb *atusb = hw->priv;

	if (atusb->data)
		return atusb->data->set_txpower(hw, mbm);
	else
		return -ENOTSUPP;
}

static int
atusb_set_txpower(struct ieee802154_hw *hw, s32 mbm)
{
	struct atusb *atusb = hw->priv;
	u32 i;

	for (i = 0; i < hw->phy->supported.tx_powers_size; i++) {
		if (hw->phy->supported.tx_powers[i] == mbm)
			return atusb_write_subreg(atusb, SR_TX_PWR_23X, i);
	}

	return -EINVAL;
}

static int
hulusb_set_txpower(struct ieee802154_hw *hw, s32 mbm)
{
	u32 i;

	for (i = 0; i < hw->phy->supported.tx_powers_size; i++) {
		if (hw->phy->supported.tx_powers[i] == mbm)
			return atusb_write_subreg(hw->priv, SR_TX_PWR_212, i);
	}

	return -EINVAL;
}

#define ATUSB_MAX_ED_LEVELS 0xF
static const s32 atusb_ed_levels[ATUSB_MAX_ED_LEVELS + 1] = {
	-9100, -8900, -8700, -8500, -8300, -8100, -7900, -7700, -7500, -7300,
	-7100, -6900, -6700, -6500, -6300, -6100,
};

#define AT86RF212_MAX_TX_POWERS 0x1F
static const s32 at86rf212_powers[AT86RF212_MAX_TX_POWERS + 1] = {
	500, 400, 300, 200, 100, 0, -100, -200, -300, -400, -500, -600, -700,
	-800, -900, -1000, -1100, -1200, -1300, -1400, -1500, -1600, -1700,
	-1800, -1900, -2000, -2100, -2200, -2300, -2400, -2500, -2600,
};

#define AT86RF2XX_MAX_ED_LEVELS 0xF
static const s32 at86rf212_ed_levels_100[AT86RF2XX_MAX_ED_LEVELS + 1] = {
	-10000, -9800, -9600, -9400, -9200, -9000, -8800, -8600, -8400, -8200,
	-8000, -7800, -7600, -7400, -7200, -7000,
};

static const s32 at86rf212_ed_levels_98[AT86RF2XX_MAX_ED_LEVELS + 1] = {
	-9800, -9600, -9400, -9200, -9000, -8800, -8600, -8400, -8200, -8000,
	-7800, -7600, -7400, -7200, -7000, -6800,
};

static int
atusb_set_cca_mode(struct ieee802154_hw *hw, const struct wpan_phy_cca *cca)
{
	struct atusb *atusb = hw->priv;
	u8 val;

	/* mapping 802.15.4 to driver spec */
	switch (cca->mode) {
	case NL802154_CCA_ENERGY:
		val = 1;
		break;
	case NL802154_CCA_CARRIER:
		val = 2;
		break;
	case NL802154_CCA_ENERGY_CARRIER:
		switch (cca->opt) {
		case NL802154_CCA_OPT_ENERGY_CARRIER_AND:
			val = 3;
			break;
		case NL802154_CCA_OPT_ENERGY_CARRIER_OR:
			val = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return atusb_write_subreg(atusb, SR_CCA_MODE, val);
}

static int hulusb_set_cca_ed_level(struct atusb *lp, int rssi_base_val)
{
	int cca_ed_thres;

	cca_ed_thres = atusb_read_subreg(lp, SR_CCA_ED_THRES);
	if (cca_ed_thres < 0)
		return cca_ed_thres;

	switch (rssi_base_val) {
	case -98:
		lp->hw->phy->supported.cca_ed_levels = at86rf212_ed_levels_98;
		lp->hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(at86rf212_ed_levels_98);
		lp->hw->phy->cca_ed_level = at86rf212_ed_levels_98[cca_ed_thres];
		break;
	case -100:
		lp->hw->phy->supported.cca_ed_levels = at86rf212_ed_levels_100;
		lp->hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(at86rf212_ed_levels_100);
		lp->hw->phy->cca_ed_level = at86rf212_ed_levels_100[cca_ed_thres];
		break;
	default:
		WARN_ON(1);
	}

	return 0;
}

static int
atusb_set_cca_ed_level(struct ieee802154_hw *hw, s32 mbm)
{
	struct atusb *atusb = hw->priv;
	u32 i;

	for (i = 0; i < hw->phy->supported.cca_ed_levels_size; i++) {
		if (hw->phy->supported.cca_ed_levels[i] == mbm)
			return atusb_write_subreg(atusb, SR_CCA_ED_THRES, i);
	}

	return -EINVAL;
}

static int atusb_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct atusb *atusb = hw->priv;
	int ret = -ENOTSUPP;

	if (atusb->data) {
		ret = atusb->data->set_channel(hw, page, channel);
		/* @@@ ugly synchronization */
		msleep(atusb->data->t_channel_switch);
	}

	return ret;
}

static int atusb_set_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct atusb *atusb = hw->priv;
	int ret;

	ret = atusb_write_subreg(atusb, SR_CHANNEL, channel);
	if (ret < 0)
		return ret;
	return 0;
}

static int hulusb_set_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	int rc;
	int rssi_base_val;

	struct atusb *lp = hw->priv;

	if (channel == 0)
		rc = atusb_write_subreg(lp, SR_SUB_MODE, 0);
	else
		rc = atusb_write_subreg(lp, SR_SUB_MODE, 1);
	if (rc < 0)
		return rc;

	if (page == 0) {
		rc = atusb_write_subreg(lp, SR_BPSK_QPSK, 0);
		rssi_base_val = -100;
	} else {
		rc = atusb_write_subreg(lp, SR_BPSK_QPSK, 1);
		rssi_base_val = -98;
	}
	if (rc < 0)
		return rc;

	rc = hulusb_set_cca_ed_level(lp, rssi_base_val);
	if (rc < 0)
		return rc;

	return atusb_write_subreg(lp, SR_CHANNEL, channel);
}

static int
atusb_set_csma_params(struct ieee802154_hw *hw, u8 min_be, u8 max_be, u8 retries)
{
	struct atusb *atusb = hw->priv;
	int ret;

	ret = atusb_write_subreg(atusb, SR_MIN_BE, min_be);
	if (ret)
		return ret;

	ret = atusb_write_subreg(atusb, SR_MAX_BE, max_be);
	if (ret)
		return ret;

	return atusb_write_subreg(atusb, SR_MAX_CSMA_RETRIES, retries);
}

static int
hulusb_set_lbt(struct ieee802154_hw *hw, bool on)
{
	struct atusb *atusb = hw->priv;

	return atusb_write_subreg(atusb, SR_CSMA_LBT_MODE, on);
}

static int
atusb_set_frame_retries(struct ieee802154_hw *hw, s8 retries)
{
	struct atusb *atusb = hw->priv;

	return atusb_write_subreg(atusb, SR_MAX_FRAME_RETRIES, retries);
}

static int
atusb_set_promiscuous_mode(struct ieee802154_hw *hw, const bool on)
{
	struct atusb *atusb = hw->priv;
	int ret;

	if (on) {
		ret = atusb_write_subreg(atusb, SR_AACK_DIS_ACK, 1);
		if (ret < 0)
			return ret;

		ret = atusb_write_subreg(atusb, SR_AACK_PROM_MODE, 1);
		if (ret < 0)
			return ret;
	} else {
		ret = atusb_write_subreg(atusb, SR_AACK_PROM_MODE, 0);
		if (ret < 0)
			return ret;

		ret = atusb_write_subreg(atusb, SR_AACK_DIS_ACK, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct atusb_chip_data atusb_chip_data = {
	.t_channel_switch = 1,
	.rssi_base_val = -91,
	.set_txpower = atusb_set_txpower,
	.set_channel = atusb_set_channel,
};

static struct atusb_chip_data hulusb_chip_data = {
	.t_channel_switch = 11,
	.rssi_base_val = -100,
	.set_txpower = hulusb_set_txpower,
	.set_channel = hulusb_set_channel,
};

static const struct ieee802154_ops atusb_ops = {
	.owner			= THIS_MODULE,
	.xmit_async		= atusb_xmit,
	.ed			= atusb_ed,
	.set_channel		= atusb_channel,
	.start			= atusb_start,
	.stop			= atusb_stop,
	.set_hw_addr_filt	= atusb_set_hw_addr_filt,
	.set_txpower		= atusb_txpower,
	.set_lbt		= hulusb_set_lbt,
	.set_cca_mode		= atusb_set_cca_mode,
	.set_cca_ed_level	= atusb_set_cca_ed_level,
	.set_csma_params	= atusb_set_csma_params,
	.set_frame_retries	= atusb_set_frame_retries,
	.set_promiscuous_mode	= atusb_set_promiscuous_mode,
};

/* ----- Firmware and chip version information ----------------------------- */

static int atusb_get_and_show_revision(struct atusb *atusb)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	char *hw_name;
	unsigned char buffer[3];
	int ret;

	/* Get a couple of the ATMega Firmware values */
	ret = usb_control_msg_recv(atusb->usb_dev, 0, ATUSB_ID, ATUSB_REQ_FROM_DEV, 0, 0,
				   buffer, 3, 1000, GFP_KERNEL);
	if (!ret) {
		atusb->fw_ver_maj = buffer[0];
		atusb->fw_ver_min = buffer[1];
		atusb->fw_hw_type = buffer[2];

		switch (atusb->fw_hw_type) {
		case ATUSB_HW_TYPE_100813:
		case ATUSB_HW_TYPE_101216:
		case ATUSB_HW_TYPE_110131:
			hw_name = "ATUSB";
			atusb->data = &atusb_chip_data;
			break;
		case ATUSB_HW_TYPE_RZUSB:
			hw_name = "RZUSB";
			atusb->data = &atusb_chip_data;
			break;
		case ATUSB_HW_TYPE_HULUSB:
			hw_name = "HULUSB";
			atusb->data = &hulusb_chip_data;
			break;
		default:
			hw_name = "UNKNOWN";
			atusb->err = -ENOTSUPP;
			ret = -ENOTSUPP;
			break;
		}

		dev_info(&usb_dev->dev,
			 "Firmware: major: %u, minor: %u, hardware type: %s (%d)\n",
			 atusb->fw_ver_maj, atusb->fw_ver_min, hw_name,
			 atusb->fw_hw_type);
	}
	if (atusb->fw_ver_maj == 0 && atusb->fw_ver_min < 2) {
		dev_info(&usb_dev->dev,
			 "Firmware version (%u.%u) predates our first public release.",
			 atusb->fw_ver_maj, atusb->fw_ver_min);
		dev_info(&usb_dev->dev, "Please update to version 0.2 or newer");
	}

	return ret;
}

static int atusb_get_and_show_build(struct atusb *atusb)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	char *build;
	int ret;

	build = kmalloc(ATUSB_BUILD_SIZE + 1, GFP_KERNEL);
	if (!build)
		return -ENOMEM;

	ret = usb_control_msg(atusb->usb_dev, usb_rcvctrlpipe(usb_dev, 0), ATUSB_BUILD,
			      ATUSB_REQ_FROM_DEV, 0, 0, build, ATUSB_BUILD_SIZE, 1000);
	if (ret >= 0) {
		build[ret] = 0;
		dev_info(&usb_dev->dev, "Firmware: build %s\n", build);
	}

	kfree(build);
	return ret;
}

static int atusb_get_and_conf_chip(struct atusb *atusb)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	u8 man_id_0, man_id_1, part_num, version_num;
	const char *chip;
	struct ieee802154_hw *hw = atusb->hw;
	int ret;

	ret = usb_control_msg_recv(usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, RG_MAN_ID_0, &man_id_0, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ret = usb_control_msg_recv(usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, RG_MAN_ID_1, &man_id_1, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ret = usb_control_msg_recv(usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, RG_PART_NUM, &part_num, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ret = usb_control_msg_recv(usb_dev, 0, ATUSB_REG_READ, ATUSB_REQ_FROM_DEV,
				   0, RG_VERSION_NUM, &version_num, 1, 1000, GFP_KERNEL);
	if (ret < 0)
		return ret;

	hw->flags = IEEE802154_HW_TX_OMIT_CKSUM | IEEE802154_HW_AFILT |
		    IEEE802154_HW_PROMISCUOUS | IEEE802154_HW_CSMA_PARAMS;

	hw->phy->flags = WPAN_PHY_FLAG_TXPOWER | WPAN_PHY_FLAG_CCA_ED_LEVEL |
			 WPAN_PHY_FLAG_CCA_MODE;

	hw->phy->supported.cca_modes = BIT(NL802154_CCA_ENERGY) |
				       BIT(NL802154_CCA_CARRIER) |
				       BIT(NL802154_CCA_ENERGY_CARRIER);
	hw->phy->supported.cca_opts = BIT(NL802154_CCA_OPT_ENERGY_CARRIER_AND) |
				      BIT(NL802154_CCA_OPT_ENERGY_CARRIER_OR);

	hw->phy->cca.mode = NL802154_CCA_ENERGY;

	hw->phy->current_page = 0;

	if ((man_id_1 << 8 | man_id_0) != ATUSB_JEDEC_ATMEL) {
		dev_err(&usb_dev->dev,
			"non-Atmel transceiver xxxx%02x%02x\n",
			man_id_1, man_id_0);
		goto fail;
	}

	switch (part_num) {
	case 2:
		chip = "AT86RF230";
		atusb->hw->phy->supported.channels[0] = 0x7FFF800;
		atusb->hw->phy->current_channel = 11;	/* reset default */
		atusb->hw->phy->supported.tx_powers = atusb_powers;
		atusb->hw->phy->supported.tx_powers_size = ARRAY_SIZE(atusb_powers);
		hw->phy->supported.cca_ed_levels = atusb_ed_levels;
		hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(atusb_ed_levels);
		break;
	case 3:
		chip = "AT86RF231";
		atusb->hw->phy->supported.channels[0] = 0x7FFF800;
		atusb->hw->phy->current_channel = 11;	/* reset default */
		atusb->hw->phy->supported.tx_powers = atusb_powers;
		atusb->hw->phy->supported.tx_powers_size = ARRAY_SIZE(atusb_powers);
		hw->phy->supported.cca_ed_levels = atusb_ed_levels;
		hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(atusb_ed_levels);
		break;
	case 7:
		chip = "AT86RF212";
		atusb->hw->flags |= IEEE802154_HW_LBT;
		atusb->hw->phy->supported.channels[0] = 0x00007FF;
		atusb->hw->phy->supported.channels[2] = 0x00007FF;
		atusb->hw->phy->current_channel = 5;
		atusb->hw->phy->supported.lbt = NL802154_SUPPORTED_BOOL_BOTH;
		atusb->hw->phy->supported.tx_powers = at86rf212_powers;
		atusb->hw->phy->supported.tx_powers_size = ARRAY_SIZE(at86rf212_powers);
		atusb->hw->phy->supported.cca_ed_levels = at86rf212_ed_levels_100;
		atusb->hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(at86rf212_ed_levels_100);
		break;
	default:
		dev_err(&usb_dev->dev,
			"unexpected transceiver, part 0x%02x version 0x%02x\n",
			part_num, version_num);
		goto fail;
	}

	hw->phy->transmit_power = hw->phy->supported.tx_powers[0];
	hw->phy->cca_ed_level = hw->phy->supported.cca_ed_levels[7];

	dev_info(&usb_dev->dev, "ATUSB: %s version %d\n", chip, version_num);

	return 0;

fail:
	atusb->err = -ENODEV;
	return -ENODEV;
}

static int atusb_set_extended_addr(struct atusb *atusb)
{
	struct usb_device *usb_dev = atusb->usb_dev;
	unsigned char buffer[IEEE802154_EXTENDED_ADDR_LEN];
	__le64 extended_addr;
	u64 addr;
	int ret;

	/* Firmware versions before 0.3 do not support the EUI64_READ command.
	 * Just use a random address and be done.
	 */
	if (atusb->fw_ver_maj == 0 && atusb->fw_ver_min < 3) {
		ieee802154_random_extended_addr(&atusb->hw->phy->perm_extended_addr);
		return 0;
	}

	/* Firmware is new enough so we fetch the address from EEPROM */
	ret = usb_control_msg_recv(atusb->usb_dev, 0, ATUSB_EUI64_READ, ATUSB_REQ_FROM_DEV, 0, 0,
				   buffer, IEEE802154_EXTENDED_ADDR_LEN, 1000, GFP_KERNEL);
	if (ret < 0) {
		dev_err(&usb_dev->dev, "failed to fetch extended address, random address set\n");
		ieee802154_random_extended_addr(&atusb->hw->phy->perm_extended_addr);
		return ret;
	}

	memcpy(&extended_addr, buffer, IEEE802154_EXTENDED_ADDR_LEN);
	/* Check if read address is not empty and the unicast bit is set correctly */
	if (!ieee802154_is_valid_extended_unicast_addr(extended_addr)) {
		dev_info(&usb_dev->dev, "no permanent extended address found, random address set\n");
		ieee802154_random_extended_addr(&atusb->hw->phy->perm_extended_addr);
	} else {
		atusb->hw->phy->perm_extended_addr = extended_addr;
		addr = swab64((__force u64)atusb->hw->phy->perm_extended_addr);
		dev_info(&usb_dev->dev, "Read permanent extended address %8phC from device\n",
			 &addr);
	}

	return ret;
}

/* ----- Setup ------------------------------------------------------------- */

static int atusb_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	struct ieee802154_hw *hw;
	struct atusb *atusb = NULL;
	int ret = -ENOMEM;

	hw = ieee802154_alloc_hw(sizeof(struct atusb), &atusb_ops);
	if (!hw)
		return -ENOMEM;

	atusb = hw->priv;
	atusb->hw = hw;
	atusb->usb_dev = usb_get_dev(usb_dev);
	usb_set_intfdata(interface, atusb);

	atusb->shutdown = 0;
	atusb->err = 0;
	INIT_DELAYED_WORK(&atusb->work, atusb_work_urbs);
	init_usb_anchor(&atusb->idle_urbs);
	init_usb_anchor(&atusb->rx_urbs);

	if (atusb_alloc_urbs(atusb, ATUSB_NUM_RX_URBS))
		goto fail;

	atusb->tx_dr.bRequestType = ATUSB_REQ_TO_DEV;
	atusb->tx_dr.bRequest = ATUSB_TX;
	atusb->tx_dr.wValue = cpu_to_le16(0);

	atusb->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!atusb->tx_urb)
		goto fail;

	hw->parent = &usb_dev->dev;

	usb_control_msg_send(atusb->usb_dev, 0, ATUSB_RF_RESET, ATUSB_REQ_TO_DEV, 0, 0,
			     NULL, 0, 1000, GFP_KERNEL);
	atusb_get_and_conf_chip(atusb);
	atusb_get_and_show_revision(atusb);
	atusb_get_and_show_build(atusb);
	atusb_set_extended_addr(atusb);

	if ((atusb->fw_ver_maj == 0 && atusb->fw_ver_min >= 3) || atusb->fw_ver_maj > 0)
		hw->flags |= IEEE802154_HW_FRAME_RETRIES;

	ret = atusb_get_and_clear_error(atusb);
	if (ret) {
		dev_err(&atusb->usb_dev->dev,
			"%s: initialization failed, error = %d\n",
			__func__, ret);
		goto fail;
	}

	ret = ieee802154_register_hw(hw);
	if (ret)
		goto fail;

	/* If we just powered on, we're now in P_ON and need to enter TRX_OFF
	 * explicitly. Any resets after that will send us straight to TRX_OFF,
	 * making the command below redundant.
	 */
	usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
			     STATE_FORCE_TRX_OFF, RG_TRX_STATE, NULL, 0, 1000, GFP_KERNEL);

	msleep(1);	/* reset => TRX_OFF, tTR13 = 37 us */

#if 0
	/* Calculating the maximum time available to empty the frame buffer
	 * on reception:
	 *
	 * According to [1], the inter-frame gap is
	 * R * 20 * 16 us + 128 us
	 * where R is a random number from 0 to 7. Furthermore, we have 20 bit
	 * times (80 us at 250 kbps) of SHR of the next frame before the
	 * transceiver begins storing data in the frame buffer.
	 *
	 * This yields a minimum time of 208 us between the last data of a
	 * frame and the first data of the next frame. This time is further
	 * reduced by interrupt latency in the atusb firmware.
	 *
	 * atusb currently needs about 500 us to retrieve a maximum-sized
	 * frame. We therefore have to allow reception of a new frame to begin
	 * while we retrieve the previous frame.
	 *
	 * [1] "JN-AN-1035 Calculating data rates in an IEEE 802.15.4-based
	 *      network", Jennic 2006.
	 *     http://www.jennic.com/download_file.php?supportFile=JN-AN-1035%20Calculating%20802-15-4%20Data%20Rates-1v0.pdf
	 */

	atusb_write_subreg(atusb, SR_RX_SAFE_MODE, 1);
#endif
	usb_control_msg_send(atusb->usb_dev, 0, ATUSB_REG_WRITE, ATUSB_REQ_TO_DEV,
			     0xff, RG_IRQ_MASK, NULL, 0, 1000, GFP_KERNEL);

	ret = atusb_get_and_clear_error(atusb);
	if (!ret)
		return 0;

	dev_err(&atusb->usb_dev->dev,
		"%s: setup failed, error = %d\n",
		__func__, ret);

	ieee802154_unregister_hw(hw);
fail:
	atusb_free_urbs(atusb);
	usb_kill_urb(atusb->tx_urb);
	usb_free_urb(atusb->tx_urb);
	usb_put_dev(usb_dev);
	ieee802154_free_hw(hw);
	return ret;
}

static void atusb_disconnect(struct usb_interface *interface)
{
	struct atusb *atusb = usb_get_intfdata(interface);

	dev_dbg(&atusb->usb_dev->dev, "%s\n", __func__);

	atusb->shutdown = 1;
	cancel_delayed_work_sync(&atusb->work);

	usb_kill_anchored_urbs(&atusb->rx_urbs);
	atusb_free_urbs(atusb);
	usb_kill_urb(atusb->tx_urb);
	usb_free_urb(atusb->tx_urb);

	ieee802154_unregister_hw(atusb->hw);

	usb_put_dev(atusb->usb_dev);

	ieee802154_free_hw(atusb->hw);

	usb_set_intfdata(interface, NULL);

	pr_debug("%s done\n", __func__);
}

/* The devices we work with */
static const struct usb_device_id atusb_device_table[] = {
	{
		.match_flags		= USB_DEVICE_ID_MATCH_DEVICE |
					  USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor		= ATUSB_VENDOR_ID,
		.idProduct		= ATUSB_PRODUCT_ID,
		.bInterfaceClass	= USB_CLASS_VENDOR_SPEC
	},
	/* end with null element */
	{}
};
MODULE_DEVICE_TABLE(usb, atusb_device_table);

static struct usb_driver atusb_driver = {
	.name		= "atusb",
	.probe		= atusb_probe,
	.disconnect	= atusb_disconnect,
	.id_table	= atusb_device_table,
};
module_usb_driver(atusb_driver);

MODULE_AUTHOR("Alexander Aring <alex.aring@gmail.com>");
MODULE_AUTHOR("Richard Sharpe <realrichardsharpe@gmail.com>");
MODULE_AUTHOR("Stefan Schmidt <stefan@datenfreihafen.org>");
MODULE_AUTHOR("Werner Almesberger <werner@almesberger.net>");
MODULE_AUTHOR("Josef Filzmaier <j.filzmaier@gmx.at>");
MODULE_DESCRIPTION("ATUSB IEEE 802.15.4 Driver");
MODULE_LICENSE("GPL");
