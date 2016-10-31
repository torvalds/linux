/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*
* This software is available under the terms of the GNU General Public License
* (GPL) Version 2, available from the file COPYING in the main directory of
* this source tree.
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
#include <linux/io.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_eth_if.h>

#define QEDE_MAJOR_VERSION		8
#define QEDE_MINOR_VERSION		10
#define QEDE_REVISION_VERSION		9
#define QEDE_ENGINEERING_VERSION	20
#define DRV_MODULE_VERSION __stringify(QEDE_MAJOR_VERSION) "."	\
		__stringify(QEDE_MINOR_VERSION) "."		\
		__stringify(QEDE_REVISION_VERSION) "."		\
		__stringify(QEDE_ENGINEERING_VERSION)

#define DRV_MODULE_SYM		qede

struct qede_stats {
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

	/* port */
	u64 rx_64_byte_packets;
	u64 rx_65_to_127_byte_packets;
	u64 rx_128_to_255_byte_packets;
	u64 rx_256_to_511_byte_packets;
	u64 rx_512_to_1023_byte_packets;
	u64 rx_1024_to_1518_byte_packets;
	u64 rx_1519_to_1522_byte_packets;
	u64 rx_1519_to_2047_byte_packets;
	u64 rx_2048_to_4095_byte_packets;
	u64 rx_4096_to_9216_byte_packets;
	u64 rx_9217_to_16383_byte_packets;
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
	u64 tx_1519_to_2047_byte_packets;
	u64 tx_2048_to_4095_byte_packets;
	u64 tx_4096_to_9216_byte_packets;
	u64 tx_9217_to_16383_byte_packets;
	u64 tx_pause_frames;
	u64 tx_pfc_frames;
	u64 tx_lpi_entry_count;
	u64 tx_total_collisions;
	u64 brb_truncates;
	u64 brb_discards;
	u64 tx_mac_ctrl_frames;
};

struct qede_vlan {
	struct list_head list;
	u16 vid;
	bool configured;
};

struct qede_rdma_dev {
	struct qedr_dev *qedr_dev;
	struct list_head entry;
	struct list_head roce_event_list;
	struct workqueue_struct *roce_wq;
};

struct qede_dev {
	struct qed_dev			*cdev;
	struct net_device		*ndev;
	struct pci_dev			*pdev;

	u32				dp_module;
	u8				dp_level;

	u32 flags;
#define QEDE_FLAG_IS_VF	BIT(0)
#define IS_VF(edev)	(!!((edev)->flags & QEDE_FLAG_IS_VF))

	const struct qed_eth_ops	*ops;

	struct qed_dev_eth_info	dev_info;
#define QEDE_MAX_RSS_CNT(edev)	((edev)->dev_info.num_queues)
#define QEDE_MAX_TSS_CNT(edev)	((edev)->dev_info.num_queues * \
				 (edev)->dev_info.num_tc)

	struct qede_fastpath		*fp_array;
	u8				req_num_tx;
	u8				fp_num_tx;
	u8				req_num_rx;
	u8				fp_num_rx;
	u16				req_queues;
	u16				num_queues;
	u8				num_tc;
#define QEDE_QUEUE_CNT(edev)	((edev)->num_queues)
#define QEDE_RSS_COUNT(edev)	((edev)->num_queues - (edev)->fp_num_tx)
#define QEDE_TSS_COUNT(edev)	(((edev)->num_queues - (edev)->fp_num_rx) * \
				 (edev)->num_tc)
#define QEDE_TX_IDX(edev, txqidx)	((edev)->fp_num_rx + (txqidx) % \
					 QEDE_TSS_COUNT(edev))
#define QEDE_TC_IDX(edev, txqidx)	((txqidx) / QEDE_TSS_COUNT(edev))
#define QEDE_TX_QUEUE(edev, txqidx)	\
	(&(edev)->fp_array[QEDE_TX_IDX((edev), (txqidx))].txqs[QEDE_TC_IDX(\
							(edev), (txqidx))])

	struct qed_int_info		int_info;
	unsigned char			primary_mac[ETH_ALEN];

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
	struct qed_update_vport_rss_params	rss_params;
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

	bool wol_enabled;

	struct qede_rdma_dev		rdma_info;
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
	struct sw_rx_data replace_buf;
	dma_addr_t replace_buf_mapping;
	struct sw_rx_data start_buf;
	dma_addr_t start_buf_mapping;
	struct eth_fast_path_rx_tpa_start_cqe start_cqe;
	enum qede_agg_state agg_state;
	struct sk_buff *skb;
	int frag_id;
	u16 vlan_tag;
};

struct qede_rx_queue {
	__le16			*hw_cons_ptr;
	struct sw_rx_data	*sw_rx_ring;
	u16			sw_rx_cons;
	u16			sw_rx_prod;
	struct qed_chain	rx_bd_ring;
	struct qed_chain	rx_comp_ring;
	void __iomem		*hw_rxq_prod_addr;

	/* GRO */
	struct qede_agg_info	tpa_info[ETH_TPA_MAX_AGGS_NUM];

	int			rx_buf_size;
	unsigned int		rx_buf_seg_size;

	u16			num_rx_buffers;
	u16			rxq_id;

	u64			rcv_pkts;
	u64			rx_hw_errors;
	u64			rx_alloc_errors;
	u64			rx_ip_frags;
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

struct qede_tx_queue {
	int			index; /* Queue index */
	__le16			*hw_cons_ptr;
	struct sw_tx_bd		*sw_tx_ring;
	u16			sw_tx_cons;
	u16			sw_tx_prod;
	struct qed_chain	tx_pbl;
	void __iomem		*doorbell_addr;
	union db_prod		tx_db;

	u16			num_tx_buffers;
	u64			xmit_pkts;
	u64			stopped_cnt;

	bool			is_legacy;
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
#define QEDE_FASTPATH_COMBINED	(QEDE_FASTPATH_TX | QEDE_FASTPATH_RX)
	u8			type;
	u8			id;
	struct napi_struct	napi;
	struct qed_sb_info	*sb_info;
	struct qede_rx_queue	*rxq;
	struct qede_tx_queue	*txqs;

#define VEC_NAME_SIZE	(sizeof(((struct net_device *)0)->name) + 8)
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
#define QEDE_SP_VXLAN_PORT_CONFIG	2
#define QEDE_SP_GENEVE_PORT_CONFIG	3

union qede_reload_args {
	u16 mtu;
};

#ifdef CONFIG_DCB
void qede_set_dcbnl_ops(struct net_device *ndev);
#endif
void qede_config_debug(uint debug, u32 *p_dp_module, u8 *p_dp_level);
void qede_set_ethtool_ops(struct net_device *netdev);
void qede_reload(struct qede_dev *edev,
		 void (*func)(struct qede_dev *edev,
			      union qede_reload_args *args),
		 union qede_reload_args *args);
int qede_change_mtu(struct net_device *dev, int new_mtu);
void qede_fill_by_demand_stats(struct qede_dev *edev);
bool qede_has_rx_work(struct qede_rx_queue *rxq);
int qede_txq_has_work(struct qede_tx_queue *txq);
void qede_recycle_rx_bd_ring(struct qede_rx_queue *rxq, struct qede_dev *edev,
			     u8 count);
void qede_update_rx_prod(struct qede_dev *edev, struct qede_rx_queue *rxq);

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

#endif /* _QEDE_H_ */
