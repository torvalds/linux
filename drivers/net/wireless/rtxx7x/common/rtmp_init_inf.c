/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include	"rt_config.h"



#ifdef CONFIG_STA_SUPPORT
#ifdef PROFILE_STORE
NDIS_STATUS WriteDatThread(
	IN  RTMP_ADAPTER *pAd);
#endif /* PROFILE_STORE */
#endif /* CONFIG_STA_SUPPORT */

#ifdef LINUX
#ifdef OS_ABL_FUNC_SUPPORT
/* Utilities provided from NET module */
RTMP_NET_ABL_OPS RtmpDrvNetOps, *pRtmpDrvNetOps = &RtmpDrvNetOps;
RTMP_PCI_CONFIG RtmpPciConfig, *pRtmpPciConfig = &RtmpPciConfig;
RTMP_USB_CONFIG RtmpUsbConfig, *pRtmpUsbConfig = &RtmpUsbConfig;

VOID RtmpDrvOpsInit(
	OUT		VOID				*pDrvOpsOrg,
	INOUT	VOID				*pDrvNetOpsOrg,
	IN		RTMP_PCI_CONFIG		*pPciConfig,
	IN		RTMP_USB_CONFIG		*pUsbConfig)
{
	RTMP_DRV_ABL_OPS *pDrvOps = (RTMP_DRV_ABL_OPS *)pDrvOpsOrg;
#ifdef RTMP_USB_SUPPORT
	RTMP_NET_ABL_OPS *pDrvNetOps = (RTMP_NET_ABL_OPS *)pDrvNetOpsOrg;
#endif /* RTMP_USB_SUPPORT */


	/* init PCI/USB configuration in different OS */
	if (pPciConfig != NULL)
		RtmpPciConfig = *pPciConfig;

	if (pUsbConfig != NULL)
		RtmpUsbConfig = *pUsbConfig;

	/* init operators provided from us (DRIVER module) */
	pDrvOps->RTMPAllocAdapterBlock = RTMPAllocAdapterBlock;
	pDrvOps->RTMPFreeAdapter = RTMPFreeAdapter;

	pDrvOps->RtmpRaDevCtrlExit = RtmpRaDevCtrlExit;
	pDrvOps->RtmpRaDevCtrlInit = RtmpRaDevCtrlInit;

	pDrvOps->RTMPSendPackets = RTMPSendPackets;
#ifdef MBSS_SUPPORT
	pDrvOps->MBSS_PacketSend = MBSS_PacketSend;
#endif /* MBSS_SUPPORT */
#ifdef APCLI_SUPPORT
	pDrvOps->APC_PacketSend = APC_PacketSend;
#endif /* APCLI_SUPPORT */

	pDrvOps->RTMP_COM_IoctlHandle = RTMP_COM_IoctlHandle;
#ifdef CONFIG_STA_SUPPORT
	pDrvOps->RTMP_STA_IoctlHandle = RTMP_STA_IoctlHandle;
#endif /* CONFIG_STA_SUPPORT */

	pDrvOps->RTMPDrvOpen = RTMPDrvOpen;
	pDrvOps->RTMPDrvClose = RTMPDrvClose;
	pDrvOps->RTMPInfClose = RTMPInfClose;
	pDrvOps->rt28xx_init = rt28xx_init;

	/* init operators provided from us and netif module */
#ifdef RTMP_USB_SUPPORT
	*pRtmpDrvNetOps = *pDrvNetOps;
	pRtmpDrvNetOps->RtmpDrvUsbBulkOutDataPacketComplete = RTUSBBulkOutDataPacketComplete;
	pRtmpDrvNetOps->RtmpDrvUsbBulkOutMLMEPacketComplete = RTUSBBulkOutMLMEPacketComplete;
	pRtmpDrvNetOps->RtmpDrvUsbBulkOutNullFrameComplete = RTUSBBulkOutNullFrameComplete;
/*	pRtmpDrvNetOps->RtmpDrvUsbBulkOutRTSFrameComplete = RTUSBBulkOutRTSFrameComplete;*/
	pRtmpDrvNetOps->RtmpDrvUsbBulkOutPsPollComplete = RTUSBBulkOutPsPollComplete;
	pRtmpDrvNetOps->RtmpDrvUsbBulkRxComplete = RTUSBBulkRxComplete;
	*pDrvNetOps = *pRtmpDrvNetOps;
#endif /* RTMP_USB_SUPPORT */
}

RTMP_BUILD_DRV_OPS_FUNCTION_BODY

#endif /* OS_ABL_FUNC_SUPPORT */
#endif /* LINUX */


int rt28xx_init(
	IN VOID		*pAdSrc,
	IN PSTRING	pDefaultMac, 
	IN PSTRING	pHostName)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)pAdSrc;
	UINT					index;
	UCHAR					TmpPhy;
	NDIS_STATUS				Status;
	UINT32 					MacCsr0 = 0;

	if (pAd == NULL)
		return FALSE;

#ifdef CONFIG_STA_SUPPORT
#ifdef PCIE_PS_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
    	/* If dirver doesn't wake up firmware here,*/
    	/* NICLoadFirmware will hang forever when interface is up again.*/
    	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) &&
        	OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
    	{
        	AUTO_WAKEUP_STRUC AutoWakeupCfg;
			AsicForceWakeup(pAd, TRUE);
        	AutoWakeupCfg.word = 0;
	    	RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG, AutoWakeupCfg.word);
        	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
    	}
	}
#endif /* PCIE_PS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

	/* reset Adapter flags*/
	RTMP_CLEAR_FLAGS(pAd);

	/* Init BssTab & ChannelInfo tabbles for auto channel select.*/

#ifdef DOT11_N_SUPPORT
	/* Allocate BA Reordering memory*/
	if (ba_reordering_resource_init(pAd, MAX_REORDERING_MPDU_NUM) != TRUE)		
		goto err1;
#endif /* DOT11_N_SUPPORT */

	/* Make sure MAC gets ready.*/
	index = 0;
	do
	{
		RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);
		pAd->MACVersion = MacCsr0;

		if ((pAd->MACVersion != 0x00) && (pAd->MACVersion != 0xFFFFFFFF))
			break;

		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))			
			goto err1;
		
		RTMPusecDelay(10);
	} while (index++ < 100);
	DBGPRINT(RT_DEBUG_TRACE, ("MAC_CSR0  [ Ver:Rev=0x%08x]\n", pAd->MACVersion));

	RtmpChipOpsHook(pAd);

	if (MAX_LEN_OF_MAC_TABLE > MAX_AVAILABLE_CLIENT_WCID(pAd))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("MAX_LEN_OF_MAC_TABLE can not be larger than MAX_AVAILABLE_CLIENT_WCID!!!!\n"));
		goto err1;
	}


	/* Disable DMA*/
	RT28XXDMADisable(pAd);

	/* Load 8051 firmware*/
	Status = NICLoadFirmware(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("NICLoadFirmware failed, Status[=0x%08x]\n", Status));
		goto err1;
	}

	NICLoadRateSwitchingParams(pAd);

	/* Disable interrupts here which is as soon as possible*/
	/* This statement should never be true. We might consider to remove it later*/

#ifdef RESOURCE_PRE_ALLOC
	Status = RTMPInitTxRxRingMemory(pAd);
#else
	Status = RTMPAllocTxRxRingMemory(pAd);
#endif /* RESOURCE_PRE_ALLOC */

	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("RTMPAllocTxRxMemory failed, Status[=0x%08x]\n", Status));
		goto err2;
	}

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);

	/* initialize MLME*/
	
	Status = RtmpMgmtTaskInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err3;

	Status = MlmeInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("MlmeInit failed, Status[=0x%08x]\n", Status));
		goto err4;
	}

#ifdef RMTP_RBUS_SUPPORT
#ifdef VIDEO_TURBINE_SUPPORT
	VideoConfigInit(pAd);
#endif /* VIDEO_TURBINE_SUPPORT */
#endif /* RMTP_RBUS_SUPPORT */

	/* Initialize pAd->StaCfg, pAd->ApCfg, pAd->CommonCfg to manufacture default*/
	
	UserCfgInit(pAd);

	Status = RtmpNetTaskInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err5;

/*	COPY_MAC_ADDR(pAd->ApCfg.MBSSID[apidx].Bssid, netif->hwaddr);*/
/*	pAd->bForcePrintTX = TRUE;*/

	CfgInitHook(pAd);


#ifdef BLOCK_NET_IF
	initblockQueueTab(pAd);
#endif /* BLOCK_NET_IF */

	Status = MeasureReqTabInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("MeasureReqTabInit failed, Status[=0x%08x]\n",Status));
		goto err6;	
	}
	Status = TpcReqTabInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("TpcReqTabInit failed, Status[=0x%08x]\n",Status));
		goto err6;	
	}

	
	/* Init the hardware, we need to init asic before read registry, otherwise mac register will be reset*/
	
	Status = NICInitializeAdapter(pAd, TRUE);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("NICInitializeAdapter failed, Status[=0x%08x]\n", Status));
		if (Status != NDIS_STATUS_SUCCESS)
		goto err6;
	}	


	/* Read parameters from Config File */
	/* unknown, it will be updated in NICReadEEPROMParameters */
	pAd->RfIcType = RFIC_UNKNOWN;
	Status = RTMPReadParametersHook(pAd);

	DBGPRINT(RT_DEBUG_OFF, ("1. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("RTMPReadParametersHook failed, Status[=0x%08x]\n",Status));
		goto err6;
	}

#ifdef RTMP_MAC_USB
	pAd->CommonCfg.bMultipleIRP = FALSE;

	if (pAd->CommonCfg.bMultipleIRP)
		pAd->CommonCfg.NumOfBulkInIRP = RX_RING_SIZE;
	else
		pAd->CommonCfg.NumOfBulkInIRP = 1;
#endif /* RTMP_MAC_USB */

#ifdef DOT11_N_SUPPORT
   	/*Init Ba Capability parameters.*/
/*	RT28XX_BA_INIT(pAd);*/
	pAd->CommonCfg.DesiredHtPhy.MpduDensity = (UCHAR)pAd->CommonCfg.BACapability.field.MpduDensity;
	pAd->CommonCfg.DesiredHtPhy.AmsduEnable = (USHORT)pAd->CommonCfg.BACapability.field.AmsduEnable;
	pAd->CommonCfg.DesiredHtPhy.AmsduSize = (USHORT)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.DesiredHtPhy.MimoPs = (USHORT)pAd->CommonCfg.BACapability.field.MMPSmode;
	/* UPdata to HT IE*/
	pAd->CommonCfg.HtCapability.HtCapInfo.MimoPs = (USHORT)pAd->CommonCfg.BACapability.field.MMPSmode;
	pAd->CommonCfg.HtCapability.HtCapInfo.AMsduSize = (USHORT)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity = (UCHAR)pAd->CommonCfg.BACapability.field.MpduDensity;
#endif /* DOT11_N_SUPPORT */

	/* after reading Registry, we now know if in AP mode or STA mode*/

	/* Load 8051 firmware; crash when FW image not existent*/
	/* Status = NICLoadFirmware(pAd);*/
	/* if (Status != NDIS_STATUS_SUCCESS)*/
	/*    break;*/

	DBGPRINT(RT_DEBUG_OFF, ("2. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));

	/* We should read EEPROM for all cases.  rt2860b*/
	NICReadEEPROMParameters(pAd, (PSTRING)pDefaultMac);	
#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

	DBGPRINT(RT_DEBUG_OFF, ("3. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));

	NICInitAsicFromEEPROM(pAd); /*rt2860b*/
#ifdef CONFIG_STA_SUPPORT	
#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
/*	if (IS_RT3593(pAd))*/
/*	{*/
		
		/* Initialize the frequency calibration*/
		
		RTMP_CHIP_ASIC_FREQ_CAL_INIT(pAd);
/*	}*/
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

#ifdef RTMP_INTERNAL_TX_ALC
	
	/* Initialize the desired TSSI table*/
	
	RTMP_CHIP_ASIC_TSSI_TABLE_INIT(pAd);
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_TEMPERATURE_COMPENSATION
	
	/* Temperature compensation, initialize the lookup table */
	
	DBGPRINT(RT_DEBUG_ERROR, ("IS_RT5392 = %d, bAutoTxAgcG = %d\n", IS_RT5392(pAd), pAd->bAutoTxAgcG));
	if (IS_RT5392(pAd) && pAd->bAutoTxAgcG && pAd->CommonCfg.TempComp != 0)
	{
		InitLookupTable(pAd);
	}
#endif /* RTMP_TEMPERATURE_COMPENSATION */



	/* Set PHY to appropriate mode*/
	TmpPhy = pAd->CommonCfg.PhyMode;
	pAd->CommonCfg.PhyMode = 0xff;
	RTMPSetPhyMode(pAd, TmpPhy);
#ifdef DOT11_N_SUPPORT
	SetCommonHT(pAd);
#endif /* DOT11_N_SUPPORT */

	/* No valid channels.*/
	if (pAd->ChannelListNum == 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Wrong configuration. No valid channel found. Check \"ContryCode\" and \"ChannelGeography\" setting.\n"));
		goto err6;
	}

#ifdef DOT11_N_SUPPORT
	DBGPRINT(RT_DEBUG_OFF, ("MCS Set = %02x %02x %02x %02x %02x\n", pAd->CommonCfg.HtCapability.MCSSet[0],
           pAd->CommonCfg.HtCapability.MCSSet[1], pAd->CommonCfg.HtCapability.MCSSet[2],
           pAd->CommonCfg.HtCapability.MCSSet[3], pAd->CommonCfg.HtCapability.MCSSet[4]));
#endif /* DOT11_N_SUPPORT */



/*		APInitialize(pAd);*/

#ifdef IKANOS_VX_1X0
	VR_IKANOS_FP_Init(pAd->ApCfg.BssidNum, pAd->PermanentAddress);
#endif /* IKANOS_VX_1X0 */

#ifdef RTMP_MAC_USB
	AsicSendCommandToMcu(pAd, 0x31, 0xff, 0x00, 0x02);
	RTMPusecDelay(10000);
#endif /* RTMP_MAC_USB */


	/*
		Some modules init must be called before APStartUp().
		Or APStartUp() will make up beacon content and call
		other modules API to get some information to fill.
	*/


	if (pAd && (Status != NDIS_STATUS_SUCCESS))
	{
		
		/* Undo everything if it failed*/
		
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
		{
/*			NdisMDeregisterInterrupt(&pAd->Interrupt);*/
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
		}
/*		RTMPFreeAdapter(pAd);  we will free it in disconnect()*/
	}
	else if (pAd)
	{
		/* Microsoft HCT require driver send a disconnect event after driver initialization.*/
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_AP_STATUS_MEDIA_STATE_CONNECTED);
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_MEDIA_STATE_CHANGE);

		DBGPRINT(RT_DEBUG_TRACE, ("NDIS_STATUS_MEDIA_DISCONNECT Event B!\n"));


#ifdef RTMP_MAC_USB
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS);
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);

		
		/* Support multiple BulkIn IRP,*/
		/* the value on pAd->CommonCfg.NumOfBulkInIRP may be large than 1.*/
		
		for(index=0; index<pAd->CommonCfg.NumOfBulkInIRP; index++)
		{
			RTUSBBulkReceive(pAd);
			DBGPRINT(RT_DEBUG_TRACE, ("RTUSBBulkReceive!\n" ));
		}
#endif /* RTMP_MAC_USB */
	}/* end of else*/


	/* Set up the Mac address*/
#ifdef CONFIG_STA_SUPPORT
	RtmpOSNetDevAddrSet(pAd->OpMode, pAd->net_dev, &pAd->CurrentAddress[0], (PUCHAR)(pAd->StaCfg.dev_name));
#endif /* CONFIG_STA_SUPPORT */

	/* Various AP function init*/

	/* assign function pointers*/





#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{

#ifdef WPA_SUPPLICANT_SUPPORT
#ifndef NATIVE_WPA_SUPPLICANT_SUPPORT
		/* send wireless event to wpa_supplicant for infroming interface up.*/
		RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CUSTOM, RT_INTERFACE_UP, NULL, NULL, 0);
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */
#endif /* WPA_SUPPLICANT_SUPPORT */

	}
#endif /* CONFIG_STA_SUPPORT */




#ifdef HW_ANTENNA_DIVERSITY_SUPPORT
	if (pAd->chipCap.FlgIsHwAntennaDiversitySup == TRUE)
		SetHWAntennaDivsersity(pAd, pAd->bHardwareAntennaDivesity);
#endif // HW_ANTENNA_DIVERSITY_SUPPORT //

	RTMP_CHIP_SPECIFIC(pAd, RTMP_CHIP_SPEC_STATE_INIT,
						RTMP_CHIP_SPEC_INITIALIZATION, NULL, 0);


	DBGPRINT_S(Status, ("<==== rt28xx_init, Status=%x\n", Status));

	return TRUE;

err6:
	MeasureReqTabExit(pAd);
	TpcReqTabExit(pAd);
err5:	
	RtmpNetTaskExit(pAd);
	UserCfgExit(pAd);
err4:	
	MlmeHalt(pAd);
err3:	
	RtmpMgmtTaskExit(pAd);
err2:
#ifdef RESOURCE_PRE_ALLOC
	RTMPResetTxRxRingMemory(pAd);
#else
	RTMPFreeTxRxRingMemory(pAd);
#endif /* RESOURCE_PRE_ALLOC */

err1:


#ifdef DOT11_N_SUPPORT
	if(pAd->mpdu_blk_pool.mem)
		os_free_mem(pAd, pAd->mpdu_blk_pool.mem); /* free BA pool*/
#endif /* DOT11_N_SUPPORT */

	/* shall not set priv to NULL here because the priv didn't been free yet.*/
	/*net_dev->priv = 0;*/
#ifdef INF_AMAZON_SE
err0:
#endif /* INF_AMAZON_SE */
#ifdef ST
err0:
#endif /* ST */

	DBGPRINT(RT_DEBUG_ERROR, ("!!! rt28xx Initialized fail !!!\n"));
	return FALSE;
}


VOID RTMPDrvOpen(
	IN VOID			*pAdSrc)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)pAdSrc;

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

	/* Enable Interrupt*/
	RTMP_IRQ_ENABLE(pAd);

	/* Now Enable RxTx*/
	RTMPEnableRxTx(pAd);
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_START_UP);

	{
	UINT32 reg = 0;
	RTMP_IO_READ32(pAd, 0x1300, &reg);  /* clear garbage interrupts*/
	printk("0x1300 = %08x\n", reg);
	}

	{
/*	u32 reg;*/
/*	UINT8  byte;*/
/*	u16 tmp;*/

/*	RTMP_IO_READ32(pAd, XIFS_TIME_CFG, &reg);*/

/*	tmp = 0x0805;*/
/*	reg  = (reg & 0xffff0000) | tmp;*/
/*	RTMP_IO_WRITE32(pAd, XIFS_TIME_CFG, reg);*/

	}


#ifdef CONFIG_STA_SUPPORT
#ifdef PCIE_PS_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
        RTMPInitPCIeLinkCtrlValue(pAd);
#endif /* PCIE_PS_SUPPORT */


#endif /* CONFIG_STA_SUPPORT */



#ifdef CONFIG_STA_SUPPORT
	/*
		To reduce connection time, 
		do auto reconnect here instead of waiting STAMlmePeriodicExec to do auto reconnect.
	*/
//Carter Debug
	/*if (pAd->OpMode == OPMODE_STA)
		MlmeAutoReconnectLastSSID(pAd);*/

#endif /* CONFIG_STA_SUPPORT */

}


VOID RTMPDrvClose(
	IN VOID				*pAdSrc,
	IN VOID				*net_dev)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)pAdSrc;
	BOOLEAN 		Cancelled;
	UINT32			i = 0;


	Cancelled = FALSE;



#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
#ifdef PCIE_PS_SUPPORT
		RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_CLOSE);
#endif /* PCIE_PS_SUPPORT */

		/* If dirver doesn't wake up firmware here,*/
		/* NICLoadFirmware will hang forever when interface is up again.*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
        {      
		    AsicForceWakeup(pAd, TRUE);
        }

#ifdef RTMP_MAC_USB
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);
#endif /* RTMP_MAC_USB */

	}
#endif /* CONFIG_STA_SUPPORT */

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);




	for (i = 0 ; i < NUM_OF_TX_RING; i++)
	{
		while (pAd->DeQueueRunning[i] == TRUE)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Waiting for TxQueue[%d] done..........\n", i));
			RTMPusecDelay(1000);
		}
	}
	
#ifdef RTMP_MAC_USB
	RtmpOsUsbEmptyUrbCheck(&pAd->wait, &pAd->BulkInLock, pAd->PendingRx);

#endif /* RTMP_MAC_USB */


	/* Stop Mlme state machine*/
	MlmeHalt(pAd);
	
	/* Close net tasklets*/
	RtmpNetTaskExit(pAd);


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		MacTableReset(pAd);
#ifdef LED_CONTROL_SUPPORT
		RTMPSetLED(pAd, LED_LINK_DOWN);
#endif /* LED_CONTROL_SUPPORT */

		MlmeRadioOff(pAd);
	}
#endif /* CONFIG_STA_SUPPORT */


	MeasureReqTabExit(pAd);
	TpcReqTabExit(pAd);

#ifdef LED_CONTROL_SUPPORT
	RTMPExitLEDMode(pAd);
#endif // LED_CONTROL_SUPPORT


	/* Close kernel threads*/
	RtmpMgmtTaskExit(pAd);

#if 0
	{
		PBF_SYS_CTRL_STRUC PbfSysCtrl = {{0}};
		DBGPRINT(RT_DEBUG_TRACE, ("%s::  Reset MCU !!!\n", __FUNCTION__));
		/* Reset MCU */
		RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &PbfSysCtrl.word);
		PbfSysCtrl.field.MCU_RESET = 1;
		RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, PbfSysCtrl.word);

		/* Clear MCU mapping memory */
		for (i=0; i<8192; i=i+4)
			RTMP_IO_WRITE32(pAd, FIRMWARE_IMAGE_BASE + i, 0x0);
	}
#endif
	/* Free IRQ*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
	{
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
	}

	/* Free Ring or USB buffers*/
#ifdef RESOURCE_PRE_ALLOC
	RTMPResetTxRxRingMemory(pAd);
#else
	/* Free Ring or USB buffers*/
	RTMPFreeTxRxRingMemory(pAd);
#endif /* RESOURCE_PRE_ALLOC */

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);

#ifdef DOT11_N_SUPPORT
	/* Free BA reorder resource*/
	ba_reordering_resource_release(pAd);
#endif /* DOT11_N_SUPPORT */

	UserCfgExit(pAd); /* must after ba_reordering_resource_release */

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_START_UP);

/*+++Modify by woody to solve the bulk fail+++*/
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
	}
#endif /* CONFIG_STA_SUPPORT */

	/* clear MAC table */
	/* TODO: do not clear spin lock, such as fLastChangeAccordingMfbLock */
	NdisZeroMemory(&pAd->MacTab, sizeof(MAC_TABLE));

	/* release all timers */
	RTMPusecDelay(2000);
	RTMP_TimerListRelease(pAd);
}


VOID RTMPInfClose(
	IN VOID				*pAdSrc)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)pAdSrc;





#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
#ifdef PROFILE_STORE
		WriteDatThread(pAd);
		RTMPusecDelay(1000);
#endif /* PROFILE_STORE */
#ifdef QOS_DLS_SUPPORT
		/* send DLS-TEAR_DOWN message, */
		if (pAd->CommonCfg.bDLSCapable)
		{
			UCHAR i;

			/* tear down local dls table entry*/
			for (i=0; i<MAX_NUM_OF_INIT_DLS_ENTRY; i++)
			{
				if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH))
				{
					RTMPSendDLSTearDownFrame(pAd, pAd->StaCfg.DLSEntry[i].MacAddr);
					pAd->StaCfg.DLSEntry[i].Status	= DLS_NONE;
					pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
				}
			}

			/* tear down peer dls table entry*/
			for (i=MAX_NUM_OF_INIT_DLS_ENTRY; i<MAX_NUM_OF_DLS_ENTRY; i++)
			{
				if (pAd->StaCfg.DLSEntry[i].Valid && (pAd->StaCfg.DLSEntry[i].Status == DLS_FINISH))
				{
					RTMPSendDLSTearDownFrame(pAd, pAd->StaCfg.DLSEntry[i].MacAddr);
					pAd->StaCfg.DLSEntry[i].Status = DLS_NONE;
					pAd->StaCfg.DLSEntry[i].Valid	= FALSE;
				}
			}
			RTMP_MLME_HANDLER(pAd);
		}
#endif /* QOS_DLS_SUPPORT */

		if (INFRA_ON(pAd) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
		{
			MLME_DISASSOC_REQ_STRUCT	DisReq;
			MLME_QUEUE_ELEM *MsgElem;/* = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);*/
    
			os_alloc_mem(NULL, (UCHAR **)&MsgElem, sizeof(MLME_QUEUE_ELEM));
			if (MsgElem)
			{
			COPY_MAC_ADDR(DisReq.Addr, pAd->CommonCfg.Bssid);
			DisReq.Reason =  REASON_DEAUTH_STA_LEAVING;

			MsgElem->Machine = ASSOC_STATE_MACHINE;
			MsgElem->MsgType = MT2_MLME_DISASSOC_REQ;
			MsgElem->MsgLen = sizeof(MLME_DISASSOC_REQ_STRUCT);
			NdisMoveMemory(MsgElem->Msg, &DisReq, sizeof(MLME_DISASSOC_REQ_STRUCT));

			/* Prevent to connect AP again in STAMlmePeriodicExec*/
			pAd->MlmeAux.AutoReconnectSsidLen= 32;
			NdisZeroMemory(pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_DISASSOC;
			MlmeDisassocReqAction(pAd, MsgElem);
/*			kfree(MsgElem);*/
			os_free_mem(NULL, MsgElem);
			}
			
			RTMPusecDelay(1000);
		}

#ifdef WPA_SUPPLICANT_SUPPORT
#ifndef NATIVE_WPA_SUPPLICANT_SUPPORT
		/* send wireless event to wpa_supplicant for infroming interface down.*/
		RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CUSTOM, RT_INTERFACE_DOWN, NULL, NULL, 0);
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */

		if (pAd->StaCfg.pWpsProbeReqIe)
		{
/*			kfree(pAd->StaCfg.pWpsProbeReqIe);*/
			os_free_mem(NULL, pAd->StaCfg.pWpsProbeReqIe);
			pAd->StaCfg.pWpsProbeReqIe = NULL;
			pAd->StaCfg.WpsProbeReqIeLen = 0;
		}

		if (pAd->StaCfg.pWpaAssocIe)
		{
/*			kfree(pAd->StaCfg.pWpaAssocIe);*/
			os_free_mem(NULL, pAd->StaCfg.pWpaAssocIe);
			pAd->StaCfg.pWpaAssocIe = NULL;
			pAd->StaCfg.WpaAssocIeLen = 0;
		}
#endif /* WPA_SUPPLICANT_SUPPORT */


	}
#endif /* CONFIG_STA_SUPPORT */
}




PNET_DEV RtmpPhyNetDevMainCreate(
	IN VOID				*pAdSrc)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdSrc;
	PNET_DEV pDevNew;
	UINT32 MC_RowID = 0, IoctlIF = 0;


	pAd = pAd;

#ifdef MULTIPLE_CARD_SUPPORT
	MC_RowID = pAd->MC_RowID;
#endif /* MULTIPLE_CARD_SUPPORT */
#ifdef HOSTAPD_SUPPORT
	IoctlIF = pAd->IoctlIF;
#endif /* HOSTAPD_SUPPORT */

	pDevNew = RtmpOSNetDevCreate((INT32)MC_RowID, (UINT32 *)&IoctlIF,
					INT_MAIN, 0, sizeof(PRTMP_ADAPTER), INF_MAIN_DEV_NAME);

#ifdef HOSTAPD_SUPPORT
	pAd->IoctlIF = IoctlIF;
#endif /* HOSTAPD_SUPPORT */

	return pDevNew;
}

#ifdef CONFIG_STA_SUPPORT
#ifdef PROFILE_STORE
static void	WriteConfToDatFile(
    IN  PRTMP_ADAPTER pAd)
{
	char	*cfgData = 0;
	PSTRING			fileName = NULL;
	RTMP_OS_FD		file_r, file_w;
	RTMP_OS_FS_INFO		osFSInfo;
	LONG			rv, fileLen = 0;
	char			*offset = 0;
	PSTRING			pTempStr = 0;
//	INT				tempStrLen = 0;
#ifdef ANDROID_SUPPORT
	PSTRING		wpa_supp_conf_file_name = NULL;
	RTMP_OS_FD	wpa_supp_conf_file_r;
	char		*wpaConfData;
	char		*conf_offset_start;
	PSTRING		pConfTempStr;
	LONG		rv1, wpa_supp_conf_file_len = 0;
#endif


	DBGPRINT(RT_DEBUG_TRACE, ("-----> WriteConfToDatFile\n"));

#ifdef RTMP_RBUS_SUPPORT
	if (pAd->infType == RTMP_DEV_INF_RBUS)
		fileName = STA_PROFILE_PATH_RBUS;
	else
#endif /* RTMP_RBUS_SUPPORT */
		fileName = STA_PROFILE_PATH;

#ifdef	ANDROID_SUPPORT
	wpa_supp_conf_file_name = ANDROID_WPA_SUPPLICANT_CONF_PATH;
#endif

	RtmpOSFSInfoChange(&osFSInfo, TRUE);

#ifdef	ANDROID_SUPPORT
	wpa_supp_conf_file_r = RtmpOSFileOpen(wpa_supp_conf_file_name, O_RDONLY, 0);
	if (IS_FILE_OPEN_ERR(wpa_supp_conf_file_r)) 
	{
		DBGPRINT(RT_DEBUG_TRACE, ("-->1) %s: Error opening file %s\n", __func__, wpa_supp_conf_file_name));
		return;
	}
	else 
	{
		char tempConfStr[64] = {0};
		while((rv1 = RtmpOSFileRead(wpa_supp_conf_file_r, tempConfStr, 64)) > 0)
		{
			wpa_supp_conf_file_len += rv1;
		}
		os_alloc_mem(NULL, (UCHAR **)&wpaConfData, wpa_supp_conf_file_len);
		if (wpaConfData == NULL)
		{
			RtmpOSFileClose(wpa_supp_conf_file_r);
			DBGPRINT(RT_DEBUG_TRACE, ("wpaConfData kmalloc fail. (wpa_supp_conf_file_len = %ld)\n", wpa_supp_conf_file_len));
			goto out;
		}
		NdisZeroMemory(wpaConfData, wpa_supp_conf_file_len);
		RtmpOSFileSeek(wpa_supp_conf_file_r, 0);
		rv1 = RtmpOSFileRead(wpa_supp_conf_file_r, (PSTRING)wpaConfData, wpa_supp_conf_file_len);
		RtmpOSFileClose(wpa_supp_conf_file_r);
		if (rv1 != wpa_supp_conf_file_len)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("wpaConfData kmalloc fail, wpa_supp_conf_file_len = %ld\n", wpa_supp_conf_file_len));
			goto ReadErr;
		}
	}

	conf_offset_start = (PCHAR)rtstrstr((PSTRING)wpaConfData, "ctrl_interface");
	conf_offset_start += strlen("ctrl_interface");
	os_alloc_mem(NULL, (UCHAR **)&pConfTempStr, 512);
	if (!pConfTempStr)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pConfTempStr kmalloc fail. (512)\n"));
		goto WriteErr;
	}

	volatile static int found_count = 0;
	PSTRING 	wpapsk_key;
	os_alloc_mem(NULL, (UCHAR **)&wpapsk_key, 64);
	for (;;)
	{

		int i = 0;
		PSTRING conf_ptr;

		NdisZeroMemory(pConfTempStr, 512);
		conf_ptr = (PSTRING)conf_offset_start;
		while(*conf_ptr && *conf_ptr != '\n')
		{
			pConfTempStr[i++] = *conf_ptr++;
		}
		pConfTempStr[i] = 0x00;

		char *find;
		if ((size_t)(conf_offset_start - wpaConfData) < wpa_supp_conf_file_len)
		{
			conf_offset_start += strlen(pConfTempStr) + 1;
			if ((find = strstr(pConfTempStr, "ssid=")) != NULL)
			{
				if (!strncmp(pAd->CommonCfg.Ssid, (find+6), pAd->CommonCfg.SsidLen)) {
					found_count = 1;
					printk("found_count = %d\n", found_count);
				}
			}
			else if (((find = strstr(pConfTempStr, "key_mgmt=WPA-PSK")) != NULL) && 
				 (found_count == 1) && 
				 (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPAPSK)){
				found_count = 2;
				printk("found_count = %d\n", found_count);
			}
			else if (((find = strstr(pConfTempStr, "psk=")) != NULL) &&
				 (found_count == 2) &&
				 (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPAPSK)) {
				NdisZeroMemory(wpapsk_key, 64);
				strncpy((char *)wpapsk_key, (const char *)(find+5), (size_t)(strlen(find)-6));
				found_count = 0;
				printk("wpapsk_key = %s\n", wpapsk_key);
			}
			else if (((find = strstr(pConfTempStr, "wep_key0=")) != NULL) &&
				 (found_count == 1) &&
				 (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled) &&
				 (pAd->StaCfg.DefaultKeyId == 0)){
				NdisZeroMemory(wpapsk_key, 64);
				strncpy((char *)wpapsk_key, (const char *)(find+9), (size_t)(strlen(find)-9));
				printk("wep_key case, wpapsk_key = %s\n", wpapsk_key);
				found_count = 0;
			}
			else if (((find = strstr(pConfTempStr, "wep_key1=")) != NULL) &&
                                 (found_count == 1) &&
                                 (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled) && 
                                 (pAd->StaCfg.DefaultKeyId == 1)){
				NdisZeroMemory(wpapsk_key, 64);
                                strncpy((char *)wpapsk_key, (const char *)(find+9), (size_t)(strlen(find)-9));
                                printk("wep_key case, wpapsk_key = %s\n", wpapsk_key);
				found_count = 0;
                        }
			else if (((find = strstr(pConfTempStr, "wep_key2=")) != NULL) &&
                                 (found_count == 1) &&
                                 (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled) && 
                                 (pAd->StaCfg.DefaultKeyId == 2)){
				NdisZeroMemory(wpapsk_key, 64);
                                strncpy((char *)wpapsk_key, (const char *)(find+9), (size_t)(strlen(find)-9));
                                printk("wep_key case, wpapsk_key = %s\n", wpapsk_key);
				found_count = 0;
                        }
			else if (((find = strstr(pConfTempStr, "wep_key3=")) != NULL) &&
                                 (found_count == 1) &&
                                 (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled) && 
                                 (pAd->StaCfg.DefaultKeyId == 3)){
				NdisZeroMemory(wpapsk_key, 64);
                                strncpy((char *)wpapsk_key, (const char *)(find+9), (size_t)(strlen(find)-9));
                                printk("wep_key case, wpapsk_key = %s\n", wpapsk_key);
				found_count = 0;
                        }
		}
                else
                {
			break;
                }
	}
#endif
	file_r = RtmpOSFileOpen(fileName, O_RDONLY, 0);
	if (IS_FILE_OPEN_ERR(file_r)) 
	{
		DBGPRINT(RT_DEBUG_TRACE, ("-->1) %s: Error opening file %s\n", __FUNCTION__, fileName));
		return;
	}
	else 
	{
		char tempStr[64] = {0};
		while((rv = RtmpOSFileRead(file_r, tempStr, 64)) > 0)
		{
			fileLen += rv;
		}
		os_alloc_mem(NULL, (UCHAR **)&cfgData, fileLen);
		if (cfgData == NULL)
		{
			RtmpOSFileClose(file_r);
			DBGPRINT(RT_DEBUG_TRACE, ("CfgData kmalloc fail. (fileLen = %ld)\n", fileLen));
			goto out;
		}
		NdisZeroMemory(cfgData, fileLen);
		RtmpOSFileSeek(file_r, 0);
		rv = RtmpOSFileRead(file_r, (PSTRING)cfgData, fileLen);
		RtmpOSFileClose(file_r);
		if (rv != fileLen)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("CfgData kmalloc fail, fileLen = %ld\n", fileLen));
			goto ReadErr;
		}
	}

	file_w = RtmpOSFileOpen(fileName, O_WRONLY|O_TRUNC, 0);
	if (IS_FILE_OPEN_ERR(file_w)) 
	{
		goto WriteFileOpenErr;
	}
	else 
	{
		offset = (PCHAR) rtstrstr((PSTRING) cfgData, "Default\n");
		offset += strlen("Default\n");
		RtmpOSFileWrite(file_w, (PSTRING)cfgData, (int)(offset-cfgData));
		os_alloc_mem(NULL, (UCHAR **)&pTempStr, 512);
		if (!pTempStr)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("pTempStr kmalloc fail. (512)\n"));
			RtmpOSFileClose(file_w);
			goto WriteErr;
		}
			
		for (;;)
		{
			int i = 0;
			PSTRING ptr;

			NdisZeroMemory(pTempStr, 512);
			ptr = (PSTRING) offset;
			while(*ptr && *ptr != '\n')
			{
				pTempStr[i++] = *ptr++;
			}
			pTempStr[i] = 0x00;
			if ((size_t)(offset - cfgData) < fileLen)
			{
				offset += strlen(pTempStr) + 1;
				if (strncmp(pTempStr, "SSID=", strlen("SSID=")) == 0)
				{
					NdisZeroMemory(pTempStr, 512);
					NdisMoveMemory(pTempStr, "SSID=", strlen("SSID="));
					NdisMoveMemory(pTempStr + 5, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen);
				}
				else if (strncmp(pTempStr, "WPAPSK=", strlen("WPAPSK=")) == 0)
				{
					if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPAPSK){
						NdisZeroMemory(pTempStr, 512);
						NdisMoveMemory(pTempStr, "WPAPSK=", strlen("WPAPSK="));
						NdisCopyMemory(pTempStr + 7, wpapsk_key, strlen(wpapsk_key));
					}
				}
				else if (strncmp(pTempStr, "AuthMode=", strlen("AuthMode=")) == 0)
				{
					NdisZeroMemory(pTempStr, 512);
					if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeOpen)
						sprintf(pTempStr, "AuthMode=OPEN");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeShared)
						sprintf(pTempStr, "AuthMode=SHARED");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeAutoSwitch)
						sprintf(pTempStr, "AuthMode=WEPAUTO");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK)
						sprintf(pTempStr, "AuthMode=WPAPSK");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
						sprintf(pTempStr, "AuthMode=WPA2PSK");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA)
						sprintf(pTempStr, "AuthMode=WPA");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2)
						sprintf(pTempStr, "AuthMode=WPA2");
					else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
						sprintf(pTempStr, "AuthMode=WPANONE");
				}
				else if (strncmp(pTempStr, "EncrypType=", strlen("EncrypType=")) == 0)
				{
					NdisZeroMemory(pTempStr, 512);
					if (pAd->StaCfg.WepStatus == Ndis802_11WEPDisabled)
						sprintf(pTempStr, "EncrypType=NONE");
					else if (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled)
						sprintf(pTempStr, "EncrypType=WEP");
					else if (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled)
						sprintf(pTempStr, "EncrypType=TKIP");
					else if (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)
						sprintf(pTempStr, "EncrypType=AES");
				}
				else if (strncmp(pTempStr, "DefaultKeyID=", strlen("DefaultKeyID=")) == 0)
                                {
                                        if (pAd->StaCfg.WepStatus == Ndis802_11WEPEnabled){
                                                NdisZeroMemory(pTempStr, 512);
						if (pAd->StaCfg.DefaultKeyId == 0)
	                                                NdisMoveMemory(pTempStr, "DefaultKeyID=1", strlen("DefaultKeyID=1"));
						if (pAd->StaCfg.DefaultKeyId == 1)
                                                        NdisMoveMemory(pTempStr, "DefaultKeyID=2", strlen("DefaultKeyID=2"));
						if (pAd->StaCfg.DefaultKeyId == 2)
                                                        NdisMoveMemory(pTempStr, "DefaultKeyID=3", strlen("DefaultKeyID=3"));
						if (pAd->StaCfg.DefaultKeyId == 3)
                                                        NdisMoveMemory(pTempStr, "DefaultKeyID=4", strlen("DefaultKeyID=4"));
					}
                                }
				else if (strncmp(pTempStr, "Key1Str=", strlen("Key1Str=")) == 0)
                                {	printk("KEY1");
					if (pAd->StaCfg.DefaultKeyId==0){
                                        	NdisZeroMemory(pTempStr, 512);
						NdisMoveMemory(pTempStr, "Key1Str=", strlen("Key1Str="));
	                                        NdisCopyMemory(pTempStr + 8, wpapsk_key, strlen(wpapsk_key));
					}
                                }
				else if (strncmp(pTempStr, "Key2Str=", strlen("Key2Str=")) == 0)
                                {	printk("KEY2");
                                        if (pAd->StaCfg.DefaultKeyId==1){
                                                NdisZeroMemory(pTempStr, 512);
                                                NdisMoveMemory(pTempStr, "Key2Str=", strlen("Key2Str="));
                                                NdisCopyMemory(pTempStr + 8, wpapsk_key, strlen(wpapsk_key));
					}
                                }
				else if (strncmp(pTempStr, "Key3Str=", strlen("Key3Str=")) == 0)
                                {	printk("KEY3");
                                        if (pAd->StaCfg.DefaultKeyId==2){
                                                NdisZeroMemory(pTempStr, 512);
                                                NdisMoveMemory(pTempStr, "Key3Str=", strlen("Key3Str="));
                                                NdisCopyMemory(pTempStr + 8, wpapsk_key, strlen(wpapsk_key));
					}
                                }
				else if (strncmp(pTempStr, "Key4Str=", strlen("Key4Str=")) == 0)
                                {	printk("KEY4");
                                        if (pAd->StaCfg.DefaultKeyId==3){
                                                NdisZeroMemory(pTempStr, 512);
                                                NdisMoveMemory(pTempStr, "Key4Str=", strlen("Key4Str="));
                                                NdisCopyMemory(pTempStr + 8, wpapsk_key, strlen(wpapsk_key));
					}
                                }
				RtmpOSFileWrite(file_w, pTempStr, strlen(pTempStr));
				RtmpOSFileWrite(file_w, "\n", 1);
			}
			else
			{
				break;
			}
		}
	}

WriteErr:
	if (pConfTempStr)
		os_free_mem(NULL, pConfTempStr);
	if (pTempStr)
/*		kfree(pTempStr); */
		os_free_mem(NULL, pTempStr);
ReadErr:
WriteFileOpenErr:
	if (wpapsk_key)
		os_free_mem(NULL, wpapsk_key);
	if (wpaConfData)
		os_free_mem(NULL, wpaConfData);
	if (cfgData)
/*		kfree(cfgData); */
		os_free_mem(NULL, cfgData);
out:
	RtmpOSFSInfoChange(&osFSInfo, FALSE);


	DBGPRINT(RT_DEBUG_TRACE, ("<----- WriteConfToDatFile\n"));
	return;
}


INT write_dat_file_thread (
    IN ULONG Context)
{
	RTMP_OS_TASK *pTask;
	RTMP_ADAPTER *pAd;
	//int 	Status = 0;

	pTask = (RTMP_OS_TASK *)Context;

	if (pTask == NULL)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%s: pTask is NULL\n", __FUNCTION__));
		return 0;
	}
	
	pAd = (PRTMP_ADAPTER)RTMP_OS_TASK_DATA_GET(pTask);

	if (pAd == NULL)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%s: pAd is NULL\n", __FUNCTION__));
		return 0;
	}

	RtmpOSTaskCustomize(pTask);

	/* Update ssid, auth mode and encr type to DAT file */
	WriteConfToDatFile(pAd);
	
		RtmpOSTaskNotifyToExit(pTask);
	
	return 0;
}

NDIS_STATUS WriteDatThread(
	IN  RTMP_ADAPTER *pAd)
{
	NDIS_STATUS status = NDIS_STATUS_FAILURE;
	RTMP_OS_TASK *pTask;

	if (pAd->bWriteDat == FALSE)
		return 0;

	DBGPRINT(RT_DEBUG_TRACE, ("-->WriteDatThreadInit()\n"));

	pTask = &pAd->WriteDatTask;

	RTMP_OS_TASK_INIT(pTask, "RtmpWriteDatTask", pAd);
	status = RtmpOSTaskAttach(pTask, write_dat_file_thread, (ULONG)&pAd->WriteDatTask);
	DBGPRINT(RT_DEBUG_TRACE, ("<--WriteDatThreadInit(), status=%d!\n", status));

	return status;
}
#endif /* PROFILE_STORE */
#endif /* CONFIG_STA_SUPPORT */

/* End of rtmp_init_inf.c */
