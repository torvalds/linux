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

u32
WBLINUX_MemoryAlloc(void* *VirtualAddress, u32 Length)
{
	*VirtualAddress = kzalloc( Length, GFP_ATOMIC ); //GFP_KERNEL is not suitable

	if (*VirtualAddress == NULL)
		return 0;
	return 1;
}

s32
EncapAtomicInc(PADAPTER Adapter, void* pAtomic)
{
	PWBLINUX pWbLinux = &Adapter->WbLinux;
	u32	ltmp;
	PULONG	pltmp = (PULONG)pAtomic;
	OS_SPIN_LOCK_ACQUIRED( &pWbLinux->AtomicSpinLock );
	(*pltmp)++;
	ltmp = (*pltmp);
	OS_SPIN_LOCK_RELEASED( &pWbLinux->AtomicSpinLock );
	return ltmp;
}

s32
EncapAtomicDec(PADAPTER Adapter, void* pAtomic)
{
	PWBLINUX pWbLinux = &Adapter->WbLinux;
	u32	ltmp;
	PULONG	pltmp = (PULONG)pAtomic;
	OS_SPIN_LOCK_ACQUIRED( &pWbLinux->AtomicSpinLock );
	(*pltmp)--;
	ltmp = (*pltmp);
	OS_SPIN_LOCK_RELEASED( &pWbLinux->AtomicSpinLock );
	return ltmp;
}

unsigned char
WBLINUX_Initial(PADAPTER Adapter)
{
	PWBLINUX pWbLinux = &Adapter->WbLinux;

	OS_SPIN_LOCK_ALLOCATE( &pWbLinux->SpinLock );
	OS_SPIN_LOCK_ALLOCATE( &pWbLinux->AtomicSpinLock );
	return TRUE;
}

void
WBLinux_ReceivePacket(PADAPTER Adapter, PRXLAYER1 pRxLayer1)
{
	BUG();
}


void
WBLINUX_GetNextPacket(PADAPTER Adapter,  PDESCRIPTOR pDes)
{
	BUG();
}

void
WBLINUX_GetNextPacketCompleted(PADAPTER Adapter, PDESCRIPTOR pDes)
{
	BUG();
}

void
WBLINUX_Destroy(PADAPTER Adapter)
{
	WBLINUX_stop( Adapter );
	OS_SPIN_LOCK_FREE( &pWbNdis->SpinLock );
#ifdef _PE_USB_INI_DUMP_
	WBDEBUG(("[w35und] unregister_netdev!\n"));
#endif
}

void
WBLINUX_stop(  PADAPTER Adapter )
{
	PWBLINUX	pWbLinux = &Adapter->WbLinux;
	struct sk_buff *pSkb;

	if (OS_ATOMIC_INC( Adapter, &pWbLinux->ThreadCount ) == 1) {
		// Shutdown module immediately
		pWbLinux->shutdown = 1;

		while (pWbLinux->skb_array[ pWbLinux->skb_GetIndex ]) {
			// Trying to free the un-sending packet
			pSkb = pWbLinux->skb_array[ pWbLinux->skb_GetIndex ];
			pWbLinux->skb_array[ pWbLinux->skb_GetIndex ] = NULL;
			if( in_irq() )
				dev_kfree_skb_irq( pSkb );
			else
				dev_kfree_skb( pSkb );

			pWbLinux->skb_GetIndex++;
			pWbLinux->skb_GetIndex %= WBLINUX_PACKET_ARRAY_SIZE;
		}

#ifdef _PE_STATE_DUMP_
		WBDEBUG(( "[w35und] SKB_RELEASE OK\n" ));
#endif
	}

	OS_ATOMIC_DEC( Adapter, &pWbLinux->ThreadCount );
}

void
WbWlanHalt(  PADAPTER Adapter )
{
	//---------------------
	Adapter->sLocalPara.ShutDowned = TRUE;

	Mds_Destroy( Adapter );

	// Turn off Rx and Tx hardware ability
	hal_stop( &Adapter->sHwData );
#ifdef _PE_USB_INI_DUMP_
	WBDEBUG(("[w35und] Hal_stop O.K.\n"));
#endif
	OS_SLEEP(100000);// Waiting Irp completed

	// Destroy the NDIS module
	WBLINUX_Destroy( Adapter );

	// Halt the HAL
	hal_halt(&Adapter->sHwData, NULL);
}

unsigned char
WbWLanInitialize(PADAPTER Adapter)
{
	phw_data_t	pHwData;
	PUCHAR		pMacAddr, pMacAddr2;
	u32		InitStep = 0;
	u8		EEPROM_region;
	u8		HwRadioOff;

	do {
		//
		// Setting default value for Linux
		//
		Adapter->sLocalPara.region_INF = REGION_AUTO;
		Adapter->sLocalPara.TxRateMode = RATE_AUTO;
		psLOCAL->bMacOperationMode = MODE_802_11_BG;	// B/G mode
		Adapter->Mds.TxRTSThreshold = DEFAULT_RTSThreshold;
		Adapter->Mds.TxFragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;
		hal_set_phy_type( &Adapter->sHwData, RF_WB_242_1 );
		Adapter->sLocalPara.MTUsize = MAX_ETHERNET_PACKET_SIZE;
		psLOCAL->bPreambleMode = AUTO_MODE;
		Adapter->sLocalPara.RadioOffStatus.boSwRadioOff = FALSE;
		pHwData = &Adapter->sHwData;
		hal_set_phy_type( pHwData, RF_DECIDE_BY_INF );

		//
		// Initial each module and variable
		//
		if (!WBLINUX_Initial(Adapter)) {
#ifdef _PE_USB_INI_DUMP_
			WBDEBUG(("[w35und]WBNDIS initialization failed\n"));
#endif
			break;
		}

		// Initial Software variable
		Adapter->sLocalPara.ShutDowned = FALSE;

		//added by ws for wep key error detection
		Adapter->sLocalPara.bWepKeyError= FALSE;
		Adapter->sLocalPara.bToSelfPacketReceived = FALSE;
		Adapter->sLocalPara.WepKeyDetectTimerCount= 2 * 100; /// 2 seconds

		// Initial USB hal
		InitStep = 1;
		pHwData = &Adapter->sHwData;
		if (!hal_init_hardware(pHwData, Adapter))
			break;

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
		Adapter->sLocalPara.boAntennaDiversity = FALSE;
		if (hal_software_set(pHwData) & 0x00000001)
			Adapter->sLocalPara.boAntennaDiversity = TRUE;

		//
		// For TS module
		//
		InitStep = 2;

		// For MDS module
		InitStep = 3;
		Mds_initial(Adapter);

		//=======================================
		// Initialize the SME, SCAN, MLME, ROAM
		//=======================================
		InitStep = 4;
		InitStep = 5;
		InitStep = 6;

		// If no user-defined address in the registry, use the addresss "burned" on the NIC instead.
		pMacAddr = Adapter->sLocalPara.ThisMacAddress;
		pMacAddr2 = Adapter->sLocalPara.PermanentAddress;
		hal_get_permanent_address( pHwData, Adapter->sLocalPara.PermanentAddress );// Reading ethernet address from EEPROM
		if (OS_MEMORY_COMPARE(pMacAddr, "\x00\x00\x00\x00\x00\x00", MAC_ADDR_LENGTH )) // Is equal
		{
			memcpy( pMacAddr, pMacAddr2, MAC_ADDR_LENGTH );
		} else {
			// Set the user define MAC address
			hal_set_ethernet_address( pHwData, Adapter->sLocalPara.ThisMacAddress );
		}

		//get current antenna
		psLOCAL->bAntennaNo = hal_get_antenna_number(pHwData);
#ifdef _PE_STATE_DUMP_
		WBDEBUG(("Driver init, antenna no = %d\n", psLOCAL->bAntennaNo));
#endif
		hal_get_hw_radio_off( pHwData );

		// Waiting for HAL setting OK
		while (!hal_idle(pHwData))
			OS_SLEEP(10000);

		MTO_Init(Adapter);

		HwRadioOff = hal_get_hw_radio_off( pHwData );
		psLOCAL->RadioOffStatus.boHwRadioOff = !!HwRadioOff;

		hal_set_radio_mode( pHwData, (unsigned char)(psLOCAL->RadioOffStatus.boSwRadioOff || psLOCAL->RadioOffStatus.boHwRadioOff) );

		hal_driver_init_OK(pHwData) = 1; // Notify hal that the driver is ready now.
		//set a tx power for reference.....
//		sme_set_tx_power_level(Adapter, 12);	FIXME?
		return TRUE;
	}
	while(FALSE);

	switch (InitStep) {
	case 5:
	case 4:
	case 3: Mds_Destroy( Adapter );
	case 2:
	case 1: WBLINUX_Destroy( Adapter );
		hal_halt( pHwData, NULL );
	case 0: break;
	}

	return FALSE;
}

void WBLINUX_ConnectStatus(PADAPTER Adapter, u32 flag)
{
	PWBLINUX	pWbLinux = &Adapter->WbLinux;

	pWbLinux->LinkStatus = flag; // OS_DISCONNECTED	or  OS_CONNECTED
}

