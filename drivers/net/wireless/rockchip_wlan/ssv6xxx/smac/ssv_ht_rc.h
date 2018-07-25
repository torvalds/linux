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

#ifndef _SSV_RC_HT_H_
#define _SSV_RC_HT_H_ 
#include "ssv_rc_common.h"
#define MINSTREL_SCALE 16
#define MINSTREL_FRAC(val,div) (((val) << MINSTREL_SCALE) / div)
#define MINSTREL_TRUNC(val) ((val) >> MINSTREL_SCALE)
#define SSV_RC_HT_INTERVAL 100
extern const u16 ampdu_max_transmit_length[];
s32 ssv62xx_ht_rate_update(struct sk_buff *skb, struct ssv_softc *sc, struct fw_rc_retry_params *ar);
void ssv62xx_ht_rc_caps(const u16 ssv6xxx_rc_rate_set[RC_TYPE_MAX][13],struct ssv_sta_rc_info *rc_sta);
void ssv6xxx_ht_report_handler(struct ssv_softc *sc,struct sk_buff *skb,struct ssv_sta_rc_info *rc_sta);
#endif
