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
*/

#include "rt_config.h"

/*---------------------------------------------------------------------*/
/* Private Variables Used                                              */
/*---------------------------------------------------------------------*/

char *mac = "";		/* default 00:00:00:00:00:00 */
char *hostname = "";		/* default CMPC */
module_param(mac, charp, 0);
MODULE_PARM_DESC(mac, "rt28xx: wireless mac addr");

/*---------------------------------------------------------------------*/
/* Prototypes of Functions Used                                        */
/*---------------------------------------------------------------------*/

/* public function prototype */
int rt28xx_close(IN struct net_device *net_dev);
int rt28xx_open(struct net_device *net_dev);

/* private function prototype */
static int rt28xx_send_packets(IN struct sk_buff *skb_p,
			       IN struct net_device *net_dev);

static struct net_device_stats *RT28xx_get_ether_stats(IN struct net_device
						       *net_dev);

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
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0;	/* close ok */

	netif_carrier_off(pAd->net_dev);
	netif_stop_queue(pAd->net_dev);

	{
		BOOLEAN Cancelled;

		if (INFRA_ON(pAd) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
			struct rt_mlme_disassoc_req DisReq;
			struct rt_mlme_queue_elem *MsgElem =
			    (struct rt_mlme_queue_elem *)kmalloc(sizeof(struct rt_mlme_queue_elem),
							MEM_ALLOC_FLAG);

			if (MsgElem) {
				COPY_MAC_ADDR(DisReq.Addr,
					      pAd->CommonCfg.Bssid);
				DisReq.Reason = REASON_DEAUTH_STA_LEAVING;

				MsgElem->Machine = ASSOC_STATE_MACHINE;
				MsgElem->MsgType = MT2_MLME_DISASSOC_REQ;
				MsgElem->MsgLen =
				    sizeof(struct rt_mlme_disassoc_req);
				NdisMoveMemory(MsgElem->Msg, &DisReq,
					       sizeof
					       (struct rt_mlme_disassoc_req));

				/* Prevent to connect AP again in STAMlmePeriodicExec */
				pAd->MlmeAux.AutoReconnectSsidLen = 32;
				NdisZeroMemory(pAd->MlmeAux.AutoReconnectSsid,
					       pAd->MlmeAux.
					       AutoReconnectSsidLen);

				pAd->Mlme.CntlMachine.CurrState =
				    CNTL_WAIT_OID_DISASSOC;
				MlmeDisassocReqAction(pAd, MsgElem);
				kfree(MsgElem);
			}

			RTMPusecDelay(1000);
		}

		RTMPCancelTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer,
				&Cancelled);
		RTMPCancelTimer(&pAd->StaCfg.WpaDisassocAndBlockAssocTimer,
				&Cancelled);
	}

	VIRTUAL_IF_DOWN(pAd);

	RT_MOD_DEC_USE_COUNT();

	return 0;		/* close ok */
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
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0;	/* close ok */

	if (VIRTUAL_IF_UP(pAd) != 0)
		return -1;

	/* increase MODULE use count */
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
int rt28xx_close(struct net_device *dev)
{
	struct net_device *net_dev = (struct net_device *)dev;
	struct rt_rtmp_adapter *pAd = NULL;
	BOOLEAN Cancelled;
	u32 i = 0;

#ifdef RTMP_MAC_USB
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(unlink_wakeup);
	DECLARE_WAITQUEUE(wait, current);
#endif /* RTMP_MAC_USB // */

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt28xx_close\n"));

	Cancelled = FALSE;
	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0;	/* close ok */

	{
#ifdef RTMP_MAC_PCI
		RTMPPCIeLinkCtrlValueRestore(pAd, RESTORE_CLOSE);
#endif /* RTMP_MAC_PCI // */

		/* If dirver doesn't wake up firmware here, */
		/* NICLoadFirmware will hang forever when interface is up again. */
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)) {
			AsicForceWakeup(pAd, TRUE);
		}
#ifdef RTMP_MAC_USB
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);
#endif /* RTMP_MAC_USB // */

		MlmeRadioOff(pAd);
#ifdef RTMP_MAC_PCI
		pAd->bPCIclkOff = FALSE;
#endif /* RTMP_MAC_PCI // */
	}

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);

	for (i = 0; i < NUM_OF_TX_RING; i++) {
		while (pAd->DeQueueRunning[i] == TRUE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Waiting for TxQueue[%d] done..........\n",
				  i));
			RTMPusecDelay(1000);
		}
	}

#ifdef RTMP_MAC_USB
	/* ensure there are no more active urbs. */
	add_wait_queue(&unlink_wakeup, &wait);
	pAd->wait = &unlink_wakeup;

	/* maybe wait for deletions to finish. */
	i = 0;
	/*while((i < 25) && atomic_read(&pAd->PendingRx) > 0) */
	while (i < 25) {
		unsigned long IrqFlags;

		RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
		if (pAd->PendingRx == 0) {
			RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
			break;
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		msleep(UNLINK_TIMEOUT_MS);	/*Time in millisecond */
		i++;
	}
	pAd->wait = NULL;
	remove_wait_queue(&unlink_wakeup, &wait);
#endif /* RTMP_MAC_USB // */

	/* Stop Mlme state machine */
	MlmeHalt(pAd);

	/* Close net tasklets */
	RtmpNetTaskExit(pAd);

	{
		MacTableReset(pAd);
	}

	MeasureReqTabExit(pAd);
	TpcReqTabExit(pAd);

	/* Close kernel threads */
	RtmpMgmtTaskExit(pAd);

#ifdef RTMP_MAC_PCI
	{
		BOOLEAN brc;
		/*      unsigned long                   Value; */

		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE)) {
			RTMP_ASIC_INTERRUPT_DISABLE(pAd);
		}
		/* Receive packets to clear DMA index after disable interrupt. */
		/*RTMPHandleRxDoneInterrupt(pAd); */
		/* put to radio off to save power when driver unload.  After radiooff, can't write /read register.  So need to finish all */
		/* register access before Radio off. */

		brc = RT28xxPciAsicRadioOff(pAd, RTMP_HALT, 0);

/*In  solution 3 of 3090F, the bPCIclkOff will be set to TRUE after calling RT28xxPciAsicRadioOff */
		pAd->bPCIclkOff = FALSE;

		if (brc == FALSE) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s call RT28xxPciAsicRadioOff fail!\n",
				  __func__));
		}
	}

/*
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE))
	{
		RTMP_ASIC_INTERRUPT_DISABLE(pAd);
	}

	// Disable Rx, register value supposed will remain after reset
	NICIssueReset(pAd);
*/
#endif /* RTMP_MAC_PCI // */

	/* Free IRQ */
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
#ifdef RTMP_MAC_PCI
		/* Deregister interrupt function */
		RtmpOSIRQRelease(net_dev);
#endif /* RTMP_MAC_PCI // */
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
	}
	/* Free Ring or USB buffers */
	RTMPFreeTxRxRingMemory(pAd);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);

	/* Free BA reorder resource */
	ba_reordering_resource_release(pAd);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_START_UP);

/*+++Modify by woody to solve the bulk fail+++*/
	{
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt28xx_close\n"));
	return 0;		/* close ok */
}				/* End of rt28xx_close */

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
int rt28xx_open(struct net_device *dev)
{
	struct net_device *net_dev = (struct net_device *)dev;
	struct rt_rtmp_adapter *pAd = NULL;
	int retval = 0;
	/*struct os_cookie *pObj; */

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	/* Sanity check for pAd */
	if (pAd == NULL) {
		/* if 1st open fail, pAd will be free;
		   So the net_dev->ml_priv will be NULL in 2rd open */
		return -1;
	}

	if (net_dev->priv_flags == INT_MAIN) {
		if (pAd->OpMode == OPMODE_STA)
			net_dev->wireless_handlers =
			    (struct iw_handler_def *)&rt28xx_iw_handler_def;
	}
	/* Request interrupt service routine for PCI device */
	/* register the interrupt routine with the os */
	RtmpOSIRQRequest(net_dev);

	/* Init IRQ parameters stored in pAd */
	RTMP_IRQ_INIT(pAd);

	/* Chip & other init */
	if (rt28xx_init(pAd, mac, hostname) == FALSE)
		goto err;

	/* Enable Interrupt */
	RTMP_IRQ_ENABLE(pAd);

	/* Now Enable RxTx */
	RTMPEnableRxTx(pAd);
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_START_UP);

	{
		u32 reg = 0;
		RTMP_IO_READ32(pAd, 0x1300, &reg);	/* clear garbage interrupts */
		printk("0x1300 = %08x\n", reg);
	}

	{
/*      u32 reg; */
/*      u8  byte; */
/*      u16 tmp; */

/*      RTMP_IO_READ32(pAd, XIFS_TIME_CFG, &reg); */

/*      tmp = 0x0805; */
/*      reg  = (reg & 0xffff0000) | tmp; */
/*      RTMP_IO_WRITE32(pAd, XIFS_TIME_CFG, reg); */

	}
#ifdef RTMP_MAC_PCI
	RTMPInitPCIeLinkCtrlValue(pAd);
#endif /* RTMP_MAC_PCI // */

	return (retval);

err:
/*+++Add by shiang, move from rt28xx_init() to here. */
	RtmpOSIRQRelease(net_dev);
/*---Add by shiang, move from rt28xx_init() to here. */
	return (-1);
}				/* End of rt28xx_open */

static const struct net_device_ops rt2860_netdev_ops = {
	.ndo_open = MainVirtualIF_open,
	.ndo_stop = MainVirtualIF_close,
	.ndo_do_ioctl = rt28xx_sta_ioctl,
	.ndo_get_stats = RT28xx_get_ether_stats,
	.ndo_validate_addr = NULL,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_change_mtu = eth_change_mtu,
	.ndo_start_xmit = rt28xx_send_packets,
};

struct net_device *RtmpPhyNetDevInit(struct rt_rtmp_adapter *pAd,
			   struct rt_rtmp_os_netdev_op_hook *pNetDevHook)
{
	struct net_device *net_dev = NULL;
/*      int             Status; */

	net_dev =
	    RtmpOSNetDevCreate(pAd, INT_MAIN, 0, sizeof(struct rt_rtmp_adapter *),
			       INF_MAIN_DEV_NAME);
	if (net_dev == NULL) {
		printk
		    ("RtmpPhyNetDevInit(): creation failed for main physical net device!\n");
		return NULL;
	}

	NdisZeroMemory((unsigned char *)pNetDevHook,
		       sizeof(struct rt_rtmp_os_netdev_op_hook));
	pNetDevHook->netdev_ops = &rt2860_netdev_ops;
	pNetDevHook->priv_flags = INT_MAIN;
	pNetDevHook->needProtcted = FALSE;

	net_dev->ml_priv = (void *)pAd;
	pAd->net_dev = net_dev;

	netif_stop_queue(net_dev);

	return net_dev;

}

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
	struct rt_rtmp_adapter *pAd = NULL;
	int status = NETDEV_TX_OK;
	void *pPacket = (void *)skb;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	/* RT2870STA does this in RTMPSendPackets() */

	{
		/* Drop send request since we are in monitor mode */
		if (MONITOR_ON(pAd)) {
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
			goto done;
		}
	}

	/* EapolStart size is 18 */
	if (skb->len < 14) {
		/*printk("bad packet size: %d\n", pkt->len); */
		hex_dump("bad packet", skb->data, skb->len);
		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		goto done;
	}

	RTMP_SET_PACKET_5VT(pPacket, 0);
	STASendPackets((void *)pAd, (void **)& pPacket, 1);

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
static int rt28xx_send_packets(IN struct sk_buff *skb_p,
			       IN struct net_device *net_dev)
{
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	if (!(net_dev->flags & IFF_UP)) {
		RELEASE_NDIS_PACKET(pAd, (void *)skb_p,
				    NDIS_STATUS_FAILURE);
		return NETDEV_TX_OK;
	}

	NdisZeroMemory((u8 *)& skb_p->cb[CB_OFF], 15);
	RTMP_SET_PACKET_NET_DEVICE_MBSSID(skb_p, MAIN_MBSSID);

	return rt28xx_packet_xmit(skb_p);
}

/* This function will be called when query /proc */
struct iw_statistics *rt28xx_get_wireless_stats(IN struct net_device *net_dev)
{
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_get_wireless_stats --->\n"));

	pAd->iw_stats.status = 0;	/* Status - device dependent for now */

	/* link quality */
	if (pAd->OpMode == OPMODE_STA)
		pAd->iw_stats.qual.qual =
		    ((pAd->Mlme.ChannelQuality * 12) / 10 + 10);

	if (pAd->iw_stats.qual.qual > 100)
		pAd->iw_stats.qual.qual = 100;

	if (pAd->OpMode == OPMODE_STA) {
		pAd->iw_stats.qual.level =
		    RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.LastRssi0,
				pAd->StaCfg.RssiSample.LastRssi1,
				pAd->StaCfg.RssiSample.LastRssi2);
	}

	pAd->iw_stats.qual.noise = pAd->BbpWriteLatch[66];	/* noise level (dBm) */

	pAd->iw_stats.qual.noise += 256 - 143;
	pAd->iw_stats.qual.updated = 1;	/* Flags to know if updated */
#ifdef IW_QUAL_DBM
	pAd->iw_stats.qual.updated |= IW_QUAL_DBM;	/* Level + Noise are dBm */
#endif /* IW_QUAL_DBM // */

	pAd->iw_stats.discard.nwid = 0;	/* Rx : Wrong nwid/essid */
	pAd->iw_stats.miss.beacon = 0;	/* Missed beacons/superframe */

	DBGPRINT(RT_DEBUG_TRACE, ("<--- rt28xx_get_wireless_stats\n"));
	return &pAd->iw_stats;
}

void tbtt_tasklet(unsigned long data)
{
/*#define MAX_TX_IN_TBTT                (16) */

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
static struct net_device_stats *RT28xx_get_ether_stats(IN struct net_device
						       *net_dev)
{
	struct rt_rtmp_adapter *pAd = NULL;

	if (net_dev)
		GET_PAD_FROM_NET_DEV(pAd, net_dev);

	if (pAd) {

		pAd->stats.rx_packets =
		    pAd->WlanCounters.ReceivedFragmentCount.QuadPart;
		pAd->stats.tx_packets =
		    pAd->WlanCounters.TransmittedFragmentCount.QuadPart;

		pAd->stats.rx_bytes = pAd->RalinkCounters.ReceivedByteCount;
		pAd->stats.tx_bytes = pAd->RalinkCounters.TransmittedByteCount;

		pAd->stats.rx_errors = pAd->Counters8023.RxErrors;
		pAd->stats.tx_errors = pAd->Counters8023.TxErrors;

		pAd->stats.rx_dropped = 0;
		pAd->stats.tx_dropped = 0;

		pAd->stats.multicast = pAd->WlanCounters.MulticastReceivedFrameCount.QuadPart;	/* multicast packets received */
		pAd->stats.collisions = pAd->Counters8023.OneCollision + pAd->Counters8023.MoreCollisions;	/* Collision packets */

		pAd->stats.rx_length_errors = 0;
		pAd->stats.rx_over_errors = pAd->Counters8023.RxNoBuffer;	/* receiver ring buff overflow */
		pAd->stats.rx_crc_errors = 0;	/*pAd->WlanCounters.FCSErrorCount;     // recved pkt with crc error */
		pAd->stats.rx_frame_errors = pAd->Counters8023.RcvAlignmentErrors;	/* recv'd frame alignment error */
		pAd->stats.rx_fifo_errors = pAd->Counters8023.RxNoBuffer;	/* recv'r fifo overrun */
		pAd->stats.rx_missed_errors = 0;	/* receiver missed packet */

		/* detailed tx_errors */
		pAd->stats.tx_aborted_errors = 0;
		pAd->stats.tx_carrier_errors = 0;
		pAd->stats.tx_fifo_errors = 0;
		pAd->stats.tx_heartbeat_errors = 0;
		pAd->stats.tx_window_errors = 0;

		/* for cslip etc */
		pAd->stats.rx_compressed = 0;
		pAd->stats.tx_compressed = 0;

		return &pAd->stats;
	} else
		return NULL;
}

BOOLEAN RtmpPhyNetDevExit(struct rt_rtmp_adapter *pAd, struct net_device *net_dev)
{

	/* Unregister network device */
	if (net_dev != NULL) {
		printk
		    ("RtmpOSNetDevDetach(): RtmpOSNetDeviceDetach(), dev->name=%s!\n",
		     net_dev->name);
		RtmpOSNetDevDetach(net_dev);
	}

	return TRUE;

}

/*
========================================================================
Routine Description:
    Allocate memory for adapter control block.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
int AdapterBlockAllocateMemory(void *handle, void ** ppAd)
{

	*ppAd = (void *)vmalloc(sizeof(struct rt_rtmp_adapter));	/*pci_alloc_consistent(pci_dev, sizeof(struct rt_rtmp_adapter), phy_addr); */

	if (*ppAd) {
		NdisZeroMemory(*ppAd, sizeof(struct rt_rtmp_adapter));
		((struct rt_rtmp_adapter *)* ppAd)->OS_Cookie = handle;
		return (NDIS_STATUS_SUCCESS);
	} else {
		return (NDIS_STATUS_FAILURE);
	}
}
