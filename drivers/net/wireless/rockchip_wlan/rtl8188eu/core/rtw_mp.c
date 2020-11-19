/* SPDX-License-Identifier: GPL-2.0 */
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
#define _RTW_MP_C_
#include <drv_types.h>
#ifdef PLATFORM_FREEBSD
	#include <sys/unistd.h>		/* for RFHIGHPID */
#endif

#include "../hal/phydm/phydm_precomp.h"
#if defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8821A)
	#include <rtw_bt_mp.h>
#endif

#ifdef CONFIG_MP_VHT_HW_TX_MODE
#define CEILING_POS(X) ((X - (int)(X)) > 0 ? (int)(X + 1) : (int)(X))
#define CEILING_NEG(X) ((X - (int)(X)) < 0 ? (int)(X - 1) : (int)(X))
#define ceil(X) (((X) > 0) ? CEILING_POS(X) : CEILING_NEG(X))

int rtfloor(float x)
{
	int i = x - 2;
	while
	(++i <= x - 1)
		;
	return i;
}
#endif

#ifdef CONFIG_MP_INCLUDED
u32 read_macreg(_adapter *padapter, u32 addr, u32 sz)
{
	u32 val = 0;

	switch (sz) {
	case 1:
		val = rtw_read8(padapter, addr);
		break;
	case 2:
		val = rtw_read16(padapter, addr);
		break;
	case 4:
		val = rtw_read32(padapter, addr);
		break;
	default:
		val = 0xffffffff;
		break;
	}

	return val;

}

void write_macreg(_adapter *padapter, u32 addr, u32 val, u32 sz)
{
	switch (sz) {
	case 1:
		rtw_write8(padapter, addr, (u8)val);
		break;
	case 2:
		rtw_write16(padapter, addr, (u16)val);
		break;
	case 4:
		rtw_write32(padapter, addr, val);
		break;
	default:
		break;
	}

}

u32 read_bbreg(_adapter *padapter, u32 addr, u32 bitmask)
{
	return rtw_hal_read_bbreg(padapter, addr, bitmask);
}

void write_bbreg(_adapter *padapter, u32 addr, u32 bitmask, u32 val)
{
	rtw_hal_write_bbreg(padapter, addr, bitmask, val);
}

u32 _read_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 bitmask)
{
	return rtw_hal_read_rfreg(padapter, rfpath, addr, bitmask);
}

void _write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 bitmask, u32 val)
{
	rtw_hal_write_rfreg(padapter, rfpath, addr, bitmask, val);
}

u32 read_rfreg(PADAPTER padapter, u8 rfpath, u32 addr)
{
	return _read_rfreg(padapter, rfpath, addr, bRFRegOffsetMask);
}

void write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 val)
{
	_write_rfreg(padapter, rfpath, addr, bRFRegOffsetMask, val);
}

static void _init_mp_priv_(struct mp_priv *pmp_priv)
{
	WLAN_BSSID_EX *pnetwork;

	_rtw_memset(pmp_priv, 0, sizeof(struct mp_priv));

	pmp_priv->mode = MP_OFF;

	pmp_priv->channel = 1;
	pmp_priv->bandwidth = CHANNEL_WIDTH_20;
	pmp_priv->prime_channel_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pmp_priv->rateidx = RATE_1M;
	pmp_priv->txpoweridx = 0x2A;

	pmp_priv->antenna_tx = ANTENNA_A;
	pmp_priv->antenna_rx = ANTENNA_AB;

	pmp_priv->check_mp_pkt = 0;

	pmp_priv->tx_pktcount = 0;

	pmp_priv->rx_bssidpktcount = 0;
	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_crcerrpktcount = 0;

	pmp_priv->network_macaddr[0] = 0x00;
	pmp_priv->network_macaddr[1] = 0xE0;
	pmp_priv->network_macaddr[2] = 0x4C;
	pmp_priv->network_macaddr[3] = 0x87;
	pmp_priv->network_macaddr[4] = 0x66;
	pmp_priv->network_macaddr[5] = 0x55;

	pmp_priv->bSetRxBssid = _FALSE;
	pmp_priv->bRTWSmbCfg = _FALSE;
	pmp_priv->bloopback = _FALSE;

	pmp_priv->bloadefusemap = _FALSE;
	pmp_priv->brx_filter_beacon = _FALSE;
	pmp_priv->mplink_brx = _FALSE;

	pnetwork = &pmp_priv->mp_network.network;
	_rtw_memcpy(pnetwork->MacAddress, pmp_priv->network_macaddr, ETH_ALEN);

	pnetwork->Ssid.SsidLength = 8;
	_rtw_memcpy(pnetwork->Ssid.Ssid, "mp_871x", pnetwork->Ssid.SsidLength);

	pmp_priv->tx.payload = 2;
#ifdef CONFIG_80211N_HT
	pmp_priv->tx.attrib.ht_en = 1;
#endif

	pmp_priv->mpt_ctx.mpt_rate_index = 1;

}


static void mp_init_xmit_attrib(struct mp_tx *pmptx, PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	struct pkt_attrib *pattrib;

	/* init xmitframe attribute */
	pattrib = &pmptx->attrib;
	_rtw_memset(pattrib, 0, sizeof(struct pkt_attrib));
	_rtw_memset(pmptx->desc, 0, TXDESC_SIZE);

	pattrib->ether_type = 0x8712;
#if 0
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
#endif
	_rtw_memset(pattrib->dst, 0xFF, ETH_ALEN);

	/*	pattrib->dhcp_pkt = 0;
	 *	pattrib->pktlen = 0; */
	pattrib->ack_policy = 0;
	/*	pattrib->pkt_hdrlen = ETH_HLEN; */
	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA;
	pattrib->priority = 0;
	pattrib->qsel = pattrib->priority;
	/*	do_queue_select(padapter, pattrib); */
	pattrib->nr_frags = 1;
	pattrib->encrypt = 0;
	pattrib->bswenc = _FALSE;
	pattrib->qos_en = _FALSE;

	pattrib->pktlen = 1500;

	if (pHalData->rf_type == RF_2T2R)
		pattrib->raid = RATEID_IDX_BGN_40M_2SS;
	else
		pattrib->raid = RATEID_IDX_BGN_40M_1SS;

#ifdef CONFIG_80211AC_VHT
	if (pHalData->rf_type == RF_1T1R)
		pattrib->raid = RATEID_IDX_VHT_1SS;
	else if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_2T4R)
		pattrib->raid = RATEID_IDX_VHT_2SS;
	else if (pHalData->rf_type == RF_3T3R)
		pattrib->raid = RATEID_IDX_VHT_3SS;
	else
		pattrib->raid = RATEID_IDX_BGN_40M_1SS;
#endif
}

s32 init_mp_priv(PADAPTER padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	PHAL_DATA_TYPE pHalData;

	pHalData = GET_HAL_DATA(padapter);

	_init_mp_priv_(pmppriv);
	pmppriv->papdater = padapter;
	if (IS_HARDWARE_TYPE_8822C(padapter))
		pmppriv->mp_dm = 1;/* default enable dpk tracking */
	else
		pmppriv->mp_dm = 0;

	pmppriv->tx.stop = 1;
	pmppriv->bSetTxPower = 0;		/*for  manually set tx power*/
	pmppriv->bTxBufCkFail = _FALSE;
	pmppriv->pktInterval = 0;
	pmppriv->pktLength = 1000;
	pmppriv->bprocess_mp_mode = _FALSE;

	mp_init_xmit_attrib(&pmppriv->tx, padapter);

	switch (padapter->registrypriv.rf_config) {
	case RF_1T1R:
		pmppriv->antenna_tx = ANTENNA_A;
		pmppriv->antenna_rx = ANTENNA_A;
		break;
	case RF_1T2R:
	default:
		pmppriv->antenna_tx = ANTENNA_A;
		pmppriv->antenna_rx = ANTENNA_AB;
		break;
	case RF_2T2R:
		pmppriv->antenna_tx = ANTENNA_AB;
		pmppriv->antenna_rx = ANTENNA_AB;
		break;
	case RF_2T4R:
		pmppriv->antenna_tx = ANTENNA_BC;
		pmppriv->antenna_rx = ANTENNA_ABCD;
		break;
	}

	pHalData->AntennaRxPath = pmppriv->antenna_rx;
	pHalData->antenna_tx_path = pmppriv->antenna_tx;

	return _SUCCESS;
}

void free_mp_priv(struct mp_priv *pmp_priv)
{
	if (pmp_priv->pallocated_mp_xmitframe_buf) {
		rtw_mfree(pmp_priv->pallocated_mp_xmitframe_buf, 0);
		pmp_priv->pallocated_mp_xmitframe_buf = NULL;
	}
	pmp_priv->pmp_xmtframe_buf = NULL;
}

#if 0
static void PHY_IQCalibrate_default(
		PADAPTER	pAdapter,
		BOOLEAN	bReCovery
)
{
	RTW_INFO("%s\n", __func__);
}

static void PHY_LCCalibrate_default(
		PADAPTER	pAdapter
)
{
	RTW_INFO("%s\n", __func__);
}

static void PHY_SetRFPathSwitch_default(
		PADAPTER	pAdapter,
		BOOLEAN		bMain
)
{
	RTW_INFO("%s\n", __func__);
}
#endif

void mpt_InitHWConfig(PADAPTER Adapter)
{
	PHAL_DATA_TYPE hal;

	hal = GET_HAL_DATA(Adapter);

	if (IS_HARDWARE_TYPE_8723B(Adapter)) {
		/* TODO: <20130114, Kordan> The following setting is only for DPDT and Fixed board type. */
		/* TODO:  A better solution is configure it according EFUSE during the run-time. */

		phy_set_mac_reg(Adapter, 0x64, BIT20, 0x0);		/* 0x66[4]=0		 */
		phy_set_mac_reg(Adapter, 0x64, BIT24, 0x0);		/* 0x66[8]=0 */
		phy_set_mac_reg(Adapter, 0x40, BIT4, 0x0);		/* 0x40[4]=0		 */
		phy_set_mac_reg(Adapter, 0x40, BIT3, 0x1);		/* 0x40[3]=1		 */
		phy_set_mac_reg(Adapter, 0x4C, BIT24, 0x1);		/* 0x4C[24:23]=10 */
		phy_set_mac_reg(Adapter, 0x4C, BIT23, 0x0);		/* 0x4C[24:23]=10 */
		phy_set_bb_reg(Adapter, 0x944, BIT1 | BIT0, 0x3);	/* 0x944[1:0]=11	 */
		phy_set_bb_reg(Adapter, 0x930, bMaskByte0, 0x77);/* 0x930[7:0]=77	  */
		phy_set_mac_reg(Adapter, 0x38, BIT11, 0x1);/* 0x38[11]=1 */

		/* TODO: <20130206, Kordan> The default setting is wrong, hard-coded here. */
		phy_set_mac_reg(Adapter, 0x778, 0x3, 0x3);					/* Turn off hardware PTA control (Asked by Scott) */
		phy_set_mac_reg(Adapter, 0x64, bMaskDWord, 0x36000000);/* Fix BT S0/S1 */
		phy_set_mac_reg(Adapter, 0x948, bMaskDWord, 0x0);		/* Fix BT can't Tx */

		/* <20130522, Kordan> Turn off equalizer to improve Rx sensitivity. (Asked by EEChou) */
		phy_set_bb_reg(Adapter, 0xA00, BIT8, 0x0);			/*0xA01[0] = 0*/
	} else if (IS_HARDWARE_TYPE_8821(Adapter)) {
		/* <20131121, VincentL> Add for 8821AU DPDT setting and fix switching antenna issue (Asked by Rock)
		<20131122, VincentL> Enable for all 8821A/8811AU  (Asked by Alex)*/
		phy_set_mac_reg(Adapter, 0x4C, BIT23, 0x0);		/*0x4C[23:22]=01*/
		phy_set_mac_reg(Adapter, 0x4C, BIT22, 0x1);		/*0x4C[23:22]=01*/
	} else if (IS_HARDWARE_TYPE_8188ES(Adapter))
		phy_set_mac_reg(Adapter, 0x4C , BIT23, 0);		/*select DPDT_P and DPDT_N as output pin*/
#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(Adapter))
		PlatformEFIOWrite2Byte(Adapter, REG_RXFLTMAP1_8814A, 0x2000);
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(Adapter)) {
		rtw_write32(Adapter, 0x520, rtw_read32(Adapter, 0x520) | 0x8000);
		rtw_write32(Adapter, 0x524, rtw_read32(Adapter, 0x524) & (~0x800));
	}
#endif


#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(Adapter)) {
		u32 tmp_reg = 0;

		PlatformEFIOWrite2Byte(Adapter, REG_RXFLTMAP1_8822B, 0x2000);
		/* fixed wifi can't 2.4g tx suggest by Szuyitasi 20160504 */
		phy_set_bb_reg(Adapter, 0x70, bMaskByte3, 0x0e);
		RTW_INFO(" 0x73 = 0x%x\n", phy_query_bb_reg(Adapter, 0x70, bMaskByte3));
		phy_set_bb_reg(Adapter, 0x1704, bMaskDWord, 0x0000ff00);
		RTW_INFO(" 0x1704 = 0x%x\n", phy_query_bb_reg(Adapter, 0x1704, bMaskDWord));
		phy_set_bb_reg(Adapter, 0x1700, bMaskDWord, 0xc00f0038);
		RTW_INFO(" 0x1700 = 0x%x\n", phy_query_bb_reg(Adapter, 0x1700, bMaskDWord));
	}
#endif /* CONFIG_RTL8822B */
#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(Adapter))
		PlatformEFIOWrite2Byte(Adapter, REG_RXFLTMAP1_8821C, 0x2000);
#endif /* CONFIG_RTL8821C */
#if defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV)
	else if (IS_HARDWARE_TYPE_8188F(Adapter) || IS_HARDWARE_TYPE_8188GTV(Adapter)) {
		if (IS_A_CUT(hal->version_id) || IS_B_CUT(hal->version_id)) {
			RTW_INFO("%s() Active large power detection\n", __func__);
			phy_active_large_power_detection_8188f(&(GET_HAL_DATA(Adapter)->odmpriv));
		}
	}
#endif
#if defined(CONFIG_RTL8822C)
	else if( IS_HARDWARE_TYPE_8822C(Adapter)) {
		rtw_write16(Adapter, REG_RXFLTMAP1_8822C, 0x2000);
	}
#endif

}

static void PHY_IQCalibrate(PADAPTER padapter, u8 bReCovery)
{
	halrf_iqk_trigger(&(GET_HAL_DATA(padapter)->odmpriv), bReCovery);
}

static void PHY_LCCalibrate(PADAPTER padapter)
{
	halrf_lck_trigger(&(GET_HAL_DATA(padapter)->odmpriv));
}

static u8 PHY_QueryRFPathSwitch(PADAPTER padapter)
{
	u8 bmain = 0;
/*
	if (IS_HARDWARE_TYPE_8723B(padapter)) {
#ifdef CONFIG_RTL8723B
		bmain = PHY_QueryRFPathSwitch_8723B(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8188E(padapter)) {
#ifdef CONFIG_RTL8188E
		bmain = PHY_QueryRFPathSwitch_8188E(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8814A(padapter)) {
#ifdef CONFIG_RTL8814A
		bmain = PHY_QueryRFPathSwitch_8814A(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8812(padapter) || IS_HARDWARE_TYPE_8821(padapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
		bmain = PHY_QueryRFPathSwitch_8812A(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8192E(padapter)) {
#ifdef CONFIG_RTL8192E
		bmain = PHY_QueryRFPathSwitch_8192E(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8703B(padapter)) {
#ifdef CONFIG_RTL8703B
		bmain = PHY_QueryRFPathSwitch_8703B(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8188F(padapter)) {
#ifdef CONFIG_RTL8188F
		bmain = PHY_QueryRFPathSwitch_8188F(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8188GTV(padapter)) {
#ifdef CONFIG_RTL8188GTV
		bmain = PHY_QueryRFPathSwitch_8188GTV(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8822B(padapter)) {
#ifdef CONFIG_RTL8822B
		bmain = PHY_QueryRFPathSwitch_8822B(padapter);
#endif
	} else if (IS_HARDWARE_TYPE_8723D(padapter)) {
#ifdef CONFIG_RTL8723D
		bmain = PHY_QueryRFPathSwitch_8723D(padapter);
#endif
	} else
*/

	if (IS_HARDWARE_TYPE_8821C(padapter)) {
#ifdef CONFIG_RTL8821C
		bmain = phy_query_rf_path_switch_8821c(padapter);
#endif
	}

	return bmain;
}

static void  PHY_SetRFPathSwitch(PADAPTER padapter , BOOLEAN bMain) {

	PHAL_DATA_TYPE hal = GET_HAL_DATA(padapter);
	struct dm_struct *phydm = &hal->odmpriv;

	if (IS_HARDWARE_TYPE_8723B(padapter)) {
#ifdef CONFIG_RTL8723B
		phy_set_rf_path_switch_8723b(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8188E(padapter)) {
#ifdef CONFIG_RTL8188E
		phy_set_rf_path_switch_8188e(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8814A(padapter)) {
#ifdef CONFIG_RTL8814A
		phy_set_rf_path_switch_8814a(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8812(padapter) || IS_HARDWARE_TYPE_8821(padapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
		phy_set_rf_path_switch_8812a(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8192E(padapter)) {
#ifdef CONFIG_RTL8192E
		phy_set_rf_path_switch_8192e(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8703B(padapter)) {
#ifdef CONFIG_RTL8703B
		phy_set_rf_path_switch_8703b(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8188F(padapter) || IS_HARDWARE_TYPE_8188GTV(padapter)) {
#if defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV)
		phy_set_rf_path_switch_8188f(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8192F(padapter)) {
#ifdef CONFIG_RTL8192F
		phy_set_rf_path_switch_8192f(padapter, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8822B(padapter)) {
#ifdef CONFIG_RTL8822B
		phy_set_rf_path_switch_8822b(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8723D(padapter)) {
#ifdef CONFIG_RTL8723D
		phy_set_rf_path_switch_8723d(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8821C(padapter)) {
#ifdef CONFIG_RTL8821C
		phy_set_rf_path_switch_8821c(phydm, bMain);
#endif
	} else if (IS_HARDWARE_TYPE_8822C(padapter)) {
#ifdef CONFIG_RTL8822C
		/* remove for MP EVM Fail, need to review by willis 20180809
		phy_set_rf_path_switch_8822c(phydm, bMain);
		*/
#endif
	}
}


static void phy_switch_rf_path_set(PADAPTER padapter , u8 *prf_set_State) {
#ifdef CONFIG_RTL8821C
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct dm_struct *p_dm = &pHalData->odmpriv;

	if (IS_HARDWARE_TYPE_8821C(padapter)) {
		config_phydm_set_ant_path(p_dm, *prf_set_State, p_dm->current_ant_num_8821c);
		/* Do IQK when switching to BTG/WLG, requested by RF Binson */
		if (*prf_set_State == SWITCH_TO_BTG || *prf_set_State == SWITCH_TO_WLG)
			PHY_IQCalibrate(padapter, FALSE);
	}
#endif

}


#ifdef CONFIG_ANTENNA_DIVERSITY
u8 rtw_mp_set_antdiv(PADAPTER padapter, BOOLEAN bMain)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 cur_ant, change_ant;

	if (!pHalData->AntDivCfg)
		return _FALSE;
	/*rtw_hal_get_odm_var(padapter, HAL_ODM_ANTDIV_SELECT, &cur_ant, NULL);*/
	change_ant = (bMain == MAIN_ANT) ? MAIN_ANT : AUX_ANT;

	RTW_INFO("%s: config %s\n", __func__, (bMain == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
	rtw_antenna_select_cmd(padapter, change_ant, _FALSE);

	return _TRUE;
}
#endif

s32
MPT_InitializeAdapter(
		PADAPTER			pAdapter,
		u8				Channel
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	s32		rtStatus = _SUCCESS;
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.mpt_ctx;
	u32		ledsetting;

	pMptCtx->bMptDrvUnload = _FALSE;
	pMptCtx->bMassProdTest = _FALSE;
	pMptCtx->bMptIndexEven = _TRUE;	/* default gain index is -6.0db */
	pMptCtx->h2cReqNum = 0x0;
	/* init for BT MP */
#if defined(CONFIG_RTL8723B)
	pMptCtx->bMPh2c_timeout = _FALSE;
	pMptCtx->MptH2cRspEvent = _FALSE;
	pMptCtx->MptBtC2hEvent = _FALSE;
	_rtw_init_sema(&pMptCtx->MPh2c_Sema, 0);
	rtw_init_timer(&pMptCtx->MPh2c_timeout_timer, pAdapter, MPh2c_timeout_handle, pAdapter);
#endif

	mpt_InitHWConfig(pAdapter);

#ifdef CONFIG_RTL8723B
	rtl8723b_InitAntenna_Selection(pAdapter);
	if (IS_HARDWARE_TYPE_8723B(pAdapter)) {

		/* <20130522, Kordan> Turn off equalizer to improve Rx sensitivity. (Asked by EEChou)*/
		phy_set_bb_reg(pAdapter, 0xA00, BIT8, 0x0);
		PHY_SetRFPathSwitch(pAdapter, 1/*pHalData->bDefaultAntenna*/); /*default use Main*/

		if (pHalData->PackageType == PACKAGE_DEFAULT)
			phy_set_rf_reg(pAdapter, RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B04E);
		else
			phy_set_rf_reg(pAdapter, RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6F10E);

	}
	/*set ant to wifi side in mp mode*/
	rtw_write16(pAdapter, 0x870, 0x300);
	rtw_write16(pAdapter, 0x860, 0x110);
#endif

	pMptCtx->bMptWorkItemInProgress = _FALSE;
	pMptCtx->CurrMptAct = NULL;
	pMptCtx->mpt_rf_path = RF_PATH_A;
	/* ------------------------------------------------------------------------- */
	/* Don't accept any packets */
	rtw_write32(pAdapter, REG_RCR, 0);

	/* ledsetting = rtw_read32(pAdapter, REG_LEDCFG0); */
	/* rtw_write32(pAdapter, REG_LEDCFG0, ledsetting & ~LED0DIS); */

	/* rtw_write32(pAdapter, REG_LEDCFG0, 0x08080); */
	ledsetting = rtw_read32(pAdapter, REG_LEDCFG0);


	PHY_LCCalibrate(pAdapter);
	PHY_IQCalibrate(pAdapter, _FALSE);
	/* dm_check_txpowertracking(&pHalData->odmpriv);	*/ /* trigger thermal meter */

	PHY_SetRFPathSwitch(pAdapter, 1/*pHalData->bDefaultAntenna*/); /* default use Main */

	pMptCtx->backup0xc50 = (u8)phy_query_bb_reg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pMptCtx->backup0xc58 = (u8)phy_query_bb_reg(pAdapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pMptCtx->backup0xc30 = (u8)phy_query_bb_reg(pAdapter, rOFDM0_RxDetector1, bMaskByte0);
	pMptCtx->backup0x52_RF_A = (u8)phy_query_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0);
	pMptCtx->backup0x52_RF_B = (u8)phy_query_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0);
#ifdef CONFIG_RTL8188E
	rtw_write32(pAdapter, REG_MACID_NO_LINK_0, 0x0);
	rtw_write32(pAdapter, REG_MACID_NO_LINK_1, 0x0);
#endif
#ifdef CONFIG_RTL8814A
	if (IS_HARDWARE_TYPE_8814A(pAdapter)) {
		pHalData->BackUp_IG_REG_4_Chnl_Section[0] = (u8)phy_query_bb_reg(pAdapter, rA_IGI_Jaguar, bMaskByte0);
		pHalData->BackUp_IG_REG_4_Chnl_Section[1] = (u8)phy_query_bb_reg(pAdapter, rB_IGI_Jaguar, bMaskByte0);
		pHalData->BackUp_IG_REG_4_Chnl_Section[2] = (u8)phy_query_bb_reg(pAdapter, rC_IGI_Jaguar2, bMaskByte0);
		pHalData->BackUp_IG_REG_4_Chnl_Section[3] = (u8)phy_query_bb_reg(pAdapter, rD_IGI_Jaguar2, bMaskByte0);
	}
#endif
	return	rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:	MPT_DeInitAdapter()
 *
 * Overview:	Extra DeInitialization for Mass Production Test.
 *
 * Input:		PADAPTER	pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/08/2007	MHC		Create Version 0.
 *	05/18/2007	MHC		Add normal driver MPHalt code.
 *
 *---------------------------------------------------------------------------*/
void
MPT_DeInitAdapter(
		PADAPTER	pAdapter
)
{
	PMPT_CONTEXT		pMptCtx = &pAdapter->mppriv.mpt_ctx;

	pMptCtx->bMptDrvUnload = _TRUE;
#if defined(CONFIG_RTL8723B)
	_rtw_free_sema(&(pMptCtx->MPh2c_Sema));
	_cancel_timer_ex(&pMptCtx->MPh2c_timeout_timer);
#endif
#if	defined(CONFIG_RTL8723B)
	phy_set_bb_reg(pAdapter, 0xA01, BIT0, 1); /* /suggestion  by jerry for MP Rx. */
#endif
#if 0 /* for Windows */
	PlatformFreeWorkItem(&(pMptCtx->MptWorkItem));

	while (pMptCtx->bMptWorkItemInProgress) {
		if (NdisWaitEvent(&(pMptCtx->MptWorkItemEvent), 50))
			break;
	}
	NdisFreeSpinLock(&(pMptCtx->MptWorkItemSpinLock));
#endif
}

static u8 mpt_ProStartTest(PADAPTER padapter)
{
	PMPT_CONTEXT pMptCtx = &padapter->mppriv.mpt_ctx;

	pMptCtx->bMassProdTest = _TRUE;
	pMptCtx->is_start_cont_tx = _FALSE;
	pMptCtx->bCckContTx = _FALSE;
	pMptCtx->bOfdmContTx = _FALSE;
	pMptCtx->bSingleCarrier = _FALSE;
	pMptCtx->is_carrier_suppression = _FALSE;
	pMptCtx->is_single_tone = _FALSE;
	pMptCtx->HWTxmode = PACKETS_TX;

	return _SUCCESS;
}

/*
 * General use
 */
s32 SetPowerTracking(PADAPTER padapter, u8 enable)
{

	hal_mpt_SetPowerTracking(padapter, enable);
	return 0;
}

void GetPowerTracking(PADAPTER padapter, u8 *enable)
{
	hal_mpt_GetPowerTracking(padapter, enable);
}

void rtw_mp_trigger_iqk(PADAPTER padapter)
{
	PHY_IQCalibrate(padapter, _FALSE);
}

void rtw_mp_trigger_lck(PADAPTER padapter)
{
	PHY_LCCalibrate(padapter);
}

void rtw_mp_trigger_dpk(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;

	halrf_dpk_trigger(pDM_Odm);
}

static void init_mp_data(PADAPTER padapter)
{
	u8 v8;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;

	/*disable BCN*/
	v8 = rtw_read8(padapter, REG_BCN_CTRL);
	v8 &= ~EN_BCN_FUNCTION;
	rtw_write8(padapter, REG_BCN_CTRL, v8);

	pDM_Odm->rf_calibrate_info.txpowertrack_control = _FALSE;
}

void MPT_PwrCtlDM(PADAPTER padapter, u32 bstart)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	u32	rf_ability;

	if (bstart == 1) {
		RTW_INFO("in MPT_PwrCtlDM start\n");

		rf_ability = ((u32)halrf_cmn_info_get(pDM_Odm, HALRF_CMNINFO_ABILITY)) | HAL_RF_TX_PWR_TRACK;
		halrf_cmn_info_set(pDM_Odm, HALRF_CMNINFO_ABILITY, rf_ability);
		halrf_set_pwr_track(pDM_Odm, true);
		pDM_Odm->rf_calibrate_info.txpowertrack_control = _TRUE;
		padapter->mppriv.mp_dm = 1;

	} else {
		RTW_INFO("in MPT_PwrCtlDM stop\n");
		rf_ability = ((u32)halrf_cmn_info_get(pDM_Odm, HALRF_CMNINFO_ABILITY)) & ~HAL_RF_TX_PWR_TRACK;
		halrf_cmn_info_set(pDM_Odm, HALRF_CMNINFO_ABILITY, rf_ability);
		halrf_set_pwr_track(pDM_Odm, false);
		pDM_Odm->rf_calibrate_info.txpowertrack_control = _FALSE;
		if (IS_HARDWARE_TYPE_8822C(padapter))
			padapter->mppriv.mp_dm = 1; /* default enable dpk tracking */
		else
			padapter->mppriv.mp_dm = 0;
		{
			struct txpwrtrack_cfg c;
			u8	chnl = 0 ;
			_rtw_memset(&c, 0, sizeof(struct txpwrtrack_cfg));
			configure_txpower_track(pDM_Odm, &c);
			odm_clear_txpowertracking_state(pDM_Odm);
			if (*c.odm_tx_pwr_track_set_pwr) {
				if (pDM_Odm->support_ic_type == ODM_RTL8188F)
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, MIX_MODE, RF_PATH_A, chnl);
				else if (pDM_Odm->support_ic_type == ODM_RTL8723D) {
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, BBSWING, RF_PATH_A, chnl);
					SetTxPower(padapter);
				} else if (pDM_Odm->support_ic_type == ODM_RTL8192F) {
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, MIX_MODE, RF_PATH_A, chnl);
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, MIX_MODE, RF_PATH_B, chnl);
				} else {
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, BBSWING, RF_PATH_A, chnl);
					(*c.odm_tx_pwr_track_set_pwr)(pDM_Odm, BBSWING, RF_PATH_B, chnl);
				}
			}
		}
	}

}


u32 mp_join(PADAPTER padapter, u8 mode)
{
	WLAN_BSSID_EX bssid;
	struct sta_info *psta;
	u32 length;
	_irqL irqL;
	s32 res = _SUCCESS;

	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));

	/* 1. initialize a new WLAN_BSSID_EX */
	_rtw_memset(&bssid, 0, sizeof(WLAN_BSSID_EX));
	RTW_INFO("%s ,pmppriv->network_macaddr=%x %x %x %x %x %x\n", __func__,
		pmppriv->network_macaddr[0], pmppriv->network_macaddr[1], pmppriv->network_macaddr[2], pmppriv->network_macaddr[3], pmppriv->network_macaddr[4],
		 pmppriv->network_macaddr[5]);
	_rtw_memcpy(bssid.MacAddress, pmppriv->network_macaddr, ETH_ALEN);

	if (mode == WIFI_FW_ADHOC_STATE) {
		bssid.Ssid.SsidLength = strlen("mp_pseudo_adhoc");
		_rtw_memcpy(bssid.Ssid.Ssid, (u8 *)"mp_pseudo_adhoc", bssid.Ssid.SsidLength);
		bssid.InfrastructureMode = Ndis802_11IBSS;
		bssid.IELength = 0;
		bssid.Configuration.DSConfig = pmppriv->channel;

	} else if (mode == WIFI_FW_STATION_STATE) {
		bssid.Ssid.SsidLength = strlen("mp_pseudo_STATION");
		_rtw_memcpy(bssid.Ssid.Ssid, (u8 *)"mp_pseudo_STATION", bssid.Ssid.SsidLength);
		bssid.InfrastructureMode = Ndis802_11Infrastructure;
		bssid.IELength = 0;
	}

	length = get_WLAN_BSSID_EX_sz(&bssid);
	if (length % 4)
		bssid.Length = ((length >> 2) + 1) << 2; /* round up to multiple of 4 bytes. */
	else
		bssid.Length = length;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		goto end_of_mp_start_test;

	/* init mp_start_test status */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		rtw_disassoc_cmd(padapter, 500, 0);
		rtw_indicate_disconnect(padapter, 0, _FALSE);
		rtw_free_assoc_resources_cmd(padapter, _TRUE, 0);
	}
	pmppriv->prev_fw_state = get_fwstate(pmlmepriv);
	/*pmlmepriv->fw_state = WIFI_MP_STATE;*/
	init_fwstate(pmlmepriv, WIFI_MP_STATE);

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	/* 3 2. create a new psta for mp driver */
	/* clear psta in the cur_network, if any */
	psta = rtw_get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta)
		rtw_free_stainfo(padapter, psta);

	psta = rtw_alloc_stainfo(&padapter->stapriv, bssid.MacAddress);
	if (psta == NULL) {
		/*pmlmepriv->fw_state = pmppriv->prev_fw_state;*/
		init_fwstate(pmlmepriv, pmppriv->prev_fw_state);
		res = _FAIL;
		goto end_of_mp_start_test;
	}
	if (mode == WIFI_FW_ADHOC_STATE)
	set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
	else
		set_fwstate(pmlmepriv, WIFI_STATION_STATE);
	/* 3 3. join psudo AdHoc */
	tgt_network->join_res = 1;
	tgt_network->aid = psta->cmn.aid = 1;

	_rtw_memcpy(&padapter->registrypriv.dev_network, &bssid, length);
	rtw_update_registrypriv_dev_network(padapter);
	_rtw_memcpy(&tgt_network->network, &padapter->registrypriv.dev_network, padapter->registrypriv.dev_network.Length);
	_rtw_memcpy(pnetwork, &padapter->registrypriv.dev_network, padapter->registrypriv.dev_network.Length);

	rtw_indicate_connect(padapter);
	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	set_fwstate(pmlmepriv, _FW_LINKED);

end_of_mp_start_test:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	if (1) { /* (res == _SUCCESS) */
		/* set MSR to WIFI_FW_ADHOC_STATE */
		if (mode == WIFI_FW_ADHOC_STATE) {
			/* set msr to WIFI_FW_ADHOC_STATE */
			pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
			Set_MSR(padapter, (pmlmeinfo->state & 0x3));
			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_ADHOC_STARTED);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		} else {
			Set_MSR(padapter, WIFI_FW_STATION_STATE);

			RTW_INFO("%s , pmppriv->network_macaddr =%x %x %x %x %x %x\n", __func__,
				pmppriv->network_macaddr[0], pmppriv->network_macaddr[1], pmppriv->network_macaddr[2], pmppriv->network_macaddr[3], pmppriv->network_macaddr[4],
				 pmppriv->network_macaddr[5]);

			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmppriv->network_macaddr);
		}
	}

	return res;
}
/* This function initializes the DUT to the MP test mode */
s32 mp_start_test(PADAPTER padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
#ifdef CONFIG_PCI_HCI
	PHAL_DATA_TYPE hal;
#endif
	s32 res = _SUCCESS;

	padapter->registrypriv.mp_mode = 1;

	init_mp_data(padapter);
#ifdef CONFIG_RTL8814A
	rtl8814_InitHalDm(padapter);
#endif /* CONFIG_RTL8814A */
#ifdef CONFIG_RTL8812A
	rtl8812_InitHalDm(padapter);
#endif /* CONFIG_RTL8812A */
#ifdef CONFIG_RTL8723B
	rtl8723b_InitHalDm(padapter);
#endif /* CONFIG_RTL8723B */
#ifdef CONFIG_RTL8703B
	rtl8703b_InitHalDm(padapter);
#endif /* CONFIG_RTL8703B */
#ifdef CONFIG_RTL8192E
	rtl8192e_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8188F
	rtl8188f_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8188GTV
	rtl8188gtv_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8188E
	rtl8188e_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8723D
	rtl8723d_InitHalDm(padapter);
#endif /* CONFIG_RTL8723D */

#ifdef CONFIG_PCI_HCI
	hal = GET_HAL_DATA(padapter);
	hal->pci_backdoor_ctrl = 0;
	rtw_pci_aspm_config(padapter);
#endif


	/* 3 0. update mp_priv */

	if (!RF_TYPE_VALID(padapter->registrypriv.rf_config)) {
		/*		switch (phal->rf_type) { */
		switch (GET_RF_TYPE(padapter)) {
		case RF_1T1R:
			pmppriv->antenna_tx = ANTENNA_A;
			pmppriv->antenna_rx = ANTENNA_A;
			break;
		case RF_1T2R:
		default:
			pmppriv->antenna_tx = ANTENNA_A;
			pmppriv->antenna_rx = ANTENNA_AB;
			break;
		case RF_2T2R:
			pmppriv->antenna_tx = ANTENNA_AB;
			pmppriv->antenna_rx = ANTENNA_AB;
			break;
		case RF_2T4R:
			pmppriv->antenna_tx = ANTENNA_AB;
			pmppriv->antenna_rx = ANTENNA_ABCD;
			break;
		}
	}

	mpt_ProStartTest(padapter);

	mp_join(padapter, WIFI_FW_ADHOC_STATE);

	return res;
}
/* ------------------------------------------------------------------------------
 * This function change the DUT from the MP test mode into normal mode */
void mp_stop_test(PADAPTER padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info *psta;
#ifdef CONFIG_PCI_HCI
	struct registry_priv  *registry_par = &padapter->registrypriv;
	PHAL_DATA_TYPE hal;
#endif

	_irqL irqL;

	if (pmppriv->mode == MP_ON) {
		pmppriv->bSetTxPower = 0;
		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE)
			goto end_of_mp_stop_test;

		/* 3 1. disconnect psudo AdHoc */
		rtw_indicate_disconnect(padapter, 0, _FALSE);

		/* 3 2. clear psta used in mp test mode.
		*	rtw_free_assoc_resources(padapter, _TRUE); */
		psta = rtw_get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
		if (psta)
			rtw_free_stainfo(padapter, psta);

		/* 3 3. return to normal state (default:station mode) */
		/*pmlmepriv->fw_state = pmppriv->prev_fw_state; */ /* WIFI_STATION_STATE;*/
		init_fwstate(pmlmepriv, pmppriv->prev_fw_state);

		/* flush the cur_network */
		_rtw_memset(tgt_network, 0, sizeof(struct wlan_network));

		_clr_fwstate_(pmlmepriv, WIFI_MP_STATE);

end_of_mp_stop_test:

		_exit_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_PCI_HCI
		hal = GET_HAL_DATA(padapter);
		hal->pci_backdoor_ctrl = registry_par->pci_aspm_config;
		rtw_pci_aspm_config(padapter);
#endif

#ifdef CONFIG_RTL8812A
		rtl8812_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8723B
		rtl8723b_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8703B
		rtl8703b_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8192E
		rtl8192e_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8188F
		rtl8188f_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8188GTV
		rtl8188gtv_InitHalDm(padapter);
#endif
#ifdef CONFIG_RTL8723D
		rtl8723d_InitHalDm(padapter);
#endif
	}
}
/*---------------------------hal\rtl8192c\MPT_Phy.c---------------------------*/
#if 0
/* #ifdef CONFIG_USB_HCI */
static void mpt_AdjustRFRegByRateByChan92CU(PADAPTER pAdapter, u8 RateIdx, u8 Channel, u8 BandWidthID)
{
	u8		eRFPath;
	u32		rfReg0x26;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);


	if (RateIdx < MPT_RATE_6M) 	/* CCK rate,for 88cu */
		rfReg0x26 = 0xf400;
	else if ((RateIdx >= MPT_RATE_6M) && (RateIdx <= MPT_RATE_54M)) {/* OFDM rate,for 88cu */
		if ((4 == Channel) || (8 == Channel) || (12 == Channel))
			rfReg0x26 = 0xf000;
		else if ((5 == Channel) || (7 == Channel) || (13 == Channel) || (14 == Channel))
			rfReg0x26 = 0xf400;
		else
			rfReg0x26 = 0x4f200;
	} else if ((RateIdx >= MPT_RATE_MCS0) && (RateIdx <= MPT_RATE_MCS15)) {
		/* MCS 20M ,for 88cu */ /* MCS40M rate,for 88cu */

		if (CHANNEL_WIDTH_20 == BandWidthID) {
			if ((4 == Channel) || (8 == Channel))
				rfReg0x26 = 0xf000;
			else if ((5 == Channel) || (7 == Channel) || (13 == Channel) || (14 == Channel))
				rfReg0x26 = 0xf400;
			else
				rfReg0x26 = 0x4f200;
		} else {
			if ((4 == Channel) || (8 == Channel))
				rfReg0x26 = 0xf000;
			else if ((5 == Channel) || (7 == Channel))
				rfReg0x26 = 0xf400;
			else
				rfReg0x26 = 0x4f200;
		}
	}

	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
		write_rfreg(pAdapter, eRFPath, RF_SYN_G2, rfReg0x26);
}
#endif
/*-----------------------------------------------------------------------------
 * Function:	mpt_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel/rate/BW for MP.
 *
 * Input:       	PADAPTER				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
#if 0
static void mpt_SwitchRfSetting(PADAPTER pAdapter)
{
	hal_mpt_SwitchRfSetting(pAdapter);
}

/*---------------------------hal\rtl8192c\MPT_Phy.c---------------------------*/
/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/
static void MPT_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14)
{
	hal_mpt_CCKTxPowerAdjust(Adapter, bInCH14);
}
#endif

/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/

/*
 * SetChannel
 * Description
 *	Use H2C command to change channel,
 *	not only modify rf register, but also other setting need to be done.
 */
void SetChannel(PADAPTER pAdapter)
{
	hal_mpt_SetChannel(pAdapter);
}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void SetBandwidth(PADAPTER pAdapter)
{
	hal_mpt_SetBandwidth(pAdapter);

}

void SetAntenna(PADAPTER pAdapter)
{
	hal_mpt_SetAntenna(pAdapter);
}

int SetTxPower(PADAPTER pAdapter)
{

	hal_mpt_SetTxPower(pAdapter);
	return _TRUE;
}

void SetTxAGCOffset(PADAPTER pAdapter, u32 ulTxAGCOffset)
{
	u32 TxAGCOffset_B, TxAGCOffset_C, TxAGCOffset_D, tmpAGC;

	TxAGCOffset_B = (ulTxAGCOffset & 0x000000ff);
	TxAGCOffset_C = ((ulTxAGCOffset & 0x0000ff00) >> 8);
	TxAGCOffset_D = ((ulTxAGCOffset & 0x00ff0000) >> 16);

	tmpAGC = (TxAGCOffset_D << 8 | TxAGCOffset_C << 4 | TxAGCOffset_B);
	write_bbreg(pAdapter, rFPGA0_TxGainStage,
		    (bXBTxAGC | bXCTxAGC | bXDTxAGC), tmpAGC);
}

void SetDataRate(PADAPTER pAdapter)
{
	hal_mpt_SetDataRate(pAdapter);
}

void MP_PHY_SetRFPathSwitch(PADAPTER pAdapter , BOOLEAN bMain)
{

	PHY_SetRFPathSwitch(pAdapter, bMain);

}

void mp_phy_switch_rf_path_set(PADAPTER pAdapter , u8 *pstate)
{

	phy_switch_rf_path_set(pAdapter, pstate);

}

u8 MP_PHY_QueryRFPathSwitch(PADAPTER pAdapter)
{
	return PHY_QueryRFPathSwitch(pAdapter);
}

s32 SetThermalMeter(PADAPTER pAdapter, u8 target_ther)
{
	return hal_mpt_SetThermalMeter(pAdapter, target_ther);
}

#if 0
static void TriggerRFThermalMeter(PADAPTER pAdapter)
{
	hal_mpt_TriggerRFThermalMeter(pAdapter);
}

static u8 ReadRFThermalMeter(PADAPTER pAdapter)
{
	return hal_mpt_ReadRFThermalMeter(pAdapter);
}
#endif

void GetThermalMeter(PADAPTER pAdapter, u8 rfpath ,u8 *value)
{
	hal_mpt_GetThermalMeter(pAdapter, rfpath, value);
}

void SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
	PhySetTxPowerLevel(pAdapter);
	hal_mpt_SetSingleCarrierTx(pAdapter, bStart);
}

void SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
	PhySetTxPowerLevel(pAdapter);
	hal_mpt_SetSingleToneTx(pAdapter, bStart);
}

void SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
	PhySetTxPowerLevel(pAdapter);
	hal_mpt_SetCarrierSuppressionTx(pAdapter, bStart);
}

void SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	PhySetTxPowerLevel(pAdapter);
	hal_mpt_SetContinuousTx(pAdapter, bStart);
}


void PhySetTxPowerLevel(PADAPTER pAdapter)
{
	struct mp_priv *pmp_priv = &pAdapter->mppriv;


	if (pmp_priv->bSetTxPower == 0) /* for NO manually set power index */
		rtw_hal_set_tx_power_level(pAdapter, pmp_priv->channel);
}

/* ------------------------------------------------------------------------------ */
static void dump_mpframe(PADAPTER padapter, struct xmit_frame *pmpframe)
{
	rtw_hal_mgnt_xmit(padapter, pmpframe);
}

static struct xmit_frame *alloc_mp_xmitframe(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame	*pmpframe;
	struct xmit_buf	*pxmitbuf;

	pmpframe = rtw_alloc_xmitframe(pxmitpriv);
	if (pmpframe == NULL)
		return NULL;

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL) {
		rtw_free_xmitframe(pxmitpriv, pmpframe);
		return NULL;
	}

	pmpframe->frame_tag = MP_FRAMETAG;

	pmpframe->pxmitbuf = pxmitbuf;

	pmpframe->buf_addr = pxmitbuf->pbuf;

	pxmitbuf->priv_data = pmpframe;

	return pmpframe;

}

#ifdef CONFIG_PCI_HCI
static u8 check_nic_enough_desc(_adapter *padapter, struct pkt_attrib *pattrib)
{
	u32 prio;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct rtw_tx_ring	*ring;

	switch (pattrib->qsel) {
	case 0:
	case 3:
		prio = BE_QUEUE_INX;
		break;
	case 1:
	case 2:
		prio = BK_QUEUE_INX;
		break;
	case 4:
	case 5:
		prio = VI_QUEUE_INX;
		break;
	case 6:
	case 7:
		prio = VO_QUEUE_INX;
		break;
	default:
		prio = BE_QUEUE_INX;
		break;
	}

	ring = &pxmitpriv->tx_ring[prio];

	/*
	 * for now we reserve two free descriptor as a safety boundary
	 * between the tail and the head
	 */
	if ((ring->entries - ring->qlen) >= 2)
		return _TRUE;
	else
		return _FALSE;
}
#endif

static thread_return mp_xmit_packet_thread(thread_context context)
{
	struct xmit_frame	*pxmitframe;
	struct mp_tx		*pmptx;
	struct mp_priv	*pmp_priv;
	struct xmit_priv	*pxmitpriv;
	PADAPTER padapter;

	pmp_priv = (struct mp_priv *)context;
	pmptx = &pmp_priv->tx;
	padapter = pmp_priv->papdater;
	pxmitpriv = &(padapter->xmitpriv);

	thread_enter("RTW_MP_THREAD");

	RTW_INFO("%s:pkTx Start\n", __func__);
	while (1) {
		pxmitframe = alloc_mp_xmitframe(pxmitpriv);
#ifdef CONFIG_PCI_HCI
		if(check_nic_enough_desc(padapter, &pmptx->attrib) == _FALSE) {
			rtw_usleep_os(1000);
			continue;
		}
#endif
		if (pxmitframe == NULL) {
			if (pmptx->stop ||
			    RTW_CANNOT_RUN(padapter))
				goto exit;
			else {
				rtw_usleep_os(10);
				continue;
			}
		}
		_rtw_memcpy((u8 *)(pxmitframe->buf_addr + TXDESC_OFFSET), pmptx->buf, pmptx->write_size);
		_rtw_memcpy(&(pxmitframe->attrib), &(pmptx->attrib), sizeof(struct pkt_attrib));


		rtw_usleep_os(padapter->mppriv.pktInterval);
		dump_mpframe(padapter, pxmitframe);

		pmptx->sended++;
		pmp_priv->tx_pktcount++;

		if (pmptx->stop ||
		    RTW_CANNOT_RUN(padapter))
			goto exit;
		if ((pmptx->count != 0) &&
		    (pmptx->count == pmptx->sended))
			goto exit;

		flush_signals_thread();
	}

exit:
	/* RTW_INFO("%s:pkTx Exit\n", __func__); */
	rtw_mfree(pmptx->pallocated_buf, pmptx->buf_size);
	pmptx->pallocated_buf = NULL;
	pmptx->stop = 1;

	thread_exit(NULL);
	return 0;
}

void fill_txdesc_for_mp(PADAPTER padapter, u8 *ptxdesc)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	_rtw_memcpy(ptxdesc, pmp_priv->tx.desc, TXDESC_SIZE);
}

#if defined(CONFIG_RTL8188E)
void fill_tx_desc_8188e(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct tx_desc *desc   = (struct tx_desc *)&(pmp_priv->tx.desc);
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u32	pkt_size = pattrib->last_txcmdsz;
	s32 bmcast = IS_MCAST(pattrib->ra);
	/* offset 0 */
#if !defined(CONFIG_RTL8188E_SDIO) && !defined(CONFIG_PCI_HCI)
	desc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	desc->txdw0 |= cpu_to_le32(pkt_size & 0x0000FFFF); /* packet size */
	desc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00FF0000); /* 32 bytes for TX Desc */
	if (bmcast)
		desc->txdw0 |= cpu_to_le32(BMC); /* broadcast packet */

	desc->txdw1 |= cpu_to_le32((0x01 << 26) & 0xff000000);
#endif

	desc->txdw1 |= cpu_to_le32((pattrib->mac_id) & 0x3F); /* CAM_ID(MAC_ID) */
	desc->txdw1 |= cpu_to_le32((pattrib->qsel << QSEL_SHT) & 0x00001F00); /* Queue Select, TID */
	desc->txdw1 |= cpu_to_le32((pattrib->raid << RATE_ID_SHT) & 0x000F0000); /* Rate Adaptive ID */
	/* offset 8 */
	/* desc->txdw2 |= cpu_to_le32(AGG_BK); */ /* AGG BK */

	desc->txdw3 |= cpu_to_le32((pattrib->seqnum << 16) & 0x0fff0000);
	desc->txdw4 |= cpu_to_le32(HW_SSN);

	desc->txdw4 |= cpu_to_le32(USERATE);
	desc->txdw4 |= cpu_to_le32(DISDATAFB);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			desc->txdw4 |= cpu_to_le32(DATA_SHORT); /* CCK Short Preamble */
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		desc->txdw4 |= cpu_to_le32(DATA_BW);

	/* offset 20 */
	desc->txdw5 |= cpu_to_le32(pmp_priv->rateidx & 0x0000001F);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) > MPT_RATE_54M)
			desc->txdw5 |= cpu_to_le32(SGI); /* MCS Short Guard Interval */
	}

	desc->txdw5 |= cpu_to_le32(RTY_LMT_EN); /* retry limit enable */
	desc->txdw5 |= cpu_to_le32(0x00180000); /* DATA/RTS Rate Fallback Limit	 */


}
#endif

#if defined(CONFIG_RTL8814A)
void fill_tx_desc_8814a(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	u8 *pDesc   = (u8 *)&(pmp_priv->tx.desc);
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);

	u32	pkt_size = pattrib->last_txcmdsz;
	s32 bmcast = IS_MCAST(pattrib->ra);
	u8 offset;

	/* SET_TX_DESC_FIRST_SEG_8814A(pDesc, 1); */
	SET_TX_DESC_LAST_SEG_8814A(pDesc, 1);
	/* SET_TX_DESC_OWN_(pDesc, 1); */

	SET_TX_DESC_PKT_SIZE_8814A(pDesc, pkt_size);

	offset = TXDESC_SIZE + OFFSET_SZ;

	SET_TX_DESC_OFFSET_8814A(pDesc, offset);
#if defined(CONFIG_PCI_HCI)
	SET_TX_DESC_PKT_OFFSET_8814A(pDesc, 0); /* 8814AE pkt_offset is 0 */
#else
	SET_TX_DESC_PKT_OFFSET_8814A(pDesc, 1);
#endif

	if (bmcast)
		SET_TX_DESC_BMC_8814A(pDesc, 1);

	SET_TX_DESC_MACID_8814A(pDesc, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_8814A(pDesc, pattrib->raid);

	/* SET_TX_DESC_RATE_ID_8812(pDesc, RATEID_IDX_G); */
	SET_TX_DESC_QUEUE_SEL_8814A(pDesc,  pattrib->qsel);
	/* SET_TX_DESC_QUEUE_SEL_8812(pDesc,  QSLT_MGNT); */

	if (pmp_priv->preamble)
		SET_TX_DESC_DATA_SHORT_8814A(pDesc, 1);

	if (!pattrib->qos_en) {
		SET_TX_DESC_HWSEQ_EN_8814A(pDesc, 1); /* Hw set sequence number */
	} else
		SET_TX_DESC_SEQ_8814A(pDesc, pattrib->seqnum);

	if (pmp_priv->bandwidth <= CHANNEL_WIDTH_160)
		SET_TX_DESC_DATA_BW_8814A(pDesc, pmp_priv->bandwidth);
	else {
		RTW_INFO("%s:Err: unknown bandwidth %d, use 20M\n", __func__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_8814A(pDesc, CHANNEL_WIDTH_20);
	}

	SET_TX_DESC_DISABLE_FB_8814A(pDesc, 1);
	SET_TX_DESC_USE_RATE_8814A(pDesc, 1);
	SET_TX_DESC_TX_RATE_8814A(pDesc, pmp_priv->rateidx);

}
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
void fill_tx_desc_8812a(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	u8 *pDesc   = (u8 *)&(pmp_priv->tx.desc);
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);

	u32	pkt_size = pattrib->last_txcmdsz;
	s32 bmcast = IS_MCAST(pattrib->ra);
	u8 data_rate, pwr_status, offset;

	SET_TX_DESC_FIRST_SEG_8812(pDesc, 1);
	SET_TX_DESC_LAST_SEG_8812(pDesc, 1);
	SET_TX_DESC_OWN_8812(pDesc, 1);

	SET_TX_DESC_PKT_SIZE_8812(pDesc, pkt_size);

	offset = TXDESC_SIZE + OFFSET_SZ;

	SET_TX_DESC_OFFSET_8812(pDesc, offset);

#if defined(CONFIG_PCI_HCI)
	SET_TX_DESC_PKT_OFFSET_8812(pDesc, 0);
#else
	SET_TX_DESC_PKT_OFFSET_8812(pDesc, 1);
#endif
	if (bmcast)
		SET_TX_DESC_BMC_8812(pDesc, 1);

	SET_TX_DESC_MACID_8812(pDesc, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_8812(pDesc, pattrib->raid);

	/* SET_TX_DESC_RATE_ID_8812(pDesc, RATEID_IDX_G); */
	SET_TX_DESC_QUEUE_SEL_8812(pDesc,  pattrib->qsel);
	/* SET_TX_DESC_QUEUE_SEL_8812(pDesc,  QSLT_MGNT); */

	if (!pattrib->qos_en) {
		SET_TX_DESC_HWSEQ_EN_8812(pDesc, 1); /* Hw set sequence number */
	} else
		SET_TX_DESC_SEQ_8812(pDesc, pattrib->seqnum);

	if (pmp_priv->bandwidth <= CHANNEL_WIDTH_160)
		SET_TX_DESC_DATA_BW_8812(pDesc, pmp_priv->bandwidth);
	else {
		RTW_INFO("%s:Err: unknown bandwidth %d, use 20M\n", __func__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_8812(pDesc, CHANNEL_WIDTH_20);
	}

	SET_TX_DESC_DISABLE_FB_8812(pDesc, 1);
	SET_TX_DESC_USE_RATE_8812(pDesc, 1);
	SET_TX_DESC_TX_RATE_8812(pDesc, pmp_priv->rateidx);

}
#endif
#if defined(CONFIG_RTL8192E)
void fill_tx_desc_8192e(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	u8 *pDesc	= (u8 *)&(pmp_priv->tx.desc);
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);

	u32 pkt_size = pattrib->last_txcmdsz;
	s32 bmcast = IS_MCAST(pattrib->ra);
	u8 data_rate, pwr_status, offset;


	SET_TX_DESC_PKT_SIZE_92E(pDesc, pkt_size);

	offset = TXDESC_SIZE + OFFSET_SZ;

	SET_TX_DESC_OFFSET_92E(pDesc, offset);
#if defined(CONFIG_PCI_HCI) /* 8192EE */

	SET_TX_DESC_PKT_OFFSET_92E(pDesc, 0); /* 8192EE pkt_offset is 0 */
#else /* 8192EU 8192ES */
	SET_TX_DESC_PKT_OFFSET_92E(pDesc, 1);
#endif

	if (bmcast)
		SET_TX_DESC_BMC_92E(pDesc, 1);

	SET_TX_DESC_MACID_92E(pDesc, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_92E(pDesc, pattrib->raid);


	SET_TX_DESC_QUEUE_SEL_92E(pDesc,  pattrib->qsel);
	/* SET_TX_DESC_QUEUE_SEL_8812(pDesc,  QSLT_MGNT); */

	if (!pattrib->qos_en) {
		SET_TX_DESC_EN_HWSEQ_92E(pDesc, 1);/* Hw set sequence number */
		SET_TX_DESC_HWSEQ_SEL_92E(pDesc, pattrib->hw_ssn_sel);
	} else
		SET_TX_DESC_SEQ_92E(pDesc, pattrib->seqnum);

	if ((pmp_priv->bandwidth == CHANNEL_WIDTH_20) || (pmp_priv->bandwidth == CHANNEL_WIDTH_40))
		SET_TX_DESC_DATA_BW_92E(pDesc, pmp_priv->bandwidth);
	else {
		RTW_INFO("%s:Err: unknown bandwidth %d, use 20M\n", __func__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_92E(pDesc, CHANNEL_WIDTH_20);
	}

	/* SET_TX_DESC_DATA_SC_92E(pDesc, SCMapping_92E(padapter,pattrib)); */

	SET_TX_DESC_DISABLE_FB_92E(pDesc, 1);
	SET_TX_DESC_USE_RATE_92E(pDesc, 1);
	SET_TX_DESC_TX_RATE_92E(pDesc, pmp_priv->rateidx);

}
#endif

#if defined(CONFIG_RTL8723B)
void fill_tx_desc_8723b(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_AGG_BREAK_8723B(ptxdesc, 1);
	SET_TX_DESC_MACID_8723B(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8723B(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8723B(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8723B(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8723B(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8723B(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8723B(ptxdesc, 1);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8723B(ptxdesc, 1);
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8723B(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8723B(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8723B(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8723B(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8703B)
void fill_tx_desc_8703b(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_AGG_BREAK_8703B(ptxdesc, 1);
	SET_TX_DESC_MACID_8703B(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8703B(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8703B(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8703B(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8703B(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8703B(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8703B(ptxdesc, 1);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8703B(ptxdesc, 1);
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8703B(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8703B(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8703B(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8703B(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8188F)
void fill_tx_desc_8188f(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_AGG_BREAK_8188F(ptxdesc, 1);
	SET_TX_DESC_MACID_8188F(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8188F(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8188F(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8188F(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8188F(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8188F(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8188F(ptxdesc, 1);

	if (pmp_priv->preamble)
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8188F(ptxdesc, 1);

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8188F(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8188F(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8188F(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8188F(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8188GTV)
void fill_tx_desc_8188gtv(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_AGG_BREAK_8188GTV(ptxdesc, 1);
	SET_TX_DESC_MACID_8188GTV(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8188GTV(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8188GTV(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8188GTV(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8188GTV(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8188GTV(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8188GTV(ptxdesc, 1);

	if (pmp_priv->preamble)
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8188GTV(ptxdesc, 1);

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8188GTV(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8188GTV(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8188GTV(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8188GTV(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8723D)
void fill_tx_desc_8723d(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_BK_8723D(ptxdesc, 1);
	SET_TX_DESC_MACID_8723D(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8723D(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8723D(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8723D(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8723D(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8723D(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8723D(ptxdesc, 1);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8723D(ptxdesc, 1);
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8723D(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8723D(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8723D(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8723D(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8710B)
void fill_tx_desc_8710b(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_BK_8710B(ptxdesc, 1);
	SET_TX_DESC_MACID_8710B(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8710B(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8710B(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8710B(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8710B(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8710B(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8710B(ptxdesc, 1);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8710B(ptxdesc, 1);
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8710B(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8710B(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8710B(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8710B(ptxdesc, 0xF);
}
#endif

#if defined(CONFIG_RTL8192F)
void fill_tx_desc_8192f(PADAPTER padapter)
{
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct pkt_attrib *pattrib = &(pmp_priv->tx.attrib);
	u8 *ptxdesc = pmp_priv->tx.desc;

	SET_TX_DESC_BK_8192F(ptxdesc, 1);
	SET_TX_DESC_MACID_8192F(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_QUEUE_SEL_8192F(ptxdesc, pattrib->qsel);

	SET_TX_DESC_RATE_ID_8192F(ptxdesc, pattrib->raid);
	SET_TX_DESC_SEQ_8192F(ptxdesc, pattrib->seqnum);
	SET_TX_DESC_HWSEQ_EN_8192F(ptxdesc, 1);
	SET_TX_DESC_USE_RATE_8192F(ptxdesc, 1);
	SET_TX_DESC_DISABLE_FB_8192F(ptxdesc, 1);

	if (pmp_priv->preamble) {
		if (HwRateToMPTRate(pmp_priv->rateidx) <=  MPT_RATE_54M)
			SET_TX_DESC_DATA_SHORT_8192F(ptxdesc, 1);
	}

	if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
		SET_TX_DESC_DATA_BW_8192F(ptxdesc, 1);

	SET_TX_DESC_TX_RATE_8192F(ptxdesc, pmp_priv->rateidx);

	SET_TX_DESC_DATA_RATE_FB_LIMIT_8192F(ptxdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT_8192F(ptxdesc, 0xF);
}

#endif
static void Rtw_MPSetMacTxEDCA(PADAPTER padapter)
{

	rtw_write32(padapter, 0x508 , 0x00a422); /* Disable EDCA BE Txop for MP pkt tx adjust Packet interval */
	/* RTW_INFO("%s:write 0x508~~~~~~ 0x%x\n", __func__,rtw_read32(padapter, 0x508)); */
	phy_set_mac_reg(padapter, 0x458 , bMaskDWord , 0x0);
	/*RTW_INFO("%s()!!!!! 0x460 = 0x%x\n" ,__func__, phy_query_bb_reg(padapter, 0x460, bMaskDWord));*/
	phy_set_mac_reg(padapter, 0x460 , bMaskLWord , 0x0); /* fast EDCA queue packet interval & time out value*/
	/*phy_set_mac_reg(padapter, ODM_EDCA_VO_PARAM ,bMaskLWord , 0x431C);*/
	/*phy_set_mac_reg(padapter, ODM_EDCA_BE_PARAM ,bMaskLWord , 0x431C);*/
	/*phy_set_mac_reg(padapter, ODM_EDCA_BK_PARAM ,bMaskLWord , 0x431C);*/
	RTW_INFO("%s()!!!!! 0x460 = 0x%x\n" , __func__, phy_query_bb_reg(padapter, 0x460, bMaskDWord));

}

void SetPacketTx(PADAPTER padapter)
{
	u8 *ptr, *pkt_start, *pkt_end;
	u32 pkt_size, i;
	struct rtw_ieee80211_hdr *hdr;
	u8 payload;
	s32 bmcast;
	struct pkt_attrib *pattrib;
	struct mp_priv *pmp_priv;

	pmp_priv = &padapter->mppriv;

	if (pmp_priv->tx.stop)
		return;
	pmp_priv->tx.sended = 0;
	pmp_priv->tx.stop = 0;
	pmp_priv->tx_pktcount = 0;

	/* 3 1. update_attrib() */
	pattrib = &pmp_priv->tx.attrib;
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
	bmcast = IS_MCAST(pattrib->ra);
	if (bmcast)
		pattrib->psta = rtw_get_bcmc_stainfo(padapter);
	else
		pattrib->psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));

	pattrib->mac_id = pattrib->psta->cmn.mac_id;
	pattrib->mbssid = 0;

	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->pktlen;

	/* 3 2. allocate xmit buffer */
	pkt_size = pattrib->last_txcmdsz;

	if (pmp_priv->tx.pallocated_buf)
		rtw_mfree(pmp_priv->tx.pallocated_buf, pmp_priv->tx.buf_size);
	pmp_priv->tx.write_size = pkt_size;
	pmp_priv->tx.buf_size = pkt_size + XMITBUF_ALIGN_SZ;
	pmp_priv->tx.pallocated_buf = rtw_zmalloc(pmp_priv->tx.buf_size);
	if (pmp_priv->tx.pallocated_buf == NULL) {
		RTW_INFO("%s: malloc(%d) fail!!\n", __func__, pmp_priv->tx.buf_size);
		return;
	}
	pmp_priv->tx.buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pmp_priv->tx.pallocated_buf), XMITBUF_ALIGN_SZ);
	ptr = pmp_priv->tx.buf;

	_rtw_memset(pmp_priv->tx.desc, 0, TXDESC_SIZE);
	pkt_start = ptr;
	pkt_end = pkt_start + pkt_size;

	/* 3 3. init TX descriptor */
#if defined(CONFIG_RTL8188E)
	if (IS_HARDWARE_TYPE_8188E(padapter))
		fill_tx_desc_8188e(padapter);
#endif

#if defined(CONFIG_RTL8814A)
	if (IS_HARDWARE_TYPE_8814A(padapter))
		fill_tx_desc_8814a(padapter);
#endif /* defined(CONFIG_RTL8814A) */

#if defined(CONFIG_RTL8822B)
	if (IS_HARDWARE_TYPE_8822B(padapter))
		rtl8822b_prepare_mp_txdesc(padapter, pmp_priv);
#endif /* CONFIG_RTL8822B */

#if defined(CONFIG_RTL8822C)
	if (IS_HARDWARE_TYPE_8822C(padapter))
		rtl8822c_prepare_mp_txdesc(padapter, pmp_priv);
#endif /* CONFIG_RTL8822C */

#if defined(CONFIG_RTL8821C)
	if (IS_HARDWARE_TYPE_8821C(padapter))
		rtl8821c_prepare_mp_txdesc(padapter, pmp_priv);
#endif /* CONFIG_RTL8821C */

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	if (IS_HARDWARE_TYPE_8812(padapter) || IS_HARDWARE_TYPE_8821(padapter))
		fill_tx_desc_8812a(padapter);
#endif

#if defined(CONFIG_RTL8192E)
	if (IS_HARDWARE_TYPE_8192E(padapter))
		fill_tx_desc_8192e(padapter);
#endif
#if defined(CONFIG_RTL8723B)
	if (IS_HARDWARE_TYPE_8723B(padapter))
		fill_tx_desc_8723b(padapter);
#endif
#if defined(CONFIG_RTL8703B)
	if (IS_HARDWARE_TYPE_8703B(padapter))
		fill_tx_desc_8703b(padapter);
#endif

#if defined(CONFIG_RTL8188F)
	if (IS_HARDWARE_TYPE_8188F(padapter))
		fill_tx_desc_8188f(padapter);
#endif

#if defined(CONFIG_RTL8188GTV)
	if (IS_HARDWARE_TYPE_8188GTV(padapter))
		fill_tx_desc_8188gtv(padapter);
#endif

#if defined(CONFIG_RTL8723D)
	if (IS_HARDWARE_TYPE_8723D(padapter))
		fill_tx_desc_8723d(padapter);
#endif
#if defined(CONFIG_RTL8192F)
		if (IS_HARDWARE_TYPE_8192F(padapter))
			fill_tx_desc_8192f(padapter);
#endif

#if defined(CONFIG_RTL8710B)
	if (IS_HARDWARE_TYPE_8710B(padapter))
		fill_tx_desc_8710b(padapter);
#endif

	/* 3 4. make wlan header, make_wlanhdr() */
	hdr = (struct rtw_ieee80211_hdr *)pkt_start;
	set_frame_sub_type(&hdr->frame_ctl, pattrib->subtype);

	_rtw_memcpy(hdr->addr1, pattrib->dst, ETH_ALEN); /* DA */
	_rtw_memcpy(hdr->addr2, pattrib->src, ETH_ALEN); /* SA */
	_rtw_memcpy(hdr->addr3, get_bssid(&padapter->mlmepriv), ETH_ALEN); /* RA, BSSID */

	/* 3 5. make payload */
	ptr = pkt_start + pattrib->hdrlen;

	switch (pmp_priv->tx.payload) {
	case 0:
		payload = 0x00;
		break;
	case 1:
		payload = 0x5a;
		break;
	case 2:
		payload = 0xa5;
		break;
	case 3:
		payload = 0xff;
		break;
	default:
		payload = 0x00;
		break;
	}
	pmp_priv->TXradomBuffer = rtw_zmalloc(4096);
	if (pmp_priv->TXradomBuffer == NULL) {
		RTW_INFO("mp create random buffer fail!\n");
		goto exit;
	}


	for (i = 0; i < 4096; i++)
		pmp_priv->TXradomBuffer[i] = rtw_random32() % 0xFF;

	/* startPlace = (u32)(rtw_random32() % 3450); */
	if (pmp_priv->mplink_btx == _TRUE)
		_rtw_memcpy(ptr, pmp_priv->mplink_buf, pkt_end - ptr);
	else
		_rtw_memcpy(ptr, pmp_priv->TXradomBuffer, pkt_end - ptr);
	/* _rtw_memset(ptr, payload, pkt_end - ptr); */
	rtw_mfree(pmp_priv->TXradomBuffer, 4096);

	/* 3 6. start thread */
#ifdef PLATFORM_LINUX
	pmp_priv->tx.PktTxThread = kthread_run(mp_xmit_packet_thread, pmp_priv, "RTW_MP_THREAD");
	if (IS_ERR(pmp_priv->tx.PktTxThread)) {
		RTW_ERR("Create PktTx Thread Fail !!!!!\n");
		pmp_priv->tx.PktTxThread = NULL;
	}
#endif
#ifdef PLATFORM_FREEBSD
	{
		struct proc *p;
		struct thread *td;
		pmp_priv->tx.PktTxThread = kproc_kthread_add(mp_xmit_packet_thread, pmp_priv,
			&p, &td, RFHIGHPID, 0, "MPXmitThread", "MPXmitThread");

		if (pmp_priv->tx.PktTxThread < 0)
			RTW_INFO("Create PktTx Thread Fail !!!!!\n");
	}
#endif

	Rtw_MPSetMacTxEDCA(padapter);
exit:
	return;
}

void SetPacketRx(PADAPTER pAdapter, u8 bStartRx, u8 bAB)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(pAdapter);
	struct mp_priv *pmppriv = &pAdapter->mppriv;


	if (bStartRx) {
#ifdef CONFIG_RTL8723B
		phy_set_mac_reg(pAdapter, 0xe70, BIT23 | BIT22, 0x3); /* Power on adc  (in RX_WAIT_CCA state) */
		write_bbreg(pAdapter, 0xa01, BIT0, bDisable);/* improve Rx performance by jerry	 */
#endif
		pHalData->ReceiveConfig = RCR_AAP | RCR_APM | RCR_AM | RCR_AMF | RCR_HTC_LOC_CTRL;
		pHalData->ReceiveConfig |= RCR_ACRC32;
		pHalData->ReceiveConfig |= RCR_APP_PHYST_RXFF | RCR_APP_ICV | RCR_APP_MIC;

		if (pmppriv->bSetRxBssid == _TRUE) {
			RTW_INFO("%s: pmppriv->network_macaddr=" MAC_FMT "\n", __func__,
				 MAC_ARG(pmppriv->network_macaddr));
			pHalData->ReceiveConfig = 0;
			pHalData->ReceiveConfig |= RCR_CBSSID_DATA | RCR_CBSSID_BCN |RCR_APM | RCR_AM | RCR_AB |RCR_AMF;
			pHalData->ReceiveConfig |= RCR_APP_PHYST_RXFF;

#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
			write_bbreg(pAdapter, 0x550, BIT3, bEnable);
#endif
			rtw_write16(pAdapter, REG_RXFLTMAP0, 0xFFEF); /* REG_RXFLTMAP0 (RX Filter Map Group 0) */
			pmppriv->brx_filter_beacon = _TRUE;

		} else {
			pHalData->ReceiveConfig |= RCR_ADF;
			/* Accept all data frames */
			rtw_write16(pAdapter, REG_RXFLTMAP2, 0xFFFF);
		}

		if (bAB)
			pHalData->ReceiveConfig |= RCR_AB;
	} else {
#ifdef CONFIG_RTL8723B
		phy_set_mac_reg(pAdapter, 0xe70, BIT23 | BIT22, 0x00); /* Power off adc  (in RX_WAIT_CCA state)*/
		write_bbreg(pAdapter, 0xa01, BIT0, bEnable);/* improve Rx performance by jerry	 */
#endif
		pHalData->ReceiveConfig = 0;
		rtw_write16(pAdapter, REG_RXFLTMAP0, 0xFFFF); /* REG_RXFLTMAP0 (RX Filter Map Group 0) */
	}

	rtw_write32(pAdapter, REG_RCR, pHalData->ReceiveConfig);
}

void ResetPhyRxPktCount(PADAPTER pAdapter)
{
	u32 i, phyrx_set = 0;

	for (i = 0; i <= 0xF; i++) {
		phyrx_set = 0;
		phyrx_set |= _RXERR_RPT_SEL(i);	/* select */
		phyrx_set |= RXERR_RPT_RST;	/* set counter to zero */
		rtw_write32(pAdapter, REG_RXERR_RPT, phyrx_set);
	}
}

static u32 GetPhyRxPktCounts(PADAPTER pAdapter, u32 selbit)
{
	/* selection */
	u32 phyrx_set = 0, count = 0;

	phyrx_set = _RXERR_RPT_SEL(selbit & 0xF);
	rtw_write32(pAdapter, REG_RXERR_RPT, phyrx_set);

	/* Read packet count */
	count = rtw_read32(pAdapter, REG_RXERR_RPT) & RXERR_COUNTER_MASK;

	return count;
}

u32 GetPhyRxPktReceived(PADAPTER pAdapter)
{
	u32 OFDM_cnt = 0, CCK_cnt = 0, HT_cnt = 0;

	OFDM_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_OFDM_MPDU_OK);
	CCK_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_CCK_MPDU_OK);
	HT_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_HT_MPDU_OK);

	return OFDM_cnt + CCK_cnt + HT_cnt;
}

u32 GetPhyRxPktCRC32Error(PADAPTER pAdapter)
{
	u32 OFDM_cnt = 0, CCK_cnt = 0, HT_cnt = 0;

	OFDM_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_OFDM_MPDU_FAIL);
	CCK_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_CCK_MPDU_FAIL);
	HT_cnt = GetPhyRxPktCounts(pAdapter, RXERR_TYPE_HT_MPDU_FAIL);

	return OFDM_cnt + CCK_cnt + HT_cnt;
}

struct psd_init_regs {
	/* 3 wire */
	int reg_88c;
	int reg_c00;
	int reg_e00;
	int reg_1800;
	int reg_1a00;
	/* cck */
	int reg_800;
	int reg_808;
};

static int rtw_mp_psd_init(PADAPTER padapter, struct psd_init_regs *regs)
{
	HAL_DATA_TYPE	*phal_data	= GET_HAL_DATA(padapter);

	switch (phal_data->rf_type) {
	/* 1R */
	case RF_1T1R:
		if (hal_chk_proto_cap(padapter, PROTO_CAP_11AC)) {
			/* 11AC 1R PSD Setting 3wire & cck off */
			regs->reg_c00 = rtw_read32(padapter, 0xC00);
			phy_set_bb_reg(padapter, 0xC00, 0x3, 0x00);
			regs->reg_808 = rtw_read32(padapter, 0x808);
			phy_set_bb_reg(padapter, 0x808, 0x10000000, 0x0);
		} else {
			/* 11N 3-wire off 1 */
			regs->reg_88c = rtw_read32(padapter, 0x88C);
			phy_set_bb_reg(padapter, 0x88C, 0x300000, 0x3);
			/* 11N CCK off */
			regs->reg_800 = rtw_read32(padapter, 0x800);
			phy_set_bb_reg(padapter, 0x800, 0x1000000, 0x0);
		}
	break;

	/* 2R */
	case RF_1T2R:
	case RF_2T2R:
		if (hal_chk_proto_cap(padapter, PROTO_CAP_11AC)) {
			/* 11AC 2R PSD Setting 3wire & cck off */
			regs->reg_c00 = rtw_read32(padapter, 0xC00);
			regs->reg_e00 = rtw_read32(padapter, 0xE00);
			phy_set_bb_reg(padapter, 0xC00, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0xE00, 0x3, 0x00);
			regs->reg_808 = rtw_read32(padapter, 0x808);
			phy_set_bb_reg(padapter, 0x808, 0x10000000, 0x0);
		} else {
			/* 11N 3-wire off 2 */
			regs->reg_88c = rtw_read32(padapter, 0x88C);
			phy_set_bb_reg(padapter, 0x88C, 0xF00000, 0xF);
			/* 11N CCK off */
			regs->reg_800 = rtw_read32(padapter, 0x800);
			phy_set_bb_reg(padapter, 0x800, 0x1000000, 0x0);
		}
	break;

	/* 3R */
	case RF_2T3R:
	case RF_3T3R:
		if (hal_chk_proto_cap(padapter, PROTO_CAP_11AC)) {
			/* 11AC 3R PSD Setting 3wire & cck off */
			regs->reg_c00 = rtw_read32(padapter, 0xC00);
			regs->reg_e00 = rtw_read32(padapter, 0xE00);
			regs->reg_1800 = rtw_read32(padapter, 0x1800);
			phy_set_bb_reg(padapter, 0xC00, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0xE00, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0x1800, 0x3, 0x00);
			regs->reg_808 = rtw_read32(padapter, 0x808);
			phy_set_bb_reg(padapter, 0x808, 0x10000000, 0x0);
		} else {
			RTW_ERR("%s: 11n don't support 3R\n", __func__);
			return -1;
		}
		break;

	/* 4R */
	case RF_2T4R:
	case RF_3T4R:
	case RF_4T4R:
		if (hal_chk_proto_cap(padapter, PROTO_CAP_11AC)) {
			/* 11AC 4R PSD Setting 3wire & cck off */
			regs->reg_c00 = rtw_read32(padapter, 0xC00);
			regs->reg_e00 = rtw_read32(padapter, 0xE00);
			regs->reg_1800 = rtw_read32(padapter, 0x1800);
			regs->reg_1a00 = rtw_read32(padapter, 0x1A00);
			phy_set_bb_reg(padapter, 0xC00, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0xE00, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0x1800, 0x3, 0x00);
			phy_set_bb_reg(padapter, 0x1A00, 0x3, 0x00);
			regs->reg_808 = rtw_read32(padapter, 0x808);
			phy_set_bb_reg(padapter, 0x808, 0x10000000, 0x0);
		} else {
			RTW_ERR("%s: 11n don't support 4R\n", __func__);
			return -1;
		}
		break;

	default:
		RTW_ERR("%s: unknown %d rf type\n", __func__, phal_data->rf_type);
		return -1;
	}

	/* Set PSD points, 0=128, 1=256, 2=512, 3=1024 */
	if (hal_chk_proto_cap(padapter, PROTO_CAP_11AC))
		phy_set_bb_reg(padapter, 0x910, 0xC000, 3);
	else
		phy_set_bb_reg(padapter, 0x808, 0xC000, 3);

	RTW_INFO("%s: set %d rf type done\n", __func__, phal_data->rf_type);
	return 0;
}

static int rtw_mp_psd_close(PADAPTER padapter, struct psd_init_regs *regs)
{
	HAL_DATA_TYPE	*phal_data	= GET_HAL_DATA(padapter);


	if (!hal_chk_proto_cap(padapter, PROTO_CAP_11AC)) {
		/* 11n 3wire restore */
		rtw_write32(padapter, 0x88C, regs->reg_88c);
		/* 11n cck restore */
		rtw_write32(padapter, 0x800, regs->reg_800);
		RTW_INFO("%s: restore %d rf type\n", __func__, phal_data->rf_type);
		return 0;
	}

	/* 11ac 3wire restore */
	switch (phal_data->rf_type) {
	case RF_1T1R:
		rtw_write32(padapter, 0xC00, regs->reg_c00);
		break;
	case RF_1T2R:
	case RF_2T2R:
		rtw_write32(padapter, 0xC00, regs->reg_c00);
		rtw_write32(padapter, 0xE00, regs->reg_e00);
		break;
	case RF_2T3R:
	case RF_3T3R:
		rtw_write32(padapter, 0xC00, regs->reg_c00);
		rtw_write32(padapter, 0xE00, regs->reg_e00);
		rtw_write32(padapter, 0x1800, regs->reg_1800);
		break;
	case RF_2T4R:
	case RF_3T4R:
	case RF_4T4R:
		rtw_write32(padapter, 0xC00, regs->reg_c00);
		rtw_write32(padapter, 0xE00, regs->reg_e00);
		rtw_write32(padapter, 0x1800, regs->reg_1800);
		rtw_write32(padapter, 0x1A00, regs->reg_1a00);
		break;
	default:
		RTW_WARN("%s: unknown %d rf type\n", __func__, phal_data->rf_type);
		break;
	}

	/* 11ac cck restore */
	rtw_write32(padapter, 0x808, regs->reg_808);
	RTW_INFO("%s: restore %d rf type done\n", __func__, phal_data->rf_type);
	return 0;
}

/* reg 0x808[9:0]: FFT data x
 * reg 0x808[22]:  0  -->  1  to get 1 FFT data y
 * reg 0x8B4[15:0]: FFT data y report */
static u32 rtw_GetPSDData(PADAPTER pAdapter, u32 point)
{
	u32 psd_val = 0;

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	u16 psd_reg = 0x910;
	u16 psd_regL = 0xF44;
#else
	u16 psd_reg = 0x808;
	u16 psd_regL = 0x8B4;
#endif

	psd_val = rtw_read32(pAdapter, psd_reg);

	psd_val &= 0xFFBFFC00;
	psd_val |= point;

	rtw_write32(pAdapter, psd_reg, psd_val);
	rtw_mdelay_os(1);
	psd_val |= 0x00400000;

	rtw_write32(pAdapter, psd_reg, psd_val);
	rtw_mdelay_os(1);

	psd_val = rtw_read32(pAdapter, psd_regL);
#if defined(CONFIG_RTL8821C)
	psd_val = (psd_val & 0x00FFFFFF) / 32;
#else
	psd_val &= 0x0000FFFF;
#endif

	return psd_val;
}

/*
 * pts	start_point_min		stop_point_max
 * 128	64			64 + 128 = 192
 * 256	128			128 + 256 = 384
 * 512	256			256 + 512 = 768
 * 1024	512			512 + 1024 = 1536
 *
 */
u32 mp_query_psd(PADAPTER pAdapter, u8 *data)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	struct dm_struct *p_dm = adapter_to_phydm(pAdapter);

	u32 i, psd_pts = 0, psd_start = 0, psd_stop = 0;
	u32 psd_data = 0;
	struct psd_init_regs regs = {};
	int psd_analysis = 0;


#ifdef PLATFORM_LINUX
	if (!netif_running(pAdapter->pnetdev)) {
		return 0;
	}
#endif

	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		return 0;
	}

	if (strlen(data) == 0) { /* default value */
		psd_pts = 128;
		psd_start = 64;
		psd_stop = 128;
	} else if (strncmp(data, "analysis,", 9) == 0) {
		if (rtw_mp_psd_init(pAdapter, &regs) != 0)
			return 0;
		psd_analysis = 1;
		sscanf(data + 9, "pts=%d,start=%d,stop=%d", &psd_pts, &psd_start, &psd_stop);
	} else
		sscanf(data, "pts=%d,start=%d,stop=%d", &psd_pts, &psd_start, &psd_stop);

	data[0] = '\0';

	if (IS_HARDWARE_TYPE_8822C(pAdapter)) {
			u32 *psdbuf = rtw_zmalloc(sizeof(u32)*256);

			if (psdbuf == NULL) {
				RTW_INFO("%s: psd buf malloc fail!!\n", __func__);
				return 0;
			}

			halrf_cmn_info_set(p_dm, HALRF_CMNINFO_MP_PSD_POINT, psd_pts);
			halrf_cmn_info_set(p_dm, HALRF_CMNINFO_MP_PSD_START_POINT, psd_start);
			halrf_cmn_info_set(p_dm, HALRF_CMNINFO_MP_PSD_STOP_POINT, psd_stop);
			halrf_cmn_info_set(p_dm, HALRF_CMNINFO_MP_PSD_AVERAGE, 0x20000);

			halrf_psd_init(p_dm);
#ifdef CONFIG_LONG_DELAY_ISSUE
		rtw_msleep_os(100);
#else
		rtw_mdelay_os(100);
#endif
			halrf_psd_query(p_dm, psdbuf, 256);

			i = 0;
			while (i < 256) {
				sprintf(data, "%s%x ", data, (psdbuf[i]));
				i++;
			}
	
		if (psdbuf)
			rtw_mfree(psdbuf, sizeof(u32)*256);

	} else {
	i = psd_start;
	while (i < psd_stop) {
		if (i >= psd_pts)
			psd_data = rtw_GetPSDData(pAdapter, i - psd_pts);
		else
			psd_data = rtw_GetPSDData(pAdapter, i);

		sprintf(data, "%s%x ", data, psd_data);
		i++;
	}

	}

#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);
#else
	rtw_mdelay_os(100);
#endif

	if (psd_analysis)
		rtw_mp_psd_close(pAdapter, &regs);

	return strlen(data) + 1;
}


#if 0
void _rtw_mp_xmit_priv(struct xmit_priv *pxmitpriv)
{
	int i, res;
	_adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	u32 max_xmit_extbuf_size = MAX_XMIT_EXTBUF_SZ;
	u32 num_xmit_extbuf = NR_XMIT_EXTBUFF;
	if (padapter->registrypriv.mp_mode == 0) {
		max_xmit_extbuf_size = MAX_XMIT_EXTBUF_SZ;
		num_xmit_extbuf = NR_XMIT_EXTBUFF;
	} else {
		max_xmit_extbuf_size = 6000;
		num_xmit_extbuf = 8;
	}

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;
	for (i = 0; i < num_xmit_extbuf; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (max_xmit_extbuf_size + XMITBUF_ALIGN_SZ), _FALSE);

		pxmitbuf++;
	}

	if (pxmitpriv->pallocated_xmit_extbuf)
		rtw_vmfree(pxmitpriv->pallocated_xmit_extbuf, num_xmit_extbuf * sizeof(struct xmit_buf) + 4);

	if (padapter->registrypriv.mp_mode == 0) {
		max_xmit_extbuf_size = 6000;
		num_xmit_extbuf = 8;
	} else {
		max_xmit_extbuf_size = MAX_XMIT_EXTBUF_SZ;
		num_xmit_extbuf = NR_XMIT_EXTBUFF;
	}

	/* Init xmit extension buff */
	_rtw_init_queue(&pxmitpriv->free_xmit_extbuf_queue);

	pxmitpriv->pallocated_xmit_extbuf = rtw_zvmalloc(num_xmit_extbuf * sizeof(struct xmit_buf) + 4);

	if (pxmitpriv->pallocated_xmit_extbuf  == NULL) {
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmit_extbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmit_extbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;

	for (i = 0; i < num_xmit_extbuf; i++) {
		_rtw_init_listhead(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->buf_tag = XMITBUF_MGNT;

		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, max_xmit_extbuf_size + XMITBUF_ALIGN_SZ, _TRUE);
		if (res == _FAIL) {
			res = _FAIL;
			goto exit;
		}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + max_xmit_extbuf_size;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmit_extbuf_queue.queue));
#ifdef DBG_XMIT_BUF_EXT
		pxmitbuf->no = i;
#endif
		pxmitbuf++;

	}

	pxmitpriv->free_xmit_extbuf_cnt = num_xmit_extbuf;

exit:
	;
}
#endif

u8
mpt_to_mgnt_rate(
		u32	MptRateIdx
)
{
	/* Mapped to MGN_XXX defined in MgntGen.h */
	switch (MptRateIdx) {
	/* CCK rate. */
	case	MPT_RATE_1M:
		return MGN_1M;
	case	MPT_RATE_2M:
		return MGN_2M;
	case	MPT_RATE_55M:
		return MGN_5_5M;
	case	MPT_RATE_11M:
		return MGN_11M;

	/* OFDM rate. */
	case	MPT_RATE_6M:
		return MGN_6M;
	case	MPT_RATE_9M:
		return MGN_9M;
	case	MPT_RATE_12M:
		return MGN_12M;
	case	MPT_RATE_18M:
		return MGN_18M;
	case	MPT_RATE_24M:
		return MGN_24M;
	case	MPT_RATE_36M:
		return MGN_36M;
	case	MPT_RATE_48M:
		return MGN_48M;
	case	MPT_RATE_54M:
		return MGN_54M;

	/* HT rate. */
	case	MPT_RATE_MCS0:
		return MGN_MCS0;
	case	MPT_RATE_MCS1:
		return MGN_MCS1;
	case	MPT_RATE_MCS2:
		return MGN_MCS2;
	case	MPT_RATE_MCS3:
		return MGN_MCS3;
	case	MPT_RATE_MCS4:
		return MGN_MCS4;
	case	MPT_RATE_MCS5:
		return MGN_MCS5;
	case	MPT_RATE_MCS6:
		return MGN_MCS6;
	case	MPT_RATE_MCS7:
		return MGN_MCS7;
	case	MPT_RATE_MCS8:
		return MGN_MCS8;
	case	MPT_RATE_MCS9:
		return MGN_MCS9;
	case	MPT_RATE_MCS10:
		return MGN_MCS10;
	case	MPT_RATE_MCS11:
		return MGN_MCS11;
	case	MPT_RATE_MCS12:
		return MGN_MCS12;
	case	MPT_RATE_MCS13:
		return MGN_MCS13;
	case	MPT_RATE_MCS14:
		return MGN_MCS14;
	case	MPT_RATE_MCS15:
		return MGN_MCS15;
	case	MPT_RATE_MCS16:
		return MGN_MCS16;
	case	MPT_RATE_MCS17:
		return MGN_MCS17;
	case	MPT_RATE_MCS18:
		return MGN_MCS18;
	case	MPT_RATE_MCS19:
		return MGN_MCS19;
	case	MPT_RATE_MCS20:
		return MGN_MCS20;
	case	MPT_RATE_MCS21:
		return MGN_MCS21;
	case	MPT_RATE_MCS22:
		return MGN_MCS22;
	case	MPT_RATE_MCS23:
		return MGN_MCS23;
	case	MPT_RATE_MCS24:
		return MGN_MCS24;
	case	MPT_RATE_MCS25:
		return MGN_MCS25;
	case	MPT_RATE_MCS26:
		return MGN_MCS26;
	case	MPT_RATE_MCS27:
		return MGN_MCS27;
	case	MPT_RATE_MCS28:
		return MGN_MCS28;
	case	MPT_RATE_MCS29:
		return MGN_MCS29;
	case	MPT_RATE_MCS30:
		return MGN_MCS30;
	case	MPT_RATE_MCS31:
		return MGN_MCS31;

	/* VHT rate. */
	case	MPT_RATE_VHT1SS_MCS0:
		return MGN_VHT1SS_MCS0;
	case	MPT_RATE_VHT1SS_MCS1:
		return MGN_VHT1SS_MCS1;
	case	MPT_RATE_VHT1SS_MCS2:
		return MGN_VHT1SS_MCS2;
	case	MPT_RATE_VHT1SS_MCS3:
		return MGN_VHT1SS_MCS3;
	case	MPT_RATE_VHT1SS_MCS4:
		return MGN_VHT1SS_MCS4;
	case	MPT_RATE_VHT1SS_MCS5:
		return MGN_VHT1SS_MCS5;
	case	MPT_RATE_VHT1SS_MCS6:
		return MGN_VHT1SS_MCS6;
	case	MPT_RATE_VHT1SS_MCS7:
		return MGN_VHT1SS_MCS7;
	case	MPT_RATE_VHT1SS_MCS8:
		return MGN_VHT1SS_MCS8;
	case	MPT_RATE_VHT1SS_MCS9:
		return MGN_VHT1SS_MCS9;
	case	MPT_RATE_VHT2SS_MCS0:
		return MGN_VHT2SS_MCS0;
	case	MPT_RATE_VHT2SS_MCS1:
		return MGN_VHT2SS_MCS1;
	case	MPT_RATE_VHT2SS_MCS2:
		return MGN_VHT2SS_MCS2;
	case	MPT_RATE_VHT2SS_MCS3:
		return MGN_VHT2SS_MCS3;
	case	MPT_RATE_VHT2SS_MCS4:
		return MGN_VHT2SS_MCS4;
	case	MPT_RATE_VHT2SS_MCS5:
		return MGN_VHT2SS_MCS5;
	case	MPT_RATE_VHT2SS_MCS6:
		return MGN_VHT2SS_MCS6;
	case	MPT_RATE_VHT2SS_MCS7:
		return MGN_VHT2SS_MCS7;
	case	MPT_RATE_VHT2SS_MCS8:
		return MGN_VHT2SS_MCS8;
	case	MPT_RATE_VHT2SS_MCS9:
		return MGN_VHT2SS_MCS9;
	case	MPT_RATE_VHT3SS_MCS0:
		return MGN_VHT3SS_MCS0;
	case	MPT_RATE_VHT3SS_MCS1:
		return MGN_VHT3SS_MCS1;
	case	MPT_RATE_VHT3SS_MCS2:
		return MGN_VHT3SS_MCS2;
	case	MPT_RATE_VHT3SS_MCS3:
		return MGN_VHT3SS_MCS3;
	case	MPT_RATE_VHT3SS_MCS4:
		return MGN_VHT3SS_MCS4;
	case	MPT_RATE_VHT3SS_MCS5:
		return MGN_VHT3SS_MCS5;
	case	MPT_RATE_VHT3SS_MCS6:
		return MGN_VHT3SS_MCS6;
	case	MPT_RATE_VHT3SS_MCS7:
		return MGN_VHT3SS_MCS7;
	case	MPT_RATE_VHT3SS_MCS8:
		return MGN_VHT3SS_MCS8;
	case	MPT_RATE_VHT3SS_MCS9:
		return MGN_VHT3SS_MCS9;
	case	MPT_RATE_VHT4SS_MCS0:
		return MGN_VHT4SS_MCS0;
	case	MPT_RATE_VHT4SS_MCS1:
		return MGN_VHT4SS_MCS1;
	case	MPT_RATE_VHT4SS_MCS2:
		return MGN_VHT4SS_MCS2;
	case	MPT_RATE_VHT4SS_MCS3:
		return MGN_VHT4SS_MCS3;
	case	MPT_RATE_VHT4SS_MCS4:
		return MGN_VHT4SS_MCS4;
	case	MPT_RATE_VHT4SS_MCS5:
		return MGN_VHT4SS_MCS5;
	case	MPT_RATE_VHT4SS_MCS6:
		return MGN_VHT4SS_MCS6;
	case	MPT_RATE_VHT4SS_MCS7:
		return MGN_VHT4SS_MCS7;
	case	MPT_RATE_VHT4SS_MCS8:
		return MGN_VHT4SS_MCS8;
	case	MPT_RATE_VHT4SS_MCS9:
		return MGN_VHT4SS_MCS9;

	case	MPT_RATE_LAST:	/* fully automatiMGN_VHT2SS_MCS1;	 */
	default:
		RTW_INFO("<===mpt_to_mgnt_rate(), Invalid Rate: %d!!\n", MptRateIdx);
		return 0x0;
	}
}


u8 HwRateToMPTRate(u8 rate)
{
	u8	ret_rate = MGN_1M;

	switch (rate) {
	case DESC_RATE1M:
		ret_rate = MPT_RATE_1M;
		break;
	case DESC_RATE2M:
		ret_rate = MPT_RATE_2M;
		break;
	case DESC_RATE5_5M:
		ret_rate = MPT_RATE_55M;
		break;
	case DESC_RATE11M:
		ret_rate = MPT_RATE_11M;
		break;
	case DESC_RATE6M:
		ret_rate = MPT_RATE_6M;
		break;
	case DESC_RATE9M:
		ret_rate = MPT_RATE_9M;
		break;
	case DESC_RATE12M:
		ret_rate = MPT_RATE_12M;
		break;
	case DESC_RATE18M:
		ret_rate = MPT_RATE_18M;
		break;
	case DESC_RATE24M:
		ret_rate = MPT_RATE_24M;
		break;
	case DESC_RATE36M:
		ret_rate = MPT_RATE_36M;
		break;
	case DESC_RATE48M:
		ret_rate = MPT_RATE_48M;
		break;
	case DESC_RATE54M:
		ret_rate = MPT_RATE_54M;
		break;
	case DESC_RATEMCS0:
		ret_rate = MPT_RATE_MCS0;
		break;
	case DESC_RATEMCS1:
		ret_rate = MPT_RATE_MCS1;
		break;
	case DESC_RATEMCS2:
		ret_rate = MPT_RATE_MCS2;
		break;
	case DESC_RATEMCS3:
		ret_rate = MPT_RATE_MCS3;
		break;
	case DESC_RATEMCS4:
		ret_rate = MPT_RATE_MCS4;
		break;
	case DESC_RATEMCS5:
		ret_rate = MPT_RATE_MCS5;
		break;
	case DESC_RATEMCS6:
		ret_rate = MPT_RATE_MCS6;
		break;
	case DESC_RATEMCS7:
		ret_rate = MPT_RATE_MCS7;
		break;
	case DESC_RATEMCS8:
		ret_rate = MPT_RATE_MCS8;
		break;
	case DESC_RATEMCS9:
		ret_rate = MPT_RATE_MCS9;
		break;
	case DESC_RATEMCS10:
		ret_rate = MPT_RATE_MCS10;
		break;
	case DESC_RATEMCS11:
		ret_rate = MPT_RATE_MCS11;
		break;
	case DESC_RATEMCS12:
		ret_rate = MPT_RATE_MCS12;
		break;
	case DESC_RATEMCS13:
		ret_rate = MPT_RATE_MCS13;
		break;
	case DESC_RATEMCS14:
		ret_rate = MPT_RATE_MCS14;
		break;
	case DESC_RATEMCS15:
		ret_rate = MPT_RATE_MCS15;
		break;
	case DESC_RATEMCS16:
		ret_rate = MPT_RATE_MCS16;
		break;
	case DESC_RATEMCS17:
		ret_rate = MPT_RATE_MCS17;
		break;
	case DESC_RATEMCS18:
		ret_rate = MPT_RATE_MCS18;
		break;
	case DESC_RATEMCS19:
		ret_rate = MPT_RATE_MCS19;
		break;
	case DESC_RATEMCS20:
		ret_rate = MPT_RATE_MCS20;
		break;
	case DESC_RATEMCS21:
		ret_rate = MPT_RATE_MCS21;
		break;
	case DESC_RATEMCS22:
		ret_rate = MPT_RATE_MCS22;
		break;
	case DESC_RATEMCS23:
		ret_rate = MPT_RATE_MCS23;
		break;
	case DESC_RATEMCS24:
		ret_rate = MPT_RATE_MCS24;
		break;
	case DESC_RATEMCS25:
		ret_rate = MPT_RATE_MCS25;
		break;
	case DESC_RATEMCS26:
		ret_rate = MPT_RATE_MCS26;
		break;
	case DESC_RATEMCS27:
		ret_rate = MPT_RATE_MCS27;
		break;
	case DESC_RATEMCS28:
		ret_rate = MPT_RATE_MCS28;
		break;
	case DESC_RATEMCS29:
		ret_rate = MPT_RATE_MCS29;
		break;
	case DESC_RATEMCS30:
		ret_rate = MPT_RATE_MCS30;
		break;
	case DESC_RATEMCS31:
		ret_rate = MPT_RATE_MCS31;
		break;
	case DESC_RATEVHTSS1MCS0:
		ret_rate = MPT_RATE_VHT1SS_MCS0;
		break;
	case DESC_RATEVHTSS1MCS1:
		ret_rate = MPT_RATE_VHT1SS_MCS1;
		break;
	case DESC_RATEVHTSS1MCS2:
		ret_rate = MPT_RATE_VHT1SS_MCS2;
		break;
	case DESC_RATEVHTSS1MCS3:
		ret_rate = MPT_RATE_VHT1SS_MCS3;
		break;
	case DESC_RATEVHTSS1MCS4:
		ret_rate = MPT_RATE_VHT1SS_MCS4;
		break;
	case DESC_RATEVHTSS1MCS5:
		ret_rate = MPT_RATE_VHT1SS_MCS5;
		break;
	case DESC_RATEVHTSS1MCS6:
		ret_rate = MPT_RATE_VHT1SS_MCS6;
		break;
	case DESC_RATEVHTSS1MCS7:
		ret_rate = MPT_RATE_VHT1SS_MCS7;
		break;
	case DESC_RATEVHTSS1MCS8:
		ret_rate = MPT_RATE_VHT1SS_MCS8;
		break;
	case DESC_RATEVHTSS1MCS9:
		ret_rate = MPT_RATE_VHT1SS_MCS9;
		break;
	case DESC_RATEVHTSS2MCS0:
		ret_rate = MPT_RATE_VHT2SS_MCS0;
		break;
	case DESC_RATEVHTSS2MCS1:
		ret_rate = MPT_RATE_VHT2SS_MCS1;
		break;
	case DESC_RATEVHTSS2MCS2:
		ret_rate = MPT_RATE_VHT2SS_MCS2;
		break;
	case DESC_RATEVHTSS2MCS3:
		ret_rate = MPT_RATE_VHT2SS_MCS3;
		break;
	case DESC_RATEVHTSS2MCS4:
		ret_rate = MPT_RATE_VHT2SS_MCS4;
		break;
	case DESC_RATEVHTSS2MCS5:
		ret_rate = MPT_RATE_VHT2SS_MCS5;
		break;
	case DESC_RATEVHTSS2MCS6:
		ret_rate = MPT_RATE_VHT2SS_MCS6;
		break;
	case DESC_RATEVHTSS2MCS7:
		ret_rate = MPT_RATE_VHT2SS_MCS7;
		break;
	case DESC_RATEVHTSS2MCS8:
		ret_rate = MPT_RATE_VHT2SS_MCS8;
		break;
	case DESC_RATEVHTSS2MCS9:
		ret_rate = MPT_RATE_VHT2SS_MCS9;
		break;
	case DESC_RATEVHTSS3MCS0:
		ret_rate = MPT_RATE_VHT3SS_MCS0;
		break;
	case DESC_RATEVHTSS3MCS1:
		ret_rate = MPT_RATE_VHT3SS_MCS1;
		break;
	case DESC_RATEVHTSS3MCS2:
		ret_rate = MPT_RATE_VHT3SS_MCS2;
		break;
	case DESC_RATEVHTSS3MCS3:
		ret_rate = MPT_RATE_VHT3SS_MCS3;
		break;
	case DESC_RATEVHTSS3MCS4:
		ret_rate = MPT_RATE_VHT3SS_MCS4;
		break;
	case DESC_RATEVHTSS3MCS5:
		ret_rate = MPT_RATE_VHT3SS_MCS5;
		break;
	case DESC_RATEVHTSS3MCS6:
		ret_rate = MPT_RATE_VHT3SS_MCS6;
		break;
	case DESC_RATEVHTSS3MCS7:
		ret_rate = MPT_RATE_VHT3SS_MCS7;
		break;
	case DESC_RATEVHTSS3MCS8:
		ret_rate = MPT_RATE_VHT3SS_MCS8;
		break;
	case DESC_RATEVHTSS3MCS9:
		ret_rate = MPT_RATE_VHT3SS_MCS9;
		break;
	case DESC_RATEVHTSS4MCS0:
		ret_rate = MPT_RATE_VHT4SS_MCS0;
		break;
	case DESC_RATEVHTSS4MCS1:
		ret_rate = MPT_RATE_VHT4SS_MCS1;
		break;
	case DESC_RATEVHTSS4MCS2:
		ret_rate = MPT_RATE_VHT4SS_MCS2;
		break;
	case DESC_RATEVHTSS4MCS3:
		ret_rate = MPT_RATE_VHT4SS_MCS3;
		break;
	case DESC_RATEVHTSS4MCS4:
		ret_rate = MPT_RATE_VHT4SS_MCS4;
		break;
	case DESC_RATEVHTSS4MCS5:
		ret_rate = MPT_RATE_VHT4SS_MCS5;
		break;
	case DESC_RATEVHTSS4MCS6:
		ret_rate = MPT_RATE_VHT4SS_MCS6;
		break;
	case DESC_RATEVHTSS4MCS7:
		ret_rate = MPT_RATE_VHT4SS_MCS7;
		break;
	case DESC_RATEVHTSS4MCS8:
		ret_rate = MPT_RATE_VHT4SS_MCS8;
		break;
	case DESC_RATEVHTSS4MCS9:
		ret_rate = MPT_RATE_VHT4SS_MCS9;
		break;

	default:
		RTW_INFO("hw_rate_to_m_rate(): Non supported Rate [%x]!!!\n", rate);
		break;
	}
	return ret_rate;
}

u8 rtw_mpRateParseFunc(PADAPTER pAdapter, u8 *targetStr)
{
	u16 i = 0;
	u8 *rateindex_Array[] = { "1M", "2M", "5.5M", "11M", "6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M",
		"HTMCS0", "HTMCS1", "HTMCS2", "HTMCS3", "HTMCS4", "HTMCS5", "HTMCS6", "HTMCS7",
		"HTMCS8", "HTMCS9", "HTMCS10", "HTMCS11", "HTMCS12", "HTMCS13", "HTMCS14", "HTMCS15",
		"HTMCS16", "HTMCS17", "HTMCS18", "HTMCS19", "HTMCS20", "HTMCS21", "HTMCS22", "HTMCS23",
		"HTMCS24", "HTMCS25", "HTMCS26", "HTMCS27", "HTMCS28", "HTMCS29", "HTMCS30", "HTMCS31",
		"VHT1MCS0", "VHT1MCS1", "VHT1MCS2", "VHT1MCS3", "VHT1MCS4", "VHT1MCS5", "VHT1MCS6", "VHT1MCS7", "VHT1MCS8", "VHT1MCS9",
		"VHT2MCS0", "VHT2MCS1", "VHT2MCS2", "VHT2MCS3", "VHT2MCS4", "VHT2MCS5", "VHT2MCS6", "VHT2MCS7", "VHT2MCS8", "VHT2MCS9",
		"VHT3MCS0", "VHT3MCS1", "VHT3MCS2", "VHT3MCS3", "VHT3MCS4", "VHT3MCS5", "VHT3MCS6", "VHT3MCS7", "VHT3MCS8", "VHT3MCS9",
		"VHT4MCS0", "VHT4MCS1", "VHT4MCS2", "VHT4MCS3", "VHT4MCS4", "VHT4MCS5", "VHT4MCS6", "VHT4MCS7", "VHT4MCS8", "VHT4MCS9"
				};

	for (i = 0; i <= 83; i++) {
		if (strcmp(targetStr, rateindex_Array[i]) == 0) {
			RTW_INFO("%s , index = %d\n", __func__ , i);
			return i;
		}
	}

	printk("%s ,please input a Data RATE String as:", __func__);
	for (i = 0; i <= 83; i++) {
		printk("%s ", rateindex_Array[i]);
		if (i % 10 == 0)
			printk("\n");
	}
	return _FAIL;
}

u8 rtw_mp_mode_check(PADAPTER pAdapter)
{
	PADAPTER primary_adapter = GET_PRIMARY_ADAPTER(pAdapter);

	if (primary_adapter->registrypriv.mp_mode == 1 || primary_adapter->mppriv.bprocess_mp_mode == _TRUE)
		return _TRUE;
	else
		return _FALSE;
}


u32 mpt_ProQueryCalTxPower(
	PADAPTER	pAdapter,
	u8		RfPath
)
{

	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);

	u32			TxPower = 1;
	struct txpwr_idx_comp tic;
	u8 mgn_rate = mpt_to_mgnt_rate(pMptCtx->mpt_rate_index);

	TxPower = rtw_hal_get_tx_power_index(pAdapter, RfPath, mgn_rate, pHalData->current_channel_bw, pHalData->current_channel, &tic);

	RTW_INFO("TXPWR: [%c][%s]ch:%u, %s %uT, pwr_idx:%u(0x%02x) = %u + (%d=%d:%d) + (%d) + (%d) + (%d) + (%d)\n"
		, rf_path_char(RfPath), ch_width_str(pHalData->current_channel_bw), pHalData->current_channel, MGN_RATE_STR(mgn_rate), tic.ntx_idx + 1
		, TxPower, TxPower, tic.pg, (tic.by_rate > tic.limit ? tic.limit : tic.by_rate), tic.by_rate, tic.limit, tic.tpt
		, tic.ebias, tic.btc, tic.dpd);

	pAdapter->mppriv.txpoweridx = (u8)TxPower;
	if (RfPath == RF_PATH_A)
		pMptCtx->TxPwrLevel[RF_PATH_A] = (u8)TxPower;
	else if (RfPath == RF_PATH_B)
		pMptCtx->TxPwrLevel[RF_PATH_B] = (u8)TxPower;
	else if (RfPath == RF_PATH_C)
		pMptCtx->TxPwrLevel[RF_PATH_C] = (u8)TxPower;
	else if (RfPath == RF_PATH_D)
		pMptCtx->TxPwrLevel[RF_PATH_D] = (u8)TxPower;
	hal_mpt_SetTxPower(pAdapter);

	return TxPower;
}

#ifdef CONFIG_MP_VHT_HW_TX_MODE
static inline void dump_buf(u8 *buf, u32 len)
{
	u32 i;

	RTW_INFO("-----------------Len %d----------------\n", len);
	for (i = 0; i < len; i++)
		RTW_INFO("%2.2x-", *(buf + i));
	RTW_INFO("\n");
}

void ByteToBit(
	u8	*out,
	bool	*in,
	u8	in_size)
{
	u8 i = 0, j = 0;

	for (i = 0; i < in_size; i++) {
		for (j = 0; j < 8; j++) {
			if (in[8 * i + j])
				out[i] |= (1 << j);
		}
	}
}


void CRC16_generator(
	bool *out,
	bool *in,
	u8 in_size
)
{
	u8 i = 0;
	bool temp = 0, reg[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

	for (i = 0; i < in_size; i++) {/* take one's complement and bit reverse*/
		temp = in[i] ^ reg[15];
		reg[15]	= reg[14];
		reg[14]	= reg[13];
		reg[13]	= reg[12];
		reg[12]	= reg[11];
		reg[11]	= reg[10];
		reg[10]	= reg[9];
		reg[9]	= reg[8];
		reg[8]	= reg[7];

		reg[7]	= reg[6];
		reg[6]	= reg[5];
		reg[5]	= reg[4];
		reg[4]	= reg[3];
		reg[3]	= reg[2];
		reg[2]	= reg[1];
		reg[1]	= reg[0];
		reg[12]	= reg[12] ^ temp;
		reg[5]	= reg[5] ^ temp;
		reg[0]	= temp;
	}
	for (i = 0; i < 16; i++)	/* take one's complement and bit reverse*/
		out[i] = 1 - reg[15 - i];
}



/*========================================
	SFD		SIGNAL	SERVICE	LENGTH	CRC
	16 bit	8 bit	8 bit	16 bit	16 bit
========================================*/
void CCK_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
)
{
	double	ratio = 0;
	bool	crc16_in[32] = {0}, crc16_out[16] = {0};
	bool LengthExtBit;
	double LengthExact;
	double LengthPSDU;
	u8 i;
	u32 PacketLength = pPMacTxInfo->PacketLength;

	if (pPMacTxInfo->bSPreamble)
		pPMacTxInfo->SFD = 0x05CF;
	else
		pPMacTxInfo->SFD = 0xF3A0;

	switch (pPMacPktInfo->MCS) {
	case 0:
		pPMacTxInfo->SignalField = 0xA;
		ratio = 8;
		/*CRC16_in(1,0:7)=[0 1 0 1 0 0 0 0]*/
		crc16_in[1] = crc16_in[3] = 1;
		break;
	case 1:
		pPMacTxInfo->SignalField = 0x14;
		ratio = 4;
		/*CRC16_in(1,0:7)=[0 0 1 0 1 0 0 0];*/
		crc16_in[2] = crc16_in[4] = 1;
		break;
	case 2:
		pPMacTxInfo->SignalField = 0x37;
		ratio = 8.0 / 5.5;
		/*CRC16_in(1,0:7)=[1 1 1 0 1 1 0 0];*/
		crc16_in[0] = crc16_in[1] = crc16_in[2] = crc16_in[4] = crc16_in[5] = 1;
		break;
	case 3:
		pPMacTxInfo->SignalField = 0x6E;
		ratio = 8.0 / 11.0;
		/*CRC16_in(1,0:7)=[0 1 1 1 0 1 1 0];*/
		crc16_in[1] = crc16_in[2] = crc16_in[3] = crc16_in[5] = crc16_in[6] = 1;
		break;
	}

	LengthExact = PacketLength * ratio;
	LengthPSDU = ceil(LengthExact);

	if ((pPMacPktInfo->MCS == 3) &&
	    ((LengthPSDU - LengthExact) >= 0.727 || (LengthPSDU - LengthExact) <= -0.727))
		LengthExtBit = 1;
	else
		LengthExtBit = 0;


	pPMacTxInfo->LENGTH = (u32)LengthPSDU;
	/* CRC16_in(1,16:31) = LengthPSDU[0:15]*/
	for (i = 0; i < 16; i++)
		crc16_in[i + 16] = (pPMacTxInfo->LENGTH >> i) & 0x1;

	if (LengthExtBit == 0) {
		pPMacTxInfo->ServiceField = 0x0;
		/* CRC16_in(1,8:15) = [0 0 0 0 0 0 0 0];*/
	} else {
		pPMacTxInfo->ServiceField = 0x80;
		/*CRC16_in(1,8:15)=[0 0 0 0 0 0 0 1];*/
		crc16_in[15] = 1;
	}

	CRC16_generator(crc16_out, crc16_in, 32);

	_rtw_memset(pPMacTxInfo->CRC16, 0, 2);
	ByteToBit(pPMacTxInfo->CRC16, crc16_out, 2);

}


void PMAC_Get_Pkt_Param(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo)
{

	u8		TX_RATE_HEX = 0, MCS = 0;
	u8		TX_RATE = pPMacTxInfo->TX_RATE;

	/*	TX_RATE & Nss	*/
	if (MPT_IS_2SS_RATE(TX_RATE))
		pPMacPktInfo->Nss = 2;
	else if (MPT_IS_3SS_RATE(TX_RATE))
		pPMacPktInfo->Nss = 3;
	else if (MPT_IS_4SS_RATE(TX_RATE))
		pPMacPktInfo->Nss = 4;
	else
		pPMacPktInfo->Nss = 1;

	RTW_INFO("PMacTxInfo.Nss =%d\n", pPMacPktInfo->Nss);

	/*	MCS & TX_RATE_HEX*/
	if (MPT_IS_CCK_RATE(TX_RATE)) {
		switch (TX_RATE) {
		case MPT_RATE_1M:
			TX_RATE_HEX = MCS = 0;
			break;
		case MPT_RATE_2M:
			TX_RATE_HEX = MCS = 1;
			break;
		case MPT_RATE_55M:
			TX_RATE_HEX = MCS = 2;
			break;
		case MPT_RATE_11M:
			TX_RATE_HEX = MCS = 3;
			break;
		}
	} else if (MPT_IS_OFDM_RATE(TX_RATE)) {
		MCS = TX_RATE - MPT_RATE_6M;
		TX_RATE_HEX = MCS + 4;
	} else if (MPT_IS_HT_RATE(TX_RATE)) {
		MCS = TX_RATE - MPT_RATE_MCS0;
		TX_RATE_HEX = MCS + 12;
	} else if (MPT_IS_VHT_RATE(TX_RATE)) {
		TX_RATE_HEX = TX_RATE - MPT_RATE_VHT1SS_MCS0 + 44;

		if (MPT_IS_VHT_2S_RATE(TX_RATE))
			MCS = TX_RATE - MPT_RATE_VHT2SS_MCS0;
		else if (MPT_IS_VHT_3S_RATE(TX_RATE))
			MCS = TX_RATE - MPT_RATE_VHT3SS_MCS0;
		else if (MPT_IS_VHT_4S_RATE(TX_RATE))
			MCS = TX_RATE - MPT_RATE_VHT4SS_MCS0;
		else
			MCS = TX_RATE - MPT_RATE_VHT1SS_MCS0;
	}

	pPMacPktInfo->MCS = MCS;
	pPMacTxInfo->TX_RATE_HEX = TX_RATE_HEX;

	RTW_INFO(" MCS=%d, TX_RATE_HEX =0x%x\n", MCS, pPMacTxInfo->TX_RATE_HEX);
	/*	mSTBC & Nsts*/
	pPMacPktInfo->Nsts = pPMacPktInfo->Nss;
	if (pPMacTxInfo->bSTBC) {
		if (pPMacPktInfo->Nss == 1) {
			pPMacTxInfo->m_STBC = 2;
			pPMacPktInfo->Nsts = pPMacPktInfo->Nss * 2;
		} else
			pPMacTxInfo->m_STBC = 1;
	} else
		pPMacTxInfo->m_STBC = 1;
}


u32 LDPC_parameter_generator(
	u32 N_pld_int,
	u32 N_CBPSS,
	u32 N_SS,
	u32 R,
	u32 m_STBC,
	u32 N_TCB_int
)
{
	double	CR = 0.;
	double	N_pld = (double)N_pld_int;
	double	N_TCB = (double)N_TCB_int;
	double	N_CW = 0., N_shrt = 0., N_spcw = 0., N_fshrt = 0.;
	double	L_LDPC = 0., K_LDPC = 0., L_LDPC_info = 0.;
	double	N_punc = 0., N_ppcw = 0., N_fpunc = 0., N_rep = 0., N_rpcw = 0., N_frep = 0.;
	double	R_eff = 0.;
	u32	VHTSIGA2B3  = 0;/* extra symbol from VHT-SIG-A2 Bit 3*/

	if (R == 0)
		CR	= 0.5;
	else if (R == 1)
		CR = 2. / 3.;
	else if (R == 2)
		CR = 3. / 4.;
	else if (R == 3)
		CR = 5. / 6.;

	if (N_TCB <= 648.) {
		N_CW	= 1.;
		if (N_TCB >= N_pld + 912.*(1. - CR))
			L_LDPC	= 1296.;
		else
			L_LDPC	= 648.;
	} else if (N_TCB <= 1296.) {
		N_CW	= 1.;
		if (N_TCB >= (double)N_pld + 1464.*(1. - CR))
			L_LDPC	= 1944.;
		else
			L_LDPC	= 1296.;
	} else if	(N_TCB <= 1944.) {
		N_CW	= 1.;
		L_LDPC	= 1944.;
	} else if (N_TCB <= 2592.) {
		N_CW	= 2.;
		if (N_TCB >= N_pld + 2916.*(1. - CR))
			L_LDPC	= 1944.;
		else
			L_LDPC	= 1296.;
	} else {
		N_CW = ceil(N_pld / 1944. / CR);
		L_LDPC	= 1944.;
	}
	/*	Number of information bits per CW*/
	K_LDPC = L_LDPC * CR;
	/*	Number of shortening bits					max(0, (N_CW * L_LDPC * R) - N_pld)*/
	N_shrt = (N_CW * K_LDPC - N_pld) > 0. ? (N_CW * K_LDPC - N_pld) : 0.;
	/*	Number of shortening bits per CW			N_spcw = rtfloor(N_shrt/N_CW)*/
	N_spcw = rtfloor(N_shrt / N_CW);
	/*	The first N_fshrt CWs shorten 1 bit more*/
	N_fshrt = (double)((int)N_shrt % (int)N_CW);
	/*	Number of data bits for the last N_CW-N_fshrt CWs*/
	L_LDPC_info = K_LDPC - N_spcw;
	/*	Number of puncturing bits*/
	N_punc = (N_CW * L_LDPC - N_TCB - N_shrt) > 0. ? (N_CW * L_LDPC - N_TCB - N_shrt) : 0.;
	if (((N_punc > .1 * N_CW * L_LDPC * (1. - CR)) && (N_shrt < 1.2 * N_punc * CR / (1. - CR))) ||
	    (N_punc > 0.3 * N_CW * L_LDPC * (1. - CR))) {
		/*cout << "*** N_TCB and N_punc are Recomputed ***" << endl;*/
		VHTSIGA2B3 = 1;
		N_TCB += (double)N_CBPSS * N_SS * m_STBC;
		N_punc = (N_CW * L_LDPC - N_TCB - N_shrt) > 0. ? (N_CW * L_LDPC - N_TCB - N_shrt) : 0.;
	} else
		VHTSIGA2B3 = 0;

	return VHTSIGA2B3;
}	/* function end of LDPC_parameter_generator */

/*========================================
	Data field of PPDU
	Get N_sym and SIGA2BB3
========================================*/
void PMAC_Nsym_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo)
{
	u32	SIGA2B3 = 0;
	u8	TX_RATE = pPMacTxInfo->TX_RATE;

	u32 R, R_list[10] = {0, 0, 2, 0, 2, 1, 2, 3, 2, 3};
	double CR = 0;
	u32 N_SD, N_BPSC_list[10] = {1, 2, 2, 4, 4, 6, 6, 6, 8, 8};
	u32 N_BPSC = 0, N_CBPS = 0, N_DBPS = 0, N_ES = 0, N_SYM = 0, N_pld = 0, N_TCB = 0;
	int D_R = 0;

	RTW_INFO("TX_RATE = %d\n", TX_RATE);
	/*	N_SD*/
	if (pPMacTxInfo->BandWidth == 0)
		N_SD = 52;
	else if (pPMacTxInfo->BandWidth == 1)
		N_SD = 108;
	else
		N_SD = 234;

	if (MPT_IS_HT_RATE(TX_RATE)) {
		u8 MCS_temp;

		if (pPMacPktInfo->MCS > 23)
			MCS_temp = pPMacPktInfo->MCS - 24;
		else if (pPMacPktInfo->MCS > 15)
			MCS_temp = pPMacPktInfo->MCS - 16;
		else if (pPMacPktInfo->MCS > 7)
			MCS_temp = pPMacPktInfo->MCS - 8;
		else
			MCS_temp = pPMacPktInfo->MCS;

		R = R_list[MCS_temp];

		switch (R) {
		case 0:
			CR = .5;
			break;
		case 1:
			CR = 2. / 3.;
			break;
		case 2:
			CR = 3. / 4.;
			break;
		case 3:
			CR = 5. / 6.;
			break;
		}

		N_BPSC = N_BPSC_list[MCS_temp];
		N_CBPS = N_BPSC * N_SD * pPMacPktInfo->Nss;
		N_DBPS = (u32)((double)N_CBPS * CR);

		if (pPMacTxInfo->bLDPC == FALSE) {
			N_ES = (u32)ceil((double)(N_DBPS * pPMacPktInfo->Nss) / 4. / 300.);
			RTW_INFO("N_ES = %d\n", N_ES);

			/*	N_SYM = m_STBC* (8*length+16+6*N_ES) / (m_STBC*N_DBPS)*/
			N_SYM = pPMacTxInfo->m_STBC * (u32)ceil((double)(pPMacTxInfo->PacketLength * 8 + 16 + N_ES * 6) /
					(double)(N_DBPS * pPMacTxInfo->m_STBC));

		} else {
			N_ES = 1;
			/*	N_pld = length * 8 + 16*/
			N_pld = pPMacTxInfo->PacketLength * 8 + 16;
			RTW_INFO("N_pld = %d\n", N_pld);
			N_SYM = pPMacTxInfo->m_STBC * (u32)ceil((double)(N_pld) /
					(double)(N_DBPS * pPMacTxInfo->m_STBC));
			RTW_INFO("N_SYM = %d\n", N_SYM);
			/*	N_avbits = N_CBPS *m_STBC *(N_pld/N_CBPS*R*m_STBC)*/
			N_TCB = N_CBPS * N_SYM;
			RTW_INFO("N_TCB = %d\n", N_TCB);
			SIGA2B3 = LDPC_parameter_generator(N_pld, N_CBPS, pPMacPktInfo->Nss, R, pPMacTxInfo->m_STBC, N_TCB);
			RTW_INFO("SIGA2B3 = %d\n", SIGA2B3);
			N_SYM = N_SYM + SIGA2B3 * pPMacTxInfo->m_STBC;
			RTW_INFO("N_SYM = %d\n", N_SYM);
		}
	} else if (MPT_IS_VHT_RATE(TX_RATE)) {
		R = R_list[pPMacPktInfo->MCS];

		switch (R) {
		case 0:
			CR = .5;
			break;
		case 1:
			CR = 2. / 3.;
			break;
		case 2:
			CR = 3. / 4.;
			break;
		case 3:
			CR = 5. / 6.;
			break;
		}
		N_BPSC = N_BPSC_list[pPMacPktInfo->MCS];
		N_CBPS = N_BPSC * N_SD * pPMacPktInfo->Nss;
		N_DBPS = (u32)((double)N_CBPS * CR);
		if (pPMacTxInfo->bLDPC == FALSE) {
			if (pPMacTxInfo->bSGI)
				N_ES = (u32)ceil((double)(N_DBPS) / 3.6 / 600.);
			else
				N_ES = (u32)ceil((double)(N_DBPS) / 4. / 600.);
			/*	N_SYM = m_STBC* (8*length+16+6*N_ES) / (m_STBC*N_DBPS)*/
			N_SYM = pPMacTxInfo->m_STBC * (u32)ceil((double)(pPMacTxInfo->PacketLength * 8 + 16 + N_ES * 6) / (double)(N_DBPS * pPMacTxInfo->m_STBC));
			SIGA2B3 = 0;
		} else {
			N_ES = 1;
			/*	N_SYM = m_STBC* (8*length+N_service) / (m_STBC*N_DBPS)*/
			N_SYM = pPMacTxInfo->m_STBC * (u32)ceil((double)(pPMacTxInfo->PacketLength * 8 + 16) / (double)(N_DBPS * pPMacTxInfo->m_STBC));
			/*	N_avbits = N_sys_init * N_CBPS*/
			N_TCB = N_CBPS * N_SYM;
			/*	N_pld = N_sys_init * N_DBPS*/
			N_pld = N_SYM * N_DBPS;
			SIGA2B3 = LDPC_parameter_generator(N_pld, N_CBPS, pPMacPktInfo->Nss, R, pPMacTxInfo->m_STBC, N_TCB);
			N_SYM = N_SYM + SIGA2B3 * pPMacTxInfo->m_STBC;
		}

		switch (R) {
		case 0:
			D_R = 2;
			break;
		case 1:
			D_R = 3;
			break;
		case 2:
			D_R = 4;
			break;
		case 3:
			D_R = 6;
			break;
		}

		if (((N_CBPS / N_ES) % D_R) != 0) {
			RTW_INFO("MCS= %d is not supported when Nss=%d and BW= %d !!\n",  pPMacPktInfo->MCS, pPMacPktInfo->Nss, pPMacTxInfo->BandWidth);
			return;
		}

		RTW_INFO("MCS= %d Nss=%d and BW= %d !!\n",  pPMacPktInfo->MCS, pPMacPktInfo->Nss, pPMacTxInfo->BandWidth);
	}

	pPMacPktInfo->N_sym = N_SYM;
	pPMacPktInfo->SIGA2B3 = SIGA2B3;
}

/*========================================
	L-SIG	Rate	R	Length	P	Tail
			4b		1b	12b		1b	6b
========================================*/

void L_SIG_generator(
	u32	N_SYM,		/* Max: 750*/
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo)
{
	u8	sig_bi[24] = {0};	/* 24 BIT*/
	u32	mode, LENGTH;
	int i;

	if (MPT_IS_OFDM_RATE(pPMacTxInfo->TX_RATE)) {
		mode = pPMacPktInfo->MCS;
		LENGTH = pPMacTxInfo->PacketLength;
	} else {
		u8	N_LTF;
		double	T_data;
		u32	OFDM_symbol;

		mode = 0;

		/*	Table 20-13 Num of HT-DLTFs request*/
		if (pPMacPktInfo->Nsts <= 2)
			N_LTF = pPMacPktInfo->Nsts;
		else
			N_LTF = 4;

		if (pPMacTxInfo->bSGI)
			T_data = 3.6;
		else
			T_data = 4.0;

		/*(L-SIG, HT-SIG, HT-STF, HT-LTF....HT-LTF, Data)*/
		if (MPT_IS_VHT_RATE(pPMacTxInfo->TX_RATE))
			OFDM_symbol = (u32)ceil((double)(8 + 4 + N_LTF * 4 + N_SYM * T_data + 4) / 4.);
		else
			OFDM_symbol = (u32)ceil((double)(8 + 4 + N_LTF * 4 + N_SYM * T_data) / 4.);

		RTW_INFO("%s , OFDM_symbol =%d\n", __func__, OFDM_symbol);
		LENGTH = OFDM_symbol * 3 - 3;
		RTW_INFO("%s , LENGTH =%d\n", __func__, LENGTH);

	}
	/*	Rate Field*/
	switch (mode) {
	case	0:
		sig_bi[0] = 1;
		sig_bi[1] = 1;
		sig_bi[2] = 0;
		sig_bi[3] = 1;
		break;
	case	1:
		sig_bi[0] = 1;
		sig_bi[1] = 1;
		sig_bi[2] = 1;
		sig_bi[3] = 1;
		break;
	case	2:
		sig_bi[0] = 0;
		sig_bi[1] = 1;
		sig_bi[2] = 0;
		sig_bi[3] = 1;
		break;
	case	3:
		sig_bi[0] = 0;
		sig_bi[1] = 1;
		sig_bi[2] = 1;
		sig_bi[3] = 1;
		break;
	case	4:
		sig_bi[0] = 1;
		sig_bi[1] = 0;
		sig_bi[2] = 0;
		sig_bi[3] = 1;
		break;
	case	5:
		sig_bi[0] = 1;
		sig_bi[1] = 0;
		sig_bi[2] = 1;
		sig_bi[3] = 1;
		break;
	case	6:
		sig_bi[0] = 0;
		sig_bi[1] = 0;
		sig_bi[2] = 0;
		sig_bi[3] = 1;
		break;
	case	7:
		sig_bi[0] = 0;
		sig_bi[1] = 0;
		sig_bi[2] = 1;
		sig_bi[3] = 1;
		break;
	}
	/*Reserved bit*/
	sig_bi[4] = 0;

	/*	Length Field*/
	for (i = 0; i < 12; i++)
		sig_bi[i + 5] = (LENGTH >> i) & 1;

	/* Parity Bit*/
	sig_bi[17] = 0;
	for (i = 0; i < 17; i++)
		sig_bi[17] = sig_bi[17] + sig_bi[i];

	sig_bi[17] %= 2;

	/*	Tail Field*/
	for (i = 18; i < 24; i++)
		sig_bi[i] = 0;

	/* dump_buf(sig_bi,24);*/
	_rtw_memset(pPMacTxInfo->LSIG, 0, 3);
	ByteToBit(pPMacTxInfo->LSIG, (bool *)sig_bi, 3);
}


void CRC8_generator(
	bool	*out,
	bool	*in,
	u8	in_size
)
{
	u8 i = 0;
	bool temp = 0, reg[] = {1, 1, 1, 1, 1, 1, 1, 1};

	for (i = 0; i < in_size; i++) { /* take one's complement and bit reverse*/
		temp = in[i] ^ reg[7];
		reg[7]	= reg[6];
		reg[6]	= reg[5];
		reg[5]	= reg[4];
		reg[4]	= reg[3];
		reg[3]	= reg[2];
		reg[2]	= reg[1] ^ temp;
		reg[1]	= reg[0] ^ temp;
		reg[0]	= temp;
	}
	for (i = 0; i < 8; i++)/* take one's complement and bit reverse*/
		out[i] = reg[7 - i] ^ 1;
}

/*/================================================================================
	HT-SIG1	MCS	CW	Length		24BIT + 24BIT
			7b	1b	16b
	HT-SIG2	Smoothing	Not sounding	Rsvd		AGG	STBC	FEC	SGI	N_ELTF	CRC	Tail
			1b			1b			1b		1b	2b		1b	1b	2b		8b	6b
================================================================================*/
void HT_SIG_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
)
{
	u32 i;
	bool sig_bi[48] = {0}, crc8[8] = {0};
	/*	MCS Field*/
	for (i = 0; i < 7; i++)
		sig_bi[i] = (pPMacPktInfo->MCS >> i) & 0x1;
	/*	Packet BW Setting*/
	sig_bi[7] = pPMacTxInfo->BandWidth;
	/*	HT-Length Field*/
	for (i = 0; i < 16; i++)
		sig_bi[i + 8] = (pPMacTxInfo->PacketLength >> i) & 0x1;
	/*	Smoothing;	1->allow smoothing*/
	sig_bi[24] = 1;
	/*Not Sounding*/
	sig_bi[25] = 1 - pPMacTxInfo->NDP_sound;
	/*Reserved bit*/
	sig_bi[26] = 1;
	/*/Aggregate*/
	sig_bi[27] = 0;
	/*STBC Field*/
	if (pPMacTxInfo->bSTBC) {
		sig_bi[28] = 1;
		sig_bi[29] = 0;
	} else {
		sig_bi[28] = 0;
		sig_bi[29] = 0;
	}
	/*Advance Coding,	0: BCC, 1: LDPC*/
	sig_bi[30] = pPMacTxInfo->bLDPC;
	/* Short GI*/
	sig_bi[31] = pPMacTxInfo->bSGI;
	/* N_ELTFs*/
	if (pPMacTxInfo->NDP_sound == FALSE) {
		sig_bi[32]	= 0;
		sig_bi[33]	= 0;
	} else {
		int	N_ELTF = pPMacTxInfo->Ntx - pPMacPktInfo->Nss;

		for (i = 0; i < 2; i++)
			sig_bi[32 + i] = (N_ELTF >> i) % 2;
	}
	/*	CRC-8*/
	CRC8_generator(crc8, sig_bi, 34);

	for (i = 0; i < 8; i++)
		sig_bi[34 + i] = crc8[i];

	/*Tail*/
	for (i = 42; i < 48; i++)
		sig_bi[i] = 0;

	_rtw_memset(pPMacTxInfo->HT_SIG, 0, 6);
	ByteToBit(pPMacTxInfo->HT_SIG, sig_bi, 6);
}


/*======================================================================================
	VHT-SIG-A1
	BW	Reserved	STBC	G_ID	SU_Nsts	P_AID	TXOP_PS_NOT_ALLOW	Reserved
	2b	1b			1b		6b	3b	9b		1b		2b					1b
	VHT-SIG-A2
	SGI	SGI_Nsym	SU/MU coding	LDPC_Extra	SU_NCS	Beamformed	Reserved	CRC	Tail
	1b	1b			1b				1b			4b		1b			1b			8b	6b
======================================================================================*/
void VHT_SIG_A_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo)
{
	u32 i;
	bool sig_bi[48], crc8[8];

	_rtw_memset(sig_bi, 0, 48);
	_rtw_memset(crc8, 0, 8);

	/*	BW Setting*/
	for (i = 0; i < 2; i++)
		sig_bi[i] = (pPMacTxInfo->BandWidth >> i) & 0x1;
	/* Reserved Bit*/
	sig_bi[2] = 1;
	/*STBC Field*/
	sig_bi[3] = pPMacTxInfo->bSTBC;
	/*Group ID: Single User->A value of 0 or 63 indicates an SU PPDU. */
	for (i = 0; i < 6; i++)
		sig_bi[4 + i] = 0;
	/*	N_STS/Partial AID*/
	for (i = 0; i < 12; i++) {
		if (i < 3)
			sig_bi[10 + i] = ((pPMacPktInfo->Nsts - 1) >> i) & 0x1;
		else
			sig_bi[10 + i] = 0;
	}
	/*TXOP_PS_NOT_ALLPWED*/
	sig_bi[22]	= 0;
	/*Reserved Bits*/
	sig_bi[23]	= 1;
	/*Short GI*/
	sig_bi[24] = pPMacTxInfo->bSGI;
	if (pPMacTxInfo->bSGI > 0 && (pPMacPktInfo->N_sym % 10) == 9)
		sig_bi[25] = 1;
	else
		sig_bi[25] = 0;
	/* SU/MU[0] Coding*/
	sig_bi[26] = pPMacTxInfo->bLDPC;	/*	0:BCC, 1:LDPC		*/
	sig_bi[27] = pPMacPktInfo->SIGA2B3;	/*/	Record Extra OFDM Symols is added or not when LDPC is used*/
	/*SU MCS/MU[1-3] Coding*/
	for (i = 0; i < 4; i++)
		sig_bi[28 + i] = (pPMacPktInfo->MCS >> i) & 0x1;
	/*SU Beamform */
	sig_bi[32] = 0;	/*packet.TXBF_en;*/
	/*Reserved Bit*/
	sig_bi[33] = 1;
	/*CRC-8*/
	CRC8_generator(crc8, sig_bi, 34);
	for (i = 0; i < 8; i++)
		sig_bi[34 + i]	= crc8[i];
	/*Tail*/
	for (i = 42; i < 48; i++)
		sig_bi[i] = 0;

	_rtw_memset(pPMacTxInfo->VHT_SIG_A, 0, 6);
	ByteToBit(pPMacTxInfo->VHT_SIG_A, sig_bi, 6);
}

/*======================================================================================
	VHT-SIG-B
	Length				Resesrved	Trail
	17/19/21 BIT		3/2/2 BIT	6b
======================================================================================*/
void VHT_SIG_B_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo)
{
	bool sig_bi[32], crc8_bi[8];
	u32 i, len, res, tail = 6, total_len, crc8_in_len;
	u32 sigb_len;

	_rtw_memset(sig_bi, 0, 32);
	_rtw_memset(crc8_bi, 0, 8);

	/*Sounding Packet*/
	if (pPMacTxInfo->NDP_sound == 1) {
		if (pPMacTxInfo->BandWidth == 0) {
			bool sigb_temp[26] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};

			_rtw_memcpy(sig_bi, sigb_temp, 26);
		} else if (pPMacTxInfo->BandWidth == 1) {
			bool sigb_temp[27] = {1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0};

			_rtw_memcpy(sig_bi, sigb_temp, 27);
		} else if (pPMacTxInfo->BandWidth == 2) {
			bool sigb_temp[29] = {0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};

			_rtw_memcpy(sig_bi, sigb_temp, 29);
		}
	} else {	/* Not NDP Sounding*/
		bool *sigb_temp[29] = {0};

		if (pPMacTxInfo->BandWidth == 0) {
			len = 17;
			res = 3;
		} else if (pPMacTxInfo->BandWidth == 1) {
			len = 19;
			res = 2;
		} else if (pPMacTxInfo->BandWidth == 2) {
			len	= 21;
			res	= 2;
		} else {
			len	= 21;
			res	= 2;
		}
		total_len = len + res + tail;
		crc8_in_len = len + res;

		/*Length Field*/
		sigb_len = (pPMacTxInfo->PacketLength + 3) >> 2;

		for (i = 0; i < len; i++)
			sig_bi[i] = (sigb_len >> i) & 0x1;
		/*Reserved Field*/
		for (i = 0; i < res; i++)
			sig_bi[len + i] = 1;
		/* CRC-8*/
		CRC8_generator(crc8_bi, sig_bi, crc8_in_len);

		/* Tail */
		for (i = 0; i < tail; i++)
			sig_bi[len + res + i] = 0;
	}

	_rtw_memset(pPMacTxInfo->VHT_SIG_B, 0, 4);
	ByteToBit(pPMacTxInfo->VHT_SIG_B, sig_bi, 4);

	pPMacTxInfo->VHT_SIG_B_CRC = 0;
	ByteToBit(&(pPMacTxInfo->VHT_SIG_B_CRC), crc8_bi, 1);
}

/*=======================
 VHT Delimiter
=======================*/
void VHT_Delimiter_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo
)
{
	bool sig_bi[32] = {0}, crc8[8] = {0};
	u32 crc8_in_len = 16;
	u32 PacketLength = pPMacTxInfo->PacketLength;
	int j;

	/* Delimiter[0]: EOF*/
	sig_bi[0] = 1;
	/* Delimiter[1]: Reserved*/
	sig_bi[1] = 0;
	/* Delimiter[3:2]: MPDU Length High*/
	sig_bi[2] = ((PacketLength - 4) >> 12) % 2;
	sig_bi[3] = ((PacketLength - 4) >> 13) % 2;
	/* Delimiter[15:4]: MPDU Length Low*/
	for (j = 4; j < 16; j++)
		sig_bi[j] = ((PacketLength - 4) >> (j - 4)) % 2;
	CRC8_generator(crc8, sig_bi, crc8_in_len);
	for (j = 16; j < 24; j++) /* Delimiter[23:16]: CRC 8*/
		sig_bi[j] = crc8[j - 16];
	for (j = 24; j < 32; j++) /* Delimiter[31:24]: Signature ('4E' in Hex, 78 in Dec)*/
		sig_bi[j]	= (78 >> (j - 24)) % 2;

	_rtw_memset(pPMacTxInfo->VHT_Delimiter, 0, 4);
	ByteToBit(pPMacTxInfo->VHT_Delimiter, sig_bi, 4);
}

#endif
#endif
