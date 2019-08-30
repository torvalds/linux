/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: card.h
 *
 * Purpose: Provide functions to setup NIC operation mode
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __CARD_H__
#define __CARD_H__
#include "device.h"

/* init card type */

#define CB_MAX_CHANNEL_24G	14
#define CB_MAX_CHANNEL_5G	42 /* add channel9(5045MHz), 41==>42 */
#define CB_MAX_CHANNEL		(CB_MAX_CHANNEL_24G + CB_MAX_CHANNEL_5G)

struct vnt_private;

void vnt_set_channel(struct vnt_private *priv, u32 connection_channel);
void vnt_set_rspinf(struct vnt_private *priv, u8 bb_type);
void vnt_update_ifs(struct vnt_private *priv);
void vnt_update_top_rates(struct vnt_private *priv);
int vnt_ofdm_min_rate(struct vnt_private *priv);
void vnt_adjust_tsf(struct vnt_private *priv, u8 rx_rate,
		    u64 time_stamp, u64 local_tsf);
bool vnt_get_current_tsf(struct vnt_private *priv, u64 *current_tsf);
bool vnt_clear_current_tsf(struct vnt_private *priv);
void vnt_reset_next_tbtt(struct vnt_private *priv, u16 beacon_interval);
void vnt_update_next_tbtt(struct vnt_private *priv, u64 tsf,
			  u16 beacon_interval);
u64 vnt_get_next_tbtt(u64 tsf, u16 beacon_interval);
u64 vnt_get_tsf_offset(u8 rx_rate, u64 tsf1, u64 tsf2);
int vnt_radio_power_off(struct vnt_private *priv);
int vnt_radio_power_on(struct vnt_private *priv);
u8 vnt_get_pkt_type(struct vnt_private *priv);
void vnt_set_bss_mode(struct vnt_private *priv);

#endif /* __CARD_H__ */
