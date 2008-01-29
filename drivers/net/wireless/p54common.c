
/*
 * Common code for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007, Christian Lamparter <chunkeey@web.de>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "p54.h"
#include "p54common.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Softmac Prism54 common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54common");

void p54_parse_firmware(struct ieee80211_hw *dev, const struct firmware *fw)
{
	struct p54_common *priv = dev->priv;
	struct bootrec_exp_if *exp_if;
	struct bootrec *bootrec;
	u32 *data = (u32 *)fw->data;
	u32 *end_data = (u32 *)fw->data + (fw->size >> 2);
	u8 *fw_version = NULL;
	size_t len;
	int i;

	if (priv->rx_start)
		return;

	while (data < end_data && *data)
		data++;

	while (data < end_data && !*data)
		data++;

	bootrec = (struct bootrec *) data;

	while (bootrec->data <= end_data &&
	       (bootrec->data + (len = le32_to_cpu(bootrec->len))) <= end_data) {
		u32 code = le32_to_cpu(bootrec->code);
		switch (code) {
		case BR_CODE_COMPONENT_ID:
			switch (be32_to_cpu(*(__be32 *)bootrec->data)) {
			case FW_FMAC:
				printk(KERN_INFO "p54: FreeMAC firmware\n");
				break;
			case FW_LM20:
				printk(KERN_INFO "p54: LM20 firmware\n");
				break;
			case FW_LM86:
				printk(KERN_INFO "p54: LM86 firmware\n");
				break;
			case FW_LM87:
				printk(KERN_INFO "p54: LM87 firmware - not supported yet!\n");
				break;
			default:
				printk(KERN_INFO "p54: unknown firmware\n");
				break;
			}
			break;
		case BR_CODE_COMPONENT_VERSION:
			/* 24 bytes should be enough for all firmwares */
			if (strnlen((unsigned char*)bootrec->data, 24) < 24)
				fw_version = (unsigned char*)bootrec->data;
			break;
		case BR_CODE_DESCR:
			priv->rx_start = le32_to_cpu(((__le32 *)bootrec->data)[1]);
			/* FIXME add sanity checking */
			priv->rx_end = le32_to_cpu(((__le32 *)bootrec->data)[2]) - 0x3500;
			break;
		case BR_CODE_EXPOSED_IF:
			exp_if = (struct bootrec_exp_if *) bootrec->data;
			for (i = 0; i < (len * sizeof(*exp_if) / 4); i++)
				if (exp_if[i].if_id == cpu_to_le16(0x1a))
					priv->fw_var = le16_to_cpu(exp_if[i].variant);
			break;
		case BR_CODE_DEPENDENT_IF:
			break;
		case BR_CODE_END_OF_BRA:
		case LEGACY_BR_CODE_END_OF_BRA:
			end_data = NULL;
			break;
		default:
			break;
		}
		bootrec = (struct bootrec *)&bootrec->data[len];
	}

	if (fw_version)
		printk(KERN_INFO "p54: FW rev %s - Softmac protocol %x.%x\n",
			fw_version, priv->fw_var >> 8, priv->fw_var & 0xff);

	if (priv->fw_var >= 0x300) {
		/* Firmware supports QoS, use it! */
		priv->tx_stats.data[0].limit = 3;
		priv->tx_stats.data[1].limit = 4;
		priv->tx_stats.data[2].limit = 3;
		priv->tx_stats.data[3].limit = 1;
		dev->queues = 4;
	}
}
EXPORT_SYMBOL_GPL(p54_parse_firmware);

static int p54_convert_rev0_to_rev1(struct ieee80211_hw *dev,
				    struct pda_pa_curve_data *curve_data)
{
	struct p54_common *priv = dev->priv;
	struct pda_pa_curve_data_sample_rev1 *rev1;
	struct pda_pa_curve_data_sample_rev0 *rev0;
	size_t cd_len = sizeof(*curve_data) +
		(curve_data->points_per_channel*sizeof(*rev1) + 2) *
		 curve_data->channels;
	unsigned int i, j;
	void *source, *target;

	priv->curve_data = kmalloc(cd_len, GFP_KERNEL);
	if (!priv->curve_data)
		return -ENOMEM;

	memcpy(priv->curve_data, curve_data, sizeof(*curve_data));
	source = curve_data->data;
	target = priv->curve_data->data;
	for (i = 0; i < curve_data->channels; i++) {
		__le16 *freq = source;
		source += sizeof(__le16);
		*((__le16 *)target) = *freq;
		target += sizeof(__le16);
		for (j = 0; j < curve_data->points_per_channel; j++) {
			rev1 = target;
			rev0 = source;

			rev1->rf_power = rev0->rf_power;
			rev1->pa_detector = rev0->pa_detector;
			rev1->data_64qam = rev0->pcv;
			/* "invent" the points for the other modulations */
#define SUB(x,y) (u8)((x) - (y)) > (x) ? 0 : (x) - (y)
			rev1->data_16qam = SUB(rev0->pcv, 12);
			rev1->data_qpsk  = SUB(rev1->data_16qam, 12);
			rev1->data_bpsk  = SUB(rev1->data_qpsk, 12);
			rev1->data_barker= SUB(rev1->data_bpsk, 14);
#undef SUB
			target += sizeof(*rev1);
			source += sizeof(*rev0);
		}
	}

	return 0;
}

int p54_parse_eeprom(struct ieee80211_hw *dev, void *eeprom, int len)
{
	struct p54_common *priv = dev->priv;
	struct eeprom_pda_wrap *wrap = NULL;
	struct pda_entry *entry;
	int i = 0;
	unsigned int data_len, entry_len;
	void *tmp;
	int err;

	wrap = (struct eeprom_pda_wrap *) eeprom;
	entry = (void *)wrap->data + wrap->len;
	i += 2;
	i += le16_to_cpu(entry->len)*2;
	while (i < len) {
		entry_len = le16_to_cpu(entry->len);
		data_len = ((entry_len - 1) << 1);
		switch (le16_to_cpu(entry->code)) {
		case PDR_MAC_ADDRESS:
			SET_IEEE80211_PERM_ADDR(dev, entry->data);
			break;
		case PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS:
			if (data_len < 2) {
				err = -EINVAL;
				goto err;
			}

			if (2 + entry->data[1]*sizeof(*priv->output_limit) > data_len) {
				err = -EINVAL;
				goto err;
			}

			priv->output_limit = kmalloc(entry->data[1] *
				sizeof(*priv->output_limit), GFP_KERNEL);

			if (!priv->output_limit) {
				err = -ENOMEM;
				goto err;
			}

			memcpy(priv->output_limit, &entry->data[2],
			       entry->data[1]*sizeof(*priv->output_limit));
			priv->output_limit_len = entry->data[1];
			break;
		case PDR_PRISM_PA_CAL_CURVE_DATA:
			if (data_len < sizeof(struct pda_pa_curve_data)) {
				err = -EINVAL;
				goto err;
			}

			if (((struct pda_pa_curve_data *)entry->data)->cal_method_rev) {
				priv->curve_data = kmalloc(data_len, GFP_KERNEL);
				if (!priv->curve_data) {
					err = -ENOMEM;
					goto err;
				}

				memcpy(priv->curve_data, entry->data, data_len);
			} else {
				err = p54_convert_rev0_to_rev1(dev, (struct pda_pa_curve_data *)entry->data);
				if (err)
					goto err;
			}

			break;
		case PDR_PRISM_ZIF_TX_IQ_CALIBRATION:
			priv->iq_autocal = kmalloc(data_len, GFP_KERNEL);
			if (!priv->iq_autocal) {
				err = -ENOMEM;
				goto err;
			}

			memcpy(priv->iq_autocal, entry->data, data_len);
			priv->iq_autocal_len = data_len / sizeof(struct pda_iq_autocal_entry);
			break;
		case PDR_INTERFACE_LIST:
			tmp = entry->data;
			while ((u8 *)tmp < entry->data + data_len) {
				struct bootrec_exp_if *exp_if = tmp;
				if (le16_to_cpu(exp_if->if_id) == 0xF)
					priv->rxhw = exp_if->variant & cpu_to_le16(0x07);
				tmp += sizeof(struct bootrec_exp_if);
			}
			break;
		case PDR_HARDWARE_PLATFORM_COMPONENT_ID:
			priv->version = *(u8 *)(entry->data + 1);
			break;
		case PDR_END:
			i = len;
			break;
		}

		entry = (void *)entry + (entry_len + 1)*2;
		i += 2;
		i += entry_len*2;
	}

	if (!priv->iq_autocal || !priv->output_limit || !priv->curve_data) {
		printk(KERN_ERR "p54: not all required entries found in eeprom!\n");
		err = -EINVAL;
		goto err;
	}

	return 0;

  err:
	if (priv->iq_autocal) {
		kfree(priv->iq_autocal);
		priv->iq_autocal = NULL;
	}

	if (priv->output_limit) {
		kfree(priv->output_limit);
		priv->output_limit = NULL;
	}

	if (priv->curve_data) {
		kfree(priv->curve_data);
		priv->curve_data = NULL;
	}

	printk(KERN_ERR "p54: eeprom parse failed!\n");
	return err;
}
EXPORT_SYMBOL_GPL(p54_parse_eeprom);

void p54_fill_eeprom_readback(struct p54_control_hdr *hdr)
{
	struct p54_eeprom_lm86 *eeprom_hdr;

	hdr->magic1 = cpu_to_le16(0x8000);
	hdr->len = cpu_to_le16(sizeof(*eeprom_hdr) + 0x2000);
	hdr->type = cpu_to_le16(P54_CONTROL_TYPE_EEPROM_READBACK);
	hdr->retry1 = hdr->retry2 = 0;
	eeprom_hdr = (struct p54_eeprom_lm86 *) hdr->data;
	eeprom_hdr->offset = 0x0;
	eeprom_hdr->len = cpu_to_le16(0x2000);
}
EXPORT_SYMBOL_GPL(p54_fill_eeprom_readback);

static void p54_rx_data(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_rx_hdr *hdr = (struct p54_rx_hdr *) skb->data;
	struct ieee80211_rx_status rx_status = {0};
	u16 freq = le16_to_cpu(hdr->freq);

	rx_status.ssi = hdr->rssi;
	rx_status.rate = hdr->rate & 0x1f; /* report short preambles & CCK too */
	rx_status.channel = freq == 2484 ? 14 : (freq - 2407)/5;
	rx_status.freq = freq;
	rx_status.phymode = MODE_IEEE80211G;
	rx_status.antenna = hdr->antenna;
	rx_status.mactime = le64_to_cpu(hdr->timestamp);
	rx_status.flag |= RX_FLAG_TSFT;

	skb_pull(skb, sizeof(*hdr));
	skb_trim(skb, le16_to_cpu(hdr->len));

	ieee80211_rx_irqsafe(dev, skb, &rx_status);
}

static void inline p54_wake_free_queues(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int i;

	/* ieee80211_start_queues is great if all queues are really empty.
	 * But, what if some are full? */

	for (i = 0; i < dev->queues; i++)
		if (priv->tx_stats.data[i].len < priv->tx_stats.data[i].limit)
			ieee80211_wake_queue(dev, i);
}

static void p54_rx_frame_sent(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr = (struct p54_control_hdr *) skb->data;
	struct p54_frame_sent_hdr *payload = (struct p54_frame_sent_hdr *) hdr->data;
	struct sk_buff *entry = (struct sk_buff *) priv->tx_queue.next;
	u32 addr = le32_to_cpu(hdr->req_id) - 0x70;
	struct memrecord *range = NULL;
	u32 freed = 0;
	u32 last_addr = priv->rx_start;

	while (entry != (struct sk_buff *)&priv->tx_queue) {
		range = (struct memrecord *)&entry->cb;
		if (range->start_addr == addr) {
			struct ieee80211_tx_status status = {{0}};
			struct p54_control_hdr *entry_hdr;
			struct p54_tx_control_allocdata *entry_data;
			int pad = 0;

			if (entry->next != (struct sk_buff *)&priv->tx_queue)
				freed = ((struct memrecord *)&entry->next->cb)->start_addr - last_addr;
			else
				freed = priv->rx_end - last_addr;

			last_addr = range->end_addr;
			__skb_unlink(entry, &priv->tx_queue);
			if (!range->control) {
				kfree_skb(entry);
				break;
			}
			memcpy(&status.control, range->control,
			       sizeof(status.control));
			kfree(range->control);
			priv->tx_stats.data[status.control.queue].len--;

			entry_hdr = (struct p54_control_hdr *) entry->data;
			entry_data = (struct p54_tx_control_allocdata *) entry_hdr->data;
			if ((entry_hdr->magic1 & cpu_to_le16(0x4000)) != 0)
				pad = entry_data->align[0];

			if (!(status.control.flags & IEEE80211_TXCTL_NO_ACK)) {
				if (!(payload->status & 0x01))
					status.flags |= IEEE80211_TX_STATUS_ACK;
				else
					status.excessive_retries = 1;
			}
			status.retry_count = payload->retries - 1;
			status.ack_signal = le16_to_cpu(payload->ack_rssi);
			skb_pull(entry, sizeof(*hdr) + pad + sizeof(*entry_data));
			ieee80211_tx_status_irqsafe(dev, entry, &status);
			break;
		} else
			last_addr = range->end_addr;
		entry = entry->next;
	}

	if (freed >= IEEE80211_MAX_RTS_THRESHOLD + 0x170 +
	    sizeof(struct p54_control_hdr))
		p54_wake_free_queues(dev);
}

static void p54_rx_control(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_control_hdr *hdr = (struct p54_control_hdr *) skb->data;

	switch (le16_to_cpu(hdr->type)) {
	case P54_CONTROL_TYPE_TXDONE:
		p54_rx_frame_sent(dev, skb);
		break;
	case P54_CONTROL_TYPE_BBP:
		break;
	default:
		printk(KERN_DEBUG "%s: not handling 0x%02x type control frame\n",
		       wiphy_name(dev->wiphy), le16_to_cpu(hdr->type));
		break;
	}
}

/* returns zero if skb can be reused */
int p54_rx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	u8 type = le16_to_cpu(*((__le16 *)skb->data)) >> 8;
	switch (type) {
	case 0x00:
	case 0x01:
		p54_rx_data(dev, skb);
		return -1;
	case 0x4d:
		/* TODO: do something better... but then again, I've never seen this happen */
		printk(KERN_ERR "%s: Received fault. Probably need to restart hardware now..\n",
		       wiphy_name(dev->wiphy));
		break;
	case 0x80:
		p54_rx_control(dev, skb);
		break;
	default:
		printk(KERN_ERR "%s: unknown frame RXed (0x%02x)\n",
		       wiphy_name(dev->wiphy), type);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(p54_rx);

/*
 * So, the firmware is somewhat stupid and doesn't know what places in its
 * memory incoming data should go to. By poking around in the firmware, we
 * can find some unused memory to upload our packets to. However, data that we
 * want the card to TX needs to stay intact until the card has told us that
 * it is done with it. This function finds empty places we can upload to and
 * marks allocated areas as reserved if necessary. p54_rx_frame_sent frees
 * allocated areas.
 */
static void p54_assign_address(struct ieee80211_hw *dev, struct sk_buff *skb,
			       struct p54_control_hdr *data, u32 len,
			       struct ieee80211_tx_control *control)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *entry = priv->tx_queue.next;
	struct sk_buff *target_skb = NULL;
	struct memrecord *range;
	u32 last_addr = priv->rx_start;
	u32 largest_hole = 0;
	u32 target_addr = priv->rx_start;
	unsigned long flags;
	unsigned int left;
	len = (len + 0x170 + 3) & ~0x3; /* 0x70 headroom, 0x100 tailroom */

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	left = skb_queue_len(&priv->tx_queue);
	while (left--) {
		u32 hole_size;
		range = (struct memrecord *)&entry->cb;
		hole_size = range->start_addr - last_addr;
		if (!target_skb && hole_size >= len) {
			target_skb = entry->prev;
			hole_size -= len;
			target_addr = last_addr;
		}
		largest_hole = max(largest_hole, hole_size);
		last_addr = range->end_addr;
		entry = entry->next;
	}
	if (!target_skb && priv->rx_end - last_addr >= len) {
		target_skb = priv->tx_queue.prev;
		largest_hole = max(largest_hole, priv->rx_end - last_addr - len);
		if (!skb_queue_empty(&priv->tx_queue)) {
			range = (struct memrecord *)&target_skb->cb;
			target_addr = range->end_addr;
		}
	} else
		largest_hole = max(largest_hole, priv->rx_end - last_addr);

	if (skb) {
		range = (struct memrecord *)&skb->cb;
		range->start_addr = target_addr;
		range->end_addr = target_addr + len;
		range->control = control;
		__skb_queue_after(&priv->tx_queue, target_skb, skb);
		if (largest_hole < IEEE80211_MAX_RTS_THRESHOLD + 0x170 +
				   sizeof(struct p54_control_hdr))
			ieee80211_stop_queues(dev);
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

	data->req_id = cpu_to_le32(target_addr + 0x70);
}

static int p54_tx(struct ieee80211_hw *dev, struct sk_buff *skb,
		  struct ieee80211_tx_control *control)
{
	struct ieee80211_tx_queue_stats_data *current_queue;
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_allocdata *txhdr;
	struct ieee80211_tx_control *control_copy;
	size_t padding, len;
	u8 rate;

	current_queue = &priv->tx_stats.data[control->queue];
	if (unlikely(current_queue->len > current_queue->limit))
		return NETDEV_TX_BUSY;
	current_queue->len++;
	current_queue->count++;
	if (current_queue->len == current_queue->limit)
		ieee80211_stop_queue(dev, control->queue);

	padding = (unsigned long)(skb->data - (sizeof(*hdr) + sizeof(*txhdr))) & 3;
	len = skb->len;

	control_copy = kmalloc(sizeof(*control), GFP_ATOMIC);
	if (control_copy)
		memcpy(control_copy, control, sizeof(*control));

	txhdr = (struct p54_tx_control_allocdata *)
			skb_push(skb, sizeof(*txhdr) + padding);
	hdr = (struct p54_control_hdr *) skb_push(skb, sizeof(*hdr));

	if (padding)
		hdr->magic1 = cpu_to_le16(0x4010);
	else
		hdr->magic1 = cpu_to_le16(0x0010);
	hdr->len = cpu_to_le16(len);
	hdr->type = (control->flags & IEEE80211_TXCTL_NO_ACK) ? 0 : cpu_to_le16(1);
	hdr->retry1 = hdr->retry2 = control->retry_limit;
	p54_assign_address(dev, skb, hdr, skb->len, control_copy);

	memset(txhdr->wep_key, 0x0, 16);
	txhdr->padding = 0;
	txhdr->padding2 = 0;

	/* TODO: add support for alternate retry TX rates */
	rate = control->tx_rate;
	if (control->flags & IEEE80211_TXCTL_USE_RTS_CTS)
		rate |= 0x40;
	else if (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		rate |= 0x20;
	memset(txhdr->rateset, rate, 8);
	txhdr->wep_key_present = 0;
	txhdr->wep_key_len = 0;
	txhdr->frame_type = cpu_to_le32(control->queue + 4);
	txhdr->magic4 = 0;
	txhdr->antenna = (control->antenna_sel_tx == 0) ?
		2 : control->antenna_sel_tx - 1;
	txhdr->output_power = 0x7f; // HW Maximum
	txhdr->magic5 = (control->flags & IEEE80211_TXCTL_NO_ACK) ?
		0 : ((rate > 0x3) ? cpu_to_le32(0x33) : cpu_to_le32(0x23));
	if (padding)
		txhdr->align[0] = padding;

	priv->tx(dev, hdr, skb->len, 0);
	return 0;
}

static int p54_set_filter(struct ieee80211_hw *dev, u16 filter_type,
			  const u8 *dst, const u8 *src, u8 antenna,
			  u32 magic3, u32 magic8, u32 magic9)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_filter *filter;

	hdr = kzalloc(sizeof(*hdr) + sizeof(*filter) +
		      priv->tx_hdr_len, GFP_ATOMIC);
	if (!hdr)
		return -ENOMEM;

	hdr = (void *)hdr + priv->tx_hdr_len;

	filter = (struct p54_tx_control_filter *) hdr->data;
	hdr->magic1 = cpu_to_le16(0x8001);
	hdr->len = cpu_to_le16(sizeof(*filter));
	p54_assign_address(dev, NULL, hdr, sizeof(*hdr) + sizeof(*filter), NULL);
	hdr->type = cpu_to_le16(P54_CONTROL_TYPE_FILTER_SET);

	filter->filter_type = cpu_to_le16(filter_type);
	memcpy(filter->dst, dst, ETH_ALEN);
	if (!src)
		memset(filter->src, ~0, ETH_ALEN);
	else
		memcpy(filter->src, src, ETH_ALEN);
	filter->antenna = antenna;
	filter->magic3 = cpu_to_le32(magic3);
	filter->rx_addr = cpu_to_le32(priv->rx_end);
	filter->max_rx = cpu_to_le16(0x0620);	/* FIXME: for usb ver 1.. maybe */
	filter->rxhw = priv->rxhw;
	filter->magic8 = cpu_to_le16(magic8);
	filter->magic9 = cpu_to_le16(magic9);

	priv->tx(dev, hdr, sizeof(*hdr) + sizeof(*filter), 1);
	return 0;
}

static int p54_set_freq(struct ieee80211_hw *dev, __le16 freq)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_channel *chan;
	unsigned int i;
	size_t payload_len = sizeof(*chan) + sizeof(u32)*2 +
			     sizeof(*chan->curve_data) *
			     priv->curve_data->points_per_channel;
	void *entry;

	hdr = kzalloc(sizeof(*hdr) + payload_len +
		      priv->tx_hdr_len, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	hdr = (void *)hdr + priv->tx_hdr_len;

	chan = (struct p54_tx_control_channel *) hdr->data;

	hdr->magic1 = cpu_to_le16(0x8001);
	hdr->len = cpu_to_le16(sizeof(*chan));
	hdr->type = cpu_to_le16(P54_CONTROL_TYPE_CHANNEL_CHANGE);
	p54_assign_address(dev, NULL, hdr, sizeof(*hdr) + payload_len, NULL);

	chan->magic1 = cpu_to_le16(0x1);
	chan->magic2 = cpu_to_le16(0x0);

	for (i = 0; i < priv->iq_autocal_len; i++) {
		if (priv->iq_autocal[i].freq != freq)
			continue;

		memcpy(&chan->iq_autocal, &priv->iq_autocal[i],
		       sizeof(*priv->iq_autocal));
		break;
	}
	if (i == priv->iq_autocal_len)
		goto err;

	for (i = 0; i < priv->output_limit_len; i++) {
		if (priv->output_limit[i].freq != freq)
			continue;

		chan->val_barker = 0x38;
		chan->val_bpsk = priv->output_limit[i].val_bpsk;
		chan->val_qpsk = priv->output_limit[i].val_qpsk;
		chan->val_16qam = priv->output_limit[i].val_16qam;
		chan->val_64qam = priv->output_limit[i].val_64qam;
		break;
	}
	if (i == priv->output_limit_len)
		goto err;

	chan->pa_points_per_curve = priv->curve_data->points_per_channel;

	entry = priv->curve_data->data;
	for (i = 0; i < priv->curve_data->channels; i++) {
		if (*((__le16 *)entry) != freq) {
			entry += sizeof(__le16);
			entry += sizeof(struct pda_pa_curve_data_sample_rev1) *
				 chan->pa_points_per_curve;
			continue;
		}

		entry += sizeof(__le16);
		memcpy(chan->curve_data, entry, sizeof(*chan->curve_data) *
		       chan->pa_points_per_curve);
		break;
	}

	memcpy(hdr->data + payload_len - 4, &chan->val_bpsk, 4);

	priv->tx(dev, hdr, sizeof(*hdr) + payload_len, 1);
	return 0;

 err:
	printk(KERN_ERR "%s: frequency change failed\n", wiphy_name(dev->wiphy));
	kfree(hdr);
	return -EINVAL;
}

static int p54_set_leds(struct ieee80211_hw *dev, int mode, int link, int act)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_led *led;

	hdr = kzalloc(sizeof(*hdr) + sizeof(*led) +
		      priv->tx_hdr_len, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	hdr = (void *)hdr + priv->tx_hdr_len;
	hdr->magic1 = cpu_to_le16(0x8001);
	hdr->len = cpu_to_le16(sizeof(*led));
	hdr->type = cpu_to_le16(P54_CONTROL_TYPE_LED);
	p54_assign_address(dev, NULL, hdr, sizeof(*hdr) + sizeof(*led), NULL);

	led = (struct p54_tx_control_led *) hdr->data;
	led->mode = cpu_to_le16(mode);
	led->led_permanent = cpu_to_le16(link);
	led->led_temporary = cpu_to_le16(act);
	led->duration = cpu_to_le16(1000);

	priv->tx(dev, hdr, sizeof(*hdr) + sizeof(*led), 1);

	return 0;
}

#define P54_SET_QUEUE(queue, ai_fs, cw_min, cw_max, burst)	\
do {	 							\
	queue.aifs = cpu_to_le16(ai_fs);			\
	queue.cwmin = cpu_to_le16(cw_min);			\
	queue.cwmax = cpu_to_le16(cw_max);			\
	queue.txop = (burst == 0) ? 				\
		0 : cpu_to_le16((burst * 100) / 32 + 1);	\
} while(0)

static void p54_init_vdcf(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_vdcf *vdcf;

	/* all USB V1 adapters need a extra headroom */
	hdr = (void *)priv->cached_vdcf + priv->tx_hdr_len;
	hdr->magic1 = cpu_to_le16(0x8001);
	hdr->len = cpu_to_le16(sizeof(*vdcf));
	hdr->type = cpu_to_le16(P54_CONTROL_TYPE_DCFINIT);
	hdr->req_id = cpu_to_le32(priv->rx_start);

	vdcf = (struct p54_tx_control_vdcf *) hdr->data;

	P54_SET_QUEUE(vdcf->queue[0], 0x0002, 0x0003, 0x0007, 0x000f);
	P54_SET_QUEUE(vdcf->queue[1], 0x0002, 0x0007, 0x000f, 0x001e);
	P54_SET_QUEUE(vdcf->queue[2], 0x0002, 0x000f, 0x03ff, 0x0014);
	P54_SET_QUEUE(vdcf->queue[3], 0x0007, 0x000f, 0x03ff, 0x0000);
}

static void p54_set_vdcf(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct p54_control_hdr *hdr;
	struct p54_tx_control_vdcf *vdcf;

	hdr = (void *)priv->cached_vdcf + priv->tx_hdr_len;

	p54_assign_address(dev, NULL, hdr, sizeof(*hdr) + sizeof(*vdcf), NULL);

	vdcf = (struct p54_tx_control_vdcf *) hdr->data;

	if (dev->conf.flags & IEEE80211_CONF_SHORT_SLOT_TIME) {
		vdcf->slottime = 9;
		vdcf->magic1 = 0x00;
		vdcf->magic2 = 0x10;
	} else {
		vdcf->slottime = 20;
		vdcf->magic1 = 0x0a;
		vdcf->magic2 = 0x06;
	}

	/* (see prism54/isl_oid.h for further details) */
	vdcf->frameburst = cpu_to_le16(0);

	priv->tx(dev, hdr, sizeof(*hdr) + sizeof(*vdcf), 0);
}

static int p54_start(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int err;

	err = priv->open(dev);
	if (!err)
		priv->mode = IEEE80211_IF_TYPE_MNTR;

	return err;
}

static void p54_stop(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&priv->tx_queue))) {
		struct memrecord *range = (struct memrecord *)&skb->cb;
		if (range->control)
			kfree(range->control);
		kfree_skb(skb);
	}
	priv->stop(dev);
	priv->mode = IEEE80211_IF_TYPE_INVALID;
}

static int p54_add_interface(struct ieee80211_hw *dev,
			     struct ieee80211_if_init_conf *conf)
{
	struct p54_common *priv = dev->priv;

	if (priv->mode != IEEE80211_IF_TYPE_MNTR)
		return -EOPNOTSUPP;

	switch (conf->type) {
	case IEEE80211_IF_TYPE_STA:
		priv->mode = conf->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);

	p54_set_filter(dev, 0, priv->mac_addr, NULL, 0, 1, 0, 0xF642);
	p54_set_filter(dev, 0, priv->mac_addr, NULL, 1, 0, 0, 0xF642);

	switch (conf->type) {
	case IEEE80211_IF_TYPE_STA:
		p54_set_filter(dev, 1, priv->mac_addr, NULL, 0, 0x15F, 0x1F4, 0);
		break;
	default:
		BUG();	/* impossible */
		break;
	}

	p54_set_leds(dev, 1, 0, 0);

	return 0;
}

static void p54_remove_interface(struct ieee80211_hw *dev,
				 struct ieee80211_if_init_conf *conf)
{
	struct p54_common *priv = dev->priv;
	priv->mode = IEEE80211_IF_TYPE_MNTR;
	memset(priv->mac_addr, 0, ETH_ALEN);
	p54_set_filter(dev, 0, priv->mac_addr, NULL, 2, 0, 0, 0);
}

static int p54_config(struct ieee80211_hw *dev, struct ieee80211_conf *conf)
{
	int ret;

	ret = p54_set_freq(dev, cpu_to_le16(conf->freq));
	p54_set_vdcf(dev);
	return ret;
}

static int p54_config_interface(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				struct ieee80211_if_conf *conf)
{
	struct p54_common *priv = dev->priv;

	p54_set_filter(dev, 0, priv->mac_addr, conf->bssid, 0, 1, 0, 0xF642);
	p54_set_filter(dev, 0, priv->mac_addr, conf->bssid, 2, 0, 0, 0);
	p54_set_leds(dev, 1, !is_multicast_ether_addr(conf->bssid), 0);
	memcpy(priv->bssid, conf->bssid, ETH_ALEN);
	return 0;
}

static void p54_configure_filter(struct ieee80211_hw *dev,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 int mc_count, struct dev_mc_list *mclist)
{
	struct p54_common *priv = dev->priv;

	*total_flags &= FIF_BCN_PRBRESP_PROMISC;

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			p54_set_filter(dev, 0, priv->mac_addr,
				       NULL, 2, 0, 0, 0);
		else
			p54_set_filter(dev, 0, priv->mac_addr,
				       priv->bssid, 2, 0, 0, 0);
	}
}

static int p54_conf_tx(struct ieee80211_hw *dev, int queue,
		       const struct ieee80211_tx_queue_params *params)
{
	struct p54_common *priv = dev->priv;
	struct p54_tx_control_vdcf *vdcf;

	vdcf = (struct p54_tx_control_vdcf *)(((struct p54_control_hdr *)
		((void *)priv->cached_vdcf + priv->tx_hdr_len))->data);

	if ((params) && !((queue < 0) || (queue > 4))) {
		P54_SET_QUEUE(vdcf->queue[queue], params->aifs,
			params->cw_min, params->cw_max, params->burst_time);
	} else
		return -EINVAL;

	p54_set_vdcf(dev);

	return 0;
}

static int p54_get_stats(struct ieee80211_hw *dev,
			 struct ieee80211_low_level_stats *stats)
{
	/* TODO */
	return 0;
}

static int p54_get_tx_stats(struct ieee80211_hw *dev,
			    struct ieee80211_tx_queue_stats *stats)
{
	struct p54_common *priv = dev->priv;
	unsigned int i;

	for (i = 0; i < dev->queues; i++)
		memcpy(&stats->data[i], &priv->tx_stats.data[i],
			sizeof(stats->data[i]));

	return 0;
}

static const struct ieee80211_ops p54_ops = {
	.tx			= p54_tx,
	.start			= p54_start,
	.stop			= p54_stop,
	.add_interface		= p54_add_interface,
	.remove_interface	= p54_remove_interface,
	.config			= p54_config,
	.config_interface	= p54_config_interface,
	.configure_filter	= p54_configure_filter,
	.conf_tx		= p54_conf_tx,
	.get_stats		= p54_get_stats,
	.get_tx_stats		= p54_get_tx_stats
};

struct ieee80211_hw *p54_init_common(size_t priv_data_len)
{
	struct ieee80211_hw *dev;
	struct p54_common *priv;
	int i;

	dev = ieee80211_alloc_hw(priv_data_len, &p54_ops);
	if (!dev)
		return NULL;

	priv = dev->priv;
	priv->mode = IEEE80211_IF_TYPE_INVALID;
	skb_queue_head_init(&priv->tx_queue);
	memcpy(priv->channels, p54_channels, sizeof(p54_channels));
	memcpy(priv->rates, p54_rates, sizeof(p54_rates));
	priv->modes[1].mode = MODE_IEEE80211B;
	priv->modes[1].num_rates = 4;
	priv->modes[1].rates = priv->rates;
	priv->modes[1].num_channels = ARRAY_SIZE(p54_channels);
	priv->modes[1].channels = priv->channels;
	priv->modes[0].mode = MODE_IEEE80211G;
	priv->modes[0].num_rates = ARRAY_SIZE(p54_rates);
	priv->modes[0].rates = priv->rates;
	priv->modes[0].num_channels = ARRAY_SIZE(p54_channels);
	priv->modes[0].channels = priv->channels;
	dev->flags = IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING | /* not sure */
		    IEEE80211_HW_RX_INCLUDES_FCS;
	dev->channel_change_time = 1000;	/* TODO: find actual value */
	dev->max_rssi = 127;

	priv->tx_stats.data[0].limit = 5;
	dev->queues = 1;

	dev->extra_tx_headroom = sizeof(struct p54_control_hdr) + 4 +
				 sizeof(struct p54_tx_control_allocdata);

        priv->cached_vdcf = kzalloc(sizeof(struct p54_tx_control_vdcf) +
              priv->tx_hdr_len + sizeof(struct p54_control_hdr), GFP_KERNEL);

	if (!priv->cached_vdcf) {
		ieee80211_free_hw(dev);
		return NULL;
	}

	p54_init_vdcf(dev);

	for (i = 0; i < 2; i++) {
		if (ieee80211_register_hwmode(dev, &priv->modes[i])) {
			kfree(priv->cached_vdcf);
			ieee80211_free_hw(dev);
			return NULL;
		}
	}

	return dev;
}
EXPORT_SYMBOL_GPL(p54_init_common);

void p54_free_common(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	kfree(priv->iq_autocal);
	kfree(priv->output_limit);
	kfree(priv->curve_data);
	kfree(priv->cached_vdcf);
}
EXPORT_SYMBOL_GPL(p54_free_common);

static int __init p54_init(void)
{
	return 0;
}

static void __exit p54_exit(void)
{
}

module_init(p54_init);
module_exit(p54_exit);
