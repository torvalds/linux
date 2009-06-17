/*
 * Copyright (C) 2005 - 2009 ServerEngines
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
#include <linux/inet_lro.h>

#include "be_hw.h"

#define DRV_VER			"2.0.348"
#define DRV_NAME		"be2net"
#define BE_NAME			"ServerEngines BladeEngine2 10Gbps NIC"
#define OC_NAME			"Emulex OneConnect 10Gbps NIC"
#define DRV_DESC		BE_NAME "Driver"

#define BE_VENDOR_ID 		0x19a2
#define BE_DEVICE_ID1		0x211
#define OC_DEVICE_ID1		0x700
#define OC_DEVICE_ID2		0x701

static inline char *nic_name(struct pci_dev *pdev)
{
	if (pdev->device == OC_DEVICE_ID1 || pdev->device == OC_DEVICE_ID2)
		return OC_NAME;
	else
		return BE_NAME;
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
#define MCC_Q_LEN		64	/* total size not to exceed 8 pages */
#define MCC_CQ_LEN		256

#define BE_NAPI_WEIGHT		64
#define MAX_RX_POST 		BE_NAPI_WEIGHT /* Frags posted at a time */
#define RX_FRAGS_REFILL_WM	(RX_Q_LEN - MAX_RX_POST)

#define BE_MAX_LRO_DESCRIPTORS  16
#define BE_MAX_FRAGS_PER_FRAME  16

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

struct be_ctrl_info {
	u8 __iomem *csr;
	u8 __iomem *db;		/* Door Bell */
	u8 __iomem *pcicfg;	/* PCI config space */
	int pci_func;

	/* Mbox used for cmd request/response */
	spinlock_t cmd_lock;	/* For serializing cmds to BE card */
	struct be_dma_mem mbox_mem;
	/* Mbox mem is adjusted to align to 16 bytes. The allocated addr
	 * is stored for freeing purpose */
	struct be_dma_mem mbox_mem_alloced;
};

#include "be_cmds.h"

struct be_drvr_stats {
	u32 be_tx_reqs;		/* number of TX requests initiated */
	u32 be_tx_stops;	/* number of times TX Q was stopped */
	u32 be_fwd_reqs;	/* number of send reqs through forwarding i/f */
	u32 be_tx_wrbs;		/* number of tx WRBs used */
	u32 be_tx_events;	/* number of tx completion events  */
	u32 be_tx_compl;	/* number of tx completion entries processed */
	ulong be_tx_jiffies;
	u64 be_tx_bytes;
	u64 be_tx_bytes_prev;
	u32 be_tx_rate;

	u32 cache_barrier[16];

	u32 be_ethrx_post_fail;/* number of ethrx buffer alloc failures */
	u32 be_polls;		/* number of times NAPI called poll function */
	u32 be_rx_events;	/* number of ucast rx completion events  */
	u32 be_rx_compl;	/* number of rx completion entries processed */
	u32 be_lro_hgram_data[8];	/* histogram of LRO data packets */
	u32 be_lro_hgram_ack[8];	/* histogram of LRO ACKs */
	ulong be_rx_jiffies;
	u64 be_rx_bytes;
	u64 be_rx_bytes_prev;
	u32 be_rx_rate;
	/* number of non ether type II frames dropped where
	 * frame len > length field of Mac Hdr */
	u32 be_802_3_dropped_frames;
	/* number of non ether type II frames malformed where
	 * in frame len < length field of Mac Hdr */
	u32 be_802_3_malformed_frames;
	u32 be_rxcp_err;	/* Num rx completion entries w/ err set. */
	ulong rx_fps_jiffies;	/* jiffies at last FPS calc */
	u32 be_rx_frags;
	u32 be_prev_rx_frags;
	u32 be_rx_fps;		/* Rx frags per second */
};

struct be_stats_obj {
	struct be_drvr_stats drvr_stats;
	struct net_device_stats net_stats;
	struct be_dma_mem cmd;
};

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

struct be_tx_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	/* Remember the skbs that were transmitted */
	struct sk_buff *sent_skb_list[TX_Q_LEN];
};

/* Struct to remember the pages posted for rx frags */
struct be_rx_page_info {
	struct page *page;
	dma_addr_t bus;
	u16 page_offset;
	bool last_page_user;
};

struct be_rx_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	struct be_rx_page_info page_info_tbl[RX_Q_LEN];
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc lro_desc[BE_MAX_LRO_DESCRIPTORS];
};

#define BE_NUM_MSIX_VECTORS		2	/* 1 each for Tx and Rx */
struct be_adapter {
	struct pci_dev *pdev;
	struct net_device *netdev;

	/* Mbox, pci config, csr address information */
	struct be_ctrl_info ctrl;

	struct msix_entry msix_entries[BE_NUM_MSIX_VECTORS];
	bool msix_enabled;
	bool isr_registered;

	/* TX Rings */
	struct be_eq_obj tx_eq;
	struct be_tx_obj tx_obj;

	u32 cache_line_break[8];

	/* Rx rings */
	struct be_eq_obj rx_eq;
	struct be_rx_obj rx_obj;
	u32 big_page_size;	/* Compounded page size shared by rx wrbs */
	bool rx_post_starved;	/* Zero rx frags have been posted to BE */

	struct vlan_group *vlan_grp;
	u16 num_vlans;
	u8 vlan_tag[VLAN_GROUP_ARRAY_LEN];

	struct be_stats_obj stats;
	/* Work queue used to perform periodic tasks like getting statistics */
	struct delayed_work work;

	/* Ethtool knobs and info */
	bool rx_csum; 		/* BE card must perform rx-checksumming */
	u32 max_rx_coal;
	char fw_ver[FW_VER_LEN];
	u32 if_handle;		/* Used to configure filtering */
	u32 pmac_id;		/* MAC addr handle used by BE card */

	struct be_link_info link;
	u32 port_num;
};

extern struct ethtool_ops be_ethtool_ops;

#define drvr_stats(adapter)		(&adapter->stats.drvr_stats)

#define BE_SET_NETDEV_OPS(netdev, ops)	(netdev->netdev_ops = ops)

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

#endif				/* BE_H */
