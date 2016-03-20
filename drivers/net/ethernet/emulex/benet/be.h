/*
 * Copyright (C) 2005 - 2015 Emulex
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
#include <linux/cpumask.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include "be_hw.h"
#include "be_roce.h"

#define DRV_VER			"11.0.0.0"
#define DRV_NAME		"be2net"
#define BE_NAME			"Emulex BladeEngine2"
#define BE3_NAME		"Emulex BladeEngine3"
#define OC_NAME			"Emulex OneConnect"
#define OC_NAME_BE		OC_NAME	"(be3)"
#define OC_NAME_LANCER		OC_NAME "(Lancer)"
#define OC_NAME_SH		OC_NAME "(Skyhawk)"
#define DRV_DESC		"Emulex OneConnect NIC Driver"

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

/* Number of bytes of an RX frame that are copied to skb->data */
#define BE_HDR_LEN		((u16) 64)
/* allocate extra space to allow tunneling decapsulation without head reallocation */
#define BE_RX_SKB_ALLOC_SIZE (BE_HDR_LEN + 64)

#define BE_MAX_JUMBO_FRAME_SIZE	9018
#define BE_MIN_MTU		256
#define BE_MAX_MTU              (BE_MAX_JUMBO_FRAME_SIZE -	\
				 (ETH_HLEN + ETH_FCS_LEN))

/* Accommodate for QnQ configurations where VLAN insertion is enabled in HW */
#define BE_MAX_GSO_SIZE		(65535 - 2 * VLAN_HLEN)

#define BE_NUM_VLANS_SUPPORTED	64
#define BE_MAX_EQD		128u
#define	BE_MAX_TX_FRAG_COUNT	30

#define EVNT_Q_LEN		1024
#define TX_Q_LEN		2048
#define TX_CQ_LEN		1024
#define RX_Q_LEN		1024	/* Does not support any other value */
#define RX_CQ_LEN		1024
#define MCC_Q_LEN		128	/* total size not to exceed 8 pages */
#define MCC_CQ_LEN		256

#define BE2_MAX_RSS_QS		4
#define BE3_MAX_RSS_QS		16
#define BE3_MAX_TX_QS		16
#define BE3_MAX_EVT_QS		16
#define BE3_SRIOV_MAX_EVT_QS	8
#define SH_VF_MAX_NIC_EQS	3	/* Skyhawk VFs can have a max of 4 EQs
					 * and at least 1 is granted to either
					 * SURF/DPDK
					 */

#define MAX_RSS_IFACES		15
#define MAX_RX_QS		32
#define MAX_EVT_QS		32
#define MAX_TX_QS		32

#define MAX_ROCE_EQS		5
#define MAX_MSIX_VECTORS	32
#define MIN_MSIX_VECTORS	1
#define BE_NAPI_WEIGHT		64
#define MAX_RX_POST		BE_NAPI_WEIGHT /* Frags posted at a time */
#define RX_FRAGS_REFILL_WM	(RX_Q_LEN - MAX_RX_POST)
#define MAX_NUM_POST_ERX_DB	255u

#define MAX_VFS			30 /* Max VFs supported by BE3 FW */
#define FW_VER_LEN		32
#define	CNTL_SERIAL_NUM_WORDS	8  /* Controller serial number words */
#define	CNTL_SERIAL_NUM_WORD_SZ	(sizeof(u16)) /* Byte-sz of serial num word */

#define	RSS_INDIR_TABLE_LEN	128
#define RSS_HASH_KEY_LEN	40

#define BE_UNKNOWN_PHY_STATE	0xFF

struct be_dma_mem {
	void *va;
	dma_addr_t dma;
	u32 size;
};

struct be_queue_info {
	u32 len;
	u32 entry_size;	/* Size of an element in the queue */
	u32 tail, head;
	atomic_t used;	/* Number of valid elements in the queue */
	u32 id;
	struct be_dma_mem dma_mem;
	bool created;
};

static inline u32 MODULO(u32 val, u32 limit)
{
	BUG_ON(limit & (limit - 1));
	return val & (limit - 1);
}

static inline void index_adv(u32 *index, u32 val, u32 limit)
{
	*index = MODULO((*index + val), limit);
}

static inline void index_inc(u32 *index, u32 limit)
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

static inline void index_dec(u32 *index, u32 limit)
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
	u8 msix_idx;
	u16 spurious_intr;
	struct napi_struct napi;
	struct be_adapter *adapter;
	cpumask_var_t  affinity_mask;

#ifdef CONFIG_NET_RX_BUSY_POLL
#define BE_EQ_IDLE		0
#define BE_EQ_NAPI		1	/* napi owns this EQ */
#define BE_EQ_POLL		2	/* poll owns this EQ */
#define BE_EQ_LOCKED		(BE_EQ_NAPI | BE_EQ_POLL)
#define BE_EQ_NAPI_YIELD	4	/* napi yielded this EQ */
#define BE_EQ_POLL_YIELD	8	/* poll yielded this EQ */
#define BE_EQ_YIELD		(BE_EQ_NAPI_YIELD | BE_EQ_POLL_YIELD)
#define BE_EQ_USER_PEND		(BE_EQ_POLL | BE_EQ_POLL_YIELD)
	unsigned int state;
	spinlock_t lock;	/* lock to serialize napi and busy-poll */
#endif  /* CONFIG_NET_RX_BUSY_POLL */
} ____cacheline_aligned_in_smp;

struct be_aic_obj {		/* Adaptive interrupt coalescing (AIC) info */
	bool enable;
	u32 min_eqd;		/* in usecs */
	u32 max_eqd;		/* in usecs */
	u32 prev_eqd;		/* in usecs */
	u32 et_eqd;		/* configured val when aic is off */
	ulong jiffies;
	u64 rx_pkts_prev;	/* Used to calculate RX pps */
	u64 tx_reqs_prev;	/* Used to calculate TX pps */
};

enum {
	NAPI_POLLING,
	BUSY_POLLING
};

struct be_mcc_obj {
	struct be_queue_info q;
	struct be_queue_info cq;
	bool rearm_cq;
};

struct be_tx_stats {
	u64 tx_bytes;
	u64 tx_pkts;
	u64 tx_vxlan_offload_pkts;
	u64 tx_reqs;
	u64 tx_compl;
	ulong tx_jiffies;
	u32 tx_stops;
	u32 tx_drv_drops;	/* pkts dropped by driver */
	/* the error counters are described in be_ethtool.c */
	u32 tx_hdr_parse_err;
	u32 tx_dma_err;
	u32 tx_tso_err;
	u32 tx_spoof_check_err;
	u32 tx_qinq_err;
	u32 tx_internal_parity_err;
	struct u64_stats_sync sync;
	struct u64_stats_sync sync_compl;
};

/* Structure to hold some data of interest obtained from a TX CQE */
struct be_tx_compl_info {
	u8 status;		/* Completion status */
	u16 end_index;		/* Completed TXQ Index */
};

struct be_tx_obj {
	u32 db_offset;
	struct be_queue_info q;
	struct be_queue_info cq;
	struct be_tx_compl_info txcp;
	/* Remember the skbs that were transmitted */
	struct sk_buff *sent_skb_list[TX_Q_LEN];
	struct be_tx_stats stats;
	u16 pend_wrb_cnt;	/* Number of WRBs yet to be given to HW */
	u16 last_req_wrb_cnt;	/* wrb cnt of the last req in the Q */
	u16 last_req_hdr;	/* index of the last req's hdr-wrb */
} ____cacheline_aligned_in_smp;

/* Struct to remember the pages posted for rx frags */
struct be_rx_page_info {
	struct page *page;
	/* set to page-addr for last frag of the page & frag-addr otherwise */
	DEFINE_DMA_UNMAP_ADDR(bus);
	u16 page_offset;
	bool last_frag;		/* last frag of the page */
};

struct be_rx_stats {
	u64 rx_bytes;
	u64 rx_pkts;
	u64 rx_vxlan_offload_pkts;
	u32 rx_drops_no_skbs;	/* skb allocation errors */
	u32 rx_drops_no_frags;	/* HW has no fetched frags */
	u32 rx_post_fail;	/* page post alloc failures */
	u32 rx_compl;
	u32 rx_mcast_pkts;
	u32 rx_compl_err;	/* completions with err set */
	struct u64_stats_sync sync;
};

struct be_rx_compl_info {
	u32 rss_hash;
	u16 vlan_tag;
	u16 pkt_size;
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
	u8 qnq;
	u8 pkt_type;
	u8 ip_frag;
	u8 tunneled;
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
	u32 eth_red_drops;
	u32 dma_map_errors;
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
	u32 rx_address_filtered;
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
	u32 rx_roce_bytes_lsd;
	u32 rx_roce_bytes_msd;
	u32 rx_roce_frames;
	u32 roce_drops_payload_len;
	u32 roce_drops_crc;
};

/* A vlan-id of 0xFFFF must be used to clear transparent vlan-tagging */
#define BE_RESET_VLAN_TAG_ID	0xFFFF

struct be_vf_cfg {
	unsigned char mac_addr[ETH_ALEN];
	int if_handle;
	int pmac_id;
	u16 vlan_tag;
	u32 tx_rate;
	u32 plink_tracking;
	u32 privileges;
	bool spoofchk;
};

enum vf_state {
	ENABLED = 0,
	ASSIGNED = 1
};

#define BE_FLAGS_LINK_STATUS_INIT		BIT(1)
#define BE_FLAGS_SRIOV_ENABLED			BIT(2)
#define BE_FLAGS_WORKER_SCHEDULED		BIT(3)
#define BE_FLAGS_NAPI_ENABLED			BIT(6)
#define BE_FLAGS_QNQ_ASYNC_EVT_RCVD		BIT(7)
#define BE_FLAGS_VXLAN_OFFLOADS			BIT(8)
#define BE_FLAGS_SETUP_DONE			BIT(9)
#define BE_FLAGS_PHY_MISCONFIGURED		BIT(10)
#define BE_FLAGS_ERR_DETECTION_SCHEDULED	BIT(11)
#define BE_FLAGS_OS2BMC				BIT(12)

#define BE_UC_PMAC_COUNT			30
#define BE_VF_UC_PMAC_COUNT			2

#define MAX_ERR_RECOVERY_RETRY_COUNT		3
#define ERR_DETECTION_DELAY			1000
#define ERR_RECOVERY_RETRY_DELAY		30000

/* Ethtool set_dump flags */
#define LANCER_INITIATE_FW_DUMP			0x1
#define LANCER_DELETE_FW_DUMP			0x2

struct phy_info {
/* From SFF-8472 spec */
#define SFP_VENDOR_NAME_LEN			17
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
	u32 advertising;
	u32 supported;
	u8 cable_type;
	u8 vendor_name[SFP_VENDOR_NAME_LEN];
	u8 vendor_pn[SFP_VENDOR_NAME_LEN];
};

struct be_resources {
	u16 max_vfs;		/* Total VFs "really" supported by FW/HW */
	u16 max_mcast_mac;
	u16 max_tx_qs;
	u16 max_rss_qs;
	u16 max_rx_qs;
	u16 max_cq_count;
	u16 max_uc_mac;		/* Max UC MACs programmable */
	u16 max_vlans;		/* Number of vlans supported */
	u16 max_iface_count;
	u16 max_mcc_count;
	u16 max_evt_qs;
	u32 if_cap_flags;
	u32 vf_if_cap_flags;	/* VF if capability flags */
};

#define be_is_os2bmc_enabled(adapter) (adapter->flags & BE_FLAGS_OS2BMC)

struct rss_info {
	u64 rss_flags;
	u8 rsstable[RSS_INDIR_TABLE_LEN];
	u8 rss_queue[RSS_INDIR_TABLE_LEN];
	u8 rss_hkey[RSS_HASH_KEY_LEN];
};

#define BE_INVALID_DIE_TEMP	0xFF
struct be_hwmon {
	struct device *hwmon_dev;
	u8 be_on_die_temp;  /* Unit: millidegree Celsius */
};

/* Macros to read/write the 'features' word of be_wrb_params structure.
 */
#define	BE_WRB_F_BIT(name)			BE_WRB_F_##name##_BIT
#define	BE_WRB_F_MASK(name)			BIT_MASK(BE_WRB_F_##name##_BIT)

#define	BE_WRB_F_GET(word, name)	\
	(((word) & (BE_WRB_F_MASK(name))) >> BE_WRB_F_BIT(name))

#define	BE_WRB_F_SET(word, name, val)	\
	((word) |= (((val) << BE_WRB_F_BIT(name)) & BE_WRB_F_MASK(name)))

/* Feature/offload bits */
enum {
	BE_WRB_F_CRC_BIT,		/* Ethernet CRC */
	BE_WRB_F_IPCS_BIT,		/* IP csum */
	BE_WRB_F_TCPCS_BIT,		/* TCP csum */
	BE_WRB_F_UDPCS_BIT,		/* UDP csum */
	BE_WRB_F_LSO_BIT,		/* LSO */
	BE_WRB_F_LSO6_BIT,		/* LSO6 */
	BE_WRB_F_VLAN_BIT,		/* VLAN */
	BE_WRB_F_VLAN_SKIP_HW_BIT,	/* Skip VLAN tag (workaround) */
	BE_WRB_F_OS2BMC_BIT		/* Send packet to the management ring */
};

/* The structure below provides a HW-agnostic abstraction of WRB params
 * retrieved from a TX skb. This is in turn passed to chip specific routines
 * during transmit, to set the corresponding params in the WRB.
 */
struct be_wrb_params {
	u32 features;	/* Feature bits */
	u16 vlan_tag;	/* VLAN tag */
	u16 lso_mss;	/* MSS for LSO */
};

struct be_adapter {
	struct pci_dev *pdev;
	struct net_device *netdev;

	u8 __iomem *csr;	/* CSR BAR used only for BE2/3 */
	u8 __iomem *db;		/* Door Bell */
	u8 __iomem *pcicfg;	/* On SH,BEx only. Shadow of PCI config space */

	struct mutex mbox_lock; /* For serializing mbox cmds to BE card */
	struct be_dma_mem mbox_mem;
	/* Mbox mem is adjusted to align to 16 bytes. The allocated addr
	 * is stored for freeing purpose */
	struct be_dma_mem mbox_mem_alloced;

	struct be_mcc_obj mcc_obj;
	spinlock_t mcc_lock;	/* For serializing mcc cmds to BE card */
	spinlock_t mcc_cq_lock;

	u16 cfg_num_qs;		/* configured via set-channels */
	u16 num_evt_qs;
	u16 num_msix_vec;
	struct be_eq_obj eq_obj[MAX_EVT_QS];
	struct msix_entry msix_entries[MAX_MSIX_VECTORS];
	bool isr_registered;

	/* TX Rings */
	u16 num_tx_qs;
	struct be_tx_obj tx_obj[MAX_TX_QS];

	/* Rx rings */
	u16 num_rx_qs;
	u16 num_rss_qs;
	u16 need_def_rxq;
	struct be_rx_obj rx_obj[MAX_RX_QS];
	u32 big_page_size;	/* Compounded page size shared by rx wrbs */

	struct be_drv_stats drv_stats;
	struct be_aic_obj aic_obj[MAX_EVT_QS];
	u8 vlan_prio_bmap;	/* Available Priority BitMap */
	u16 recommended_prio_bits;/* Recommended Priority bits in vlan tag */
	struct be_dma_mem rx_filter; /* Cmd DMA mem for rx-filter */

	struct be_dma_mem stats_cmd;
	/* Work queue used to perform periodic tasks like getting statistics */
	struct delayed_work work;
	u16 work_counter;

	struct delayed_work be_err_detection_work;
	u8 recovery_retries;
	u8 err_flags;
	bool pcicfg_mapped;	/* pcicfg obtained via pci_iomap() */
	u32 flags;
	u32 cmd_privileges;
	/* Ethtool knobs and info */
	char fw_ver[FW_VER_LEN];
	char fw_on_flash[FW_VER_LEN];

	/* IFACE filtering fields */
	int if_handle;		/* Used to configure filtering */
	u32 if_flags;		/* Interface filtering flags */
	u32 *pmac_id;		/* MAC addr handle used by BE card */
	u32 uc_macs;		/* Count of secondary UC MAC programmed */
	unsigned long vids[BITS_TO_LONGS(VLAN_N_VID)];
	u16 vlans_added;

	u32 beacon_state;	/* for set_phys_id */

	u32 port_num;
	char port_name;
	u8 mc_type;
	u32 function_mode;
	u32 function_caps;
	u32 rx_fc;		/* Rx flow control */
	u32 tx_fc;		/* Tx flow control */
	bool stats_cmd_sent;
	struct {
		u32 size;
		u32 total_size;
		u64 io_addr;
	} roce_db;
	u32 num_msix_roce_vec;
	struct ocrdma_dev *ocrdma_dev;
	struct list_head entry;

	u32 flash_status;
	struct completion et_cmd_compl;

	struct be_resources pool_res;	/* resources available for the port */
	struct be_resources res;	/* resources available for the func */
	u16 num_vfs;			/* Number of VFs provisioned by PF */
	u8 pf_num;			/* Numbering used by FW, starts at 0 */
	u8 vf_num;			/* Numbering used by FW, starts at 1 */
	u8 virtfn;
	struct be_vf_cfg *vf_cfg;
	bool be3_native;
	u32 sli_family;
	u8 hba_port_num;
	u16 pvid;
	__be16 vxlan_port;
	int vxlan_port_count;
	int vxlan_port_aliases;
	struct phy_info phy;
	u8 wol_cap;
	bool wol_en;
	u16 asic_rev;
	u16 qnq_vid;
	u32 msg_enable;
	int be_get_temp_freq;
	struct be_hwmon hwmon_info;
	struct rss_info rss_info;
	/* Filters for packets that need to be sent to BMC */
	u32 bmc_filt_mask;
	u32 fat_dump_len;
	u16 serial_num[CNTL_SERIAL_NUM_WORDS];
	u8 phy_state; /* state of sfp optics (functional, faulted, etc.,) */
};

#define be_physfn(adapter)		(!adapter->virtfn)
#define be_virtfn(adapter)		(adapter->virtfn)
#define sriov_enabled(adapter)		(adapter->flags &	\
					 BE_FLAGS_SRIOV_ENABLED)

#define for_all_vfs(adapter, vf_cfg, i)					\
	for (i = 0, vf_cfg = &adapter->vf_cfg[i]; i < adapter->num_vfs;	\
		i++, vf_cfg++)

#define ON				1
#define OFF				0

#define be_max_vlans(adapter)		(adapter->res.max_vlans)
#define be_max_uc(adapter)		(adapter->res.max_uc_mac)
#define be_max_mc(adapter)		(adapter->res.max_mcast_mac)
#define be_max_vfs(adapter)		(adapter->pool_res.max_vfs)
#define be_max_rss(adapter)		(adapter->res.max_rss_qs)
#define be_max_txqs(adapter)		(adapter->res.max_tx_qs)
#define be_max_prio_txqs(adapter)	(adapter->res.max_prio_tx_qs)
#define be_max_rxqs(adapter)		(adapter->res.max_rx_qs)
#define be_max_eqs(adapter)		(adapter->res.max_evt_qs)
#define be_if_cap_flags(adapter)	(adapter->res.if_cap_flags)

static inline u16 be_max_qs(struct be_adapter *adapter)
{
	/* If no RSS, need atleast the one def RXQ */
	u16 num = max_t(u16, be_max_rss(adapter), 1);

	num = min(num, be_max_eqs(adapter));
	return min_t(u16, num, num_online_cpus());
}

/* Is BE in pvid_tagging mode */
#define be_pvid_tagging_enabled(adapter)	(adapter->pvid)

/* Is BE in QNQ multi-channel mode */
#define be_is_qnq_mode(adapter)		(adapter->function_mode & QNQ_MODE)

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

#define for_all_rss_queues(adapter, rxo, i)				\
	for (i = 0, rxo = &adapter->rx_obj[i]; i < adapter->num_rss_qs;	\
		i++, rxo++)

#define for_all_tx_queues(adapter, txo, i)				\
	for (i = 0, txo = &adapter->tx_obj[i]; i < adapter->num_tx_qs;	\
		i++, txo++)

#define for_all_evt_queues(adapter, eqo, i)				\
	for (i = 0, eqo = &adapter->eq_obj[i]; i < adapter->num_evt_qs; \
		i++, eqo++)

#define for_all_rx_queues_on_eq(adapter, eqo, rxo, i)			\
	for (i = eqo->idx, rxo = &adapter->rx_obj[i]; i < adapter->num_rx_qs;\
		 i += adapter->num_evt_qs, rxo += adapter->num_evt_qs)

#define for_all_tx_queues_on_eq(adapter, eqo, txo, i)			\
	for (i = eqo->idx, txo = &adapter->tx_obj[i]; i < adapter->num_tx_qs;\
		i += adapter->num_evt_qs, txo += adapter->num_evt_qs)

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

#define GET_RX_COMPL_V0_BITS(field, ptr)				\
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, field, ptr)

#define GET_RX_COMPL_V1_BITS(field, ptr)				\
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, field, ptr)

#define GET_TX_COMPL_BITS(field, ptr)					\
		AMAP_GET_BITS(struct amap_eth_tx_compl, field, ptr)

#define SET_TX_WRB_HDR_BITS(field, ptr, val)				\
		AMAP_SET_BITS(struct amap_eth_hdr_wrb, field, ptr, val)

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

#define be_cmd_status(status)		(status > 0 ? -EIO : status)

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

#define BE_ERROR_EEH		1
#define BE_ERROR_UE		BIT(1)
#define BE_ERROR_FW		BIT(2)
#define BE_ERROR_HW		(BE_ERROR_EEH | BE_ERROR_UE)
#define BE_ERROR_ANY		(BE_ERROR_EEH | BE_ERROR_UE | BE_ERROR_FW)
#define BE_CLEAR_ALL		0xFF

static inline u8 be_check_error(struct be_adapter *adapter, u32 err_type)
{
	return (adapter->err_flags & err_type);
}

static inline void be_set_error(struct be_adapter *adapter, int err_type)
{
	struct net_device *netdev = adapter->netdev;

	adapter->err_flags |= err_type;
	netif_carrier_off(netdev);

	dev_info(&adapter->pdev->dev, "%s: Link down\n", netdev->name);
}

static inline void  be_clear_error(struct be_adapter *adapter, int err_type)
{
	adapter->err_flags &= ~err_type;
}

static inline bool be_multi_rxq(const struct be_adapter *adapter)
{
	return adapter->num_rx_qs > 1;
}

void be_cq_notify(struct be_adapter *adapter, u16 qid, bool arm,
		  u16 num_popped);
void be_link_status_update(struct be_adapter *adapter, u8 link_status);
void be_parse_stats(struct be_adapter *adapter);
int be_load_fw(struct be_adapter *adapter, u8 *func);
bool be_is_wol_supported(struct be_adapter *adapter);
bool be_pause_supported(struct be_adapter *adapter);
u32 be_get_fw_log_level(struct be_adapter *adapter);
int be_update_queues(struct be_adapter *adapter);
int be_poll(struct napi_struct *napi, int budget);
void be_eqd_update(struct be_adapter *adapter, bool force_update);

/*
 * internal function to initialize-cleanup roce device.
 */
void be_roce_dev_add(struct be_adapter *);
void be_roce_dev_remove(struct be_adapter *);

/*
 * internal function to open-close roce device during ifup-ifdown.
 */
void be_roce_dev_shutdown(struct be_adapter *);

#endif				/* BE_H */
