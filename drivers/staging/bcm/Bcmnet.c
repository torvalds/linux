#include "headers.h"

struct net_device *gblpnetdev;
/***************************************************************************************/
/* proto-type of lower function */

static INT bcm_open(struct net_device *dev)
{
    PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);

    if(Adapter->fw_download_done==FALSE)
        return -EINVAL;
	if(Adapter->LinkUpStatus == 1){
		if(netif_queue_stopped(Adapter->dev)){
			netif_carrier_on(Adapter->dev);
			netif_start_queue(Adapter->dev);
		}
	}

    return 0;
}

static INT bcm_close(struct net_device *dev)
{
   PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);

	if(!netif_queue_stopped(dev)) {
		netif_carrier_off(dev);
	    netif_stop_queue(dev);
	}
    return 0;
}

static struct net_device_stats *bcm_get_stats(struct net_device *dev)
{
	PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);
	struct net_device_stats*  	netstats = &dev->stats;

	netstats->rx_packets = atomic_read(&Adapter->RxRollOverCount)*64*1024
		+ Adapter->PrevNumRecvDescs;
	netstats->rx_bytes = atomic_read(&Adapter->GoodRxByteCount)
		+ atomic_read(&Adapter->BadRxByteCount);

	netstats->rx_dropped = atomic_read(&Adapter->RxPacketDroppedCount);
	netstats->rx_errors  = atomic_read(&Adapter->RxPacketDroppedCount);
	netstats->tx_bytes   = atomic_read(&Adapter->GoodTxByteCount);
	netstats->tx_packets = atomic_read(&Adapter->TxTotalPacketCount);
	netstats->tx_dropped = atomic_read(&Adapter->TxDroppedPacketCount);

	return netstats;
}

static u16 bcm_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	return ClassifyPacket(netdev_priv(dev), skb);
}


/*******************************************************************
* Function    -	bcm_transmit()
*
* Description - This is the main transmit function for our virtual
*				interface(eth0). It handles the ARP packets. It
*				clones this packet and then Queue it to a suitable
* 		 		Queue. Then calls the transmit_packet().
*
* Parameter   -	 skb - Pointer to the socket buffer structure
*				 dev - Pointer to the virtual net device structure
*
*********************************************************************/

static netdev_tx_t bcm_transmit(struct sk_buff *skb, struct net_device *dev)
{
	PMINI_ADAPTER      Adapter = GET_BCM_ADAPTER(dev);
	u16 qindex = skb_get_queue_mapping(skb);

	if (Adapter->device_removed || !Adapter->LinkUpStatus)
		goto drop;

	if (Adapter->TransferMode != IP_PACKET_ONLY_MODE )
		goto drop;

	if (INVALID_QUEUE_INDEX==qindex)
		goto drop;

	if (Adapter->PackInfo[qindex].uiCurrentPacketsOnHost >= SF_MAX_ALLOWED_PACKETS_TO_BACKUP)
		return NETDEV_TX_BUSY;

	/* Now Enqueue the packet */
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL,
			"bcm_transmit Enqueueing the Packet To Queue %d",qindex);
	spin_lock(&Adapter->PackInfo[qindex].SFQueueLock);
	Adapter->PackInfo[qindex].uiCurrentBytesOnHost += skb->len;
	Adapter->PackInfo[qindex].uiCurrentPacketsOnHost++;

	*((B_UINT32 *)skb->cb + SKB_CB_LATENCY_OFFSET ) = jiffies;
	ENQUEUEPACKET(Adapter->PackInfo[qindex].FirstTxQueue,
		      Adapter->PackInfo[qindex].LastTxQueue, skb);
	atomic_inc(&Adapter->TotalPacketCount);
	spin_unlock(&Adapter->PackInfo[qindex].SFQueueLock);

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL,"ENQ: \n");

	/* FIXME - this is racy and incorrect, replace with work queue */
	if (!atomic_read(&Adapter->TxPktAvail)) {
		atomic_set(&Adapter->TxPktAvail, 1);
		wake_up(&Adapter->tx_packet_wait_queue);
	}
	return NETDEV_TX_OK;

 drop:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}



/**
@ingroup init_functions
Register other driver entry points with the kernel
*/
static const struct net_device_ops bcmNetDevOps = {
    .ndo_open		= bcm_open,
    .ndo_stop 		= bcm_close,
    .ndo_get_stats 	= bcm_get_stats,
    .ndo_start_xmit	= bcm_transmit,
    .ndo_change_mtu	= eth_change_mtu,
    .ndo_set_mac_address = eth_mac_addr,
    .ndo_validate_addr	= eth_validate_addr,
    .ndo_select_queue	= bcm_select_queue,
};

static struct device_type wimax_type = {
	.name	= "wimax",
};

static int bcm_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cmd->supported		= 0;
	cmd->advertising	= 0;
	cmd->speed		= SPEED_10000;
	cmd->duplex		= DUPLEX_FULL;
	cmd->port		= PORT_TP;
	cmd->phy_address	= 0;
	cmd->transceiver	= XCVR_INTERNAL;
	cmd->autoneg		= AUTONEG_DISABLE;
	cmd->maxtxpkt		= 0;
	cmd->maxrxpkt		= 0;
	return 0;
}

static void bcm_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);
	PS_INTERFACE_ADAPTER psIntfAdapter = Adapter->pvInterfaceAdapter;
	struct usb_device *udev = interface_to_usbdev(psIntfAdapter->interface);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	snprintf(info->fw_version, sizeof(info->fw_version), "%u.%u",
		 Adapter->uiFlashLayoutMajorVersion,
		 Adapter->uiFlashLayoutMinorVersion);

	usb_make_path(udev, info->bus_info, sizeof(info->bus_info));
}

static u32 bcm_get_link(struct net_device *dev)
{
	PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);

	return Adapter->LinkUpStatus;
}

static const struct ethtool_ops bcm_ethtool_ops = {
	.get_settings	= bcm_get_settings,
	.get_drvinfo	= bcm_get_drvinfo,
	.get_link 	= bcm_get_link,
};

int register_networkdev(PMINI_ADAPTER Adapter)
{
	struct net_device *net = Adapter->dev;
	int result;

        net->netdev_ops = &bcmNetDevOps;
	net->ethtool_ops = &bcm_ethtool_ops;
	net->mtu          = MTU_SIZE; /* 1400 Bytes */
	net->tx_queue_len = TX_QLEN;
	net->flags |= IFF_NOARP;
	net->flags &= ~(IFF_BROADCAST|IFF_MULTICAST);

	netif_carrier_off(net);

	SET_NETDEV_DEVTYPE(net, &wimax_type);

	/* Read the MAC Address from EEPROM */
	ReadMacAddressFromNVM(Adapter);

	result = register_netdev(net);
	if (result == 0)
		gblpnetdev = Adapter->dev = net;
	else {
		Adapter->dev = NULL;
		free_netdev(net);
	}

	return result;
}

static int bcm_init(void)
{
	printk(KERN_INFO "%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	printk(KERN_INFO "%s\n", DRV_COPYRIGHT);

	return InterfaceInitialize();
}


static void bcm_exit(void)
{
	InterfaceExit();
}

module_init(bcm_init);
module_exit(bcm_exit);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE ("GPL");

