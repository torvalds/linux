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
#define _RTW_BEAMFORMING_C_

#include <drv_types.h>

#ifdef CONFIG_BEAMFORMING

struct beamforming_entry	*beamforming_get_entry_by_addr(struct mlme_priv *pmlmepriv, u8* ra,u8* idx)
{
	u8	i = 0;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	
	for(i = 0; i < BEAMFORMING_ENTRY_NUM; i++)
	{
		if(	pBeamInfo->beamforming_entry[i].used && 
			(_rtw_memcmp(ra,pBeamInfo->beamforming_entry[i].mac_addr, ETH_ALEN)))
		{
			*idx = i;
			return &(pBeamInfo->beamforming_entry[i]);
		}
	}

	return NULL;
}

struct beamforming_entry	*beamforming_get_free_entry(struct mlme_priv *pmlmepriv, u8* idx)
{
	u8	i = 0;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for(i = 0; i < BEAMFORMING_ENTRY_NUM; i++)
	{
		if(pBeamInfo->beamforming_entry[i].used == _FALSE)
		{
			*idx = i;
			return &(pBeamInfo->beamforming_entry[i]);
		}	
	}
	return NULL;
}


struct beamforming_entry	*beamforming_add_entry(PADAPTER adapter, u8* ra, u16 aid,
	u16 mac_id, CHANNEL_WIDTH bw, BEAMFORMING_CAP beamfrom_cap, u8* idx)
{
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_entry	*pEntry = beamforming_get_free_entry(pmlmepriv, idx);

	if(pEntry != NULL)
	{	
		pEntry->used = _TRUE;
		pEntry->aid = aid;
		pEntry->mac_id = mac_id;
		pEntry->sound_bw = bw;
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		{
			u16	BSSID = ((adapter->eeprompriv.mac_addr[5] & 0xf0) >> 4) ^ 
							(adapter->eeprompriv.mac_addr[5] & 0xf);	// BSSID[44:47] xor BSSID[40:43]
			pEntry->p_aid = (aid + BSSID * 32) & 0x1ff;		// (dec(A) + dec(B)*32) mod 512
		}		
		else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		{
			pEntry->p_aid = 0;
		}
		else
		{
			pEntry->p_aid =  ra[5];						// BSSID[39:47]
			pEntry->p_aid = (pEntry->p_aid << 1) | (ra[4] >> 7 );
		}
		_rtw_memcpy(pEntry->mac_addr, ra, ETH_ALEN);
		pEntry->tx_bf = _FALSE;
		pEntry->sound_period = 6;
		pEntry->beamforming_entry_cap = beamfrom_cap;
		pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;

		return pEntry;
	}
	else
		return NULL;
}

BOOLEAN	beamforming_remove_entry(struct mlme_priv *pmlmepriv, u8* ra, u8* idx)
{
	struct beamforming_entry	*pEntry = beamforming_get_entry_by_addr(pmlmepriv, ra, idx);

	if(pEntry != NULL)
	{	
		pEntry->used = _FALSE;
		pEntry->beamforming_entry_cap = BEAMFORMING_CAP_NONE;
		pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		return _TRUE;
	}
	else
		return _FALSE;
}

/* Used for BeamformingStart_SW and  BeamformingStart_FW */
void	beamforming_dym_ndpa_rate(PADAPTER adapter)
{
	u16	NDPARate = MGN_6M;
	/*PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	
	if(pHalData->dmpriv.MinUndecoratedPWDBForDM > 30) // link RSSI > 30%
		NDPARate = MGN_24M;
	else
		NDPARate = MGN_6M;
*/
	//BW = CHANNEL_WIDTH_20;
	NDPARate = NDPARate << 8;
	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_RATE, (u8 *)&NDPARate);
}

BOOLEAN	beamfomring_bSounding(struct beamforming_info *pBeamInfo)
{
	BOOLEAN		bSounding = _FALSE;

	if(( beamforming_get_beamform_cap(pBeamInfo) & BEAMFORMER_CAP) == 0)
		bSounding = _FALSE;
	else 
		bSounding = _TRUE;

	return bSounding;
}

u8	beamforming_sounding_idx(struct beamforming_info *pBeamInfo)
{
	u8	idx = 0;
	u8	i;

	for(i = 0; i < BEAMFORMING_ENTRY_NUM; i++)
	{
		if (pBeamInfo->beamforming_entry[i].used)
		{
			idx = i;
			break;
		}
	}

	return idx;
}

SOUNDING_MODE	beamforming_sounding_mode(struct beamforming_info *pBeamInfo, u8 idx)
{
	struct beamforming_entry	BeamEntry = pBeamInfo->beamforming_entry[idx];
	SOUNDING_MODE	mode;

	if(BeamEntry.beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU)
	{
		mode = SOUNDING_FW_VHT_TIMER;
	}
	else if(BeamEntry.beamforming_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)
	{
		mode = SOUNDING_FW_HT_TIMER;
	}
	else
	{
		mode = SOUNDING_STOP_All_TIMER;
	}

	return mode;
}

u16	beamforming_sounding_time(struct beamforming_info *pBeamInfo, SOUNDING_MODE mode, u8 idx)
{
	u16						sounding_time = 0xffff;
	struct beamforming_entry	BeamEntry = pBeamInfo->beamforming_entry[idx];

	sounding_time = BeamEntry.sound_period;

	return sounding_time;
}

CHANNEL_WIDTH	beamforming_sounding_bw(struct beamforming_info *pBeamInfo, SOUNDING_MODE mode, u8 idx)
{
	CHANNEL_WIDTH				sounding_bw = CHANNEL_WIDTH_20;
	struct beamforming_entry		BeamEntry = pBeamInfo->beamforming_entry[idx];

	sounding_bw = BeamEntry.sound_bw;

	return sounding_bw;
}

BOOLEAN	beamforming_select_beam_entry(struct beamforming_info *pBeamInfo)
{
	struct sounding_info		*pSoundInfo = &(pBeamInfo->sounding_info);

	pSoundInfo->sound_idx = beamforming_sounding_idx(pBeamInfo);

	if(pSoundInfo->sound_idx < BEAMFORMING_ENTRY_NUM)
		pSoundInfo->sound_mode = beamforming_sounding_mode(pBeamInfo, pSoundInfo->sound_idx);
	else
		pSoundInfo->sound_mode = SOUNDING_STOP_All_TIMER;
	
	if(SOUNDING_STOP_All_TIMER == pSoundInfo->sound_mode)
	{
		return _FALSE;
	}
	else
	{
		pSoundInfo->sound_bw = beamforming_sounding_bw(pBeamInfo, pSoundInfo->sound_mode, pSoundInfo->sound_idx );
		pSoundInfo->sound_period = beamforming_sounding_time(pBeamInfo, pSoundInfo->sound_mode, pSoundInfo->sound_idx );
		return _TRUE;
	}
}

BOOLEAN	beamforming_start_fw(PADAPTER adapter, u8 idx)
{
	u8						*RA = NULL;
	struct beamforming_entry	*pEntry;
	BOOLEAN					ret = _TRUE;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	pEntry = &(pBeamInfo->beamforming_entry[idx]);
	if(pEntry->used == _FALSE)
	{
		DBG_871X("Skip Beamforming, no entry for Idx =%d\n", idx);
		return _FALSE;
	}

	pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSING;
	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&idx);
	
	DBG_871X("%s  Idx %d \n", __FUNCTION__, idx);
	return _TRUE;
}

void	beamforming_end_fw(PADAPTER adapter)
{
	u8	idx = 0;

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&idx);

	DBG_871X("%s\n", __FUNCTION__);
}

BOOLEAN	beamforming_start_period(PADAPTER adapter)
{
	BOOLEAN	ret = _TRUE;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct sounding_info		*pSoundInfo = &(pBeamInfo->sounding_info);

	beamforming_dym_ndpa_rate(adapter);

	beamforming_select_beam_entry(pBeamInfo);

	if(pSoundInfo->sound_mode == SOUNDING_FW_VHT_TIMER || pSoundInfo->sound_mode == SOUNDING_FW_HT_TIMER)
	{
		ret = beamforming_start_fw(adapter, pSoundInfo->sound_idx);
	}
	else
	{
		ret = _FALSE;
	}

	DBG_871X("%s Idx %d Mode %d BW %d Period %d\n", __FUNCTION__, 
			pSoundInfo->sound_idx, pSoundInfo->sound_mode, pSoundInfo->sound_bw, pSoundInfo->sound_period);

	return ret;
}

void	beamforming_end_period(PADAPTER adapter)
{
	u8						idx = 0;
	struct beamforming_entry	*pBeamformEntry;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct sounding_info		*pSoundInfo = &(pBeamInfo->sounding_info);


	if(pSoundInfo->sound_mode == SOUNDING_FW_VHT_TIMER || pSoundInfo->sound_mode == SOUNDING_FW_HT_TIMER)
	{		
		beamforming_end_fw(adapter);
	}
}

void	beamforming_notify(PADAPTER adapter)
{
	BOOLEAN		bSounding = _FALSE;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(adapter->mlmepriv));

	bSounding = beamfomring_bSounding(pBeamInfo);
	
	if(pBeamInfo->beamforming_state == BEAMFORMING_STATE_IDLE)
	{
		if(bSounding)
		{			
			if(beamforming_start_period(adapter) == _TRUE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_START;
		}
	}
	else if(pBeamInfo->beamforming_state == BEAMFORMING_STATE_START)
	{
		if(bSounding)
		{
			if(beamforming_start_period(adapter) == _FALSE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_END;
		}
		else
		{
			beamforming_end_period(adapter);
			pBeamInfo->beamforming_state = BEAMFORMING_STATE_END;
		}
	}
	else if(pBeamInfo->beamforming_state == BEAMFORMING_STATE_END)
	{
		if(bSounding)
		{
			if(beamforming_start_period(adapter) == _TRUE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_START;
		}
	}
	else
	{
		DBG_871X("%s BeamformState %d\n", __FUNCTION__, pBeamInfo->beamforming_state);
	}

	DBG_871X("%s BeamformState %d bSounding %d\n", __FUNCTION__, pBeamInfo->beamforming_state, bSounding);
}

void	beamforming_reset(PADAPTER adapter)
{
	u8	idx = 0;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for(idx = 0; idx < BEAMFORMING_ENTRY_NUM; idx++)
	{
		if(pBeamInfo->beamforming_entry[idx].used == _TRUE)
		{
			pBeamInfo->beamforming_entry[idx].used = _FALSE;
			pBeamInfo->beamforming_entry[idx].beamforming_entry_cap = BEAMFORMING_CAP_NONE;
			pBeamInfo->beamforming_entry[idx].beamforming_entry_state= BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, (u8 *)&idx);
		}
	}

	DBG_871X("%s\n", __FUNCTION__);
}

BOOLEAN	beamforming_init_entry(PADAPTER	adapter, struct sta_info *psta, u8* idx)
{
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct ht_priv		*phtpriv = &(pmlmepriv->htpriv);
#ifdef CONFIG_80211AC_VHT	
	struct vht_priv		*pvhtpriv = &(pmlmepriv->vhtpriv);
#endif
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct beamforming_entry	*pBeamformEntry = NULL;
	u8	*ra; 
	u16	aid, mac_id;
	u8	wireless_mode;
	CHANNEL_WIDTH	bw = CHANNEL_WIDTH_20;
	BEAMFORMING_CAP	beamform_cap = BEAMFORMING_CAP_NONE;

	// The current setting does not support Beaforming
	if (0 == phtpriv->beamform_cap 
#ifdef CONFIG_80211AC_VHT
		&& 0 == pvhtpriv->beamform_cap
#endif
		) {
		DBG_871X("The configuration disabled Beamforming! Skip...\n");
		return _FALSE;
	}

	aid = psta->aid;
	ra = psta->hwaddr;
	mac_id = psta->mac_id;
	wireless_mode = psta->wireless_mode;
	bw = psta->bw_mode;

	if (IsSupportedHT(wireless_mode) || IsSupportedVHT(wireless_mode)) {
		//3 // HT
		u8	cur_beamform;

		cur_beamform = psta->htpriv.beamform_cap;

		// We are Beamformee because the STA is Beamformer
		if(TEST_FLAG(cur_beamform, BEAMFORMING_HT_BEAMFORMER_ENABLE))
			beamform_cap =(BEAMFORMING_CAP)(beamform_cap |BEAMFORMEE_CAP_HT_EXPLICIT);

		// We are Beamformer because the STA is Beamformee
		if(TEST_FLAG(cur_beamform, BEAMFORMING_HT_BEAMFORMEE_ENABLE))
			beamform_cap =(BEAMFORMING_CAP)(beamform_cap | BEAMFORMER_CAP_HT_EXPLICIT);
#ifdef CONFIG_80211AC_VHT
		if (IsSupportedVHT(wireless_mode)) {
			//3 // VHT
			cur_beamform = psta->vhtpriv.beamform_cap;

			// We are Beamformee because the STA is Beamformer
			if(TEST_FLAG(cur_beamform, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
				beamform_cap =(BEAMFORMING_CAP)(beamform_cap |BEAMFORMEE_CAP_VHT_SU);
			// We are Beamformer because the STA is Beamformee
			if(TEST_FLAG(cur_beamform, BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
				beamform_cap =(BEAMFORMING_CAP)(beamform_cap |BEAMFORMER_CAP_VHT_SU);
		}
#endif //CONFIG_80211AC_VHT

		if(beamform_cap == BEAMFORMING_CAP_NONE)
			return _FALSE;
		
		DBG_871X("Beamforming Config Capability = 0x%02X\n", beamform_cap);

		pBeamformEntry = beamforming_get_entry_by_addr(pmlmepriv, ra, idx);
		if (pBeamformEntry == NULL) {
			pBeamformEntry = beamforming_add_entry(adapter, ra, aid, mac_id, bw, beamform_cap, idx);
			if(pBeamformEntry == NULL)
				return _FALSE;
			else
				pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		} else {
			// Entry has been created. If entry is initialing or progressing then errors occur.
			if (pBeamformEntry->beamforming_entry_state != BEAMFORMING_ENTRY_STATE_INITIALIZED && 
				pBeamformEntry->beamforming_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
				DBG_871X("Error State of Beamforming");
				return _FALSE;
			} else {
				pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
			}
		}

		pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;

		DBG_871X("%s Idx %d\n", __FUNCTION__, *idx);
	} else {
		return _FALSE;
	}

	return _SUCCESS;
}

void	beamforming_deinit_entry(PADAPTER adapter, u8* ra)
{
	u8	idx = 0;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	
	if(beamforming_remove_entry(pmlmepriv, ra, &idx) == _TRUE)
	{
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, (u8 *)&idx);
	}

	DBG_871X("%s Idx %d\n", __FUNCTION__, idx);
}

void	beamforming_enter(PADAPTER adapter, PVOID psta)
{
	u8	idx = 0xff;

	if(beamforming_init_entry(adapter, (struct sta_info *)psta, &idx))
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_ENTER, (u8 *)&idx);

	DBG_871X("%s Idx %d\n", __FUNCTION__, idx);
}

void	beamforming_leave(PADAPTER adapter,u8* ra)
{
	if(ra == NULL)
		beamforming_reset(adapter);
	else
		beamforming_deinit_entry(adapter, ra);

	beamforming_notify(adapter);
}

BEAMFORMING_CAP beamforming_get_beamform_cap(struct beamforming_info	*pBeamInfo)
{
	u8	i;
	BOOLEAN 				bSelfBeamformer = _FALSE;
	BOOLEAN 				bSelfBeamformee = _FALSE;
	struct beamforming_entry	beamforming_entry;
	BEAMFORMING_CAP 		beamform_cap = BEAMFORMING_CAP_NONE;

	for(i = 0; i < BEAMFORMING_ENTRY_NUM; i++)
	{
		beamforming_entry = pBeamInfo->beamforming_entry[i];

		if(beamforming_entry.used)
		{
			if( (beamforming_entry.beamforming_entry_cap& BEAMFORMEE_CAP_VHT_SU) ||
				(beamforming_entry.beamforming_entry_cap & BEAMFORMEE_CAP_HT_EXPLICIT))
				bSelfBeamformee = _TRUE;
			if( (beamforming_entry.beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU) ||
				(beamforming_entry.beamforming_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT))
				bSelfBeamformer = _TRUE;
		}

		if(bSelfBeamformer && bSelfBeamformee)
			i = BEAMFORMING_ENTRY_NUM;
	}

	if(bSelfBeamformer)
		beamform_cap |= BEAMFORMER_CAP;
	if(bSelfBeamformee)
		beamform_cap |= BEAMFORMEE_CAP;

	return beamform_cap;
}


void	beamforming_wk_hdl(_adapter *padapter, u8 type, u8 *pbuf)
{
	struct pwrctrl_priv		*pwrpriv = adapter_to_pwrctl(padapter);
	
_func_enter_;

	switch(type)
	{
		case BEAMFORMING_CTRL_ENTER:
			beamforming_enter(padapter, (PVOID)pbuf);
			break;

		case BEAMFORMING_CTRL_LEAVE:
			beamforming_leave(padapter, pbuf);
			break;
	
		default:
			break;
	}

_func_exit_;
}

u8	beamforming_wk_cmd(_adapter*padapter, s32 type, u8 *pbuf, s32 size, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;
	
_func_enter_;

	if(enqueue)
	{
		u8	*wk_buf;
	
		ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));	
		if(ph2c==NULL){
			res= _FAIL;
			goto exit;
		}
		
		pdrvextra_cmd_parm = (struct drvextra_cmd_parm*)rtw_zmalloc(sizeof(struct drvextra_cmd_parm)); 
		if(pdrvextra_cmd_parm==NULL){
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res= _FAIL;
			goto exit;
		}

		wk_buf = rtw_zmalloc(size);
		if(wk_buf==NULL){
			rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
			rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
			res= _FAIL;
			goto exit;
		}

		_rtw_memcpy(wk_buf, pbuf, size);

		pdrvextra_cmd_parm->ec_id = BEAMFORMING_WK_CID;
		pdrvextra_cmd_parm->type = type;
		pdrvextra_cmd_parm->size = size;
		pdrvextra_cmd_parm->pbuf = wk_buf;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	}
	else
	{
		beamforming_wk_hdl(padapter, type, pbuf);
	}
	
exit:
	
_func_exit_;

	return res;
}

#endif //CONFIG_BEAMFORMING

