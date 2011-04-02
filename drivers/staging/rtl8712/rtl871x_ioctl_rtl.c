/******************************************************************************
 * rtl871x_ioctl_rtl.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define  _RTL871X_IOCTL_RTL_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "wlan_bssdef.h"
#include "wifi.h"
#include "rtl871x_ioctl.h"
#include "rtl871x_ioctl_set.h"
#include "rtl871x_ioctl_rtl.h"
#include "mp_custom_oid.h"
#include "rtl871x_mp.h"
#include "rtl871x_mp_ioctl.h"

uint oid_rt_get_signal_quality_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_small_packet_crc_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >=  sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
				padapter->recvpriv.rx_smallpacket_crcerr;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_middle_packet_crc_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >=  sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
				padapter->recvpriv.rx_middlepacket_crcerr;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_large_packet_crc_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >=  sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
				 padapter->recvpriv.rx_largepacket_crcerr;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_tx_retry_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_rx_retry_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_rx_total_packet_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >=  sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
					 padapter->recvpriv.rx_pkts +
					 padapter->recvpriv.rx_drop;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_tx_beacon_ok_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_tx_beacon_err_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_rx_icv_err_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		*(uint *)poid_par_priv->information_buf =
					 padapter->recvpriv.rx_icv_err;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH ;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_set_encryption_algorithm_hdl(struct oid_par_priv
						*poid_par_priv)
{
	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_preamble_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);
	u32 preamblemode = 0 ;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		if (padapter->registrypriv.preamble == PREAMBLE_LONG)
			preamblemode = 0;
		else if (padapter->registrypriv.preamble == PREAMBLE_AUTO)
			preamblemode = 1;
		else if (padapter->registrypriv.preamble == PREAMBLE_SHORT)
			preamblemode = 2;
		*(u32 *)poid_par_priv->information_buf = preamblemode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_ap_ip_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_channelplan_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);
	struct eeprom_priv *peeprompriv = &padapter->eeprompriv;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	*(u16 *)poid_par_priv->information_buf = peeprompriv->channel_plan;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_set_channelplan_hdl(struct oid_par_priv
				       *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);
	struct eeprom_priv *peeprompriv = &padapter->eeprompriv;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	peeprompriv->channel_plan = *(u16 *)poid_par_priv->information_buf;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_set_preamble_mode_hdl(struct oid_par_priv
					 *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);
	u32 preamblemode = 0;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		preamblemode = *(u32 *)poid_par_priv->information_buf;
		if (preamblemode == 0)
			padapter->registrypriv.preamble = PREAMBLE_LONG;
		else if (preamblemode == 1)
			padapter->registrypriv.preamble = PREAMBLE_AUTO;
		else if (preamblemode == 2)
			padapter->registrypriv.preamble = PREAMBLE_SHORT;
		*(u32 *)poid_par_priv->information_buf = preamblemode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_set_bcn_intvl_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_dedicate_probe_hdl(struct oid_par_priv
				      *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_total_tx_bytes_hdl(struct oid_par_priv
					  *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
						 padapter->xmitpriv.tx_bytes;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_total_rx_bytes_hdl(struct oid_par_priv
					  *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =
					   padapter->recvpriv.rx_bytes;
		*poid_par_priv->bytes_rw = poid_par_priv->
					   information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_current_tx_power_level_hdl(struct oid_par_priv
					      *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_enc_key_mismatch_count_hdl(struct oid_par_priv
						  *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_enc_key_match_count_hdl(struct oid_par_priv
					       *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_channel_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				    (poid_par_priv->adapter_context);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct NDIS_802_11_CONFIGURATION *pnic_Config;
	u32   channelnum;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true))
		pnic_Config = &pmlmepriv->cur_network.network.Configuration;
	else
		pnic_Config = &padapter->registrypriv.dev_network.
			      Configuration;
	channelnum = pnic_Config->DSConfig;
	*(u32 *)poid_par_priv->information_buf = channelnum;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_hardware_radio_off_hdl(struct oid_par_priv
			 *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_key_mismatch_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_supported_wireless_mode_hdl(struct oid_par_priv
					       *poid_par_priv)
{
	u32 ulInfo = 0;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len >= sizeof(u32)) {
		ulInfo |= 0x0100; /* WIRELESS_MODE_B */
		ulInfo |= 0x0200; /* WIRELESS_MODE_G */
		ulInfo |= 0x0400; /* WIRELESS_MODE_A */
		*(u32 *) poid_par_priv->information_buf = ulInfo;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		return NDIS_STATUS_INVALID_LENGTH;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_channel_list_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_scan_in_progress_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}


uint oid_rt_forced_data_rate_hdl(struct oid_par_priv *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_wireless_mode_for_scan_list_hdl(struct oid_par_priv
						   *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_get_bss_wireless_mode_hdl(struct oid_par_priv
					     *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_scan_with_magic_packet_hdl(struct oid_par_priv
					      *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_ap_get_associated_station_list_hdl(struct oid_par_priv
						      *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_ap_switch_into_ap_mode_hdl(struct oid_par_priv*
					      poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_ap_supported_hdl(struct oid_par_priv *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_ap_set_passphrase_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_pro_rf_write_registry_hdl(struct oid_par_priv*
					     poid_par_priv)
{
	uint status = NDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
			(poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID) /* QUERY_OID */
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len ==
	   (sizeof(unsigned long) * 3)) {
		if (!r8712_setrfreg_cmd(Adapter,
			*(unsigned char *)poid_par_priv->information_buf,
			(unsigned long)(*((unsigned long *)
					poid_par_priv->information_buf + 2))))
			status = NDIS_STATUS_NOT_ACCEPTED;
	} else
		status = NDIS_STATUS_INVALID_LENGTH;
	return status;
}

uint oid_rt_pro_rf_read_registry_hdl(struct oid_par_priv *poid_par_priv)
{
	uint status = NDIS_STATUS_SUCCESS;
	struct _adapter *Adapter = (struct _adapter *)
			(poid_par_priv->adapter_context);

	if (poid_par_priv->type_of_oid != SET_OID) /* QUERY_OID */
		return NDIS_STATUS_NOT_ACCEPTED;
	if (poid_par_priv->information_buf_len == (sizeof(unsigned long)*3)) {
		if (Adapter->mppriv.act_in_progress == true)
			status = NDIS_STATUS_NOT_ACCEPTED;
		else {
			/* init workparam */
			Adapter->mppriv.act_in_progress = true;
			Adapter->mppriv.workparam.bcompleted = false;
			Adapter->mppriv.workparam.act_type = MPT_READ_RF;
			Adapter->mppriv.workparam.io_offset = *(unsigned long *)
						poid_par_priv->information_buf;
			Adapter->mppriv.workparam.io_value = 0xcccccccc;

		/* RegOffsetValue	- The offset of RF register to read.
		 * RegDataWidth	- The data width of RF register to read.
		 * RegDataValue	- The value to read.
		 * RegOffsetValue = *((unsigned long *)InformationBuffer);
		 * RegDataWidth = *((unsigned long *)InformationBuffer+1);
		 * RegDataValue =  *((unsigned long *)InformationBuffer+2);
		 */
			if (!r8712_getrfreg_cmd(Adapter,
			    *(unsigned char *)poid_par_priv->information_buf,
			    (unsigned char *)&Adapter->mppriv.workparam.
			    io_value))
				status = NDIS_STATUS_NOT_ACCEPTED;
		}
	} else
		status = NDIS_STATUS_INVALID_LENGTH;
	return status;
}

enum _CONNECT_STATE_ {
	CHECKINGSTATUS,
	ASSOCIATED,
	ADHOCMODE,
	NOTASSOCIATED
};

uint oid_rt_get_connect_state_hdl(struct oid_par_priv *poid_par_priv)
{
	struct _adapter *padapter = (struct _adapter *)
				     (poid_par_priv->adapter_context);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u32 ulInfo;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	/* nStatus==0	CheckingStatus
	 * nStatus==1	Associated
	 * nStatus==2	AdHocMode
	 * nStatus==3	NotAssociated
	 */
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		ulInfo = CHECKINGSTATUS;
	else if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
		ulInfo = ASSOCIATED;
	else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)
		ulInfo = ADHOCMODE;
	else
		ulInfo = NOTASSOCIATED ;
	*(u32 *)poid_par_priv->information_buf = ulInfo;
	*poid_par_priv->bytes_rw =  poid_par_priv->information_buf_len;
	return NDIS_STATUS_SUCCESS;
}

uint oid_rt_set_default_key_id_hdl(struct oid_par_priv *poid_par_priv)
{
	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
	return NDIS_STATUS_SUCCESS;
}
