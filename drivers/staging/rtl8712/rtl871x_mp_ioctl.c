// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * rtl871x_mp_ioctl.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#include <linux/rndis.h>
#include "osdep_service.h"
#include "drv_types.h"
#include "mlme_osdep.h"
#include "rtl871x_mp.h"
#include "rtl871x_mp_ioctl.h"

uint oid_null_function(struct oid_par_priv *poid_par_priv)
{
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid == SET_OID) {
		if (poid_par_priv->information_buf_len >= sizeof(u8))
			Adapter->registrypriv.wireless_mode =
					*(u8 *)poid_par_priv->information_buf;
		else
			status = RNDIS_STATUS_INVALID_LENGTH;
	} else if (poid_par_priv->type_of_oid == QUERY_OID) {
		if (poid_par_priv->information_buf_len >= sizeof(u8)) {
			*(u8 *)poid_par_priv->information_buf =
					 Adapter->registrypriv.wireless_mode;
			*poid_par_priv->bytes_rw =
					poid_par_priv->information_buf_len;
		} else {
			status = RNDIS_STATUS_INVALID_LENGTH;
		}
	} else {
		status = RNDIS_STATUS_NOT_ACCEPTED;
	}
	return status;
}

uint oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return RNDIS_STATUS_INVALID_LENGTH;
	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);
	offset = (u16)(pbbreg->offset) & 0xFFF; /*0ffset :0x800~0xfff*/
	if (offset < BB_REG_BASE_ADDR)
		offset |= BB_REG_BASE_ADDR;
	value = pbbreg->value;
	r8712_bb_reg_write(Adapter, offset, value);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return RNDIS_STATUS_INVALID_LENGTH;
	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);
	offset = (u16)(pbbreg->offset) & 0xFFF; /*0ffset :0x800~0xfff*/
	if (offset < BB_REG_BASE_ADDR)
		offset |= BB_REG_BASE_ADDR;
	value = r8712_bb_reg_read(Adapter, offset);
	pbbreg->value = value;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return RNDIS_STATUS_INVALID_LENGTH;
	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);
	path = (u8)pbbreg->path;
	if (path > RF_PATH_B)
		return RNDIS_STATUS_NOT_ACCEPTED;
	offset = (u8)pbbreg->offset;
	value = pbbreg->value;
	r8712_rf_reg_write(Adapter, path, offset, value);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return RNDIS_STATUS_INVALID_LENGTH;
	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);
	path = (u8)pbbreg->path;
	if (path > RF_PATH_B) /* 1T2R  path_a /path_b */
		return RNDIS_STATUS_NOT_ACCEPTED;
	offset = (u8)pbbreg->offset;
	value = r8712_rf_reg_read(Adapter, path, offset);
	pbbreg->value = value;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

/*This function initializes the DUT to the MP test mode*/
static int mp_start_test(struct _adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct wlan_bssid_ex bssid;
	struct sta_info *psta;
	unsigned long length;
	unsigned long irqL;
	int res = _SUCCESS;

	/* 3 1. initialize a new struct wlan_bssid_ex */
	memcpy(bssid.MacAddress, pmppriv->network_macaddr, ETH_ALEN);
	bssid.Ssid.SsidLength = 16;
	memcpy(bssid.Ssid.Ssid, (unsigned char *)"mp_pseudo_adhoc",
		bssid.Ssid.SsidLength);
	bssid.InfrastructureMode = Ndis802_11IBSS;
	bssid.NetworkTypeInUse = Ndis802_11DS;
	bssid.IELength = 0;
	length = r8712_get_wlan_bssid_ex_sz(&bssid);
	if (length % 4) {
		/*round up to multiple of 4 bytes.*/
		bssid.Length = ((length >> 2) + 1) << 2;
	} else {
		bssid.Length = length;
	}
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE))
		goto end_of_mp_start_test;
	/*init mp_start_test status*/
	pmppriv->prev_fw_state = get_fwstate(pmlmepriv);
	pmlmepriv->fw_state = WIFI_MP_STATE;
	if (pmppriv->mode == _LOOPBOOK_MODE_)
		set_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE); /*append txdesc*/
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	/* 3 2. create a new psta for mp driver */
	/* clear psta in the cur_network, if any */
	psta = r8712_get_stainfo(&padapter->stapriv,
				 tgt_network->network.MacAddress);
	if (psta)
		r8712_free_stainfo(padapter, psta);
	psta = r8712_alloc_stainfo(&padapter->stapriv, bssid.MacAddress);
	if (psta == NULL) {
		res = _FAIL;
		goto end_of_mp_start_test;
	}
	/* 3 3. join pseudo AdHoc */
	tgt_network->join_res = 1;
	tgt_network->aid = psta->aid = 1;
	memcpy(&tgt_network->network, &bssid, length);
	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	r8712_os_indicate_connect(padapter);
	/* Set to LINKED STATE for MP TRX Testing */
	set_fwstate(pmlmepriv, _FW_LINKED);
end_of_mp_start_test:
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	return res;
}

/*This function change the DUT from the MP test mode into normal mode */
static int mp_stop_test(struct _adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info *psta;
	unsigned long irqL;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (!check_fwstate(pmlmepriv, WIFI_MP_STATE))
		goto end_of_mp_stop_test;
	/* 3 1. disconnect pseudo AdHoc */
	r8712_os_indicate_disconnect(padapter);
	/* 3 2. clear psta used in mp test mode. */
	psta = r8712_get_stainfo(&padapter->stapriv,
				 tgt_network->network.MacAddress);
	if (psta)
		r8712_free_stainfo(padapter, psta);
	/* 3 3. return to normal state (default:station mode) */
	pmlmepriv->fw_state = pmppriv->prev_fw_state; /* WIFI_STATION_STATE;*/
	/*flush the cur_network*/
	memset(tgt_network, 0, sizeof(struct wlan_network));
end_of_mp_stop_test:
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	return _SUCCESS;
}

int mp_start_joinbss(struct _adapter *padapter, struct ndis_802_11_ssid *pssid)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	unsigned char res = _SUCCESS;

	if (!check_fwstate(pmlmepriv, WIFI_MP_STATE))
		return _FAIL;
	if (!check_fwstate(pmlmepriv, _FW_LINKED))
		return _FAIL;
	_clr_fwstate_(pmlmepriv, _FW_LINKED);
	res = r8712_setassocsta_cmd(padapter, pmppriv->network_macaddr);
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	return res;
}

uint oid_rt_pro_set_data_rate_hdl(struct oid_par_priv
					 *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32 ratevalue;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	ratevalue = *((u32 *)poid_par_priv->information_buf);
	if (ratevalue >= MPT_RATE_LAST)
		return RNDIS_STATUS_INVALID_DATA;
	Adapter->mppriv.curr_rateidx = ratevalue;
	r8712_SetDataRate(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	uint status = RNDIS_STATUS_SUCCESS;
	u32 mode;
	u8 val8;

	if (poid_par_priv->type_of_oid != SET_OID)
		return  RNDIS_STATUS_NOT_ACCEPTED;
	mode = *((u32 *)poid_par_priv->information_buf);
	Adapter->mppriv.mode = mode;/* 1 for loopback*/
	if (mp_start_test(Adapter) == _FAIL)
		status = RNDIS_STATUS_NOT_ACCEPTED;
	r8712_write8(Adapter, MSR, 1); /* Link in ad hoc network, 0x1025004C */
	r8712_write8(Adapter, RCR, 0); /* RCR : disable all pkt, 0x10250048 */
	/* RCR disable Check BSSID, 0x1025004a */
	r8712_write8(Adapter, RCR + 2, 0x57);
	/* disable RX filter map , mgt frames will put in RX FIFO 0 */
	r8712_write16(Adapter, RXFLTMAP0, 0x0);
	val8 = r8712_read8(Adapter, EE_9346CR);
	if (!(val8 & _9356SEL)) { /*boot from EFUSE*/
		r8712_efuse_reg_init(Adapter);
		r8712_efuse_change_max_size(Adapter);
		r8712_efuse_reg_uninit(Adapter);
	}
	return status;
}

uint oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	uint status = RNDIS_STATUS_SUCCESS;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (mp_stop_test(Adapter) == _FAIL)
		status = RNDIS_STATUS_NOT_ACCEPTED;
	return status;
}

uint oid_rt_pro_set_channel_direct_call_hdl(struct oid_par_priv
						   *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		Channel;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	Channel = *((u32 *)poid_par_priv->information_buf);
	if (Channel > 14)
		return RNDIS_STATUS_NOT_ACCEPTED;
	Adapter->mppriv.curr_ch = Channel;
	r8712_SetChannel(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_antenna_bb_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32 antenna;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	antenna = *((u32 *)poid_par_priv->information_buf);
	Adapter->mppriv.antenna_tx = (u16)((antenna & 0xFFFF0000) >> 16);
	Adapter->mppriv.antenna_rx = (u16)(antenna & 0x0000FFFF);
	r8712_SwitchAntenna(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_tx_power_control_hdl(
					struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32 tx_pwr_idx;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	tx_pwr_idx = *((u32 *)poid_par_priv->information_buf);
	if (tx_pwr_idx > MAX_TX_PWR_INDEX_N_MODE)
		return RNDIS_STATUS_NOT_ACCEPTED;
	Adapter->mppriv.curr_txpoweridx = (u8)tx_pwr_idx;
	r8712_SetTxPower(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_query_tx_packet_sent_hdl(
					struct oid_par_priv *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = RNDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
					Adapter->mppriv.tx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = RNDIS_STATUS_INVALID_LENGTH;
	}
	return status;
}

uint oid_rt_pro_query_rx_packet_received_hdl(
					struct oid_par_priv *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = RNDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
					Adapter->mppriv.rx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = RNDIS_STATUS_INVALID_LENGTH;
	}
	return status;
}

uint oid_rt_pro_query_rx_packet_crc32_error_hdl(
					struct oid_par_priv *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = RNDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
					Adapter->mppriv.rx_crcerrpktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = RNDIS_STATUS_INVALID_LENGTH;
	}
	return status;
}

uint oid_rt_pro_reset_tx_packet_sent_hdl(struct oid_par_priv
						*poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	Adapter->mppriv.tx_pktcount = 0;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_reset_rx_packet_received_hdl(struct oid_par_priv
						    *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		Adapter->mppriv.rx_pktcount = 0;
		Adapter->mppriv.rx_crcerrpktcount = 0;
	} else {
		status = RNDIS_STATUS_INVALID_LENGTH;
	}
	return status;
}

uint oid_rt_reset_phy_rx_packet_count_hdl(struct oid_par_priv
						 *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	r8712_ResetPhyRxPktCount(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_get_phy_rx_packet_received_hdl(struct oid_par_priv
						  *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	*(u32 *)poid_par_priv->information_buf =
					 r8712_GetPhyRxPktReceived(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_get_phy_rx_packet_crc32_error_hdl(struct oid_par_priv
						     *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len != sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	*(u32 *)poid_par_priv->information_buf =
					 r8712_GetPhyRxPktCRC32Error(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_modulation_hdl(struct oid_par_priv
					  *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.curr_modem = *((u8 *)poid_par_priv->information_buf);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_continuous_tx_hdl(struct oid_par_priv
					     *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		bStartTest;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	bStartTest = *((u32 *)poid_par_priv->information_buf);
	r8712_SetContinuousTx(Adapter, (u8)bStartTest);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_single_carrier_tx_hdl(struct oid_par_priv
						 *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		bStartTest;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	bStartTest = *((u32 *)poid_par_priv->information_buf);
	r8712_SetSingleCarrierTx(Adapter, (u8)bStartTest);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_carrier_suppression_tx_hdl(struct oid_par_priv
						      *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		bStartTest;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	bStartTest = *((u32 *)poid_par_priv->information_buf);
	r8712_SetCarrierSuppressionTx(Adapter, (u8)bStartTest);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_set_single_tone_tx_hdl(struct oid_par_priv
					      *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		bStartTest;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	bStartTest = *((u32 *)poid_par_priv->information_buf);
	r8712_SetSingleToneTx(Adapter, (u8)bStartTest);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_read_register_hdl(struct oid_par_priv
					 *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	uint status = RNDIS_STATUS_SUCCESS;
	struct mp_rw_reg *RegRWStruct;
	u16		offset;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	RegRWStruct = (struct mp_rw_reg *)poid_par_priv->information_buf;
	if ((RegRWStruct->offset >= 0x10250800) &&
	    (RegRWStruct->offset <= 0x10250FFF)) {
		/*baseband register*/
		/*0ffset :0x800~0xfff*/
		offset = (u16)(RegRWStruct->offset) & 0xFFF;
		RegRWStruct->value = r8712_bb_reg_read(Adapter, offset);
	} else {
		switch (RegRWStruct->width) {
		case 1:
			RegRWStruct->value = r8712_read8(Adapter,
						   RegRWStruct->offset);
			break;
		case 2:
			RegRWStruct->value = r8712_read16(Adapter,
						    RegRWStruct->offset);
			break;
		case 4:
			RegRWStruct->value = r8712_read32(Adapter,
						    RegRWStruct->offset);
			break;
		default:
			status = RNDIS_STATUS_NOT_ACCEPTED;
			break;
		}
	}
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return status;
}

uint oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	uint status = RNDIS_STATUS_SUCCESS;
	struct mp_rw_reg *RegRWStruct;
	u16		offset;
	u32		value;
	u32 oldValue = 0;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	RegRWStruct = (struct mp_rw_reg *)poid_par_priv->information_buf;
	if ((RegRWStruct->offset >= 0x10250800) &&
	    (RegRWStruct->offset <= 0x10250FFF)) {
		/*baseband register*/
		offset = (u16)(RegRWStruct->offset) & 0xFFF;
		value = RegRWStruct->value;
		switch (RegRWStruct->width) {
		case 1:
			oldValue = r8712_bb_reg_read(Adapter, offset);
			oldValue &= 0xFFFFFF00;
			value &= 0x000000FF;
			value |= oldValue;
			break;
		case 2:
			oldValue = r8712_bb_reg_read(Adapter, offset);
			oldValue &= 0xFFFF0000;
			value &= 0x0000FFFF;
			value |= oldValue;
			break;
		}
		r8712_bb_reg_write(Adapter, offset, value);
	} else {
		switch (RegRWStruct->width) {
		case 1:
			r8712_write8(Adapter, RegRWStruct->offset,
			       (unsigned char)RegRWStruct->value);
			break;
		case 2:
			r8712_write16(Adapter, RegRWStruct->offset,
				(unsigned short)RegRWStruct->value);
			break;
		case 4:
			r8712_write32(Adapter, RegRWStruct->offset,
				(unsigned int)RegRWStruct->value);
			break;
		default:
			status = RNDIS_STATUS_NOT_ACCEPTED;
			break;
		}
	}
	return status;
}

uint oid_rt_get_thermal_meter_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;

	if (Adapter->mppriv.act_in_progress)
		return RNDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return RNDIS_STATUS_INVALID_LENGTH;
	/*init workparam*/
	Adapter->mppriv.act_in_progress = true;
	Adapter->mppriv.workparam.bcompleted = false;
	Adapter->mppriv.workparam.act_type = MPT_GET_THERMAL_METER;
	Adapter->mppriv.workparam.io_offset = 0;
	Adapter->mppriv.workparam.io_value = 0xFFFFFFFF;
	r8712_GetThermalMeter(Adapter, &Adapter->mppriv.workparam.io_value);
	Adapter->mppriv.workparam.bcompleted = true;
	Adapter->mppriv.act_in_progress = false;
	*(u32 *)poid_par_priv->information_buf =
				 Adapter->mppriv.workparam.io_value;
	*poid_par_priv->bytes_rw = sizeof(u32);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	uint status = RNDIS_STATUS_SUCCESS;

	struct EFUSE_ACCESS_STRUCT *pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len <
	    sizeof(struct EFUSE_ACCESS_STRUCT))
		return RNDIS_STATUS_INVALID_LENGTH;
	pefuse = (struct EFUSE_ACCESS_STRUCT *)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;
	memset(data, 0xFF, cnts);
	if ((addr > 511) || (cnts < 1) || (cnts > 512) || (addr + cnts) >
	     EFUSE_MAX_SIZE)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (!r8712_efuse_access(Adapter, true, addr, cnts, data))
		status = RNDIS_STATUS_FAILURE;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return status;
}
/*------------------------------------------------------------------------*/
uint oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	uint status = RNDIS_STATUS_SUCCESS;

	struct EFUSE_ACCESS_STRUCT *pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;

	pefuse = (struct EFUSE_ACCESS_STRUCT *)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;

	if ((addr > 511) || (cnts < 1) || (cnts > 512) ||
	    (addr + cnts) > r8712_efuse_get_max_size(Adapter))
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (!r8712_efuse_access(Adapter, false, addr, cnts, data))
		status = RNDIS_STATUS_FAILURE;
	return status;
}
/*----------------------------------------------------------------------*/

uint oid_rt_get_efuse_current_size_hdl(struct oid_par_priv
					      *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(int))
		return RNDIS_STATUS_INVALID_LENGTH;
	r8712_efuse_reg_init(Adapter);
	*(int *)poid_par_priv->information_buf =
				 r8712_efuse_get_current_size(Adapter);
	r8712_efuse_reg_uninit(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	*(int *)poid_par_priv->information_buf =
					 r8712_efuse_get_max_size(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	uint status = RNDIS_STATUS_SUCCESS;

	if (poid_par_priv->type_of_oid == QUERY_OID)
		status = oid_rt_pro_read_efuse_hdl(poid_par_priv);
	else
		status = oid_rt_pro_write_efuse_hdl(poid_par_priv);
	return status;
}

uint oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	uint status = RNDIS_STATUS_SUCCESS;
	u8		*data;

	*poid_par_priv->bytes_rw = 0;
	if (poid_par_priv->information_buf_len < EFUSE_MAP_MAX_SIZE)
		return RNDIS_STATUS_INVALID_LENGTH;
	data = (u8 *)poid_par_priv->information_buf;
	if (poid_par_priv->type_of_oid == QUERY_OID) {
		if (r8712_efuse_map_read(Adapter, 0, EFUSE_MAP_MAX_SIZE, data))
			*poid_par_priv->bytes_rw = EFUSE_MAP_MAX_SIZE;
		else
			status = RNDIS_STATUS_FAILURE;
	} else {
		/* SET_OID */
		if (r8712_efuse_reg_init(Adapter)) {
			if (r8712_efuse_map_write(Adapter, 0,
			    EFUSE_MAP_MAX_SIZE, data))
				*poid_par_priv->bytes_rw = EFUSE_MAP_MAX_SIZE;
			else
				status = RNDIS_STATUS_FAILURE;
			r8712_efuse_reg_uninit(Adapter);
		} else {
			status = RNDIS_STATUS_FAILURE;
		}
	}
	return status;
}

uint oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u32		bandwidth;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	bandwidth = *((u32 *)poid_par_priv->information_buf);/*4*/
	if (bandwidth != HT_CHANNEL_WIDTH_20)
		bandwidth = HT_CHANNEL_WIDTH_40;
	Adapter->mppriv.curr_bandwidth = (u8)bandwidth;
	r8712_SwitchBandwidth(Adapter);
	return RNDIS_STATUS_SUCCESS;
}

uint oid_rt_set_rx_packet_type_hdl(struct oid_par_priv
					   *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);
	u8		rx_pkt_type;
	u32		rcr_val32;

	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(u8))
		return RNDIS_STATUS_INVALID_LENGTH;
	rx_pkt_type = *((u8 *)poid_par_priv->information_buf);/*4*/
	rcr_val32 = r8712_read32(Adapter, RCR);/*RCR = 0x10250048*/
	rcr_val32 &= ~(RCR_CBSSID | RCR_AB | RCR_AM | RCR_APM | RCR_AAP);
	switch (rx_pkt_type) {
	case RX_PKT_BROADCAST:
		rcr_val32 |= (RCR_AB | RCR_AM | RCR_APM | RCR_AAP | RCR_ACRC32);
		break;
	case RX_PKT_DEST_ADDR:
		rcr_val32 |= (RCR_AB | RCR_AM | RCR_APM | RCR_AAP | RCR_ACRC32);
		break;
	case RX_PKT_PHY_MATCH:
		rcr_val32 |= (RCR_APM | RCR_ACRC32);
		break;
	default:
		rcr_val32 &= ~(RCR_AAP |
			       RCR_APM |
			       RCR_AM |
			       RCR_AB |
			       RCR_ACRC32);
		break;
	}
	if (rx_pkt_type == RX_PKT_DEST_ADDR)
		Adapter->mppriv.check_mp_pkt = 1;
	else
		Adapter->mppriv.check_mp_pkt = 0;
	r8712_write32(Adapter, RCR, rcr_val32);
	return RNDIS_STATUS_SUCCESS;
}

/*--------------------------------------------------------------------------*/
/*Linux*/
unsigned int mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv)
{
	return _SUCCESS;
}
/*-------------------------------------------------------------------------*/
uint oid_rt_set_power_down_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != SET_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	/*CALL  the power_down function*/
	return RNDIS_STATUS_SUCCESS;
}

/*-------------------------------------------------------------------------- */
uint oid_rt_get_power_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *Adapter = (struct _adapter *)
				   (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return RNDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len < sizeof(u32))
		return RNDIS_STATUS_INVALID_LENGTH;
	*(int *)poid_par_priv->information_buf =
		 Adapter->registrypriv.low_power ? POWER_LOW : POWER_NORMAL;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return RNDIS_STATUS_SUCCESS;
}
