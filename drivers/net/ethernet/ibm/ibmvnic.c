/**************************************************************************/
/*                                                                        */
/*  IBM System i and System p Virtual NIC Device Driver                   */
/*  Copyright (C) 2014 IBM Corp.                                          */
/*  Santiago Leon (santi_leon@yahoo.com)                                  */
/*  Thomas Falcon (tlfalcon@linux.vnet.ibm.com)                           */
/*  John Allen (jallen@linux.vnet.ibm.com)                                */
/*                                                                        */
/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program.                                              */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with IBM i/p Series LPAR Linux. It utilizes the logical LAN    */
/* option of the RS/6000 Platform Architecture to interface with virtual  */
/* ethernet NICs that are presented to the partition by the hypervisor.   */
/*									   */
/* Messages are passed between the VNIC driver and the VNIC server using  */
/* Command/Response Queues (CRQs) and sub CRQs (sCRQs). CRQs are used to  */
/* issue and receive commands that initiate communication with the server */
/* on driver initialization. Sub CRQs (sCRQs) are similar to CRQs, but    */
/* are used by the driver to notify the server that a packet is           */
/* ready for transmission or that a buffer has been added to receive a    */
/* packet. Subsequently, sCRQs are used by the server to notify the       */
/* driver that a packet transmission has been completed or that a packet  */
/* has been received and placed in a waiting buffer.                      */
/*                                                                        */
/* In lieu of a more conventional "on-the-fly" DMA mapping strategy in    */
/* which skbs are DMA mapped and immediately unmapped when the transmit   */
/* or receive has been completed, the VNIC driver is required to use      */
/* "long term mapping". This entails that large, continuous DMA mapped    */
/* buffers are allocated on driver initialization and these buffers are   */
/* then continuously reused to pass skbs to and from the VNIC server.     */
/*                                                                        */
/**************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/completion.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/ethtool.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <net/net_namespace.h>
#include <asm/hvcall.h>
#include <linux/atomic.h>
#include <asm/vio.h>
#include <asm/iommu.h>
#include <linux/uaccess.h>
#include <asm/firmware.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/utsname.h>

#include "ibmvnic.h"

static const char ibmvnic_driver_name[] = "ibmvnic";
static const char ibmvnic_driver_string[] = "IBM System i/p Virtual NIC Driver";

MODULE_AUTHOR("Santiago Leon");
MODULE_DESCRIPTION("IBM System i/p Virtual NIC Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVNIC_DRIVER_VERSION);

static int ibmvnic_version = IBMVNIC_INITIAL_VERSION;
static int ibmvnic_remove(struct vio_dev *);
static void release_sub_crqs(struct ibmvnic_adapter *, bool);
static int ibmvnic_reset_crq(struct ibmvnic_adapter *);
static int ibmvnic_send_crq_init(struct ibmvnic_adapter *);
static int ibmvnic_reenable_crq_queue(struct ibmvnic_adapter *);
static int ibmvnic_send_crq(struct ibmvnic_adapter *, union ibmvnic_crq *);
static int send_subcrq(struct ibmvnic_adapter *adapter, u64 remote_handle,
		       union sub_crq *sub_crq);
static int send_subcrq_indirect(struct ibmvnic_adapter *, u64, u64, u64);
static irqreturn_t ibmvnic_interrupt_rx(int irq, void *instance);
static int enable_scrq_irq(struct ibmvnic_adapter *,
			   struct ibmvnic_sub_crq_queue *);
static int disable_scrq_irq(struct ibmvnic_adapter *,
			    struct ibmvnic_sub_crq_queue *);
static int pending_scrq(struct ibmvnic_adapter *,
			struct ibmvnic_sub_crq_queue *);
static union sub_crq *ibmvnic_next_scrq(struct ibmvnic_adapter *,
					struct ibmvnic_sub_crq_queue *);
static int ibmvnic_poll(struct napi_struct *napi, int data);
static void send_map_query(struct ibmvnic_adapter *adapter);
static int send_request_map(struct ibmvnic_adapter *, dma_addr_t, __be32, u8);
static int send_request_unmap(struct ibmvnic_adapter *, u8);
static int send_login(struct ibmvnic_adapter *adapter);
static void send_cap_queries(struct ibmvnic_adapter *adapter);
static int init_sub_crqs(struct ibmvnic_adapter *);
static int init_sub_crq_irqs(struct ibmvnic_adapter *adapter);
static int ibmvnic_init(struct ibmvnic_adapter *);
static int ibmvnic_reset_init(struct ibmvnic_adapter *);
static void release_crq_queue(struct ibmvnic_adapter *);
static int __ibmvnic_set_mac(struct net_device *netdev, struct sockaddr *p);
static int init_crq_queue(struct ibmvnic_adapter *adapter);

struct ibmvnic_stat {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define IBMVNIC_STAT_OFF(stat) (offsetof(struct ibmvnic_adapter, stats) + \
			     offsetof(struct ibmvnic_statistics, stat))
#define IBMVNIC_GET_STAT(a, off) (*((u64 *)(((unsigned long)(a)) + off)))

static const struct ibmvnic_stat ibmvnic_stats[] = {
	{"rx_packets", IBMVNIC_STAT_OFF(rx_packets)},
	{"rx_bytes", IBMVNIC_STAT_OFF(rx_bytes)},
	{"tx_packets", IBMVNIC_STAT_OFF(tx_packets)},
	{"tx_bytes", IBMVNIC_STAT_OFF(tx_bytes)},
	{"ucast_tx_packets", IBMVNIC_STAT_OFF(ucast_tx_packets)},
	{"ucast_rx_packets", IBMVNIC_STAT_OFF(ucast_rx_packets)},
	{"mcast_tx_packets", IBMVNIC_STAT_OFF(mcast_tx_packets)},
	{"mcast_rx_packets", IBMVNIC_STAT_OFF(mcast_rx_packets)},
	{"bcast_tx_packets", IBMVNIC_STAT_OFF(bcast_tx_packets)},
	{"bcast_rx_packets", IBMVNIC_STAT_OFF(bcast_rx_packets)},
	{"align_errors", IBMVNIC_STAT_OFF(align_errors)},
	{"fcs_errors", IBMVNIC_STAT_OFF(fcs_errors)},
	{"single_collision_frames", IBMVNIC_STAT_OFF(single_collision_frames)},
	{"multi_collision_frames", IBMVNIC_STAT_OFF(multi_collision_frames)},
	{"sqe_test_errors", IBMVNIC_STAT_OFF(sqe_test_errors)},
	{"deferred_tx", IBMVNIC_STAT_OFF(deferred_tx)},
	{"late_collisions", IBMVNIC_STAT_OFF(late_collisions)},
	{"excess_collisions", IBMVNIC_STAT_OFF(excess_collisions)},
	{"internal_mac_tx_errors", IBMVNIC_STAT_OFF(internal_mac_tx_errors)},
	{"carrier_sense", IBMVNIC_STAT_OFF(carrier_sense)},
	{"too_long_frames", IBMVNIC_STAT_OFF(too_long_frames)},
	{"internal_mac_rx_errors", IBMVNIC_STAT_OFF(internal_mac_rx_errors)},
};

static long h_reg_sub_crq(unsigned long unit_address, unsigned long token,
			  unsigned long length, unsigned long *number,
			  unsigned long *irq)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_REG_SUB_CRQ, retbuf, unit_address, token, length);
	*number = retbuf[0];
	*irq = retbuf[1];

	return rc;
}

static int alloc_long_term_buff(struct ibmvnic_adapter *adapter,
				struct ibmvnic_long_term_buff *ltb, int size)
{
	struct device *dev = &adapter->vdev->dev;
	int rc;

	ltb->size = size;
	ltb->buff = dma_alloc_coherent(dev, ltb->size, &ltb->addr,
				       GFP_KERNEL);

	if (!ltb->buff) {
		dev_err(dev, "Couldn't alloc long term buffer\n");
		return -ENOMEM;
	}
	ltb->map_id = adapter->map_id;
	adapter->map_id++;

	init_completion(&adapter->fw_done);
	rc = send_request_map(adapter, ltb->addr,
			      ltb->size, ltb->map_id);
	if (rc) {
		dma_free_coherent(dev, ltb->size, ltb->buff, ltb->addr);
		return rc;
	}
	wait_for_completion(&adapter->fw_done);

	if (adapter->fw_done_rc) {
		dev_err(dev, "Couldn't map long term buffer,rc = %d\n",
			adapter->fw_done_rc);
		dma_free_coherent(dev, ltb->size, ltb->buff, ltb->addr);
		return -1;
	}
	return 0;
}

static void free_long_term_buff(struct ibmvnic_adapter *adapter,
				struct ibmvnic_long_term_buff *ltb)
{
	struct device *dev = &adapter->vdev->dev;

	if (!ltb->buff)
		return;

	if (adapter->reset_reason != VNIC_RESET_FAILOVER &&
	    adapter->reset_reason != VNIC_RESET_MOBILITY)
		send_request_unmap(adapter, ltb->map_id);
	dma_free_coherent(dev, ltb->size, ltb->buff, ltb->addr);
}

static int reset_long_term_buff(struct ibmvnic_adapter *adapter,
				struct ibmvnic_long_term_buff *ltb)
{
	int rc;

	memset(ltb->buff, 0, ltb->size);

	init_completion(&adapter->fw_done);
	rc = send_request_map(adapter, ltb->addr, ltb->size, ltb->map_id);
	if (rc)
		return rc;
	wait_for_completion(&adapter->fw_done);

	if (adapter->fw_done_rc) {
		dev_info(&adapter->vdev->dev,
			 "Reset failed, attempting to free and reallocate buffer\n");
		free_long_term_buff(adapter, ltb);
		return alloc_long_term_buff(adapter, ltb, ltb->size);
	}
	return 0;
}

static void deactivate_rx_pools(struct ibmvnic_adapter *adapter)
{
	int i;

	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	     i++)
		adapter->rx_pool[i].active = 0;
}

static void replenish_rx_pool(struct ibmvnic_adapter *adapter,
			      struct ibmvnic_rx_pool *pool)
{
	int count = pool->size - atomic_read(&pool->available);
	struct device *dev = &adapter->vdev->dev;
	int buffers_added = 0;
	unsigned long lpar_rc;
	union sub_crq sub_crq;
	struct sk_buff *skb;
	unsigned int offset;
	dma_addr_t dma_addr;
	unsigned char *dst;
	u64 *handle_array;
	int shift = 0;
	int index;
	int i;

	if (!pool->active)
		return;

	handle_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
				      be32_to_cpu(adapter->login_rsp_buf->
				      off_rxadd_subcrqs));

	for (i = 0; i < count; ++i) {
		skb = alloc_skb(pool->buff_size, GFP_ATOMIC);
		if (!skb) {
			dev_err(dev, "Couldn't replenish rx buff\n");
			adapter->replenish_no_mem++;
			break;
		}

		index = pool->free_map[pool->next_free];

		if (pool->rx_buff[index].skb)
			dev_err(dev, "Inconsistent free_map!\n");

		/* Copy the skb to the long term mapped DMA buffer */
		offset = index * pool->buff_size;
		dst = pool->long_term_buff.buff + offset;
		memset(dst, 0, pool->buff_size);
		dma_addr = pool->long_term_buff.addr + offset;
		pool->rx_buff[index].data = dst;

		pool->free_map[pool->next_free] = IBMVNIC_INVALID_MAP;
		pool->rx_buff[index].dma = dma_addr;
		pool->rx_buff[index].skb = skb;
		pool->rx_buff[index].pool_index = pool->index;
		pool->rx_buff[index].size = pool->buff_size;

		memset(&sub_crq, 0, sizeof(sub_crq));
		sub_crq.rx_add.first = IBMVNIC_CRQ_CMD;
		sub_crq.rx_add.correlator =
		    cpu_to_be64((u64)&pool->rx_buff[index]);
		sub_crq.rx_add.ioba = cpu_to_be32(dma_addr);
		sub_crq.rx_add.map_id = pool->long_term_buff.map_id;

		/* The length field of the sCRQ is defined to be 24 bits so the
		 * buffer size needs to be left shifted by a byte before it is
		 * converted to big endian to prevent the last byte from being
		 * truncated.
		 */
#ifdef __LITTLE_ENDIAN__
		shift = 8;
#endif
		sub_crq.rx_add.len = cpu_to_be32(pool->buff_size << shift);

		lpar_rc = send_subcrq(adapter, handle_array[pool->index],
				      &sub_crq);
		if (lpar_rc != H_SUCCESS)
			goto failure;

		buffers_added++;
		adapter->replenish_add_buff_success++;
		pool->next_free = (pool->next_free + 1) % pool->size;
	}
	atomic_add(buffers_added, &pool->available);
	return;

failure:
	if (lpar_rc != H_PARAMETER && lpar_rc != H_CLOSED)
		dev_err_ratelimited(dev, "rx: replenish packet buffer failed\n");
	pool->free_map[pool->next_free] = index;
	pool->rx_buff[index].skb = NULL;

	dev_kfree_skb_any(skb);
	adapter->replenish_add_buff_failure++;
	atomic_add(buffers_added, &pool->available);

	if (lpar_rc == H_CLOSED || adapter->failover_pending) {
		/* Disable buffer pool replenishment and report carrier off if
		 * queue is closed or pending failover.
		 * Firmware guarantees that a signal will be sent to the
		 * driver, triggering a reset.
		 */
		deactivate_rx_pools(adapter);
		netif_carrier_off(adapter->netdev);
	}
}

static void replenish_pools(struct ibmvnic_adapter *adapter)
{
	int i;

	adapter->replenish_task_cycles++;
	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	     i++) {
		if (adapter->rx_pool[i].active)
			replenish_rx_pool(adapter, &adapter->rx_pool[i]);
	}
}

static void release_stats_buffers(struct ibmvnic_adapter *adapter)
{
	kfree(adapter->tx_stats_buffers);
	kfree(adapter->rx_stats_buffers);
	adapter->tx_stats_buffers = NULL;
	adapter->rx_stats_buffers = NULL;
}

static int init_stats_buffers(struct ibmvnic_adapter *adapter)
{
	adapter->tx_stats_buffers =
				kcalloc(IBMVNIC_MAX_QUEUES,
					sizeof(struct ibmvnic_tx_queue_stats),
					GFP_KERNEL);
	if (!adapter->tx_stats_buffers)
		return -ENOMEM;

	adapter->rx_stats_buffers =
				kcalloc(IBMVNIC_MAX_QUEUES,
					sizeof(struct ibmvnic_rx_queue_stats),
					GFP_KERNEL);
	if (!adapter->rx_stats_buffers)
		return -ENOMEM;

	return 0;
}

static void release_stats_token(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;

	if (!adapter->stats_token)
		return;

	dma_unmap_single(dev, adapter->stats_token,
			 sizeof(struct ibmvnic_statistics),
			 DMA_FROM_DEVICE);
	adapter->stats_token = 0;
}

static int init_stats_token(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	dma_addr_t stok;

	stok = dma_map_single(dev, &adapter->stats,
			      sizeof(struct ibmvnic_statistics),
			      DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, stok)) {
		dev_err(dev, "Couldn't map stats buffer\n");
		return -1;
	}

	adapter->stats_token = stok;
	netdev_dbg(adapter->netdev, "Stats token initialized (%llx)\n", stok);
	return 0;
}

static int reset_rx_pools(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_rx_pool *rx_pool;
	int rx_scrqs;
	int i, j, rc;
	u64 *size_array;

	size_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
		be32_to_cpu(adapter->login_rsp_buf->off_rxadd_buff_size));

	rx_scrqs = be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	for (i = 0; i < rx_scrqs; i++) {
		rx_pool = &adapter->rx_pool[i];

		netdev_dbg(adapter->netdev, "Re-setting rx_pool[%d]\n", i);

		if (rx_pool->buff_size != be64_to_cpu(size_array[i])) {
			free_long_term_buff(adapter, &rx_pool->long_term_buff);
			rx_pool->buff_size = be64_to_cpu(size_array[i]);
			rc = alloc_long_term_buff(adapter,
						  &rx_pool->long_term_buff,
						  rx_pool->size *
						  rx_pool->buff_size);
		} else {
			rc = reset_long_term_buff(adapter,
						  &rx_pool->long_term_buff);
		}

		if (rc)
			return rc;

		for (j = 0; j < rx_pool->size; j++)
			rx_pool->free_map[j] = j;

		memset(rx_pool->rx_buff, 0,
		       rx_pool->size * sizeof(struct ibmvnic_rx_buff));

		atomic_set(&rx_pool->available, 0);
		rx_pool->next_alloc = 0;
		rx_pool->next_free = 0;
		rx_pool->active = 1;
	}

	return 0;
}

static void release_rx_pools(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_rx_pool *rx_pool;
	int i, j;

	if (!adapter->rx_pool)
		return;

	for (i = 0; i < adapter->num_active_rx_pools; i++) {
		rx_pool = &adapter->rx_pool[i];

		netdev_dbg(adapter->netdev, "Releasing rx_pool[%d]\n", i);

		kfree(rx_pool->free_map);
		free_long_term_buff(adapter, &rx_pool->long_term_buff);

		if (!rx_pool->rx_buff)
			continue;

		for (j = 0; j < rx_pool->size; j++) {
			if (rx_pool->rx_buff[j].skb) {
				dev_kfree_skb_any(rx_pool->rx_buff[j].skb);
				rx_pool->rx_buff[j].skb = NULL;
			}
		}

		kfree(rx_pool->rx_buff);
	}

	kfree(adapter->rx_pool);
	adapter->rx_pool = NULL;
	adapter->num_active_rx_pools = 0;
}

static int init_rx_pools(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_rx_pool *rx_pool;
	int rxadd_subcrqs;
	u64 *size_array;
	int i, j;

	rxadd_subcrqs =
		be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	size_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
		be32_to_cpu(adapter->login_rsp_buf->off_rxadd_buff_size));

	adapter->rx_pool = kcalloc(rxadd_subcrqs,
				   sizeof(struct ibmvnic_rx_pool),
				   GFP_KERNEL);
	if (!adapter->rx_pool) {
		dev_err(dev, "Failed to allocate rx pools\n");
		return -1;
	}

	adapter->num_active_rx_pools = rxadd_subcrqs;

	for (i = 0; i < rxadd_subcrqs; i++) {
		rx_pool = &adapter->rx_pool[i];

		netdev_dbg(adapter->netdev,
			   "Initializing rx_pool[%d], %lld buffs, %lld bytes each\n",
			   i, adapter->req_rx_add_entries_per_subcrq,
			   be64_to_cpu(size_array[i]));

		rx_pool->size = adapter->req_rx_add_entries_per_subcrq;
		rx_pool->index = i;
		rx_pool->buff_size = be64_to_cpu(size_array[i]);
		rx_pool->active = 1;

		rx_pool->free_map = kcalloc(rx_pool->size, sizeof(int),
					    GFP_KERNEL);
		if (!rx_pool->free_map) {
			release_rx_pools(adapter);
			return -1;
		}

		rx_pool->rx_buff = kcalloc(rx_pool->size,
					   sizeof(struct ibmvnic_rx_buff),
					   GFP_KERNEL);
		if (!rx_pool->rx_buff) {
			dev_err(dev, "Couldn't alloc rx buffers\n");
			release_rx_pools(adapter);
			return -1;
		}

		if (alloc_long_term_buff(adapter, &rx_pool->long_term_buff,
					 rx_pool->size * rx_pool->buff_size)) {
			release_rx_pools(adapter);
			return -1;
		}

		for (j = 0; j < rx_pool->size; ++j)
			rx_pool->free_map[j] = j;

		atomic_set(&rx_pool->available, 0);
		rx_pool->next_alloc = 0;
		rx_pool->next_free = 0;
	}

	return 0;
}

static int reset_one_tx_pool(struct ibmvnic_adapter *adapter,
			     struct ibmvnic_tx_pool *tx_pool)
{
	int rc, i;

	rc = reset_long_term_buff(adapter, &tx_pool->long_term_buff);
	if (rc)
		return rc;

	memset(tx_pool->tx_buff, 0,
	       tx_pool->num_buffers *
	       sizeof(struct ibmvnic_tx_buff));

	for (i = 0; i < tx_pool->num_buffers; i++)
		tx_pool->free_map[i] = i;

	tx_pool->consumer_index = 0;
	tx_pool->producer_index = 0;

	return 0;
}

static int reset_tx_pools(struct ibmvnic_adapter *adapter)
{
	int tx_scrqs;
	int i, rc;

	tx_scrqs = be32_to_cpu(adapter->login_rsp_buf->num_txsubm_subcrqs);
	for (i = 0; i < tx_scrqs; i++) {
		rc = reset_one_tx_pool(adapter, &adapter->tso_pool[i]);
		if (rc)
			return rc;
		rc = reset_one_tx_pool(adapter, &adapter->tx_pool[i]);
		if (rc)
			return rc;
	}

	return 0;
}

static void release_vpd_data(struct ibmvnic_adapter *adapter)
{
	if (!adapter->vpd)
		return;

	kfree(adapter->vpd->buff);
	kfree(adapter->vpd);

	adapter->vpd = NULL;
}

static void release_one_tx_pool(struct ibmvnic_adapter *adapter,
				struct ibmvnic_tx_pool *tx_pool)
{
	kfree(tx_pool->tx_buff);
	kfree(tx_pool->free_map);
	free_long_term_buff(adapter, &tx_pool->long_term_buff);
}

static void release_tx_pools(struct ibmvnic_adapter *adapter)
{
	int i;

	if (!adapter->tx_pool)
		return;

	for (i = 0; i < adapter->num_active_tx_pools; i++) {
		release_one_tx_pool(adapter, &adapter->tx_pool[i]);
		release_one_tx_pool(adapter, &adapter->tso_pool[i]);
	}

	kfree(adapter->tx_pool);
	adapter->tx_pool = NULL;
	kfree(adapter->tso_pool);
	adapter->tso_pool = NULL;
	adapter->num_active_tx_pools = 0;
}

static int init_one_tx_pool(struct net_device *netdev,
			    struct ibmvnic_tx_pool *tx_pool,
			    int num_entries, int buf_size)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int i;

	tx_pool->tx_buff = kcalloc(num_entries,
				   sizeof(struct ibmvnic_tx_buff),
				   GFP_KERNEL);
	if (!tx_pool->tx_buff)
		return -1;

	if (alloc_long_term_buff(adapter, &tx_pool->long_term_buff,
				 num_entries * buf_size))
		return -1;

	tx_pool->free_map = kcalloc(num_entries, sizeof(int), GFP_KERNEL);
	if (!tx_pool->free_map)
		return -1;

	for (i = 0; i < num_entries; i++)
		tx_pool->free_map[i] = i;

	tx_pool->consumer_index = 0;
	tx_pool->producer_index = 0;
	tx_pool->num_buffers = num_entries;
	tx_pool->buf_size = buf_size;

	return 0;
}

static int init_tx_pools(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int tx_subcrqs;
	int i, rc;

	tx_subcrqs = be32_to_cpu(adapter->login_rsp_buf->num_txsubm_subcrqs);
	adapter->tx_pool = kcalloc(tx_subcrqs,
				   sizeof(struct ibmvnic_tx_pool), GFP_KERNEL);
	if (!adapter->tx_pool)
		return -1;

	adapter->tso_pool = kcalloc(tx_subcrqs,
				    sizeof(struct ibmvnic_tx_pool), GFP_KERNEL);
	if (!adapter->tso_pool)
		return -1;

	adapter->num_active_tx_pools = tx_subcrqs;

	for (i = 0; i < tx_subcrqs; i++) {
		rc = init_one_tx_pool(netdev, &adapter->tx_pool[i],
				      adapter->req_tx_entries_per_subcrq,
				      adapter->req_mtu + VLAN_HLEN);
		if (rc) {
			release_tx_pools(adapter);
			return rc;
		}

		rc = init_one_tx_pool(netdev, &adapter->tso_pool[i],
				      IBMVNIC_TSO_BUFS,
				      IBMVNIC_TSO_BUF_SZ);
		if (rc) {
			release_tx_pools(adapter);
			return rc;
		}
	}

	return 0;
}

static void ibmvnic_napi_enable(struct ibmvnic_adapter *adapter)
{
	int i;

	if (adapter->napi_enabled)
		return;

	for (i = 0; i < adapter->req_rx_queues; i++)
		napi_enable(&adapter->napi[i]);

	adapter->napi_enabled = true;
}

static void ibmvnic_napi_disable(struct ibmvnic_adapter *adapter)
{
	int i;

	if (!adapter->napi_enabled)
		return;

	for (i = 0; i < adapter->req_rx_queues; i++) {
		netdev_dbg(adapter->netdev, "Disabling napi[%d]\n", i);
		napi_disable(&adapter->napi[i]);
	}

	adapter->napi_enabled = false;
}

static int init_napi(struct ibmvnic_adapter *adapter)
{
	int i;

	adapter->napi = kcalloc(adapter->req_rx_queues,
				sizeof(struct napi_struct), GFP_KERNEL);
	if (!adapter->napi)
		return -ENOMEM;

	for (i = 0; i < adapter->req_rx_queues; i++) {
		netdev_dbg(adapter->netdev, "Adding napi[%d]\n", i);
		netif_napi_add(adapter->netdev, &adapter->napi[i],
			       ibmvnic_poll, NAPI_POLL_WEIGHT);
	}

	adapter->num_active_rx_napi = adapter->req_rx_queues;
	return 0;
}

static void release_napi(struct ibmvnic_adapter *adapter)
{
	int i;

	if (!adapter->napi)
		return;

	for (i = 0; i < adapter->num_active_rx_napi; i++) {
		if (&adapter->napi[i]) {
			netdev_dbg(adapter->netdev,
				   "Releasing napi[%d]\n", i);
			netif_napi_del(&adapter->napi[i]);
		}
	}

	kfree(adapter->napi);
	adapter->napi = NULL;
	adapter->num_active_rx_napi = 0;
	adapter->napi_enabled = false;
}

static int ibmvnic_login(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	unsigned long timeout = msecs_to_jiffies(30000);
	int retry_count = 0;
	bool retry;
	int rc;

	do {
		retry = false;
		if (retry_count > IBMVNIC_MAX_QUEUES) {
			netdev_warn(netdev, "Login attempts exceeded\n");
			return -1;
		}

		adapter->init_done_rc = 0;
		reinit_completion(&adapter->init_done);
		rc = send_login(adapter);
		if (rc) {
			netdev_warn(netdev, "Unable to login\n");
			return rc;
		}

		if (!wait_for_completion_timeout(&adapter->init_done,
						 timeout)) {
			netdev_warn(netdev, "Login timed out\n");
			return -1;
		}

		if (adapter->init_done_rc == PARTIALSUCCESS) {
			retry_count++;
			release_sub_crqs(adapter, 1);

			retry = true;
			netdev_dbg(netdev,
				   "Received partial success, retrying...\n");
			adapter->init_done_rc = 0;
			reinit_completion(&adapter->init_done);
			send_cap_queries(adapter);
			if (!wait_for_completion_timeout(&adapter->init_done,
							 timeout)) {
				netdev_warn(netdev,
					    "Capabilities query timed out\n");
				return -1;
			}

			rc = init_sub_crqs(adapter);
			if (rc) {
				netdev_warn(netdev,
					    "SCRQ initialization failed\n");
				return -1;
			}

			rc = init_sub_crq_irqs(adapter);
			if (rc) {
				netdev_warn(netdev,
					    "SCRQ irq initialization failed\n");
				return -1;
			}
		} else if (adapter->init_done_rc) {
			netdev_warn(netdev, "Adapter login failed\n");
			return -1;
		}
	} while (retry);

	/* handle pending MAC address changes after successful login */
	if (adapter->mac_change_pending) {
		__ibmvnic_set_mac(netdev, &adapter->desired.mac);
		adapter->mac_change_pending = false;
	}

	return 0;
}

static void release_login_buffer(struct ibmvnic_adapter *adapter)
{
	kfree(adapter->login_buf);
	adapter->login_buf = NULL;
}

static void release_login_rsp_buffer(struct ibmvnic_adapter *adapter)
{
	kfree(adapter->login_rsp_buf);
	adapter->login_rsp_buf = NULL;
}

static void release_resources(struct ibmvnic_adapter *adapter)
{
	release_vpd_data(adapter);

	release_tx_pools(adapter);
	release_rx_pools(adapter);

	release_napi(adapter);
	release_login_rsp_buffer(adapter);
}

static int set_link_state(struct ibmvnic_adapter *adapter, u8 link_state)
{
	struct net_device *netdev = adapter->netdev;
	unsigned long timeout = msecs_to_jiffies(30000);
	union ibmvnic_crq crq;
	bool resend;
	int rc;

	netdev_dbg(netdev, "setting link state %d\n", link_state);

	memset(&crq, 0, sizeof(crq));
	crq.logical_link_state.first = IBMVNIC_CRQ_CMD;
	crq.logical_link_state.cmd = LOGICAL_LINK_STATE;
	crq.logical_link_state.link_state = link_state;

	do {
		resend = false;

		reinit_completion(&adapter->init_done);
		rc = ibmvnic_send_crq(adapter, &crq);
		if (rc) {
			netdev_err(netdev, "Failed to set link state\n");
			return rc;
		}

		if (!wait_for_completion_timeout(&adapter->init_done,
						 timeout)) {
			netdev_err(netdev, "timeout setting link state\n");
			return -1;
		}

		if (adapter->init_done_rc == 1) {
			/* Partuial success, delay and re-send */
			mdelay(1000);
			resend = true;
		} else if (adapter->init_done_rc) {
			netdev_warn(netdev, "Unable to set link state, rc=%d\n",
				    adapter->init_done_rc);
			return adapter->init_done_rc;
		}
	} while (resend);

	return 0;
}

static int set_real_num_queues(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int rc;

	netdev_dbg(netdev, "Setting real tx/rx queues (%llx/%llx)\n",
		   adapter->req_tx_queues, adapter->req_rx_queues);

	rc = netif_set_real_num_tx_queues(netdev, adapter->req_tx_queues);
	if (rc) {
		netdev_err(netdev, "failed to set the number of tx queues\n");
		return rc;
	}

	rc = netif_set_real_num_rx_queues(netdev, adapter->req_rx_queues);
	if (rc)
		netdev_err(netdev, "failed to set the number of rx queues\n");

	return rc;
}

static int ibmvnic_get_vpd(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq crq;
	int len = 0;
	int rc;

	if (adapter->vpd->buff)
		len = adapter->vpd->len;

	init_completion(&adapter->fw_done);
	crq.get_vpd_size.first = IBMVNIC_CRQ_CMD;
	crq.get_vpd_size.cmd = GET_VPD_SIZE;
	rc = ibmvnic_send_crq(adapter, &crq);
	if (rc)
		return rc;
	wait_for_completion(&adapter->fw_done);

	if (!adapter->vpd->len)
		return -ENODATA;

	if (!adapter->vpd->buff)
		adapter->vpd->buff = kzalloc(adapter->vpd->len, GFP_KERNEL);
	else if (adapter->vpd->len != len)
		adapter->vpd->buff =
			krealloc(adapter->vpd->buff,
				 adapter->vpd->len, GFP_KERNEL);

	if (!adapter->vpd->buff) {
		dev_err(dev, "Could allocate VPD buffer\n");
		return -ENOMEM;
	}

	adapter->vpd->dma_addr =
		dma_map_single(dev, adapter->vpd->buff, adapter->vpd->len,
			       DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, adapter->vpd->dma_addr)) {
		dev_err(dev, "Could not map VPD buffer\n");
		kfree(adapter->vpd->buff);
		adapter->vpd->buff = NULL;
		return -ENOMEM;
	}

	reinit_completion(&adapter->fw_done);
	crq.get_vpd.first = IBMVNIC_CRQ_CMD;
	crq.get_vpd.cmd = GET_VPD;
	crq.get_vpd.ioba = cpu_to_be32(adapter->vpd->dma_addr);
	crq.get_vpd.len = cpu_to_be32((u32)adapter->vpd->len);
	rc = ibmvnic_send_crq(adapter, &crq);
	if (rc) {
		kfree(adapter->vpd->buff);
		adapter->vpd->buff = NULL;
		return rc;
	}
	wait_for_completion(&adapter->fw_done);

	return 0;
}

static int init_resources(struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int rc;

	rc = set_real_num_queues(netdev);
	if (rc)
		return rc;

	adapter->vpd = kzalloc(sizeof(*adapter->vpd), GFP_KERNEL);
	if (!adapter->vpd)
		return -ENOMEM;

	/* Vital Product Data (VPD) */
	rc = ibmvnic_get_vpd(adapter);
	if (rc) {
		netdev_err(netdev, "failed to initialize Vital Product Data (VPD)\n");
		return rc;
	}

	adapter->map_id = 1;

	rc = init_napi(adapter);
	if (rc)
		return rc;

	send_map_query(adapter);

	rc = init_rx_pools(netdev);
	if (rc)
		return rc;

	rc = init_tx_pools(netdev);
	return rc;
}

static int __ibmvnic_open(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	enum vnic_state prev_state = adapter->state;
	int i, rc;

	adapter->state = VNIC_OPENING;
	replenish_pools(adapter);
	ibmvnic_napi_enable(adapter);

	/* We're ready to receive frames, enable the sub-crq interrupts and
	 * set the logical link state to up
	 */
	for (i = 0; i < adapter->req_rx_queues; i++) {
		netdev_dbg(netdev, "Enabling rx_scrq[%d] irq\n", i);
		if (prev_state == VNIC_CLOSED)
			enable_irq(adapter->rx_scrq[i]->irq);
		enable_scrq_irq(adapter, adapter->rx_scrq[i]);
	}

	for (i = 0; i < adapter->req_tx_queues; i++) {
		netdev_dbg(netdev, "Enabling tx_scrq[%d] irq\n", i);
		if (prev_state == VNIC_CLOSED)
			enable_irq(adapter->tx_scrq[i]->irq);
		enable_scrq_irq(adapter, adapter->tx_scrq[i]);
	}

	rc = set_link_state(adapter, IBMVNIC_LOGICAL_LNK_UP);
	if (rc) {
		for (i = 0; i < adapter->req_rx_queues; i++)
			napi_disable(&adapter->napi[i]);
		release_resources(adapter);
		return rc;
	}

	netif_tx_start_all_queues(netdev);

	if (prev_state == VNIC_CLOSED) {
		for (i = 0; i < adapter->req_rx_queues; i++)
			napi_schedule(&adapter->napi[i]);
	}

	adapter->state = VNIC_OPEN;
	return rc;
}

static int ibmvnic_open(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int rc;

	/* If device failover is pending, just set device state and return.
	 * Device operation will be handled by reset routine.
	 */
	if (adapter->failover_pending) {
		adapter->state = VNIC_OPEN;
		return 0;
	}

	if (adapter->state != VNIC_CLOSED) {
		rc = ibmvnic_login(netdev);
		if (rc)
			return rc;

		rc = init_resources(adapter);
		if (rc) {
			netdev_err(netdev, "failed to initialize resources\n");
			release_resources(adapter);
			return rc;
		}
	}

	rc = __ibmvnic_open(netdev);
	netif_carrier_on(netdev);

	return rc;
}

static void clean_rx_pools(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_rx_pool *rx_pool;
	struct ibmvnic_rx_buff *rx_buff;
	u64 rx_entries;
	int rx_scrqs;
	int i, j;

	if (!adapter->rx_pool)
		return;

	rx_scrqs = adapter->num_active_rx_pools;
	rx_entries = adapter->req_rx_add_entries_per_subcrq;

	/* Free any remaining skbs in the rx buffer pools */
	for (i = 0; i < rx_scrqs; i++) {
		rx_pool = &adapter->rx_pool[i];
		if (!rx_pool || !rx_pool->rx_buff)
			continue;

		netdev_dbg(adapter->netdev, "Cleaning rx_pool[%d]\n", i);
		for (j = 0; j < rx_entries; j++) {
			rx_buff = &rx_pool->rx_buff[j];
			if (rx_buff && rx_buff->skb) {
				dev_kfree_skb_any(rx_buff->skb);
				rx_buff->skb = NULL;
			}
		}
	}
}

static void clean_one_tx_pool(struct ibmvnic_adapter *adapter,
			      struct ibmvnic_tx_pool *tx_pool)
{
	struct ibmvnic_tx_buff *tx_buff;
	u64 tx_entries;
	int i;

	if (!tx_pool || !tx_pool->tx_buff)
		return;

	tx_entries = tx_pool->num_buffers;

	for (i = 0; i < tx_entries; i++) {
		tx_buff = &tx_pool->tx_buff[i];
		if (tx_buff && tx_buff->skb) {
			dev_kfree_skb_any(tx_buff->skb);
			tx_buff->skb = NULL;
		}
	}
}

static void clean_tx_pools(struct ibmvnic_adapter *adapter)
{
	int tx_scrqs;
	int i;

	if (!adapter->tx_pool || !adapter->tso_pool)
		return;

	tx_scrqs = adapter->num_active_tx_pools;

	/* Free any remaining skbs in the tx buffer pools */
	for (i = 0; i < tx_scrqs; i++) {
		netdev_dbg(adapter->netdev, "Cleaning tx_pool[%d]\n", i);
		clean_one_tx_pool(adapter, &adapter->tx_pool[i]);
		clean_one_tx_pool(adapter, &adapter->tso_pool[i]);
	}
}

static void ibmvnic_disable_irqs(struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	if (adapter->tx_scrq) {
		for (i = 0; i < adapter->req_tx_queues; i++)
			if (adapter->tx_scrq[i]->irq) {
				netdev_dbg(netdev,
					   "Disabling tx_scrq[%d] irq\n", i);
				disable_scrq_irq(adapter, adapter->tx_scrq[i]);
				disable_irq(adapter->tx_scrq[i]->irq);
			}
	}

	if (adapter->rx_scrq) {
		for (i = 0; i < adapter->req_rx_queues; i++) {
			if (adapter->rx_scrq[i]->irq) {
				netdev_dbg(netdev,
					   "Disabling rx_scrq[%d] irq\n", i);
				disable_scrq_irq(adapter, adapter->rx_scrq[i]);
				disable_irq(adapter->rx_scrq[i]->irq);
			}
		}
	}
}

static void ibmvnic_cleanup(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	/* ensure that transmissions are stopped if called by do_reset */
	if (adapter->resetting)
		netif_tx_disable(netdev);
	else
		netif_tx_stop_all_queues(netdev);

	ibmvnic_napi_disable(adapter);
	ibmvnic_disable_irqs(adapter);

	clean_rx_pools(adapter);
	clean_tx_pools(adapter);
}

static int __ibmvnic_close(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	adapter->state = VNIC_CLOSING;
	rc = set_link_state(adapter, IBMVNIC_LOGICAL_LNK_DN);
	if (rc)
		return rc;
	adapter->state = VNIC_CLOSED;
	return 0;
}

static int ibmvnic_close(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int rc;

	/* If device failover is pending, just set device state and return.
	 * Device operation will be handled by reset routine.
	 */
	if (adapter->failover_pending) {
		adapter->state = VNIC_CLOSED;
		return 0;
	}

	rc = __ibmvnic_close(netdev);
	ibmvnic_cleanup(netdev);

	return rc;
}

/**
 * build_hdr_data - creates L2/L3/L4 header data buffer
 * @hdr_field - bitfield determining needed headers
 * @skb - socket buffer
 * @hdr_len - array of header lengths
 * @tot_len - total length of data
 *
 * Reads hdr_field to determine which headers are needed by firmware.
 * Builds a buffer containing these headers.  Saves individual header
 * lengths and total buffer length to be used to build descriptors.
 */
static int build_hdr_data(u8 hdr_field, struct sk_buff *skb,
			  int *hdr_len, u8 *hdr_data)
{
	int len = 0;
	u8 *hdr;

	if (skb_vlan_tagged(skb) && !skb_vlan_tag_present(skb))
		hdr_len[0] = sizeof(struct vlan_ethhdr);
	else
		hdr_len[0] = sizeof(struct ethhdr);

	if (skb->protocol == htons(ETH_P_IP)) {
		hdr_len[1] = ip_hdr(skb)->ihl * 4;
		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			hdr_len[2] = tcp_hdrlen(skb);
		else if (ip_hdr(skb)->protocol == IPPROTO_UDP)
			hdr_len[2] = sizeof(struct udphdr);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		hdr_len[1] = sizeof(struct ipv6hdr);
		if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
			hdr_len[2] = tcp_hdrlen(skb);
		else if (ipv6_hdr(skb)->nexthdr == IPPROTO_UDP)
			hdr_len[2] = sizeof(struct udphdr);
	} else if (skb->protocol == htons(ETH_P_ARP)) {
		hdr_len[1] = arp_hdr_len(skb->dev);
		hdr_len[2] = 0;
	}

	memset(hdr_data, 0, 120);
	if ((hdr_field >> 6) & 1) {
		hdr = skb_mac_header(skb);
		memcpy(hdr_data, hdr, hdr_len[0]);
		len += hdr_len[0];
	}

	if ((hdr_field >> 5) & 1) {
		hdr = skb_network_header(skb);
		memcpy(hdr_data + len, hdr, hdr_len[1]);
		len += hdr_len[1];
	}

	if ((hdr_field >> 4) & 1) {
		hdr = skb_transport_header(skb);
		memcpy(hdr_data + len, hdr, hdr_len[2]);
		len += hdr_len[2];
	}
	return len;
}

/**
 * create_hdr_descs - create header and header extension descriptors
 * @hdr_field - bitfield determining needed headers
 * @data - buffer containing header data
 * @len - length of data buffer
 * @hdr_len - array of individual header lengths
 * @scrq_arr - descriptor array
 *
 * Creates header and, if needed, header extension descriptors and
 * places them in a descriptor array, scrq_arr
 */

static int create_hdr_descs(u8 hdr_field, u8 *hdr_data, int len, int *hdr_len,
			    union sub_crq *scrq_arr)
{
	union sub_crq hdr_desc;
	int tmp_len = len;
	int num_descs = 0;
	u8 *data, *cur;
	int tmp;

	while (tmp_len > 0) {
		cur = hdr_data + len - tmp_len;

		memset(&hdr_desc, 0, sizeof(hdr_desc));
		if (cur != hdr_data) {
			data = hdr_desc.hdr_ext.data;
			tmp = tmp_len > 29 ? 29 : tmp_len;
			hdr_desc.hdr_ext.first = IBMVNIC_CRQ_CMD;
			hdr_desc.hdr_ext.type = IBMVNIC_HDR_EXT_DESC;
			hdr_desc.hdr_ext.len = tmp;
		} else {
			data = hdr_desc.hdr.data;
			tmp = tmp_len > 24 ? 24 : tmp_len;
			hdr_desc.hdr.first = IBMVNIC_CRQ_CMD;
			hdr_desc.hdr.type = IBMVNIC_HDR_DESC;
			hdr_desc.hdr.len = tmp;
			hdr_desc.hdr.l2_len = (u8)hdr_len[0];
			hdr_desc.hdr.l3_len = cpu_to_be16((u16)hdr_len[1]);
			hdr_desc.hdr.l4_len = (u8)hdr_len[2];
			hdr_desc.hdr.flag = hdr_field << 1;
		}
		memcpy(data, cur, tmp);
		tmp_len -= tmp;
		*scrq_arr = hdr_desc;
		scrq_arr++;
		num_descs++;
	}

	return num_descs;
}

/**
 * build_hdr_descs_arr - build a header descriptor array
 * @skb - socket buffer
 * @num_entries - number of descriptors to be sent
 * @subcrq - first TX descriptor
 * @hdr_field - bit field determining which headers will be sent
 *
 * This function will build a TX descriptor array with applicable
 * L2/L3/L4 packet header descriptors to be sent by send_subcrq_indirect.
 */

static void build_hdr_descs_arr(struct ibmvnic_tx_buff *txbuff,
				int *num_entries, u8 hdr_field)
{
	int hdr_len[3] = {0, 0, 0};
	int tot_len;
	u8 *hdr_data = txbuff->hdr_data;

	tot_len = build_hdr_data(hdr_field, txbuff->skb, hdr_len,
				 txbuff->hdr_data);
	*num_entries += create_hdr_descs(hdr_field, hdr_data, tot_len, hdr_len,
			 txbuff->indir_arr + 1);
}

static int ibmvnic_xmit_workarounds(struct sk_buff *skb,
				    struct net_device *netdev)
{
	/* For some backing devices, mishandling of small packets
	 * can result in a loss of connection or TX stall. Device
	 * architects recommend that no packet should be smaller
	 * than the minimum MTU value provided to the driver, so
	 * pad any packets to that length
	 */
	if (skb->len < netdev->min_mtu)
		return skb_put_padto(skb, netdev->min_mtu);

	return 0;
}

static netdev_tx_t ibmvnic_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int queue_num = skb_get_queue_mapping(skb);
	u8 *hdrs = (u8 *)&adapter->tx_rx_desc_req;
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_tx_buff *tx_buff = NULL;
	struct ibmvnic_sub_crq_queue *tx_scrq;
	struct ibmvnic_tx_pool *tx_pool;
	unsigned int tx_send_failed = 0;
	unsigned int tx_map_failed = 0;
	unsigned int tx_dropped = 0;
	unsigned int tx_packets = 0;
	unsigned int tx_bytes = 0;
	dma_addr_t data_dma_addr;
	struct netdev_queue *txq;
	unsigned long lpar_rc;
	union sub_crq tx_crq;
	unsigned int offset;
	int num_entries = 1;
	unsigned char *dst;
	u64 *handle_array;
	int index = 0;
	u8 proto = 0;
	netdev_tx_t ret = NETDEV_TX_OK;

	if (adapter->resetting) {
		if (!netif_subqueue_stopped(netdev, skb))
			netif_stop_subqueue(netdev, queue_num);
		dev_kfree_skb_any(skb);

		tx_send_failed++;
		tx_dropped++;
		ret = NETDEV_TX_OK;
		goto out;
	}

	if (ibmvnic_xmit_workarounds(skb, netdev)) {
		tx_dropped++;
		tx_send_failed++;
		ret = NETDEV_TX_OK;
		goto out;
	}
	if (skb_is_gso(skb))
		tx_pool = &adapter->tso_pool[queue_num];
	else
		tx_pool = &adapter->tx_pool[queue_num];

	tx_scrq = adapter->tx_scrq[queue_num];
	txq = netdev_get_tx_queue(netdev, skb_get_queue_mapping(skb));
	handle_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
		be32_to_cpu(adapter->login_rsp_buf->off_txsubm_subcrqs));

	index = tx_pool->free_map[tx_pool->consumer_index];

	if (index == IBMVNIC_INVALID_MAP) {
		dev_kfree_skb_any(skb);
		tx_send_failed++;
		tx_dropped++;
		ret = NETDEV_TX_OK;
		goto out;
	}

	tx_pool->free_map[tx_pool->consumer_index] = IBMVNIC_INVALID_MAP;

	offset = index * tx_pool->buf_size;
	dst = tx_pool->long_term_buff.buff + offset;
	memset(dst, 0, tx_pool->buf_size);
	data_dma_addr = tx_pool->long_term_buff.addr + offset;

	if (skb_shinfo(skb)->nr_frags) {
		int cur, i;

		/* Copy the head */
		skb_copy_from_linear_data(skb, dst, skb_headlen(skb));
		cur = skb_headlen(skb);

		/* Copy the frags */
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			memcpy(dst + cur,
			       page_address(skb_frag_page(frag)) +
			       frag->page_offset, skb_frag_size(frag));
			cur += skb_frag_size(frag);
		}
	} else {
		skb_copy_from_linear_data(skb, dst, skb->len);
	}

	tx_pool->consumer_index =
	    (tx_pool->consumer_index + 1) % tx_pool->num_buffers;

	tx_buff = &tx_pool->tx_buff[index];
	tx_buff->skb = skb;
	tx_buff->data_dma[0] = data_dma_addr;
	tx_buff->data_len[0] = skb->len;
	tx_buff->index = index;
	tx_buff->pool_index = queue_num;
	tx_buff->last_frag = true;

	memset(&tx_crq, 0, sizeof(tx_crq));
	tx_crq.v1.first = IBMVNIC_CRQ_CMD;
	tx_crq.v1.type = IBMVNIC_TX_DESC;
	tx_crq.v1.n_crq_elem = 1;
	tx_crq.v1.n_sge = 1;
	tx_crq.v1.flags1 = IBMVNIC_TX_COMP_NEEDED;

	if (skb_is_gso(skb))
		tx_crq.v1.correlator =
			cpu_to_be32(index | IBMVNIC_TSO_POOL_MASK);
	else
		tx_crq.v1.correlator = cpu_to_be32(index);
	tx_crq.v1.dma_reg = cpu_to_be16(tx_pool->long_term_buff.map_id);
	tx_crq.v1.sge_len = cpu_to_be32(skb->len);
	tx_crq.v1.ioba = cpu_to_be64(data_dma_addr);

	if (adapter->vlan_header_insertion && skb_vlan_tag_present(skb)) {
		tx_crq.v1.flags2 |= IBMVNIC_TX_VLAN_INSERT;
		tx_crq.v1.vlan_id = cpu_to_be16(skb->vlan_tci);
	}

	if (skb->protocol == htons(ETH_P_IP)) {
		tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_IPV4;
		proto = ip_hdr(skb)->protocol;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_IPV6;
		proto = ipv6_hdr(skb)->nexthdr;
	}

	if (proto == IPPROTO_TCP)
		tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_TCP;
	else if (proto == IPPROTO_UDP)
		tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_UDP;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tx_crq.v1.flags1 |= IBMVNIC_TX_CHKSUM_OFFLOAD;
		hdrs += 2;
	}
	if (skb_is_gso(skb)) {
		tx_crq.v1.flags1 |= IBMVNIC_TX_LSO;
		tx_crq.v1.mss = cpu_to_be16(skb_shinfo(skb)->gso_size);
		hdrs += 2;
	}
	/* determine if l2/3/4 headers are sent to firmware */
	if ((*hdrs >> 7) & 1) {
		build_hdr_descs_arr(tx_buff, &num_entries, *hdrs);
		tx_crq.v1.n_crq_elem = num_entries;
		tx_buff->num_entries = num_entries;
		tx_buff->indir_arr[0] = tx_crq;
		tx_buff->indir_dma = dma_map_single(dev, tx_buff->indir_arr,
						    sizeof(tx_buff->indir_arr),
						    DMA_TO_DEVICE);
		if (dma_mapping_error(dev, tx_buff->indir_dma)) {
			dev_kfree_skb_any(skb);
			tx_buff->skb = NULL;
			if (!firmware_has_feature(FW_FEATURE_CMO))
				dev_err(dev, "tx: unable to map descriptor array\n");
			tx_map_failed++;
			tx_dropped++;
			ret = NETDEV_TX_OK;
			goto tx_err_out;
		}
		lpar_rc = send_subcrq_indirect(adapter, handle_array[queue_num],
					       (u64)tx_buff->indir_dma,
					       (u64)num_entries);
		dma_unmap_single(dev, tx_buff->indir_dma,
				 sizeof(tx_buff->indir_arr), DMA_TO_DEVICE);
	} else {
		tx_buff->num_entries = num_entries;
		lpar_rc = send_subcrq(adapter, handle_array[queue_num],
				      &tx_crq);
	}
	if (lpar_rc != H_SUCCESS) {
		if (lpar_rc != H_CLOSED && lpar_rc != H_PARAMETER)
			dev_err_ratelimited(dev, "tx: send failed\n");
		dev_kfree_skb_any(skb);
		tx_buff->skb = NULL;

		if (lpar_rc == H_CLOSED || adapter->failover_pending) {
			/* Disable TX and report carrier off if queue is closed
			 * or pending failover.
			 * Firmware guarantees that a signal will be sent to the
			 * driver, triggering a reset or some other action.
			 */
			netif_tx_stop_all_queues(netdev);
			netif_carrier_off(netdev);
		}

		tx_send_failed++;
		tx_dropped++;
		ret = NETDEV_TX_OK;
		goto tx_err_out;
	}

	if (atomic_add_return(num_entries, &tx_scrq->used)
					>= adapter->req_tx_entries_per_subcrq) {
		netdev_dbg(netdev, "Stopping queue %d\n", queue_num);
		netif_stop_subqueue(netdev, queue_num);
	}

	tx_packets++;
	tx_bytes += skb->len;
	txq->trans_start = jiffies;
	ret = NETDEV_TX_OK;
	goto out;

tx_err_out:
	/* roll back consumer index and map array*/
	if (tx_pool->consumer_index == 0)
		tx_pool->consumer_index =
			tx_pool->num_buffers - 1;
	else
		tx_pool->consumer_index--;
	tx_pool->free_map[tx_pool->consumer_index] = index;
out:
	netdev->stats.tx_dropped += tx_dropped;
	netdev->stats.tx_bytes += tx_bytes;
	netdev->stats.tx_packets += tx_packets;
	adapter->tx_send_failed += tx_send_failed;
	adapter->tx_map_failed += tx_map_failed;
	adapter->tx_stats_buffers[queue_num].packets += tx_packets;
	adapter->tx_stats_buffers[queue_num].bytes += tx_bytes;
	adapter->tx_stats_buffers[queue_num].dropped_packets += tx_dropped;

	return ret;
}

static void ibmvnic_set_multi(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_capability.first = IBMVNIC_CRQ_CMD;
	crq.request_capability.cmd = REQUEST_CAPABILITY;

	if (netdev->flags & IFF_PROMISC) {
		if (!adapter->promisc_supported)
			return;
	} else {
		if (netdev->flags & IFF_ALLMULTI) {
			/* Accept all multicast */
			memset(&crq, 0, sizeof(crq));
			crq.multicast_ctrl.first = IBMVNIC_CRQ_CMD;
			crq.multicast_ctrl.cmd = MULTICAST_CTRL;
			crq.multicast_ctrl.flags = IBMVNIC_ENABLE_ALL;
			ibmvnic_send_crq(adapter, &crq);
		} else if (netdev_mc_empty(netdev)) {
			/* Reject all multicast */
			memset(&crq, 0, sizeof(crq));
			crq.multicast_ctrl.first = IBMVNIC_CRQ_CMD;
			crq.multicast_ctrl.cmd = MULTICAST_CTRL;
			crq.multicast_ctrl.flags = IBMVNIC_DISABLE_ALL;
			ibmvnic_send_crq(adapter, &crq);
		} else {
			/* Accept one or more multicast(s) */
			netdev_for_each_mc_addr(ha, netdev) {
				memset(&crq, 0, sizeof(crq));
				crq.multicast_ctrl.first = IBMVNIC_CRQ_CMD;
				crq.multicast_ctrl.cmd = MULTICAST_CTRL;
				crq.multicast_ctrl.flags = IBMVNIC_ENABLE_MC;
				ether_addr_copy(&crq.multicast_ctrl.mac_addr[0],
						ha->addr);
				ibmvnic_send_crq(adapter, &crq);
			}
		}
	}
}

static int __ibmvnic_set_mac(struct net_device *netdev, struct sockaddr *p)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	union ibmvnic_crq crq;
	int rc;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memset(&crq, 0, sizeof(crq));
	crq.change_mac_addr.first = IBMVNIC_CRQ_CMD;
	crq.change_mac_addr.cmd = CHANGE_MAC_ADDR;
	ether_addr_copy(&crq.change_mac_addr.mac_addr[0], addr->sa_data);

	init_completion(&adapter->fw_done);
	rc = ibmvnic_send_crq(adapter, &crq);
	if (rc)
		return rc;
	wait_for_completion(&adapter->fw_done);
	/* netdev->dev_addr is changed in handle_change_mac_rsp function */
	return adapter->fw_done_rc ? -EIO : 0;
}

static int ibmvnic_set_mac(struct net_device *netdev, void *p)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int rc;

	if (adapter->state == VNIC_PROBED) {
		memcpy(&adapter->desired.mac, addr, sizeof(struct sockaddr));
		adapter->mac_change_pending = true;
		return 0;
	}

	rc = __ibmvnic_set_mac(netdev, addr);

	return rc;
}

/**
 * do_reset returns zero if we are able to keep processing reset events, or
 * non-zero if we hit a fatal error and must halt.
 */
static int do_reset(struct ibmvnic_adapter *adapter,
		    struct ibmvnic_rwi *rwi, u32 reset_state)
{
	u64 old_num_rx_queues, old_num_tx_queues;
	u64 old_num_rx_slots, old_num_tx_slots;
	struct net_device *netdev = adapter->netdev;
	int i, rc;

	netdev_dbg(adapter->netdev, "Re-setting driver (%d)\n",
		   rwi->reset_reason);

	netif_carrier_off(netdev);
	adapter->reset_reason = rwi->reset_reason;

	old_num_rx_queues = adapter->req_rx_queues;
	old_num_tx_queues = adapter->req_tx_queues;
	old_num_rx_slots = adapter->req_rx_add_entries_per_subcrq;
	old_num_tx_slots = adapter->req_tx_entries_per_subcrq;

	ibmvnic_cleanup(netdev);

	if (reset_state == VNIC_OPEN &&
	    adapter->reset_reason != VNIC_RESET_MOBILITY &&
	    adapter->reset_reason != VNIC_RESET_FAILOVER) {
		rc = __ibmvnic_close(netdev);
		if (rc)
			return rc;
	}

	if (adapter->reset_reason == VNIC_RESET_CHANGE_PARAM ||
	    adapter->wait_for_reset) {
		release_resources(adapter);
		release_sub_crqs(adapter, 1);
		release_crq_queue(adapter);
	}

	if (adapter->reset_reason != VNIC_RESET_NON_FATAL) {
		/* remove the closed state so when we call open it appears
		 * we are coming from the probed state.
		 */
		adapter->state = VNIC_PROBED;

		if (adapter->wait_for_reset) {
			rc = init_crq_queue(adapter);
		} else if (adapter->reset_reason == VNIC_RESET_MOBILITY) {
			rc = ibmvnic_reenable_crq_queue(adapter);
			release_sub_crqs(adapter, 1);
		} else {
			rc = ibmvnic_reset_crq(adapter);
			if (!rc)
				rc = vio_enable_interrupts(adapter->vdev);
		}

		if (rc) {
			netdev_err(adapter->netdev,
				   "Couldn't initialize crq. rc=%d\n", rc);
			return rc;
		}

		rc = ibmvnic_reset_init(adapter);
		if (rc)
			return IBMVNIC_INIT_FAILED;

		/* If the adapter was in PROBE state prior to the reset,
		 * exit here.
		 */
		if (reset_state == VNIC_PROBED)
			return 0;

		rc = ibmvnic_login(netdev);
		if (rc) {
			adapter->state = reset_state;
			return rc;
		}

		if (adapter->reset_reason == VNIC_RESET_CHANGE_PARAM ||
		    adapter->wait_for_reset) {
			rc = init_resources(adapter);
			if (rc)
				return rc;
		} else if (adapter->req_rx_queues != old_num_rx_queues ||
			   adapter->req_tx_queues != old_num_tx_queues ||
			   adapter->req_rx_add_entries_per_subcrq !=
							old_num_rx_slots ||
			   adapter->req_tx_entries_per_subcrq !=
							old_num_tx_slots) {
			release_rx_pools(adapter);
			release_tx_pools(adapter);
			release_napi(adapter);
			release_vpd_data(adapter);

			rc = init_resources(adapter);
			if (rc)
				return rc;

		} else {
			rc = reset_tx_pools(adapter);
			if (rc)
				return rc;

			rc = reset_rx_pools(adapter);
			if (rc)
				return rc;
		}
		ibmvnic_disable_irqs(adapter);
	}
	adapter->state = VNIC_CLOSED;

	if (reset_state == VNIC_CLOSED)
		return 0;

	rc = __ibmvnic_open(netdev);
	if (rc) {
		if (list_empty(&adapter->rwi_list))
			adapter->state = VNIC_CLOSED;
		else
			adapter->state = reset_state;

		return 0;
	}

	/* refresh device's multicast list */
	ibmvnic_set_multi(netdev);

	/* kick napi */
	for (i = 0; i < adapter->req_rx_queues; i++)
		napi_schedule(&adapter->napi[i]);

	if (adapter->reset_reason != VNIC_RESET_FAILOVER &&
	    adapter->reset_reason != VNIC_RESET_CHANGE_PARAM)
		call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, netdev);

	netif_carrier_on(netdev);

	return 0;
}

static int do_hard_reset(struct ibmvnic_adapter *adapter,
			 struct ibmvnic_rwi *rwi, u32 reset_state)
{
	struct net_device *netdev = adapter->netdev;
	int rc;

	netdev_dbg(adapter->netdev, "Hard resetting driver (%d)\n",
		   rwi->reset_reason);

	netif_carrier_off(netdev);
	adapter->reset_reason = rwi->reset_reason;

	ibmvnic_cleanup(netdev);
	release_resources(adapter);
	release_sub_crqs(adapter, 0);
	release_crq_queue(adapter);

	/* remove the closed state so when we call open it appears
	 * we are coming from the probed state.
	 */
	adapter->state = VNIC_PROBED;

	reinit_completion(&adapter->init_done);
	rc = init_crq_queue(adapter);
	if (rc) {
		netdev_err(adapter->netdev,
			   "Couldn't initialize crq. rc=%d\n", rc);
		return rc;
	}

	rc = ibmvnic_init(adapter);
	if (rc)
		return rc;

	/* If the adapter was in PROBE state prior to the reset,
	 * exit here.
	 */
	if (reset_state == VNIC_PROBED)
		return 0;

	rc = ibmvnic_login(netdev);
	if (rc) {
		adapter->state = VNIC_PROBED;
		return 0;
	}

	rc = init_resources(adapter);
	if (rc)
		return rc;

	ibmvnic_disable_irqs(adapter);
	adapter->state = VNIC_CLOSED;

	if (reset_state == VNIC_CLOSED)
		return 0;

	rc = __ibmvnic_open(netdev);
	if (rc) {
		if (list_empty(&adapter->rwi_list))
			adapter->state = VNIC_CLOSED;
		else
			adapter->state = reset_state;

		return 0;
	}

	netif_carrier_on(netdev);

	return 0;
}

static struct ibmvnic_rwi *get_next_rwi(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_rwi *rwi;
	unsigned long flags;

	spin_lock_irqsave(&adapter->rwi_lock, flags);

	if (!list_empty(&adapter->rwi_list)) {
		rwi = list_first_entry(&adapter->rwi_list, struct ibmvnic_rwi,
				       list);
		list_del(&rwi->list);
	} else {
		rwi = NULL;
	}

	spin_unlock_irqrestore(&adapter->rwi_lock, flags);
	return rwi;
}

static void free_all_rwi(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_rwi *rwi;

	rwi = get_next_rwi(adapter);
	while (rwi) {
		kfree(rwi);
		rwi = get_next_rwi(adapter);
	}
}

static void __ibmvnic_reset(struct work_struct *work)
{
	struct ibmvnic_rwi *rwi;
	struct ibmvnic_adapter *adapter;
	struct net_device *netdev;
	bool we_lock_rtnl = false;
	u32 reset_state;
	int rc = 0;

	adapter = container_of(work, struct ibmvnic_adapter, ibmvnic_reset);
	netdev = adapter->netdev;

	/* netif_set_real_num_xx_queues needs to take rtnl lock here
	 * unless wait_for_reset is set, in which case the rtnl lock
	 * has already been taken before initializing the reset
	 */
	if (!adapter->wait_for_reset) {
		rtnl_lock();
		we_lock_rtnl = true;
	}
	reset_state = adapter->state;

	rwi = get_next_rwi(adapter);
	while (rwi) {
		if (adapter->state == VNIC_REMOVING ||
		    adapter->state == VNIC_REMOVED) {
			kfree(rwi);
			rc = EBUSY;
			break;
		}

		if (adapter->force_reset_recovery) {
			adapter->force_reset_recovery = false;
			rc = do_hard_reset(adapter, rwi, reset_state);
		} else {
			rc = do_reset(adapter, rwi, reset_state);
		}
		kfree(rwi);
		if (rc && rc != IBMVNIC_INIT_FAILED &&
		    !adapter->force_reset_recovery)
			break;

		rwi = get_next_rwi(adapter);
	}

	if (adapter->wait_for_reset) {
		adapter->wait_for_reset = false;
		adapter->reset_done_rc = rc;
		complete(&adapter->reset_done);
	}

	if (rc) {
		netdev_dbg(adapter->netdev, "Reset failed\n");
		free_all_rwi(adapter);
	}

	adapter->resetting = false;
	if (we_lock_rtnl)
		rtnl_unlock();
}

static int ibmvnic_reset(struct ibmvnic_adapter *adapter,
			 enum ibmvnic_reset_reason reason)
{
	struct list_head *entry, *tmp_entry;
	struct ibmvnic_rwi *rwi, *tmp;
	struct net_device *netdev = adapter->netdev;
	unsigned long flags;
	int ret;

	if (adapter->state == VNIC_REMOVING ||
	    adapter->state == VNIC_REMOVED ||
	    adapter->failover_pending) {
		ret = EBUSY;
		netdev_dbg(netdev, "Adapter removing or pending failover, skipping reset\n");
		goto err;
	}

	if (adapter->state == VNIC_PROBING) {
		netdev_warn(netdev, "Adapter reset during probe\n");
		ret = adapter->init_done_rc = EAGAIN;
		goto err;
	}

	spin_lock_irqsave(&adapter->rwi_lock, flags);

	list_for_each(entry, &adapter->rwi_list) {
		tmp = list_entry(entry, struct ibmvnic_rwi, list);
		if (tmp->reset_reason == reason) {
			netdev_dbg(netdev, "Skipping matching reset\n");
			spin_unlock_irqrestore(&adapter->rwi_lock, flags);
			ret = EBUSY;
			goto err;
		}
	}

	rwi = kzalloc(sizeof(*rwi), GFP_ATOMIC);
	if (!rwi) {
		spin_unlock_irqrestore(&adapter->rwi_lock, flags);
		ibmvnic_close(netdev);
		ret = ENOMEM;
		goto err;
	}
	/* if we just received a transport event,
	 * flush reset queue and process this reset
	 */
	if (adapter->force_reset_recovery && !list_empty(&adapter->rwi_list)) {
		list_for_each_safe(entry, tmp_entry, &adapter->rwi_list)
			list_del(entry);
	}
	rwi->reset_reason = reason;
	list_add_tail(&rwi->list, &adapter->rwi_list);
	spin_unlock_irqrestore(&adapter->rwi_lock, flags);
	adapter->resetting = true;
	netdev_dbg(adapter->netdev, "Scheduling reset (reason %d)\n", reason);
	schedule_work(&adapter->ibmvnic_reset);

	return 0;
err:
	if (adapter->wait_for_reset)
		adapter->wait_for_reset = false;
	return -ret;
}

static void ibmvnic_tx_timeout(struct net_device *dev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);

	ibmvnic_reset(adapter, VNIC_RESET_TIMEOUT);
}

static void remove_buff_from_pool(struct ibmvnic_adapter *adapter,
				  struct ibmvnic_rx_buff *rx_buff)
{
	struct ibmvnic_rx_pool *pool = &adapter->rx_pool[rx_buff->pool_index];

	rx_buff->skb = NULL;

	pool->free_map[pool->next_alloc] = (int)(rx_buff - pool->rx_buff);
	pool->next_alloc = (pool->next_alloc + 1) % pool->size;

	atomic_dec(&pool->available);
}

static int ibmvnic_poll(struct napi_struct *napi, int budget)
{
	struct net_device *netdev = napi->dev;
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int scrq_num = (int)(napi - adapter->napi);
	int frames_processed = 0;

restart_poll:
	while (frames_processed < budget) {
		struct sk_buff *skb;
		struct ibmvnic_rx_buff *rx_buff;
		union sub_crq *next;
		u32 length;
		u16 offset;
		u8 flags = 0;

		if (unlikely(adapter->resetting &&
			     adapter->reset_reason != VNIC_RESET_NON_FATAL)) {
			enable_scrq_irq(adapter, adapter->rx_scrq[scrq_num]);
			napi_complete_done(napi, frames_processed);
			return frames_processed;
		}

		if (!pending_scrq(adapter, adapter->rx_scrq[scrq_num]))
			break;
		next = ibmvnic_next_scrq(adapter, adapter->rx_scrq[scrq_num]);
		rx_buff =
		    (struct ibmvnic_rx_buff *)be64_to_cpu(next->
							  rx_comp.correlator);
		/* do error checking */
		if (next->rx_comp.rc) {
			netdev_dbg(netdev, "rx buffer returned with rc %x\n",
				   be16_to_cpu(next->rx_comp.rc));
			/* free the entry */
			next->rx_comp.first = 0;
			dev_kfree_skb_any(rx_buff->skb);
			remove_buff_from_pool(adapter, rx_buff);
			continue;
		} else if (!rx_buff->skb) {
			/* free the entry */
			next->rx_comp.first = 0;
			remove_buff_from_pool(adapter, rx_buff);
			continue;
		}

		length = be32_to_cpu(next->rx_comp.len);
		offset = be16_to_cpu(next->rx_comp.off_frame_data);
		flags = next->rx_comp.flags;
		skb = rx_buff->skb;
		skb_copy_to_linear_data(skb, rx_buff->data + offset,
					length);

		/* VLAN Header has been stripped by the system firmware and
		 * needs to be inserted by the driver
		 */
		if (adapter->rx_vlan_header_insertion &&
		    (flags & IBMVNIC_VLAN_STRIPPED))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       ntohs(next->rx_comp.vlan_tci));

		/* free the entry */
		next->rx_comp.first = 0;
		remove_buff_from_pool(adapter, rx_buff);

		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, netdev);
		skb_record_rx_queue(skb, scrq_num);

		if (flags & IBMVNIC_IP_CHKSUM_GOOD &&
		    flags & IBMVNIC_TCP_UDP_CHKSUM_GOOD) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

		length = skb->len;
		napi_gro_receive(napi, skb); /* send it up */
		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += length;
		adapter->rx_stats_buffers[scrq_num].packets++;
		adapter->rx_stats_buffers[scrq_num].bytes += length;
		frames_processed++;
	}

	if (adapter->state != VNIC_CLOSING)
		replenish_rx_pool(adapter, &adapter->rx_pool[scrq_num]);

	if (frames_processed < budget) {
		enable_scrq_irq(adapter, adapter->rx_scrq[scrq_num]);
		napi_complete_done(napi, frames_processed);
		if (pending_scrq(adapter, adapter->rx_scrq[scrq_num]) &&
		    napi_reschedule(napi)) {
			disable_scrq_irq(adapter, adapter->rx_scrq[scrq_num]);
			goto restart_poll;
		}
	}
	return frames_processed;
}

static int wait_for_reset(struct ibmvnic_adapter *adapter)
{
	int rc, ret;

	adapter->fallback.mtu = adapter->req_mtu;
	adapter->fallback.rx_queues = adapter->req_rx_queues;
	adapter->fallback.tx_queues = adapter->req_tx_queues;
	adapter->fallback.rx_entries = adapter->req_rx_add_entries_per_subcrq;
	adapter->fallback.tx_entries = adapter->req_tx_entries_per_subcrq;

	init_completion(&adapter->reset_done);
	adapter->wait_for_reset = true;
	rc = ibmvnic_reset(adapter, VNIC_RESET_CHANGE_PARAM);
	if (rc)
		return rc;
	wait_for_completion(&adapter->reset_done);

	ret = 0;
	if (adapter->reset_done_rc) {
		ret = -EIO;
		adapter->desired.mtu = adapter->fallback.mtu;
		adapter->desired.rx_queues = adapter->fallback.rx_queues;
		adapter->desired.tx_queues = adapter->fallback.tx_queues;
		adapter->desired.rx_entries = adapter->fallback.rx_entries;
		adapter->desired.tx_entries = adapter->fallback.tx_entries;

		init_completion(&adapter->reset_done);
		adapter->wait_for_reset = true;
		rc = ibmvnic_reset(adapter, VNIC_RESET_CHANGE_PARAM);
		if (rc)
			return ret;
		wait_for_completion(&adapter->reset_done);
	}
	adapter->wait_for_reset = false;

	return ret;
}

static int ibmvnic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	adapter->desired.mtu = new_mtu + ETH_HLEN;

	return wait_for_reset(adapter);
}

static netdev_features_t ibmvnic_features_check(struct sk_buff *skb,
						struct net_device *dev,
						netdev_features_t features)
{
	/* Some backing hardware adapters can not
	 * handle packets with a MSS less than 224
	 * or with only one segment.
	 */
	if (skb_is_gso(skb)) {
		if (skb_shinfo(skb)->gso_size < 224 ||
		    skb_shinfo(skb)->gso_segs == 1)
			features &= ~NETIF_F_GSO_MASK;
	}

	return features;
}

static const struct net_device_ops ibmvnic_netdev_ops = {
	.ndo_open		= ibmvnic_open,
	.ndo_stop		= ibmvnic_close,
	.ndo_start_xmit		= ibmvnic_xmit,
	.ndo_set_rx_mode	= ibmvnic_set_multi,
	.ndo_set_mac_address	= ibmvnic_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= ibmvnic_tx_timeout,
	.ndo_change_mtu		= ibmvnic_change_mtu,
	.ndo_features_check     = ibmvnic_features_check,
};

/* ethtool functions */

static int ibmvnic_get_link_ksettings(struct net_device *netdev,
				      struct ethtool_link_ksettings *cmd)
{
	u32 supported, advertising;

	supported = (SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg |
			  SUPPORTED_FIBRE);
	advertising = (ADVERTISED_1000baseT_Full | ADVERTISED_Autoneg |
			    ADVERTISED_FIBRE);
	cmd->base.speed = SPEED_1000;
	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.port = PORT_FIBRE;
	cmd->base.phy_address = 0;
	cmd->base.autoneg = AUTONEG_ENABLE;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static void ibmvnic_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *info)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	strlcpy(info->driver, ibmvnic_driver_name, sizeof(info->driver));
	strlcpy(info->version, IBMVNIC_DRIVER_VERSION, sizeof(info->version));
	strlcpy(info->fw_version, adapter->fw_version,
		sizeof(info->fw_version));
}

static u32 ibmvnic_get_msglevel(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void ibmvnic_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static u32 ibmvnic_get_link(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	/* Don't need to send a query because we request a logical link up at
	 * init and then we wait for link state indications
	 */
	return adapter->logical_link_state;
}

static void ibmvnic_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = adapter->max_rx_add_entries_per_subcrq;
	ring->tx_max_pending = adapter->max_tx_entries_per_subcrq;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = adapter->req_rx_add_entries_per_subcrq;
	ring->tx_pending = adapter->req_tx_entries_per_subcrq;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int ibmvnic_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	if (ring->rx_pending > adapter->max_rx_add_entries_per_subcrq  ||
	    ring->tx_pending > adapter->max_tx_entries_per_subcrq) {
		netdev_err(netdev, "Invalid request.\n");
		netdev_err(netdev, "Max tx buffers = %llu\n",
			   adapter->max_rx_add_entries_per_subcrq);
		netdev_err(netdev, "Max rx buffers = %llu\n",
			   adapter->max_tx_entries_per_subcrq);
		return -EINVAL;
	}

	adapter->desired.rx_entries = ring->rx_pending;
	adapter->desired.tx_entries = ring->tx_pending;

	return wait_for_reset(adapter);
}

static void ibmvnic_get_channels(struct net_device *netdev,
				 struct ethtool_channels *channels)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	channels->max_rx = adapter->max_rx_queues;
	channels->max_tx = adapter->max_tx_queues;
	channels->max_other = 0;
	channels->max_combined = 0;
	channels->rx_count = adapter->req_rx_queues;
	channels->tx_count = adapter->req_tx_queues;
	channels->other_count = 0;
	channels->combined_count = 0;
}

static int ibmvnic_set_channels(struct net_device *netdev,
				struct ethtool_channels *channels)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	adapter->desired.rx_queues = channels->rx_count;
	adapter->desired.tx_queues = channels->tx_count;

	return wait_for_reset(adapter);
}

static void ibmvnic_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ibmvnic_stats); i++, data += ETH_GSTRING_LEN)
		memcpy(data, ibmvnic_stats[i].name, ETH_GSTRING_LEN);

	for (i = 0; i < adapter->req_tx_queues; i++) {
		snprintf(data, ETH_GSTRING_LEN, "tx%d_packets", i);
		data += ETH_GSTRING_LEN;

		snprintf(data, ETH_GSTRING_LEN, "tx%d_bytes", i);
		data += ETH_GSTRING_LEN;

		snprintf(data, ETH_GSTRING_LEN, "tx%d_dropped_packets", i);
		data += ETH_GSTRING_LEN;
	}

	for (i = 0; i < adapter->req_rx_queues; i++) {
		snprintf(data, ETH_GSTRING_LEN, "rx%d_packets", i);
		data += ETH_GSTRING_LEN;

		snprintf(data, ETH_GSTRING_LEN, "rx%d_bytes", i);
		data += ETH_GSTRING_LEN;

		snprintf(data, ETH_GSTRING_LEN, "rx%d_interrupts", i);
		data += ETH_GSTRING_LEN;
	}
}

static int ibmvnic_get_sset_count(struct net_device *dev, int sset)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ibmvnic_stats) +
		       adapter->req_tx_queues * NUM_TX_STATS +
		       adapter->req_rx_queues * NUM_RX_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static void ibmvnic_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);
	union ibmvnic_crq crq;
	int i, j;
	int rc;

	memset(&crq, 0, sizeof(crq));
	crq.request_statistics.first = IBMVNIC_CRQ_CMD;
	crq.request_statistics.cmd = REQUEST_STATISTICS;
	crq.request_statistics.ioba = cpu_to_be32(adapter->stats_token);
	crq.request_statistics.len =
	    cpu_to_be32(sizeof(struct ibmvnic_statistics));

	/* Wait for data to be written */
	init_completion(&adapter->stats_done);
	rc = ibmvnic_send_crq(adapter, &crq);
	if (rc)
		return;
	wait_for_completion(&adapter->stats_done);

	for (i = 0; i < ARRAY_SIZE(ibmvnic_stats); i++)
		data[i] = be64_to_cpu(IBMVNIC_GET_STAT(adapter,
						ibmvnic_stats[i].offset));

	for (j = 0; j < adapter->req_tx_queues; j++) {
		data[i] = adapter->tx_stats_buffers[j].packets;
		i++;
		data[i] = adapter->tx_stats_buffers[j].bytes;
		i++;
		data[i] = adapter->tx_stats_buffers[j].dropped_packets;
		i++;
	}

	for (j = 0; j < adapter->req_rx_queues; j++) {
		data[i] = adapter->rx_stats_buffers[j].packets;
		i++;
		data[i] = adapter->rx_stats_buffers[j].bytes;
		i++;
		data[i] = adapter->rx_stats_buffers[j].interrupts;
		i++;
	}
}

static const struct ethtool_ops ibmvnic_ethtool_ops = {
	.get_drvinfo		= ibmvnic_get_drvinfo,
	.get_msglevel		= ibmvnic_get_msglevel,
	.set_msglevel		= ibmvnic_set_msglevel,
	.get_link		= ibmvnic_get_link,
	.get_ringparam		= ibmvnic_get_ringparam,
	.set_ringparam		= ibmvnic_set_ringparam,
	.get_channels		= ibmvnic_get_channels,
	.set_channels		= ibmvnic_set_channels,
	.get_strings            = ibmvnic_get_strings,
	.get_sset_count         = ibmvnic_get_sset_count,
	.get_ethtool_stats	= ibmvnic_get_ethtool_stats,
	.get_link_ksettings	= ibmvnic_get_link_ksettings,
};

/* Routines for managing CRQs/sCRQs  */

static int reset_one_sub_crq_queue(struct ibmvnic_adapter *adapter,
				   struct ibmvnic_sub_crq_queue *scrq)
{
	int rc;

	if (scrq->irq) {
		free_irq(scrq->irq, scrq);
		irq_dispose_mapping(scrq->irq);
		scrq->irq = 0;
	}

	memset(scrq->msgs, 0, 4 * PAGE_SIZE);
	atomic_set(&scrq->used, 0);
	scrq->cur = 0;

	rc = h_reg_sub_crq(adapter->vdev->unit_address, scrq->msg_token,
			   4 * PAGE_SIZE, &scrq->crq_num, &scrq->hw_irq);
	return rc;
}

static int reset_sub_crq_queues(struct ibmvnic_adapter *adapter)
{
	int i, rc;

	for (i = 0; i < adapter->req_tx_queues; i++) {
		netdev_dbg(adapter->netdev, "Re-setting tx_scrq[%d]\n", i);
		rc = reset_one_sub_crq_queue(adapter, adapter->tx_scrq[i]);
		if (rc)
			return rc;
	}

	for (i = 0; i < adapter->req_rx_queues; i++) {
		netdev_dbg(adapter->netdev, "Re-setting rx_scrq[%d]\n", i);
		rc = reset_one_sub_crq_queue(adapter, adapter->rx_scrq[i]);
		if (rc)
			return rc;
	}

	return rc;
}

static void release_sub_crq_queue(struct ibmvnic_adapter *adapter,
				  struct ibmvnic_sub_crq_queue *scrq,
				  bool do_h_free)
{
	struct device *dev = &adapter->vdev->dev;
	long rc;

	netdev_dbg(adapter->netdev, "Releasing sub-CRQ\n");

	if (do_h_free) {
		/* Close the sub-crqs */
		do {
			rc = plpar_hcall_norets(H_FREE_SUB_CRQ,
						adapter->vdev->unit_address,
						scrq->crq_num);
		} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

		if (rc) {
			netdev_err(adapter->netdev,
				   "Failed to release sub-CRQ %16lx, rc = %ld\n",
				   scrq->crq_num, rc);
		}
	}

	dma_unmap_single(dev, scrq->msg_token, 4 * PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
	free_pages((unsigned long)scrq->msgs, 2);
	kfree(scrq);
}

static struct ibmvnic_sub_crq_queue *init_sub_crq_queue(struct ibmvnic_adapter
							*adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_sub_crq_queue *scrq;
	int rc;

	scrq = kzalloc(sizeof(*scrq), GFP_KERNEL);
	if (!scrq)
		return NULL;

	scrq->msgs =
		(union sub_crq *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 2);
	if (!scrq->msgs) {
		dev_warn(dev, "Couldn't allocate crq queue messages page\n");
		goto zero_page_failed;
	}

	scrq->msg_token = dma_map_single(dev, scrq->msgs, 4 * PAGE_SIZE,
					 DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, scrq->msg_token)) {
		dev_warn(dev, "Couldn't map crq queue messages page\n");
		goto map_failed;
	}

	rc = h_reg_sub_crq(adapter->vdev->unit_address, scrq->msg_token,
			   4 * PAGE_SIZE, &scrq->crq_num, &scrq->hw_irq);

	if (rc == H_RESOURCE)
		rc = ibmvnic_reset_crq(adapter);

	if (rc == H_CLOSED) {
		dev_warn(dev, "Partner adapter not ready, waiting.\n");
	} else if (rc) {
		dev_warn(dev, "Error %d registering sub-crq\n", rc);
		goto reg_failed;
	}

	scrq->adapter = adapter;
	scrq->size = 4 * PAGE_SIZE / sizeof(*scrq->msgs);
	spin_lock_init(&scrq->lock);

	netdev_dbg(adapter->netdev,
		   "sub-crq initialized, num %lx, hw_irq=%lx, irq=%x\n",
		   scrq->crq_num, scrq->hw_irq, scrq->irq);

	return scrq;

reg_failed:
	dma_unmap_single(dev, scrq->msg_token, 4 * PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
map_failed:
	free_pages((unsigned long)scrq->msgs, 2);
zero_page_failed:
	kfree(scrq);

	return NULL;
}

static void release_sub_crqs(struct ibmvnic_adapter *adapter, bool do_h_free)
{
	int i;

	if (adapter->tx_scrq) {
		for (i = 0; i < adapter->num_active_tx_scrqs; i++) {
			if (!adapter->tx_scrq[i])
				continue;

			netdev_dbg(adapter->netdev, "Releasing tx_scrq[%d]\n",
				   i);
			if (adapter->tx_scrq[i]->irq) {
				free_irq(adapter->tx_scrq[i]->irq,
					 adapter->tx_scrq[i]);
				irq_dispose_mapping(adapter->tx_scrq[i]->irq);
				adapter->tx_scrq[i]->irq = 0;
			}

			release_sub_crq_queue(adapter, adapter->tx_scrq[i],
					      do_h_free);
		}

		kfree(adapter->tx_scrq);
		adapter->tx_scrq = NULL;
		adapter->num_active_tx_scrqs = 0;
	}

	if (adapter->rx_scrq) {
		for (i = 0; i < adapter->num_active_rx_scrqs; i++) {
			if (!adapter->rx_scrq[i])
				continue;

			netdev_dbg(adapter->netdev, "Releasing rx_scrq[%d]\n",
				   i);
			if (adapter->rx_scrq[i]->irq) {
				free_irq(adapter->rx_scrq[i]->irq,
					 adapter->rx_scrq[i]);
				irq_dispose_mapping(adapter->rx_scrq[i]->irq);
				adapter->rx_scrq[i]->irq = 0;
			}

			release_sub_crq_queue(adapter, adapter->rx_scrq[i],
					      do_h_free);
		}

		kfree(adapter->rx_scrq);
		adapter->rx_scrq = NULL;
		adapter->num_active_rx_scrqs = 0;
	}
}

static int disable_scrq_irq(struct ibmvnic_adapter *adapter,
			    struct ibmvnic_sub_crq_queue *scrq)
{
	struct device *dev = &adapter->vdev->dev;
	unsigned long rc;

	rc = plpar_hcall_norets(H_VIOCTL, adapter->vdev->unit_address,
				H_DISABLE_VIO_INTERRUPT, scrq->hw_irq, 0, 0);
	if (rc)
		dev_err(dev, "Couldn't disable scrq irq 0x%lx. rc=%ld\n",
			scrq->hw_irq, rc);
	return rc;
}

static int enable_scrq_irq(struct ibmvnic_adapter *adapter,
			   struct ibmvnic_sub_crq_queue *scrq)
{
	struct device *dev = &adapter->vdev->dev;
	unsigned long rc;

	if (scrq->hw_irq > 0x100000000ULL) {
		dev_err(dev, "bad hw_irq = %lx\n", scrq->hw_irq);
		return 1;
	}

	if (adapter->resetting &&
	    adapter->reset_reason == VNIC_RESET_MOBILITY) {
		u64 val = (0xff000000) | scrq->hw_irq;

		rc = plpar_hcall_norets(H_EOI, val);
		if (rc)
			dev_err(dev, "H_EOI FAILED irq 0x%llx. rc=%ld\n",
				val, rc);
	}

	rc = plpar_hcall_norets(H_VIOCTL, adapter->vdev->unit_address,
				H_ENABLE_VIO_INTERRUPT, scrq->hw_irq, 0, 0);
	if (rc)
		dev_err(dev, "Couldn't enable scrq irq 0x%lx. rc=%ld\n",
			scrq->hw_irq, rc);
	return rc;
}

static int ibmvnic_complete_tx(struct ibmvnic_adapter *adapter,
			       struct ibmvnic_sub_crq_queue *scrq)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_tx_pool *tx_pool;
	struct ibmvnic_tx_buff *txbuff;
	union sub_crq *next;
	int index;
	int i, j;

restart_loop:
	while (pending_scrq(adapter, scrq)) {
		unsigned int pool = scrq->pool_index;
		int num_entries = 0;

		next = ibmvnic_next_scrq(adapter, scrq);
		for (i = 0; i < next->tx_comp.num_comps; i++) {
			if (next->tx_comp.rcs[i]) {
				dev_err(dev, "tx error %x\n",
					next->tx_comp.rcs[i]);
				continue;
			}
			index = be32_to_cpu(next->tx_comp.correlators[i]);
			if (index & IBMVNIC_TSO_POOL_MASK) {
				tx_pool = &adapter->tso_pool[pool];
				index &= ~IBMVNIC_TSO_POOL_MASK;
			} else {
				tx_pool = &adapter->tx_pool[pool];
			}

			txbuff = &tx_pool->tx_buff[index];

			for (j = 0; j < IBMVNIC_MAX_FRAGS_PER_CRQ; j++) {
				if (!txbuff->data_dma[j])
					continue;

				txbuff->data_dma[j] = 0;
			}

			if (txbuff->last_frag) {
				dev_kfree_skb_any(txbuff->skb);
				txbuff->skb = NULL;
			}

			num_entries += txbuff->num_entries;

			tx_pool->free_map[tx_pool->producer_index] = index;
			tx_pool->producer_index =
				(tx_pool->producer_index + 1) %
					tx_pool->num_buffers;
		}
		/* remove tx_comp scrq*/
		next->tx_comp.first = 0;

		if (atomic_sub_return(num_entries, &scrq->used) <=
		    (adapter->req_tx_entries_per_subcrq / 2) &&
		    __netif_subqueue_stopped(adapter->netdev,
					     scrq->pool_index)) {
			netif_wake_subqueue(adapter->netdev, scrq->pool_index);
			netdev_dbg(adapter->netdev, "Started queue %d\n",
				   scrq->pool_index);
		}
	}

	enable_scrq_irq(adapter, scrq);

	if (pending_scrq(adapter, scrq)) {
		disable_scrq_irq(adapter, scrq);
		goto restart_loop;
	}

	return 0;
}

static irqreturn_t ibmvnic_interrupt_tx(int irq, void *instance)
{
	struct ibmvnic_sub_crq_queue *scrq = instance;
	struct ibmvnic_adapter *adapter = scrq->adapter;

	disable_scrq_irq(adapter, scrq);
	ibmvnic_complete_tx(adapter, scrq);

	return IRQ_HANDLED;
}

static irqreturn_t ibmvnic_interrupt_rx(int irq, void *instance)
{
	struct ibmvnic_sub_crq_queue *scrq = instance;
	struct ibmvnic_adapter *adapter = scrq->adapter;

	/* When booting a kdump kernel we can hit pending interrupts
	 * prior to completing driver initialization.
	 */
	if (unlikely(adapter->state != VNIC_OPEN))
		return IRQ_NONE;

	adapter->rx_stats_buffers[scrq->scrq_num].interrupts++;

	if (napi_schedule_prep(&adapter->napi[scrq->scrq_num])) {
		disable_scrq_irq(adapter, scrq);
		__napi_schedule(&adapter->napi[scrq->scrq_num]);
	}

	return IRQ_HANDLED;
}

static int init_sub_crq_irqs(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_sub_crq_queue *scrq;
	int i = 0, j = 0;
	int rc = 0;

	for (i = 0; i < adapter->req_tx_queues; i++) {
		netdev_dbg(adapter->netdev, "Initializing tx_scrq[%d] irq\n",
			   i);
		scrq = adapter->tx_scrq[i];
		scrq->irq = irq_create_mapping(NULL, scrq->hw_irq);

		if (!scrq->irq) {
			rc = -EINVAL;
			dev_err(dev, "Error mapping irq\n");
			goto req_tx_irq_failed;
		}

		rc = request_irq(scrq->irq, ibmvnic_interrupt_tx,
				 0, "ibmvnic_tx", scrq);

		if (rc) {
			dev_err(dev, "Couldn't register tx irq 0x%x. rc=%d\n",
				scrq->irq, rc);
			irq_dispose_mapping(scrq->irq);
			goto req_tx_irq_failed;
		}
	}

	for (i = 0; i < adapter->req_rx_queues; i++) {
		netdev_dbg(adapter->netdev, "Initializing rx_scrq[%d] irq\n",
			   i);
		scrq = adapter->rx_scrq[i];
		scrq->irq = irq_create_mapping(NULL, scrq->hw_irq);
		if (!scrq->irq) {
			rc = -EINVAL;
			dev_err(dev, "Error mapping irq\n");
			goto req_rx_irq_failed;
		}
		rc = request_irq(scrq->irq, ibmvnic_interrupt_rx,
				 0, "ibmvnic_rx", scrq);
		if (rc) {
			dev_err(dev, "Couldn't register rx irq 0x%x. rc=%d\n",
				scrq->irq, rc);
			irq_dispose_mapping(scrq->irq);
			goto req_rx_irq_failed;
		}
	}
	return rc;

req_rx_irq_failed:
	for (j = 0; j < i; j++) {
		free_irq(adapter->rx_scrq[j]->irq, adapter->rx_scrq[j]);
		irq_dispose_mapping(adapter->rx_scrq[j]->irq);
	}
	i = adapter->req_tx_queues;
req_tx_irq_failed:
	for (j = 0; j < i; j++) {
		free_irq(adapter->tx_scrq[j]->irq, adapter->tx_scrq[j]);
		irq_dispose_mapping(adapter->rx_scrq[j]->irq);
	}
	release_sub_crqs(adapter, 1);
	return rc;
}

static int init_sub_crqs(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_sub_crq_queue **allqueues;
	int registered_queues = 0;
	int total_queues;
	int more = 0;
	int i;

	total_queues = adapter->req_tx_queues + adapter->req_rx_queues;

	allqueues = kcalloc(total_queues, sizeof(*allqueues), GFP_KERNEL);
	if (!allqueues)
		return -1;

	for (i = 0; i < total_queues; i++) {
		allqueues[i] = init_sub_crq_queue(adapter);
		if (!allqueues[i]) {
			dev_warn(dev, "Couldn't allocate all sub-crqs\n");
			break;
		}
		registered_queues++;
	}

	/* Make sure we were able to register the minimum number of queues */
	if (registered_queues <
	    adapter->min_tx_queues + adapter->min_rx_queues) {
		dev_err(dev, "Fatal: Couldn't init  min number of sub-crqs\n");
		goto tx_failed;
	}

	/* Distribute the failed allocated queues*/
	for (i = 0; i < total_queues - registered_queues + more ; i++) {
		netdev_dbg(adapter->netdev, "Reducing number of queues\n");
		switch (i % 3) {
		case 0:
			if (adapter->req_rx_queues > adapter->min_rx_queues)
				adapter->req_rx_queues--;
			else
				more++;
			break;
		case 1:
			if (adapter->req_tx_queues > adapter->min_tx_queues)
				adapter->req_tx_queues--;
			else
				more++;
			break;
		}
	}

	adapter->tx_scrq = kcalloc(adapter->req_tx_queues,
				   sizeof(*adapter->tx_scrq), GFP_KERNEL);
	if (!adapter->tx_scrq)
		goto tx_failed;

	for (i = 0; i < adapter->req_tx_queues; i++) {
		adapter->tx_scrq[i] = allqueues[i];
		adapter->tx_scrq[i]->pool_index = i;
		adapter->num_active_tx_scrqs++;
	}

	adapter->rx_scrq = kcalloc(adapter->req_rx_queues,
				   sizeof(*adapter->rx_scrq), GFP_KERNEL);
	if (!adapter->rx_scrq)
		goto rx_failed;

	for (i = 0; i < adapter->req_rx_queues; i++) {
		adapter->rx_scrq[i] = allqueues[i + adapter->req_tx_queues];
		adapter->rx_scrq[i]->scrq_num = i;
		adapter->num_active_rx_scrqs++;
	}

	kfree(allqueues);
	return 0;

rx_failed:
	kfree(adapter->tx_scrq);
	adapter->tx_scrq = NULL;
tx_failed:
	for (i = 0; i < registered_queues; i++)
		release_sub_crq_queue(adapter, allqueues[i], 1);
	kfree(allqueues);
	return -1;
}

static void ibmvnic_send_req_caps(struct ibmvnic_adapter *adapter, int retry)
{
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq crq;
	int max_entries;

	if (!retry) {
		/* Sub-CRQ entries are 32 byte long */
		int entries_page = 4 * PAGE_SIZE / (sizeof(u64) * 4);

		if (adapter->min_tx_entries_per_subcrq > entries_page ||
		    adapter->min_rx_add_entries_per_subcrq > entries_page) {
			dev_err(dev, "Fatal, invalid entries per sub-crq\n");
			return;
		}

		if (adapter->desired.mtu)
			adapter->req_mtu = adapter->desired.mtu;
		else
			adapter->req_mtu = adapter->netdev->mtu + ETH_HLEN;

		if (!adapter->desired.tx_entries)
			adapter->desired.tx_entries =
					adapter->max_tx_entries_per_subcrq;
		if (!adapter->desired.rx_entries)
			adapter->desired.rx_entries =
					adapter->max_rx_add_entries_per_subcrq;

		max_entries = IBMVNIC_MAX_LTB_SIZE /
			      (adapter->req_mtu + IBMVNIC_BUFFER_HLEN);

		if ((adapter->req_mtu + IBMVNIC_BUFFER_HLEN) *
			adapter->desired.tx_entries > IBMVNIC_MAX_LTB_SIZE) {
			adapter->desired.tx_entries = max_entries;
		}

		if ((adapter->req_mtu + IBMVNIC_BUFFER_HLEN) *
			adapter->desired.rx_entries > IBMVNIC_MAX_LTB_SIZE) {
			adapter->desired.rx_entries = max_entries;
		}

		if (adapter->desired.tx_entries)
			adapter->req_tx_entries_per_subcrq =
					adapter->desired.tx_entries;
		else
			adapter->req_tx_entries_per_subcrq =
					adapter->max_tx_entries_per_subcrq;

		if (adapter->desired.rx_entries)
			adapter->req_rx_add_entries_per_subcrq =
					adapter->desired.rx_entries;
		else
			adapter->req_rx_add_entries_per_subcrq =
					adapter->max_rx_add_entries_per_subcrq;

		if (adapter->desired.tx_queues)
			adapter->req_tx_queues =
					adapter->desired.tx_queues;
		else
			adapter->req_tx_queues =
					adapter->opt_tx_comp_sub_queues;

		if (adapter->desired.rx_queues)
			adapter->req_rx_queues =
					adapter->desired.rx_queues;
		else
			adapter->req_rx_queues =
					adapter->opt_rx_comp_queues;

		adapter->req_rx_add_queues = adapter->max_rx_add_queues;
	}

	memset(&crq, 0, sizeof(crq));
	crq.request_capability.first = IBMVNIC_CRQ_CMD;
	crq.request_capability.cmd = REQUEST_CAPABILITY;

	crq.request_capability.capability = cpu_to_be16(REQ_TX_QUEUES);
	crq.request_capability.number = cpu_to_be64(adapter->req_tx_queues);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_RX_QUEUES);
	crq.request_capability.number = cpu_to_be64(adapter->req_rx_queues);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_RX_ADD_QUEUES);
	crq.request_capability.number = cpu_to_be64(adapter->req_rx_add_queues);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability =
	    cpu_to_be16(REQ_TX_ENTRIES_PER_SUBCRQ);
	crq.request_capability.number =
	    cpu_to_be64(adapter->req_tx_entries_per_subcrq);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability =
	    cpu_to_be16(REQ_RX_ADD_ENTRIES_PER_SUBCRQ);
	crq.request_capability.number =
	    cpu_to_be64(adapter->req_rx_add_entries_per_subcrq);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_MTU);
	crq.request_capability.number = cpu_to_be64(adapter->req_mtu);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	if (adapter->netdev->flags & IFF_PROMISC) {
		if (adapter->promisc_supported) {
			crq.request_capability.capability =
			    cpu_to_be16(PROMISC_REQUESTED);
			crq.request_capability.number = cpu_to_be64(1);
			atomic_inc(&adapter->running_cap_crqs);
			ibmvnic_send_crq(adapter, &crq);
		}
	} else {
		crq.request_capability.capability =
		    cpu_to_be16(PROMISC_REQUESTED);
		crq.request_capability.number = cpu_to_be64(0);
		atomic_inc(&adapter->running_cap_crqs);
		ibmvnic_send_crq(adapter, &crq);
	}
}

static int pending_scrq(struct ibmvnic_adapter *adapter,
			struct ibmvnic_sub_crq_queue *scrq)
{
	union sub_crq *entry = &scrq->msgs[scrq->cur];

	if (entry->generic.first & IBMVNIC_CRQ_CMD_RSP)
		return 1;
	else
		return 0;
}

static union sub_crq *ibmvnic_next_scrq(struct ibmvnic_adapter *adapter,
					struct ibmvnic_sub_crq_queue *scrq)
{
	union sub_crq *entry;
	unsigned long flags;

	spin_lock_irqsave(&scrq->lock, flags);
	entry = &scrq->msgs[scrq->cur];
	if (entry->generic.first & IBMVNIC_CRQ_CMD_RSP) {
		if (++scrq->cur == scrq->size)
			scrq->cur = 0;
	} else {
		entry = NULL;
	}
	spin_unlock_irqrestore(&scrq->lock, flags);

	return entry;
}

static union ibmvnic_crq *ibmvnic_next_crq(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *queue = &adapter->crq;
	union ibmvnic_crq *crq;

	crq = &queue->msgs[queue->cur];
	if (crq->generic.first & IBMVNIC_CRQ_CMD_RSP) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	} else {
		crq = NULL;
	}

	return crq;
}

static void print_subcrq_error(struct device *dev, int rc, const char *func)
{
	switch (rc) {
	case H_PARAMETER:
		dev_warn_ratelimited(dev,
				     "%s failed: Send request is malformed or adapter failover pending. (rc=%d)\n",
				     func, rc);
		break;
	case H_CLOSED:
		dev_warn_ratelimited(dev,
				     "%s failed: Backing queue closed. Adapter is down or failover pending. (rc=%d)\n",
				     func, rc);
		break;
	default:
		dev_err_ratelimited(dev, "%s failed: (rc=%d)\n", func, rc);
		break;
	}
}

static int send_subcrq(struct ibmvnic_adapter *adapter, u64 remote_handle,
		       union sub_crq *sub_crq)
{
	unsigned int ua = adapter->vdev->unit_address;
	struct device *dev = &adapter->vdev->dev;
	u64 *u64_crq = (u64 *)sub_crq;
	int rc;

	netdev_dbg(adapter->netdev,
		   "Sending sCRQ %016lx: %016lx %016lx %016lx %016lx\n",
		   (unsigned long int)cpu_to_be64(remote_handle),
		   (unsigned long int)cpu_to_be64(u64_crq[0]),
		   (unsigned long int)cpu_to_be64(u64_crq[1]),
		   (unsigned long int)cpu_to_be64(u64_crq[2]),
		   (unsigned long int)cpu_to_be64(u64_crq[3]));

	/* Make sure the hypervisor sees the complete request */
	mb();

	rc = plpar_hcall_norets(H_SEND_SUB_CRQ, ua,
				cpu_to_be64(remote_handle),
				cpu_to_be64(u64_crq[0]),
				cpu_to_be64(u64_crq[1]),
				cpu_to_be64(u64_crq[2]),
				cpu_to_be64(u64_crq[3]));

	if (rc)
		print_subcrq_error(dev, rc, __func__);

	return rc;
}

static int send_subcrq_indirect(struct ibmvnic_adapter *adapter,
				u64 remote_handle, u64 ioba, u64 num_entries)
{
	unsigned int ua = adapter->vdev->unit_address;
	struct device *dev = &adapter->vdev->dev;
	int rc;

	/* Make sure the hypervisor sees the complete request */
	mb();
	rc = plpar_hcall_norets(H_SEND_SUB_CRQ_INDIRECT, ua,
				cpu_to_be64(remote_handle),
				ioba, num_entries);

	if (rc)
		print_subcrq_error(dev, rc, __func__);

	return rc;
}

static int ibmvnic_send_crq(struct ibmvnic_adapter *adapter,
			    union ibmvnic_crq *crq)
{
	unsigned int ua = adapter->vdev->unit_address;
	struct device *dev = &adapter->vdev->dev;
	u64 *u64_crq = (u64 *)crq;
	int rc;

	netdev_dbg(adapter->netdev, "Sending CRQ: %016lx %016lx\n",
		   (unsigned long int)cpu_to_be64(u64_crq[0]),
		   (unsigned long int)cpu_to_be64(u64_crq[1]));

	if (!adapter->crq.active &&
	    crq->generic.first != IBMVNIC_CRQ_INIT_CMD) {
		dev_warn(dev, "Invalid request detected while CRQ is inactive, possible device state change during reset\n");
		return -EINVAL;
	}

	/* Make sure the hypervisor sees the complete request */
	mb();

	rc = plpar_hcall_norets(H_SEND_CRQ, ua,
				cpu_to_be64(u64_crq[0]),
				cpu_to_be64(u64_crq[1]));

	if (rc) {
		if (rc == H_CLOSED) {
			dev_warn(dev, "CRQ Queue closed\n");
			if (adapter->resetting)
				ibmvnic_reset(adapter, VNIC_RESET_FATAL);
		}

		dev_warn(dev, "Send error (rc=%d)\n", rc);
	}

	return rc;
}

static int ibmvnic_send_crq_init(struct ibmvnic_adapter *adapter)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.generic.first = IBMVNIC_CRQ_INIT_CMD;
	crq.generic.cmd = IBMVNIC_CRQ_INIT;
	netdev_dbg(adapter->netdev, "Sending CRQ init\n");

	return ibmvnic_send_crq(adapter, &crq);
}

static int send_version_xchg(struct ibmvnic_adapter *adapter)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.version_exchange.first = IBMVNIC_CRQ_CMD;
	crq.version_exchange.cmd = VERSION_EXCHANGE;
	crq.version_exchange.version = cpu_to_be16(ibmvnic_version);

	return ibmvnic_send_crq(adapter, &crq);
}

struct vnic_login_client_data {
	u8	type;
	__be16	len;
	char	name[];
} __packed;

static int vnic_client_data_len(struct ibmvnic_adapter *adapter)
{
	int len;

	/* Calculate the amount of buffer space needed for the
	 * vnic client data in the login buffer. There are four entries,
	 * OS name, LPAR name, device name, and a null last entry.
	 */
	len = 4 * sizeof(struct vnic_login_client_data);
	len += 6; /* "Linux" plus NULL */
	len += strlen(utsname()->nodename) + 1;
	len += strlen(adapter->netdev->name) + 1;

	return len;
}

static void vnic_add_client_data(struct ibmvnic_adapter *adapter,
				 struct vnic_login_client_data *vlcd)
{
	const char *os_name = "Linux";
	int len;

	/* Type 1 - LPAR OS */
	vlcd->type = 1;
	len = strlen(os_name) + 1;
	vlcd->len = cpu_to_be16(len);
	strncpy(vlcd->name, os_name, len);
	vlcd = (struct vnic_login_client_data *)(vlcd->name + len);

	/* Type 2 - LPAR name */
	vlcd->type = 2;
	len = strlen(utsname()->nodename) + 1;
	vlcd->len = cpu_to_be16(len);
	strncpy(vlcd->name, utsname()->nodename, len);
	vlcd = (struct vnic_login_client_data *)(vlcd->name + len);

	/* Type 3 - device name */
	vlcd->type = 3;
	len = strlen(adapter->netdev->name) + 1;
	vlcd->len = cpu_to_be16(len);
	strncpy(vlcd->name, adapter->netdev->name, len);
}

static int send_login(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_login_rsp_buffer *login_rsp_buffer;
	struct ibmvnic_login_buffer *login_buffer;
	struct device *dev = &adapter->vdev->dev;
	dma_addr_t rsp_buffer_token;
	dma_addr_t buffer_token;
	size_t rsp_buffer_size;
	union ibmvnic_crq crq;
	size_t buffer_size;
	__be64 *tx_list_p;
	__be64 *rx_list_p;
	int client_data_len;
	struct vnic_login_client_data *vlcd;
	int i;

	if (!adapter->tx_scrq || !adapter->rx_scrq) {
		netdev_err(adapter->netdev,
			   "RX or TX queues are not allocated, device login failed\n");
		return -1;
	}

	release_login_rsp_buffer(adapter);
	client_data_len = vnic_client_data_len(adapter);

	buffer_size =
	    sizeof(struct ibmvnic_login_buffer) +
	    sizeof(u64) * (adapter->req_tx_queues + adapter->req_rx_queues) +
	    client_data_len;

	login_buffer = kzalloc(buffer_size, GFP_ATOMIC);
	if (!login_buffer)
		goto buf_alloc_failed;

	buffer_token = dma_map_single(dev, login_buffer, buffer_size,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(dev, buffer_token)) {
		dev_err(dev, "Couldn't map login buffer\n");
		goto buf_map_failed;
	}

	rsp_buffer_size = sizeof(struct ibmvnic_login_rsp_buffer) +
			  sizeof(u64) * adapter->req_tx_queues +
			  sizeof(u64) * adapter->req_rx_queues +
			  sizeof(u64) * adapter->req_rx_queues +
			  sizeof(u8) * IBMVNIC_TX_DESC_VERSIONS;

	login_rsp_buffer = kmalloc(rsp_buffer_size, GFP_ATOMIC);
	if (!login_rsp_buffer)
		goto buf_rsp_alloc_failed;

	rsp_buffer_token = dma_map_single(dev, login_rsp_buffer,
					  rsp_buffer_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, rsp_buffer_token)) {
		dev_err(dev, "Couldn't map login rsp buffer\n");
		goto buf_rsp_map_failed;
	}

	adapter->login_buf = login_buffer;
	adapter->login_buf_token = buffer_token;
	adapter->login_buf_sz = buffer_size;
	adapter->login_rsp_buf = login_rsp_buffer;
	adapter->login_rsp_buf_token = rsp_buffer_token;
	adapter->login_rsp_buf_sz = rsp_buffer_size;

	login_buffer->len = cpu_to_be32(buffer_size);
	login_buffer->version = cpu_to_be32(INITIAL_VERSION_LB);
	login_buffer->num_txcomp_subcrqs = cpu_to_be32(adapter->req_tx_queues);
	login_buffer->off_txcomp_subcrqs =
	    cpu_to_be32(sizeof(struct ibmvnic_login_buffer));
	login_buffer->num_rxcomp_subcrqs = cpu_to_be32(adapter->req_rx_queues);
	login_buffer->off_rxcomp_subcrqs =
	    cpu_to_be32(sizeof(struct ibmvnic_login_buffer) +
			sizeof(u64) * adapter->req_tx_queues);
	login_buffer->login_rsp_ioba = cpu_to_be32(rsp_buffer_token);
	login_buffer->login_rsp_len = cpu_to_be32(rsp_buffer_size);

	tx_list_p = (__be64 *)((char *)login_buffer +
				      sizeof(struct ibmvnic_login_buffer));
	rx_list_p = (__be64 *)((char *)login_buffer +
				      sizeof(struct ibmvnic_login_buffer) +
				      sizeof(u64) * adapter->req_tx_queues);

	for (i = 0; i < adapter->req_tx_queues; i++) {
		if (adapter->tx_scrq[i]) {
			tx_list_p[i] = cpu_to_be64(adapter->tx_scrq[i]->
						   crq_num);
		}
	}

	for (i = 0; i < adapter->req_rx_queues; i++) {
		if (adapter->rx_scrq[i]) {
			rx_list_p[i] = cpu_to_be64(adapter->rx_scrq[i]->
						   crq_num);
		}
	}

	/* Insert vNIC login client data */
	vlcd = (struct vnic_login_client_data *)
		((char *)rx_list_p + (sizeof(u64) * adapter->req_rx_queues));
	login_buffer->client_data_offset =
			cpu_to_be32((char *)vlcd - (char *)login_buffer);
	login_buffer->client_data_len = cpu_to_be32(client_data_len);

	vnic_add_client_data(adapter, vlcd);

	netdev_dbg(adapter->netdev, "Login Buffer:\n");
	for (i = 0; i < (adapter->login_buf_sz - 1) / 8 + 1; i++) {
		netdev_dbg(adapter->netdev, "%016lx\n",
			   ((unsigned long int *)(adapter->login_buf))[i]);
	}

	memset(&crq, 0, sizeof(crq));
	crq.login.first = IBMVNIC_CRQ_CMD;
	crq.login.cmd = LOGIN;
	crq.login.ioba = cpu_to_be32(buffer_token);
	crq.login.len = cpu_to_be32(buffer_size);
	ibmvnic_send_crq(adapter, &crq);

	return 0;

buf_rsp_map_failed:
	kfree(login_rsp_buffer);
buf_rsp_alloc_failed:
	dma_unmap_single(dev, buffer_token, buffer_size, DMA_TO_DEVICE);
buf_map_failed:
	kfree(login_buffer);
buf_alloc_failed:
	return -1;
}

static int send_request_map(struct ibmvnic_adapter *adapter, dma_addr_t addr,
			    u32 len, u8 map_id)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_map.first = IBMVNIC_CRQ_CMD;
	crq.request_map.cmd = REQUEST_MAP;
	crq.request_map.map_id = map_id;
	crq.request_map.ioba = cpu_to_be32(addr);
	crq.request_map.len = cpu_to_be32(len);
	return ibmvnic_send_crq(adapter, &crq);
}

static int send_request_unmap(struct ibmvnic_adapter *adapter, u8 map_id)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_unmap.first = IBMVNIC_CRQ_CMD;
	crq.request_unmap.cmd = REQUEST_UNMAP;
	crq.request_unmap.map_id = map_id;
	return ibmvnic_send_crq(adapter, &crq);
}

static void send_map_query(struct ibmvnic_adapter *adapter)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.query_map.first = IBMVNIC_CRQ_CMD;
	crq.query_map.cmd = QUERY_MAP;
	ibmvnic_send_crq(adapter, &crq);
}

/* Send a series of CRQs requesting various capabilities of the VNIC server */
static void send_cap_queries(struct ibmvnic_adapter *adapter)
{
	union ibmvnic_crq crq;

	atomic_set(&adapter->running_cap_crqs, 0);
	memset(&crq, 0, sizeof(crq));
	crq.query_capability.first = IBMVNIC_CRQ_CMD;
	crq.query_capability.cmd = QUERY_CAPABILITY;

	crq.query_capability.capability = cpu_to_be16(MIN_TX_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_RX_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_RX_ADD_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_TX_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_RX_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_RX_ADD_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MIN_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MIN_RX_ADD_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MAX_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MAX_RX_ADD_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(TCP_IP_OFFLOAD);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(PROMISC_SUPPORTED);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_MTU);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_MTU);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_MULTICAST_FILTERS);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(VLAN_HEADER_INSERTION);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(RX_VLAN_HEADER_INSERTION);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_TX_SG_ENTRIES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(RX_SG_SUPPORTED);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(OPT_TX_COMP_SUB_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(OPT_RX_COMP_QUEUES);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_RX_BUFADD_Q_PER_RX_COMP_Q);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_RXBA_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(TX_RX_DESC_REQ);
	atomic_inc(&adapter->running_cap_crqs);
	ibmvnic_send_crq(adapter, &crq);
}

static void handle_vpd_size_rsp(union ibmvnic_crq *crq,
				struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;

	if (crq->get_vpd_size_rsp.rc.code) {
		dev_err(dev, "Error retrieving VPD size, rc=%x\n",
			crq->get_vpd_size_rsp.rc.code);
		complete(&adapter->fw_done);
		return;
	}

	adapter->vpd->len = be64_to_cpu(crq->get_vpd_size_rsp.len);
	complete(&adapter->fw_done);
}

static void handle_vpd_rsp(union ibmvnic_crq *crq,
			   struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	unsigned char *substr = NULL;
	u8 fw_level_len = 0;

	memset(adapter->fw_version, 0, 32);

	dma_unmap_single(dev, adapter->vpd->dma_addr, adapter->vpd->len,
			 DMA_FROM_DEVICE);

	if (crq->get_vpd_rsp.rc.code) {
		dev_err(dev, "Error retrieving VPD from device, rc=%x\n",
			crq->get_vpd_rsp.rc.code);
		goto complete;
	}

	/* get the position of the firmware version info
	 * located after the ASCII 'RM' substring in the buffer
	 */
	substr = strnstr(adapter->vpd->buff, "RM", adapter->vpd->len);
	if (!substr) {
		dev_info(dev, "Warning - No FW level has been provided in the VPD buffer by the VIOS Server\n");
		goto complete;
	}

	/* get length of firmware level ASCII substring */
	if ((substr + 2) < (adapter->vpd->buff + adapter->vpd->len)) {
		fw_level_len = *(substr + 2);
	} else {
		dev_info(dev, "Length of FW substr extrapolated VDP buff\n");
		goto complete;
	}

	/* copy firmware version string from vpd into adapter */
	if ((substr + 3 + fw_level_len) <
	    (adapter->vpd->buff + adapter->vpd->len)) {
		strncpy((char *)adapter->fw_version, substr + 3, fw_level_len);
	} else {
		dev_info(dev, "FW substr extrapolated VPD buff\n");
	}

complete:
	if (adapter->fw_version[0] == '\0')
		strncpy((char *)adapter->fw_version, "N/A", 3 * sizeof(char));
	complete(&adapter->fw_done);
}

static void handle_query_ip_offload_rsp(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_query_ip_offload_buffer *buf = &adapter->ip_offload_buf;
	union ibmvnic_crq crq;
	int i;

	dma_unmap_single(dev, adapter->ip_offload_tok,
			 sizeof(adapter->ip_offload_buf), DMA_FROM_DEVICE);

	netdev_dbg(adapter->netdev, "Query IP Offload Buffer:\n");
	for (i = 0; i < (sizeof(adapter->ip_offload_buf) - 1) / 8 + 1; i++)
		netdev_dbg(adapter->netdev, "%016lx\n",
			   ((unsigned long int *)(buf))[i]);

	netdev_dbg(adapter->netdev, "ipv4_chksum = %d\n", buf->ipv4_chksum);
	netdev_dbg(adapter->netdev, "ipv6_chksum = %d\n", buf->ipv6_chksum);
	netdev_dbg(adapter->netdev, "tcp_ipv4_chksum = %d\n",
		   buf->tcp_ipv4_chksum);
	netdev_dbg(adapter->netdev, "tcp_ipv6_chksum = %d\n",
		   buf->tcp_ipv6_chksum);
	netdev_dbg(adapter->netdev, "udp_ipv4_chksum = %d\n",
		   buf->udp_ipv4_chksum);
	netdev_dbg(adapter->netdev, "udp_ipv6_chksum = %d\n",
		   buf->udp_ipv6_chksum);
	netdev_dbg(adapter->netdev, "large_tx_ipv4 = %d\n",
		   buf->large_tx_ipv4);
	netdev_dbg(adapter->netdev, "large_tx_ipv6 = %d\n",
		   buf->large_tx_ipv6);
	netdev_dbg(adapter->netdev, "large_rx_ipv4 = %d\n",
		   buf->large_rx_ipv4);
	netdev_dbg(adapter->netdev, "large_rx_ipv6 = %d\n",
		   buf->large_rx_ipv6);
	netdev_dbg(adapter->netdev, "max_ipv4_hdr_sz = %d\n",
		   buf->max_ipv4_header_size);
	netdev_dbg(adapter->netdev, "max_ipv6_hdr_sz = %d\n",
		   buf->max_ipv6_header_size);
	netdev_dbg(adapter->netdev, "max_tcp_hdr_size = %d\n",
		   buf->max_tcp_header_size);
	netdev_dbg(adapter->netdev, "max_udp_hdr_size = %d\n",
		   buf->max_udp_header_size);
	netdev_dbg(adapter->netdev, "max_large_tx_size = %d\n",
		   buf->max_large_tx_size);
	netdev_dbg(adapter->netdev, "max_large_rx_size = %d\n",
		   buf->max_large_rx_size);
	netdev_dbg(adapter->netdev, "ipv6_ext_hdr = %d\n",
		   buf->ipv6_extension_header);
	netdev_dbg(adapter->netdev, "tcp_pseudosum_req = %d\n",
		   buf->tcp_pseudosum_req);
	netdev_dbg(adapter->netdev, "num_ipv6_ext_hd = %d\n",
		   buf->num_ipv6_ext_headers);
	netdev_dbg(adapter->netdev, "off_ipv6_ext_hd = %d\n",
		   buf->off_ipv6_ext_headers);

	adapter->ip_offload_ctrl_tok =
	    dma_map_single(dev, &adapter->ip_offload_ctrl,
			   sizeof(adapter->ip_offload_ctrl), DMA_TO_DEVICE);

	if (dma_mapping_error(dev, adapter->ip_offload_ctrl_tok)) {
		dev_err(dev, "Couldn't map ip offload control buffer\n");
		return;
	}

	adapter->ip_offload_ctrl.len =
	    cpu_to_be32(sizeof(adapter->ip_offload_ctrl));
	adapter->ip_offload_ctrl.version = cpu_to_be32(INITIAL_VERSION_IOB);
	adapter->ip_offload_ctrl.ipv4_chksum = buf->ipv4_chksum;
	adapter->ip_offload_ctrl.ipv6_chksum = buf->ipv6_chksum;
	adapter->ip_offload_ctrl.tcp_ipv4_chksum = buf->tcp_ipv4_chksum;
	adapter->ip_offload_ctrl.udp_ipv4_chksum = buf->udp_ipv4_chksum;
	adapter->ip_offload_ctrl.tcp_ipv6_chksum = buf->tcp_ipv6_chksum;
	adapter->ip_offload_ctrl.udp_ipv6_chksum = buf->udp_ipv6_chksum;
	adapter->ip_offload_ctrl.large_tx_ipv4 = buf->large_tx_ipv4;
	adapter->ip_offload_ctrl.large_tx_ipv6 = buf->large_tx_ipv6;

	/* large_rx disabled for now, additional features needed */
	adapter->ip_offload_ctrl.large_rx_ipv4 = 0;
	adapter->ip_offload_ctrl.large_rx_ipv6 = 0;

	adapter->netdev->features = NETIF_F_SG | NETIF_F_GSO;

	if (buf->tcp_ipv4_chksum || buf->udp_ipv4_chksum)
		adapter->netdev->features |= NETIF_F_IP_CSUM;

	if (buf->tcp_ipv6_chksum || buf->udp_ipv6_chksum)
		adapter->netdev->features |= NETIF_F_IPV6_CSUM;

	if ((adapter->netdev->features &
	    (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)))
		adapter->netdev->features |= NETIF_F_RXCSUM;

	if (buf->large_tx_ipv4)
		adapter->netdev->features |= NETIF_F_TSO;
	if (buf->large_tx_ipv6)
		adapter->netdev->features |= NETIF_F_TSO6;

	adapter->netdev->hw_features |= adapter->netdev->features;

	memset(&crq, 0, sizeof(crq));
	crq.control_ip_offload.first = IBMVNIC_CRQ_CMD;
	crq.control_ip_offload.cmd = CONTROL_IP_OFFLOAD;
	crq.control_ip_offload.len =
	    cpu_to_be32(sizeof(adapter->ip_offload_ctrl));
	crq.control_ip_offload.ioba = cpu_to_be32(adapter->ip_offload_ctrl_tok);
	ibmvnic_send_crq(adapter, &crq);
}

static const char *ibmvnic_fw_err_cause(u16 cause)
{
	switch (cause) {
	case ADAPTER_PROBLEM:
		return "adapter problem";
	case BUS_PROBLEM:
		return "bus problem";
	case FW_PROBLEM:
		return "firmware problem";
	case DD_PROBLEM:
		return "device driver problem";
	case EEH_RECOVERY:
		return "EEH recovery";
	case FW_UPDATED:
		return "firmware updated";
	case LOW_MEMORY:
		return "low Memory";
	default:
		return "unknown";
	}
}

static void handle_error_indication(union ibmvnic_crq *crq,
				    struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	u16 cause;

	cause = be16_to_cpu(crq->error_indication.error_cause);

	dev_warn_ratelimited(dev,
			     "Firmware reports %serror, cause: %s. Starting recovery...\n",
			     crq->error_indication.flags
				& IBMVNIC_FATAL_ERROR ? "FATAL " : "",
			     ibmvnic_fw_err_cause(cause));

	if (crq->error_indication.flags & IBMVNIC_FATAL_ERROR)
		ibmvnic_reset(adapter, VNIC_RESET_FATAL);
	else
		ibmvnic_reset(adapter, VNIC_RESET_NON_FATAL);
}

static int handle_change_mac_rsp(union ibmvnic_crq *crq,
				 struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	long rc;

	rc = crq->change_mac_addr_rsp.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in CHANGE_MAC_ADDR_RSP\n", rc);
		goto out;
	}
	memcpy(netdev->dev_addr, &crq->change_mac_addr_rsp.mac_addr[0],
	       ETH_ALEN);
out:
	complete(&adapter->fw_done);
	return rc;
}

static void handle_request_cap_rsp(union ibmvnic_crq *crq,
				   struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	u64 *req_value;
	char *name;

	atomic_dec(&adapter->running_cap_crqs);
	switch (be16_to_cpu(crq->request_capability_rsp.capability)) {
	case REQ_TX_QUEUES:
		req_value = &adapter->req_tx_queues;
		name = "tx";
		break;
	case REQ_RX_QUEUES:
		req_value = &adapter->req_rx_queues;
		name = "rx";
		break;
	case REQ_RX_ADD_QUEUES:
		req_value = &adapter->req_rx_add_queues;
		name = "rx_add";
		break;
	case REQ_TX_ENTRIES_PER_SUBCRQ:
		req_value = &adapter->req_tx_entries_per_subcrq;
		name = "tx_entries_per_subcrq";
		break;
	case REQ_RX_ADD_ENTRIES_PER_SUBCRQ:
		req_value = &adapter->req_rx_add_entries_per_subcrq;
		name = "rx_add_entries_per_subcrq";
		break;
	case REQ_MTU:
		req_value = &adapter->req_mtu;
		name = "mtu";
		break;
	case PROMISC_REQUESTED:
		req_value = &adapter->promisc;
		name = "promisc";
		break;
	default:
		dev_err(dev, "Got invalid cap request rsp %d\n",
			crq->request_capability.capability);
		return;
	}

	switch (crq->request_capability_rsp.rc.code) {
	case SUCCESS:
		break;
	case PARTIALSUCCESS:
		dev_info(dev, "req=%lld, rsp=%ld in %s queue, retrying.\n",
			 *req_value,
			 (long int)be64_to_cpu(crq->request_capability_rsp.
					       number), name);

		if (be16_to_cpu(crq->request_capability_rsp.capability) ==
		    REQ_MTU) {
			pr_err("mtu of %llu is not supported. Reverting.\n",
			       *req_value);
			*req_value = adapter->fallback.mtu;
		} else {
			*req_value =
				be64_to_cpu(crq->request_capability_rsp.number);
		}

		ibmvnic_send_req_caps(adapter, 1);
		return;
	default:
		dev_err(dev, "Error %d in request cap rsp\n",
			crq->request_capability_rsp.rc.code);
		return;
	}

	/* Done receiving requested capabilities, query IP offload support */
	if (atomic_read(&adapter->running_cap_crqs) == 0) {
		union ibmvnic_crq newcrq;
		int buf_sz = sizeof(struct ibmvnic_query_ip_offload_buffer);
		struct ibmvnic_query_ip_offload_buffer *ip_offload_buf =
		    &adapter->ip_offload_buf;

		adapter->wait_capability = false;
		adapter->ip_offload_tok = dma_map_single(dev, ip_offload_buf,
							 buf_sz,
							 DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, adapter->ip_offload_tok)) {
			if (!firmware_has_feature(FW_FEATURE_CMO))
				dev_err(dev, "Couldn't map offload buffer\n");
			return;
		}

		memset(&newcrq, 0, sizeof(newcrq));
		newcrq.query_ip_offload.first = IBMVNIC_CRQ_CMD;
		newcrq.query_ip_offload.cmd = QUERY_IP_OFFLOAD;
		newcrq.query_ip_offload.len = cpu_to_be32(buf_sz);
		newcrq.query_ip_offload.ioba =
		    cpu_to_be32(adapter->ip_offload_tok);

		ibmvnic_send_crq(adapter, &newcrq);
	}
}

static int handle_login_rsp(union ibmvnic_crq *login_rsp_crq,
			    struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct net_device *netdev = adapter->netdev;
	struct ibmvnic_login_rsp_buffer *login_rsp = adapter->login_rsp_buf;
	struct ibmvnic_login_buffer *login = adapter->login_buf;
	int i;

	dma_unmap_single(dev, adapter->login_buf_token, adapter->login_buf_sz,
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, adapter->login_rsp_buf_token,
			 adapter->login_rsp_buf_sz, DMA_FROM_DEVICE);

	/* If the number of queues requested can't be allocated by the
	 * server, the login response will return with code 1. We will need
	 * to resend the login buffer with fewer queues requested.
	 */
	if (login_rsp_crq->generic.rc.code) {
		adapter->init_done_rc = login_rsp_crq->generic.rc.code;
		complete(&adapter->init_done);
		return 0;
	}

	netdev->mtu = adapter->req_mtu - ETH_HLEN;

	netdev_dbg(adapter->netdev, "Login Response Buffer:\n");
	for (i = 0; i < (adapter->login_rsp_buf_sz - 1) / 8 + 1; i++) {
		netdev_dbg(adapter->netdev, "%016lx\n",
			   ((unsigned long int *)(adapter->login_rsp_buf))[i]);
	}

	/* Sanity checks */
	if (login->num_txcomp_subcrqs != login_rsp->num_txsubm_subcrqs ||
	    (be32_to_cpu(login->num_rxcomp_subcrqs) *
	     adapter->req_rx_add_queues !=
	     be32_to_cpu(login_rsp->num_rxadd_subcrqs))) {
		dev_err(dev, "FATAL: Inconsistent login and login rsp\n");
		ibmvnic_remove(adapter->vdev);
		return -EIO;
	}
	release_login_buffer(adapter);
	complete(&adapter->init_done);

	return 0;
}

static void handle_request_unmap_rsp(union ibmvnic_crq *crq,
				     struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	long rc;

	rc = crq->request_unmap_rsp.rc.code;
	if (rc)
		dev_err(dev, "Error %ld in REQUEST_UNMAP_RSP\n", rc);
}

static void handle_query_map_rsp(union ibmvnic_crq *crq,
				 struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	long rc;

	rc = crq->query_map_rsp.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in QUERY_MAP_RSP\n", rc);
		return;
	}
	netdev_dbg(netdev, "page_size = %d\ntot_pages = %d\nfree_pages = %d\n",
		   crq->query_map_rsp.page_size, crq->query_map_rsp.tot_pages,
		   crq->query_map_rsp.free_pages);
}

static void handle_query_cap_rsp(union ibmvnic_crq *crq,
				 struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	long rc;

	atomic_dec(&adapter->running_cap_crqs);
	netdev_dbg(netdev, "Outstanding queries: %d\n",
		   atomic_read(&adapter->running_cap_crqs));
	rc = crq->query_capability.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in QUERY_CAP_RSP\n", rc);
		goto out;
	}

	switch (be16_to_cpu(crq->query_capability.capability)) {
	case MIN_TX_QUEUES:
		adapter->min_tx_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_tx_queues = %lld\n",
			   adapter->min_tx_queues);
		break;
	case MIN_RX_QUEUES:
		adapter->min_rx_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_queues = %lld\n",
			   adapter->min_rx_queues);
		break;
	case MIN_RX_ADD_QUEUES:
		adapter->min_rx_add_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_add_queues = %lld\n",
			   adapter->min_rx_add_queues);
		break;
	case MAX_TX_QUEUES:
		adapter->max_tx_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_queues = %lld\n",
			   adapter->max_tx_queues);
		break;
	case MAX_RX_QUEUES:
		adapter->max_rx_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_queues = %lld\n",
			   adapter->max_rx_queues);
		break;
	case MAX_RX_ADD_QUEUES:
		adapter->max_rx_add_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_add_queues = %lld\n",
			   adapter->max_rx_add_queues);
		break;
	case MIN_TX_ENTRIES_PER_SUBCRQ:
		adapter->min_tx_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_tx_entries_per_subcrq = %lld\n",
			   adapter->min_tx_entries_per_subcrq);
		break;
	case MIN_RX_ADD_ENTRIES_PER_SUBCRQ:
		adapter->min_rx_add_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_add_entrs_per_subcrq = %lld\n",
			   adapter->min_rx_add_entries_per_subcrq);
		break;
	case MAX_TX_ENTRIES_PER_SUBCRQ:
		adapter->max_tx_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_entries_per_subcrq = %lld\n",
			   adapter->max_tx_entries_per_subcrq);
		break;
	case MAX_RX_ADD_ENTRIES_PER_SUBCRQ:
		adapter->max_rx_add_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_add_entrs_per_subcrq = %lld\n",
			   adapter->max_rx_add_entries_per_subcrq);
		break;
	case TCP_IP_OFFLOAD:
		adapter->tcp_ip_offload =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "tcp_ip_offload = %lld\n",
			   adapter->tcp_ip_offload);
		break;
	case PROMISC_SUPPORTED:
		adapter->promisc_supported =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "promisc_supported = %lld\n",
			   adapter->promisc_supported);
		break;
	case MIN_MTU:
		adapter->min_mtu = be64_to_cpu(crq->query_capability.number);
		netdev->min_mtu = adapter->min_mtu - ETH_HLEN;
		netdev_dbg(netdev, "min_mtu = %lld\n", adapter->min_mtu);
		break;
	case MAX_MTU:
		adapter->max_mtu = be64_to_cpu(crq->query_capability.number);
		netdev->max_mtu = adapter->max_mtu - ETH_HLEN;
		netdev_dbg(netdev, "max_mtu = %lld\n", adapter->max_mtu);
		break;
	case MAX_MULTICAST_FILTERS:
		adapter->max_multicast_filters =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_multicast_filters = %lld\n",
			   adapter->max_multicast_filters);
		break;
	case VLAN_HEADER_INSERTION:
		adapter->vlan_header_insertion =
		    be64_to_cpu(crq->query_capability.number);
		if (adapter->vlan_header_insertion)
			netdev->features |= NETIF_F_HW_VLAN_STAG_TX;
		netdev_dbg(netdev, "vlan_header_insertion = %lld\n",
			   adapter->vlan_header_insertion);
		break;
	case RX_VLAN_HEADER_INSERTION:
		adapter->rx_vlan_header_insertion =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "rx_vlan_header_insertion = %lld\n",
			   adapter->rx_vlan_header_insertion);
		break;
	case MAX_TX_SG_ENTRIES:
		adapter->max_tx_sg_entries =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_sg_entries = %lld\n",
			   adapter->max_tx_sg_entries);
		break;
	case RX_SG_SUPPORTED:
		adapter->rx_sg_supported =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "rx_sg_supported = %lld\n",
			   adapter->rx_sg_supported);
		break;
	case OPT_TX_COMP_SUB_QUEUES:
		adapter->opt_tx_comp_sub_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_tx_comp_sub_queues = %lld\n",
			   adapter->opt_tx_comp_sub_queues);
		break;
	case OPT_RX_COMP_QUEUES:
		adapter->opt_rx_comp_queues =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_rx_comp_queues = %lld\n",
			   adapter->opt_rx_comp_queues);
		break;
	case OPT_RX_BUFADD_Q_PER_RX_COMP_Q:
		adapter->opt_rx_bufadd_q_per_rx_comp_q =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_rx_bufadd_q_per_rx_comp_q = %lld\n",
			   adapter->opt_rx_bufadd_q_per_rx_comp_q);
		break;
	case OPT_TX_ENTRIES_PER_SUBCRQ:
		adapter->opt_tx_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_tx_entries_per_subcrq = %lld\n",
			   adapter->opt_tx_entries_per_subcrq);
		break;
	case OPT_RXBA_ENTRIES_PER_SUBCRQ:
		adapter->opt_rxba_entries_per_subcrq =
		    be64_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_rxba_entries_per_subcrq = %lld\n",
			   adapter->opt_rxba_entries_per_subcrq);
		break;
	case TX_RX_DESC_REQ:
		adapter->tx_rx_desc_req = crq->query_capability.number;
		netdev_dbg(netdev, "tx_rx_desc_req = %llx\n",
			   adapter->tx_rx_desc_req);
		break;

	default:
		netdev_err(netdev, "Got invalid cap rsp %d\n",
			   crq->query_capability.capability);
	}

out:
	if (atomic_read(&adapter->running_cap_crqs) == 0) {
		adapter->wait_capability = false;
		ibmvnic_send_req_caps(adapter, 0);
	}
}

static void ibmvnic_handle_crq(union ibmvnic_crq *crq,
			       struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_generic_crq *gen_crq = &crq->generic;
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	u64 *u64_crq = (u64 *)crq;
	long rc;

	netdev_dbg(netdev, "Handling CRQ: %016lx %016lx\n",
		   (unsigned long int)cpu_to_be64(u64_crq[0]),
		   (unsigned long int)cpu_to_be64(u64_crq[1]));
	switch (gen_crq->first) {
	case IBMVNIC_CRQ_INIT_RSP:
		switch (gen_crq->cmd) {
		case IBMVNIC_CRQ_INIT:
			dev_info(dev, "Partner initialized\n");
			adapter->from_passive_init = true;
			adapter->failover_pending = false;
			if (!completion_done(&adapter->init_done)) {
				complete(&adapter->init_done);
				adapter->init_done_rc = -EIO;
			}
			ibmvnic_reset(adapter, VNIC_RESET_FAILOVER);
			break;
		case IBMVNIC_CRQ_INIT_COMPLETE:
			dev_info(dev, "Partner initialization complete\n");
			adapter->crq.active = true;
			send_version_xchg(adapter);
			break;
		default:
			dev_err(dev, "Unknown crq cmd: %d\n", gen_crq->cmd);
		}
		return;
	case IBMVNIC_CRQ_XPORT_EVENT:
		netif_carrier_off(netdev);
		adapter->crq.active = false;
		if (adapter->resetting)
			adapter->force_reset_recovery = true;
		if (gen_crq->cmd == IBMVNIC_PARTITION_MIGRATED) {
			dev_info(dev, "Migrated, re-enabling adapter\n");
			ibmvnic_reset(adapter, VNIC_RESET_MOBILITY);
		} else if (gen_crq->cmd == IBMVNIC_DEVICE_FAILOVER) {
			dev_info(dev, "Backing device failover detected\n");
			adapter->failover_pending = true;
		} else {
			/* The adapter lost the connection */
			dev_err(dev, "Virtual Adapter failed (rc=%d)\n",
				gen_crq->cmd);
			ibmvnic_reset(adapter, VNIC_RESET_FATAL);
		}
		return;
	case IBMVNIC_CRQ_CMD_RSP:
		break;
	default:
		dev_err(dev, "Got an invalid msg type 0x%02x\n",
			gen_crq->first);
		return;
	}

	switch (gen_crq->cmd) {
	case VERSION_EXCHANGE_RSP:
		rc = crq->version_exchange_rsp.rc.code;
		if (rc) {
			dev_err(dev, "Error %ld in VERSION_EXCHG_RSP\n", rc);
			break;
		}
		dev_info(dev, "Partner protocol version is %d\n",
			 crq->version_exchange_rsp.version);
		if (be16_to_cpu(crq->version_exchange_rsp.version) <
		    ibmvnic_version)
			ibmvnic_version =
			    be16_to_cpu(crq->version_exchange_rsp.version);
		send_cap_queries(adapter);
		break;
	case QUERY_CAPABILITY_RSP:
		handle_query_cap_rsp(crq, adapter);
		break;
	case QUERY_MAP_RSP:
		handle_query_map_rsp(crq, adapter);
		break;
	case REQUEST_MAP_RSP:
		adapter->fw_done_rc = crq->request_map_rsp.rc.code;
		complete(&adapter->fw_done);
		break;
	case REQUEST_UNMAP_RSP:
		handle_request_unmap_rsp(crq, adapter);
		break;
	case REQUEST_CAPABILITY_RSP:
		handle_request_cap_rsp(crq, adapter);
		break;
	case LOGIN_RSP:
		netdev_dbg(netdev, "Got Login Response\n");
		handle_login_rsp(crq, adapter);
		break;
	case LOGICAL_LINK_STATE_RSP:
		netdev_dbg(netdev,
			   "Got Logical Link State Response, state: %d rc: %d\n",
			   crq->logical_link_state_rsp.link_state,
			   crq->logical_link_state_rsp.rc.code);
		adapter->logical_link_state =
		    crq->logical_link_state_rsp.link_state;
		adapter->init_done_rc = crq->logical_link_state_rsp.rc.code;
		complete(&adapter->init_done);
		break;
	case LINK_STATE_INDICATION:
		netdev_dbg(netdev, "Got Logical Link State Indication\n");
		adapter->phys_link_state =
		    crq->link_state_indication.phys_link_state;
		adapter->logical_link_state =
		    crq->link_state_indication.logical_link_state;
		break;
	case CHANGE_MAC_ADDR_RSP:
		netdev_dbg(netdev, "Got MAC address change Response\n");
		adapter->fw_done_rc = handle_change_mac_rsp(crq, adapter);
		break;
	case ERROR_INDICATION:
		netdev_dbg(netdev, "Got Error Indication\n");
		handle_error_indication(crq, adapter);
		break;
	case REQUEST_STATISTICS_RSP:
		netdev_dbg(netdev, "Got Statistics Response\n");
		complete(&adapter->stats_done);
		break;
	case QUERY_IP_OFFLOAD_RSP:
		netdev_dbg(netdev, "Got Query IP offload Response\n");
		handle_query_ip_offload_rsp(adapter);
		break;
	case MULTICAST_CTRL_RSP:
		netdev_dbg(netdev, "Got multicast control Response\n");
		break;
	case CONTROL_IP_OFFLOAD_RSP:
		netdev_dbg(netdev, "Got Control IP offload Response\n");
		dma_unmap_single(dev, adapter->ip_offload_ctrl_tok,
				 sizeof(adapter->ip_offload_ctrl),
				 DMA_TO_DEVICE);
		complete(&adapter->init_done);
		break;
	case COLLECT_FW_TRACE_RSP:
		netdev_dbg(netdev, "Got Collect firmware trace Response\n");
		complete(&adapter->fw_done);
		break;
	case GET_VPD_SIZE_RSP:
		handle_vpd_size_rsp(crq, adapter);
		break;
	case GET_VPD_RSP:
		handle_vpd_rsp(crq, adapter);
		break;
	default:
		netdev_err(netdev, "Got an invalid cmd type 0x%02x\n",
			   gen_crq->cmd);
	}
}

static irqreturn_t ibmvnic_interrupt(int irq, void *instance)
{
	struct ibmvnic_adapter *adapter = instance;

	tasklet_schedule(&adapter->tasklet);
	return IRQ_HANDLED;
}

static void ibmvnic_tasklet(void *data)
{
	struct ibmvnic_adapter *adapter = data;
	struct ibmvnic_crq_queue *queue = &adapter->crq;
	union ibmvnic_crq *crq;
	unsigned long flags;
	bool done = false;

	spin_lock_irqsave(&queue->lock, flags);
	while (!done) {
		/* Pull all the valid messages off the CRQ */
		while ((crq = ibmvnic_next_crq(adapter)) != NULL) {
			ibmvnic_handle_crq(crq, adapter);
			crq->generic.first = 0;
		}

		/* remain in tasklet until all
		 * capabilities responses are received
		 */
		if (!adapter->wait_capability)
			done = true;
	}
	/* if capabilities CRQ's were sent in this tasklet, the following
	 * tasklet must wait until all responses are received
	 */
	if (atomic_read(&adapter->running_cap_crqs) != 0)
		adapter->wait_capability = true;
	spin_unlock_irqrestore(&queue->lock, flags);
}

static int ibmvnic_reenable_crq_queue(struct ibmvnic_adapter *adapter)
{
	struct vio_dev *vdev = adapter->vdev;
	int rc;

	do {
		rc = plpar_hcall_norets(H_ENABLE_CRQ, vdev->unit_address);
	} while (rc == H_IN_PROGRESS || rc == H_BUSY || H_IS_LONG_BUSY(rc));

	if (rc)
		dev_err(&vdev->dev, "Error enabling adapter (rc=%d)\n", rc);

	return rc;
}

static int ibmvnic_reset_crq(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *crq = &adapter->crq;
	struct device *dev = &adapter->vdev->dev;
	struct vio_dev *vdev = adapter->vdev;
	int rc;

	/* Close the CRQ */
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	/* Clean out the queue */
	memset(crq->msgs, 0, PAGE_SIZE);
	crq->cur = 0;
	crq->active = false;

	/* And re-open it again */
	rc = plpar_hcall_norets(H_REG_CRQ, vdev->unit_address,
				crq->msg_token, PAGE_SIZE);

	if (rc == H_CLOSED)
		/* Adapter is good, but other end is not ready */
		dev_warn(dev, "Partner adapter not ready\n");
	else if (rc != 0)
		dev_warn(dev, "Couldn't register crq (rc=%d)\n", rc);

	return rc;
}

static void release_crq_queue(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *crq = &adapter->crq;
	struct vio_dev *vdev = adapter->vdev;
	long rc;

	if (!crq->msgs)
		return;

	netdev_dbg(adapter->netdev, "Releasing CRQ\n");
	free_irq(vdev->irq, adapter);
	tasklet_kill(&adapter->tasklet);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	dma_unmap_single(&vdev->dev, crq->msg_token, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
	free_page((unsigned long)crq->msgs);
	crq->msgs = NULL;
	crq->active = false;
}

static int init_crq_queue(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *crq = &adapter->crq;
	struct device *dev = &adapter->vdev->dev;
	struct vio_dev *vdev = adapter->vdev;
	int rc, retrc = -ENOMEM;

	if (crq->msgs)
		return 0;

	crq->msgs = (union ibmvnic_crq *)get_zeroed_page(GFP_KERNEL);
	/* Should we allocate more than one page? */

	if (!crq->msgs)
		return -ENOMEM;

	crq->size = PAGE_SIZE / sizeof(*crq->msgs);
	crq->msg_token = dma_map_single(dev, crq->msgs, PAGE_SIZE,
					DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, crq->msg_token))
		goto map_failed;

	rc = plpar_hcall_norets(H_REG_CRQ, vdev->unit_address,
				crq->msg_token, PAGE_SIZE);

	if (rc == H_RESOURCE)
		/* maybe kexecing and resource is busy. try a reset */
		rc = ibmvnic_reset_crq(adapter);
	retrc = rc;

	if (rc == H_CLOSED) {
		dev_warn(dev, "Partner adapter not ready\n");
	} else if (rc) {
		dev_warn(dev, "Error %d opening adapter\n", rc);
		goto reg_crq_failed;
	}

	retrc = 0;

	tasklet_init(&adapter->tasklet, (void *)ibmvnic_tasklet,
		     (unsigned long)adapter);

	netdev_dbg(adapter->netdev, "registering irq 0x%x\n", vdev->irq);
	rc = request_irq(vdev->irq, ibmvnic_interrupt, 0, IBMVNIC_NAME,
			 adapter);
	if (rc) {
		dev_err(dev, "Couldn't register irq 0x%x. rc=%d\n",
			vdev->irq, rc);
		goto req_irq_failed;
	}

	rc = vio_enable_interrupts(vdev);
	if (rc) {
		dev_err(dev, "Error %d enabling interrupts\n", rc);
		goto req_irq_failed;
	}

	crq->cur = 0;
	spin_lock_init(&crq->lock);

	return retrc;

req_irq_failed:
	tasklet_kill(&adapter->tasklet);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));
reg_crq_failed:
	dma_unmap_single(dev, crq->msg_token, PAGE_SIZE, DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long)crq->msgs);
	crq->msgs = NULL;
	return retrc;
}

static int ibmvnic_reset_init(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	unsigned long timeout = msecs_to_jiffies(30000);
	u64 old_num_rx_queues, old_num_tx_queues;
	int rc;

	adapter->from_passive_init = false;

	old_num_rx_queues = adapter->req_rx_queues;
	old_num_tx_queues = adapter->req_tx_queues;

	reinit_completion(&adapter->init_done);
	adapter->init_done_rc = 0;
	ibmvnic_send_crq_init(adapter);
	if (!wait_for_completion_timeout(&adapter->init_done, timeout)) {
		dev_err(dev, "Initialization sequence timed out\n");
		return -1;
	}

	if (adapter->init_done_rc) {
		release_crq_queue(adapter);
		return adapter->init_done_rc;
	}

	if (adapter->from_passive_init) {
		adapter->state = VNIC_OPEN;
		adapter->from_passive_init = false;
		return -1;
	}

	if (adapter->resetting && !adapter->wait_for_reset &&
	    adapter->reset_reason != VNIC_RESET_MOBILITY) {
		if (adapter->req_rx_queues != old_num_rx_queues ||
		    adapter->req_tx_queues != old_num_tx_queues) {
			release_sub_crqs(adapter, 0);
			rc = init_sub_crqs(adapter);
		} else {
			rc = reset_sub_crq_queues(adapter);
		}
	} else {
		rc = init_sub_crqs(adapter);
	}

	if (rc) {
		dev_err(dev, "Initialization of sub crqs failed\n");
		release_crq_queue(adapter);
		return rc;
	}

	rc = init_sub_crq_irqs(adapter);
	if (rc) {
		dev_err(dev, "Failed to initialize sub crq irqs\n");
		release_crq_queue(adapter);
	}

	return rc;
}

static int ibmvnic_init(struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	unsigned long timeout = msecs_to_jiffies(30000);
	int rc;

	adapter->from_passive_init = false;

	adapter->init_done_rc = 0;
	ibmvnic_send_crq_init(adapter);
	if (!wait_for_completion_timeout(&adapter->init_done, timeout)) {
		dev_err(dev, "Initialization sequence timed out\n");
		return -1;
	}

	if (adapter->init_done_rc) {
		release_crq_queue(adapter);
		return adapter->init_done_rc;
	}

	if (adapter->from_passive_init) {
		adapter->state = VNIC_OPEN;
		adapter->from_passive_init = false;
		return -1;
	}

	rc = init_sub_crqs(adapter);
	if (rc) {
		dev_err(dev, "Initialization of sub crqs failed\n");
		release_crq_queue(adapter);
		return rc;
	}

	rc = init_sub_crq_irqs(adapter);
	if (rc) {
		dev_err(dev, "Failed to initialize sub crq irqs\n");
		release_crq_queue(adapter);
	}

	return rc;
}

static struct device_attribute dev_attr_failover;

static int ibmvnic_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	struct ibmvnic_adapter *adapter;
	struct net_device *netdev;
	unsigned char *mac_addr_p;
	int rc;

	dev_dbg(&dev->dev, "entering ibmvnic_probe for UA 0x%x\n",
		dev->unit_address);

	mac_addr_p = (unsigned char *)vio_get_attribute(dev,
							VETH_MAC_ADDR, NULL);
	if (!mac_addr_p) {
		dev_err(&dev->dev,
			"(%s:%3.3d) ERROR: Can't find MAC_ADDR attribute\n",
			__FILE__, __LINE__);
		return 0;
	}

	netdev = alloc_etherdev_mq(sizeof(struct ibmvnic_adapter),
				   IBMVNIC_MAX_QUEUES);
	if (!netdev)
		return -ENOMEM;

	adapter = netdev_priv(netdev);
	adapter->state = VNIC_PROBING;
	dev_set_drvdata(&dev->dev, netdev);
	adapter->vdev = dev;
	adapter->netdev = netdev;

	ether_addr_copy(adapter->mac_addr, mac_addr_p);
	ether_addr_copy(netdev->dev_addr, adapter->mac_addr);
	netdev->irq = dev->irq;
	netdev->netdev_ops = &ibmvnic_netdev_ops;
	netdev->ethtool_ops = &ibmvnic_ethtool_ops;
	SET_NETDEV_DEV(netdev, &dev->dev);

	spin_lock_init(&adapter->stats_lock);

	INIT_WORK(&adapter->ibmvnic_reset, __ibmvnic_reset);
	INIT_LIST_HEAD(&adapter->rwi_list);
	spin_lock_init(&adapter->rwi_lock);
	init_completion(&adapter->init_done);
	adapter->resetting = false;

	adapter->mac_change_pending = false;

	do {
		rc = init_crq_queue(adapter);
		if (rc) {
			dev_err(&dev->dev, "Couldn't initialize crq. rc=%d\n",
				rc);
			goto ibmvnic_init_fail;
		}

		rc = ibmvnic_init(adapter);
		if (rc && rc != EAGAIN)
			goto ibmvnic_init_fail;
	} while (rc == EAGAIN);

	rc = init_stats_buffers(adapter);
	if (rc)
		goto ibmvnic_init_fail;

	rc = init_stats_token(adapter);
	if (rc)
		goto ibmvnic_stats_fail;

	netdev->mtu = adapter->req_mtu - ETH_HLEN;
	netdev->min_mtu = adapter->min_mtu - ETH_HLEN;
	netdev->max_mtu = adapter->max_mtu - ETH_HLEN;

	rc = device_create_file(&dev->dev, &dev_attr_failover);
	if (rc)
		goto ibmvnic_dev_file_err;

	netif_carrier_off(netdev);
	rc = register_netdev(netdev);
	if (rc) {
		dev_err(&dev->dev, "failed to register netdev rc=%d\n", rc);
		goto ibmvnic_register_fail;
	}
	dev_info(&dev->dev, "ibmvnic registered\n");

	adapter->state = VNIC_PROBED;

	adapter->wait_for_reset = false;

	return 0;

ibmvnic_register_fail:
	device_remove_file(&dev->dev, &dev_attr_failover);

ibmvnic_dev_file_err:
	release_stats_token(adapter);

ibmvnic_stats_fail:
	release_stats_buffers(adapter);

ibmvnic_init_fail:
	release_sub_crqs(adapter, 1);
	release_crq_queue(adapter);
	free_netdev(netdev);

	return rc;
}

static int ibmvnic_remove(struct vio_dev *dev)
{
	struct net_device *netdev = dev_get_drvdata(&dev->dev);
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	adapter->state = VNIC_REMOVING;
	rtnl_lock();
	unregister_netdevice(netdev);

	release_resources(adapter);
	release_sub_crqs(adapter, 1);
	release_crq_queue(adapter);

	release_stats_token(adapter);
	release_stats_buffers(adapter);

	adapter->state = VNIC_REMOVED;

	rtnl_unlock();
	device_remove_file(&dev->dev, &dev_attr_failover);
	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static ssize_t failover_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	__be64 session_token;
	long rc;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	rc = plpar_hcall(H_VIOCTL, retbuf, adapter->vdev->unit_address,
			 H_GET_SESSION_TOKEN, 0, 0, 0);
	if (rc) {
		netdev_err(netdev, "Couldn't retrieve session token, rc %ld\n",
			   rc);
		return -EINVAL;
	}

	session_token = (__be64)retbuf[0];
	netdev_dbg(netdev, "Initiating client failover, session id %llx\n",
		   be64_to_cpu(session_token));
	rc = plpar_hcall_norets(H_VIOCTL, adapter->vdev->unit_address,
				H_SESSION_ERR_DETECTED, session_token, 0, 0);
	if (rc) {
		netdev_err(netdev, "Client initiated failover failed, rc %ld\n",
			   rc);
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_WO(failover);

static unsigned long ibmvnic_get_desired_dma(struct vio_dev *vdev)
{
	struct net_device *netdev = dev_get_drvdata(&vdev->dev);
	struct ibmvnic_adapter *adapter;
	struct iommu_table *tbl;
	unsigned long ret = 0;
	int i;

	tbl = get_iommu_table_base(&vdev->dev);

	/* netdev inits at probe time along with the structures we need below*/
	if (!netdev)
		return IOMMU_PAGE_ALIGN(IBMVNIC_IO_ENTITLEMENT_DEFAULT, tbl);

	adapter = netdev_priv(netdev);

	ret += PAGE_SIZE; /* the crq message queue */
	ret += IOMMU_PAGE_ALIGN(sizeof(struct ibmvnic_statistics), tbl);

	for (i = 0; i < adapter->req_tx_queues + adapter->req_rx_queues; i++)
		ret += 4 * PAGE_SIZE; /* the scrq message queue */

	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	     i++)
		ret += adapter->rx_pool[i].size *
		    IOMMU_PAGE_ALIGN(adapter->rx_pool[i].buff_size, tbl);

	return ret;
}

static int ibmvnic_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	if (adapter->state != VNIC_OPEN)
		return 0;

	tasklet_schedule(&adapter->tasklet);

	return 0;
}

static const struct vio_device_id ibmvnic_device_table[] = {
	{"network", "IBM,vnic"},
	{"", "" }
};
MODULE_DEVICE_TABLE(vio, ibmvnic_device_table);

static const struct dev_pm_ops ibmvnic_pm_ops = {
	.resume = ibmvnic_resume
};

static struct vio_driver ibmvnic_driver = {
	.id_table       = ibmvnic_device_table,
	.probe          = ibmvnic_probe,
	.remove         = ibmvnic_remove,
	.get_desired_dma = ibmvnic_get_desired_dma,
	.name		= ibmvnic_driver_name,
	.pm		= &ibmvnic_pm_ops,
};

/* module functions */
static int __init ibmvnic_module_init(void)
{
	pr_info("%s: %s %s\n", ibmvnic_driver_name, ibmvnic_driver_string,
		IBMVNIC_DRIVER_VERSION);

	return vio_register_driver(&ibmvnic_driver);
}

static void __exit ibmvnic_module_exit(void)
{
	vio_unregister_driver(&ibmvnic_driver);
}

module_init(ibmvnic_module_init);
module_exit(ibmvnic_module_exit);
