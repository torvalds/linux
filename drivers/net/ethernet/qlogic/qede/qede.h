/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _QEDE_H_
#define _QEDE_H_
#include <linux/compiler.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/bpf.h>
#include <net/xdp.h>
#include <linux/qed/qede_rdma.h>
#include <linux/io.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include <linux/qed/common_hsi.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_eth_if.h>

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>

#define QEDE_MAJOR_VERSION		8
#define QEDE_MINOR_VERSION		33
#define QEDE_REVISION_VERSION		0
#define QEDE_ENGINEERING_VERSION	20
#define DRV_MODULE_VERSION __stringify(QEDE_MAJOR_VERSION) "."	\
		__stringify(QEDE_MINOR_VERSION) "."		\
		__stringify(QEDE_REVISION_VERSION) "."		\
		__stringify(QEDE_ENGINEERING_VERSION)

#define DRV_MODULE_SYM		qede

struct qede_stats_common {
	u64 no_buff_discards;
	u64 packet_too_big_discard;
	u64 ttl0_discard;
	u64 rx_ucast_bytes;
	u64 rx_mcast_bytes;
	u64 rx_bcast_bytes;
	u64 rx_ucast_pkts;
	u64 rx_mcast_pkts;
	u64 rx_bcast_pkts;
	u64 mftag_filter_discards;
	u64 mac_filter_discards;
	u64 gft_filter_drop;
	u64 tx_ucast_bytes;
	u64 tx_mcast_bytes;
	u64 tx_bcast_bytes;
	u64 tx_ucast_pkts;
	u64 tx_mcast_pkts;
	u64 tx_bcast_pkts;
	u64 tx_err_drop_pkts;
	u64 coalesced_pkts;
	u64 coalesced_events;
	u64 coalesced_aborts_num;
	u64 non_coalesced_pkts;
	u64 coalesced_bytes;
	u64 link_change_count;

	/* port */
	u64 rx_64_byte_packets;
	u64 rx_65_to_127_byte_packets;
	u64 rx_128_to_255_byte_packets;
	u64 rx_256_to_511_byte_packets;
	u64 rx_512_to_1023_byte_packets;
	u64 rx_1024_to_1518_byte_packets;
	u64 rx_crc_errors;
	u64 rx_mac_crtl_frames;
	u64 rx_pause_frames;
	u64 rx_pfc_frames;
	u64 rx_align_errors;
	u64 rx_carrier_errors;
	u64 rx_oversize_packets;
	u64 rx_jabbers;
	u64 rx_undersize_packets;
	u64 rx_fragments;
	u64 tx_64_byte_packets;
	u64 tx_65_to_127_byte_packets;
	u64 tx_128_to_255_byte_packets;
	u64 tx_256_to_511_byte_packets;
	u64 tx_512_to_1023_byte_packets;
	u64 tx_1024_to_1518_byte_packets;
	u64 tx_pause_frames;
	u64 tx_pfc_frames;
	u64 brb_truncates;
	u64 brb_discards;
	u64 tx_mac_ctrl_frames;
};

struct qede_stats_bb {
	u64 rx_1519_to_1522_byte_packets;
	u64 rx_1519_to_2047_byte_packets;
	u64 rx_2048_to_4095_byte_packets;
	u64 rx_4096_to_9216_byte_packets;
	u64 rx_9217_to_16383_byte_packets;
	u64 tx_1519_to_2047_byte_packets;
	u64 tx_2048_to_4095_byte_packets;
	u64 tx_4096_to_9216_byte_packets;
	u64 tx_9217_to_16383_byte_packets;
	u64 tx_lpi_entry_count;
	u64 tx_total_collisions;
};

struct qede_stats_ah {
	u64 rx_1519_to_max_byte_packets;
	u64 tx_1519_to_max_byte_packets;
};

struct qede_stats {
	struct qede_stats_common common;

	union {
		struct qede_stats_bb bb;
		struct qede_stats_ah ah;
	};
};

struct qede_vlan {
	struct list_head list;
	u16 vid;
	bool configured;
};

struct qede_rdma_dev {
	struct qedr_dev *qedr_dev;
	struct list_head entry;
	struct list_head rdma_event_list;
	struct workqueue_struct *rdma_wq;
};

struct qede_ptp;

#define QEDE_RFS_MAX_FLTR	256

enum qede_flags_bit {
	QEDE_FLAGS_IS_VF = 0,
	QEDE_FLAGS_LINK_REQUESTED,
	QEDE_FLAGS_PTP_TX_IN_PRORGESS,
	QEDE_FLAGS_TX_TIMESTAMPING_EN
};

struct qede_dev {
	struct qed_dev			*cdev;
	struct net_device		*ndev;
	struct pci_dev			*pdev;

	u32				dp_module;
	u8				dp_level;

	unsigned long flags;
#define IS_VF(edev)	(test_bit(QEDE_FLAGS_IS_VF, &(edev)->flags))

	const struct qed_eth_ops	*ops;
	struct qede_ptp			*ptp;

	struct qed_dev_eth_info dev_info;
#define QEDE_MAX_RSS_CNT(edev)	((edev)->dev_info.num_queues)
#define QEDE_MAX_TSS_CNT(edev)	((edev)->dev_info.num_queues)
#define QEDE_IS_BB(edev) \
	((edev)->dev_info.common.dev_type == QED_DEV_TYPE_BB)
#define QEDE_IS_AH(edev) \
	((edev)->dev_info.common.dev_type == QED_DEV_TYPE_AH)

	struct qede_fastpath		*fp_array;
	u8				req_num_tx;
	u8				fp_num_tx;
	u8				req_num_rx;
	u8				fp_num_rx;
	u16				req_queues;
	u16				num_queues;
#define QEDE_QUEUE_CNT(edev)	((edev)->num_queues)
#define QEDE_RSS_COUNT(edev)	((edev)->num_queues - (edev)->fp_num_tx)
#define QEDE_RX_QUEUE_IDX(edev, i)	(i)
#define QEDE_TSS_COUNT(edev)	((edev)->num_queues - (edev)->fp_num_rx)

	struct qed_int_info		int_info;

	/* Smaller private varaiant of the RTNL lock */
	struct mutex			qede_lock;
	u32				state; /* Protected by qede_lock */
	u16				rx_buf_size;
	u32				rx_copybreak;

	/* L2 header size + 2*VLANs (8 bytes) + LLC SNAP (8 bytes) */
#define ETH_OVERHEAD			(ETH_HLEN + 8 + 8)
	/* Max supported alignment is 256 (8 shift)
	 * minimal alignment shift 6 is optimal for 57xxx HW performance
	 */
#define QEDE_RX_ALIGN_SHIFT		max(6, min(8, L1_CACHE_SHIFT))
	/* We assume skb_build() uses sizeof(struct skb_shared_info) bytes
	 * at the end of skb->data, to avoid wasting a full cache line.
	 * This reduces memory use (skb->truesize).
	 */
#define QEDE_FW_RX_ALIGN_END					\
	max_t(u64, 1UL << QEDE_RX_ALIGN_SHIFT,			\
	      SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

	struct qede_stats		stats;
#define QEDE_RSS_INDIR_INITED	BIT(0)
#define QEDE_RSS_KEY_INITED	BIT(1)
#define QEDE_RSS_CAPS_INITED	BIT(2)
	u32 rss_params_inited; /* bit-field to track initialized rss params */
	u16 rss_ind_table[128];
	u32 rss_key[10];
	u8 rss_caps;

	u16			q_num_rx_buffers; /* Must be a power of two */
	u16			q_num_tx_buffers; /* Must be a power of two */

	bool gro_disable;
	struct list_head vlan_list;
	u16 configured_vlans;
	u16 non_configured_vlans;
	bool accept_any_vlan;
	struct delayed_work		sp_task;
	unsigned long			sp_flags;
	u16				vxlan_dst_port;
	u16				geneve_dst_port;

	struct qede_arfs		*arfs;
	bool				wol_enabled;

	struct qede_rdma_dev		rdma_info;

	struct bpf_prog *xdp_prog;
};

enum QEDE_STATE {
	QEDE_STATE_CLOSED,
	QEDE_STATE_OPEN,
};

#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))

#define	MAX_NUM_TC	8
#define	MAX_NUM_PRI	8

/* The driver supports the new build_skb() API:
 * RX ring buffer contains pointer to kmalloc() data only,
 * skb are built only after the frame was DMA-ed.
 */
struct sw_rx_data {
	struct page *data;
	dma_addr_t mapping;
	unsigned int page_offset;
};

enum qede_agg_state {
	QEDE_AGG_STATE_NONE  = 0,
	QEDE_AGG_STATE_START = 1,
	QEDE_AGG_STATE_ERROR = 2
};

struct qede_agg_info {
	/* rx_buf is a data buffer that can be placed / consumed from rx bd
	 * chain. It has two purposes: We will preallocate the data buffer
	 * for each aggregation when we open the interface and will place this
	 * buffer on the rx-bd-ring when we receive TPA_START. We don't want
	 * to be in a state where allocation fails, as we can't reuse the
	 * consumer buffer in the rx-chain since FW may still be writing to it
	 * (since header needs to be modified for TPA).
	 * The second purpose is to keep a pointer to the bd buffer during
	 * aggregation.
	 */
	struct sw_rx_data buffer;
	struct sk_buff *skb;

	/* We need some structs from the start cookie until termination */
	u16 vlan_tag;

	bool tpa_start_fail;
	u8 state;
	u8 frag_id;

	u8 tunnel_type;
};

struct qede_rx_queue {
	__le16 *hw_cons_ptr;
	void __iomem *hw_rxq_prod_addr;

	/* Required for the allocation of replacement buffers */
	struct device *dev;

	struct bpf_prog *xdp_prog;

	u16 sw_rx_cons;
	u16 sw_rx_prod;

	u16 filled_buffers;
	u8 data_direction;
	u8 rxq_id;

	/* Used once per each NAPI run */
	u16 num_rx_buffers;

	u16 rx_headroom;

	u32 rx_buf_size;
	u32 rx_buf_seg_size;

	struct sw_rx_data *sw_rx_ring;
	struct qed_chain rx_bd_ring;
	struct qed_chain rx_comp_ring ____cacheline_aligned;

	/* GRO */
	struct qede_agg_info tpa_info[ETH_TPA_MAX_AGGS_NUM];

	/* Used once per each NAPI run */
	u64 rcv_pkts;

	u64 rx_hw_errors;
	u64 rx_alloc_errors;
	u64 rx_ip_frags;

	u64 xdp_no_pass;

	void *handle;
	struct xdp_rxq_info xdp_rxq;
};

union db_prod {
	struct eth_db_data data;
	u32		raw;
};

struct sw_tx_bd {
	struct sk_buff *skb;
	u8 flags;
/* Set on the first BD descriptor when there is a split BD */
#define QEDE_TSO_SPLIT_BD		BIT(0)
};

struct sw_tx_xdp {
	struct page *page;
	dma_addr_t mapping;
};

struct qede_tx_queue {
	u8 is_xdp;
	bool is_legacy;
	u16 sw_tx_cons;
	u16 sw_tx_prod;
	u16 num_tx_buffers; /* Slowpath only */

	u64 xmit_pkts;
	u64 stopped_cnt;

	__le16 *hw_cons_ptr;

	/* Needed for the mapping of packets */
	struct device *dev;

	void __iomem *doorbell_addr;
	union db_prod tx_db;
	int index; /* Slowpath only */
#define QEDE_TXQ_XDP_TO_IDX(edev, txq)	((txq)->index - \
					 QEDE_MAX_TSS_CNT(edev))
#define QEDE_TXQ_IDX_TO_XDP(edev, idx)	((idx) + QEDE_MAX_TSS_CNT(edev))
#define QEDE_NDEV_TXQ_ID_TO_FP_ID(edev, idx)	((edev)->fp_num_rx + \
						 ((idx) % QEDE_TSS_COUNT(edev)))
#define QEDE_NDEV_TXQ_ID_TO_TXQ_COS(edev, idx)	((idx) / QEDE_TSS_COUNT(edev))
#define QEDE_TXQ_TO_NDEV_TXQ_ID(edev, txq)	((QEDE_TSS_COUNT(edev) * \
						 (txq)->cos) + (txq)->index)
#define QEDE_NDEV_TXQ_ID_TO_TXQ(edev, idx)	\
	(&((edev)->fp_array[QEDE_NDEV_TXQ_ID_TO_FP_ID(edev, idx)].txq \
	[QEDE_NDEV_TXQ_ID_TO_TXQ_COS(edev, idx)]))
#define QEDE_FP_TC0_TXQ(fp)	(&((fp)->txq[0]))

	/* Regular Tx requires skb + metadata for release purpose,
	 * while XDP requires the pages and the mapped address.
	 */
	union {
		struct sw_tx_bd *skbs;
		struct sw_tx_xdp *xdp;
	} sw_tx_ring;

	struct qed_chain tx_pbl;

	/* Slowpath; Should be kept in end [unless missing padding] */
	void *handle;
	u16 cos;
	u16 ndev_txq_id;
};

#define BD_UNMAP_ADDR(bd)		HILO_U64(le32_to_cpu((bd)->addr.hi), \
						 le32_to_cpu((bd)->addr.lo))
#define BD_SET_UNMAP_ADDR_LEN(bd, maddr, len)				\
	do {								\
		(bd)->addr.hi = cpu_to_le32(upper_32_bits(maddr));	\
		(bd)->addr.lo = cpu_to_le32(lower_32_bits(maddr));	\
		(bd)->nbytes = cpu_to_le16(len);			\
	} while (0)
#define BD_UNMAP_LEN(bd)		(le16_to_cpu((bd)->nbytes))

struct qede_fastpath {
	struct qede_dev	*edev;
#define QEDE_FASTPATH_TX	BIT(0)
#define QEDE_FASTPATH_RX	BIT(1)
#define QEDE_FASTPATH_XDP	BIT(2)
#define QEDE_FASTPATH_COMBINED	(QEDE_FASTPATH_TX | QEDE_FASTPATH_RX)
	u8			type;
	u8			id;
	u8			xdp_xmit;
	struct napi_struct	napi;
	struct qed_sb_info	*sb_info;
	struct qede_rx_queue	*rxq;
	struct qede_tx_queue	*txq;
	struct qede_tx_queue	*xdp_tx;

#define VEC_NAME_SIZE  (FIELD_SIZEOF(struct net_device, name) + 8)
	char	name[VEC_NAME_SIZE];
};

/* Debug print definitions */
#define DP_NAME(edev) ((edev)->ndev->name)

#define XMIT_PLAIN		0
#define XMIT_L4_CSUM		BIT(0)
#define XMIT_LSO		BIT(1)
#define XMIT_ENC		BIT(2)
#define XMIT_ENC_GSO_L4_CSUM	BIT(3)

#define QEDE_CSUM_ERROR			BIT(0)
#define QEDE_CSUM_UNNECESSARY		BIT(1)
#define QEDE_TUNN_CSUM_UNNECESSARY	BIT(2)

#define QEDE_SP_RX_MODE			1

#ifdef CONFIG_RFS_ACCEL
int qede_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id);
#define QEDE_SP_ARFS_CONFIG	4
#define QEDE_SP_TASK_POLL_DELAY	(5 * HZ)
#endif

void qede_process_arfs_filters(struct qede_dev *edev, bool free_fltr);
void qede_poll_for_freeing_arfs_filters(struct qede_dev *edev);
void qede_arfs_filter_op(void *dev, void *filter, u8 fw_rc);
void qede_free_arfs(struct qede_dev *edev);
int qede_alloc_arfs(struct qede_dev *edev);
int qede_add_cls_rule(struct qede_dev *edev, struct ethtool_rxnfc *info);
int qede_delete_flow_filter(struct qede_dev *edev, u64 cookie);
int qede_get_cls_rule_entry(struct qede_dev *edev, struct ethtool_rxnfc *cmd);
int qede_get_cls_rule_all(struct qede_dev *edev, struct ethtool_rxnfc *info,
			  u32 *rule_locs);
int qede_get_arfs_filter_count(struct qede_dev *edev);

struct qede_reload_args {
	void (*func)(struct qede_dev *edev, struct qede_reload_args *args);
	union {
		netdev_features_t features;
		struct bpf_prog *new_prog;
		u16 mtu;
	} u;
};

/* Datapath functions definition */
netdev_tx_t qede_start_xmit(struct sk_buff *skb, struct net_device *ndev);
netdev_features_t qede_features_check(struct sk_buff *skb,
				      struct net_device *dev,
				      netdev_features_t features);
void qede_tx_log_print(struct qede_dev *edev, struct qede_fastpath *fp);
int qede_alloc_rx_buffer(struct qede_rx_queue *rxq, bool allow_lazy);
int qede_free_tx_pkt(struct qede_dev *edev,
		     struct qede_tx_queue *txq, int *len);
int qede_poll(struct napi_struct *napi, int budget);
irqreturn_t qede_msix_fp_int(int irq, void *fp_cookie);

/* Filtering function definitions */
void qede_force_mac(void *dev, u8 *mac, bool forced);
void qede_udp_ports_update(void *dev, u16 vxlan_port, u16 geneve_port);
int qede_set_mac_addr(struct net_device *ndev, void *p);

int qede_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid);
int qede_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid);
void qede_vlan_mark_nonconfigured(struct qede_dev *edev);
int qede_configure_vlan_filters(struct qede_dev *edev);

netdev_features_t qede_fix_features(struct net_device *dev,
				    netdev_features_t features);
int qede_set_features(struct net_device *dev, netdev_features_t features);
void qede_set_rx_mode(struct net_device *ndev);
void qede_config_rx_mode(struct net_device *ndev);
void qede_fill_rss_params(struct qede_dev *edev,
			  struct qed_update_vport_rss_params *rss, u8 *update);

void qede_udp_tunnel_add(struct net_device *dev, struct udp_tunnel_info *ti);
void qede_udp_tunnel_del(struct net_device *dev, struct udp_tunnel_info *ti);

int qede_xdp(struct net_device *dev, struct netdev_bpf *xdp);

#ifdef CONFIG_DCB
void qede_set_dcbnl_ops(struct net_device *ndev);
#endif

void qede_config_debug(uint debug, u32 *p_dp_module, u8 *p_dp_level);
void qede_set_ethtool_ops(struct net_device *netdev);
void qede_reload(struct qede_dev *edev,
		 struct qede_reload_args *args, bool is_locked);
int qede_change_mtu(struct net_device *dev, int new_mtu);
void qede_fill_by_demand_stats(struct qede_dev *edev);
void __qede_lock(struct qede_dev *edev);
void __qede_unlock(struct qede_dev *edev);
bool qede_has_rx_work(struct qede_rx_queue *rxq);
int qede_txq_has_work(struct qede_tx_queue *txq);
void qede_recycle_rx_bd_ring(struct qede_rx_queue *rxq, u8 count);
void qede_update_rx_prod(struct qede_dev *edev, struct qede_rx_queue *rxq);
int qede_add_tc_flower_fltr(struct qede_dev *edev, __be16 proto,
			    struct tc_cls_flower_offload *f);

#define RX_RING_SIZE_POW	13
#define RX_RING_SIZE		((u16)BIT(RX_RING_SIZE_POW))
#define NUM_RX_BDS_MAX		(RX_RING_SIZE - 1)
#define NUM_RX_BDS_MIN		128
#define NUM_RX_BDS_DEF		((u16)BIT(10) - 1)

#define TX_RING_SIZE_POW	13
#define TX_RING_SIZE		((u16)BIT(TX_RING_SIZE_POW))
#define NUM_TX_BDS_MAX		(TX_RING_SIZE - 1)
#define NUM_TX_BDS_MIN		128
#define NUM_TX_BDS_DEF		NUM_TX_BDS_MAX

#define QEDE_MIN_PKT_LEN		64
#define QEDE_RX_HDR_SIZE		256
#define QEDE_MAX_JUMBO_PACKET_SIZE	9600
#define	for_each_queue(i) for (i = 0; i < edev->num_queues; i++)
#define for_each_cos_in_txq(edev, var) \
	for ((var) = 0; (var) < (edev)->dev_info.num_tc; (var)++)

#endif /* _QEDE_H_ */
