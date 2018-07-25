/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SSV_RC_H_
#define _SSV_RC_H_ 
#include "ssv_rc_common.h"
#define RC_PID_REPORT_INTERVAL 40
#define RC_PID_INTERVAL 125
#define RC_PID_DO_ARITH_RIGHT_SHIFT(x,y) \
 ((x) < 0 ? -((-(x)) >> (y)) : (x) >> (y))
#define RC_PID_NORM_OFFSET 3
#define RC_PID_SMOOTHING_SHIFT 1
#define RC_PID_SMOOTHING (1 << RC_PID_SMOOTHING_SHIFT)
#define RC_PID_COEFF_P 15
#define RC_PID_COEFF_I 15
#define RC_PID_COEFF_D 5
#define MAXPROBES 3
#define SSV_DRATE_IDX (2)
#define SSV_CRATE_IDX (3)
struct ssv_softc;
struct ssv_rc_rate *ssv6xxx_rc_get_rate(int rc_index);
void ssv6xxx_rc_hw_rate_idx(struct ssv_softc *sc,
            struct ieee80211_tx_info *info, struct ssv_rate_info *sr);
#ifdef RATE_CONTROL_REALTIME_UPDATA
u8 ssv6xxx_rc_hw_rate_update_check(struct sk_buff *skb, struct ssv_softc *sc, u32 do_rts_cts);
#endif
void ssv6xxx_rc_mac8011_rate_idx(struct ssv_softc *sc, int hw_rate_idx,
                struct ieee80211_rx_status *rxs);
void ssv6xxx_rc_hw_reset(struct ssv_softc *sc, int rc_idx, int hwidx);
void ssv6xxx_rc_update_basic_rate(struct ssv_softc *sc, u32 basic_rates);
int ssv6xxx_rate_control_register(void);
void ssv6xxx_rate_control_unregister(void);
void ssv6xxx_rc_rx_data_handler(struct ieee80211_hw *hw, struct sk_buff *skb, u32 rate_index);
int pide_frame_duration(size_t len, int rate, int short_preamble, int flags);
#endif
