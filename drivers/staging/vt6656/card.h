/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

typedef enum _CARD_PHY_TYPE {
    PHY_TYPE_AUTO = 0,
    PHY_TYPE_11B,
    PHY_TYPE_11G,
    PHY_TYPE_11A
} CARD_PHY_TYPE, *PCARD_PHY_TYPE;

#define CB_MAX_CHANNEL_24G  14
#define CB_MAX_CHANNEL_5G       42 /* add channel9(5045MHz), 41==>42 */
#define CB_MAX_CHANNEL      (CB_MAX_CHANNEL_24G+CB_MAX_CHANNEL_5G)

struct vnt_private;

void vnt_set_channel(struct vnt_private *, u32);
void vnt_set_rspinf(struct vnt_private *, u8);
void vnt_update_ifs(struct vnt_private *);
void vnt_update_top_rates(struct vnt_private *);
int vnt_ofdm_min_rate(struct vnt_private *);
void vnt_adjust_tsf(struct vnt_private *, u8, u64, u64);
bool vnt_get_current_tsf(struct vnt_private *, u64 *);
bool vnt_clear_current_tsf(struct vnt_private *);
void vnt_reset_next_tbtt(struct vnt_private *, u16);
void vnt_update_next_tbtt(struct vnt_private *, u64, u16);
u64 vnt_get_next_tbtt(u64, u16);
u64 vnt_get_tsf_offset(u8 byRxRate, u64 qwTSF1, u64 qwTSF2);
int vnt_radio_power_off(struct vnt_private *);
int vnt_radio_power_on(struct vnt_private *);
u8 vnt_get_pkt_type(struct vnt_private *);
void vnt_set_bss_mode(struct vnt_private *);

#endif /* __CARD_H__ */
