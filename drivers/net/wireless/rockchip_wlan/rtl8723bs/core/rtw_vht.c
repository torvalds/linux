/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#define _RTW_VHT_C

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_80211AC_VHT
/*				20/40/80,	ShortGI,	MCS Rate  */
const u16 VHT_MCS_DATA_RATE[3][2][30] = {
	{	{
			13, 26, 39, 52, 78, 104, 117, 130, 156, 156,
			26, 52, 78, 104, 156, 208, 234, 260, 312, 312,
			39, 78, 117, 156, 234, 312, 351, 390, 468, 520
		},			/* Long GI, 20MHz */
		{
			14, 29, 43, 58, 87, 116, 130, 144, 173, 173,
			29, 58, 87, 116, 173, 231, 260, 289, 347, 347,
			43,	87, 130, 173, 260, 347, 390,	433,	520, 578
		}
	},		/* Short GI, 20MHz */
	{	{
			27, 54, 81, 108, 162, 216, 243, 270, 324, 360,
			54, 108, 162, 216, 324, 432, 486, 540, 648, 720,
			81, 162, 243, 324, 486, 648, 729, 810, 972, 1080
		}, 		/* Long GI, 40MHz */
		{
			30, 60, 90, 120, 180, 240, 270, 300, 360, 400,
			60, 120, 180, 240, 360, 480, 540, 600, 720, 800,
			90, 180, 270, 360, 540, 720, 810, 900, 1080, 1200
		}
	},		/* Short GI, 40MHz */
	{	{
			59, 117,  176, 234, 351, 468, 527, 585, 702, 780,
			117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560,
			176, 351, 527, 702, 1053, 1404, 1580, 1755, 2106, 2340
		},	/* Long GI, 80MHz */
		{
			65, 130, 195, 260, 390, 520, 585, 650, 780, 867,
			130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734,
			195, 390, 585, 780, 1170, 1560, 1755, 1950, 2340, 2600
		}
	}	/* Short GI, 80MHz */
};

u8	rtw_get_vht_highest_rate(u8 *pvht_mcs_map)
{
	u8	i, j;
	u8	bit_map;
	u8	vht_mcs_rate = 0;

	for (i = 0; i < 2; i++) {
		if (pvht_mcs_map[i] != 0xff) {
			for (j = 0; j < 8; j += 2) {
				bit_map = (pvht_mcs_map[i] >> j) & 3;

				if (bit_map != 3)
					vht_mcs_rate = MGN_VHT1SS_MCS7 + 10 * j / 2 + i * 40 + bit_map; /* VHT rate indications begin from 0x90 */
			}
		}
	}

	/* RTW_INFO("HighestVHTMCSRate is %x\n", vht_mcs_rate); */
	return vht_mcs_rate;
}

u8	rtw_vht_mcsmap_to_nss(u8 *pvht_mcs_map)
{
	u8	i, j;
	u8	bit_map;
	u8	nss = 0;

	for (i = 0; i < 2; i++) {
		if (pvht_mcs_map[i] != 0xff) {
			for (j = 0; j < 8; j += 2) {
				bit_map = (pvht_mcs_map[i] >> j) & 3;

				if (bit_map != 3)
					nss++;
			}
		}
	}

	/* RTW_INFO("%s : %dSS\n", __FUNCTION__, nss); */
	return nss;
}

void rtw_vht_nss_to_mcsmap(u8 nss, u8 *target_mcs_map, u8 *cur_mcs_map)
{
	u8	i, j;
	u8	cur_rate, target_rate;

	for (i = 0; i < 2; i++) {
		target_mcs_map[i] = 0;
		for (j = 0; j < 8; j += 2) {
			cur_rate = (cur_mcs_map[i] >> j) & 3;
			if (cur_rate == 3) /* 0x3 indicates not supported that num of SS */
				target_rate = 3;
			else if (nss <= ((j / 2) + i * 4))
				target_rate = 3;
			else
				target_rate = cur_rate;

			target_mcs_map[i] |= (target_rate << j);
		}
	}

	/* RTW_INFO("%s : %dSS\n", __FUNCTION__, nss); */
}

u16	rtw_vht_mcs_to_data_rate(u8 bw, u8 short_GI, u8 vht_mcs_rate)
{
	if (vht_mcs_rate > MGN_VHT3SS_MCS9)
		vht_mcs_rate = MGN_VHT3SS_MCS9;
	/* RTW_INFO("bw=%d, short_GI=%d, ((vht_mcs_rate - MGN_VHT1SS_MCS0)&0x3f)=%d\n", bw, short_GI, ((vht_mcs_rate - MGN_VHT1SS_MCS0)&0x3f)); */
	return VHT_MCS_DATA_RATE[bw][short_GI][((vht_mcs_rate - MGN_VHT1SS_MCS0) & 0x3f)];
}

void	rtw_vht_use_default_setting(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	BOOLEAN		bHwLDPCSupport = _FALSE, bHwSTBCSupport = _FALSE;
#ifdef CONFIG_BEAMFORMING
	BOOLEAN		bHwSupportBeamformer = _FALSE, bHwSupportBeamformee = _FALSE;
	u8	mu_bfer, mu_bfee;
#endif /* CONFIG_BEAMFORMING */
	u8	rf_type = 0;
	u8 tx_nss, rx_nss;
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	pvhtpriv->sgi_80m = TEST_FLAG(pregistrypriv->short_gi, BIT2) ? _TRUE : _FALSE;

	/* LDPC support */
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_LDPC, (u8 *)&bHwLDPCSupport);
	CLEAR_FLAGS(pvhtpriv->ldpc_cap);
	if (bHwLDPCSupport) {
		if (TEST_FLAG(pregistrypriv->ldpc_cap, BIT0))
			SET_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_RX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_LDPC, (u8 *)&bHwLDPCSupport);
	if (bHwLDPCSupport) {
		if (TEST_FLAG(pregistrypriv->ldpc_cap, BIT1))
			SET_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_TX);
	}
	if (pvhtpriv->ldpc_cap)
		RTW_INFO("[VHT] Support LDPC = 0x%02X\n", pvhtpriv->ldpc_cap);

	/* STBC */
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_STBC, (u8 *)&bHwSTBCSupport);
	CLEAR_FLAGS(pvhtpriv->stbc_cap);
	if (bHwSTBCSupport) {
		if (TEST_FLAG(pregistrypriv->stbc_cap, BIT1))
			SET_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_STBC, (u8 *)&bHwSTBCSupport);
	if (bHwSTBCSupport) {
		if (TEST_FLAG(pregistrypriv->stbc_cap, BIT0))
			SET_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_RX);
	}
	if (pvhtpriv->stbc_cap)
		RTW_INFO("[VHT] Support STBC = 0x%02X\n", pvhtpriv->stbc_cap);

	/* Beamforming setting */
	CLEAR_FLAGS(pvhtpriv->beamform_cap);
#ifdef CONFIG_BEAMFORMING
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&bHwSupportBeamformer);
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&bHwSupportBeamformee);
	mu_bfer = _FALSE;
	mu_bfee = _FALSE;
	rtw_hal_get_def_var(padapter, HAL_DEF_VHT_MU_BEAMFORMER, &mu_bfer);
	rtw_hal_get_def_var(padapter, HAL_DEF_VHT_MU_BEAMFORMEE, &mu_bfee);
	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT0) && bHwSupportBeamformer) {
#ifdef CONFIG_CONCURRENT_MODE
		if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
			SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
			RTW_INFO("[VHT] CONCURRENT AP Support Beamformer\n");
			if (TEST_FLAG(pregistrypriv->beamform_cap, BIT(2))
			    && (_TRUE == mu_bfer)) {
				SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE);
				RTW_INFO("[VHT] Support MU-MIMO AP\n");
			}
		} else
			RTW_INFO("[VHT] CONCURRENT not AP ;not allow  Support Beamformer\n");
#else
		SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
		RTW_INFO("[VHT] Support Beamformer\n");
		if (TEST_FLAG(pregistrypriv->beamform_cap, BIT(2))
		    && (_TRUE == mu_bfer)
		    && ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
			SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE);
			RTW_INFO("[VHT] Support MU-MIMO AP\n");
		}
#endif
	}
	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT1) && bHwSupportBeamformee) {
		SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
		RTW_INFO("[VHT] Support Beamformee\n");
		if (TEST_FLAG(pregistrypriv->beamform_cap, BIT(3))
		    && (_TRUE == mu_bfee)
		    && ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)) {
			SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE);
			RTW_INFO("[VHT] Support MU-MIMO STA\n");
		}
	}
#endif /* CONFIG_BEAMFORMING */

	pvhtpriv->ampdu_len = pregistrypriv->ampdu_factor;

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	tx_nss = rtw_min(rf_type_to_rf_tx_cnt(rf_type), hal_spec->tx_nss_num);
	rx_nss = rtw_min(rf_type_to_rf_rx_cnt(rf_type), hal_spec->rx_nss_num);

	/* for now, vhtpriv.vht_mcs_map comes from RX NSS */
	rtw_vht_nss_to_mcsmap(rx_nss, pvhtpriv->vht_mcs_map, pregistrypriv->vht_rx_mcs_map);
	pvhtpriv->vht_highest_rate = rtw_get_vht_highest_rate(pvhtpriv->vht_mcs_map);
}

u64	rtw_vht_mcs_map_to_bitmap(u8 *mcs_map, u8 nss)
{
	u8 i, j, tmp;
	u64 bitmap = 0;
	u8 bits_nss = nss * 2;

	for (i = j = 0; i < bits_nss; i += 2, j += 10) {
		/* every two bits means single sptial stream */
		tmp = (mcs_map[i / 8] >> i) & 3;

		switch (tmp) {
		case 2:
			bitmap = bitmap | (0x03ff << j);
			break;
		case 1:
			bitmap = bitmap | (0x01ff << j);
			break;
		case 0:
			bitmap = bitmap | (0x00ff << j);
			break;
		default:
			break;
		}
	}

	RTW_INFO("vht_mcs_map=%02x %02x, nss=%u => bitmap=%016llx\n"
		, mcs_map[0], mcs_map[1], nss, bitmap);

	return bitmap;
}

#ifdef CONFIG_BEAMFORMING
void update_sta_vht_info_apmode_bf_cap(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct vht_priv	*pvhtpriv_ap = &pmlmepriv->vhtpriv;
	struct vht_priv	*pvhtpriv_sta = &psta->vhtpriv;
	u16	cur_beamform_cap = 0;

	/* B11 SU Beamformer Capable, the target supports Beamformer and we are Beamformee */
	if (TEST_FLAG(pvhtpriv_ap->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE) &&
	    GET_VHT_CAPABILITY_ELE_SU_BFEE(pvhtpriv_sta->vht_cap)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
		/*Shift to BEAMFORMING_VHT_BEAMFORMER_STS_CAP*/
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFEE_STS_CAP(pvhtpriv_sta->vht_cap) << 8);
	}

	/* B12 SU Beamformee Capable, the target supports Beamformee and we are Beamformer */
	if (TEST_FLAG(pvhtpriv_ap->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) &&
	    GET_VHT_CAPABILITY_ELE_SU_BFER(pvhtpriv_sta->vht_cap)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
		/*Shit to BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM*/
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFER_SOUND_DIM_NUM(pvhtpriv_sta->vht_cap) << 12);
	}

	if (cur_beamform_cap)
		RTW_INFO("Current STA(%d) VHT Beamforming Setting = %02X\n", psta->cmn.aid, cur_beamform_cap);

	pvhtpriv_sta->beamform_cap = cur_beamform_cap;
	psta->cmn.bf_info.vht_beamform_cap = cur_beamform_cap;
}
#endif

void	update_sta_vht_info_apmode(_adapter *padapter, PVOID sta)
{
	struct sta_info	*psta = (struct sta_info *)sta;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct vht_priv	*pvhtpriv_ap = &pmlmepriv->vhtpriv;
	struct vht_priv	*pvhtpriv_sta = &psta->vhtpriv;
	u8	cur_ldpc_cap = 0, cur_stbc_cap = 0, bw_mode = 0;
	u8	*pcap_mcs;

	if (pvhtpriv_sta->vht_option == _FALSE)
		return;

	bw_mode = GET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(&pvhtpriv_sta->vht_op_mode_notify);

	/* if (bw_mode > psta->cmn.bw_mode) */
	psta->cmn.bw_mode = bw_mode;
	psta->cmn.ra_info.is_vht_enable = _TRUE;

	/* B4 Rx LDPC */
	if (TEST_FLAG(pvhtpriv_ap->ldpc_cap, LDPC_VHT_ENABLE_TX) &&
	    GET_VHT_CAPABILITY_ELE_RX_LDPC(pvhtpriv_sta->vht_cap)) {
		SET_FLAG(cur_ldpc_cap, (LDPC_VHT_ENABLE_TX | LDPC_VHT_CAP_TX));
		RTW_INFO("Current STA(%d) VHT LDPC = %02X\n", psta->cmn.aid, cur_ldpc_cap);
	}
	pvhtpriv_sta->ldpc_cap = cur_ldpc_cap;

	if (psta->cmn.bw_mode > pmlmeext->cur_bwmode)
		psta->cmn.bw_mode = pmlmeext->cur_bwmode;

	if (psta->cmn.bw_mode == CHANNEL_WIDTH_80) {
		/* B5 Short GI for 80 MHz */
		pvhtpriv_sta->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI80M(pvhtpriv_sta->vht_cap) & pvhtpriv_ap->sgi_80m) ? _TRUE : _FALSE;
		/* RTW_INFO("Current STA ShortGI80MHz = %d\n", pvhtpriv_sta->sgi_80m); */
	} else if (psta->cmn.bw_mode >= CHANNEL_WIDTH_160) {
		/* B5 Short GI for 80 MHz */
		pvhtpriv_sta->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI160M(pvhtpriv_sta->vht_cap) & pvhtpriv_ap->sgi_80m) ? _TRUE : _FALSE;
		/* RTW_INFO("Current STA ShortGI160MHz = %d\n", pvhtpriv_sta->sgi_80m); */
	}

	/* B8 B9 B10 Rx STBC */
	if (TEST_FLAG(pvhtpriv_ap->stbc_cap, STBC_VHT_ENABLE_TX) &&
	    GET_VHT_CAPABILITY_ELE_RX_STBC(pvhtpriv_sta->vht_cap)) {
		SET_FLAG(cur_stbc_cap, (STBC_VHT_ENABLE_TX | STBC_VHT_CAP_TX));
		RTW_INFO("Current STA(%d) VHT STBC = %02X\n", psta->cmn.aid, cur_stbc_cap);
	}
	pvhtpriv_sta->stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
	update_sta_vht_info_apmode_bf_cap(padapter, psta);
#endif

	/* B23 B24 B25 Maximum A-MPDU Length Exponent */
	pvhtpriv_sta->ampdu_len = GET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pvhtpriv_sta->vht_cap);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_RX_MCS(pvhtpriv_sta->vht_cap);
	_rtw_memcpy(pvhtpriv_sta->vht_mcs_map, pcap_mcs, 2);
	pvhtpriv_sta->vht_highest_rate = rtw_get_vht_highest_rate(pvhtpriv_sta->vht_mcs_map);
}

void	update_hw_vht_param(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	ht_AMPDU_len;

	ht_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	if (pvhtpriv->ampdu_len > ht_AMPDU_len)
		rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&pvhtpriv->ampdu_len));
}

void VHT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	cur_ldpc_cap = 0, cur_stbc_cap = 0, rf_type = RF_1T1R, tx_nss = 0;
	u16	cur_beamform_cap = 0;
	u8	*pcap_mcs;

	if (pIE == NULL)
		return;

	if (pvhtpriv->vht_option == _FALSE)
		return;

	pmlmeinfo->VHT_enable = 1;

	/* B4 Rx LDPC */
	if (TEST_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_TX) &&
	    GET_VHT_CAPABILITY_ELE_RX_LDPC(pIE->data)) {
		SET_FLAG(cur_ldpc_cap, (LDPC_VHT_ENABLE_TX | LDPC_VHT_CAP_TX));
		RTW_INFO("Current VHT LDPC Setting = %02X\n", cur_ldpc_cap);
	}
	pvhtpriv->ldpc_cap = cur_ldpc_cap;

	/* B5 Short GI for 80 MHz */
	pvhtpriv->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI80M(pIE->data) & pvhtpriv->sgi_80m) ? _TRUE : _FALSE;
	/* RTW_INFO("Current ShortGI80MHz = %d\n", pvhtpriv->sgi_80m); */

	/* B8 B9 B10 Rx STBC */
	if (TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX) &&
	    GET_VHT_CAPABILITY_ELE_RX_STBC(pIE->data)) {
		SET_FLAG(cur_stbc_cap, (STBC_VHT_ENABLE_TX | STBC_VHT_CAP_TX));
		RTW_INFO("Current VHT STBC Setting = %02X\n", cur_stbc_cap);
	}
	pvhtpriv->stbc_cap = cur_stbc_cap;
#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	/*
	 * B11 SU Beamformer Capable,
	 * the target supports Beamformer and we are Beamformee
	 */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE)
	    && GET_VHT_CAPABILITY_ELE_SU_BFER(pIE->data)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);

		/* Shift to BEAMFORMING_VHT_BEAMFORMEE_STS_CAP */
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFEE_STS_CAP(pIE->data) << 8);

		/*
		 * B19 MU Beamformer Capable,
		 * the target supports Beamformer and we are Beamformee
		 */
		if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE)
		    && GET_VHT_CAPABILITY_ELE_MU_BFER(pIE->data))
			SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE);
	}

	/*
	 * B12 SU Beamformee Capable,
	 * the target supports Beamformee and we are Beamformer
	 */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE)
	    && GET_VHT_CAPABILITY_ELE_SU_BFEE(pIE->data)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);

		/* Shit to BEAMFORMING_VHT_BEAMFORMER_SOUND_DIM */
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFER_SOUND_DIM_NUM(pIE->data) << 12);

		/*
		 * B20 MU Beamformee Capable,
		 * the target supports Beamformee and we are Beamformer
		 */
		if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE)
		    && GET_VHT_CAPABILITY_ELE_MU_BFEE(pIE->data))
			SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE);
	}

	pvhtpriv->beamform_cap = cur_beamform_cap;
	RTW_INFO("Current VHT Beamforming Setting=0x%04X\n", cur_beamform_cap);
#else /* !RTW_BEAMFORMING_VERSION_2 */
	/* B11 SU Beamformer Capable, the target supports Beamformer and we are Beamformee */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE) &&
	    GET_VHT_CAPABILITY_ELE_SU_BFEE(pIE->data)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
		/*Shift to BEAMFORMING_VHT_BEAMFORMER_STS_CAP*/
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFEE_STS_CAP(pIE->data) << 8);
	}

	/* B12 SU Beamformee Capable, the target supports Beamformee and we are Beamformer */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) &&
	    GET_VHT_CAPABILITY_ELE_SU_BFER(pIE->data)) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
		/*Shit to BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM*/
		SET_FLAG(cur_beamform_cap, GET_VHT_CAPABILITY_ELE_SU_BFER_SOUND_DIM_NUM(pIE->data) << 12);

	}
	pvhtpriv->beamform_cap = cur_beamform_cap;
	if (cur_beamform_cap)
		RTW_INFO("Current VHT Beamforming Setting = %02X\n", cur_beamform_cap);
#endif /* !RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */
	/* B23 B24 B25 Maximum A-MPDU Length Exponent */
	pvhtpriv->ampdu_len = GET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pIE->data);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_RX_MCS(pIE->data);
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	tx_nss = rtw_min(rf_type_to_rf_tx_cnt(rf_type), hal_spec->tx_nss_num);
	rtw_vht_nss_to_mcsmap(tx_nss, pvhtpriv->vht_mcs_map, pcap_mcs);
	pvhtpriv->vht_highest_rate = rtw_get_vht_highest_rate(pvhtpriv->vht_mcs_map);
}

void VHT_operation_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;

	if (pIE == NULL)
		return;

	if (pvhtpriv->vht_option == _FALSE)
		return;
}

void rtw_process_vht_op_mode_notify(_adapter *padapter, u8 *pframe, PVOID sta)
{
	struct sta_info		*psta = (struct sta_info *)sta;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct registry_priv *regsty = adapter_to_regsty(padapter);
	u8	target_bw;
	u8	target_rxss, current_rxss;
	u8	update_ra = _FALSE;

	if (pvhtpriv->vht_option == _FALSE)
		return;

	target_bw = GET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(pframe);
	target_rxss = (GET_VHT_OPERATING_MODE_FIELD_RX_NSS(pframe) + 1);

	if (target_bw != psta->cmn.bw_mode) {
		if (hal_is_bw_support(padapter, target_bw)
		    && REGSTY_IS_BW_5G_SUPPORT(regsty, target_bw)
		   ) {
			update_ra = _TRUE;
			psta->cmn.bw_mode = target_bw;
		}
	}

	current_rxss = rtw_vht_mcsmap_to_nss(psta->vhtpriv.vht_mcs_map);
	if (target_rxss != current_rxss) {
		u8	vht_mcs_map[2] = {};

		update_ra = _TRUE;

		rtw_vht_nss_to_mcsmap(target_rxss, vht_mcs_map, psta->vhtpriv.vht_mcs_map);
		_rtw_memcpy(psta->vhtpriv.vht_mcs_map, vht_mcs_map, 2);

		rtw_hal_update_sta_ra_info(padapter, psta);
	}

	if (update_ra)
		rtw_dm_ra_mask_wk_cmd(padapter, (u8 *)psta);
}

u32	rtw_build_vht_operation_ie(_adapter *padapter, u8 *pbuf, u8 channel)
{
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	/* struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv; */
	u8	ChnlWidth, center_freq, bw_mode;
	u32	len = 0;
	u8	operation[5];

	_rtw_memset(operation, 0, 5);

	bw_mode = REGSTY_BW_5G(pregistrypriv); /* TODO: control op bw with other info */

	if (hal_chk_bw_cap(padapter, BW_CAP_80M | BW_CAP_160M)
	    && REGSTY_BW_5G(pregistrypriv) >= CHANNEL_WIDTH_80
	   ) {
		center_freq = rtw_get_center_ch(channel, bw_mode, HAL_PRIME_CHNL_OFFSET_LOWER);
		ChnlWidth = 1;
	} else {
		center_freq = 0;
		ChnlWidth = 0;
	}


	SET_VHT_OPERATION_ELE_CHL_WIDTH(operation, ChnlWidth);
	/* center frequency */
	SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(operation, center_freq);/* Todo: need to set correct center channel */
	SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(operation, 0);

	_rtw_memcpy(operation + 3, pvhtpriv->vht_mcs_map, 2);

	rtw_set_ie(pbuf, EID_VHTOperation, 5, operation, &len);

	return len;
}

u32	rtw_build_vht_op_mode_notify_ie(_adapter *padapter, u8 *pbuf, u8 bw)
{
	/* struct registry_priv *pregistrypriv = &padapter->registrypriv; */
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;
	u32	len = 0;
	u8	opmode = 0;
	u8	chnl_width, rx_nss;

	chnl_width = bw;
	rx_nss = rtw_vht_mcsmap_to_nss(pvhtpriv->vht_mcs_map);

	SET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(&opmode, chnl_width);
	SET_VHT_OPERATING_MODE_FIELD_RX_NSS(&opmode, (rx_nss - 1));
	SET_VHT_OPERATING_MODE_FIELD_RX_NSS_TYPE(&opmode, 0); /* Todo */

	pvhtpriv->vht_op_mode_notify = opmode;

	pbuf = rtw_set_ie(pbuf, EID_OpModeNotification, 1, &opmode, &len);

	return len;
}

u32	rtw_build_vht_cap_ie(_adapter *padapter, u8 *pbuf)
{
	u8	bw, rf_type, rf_num, rx_stbc_nss = 0;
	u16	HighestRate;
	u8	*pcap, *pcap_mcs;
	u32	len = 0;
	u32 rx_packet_offset, max_recvbuf_sz;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pcap = pvhtpriv->vht_cap;
	_rtw_memset(pcap, 0, 32);

	/* B0 B1 Maximum MPDU Length */
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
	rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);

	RTW_DBG("%s, line%d, Available RX buf size = %d bytes\n.", __FUNCTION__, __LINE__, max_recvbuf_sz - rx_packet_offset);

	if ((max_recvbuf_sz - rx_packet_offset) >= 11454) {
		SET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(pcap, 2);
		RTW_INFO("%s, line%d, Set MAX MPDU len = 11454 bytes\n.", __FUNCTION__, __LINE__);
	} else if ((max_recvbuf_sz - rx_packet_offset) >= 7991) {
		SET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(pcap, 1);
		RTW_INFO("%s, line%d, Set MAX MPDU len = 7991 bytes\n.", __FUNCTION__, __LINE__);
	} else if ((max_recvbuf_sz - rx_packet_offset) >= 3895) {
		SET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(pcap, 0);
		RTW_INFO("%s, line%d, Set MAX MPDU len = 3895 bytes\n.", __FUNCTION__, __LINE__);
	} else
		RTW_ERR("%s, line%d, Error!! Available RX buf size < 3895 bytes\n.", __FUNCTION__, __LINE__);

	/* B2 B3 Supported Channel Width Set */
	if (hal_chk_bw_cap(padapter, BW_CAP_160M) && REGSTY_IS_BW_5G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_160)) {
		if (hal_chk_bw_cap(padapter, BW_CAP_80_80M) && REGSTY_IS_BW_5G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_80_80))
			SET_VHT_CAPABILITY_ELE_CHL_WIDTH(pcap, 2);
		else
			SET_VHT_CAPABILITY_ELE_CHL_WIDTH(pcap, 1);
	} else
		SET_VHT_CAPABILITY_ELE_CHL_WIDTH(pcap, 0);

	/* B4 Rx LDPC */
	if (TEST_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_RX)) {
		SET_VHT_CAPABILITY_ELE_RX_LDPC(pcap, 1);
		RTW_INFO("[VHT] Declare supporting RX LDPC\n");
	}

	/* B5 ShortGI for 80MHz */
	SET_VHT_CAPABILITY_ELE_SHORT_GI80M(pcap, pvhtpriv->sgi_80m ? 1 : 0); /* We can receive Short GI of 80M */
	if (pvhtpriv->sgi_80m)
		RTW_INFO("[VHT] Declare supporting SGI 80MHz\n");

	/* B6 ShortGI for 160MHz */
	/* SET_VHT_CAPABILITY_ELE_SHORT_GI160M(pcap, pvhtpriv->sgi_80m? 1 : 0); */

	/* B7 Tx STBC */
	if (TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX)) {
		SET_VHT_CAPABILITY_ELE_TX_STBC(pcap, 1);
		RTW_INFO("[VHT] Declare supporting TX STBC\n");
	}

	/* B8 B9 B10 Rx STBC */
	if (TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_RX)) {
		rtw_hal_get_def_var(padapter, HAL_DEF_RX_STBC, (u8 *)(&rx_stbc_nss));

		SET_VHT_CAPABILITY_ELE_RX_STBC(pcap, rx_stbc_nss);
		RTW_INFO("[VHT] Declare supporting RX STBC = %d\n", rx_stbc_nss);
	}
	#ifdef CONFIG_BEAMFORMING
	/* B11 SU Beamformer Capable */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE)) {
		SET_VHT_CAPABILITY_ELE_SU_BFER(pcap, 1);
		RTW_INFO("[VHT] Declare supporting SU Bfer\n");
		/* B16 17 18 Number of Sounding Dimensions */
		rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMER_CAP, (u8 *)&rf_num);
		SET_VHT_CAPABILITY_ELE_SOUNDING_DIMENSIONS(pcap, rf_num);
		/* B19 MU Beamformer Capable */
		if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE)) {
			SET_VHT_CAPABILITY_ELE_MU_BFER(pcap, 1);
			RTW_INFO("[VHT] Declare supporting MU Bfer\n");
		}
	}

	/* B12 SU Beamformee Capable */
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE)) {
		SET_VHT_CAPABILITY_ELE_SU_BFEE(pcap, 1);
		RTW_INFO("[VHT] Declare supporting SU Bfee\n");

		rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMEE_CAP, (u8 *)&rf_num);

		/* IOT action suggested by Yu Chen 2017/3/3 */
		if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_BROADCOM) &&
			!GET_VHT_CAPABILITY_ELE_MU_BFER(&pvhtpriv->beamform_cap))
			rf_num = (rf_num >= 2 ? 2 : rf_num);

		/* B13 14 15 Compressed Steering Number of Beamformer Antennas Supported */
		SET_VHT_CAPABILITY_ELE_BFER_ANT_SUPP(pcap, rf_num);
		/* B20 SU Beamformee Capable */
		if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE)) {
			SET_VHT_CAPABILITY_ELE_MU_BFEE(pcap, 1);
			RTW_INFO("[VHT] Declare supporting MU Bfee\n");
		}
	}
	#endif/*CONFIG_BEAMFORMING*/

	/* B21 VHT TXOP PS */
	SET_VHT_CAPABILITY_ELE_TXOP_PS(pcap, 0);
	/* B22 +HTC-VHT Capable */
	SET_VHT_CAPABILITY_ELE_HTC_VHT(pcap, 1);
	/* B23 24 25 Maximum A-MPDU Length Exponent */
	if (pregistrypriv->ampdu_factor != 0xFE)
		SET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pcap, pregistrypriv->ampdu_factor);
	else
		SET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pcap, 7);
	/* B26 27 VHT Link Adaptation Capable */
	SET_VHT_CAPABILITY_ELE_LINK_ADAPTION(pcap, 0);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_RX_MCS(pcap);
	_rtw_memcpy(pcap_mcs, pvhtpriv->vht_mcs_map, 2);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_TX_MCS(pcap);
	_rtw_memcpy(pcap_mcs, pvhtpriv->vht_mcs_map, 2);

	/* find the largest bw supported by both registry and hal */
	bw = hal_largest_bw(padapter, REGSTY_BW_5G(pregistrypriv));

	HighestRate = VHT_MCS_DATA_RATE[bw][pvhtpriv->sgi_80m][((pvhtpriv->vht_highest_rate - MGN_VHT1SS_MCS0) & 0x3f)];
	HighestRate = (HighestRate + 1) >> 1;

	SET_VHT_CAPABILITY_ELE_MCS_RX_HIGHEST_RATE(pcap, HighestRate); /* indicate we support highest rx rate is 600Mbps. */
	SET_VHT_CAPABILITY_ELE_MCS_TX_HIGHEST_RATE(pcap, HighestRate); /* indicate we support highest tx rate is 600Mbps. */

	pbuf = rtw_set_ie(pbuf, EID_VHTCapability, 12, pcap, &len);

	return len;
}

u32 rtw_restructure_vht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len)
{
	u32	ielen = 0, out_len = 0;
	u8	cap_len = 0, notify_len = 0, notify_bw = 0, operation_bw = 0, supported_chnl_width = 0;
	u8	*p, *pframe;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;

	rtw_vht_use_default_setting(padapter);

	p = rtw_get_ie(in_ie + 12, EID_VHTCapability, &ielen, in_len - 12);
	if (p && ielen > 0) {
		supported_chnl_width = GET_VHT_CAPABILITY_ELE_CHL_WIDTH(p + 2);

		/* VHT Capabilities element */
		cap_len = rtw_build_vht_cap_ie(padapter, out_ie + *pout_len);
		*pout_len += cap_len;

		/* Get HT BW */
		p = rtw_get_ie(in_ie + 12, _HT_EXTRA_INFO_IE_, &ielen, in_len - 12);
		if (p && ielen > 0) {
			struct HT_info_element *pht_info = (struct HT_info_element *)(p + 2);
			if (pht_info->infos[0] & BIT(2))
				operation_bw = CHANNEL_WIDTH_40;
			else
				operation_bw = CHANNEL_WIDTH_20;
		}

		/* VHT Operation element */
		p = rtw_get_ie(in_ie + 12, EID_VHTOperation, &ielen, in_len - 12);
		if (p && ielen > 0) {
			out_len = *pout_len;
			if (GET_VHT_OPERATION_ELE_CHL_WIDTH(p + 2) >= 1) {
				if (supported_chnl_width == 2)
					operation_bw = CHANNEL_WIDTH_80_80;
				else if (supported_chnl_width == 1)
					operation_bw = CHANNEL_WIDTH_160;
				else
					operation_bw = CHANNEL_WIDTH_80;
			}
			pframe = rtw_set_ie(out_ie + out_len, EID_VHTOperation, ielen, p + 2 , pout_len);
		}

		/* find the largest bw supported by both registry and hal */
		notify_bw = hal_largest_bw(padapter, REGSTY_BW_5G(pregistrypriv));

		if (notify_bw > operation_bw)
			notify_bw = operation_bw;

		/* Operating Mode Notification element */
		notify_len = rtw_build_vht_op_mode_notify_ie(padapter, out_ie + *pout_len, notify_bw);
		*pout_len += notify_len;

		pvhtpriv->vht_option = _TRUE;
	}

	return pvhtpriv->vht_option;

}

void VHTOnAssocRsp(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	ht_AMPDU_len;

	RTW_INFO("%s\n", __FUNCTION__);

	if (!pmlmeinfo->HT_enable)
		return;

	if (!pmlmeinfo->VHT_enable)
		return;

	ht_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	if (pvhtpriv->ampdu_len > ht_AMPDU_len)
		rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&pvhtpriv->ampdu_len));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MAX_TIME, (u8 *)(&pvhtpriv->vht_highest_rate));
}

void rtw_vht_ies_attach(_adapter *padapter, WLAN_BSSID_EX *pnetwork)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 cap_len, operation_len;
	uint len = 0;
	sint ie_len = 0;
	u8 *p = NULL;

	p = rtw_get_ie(pnetwork->IEs + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len,
			(pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		return;

	rtw_vht_use_default_setting(padapter);

	/* VHT Operation mode notifiy bit in Extended IE (127) */
	SET_EXT_CAPABILITY_ELE_OP_MODE_NOTIF(pmlmepriv->ext_capab_ie_data, 1);
	pmlmepriv->ext_capab_ie_len = 10;
	rtw_set_ie(pnetwork->IEs + pnetwork->IELength, EID_EXTCapability, 8, pmlmepriv->ext_capab_ie_data, &len);
	pnetwork->IELength += pmlmepriv->ext_capab_ie_len;

	/* VHT Capabilities element */
	cap_len = rtw_build_vht_cap_ie(padapter, pnetwork->IEs + pnetwork->IELength);
	pnetwork->IELength += cap_len;

	/* VHT Operation element */
	operation_len = rtw_build_vht_operation_ie(padapter, pnetwork->IEs + pnetwork->IELength,
										pnetwork->Configuration.DSConfig);
	pnetwork->IELength += operation_len;

	pmlmepriv->vhtpriv.vht_option = _TRUE;
}

void rtw_vht_ies_detach(_adapter *padapter, WLAN_BSSID_EX *pnetwork)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	rtw_remove_bcn_ie(padapter, pnetwork, EID_EXTCapability);
	rtw_remove_bcn_ie(padapter, pnetwork, EID_VHTCapability);
	rtw_remove_bcn_ie(padapter, pnetwork, EID_VHTOperation);

	pmlmepriv->vhtpriv.vht_option = _FALSE;
}

#endif /* CONFIG_80211AC_VHT */
