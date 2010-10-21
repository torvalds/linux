/*
 * Copyright (C) 2005 - 2010 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */

#ifndef BE_H
#define BE_H

#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <net/tcp.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#include "be_hw.h"

#define DRV_VER			"2.103.175u"
#define DRV_NAME		"be2net"
#define BE_NAME			"ServerEngines BladeEngine2 10Gbps NIC"
#define BE3_NAME		"ServerEngines BladeEngine3 10Gbps NIC"
#define OC_NAME			"Emulex OneConnect 10Gbps NIC"
#define OC_NAME1		"Emulex OneConnect 10Gbps NIC (be3)"
#define DRV_DESC		"ServerEngines BladeEngine 10Gbps NIC Driver"

#define BE_VENDOR_ID 		0x19a2
#define BE_DEVICE_ID1		0x211
#define BE_DEVICE_ID2		0x221
#define OC_DEVICE_ID1		0x700
#define OC_DEVICE_ID2		0x710

static inline char *nic_name(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case OC_DEVICE_ID1:
		return OC_NAME;
	case OC_DEVICE_ID2:
		return OC_NAME1;
	case BE_DEVICE_ID2:
		return BE3_NAME;
	default:
		return BE_NAME;
	}
}

/* Number of bytes of an RX frame that are copied to skb->data */
#define BE_HDR_LEN 		64
#define BE_MAX_JUMBO_FRAME_SIZE	9018
#define BE_MIN_MTU		256

#define BE_NUM_VLANS_SUPPORTED	64
#define BE_MAX_EQD		96
#define	BE_MAX_TX_FRAG_COUNT	30

#define EVNT_Q_LEN		1024
#define TX_Q_LEN		2048
#define TX_CQ_LEN		1024
#define RX_Q_LEN		1024	/* Does not support any other value */
#define RX_CQ_LEN		1024
#define MCC_Q_LEN		128	/* total size not to exceed 8 pages */
#define MCC_CQ_LEN		256

#define MAX_RSS_QS		4	/* BE limit is 4 queues/port */
#define BE_MAX_MSIX_VECTORS	(MAX_RSS_QS + 1 + 1)/* RSS qs + 1 def Rx + Tx */
#define BE_NAPI_WEIGHT		64
#define MAX_RX_POST 		BE_NAPI_WEIGHT /* Frags posted at a time */
#define RX_FRAGS_REFILL_WM	(RX_Q_LEN - MAX_RX_POST)

#define FW_VER_LEN		32

#define BE_MAX_VF		32

struct be_dma_mem {
	void *va;
	dma_addr_t dma;
	u32 size;
};

struct be_queue_info {
	struct be_dma_mem dma_mem;
	u16 len;
	u16 entry_size;	/* Size of an element in the queue */
	u16 id;
	u16 tail, head;
	bool created;
	atomic_t used;	/* Number of valid elements in the queue */
};

static inline u32 MODULO(u16 val, u16 limit)
{
	BUG_ON(limit & (limit - 1));
	return val & (limit - 1);
}

static inline void index_adv(u16 *index, u16 val, u16 limit)
{
	*index = MODULO((*index + val), limit);
}

static inline void index_inc(u16 *index, u16 limit)
{
	*index = MODULO((*index + 1), limit);
}

static inline void *queue_head_node(struct be_queue_info *q)
{
	return q->dma_mem.va + q->head * q->entry_size;
}

static inline void *queue_tail_node(struct be_queue_info *q)
{
	return q->dma_mem.va + q->tail * q->entry_size;
}

static inline void queue_head_inc(struct be_queue_info *q)
{
	index_inc(&q->head, q->len);
}

static inline void queue_tail_inc(struct be_queue_info *q)
{
	index_inc(&q->tail, q->len);
}

struct be_eq_obj {
	struct be_queue_info q;
	char desc[32];

	/* Adaptive interrupt coalescing (AIC) info */
	bool enable_aic;
	u16 min_eqd;		/* in usecs */
	u16 max_eqd;		/* in usecs */
	u16 cur_eqd;		/* in usecs */

	struct napi_struct napi;
};

struct be_mcc_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	bool rearm_cq;
};

struct be_tx_stats {
	u32 be_tx_reqs;		/* number of TX requests initiated */
	u32 be_tx_stops;	/* number of times TX Q was stopped */
	u32 be_tx_wrbs;		/* number of tx WRBs used */
	u32 be_tx_events;	/* number of tx completion events  */
	u32 be_tx_compl;	/* number of tx completion entries processed */
	ulong be_tx_jiffies;
	u64 be_tx_bytes;
	u64 be_tx_bytes_prev;
	u64 be_tx_pkts;
	u32 be_tx_rate;
};

struct be_tx_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	/* Remember the skbs that were transmitted */
	struct sk_buff *sent_skb_list[TX_Q_LEN];
};

/* Struct to remember the pages posted for rx frags */
struct be_rx_page_info {
	struct page *page;
	DEFINE_DMA_UNMAP_ADDR(bus);
	u16 page_offset;
	bool last_page_user;
};

struct be_rx_stats {
	u32 rx_post_fail;/* number of ethrx buffer alloc failures */
	u32 rx_polls;	/* number of times NAPI called poll function */
	u32 rx_events;	/* number of ucast rx completion events  */
	u32 rx_compl;	/* number of rx completion entries processed */
	ulong rx_jiffies;
	u64 rx_bytes;
	u64 rx_bytes_prev;
	u64 rx_pkts;
	u32 rx_rate;
	u32 rx_mcast_pkts;
	u32 rxcp_err;	/* Num rx completion entries w/ err set. */
	ulong rx_fps_jiffies;	/* jiffies at last FPS calc */
	u32 rx_frags;
	u32 prev_rx_frags;
	u32 rx_fps;		/* Rx frags per second */
};

struct be_rx_obj {
	struct be_adapter *adapter;
	struct be_queue_info q;
	struct be_queue_info cq;
	struct be_rx_page_info page_info_tbl[RX_Q_LEN];
	struct be_eq_obj rx_eq;
	struct be_rx_stats stats;
	u8 rss_id;
	bool rx_post_starved;	/* Zero rx frags have been posted to BE */
	u32 cache_line_barrier[16];
};

struct be_vf_cfg {
	unsigned char vf_mac_addr[ETH_ALEN];
	u32 vf_if_handle;
	u32 vf_pmac_id;
	u16 vf_vlan_tag;
	u32 vf_tx_rate;
};

#define BE_INVALID_PMAC_ID		0xffffffff
struct be_adapter {
	struct pci_dev *pdev;
	struct net_device *netdev;

	u8 __iomem *csr;
	u8 __iomem *db;		/* Door Bell */
	u8 __iomem *pcicfg;	/* PCI config space */

	spinlock_t mbox_lock;	/* For serializing mbox cmds to BE card */
	struct be_dma_mem mbox_mem;
	/* Mbox mem is adjusted to align to 16 bytes. The allocated addr
	 * is stored for freeing purpose */
	struct be_dma_mem mbox_mem_alloced;

	struct be_mcc_obj mcc_obj;
	spinlock_t mcc_lock;	/* For serializing mcc cmds to BE card */
	spinlock_t mcc_cq_lock;

	struct msix_entry msix_entries[BE_MAX_MSIX_VECTORS];
	bool msix_enabled;
	bool isr_registered;

	/* TX Rings */
	struct be_eq_obj tx_eq;
	struct be_tx_obj tx_obj;
	struct be_tx_stats tx_stats;

	u32 cache_line_break[8];

	/* Rx rings */
	struct be_rx_obj rx_obj[MAX_RSS_QS + 1]; /* one default non-rss Q */
	u32 num_rx_qs;
	u32 big_page_size;	/* Compounded page size shared by rx wrbs */

	struct vlan_group *vlan_grp;
	u16 vlans_added;
	u16 max_vlans;	/* Number of vlans supported */
	u8 vlan_tag[VLAN_N_VID];
	u8 vlan_prio_bmap;	/* Available Priority BitMap */
	u16 recommended_prio;	/* Recommended Priority */
	struct be_dma_mem mc_cmd_mem;

	struct be_dma_mem stats_cmd;
	/* Work queue used to perform periodic tasks like getting statistics */
	struct delayed_work work;

	/* Ethtool knobs and info */
	bool rx_csum; 		/* BE card must perform rx-checksumming */
	char fw_ver[FW_VER_LEN];
	u32 if_handle;		/* Used to configure filtering */
	u32 pmac_id;		/* MAC addr handle used by BE card */

	bool eeh_err;
	bool link_up;
	u32 port_num;
	bool promiscuous;
	bool wol;
	u32 function_mode;
	u32 function_caps;
	u32 rx_fc;		/* Rx flow control */
	u32 tx_fc;		/* Tx flow control */
	bool ue_detected;
	bool stats_ioctl_sent;
	int link_speed;
	u8 port_type;
	u8 transceiver;
	u8 autoneg;
	u8 generation;		/* BladeEngine ASIC generation */
	u32 flash_status;
	struct completion flash_compl;

	bool sriov_enabled;
	struct be_vf_cfg vf_cfg[BE_MAX_VF];
	u8 base_eq_id;
	u8 is_virtfn;
};

#define be_physfn(adapter) (!adapter->is_virtfn)

/* BladeEngine Generation numbers */
#define BE_GEN2 2
#define BE_GEN3 3

extern const struct ethtool_ops be_ethtool_ops;

#define tx_stats(adapter)		(&adapter->tx_stats)
#define rx_stats(rxo)			(&rxo->stats)

#define BE_SET_NETDEV_OPS(netdev, ops)	(netdev->netdev_ops = ops)

#define for_all_rx_queues(adapter, rxo, i)				\
	for (i = 0, rxo = &adapter->rx_obj[i]; i < adapter->num_rx_qs;	\
		i++, rxo++)

/* Just skip the first default non-rss queue */
#define for_all_rss_queues(adapter, rxo, i)				\
	for (i = 0, rxo = &adapter->rx_obj[i+1]; i < (adapter->num_rx_qs - 1);\
		i++, rxo++)

#define PAGE_SHIFT_4K		12
#define PAGE_SIZE_4K		(1 << PAGE_SHIFT_4K)

/* Returns number of pages spanned by the data starting at the given addr */
#define PAGES_4K_SPANNED(_address, size) 				\
		((u32)((((size_t)(_address) & (PAGE_SIZE_4K - 1)) + 	\
			(size) + (PAGE_SIZE_4K - 1)) >> PAGE_SHIFT_4K))

/* Byte offset into the page corresponding to given address */
#define OFFSET_IN_PAGE(addr)						\
		 ((size_t)(addr) & (PAGE_SIZE_4K-1))

/* Returns bit offset within a DWORD of a bitfield */
#define AMAP_BIT_OFFSET(_struct, field)  				\
		(((size_t)&(((_struct *)0)->field))%32)

/* Returns the bit mask of the field that is NOT shifted into location. */
static inline u32 amap_mask(u32 bitsize)
{
	return (bitsize == 32 ? 0xFFFFFFFF : (1 << bitsize) - 1);
}

static inline void
amap_set(void *ptr, u32 dw_offset, u32 mask, u32 offset, u32 value)
{
	u32 *dw = (u32 *) ptr + dw_offset;
	*dw &= ~(mask << offset);
	*dw |= (mask & value) << offset;
}

#define AMAP_SET_BITS(_struct, field, ptr, val)				\
		amap_set(ptr,						\
			offsetof(_struct, field)/32,			\
			amap_mask(sizeof(((_struct *)0)->field)),	\
			AMAP_BIT_OFFSET(_struct, field),		\
			val)

static inline u32 amap_get(void *ptr, u32 dw_offset, u32 mask, u32 offset)
{
	u32 *dw = (u32 *) ptr;
	return mask & (*(dw + dw_offset) >> offset);
}

#define AMAP_GET_BITS(_struct, field, ptr)				\
		amap_get(ptr,						\
			offsetof(_struct, field)/32,			\
			amap_mask(sizeof(((_struct *)0)->field)),	\
			AMAP_BIT_OFFSET(_struct, field))

#define be_dws_cpu_to_le(wrb, len)	swap_dws(wrb, len)
#define be_dws_le_to_cpu(wrb, len)	swap_dws(wrb, len)
static inline void swap_dws(void *wrb, int len)
{
#ifdef __BIG_ENDIAN
	u32 *dw = wrb;
	BUG_ON(len % 4);
	do {
		*dw = cpu_to_le32(*dw);
		dw++;
		len -= 4;
	} while (len);
#endif				/* __BIG_ENDIAN */
}

static inline u8 is_tcp_pkt(struct sk_buff *skb)
{
	u8 val = 0;

	if (ip_hdr(skb)->version == 4)
		val = (ip_hdr(skb)->protocol == IPPROTO_TCP);
	else if (ip_hdr(skb)->version == 6)
		val = (ipv6_hdr(skb)->nexthdr == NEXTHDR_TCP);

	return val;
}

static inline u8 is_udp_pkt(struct sk_buff *skb)
{
	u8 val = 0;

	if (ip_hdr(skb)->version == 4)
		val = (ip_hdr(skb)->protocol == IPPROTO_UDP);
	else if (ip_hdr(skb)->version == 6)
		val = (ipv6_hdr(skb)->nexthdr == NEXTHDR_UDP);

	return val;
}

static inline void be_check_sriov_fn_type(struct be_adapter *adapter)
{
	u8 data;

	pci_write_config_byte(adapter->pdev, 0xFE, 0xAA);
	pci_read_config_byte(adapter->pdev, 0xFE, &data);
	adapter->is_virtfn = (data != 0xAA);
}

static inline void be_vf_eth_addr_generate(struct be_adapter *adapter, u8 *mac)
{
	u32 addr;

	addr = jhash(adapter->netdev->dev_addr, ETH_ALEN, 0);

	mac[5] = (u8)(addr & 0xFF);
	mac[4] = (u8)((addr >> 8) & 0xFF);
	mac[3] = (u8)((addr >> 16) & 0xFF);
	mac[2] = 0xC9;
	mac[1] = 0x00;
	mac[0] = 0x00;
}

extern void be_cq_notify(struct be_adapter *adapter, u16 qid, bool arm,
		u16 num_popped);
extern void be_link_status_update(struct be_adapter *adapter, bool link_up);
extern void netdev_stats_update(struct be_adapter *adapter);
extern int be_load_fw(struct be_adapter *adapter, u8 *func);
#endif				/* BE_H */
