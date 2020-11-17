/* SPDX-License-Identifier: GPL-2.0 */
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
#define _RTW_RSON_C_

#include <drv_types.h>

#ifdef CONFIG_RTW_REPEATER_SON

/********	Custommize Part	***********************/

unsigned char	RTW_RSON_OUI[] = {0xFA, 0xFA, 0xFA};
#define RSON_SCORE_DIFF_TH				8

/*
	Calculate the corresponding score.
*/
inline u8 rtw_cal_rson_score(struct rtw_rson_struct *cand_rson_data, NDIS_802_11_RSSI  Rssi)
{
	if ((cand_rson_data->hopcnt == RTW_RSON_HC_NOTREADY)
		|| (cand_rson_data->connectible == RTW_RSON_DENYCONNECT))
		return RTW_RSON_SCORE_NOTCNNT;

	return RTW_RSON_SCORE_MAX - (cand_rson_data->hopcnt * 10) + (Rssi/10);
}

/*************************************************/


static u8 rtw_rson_block_bssid_idx = 0;
u8 rtw_rson_block_bssid[10][6] = {
			/*{0x02, 0xE0, 0x4C, 0x07, 0xC3, 0xF6}*/
};

/* fake root, regard a real AP as a SO root */
static u8 rtw_rson_root_bssid_idx = 0;
u8 rtw_rson_root_bssid[10][6] = {
			/*{0x1c, 0x5f, 0x2b, 0x5a, 0x60, 0x24}*/
};

int is_match_bssid(u8 *mac, u8 bssid_array[][6], int num)
{
	int i;

	for (i = 0; i < num; i++)
		if (_rtw_memcmp(mac, bssid_array[i], 6) == _TRUE)
			return _TRUE;
	return _FALSE;
}

void init_rtw_rson_data(struct dvobj_priv *dvobj)
{
	/*Aries  todo.  if pdvobj->rson_data.ver == 1 */
	dvobj->rson_data.ver = RTW_RSON_VER;
	dvobj->rson_data.id = CONFIG_RTW_REPEATER_SON_ID;
#ifdef CONFIG_RTW_REPEATER_SON_ROOT
	dvobj->rson_data.hopcnt = RTW_RSON_HC_ROOT;
	dvobj->rson_data.connectible = RTW_RSON_ALLOWCONNECT;
#else
	dvobj->rson_data.hopcnt = RTW_RSON_HC_NOTREADY;
	dvobj->rson_data.connectible = RTW_RSON_DENYCONNECT;
#endif
	dvobj->rson_data.loading = 0;
	_rtw_memset(dvobj->rson_data.res, 0xAA, sizeof(dvobj->rson_data.res));
}

void	rtw_rson_get_property_str(_adapter *padapter, char *rson_data_str)
{
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);

	sprintf(rson_data_str, "version : \t%d\nid : \t\t%08x\nhop count : \t%d\nconnectible : \t%s\nloading : \t%d\nreserve : \t%16ph\n",
		pdvobj->rson_data.ver,
		pdvobj->rson_data.id,
		pdvobj->rson_data.hopcnt,
		pdvobj->rson_data.connectible ? "connectable":"unconnectable",
		pdvobj->rson_data.loading,
		pdvobj->rson_data.res);
}

int str2hexbuf(char *str, u8 *hexbuf, int len)
{
	u8 *p;
	int i, slen, idx = 0;

	p = (unsigned char *)str;
	if ((*p != '0') || (*(p+1) != 'x'))
		return _FALSE;
	slen = strlen(str);
	if (slen > (len*2) + 2)
		return _FALSE;
	p += 2;
	for (i = 0 ; i < len; i++, idx = idx+2) {
		hexbuf[i] = key_2char2num(p[idx], p[idx + 1]);
		if (slen <= idx+2)
			break;
	}
	return _TRUE;
}

int rtw_rson_set_property(_adapter *padapter, char *field, char *value)
{
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	int num = 0;

	if (_rtw_memcmp(field, (u8 *)"ver", 3) == _TRUE)
		pdvobj->rson_data.ver = rtw_atoi(value);
	else if (_rtw_memcmp(field, (u8 *)"id", 2) == _TRUE)
		num = sscanf(value, "%08x",   &(pdvobj->rson_data.id));
	else if (_rtw_memcmp(field, (u8 *)"hc", 2) == _TRUE)
		num = sscanf(value, "%hhu", &(pdvobj->rson_data.hopcnt));
	else if (_rtw_memcmp(field, (u8 *)"cnt", 3) == _TRUE)
		num = sscanf(value, "%hhu", &(pdvobj->rson_data.connectible));
	else if (_rtw_memcmp(field, (u8 *)"loading", 2) == _TRUE)
		num = sscanf(value, "%hhu", &(pdvobj->rson_data.loading));
	else if (_rtw_memcmp(field, (u8 *)"res", 2) == _TRUE) {
		str2hexbuf(value, pdvobj->rson_data.res, 16);
		return 1;
	} else
		return _FALSE;
	return num;
}

/*
	return :	TRUE  -- competitor is taking advantage than condidate
			FALSE -- we should continue keeping candidate
*/
int rtw_rson_choose(struct wlan_network **candidate, struct wlan_network *competitor)
{
	s16 comp_score = 0, cand_score = 0;
	struct rtw_rson_struct rson_cand, rson_comp;

	if (is_match_bssid(competitor->network.MacAddress, rtw_rson_block_bssid, rtw_rson_block_bssid_idx) == _TRUE)
		return _FALSE;

	if ((competitor == NULL)
		|| (rtw_get_rson_struct(&(competitor->network), &rson_comp) != _TRUE)
		|| (rson_comp.id != CONFIG_RTW_REPEATER_SON_ID))
		return _FALSE;

	comp_score = rtw_cal_rson_score(&rson_comp, competitor->network.Rssi);
	if (comp_score == RTW_RSON_SCORE_NOTCNNT)
		return _FALSE;

	if (*candidate == NULL)
		return _TRUE;
	if (rtw_get_rson_struct(&((*candidate)->network), &rson_cand) != _TRUE)
		return _FALSE;

	cand_score = rtw_cal_rson_score(&rson_cand, (*candidate)->network.Rssi);
	RTW_INFO("%s: competitor_score=%d,  candidate_score=%d\n", __func__, comp_score, cand_score);
	if (comp_score - cand_score > RSON_SCORE_DIFF_TH)
		return _TRUE;

	return _FALSE;
}

inline u8 rtw_rson_varify_ie(u8 *p)
{
	u8 *ptr = NULL;
	u8 ver;
	u32 id;
	u8 hopcnt;
	u8 allcnnt;

	ptr = p + 2 + sizeof(RTW_RSON_OUI);
	ver = *ptr;

	/*	for (ver == 1)	*/
	if (ver != 1)
		return _FALSE;

	return _TRUE;
}

/*
	Parsing RTK self-organization vendor IE
*/
int rtw_get_rson_struct(WLAN_BSSID_EX *bssid, struct  rtw_rson_struct *rson_data)
{
	sint  limit = 0;
	u32	len;
	u8	*p;

	if ((rson_data == NULL) || (bssid == NULL))
		return -EINVAL;

	/*	Default		*/
	rson_data->id = 0;
	rson_data->ver = 0;
	rson_data->hopcnt = 0;
	rson_data->connectible = 0;
	rson_data->loading = 0;
	/*	fake root		*/
	if (is_match_bssid(bssid->MacAddress, rtw_rson_root_bssid, rtw_rson_root_bssid_idx) == _TRUE) {
		rson_data->id = CONFIG_RTW_REPEATER_SON_ID;
		rson_data->ver = RTW_RSON_VER;
		rson_data->hopcnt = RTW_RSON_HC_ROOT;
		rson_data->connectible = RTW_RSON_ALLOWCONNECT;
		rson_data->loading = 0;
		return _TRUE;
	}
	limit = bssid->IELength - _BEACON_IE_OFFSET_;

	for (p = bssid->IEs + _BEACON_IE_OFFSET_; ; p += (len + 2)) {
		p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &len, limit);
		limit -= len;
		if ((p == NULL) || (len == 0))
			break;
		if (p && (_rtw_memcmp(p + 2, RTW_RSON_OUI, sizeof(RTW_RSON_OUI)) == _TRUE)
			&& rtw_rson_varify_ie(p)) {
			p = p + 2 + sizeof(RTW_RSON_OUI);
			rson_data->ver = *p;
			/*	for (ver == 1)		*/
			p = p + 1;
			rson_data->id = le32_to_cpup((__le32 *)p);
			p = p + 4;
			rson_data->hopcnt = *p;
			p = p + 1;
			rson_data->connectible = *p;
			p = p + 1;
			rson_data->loading = *p;

			return _TRUE;
		}
	}
	return -EBADMSG;
}

u32 rtw_rson_append_ie(_adapter *padapter, unsigned char *pframe, u32 *len)
{
	u8 *ptr, *ori, ie_len = 0;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
/*	static int iii = 0;*/

	if ((!pdvobj) || (!pframe))
		return 0;
	ptr = ori = pframe;
	*ptr++ = _VENDOR_SPECIFIC_IE_;
	*ptr++ = ie_len = sizeof(RTW_RSON_OUI)+sizeof(pdvobj->rson_data);
	_rtw_memcpy(ptr, RTW_RSON_OUI, sizeof(RTW_RSON_OUI));
	ptr = ptr + sizeof(RTW_RSON_OUI);
	*ptr++ = pdvobj->rson_data.ver;
	*(s32 *)ptr = cpu_to_le32(pdvobj->rson_data.id);
	ptr = ptr + sizeof(pdvobj->rson_data.id);
	*ptr++ = pdvobj->rson_data.hopcnt;
	*ptr++ = pdvobj->rson_data.connectible;
	*ptr++ = pdvobj->rson_data.loading;
	_rtw_memcpy(ptr, pdvobj->rson_data.res, sizeof(pdvobj->rson_data.res));
	pframe = ptr;
/*
	iii = iii % 20;
	if (iii++ == 0)
		RTW_INFO("%s : RTW RSON IE : %20ph\n", __func__, ori);
*/
	*len += (ie_len+2);
	return ie_len;

}

void rtw_rson_do_disconnect(_adapter *padapter)
{
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
#ifndef CONFIG_RTW_REPEATER_SON_ROOT
	pdvobj->rson_data.ver = RTW_RSON_VER;
	pdvobj->rson_data.id = CONFIG_RTW_REPEATER_SON_ID;
	pdvobj->rson_data.hopcnt = RTW_RSON_HC_NOTREADY;
	pdvobj->rson_data.connectible = RTW_RSON_DENYCONNECT;
	pdvobj->rson_data.loading = 0;
	rtw_mi_tx_beacon_hdl(padapter);
#endif
}

void rtw_rson_join_done(_adapter *padapter)
{
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	WLAN_BSSID_EX	*cur_network = NULL;
	struct rtw_rson_struct  rson_data;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	if (!padapter->mlmepriv.cur_network_scanned)
		return;
	cur_network = &(padapter->mlmepriv.cur_network_scanned->network);
	if (rtw_get_rson_struct(cur_network, &rson_data) != _TRUE) {
		RTW_ERR("%s: try to join a improper network(%s)\n", __func__, cur_network->Ssid.Ssid);
		return;
	}

#ifndef CONFIG_RTW_REPEATER_SON_ROOT
	/* update rson_data */
	pdvobj->rson_data.ver = RTW_RSON_VER;
	pdvobj->rson_data.id = rson_data.id;
	pdvobj->rson_data.hopcnt = rson_data.hopcnt + 1;
	pdvobj->rson_data.connectible = RTW_RSON_ALLOWCONNECT;
	pdvobj->rson_data.loading = 0;
	rtw_mi_tx_beacon_hdl(padapter);
#endif
}

int rtw_rson_isupdate_roamcan(struct mlme_priv *mlme
	, struct wlan_network **candidate, struct wlan_network *competitor)
{
	struct rtw_rson_struct  rson_cand, rson_comp, rson_curr;
	s16 comp_score, cand_score, curr_score;

	if ((competitor == NULL)
		|| (rtw_get_rson_struct(&(competitor->network), &rson_comp) != _TRUE)
		|| (rson_comp.id != CONFIG_RTW_REPEATER_SON_ID))
		return _FALSE;

	if (is_match_bssid(competitor->network.MacAddress, rtw_rson_block_bssid, rtw_rson_block_bssid_idx) == _TRUE)
		return _FALSE;

	if ((!mlme->cur_network_scanned)
		|| (mlme->cur_network_scanned == competitor)
		|| (rtw_get_rson_struct(&(mlme->cur_network_scanned->network), &rson_curr)) != _TRUE)
		return _FALSE;

	if (rtw_get_passing_time_ms((u32)competitor->last_scanned) >= mlme->roam_scanr_exp_ms)
		return _FALSE;

	comp_score = rtw_cal_rson_score(&rson_comp, competitor->network.Rssi);
	curr_score = rtw_cal_rson_score(&rson_curr, mlme->cur_network_scanned->network.Rssi);
	if (comp_score - curr_score < RSON_SCORE_DIFF_TH)
		return _FALSE;

	if (*candidate == NULL)
		return _TRUE;

	if (rtw_get_rson_struct(&((*candidate)->network), &rson_cand) != _TRUE) {
		RTW_ERR("%s : Unable to get rson_struct from candidate(%s -- " MAC_FMT")\n",
				__func__, (*candidate)->network.Ssid.Ssid, MAC_ARG((*candidate)->network.MacAddress));
		return _FALSE;
	}
	cand_score = rtw_cal_rson_score(&rson_cand, (*candidate)->network.Rssi);
	RTW_DBG("comp_score=%d , cand_score=%d , curr_score=%d\n", comp_score, cand_score, curr_score);
	if (cand_score < comp_score)
		return _TRUE;

#if 0		/*	Handle 11R protocol	*/
#ifdef CONFIG_RTW_80211R
	if (rtw_chk_ft_flags(adapter, RTW_FT_SUPPORTED)) {
		ptmp = rtw_get_ie(&competitor->network.IEs[12], _MDIE_, &mdie_len, competitor->network.IELength-12);
		if (ptmp) {
			if (!_rtw_memcmp(&pftpriv->mdid, ptmp+2, 2))
				goto exit;

			/*The candidate don't support over-the-DS*/
			if (rtw_chk_ft_flags(adapter, RTW_FT_STA_OVER_DS_SUPPORTED)) {
				if ((rtw_chk_ft_flags(adapter, RTW_FT_OVER_DS_SUPPORTED) && !(*(ptmp+4) & 0x01)) ||
					(!rtw_chk_ft_flags(adapter, RTW_FT_OVER_DS_SUPPORTED) && (*(ptmp+4) & 0x01))) {
					RTW_INFO("FT: ignore the candidate(" MAC_FMT ") for over-the-DS\n", MAC_ARG(competitor->network.MacAddress));
					rtw_clr_ft_flags(adapter, RTW_FT_OVER_DS_SUPPORTED);
					goto exit;
				}
			}
		} else
			goto exit;
	}
#endif
#endif
	return _FALSE;
}

void rtw_rson_show_survey_info(struct seq_file *m, _list *plist, _list *phead)
{
	struct wlan_network	*pnetwork = NULL;
	struct rtw_rson_struct  rson_data;
	s16 rson_score;
	u16  index = 0;

	RTW_PRINT_SEL(m, "%5s  %-17s  %3s  %5s %14s  %10s  %-3s  %5s %32s\n", "index", "bssid", "ch", "id", "hop_cnt", "loading", "RSSI", "score", "ssid");
	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if (!pnetwork)
			break;

		_rtw_memset(&rson_data, 0, sizeof(rson_data));
		rson_score = 0;
		if (rtw_get_rson_struct(&(pnetwork->network), &rson_data) == _TRUE)
			rson_score = rtw_cal_rson_score(&rson_data, pnetwork->network.Rssi);
		RTW_PRINT_SEL(m, "%5d  "MAC_FMT" %3d  0x%08x %6d %10d   %6d %6d   %32s\n",
			      ++index,
			      MAC_ARG(pnetwork->network.MacAddress),
			      pnetwork->network.Configuration.DSConfig,
			      rson_data.id,
			      rson_data.hopcnt,
			      rson_data.loading,
			      (int)pnetwork->network.Rssi,
			      rson_score,
			      pnetwork->network.Ssid.Ssid);
		plist = get_next(plist);
		}

}

/*
	Description :	As a AP role, We need to check the qualify of associating STA.
					We also need to check if we are ready to be associated.

	return :	TRUE  -- AP REJECT this STA
				FALSE -- AP ACCEPT this STA
*/
u8 rtw_rson_ap_check_sta(_adapter *padapter, u8 *pframe, uint pkt_len, unsigned short ie_offset)
{
	struct wlan_network	*pnetwork = NULL;
	struct rtw_rson_struct  rson_target;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	int len = 0;
	u8 ret = _FALSE;
	u8 *p;

#ifndef CONFIG_RTW_REPEATER_SON_ROOT
	_rtw_memset(&rson_target, 0, sizeof(rson_target));
	for (p = pframe + WLAN_HDR_A3_LEN + ie_offset; ; p += (len + 2)) {
		p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);

		if ((p == NULL) || (len == 0))
			break;

		if (p && (_rtw_memcmp(p + 2, RTW_RSON_OUI, sizeof(RTW_RSON_OUI)) == _TRUE)
			&& rtw_rson_varify_ie(p)) {
			p = p + 2 + sizeof(RTW_RSON_OUI);
			rson_target.ver = *p;
			/*	for (ver == 1)		*/
			p = p + 1;
			rson_target.id = le32_to_cpup((__le32 *)p);
			p = p + 4;
			rson_target.hopcnt = *p;
			p = p + 1;
			rson_target.connectible = *p;
			p = p + 1;
			rson_target.loading = *p;
			break;
		}
	}

	if (rson_target.id == 0)		/*	Normal STA, not a RSON STA	*/
		ret = _FALSE;
	else if (rson_target.id != pdvobj->rson_data.id) {
		ret = _TRUE;
		RTW_INFO("%s : Reject AssoReq because RSON ID not match, STA=%08x, our=%08x\n",
				__func__, rson_target.id, pdvobj->rson_data.id);
	} else if ((pdvobj->rson_data.hopcnt == RTW_RSON_HC_NOTREADY)
		|| (pdvobj->rson_data.connectible == RTW_RSON_DENYCONNECT)) {
		ret = _TRUE;
		RTW_INFO("%s : Reject AssoReq becuase our hopcnt=%d or connectbile=%d\n",
				__func__, pdvobj->rson_data.hopcnt, pdvobj->rson_data.connectible);
	}
#endif
	return ret;
}

u8 rtw_rson_scan_wk_cmd(_adapter *padapter, int op)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 *extra_cmd_buf;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}
	pdrvextra_cmd_parm->ec_id = RSON_SCAN_WK_CID;
	pdrvextra_cmd_parm->type = op;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;

}

void rtw_rson_scan_cmd_hdl(_adapter *padapter, int op)
{
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 val8;

	if (mlmeext_chk_scan_state(pmlmeext, SCAN_DISABLE) != _TRUE)
		return;
	if (op == RSON_SCAN_PROCESS) {
		padapter->rtw_rson_scanstage = RSON_SCAN_PROCESS;
		val8 = 0x1e;
		rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &val8, _FALSE);
		val8 = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
		issue_probereq(padapter, NULL, NULL);
		/*	stop rson_scan after 100ms	*/
		_set_timer(&(pmlmeext->rson_scan_timer), 100);
	} else if  (op == RSON_SCAN_DISABLE) {
		padapter->rtw_rson_scanstage = RSON_SCAN_DISABLE;
		val8 = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
		val8 = 0xff;
		rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &val8, _FALSE);
		/*	report_surveydone_event(padapter);*/
		if (pmlmepriv->to_join == _TRUE) {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) != _TRUE) {
				int s_ret;

				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
				pmlmepriv->to_join = _FALSE;
				s_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
				if (s_ret == _SUCCESS)
					_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				else if (s_ret == 2) {
					_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
					rtw_indicate_connect(padapter);
				} else {
					RTW_INFO("try_to_join, but select scanning queue fail, to_roam:%d\n", rtw_to_roam(padapter));
					if (rtw_to_roam(padapter) != 0) {
						if (rtw_dec_to_roam(padapter) == 0) {
							rtw_set_to_roam(padapter, 0);
							rtw_free_assoc_resources(padapter, _TRUE);
							rtw_indicate_disconnect(padapter, 0, _FALSE);
						} else
							pmlmepriv->to_join = _TRUE;
					} else
						rtw_indicate_disconnect(padapter, 0, _FALSE);
					_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				}
			}
		} else {
			if (rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE)) {
				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
				    && check_fwstate(pmlmepriv, _FW_LINKED)) {
					if (rtw_select_roaming_candidate(pmlmepriv) == _SUCCESS) {
#ifdef CONFIG_RTW_80211R
						if (rtw_chk_ft_flags(padapter, RTW_FT_OVER_DS_SUPPORTED)) {
							start_clnt_ft_action(adapter, (u8 *)pmlmepriv->roam_network->network.MacAddress);
						} else {
							/*wait a little time to retrieve packets buffered in the current ap while scan*/
							_set_timer(&pmlmeext->ft_roam_timer, 30);
						}
#else
						receive_disconnect(padapter, pmlmepriv->cur_network.network.MacAddress
							, WLAN_REASON_ACTIVE_ROAM, _FALSE);
#endif
					}
				}
			}
			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);
		}
	} else {
		RTW_ERR("%s : improper parameter -- op = %d\n", __func__, op);
	}
}

#endif	/* CONFIG_RTW_REPEATER_SON */
