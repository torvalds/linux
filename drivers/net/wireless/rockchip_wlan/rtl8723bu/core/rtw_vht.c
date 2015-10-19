/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_VHT_C

#include <drv_types.h>

#ifdef CONFIG_80211AC_VHT
// 				20/40/80,	ShortGI,	MCS Rate 
const u16 VHT_MCS_DATA_RATE[3][2][30] = 
	{	{	{13, 26, 39, 52, 78, 104, 117, 130, 156, 156,
			 26, 52, 78, 104, 156, 208, 234, 260, 312, 312,
			 39, 78, 117, 156, 234, 312, 351, 390, 468, 520},			// Long GI, 20MHz
			{14, 29, 43, 58, 87, 116, 130, 144, 173, 173,
			29, 58, 87, 116, 173, 231, 260, 289, 347, 347,
			43,	87, 130, 173, 260, 347,390,	433,	520, 578}	},		// Short GI, 20MHz
		{	{27, 54, 81, 108, 162, 216, 243, 270, 324, 360, 
			54, 108, 162, 216, 324, 432, 486, 540, 648, 720,
			81, 162, 243, 324, 486, 648, 729, 810, 972, 1080}, 		// Long GI, 40MHz
			{30, 60, 90, 120, 180, 240, 270, 300,360, 400, 
			60, 120, 180, 240, 360, 480, 540, 600, 720, 800,
			90, 180, 270, 360, 540, 720, 810, 900, 1080, 1200}},		// Short GI, 40MHz
		{	{59, 117,  176, 234, 351, 468, 527, 585, 702, 780,
			117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560,
			176, 351, 527, 702, 1053, 1404, 1580, 1755, 2106, 2106}, 	// Long GI, 80MHz
			{65, 130, 195, 260, 390, 520, 585, 650, 780, 867, 
			130, 260, 390, 520, 780, 1040, 1170, 1300, 1560,1734,
			195, 390, 585, 780, 1170, 1560, 1755, 1950, 2340, 2340}	}	// Short GI, 80MHz
	};

u8	rtw_get_vht_highest_rate(u8 *pvht_mcs_map)
{
	u8	i, j;
	u8	bit_map;
	u8	vht_mcs_rate = 0;
	
	for(i = 0; i < 2; i++)
	{
		if(pvht_mcs_map[i] != 0xff)
		{
			for(j = 0; j < 8; j += 2)
			{
				bit_map = (pvht_mcs_map[i] >> j) & 3;
				
				if(bit_map != 3)
					vht_mcs_rate = MGN_VHT1SS_MCS7 + 10*j/2 + i*40 + bit_map;  //VHT rate indications begin from 0x90
			}
		}
	}
	
	/* DBG_871X("HighestVHTMCSRate is %x\n", vht_mcs_rate); */
	return vht_mcs_rate;
}

u8	rtw_vht_mcsmap_to_nss(u8 *pvht_mcs_map)
{
	u8	i, j;
	u8	bit_map;
	u8	nss = 0;
	
	for(i = 0; i < 2; i++)
	{
		if(pvht_mcs_map[i] != 0xff)
		{
			for(j = 0; j < 8; j += 2)
			{
				bit_map = (pvht_mcs_map[i] >> j) & 3;
				
				if(bit_map != 3)
					nss++;
			}
		}
	}
	
	DBG_871X("%s : %dSS\n", __FUNCTION__, nss);
	return nss;
}

void	rtw_vht_nss_to_mcsmap(u8 nss, u8 *target_mcs_map, u8 *cur_mcs_map)
{
	u8	i, j;
	u8	cur_rate, target_rate;
	
	for(i = 0; i < 2; i++)
	{
		target_mcs_map[i] = 0;
		for(j = 0; j < 8; j+=2)
		{
			cur_rate = (cur_mcs_map[i] >> j) & 3;
			if(cur_rate == 3)  //0x3 indicates not supported that num of SS
				target_rate = 3;
			else if(nss <= ((j/2)+i*4))
				target_rate = 3;
			else	
				target_rate = cur_rate;

			target_mcs_map[i] |= (target_rate << j);
		}
	}
	
	//DBG_871X("%s : %dSS\n", __FUNCTION__, nss);
}

u16	rtw_vht_mcs_to_data_rate(u8 bw, u8 short_GI, u8 vht_mcs_rate)
{
	if(vht_mcs_rate > MGN_VHT3SS_MCS9)
		vht_mcs_rate = MGN_VHT3SS_MCS9;
	/* DBG_871X("bw=%d, short_GI=%d, ((vht_mcs_rate - MGN_VHT1SS_MCS0)&0x3f)=%d\n", bw, short_GI, ((vht_mcs_rate - MGN_VHT1SS_MCS0)&0x3f)); */
	return VHT_MCS_DATA_RATE[bw][short_GI][((vht_mcs_rate - MGN_VHT1SS_MCS0)&0x3f)];
}

void	rtw_vht_use_default_setting(_adapter *padapter)
{
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	BOOLEAN		bHwLDPCSupport = _FALSE, bHwSTBCSupport = _FALSE;
	BOOLEAN		bHwSupportBeamformer = _FALSE, bHwSupportBeamformee = _FALSE;
	u8	rf_type = 0;

	pvhtpriv->sgi_80m = TEST_FLAG(pregistrypriv->short_gi, BIT2) ? _TRUE : _FALSE;

	// LDPC support
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_LDPC, (u8 *)&bHwLDPCSupport);
	CLEAR_FLAGS(pvhtpriv->ldpc_cap);
	if(bHwLDPCSupport)
	{
		if(TEST_FLAG(pregistrypriv->ldpc_cap, BIT0))
			SET_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_RX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_LDPC, (u8 *)&bHwLDPCSupport);
	if(bHwLDPCSupport)
	{
		if(TEST_FLAG(pregistrypriv->ldpc_cap, BIT1))
			SET_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_TX);
	}
	if (pvhtpriv->ldpc_cap)
		DBG_871X("[VHT] Support LDPC = 0x%02X\n", pvhtpriv->ldpc_cap);

	// STBC
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_STBC, (u8 *)&bHwSTBCSupport);
	CLEAR_FLAGS(pvhtpriv->stbc_cap);
	if(bHwSTBCSupport)
	{
		if(TEST_FLAG(pregistrypriv->stbc_cap, BIT1))
			SET_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_STBC, (u8 *)&bHwSTBCSupport);
	if(bHwSTBCSupport)
	{
		if(TEST_FLAG(pregistrypriv->stbc_cap, BIT0))
			SET_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_RX);
	}
	if (pvhtpriv->stbc_cap)
		DBG_871X("[VHT] Support STBC = 0x%02X\n", pvhtpriv->stbc_cap);

	// Beamforming setting
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&bHwSupportBeamformer);
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&bHwSupportBeamformee);
	CLEAR_FLAGS(pvhtpriv->beamform_cap);
	if(TEST_FLAG(pregistrypriv->beamform_cap, BIT0) && bHwSupportBeamformer)
	{
		SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
		DBG_871X("[VHT] Support Beamformer\n");
	}
	if(TEST_FLAG(pregistrypriv->beamform_cap, BIT1) && bHwSupportBeamformee)
	{
		SET_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
		DBG_871X("[VHT] Support Beamformee\n");
	}

	pvhtpriv->ampdu_len = pregistrypriv->ampdu_factor;

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	if (rf_type == RF_3T3R)
		pvhtpriv->vht_mcs_map[0] = 0xea;	/* support 1SS MCS 0~9 2SS MCS 0~9 3SS MCS 0~9 */
	else if(rf_type == RF_2T2R)
		pvhtpriv->vht_mcs_map[0] = 0xfa;	/* support 1SS MCS 0~9 2SS MCS 0~9 */
	else
		pvhtpriv->vht_mcs_map[0] = 0xfe;	/* Only support 1SS MCS 0~9; */
	pvhtpriv->vht_mcs_map[1] = 0xff;

	if(pregistrypriv->vht_rate_sel == 1)
	{			
		pvhtpriv->vht_mcs_map[0] = 0xfc;	// support 1SS MCS 0~7
	}
	else if(pregistrypriv->vht_rate_sel == 2)
	{
		pvhtpriv->vht_mcs_map[0] = 0xfd;	// Support 1SS MCS 0~8
	}
	else if(pregistrypriv->vht_rate_sel == 3)
	{
		pvhtpriv->vht_mcs_map[0] = 0xfe;	// Support 1SS MCS 0~9
	}
	else if(pregistrypriv->vht_rate_sel == 4)
	{
		pvhtpriv->vht_mcs_map[0] = 0xf0;	// support 1SS MCS 0~7 2SS MCS 0~7
	}
	else if(pregistrypriv->vht_rate_sel == 5)
	{
		pvhtpriv->vht_mcs_map[0] = 0xf5;	// support 1SS MCS 0~8 2SS MCS 0~8
	}
	else if(pregistrypriv->vht_rate_sel == 6)
	{
		pvhtpriv->vht_mcs_map[0] = 0xfa;	// support 1SS MCS 0~9 2SS MCS 0~9
	}
	else if(pregistrypriv->vht_rate_sel == 7)
	{
		pvhtpriv->vht_mcs_map[0] = 0xf8;	// support 1SS MCS 0-7 2SS MCS 0~9
	}
	else if(pregistrypriv->vht_rate_sel == 8)
	{
		pvhtpriv->vht_mcs_map[0] = 0xf9;	// support 1SS MCS 0-8 2SS MCS 0~9
	}
	else if(pregistrypriv->vht_rate_sel == 9)
	{
		pvhtpriv->vht_mcs_map[0] = 0xf4;	// support 1SS MCS 0-7 2SS MCS 0~8
	}

	pvhtpriv->vht_highest_rate = rtw_get_vht_highest_rate(pvhtpriv->vht_mcs_map);
}

u64	rtw_vht_rate_to_bitmap(u8 *pVHTRate)
{

	u8	i,j , tmpRate;
	u64	RateBitmap = 0;
	u8 Bits_3ss = 6;
		
	for(i = j= 0; i < Bits_3ss; i+=2, j+=10)
	{
		/* every two bits means single sptial stream */
		tmpRate = (pVHTRate[0] >> i) & 3;

		switch(tmpRate){
		case 2:
			RateBitmap = RateBitmap | (0x03ff << j);
			break;
		case 1:
			RateBitmap = RateBitmap | (0x01ff << j);
		break;

		case 0:
			RateBitmap = RateBitmap | (0x00ff << j);
		break;

		default:
			break;
		}
	}
	DBG_871X("RateBitmap=%016llx , pVHTRate[0]=%02x\n", RateBitmap, pVHTRate[0]);
	return RateBitmap;
}

void	update_sta_vht_info_apmode(_adapter *padapter, PVOID sta)
{
	struct sta_info	*psta = (struct sta_info *)sta;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct vht_priv	*pvhtpriv_ap = &pmlmepriv->vhtpriv;
	struct vht_priv	*pvhtpriv_sta = &psta->vhtpriv;
	struct ht_priv		*phtpriv_sta = &psta->htpriv;
	u8	cur_ldpc_cap=0, cur_stbc_cap=0, cur_beamform_cap=0, bw_mode = 0;
	u8	*pcap_mcs;

	if (pvhtpriv_sta->vht_option == _FALSE) {
		return;
	}

	bw_mode = GET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(&pvhtpriv_sta->vht_op_mode_notify);
	
	//if (bw_mode > psta->bw_mode)
	psta->bw_mode = bw_mode;

	// B4 Rx LDPC
	if (TEST_FLAG(pvhtpriv_ap->ldpc_cap, LDPC_VHT_ENABLE_TX) && 
		GET_VHT_CAPABILITY_ELE_RX_LDPC(pvhtpriv_sta->vht_cap))
	{
		SET_FLAG(cur_ldpc_cap, (LDPC_VHT_ENABLE_TX | LDPC_VHT_CAP_TX));
		DBG_871X("Current STA(%d) VHT LDPC = %02X\n", psta->aid, cur_ldpc_cap);
	}
	pvhtpriv_sta->ldpc_cap = cur_ldpc_cap;

	if (psta->bw_mode > pmlmeext->cur_bwmode)
		psta->bw_mode = pmlmeext->cur_bwmode;

	if (psta->bw_mode == CHANNEL_WIDTH_80) {
		// B5 Short GI for 80 MHz
		pvhtpriv_sta->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI80M(pvhtpriv_sta->vht_cap) & pvhtpriv_ap->sgi_80m) ? _TRUE : _FALSE;
		//DBG_871X("Current STA ShortGI80MHz = %d\n", pvhtpriv_sta->sgi_80m);
	} else if (psta->bw_mode >= CHANNEL_WIDTH_160) {
		// B5 Short GI for 80 MHz
		pvhtpriv_sta->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI160M(pvhtpriv_sta->vht_cap) & pvhtpriv_ap->sgi_80m) ? _TRUE : _FALSE;
		//DBG_871X("Current STA ShortGI160MHz = %d\n", pvhtpriv_sta->sgi_80m);
	}

	// B8 B9 B10 Rx STBC
	if (TEST_FLAG(pvhtpriv_ap->stbc_cap, STBC_VHT_ENABLE_TX) && 
		GET_VHT_CAPABILITY_ELE_RX_STBC(pvhtpriv_sta->vht_cap))
	{
		SET_FLAG(cur_stbc_cap, (STBC_VHT_ENABLE_TX | STBC_VHT_CAP_TX));	
		DBG_871X("Current STA(%d) VHT STBC = %02X\n", psta->aid, cur_stbc_cap);
	}
	pvhtpriv_sta->stbc_cap = cur_stbc_cap;

	// B11 SU Beamformer Capable, the target supports Beamformer and we are Beamformee
	if (TEST_FLAG(pvhtpriv_ap->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE) && 
		GET_VHT_CAPABILITY_ELE_SU_BFEE(pvhtpriv_sta->vht_cap))
	{
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
	}

	// B12 SU Beamformee Capable, the target supports Beamformee and we are Beamformer
	if (TEST_FLAG(pvhtpriv_ap->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) &&
		GET_VHT_CAPABILITY_ELE_SU_BFER(pvhtpriv_sta->vht_cap))
	{
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
	}
	pvhtpriv_sta->beamform_cap = cur_beamform_cap;
	if (cur_beamform_cap) {
		DBG_871X("Current STA(%d) VHT Beamforming Setting = %02X\n", psta->aid, cur_beamform_cap);
	}

	// B23 B24 B25 Maximum A-MPDU Length Exponent
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

	if(pvhtpriv->ampdu_len > ht_AMPDU_len)
		rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&pvhtpriv->ampdu_len));
}

void VHT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	cur_ldpc_cap=0, cur_stbc_cap=0, cur_beamform_cap=0, rf_type = RF_1T1R;
	u8	*pcap_mcs;
	u8	vht_mcs[2];

	if(pIE==NULL) return;

	if(pvhtpriv->vht_option == _FALSE)	return;

	pmlmeinfo->VHT_enable = 1;

	// B4 Rx LDPC
	if (TEST_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_TX) && 
		GET_VHT_CAPABILITY_ELE_RX_LDPC(pIE->data))
	{
		SET_FLAG(cur_ldpc_cap, (LDPC_VHT_ENABLE_TX | LDPC_VHT_CAP_TX));
		DBG_871X("Current VHT LDPC Setting = %02X\n", cur_ldpc_cap);
	}
	pvhtpriv->ldpc_cap = cur_ldpc_cap;

	// B5 Short GI for 80 MHz
	pvhtpriv->sgi_80m = (GET_VHT_CAPABILITY_ELE_SHORT_GI80M(pIE->data) & pvhtpriv->sgi_80m) ? _TRUE : _FALSE;
	//DBG_871X("Current ShortGI80MHz = %d\n", pvhtpriv->sgi_80m);

	// B8 B9 B10 Rx STBC
	if (TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX) && 
		GET_VHT_CAPABILITY_ELE_RX_STBC(pIE->data))
	{
		SET_FLAG(cur_stbc_cap, (STBC_VHT_ENABLE_TX | STBC_VHT_CAP_TX));	
		DBG_871X("Current VHT STBC Setting = %02X\n", cur_stbc_cap);
	}
	pvhtpriv->stbc_cap = cur_stbc_cap;

	// B11 SU Beamformer Capable, the target supports Beamformer and we are Beamformee
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE) && 
		GET_VHT_CAPABILITY_ELE_SU_BFEE(pIE->data))
	{
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE);
	}

	// B12 SU Beamformee Capable, the target supports Beamformee and we are Beamformer
	if (TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) &&
		GET_VHT_CAPABILITY_ELE_SU_BFER(pIE->data))
	{
		SET_FLAG(cur_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE);
	}
	pvhtpriv->beamform_cap = cur_beamform_cap;
	if (cur_beamform_cap) {
		DBG_871X("Current VHT Beamforming Setting = %02X\n", cur_beamform_cap);
	}

	// B23 B24 B25 Maximum A-MPDU Length Exponent
	pvhtpriv->ampdu_len = GET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pIE->data);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_RX_MCS(pIE->data);
	_rtw_memcpy(vht_mcs, pcap_mcs, 2);

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if ((rf_type == RF_1T1R) || (rf_type == RF_1T2R))
		vht_mcs[0] |= 0xfc;
	else if (rf_type == RF_2T2R)
		vht_mcs[0] |= 0xf0;
	else if (rf_type == RF_3T3R)
		vht_mcs[0] |= 0xc0;

	_rtw_memcpy(pvhtpriv->vht_mcs_map, vht_mcs, 2);

	pvhtpriv->vht_highest_rate = rtw_get_vht_highest_rate(pvhtpriv->vht_mcs_map);
}

void VHT_operation_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;

	if(pIE==NULL) return;

	if(pvhtpriv->vht_option == _FALSE)	return;
}

void rtw_process_vht_op_mode_notify(_adapter *padapter, u8 *pframe, PVOID sta)
{
	struct sta_info		*psta = (struct sta_info *)sta;
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	u8	target_bw;
	u8	target_rxss, current_rxss;
	u8	update_ra = _FALSE;
	u8	vht_mcs_map[2] = {};

	if(pvhtpriv->vht_option == _FALSE)
		return;

	target_bw = GET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(pframe);
	target_rxss = (GET_VHT_OPERATING_MODE_FIELD_RX_NSS(pframe)+1);

	if (target_bw != psta->bw_mode) {
		if (target_bw <= (padapter->registrypriv.bw_mode >> 4)) {
			update_ra = _TRUE;
			psta->bw_mode = target_bw;
		}
	}

	current_rxss = rtw_vht_mcsmap_to_nss(psta->vhtpriv.vht_mcs_map);
	if (target_rxss != current_rxss) {
		update_ra = _TRUE;

		rtw_vht_nss_to_mcsmap(target_rxss, vht_mcs_map, psta->vhtpriv.vht_mcs_map);
		_rtw_memcpy(psta->vhtpriv.vht_mcs_map, vht_mcs_map, 2);
		
		rtw_hal_update_sta_rate_mask(padapter, psta);
	}

	if (update_ra) {
		rtw_dm_ra_mask_wk_cmd(padapter, (u8 *)psta);
	}
}

u32	rtw_build_vht_operation_ie(_adapter *padapter, u8 *pbuf, u8 channel)
{
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	//struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	u8	ChnlWidth, center_freq, bw_mode;
	u32	len = 0;
	u8	operation[5];

	_rtw_memset(operation, 0, 5);

	bw_mode = pregistrypriv->bw_mode >> 4;

	if (bw_mode >= CHANNEL_WIDTH_80)
	{
		center_freq = rtw_get_center_ch(channel, bw_mode, HAL_PRIME_CHNL_OFFSET_LOWER);
		ChnlWidth = 1;
	}
	else
	{
		center_freq = 0;
		ChnlWidth = 0;
	}

	
	SET_VHT_OPERATION_ELE_CHL_WIDTH(operation, ChnlWidth);
	//center frequency
	SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(operation, center_freq);//Todo: need to set correct center channel
	SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(operation,0);
	operation[3] = 0xff;
	operation[4] = 0xff;

	rtw_set_ie(pbuf, EID_VHTOperation, 5, operation, &len);

	return len;
}

u32	rtw_build_vht_op_mode_notify_ie(_adapter *padapter, u8 *pbuf, u8 bw)
{
	//struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;
	u32	len = 0;
	u8	opmode = 0, rf_type = 0;
	u8	chnl_width, rx_nss;

	chnl_width = bw;

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if(rf_type == RF_3T3R)
		rx_nss = 3;
	else if(rf_type == RF_2T2R)
		rx_nss = 2;
	else
		rx_nss = 1;

	SET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(&opmode, chnl_width);
	SET_VHT_OPERATING_MODE_FIELD_RX_NSS(&opmode, (rx_nss-1));
	SET_VHT_OPERATING_MODE_FIELD_RX_NSS_TYPE(&opmode, 0); //Todo

	pvhtpriv->vht_op_mode_notify = opmode;

	pbuf = rtw_set_ie(pbuf, EID_OpModeNotification, 1, &opmode, &len);

	return len;
}

u32	rtw_build_vht_cap_ie(_adapter *padapter, u8 *pbuf)
{
	u8	bw, rf_type;
	u16	HighestRate;
	u8	*pcap, *pcap_mcs;
	u32	len = 0;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;

	pcap = pvhtpriv->vht_cap;
	_rtw_memset(pcap, 0, 32);
	
	/* B0 B1 Maximum MPDU Length */
	SET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(pcap, 2); 
	
	// B2 B3 Supported Channel Width Set
	SET_VHT_CAPABILITY_ELE_CHL_WIDTH(pcap, 0);  //indicate we don't support neither 160M nor 80+80M bandwidth.

	// B4 Rx LDPC
	if(TEST_FLAG(pvhtpriv->ldpc_cap, LDPC_VHT_ENABLE_RX))
	{
		SET_VHT_CAPABILITY_ELE_RX_LDPC(pcap, 1);
	}

	// B5 ShortGI for 80MHz
	SET_VHT_CAPABILITY_ELE_SHORT_GI80M(pcap, pvhtpriv->sgi_80m? 1 : 0); // We can receive Short GI of 80M

	// B6 ShortGI for 160MHz
	//SET_VHT_CAPABILITY_ELE_SHORT_GI160M(pcap, pvhtpriv->sgi_80m? 1 : 0);

	// B7 Tx STBC
	if(TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_TX))
	{
		SET_VHT_CAPABILITY_ELE_TX_STBC(pcap, 1);
	}

	// B8 B9 B10 Rx STBC
	if(TEST_FLAG(pvhtpriv->stbc_cap, STBC_VHT_ENABLE_RX))
	{
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if ((rf_type == RF_2T2R) || (rf_type == RF_1T2R)) {
			SET_VHT_CAPABILITY_ELE_RX_STBC(pcap, 2);
		}
		else if (rf_type == RF_1T1R) {
			SET_VHT_CAPABILITY_ELE_RX_STBC(pcap, 1);
		}
	}

	// B11 SU Beamformer Capable
	if(TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
	{
		SET_VHT_CAPABILITY_ELE_SU_BFER(pcap, 1);
		// B16 17 18 Number of Sounding Dimensions
		SET_VHT_CAPABILITY_ELE_SOUNDING_DIMENSIONS(pcap, 1);
	}

	// B12 SU Beamformee Capable
	if(TEST_FLAG(pvhtpriv->beamform_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
	{
		SET_VHT_CAPABILITY_ELE_SU_BFEE(pcap, 1);
		// B13 14 15 Compressed Steering Number of Beamformer Antennas Supported
		SET_VHT_CAPABILITY_ELE_BFER_ANT_SUPP(pcap, 1);
	}

	// B19 MU Beamformer Capable
	SET_VHT_CAPABILITY_ELE_MU_BFER(pcap, 0);  //HW don't support mu bfee/bfer
	// B20 MU Beamformee Capable
	SET_VHT_CAPABILITY_ELE_MU_BFEE(pcap, 0);
	// B21 VHT TXOP PS
	SET_VHT_CAPABILITY_ELE_TXOP_PS(pcap, 0);
	// B22 +HTC-VHT Capable
	SET_VHT_CAPABILITY_ELE_HTC_VHT(pcap, 1);
	// B23 24 25 Maximum A-MPDU Length Exponent
	if (pregistrypriv->ampdu_factor != 0xFE)
	{
		SET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pcap, pregistrypriv->ampdu_factor);
	}
	else
	{
		SET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(pcap, 7);
	}
	// B26 27 VHT Link Adaptation Capable
	SET_VHT_CAPABILITY_ELE_LINK_ADAPTION(pcap, 0);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_RX_MCS(pcap);
	_rtw_memcpy(pcap_mcs, pvhtpriv->vht_mcs_map, 2);

	pcap_mcs = GET_VHT_CAPABILITY_ELE_TX_MCS(pcap);
	_rtw_memcpy(pcap_mcs, pvhtpriv->vht_mcs_map, 2);

	bw = (pregistrypriv->bw_mode >> 4);
	HighestRate = VHT_MCS_DATA_RATE[bw][pvhtpriv->sgi_80m][((pvhtpriv->vht_highest_rate - MGN_VHT1SS_MCS0)&0x3f)];
	HighestRate = (HighestRate+1) >> 1;	

	SET_VHT_CAPABILITY_ELE_MCS_RX_HIGHEST_RATE(pcap, HighestRate); //indicate we support highest rx rate is 600Mbps.
	SET_VHT_CAPABILITY_ELE_MCS_TX_HIGHEST_RATE(pcap, HighestRate); //indicate we support highest tx rate is 600Mbps.

	pbuf = rtw_set_ie(pbuf, EID_VHTCapability, 12, pcap, &len);

	return len;
}

u32 rtw_restructure_vht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len)
{
	u32	ielen=0, out_len=0;
	u8	cap_len=0, notify_len=0, notify_bw=0, operation_bw=0, supported_chnl_width=0;
	u8	*p, *pframe;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;

	rtw_vht_use_default_setting(padapter);

	p = rtw_get_ie(in_ie+12, EID_VHTCapability, &ielen, in_len-12);
	if (p && ielen>0) {
		supported_chnl_width = GET_VHT_CAPABILITY_ELE_CHL_WIDTH(p+2);
		
		// VHT Capabilities element
		cap_len = rtw_build_vht_cap_ie(padapter, out_ie+*pout_len);
		*pout_len += cap_len;

		// Get HT BW
		p = rtw_get_ie(in_ie+12, _HT_EXTRA_INFO_IE_, &ielen, in_len-12);
		if (p && ielen>0) {
			struct HT_info_element *pht_info = (struct HT_info_element *)(p+2);
			if (pht_info->infos[0] & BIT(2))
				operation_bw = CHANNEL_WIDTH_40;
			else
				operation_bw = CHANNEL_WIDTH_20;
		}

		// VHT Operation element
		p = rtw_get_ie(in_ie+12, EID_VHTOperation, &ielen, in_len-12);
		if (p && ielen>0) {
			out_len = *pout_len;
			if (GET_VHT_OPERATION_ELE_CHL_WIDTH(p+2) >= 1) {
				if (supported_chnl_width == 2)
					operation_bw = CHANNEL_WIDTH_80_80;
				else if (supported_chnl_width == 1)
					operation_bw = CHANNEL_WIDTH_160;
				else
					operation_bw = CHANNEL_WIDTH_80;
			}
			pframe = rtw_set_ie(out_ie+out_len, EID_VHTOperation, ielen, p+2 , pout_len);
		}

		notify_bw = pregistrypriv->bw_mode >> 4;

		if (notify_bw > operation_bw)
			notify_bw = operation_bw;

		// Operating Mode Notification element
		notify_len = rtw_build_vht_op_mode_notify_ie(padapter, out_ie+*pout_len, notify_bw);
		*pout_len += notify_len;

		pvhtpriv->vht_option = _TRUE;
	}
	
	return (pvhtpriv->vht_option);
	
}

void VHTOnAssocRsp(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	ht_AMPDU_len;
	
	DBG_871X("%s\n", __FUNCTION__);

	if (!pmlmeinfo->HT_enable)
		return;

	if (!pmlmeinfo->VHT_enable)
		return;

	ht_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	if(pvhtpriv->ampdu_len > ht_AMPDU_len)
		rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&pvhtpriv->ampdu_len));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MAX_TIME, (u8 *)(&pvhtpriv->vht_highest_rate));
}

#endif //CONFIG_80211AC_VHT

