/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
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
/* LL2 queues handles will be split as follows:
 * first will be legacy queues, and then the ctx based queues.
 */
#define QED_MAX_NUM_OF_LL2_CONNS_PF            (4)
#define QED_MAX_NUM_OF_LEGACY_LL2_CONNS_PF   (3)

#define QED_MAX_NUM_OF_CTX_LL2_CONNS_PF	\
	(QED_MAX_NUM_OF_LL2_CONNS_PF - QED_MAX_NUM_OF_LEGACY_LL2_CONNS_PF)

#define QED_LL2_LEGACY_CONN_BASE_PF     0
#define QED_LL2_CTX_CONN_BASE_PF        QED_MAX_NUM_OF_LEGACY_LL2_CONNS_PF

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
	bool notify_fw;
	void *cookie;
	/* Flexible Array of bds_set determined by max_bds_per_packet */
	struct {
		struct core_tx_bd *txq_bd;
		dma_addr_t tx_frag;
		u16 frag_len;
	} bds_set[];
};

struct qed_ll2_rx_queue {
	/* Lock protecting the Rx queue manipulation */
	spinlock_t lock;
	struct qed_chain rxq_chain;
	struct qed_chain rcq_chain;
	u8 rx_sb_index;
	u8 ctx_based;
	bool b_cb_registered;
	__le16 *p_fw_cons;
	struct list_head active_descq;
	struct list_head free_descq;
	struct list_head posting_descq;
	struct qed_ll2_rx_packet *descq_array;
	void __iomem *set_prod_addr;
	struct core_pwm_prod_update_data db_data;
};

struct qed_ll2_tx_queue {
	/* Lock protecting the Tx queue manipulation */
	spinlock_t lock;
	struct qed_chain txq_chain;
	u8 tx_sb_index;
	bool b_cb_registered;
	__le16 *p_fw_cons;
	struct list_head active_descq;
	struct list_head free_descq;
	struct list_head sending_descq;
	u16 cur_completing_bd_idx;
	void __iomem *doorbell_addr;
	struct core_db_data db_msg;
	u16 bds_idx;
	u16 cur_send_frag_num;
	u16 cur_completing_frag_num;
	bool b_completing_packet;
	void *descq_mem; /* memory for variable sized qed_ll2_tx_packet*/
	struct qed_ll2_tx_packet *cur_send_packet;
	struct qed_ll2_tx_packet cur_completing_packet;
};

struct qed_ll2_info {
	/* Lock protecting the state of LL2 */
	struct mutex mutex;

	struct qed_ll2_acquire_data_inputs input;
	u32 cid;
	u8 my_id;
	u8 queue_id;
	u8 tx_stats_id;
	bool b_active;
	enum core_tx_dest tx_dest;
	u8 tx_stats_en;
	bool main_func_queue;
	struct qed_ll2_rx_queue rx_queue;
	struct qed_ll2_tx_queue tx_queue;
	struct qed_ll2_cbs cbs;
};

extern const struct qed_ll2_ops qed_ll2_ops_pass;

/**
 * qed_ll2_acquire_connection(): Allocate resources,
 *                               starts rx & tx (if relevant) queues pair.
 *                               Provides connecion handler as output
 *                               parameter.
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @data: Describes connection parameters.
 *
 * Return: Int.
 */
int qed_ll2_acquire_connection(void *cxt, struct qed_ll2_acquire_data *data);

/**
 * qed_ll2_establish_connection(): start previously allocated LL2 queues pair
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                     qed_ll2_require_connection.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_establish_connection(void *cxt, u8 connection_handle);

/**
 * qed_ll2_post_rx_buffer(): Submit buffers to LL2 Rx queue.
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                     qed_ll2_require_connection.
 * @addr: RX (physical address) buffers to submit.
 * @buf_len: Buffer Len.
 * @cookie: Cookie.
 * @notify_fw: Produce corresponding Rx BD immediately.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_post_rx_buffer(void *cxt,
			   u8 connection_handle,
			   dma_addr_t addr,
			   u16 buf_len, void *cookie, u8 notify_fw);

/**
 * qed_ll2_prepare_tx_packet(): Request for start Tx BD
 *				to prepare Tx packet submission to FW.
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: Connection handle.
 * @pkt: Info regarding the tx packet.
 * @notify_fw: Issue doorbell to fw for this packet.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_prepare_tx_packet(void *cxt,
			      u8 connection_handle,
			      struct qed_ll2_tx_pkt_info *pkt,
			      bool notify_fw);

/**
 * qed_ll2_release_connection(): Releases resources allocated for LL2
 *                               connection.
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                     qed_ll2_require_connection.
 *
 * Return: Void.
 */
void qed_ll2_release_connection(void *cxt, u8 connection_handle);

/**
 * qed_ll2_set_fragment_of_tx_packet(): Provides fragments to fill
 *                                      Tx BD of BDs requested by
 *                                      qed_ll2_prepare_tx_packet
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                     qed_ll2_require_connection.
 * @addr: Address.
 * @nbytes: Number of bytes.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_set_fragment_of_tx_packet(void *cxt,
				      u8 connection_handle,
				      dma_addr_t addr, u16 nbytes);

/**
 * qed_ll2_terminate_connection(): Stops Tx/Rx queues
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                    qed_ll2_require_connection.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_terminate_connection(void *cxt, u8 connection_handle);

/**
 * qed_ll2_get_stats(): Get LL2 queue's statistics
 *
 * @cxt: Pointer to the hw-function [opaque to some].
 * @connection_handle: LL2 connection's handle obtained from
 *                    qed_ll2_require_connection.
 * @p_stats: Pointer Status.
 *
 * Return: 0 on success, failure otherwise.
 */
int qed_ll2_get_stats(void *cxt,
		      u8 connection_handle, struct qed_ll2_stats *p_stats);

/**
 * qed_ll2_alloc(): Allocates LL2 connections set.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Int.
 */
int qed_ll2_alloc(struct qed_hwfn *p_hwfn);

/**
 * qed_ll2_setup(): Inits LL2 connections set.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 *
 */
void qed_ll2_setup(struct qed_hwfn *p_hwfn);

/**
 * qed_ll2_free(): Releases LL2 connections set
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 *
 */
void qed_ll2_free(struct qed_hwfn *p_hwfn);

#endif
