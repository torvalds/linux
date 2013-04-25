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


#define RTMP_MODULE_OS

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"
MODULE_LICENSE("GPL");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#ifndef SA_SHIRQ
#define SA_SHIRQ IRQF_SHARED
#endif
#endif

#ifdef RTMP_MAC_USB
#ifdef OS_ABL_SUPPORT
MODULE_LICENSE("GPL");
#endif /* OS_ABL_SUPPORT */
#endif /* RTMP_MAC_USB */

#ifdef CONFIG_APSTA_MIXED_SUPPORT
/*UINT32 CW_MAX_IN_BITS;*/
#endif /* CONFIG_APSTA_MIXED_SUPPORT */

/*---------------------------------------------------------------------*/
/* Private Variables Used                                              */
/*---------------------------------------------------------------------*/

PSTRING mac = "";		   /* default 00:00:00:00:00:00 */
PSTRING hostname = "";		   /* default CMPC */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12)
MODULE_PARM (mac, "s");
#else
module_param (mac, charp, 0);
#endif
MODULE_PARM_DESC (mac, "rt28xx: wireless mac addr");

#ifdef OS_ABL_SUPPORT
RTMP_DRV_ABL_OPS RtmpDrvOps, *pRtmpDrvOps = &RtmpDrvOps;
RTMP_NET_ABL_OPS RtmpDrvNetOps, *pRtmpDrvNetOps = &RtmpDrvNetOps;
#endif /* OS_ABL_SUPPORT */


/*---------------------------------------------------------------------*/
/* Prototypes of Functions Used                                        */
/*---------------------------------------------------------------------*/

/* public function prototype */
int rt28xx_close(VOID *net_dev);
int rt28xx_open(VOID *net_dev);

/* private function prototype */
static INT rt28xx_send_packets(IN struct sk_buff *skb_p, IN struct net_device *net_dev);




static struct net_device_stats *RT28xx_get_ether_stats(
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
    VOID *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0; /* close ok */

	netif_carrier_off(net_dev);
	netif_stop_queue(net_dev);

	RTMPInfClose(pAd);


	VIRTUAL_IF_DOWN(pAd);

	RT_MOD_DEC_USE_COUNT();

	return 0; /* close ok */
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
    VOID *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0; /* close ok */

#if 0
	while (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
        {
                OS_WAIT(10);
                DBGPRINT(RT_DEBUG_TRACE, ("Card not ready, NDIS_STATUS_SUCCESS!\n"));
        }
#endif 

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
int rt28xx_close(VOID *dev)
{
	struct net_device * net_dev = (struct net_device *)dev;
    VOID	*pAd = NULL;
/*	BOOLEAN 		Cancelled; */
/*	UINT32			i = 0; */
	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt28xx_close\n"));

	/* Sanity check for pAd */
	if (pAd == NULL)
		return 0; /* close ok */

	RTMPDrvClose(pAd, net_dev);



#ifdef VENDOR_FEATURE2_SUPPORT
	printk("Number of Packet Allocated = %lu\n", OS_NumOfPktAlloc);
	printk("Number of Packet Freed = %lu\n", OS_NumOfPktFree);
#endif /* VENDOR_FEATURE2_SUPPORT */

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt28xx_close\n"));
	return 0; /* close ok */
} /* End of rt28xx_close */


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
int rt28xx_open(VOID *dev)
{				 
	struct net_device * net_dev = (struct net_device *)dev;
	VOID *pAd = NULL;
	int retval = 0;
	ULONG OpMode;

#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	struct usb_interface *intf;
	struct usb_device		*pUsb_Dev;
	INT 		pm_usage_cnt;
	UCHAR	Flag;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */

	/* sanity check */
	if (sizeof(ra_dma_addr_t) < sizeof(dma_addr_t))
		DBGPRINT(RT_DEBUG_ERROR, ("Fatal error for DMA address size!!!\n"));
 
	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	/* Sanity check for pAd */
	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -1;
	}

	RTMP_DRIVER_OP_MODE_GET(pAd, &OpMode);

#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

	RTMP_DRIVER_USB_DEV_GET(pAd, &pUsb_Dev);
	RTMP_DRIVER_USB_INTF_GET(pAd, &intf);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	pm_usage_cnt = atomic_read(&intf->pm_usage_cnt);	
#else
	pm_usage_cnt = intf->pm_usage_cnt;
#endif
/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND)) */
	RTMP_DRIVER_ADAPTER_CPU_SUSPEND_TEST(pAd, &Flag);
	if(!Flag)
	{
		if(pm_usage_cnt == 0)
		{
			int res=1;
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
		if(pUsb_Dev->autosuspend_disabled  ==0)
#else
		if(pUsb_Dev->auto_pm ==1)
#endif
#endif
			{
				res = usb_autopm_get_interface(intf);

/*
when system  power level from auto to on, auto_pm is 0 and the function radioon will set fRTMP_ADAPTER_SUSPEND
so we must clear fkag here;

*/				
/*				RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
				RTMP_DRIVER_ADAPTER_SUSPEND_CLEAR(pAd);
				if (res)
				{
					DBGPRINT(RT_DEBUG_ERROR, ("rt28xx_open autopm_resume fail ------\n"));
					return (-1);;
				}			
			}
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_open: fRTMP_ADAPTER_SUSPEND\n"));
/*				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
				RTMP_DRIVER_ADAPTER_SUSPEND_SET(pAd);
				return (-1);
			}
#endif
#endif

		}
 	}
	else
	{
				DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_open: fRTMP_ADAPTER_CPU_SUSPEND\n"));
				return (-1);;
	}

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */


#ifdef CONFIG_APSTA_MIXED_SUPPORT
	if (OpMode == OPMODE_AP)
	{
		/*CW_MAX_IN_BITS = 6; */
		RTMP_DRIVER_MAX_IN_BITS_SET(pAd, 6);
	}
	else if (OpMode == OPMODE_STA)
	{
		/*CW_MAX_IN_BITS = 10; */
		RTMP_DRIVER_MAX_IN_BITS_SET(pAd, 10);
	}
#endif /* CONFIG_APSTA_MIXED_SUPPORT */


#if 0 //WIRELESS_EXT >= 12
/*	if (net_dev->priv_flags == INT_MAIN) */
	if (RTMP_DRIVER_MAIN_INF_CHECK(pAd, net_dev->priv_flags) == NDIS_STATUS_SUCCESS)
	{
#ifdef CONFIG_APSTA_MIXED_SUPPORT
		if (OpMode == OPMODE_AP)
			net_dev->wireless_handlers = (struct iw_handler_def *) &rt28xx_ap_iw_handler_def;
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
#ifdef CONFIG_STA_SUPPORT
		if (OpMode == OPMODE_STA)
			net_dev->wireless_handlers = (struct iw_handler_def *) &rt28xx_iw_handler_def;
#endif /* CONFIG_STA_SUPPORT */
	}
#endif /* WIRELESS_EXT >= 12 */

	/* Request interrupt service routine for PCI device */
	/* register the interrupt routine with the os */
	/*
		AP Channel auto-selection will be run in rt28xx_init(),
		so we must reqister IRQ hander here.
	*/
	RtmpOSIRQRequest(net_dev);

	/* Init IRQ parameters stored in pAd */
/*	RTMP_IRQ_INIT(pAd); */
	RTMP_DRIVER_IRQ_INIT(pAd);

	/* Chip & other init */
	if (rt28xx_init(pAd, mac, hostname) == FALSE)
		goto err;

#if WIRELESS_EXT >= 12
/*      if (net_dev->priv_flags == INT_MAIN) */
        if (RTMP_DRIVER_MAIN_INF_CHECK(pAd, net_dev->priv_flags) == NDIS_STATUS_SUCCESS)
        {
#ifdef CONFIG_APSTA_MIXED_SUPPORT
                if (OpMode == OPMODE_AP)
                        net_dev->wireless_handlers = (struct iw_handler_def *) &rt28xx_ap_iw_handler_def;
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
#ifdef CONFIG_STA_SUPPORT
                if (OpMode == OPMODE_STA)
                        net_dev->wireless_handlers = (struct iw_handler_def *) &rt28xx_iw_handler_def;
#endif /* CONFIG_STA_SUPPORT */
        }
#endif /* WIRELESS_EXT >= 12 */


#ifdef MBSS_SUPPORT
	/* the function can not be moved to RT2860_probe() even register_netdev()
	   is changed as register_netdevice().
	   Or in some PC, kernel will panic (Fedora 4) */
	RT28xx_MBSS_Init(pAd, net_dev);
#endif /* MBSS_SUPPORT */


#ifdef APCLI_SUPPORT
	RT28xx_ApCli_Init(pAd, net_dev);
#endif /* APCLI_SUPPORT */



#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
/*	RT_CFG80211_REINIT(pAd); */
/*	RT_CFG80211_CRDA_REG_RULE_APPLY(pAd); */
	RTMP_DRIVER_CFG80211_START(pAd);
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */

	RTMPDrvOpen(pAd);


#ifdef VENDOR_FEATURE2_SUPPORT
	printk("Number of Packet Allocated in open = %lu\n", OS_NumOfPktAlloc);
	printk("Number of Packet Freed in open = %lu\n", OS_NumOfPktFree);
#endif /* VENDOR_FEATURE2_SUPPORT */

	return (retval);

err:
/*+++Add by shiang, move from rt28xx_init() to here. */
/*	RtmpOSIRQRelease(net_dev); */
	RTMP_DRIVER_IRQ_RELEASE(pAd);
/*---Add by shiang, move from rt28xx_init() to here. */

	return (-1);
} /* End of rt28xx_open */


PNET_DEV RtmpPhyNetDevInit(
	IN VOID						*pAd,
	IN RTMP_OS_NETDEV_OP_HOOK	*pNetDevHook)
{
	struct net_device	*net_dev = NULL;
/*	NDIS_STATUS		Status; */
	ULONG InfId, OpMode;


	RTMP_DRIVER_MAIN_INF_GET(pAd, &InfId);

/*	net_dev = RtmpOSNetDevCreate(pAd, INT_MAIN, 0, sizeof(PRTMP_ADAPTER), INF_MAIN_DEV_NAME); */
	RTMP_DRIVER_MAIN_INF_CREATE(pAd, &net_dev);
	if (net_dev == NULL)
	{
		printk("RtmpPhyNetDevInit(): creation failed for main physical net device!\n");
		return NULL;
	}

	NdisZeroMemory((unsigned char *)pNetDevHook, sizeof(RTMP_OS_NETDEV_OP_HOOK));
	pNetDevHook->open = MainVirtualIF_open;
	pNetDevHook->stop = MainVirtualIF_close;
	pNetDevHook->xmit = rt28xx_send_packets;
#ifdef IKANOS_VX_1X0
	pNetDevHook->xmit = IKANOS_DataFramesTx;
#endif /* IKANOS_VX_1X0 */
	pNetDevHook->ioctl = rt28xx_ioctl;
	pNetDevHook->priv_flags = InfId; /*INT_MAIN; */
	pNetDevHook->get_stats = RT28xx_get_ether_stats;

	pNetDevHook->needProtcted = FALSE;

#if (WIRELESS_EXT < 21) && (WIRELESS_EXT >= 12)
	pNetDevHook->get_wstats = rt28xx_get_wireless_stats;
#endif

	RTMP_DRIVER_OP_MODE_GET(pAd, &OpMode);

#ifdef CONFIG_STA_SUPPORT
#if WIRELESS_EXT >= 12
	if (OpMode == OPMODE_STA)
	{
		pNetDevHook->iw_handler = (void *)&rt28xx_iw_handler_def;
	}
#endif /*WIRELESS_EXT >= 12 */
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_APSTA_MIXED_SUPPORT
#if WIRELESS_EXT >= 12
	if (OpMode == OPMODE_AP)
	{
		pNetDevHook->iw_handler = &rt28xx_ap_iw_handler_def;
	}
#endif /*WIRELESS_EXT >= 12 */
#endif /* CONFIG_APSTA_MIXED_SUPPORT */

	RTMP_OS_NETDEV_SET_PRIV(net_dev, pAd);
/*	pAd->net_dev = net_dev; */

	RTMP_DRIVER_NET_DEV_SET(pAd, net_dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(net_dev);
#endif 

	return net_dev;
	
}


VOID *RtmpNetEthConvertDevSearch(
	IN	VOID			*net_dev_,
	IN	UCHAR			*pData)
{
	struct net_device *pNetDev;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	struct net_device *net_dev = (struct net_device *)net_dev_;
	struct net *net;
	net = dev_net(net_dev);
	
	BUG_ON(!net);
	for_each_netdev(net, pNetDev)
#else
	struct net *net;

	struct net_device *net_dev = (struct net_device *)net_dev_;
	BUG_ON(!net_dev->nd_net);
	net = net_dev->nd_net;
	for_each_netdev(net, pNetDev)
#endif
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
		for_each_netdev(pNetDev)
#else 
	for (pNetDev = dev_base; pNetDev; pNetDev = pNetDev->next)
#endif
#endif
	{
		if ((pNetDev->type == ARPHRD_ETHER)
			&& NdisEqualMemory(pNetDev->dev_addr, &pData[6], pNetDev->addr_len))
			break;
	}

	return (VOID *)pNetDev;
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
int rt28xx_packet_xmit(void *skbsrc)
{
	struct sk_buff *skb = (struct sk_buff *)skbsrc;
	struct net_device *net_dev = skb->dev;
	VOID *pAd = NULL;
/*	int status = 0; */
	PNDIS_PACKET pPacket = (PNDIS_PACKET) skb;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
        {
                RELEASE_NDIS_PACKET(NULL, (PNDIS_PACKET)pPacket, NDIS_STATUS_FAILURE);
                return 0;
        }

	return RTMPSendPackets((NDIS_HANDLE)pAd, (PPNDIS_PACKET) &pPacket, 1,
							skb->len, RtmpNetEthConvertDevSearch);

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
static int rt28xx_send_packets(
	IN struct sk_buff 		*skb_p, 
	IN struct net_device 	*net_dev)
{
/*	RTMP_ADAPTER *pAd = NULL; */

/*	GET_PAD_FROM_NET_DEV(pAd, net_dev); */

	if (!(RTMP_OS_NETDEV_STATE_RUNNING(net_dev)))
	{
		RELEASE_NDIS_PACKET(NULL, (PNDIS_PACKET)skb_p, NDIS_STATUS_FAILURE);
		return 0;
	}

	NdisZeroMemory((PUCHAR)&skb_p->cb[CB_OFF], 15);
	RTMP_SET_PACKET_NET_DEVICE_MBSSID(skb_p, MAIN_MBSSID);
	MEM_DBG_PKT_ALLOC_INC(skb_p);

	return rt28xx_packet_xmit(skb_p);
}


#if WIRELESS_EXT >= 12
/* This function will be called when query /proc */
struct iw_statistics *rt28xx_get_wireless_stats(
    IN struct net_device *net_dev)
{
	VOID *pAd = NULL;
	struct iw_statistics *pStats;
	RT_CMD_IW_STATS DrvIwStats, *pDrvIwStats = &DrvIwStats;


	GET_PAD_FROM_NET_DEV(pAd, net_dev);	


	DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_get_wireless_stats --->\n"));


	pDrvIwStats->priv_flags = net_dev->priv_flags;
	pDrvIwStats->dev_addr = (PUCHAR)net_dev->dev_addr;

	if (RTMP_DRIVER_IW_STATS_GET(pAd, pDrvIwStats) != NDIS_STATUS_SUCCESS)
		return NULL;

	pStats = (struct iw_statistics *)(pDrvIwStats->pStats);
	pStats->status = 0; /* Status - device dependent for now */


	pStats->qual.updated = 1;     /* Flags to know if updated */
#ifdef IW_QUAL_DBM
	pStats->qual.updated |= IW_QUAL_DBM;	/* Level + Noise are dBm */
#endif /* IW_QUAL_DBM */
	pStats->qual.qual = pDrvIwStats->qual;
	pStats->qual.level = pDrvIwStats->level;
	pStats->qual.noise = pDrvIwStats->noise;
	pStats->discard.nwid = 0;     /* Rx : Wrong nwid/essid */
	pStats->miss.beacon = 0;      /* Missed beacons/superframe */
	
	DBGPRINT(RT_DEBUG_TRACE, ("<--- rt28xx_get_wireless_stats\n"));
	return pStats;
}
#endif /* WIRELESS_EXT */



INT rt28xx_ioctl(
	IN	PNET_DEV	net_dev, 
	IN	OUT	struct ifreq	*rq, 
	IN	INT					cmd)
{
	VOID			*pAd = NULL;
	INT				ret = 0;
	ULONG			OpMode;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	if (pAd == NULL)
	{
		/* if 1st open fail, pAd will be free;
		   So the net_dev->priv will be NULL in 2rd open */
		return -ENETDOWN;
	}

	RTMP_DRIVER_OP_MODE_GET(pAd, &OpMode);


#ifdef CONFIG_STA_SUPPORT
/*	IF_DEV_CONFIG_OPMODE_ON_STA(pAd) */
	RT_CONFIG_IF_OPMODE_ON_STA(OpMode)
	{
		ret = rt28xx_sta_ioctl(net_dev, rq, cmd);
	}
#endif /* CONFIG_STA_SUPPORT */

	return ret;
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
static struct net_device_stats *RT28xx_get_ether_stats(
    IN  struct net_device *net_dev)
{
    VOID *pAd = NULL;
	struct net_device_stats *pStats;

	if (net_dev)
		GET_PAD_FROM_NET_DEV(pAd, net_dev);	

	if (pAd)
	{
		RT_CMD_STATS DrvStats, *pDrvStats = &DrvStats;
 

		RTMP_DRIVER_INF_STATS_GET(pAd, pDrvStats);

		pStats = (struct net_device_stats *)(pDrvStats->pStats);
		pStats->rx_packets = pDrvStats->rx_packets;
		pStats->tx_packets = pDrvStats->tx_packets;

		pStats->rx_bytes = pDrvStats->rx_bytes;
		pStats->tx_bytes = pDrvStats->tx_bytes;

		pStats->rx_errors = pDrvStats->rx_errors;
		pStats->tx_errors = pDrvStats->tx_errors;

		pStats->rx_dropped = 0;
		pStats->tx_dropped = 0;

	    pStats->multicast = pDrvStats->multicast;
	    pStats->collisions = pDrvStats->collisions;

	    pStats->rx_length_errors = 0;
	    pStats->rx_over_errors = pDrvStats->rx_over_errors;
	    pStats->rx_crc_errors = 0;/*pAd->WlanCounters.FCSErrorCount;     // recved pkt with crc error */
	    pStats->rx_frame_errors = pDrvStats->rx_frame_errors;
	    pStats->rx_fifo_errors = pDrvStats->rx_fifo_errors;
	    pStats->rx_missed_errors = 0;                                            /* receiver missed packet */

	    /* detailed tx_errors */
	    pStats->tx_aborted_errors = 0;
	    pStats->tx_carrier_errors = 0;
	    pStats->tx_fifo_errors = 0;
	    pStats->tx_heartbeat_errors = 0;
	    pStats->tx_window_errors = 0;

	    /* for cslip etc */
	    pStats->rx_compressed = 0;
	    pStats->tx_compressed = 0;
		
		return pStats;
	}
	else
    	return NULL;
}


BOOLEAN RtmpPhyNetDevExit(
	IN VOID			*pAd, 
	IN PNET_DEV		net_dev)
{




#ifdef INF_PPA_SUPPORT

	RTMP_DRIVER_INF_PPA_EXIT(pAd);
#endif /* INF_PPA_SUPPORT */

	/* Unregister network device */
	if (net_dev != NULL)
	{
		printk("RtmpOSNetDevDetach(): RtmpOSNetDeviceDetach(), dev->name=%s!\n", net_dev->name);
		RtmpOSNetDevDetach(net_dev);
	}

	return TRUE;
	
}


/*******************************************************************************

	Device IRQ related functions.
	
 *******************************************************************************/
int RtmpOSIRQRequest(IN PNET_DEV pNetDev)
{
	ULONG infType;
	VOID *pAd = NULL;
	int retval = 0;
	
	GET_PAD_FROM_NET_DEV(pAd, pNetDev);	
	
	ASSERT(pAd);

	RTMP_DRIVER_INF_TYPE_GET(pAd, &infType);



	return retval; 
	
}





