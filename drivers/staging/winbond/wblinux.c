//============================================================================
//  Copyright (c) 1996-2005 Winbond Electronic Corporation
//
//  Module Name:
//    wblinux.c
//
//  Abstract:
//    Linux releated routines
//
//============================================================================
#include "os_common.h"

s32
EncapAtomicInc(struct wb35_adapter * adapter, void* pAtomic)
{
	u32	ltmp;
	u32 *	pltmp = (u32 *)pAtomic;
	spin_lock_irq( &adapter->AtomicSpinLock );
	(*pltmp)++;
	ltmp = (*pltmp);
	spin_unlock_irq( &adapter->AtomicSpinLock );
	return ltmp;
}

s32
EncapAtomicDec(struct wb35_adapter * adapter, void* pAtomic)
{
	u32	ltmp;
	u32 *	pltmp = (u32 *)pAtomic;
	spin_lock_irq( &adapter->AtomicSpinLock );
	(*pltmp)--;
	ltmp = (*pltmp);
	spin_unlock_irq( &adapter->AtomicSpinLock );
	return ltmp;
}

unsigned char
WBLINUX_Initial(struct wb35_adapter * adapter)
{
	spin_lock_init( &adapter->SpinLock );
	spin_lock_init( &adapter->AtomicSpinLock );
	return true;
}

void
WBLinux_ReceivePacket(struct wb35_adapter * adapter, PRXLAYER1 pRxLayer1)
{
	BUG();
}


void
WBLINUX_GetNextPacket(struct wb35_adapter * adapter,  PDESCRIPTOR pDes)
{
	BUG();
}

void
WBLINUX_GetNextPacketCompleted(struct wb35_adapter * adapter, PDESCRIPTOR pDes)
{
	BUG();
}

void
WBLINUX_Destroy(struct wb35_adapter * adapter)
{
	WBLINUX_stop( adapter );
#ifdef _PE_USB_INI_DUMP_
	WBDEBUG(("[w35und] unregister_netdev!\n"));
#endif
}

void
WBLINUX_stop(  struct wb35_adapter * adapter )
{
	struct sk_buff *pSkb;

	if (OS_ATOMIC_INC( adapter, &adapter->ThreadCount ) == 1) {
		// Shutdown module immediately
		adapter->shutdown = 1;

		while (adapter->skb_array[ adapter->skb_GetIndex ]) {
			// Trying to free the un-sending packet
			pSkb = adapter->skb_array[ adapter->skb_GetIndex ];
			adapter->skb_array[ adapter->skb_GetIndex ] = NULL;
			if( in_irq() )
				dev_kfree_skb_irq( pSkb );
			else
				dev_kfree_skb( pSkb );

			adapter->skb_GetIndex++;
			adapter->skb_GetIndex %= WBLINUX_PACKET_ARRAY_SIZE;
		}

#ifdef _PE_STATE_DUMP_
		WBDEBUG(( "[w35und] SKB_RELEASE OK\n" ));
#endif
	}

	OS_ATOMIC_DEC(adapter, &adapter->ThreadCount);
}

void
WbWlanHalt(struct wb35_adapter *adapter)
{
	//---------------------
	adapter->sLocalPara.ShutDowned = true;

	Mds_Destroy(adapter);

	// Turn off Rx and Tx hardware ability
	hal_stop(&adapter->sHwData);
#ifdef _PE_USB_INI_DUMP_
	WBDEBUG(("[w35und] Hal_stop O.K.\n"));
#endif
	msleep(100);// Waiting Irp completed

	// Destroy the NDIS module
	WBLINUX_Destroy(adapter);

	// Halt the HAL
	hal_halt(&adapter->sHwData, NULL);
}

unsigned char
WbWLanInitialize(struct wb35_adapter *adapter)
{
	phw_data_t	pHwData;
	u8		*pMacAddr;
	u8		*pMacAddr2;
	u32		InitStep = 0;
	u8		EEPROM_region;
	u8		HwRadioOff;

	//
	// Setting default value for Linux
	//
	adapter->sLocalPara.region_INF = REGION_AUTO;
	adapter->sLocalPara.TxRateMode = RATE_AUTO;
	psLOCAL->bMacOperationMode = MODE_802_11_BG;	// B/G mode
	adapter->Mds.TxRTSThreshold = DEFAULT_RTSThreshold;
	adapter->Mds.TxFragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;
	hal_set_phy_type( &adapter->sHwData, RF_WB_242_1 );
	adapter->sLocalPara.MTUsize = MAX_ETHERNET_PACKET_SIZE;
	psLOCAL->bPreambleMode = AUTO_MODE;
	adapter->sLocalPara.RadioOffStatus.boSwRadioOff = false;
	pHwData = &adapter->sHwData;
	hal_set_phy_type( pHwData, RF_DECIDE_BY_INF );

	//
	// Initial each module and variable
	//
	if (!WBLINUX_Initial(adapter)) {
#ifdef _PE_USB_INI_DUMP_
		WBDEBUG(("[w35und]WBNDIS initialization failed\n"));
#endif
		goto error;
	}

	// Initial Software variable
	adapter->sLocalPara.ShutDowned = false;

	//added by ws for wep key error detection
	adapter->sLocalPara.bWepKeyError= false;
	adapter->sLocalPara.bToSelfPacketReceived = false;
	adapter->sLocalPara.WepKeyDetectTimerCount= 2 * 100; /// 2 seconds

	// Initial USB hal
	InitStep = 1;
	pHwData = &adapter->sHwData;
	if (!hal_init_hardware(pHwData, adapter))
		goto error;

	EEPROM_region = hal_get_region_from_EEPROM( pHwData );
	if (EEPROM_region != REGION_AUTO)
		psLOCAL->region = EEPROM_region;
	else {
		if (psLOCAL->region_INF != REGION_AUTO)
			psLOCAL->region = psLOCAL->region_INF;
		else
			psLOCAL->region = REGION_USA;	//default setting
	}

	// Get Software setting flag from hal
	adapter->sLocalPara.boAntennaDiversity = false;
	if (hal_software_set(pHwData) & 0x00000001)
		adapter->sLocalPara.boAntennaDiversity = true;

	//
	// For TS module
	//
	InitStep = 2;

	// For MDS module
	InitStep = 3;
	Mds_initial(adapter);

	//=======================================
	// Initialize the SME, SCAN, MLME, ROAM
	//=======================================
	InitStep = 4;
	InitStep = 5;
	InitStep = 6;

	// If no user-defined address in the registry, use the addresss "burned" on the NIC instead.
	pMacAddr = adapter->sLocalPara.ThisMacAddress;
	pMacAddr2 = adapter->sLocalPara.PermanentAddress;
	hal_get_permanent_address( pHwData, adapter->sLocalPara.PermanentAddress );// Reading ethernet address from EEPROM
	if (OS_MEMORY_COMPARE(pMacAddr, "\x00\x00\x00\x00\x00\x00", MAC_ADDR_LENGTH )) // Is equal
	{
		memcpy( pMacAddr, pMacAddr2, MAC_ADDR_LENGTH );
	} else {
		// Set the user define MAC address
		hal_set_ethernet_address( pHwData, adapter->sLocalPara.ThisMacAddress );
	}

	//get current antenna
	psLOCAL->bAntennaNo = hal_get_antenna_number(pHwData);
#ifdef _PE_STATE_DUMP_
	WBDEBUG(("Driver init, antenna no = %d\n", psLOCAL->bAntennaNo));
#endif
	hal_get_hw_radio_off( pHwData );

	// Waiting for HAL setting OK
	while (!hal_idle(pHwData))
		msleep(10);

	MTO_Init(adapter);

	HwRadioOff = hal_get_hw_radio_off( pHwData );
	psLOCAL->RadioOffStatus.boHwRadioOff = !!HwRadioOff;

	hal_set_radio_mode( pHwData, (unsigned char)(psLOCAL->RadioOffStatus.boSwRadioOff || psLOCAL->RadioOffStatus.boHwRadioOff) );

	hal_driver_init_OK(pHwData) = 1; // Notify hal that the driver is ready now.
	//set a tx power for reference.....
//	sme_set_tx_power_level(adapter, 12);	FIXME?
	return true;

error:
	switch (InitStep) {
	case 5:
	case 4:
	case 3: Mds_Destroy( adapter );
	case 2:
	case 1: WBLINUX_Destroy( adapter );
		hal_halt( pHwData, NULL );
	case 0: break;
	}

	return false;
}

void WBLINUX_ConnectStatus(struct wb35_adapter * adapter, u32 flag)
{
	adapter->LinkStatus = flag; // OS_DISCONNECTED	or  OS_CONNECTED
}

