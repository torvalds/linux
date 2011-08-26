/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __iwl_trans_h__
#define __iwl_trans_h__

#include <linux/debugfs.h>

 /*This file includes the declaration that are exported from the transport
 * layer */

struct iwl_priv;
struct iwl_rxon_context;
struct iwl_host_cmd;
struct iwl_shared;

/**
 * struct iwl_trans_ops - transport specific operations
 * @alloc: allocates the meta data (not the queues themselves)
 * @request_irq: requests IRQ - will be called before the FW load in probe flow
 * @start_device: allocates and inits all the resources for the transport
 *                layer.
 * @prepare_card_hw: claim the ownership on the HW. Will be called during
 *                   probe.
 * @tx_start: starts and configures all the Tx fifo - usually done once the fw
 *           is alive.
 * @stop_device:stops the whole device (embedded CPU put to reset)
 * @rx_free: frees the rx memory
 * @tx_free: frees the tx memory
 * @send_cmd:send a host command
 * @send_cmd_pdu:send a host command: flags can be CMD_*
 * @get_tx_cmd: returns a pointer to a new Tx cmd for the upper layer use
 * @tx: send an skb
 * @reclaim: free packet until ssn. Returns a list of freed packets.
 * @txq_agg_setup: setup a tx queue for AMPDU - will be called once the HW is
 *                 ready and a successful ADDBA response has been received.
 * @txq_agg_disable: de-configure a Tx queue to send AMPDUs
 * @kick_nic: remove the RESET from the embedded CPU and let it run
 * @disable_sync_irq: Disable and sync: after this handler returns, it is
 *	guaranteed that all the ISR / tasklet etc... have finished running
 *	and the transport layer shall not pass any Rx.
 * @free: release all the ressource for the transport layer itself such as
 *        irq, tasklet etc...
 * @dbgfs_register: add the dbgfs files under this directory. Files will be
 *	automatically deleted.
 * @suspend: stop the device unless WoWLAN is configured
 * @resume: resume activity of the device
 */
struct iwl_trans_ops {

	struct iwl_trans *(*alloc)(struct iwl_shared *shrd);
	int (*request_irq)(struct iwl_trans *iwl_trans);
	int (*start_device)(struct iwl_priv *priv);
	int (*prepare_card_hw)(struct iwl_priv *priv);
	void (*stop_device)(struct iwl_priv *priv);
	void (*tx_start)(struct iwl_priv *priv);
	void (*tx_free)(struct iwl_priv *priv);
	void (*rx_free)(struct iwl_trans *trans);

	int (*send_cmd)(struct iwl_priv *priv, struct iwl_host_cmd *cmd);

	int (*send_cmd_pdu)(struct iwl_priv *priv, u8 id, u32 flags, u16 len,
		     const void *data);
	struct iwl_tx_cmd * (*get_tx_cmd)(struct iwl_priv *priv, int txq_id);
	int (*tx)(struct iwl_priv *priv, struct sk_buff *skb,
		struct iwl_tx_cmd *tx_cmd, int txq_id, __le16 fc, bool ampdu,
		struct iwl_rxon_context *ctx);
	void (*reclaim)(struct iwl_trans *trans, int txq_id, int ssn,
			u32 status, struct sk_buff_head *skbs);

	int (*txq_agg_disable)(struct iwl_priv *priv, u16 txq_id,
				  u16 ssn_idx, u8 tx_fifo);
	void (*txq_agg_setup)(struct iwl_priv *priv, int sta_id, int tid,
						int frame_limit);

	void (*kick_nic)(struct iwl_priv *priv);

	void (*disable_sync_irq)(struct iwl_trans *trans);
	void (*free)(struct iwl_priv *priv);

	int (*dbgfs_register)(struct iwl_trans *trans, struct dentry* dir);
	int (*suspend)(struct iwl_trans *trans);
	int (*resume)(struct iwl_trans *trans);
};

/**
 * struct iwl_trans - transport common data
 * @ops - pointer to iwl_trans_ops
 * @shrd - pointer to iwl_shared which holds shared data from the upper layer
 */
struct iwl_trans {
	const struct iwl_trans_ops *ops;
	struct iwl_shared *shrd;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[0] __attribute__((__aligned__(sizeof(void *))));
};

static inline int iwl_trans_request_irq(struct iwl_trans *trans)
{
	return trans->ops->request_irq(trans);
}

static inline int iwl_trans_start_device(struct iwl_trans *trans)
{
	return trans->ops->start_device(priv(trans));
}

static inline int iwl_trans_prepare_card_hw(struct iwl_trans *trans)
{
	return trans->ops->prepare_card_hw(priv(trans));
}

static inline void iwl_trans_stop_device(struct iwl_trans *trans)
{
	trans->ops->stop_device(priv(trans));
}

static inline void iwl_trans_tx_start(struct iwl_trans *trans)
{
	trans->ops->tx_start(priv(trans));
}

static inline void iwl_trans_rx_free(struct iwl_trans *trans)
{
	trans->ops->rx_free(trans);
}

static inline void iwl_trans_tx_free(struct iwl_trans *trans)
{
	trans->ops->tx_free(priv(trans));
}

static inline int iwl_trans_send_cmd(struct iwl_trans *trans,
				struct iwl_host_cmd *cmd)
{
	return trans->ops->send_cmd(priv(trans), cmd);
}

static inline int iwl_trans_send_cmd_pdu(struct iwl_trans *trans, u8 id,
					u32 flags, u16 len, const void *data)
{
	return trans->ops->send_cmd_pdu(priv(trans), id, flags, len, data);
}

static inline struct iwl_tx_cmd *iwl_trans_get_tx_cmd(struct iwl_trans *trans,
					int txq_id)
{
	return trans->ops->get_tx_cmd(priv(trans), txq_id);
}

static inline int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
		struct iwl_tx_cmd *tx_cmd, int txq_id, __le16 fc, bool ampdu,
		struct iwl_rxon_context *ctx)
{
	return trans->ops->tx(priv(trans), skb, tx_cmd, txq_id, fc, ampdu, ctx);
}

static inline void iwl_trans_reclaim(struct iwl_trans *trans, int txq_id,
				 int ssn, u32 status,
				 struct sk_buff_head *skbs)
{
	trans->ops->reclaim(trans, txq_id, ssn, status, skbs);
}

static inline int iwl_trans_txq_agg_disable(struct iwl_trans *trans, u16 txq_id,
			  u16 ssn_idx, u8 tx_fifo)
{
	return trans->ops->txq_agg_disable(priv(trans), txq_id,
					   ssn_idx, tx_fifo);
}

static inline void iwl_trans_txq_agg_setup(struct iwl_trans *trans, int sta_id,
						int tid, int frame_limit)
{
	trans->ops->txq_agg_setup(priv(trans), sta_id, tid, frame_limit);
}

static inline void iwl_trans_kick_nic(struct iwl_trans *trans)
{
	trans->ops->kick_nic(priv(trans));
}

static inline void iwl_trans_disable_sync_irq(struct iwl_trans *trans)
{
	trans->ops->disable_sync_irq(trans);
}

static inline void iwl_trans_free(struct iwl_trans *trans)
{
	trans->ops->free(priv(trans));
}

static inline int iwl_trans_dbgfs_register(struct iwl_trans *trans,
					    struct dentry *dir)
{
	return trans->ops->dbgfs_register(trans, dir);
}

static inline int iwl_trans_suspend(struct iwl_trans *trans)
{
	return trans->ops->suspend(trans);
}

static inline int iwl_trans_resume(struct iwl_trans *trans)
{
	return trans->ops->resume(trans);
}

/*****************************************************
* Transport layers implementations
******************************************************/
extern const struct iwl_trans_ops trans_ops_pcie;

/*TODO: this functions should NOT be exported from trans module - export it
 * until the reclaim flow will be brought to the transport module too */

struct iwl_tx_queue;
void iwlagn_txq_inval_byte_cnt_tbl(struct iwl_priv *priv,
					  struct iwl_tx_queue *txq);

#endif /* __iwl_trans_h__ */
