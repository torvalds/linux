/* QLogic qed NIC Driver
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
	} bds_set[1];
};

struct qed_ll2_rx_queue {
	/* Lock protecting the Rx queue manipulation */
	spinlock_t lock;
	struct qed_chain rxq_chain;
	struct qed_chain rcq_chain;
	u8 rx_sb_index;
	bool b_cb_registered;
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
	bool b_cb_registered;
	__le16 *p_fw_cons;
	struct list_head active_descq;
	struct list_head free_descq;
	struct list_head sending_descq;
	void *descq_mem; /* memory for variable sized qed_ll2_tx_packet*/
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

/**
 * @brief qed_ll2_acquire_connection - allocate resources,
 *        starts rx & tx (if relevant) queues pair. Provides
 *        connecion handler as output parameter.
 *
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param data - describes connection parameters
 * @return int
 */
int qed_ll2_acquire_connection(void *cxt, struct qed_ll2_acquire_data *data);

/**
 * @brief qed_ll2_establish_connection - start previously
 *        allocated LL2 queues pair
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param p_ptt
 * @param connection_handle	LL2 connection's handle obtained from
 *                              qed_ll2_require_connection
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_establish_connection(void *cxt, u8 connection_handle);

/**
 * @brief qed_ll2_post_rx_buffers - submit buffers to LL2 Rx queue.
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle	LL2 connection's handle obtained from
 *				qed_ll2_require_connection
 * @param addr			rx (physical address) buffers to submit
 * @param cookie
 * @param notify_fw		produce corresponding Rx BD immediately
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_post_rx_buffer(void *cxt,
			   u8 connection_handle,
			   dma_addr_t addr,
			   u16 buf_len, void *cookie, u8 notify_fw);

/**
 * @brief qed_ll2_prepare_tx_packet - request for start Tx BD
 *				      to prepare Tx packet submission to FW.
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle
 * @param pkt - info regarding the tx packet
 * @param notify_fw - issue doorbell to fw for this packet
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_prepare_tx_packet(void *cxt,
			      u8 connection_handle,
			      struct qed_ll2_tx_pkt_info *pkt,
			      bool notify_fw);

/**
 * @brief qed_ll2_release_connection -	releases resources
 *					allocated for LL2 connection
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle		LL2 connection's handle obtained from
 *					qed_ll2_require_connection
 */
void qed_ll2_release_connection(void *cxt, u8 connection_handle);

/**
 * @brief qed_ll2_set_fragment_of_tx_packet -	provides fragments to fill
 *						Tx BD of BDs requested by
 *						qed_ll2_prepare_tx_packet
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle			LL2 connection's handle
 *						obtained from
 *						qed_ll2_require_connection
 * @param addr
 * @param nbytes
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_set_fragment_of_tx_packet(void *cxt,
				      u8 connection_handle,
				      dma_addr_t addr, u16 nbytes);

/**
 * @brief qed_ll2_terminate_connection -	stops Tx/Rx queues
 *
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle			LL2 connection's handle
 *						obtained from
 *						qed_ll2_require_connection
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_terminate_connection(void *cxt, u8 connection_handle);

/**
 * @brief qed_ll2_get_stats -	get LL2 queue's statistics
 *
 *
 * @param cxt - pointer to the hw-function [opaque to some]
 * @param connection_handle	LL2 connection's handle obtained from
 *				qed_ll2_require_connection
 * @param p_stats
 *
 * @return 0 on success, failure otherwise
 */
int qed_ll2_get_stats(void *cxt,
		      u8 connection_handle, struct qed_ll2_stats *p_stats);

/**
 * @brief qed_ll2_alloc - Allocates LL2 connections set
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_ll2_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ll2_setup - Inits LL2 connections set
 *
 * @param p_hwfn
 *
 */
void qed_ll2_setup(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ll2_free - Releases LL2 connections set
 *
 * @param p_hwfn
 *
 */
void qed_ll2_free(struct qed_hwfn *p_hwfn);

#endif
