/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <net/mac80211.h>

#include "iwl-agn.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-trans-int-pcie.h"

/**
 * iwl_trans_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
void iwl_trans_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					   struct iwl_tx_queue *txq,
					   u16 byte_cnt)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int write_ptr = txq->q.write_ptr;
	int txq_id = txq->q.id;
	u8 sec_ctl = 0;
	u8 sta_id = 0;
	u16 len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;

	WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX);

	sta_id = txq->cmd[txq->q.write_ptr]->cmd.tx.sta_id;
	sec_ctl = txq->cmd[txq->q.write_ptr]->cmd.tx.sec_ctl;

	switch (sec_ctl & TX_CMD_SEC_MSK) {
	case TX_CMD_SEC_CCM:
		len += CCMP_MIC_LEN;
		break;
	case TX_CMD_SEC_TKIP:
		len += TKIP_ICV_LEN;
		break;
	case TX_CMD_SEC_WEP:
		len += WEP_IV_LEN + WEP_ICV_LEN;
		break;
	}

	bc_ent = cpu_to_le16((len & 0xFFF) | (sta_id << 12));

	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] = bc_ent;
}

/**
 * iwl_txq_update_write_ptr - Send new write index to hardware
 */
void iwl_txq_update_write_ptr(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	u32 reg = 0;
	int txq_id = txq->q.id;

	if (txq->need_update == 0)
		return;

	if (priv->cfg->base_params->shadow_reg_enable) {
		/* shadow register enabled */
		iwl_write32(priv, HBUS_TARG_WRPTR,
			    txq->q.write_ptr | (txq_id << 8));
	} else {
		/* if we're trying to save power */
		if (test_bit(STATUS_POWER_PMI, &priv->status)) {
			/* wake up nic if it's powered down ...
			 * uCode will wake up, and interrupt us again, so next
			 * time we'll skip this part. */
			reg = iwl_read32(priv, CSR_UCODE_DRV_GP1);

			if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
				IWL_DEBUG_INFO(priv,
					"Tx queue %d requesting wakeup,"
					" GP1 = 0x%x\n", txq_id, reg);
				iwl_set_bit(priv, CSR_GP_CNTRL,
					CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
				return;
			}

			iwl_write_direct32(priv, HBUS_TARG_WRPTR,
				     txq->q.write_ptr | (txq_id << 8));

		/*
		 * else not in power-save mode,
		 * uCode will never sleep when we're
		 * trying to tx (during RFKILL, we're not trying to tx).
		 */
		} else
			iwl_write32(priv, HBUS_TARG_WRPTR,
				    txq->q.write_ptr | (txq_id << 8));
	}
	txq->need_update = 0;
}

static inline dma_addr_t iwl_tfd_tb_get_addr(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];

	dma_addr_t addr = get_unaligned_le32(&tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		addr |=
		((dma_addr_t)(le16_to_cpu(tb->hi_n_len) & 0xF) << 16) << 16;

	return addr;
}

static inline u16 iwl_tfd_tb_get_len(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

static inline void iwl_tfd_set_tb(struct iwl_tfd *tfd, u8 idx,
				  dma_addr_t addr, u16 len)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];
	u16 hi_n_len = len << 4;

	put_unaligned_le32(addr, &tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		hi_n_len |= ((addr >> 16) >> 16) & 0xF;

	tb->hi_n_len = cpu_to_le16(hi_n_len);

	tfd->num_tbs = idx + 1;
}

static inline u8 iwl_tfd_get_num_tbs(struct iwl_tfd *tfd)
{
	return tfd->num_tbs & 0x1f;
}

static void iwlagn_unmap_tfd(struct iwl_priv *priv, struct iwl_cmd_meta *meta,
		     struct iwl_tfd *tfd, enum dma_data_direction dma_dir)
{
	int i;
	int num_tbs;

	/* Sanity check on number of chunks */
	num_tbs = iwl_tfd_get_num_tbs(tfd);

	if (num_tbs >= IWL_NUM_OF_TBS) {
		IWL_ERR(priv, "Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* Unmap tx_cmd */
	if (num_tbs)
		dma_unmap_single(priv->bus->dev,
				dma_unmap_addr(meta, mapping),
				dma_unmap_len(meta, len),
				DMA_BIDIRECTIONAL);

	/* Unmap chunks, if any. */
	for (i = 1; i < num_tbs; i++)
		dma_unmap_single(priv->bus->dev, iwl_tfd_tb_get_addr(tfd, i),
				iwl_tfd_tb_get_len(tfd, i), dma_dir);
}

/**
 * iwlagn_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @priv - driver private data
 * @txq - tx queue
 * @index - the index of the TFD to be freed
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void iwlagn_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq,
	int index)
{
	struct iwl_tfd *tfd_tmp = txq->tfds;

	iwlagn_unmap_tfd(priv, &txq->meta[index], &tfd_tmp[index],
			 DMA_TO_DEVICE);

	/* free SKB */
	if (txq->txb) {
		struct sk_buff *skb;

		skb = txq->txb[index].skb;

		/* can be called from irqs-disabled context */
		if (skb) {
			dev_kfree_skb_any(skb);
			txq->txb[index].skb = NULL;
		}
	}
}

int iwlagn_txq_attach_buf_to_tfd(struct iwl_priv *priv,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len,
				 u8 reset)
{
	struct iwl_queue *q;
	struct iwl_tfd *tfd, *tfd_tmp;
	u32 num_tbs;

	q = &txq->q;
	tfd_tmp = txq->tfds;
	tfd = &tfd_tmp[q->write_ptr];

	if (reset)
		memset(tfd, 0, sizeof(*tfd));

	num_tbs = iwl_tfd_get_num_tbs(tfd);

	/* Each TFD can point to a maximum 20 Tx buffers */
	if (num_tbs >= IWL_NUM_OF_TBS) {
		IWL_ERR(priv, "Error can not send more than %d chunks\n",
			  IWL_NUM_OF_TBS);
		return -EINVAL;
	}

	if (WARN_ON(addr & ~DMA_BIT_MASK(36)))
		return -EINVAL;

	if (unlikely(addr & ~IWL_TX_DMA_MASK))
		IWL_ERR(priv, "Unaligned address = %llx\n",
			  (unsigned long long)addr);

	iwl_tfd_set_tb(tfd, num_tbs, addr, len);

	return 0;
}

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 ***************************************************/

int iwl_queue_space(const struct iwl_queue *q)
{
	int s = q->read_ptr - q->write_ptr;

	if (q->read_ptr > q->write_ptr)
		s -= q->n_bd;

	if (s <= 0)
		s += q->n_window;
	/* keep some reserve to not confuse empty and full situations */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}

/**
 * iwl_queue_init - Initialize queue's high/low-water and read/write indexes
 */
int iwl_queue_init(struct iwl_priv *priv, struct iwl_queue *q,
			  int count, int slots_num, u32 id)
{
	q->n_bd = count;
	q->n_window = slots_num;
	q->id = id;

	/* count must be power-of-two size, otherwise iwl_queue_inc_wrap
	 * and iwl_queue_dec_wrap are broken. */
	if (WARN_ON(!is_power_of_2(count)))
		return -EINVAL;

	/* slots_num must be power-of-two size, otherwise
	 * get_cmd_index is broken. */
	if (WARN_ON(!is_power_of_2(slots_num)))
		return -EINVAL;

	q->low_mark = q->n_window / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_window / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = q->read_ptr = 0;

	return 0;
}

/*TODO: this functions should NOT be exported from trans module - export it
 * until the reclaim flow will be brought to the transport module too.
 * Add a declaration to make sparse happy */
void iwlagn_txq_inval_byte_cnt_tbl(struct iwl_priv *priv,
					  struct iwl_tx_queue *txq);

void iwlagn_txq_inval_byte_cnt_tbl(struct iwl_priv *priv,
					  struct iwl_tx_queue *txq)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int txq_id = txq->q.id;
	int read_ptr = txq->q.read_ptr;
	u8 sta_id = 0;
	__le16 bc_ent;

	WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != priv->cmd_queue)
		sta_id = txq->cmd[read_ptr]->cmd.tx.sta_id;

	bc_ent = cpu_to_le16(1 | (sta_id << 12));
	scd_bc_tbl[txq_id].tfd_offset[read_ptr] = bc_ent;

	if (read_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + read_ptr] = bc_ent;
}

static int iwlagn_tx_queue_set_q2ratid(struct iwl_priv *priv, u16 ra_tid,
					u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = priv->scd_base_addr +
			SCD_TRANS_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = iwl_read_targ_mem(priv, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	iwl_write_targ_mem(priv, tbl_dw_addr, tbl_dw);

	return 0;
}

static void iwlagn_tx_queue_stop_scheduler(struct iwl_priv *priv, u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	iwl_write_prph(priv,
		SCD_QUEUE_STATUS_BITS(txq_id),
		(0 << SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		(1 << SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

void iwl_trans_set_wr_ptrs(struct iwl_priv *priv,
				int txq_id, u32 index)
{
	iwl_write_direct32(priv, HBUS_TARG_WRPTR,
			(index & 0xff) | (txq_id << 8));
	iwl_write_prph(priv, SCD_QUEUE_RDPTR(txq_id), index);
}

void iwl_trans_tx_queue_set_status(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					int tx_fifo_id, int scd_retry)
{
	int txq_id = txq->q.id;
	int active = test_bit(txq_id, &priv->txq_ctx_active_msk) ? 1 : 0;

	iwl_write_prph(priv, SCD_QUEUE_STATUS_BITS(txq_id),
			(active << SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			(tx_fifo_id << SCD_QUEUE_STTS_REG_POS_TXF) |
			(1 << SCD_QUEUE_STTS_REG_POS_WSL) |
			SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	IWL_DEBUG_INFO(priv, "%s %s Queue %d on FIFO %d\n",
		       active ? "Activate" : "Deactivate",
		       scd_retry ? "BA" : "AC/CMD", txq_id, tx_fifo_id);
}

void iwl_trans_txq_agg_setup(struct iwl_priv *priv, int sta_id, int tid,
						int frame_limit)
{
	int tx_fifo, txq_id, ssn_idx;
	u16 ra_tid;
	unsigned long flags;
	struct iwl_tid_data *tid_data;

	if (WARN_ON(sta_id == IWL_INVALID_STATION))
		return;
	if (WARN_ON(tid >= MAX_TID_COUNT))
		return;

	spin_lock_irqsave(&priv->sta_lock, flags);
	tid_data = &priv->stations[sta_id].tid[tid];
	ssn_idx = SEQ_TO_SN(tid_data->seq_number);
	txq_id = tid_data->agg.txq_id;
	tx_fifo = tid_data->agg.tx_fifo;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	ra_tid = BUILD_RAxTID(sta_id, tid);

	spin_lock_irqsave(&priv->lock, flags);

	/* Stop this Tx queue before configuring it */
	iwlagn_tx_queue_stop_scheduler(priv, txq_id);

	/* Map receiver-address / traffic-ID to this queue */
	iwlagn_tx_queue_set_q2ratid(priv, ra_tid, txq_id);

	/* Set this queue as a chain-building queue */
	iwl_set_bits_prph(priv, SCD_QUEUECHAIN_SEL, (1<<txq_id));

	/* enable aggregations for the queue */
	iwl_set_bits_prph(priv, SCD_AGGR_SEL, (1<<txq_id));

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	iwl_trans_set_wr_ptrs(priv, txq_id, ssn_idx);

	/* Set up Tx window size and frame limit for this queue */
	iwl_write_targ_mem(priv, priv->scd_base_addr +
			SCD_CONTEXT_QUEUE_OFFSET(txq_id) +
			sizeof(u32),
			((frame_limit <<
			SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
			SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
			((frame_limit <<
			SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
			SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

	iwl_set_bits_prph(priv, SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	iwl_trans_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&priv->lock, flags);
}

int iwl_trans_txq_agg_disable(struct iwl_priv *priv, u16 txq_id,
				  u16 ssn_idx, u8 tx_fifo)
{
	if ((IWLAGN_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWLAGN_FIRST_AMPDU_QUEUE +
		priv->cfg->base_params->num_of_ampdu_queues <= txq_id)) {
		IWL_ERR(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWLAGN_FIRST_AMPDU_QUEUE,
			IWLAGN_FIRST_AMPDU_QUEUE +
			priv->cfg->base_params->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	iwlagn_tx_queue_stop_scheduler(priv, txq_id);

	iwl_clear_bits_prph(priv, SCD_AGGR_SEL, (1 << txq_id));

	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	iwl_trans_set_wr_ptrs(priv, txq_id, ssn_idx);

	iwl_clear_bits_prph(priv, SCD_INTERRUPT_MASK, (1 << txq_id));
	iwl_txq_ctx_deactivate(priv, txq_id);
	iwl_trans_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 0);

	return 0;
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/**
 * iwl_enqueue_hcmd - enqueue a uCode command
 * @priv: device private data point
 * @cmd: a point to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation is
 * failed. On success, it turns the index (> 0) of command in the
 * command queue.
 */
static int iwl_enqueue_hcmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	struct iwl_tx_queue *txq = &priv->txq[priv->cmd_queue];
	struct iwl_queue *q = &txq->q;
	struct iwl_device_cmd *out_cmd;
	struct iwl_cmd_meta *out_meta;
	dma_addr_t phys_addr;
	unsigned long flags;
	u32 idx;
	u16 copy_size, cmd_size;
	bool is_ct_kill = false;
	bool had_nocopy = false;
	int i;
	u8 *cmd_dest;
#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
	const void *trace_bufs[IWL_MAX_CMD_TFDS + 1] = {};
	int trace_lens[IWL_MAX_CMD_TFDS + 1] = {};
	int trace_idx;
#endif

	if (test_bit(STATUS_FW_ERROR, &priv->status)) {
		IWL_WARN(priv, "fw recovery, no hcmd send\n");
		return -EIO;
	}

	if ((priv->ucode_owner == IWL_OWNERSHIP_TM) &&
	    !(cmd->flags & CMD_ON_DEMAND)) {
		IWL_DEBUG_HC(priv, "tm own the uCode, no regular hcmd send\n");
		return -EIO;
	}

	copy_size = sizeof(out_cmd->hdr);
	cmd_size = sizeof(out_cmd->hdr);

	/* need one for the header if the first is NOCOPY */
	BUILD_BUG_ON(IWL_MAX_CMD_TFDS > IWL_NUM_OF_TBS - 1);

	for (i = 0; i < IWL_MAX_CMD_TFDS; i++) {
		if (!cmd->len[i])
			continue;
		if (cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY) {
			had_nocopy = true;
		} else {
			/* NOCOPY must not be followed by normal! */
			if (WARN_ON(had_nocopy))
				return -EINVAL;
			copy_size += cmd->len[i];
		}
		cmd_size += cmd->len[i];
	}

	/*
	 * If any of the command structures end up being larger than
	 * the TFD_MAX_PAYLOAD_SIZE and they aren't dynamically
	 * allocated into separate TFDs, then we will need to
	 * increase the size of the buffers.
	 */
	if (WARN_ON(copy_size > TFD_MAX_PAYLOAD_SIZE))
		return -EINVAL;

	if (iwl_is_rfkill(priv) || iwl_is_ctkill(priv)) {
		IWL_WARN(priv, "Not sending command - %s KILL\n",
			 iwl_is_rfkill(priv) ? "RF" : "CT");
		return -EIO;
	}

	spin_lock_irqsave(&priv->hcmd_lock, flags);

	if (iwl_queue_space(q) < ((cmd->flags & CMD_ASYNC) ? 2 : 1)) {
		spin_unlock_irqrestore(&priv->hcmd_lock, flags);

		IWL_ERR(priv, "No space in command queue\n");
		is_ct_kill = iwl_check_for_ct_kill(priv);
		if (!is_ct_kill) {
			IWL_ERR(priv, "Restarting adapter due to queue full\n");
			iwlagn_fw_error(priv, false);
		}
		return -ENOSPC;
	}

	idx = get_cmd_index(q, q->write_ptr);
	out_cmd = txq->cmd[idx];
	out_meta = &txq->meta[idx];

	memset(out_meta, 0, sizeof(*out_meta));	/* re-initialize to NULL */
	if (cmd->flags & CMD_WANT_SKB)
		out_meta->source = cmd;
	if (cmd->flags & CMD_ASYNC)
		out_meta->callback = cmd->callback;

	/* set up the header */

	out_cmd->hdr.cmd = cmd->id;
	out_cmd->hdr.flags = 0;
	out_cmd->hdr.sequence = cpu_to_le16(QUEUE_TO_SEQ(priv->cmd_queue) |
					    INDEX_TO_SEQ(q->write_ptr));

	/* and copy the data that needs to be copied */

	cmd_dest = &out_cmd->cmd.payload[0];
	for (i = 0; i < IWL_MAX_CMD_TFDS; i++) {
		if (!cmd->len[i])
			continue;
		if (cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY)
			break;
		memcpy(cmd_dest, cmd->data[i], cmd->len[i]);
		cmd_dest += cmd->len[i];
	}

	IWL_DEBUG_HC(priv, "Sending command %s (#%x), seq: 0x%04X, "
			"%d bytes at %d[%d]:%d\n",
			get_cmd_string(out_cmd->hdr.cmd),
			out_cmd->hdr.cmd,
			le16_to_cpu(out_cmd->hdr.sequence), cmd_size,
			q->write_ptr, idx, priv->cmd_queue);

	phys_addr = dma_map_single(priv->bus->dev, &out_cmd->hdr, copy_size,
				DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(priv->bus->dev, phys_addr))) {
		idx = -ENOMEM;
		goto out;
	}

	dma_unmap_addr_set(out_meta, mapping, phys_addr);
	dma_unmap_len_set(out_meta, len, copy_size);

	iwlagn_txq_attach_buf_to_tfd(priv, txq, phys_addr, copy_size, 1);
#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
	trace_bufs[0] = &out_cmd->hdr;
	trace_lens[0] = copy_size;
	trace_idx = 1;
#endif

	for (i = 0; i < IWL_MAX_CMD_TFDS; i++) {
		if (!cmd->len[i])
			continue;
		if (!(cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY))
			continue;
		phys_addr = dma_map_single(priv->bus->dev, (void *)cmd->data[i],
					   cmd->len[i], DMA_BIDIRECTIONAL);
		if (dma_mapping_error(priv->bus->dev, phys_addr)) {
			iwlagn_unmap_tfd(priv, out_meta,
					 &txq->tfds[q->write_ptr],
					 DMA_BIDIRECTIONAL);
			idx = -ENOMEM;
			goto out;
		}

		iwlagn_txq_attach_buf_to_tfd(priv, txq, phys_addr,
					     cmd->len[i], 0);
#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
		trace_bufs[trace_idx] = cmd->data[i];
		trace_lens[trace_idx] = cmd->len[i];
		trace_idx++;
#endif
	}

	out_meta->flags = cmd->flags;

	txq->need_update = 1;

	/* check that tracing gets all possible blocks */
	BUILD_BUG_ON(IWL_MAX_CMD_TFDS + 1 != 3);
#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
	trace_iwlwifi_dev_hcmd(priv, cmd->flags,
			       trace_bufs[0], trace_lens[0],
			       trace_bufs[1], trace_lens[1],
			       trace_bufs[2], trace_lens[2]);
#endif

	/* Increment and update queue's write index */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	iwl_txq_update_write_ptr(priv, txq);

 out:
	spin_unlock_irqrestore(&priv->hcmd_lock, flags);
	return idx;
}

/**
 * iwl_hcmd_queue_reclaim - Reclaim TX command queue entries already Tx'd
 *
 * When FW advances 'R' index, all entries between old and new 'R' index
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
static void iwl_hcmd_queue_reclaim(struct iwl_priv *priv, int txq_id, int idx)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	int nfreed = 0;

	if ((idx >= q->n_bd) || (iwl_queue_used(q, idx) == 0)) {
		IWL_ERR(priv, "%s: Read index for DMA queue txq id (%d), "
			  "index %d is out of range [0-%d] %d %d.\n", __func__,
			  txq_id, idx, q->n_bd, q->write_ptr, q->read_ptr);
		return;
	}

	for (idx = iwl_queue_inc_wrap(idx, q->n_bd); q->read_ptr != idx;
	     q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		if (nfreed++ > 0) {
			IWL_ERR(priv, "HCMD skipped: index (%d) %d %d\n", idx,
					q->write_ptr, q->read_ptr);
			iwlagn_fw_error(priv, false);
		}

	}
}

/**
 * iwl_tx_cmd_complete - Pull unused buffers off the queue and reclaim them
 * @rxb: Rx buffer to reclaim
 *
 * If an Rx buffer has an async callback associated with it the callback
 * will be executed.  The attached skb (if present) will only be freed
 * if the callback returns 1
 */
void iwl_tx_cmd_complete(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	int cmd_index;
	struct iwl_device_cmd *cmd;
	struct iwl_cmd_meta *meta;
	struct iwl_tx_queue *txq = &priv->txq[priv->cmd_queue];
	unsigned long flags;

	/* If a Tx command is being handled and it isn't in the actual
	 * command queue then there a command routing bug has been introduced
	 * in the queue management code. */
	if (WARN(txq_id != priv->cmd_queue,
		 "wrong command queue %d (should be %d), sequence 0x%X readp=%d writep=%d\n",
		  txq_id, priv->cmd_queue, sequence,
		  priv->txq[priv->cmd_queue].q.read_ptr,
		  priv->txq[priv->cmd_queue].q.write_ptr)) {
		iwl_print_hex_error(priv, pkt, 32);
		return;
	}

	cmd_index = get_cmd_index(&txq->q, index);
	cmd = txq->cmd[cmd_index];
	meta = &txq->meta[cmd_index];

	iwlagn_unmap_tfd(priv, meta, &txq->tfds[index], DMA_BIDIRECTIONAL);

	/* Input error checking is done when commands are added to queue. */
	if (meta->flags & CMD_WANT_SKB) {
		meta->source->reply_page = (unsigned long)rxb_addr(rxb);
		rxb->page = NULL;
	} else if (meta->callback)
		meta->callback(priv, cmd, pkt);

	spin_lock_irqsave(&priv->hcmd_lock, flags);

	iwl_hcmd_queue_reclaim(priv, txq_id, index);

	if (!(meta->flags & CMD_ASYNC)) {
		clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
		IWL_DEBUG_INFO(priv, "Clearing HCMD_ACTIVE for command %s\n",
			       get_cmd_string(cmd->hdr.cmd));
		wake_up_interruptible(&priv->wait_command_queue);
	}

	meta->flags = 0;

	spin_unlock_irqrestore(&priv->hcmd_lock, flags);
}

const char *get_cmd_string(u8 cmd)
{
	switch (cmd) {
		IWL_CMD(REPLY_ALIVE);
		IWL_CMD(REPLY_ERROR);
		IWL_CMD(REPLY_RXON);
		IWL_CMD(REPLY_RXON_ASSOC);
		IWL_CMD(REPLY_QOS_PARAM);
		IWL_CMD(REPLY_RXON_TIMING);
		IWL_CMD(REPLY_ADD_STA);
		IWL_CMD(REPLY_REMOVE_STA);
		IWL_CMD(REPLY_REMOVE_ALL_STA);
		IWL_CMD(REPLY_TXFIFO_FLUSH);
		IWL_CMD(REPLY_WEPKEY);
		IWL_CMD(REPLY_TX);
		IWL_CMD(REPLY_LEDS_CMD);
		IWL_CMD(REPLY_TX_LINK_QUALITY_CMD);
		IWL_CMD(COEX_PRIORITY_TABLE_CMD);
		IWL_CMD(COEX_MEDIUM_NOTIFICATION);
		IWL_CMD(COEX_EVENT_CMD);
		IWL_CMD(REPLY_QUIET_CMD);
		IWL_CMD(REPLY_CHANNEL_SWITCH);
		IWL_CMD(CHANNEL_SWITCH_NOTIFICATION);
		IWL_CMD(REPLY_SPECTRUM_MEASUREMENT_CMD);
		IWL_CMD(SPECTRUM_MEASURE_NOTIFICATION);
		IWL_CMD(POWER_TABLE_CMD);
		IWL_CMD(PM_SLEEP_NOTIFICATION);
		IWL_CMD(PM_DEBUG_STATISTIC_NOTIFIC);
		IWL_CMD(REPLY_SCAN_CMD);
		IWL_CMD(REPLY_SCAN_ABORT_CMD);
		IWL_CMD(SCAN_START_NOTIFICATION);
		IWL_CMD(SCAN_RESULTS_NOTIFICATION);
		IWL_CMD(SCAN_COMPLETE_NOTIFICATION);
		IWL_CMD(BEACON_NOTIFICATION);
		IWL_CMD(REPLY_TX_BEACON);
		IWL_CMD(WHO_IS_AWAKE_NOTIFICATION);
		IWL_CMD(QUIET_NOTIFICATION);
		IWL_CMD(REPLY_TX_PWR_TABLE_CMD);
		IWL_CMD(MEASURE_ABORT_NOTIFICATION);
		IWL_CMD(REPLY_BT_CONFIG);
		IWL_CMD(REPLY_STATISTICS_CMD);
		IWL_CMD(STATISTICS_NOTIFICATION);
		IWL_CMD(REPLY_CARD_STATE_CMD);
		IWL_CMD(CARD_STATE_NOTIFICATION);
		IWL_CMD(MISSED_BEACONS_NOTIFICATION);
		IWL_CMD(REPLY_CT_KILL_CONFIG_CMD);
		IWL_CMD(SENSITIVITY_CMD);
		IWL_CMD(REPLY_PHY_CALIBRATION_CMD);
		IWL_CMD(REPLY_RX_PHY_CMD);
		IWL_CMD(REPLY_RX_MPDU_CMD);
		IWL_CMD(REPLY_RX);
		IWL_CMD(REPLY_COMPRESSED_BA);
		IWL_CMD(CALIBRATION_CFG_CMD);
		IWL_CMD(CALIBRATION_RES_NOTIFICATION);
		IWL_CMD(CALIBRATION_COMPLETE_NOTIFICATION);
		IWL_CMD(REPLY_TX_POWER_DBM_CMD);
		IWL_CMD(TEMPERATURE_NOTIFICATION);
		IWL_CMD(TX_ANT_CONFIGURATION_CMD);
		IWL_CMD(REPLY_BT_COEX_PROFILE_NOTIF);
		IWL_CMD(REPLY_BT_COEX_PRIO_TABLE);
		IWL_CMD(REPLY_BT_COEX_PROT_ENV);
		IWL_CMD(REPLY_WIPAN_PARAMS);
		IWL_CMD(REPLY_WIPAN_RXON);
		IWL_CMD(REPLY_WIPAN_RXON_TIMING);
		IWL_CMD(REPLY_WIPAN_RXON_ASSOC);
		IWL_CMD(REPLY_WIPAN_QOS_PARAM);
		IWL_CMD(REPLY_WIPAN_WEPKEY);
		IWL_CMD(REPLY_WIPAN_P2P_CHANNEL_SWITCH);
		IWL_CMD(REPLY_WIPAN_NOA_NOTIFICATION);
		IWL_CMD(REPLY_WIPAN_DEACTIVATION_COMPLETE);
		IWL_CMD(REPLY_WOWLAN_PATTERNS);
		IWL_CMD(REPLY_WOWLAN_WAKEUP_FILTER);
		IWL_CMD(REPLY_WOWLAN_TSC_RSC_PARAMS);
		IWL_CMD(REPLY_WOWLAN_TKIP_PARAMS);
		IWL_CMD(REPLY_WOWLAN_KEK_KCK_MATERIAL);
		IWL_CMD(REPLY_WOWLAN_GET_STATUS);
	default:
		return "UNKNOWN";

	}
}

#define HOST_COMPLETE_TIMEOUT (2 * HZ)

static void iwl_generic_cmd_callback(struct iwl_priv *priv,
				     struct iwl_device_cmd *cmd,
				     struct iwl_rx_packet *pkt)
{
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from %s (0x%08X)\n",
			get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	switch (cmd->hdr.cmd) {
	case REPLY_TX_LINK_QUALITY_CMD:
	case SENSITIVITY_CMD:
		IWL_DEBUG_HC_DUMP(priv, "back from %s (0x%08X)\n",
				get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		break;
	default:
		IWL_DEBUG_HC(priv, "back from %s (0x%08X)\n",
				get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
	}
#endif
}

static int iwl_send_cmd_async(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int ret;

	/* An asynchronous command can not expect an SKB to be set. */
	if (WARN_ON(cmd->flags & CMD_WANT_SKB))
		return -EINVAL;

	/* Assign a generic callback if one is not provided */
	if (!cmd->callback)
		cmd->callback = iwl_generic_cmd_callback;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EBUSY;

	ret = iwl_enqueue_hcmd(priv, cmd);
	if (ret < 0) {
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  get_cmd_string(cmd->id), ret);
		return ret;
	}
	return 0;
}

static int iwl_send_cmd_sync(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int cmd_idx;
	int ret;

	lockdep_assert_held(&priv->mutex);

	 /* A synchronous command can not have a callback set. */
	if (WARN_ON(cmd->callback))
		return -EINVAL;

	IWL_DEBUG_INFO(priv, "Attempting to send sync command %s\n",
			get_cmd_string(cmd->id));

	set_bit(STATUS_HCMD_ACTIVE, &priv->status);
	IWL_DEBUG_INFO(priv, "Setting HCMD_ACTIVE for command %s\n",
			get_cmd_string(cmd->id));

	cmd_idx = iwl_enqueue_hcmd(priv, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  get_cmd_string(cmd->id), ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
			!test_bit(STATUS_HCMD_ACTIVE, &priv->status),
			HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		if (test_bit(STATUS_HCMD_ACTIVE, &priv->status)) {
			IWL_ERR(priv,
				"Error sending %s: time out after %dms.\n",
				get_cmd_string(cmd->id),
				jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

			clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
			IWL_DEBUG_INFO(priv, "Clearing HCMD_ACTIVE for command"
				 "%s\n", get_cmd_string(cmd->id));
			ret = -ETIMEDOUT;
			goto cancel;
		}
	}

	if (test_bit(STATUS_RF_KILL_HW, &priv->status)) {
		IWL_ERR(priv, "Command %s aborted: RF KILL Switch\n",
			       get_cmd_string(cmd->id));
		ret = -ECANCELED;
		goto fail;
	}
	if (test_bit(STATUS_FW_ERROR, &priv->status)) {
		IWL_ERR(priv, "Command %s failed: FW Error\n",
			       get_cmd_string(cmd->id));
		ret = -EIO;
		goto fail;
	}
	if ((cmd->flags & CMD_WANT_SKB) && !cmd->reply_page) {
		IWL_ERR(priv, "Error: Response NULL in '%s'\n",
			  get_cmd_string(cmd->id));
		ret = -EIO;
		goto cancel;
	}

	return 0;

cancel:
	if (cmd->flags & CMD_WANT_SKB) {
		/*
		 * Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source).
		 */
		priv->txq[priv->cmd_queue].meta[cmd_idx].flags &=
							~CMD_WANT_SKB;
	}
fail:
	if (cmd->reply_page) {
		iwl_free_pages(priv, cmd->reply_page);
		cmd->reply_page = 0;
	}

	return ret;
}

int iwl_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	if (cmd->flags & CMD_ASYNC)
		return iwl_send_cmd_async(priv, cmd);

	return iwl_send_cmd_sync(priv, cmd);
}

int iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id, u32 flags, u16 len,
		     const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_send_cmd(priv, &cmd);
}
