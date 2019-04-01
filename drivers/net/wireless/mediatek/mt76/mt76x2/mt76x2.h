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

#ifndef __MT76x2_H
#define __MT76x2_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/bitops.h>

#define MT7662_FIRMWARE		"mt7662.bin"
#define MT7662_ROM_PATCH	"mt7662_rom_patch.bin"
#define MT7662_EEPROM_SIZE	512

#include "../mt76x02.h"
#include "mac.h"

static inline bool is_mt7612(struct mt76x02_dev *dev)
{
	return mt76_chip(&dev->mt76) == 0x7612;
}

static inline bool mt76x2_channel_silent(struct mt76x02_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;

	return ((chan->flags & IEEE80211_CHAN_RADAR) &&
		chan->dfs_state != NL80211_DFS_AVAILABLE);
}

extern const struct ieee80211_ops mt76x2_ops;

int mt76x2_register_device(struct mt76x02_dev *dev);

void mt76x2_phy_power_on(struct mt76x02_dev *dev);
void mt76x2_stop_hardware(struct mt76x02_dev *dev);
int mt76x2_eeprom_init(struct mt76x02_dev *dev);
int mt76x2_apply_calibration_data(struct mt76x02_dev *dev, int channel);

void mt76x2_phy_set_antenna(struct mt76x02_dev *dev);
int mt76x2_phy_start(struct mt76x02_dev *dev);
int mt76x2_phy_set_channel(struct mt76x02_dev *dev,
			   struct cfg80211_chan_def *chandef);
void mt76x2_phy_calibrate(struct work_struct *work);
void mt76x2_phy_set_txpower(struct mt76x02_dev *dev);

int mt76x2_mcu_init(struct mt76x02_dev *dev);
int mt76x2_mcu_set_channel(struct mt76x02_dev *dev, u8 channel, u8 bw,
			   u8 bw_index, bool scan);
int mt76x2_mcu_load_cr(struct mt76x02_dev *dev, u8 type, u8 temp_level,
		       u8 channel);

void mt76x2_cleanup(struct mt76x02_dev *dev);

int mt76x2_mac_reset(struct mt76x02_dev *dev, bool hard);
void mt76x2_reset_wlan(struct mt76x02_dev *dev, bool enable);
void mt76x2_init_txpower(struct mt76x02_dev *dev,
			 struct ieee80211_supported_band *sband);
void mt76_write_mac_initvals(struct mt76x02_dev *dev);

void mt76x2_phy_tssi_compensate(struct mt76x02_dev *dev);
void mt76x2_phy_set_txpower_regs(struct mt76x02_dev *dev,
				 enum nl80211_band band);
void mt76x2_configure_tx_delay(struct mt76x02_dev *dev,
			       enum nl80211_band band, u8 bw);
void mt76x2_apply_gain_adj(struct mt76x02_dev *dev);
void mt76x2_phy_update_channel_gain(struct mt76x02_dev *dev);

#endif
