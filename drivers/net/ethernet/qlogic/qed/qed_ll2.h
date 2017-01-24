/* QLogic qed NIC Driver
 *
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_LL2_H
#define _QED_LL2_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_ll2_if.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_sp.h"

#define QED_MAX_NUM_OF_LL2_CONNECTIONS                    (4)

enum qed_ll2_roce_flavor_type {
	QED_LL2_ROCE,
	QED_LL2_RROCE,
	MAX_QED_LL2_ROCE_FLAVOR_TYPE
};

enum qed_ll2_conn_type {
	QED_LL2_TYPE_RESERVED,
	QED_LL2_TYPE_ISCSI,
	QED_LL2_TYPE_TEST,
	QED_LL2_TYPE_ISCSI_OOO,
	QED_LL2_TYPE_RESERVED2,
	QED_LL2_TYPE_ROCE,
	QED_LL2_TYPE_RESERVED3,
	MAX_QED_LL2_RX_CONN_TYPE
};

enum qed_ll2_tx_dest {
	QED_LL2_TX_DEST_NW, /* Light L2 TX Destination to the Network */
	QED_LL2_TX_DEST_LB, /* Light L2 TX Destination to the Loopback */
	QED_LL2_TX_DEST_MAX
};

struct qed_ll2_rx_packet {
	struct list_head list_entry;
	struct core_rx_bd_with_buff_len *rxq_bd;
	dma_addr_t rx_buf_addr;
	u16 buf_length;
	void *cookie;
	u8 placement_offset;
	u16 parse_flags;
	u16 packet_length;
	u16 vlan;
	u32 opaque_data[2];
};

struct qed_ll2_tx_packet {
	struct list_head list_entry;
	u16 bd_used;
	u16 vlan;
	u16 l4_hdr_offset_w;
	u8 bd_flags;
	bool notify_fw;
	void *cookie;

	struct {
		struct core_tx_bd *txq_bd;
		dma_addr_t tx_frag;
		u16 frag_len;
	} bds_set[ETH_TX_MAX_BDS_PER_NON_LSO_PACKET];
};

struct qed_ll2_rx_queue {
	/* Lock protecting the Rx queue manipulation */
	spinlock_t lock;
	struct qed_chain rxq_chain;
	struct qed_chain rcq_chain;
	u8 rx_sb_index;
	bool b_cb_registred;
	__le16 *p_fw_cons;
	struct list_head active_descq;
	struct list_head free_descq;
	struct list_head posting_descq;
	struct qed_ll2_rx_packet *descq_array;
	void __iomem *set_prod_addr;
};

struct qed_ll2_tx_queue {
	/* Lock protecting the Tx queue manipulation */
	spinlock_t lock;
	struct qed_chain txq_chain;
	u8 tx_sb_index;
	bool b_cb_registred;
	__le16 *p_fw_cons;
	struct list_head active_descq;
	struct list_head free_descq;
	struct list_head sending_descq;
	struct qed_ll2_tx_packet *descq_array;
	struct qed_ll2_tx_packet *cur_send_packet;
	struct qed_ll2_tx_packet cur_completing_packet;
	u16 cur_completing_bd_idx;
	void __iomem *doorbell_addr;
	u16 bds_idx;
	u16 cur_send_frag_num;
	u16 cur_completing_frag_num;
	bool b_completing_packet;
};

struct qed_ll2_info {
	/* Lock protecting the state of LL2 */
	struct mutex mutex;
	enum qed_ll2_conn_type conn_type;
	u32 cid;
	u8 my_id;
	u8 queue_id;
	u8 tx_stats_id;
	bool b_active;
	u16 mtu;
	u8 rx_drop_ttl0_flg;
	u8 rx_vlan_removal_en;
	u8 tx_tc;
	enum core_tx_dest tx_dest;
	enum core_error_handle ai_err_packet_too_big;
	enum core_error_handle ai_err_no_buf;
	u8 tx_stats_en;
	struct qed_ll2_rx_queue rx_queue;
	struct qed_ll2_tx_queue tx_queue;
	u8 gsi_enable;
};

/**
 * @brief qed_ll2_acquire_connection - allocate resources,
 *        starts rx & tx (if relevant) queues pair. Provides
 *        connecion handler as output parameter.
 *
 * @param p_hwfn
 * @param p_params		Contain various configuration properties
 * @param rx_num_desc
 * @param tx_num_desc
 *
 * @param p_connection_handle  Output container for LL2 connection's handle
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_acquire_connection(struct qed_hwfn *p_hwfn,
			       struct qed_ll2_info *p_params,
			       u16 rx_num_desc,
			       u16 tx_num_desc,
			       u8 *p_connection_handle);

/**
 * @brief qed_ll2_establish_connection - start previously
 *        allocated LL2 queues pair
 *
 * @param p_hwfn
 * @param p_ptt
 * @param connection_handle	LL2 connection's handle obtained from
 *                              qed_ll2_require_connection
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_establish_connection(struct qed_hwfn *p_hwfn, u8 connection_handle);

/**
 * @brief qed_ll2_post_rx_buffers - submit buffers to LL2 Rx queue.
 *
 * @param p_hwfn
 * @param connection_handle	LL2 connection's handle obtained from
 *				qed_ll2_require_connection
 * @param addr			rx (physical address) buffers to submit
 * @param cookie
 * @param notify_fw		produce corresponding Rx BD immediately
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_post_rx_buffer(struct qed_hwfn *p_hwfn,
			   u8 connection_handle,
			   dma_addr_t addr,
			   u16 buf_len, void *cookie, u8 notify_fw);

/**
 * @brief qed_ll2_prepare_tx_packet - request for start Tx BD
 *				      to prepare Tx packet submission to FW.
 *
 * @param p_hwfn
 * @param connection_handle	LL2 connection's handle obtained from
 *				qed_ll2_require_connection
 * @param num_of_bds		a number of requested BD equals a number of
 *				fragments in Tx packet
 * @param vlan			VLAN to insert to packet (if insertion set)
 * @param bd_flags
 * @param l4_hdr_offset_w	L4 Header Offset from start of packet
 *				(in words). This is needed if both l4_csum
 *				and ipv6_ext are set
 * @param e_tx_dest             indicates if the packet is to be transmitted via
 *                              loopback or to the network
 * @param first_frag
 * @param first_frag_len
 * @param cookie
 *
 * @param notify_fw
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_prepare_tx_packet(struct qed_hwfn *p_hwfn,
			      u8 connection_handle,
			      u8 num_of_bds,
			      u16 vlan,
			      u8 bd_flags,
			      u16 l4_hdr_offset_w,
			      enum qed_ll2_tx_dest e_tx_dest,
			      enum qed_ll2_roce_flavor_type qed_roce_flavor,
			      dma_addr_t first_frag,
			      u16 first_frag_len, void *cookie, u8 notify_fw);

/**
 * @brief qed_ll2_release_connection -	releases resources
 *					allocated for LL2 connection
 *
 * @param p_hwfn
 * @param connection_handle		LL2 connection's handle obtained from
 *					qed_ll2_require_connection
 */
void qed_ll2_release_connection(struct qed_hwfn *p_hwfn, u8 connection_handle);

/**
 * @brief qed_ll2_set_fragment_of_tx_packet -	provides fragments to fill
 *						Tx BD of BDs requested by
 *						qed_ll2_prepare_tx_packet
 *
 * @param p_hwfn
 * @param connection_handle			LL2 connection's handle
 *						obtained from
 *						qed_ll2_require_connection
 * @param addr
 * @param nbytes
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_set_fragment_of_tx_packet(struct qed_hwfn *p_hwfn,
				      u8 connection_handle,
				      dma_addr_t addr, u16 nbytes);

/**
 * @brief qed_ll2_terminate_connection -	stops Tx/Rx queues
 *
 *
 * @param p_hwfn
 * @param connection_handle			LL2 connection's handle
 *						obtained from
 *						qed_ll2_require_connection
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_terminate_connection(struct qed_hwfn *p_hwfn, u8 connection_handle);

/**
 * @brief qed_ll2_get_stats -	get LL2 queue's statistics
 *
 *
 * @param p_hwfn
 * @param connection_handle	LL2 connection's handle obtained from
 *				qed_ll2_require_connection
 * @param p_stats
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_get_stats(struct qed_hwfn *p_hwfn,
		      u8 connection_handle, struct qed_ll2_stats *p_stats);

/**
 * @brief qed_ll2_alloc - Allocates LL2 connections set
 *
 * @param p_hwfn
 *
 * @return pointer to alocated qed_ll2_info or NULL
 */
struct qed_ll2_info *qed_ll2_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ll2_setup - Inits LL2 connections set
 *
 * @param p_hwfn
 * @param p_ll2_connections
 *
 */
void qed_ll2_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ll2_info *p_ll2_connections);

/**
 * @brief qed_ll2_free - Releases LL2 connections set
 *
 * @param p_hwfn
 * @param p_ll2_connections
 *
 */
void qed_ll2_free(struct qed_hwfn *p_hwfn,
		  struct qed_ll2_info *p_ll2_connections);
#endif
