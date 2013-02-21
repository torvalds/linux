/*
 * Copyright (C) 2005 - 2011 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef BE_H
#define BE_H

#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <net/tcp.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/u64_stats_sync.h>

#include "be_hw.h"
#include "be_roce.h"

#define DRV_VER			"4.4.161.0u"
#define DRV_NAME		"be2net"
#define BE_NAME			"ServerEngines BladeEngine2 10Gbps NIC"
#define BE3_NAME		"ServerEngines BladeEngine3 10Gbps NIC"
#define OC_NAME			"Emulex OneConnect 10Gbps NIC"
#define OC_NAME_BE		OC_NAME	"(be3)"
#define OC_NAME_LANCER		OC_NAME "(Lancer)"
#define OC_NAME_SH		OC_NAME "(Skyhawk)"
#define DRV_DESC		"ServerEngines BladeEngine 10Gbps NIC Driver"

#define BE_VENDOR_ID 		0x19a2
#define EMULEX_VENDOR_ID	0x10df
#define BE_DEVICE_ID1		0x211
#define BE_DEVICE_ID2		0x221
#define OC_DEVICE_ID1		0x700	/* Device Id for BE2 cards */
#define OC_DEVICE_ID2		0x710	/* Device Id for BE3 cards */
#define OC_DEVICE_ID3		0xe220	/* Device id for Lancer cards */
#define OC_DEVICE_ID4           0xe228   /* Device id for VF in Lancer */
#define OC_DEVICE_ID5		0x720	/* Device Id for Skyhawk cards */
#define OC_DEVICE_ID6		0x728   /* Device id for VF in SkyHawk */
#define OC_SUBSYS_DEVICE_ID1	0xE602
#define OC_SUBSYS_DEVICE_ID2	0xE642
#define OC_SUBSYS_DEVICE_ID3	0xE612
#define OC_SUBSYS_DEVICE_ID4	0xE652

static inline char *nic_name(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case OC_DEVICE_ID1:
		return OC_NAME;
	case OC_DEVICE_ID2:
		return OC_NAME_BE;
	case OC_DEVICE_ID3:
	case OC_DEVICE_ID4:
		return OC_NAME_LANCER;
	case BE_DEVICE_ID2:
		return BE3_NAME;
	case OC_DEVICE_ID5:
	case OC_DEVICE_ID6:
		return OC_NAME_SH;
	default:
		return BE_NAME;
	}
}

/* Number of bytes of an RX frame that are copied to skb->data */
#define BE_HDR_LEN		((u16) 64)
/* allocate extra space to allow tunneling decapsulation without head reallocation */
#define BE_RX_SKB_ALLOC_SIZE (BE_HDR_LEN + 64)

#define BE_MAX_JUMBO_FRAME_SIZE	9018
#define BE_MIN_MTU		256

#define BE_NUM_VLANS_SUPPORTED	64
#define BE_MAX_EQD		96u
#define	BE_MAX_TX_FRAG_COUNT	30

#define EVNT_Q_LEN		1024
#define TX_Q_LEN		2048
#define TX_CQ_LEN		1024
#define RX_Q_LEN		1024	/* Does not support any other value */
#define RX_CQ_LEN		1024
#define MCC_Q_LEN		128	/* total size not to exceed 8 pages */
#define MCC_CQ_LEN		256

#define BE3_MAX_RSS_QS		8
#define BE2_MAX_RSS_QS		4
#define MAX_RSS_QS		BE3_MAX_RSS_QS
#define MAX_RX_QS		(MAX_RSS_QS + 1) /* RSS qs + 1 def Rx */

#define MAX_TX_QS		8
#define MAX_ROCE_EQS		5
#define MAX_MSIX_VECTORS	(MAX_RSS_QS + MAX_ROCE_EQS) /* RSS qs + RoCE */
#define BE_TX_BUDGET		256
#define BE_NAPI_WEIGHT		64
#define MAX_RX_POST		BE_NAPI_WEIGHT /* Frags posted at a time */
#define RX_FRAGS_REFILL_WM	(RX_Q_LEN - MAX_RX_POST)

#define MAX_VFS			30 /* Max VFs supported by BE3 FW */
#define FW_VER_LEN		32

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

static inline void *queue_index_node(struct be_queue_info *q, u16 index)
{
	return q->dma_mem.va + index * q->entry_size;
}

static inline void queue_head_inc(struct be_queue_info *q)
{
	index_inc(&q->head, q->len);
}

static inline void index_dec(u16 *index, u16 limit)
{
	*index = MODULO((*index - 1), limit);
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
	u32 min_eqd;		/* in usecs */
	u32 max_eqd;		/* in usecs */
	u32 eqd;		/* configured val when aic is off */
	u32 cur_eqd;		/* in usecs */

	u8 idx;			/* array index */
	u16 tx_budget;
	u16 spurious_intr;
	struct napi_struct napi;
	struct be_adapter *adapter;
} ____cacheline_aligned_in_smp;

struct be_mcc_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	bool rearm_cq;
};

struct be_tx_stats {
	u64 tx_bytes;
	u64 tx_pkts;
	u64 tx_reqs;
	u64 tx_wrbs;
	u64 tx_compl;
	ulong tx_jiffies;
	u32 tx_stops;
	struct u64_stats_sync sync;
	struct u64_stats_sync sync_compl;
};

struct be_tx_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	/* Remember the skbs that were transmitted */
	struct sk_buff *sent_skb_list[TX_Q_LEN];
	struct be_tx_stats stats;
} ____cacheline_aligned_in_smp;

/* Struct to remember the pages posted for rx frags */
struct be_rx_page_info {
	struct page *page;
	DEFINE_DMA_UNMAP_ADDR(bus);
	u16 page_offset;
	bool last_page_user;
};

struct be_rx_stats {
	u64 rx_bytes;
	u64 rx_pkts;
	u64 rx_pkts_prev;
	ulong rx_jiffies;
	u32 rx_drops_no_skbs;	/* skb allocation errors */
	u32 rx_drops_no_frags;	/* HW has no fetched frags */
	u32 rx_post_fail;	/* page post alloc failures */
	u32 rx_compl;
	u32 rx_mcast_pkts;
	u32 rx_compl_err;	/* completions with err set */
	u32 rx_pps;		/* pkts per second */
	struct u64_stats_sync sync;
};

struct be_rx_compl_info {
	u32 rss_hash;
	u16 vlan_tag;
	u16 pkt_size;
	u16 rxq_idx;
	u16 port;
	u8 vlanf;
	u8 num_rcvd;
	u8 err;
	u8 ipf;
	u8 tcpf;
	u8 udpf;
	u8 ip_csum;
	u8 l4_csum;
	u8 ipv6;
	u8 vtm;
	u8 pkt_type;
};

struct be_rx_obj {
	struct be_adapter *adapter;
	struct be_queue_info q;
	struct be_queue_info cq;
	struct be_rx_compl_info rxcp;
	struct be_rx_page_info page_info_tbl[RX_Q_LEN];
	struct be_rx_stats stats;
	u8 rss_id;
	bool rx_post_starved;	/* Zero rx frags have been posted to BE */
} ____cacheline_aligned_in_smp;

struct be_drv_stats {
	u32 be_on_die_temperature;
	u32 eth_red_drops;
	u32 rx_drops_no_pbuf;
	u32 rx_drops_no_txpb;
	u32 rx_drops_no_erx_descr;
	u32 rx_drops_no_tpre_descr;
	u32 rx_drops_too_many_frags;
	u32 forwarded_packets;
	u32 rx_drops_mtu;
	u32 rx_crc_errors;
	u32 rx_alignment_symbol_errors;
	u32 rx_pause_frames;
	u32 rx_priority_pause_frames;
	u32 rx_control_frames;
	u32 rx_in_range_errors;
	u32 rx_out_range_errors;
	u32 rx_frame_too_long;
	u32 rx_address_mismatch_drops;
	u32 rx_dropped_too_small;
	u32 rx_dropped_too_short;
	u32 rx_dropped_header_too_small;
	u32 rx_dropped_tcp_length;
	u32 rx_dropped_runt;
	u32 rx_ip_checksum_errs;
	u32 rx_tcp_checksum_errs;
	u32 rx_udp_checksum_errs;
	u32 tx_pauseframes;
	u32 tx_priority_pauseframes;
	u32 tx_controlframes;
	u32 rxpp_fifo_overflow_drop;
	u32 rx_input_fifo_overflow_drop;
	u32 pmem_fifo_overflow_drop;
	u32 jabber_events;
};

struct be_vf_cfg {
	unsigned char mac_addr[ETH_ALEN];
	int if_handle;
	int pmac_id;
	u16 def_vid;
	u16 vlan_tag;
	u32 tx_rate;
};

enum vf_state {
	ENABLED = 0,
	ASSIGNED = 1
};

#define BE_FLAGS_LINK_STATUS_INIT		1
#define BE_FLAGS_WORKER_SCHEDULED		(1 << 3)
#define BE_UC_PMAC_COUNT		30
#define BE_VF_UC_PMAC_COUNT		2

struct phy_info {
	u8 transceiver;
	u8 autoneg;
	u8 fc_autoneg;
	u8 port_type;
	u16 phy_type;
	u16 interface_type;
	u32 misc_params;
	u16 auto_speeds_supported;
	u16 fixed_speeds_supported;
	int link_speed;
	u32 dac_cable_len;
	u32 advertising;
	u32 supported;
};

struct be_adapter {
	struct pci_dev *pdev;
	struct net_device *netdev;

	u8 __iomem *db;		/* Door Bell */

	struct mutex mbox_lock; /* For serializing mbox cmds to BE card */
	struct be_dma_mem mbox_mem;
	/* Mbox mem is adjusted to align to 16 bytes. The allocated addr
	 * is stored for freeing purpose */
	struct be_dma_mem mbox_mem_alloced;

	struct be_mcc_obj mcc_obj;
	spinlock_t mcc_lock;	/* For serializing mcc cmds to BE card */
	spinlock_t mcc_cq_lock;

	u32 num_msix_vec;
	u32 num_evt_qs;
	struct be_eq_obj eq_obj[MAX_MSIX_VECTORS];
	struct msix_entry msix_entries[MAX_MSIX_VECTORS];
	bool isr_registered;

	/* TX Rings */
	u32 num_tx_qs;
	struct be_tx_obj tx_obj[MAX_TX_QS];

	/* Rx rings */
	u32 num_rx_qs;
	struct be_rx_obj rx_obj[MAX_RX_QS];
	u32 big_page_size;	/* Compounded page size shared by rx wrbs */

	struct be_drv_stats drv_stats;
	u16 vlans_added;
	u8 vlan_tag[VLAN_N_VID];
	u8 vlan_prio_bmap;	/* Available Priority BitMap */
	u16 recommended_prio;	/* Recommended Priority */
	struct be_dma_mem rx_filter; /* Cmd DMA mem for rx-filter */

	struct be_dma_mem stats_cmd;
	/* Work queue used to perform periodic tasks like getting statistics */
	struct delayed_work work;
	u16 work_counter;

	struct delayed_work func_recovery_work;
	u32 flags;
	u32 cmd_privileges;
	/* Ethtool knobs and info */
	char fw_ver[FW_VER_LEN];
	int if_handle;		/* Used to configure filtering */
	u32 *pmac_id;		/* MAC addr handle used by BE card */
	u32 beacon_state;	/* for set_phys_id */

	bool eeh_error;
	bool fw_timeout;
	bool hw_error;

	u32 port_num;
	bool promiscuous;
	u32 function_mode;
	u32 function_caps;
	u32 rx_fc;		/* Rx flow control */
	u32 tx_fc;		/* Tx flow control */
	bool stats_cmd_sent;
	u32 if_type;
	struct {
		u32 size;
		u32 total_size;
		u64 io_addr;
	} roce_db;
	u32 num_msix_roce_vec;
	struct ocrdma_dev *ocrdma_dev;
	struct list_head entry;

	u32 flash_status;
	struct completion flash_compl;

	u32 num_vfs;		/* Number of VFs provisioned by PF driver */
	u32 dev_num_vfs;	/* Number of VFs supported by HW */
	u8 virtfn;
	struct be_vf_cfg *vf_cfg;
	bool be3_native;
	u32 sli_family;
	u8 hba_port_num;
	u16 pvid;
	struct phy_info phy;
	u8 wol_cap;
	bool wol;
	u32 uc_macs;		/* Count of secondary UC MAC programmed */
	u32 msg_enable;
	int be_get_temp_freq;
	u16 max_mcast_mac;
	u16 max_tx_queues;
	u16 max_rss_queues;
	u16 max_rx_queues;
	u16 max_pmac_cnt;
	u16 max_vlans;
	u16 max_event_queues;
	u32 if_cap_flags;
	u8 pf_number;
};

#define be_physfn(adapter)		(!adapter->virtfn)
#define	sriov_enabled(adapter)		(adapter->num_vfs > 0)
#define	sriov_want(adapter)		(adapter->dev_num_vfs && num_vfs && \
					 be_physfn(adapter))
#define for_all_vfs(adapter, vf_cfg, i)					\
	for (i = 0, vf_cfg = &adapter->vf_cfg[i]; i < adapter->num_vfs;	\
		i++, vf_cfg++)

#define ON				1
#define OFF				0

#define lancer_chip(adapter)	(adapter->pdev->device == OC_DEVICE_ID3 || \
				 adapter->pdev->device == OC_DEVICE_ID4)

#define skyhawk_chip(adapter)	(adapter->pdev->device == OC_DEVICE_ID5 || \
				 adapter->pdev->device == OC_DEVICE_ID6)

#define BE3_chip(adapter)	(adapter->pdev->device == BE_DEVICE_ID2 || \
				 adapter->pdev->device == OC_DEVICE_ID2)

#define BE2_chip(adapter)	(adapter->pdev->device == BE_DEVICE_ID1 || \
				 adapter->pdev->device == OC_DEVICE_ID1)

#define BEx_chip(adapter)	(BE3_chip(adapter) || BE2_chip(adapter))

#define be_roce_supported(adapter)	(skyhawk_chip(adapter) && \
					(adapter->function_mode & RDMA_ENABLED))

extern const struct ethtool_ops be_ethtool_ops;

#define msix_enabled(adapter)		(adapter->num_msix_vec > 0)
#define num_irqs(adapter)		(msix_enabled(adapter) ?	\
						adapter->num_msix_vec : 1)
#define tx_stats(txo)			(&(txo)->stats)
#define rx_stats(rxo)			(&(rxo)->stats)

/* The default RXQ is the last RXQ */
#define default_rxo(adpt)		(&adpt->rx_obj[adpt->num_rx_qs - 1])

#define for_all_rx_queues(adapter, rxo, i)				\
	for (i = 0, rxo = &adapter->rx_obj[i]; i < adapter->num_rx_qs;	\
		i++, rxo++)

/* Skip the default non-rss queue (last one)*/
#define for_all_rss_queues(adapter, rxo, i)				\
	for (i = 0, rxo = &adapter->rx_obj[i]; i < (adapter->num_rx_qs - 1);\
		i++, rxo++)

#define for_all_tx_queues(adapter, txo, i)				\
	for (i = 0, txo = &adapter->tx_obj[i]; i < adapter->num_tx_qs;	\
		i++, txo++)

#define for_all_evt_queues(adapter, eqo, i)				\
	for (i = 0, eqo = &adapter->eq_obj[i]; i < adapter->num_evt_qs; \
		i++, eqo++)

#define is_mcc_eqo(eqo)			(eqo->idx == 0)
#define mcc_eqo(adapter)		(&adapter->eq_obj[0])

#define PAGE_SHIFT_4K		12
#define PAGE_SIZE_4K		(1 << PAGE_SHIFT_4K)

/* Returns number of pages spanned by the data starting at the given addr */
#define PAGES_4K_SPANNED(_address, size) 				\
		((u32)((((size_t)(_address) & (PAGE_SIZE_4K - 1)) + 	\
			(size) + (PAGE_SIZE_4K - 1)) >> PAGE_SHIFT_4K))

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

static inline bool is_ipv4_pkt(struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_IP) && ip_hdr(skb)->version == 4;
}

static inline void be_vf_eth_addr_generate(struct be_adapter *adapter, u8 *mac)
{
	u32 addr;

	addr = jhash(adapter->netdev->dev_addr, ETH_ALEN, 0);

	mac[5] = (u8)(addr & 0xFF);
	mac[4] = (u8)((addr >> 8) & 0xFF);
	mac[3] = (u8)((addr >> 16) & 0xFF);
	/* Use the OUI from the current MAC address */
	memcpy(mac, adapter->netdev->dev_addr, 3);
}

static inline bool be_multi_rxq(const struct be_adapter *adapter)
{
	return adapter->num_rx_qs > 1;
}

static inline bool be_error(struct be_adapter *adapter)
{
	return adapter->eeh_error || adapter->hw_error || adapter->fw_timeout;
}

static inline bool be_hw_error(struct be_adapter *adapter)
{
	return adapter->eeh_error || adapter->hw_error;
}

static inline void  be_clear_all_error(struct be_adapter *adapter)
{
	adapter->eeh_error = false;
	adapter->hw_error = false;
	adapter->fw_timeout = false;
}

static inline bool be_is_wol_excluded(struct be_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	if (!be_physfn(adapter))
		return true;

	switch (pdev->subsystem_device) {
	case OC_SUBSYS_DEVICE_ID1:
	case OC_SUBSYS_DEVICE_ID2:
	case OC_SUBSYS_DEVICE_ID3:
	case OC_SUBSYS_DEVICE_ID4:
		return true;
	default:
		return false;
	}
}

extern void be_cq_notify(struct be_adapter *adapter, u16 qid, bool arm,
		u16 num_popped);
extern void be_link_status_update(struct be_adapter *adapter, u8 link_status);
extern void be_parse_stats(struct be_adapter *adapter);
extern int be_load_fw(struct be_adapter *adapter, u8 *func);
extern bool be_is_wol_supported(struct be_adapter *adapter);
extern bool be_pause_supported(struct be_adapter *adapter);
extern u32 be_get_fw_log_level(struct be_adapter *adapter);

/*
 * internal function to initialize-cleanup roce device.
 */
extern void be_roce_dev_add(struct be_adapter *);
extern void be_roce_dev_remove(struct be_adapter *);

/*
 * internal function to open-close roce device during ifup-ifdown.
 */
extern void be_roce_dev_open(struct be_adapter *);
extern void be_roce_dev_close(struct be_adapter *);

#endif				/* BE_H */
