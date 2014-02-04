
/******************************************************************************
 **
 ** Copyright(c) 2009-2010  Realtek Corporation.
 **
 ** This program is free software; you can redistribute it and/or modify it
 ** under the terms of version 2 of the GNU General Public License as
 ** published by the Free Software Foundation.
 **
 ** This program is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along with
 ** this program; if not, write to the Free Software Foundation, Inc.,
 ** 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 **
 ** The full GNU General Public License is included in this distribution in the
 ** file called LICENSE.
 **
 ** Contact Information:
 ** wlanfae <wlanfae@realtek.com>
 ** Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 ** Hsinchu 300, Taiwan.
 ** Larry Finger <Larry.Finger@lwfinger.net>
 **
 ******************************************************************************/

#ifndef __RTL8821AE_BTC_H__
#define __RTL8821AE_BTC_H__

#include "../wifi.h"
#include "hal_bt_coexist.h"

struct bt_coexist_c2h_info {
	u8 no_parse_c2h;
	u8 has_c2h;
};

struct btdm_8821ae {
	bool b_all_off;
	bool b_agc_table_en;
	bool b_adc_back_off_on;
	bool b2_ant_hid_en;
	bool b_low_penalty_rate_adaptive;
	bool b_rf_rx_lpf_shrink;
	bool b_reject_aggre_pkt;
	bool b_tra_tdma_on;
	u8 tra_tdma_nav;
	u8 tra_tdma_ant;
	bool b_tdma_on;
	u8 tdma_ant;
	u8 tdma_nav;
	u8 tdma_dac_swing;
	u8 fw_dac_swing_lvl;
	bool b_ps_tdma_on;
	u8 ps_tdma_byte[5];
	bool b_pta_on;
	u32 val_0x6c0;
	u32 val_0x6c8;
	u32 val_0x6cc;
	bool b_sw_dac_swing_on;
	u32 sw_dac_swing_lvl;
	u32 wlan_act_hi;
	u32 wlan_act_lo;
	u32 bt_retry_index;
	bool b_dec_bt_pwr;
	bool b_ignore_wlan_act;
};

struct bt_coexist_8821ae {
	u32 high_priority_tx;
	u32 high_priority_rx;
	u32 low_priority_tx;
	u32 low_priority_rx;
	u8 c2h_bt_info;
	bool b_c2h_bt_info_req_sent;
	bool b_c2h_bt_inquiry_page;
	u32 bt_inq_page_start_time;
	u8 bt_retry_cnt;
	u8 c2h_bt_info_original;
	u8 bt_inquiry_page_cnt;
	struct btdm_8821ae btdm;
};

#endif
