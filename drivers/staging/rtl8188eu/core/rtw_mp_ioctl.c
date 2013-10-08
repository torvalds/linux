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
#define _RTW_MP_IOCTL_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <mlme_osdep.h>

/* include <rtw_mp.h> */
#include <rtw_mp_ioctl.h>


/*   rtl8188eu_oid_rtl_seg_81_85   section start **************** */
int rtl8188eu_oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID) {
		Adapter->registrypriv.wireless_mode = *(u8 *)poid_par_priv->information_buf;
	} else if (poid_par_priv->type_of_oid == QUERY_OID) {
		*(u8 *)poid_par_priv->information_buf = Adapter->registrypriv.wireless_mode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		 RT_TRACE(_module_mp_, _drv_info_, ("-query Wireless Mode=%d\n", Adapter->registrypriv.wireless_mode));
	} else {
		status = NDIS_STATUS_NOT_ACCEPTED;
	}

_func_exit_;

	return status;
}
/*   rtl8188eu_oid_rtl_seg_81_87_80   section start **************** */
int rtl8188eu_oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_write_bb_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);

	offset = (u16)(pbbreg->offset) & 0xFFF; /* 0ffset :0x800~0xfff */
	if (offset < BB_REG_BASE_ADDR)
		offset |= BB_REG_BASE_ADDR;

	value = pbbreg->value;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("rtl8188eu_oid_rt_pro_write_bb_reg_hdl: offset=0x%03X value=0x%08X\n",
		  offset, value));

	_irqlevel_changed_(&oldirql, LOWER);
	write_bbreg(Adapter, offset, 0xFFFFFFFF, value);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_read_bb_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);

	offset = (u16)(pbbreg->offset) & 0xFFF; /* 0ffset :0x800~0xfff */
	if (offset < BB_REG_BASE_ADDR)
		offset |= BB_REG_BASE_ADDR;

	_irqlevel_changed_(&oldirql, LOWER);
	value = read_bbreg(Adapter, offset, 0xFFFFFFFF);
	_irqlevel_changed_(&oldirql, RAISE);

	pbbreg->value = value;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-rtl8188eu_oid_rt_pro_read_bb_reg_hdl: offset=0x%03X value:0x%08X\n",
		  offset, value));
_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_write_rf_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);

	if (pbbreg->path >= MAX_RF_PATH_NUMS)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (pbbreg->offset > 0xFF)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (pbbreg->value > 0xFFFFF)
		return NDIS_STATUS_NOT_ACCEPTED;

	path = (u8)pbbreg->path;
	offset = (u8)pbbreg->offset;
	value = pbbreg->value;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("rtl8188eu_oid_rt_pro_write_rf_reg_hdl: path=%d offset=0x%02X value=0x%05X\n",
		  path, offset, value));

	_irqlevel_changed_(&oldirql, LOWER);
	write_rfreg(Adapter, path, offset, value);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);
	int status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_read_rf_reg_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct rf_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct rf_reg_param *)(poid_par_priv->information_buf);

	if (pbbreg->path >= MAX_RF_PATH_NUMS)
		return NDIS_STATUS_NOT_ACCEPTED;
	if (pbbreg->offset > 0xFF)
		return NDIS_STATUS_NOT_ACCEPTED;

	path = (u8)pbbreg->path;
	offset = (u8)pbbreg->offset;

	_irqlevel_changed_(&oldirql, LOWER);
	value = read_rfreg(Adapter, path, offset);
	_irqlevel_changed_(&oldirql, RAISE);

	pbbreg->value = value;

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-rtl8188eu_oid_rt_pro_read_rf_reg_hdl: path=%d offset=0x%02X value=0x%05X\n",
		  path, offset, value));

_func_exit_;

	return status;
}
/*   rtl8188eu_oid_rtl_seg_81_87_00   section end**************** */
/*  */

/*   rtl8188eu_oid_rtl_seg_81_80_00   section start **************** */
/*  */
int rtl8188eu_oid_rt_pro_set_data_rate_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		ratevalue;/* 4 */
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("+rtl8188eu_oid_rt_pro_set_data_rate_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	ratevalue = *((u32 *)poid_par_priv->information_buf);/* 4 */
	RT_TRACE(_module_mp_, _drv_notice_,
		 ("rtl8188eu_oid_rt_pro_set_data_rate_hdl: data rate idx=%d\n", ratevalue));
	if (ratevalue >= MPT_RATE_LAST)
		return NDIS_STATUS_INVALID_DATA;

	Adapter->mppriv.rateidx = ratevalue;

	_irqlevel_changed_(&oldirql, LOWER);
	SetDataRate(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		mode;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_start_test_hdl\n"));

	if (Adapter->registrypriv.mp_mode == 0)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	/* IQCalibrateBcut(Adapter); */

	mode = *((u32 *)poid_par_priv->information_buf);
	Adapter->mppriv.mode = mode;/*  1 for loopback */

	if (mp_start_test(Adapter) == _FAIL) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		goto exit;
	}

exit:
	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_notice_, ("-rtl8188eu_oid_rt_pro_start_test_hdl: mp_mode=%d\n", Adapter->mppriv.mode));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+Set OID_RT_PRO_STOP_TEST\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);
	mp_stop_test(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_notice_, ("-Set OID_RT_PRO_STOP_TEST\n"));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		Channel;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl\n"));

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		*((u32 *)poid_par_priv->information_buf) = Adapter->mppriv.channel;
		return NDIS_STATUS_SUCCESS;
	}

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	Channel = *((u32 *)poid_par_priv->information_buf);
	RT_TRACE(_module_mp_, _drv_notice_, ("rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl: Channel=%d\n", Channel));
	if (Channel > 14)
		return NDIS_STATUS_NOT_ACCEPTED;
	Adapter->mppriv.channel = Channel;

	_irqlevel_changed_(&oldirql, LOWER);
	SetChannel(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv)
{
	u16		bandwidth;
	u16		channel_offset;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *padapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_,
		 ("+rtl8188eu_oid_rt_set_bandwidth_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	bandwidth = *((u32 *)poid_par_priv->information_buf);/* 4 */
	channel_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (bandwidth != HT_CHANNEL_WIDTH_40)
		bandwidth = HT_CHANNEL_WIDTH_20;
	padapter->mppriv.bandwidth = (u8)bandwidth;
	padapter->mppriv.prime_channel_offset = (u8)channel_offset;

	_irqlevel_changed_(&oldirql, LOWER);
	SetBandwidth(padapter);
	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-rtl8188eu_oid_rt_set_bandwidth_hdl: bandwidth=%d channel_offset=%d\n",
		  bandwidth, channel_offset));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_set_antenna_bb_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		antenna;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_set_antenna_bb_hdl\n"));

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID) {
		antenna = *(u32 *)poid_par_priv->information_buf;

		Adapter->mppriv.antenna_tx = (u16)((antenna & 0xFFFF0000) >> 16);
		Adapter->mppriv.antenna_rx = (u16)(antenna & 0x0000FFFF);
		RT_TRACE(_module_mp_, _drv_notice_,
			 ("rtl8188eu_oid_rt_pro_set_antenna_bb_hdl: tx_ant=0x%04x rx_ant=0x%04x\n",
			  Adapter->mppriv.antenna_tx, Adapter->mppriv.antenna_rx));

		_irqlevel_changed_(&oldirql, LOWER);
		SetAntenna(Adapter);
		_irqlevel_changed_(&oldirql, RAISE);
	} else {
		antenna = (Adapter->mppriv.antenna_tx << 16)|Adapter->mppriv.antenna_rx;
		*(u32 *)poid_par_priv->information_buf = antenna;
	}

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_tx_power_control_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		tx_pwr_idx;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_, ("+rtl8188eu_oid_rt_pro_set_tx_power_control_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	tx_pwr_idx = *((u32 *)poid_par_priv->information_buf);
	if (tx_pwr_idx > MAX_TX_PWR_INDEX_N_MODE)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.txpoweridx = (u8)tx_pwr_idx;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("rtl8188eu_oid_rt_pro_set_tx_power_control_hdl: idx=0x%2x\n",
		  Adapter->mppriv.txpoweridx));

	_irqlevel_changed_(&oldirql, LOWER);
	SetTxPower(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

/*  */
/*   rtl8188eu_oid_rtl_seg_81_80_20   section start **************** */
/*  */
int rtl8188eu_oid_rt_pro_query_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =  Adapter->mppriv.tx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_query_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	RT_TRACE(_module_mp_, _drv_alert_, ("===> rtl8188eu_oid_rt_pro_query_rx_packet_received_hdl.\n"));
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =  Adapter->mppriv.rx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		RT_TRACE(_module_mp_, _drv_alert_, ("recv_ok:%d\n", Adapter->mppriv.rx_pktcount));
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_query_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	RT_TRACE(_module_mp_, _drv_alert_, ("===> rtl8188eu_oid_rt_pro_query_rx_packet_crc32_error_hdl.\n"));
	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		*(u32 *)poid_par_priv->information_buf =  Adapter->mppriv.rx_crcerrpktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		RT_TRACE(_module_mp_, _drv_alert_, ("recv_err:%d\n", Adapter->mppriv.rx_crcerrpktcount));
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
/*  */

int rtl8188eu_oid_rt_pro_reset_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	RT_TRACE(_module_mp_, _drv_alert_, ("===> rtl8188eu_oid_rt_pro_reset_tx_packet_sent_hdl.\n"));
	Adapter->mppriv.tx_pktcount = 0;

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_reset_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(u32)) {
		Adapter->mppriv.rx_pktcount = 0;
		Adapter->mppriv.rx_crcerrpktcount = 0;
	} else {
		status = NDIS_STATUS_INVALID_LENGTH;
	}

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_reset_phy_rx_packet_count_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

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
/*  */
int rtl8188eu_oid_rt_get_phy_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_, ("+rtl8188eu_oid_rt_get_phy_rx_packet_received_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(u32 *)poid_par_priv->information_buf = GetPhyRxPktReceived(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_mp_, _drv_notice_, ("-rtl8188eu_oid_rt_get_phy_rx_packet_received_hdl: recv_ok=%d\n", *(u32 *)poid_par_priv->information_buf));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_get_phy_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_, ("+rtl8188eu_oid_rt_get_phy_rx_packet_crc32_error_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;


	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(u32 *)poid_par_priv->information_buf = GetPhyRxPktCRC32Error(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_mp_, _drv_info_,
		 ("-rtl8188eu_oid_rt_get_phy_rx_packet_crc32_error_hdl: recv_err =%d\n",
		 *(u32 *)poid_par_priv->information_buf));

_func_exit_;

	return status;
}
/*   rtl8188eu_oid_rtl_seg_81_80_20   section end **************** */
int rtl8188eu_oid_rt_pro_set_continuous_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		bStartTest;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_set_continuous_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetContinuousTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			DBG_88E("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_single_carrier_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		bStartTest;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_alert_, ("+rtl8188eu_oid_rt_pro_set_single_carrier_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleCarrierTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			DBG_88E("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_carrier_suppression_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		bStartTest;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_set_carrier_suppression_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetCarrierSuppressionTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			DBG_88E("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_single_tone_tx_hdl(struct oid_par_priv *poid_par_priv)
{
	u32		bStartTest;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_alert_, ("+rtl8188eu_oid_rt_pro_set_single_tone_tx_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleToneTx(Adapter, (u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_modulation_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_trigger_gpio_hdl(struct oid_par_priv *poid_par_priv)
{
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);
	int status = NDIS_STATUS_SUCCESS;
_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);
	rtw_hal_set_hwreg(Adapter, HW_VAR_TRIGGER_GPIO_0, NULL);
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*   rtl8188eu_oid_rtl_seg_81_80_00   section end **************** */
/*  */
int rtl8188eu_oid_rt_pro8711_join_bss_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
	struct mp_rw_reg *RegRWStruct;
	u32		offset, width;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_,
		 ("+rtl8188eu_oid_rt_pro_read_register_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RegRWStruct = (struct mp_rw_reg *)poid_par_priv->information_buf;
	offset = RegRWStruct->offset;
	width = RegRWStruct->width;

	if (offset > 0xFFF)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	switch (width) {
	case 1:
		RegRWStruct->value = rtw_read8(Adapter, offset);
		break;
	case 2:
		RegRWStruct->value = rtw_read16(Adapter, offset);
		break;
	default:
		width = 4;
		RegRWStruct->value = rtw_read32(Adapter, offset);
		break;
	}
	RT_TRACE(_module_mp_, _drv_notice_,
		 ("rtl8188eu_oid_rt_pro_read_register_hdl: offset:0x%04X value:0x%X\n",
		  offset, RegRWStruct->value));

	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = width;

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
	struct mp_rw_reg *RegRWStruct;
	u32		offset, width, value;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *padapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_,
		 ("+rtl8188eu_oid_rt_pro_write_register_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RegRWStruct = (struct mp_rw_reg *)poid_par_priv->information_buf;
	offset = RegRWStruct->offset;
	width = RegRWStruct->width;
	value = RegRWStruct->value;

	if (offset > 0xFFF)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	switch (RegRWStruct->width) {
	case 1:
		if (value > 0xFF) {
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}
		rtw_write8(padapter, offset, (u8)value);
		break;
	case 2:
		if (value > 0xFFFF) {
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}
		rtw_write16(padapter, offset, (u16)value);
		break;
	case 4:
		rtw_write32(padapter, offset, value);
		break;
	default:
		status = NDIS_STATUS_NOT_ACCEPTED;
		break;
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_info_,
		 ("-rtl8188eu_oid_rt_pro_write_register_hdl: offset=0x%08X width=%d value=0x%X\n",
		  offset, width, value));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_burst_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_burst_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_write_txcmd_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

/*  */
int rtl8188eu_oid_rt_pro_read16_eeprom_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

/*  */
int rtl8188eu_oid_rt_pro_write16_eeprom_hdl (struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro8711_wi_poll_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro8711_pkt_loss_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_rd_attrib_mem_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_wr_attrib_mem_hdl (struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int  rtl8188eu_oid_rt_pro_set_rf_intfs_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_poll_rx_status_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_cfg_debug_message_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_set_data_rate_ex_hdl(struct oid_par_priv *poid_par_priv)
{
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

	int status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+OID_RT_PRO_SET_DATA_RATE_EX\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (rtw_setdatarate_cmd(Adapter, poid_par_priv->information_buf) != _SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_get_thermal_meter_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	u8 thermal = 0;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_get_thermal_meter_hdl\n"));

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	GetThermalMeter(Adapter, &thermal);
	_irqlevel_changed_(&oldirql, RAISE);

	*(u32 *)poid_par_priv->information_buf = (u32)thermal;
	*poid_par_priv->bytes_rw = sizeof(u32);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_read_tssi_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_set_power_tracking_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);


_func_enter_;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	if (poid_par_priv->type_of_oid == SET_OID) {
		u8 enable;

		enable = *(u8 *)poid_par_priv->information_buf;
		RT_TRACE(_module_mp_, _drv_notice_,
			 ("+rtl8188eu_oid_rt_pro_set_power_tracking_hdl: enable =%d\n", enable));

		SetPowerTracking(Adapter, enable);
	} else {
		GetPowerTracking(Adapter, (u8 *)poid_par_priv->information_buf);
	}
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_set_basic_rate_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_qry_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_set_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_h2c_set_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
/*  */
int rtl8188eu_oid_rt_pro_h2c_get_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

/*   rtl8188eu_oid_rtl_seg_87_12_00   section start **************** */
int rtl8188eu_oid_rt_pro_encryption_ctrl_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_add_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_dele_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_query_dr_variable_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}
/*  */
int rtl8188eu_oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	struct efuse_access_struct *pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0, max_available_size = 0;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct efuse_access_struct))
		return NDIS_STATUS_INVALID_LENGTH;

	pefuse = (struct efuse_access_struct *)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("+rtl8188eu_oid_rt_pro_read_efuse_hd: buf_len=%d addr=%d cnts=%d\n",
		  poid_par_priv->information_buf_len, addr, cnts));

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

	if ((addr + cnts) > max_available_size) {
		RT_TRACE(_module_mp_, _drv_err_, ("!rtl8188eu_oid_rt_pro_read_efuse_hdl: parameter error!\n"));
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (rtw_efuse_access(Adapter, false, addr, cnts, data) == _FAIL) {
		RT_TRACE(_module_mp_, _drv_err_, ("!rtl8188eu_oid_rt_pro_read_efuse_hdl: rtw_efuse_access FAIL!\n"));
		status = NDIS_STATUS_FAILURE;
	} else {
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	}
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	struct efuse_access_struct *pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0, max_available_size = 0;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);


_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pefuse = (struct efuse_access_struct *)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("+rtl8188eu_oid_rt_pro_write_efuse_hdl: buf_len=%d addr=0x%04x cnts=%d\n",
		  poid_par_priv->information_buf_len, addr, cnts));

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

	if ((addr + cnts) > max_available_size) {
		RT_TRACE(_module_mp_, _drv_err_, ("!rtl8188eu_oid_rt_pro_write_efuse_hdl: parameter error"));
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (rtw_efuse_access(Adapter, true, addr, cnts, data) == _FAIL)
		status = NDIS_STATUS_FAILURE;
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_rw_efuse_pgpkt_hdl(struct oid_par_priv *poid_par_priv)
{
	struct pgpkt *ppgpkt;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < sizeof(struct pgpkt *))
		return NDIS_STATUS_INVALID_LENGTH;

	ppgpkt = (struct pgpkt *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		RT_TRACE(_module_mp_, _drv_notice_,
			 ("rtl8188eu_oid_rt_pro_rw_efuse_pgpkt_hdl: Read offset=0x%x\n",\
			 ppgpkt->offset));

		Efuse_PowerSwitch(Adapter, false, true);
		if (Efuse_PgPacketRead(Adapter, ppgpkt->offset, ppgpkt->data, false) == true)
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		else
			status = NDIS_STATUS_FAILURE;
		Efuse_PowerSwitch(Adapter, false, false);
	} else {
		RT_TRACE(_module_mp_, _drv_notice_,
			 ("rtl8188eu_oid_rt_pro_rw_efuse_pgpkt_hdl: Write offset=0x%x word_en=0x%x\n",\
			 ppgpkt->offset, ppgpkt->word_en));

		Efuse_PowerSwitch(Adapter, true, true);
		if (Efuse_PgPacketWrite(Adapter, ppgpkt->offset, ppgpkt->word_en, ppgpkt->data, false) == true)
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		else
			status = NDIS_STATUS_FAILURE;
		Efuse_PowerSwitch(Adapter, true, false);
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_info_,
		 ("-rtl8188eu_oid_rt_pro_rw_efuse_pgpkt_hdl: status=0x%08X\n", status));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_get_efuse_current_size_hdl(struct oid_par_priv *poid_par_priv)
{
	u16 size;
	u8 ret;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	ret = efuse_GetCurrentSize(Adapter, &size);
	_irqlevel_changed_(&oldirql, RAISE);
	if (ret == _SUCCESS) {
		*(u32 *)poid_par_priv->information_buf = size;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else {
		status = NDIS_STATUS_FAILURE;
	}
_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);

_func_enter_;

	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	*(u32 *)poid_par_priv->information_buf = efuse_GetMaxSize(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	RT_TRACE(_module_mp_, _drv_info_,
		 ("-rtl8188eu_oid_rt_get_efuse_max_size_hdl: size=%d status=0x%08X\n",
		  *(int *)poid_par_priv->information_buf, status));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	int status;

_func_enter_;

	RT_TRACE(_module_mp_, _drv_info_, ("+rtl8188eu_oid_rt_pro_efuse_hdl\n"));

	if (poid_par_priv->type_of_oid == QUERY_OID)
		status = rtl8188eu_oid_rt_pro_read_efuse_hdl(poid_par_priv);
	else
		status = rtl8188eu_oid_rt_pro_write_efuse_hdl(poid_par_priv);

	RT_TRACE(_module_mp_, _drv_info_, ("-rtl8188eu_oid_rt_pro_efuse_hdl: status=0x%08X\n", status));

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv)
{
	u8		*data;
	int status = NDIS_STATUS_SUCCESS;
	struct adapter *Adapter = (struct adapter *)(poid_par_priv->adapter_context);
	u16	maplen = 0;

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_pro_efuse_map_hdl\n"));

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&maplen, false);

	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < maplen)
		return NDIS_STATUS_INVALID_LENGTH;

	data = (u8 *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		RT_TRACE(_module_mp_, _drv_info_,
			 ("rtl8188eu_oid_rt_pro_efuse_map_hdl: READ\n"));

		if (rtw_efuse_map_read(Adapter, 0, maplen, data) == _SUCCESS) {
			*poid_par_priv->bytes_rw = maplen;
		} else {
			RT_TRACE(_module_mp_, _drv_err_,
				 ("rtl8188eu_oid_rt_pro_efuse_map_hdl: READ fail\n"));
			status = NDIS_STATUS_FAILURE;
		}
	} else {
		/*  SET_OID */
		RT_TRACE(_module_mp_, _drv_info_,
			 ("rtl8188eu_oid_rt_pro_efuse_map_hdl: WRITE\n"));

		if (rtw_efuse_map_write(Adapter, 0, maplen, data) == _SUCCESS) {
			*poid_par_priv->bytes_rw = maplen;
		} else {
			RT_TRACE(_module_mp_, _drv_err_,
				 ("rtl8188eu_oid_rt_pro_efuse_map_hdl: WRITE fail\n"));
			status = NDIS_STATUS_FAILURE;
		}
	}

	_irqlevel_changed_(&oldirql, RAISE);

	RT_TRACE(_module_mp_, _drv_info_,
		 ("-rtl8188eu_oid_rt_pro_efuse_map_hdl: status=0x%08X\n", status));

_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_set_crystal_cap_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;
	return status;
}

int rtl8188eu_oid_rt_set_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
	u8		rx_pkt_type;
	int status = NDIS_STATUS_SUCCESS;

_func_enter_;

	RT_TRACE(_module_mp_, _drv_notice_, ("+rtl8188eu_oid_rt_set_rx_packet_type_hdl\n"));

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	rx_pkt_type = *((u8 *)poid_par_priv->information_buf);/* 4 */

	RT_TRACE(_module_mp_, _drv_info_, ("rx_pkt_type: %x\n", rx_pkt_type));
_func_exit_;

	return status;
}

int rtl8188eu_oid_rt_pro_set_tx_agc_offset_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_oid_rt_pro_set_pkt_test_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

int rtl8188eu_mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv)
{
	struct mp_xmit_parm *pparm;
	struct adapter *padapter;
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;

	RT_TRACE(_module_mp_, _drv_notice_, ("+%s\n", __func__));

	pparm = (struct mp_xmit_parm *)poid_par_priv->information_buf;
	padapter = (struct adapter *)poid_par_priv->adapter_context;
	pmp_priv = &padapter->mppriv;

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		pparm->enable = !pmp_priv->tx.stop;
		pparm->count = pmp_priv->tx.sended;
	} else {
		if (pparm->enable == 0) {
			pmp_priv->tx.stop = 1;
		} else if (pmp_priv->tx.stop == 1) {
			pmp_priv->tx.stop = 0;
			pmp_priv->tx.count = pparm->count;
			pmp_priv->tx.payload = pparm->payload_type;
			pattrib = &pmp_priv->tx.attrib;
			pattrib->pktlen = pparm->length;
			memcpy(pattrib->dst, pparm->da, ETH_ALEN);
			SetPacketTx(padapter);
		} else {
			return NDIS_STATUS_FAILURE;
		}
	}

	return NDIS_STATUS_SUCCESS;
}

/*  */
int rtl8188eu_oid_rt_set_power_down_hdl(struct oid_par_priv *poid_par_priv)
{
	int status = NDIS_STATUS_SUCCESS;

_func_enter_;

	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	RT_TRACE(_module_mp_, _drv_info_,
		 ("\n ===> Setrtl8188eu_oid_rt_set_power_down_hdl.\n"));

	_irqlevel_changed_(&oldirql, LOWER);

	/* CALL  the power_down function */
	_irqlevel_changed_(&oldirql, RAISE);

_func_exit_;

	return status;
}
/*  */
int rtl8188eu_oid_rt_get_power_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}
