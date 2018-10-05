/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/delay.h>
#include "mt76x2.h"
#include "mcu.h"
#include "eeprom.h"
#include "trace.h"

void mt76x2_mac_set_bssid(struct mt76x02_dev *dev, u8 idx, const u8 *addr)
{
	idx &= 7;
	mt76_wr(dev, MT_MAC_APC_BSSID_L(idx), get_unaligned_le32(addr));
	mt76_rmw_field(dev, MT_MAC_APC_BSSID_H(idx), MT_MAC_APC_BSSID_H_ADDR,
		       get_unaligned_le16(addr + 4));
}

void mt76x2_mac_process_tx_status_fifo(struct mt76x02_dev *dev)
{
	struct mt76x02_tx_status stat;
	u8 update = 1;

	while (kfifo_get(&dev->txstatus_fifo, &stat))
		mt76x02_send_tx_status(&dev->mt76, &stat, &update);
}

static int
mt76_write_beacon(struct mt76x02_dev *dev, int offset, struct sk_buff *skb)
{
	int beacon_len = mt76x02_beacon_offsets[1] - mt76x02_beacon_offsets[0];
	struct mt76x02_txwi txwi;

	if (WARN_ON_ONCE(beacon_len < skb->len + sizeof(struct mt76x02_txwi)))
		return -ENOSPC;

	mt76x02_mac_write_txwi(&dev->mt76, &txwi, skb, NULL, NULL, skb->len);

	mt76_wr_copy(dev, offset, &txwi, sizeof(txwi));
	offset += sizeof(txwi);

	mt76_wr_copy(dev, offset, skb->data, skb->len);
	return 0;
}

static int
__mt76x2_mac_set_beacon(struct mt76x02_dev *dev, u8 bcn_idx, struct sk_buff *skb)
{
	int beacon_len = mt76x02_beacon_offsets[1] - mt76x02_beacon_offsets[0];
	int beacon_addr = mt76x02_beacon_offsets[bcn_idx];
	int ret = 0;
	int i;

	/* Prevent corrupt transmissions during update */
	mt76_set(dev, MT_BCN_BYPASS_MASK, BIT(bcn_idx));

	if (skb) {
		ret = mt76_write_beacon(dev, beacon_addr, skb);
		if (!ret)
			dev->beacon_data_mask |= BIT(bcn_idx);
	} else {
		dev->beacon_data_mask &= ~BIT(bcn_idx);
		for (i = 0; i < beacon_len; i += 4)
			mt76_wr(dev, beacon_addr + i, 0);
	}

	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xff00 | ~dev->beacon_data_mask);

	return ret;
}

int mt76x2_mac_set_beacon(struct mt76x02_dev *dev, u8 vif_idx,
			  struct sk_buff *skb)
{
	bool force_update = false;
	int bcn_idx = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->beacons); i++) {
		if (vif_idx == i) {
			force_update = !!dev->beacons[i] ^ !!skb;

			if (dev->beacons[i])
				dev_kfree_skb(dev->beacons[i]);

			dev->beacons[i] = skb;
			__mt76x2_mac_set_beacon(dev, bcn_idx, skb);
		} else if (force_update && dev->beacons[i]) {
			__mt76x2_mac_set_beacon(dev, bcn_idx, dev->beacons[i]);
		}

		bcn_idx += !!dev->beacons[i];
	}

	for (i = bcn_idx; i < ARRAY_SIZE(dev->beacons); i++) {
		if (!(dev->beacon_data_mask & BIT(i)))
			break;

		__mt76x2_mac_set_beacon(dev, i, NULL);
	}

	mt76_rmw_field(dev, MT_MAC_BSSID_DW1, MT_MAC_BSSID_DW1_MBEACON_N,
		       bcn_idx - 1);
	return 0;
}

void mt76x2_mac_set_beacon_enable(struct mt76x02_dev *dev,
				  u8 vif_idx, bool val)
{
	u8 old_mask = dev->beacon_mask;
	bool en;
	u32 reg;

	if (val) {
		dev->beacon_mask |= BIT(vif_idx);
	} else {
		dev->beacon_mask &= ~BIT(vif_idx);
		mt76x2_mac_set_beacon(dev, vif_idx, NULL);
	}

	if (!!old_mask == !!dev->beacon_mask)
		return;

	en = dev->beacon_mask;

	mt76_rmw_field(dev, MT_INT_TIMER_EN, MT_INT_TIMER_EN_PRE_TBTT_EN, en);
	reg = MT_BEACON_TIME_CFG_BEACON_TX |
	      MT_BEACON_TIME_CFG_TBTT_EN |
	      MT_BEACON_TIME_CFG_TIMER_EN;
	mt76_rmw(dev, MT_BEACON_TIME_CFG, reg, reg * en);

	if (en)
		mt76x02_irq_enable(&dev->mt76, MT_INT_PRE_TBTT | MT_INT_TBTT);
	else
		mt76x02_irq_disable(&dev->mt76, MT_INT_PRE_TBTT | MT_INT_TBTT);
}

void mt76x2_update_channel(struct mt76_dev *mdev)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76_channel_state *state;
	u32 active, busy;

	state = mt76_channel_state(&dev->mt76, dev->mt76.chandef.chan);

	busy = mt76_rr(dev, MT_CH_BUSY);
	active = busy + mt76_rr(dev, MT_CH_IDLE);

	spin_lock_bh(&dev->mt76.cc_lock);
	state->cc_busy += busy;
	state->cc_active += active;
	spin_unlock_bh(&dev->mt76.cc_lock);
}

void mt76x2_mac_work(struct work_struct *work)
{
	struct mt76x02_dev *dev = container_of(work, struct mt76x02_dev,
					       mac_work.work);
	int i, idx;

	mt76x2_update_channel(&dev->mt76);
	for (i = 0, idx = 0; i < 16; i++) {
		u32 val = mt76_rr(dev, MT_TX_AGG_CNT(i));

		dev->aggr_stats[idx++] += val & 0xffff;
		dev->aggr_stats[idx++] += val >> 16;
	}

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     MT_CALIBRATE_INTERVAL);
}

void mt76x2_mac_set_tx_protection(struct mt76x02_dev *dev, u32 val)
{
	u32 data = 0;

	if (val != ~0)
		data = FIELD_PREP(MT_PROT_CFG_CTRL, 1) |
		       MT_PROT_CFG_RTS_THRESH;

	mt76_rmw_field(dev, MT_TX_RTS_CFG, MT_TX_RTS_CFG_THRESH, val);

	mt76_rmw(dev, MT_CCK_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_OFDM_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_MM20_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_MM40_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_GF20_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_GF40_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_TX_PROT_CFG6,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_TX_PROT_CFG7,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_TX_PROT_CFG8,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
}
