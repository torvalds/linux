// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IBM Power Virtual Ethernet Device Driver
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
#include <net/tcp.h>
#include <net/ip6_checksum.h>

#include "ibmveth.h"

static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance);
static unsigned long ibmveth_get_desired_dma(struct vio_dev *vdev);

static struct kobj_type ktype_veth_pool;


static const char ibmveth_driver_name[] = "ibmveth";
static const char ibmveth_driver_string[] = "IBM Power Virtual Ethernet Driver";
#define ibmveth_driver_version "1.06"

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

static bool old_large_send __read_mostly;
module_param(old_large_send, bool, 0444);
MODULE_PARM_DESC(old_large_send,
	"Use old large send method on firmware that supports the new method");

struct ibmveth_stat {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define IBMVETH_STAT_OFF(stat) offsetof(struct ibmveth_adapter, stat)
#define IBMVETH_GET_STAT(a, off) *((u64 *)(((unsigned long)(a)) + off))

static struct ibmveth_stat ibmveth_stats[] = {
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
	{ "tx_large_packets", IBMVETH_STAT_OFF(tx_large_packets) },
	{ "rx_large_packets", IBMVETH_STAT_OFF(rx_large_packets) },
	{ "fw_enabled_large_send", IBMVETH_STAT_OFF(fw_large_send_support) }
};

/* simple methods of getting data from the current rxq entry */
static inline u32 ibmveth_rxq_flags(struct ibmveth_adapter *adapter)
{
	return be32_to_cpu(adapter->rx_queue.queue_addr[adapter->rx_queue.index].flags_off);
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

static inline int ibmveth_rxq_large_packet(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_LRG_PKT;
}

static inline int ibmveth_rxq_frame_length(struct ibmveth_adapter *adapter)
{
	return be32_to_cpu(adapter->rx_queue.queue_addr[adapter->rx_queue.index].length);
}

static inline int ibmveth_rxq_csum_good(struct ibmveth_adapter *adapter)
{
	return ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_CSUM_GOOD;
}

static unsigned int ibmveth_real_max_tx_queues(void)
{
	unsigned int n_cpu = num_online_cpus();

	return min(n_cpu, IBMVETH_MAX_QUEUES);
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

	pool->free_map = kmalloc_array(pool->size, sizeof(u16), GFP_KERNEL);

	if (!pool->free_map)
		return -1;

	pool->dma_addr = kcalloc(pool->size, sizeof(dma_addr_t), GFP_KERNEL);
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
		asm("dcbf %0,%1,1" :: "b" (addr), "r" (offset));
}

/* replenish the buffers for a pool.  note that we don't need to
 * skb_reserve these since they are used for incoming...
 */
static void ibmveth_replenish_buffer_pool(struct ibmveth_adapter *adapter,
					  struct ibmveth_buff_pool *pool)
{
	union ibmveth_buf_desc descs[IBMVETH_MAX_RX_PER_HCALL] = {0};
	u32 remaining = pool->size - atomic_read(&pool->available);
	u64 correlators[IBMVETH_MAX_RX_PER_HCALL] = {0};
	unsigned long lpar_rc;
	u32 buffers_added = 0;
	u32 i, filled, batch;
	struct vio_dev *vdev;
	dma_addr_t dma_addr;
	struct device *dev;
	u32 index;

	vdev = adapter->vdev;
	dev = &vdev->dev;

	mb();

	batch = adapter->rx_buffers_per_hcall;

	while (remaining > 0) {
		unsigned int free_index = pool->consumer_index;

		/* Fill a batch of descriptors */
		for (filled = 0; filled < min(remaining, batch); filled++) {
			index = pool->free_map[free_index];
			if (WARN_ON(index == IBM_VETH_INVALID_MAP)) {
				adapter->replenish_add_buff_failure++;
				netdev_info(adapter->netdev,
					    "Invalid map index %u, reset\n",
					    index);
				schedule_work(&adapter->work);
				break;
			}

			if (!pool->skbuff[index]) {
				struct sk_buff *skb = NULL;

				skb = netdev_alloc_skb(adapter->netdev,
						       pool->buff_size);
				if (!skb) {
					adapter->replenish_no_mem++;
					adapter->replenish_add_buff_failure++;
					break;
				}

				dma_addr = dma_map_single(dev, skb->data,
							  pool->buff_size,
							  DMA_FROM_DEVICE);
				if (dma_mapping_error(dev, dma_addr)) {
					dev_kfree_skb_any(skb);
					adapter->replenish_add_buff_failure++;
					break;
				}

				pool->dma_addr[index] = dma_addr;
				pool->skbuff[index] = skb;
			} else {
				/* re-use case */
				dma_addr = pool->dma_addr[index];
			}

			if (rx_flush) {
				unsigned int len;

				len = adapter->netdev->mtu + IBMVETH_BUFF_OH;
				len = min(pool->buff_size, len);
				ibmveth_flush_buffer(pool->skbuff[index]->data,
						     len);
			}

			descs[filled].fields.flags_len = IBMVETH_BUF_VALID |
							  pool->buff_size;
			descs[filled].fields.address = dma_addr;

			correlators[filled] = ((u64)pool->index << 32) | index;
			*(u64 *)pool->skbuff[index]->data = correlators[filled];

			free_index++;
			if (free_index >= pool->size)
				free_index = 0;
		}

		if (!filled)
			break;

		/* single buffer case*/
		if (filled == 1)
			lpar_rc = h_add_logical_lan_buffer(vdev->unit_address,
							   descs[0].desc);
		else
			/* Multi-buffer hcall */
			lpar_rc = h_add_logical_lan_buffers(vdev->unit_address,
							    descs[0].desc,
							    descs[1].desc,
							    descs[2].desc,
							    descs[3].desc,
							    descs[4].desc,
							    descs[5].desc,
							    descs[6].desc,
							    descs[7].desc);
		if (lpar_rc != H_SUCCESS) {
			dev_warn_ratelimited(dev,
					     "RX h_add_logical_lan failed: filled=%u, rc=%lu, batch=%u\n",
					     filled, lpar_rc, batch);
			goto hcall_failure;
		}

		/* Only update pool state after hcall succeeds */
		for (i = 0; i < filled; i++) {
			free_index = pool->consumer_index;
			pool->free_map[free_index] = IBM_VETH_INVALID_MAP;

			pool->consumer_index++;
			if (pool->consumer_index >= pool->size)
				pool->consumer_index = 0;
		}

		buffers_added += filled;
		adapter->replenish_add_buff_success += filled;
		remaining -= filled;

		memset(&descs, 0, sizeof(descs));
		memset(&correlators, 0, sizeof(correlators));
		continue;

hcall_failure:
		for (i = 0; i < filled; i++) {
			index = correlators[i] & 0xffffffffUL;
			dma_addr =  pool->dma_addr[index];

			if (pool->skbuff[index]) {
				if (dma_addr &&
				    !dma_mapping_error(dev, dma_addr))
					dma_unmap_single(dev, dma_addr,
							 pool->buff_size,
							 DMA_FROM_DEVICE);

				dev_kfree_skb_any(pool->skbuff[index]);
				pool->skbuff[index] = NULL;
			}
		}
		adapter->replenish_add_buff_failure += filled;

		/*
		 * If multi rx buffers hcall is no longer supported by FW
		 * e.g. in the case of Live Parttion Migration
		 */
		if (batch > 1 && lpar_rc == H_FUNCTION) {
			/*
			 * Instead of retry submit single buffer individually
			 * here just set the max rx buffer per hcall to 1
			 * buffers will be respleshed next time
			 * when ibmveth_replenish_buffer_pool() is called again
			 * with single-buffer case
			 */
			netdev_info(adapter->netdev,
				    "RX Multi buffers not supported by FW, rc=%lu\n",
				    lpar_rc);
			adapter->rx_buffers_per_hcall = 1;
			netdev_info(adapter->netdev,
				    "Next rx replesh will fall back to single-buffer hcall\n");
		}
		break;
	}

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

/**
 * ibmveth_remove_buffer_from_pool - remove a buffer from a pool
 * @adapter: adapter instance
 * @correlator: identifies pool and index
 * @reuse: whether to reuse buffer
 *
 * Return:
 * * %0       - success
 * * %-EINVAL - correlator maps to pool or index out of range
 * * %-EFAULT - pool and index map to null skb
 */
static int ibmveth_remove_buffer_from_pool(struct ibmveth_adapter *adapter,
					   u64 correlator, bool reuse)
{
	unsigned int pool  = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;
	unsigned int free_index;
	struct sk_buff *skb;

	if (WARN_ON(pool >= IBMVETH_NUM_BUFF_POOLS) ||
	    WARN_ON(index >= adapter->rx_buff_pool[pool].size)) {
		schedule_work(&adapter->work);
		return -EINVAL;
	}

	skb = adapter->rx_buff_pool[pool].skbuff[index];
	if (WARN_ON(!skb)) {
		schedule_work(&adapter->work);
		return -EFAULT;
	}

	/* if we are going to reuse the buffer then keep the pointers around
	 * but mark index as available. replenish will see the skb pointer and
	 * assume it is to be recycled.
	 */
	if (!reuse) {
		/* remove the skb pointer to mark free. actual freeing is done
		 * by upper level networking after gro_recieve
		 */
		adapter->rx_buff_pool[pool].skbuff[index] = NULL;

		dma_unmap_single(&adapter->vdev->dev,
				 adapter->rx_buff_pool[pool].dma_addr[index],
				 adapter->rx_buff_pool[pool].buff_size,
				 DMA_FROM_DEVICE);
	}

	free_index = adapter->rx_buff_pool[pool].producer_index;
	adapter->rx_buff_pool[pool].producer_index++;
	if (adapter->rx_buff_pool[pool].producer_index >=
	    adapter->rx_buff_pool[pool].size)
		adapter->rx_buff_pool[pool].producer_index = 0;
	adapter->rx_buff_pool[pool].free_map[free_index] = index;

	mb();

	atomic_dec(&(adapter->rx_buff_pool[pool].available));

	return 0;
}

/* get the current buffer on the rx queue */
static inline struct sk_buff *ibmveth_rxq_get_buffer(struct ibmveth_adapter *adapter)
{
	u64 correlator = adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator;
	unsigned int pool = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;

	if (WARN_ON(pool >= IBMVETH_NUM_BUFF_POOLS) ||
	    WARN_ON(index >= adapter->rx_buff_pool[pool].size)) {
		schedule_work(&adapter->work);
		return NULL;
	}

	return adapter->rx_buff_pool[pool].skbuff[index];
}

/**
 * ibmveth_rxq_harvest_buffer - Harvest buffer from pool
 *
 * @adapter: pointer to adapter
 * @reuse:   whether to reuse buffer
 *
 * Context: called from ibmveth_poll
 *
 * Return:
 * * %0    - success
 * * other - non-zero return from ibmveth_remove_buffer_from_pool
 */
static int ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter,
				      bool reuse)
{
	u64 cor;
	int rc;

	cor = adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator;
	rc = ibmveth_remove_buffer_from_pool(adapter, cor, reuse);
	if (unlikely(rc))
		return rc;

	if (++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
		adapter->rx_queue.index = 0;
		adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
	}

	return 0;
}

static void ibmveth_free_tx_ltb(struct ibmveth_adapter *adapter, int idx)
{
	dma_unmap_single(&adapter->vdev->dev, adapter->tx_ltb_dma[idx],
			 adapter->tx_ltb_size, DMA_TO_DEVICE);
	kfree(adapter->tx_ltb_ptr[idx]);
	adapter->tx_ltb_ptr[idx] = NULL;
}

static int ibmveth_allocate_tx_ltb(struct ibmveth_adapter *adapter, int idx)
{
	adapter->tx_ltb_ptr[idx] = kzalloc(adapter->tx_ltb_size,
					   GFP_KERNEL);
	if (!adapter->tx_ltb_ptr[idx]) {
		netdev_err(adapter->netdev,
			   "unable to allocate tx long term buffer\n");
		return -ENOMEM;
	}
	adapter->tx_ltb_dma[idx] = dma_map_single(&adapter->vdev->dev,
						  adapter->tx_ltb_ptr[idx],
						  adapter->tx_ltb_size,
						  DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->vdev->dev, adapter->tx_ltb_dma[idx])) {
		netdev_err(adapter->netdev,
			   "unable to DMA map tx long term buffer\n");
		kfree(adapter->tx_ltb_ptr[idx]);
		adapter->tx_ltb_ptr[idx] = NULL;
		return -ENOMEM;
	}

	return 0;
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
	u64 mac_address;
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

	rc = -ENOMEM;
	adapter->buffer_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
	if (!adapter->buffer_list_addr) {
		netdev_err(netdev, "unable to allocate list pages\n");
		goto out;
	}

	adapter->filter_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
	if (!adapter->filter_list_addr) {
		netdev_err(netdev, "unable to allocate filter pages\n");
		goto out_free_buffer_list;
	}

	dev = &adapter->vdev->dev;

	adapter->rx_queue.queue_len = sizeof(struct ibmveth_rx_q_entry) *
						rxq_entries;
	adapter->rx_queue.queue_addr =
		dma_alloc_coherent(dev, adapter->rx_queue.queue_len,
				   &adapter->rx_queue.queue_dma, GFP_KERNEL);
	if (!adapter->rx_queue.queue_addr)
		goto out_free_filter_list;

	adapter->buffer_list_dma = dma_map_single(dev,
			adapter->buffer_list_addr, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, adapter->buffer_list_dma)) {
		netdev_err(netdev, "unable to map buffer list pages\n");
		goto out_free_queue_mem;
	}

	adapter->filter_list_dma = dma_map_single(dev,
			adapter->filter_list_addr, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, adapter->filter_list_dma)) {
		netdev_err(netdev, "unable to map filter list pages\n");
		goto out_unmap_buffer_list;
	}

	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		if (ibmveth_allocate_tx_ltb(adapter, i))
			goto out_free_tx_ltb;
	}

	adapter->rx_queue.index = 0;
	adapter->rx_queue.num_slots = rxq_entries;
	adapter->rx_queue.toggle = 1;

	mac_address = ether_addr_to_u64(netdev->dev_addr);

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
		goto out_unmap_filter_list;
	}

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		if (!adapter->rx_buff_pool[i].active)
			continue;
		if (ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[i])) {
			netdev_err(netdev, "unable to alloc pool\n");
			adapter->rx_buff_pool[i].active = 0;
			rc = -ENOMEM;
			goto out_free_buffer_pools;
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

		goto out_free_buffer_pools;
	}

	rc = -ENOMEM;

	netdev_dbg(netdev, "initial replenish cycle\n");
	ibmveth_interrupt(netdev->irq, netdev);

	netif_tx_start_all_queues(netdev);

	netdev_dbg(netdev, "open complete\n");

	return 0;

out_free_buffer_pools:
	while (--i >= 0) {
		if (adapter->rx_buff_pool[i].active)
			ibmveth_free_buffer_pool(adapter,
						 &adapter->rx_buff_pool[i]);
	}
out_unmap_filter_list:
	dma_unmap_single(dev, adapter->filter_list_dma, 4096,
			 DMA_BIDIRECTIONAL);

out_free_tx_ltb:
	while (--i >= 0) {
		ibmveth_free_tx_ltb(adapter, i);
	}

out_unmap_buffer_list:
	dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
			 DMA_BIDIRECTIONAL);
out_free_queue_mem:
	dma_free_coherent(dev, adapter->rx_queue.queue_len,
			  adapter->rx_queue.queue_addr,
			  adapter->rx_queue.queue_dma);
out_free_filter_list:
	free_page((unsigned long)adapter->filter_list_addr);
out_free_buffer_list:
	free_page((unsigned long)adapter->buffer_list_addr);
out:
	napi_disable(&adapter->napi);
	return rc;
}

static int ibmveth_close(struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->vdev->dev;
	long lpar_rc;
	int i;

	netdev_dbg(netdev, "close starting\n");

	napi_disable(&adapter->napi);

	netif_tx_stop_all_queues(netdev);

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

	dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
			 DMA_BIDIRECTIONAL);
	free_page((unsigned long)adapter->buffer_list_addr);

	dma_unmap_single(dev, adapter->filter_list_dma, 4096,
			 DMA_BIDIRECTIONAL);
	free_page((unsigned long)adapter->filter_list_addr);

	dma_free_coherent(dev, adapter->rx_queue.queue_len,
			  adapter->rx_queue.queue_addr,
			  adapter->rx_queue.queue_dma);

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		if (adapter->rx_buff_pool[i].active)
			ibmveth_free_buffer_pool(adapter,
						 &adapter->rx_buff_pool[i]);

	for (i = 0; i < netdev->real_num_tx_queues; i++)
		ibmveth_free_tx_ltb(adapter, i);

	netdev_dbg(netdev, "close complete\n");

	return 0;
}

/**
 * ibmveth_reset - Handle scheduled reset work
 *
 * @w: pointer to work_struct embedded in adapter structure
 *
 * Context: This routine acquires rtnl_mutex and disables its NAPI through
 *          ibmveth_close. It can't be called directly in a context that has
 *          already acquired rtnl_mutex or disabled its NAPI, or directly from
 *          a poll routine.
 *
 * Return: void
 */
static void ibmveth_reset(struct work_struct *w)
{
	struct ibmveth_adapter *adapter = container_of(w, struct ibmveth_adapter, work);
	struct net_device *netdev = adapter->netdev;

	netdev_dbg(netdev, "reset starting\n");

	rtnl_lock();

	dev_close(adapter->netdev);
	dev_open(adapter->netdev, NULL);

	rtnl_unlock();

	netdev_dbg(netdev, "reset complete\n");
}

static int ibmveth_set_link_ksettings(struct net_device *dev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	return ethtool_virtdev_set_link_ksettings(dev, cmd,
						  &adapter->speed,
						  &adapter->duplex);
}

static int ibmveth_get_link_ksettings(struct net_device *dev,
				      struct ethtool_link_ksettings *cmd)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	cmd->base.speed = adapter->speed;
	cmd->base.duplex = adapter->duplex;
	cmd->base.port = PORT_OTHER;

	return 0;
}

static void ibmveth_init_link_settings(struct net_device *dev)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	adapter->speed = SPEED_1000;
	adapter->duplex = DUPLEX_FULL;
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strscpy(info->driver, ibmveth_driver_name, sizeof(info->driver));
	strscpy(info->version, ibmveth_driver_version, sizeof(info->version));
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
		features &= ~NETIF_F_CSUM_MASK;

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
		ibmveth_close(dev);
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

	if (ret == H_SUCCESS &&
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

static int ibmveth_set_tso(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	unsigned long set_attr, clr_attr, ret_attr;
	long ret1, ret2;
	int rc1 = 0, rc2 = 0;
	int restart = 0;

	if (netif_running(dev)) {
		restart = 1;
		ibmveth_close(dev);
	}

	set_attr = 0;
	clr_attr = 0;

	if (data)
		set_attr = IBMVETH_ILLAN_LRG_SR_ENABLED;
	else
		clr_attr = IBMVETH_ILLAN_LRG_SR_ENABLED;

	ret1 = h_illan_attributes(adapter->vdev->unit_address, 0, 0, &ret_attr);

	if (ret1 == H_SUCCESS && (ret_attr & IBMVETH_ILLAN_LRG_SND_SUPPORT) &&
	    !old_large_send) {
		ret2 = h_illan_attributes(adapter->vdev->unit_address, clr_attr,
					  set_attr, &ret_attr);

		if (ret2 != H_SUCCESS) {
			netdev_err(dev, "unable to change tso settings. %d rc=%ld\n",
				   data, ret2);

			h_illan_attributes(adapter->vdev->unit_address,
					   set_attr, clr_attr, &ret_attr);

			if (data == 1)
				dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);
			rc1 = -EIO;

		} else {
			adapter->fw_large_send_support = data;
			adapter->large_send = data;
		}
	} else {
		/* Older firmware version of large send offload does not
		 * support tcp6/ipv6
		 */
		if (data == 1) {
			dev->features &= ~NETIF_F_TSO6;
			netdev_info(dev, "TSO feature requires all partitions to have updated driver");
		}
		adapter->large_send = data;
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
	int large_send = !!(features & (NETIF_F_TSO | NETIF_F_TSO6));
	int rc1 = 0, rc2 = 0;

	if (rx_csum != adapter->rx_csum) {
		rc1 = ibmveth_set_csum_offload(dev, rx_csum);
		if (rc1 && !adapter->rx_csum)
			dev->features =
				features & ~(NETIF_F_CSUM_MASK |
					     NETIF_F_RXCSUM);
	}

	if (large_send != adapter->large_send) {
		rc2 = ibmveth_set_tso(dev, large_send);
		if (rc2 && !adapter->large_send)
			dev->features =
				features & ~(NETIF_F_TSO | NETIF_F_TSO6);
	}

	return rc1 ? rc1 : rc2;
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

static void ibmveth_get_channels(struct net_device *netdev,
				 struct ethtool_channels *channels)
{
	channels->max_tx = ibmveth_real_max_tx_queues();
	channels->tx_count = netdev->real_num_tx_queues;

	channels->max_rx = netdev->real_num_rx_queues;
	channels->rx_count = netdev->real_num_rx_queues;
}

static int ibmveth_set_channels(struct net_device *netdev,
				struct ethtool_channels *channels)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	unsigned int old = netdev->real_num_tx_queues,
		     goal = channels->tx_count;
	int rc, i;

	/* If ndo_open has not been called yet then don't allocate, just set
	 * desired netdev_queue's and return
	 */
	if (!(netdev->flags & IFF_UP))
		return netif_set_real_num_tx_queues(netdev, goal);

	/* We have IBMVETH_MAX_QUEUES netdev_queue's allocated
	 * but we may need to alloc/free the ltb's.
	 */
	netif_tx_stop_all_queues(netdev);

	/* Allocate any queue that we need */
	for (i = old; i < goal; i++) {
		if (adapter->tx_ltb_ptr[i])
			continue;

		rc = ibmveth_allocate_tx_ltb(adapter, i);
		if (!rc)
			continue;

		/* if something goes wrong, free everything we just allocated */
		netdev_err(netdev, "Failed to allocate more tx queues, returning to %d queues\n",
			   old);
		goal = old;
		old = i;
		break;
	}
	rc = netif_set_real_num_tx_queues(netdev, goal);
	if (rc) {
		netdev_err(netdev, "Failed to set real tx queues, returning to %d queues\n",
			   old);
		goal = old;
		old = i;
	}
	/* Free any that are no longer needed */
	for (i = old; i > goal; i--) {
		if (adapter->tx_ltb_ptr[i - 1])
			ibmveth_free_tx_ltb(adapter, i - 1);
	}

	netif_tx_wake_all_queues(netdev);

	return rc;
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		         = netdev_get_drvinfo,
	.get_link		         = ethtool_op_get_link,
	.get_strings		         = ibmveth_get_strings,
	.get_sset_count		         = ibmveth_get_sset_count,
	.get_ethtool_stats	         = ibmveth_get_ethtool_stats,
	.get_link_ksettings	         = ibmveth_get_link_ksettings,
	.set_link_ksettings              = ibmveth_set_link_ksettings,
	.get_channels			 = ibmveth_get_channels,
	.set_channels			 = ibmveth_set_channels
};

static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static int ibmveth_send(struct ibmveth_adapter *adapter,
			unsigned long desc, unsigned long mss)
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
		ret = h_send_logical_lan(adapter->vdev->unit_address, desc,
					 correlator, &correlator, mss,
					 adapter->fw_large_send_support);
	} while ((ret == H_BUSY) && (retry_count--));

	if (ret != H_SUCCESS && ret != H_DROPPED) {
		netdev_err(adapter->netdev, "tx: h_send_logical_lan failed "
			   "with rc=%ld\n", ret);
		return 1;
	}

	return 0;
}

static int ibmveth_is_packet_unsupported(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct ethhdr *ether_header;
	int ret = 0;

	ether_header = eth_hdr(skb);

	if (ether_addr_equal(ether_header->h_dest, netdev->dev_addr)) {
		netdev_dbg(netdev, "veth doesn't support loopback packets, dropping packet.\n");
		netdev->stats.tx_dropped++;
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static netdev_tx_t ibmveth_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	unsigned int desc_flags, total_bytes;
	union ibmveth_buf_desc desc;
	int i, queue_num = skb_get_queue_mapping(skb);
	unsigned long mss = 0;

	if (ibmveth_is_packet_unsupported(skb, netdev))
		goto out;
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

		if (skb_is_gso(skb) && adapter->fw_large_send_support)
			desc_flags |= IBMVETH_BUF_LRG_SND;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_is_gso(skb)) {
		if (adapter->fw_large_send_support) {
			mss = (unsigned long)skb_shinfo(skb)->gso_size;
			adapter->tx_large_packets++;
		} else if (!skb_is_gso_v6(skb)) {
			/* Put -1 in the IP checksum to tell phyp it
			 * is a largesend packet. Put the mss in
			 * the TCP checksum.
			 */
			ip_hdr(skb)->check = 0xffff;
			tcp_hdr(skb)->check =
				cpu_to_be16(skb_shinfo(skb)->gso_size);
			adapter->tx_large_packets++;
		}
	}

	/* Copy header into mapped buffer */
	if (unlikely(skb->len > adapter->tx_ltb_size)) {
		netdev_err(adapter->netdev, "tx: packet size (%u) exceeds ltb (%u)\n",
			   skb->len, adapter->tx_ltb_size);
		netdev->stats.tx_dropped++;
		goto out;
	}
	memcpy(adapter->tx_ltb_ptr[queue_num], skb->data, skb_headlen(skb));
	total_bytes = skb_headlen(skb);
	/* Copy frags into mapped buffers */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		memcpy(adapter->tx_ltb_ptr[queue_num] + total_bytes,
		       skb_frag_address_safe(frag), skb_frag_size(frag));
		total_bytes += skb_frag_size(frag);
	}

	if (unlikely(total_bytes != skb->len)) {
		netdev_err(adapter->netdev, "tx: incorrect packet len copied into ltb (%u != %u)\n",
			   skb->len, total_bytes);
		netdev->stats.tx_dropped++;
		goto out;
	}
	desc.fields.flags_len = desc_flags | skb->len;
	desc.fields.address = adapter->tx_ltb_dma[queue_num];
	/* finish writing to long_term_buff before VIOS accessing it */
	dma_wmb();

	if (ibmveth_send(adapter, desc.desc, mss)) {
		adapter->tx_send_failed++;
		netdev->stats.tx_dropped++;
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += skb->len;
	}

out:
	dev_consume_skb_any(skb);
	return NETDEV_TX_OK;


}

static void ibmveth_rx_mss_helper(struct sk_buff *skb, u16 mss, int lrg_pkt)
{
	struct tcphdr *tcph;
	int offset = 0;
	int hdr_len;

	/* only TCP packets will be aggregated */
	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)skb->data;

		if (iph->protocol == IPPROTO_TCP) {
			offset = iph->ihl * 4;
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
		} else {
			return;
		}
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *iph6 = (struct ipv6hdr *)skb->data;

		if (iph6->nexthdr == IPPROTO_TCP) {
			offset = sizeof(struct ipv6hdr);
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
		} else {
			return;
		}
	} else {
		return;
	}
	/* if mss is not set through Large Packet bit/mss in rx buffer,
	 * expect that the mss will be written to the tcp header checksum.
	 */
	tcph = (struct tcphdr *)(skb->data + offset);
	if (lrg_pkt) {
		skb_shinfo(skb)->gso_size = mss;
	} else if (offset) {
		skb_shinfo(skb)->gso_size = ntohs(tcph->check);
		tcph->check = 0;
	}

	if (skb_shinfo(skb)->gso_size) {
		hdr_len = offset + tcph->doff * 4;
		skb_shinfo(skb)->gso_segs =
				DIV_ROUND_UP(skb->len - hdr_len,
					     skb_shinfo(skb)->gso_size);
	}
}

static void ibmveth_rx_csum_helper(struct sk_buff *skb,
				   struct ibmveth_adapter *adapter)
{
	struct iphdr *iph = NULL;
	struct ipv6hdr *iph6 = NULL;
	__be16 skb_proto = 0;
	u16 iphlen = 0;
	u16 iph_proto = 0;
	u16 tcphdrlen = 0;

	skb_proto = be16_to_cpu(skb->protocol);

	if (skb_proto == ETH_P_IP) {
		iph = (struct iphdr *)skb->data;

		/* If the IP checksum is not offloaded and if the packet
		 *  is large send, the checksum must be rebuilt.
		 */
		if (iph->check == 0xffff) {
			iph->check = 0;
			iph->check = ip_fast_csum((unsigned char *)iph,
						  iph->ihl);
		}

		iphlen = iph->ihl * 4;
		iph_proto = iph->protocol;
	} else if (skb_proto == ETH_P_IPV6) {
		iph6 = (struct ipv6hdr *)skb->data;
		iphlen = sizeof(struct ipv6hdr);
		iph_proto = iph6->nexthdr;
	}

	/* When CSO is enabled the TCP checksum may have be set to NULL by
	 * the sender given that we zeroed out TCP checksum field in
	 * transmit path (refer ibmveth_start_xmit routine). In this case set
	 * up CHECKSUM_PARTIAL. If the packet is forwarded, the checksum will
	 * then be recalculated by the destination NIC (CSO must be enabled
	 * on the destination NIC).
	 *
	 * In an OVS environment, when a flow is not cached, specifically for a
	 * new TCP connection, the first packet information is passed up to
	 * the user space for finding a flow. During this process, OVS computes
	 * checksum on the first packet when CHECKSUM_PARTIAL flag is set.
	 *
	 * So, re-compute TCP pseudo header checksum.
	 */

	if (iph_proto == IPPROTO_TCP) {
		struct tcphdr *tcph = (struct tcphdr *)(skb->data + iphlen);

		if (tcph->check == 0x0000) {
			/* Recompute TCP pseudo header checksum  */
			tcphdrlen = skb->len - iphlen;
			if (skb_proto == ETH_P_IP)
				tcph->check =
				 ~csum_tcpudp_magic(iph->saddr,
				iph->daddr, tcphdrlen, iph_proto, 0);
			else if (skb_proto == ETH_P_IPV6)
				tcph->check =
				 ~csum_ipv6_magic(&iph6->saddr,
				&iph6->daddr, tcphdrlen, iph_proto, 0);
			/* Setup SKB fields for checksum offload */
			skb_partial_csum_set(skb, iphlen,
					     offsetof(struct tcphdr, check));
			skb_reset_network_header(skb);
		}
	}
}

static int ibmveth_poll(struct napi_struct *napi, int budget)
{
	struct ibmveth_adapter *adapter =
			container_of(napi, struct ibmveth_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int frames_processed = 0;
	unsigned long lpar_rc;
	u16 mss = 0;

restart_poll:
	while (frames_processed < budget) {
		if (!ibmveth_rxq_pending_buffer(adapter))
			break;

		smp_rmb();
		if (!ibmveth_rxq_buffer_valid(adapter)) {
			wmb(); /* suggested by larson1 */
			adapter->rx_invalid_buffer++;
			netdev_dbg(netdev, "recycling invalid buffer\n");
			if (unlikely(ibmveth_rxq_harvest_buffer(adapter, true)))
				break;
		} else {
			struct sk_buff *skb, *new_skb;
			int length = ibmveth_rxq_frame_length(adapter);
			int offset = ibmveth_rxq_frame_offset(adapter);
			int csum_good = ibmveth_rxq_csum_good(adapter);
			int lrg_pkt = ibmveth_rxq_large_packet(adapter);
			__sum16 iph_check = 0;

			skb = ibmveth_rxq_get_buffer(adapter);
			if (unlikely(!skb))
				break;

			/* if the large packet bit is set in the rx queue
			 * descriptor, the mss will be written by PHYP eight
			 * bytes from the start of the rx buffer, which is
			 * skb->data at this stage
			 */
			if (lrg_pkt) {
				__be64 *rxmss = (__be64 *)(skb->data + 8);

				mss = (u16)be64_to_cpu(*rxmss);
			}

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
				if (unlikely(ibmveth_rxq_harvest_buffer(adapter, true)))
					break;
				skb = new_skb;
			} else {
				if (unlikely(ibmveth_rxq_harvest_buffer(adapter, false)))
					break;
				skb_reserve(skb, offset);
			}

			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, netdev);

			/* PHYP without PLSO support places a -1 in the ip
			 * checksum for large send frames.
			 */
			if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
				struct iphdr *iph = (struct iphdr *)skb->data;

				iph_check = iph->check;
			}

			if ((length > netdev->mtu + ETH_HLEN) ||
			    lrg_pkt || iph_check == 0xffff) {
				ibmveth_rx_mss_helper(skb, mss, lrg_pkt);
				adapter->rx_large_packets++;
			}

			if (csum_good) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				ibmveth_rx_csum_helper(skb, adapter);
			}

			napi_gro_receive(napi, skb);	/* send it up */

			netdev->stats.rx_packets++;
			netdev->stats.rx_bytes += length;
			frames_processed++;
		}
	}

	ibmveth_replenish_task(adapter);

	if (frames_processed == budget)
		goto out;

	if (!napi_complete_done(napi, frames_processed))
		goto out;

	/* We think we are done - reenable interrupts,
	 * then check once more to make sure we are done.
	 */
	lpar_rc = h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_ENABLE);
	if (WARN_ON(lpar_rc != H_SUCCESS)) {
		schedule_work(&adapter->work);
		goto out;
	}

	if (ibmveth_rxq_pending_buffer(adapter) && napi_schedule(napi)) {
		lpar_rc = h_vio_signal(adapter->vdev->unit_address,
				       VIO_IRQ_DISABLE);
		goto restart_poll;
	}

out:
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
		WARN_ON(lpar_rc != H_SUCCESS);
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
			u64 mcast_addr;
			mcast_addr = ether_addr_to_u64(ha->addr);
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

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		if (new_mtu_oh <= adapter->rx_buff_pool[i].buff_size)
			break;

	if (i == IBMVETH_NUM_BUFF_POOLS)
		return -EINVAL;

	/* Deactivate all the buffer pools so that the next loop can activate
	   only the buffer pools necessary to hold the new MTU */
	if (netif_running(adapter->netdev)) {
		need_restart = 1;
		ibmveth_close(adapter->netdev);
	}

	/* Look for an active buffer pool that can hold the new MTU */
	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		adapter->rx_buff_pool[i].active = 1;

		if (new_mtu_oh <= adapter->rx_buff_pool[i].buff_size) {
			WRITE_ONCE(dev->mtu, new_mtu);
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
	struct iommu_table *tbl;
	unsigned long ret;
	int i;
	int rxqentries = 1;

	tbl = get_iommu_table_base(&vdev->dev);

	/* netdev inits at probe time along with the structures we need below*/
	if (netdev == NULL)
		return IOMMU_PAGE_ALIGN(IBMVETH_IO_ENTITLEMENT_DEFAULT, tbl);

	adapter = netdev_priv(netdev);

	ret = IBMVETH_BUFF_LIST_SIZE + IBMVETH_FILT_LIST_SIZE;
	ret += IOMMU_PAGE_ALIGN(netdev->mtu, tbl);
	/* add size of mapped tx buffers */
	ret += IOMMU_PAGE_ALIGN(IBMVETH_MAX_TX_BUF_SIZE, tbl);

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
		/* add the size of the active receive buffers */
		if (adapter->rx_buff_pool[i].active)
			ret +=
			    adapter->rx_buff_pool[i].size *
			    IOMMU_PAGE_ALIGN(adapter->rx_buff_pool[i].
					     buff_size, tbl);
		rxqentries += adapter->rx_buff_pool[i].size;
	}
	/* add the size of the receive queue entries */
	ret += IOMMU_PAGE_ALIGN(
		rxqentries * sizeof(struct ibmveth_rx_q_entry), tbl);

	return ret;
}

static int ibmveth_set_mac_addr(struct net_device *dev, void *p)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	struct sockaddr *addr = p;
	u64 mac_address;
	int rc;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	mac_address = ether_addr_to_u64(addr->sa_data);
	rc = h_change_logical_lan_mac(adapter->vdev->unit_address, mac_address);
	if (rc) {
		netdev_err(adapter->netdev, "h_change_logical_lan_mac failed with rc=%d\n", rc);
		return rc;
	}

	eth_hw_addr_set(dev, addr->sa_data);

	return 0;
}

static const struct net_device_ops ibmveth_netdev_ops = {
	.ndo_open		= ibmveth_open,
	.ndo_stop		= ibmveth_close,
	.ndo_start_xmit		= ibmveth_start_xmit,
	.ndo_set_rx_mode	= ibmveth_set_multicast_list,
	.ndo_eth_ioctl		= ibmveth_ioctl,
	.ndo_change_mtu		= ibmveth_change_mtu,
	.ndo_fix_features	= ibmveth_fix_features,
	.ndo_set_features	= ibmveth_set_features,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address    = ibmveth_set_mac_addr,
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
	__be32 *mcastFilterSize_p;
	long ret;
	unsigned long ret_attr;

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

	mcastFilterSize_p = (__be32 *)vio_get_attribute(dev,
							VETH_MCAST_FILTER_SIZE,
							NULL);
	if (!mcastFilterSize_p) {
		dev_err(&dev->dev, "Can't find VETH_MCAST_FILTER_SIZE "
			"attribute\n");
		return -EINVAL;
	}

	netdev = alloc_etherdev_mqs(sizeof(struct ibmveth_adapter), IBMVETH_MAX_QUEUES, 1);
	if (!netdev)
		return -ENOMEM;

	adapter = netdev_priv(netdev);
	dev_set_drvdata(&dev->dev, netdev);

	adapter->vdev = dev;
	adapter->netdev = netdev;
	INIT_WORK(&adapter->work, ibmveth_reset);
	adapter->mcastFilterSize = be32_to_cpu(*mcastFilterSize_p);
	ibmveth_init_link_settings(netdev);

	netif_napi_add_weight(netdev, &adapter->napi, ibmveth_poll, 16);

	netdev->irq = dev->irq;
	netdev->netdev_ops = &ibmveth_netdev_ops;
	netdev->ethtool_ops = &netdev_ethtool_ops;
	SET_NETDEV_DEV(netdev, &dev->dev);
	netdev->hw_features = NETIF_F_SG;
	if (vio_get_attribute(dev, "ibm,illan-options", NULL) != NULL) {
		netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				       NETIF_F_RXCSUM;
	}

	netdev->features |= netdev->hw_features;

	ret = h_illan_attributes(adapter->vdev->unit_address, 0, 0, &ret_attr);

	/* If running older firmware, TSO should not be enabled by default */
	if (ret == H_SUCCESS && (ret_attr & IBMVETH_ILLAN_LRG_SND_SUPPORT) &&
	    !old_large_send) {
		netdev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		netdev->features |= netdev->hw_features;
	} else {
		netdev->hw_features |= NETIF_F_TSO;
	}

	adapter->is_active_trunk = false;
	if (ret == H_SUCCESS && (ret_attr & IBMVETH_ILLAN_ACTIVE_TRUNK)) {
		adapter->is_active_trunk = true;
		netdev->hw_features |= NETIF_F_FRAGLIST;
		netdev->features |= NETIF_F_FRAGLIST;
	}

	if (ret == H_SUCCESS &&
	    (ret_attr & IBMVETH_ILLAN_RX_MULTI_BUFF_SUPPORT)) {
		adapter->rx_buffers_per_hcall = IBMVETH_MAX_RX_PER_HCALL;
		netdev_dbg(netdev,
			   "RX Multi-buffer hcall supported by FW, batch set to %u\n",
			    adapter->rx_buffers_per_hcall);
	} else {
		adapter->rx_buffers_per_hcall = 1;
		netdev_dbg(netdev,
			   "RX Single-buffer hcall mode, batch set to %u\n",
			   adapter->rx_buffers_per_hcall);
	}

	netdev->min_mtu = IBMVETH_MIN_MTU;
	netdev->max_mtu = ETH_MAX_MTU - IBMVETH_BUFF_OH;

	eth_hw_addr_set(netdev, mac_addr_p);

	if (firmware_has_feature(FW_FEATURE_CMO))
		memcpy(pool_count, pool_count_cmo, sizeof(pool_count));

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

	rc = netif_set_real_num_tx_queues(netdev, min(num_online_cpus(),
						      IBMVETH_DEFAULT_QUEUES));
	if (rc) {
		netdev_dbg(netdev, "failed to set number of tx queues rc=%d\n",
			   rc);
		free_netdev(netdev);
		return rc;
	}
	adapter->tx_ltb_size = PAGE_ALIGN(IBMVETH_MAX_TX_BUF_SIZE);
	for (i = 0; i < IBMVETH_MAX_QUEUES; i++)
		adapter->tx_ltb_ptr[i] = NULL;

	netdev_dbg(netdev, "adapter @ 0x%p\n", adapter);
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

static void ibmveth_remove(struct vio_dev *dev)
{
	struct net_device *netdev = dev_get_drvdata(&dev->dev);
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	int i;

	cancel_work_sync(&adapter->work);

	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		kobject_put(&adapter->rx_buff_pool[i].kobj);

	unregister_netdev(netdev);

	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);
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

/**
 * veth_pool_store - sysfs store handler for pool attributes
 * @kobj: kobject embedded in pool
 * @attr: attribute being changed
 * @buf: value being stored
 * @count: length of @buf in bytes
 *
 * Stores new value in pool attribute. Verifies the range of the new value for
 * size and buff_size. Verifies that at least one pool remains available to
 * receive MTU-sized packets.
 *
 * Context: Process context.
 *          Takes and releases rtnl_mutex to ensure correct ordering of close
 *	    and open calls.
 * Return:
 * * %-EPERM  - Not allowed to disabled all MTU-sized buffer pools
 * * %-EINVAL - New pool size or buffer size is out of range
 * * count    - Return count for success
 * * other    - Return value from a failed ibmveth_open call
 */
static ssize_t veth_pool_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t count)
{
	struct ibmveth_buff_pool *pool = container_of(kobj,
						      struct ibmveth_buff_pool,
						      kobj);
	struct net_device *netdev = dev_get_drvdata(kobj_to_dev(kobj->parent));
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	long value = simple_strtol(buf, NULL, 10);
	bool change = false;
	u32 newbuff_size;
	u32 oldbuff_size;
	int newactive;
	int oldactive;
	u32 newsize;
	u32 oldsize;
	long rc;

	rtnl_lock();

	oldbuff_size = pool->buff_size;
	oldactive = pool->active;
	oldsize = pool->size;

	newbuff_size = oldbuff_size;
	newactive = oldactive;
	newsize = oldsize;

	if (attr == &veth_active_attr) {
		if (value && !oldactive) {
			newactive = 1;
			change = true;
		} else if (!value && oldactive) {
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
				rc = -EPERM;
				goto unlock_err;
			}

			newactive = 0;
			change = true;
		}
	} else if (attr == &veth_num_attr) {
		if (value <= 0 || value > IBMVETH_MAX_POOL_COUNT) {
			rc = -EINVAL;
			goto unlock_err;
		}
		if (value != oldsize) {
			newsize = value;
			change = true;
		}
	} else if (attr == &veth_size_attr) {
		if (value <= IBMVETH_BUFF_OH || value > IBMVETH_MAX_BUF_SIZE) {
			rc = -EINVAL;
			goto unlock_err;
		}
		if (value != oldbuff_size) {
			newbuff_size = value;
			change = true;
		}
	}

	if (change) {
		if (netif_running(netdev))
			ibmveth_close(netdev);

		pool->active = newactive;
		pool->buff_size = newbuff_size;
		pool->size = newsize;

		if (netif_running(netdev)) {
			rc = ibmveth_open(netdev);
			if (rc) {
				pool->active = oldactive;
				pool->buff_size = oldbuff_size;
				pool->size = oldsize;
				goto unlock_err;
			}
		}
	}
	rtnl_unlock();

	/* kick the interrupt handler to allocate/deallocate pools */
	ibmveth_interrupt(netdev->irq, netdev);
	return count;

unlock_err:
	rtnl_unlock();
	return rc;
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
ATTRIBUTE_GROUPS(veth_pool);

static const struct sysfs_ops veth_pool_ops = {
	.show   = veth_pool_show,
	.store  = veth_pool_store,
};

static struct kobj_type ktype_veth_pool = {
	.release        = NULL,
	.sysfs_ops      = &veth_pool_ops,
	.default_groups = veth_pool_groups,
};

static int ibmveth_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	ibmveth_interrupt(netdev->irq, netdev);
	return 0;
}

static const struct vio_device_id ibmveth_device_table[] = {
	{ "network", "IBM,l-lan"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmveth_device_table);

static const struct dev_pm_ops ibmveth_pm_ops = {
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

#ifdef CONFIG_IBMVETH_KUNIT_TEST
#include <kunit/test.h>

/**
 * ibmveth_reset_kunit - reset routine for running in KUnit environment
 *
 * @w: pointer to work_struct embedded in adapter structure
 *
 * Context: Called in the KUnit environment. Does nothing.
 *
 * Return: void
 */
static void ibmveth_reset_kunit(struct work_struct *w)
{
	netdev_dbg(NULL, "reset_kunit starting\n");
	netdev_dbg(NULL, "reset_kunit complete\n");
}

/**
 * ibmveth_remove_buffer_from_pool_test - unit test for some of
 *                                        ibmveth_remove_buffer_from_pool
 * @test: pointer to kunit structure
 *
 * Tests the error returns from ibmveth_remove_buffer_from_pool.
 * ibmveth_remove_buffer_from_pool also calls WARN_ON, so dmesg should be
 * checked to see that these warnings happened.
 *
 * Return: void
 */
static void ibmveth_remove_buffer_from_pool_test(struct kunit *test)
{
	struct ibmveth_adapter *adapter = kunit_kzalloc(test, sizeof(*adapter), GFP_KERNEL);
	struct ibmveth_buff_pool *pool;
	u64 correlator;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adapter);

	INIT_WORK(&adapter->work, ibmveth_reset_kunit);

	/* Set sane values for buffer pools */
	for (int i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		ibmveth_init_buffer_pool(&adapter->rx_buff_pool[i], i,
					 pool_count[i], pool_size[i],
					 pool_active[i]);

	pool = &adapter->rx_buff_pool[0];
	pool->skbuff = kunit_kcalloc(test, pool->size, sizeof(void *), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pool->skbuff);

	correlator = ((u64)IBMVETH_NUM_BUFF_POOLS << 32) | 0;
	KUNIT_EXPECT_EQ(test, -EINVAL, ibmveth_remove_buffer_from_pool(adapter, correlator, false));
	KUNIT_EXPECT_EQ(test, -EINVAL, ibmveth_remove_buffer_from_pool(adapter, correlator, true));

	correlator = ((u64)0 << 32) | adapter->rx_buff_pool[0].size;
	KUNIT_EXPECT_EQ(test, -EINVAL, ibmveth_remove_buffer_from_pool(adapter, correlator, false));
	KUNIT_EXPECT_EQ(test, -EINVAL, ibmveth_remove_buffer_from_pool(adapter, correlator, true));

	correlator = (u64)0 | 0;
	pool->skbuff[0] = NULL;
	KUNIT_EXPECT_EQ(test, -EFAULT, ibmveth_remove_buffer_from_pool(adapter, correlator, false));
	KUNIT_EXPECT_EQ(test, -EFAULT, ibmveth_remove_buffer_from_pool(adapter, correlator, true));

	flush_work(&adapter->work);
}

/**
 * ibmveth_rxq_get_buffer_test - unit test for ibmveth_rxq_get_buffer
 * @test: pointer to kunit structure
 *
 * Tests ibmveth_rxq_get_buffer. ibmveth_rxq_get_buffer also calls WARN_ON for
 * the NULL returns, so dmesg should be checked to see that these warnings
 * happened.
 *
 * Return: void
 */
static void ibmveth_rxq_get_buffer_test(struct kunit *test)
{
	struct ibmveth_adapter *adapter = kunit_kzalloc(test, sizeof(*adapter), GFP_KERNEL);
	struct sk_buff *skb = kunit_kzalloc(test, sizeof(*skb), GFP_KERNEL);
	struct ibmveth_buff_pool *pool;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adapter);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	INIT_WORK(&adapter->work, ibmveth_reset_kunit);

	adapter->rx_queue.queue_len = 1;
	adapter->rx_queue.index = 0;
	adapter->rx_queue.queue_addr = kunit_kzalloc(test, sizeof(struct ibmveth_rx_q_entry),
						     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adapter->rx_queue.queue_addr);

	/* Set sane values for buffer pools */
	for (int i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
		ibmveth_init_buffer_pool(&adapter->rx_buff_pool[i], i,
					 pool_count[i], pool_size[i],
					 pool_active[i]);

	pool = &adapter->rx_buff_pool[0];
	pool->skbuff = kunit_kcalloc(test, pool->size, sizeof(void *), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pool->skbuff);

	adapter->rx_queue.queue_addr[0].correlator = (u64)IBMVETH_NUM_BUFF_POOLS << 32 | 0;
	KUNIT_EXPECT_PTR_EQ(test, NULL, ibmveth_rxq_get_buffer(adapter));

	adapter->rx_queue.queue_addr[0].correlator = (u64)0 << 32 | adapter->rx_buff_pool[0].size;
	KUNIT_EXPECT_PTR_EQ(test, NULL, ibmveth_rxq_get_buffer(adapter));

	pool->skbuff[0] = skb;
	adapter->rx_queue.queue_addr[0].correlator = (u64)0 << 32 | 0;
	KUNIT_EXPECT_PTR_EQ(test, skb, ibmveth_rxq_get_buffer(adapter));

	flush_work(&adapter->work);
}

static struct kunit_case ibmveth_test_cases[] = {
	KUNIT_CASE(ibmveth_remove_buffer_from_pool_test),
	KUNIT_CASE(ibmveth_rxq_get_buffer_test),
	{}
};

static struct kunit_suite ibmveth_test_suite = {
	.name = "ibmveth-kunit-test",
	.test_cases = ibmveth_test_cases,
};

kunit_test_suite(ibmveth_test_suite);
#endif
