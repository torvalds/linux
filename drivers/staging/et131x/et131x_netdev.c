/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et131x_netdev.c - Routines and data required by all Linux network devices.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "et131x_version.h"
#include "et131x_debug.h"
#include "et131x_defs.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include "et1310_phy.h"
#include "et1310_pm.h"
#include "et1310_jagcore.h"
#include "et1310_mac.h"
#include "et1310_tx.h"

#include "et131x_adapter.h"
#include "et131x_isr.h"
#include "et131x_initpci.h"

/* Data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
extern dbg_info_t *et131x_dbginfo;
#endif /* CONFIG_ET131X_DEBUG */

struct net_device_stats *et131x_stats(struct net_device *netdev);
int et131x_open(struct net_device *netdev);
int et131x_close(struct net_device *netdev);
int et131x_ioctl(struct net_device *netdev, struct ifreq *reqbuf, int cmd);
void et131x_multicast(struct net_device *netdev);
int et131x_tx(struct sk_buff *skb, struct net_device *netdev);
void et131x_tx_timeout(struct net_device *netdev);
int et131x_change_mtu(struct net_device *netdev, int new_mtu);
int et131x_set_mac_addr(struct net_device *netdev, void *new_mac);
void et131x_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp);
void et131x_vlan_rx_add_vid(struct net_device *netdev, uint16_t vid);
void et131x_vlan_rx_kill_vid(struct net_device *netdev, uint16_t vid);

/**
 * et131x_device_alloc
 *
 * Returns pointer to the allocated and initialized net_device struct for
 * this device.
 *
 * Create instances of net_device and wl_private for the new adapter and
 * register the device's entry points in the net_device structure.
 */
struct net_device *et131x_device_alloc(void)
{
	struct net_device *netdev;

	DBG_ENTER(et131x_dbginfo);

	/* Alloc net_device and adapter structs */
	netdev = alloc_etherdev(sizeof(struct et131x_adapter));

	if (netdev == NULL) {
		DBG_ERROR(et131x_dbginfo,
			  "Alloc of net_device struct failed\n");
		DBG_LEAVE(et131x_dbginfo);
		return NULL;
	}

	/* Setup the function registration table (and other data) for a
	 * net_device
	 */
	//netdev->init               = &et131x_init;
	//netdev->set_config = &et131x_config;
	netdev->get_stats = &et131x_stats;
	netdev->open = &et131x_open;
	netdev->stop = &et131x_close;
	netdev->do_ioctl = &et131x_ioctl;
	netdev->set_multicast_list = &et131x_multicast;
	netdev->hard_start_xmit = &et131x_tx;
	netdev->tx_timeout = &et131x_tx_timeout;
	netdev->watchdog_timeo = ET131X_TX_TIMEOUT;
	netdev->change_mtu = &et131x_change_mtu;
	netdev->set_mac_address = &et131x_set_mac_addr;

	//netdev->ethtool_ops        = &et131x_ethtool_ops;

	// Poll?
	//netdev->poll               = &et131x_poll;
	//netdev->poll_controller    = &et131x_poll_controller;

	DBG_LEAVE(et131x_dbginfo);
	return netdev;
}

/**
 * et131x_stats - Return the current device statistics.
 * @netdev: device whose stats are being queried
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
struct net_device_stats *et131x_stats(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &adapter->net_stats;
	CE_STATS_t *devstat = &adapter->Stats;

	DBG_ENTER(et131x_dbginfo);

	stats->rx_packets = devstat->ipackets;
	stats->tx_packets = devstat->opackets;
	stats->rx_errors = devstat->length_err + devstat->alignment_err +
	    devstat->crc_err + devstat->code_violations + devstat->other_errors;
	stats->tx_errors = devstat->max_pkt_error;
	stats->multicast = devstat->multircv;
	stats->collisions = devstat->collisions;

	stats->rx_length_errors = devstat->length_err;
	stats->rx_over_errors = devstat->rx_ov_flow;
	stats->rx_crc_errors = devstat->crc_err;

	// NOTE: These stats don't have corresponding values in CE_STATS, so we're
	//       going to have to update these directly from within the TX/RX code
	//stats->rx_bytes            = 20; //devstat->;
	//stats->tx_bytes            = 20; //devstat->;
	//stats->rx_dropped          = devstat->;
	//stats->tx_dropped          = devstat->;

	// NOTE: Not used, can't find analogous statistics
	//stats->rx_frame_errors     = devstat->;
	//stats->rx_fifo_errors      = devstat->;
	//stats->rx_missed_errors    = devstat->;

	//stats->tx_aborted_errors   = devstat->;
	//stats->tx_carrier_errors   = devstat->;
	//stats->tx_fifo_errors      = devstat->;
	//stats->tx_heartbeat_errors = devstat->;
	//stats->tx_window_errors    = devstat->;

	DBG_LEAVE(et131x_dbginfo);
	return stats;
}

/**
 * et131x_open - Open the device for use.
 * @netdev: device to be opened
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_open(struct net_device *netdev)
{
	int result = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	DBG_ENTER(et131x_dbginfo);

	/* Start the timer to track NIC errors */
	add_timer(&adapter->ErrorTimer);

	/* Register our ISR */
	DBG_TRACE(et131x_dbginfo, "Registering ISR...\n");

	result =
	    request_irq(netdev->irq, et131x_isr, IRQF_SHARED, netdev->name,
			netdev);
	if (result) {
		DBG_ERROR(et131x_dbginfo, "Could not register ISR\n");
		DBG_LEAVE(et131x_dbginfo);
		return result;
	}

	/* Enable the Tx and Rx DMA engines (if not already enabled) */
	et131x_rx_dma_enable(adapter);
	et131x_tx_dma_enable(adapter);

	/* Enable device interrupts */
	et131x_enable_interrupts(adapter);

	MP_SET_FLAG(adapter, fMP_ADAPTER_INTERRUPT_IN_USE);

	/* We're ready to move some data, so start the queue */
	netif_start_queue(netdev);

	DBG_LEAVE(et131x_dbginfo);
	return result;
}

/**
 * et131x_close - Close the device
 * @netdev: device to be closed
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_close(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	DBG_ENTER(et131x_dbginfo);

	/* First thing is to stop the queue */
	netif_stop_queue(netdev);

	/* Stop the Tx and Rx DMA engines */
	et131x_rx_dma_disable(adapter);
	et131x_tx_dma_disable(adapter);

	/* Disable device interrupts */
	et131x_disable_interrupts(adapter);

	/* Deregistering ISR */
	MP_CLEAR_FLAG(adapter, fMP_ADAPTER_INTERRUPT_IN_USE);

	DBG_TRACE(et131x_dbginfo, "Deregistering ISR...\n");
	free_irq(netdev->irq, netdev);

	/* Stop the error timer */
	del_timer_sync(&adapter->ErrorTimer);

	DBG_LEAVE(et131x_dbginfo);
	return 0;
}

/**
 * et131x_ioctl_mii - The function which handles MII IOCTLs
 * @netdev: device on which the query is being made
 * @reqbuf: the request-specific data buffer
 * @cmd: the command request code
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_ioctl_mii(struct net_device *netdev, struct ifreq *reqbuf, int cmd)
{
	int status = 0;
	struct et131x_adapter *pAdapter = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(reqbuf);

	DBG_ENTER(et131x_dbginfo);

	switch (cmd) {
	case SIOCGMIIPHY:
		DBG_VERBOSE(et131x_dbginfo, "SIOCGMIIPHY\n");
		data->phy_id = pAdapter->Stats.xcvr_addr;
		break;

	case SIOCGMIIREG:
		DBG_VERBOSE(et131x_dbginfo, "SIOCGMIIREG\n");
		if (!capable(CAP_NET_ADMIN)) {
			status = -EPERM;
		} else {
			status = MiRead(pAdapter,
					data->reg_num, &data->val_out);
		}
		break;

	case SIOCSMIIREG:
		DBG_VERBOSE(et131x_dbginfo, "SIOCSMIIREG\n");
		if (!capable(CAP_NET_ADMIN)) {
			status = -EPERM;
		} else {
			status = MiWrite(pAdapter, data->reg_num,
					 data->val_in);
		}
		break;

	default:
		status = -EOPNOTSUPP;
	}

	DBG_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_ioctl - The I/O Control handler for the driver
 * @netdev: device on which the control request is being made
 * @reqbuf: a pointer to the IOCTL request buffer
 * @cmd: the IOCTL command code
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_ioctl(struct net_device *netdev, struct ifreq *reqbuf, int cmd)
{
	int status = 0;

	DBG_ENTER(et131x_dbginfo);

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		status = et131x_ioctl_mii(netdev, reqbuf, cmd);
		break;

	default:
		DBG_WARNING(et131x_dbginfo, "Unhandled IOCTL Code: 0x%04x\n",
			    cmd);
		status = -EOPNOTSUPP;
	}

	DBG_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_set_packet_filter - Configures the Rx Packet filtering on the device
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure
 */
int et131x_set_packet_filter(struct et131x_adapter *adapter)
{
	int status = 0;
	uint32_t filter = adapter->PacketFilter;
	RXMAC_CTRL_t ctrl;
	RXMAC_PF_CTRL_t pf_ctrl;

	DBG_ENTER(et131x_dbginfo);

	ctrl.value = readl(&adapter->CSRAddress->rxmac.ctrl.value);
	pf_ctrl.value = readl(&adapter->CSRAddress->rxmac.pf_ctrl.value);

	/* Default to disabled packet filtering.  Enable it in the individual
	 * case statements that require the device to filter something
	 */
	ctrl.bits.pkt_filter_disable = 1;

	/* Set us to be in promiscuous mode so we receive everything, this
	 * is also true when we get a packet filter of 0
	 */
	if ((filter & ET131X_PACKET_TYPE_PROMISCUOUS) || filter == 0) {
		pf_ctrl.bits.filter_broad_en = 0;
		pf_ctrl.bits.filter_multi_en = 0;
		pf_ctrl.bits.filter_uni_en = 0;
	} else {
		/*
		 * Set us up with Multicast packet filtering.  Three cases are
		 * possible - (1) we have a multi-cast list, (2) we receive ALL
		 * multicast entries or (3) we receive none.
		 */
		if (filter & ET131X_PACKET_TYPE_ALL_MULTICAST) {
			DBG_VERBOSE(et131x_dbginfo,
				    "Multicast filtering OFF (Rx ALL MULTICAST)\n");
			pf_ctrl.bits.filter_multi_en = 0;
		} else {
			DBG_VERBOSE(et131x_dbginfo, "Multicast filtering ON\n");
			SetupDeviceForMulticast(adapter);
			pf_ctrl.bits.filter_multi_en = 1;
			ctrl.bits.pkt_filter_disable = 0;
		}

		/* Set us up with Unicast packet filtering */
		if (filter & ET131X_PACKET_TYPE_DIRECTED) {
			DBG_VERBOSE(et131x_dbginfo, "Unicast Filtering ON\n");
			SetupDeviceForUnicast(adapter);
			pf_ctrl.bits.filter_uni_en = 1;
			ctrl.bits.pkt_filter_disable = 0;
		}

		/* Set us up with Broadcast packet filtering */
		if (filter & ET131X_PACKET_TYPE_BROADCAST) {
			DBG_VERBOSE(et131x_dbginfo, "Broadcast Filtering ON\n");
			pf_ctrl.bits.filter_broad_en = 1;
			ctrl.bits.pkt_filter_disable = 0;
		} else {
			DBG_VERBOSE(et131x_dbginfo,
				    "Broadcast Filtering OFF\n");
			pf_ctrl.bits.filter_broad_en = 0;
		}

		/* Setup the receive mac configuration registers - Packet
		 * Filter control + the enable / disable for packet filter
		 * in the control reg.
		 */
		writel(pf_ctrl.value,
		       &adapter->CSRAddress->rxmac.pf_ctrl.value);
		writel(ctrl.value, &adapter->CSRAddress->rxmac.ctrl.value);
	}

	DBG_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_multicast - The handler to configure multicasting on the interface
 * @netdev: a pointer to a net_device struct representing the device
 */
void et131x_multicast(struct net_device *netdev)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	uint32_t PacketFilter = 0;
	uint32_t count;
	unsigned long lockflags;
	struct dev_mc_list *mclist = netdev->mc_list;

	DBG_ENTER(et131x_dbginfo);

	spin_lock_irqsave(&adapter->Lock, lockflags);

	/* Before we modify the platform-independent filter flags, store them
	 * locally. This allows us to determine if anything's changed and if
	 * we even need to bother the hardware
	 */
	PacketFilter = adapter->PacketFilter;

	/* Clear the 'multicast' flag locally; becuase we only have a single
	 * flag to check multicast, and multiple multicast addresses can be
	 * set, this is the easiest way to determine if more than one
	 * multicast address is being set.
	 */
	PacketFilter &= ~ET131X_PACKET_TYPE_MULTICAST;

	/* Check the net_device flags and set the device independent flags
	 * accordingly
	 */
	DBG_VERBOSE(et131x_dbginfo,
		    "MULTICAST ADDR COUNT: %d\n", netdev->mc_count);

	if (netdev->flags & IFF_PROMISC) {
		DBG_VERBOSE(et131x_dbginfo, "Request: PROMISCUOUS MODE ON\n");
		adapter->PacketFilter |= ET131X_PACKET_TYPE_PROMISCUOUS;
	} else {
		DBG_VERBOSE(et131x_dbginfo, "Request: PROMISCUOUS MODE OFF\n");
		adapter->PacketFilter &= ~ET131X_PACKET_TYPE_PROMISCUOUS;
	}

	if (netdev->flags & IFF_ALLMULTI) {
		DBG_VERBOSE(et131x_dbginfo, "Request: ACCEPT ALL MULTICAST\n");
		adapter->PacketFilter |= ET131X_PACKET_TYPE_ALL_MULTICAST;
	}

	if (netdev->mc_count > NIC_MAX_MCAST_LIST) {
		DBG_WARNING(et131x_dbginfo,
			    "ACCEPT ALL MULTICAST for now, as there's more Multicast "
			    "addresses than the HW supports\n");

		adapter->PacketFilter |= ET131X_PACKET_TYPE_ALL_MULTICAST;
	}

	if (netdev->mc_count < 1) {
		DBG_VERBOSE(et131x_dbginfo, "Request: REJECT ALL MULTICAST\n");
		adapter->PacketFilter &= ~ET131X_PACKET_TYPE_ALL_MULTICAST;
		adapter->PacketFilter &= ~ET131X_PACKET_TYPE_MULTICAST;
	} else {
		DBG_VERBOSE(et131x_dbginfo,
			    "Request: SET MULTICAST FILTER(S)\n");
		adapter->PacketFilter |= ET131X_PACKET_TYPE_MULTICAST;
	}

	/* Set values in the private adapter struct */
	adapter->MCAddressCount = netdev->mc_count;

	if (netdev->mc_count) {
		if (mclist->dmi_addrlen != ETH_ALEN) {
			DBG_WARNING(et131x_dbginfo,
				    "Multicast addrs are not ETH_ALEN in size\n");
		} else {
			count = netdev->mc_count - 1;
			memcpy(adapter->MCList[count], mclist->dmi_addr,
			       ETH_ALEN);
		}
	}

	/* Are the new flags different from the previous ones? If not, then no
	 * action is required
	 *
	 * NOTE - This block will always update the MCList with the hardware,
	 *        even if the addresses aren't the same.
	 */
	if (PacketFilter != adapter->PacketFilter) {
		/* Call the device's filter function */
		DBG_VERBOSE(et131x_dbginfo, "UPDATE REQUIRED, FLAGS changed\n");

		et131x_set_packet_filter(adapter);
	} else {
		DBG_VERBOSE(et131x_dbginfo,
			    "NO UPDATE REQUIRED, FLAGS didn't change\n");
	}

	spin_unlock_irqrestore(&adapter->Lock, lockflags);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_tx - The handler to tx a packet on the device
 * @skb: data to be Tx'd
 * @netdev: device on which data is to be Tx'd
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_tx(struct sk_buff *skb, struct net_device *netdev)
{
	int status = 0;

	DBG_TX_ENTER(et131x_dbginfo);

	/* Save the timestamp for the TX timeout watchdog */
	netdev->trans_start = jiffies;

	/* Call the device-specific data Tx routine */
	status = et131x_send_packets(skb, netdev);

	/* Check status and manage the netif queue if necessary */
	if (status != 0) {
		if (status == -ENOMEM) {
			DBG_VERBOSE(et131x_dbginfo,
				    "OUT OF TCBs; STOP NETIF QUEUE\n");

			/* Put the queue to sleep until resources are
			 * available
			 */
			netif_stop_queue(netdev);
			status = 1;
		} else {
			DBG_WARNING(et131x_dbginfo,
				    "Misc error; drop packet\n");
			status = 0;
		}
	}

	DBG_TX_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_tx_timeout - Timeout handler
 * @netdev: a pointer to a net_device struct representing the device
 *
 * The handler called when a Tx request times out. The timeout period is
 * specified by the 'tx_timeo" element in the net_device structure (see
 * et131x_alloc_device() to see how this value is set).
 */
void et131x_tx_timeout(struct net_device *netdev)
{
	struct et131x_adapter *pAdapter = netdev_priv(netdev);
	PMP_TCB pMpTcb;
	unsigned long lockflags;

	DBG_WARNING(et131x_dbginfo, "TX TIMEOUT\n");

	/* Just skip this part if the adapter is doing link detection */
	if (MP_TEST_FLAG(pAdapter, fMP_ADAPTER_LINK_DETECTION)) {
		DBG_ERROR(et131x_dbginfo, "Still doing link detection\n");
		return;
	}

	/* Any nonrecoverable hardware error?
	 * Checks adapter->flags for any failure in phy reading
	 */
	if (MP_TEST_FLAG(pAdapter, fMP_ADAPTER_NON_RECOVER_ERROR)) {
		DBG_WARNING(et131x_dbginfo, "Non recoverable error - remove\n");
		return;
	}

	/* Hardware failure? */
	if (MP_TEST_FLAG(pAdapter, fMP_ADAPTER_HARDWARE_ERROR)) {
		DBG_WARNING(et131x_dbginfo, "hardware error - reset\n");
		return;
	}

	/* Is send stuck? */
	spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

	pMpTcb = pAdapter->TxRing.CurrSendHead;

	if (pMpTcb != NULL) {
		pMpTcb->Count++;

		if (pMpTcb->Count > NIC_SEND_HANG_THRESHOLD) {
#ifdef CONFIG_ET131X_DEBUG
			TX_STATUS_BLOCK_t txDmaComplete =
			    *(pAdapter->TxRing.pTxStatusVa);
			PTX_DESC_ENTRY_t pDesc =
			    pAdapter->TxRing.pTxDescRingVa +
			    pMpTcb->WrIndex.bits.val;
#endif
			TX_DESC_ENTRY_t StuckDescriptors[10];

			if (pMpTcb->WrIndex.bits.val > 7) {
				memcpy(StuckDescriptors,
				       pAdapter->TxRing.pTxDescRingVa +
				       pMpTcb->WrIndex.bits.val - 6,
				       sizeof(TX_DESC_ENTRY_t) * 10);
			}

			spin_unlock_irqrestore(&pAdapter->TCBSendQLock,
					       lockflags);

			DBG_WARNING(et131x_dbginfo,
				    "Send stuck - reset.  pMpTcb->WrIndex %x, Flags 0x%08x\n",
				    pMpTcb->WrIndex.bits.val,
				    pMpTcb->Flags);

			DBG_WARNING(et131x_dbginfo,
				    "pDesc 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
				    pDesc->DataBufferPtrHigh,
				    pDesc->DataBufferPtrLow, pDesc->word2.value,
				    pDesc->word3.value);

			DBG_WARNING(et131x_dbginfo,
				    "WbStatus 0x%08x\n", txDmaComplete.value);

#ifdef CONFIG_ET131X_DEBUG
			DumpDeviceBlock(DBG_WARNING_ON, pAdapter, 0);
			DumpDeviceBlock(DBG_WARNING_ON, pAdapter, 1);
			DumpDeviceBlock(DBG_WARNING_ON, pAdapter, 3);
			DumpDeviceBlock(DBG_WARNING_ON, pAdapter, 5);
#endif
			et131x_close(netdev);
			et131x_open(netdev);

			return;
		}
	}

	spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);
}

/**
 * et131x_change_mtu - The handler called to change the MTU for the device
 * @netdev: device whose MTU is to be changed
 * @new_mtu: the desired MTU
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_change_mtu(struct net_device *netdev, int new_mtu)
{
	int result = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	DBG_ENTER(et131x_dbginfo);

	/* Make sure the requested MTU is valid */
	if (new_mtu == 0 || new_mtu > 9216) {
		DBG_LEAVE(et131x_dbginfo);
		return -EINVAL;
	}

	/* Stop the netif queue */
	netif_stop_queue(netdev);

	/* Stop the Tx and Rx DMA engines */
	et131x_rx_dma_disable(adapter);
	et131x_tx_dma_disable(adapter);

	/* Disable device interrupts */
	et131x_disable_interrupts(adapter);
	et131x_handle_send_interrupt(adapter);
	et131x_handle_recv_interrupt(adapter);

	/* Set the new MTU */
	netdev->mtu = new_mtu;

	/* Free Rx DMA memory */
	et131x_adapter_memory_free(adapter);

	/* Set the config parameter for Jumbo Packet support */
	adapter->RegistryJumboPacket = new_mtu + 14;
	et131x_soft_reset(adapter);

	/* Alloc and init Rx DMA memory */
	result = et131x_adapter_memory_alloc(adapter);
	if (result != 0) {
		DBG_WARNING(et131x_dbginfo,
			    "Change MTU failed; couldn't re-alloc DMA memory\n");
		return result;
	}

	et131x_init_send(adapter);

	et131x_setup_hardware_properties(adapter);
	memcpy(netdev->dev_addr, adapter->CurrentAddress, ETH_ALEN);

	/* Init the device with the new settings */
	et131x_adapter_setup(adapter);

	/* Enable interrupts */
	if (MP_TEST_FLAG(adapter, fMP_ADAPTER_INTERRUPT_IN_USE)) {
		et131x_enable_interrupts(adapter);
	}

	/* Restart the Tx and Rx DMA engines */
	et131x_rx_dma_enable(adapter);
	et131x_tx_dma_enable(adapter);

	/* Restart the netif queue */
	netif_wake_queue(netdev);

	DBG_LEAVE(et131x_dbginfo);
	return result;
}

/**
 * et131x_set_mac_addr - handler to change the MAC address for the device
 * @netdev: device whose MAC is to be changed
 * @new_mac: the desired MAC address
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 *
 * IMPLEMENTED BY : blux http://berndlux.de 22.01.2007 21:14
 */
int et131x_set_mac_addr(struct net_device *netdev, void *new_mac)
{
	int result = 0;
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *address = new_mac;

	DBG_ENTER(et131x_dbginfo);
	// begin blux
	// DBG_VERBOSE( et131x_dbginfo, "Function not implemented!!\n" );

	if (adapter == NULL) {
		DBG_LEAVE(et131x_dbginfo);
		return -ENODEV;
	}

	/* Make sure the requested MAC is valid */
	if (!is_valid_ether_addr(address->sa_data)) {
		DBG_LEAVE(et131x_dbginfo);
		return -EINVAL;
	}

	/* Stop the netif queue */
	netif_stop_queue(netdev);

	/* Stop the Tx and Rx DMA engines */
	et131x_rx_dma_disable(adapter);
	et131x_tx_dma_disable(adapter);

	/* Disable device interrupts */
	et131x_disable_interrupts(adapter);
	et131x_handle_send_interrupt(adapter);
	et131x_handle_recv_interrupt(adapter);

	/* Set the new MAC */
	// netdev->set_mac_address  = &new_mac;
	// netdev->mtu = new_mtu;

	memcpy(netdev->dev_addr, address->sa_data, netdev->addr_len);

	printk("%s: Setting MAC address to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       netdev->name, netdev->dev_addr[0], netdev->dev_addr[1],
	       netdev->dev_addr[2], netdev->dev_addr[3], netdev->dev_addr[4],
	       netdev->dev_addr[5]);

	/* Free Rx DMA memory */
	et131x_adapter_memory_free(adapter);

	/* Set the config parameter for Jumbo Packet support */
	// adapter->RegistryJumboPacket = new_mtu + 14;
	// blux: not needet here, w'll change the MAC

	et131x_soft_reset(adapter);

	/* Alloc and init Rx DMA memory */
	result = et131x_adapter_memory_alloc(adapter);
	if (result != 0) {
		DBG_WARNING(et131x_dbginfo,
			    "Change MAC failed; couldn't re-alloc DMA memory\n");
		return result;
	}

	et131x_init_send(adapter);

	et131x_setup_hardware_properties(adapter);
	// memcpy( netdev->dev_addr, adapter->CurrentAddress, ETH_ALEN );
	// blux: no, do not override our nice address

	/* Init the device with the new settings */
	et131x_adapter_setup(adapter);

	/* Enable interrupts */
	if (MP_TEST_FLAG(adapter, fMP_ADAPTER_INTERRUPT_IN_USE)) {
		et131x_enable_interrupts(adapter);
	}

	/* Restart the Tx and Rx DMA engines */
	et131x_rx_dma_enable(adapter);
	et131x_tx_dma_enable(adapter);

	/* Restart the netif queue */
	netif_wake_queue(netdev);

	DBG_LEAVE(et131x_dbginfo);
	return result;
}
