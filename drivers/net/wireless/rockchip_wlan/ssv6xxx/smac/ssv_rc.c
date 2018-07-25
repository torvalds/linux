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

#include <linux/version.h>
#include <ssv6200.h>
#include "dev.h"
#include "ssv_ht_rc.h"
#include "ssv_rc.h"
#include "ssv_rc_common.h"
static struct ssv_rc_rate ssv_11bgn_rate_table[] =
{
        [0] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 1000,
                .dot11_rate_idx = 0,
                .ctrl_rate_idx = 0,
                .hw_rate_idx = 0,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [1] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 2000,
                .dot11_rate_idx = 1,
                .ctrl_rate_idx = 1,
                .hw_rate_idx = 1,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [2] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 5500,
                .dot11_rate_idx = 2,
                .ctrl_rate_idx = 1,
                .hw_rate_idx = 2,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [3] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 11000,
                .dot11_rate_idx = 3,
                .ctrl_rate_idx = 1,
                .hw_rate_idx = 3,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [4] = { .rc_flags = RC_FLAG_LEGACY | RC_FLAG_SHORT_PREAMBLE,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 2000,
                .dot11_rate_idx = 1,
                .ctrl_rate_idx = 4,
                .hw_rate_idx = 4,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [5] = { .rc_flags = RC_FLAG_LEGACY | RC_FLAG_SHORT_PREAMBLE,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 5500,
                .dot11_rate_idx = 2,
                .ctrl_rate_idx = 4,
                .hw_rate_idx = 5,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [6] = { .rc_flags = RC_FLAG_LEGACY | RC_FLAG_SHORT_PREAMBLE,
                .phy_type = WLAN_RC_PHY_CCK,
                .rate_kbps = 11000,
                .dot11_rate_idx = 3,
                .ctrl_rate_idx = 4,
                .hw_rate_idx = 6,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [7] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_OFDM,
                .rate_kbps = 6000,
                .dot11_rate_idx = 4,
                .ctrl_rate_idx = 7,
                .hw_rate_idx = 7,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [8] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_OFDM,
                .rate_kbps = 9000,
                .dot11_rate_idx = 5,
                .ctrl_rate_idx = 7,
                .hw_rate_idx = 8,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [9] = { .rc_flags = RC_FLAG_LEGACY,
                .phy_type = WLAN_RC_PHY_OFDM,
                .rate_kbps = 12000,
                .dot11_rate_idx = 6,
                .ctrl_rate_idx = 9,
                .hw_rate_idx = 9,
                .arith_shift = 8,
                .target_pf = 26,
           },
        [10] = { .rc_flags = RC_FLAG_LEGACY,
                 .phy_type = WLAN_RC_PHY_OFDM,
                 .rate_kbps = 18000,
                 .dot11_rate_idx = 7,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 10,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [11] = { .rc_flags = RC_FLAG_LEGACY,
                 .phy_type = WLAN_RC_PHY_OFDM,
                 .rate_kbps = 24000,
                 .dot11_rate_idx = 8,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 11,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [12] = { .rc_flags = RC_FLAG_LEGACY,
                 .phy_type = WLAN_RC_PHY_OFDM,
                 .rate_kbps = 36000,
                 .dot11_rate_idx = 9,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 12,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [13] = { .rc_flags = RC_FLAG_LEGACY,
                 .phy_type = WLAN_RC_PHY_OFDM,
                 .rate_kbps = 48000,
                 .dot11_rate_idx = 10,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 13,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [14] = { .rc_flags = RC_FLAG_LEGACY,
                 .phy_type = WLAN_RC_PHY_OFDM,
                 .rate_kbps = 54000,
                 .dot11_rate_idx = 11,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 14,
                 .arith_shift = 8,
                 .target_pf = 8
               },
        [15] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 6500,
                 .dot11_rate_idx = 0,
                 .ctrl_rate_idx = 7,
                 .hw_rate_idx = 15,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [16] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 13000,
                 .dot11_rate_idx = 1,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 16,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [17] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 19500,
                 .dot11_rate_idx = 2,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 17,
                 .arith_shift = 8,
                 .target_pf = 26,
                 },
        [18] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 26000,
                 .dot11_rate_idx = 3,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 18,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [19] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 39000,
                 .dot11_rate_idx = 4,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 19,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [20] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 52000,
                 .dot11_rate_idx = 5,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 20,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [21] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 58500,
                 .dot11_rate_idx = 6,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 21,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [22] = { .rc_flags = RC_FLAG_HT,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_LGI,
                 .rate_kbps = 65000,
                 .dot11_rate_idx = 7,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 22,
                 .arith_shift = 8,
                 .target_pf = 8
               },
        [23] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 7200,
                 .dot11_rate_idx = 0,
                 .ctrl_rate_idx = 7,
                 .hw_rate_idx = 23,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [24] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 14400,
                 .dot11_rate_idx = 1,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 24,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [25] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 21700,
                 .dot11_rate_idx = 2,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 25,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [26] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 28900,
                 .dot11_rate_idx = 3,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 26,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [27] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 43300,
                 .dot11_rate_idx = 4,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 27,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [28] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 57800,
                 .dot11_rate_idx = 5,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 28,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [29] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 65000,
                 .dot11_rate_idx = 6,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 29,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [30] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_SGI,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_SGI,
                 .rate_kbps = 72200,
                 .dot11_rate_idx = 7,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 30,
                 .arith_shift = 8,
                 .target_pf = 8
               },
        [31] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 6500,
                 .dot11_rate_idx = 0,
                 .ctrl_rate_idx = 7,
                 .hw_rate_idx = 31,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [32] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 13000,
                 .dot11_rate_idx = 1,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 32,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [33] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 19500,
                 .dot11_rate_idx = 2,
                 .ctrl_rate_idx = 9,
                 .hw_rate_idx = 33,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [34] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 26000,
                 .dot11_rate_idx = 3,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 34,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [35] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 39000,
                 .dot11_rate_idx = 4,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 35,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [36] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 52000,
                 .dot11_rate_idx = 5,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 36,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [37] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 58500,
                 .dot11_rate_idx = 6,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 37,
                 .arith_shift = 8,
                 .target_pf = 26,
               },
        [38] = { .rc_flags = RC_FLAG_HT | RC_FLAG_HT_GF,
                 .phy_type = WLAN_RC_PHY_HT_20_SS_GF,
                 .rate_kbps = 65000,
                 .dot11_rate_idx = 7,
                 .ctrl_rate_idx = 11,
                 .hw_rate_idx = 38,
                 .arith_shift = 8,
                 .target_pf = 8
                },
};
const u16 ssv6xxx_rc_rate_set[RC_TYPE_MAX][13] =
{
    [RC_TYPE_B_ONLY] = { 4, 0, 1, 2, 3},
    [RC_TYPE_LEGACY_GB] = { 12, 0, 1, 2, 7, 8, 3, 9, 10, 11, 12, 13, 14 },
#if 0
    [RC_TYPE_SGI_20] = { 12, 0, 1, 2, 3, 23, 24, 25, 26, 27, 28, 29, 30 },
    [RC_TYPE_LGI_20] = { 12, 0, 1, 2, 3, 15, 16, 17, 18, 19, 20, 21, 22 },
#else
    [RC_TYPE_SGI_20] = { 8, 23, 24, 25, 26, 27, 28, 29, 30 },
    [RC_TYPE_LGI_20] = { 8, 15, 16, 17, 18, 19, 20, 21, 22 },
#endif
    [RC_TYPE_HT_SGI_20] = { 8, 23, 24, 25, 26, 27, 28, 29, 30 },
    [RC_TYPE_HT_LGI_20] = { 8, 15, 16, 17, 18, 19, 20, 21, 22 },
    [RC_TYPE_HT_GF] = { 8, 31, 32, 33, 34, 35, 36, 37, 38 },
};
static u32 ssv6xxx_rate_supported(struct ssv_sta_rc_info *rc_sta, u32 index)
{
    return (rc_sta->rc_supp_rates & BIT(index));
}
#if 1
static u8 ssv6xxx_rate_lowest_index(struct ssv_sta_rc_info *rc_sta)
{
    int i;
    for (i = 0; i < rc_sta->rc_num_rate; i++)
        if (ssv6xxx_rate_supported(rc_sta, i))
            return i;
    return 0;
}
#endif
#ifdef DISABLE_RATE_CONTROL_SAMPLE
static u8 ssv6xxx_rate_highest_index(struct ssv_sta_rc_info *rc_sta)
{
    int i;
    for (i=rc_sta->rc_num_rate-1; i >= 0; i--)
        if (ssv6xxx_rate_supported(rc_sta, i))
            return i;
    return 0;
}
#endif
static void rate_control_pid_adjust_rate(struct ssv_sta_rc_info *rc_sta,
            struct rc_pid_sta_info *spinfo, int adj,struct rc_pid_rateinfo *rinfo)
{
    int cur_sorted, new_sorted, probe, tmp, n_bitrates;
    int cur = spinfo->txrate_idx;
    n_bitrates = rc_sta->rc_num_rate;
    cur_sorted = rinfo[cur].index;
    new_sorted = cur_sorted + adj;
    if (new_sorted < 0)
        new_sorted = rinfo[0].index;
    else if (new_sorted >= n_bitrates)
        new_sorted = rinfo[n_bitrates - 1].index;
    tmp = new_sorted;
 if (adj < 0) {
  for (probe = cur_sorted; probe >= new_sorted; probe--)
   if (rinfo[probe].diff <= rinfo[cur_sorted].diff &&
       ssv6xxx_rate_supported(rc_sta, rinfo[probe].index))
    tmp = probe;
 } else {
  for (probe = new_sorted + 1; probe < n_bitrates; probe++)
   if (rinfo[probe].diff <= rinfo[new_sorted].diff &&
       ssv6xxx_rate_supported(rc_sta, rinfo[probe].index))
    tmp = probe;
 }
    BUG_ON(tmp<0 || tmp>=n_bitrates);
 do {
  if (ssv6xxx_rate_supported(rc_sta, rinfo[tmp].index)) {
   spinfo->tmp_rate_idx = rinfo[tmp].index;
   break;
  }
  if (adj < 0)
   tmp--;
  else
   tmp++;
 } while (tmp < n_bitrates && tmp >= 0);
    spinfo->oldrate = spinfo->txrate_idx;
 if (spinfo->tmp_rate_idx != spinfo->txrate_idx) {
  spinfo->monitoring = 1;
#ifdef RATE_CONTROL_PARAMETER_DEBUG
        printk("Trigger monitor tmp_rate_idx=[%d]\n",spinfo->tmp_rate_idx);
#endif
  spinfo->probe_cnt = MAXPROBES;
 }
}
static void rate_control_pid_normalize(struct rc_pid_info *pinfo, int l)
{
        int i, norm_offset = RC_PID_NORM_OFFSET;
        struct rc_pid_rateinfo *r = pinfo->rinfo;
        if (r[0].diff > norm_offset)
                r[0].diff -= norm_offset;
        else if (r[0].diff < -norm_offset)
                r[0].diff += norm_offset;
        for (i = 0; i < l - 1; i++)
                if (r[i + 1].diff > r[i].diff + norm_offset)
                        r[i + 1].diff -= norm_offset;
                else if (r[i + 1].diff <= r[i].diff)
                        r[i + 1].diff += norm_offset;
}
#ifdef RATE_CONTROL_DEBUG
    unsigned int txrate_dlr=0;
#endif
static void rate_control_pid_sample(struct ssv_rate_ctrl* ssv_rc,struct rc_pid_info *pinfo,
        struct ssv_sta_rc_info *rc_sta,
        struct rc_pid_sta_info *spinfo)
{
 struct rc_pid_rateinfo *rinfo = pinfo->rinfo;
 u8 pf;
 s32 err_avg;
 s32 err_prop;
 s32 err_int;
 s32 err_der;
 int adj, i, j, tmp;
    struct ssv_rc_rate *rc_table;
 unsigned int dlr;
 unsigned int perfect_time = 0;
 unsigned int this_thp, ewma_thp;
 struct rc_pid_rateinfo *rate;
 if (!spinfo->monitoring)
 {
#if 0
  period = msecs_to_jiffies(pinfo->sampling_period);
  if (jiffies - spinfo->last_sample > 2 * period)
   spinfo->sharp_cnt = RC_PID_SHARPENING_DURATION;
#endif
        if (spinfo->tx_num_xmit == 0)
            return;
  spinfo->last_sample = jiffies;
  pf = spinfo->tx_num_failed * 100 / spinfo->tx_num_xmit;
  if (pinfo->rinfo[spinfo->txrate_idx].this_attempt > 0)
  {
   rate = &pinfo->rinfo[spinfo->txrate_idx];
            rc_table = &ssv_rc->rc_table[spinfo->txrate_idx];
   dlr = 100 - rate->this_fail * 100 / rate->this_attempt;
   perfect_time = rate->perfect_tx_time;
   if (!perfect_time)
    perfect_time = 1000000;
   this_thp = dlr * (1000000 / perfect_time);
   ewma_thp = rate->throughput;
   if (ewma_thp == 0)
    rate->throughput = this_thp;
   else
    rate->throughput = (ewma_thp + this_thp) >> 1;
   rate->attempt += rate->this_attempt;
   rate->success += rate->this_success;
   rate->fail += rate->this_fail;
   spinfo->tx_num_xmit = 0;
   spinfo->tx_num_failed = 0;
   rate->this_fail = 0;
   rate->this_success = 0;
   rate->this_attempt = 0;
            if (pinfo->oldrate<0 || pinfo->oldrate>=rc_sta->rc_num_rate)
            {
                WARN_ON(1);
            }
            if (spinfo->txrate_idx<0 || spinfo->txrate_idx>=rc_sta->rc_num_rate)
            {
                WARN_ON(1);
            }
   if (pinfo->oldrate != spinfo->txrate_idx) {
    i = rinfo[pinfo->oldrate].index;
    j = rinfo[spinfo->txrate_idx].index;
                tmp = (pf - spinfo->last_pf);
    tmp = RC_PID_DO_ARITH_RIGHT_SHIFT(tmp, rc_table->arith_shift);
    rinfo[j].diff = rinfo[i].diff + tmp;
    pinfo->oldrate = spinfo->txrate_idx;
   }
   rate_control_pid_normalize(pinfo, rc_sta->rc_num_rate);
   err_prop = (rc_table->target_pf - pf) << rc_table->arith_shift;
   err_avg = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;
   spinfo->err_avg_sc = spinfo->err_avg_sc - err_avg + err_prop;
   err_int = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;
   err_der = pf - spinfo->last_pf;
   spinfo->last_pf = pf;
   spinfo->last_dlr = dlr;
   spinfo->oldrate = spinfo->txrate_idx;
#if 0
   if (spinfo->sharp_cnt)
    spinfo->sharp_cnt--;
#endif
   adj = (err_prop * RC_PID_COEFF_P + err_int * RC_PID_COEFF_I + err_der * RC_PID_COEFF_D);
   adj = RC_PID_DO_ARITH_RIGHT_SHIFT(adj, rc_table->arith_shift<<1);
   if (adj) {
#ifdef RATE_CONTROL_PARAMETER_DEBUG
                if((spinfo->txrate_idx!=11) || ((spinfo->txrate_idx==11)&&(adj < 0)))
                printk("[RC]Probe adjust[%d] dlr[%d%%] this_thp[%d] ewma_thp[%d] index[%d]\n",adj ,dlr,this_thp,ewma_thp,spinfo->txrate_idx);
#endif
    rate_control_pid_adjust_rate(rc_sta, spinfo, adj, rinfo);
   }
  }
 }
 else
    {
        if((spinfo->feedback_probes >= MAXPROBES) || (spinfo->feedback_probes && spinfo->probe_cnt))
        {
      rate = &pinfo->rinfo[spinfo->txrate_idx];
#if 0
      period = msecs_to_jiffies(pinfo->sampling_period);
      if (jiffies - spinfo->last_sample > 2 * period)
       spinfo->sharp_cnt = RC_PID_SHARPENING_DURATION;
#endif
      spinfo->last_sample = jiffies;
      if (rate->this_attempt > 0) {
       dlr = 100 - rate->this_fail * 100 / rate->this_attempt;
#ifdef RATE_CONTROL_DEBUG
#ifdef PROBE
                txrate_dlr=dlr;
#endif
#endif
       spinfo->last_dlr = dlr;
       perfect_time = rate->perfect_tx_time;
       if (!perfect_time)
        perfect_time = 1000000;
       this_thp = dlr * (1000000 / perfect_time);
       ewma_thp = rate->throughput;
       if (ewma_thp == 0)
        rate->throughput = this_thp;
       else
        rate->throughput = (ewma_thp + this_thp) >> 1;
       rate->attempt += rate->this_attempt;
       rate->success += rate->this_success;
       rinfo[spinfo->txrate_idx].fail += rate->this_fail;
       rate->this_fail = 0;
       rate->this_success = 0;
       rate->this_attempt = 0;
      }
            else
            {
#ifdef RATE_CONTROL_DEBUG
#ifdef PROBE
                txrate_dlr=0;
#endif
#endif
            }
            rate = &pinfo->rinfo[spinfo->tmp_rate_idx];
            if (rate->this_attempt > 0) {
                dlr = 100 - ((rate->this_fail * 100) / rate->this_attempt);
                {
                    perfect_time = rate->perfect_tx_time;
                    if (!perfect_time)
                        perfect_time = 1000000;
                    if(dlr)
                        this_thp = dlr * (1000000 / perfect_time);
                    else
                        this_thp = 0;
                    ewma_thp = rate->throughput;
                    if (ewma_thp == 0)
                        rate->throughput = this_thp;
                    else
                        rate->throughput = (ewma_thp + this_thp) >> 1;
                    if (rate->throughput > pinfo->rinfo[spinfo->txrate_idx].throughput)
                    {
#ifdef RATE_CONTROL_PARAMETER_DEBUG
                        printk("[RC]UPDATE probe rate idx[%d] [%d][%d%%] Old idx[%d] [%d][%d%%] feedback[%d] \n",spinfo->tmp_rate_idx,rate->throughput,dlr,spinfo->txrate_idx,pinfo->rinfo[spinfo->txrate_idx].throughput,txrate_dlr,spinfo->feedback_probes);
#endif
                        spinfo->txrate_idx = spinfo->tmp_rate_idx;
                    }
                    else
                    {
#ifdef RATE_CONTROL_PARAMETER_DEBUG
                        printk("[RC]Fail probe rate idx[%d] [%d][%d%%] Old idx[%d] [%d][%d%%] feedback[%d] \n",spinfo->tmp_rate_idx,rate->throughput,dlr,spinfo->txrate_idx,pinfo->rinfo[spinfo->txrate_idx].throughput,txrate_dlr,spinfo->feedback_probes);
#endif
                        ;
                    }
                    rate->attempt += rate->this_attempt;
                    rate->success += rate->this_success;
                    rate->fail += rate->this_fail;
                    rate->this_fail = 0;
                    rate->this_success = 0;
                    rate->this_attempt = 0;
                    spinfo->oldrate = spinfo->txrate_idx;
                }
            }
#ifdef RATE_CONTROL_DEBUG
            else
                printk("SHIT-2!!!!\n");
#endif
            spinfo->feedback_probes = 0;
            spinfo->tx_num_xmit = 0;
            spinfo->tx_num_failed = 0;
            spinfo->monitoring = 0;
#ifdef RATE_CONTROL_PARAMETER_DEBUG
            printk("Disable monitor\n");
#endif
            spinfo->probe_report_flag = 0;
            spinfo->probe_wating_times = 0;
        }
        else
        {
            spinfo->probe_wating_times ++;
#ifdef RATE_CONTROL_DEBUG
            if(spinfo->probe_wating_times > 3)
            {
                printk("[RC]@@@@@ PROBE LOSE @@@@@ feedback=[%d] need=[%d] probe_cnt=[%d] wating times[%d]\n",
                    spinfo->feedback_probes,MAXPROBES,spinfo->probe_cnt,spinfo->probe_wating_times);
                spinfo->feedback_probes = 0;
                spinfo->tx_num_xmit = 0;
                spinfo->tx_num_failed = 0;
                spinfo->monitoring = 0;
                spinfo->probe_report_flag = 0;
                spinfo->probe_wating_times = 0;
            }
#else
            if(spinfo->probe_wating_times > 3)
            {
                spinfo->feedback_probes = 0;
                spinfo->tx_num_xmit = 0;
                spinfo->tx_num_failed = 0;
                spinfo->monitoring = 0;
                spinfo->probe_report_flag = 0;
                spinfo->probe_wating_times = 0;
            }
#endif
        }
    }
}
#ifdef RATE_CONTROL_PERCENTAGE_TRACE
int percentage = 0;
int percentageCounter = 0;
#endif
void ssv6xxx_legacy_report_handler(struct ssv_softc *sc,struct sk_buff *skb,struct ssv_sta_rc_info *rc_sta)
{
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    struct cfg_host_event *host_event;
    struct firmware_rate_control_report_data *report_data;
    struct rc_pid_info *pinfo;
    struct rc_pid_sta_info *spinfo;
    struct rc_pid_rateinfo * pidrate;
    struct rc_pid_rateinfo *rate;
    s32 report_data_index = 0;
    unsigned long period;
    host_event = (struct cfg_host_event *)skb->data;
    report_data = (struct firmware_rate_control_report_data *)&host_event->dat[0];
    if ( (report_data->wsid != (-1))
        && sc->sta_info[report_data->wsid].sta == NULL)
    {
        dev_warn(sc->dev, "RC report has no valid STA.(%d)\n", report_data->wsid);
        return;
    }
    pinfo = &rc_sta->pinfo;
    spinfo = &rc_sta->spinfo;
    pidrate = rc_sta->pinfo.rinfo;
    if(host_event->h_event == SOC_EVT_RC_AMPDU_REPORT)
    {
#if 1
        period = msecs_to_jiffies(HT_RC_UPDATE_INTERVAL);
        if (time_after(jiffies, spinfo->last_sample + period))
        {
            if(rc_sta->rc_num_rate == 12)
                spinfo->txrate_idx = rc_sta->ht.max_tp_rate + 4;
            else
                spinfo->txrate_idx = rc_sta->ht.max_tp_rate;
#ifdef RATE_CONTROL_DEBUG
            printk("MPDU rate update time txrate_idx[%d]!!\n",spinfo->txrate_idx);
#endif
            spinfo->last_sample = jiffies;
        }
#endif
        return;
    }
    else if(host_event->h_event == SOC_EVT_RC_MPDU_REPORT)
    {
#if 0
        printk("SC report !MPDU! wsid[%d]rate[%d]M[%d]S[%d]R[%d]\n",report_data->wsid,report_data->rates[0].data_rate,
               report_data->ampdu_len,report_data->ampdu_ack_len,report_data->rates[0].count);
#endif
        ;
    }
    else
    {
        printk("RC work get garbage!!\n");
        return;
    }
    if(report_data->rates[0].data_rate < 7)
    {
        if(report_data->rates[0].data_rate>3)
        {
            report_data->rates[0].data_rate -= 3;
        }
    }
    if(ssv_rc->rc_table[rc_sta->pinfo.rinfo[spinfo->txrate_idx].rc_index].hw_rate_idx == report_data->rates[0].data_rate)
    {
        report_data_index = rc_sta->pinfo.rinfo[spinfo->txrate_idx].index;
    }
    else if(ssv_rc->rc_table[rc_sta->pinfo.rinfo[spinfo->tmp_rate_idx].rc_index].hw_rate_idx == report_data->rates[0].data_rate)
    {
        report_data_index = rc_sta->pinfo.rinfo[spinfo->tmp_rate_idx].index;
    }
    if((report_data_index != spinfo->tmp_rate_idx) && (report_data_index != spinfo->txrate_idx))
    {
#ifdef RATE_CONTROL_DEBUG
        printk("Rate control report mismatch report_rate_idx[%d] tmp_rate_idx[%d]rate[%d] txrate_idx[%d]rate[%d]!!\n",
            report_data->rates[0].data_rate,spinfo->tmp_rate_idx,
            ssv_rc->rc_table[rc_sta->pinfo.rinfo[spinfo->tmp_rate_idx].rc_index].hw_rate_idx,
            spinfo->txrate_idx,
            ssv_rc->rc_table[rc_sta->pinfo.rinfo[spinfo->txrate_idx].rc_index].hw_rate_idx);
#endif
        return;
    }
    if(report_data_index == spinfo->txrate_idx)
    {
        spinfo->tx_num_xmit += report_data->rates[0].count;
        spinfo->tx_num_failed += (report_data->rates[0].count - report_data->ampdu_ack_len );
        rate = &pidrate[spinfo->txrate_idx];
        rate->this_fail += ( report_data->rates[0].count - report_data->ampdu_ack_len );
        rate->this_attempt += report_data->rates[0].count;
        rate->this_success += report_data->ampdu_ack_len;
    }
    if (report_data_index != spinfo->txrate_idx && report_data_index == spinfo->tmp_rate_idx)
    {
        spinfo->feedback_probes += report_data->ampdu_len;
        rate = &pidrate[spinfo->tmp_rate_idx];
        rate->this_fail += ( report_data->rates[0].count - report_data->ampdu_ack_len );
        rate->this_attempt += report_data->rates[0].count;
        rate->this_success += report_data->ampdu_ack_len;
    }
    period = msecs_to_jiffies(RC_PID_INTERVAL);
    if (time_after(jiffies, spinfo->last_sample + period))
    {
#ifdef RATE_CONTROL_PERCENTAGE_TRACE
        rate = &pidrate[spinfo->txrate_idx];
        if(rate->this_success > rate->this_attempt)
        {
            printk("#############################\n");
            printk("this_success[%ld] this_attempt[%ld]\n",rate->this_success,rate->this_attempt);
            printk("#############################\n");
        }
        else
        {
            if(percentage == 0)
                percentage = (int)((rate->this_success*100)/rate->this_attempt);
            else
                percentage = (percentage + (int)((rate->this_success*100)/rate->this_attempt))/2;
            printk("Percentage[%d]\n",percentage);
            if((percentageCounter % 16)==1)
                percentage = 0;
        }
#endif
#ifdef RATE_CONTROL_STUPID_DEBUG
        if (spinfo->txrate_idx != spinfo->tmp_rate_idx)
        {
            rate = &pidrate[spinfo->tmp_rate_idx];
            if (spinfo->monitoring && ((rate->this_attempt == 0)||(rate->this_attempt!=MAXPROBES)))
            {
                printk("Probe result a[%ld]s[%ld]f[%ld]",rate->this_attempt,rate->this_success,rate->this_fail);
            }
            rate = &pidrate[spinfo->txrate_idx];
            printk("New a[%ld]s[%ld]f[%ld] \n",rate->this_attempt,rate->this_success,rate->this_fail);
        }
        else
        {
            rate = &pidrate[spinfo->txrate_idx];
            printk("New a[%ld]s[%ld]f[%ld] \n",rate->this_attempt,rate->this_success,rate->this_fail);
        }
        printk("w[%d]x%03d-f%03d\n",rc_sta->rc_wsid,spinfo->tx_num_xmit,spinfo->tx_num_failed);
#endif
        rate_control_pid_sample(sc->rc, pinfo, rc_sta, spinfo);
    }
}
void ssv6xxx_sample_work(struct work_struct *work)
{
    struct ssv_softc *sc = container_of(work, struct ssv_softc, rc_sample_work);
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    struct sk_buff *skb;
    struct cfg_host_event *host_event;
    struct ssv_sta_rc_info *rc_sta=NULL;
    struct firmware_rate_control_report_data *report_data;
    struct ssv_sta_info *ssv_sta;
    u8 hw_wsid = 0;
    sc->rc_sample_sechedule = 1;
    while(1)
    {
        skb = skb_dequeue(&sc->rc_report_queue);
        if(skb == NULL)
            break;
#ifdef DISABLE_RATE_CONTROL_SAMPLE
        {
            dev_kfree_skb_any(skb);
            continue;
        }
#endif
        host_event = (struct cfg_host_event *)skb->data;
        if((host_event->h_event == SOC_EVT_RC_AMPDU_REPORT) || (host_event->h_event == SOC_EVT_RC_MPDU_REPORT))
        {
            report_data = (struct firmware_rate_control_report_data *)&host_event->dat[0];
            hw_wsid = report_data->wsid;
        }
        else
        {
            printk("RC work get garbage!!\n");
            dev_kfree_skb_any(skb);
            continue;
        }
        if(hw_wsid >= SSV_RC_MAX_HARDWARE_SUPPORT)
        {
#ifdef RATE_CONTROL_DEBUG
            printk("[RC]rc_sta is NULL pointer Check-0!!\n");
#endif
            dev_kfree_skb_any(skb);
            continue;
        }
        ssv_sta = &sc->sta_info[hw_wsid];
        if (ssv_sta->sta == NULL)
        {
            dev_err(sc->dev, "Null STA %d for RC report.\n", hw_wsid);
            rc_sta = NULL;
        }
        else
        {
            struct ssv_sta_priv_data *ssv_sta_priv = (struct ssv_sta_priv_data *)ssv_sta->sta->drv_priv;
            rc_sta = &ssv_rc->sta_rc_info[ssv_sta_priv->rc_idx];
            if (rc_sta->rc_wsid != hw_wsid)
            {
                rc_sta = NULL;
            }
        }
        if(rc_sta == NULL)
        {
            dev_err(sc->dev, "[RC]rc_sta is NULL pointer Check-1!!\n");
            dev_kfree_skb_any(skb);
            continue;
        }
#if 0
        if(rc_sta->rc_wsid != hw_wsid)
        {
            printk("[RC] wsid mapping [ERROR] feedback wsid[%d] rc_wsid[%d]...><\n",hw_wsid,rc_sta->rc_wsid);
#if 1
            for(i=0;i<SSV_RC_MAX_STA;i++)
            {
                rc_sta = &ssv_rc->sta_rc_info[i];
                if(rc_sta == NULL)
                    break;
                if(hw_wsid == rc_sta->rc_wsid)
                {
                    printk("[RC] Get new mapping-%d-%d...@o@\n",hw_wsid, i);
                    break;
                }
            }
   if(i == SSV_RC_MAX_STA)
#endif
                rc_sta = NULL;
        }
#endif
        if(rc_sta == NULL)
        {
#ifdef RATE_CONTROL_DEBUG
            printk("[RC]rc_sta is NULL pointer Check-2!!\n");
#endif
            dev_kfree_skb_any(skb);
            continue;
        }
        if(rc_sta->is_ht)
        {
            ssv6xxx_legacy_report_handler(sc,skb,rc_sta);
            ssv6xxx_ht_report_handler(sc,skb,rc_sta);
        }
        else
            ssv6xxx_legacy_report_handler(sc,skb,rc_sta);
        dev_kfree_skb_any(skb);
    }
    sc->rc_sample_sechedule = 0;
}
static void ssv6xxx_tx_status(void *priv, struct ieee80211_supported_band *sband,
                  struct ieee80211_sta *sta, void *priv_sta,
                  struct sk_buff *skb)
{
    struct ssv_softc *sc;
    struct ieee80211_hdr *hdr;
    __le16 fc;
    hdr = (struct ieee80211_hdr *)skb->data;
    fc = hdr->frame_control;
    if (!priv_sta || !ieee80211_is_data_qos(fc))
        return;
    sc = (struct ssv_softc *)priv;
    if ( conf_is_ht(&sc->hw->conf)
        && (!(skb->protocol == cpu_to_be16(ETH_P_PAE))))
    {
        if (skb_get_queue_mapping(skb) != IEEE80211_AC_VO)
            ssv6200_ampdu_tx_update_state(priv, sta, skb);
    }
    return;
}
#if 1
static void rateControlGetRate(u8 rateIndex, char * pointer)
{
    switch(rateIndex)
    {
        case 0:
            sprintf(pointer, "1Mbps");
            return;
        case 1:
        case 4:
            sprintf(pointer, "2Mbps");
            return;
        case 2:
        case 5:
            sprintf(pointer, "5.5Mbps");
            return;
        case 3:
        case 6:
            sprintf(pointer, "11Mbps");
            return;
        case 7:
            sprintf(pointer, "6Mbps");
            return;
        case 8:
            sprintf(pointer, "9Mbps");
            return;
        case 9:
            sprintf(pointer, "12Mbps");
            return;
        case 10:
            sprintf(pointer, "18Mbps");
            return;
        case 11:
            sprintf(pointer, "24Mbps");
            return;
        case 12:
            sprintf(pointer, "36Mbps");
            return;
        case 13:
            sprintf(pointer, "48Mbps");
            return;
        case 14:
            sprintf(pointer, "54Mbps");
            return;
        case 15:
        case 31:
            sprintf(pointer, "MCS0-l");
            return;
        case 16:
        case 32:
            sprintf(pointer, "MCS1-l");
            return;
        case 17:
        case 33:
            sprintf(pointer, "MCS2-l");
            return;
        case 18:
        case 34:
            sprintf(pointer, "MCS3-l");
            return;
        case 19:
        case 35:
            sprintf(pointer, "MCS4-l");
            return;
        case 20:
        case 36:
            sprintf(pointer, "MCS5-l");
            return;
        case 21:
        case 37:
            sprintf(pointer, "MCS6-l");
            return;
        case 22:
        case 38:
            sprintf(pointer, "MCS7-l");
            return;
        case 23:
            sprintf(pointer, "MCS0-s");
            return;
        case 24:
            sprintf(pointer, "MCS1-s");
            return;
        case 25:
            sprintf(pointer, "MCS2-s");
            return;
        case 26:
            sprintf(pointer, "MCS3-s");
            return;
        case 27:
            sprintf(pointer, "MCS4-s");
            return;
        case 28:
            sprintf(pointer, "MCS5-s");
            return;
        case 29:
            sprintf(pointer, "MCS6-s");
            return;
        case 30:
            sprintf(pointer, "MCS7-s");
            return;
        default:
            sprintf(pointer, "Unknow");
            return;
    };
}
#endif
static void ssv6xxx_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
                 struct ieee80211_tx_rate_control *txrc)
{
    struct ssv_softc *sc=priv;
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    struct ssv_sta_rc_info *rc_sta=priv_sta;
    struct sk_buff *skb = txrc->skb;
    struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
    struct ieee80211_tx_rate *rates = tx_info->control.rates;
    struct rc_pid_sta_info *spinfo=&rc_sta->spinfo;
    struct ssv_rc_rate *rc_rate = NULL;
    struct ssv_sta_priv_data *ssv_sta_priv;
    int rateidx = 99;
    #if 0
    if ( (tx_info->control.vif != NULL)
        && (tx_info->control.vif->p2p))
    {
        tx_info->flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
    }
    #endif
    if (rate_control_send_low(sta, priv_sta, txrc))
    {
        int i = 0;
        int total_rates = (sizeof(ssv_11bgn_rate_table) / sizeof(ssv_11bgn_rate_table[0]));
#if 1
        if ((txrc->rate_idx_mask & (1 << rates[0].idx)) == 0)
        {
            u32 rate_idx = rates[0].idx + 1;
            u32 rate_idx_mask = txrc->rate_idx_mask >> rate_idx;
            while (rate_idx_mask && (rate_idx_mask & 1) == 0)
            {
                rate_idx_mask >>= 1;
                rate_idx++;
            }
            if (rate_idx_mask)
                rates[0].idx = rate_idx;
            else
            {
                WARN_ON(rate_idx_mask == 0);
            }
        }
#endif
        for (i = 0; i < total_rates; i++)
        {
            if (rates[0].idx == ssv_11bgn_rate_table[i].dot11_rate_idx)
            {
                break;
            }
        }
        if (i < total_rates)
            rc_rate = &ssv_rc->rc_table[i];
        else
        {
            WARN_ON("Failed to find matching low rate.");
        }
    }
    if (rc_rate == NULL) {
        if (conf_is_ht(&sc->hw->conf) &&
                (sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
            tx_info->flags |= IEEE80211_TX_CTL_LDPC;
        if (conf_is_ht(&sc->hw->conf) &&
                (sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC))
            tx_info->flags |= (1 << IEEE80211_TX_CTL_STBC_SHIFT);
        if (sc->sc_flags & SC_OP_FIXED_RATE) {
            rateidx = sc->max_rate_idx;
        }
        else {
            if (rc_sta->rc_valid == false) {
                rateidx = 0;
            }
            else {
                if ((rc_sta->rc_wsid >= SSV_RC_MAX_HARDWARE_SUPPORT) || (rc_sta->rc_wsid < 0))
                {
                    ssv_sta_priv = (struct ssv_sta_priv_data *)sta->drv_priv;
                    {
                        if ((rc_sta->ht_rc_type >= RC_TYPE_HT_SGI_20) &&
                            (ssv_sta_priv->rx_data_rate < SSV62XX_RATE_MCS_INDEX))
                        {
                            rateidx = rc_sta->pinfo.rinfo[spinfo->txrate_idx].rc_index;
                            #if 0
                            printk("RC %d rx %d tx %d\n", ssv_sta_priv->sta_idx,
                                   ssv_sta_priv->rx_data_rate, rateidx);
                            #endif
                        }
                        else
                        {
                            rateidx = ssv_sta_priv->rx_data_rate;
                        }
                    }
                }
                else
                {
                    if (rc_sta->is_ht)
                    {
#ifdef DISABLE_RATE_CONTROL_SAMPLE
                        rateidx = rc_sta->ht.groups.rates[MCS_GROUP_RATES-1].rc_index;
#else
                        rateidx = rc_sta->pinfo.rinfo[spinfo->txrate_idx].rc_index;
#endif
                    }
                    else
                    {
#if 0
                        if (spinfo->monitoring && spinfo->probe_cnt > 0) {
                            rateidx = rc_sta->pinfo.rinfo[spinfo->tmp_rate_idx].rc_index;
                            spinfo->probe_cnt--;
                        }
                        else
#endif
                        {
                            BUG_ON(spinfo->txrate_idx >= rc_sta->rc_num_rate);
                            rateidx = rc_sta->pinfo.rinfo[spinfo->txrate_idx].rc_index;
                        }
                        if(rateidx<4)
                        {
                            if(rateidx)
                            {
                                if ((sc->sc_flags & SC_OP_SHORT_PREAMBLE)||(txrc->short_preamble))
                                {
                                    rateidx += 3;
                                }
                            }
                        }
                    }
                }
            }
        }
        rc_rate = &ssv_rc->rc_table[rateidx];
#if 1
        if (spinfo->real_hw_index != rc_rate->hw_rate_idx)
        {
            char string[24];
            rateControlGetRate(rc_rate->hw_rate_idx,string);
        }
#endif
  spinfo->real_hw_index = rc_rate->hw_rate_idx;
        rates[0].count = 4;
        rates[0].idx = rc_rate->dot11_rate_idx;
        tx_info->control.rts_cts_rate_idx =
            ssv_rc->rc_table[rc_rate->ctrl_rate_idx].dot11_rate_idx;
        if (rc_rate->rc_flags & RC_FLAG_SHORT_PREAMBLE)
            rates[0].flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;
        if (rc_rate->rc_flags & RC_FLAG_HT) {
            rates[0].flags |= IEEE80211_TX_RC_MCS;
            if (rc_rate->rc_flags & RC_FLAG_HT_SGI)
                rates[0].flags |= IEEE80211_TX_RC_SHORT_GI;
            if (rc_rate->rc_flags & RC_FLAG_HT_GF)
                rates[0].flags |= IEEE80211_TX_RC_GREEN_FIELD;
        }
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
        if (txrc->rts)
        {
            rates[0].flags |= IEEE80211_TX_RC_USE_RTS_CTS;
        }
        if ((tx_info->control.vif &&
            tx_info->control.vif->bss_conf.use_cts_prot) &&
            (rc_rate->phy_type==WLAN_RC_PHY_OFDM ||
            rc_rate->phy_type>WLAN_RC_PHY_OFDM))
        {
            rates[0].flags |= IEEE80211_TX_RC_USE_CTS_PROTECT;
            tx_info->control.rts_cts_rate_idx = 1;
        }
#endif
    }
    rates[1].count = 0;
    rates[1].idx = -1;
    rates[SSV_DRATE_IDX].count = rc_rate->hw_rate_idx;
 rc_rate = &ssv_rc->rc_table[rc_rate->ctrl_rate_idx];
 rates[SSV_CRATE_IDX].count = rc_rate->hw_rate_idx;
}
int pide_frame_duration(size_t len,
                 int rate, int short_preamble, int flags)
{
    int dur=0;
    if (flags == WLAN_RC_PHY_CCK)
    {
        dur = 10;
        dur += short_preamble ? (72 + 24) : (144 + 48);
        dur += DIV_ROUND_UP(8 * (len + 4) * 10, rate);
    }
    else {
        dur = 16;
        dur += 16;
        dur += 4;
        dur += 4 * DIV_ROUND_UP((16 + 8 * (len + 4) + 6) * 10,
                    4 * rate);
    }
    return dur;
}
static void ssv62xx_rc_caps(struct ssv_sta_rc_info *rc_sta)
{
    struct rc_pid_sta_info *spinfo;
    struct rc_pid_info *pinfo;
    struct rc_pid_rateinfo *rinfo;
    int i;
    spinfo = &rc_sta->spinfo;
    pinfo = &rc_sta->pinfo;
    memset(spinfo, 0, sizeof(struct rc_pid_sta_info));
    memset(pinfo, 0, sizeof(struct rc_pid_info));
    rinfo = rc_sta->pinfo.rinfo;
    for(i=0; i<rc_sta->rc_num_rate; i++) {
        rinfo[i].rc_index = ssv6xxx_rc_rate_set[rc_sta->rc_type][i+1];
        rinfo[i].diff = i * RC_PID_NORM_OFFSET;
        rinfo[i].index = (u16)i;
        rinfo[i].perfect_tx_time = TDIFS + (TSLOT * 15 >> 1) + pide_frame_duration(1530,
            ssv_11bgn_rate_table[rinfo[i].rc_index].rate_kbps/100, 1,ssv_11bgn_rate_table[rinfo[i].rc_index].phy_type) +
            pide_frame_duration(10, ssv_11bgn_rate_table[rinfo[i].rc_index].rate_kbps/100, 1,ssv_11bgn_rate_table[rinfo[i].rc_index].phy_type);
#if 1
        printk("[RC]Init perfect_tx_time[%d][%d]\n",i,rinfo[i].perfect_tx_time);
#endif
        rinfo[i].throughput = 0;
    }
    if(rc_sta->is_ht)
    {
        if(ssv6xxx_rc_rate_set[rc_sta->ht_rc_type][0] == 12)
            spinfo->txrate_idx = 4;
        else
            spinfo->txrate_idx = 0;
    }
    else
    {
        spinfo->txrate_idx = ssv6xxx_rate_lowest_index(rc_sta);
#ifdef DISABLE_RATE_CONTROL_SAMPLE
        spinfo->txrate_idx = ssv6xxx_rate_highest_index(rc_sta);
#endif
    }
    spinfo->real_hw_index = 0;
    spinfo->probe_cnt = MAXPROBES;
    spinfo->tmp_rate_idx = spinfo->txrate_idx;
    spinfo->oldrate = spinfo->txrate_idx;
    spinfo->last_sample = jiffies;
    spinfo->last_report = jiffies;
}
static void ssv6xxx_rate_update_rc_type(void *priv, struct ieee80211_supported_band *sband,
                  struct ieee80211_sta *sta, void *priv_sta)
{
    struct ssv_softc *sc=priv;
    struct ssv_hw *sh=sc->sh;
    struct ssv_sta_rc_info *rc_sta=priv_sta;
    int i;
    u32 ht_supp_rates = 0;
    BUG_ON(rc_sta->rc_valid == false);
    printk("[I] %s(): \n", __FUNCTION__);
    rc_sta->ht_supp_rates = 0;
    rc_sta->rc_supp_rates = 0;
    rc_sta->is_ht = 0;
#ifndef CONFIG_CH14_SUPPORT_GN_MODE
    if(sc->cur_channel->hw_value == 14)
    {
        printk("[RC init ]Channel 14 support\n");
        if((sta->supp_rates[sband->band] & (~0xfL)) == 0x0)
        {
            printk("[RC init ]B only mode\n");
            rc_sta->rc_type = RC_TYPE_B_ONLY;
        }
        else
        {
            printk("[RC init ]GB mode\n");
            rc_sta->rc_type = RC_TYPE_LEGACY_GB;
        }
    }
    else
#endif
    if (sta->ht_cap.ht_supported == true) {
        printk("[RC init ]HT support wsid\n");
        for (i = 0; i < SSV_HT_RATE_MAX; i++) {
            if (sta->ht_cap.mcs.rx_mask[i/MCS_GROUP_RATES] & (1<<(i%MCS_GROUP_RATES)))
                ht_supp_rates |= BIT(i);
        }
        rc_sta->ht_supp_rates = ht_supp_rates;
        if (sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD)
        {
            rc_sta->rc_type = RC_TYPE_HT_GF;
            rc_sta->ht_rc_type = RC_TYPE_HT_GF;
        }
        else if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
        {
            rc_sta->rc_type = RC_TYPE_SGI_20;
            rc_sta->ht_rc_type = RC_TYPE_HT_SGI_20;
        }
        else
        {
            rc_sta->rc_type = RC_TYPE_LGI_20;
            rc_sta->ht_rc_type = RC_TYPE_HT_LGI_20;
        }
    }
    else
    {
        if((sta->supp_rates[sband->band] & (~0xfL)) == 0x0){
            rc_sta->rc_type = RC_TYPE_B_ONLY;
            printk("[RC init ]B only mode\n");
        }
        else{
            rc_sta->rc_type = RC_TYPE_LEGACY_GB;
            printk("[RC init ]legacy G mode\n");
        }
    }
#ifdef CONFIG_SSV_DPD
#ifdef CONFIG_SSV_CABRIO_E
    if(rc_sta->rc_type == RC_TYPE_B_ONLY)
    {
        SMAC_REG_WRITE(sh, ADR_TX_FE_REGISTER, 0x3D3E84FE);
        SMAC_REG_WRITE(sh, ADR_RX_FE_REGISTER_1, 0x1457D79);
        SMAC_REG_WRITE(sh, ADR_DPD_CONTROL, 0x0);
    }
    else
    {
        SMAC_REG_WRITE(sh, ADR_TX_FE_REGISTER, 0x3CBE84FE);
        SMAC_REG_WRITE(sh, ADR_RX_FE_REGISTER_1, 0x4507F9);
        SMAC_REG_WRITE(sh, ADR_DPD_CONTROL, 0x3);
    }
#endif
#endif
    if((rc_sta->rc_type != RC_TYPE_B_ONLY) && (rc_sta->rc_type != RC_TYPE_LEGACY_GB))
    {
        if ((sta->ht_cap.ht_supported) && (sh->cfg.hw_caps & SSV6200_HW_CAP_AMPDU_TX))
        {
            rc_sta->is_ht = 1;
            ssv62xx_ht_rc_caps(ssv6xxx_rc_rate_set, rc_sta);
        }
    }
    {
        rc_sta->rc_num_rate = (u8)ssv6xxx_rc_rate_set[rc_sta->rc_type][0];
        if((rc_sta->rc_type == RC_TYPE_HT_GF) ||
            (rc_sta->rc_type == RC_TYPE_LGI_20) || (rc_sta->rc_type == RC_TYPE_SGI_20))
        {
            if(rc_sta->rc_num_rate == 12)
            {
                rc_sta->rc_supp_rates = sta->supp_rates[sband->band] & 0xfL;
                rc_sta->rc_supp_rates |= (ht_supp_rates << 4);
            }
            else
                rc_sta->rc_supp_rates = ht_supp_rates;
        }
        else if(rc_sta->rc_type == RC_TYPE_LEGACY_GB)
            rc_sta->rc_supp_rates = sta->supp_rates[sband->band];
        else if(rc_sta->rc_type == RC_TYPE_B_ONLY)
            rc_sta->rc_supp_rates = sta->supp_rates[sband->band] & 0xfL;
        ssv62xx_rc_caps(rc_sta);
    }
}
#if LINUX_VERSION_CODE > 0x030500
static void ssv6xxx_rate_update(void *priv, struct ieee80211_supported_band *sband,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
                            struct cfg80211_chan_def *chandef,
#endif
                            struct ieee80211_sta *sta, void *priv_sta,
                            u32 changed)
#else
static void ssv6xxx_rate_update(void *priv, struct ieee80211_supported_band *sband,
                            struct ieee80211_sta *sta, void *priv_sta,
                            u32 changed, enum nl80211_channel_type oper_chan_type)
#endif
{
    printk("%s: changed=%d\n",__FUNCTION__,changed);
    return;
}
static void ssv6xxx_rate_init(void *priv, struct ieee80211_supported_band *sband,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
                  struct cfg80211_chan_def *chandef,
#endif
                  struct ieee80211_sta *sta, void *priv_sta)
{
    ssv6xxx_rate_update_rc_type(priv, sband, sta, priv_sta);
}
static void *ssv6xxx_rate_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
    struct ssv_sta_priv_data *sta_priv = (struct ssv_sta_priv_data *)sta->drv_priv;
#ifndef RC_STA_DIRECT_MAP
    struct ssv_softc *sc = priv;
    struct ssv_rate_ctrl *ssv_rc = sc->rc;
    int s;
    sta_priv = (struct ssv_sta_priv_data *)sta->drv_priv;
    for(s=0; s<SSV_RC_MAX_STA; s++) {
        if (ssv_rc->sta_rc_info[s].rc_valid == false) {
            printk("%s(): use index %d\n", __FUNCTION__, s);
            memset(&ssv_rc->sta_rc_info[s], 0, sizeof(struct ssv_sta_rc_info));
            ssv_rc->sta_rc_info[s].rc_valid = true;
            ssv_rc->sta_rc_info[s].rc_wsid = -1;
            sta_priv->rc_idx = s;
            return &ssv_rc->sta_rc_info[s];
        }
    }
    return NULL;
#else
    sta_priv->rc_idx = (-1);
    return sta_priv;
#endif
}
static void ssv6xxx_rate_free_sta(void *priv, struct ieee80211_sta *sta,
                              void *priv_sta)
{
    struct ssv_sta_rc_info *rc_sta=priv_sta;
    rc_sta->rc_valid = false;
}
static void *ssv6xxx_rate_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
    struct ssv_softc *sc=hw->priv;
    struct ssv_rate_ctrl *ssv_rc;
    sc->rc = kzalloc(sizeof(struct ssv_rate_ctrl), GFP_KERNEL);
    if (!sc->rc) {
        printk("%s(): Unable to allocate RC structure !\n",
            __FUNCTION__);
        return NULL;
    }
    memset(sc->rc, 0, sizeof(struct ssv_rate_ctrl));
    ssv_rc = (struct ssv_rate_ctrl *)sc->rc;
    ssv_rc->rc_table = ssv_11bgn_rate_table;
    skb_queue_head_init(&sc->rc_report_queue);
    INIT_WORK(&sc->rc_sample_work,ssv6xxx_sample_work);
    sc->rc_sample_workqueue = create_workqueue("ssv6xxx_rc_sample");
    sc->rc_sample_sechedule = 0;
    return hw->priv;
}
static void ssv6xxx_rate_free(void *priv)
{
    struct ssv_softc *sc=priv;
    if (sc->rc) {
        kfree(sc->rc);
        sc->rc = NULL;
    }
    sc->rc_sample_sechedule = 0;
    cancel_work_sync(&sc->rc_sample_work);
    flush_workqueue(sc->rc_sample_workqueue);
    destroy_workqueue(sc->rc_sample_workqueue);
}
static struct rate_control_ops ssv_rate_ops =
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
    .module = NULL,
#endif
    .name = "ssv6xxx_rate_control",
    .tx_status = ssv6xxx_tx_status,
    .get_rate = ssv6xxx_get_rate,
    .rate_init = ssv6xxx_rate_init,
    .rate_update = ssv6xxx_rate_update,
    .alloc = ssv6xxx_rate_alloc,
    .free = ssv6xxx_rate_free,
    .alloc_sta = ssv6xxx_rate_alloc_sta,
    .free_sta = ssv6xxx_rate_free_sta,
};
void ssv6xxx_rc_mac8011_rate_idx(struct ssv_softc *sc,
            int hw_rate_idx, struct ieee80211_rx_status *rxs)
{
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    struct ssv_rc_rate *rc_rate;
    BUG_ON(hw_rate_idx>=RATE_TABLE_SIZE &&
        hw_rate_idx < 0);
    rc_rate = &ssv_rc->rc_table[hw_rate_idx];
    if (rc_rate->rc_flags & RC_FLAG_HT) {
        rxs->flag |= RX_FLAG_HT;
        if (rc_rate->rc_flags & RC_FLAG_HT_SGI)
            rxs->flag |= RX_FLAG_SHORT_GI;
    }
    else {
        if (rc_rate->rc_flags & RC_FLAG_SHORT_PREAMBLE)
            rxs->flag |= RX_FLAG_SHORTPRE;
    }
    rxs->rate_idx = rc_rate->dot11_rate_idx;
}
void ssv6xxx_rc_hw_rate_idx(struct ssv_softc *sc,
            struct ieee80211_tx_info *info, struct ssv_rate_info *sr)
{
    struct ieee80211_tx_rate *tx_rate;
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    tx_rate = &info->control.rates[0];
    sr->d_flags = (ssv_rc->rc_table[tx_rate[SSV_DRATE_IDX].count].phy_type == WLAN_RC_PHY_OFDM) ? IEEE80211_RATE_ERP_G:0;
    sr->d_flags |= (ssv_rc->rc_table[tx_rate[SSV_DRATE_IDX].count].rc_flags & RC_FLAG_SHORT_PREAMBLE)? IEEE80211_RATE_SHORT_PREAMBLE:0;
    sr->c_flags = (ssv_rc->rc_table[tx_rate[SSV_CRATE_IDX].count].phy_type == WLAN_RC_PHY_OFDM) ? IEEE80211_RATE_ERP_G:0;
    sr->c_flags |= (ssv_rc->rc_table[tx_rate[SSV_CRATE_IDX].count].rc_flags & RC_FLAG_SHORT_PREAMBLE)? IEEE80211_RATE_SHORT_PREAMBLE:0;
    sr->drate_kbps = ssv_rc->rc_table[tx_rate[SSV_DRATE_IDX].count].rate_kbps;
    sr->drate_hw_idx = tx_rate[SSV_DRATE_IDX].count;
    sr->crate_kbps = ssv_rc->rc_table[tx_rate[SSV_CRATE_IDX].count].rate_kbps;
    sr->crate_hw_idx = tx_rate[SSV_CRATE_IDX].count;
}
#ifdef RATE_CONTROL_REALTIME_UPDATA
u8 ssv6xxx_rc_hw_rate_update_check(struct sk_buff *skb, struct ssv_softc *sc, u32 do_rts_cts)
{
    int ret = 0;
    struct ssv_rate_ctrl *ssv_rc = sc->rc;
    struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
    struct SKB_info_st *skb_info = (struct SKB_info_st *)skb->head;
    struct ieee80211_sta *sta = skb_info->sta;
    struct ieee80211_tx_rate *rates = &tx_info->control.rates[0];
    struct ssv_rc_rate *rc_rate = NULL;
    u8 rateidx=0;
    struct ssv_sta_rc_info *rc_sta = NULL;
    struct rc_pid_sta_info *spinfo;
    struct ssv_sta_priv_data *sta_priv = NULL;
    unsigned long period=0;
    if (sc->sc_flags & SC_OP_FIXED_RATE)
        return ret;
    if(sta == NULL)
        return ret;
    sta_priv = (struct ssv_sta_priv_data *)sta->drv_priv;
    if(sta_priv == NULL)
    {
#ifdef RATE_CONTROL_DEBUG
        printk("%s sta_priv == NULL \n\r", __FUNCTION__);
#endif
        return ret;
    }
    if((sta_priv->rc_idx < 0)||(sta_priv->rc_idx >= SSV_RC_MAX_STA))
    {
#ifdef RATE_CONTROL_DEBUG
        printk("%s rc_idx %x illegal \n\r", __FUNCTION__, sta_priv->rc_idx);
#endif
        return ret;
    }
    rc_sta = &ssv_rc->sta_rc_info[sta_priv->rc_idx];
    if(rc_sta->rc_valid == false)
    {
#ifdef RATE_CONTROL_DEBUG
        printk("%s rc_valid false \n\r", __FUNCTION__);
#endif
        return ret;
    }
    spinfo= &rc_sta->spinfo;
    period = msecs_to_jiffies(RC_PID_REPORT_INTERVAL);
    if (time_after(jiffies, spinfo->last_report + period))
    {
        ret |= RC_FIRMWARE_REPORT_FLAG;
        spinfo->last_report = jiffies;
    }
    {
        if (spinfo->monitoring)
        {
            if(spinfo->probe_report_flag == 0)
            {
                ret |= RC_FIRMWARE_REPORT_FLAG;
                spinfo->last_report = jiffies;
                spinfo->probe_report_flag = 1;
                rateidx = spinfo->real_hw_index;
            }
            else if (spinfo->probe_cnt > 0 && spinfo->probe_report_flag) {
                rateidx = rc_sta->pinfo.rinfo[spinfo->tmp_rate_idx].rc_index;
                spinfo->probe_cnt--;
                if(spinfo->probe_cnt == 0)
                {
                    ret |= RC_FIRMWARE_REPORT_FLAG;
                    spinfo->last_report = jiffies;
                }
            }
            else
                rateidx = spinfo->real_hw_index;
        }
        else
            rateidx = spinfo->real_hw_index;
    }
    if(rateidx >= RATE_TABLE_SIZE)
    {
        printk("[ERROR]rateidx over range\n\r");
        return 0;
    }
    rc_rate = &ssv_rc->rc_table[rateidx];
#ifdef RATE_CONTROL_STUPID_DEBUG
    if (spinfo->monitoring && (spinfo->probe_cnt))
    {
        char string[24];
        rateControlGetRate(rc_rate->hw_rate_idx,string);
        printk("[RC]Probe rate[%s]\n",string);
    }
#endif
    if(rc_rate == NULL)
        return ret;
    if(rc_rate->hw_rate_idx != rates[SSV_DRATE_IDX].count)
    {
        rates[0].flags = 0;
        if (rc_rate->rc_flags & RC_FLAG_SHORT_PREAMBLE)
            rates[0].flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;
        if (rc_rate->rc_flags & RC_FLAG_HT) {
            rates[0].flags |= IEEE80211_TX_RC_MCS;
            if (rc_rate->rc_flags & RC_FLAG_HT_SGI)
                rates[0].flags |= IEEE80211_TX_RC_SHORT_GI;
            if (rc_rate->rc_flags & RC_FLAG_HT_GF)
                rates[0].flags |= IEEE80211_TX_RC_GREEN_FIELD;
        }
        rates[SSV_DRATE_IDX].count = rc_rate->hw_rate_idx;
        if (do_rts_cts & IEEE80211_TX_RC_USE_CTS_PROTECT)
        {
            rates[SSV_CRATE_IDX].count = 0;
        }
        else
        {
            rc_rate = &ssv_rc->rc_table[rc_rate->ctrl_rate_idx];
            rates[SSV_CRATE_IDX].count = rc_rate->hw_rate_idx;
        }
        ret |= 0x1;
    }
    return ret;
}
#endif
void ssv6xxx_rc_hw_reset(struct ssv_softc *sc, int rc_idx, int hwidx)
{
    struct ssv_rate_ctrl *ssv_rc=sc->rc;
    struct ssv_sta_rc_info *rc_sta;
    u32 rc_hw_reg[] = { ADR_MTX_MIB_WSID0, ADR_MTX_MIB_WSID1 };
    BUG_ON(rc_idx >= SSV_RC_MAX_STA);
    rc_sta = &ssv_rc->sta_rc_info[rc_idx];
    if (hwidx >=0 && hwidx<SSV_NUM_HW_STA) {
        rc_sta->rc_wsid = hwidx;
        printk("rc_wsid[%d] rc_idx[%d]\n",rc_sta[rc_idx].rc_wsid,rc_idx);
        SMAC_REG_WRITE(sc->sh, rc_hw_reg[hwidx], 0x40000000);
    }
    else
    {
        rc_sta->rc_wsid = -1;
    }
}
#define UPDATE_PHY_INFO_ACK_RATE(_phy_info,_ack_rate_idx) ( _phy_info = (_phy_info&0xfffffc0f)|(_ack_rate_idx<<4))
int ssv6xxx_rc_update_bmode_ctrl_rate(struct ssv_softc *sc, int rate_tbl_idx, int ctrl_rate_idx)
{
     u32 temp32;
     struct ssv_hw *sh = sc->sh;
     u32 addr;
    addr = sh->hw_pinfo+rate_tbl_idx*4;
    ssv_11bgn_rate_table[rate_tbl_idx].ctrl_rate_idx = ctrl_rate_idx;
    SMAC_REG_READ(sh, addr, &temp32);
    UPDATE_PHY_INFO_ACK_RATE(temp32, ctrl_rate_idx);
    SMAC_REG_WRITE(sh, addr, temp32);
    SMAC_REG_CONFIRM(sh, addr, temp32);
    return 0;
}
void ssv6xxx_rc_update_basic_rate(struct ssv_softc *sc, u32 basic_rates)
{
    int i;
    int rate_idx, pre_rate_idx = 0;
    for(i=0;i<4;i++)
    {
        if(((basic_rates>>i)&0x01))
        {
            rate_idx = i;
            pre_rate_idx = i;
        }
        else
            rate_idx = pre_rate_idx;
        ssv6xxx_rc_update_bmode_ctrl_rate(sc, i, rate_idx);
        if(i)
            ssv6xxx_rc_update_bmode_ctrl_rate(sc, i+3, rate_idx);
    }
}
int ssv6xxx_rate_control_register(void)
{
    return ieee80211_rate_control_register(&ssv_rate_ops);
}
void ssv6xxx_rate_control_unregister(void)
{
    ieee80211_rate_control_unregister(&ssv_rate_ops);
}
void ssv6xxx_rc_rx_data_handler(struct ieee80211_hw *hw, struct sk_buff *skb, u32 rate_index)
{
    struct ssv_softc *sc = hw->priv;
    struct ieee80211_sta *sta;
    struct ssv_sta_priv_data *ssv_sta_priv;
    sta = ssv6xxx_find_sta_by_rx_skb(sc, skb);
    if(sta == NULL)
    {
        return;
    }
    ssv_sta_priv = (struct ssv_sta_priv_data *)sta->drv_priv;
    ssv_sta_priv->rx_data_rate = rate_index;
}
