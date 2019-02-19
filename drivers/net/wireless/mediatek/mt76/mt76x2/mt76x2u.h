/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
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

#ifndef __MT76x2U_H
#define __MT76x2U_H

#include <linux/device.h>

#include "mt76x2.h"
#include "mcu.h"

#define MT7612U_EEPROM_SIZE		512

#define MT_USB_AGGR_SIZE_LIMIT		21 /* 1024B unit */
#define MT_USB_AGGR_TIMEOUT		0x80 /* 33ns unit */

extern const struct ieee80211_ops mt76x2u_ops;

int mt76x2u_register_device(struct mt76x02_dev *dev);
int mt76x2u_init_hardware(struct mt76x02_dev *dev);
void mt76x2u_cleanup(struct mt76x02_dev *dev);
void mt76x2u_stop_hw(struct mt76x02_dev *dev);

int mt76x2u_mac_reset(struct mt76x02_dev *dev);
void mt76x2u_mac_resume(struct mt76x02_dev *dev);
int mt76x2u_mac_start(struct mt76x02_dev *dev);
int mt76x2u_mac_stop(struct mt76x02_dev *dev);

int mt76x2u_phy_set_channel(struct mt76x02_dev *dev,
			    struct cfg80211_chan_def *chandef);
void mt76x2u_phy_calibrate(struct work_struct *work);

void mt76x2u_mcu_complete_urb(struct urb *urb);
int mt76x2u_mcu_init(struct mt76x02_dev *dev);
int mt76x2u_mcu_fw_init(struct mt76x02_dev *dev);

int mt76x2u_alloc_queues(struct mt76x02_dev *dev);
void mt76x2u_queues_deinit(struct mt76x02_dev *dev);
void mt76x2u_stop_queues(struct mt76x02_dev *dev);
int mt76x2u_skb_dma_info(struct sk_buff *skb, enum dma_msg_port port,
			 u32 flags);

#endif /* __MT76x2U_H */
