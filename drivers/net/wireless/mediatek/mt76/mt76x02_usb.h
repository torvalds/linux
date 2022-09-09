/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#ifndef __MT76x02_USB_H
#define __MT76x02_USB_H

#include "mt76x02.h"

int mt76x02u_mac_start(struct mt76x02_dev *dev);
void mt76x02u_init_mcu(struct mt76_dev *dev);
void mt76x02u_mcu_fw_reset(struct mt76x02_dev *dev);
int mt76x02u_mcu_fw_send_data(struct mt76x02_dev *dev, const void *data,
			      int data_len, u32 max_payload, u32 offset);

int mt76x02u_skb_dma_info(struct sk_buff *skb, int port, u32 flags);
int mt76x02u_tx_prepare_skb(struct mt76_dev *mdev, void *data,
			    enum mt76_txq_id qid, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta,
			    struct mt76_tx_info *tx_info);
void mt76x02u_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e);
void mt76x02u_init_beacon_config(struct mt76x02_dev *dev);
void mt76x02u_exit_beacon_config(struct mt76x02_dev *dev);
#endif /* __MT76x02_USB_H */
