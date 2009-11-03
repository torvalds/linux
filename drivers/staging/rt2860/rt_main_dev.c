/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

    Module Name:
    rt_main_dev.c

    Abstract:
    Create and register network interface.

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
	Sample		Mar/21/07		Merge RT2870 and RT2860 drivers.
*/

#include "rt_config.h"

#define FORTY_MHZ_INTOLERANT_INTERVAL	(60*1000) // 1 min

/*---------------------------------------------------------------------*/
/* Private Variables Used                                              */
/*---------------------------------------------------------------------*/
//static RALINK_TIMER_STRUCT     PeriodicTimer;

char *mac = "";		   // default 00:00:00:00:00:00
char *hostname = "";		   // default CMPC
module_param (mac, charp, 0);
MODULE_PARM_DESC (mac, "rt28xx: wireless mac addr");


/*---------------------------------------------------------------------*/
/* Prototypes of Functions Used                                        */
/*---------------------------------------------------------------------*/
extern BOOLEAN ba_reordering_resource_init(PRTMP_ADAPTER pAd, int num);
extern void ba_reordering_resource_release(PRTMP_ADAPTER pAd);
extern NDIS_STATUS NICLoadRateSwitchingParams(IN PRTMP_ADAPTER pAd);

#ifdef RT2860
extern void init_thread_task(PRTMP_ADAPTER pAd);
#endif

// public function prototype
INT __devinit rt28xx_probe(IN void *_dev_p, IN void *_dev_id_p,
							IN UINT argc, OUT PRTMP_ADAPTER *ppAd);

// private function prototype
static int rt28xx_init(IN struct net_device *net_dev);
INT rt28xx_send_packets(IN struct sk_buff *skb_p, IN struct net_device *net_dev);

static void CfgInitHook(PRTMP_ADAPTER pAd);

extern	const struct iw_handler_def rt28xx_iw_handler_def;

// This function will be called when query /proc
struct iw_statistics *rt28xx_get_wireless_stats(
    IN struct net_device *net_dev);

struct net_device_stats *RT28xx_get_ether_stats(
    IN  struct net_device *net_dev);

/*
========================================================================
Routine Description:
    Close raxx interface.

Arguments:
	*net_dev			the raxx interface pointer

Return Value:
    0					Open OK
	otherwise			Open Fail

Note:
	1. if open fail, kernel will not call the close function.
	2. Free memory for
		(1) Mlme Memory Handler:		MlmeHalt()
		(2) TX & RX:					RTMPFreeTxRxRingMemory()
		(3) BA Reordering: 				ba_reordering_resource_release()
========================================================================
*/
int MainVirtualIF_close(IN struct net_device *net_dev)
{
    RTMP_ADAPTER *pAd = net_dev->ml_priv;

	// Sanity check for pAd
	if (pAd == NULL)
		return 0; // close ok

	netif_carrier_off(pAd->net_dev);
	netif_stop_queue(pAd->net_dev);


	VIRTUAL_IF_DOWN(pAd);

	RT_MOD_DEC_USE_COUNT();

	return 0; // close ok
}

/*
========================================================================
Routine Description:
    Open raxx interface.

Arguments:
	*net_dev			the raxx interface pointer

Return Value:
    0					Open OK
	otherwise			Open Fail

Note:
	1. if open fail, kernel will not call the close function.
	2. Free memory for
		(1) Mlme Memory Handler:		MlmeHalt()
		(2) TX & RX:					RTMPFreeTxRxRingMemory()
		(3) BA Reordering: 				ba_reordering_resource_release()
========================================================================
*/
int MainVirtualIF_open(IN struct net_device *net_dev)
{
    RTMP_ADAPTER *pAd = net_dev->ml_priv;

	// Sanity check for pAd
	if (pAd == NULL)
		return 0; // close ok

	if (VIRTUAL_IF_UP(pAd) != 0)
		return -1;

	// increase MODULE use count
	RT_MOD_INC_USE_COUNT();

	netif_start_queue(net_dev);
	netif_carrier_on(net_dev);
	netif_wake_queue(net_dev);

	return 0;
}

/*
========================================================================
Routine Description:
    Close raxx interface.

Arguments:
	*net_dev			the raxx interface pointer

Return Value:
    0					Open OK
	otherwise			Open Fail

Note:
	1. if open fail, kernel will not call the close function.
	2. Free memory for
		(1) Mlme Memory Handler:		MlmeHalt()
		(2) TX & RX:					RTMPFreeTxRxRingMemory()
		(3) BA Reordering: 				ba_reordering_resource_release()
========================================================================
*/
int rt28xx_close(IN PNET_DEV dev)
{
	struct net_device * net_dev = (struct net_device *)dev;
    RTMP_ADAPTER	*pAd = net_dev->ml_priv;
	BOOLEAN 		Cancelled = FALSE;
	UINT32			i = 0;
#ifdef RT2870
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(unlink_wakeup);
	DECLARE_WAITQUEUE(wait, current);

	//RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);
#endif // RT2870 //


    DBGPRINT(RT_DEBUG_TRACE, ("===> rt28xx_close\n"));

	// Sanity check for pAd
	if (pAd == NULL)
		return 0; // close ok

	{
		// If dirver doesn't wake up firmware here,
		// NICLoadFirmware will hang forever when interface is up again.
#ifdef RT2860
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) ||
			RTMP_SET_PSFLAG(pAd, fRTMP_PS_SET_PCI_CLK_OFF_COMMAND) ||
			RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
#endif
#ifdef RT2870
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
#endif
        {
#ifdef RT2860
		    AsicForceWakeup(pAd, RTMP_HALT);
#endif
#ifdef RT2870
		    AsicForceWakeup(pAd, TRUE);
#endif
        }

		if (INFRA_ON(pAd) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
		{
			MLME_DISASSOC_REQ_STRUCT	DisReq;
			MLME_QUEUE_ELEM *MsgElem = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);

			COPY_MAC_ADDR(DisReq.Addr, pAd->CommonCfg.Bssid);
			DisReq.Reason =  REASON_DEAUTH_STA_LEAVING;

			MsgElem->Machine = ASSOC_STATE_MACHINE;
			MsgElem->MsgType = MT2_MLME_DISASSOC_REQ;
			MsgElem->MsgLen = sizeof(MLME_DISASSOC_REQ_STRUCT);
			NdisMoveMemory(MsgElem->Msg, &DisReq, sizeof(MLME_DISASSOC_REQ_STRUCT));

			// Prevent to connect AP again in STAMlmePeriodicExec
			pAd->MlmeAux.AutoReconnectSsidLen= 32;
			NdisZeroMemory(pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_DISASSOC;
			MlmeDisassocReqAction(pAd, MsgElem);
			kfree(MsgElem);

			RTMPusecDelay(1000);
		}

#ifdef RT2870
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);
#endif // RT2870 //

#ifdef CCX_SUPPORT
		RTMPCancelTimer(&pAd->StaCfg.LeapAuthTimer, &Cancelled);
#endif

		RTMPCancelTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, &Cancelled);
		RTMPCancelTimer(&pAd->StaCfg.WpaDisassocAndBlockAssocTimer, &Cancelled);

		MlmeRadioOff(pAd);
#ifdef RT2860
		pAd->bPCIclkOff = FALSE;
#endif
	}

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);

	for (i = 0 ; i < NUM_OF_TX_RING; i++)
	{
		while (pAd->DeQueueRunning[i] == TRUE)
		{
			printk("Waiting for TxQueue[%d] done..........\n", i);
			RTMPusecDelay(1000);
		}
	}

#ifdef RT2870
	// ensure there are no more active urbs.
	add_wait_queue (&unlink_wakeup, &wait);
	pAd->wait = &unlink_wakeup;

	// maybe wait for deletions to finish.
	i = 0;
	//while((i < 25) && atomic_read(&pAd->PendingRx) > 0)
	while(i < 25)
	{
		unsigned long IrqFlags;

		RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
		if (pAd->PendingRx == 0)
		{
			RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
			break;
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		msleep(UNLINK_TIMEOUT_MS);	//Time in millisecond
		i++;
	}
	pAd->wait = NULL;
	remove_wait_queue (&unlink_wakeup, &wait);
#endif // RT2870 //

#ifdef RT2870
	// We need clear timerQ related structure before exits of the timer thread.
	RT2870_TimerQ_Exit(pAd);
	// Close kernel threads or tasklets
	RT28xxThreadTerminate(pAd);
#endif // RT2870 //

	// Stop Mlme state machine
	MlmeHalt(pAd);

	// Close kernel threads or tasklets
	kill_thread_task(pAd);

	MacTableReset(pAd);

	MeasureReqTabExit(pAd);
	TpcReqTabExit(pAd);

#ifdef RT2860
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE))
	{
		NICDisableInterrupt(pAd);
	}

	// Disable Rx, register value supposed will remain after reset
	NICIssueReset(pAd);

	// Free IRQ
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
	{
		// Deregister interrupt function
		RT28XX_IRQ_RELEASE(net_dev)
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
	}
#endif

	// Free Ring or USB buffers
	RTMPFreeTxRxRingMemory(pAd);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);

	// Free BA reorder resource
	ba_reordering_resource_release(pAd);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_START_UP);

	return 0; // close ok
} /* End of rt28xx_close */

static int rt28xx_init(IN struct net_device *net_dev)
{
#ifdef RT2860
	PRTMP_ADAPTER 			pAd = (PRTMP_ADAPTER)net_dev->ml_priv;
#endif
#ifdef RT2870
	PRTMP_ADAPTER 			pAd = net_dev->ml_priv;
#endif
	UINT					index;
	UCHAR					TmpPhy;
	NDIS_STATUS				Status;
	UINT32 		MacCsr0 = 0;

	// Allocate BA Reordering memory
	ba_reordering_resource_init(pAd, MAX_REORDERING_MPDU_NUM);

	// Make sure MAC gets ready.
	index = 0;
	do
	{
		RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);
		pAd->MACVersion = MacCsr0;

		if ((pAd->MACVersion != 0x00) && (pAd->MACVersion != 0xFFFFFFFF))
			break;

		RTMPusecDelay(10);
	} while (index++ < 100);

	DBGPRINT(RT_DEBUG_TRACE, ("MAC_CSR0  [ Ver:Rev=0x%08x]\n", pAd->MACVersion));
/*Iverson patch PCIE L1 issue */

	// Disable DMA
	RT28XXDMADisable(pAd);

	// Load 8051 firmware
	Status = NICLoadFirmware(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("NICLoadFirmware failed, Status[=0x%08x]\n", Status));
		goto err1;
	}

	NICLoadRateSwitchingParams(pAd);

	// Disable interrupts here which is as soon as possible
	// This statement should never be true. We might consider to remove it later
#ifdef RT2860
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE))
	{
		NICDisableInterrupt(pAd);
	}
#endif

	Status = RTMPAllocTxRxRingMemory(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("RTMPAllocDMAMemory failed, Status[=0x%08x]\n", Status));
		goto err1;
	}

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);

	// initialize MLME
	//

	Status = MlmeInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("MlmeInit failed, Status[=0x%08x]\n", Status));
		goto err2;
	}

	// Initialize pAd->StaCfg, pAd->ApCfg, pAd->CommonCfg to manufacture default
	//
	UserCfgInit(pAd);

#ifdef RT2870
	// We need init timerQ related structure before create the timer thread.
	RT2870_TimerQ_Init(pAd);
#endif // RT2870 //

	RT28XX_TASK_THREAD_INIT(pAd, Status);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err1;

	CfgInitHook(pAd);

	NdisAllocateSpinLock(&pAd->MacTabLock);

	MeasureReqTabInit(pAd);
	TpcReqTabInit(pAd);

	//
	// Init the hardware, we need to init asic before read registry, otherwise mac register will be reset
	//
	Status = NICInitializeAdapter(pAd, TRUE);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("NICInitializeAdapter failed, Status[=0x%08x]\n", Status));
		if (Status != NDIS_STATUS_SUCCESS)
		goto err3;
	}

	// Read parameters from Config File
	Status = RTMPReadParametersHook(pAd);

	printk("1. Phy Mode = %d\n", pAd->CommonCfg.PhyMode);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT_ERR(("NICReadRegParameters failed, Status[=0x%08x]\n",Status));
		goto err4;
	}

#ifdef RT2870
	pAd->CommonCfg.bMultipleIRP = FALSE;

	if (pAd->CommonCfg.bMultipleIRP)
		pAd->CommonCfg.NumOfBulkInIRP = RX_RING_SIZE;
	else
		pAd->CommonCfg.NumOfBulkInIRP = 1;
#endif // RT2870 //


   	//Init Ba Capability parameters.
	pAd->CommonCfg.DesiredHtPhy.MpduDensity = (UCHAR)pAd->CommonCfg.BACapability.field.MpduDensity;
	pAd->CommonCfg.DesiredHtPhy.AmsduEnable = (USHORT)pAd->CommonCfg.BACapability.field.AmsduEnable;
	pAd->CommonCfg.DesiredHtPhy.AmsduSize = (USHORT)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.DesiredHtPhy.MimoPs = (USHORT)pAd->CommonCfg.BACapability.field.MMPSmode;
	// UPdata to HT IE
	pAd->CommonCfg.HtCapability.HtCapInfo.MimoPs = (USHORT)pAd->CommonCfg.BACapability.field.MMPSmode;
	pAd->CommonCfg.HtCapability.HtCapInfo.AMsduSize = (USHORT)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity = (UCHAR)pAd->CommonCfg.BACapability.field.MpduDensity;

	printk("2. Phy Mode = %d\n", pAd->CommonCfg.PhyMode);

	// We should read EEPROM for all cases.  rt2860b
	NICReadEEPROMParameters(pAd, mac);

	printk("3. Phy Mode = %d\n", pAd->CommonCfg.PhyMode);

	NICInitAsicFromEEPROM(pAd); //rt2860b

	// Set PHY to appropriate mode
	TmpPhy = pAd->CommonCfg.PhyMode;
	pAd->CommonCfg.PhyMode = 0xff;
	RTMPSetPhyMode(pAd, TmpPhy);
	SetCommonHT(pAd);

	// No valid channels.
	if (pAd->ChannelListNum == 0)
	{
		printk("Wrong configuration. No valid channel found. Check \"ContryCode\" and \"ChannelGeography\" setting.\n");
		goto err4;
	}

	printk("MCS Set = %02x %02x %02x %02x %02x\n", pAd->CommonCfg.HtCapability.MCSSet[0],
           pAd->CommonCfg.HtCapability.MCSSet[1], pAd->CommonCfg.HtCapability.MCSSet[2],
           pAd->CommonCfg.HtCapability.MCSSet[3], pAd->CommonCfg.HtCapability.MCSSet[4]);

#ifdef RT2870
    //Init RT30xx RFRegisters after read RFIC type from EEPROM
	NICInitRT30xxRFRegisters(pAd);
#endif // RT2870 //


		//
	// Initialize RF register to default value
	//
	AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
	AsicLockChannel(pAd, pAd->CommonCfg.Channel);

#ifndef RT2870
	// 8051 firmware require the signal during booting time.
	AsicSendCommandToMcu(pAd, 0x72, 0xFF, 0x00, 0x00);
#endif

	if (pAd && (Status != NDIS_STATUS_SUCCESS))
	{
		//
		// Undo everything if it failed
		//
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
		{
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
		}
	}
	else if (pAd)
	{
		// Microsoft HCT require driver send a disconnect event after driver initialization.
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_MEDIA_STATE_CHANGE);

		DBGPRINT(RT_DEBUG_TRACE, ("NDIS_STATUS_MEDIA_DISCONNECT Event B!\n"));


#ifdef RT2870
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS);
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);

		//
		// Support multiple BulkIn IRP,
		// the value on pAd->CommonCfg.NumOfBulkInIRP may be large than 1.
		//
		for(index=0; index<pAd->CommonCfg.NumOfBulkInIRP; index++)
		{
			RTUSBBulkReceive(pAd);
			DBGPRINT(RT_DEBUG_TRACE, ("RTUSBBulkReceive!\n" ));
		}
#endif // RT2870 //
	}// end of else


	DBGPRINT_S(Status, ("<==== RTMPInitialize, Status=%x\n", Status));

	return TRUE;


err4:
err3:
	MlmeHalt(pAd);
err2:
	RTMPFreeTxRxRingMemory(pAd);
err1:
	os_free_mem(pAd, pAd->mpdu_blk_pool.mem); // free BA pool
	RT28XX_IRQ_RELEASE(net_dev);

	// shall not set ml_priv to NULL here because the ml_priv didn't been free yet.
	//net_dev->ml_priv = 0;

	printk("!!! %s Initialized fail !!!\n", RT28xx_CHIP_NAME);
	return FALSE;
} /* End of rt28xx_init */


/*
========================================================================
Routine Description:
    Open raxx interface.

Arguments:
	*net_dev			the raxx interface pointer

Return Value:
    0					Open OK
	otherwise			Open Fail

Note:
========================================================================
*/
int rt28xx_open(IN PNET_DEV dev)
{
	struct net_device * net_dev = (struct net_device *)dev;
	PRTMP_ADAPTER pAd = net_dev->ml_priv;
	int retval = 0;
 	POS_COOKIE pObj;


	// Sanity check for pAd
	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->ml_priv will be NULL in 2rd open */
		return -1;
	}

	// Init
 	pObj = (POS_COOKIE)pAd->OS_Cookie;

	// reset Adapter flags
	RTMP_CLEAR_FLAGS(pAd);

	// Request interrupt service routine for PCI device
	// register the interrupt routine with the os
	RT28XX_IRQ_REQUEST(net_dev);


	// Init BssTab & ChannelInfo tabbles for auto channel select.


	// Chip & other init
	if (rt28xx_init(net_dev) == FALSE)
		goto err;

	NdisZeroMemory(pAd->StaCfg.dev_name, 16);
	NdisMoveMemory(pAd->StaCfg.dev_name, net_dev->name, strlen(net_dev->name));

	// Set up the Mac address
	NdisMoveMemory(net_dev->dev_addr, (void *) pAd->CurrentAddress, 6);

	// Init IRQ parameters
	RT28XX_IRQ_INIT(pAd);

	// Various AP function init

	// Enable Interrupt
	RT28XX_IRQ_ENABLE(pAd);

	// Now Enable RxTx
	RTMPEnableRxTx(pAd);
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_START_UP);

	{
	UINT32 reg = 0;
	RTMP_IO_READ32(pAd, 0x1300, &reg);  // clear garbage interrupts
	printk("0x1300 = %08x\n", reg);
	}

#ifdef RT2860
        RTMPInitPCIeLinkCtrlValue(pAd);
#endif
	return (retval);

err:
	return (-1);
} /* End of rt28xx_open */

static const struct net_device_ops rt2860_netdev_ops = {
	.ndo_open		= MainVirtualIF_open,
	.ndo_stop		= MainVirtualIF_close,
	.ndo_do_ioctl		= rt28xx_sta_ioctl,
	.ndo_get_stats		= RT28xx_get_ether_stats,
	.ndo_validate_addr	= NULL,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_start_xmit		= rt28xx_send_packets,
};

/* Must not be called for mdev and apdev */
static NDIS_STATUS rt_ieee80211_if_setup(struct net_device *dev, PRTMP_ADAPTER pAd)
{
	NDIS_STATUS Status;
	INT     i=0;
	CHAR    slot_name[IFNAMSIZ];
	struct net_device   *device;

	if (pAd->OpMode == OPMODE_STA)
	{
		dev->wireless_handlers = &rt28xx_iw_handler_def;
	}

	dev->priv_flags = INT_MAIN;
	dev->netdev_ops = &rt2860_netdev_ops;
	// find available device name
	for (i = 0; i < 8; i++)
	{
		sprintf(slot_name, "wlan%d", i);

		device = dev_get_by_name(dev_net(dev), slot_name);
		if (device != NULL)
			dev_put(device);

		if (device == NULL)
			break;
	}

	if(i == 8)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("No available slot name\n"));
		Status = NDIS_STATUS_FAILURE;
	}
	else
	{
		sprintf(dev->name, "wlan%d", i);
		Status = NDIS_STATUS_SUCCESS;
	}

	return Status;

}

/*
========================================================================
Routine Description:
    Probe RT28XX chipset.

Arguments:
    _dev_p				Point to the PCI or USB device
	_dev_id_p			Point to the PCI or USB device ID

Return Value:
    0					Probe OK
	-ENODEV				Probe Fail

Note:
========================================================================
*/
INT __devinit   rt28xx_probe(
    IN  void *_dev_p,
    IN  void *_dev_id_p,
	IN  UINT argc,
	OUT PRTMP_ADAPTER *ppAd)
{
    struct  net_device	*net_dev;
    PRTMP_ADAPTER       pAd = (PRTMP_ADAPTER) NULL;
    INT                 status;
	PVOID				handle;
#ifdef RT2860
	struct pci_dev *dev_p = (struct pci_dev *)_dev_p;
#endif
#ifdef RT2870
	struct usb_interface *intf = (struct usb_interface *)_dev_p;
	struct usb_device *dev_p = interface_to_usbdev(intf);

	dev_p = usb_get_dev(dev_p);
#endif // RT2870 //

    DBGPRINT(RT_DEBUG_TRACE, ("STA Driver version-%s\n", STA_DRIVER_VERSION));

    net_dev = alloc_etherdev(sizeof(PRTMP_ADAPTER));
    if (net_dev == NULL)
    {
        printk("alloc_netdev failed\n");

        goto err_out;
    }

	netif_stop_queue(net_dev);

/* for supporting Network Manager */
/* Set the sysfs physical device reference for the network logical device
 * if set prior to registration will cause a symlink during initialization.
 */
    SET_NETDEV_DEV(net_dev, &(dev_p->dev));

	// Allocate RTMP_ADAPTER miniport adapter structure
	handle = kmalloc(sizeof(struct os_cookie), GFP_KERNEL);
	if (handle == NULL)
		goto err_out_free_netdev;;
	RT28XX_HANDLE_DEV_ASSIGN(handle, dev_p);

	status = RTMPAllocAdapterBlock(handle, &pAd);
	if (status != NDIS_STATUS_SUCCESS)
		goto err_out_free_netdev;

	net_dev->ml_priv = (PVOID)pAd;
    pAd->net_dev = net_dev; // must be before RT28XXNetDevInit()

	RT28XXNetDevInit(_dev_p, net_dev, pAd);

    pAd->StaCfg.OriDevType = net_dev->type;

	// Post config
	if (RT28XXProbePostConfig(_dev_p, pAd, 0) == FALSE)
		goto err_out_unmap;

	pAd->OpMode = OPMODE_STA;

	// sample move
	if (rt_ieee80211_if_setup(net_dev, pAd) != NDIS_STATUS_SUCCESS)
		goto err_out_unmap;

    // Register this device
    status = register_netdev(net_dev);
    if (status)
        goto err_out_unmap;

    // Set driver data
	RT28XX_DRVDATA_SET(_dev_p);

	*ppAd = pAd;
    return 0; // probe ok


	/* --------------------------- ERROR HANDLE --------------------------- */
err_out_unmap:
	RTMPFreeAdapter(pAd);
	RT28XX_UNMAP();

err_out_free_netdev:
	free_netdev(net_dev);

err_out:
	RT28XX_PUT_DEVICE(dev_p);

	return -ENODEV; /* probe fail */
} /* End of rt28xx_probe */


/*
========================================================================
Routine Description:
    The entry point for Linux kernel sent packet to our driver.

Arguments:
    sk_buff *skb		the pointer refer to a sk_buffer.

Return Value:
    0

Note:
	This function is the entry point of Tx Path for Os delivery packet to
	our driver. You only can put OS-depened & STA/AP common handle procedures
	in here.
========================================================================
*/
int rt28xx_packet_xmit(struct sk_buff *skb)
{
	struct net_device *net_dev = skb->dev;
	PRTMP_ADAPTER pAd = net_dev->ml_priv;
	int status = NETDEV_TX_OK;
	PNDIS_PACKET pPacket = (PNDIS_PACKET) skb;

	{
		// Drop send request since we are in monitor mode
		if (MONITOR_ON(pAd))
		{
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
			goto done;
		}
	}

        // EapolStart size is 18
	if (skb->len < 14)
	{
		//printk("bad packet size: %d\n", pkt->len);
		hex_dump("bad packet", skb->data, skb->len);
		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		goto done;
	}

	RTMP_SET_PACKET_5VT(pPacket, 0);
#ifdef CONFIG_5VT_ENHANCE
    if (*(int*)(skb->cb) == BRIDGE_TAG) {
		RTMP_SET_PACKET_5VT(pPacket, 1);
    }
#endif

	STASendPackets((NDIS_HANDLE)pAd, (PPNDIS_PACKET) &pPacket, 1);

	status = NETDEV_TX_OK;
done:

	return status;
}


/*
========================================================================
Routine Description:
    Send a packet to WLAN.

Arguments:
    skb_p           points to our adapter
    dev_p           which WLAN network interface

Return Value:
    0: transmit successfully
    otherwise: transmit fail

Note:
========================================================================
*/
INT rt28xx_send_packets(
	IN struct sk_buff 		*skb_p,
	IN struct net_device 	*net_dev)
{
    RTMP_ADAPTER *pAd = net_dev->ml_priv;
	if (!(net_dev->flags & IFF_UP))
	{
		RELEASE_NDIS_PACKET(pAd, (PNDIS_PACKET)skb_p, NDIS_STATUS_FAILURE);
		return NETDEV_TX_OK;
	}

	NdisZeroMemory((PUCHAR)&skb_p->cb[CB_OFF], 15);
	RTMP_SET_PACKET_NET_DEVICE_MBSSID(skb_p, MAIN_MBSSID);

	return rt28xx_packet_xmit(skb_p);

} /* End of MBSS_VirtualIF_PacketSend */




void CfgInitHook(PRTMP_ADAPTER pAd)
{
	pAd->bBroadComHT = TRUE;
} /* End of CfgInitHook */


// This function will be called when query /proc
struct iw_statistics *rt28xx_get_wireless_stats(
    IN struct net_device *net_dev)
{
	PRTMP_ADAPTER pAd = net_dev->ml_priv;


	DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_get_wireless_stats --->\n"));

	pAd->iw_stats.status = 0; // Status - device dependent for now

	// link quality
	pAd->iw_stats.qual.qual = ((pAd->Mlme.ChannelQuality * 12)/10 + 10);
	if(pAd->iw_stats.qual.qual > 100)
		pAd->iw_stats.qual.qual = 100;

	if (pAd->OpMode == OPMODE_STA)
		pAd->iw_stats.qual.level = RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.LastRssi0, pAd->StaCfg.RssiSample.LastRssi1, pAd->StaCfg.RssiSample.LastRssi2);

	pAd->iw_stats.qual.noise = pAd->BbpWriteLatch[66]; // noise level (dBm)

	pAd->iw_stats.qual.noise += 256 - 143;
	pAd->iw_stats.qual.updated = 1;     // Flags to know if updated
#ifdef IW_QUAL_DBM
	pAd->iw_stats.qual.updated |= IW_QUAL_DBM;	// Level + Noise are dBm
#endif // IW_QUAL_DBM //

	pAd->iw_stats.discard.nwid = 0;     // Rx : Wrong nwid/essid
	pAd->iw_stats.miss.beacon = 0;      // Missed beacons/superframe

	DBGPRINT(RT_DEBUG_TRACE, ("<--- rt28xx_get_wireless_stats\n"));
	return &pAd->iw_stats;
} /* End of rt28xx_get_wireless_stats */



void tbtt_tasklet(unsigned long data)
{
#define MAX_TX_IN_TBTT		(16)

}

/*
    ========================================================================

    Routine Description:
        return ethernet statistics counter

    Arguments:
        net_dev                     Pointer to net_device

    Return Value:
        net_device_stats*

    Note:

    ========================================================================
*/
struct net_device_stats *RT28xx_get_ether_stats(
    IN  struct net_device *net_dev)
{
    RTMP_ADAPTER *pAd = NULL;

	if (net_dev)
		pAd = net_dev->ml_priv;

	if (pAd)
	{

		pAd->stats.rx_packets = pAd->WlanCounters.ReceivedFragmentCount.QuadPart;
		pAd->stats.tx_packets = pAd->WlanCounters.TransmittedFragmentCount.QuadPart;

		pAd->stats.rx_bytes = pAd->RalinkCounters.ReceivedByteCount;
		pAd->stats.tx_bytes = pAd->RalinkCounters.TransmittedByteCount;

		pAd->stats.rx_errors = pAd->Counters8023.RxErrors;
		pAd->stats.tx_errors = pAd->Counters8023.TxErrors;

		pAd->stats.rx_dropped = 0;
		pAd->stats.tx_dropped = 0;

	    pAd->stats.multicast = pAd->WlanCounters.MulticastReceivedFrameCount.QuadPart;   // multicast packets received
	    pAd->stats.collisions = pAd->Counters8023.OneCollision + pAd->Counters8023.MoreCollisions;  // Collision packets

	    pAd->stats.rx_length_errors = 0;
	    pAd->stats.rx_over_errors = pAd->Counters8023.RxNoBuffer;                   // receiver ring buff overflow
	    pAd->stats.rx_crc_errors = 0;//pAd->WlanCounters.FCSErrorCount;     // recved pkt with crc error
	    pAd->stats.rx_frame_errors = pAd->Counters8023.RcvAlignmentErrors;          // recv'd frame alignment error
	    pAd->stats.rx_fifo_errors = pAd->Counters8023.RxNoBuffer;                   // recv'r fifo overrun
	    pAd->stats.rx_missed_errors = 0;                                            // receiver missed packet

	    // detailed tx_errors
	    pAd->stats.tx_aborted_errors = 0;
	    pAd->stats.tx_carrier_errors = 0;
	    pAd->stats.tx_fifo_errors = 0;
	    pAd->stats.tx_heartbeat_errors = 0;
	    pAd->stats.tx_window_errors = 0;

	    // for cslip etc
	    pAd->stats.rx_compressed = 0;
	    pAd->stats.tx_compressed = 0;

		return &pAd->stats;
	}
	else
    	return NULL;
}

