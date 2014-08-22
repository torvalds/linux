/*
 * IBM Power Virtual Ethernet Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2003, 2010
 *
 * Authors: Dave Larson <larson1@us.ibm.com>
 *	    Santiago Leon <santil@linux.vnet.ibm.com>
 *	    Brian King <brking@linux.vnet.ibm.com>
 *	    Robert Jennings <rcj@linux.vnet.ibm.com>
 *	    Anton Blanchard <anton@au.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <asm/hvcall.h>
#include <linux/atomic.h>
#include <asm/vio.h>
#include <asm/iommu.h>
#include <asm/firmware.h>

#include "ibmveth.h"

static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance);
static void ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter);
static unsigned long ibmveth_get_desired_dma(struct vio_dev *vdev);

static struct kobj_type ktype_veth_pool;


static const char ibmveth_driver_name[] = "ibmveth";
static const char ibmveth_driver_string[] = "IBM Power Virtual Ethernet Driver";
#define ibmveth_driver_version "1.04"

MODULE_AUTHOR("Santiago Leon <santil@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM Power Virtual Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ibmveth_driver_version);

static unsigned int tx_copybreak __read_mostly = 128;
module_param(tx_copybreak, uint, 0644);
MODULE_PARM_DESC(tx_copybreak,
	"Maximum size of packet that is copied to a new buffer on transmit");

static unsigned int rx_copybreak __read_mostly = 128;
module_param(rx_copybreak, uint, 0644);
MODULE_PARM_DESC(rx_copybreak,
	"Maximum size of packet that is copied to a new buffer on receive");

static unsigned int rx_flush __read_mostly = 0;
module_param(rx_flush, uint, 0644);
MODULE_PARM_DESC(rx_flush, "Flush receive buffers before use");

struct ibmveth_stat {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define IBMVETH_STAT_OFF(stat) offsetof(struct ibmveth_adapter, stat)
#define IBMVETH_GET_STAT(a, off) *((u64 *)(((unsigned long)(a)) + off))

struct ibmveth_stat ibmveth_stats[] = {
	{ "replenish_task_cycles", IBMVETH_STAT_OFF(replenish_task_cycles) },
	{ "replenish_no_mem", IBMVETH_STAT_OFF(replenish_no_mem) },
	{ "replenish_add_buff_failure",
			IBMVETH_STAT_OFF(replenish_add_buff_failure) },
	{ "replenish_add_buff_success",
			IBMVETH_STAT_OFF(replenish_add_buff_success) },
	{ "rx_invalid_buffer", IBMVETH_STAT_OFF(rx_invalid_buffer) },
	{ "rx_no_buffer", IBMVETH_STAT_OFF(rx_no_buffer) },
	{ "tx_map_failed", IBMVETH_STAT_OFF(tx_map_failed) },
	{ "tx_send_failed", IBMVETH_STAT_OFF(tx_send_failed) },
	{ "fw_enabled_ipv4_csum", IBMVETH_STAT_OFF(fw_ipv4_csum_support) },
	{ "fw_enabled_ipv6_csum", IBMVETH_STAT_OFF(fw_ipv6_csum_support) },
};

/* simple methods of getting data from the current rxq entry */
static inline u32 ibmveth_rxq_flags(struct ibmveth_adapter *adapter)
{
	return adapter->rx_queue.queue_addr[adapter->rx_queue.index].flags_off;
}

static inline int ibmveth_rxq_toggle(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_TOGGLE) >>
			IBMVETH_RXQ_TOGGLE_SHIFT;
}

static inline int ibmveth_rxq_pending_buffer(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_toggle(adapter) == adapter->rx_queue.toggle;
}

static inline int ibmveth_rxq_buffer_valid(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_VALID;
}

static inline int ibmveth_rxq_frame_offset(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_OFF_MASK;
}

static inline int ibmveth_rxq_frame_length(struct ibmveth_adapter *adapter)
{
	return adapter->rx_queue.queue_addr[adapter->rx_queue.index].length;
}

static inline int ibmveth_rxq_csum_good(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_CSUM_GOOD;
}

/* setup the initial settings for a buffer pool */
static void ibmveth_init_buffer_pool(struct ibmveth_buff_pool *pool,
				     u32 pool_index, u32 pool_size,
				     u32 buff_size, u32 pool_active)
{
	pool->size = pool_size;
	pool->index = pool_index;
	pool->buff_size = buff_size;
	pool->threshold = pool_size * 7 / 8;
	pool->active = pool_active;
}

/* allocate and setup an buffer pool - called during open */
static int ibmveth_alloc_buffer_pool(struct ibmveth_buff_pool *pool)
{
	int i;

	pool->free_map = kmalloc(sizeof(u16) * pool->size, GFP_KERNEL);

	if (!pool->free_map)
		return -1;

	pool->dma_addr = kmalloc(sizeof(dma_addr_t) * pool->size, GFP_KERNEL);
	if (!pool->dma_addr) {
		kfree(pool->free_map);
		pool->free_map = NULL;
		return -1;
	}

	pool->skbuff = kcalloc(pool->size, sizeof(void *), GFP_KERNEL);

	if (!pool->skbuff) {
		kfree(pool->dma_addr);
		pool->dma_addr = NULL;

		kfree(pool->free_map);
		pool->free_map = NULL;
		return -1;
	}

	memset(pool->dma_addr, 0, sizeof(dma_addr_t) * pool->size);

	for (i = 0; i < pool->size; ++i)
		pool->free_map[i] = i;

	atomic_set(&pool->available, 0);
	pool->producer_index = 0;
	pool->consumer_index = 0;

	return 0;
}

static inline void ibmveth_flush_buffer(void *addr, unsigned long length)
{
	unsigned long offset;

	for (offset = 0; offset < length; offset += SMP_CACHE_BYTES)
		asm("dcbfl %0,%1" :: "b" (addr), "r" (offset));
}

/* replenish the buffers for a pool.  note that we don't need to
 * skb_reserve these since they are used for incoming...
 */
static void ibmveth_replenish_buffer_pool(struct ibmveth_adapter *adapter,
					  struct ibmveth_buff_pool *pool)
{
	u32 i;
	u32 count = pool->size - atomic_read(&pool->available);
	u32 buffers_added = 0;
	struct sk_buff *skb;
	unsigned int free_index, index;
	u64 correlator;
	unsigned long lpar_rc;
	dma_addr_t dma_addr;

	mb();

	for (i = 0; i < count; ++i) {
		union ibmveth_buf_desc desc;

		skb = netdev_alloc_skb(adapter->netdev, pool->buff_size);

		if (!skb) {
			netdev_dbg(adapter->netdev,
				   "replenish: unable to allocate skb\n");
			adapter->replenish_no_mem++;
			break;
		}

		free_index = pool->consumer_index;
		pool->consumer_index++;
		if (pool->consumer_index >= pool->size)
			pool->consumer_index = 0;
		index = pool->free_map[free_index];

		BUG_ON(index == IBM_VETH_INVALID_MAP);
		BUG_ON(pool->skbuff[index] != NULL);

		dma_addr = dma_map_single(&adapter->vdev->dev, skb->data,
				pool->buff_size, DMA_FROM_DEVICE);

		if (dma_mapping_error(&adapter->vdev->dev, dma_addr))
			goto failure;

		pool->free_map[free_index] = IBM_VETH_INVALID_MAP;
		pool->dma_addr[index] = dma_addr;
		pool->skbuff[index] = skb;

		correlator = ((u64)pool->index << 32) | index;
		*(u64 *)skb->data = correlator;

		desc.fields.flags_len = IBMVETH_BUF_VALID | pool->buff_size;
		desc.fields.address = dma_addr;

		if (rx_flush) {
			unsigned int len = min(pool->buff_size,
						adapter->netdev->mtu +
						IBMVETH_BUFF_OH);
			ibmveth_flush_buffer(skb->data, len);
		}
		lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address,
						   desc.desc);

		if (lpar_rc != H_SUCCESS) {
			goto failure;
		} else {
			buffers_added++;
			adapter->replenish_add_buff_success++;
		}
	}

	mb();
	atomic_add(buffers_added, &(pool->available));
	return;

failure:
	pool->free_map[free_index] = index;
	pool->skbuff[index] = NULL;
	if (pool->consumer_index == 0)
		pool->consumer_index = pool->size - 1;
	else
		pool->consumer_index--;
	if (!dma_mapping_error(&adapter->vdev->dev, dma_addr))
		dma_unmap_single(&adapter->vdev->dev,
		                 pool->dma_addr[index], pool->buff_size,
		                 DMA_FROM_DEVICE);
	dev_kfree_skb_any(skb);
	adapter->replenish_add_buff_failure++;

	mb();
	atomic_add(buffers_added, &(pool->available));
}

/*
 * The final 8 bytes of the buffer list is a counter of frames dropped
 * because there was not a buffer in the buffer list capable of holding
 * the frame.
 */
static void ibmveth_update_rx_no_buffer(struct ibmveth_adapter *adapter)
{
	__be64 *p = adapter->buffer_list_addr + 4096 - 8;

	adapter->rx_no_buffer = be64_to_cpup(p);
}

/* replenish routine */
static void ibmveth_replenish_task(struct ibmveth_adapter *adapter)
{
	int i;

	adapter->replenish_task_cycles++;

	for (i = (IBMVETH_NUM_BUFF_POOLS - 1); i >= 0; i--) {
		struct ibmveth_buff_pool *pool = &adapter->rx_buff_pool[i];

		if (pool->active &&
		    (atomic_read(&pool->available) < pool->threshold))
			ibmveth_replenish_buffer_pool(adapter, pool);
	}

	ibmveth_update_rx_no_buffer(adapter);
}

/* empty and free ana buffer pool - also used to do cleanup in error paths */
static void ibmveth_free_buffer_pool(struct ibmveth_adapter *adapter,
				     struct ibmveth_buff_pool *pool)
{
	int i;

	kfree(pool->free_map);
	pool->free_map = NULL;

	if (pool->skbuff && pool->dma_addr) {
		for (i = 0; i < pool->size; ++i) {
			struct sk_buff *skb = pool->skbuff[i];
			if (skb) {
				dma_unmap_single(&adapter->vdev->dev,
						 pool->dma_addr[i],
						 pool->buff_size,
						 DMA_FROM_DEVICE);
				dev_kfree_skb_any(skb);
				pool->skbuff[i] = NULL;
			}
		}
	}

	if (pool->dma_addr) {
		kfree(pool->dma_addr);
		pool->dma_addr = NULL;
	}

	if (pool->skbuff) {
		kfree(pool->skbuff);
		pool->skbuff = NULL;
	}
}

/* remove a buffer from a pool */
static void ibmveth_remove_buffer_from_pool(struct ibmveth_adapter *adapter,
					    u64 correlator)
{
	unsigned int pool  = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;
	unsigned int free_index;
	struct sk_buff *skb;

	BUG_ON(pool >= IBMVETH_NUM_BUFF_POOLS);
	BUG_ON(index >= adapter->rx_buff_pool[pool].size);

	skb = adapter->rx_buff_pool[pool].skbuff[index];

	BUG_ON(skb == NULL);

	adapter->rx_buff_pool[pool].skbuff[index] = NULL;

	dma_unmap_single(&adapter->vdev->dev,
			 adapter->rx_buff_pool[pool].dma_addr[index],
			 adapter->rx_buff_pool[pool].buff_size,
			 DMA_FROM_DEVICE);

	free_index = adapter->rx_buff_pool[pool].producer_index;
	adapter->rx_buff_pool[pool].producer_index++;
	if (adapter->rx_buff_pool[pool].producer_index >=
	    adapter->rx_buff_pool[pool].size)
		adapter->rx_buff_pool[pool].producer_index = 0;
	adapter->rx_buff_pool[pool].free_map[free_index] = index;

	mb();

	atomic_dec(&(adapter->rx_buff_pool[pool].available));
}

/* get the current buffer on the rx queue */
static inline struct sk_buff *ibmveth_rxq_get_buffer(struct ibmveth_adapter *adapter)
{
	u64 correlator = adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator;
	unsigned int pool = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;

	BUG_ON(pool >= IBMVETH_NUM_BUFF_POOLS);
	BUG_ON(index >= adapter->rx_buff_pool[pool].size);

	return adapter->rx_buff_pool[pool].skbuff[index];
}

/* recycle the current buffer on the rx queue */
static int ibmveth_rxq_recycle_buffer(struct ibmveth_adapter *adapter)
{
	u32 q_index = adapter->rx_queue.index;
	u64 correlator = adapter->rx_queue.queue_addr[q_index].correlator;
	unsigned int pool = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;
	union ibmveth_buf_desc desc;
	unsigned long lpar_rc;
	int ret = 1;

	BUG_ON(pool >= IBMVETH_NUM_BUFF_POOLS);
	BUG_ON(index >= adapter->rx_buff_pool[pool].size);

	if (!adapter->rx_buff_pool[pool].active) {
		ibmveth_rxq_harvest_buffer(adapter);
		ibmveth_free_buffer_pool(adapter, &adapter->rx_buff_pool[pool]);
		goto out;
	}

	desc.fields.flags_len = IBMVETH_BUF_VALID |
		adapter->rx_buff_pool[pool].buff_size;
	desc.fields.address = adapter->rx_buff_pool[pool].dma_addr[index];

	lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address, desc.desc);

	if (lpar_rc != H_SUCCESS) {
		netdev_dbg(adapter->netdev, "h_add_logical_lan_buffer failed "
			   "during recycle rc=%ld", lpar_rc);
		ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);
		ret = 0;
	}

	if (++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
		adapter->rx_queue.index = 0;
		adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
	}

out:
	return ret;
}

static void ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter)
{
	ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);

	if (++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
		adapter->rx_queue.index = 0;
		adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
	}
}

static void ibmveth_cleanup(struct ibmveth_adapter *adapter)
{
	int i;
	struct device *dev = &adapter->vdev->dev;

	if (adapter->buffer_list_addr != NULL) {
		if (!dma_mapping_error(dev, adapter->buffer_list_dma)) {
			dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
					DMA_BIDIRECTIONAL);
			adapter->buffer_list_dma = DMA_ERROR_CODE;
		}
		free_page((unsigned long)adapter->buffer_list_addr);
		adapter->buffer_list_addr = NULL;
	}

	if (adapter->filter_list_addr != NULL) {
		if (!dma_mapping_error(dev, adapter->filter_list_dma)) {
			dma_unmap_single(dev, adapter->filter_list_dma, 4096,
					DMA_BIDIRECTIONAL);
			adapter->filter_list_dma = DMA_ERROR_CODE;
		}
		free_page((unsigned long)adapter->filter_list_addr);
		adapter->filter_list_addr = NULL;
	}

	if (adapter->rx_queue.queue_addr != NULL) {
		dma_free_coherent(dev, adapter->rx_queue.queue_len,
				  adapter->rx_queue.queue_addr,
				  adapter->rx_queue.queue_dma);
		adapter->rx_queue.queue_addr = NULL;
	}

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		if (adapter->rx_buff_pool[i].active)
			ibmveth_free_buffer_pool(adapter,
						 &adapter->rx_buff_pool[i]);

	if (adapter->bounce_buffer != NULL) {
		if (!dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
			dma_unmap_single(&adapter->vdev->dev,
					adapter->bounce_buffer_dma,
					adapter->netdev->mtu + IBMVETH_BUFF_OH,
					DMA_BIDIRECTIONAL);
			adapter->bounce_buffer_dma = DMA_ERROR_CODE;
		}
		kfree(adapter->bounce_buffer);
		adapter->bounce_buffer = NULL;
	}
}

static int ibmveth_register_logical_lan(struct ibmveth_adapter *adapter,
        union ibmveth_buf_desc rxq_desc, u64 mac_address)
{
	int rc, try_again = 1;

	/*
	 * After a kexec the adapter will still be open, so our attempt to
	 * open it will fail. So if we get a failure we free the adapter and
	 * try again, but only once.
	 */
retry:
	rc = h_register_logical_lan(adapter->vdev->unit_address,
				    adapter->buffer_list_dma, rxq_desc.desc,
				    adapter->filter_list_dma, mac_address);

	if (rc != H_SUCCESS && try_again) {
		do {
			rc = h_free_logical_lan(adapter->vdev->unit_address);
		} while (H_IS_LONG_BUSY(rc) || (rc == H_BUSY));

		try_again = 0;
		goto retry;
	}

	return rc;
}

static int ibmveth_open(struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	u64 mac_address = 0;
	int rxq_entries = 1;
	unsigned long lpar_rc;
	int rc;
	union ibmveth_buf_desc rxq_desc;
	int i;
	struct device *dev;

	netdev_dbg(netdev, "open starting\n");

	napi_enable(&adapter->napi);

	for(i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		rxq_entries += adapter->rx_buff_pool[i].size;

	adapter->buffer_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
	adapter->filter_list_addr = (void*) get_zeroed_page(GFP_KERNEL);

	if (!adapter->buffer_list_addr || !adapter->filter_list_addr) {
		netdev_err(netdev, "unable to allocate filter or buffer list "
			   "pages\n");
		rc = -ENOMEM;
		goto err_out;
	}

	dev = &adapter->vdev->dev;

	adapter->rx_queue.queue_len = sizeof(struct ibmveth_rx_q_entry) *
						rxq_entries;
	adapter->rx_queue.queue_addr =
		dma_alloc_coherent(dev, adapter->rx_queue.queue_len,
				   &adapter->rx_queue.queue_dma, GFP_KERNEL);
	if (!adapter->rx_queue.queue_addr) {
		rc = -ENOMEM;
		goto err_out;
	}

	adapter->buffer_list_dma = dma_map_single(dev,
			adapter->buffer_list_addr, 4096, DMA_BIDIRECTIONAL);
	adapter->filter_list_dma = dma_map_single(dev,
			adapter->filter_list_addr, 4096, DMA_BIDIRECTIONAL);

	if ((dma_mapping_error(dev, adapter->buffer_list_dma)) ||
	    (dma_mapping_error(dev, adapter->filter_list_dma))) {
		netdev_err(netdev, "unable to map filter or buffer list "
			   "pages\n");
		rc = -ENOMEM;
		goto err_out;
	}

	adapter->rx_queue.index = 0;
	adapter->rx_queue.num_slots = rxq_entries;
	adapter->rx_queue.toggle = 1;

	memcpy(&mac_address, netdev->dev_addr, netdev->addr_len);
	mac_address = mac_address >> 16;

	rxq_desc.fields.flags_len = IBMVETH_BUF_VALID |
					adapter->rx_queue.queue_len;
	rxq_desc.fields.address = adapter->rx_queue.queue_dma;

	netdev_dbg(netdev, "buffer list @ 0x%p\n", adapter->buffer_list_addr);
	netdev_dbg(netdev, "filter list @ 0x%p\n", adapter->filter_list_addr);
	netdev_dbg(netdev, "receive q   @ 0x%p\n", adapter->rx_queue.queue_addr);

	h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);

	lpar_rc = ibmveth_register_logical_lan(adapter, rxq_desc, mac_address);

	if (lpar_rc != H_SUCCESS) {
		netdev_err(netdev, "h_register_logical_lan failed with %ld\n",
			   lpar_rc);
		netdev_err(netdev, "buffer TCE:0x%llx filter TCE:0x%llx rxq "
			   "desc:0x%llx MAC:0x%llx\n",
				     adapter->buffer_list_dma,
				     adapter->filter_list_dma,
				     rxq_desc.desc,
				     mac_address);
		rc = -ENONET;
		goto err_out;
	}

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		if (!adapter->rx_buff_pool[i].active)
			continue;
		if (ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[i])) {
			netdev_err(netdev, "unable to alloc pool\n");
			adapter->rx_buff_pool[i].active = 0;
			rc = -ENOMEM;
			goto err_out;
		}
	}

	netdev_dbg(netdev, "registering irq 0x%x\n", netdev->irq);
	rc = request_irq(netdev->irq, ibmveth_interrupt, 0, netdev->name,
			 netdev);
	if (rc != 0) {
		netdev_err(netdev, "unable to request irq 0x%x, rc %d\n",
			   netdev->irq, rc);
		do {
			lpar_rc = h_free_logical_lan(adapter->vdev->unit_address);
		} while (H_IS_LONG_BUSY(lpar_rc) || (lpar_rc == H_BUSY));

		goto err_out;
	}

	adapter->bounce_buffer =
	    kmalloc(netdev->mtu + IBMVETH_BUFF_OH, GFP_KERNEL);
	if (!adapter->bounce_buffer) {
		rc = -ENOMEM;
		goto err_out_free_irq;
	}
	adapter->bounce_buffer_dma =
	    dma_map_single(&adapter->vdev->dev, adapter->bounce_buffer,
			   netdev->mtu + IBMVETH_BUFF_OH, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
		netdev_err(netdev, "unable to map bounce buffer\n");
		rc = -ENOMEM;
		goto err_out_free_irq;
	}

	netdev_dbg(netdev, "initial replenish cycle\n");
	ibmveth_interrupt(netdev->irq, netdev);

	netif_start_queue(netdev);

	netdev_dbg(netdev, "open complete\n");

	return 0;

err_out_free_irq:
	free_irq(netdev->irq, netdev);
err_out:
	ibmveth_cleanup(adapter);
	napi_disable(&adapter->napi);
	return rc;
}

static int ibmveth_close(struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	long lpar_rc;

	netdev_dbg(netdev, "close starting\n");

	napi_disable(&adapter->napi);

	if (!adapter->pool_config)
		netif_stop_queue(netdev);

	h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);

	do {
		lpar_rc = h_free_logical_lan(adapter->vdev->unit_address);
	} while (H_IS_LONG_BUSY(lpar_rc) || (lpar_rc == H_BUSY));

	if (lpar_rc != H_SUCCESS) {
		netdev_err(netdev, "h_free_logical_lan failed with %lx, "
			   "continuing with close\n", lpar_rc);
	}

	free_irq(netdev->irq, netdev);

	ibmveth_update_rx_no_buffer(adapter);

	ibmveth_cleanup(adapter);

	netdev_dbg(netdev, "close complete\n");

	return 0;
}

static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
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

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, ibmveth_driver_name, sizeof(info->driver));
	strlcpy(info->version, ibmveth_driver_version, sizeof(info->version));
}

static netdev_features_t ibmveth_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	/*
	 * Since the ibmveth firmware interface does not have the
	 * concept of separate tx/rx checksum offload enable, if rx
	 * checksum is disabled we also have to disable tx checksum
	 * offload. Once we disable rx checksum offload, we are no
	 * longer allowed to send tx buffers that are not properly
	 * checksummed.
	 */

	if (!(features & NETIF_F_RXCSUM))
		features &= ~NETIF_F_ALL_CSUM;

	return features;
}

static int ibmveth_set_csum_offload(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	unsigned long set_attr, clr_attr, ret_attr;
	unsigned long set_attr6, clr_attr6;
	long ret, ret4, ret6;
	int rc1 = 0, rc2 = 0;
	int restart = 0;

	if (netif_running(dev)) {
		restart = 1;
		adapter->pool_config = 1;
		ibmveth_close(dev);
		adapter->pool_config = 0;
	}

	set_attr = 0;
	clr_attr = 0;
	set_attr6 = 0;
	clr_attr6 = 0;

	if (data) {
		set_attr = IBMVETH_ILLAN_IPV4_TCP_CSUM;
		set_attr6 = IBMVETH_ILLAN_IPV6_TCP_CSUM;
	} else {
		clr_attr = IBMVETH_ILLAN_IPV4_TCP_CSUM;
		clr_attr6 = IBMVETH_ILLAN_IPV6_TCP_CSUM;
	}

	ret = h_illan_attributes(adapter->vdev->unit_address, 0, 0, &ret_attr);

	if (ret == H_SUCCESS && !(ret_attr & IBMVETH_ILLAN_ACTIVE_TRUNK) &&
	    !(ret_attr & IBMVETH_ILLAN_TRUNK_PRI_MASK) &&
	    (ret_attr & IBMVETH_ILLAN_PADDED_PKT_CSUM)) {
		ret4 = h_illan_attributes(adapter->vdev->unit_address, clr_attr,
					 set_attr, &ret_attr);

		if (ret4 != H_SUCCESS) {
			netdev_err(dev, "unable to change IPv4 checksum "
					"offload settings. %d rc=%ld\n",
					data, ret4);

			h_illan_attributes(adapter->vdev->unit_address,
					   set_attr, clr_attr, &ret_attr);

			if (data == 1)
				dev->features &= ~NETIF_F_IP_CSUM;

		} else {
			adapter->fw_ipv4_csum_support = data;
		}

		ret6 = h_illan_attributes(adapter->vdev->unit_address,
					 clr_attr6, set_attr6, &ret_attr);

		if (ret6 != H_SUCCESS) {
			netdev_err(dev, "unable to change IPv6 checksum "
					"offload settings. %d rc=%ld\n",
					data, ret6);

			h_illan_attributes(adapter->vdev->unit_address,
					   set_attr6, clr_attr6, &ret_attr);

			if (data == 1)
				dev->features &= ~NETIF_F_IPV6_CSUM;

		} else
			adapter->fw_ipv6_csum_support = data;

		if (ret4 == H_SUCCESS || ret6 == H_SUCCESS)
			adapter->rx_csum = data;
		else
			rc1 = -EIO;
	} else {
		rc1 = -EIO;
		netdev_err(dev, "unable to change checksum offload settings."
				     " %d rc=%ld ret_attr=%lx\n", data, ret,
				     ret_attr);
	}

	if (restart)
		rc2 = ibmveth_open(dev);

	return rc1 ? rc1 : rc2;
}

static int ibmveth_set_features(struct net_device *dev,
	netdev_features_t features)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	int rx_csum = !!(features & NETIF_F_RXCSUM);
	int rc;

	if (rx_csum == adapter->rx_csum)
		return 0;

	rc = ibmveth_set_csum_offload(dev, rx_csum);
	if (rc && !adapter->rx_csum)
		dev->features = features & ~(NETIF_F_ALL_CSUM | NETIF_F_RXCSUM);

	return rc;
}

static void ibmveth_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ibmveth_stats); i++, data += ETH_GSTRING_LEN)
		memcpy(data, ibmveth_stats[i].name, ETH_GSTRING_LEN);
}

static int ibmveth_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ibmveth_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void ibmveth_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	int i;
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	for (i = 0; i < ARRAY_SIZE(ibmveth_stats); i++)
		data[i] = IBMVETH_GET_STAT(adapter, ibmveth_stats[i].offset);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_settings		= netdev_get_settings,
	.get_link		= ethtool_op_get_link,
	.get_strings		= ibmveth_get_strings,
	.get_sset_count		= ibmveth_get_sset_count,
	.get_ethtool_stats	= ibmveth_get_ethtool_stats,
};

static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

#define page_offset(v) ((unsigned long)(v) & ((1 << 12) - 1))

static int ibmveth_send(struct ibmveth_adapter *adapter,
			union ibmveth_buf_desc *descs)
{
	unsigned long correlator;
	unsigned int retry_count;
	unsigned long ret;

	/*
	 * The retry count sets a maximum for the number of broadcast and
	 * multicast destinations within the system.
	 */
	retry_count = 1024;
	correlator = 0;
	do {
		ret = h_send_logical_lan(adapter->vdev->unit_address,
					     descs[0].desc, descs[1].desc,
					     descs[2].desc, descs[3].desc,
					     descs[4].desc, descs[5].desc,
					     correlator, &correlator);
	} while ((ret == H_BUSY) && (retry_count--));

	if (ret != H_SUCCESS && ret != H_DROPPED) {
		netdev_err(adapter->netdev, "tx: h_send_logical_lan failed "
			   "with rc=%ld\n", ret);
		return 1;
	}

	return 0;
}

static netdev_tx_t ibmveth_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	unsigned int desc_flags;
	union ibmveth_buf_desc descs[6];
	int last, i;
	int force_bounce = 0;
	dma_addr_t dma_addr;

	/*
	 * veth handles a maximum of 6 segments including the header, so
	 * we have to linearize the skb if there are more than this.
	 */
	if (skb_shinfo(skb)->nr_frags > 5 && __skb_linearize(skb)) {
		netdev->stats.tx_dropped++;
		goto out;
	}

	/* veth can't checksum offload UDP */
	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    ((skb->protocol == htons(ETH_P_IP) &&
	      ip_hdr(skb)->protocol != IPPROTO_TCP) ||
	     (skb->protocol == htons(ETH_P_IPV6) &&
	      ipv6_hdr(skb)->nexthdr != IPPROTO_TCP)) &&
	    skb_checksum_help(skb)) {

		netdev_err(netdev, "tx: failed to checksum packet\n");
		netdev->stats.tx_dropped++;
		goto out;
	}

	desc_flags = IBMVETH_BUF_VALID;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		unsigned char *buf = skb_transport_header(skb) +
						skb->csum_offset;

		desc_flags |= (IBMVETH_BUF_NO_CSUM | IBMVETH_BUF_CSUM_GOOD);

		/* Need to zero out the checksum */
		buf[0] = 0;
		buf[1] = 0;
	}

retry_bounce:
	memset(descs, 0, sizeof(descs));

	/*
	 * If a linear packet is below the rx threshold then
	 * copy it into the static bounce buffer. This avoids the
	 * cost of a TCE insert and remove.
	 */
	if (force_bounce || (!skb_is_nonlinear(skb) &&
				(skb->len < tx_copybreak))) {
		skb_copy_from_linear_data(skb, adapter->bounce_buffer,
					  skb->len);

		descs[0].fields.flags_len = desc_flags | skb->len;
		descs[0].fields.address = adapter->bounce_buffer_dma;

		if (ibmveth_send(adapter, descs)) {
			adapter->tx_send_failed++;
			netdev->stats.tx_dropped++;
		} else {
			netdev->stats.tx_packets++;
			netdev->stats.tx_bytes += skb->len;
		}

		goto out;
	}

	/* Map the header */
	dma_addr = dma_map_single(&adapter->vdev->dev, skb->data,
				  skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->vdev->dev, dma_addr))
		goto map_failed;

	descs[0].fields.flags_len = desc_flags | skb_headlen(skb);
	descs[0].fields.address = dma_addr;

	/* Map the frags */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		dma_addr = skb_frag_dma_map(&adapter->vdev->dev, frag, 0,
					    skb_frag_size(frag), DMA_TO_DEVICE);

		if (dma_mapping_error(&adapter->vdev->dev, dma_addr))
			goto map_failed_frags;

		descs[i+1].fields.flags_len = desc_flags | skb_frag_size(frag);
		descs[i+1].fields.address = dma_addr;
	}

	if (ibmveth_send(adapter, descs)) {
		adapter->tx_send_failed++;
		netdev->stats.tx_dropped++;
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += skb->len;
	}

	dma_unmap_single(&adapter->vdev->dev,
			 descs[0].fields.address,
			 descs[0].fields.flags_len & IBMVETH_BUF_LEN_MASK,
			 DMA_TO_DEVICE);

	for (i = 1; i < skb_shinfo(skb)->nr_frags + 1; i++)
		dma_unmap_page(&adapter->vdev->dev, descs[i].fields.address,
			       descs[i].fields.flags_len & IBMVETH_BUF_LEN_MASK,
			       DMA_TO_DEVICE);

out:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;

map_failed_frags:
	last = i+1;
	for (i = 0; i < last; i++)
		dma_unmap_page(&adapter->vdev->dev, descs[i].fields.address,
			       descs[i].fields.flags_len & IBMVETH_BUF_LEN_MASK,
			       DMA_TO_DEVICE);

map_failed:
	if (!firmware_has_feature(FW_FEATURE_CMO))
		netdev_err(netdev, "tx: unable to map xmit buffer\n");
	adapter->tx_map_failed++;
	skb_linearize(skb);
	force_bounce = 1;
	goto retry_bounce;
}

static int ibmveth_poll(struct napi_struct *napi, int budget)
{
	struct ibmveth_adapter *adapter =
			container_of(napi, struct ibmveth_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int frames_processed = 0;
	unsigned long lpar_rc;

restart_poll:
	do {
		if (!ibmveth_rxq_pending_buffer(adapter))
			break;

		smp_rmb();
		if (!ibmveth_rxq_buffer_valid(adapter)) {
			wmb(); /* suggested by larson1 */
			adapter->rx_invalid_buffer++;
			netdev_dbg(netdev, "recycling invalid buffer\n");
			ibmveth_rxq_recycle_buffer(adapter);
		} else {
			struct sk_buff *skb, *new_skb;
			int length = ibmveth_rxq_frame_length(adapter);
			int offset = ibmveth_rxq_frame_offset(adapter);
			int csum_good = ibmveth_rxq_csum_good(adapter);

			skb = ibmveth_rxq_get_buffer(adapter);

			new_skb = NULL;
			if (length < rx_copybreak)
				new_skb = netdev_alloc_skb(netdev, length);

			if (new_skb) {
				skb_copy_to_linear_data(new_skb,
							skb->data + offset,
							length);
				if (rx_flush)
					ibmveth_flush_buffer(skb->data,
						length + offset);
				if (!ibmveth_rxq_recycle_buffer(adapter))
					kfree_skb(skb);
				skb = new_skb;
			} else {
				ibmveth_rxq_harvest_buffer(adapter);
				skb_reserve(skb, offset);
			}

			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, netdev);

			if (csum_good)
				skb->ip_summed = CHECKSUM_UNNECESSARY;

			netif_receive_skb(skb);	/* send it up */

			netdev->stats.rx_packets++;
			netdev->stats.rx_bytes += length;
			frames_processed++;
		}
	} while (frames_processed < budget);

	ibmveth_replenish_task(adapter);

	if (frames_processed < budget) {
		/* We think we are done - reenable interrupts,
		 * then check once more to make sure we are done.
		 */
		lpar_rc = h_vio_signal(adapter->vdev->unit_address,
				       VIO_IRQ_ENABLE);

		BUG_ON(lpar_rc != H_SUCCESS);

		napi_complete(napi);

		if (ibmveth_rxq_pending_buffer(adapter) &&
		    napi_reschedule(napi)) {
			lpar_rc = h_vio_signal(adapter->vdev->unit_address,
					       VIO_IRQ_DISABLE);
			goto restart_poll;
		}
	}

	return frames_processed;
}

static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance)
{
	struct net_device *netdev = dev_instance;
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	unsigned long lpar_rc;

	if (napi_schedule_prep(&adapter->napi)) {
		lpar_rc = h_vio_signal(adapter->vdev->unit_address,
				       VIO_IRQ_DISABLE);
		BUG_ON(lpar_rc != H_SUCCESS);
		__napi_schedule(&adapter->napi);
	}
	return IRQ_HANDLED;
}

static void ibmveth_set_multicast_list(struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	unsigned long lpar_rc;

	if ((netdev->flags & IFF_PROMISC) ||
	    (netdev_mc_count(netdev) > adapter->mcastFilterSize)) {
		lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					   IbmVethMcastEnableRecv |
					   IbmVethMcastDisableFiltering,
					   0);
		if (lpar_rc != H_SUCCESS) {
			netdev_err(netdev, "h_multicast_ctrl rc=%ld when "
				   "entering promisc mode\n", lpar_rc);
		}
	} else {
		struct netdev_hw_addr *ha;
		/* clear the filter table & disable filtering */
		lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					   IbmVethMcastEnableRecv |
					   IbmVethMcastDisableFiltering |
					   IbmVethMcastClearFilterTable,
					   0);
		if (lpar_rc != H_SUCCESS) {
			netdev_err(netdev, "h_multicast_ctrl rc=%ld when "
				   "attempting to clear filter table\n",
				   lpar_rc);
		}
		/* add the addresses to the filter table */
		netdev_for_each_mc_addr(ha, netdev) {
			/* add the multicast address to the filter table */
			unsigned long mcast_addr = 0;
			memcpy(((char *)&mcast_addr)+2, ha->addr, 6);
			lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
						   IbmVethMcastAddFilter,
						   mcast_addr);
			if (lpar_rc != H_SUCCESS) {
				netdev_err(netdev, "h_multicast_ctrl rc=%ld "
					   "when adding an entry to the filter "
					   "table\n", lpar_rc);
			}
		}

		/* re-enable filtering */
		lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					   IbmVethMcastEnableFiltering,
					   0);
		if (lpar_rc != H_SUCCESS) {
			netdev_err(netdev, "h_multicast_ctrl rc=%ld when "
				   "enabling filtering\n", lpar_rc);
		}
	}
}

static int ibmveth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	struct vio_dev *viodev = adapter->vdev;
	int new_mtu_oh = new_mtu + IBMVETH_BUFF_OH;
	int i, rc;
	int need_restart = 0;

	if (new_mtu < IBMVETH_MIN_MTU)
		return -EINVAL;

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		if (new_mtu_oh < adapter->rx_buff_pool[i].buff_size)
			break;

	if (i == IBMVETH_NUM_BUFF_POOLS)
		return -EINVAL;

	/* Deactivate all the buffer pools so that the next loop can activate
	   only the buffer pools necessary to hold the new MTU */
	if (netif_running(adapter->netdev)) {
		need_restart = 1;
		adapter->pool_config = 1;
		ibmveth_close(adapter->netdev);
		adapter->pool_config = 0;
	}

	/* Look for an active buffer pool that can hold the new MTU */
	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		adapter->rx_buff_pool[i].active = 1;

		if (new_mtu_oh < adapter->rx_buff_pool[i].buff_size) {
			dev->mtu = new_mtu;
			vio_cmo_set_dev_desired(viodev,
						ibmveth_get_desired_dma
						(viodev));
			if (need_restart) {
				return ibmveth_open(adapter->netdev);
			}
			return 0;
		}
	}

	if (need_restart && (rc = ibmveth_open(adapter->netdev)))
		return rc;

	return -EINVAL;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ibmveth_poll_controller(struct net_device *dev)
{
	ibmveth_replenish_task(netdev_priv(dev));
	ibmveth_interrupt(dev->irq, dev);
}
#endif

/**
 * ibmveth_get_desired_dma - Calculate IO memory desired by the driver
 *
 * @vdev: struct vio_dev for the device whose desired IO mem is to be returned
 *
 * Return value:
 *	Number of bytes of IO data the driver will need to perform well.
 */
static unsigned long ibmveth_get_desired_dma(struct vio_dev *vdev)
{
	struct net_device *netdev = dev_get_drvdata(&vdev->dev);
	struct ibmveth_adapter *adapter;
	unsigned long ret;
	int i;
	int rxqentries = 1;

	/* netdev inits at probe time along with the structures we need below*/
	if (netdev == NULL)
		return IOMMU_PAGE_ALIGN(IBMVETH_IO_ENTITLEMENT_DEFAULT);

	adapter = netdev_priv(netdev);

	ret = IBMVETH_BUFF_LIST_SIZE + IBMVETH_FILT_LIST_SIZE;
	ret += IOMMU_PAGE_ALIGN(netdev->mtu);

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		/* add the size of the active receive buffers */
		if (adapter->rx_buff_pool[i].active)
			ret +=
			    adapter->rx_buff_pool[i].size *
			    IOMMU_PAGE_ALIGN(adapter->rx_buff_pool[i].
			            buff_size);
		rxqentries += adapter->rx_buff_pool[i].size;
	}
	/* add the size of the receive queue entries */
	ret += IOMMU_PAGE_ALIGN(rxqentries * sizeof(struct ibmveth_rx_q_entry));

	return ret;
}

static const struct net_device_ops ibmveth_netdev_ops = {
	.ndo_open		= ibmveth_open,
	.ndo_stop		= ibmveth_close,
	.ndo_start_xmit		= ibmveth_start_xmit,
	.ndo_set_rx_mode	= ibmveth_set_multicast_list,
	.ndo_do_ioctl		= ibmveth_ioctl,
	.ndo_change_mtu		= ibmveth_change_mtu,
	.ndo_fix_features	= ibmveth_fix_features,
	.ndo_set_features	= ibmveth_set_features,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ibmveth_poll_controller,
#endif
};

static int ibmveth_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	int rc, i, mac_len;
	struct net_device *netdev;
	struct ibmveth_adapter *adapter;
	unsigned char *mac_addr_p;
	unsigned int *mcastFilterSize_p;

	dev_dbg(&dev->dev, "entering ibmveth_probe for UA 0x%x\n",
		dev->unit_address);

	mac_addr_p = (unsigned char *)vio_get_attribute(dev, VETH_MAC_ADDR,
							&mac_len);
	if (!mac_addr_p) {
		dev_err(&dev->dev, "Can't find VETH_MAC_ADDR attribute\n");
		return -EINVAL;
	}
	/* Workaround for old/broken pHyp */
	if (mac_len == 8)
		mac_addr_p += 2;
	else if (mac_len != 6) {
		dev_err(&dev->dev, "VETH_MAC_ADDR attribute wrong len %d\n",
			mac_len);
		return -EINVAL;
	}

	mcastFilterSize_p = (unsigned int *)vio_get_attribute(dev,
						VETH_MCAST_FILTER_SIZE, NULL);
	if (!mcastFilterSize_p) {
		dev_err(&dev->dev, "Can't find VETH_MCAST_FILTER_SIZE "
			"attribute\n");
		return -EINVAL;
	}

	netdev = alloc_etherdev(sizeof(struct ibmveth_adapter));

	if (!netdev)
		return -ENOMEM;

	adapter = netdev_priv(netdev);
	dev_set_drvdata(&dev->dev, netdev);

	adapter->vdev = dev;
	adapter->netdev = netdev;
	adapter->mcastFilterSize = *mcastFilterSize_p;
	adapter->pool_config = 0;

	netif_napi_add(netdev, &adapter->napi, ibmveth_poll, 16);

	adapter->mac_addr = 0;
	memcpy(&adapter->mac_addr, mac_addr_p, 6);

	netdev->irq = dev->irq;
	netdev->netdev_ops = &ibmveth_netdev_ops;
	netdev->ethtool_ops = &netdev_ethtool_ops;
	SET_NETDEV_DEV(netdev, &dev->dev);
	netdev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	netdev->features |= netdev->hw_features;

	memcpy(netdev->dev_addr, &adapter->mac_addr, netdev->addr_len);

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		struct kobject *kobj = &adapter->rx_buff_pool[i].kobj;
		int error;

		ibmveth_init_buffer_pool(&adapter->rx_buff_pool[i], i,
					 pool_count[i], pool_size[i],
					 pool_active[i]);
		error = kobject_init_and_add(kobj, &ktype_veth_pool,
					     &dev->dev.kobj, "pool%d", i);
		if (!error)
			kobject_uevent(kobj, KOBJ_ADD);
	}

	netdev_dbg(netdev, "adapter @ 0x%p\n", adapter);

	adapter->buffer_list_dma = DMA_ERROR_CODE;
	adapter->filter_list_dma = DMA_ERROR_CODE;
	adapter->rx_queue.queue_dma = DMA_ERROR_CODE;

	netdev_dbg(netdev, "registering netdev...\n");

	ibmveth_set_features(netdev, netdev->features);

	rc = register_netdev(netdev);

	if (rc) {
		netdev_dbg(netdev, "failed to register netdev rc=%d\n", rc);
		free_netdev(netdev);
		return rc;
	}

	netdev_dbg(netdev, "registered\n");

	return 0;
}

static int ibmveth_remove(struct vio_dev *dev)
{
	struct net_device *netdev = dev_get_drvdata(&dev->dev);
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	int i;

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		kobject_put(&adapter->rx_buff_pool[i].kobj);

	unregister_netdev(netdev);

	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static struct attribute veth_active_attr;
static struct attribute veth_num_attr;
static struct attribute veth_size_attr;

static ssize_t veth_pool_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct ibmveth_buff_pool *pool = container_of(kobj,
						      struct ibmveth_buff_pool,
						      kobj);

	if (attr == &veth_active_attr)
		return sprintf(buf, "%d\n", pool->active);
	else if (attr == &veth_num_attr)
		return sprintf(buf, "%d\n", pool->size);
	else if (attr == &veth_size_attr)
		return sprintf(buf, "%d\n", pool->buff_size);
	return 0;
}

static ssize_t veth_pool_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t count)
{
	struct ibmveth_buff_pool *pool = container_of(kobj,
						      struct ibmveth_buff_pool,
						      kobj);
	struct net_device *netdev = dev_get_drvdata(
	    container_of(kobj->parent, struct device, kobj));
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	long value = simple_strtol(buf, NULL, 10);
	long rc;

	if (attr == &veth_active_attr) {
		if (value && !pool->active) {
			if (netif_running(netdev)) {
				if (ibmveth_alloc_buffer_pool(pool)) {
					netdev_err(netdev,
						   "unable to alloc pool\n");
					return -ENOMEM;
				}
				pool->active = 1;
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else {
				pool->active = 1;
			}
		} else if (!value && pool->active) {
			int mtu = netdev->mtu + IBMVETH_BUFF_OH;
			int i;
			/* Make sure there is a buffer pool with buffers that
			   can hold a packet of the size of the MTU */
			for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
				if (pool == &adapter->rx_buff_pool[i])
					continue;
				if (!adapter->rx_buff_pool[i].active)
					continue;
				if (mtu <= adapter->rx_buff_pool[i].buff_size)
					break;
			}

			if (i == IBMVETH_NUM_BUFF_POOLS) {
				netdev_err(netdev, "no active pool >= MTU\n");
				return -EPERM;
			}

			if (netif_running(netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				pool->active = 0;
				adapter->pool_config = 0;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			}
			pool->active = 0;
		}
	} else if (attr == &veth_num_attr) {
		if (value <= 0 || value > IBMVETH_MAX_POOL_COUNT) {
			return -EINVAL;
		} else {
			if (netif_running(netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				pool->size = value;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else {
				pool->size = value;
			}
		}
	} else if (attr == &veth_size_attr) {
		if (value <= IBMVETH_BUFF_OH || value > IBMVETH_MAX_BUF_SIZE) {
			return -EINVAL;
		} else {
			if (netif_running(netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				pool->buff_size = value;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else {
				pool->buff_size = value;
			}
		}
	}

	/* kick the interrupt handler to allocate/deallocate pools */
	ibmveth_interrupt(netdev->irq, netdev);
	return count;
}


#define ATTR(_name, _mode)				\
	struct attribute veth_##_name##_attr = {	\
	.name = __stringify(_name), .mode = _mode,	\
	};

static ATTR(active, 0644);
static ATTR(num, 0644);
static ATTR(size, 0644);

static struct attribute *veth_pool_attrs[] = {
	&veth_active_attr,
	&veth_num_attr,
	&veth_size_attr,
	NULL,
};

static const struct sysfs_ops veth_pool_ops = {
	.show   = veth_pool_show,
	.store  = veth_pool_store,
};

static struct kobj_type ktype_veth_pool = {
	.release        = NULL,
	.sysfs_ops      = &veth_pool_ops,
	.default_attrs  = veth_pool_attrs,
};

static int ibmveth_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	ibmveth_interrupt(netdev->irq, netdev);
	return 0;
}

static struct vio_device_id ibmveth_device_table[] = {
	{ "network", "IBM,l-lan"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmveth_device_table);

static struct dev_pm_ops ibmveth_pm_ops = {
	.resume = ibmveth_resume
};

static struct vio_driver ibmveth_driver = {
	.id_table	= ibmveth_device_table,
	.probe		= ibmveth_probe,
	.remove		= ibmveth_remove,
	.get_desired_dma = ibmveth_get_desired_dma,
	.name		= ibmveth_driver_name,
	.pm		= &ibmveth_pm_ops,
};

static int __init ibmveth_module_init(void)
{
	printk(KERN_DEBUG "%s: %s %s\n", ibmveth_driver_name,
	       ibmveth_driver_string, ibmveth_driver_version);

	return vio_register_driver(&ibmveth_driver);
}

static void __exit ibmveth_module_exit(void)
{
	vio_unregister_driver(&ibmveth_driver);
}

module_init(ibmveth_module_init);
module_exit(ibmveth_module_exit);
