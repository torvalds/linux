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
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <net/net_namespace.h>
#include <asm/hvcall.h>
#include <linux/atomic.h>
#include <asm/vio.h>
#include <asm/iommu.h>
#include <linux/uaccess.h>
#include <asm/firmware.h>
#include <linux/seq_file.h>

#include "ibmvnic.h"

static const char ibmvnic_driver_name[] = "ibmvnic";
static const char ibmvnic_driver_string[] = "IBM System i/p Virtual NIC Driver";

MODULE_AUTHOR("Santiago Leon <santi_leon@yahoo.com>");
MODULE_DESCRIPTION("IBM System i/p Virtual NIC Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVNIC_DRIVER_VERSION);

static int ibmvnic_version = IBMVNIC_INITIAL_VERSION;
static int ibmvnic_remove(struct vio_dev *);
static void release_sub_crqs(struct ibmvnic_adapter *);
static int ibmvnic_reset_crq(struct ibmvnic_adapter *);
static int ibmvnic_send_crq_init(struct ibmvnic_adapter *);
static int ibmvnic_reenable_crq_queue(struct ibmvnic_adapter *);
static int ibmvnic_send_crq(struct ibmvnic_adapter *, union ibmvnic_crq *);
static int send_subcrq(struct ibmvnic_adapter *adapter, u64 remote_handle,
		       union sub_crq *sub_crq);
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
static void send_request_map(struct ibmvnic_adapter *, dma_addr_t, __be32, u8);
static void send_request_unmap(struct ibmvnic_adapter *, u8);

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

/* net_device_ops functions */

static void init_rx_pool(struct ibmvnic_adapter *adapter,
			 struct ibmvnic_rx_pool *rx_pool, int num, int index,
			 int buff_size, int active)
{
	netdev_dbg(adapter->netdev,
		   "Initializing rx_pool %d, %d buffs, %d bytes each\n",
		   index, num, buff_size);
	rx_pool->size = num;
	rx_pool->index = index;
	rx_pool->buff_size = buff_size;
	rx_pool->active = active;
}

static int alloc_long_term_buff(struct ibmvnic_adapter *adapter,
				struct ibmvnic_long_term_buff *ltb, int size)
{
	struct device *dev = &adapter->vdev->dev;

	ltb->size = size;
	ltb->buff = dma_alloc_coherent(dev, ltb->size, &ltb->addr,
				       GFP_KERNEL);

	if (!ltb->buff) {
		dev_err(dev, "Couldn't alloc long term buffer\n");
		return -ENOMEM;
	}
	ltb->map_id = adapter->map_id;
	adapter->map_id++;
	send_request_map(adapter, ltb->addr,
			 ltb->size, ltb->map_id);
	init_completion(&adapter->fw_done);
	wait_for_completion(&adapter->fw_done);
	return 0;
}

static void free_long_term_buff(struct ibmvnic_adapter *adapter,
				struct ibmvnic_long_term_buff *ltb)
{
	struct device *dev = &adapter->vdev->dev;

	dma_free_coherent(dev, ltb->size, ltb->buff, ltb->addr);
	send_request_unmap(adapter, ltb->map_id);
}

static int alloc_rx_pool(struct ibmvnic_adapter *adapter,
			 struct ibmvnic_rx_pool *pool)
{
	struct device *dev = &adapter->vdev->dev;
	int i;

	pool->free_map = kcalloc(pool->size, sizeof(int), GFP_KERNEL);
	if (!pool->free_map)
		return -ENOMEM;

	pool->rx_buff = kcalloc(pool->size, sizeof(struct ibmvnic_rx_buff),
				GFP_KERNEL);

	if (!pool->rx_buff) {
		dev_err(dev, "Couldn't alloc rx buffers\n");
		kfree(pool->free_map);
		return -ENOMEM;
	}

	if (alloc_long_term_buff(adapter, &pool->long_term_buff,
				 pool->size * pool->buff_size)) {
		kfree(pool->free_map);
		kfree(pool->rx_buff);
		return -ENOMEM;
	}

	for (i = 0; i < pool->size; ++i)
		pool->free_map[i] = i;

	atomic_set(&pool->available, 0);
	pool->next_alloc = 0;
	pool->next_free = 0;

	return 0;
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
	dev_info(dev, "replenish pools failure\n");
	pool->free_map[pool->next_free] = index;
	pool->rx_buff[index].skb = NULL;
	if (!dma_mapping_error(dev, dma_addr))
		dma_unmap_single(dev, dma_addr, pool->buff_size,
				 DMA_FROM_DEVICE);

	dev_kfree_skb_any(skb);
	adapter->replenish_add_buff_failure++;
	atomic_add(buffers_added, &pool->available);
}

static void replenish_pools(struct ibmvnic_adapter *adapter)
{
	int i;

	if (adapter->migrated)
		return;

	adapter->replenish_task_cycles++;
	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	     i++) {
		if (adapter->rx_pool[i].active)
			replenish_rx_pool(adapter, &adapter->rx_pool[i]);
	}
}

static void free_rx_pool(struct ibmvnic_adapter *adapter,
			 struct ibmvnic_rx_pool *pool)
{
	int i;

	kfree(pool->free_map);
	pool->free_map = NULL;

	if (!pool->rx_buff)
		return;

	for (i = 0; i < pool->size; i++) {
		if (pool->rx_buff[i].skb) {
			dev_kfree_skb_any(pool->rx_buff[i].skb);
			pool->rx_buff[i].skb = NULL;
		}
	}
	kfree(pool->rx_buff);
	pool->rx_buff = NULL;
}

static int ibmvnic_open(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_tx_pool *tx_pool;
	union ibmvnic_crq crq;
	int rxadd_subcrqs;
	u64 *size_array;
	int tx_subcrqs;
	int i, j;

	rxadd_subcrqs =
	    be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	tx_subcrqs =
	    be32_to_cpu(adapter->login_rsp_buf->num_txsubm_subcrqs);
	size_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
				  be32_to_cpu(adapter->login_rsp_buf->
					      off_rxadd_buff_size));
	adapter->map_id = 1;
	adapter->napi = kcalloc(adapter->req_rx_queues,
				sizeof(struct napi_struct), GFP_KERNEL);
	if (!adapter->napi)
		goto alloc_napi_failed;
	for (i = 0; i < adapter->req_rx_queues; i++) {
		netif_napi_add(netdev, &adapter->napi[i], ibmvnic_poll,
			       NAPI_POLL_WEIGHT);
		napi_enable(&adapter->napi[i]);
	}
	adapter->rx_pool =
	    kcalloc(rxadd_subcrqs, sizeof(struct ibmvnic_rx_pool), GFP_KERNEL);

	if (!adapter->rx_pool)
		goto rx_pool_arr_alloc_failed;
	send_map_query(adapter);
	for (i = 0; i < rxadd_subcrqs; i++) {
		init_rx_pool(adapter, &adapter->rx_pool[i],
			     IBMVNIC_BUFFS_PER_POOL, i,
			     be64_to_cpu(size_array[i]), 1);
		if (alloc_rx_pool(adapter, &adapter->rx_pool[i])) {
			dev_err(dev, "Couldn't alloc rx pool\n");
			goto rx_pool_alloc_failed;
		}
	}
	adapter->tx_pool =
	    kcalloc(tx_subcrqs, sizeof(struct ibmvnic_tx_pool), GFP_KERNEL);

	if (!adapter->tx_pool)
		goto tx_pool_arr_alloc_failed;
	for (i = 0; i < tx_subcrqs; i++) {
		tx_pool = &adapter->tx_pool[i];
		tx_pool->tx_buff =
		    kcalloc(adapter->max_tx_entries_per_subcrq,
			    sizeof(struct ibmvnic_tx_buff), GFP_KERNEL);
		if (!tx_pool->tx_buff)
			goto tx_pool_alloc_failed;

		if (alloc_long_term_buff(adapter, &tx_pool->long_term_buff,
					 adapter->max_tx_entries_per_subcrq *
					 adapter->req_mtu))
			goto tx_ltb_alloc_failed;

		tx_pool->free_map =
		    kcalloc(adapter->max_tx_entries_per_subcrq,
			    sizeof(int), GFP_KERNEL);
		if (!tx_pool->free_map)
			goto tx_fm_alloc_failed;

		for (j = 0; j < adapter->max_tx_entries_per_subcrq; j++)
			tx_pool->free_map[j] = j;

		tx_pool->consumer_index = 0;
		tx_pool->producer_index = 0;
	}
	adapter->bounce_buffer_size =
	    (netdev->mtu + ETH_HLEN - 1) / PAGE_SIZE + 1;
	adapter->bounce_buffer = kmalloc(adapter->bounce_buffer_size,
					 GFP_KERNEL);
	if (!adapter->bounce_buffer)
		goto bounce_alloc_failed;

	adapter->bounce_buffer_dma = dma_map_single(dev, adapter->bounce_buffer,
						    adapter->bounce_buffer_size,
						    DMA_TO_DEVICE);
	if (dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
		dev_err(dev, "Couldn't map tx bounce buffer\n");
		goto bounce_map_failed;
	}
	replenish_pools(adapter);

	/* We're ready to receive frames, enable the sub-crq interrupts and
	 * set the logical link state to up
	 */
	for (i = 0; i < adapter->req_rx_queues; i++)
		enable_scrq_irq(adapter, adapter->rx_scrq[i]);

	for (i = 0; i < adapter->req_tx_queues; i++)
		enable_scrq_irq(adapter, adapter->tx_scrq[i]);

	memset(&crq, 0, sizeof(crq));
	crq.logical_link_state.first = IBMVNIC_CRQ_CMD;
	crq.logical_link_state.cmd = LOGICAL_LINK_STATE;
	crq.logical_link_state.link_state = IBMVNIC_LOGICAL_LNK_UP;
	ibmvnic_send_crq(adapter, &crq);

	netif_start_queue(netdev);
	return 0;

bounce_map_failed:
	kfree(adapter->bounce_buffer);
bounce_alloc_failed:
	i = tx_subcrqs - 1;
	kfree(adapter->tx_pool[i].free_map);
tx_fm_alloc_failed:
	free_long_term_buff(adapter, &adapter->tx_pool[i].long_term_buff);
tx_ltb_alloc_failed:
	kfree(adapter->tx_pool[i].tx_buff);
tx_pool_alloc_failed:
	for (j = 0; j < i; j++) {
		kfree(adapter->tx_pool[j].tx_buff);
		free_long_term_buff(adapter,
				    &adapter->tx_pool[j].long_term_buff);
		kfree(adapter->tx_pool[j].free_map);
	}
	kfree(adapter->tx_pool);
	adapter->tx_pool = NULL;
tx_pool_arr_alloc_failed:
	i = rxadd_subcrqs;
rx_pool_alloc_failed:
	for (j = 0; j < i; j++) {
		free_rx_pool(adapter, &adapter->rx_pool[j]);
		free_long_term_buff(adapter,
				    &adapter->rx_pool[j].long_term_buff);
	}
	kfree(adapter->rx_pool);
	adapter->rx_pool = NULL;
rx_pool_arr_alloc_failed:
	for (i = 0; i < adapter->req_rx_queues; i++)
		napi_enable(&adapter->napi[i]);
alloc_napi_failed:
	return -ENOMEM;
}

static int ibmvnic_close(struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq crq;
	int i;

	adapter->closing = true;

	for (i = 0; i < adapter->req_rx_queues; i++)
		napi_disable(&adapter->napi[i]);

	netif_stop_queue(netdev);

	if (adapter->bounce_buffer) {
		if (!dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
			dma_unmap_single(&adapter->vdev->dev,
					 adapter->bounce_buffer_dma,
					 adapter->bounce_buffer_size,
					 DMA_BIDIRECTIONAL);
			adapter->bounce_buffer_dma = DMA_ERROR_CODE;
		}
		kfree(adapter->bounce_buffer);
		adapter->bounce_buffer = NULL;
	}

	memset(&crq, 0, sizeof(crq));
	crq.logical_link_state.first = IBMVNIC_CRQ_CMD;
	crq.logical_link_state.cmd = LOGICAL_LINK_STATE;
	crq.logical_link_state.link_state = IBMVNIC_LOGICAL_LNK_DN;
	ibmvnic_send_crq(adapter, &crq);

	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_txsubm_subcrqs);
	     i++) {
		kfree(adapter->tx_pool[i].tx_buff);
		free_long_term_buff(adapter,
				    &adapter->tx_pool[i].long_term_buff);
		kfree(adapter->tx_pool[i].free_map);
	}
	kfree(adapter->tx_pool);
	adapter->tx_pool = NULL;

	for (i = 0; i < be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);
	     i++) {
		free_rx_pool(adapter, &adapter->rx_pool[i]);
		free_long_term_buff(adapter,
				    &adapter->rx_pool[i].long_term_buff);
	}
	kfree(adapter->rx_pool);
	adapter->rx_pool = NULL;

	adapter->closing = false;

	return 0;
}

static int ibmvnic_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	int queue_num = skb_get_queue_mapping(skb);
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_tx_buff *tx_buff = NULL;
	struct ibmvnic_tx_pool *tx_pool;
	unsigned int tx_send_failed = 0;
	unsigned int tx_map_failed = 0;
	unsigned int tx_dropped = 0;
	unsigned int tx_packets = 0;
	unsigned int tx_bytes = 0;
	dma_addr_t data_dma_addr;
	struct netdev_queue *txq;
	bool used_bounce = false;
	unsigned long lpar_rc;
	union sub_crq tx_crq;
	unsigned int offset;
	unsigned char *dst;
	u64 *handle_array;
	int index = 0;
	int ret = 0;

	tx_pool = &adapter->tx_pool[queue_num];
	txq = netdev_get_tx_queue(netdev, skb_get_queue_mapping(skb));
	handle_array = (u64 *)((u8 *)(adapter->login_rsp_buf) +
				   be32_to_cpu(adapter->login_rsp_buf->
					       off_txsubm_subcrqs));
	if (adapter->migrated) {
		tx_send_failed++;
		tx_dropped++;
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	index = tx_pool->free_map[tx_pool->consumer_index];
	offset = index * adapter->req_mtu;
	dst = tx_pool->long_term_buff.buff + offset;
	memset(dst, 0, adapter->req_mtu);
	skb_copy_from_linear_data(skb, dst, skb->len);
	data_dma_addr = tx_pool->long_term_buff.addr + offset;

	tx_pool->consumer_index =
	    (tx_pool->consumer_index + 1) %
		adapter->max_tx_entries_per_subcrq;

	tx_buff = &tx_pool->tx_buff[index];
	tx_buff->skb = skb;
	tx_buff->data_dma[0] = data_dma_addr;
	tx_buff->data_len[0] = skb->len;
	tx_buff->index = index;
	tx_buff->pool_index = queue_num;
	tx_buff->last_frag = true;
	tx_buff->used_bounce = used_bounce;

	memset(&tx_crq, 0, sizeof(tx_crq));
	tx_crq.v1.first = IBMVNIC_CRQ_CMD;
	tx_crq.v1.type = IBMVNIC_TX_DESC;
	tx_crq.v1.n_crq_elem = 1;
	tx_crq.v1.n_sge = 1;
	tx_crq.v1.flags1 = IBMVNIC_TX_COMP_NEEDED;
	tx_crq.v1.correlator = cpu_to_be32(index);
	tx_crq.v1.dma_reg = cpu_to_be16(tx_pool->long_term_buff.map_id);
	tx_crq.v1.sge_len = cpu_to_be32(skb->len);
	tx_crq.v1.ioba = cpu_to_be64(data_dma_addr);

	if (adapter->vlan_header_insertion) {
		tx_crq.v1.flags2 |= IBMVNIC_TX_VLAN_INSERT;
		tx_crq.v1.vlan_id = cpu_to_be16(skb->vlan_tci);
	}

	if (skb->protocol == htons(ETH_P_IP)) {
		if (ip_hdr(skb)->version == 4)
			tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_IPV4;
		else if (ip_hdr(skb)->version == 6)
			tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_IPV6;

		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_TCP;
		else if (ip_hdr(skb)->protocol != IPPROTO_TCP)
			tx_crq.v1.flags1 |= IBMVNIC_TX_PROT_UDP;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		tx_crq.v1.flags1 |= IBMVNIC_TX_CHKSUM_OFFLOAD;

	lpar_rc = send_subcrq(adapter, handle_array[0], &tx_crq);

	if (lpar_rc != H_SUCCESS) {
		dev_err(dev, "tx failed with code %ld\n", lpar_rc);

		if (tx_pool->consumer_index == 0)
			tx_pool->consumer_index =
				adapter->max_tx_entries_per_subcrq - 1;
		else
			tx_pool->consumer_index--;

		tx_send_failed++;
		tx_dropped++;
		ret = NETDEV_TX_BUSY;
		goto out;
	}
	tx_packets++;
	tx_bytes += skb->len;
	txq->trans_start = jiffies;
	ret = NETDEV_TX_OK;

out:
	netdev->stats.tx_dropped += tx_dropped;
	netdev->stats.tx_bytes += tx_bytes;
	netdev->stats.tx_packets += tx_packets;
	adapter->tx_send_failed += tx_send_failed;
	adapter->tx_map_failed += tx_map_failed;

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

static int ibmvnic_set_mac(struct net_device *netdev, void *p)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	union ibmvnic_crq crq;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memset(&crq, 0, sizeof(crq));
	crq.change_mac_addr.first = IBMVNIC_CRQ_CMD;
	crq.change_mac_addr.cmd = CHANGE_MAC_ADDR;
	ether_addr_copy(&crq.change_mac_addr.mac_addr[0], addr->sa_data);
	ibmvnic_send_crq(adapter, &crq);
	/* netdev->dev_addr is changed in handle_change_mac_rsp function */
	return 0;
}

static int ibmvnic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	if (new_mtu > adapter->req_mtu || new_mtu < adapter->min_mtu)
		return -EINVAL;

	netdev->mtu = new_mtu;
	return 0;
}

static void ibmvnic_tx_timeout(struct net_device *dev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);
	int rc;

	/* Adapter timed out, resetting it */
	release_sub_crqs(adapter);
	rc = ibmvnic_reset_crq(adapter);
	if (rc)
		dev_err(&adapter->vdev->dev, "Adapter timeout, reset failed\n");
	else
		ibmvnic_send_crq_init(adapter);
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

		if (!pending_scrq(adapter, adapter->rx_scrq[scrq_num]))
			break;
		next = ibmvnic_next_scrq(adapter, adapter->rx_scrq[scrq_num]);
		rx_buff =
		    (struct ibmvnic_rx_buff *)be64_to_cpu(next->
							  rx_comp.correlator);
		/* do error checking */
		if (next->rx_comp.rc) {
			netdev_err(netdev, "rx error %x\n", next->rx_comp.rc);
			/* free the entry */
			next->rx_comp.first = 0;
			remove_buff_from_pool(adapter, rx_buff);
			break;
		}

		length = be32_to_cpu(next->rx_comp.len);
		offset = be16_to_cpu(next->rx_comp.off_frame_data);
		flags = next->rx_comp.flags;
		skb = rx_buff->skb;
		skb_copy_to_linear_data(skb, rx_buff->data + offset,
					length);
		skb->vlan_tci = be16_to_cpu(next->rx_comp.vlan_tci);
		/* free the entry */
		next->rx_comp.first = 0;
		remove_buff_from_pool(adapter, rx_buff);

		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, netdev);

		if (flags & IBMVNIC_IP_CHKSUM_GOOD &&
		    flags & IBMVNIC_TCP_UDP_CHKSUM_GOOD) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

		length = skb->len;
		napi_gro_receive(napi, skb); /* send it up */
		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += length;
		frames_processed++;
	}
	replenish_pools(adapter);

	if (frames_processed < budget) {
		enable_scrq_irq(adapter, adapter->rx_scrq[scrq_num]);
		napi_complete(napi);
		if (pending_scrq(adapter, adapter->rx_scrq[scrq_num]) &&
		    napi_reschedule(napi)) {
			disable_scrq_irq(adapter, adapter->rx_scrq[scrq_num]);
			goto restart_poll;
		}
	}
	return frames_processed;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ibmvnic_netpoll_controller(struct net_device *dev)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);
	int i;

	replenish_pools(netdev_priv(dev));
	for (i = 0; i < adapter->req_rx_queues; i++)
		ibmvnic_interrupt_rx(adapter->rx_scrq[i]->irq,
				     adapter->rx_scrq[i]);
}
#endif

static const struct net_device_ops ibmvnic_netdev_ops = {
	.ndo_open		= ibmvnic_open,
	.ndo_stop		= ibmvnic_close,
	.ndo_start_xmit		= ibmvnic_xmit,
	.ndo_set_rx_mode	= ibmvnic_set_multi,
	.ndo_set_mac_address	= ibmvnic_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= ibmvnic_change_mtu,
	.ndo_tx_timeout		= ibmvnic_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ibmvnic_netpoll_controller,
#endif
};

/* ethtool functions */

static int ibmvnic_get_settings(struct net_device *netdev,
				struct ethtool_cmd *cmd)
{
	cmd->supported = (SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg |
			  SUPPORTED_FIBRE);
	cmd->advertising = (ADVERTISED_1000baseT_Full | ADVERTISED_Autoneg |
			    ADVERTISED_FIBRE);
	ethtool_cmd_speed_set(cmd, SPEED_1000);
	cmd->duplex = DUPLEX_FULL;
	cmd->port = PORT_FIBRE;
	cmd->phy_address = 0;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = AUTONEG_ENABLE;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 1;
	return 0;
}

static void ibmvnic_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, ibmvnic_driver_name, sizeof(info->driver));
	strlcpy(info->version, IBMVNIC_DRIVER_VERSION, sizeof(info->version));
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
	ring->rx_max_pending = 0;
	ring->tx_max_pending = 0;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = 0;
	ring->tx_pending = 0;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static void ibmvnic_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ibmvnic_stats); i++, data += ETH_GSTRING_LEN)
		memcpy(data, ibmvnic_stats[i].name, ETH_GSTRING_LEN);
}

static int ibmvnic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ibmvnic_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void ibmvnic_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct ibmvnic_adapter *adapter = netdev_priv(dev);
	union ibmvnic_crq crq;
	int i;

	memset(&crq, 0, sizeof(crq));
	crq.request_statistics.first = IBMVNIC_CRQ_CMD;
	crq.request_statistics.cmd = REQUEST_STATISTICS;
	crq.request_statistics.ioba = cpu_to_be32(adapter->stats_token);
	crq.request_statistics.len =
	    cpu_to_be32(sizeof(struct ibmvnic_statistics));
	ibmvnic_send_crq(adapter, &crq);

	/* Wait for data to be written */
	init_completion(&adapter->stats_done);
	wait_for_completion(&adapter->stats_done);

	for (i = 0; i < ARRAY_SIZE(ibmvnic_stats); i++)
		data[i] = IBMVNIC_GET_STAT(adapter, ibmvnic_stats[i].offset);
}

static const struct ethtool_ops ibmvnic_ethtool_ops = {
	.get_settings		= ibmvnic_get_settings,
	.get_drvinfo		= ibmvnic_get_drvinfo,
	.get_msglevel		= ibmvnic_get_msglevel,
	.set_msglevel		= ibmvnic_set_msglevel,
	.get_link		= ibmvnic_get_link,
	.get_ringparam		= ibmvnic_get_ringparam,
	.get_strings            = ibmvnic_get_strings,
	.get_sset_count         = ibmvnic_get_sset_count,
	.get_ethtool_stats	= ibmvnic_get_ethtool_stats,
};

/* Routines for managing CRQs/sCRQs  */

static void release_sub_crq_queue(struct ibmvnic_adapter *adapter,
				  struct ibmvnic_sub_crq_queue *scrq)
{
	struct device *dev = &adapter->vdev->dev;
	long rc;

	netdev_dbg(adapter->netdev, "Releasing sub-CRQ\n");

	/* Close the sub-crqs */
	do {
		rc = plpar_hcall_norets(H_FREE_SUB_CRQ,
					adapter->vdev->unit_address,
					scrq->crq_num);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

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

	scrq = kmalloc(sizeof(*scrq), GFP_ATOMIC);
	if (!scrq)
		return NULL;

	scrq->msgs = (union sub_crq *)__get_free_pages(GFP_KERNEL, 2);
	memset(scrq->msgs, 0, 4 * PAGE_SIZE);
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

	scrq->irq = irq_create_mapping(NULL, scrq->hw_irq);
	if (scrq->irq == NO_IRQ) {
		dev_err(dev, "Error mapping irq\n");
		goto map_irq_failed;
	}

	scrq->adapter = adapter;
	scrq->size = 4 * PAGE_SIZE / sizeof(*scrq->msgs);
	scrq->cur = 0;
	scrq->rx_skb_top = NULL;
	spin_lock_init(&scrq->lock);

	netdev_dbg(adapter->netdev,
		   "sub-crq initialized, num %lx, hw_irq=%lx, irq=%x\n",
		   scrq->crq_num, scrq->hw_irq, scrq->irq);

	return scrq;

map_irq_failed:
	do {
		rc = plpar_hcall_norets(H_FREE_SUB_CRQ,
					adapter->vdev->unit_address,
					scrq->crq_num);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));
reg_failed:
	dma_unmap_single(dev, scrq->msg_token, 4 * PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
map_failed:
	free_pages((unsigned long)scrq->msgs, 2);
zero_page_failed:
	kfree(scrq);

	return NULL;
}

static void release_sub_crqs(struct ibmvnic_adapter *adapter)
{
	int i;

	if (adapter->tx_scrq) {
		for (i = 0; i < adapter->req_tx_queues; i++)
			if (adapter->tx_scrq[i]) {
				free_irq(adapter->tx_scrq[i]->irq,
					 adapter->tx_scrq[i]);
				release_sub_crq_queue(adapter,
						      adapter->tx_scrq[i]);
			}
		adapter->tx_scrq = NULL;
	}

	if (adapter->rx_scrq) {
		for (i = 0; i < adapter->req_rx_queues; i++)
			if (adapter->rx_scrq[i]) {
				free_irq(adapter->rx_scrq[i]->irq,
					 adapter->rx_scrq[i]);
				release_sub_crq_queue(adapter,
						      adapter->rx_scrq[i]);
			}
		adapter->rx_scrq = NULL;
	}

	adapter->requested_caps = 0;
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
	struct ibmvnic_tx_buff *txbuff;
	union sub_crq *next;
	int index;
	int i, j;

restart_loop:
	while (pending_scrq(adapter, scrq)) {
		unsigned int pool = scrq->pool_index;

		next = ibmvnic_next_scrq(adapter, scrq);
		for (i = 0; i < next->tx_comp.num_comps; i++) {
			if (next->tx_comp.rcs[i]) {
				dev_err(dev, "tx error %x\n",
					next->tx_comp.rcs[i]);
				continue;
			}
			index = be32_to_cpu(next->tx_comp.correlators[i]);
			txbuff = &adapter->tx_pool[pool].tx_buff[index];

			for (j = 0; j < IBMVNIC_MAX_FRAGS_PER_CRQ; j++) {
				if (!txbuff->data_dma[j])
					continue;

				txbuff->data_dma[j] = 0;
				txbuff->used_bounce = false;
			}

			if (txbuff->last_frag)
				dev_kfree_skb_any(txbuff->skb);

			adapter->tx_pool[pool].free_map[adapter->tx_pool[pool].
						     producer_index] = index;
			adapter->tx_pool[pool].producer_index =
			    (adapter->tx_pool[pool].producer_index + 1) %
			    adapter->max_tx_entries_per_subcrq;
		}
		/* remove tx_comp scrq*/
		next->tx_comp.first = 0;
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

	if (napi_schedule_prep(&adapter->napi[scrq->scrq_num])) {
		disable_scrq_irq(adapter, scrq);
		__napi_schedule(&adapter->napi[scrq->scrq_num]);
	}

	return IRQ_HANDLED;
}

static void init_sub_crqs(struct ibmvnic_adapter *adapter, int retry)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_sub_crq_queue **allqueues;
	int registered_queues = 0;
	union ibmvnic_crq crq;
	int total_queues;
	int more = 0;
	int i, j;
	int rc;

	if (!retry) {
		/* Sub-CRQ entries are 32 byte long */
		int entries_page = 4 * PAGE_SIZE / (sizeof(u64) * 4);

		if (adapter->min_tx_entries_per_subcrq > entries_page ||
		    adapter->min_rx_add_entries_per_subcrq > entries_page) {
			dev_err(dev, "Fatal, invalid entries per sub-crq\n");
			goto allqueues_failed;
		}

		/* Get the minimum between the queried max and the entries
		 * that fit in our PAGE_SIZE
		 */
		adapter->req_tx_entries_per_subcrq =
		    adapter->max_tx_entries_per_subcrq > entries_page ?
		    entries_page : adapter->max_tx_entries_per_subcrq;
		adapter->req_rx_add_entries_per_subcrq =
		    adapter->max_rx_add_entries_per_subcrq > entries_page ?
		    entries_page : adapter->max_rx_add_entries_per_subcrq;

		/* Choosing the maximum number of queues supported by firmware*/
		adapter->req_tx_queues = adapter->min_tx_queues;
		adapter->req_rx_queues = adapter->min_rx_queues;
		adapter->req_rx_add_queues = adapter->min_rx_add_queues;

		adapter->req_mtu = adapter->max_mtu;
	}

	total_queues = adapter->req_tx_queues + adapter->req_rx_queues;

	allqueues = kcalloc(total_queues, sizeof(*allqueues), GFP_ATOMIC);
	if (!allqueues)
		goto allqueues_failed;

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
				   sizeof(*adapter->tx_scrq), GFP_ATOMIC);
	if (!adapter->tx_scrq)
		goto tx_failed;

	for (i = 0; i < adapter->req_tx_queues; i++) {
		adapter->tx_scrq[i] = allqueues[i];
		adapter->tx_scrq[i]->pool_index = i;
		rc = request_irq(adapter->tx_scrq[i]->irq, ibmvnic_interrupt_tx,
				 0, "ibmvnic_tx", adapter->tx_scrq[i]);
		if (rc) {
			dev_err(dev, "Couldn't register tx irq 0x%x. rc=%d\n",
				adapter->tx_scrq[i]->irq, rc);
			goto req_tx_irq_failed;
		}
	}

	adapter->rx_scrq = kcalloc(adapter->req_rx_queues,
				   sizeof(*adapter->rx_scrq), GFP_ATOMIC);
	if (!adapter->rx_scrq)
		goto rx_failed;

	for (i = 0; i < adapter->req_rx_queues; i++) {
		adapter->rx_scrq[i] = allqueues[i + adapter->req_tx_queues];
		adapter->rx_scrq[i]->scrq_num = i;
		rc = request_irq(adapter->rx_scrq[i]->irq, ibmvnic_interrupt_rx,
				 0, "ibmvnic_rx", adapter->rx_scrq[i]);
		if (rc) {
			dev_err(dev, "Couldn't register rx irq 0x%x. rc=%d\n",
				adapter->rx_scrq[i]->irq, rc);
			goto req_rx_irq_failed;
		}
	}

	memset(&crq, 0, sizeof(crq));
	crq.request_capability.first = IBMVNIC_CRQ_CMD;
	crq.request_capability.cmd = REQUEST_CAPABILITY;

	crq.request_capability.capability = cpu_to_be16(REQ_TX_QUEUES);
	crq.request_capability.number = cpu_to_be32(adapter->req_tx_queues);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_RX_QUEUES);
	crq.request_capability.number = cpu_to_be32(adapter->req_rx_queues);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_RX_ADD_QUEUES);
	crq.request_capability.number = cpu_to_be32(adapter->req_rx_add_queues);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability =
	    cpu_to_be16(REQ_TX_ENTRIES_PER_SUBCRQ);
	crq.request_capability.number =
	    cpu_to_be32(adapter->req_tx_entries_per_subcrq);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability =
	    cpu_to_be16(REQ_RX_ADD_ENTRIES_PER_SUBCRQ);
	crq.request_capability.number =
	    cpu_to_be32(adapter->req_rx_add_entries_per_subcrq);
	ibmvnic_send_crq(adapter, &crq);

	crq.request_capability.capability = cpu_to_be16(REQ_MTU);
	crq.request_capability.number = cpu_to_be32(adapter->req_mtu);
	ibmvnic_send_crq(adapter, &crq);

	if (adapter->netdev->flags & IFF_PROMISC) {
		if (adapter->promisc_supported) {
			crq.request_capability.capability =
			    cpu_to_be16(PROMISC_REQUESTED);
			crq.request_capability.number = cpu_to_be32(1);
			ibmvnic_send_crq(adapter, &crq);
		}
	} else {
		crq.request_capability.capability =
		    cpu_to_be16(PROMISC_REQUESTED);
		crq.request_capability.number = cpu_to_be32(0);
		ibmvnic_send_crq(adapter, &crq);
	}

	kfree(allqueues);

	return;

req_rx_irq_failed:
	for (j = 0; j < i; j++)
		free_irq(adapter->rx_scrq[j]->irq, adapter->rx_scrq[j]);
	i = adapter->req_tx_queues;
req_tx_irq_failed:
	for (j = 0; j < i; j++)
		free_irq(adapter->tx_scrq[j]->irq, adapter->tx_scrq[j]);
	kfree(adapter->rx_scrq);
	adapter->rx_scrq = NULL;
rx_failed:
	kfree(adapter->tx_scrq);
	adapter->tx_scrq = NULL;
tx_failed:
	for (i = 0; i < registered_queues; i++)
		release_sub_crq_queue(adapter, allqueues[i]);
	kfree(allqueues);
allqueues_failed:
	ibmvnic_remove(adapter->vdev);
}

static int pending_scrq(struct ibmvnic_adapter *adapter,
			struct ibmvnic_sub_crq_queue *scrq)
{
	union sub_crq *entry = &scrq->msgs[scrq->cur];

	if (entry->generic.first & IBMVNIC_CRQ_CMD_RSP || adapter->closing)
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

	if (rc) {
		if (rc == H_CLOSED)
			dev_warn(dev, "CRQ Queue closed\n");
		dev_err(dev, "Send error (rc=%d)\n", rc);
	}

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

	/* Make sure the hypervisor sees the complete request */
	mb();

	rc = plpar_hcall_norets(H_SEND_CRQ, ua,
				cpu_to_be64(u64_crq[0]),
				cpu_to_be64(u64_crq[1]));

	if (rc) {
		if (rc == H_CLOSED)
			dev_warn(dev, "CRQ Queue closed\n");
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

static int ibmvnic_send_crq_init_complete(struct ibmvnic_adapter *adapter)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.generic.first = IBMVNIC_CRQ_INIT_CMD;
	crq.generic.cmd = IBMVNIC_CRQ_INIT_COMPLETE;
	netdev_dbg(adapter->netdev, "Sending CRQ init complete\n");

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

static void send_login(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_login_rsp_buffer *login_rsp_buffer;
	struct ibmvnic_login_buffer *login_buffer;
	struct ibmvnic_inflight_cmd *inflight_cmd;
	struct device *dev = &adapter->vdev->dev;
	dma_addr_t rsp_buffer_token;
	dma_addr_t buffer_token;
	size_t rsp_buffer_size;
	union ibmvnic_crq crq;
	unsigned long flags;
	size_t buffer_size;
	__be64 *tx_list_p;
	__be64 *rx_list_p;
	int i;

	buffer_size =
	    sizeof(struct ibmvnic_login_buffer) +
	    sizeof(u64) * (adapter->req_tx_queues + adapter->req_rx_queues);

	login_buffer = kmalloc(buffer_size, GFP_ATOMIC);
	if (!login_buffer)
		goto buf_alloc_failed;

	buffer_token = dma_map_single(dev, login_buffer, buffer_size,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(dev, buffer_token)) {
		dev_err(dev, "Couldn't map login buffer\n");
		goto buf_map_failed;
	}

	rsp_buffer_size =
	    sizeof(struct ibmvnic_login_rsp_buffer) +
	    sizeof(u64) * (adapter->req_tx_queues +
			   adapter->req_rx_queues *
			   adapter->req_rx_add_queues + adapter->
			   req_rx_add_queues) +
	    sizeof(u8) * (IBMVNIC_TX_DESC_VERSIONS);

	login_rsp_buffer = kmalloc(rsp_buffer_size, GFP_ATOMIC);
	if (!login_rsp_buffer)
		goto buf_rsp_alloc_failed;

	rsp_buffer_token = dma_map_single(dev, login_rsp_buffer,
					  rsp_buffer_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, rsp_buffer_token)) {
		dev_err(dev, "Couldn't map login rsp buffer\n");
		goto buf_rsp_map_failed;
	}
	inflight_cmd = kmalloc(sizeof(*inflight_cmd), GFP_ATOMIC);
	if (!inflight_cmd) {
		dev_err(dev, "Couldn't allocate inflight_cmd\n");
		goto inflight_alloc_failed;
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

	memcpy(&inflight_cmd->crq, &crq, sizeof(crq));

	spin_lock_irqsave(&adapter->inflight_lock, flags);
	list_add_tail(&inflight_cmd->list, &adapter->inflight);
	spin_unlock_irqrestore(&adapter->inflight_lock, flags);

	ibmvnic_send_crq(adapter, &crq);

	return;

inflight_alloc_failed:
	dma_unmap_single(dev, rsp_buffer_token, rsp_buffer_size,
			 DMA_FROM_DEVICE);
buf_rsp_map_failed:
	kfree(login_rsp_buffer);
buf_rsp_alloc_failed:
	dma_unmap_single(dev, buffer_token, buffer_size, DMA_TO_DEVICE);
buf_map_failed:
	kfree(login_buffer);
buf_alloc_failed:
	return;
}

static void send_request_map(struct ibmvnic_adapter *adapter, dma_addr_t addr,
			     u32 len, u8 map_id)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_map.first = IBMVNIC_CRQ_CMD;
	crq.request_map.cmd = REQUEST_MAP;
	crq.request_map.map_id = map_id;
	crq.request_map.ioba = cpu_to_be32(addr);
	crq.request_map.len = cpu_to_be32(len);
	ibmvnic_send_crq(adapter, &crq);
}

static void send_request_unmap(struct ibmvnic_adapter *adapter, u8 map_id)
{
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_unmap.first = IBMVNIC_CRQ_CMD;
	crq.request_unmap.cmd = REQUEST_UNMAP;
	crq.request_unmap.map_id = map_id;
	ibmvnic_send_crq(adapter, &crq);
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

	atomic_set(&adapter->running_cap_queries, 0);
	memset(&crq, 0, sizeof(crq));
	crq.query_capability.first = IBMVNIC_CRQ_CMD;
	crq.query_capability.cmd = QUERY_CAPABILITY;

	crq.query_capability.capability = cpu_to_be16(MIN_TX_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_RX_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_RX_ADD_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_TX_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_RX_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_RX_ADD_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MIN_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MIN_RX_ADD_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MAX_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
	    cpu_to_be16(MAX_RX_ADD_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(TCP_IP_OFFLOAD);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(PROMISC_SUPPORTED);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MIN_MTU);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_MTU);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_MULTICAST_FILTERS);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(VLAN_HEADER_INSERTION);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(MAX_TX_SG_ENTRIES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(RX_SG_SUPPORTED);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(OPT_TX_COMP_SUB_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(OPT_RX_COMP_QUEUES);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_RX_BUFADD_Q_PER_RX_COMP_Q);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_TX_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability =
			cpu_to_be16(OPT_RXBA_ENTRIES_PER_SUBCRQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);

	crq.query_capability.capability = cpu_to_be16(TX_RX_DESC_REQ);
	atomic_inc(&adapter->running_cap_queries);
	ibmvnic_send_crq(adapter, &crq);
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

	adapter->ip_offload_ctrl.version = cpu_to_be32(INITIAL_VERSION_IOB);
	adapter->ip_offload_ctrl.tcp_ipv4_chksum = buf->tcp_ipv4_chksum;
	adapter->ip_offload_ctrl.udp_ipv4_chksum = buf->udp_ipv4_chksum;
	adapter->ip_offload_ctrl.tcp_ipv6_chksum = buf->tcp_ipv6_chksum;
	adapter->ip_offload_ctrl.udp_ipv6_chksum = buf->udp_ipv6_chksum;

	/* large_tx/rx disabled for now, additional features needed */
	adapter->ip_offload_ctrl.large_tx_ipv4 = 0;
	adapter->ip_offload_ctrl.large_tx_ipv6 = 0;
	adapter->ip_offload_ctrl.large_rx_ipv4 = 0;
	adapter->ip_offload_ctrl.large_rx_ipv6 = 0;

	adapter->netdev->features = NETIF_F_GSO;

	if (buf->tcp_ipv4_chksum || buf->udp_ipv4_chksum)
		adapter->netdev->features |= NETIF_F_IP_CSUM;

	if (buf->tcp_ipv6_chksum || buf->udp_ipv6_chksum)
		adapter->netdev->features |= NETIF_F_IPV6_CSUM;

	memset(&crq, 0, sizeof(crq));
	crq.control_ip_offload.first = IBMVNIC_CRQ_CMD;
	crq.control_ip_offload.cmd = CONTROL_IP_OFFLOAD;
	crq.control_ip_offload.len =
	    cpu_to_be32(sizeof(adapter->ip_offload_ctrl));
	crq.control_ip_offload.ioba = cpu_to_be32(adapter->ip_offload_ctrl_tok);
	ibmvnic_send_crq(adapter, &crq);
}

static void handle_error_info_rsp(union ibmvnic_crq *crq,
				  struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_error_buff *error_buff;
	unsigned long flags;
	bool found = false;
	int i;

	if (!crq->request_error_rsp.rc.code) {
		dev_info(dev, "Request Error Rsp returned with rc=%x\n",
			 crq->request_error_rsp.rc.code);
		return;
	}

	spin_lock_irqsave(&adapter->error_list_lock, flags);
	list_for_each_entry(error_buff, &adapter->errors, list)
		if (error_buff->error_id == crq->request_error_rsp.error_id) {
			found = true;
			list_del(&error_buff->list);
			break;
		}
	spin_unlock_irqrestore(&adapter->error_list_lock, flags);

	if (!found) {
		dev_err(dev, "Couldn't find error id %x\n",
			crq->request_error_rsp.error_id);
		return;
	}

	dev_err(dev, "Detailed info for error id %x:",
		crq->request_error_rsp.error_id);

	for (i = 0; i < error_buff->len; i++) {
		pr_cont("%02x", (int)error_buff->buff[i]);
		if (i % 8 == 7)
			pr_cont(" ");
	}
	pr_cont("\n");

	dma_unmap_single(dev, error_buff->dma, error_buff->len,
			 DMA_FROM_DEVICE);
	kfree(error_buff->buff);
	kfree(error_buff);
}

static void handle_dump_size_rsp(union ibmvnic_crq *crq,
				 struct ibmvnic_adapter *adapter)
{
	int len = be32_to_cpu(crq->request_dump_size_rsp.len);
	struct ibmvnic_inflight_cmd *inflight_cmd;
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq newcrq;
	unsigned long flags;

	/* allocate and map buffer */
	adapter->dump_data = kmalloc(len, GFP_KERNEL);
	if (!adapter->dump_data) {
		complete(&adapter->fw_done);
		return;
	}

	adapter->dump_data_token = dma_map_single(dev, adapter->dump_data, len,
						  DMA_FROM_DEVICE);

	if (dma_mapping_error(dev, adapter->dump_data_token)) {
		if (!firmware_has_feature(FW_FEATURE_CMO))
			dev_err(dev, "Couldn't map dump data\n");
		kfree(adapter->dump_data);
		complete(&adapter->fw_done);
		return;
	}

	inflight_cmd = kmalloc(sizeof(*inflight_cmd), GFP_ATOMIC);
	if (!inflight_cmd) {
		dma_unmap_single(dev, adapter->dump_data_token, len,
				 DMA_FROM_DEVICE);
		kfree(adapter->dump_data);
		complete(&adapter->fw_done);
		return;
	}

	memset(&newcrq, 0, sizeof(newcrq));
	newcrq.request_dump.first = IBMVNIC_CRQ_CMD;
	newcrq.request_dump.cmd = REQUEST_DUMP;
	newcrq.request_dump.ioba = cpu_to_be32(adapter->dump_data_token);
	newcrq.request_dump.len = cpu_to_be32(adapter->dump_data_size);

	memcpy(&inflight_cmd->crq, &newcrq, sizeof(newcrq));

	spin_lock_irqsave(&adapter->inflight_lock, flags);
	list_add_tail(&inflight_cmd->list, &adapter->inflight);
	spin_unlock_irqrestore(&adapter->inflight_lock, flags);

	ibmvnic_send_crq(adapter, &newcrq);
}

static void handle_error_indication(union ibmvnic_crq *crq,
				    struct ibmvnic_adapter *adapter)
{
	int detail_len = be32_to_cpu(crq->error_indication.detail_error_sz);
	struct ibmvnic_inflight_cmd *inflight_cmd;
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_error_buff *error_buff;
	union ibmvnic_crq new_crq;
	unsigned long flags;

	dev_err(dev, "Firmware reports %serror id %x, cause %d\n",
		crq->error_indication.
		    flags & IBMVNIC_FATAL_ERROR ? "FATAL " : "",
		crq->error_indication.error_id,
		crq->error_indication.error_cause);

	error_buff = kmalloc(sizeof(*error_buff), GFP_ATOMIC);
	if (!error_buff)
		return;

	error_buff->buff = kmalloc(detail_len, GFP_ATOMIC);
	if (!error_buff->buff) {
		kfree(error_buff);
		return;
	}

	error_buff->dma = dma_map_single(dev, error_buff->buff, detail_len,
					 DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, error_buff->dma)) {
		if (!firmware_has_feature(FW_FEATURE_CMO))
			dev_err(dev, "Couldn't map error buffer\n");
		kfree(error_buff->buff);
		kfree(error_buff);
		return;
	}

	inflight_cmd = kmalloc(sizeof(*inflight_cmd), GFP_ATOMIC);
	if (!inflight_cmd) {
		dma_unmap_single(dev, error_buff->dma, detail_len,
				 DMA_FROM_DEVICE);
		kfree(error_buff->buff);
		kfree(error_buff);
		return;
	}

	error_buff->len = detail_len;
	error_buff->error_id = crq->error_indication.error_id;

	spin_lock_irqsave(&adapter->error_list_lock, flags);
	list_add_tail(&error_buff->list, &adapter->errors);
	spin_unlock_irqrestore(&adapter->error_list_lock, flags);

	memset(&new_crq, 0, sizeof(new_crq));
	new_crq.request_error_info.first = IBMVNIC_CRQ_CMD;
	new_crq.request_error_info.cmd = REQUEST_ERROR_INFO;
	new_crq.request_error_info.ioba = cpu_to_be32(error_buff->dma);
	new_crq.request_error_info.len = cpu_to_be32(detail_len);
	new_crq.request_error_info.error_id = crq->error_indication.error_id;

	memcpy(&inflight_cmd->crq, &crq, sizeof(crq));

	spin_lock_irqsave(&adapter->inflight_lock, flags);
	list_add_tail(&inflight_cmd->list, &adapter->inflight);
	spin_unlock_irqrestore(&adapter->inflight_lock, flags);

	ibmvnic_send_crq(adapter, &new_crq);
}

static void handle_change_mac_rsp(union ibmvnic_crq *crq,
				  struct ibmvnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	long rc;

	rc = crq->change_mac_addr_rsp.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in CHANGE_MAC_ADDR_RSP\n", rc);
		return;
	}
	memcpy(netdev->dev_addr, &crq->change_mac_addr_rsp.mac_addr[0],
	       ETH_ALEN);
}

static void handle_request_cap_rsp(union ibmvnic_crq *crq,
				   struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	u64 *req_value;
	char *name;

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
			 (long int)be32_to_cpu(crq->request_capability_rsp.
					       number), name);
		release_sub_crqs(adapter);
		*req_value = be32_to_cpu(crq->request_capability_rsp.number);
		complete(&adapter->init_done);
		return;
	default:
		dev_err(dev, "Error %d in request cap rsp\n",
			crq->request_capability_rsp.rc.code);
		return;
	}

	/* Done receiving requested capabilities, query IP offload support */
	if (++adapter->requested_caps == 7) {
		union ibmvnic_crq newcrq;
		int buf_sz = sizeof(struct ibmvnic_query_ip_offload_buffer);
		struct ibmvnic_query_ip_offload_buffer *ip_offload_buf =
		    &adapter->ip_offload_buf;

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
	struct ibmvnic_login_rsp_buffer *login_rsp = adapter->login_rsp_buf;
	struct ibmvnic_login_buffer *login = adapter->login_buf;
	union ibmvnic_crq crq;
	int i;

	dma_unmap_single(dev, adapter->login_buf_token, adapter->login_buf_sz,
			 DMA_BIDIRECTIONAL);
	dma_unmap_single(dev, adapter->login_rsp_buf_token,
			 adapter->login_rsp_buf_sz, DMA_BIDIRECTIONAL);

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
	complete(&adapter->init_done);

	memset(&crq, 0, sizeof(crq));
	crq.request_ras_comp_num.first = IBMVNIC_CRQ_CMD;
	crq.request_ras_comp_num.cmd = REQUEST_RAS_COMP_NUM;
	ibmvnic_send_crq(adapter, &crq);

	return 0;
}

static void handle_request_map_rsp(union ibmvnic_crq *crq,
				   struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	u8 map_id = crq->request_map_rsp.map_id;
	int tx_subcrqs;
	int rx_subcrqs;
	long rc;
	int i;

	tx_subcrqs = be32_to_cpu(adapter->login_rsp_buf->num_txsubm_subcrqs);
	rx_subcrqs = be32_to_cpu(adapter->login_rsp_buf->num_rxadd_subcrqs);

	rc = crq->request_map_rsp.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in REQUEST_MAP_RSP\n", rc);
		adapter->map_id--;
		/* need to find and zero tx/rx_pool map_id */
		for (i = 0; i < tx_subcrqs; i++) {
			if (adapter->tx_pool[i].long_term_buff.map_id == map_id)
				adapter->tx_pool[i].long_term_buff.map_id = 0;
		}
		for (i = 0; i < rx_subcrqs; i++) {
			if (adapter->rx_pool[i].long_term_buff.map_id == map_id)
				adapter->rx_pool[i].long_term_buff.map_id = 0;
		}
	}
	complete(&adapter->fw_done);
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

	atomic_dec(&adapter->running_cap_queries);
	netdev_dbg(netdev, "Outstanding queries: %d\n",
		   atomic_read(&adapter->running_cap_queries));
	rc = crq->query_capability.rc.code;
	if (rc) {
		dev_err(dev, "Error %ld in QUERY_CAP_RSP\n", rc);
		goto out;
	}

	switch (be16_to_cpu(crq->query_capability.capability)) {
	case MIN_TX_QUEUES:
		adapter->min_tx_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_tx_queues = %lld\n",
			   adapter->min_tx_queues);
		break;
	case MIN_RX_QUEUES:
		adapter->min_rx_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_queues = %lld\n",
			   adapter->min_rx_queues);
		break;
	case MIN_RX_ADD_QUEUES:
		adapter->min_rx_add_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_add_queues = %lld\n",
			   adapter->min_rx_add_queues);
		break;
	case MAX_TX_QUEUES:
		adapter->max_tx_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_queues = %lld\n",
			   adapter->max_tx_queues);
		break;
	case MAX_RX_QUEUES:
		adapter->max_rx_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_queues = %lld\n",
			   adapter->max_rx_queues);
		break;
	case MAX_RX_ADD_QUEUES:
		adapter->max_rx_add_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_add_queues = %lld\n",
			   adapter->max_rx_add_queues);
		break;
	case MIN_TX_ENTRIES_PER_SUBCRQ:
		adapter->min_tx_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_tx_entries_per_subcrq = %lld\n",
			   adapter->min_tx_entries_per_subcrq);
		break;
	case MIN_RX_ADD_ENTRIES_PER_SUBCRQ:
		adapter->min_rx_add_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_rx_add_entrs_per_subcrq = %lld\n",
			   adapter->min_rx_add_entries_per_subcrq);
		break;
	case MAX_TX_ENTRIES_PER_SUBCRQ:
		adapter->max_tx_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_entries_per_subcrq = %lld\n",
			   adapter->max_tx_entries_per_subcrq);
		break;
	case MAX_RX_ADD_ENTRIES_PER_SUBCRQ:
		adapter->max_rx_add_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_rx_add_entrs_per_subcrq = %lld\n",
			   adapter->max_rx_add_entries_per_subcrq);
		break;
	case TCP_IP_OFFLOAD:
		adapter->tcp_ip_offload =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "tcp_ip_offload = %lld\n",
			   adapter->tcp_ip_offload);
		break;
	case PROMISC_SUPPORTED:
		adapter->promisc_supported =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "promisc_supported = %lld\n",
			   adapter->promisc_supported);
		break;
	case MIN_MTU:
		adapter->min_mtu = be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "min_mtu = %lld\n", adapter->min_mtu);
		break;
	case MAX_MTU:
		adapter->max_mtu = be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_mtu = %lld\n", adapter->max_mtu);
		break;
	case MAX_MULTICAST_FILTERS:
		adapter->max_multicast_filters =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_multicast_filters = %lld\n",
			   adapter->max_multicast_filters);
		break;
	case VLAN_HEADER_INSERTION:
		adapter->vlan_header_insertion =
		    be32_to_cpu(crq->query_capability.number);
		if (adapter->vlan_header_insertion)
			netdev->features |= NETIF_F_HW_VLAN_STAG_TX;
		netdev_dbg(netdev, "vlan_header_insertion = %lld\n",
			   adapter->vlan_header_insertion);
		break;
	case MAX_TX_SG_ENTRIES:
		adapter->max_tx_sg_entries =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "max_tx_sg_entries = %lld\n",
			   adapter->max_tx_sg_entries);
		break;
	case RX_SG_SUPPORTED:
		adapter->rx_sg_supported =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "rx_sg_supported = %lld\n",
			   adapter->rx_sg_supported);
		break;
	case OPT_TX_COMP_SUB_QUEUES:
		adapter->opt_tx_comp_sub_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_tx_comp_sub_queues = %lld\n",
			   adapter->opt_tx_comp_sub_queues);
		break;
	case OPT_RX_COMP_QUEUES:
		adapter->opt_rx_comp_queues =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_rx_comp_queues = %lld\n",
			   adapter->opt_rx_comp_queues);
		break;
	case OPT_RX_BUFADD_Q_PER_RX_COMP_Q:
		adapter->opt_rx_bufadd_q_per_rx_comp_q =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_rx_bufadd_q_per_rx_comp_q = %lld\n",
			   adapter->opt_rx_bufadd_q_per_rx_comp_q);
		break;
	case OPT_TX_ENTRIES_PER_SUBCRQ:
		adapter->opt_tx_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
		netdev_dbg(netdev, "opt_tx_entries_per_subcrq = %lld\n",
			   adapter->opt_tx_entries_per_subcrq);
		break;
	case OPT_RXBA_ENTRIES_PER_SUBCRQ:
		adapter->opt_rxba_entries_per_subcrq =
		    be32_to_cpu(crq->query_capability.number);
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
	if (atomic_read(&adapter->running_cap_queries) == 0)
		complete(&adapter->init_done);
		/* We're done querying the capabilities, initialize sub-crqs */
}

static void handle_control_ras_rsp(union ibmvnic_crq *crq,
				   struct ibmvnic_adapter *adapter)
{
	u8 correlator = crq->control_ras_rsp.correlator;
	struct device *dev = &adapter->vdev->dev;
	bool found = false;
	int i;

	if (crq->control_ras_rsp.rc.code) {
		dev_warn(dev, "Control ras failed rc=%d\n",
			 crq->control_ras_rsp.rc.code);
		return;
	}

	for (i = 0; i < adapter->ras_comp_num; i++) {
		if (adapter->ras_comps[i].correlator == correlator) {
			found = true;
			break;
		}
	}

	if (!found) {
		dev_warn(dev, "Correlator not found on control_ras_rsp\n");
		return;
	}

	switch (crq->control_ras_rsp.op) {
	case IBMVNIC_TRACE_LEVEL:
		adapter->ras_comps[i].trace_level = crq->control_ras.level;
		break;
	case IBMVNIC_ERROR_LEVEL:
		adapter->ras_comps[i].error_check_level =
		    crq->control_ras.level;
		break;
	case IBMVNIC_TRACE_PAUSE:
		adapter->ras_comp_int[i].paused = 1;
		break;
	case IBMVNIC_TRACE_RESUME:
		adapter->ras_comp_int[i].paused = 0;
		break;
	case IBMVNIC_TRACE_ON:
		adapter->ras_comps[i].trace_on = 1;
		break;
	case IBMVNIC_TRACE_OFF:
		adapter->ras_comps[i].trace_on = 0;
		break;
	case IBMVNIC_CHG_TRACE_BUFF_SZ:
		/* trace_buff_sz is 3 bytes, stuff it into an int */
		((u8 *)(&adapter->ras_comps[i].trace_buff_size))[0] = 0;
		((u8 *)(&adapter->ras_comps[i].trace_buff_size))[1] =
		    crq->control_ras_rsp.trace_buff_sz[0];
		((u8 *)(&adapter->ras_comps[i].trace_buff_size))[2] =
		    crq->control_ras_rsp.trace_buff_sz[1];
		((u8 *)(&adapter->ras_comps[i].trace_buff_size))[3] =
		    crq->control_ras_rsp.trace_buff_sz[2];
		break;
	default:
		dev_err(dev, "invalid op %d on control_ras_rsp",
			crq->control_ras_rsp.op);
	}
}

static int ibmvnic_fw_comp_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t trace_read(struct file *file, char __user *user_buf, size_t len,
			  loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_fw_trace_entry *trace;
	int num = ras_comp_int->num;
	union ibmvnic_crq crq;
	dma_addr_t trace_tok;

	if (*ppos >= be32_to_cpu(adapter->ras_comps[num].trace_buff_size))
		return 0;

	trace =
	    dma_alloc_coherent(dev,
			       be32_to_cpu(adapter->ras_comps[num].
					   trace_buff_size), &trace_tok,
			       GFP_KERNEL);
	if (!trace) {
		dev_err(dev, "Couldn't alloc trace buffer\n");
		return 0;
	}

	memset(&crq, 0, sizeof(crq));
	crq.collect_fw_trace.first = IBMVNIC_CRQ_CMD;
	crq.collect_fw_trace.cmd = COLLECT_FW_TRACE;
	crq.collect_fw_trace.correlator = adapter->ras_comps[num].correlator;
	crq.collect_fw_trace.ioba = cpu_to_be32(trace_tok);
	crq.collect_fw_trace.len = adapter->ras_comps[num].trace_buff_size;
	ibmvnic_send_crq(adapter, &crq);

	init_completion(&adapter->fw_done);
	wait_for_completion(&adapter->fw_done);

	if (*ppos + len > be32_to_cpu(adapter->ras_comps[num].trace_buff_size))
		len =
		    be32_to_cpu(adapter->ras_comps[num].trace_buff_size) -
		    *ppos;

	copy_to_user(user_buf, &((u8 *)trace)[*ppos], len);

	dma_free_coherent(dev,
			  be32_to_cpu(adapter->ras_comps[num].trace_buff_size),
			  trace, trace_tok);
	*ppos += len;
	return len;
}

static const struct file_operations trace_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= trace_read,
};

static ssize_t paused_read(struct file *file, char __user *user_buf, size_t len,
			   loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	char buff[5]; /*  1 or 0 plus \n and \0 */
	int size;

	size = sprintf(buff, "%d\n", adapter->ras_comp_int[num].paused);

	if (*ppos >= size)
		return 0;

	copy_to_user(user_buf, buff, size);
	*ppos += size;
	return size;
}

static ssize_t paused_write(struct file *file, const char __user *user_buf,
			    size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	union ibmvnic_crq crq;
	unsigned long val;
	char buff[9]; /* decimal max int plus \n and \0 */

	copy_from_user(buff, user_buf, sizeof(buff));
	val = kstrtoul(buff, 10, NULL);

	adapter->ras_comp_int[num].paused = val ? 1 : 0;

	memset(&crq, 0, sizeof(crq));
	crq.control_ras.first = IBMVNIC_CRQ_CMD;
	crq.control_ras.cmd = CONTROL_RAS;
	crq.control_ras.correlator = adapter->ras_comps[num].correlator;
	crq.control_ras.op = val ? IBMVNIC_TRACE_PAUSE : IBMVNIC_TRACE_RESUME;
	ibmvnic_send_crq(adapter, &crq);

	return len;
}

static const struct file_operations paused_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= paused_read,
	.write		= paused_write,
};

static ssize_t tracing_read(struct file *file, char __user *user_buf,
			    size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	char buff[5]; /*  1 or 0 plus \n and \0 */
	int size;

	size = sprintf(buff, "%d\n", adapter->ras_comps[num].trace_on);

	if (*ppos >= size)
		return 0;

	copy_to_user(user_buf, buff, size);
	*ppos += size;
	return size;
}

static ssize_t tracing_write(struct file *file, const char __user *user_buf,
			     size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	union ibmvnic_crq crq;
	unsigned long val;
	char buff[9]; /* decimal max int plus \n and \0 */

	copy_from_user(buff, user_buf, sizeof(buff));
	val = kstrtoul(buff, 10, NULL);

	memset(&crq, 0, sizeof(crq));
	crq.control_ras.first = IBMVNIC_CRQ_CMD;
	crq.control_ras.cmd = CONTROL_RAS;
	crq.control_ras.correlator = adapter->ras_comps[num].correlator;
	crq.control_ras.op = val ? IBMVNIC_TRACE_ON : IBMVNIC_TRACE_OFF;

	return len;
}

static const struct file_operations tracing_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= tracing_read,
	.write		= tracing_write,
};

static ssize_t error_level_read(struct file *file, char __user *user_buf,
				size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	char buff[5]; /* decimal max char plus \n and \0 */
	int size;

	size = sprintf(buff, "%d\n", adapter->ras_comps[num].error_check_level);

	if (*ppos >= size)
		return 0;

	copy_to_user(user_buf, buff, size);
	*ppos += size;
	return size;
}

static ssize_t error_level_write(struct file *file, const char __user *user_buf,
				 size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	union ibmvnic_crq crq;
	unsigned long val;
	char buff[9]; /* decimal max int plus \n and \0 */

	copy_from_user(buff, user_buf, sizeof(buff));
	val = kstrtoul(buff, 10, NULL);

	if (val > 9)
		val = 9;

	memset(&crq, 0, sizeof(crq));
	crq.control_ras.first = IBMVNIC_CRQ_CMD;
	crq.control_ras.cmd = CONTROL_RAS;
	crq.control_ras.correlator = adapter->ras_comps[num].correlator;
	crq.control_ras.op = IBMVNIC_ERROR_LEVEL;
	crq.control_ras.level = val;
	ibmvnic_send_crq(adapter, &crq);

	return len;
}

static const struct file_operations error_level_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= error_level_read,
	.write		= error_level_write,
};

static ssize_t trace_level_read(struct file *file, char __user *user_buf,
				size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	char buff[5]; /* decimal max char plus \n and \0 */
	int size;

	size = sprintf(buff, "%d\n", adapter->ras_comps[num].trace_level);
	if (*ppos >= size)
		return 0;

	copy_to_user(user_buf, buff, size);
	*ppos += size;
	return size;
}

static ssize_t trace_level_write(struct file *file, const char __user *user_buf,
				 size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	union ibmvnic_crq crq;
	unsigned long val;
	char buff[9]; /* decimal max int plus \n and \0 */

	copy_from_user(buff, user_buf, sizeof(buff));
	val = kstrtoul(buff, 10, NULL);
	if (val > 9)
		val = 9;

	memset(&crq, 0, sizeof(crq));
	crq.control_ras.first = IBMVNIC_CRQ_CMD;
	crq.control_ras.cmd = CONTROL_RAS;
	crq.control_ras.correlator =
	    adapter->ras_comps[ras_comp_int->num].correlator;
	crq.control_ras.op = IBMVNIC_TRACE_LEVEL;
	crq.control_ras.level = val;
	ibmvnic_send_crq(adapter, &crq);

	return len;
}

static const struct file_operations trace_level_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= trace_level_read,
	.write		= trace_level_write,
};

static ssize_t trace_buff_size_read(struct file *file, char __user *user_buf,
				    size_t len, loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	int num = ras_comp_int->num;
	char buff[9]; /* decimal max int plus \n and \0 */
	int size;

	size = sprintf(buff, "%d\n", adapter->ras_comps[num].trace_buff_size);
	if (*ppos >= size)
		return 0;

	copy_to_user(user_buf, buff, size);
	*ppos += size;
	return size;
}

static ssize_t trace_buff_size_write(struct file *file,
				     const char __user *user_buf, size_t len,
				     loff_t *ppos)
{
	struct ibmvnic_fw_comp_internal *ras_comp_int = file->private_data;
	struct ibmvnic_adapter *adapter = ras_comp_int->adapter;
	union ibmvnic_crq crq;
	unsigned long val;
	char buff[9]; /* decimal max int plus \n and \0 */

	copy_from_user(buff, user_buf, sizeof(buff));
	val = kstrtoul(buff, 10, NULL);

	memset(&crq, 0, sizeof(crq));
	crq.control_ras.first = IBMVNIC_CRQ_CMD;
	crq.control_ras.cmd = CONTROL_RAS;
	crq.control_ras.correlator =
	    adapter->ras_comps[ras_comp_int->num].correlator;
	crq.control_ras.op = IBMVNIC_CHG_TRACE_BUFF_SZ;
	/* trace_buff_sz is 3 bytes, stuff an int into it */
	crq.control_ras.trace_buff_sz[0] = ((u8 *)(&val))[5];
	crq.control_ras.trace_buff_sz[1] = ((u8 *)(&val))[6];
	crq.control_ras.trace_buff_sz[2] = ((u8 *)(&val))[7];
	ibmvnic_send_crq(adapter, &crq);

	return len;
}

static const struct file_operations trace_size_ops = {
	.owner		= THIS_MODULE,
	.open		= ibmvnic_fw_comp_open,
	.read		= trace_buff_size_read,
	.write		= trace_buff_size_write,
};

static void handle_request_ras_comps_rsp(union ibmvnic_crq *crq,
					 struct ibmvnic_adapter *adapter)
{
	struct device *dev = &adapter->vdev->dev;
	struct dentry *dir_ent;
	struct dentry *ent;
	int i;

	debugfs_remove_recursive(adapter->ras_comps_ent);

	adapter->ras_comps_ent = debugfs_create_dir("ras_comps",
						    adapter->debugfs_dir);
	if (!adapter->ras_comps_ent || IS_ERR(adapter->ras_comps_ent)) {
		dev_info(dev, "debugfs create ras_comps dir failed\n");
		return;
	}

	for (i = 0; i < adapter->ras_comp_num; i++) {
		dir_ent = debugfs_create_dir(adapter->ras_comps[i].name,
					     adapter->ras_comps_ent);
		if (!dir_ent || IS_ERR(dir_ent)) {
			dev_info(dev, "debugfs create %s dir failed\n",
				 adapter->ras_comps[i].name);
			continue;
		}

		adapter->ras_comp_int[i].adapter = adapter;
		adapter->ras_comp_int[i].num = i;
		adapter->ras_comp_int[i].desc_blob.data =
		    &adapter->ras_comps[i].description;
		adapter->ras_comp_int[i].desc_blob.size =
		    sizeof(adapter->ras_comps[i].description);

		/* Don't need to remember the dentry's because the debugfs dir
		 * gets removed recursively
		 */
		ent = debugfs_create_blob("description", S_IRUGO, dir_ent,
					  &adapter->ras_comp_int[i].desc_blob);
		ent = debugfs_create_file("trace_buf_size", S_IRUGO | S_IWUSR,
					  dir_ent, &adapter->ras_comp_int[i],
					  &trace_size_ops);
		ent = debugfs_create_file("trace_level",
					  S_IRUGO |
					  (adapter->ras_comps[i].trace_level !=
					   0xFF  ? S_IWUSR : 0),
					   dir_ent, &adapter->ras_comp_int[i],
					   &trace_level_ops);
		ent = debugfs_create_file("error_level",
					  S_IRUGO |
					  (adapter->
					   ras_comps[i].error_check_level !=
					   0xFF ? S_IWUSR : 0),
					  dir_ent, &adapter->ras_comp_int[i],
					  &trace_level_ops);
		ent = debugfs_create_file("tracing", S_IRUGO | S_IWUSR,
					  dir_ent, &adapter->ras_comp_int[i],
					  &tracing_ops);
		ent = debugfs_create_file("paused", S_IRUGO | S_IWUSR,
					  dir_ent, &adapter->ras_comp_int[i],
					  &paused_ops);
		ent = debugfs_create_file("trace", S_IRUGO, dir_ent,
					  &adapter->ras_comp_int[i],
					  &trace_ops);
	}
}

static void handle_request_ras_comp_num_rsp(union ibmvnic_crq *crq,
					    struct ibmvnic_adapter *adapter)
{
	int len = adapter->ras_comp_num * sizeof(struct ibmvnic_fw_component);
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq newcrq;

	adapter->ras_comps = dma_alloc_coherent(dev, len,
						&adapter->ras_comps_tok,
						GFP_KERNEL);
	if (!adapter->ras_comps) {
		if (!firmware_has_feature(FW_FEATURE_CMO))
			dev_err(dev, "Couldn't alloc fw comps buffer\n");
		return;
	}

	adapter->ras_comp_int = kmalloc(adapter->ras_comp_num *
					sizeof(struct ibmvnic_fw_comp_internal),
					GFP_KERNEL);
	if (!adapter->ras_comp_int)
		dma_free_coherent(dev, len, adapter->ras_comps,
				  adapter->ras_comps_tok);

	memset(&newcrq, 0, sizeof(newcrq));
	newcrq.request_ras_comps.first = IBMVNIC_CRQ_CMD;
	newcrq.request_ras_comps.cmd = REQUEST_RAS_COMPS;
	newcrq.request_ras_comps.ioba = cpu_to_be32(adapter->ras_comps_tok);
	newcrq.request_ras_comps.len = cpu_to_be32(len);
	ibmvnic_send_crq(adapter, &newcrq);
}

static void ibmvnic_free_inflight(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_inflight_cmd *inflight_cmd;
	struct device *dev = &adapter->vdev->dev;
	struct ibmvnic_error_buff *error_buff;
	unsigned long flags;
	unsigned long flags2;

	spin_lock_irqsave(&adapter->inflight_lock, flags);
	list_for_each_entry(inflight_cmd, &adapter->inflight, list) {
		switch (inflight_cmd->crq.generic.cmd) {
		case LOGIN:
			dma_unmap_single(dev, adapter->login_buf_token,
					 adapter->login_buf_sz,
					 DMA_BIDIRECTIONAL);
			dma_unmap_single(dev, adapter->login_rsp_buf_token,
					 adapter->login_rsp_buf_sz,
					 DMA_BIDIRECTIONAL);
			kfree(adapter->login_rsp_buf);
			kfree(adapter->login_buf);
			break;
		case REQUEST_DUMP:
			complete(&adapter->fw_done);
			break;
		case REQUEST_ERROR_INFO:
			spin_lock_irqsave(&adapter->error_list_lock, flags2);
			list_for_each_entry(error_buff, &adapter->errors,
					    list) {
				dma_unmap_single(dev, error_buff->dma,
						 error_buff->len,
						 DMA_FROM_DEVICE);
				kfree(error_buff->buff);
				list_del(&error_buff->list);
				kfree(error_buff);
			}
			spin_unlock_irqrestore(&adapter->error_list_lock,
					       flags2);
			break;
		}
		list_del(&inflight_cmd->list);
		kfree(inflight_cmd);
	}
	spin_unlock_irqrestore(&adapter->inflight_lock, flags);
}

static void ibmvnic_handle_crq(union ibmvnic_crq *crq,
			       struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_generic_crq *gen_crq = &crq->generic;
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &adapter->vdev->dev;
	long rc;

	netdev_dbg(netdev, "Handling CRQ: %016lx %016lx\n",
		   ((unsigned long int *)crq)[0],
		   ((unsigned long int *)crq)[1]);
	switch (gen_crq->first) {
	case IBMVNIC_CRQ_INIT_RSP:
		switch (gen_crq->cmd) {
		case IBMVNIC_CRQ_INIT:
			dev_info(dev, "Partner initialized\n");
			/* Send back a response */
			rc = ibmvnic_send_crq_init_complete(adapter);
			if (rc == 0)
				send_version_xchg(adapter);
			else
				dev_err(dev, "Can't send initrsp rc=%ld\n", rc);
			break;
		case IBMVNIC_CRQ_INIT_COMPLETE:
			dev_info(dev, "Partner initialization complete\n");
			send_version_xchg(adapter);
			break;
		default:
			dev_err(dev, "Unknown crq cmd: %d\n", gen_crq->cmd);
		}
		return;
	case IBMVNIC_CRQ_XPORT_EVENT:
		if (gen_crq->cmd == IBMVNIC_PARTITION_MIGRATED) {
			dev_info(dev, "Re-enabling adapter\n");
			adapter->migrated = true;
			ibmvnic_free_inflight(adapter);
			release_sub_crqs(adapter);
			rc = ibmvnic_reenable_crq_queue(adapter);
			if (rc)
				dev_err(dev, "Error after enable rc=%ld\n", rc);
			adapter->migrated = false;
			rc = ibmvnic_send_crq_init(adapter);
			if (rc)
				dev_err(dev, "Error sending init rc=%ld\n", rc);
		} else {
			/* The adapter lost the connection */
			dev_err(dev, "Virtual Adapter failed (rc=%d)\n",
				gen_crq->cmd);
			ibmvnic_free_inflight(adapter);
			release_sub_crqs(adapter);
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
		handle_request_map_rsp(crq, adapter);
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
		netdev_dbg(netdev, "Got Logical Link State Response\n");
		adapter->logical_link_state =
		    crq->logical_link_state_rsp.link_state;
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
		handle_change_mac_rsp(crq, adapter);
		break;
	case ERROR_INDICATION:
		netdev_dbg(netdev, "Got Error Indication\n");
		handle_error_indication(crq, adapter);
		break;
	case REQUEST_ERROR_RSP:
		netdev_dbg(netdev, "Got Error Detail Response\n");
		handle_error_info_rsp(crq, adapter);
		break;
	case REQUEST_STATISTICS_RSP:
		netdev_dbg(netdev, "Got Statistics Response\n");
		complete(&adapter->stats_done);
		break;
	case REQUEST_DUMP_SIZE_RSP:
		netdev_dbg(netdev, "Got Request Dump Size Response\n");
		handle_dump_size_rsp(crq, adapter);
		break;
	case REQUEST_DUMP_RSP:
		netdev_dbg(netdev, "Got Request Dump Response\n");
		complete(&adapter->fw_done);
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
		/* We're done with the queries, perform the login */
		send_login(adapter);
		break;
	case REQUEST_RAS_COMP_NUM_RSP:
		netdev_dbg(netdev, "Got Request RAS Comp Num Response\n");
		if (crq->request_ras_comp_num_rsp.rc.code == 10) {
			netdev_dbg(netdev, "Request RAS Comp Num not supported\n");
			break;
		}
		adapter->ras_comp_num =
		    be32_to_cpu(crq->request_ras_comp_num_rsp.num_components);
		handle_request_ras_comp_num_rsp(crq, adapter);
		break;
	case REQUEST_RAS_COMPS_RSP:
		netdev_dbg(netdev, "Got Request RAS Comps Response\n");
		handle_request_ras_comps_rsp(crq, adapter);
		break;
	case CONTROL_RAS_RSP:
		netdev_dbg(netdev, "Got Control RAS Response\n");
		handle_control_ras_rsp(crq, adapter);
		break;
	case COLLECT_FW_TRACE_RSP:
		netdev_dbg(netdev, "Got Collect firmware trace Response\n");
		complete(&adapter->fw_done);
		break;
	default:
		netdev_err(netdev, "Got an invalid cmd type 0x%02x\n",
			   gen_crq->cmd);
	}
}

static irqreturn_t ibmvnic_interrupt(int irq, void *instance)
{
	struct ibmvnic_adapter *adapter = instance;
	struct ibmvnic_crq_queue *queue = &adapter->crq;
	struct vio_dev *vdev = adapter->vdev;
	union ibmvnic_crq *crq;
	unsigned long flags;
	bool done = false;

	spin_lock_irqsave(&queue->lock, flags);
	vio_disable_interrupts(vdev);
	while (!done) {
		/* Pull all the valid messages off the CRQ */
		while ((crq = ibmvnic_next_crq(adapter)) != NULL) {
			ibmvnic_handle_crq(crq, adapter);
			crq->generic.first = 0;
		}
		vio_enable_interrupts(vdev);
		crq = ibmvnic_next_crq(adapter);
		if (crq) {
			vio_disable_interrupts(vdev);
			ibmvnic_handle_crq(crq, adapter);
			crq->generic.first = 0;
		} else {
			done = true;
		}
	}
	spin_unlock_irqrestore(&queue->lock, flags);
	return IRQ_HANDLED;
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

static void ibmvnic_release_crq_queue(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *crq = &adapter->crq;
	struct vio_dev *vdev = adapter->vdev;
	long rc;

	netdev_dbg(adapter->netdev, "Releasing CRQ\n");
	free_irq(vdev->irq, adapter);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	dma_unmap_single(&vdev->dev, crq->msg_token, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
	free_page((unsigned long)crq->msgs);
}

static int ibmvnic_init_crq_queue(struct ibmvnic_adapter *adapter)
{
	struct ibmvnic_crq_queue *crq = &adapter->crq;
	struct device *dev = &adapter->vdev->dev;
	struct vio_dev *vdev = adapter->vdev;
	int rc, retrc = -ENOMEM;

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
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));
reg_crq_failed:
	dma_unmap_single(dev, crq->msg_token, PAGE_SIZE, DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long)crq->msgs);
	return retrc;
}

/* debugfs for dump */
static int ibmvnic_dump_show(struct seq_file *seq, void *v)
{
	struct net_device *netdev = seq->private;
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->vdev->dev;
	union ibmvnic_crq crq;

	memset(&crq, 0, sizeof(crq));
	crq.request_dump_size.first = IBMVNIC_CRQ_CMD;
	crq.request_dump_size.cmd = REQUEST_DUMP_SIZE;
	ibmvnic_send_crq(adapter, &crq);

	init_completion(&adapter->fw_done);
	wait_for_completion(&adapter->fw_done);

	seq_write(seq, adapter->dump_data, adapter->dump_data_size);

	dma_unmap_single(dev, adapter->dump_data_token, adapter->dump_data_size,
			 DMA_BIDIRECTIONAL);

	kfree(adapter->dump_data);

	return 0;
}

static int ibmvnic_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ibmvnic_dump_show, inode->i_private);
}

static const struct file_operations ibmvnic_dump_ops = {
	.owner          = THIS_MODULE,
	.open           = ibmvnic_dump_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int ibmvnic_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	struct ibmvnic_adapter *adapter;
	struct net_device *netdev;
	unsigned char *mac_addr_p;
	struct dentry *ent;
	char buf[16]; /* debugfs name buf */
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
				   IBMVNIC_MAX_TX_QUEUES);
	if (!netdev)
		return -ENOMEM;

	adapter = netdev_priv(netdev);
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

	rc = ibmvnic_init_crq_queue(adapter);
	if (rc) {
		dev_err(&dev->dev, "Couldn't initialize crq. rc=%d\n", rc);
		goto free_netdev;
	}

	INIT_LIST_HEAD(&adapter->errors);
	INIT_LIST_HEAD(&adapter->inflight);
	spin_lock_init(&adapter->error_list_lock);
	spin_lock_init(&adapter->inflight_lock);

	adapter->stats_token = dma_map_single(&dev->dev, &adapter->stats,
					      sizeof(struct ibmvnic_statistics),
					      DMA_FROM_DEVICE);
	if (dma_mapping_error(&dev->dev, adapter->stats_token)) {
		if (!firmware_has_feature(FW_FEATURE_CMO))
			dev_err(&dev->dev, "Couldn't map stats buffer\n");
		goto free_crq;
	}

	snprintf(buf, sizeof(buf), "ibmvnic_%x", dev->unit_address);
	ent = debugfs_create_dir(buf, NULL);
	if (!ent || IS_ERR(ent)) {
		dev_info(&dev->dev, "debugfs create directory failed\n");
		adapter->debugfs_dir = NULL;
	} else {
		adapter->debugfs_dir = ent;
		ent = debugfs_create_file("dump", S_IRUGO, adapter->debugfs_dir,
					  netdev, &ibmvnic_dump_ops);
		if (!ent || IS_ERR(ent)) {
			dev_info(&dev->dev,
				 "debugfs create dump file failed\n");
			adapter->debugfs_dump = NULL;
		} else {
			adapter->debugfs_dump = ent;
		}
	}
	ibmvnic_send_crq_init(adapter);

	init_completion(&adapter->init_done);
	wait_for_completion(&adapter->init_done);

	/* needed to pull init_sub_crqs outside of an interrupt context
	 * because it creates IRQ mappings for the subCRQ queues, causing
	 * a kernel warning
	 */
	init_sub_crqs(adapter, 0);

	reinit_completion(&adapter->init_done);
	wait_for_completion(&adapter->init_done);

	/* if init_sub_crqs is partially successful, retry */
	while (!adapter->tx_scrq || !adapter->rx_scrq) {
		init_sub_crqs(adapter, 1);

		reinit_completion(&adapter->init_done);
		wait_for_completion(&adapter->init_done);
	}

	netdev->real_num_tx_queues = adapter->req_tx_queues;

	rc = register_netdev(netdev);
	if (rc) {
		dev_err(&dev->dev, "failed to register netdev rc=%d\n", rc);
		goto free_debugfs;
	}
	dev_info(&dev->dev, "ibmvnic registered\n");

	return 0;

free_debugfs:
	if (adapter->debugfs_dir && !IS_ERR(adapter->debugfs_dir))
		debugfs_remove_recursive(adapter->debugfs_dir);
free_crq:
	ibmvnic_release_crq_queue(adapter);
free_netdev:
	free_netdev(netdev);
	return rc;
}

static int ibmvnic_remove(struct vio_dev *dev)
{
	struct net_device *netdev = dev_get_drvdata(&dev->dev);
	struct ibmvnic_adapter *adapter = netdev_priv(netdev);

	unregister_netdev(netdev);

	release_sub_crqs(adapter);

	ibmvnic_release_crq_queue(adapter);

	if (adapter->debugfs_dir && !IS_ERR(adapter->debugfs_dir))
		debugfs_remove_recursive(adapter->debugfs_dir);

	if (adapter->ras_comps)
		dma_free_coherent(&dev->dev,
				  adapter->ras_comp_num *
				  sizeof(struct ibmvnic_fw_component),
				  adapter->ras_comps, adapter->ras_comps_tok);

	kfree(adapter->ras_comp_int);

	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

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
	ret += adapter->bounce_buffer_size;
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
	int i;

	/* kick the interrupt handlers just in case we lost an interrupt */
	for (i = 0; i < adapter->req_rx_queues; i++)
		ibmvnic_interrupt_rx(adapter->rx_scrq[i]->irq,
				     adapter->rx_scrq[i]);

	return 0;
}

static struct vio_device_id ibmvnic_device_table[] = {
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
