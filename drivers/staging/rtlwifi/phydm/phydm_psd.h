/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __PHYDMPSD_H__
#define __PHYDMPSD_H__

/*#define PSD_VERSION	"1.0"*/ /*2016.09.22  Dino*/
#define PSD_VERSION "1.1" /*2016.10.07  Dino, Add Option for PSD Tone index
			   *Selection
			   */

#define STOP_TRX_SUCCESS 1
#define STOP_TRX_FAIL 0

struct psd_info {
	u8 psd_in_progress;
	u32 psd_reg;
	u32 psd_report_reg;
	u8 psd_pwr_common_offset;
	u16 sw_avg_time;
	u16 fft_smp_point;
	u32 initial_gain_backup;
	u32 rf_0x18_bkp;
	u16 psd_fc_channel;
	u32 psd_bw_rf_reg;
	u8 psd_result[128];
	u8 noise_k_en;
};

u32 phydm_get_psd_data(void *dm_void, u32 psd_tone_idx, u32 igi);

void phydm_psd_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		     u32 *_out_len, u32 input_num);

void phydm_psd(void *dm_void, u32 igi, u16 start_point, u16 stop_point);

void phydm_psd_para_setting(void *dm_void, u8 sw_avg_time, u8 hw_avg_time,
			    u8 i_q_setting, u16 fft_smp_point, u8 ant_sel,
			    u8 psd_input, u8 channel, u8 noise_k_en);

void phydm_psd_init(void *dm_void);

u8 phydm_get_psd_result_table(void *dm_void, int index);

#endif
