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

#ifndef _SSV_RC_COM_H_
#define _SSV_RC_COM_H_ 
#define SSV_RC_MAX_STA 8
#define MCS_GROUP_RATES 8
#define SSV_HT_RATE_MAX 8
#define TDIFS 34
#define TSLOT 9
#define SSV_RC_MAX_HARDWARE_SUPPORT 2
#define RC_FIRMWARE_REPORT_FLAG 0x80
#define RC_FLAG_INVALID 0x00000001
#define RC_FLAG_LEGACY 0x00000002
#define RC_FLAG_HT 0x00000004
#define RC_FLAG_HT_SGI 0x00000008
#define RC_FLAG_HT_GF 0x00000010
#define RC_FLAG_SHORT_PREAMBLE 0x00000020
enum ssv6xxx_rc_phy_type {
    WLAN_RC_PHY_CCK,
    WLAN_RC_PHY_OFDM,
    WLAN_RC_PHY_HT_20_SS_LGI,
    WLAN_RC_PHY_HT_20_SS_SGI,
    WLAN_RC_PHY_HT_20_SS_GF,
};
#define RATE_TABLE_SIZE 39
#define RC_STA_VALID 0x00000001
#define RC_STA_CAP_HT 0x00000002
#define RC_STA_CAP_GF 0x00000004
#define RC_STA_CAP_SGI_20 0x00000008
#define RC_STA_CAP_SHORT_PREAMBLE 0x00000010
#define SSV62XX_G_RATE_INDEX 7
#define SSV62XX_RATE_MCS_INDEX 15
#define SSV62XX_RATE_MCS_LGI_INDEX 15
#define SSV62XX_RATE_MCS_SGI_INDEX 23
#define SSV62XX_RATE_MCS_GREENFIELD_INDEX 31
enum ssv_rc_rate_type {
    RC_TYPE_B_ONLY=0,
    RC_TYPE_LEGACY_GB,
    RC_TYPE_SGI_20,
    RC_TYPE_LGI_20,
    RC_TYPE_HT_SGI_20,
    RC_TYPE_HT_LGI_20,
    RC_TYPE_HT_GF,
    RC_TYPE_MAX,
};
struct ssv_rate_info {
    int crate_kbps;
    int crate_hw_idx;
    int drate_kbps;
    int drate_hw_idx;
    u32 d_flags;
    u32 c_flags;
};
struct ssv_rc_rate {
    u32 rc_flags;
    u16 phy_type;
    u32 rate_kbps;
    u8 dot11_rate_idx;
    u8 ctrl_rate_idx;
    u8 hw_rate_idx;
    u8 arith_shift;
    u8 target_pf;
};
struct rc_pid_sta_info {
    unsigned long last_sample;
    unsigned long last_report;
    u16 tx_num_failed;
    u16 tx_num_xmit;
    u8 probe_report_flag;
    u8 probe_wating_times;
    u8 real_hw_index;
    int txrate_idx;
    u8 last_pf;
    s32 err_avg_sc;
    int last_dlr;
    u8 feedback_probes;
    u8 monitoring;
    u8 oldrate;
    u8 tmp_rate_idx;
    u8 probe_cnt;
};
struct rc_pid_rateinfo {
    u16 rc_index;
    u16 index;
 s32 diff;
 u16 perfect_tx_time;
 u32 throughput;
 unsigned long this_attempt;
 unsigned long this_success;
 unsigned long this_fail;
 u64 attempt;
 u64 success;
 u64 fail;
};
struct rc_pid_info {
 unsigned int target;
#if 0
 u8 coeff_p;
 u8 coeff_i;
 u8 coeff_d;
 u8 smoothing_shift;
 u8 sharpen_factor;
 u8 sharpen_duration;
 u8 norm_offset;
#endif
 int oldrate;
 struct rc_pid_rateinfo rinfo[12];
};
struct mcs_group {
    unsigned int duration[MCS_GROUP_RATES];
};
struct minstrel_rate_stats {
    u16 rc_index;
    unsigned int attempts, last_attempts;
    unsigned int success, last_success;
    u64 att_hist, succ_hist;
    unsigned int cur_tp;
    unsigned int cur_prob, probability;
    unsigned int retry_count;
    unsigned int retry_count_rtscts;
    u8 sample_skipped;
};
struct minstrel_mcs_group_data {
    u8 index;
    u8 column;
    unsigned int max_tp_rate;
    unsigned int max_tp_rate2;
    unsigned int max_prob_rate;
    struct minstrel_rate_stats rates[MCS_GROUP_RATES];
};
struct ssv62xx_ht {
    unsigned int ampdu_len;
    unsigned int ampdu_packets;
    unsigned int avg_ampdu_len;
    unsigned int max_tp_rate;
    unsigned int max_tp_rate2;
    unsigned int max_prob_rate;
    int first_try_count;
    int second_try_count;
    int other_try_count;
    unsigned long stats_update;
    unsigned int overhead;
    unsigned int overhead_rtscts;
    unsigned int total_packets;
    unsigned int sample_packets;
    u8 sample_wait;
    u8 sample_tries;
    u8 sample_count;
    u8 sample_slow;
    struct minstrel_mcs_group_data groups;
};
struct ssv_sta_rc_info {
    u8 rc_valid;
    u8 rc_type;
    u8 rc_num_rate;
    s8 rc_wsid;
    u8 ht_rc_type;
    u8 is_ht;
    u32 rc_supp_rates;
    u32 ht_supp_rates;
    struct rc_pid_info pinfo;
    struct rc_pid_sta_info spinfo;
    struct ssv62xx_ht ht;
};
struct ssv_rate_ctrl {
    struct ssv_rc_rate *rc_table;
    struct ssv_sta_rc_info sta_rc_info[SSV_RC_MAX_STA];
};
#define HT_RC_UPDATE_INTERVAL 1000
#endif
