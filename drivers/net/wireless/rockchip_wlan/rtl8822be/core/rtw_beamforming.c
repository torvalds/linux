/******************************************************************************
 *
 * Copyright(c) 2007 - 2016 Realtek Corporation. All rights reserved.
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
#include <hal_data.h>

#ifdef CONFIG_BEAMFORMING

#if (BEAMFORMING_SUPPORT == 0) /*for diver defined beamforming*/
#ifdef RTW_BEAMFORMING_VERSION_2
/*
 * For phydm
 */
BEAMFORMING_CAP beamforming_get_entry_beam_cap_by_mac_id(void *mlme, u8 mac_id)
{
	PADAPTER adapter;
	struct beamforming_info	*pBeamInfo;
	struct beamformee_entry *bfee;
	BEAMFORMING_CAP cap = BEAMFORMING_CAP_NONE;
	u8 i = 0;


	adapter = mlme_to_adapter((struct mlme_priv *)mlme);
	pBeamInfo = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		bfee = &pBeamInfo->bfee_entry[i];
		if ((bfee->used == _TRUE)
		    && (bfee->mac_id == mac_id)) {
			cap =  bfee->cap;
			break;
		}
	}

	return cap;
}

struct beamformer_entry *beamforming_get_bfer_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	u8 i = 0;
	struct beamforming_info	*bf_info;
	struct beamformer_entry *entry;


	bf_info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		entry = &bf_info->bfer_entry[i];
		if (entry->used == _FALSE)
			continue;
		if (_rtw_memcmp(ra, entry->mac_addr, ETH_ALEN) == _TRUE) {
			return entry;
		}
	}

	return NULL;
}

struct beamformee_entry *beamforming_get_bfee_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	u8 i = 0;
	struct beamforming_info	*bf_info;
	struct beamformee_entry *entry;


	bf_info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		entry = &bf_info->bfee_entry[i];
		if (entry->used == _FALSE)
			continue;
		if (_rtw_memcmp(ra, entry->mac_addr, ETH_ALEN) == _TRUE)
			return entry;
	}

	return NULL;
}

static struct beamformer_entry *_get_bfer_free_entry(PADAPTER adapter)
{
	u8 i = 0;
	struct beamforming_info	*bf_info;
	struct beamformer_entry *entry;


	bf_info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		entry = &bf_info->bfer_entry[i];
		if (entry->used == _FALSE)
			return entry;
	}

	return NULL;
}

static struct beamformee_entry *_get_bfee_free_entry(PADAPTER adapter)
{
	u8 i = 0;
	struct beamforming_info	*bf_info;
	struct beamformee_entry *entry;


	bf_info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		entry = &bf_info->bfee_entry[i];
		if (entry->used == _FALSE)
			return entry;
	}

	return NULL;
}

/*
 * Description:
 *	Get the first entry index of MU Beamformee.
 *
 * Return Value:
 *	Index of the first MU sta.
 *
 * 2015.05.25. Created by tynli.
 *
 */
static u8 _get_first_mu_bfee_entry_idx(PADAPTER adapter, struct beamformee_entry *ignore)
{
	struct beamforming_info *bf_info;
	struct beamformee_entry *entry;
	u8 idx = 0xFF;
	u8 bFound = _FALSE;


	bf_info = GET_BEAMFORM_INFO(adapter);

	for (idx = 0; idx < MAX_BEAMFORMEE_ENTRY_NUM; idx++) {
		entry = &bf_info->bfee_entry[idx];
		if (ignore && (entry == ignore))
			continue;
		if ((entry->used == _TRUE) &&
		    TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_MU)) {
			bFound = _TRUE;
			break;
		}
	}

	if (bFound == _FALSE)
		idx = 0xFF;

	return idx;
}

static void _update_min_sounding_period(PADAPTER adapter, u16 period, u8 leave)
{
	struct beamforming_info *bf_info;
	struct beamformee_entry *entry;
	u8 i = 0;
	u16 min_val = 0xFFFF;


	bf_info = GET_BEAMFORM_INFO(adapter);

	if (_TRUE == leave) {
		/*
		 * When a BFee left,
		 * we need to find the latest min sounding period
		 * from the remaining BFees
		 */
		for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
			entry = &bf_info->bfee_entry[i];
			if ((entry->used == _TRUE)
			    && (entry->sound_period < min_val))
				min_val = entry->sound_period;
		}

		if (min_val == 0xFFFF)
			bf_info->sounding_info.min_sounding_period = 0;
		else
			bf_info->sounding_info.min_sounding_period = min_val;
	} else {
		if ((bf_info->sounding_info.min_sounding_period == 0)
		    || (period < bf_info->sounding_info.min_sounding_period))
			bf_info->sounding_info.min_sounding_period = period;	
	}
}

static struct beamformer_entry *_add_bfer_entry(PADAPTER adapter,
	struct sta_info *sta, u8 bf_cap, u8 sounding_dim, u8 comp_steering)
{
	struct mlme_priv *mlme;
	struct beamforming_info *bf_info;
	struct beamformer_entry *entry;
	u8 *bssid;
	u16 val16;
	u8 i;


	mlme = &adapter->mlmepriv;
	bf_info = GET_BEAMFORM_INFO(adapter);

	entry = beamforming_get_bfer_entry_by_addr(adapter, sta->hwaddr);
	if (!entry) {
		entry = _get_bfer_free_entry(adapter);
		if (!entry)
			return NULL;
	}

	entry->used = _TRUE;

	if (check_fwstate(mlme, WIFI_AP_STATE)) {
		bssid = adapter_mac_addr(adapter);
		/* BSSID[44:47] xor BSSID[40:43] */
		val16 = ((bssid[5] & 0xF0) >> 4) ^ (bssid[5] & 0xF);
		/* (dec(A) + dec(B)*32) mod 512 */
		entry->p_aid = (sta->aid + val16 * 32) & 0x1FF;
		entry->g_id = 63;
	} else if ((check_fwstate(mlme, WIFI_ADHOC_STATE) == _TRUE)
		   || (check_fwstate(mlme, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
		entry->p_aid = 0;
		entry->g_id = 63;
	} else {
		bssid = sta->hwaddr;
		/* BSSID[39:47] */
		entry->p_aid = (bssid[5] << 1) | (bssid[4] >> 7);
		entry->g_id = 0;
	}
	RTW_INFO("%s: p_aid=0x%04x g_id=0x%04x aid=0x%x\n",
		 __FUNCTION__, entry->p_aid, entry->g_id, sta->aid);

	_rtw_memcpy(entry->mac_addr, sta->hwaddr, ETH_ALEN);
	entry->cap = bf_cap;
	entry->state = BEAMFORM_ENTRY_HW_STATE_ADD_INIT;
	entry->NumofSoundingDim = sounding_dim;

	if (TEST_FLAG(bf_cap, BEAMFORMER_CAP_VHT_MU)) {
		bf_info->beamformer_mu_cnt += 1;
		entry->aid = sta->aid;
	} else if (TEST_FLAG(bf_cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
		bf_info->beamformer_su_cnt += 1;

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMER_SU; i++) {
			if ((bf_info->beamformer_su_reg_maping & BIT(i)) == 0) {
				bf_info->beamformer_su_reg_maping |= BIT(i);
				entry->su_reg_index = i;
				break;
			}
		}
		RTW_INFO("%s: Add BFer entry beamformer_su_reg_maping=%#X, su_reg_index=%d\n",
			 __FUNCTION__, bf_info->beamformer_su_reg_maping, entry->su_reg_index);
	}

	return entry;
}

static struct beamformee_entry *_add_bfee_entry(PADAPTER adapter,
	struct sta_info *sta, u8 bf_cap, u8 sounding_dim, u8 comp_steering)
{
	struct mlme_priv *mlme;
	struct beamforming_info *bf_info;
	struct beamformee_entry *entry;
	u8 *bssid;
	u16 val16;
	u8 i;


	mlme = &adapter->mlmepriv;
	bf_info = GET_BEAMFORM_INFO(adapter);

	entry = beamforming_get_bfee_entry_by_addr(adapter, sta->hwaddr);
	if (!entry) {
		entry = _get_bfee_free_entry(adapter);
		if (!entry)
			return NULL;
	}

	entry->used = _TRUE;
	entry->aid = sta->aid;
	entry->mac_id = sta->mac_id;
	entry->sound_bw = sta->bw_mode;

	if (check_fwstate(mlme, WIFI_AP_STATE)) {
		bssid = adapter_mac_addr(adapter);
		/* BSSID[44:47] xor BSSID[40:43] */
		val16 = ((bssid[5] & 0xF0) >> 4) ^ (bssid[5] & 0xF);
		/* (dec(A) + dec(B)*32) mod 512 */
		entry->p_aid = (sta->aid + val16 * 32) & 0x1FF;
		entry->g_id = 63;
	} else if (check_fwstate(mlme, WIFI_ADHOC_STATE) || check_fwstate(mlme, WIFI_ADHOC_MASTER_STATE)) {
		entry->p_aid = 0;
		entry->g_id = 63;
	} else {
		bssid = sta->hwaddr;
		/* BSSID[39:47] */
		entry->p_aid = (bssid[5] << 1) | (bssid[4] >> 7);
		entry->g_id = 0;
	}

	_rtw_memcpy(entry->mac_addr, sta->hwaddr, ETH_ALEN);
	entry->txbf = _FALSE;
	entry->sounding = _FALSE;
	entry->sound_period = 40;
	entry->cap = bf_cap;

	_update_min_sounding_period(adapter, entry->sound_period, _FALSE);
	entry->SoundCnt = GetInitSoundCnt(entry->sound_period, bf_info->sounding_info.min_sounding_period);

	entry->LogStatusFailCnt = 0;

	entry->NumofSoundingDim = sounding_dim;
	entry->CompSteeringNumofBFer = comp_steering;
	entry->state = BEAMFORM_ENTRY_HW_STATE_ADD_INIT;

	if (TEST_FLAG(bf_cap, BEAMFORMEE_CAP_VHT_MU)) {
		bf_info->beamformee_mu_cnt += 1;
		bf_info->first_mu_bfee_index = _get_first_mu_bfee_entry_idx(adapter, NULL);

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMEE_MU; i++) {
			if ((bf_info->beamformee_mu_reg_maping & BIT(i)) == 0) {
				bf_info->beamformee_mu_reg_maping |= BIT(i);
				entry->mu_reg_index = i;				
				break;
			}
		}
		RTW_INFO("%s: Add BFee entry beamformee_mu_reg_maping=%#X, mu_reg_index=%d\n",
			 __FUNCTION__, bf_info->beamformee_mu_reg_maping, entry->mu_reg_index);

	} else if (TEST_FLAG(bf_cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
		bf_info->beamformee_su_cnt += 1;

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMEE_SU; i++) {
			if ((bf_info->beamformee_su_reg_maping & BIT(i)) == 0) {
				bf_info->beamformee_su_reg_maping |= BIT(i);
				entry->su_reg_index = i;
				break;
			}
		}
		RTW_INFO("%s: Add BFee entry beamformee_su_reg_maping=%#X, su_reg_index=%d\n",
			 __FUNCTION__, bf_info->beamformee_su_reg_maping, entry->su_reg_index);
	}

	return entry;
}

static void _remove_bfer_entry(PADAPTER adapter, struct beamformer_entry *entry)
{
	struct beamforming_info *bf_info;


	bf_info = GET_BEAMFORM_INFO(adapter);

	entry->state = BEAMFORM_ENTRY_HW_STATE_DELETE_INIT;

	if (TEST_FLAG(entry->cap, BEAMFORMER_CAP_VHT_MU)) {
		bf_info->beamformer_mu_cnt -= 1;
		_rtw_memset(entry->gid_valid, 0, 8);
		_rtw_memset(entry->user_position, 0, 16);
	} else if (TEST_FLAG(entry->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
		bf_info->beamformer_su_cnt -= 1;
	}

	if (bf_info->beamformer_mu_cnt == 0)
		bf_info->beamforming_cap &= ~BEAMFORMEE_CAP_VHT_MU;
	if (bf_info->beamformer_su_cnt == 0)
		bf_info->beamforming_cap &= ~(BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT);
}

static void _remove_bfee_entry(PADAPTER adapter, struct beamformee_entry *entry)
{
	struct beamforming_info *bf_info;


	bf_info = GET_BEAMFORM_INFO(adapter);

	entry->state = BEAMFORM_ENTRY_HW_STATE_DELETE_INIT;

	if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_MU)) {
		bf_info->beamformee_mu_cnt -= 1;
		bf_info->first_mu_bfee_index = _get_first_mu_bfee_entry_idx(adapter, entry);
	} else if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
		bf_info->beamformee_su_cnt -= 1;
	}

	if (bf_info->beamformee_mu_cnt == 0)
		bf_info->beamforming_cap &= ~BEAMFORMER_CAP_VHT_MU;
	if (bf_info->beamformee_su_cnt == 0)
		bf_info->beamforming_cap &= ~(BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT);

	_update_min_sounding_period(adapter, 0, _TRUE);
}

/*
 * Parameters
 *	adapter		struct _adapter*
 *	sta		struct sta_info*
 *	sta_bf_cap	beamforming capabe of sta
 *	sounding_dim	Number of Sounding Dimensions
 *	comp_steering	Compressed Steering Number of Beamformer Antennas Supported
 */
static void _get_sta_beamform_cap(PADAPTER adapter, struct sta_info *sta,
	u8 *sta_bf_cap, u8 *sounding_dim, u8 *comp_steering)
{
	struct ht_priv *ht;
#ifdef CONFIG_80211AC_VHT
	struct vht_priv *vht;
#endif /* CONFIG_80211AC_VHT */
	u16 bf_cap;


	*sta_bf_cap = 0;
	*sounding_dim = 0;
	*comp_steering = 0;

	ht = &adapter->mlmepriv.htpriv;
#ifdef CONFIG_80211AC_VHT
	vht = &adapter->mlmepriv.vhtpriv;
#endif /* CONFIG_80211AC_VHT */

	if (IsSupportedHT(sta->wireless_mode) == _TRUE) {
		/* HT */
		bf_cap = ht->beamform_cap;

		if (TEST_FLAG(bf_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE)) {
			*sta_bf_cap |= BEAMFORMER_CAP_HT_EXPLICIT;
			*sounding_dim = (bf_cap & BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP) >> 6;
		}
		if (TEST_FLAG(bf_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) {
			*sta_bf_cap |= BEAMFORMEE_CAP_HT_EXPLICIT;
			*comp_steering = (bf_cap & BEAMFORMING_HT_BEAMFORMER_STEER_NUM) >> 4;
		}
	}

#ifdef CONFIG_80211AC_VHT
	if (IsSupportedVHT(sta->wireless_mode) == _TRUE) {
		/* VHT */
		bf_cap = vht->beamform_cap;

		/* We are SU Beamformee because the STA is SU Beamformer */
		if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE)) {
			*sta_bf_cap |= BEAMFORMER_CAP_VHT_SU;

			/* We are MU Beamformee because the STA is MU Beamformer */
			if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE))
				*sta_bf_cap |= BEAMFORMER_CAP_VHT_MU;

			*sounding_dim = (bf_cap & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM) >> 12;
		}
		/* We are SU Beamformer because the STA is SU Beamformee */
		if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE)) {
			*sta_bf_cap |= BEAMFORMEE_CAP_VHT_SU;

			/* We are MU Beamformer because the STA is MU Beamformee */
			if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE))
				*sta_bf_cap |= BEAMFORMEE_CAP_VHT_MU;

			*comp_steering = (bf_cap & BEAMFORMING_VHT_BEAMFORMER_STS_CAP) >> 8;
		}
	}
#endif /* CONFIG_80211AC_VHT */
}
/*
 * Return:
 *	_TRUE	success
 *	_FALSE	fail
 */
static u8 _init_entry(PADAPTER adapter, struct sta_info *sta)
{
	struct mlme_priv *mlme;
	struct ht_priv *htpriv;
#ifdef CONFIG_80211AC_VHT
	struct vht_priv *vhtpriv;
#endif
	struct mlme_ext_priv *mlme_ext;
	struct sta_info *sta_real;
	struct beamformer_entry *bfer = NULL;
	struct beamformee_entry *bfee = NULL;
	u8 *ra;
	u8 wireless_mode;
	u8 sta_bf_cap;
	u8 sounding_dim = 0; /* number of sounding dimensions */
	u8 comp_steering_num = 0; /* compressed steering number */


	mlme = &adapter->mlmepriv;
	htpriv = &mlme->htpriv;
#ifdef CONFIG_80211AC_VHT
	vhtpriv = &mlme->vhtpriv;
#endif
	mlme_ext = &adapter->mlmeextpriv;
	ra = sta->hwaddr;
	wireless_mode = sta->wireless_mode;
	sta_real = rtw_get_stainfo(&adapter->stapriv, ra);

	/* The current setting does not support Beaforming */
	if ((IsSupportedHT(wireless_mode) == _FALSE)
	    && (IsSupportedVHT(wireless_mode) == _FALSE))
		return _FALSE;

	if ((0 == htpriv->beamform_cap)
#ifdef CONFIG_80211AC_VHT
	    && (0 == vhtpriv->beamform_cap)
#endif
	   ) {
		RTW_INFO("The configuration disabled Beamforming! Skip...\n");
		return _FALSE;
	}

	_get_sta_beamform_cap(adapter, sta,
			      &sta_bf_cap, &sounding_dim, &comp_steering_num);
	RTW_INFO("STA Beamforming Capability=0x%02X\n", sta_bf_cap);

	if (sta_bf_cap == BEAMFORMING_CAP_NONE)
		return _FALSE;

	if ((sta_bf_cap & BEAMFORMEE_CAP_HT_EXPLICIT)
	    || (sta_bf_cap & BEAMFORMEE_CAP_VHT_SU)
	    || (sta_bf_cap & BEAMFORMEE_CAP_VHT_MU))
		sta_bf_cap |= BEAMFORMEE_CAP;
	else
		sta_bf_cap |= BEAMFORMER_CAP;

	if (sta_bf_cap & BEAMFORMER_CAP) {
		/* The other side is beamformer */
		bfer = _add_bfer_entry(adapter, sta, sta_bf_cap, sounding_dim, comp_steering_num);
		if (bfer == NULL) {
			RTW_ERR("%s: Fail to allocate bfer entry!\n", __FUNCTION__);
			return _FALSE;
		}

		sta_real->txbf_paid = bfer->p_aid;
		sta_real->txbf_gid = bfer->g_id;
	} else {
		/* The other side is beamformee */
		bfee = _add_bfee_entry(adapter, sta, sta_bf_cap, sounding_dim, comp_steering_num);
		if (bfee == NULL) {
			RTW_ERR("%s: Fail to allocate bfee entry!\n", __FUNCTION__);
			return _FALSE;
		}

		sta_real->txbf_paid = bfee->p_aid;
		sta_real->txbf_gid = bfee->g_id;
	}

	return _TRUE;
}

static void _deinit_entry(PADAPTER adapter, u8 *ra)
{
	struct beamforming_info *bf_info;
	struct beamformer_entry *bfer = NULL;
	struct beamformee_entry *bfee = NULL;
	u8 bHwStateAddInit = _FALSE;


	RTW_INFO("+%s\n", __FUNCTION__);

	bf_info = GET_BEAMFORM_INFO(adapter);
	bfer = beamforming_get_bfer_entry_by_addr(adapter, ra);
	bfee = beamforming_get_bfee_entry_by_addr(adapter, ra);

	if (!bfer && !bfee) {
		RTW_WARN("%s: " MAC_FMT " is neither beamforming ee or er!!\n",
			__FUNCTION__, MAC_ARG(ra));
		return;
	}

	if (bfer && bfee)
		RTW_ERR("%s: " MAC_FMT " is both beamforming ee & er!!\n",
			__FUNCTION__, MAC_ARG(ra));

	if (bfer)
		_remove_bfer_entry(adapter, bfer);

	if (bfee)
		_remove_bfee_entry(adapter, bfee);

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, ra);

	RTW_DBG("-%s\n", __FUNCTION__);
}

void _beamforming_reset(PADAPTER adapter)
{
	RTW_ERR("%s: Not ready!!\n", __FUNCTION__);
}

void beamforming_enter(PADAPTER adapter, void *sta)
{
	u8 ret;

	ret = _init_entry(adapter, (struct sta_info *)sta);
	if (ret == _FALSE)
		return;

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_ENTER, sta);
}

void beamforming_leave(PADAPTER adapter, u8 *ra)
{
	if (ra == NULL)
		_beamforming_reset(adapter);
	else
		_deinit_entry(adapter, ra);
}

void beamforming_sounding_fail(PADAPTER adapter)
{
	RTW_ERR("+%s: not implemented yet!\n", __FUNCTION__);
}

u8 beamforming_send_vht_gid_mgnt_packet(PADAPTER adapter, struct beamformee_entry *entry)
{
	struct xmit_priv *xmitpriv;
	struct mlme_priv *mlmepriv;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *attrib;
	struct rtw_ieee80211_hdr *wlanhdr;
	u8 *pframe;


	xmitpriv = &adapter->xmitpriv;
	mlmepriv = &adapter->mlmepriv;

	pmgntframe = alloc_mgtxmitframe(xmitpriv);
	if (!pmgntframe)
		return _FALSE;

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	attrib->rate = MGN_6M;
	attrib->bwmode = CHANNEL_WIDTH_20;
	attrib->subtype = WIFI_ACTION;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)pmgntframe->buf_addr + TXDESC_OFFSET;
	wlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	wlanhdr->frame_ctl = 0;
	SetFrameSubType(pframe, attrib->subtype);
	SetDuration(pframe, 0);
	SetFragNum(pframe, 0);
	SetSeqNum(pframe, 0);

	_rtw_memcpy(wlanhdr->addr1, entry->mac_addr, ETH_ALEN);
	_rtw_memcpy(wlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memcpy(wlanhdr->addr3, get_bssid(mlmepriv), ETH_ALEN);

	pframe[24] = RTW_WLAN_CATEGORY_VHT;
	pframe[25] = RTW_WLAN_ACTION_VHT_GROUPID_MANAGEMENT;
	_rtw_memcpy(&pframe[26], entry->gid_valid, 8);
	_rtw_memcpy(&pframe[34], entry->user_position, 16);

	attrib->pktlen = 54;
	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

void beamforming_watchdog(PADAPTER adapter)
{
}
#else /* !RTW_BEAMFORMING_VERSION_2 */

struct beamforming_entry	*beamforming_get_entry_by_addr(struct mlme_priv *pmlmepriv, u8 *ra, u8 *idx)
{
	u8	i = 0;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for (i = 0; i < BEAMFORMING_ENTRY_NUM; i++) {
		if (pBeamInfo->beamforming_entry[i].bUsed &&
		    (_rtw_memcmp(ra, pBeamInfo->beamforming_entry[i].mac_addr, ETH_ALEN))) {
			*idx = i;
			return &(pBeamInfo->beamforming_entry[i]);
		}
	}

	return NULL;
}

BEAMFORMING_CAP beamforming_get_entry_beam_cap_by_mac_id(PVOID pmlmepriv , u8 mac_id)
{
	u8	i = 0;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO((struct mlme_priv *)pmlmepriv);
	BEAMFORMING_CAP		BeamformEntryCap = BEAMFORMING_CAP_NONE;

	for (i = 0; i < BEAMFORMING_ENTRY_NUM; i++) {
		if (pBeamInfo->beamforming_entry[i].bUsed &&
		    (mac_id == pBeamInfo->beamforming_entry[i].mac_id)) {
			BeamformEntryCap =  pBeamInfo->beamforming_entry[i].beamforming_entry_cap;
			i = BEAMFORMING_ENTRY_NUM;
		}
	}

	return BeamformEntryCap;
}

struct beamforming_entry	*beamforming_get_free_entry(struct mlme_priv *pmlmepriv, u8 *idx)
{
	u8	i = 0;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for (i = 0; i < BEAMFORMING_ENTRY_NUM; i++) {
		if (pBeamInfo->beamforming_entry[i].bUsed == _FALSE) {
			*idx = i;
			return &(pBeamInfo->beamforming_entry[i]);
		}
	}
	return NULL;
}


struct beamforming_entry	*beamforming_add_entry(PADAPTER adapter, u8 *ra, u16 aid,
	u16 mac_id, CHANNEL_WIDTH bw, BEAMFORMING_CAP beamfrom_cap, u8 *idx)
{
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_entry	*pEntry = beamforming_get_free_entry(pmlmepriv, idx);

	if (pEntry != NULL) {
		pEntry->bUsed = _TRUE;
		pEntry->aid = aid;
		pEntry->mac_id = mac_id;
		pEntry->sound_bw = bw;
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
			u16	BSSID = ((*(adapter_mac_addr(adapter) + 5) & 0xf0) >> 4) ^
				(*(adapter_mac_addr(adapter) + 5) & 0xf); /* BSSID[44:47] xor BSSID[40:43] */
			pEntry->p_aid = (aid + BSSID * 32) & 0x1ff;		/* (dec(A) + dec(B)*32) mod 512 */
			pEntry->g_id = 63;
		} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
			pEntry->p_aid = 0;
			pEntry->g_id = 63;
		} else {
			pEntry->p_aid =  ra[5];						/* BSSID[39:47] */
			pEntry->p_aid = (pEntry->p_aid << 1) | (ra[4] >> 7);
			pEntry->g_id = 0;
		}
		_rtw_memcpy(pEntry->mac_addr, ra, ETH_ALEN);
		pEntry->bSound = _FALSE;

		/* 3 TODO SW/FW sound period */
		pEntry->sound_period = 200;
		pEntry->beamforming_entry_cap = beamfrom_cap;
		pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;


		pEntry->PreLogSeq = 0;	/*Modified by Jeffery @2015-04-13*/
		pEntry->LogSeq = 0;		/*Modified by Jeffery @2014-10-29*/
		pEntry->LogRetryCnt = 0;	/*Modified by Jeffery @2014-10-29*/
		pEntry->LogSuccess = 0;	/*LogSuccess is NOT needed to be accumulated, so  LogSuccessCnt->LogSuccess, 2015-04-13, Jeffery*/
		pEntry->ClockResetTimes = 0;	/*Modified by Jeffery @2015-04-13*/
		pEntry->LogStatusFailCnt = 0;

		return pEntry;
	} else
		return NULL;
}

BOOLEAN	beamforming_remove_entry(struct mlme_priv *pmlmepriv, u8 *ra, u8 *idx)
{
	struct beamforming_entry	*pEntry = beamforming_get_entry_by_addr(pmlmepriv, ra, idx);

	if (pEntry != NULL) {
		pEntry->bUsed = _FALSE;
		pEntry->beamforming_entry_cap = BEAMFORMING_CAP_NONE;
		pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		return _TRUE;
	} else
		return _FALSE;
}

/* Used for BeamformingStart_V1 */
void	beamforming_dym_ndpa_rate(PADAPTER adapter)
{
	u16	NDPARate = MGN_6M;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);

	if (pHalData->MinUndecoratedPWDBForDM > 30) /* link RSSI > 30% */
		NDPARate = MGN_24M;
	else
		NDPARate = MGN_6M;

	/* BW = CHANNEL_WIDTH_20; */
	NDPARate = NDPARate << 8;
	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_RATE, (u8 *)&NDPARate);
}

void beamforming_dym_period(PADAPTER Adapter)
{
	u8	Idx;
	BOOLEAN	bChangePeriod = _FALSE;
	u16	SoundPeriod_SW, SoundPeriod_FW;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);
	struct beamforming_entry	*pBeamformEntry;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO((&Adapter->mlmepriv));
	struct sounding_info		*pSoundInfo = &(pBeamInfo->sounding_info);

	/* 3 TODO  per-client throughput caculation. */

	if (pdvobjpriv->traffic_stat.cur_tx_tp + pdvobjpriv->traffic_stat.cur_rx_tp > 2) {
		SoundPeriod_SW = 32 * 20;
		SoundPeriod_FW = 2;
	} else {
		SoundPeriod_SW = 32 * 2000;
		SoundPeriod_FW = 200;
	}

	for (Idx = 0; Idx < BEAMFORMING_ENTRY_NUM; Idx++) {
		pBeamformEntry = pBeamInfo->beamforming_entry + Idx;
		if (pBeamformEntry->bDefaultCSI) {
			SoundPeriod_SW = 32 * 2000;
			SoundPeriod_FW = 200;
		}

		if (pBeamformEntry->beamforming_entry_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU)) {
			if (pSoundInfo->sound_mode == SOUNDING_FW_VHT_TIMER || pSoundInfo->sound_mode == SOUNDING_FW_HT_TIMER) {
				if (pBeamformEntry->sound_period != SoundPeriod_FW) {
					pBeamformEntry->sound_period = SoundPeriod_FW;
					bChangePeriod = _TRUE;	/* Only FW sounding need to send H2C packet to change sound period. */
				}
			} else if (pBeamformEntry->sound_period != SoundPeriod_SW)
				pBeamformEntry->sound_period = SoundPeriod_SW;
		}
	}

	if (bChangePeriod)
		rtw_hal_set_hwreg(Adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&Idx);
}

BOOLEAN	issue_ht_sw_ndpa_packet(PADAPTER Adapter, u8 *ra, CHANNEL_WIDTH bw, u8 qidx)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	aSifsTime = 0;
	u8	NDPTxRate = 0;

	RTW_INFO("%s: issue_ht_sw_ndpa_packet!\n", __func__);

	NDPTxRate = MGN_MCS8;
	RTW_INFO("%s: NDPTxRate =%d\n", __func__, NDPTxRate);
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL)
		return _FALSE;

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = bw;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetOrderBit(pframe);
	SetFrameSubType(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(Adapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

	duration = 2 * aSifsTime + 40;

	if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SetDuration(pframe, duration);

	/*HT control field*/
	SET_HT_CTRL_CSI_STEERING(pframe + 24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe + 24, 1);

	_rtw_memcpy(pframe + 28, ActionHdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;


}
BOOLEAN	issue_ht_ndpa_packet(PADAPTER Adapter, u8 *ra, CHANNEL_WIDTH bw, u8 qidx)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	aSifsTime = 0;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL)
		return _FALSE;

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);

	if (qidx == BCN_QUEUE_INX)
		pattrib->qsel = QSLT_BEACON;
	pattrib->rate = MGN_MCS8;
	pattrib->bwmode = bw;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetOrderBit(pframe);
	SetFrameSubType(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(Adapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

	duration = 2 * aSifsTime + 40;

	if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SetDuration(pframe, duration);

	/* HT control field */
	SET_HT_CTRL_CSI_STEERING(pframe + 24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe + 24, 1);

	_rtw_memcpy(pframe + 28, ActionHdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;
}

BOOLEAN	beamforming_send_ht_ndpa_packet(PADAPTER Adapter, u8 *ra, CHANNEL_WIDTH bw, u8 qidx)
{
	return issue_ht_ndpa_packet(Adapter, ra, bw, qidx);
}
BOOLEAN	issue_vht_sw_ndpa_packet(PADAPTER Adapter, u8 *ra, u16 aid, CHANNEL_WIDTH bw, u8 qidx)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct rtw_ndpa_sta_info	sta_info;
	u8		 NDPTxRate = 0;

	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	sequence = 0, aSifsTime = 0;

	RTW_INFO("%s: issue_vht_sw_ndpa_packet!\n", __func__);


	NDPTxRate = MGN_VHT2SS_MCS0;
	RTW_INFO("%s: NDPTxRate =%d\n", __func__, NDPTxRate);
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL) {
		RTW_INFO("%s, alloc mgnt frame fail\n", __func__);
		return _FALSE;
	}

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = bw;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetFrameSubType(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(Adapter), ETH_ALEN);

	if (IsSupported5G(pmlmeext->cur_wireless_mode) || IsSupportedHT(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	duration = 2 * aSifsTime + 44;

	if (bw == CHANNEL_WIDTH_80)
		duration += 40;
	else if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SetDuration(pframe, duration);

	sequence = pBeamInfo->sounding_sequence << 2;
	if (pBeamInfo->sounding_sequence >= 0x3f)
		pBeamInfo->sounding_sequence = 0;
	else
		pBeamInfo->sounding_sequence++;

	_rtw_memcpy(pframe + 16, &sequence, 1);
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		aid = 0;

	sta_info.aid = aid;
	sta_info.feedback_type = 0;
	sta_info.nc_index = 0;

	_rtw_memcpy(pframe + 17, (u8 *)&sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);


	return _TRUE;

}
BOOLEAN	issue_vht_ndpa_packet(PADAPTER Adapter, u8 *ra, u16 aid, CHANNEL_WIDTH bw, u8 qidx)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct rtw_ndpa_sta_info	sta_info;
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	sequence = 0, aSifsTime = 0;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return _FALSE;

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);

	if (qidx == BCN_QUEUE_INX)
		pattrib->qsel = QSLT_BEACON;
	pattrib->rate = MGN_VHT2SS_MCS0;
	pattrib->bwmode = bw;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetFrameSubType(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(Adapter), ETH_ALEN);

	if (IsSupported5G(pmlmeext->cur_wireless_mode) || IsSupportedHT(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	duration = 2 * aSifsTime + 44;

	if (bw == CHANNEL_WIDTH_80)
		duration += 40;
	else if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SetDuration(pframe, duration);

	sequence = pBeamInfo->sounding_sequence << 2;
	if (pBeamInfo->sounding_sequence >= 0x3f)
		pBeamInfo->sounding_sequence = 0;
	else
		pBeamInfo->sounding_sequence++;

	_rtw_memcpy(pframe + 16, &sequence, 1);

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		aid = 0;

	sta_info.aid = aid;
	sta_info.feedback_type = 0;
	sta_info.nc_index = 0;

	_rtw_memcpy(pframe + 17, (u8 *)&sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;
}

BOOLEAN	beamforming_send_vht_ndpa_packet(PADAPTER Adapter, u8 *ra, u16 aid, CHANNEL_WIDTH bw, u8 qidx)
{
	return issue_vht_ndpa_packet(Adapter, ra, aid, bw, qidx);
}

BOOLEAN	beamfomring_bSounding(struct beamforming_info *pBeamInfo)
{
	BOOLEAN		bSounding = _FALSE;

	if ((beamforming_get_beamform_cap(pBeamInfo) & BEAMFORMER_CAP) == 0)
		bSounding = _FALSE;
	else
		bSounding = _TRUE;

	return bSounding;
}

u8	beamforming_sounding_idx(struct beamforming_info *pBeamInfo)
{
	u8	idx = 0;
	u8	i;

	for (i = 0; i < BEAMFORMING_ENTRY_NUM; i++) {
		if (pBeamInfo->beamforming_entry[i].bUsed &&
		    (_FALSE == pBeamInfo->beamforming_entry[i].bSound)) {
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

	if (BeamEntry.beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU)
		mode = SOUNDING_FW_VHT_TIMER;
	else if (BeamEntry.beamforming_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)
		mode = SOUNDING_FW_HT_TIMER;
	else
		mode = SOUNDING_STOP_All_TIMER;

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

	if (pSoundInfo->sound_idx < BEAMFORMING_ENTRY_NUM)
		pSoundInfo->sound_mode = beamforming_sounding_mode(pBeamInfo, pSoundInfo->sound_idx);
	else
		pSoundInfo->sound_mode = SOUNDING_STOP_All_TIMER;

	if (SOUNDING_STOP_All_TIMER == pSoundInfo->sound_mode)
		return _FALSE;
	else {
		pSoundInfo->sound_bw = beamforming_sounding_bw(pBeamInfo, pSoundInfo->sound_mode, pSoundInfo->sound_idx);
		pSoundInfo->sound_period = beamforming_sounding_time(pBeamInfo, pSoundInfo->sound_mode, pSoundInfo->sound_idx);
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
	if (pEntry->bUsed == _FALSE) {
		RTW_INFO("Skip Beamforming, no entry for Idx =%d\n", idx);
		return _FALSE;
	}

	pEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSING;
	pEntry->bSound = _TRUE;
	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&idx);

	return _TRUE;
}

void	beamforming_end_fw(PADAPTER adapter)
{
	u8	idx = 0;

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&idx);

	RTW_INFO("%s\n", __FUNCTION__);
}

BOOLEAN	beamforming_start_period(PADAPTER adapter)
{
	BOOLEAN	ret = _TRUE;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct sounding_info		*pSoundInfo = &(pBeamInfo->sounding_info);

	beamforming_dym_ndpa_rate(adapter);

	beamforming_select_beam_entry(pBeamInfo);

	if (pSoundInfo->sound_mode == SOUNDING_FW_VHT_TIMER || pSoundInfo->sound_mode == SOUNDING_FW_HT_TIMER)
		ret = beamforming_start_fw(adapter, pSoundInfo->sound_idx);
	else
		ret = _FALSE;

	RTW_INFO("%s Idx %d Mode %d BW %d Period %d\n", __FUNCTION__,
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


	if (pSoundInfo->sound_mode == SOUNDING_FW_VHT_TIMER || pSoundInfo->sound_mode == SOUNDING_FW_HT_TIMER)
		beamforming_end_fw(adapter);
}

void	beamforming_notify(PADAPTER adapter)
{
	BOOLEAN		bSounding = _FALSE;
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(adapter->mlmepriv));

	bSounding = beamfomring_bSounding(pBeamInfo);

	if (pBeamInfo->beamforming_state == BEAMFORMING_STATE_IDLE) {
		if (bSounding) {
			if (beamforming_start_period(adapter) == _TRUE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_START;
		}
	} else if (pBeamInfo->beamforming_state == BEAMFORMING_STATE_START) {
		if (bSounding) {
			if (beamforming_start_period(adapter) == _FALSE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_END;
		} else {
			beamforming_end_period(adapter);
			pBeamInfo->beamforming_state = BEAMFORMING_STATE_END;
		}
	} else if (pBeamInfo->beamforming_state == BEAMFORMING_STATE_END) {
		if (bSounding) {
			if (beamforming_start_period(adapter) == _TRUE)
				pBeamInfo->beamforming_state = BEAMFORMING_STATE_START;
		}
	} else
		RTW_INFO("%s BeamformState %d\n", __FUNCTION__, pBeamInfo->beamforming_state);

	RTW_INFO("%s BeamformState %d bSounding %d\n", __FUNCTION__, pBeamInfo->beamforming_state, bSounding);
}

BOOLEAN	beamforming_init_entry(PADAPTER	adapter, struct sta_info *psta, u8 *idx)
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

	/* The current setting does not support Beaforming */
	if (0 == phtpriv->beamform_cap
#ifdef CONFIG_80211AC_VHT
	    && 0 == pvhtpriv->beamform_cap
#endif
	   ) {
		RTW_INFO("The configuration disabled Beamforming! Skip...\n");
		return _FALSE;
	}

	aid = psta->aid;
	ra = psta->hwaddr;
	mac_id = psta->mac_id;
	wireless_mode = psta->wireless_mode;
	bw = psta->bw_mode;

	if (IsSupportedHT(wireless_mode) || IsSupportedVHT(wireless_mode)) {
		/* 3 */ /* HT */
		u8	cur_beamform;

		cur_beamform = psta->htpriv.beamform_cap;

		/* We are Beamformee because the STA is Beamformer */
		if (TEST_FLAG(cur_beamform, BEAMFORMING_HT_BEAMFORMER_ENABLE))
			beamform_cap = (BEAMFORMING_CAP)(beamform_cap | BEAMFORMEE_CAP_HT_EXPLICIT);

		/* We are Beamformer because the STA is Beamformee */
		if (TEST_FLAG(cur_beamform, BEAMFORMING_HT_BEAMFORMEE_ENABLE))
			beamform_cap = (BEAMFORMING_CAP)(beamform_cap | BEAMFORMER_CAP_HT_EXPLICIT);
#ifdef CONFIG_80211AC_VHT
		if (IsSupportedVHT(wireless_mode)) {
			/* 3 */ /* VHT */
			cur_beamform = psta->vhtpriv.beamform_cap;

			/* We are Beamformee because the STA is Beamformer */
			if (TEST_FLAG(cur_beamform, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
				beamform_cap = (BEAMFORMING_CAP)(beamform_cap | BEAMFORMEE_CAP_VHT_SU);
			/* We are Beamformer because the STA is Beamformee */
			if (TEST_FLAG(cur_beamform, BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
				beamform_cap = (BEAMFORMING_CAP)(beamform_cap | BEAMFORMER_CAP_VHT_SU);
		}
#endif /* CONFIG_80211AC_VHT */

		if (beamform_cap == BEAMFORMING_CAP_NONE)
			return _FALSE;

		RTW_INFO("Beamforming Config Capability = 0x%02X\n", beamform_cap);

		pBeamformEntry = beamforming_get_entry_by_addr(pmlmepriv, ra, idx);
		if (pBeamformEntry == NULL) {
			pBeamformEntry = beamforming_add_entry(adapter, ra, aid, mac_id, bw, beamform_cap, idx);
			if (pBeamformEntry == NULL)
				return _FALSE;
			else
				pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		} else {
			/* Entry has been created. If entry is initialing or progressing then errors occur. */
			if (pBeamformEntry->beamforming_entry_state != BEAMFORMING_ENTRY_STATE_INITIALIZED &&
			    pBeamformEntry->beamforming_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
				RTW_INFO("Error State of Beamforming");
				return _FALSE;
			} else
				pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		}

		pBeamformEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		psta->txbf_paid = pBeamformEntry->p_aid;
		psta->txbf_gid = pBeamformEntry->g_id;

		RTW_INFO("%s Idx %d\n", __FUNCTION__, *idx);
	} else
		return _FALSE;

	return _SUCCESS;
}

void	beamforming_deinit_entry(PADAPTER adapter, u8 *ra)
{
	u8	idx = 0;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if (beamforming_remove_entry(pmlmepriv, ra, &idx) == _TRUE)
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, (u8 *)&idx);

	RTW_INFO("%s Idx %d\n", __FUNCTION__, idx);
}

void	beamforming_reset(PADAPTER adapter)
{
	u8	idx = 0;
	struct mlme_priv			*pmlmepriv = &(adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for (idx = 0; idx < BEAMFORMING_ENTRY_NUM; idx++) {
		if (pBeamInfo->beamforming_entry[idx].bUsed == _TRUE) {
			pBeamInfo->beamforming_entry[idx].bUsed = _FALSE;
			pBeamInfo->beamforming_entry[idx].beamforming_entry_cap = BEAMFORMING_CAP_NONE;
			pBeamInfo->beamforming_entry[idx].beamforming_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, (u8 *)&idx);
		}
	}

	RTW_INFO("%s\n", __FUNCTION__);
}

void beamforming_sounding_fail(PADAPTER Adapter)
{
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct beamforming_entry	*pEntry = &(pBeamInfo->beamforming_entry[pBeamInfo->beamforming_cur_idx]);

	pEntry->bSound = _FALSE;
	rtw_hal_set_hwreg(Adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&pBeamInfo->beamforming_cur_idx);
	beamforming_deinit_entry(Adapter, pEntry->mac_addr);
}

void	beamforming_check_sounding_success(PADAPTER Adapter, BOOLEAN status)
{
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct beamforming_entry	*pEntry = &(pBeamInfo->beamforming_entry[pBeamInfo->beamforming_cur_idx]);

	if (status == 1)
		pEntry->LogStatusFailCnt = 0;
	else {
		pEntry->LogStatusFailCnt++;
		RTW_INFO("%s LogStatusFailCnt %d\n", __FUNCTION__, pEntry->LogStatusFailCnt);
	}
	if (pEntry->LogStatusFailCnt > 20) {
		RTW_INFO("%s LogStatusFailCnt > 20, Stop SOUNDING\n", __FUNCTION__);
		/* pEntry->bSound = _FALSE; */
		/* rtw_hal_set_hwreg(Adapter, HW_VAR_SOUNDING_FW_NDPA, (u8 *)&pBeamInfo->beamforming_cur_idx); */
		/* beamforming_deinit_entry(Adapter, pEntry->mac_addr); */
		beamforming_wk_cmd(Adapter, BEAMFORMING_CTRL_SOUNDING_FAIL, NULL, 0, 1);
	}
}

void	beamforming_enter(PADAPTER adapter, PVOID psta)
{
	u8	idx = 0xff;

	if (beamforming_init_entry(adapter, (struct sta_info *)psta, &idx))
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_ENTER, (u8 *)&idx);

	/* RTW_INFO("%s Idx %d\n", __FUNCTION__, idx); */
}

void	beamforming_leave(PADAPTER adapter, u8 *ra)
{
	if (ra == NULL)
		beamforming_reset(adapter);
	else
		beamforming_deinit_entry(adapter, ra);

	beamforming_notify(adapter);
}

BEAMFORMING_CAP beamforming_get_beamform_cap(struct beamforming_info	*pBeamInfo)
{
	u8	i;
	BOOLEAN				bSelfBeamformer = _FALSE;
	BOOLEAN				bSelfBeamformee = _FALSE;
	struct beamforming_entry	beamforming_entry;
	BEAMFORMING_CAP		beamform_cap = BEAMFORMING_CAP_NONE;

	for (i = 0; i < BEAMFORMING_ENTRY_NUM; i++) {
		beamforming_entry = pBeamInfo->beamforming_entry[i];

		if (beamforming_entry.bUsed) {
			if ((beamforming_entry.beamforming_entry_cap & BEAMFORMEE_CAP_VHT_SU) ||
			    (beamforming_entry.beamforming_entry_cap & BEAMFORMEE_CAP_HT_EXPLICIT))
				bSelfBeamformee = _TRUE;
			if ((beamforming_entry.beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU) ||
			    (beamforming_entry.beamforming_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT))
				bSelfBeamformer = _TRUE;
		}

		if (bSelfBeamformer && bSelfBeamformee)
			i = BEAMFORMING_ENTRY_NUM;
	}

	if (bSelfBeamformer)
		beamform_cap |= BEAMFORMER_CAP;
	if (bSelfBeamformee)
		beamform_cap |= BEAMFORMEE_CAP;

	return beamform_cap;
}

void	beamforming_watchdog(PADAPTER Adapter)
{
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO((&(Adapter->mlmepriv)));

	if (pBeamInfo->beamforming_state != BEAMFORMING_STATE_START)
		return;

	beamforming_dym_period(Adapter);
	beamforming_dym_ndpa_rate(Adapter);
}
#endif /* !RTW_BEAMFORMING_VERSION_2 */
#endif/* #if (BEAMFORMING_SUPPORT ==0) - for diver defined beamforming*/

u32	beamforming_get_report_frame(PADAPTER	 Adapter, union recv_frame *precv_frame)
{
	u32	ret = _SUCCESS;
#if (BEAMFORMING_SUPPORT == 1)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);

	ret = Beamforming_GetReportFrame(pDM_Odm, precv_frame);

#else /*(BEAMFORMING_SUPPORT == 0)- for drv beamfoming*/
#ifdef RTW_BEAMFORMING_VERSION_2
	struct beamformee_entry *pBeamformEntry = NULL;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u32 frame_len = precv_frame->u.hdr.len;
	u8 *ta;
	u8 *frame_body;
	u8 category, action;
	u8 *pMIMOCtrlField, *pCSIMatrix;
	u8 Nc = 0, Nr = 0, CH_W = 0;
	u16 CSIMatrixLen = 0;


	RTW_DBG("+%s\n", __FUNCTION__);

	/* Memory comparison to see if CSI report is the same with previous one */
	ta = GetAddr2Ptr(pframe);
	pBeamformEntry = beamforming_get_bfee_entry_by_addr(Adapter, ta);
	if (!pBeamformEntry)
		return _FAIL;

	frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	category = frame_body[0];
	action = frame_body[1];

	if ((category == RTW_WLAN_CATEGORY_VHT)
	    && (action == RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING)) {
		pMIMOCtrlField = pframe + 26; 
		Nc = ((*pMIMOCtrlField) & 0x7) + 1;
		Nr = (((*pMIMOCtrlField) & 0x38) >> 3) + 1;
		CH_W =  (((*pMIMOCtrlField) & 0xC0) >> 6);
		/*
		 * 24+(1+1+3)+2
		 * ==> MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)
		 */
		pCSIMatrix = pMIMOCtrlField + 3 + Nc;
		CSIMatrixLen = frame_len - 26 - 3 - Nc;
	} else if ((category == RTW_WLAN_CATEGORY_HT)
		   && (action == RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING)) {
		pMIMOCtrlField = pframe + 26; 
		Nc = ((*pMIMOCtrlField) & 0x3) + 1;
		Nr = (((*pMIMOCtrlField) & 0xC) >> 2) + 1;
		CH_W = ((*pMIMOCtrlField) & 0x10) >> 4;
		/*
		 * 24+(1+1+6)+2
		 * ==> MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)
		 */
		pCSIMatrix = pMIMOCtrlField + 6 + Nr;
		CSIMatrixLen = frame_len  - 26 - 6 - Nr;
	}
	
	RTW_INFO("%s: pkt type=%d-%d, Nc=%d, Nr=%d, CH_W=%d\n",
		 __FUNCTION__, category, action, Nc, Nr, CH_W);
#else /* !RTW_BEAMFORMING_VERSION_2 */
	struct beamforming_entry	*pBeamformEntry = NULL;
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	u8	*pframe = precv_frame->u.hdr.rx_data;
	u32	frame_len = precv_frame->u.hdr.len;
	u8	*ta;
	u8	idx, offset;

	/*RTW_INFO("beamforming_get_report_frame\n");*/

	/*Memory comparison to see if CSI report is the same with previous one*/
	ta = GetAddr2Ptr(pframe);
	pBeamformEntry = beamforming_get_entry_by_addr(pmlmepriv, ta, &idx);
	if (pBeamformEntry->beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU)
		offset = 31;	/*24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)*/
	else if (pBeamformEntry->beamforming_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)
		offset = 34;	/*24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)*/
	else
		return ret;

	/*RTW_INFO("%s MacId %d offset=%d\n", __FUNCTION__, pBeamformEntry->mac_id, offset);*/

	if (_rtw_memcmp(pBeamformEntry->PreCsiReport + offset, pframe + offset, frame_len - offset) == _FALSE)
		pBeamformEntry->DefaultCsiCnt = 0;
	else
		pBeamformEntry->DefaultCsiCnt++;

	_rtw_memcpy(&pBeamformEntry->PreCsiReport, pframe, frame_len);

	pBeamformEntry->bDefaultCSI = _FALSE;

	if (pBeamformEntry->DefaultCsiCnt > 20)
		pBeamformEntry->bDefaultCSI = _TRUE;
	else
		pBeamformEntry->bDefaultCSI = _FALSE;
#endif /* !RTW_BEAMFORMING_VERSION_2 */
#endif
	return ret;
}

void	beamforming_get_ndpa_frame(PADAPTER	 Adapter, union recv_frame *precv_frame)
{
#if (BEAMFORMING_SUPPORT == 1)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);

	Beamforming_GetNDPAFrame(pDM_Odm, precv_frame);

#else /*(BEAMFORMING_SUPPORT == 0)- for drv beamfoming*/
#ifdef RTW_BEAMFORMING_VERSION_2
	RTW_DBG("+%s\n", __FUNCTION__);
#else /* !RTW_BEAMFORMING_VERSION_2 */
	u8	*ta;
	u8	idx, Sequence;
	u8	*pframe = precv_frame->u.hdr.rx_data;
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_entry	*pBeamformEntry = NULL;

	/*RTW_INFO("beamforming_get_ndpa_frame\n");*/

	if (IS_HARDWARE_TYPE_8812(Adapter) == _FALSE)
		return;
	else if (GetFrameSubType(pframe) != WIFI_NDPA)
		return;

	ta = GetAddr2Ptr(pframe);
	/*Remove signaling TA. */
	ta[0] = ta[0] & 0xFE;

	pBeamformEntry = beamforming_get_entry_by_addr(pmlmepriv, ta, &idx);

	if (pBeamformEntry == NULL)
		return;
	else if (!(pBeamformEntry->beamforming_entry_cap & BEAMFORMEE_CAP_VHT_SU))
		return;
	/*LogSuccess: As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is NO LONGER needed !2015-04-10, Jeffery*/
	/*ClockResetTimes: While BFer entry always doesn't receive our CSI, clock will reset again and again.So ClockResetTimes is limited to 5 times.2015-04-13, Jeffery*/
	else if ((pBeamformEntry->LogSuccess == 1) || (pBeamformEntry->ClockResetTimes == 5)) {
		RTW_INFO("[%s] LogSeq=%d, PreLogSeq=%d\n", __func__, pBeamformEntry->LogSeq, pBeamformEntry->PreLogSeq);
		return;
	}

	Sequence = (pframe[16]) >> 2;
	RTW_INFO("[%s] Start, Sequence=%d, LogSeq=%d, PreLogSeq=%d, LogRetryCnt=%d, ClockResetTimes=%d, LogSuccess=%d\n",
		__func__, Sequence, pBeamformEntry->LogSeq, pBeamformEntry->PreLogSeq, pBeamformEntry->LogRetryCnt, pBeamformEntry->ClockResetTimes, pBeamformEntry->LogSuccess);

	if ((pBeamformEntry->LogSeq != 0) && (pBeamformEntry->PreLogSeq != 0)) {
		/*Success condition*/
		if ((pBeamformEntry->LogSeq != Sequence) && (pBeamformEntry->PreLogSeq != pBeamformEntry->LogSeq)) {
			/* break option for clcok reset, 2015-03-30, Jeffery */
			pBeamformEntry->LogRetryCnt = 0;
			/*As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is no longer needed.*/
			/*That is, LogSuccess is NOT needed to be reset to zero, 2015-04-13, Jeffery*/
			pBeamformEntry->LogSuccess = 1;

		} else {/*Fail condition*/

			if (pBeamformEntry->LogRetryCnt == 5) {
				pBeamformEntry->ClockResetTimes++;
				pBeamformEntry->LogRetryCnt = 0;

				RTW_INFO("[%s] Clock Reset!!! ClockResetTimes=%d\n",  __func__, pBeamformEntry->ClockResetTimes);
				beamforming_wk_cmd(Adapter, BEAMFORMING_CTRL_SOUNDING_CLK, NULL, 0, 1);

			} else
				pBeamformEntry->LogRetryCnt++;
		}
	}

	/*Update LogSeq & PreLogSeq*/
	pBeamformEntry->PreLogSeq = pBeamformEntry->LogSeq;
	pBeamformEntry->LogSeq = Sequence;
#endif /* !RTW_BEAMFORMING_VERSION_2 */
#endif

}

/* octets in data header, no WEP */
#define sMacHdrLng						24
/* VHT Group ID (GID) Management Frame */
#define FRAME_OFFSET_VHT_GID_MGNT_MEMBERSHIP_STATUS_ARRAY	(sMacHdrLng + 2)
#define FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY		(sMacHdrLng + 10)
/* VHT GID Management Frame Info */
#define GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(_pStart)	LE_BITS_TO_1BYTE((_pStart), 0, 8)
#define GET_VHT_GID_MGNT_INFO_USER_POSITION(_pStart)		LE_BITS_TO_1BYTE((_pStart), 0, 8)
/*
 * Description:
 *	On VHT GID management frame by an MU beamformee.
 *
 * 2015.05.20. Created by tynli.
 */
u32 beamforming_get_vht_gid_mgnt_frame(PADAPTER adapter, union recv_frame *precv_frame)
{
#ifdef RTW_BEAMFORMING_VERSION_2
	u8 *ta;
	u8 idx;
	u8 *pframe;
	u8 *pBuffer = NULL;
	struct beamformer_entry *bfer = NULL;


	RTW_DBG("+%s\n", __FUNCTION__);

	pframe = precv_frame->u.hdr.rx_data;
	/* Get BFer entry by Addr2 */
	ta = GetAddr2Ptr(pframe);
	/* Remove signaling TA */
	ta[0] &= 0xFE;

	bfer = beamforming_get_bfer_entry_by_addr(adapter, ta);
	if (!bfer) {
		RTW_INFO("%s: Cannot find BFer entry!!\n", __FUNCTION__);
		return _FAIL;
	}

	/* Parsing Membership Status Array */
	pBuffer = pframe + FRAME_OFFSET_VHT_GID_MGNT_MEMBERSHIP_STATUS_ARRAY;
	for (idx = 0; idx < 8; idx++)
		bfer->gid_valid[idx] = GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(pBuffer+idx);

	/* Parsing User Position Array */
	pBuffer = pframe + FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY;
	for (idx = 0; idx < 16; idx++)
		bfer->user_position[idx] = GET_VHT_GID_MGNT_INFO_USER_POSITION(pBuffer+idx);

	/* Config HW GID table */
	beamforming_wk_cmd(adapter, BEAMFORMING_CTRL_SET_GID_TABLE, (u8*)&bfer, sizeof(struct beamformer_entry *), 1);

	return _SUCCESS;
#else /* !RTW_BEAMFORMING_VERSION_2 */
	return _FAIL;
#endif /* !RTW_BEAMFORMING_VERSION_2 */
}

void	beamforming_wk_hdl(_adapter *padapter, u8 type, u8 *pbuf)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	_func_enter_;

#if (BEAMFORMING_SUPPORT == 1) /*(BEAMFORMING_SUPPORT == 1)- for PHYDM beamfoming*/
	switch (type) {
	case BEAMFORMING_CTRL_ENTER: {
		struct sta_info	*psta = (PVOID)pbuf;
		u16			staIdx = psta->mac_id;

		Beamforming_Enter(pDM_Odm, staIdx);
		break;
	}
	case BEAMFORMING_CTRL_LEAVE:
		Beamforming_Leave(pDM_Odm, pbuf);
		break;
	default:
		break;

	}
#else /*(BEAMFORMING_SUPPORT == 0)- for drv beamfoming*/
	switch (type) {
	case BEAMFORMING_CTRL_ENTER:
		beamforming_enter(padapter, (PVOID)pbuf);
		break;

	case BEAMFORMING_CTRL_LEAVE:
		beamforming_leave(padapter, pbuf);
		break;

	case BEAMFORMING_CTRL_SOUNDING_FAIL:
		beamforming_sounding_fail(padapter);
		break;

	case BEAMFORMING_CTRL_SOUNDING_CLK:
		rtw_hal_set_hwreg(padapter, HW_VAR_SOUNDING_CLK, NULL);
		break;

	case BEAMFORMING_CTRL_SET_GID_TABLE:
		rtw_hal_set_hwreg(padapter, HW_VAR_SOUNDING_SET_GID_TABLE, *(void**)pbuf);
		break;

	default:
		break;
	}
#endif
	_func_exit_;
}

u8	beamforming_wk_cmd(_adapter *padapter, s32 type, u8 *pbuf, s32 size, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	_func_enter_;

	if (enqueue) {
		u8	*wk_buf;

		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		if (pbuf != NULL) {
			wk_buf = rtw_zmalloc(size);
			if (wk_buf == NULL) {
				rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
				rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
				res = _FAIL;
				goto exit;
			}

			_rtw_memcpy(wk_buf, pbuf, size);
		} else {
			wk_buf = NULL;
			size = 0;
		}

		pdrvextra_cmd_parm->ec_id = BEAMFORMING_WK_CID;
		pdrvextra_cmd_parm->type = type;
		pdrvextra_cmd_parm->size = size;
		pdrvextra_cmd_parm->pbuf = wk_buf;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else
		beamforming_wk_hdl(padapter, type, pbuf);

exit:

	_func_exit_;

	return res;
}

void update_attrib_txbf_info(_adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	if (psta) {
		pattrib->txbf_g_id = psta->txbf_gid;
		pattrib->txbf_p_aid = psta->txbf_paid;
	}
}

#endif
