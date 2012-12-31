/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL871X_MP_IOCTL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <mlme_osdep.h>

#include <rtl871x_mp.h>
#include <rtl871x_mp_ioctl.h>
#include <rtl871x_mp_phy_regdef.h>

#ifdef PLATFORM_LINUX
NDIS_STATUS oid_null_function(struct oid_par_priv *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}
#endif

//****************  oid_rtl_seg_81_85   section start ****************
NDIS_STATUS oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid == SET_OID)
	{
		if (poid_par_priv->information_buf_len >= sizeof(u8))
			Adapter->registrypriv.wireless_mode = *(u8*)poid_par_priv->information_buf;
		else
			status = NDIS_STATUS_INVALID_LENGTH;
	}
	else if (poid_par_priv->type_of_oid == QUERY_OID)
	{
		if (poid_par_priv->information_buf_len >= sizeof(u8)) {
			*(u8*)poid_par_priv->information_buf = Adapter->registrypriv.wireless_mode;
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
			 RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-query Wireless Mode=%d\n", Adapter->registrypriv.wireless_mode));
		} else
			status = NDIS_STATUS_INVALID_LENGTH;
	} else {
		status = NDIS_STATUS_NOT_ACCEPTED;
	}

_func_exit_;

	return status;
}
//****************  oid_rtl_seg_81_87_80   section start ****************
NDIS_STATUS oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	_irqL oldirql;

	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_write_bb_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);

	offset = (u16)(pbbreg->offset) & 0xFFF; //0ffset :0x800~0xfff
	if (offset < BB_REG_BASE_ADDR) offset |= BB_REG_BASE_ADDR;

	value = pbbreg->value;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("oid_rt_pro_write_bb_reg_hdl: offset=0x%04x value=0x%08x\n",
		  offset, value));

	_irqlevel_changed_(&oldirql, LOWER);
	bb_reg_write(Adapter, offset, value);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	_irqL oldirql;

	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_read_bb_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);

	offset = (u16)(pbbreg->offset) & 0xFFF; //0ffset :0x800~0xfff
	if (offset < BB_REG_BASE_ADDR) offset |= BB_REG_BASE_ADDR;

	_irqlevel_changed_(&oldirql, LOWER);
	value = bb_reg_read(Adapter, offset);
	_irqlevel_changed_(&oldirql, RAISE);

	pbbreg->value = value;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-oid_rt_pro_read_bb_reg_hdl: offset=0x%04x value:0x%08x\n",
		  offset, value));
_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	_irqL oldirql;

	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_write_rf_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);

	path = (u8)pbbreg->path;
	if (path > RF_PATH_B)
		return NDIS_STATUS_NOT_ACCEPTED;

	offset = (u8)pbbreg->offset;
	value = pbbreg->value;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("oid_rt_pro_write_rf_reg_hdl: path=%d offset=0x%02x value=0x%08x\n",
		  path, offset, value));

	_irqlevel_changed_(&oldirql, LOWER);
 	rf_reg_write(Adapter, path, offset, value);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	_irqL oldirql;

	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_read_rf_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);

	path = (u8)pbbreg->path;
	if (path > RF_PATH_B) // 1T2R  path_a /path_b
		return NDIS_STATUS_NOT_ACCEPTED;

	offset = (u8)pbbreg->offset;

	_irqlevel_changed_(&oldirql, LOWER);
	value = rf_reg_read(Adapter, path, offset);
	_irqlevel_changed_(&oldirql, RAISE);

	pbbreg->value = value;

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-oid_rt_pro_read_rf_reg_hdl: path=%d offset=0x%02x value=0x%08x\n",
		  path, offset, value));

_func_exit_;

	return status;
}
//****************  oid_rtl_seg_81_87_00   section end****************
//------------------------------------------------------------------------------
//This function initializes the DUT to the MP test mode
int mp_start_test(_adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	NDIS_WLAN_BSSID_EX bssid;
	struct sta_info *psta;
	unsigned long length;

		_irqL irqL;
	int res = _SUCCESS;


	//3 1. initialize a new NDIS_WLAN_BSSID_EX
//	_memset(&bssid, 0, sizeof(NDIS_WLAN_BSSID_EX));

	_memcpy(bssid.MacAddress, pmppriv->network_macaddr, ETH_ALEN);
	bssid.Ssid.SsidLength = 16;
	_memcpy(bssid.Ssid.Ssid, (unsigned char*)"mp_pseudo_adhoc", bssid.Ssid.SsidLength);
	bssid.InfrastructureMode = Ndis802_11IBSS;
	bssid.NetworkTypeInUse = Ndis802_11DS;
	bssid.IELength = 0;

	length = get_NDIS_WLAN_BSSID_EX_sz(&bssid);
	if (length % 4)
		bssid.Length = ((length >> 2) + 1) << 2; //round up to multiple of 4 bytes.
	else
		bssid.Length = length;

	_enter_critical(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		goto end_of_mp_start_test;

	//init mp_start_test status
	pmppriv->prev_fw_state = get_fwstate(pmlmepriv);
	pmlmepriv->fw_state = WIFI_MP_STATE;

	if (pmppriv->mode == _LOOPBOOK_MODE_) {
		set_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE); //append txdesc
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+start mp in Lookback mode\n"));
	} else {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+start mp in normal mode\n"));
	}

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	//3 2. create a new psta for mp driver
	//clear psta in the cur_network, if any
	psta = get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta) free_stainfo(padapter, psta);

	psta = alloc_stainfo(&padapter->stapriv, bssid.MacAddress);
	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("mp_start_test: Can't alloc sta_info!\n"));
		res = _FAIL;
		goto end_of_mp_start_test;
	}

	//3 3. join psudo AdHoc
	tgt_network->join_res = 1;
	tgt_network->aid = psta->aid = 1;
	_memcpy(&tgt_network->network, &bssid, length);

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	os_indicate_connect(padapter);
	set_fwstate(pmlmepriv, _FW_LINKED); // Set to LINKED STATE for MP TRX Testing

end_of_mp_start_test:

	_exit_critical(&pmlmepriv->lock, &irqL);

	return res;
}
//------------------------------------------------------------------------------
//This function change the DUT from the MP test mode into normal mode
int mp_stop_test(_adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info *psta;

	_irqL irqL;


	_enter_critical(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE)
		goto end_of_mp_stop_test;

	//3 1. disconnect psudo AdHoc
	indicate_disconnect(padapter);
//	os_indicate_disconnect(padapter);

	//3 2. clear psta used in mp test mode.
//	free_assoc_resources(padapter);
	psta = get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta) free_stainfo(padapter, psta);

	//3 3. return to normal state (default:station mode)
	pmlmepriv->fw_state = pmppriv->prev_fw_state; // WIFI_STATION_STATE;

	//flush the cur_network
	_memset(tgt_network, 0, sizeof(struct wlan_network));

end_of_mp_stop_test:

	_exit_critical(&pmlmepriv->lock, &irqL);

	return _SUCCESS;
}
//------------------------------------------------------------------------------
int mp_start_joinbss(_adapter *padapter, NDIS_802_11_SSID *pssid)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = (struct wlan_network *)&pmppriv->mp_network;

	unsigned char res = _SUCCESS;

#if 1
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE)
		return _FAIL;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)
		return _FAIL;

	_clr_fwstate_(pmlmepriv, _FW_LINKED);
#else
	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE))
		pmlmepriv->fw_state ^= _FW_LINKED;
	else
		return _FAIL;
#endif
	res = setassocsta_cmd(padapter, pmppriv->network_macaddr);

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

#if 0
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &mp_priv->mp_network;

	unsigned char *dst_ssid, *src_ssid;
	unsigned char res = _SUCCESS;

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) )
		pmlmepriv->fw_state ^= _FW_LINKED;
	else
		return _FAIL;

	_memcpy(&padapter->MgntInfo.ssid, pssid, sizeof(NDIS_802_11_SSID));
	_memcpy(&pmlmepriv->assoc_ssid, pssid, sizeof(NDIS_802_11_SSID));

	pmlmepriv->cur_network.join_res = -2;
	pmlmepriv->fw_state |= _FW_UNDER_LINKING;

	dst_ssid = pnetwork->network.Ssid.Ssid;
	src_ssid = pmlmepriv->assoc_ssid.Ssid;
	if (_memcmp(dst_ssid, src_ssid, pmlmepriv->assoc_ssid.SsidLength) == _FALSE)
		return _FAIL;

	res = joinbss_cmd((unsigned char*)padapter, pnetwork);

	NdisMSetTimer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
#endif

	return res;
}

//****************  oid_rtl_seg_81_80_00   section start ****************
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_data_rate_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

//	unsigned char	datarates[NumRates];
	u32		ratevalue;//4

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("+oid_rt_pro_set_data_rate_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	ratevalue = *((u32*)poid_par_priv->information_buf);//4
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("oid_rt_pro_set_data_rate_hdl: data rate idx=%d\n", ratevalue));
	if (ratevalue >= MPT_RATE_LAST)
		return NDIS_STATUS_INVALID_DATA;

	_irqlevel_changed_(&oldirql, LOWER);
#if 0
	for (i = 0; i< NumRates; i++)
	{
		if (ratevalue == mpdatarate[i]) {
			datarates[i] = mpdatarate[i];
		} else {
			datarates[i] = 0xff;
		}
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("datarate_inx=%d\n", datarates[i]));
	}

	if (setdatarate_cmd(Adapter, datarates) != _SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;
#else
	Adapter->mppriv.curr_rateidx = ratevalue;
	SetDataRate(Adapter);
#endif
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		mode;
	u8		val8;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_start_test_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return  NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	//IQCalibrateBcut(Adapter);

	mode = *((u32*)poid_par_priv->information_buf);
	Adapter->mppriv.mode = mode;// 1 for loopback

	if (mp_start_test(Adapter) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;

	write8(Adapter, MSR, 1); // Link in ad hoc network, 0x1025004C
	write8(Adapter, RCR, 0); // RCR : disable all pkt, 0x10250048
	write8(Adapter, RCR+2, 0x57); // RCR disable Check BSSID, 0x1025004a

	//disable RX filter map , mgt frames will put in RX FIFO 0
	write16(Adapter, RXFLTMAP0, 0x0); // 0x10250116

	val8 = read8(Adapter, EE_9346CR); // 0x1025000A
	if (!(val8 & _9356SEL))//boot from EFUSE
	{
		efuse_reg_init(Adapter);
		efuse_change_max_size(Adapter);
		efuse_reg_uninit(Adapter);
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("-oid_rt_pro_start_test_hdl: mp_mode=%d\n", Adapter->mppriv.mode));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+Set OID_RT_PRO_STOP_TEST\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (mp_stop_test(Adapter) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("-Set OID_RT_PRO_STOP_TEST\n"));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_channel_direct_call_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		Channel;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_set_channel_direct_call_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	Channel = *((u32*)poid_par_priv->information_buf);
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("oid_rt_pro_set_channel_direct_call_hdl: Channel=%d\n", Channel));
	if (Channel > 14)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.curr_ch = Channel;

#if 0
	if (setphy_cmd(Adapter, (unsigned char)Adapter->mppriv.curr_modem, (unsigned char)Channel) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;
#else
	_irqlevel_changed_(&oldirql, LOWER);
	SetChannel(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);
#endif

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_antenna_bb_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		antenna;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_set_antenna_bb_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	antenna = *((u32*)poid_par_priv->information_buf);

	Adapter->mppriv.antenna_tx = (u16)((antenna & 0xFFFF0000) >> 16);
	Adapter->mppriv.antenna_rx = (u16)(antenna & 0x0000FFFF);
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("tx_ant=0x%04x rx_ant=0x%04x\n",
		  Adapter->mppriv.antenna_tx, Adapter->mppriv.antenna_rx));

	_irqlevel_changed_(&oldirql,LOWER);
	SwitchAntenna(Adapter);
	_irqlevel_changed_(&oldirql,RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_tx_power_control_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		tx_pwr_idx;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+oid_rt_pro_set_tx_power_control_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	tx_pwr_idx = *((u32*)poid_par_priv->information_buf);
	if (tx_pwr_idx > MAX_TX_PWR_INDEX_N_MODE)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.curr_txpoweridx = (u8)tx_pwr_idx;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("oid_rt_pro_set_tx_power_control_hdl: idx=0x%2x\n",
		  Adapter->mppriv.curr_txpoweridx));

	_irqlevel_changed_(&oldirql, LOWER);
	SetTxPower(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

//------------------------------------------------------------------------------
//****************  oid_rtl_seg_81_80_20   section start ****************
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_query_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid !=QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG*)poid_par_priv->information_buf =  Adapter->mppriv.tx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_query_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("===> oid_rt_pro_query_rx_packet_received_hdl.\n"));
	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG*)poid_par_priv->information_buf =  Adapter->mppriv.rx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("recv_ok:%d \n",Adapter->mppriv.rx_pktcount));
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_query_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("===> oid_rt_pro_query_rx_packet_crc32_error_hdl.\n"));
	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG*)poid_par_priv->information_buf =  Adapter->mppriv.rx_crcerrpktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("recv_err:%d \n",Adapter->mppriv.rx_crcerrpktcount));
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------

NDIS_STATUS oid_rt_pro_reset_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("===> oid_rt_pro_reset_tx_packet_sent_hdl.\n"));
	Adapter->mppriv.tx_pktcount = 0;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_reset_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
	{
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		Adapter->mppriv.rx_pktcount = 0;
		Adapter->mppriv.rx_crcerrpktcount = 0;
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_reset_phy_rx_packet_count_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	_irqL		oldirql;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	ResetPhyRxPktCount(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_phy_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+oid_rt_get_phy_rx_packet_received_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(ULONG))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(ULONG*)poid_par_priv->information_buf = GetPhyRxPktReceived(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("-oid_rt_get_phy_rx_packet_received_hdl: recv_ok=%d\n", *(ULONG*)poid_par_priv->information_buf));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_phy_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+oid_rt_get_phy_rx_packet_crc32_error_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;


	if (poid_par_priv->information_buf_len != sizeof(ULONG))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(ULONG*)poid_par_priv->information_buf = GetPhyRxPktCRC32Error(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-oid_rt_get_phy_rx_packet_crc32_error_hdl: recv_err=%d\n", *(ULONG*)poid_par_priv->information_buf));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_modulation_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+OID_RT_PRO_SET_MODULATION\n"));
	Adapter->mppriv.curr_modem = *((u8*)poid_par_priv->information_buf);

_func_exit_;

	return status;
}

//****************  oid_rtl_seg_81_80_20   section end ****************
NDIS_STATUS oid_rt_pro_set_continuous_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		bStartTest;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_set_continuous_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32*)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetContinuousTx(Adapter,(u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_single_carrier_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		bStartTest;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("+oid_rt_pro_set_single_carrier_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32*)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleCarrierTx(Adapter, (u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_carrier_suppression_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		bStartTest;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_set_carrier_suppression_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32*)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetCarrierSuppressionTx(Adapter, (u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_single_tone_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32 		bStartTest;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("+oid_rt_pro_set_single_tone_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32*)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleToneTx(Adapter,(u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

//****************  oid_rtl_seg_81_80_00   section end ****************
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro8711_join_bss_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PNDIS_802_11_SSID pssid;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = (u32)sizeof(NDIS_802_11_SSID);
	*poid_par_priv->bytes_rw = 0;
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	pssid = (PNDIS_802_11_SSID)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (mp_start_joinbss(Adapter, pssid) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = sizeof(NDIS_802_11_SSID);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pRW_Reg 	RegRWStruct;
	u16		offset;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("+oid_rt_pro_read_register_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RegRWStruct = (pRW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if ((RegRWStruct->offset >= 0x10250800) &&
	    (RegRWStruct->offset <= 0x10250FFF))
	{
		//baseband register
		offset = (u16)(RegRWStruct->offset) & 0xFFF;  	//0ffset :0x800~0xfff

		RegRWStruct->value = bb_reg_read(Adapter ,offset);
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
			 ("oid_rt_pro_read_register_hdl: bb offset=0x%04x value=0x%x\n",
			  offset, RegRWStruct->value));
	}
	else
	{
		switch (RegRWStruct->width)
		{
			case 1:
				RegRWStruct->value = read8(Adapter, RegRWStruct->offset);
				break;
			case 2:
				RegRWStruct->value = read16(Adapter, RegRWStruct->offset);
				break;
			case 4:
				RegRWStruct->value = read32(Adapter, RegRWStruct->offset);
				break;
			default:
				status = NDIS_STATUS_NOT_ACCEPTED;
				break;
		}
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
			 ("oid_rt_pro_read_register_hdl: offset:0x%04x value:0x%x\n",
			  RegRWStruct->offset, RegRWStruct->value));
	}

	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	//DEBUG_ERR(("\n <=== Query OID_RT_PRO_READ_REGISTER.
	//	Add:0x%08x Width:%d Value:0x%08x\n",RegRWStruct->offset,RegRWStruct->width,RegRWStruct->value));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pRW_Reg 	RegRWStruct;
	u16		offset;
	u32		value;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("\n ===> Set OID_RT_PRO_WRITE_REGISTER.\n"));

	RegRWStruct = (pRW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if ((RegRWStruct->offset >= 0x10250800) &&
	    (RegRWStruct->offset <= 0x10250FFF))
	{
		//baseband register
		offset = (u16)(RegRWStruct->offset) & 0xFFF;  	//0ffset :0x800~0xfff
		value = RegRWStruct->value;
#if 1
{
		u32 oldValue = 0;
		switch (RegRWStruct->width)
		{
			case 1:
				oldValue = bb_reg_read(Adapter, offset);
				oldValue &= 0xFFFFFF00;
				value &= 0x000000FF;
				value |= oldValue;
				break;
			case 2:
				oldValue = bb_reg_read(Adapter, offset);
				oldValue &= 0xFFFF0000;
				value &= 0x0000FFFF;
				value |= oldValue;
				break;
		}
}
#else
// reduce IO access
		if ( (RegRWStruct->width == 1) ||
		     (RegRWStruct->width == 2) )
		{
			u32 oldValue = 0;
			u8 shift = offset & 0x3;
			u32 mask = 0x000000FF;

			if (RegRWStruct->width == 2) {
				mask = 0x0000FFFF;
				if (shift == 3)
					shift = 0;
			}

			if (shift != 0) {
				mask <<= (shift * 8);
				offset &= 0xFFC;
			}

			oldValue = bb_reg_read(Adapter, offset);
			oldValue &= ~mask;
			value = (value << (shift * 8)) & mask;
			value |= oldValue;
		}
#endif
		bb_reg_write(Adapter, offset, value);
	}
	else
	{
		switch (RegRWStruct->width)
		{
			case 1:
				write8(Adapter, RegRWStruct->offset, (unsigned char)RegRWStruct->value);
				break;
			case 2:
				write16(Adapter, RegRWStruct->offset, (unsigned short)RegRWStruct->value);
				break;
			case 4:
				write32(Adapter, RegRWStruct->offset, (unsigned int)RegRWStruct->value);
				break;
			default:
				status = NDIS_STATUS_NOT_ACCEPTED;
				break;
		}

		//Henry: for maintain the correct HIMR status
		if ((status == NDIS_STATUS_SUCCESS) &&
		    (RegRWStruct->offset == HIMR) &&
		    (RegRWStruct->width == 4))
			Adapter->ImrContent=RegRWStruct->value;
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("\n <=== Set OID_RT_PRO_WRITE_REGISTER.offset:0x%08x value0x%x\n",RegRWStruct->offset,RegRWStruct->value));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_burst_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pBurst_RW_Reg	pBstRwReg;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_burst_read_register_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pBstRwReg = (pBurst_RW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	read_mem(Adapter, pBstRwReg->offset, (u32)pBstRwReg->len, pBstRwReg->Data);
	_irqlevel_changed_(&oldirql,RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-oid_rt_pro_burst_read_register_hdl\n"));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_burst_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pBurst_RW_Reg	pBstRwReg;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_burst_write_register_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pBstRwReg = (pBurst_RW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	write_mem(Adapter, pBstRwReg->offset, (u32)pBstRwReg->len, pBstRwReg->Data);
	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-oid_rt_pro_burst_write_register_hdl\n"));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_write_txcmd_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
/*
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);

	_irqL		oldirql;

	TX_CMD_Desc	*TxCmd_Info;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+Set OID_RT_PRO_WRITE_TXCMD\n"));

	TxCmd_Info=(TX_CMD_Desc*)poid_par_priv->information_buf;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("WRITE_TXCMD:Addr=%.8X\n", TxCmd_Info->offset));
  	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("WRITE_TXCMD:1.)%.8X\n", (ULONG)TxCmd_Info->TxCMD.value[0]));
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("WRITE_TXCMD:2.)%.8X\n", (ULONG)TxCmd_Info->TxCMD.value[1]));
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, (("WRITE_TXCMD:3.)%.8X\n", (ULONG)TxCmd_Info->TxCMD.value[2]));
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("WRITE_TXCMD:4.)%.8X\n", (ULONG)TxCmd_Info->TxCMD.value[3]));

	_irqlevel_changed_(&oldirql, LOWER);

	write32(Adapter, TxCmd_Info->offset + 0, (unsigned int)TxCmd_Info->TxCMD.value[0]);
	write32(Adapter, TxCmd_Info->offset + 4, (unsigned int)TxCmd_Info->TxCMD.value[1]);

	_irqlevel_changed_(&oldirql, RAISE);
*/

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-Set OID_RT_PRO_WRITE_TXCMD: status=%d\n", status));

_func_exit_;

	return status;
}

//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read16_eeprom_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pEEPROM_RWParam pEEPROM;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+Query OID_RT_PRO_READ16_EEPROM\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pEEPROM = (pEEPROM_RWParam)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	pEEPROM->value = eeprom_read16(Adapter, (u16)(pEEPROM->offset >> 1));
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_,  _drv_notice_,
		 ("-Query OID_RT_PRO_READ16_EEPROM: offset=0x%x value=0x%x\n",
		  pEEPROM->offset, pEEPROM->value));

_func_exit_;

	return status;
}

//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_write16_eeprom_hdl (struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	pEEPROM_RWParam pEEPROM;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+Set OID_RT_PRO_WRITE16_EEPROM\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pEEPROM = (pEEPROM_RWParam)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	eeprom_write16(Adapter, (u16)(pEEPROM->offset >> 1), pEEPROM->value);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro8711_wi_poll_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct mp_wiparam *pwi_param;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct mp_wiparam))
		return NDIS_STATUS_INVALID_LENGTH;

	if (Adapter->mppriv.workparam.bcompleted == _FALSE)
		return NDIS_STATUS_NOT_ACCEPTED;

	pwi_param = (struct mp_wiparam *)poid_par_priv->information_buf;

	_memcpy(pwi_param, &Adapter->mppriv.workparam, sizeof(struct mp_wiparam));
	Adapter->mppriv.act_in_progress = _FALSE;
//	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("rf:%x\n", pwiparam->IoValue));
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro8711_pkt_loss_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro8711_pkt_loss_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(uint)*2) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("-oid_rt_pro8711_pkt_loss_hdl: buf_len=%d\n", (int)poid_par_priv->information_buf_len));
		return NDIS_STATUS_INVALID_LENGTH;
	}

	if (*(uint*)poid_par_priv->information_buf == 1)//init==1
		Adapter->mppriv.rx_pktloss = 0;

	*((uint*)poid_par_priv->information_buf+1) = Adapter->mppriv.rx_pktloss;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_rd_attrib_mem_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)( poid_par_priv->adapter_context);
	struct io_queue *pio_queue = (struct io_queue *)Adapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;

	_irqL	oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

#ifdef CONFIG_SDIO_HCI
	void (*_attrib_read)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#endif

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+Query OID_RT_RD_ATTRIB_MEM\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_SDIO_HCI
	_irqlevel_changed_(&oldirql, LOWER);
{
	u32 *plmem = (u32*)poid_par_priv->information_buf+2;
	_attrib_read = pintfhdl->io_ops._attrib_read;
	_attrib_read(pintfhdl, *((u32*)poid_par_priv->information_buf),
				*((u32*)poid_par_priv->information_buf+1), (u8*)plmem);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
}
	_irqlevel_changed_(&oldirql, RAISE);
#endif

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_wr_attrib_mem_hdl (struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	struct io_queue *pio_queue = (struct io_queue *)Adapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

#ifdef CONFIG_SDIO_HCI
	void (*_attrib_write)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#endif

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_SDIO_HCI
	_irqlevel_changed_(&oldirql, LOWER);
{
	u32 *plmem = (u32*)poid_par_priv->information_buf + 2;
	_attrib_write = pintfhdl->io_ops._attrib_write;
	_attrib_write(pintfhdl, *(u32*)poid_par_priv->information_buf,
				*((u32*)poid_par_priv->information_buf+1), (u8*)plmem);
}
	_irqlevel_changed_(&oldirql, RAISE);
#endif

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS  oid_rt_pro_set_rf_intfs_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+OID_RT_PRO_SET_RF_INTFS\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (setrfintfs_cmd(Adapter, *(unsigned char*)poid_par_priv->information_buf) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_poll_rx_status_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_memcpy(poid_par_priv->information_buf, (unsigned char*)&Adapter->mppriv.rxstat, sizeof(struct recv_stat));
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_cfg_debug_message_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PCFG_DBG_MSG_STRUCT pdbg_msg;

_func_enter_;

//	RT_TRACE(0xffffffffff,_drv_alert_,("===> oid_rt_pro_cfg_debug_message_hdl.\n"));

#if 0//#ifdef CONFIG_DEBUG_RTL871X

	pdbg_msg = (PCFG_DBG_MSG_STRUCT)(poid_par_priv->information_buf);

	if (poid_par_priv->type_of_oid == SET_OID) {
		RT_TRACE(0xffffffffff, _drv_alert_,
			 ("===>Set level :0x%08x, H32:0x%08x L32:0x%08x\n",
			  pdbg_msg->DebugLevel, pdbg_msg->DebugComponent_H32, pdbg_msg->DebugComponent_L32));

		GlobalDebugLevel = pdbg_msg->DebugLevel;
		GlobalDebugComponents = (pdbg_msg->DebugComponent_H32 << 32) | pdbg_msg->DebugComponent_L32;
		RT_TRACE(0xffffffffff, _drv_alert_,
			 ("===> Set level :0x%08x, component:0x%016x\n",
			  GlobalDebugLevel, (u32)GlobalDebugComponents));
	} else {
		pdbg_msg->DebugLevel = GlobalDebugLevel;
		pdbg_msg->DebugComponent_H32 = (u32)(GlobalDebugComponents >> 32);
		pdbg_msg->DebugComponent_L32 = (u32)GlobalDebugComponents;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

		RT_TRACE(0xffffffffff, _drv_alert_,
			 ("===>Query level:0x%08x H32:0x%08x L32:0x%08x\n",
			  (u32)pdbg_msg->DebugLevel, (u32)pdbg_msg->DebugComponent_H32, (u32)pdbg_msg->DebugComponent_L32));
	}

#endif

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_data_rate_ex_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+OID_RT_PRO_SET_DATA_RATE_EX\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (setdatarate_cmd(Adapter, poid_par_priv->information_buf) !=_SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//-----------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_thermal_meter_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_get_thermal_meter_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (Adapter->mppriv.act_in_progress == _TRUE)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	//init workparam
	Adapter->mppriv.act_in_progress = _TRUE;
	Adapter->mppriv.workparam.bcompleted = _FALSE;
	Adapter->mppriv.workparam.act_type = MPT_GET_THERMAL_METER;
	Adapter->mppriv.workparam.io_offset = 0;
	Adapter->mppriv.workparam.io_value = 0xFFFFFFFF;

	_irqlevel_changed_(&oldirql, LOWER);
	GetThermalMeter(Adapter, &Adapter->mppriv.workparam.io_value);
	_irqlevel_changed_(&oldirql,RAISE);

	Adapter->mppriv.workparam.bcompleted = _TRUE;
	Adapter->mppriv.act_in_progress = _FALSE;

	*(u32*)poid_par_priv->information_buf = Adapter->mppriv.workparam.io_value;
	*poid_par_priv->bytes_rw = sizeof(u32);

_func_exit_;

	return status;
}
//-----------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read_tssi_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_read_tssi_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (Adapter->mppriv.act_in_progress == _TRUE)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	//init workparam
	Adapter->mppriv.act_in_progress = _TRUE;
	Adapter->mppriv.workparam.bcompleted = _FALSE;
	Adapter->mppriv.workparam.act_type = MPT_READ_TSSI;
	Adapter->mppriv.workparam.io_offset = 0;
	Adapter->mppriv.workparam.io_value = 0xFFFFFFFF;

	_irqlevel_changed_(&oldirql, LOWER);

	if (!gettssi_cmd(Adapter,0, (u8*)&Adapter->mppriv.workparam.io_value))
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_power_tracking_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("+oid_rt_pro_set_power_tracking_hdl: type=0x%02x\n",
		  *((u8*)poid_par_priv->information_buf )));

	_irqlevel_changed_(&oldirql, LOWER);

	if (!setptm_cmd(Adapter,*((u8*)poid_par_priv->information_buf )))
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//-----------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_basic_rate_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32 ratevalue;
	u8 datarates[NumRates];
	int i;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+OID_RT_PRO_SET_BASIC_RATE\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	ratevalue = *((u32*)poid_par_priv->information_buf);

	for (i = 0; i < NumRates; i++) {
		if (ratevalue == mpdatarate[i])
			datarates[i] = mpdatarate[i];
		else
			datarates[i] = 0xff;
		RT_TRACE(_module_rtl871x_ioctl_c_, _drv_info_, ("basicrate_inx=%d\n", datarates[i]));
	}

	_irqlevel_changed_(&oldirql, LOWER);

	if (setbasicrate_cmd(Adapter, datarates) != _SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-OID_RT_PRO_SET_BASIC_RATE: status=%d\n", status));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_qry_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_PWRCTRL

	if (poid_par_priv->information_buf_len < 8)
		return NDIS_STATUS_INVALID_LENGTH;

	*poid_par_priv->bytes_rw = 8;
	_memcpy(poid_par_priv->information_buf, &(Adapter->pwrctrlpriv.pwr_mode), 8);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-oid_rt_pro_qry_pwrstate_hdl: pwr_mode=%d smart_ps=%d\n",
		  Adapter->pwrctrlpriv.pwr_mode, Adapter->pwrctrlpriv.smart_ps));
#endif

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_set_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	uint pwr_mode, smart_ps;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+Set OID_RT_PRO_SET_PWRSTATE\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_PWRCTRL

	*poid_par_priv->bytes_rw = 0;
	*poid_par_priv->bytes_needed = 8;

	if (poid_par_priv->information_buf_len < 8)
		return NDIS_STATUS_INVALID_LENGTH;

	pwr_mode = *(uint *)(poid_par_priv->information_buf);
	smart_ps = *(uint *)((int)poid_par_priv->information_buf + 4);
	if (pwr_mode != Adapter->pwrctrlpriv.pwr_mode || smart_ps != Adapter->pwrctrlpriv.smart_ps)
		set_ps_mode(Adapter, pwr_mode, smart_ps);

	*poid_par_priv->bytes_rw = 8;

#endif

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_h2c_set_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct setratable_parm *prate_table;
	u8		res;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed  = sizeof(struct setratable_parm);
	if (poid_par_priv->information_buf_len < sizeof(struct setratable_parm))
		return NDIS_STATUS_INVALID_LENGTH;

	prate_table = (struct setratable_parm*)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	res = setrttbl_cmd(Adapter, prate_table);
	_irqlevel_changed_(&oldirql, RAISE);

	if (res == _FAIL)
		status = NDIS_STATUS_FAILURE;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_h2c_get_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	#if 0
			struct mp_wi_cntx *pmp_wi_cntx=&(Adapter->mppriv.wi_cntx);
			u8 res=_SUCCESS;
			DEBUG_INFO(("===> Set OID_RT_PRO_H2C_GET_RATE_TABLE.\n"));

			if(pmp_wi_cntx->bmp_wi_progress ==_TRUE){
				DEBUG_ERR(("\n mp workitem is progressing, not allow to set another workitem right now!!!\n"));
				Status = NDIS_STATUS_NOT_ACCEPTED;
				break;
			}
			else{
				pmp_wi_cntx->bmp_wi_progress=_TRUE;
				pmp_wi_cntx->param.bcompleted=_FALSE;
				pmp_wi_cntx->param.act_type=MPT_GET_RATE_TABLE;
				pmp_wi_cntx->param.io_offset=0x0;
				pmp_wi_cntx->param.bytes_cnt=sizeof(struct getratable_rsp);
				pmp_wi_cntx->param.io_value=0xffffffff;

				res=getrttbl_cmd(Adapter,(struct getratable_rsp *)pmp_wi_cntx->param.data);
				*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
				if(res != _SUCCESS)
				{
					Status = NDIS_STATUS_NOT_ACCEPTED;
				}
			}
			DEBUG_INFO(("\n <=== Set OID_RT_PRO_H2C_GET_RATE_TABLE.\n"));
	#endif

_func_exit_;

	return status;
}

//****************  oid_rtl_seg_87_12_00   section start ****************
NDIS_STATUS oid_rt_pro_encryption_ctrl_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	struct security_priv *psecuritypriv = &Adapter->securitypriv;

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	ENCRY_CTRL_STATE encry_mode;


	*poid_par_priv->bytes_needed = sizeof(u8);
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID)
	{
		encry_mode = *((u8*)poid_par_priv->information_buf);
		switch (encry_mode)
		{
			case HW_CONTROL:
				#if 0
				Adapter->registrypriv.software_decrypt=_FALSE;
				Adapter->registrypriv.software_encrypt=_FALSE;
				#else
				psecuritypriv->sw_decrypt = _FALSE;
				psecuritypriv->sw_encrypt = _FALSE;
				#endif
				break;
			case SW_CONTROL:
				#if 0
				Adapter->registrypriv.software_decrypt=_TRUE;
				Adapter->registrypriv.software_encrypt=_TRUE;
				#else
				psecuritypriv->sw_decrypt = _TRUE;
				psecuritypriv->sw_encrypt = _TRUE;
				#endif
				break;
			case HW_ENCRY_SW_DECRY:
				#if 0
				Adapter->registrypriv.software_decrypt=_TRUE;
				Adapter->registrypriv.software_encrypt=_FALSE;
				#else
				psecuritypriv->sw_decrypt = _TRUE;
				psecuritypriv->sw_encrypt = _FALSE;
				#endif
				break;
			case SW_ENCRY_HW_DECRY:
				#if 0
				Adapter->registrypriv.software_decrypt=_FALSE;
				Adapter->registrypriv.software_encrypt=_TRUE;
				#else
				psecuritypriv->sw_decrypt = _FALSE;
				psecuritypriv->sw_encrypt = _TRUE;
				#endif
				break;
		}

		RT_TRACE(_module_rtl871x_ioctl_c_, _drv_notice_,
			 ("-oid_rt_pro_encryption_ctrl_hdl: SET encry_mode=0x%x sw_encrypt=0x%x sw_decrypt=0x%x\n",
			  encry_mode, psecuritypriv->sw_encrypt, psecuritypriv->sw_decrypt));
	}
	else {
		#if 0
		if (Adapter->registrypriv.software_encrypt == _FALSE) {
			if (Adapter->registrypriv.software_decrypt == _FALSE)
				encry_mode = HW_CONTROL;
			else
				encry_mode = HW_ENCRY_SW_DECRY;
		}
		else {
			if (Adapter->registrypriv.software_decrypt == _FALSE)
				encry_mode = SW_ENCRY_HW_DECRY;
			else
				encry_mode = SW_CONTROL;
		}
		#else

		if ((psecuritypriv->sw_encrypt == _FALSE) && (psecuritypriv->sw_decrypt == _FALSE))
			encry_mode = HW_CONTROL;
		else if ((psecuritypriv->sw_encrypt == _FALSE) && (psecuritypriv->sw_decrypt == _TRUE))
			encry_mode = HW_ENCRY_SW_DECRY;
		else if ((psecuritypriv->sw_encrypt == _TRUE) && (psecuritypriv->sw_decrypt == _FALSE))
			encry_mode = SW_ENCRY_HW_DECRY;
		else if ((psecuritypriv->sw_encrypt == _TRUE) && (psecuritypriv->sw_decrypt == _TRUE))
			encry_mode = SW_CONTROL;

		#endif

		*(u8*)poid_par_priv->information_buf =  encry_mode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
			 ("-oid_rt_pro_encryption_ctrl_hdl: QUERY encry_mode=0x%x\n",
			  encry_mode));
	}

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_add_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER		Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL			oldirql;
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;

	struct sta_info 	*psta = NULL;
	UCHAR 			*macaddr;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = ETH_ALEN;
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	macaddr = (UCHAR *) poid_par_priv->information_buf ;

	RT_TRACE(_module_rtl871x_ioctl_c_,_drv_notice_,
		 ("OID_RT_PRO_ADD_STA_INFO: addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  macaddr[0],macaddr[1],macaddr[2],macaddr[3],macaddr[4],macaddr[5]));

	_irqlevel_changed_(&oldirql, LOWER);

	psta = get_stainfo(&Adapter->stapriv, macaddr);

	if (psta == NULL) { // the sta have been in sta_info_queue => do nothing
		psta = alloc_stainfo(&Adapter->stapriv, macaddr);

		if (psta == NULL) {
			RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_,("Can't alloc sta_info when OID_RT_PRO_ADD_STA_INFO\n"));
			status = NDIS_STATUS_FAILURE;
		}
	} else { //(between drv has received this event before and  fw have not yet to set key to CAM_ENTRY)
		RT_TRACE(_module_rtl871x_ioctl_c_, _drv_err_,
			("Error: OID_RT_PRO_ADD_STA_INFO: sta has been in sta_hash_queue \n"));
	}

	_irqlevel_changed_(&oldirql, RAISE);

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_dele_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER		Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL			irqL;
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;

	struct sta_info 	*psta = NULL;
	UCHAR			*macaddr;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = ETH_ALEN;
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	macaddr = (UCHAR *) poid_par_priv->information_buf ;
	RT_TRACE(_module_rtl871x_ioctl_c_,_drv_notice_,
		 ("+OID_RT_PRO_ADD_STA_INFO: addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5]));

	psta = get_stainfo(&Adapter->stapriv, macaddr);
	if (psta != NULL) {
		_enter_critical(&(Adapter->stapriv.sta_hash_lock), &irqL);
		free_stainfo(Adapter, psta);
		_exit_critical(&(Adapter->stapriv.sta_hash_lock), &irqL);
	}

	return status;
}
//------------------------------------------------------------------------------
#include <sdio_osintf.h>
u32 mp_query_drv_var(_adapter *padapter, u8 offset, u32 var)
{
#ifdef CONFIG_SDIO_HCI

	if (offset == 1) {
		u16 tmp_blk_num;
		tmp_blk_num = read16(padapter, SDIO_RX0_RDYBLK_NUM);
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("Query Information, mp_query_drv_var  SDIO_RX0_RDYBLK_NUM=0x%x   padapter->dvobjpriv.rxblknum=0x%x\n", tmp_blk_num, padapter->dvobjpriv.rxblknum));
		if (padapter->dvobjpriv.rxblknum != tmp_blk_num) {
			RT_TRACE(_module_rtl871x_mp_ioctl_c_,_drv_err_, ("Query Information, mp_query_drv_var  call recv rx\n"));
		//	sd_recv_rxfifo(padapter);
		}
	}

#if 0
	if(offset <=100){  //For setting data rate and query data rate
		if(offset==100){ //For query data rate
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, ("\n mp_query_drv_var: offset(%d): query rate=0x%.2x \n",offset,padapter->registrypriv.tx_rate));
			var=padapter->registrypriv.tx_rate;

		}
		else if(offset<0x1d){  //For setting data rate
			padapter->registrypriv.tx_rate=offset;
			var=padapter->registrypriv.tx_rate;
			padapter->registrypriv.use_rate=_TRUE;
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, ("\n mp_query_drv_var: offset(%d): set rate=0x%.2x \n",offset,padapter->registrypriv.tx_rate));
		}
		else{ //not use the data rate
			padapter->registrypriv.use_rate=_FALSE;
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, ("\n mp_query_drv_var: offset(%d) out of rate range\n",offset));
		}
	}
	else if (offset<=110){  //for setting debug level
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" mp_query_drv_var: offset(%d) for set debug level\n",offset));
		if(offset==110){ //For query data rate
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" mp_query_drv_var: offset(%d): query dbg level=0x%.2x \n",offset,padapter->registrypriv.dbg_level));
			padapter->registrypriv.dbg_level=GlobalDebugLevel;
			var=padapter->registrypriv.dbg_level;
		}
		else if(offset<110 && offset>100){
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" mp_query_drv_var: offset(%d): set dbg level=0x%.2x \n",offset,offset-100));
			padapter->registrypriv.dbg_level=GlobalDebugLevel=offset-100;
			var=padapter->registrypriv.dbg_level;
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" mp_query_drv_var(_drv_emerg_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, (" mp_query_drv_var(_drv_alert_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_crit_, (" mp_query_drv_var(_drv_crit_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, (" mp_query_drv_var(_drv_err_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_warning_, (" mp_query_drv_var(_drv_warning_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, (" mp_query_drv_var(_drv_notice_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, (" mp_query_drv_var(_drv_info_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_debug_, (" mp_query_drv_var(_drv_debug_): offset(%d): set dbg level=0x%.2x \n",offset,GlobalDebugLevel));

		}
	}
	else if(offset >110 &&offset <116){
		if(115==offset){
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" mp_query_drv_var(_drv_emerg_): offset(%d): query TRX access type: [tx_block_mode=%x,rx_block_mode=%x]\n",\
															offset,padapter->dvobjpriv.tx_block_mode,padapter->dvobjpriv.rx_block_mode));
		}
		else {
			switch(offset){
				case 111:
					padapter->dvobjpriv.tx_block_mode=1;
					padapter->dvobjpriv.rx_block_mode=1;
					RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, \
						(" mp_query_drv_var(_drv_emerg_): offset(%d): SET TRX access type:(TX block/RX block) [tx_block_mode=%x,rx_block_mode=%x]\n",\
						offset,padapter->dvobjpriv.tx_block_mode,padapter->dvobjpriv.rx_block_mode));
					break;
				case 112:
					padapter->dvobjpriv.tx_block_mode=1;
					padapter->dvobjpriv.rx_block_mode=0;
					RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, \
						(" mp_query_drv_var(_drv_emerg_): offset(%d): SET TRX access type:(TX block/RX byte) [tx_block_mode=%x,rx_block_mode=%x]\n",\
						offset,padapter->dvobjpriv.tx_block_mode,padapter->dvobjpriv.rx_block_mode));
					break;
				case 113:
					padapter->dvobjpriv.tx_block_mode=0;
					padapter->dvobjpriv.rx_block_mode=1;
					RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, \
						(" mp_query_drv_var(_drv_emerg_): offset(%d): SET TRX access type:(TX byte/RX block) [tx_block_mode=%x,rx_block_mode=%x]\n",\
						offset,padapter->dvobjpriv.tx_block_mode,padapter->dvobjpriv.rx_block_mode));
					break;
				case 114:
					padapter->dvobjpriv.tx_block_mode=0;
					padapter->dvobjpriv.rx_block_mode=0;
					RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, \
						(" mp_query_drv_var(_drv_emerg_): offset(%d): SET TRX access type:(TX byte/RX byte) [tx_block_mode=%x,rx_block_mode=%x]\n",\
						offset,padapter->dvobjpriv.tx_block_mode,padapter->dvobjpriv.rx_block_mode));
					break;
				default :
					break;

			}

		}

	}
	else if(offset>=127){
		u64	prnt_dbg_comp;
		u8   chg_idx;
		u64	tmp_dbg_comp;
		chg_idx=offset-0x80;
		tmp_dbg_comp=BIT(chg_idx);
		prnt_dbg_comp=padapter->registrypriv.dbg_component= GlobalDebugComponents;
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, (" 1: mp_query_drv_var: offset(%d;0x%x):for dbg conpoment prnt_dbg_comp=0x%.16x GlobalDebugComponents=0x%.16x padapter->registrypriv.dbg_component=0x%.16x\n",offset,offset,prnt_dbg_comp,GlobalDebugComponents,padapter->registrypriv.dbg_component));
		if(offset==127){
	//		prnt_dbg_comp=padapter->registrypriv.dbg_component= GlobalDebugComponents;
			var=(u32)(padapter->registrypriv.dbg_component);
			RT_TRACE(0xffffffff, _drv_emerg_, ("2: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h)  GlobalDebugComponents=0x%x(l) 0x%x(h) \n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp));
			prnt_dbg_comp=GlobalDebugComponents;
			RT_TRACE(0xffffffff, _drv_emerg_, ("2-1: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h)  GlobalDebugComponents=0x%x(l) 0x%x(h)\n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp));
			prnt_dbg_comp=GlobalDebugComponents=padapter->registrypriv.dbg_component;
			RT_TRACE(0xffffffff, _drv_emerg_, ("2-2: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h)  GlobalDebugComponents=0x%x(l) 0x%x(h)\n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp));

		}
		else{
			RT_TRACE(0xffffffff, _drv_emerg_, ("3: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h) GlobalDebugComponents=0x%x(l) 0x%x(h) chg_idx=%d\n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp,chg_idx));
			prnt_dbg_comp=GlobalDebugComponents;
			RT_TRACE(0xffffffff, _drv_emerg_,("3-1: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h)  GlobalDebugComponents=0x%x(l) 0x%x(h) chg_idx=%d\n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp,chg_idx));// ("3-1: mp_query_drv_var: offset(%d;0x%x):before set dbg conpoment=0x%x chg_idx=%d or0x%x BIT(chg_idx[%d]=0x%x)\n",offset,offset,prnt_dbg_comp,chg_idx,chg_idx,(chg_idx),tmp_dbg_comp)
			prnt_dbg_comp=GlobalDebugComponents=padapter->registrypriv.dbg_component;
			RT_TRACE(0xffffffff, _drv_emerg_, ("3-2: mp_query_drv_var: offset(%d;0x%x):for query dbg conpoment=0x%x(l) 0x%x(h)  GlobalDebugComponents=0x%x(l) 0x%x(h)\n",offset,offset,padapter->registrypriv.dbg_component,prnt_dbg_comp));

			if(GlobalDebugComponents&tmp_dbg_comp){
				//this bit is already set, now clear it
				GlobalDebugComponents=GlobalDebugComponents&(~tmp_dbg_comp);
			}
			else{
				//this bit is not set, now set it.
				GlobalDebugComponents =GlobalDebugComponents|tmp_dbg_comp;
			}
			RT_TRACE(0xffffffff, _drv_emerg_, ("4: mp_query_drv_var: offset(%d;0x%x):before set dbg conpoment tmp_dbg_comp=0x%x GlobalDebugComponents=0x%x(l) 0x%x(h)",offset,offset,tmp_dbg_comp,prnt_dbg_comp));
			prnt_dbg_comp=GlobalDebugComponents;
			RT_TRACE(0xffffffff, _drv_emerg_, ("4-1: mp_query_drv_var: offset(%d;0x%x):before set dbg conpoment tmp_dbg_comp=0x%x GlobalDebugComponents=0x%x(l) 0x%x(h)",offset,offset,tmp_dbg_comp,prnt_dbg_comp));

			RT_TRACE(_module_rtl871x_xmit_c_, _drv_emerg_, ("0: mp_query_drv_var(_module_rtl871x_xmit_c_:0): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,prnt_dbg_comp));
			RT_TRACE(_module_xmit_osdep_c_, _drv_emerg_, ("1: mp_query_drv_var(_module_xmit_osdep_c_:1): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_recv_c_, _drv_emerg_, ("2: mp_query_drv_var(_module_rtl871x_recv_c_:2): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_recv_osdep_c_, _drv_emerg_, ("3: mp_query_drv_var(_module_recv_osdep_c_:3): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_emerg_, ("4: mp_query_drv_var(_module_rtl871x_mlme_c_:4): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_mlme_osdep_c_, _drv_emerg_, (" 5:mp_query_drv_var(_module_mlme_osdep_c_:5): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_emerg_, ("6: mp_query_drv_var(_module_rtl871x_sta_mgt_c_:6): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_emerg_, ("7: mp_query_drv_var(_module_rtl871x_cmd_c_:7): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_cmd_osdep_c_, _drv_emerg_, ("8: mp_query_drv_var(_module_cmd_osdep_c_:8): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_io_c_, _drv_emerg_, ("9: mp_query_drv_var(_module_rtl871x_io_c_:9): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_io_osdep_c_, _drv_emerg_, ("10: mp_query_drv_var(_module_io_osdep_c_:10): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_os_intfs_c_, _drv_emerg_, ("11: mp_query_drv_var(_module_os_intfs_c_:11): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_security_c_, _drv_emerg_, ("12: mp_query_drv_var(_module_rtl871x_security_c_:12): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_eeprom_c_, _drv_emerg_, ("13: mp_query_drv_var(_module_rtl871x_eeprom_c_:13): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_hal_init_c_, _drv_emerg_, ("14: mp_query_drv_var(_module_hal_init_c_:14): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_hci_hal_init_c_, _drv_emerg_, ("15: mp_query_drv_var(_module_hci_hal_init_c_:15): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_ioctl_c_, _drv_emerg_, ("16: mp_query_drv_var(_module_rtl871x_ioctl_c_:16): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_emerg_, ("17: mp_query_drv_var(_module_rtl871x_ioctl_set_c_:17): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_ioctl_query_c_, _drv_emerg_, ("18: mp_query_drv_var(_module_rtl871x_ioctl_query_c_:18): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_emerg_, ("19: mp_query_drv_var(_module_rtl871x_pwrctrl_c_:19): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_hci_intfs_c_, _drv_emerg_, ("20: mp_query_drv_var(_module_hci_intfs_c_:20): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_hci_ops_c_, _drv_emerg_, ("21: mp_query_drv_var(_module_hci_ops_c_:21): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_osdep_service_c_, _drv_emerg_, ("22: mp_query_drv_var(_module_osdep_service_c_:22): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, ("23: mp_query_drv_var(_module_rtl871x_mp_ioctl_c_:23): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			RT_TRACE(_module_hci_ops_os_c_, _drv_emerg_, ("24: mp_query_drv_var(_module_hci_ops_os_c_:24): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));
			var=(u32)(GlobalDebugComponents);
			//GlobalDebugComponents=padapter->registrypriv.dbg_component;
			RT_TRACE(0xffffffff, _drv_emerg_, (" ==mp_query_drv_var(_module_rtl871x_mp_ioctl_c_): offset(%d;0x%x):before set dbg conpoment=0x%x(l) 0x%x(h)\n",offset,offset,GlobalDebugComponents));

		}
	}
	else{
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_, ("\n mp_query_drv_var: offset(%d) >110\n",offset));
	}
#endif
#endif

	return var;
}

NDIS_STATUS oid_rt_pro_query_dr_variable_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL oldirql;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;

	DR_VARIABLE_STRUCT *pdrv_var;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = sizeof(DR_VARIABLE_STRUCT);
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+Query Information, OID_RT_PRO_QUERY_DR_VARIABLE\n"));

	pdrv_var = (struct _DR_VARIABLE_STRUCT_ *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	pdrv_var->variable = mp_query_drv_var(Adapter, pdrv_var->offset, pdrv_var->variable);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("-oid_rt_pro_query_dr_variable_hdl: offset=0x%x valule=0x%x\n",
		  pdrv_var->offset, pdrv_var->variable));

	return status;
}

//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("oid_rt_pro_rx_packet_type_hdl...................\n"));
#if 0

	if (poid_par_priv->information_buf_len < sizeof (UCHAR)) {
		status = NDIS_STATUS_INVALID_LENGTH;
		*poid_par_priv->bytes_needed = sizeof(UCHAR);
		return status;
	}

	if (poid_par_priv->type_of_oid == SET_OID) {
		Adapter->mppriv.rx_with_status = *(UCHAR *) poid_par_priv->information_buf;
		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_, ("Query Information, OID_RT_PRO_RX_PACKET_TYPE:%d \n",\
												Adapter->mppriv.rx_with_status));

		//*(u32 *)&Adapter->eeprompriv.mac_addr[0]=read32(Adapter, 0x10250050);
		//*(u16 *)&Adapter->eeprompriv.mac_addr[4]=read16(Adapter, 0x10250054);
		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_,("MAC addr=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x  \n",
			Adapter->eeprompriv.mac_addr[0],Adapter->eeprompriv.mac_addr[1],Adapter->eeprompriv.mac_addr[2],\
			Adapter->eeprompriv.mac_addr[3],Adapter->eeprompriv.mac_addr[4],Adapter->eeprompriv.mac_addr[5]));

	}
	else {
		*(UCHAR *) poid_par_priv->information_buf = Adapter->mppriv.rx_with_status;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_, ("Query Information, OID_RT_PRO_RX_PACKET_TYPE:%d \n", \
												Adapter->mppriv.rx_with_status));

		//*(u32 *)&Adapter->eeprompriv.mac_addr[0]=read32(Adapter, 0x10250050);
		//*(u16 *)&Adapter->eeprompriv.mac_addr[4]=read16(Adapter, 0x10250054);
		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_,("MAC addr=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x  \n",
			Adapter->eeprompriv.mac_addr[0],Adapter->eeprompriv.mac_addr[1],Adapter->eeprompriv.mac_addr[2],\
			Adapter->eeprompriv.mac_addr[3],Adapter->eeprompriv.mac_addr[4],Adapter->eeprompriv.mac_addr[5]));
	}
#endif

	return NDIS_STATUS_SUCCESS;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL oldirql;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;

	PEFUSE_ACCESS_STRUCT pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0;

#if 0
	u16 i;
#endif
_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(EFUSE_ACCESS_STRUCT))
		return NDIS_STATUS_INVALID_LENGTH;

	pefuse = (PEFUSE_ACCESS_STRUCT)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;

	_memset(data, 0xFF, cnts);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		("+oid_rt_pro_read_efuse_hd: buf_len=%ld addr=0x%04x cnts=%d\n",
		 poid_par_priv->information_buf_len, addr, cnts));

	if ((addr>511) || (cnts<1) || (cnts>512) || (addr+cnts)>EFUSE_MAX_SIZE) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("!oid_rt_pro_read_efuse_hdl: parameter error\n"));
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (efuse_access(Adapter, _TRUE, addr, cnts, data) == _FALSE)
		status = NDIS_STATUS_FAILURE;
	_irqlevel_changed_(&oldirql, RAISE);

#if 0
	for (i = 0; i < cnts; i++) {
		*((UCHAR *)poid_par_priv->information_buf+3+i) = tmpdata[i];
//		RT_TRACE(_module_rtl871x_mp_ioctl_c_,_drv_err_,("====> Read Efuse returen [ addr =0x%x, value=0x%x ] =====\n", start_addr+i,tmpdata[i]));
	}
#endif
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL oldirql;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;

	PEFUSE_ACCESS_STRUCT pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pefuse = (PEFUSE_ACCESS_STRUCT)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("+oid_rt_pro_write_efuse_hdl: buf_len=%ld addr=0x%04x cnts=%d\n",
		  poid_par_priv->information_buf_len, addr, cnts));

	if ((addr>511) || (cnts<1) || (cnts>512) || (addr+cnts)>efuse_get_max_size(Adapter)) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("!oid_rt_pro_write_efuse_hdl: parameter error"));
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (efuse_access(Adapter, _FALSE, addr, cnts, data) == _FALSE)
		status = NDIS_STATUS_FAILURE;
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_rw_efuse_pgpkt_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PPGPKT_STRUCT	ppgpkt;
	u8		tmpidx;

_func_enter_;

//	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+oid_rt_pro_rw_efuse_pgpkt_hdl\n"));

	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < sizeof(PGPKT_STRUCT))
		return NDIS_STATUS_INVALID_LENGTH;

	ppgpkt = (PPGPKT_STRUCT)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID)
	{
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
			("oid_rt_pro_rw_efuse_pgpkt_hdl: Read offset=0x%x\n",\
			ppgpkt->offset));

		if (efuse_pg_packet_read(Adapter, ppgpkt->offset, ppgpkt->data) == _TRUE)
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		else
			status = NDIS_STATUS_FAILURE;
	} else {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
			("oid_rt_pro_rw_efuse_pgpkt_hdl: Write offset=0x%x word_en=0x%x\n",\
			ppgpkt->offset, ppgpkt->word_en));

		if (efuse_reg_init(Adapter) == _TRUE) {
			if (efuse_pg_packet_write(Adapter, ppgpkt->offset, ppgpkt->word_en, ppgpkt->data) == _TRUE)
				*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
			else
				status = NDIS_STATUS_FAILURE;
			efuse_reg_uninit(Adapter);
		} else
			status = NDIS_STATUS_FAILURE;
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("-oid_rt_pro_rw_efuse_pgpkt_hdl: status=%d\n", status));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_efuse_current_size_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len <sizeof(int))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	efuse_reg_init(Adapter);
	*(int*)poid_par_priv->information_buf = efuse_get_current_size(Adapter);
	efuse_reg_uninit(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	*(int*)poid_par_priv->information_buf = efuse_get_max_size(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("-oid_rt_get_efuse_max_size_hdl: size=%d status=%d\n",
		  *(int*)poid_par_priv->information_buf, status));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("+oid_rt_pro_efuse_hdl\n"));
	if (poid_par_priv->type_of_oid == QUERY_OID)
		status = oid_rt_pro_read_efuse_hdl(poid_par_priv);
	else
		status = oid_rt_pro_write_efuse_hdl(poid_par_priv);
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-oid_rt_pro_efuse_hdl: status=%d\n", status));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u8		*data;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_pro_efuse_map_hdl\n"));

	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < EFUSE_MAP_MAX_SIZE)
		return NDIS_STATUS_INVALID_LENGTH;

	data = (u8*)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID)
	{
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
			("oid_rt_pro_efuse_map_hdl: READ\n"));

		if (efuse_map_read(Adapter, 0, EFUSE_MAP_MAX_SIZE, data) == _TRUE)
			*poid_par_priv->bytes_rw = EFUSE_MAP_MAX_SIZE;
		else {
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_,
				("oid_rt_pro_efuse_map_hdl: READ fail\n"));
			status = NDIS_STATUS_FAILURE;
		}
	} else {
		// SET_OID
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
			("oid_rt_pro_efuse_map_hdl: WRITE\n"));

		if (efuse_reg_init(Adapter) == _TRUE) {
			if (efuse_map_write(Adapter, 0, EFUSE_MAP_MAX_SIZE, data) == _TRUE)
				*poid_par_priv->bytes_rw = EFUSE_MAP_MAX_SIZE;
			else {
				RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_,
					("oid_rt_pro_efuse_map_hdl: WRITE fail\n"));
				status = NDIS_STATUS_FAILURE;
			}
			efuse_reg_uninit(Adapter);
		} else {
			RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_emerg_,
				("oid_rt_pro_efuse_map_hdl: WRITE enable clock fail!\n"));
			status = NDIS_STATUS_FAILURE;
		}
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("-oid_rt_pro_efuse_map_hdl: status=%d\n", status));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		bandwidth;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_set_bandwidth_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	bandwidth = *((u32*)poid_par_priv->information_buf);//4
	if (bandwidth != HT_CHANNEL_WIDTH_20)
		bandwidth = HT_CHANNEL_WIDTH_40;

	Adapter->mppriv.curr_bandwidth = (u8)bandwidth;

	_irqlevel_changed_(&oldirql, LOWER);
	SwitchBandwidth(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("-oid_rt_set_bandwidth_hdl: bandwidth=%d\n", Adapter->mppriv.curr_bandwidth));

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_set_crystal_cap_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		crystal_cap = 0;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len <sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	crystal_cap = *((u32*)poid_par_priv->information_buf);//4
	if (crystal_cap > 0xf)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.curr_crystalcap = crystal_cap;

	_irqlevel_changed_(&oldirql,LOWER);
	SetCrystalCap(Adapter);
	_irqlevel_changed_(&oldirql,RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS  oid_rt_set_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	_irqL		oldirql;
	u8		rx_pkt_type;
	u32		rcr_val32;

_func_enter_;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("+oid_rt_set_rx_packet_type_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len < sizeof(u8)) {
		status = NDIS_STATUS_INVALID_LENGTH;
		return status;
	}
	rx_pkt_type = *((u8*)poid_par_priv->information_buf);//4

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("rx_pkt_type: %x\n",rx_pkt_type ));

	_irqlevel_changed_(&oldirql, LOWER);
#if 0
	rcr_val8 = read8(Adapter, 0x10250048);//RCR
	rcr_val8 &= ~(RCR_AB|RCR_AM|RCR_APM|RCR_AAP);

	if(rx_pkt_type == RX_PKT_BROADCAST){
		rcr_val8 |= (RCR_AB | RCR_ACRC32 );
	}
	else if(rx_pkt_type == RX_PKT_DEST_ADDR){
		rcr_val8 |= (RCR_AAP| RCR_AM |RCR_ACRC32);
	}
	else if(rx_pkt_type == RX_PKT_PHY_MATCH){
		rcr_val8 |= (RCR_APM|RCR_ACRC32);
	}
	else{
		rcr_val8 &= ~(RCR_AAP|RCR_APM|RCR_AM|RCR_AB|RCR_ACRC32);
	}
	write8(Adapter, 0x10250048,rcr_val8);
#else
	rcr_val32 = read32(Adapter, RCR);//RCR = 0x10250048
	rcr_val32 &= ~(RCR_CBSSID|RCR_AB|RCR_AM|RCR_APM|RCR_AAP);
#if 0
	if(rx_pkt_type == RX_PKT_BROADCAST){
		rcr_val32 |= (RCR_AB|RCR_AM|RCR_APM|RCR_AAP|RCR_ACRC32);
	}
	else if(rx_pkt_type == RX_PKT_DEST_ADDR){
		//rcr_val32 |= (RCR_CBSSID|RCR_AAP|RCR_AM|RCR_ACRC32);
		rcr_val32 |= (RCR_CBSSID|RCR_APM|RCR_ACRC32);
	}
	else if(rx_pkt_type == RX_PKT_PHY_MATCH){
		rcr_val32 |= (RCR_APM|RCR_ACRC32);
		//rcr_val32 |= (RCR_AAP|RCR_ACRC32);
	}
	else{
		rcr_val32 &= ~(RCR_AAP|RCR_APM|RCR_AM|RCR_AB|RCR_ACRC32);
	}
#else
	switch (rx_pkt_type)
	{
		case RX_PKT_BROADCAST :
			rcr_val32 |= (RCR_AB|RCR_AM|RCR_APM|RCR_AAP|RCR_ACRC32);
			break;
		case RX_PKT_DEST_ADDR :
			rcr_val32 |= (RCR_AB|RCR_AM|RCR_APM|RCR_AAP|RCR_ACRC32);
			break;
		case RX_PKT_PHY_MATCH:
			rcr_val32 |= (RCR_APM|RCR_ACRC32);
			break;
		default:
			rcr_val32 &= ~(RCR_AAP|RCR_APM|RCR_AM|RCR_AB|RCR_ACRC32);
			break;
	}

	if (rx_pkt_type == RX_PKT_DEST_ADDR) {
		Adapter->mppriv.check_mp_pkt = 1;
	} else {
		Adapter->mppriv.check_mp_pkt = 0;
	}
#endif
	write32(Adapter, RCR, rcr_val32);

#endif
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_tx_agc_offset_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	_irqL		oldirql;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32 		txagc;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	txagc = *(u32*)poid_par_priv->information_buf;
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("oid_rt_pro_set_tx_agc_offset_hdl: 0x%08x\n", txagc));

	_irqlevel_changed_(&oldirql, LOWER);
	SetTxAGCOffset(Adapter, txagc);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

NDIS_STATUS oid_rt_pro_set_pkt_test_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER		Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;

	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct mp_priv		*pmppriv = &Adapter->mppriv;
	u32 			type;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len <sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	type = *(u32*)poid_par_priv->information_buf;

	if (_LOOPBOOK_MODE_ == type) {
		pmppriv->mode = type;
		set_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE); //append txdesc
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("test mode change to loopback mode:0x%08x.\n", pmlmepriv->fw_state));
	} else if (_2MAC_MODE_ == type){
		pmppriv->mode = type;
		_clr_fwstate_(pmlmepriv, WIFI_MP_LPBK_STATE);
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_, ("test mode change to 2mac mode:0x%08x.\n", pmlmepriv->fw_state));
	} else
		status = NDIS_STATUS_NOT_ACCEPTED;

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
//Linux
struct mp_xmit_frame *alloc_mp_xmitframe(struct mp_priv *pmp_priv)
{
	_queue *pfree_xmit_queue = &pmp_priv->free_mp_xmitqueue;

	_irqL irqL;

	struct mp_xmit_frame *pxframe=  NULL;
	_list *plist, *phead;

_func_enter_;

	_enter_critical(&pfree_xmit_queue->lock, &irqL);

	if (_queue_empty(pfree_xmit_queue) == _TRUE) {
		//DEBUG_ERR(("free_mp_xmitframe_cnt:%d\n", pmp_priv->free_mp_xmitframe_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);
		plist = get_next(phead);

		pxframe = LIST_CONTAINOR(plist, struct mp_xmit_frame, list);

		list_delete(&(pxframe->list));
	}

	if (pxframe !=  NULL)
		pmp_priv->free_mp_xmitframe_cnt--;

	_exit_critical(&pfree_xmit_queue->lock, &irqL);

_func_exit_;

	return pxframe;
}

int free_mp_xmitframe(struct xmit_priv *pxmitpriv, struct mp_xmit_frame *pmp_xmitframe)
{
	_irqL irqL;
	_adapter *padapter = pxmitpriv->adapter;
	 struct mp_priv *pmp_priv = &padapter->mppriv;
	_queue *pfree_xmit_queue = &pmp_priv->free_mp_xmitqueue;

	if (pmp_xmitframe == NULL)
		goto exit;

	if (pmp_xmitframe->frame_tag != MP_FRAMETAG)
		goto exit;

	list_delete(&pmp_xmitframe->list);

	_enter_critical(&pfree_xmit_queue->lock, &irqL);

	list_insert_tail(&(pmp_xmitframe->list), get_list_head(pfree_xmit_queue));

	pmp_priv->free_mp_xmitframe_cnt++;

	_exit_critical(&pfree_xmit_queue->lock, &irqL);

exit:

	return _SUCCESS;
}

unsigned int mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	unsigned char *pframe, *pmp_pkt;
	struct ethhdr *pethhdr;
	struct pkt_attrib *pattrib;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	int llc_sz, payload_len;
	struct mp_xmit_frame *pxframe=  NULL;
	struct mp_xmit_packet *pmp_xmitpkt = (struct mp_xmit_packet*)param;
	u8 addr3[] = {0x02, 0xE0, 0x4C, 0x87, 0x66, 0x55};

//	printk("+mp_ioctl_xmit_packet_hdl\n");

	pxframe = alloc_mp_xmitframe(&padapter->mppriv);
	if (pxframe == NULL)
	{
		DEBUG_ERR(("Can't alloc pmpframe %d:%s\n", __LINE__, __FILE__));
		return -1;
	}

	//mp_xmit_pkt
	payload_len = pmp_xmitpkt->len - 14;
	pmp_pkt = (unsigned char*)pmp_xmitpkt->mem;
	pethhdr = (struct ethhdr *)pmp_pkt;

	//printk("payload_len=%d, pkt_mem=0x%x\n", pmp_xmitpkt->len, (void*)pmp_xmitpkt->mem);

	//printk("pxframe=0x%x\n", (void*)pxframe);
	//printk("pxframe->mem=0x%x\n", (void*)pxframe->mem);

	//update attribute
	pattrib = &pxframe->attrib;
	memset((u8 *)(pattrib), 0, sizeof (struct pkt_attrib));
	pattrib->pktlen = pmp_xmitpkt->len;
	pattrib->ether_type = ntohs(pethhdr->h_proto);
	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 0;
#ifndef CONFIG_MP_LINUX
	if(IS_MCAST(pethhdr->h_dest))
		pattrib->mac_id = 4;
	else
		pattrib->mac_id = 5;
#else
	pattrib->mac_id = 5;
#endif

	//
	memset(pxframe->mem, 0 , WLANHDR_OFFSET);
	pframe = (u8 *)(pxframe->mem) + WLANHDR_OFFSET;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetFrameSubType(pframe, WIFI_DATA);

	_memcpy(pwlanhdr->addr1, pethhdr->h_dest, ETH_ALEN);
	_memcpy(pwlanhdr->addr2, pethhdr->h_source, ETH_ALEN);

	_memcpy(pwlanhdr->addr3, addr3, ETH_ALEN);

	pwlanhdr->seq_ctl = 0;
	pframe += pattrib->hdrlen;

	llc_sz= rtl8711_put_snap(pframe, pattrib->ether_type);
	pframe += llc_sz;

	_memcpy(pframe, (void*)(pmp_pkt+14),  payload_len);

	pattrib->last_txcmdsz = pattrib->hdrlen + llc_sz + payload_len;

	DEBUG_INFO(("issuing mp_xmit_frame, tx_len=%d, ether_type=0x%x\n", pattrib->last_txcmdsz, pattrib->ether_type));
	xmit_mp_frame(padapter, pxframe);

#endif

	return _SUCCESS;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_set_power_down_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	_irqL		oldirql;
	u8		bpwrup;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("\n ===> Setoid_rt_set_power_down_hdl.\n"));

	_irqlevel_changed_(&oldirql, LOWER);

	bpwrup = *(u8 *)poid_par_priv->information_buf;
	//CALL  the power_down function
#ifdef PLATFORM_LINUX
#ifdef CONFIG_SDIO_HCI
	dev_power_down(Adapter,bpwrup);
#endif
#endif
	_irqlevel_changed_(&oldirql, RAISE);

	//DEBUG_ERR(("\n <=== Query OID_RT_PRO_READ_REGISTER.
	//	Add:0x%08x Width:%d Value:0x%08x\n",RegRWStruct->offset,RegRWStruct->width,RegRWStruct->value));

_func_exit_;

	return status;
}
//------------------------------------------------------------------------------
NDIS_STATUS oid_rt_get_power_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
//	_irqL		oldirql;

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len < sizeof(u32)) {
		status = NDIS_STATUS_INVALID_LENGTH;
		return status;
	}

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
		 ("\n ===> oid_rt_get_power_mode_hdl.\n"));

//	_irqlevel_changed_(&oldirql, LOWER);
	*(int*)poid_par_priv->information_buf = Adapter->registrypriv.low_power ? POWER_LOW : POWER_NORMAL;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
//	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

