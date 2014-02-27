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
#define _RTW_MP_C_

#include <drv_types.h>

#ifdef PLATFORM_FREEBSD
#include <sys/unistd.h>		/* for RFHIGHPID */
#endif

#ifdef CONFIG_RTL8712
#include <rtw_mp_phy_regdef.h>
#endif
#ifdef CONFIG_RTL8192C
#include <rtl8192c_hal.h>
#endif
#ifdef CONFIG_RTL8192D
#include <rtl8192d_hal.h>
#endif
#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
#endif


#ifdef CONFIG_MP_INCLUDED

u32 read_macreg(_adapter *padapter, u32 addr, u32 sz)
{
	u32 val = 0;

	switch(sz)
	{
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
	switch(sz)
	{
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
	return rtw_hal_read_rfreg(padapter, (RF_RADIO_PATH_E)rfpath, addr, bitmask);
}

void _write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 bitmask, u32 val)
{
	rtw_hal_write_rfreg(padapter, (RF_RADIO_PATH_E)rfpath, addr, bitmask, val);
}

u32 read_rfreg(PADAPTER padapter, u8 rfpath, u32 addr)
{
	return _read_rfreg(padapter, (RF_RADIO_PATH_E)rfpath, addr, bRFRegOffsetMask);
}

void write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 val)
{
	_write_rfreg(padapter, (RF_RADIO_PATH_E)rfpath, addr, bRFRegOffsetMask, val);
}

static void _init_mp_priv_(struct mp_priv *pmp_priv)
{
	WLAN_BSSID_EX *pnetwork;

	_rtw_memset(pmp_priv, 0, sizeof(struct mp_priv));

	pmp_priv->mode = MP_OFF;

	pmp_priv->channel = 1;
	pmp_priv->bandwidth = HT_CHANNEL_WIDTH_20;
	pmp_priv->prime_channel_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pmp_priv->rateidx = MPT_RATE_1M;
	pmp_priv->txpoweridx = 0x2A;

	pmp_priv->antenna_tx = ANTENNA_A;
	pmp_priv->antenna_rx = ANTENNA_AB;

	pmp_priv->check_mp_pkt = 0;

	pmp_priv->tx_pktcount = 0;

	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_crcerrpktcount = 0;

	pmp_priv->network_macaddr[0] = 0x00;
	pmp_priv->network_macaddr[1] = 0xE0;
	pmp_priv->network_macaddr[2] = 0x4C;
	pmp_priv->network_macaddr[3] = 0x87;
	pmp_priv->network_macaddr[4] = 0x66;
	pmp_priv->network_macaddr[5] = 0x55;

	pnetwork = &pmp_priv->mp_network.network;
	_rtw_memcpy(pnetwork->MacAddress, pmp_priv->network_macaddr, ETH_ALEN);

	pnetwork->Ssid.SsidLength = 8;
	_rtw_memcpy(pnetwork->Ssid.Ssid, "mp_871x", pnetwork->Ssid.SsidLength);
}

#ifdef PLATFORM_WINDOWS
/*
void mp_wi_callback(
	IN NDIS_WORK_ITEM*	pwk_item,
	IN PVOID			cntx
	)
{
	_adapter* padapter =(_adapter *)cntx;
	struct mp_priv *pmppriv=&padapter->mppriv;
	struct mp_wi_cntx	*pmp_wi_cntx=&pmppriv->wi_cntx;

	// Execute specified action.
	if(pmp_wi_cntx->curractfunc != NULL)
	{
		LARGE_INTEGER	cur_time;
		ULONGLONG start_time, end_time;
		NdisGetCurrentSystemTime(&cur_time);	// driver version
		start_time = cur_time.QuadPart/10; // The return value is in microsecond

		pmp_wi_cntx->curractfunc(padapter);

		NdisGetCurrentSystemTime(&cur_time);	// driver version
		end_time = cur_time.QuadPart/10; // The return value is in microsecond

		RT_TRACE(_module_mp_, _drv_info_,
			 ("WorkItemActType: %d, time spent: %I64d us\n",
			  pmp_wi_cntx->param.act_type, (end_time-start_time)));
	}

	NdisAcquireSpinLock(&(pmp_wi_cntx->mp_wi_lock));
	pmp_wi_cntx->bmp_wi_progress= _FALSE;
	NdisReleaseSpinLock(&(pmp_wi_cntx->mp_wi_lock));

	if (pmp_wi_cntx->bmpdrv_unload)
	{
		NdisSetEvent(&(pmp_wi_cntx->mp_wi_evt));
	}

}
*/

static int init_mp_priv_by_os(struct mp_priv *pmp_priv)
{
	struct mp_wi_cntx *pmp_wi_cntx;

	if (pmp_priv == NULL) return _FAIL;

	pmp_priv->rx_testcnt = 0;
	pmp_priv->rx_testcnt1 = 0;
	pmp_priv->rx_testcnt2 = 0;

	pmp_priv->tx_testcnt = 0;
	pmp_priv->tx_testcnt1 = 0;

	pmp_wi_cntx = &pmp_priv->wi_cntx
	pmp_wi_cntx->bmpdrv_unload = _FALSE;
	pmp_wi_cntx->bmp_wi_progress = _FALSE;
	pmp_wi_cntx->curractfunc = NULL;

	return _SUCCESS;
}
#endif

#ifdef PLATFORM_LINUX
static int init_mp_priv_by_os(struct mp_priv *pmp_priv)
{
	int i, res;
	struct mp_xmit_frame *pmp_xmitframe;

	if (pmp_priv == NULL) return _FAIL;

	_rtw_init_queue(&pmp_priv->free_mp_xmitqueue);

	pmp_priv->pallocated_mp_xmitframe_buf = NULL;
	pmp_priv->pallocated_mp_xmitframe_buf = rtw_zmalloc(NR_MP_XMITFRAME * sizeof(struct mp_xmit_frame) + 4);
	if (pmp_priv->pallocated_mp_xmitframe_buf == NULL) {
		res = _FAIL;
		goto _exit_init_mp_priv;
	}

	pmp_priv->pmp_xmtframe_buf = pmp_priv->pallocated_mp_xmitframe_buf + 4 - ((uint) (pmp_priv->pallocated_mp_xmitframe_buf) & 3);

	pmp_xmitframe = (struct mp_xmit_frame*)pmp_priv->pmp_xmtframe_buf;

	for (i = 0; i < NR_MP_XMITFRAME; i++)
	{
		_rtw_init_listhead(&pmp_xmitframe->list);
		rtw_list_insert_tail(&pmp_xmitframe->list, &pmp_priv->free_mp_xmitqueue.queue);

		pmp_xmitframe->pkt = NULL;
		pmp_xmitframe->frame_tag = MP_FRAMETAG;
		pmp_xmitframe->padapter = pmp_priv->papdater;

		pmp_xmitframe++;
	}

	pmp_priv->free_mp_xmitframe_cnt = NR_MP_XMITFRAME;

	res = _SUCCESS;

_exit_init_mp_priv:

	return res;
}
#endif

static void mp_init_xmit_attrib(struct mp_tx *pmptx, PADAPTER padapter)
{
	struct pkt_attrib *pattrib;
	struct tx_desc *desc;

	// init xmitframe attribute
	pattrib = &pmptx->attrib;
	_rtw_memset(pattrib, 0, sizeof(struct pkt_attrib));
	desc = &pmptx->desc;
	_rtw_memset(desc, 0, TXDESC_SIZE);

	pattrib->ether_type = 0x8712;
	//_rtw_memcpy(pattrib->src, padapter->eeprompriv.mac_addr, ETH_ALEN);
//	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	_rtw_memset(pattrib->dst, 0xFF, ETH_ALEN);
//	pattrib->pctrl = 0;
//	pattrib->dhcp_pkt = 0;
//	pattrib->pktlen = 0;
	pattrib->ack_policy = 0;
//	pattrib->pkt_hdrlen = ETH_HLEN;
	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA;
	pattrib->priority = 0;
	pattrib->qsel = pattrib->priority;
//	do_queue_select(padapter, pattrib);
	pattrib->nr_frags = 1;
	pattrib->encrypt = 0;
	pattrib->bswenc = _FALSE;
	pattrib->qos_en = _FALSE;
}

s32 init_mp_priv(PADAPTER padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;

	_init_mp_priv_(pmppriv);
	pmppriv->papdater = padapter;

	pmppriv->tx.stop = 1;
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
		case RF_2T2R_GREEN:
			pmppriv->antenna_tx = ANTENNA_AB;
			pmppriv->antenna_rx = ANTENNA_AB;
			break;
		case RF_2T4R:
			pmppriv->antenna_tx = ANTENNA_AB;
			pmppriv->antenna_rx = ANTENNA_ABCD;
			break;
	}

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

#ifdef CONFIG_RTL8192C
#define PHY_IQCalibrate(a,b)	rtl8192c_PHY_IQCalibrate(a,b)
#define PHY_LCCalibrate(a)	rtl8192c_PHY_LCCalibrate(a)
#define dm_CheckTXPowerTracking(a)	rtl8192c_dm_CheckTXPowerTracking(a)
#define PHY_SetRFPathSwitch(a,b)	rtl8192c_PHY_SetRFPathSwitch(a,b)
#endif

#ifdef CONFIG_RTL8192D
#define PHY_IQCalibrate(a)	rtl8192d_PHY_IQCalibrate(a)
#define PHY_LCCalibrate(a)	rtl8192d_PHY_LCCalibrate(a, _TRUE)
#define dm_CheckTXPowerTracking(a)	rtl8192d_dm_CheckTXPowerTracking(a)
#define PHY_SetRFPathSwitch(a,b)	rtl8192d_PHY_SetRFPathSwitch(a,b)
#endif

s32
MPT_InitializeAdapter(
	IN	PADAPTER			pAdapter,
	IN	u8				Channel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	s32		rtStatus = _SUCCESS;
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;
	u32		ledsetting;

	//-------------------------------------------------------------------------
	// HW Initialization for 8190 MPT.
	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	// SW Initialization for 8190 MP.
	//-------------------------------------------------------------------------
	pMptCtx->bMptDrvUnload = _FALSE;
	pMptCtx->bMassProdTest = _FALSE;
	pMptCtx->bMptIndexEven = _TRUE;	//default gain index is -6.0db

	/* Init mpt event. */
#if 0 // for Windows
	NdisInitializeEvent( &(pMptCtx->MptWorkItemEvent) );
	NdisAllocateSpinLock( &(pMptCtx->MptWorkItemSpinLock) );

	PlatformInitializeWorkItem(
		Adapter,
		&(pMptCtx->MptWorkItem),
		(RT_WORKITEM_CALL_BACK)MPT_WorkItemCallback,
		(PVOID)Adapter,
		"MptWorkItem");
#endif
	pMptCtx->bMptWorkItemInProgress = _FALSE;
	pMptCtx->CurrMptAct = NULL;
	//-------------------------------------------------------------------------

#if 1
	// Don't accept any packets
	rtw_write32(pAdapter, REG_RCR, 0);
#else
	// Accept CRC error and destination address
	pHalData->ReceiveConfig |= (RCR_ACRC32|RCR_AAP);
	rtw_write32(pAdapter, REG_RCR, pHalData->ReceiveConfig);
#endif

#if 0
	// If EEPROM or EFUSE is empty,we assign as RF 2T2R for MP.
	if (pHalData->AutoloadFailFlag == TRUE)
	{
		pHalData->RF_Type = RF_2T2R;
	}
#endif
	ledsetting = rtw_read32(pAdapter, REG_LEDCFG0);
	rtw_write32(pAdapter, REG_LEDCFG0, ledsetting & ~LED0DIS);

#ifdef CONFIG_RTL8192C
	PHY_IQCalibrate(pAdapter, _FALSE);
	dm_CheckTXPowerTracking(pAdapter);	//trigger thermal meter
	PHY_LCCalibrate(pAdapter);
#endif

#ifdef CONFIG_RTL8192D
	PHY_IQCalibrate(pAdapter);
	dm_CheckTXPowerTracking(pAdapter);	//trigger thermal meter
	PHY_LCCalibrate(pAdapter);
#endif

#ifdef CONFIG_PCI_HCI
	PHY_SetRFPathSwitch(pAdapter, 1/*pHalData->bDefaultAntenna*/);	//Wifi default use Main
#else

#ifdef CONFIG_RTL8192C
#if 1
	if (pHalData->BoardType == BOARD_MINICARD)
		PHY_SetRFPathSwitch(pAdapter, 1/*pHalData->bDefaultAntenna*/); //default use Main
#else
	if(pAdapter->HalFunc.GetInterfaceSelectionHandler(pAdapter) == INTF_SEL2_MINICARD )
		PHY_SetRFPathSwitch(Adapter, pAdapter->MgntInfo.bDefaultAntenna); //default use Main
#endif

#endif

#endif

	pMptCtx->backup0xc50 = (u1Byte)PHY_QueryBBReg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pMptCtx->backup0xc58 = (u1Byte)PHY_QueryBBReg(pAdapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pMptCtx->backup0xc30 = (u1Byte)PHY_QueryBBReg(pAdapter, rOFDM0_RxDetector1, bMaskByte0);

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
VOID
MPT_DeInitAdapter(
	IN	PADAPTER	pAdapter
	)
{
	PMPT_CONTEXT		pMptCtx = &pAdapter->mppriv.MptCtx;

	pMptCtx->bMptDrvUnload = _TRUE;
#if 0 // for Windows
	PlatformFreeWorkItem( &(pMptCtx->MptWorkItem) );

	while(pMptCtx->bMptWorkItemInProgress)
	{
		if(NdisWaitEvent(&(pMptCtx->MptWorkItemEvent), 50))
		{
			break;
		}
	}
	NdisFreeSpinLock( &(pMptCtx->MptWorkItemSpinLock) );
#endif
}

static u8 mpt_ProStartTest(PADAPTER padapter)
{
	PMPT_CONTEXT pMptCtx = &padapter->mppriv.MptCtx;

	pMptCtx->bMassProdTest = _TRUE;
	pMptCtx->bStartContTx = _FALSE;
	pMptCtx->bCckContTx = _FALSE;
	pMptCtx->bOfdmContTx = _FALSE;
	pMptCtx->bSingleCarrier = _FALSE;
	pMptCtx->bCarrierSuppression = _FALSE;
	pMptCtx->bSingleTone = _FALSE;

	return _SUCCESS;
}

/*
 * General use
 */
s32 SetPowerTracking(PADAPTER padapter, u8 enable)
{

	Hal_SetPowerTracking( padapter, enable );
	return 0;
}

void GetPowerTracking(PADAPTER padapter, u8 *enable)
{
	Hal_GetPowerTracking( padapter, enable );
}

static void disable_dm(PADAPTER padapter)
{
#ifndef CONFIG_RTL8723A
	u8 v8;
#endif
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;


	//3 1. disable firmware dynamic mechanism
	// disable Power Training, Rate Adaptive
#ifdef CONFIG_RTL8723A
	SetBcnCtrlReg(padapter, 0, EN_BCN_FUNCTION);
#else
	v8 = rtw_read8(padapter, REG_BCN_CTRL);
	v8 &= ~EN_BCN_FUNCTION;
	rtw_write8(padapter, REG_BCN_CTRL, v8);
#endif

	//3 2. disable driver dynamic mechanism
	// disable Dynamic Initial Gain
	// disable High Power
	// disable Power Tracking
	Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

	// enable APK, LCK and IQK but disable power tracking
	pdmpriv->TxPowerTrackControl = _FALSE;
	Switch_DM_Func(padapter, DYNAMIC_FUNC_SS, _TRUE);
}

//This function initializes the DUT to the MP test mode
s32 mp_start_test(PADAPTER padapter)
{
	WLAN_BSSID_EX bssid;
	struct sta_info *psta;
	u32 length;
	u8 val8;

	_irqL irqL;
	s32 res = _SUCCESS;

	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;


	//3 disable dynamic mechanism
	disable_dm(padapter);

	//3 0. update mp_priv
#if  defined (CONFIG_RTL8192C)  ||  defined (CONFIG_RTL8192D)
	if (padapter->registrypriv.rf_config == RF_819X_MAX_TYPE) {
//		HAL_DATA_TYPE *phal = GET_HAL_DATA(padapter);
//		switch (phal->rf_type) {
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
			case RF_2T2R_GREEN:
				pmppriv->antenna_tx = ANTENNA_AB;
				pmppriv->antenna_rx = ANTENNA_AB;
				break;
			case RF_2T4R:
				pmppriv->antenna_tx = ANTENNA_AB;
				pmppriv->antenna_rx = ANTENNA_ABCD;
				break;
		}
	}
#endif
	mpt_ProStartTest(padapter);

	//3 1. initialize a new WLAN_BSSID_EX
//	_rtw_memset(&bssid, 0, sizeof(WLAN_BSSID_EX));
	_rtw_memcpy(bssid.MacAddress, pmppriv->network_macaddr, ETH_ALEN);
	bssid.Ssid.SsidLength = strlen("mp_pseudo_adhoc");
	_rtw_memcpy(bssid.Ssid.Ssid, (u8*)"mp_pseudo_adhoc", bssid.Ssid.SsidLength);
	bssid.InfrastructureMode = Ndis802_11IBSS;
	bssid.NetworkTypeInUse = Ndis802_11DS;
	bssid.IELength = 0;

	length = get_WLAN_BSSID_EX_sz(&bssid);
	if (length % 4)
		bssid.Length = ((length >> 2) + 1) << 2; //round up to multiple of 4 bytes.
	else
		bssid.Length = length;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		goto end_of_mp_start_test;

	//init mp_start_test status
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		rtw_disassoc_cmd(padapter, 500, _TRUE);
		rtw_indicate_disconnect(padapter);
		rtw_free_assoc_resources(padapter, 1);
	}
	pmppriv->prev_fw_state = get_fwstate(pmlmepriv);
	pmlmepriv->fw_state = WIFI_MP_STATE;
#if 0
	if (pmppriv->mode == _LOOPBOOK_MODE_) {
		set_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE); //append txdesc
		RT_TRACE(_module_mp_, _drv_notice_, ("+start mp in Lookback mode\n"));
	} else {
		RT_TRACE(_module_mp_, _drv_notice_, ("+start mp in normal mode\n"));
	}
#endif
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	//3 2. create a new psta for mp driver
	//clear psta in the cur_network, if any
	psta = rtw_get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta) rtw_free_stainfo(padapter, psta);

	psta = rtw_alloc_stainfo(&padapter->stapriv, bssid.MacAddress);
	if (psta == NULL) {
		RT_TRACE(_module_mp_, _drv_err_, ("mp_start_test: Can't alloc sta_info!\n"));
		pmlmepriv->fw_state = pmppriv->prev_fw_state;
		res = _FAIL;
		goto end_of_mp_start_test;
	}

	//3 3. join psudo AdHoc
	tgt_network->join_res = 1;
	tgt_network->aid = psta->aid = 1;
	_rtw_memcpy(&tgt_network->network, &bssid, length);

	rtw_indicate_connect(padapter);
	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

end_of_mp_start_test:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	if (res == _SUCCESS)
	{
		// set MSR to WIFI_FW_ADHOC_STATE
#if  defined (CONFIG_RTL8192C)  ||  defined (CONFIG_RTL8192D)
		val8 = rtw_read8(padapter, MSR) & 0xFC; // 0x0102
		val8 |= WIFI_FW_ADHOC_STATE;
		rtw_write8(padapter, MSR, val8); // Link in ad hoc network
#endif

#if  !defined (CONFIG_RTL8192C)  &&  !defined (CONFIG_RTL8192D)
		rtw_write8(padapter, MSR, 1); // Link in ad hoc network
		rtw_write8(padapter, RCR, 0); // RCR : disable all pkt, 0x10250048
		rtw_write8(padapter, RCR+2, 0x57); // RCR disable Check BSSID, 0x1025004a

		// disable RX filter map , mgt frames will put in RX FIFO 0
		rtw_write16(padapter, RXFLTMAP0, 0x0); // 0x10250116

		val8 = rtw_read8(padapter, EE_9346CR); // 0x1025000A
		if (!(val8 & _9356SEL))//boot from EFUSE
			efuse_change_max_size(padapter);
#endif
	}

	return res;
}
//------------------------------------------------------------------------------
//This function change the DUT from the MP test mode into normal mode
void mp_stop_test(PADAPTER padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info *psta;

	_irqL irqL;


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE)
		goto end_of_mp_stop_test;

	//3 1. disconnect psudo AdHoc
	rtw_indicate_disconnect(padapter);

	//3 2. clear psta used in mp test mode.
//	rtw_free_assoc_resources(padapter, 1);
	psta = rtw_get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta) rtw_free_stainfo(padapter, psta);

	//3 3. return to normal state (default:station mode)
	pmlmepriv->fw_state = pmppriv->prev_fw_state; // WIFI_STATION_STATE;

	//flush the cur_network
	_rtw_memset(tgt_network, 0, sizeof(struct wlan_network));

	_clr_fwstate_(pmlmepriv, WIFI_MP_STATE);

end_of_mp_stop_test:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}
/*---------------------------hal\rtl8192c\MPT_Phy.c---------------------------*/
#if 0
//#ifdef CONFIG_USB_HCI
static VOID mpt_AdjustRFRegByRateByChan92CU(PADAPTER pAdapter, u8 RateIdx, u8 Channel, u8 BandWidthID)
{
	u8		eRFPath;
	u32		rfReg0x26;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);


	if (RateIdx < MPT_RATE_6M) {	// CCK rate,for 88cu
		rfReg0x26 = 0xf400;
	}
	else if ((RateIdx >= MPT_RATE_6M) && (RateIdx <= MPT_RATE_54M)) {// OFDM rate,for 88cu
		if ((4 == Channel) || (8 == Channel) || (12 == Channel))
			rfReg0x26 = 0xf000;
		else if ((5 == Channel) || (7 == Channel) || (13 == Channel) || (14 == Channel))
			rfReg0x26 = 0xf400;
		else
			rfReg0x26 = 0x4f200;
	}
	else if ((RateIdx >= MPT_RATE_MCS0) && (RateIdx <= MPT_RATE_MCS15)) {// MCS 20M ,for 88cu // MCS40M rate,for 88cu

		if (HT_CHANNEL_WIDTH_20 == BandWidthID) {
			if ((4 == Channel) || (8 == Channel))
				rfReg0x26 = 0xf000;
			else if ((5 == Channel) || (7 == Channel) || (13 == Channel) || (14 == Channel))
				rfReg0x26 = 0xf400;
			else
				rfReg0x26 = 0x4f200;
		}
		else{
			if ((4 == Channel) || (8 == Channel))
				rfReg0x26 = 0xf000;
			else if ((5 == Channel) || (7 == Channel))
				rfReg0x26 = 0xf400;
			else
				rfReg0x26 = 0x4f200;
		}
	}

//	RT_TRACE(COMP_CMD, DBG_LOUD, ("\n mpt_AdjustRFRegByRateByChan92CU():Chan:%d Rate=%d rfReg0x26:0x%08x\n",Channel, RateIdx,rfReg0x26));
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {
		write_rfreg(pAdapter, eRFPath, RF_SYN_G2, rfReg0x26);
	}
}
#endif
/*-----------------------------------------------------------------------------
 * Function:	mpt_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel/rate/BW for MP.
 *
 * Input:       IN	PADAPTER				pAdapter
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
static void mpt_SwitchRfSetting(PADAPTER pAdapter)
{
	Hal_mpt_SwitchRfSetting(pAdapter);
    }

/*---------------------------hal\rtl8192c\MPT_Phy.c---------------------------*/
/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/
static void MPT_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14)
{
	Hal_MPT_CCKTxPowerAdjust(Adapter,bInCH14);
}

static void MPT_CCKTxPowerAdjustbyIndex(PADAPTER pAdapter, BOOLEAN beven)
{
	Hal_MPT_CCKTxPowerAdjustbyIndex(pAdapter,beven);
	}

/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/

/*
 * SetChannel
 * Description
 *	Use H2C command to change channel,
 *	not only modify rf register, but also other setting need to be done.
 */
void SetChannel(PADAPTER pAdapter)
{
	Hal_SetChannel(pAdapter);

}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void SetBandwidth(PADAPTER pAdapter)
{
	Hal_SetBandwidth(pAdapter);

}

static void SetCCKTxPower(PADAPTER pAdapter, u8 *TxPower)
{
	Hal_SetCCKTxPower(pAdapter,TxPower);
}

static void SetOFDMTxPower(PADAPTER pAdapter, u8 *TxPower)
{
	Hal_SetOFDMTxPower(pAdapter,TxPower);
	}


void SetAntenna(PADAPTER pAdapter)
	{
	Hal_SetAntenna(pAdapter);
}

void	SetAntennaPathPower(PADAPTER pAdapter)
{
	Hal_SetAntennaPathPower(pAdapter);
}
	
void SetTxPower(PADAPTER pAdapter)
{
	Hal_SetTxPower(pAdapter);
	}

void SetTxAGCOffset(PADAPTER pAdapter, u32 ulTxAGCOffset)
{
	u32 TxAGCOffset_B, TxAGCOffset_C, TxAGCOffset_D,tmpAGC;

	TxAGCOffset_B = (ulTxAGCOffset&0x000000ff);
	TxAGCOffset_C = ((ulTxAGCOffset&0x0000ff00)>>8);
	TxAGCOffset_D = ((ulTxAGCOffset&0x00ff0000)>>16);

	tmpAGC = (TxAGCOffset_D<<8 | TxAGCOffset_C<<4 | TxAGCOffset_B);
	write_bbreg(pAdapter, rFPGA0_TxGainStage,
			(bXBTxAGC|bXCTxAGC|bXDTxAGC), tmpAGC);
}

void SetDataRate(PADAPTER pAdapter)
{
	Hal_SetDataRate(pAdapter);
}

#if  !defined (CONFIG_RTL8192C)  &&  !defined (CONFIG_RTL8192D)
/*------------------------------Define structure----------------------------*/
typedef struct _R_ANTENNA_SELECT_OFDM {
	u32	r_tx_antenna:4;
	u32	r_ant_l:4;
	u32	r_ant_non_ht:4;
	u32	r_ant_ht1:4;
	u32	r_ant_ht2:4;
	u32	r_ant_ht_s1:4;
	u32	r_ant_non_ht_s1:4;
	u32	OFDM_TXSC:2;
	u32	Reserved:2;
}R_ANTENNA_SELECT_OFDM;

typedef struct _R_ANTENNA_SELECT_CCK {
	u8	r_cckrx_enable_2:2;
	u8	r_cckrx_enable:2;
	u8	r_ccktx_enable:4;
}R_ANTENNA_SELECT_CCK;
#endif

s32 SetThermalMeter(PADAPTER pAdapter, u8 target_ther)
{
	return Hal_SetThermalMeter( pAdapter, target_ther);
}

static void TriggerRFThermalMeter(PADAPTER pAdapter)
{
	Hal_TriggerRFThermalMeter(pAdapter);
}

static u8 ReadRFThermalMeter(PADAPTER pAdapter)
{
	return Hal_ReadRFThermalMeter(pAdapter);
}

void GetThermalMeter(PADAPTER pAdapter, u8 *value)
{
	Hal_GetThermalMeter(pAdapter,value);
}

void SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
	Hal_SetSingleCarrierTx(pAdapter,bStart);
}

void SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
	Hal_SetSingleToneTx(pAdapter,bStart);
}

void SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
	Hal_SetCarrierSuppressionTx(pAdapter, bStart);
}

void SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart)
		{
	Hal_SetCCKContinuousTx(pAdapter,bStart);
		}

void SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart)
        {
   Hal_SetOFDMContinuousTx( pAdapter, bStart);
}/* mpt_StartOfdmContTx */

void SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	Hal_SetContinuousTx(pAdapter,bStart);
}

//------------------------------------------------------------------------------
static void dump_mpframe(PADAPTER padapter, struct xmit_frame *pmpframe)
{
	rtw_hal_mgnt_xmit(padapter, pmpframe);
}

static struct xmit_frame *alloc_mp_xmitframe(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame	*pmpframe;
	struct xmit_buf	*pxmitbuf;

	if ((pmpframe = rtw_alloc_xmitframe(pxmitpriv)) == NULL)
	{
		return NULL;
	}

	if ((pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv)) == NULL)
	{
		rtw_free_xmitframe(pxmitpriv, pmpframe);
		return NULL;
	}

	pmpframe->frame_tag = MP_FRAMETAG;

	pmpframe->pxmitbuf = pxmitbuf;

	pmpframe->buf_addr = pxmitbuf->pbuf;

	pxmitbuf->priv_data = pmpframe;

	return pmpframe;

}

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

	DBG_871X("%s:pkTx Start\n", __func__);
	while (1) {
		pxmitframe = alloc_mp_xmitframe(pxmitpriv);
		if (pxmitframe == NULL) {
			if (pmptx->stop ||
			    padapter->bSurpriseRemoved ||
			    padapter->bDriverStopped) {
				goto exit;
			}
			else {
				rtw_msleep_os(1);
				continue;
			}
		}

		_rtw_memcpy((u8 *)(pxmitframe->buf_addr+TXDESC_OFFSET), pmptx->buf, pmptx->write_size);
		_rtw_memcpy(&(pxmitframe->attrib), &(pmptx->attrib), sizeof(struct pkt_attrib));

		dump_mpframe(padapter, pxmitframe);

		pmptx->sended++;
		pmp_priv->tx_pktcount++;

		if (pmptx->stop ||
		    padapter->bSurpriseRemoved ||
		    padapter->bDriverStopped)
			goto exit;
		if ((pmptx->count != 0) &&
		    (pmptx->count == pmptx->sended))
			goto exit;

		flush_signals_thread();
	}

exit:
	//DBG_871X("%s:pkTx Exit\n", __func__);
	rtw_mfree(pmptx->pallocated_buf, pmptx->buf_size);
	pmptx->pallocated_buf = NULL;
	pmptx->stop = 1;

	thread_exit();
}

void fill_txdesc_for_mp(PADAPTER padapter, struct tx_desc *ptxdesc)
{		
	struct mp_priv *pmp_priv = &padapter->mppriv;
	_rtw_memcpy(ptxdesc, &(pmp_priv->tx.desc), TXDESC_SIZE);
}

void SetPacketTx(PADAPTER padapter)
{
	u8 *ptr, *pkt_start, *pkt_end;
	u32 pkt_size;
	struct tx_desc *desc;
	struct rtw_ieee80211_hdr *hdr;
	u8 payload;
	s32 bmcast;
	struct pkt_attrib *pattrib;
	struct mp_priv *pmp_priv;


	pmp_priv = &padapter->mppriv;
	if (pmp_priv->tx.stop) return;
	pmp_priv->tx.sended = 0;
	pmp_priv->tx.stop = 0;
	pmp_priv->tx_pktcount = 0;

	//3 1. update_attrib()
	pattrib = &pmp_priv->tx.attrib;
	_rtw_memcpy(pattrib->src, padapter->eeprompriv.mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
	bmcast = IS_MCAST(pattrib->ra);
	if (bmcast) {
		pattrib->mac_id = 1;
		pattrib->psta = rtw_get_bcmc_stainfo(padapter);
	} else {
		pattrib->mac_id = 0;
		pattrib->psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
	}

	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->pktlen;

	//3 2. allocate xmit buffer
	pkt_size = pattrib->last_txcmdsz;

	if (pmp_priv->tx.pallocated_buf)
		rtw_mfree(pmp_priv->tx.pallocated_buf, pmp_priv->tx.buf_size);
	pmp_priv->tx.write_size = pkt_size;
	pmp_priv->tx.buf_size = pkt_size + XMITBUF_ALIGN_SZ;
	pmp_priv->tx.pallocated_buf = rtw_zmalloc(pmp_priv->tx.buf_size);
	if (pmp_priv->tx.pallocated_buf == NULL) {
		DBG_871X("%s: malloc(%d) fail!!\n", __func__, pmp_priv->tx.buf_size);
		return;
	}
	pmp_priv->tx.buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pmp_priv->tx.pallocated_buf), XMITBUF_ALIGN_SZ);
	ptr = pmp_priv->tx.buf;

	desc = &(pmp_priv->tx.desc);
	_rtw_memset(desc, 0, TXDESC_SIZE);
	pkt_start = ptr;
	pkt_end = pkt_start + pkt_size;

	//3 3. init TX descriptor
	// offset 0
	//desc->txdw0 |= cpu_to_le32(pkt_size & 0x0000FFFF); // packet size
	//desc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	//desc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00FF0000); //32 bytes for TX Desc
	//if (bmcast) desc->txdw0 |= cpu_to_le32(BMC); // broadcast packet

	// offset 4
	desc->txdw1 |= cpu_to_le32(BK); // don't aggregate(AMPDU)
	desc->txdw1 |= cpu_to_le32((pattrib->mac_id) & 0x1F); //CAM_ID(MAC_ID)
	desc->txdw1 |= cpu_to_le32((pattrib->qsel << QSEL_SHT) & 0x00001F00); // Queue Select, TID
	desc->txdw1 |= cpu_to_le32((pattrib->raid << Rate_ID_SHT) & 0x000F0000); // Rate Adaptive ID

	// offset 8
	// offset 12
	//desc->txdw3 |= cpu_to_le32((pattrib->seqnum << SEQ_SHT) & 0xffff0000);

	// offset 16
	//desc->txdw4 |= cpu_to_le32(QoS);
	desc->txdw4 |= cpu_to_le32(HW_SEQ_EN);
	desc->txdw4 |= cpu_to_le32(USERATE);
	desc->txdw4 |= cpu_to_le32(DISDATAFB);

	if( pmp_priv->preamble ){
		if (pmp_priv->rateidx <=  MPT_RATE_54M)
			desc->txdw4 |= cpu_to_le32(DATA_SHORT); // CCK Short Preamble
	}
	if (pmp_priv->bandwidth == HT_CHANNEL_WIDTH_40)
		desc->txdw4 |= cpu_to_le32(DATA_BW);

	// offset 20
	desc->txdw5 |= cpu_to_le32(pmp_priv->rateidx & 0x0000001F);

	if( pmp_priv->preamble ){
		if (pmp_priv->rateidx > MPT_RATE_54M)
			desc->txdw5 |= cpu_to_le32(SGI); // MCS Short Guard Interval
	}
	desc->txdw5 |= cpu_to_le32(0x0001FF00); // DATA/RTS Rate Fallback Limit

	//3 4. make wlan header, make_wlanhdr()
	hdr = (struct rtw_ieee80211_hdr *)pkt_start;
	SetFrameSubType(&hdr->frame_ctl, pattrib->subtype);
	_rtw_memcpy(hdr->addr1, pattrib->dst, ETH_ALEN); // DA
	_rtw_memcpy(hdr->addr2, pattrib->src, ETH_ALEN); // SA
	_rtw_memcpy(hdr->addr3, get_bssid(&padapter->mlmepriv), ETH_ALEN); // RA, BSSID

	//3 5. make payload
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

	_rtw_memset(ptr, payload, pkt_end - ptr);

	//3 6. start thread
#ifdef PLATFORM_LINUX
	pmp_priv->tx.PktTxThread = kthread_run(mp_xmit_packet_thread, pmp_priv, "RTW_MP_THREAD");
	if (IS_ERR(pmp_priv->tx.PktTxThread))
		DBG_871X("Create PktTx Thread Fail !!!!!\n");
#endif
#ifdef PLATFORM_FREEBSD
{
	struct proc *p;
	struct thread *td;
	pmp_priv->tx.PktTxThread = kproc_kthread_add(mp_xmit_packet_thread, pmp_priv,
					&p, &td, RFHIGHPID, 0, "MPXmitThread", "MPXmitThread");

	if (pmp_priv->tx.PktTxThread < 0)
		DBG_871X("Create PktTx Thread Fail !!!!!\n");
}
#endif
}

void SetPacketRx(PADAPTER pAdapter, u8 bStartRx)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if(bStartRx)
	{
		pHalData->ReceiveConfig = AAP | APM | AM | AB | APP_ICV | ADF | AMF | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS;
	
		pHalData->ReceiveConfig |= ACRC32;
	
		rtw_write32(pAdapter, REG_RCR, pHalData->ReceiveConfig);

		// Accept all data frames
		rtw_write16(pAdapter, REG_RXFLTMAP2, 0xFFFF);
	}
	else
	{
		rtw_write32(pAdapter, REG_RCR, 0);
	}
}

void ResetPhyRxPktCount(PADAPTER pAdapter)
{
	u32 i, phyrx_set = 0;

	for (i = 0; i <= 0xF; i++) {
		phyrx_set = 0;
		phyrx_set |= _RXERR_RPT_SEL(i);	//select
		phyrx_set |= RXERR_RPT_RST;	// set counter to zero
		rtw_write32(pAdapter, REG_RXERR_RPT, phyrx_set);
	}
}

static u32 GetPhyRxPktCounts(PADAPTER pAdapter, u32 selbit)
{
	//selection
	u32 phyrx_set = 0, count = 0;

	phyrx_set = _RXERR_RPT_SEL(selbit & 0xF);
	rtw_write32(pAdapter, REG_RXERR_RPT, phyrx_set);

	//Read packet count
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

//reg 0x808[9:0]: FFT data x
//reg 0x808[22]:  0  -->  1  to get 1 FFT data y
//reg 0x8B4[15:0]: FFT data y report
static u32 GetPSDData(PADAPTER pAdapter, u32 point)
{
	int psd_val;


	psd_val = rtw_read32(pAdapter, 0x808);
	psd_val &= 0xFFBFFC00;
	psd_val |= point;

	rtw_write32(pAdapter, 0x808, psd_val);
	rtw_mdelay_os(1);
	psd_val |= 0x00400000;

	rtw_write32(pAdapter, 0x808, psd_val);
	rtw_mdelay_os(1);
	psd_val = rtw_read32(pAdapter, 0x8B4);

	psd_val &= 0x0000FFFF;

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
	u32 i, psd_pts=0, psd_start=0, psd_stop=0;
	u32 psd_data=0;
	
#ifdef PLATFORM_LINUX
	if (!netif_running(pAdapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("mp_query_psd: Fail! interface not opened!\n"));
		return 0;
	}
#endif

	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		RT_TRACE(_module_mp_, _drv_warning_, ("mp_query_psd: Fail! not in MP mode!\n"));
		return 0;
	}

	if (strlen(data) == 0) { //default value
		psd_pts = 128;
		psd_start = 64;
		psd_stop = 128;   
	} else {
		sscanf(data, "pts=%d,start=%d,stop=%d", &psd_pts, &psd_start, &psd_stop);
	}

	_rtw_memset(data, '\0', sizeof(data));

	i = psd_start;
	while (i < psd_stop)
	{
		if (i >= psd_pts) {
			psd_data = GetPSDData(pAdapter, i-psd_pts);
		} else {
			psd_data = GetPSDData(pAdapter, i);
		}
		sprintf(data, "%s%x ", data, psd_data);
		i++;
	}

	#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);
	#else
	rtw_mdelay_os(100);
	#endif

	return strlen(data)+1;
}

#endif

