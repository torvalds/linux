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
#define _RTW_MI_C_

#include <drv_types.h>
#include <hal_data.h>

void rtw_mi_update_union_chan_inf(_adapter *adapter, u8 ch, u8 offset , u8 bw)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mi_state *iface_state = &dvobj->iface_state;

	iface_state->union_ch = ch;
	iface_state->union_bw = bw;
	iface_state->union_offset = offset;
}

#ifdef CONFIG_P2P
static u8 _rtw_mi_p2p_listen_scan_chk(_adapter *adapter)
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 p2p_listen_scan_state = _FALSE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (rtw_p2p_chk_state(&iface->wdinfo, P2P_STATE_LISTEN) ||
			rtw_p2p_chk_state(&iface->wdinfo, P2P_STATE_SCAN)) {
			p2p_listen_scan_state = _TRUE;
			break;
		}
	}
	return p2p_listen_scan_state;
}
#endif
u8 rtw_mi_stayin_union_ch_chk(_adapter *adapter)
{
	u8 rst = _TRUE;
	u8 u_ch, u_bw, u_offset;
	u8 o_ch, o_bw, o_offset;

	u_ch = rtw_mi_get_union_chan(adapter);
	u_bw = rtw_mi_get_union_bw(adapter);
	u_offset = rtw_mi_get_union_offset(adapter);

	o_ch = rtw_get_oper_ch(adapter);
	o_bw = rtw_get_oper_bw(adapter);
	o_offset = rtw_get_oper_choffset(adapter);

	if ((u_ch != o_ch) || (u_bw != o_bw) || (u_offset != o_offset))
		rst = _FALSE;

	#ifdef DBG_IFACE_STATUS
	if (rst == _FALSE) {
		RTW_ERR("%s Not stay in union channel\n", __func__);
		if (GET_HAL_DATA(adapter)->bScanInProcess == _TRUE)
			RTW_ERR("ScanInProcess\n");
		#ifdef CONFIG_P2P
		if (_rtw_mi_p2p_listen_scan_chk(adapter))
			RTW_ERR("P2P in listen or scan state\n");
		#endif
		RTW_ERR("union ch, bw, offset: %u,%u,%u\n", u_ch, u_bw, u_offset);
		RTW_ERR("oper ch, bw, offset: %u,%u,%u\n", o_ch, o_bw, o_offset);
		RTW_ERR("=========================\n");
	}
	#endif
	return rst;
}

u8 rtw_mi_stayin_union_band_chk(_adapter *adapter)
{
	u8 rst = _TRUE;
	u8 u_ch, o_ch;
	u8 u_band, o_band;

	u_ch = rtw_mi_get_union_chan(adapter);
	o_ch = rtw_get_oper_ch(adapter);
	u_band = (u_ch > 14) ? BAND_ON_5G : BAND_ON_2_4G;
	o_band = (o_ch > 14) ? BAND_ON_5G : BAND_ON_2_4G;

	if (u_ch != o_ch)
		if(u_band != o_band)
			rst = _FALSE;

	#ifdef DBG_IFACE_STATUS
	if (rst == _FALSE)
		RTW_ERR("%s Not stay in union band\n", __func__);
	#endif

	return rst;
}

/* Find union about ch, bw, ch_offset of all linked/linking interfaces */
int _rtw_mi_get_ch_setting_union(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset, bool include_self)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	int i;
	u8 ch_ret = 0;
	u8 bw_ret = CHANNEL_WIDTH_20;
	u8 offset_ret = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	int num = 0;

	if (ch)
		*ch = 0;
	if (bw)
		*bw = CHANNEL_WIDTH_20;
	if (offset)
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		mlmeext = &iface->mlmeextpriv;

		if (!check_fwstate(&iface->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING))
			continue;

		if (check_fwstate(&iface->mlmepriv, WIFI_OP_CH_SWITCHING))
			continue;

		if (include_self == _FALSE && adapter == iface)
			continue;

		if (num == 0) {
			ch_ret = mlmeext->cur_channel;
			bw_ret = mlmeext->cur_bwmode;
			offset_ret = mlmeext->cur_ch_offset;
			num++;
			continue;
		}

		if (ch_ret != mlmeext->cur_channel) {
			num = 0;
			break;
		}

		if (bw_ret < mlmeext->cur_bwmode) {
			bw_ret = mlmeext->cur_bwmode;
			offset_ret = mlmeext->cur_ch_offset;
		} else if (bw_ret == mlmeext->cur_bwmode && offset_ret != mlmeext->cur_ch_offset) {
			num = 0;
			break;
		}

		num++;
	}

	if (num) {
		if (ch)
			*ch = ch_ret;
		if (bw)
			*bw = bw_ret;
		if (offset)
			*offset = offset_ret;
	}

	return num;
}

inline int rtw_mi_get_ch_setting_union(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	return _rtw_mi_get_ch_setting_union(adapter, ch, bw, offset, 1);
}

inline int rtw_mi_get_ch_setting_union_no_self(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	return _rtw_mi_get_ch_setting_union(adapter, ch, bw, offset, 0);
}

#define MI_STATUS_SELF_ONLY		0
#define MI_STATUS_OTHERS_ONLY	1
#define MI_STATUS_ALL			2

/* For now, not return union_ch/bw/offset */
void _rtw_mi_status(_adapter *adapter, struct mi_state *mstate, u8 target_sel)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	int i;

	_rtw_memset(mstate, 0, sizeof(struct mi_state));

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];

		if (target_sel == MI_STATUS_SELF_ONLY && iface != adapter)
			continue;
		if (target_sel == MI_STATUS_OTHERS_ONLY && iface == adapter)
			continue;

		if (check_fwstate(&iface->mlmepriv, WIFI_STATION_STATE) == _TRUE) {
			MSTATE_STA_NUM(mstate)++;
			if (check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE) {
				MSTATE_STA_LD_NUM(mstate)++;

				#ifdef CONFIG_TDLS
				if (iface->tdlsinfo.link_established == _TRUE)
					MSTATE_TDLS_LD_NUM(mstate)++;
				#endif
			}
			if (check_fwstate(&iface->mlmepriv, _FW_UNDER_LINKING) == _TRUE)
				MSTATE_STA_LG_NUM(mstate)++;

#ifdef CONFIG_AP_MODE
		} else if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			MSTATE_AP_NUM(mstate)++;
			if (iface->stapriv.asoc_sta_count > 2)
				MSTATE_AP_LD_NUM(mstate)++;
#endif

		} else if (check_fwstate(&iface->mlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE) == _TRUE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			MSTATE_ADHOC_NUM(mstate)++;
			if (iface->stapriv.asoc_sta_count > 2)
				MSTATE_ADHOC_LD_NUM(mstate)++;

#ifdef CONFIG_RTW_MESH
		} else if (check_fwstate(&iface->mlmepriv, WIFI_MESH_STATE) == _TRUE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			MSTATE_MESH_NUM(mstate)++;
			if (iface->stapriv.asoc_sta_count > 2)
				MSTATE_MESH_LD_NUM(mstate)++;
#endif

		}

		if (check_fwstate(&iface->mlmepriv, WIFI_UNDER_WPS) == _TRUE)
			MSTATE_WPS_NUM(mstate)++;

		if (check_fwstate(&iface->mlmepriv, WIFI_SITE_MONITOR) == _TRUE) {
			MSTATE_SCAN_NUM(mstate)++;

			if (mlmeext_scan_state(&iface->mlmeextpriv) != SCAN_DISABLE
				&& mlmeext_scan_state(&iface->mlmeextpriv) != SCAN_BACK_OP)
				MSTATE_SCAN_ENTER_NUM(mstate)++;
		}

#ifdef CONFIG_IOCTL_CFG80211
		if (rtw_cfg80211_get_is_mgmt_tx(iface))
			MSTATE_MGMT_TX_NUM(mstate)++;
		#ifdef CONFIG_P2P
		if (rtw_cfg80211_get_is_roch(iface) == _TRUE)
			MSTATE_ROCH_NUM(mstate)++;
		#endif
#endif /* CONFIG_IOCTL_CFG80211 */

	}
}

inline void rtw_mi_status(_adapter *adapter, struct mi_state *mstate)
{
	return _rtw_mi_status(adapter, mstate, MI_STATUS_ALL);
}

inline void rtw_mi_status_no_self(_adapter *adapter, struct mi_state *mstate)
{
	return _rtw_mi_status(adapter, mstate, MI_STATUS_OTHERS_ONLY);
}

inline void rtw_mi_status_no_others(_adapter *adapter, struct mi_state *mstate)
{
	return _rtw_mi_status(adapter, mstate, MI_STATUS_SELF_ONLY);
}

/* For now, not handle union_ch/bw/offset */
inline void rtw_mi_status_merge(struct mi_state *d, struct mi_state *a)
{
	d->sta_num += a->sta_num;
	d->ld_sta_num += a->ld_sta_num;
	d->lg_sta_num += a->lg_sta_num;
#ifdef CONFIG_TDLS
	d->ld_tdls_num += a->ld_tdls_num;
#endif
#ifdef CONFIG_AP_MODE
	d->ap_num += a->ap_num;
	d->ld_ap_num += a->ld_ap_num;
#endif
	d->adhoc_num += a->adhoc_num;
	d->ld_adhoc_num += a->ld_adhoc_num;
#ifdef CONFIG_RTW_MESH
	d->mesh_num += a->mesh_num;
	d->ld_mesh_num += a->ld_mesh_num;
#endif
	d->scan_num += a->scan_num;
	d->scan_enter_num += a->scan_enter_num;
	d->uwps_num += a->uwps_num;
#ifdef CONFIG_IOCTL_CFG80211
	#ifdef CONFIG_P2P
	d->roch_num += a->roch_num;
	#endif
	d->mgmt_tx_num += a->mgmt_tx_num;
#endif
}

void dump_mi_status(void *sel, struct dvobj_priv *dvobj)
{
	RTW_PRINT_SEL(sel, "== dvobj-iface_state ==\n");
	RTW_PRINT_SEL(sel, "sta_num:%d\n", DEV_STA_NUM(dvobj));
	RTW_PRINT_SEL(sel, "linking_sta_num:%d\n", DEV_STA_LG_NUM(dvobj));
	RTW_PRINT_SEL(sel, "linked_sta_num:%d\n", DEV_STA_LD_NUM(dvobj));
#ifdef CONFIG_TDLS
	RTW_PRINT_SEL(sel, "linked_tdls_num:%d\n", DEV_TDLS_LD_NUM(dvobj));
#endif
#ifdef CONFIG_AP_MODE
	RTW_PRINT_SEL(sel, "ap_num:%d\n", DEV_AP_NUM(dvobj));
	RTW_PRINT_SEL(sel, "linked_ap_num:%d\n", DEV_AP_LD_NUM(dvobj));
#endif
	RTW_PRINT_SEL(sel, "adhoc_num:%d\n", DEV_ADHOC_NUM(dvobj));
	RTW_PRINT_SEL(sel, "linked_adhoc_num:%d\n", DEV_ADHOC_LD_NUM(dvobj));
#ifdef CONFIG_RTW_MESH
	RTW_PRINT_SEL(sel, "mesh_num:%d\n", DEV_MESH_NUM(dvobj));
	RTW_PRINT_SEL(sel, "linked_mesh_num:%d\n", DEV_MESH_LD_NUM(dvobj));
#endif
#ifdef CONFIG_P2P
	RTW_PRINT_SEL(sel, "p2p_device_num:%d\n", rtw_mi_stay_in_p2p_mode(dvobj->padapters[IFACE_ID0]));
#endif
	RTW_PRINT_SEL(sel, "scan_num:%d\n", DEV_STA_NUM(dvobj));
	RTW_PRINT_SEL(sel, "under_wps_num:%d\n", DEV_WPS_NUM(dvobj));
#if defined(CONFIG_IOCTL_CFG80211)
	#if defined(CONFIG_P2P)
	RTW_PRINT_SEL(sel, "roch_num:%d\n", DEV_ROCH_NUM(dvobj));
	#endif
	RTW_PRINT_SEL(sel, "mgmt_tx_num:%d\n", DEV_MGMT_TX_NUM(dvobj));
#endif
	RTW_PRINT_SEL(sel, "union_ch:%d\n", DEV_U_CH(dvobj));
	RTW_PRINT_SEL(sel, "union_bw:%d\n", DEV_U_BW(dvobj));
	RTW_PRINT_SEL(sel, "union_offset:%d\n", DEV_U_OFFSET(dvobj));
	RTW_PRINT_SEL(sel, "================\n\n");
}

void dump_dvobj_mi_status(void *sel, const char *fun_name, _adapter *adapter)
{
	RTW_INFO("\n[ %s ] call %s\n", fun_name, __func__);
	dump_mi_status(sel, adapter_to_dvobj(adapter));
}

inline void rtw_mi_update_iface_status(struct mlme_priv *pmlmepriv, sint state)
{
	_adapter *adapter = container_of(pmlmepriv, _adapter, mlmepriv);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mi_state *iface_state = &dvobj->iface_state;
	struct mi_state tmp_mstate;
	u8 i;
	u8 u_ch, u_offset, u_bw;
	_adapter *iface;

	if (state == WIFI_MONITOR_STATE
		|| state == 0xFFFFFFFF
	)
		return;

	if (0)
		RTW_INFO("%s => will change or clean state to 0x%08x\n", __func__, state);

	rtw_mi_status(adapter, &tmp_mstate);
	_rtw_memcpy(iface_state, &tmp_mstate, sizeof(struct mi_state));

	if (rtw_mi_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset))
		rtw_mi_update_union_chan_inf(adapter , u_ch, u_offset , u_bw);
	else {
		if (0) {
			dump_adapters_status(RTW_DBGDUMP , dvobj);
			RTW_INFO("%s-[ERROR] cannot get union channel\n", __func__);
			rtw_warn_on(1);
		}
	}

#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
#endif
}
u8 rtw_mi_check_status(_adapter *adapter, u8 type)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mi_state *iface_state = &dvobj->iface_state;
	u8 ret = _FALSE;

#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
	RTW_INFO("%s-"ADPT_FMT" check type:%d\n", __func__, ADPT_ARG(adapter), type);
#endif

	switch (type) {
	case MI_LINKED:
		if (MSTATE_STA_LD_NUM(iface_state) || MSTATE_AP_NUM(iface_state) || MSTATE_ADHOC_NUM(iface_state) || MSTATE_MESH_NUM(iface_state)) /*check_fwstate(&iface->mlmepriv, _FW_LINKED)*/
			ret = _TRUE;
		break;
	case MI_ASSOC:
		if (MSTATE_STA_LD_NUM(iface_state) || MSTATE_AP_LD_NUM(iface_state) || MSTATE_ADHOC_LD_NUM(iface_state) || MSTATE_MESH_LD_NUM(iface_state))
			ret = _TRUE;
		break;
	case MI_UNDER_WPS:
		if (MSTATE_WPS_NUM(iface_state))
			ret = _TRUE;
		break;

	case MI_AP_MODE:
		if (MSTATE_AP_NUM(iface_state))
			ret = _TRUE;
		break;
	case MI_AP_ASSOC:
		if (MSTATE_AP_LD_NUM(iface_state))
			ret = _TRUE;
		break;

	case MI_ADHOC:
		if (MSTATE_ADHOC_NUM(iface_state))
			ret = _TRUE;
		break;
	case MI_ADHOC_ASSOC:
		if (MSTATE_ADHOC_LD_NUM(iface_state))
			ret = _TRUE;
		break;

#ifdef CONFIG_RTW_MESH
	case MI_MESH:
		if (MSTATE_MESH_NUM(iface_state))
			ret = _TRUE;
		break;
	case MI_MESH_ASSOC:
		if (MSTATE_MESH_LD_NUM(iface_state))
			ret = _TRUE;
		break;
#endif

	case MI_STA_NOLINK: /* this is misleading, but not used now */
		if (MSTATE_STA_NUM(iface_state) && (!(MSTATE_STA_LD_NUM(iface_state) || MSTATE_STA_LG_NUM(iface_state))))
			ret = _TRUE;
		break;
	case MI_STA_LINKED:
		if (MSTATE_STA_LD_NUM(iface_state))
			ret = _TRUE;
		break;
	case MI_STA_LINKING:
		if (MSTATE_STA_LG_NUM(iface_state))
			ret = _TRUE;
		break;

	default:
		break;
	}
	return ret;
}

/*
* return value : 0 is failed or have not interface meet condition
* return value : !0 is success or interface numbers which meet condition
* return value of ops_func must be _TRUE or _FALSE
*/
static u8 _rtw_mi_process(_adapter *padapter, bool exclude_self,
		  void *data, u8(*ops_func)(_adapter *padapter, void *data))
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	u8 ret = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {

			if ((exclude_self) && (iface == padapter))
				continue;

			if (ops_func)
				if (_TRUE == ops_func(iface, data))
					ret++;
		}
	}
	return ret;
}
static u8 _rtw_mi_process_without_schk(_adapter *padapter, bool exclude_self,
		  void *data, u8(*ops_func)(_adapter *padapter, void *data))
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	u8 ret = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface) {
			if ((exclude_self) && (iface == padapter))
				continue;

			if (ops_func)
				if (ops_func(iface, data) == _TRUE)
					ret++;
		}
	}
	return ret;
}

static u8 _rtw_mi_netif_caroff_qstop(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	rtw_netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_caroff_qstop(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_caroff_qstop);
}
u8 rtw_mi_buddy_netif_caroff_qstop(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_caroff_qstop);
}

static u8 _rtw_mi_netif_caron_qstart(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	rtw_netif_carrier_on(pnetdev);
	rtw_netif_start_queue(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_caron_qstart(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_caron_qstart);
}
u8 rtw_mi_buddy_netif_caron_qstart(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_caron_qstart);
}

static u8 _rtw_mi_netif_stop_queue(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	rtw_netif_stop_queue(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_stop_queue(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_stop_queue);
}
u8 rtw_mi_buddy_netif_stop_queue(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_stop_queue);
}

static u8 _rtw_mi_netif_wake_queue(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	if (pnetdev)
		rtw_netif_wake_queue(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_wake_queue(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_wake_queue);
}
u8 rtw_mi_buddy_netif_wake_queue(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_wake_queue);
}

static u8 _rtw_mi_netif_carrier_on(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	if (pnetdev)
		rtw_netif_carrier_on(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_carrier_on(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_carrier_on);
}
u8 rtw_mi_buddy_netif_carrier_on(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_carrier_on);
}

static u8 _rtw_mi_netif_carrier_off(_adapter *padapter, void *data)
{
	struct net_device *pnetdev = padapter->pnetdev;

	if (pnetdev)
		rtw_netif_carrier_off(pnetdev);
	return _TRUE;
}
u8 rtw_mi_netif_carrier_off(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_netif_carrier_off);
}
u8 rtw_mi_buddy_netif_carrier_off(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_netif_carrier_off);
}

static u8 _rtw_mi_scan_abort(_adapter *adapter, void *data)
{
	bool bwait = *(bool *)data;

	if (bwait)
		rtw_scan_abort(adapter);
	else
		rtw_scan_abort_no_wait(adapter);

	return _TRUE;
}
void rtw_mi_scan_abort(_adapter *adapter, bool bwait)
{
	bool in_data = bwait;

	_rtw_mi_process(adapter, _FALSE, &in_data, _rtw_mi_scan_abort);

}
void rtw_mi_buddy_scan_abort(_adapter *adapter, bool bwait)
{
	bool in_data = bwait;

	_rtw_mi_process(adapter, _TRUE, &in_data, _rtw_mi_scan_abort);
}

static u8 _rtw_mi_start_drv_threads(_adapter *adapter, void *data)
{
	rtw_start_drv_threads(adapter);
	return _TRUE;
}
void rtw_mi_start_drv_threads(_adapter *adapter)
{
	_rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_start_drv_threads);
}
void rtw_mi_buddy_start_drv_threads(_adapter *adapter)
{
	_rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_start_drv_threads);
}

static void _rtw_mi_stop_drv_threads(_adapter *adapter, bool exclude_self)
{
	int i;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && (iface->bup == _TRUE))
			if ((exclude_self) && (iface == adapter))
				continue;
		rtw_stop_drv_threads(iface);
	}
}
void rtw_mi_stop_drv_threads(_adapter *adapter)
{
	_rtw_mi_stop_drv_threads(adapter, _FALSE);
}
void rtw_mi_buddy_stop_drv_threads(_adapter *adapter)
{
	_rtw_mi_stop_drv_threads(adapter, _TRUE);
}

static u8 _rtw_mi_cancel_all_timer(_adapter *adapter, void *data)
{
	rtw_cancel_all_timer(adapter);
	return _TRUE;
}
void rtw_mi_cancel_all_timer(_adapter *adapter)
{
	_rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_cancel_all_timer);
}
void rtw_mi_buddy_cancel_all_timer(_adapter *adapter)
{
	_rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_cancel_all_timer);
}

static u8 _rtw_mi_reset_drv_sw(_adapter *adapter, void *data)
{
	rtw_reset_drv_sw(adapter);
	return _TRUE;
}
void rtw_mi_reset_drv_sw(_adapter *adapter)
{
	_rtw_mi_process_without_schk(adapter, _FALSE, NULL, _rtw_mi_reset_drv_sw);
}
void rtw_mi_buddy_reset_drv_sw(_adapter *adapter)
{
	_rtw_mi_process_without_schk(adapter, _TRUE, NULL, _rtw_mi_reset_drv_sw);
}

static u8 _rtw_mi_intf_start(_adapter *adapter, void *data)
{
	rtw_intf_start(adapter);
	return _TRUE;
}
void rtw_mi_intf_start(_adapter *adapter)
{
	_rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_intf_start);
}
void rtw_mi_buddy_intf_start(_adapter *adapter)
{
	_rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_intf_start);
}

static u8 _rtw_mi_intf_stop(_adapter *adapter, void *data)
{
	rtw_intf_stop(adapter);
	return _TRUE;
}
void rtw_mi_intf_stop(_adapter *adapter)
{
	_rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_intf_stop);
}
void rtw_mi_buddy_intf_stop(_adapter *adapter)
{
	_rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_intf_stop);
}

static u8 _rtw_mi_suspend_free_assoc_resource(_adapter *padapter, void *data)
{
	return rtw_suspend_free_assoc_resource(padapter);
}
void rtw_mi_suspend_free_assoc_resource(_adapter *adapter)
{
	_rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_suspend_free_assoc_resource);
}
void rtw_mi_buddy_suspend_free_assoc_resource(_adapter *adapter)
{
	_rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_suspend_free_assoc_resource);
}

static u8 _rtw_mi_is_scan_deny(_adapter *adapter, void *data)
{
	return rtw_is_scan_deny(adapter);
}

u8 rtw_mi_is_scan_deny(_adapter *adapter)
{
	return _rtw_mi_process(adapter, _FALSE, NULL, _rtw_mi_is_scan_deny);

}
u8 rtw_mi_buddy_is_scan_deny(_adapter *adapter)
{
	return _rtw_mi_process(adapter, _TRUE, NULL, _rtw_mi_is_scan_deny);
}

#ifdef CONFIG_SET_SCAN_DENY_TIMER
static u8 _rtw_mi_set_scan_deny(_adapter *adapter, void *data)
{
	u32 ms = *(u32 *)data;

	rtw_set_scan_deny(adapter, ms);
	return _TRUE;
}
void rtw_mi_set_scan_deny(_adapter *adapter, u32 ms)
{
	u32 in_data = ms;

	_rtw_mi_process(adapter, _FALSE, &in_data, _rtw_mi_set_scan_deny);
}
void rtw_mi_buddy_set_scan_deny(_adapter *adapter, u32 ms)
{
	u32 in_data = ms;

	_rtw_mi_process(adapter, _TRUE, &in_data, _rtw_mi_set_scan_deny);
}
#endif /*CONFIG_SET_SCAN_DENY_TIMER*/

static u8 _rtw_mi_beacon_update(_adapter *padapter, void *data)
{
	if (!MLME_IS_STA(padapter)
	    && check_fwstate(&padapter->mlmepriv, _FW_LINKED) == _TRUE) {
		RTW_INFO(ADPT_FMT" - update_beacon\n", ADPT_ARG(padapter));
		update_beacon(padapter, 0xFF, NULL, _TRUE);
	}
	return _TRUE;
}

void rtw_mi_beacon_update(_adapter *padapter)
{
	_rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_beacon_update);
}

void rtw_mi_buddy_beacon_update(_adapter *padapter)
{
	_rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_beacon_update);
}

static u8 _rtw_mi_hal_dump_macaddr(_adapter *padapter, void *data)
{
	u8 mac_addr[ETH_ALEN] = {0};

	rtw_hal_get_hwreg(padapter, HW_VAR_MAC_ADDR, mac_addr);
	RTW_INFO(ADPT_FMT"MAC Address ="MAC_FMT"\n", ADPT_ARG(padapter), MAC_ARG(mac_addr));
	return _TRUE;
}
void rtw_mi_hal_dump_macaddr(_adapter *padapter)
{
	_rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_hal_dump_macaddr);
}
void rtw_mi_buddy_hal_dump_macaddr(_adapter *padapter)
{
	_rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_hal_dump_macaddr);
}

#ifdef CONFIG_PCI_HCI
static u8 _rtw_mi_xmit_tasklet_schedule(_adapter *padapter, void *data)
{
	if (rtw_txframes_pending(padapter)) {
		/* try to deal with the pending packets */
		tasklet_hi_schedule(&(padapter->xmitpriv.xmit_tasklet));
	}
	return _TRUE;
}
void rtw_mi_xmit_tasklet_schedule(_adapter *padapter)
{
	_rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_xmit_tasklet_schedule);
}
void rtw_mi_buddy_xmit_tasklet_schedule(_adapter *padapter)
{
	_rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_xmit_tasklet_schedule);
}
#endif

u8 _rtw_mi_busy_traffic_check(_adapter *padapter, void *data)
{
	u32 passtime;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	bool check_sc_interval = *(bool *)data;

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE) {
		if (check_sc_interval) {
			/* Miracast can't do AP scan*/
			passtime = rtw_get_passing_time_ms(pmlmepriv->lastscantime);
			pmlmepriv->lastscantime = rtw_get_current_time();
			if (passtime > BUSY_TRAFFIC_SCAN_DENY_PERIOD) {
				RTW_INFO(ADPT_FMT" bBusyTraffic == _TRUE\n", ADPT_ARG(padapter));
				return _TRUE;
			}
		} else
			return _TRUE;
	}

	return _FALSE;
}

u8 rtw_mi_busy_traffic_check(_adapter *padapter, bool check_sc_interval)
{
	bool in_data = check_sc_interval;

	return _rtw_mi_process(padapter, _FALSE, &in_data, _rtw_mi_busy_traffic_check);
}
u8 rtw_mi_buddy_busy_traffic_check(_adapter *padapter, bool check_sc_interval)
{
	bool in_data = check_sc_interval;

	return _rtw_mi_process(padapter, _TRUE, &in_data, _rtw_mi_busy_traffic_check);
}
static u8 _rtw_mi_check_mlmeinfo_state(_adapter *padapter, void *data)
{
	u32 state = *(u32 *)data;
	struct mlme_ext_priv *mlmeext = &padapter->mlmeextpriv;

	/*if (mlmeext_msr(mlmeext) == state)*/
	if (check_mlmeinfo_state(mlmeext, state))
		return _TRUE;
	else
		return _FALSE;
}

u8 rtw_mi_check_mlmeinfo_state(_adapter *padapter, u32 state)
{
	u32 in_data = state;

	return _rtw_mi_process(padapter, _FALSE, &in_data, _rtw_mi_check_mlmeinfo_state);
}

u8 rtw_mi_buddy_check_mlmeinfo_state(_adapter *padapter, u32 state)
{
	u32 in_data = state;

	return _rtw_mi_process(padapter, _TRUE, &in_data, _rtw_mi_check_mlmeinfo_state);
}

/*#define DBG_DUMP_FW_STATE*/
#ifdef DBG_DUMP_FW_STATE
static void rtw_dbg_dump_fwstate(_adapter *padapter, sint state)
{
	u8 buf[32] = {0};

	if (state & WIFI_FW_NULL_STATE) {
		_rtw_memset(buf, 0, 32);
		sprintf(buf, "WIFI_FW_NULL_STATE");
		RTW_INFO(FUNC_ADPT_FMT"fwstate-%s\n", FUNC_ADPT_ARG(padapter), buf);
	}

	if (state & _FW_LINKED) {
		_rtw_memset(buf, 0, 32);
		sprintf(buf, "_FW_LINKED");
		RTW_INFO(FUNC_ADPT_FMT"fwstate-%s\n", FUNC_ADPT_ARG(padapter), buf);
	}

	if (state & _FW_UNDER_LINKING) {
		_rtw_memset(buf, 0, 32);
		sprintf(buf, "_FW_UNDER_LINKING");
		RTW_INFO(FUNC_ADPT_FMT"fwstate-%s\n", FUNC_ADPT_ARG(padapter), buf);
	}

	if (state & _FW_UNDER_SURVEY) {
		_rtw_memset(buf, 0, 32);
		sprintf(buf, "_FW_UNDER_SURVEY");
		RTW_INFO(FUNC_ADPT_FMT"fwstate-%s\n", FUNC_ADPT_ARG(padapter), buf);
	}
}
#endif

static u8 _rtw_mi_check_fwstate(_adapter *padapter, void *data)
{
	u8 ret = _FALSE;

	sint state = *(sint *)data;

	if ((state == WIFI_FW_NULL_STATE) &&
	    (padapter->mlmepriv.fw_state == WIFI_FW_NULL_STATE))
		ret = _TRUE;
	else if (_TRUE == check_fwstate(&padapter->mlmepriv, state))
		ret = _TRUE;
#ifdef DBG_DUMP_FW_STATE
	if (ret)
		rtw_dbg_dump_fwstate(padapter, state);
#endif
	return ret;
}
u8 rtw_mi_check_fwstate(_adapter *padapter, sint state)
{
	sint in_data = state;

	return _rtw_mi_process(padapter, _FALSE, &in_data, _rtw_mi_check_fwstate);
}
u8 rtw_mi_buddy_check_fwstate(_adapter *padapter, sint state)
{
	sint in_data = state;

	return _rtw_mi_process(padapter, _TRUE, &in_data, _rtw_mi_check_fwstate);
}

static u8 _rtw_mi_traffic_statistics(_adapter *padapter , void *data)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	/* Tx */
	pdvobjpriv->traffic_stat.tx_bytes += padapter->xmitpriv.tx_bytes;
	pdvobjpriv->traffic_stat.tx_pkts += padapter->xmitpriv.tx_pkts;
	pdvobjpriv->traffic_stat.tx_drop += padapter->xmitpriv.tx_drop;

	/* Rx */
	pdvobjpriv->traffic_stat.rx_bytes += padapter->recvpriv.rx_bytes;
	pdvobjpriv->traffic_stat.rx_pkts += padapter->recvpriv.rx_pkts;
	pdvobjpriv->traffic_stat.rx_drop += padapter->recvpriv.rx_drop;
	return _TRUE;
}
u8 rtw_mi_traffic_statistics(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_traffic_statistics);
}

static u8 _rtw_mi_check_miracast_enabled(_adapter *padapter , void *data)
{
	return is_miracast_enabled(padapter);
}
u8 rtw_mi_check_miracast_enabled(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_check_miracast_enabled);
}

#ifdef CONFIG_XMIT_THREAD_MODE
static u8 _rtw_mi_check_pending_xmitbuf(_adapter *padapter , void *data)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return check_pending_xmitbuf(pxmitpriv);
}
u8 rtw_mi_check_pending_xmitbuf(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_check_pending_xmitbuf);
}
u8 rtw_mi_buddy_check_pending_xmitbuf(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_check_pending_xmitbuf);
}
#endif

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
static u8 _rtw_mi_dequeue_writeport(_adapter *padapter , bool exclude_self)
{
	int i;
	u8	queue_empty = _TRUE;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {

			if ((exclude_self) && (iface == padapter))
				continue;

			queue_empty &= _dequeue_writeport(iface);
		}
	}
	return queue_empty;
}
u8 rtw_mi_dequeue_writeport(_adapter *padapter)
{
	return _rtw_mi_dequeue_writeport(padapter, _FALSE);
}
u8 rtw_mi_buddy_dequeue_writeport(_adapter *padapter)
{
	return _rtw_mi_dequeue_writeport(padapter, _TRUE);
}
#endif
static void _rtw_mi_adapter_reset(_adapter *padapter , u8 exclude_self)
{
	int i;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (dvobj->padapters[i]) {
			if ((exclude_self) && (dvobj->padapters[i] == padapter))
				continue;
			dvobj->padapters[i] = NULL;
		}
	}
}

void rtw_mi_adapter_reset(_adapter *padapter)
{
	_rtw_mi_adapter_reset(padapter, _FALSE);
}

void rtw_mi_buddy_adapter_reset(_adapter *padapter)
{
	_rtw_mi_adapter_reset(padapter, _TRUE);
}

static u8 _rtw_mi_dynamic_check_timer_handlder(_adapter *adapter, void *data)
{
	rtw_iface_dynamic_check_timer_handlder(adapter);
	return _TRUE;
}
u8 rtw_mi_dynamic_check_timer_handlder(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_dynamic_check_timer_handlder);
}
u8 rtw_mi_buddy_dynamic_check_timer_handlder(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_dynamic_check_timer_handlder);
}

static u8 _rtw_mi_dev_unload(_adapter *adapter, void *data)
{
	rtw_dev_unload(adapter);
	return _TRUE;
}
u8 rtw_mi_dev_unload(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_dev_unload);
}
u8 rtw_mi_buddy_dev_unload(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_dev_unload);
}

static u8 _rtw_mi_dynamic_chk_wk_hdl(_adapter *adapter, void *data)
{
	rtw_iface_dynamic_chk_wk_hdl(adapter);
	return _TRUE;
}
u8 rtw_mi_dynamic_chk_wk_hdl(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_dynamic_chk_wk_hdl);
}
u8 rtw_mi_buddy_dynamic_chk_wk_hdl(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_dynamic_chk_wk_hdl);
}

static u8 _rtw_mi_os_xmit_schedule(_adapter *adapter, void *data)
{
	rtw_os_xmit_schedule(adapter);
	return _TRUE;
}
u8 rtw_mi_os_xmit_schedule(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_os_xmit_schedule);
}
u8 rtw_mi_buddy_os_xmit_schedule(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_os_xmit_schedule);
}

static u8 _rtw_mi_report_survey_event(_adapter *adapter, void *data)
{
	union recv_frame *precv_frame = (union recv_frame *)data;

	report_survey_event(adapter, precv_frame);
	return _TRUE;
}
u8 rtw_mi_report_survey_event(_adapter *padapter, union recv_frame *precv_frame)
{
	return _rtw_mi_process(padapter, _FALSE, precv_frame, _rtw_mi_report_survey_event);
}
u8 rtw_mi_buddy_report_survey_event(_adapter *padapter, union recv_frame *precv_frame)
{
	return _rtw_mi_process(padapter, _TRUE, precv_frame, _rtw_mi_report_survey_event);
}

static u8 _rtw_mi_sreset_adapter_hdl(_adapter *adapter, void *data)
{
	u8 bstart = *(u8 *)data;

	if (bstart)
		sreset_start_adapter(adapter);
	else
		sreset_stop_adapter(adapter);
	return _TRUE;
}
u8 rtw_mi_sreset_adapter_hdl(_adapter *padapter, u8 bstart)
{
	u8 in_data = bstart;

	return _rtw_mi_process(padapter, _FALSE, &in_data, _rtw_mi_sreset_adapter_hdl);
}
u8 rtw_mi_buddy_sreset_adapter_hdl(_adapter *padapter, u8 bstart)
{
	u8 in_data = bstart;

	return _rtw_mi_process(padapter, _TRUE, &in_data, _rtw_mi_sreset_adapter_hdl);
}
static u8 _rtw_mi_tx_beacon_hdl(_adapter *adapter, void *data)
{
	if ((MLME_IS_AP(adapter) || MLME_IS_MESH(adapter))
		&& check_fwstate(&adapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE
	) {
		adapter->mlmepriv.update_bcn = _TRUE;
#ifndef CONFIG_INTERRUPT_BASED_TXBCN
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		tx_beacon_hdl(adapter, NULL);
#endif
#endif
	}
	return _TRUE;
}
u8 rtw_mi_tx_beacon_hdl(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_tx_beacon_hdl);
}
u8 rtw_mi_buddy_tx_beacon_hdl(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_sreset_adapter_hdl);
}

static u8 _rtw_mi_set_tx_beacon_cmd(_adapter *adapter, void *data)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
		if (pmlmepriv->update_bcn == _TRUE)
			set_tx_beacon_cmd(adapter);
	}
	return _TRUE;
}
u8 rtw_mi_set_tx_beacon_cmd(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_set_tx_beacon_cmd);
}
u8 rtw_mi_buddy_set_tx_beacon_cmd(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_set_tx_beacon_cmd);
}

#ifdef CONFIG_P2P
static u8 _rtw_mi_p2p_chk_state(_adapter *adapter, void *data)
{
	struct wifidirect_info *pwdinfo = &(adapter->wdinfo);
	enum P2P_STATE state = *(enum P2P_STATE *)data;

	return rtw_p2p_chk_state(pwdinfo, state);
}
u8 rtw_mi_p2p_chk_state(_adapter *padapter, enum P2P_STATE p2p_state)
{
	u8 in_data = p2p_state;

	return _rtw_mi_process(padapter, _FALSE, &in_data, _rtw_mi_p2p_chk_state);
}
u8 rtw_mi_buddy_p2p_chk_state(_adapter *padapter, enum P2P_STATE p2p_state)
{
	u8 in_data  = p2p_state;

	return _rtw_mi_process(padapter, _TRUE, &in_data, _rtw_mi_p2p_chk_state);
}
static u8 _rtw_mi_stay_in_p2p_mode(_adapter *adapter, void *data)
{
	struct wifidirect_info *pwdinfo = &(adapter->wdinfo);

	if (rtw_p2p_role(pwdinfo) != P2P_ROLE_DISABLE)
		return _TRUE;
	return _FALSE;
}
u8 rtw_mi_stay_in_p2p_mode(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _FALSE, NULL, _rtw_mi_stay_in_p2p_mode);
}
u8 rtw_mi_buddy_stay_in_p2p_mode(_adapter *padapter)
{
	return _rtw_mi_process(padapter, _TRUE, NULL, _rtw_mi_stay_in_p2p_mode);
}
#endif /*CONFIG_P2P*/

_adapter *rtw_get_iface_by_id(_adapter *padapter, u8 iface_id)
{
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	if ((padapter == NULL) || (iface_id >= CONFIG_IFACE_NUMBER)) {
		rtw_warn_on(1);
		return iface;
	}

	return  dvobj->padapters[iface_id];
}

_adapter *rtw_get_iface_by_macddr(_adapter *padapter, u8 *mac_addr)
{
	int i;
	_adapter *iface = NULL;
	u8 bmatch = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && (_rtw_memcmp(mac_addr, adapter_mac_addr(iface), ETH_ALEN))) {
			bmatch = _TRUE;
			break;
		}
	}
	if (bmatch)
		return iface;
	else
		return NULL;
}

_adapter *rtw_get_iface_by_hwport(_adapter *padapter, u8 hw_port)
{
	int i;
	_adapter *iface = NULL;
	u8 bmatch = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && (hw_port == iface->hw_port)) {
			bmatch = _TRUE;
			break;
		}
	}
	if (bmatch)
		return iface;
	else
		return NULL;
}

/*#define CONFIG_SKB_ALLOCATED*/
#define DBG_SKB_PROCESS
#ifdef DBG_SKB_PROCESS
void rtw_dbg_skb_process(_adapter *padapter, union recv_frame *precvframe, union recv_frame *pcloneframe)
{
	_pkt *pkt_copy, *pkt_org;

	pkt_org = precvframe->u.hdr.pkt;
	pkt_copy = pcloneframe->u.hdr.pkt;
	/*
		RTW_INFO("%s ===== ORG SKB =====\n", __func__);
		RTW_INFO(" SKB head(%p)\n", pkt_org->head);
		RTW_INFO(" SKB data(%p)\n", pkt_org->data);
		RTW_INFO(" SKB tail(%p)\n", pkt_org->tail);
		RTW_INFO(" SKB end(%p)\n", pkt_org->end);

		RTW_INFO(" recv frame head(%p)\n", precvframe->u.hdr.rx_head);
		RTW_INFO(" recv frame data(%p)\n", precvframe->u.hdr.rx_data);
		RTW_INFO(" recv frame tail(%p)\n", precvframe->u.hdr.rx_tail);
		RTW_INFO(" recv frame end(%p)\n", precvframe->u.hdr.rx_end);

		RTW_INFO("%s ===== COPY SKB =====\n", __func__);
		RTW_INFO(" SKB head(%p)\n", pkt_copy->head);
		RTW_INFO(" SKB data(%p)\n", pkt_copy->data);
		RTW_INFO(" SKB tail(%p)\n", pkt_copy->tail);
		RTW_INFO(" SKB end(%p)\n", pkt_copy->end);

		RTW_INFO(" recv frame head(%p)\n", pcloneframe->u.hdr.rx_head);
		RTW_INFO(" recv frame data(%p)\n", pcloneframe->u.hdr.rx_data);
		RTW_INFO(" recv frame tail(%p)\n", pcloneframe->u.hdr.rx_tail);
		RTW_INFO(" recv frame end(%p)\n", pcloneframe->u.hdr.rx_end);
	*/
	/*
		RTW_INFO("%s => recv_frame adapter(%p,%p)\n", __func__, precvframe->u.hdr.adapter, pcloneframe->u.hdr.adapter);
		RTW_INFO("%s => recv_frame dev(%p,%p)\n", __func__, pkt_org->dev , pkt_copy->dev);
		RTW_INFO("%s => recv_frame len(%d,%d)\n", __func__, precvframe->u.hdr.len, pcloneframe->u.hdr.len);
	*/
	if (precvframe->u.hdr.len != pcloneframe->u.hdr.len)
		RTW_INFO("%s [WARN]  recv_frame length(%d:%d) compare failed\n", __func__, precvframe->u.hdr.len, pcloneframe->u.hdr.len);

	if (_rtw_memcmp(&precvframe->u.hdr.attrib, &pcloneframe->u.hdr.attrib, sizeof(struct rx_pkt_attrib)) == _FALSE)
		RTW_INFO("%s [WARN]  recv_frame attrib compare failed\n", __func__);

	if (_rtw_memcmp(precvframe->u.hdr.rx_data, pcloneframe->u.hdr.rx_data, precvframe->u.hdr.len) == _FALSE)
		RTW_INFO("%s [WARN]  recv_frame rx_data compare failed\n", __func__);

}
#endif

static s32 _rtw_mi_buddy_clone_bcmc_packet(_adapter *adapter, union recv_frame *precvframe, u8 *pphy_status, union recv_frame *pcloneframe)
{
	s32 ret = _SUCCESS;
	u8 *pbuf = precvframe->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = NULL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);

	if (pcloneframe) {
		pcloneframe->u.hdr.adapter = adapter;

		_rtw_init_listhead(&pcloneframe->u.hdr.list);
		pcloneframe->u.hdr.precvbuf = NULL;	/*can't access the precvbuf for new arch.*/
		pcloneframe->u.hdr.len = 0;

		_rtw_memcpy(&pcloneframe->u.hdr.attrib, &precvframe->u.hdr.attrib, sizeof(struct rx_pkt_attrib));

		pattrib = &pcloneframe->u.hdr.attrib;
#ifdef CONFIG_SKB_ALLOCATED
		if (rtw_os_alloc_recvframe(adapter, pcloneframe, pbuf, NULL) == _SUCCESS)
#else
		if (rtw_os_recvframe_duplicate_skb(adapter, pcloneframe, precvframe->u.hdr.pkt) == _SUCCESS)
#endif
		{
#ifdef CONFIG_SKB_ALLOCATED
			recvframe_put(pcloneframe, pattrib->pkt_len);
#endif

#ifdef DBG_SKB_PROCESS
			rtw_dbg_skb_process(adapter, precvframe, pcloneframe);
#endif

			if (pphy_status)
				rx_query_phy_status(pcloneframe, pphy_status);

			ret = rtw_recv_entry(pcloneframe);
		} else {
			ret = -1;
			RTW_INFO("%s()-%d: rtw_os_alloc_recvframe() failed!\n", __func__, __LINE__);
		}

	}
	return ret;
}

void rtw_mi_buddy_clone_bcmc_packet(_adapter *padapter, union recv_frame *precvframe, u8 *pphy_status)
{
	int i;
	s32 ret = _SUCCESS;
	_adapter *iface = NULL;
	union recv_frame *pcloneframe = NULL;
	struct recv_priv *precvpriv = &padapter->recvpriv;/*primary_padapter*/
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 *fhead = get_recvframe_data(precvframe);
	u8 type = GetFrameType(fhead);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface || iface == padapter)
			continue;
		if (rtw_is_adapter_up(iface) == _FALSE || iface->registered == 0)
			continue;
		if (type == WIFI_DATA_TYPE && !adapter_allow_bmc_data_rx(iface))
			continue;

		pcloneframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (pcloneframe) {
			ret = _rtw_mi_buddy_clone_bcmc_packet(iface, precvframe, pphy_status, pcloneframe);
			if (_SUCCESS != ret) {
				if (ret == -1)
					rtw_free_recvframe(pcloneframe, pfree_recv_queue);
				/*RTW_INFO(ADPT_FMT"-clone BC/MC frame failed\n", ADPT_ARG(iface));*/
			}
		}
	}

}

#ifdef CONFIG_PCI_HCI
/*API be created temporary for MI, caller is interrupt-handler, PCIE's interrupt handler cannot apply to multi-AP*/
_adapter *rtw_mi_get_ap_adapter(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	int i;
	_adapter *iface = NULL;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE
		    && check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
			break;

	}
	return iface;
}
#endif

void rtw_mi_update_ap_bmc_camid(_adapter *padapter, u8 camid_a, u8 camid_b)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	int i;
	_adapter *iface = NULL;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		if (macid_ctl->iface_bmc[iface->iface_id] != INVALID_SEC_MAC_CAM_ID) {
			if (macid_ctl->iface_bmc[iface->iface_id] == camid_a)
				macid_ctl->iface_bmc[iface->iface_id] = camid_b;
			else if (macid_ctl->iface_bmc[iface->iface_id] == camid_b)
				macid_ctl->iface_bmc[iface->iface_id] = camid_a;
			iface->securitypriv.dot118021x_bmc_cam_id  = macid_ctl->iface_bmc[iface->iface_id];
		}
	}
#endif
}

