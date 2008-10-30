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
#include <linux/netdevice.h>

#include "mds_f.h"
#include "mto_f.h"
#include "os_common.h"
#include "wbhal_f.h"
#include "wblinux_f.h"

unsigned char
WBLINUX_Initial(struct wbsoft_priv * adapter)
{
	spin_lock_init( &adapter->SpinLock );
	return true;
}

void
WBLINUX_Destroy(struct wbsoft_priv * adapter)
{
	WBLINUX_stop( adapter );
#ifdef _PE_USB_INI_DUMP_
	WBDEBUG(("[w35und] unregister_netdev!\n"));
#endif
}

void
WBLINUX_stop(  struct wbsoft_priv * adapter )
{
	struct sk_buff *pSkb;

	if (atomic_inc_return(&adapter->ThreadCount) == 1) {
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

	atomic_dec(&adapter->ThreadCount);
}

void
WbWlanHalt(  struct wbsoft_priv * adapter )
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
WbWLanInitialize(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t	pHwData;
	u8		*pMacAddr;
	u8		*pMacAddr2;
	u32		InitStep = 0;
	u8		EEPROM_region;
	u8		HwRadioOff;

	//
	// Setting default value for Linux
	//
	priv->sLocalPara.region_INF = REGION_AUTO;
	priv->sLocalPara.TxRateMode = RATE_AUTO;
	priv->sLocalPara.bMacOperationMode = MODE_802_11_BG;	// B/G mode
	priv->Mds.TxRTSThreshold = DEFAULT_RTSThreshold;
	priv->Mds.TxFragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;
	hal_set_phy_type( &priv->sHwData, RF_WB_242_1 );
	priv->sLocalPara.MTUsize = MAX_ETHERNET_PACKET_SIZE;
	priv->sLocalPara.bPreambleMode = AUTO_MODE;
	priv->sLocalPara.RadioOffStatus.boSwRadioOff = false;
	pHwData = &priv->sHwData;
	hal_set_phy_type( pHwData, RF_DECIDE_BY_INF );

	//
	// Initial each module and variable
	//
	if (!WBLINUX_Initial(priv)) {
#ifdef _PE_USB_INI_DUMP_
		WBDEBUG(("[w35und]WBNDIS initialization failed\n"));
#endif
		goto error;
	}

	// Initial Software variable
	priv->sLocalPara.ShutDowned = false;

	//added by ws for wep key error detection
	priv->sLocalPara.bWepKeyError= false;
	priv->sLocalPara.bToSelfPacketReceived = false;
	priv->sLocalPara.WepKeyDetectTimerCount= 2 * 100; /// 2 seconds

	// Initial USB hal
	InitStep = 1;
	pHwData = &priv->sHwData;
	if (!hal_init_hardware(hw))
		goto error;

	EEPROM_region = hal_get_region_from_EEPROM( pHwData );
	if (EEPROM_region != REGION_AUTO)
		priv->sLocalPara.region = EEPROM_region;
	else {
		if (priv->sLocalPara.region_INF != REGION_AUTO)
			priv->sLocalPara.region = priv->sLocalPara.region_INF;
		else
			priv->sLocalPara.region = REGION_USA;	//default setting
	}

	// Get Software setting flag from hal
	priv->sLocalPara.boAntennaDiversity = false;
	if (hal_software_set(pHwData) & 0x00000001)
		priv->sLocalPara.boAntennaDiversity = true;

	//
	// For TS module
	//
	InitStep = 2;

	// For MDS module
	InitStep = 3;
	Mds_initial(priv);

	//=======================================
	// Initialize the SME, SCAN, MLME, ROAM
	//=======================================
	InitStep = 4;
	InitStep = 5;
	InitStep = 6;

	// If no user-defined address in the registry, use the addresss "burned" on the NIC instead.
	pMacAddr = priv->sLocalPara.ThisMacAddress;
	pMacAddr2 = priv->sLocalPara.PermanentAddress;
	hal_get_permanent_address( pHwData, priv->sLocalPara.PermanentAddress );// Reading ethernet address from EEPROM
	if (memcmp(pMacAddr, "\x00\x00\x00\x00\x00\x00", MAC_ADDR_LENGTH) == 0)
		memcpy(pMacAddr, pMacAddr2, MAC_ADDR_LENGTH);
	else {
		// Set the user define MAC address
		hal_set_ethernet_address(pHwData, priv->sLocalPara.ThisMacAddress);
	}

	//get current antenna
	priv->sLocalPara.bAntennaNo = hal_get_antenna_number(pHwData);
#ifdef _PE_STATE_DUMP_
	WBDEBUG(("Driver init, antenna no = %d\n", psLOCAL->bAntennaNo));
#endif
	hal_get_hw_radio_off( pHwData );

	// Waiting for HAL setting OK
	while (!hal_idle(pHwData))
		msleep(10);

	MTO_Init(priv);

	HwRadioOff = hal_get_hw_radio_off( pHwData );
	priv->sLocalPara.RadioOffStatus.boHwRadioOff = !!HwRadioOff;

	hal_set_radio_mode( pHwData, (unsigned char)(priv->sLocalPara.RadioOffStatus.boSwRadioOff || priv->sLocalPara.RadioOffStatus.boHwRadioOff) );

	hal_driver_init_OK(pHwData) = 1; // Notify hal that the driver is ready now.
	//set a tx power for reference.....
//	sme_set_tx_power_level(priv, 12);	FIXME?
	return true;

error:
	switch (InitStep) {
	case 5:
	case 4:
	case 3: Mds_Destroy( priv );
	case 2:
	case 1: WBLINUX_Destroy( priv );
		hal_halt( pHwData, NULL );
	case 0: break;
	}

	return false;
}
