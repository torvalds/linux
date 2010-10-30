#include "headers.h"

#define DRV_NAME	"beceem"
#define DRV_VERSION	"5.2.7.3P1"
#define DRV_DESCRIPTION "Beceem Communications Inc. WiMAX driver"
#define DRV_COPYRIGHT	"Copyright 2010. Beceem Communications Inc"


struct net_device *gblpnetdev;
/***************************************************************************************/
/* proto-type of lower function */

static INT bcm_open(struct net_device *dev)
{
    PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);

    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "======>");
    if(Adapter->fw_download_done==FALSE)
        return -EINVAL;
	Adapter->if_up=1;
	if(Adapter->LinkUpStatus == 1){
		if(netif_queue_stopped(Adapter->dev)){
			netif_carrier_on(Adapter->dev);
			netif_start_queue(Adapter->dev);
		}
	}

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "<======");
    return 0;
}

static INT bcm_close(struct net_device *dev)
{
   PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(dev);

    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "=====>");
	Adapter->if_up=0;
	if(!netif_queue_stopped(dev)) {
		netif_carrier_off(dev);
	    netif_stop_queue(dev);
	}
    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL,"<=====");
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
	struct net_device *net;
	PMINI_ADAPTER *temp;
	PS_INTERFACE_ADAPTER psIntfAdapter = Adapter->pvInterfaceAdapter;
	struct usb_interface *uintf = psIntfAdapter->interface;
	int result;

	net = alloc_etherdev(sizeof(PMINI_ADAPTER));
	if(!net) {
		pr_notice("bcmnet: no memory for device\n");
		return -ENOMEM;
	}

	Adapter->dev = net;	/* FIXME - only allows one adapter! */
	temp = netdev_priv(net);
	*temp = Adapter;

        net->netdev_ops = &bcmNetDevOps;
	net->ethtool_ops = &bcm_ethtool_ops;
	net->mtu          = MTU_SIZE; /* 1400 Bytes */
	net->tx_queue_len = TX_QLEN;

	SET_NETDEV_DEV(net, &uintf->dev);
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

void bcm_unregister_networkdev(PMINI_ADAPTER Adapter)
{
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Unregistering the Net Dev...\n");
	if(Adapter->dev) {
		unregister_netdev(Adapter->dev);
		Adapter->dev = NULL;
	}
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

