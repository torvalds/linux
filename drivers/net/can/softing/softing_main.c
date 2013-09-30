/*
 * Copyright (C) 2008-2010
 *
 * - Kurt Van Dijck, EIA Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "softing.h"

#define TX_ECHO_SKB_MAX (((TXMAX+1)/2)-1)

/*
 * test is a specific CAN netdev
 * is online (ie. up 'n running, not sleeping, not busoff
 */
static inline int canif_is_active(struct net_device *netdev)
{
	struct can_priv *can = netdev_priv(netdev);

	if (!netif_running(netdev))
		return 0;
	return (can->state <= CAN_STATE_ERROR_PASSIVE);
}

/* reset DPRAM */
static inline void softing_set_reset_dpram(struct softing *card)
{
	if (card->pdat->generation >= 2) {
		spin_lock_bh(&card->spin);
		iowrite8(ioread8(&card->dpram[DPRAM_V2_RESET]) & ~1,
				&card->dpram[DPRAM_V2_RESET]);
		spin_unlock_bh(&card->spin);
	}
}

static inline void softing_clr_reset_dpram(struct softing *card)
{
	if (card->pdat->generation >= 2) {
		spin_lock_bh(&card->spin);
		iowrite8(ioread8(&card->dpram[DPRAM_V2_RESET]) | 1,
				&card->dpram[DPRAM_V2_RESET]);
		spin_unlock_bh(&card->spin);
	}
}

/* trigger the tx queue-ing */
static netdev_tx_t softing_netdev_start_xmit(struct sk_buff *skb,
		struct net_device *dev)
{
	struct softing_priv *priv = netdev_priv(dev);
	struct softing *card = priv->card;
	int ret;
	uint8_t *ptr;
	uint8_t fifo_wr, fifo_rd;
	struct can_frame *cf = (struct can_frame *)skb->data;
	uint8_t buf[DPRAM_TX_SIZE];

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	spin_lock(&card->spin);

	ret = NETDEV_TX_BUSY;
	if (!card->fw.up ||
			(card->tx.pending >= TXMAX) ||
			(priv->tx.pending >= TX_ECHO_SKB_MAX))
		goto xmit_done;
	fifo_wr = ioread8(&card->dpram[DPRAM_TX_WR]);
	fifo_rd = ioread8(&card->dpram[DPRAM_TX_RD]);
	if (fifo_wr == fifo_rd)
		/* fifo full */
		goto xmit_done;
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	*ptr = CMD_TX;
	if (cf->can_id & CAN_RTR_FLAG)
		*ptr |= CMD_RTR;
	if (cf->can_id & CAN_EFF_FLAG)
		*ptr |= CMD_XTD;
	if (priv->index)
		*ptr |= CMD_BUS2;
	++ptr;
	*ptr++ = cf->can_dlc;
	*ptr++ = (cf->can_id >> 0);
	*ptr++ = (cf->can_id >> 8);
	if (cf->can_id & CAN_EFF_FLAG) {
		*ptr++ = (cf->can_id >> 16);
		*ptr++ = (cf->can_id >> 24);
	} else {
		/* increment 1, not 2 as you might think */
		ptr += 1;
	}
	if (!(cf->can_id & CAN_RTR_FLAG))
		memcpy(ptr, &cf->data[0], cf->can_dlc);
	memcpy_toio(&card->dpram[DPRAM_TX + DPRAM_TX_SIZE * fifo_wr],
			buf, DPRAM_TX_SIZE);
	if (++fifo_wr >= DPRAM_TX_CNT)
		fifo_wr = 0;
	iowrite8(fifo_wr, &card->dpram[DPRAM_TX_WR]);
	card->tx.last_bus = priv->index;
	++card->tx.pending;
	++priv->tx.pending;
	can_put_echo_skb(skb, dev, priv->tx.echo_put);
	++priv->tx.echo_put;
	if (priv->tx.echo_put >= TX_ECHO_SKB_MAX)
		priv->tx.echo_put = 0;
	/* can_put_echo_skb() saves the skb, safe to return TX_OK */
	ret = NETDEV_TX_OK;
xmit_done:
	spin_unlock(&card->spin);
	if (card->tx.pending >= TXMAX) {
		int j;
		for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
			if (card->net[j])
				netif_stop_queue(card->net[j]);
		}
	}
	if (ret != NETDEV_TX_OK)
		netif_stop_queue(dev);

	return ret;
}

/*
 * shortcut for skb delivery
 */
int softing_netdev_rx(struct net_device *netdev, const struct can_frame *msg,
		ktime_t ktime)
{
	struct sk_buff *skb;
	struct can_frame *cf;

	skb = alloc_can_skb(netdev, &cf);
	if (!skb)
		return -ENOMEM;
	memcpy(cf, msg, sizeof(*msg));
	skb->tstamp = ktime;
	return netif_rx(skb);
}

/*
 * softing_handle_1
 * pop 1 entry from the DPRAM queue, and process
 */
static int softing_handle_1(struct softing *card)
{
	struct net_device *netdev;
	struct softing_priv *priv;
	ktime_t ktime;
	struct can_frame msg;
	int cnt = 0, lost_msg;
	uint8_t fifo_rd, fifo_wr, cmd;
	uint8_t *ptr;
	uint32_t tmp_u32;
	uint8_t buf[DPRAM_RX_SIZE];

	memset(&msg, 0, sizeof(msg));
	/* test for lost msgs */
	lost_msg = ioread8(&card->dpram[DPRAM_RX_LOST]);
	if (lost_msg) {
		int j;
		/* reset condition */
		iowrite8(0, &card->dpram[DPRAM_RX_LOST]);
		/* prepare msg */
		msg.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
		msg.can_dlc = CAN_ERR_DLC;
		msg.data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		/*
		 * service to all busses, we don't know which it was applicable
		 * but only service busses that are online
		 */
		for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
			netdev = card->net[j];
			if (!netdev)
				continue;
			if (!canif_is_active(netdev))
				/* a dead bus has no overflows */
				continue;
			++netdev->stats.rx_over_errors;
			softing_netdev_rx(netdev, &msg, ktime_set(0, 0));
		}
		/* prepare for other use */
		memset(&msg, 0, sizeof(msg));
		++cnt;
	}

	fifo_rd = ioread8(&card->dpram[DPRAM_RX_RD]);
	fifo_wr = ioread8(&card->dpram[DPRAM_RX_WR]);

	if (++fifo_rd >= DPRAM_RX_CNT)
		fifo_rd = 0;
	if (fifo_wr == fifo_rd)
		return cnt;

	memcpy_fromio(buf, &card->dpram[DPRAM_RX + DPRAM_RX_SIZE*fifo_rd],
			DPRAM_RX_SIZE);
	mb();
	/* trigger dual port RAM */
	iowrite8(fifo_rd, &card->dpram[DPRAM_RX_RD]);

	ptr = buf;
	cmd = *ptr++;
	if (cmd == 0xff)
		/* not quite useful, probably the card has got out */
		return 0;
	netdev = card->net[0];
	if (cmd & CMD_BUS2)
		netdev = card->net[1];
	priv = netdev_priv(netdev);

	if (cmd & CMD_ERR) {
		uint8_t can_state, state;

		state = *ptr++;

		msg.can_id = CAN_ERR_FLAG;
		msg.can_dlc = CAN_ERR_DLC;

		if (state & SF_MASK_BUSOFF) {
			can_state = CAN_STATE_BUS_OFF;
			msg.can_id |= CAN_ERR_BUSOFF;
			state = STATE_BUSOFF;
		} else if (state & SF_MASK_EPASSIVE) {
			can_state = CAN_STATE_ERROR_PASSIVE;
			msg.can_id |= CAN_ERR_CRTL;
			msg.data[1] = CAN_ERR_CRTL_TX_PASSIVE;
			state = STATE_EPASSIVE;
		} else {
			can_state = CAN_STATE_ERROR_ACTIVE;
			msg.can_id |= CAN_ERR_CRTL;
			state = STATE_EACTIVE;
		}
		/* update DPRAM */
		iowrite8(state, &card->dpram[priv->index ?
				DPRAM_INFO_BUSSTATE2 : DPRAM_INFO_BUSSTATE]);
		/* timestamp */
		tmp_u32 = le32_to_cpup((void *)ptr);
		ptr += 4;
		ktime = softing_raw2ktime(card, tmp_u32);

		++netdev->stats.rx_errors;
		/* update internal status */
		if (can_state != priv->can.state) {
			priv->can.state = can_state;
			if (can_state == CAN_STATE_ERROR_PASSIVE)
				++priv->can.can_stats.error_passive;
			else if (can_state == CAN_STATE_BUS_OFF) {
				/* this calls can_close_cleanup() */
				can_bus_off(netdev);
				netif_stop_queue(netdev);
			}
			/* trigger socketcan */
			softing_netdev_rx(netdev, &msg, ktime);
		}

	} else {
		if (cmd & CMD_RTR)
			msg.can_id |= CAN_RTR_FLAG;
		msg.can_dlc = get_can_dlc(*ptr++);
		if (cmd & CMD_XTD) {
			msg.can_id |= CAN_EFF_FLAG;
			msg.can_id |= le32_to_cpup((void *)ptr);
			ptr += 4;
		} else {
			msg.can_id |= le16_to_cpup((void *)ptr);
			ptr += 2;
		}
		/* timestamp */
		tmp_u32 = le32_to_cpup((void *)ptr);
		ptr += 4;
		ktime = softing_raw2ktime(card, tmp_u32);
		if (!(msg.can_id & CAN_RTR_FLAG))
			memcpy(&msg.data[0], ptr, 8);
		ptr += 8;
		/* update socket */
		if (cmd & CMD_ACK) {
			/* acknowledge, was tx msg */
			struct sk_buff *skb;
			skb = priv->can.echo_skb[priv->tx.echo_get];
			if (skb)
				skb->tstamp = ktime;
			can_get_echo_skb(netdev, priv->tx.echo_get);
			++priv->tx.echo_get;
			if (priv->tx.echo_get >= TX_ECHO_SKB_MAX)
				priv->tx.echo_get = 0;
			if (priv->tx.pending)
				--priv->tx.pending;
			if (card->tx.pending)
				--card->tx.pending;
			++netdev->stats.tx_packets;
			if (!(msg.can_id & CAN_RTR_FLAG))
				netdev->stats.tx_bytes += msg.can_dlc;
		} else {
			int ret;

			ret = softing_netdev_rx(netdev, &msg, ktime);
			if (ret == NET_RX_SUCCESS) {
				++netdev->stats.rx_packets;
				if (!(msg.can_id & CAN_RTR_FLAG))
					netdev->stats.rx_bytes += msg.can_dlc;
			} else {
				++netdev->stats.rx_dropped;
			}
		}
	}
	++cnt;
	return cnt;
}

/*
 * real interrupt handler
 */
static irqreturn_t softing_irq_thread(int irq, void *dev_id)
{
	struct softing *card = (struct softing *)dev_id;
	struct net_device *netdev;
	struct softing_priv *priv;
	int j, offset, work_done;

	work_done = 0;
	spin_lock_bh(&card->spin);
	while (softing_handle_1(card) > 0) {
		++card->irq.svc_count;
		++work_done;
	}
	spin_unlock_bh(&card->spin);
	/* resume tx queue's */
	offset = card->tx.last_bus;
	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		if (card->tx.pending >= TXMAX)
			break;
		netdev = card->net[(j + offset + 1) % card->pdat->nbus];
		if (!netdev)
			continue;
		priv = netdev_priv(netdev);
		if (!canif_is_active(netdev))
			/* it makes no sense to wake dead busses */
			continue;
		if (priv->tx.pending >= TX_ECHO_SKB_MAX)
			continue;
		++work_done;
		netif_wake_queue(netdev);
	}
	return work_done ? IRQ_HANDLED : IRQ_NONE;
}

/*
 * interrupt routines:
 * schedule the 'real interrupt handler'
 */
static irqreturn_t softing_irq_v2(int irq, void *dev_id)
{
	struct softing *card = (struct softing *)dev_id;
	uint8_t ir;

	ir = ioread8(&card->dpram[DPRAM_V2_IRQ_TOHOST]);
	iowrite8(0, &card->dpram[DPRAM_V2_IRQ_TOHOST]);
	return (1 == ir) ? IRQ_WAKE_THREAD : IRQ_NONE;
}

static irqreturn_t softing_irq_v1(int irq, void *dev_id)
{
	struct softing *card = (struct softing *)dev_id;
	uint8_t ir;

	ir = ioread8(&card->dpram[DPRAM_IRQ_TOHOST]);
	iowrite8(0, &card->dpram[DPRAM_IRQ_TOHOST]);
	return ir ? IRQ_WAKE_THREAD : IRQ_NONE;
}

/*
 * netdev/candev inter-operability
 */
static int softing_netdev_open(struct net_device *ndev)
{
	int ret;

	/* check or determine and set bittime */
	ret = open_candev(ndev);
	if (!ret)
		ret = softing_startstop(ndev, 1);
	return ret;
}

static int softing_netdev_stop(struct net_device *ndev)
{
	int ret;

	netif_stop_queue(ndev);

	/* softing cycle does close_candev() */
	ret = softing_startstop(ndev, 0);
	return ret;
}

static int softing_candev_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		/* softing_startstop does close_candev() */
		ret = softing_startstop(ndev, 1);
		return ret;
	case CAN_MODE_STOP:
	case CAN_MODE_SLEEP:
		return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * Softing device management helpers
 */
int softing_enable_irq(struct softing *card, int enable)
{
	int ret;

	if (!card->irq.nr) {
		return 0;
	} else if (card->irq.requested && !enable) {
		free_irq(card->irq.nr, card);
		card->irq.requested = 0;
	} else if (!card->irq.requested && enable) {
		ret = request_threaded_irq(card->irq.nr,
				(card->pdat->generation >= 2) ?
					softing_irq_v2 : softing_irq_v1,
				softing_irq_thread, IRQF_SHARED,
				dev_name(&card->pdev->dev), card);
		if (ret) {
			dev_alert(&card->pdev->dev,
					"request_threaded_irq(%u) failed\n",
					card->irq.nr);
			return ret;
		}
		card->irq.requested = 1;
	}
	return 0;
}

static void softing_card_shutdown(struct softing *card)
{
	int fw_up = 0;

	if (mutex_lock_interruptible(&card->fw.lock))
		/* return -ERESTARTSYS */;
	fw_up = card->fw.up;
	card->fw.up = 0;

	if (card->irq.requested && card->irq.nr) {
		free_irq(card->irq.nr, card);
		card->irq.requested = 0;
	}
	if (fw_up) {
		if (card->pdat->enable_irq)
			card->pdat->enable_irq(card->pdev, 0);
		softing_set_reset_dpram(card);
		if (card->pdat->reset)
			card->pdat->reset(card->pdev, 1);
	}
	mutex_unlock(&card->fw.lock);
}

static int softing_card_boot(struct softing *card)
{
	int ret, j;
	static const uint8_t stream[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, };
	unsigned char back[sizeof(stream)];

	if (mutex_lock_interruptible(&card->fw.lock))
		return -ERESTARTSYS;
	if (card->fw.up) {
		mutex_unlock(&card->fw.lock);
		return 0;
	}
	/* reset board */
	if (card->pdat->enable_irq)
		card->pdat->enable_irq(card->pdev, 1);
	/* boot card */
	softing_set_reset_dpram(card);
	if (card->pdat->reset)
		card->pdat->reset(card->pdev, 1);
	for (j = 0; (j + sizeof(stream)) < card->dpram_size;
			j += sizeof(stream)) {

		memcpy_toio(&card->dpram[j], stream, sizeof(stream));
		/* flush IO cache */
		mb();
		memcpy_fromio(back, &card->dpram[j], sizeof(stream));

		if (!memcmp(back, stream, sizeof(stream)))
			continue;
		/* memory is not equal */
		dev_alert(&card->pdev->dev, "dpram failed at 0x%04x\n", j);
		ret = -EIO;
		goto failed;
	}
	wmb();
	/* load boot firmware */
	ret = softing_load_fw(card->pdat->boot.fw, card, card->dpram,
				card->dpram_size,
				card->pdat->boot.offs - card->pdat->boot.addr);
	if (ret < 0)
		goto failed;
	/* load loader firmware */
	ret = softing_load_fw(card->pdat->load.fw, card, card->dpram,
				card->dpram_size,
				card->pdat->load.offs - card->pdat->load.addr);
	if (ret < 0)
		goto failed;

	if (card->pdat->reset)
		card->pdat->reset(card->pdev, 0);
	softing_clr_reset_dpram(card);
	ret = softing_bootloader_command(card, 0, "card boot");
	if (ret < 0)
		goto failed;
	ret = softing_load_app_fw(card->pdat->app.fw, card);
	if (ret < 0)
		goto failed;

	ret = softing_chip_poweron(card);
	if (ret < 0)
		goto failed;

	card->fw.up = 1;
	mutex_unlock(&card->fw.lock);
	return 0;
failed:
	card->fw.up = 0;
	if (card->pdat->enable_irq)
		card->pdat->enable_irq(card->pdev, 0);
	softing_set_reset_dpram(card);
	if (card->pdat->reset)
		card->pdat->reset(card->pdev, 1);
	mutex_unlock(&card->fw.lock);
	return ret;
}

/*
 * netdev sysfs
 */
static ssize_t show_channel(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	struct softing_priv *priv = netdev2softing(ndev);

	return sprintf(buf, "%i\n", priv->index);
}

static ssize_t show_chip(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	struct softing_priv *priv = netdev2softing(ndev);

	return sprintf(buf, "%i\n", priv->chip);
}

static ssize_t show_output(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	struct softing_priv *priv = netdev2softing(ndev);

	return sprintf(buf, "0x%02x\n", priv->output);
}

static ssize_t store_output(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	struct softing_priv *priv = netdev2softing(ndev);
	struct softing *card = priv->card;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	val &= 0xFF;

	ret = mutex_lock_interruptible(&card->fw.lock);
	if (ret)
		return -ERESTARTSYS;
	if (netif_running(ndev)) {
		mutex_unlock(&card->fw.lock);
		return -EBUSY;
	}
	priv->output = val;
	mutex_unlock(&card->fw.lock);
	return count;
}

static const DEVICE_ATTR(channel, S_IRUGO, show_channel, NULL);
static const DEVICE_ATTR(chip, S_IRUGO, show_chip, NULL);
static const DEVICE_ATTR(output, S_IRUGO | S_IWUSR, show_output, store_output);

static const struct attribute *const netdev_sysfs_attrs[] = {
	&dev_attr_channel.attr,
	&dev_attr_chip.attr,
	&dev_attr_output.attr,
	NULL,
};
static const struct attribute_group netdev_sysfs_group = {
	.name = NULL,
	.attrs = (struct attribute **)netdev_sysfs_attrs,
};

static const struct net_device_ops softing_netdev_ops = {
	.ndo_open = softing_netdev_open,
	.ndo_stop = softing_netdev_stop,
	.ndo_start_xmit	= softing_netdev_start_xmit,
};

static const struct can_bittiming_const softing_btr_const = {
	.name = "softing",
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4, /* overruled */
	.brp_min = 1,
	.brp_max = 32, /* overruled */
	.brp_inc = 1,
};


static struct net_device *softing_netdev_create(struct softing *card,
						uint16_t chip_id)
{
	struct net_device *netdev;
	struct softing_priv *priv;

	netdev = alloc_candev(sizeof(*priv), TX_ECHO_SKB_MAX);
	if (!netdev) {
		dev_alert(&card->pdev->dev, "alloc_candev failed\n");
		return NULL;
	}
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->card = card;
	memcpy(&priv->btr_const, &softing_btr_const, sizeof(priv->btr_const));
	priv->btr_const.brp_max = card->pdat->max_brp;
	priv->btr_const.sjw_max = card->pdat->max_sjw;
	priv->can.bittiming_const = &priv->btr_const;
	priv->can.clock.freq = 8000000;
	priv->chip = chip_id;
	priv->output = softing_default_output(netdev);
	SET_NETDEV_DEV(netdev, &card->pdev->dev);

	netdev->flags |= IFF_ECHO;
	netdev->netdev_ops = &softing_netdev_ops;
	priv->can.do_set_mode = softing_candev_set_mode;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;

	return netdev;
}

static int softing_netdev_register(struct net_device *netdev)
{
	int ret;

	netdev->sysfs_groups[0] = &netdev_sysfs_group;
	ret = register_candev(netdev);
	if (ret) {
		dev_alert(&netdev->dev, "register failed\n");
		return ret;
	}
	return 0;
}

static void softing_netdev_cleanup(struct net_device *netdev)
{
	unregister_candev(netdev);
	free_candev(netdev);
}

/*
 * sysfs for Platform device
 */
#define DEV_ATTR_RO(name, member) \
static ssize_t show_##name(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	struct softing *card = platform_get_drvdata(to_platform_device(dev)); \
	return sprintf(buf, "%u\n", card->member); \
} \
static DEVICE_ATTR(name, 0444, show_##name, NULL)

#define DEV_ATTR_RO_STR(name, member) \
static ssize_t show_##name(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	struct softing *card = platform_get_drvdata(to_platform_device(dev)); \
	return sprintf(buf, "%s\n", card->member); \
} \
static DEVICE_ATTR(name, 0444, show_##name, NULL)

DEV_ATTR_RO(serial, id.serial);
DEV_ATTR_RO_STR(firmware, pdat->app.fw);
DEV_ATTR_RO(firmware_version, id.fw_version);
DEV_ATTR_RO_STR(hardware, pdat->name);
DEV_ATTR_RO(hardware_version, id.hw_version);
DEV_ATTR_RO(license, id.license);
DEV_ATTR_RO(frequency, id.freq);
DEV_ATTR_RO(txpending, tx.pending);

static struct attribute *softing_pdev_attrs[] = {
	&dev_attr_serial.attr,
	&dev_attr_firmware.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_hardware.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_license.attr,
	&dev_attr_frequency.attr,
	&dev_attr_txpending.attr,
	NULL,
};

static const struct attribute_group softing_pdev_group = {
	.name = NULL,
	.attrs = softing_pdev_attrs,
};

/*
 * platform driver
 */
static int softing_pdev_remove(struct platform_device *pdev)
{
	struct softing *card = platform_get_drvdata(pdev);
	int j;

	/* first, disable card*/
	softing_card_shutdown(card);

	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		if (!card->net[j])
			continue;
		softing_netdev_cleanup(card->net[j]);
		card->net[j] = NULL;
	}
	sysfs_remove_group(&pdev->dev.kobj, &softing_pdev_group);

	iounmap(card->dpram);
	kfree(card);
	return 0;
}

static int softing_pdev_probe(struct platform_device *pdev)
{
	const struct softing_platform_data *pdat = dev_get_platdata(&pdev->dev);
	struct softing *card;
	struct net_device *netdev;
	struct softing_priv *priv;
	struct resource *pres;
	int ret;
	int j;

	if (!pdat) {
		dev_warn(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}
	if (pdat->nbus > ARRAY_SIZE(card->net)) {
		dev_warn(&pdev->dev, "%u nets??\n", pdat->nbus);
		return -EINVAL;
	}

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	card->pdat = pdat;
	card->pdev = pdev;
	platform_set_drvdata(pdev, card);
	mutex_init(&card->fw.lock);
	spin_lock_init(&card->spin);

	ret = -EINVAL;
	pres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pres)
		goto platform_resource_failed;
	card->dpram_phys = pres->start;
	card->dpram_size = resource_size(pres);
	card->dpram = ioremap_nocache(card->dpram_phys, card->dpram_size);
	if (!card->dpram) {
		dev_alert(&card->pdev->dev, "dpram ioremap failed\n");
		goto ioremap_failed;
	}

	pres = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (pres)
		card->irq.nr = pres->start;

	/* reset card */
	ret = softing_card_boot(card);
	if (ret < 0) {
		dev_alert(&pdev->dev, "failed to boot\n");
		goto boot_failed;
	}

	/* only now, the chip's are known */
	card->id.freq = card->pdat->freq;

	ret = sysfs_create_group(&pdev->dev.kobj, &softing_pdev_group);
	if (ret < 0) {
		dev_alert(&card->pdev->dev, "sysfs failed\n");
		goto sysfs_failed;
	}

	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		card->net[j] = netdev =
			softing_netdev_create(card, card->id.chip[j]);
		if (!netdev) {
			dev_alert(&pdev->dev, "failed to make can[%i]", j);
			ret = -ENOMEM;
			goto netdev_failed;
		}
		priv = netdev_priv(card->net[j]);
		priv->index = j;
		ret = softing_netdev_register(netdev);
		if (ret) {
			free_candev(netdev);
			card->net[j] = NULL;
			dev_alert(&card->pdev->dev,
					"failed to register can[%i]\n", j);
			goto netdev_failed;
		}
	}
	dev_info(&card->pdev->dev, "%s ready.\n", card->pdat->name);
	return 0;

netdev_failed:
	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		if (!card->net[j])
			continue;
		softing_netdev_cleanup(card->net[j]);
	}
	sysfs_remove_group(&pdev->dev.kobj, &softing_pdev_group);
sysfs_failed:
	softing_card_shutdown(card);
boot_failed:
	iounmap(card->dpram);
ioremap_failed:
platform_resource_failed:
	kfree(card);
	return ret;
}

static struct platform_driver softing_driver = {
	.driver = {
		.name = "softing",
		.owner = THIS_MODULE,
	},
	.probe = softing_pdev_probe,
	.remove = softing_pdev_remove,
};

module_platform_driver(softing_driver);

MODULE_ALIAS("platform:softing");
MODULE_DESCRIPTION("Softing DPRAM CAN driver");
MODULE_AUTHOR("Kurt Van Dijck <kurt.van.dijck@eia.be>");
MODULE_LICENSE("GPL v2");
