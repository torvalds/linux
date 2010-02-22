/**************************************************************************/
/*                                                                        */
/* IBM eServer i/pSeries Virtual Ethernet Device Driver                   */
/* Copyright (C) 2003 IBM Corp.                                           */
/*  Originally written by Dave Larson (larson1@us.ibm.com)                */
/*  Maintained by Santiago Leon (santil@us.ibm.com)                       */
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
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with IBM i/pSeries LPAR Linux.  It utilizes the logical LAN    */
/* option of the RS/6000 Platform Architechture to interface with virtual */
/* ethernet NICs that are presented to the partition by the hypervisor.   */
/*                                                                        */
/**************************************************************************/
/*
  TODO:
  - add support for sysfs
  - possibly remove procfs support
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
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
#include <net/net_namespace.h>
#include <asm/hvcall.h>
#include <asm/atomic.h>
#include <asm/vio.h>
#include <asm/iommu.h>
#include <asm/uaccess.h>
#include <asm/firmware.h>
#include <linux/seq_file.h>

#include "ibmveth.h"

#undef DEBUG

#define ibmveth_printk(fmt, args...) \
  printk(KERN_DEBUG "%s: " fmt, __FILE__, ## args)

#define ibmveth_error_printk(fmt, args...) \
  printk(KERN_ERR "(%s:%3.3d ua:%x) ERROR: " fmt, __FILE__, __LINE__ , adapter->vdev->unit_address, ## args)

#ifdef DEBUG
#define ibmveth_debug_printk_no_adapter(fmt, args...) \
  printk(KERN_DEBUG "(%s:%3.3d): " fmt, __FILE__, __LINE__ , ## args)
#define ibmveth_debug_printk(fmt, args...) \
  printk(KERN_DEBUG "(%s:%3.3d ua:%x): " fmt, __FILE__, __LINE__ , adapter->vdev->unit_address, ## args)
#define ibmveth_assert(expr) \
  if(!(expr)) {                                   \
    printk(KERN_DEBUG "assertion failed (%s:%3.3d ua:%x): %s\n", __FILE__, __LINE__, adapter->vdev->unit_address, #expr); \
    BUG(); \
  }
#else
#define ibmveth_debug_printk_no_adapter(fmt, args...)
#define ibmveth_debug_printk(fmt, args...)
#define ibmveth_assert(expr)
#endif

static int ibmveth_open(struct net_device *dev);
static int ibmveth_close(struct net_device *dev);
static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int ibmveth_poll(struct napi_struct *napi, int budget);
static int ibmveth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ibmveth_set_multicast_list(struct net_device *dev);
static int ibmveth_change_mtu(struct net_device *dev, int new_mtu);
static void ibmveth_proc_register_driver(void);
static void ibmveth_proc_unregister_driver(void);
static void ibmveth_proc_register_adapter(struct ibmveth_adapter *adapter);
static void ibmveth_proc_unregister_adapter(struct ibmveth_adapter *adapter);
static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance);
static void ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter);
static unsigned long ibmveth_get_desired_dma(struct vio_dev *vdev);
static struct kobj_type ktype_veth_pool;


#ifdef CONFIG_PROC_FS
#define IBMVETH_PROC_DIR "ibmveth"
static struct proc_dir_entry *ibmveth_proc_dir;
#endif

static const char ibmveth_driver_name[] = "ibmveth";
static const char ibmveth_driver_string[] = "IBM i/pSeries Virtual Ethernet Driver";
#define ibmveth_driver_version "1.03"

MODULE_AUTHOR("Santiago Leon <santil@us.ibm.com>");
MODULE_DESCRIPTION("IBM i/pSeries Virtual Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ibmveth_driver_version);

struct ibmveth_stat {
	char name[ETH_GSTRING_LEN];
	int offset;
};

#define IBMVETH_STAT_OFF(stat) offsetof(struct ibmveth_adapter, stat)
#define IBMVETH_GET_STAT(a, off) *((u64 *)(((unsigned long)(a)) + off))

struct ibmveth_stat ibmveth_stats[] = {
	{ "replenish_task_cycles", IBMVETH_STAT_OFF(replenish_task_cycles) },
	{ "replenish_no_mem", IBMVETH_STAT_OFF(replenish_no_mem) },
	{ "replenish_add_buff_failure", IBMVETH_STAT_OFF(replenish_add_buff_failure) },
	{ "replenish_add_buff_success", IBMVETH_STAT_OFF(replenish_add_buff_success) },
	{ "rx_invalid_buffer", IBMVETH_STAT_OFF(rx_invalid_buffer) },
	{ "rx_no_buffer", IBMVETH_STAT_OFF(rx_no_buffer) },
	{ "tx_map_failed", IBMVETH_STAT_OFF(tx_map_failed) },
	{ "tx_send_failed", IBMVETH_STAT_OFF(tx_send_failed) },
};

/* simple methods of getting data from the current rxq entry */
static inline u32 ibmveth_rxq_flags(struct ibmveth_adapter *adapter)
{
	return adapter->rx_queue.queue_addr[adapter->rx_queue.index].flags_off;
}

static inline int ibmveth_rxq_toggle(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_TOGGLE) >> IBMVETH_RXQ_TOGGLE_SHIFT;
}

static inline int ibmveth_rxq_pending_buffer(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_toggle(adapter) == adapter->rx_queue.toggle);
}

static inline int ibmveth_rxq_buffer_valid(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_VALID);
}

static inline int ibmveth_rxq_frame_offset(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_OFF_MASK);
}

static inline int ibmveth_rxq_frame_length(struct ibmveth_adapter *adapter)
{
	return (adapter->rx_queue.queue_addr[adapter->rx_queue.index].length);
}

static inline int ibmveth_rxq_csum_good(struct ibmveth_adapter *adapter)
{
	return (ibmveth_rxq_flags(adapter) & IBMVETH_RXQ_CSUM_GOOD);
}

/* setup the initial settings for a buffer pool */
static void ibmveth_init_buffer_pool(struct ibmveth_buff_pool *pool, u32 pool_index, u32 pool_size, u32 buff_size, u32 pool_active)
{
	pool->size = pool_size;
	pool->index = pool_index;
	pool->buff_size = buff_size;
	pool->threshold = pool_size / 2;
	pool->active = pool_active;
}

/* allocate and setup an buffer pool - called during open */
static int ibmveth_alloc_buffer_pool(struct ibmveth_buff_pool *pool)
{
	int i;

	pool->free_map = kmalloc(sizeof(u16) * pool->size, GFP_KERNEL);

	if(!pool->free_map) {
		return -1;
	}

	pool->dma_addr = kmalloc(sizeof(dma_addr_t) * pool->size, GFP_KERNEL);
	if(!pool->dma_addr) {
		kfree(pool->free_map);
		pool->free_map = NULL;
		return -1;
	}

	pool->skbuff = kmalloc(sizeof(void*) * pool->size, GFP_KERNEL);

	if(!pool->skbuff) {
		kfree(pool->dma_addr);
		pool->dma_addr = NULL;

		kfree(pool->free_map);
		pool->free_map = NULL;
		return -1;
	}

	memset(pool->skbuff, 0, sizeof(void*) * pool->size);
	memset(pool->dma_addr, 0, sizeof(dma_addr_t) * pool->size);

	for(i = 0; i < pool->size; ++i) {
		pool->free_map[i] = i;
	}

	atomic_set(&pool->available, 0);
	pool->producer_index = 0;
	pool->consumer_index = 0;

	return 0;
}

/* replenish the buffers for a pool.  note that we don't need to
 * skb_reserve these since they are used for incoming...
 */
static void ibmveth_replenish_buffer_pool(struct ibmveth_adapter *adapter, struct ibmveth_buff_pool *pool)
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

	for(i = 0; i < count; ++i) {
		union ibmveth_buf_desc desc;

		skb = alloc_skb(pool->buff_size, GFP_ATOMIC);

		if(!skb) {
			ibmveth_debug_printk("replenish: unable to allocate skb\n");
			adapter->replenish_no_mem++;
			break;
		}

		free_index = pool->consumer_index;
		pool->consumer_index = (pool->consumer_index + 1) % pool->size;
		index = pool->free_map[free_index];

		ibmveth_assert(index != IBM_VETH_INVALID_MAP);
		ibmveth_assert(pool->skbuff[index] == NULL);

		dma_addr = dma_map_single(&adapter->vdev->dev, skb->data,
				pool->buff_size, DMA_FROM_DEVICE);

		if (dma_mapping_error(&adapter->vdev->dev, dma_addr))
			goto failure;

		pool->free_map[free_index] = IBM_VETH_INVALID_MAP;
		pool->dma_addr[index] = dma_addr;
		pool->skbuff[index] = skb;

		correlator = ((u64)pool->index << 32) | index;
		*(u64*)skb->data = correlator;

		desc.fields.flags_len = IBMVETH_BUF_VALID | pool->buff_size;
		desc.fields.address = dma_addr;

		lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address, desc.desc);

		if (lpar_rc != H_SUCCESS)
			goto failure;
		else {
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

/* replenish routine */
static void ibmveth_replenish_task(struct ibmveth_adapter *adapter)
{
	int i;

	adapter->replenish_task_cycles++;

	for (i = (IbmVethNumBufferPools - 1); i >= 0; i--)
		if(adapter->rx_buff_pool[i].active)
			ibmveth_replenish_buffer_pool(adapter,
						     &adapter->rx_buff_pool[i]);

	adapter->rx_no_buffer = *(u64*)(((char*)adapter->buffer_list_addr) + 4096 - 8);
}

/* empty and free ana buffer pool - also used to do cleanup in error paths */
static void ibmveth_free_buffer_pool(struct ibmveth_adapter *adapter, struct ibmveth_buff_pool *pool)
{
	int i;

	kfree(pool->free_map);
	pool->free_map = NULL;

	if(pool->skbuff && pool->dma_addr) {
		for(i = 0; i < pool->size; ++i) {
			struct sk_buff *skb = pool->skbuff[i];
			if(skb) {
				dma_unmap_single(&adapter->vdev->dev,
						 pool->dma_addr[i],
						 pool->buff_size,
						 DMA_FROM_DEVICE);
				dev_kfree_skb_any(skb);
				pool->skbuff[i] = NULL;
			}
		}
	}

	if(pool->dma_addr) {
		kfree(pool->dma_addr);
		pool->dma_addr = NULL;
	}

	if(pool->skbuff) {
		kfree(pool->skbuff);
		pool->skbuff = NULL;
	}
}

/* remove a buffer from a pool */
static void ibmveth_remove_buffer_from_pool(struct ibmveth_adapter *adapter, u64 correlator)
{
	unsigned int pool  = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;
	unsigned int free_index;
	struct sk_buff *skb;

	ibmveth_assert(pool < IbmVethNumBufferPools);
	ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

	skb = adapter->rx_buff_pool[pool].skbuff[index];

	ibmveth_assert(skb != NULL);

	adapter->rx_buff_pool[pool].skbuff[index] = NULL;

	dma_unmap_single(&adapter->vdev->dev,
			 adapter->rx_buff_pool[pool].dma_addr[index],
			 adapter->rx_buff_pool[pool].buff_size,
			 DMA_FROM_DEVICE);

	free_index = adapter->rx_buff_pool[pool].producer_index;
	adapter->rx_buff_pool[pool].producer_index
		= (adapter->rx_buff_pool[pool].producer_index + 1)
		% adapter->rx_buff_pool[pool].size;
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

	ibmveth_assert(pool < IbmVethNumBufferPools);
	ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

	return adapter->rx_buff_pool[pool].skbuff[index];
}

/* recycle the current buffer on the rx queue */
static void ibmveth_rxq_recycle_buffer(struct ibmveth_adapter *adapter)
{
	u32 q_index = adapter->rx_queue.index;
	u64 correlator = adapter->rx_queue.queue_addr[q_index].correlator;
	unsigned int pool = correlator >> 32;
	unsigned int index = correlator & 0xffffffffUL;
	union ibmveth_buf_desc desc;
	unsigned long lpar_rc;

	ibmveth_assert(pool < IbmVethNumBufferPools);
	ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

	if(!adapter->rx_buff_pool[pool].active) {
		ibmveth_rxq_harvest_buffer(adapter);
		ibmveth_free_buffer_pool(adapter, &adapter->rx_buff_pool[pool]);
		return;
	}

	desc.fields.flags_len = IBMVETH_BUF_VALID |
		adapter->rx_buff_pool[pool].buff_size;
	desc.fields.address = adapter->rx_buff_pool[pool].dma_addr[index];

	lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address, desc.desc);

	if(lpar_rc != H_SUCCESS) {
		ibmveth_debug_printk("h_add_logical_lan_buffer failed during recycle rc=%ld", lpar_rc);
		ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);
	}

	if(++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
		adapter->rx_queue.index = 0;
		adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
	}
}

static void ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter)
{
	ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);

	if(++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
		adapter->rx_queue.index = 0;
		adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
	}
}

static void ibmveth_cleanup(struct ibmveth_adapter *adapter)
{
	int i;
	struct device *dev = &adapter->vdev->dev;

	if(adapter->buffer_list_addr != NULL) {
		if (!dma_mapping_error(dev, adapter->buffer_list_dma)) {
			dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
					DMA_BIDIRECTIONAL);
			adapter->buffer_list_dma = DMA_ERROR_CODE;
		}
		free_page((unsigned long)adapter->buffer_list_addr);
		adapter->buffer_list_addr = NULL;
	}

	if(adapter->filter_list_addr != NULL) {
		if (!dma_mapping_error(dev, adapter->filter_list_dma)) {
			dma_unmap_single(dev, adapter->filter_list_dma, 4096,
					DMA_BIDIRECTIONAL);
			adapter->filter_list_dma = DMA_ERROR_CODE;
		}
		free_page((unsigned long)adapter->filter_list_addr);
		adapter->filter_list_addr = NULL;
	}

	if(adapter->rx_queue.queue_addr != NULL) {
		if (!dma_mapping_error(dev, adapter->rx_queue.queue_dma)) {
			dma_unmap_single(dev,
					adapter->rx_queue.queue_dma,
					adapter->rx_queue.queue_len,
					DMA_BIDIRECTIONAL);
			adapter->rx_queue.queue_dma = DMA_ERROR_CODE;
		}
		kfree(adapter->rx_queue.queue_addr);
		adapter->rx_queue.queue_addr = NULL;
	}

	for(i = 0; i<IbmVethNumBufferPools; i++)
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

	/* After a kexec the adapter will still be open, so our attempt to
	* open it will fail. So if we get a failure we free the adapter and
	* try again, but only once. */
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

	ibmveth_debug_printk("open starting\n");

	napi_enable(&adapter->napi);

	for(i = 0; i<IbmVethNumBufferPools; i++)
		rxq_entries += adapter->rx_buff_pool[i].size;

	adapter->buffer_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
	adapter->filter_list_addr = (void*) get_zeroed_page(GFP_KERNEL);

	if(!adapter->buffer_list_addr || !adapter->filter_list_addr) {
		ibmveth_error_printk("unable to allocate filter or buffer list pages\n");
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENOMEM;
	}

	adapter->rx_queue.queue_len = sizeof(struct ibmveth_rx_q_entry) * rxq_entries;
	adapter->rx_queue.queue_addr = kmalloc(adapter->rx_queue.queue_len, GFP_KERNEL);

	if(!adapter->rx_queue.queue_addr) {
		ibmveth_error_printk("unable to allocate rx queue pages\n");
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENOMEM;
	}

	dev = &adapter->vdev->dev;

	adapter->buffer_list_dma = dma_map_single(dev,
			adapter->buffer_list_addr, 4096, DMA_BIDIRECTIONAL);
	adapter->filter_list_dma = dma_map_single(dev,
			adapter->filter_list_addr, 4096, DMA_BIDIRECTIONAL);
	adapter->rx_queue.queue_dma = dma_map_single(dev,
			adapter->rx_queue.queue_addr,
			adapter->rx_queue.queue_len, DMA_BIDIRECTIONAL);

	if ((dma_mapping_error(dev, adapter->buffer_list_dma)) ||
	    (dma_mapping_error(dev, adapter->filter_list_dma)) ||
	    (dma_mapping_error(dev, adapter->rx_queue.queue_dma))) {
		ibmveth_error_printk("unable to map filter or buffer list pages\n");
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENOMEM;
	}

	adapter->rx_queue.index = 0;
	adapter->rx_queue.num_slots = rxq_entries;
	adapter->rx_queue.toggle = 1;

	memcpy(&mac_address, netdev->dev_addr, netdev->addr_len);
	mac_address = mac_address >> 16;

	rxq_desc.fields.flags_len = IBMVETH_BUF_VALID | adapter->rx_queue.queue_len;
	rxq_desc.fields.address = adapter->rx_queue.queue_dma;

	ibmveth_debug_printk("buffer list @ 0x%p\n", adapter->buffer_list_addr);
	ibmveth_debug_printk("filter list @ 0x%p\n", adapter->filter_list_addr);
	ibmveth_debug_printk("receive q   @ 0x%p\n", adapter->rx_queue.queue_addr);

	h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);

	lpar_rc = ibmveth_register_logical_lan(adapter, rxq_desc, mac_address);

	if(lpar_rc != H_SUCCESS) {
		ibmveth_error_printk("h_register_logical_lan failed with %ld\n", lpar_rc);
		ibmveth_error_printk("buffer TCE:0x%llx filter TCE:0x%llx rxq desc:0x%llx MAC:0x%llx\n",
				     adapter->buffer_list_dma,
				     adapter->filter_list_dma,
				     rxq_desc.desc,
				     mac_address);
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENONET;
	}

	for(i = 0; i<IbmVethNumBufferPools; i++) {
		if(!adapter->rx_buff_pool[i].active)
			continue;
		if (ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[i])) {
			ibmveth_error_printk("unable to alloc pool\n");
			adapter->rx_buff_pool[i].active = 0;
			ibmveth_cleanup(adapter);
			napi_disable(&adapter->napi);
			return -ENOMEM ;
		}
	}

	ibmveth_debug_printk("registering irq 0x%x\n", netdev->irq);
	if((rc = request_irq(netdev->irq, ibmveth_interrupt, 0, netdev->name, netdev)) != 0) {
		ibmveth_error_printk("unable to request irq 0x%x, rc %d\n", netdev->irq, rc);
		do {
			rc = h_free_logical_lan(adapter->vdev->unit_address);
		} while (H_IS_LONG_BUSY(rc) || (rc == H_BUSY));

		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return rc;
	}

	adapter->bounce_buffer =
	    kmalloc(netdev->mtu + IBMVETH_BUFF_OH, GFP_KERNEL);
	if (!adapter->bounce_buffer) {
		ibmveth_error_printk("unable to allocate bounce buffer\n");
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENOMEM;
	}
	adapter->bounce_buffer_dma =
	    dma_map_single(&adapter->vdev->dev, adapter->bounce_buffer,
			   netdev->mtu + IBMVETH_BUFF_OH, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
		ibmveth_error_printk("unable to map bounce buffer\n");
		ibmveth_cleanup(adapter);
		napi_disable(&adapter->napi);
		return -ENOMEM;
	}

	ibmveth_debug_printk("initial replenish cycle\n");
	ibmveth_interrupt(netdev->irq, netdev);

	netif_start_queue(netdev);

	ibmveth_debug_printk("open complete\n");

	return 0;
}

static int ibmveth_close(struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	long lpar_rc;

	ibmveth_debug_printk("close starting\n");

	napi_disable(&adapter->napi);

	if (!adapter->pool_config)
		netif_stop_queue(netdev);

	free_irq(netdev->irq, netdev);

	do {
		lpar_rc = h_free_logical_lan(adapter->vdev->unit_address);
	} while (H_IS_LONG_BUSY(lpar_rc) || (lpar_rc == H_BUSY));

	if(lpar_rc != H_SUCCESS)
	{
		ibmveth_error_printk("h_free_logical_lan failed with %lx, continuing with close\n",
				     lpar_rc);
	}

	adapter->rx_no_buffer = *(u64*)(((char*)adapter->buffer_list_addr) + 4096 - 8);

	ibmveth_cleanup(adapter);

	ibmveth_debug_printk("close complete\n");

	return 0;
}

static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd) {
	cmd->supported = (SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_FIBRE);
	cmd->advertising = (ADVERTISED_1000baseT_Full | ADVERTISED_Autoneg | ADVERTISED_FIBRE);
	cmd->speed = SPEED_1000;
	cmd->duplex = DUPLEX_FULL;
	cmd->port = PORT_FIBRE;
	cmd->phy_address = 0;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = AUTONEG_ENABLE;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 1;
	return 0;
}

static void netdev_get_drvinfo (struct net_device *dev, struct ethtool_drvinfo *info) {
	strncpy(info->driver, ibmveth_driver_name, sizeof(info->driver) - 1);
	strncpy(info->version, ibmveth_driver_version, sizeof(info->version) - 1);
}

static u32 netdev_get_link(struct net_device *dev) {
	return 1;
}

static void ibmveth_set_rx_csum_flags(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	if (data)
		adapter->rx_csum = 1;
	else {
		/*
		 * Since the ibmveth firmware interface does not have the concept of
		 * separate tx/rx checksum offload enable, if rx checksum is disabled
		 * we also have to disable tx checksum offload. Once we disable rx
		 * checksum offload, we are no longer allowed to send tx buffers that
		 * are not properly checksummed.
		 */
		adapter->rx_csum = 0;
		dev->features &= ~NETIF_F_IP_CSUM;
	}
}

static void ibmveth_set_tx_csum_flags(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	if (data) {
		dev->features |= NETIF_F_IP_CSUM;
		adapter->rx_csum = 1;
	} else
		dev->features &= ~NETIF_F_IP_CSUM;
}

static int ibmveth_set_csum_offload(struct net_device *dev, u32 data,
				    void (*done) (struct net_device *, u32))
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	unsigned long set_attr, clr_attr, ret_attr;
	long ret;
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

	if (data)
		set_attr = IBMVETH_ILLAN_IPV4_TCP_CSUM;
	else
		clr_attr = IBMVETH_ILLAN_IPV4_TCP_CSUM;

	ret = h_illan_attributes(adapter->vdev->unit_address, 0, 0, &ret_attr);

	if (ret == H_SUCCESS && !(ret_attr & IBMVETH_ILLAN_ACTIVE_TRUNK) &&
	    !(ret_attr & IBMVETH_ILLAN_TRUNK_PRI_MASK) &&
	    (ret_attr & IBMVETH_ILLAN_PADDED_PKT_CSUM)) {
		ret = h_illan_attributes(adapter->vdev->unit_address, clr_attr,
					 set_attr, &ret_attr);

		if (ret != H_SUCCESS) {
			rc1 = -EIO;
			ibmveth_error_printk("unable to change checksum offload settings."
					     " %d rc=%ld\n", data, ret);

			ret = h_illan_attributes(adapter->vdev->unit_address,
						 set_attr, clr_attr, &ret_attr);
		} else
			done(dev, data);
	} else {
		rc1 = -EIO;
		ibmveth_error_printk("unable to change checksum offload settings."
				     " %d rc=%ld ret_attr=%lx\n", data, ret, ret_attr);
	}

	if (restart)
		rc2 = ibmveth_open(dev);

	return rc1 ? rc1 : rc2;
}

static int ibmveth_set_rx_csum(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);

	if ((data && adapter->rx_csum) || (!data && !adapter->rx_csum))
		return 0;

	return ibmveth_set_csum_offload(dev, data, ibmveth_set_rx_csum_flags);
}

static int ibmveth_set_tx_csum(struct net_device *dev, u32 data)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	int rc = 0;

	if (data && (dev->features & NETIF_F_IP_CSUM))
		return 0;
	if (!data && !(dev->features & NETIF_F_IP_CSUM))
		return 0;

	if (data && !adapter->rx_csum)
		rc = ibmveth_set_csum_offload(dev, data, ibmveth_set_tx_csum_flags);
	else
		ibmveth_set_tx_csum_flags(dev, data);

	return rc;
}

static u32 ibmveth_get_rx_csum(struct net_device *dev)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	return adapter->rx_csum;
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
	.get_link		= netdev_get_link,
	.set_tx_csum		= ibmveth_set_tx_csum,
	.get_rx_csum		= ibmveth_get_rx_csum,
	.set_rx_csum		= ibmveth_set_rx_csum,
	.get_strings		= ibmveth_get_strings,
	.get_sset_count		= ibmveth_get_sset_count,
	.get_ethtool_stats	= ibmveth_get_ethtool_stats,
};

static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

#define page_offset(v) ((unsigned long)(v) & ((1 << 12) - 1))

static netdev_tx_t ibmveth_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	union ibmveth_buf_desc desc;
	unsigned long lpar_rc;
	unsigned long correlator;
	unsigned long flags;
	unsigned int retry_count;
	unsigned int tx_dropped = 0;
	unsigned int tx_bytes = 0;
	unsigned int tx_packets = 0;
	unsigned int tx_send_failed = 0;
	unsigned int tx_map_failed = 0;
	int used_bounce = 0;
	unsigned long data_dma_addr;

	desc.fields.flags_len = IBMVETH_BUF_VALID | skb->len;

	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    ip_hdr(skb)->protocol != IPPROTO_TCP && skb_checksum_help(skb)) {
		ibmveth_error_printk("tx: failed to checksum packet\n");
		tx_dropped++;
		goto out;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		unsigned char *buf = skb_transport_header(skb) + skb->csum_offset;

		desc.fields.flags_len |= (IBMVETH_BUF_NO_CSUM | IBMVETH_BUF_CSUM_GOOD);

		/* Need to zero out the checksum */
		buf[0] = 0;
		buf[1] = 0;
	}

	data_dma_addr = dma_map_single(&adapter->vdev->dev, skb->data,
				       skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->vdev->dev, data_dma_addr)) {
		if (!firmware_has_feature(FW_FEATURE_CMO))
			ibmveth_error_printk("tx: unable to map xmit buffer\n");
		skb_copy_from_linear_data(skb, adapter->bounce_buffer,
					  skb->len);
		desc.fields.address = adapter->bounce_buffer_dma;
		tx_map_failed++;
		used_bounce = 1;
		wmb();
	} else
		desc.fields.address = data_dma_addr;

	/* send the frame. Arbitrarily set retrycount to 1024 */
	correlator = 0;
	retry_count = 1024;
	do {
		lpar_rc = h_send_logical_lan(adapter->vdev->unit_address,
					     desc.desc, 0, 0, 0, 0, 0,
					     correlator, &correlator);
	} while ((lpar_rc == H_BUSY) && (retry_count--));

	if(lpar_rc != H_SUCCESS && lpar_rc != H_DROPPED) {
		ibmveth_error_printk("tx: h_send_logical_lan failed with rc=%ld\n", lpar_rc);
		ibmveth_error_printk("tx: valid=%d, len=%d, address=0x%08x\n",
				     (desc.fields.flags_len & IBMVETH_BUF_VALID) ? 1 : 0,
				     skb->len, desc.fields.address);
		tx_send_failed++;
		tx_dropped++;
	} else {
		tx_packets++;
		tx_bytes += skb->len;
		netdev->trans_start = jiffies;
	}

	if (!used_bounce)
		dma_unmap_single(&adapter->vdev->dev, data_dma_addr,
				 skb->len, DMA_TO_DEVICE);

out:	spin_lock_irqsave(&adapter->stats_lock, flags);
	netdev->stats.tx_dropped += tx_dropped;
	netdev->stats.tx_bytes += tx_bytes;
	netdev->stats.tx_packets += tx_packets;
	adapter->tx_send_failed += tx_send_failed;
	adapter->tx_map_failed += tx_map_failed;
	spin_unlock_irqrestore(&adapter->stats_lock, flags);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int ibmveth_poll(struct napi_struct *napi, int budget)
{
	struct ibmveth_adapter *adapter = container_of(napi, struct ibmveth_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int frames_processed = 0;
	unsigned long lpar_rc;

 restart_poll:
	do {
		struct sk_buff *skb;

		if (!ibmveth_rxq_pending_buffer(adapter))
			break;

		rmb();
		if (!ibmveth_rxq_buffer_valid(adapter)) {
			wmb(); /* suggested by larson1 */
			adapter->rx_invalid_buffer++;
			ibmveth_debug_printk("recycling invalid buffer\n");
			ibmveth_rxq_recycle_buffer(adapter);
		} else {
			int length = ibmveth_rxq_frame_length(adapter);
			int offset = ibmveth_rxq_frame_offset(adapter);
			int csum_good = ibmveth_rxq_csum_good(adapter);

			skb = ibmveth_rxq_get_buffer(adapter);
			if (csum_good)
				skb->ip_summed = CHECKSUM_UNNECESSARY;

			ibmveth_rxq_harvest_buffer(adapter);

			skb_reserve(skb, offset);
			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, netdev);

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

		ibmveth_assert(lpar_rc == H_SUCCESS);

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
		ibmveth_assert(lpar_rc == H_SUCCESS);
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
		if(lpar_rc != H_SUCCESS) {
			ibmveth_error_printk("h_multicast_ctrl rc=%ld when entering promisc mode\n", lpar_rc);
		}
	} else {
		struct dev_mc_list *mclist;
		/* clear the filter table & disable filtering */
		lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					   IbmVethMcastEnableRecv |
					   IbmVethMcastDisableFiltering |
					   IbmVethMcastClearFilterTable,
					   0);
		if(lpar_rc != H_SUCCESS) {
			ibmveth_error_printk("h_multicast_ctrl rc=%ld when attempting to clear filter table\n", lpar_rc);
		}
		/* add the addresses to the filter table */
		netdev_for_each_mc_addr(mclist, netdev) {
			// add the multicast address to the filter table
			unsigned long mcast_addr = 0;
			memcpy(((char *)&mcast_addr)+2, mclist->dmi_addr, 6);
			lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
						   IbmVethMcastAddFilter,
						   mcast_addr);
			if(lpar_rc != H_SUCCESS) {
				ibmveth_error_printk("h_multicast_ctrl rc=%ld when adding an entry to the filter table\n", lpar_rc);
			}
		}

		/* re-enable filtering */
		lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					   IbmVethMcastEnableFiltering,
					   0);
		if(lpar_rc != H_SUCCESS) {
			ibmveth_error_printk("h_multicast_ctrl rc=%ld when enabling filtering\n", lpar_rc);
		}
	}
}

static int ibmveth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ibmveth_adapter *adapter = netdev_priv(dev);
	struct vio_dev *viodev = adapter->vdev;
	int new_mtu_oh = new_mtu + IBMVETH_BUFF_OH;
	int i;

	if (new_mtu < IBMVETH_MAX_MTU)
		return -EINVAL;

	for (i = 0; i < IbmVethNumBufferPools; i++)
		if (new_mtu_oh < adapter->rx_buff_pool[i].buff_size)
			break;

	if (i == IbmVethNumBufferPools)
		return -EINVAL;

	/* Deactivate all the buffer pools so that the next loop can activate
	   only the buffer pools necessary to hold the new MTU */
	for (i = 0; i < IbmVethNumBufferPools; i++)
		if (adapter->rx_buff_pool[i].active) {
			ibmveth_free_buffer_pool(adapter,
						 &adapter->rx_buff_pool[i]);
			adapter->rx_buff_pool[i].active = 0;
		}

	/* Look for an active buffer pool that can hold the new MTU */
	for(i = 0; i<IbmVethNumBufferPools; i++) {
		adapter->rx_buff_pool[i].active = 1;

		if (new_mtu_oh < adapter->rx_buff_pool[i].buff_size) {
			if (netif_running(adapter->netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(adapter->netdev);
				adapter->pool_config = 0;
				dev->mtu = new_mtu;
				vio_cmo_set_dev_desired(viodev,
						ibmveth_get_desired_dma
						(viodev));
				return ibmveth_open(adapter->netdev);
			}
			dev->mtu = new_mtu;
			vio_cmo_set_dev_desired(viodev,
						ibmveth_get_desired_dma
						(viodev));
			return 0;
		}
	}
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

	for (i = 0; i < IbmVethNumBufferPools; i++) {
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
	.ndo_set_multicast_list	= ibmveth_set_multicast_list,
	.ndo_do_ioctl		= ibmveth_ioctl,
	.ndo_change_mtu		= ibmveth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ibmveth_poll_controller,
#endif
};

static int __devinit ibmveth_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	int rc, i;
	long ret;
	struct net_device *netdev;
	struct ibmveth_adapter *adapter;
	unsigned long set_attr, ret_attr;

	unsigned char *mac_addr_p;
	unsigned int *mcastFilterSize_p;


	ibmveth_debug_printk_no_adapter("entering ibmveth_probe for UA 0x%x\n",
					dev->unit_address);

	mac_addr_p = (unsigned char *) vio_get_attribute(dev,
						VETH_MAC_ADDR, NULL);
	if(!mac_addr_p) {
		printk(KERN_ERR "(%s:%3.3d) ERROR: Can't find VETH_MAC_ADDR "
				"attribute\n", __FILE__, __LINE__);
		return 0;
	}

	mcastFilterSize_p = (unsigned int *) vio_get_attribute(dev,
						VETH_MCAST_FILTER_SIZE, NULL);
	if(!mcastFilterSize_p) {
		printk(KERN_ERR "(%s:%3.3d) ERROR: Can't find "
				"VETH_MCAST_FILTER_SIZE attribute\n",
				__FILE__, __LINE__);
		return 0;
	}

	netdev = alloc_etherdev(sizeof(struct ibmveth_adapter));

	if(!netdev)
		return -ENOMEM;

	adapter = netdev_priv(netdev);
	dev_set_drvdata(&dev->dev, netdev);

	adapter->vdev = dev;
	adapter->netdev = netdev;
	adapter->mcastFilterSize= *mcastFilterSize_p;
	adapter->pool_config = 0;

	netif_napi_add(netdev, &adapter->napi, ibmveth_poll, 16);

	/* 	Some older boxes running PHYP non-natively have an OF that
		returns a 8-byte local-mac-address field (and the first
		2 bytes have to be ignored) while newer boxes' OF return
		a 6-byte field. Note that IEEE 1275 specifies that
		local-mac-address must be a 6-byte field.
		The RPA doc specifies that the first byte must be 10b, so
		we'll just look for it to solve this 8 vs. 6 byte field issue */

	if ((*mac_addr_p & 0x3) != 0x02)
		mac_addr_p += 2;

	adapter->mac_addr = 0;
	memcpy(&adapter->mac_addr, mac_addr_p, 6);

	netdev->irq = dev->irq;
	netdev->netdev_ops = &ibmveth_netdev_ops;
	netdev->ethtool_ops = &netdev_ethtool_ops;
	SET_NETDEV_DEV(netdev, &dev->dev);
 	netdev->features |= NETIF_F_LLTX;
	spin_lock_init(&adapter->stats_lock);

	memcpy(netdev->dev_addr, &adapter->mac_addr, netdev->addr_len);

	for(i = 0; i<IbmVethNumBufferPools; i++) {
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

	ibmveth_debug_printk("adapter @ 0x%p\n", adapter);

	adapter->buffer_list_dma = DMA_ERROR_CODE;
	adapter->filter_list_dma = DMA_ERROR_CODE;
	adapter->rx_queue.queue_dma = DMA_ERROR_CODE;

	ibmveth_debug_printk("registering netdev...\n");

	ret = h_illan_attributes(dev->unit_address, 0, 0, &ret_attr);

	if (ret == H_SUCCESS && !(ret_attr & IBMVETH_ILLAN_ACTIVE_TRUNK) &&
	    !(ret_attr & IBMVETH_ILLAN_TRUNK_PRI_MASK) &&
	    (ret_attr & IBMVETH_ILLAN_PADDED_PKT_CSUM)) {
		set_attr = IBMVETH_ILLAN_IPV4_TCP_CSUM;

		ret = h_illan_attributes(dev->unit_address, 0, set_attr, &ret_attr);

		if (ret == H_SUCCESS) {
			adapter->rx_csum = 1;
			netdev->features |= NETIF_F_IP_CSUM;
		} else
			ret = h_illan_attributes(dev->unit_address, set_attr, 0, &ret_attr);
	}

	rc = register_netdev(netdev);

	if(rc) {
		ibmveth_debug_printk("failed to register netdev rc=%d\n", rc);
		free_netdev(netdev);
		return rc;
	}

	ibmveth_debug_printk("registered\n");

	ibmveth_proc_register_adapter(adapter);

	return 0;
}

static int __devexit ibmveth_remove(struct vio_dev *dev)
{
	struct net_device *netdev = dev_get_drvdata(&dev->dev);
	struct ibmveth_adapter *adapter = netdev_priv(netdev);
	int i;

	for(i = 0; i<IbmVethNumBufferPools; i++)
		kobject_put(&adapter->rx_buff_pool[i].kobj);

	unregister_netdev(netdev);

	ibmveth_proc_unregister_adapter(adapter);

	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

#ifdef CONFIG_PROC_FS
static void ibmveth_proc_register_driver(void)
{
	ibmveth_proc_dir = proc_mkdir(IBMVETH_PROC_DIR, init_net.proc_net);
	if (ibmveth_proc_dir) {
	}
}

static void ibmveth_proc_unregister_driver(void)
{
	remove_proc_entry(IBMVETH_PROC_DIR, init_net.proc_net);
}

static int ibmveth_show(struct seq_file *seq, void *v)
{
	struct ibmveth_adapter *adapter = seq->private;
	char *current_mac = (char *) adapter->netdev->dev_addr;
	char *firmware_mac = (char *) &adapter->mac_addr;

	seq_printf(seq, "%s %s\n\n", ibmveth_driver_string, ibmveth_driver_version);

	seq_printf(seq, "Unit Address:    0x%x\n", adapter->vdev->unit_address);
	seq_printf(seq, "Current MAC:     %pM\n", current_mac);
	seq_printf(seq, "Firmware MAC:    %pM\n", firmware_mac);

	seq_printf(seq, "\nAdapter Statistics:\n");
	seq_printf(seq, "  TX:  vio_map_single failres:      %lld\n", adapter->tx_map_failed);
	seq_printf(seq, "       send failures:               %lld\n", adapter->tx_send_failed);
	seq_printf(seq, "  RX:  replenish task cycles:       %lld\n", adapter->replenish_task_cycles);
	seq_printf(seq, "       alloc_skb_failures:          %lld\n", adapter->replenish_no_mem);
	seq_printf(seq, "       add buffer failures:         %lld\n", adapter->replenish_add_buff_failure);
	seq_printf(seq, "       invalid buffers:             %lld\n", adapter->rx_invalid_buffer);
	seq_printf(seq, "       no buffers:                  %lld\n", adapter->rx_no_buffer);

	return 0;
}

static int ibmveth_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ibmveth_show, PDE(inode)->data);
}

static const struct file_operations ibmveth_proc_fops = {
	.owner	 = THIS_MODULE,
	.open    = ibmveth_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static void ibmveth_proc_register_adapter(struct ibmveth_adapter *adapter)
{
	struct proc_dir_entry *entry;
	if (ibmveth_proc_dir) {
		char u_addr[10];
		sprintf(u_addr, "%x", adapter->vdev->unit_address);
		entry = proc_create_data(u_addr, S_IFREG, ibmveth_proc_dir,
					 &ibmveth_proc_fops, adapter);
		if (!entry)
			ibmveth_error_printk("Cannot create adapter proc entry");
	}
	return;
}

static void ibmveth_proc_unregister_adapter(struct ibmveth_adapter *adapter)
{
	if (ibmveth_proc_dir) {
		char u_addr[10];
		sprintf(u_addr, "%x", adapter->vdev->unit_address);
		remove_proc_entry(u_addr, ibmveth_proc_dir);
	}
}

#else /* CONFIG_PROC_FS */
static void ibmveth_proc_register_adapter(struct ibmveth_adapter *adapter)
{
}

static void ibmveth_proc_unregister_adapter(struct ibmveth_adapter *adapter)
{
}
static void ibmveth_proc_register_driver(void)
{
}

static void ibmveth_proc_unregister_driver(void)
{
}
#endif /* CONFIG_PROC_FS */

static struct attribute veth_active_attr;
static struct attribute veth_num_attr;
static struct attribute veth_size_attr;

static ssize_t veth_pool_show(struct kobject * kobj,
                              struct attribute * attr, char * buf)
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

static ssize_t veth_pool_store(struct kobject * kobj, struct attribute * attr,
const char * buf, size_t count)
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
				if(ibmveth_alloc_buffer_pool(pool)) {
					ibmveth_error_printk("unable to alloc pool\n");
					return -ENOMEM;
				}
				pool->active = 1;
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else
				pool->active = 1;
		} else if (!value && pool->active) {
			int mtu = netdev->mtu + IBMVETH_BUFF_OH;
			int i;
			/* Make sure there is a buffer pool with buffers that
			   can hold a packet of the size of the MTU */
			for (i = 0; i < IbmVethNumBufferPools; i++) {
				if (pool == &adapter->rx_buff_pool[i])
					continue;
				if (!adapter->rx_buff_pool[i].active)
					continue;
				if (mtu <= adapter->rx_buff_pool[i].buff_size)
					break;
			}

			if (i == IbmVethNumBufferPools) {
				ibmveth_error_printk("no active pool >= MTU\n");
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
		if (value <= 0 || value > IBMVETH_MAX_POOL_COUNT)
			return -EINVAL;
		else {
			if (netif_running(netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				pool->size = value;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else
				pool->size = value;
		}
	} else if (attr == &veth_size_attr) {
		if (value <= IBMVETH_BUFF_OH || value > IBMVETH_MAX_BUF_SIZE)
			return -EINVAL;
		else {
			if (netif_running(netdev)) {
				adapter->pool_config = 1;
				ibmveth_close(netdev);
				adapter->pool_config = 0;
				pool->buff_size = value;
				if ((rc = ibmveth_open(netdev)))
					return rc;
			} else
				pool->buff_size = value;
		}
	}

	/* kick the interrupt handler to allocate/deallocate pools */
	ibmveth_interrupt(netdev->irq, netdev);
	return count;
}


#define ATTR(_name, _mode)      \
        struct attribute veth_##_name##_attr = {               \
        .name = __stringify(_name), .mode = _mode, \
        };

static ATTR(active, 0644);
static ATTR(num, 0644);
static ATTR(size, 0644);

static struct attribute * veth_pool_attrs[] = {
	&veth_active_attr,
	&veth_num_attr,
	&veth_size_attr,
	NULL,
};

static struct sysfs_ops veth_pool_ops = {
	.show   = veth_pool_show,
	.store  = veth_pool_store,
};

static struct kobj_type ktype_veth_pool = {
	.release        = NULL,
	.sysfs_ops      = &veth_pool_ops,
	.default_attrs  = veth_pool_attrs,
};


static struct vio_device_id ibmveth_device_table[] __devinitdata= {
	{ "network", "IBM,l-lan"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmveth_device_table);

static struct vio_driver ibmveth_driver = {
	.id_table	= ibmveth_device_table,
	.probe		= ibmveth_probe,
	.remove		= ibmveth_remove,
	.get_desired_dma = ibmveth_get_desired_dma,
	.driver		= {
		.name	= ibmveth_driver_name,
		.owner	= THIS_MODULE,
	}
};

static int __init ibmveth_module_init(void)
{
	ibmveth_printk("%s: %s %s\n", ibmveth_driver_name, ibmveth_driver_string, ibmveth_driver_version);

	ibmveth_proc_register_driver();

	return vio_register_driver(&ibmveth_driver);
}

static void __exit ibmveth_module_exit(void)
{
	vio_unregister_driver(&ibmveth_driver);
	ibmveth_proc_unregister_driver();
}

module_init(ibmveth_module_init);
module_exit(ibmveth_module_exit);
