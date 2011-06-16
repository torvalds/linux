/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 *
 * Copyright (C) 2008-2009, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Maintained by: Shreyas Bhatewara <pv-drivers@vmware.com>
 *
 */


#include "vmxnet3_int.h"

struct vmxnet3_stat_desc {
	char desc[ETH_GSTRING_LEN];
	int  offset;
};


static u32
vmxnet3_get_rx_csum(struct net_device *netdev)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	return adapter->rxcsum;
}


static int
vmxnet3_set_rx_csum(struct net_device *netdev, u32 val)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	if (adapter->rxcsum != val) {
		adapter->rxcsum = val;
		if (netif_running(netdev)) {
			if (val)
				adapter->shared->devRead.misc.uptFeatures |=
								UPT1_F_RXCSUM;
			else
				adapter->shared->devRead.misc.uptFeatures &=
								~UPT1_F_RXCSUM;

			VMXNET3_WRITE_BAR1_REG(adapter, VMXNET3_REG_CMD,
					       VMXNET3_CMD_UPDATE_FEATURE);
		}
	}
	return 0;
}


/* per tq stats maintained by the device */
static const struct vmxnet3_stat_desc
vmxnet3_tq_dev_stats[] = {
	/* description,         offset */
	{ "TSO pkts tx",        offsetof(struct UPT1_TxStats, TSOPktsTxOK) },
	{ "TSO bytes tx",       offsetof(struct UPT1_TxStats, TSOBytesTxOK) },
	{ "ucast pkts tx",      offsetof(struct UPT1_TxStats, ucastPktsTxOK) },
	{ "ucast bytes tx",     offsetof(struct UPT1_TxStats, ucastBytesTxOK) },
	{ "mcast pkts tx",      offsetof(struct UPT1_TxStats, mcastPktsTxOK) },
	{ "mcast bytes tx",     offsetof(struct UPT1_TxStats, mcastBytesTxOK) },
	{ "bcast pkts tx",      offsetof(struct UPT1_TxStats, bcastPktsTxOK) },
	{ "bcast bytes tx",     offsetof(struct UPT1_TxStats, bcastBytesTxOK) },
	{ "pkts tx err",        offsetof(struct UPT1_TxStats, pktsTxError) },
	{ "pkts tx discard",    offsetof(struct UPT1_TxStats, pktsTxDiscard) },
};

/* per tq stats maintained by the driver */
static const struct vmxnet3_stat_desc
vmxnet3_tq_driver_stats[] = {
	/* description,         offset */
	{"drv dropped tx total", offsetof(struct vmxnet3_tq_driver_stats,
					drop_total) },
	{ "   too many frags",  offsetof(struct vmxnet3_tq_driver_stats,
					drop_too_many_frags) },
	{ "   giant hdr",       offsetof(struct vmxnet3_tq_driver_stats,
					drop_oversized_hdr) },
	{ "   hdr err",         offsetof(struct vmxnet3_tq_driver_stats,
					drop_hdr_inspect_err) },
	{ "   tso",             offsetof(struct vmxnet3_tq_driver_stats,
					drop_tso) },
	{ "ring full",          offsetof(struct vmxnet3_tq_driver_stats,
					tx_ring_full) },
	{ "pkts linearized",    offsetof(struct vmxnet3_tq_driver_stats,
					linearized) },
	{ "hdr cloned",         offsetof(struct vmxnet3_tq_driver_stats,
					copy_skb_header) },
	{ "giant hdr",          offsetof(struct vmxnet3_tq_driver_stats,
					oversized_hdr) },
};

/* per rq stats maintained by the device */
static const struct vmxnet3_stat_desc
vmxnet3_rq_dev_stats[] = {
	{ "LRO pkts rx",        offsetof(struct UPT1_RxStats, LROPktsRxOK) },
	{ "LRO byte rx",        offsetof(struct UPT1_RxStats, LROBytesRxOK) },
	{ "ucast pkts rx",      offsetof(struct UPT1_RxStats, ucastPktsRxOK) },
	{ "ucast bytes rx",     offsetof(struct UPT1_RxStats, ucastBytesRxOK) },
	{ "mcast pkts rx",      offsetof(struct UPT1_RxStats, mcastPktsRxOK) },
	{ "mcast bytes rx",     offsetof(struct UPT1_RxStats, mcastBytesRxOK) },
	{ "bcast pkts rx",      offsetof(struct UPT1_RxStats, bcastPktsRxOK) },
	{ "bcast bytes rx",     offsetof(struct UPT1_RxStats, bcastBytesRxOK) },
	{ "pkts rx out of buf", offsetof(struct UPT1_RxStats, pktsRxOutOfBuf) },
	{ "pkts rx err",        offsetof(struct UPT1_RxStats, pktsRxError) },
};

/* per rq stats maintained by the driver */
static const struct vmxnet3_stat_desc
vmxnet3_rq_driver_stats[] = {
	/* description,         offset */
	{ "drv dropped rx total", offsetof(struct vmxnet3_rq_driver_stats,
					   drop_total) },
	{ "   err",            offsetof(struct vmxnet3_rq_driver_stats,
					drop_err) },
	{ "   fcs",            offsetof(struct vmxnet3_rq_driver_stats,
					drop_fcs) },
	{ "rx buf alloc fail", offsetof(struct vmxnet3_rq_driver_stats,
					rx_buf_alloc_failure) },
};

/* gloabl stats maintained by the driver */
static const struct vmxnet3_stat_desc
vmxnet3_global_stats[] = {
	/* description,         offset */
	{ "tx timeout count",   offsetof(struct vmxnet3_adapter,
					 tx_timeout_count) }
};


struct net_device_stats *
vmxnet3_get_stats(struct net_device *netdev)
{
	struct vmxnet3_adapter *adapter;
	struct vmxnet3_tq_driver_stats *drvTxStats;
	struct vmxnet3_rq_driver_stats *drvRxStats;
	struct UPT1_TxStats *devTxStats;
	struct UPT1_RxStats *devRxStats;
	struct net_device_stats *net_stats = &netdev->stats;

	adapter = netdev_priv(netdev);

	/* Collect the dev stats into the shared area */
	VMXNET3_WRITE_BAR1_REG(adapter, VMXNET3_REG_CMD, VMXNET3_CMD_GET_STATS);

	/* Assuming that we have a single queue device */
	devTxStats = &adapter->tqd_start->stats;
	devRxStats = &adapter->rqd_start->stats;

	/* Get access to the driver stats per queue */
	drvTxStats = &adapter->tx_queue.stats;
	drvRxStats = &adapter->rx_queue.stats;

	memset(net_stats, 0, sizeof(*net_stats));

	net_stats->rx_packets = devRxStats->ucastPktsRxOK +
				devRxStats->mcastPktsRxOK +
				devRxStats->bcastPktsRxOK;

	net_stats->tx_packets = devTxStats->ucastPktsTxOK +
				devTxStats->mcastPktsTxOK +
				devTxStats->bcastPktsTxOK;

	net_stats->rx_bytes = devRxStats->ucastBytesRxOK +
			      devRxStats->mcastBytesRxOK +
			      devRxStats->bcastBytesRxOK;

	net_stats->tx_bytes = devTxStats->ucastBytesTxOK +
			      devTxStats->mcastBytesTxOK +
			      devTxStats->bcastBytesTxOK;

	net_stats->rx_errors = devRxStats->pktsRxError;
	net_stats->tx_errors = devTxStats->pktsTxError;
	net_stats->rx_dropped = drvRxStats->drop_total;
	net_stats->tx_dropped = drvTxStats->drop_total;
	net_stats->multicast =  devRxStats->mcastPktsRxOK;

	return net_stats;
}

static int
vmxnet3_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(vmxnet3_tq_dev_stats) +
			ARRAY_SIZE(vmxnet3_tq_driver_stats) +
			ARRAY_SIZE(vmxnet3_rq_dev_stats) +
			ARRAY_SIZE(vmxnet3_rq_driver_stats) +
			ARRAY_SIZE(vmxnet3_global_stats);
	default:
		return -EOPNOTSUPP;
	}
}


static int
vmxnet3_get_regs_len(struct net_device *netdev)
{
	return 20 * sizeof(u32);
}


static void
vmxnet3_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, vmxnet3_driver_name, sizeof(drvinfo->driver));
	drvinfo->driver[sizeof(drvinfo->driver) - 1] = '\0';

	strlcpy(drvinfo->version, VMXNET3_DRIVER_VERSION_REPORT,
		sizeof(drvinfo->version));
	drvinfo->driver[sizeof(drvinfo->version) - 1] = '\0';

	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	drvinfo->fw_version[sizeof(drvinfo->fw_version) - 1] = '\0';

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		ETHTOOL_BUSINFO_LEN);
	drvinfo->n_stats = vmxnet3_get_sset_count(netdev, ETH_SS_STATS);
	drvinfo->testinfo_len = 0;
	drvinfo->eedump_len   = 0;
	drvinfo->regdump_len  = vmxnet3_get_regs_len(netdev);
}


static void
vmxnet3_get_strings(struct net_device *netdev, u32 stringset, u8 *buf)
{
	if (stringset == ETH_SS_STATS) {
		int i;

		for (i = 0; i < ARRAY_SIZE(vmxnet3_tq_dev_stats); i++) {
			memcpy(buf, vmxnet3_tq_dev_stats[i].desc,
			       ETH_GSTRING_LEN);
			buf += ETH_GSTRING_LEN;
		}
		for (i = 0; i < ARRAY_SIZE(vmxnet3_tq_driver_stats); i++) {
			memcpy(buf, vmxnet3_tq_driver_stats[i].desc,
			       ETH_GSTRING_LEN);
			buf += ETH_GSTRING_LEN;
		}
		for (i = 0; i < ARRAY_SIZE(vmxnet3_rq_dev_stats); i++) {
			memcpy(buf, vmxnet3_rq_dev_stats[i].desc,
			       ETH_GSTRING_LEN);
			buf += ETH_GSTRING_LEN;
		}
		for (i = 0; i < ARRAY_SIZE(vmxnet3_rq_driver_stats); i++) {
			memcpy(buf, vmxnet3_rq_driver_stats[i].desc,
			       ETH_GSTRING_LEN);
			buf += ETH_GSTRING_LEN;
		}
		for (i = 0; i < ARRAY_SIZE(vmxnet3_global_stats); i++) {
			memcpy(buf, vmxnet3_global_stats[i].desc,
				ETH_GSTRING_LEN);
			buf += ETH_GSTRING_LEN;
		}
	}
}

static u32
vmxnet3_get_flags(struct net_device *netdev) {
	return netdev->features;
}

static int
vmxnet3_set_flags(struct net_device *netdev, u32 data) {
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	u8 lro_requested = (data & ETH_FLAG_LRO) == 0 ? 0 : 1;
	u8 lro_present = (netdev->features & NETIF_F_LRO) == 0 ? 0 : 1;

	if (lro_requested ^ lro_present) {
		/* toggle the LRO feature*/
		netdev->features ^= NETIF_F_LRO;

		/* Update private LRO flag */
		adapter->lro = lro_requested;

		/* update harware LRO capability accordingly */
		if (lro_requested)
			adapter->shared->devRead.misc.uptFeatures &= UPT1_F_LRO;
		else
			adapter->shared->devRead.misc.uptFeatures &=
								~UPT1_F_LRO;
		VMXNET3_WRITE_BAR1_REG(adapter, VMXNET3_REG_CMD,
				       VMXNET3_CMD_UPDATE_FEATURE);
	}
	return 0;
}

static void
vmxnet3_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, u64  *buf)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	u8 *base;
	int i;

	VMXNET3_WRITE_BAR1_REG(adapter, VMXNET3_REG_CMD, VMXNET3_CMD_GET_STATS);

	/* this does assume each counter is 64-bit wide */

	base = (u8 *)&adapter->tqd_start->stats;
	for (i = 0; i < ARRAY_SIZE(vmxnet3_tq_dev_stats); i++)
		*buf++ = *(u64 *)(base + vmxnet3_tq_dev_stats[i].offset);

	base = (u8 *)&adapter->tx_queue.stats;
	for (i = 0; i < ARRAY_SIZE(vmxnet3_tq_driver_stats); i++)
		*buf++ = *(u64 *)(base + vmxnet3_tq_driver_stats[i].offset);

	base = (u8 *)&adapter->rqd_start->stats;
	for (i = 0; i < ARRAY_SIZE(vmxnet3_rq_dev_stats); i++)
		*buf++ = *(u64 *)(base + vmxnet3_rq_dev_stats[i].offset);

	base = (u8 *)&adapter->rx_queue.stats;
	for (i = 0; i < ARRAY_SIZE(vmxnet3_rq_driver_stats); i++)
		*buf++ = *(u64 *)(base + vmxnet3_rq_driver_stats[i].offset);

	base = (u8 *)adapter;
	for (i = 0; i < ARRAY_SIZE(vmxnet3_global_stats); i++)
		*buf++ = *(u64 *)(base + vmxnet3_global_stats[i].offset);
}


static void
vmxnet3_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *p)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	u32 *buf = p;

	memset(p, 0, vmxnet3_get_regs_len(netdev));

	regs->version = 1;

	/* Update vmxnet3_get_regs_len if we want to dump more registers */

	/* make each ring use multiple of 16 bytes */
	buf[0] = adapter->tx_queue.tx_ring.next2fill;
	buf[1] = adapter->tx_queue.tx_ring.next2comp;
	buf[2] = adapter->tx_queue.tx_ring.gen;
	buf[3] = 0;

	buf[4] = adapter->tx_queue.comp_ring.next2proc;
	buf[5] = adapter->tx_queue.comp_ring.gen;
	buf[6] = adapter->tx_queue.stopped;
	buf[7] = 0;

	buf[8] = adapter->rx_queue.rx_ring[0].next2fill;
	buf[9] = adapter->rx_queue.rx_ring[0].next2comp;
	buf[10] = adapter->rx_queue.rx_ring[0].gen;
	buf[11] = 0;

	buf[12] = adapter->rx_queue.rx_ring[1].next2fill;
	buf[13] = adapter->rx_queue.rx_ring[1].next2comp;
	buf[14] = adapter->rx_queue.rx_ring[1].gen;
	buf[15] = 0;

	buf[16] = adapter->rx_queue.comp_ring.next2proc;
	buf[17] = adapter->rx_queue.comp_ring.gen;
	buf[18] = 0;
	buf[19] = 0;
}


static void
vmxnet3_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_ARP | WAKE_MAGIC;
	wol->wolopts = adapter->wol;
}


static int
vmxnet3_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_PHY | WAKE_MCAST | WAKE_BCAST |
			    WAKE_MAGICSECURE)) {
		return -EOPNOTSUPP;
	}

	adapter->wol = wol->wolopts;

	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}


static int
vmxnet3_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	ecmd->supported = SUPPORTED_10000baseT_Full | SUPPORTED_1000baseT_Full |
			  SUPPORTED_TP;
	ecmd->advertising = ADVERTISED_TP;
	ecmd->port = PORT_TP;
	ecmd->transceiver = XCVR_INTERNAL;

	if (adapter->link_speed) {
		ecmd->speed = adapter->link_speed;
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}
	return 0;
}


static void
vmxnet3_get_ringparam(struct net_device *netdev,
		      struct ethtool_ringparam *param)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);

	param->rx_max_pending = VMXNET3_RX_RING_MAX_SIZE;
	param->tx_max_pending = VMXNET3_TX_RING_MAX_SIZE;
	param->rx_mini_max_pending = 0;
	param->rx_jumbo_max_pending = 0;

	param->rx_pending = adapter->rx_queue.rx_ring[0].size;
	param->tx_pending = adapter->tx_queue.tx_ring.size;
	param->rx_mini_pending = 0;
	param->rx_jumbo_pending = 0;
}


static int
vmxnet3_set_ringparam(struct net_device *netdev,
		      struct ethtool_ringparam *param)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	u32 new_tx_ring_size, new_rx_ring_size;
	u32 sz;
	int err = 0;

	if (param->tx_pending == 0 || param->tx_pending >
						VMXNET3_TX_RING_MAX_SIZE)
		return -EINVAL;

	if (param->rx_pending == 0 || param->rx_pending >
						VMXNET3_RX_RING_MAX_SIZE)
		return -EINVAL;


	/* round it up to a multiple of VMXNET3_RING_SIZE_ALIGN */
	new_tx_ring_size = (param->tx_pending + VMXNET3_RING_SIZE_MASK) &
							~VMXNET3_RING_SIZE_MASK;
	new_tx_ring_size = min_t(u32, new_tx_ring_size,
				 VMXNET3_TX_RING_MAX_SIZE);
	if (new_tx_ring_size > VMXNET3_TX_RING_MAX_SIZE || (new_tx_ring_size %
						VMXNET3_RING_SIZE_ALIGN) != 0)
		return -EINVAL;

	/* ring0 has to be a multiple of
	 * rx_buf_per_pkt * VMXNET3_RING_SIZE_ALIGN
	 */
	sz = adapter->rx_buf_per_pkt * VMXNET3_RING_SIZE_ALIGN;
	new_rx_ring_size = (param->rx_pending + sz - 1) / sz * sz;
	new_rx_ring_size = min_t(u32, new_rx_ring_size,
				 VMXNET3_RX_RING_MAX_SIZE / sz * sz);
	if (new_rx_ring_size > VMXNET3_RX_RING_MAX_SIZE || (new_rx_ring_size %
							   sz) != 0)
		return -EINVAL;

	if (new_tx_ring_size == adapter->tx_queue.tx_ring.size &&
			new_rx_ring_size == adapter->rx_queue.rx_ring[0].size) {
		return 0;
	}

	/*
	 * Reset_work may be in the middle of resetting the device, wait for its
	 * completion.
	 */
	while (test_and_set_bit(VMXNET3_STATE_BIT_RESETTING, &adapter->state))
		msleep(1);

	if (netif_running(netdev)) {
		vmxnet3_quiesce_dev(adapter);
		vmxnet3_reset_dev(adapter);

		/* recreate the rx queue and the tx queue based on the
		 * new sizes */
		vmxnet3_tq_destroy(&adapter->tx_queue, adapter);
		vmxnet3_rq_destroy(&adapter->rx_queue, adapter);

		err = vmxnet3_create_queues(adapter, new_tx_ring_size,
			new_rx_ring_size, VMXNET3_DEF_RX_RING_SIZE);
		if (err) {
			/* failed, most likely because of OOM, try default
			 * size */
			printk(KERN_ERR "%s: failed to apply new sizes, try the"
				" default ones\n", netdev->name);
			err = vmxnet3_create_queues(adapter,
						    VMXNET3_DEF_TX_RING_SIZE,
						    VMXNET3_DEF_RX_RING_SIZE,
						    VMXNET3_DEF_RX_RING_SIZE);
			if (err) {
				printk(KERN_ERR "%s: failed to create queues "
					"with default sizes. Closing it\n",
					netdev->name);
				goto out;
			}
		}

		err = vmxnet3_activate_dev(adapter);
		if (err)
			printk(KERN_ERR "%s: failed to re-activate, error %d."
				" Closing it\n", netdev->name, err);
	}

out:
	clear_bit(VMXNET3_STATE_BIT_RESETTING, &adapter->state);
	if (err)
		vmxnet3_force_close(adapter);

	return err;
}


static struct ethtool_ops vmxnet3_ethtool_ops = {
	.get_settings      = vmxnet3_get_settings,
	.get_drvinfo       = vmxnet3_get_drvinfo,
	.get_regs_len      = vmxnet3_get_regs_len,
	.get_regs          = vmxnet3_get_regs,
	.get_wol           = vmxnet3_get_wol,
	.set_wol           = vmxnet3_set_wol,
	.get_link          = ethtool_op_get_link,
	.get_rx_csum       = vmxnet3_get_rx_csum,
	.set_rx_csum       = vmxnet3_set_rx_csum,
	.get_tx_csum       = ethtool_op_get_tx_csum,
	.set_tx_csum       = ethtool_op_set_tx_hw_csum,
	.get_sg            = ethtool_op_get_sg,
	.set_sg            = ethtool_op_set_sg,
	.get_tso           = ethtool_op_get_tso,
	.set_tso           = ethtool_op_set_tso,
	.get_strings       = vmxnet3_get_strings,
	.get_flags	   = vmxnet3_get_flags,
	.set_flags	   = vmxnet3_set_flags,
	.get_sset_count	   = vmxnet3_get_sset_count,
	.get_ethtool_stats = vmxnet3_get_ethtool_stats,
	.get_ringparam     = vmxnet3_get_ringparam,
	.set_ringparam     = vmxnet3_set_ringparam,
};

void vmxnet3_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &vmxnet3_ethtool_ops);
}
