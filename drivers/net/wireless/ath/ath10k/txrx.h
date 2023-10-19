/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2014,2016 Qualcomm Atheros, Inc.
 */
#ifndef _TXRX_H_
#define _TXRX_H_

#include "htt.h"

int ath10k_txrx_tx_unref(struct ath10k_htt *htt,
			 const struct htt_tx_done *tx_done);

struct ath10k_peer *ath10k_peer_find(struct ath10k *ar, int vdev_id,
				     const u8 *addr);
struct ath10k_peer *ath10k_peer_find_by_id(struct ath10k *ar, int peer_id);
int ath10k_wait_for_peer_created(struct ath10k *ar, int vdev_id,
				 const u8 *addr);
int ath10k_wait_for_peer_deleted(struct ath10k *ar, int vdev_id,
				 const u8 *addr);

void ath10k_peer_map_event(struct ath10k_htt *htt,
			   struct htt_peer_map_event *ev);
void ath10k_peer_unmap_event(struct ath10k_htt *htt,
			     struct htt_peer_unmap_event *ev);

#endif
