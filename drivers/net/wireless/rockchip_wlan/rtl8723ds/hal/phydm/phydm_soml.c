/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

/*************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_ADAPTIVE_SOML

void phydm_dynamicsoftmletting(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 ret_val;

#if (RTL8822B_SUPPORT == 1)
	if (!*dm->mp_mode) {
		if (dm->support_ic_type & ODM_RTL8822B) {
			if (!dm->is_linked | dm->iot_table.is_linked_cmw500)
				return;

			if (dm->bsomlenabled) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "PHYDM_DynamicSoftMLSetting(): SoML has been enable, skip dynamic SoML switch\n");
				return;
			}

			ret_val = odm_get_bb_reg(dm, R_0xf8c, MASKBYTE0);
			PHYDM_DBG(dm, ODM_COMP_API,
				  "PHYDM_DynamicSoftMLSetting(): Read 0xF8C = 0x%08X\n",
				  ret_val);

			if (ret_val < 0x16) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "PHYDM_DynamicSoftMLSetting(): 0xF8C(== 0x%08X) < 0x16, enable SoML\n",
					  ret_val);
				phydm_somlrxhp_setting(dm, true);
#if 0
			/*odm_set_bb_reg(dm, R_0x19a8, MASKDWORD, 0xc10a0000);*/
#endif
				dm->bsomlenabled = true;
			}
		}
	}
#endif
}

void phydm_soml_on_off(void *dm_void, u8 swch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	if (swch == SOML_ON) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "(( Turn on )) SOML\n");

		if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F))
			odm_set_bb_reg(dm, R_0x998, BIT(6), swch);
#if (RTL8822B_SUPPORT == 1)
		else if (dm->support_ic_type == ODM_RTL8822B)
			phydm_somlrxhp_setting(dm, true);
#endif

	} else if (swch == SOML_OFF) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "(( Turn off )) SOML\n");

		if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F))
			odm_set_bb_reg(dm, R_0x998, BIT(6), swch);
#if (RTL8822B_SUPPORT == 1)
		else if (dm->support_ic_type == ODM_RTL8822B)
			phydm_somlrxhp_setting(dm, false);
#endif
	}
	soml_tab->soml_on_off = swch;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_adaptive_soml_callback(struct phydm_timer_list *timer)
{
	void *adapter = (void *)timer->Adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	#if USE_WORKITEM
	odm_schedule_work_item(&soml_tab->phydm_adaptive_soml_workitem);
	#else
	{
#if 0
		/*@dbg_print("%s\n",__func__);*/
#endif
		phydm_adsl(dm);
	}
	#endif
	#else
	odm_schedule_work_item(&soml_tab->phydm_adaptive_soml_workitem);
	#endif
}

void phydm_adaptive_soml_workitem_callback(void *context)
{
#ifdef CONFIG_ADAPTIVE_SOML
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

#if 0
	/*@dbg_print("%s\n",__func__);*/
#endif
	phydm_adsl(dm);
#endif
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
void phydm_adaptive_soml_callback(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *padapter = dm->adapter;

	if (*dm->is_net_closed == true)
		return;
	if (dm->support_interface == ODM_ITRF_PCIE)
		phydm_adsl(dm);
	else {
		/* @Can't do I/O in timer callback*/
		phydm_run_in_thread_cmd(dm,
					phydm_adaptive_soml_workitem_callback,
					dm);
	}
}

void phydm_adaptive_soml_workitem_callback(void *context)
{
	struct dm_struct *dm = (void *)context;

#if 0
	/*@dbg_print("%s\n",__func__);*/
#endif
	phydm_adsl(dm);
}

#else
void phydm_adaptive_soml_callback(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ADPTV_SOML, "******SOML_Callback******\n");
	phydm_adsl(dm);
}
#endif

void phydm_rx_rate_for_soml(void *dm_void, void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	u8 data_rate;

	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	data_rate = (pktinfo->data_rate & 0x7f);

	if (pktinfo->data_rate >= ODM_RATEMCS0 &&
	    pktinfo->data_rate <= ODM_RATEMCS31)
		soml_tab->ht_cnt[data_rate - ODM_RATEMCS0]++;
	else if ((pktinfo->data_rate >= ODM_RATEVHTSS1MCS0) &&
		 (pktinfo->data_rate <= ODM_RATEVHTSS4MCS9))
		soml_tab->vht_cnt[data_rate - ODM_RATEVHTSS1MCS0]++;
}

void phydm_rx_qam_for_soml(void *dm_void, void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	u8 date_rate;

	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	date_rate = (pktinfo->data_rate & 0x7f);
	if (soml_tab->soml_state_cnt < (soml_tab->soml_train_num << 1)) {
		if (soml_tab->soml_on_off == SOML_ON) {
			return;
		} else if (soml_tab->soml_on_off == SOML_OFF) {
			if (date_rate >= ODM_RATEMCS8 &&
			    date_rate <= ODM_RATEMCS10)
				soml_tab->num_ht_qam[BPSK_QPSK]++;

			else if ((date_rate >= ODM_RATEMCS11) &&
				 (date_rate <= ODM_RATEMCS12))
				soml_tab->num_ht_qam[QAM16]++;

			else if ((date_rate >= ODM_RATEMCS13) &&
				 (date_rate <= ODM_RATEMCS15))
				soml_tab->num_ht_qam[QAM64]++;

			else if ((date_rate >= ODM_RATEVHTSS2MCS0) &&
				 (date_rate <= ODM_RATEVHTSS2MCS2))
				soml_tab->num_vht_qam[BPSK_QPSK]++;

			else if ((date_rate >= ODM_RATEVHTSS2MCS3) &&
				 (date_rate <= ODM_RATEVHTSS2MCS4))
				soml_tab->num_vht_qam[QAM16]++;

			else if ((date_rate >= ODM_RATEVHTSS2MCS5) &&
				 (date_rate <= ODM_RATEVHTSS2MCS5))
				soml_tab->num_vht_qam[QAM64]++;

			else if ((date_rate >= ODM_RATEVHTSS2MCS8) &&
				 (date_rate <= ODM_RATEVHTSS2MCS9))
				soml_tab->num_vht_qam[QAM256]++;
		}
	}
}

void phydm_soml_reset_rx_rate(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 order;

	for (order = 0; order < HT_RATE_IDX; order++) {
		soml_tab->ht_cnt[order] = 0;
		soml_tab->pre_ht_cnt[order] = 0;
		soml_tab->ht_cnt_on[order] = 0;
		soml_tab->ht_cnt_off[order] = 0;
		soml_tab->ht_crc_ok_cnt_on[order] = 0;
		soml_tab->ht_crc_fail_cnt_on[order] = 0;
		soml_tab->ht_crc_ok_cnt_off[order] = 0;
		soml_tab->ht_crc_fail_cnt_off[order] = 0;
	}

	for (order = 0; order < VHT_RATE_IDX; order++) {
		soml_tab->vht_cnt[order] = 0;
		soml_tab->pre_vht_cnt[order] = 0;
		soml_tab->vht_cnt_on[order] = 0;
		soml_tab->vht_cnt_off[order] = 0;
		soml_tab->vht_crc_ok_cnt_on[order] = 0;
		soml_tab->vht_crc_fail_cnt_on[order] = 0;
		soml_tab->vht_crc_ok_cnt_off[order] = 0;
		soml_tab->vht_crc_fail_cnt_off[order] = 0;
	}
}

void phydm_soml_reset_qam(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 order;

	for (order = 0; order < HT_ORDER_TYPE; order++)
		soml_tab->num_ht_qam[order] = 0;

	for (order = 0; order < VHT_ORDER_TYPE; order++)
		soml_tab->num_vht_qam[order] = 0;
}

void phydm_soml_cfo_process(void *dm_void, s32 *diff_a, s32 *diff_b)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32, value32_1, value32_2, value32_3;
	s32 cfo_acq_a, cfo_acq_b, cfo_end_a, cfo_end_b;

	value32 = odm_get_bb_reg(dm, R_0xd10, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, R_0xd14, MASKDWORD);
	value32_2 = odm_get_bb_reg(dm, R_0xd50, MASKDWORD);
	value32_3 = odm_get_bb_reg(dm, R_0xd54, MASKDWORD);

	cfo_acq_a = (s32)((value32 & 0x1fff0000) >> 16);
	cfo_end_a = (s32)((value32_1 & 0x1fff0000) >> 16);
	cfo_acq_b = (s32)((value32_2 & 0x1fff0000) >> 16);
	cfo_end_b = (s32)((value32_3 & 0x1fff0000) >> 16);

	*diff_a = ((cfo_acq_a >= cfo_end_a) ? (cfo_acq_a - cfo_end_a) :
		  (cfo_end_a - cfo_acq_a));
	*diff_b = ((cfo_acq_b >= cfo_end_b) ? (cfo_acq_b - cfo_end_b) :
		  (cfo_end_b - cfo_acq_b));

	*diff_a = ((*diff_a * 312) + (*diff_a >> 1)) >> 12; /* @312.5/2^12 */
	*diff_b = ((*diff_b * 312) + (*diff_b >> 1)) >> 12; /* @312.5/2^12 */
}

void phydm_soml_debug(void *dm_void, char input[][16], u32 *_used,
		      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 dm_value[10] = {0};
	u8 i = 0, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &dm_value[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if (dm_value[0] == 1) { /*Turn on/off SOML*/
		soml_tab->soml_select = (u8)dm_value[1];

	} else if (dm_value[0] == 2) { /*training number for SOML*/

		soml_tab->soml_train_num = (u8)dm_value[1];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_train_num = ((%d))\n",
			 soml_tab->soml_train_num);
	} else if (dm_value[0] == 3) { /*training interval for SOML*/

		soml_tab->soml_intvl = (u8)dm_value[1];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_intvl = ((%d))\n", soml_tab->soml_intvl);
	} else if (dm_value[0] == 4) { /*@function period for SOML*/

		soml_tab->soml_period = (u8)dm_value[1];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_period = ((%d))\n", soml_tab->soml_period);
	} else if (dm_value[0] == 5) { /*@delay_time for SOML*/

		soml_tab->soml_delay_time = (u8)dm_value[1];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_delay_time = ((%d))\n",
			 soml_tab->soml_delay_time);
	} else if (dm_value[0] == 6) { /* @for SOML Rx QAM distribution th*/
		if (dm_value[1] == 256) {
			soml_tab->qam256_dist_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "qam256_dist_th = ((%d))\n",
				 soml_tab->qam256_dist_th);
		} else if (dm_value[1] == 64) {
			soml_tab->qam64_dist_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "qam64_dist_th = ((%d))\n",
				 soml_tab->qam64_dist_th);
		} else if (dm_value[1] == 16) {
			soml_tab->qam16_dist_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "qam16_dist_th = ((%d))\n",
				 soml_tab->qam16_dist_th);
		} else if (dm_value[1] == 4) {
			soml_tab->bpsk_qpsk_dist_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "bpsk_qpsk_dist_th = ((%d))\n",
				 soml_tab->bpsk_qpsk_dist_th);
		}
	} else if (dm_value[0] == 7) { /* @for SOML cfo th*/
		if (dm_value[1] == 256) {
			soml_tab->cfo_qam256_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "cfo_qam256_th = ((%d KHz))\n",
				 soml_tab->cfo_qam256_th);
		} else if (dm_value[1] == 64) {
			soml_tab->cfo_qam64_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "cfo_qam64_th = ((%d KHz))\n",
				 soml_tab->cfo_qam64_th);
		} else if (dm_value[1] == 16) {
			soml_tab->cfo_qam16_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "cfo_qam16_th = ((%d KHz))\n",
				 soml_tab->cfo_qam16_th);
		} else if (dm_value[1] == 4) {
			soml_tab->cfo_qpsk_th = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "cfo_qpsk_th = ((%d KHz))\n",
				 soml_tab->cfo_qpsk_th);
		}
	} else if (dm_value[0] == 100) {
		/*show parameters*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_select = ((%d))\n", soml_tab->soml_select);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_train_num = ((%d))\n",
			 soml_tab->soml_train_num);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_intvl = ((%d))\n", soml_tab->soml_intvl);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_period = ((%d))\n", soml_tab->soml_period);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "soml_delay_time = ((%d))\n\n",
			 soml_tab->soml_delay_time);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "qam256_dist_th = ((%d)),  qam64_dist_th = ((%d)), ",
			 soml_tab->qam256_dist_th,
			 soml_tab->qam64_dist_th);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "qam16_dist_th = ((%d)),  bpsk_qpsk_dist_th = ((%d))\n",
			 soml_tab->qam16_dist_th,
			 soml_tab->bpsk_qpsk_dist_th);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "cfo_qam256_th = ((%d KHz)),  cfo_qam64_th = ((%d KHz)), ",
			 soml_tab->cfo_qam256_th,
			 soml_tab->cfo_qam64_th);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "cfo_qam16_th = ((%d KHz)),  cfo_qpsk_th  = ((%d KHz))\n",
			 soml_tab->cfo_qam16_th,
			 soml_tab->cfo_qpsk_th);
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_soml_stats_ht_on(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 i, mcs0;
	u16 num_bytes_diff, num_rate_diff;

	mcs0 = ODM_RATEMCS0;
	for (i = mcs0; i <= ODM_RATEMCS15; i++) {
		num_rate_diff = soml_tab->ht_cnt[i - mcs0] -
				soml_tab->pre_ht_cnt[i - mcs0];
		soml_tab->ht_cnt_on[i - mcs0] += num_rate_diff;
		soml_tab->pre_ht_cnt[i - mcs0] = soml_tab->ht_cnt[i - mcs0];
		num_bytes_diff = soml_tab->ht_byte[i - mcs0] -
				 soml_tab->pre_ht_byte[i - mcs0];
		soml_tab->ht_byte_on[i - mcs0] += num_bytes_diff;
		soml_tab->pre_ht_byte[i - mcs0] = soml_tab->ht_byte[i - mcs0];
	}
}

void phydm_soml_stats_ht_off(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 i, mcs0;
	u16 num_bytes_diff, num_rate_diff;

	mcs0 = ODM_RATEMCS0;
	for (i = mcs0; i <= ODM_RATEMCS15; i++) {
		num_rate_diff = soml_tab->ht_cnt[i - mcs0] -
				soml_tab->pre_ht_cnt[i - mcs0];
		soml_tab->ht_cnt_off[i - mcs0] += num_rate_diff;
		soml_tab->pre_ht_cnt[i - mcs0] = soml_tab->ht_cnt[i - mcs0];
		num_bytes_diff = soml_tab->ht_byte[i - mcs0] -
				 soml_tab->pre_ht_byte[i - mcs0];
		soml_tab->ht_byte_off[i - mcs0] += num_bytes_diff;
		soml_tab->pre_ht_byte[i - mcs0] = soml_tab->ht_byte[i - mcs0];
	}
}

void phydm_soml_stats_vht_on(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 j, vht0;
	u16 num_bytes_diff, num_rate_diff;

	vht0 = ODM_RATEVHTSS1MCS0;
	for (j = vht0; j <= ODM_RATEVHTSS2MCS9; j++) {
		num_rate_diff = soml_tab->vht_cnt[j - vht0] -
				soml_tab->pre_vht_cnt[j - vht0];
		soml_tab->vht_cnt_on[j - vht0] += num_rate_diff;
		soml_tab->pre_vht_cnt[j - vht0] = soml_tab->vht_cnt[j - vht0];
		num_bytes_diff = soml_tab->vht_byte[j - vht0] -
				 soml_tab->pre_vht_byte[j - vht0];
		soml_tab->vht_byte_on[j - vht0] += num_bytes_diff;
		soml_tab->pre_vht_byte[j - vht0] = soml_tab->vht_byte[j - vht0];
	}
}

void phydm_soml_stats_vht_off(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 j, vht0;
	u16 num_bytes_diff, num_rate_diff;

	vht0 = ODM_RATEVHTSS1MCS0;
	for (j = vht0; j <= ODM_RATEVHTSS2MCS9; j++) {
		num_rate_diff = soml_tab->vht_cnt[j - vht0] -
				soml_tab->pre_vht_cnt[j - vht0];
		soml_tab->vht_cnt_off[j - vht0] += num_rate_diff;
		soml_tab->pre_vht_cnt[j - vht0] = soml_tab->vht_cnt[j - vht0];
		num_bytes_diff = soml_tab->vht_byte[j - vht0] -
				 soml_tab->pre_vht_byte[j - vht0];
		soml_tab->vht_byte_off[j - vht0] += num_bytes_diff;
		soml_tab->pre_vht_byte[j - vht0] = soml_tab->vht_byte[j - vht0];
	}
}

void phydm_soml_statistics(void *dm_void, u8 on_off_state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	if (on_off_state == SOML_ON) {
		if (*dm->channel <= 14)
			phydm_soml_stats_ht_on(dm);
		if (dm->support_ic_type == ODM_RTL8822B)
			phydm_soml_stats_vht_on(dm);
	} else if (on_off_state == SOML_OFF) {
		if (*dm->channel <= 14)
			phydm_soml_stats_ht_off(dm);
		if (dm->support_ic_type == ODM_RTL8822B)
			phydm_soml_stats_vht_off(dm);
	}
}

void phydm_adsl_init_state(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	u8 next_on_off;
	u16 ht_reset[HT_RATE_IDX] = {0}, vht_reset[VHT_RATE_IDX] = {0};
	u8 size = sizeof(ht_reset[0]);

	phydm_soml_reset_rx_rate(dm);
	odm_move_memory(dm, soml_tab->ht_byte, ht_reset,
			HT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->ht_byte_on, ht_reset,
			HT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->ht_byte_off, ht_reset,
			HT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->vht_byte, vht_reset,
			VHT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->vht_byte_on, vht_reset,
			VHT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->vht_byte_off, vht_reset,
			VHT_RATE_IDX * size);
	if (dm->support_ic_type == ODM_RTL8822B) {
		soml_tab->cfo_cnt++;
		phydm_soml_cfo_process(dm,
				       &soml_tab->cfo_diff_a,
				       &soml_tab->cfo_diff_b);
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ (%d) cfo_diff_a = %d KHz; cfo_diff_b = %d KHz ]\n",
			  soml_tab->cfo_cnt, soml_tab->cfo_diff_a,
			  soml_tab->cfo_diff_b);
		soml_tab->cfo_diff_sum_a += soml_tab->cfo_diff_a;
		soml_tab->cfo_diff_sum_b += soml_tab->cfo_diff_b;
	}

	soml_tab->is_soml_method_enable = 1;
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	odm_set_mac_reg(dm, R_0x608, BIT(8), 1);
	/*RCR accepts CRC32-Error packets*/
	#endif
	soml_tab->get_stats = false;
	soml_tab->soml_state_cnt++;
	next_on_off = (soml_tab->soml_on_off == SOML_ON) ? SOML_ON : SOML_OFF;
	phydm_soml_on_off(dm, next_on_off);
	odm_set_timer(dm, &soml_tab->phydm_adaptive_soml_timer,
		      soml_tab->soml_delay_time); /*@ms*/
}

void phydm_adsl_odd_state(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u16 ht_reset[HT_RATE_IDX] = {0}, vht_reset[VHT_RATE_IDX] = {0};
	u8 size = sizeof(ht_reset[0]);

	soml_tab->get_stats = true;
	soml_tab->soml_state_cnt++;
	odm_move_memory(dm, soml_tab->pre_ht_cnt, soml_tab->ht_cnt,
			HT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->pre_vht_cnt, soml_tab->vht_cnt,
			VHT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->pre_ht_byte, soml_tab->ht_byte,
			HT_RATE_IDX * size);
	odm_move_memory(dm, soml_tab->pre_vht_byte, soml_tab->vht_byte,
			VHT_RATE_IDX * size);

	if (dm->support_ic_type == ODM_RTL8822B) {
		soml_tab->cfo_cnt++;
		phydm_soml_cfo_process(dm,
				       &soml_tab->cfo_diff_a,
				       &soml_tab->cfo_diff_b);
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ (%d) cfo_diff_a = %d KHz; cfo_diff_b = %d KHz ]\n",
			  soml_tab->cfo_cnt, soml_tab->cfo_diff_a,
			  soml_tab->cfo_diff_b);
		soml_tab->cfo_diff_sum_a += soml_tab->cfo_diff_a;
		soml_tab->cfo_diff_sum_b += soml_tab->cfo_diff_b;
	}
	odm_set_timer(dm, &soml_tab->phydm_adaptive_soml_timer,
		      soml_tab->soml_intvl); /*@ms*/
}

void phydm_adsl_even_state(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 next_on_off;

	soml_tab->get_stats = false;
	if (dm->support_ic_type == ODM_RTL8822B) {
		soml_tab->cfo_cnt++;
		phydm_soml_cfo_process(dm,
				       &soml_tab->cfo_diff_a,
				       &soml_tab->cfo_diff_b);
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ (%d) cfo_diff_a = %d KHz; cfo_diff_b = %d KHz ]\n",
			  soml_tab->cfo_cnt, soml_tab->cfo_diff_a,
			  soml_tab->cfo_diff_b);
		soml_tab->cfo_diff_sum_a += soml_tab->cfo_diff_a;
		soml_tab->cfo_diff_sum_b += soml_tab->cfo_diff_b;
	}
	soml_tab->soml_state_cnt++;
	phydm_soml_statistics(dm, soml_tab->soml_on_off);
	next_on_off = (soml_tab->soml_on_off == SOML_ON) ? SOML_OFF : SOML_ON;
	phydm_soml_on_off(dm, next_on_off);
	odm_set_timer(dm, &soml_tab->phydm_adaptive_soml_timer,
		      soml_tab->soml_delay_time); /*@ms*/
}

void phydm_adsl_decision_state(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	boolean on_above = false, off_above = false;
	u8 i, max_idx_on = 0, max_idx_off = 0;
	u8 next_on_off = soml_tab->soml_last_state;
	u8 mcs0 = ODM_RATEMCS0, vht0 = ODM_RATEVHTSS1MCS0;
	u8 crc_taget = soml_tab->soml_last_state;
	u8 rate_num = 1, ss_shift = 0;
	u16 ht_ok_max_on = 0, ht_fail_max_on = 0, utility_on = 0;
	u16 ht_ok_max_off = 0, ht_fail_max_off = 0, utility_off = 0;
	u16 vht_ok_max_on = 0, vht_fail_max_on = 0;
	u16 vht_ok_max_off = 0, vht_fail_max_off = 0;
	u16 num_total_qam = 0;
	u16 cnt_max_on = 0, cnt_max_off = 0;
	u32 ht_total_cnt_on = 0, ht_total_cnt_off = 0;
	u32 total_ht_rate_on = 0, total_ht_rate_off = 0;
	u32 vht_total_cnt_on = 0, vht_total_cnt_off = 0;
	u32 total_vht_rate_on = 0, total_vht_rate_off = 0;
	u32 rate_per_pkt_on = 0, rate_per_pkt_off = 0;
	s32 cfo_diff_avg_a, cfo_diff_avg_b;
	u16 vht_phy_rate_table[] = {
		/*@20M*/
		6, 13, 19, 26, 39, 52, 58, 65, 78, 90, /*@1SS MCS0~9*/
		13, 26, 39, 52, 78, 104, 117, 130, 156, 180 /*@2SSMCS0~9*/
	};

	if (dm->support_ic_type & ODM_IC_1SS)
		rate_num = 1;
	#ifdef PHYDM_COMPILE_ABOVE_2SS
	else if (dm->support_ic_type & ODM_IC_2SS)
		rate_num = 2;
	#endif
	#ifdef PHYDM_COMPILE_ABOVE_3SS
	else if (dm->support_ic_type & ODM_IC_3SS)
		rate_num = 3;
	#endif
	#ifdef PHYDM_COMPILE_ABOVE_4SS
	else if (dm->support_ic_type & ODM_IC_4SS)
		rate_num = 4;
	#endif
	else
		pr_debug("%s: mismatch IC type %x\n", __func__,
			 dm->support_ic_type);
	soml_tab->get_stats = false;
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	odm_set_mac_reg(dm, R_0x608, BIT(8), 0);
	/* NOT Accept CRC32 Error packets. */
	#endif
	PHYDM_DBG(dm, DBG_ADPTV_SOML, "[Decisoin state ]\n");
	phydm_soml_statistics(dm, soml_tab->soml_on_off);
	if (*dm->channel <= 14) {
		/* @[Search 1st and 2nd rate by counter] */
		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_cnt_on  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_cnt_on[ss_shift + 0],
				  soml_tab->ht_cnt_on[ss_shift + 1],
				  soml_tab->ht_cnt_on[ss_shift + 2],
				  soml_tab->ht_cnt_on[ss_shift + 3],
				  soml_tab->ht_cnt_on[ss_shift + 4],
				  soml_tab->ht_cnt_on[ss_shift + 5],
				  soml_tab->ht_cnt_on[ss_shift + 6],
				  soml_tab->ht_cnt_on[ss_shift + 7]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_cnt_off  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_cnt_off[ss_shift + 0],
				  soml_tab->ht_cnt_off[ss_shift + 1],
				  soml_tab->ht_cnt_off[ss_shift + 2],
				  soml_tab->ht_cnt_off[ss_shift + 3],
				  soml_tab->ht_cnt_off[ss_shift + 4],
				  soml_tab->ht_cnt_off[ss_shift + 5],
				  soml_tab->ht_cnt_off[ss_shift + 6],
				  soml_tab->ht_cnt_off[ss_shift + 7]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_crc_ok_cnt_on  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 0],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 1],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 2],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 3],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 4],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 5],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 6],
				  soml_tab->ht_crc_ok_cnt_on[ss_shift + 7]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_crc_fail_cnt_on  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 0],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 1],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 2],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 3],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 4],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 5],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 6],
				  soml_tab->ht_crc_fail_cnt_on[ss_shift + 7]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_crc_ok_cnt_off  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 0],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 1],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 2],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 3],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 4],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 5],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 6],
				  soml_tab->ht_crc_ok_cnt_off[ss_shift + 7]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = (i << 3);
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*ht_crc_fail_cnt_off  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_shift), (ss_shift + 7),
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 0],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 1],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 2],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 3],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 4],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 5],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 6],
				  soml_tab->ht_crc_fail_cnt_off[ss_shift + 7]);
		}
		for (i = ODM_RATEMCS0; i <= ODM_RATEMCS15; i++) {
			ht_total_cnt_on += soml_tab->ht_cnt_on[i - mcs0];
			ht_total_cnt_off += soml_tab->ht_cnt_off[i - mcs0];
			total_ht_rate_on += (soml_tab->ht_cnt_on[i - mcs0] *
					    phy_rate_table[i]);
			total_ht_rate_off += (soml_tab->ht_cnt_off[i - mcs0] *
					     phy_rate_table[i]);
			if (soml_tab->ht_cnt_on[i - mcs0] > cnt_max_on) {
				cnt_max_on = soml_tab->ht_cnt_on[i - mcs0];
				max_idx_on = i - mcs0;
			}

			if (soml_tab->ht_cnt_off[i - mcs0] > cnt_max_off) {
				cnt_max_off = soml_tab->ht_cnt_off[i - mcs0];
				max_idx_off = i - mcs0;
			}
		}
		total_ht_rate_on = total_ht_rate_on << 3;
		total_ht_rate_off = total_ht_rate_off << 3;
		rate_per_pkt_on = (ht_total_cnt_on != 0) ?
				  (total_ht_rate_on / ht_total_cnt_on) : 0;
		rate_per_pkt_off = (ht_total_cnt_off != 0) ?
				   (total_ht_rate_off / ht_total_cnt_off) : 0;
		#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		ht_ok_max_on = soml_tab->ht_crc_ok_cnt_on[max_idx_on];
		ht_fail_max_on = soml_tab->ht_crc_fail_cnt_on[max_idx_on];
		ht_ok_max_off = soml_tab->ht_crc_ok_cnt_off[max_idx_off];
		ht_fail_max_off = soml_tab->ht_crc_fail_cnt_off[max_idx_off];

		if (ht_fail_max_on == 0)
			ht_fail_max_on = 1;

		if (ht_fail_max_off == 0)
			ht_fail_max_off = 1;

		if (ht_ok_max_on > ht_fail_max_on)
			on_above = true;

		if (ht_ok_max_off > ht_fail_max_off)
			off_above = true;

		if (on_above && !off_above) {
			crc_taget = SOML_ON;
		} else if (!on_above && off_above) {
			crc_taget = SOML_OFF;
		} else if (on_above && off_above) {
			utility_on = (ht_ok_max_on << 7) / ht_fail_max_on;
			utility_off = (ht_ok_max_off << 7) / ht_fail_max_off;
			crc_taget = (utility_on == utility_off) ?
				    (soml_tab->soml_last_state) :
				    ((utility_on > utility_off) ? SOML_ON :
				    SOML_OFF);

		} else if (!on_above && !off_above) {
			if (ht_ok_max_on == 0)
				ht_ok_max_on = 1;
			if (ht_ok_max_off == 0)
				ht_ok_max_off = 1;
			utility_on = (ht_fail_max_on << 7) / ht_ok_max_on;
			utility_off = (ht_fail_max_off << 7) / ht_ok_max_off;
			crc_taget = (utility_on == utility_off) ?
				    (soml_tab->soml_last_state) :
				    ((utility_on < utility_off) ? SOML_ON :
				    SOML_OFF);
		}
		#endif
	} else if (dm->support_ic_type == ODM_RTL8822B) {
		cfo_diff_avg_a = soml_tab->cfo_diff_sum_a / soml_tab->cfo_cnt;
		cfo_diff_avg_b = soml_tab->cfo_diff_sum_b / soml_tab->cfo_cnt;
		soml_tab->cfo_diff_avg_a = (soml_tab->cfo_cnt != 0) ?
					   cfo_diff_avg_a : 0;
		soml_tab->cfo_diff_avg_b = (soml_tab->cfo_cnt != 0) ?
					   cfo_diff_avg_b : 0;
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ cfo_diff_avg_a = %d KHz; cfo_diff_avg_b = %d KHz]\n",
			  soml_tab->cfo_diff_avg_a,
			  soml_tab->cfo_diff_avg_b);
		for (i = 0; i < VHT_ORDER_TYPE; i++)
			num_total_qam += soml_tab->num_vht_qam[i];

		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ ((2SS)) BPSK_QPSK_count = %d ; 16QAM_count = %d ; 64QAM_count = %d ; 256QAM_count = %d ; num_total_qam = %d]\n",
			  soml_tab->num_vht_qam[BPSK_QPSK],
			  soml_tab->num_vht_qam[QAM16],
			  soml_tab->num_vht_qam[QAM64],
			  soml_tab->num_vht_qam[QAM256],
			  num_total_qam);
		if (((soml_tab->num_vht_qam[QAM256] * 100) >
		    (num_total_qam * soml_tab->qam256_dist_th)) &&
		    cfo_diff_avg_a > soml_tab->cfo_qam256_th &&
		    cfo_diff_avg_b > soml_tab->cfo_qam256_th) {
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  QAM256_ratio > %d ; cfo_diff_avg_a > %d KHz ==> SOML_OFF]\n",
				  soml_tab->qam256_dist_th,
				  soml_tab->cfo_qam256_th);
			PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Final decisoin ] : ");
			phydm_soml_on_off(dm, SOML_OFF);
			return;
		} else if (((soml_tab->num_vht_qam[QAM64] * 100) >
			   (num_total_qam * soml_tab->qam64_dist_th)) &&
			   (cfo_diff_avg_a > soml_tab->cfo_qam64_th) &&
			   (cfo_diff_avg_b > soml_tab->cfo_qam64_th)) {
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  QAM64_ratio > %d ; cfo_diff_avg_a > %d KHz ==> SOML_OFF]\n",
				  soml_tab->qam64_dist_th,
				  soml_tab->cfo_qam64_th);
			PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Final decisoin ] : ");
			phydm_soml_on_off(dm, SOML_OFF);
			return;
		} else if (((soml_tab->num_vht_qam[QAM16] * 100) >
			   (num_total_qam * soml_tab->qam16_dist_th)) &&
			   (cfo_diff_avg_a > soml_tab->cfo_qam16_th) &&
			   (cfo_diff_avg_b > soml_tab->cfo_qam16_th)) {
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  QAM16_ratio > %d ; cfo_diff_avg_a > %d KHz ==> SOML_OFF]\n",
				  soml_tab->qam16_dist_th,
				  soml_tab->cfo_qam16_th);
			PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Final decisoin ] : ");
			phydm_soml_on_off(dm, SOML_OFF);
			return;
		} else if (((soml_tab->num_vht_qam[BPSK_QPSK] * 100) >
			   (num_total_qam * soml_tab->bpsk_qpsk_dist_th)) &&
			   (cfo_diff_avg_a > soml_tab->cfo_qpsk_th) &&
			   (cfo_diff_avg_b > soml_tab->cfo_qpsk_th)) {
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  BPSK_QPSK_ratio > %d ; cfo_diff_avg_a > %d KHz ==> SOML_OFF]\n",
				  soml_tab->bpsk_qpsk_dist_th,
				  soml_tab->cfo_qpsk_th);
			PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Final decisoin ] : ");
			phydm_soml_on_off(dm, SOML_OFF);
			return;
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  vht_cnt_on  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d} ]\n",
				  (i + 1),
				  soml_tab->vht_cnt_on[ss_shift + 0],
				  soml_tab->vht_cnt_on[ss_shift + 1],
				  soml_tab->vht_cnt_on[ss_shift + 2],
				  soml_tab->vht_cnt_on[ss_shift + 3],
				  soml_tab->vht_cnt_on[ss_shift + 4],
				  soml_tab->vht_cnt_on[ss_shift + 5],
				  soml_tab->vht_cnt_on[ss_shift + 6],
				  soml_tab->vht_cnt_on[ss_shift + 7],
				  soml_tab->vht_cnt_on[ss_shift + 8],
				  soml_tab->vht_cnt_on[ss_shift + 9]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "[  vht_cnt_off  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d} ]\n",
				  (i + 1),
				  soml_tab->vht_cnt_off[ss_shift + 0],
				  soml_tab->vht_cnt_off[ss_shift + 1],
				  soml_tab->vht_cnt_off[ss_shift + 2],
				  soml_tab->vht_cnt_off[ss_shift + 3],
				  soml_tab->vht_cnt_off[ss_shift + 4],
				  soml_tab->vht_cnt_off[ss_shift + 5],
				  soml_tab->vht_cnt_off[ss_shift + 6],
				  soml_tab->vht_cnt_off[ss_shift + 7],
				  soml_tab->vht_cnt_off[ss_shift + 8],
				  soml_tab->vht_cnt_off[ss_shift + 9]);
		}

		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*vht_crc_ok_cnt_on  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (i + 1),
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 0],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 1],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 2],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 3],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 4],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 5],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 6],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 7],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 8],
				  soml_tab->vht_crc_ok_cnt_on[ss_shift + 9]);
		}
		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*vht_crc_fail_cnt_on  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (i + 1),
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 0],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 1],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 2],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 3],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 4],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 5],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 6],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 7],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 8],
				  soml_tab->vht_crc_fail_cnt_on[ss_shift + 9]);
		}
		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*vht_crc_ok_cnt_off  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (i + 1),
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 0],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 1],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 2],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 3],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 4],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 5],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 6],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 7],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 8],
				  soml_tab->vht_crc_ok_cnt_off[ss_shift + 9]);
		}
		for (i = 0; i < rate_num; i++) {
			ss_shift = 10 * i;
			PHYDM_DBG(dm, DBG_ADPTV_SOML,
				  "*vht_crc_fail_cnt_off  VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (i + 1),
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 0],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 1],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 2],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 3],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 4],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 5],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 6],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 7],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 8],
				  soml_tab->vht_crc_fail_cnt_off[ss_shift + 9]);
		}

		for (i = ODM_RATEVHTSS2MCS0; i <= ODM_RATEVHTSS2MCS9; i++) {
			vht_total_cnt_on += soml_tab->vht_cnt_on[i - vht0];
			vht_total_cnt_off += soml_tab->vht_cnt_off[i - vht0];
			total_vht_rate_on += (soml_tab->vht_cnt_on[i - vht0] *
					     vht_phy_rate_table[i - vht0]);
			total_vht_rate_off += (soml_tab->vht_cnt_off[i - vht0] *
					      vht_phy_rate_table[i - vht0]);

			if (soml_tab->vht_cnt_on[i - vht0] > cnt_max_on) {
				cnt_max_on = soml_tab->vht_cnt_on[i - vht0];
				max_idx_on = i - vht0;
			}

			if (soml_tab->vht_cnt_off[i - vht0] > cnt_max_off) {
				cnt_max_off = soml_tab->vht_cnt_off[i - vht0];
				max_idx_off = i - vht0;
			}
		}
		total_vht_rate_on = total_vht_rate_on << 3;
		total_vht_rate_off = total_vht_rate_off << 3;
		rate_per_pkt_on = (vht_total_cnt_on != 0) ?
				  (total_vht_rate_on / vht_total_cnt_on) : 0;
		rate_per_pkt_off = (vht_total_cnt_off != 0) ?
				   (total_vht_rate_off / vht_total_cnt_off) : 0;
		#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		vht_ok_max_on = soml_tab->vht_crc_ok_cnt_on[max_idx_on];
		vht_fail_max_on = soml_tab->vht_crc_fail_cnt_on[max_idx_on];
		vht_ok_max_off = soml_tab->vht_crc_ok_cnt_off[max_idx_off];
		vht_fail_max_off = soml_tab->vht_crc_fail_cnt_off[max_idx_off];

		if (vht_fail_max_on == 0)
			vht_fail_max_on = 1;

		if (vht_fail_max_off == 0)
			vht_fail_max_off = 1;

		if (vht_ok_max_on > vht_fail_max_on)
			on_above = true;

		if (vht_ok_max_off > vht_fail_max_off)
			off_above = true;

		if (on_above && !off_above) {
			crc_taget = SOML_ON;
		} else if (!on_above && off_above) {
			crc_taget = SOML_OFF;
		} else if (on_above && off_above) {
			utility_on = (vht_ok_max_on << 7) / vht_fail_max_on;
			utility_off = (vht_ok_max_off << 7) / vht_fail_max_off;
			crc_taget = (utility_on == utility_off) ?
				    (soml_tab->soml_last_state) :
				    ((utility_on > utility_off) ? SOML_ON :
				    SOML_OFF);

		} else if (!on_above && !off_above) {
			if (vht_ok_max_on == 0)
				vht_ok_max_on = 1;
			if (vht_ok_max_off == 0)
				vht_ok_max_off = 1;
			utility_on = (vht_fail_max_on << 7) / vht_ok_max_on;
			utility_off = (vht_fail_max_off << 7) / vht_ok_max_off;
			crc_taget = (utility_on == utility_off) ?
				    (soml_tab->soml_last_state) :
				    ((utility_on < utility_off) ? SOML_ON :
				    SOML_OFF);
		}
		#endif

	}

	/* @[Decision] */
	PHYDM_DBG(dm, DBG_ADPTV_SOML,
		  "[  rate_per_pkt_on = %d ; rate_per_pkt_off = %d ]\n",
		  rate_per_pkt_on, rate_per_pkt_off);
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (max_idx_on == max_idx_off && max_idx_on != 0) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ max_idx_on == max_idx_off ]\n");
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ max_idx = %d, crc_utility_on = %d, crc_utility_off = %d, crc_target = %d]\n",
			  max_idx_on, utility_on, utility_off,
			  crc_taget);
		next_on_off = crc_taget;
	} else
	#endif
	if (rate_per_pkt_on > rate_per_pkt_off) {
		next_on_off = SOML_ON;
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ rate_per_pkt_on > rate_per_pkt_off ==> SOML_ON ]\n");
	} else if (rate_per_pkt_on < rate_per_pkt_off) {
		next_on_off = SOML_OFF;
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ rate_per_pkt_on < rate_per_pkt_off ==> SOML_OFF ]\n");
	} else {
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ stay at soml_last_state ]\n");
		next_on_off = soml_tab->soml_last_state;
	}

	PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Final decisoin ] : ");
	phydm_soml_on_off(dm, next_on_off);
	soml_tab->soml_last_state = next_on_off;
}

void phydm_adsl(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	if (dm->support_ic_type & PHYDM_ADAPTIVE_SOML_IC) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "soml_state_cnt =((%d))\n",
			  soml_tab->soml_state_cnt);
		/*Traning state: 0(alt) 1(ori) 2(alt) 3(ori)===============*/
		if (soml_tab->soml_state_cnt <
		    (soml_tab->soml_train_num << 1)) {
			if (soml_tab->soml_state_cnt == 0)
				phydm_adsl_init_state(dm);
			else if ((soml_tab->soml_state_cnt % 2) != 0)
				phydm_adsl_odd_state(dm);
			else if ((soml_tab->soml_state_cnt % 2) == 0)
				phydm_adsl_even_state(dm);
		} else {
			phydm_adsl_decision_state(dm);
		}
	}
}

void phydm_adaptive_soml_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	soml_tab->soml_state_cnt = 0;
	soml_tab->is_soml_method_enable = 0;
	soml_tab->soml_counter = 0;
}

void phydm_set_adsl_val(void *dm_void, u32 *val_buf, u8 val_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (val_len != 1) {
		PHYDM_DBG(dm, ODM_COMP_API, "[Error][ADSL]Need val_len=1\n");
		return;
	}

	phydm_soml_on_off(dm, (u8)val_buf[1]);
}

void phydm_soml_crc_acq(void *dm_void, u8 rate_id, boolean crc32, u32 length)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 offset = 0;

	if (!soml_tab->get_stats)
		return;
	if (length < 1400)
		return;

	if (soml_tab->soml_on_off == SOML_ON) {
		if (rate_id >= ODM_RATEMCS0 && rate_id <= ODM_RATEMCS15) {
			offset = rate_id - ODM_RATEMCS0;
			if (crc32 == CRC_OK)
				soml_tab->ht_crc_ok_cnt_on[offset]++;
			else if (crc32 == CRC_FAIL)
				soml_tab->ht_crc_fail_cnt_on[offset]++;
		} else if (rate_id >= ODM_RATEVHTSS1MCS0 &&
			   rate_id <= ODM_RATEVHTSS2MCS9) {
			offset = rate_id - ODM_RATEVHTSS1MCS0;
			if (crc32 == CRC_OK)
				soml_tab->vht_crc_ok_cnt_on[offset]++;
			else if (crc32 == CRC_FAIL)
				soml_tab->vht_crc_fail_cnt_on[offset]++;
		}
	} else if (soml_tab->soml_on_off == SOML_OFF) {
		if (rate_id >= ODM_RATEMCS0 && rate_id <= ODM_RATEMCS15) {
			offset = rate_id - ODM_RATEMCS0;
			if (crc32 == CRC_OK)
				soml_tab->ht_crc_ok_cnt_off[offset]++;
			else if (crc32 == CRC_FAIL)
				soml_tab->ht_crc_fail_cnt_off[offset]++;
		} else if (rate_id >= ODM_RATEVHTSS1MCS0 &&
			   rate_id <= ODM_RATEVHTSS2MCS9) {
			offset = rate_id - ODM_RATEVHTSS1MCS0;
			if (crc32 == CRC_OK)
				soml_tab->vht_crc_ok_cnt_off[offset]++;
			else if (crc32 == CRC_FAIL)
				soml_tab->vht_crc_fail_cnt_off[offset]++;
		}
	}
}

void phydm_soml_bytes_acq(void *dm_void, u8 rate_id, u32 length)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	u8 offset = 0;


	if (rate_id >= ODM_RATEMCS0 && rate_id <= ODM_RATEMCS31) {
		offset = rate_id - ODM_RATEMCS0;
		if (offset > (HT_RATE_IDX - 1))
			offset = HT_RATE_IDX - 1;

		soml_tab->ht_byte[offset] += (u16)length;
	} else if (rate_id >= ODM_RATEVHTSS1MCS0 &&
		   rate_id <= ODM_RATEVHTSS4MCS9) {
		offset = rate_id - ODM_RATEVHTSS1MCS0;
		if (offset > (VHT_RATE_IDX - 1))
			offset = VHT_RATE_IDX - 1;

		soml_tab->vht_byte[offset] += (u16)length;
	}
}

#if defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI)
static void pre_phydm_adaptive_soml_callback(unsigned long task_dm)
{
	struct dm_struct *dm = (struct dm_struct *)task_dm;
	struct rtl8192cd_priv *priv = dm->priv;
	struct priv_shared_info *pshare = priv->pshare;

	if (!(priv->drv_state & DRV_STATE_OPEN))
		return;
	if (pshare->bDriverStopped || pshare->bSurpriseRemoved) {
		printk("[%s] bDriverStopped(%d) OR bSurpriseRemoved(%d)\n",
		       __FUNCTION__, pshare->bDriverStopped,
		       pshare->bSurpriseRemoved);
		return;
	}

	rtw_enqueue_timer_event(priv, &pshare->adaptive_soml_event,
				ENQUEUE_TO_TAIL);
}

void phydm_adaptive_soml_timers_usb(void *dm_void, u8 state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
	struct rtl8192cd_priv *priv = dm->priv;

	if (state == INIT_SOML_TIMMER) {
		init_timer(&soml_tab->phydm_adaptive_soml_timer);
		soml_tab->phydm_adaptive_soml_timer.data = (unsigned long)dm;
		soml_tab->phydm_adaptive_soml_timer.function = pre_phydm_adaptive_soml_callback;
		INIT_TIMER_EVENT_ENTRY(&priv->pshare->adaptive_soml_event,
				       phydm_adaptive_soml_callback,
				       (unsigned long)dm);
	} else if (state == CANCEL_SOML_TIMMER) {
		odm_cancel_timer(dm, &soml_tab->phydm_adaptive_soml_timer);
	} else if (state == RELEASE_SOML_TIMMER) {
		odm_release_timer(dm, &soml_tab->phydm_adaptive_soml_timer);
	}
}
#endif /* defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI) */

void phydm_adaptive_soml_timers(void *dm_void, u8 state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

#if defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI)
	struct rtl8192cd_priv *priv = dm->priv;

	if (priv->hci_type == RTL_HCI_USB) {
		phydm_adaptive_soml_timers_usb(dm_void, state);
	} else
#endif /* defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI) */
	{
	if (state == INIT_SOML_TIMMER) {
		odm_initialize_timer(dm, &soml_tab->phydm_adaptive_soml_timer,
				     (void *)phydm_adaptive_soml_callback, NULL,
				     "phydm_adaptive_soml_timer");
	} else if (state == CANCEL_SOML_TIMMER) {
		odm_cancel_timer(dm, &soml_tab->phydm_adaptive_soml_timer);
	} else if (state == RELEASE_SOML_TIMMER) {
		odm_release_timer(dm, &soml_tab->phydm_adaptive_soml_timer);
	}
	}
}

void phydm_adaptive_soml_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;
#if 0
	if (!(dm->support_ability & ODM_BB_ADAPTIVE_SOML)) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[Return]   Not Support Adaptive SOML\n");
		return;
	}
#endif
	PHYDM_DBG(dm, DBG_ADPTV_SOML, "%s\n", __func__);

	soml_tab->soml_state_cnt = 0;
	soml_tab->soml_delay_time = 40;
	soml_tab->soml_intvl = 150;
	soml_tab->soml_train_num = 4;
	soml_tab->is_soml_method_enable = 0;
	soml_tab->soml_counter = 0;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	soml_tab->soml_period = 1;
#else
	soml_tab->soml_period = 4;
#endif
	soml_tab->soml_select = 0;
	soml_tab->cfo_cnt = 0;
	soml_tab->cfo_diff_sum_a = 0;
	soml_tab->cfo_diff_sum_b = 0;

	soml_tab->cfo_qpsk_th = 94;
	soml_tab->cfo_qam16_th = 38;
	soml_tab->cfo_qam64_th = 17;
	soml_tab->cfo_qam256_th = 7;

	soml_tab->bpsk_qpsk_dist_th = 20;
	soml_tab->qam16_dist_th = 20;
	soml_tab->qam64_dist_th = 20;
	soml_tab->qam256_dist_th = 20;

	if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F))
		odm_set_bb_reg(dm, 0x988, BIT(25), 1);
}

void phydm_adaptive_soml(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	if (!(dm->support_ability & ODM_BB_ADAPTIVE_SOML)) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[Return!!!] Not Support Adaptive SOML Function\n");
		return;
	}

	if (dm->pause_ability & ODM_BB_ADAPTIVE_SOML) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "Return: Pause ADSL in LV=%d\n",
			  dm->pause_lv_table.lv_adsl);
		return;
	}

	if (soml_tab->soml_counter < soml_tab->soml_period) {
		soml_tab->soml_counter++;
		return;
	}
	soml_tab->soml_counter = 0;
	soml_tab->soml_state_cnt = 0;
	soml_tab->cfo_cnt = 0;
	soml_tab->cfo_diff_sum_a = 0;
	soml_tab->cfo_diff_sum_b = 0;

	phydm_soml_reset_qam(dm);

	if (soml_tab->soml_select == 0) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML,
			  "[ Adaptive SOML Training !!!]\n");
	} else if (soml_tab->soml_select == 1) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Stop Adaptive SOML !!!]\n");
		phydm_soml_on_off(dm, SOML_ON);
		return;
	} else if (soml_tab->soml_select == 2) {
		PHYDM_DBG(dm, DBG_ADPTV_SOML, "[ Stop Adaptive SOML !!!]\n");
		phydm_soml_on_off(dm, SOML_OFF);
		return;
	}

	if (dm->support_ic_type & PHYDM_ADAPTIVE_SOML_IC)
		phydm_adsl(dm);
}

void phydm_enable_adaptive_soml(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ADPTV_SOML, "[%s]\n", __func__);
	dm->support_ability |= ODM_BB_ADAPTIVE_SOML;
	phydm_soml_on_off(dm, SOML_ON);
}

void phydm_stop_adaptive_soml(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ADPTV_SOML, "[%s]\n", __func__);
	dm->support_ability &= ~ODM_BB_ADAPTIVE_SOML;
	phydm_soml_on_off(dm, SOML_ON);
}

void phydm_adaptive_soml_para_set(void *dm_void, u8 train_num, u8 intvl,
				  u8 period, u8 delay_time)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct adaptive_soml *soml_tab = &dm->dm_soml_table;

	soml_tab->soml_train_num = train_num;
	soml_tab->soml_intvl = intvl;
	soml_tab->soml_period = period;
	soml_tab->soml_delay_time = delay_time;
}
#endif /* @end of CONFIG_ADAPTIVE_SOML*/

void phydm_init_soft_ml_setting(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 soml_mask = BIT(31) | BIT(30) | BIT(29) | BIT(28);

#if (RTL8822B_SUPPORT == 1)
	if (!*dm->mp_mode) {
		if (dm->support_ic_type & ODM_RTL8822B) {
#if 0
			/*odm_set_bb_reg(dm, R_0x19a8, MASKDWORD, 0xd10a0000);*/
#endif
			phydm_somlrxhp_setting(dm, true);
			dm->bsomlenabled = true;
		}
	}
#endif
#if (RTL8821C_SUPPORT == 1)
	if (!*dm->mp_mode) {
		if (dm->support_ic_type & ODM_RTL8821C)
			odm_set_bb_reg(dm, R_0x19a8, soml_mask, 0xd);
	}
#endif
#if (RTL8195B_SUPPORT == 1)
	if (!*dm->mp_mode) {
		if (dm->support_ic_type & ODM_RTL8195B)
			odm_set_bb_reg(dm, R_0x19a8, soml_mask, 0xd);
	}
#endif
}
