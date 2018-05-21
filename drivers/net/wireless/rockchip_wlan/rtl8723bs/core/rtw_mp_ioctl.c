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
#define _RTW_MP_IOCTL_C_

#include <drv_types.h>
#include <rtw_mp_ioctl.h>
#include "../hal/phydm/phydm_precomp.h"

/* ****************  oid_rtl_seg_81_85   section start **************** */
NDIS_STATUS oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID)
		Adapter->registrypriv.wireless_mode = *(u8 *)poid_par_priv->information_buf;
	else if (poid_par_priv->type_of_oid == QUERY_OID) {
		*(u8 *)poid_par_priv->information_buf = Adapter->registrypriv.wireless_mode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		status = NDIS_STATUS_NOT_ACCEPTED;


	return status;
}
/* ****************  oid_rtl_seg_81_87_80   section start **************** */
NDIS_STATUS oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct bb_reg_param))
		return NDIS_STATUS_INVALID_LENGTH;

	pbbreg = (struct bb_reg_param *)(poid_par_priv->information_buf);

	offset = (u16)(pbbreg->offset) & 0xFFF; /* 0ffset :0x800~0xfff */
	if (offset < BB_REG_BASE_ADDR)
		offset |= BB_REG_BASE_ADDR;

	value = pbbreg->value;


	_irqlevel_changed_(&oldirql, LOWER);
	write_bbreg(Adapter, offset, 0xFFFFFFFF, value);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	struct bb_reg_param *pbbreg;
	u16 offset;
	u32 value;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);



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


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);



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


	_irqlevel_changed_(&oldirql, LOWER);
	write_rfreg(Adapter, path, offset, value);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	struct rf_reg_param *pbbreg;
	u8 path;
	u8 offset;
	u32 value;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;



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



	return status;
}
/* ****************  oid_rtl_seg_81_87_00   section end****************
 * ------------------------------------------------------------------------------ */

/* ****************  oid_rtl_seg_81_80_00   section start ****************
 * ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_data_rate_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		ratevalue;/* 4 */
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	ratevalue = *((u32 *)poid_par_priv->information_buf); /* 4 */
	if (ratevalue >= MPT_RATE_LAST)
		return NDIS_STATUS_INVALID_DATA;

	Adapter->mppriv.rateidx = ratevalue;

	_irqlevel_changed_(&oldirql, LOWER);
	SetDataRate(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		mode;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (Adapter->registrypriv.mp_mode == 0)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	/* IQCalibrateBcut(Adapter); */

	mode = *((u32 *)poid_par_priv->information_buf);
	Adapter->mppriv.mode = mode;/* 1 for loopback */

	if (mp_start_test(Adapter) == _FAIL) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		goto exit;
	}

exit:
	_irqlevel_changed_(&oldirql, RAISE);



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);
	mp_stop_test(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_channel_direct_call_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		Channel;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		*((u32 *)poid_par_priv->information_buf) = Adapter->mppriv.channel;
		return NDIS_STATUS_SUCCESS;
	}

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	Channel = *((u32 *)poid_par_priv->information_buf);
	if (Channel > 14)
		return NDIS_STATUS_NOT_ACCEPTED;
	Adapter->mppriv.channel = Channel;

	_irqlevel_changed_(&oldirql, LOWER);
	SetChannel(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u16		bandwidth;
	u16		channel_offset;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	bandwidth = *((u32 *)poid_par_priv->information_buf); /* 4 */
	channel_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (bandwidth != CHANNEL_WIDTH_40)
		bandwidth = CHANNEL_WIDTH_20;
	padapter->mppriv.bandwidth = (u8)bandwidth;
	padapter->mppriv.prime_channel_offset = (u8)channel_offset;

	_irqlevel_changed_(&oldirql, LOWER);
	SetBandwidth(padapter);
	_irqlevel_changed_(&oldirql, RAISE);



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_antenna_bb_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		antenna;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID) {
		antenna = *(u32 *)poid_par_priv->information_buf;

		Adapter->mppriv.antenna_tx = (u16)((antenna & 0xFFFF0000) >> 16);
		Adapter->mppriv.antenna_rx = (u16)(antenna & 0x0000FFFF);

		_irqlevel_changed_(&oldirql, LOWER);
		SetAntenna(Adapter);
		_irqlevel_changed_(&oldirql, RAISE);
	} else {
		antenna = (Adapter->mppriv.antenna_tx << 16) | Adapter->mppriv.antenna_rx;
		*(u32 *)poid_par_priv->information_buf = antenna;
	}


	return status;
}

NDIS_STATUS oid_rt_pro_set_tx_power_control_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		tx_pwr_idx;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	tx_pwr_idx = *((u32 *)poid_par_priv->information_buf);
	if (tx_pwr_idx > MAX_TX_PWR_INDEX_N_MODE)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.txpoweridx = (u8)tx_pwr_idx;


	_irqlevel_changed_(&oldirql, LOWER);
	SetTxPower(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}

/* ------------------------------------------------------------------------------
 * ****************  oid_rtl_seg_81_80_20   section start ****************
 * ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_query_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG *)poid_par_priv->information_buf =  Adapter->mppriv.tx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		status = NDIS_STATUS_INVALID_LENGTH;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_query_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG *)poid_par_priv->information_buf =  Adapter->mppriv.rx_pktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		status = NDIS_STATUS_INVALID_LENGTH;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_query_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		*(ULONG *)poid_par_priv->information_buf =  Adapter->mppriv.rx_crcerrpktcount;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	} else
		status = NDIS_STATUS_INVALID_LENGTH;


	return status;
}
/* ------------------------------------------------------------------------------ */

NDIS_STATUS oid_rt_pro_reset_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	Adapter->mppriv.tx_pktcount = 0;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_reset_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	if (poid_par_priv->information_buf_len == sizeof(ULONG)) {
		Adapter->mppriv.rx_pktcount = 0;
		Adapter->mppriv.rx_crcerrpktcount = 0;
	} else
		status = NDIS_STATUS_INVALID_LENGTH;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_reset_phy_rx_packet_count_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	ResetPhyRxPktCount(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_get_phy_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len != sizeof(ULONG))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(ULONG *)poid_par_priv->information_buf = GetPhyRxPktReceived(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_get_phy_rx_packet_crc32_error_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;


	if (poid_par_priv->information_buf_len != sizeof(ULONG))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	*(ULONG *)poid_par_priv->information_buf = GetPhyRxPktCRC32Error(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
}
/* ****************  oid_rtl_seg_81_80_20   section end **************** */
NDIS_STATUS oid_rt_pro_set_continuous_tx_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		bStartTest;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetContinuousTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			RTW_INFO("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}

NDIS_STATUS oid_rt_pro_set_single_carrier_tx_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		bStartTest;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleCarrierTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			RTW_INFO("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}

NDIS_STATUS oid_rt_pro_set_carrier_suppression_tx_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		bStartTest;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetCarrierSuppressionTx(Adapter, (u8)bStartTest);
	if (bStartTest) {
		struct mp_priv *pmp_priv = &Adapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			RTW_INFO("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(Adapter);
	}
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}

NDIS_STATUS oid_rt_pro_set_single_tone_tx_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32		bStartTest;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	bStartTest = *((u32 *)poid_par_priv->information_buf);

	_irqlevel_changed_(&oldirql, LOWER);
	SetSingleToneTx(Adapter, (u8)bStartTest);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}

NDIS_STATUS oid_rt_pro_set_modulation_hdl(struct oid_par_priv *poid_par_priv)
{
	return 0;
}

NDIS_STATUS oid_rt_pro_trigger_gpio_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);
	rtw_hal_set_hwreg(Adapter, HW_VAR_TRIGGER_GPIO_0, 0);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ****************  oid_rtl_seg_81_80_00   section end ****************
 * ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro8711_join_bss_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PNDIS_802_11_SSID pssid;


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


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pRW_Reg	RegRWStruct;
	u32		offset, width;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RegRWStruct = (pRW_Reg)poid_par_priv->information_buf;
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

	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = width;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pRW_Reg	RegRWStruct;
	u32		offset, width, value;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	RegRWStruct = (pRW_Reg)poid_par_priv->information_buf;
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



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_burst_read_register_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pBurst_RW_Reg	pBstRwReg;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pBstRwReg = (pBurst_RW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	rtw_read_mem(padapter, pBstRwReg->offset, (u32)pBstRwReg->len, pBstRwReg->Data);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_burst_write_register_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pBurst_RW_Reg	pBstRwReg;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pBstRwReg = (pBurst_RW_Reg)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	rtw_write_mem(padapter, pBstRwReg->offset, (u32)pBstRwReg->len, pBstRwReg->Data);
	_irqlevel_changed_(&oldirql, RAISE);



	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_write_txcmd_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif

	TX_CMD_Desc	*TxCmd_Info;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;


	TxCmd_Info = (TX_CMD_Desc *)poid_par_priv->information_buf;


		 _irqlevel_changed_(&oldirql, LOWER);

		rtw_write32(Adapter, TxCmd_Info->offset + 0, (unsigned int)TxCmd_Info->TxCMD.value[0]);
		rtw_write32(Adapter, TxCmd_Info->offset + 4, (unsigned int)TxCmd_Info->TxCMD.value[1]);

		 _irqlevel_changed_(&oldirql, RAISE);



		 return status;
#else
	return 0;
#endif
}

/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_read16_eeprom_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pEEPROM_RWParam pEEPROM;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pEEPROM = (pEEPROM_RWParam)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	pEEPROM->value = eeprom_read16(padapter, (u16)(pEEPROM->offset >> 1));
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
#else
	return 0;
#endif
}

/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_write16_eeprom_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	pEEPROM_RWParam pEEPROM;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pEEPROM = (pEEPROM_RWParam)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	eeprom_write16(padapter, (u16)(pEEPROM->offset >> 1), pEEPROM->value);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro8711_wi_poll_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct mp_wiparam *pwi_param;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(struct mp_wiparam))
		return NDIS_STATUS_INVALID_LENGTH;

	if (Adapter->mppriv.workparam.bcompleted == _FALSE)
		return NDIS_STATUS_NOT_ACCEPTED;

	pwi_param = (struct mp_wiparam *)poid_par_priv->information_buf;

	_rtw_memcpy(pwi_param, &Adapter->mppriv.workparam, sizeof(struct mp_wiparam));
	Adapter->mppriv.act_in_progress = _FALSE;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro8711_pkt_loss_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(uint) * 2) {
		return NDIS_STATUS_INVALID_LENGTH;
	}

	if (*(uint *)poid_par_priv->information_buf == 1) /* init==1 */
		Adapter->mppriv.rx_pktloss = 0;

	*((uint *)poid_par_priv->information_buf + 1) = Adapter->mppriv.rx_pktloss;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_rd_attrib_mem_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	struct io_queue *pio_queue = (struct io_queue *)Adapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

#ifdef CONFIG_SDIO_HCI
	void (*_attrib_read)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#endif



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_SDIO_HCI
	_irqlevel_changed_(&oldirql, LOWER);
	{
		u32 *plmem = (u32 *)poid_par_priv->information_buf + 2;
		_attrib_read = pintfhdl->io_ops._attrib_read;
		_attrib_read(pintfhdl, *((u32 *)poid_par_priv->information_buf),
			*((u32 *)poid_par_priv->information_buf + 1), (u8 *)plmem);
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	}
	_irqlevel_changed_(&oldirql, RAISE);
#endif


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_wr_attrib_mem_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	struct io_queue *pio_queue = (struct io_queue *)Adapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

#ifdef CONFIG_SDIO_HCI
	void (*_attrib_write)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#endif


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#ifdef CONFIG_SDIO_HCI
	_irqlevel_changed_(&oldirql, LOWER);
	{
		u32 *plmem = (u32 *)poid_par_priv->information_buf + 2;
		_attrib_write = pintfhdl->io_ops._attrib_write;
		_attrib_write(pintfhdl, *(u32 *)poid_par_priv->information_buf,
			*((u32 *)poid_par_priv->information_buf + 1), (u8 *)plmem);
	}
	_irqlevel_changed_(&oldirql, RAISE);
#endif


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS  oid_rt_pro_set_rf_intfs_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (rtw_setrfintfs_cmd(Adapter, *(unsigned char *)poid_par_priv->information_buf) == _FAIL)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_poll_rx_status_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_rtw_memcpy(poid_par_priv->information_buf, (unsigned char *)&Adapter->mppriv.rxstat, sizeof(struct recv_stat));
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_cfg_debug_message_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	PCFG_DBG_MSG_STRUCT pdbg_msg;



#if 0/*#ifdef CONFIG_DEBUG_RTL871X*/

	pdbg_msg = (PCFG_DBG_MSG_STRUCT)(poid_par_priv->information_buf);

	if (poid_par_priv->type_of_oid == SET_OID) {

		GlobalDebugLevel = pdbg_msg->DebugLevel;
		GlobalDebugComponents = (pdbg_msg->DebugComponent_H32 << 32) | pdbg_msg->DebugComponent_L32;
	} else {
		pdbg_msg->DebugLevel = GlobalDebugLevel;
		pdbg_msg->DebugComponent_H32 = (u32)(GlobalDebugComponents >> 32);
		pdbg_msg->DebugComponent_L32 = (u32)GlobalDebugComponents;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	}

#endif


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_data_rate_ex_hdl(struct oid_par_priv *poid_par_priv)
{
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, LOWER);

	if (rtw_setdatarate_cmd(Adapter, poid_par_priv->information_buf) != _SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ----------------------------------------------------------------------------- */
NDIS_STATUS oid_rt_get_thermal_meter_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	u8 thermal = 0;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	GetThermalMeter(Adapter, &thermal);
	_irqlevel_changed_(&oldirql, RAISE);

	*(u32 *)poid_par_priv->information_buf = (u32)thermal;
	*poid_par_priv->bytes_rw = sizeof(u32);


	return status;
}
/* ----------------------------------------------------------------------------- */
NDIS_STATUS oid_rt_pro_read_tssi_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (Adapter->mppriv.act_in_progress == _TRUE)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	/* init workparam */
	Adapter->mppriv.act_in_progress = _TRUE;
	Adapter->mppriv.workparam.bcompleted = _FALSE;
	Adapter->mppriv.workparam.act_type = MPT_READ_TSSI;
	Adapter->mppriv.workparam.io_offset = 0;
	Adapter->mppriv.workparam.io_value = 0xFFFFFFFF;

	_irqlevel_changed_(&oldirql, LOWER);

	if (!rtw_gettssi_cmd(Adapter, 0, (u8 *)&Adapter->mppriv.workparam.io_value))
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_power_tracking_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	/*	if (poid_par_priv->type_of_oid != SET_OID)
	 *		return NDIS_STATUS_NOT_ACCEPTED; */

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	_irqlevel_changed_(&oldirql, LOWER);
	if (poid_par_priv->type_of_oid == SET_OID) {
		u8 enable;

		enable = *(u8 *)poid_par_priv->information_buf;

		SetPowerTracking(Adapter, enable);
	} else
		GetPowerTracking(Adapter, (u8 *)poid_par_priv->information_buf);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ----------------------------------------------------------------------------- */
NDIS_STATUS oid_rt_pro_set_basic_rate_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u32 ratevalue;
	u8 datarates[NumRates];
	int i;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;
#if 0
	ratevalue = *((u32 *)poid_par_priv->information_buf);

	for (i = 0; i < NumRates; i++) {
		if (ratevalue == mpdatarate[i])
			datarates[i] = mpdatarate[i];
		else
			datarates[i] = 0xff;
	}

	_irqlevel_changed_(&oldirql, LOWER);

	if (rtw_setbasicrate_cmd(padapter, datarates) != _SUCCESS)
		status = NDIS_STATUS_NOT_ACCEPTED;

	_irqlevel_changed_(&oldirql, RAISE);
#endif


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_qry_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < 8)
		return NDIS_STATUS_INVALID_LENGTH;

	*poid_par_priv->bytes_rw = 8;
	_rtw_memcpy(poid_par_priv->information_buf, &(adapter_to_pwrctl(Adapter)->pwr_mode), 8);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_set_pwrstate_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	uint pwr_mode, smart_ps;



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_rw = 0;
	*poid_par_priv->bytes_needed = 8;

	if (poid_par_priv->information_buf_len < 8)
		return NDIS_STATUS_INVALID_LENGTH;

	pwr_mode = *(uint *)(poid_par_priv->information_buf);
	smart_ps = *(uint *)((int)poid_par_priv->information_buf + 4);

	*poid_par_priv->bytes_rw = 8;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_h2c_set_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct setratable_parm *prate_table;
	u8		res;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed  = sizeof(struct setratable_parm);
	if (poid_par_priv->information_buf_len < sizeof(struct setratable_parm))
		return NDIS_STATUS_INVALID_LENGTH;

	prate_table = (struct setratable_parm *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	res = rtw_setrttbl_cmd(Adapter, prate_table);
	_irqlevel_changed_(&oldirql, RAISE);

	if (res == _FAIL)
		status = NDIS_STATUS_FAILURE;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_h2c_get_rate_table_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

#if 0
	struct mp_wi_cntx *pmp_wi_cntx = &(Adapter->mppriv.wi_cntx);
	u8 res = _SUCCESS;
	DEBUG_INFO(("===> Set OID_RT_PRO_H2C_GET_RATE_TABLE.\n"));

	if (pmp_wi_cntx->bmp_wi_progress == _TRUE) {
		DEBUG_ERR(("\n mp workitem is progressing, not allow to set another workitem right now!!!\n"));
		Status = NDIS_STATUS_NOT_ACCEPTED;
		break;
	} else {
		pmp_wi_cntx->bmp_wi_progress = _TRUE;
		pmp_wi_cntx->param.bcompleted = _FALSE;
		pmp_wi_cntx->param.act_type = MPT_GET_RATE_TABLE;
		pmp_wi_cntx->param.io_offset = 0x0;
		pmp_wi_cntx->param.bytes_cnt = sizeof(struct getratable_rsp);
		pmp_wi_cntx->param.io_value = 0xffffffff;

		res = rtw_getrttbl_cmd(Adapter, (struct getratable_rsp *)pmp_wi_cntx->param.data);
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		if (res != _SUCCESS)
			Status = NDIS_STATUS_NOT_ACCEPTED;
	}
	DEBUG_INFO(("\n <=== Set OID_RT_PRO_H2C_GET_RATE_TABLE.\n"));
#endif


	return status;
#else
	return 0;
#endif
}

/* ****************  oid_rtl_seg_87_12_00   section start **************** */
NDIS_STATUS oid_rt_pro_encryption_ctrl_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	struct security_priv *psecuritypriv = &Adapter->securitypriv;

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	ENCRY_CTRL_STATE encry_mode;


	*poid_par_priv->bytes_needed = sizeof(u8);
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	if (poid_par_priv->type_of_oid == SET_OID) {
		encry_mode = *((u8 *)poid_par_priv->information_buf);
		switch (encry_mode) {
		case HW_CONTROL:
#if 0
			Adapter->registrypriv.software_decrypt = _FALSE;
			Adapter->registrypriv.software_encrypt = _FALSE;
#else
			psecuritypriv->sw_decrypt = _FALSE;
			psecuritypriv->sw_encrypt = _FALSE;
#endif
			break;
		case SW_CONTROL:
#if 0
			Adapter->registrypriv.software_decrypt = _TRUE;
			Adapter->registrypriv.software_encrypt = _TRUE;
#else
			psecuritypriv->sw_decrypt = _TRUE;
			psecuritypriv->sw_encrypt = _TRUE;
#endif
			break;
		case HW_ENCRY_SW_DECRY:
#if 0
			Adapter->registrypriv.software_decrypt = _TRUE;
			Adapter->registrypriv.software_encrypt = _FALSE;
#else
			psecuritypriv->sw_decrypt = _TRUE;
			psecuritypriv->sw_encrypt = _FALSE;
#endif
			break;
		case SW_ENCRY_HW_DECRY:
#if 0
			Adapter->registrypriv.software_decrypt = _FALSE;
			Adapter->registrypriv.software_encrypt = _TRUE;
#else
			psecuritypriv->sw_decrypt = _FALSE;
			psecuritypriv->sw_encrypt = _TRUE;
#endif
			break;
		}

	} else {
#if 0
		if (Adapter->registrypriv.software_encrypt == _FALSE) {
			if (Adapter->registrypriv.software_decrypt == _FALSE)
				encry_mode = HW_CONTROL;
			else
				encry_mode = HW_ENCRY_SW_DECRY;
		} else {
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

		*(u8 *)poid_par_priv->information_buf =  encry_mode;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;

	}

	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_add_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct sta_info *psta = NULL;
	UCHAR		*macaddr;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = ETH_ALEN;
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	macaddr = (UCHAR *) poid_par_priv->information_buf ;


	_irqlevel_changed_(&oldirql, LOWER);

	psta = rtw_get_stainfo(&Adapter->stapriv, macaddr);

	if (psta == NULL) { /* the sta have been in sta_info_queue => do nothing */
		psta = rtw_alloc_stainfo(&Adapter->stapriv, macaddr);

		if (psta == NULL) {
			status = NDIS_STATUS_FAILURE;
		}
	}

	_irqlevel_changed_(&oldirql, RAISE);

	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_dele_sta_info_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	struct sta_info *psta = NULL;
	UCHAR		*macaddr;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = ETH_ALEN;
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;

	macaddr = (UCHAR *) poid_par_priv->information_buf ;

	psta = rtw_get_stainfo(&Adapter->stapriv, macaddr);
	if (psta != NULL) {
		/* _enter_critical(&(Adapter->stapriv.sta_hash_lock), &irqL); */
		rtw_free_stainfo(Adapter, psta);
		/* _exit_critical(&(Adapter->stapriv.sta_hash_lock), &irqL); */
	}

	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
#if 0
static u32 mp_query_drv_var(_adapter *padapter, u8 offset, u32 var)
{
#ifdef CONFIG_SDIO_HCI

	if (offset == 1) {
		u16 tmp_blk_num;
		tmp_blk_num = rtw_read16(padapter, SDIO_RX0_RDYBLK_NUM);
		if (adapter_to_dvobj(padapter)->rxblknum != tmp_blk_num) {
			/*	sd_recv_rxfifo(padapter); */
		}
	}

#if 0
	if (offset <= 100) { /* For setting data rate and query data rate */
		if (offset == 100) { /* For query data rate */
			var = padapter->registrypriv.tx_rate;

		} else if (offset < 0x1d) { /* For setting data rate */
			padapter->registrypriv.tx_rate = offset;
			var = padapter->registrypriv.tx_rate;
			padapter->registrypriv.use_rate = _TRUE;
		} else { /* not use the data rate */
			padapter->registrypriv.use_rate = _FALSE;
		}
	} else if (offset <= 110) { /* for setting debug level */
		if (offset == 110) { /* For query data rate */
			padapter->registrypriv.dbg_level = GlobalDebugLevel;
			var = padapter->registrypriv.dbg_level;
		} else if (offset < 110 && offset > 100) {
			padapter->registrypriv.dbg_level = GlobalDebugLevel = offset - 100;
			var = padapter->registrypriv.dbg_level;

		}
	} else if (offset > 110 && offset < 116) {
		if (115 == offset) {
		} else {
			switch (offset) {
			case 111:
				adapter_to_dvobj(padapter)->tx_block_mode = 1;
				adapter_to_dvobj(padapter)->rx_block_mode = 1;
				break;
			case 112:
				adapter_to_dvobj(padapter)->tx_block_mode = 1;
				adapter_to_dvobj(padapter)->rx_block_mode = 0;
				break;
			case 113:
				adapter_to_dvobj(padapter)->tx_block_mode = 0;
				adapter_to_dvobj(padapter)->rx_block_mode = 1;
				break;
			case 114:
				adapter_to_dvobj(padapter)->tx_block_mode = 0;
				adapter_to_dvobj(padapter)->rx_block_mode = 0;
				break;
			default:
				break;

			}

		}

	} else if (offset >= 127) {
		u64	prnt_dbg_comp;
		u8   chg_idx;
		u64	tmp_dbg_comp;
		chg_idx = offset - 0x80;
		tmp_dbg_comp = BIT(chg_idx);
		prnt_dbg_comp = padapter->registrypriv.dbg_component = GlobalDebugComponents;
		if (offset == 127) {
			/*		prnt_dbg_comp=padapter->registrypriv.dbg_component= GlobalDebugComponents; */
			var = (u32)(padapter->registrypriv.dbg_component);
			prnt_dbg_comp = GlobalDebugComponents;
			prnt_dbg_comp = GlobalDebugComponents = padapter->registrypriv.dbg_component;

		} else {
			prnt_dbg_comp = GlobalDebugComponents;
			prnt_dbg_comp = GlobalDebugComponents = padapter->registrypriv.dbg_component;

			if (GlobalDebugComponents & tmp_dbg_comp) {
				/* this bit is already set, now clear it */
				GlobalDebugComponents = GlobalDebugComponents & (~tmp_dbg_comp);
			} else {
				/* this bit is not set, now set it. */
				GlobalDebugComponents = GlobalDebugComponents | tmp_dbg_comp;
			}
			prnt_dbg_comp = GlobalDebugComponents;

			var = (u32)(GlobalDebugComponents);
			/* GlobalDebugComponents=padapter->registrypriv.dbg_component; */

		}
	}
#endif
#endif

	return var;
}
#endif

NDIS_STATUS oid_rt_pro_query_dr_variable_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER		Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL			oldirql;
#endif
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;

	DR_VARIABLE_STRUCT	*pdrv_var;


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	*poid_par_priv->bytes_needed = sizeof(DR_VARIABLE_STRUCT);
	if (poid_par_priv->information_buf_len < *poid_par_priv->bytes_needed)
		return NDIS_STATUS_INVALID_LENGTH;


	pdrv_var = (struct _DR_VARIABLE_STRUCT_ *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	pdrv_var->variable = mp_query_drv_var(Adapter, pdrv_var->offset, pdrv_var->variable);
	_irqlevel_changed_(&oldirql, RAISE);

	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


	return status;
#else
	return 0;
#endif
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;


	if (poid_par_priv->information_buf_len < sizeof(UCHAR)) {
		status = NDIS_STATUS_INVALID_LENGTH;
		*poid_par_priv->bytes_needed = sizeof(UCHAR);
		return status;
	}

	if (poid_par_priv->type_of_oid == SET_OID) {
		Adapter->mppriv.rx_with_status = *(UCHAR *) poid_par_priv->information_buf;


	} else {
		*(UCHAR *) poid_par_priv->information_buf = Adapter->mppriv.rx_with_status;
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;


		/* *(u32 *)&Adapter->eeprompriv.mac_addr[0]=rtw_read32(Adapter, 0x10250050); */
		/* *(u16 *)&Adapter->eeprompriv.mac_addr[4]=rtw_read16(Adapter, 0x10250054); */
	}
#endif

	return NDIS_STATUS_SUCCESS;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	PEFUSE_ACCESS_STRUCT pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0, max_available_size = 0;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(EFUSE_ACCESS_STRUCT))
		return NDIS_STATUS_INVALID_LENGTH;

	pefuse = (PEFUSE_ACCESS_STRUCT)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;


	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);

	if ((addr + cnts) > max_available_size) {
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (rtw_efuse_access(Adapter, _FALSE, addr, cnts, data) == _FAIL) {
		status = NDIS_STATUS_FAILURE;
	} else
		*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL oldirql;
#endif
	PEFUSE_ACCESS_STRUCT pefuse;
	u8 *data;
	u16 addr = 0, cnts = 0, max_available_size = 0;
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PADAPTER Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	pefuse = (PEFUSE_ACCESS_STRUCT)poid_par_priv->information_buf;
	addr = pefuse->start_addr;
	cnts = pefuse->cnts;
	data = pefuse->data;


	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);

	if ((addr + cnts) > max_available_size) {
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	_irqlevel_changed_(&oldirql, LOWER);
	if (rtw_efuse_access(Adapter, _TRUE, addr, cnts, data) == _FAIL)
		status = NDIS_STATUS_FAILURE;
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_rw_efuse_pgpkt_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	PPGPKT_STRUCT	ppgpkt;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);



	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < sizeof(PGPKT_STRUCT))
		return NDIS_STATUS_INVALID_LENGTH;

	ppgpkt = (PPGPKT_STRUCT)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID) {

		Efuse_PowerSwitch(Adapter, _FALSE, _TRUE);
		if (Efuse_PgPacketRead(Adapter, ppgpkt->offset, ppgpkt->data, _FALSE) == _TRUE)
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		else
			status = NDIS_STATUS_FAILURE;
		Efuse_PowerSwitch(Adapter, _FALSE, _FALSE);
	} else {

		Efuse_PowerSwitch(Adapter, _TRUE, _TRUE);
		if (Efuse_PgPacketWrite(Adapter, ppgpkt->offset, ppgpkt->word_en, ppgpkt->data, _FALSE) == _TRUE)
			*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
		else
			status = NDIS_STATUS_FAILURE;
		Efuse_PowerSwitch(Adapter, _TRUE, _FALSE);
	}

	_irqlevel_changed_(&oldirql, RAISE);



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_get_efuse_current_size_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u16 size;
	u8 ret;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


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
	} else
		status = NDIS_STATUS_FAILURE;


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);


	if (poid_par_priv->type_of_oid != QUERY_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	*(u32 *)poid_par_priv->information_buf = efuse_GetMaxSize(Adapter);
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status;



	if (poid_par_priv->type_of_oid == QUERY_OID)
		status = oid_rt_pro_read_efuse_hdl(poid_par_priv);
	else
		status = oid_rt_pro_write_efuse_hdl(poid_par_priv);



	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u8		*data;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	u16	mapLen = 0;



	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, _FALSE);

	*poid_par_priv->bytes_rw = 0;

	if (poid_par_priv->information_buf_len < mapLen)
		return NDIS_STATUS_INVALID_LENGTH;

	data = (u8 *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);

	if (poid_par_priv->type_of_oid == QUERY_OID) {

		if (rtw_efuse_map_read(Adapter, 0, mapLen, data) == _SUCCESS)
			*poid_par_priv->bytes_rw = mapLen;
		else {
			status = NDIS_STATUS_FAILURE;
		}
	} else {
		/* SET_OID */

		if (rtw_efuse_map_write(Adapter, 0, mapLen, data) == _SUCCESS)
			*poid_par_priv->bytes_rw = mapLen;
		else {
			status = NDIS_STATUS_FAILURE;
		}
	}

	_irqlevel_changed_(&oldirql, RAISE);



	return status;
}

NDIS_STATUS oid_rt_set_crystal_cap_hdl(struct oid_par_priv *poid_par_priv)
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif

	u32		crystal_cap = 0;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	crystal_cap = *((u32 *)poid_par_priv->information_buf); /* 4 */
	if (crystal_cap > 0xf)
		return NDIS_STATUS_NOT_ACCEPTED;

	Adapter->mppriv.curr_crystalcap = crystal_cap;

	_irqlevel_changed_(&oldirql, LOWER);
	SetCrystalCap(Adapter);
	_irqlevel_changed_(&oldirql, RAISE);


#endif
	return status;
}

NDIS_STATUS oid_rt_set_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u8		rx_pkt_type;
	/*	u32		rcr_val32; */
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	/*	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context); */



	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u8))
		return NDIS_STATUS_INVALID_LENGTH;

	rx_pkt_type = *((u8 *)poid_par_priv->information_buf); /* 4 */

#if 0
	_irqlevel_changed_(&oldirql, LOWER);
#if 0
	rcr_val8 = rtw_read8(Adapter, 0x10250048);/* RCR */
	rcr_val8 &= ~(RCR_AB | RCR_AM | RCR_APM | RCR_AAP);

	if (rx_pkt_type == RX_PKT_BROADCAST)
		rcr_val8 |= (RCR_AB | RCR_ACRC32);
	else if (rx_pkt_type == RX_PKT_DEST_ADDR)
		rcr_val8 |= (RCR_AAP | RCR_AM | RCR_ACRC32);
	else if (rx_pkt_type == RX_PKT_PHY_MATCH)
		rcr_val8 |= (RCR_APM | RCR_ACRC32);
	else
		rcr_val8 &= ~(RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_ACRC32);
	rtw_write8(padapter, 0x10250048, rcr_val8);
#else
	rcr_val32 = rtw_read32(padapter, RCR);/* RCR = 0x10250048 */
	rcr_val32 &= ~(RCR_CBSSID | RCR_AB | RCR_AM | RCR_APM | RCR_AAP);
#if 0
	if (rx_pkt_type == RX_PKT_BROADCAST)
		rcr_val32 |= (RCR_AB | RCR_AM | RCR_APM | RCR_AAP | RCR_ACRC32);
	else if (rx_pkt_type == RX_PKT_DEST_ADDR) {
		/* rcr_val32 |= (RCR_CBSSID|RCR_AAP|RCR_AM|RCR_ACRC32); */
		rcr_val32 |= (RCR_CBSSID | RCR_APM | RCR_ACRC32);
	} else if (rx_pkt_type == RX_PKT_PHY_MATCH) {
		rcr_val32 |= (RCR_APM | RCR_ACRC32);
		/* rcr_val32 |= (RCR_AAP|RCR_ACRC32); */
	} else
		rcr_val32 &= ~(RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_ACRC32);
#else
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
		rcr_val32 &= ~(RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_ACRC32);
		break;
	}

	if (rx_pkt_type == RX_PKT_DEST_ADDR)
		padapter->mppriv.check_mp_pkt = 1;
	else
		padapter->mppriv.check_mp_pkt = 0;
#endif
	rtw_write32(padapter, RCR, rcr_val32);

#endif
	_irqlevel_changed_(&oldirql, RAISE);
#endif

	return status;
}

NDIS_STATUS oid_rt_pro_set_tx_agc_offset_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);

#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	u32		txagc;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	txagc = *(u32 *)poid_par_priv->information_buf;

	_irqlevel_changed_(&oldirql, LOWER);
	SetTxAGCOffset(Adapter, txagc);
	_irqlevel_changed_(&oldirql, RAISE);


	return status;
#else
	return 0;
#endif
}

NDIS_STATUS oid_rt_pro_set_pkt_test_mode_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	PADAPTER		Adapter = (PADAPTER)(poid_par_priv->adapter_context);

	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;

	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct mp_priv		*pmppriv = &Adapter->mppriv;
	u32			type;


	if (poid_par_priv->type_of_oid != SET_OID)
		return NDIS_STATUS_NOT_ACCEPTED;

	if (poid_par_priv->information_buf_len < sizeof(u32))
		return NDIS_STATUS_INVALID_LENGTH;

	type = *(u32 *)poid_par_priv->information_buf;

	if (_LOOPBOOK_MODE_ == type) {
		pmppriv->mode = type;
		set_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE); /* append txdesc */
	} else if (_2MAC_MODE_ == type) {
		pmppriv->mode = type;
		_clr_fwstate_(pmlmepriv, WIFI_MP_LPBK_STATE);
	} else
		status = NDIS_STATUS_NOT_ACCEPTED;


	return status;
#else
	return 0;
#endif
}

unsigned int mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv)
{
	PMP_XMIT_PARM pparm;
	PADAPTER padapter;
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;


	pparm = (PMP_XMIT_PARM)poid_par_priv->information_buf;
	padapter = (PADAPTER)poid_par_priv->adapter_context;
	pmp_priv = &padapter->mppriv;

	if (poid_par_priv->type_of_oid == QUERY_OID) {
		pparm->enable = !pmp_priv->tx.stop;
		pparm->count = pmp_priv->tx.sended;
	} else {
		if (pparm->enable == 0)
			pmp_priv->tx.stop = 1;
		else if (pmp_priv->tx.stop == 1) {
			pmp_priv->tx.stop = 0;
			pmp_priv->tx.count = pparm->count;
			pmp_priv->tx.payload = pparm->payload_type;
			pattrib = &pmp_priv->tx.attrib;
			pattrib->pktlen = pparm->length;
			_rtw_memcpy(pattrib->dst, pparm->da, ETH_ALEN);
			SetPacketTx(padapter);
		} else
			return NDIS_STATUS_FAILURE;
	}

	return NDIS_STATUS_SUCCESS;
}

#if 0
unsigned int mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv)
{
	unsigned char *pframe, *pmp_pkt;
	struct ethhdr *pethhdr;
	struct pkt_attrib *pattrib;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	int llc_sz, payload_len;
	struct mp_xmit_frame *pxframe =  NULL;
	struct mp_xmit_packet *pmp_xmitpkt = (struct mp_xmit_packet *)param;
	u8 addr3[] = {0x02, 0xE0, 0x4C, 0x87, 0x66, 0x55};

	/*	RTW_INFO("+mp_ioctl_xmit_packet_hdl\n"); */

	pxframe = alloc_mp_xmitframe(&padapter->mppriv);
	if (pxframe == NULL) {
		DEBUG_ERR(("Can't alloc pmpframe %d:%s\n", __LINE__, __FILE__));
		return -1;
	}

	/* mp_xmit_pkt */
	payload_len = pmp_xmitpkt->len - 14;
	pmp_pkt = (unsigned char *)pmp_xmitpkt->mem;
	pethhdr = (struct ethhdr *)pmp_pkt;

	/* RTW_INFO("payload_len=%d, pkt_mem=0x%x\n", pmp_xmitpkt->len, (void*)pmp_xmitpkt->mem); */

	/* RTW_INFO("pxframe=0x%x\n", (void*)pxframe); */
	/* RTW_INFO("pxframe->mem=0x%x\n", (void*)pxframe->mem); */

	/* update attribute */
	pattrib = &pxframe->attrib;
	memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib));
	pattrib->pktlen = pmp_xmitpkt->len;
	pattrib->ether_type = ntohs(pethhdr->h_proto);
	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 0;
#ifndef CONFIG_MP_LINUX
	if (IS_MCAST(pethhdr->h_dest))
		pattrib->mac_id = 4;
	else
		pattrib->mac_id = 5;
#else
	pattrib->mac_id = 5;
#endif

	/*  */
	memset(pxframe->mem, 0 , WLANHDR_OFFSET);
	pframe = (u8 *)(pxframe->mem) + WLANHDR_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	set_frame_sub_type(pframe, WIFI_DATA);

	_rtw_memcpy(pwlanhdr->addr1, pethhdr->h_dest, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pethhdr->h_source, ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr3, addr3, ETH_ALEN);

	pwlanhdr->seq_ctl = 0;
	pframe += pattrib->hdrlen;

	llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
	pframe += llc_sz;

	_rtw_memcpy(pframe, (void *)(pmp_pkt + 14),  payload_len);

	pattrib->last_txcmdsz = pattrib->hdrlen + llc_sz + payload_len;

	DEBUG_INFO(("issuing mp_xmit_frame, tx_len=%d, ether_type=0x%x\n", pattrib->last_txcmdsz, pattrib->ether_type));
	xmit_mp_frame(padapter, pxframe);

	return _SUCCESS;
}
#endif
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_set_power_down_hdl(struct oid_par_priv *poid_par_priv)
{
#ifdef PLATFORM_OS_XP
	_irqL		oldirql;
#endif
	u8		bpwrup;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
#ifdef PLATFORM_LINUX
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	PADAPTER	padapter = (PADAPTER)(poid_par_priv->adapter_context);
#endif
#endif


	if (poid_par_priv->type_of_oid != SET_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}


	_irqlevel_changed_(&oldirql, LOWER);

	bpwrup = *(u8 *)poid_par_priv->information_buf;
	/* CALL  the power_down function */
#ifdef PLATFORM_LINUX
#if defined(CONFIG_RTL8712) /* Linux MP insmod unknown symbol */
	dev_power_down(padapter, bpwrup);
#endif
#endif
	_irqlevel_changed_(&oldirql, RAISE);

	/* DEBUG_ERR(("\n <=== Query OID_RT_PRO_READ_REGISTER. */
	/*	Add:0x%08x Width:%d Value:0x%08x\n",RegRWStruct->offset,RegRWStruct->width,RegRWStruct->value)); */


	return status;
}
/* ------------------------------------------------------------------------------ */
NDIS_STATUS oid_rt_get_power_mode_hdl(struct oid_par_priv *poid_par_priv)
{
#if 0
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	PADAPTER	Adapter = (PADAPTER)(poid_par_priv->adapter_context);
	/* #ifdef PLATFORM_OS_XP */
	/*	_irqL		oldirql;
	 * #endif */


	if (poid_par_priv->type_of_oid != QUERY_OID) {
		status = NDIS_STATUS_NOT_ACCEPTED;
		return status;
	}
	if (poid_par_priv->information_buf_len < sizeof(u32)) {
		status = NDIS_STATUS_INVALID_LENGTH;
		return status;
	}


	/*	_irqlevel_changed_(&oldirql, LOWER); */
	*(int *)poid_par_priv->information_buf = Adapter->registrypriv.low_power ? POWER_LOW : POWER_NORMAL;
	*poid_par_priv->bytes_rw = poid_par_priv->information_buf_len;
	/*	_irqlevel_changed_(&oldirql, RAISE); */


	return status;
#else
	return 0;
#endif
}
