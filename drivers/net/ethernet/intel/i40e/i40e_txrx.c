/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40e.h"

static inline __le64 build_ctob(u32 td_cmd, u32 td_offset, unsigned int size,
				u32 td_tag)
{
	return cpu_to_le64(I40E_TX_DESC_DTYPE_DATA |
			   ((u64)td_cmd  << I40E_TXD_QW1_CMD_SHIFT) |
			   ((u64)td_offset << I40E_TXD_QW1_OFFSET_SHIFT) |
			   ((u64)size  << I40E_TXD_QW1_TX_BUF_SZ_SHIFT) |
			   ((u64)td_tag  << I40E_TXD_QW1_L2TAG1_SHIFT));
}

/**
 * i40e_program_fdir_filter - Program a Flow Director filter
 * @fdir_input: Packet data that will be filter parameters
 * @pf: The pf pointer
 * @add: True for add/update, False for remove
 **/
int i40e_program_fdir_filter(struct i40e_fdir_data *fdir_data,
			     struct i40e_pf *pf, bool add)
{
	struct i40e_filter_program_desc *fdir_desc;
	struct i40e_tx_buffer *tx_buf;
	struct i40e_tx_desc *tx_desc;
	struct i40e_ring *tx_ring;
	struct i40e_vsi *vsi;
	struct device *dev;
	dma_addr_t dma;
	u32 td_cmd = 0;
	u16 i;

	/* find existing FDIR VSI */
	vsi = NULL;
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++)
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR)
			vsi = pf->vsi[i];
	if (!vsi)
		return -ENOENT;

	tx_ring = &vsi->tx_rings[0];
	dev = tx_ring->dev;

	dma = dma_map_single(dev, fdir_data->raw_packet,
				I40E_FDIR_MAX_RAW_PACKET_LOOKUP, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma))
		goto dma_fail;

	/* grab the next descriptor */
	fdir_desc = I40E_TX_FDIRDESC(tx_ring, tx_ring->next_to_use);
	tx_buf = &tx_ring->tx_bi[tx_ring->next_to_use];
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	fdir_desc->qindex_flex_ptype_vsi = cpu_to_le32((fdir_data->q_index
					     << I40E_TXD_FLTR_QW0_QINDEX_SHIFT)
					     & I40E_TXD_FLTR_QW0_QINDEX_MASK);

	fdir_desc->qindex_flex_ptype_vsi |= cpu_to_le32((fdir_data->flex_off
					    << I40E_TXD_FLTR_QW0_FLEXOFF_SHIFT)
					    & I40E_TXD_FLTR_QW0_FLEXOFF_MASK);

	fdir_desc->qindex_flex_ptype_vsi |= cpu_to_le32((fdir_data->pctype
					     << I40E_TXD_FLTR_QW0_PCTYPE_SHIFT)
					     & I40E_TXD_FLTR_QW0_PCTYPE_MASK);

	/* Use LAN VSI Id if not programmed by user */
	if (fdir_data->dest_vsi == 0)
		fdir_desc->qindex_flex_ptype_vsi |=
					  cpu_to_le32((pf->vsi[pf->lan_vsi]->id)
					   << I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT);
	else
		fdir_desc->qindex_flex_ptype_vsi |=
					    cpu_to_le32((fdir_data->dest_vsi
					    << I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT)
					    & I40E_TXD_FLTR_QW0_DEST_VSI_MASK);

	fdir_desc->dtype_cmd_cntindex =
				    cpu_to_le32(I40E_TX_DESC_DTYPE_FILTER_PROG);

	if (add)
		fdir_desc->dtype_cmd_cntindex |= cpu_to_le32(
				       I40E_FILTER_PROGRAM_DESC_PCMD_ADD_UPDATE
					<< I40E_TXD_FLTR_QW1_PCMD_SHIFT);
	else
		fdir_desc->dtype_cmd_cntindex |= cpu_to_le32(
					   I40E_FILTER_PROGRAM_DESC_PCMD_REMOVE
					   << I40E_TXD_FLTR_QW1_PCMD_SHIFT);

	fdir_desc->dtype_cmd_cntindex |= cpu_to_le32((fdir_data->dest_ctl
					  << I40E_TXD_FLTR_QW1_DEST_SHIFT)
					  & I40E_TXD_FLTR_QW1_DEST_MASK);

	fdir_desc->dtype_cmd_cntindex |= cpu_to_le32(
		     (fdir_data->fd_status << I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT)
		      & I40E_TXD_FLTR_QW1_FD_STATUS_MASK);

	if (fdir_data->cnt_index != 0) {
		fdir_desc->dtype_cmd_cntindex |=
				    cpu_to_le32(I40E_TXD_FLTR_QW1_CNT_ENA_MASK);
		fdir_desc->dtype_cmd_cntindex |=
					    cpu_to_le32((fdir_data->cnt_index
					    << I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT)
					    & I40E_TXD_FLTR_QW1_CNTINDEX_MASK);
	}

	fdir_desc->fd_id = cpu_to_le32(fdir_data->fd_id);

	/* Now program a dummy descriptor */
	tx_desc = I40E_TX_DESC(tx_ring, tx_ring->next_to_use);
	tx_buf = &tx_ring->tx_bi[tx_ring->next_to_use];
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	tx_desc->buffer_addr = cpu_to_le64(dma);
	td_cmd = I40E_TX_DESC_CMD_EOP |
		 I40E_TX_DESC_CMD_RS  |
		 I40E_TX_DESC_CMD_DUMMY;

	tx_desc->cmd_type_offset_bsz =
		build_ctob(td_cmd, 0, I40E_FDIR_MAX_RAW_PACKET_LOOKUP, 0);

	/* Mark the data descriptor to be watched */
	tx_buf->next_to_watch = tx_desc;

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	writel(tx_ring->next_to_use, tx_ring->tail);
	return 0;

dma_fail:
	return -1;
}

/**
 * i40e_fd_handle_status - check the Programming Status for FD
 * @rx_ring: the Rx ring for this descriptor
 * @qw: the descriptor data
 * @prog_id: the id originally used for programming
 *
 * This is used to verify if the FD programming or invalidation
 * requested by SW to the HW is successful or not and take actions accordingly.
 **/
static void i40e_fd_handle_status(struct i40e_ring *rx_ring, u32 qw, u8 prog_id)
{
	struct pci_dev *pdev = rx_ring->vsi->back->pdev;
	u32 error;

	error = (qw & I40E_RX_PROG_STATUS_DESC_QW1_ERROR_MASK) >>
		I40E_RX_PROG_STATUS_DESC_QW1_ERROR_SHIFT;

	/* for now just print the Status */
	dev_info(&pdev->dev, "FD programming id %02x, Status %08x\n",
		 prog_id, error);
}

/**
 * i40e_unmap_tx_resource - Release a Tx buffer
 * @ring:      the ring that owns the buffer
 * @tx_buffer: the buffer to free
 **/
static inline void i40e_unmap_tx_resource(struct i40e_ring *ring,
					  struct i40e_tx_buffer *tx_buffer)
{
	if (tx_buffer->dma) {
		if (tx_buffer->tx_flags & I40E_TX_FLAGS_MAPPED_AS_PAGE)
			dma_unmap_page(ring->dev,
				       tx_buffer->dma,
				       tx_buffer->length,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(ring->dev,
					 tx_buffer->dma,
					 tx_buffer->length,
					 DMA_TO_DEVICE);
	}
	tx_buffer->dma = 0;
	tx_buffer->time_stamp = 0;
}

/**
 * i40e_clean_tx_ring - Free any empty Tx buffers
 * @tx_ring: ring to be cleaned
 **/
void i40e_clean_tx_ring(struct i40e_ring *tx_ring)
{
	struct i40e_tx_buffer *tx_buffer;
	unsigned long bi_size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_bi)
		return;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++) {
		tx_buffer = &tx_ring->tx_bi[i];
		i40e_unmap_tx_resource(tx_ring, tx_buffer);
		if (tx_buffer->skb)
			dev_kfree_skb_any(tx_buffer->skb);
		tx_buffer->skb = NULL;
	}

	bi_size = sizeof(struct i40e_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_bi, 0, bi_size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * i40e_free_tx_resources - Free Tx resources per queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
void i40e_free_tx_resources(struct i40e_ring *tx_ring)
{
	i40e_clean_tx_ring(tx_ring);
	kfree(tx_ring->tx_bi);
	tx_ring->tx_bi = NULL;

	if (tx_ring->desc) {
		dma_free_coherent(tx_ring->dev, tx_ring->size,
				  tx_ring->desc, tx_ring->dma);
		tx_ring->desc = NULL;
	}
}

/**
 * i40e_get_tx_pending - how many tx descriptors not processed
 * @tx_ring: the ring of descriptors
 *
 * Since there is no access to the ring head register
 * in XL710, we need to use our local copies
 **/
static u32 i40e_get_tx_pending(struct i40e_ring *ring)
{
	u32 ntu = ((ring->next_to_clean <= ring->next_to_use)
			? ring->next_to_use
			: ring->next_to_use + ring->count);
	return ntu - ring->next_to_clean;
}

/**
 * i40e_check_tx_hang - Is there a hang in the Tx queue
 * @tx_ring: the ring of descriptors
 **/
static bool i40e_check_tx_hang(struct i40e_ring *tx_ring)
{
	u32 tx_pending = i40e_get_tx_pending(tx_ring);
	bool ret = false;

	clear_check_for_tx_hang(tx_ring);

	/* Check for a hung queue, but be thorough. This verifies
	 * that a transmit has been completed since the previous
	 * check AND there is at least one packet pending. The
	 * ARMED bit is set to indicate a potential hang. The
	 * bit is cleared if a pause frame is received to remove
	 * false hang detection due to PFC or 802.3x frames. By
	 * requiring this to fail twice we avoid races with
	 * PFC clearing the ARMED bit and conditions where we
	 * run the check_tx_hang logic with a transmit completion
	 * pending but without time to complete it yet.
	 */
	if ((tx_ring->tx_stats.tx_done_old == tx_ring->tx_stats.packets) &&
	    tx_pending) {
		/* make sure it is true for two checks in a row */
		ret = test_and_set_bit(__I40E_HANG_CHECK_ARMED,
				       &tx_ring->state);
	} else {
		/* update completed stats and disarm the hang check */
		tx_ring->tx_stats.tx_done_old = tx_ring->tx_stats.packets;
		clear_bit(__I40E_HANG_CHECK_ARMED, &tx_ring->state);
	}

	return ret;
}

/**
 * i40e_clean_tx_irq - Reclaim resources after transmit completes
 * @tx_ring:  tx ring to clean
 * @budget:   how many cleans we're allowed
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 **/
static bool i40e_clean_tx_irq(struct i40e_ring *tx_ring, int budget)
{
	u16 i = tx_ring->next_to_clean;
	struct i40e_tx_buffer *tx_buf;
	struct i40e_tx_desc *tx_desc;
	unsigned int total_packets = 0;
	unsigned int total_bytes = 0;

	tx_buf = &tx_ring->tx_bi[i];
	tx_desc = I40E_TX_DESC(tx_ring, i);

	for (; budget; budget--) {
		struct i40e_tx_desc *eop_desc;

		eop_desc = tx_buf->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* if the descriptor isn't done, no work yet to do */
		if (!(eop_desc->cmd_type_offset_bsz &
		      cpu_to_le64(I40E_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* count the packet as being completed */
		tx_ring->tx_stats.completed++;
		tx_buf->next_to_watch = NULL;
		tx_buf->time_stamp = 0;

		/* set memory barrier before eop_desc is verified */
		rmb();

		do {
			i40e_unmap_tx_resource(tx_ring, tx_buf);

			/* clear dtype status */
			tx_desc->cmd_type_offset_bsz &=
				~cpu_to_le64(I40E_TXD_QW1_DTYPE_MASK);

			if (likely(tx_desc == eop_desc)) {
				eop_desc = NULL;

				dev_kfree_skb_any(tx_buf->skb);
				tx_buf->skb = NULL;

				total_bytes += tx_buf->bytecount;
				total_packets += tx_buf->gso_segs;
			}

			tx_buf++;
			tx_desc++;
			i++;
			if (unlikely(i == tx_ring->count)) {
				i = 0;
				tx_buf = tx_ring->tx_bi;
				tx_desc = I40E_TX_DESC(tx_ring, 0);
			}
		} while (eop_desc);
	}

	tx_ring->next_to_clean = i;
	tx_ring->tx_stats.bytes += total_bytes;
	tx_ring->tx_stats.packets += total_packets;
	tx_ring->q_vector->tx.total_bytes += total_bytes;
	tx_ring->q_vector->tx.total_packets += total_packets;
	if (check_for_tx_hang(tx_ring) && i40e_check_tx_hang(tx_ring)) {
		/* schedule immediate reset if we believe we hung */
		dev_info(tx_ring->dev, "Detected Tx Unit Hang\n"
			 "  VSI                  <%d>\n"
			 "  Tx Queue             <%d>\n"
			 "  next_to_use          <%x>\n"
			 "  next_to_clean        <%x>\n",
			 tx_ring->vsi->seid,
			 tx_ring->queue_index,
			 tx_ring->next_to_use, i);
		dev_info(tx_ring->dev, "tx_bi[next_to_clean]\n"
			 "  time_stamp           <%lx>\n"
			 "  jiffies              <%lx>\n",
			 tx_ring->tx_bi[i].time_stamp, jiffies);

		netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);

		dev_info(tx_ring->dev,
			 "tx hang detected on queue %d, resetting adapter\n",
			 tx_ring->queue_index);

		tx_ring->netdev->netdev_ops->ndo_tx_timeout(tx_ring->netdev);

		/* the adapter is about to reset, no point in enabling stuff */
		return true;
	}

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	if (unlikely(total_packets && netif_carrier_ok(tx_ring->netdev) &&
		     (I40E_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->queue_index) &&
		   !test_bit(__I40E_DOWN, &tx_ring->vsi->state)) {
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);
			++tx_ring->tx_stats.restart_queue;
		}
	}

	return budget > 0;
}

/**
 * i40e_set_new_dynamic_itr - Find new ITR level
 * @rc: structure containing ring performance data
 *
 * Stores a new ITR value based on packets and byte counts during
 * the last interrupt.  The advantage of per interrupt computation
 * is faster updates and more accurate ITR for the current traffic
 * pattern.  Constants in this function were computed based on
 * theoretical maximum wire speed and thresholds were set based on
 * testing data as well as attempting to minimize response time
 * while increasing bulk throughput.
 **/
static void i40e_set_new_dynamic_itr(struct i40e_ring_container *rc)
{
	enum i40e_latency_range new_latency_range = rc->latency_range;
	u32 new_itr = rc->itr;
	int bytes_per_int;

	if (rc->total_packets == 0 || !rc->itr)
		return;

	/* simple throttlerate management
	 *   0-10MB/s   lowest (100000 ints/s)
	 *  10-20MB/s   low    (20000 ints/s)
	 *  20-1249MB/s bulk   (8000 ints/s)
	 */
	bytes_per_int = rc->total_bytes / rc->itr;
	switch (rc->itr) {
	case I40E_LOWEST_LATENCY:
		if (bytes_per_int > 10)
			new_latency_range = I40E_LOW_LATENCY;
		break;
	case I40E_LOW_LATENCY:
		if (bytes_per_int > 20)
			new_latency_range = I40E_BULK_LATENCY;
		else if (bytes_per_int <= 10)
			new_latency_range = I40E_LOWEST_LATENCY;
		break;
	case I40E_BULK_LATENCY:
		if (bytes_per_int <= 20)
			rc->latency_range = I40E_LOW_LATENCY;
		break;
	}

	switch (new_latency_range) {
	case I40E_LOWEST_LATENCY:
		new_itr = I40E_ITR_100K;
		break;
	case I40E_LOW_LATENCY:
		new_itr = I40E_ITR_20K;
		break;
	case I40E_BULK_LATENCY:
		new_itr = I40E_ITR_8K;
		break;
	default:
		break;
	}

	if (new_itr != rc->itr) {
		/* do an exponential smoothing */
		new_itr = (10 * new_itr * rc->itr) /
			  ((9 * new_itr) + rc->itr);
		rc->itr = new_itr & I40E_MAX_ITR;
	}

	rc->total_bytes = 0;
	rc->total_packets = 0;
}

/**
 * i40e_update_dynamic_itr - Adjust ITR based on bytes per int
 * @q_vector: the vector to adjust
 **/
static void i40e_update_dynamic_itr(struct i40e_q_vector *q_vector)
{
	u16 vector = q_vector->vsi->base_vector + q_vector->v_idx;
	struct i40e_hw *hw = &q_vector->vsi->back->hw;
	u32 reg_addr;
	u16 old_itr;

	reg_addr = I40E_PFINT_ITRN(I40E_RX_ITR, vector - 1);
	old_itr = q_vector->rx.itr;
	i40e_set_new_dynamic_itr(&q_vector->rx);
	if (old_itr != q_vector->rx.itr)
		wr32(hw, reg_addr, q_vector->rx.itr);

	reg_addr = I40E_PFINT_ITRN(I40E_TX_ITR, vector - 1);
	old_itr = q_vector->tx.itr;
	i40e_set_new_dynamic_itr(&q_vector->tx);
	if (old_itr != q_vector->tx.itr)
		wr32(hw, reg_addr, q_vector->tx.itr);

	i40e_flush(hw);
}

/**
 * i40e_clean_programming_status - clean the programming status descriptor
 * @rx_ring: the rx ring that has this descriptor
 * @rx_desc: the rx descriptor written back by HW
 *
 * Flow director should handle FD_FILTER_STATUS to check its filter programming
 * status being successful or not and take actions accordingly. FCoE should
 * handle its context/filter programming/invalidation status and take actions.
 *
 **/
static void i40e_clean_programming_status(struct i40e_ring *rx_ring,
					  union i40e_rx_desc *rx_desc)
{
	u64 qw;
	u8 id;

	qw = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	id = (qw & I40E_RX_PROG_STATUS_DESC_QW1_PROGID_MASK) >>
		  I40E_RX_PROG_STATUS_DESC_QW1_PROGID_SHIFT;

	if (id == I40E_RX_PROG_STATUS_DESC_FD_FILTER_STATUS)
		i40e_fd_handle_status(rx_ring, qw, id);
}

/**
 * i40e_setup_tx_descriptors - Allocate the Tx descriptors
 * @tx_ring: the tx ring to set up
 *
 * Return 0 on success, negative on error
 **/
int i40e_setup_tx_descriptors(struct i40e_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int bi_size;

	if (!dev)
		return -ENOMEM;

	bi_size = sizeof(struct i40e_tx_buffer) * tx_ring->count;
	tx_ring->tx_bi = kzalloc(bi_size, GFP_KERNEL);
	if (!tx_ring->tx_bi)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct i40e_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		dev_info(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			 tx_ring->size);
		goto err;
	}

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	return 0;

err:
	kfree(tx_ring->tx_bi);
	tx_ring->tx_bi = NULL;
	return -ENOMEM;
}

/**
 * i40e_clean_rx_ring - Free Rx buffers
 * @rx_ring: ring to be cleaned
 **/
void i40e_clean_rx_ring(struct i40e_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	struct i40e_rx_buffer *rx_bi;
	unsigned long bi_size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!rx_ring->rx_bi)
		return;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		rx_bi = &rx_ring->rx_bi[i];
		if (rx_bi->dma) {
			dma_unmap_single(dev,
					 rx_bi->dma,
					 rx_ring->rx_buf_len,
					 DMA_FROM_DEVICE);
			rx_bi->dma = 0;
		}
		if (rx_bi->skb) {
			dev_kfree_skb(rx_bi->skb);
			rx_bi->skb = NULL;
		}
		if (rx_bi->page) {
			if (rx_bi->page_dma) {
				dma_unmap_page(dev,
					       rx_bi->page_dma,
					       PAGE_SIZE / 2,
					       DMA_FROM_DEVICE);
				rx_bi->page_dma = 0;
			}
			__free_page(rx_bi->page);
			rx_bi->page = NULL;
			rx_bi->page_offset = 0;
		}
	}

	bi_size = sizeof(struct i40e_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_bi, 0, bi_size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * i40e_free_rx_resources - Free Rx resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
void i40e_free_rx_resources(struct i40e_ring *rx_ring)
{
	i40e_clean_rx_ring(rx_ring);
	kfree(rx_ring->rx_bi);
	rx_ring->rx_bi = NULL;

	if (rx_ring->desc) {
		dma_free_coherent(rx_ring->dev, rx_ring->size,
				  rx_ring->desc, rx_ring->dma);
		rx_ring->desc = NULL;
	}
}

/**
 * i40e_setup_rx_descriptors - Allocate Rx descriptors
 * @rx_ring: Rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int i40e_setup_rx_descriptors(struct i40e_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int bi_size;

	bi_size = sizeof(struct i40e_rx_buffer) * rx_ring->count;
	rx_ring->rx_bi = kzalloc(bi_size, GFP_KERNEL);
	if (!rx_ring->rx_bi)
		goto err;

	/* Round up to nearest 4K */
	rx_ring->size = ring_is_16byte_desc_enabled(rx_ring)
		? rx_ring->count * sizeof(union i40e_16byte_rx_desc)
		: rx_ring->count * sizeof(union i40e_32byte_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);
	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);

	if (!rx_ring->desc) {
		dev_info(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			 rx_ring->size);
		goto err;
	}

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	return 0;
err:
	kfree(rx_ring->rx_bi);
	rx_ring->rx_bi = NULL;
	return -ENOMEM;
}

/**
 * i40e_release_rx_desc - Store the new tail and head values
 * @rx_ring: ring to bump
 * @val: new head index
 **/
static inline void i40e_release_rx_desc(struct i40e_ring *rx_ring, u32 val)
{
	rx_ring->next_to_use = val;
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}

/**
 * i40e_alloc_rx_buffers - Replace used receive buffers; packet split
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 **/
void i40e_alloc_rx_buffers(struct i40e_ring *rx_ring, u16 cleaned_count)
{
	u16 i = rx_ring->next_to_use;
	union i40e_rx_desc *rx_desc;
	struct i40e_rx_buffer *bi;
	struct sk_buff *skb;

	/* do nothing if no valid netdev defined */
	if (!rx_ring->netdev || !cleaned_count)
		return;

	while (cleaned_count--) {
		rx_desc = I40E_RX_DESC(rx_ring, i);
		bi = &rx_ring->rx_bi[i];
		skb = bi->skb;

		if (!skb) {
			skb = netdev_alloc_skb_ip_align(rx_ring->netdev,
							rx_ring->rx_buf_len);
			if (!skb) {
				rx_ring->rx_stats.alloc_rx_buff_failed++;
				goto no_buffers;
			}
			/* initialize queue mapping */
			skb_record_rx_queue(skb, rx_ring->queue_index);
			bi->skb = skb;
		}

		if (!bi->dma) {
			bi->dma = dma_map_single(rx_ring->dev,
						 skb->data,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);
			if (dma_mapping_error(rx_ring->dev, bi->dma)) {
				rx_ring->rx_stats.alloc_rx_buff_failed++;
				bi->dma = 0;
				goto no_buffers;
			}
		}

		if (ring_is_ps_enabled(rx_ring)) {
			if (!bi->page) {
				bi->page = alloc_page(GFP_ATOMIC);
				if (!bi->page) {
					rx_ring->rx_stats.alloc_rx_page_failed++;
					goto no_buffers;
				}
			}

			if (!bi->page_dma) {
				/* use a half page if we're re-using */
				bi->page_offset ^= PAGE_SIZE / 2;
				bi->page_dma = dma_map_page(rx_ring->dev,
							    bi->page,
							    bi->page_offset,
							    PAGE_SIZE / 2,
							    DMA_FROM_DEVICE);
				if (dma_mapping_error(rx_ring->dev,
						      bi->page_dma)) {
					rx_ring->rx_stats.alloc_rx_page_failed++;
					bi->page_dma = 0;
					goto no_buffers;
				}
			}

			/* Refresh the desc even if buffer_addrs didn't change
			 * because each write-back erases this info.
			 */
			rx_desc->read.pkt_addr = cpu_to_le64(bi->page_dma);
			rx_desc->read.hdr_addr = cpu_to_le64(bi->dma);
		} else {
			rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);
			rx_desc->read.hdr_addr = 0;
		}
		i++;
		if (i == rx_ring->count)
			i = 0;
	}

no_buffers:
	if (rx_ring->next_to_use != i)
		i40e_release_rx_desc(rx_ring, i);
}

/**
 * i40e_receive_skb - Send a completed packet up the stack
 * @rx_ring:  rx ring in play
 * @skb: packet to send up
 * @vlan_tag: vlan tag for packet
 **/
static void i40e_receive_skb(struct i40e_ring *rx_ring,
			     struct sk_buff *skb, u16 vlan_tag)
{
	struct i40e_q_vector *q_vector = rx_ring->q_vector;
	struct i40e_vsi *vsi = rx_ring->vsi;
	u64 flags = vsi->back->flags;

	if (vlan_tag & VLAN_VID_MASK)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);

	if (flags & I40E_FLAG_IN_NETPOLL)
		netif_rx(skb);
	else
		napi_gro_receive(&q_vector->napi, skb);
}

/**
 * i40e_rx_checksum - Indicate in skb if hw indicated a good cksum
 * @vsi: the VSI we care about
 * @skb: skb currently being received and modified
 * @rx_status: status value of last descriptor in packet
 * @rx_error: error value of last descriptor in packet
 **/
static inline void i40e_rx_checksum(struct i40e_vsi *vsi,
				    struct sk_buff *skb,
				    u32 rx_status,
				    u32 rx_error)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Rx csum enabled and ip headers found? */
	if (!(vsi->netdev->features & NETIF_F_RXCSUM &&
	      rx_status & (1 << I40E_RX_DESC_STATUS_L3L4P_SHIFT)))
		return;

	/* IP or L4 checksum error */
	if (rx_error & ((1 << I40E_RX_DESC_ERROR_IPE_SHIFT) |
			(1 << I40E_RX_DESC_ERROR_L4E_SHIFT))) {
		vsi->back->hw_csum_rx_error++;
		return;
	}

	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

/**
 * i40e_rx_hash - returns the hash value from the Rx descriptor
 * @ring: descriptor ring
 * @rx_desc: specific descriptor
 **/
static inline u32 i40e_rx_hash(struct i40e_ring *ring,
			       union i40e_rx_desc *rx_desc)
{
	if (ring->netdev->features & NETIF_F_RXHASH) {
		if ((le64_to_cpu(rx_desc->wb.qword1.status_error_len) >>
		     I40E_RX_DESC_STATUS_FLTSTAT_SHIFT) &
		    I40E_RX_DESC_FLTSTAT_RSS_HASH)
			return le32_to_cpu(rx_desc->wb.qword0.hi_dword.rss);
	}
	return 0;
}

/**
 * i40e_clean_rx_irq - Reclaim resources after receive completes
 * @rx_ring:  rx ring to clean
 * @budget:   how many cleans we're allowed
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 **/
static int i40e_clean_rx_irq(struct i40e_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	u16 rx_packet_len, rx_header_len, rx_sph, rx_hbo;
	u16 cleaned_count = I40E_DESC_UNUSED(rx_ring);
	const int current_node = numa_node_id();
	struct i40e_vsi *vsi = rx_ring->vsi;
	u16 i = rx_ring->next_to_clean;
	union i40e_rx_desc *rx_desc;
	u32 rx_error, rx_status;
	u64 qword;

	rx_desc = I40E_RX_DESC(rx_ring, i);
	qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	rx_status = (qword & I40E_RXD_QW1_STATUS_MASK)
				>> I40E_RXD_QW1_STATUS_SHIFT;

	while (rx_status & (1 << I40E_RX_DESC_STATUS_DD_SHIFT)) {
		union i40e_rx_desc *next_rxd;
		struct i40e_rx_buffer *rx_bi;
		struct sk_buff *skb;
		u16 vlan_tag;
		if (i40e_rx_is_programming_status(qword)) {
			i40e_clean_programming_status(rx_ring, rx_desc);
			I40E_RX_NEXT_DESC_PREFETCH(rx_ring, i, next_rxd);
			goto next_desc;
		}
		rx_bi = &rx_ring->rx_bi[i];
		skb = rx_bi->skb;
		prefetch(skb->data);

		rx_packet_len = (qword & I40E_RXD_QW1_LENGTH_PBUF_MASK)
					      >> I40E_RXD_QW1_LENGTH_PBUF_SHIFT;
		rx_header_len = (qword & I40E_RXD_QW1_LENGTH_HBUF_MASK)
					      >> I40E_RXD_QW1_LENGTH_HBUF_SHIFT;
		rx_sph = (qword & I40E_RXD_QW1_LENGTH_SPH_MASK)
					      >> I40E_RXD_QW1_LENGTH_SPH_SHIFT;

		rx_error = (qword & I40E_RXD_QW1_ERROR_MASK)
					      >> I40E_RXD_QW1_ERROR_SHIFT;
		rx_hbo = rx_error & (1 << I40E_RX_DESC_ERROR_HBO_SHIFT);
		rx_error &= ~(1 << I40E_RX_DESC_ERROR_HBO_SHIFT);

		rx_bi->skb = NULL;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * STATUS_DD bit is set
		 */
		rmb();

		/* Get the header and possibly the whole packet
		 * If this is an skb from previous receive dma will be 0
		 */
		if (rx_bi->dma) {
			u16 len;

			if (rx_hbo)
				len = I40E_RX_HDR_SIZE;
			else if (rx_sph)
				len = rx_header_len;
			else if (rx_packet_len)
				len = rx_packet_len;   /* 1buf/no split found */
			else
				len = rx_header_len;   /* split always mode */

			skb_put(skb, len);
			dma_unmap_single(rx_ring->dev,
					 rx_bi->dma,
					 rx_ring->rx_buf_len,
					 DMA_FROM_DEVICE);
			rx_bi->dma = 0;
		}

		/* Get the rest of the data if this was a header split */
		if (ring_is_ps_enabled(rx_ring) && rx_packet_len) {

			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
					   rx_bi->page,
					   rx_bi->page_offset,
					   rx_packet_len);

			skb->len += rx_packet_len;
			skb->data_len += rx_packet_len;
			skb->truesize += rx_packet_len;

			if ((page_count(rx_bi->page) == 1) &&
			    (page_to_nid(rx_bi->page) == current_node))
				get_page(rx_bi->page);
			else
				rx_bi->page = NULL;

			dma_unmap_page(rx_ring->dev,
				       rx_bi->page_dma,
				       PAGE_SIZE / 2,
				       DMA_FROM_DEVICE);
			rx_bi->page_dma = 0;
		}
		I40E_RX_NEXT_DESC_PREFETCH(rx_ring, i, next_rxd);

		if (unlikely(
		    !(rx_status & (1 << I40E_RX_DESC_STATUS_EOF_SHIFT)))) {
			struct i40e_rx_buffer *next_buffer;

			next_buffer = &rx_ring->rx_bi[i];

			if (ring_is_ps_enabled(rx_ring)) {
				rx_bi->skb = next_buffer->skb;
				rx_bi->dma = next_buffer->dma;
				next_buffer->skb = skb;
				next_buffer->dma = 0;
			}
			rx_ring->rx_stats.non_eop_descs++;
			goto next_desc;
		}

		/* ERR_MASK will only have valid bits if EOP set */
		if (unlikely(rx_error & (1 << I40E_RX_DESC_ERROR_RXE_SHIFT))) {
			dev_kfree_skb_any(skb);
			goto next_desc;
		}

		skb->rxhash = i40e_rx_hash(rx_ring, rx_desc);
		i40e_rx_checksum(vsi, skb, rx_status, rx_error);

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;
		total_rx_packets++;

		skb->protocol = eth_type_trans(skb, rx_ring->netdev);
		vlan_tag = rx_status & (1 << I40E_RX_DESC_STATUS_L2TAG1P_SHIFT)
			 ? le16_to_cpu(rx_desc->wb.qword0.lo_dword.l2tag1)
			 : 0;
		i40e_receive_skb(rx_ring, skb, vlan_tag);

		rx_ring->netdev->last_rx = jiffies;
		budget--;
next_desc:
		rx_desc->wb.qword1.status_error_len = 0;
		if (!budget)
			break;

		cleaned_count++;
		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= I40E_RX_BUFFER_WRITE) {
			i40e_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
		rx_status = (qword & I40E_RXD_QW1_STATUS_MASK)
						>> I40E_RXD_QW1_STATUS_SHIFT;
	}

	rx_ring->next_to_clean = i;
	rx_ring->rx_stats.packets += total_rx_packets;
	rx_ring->rx_stats.bytes += total_rx_bytes;
	rx_ring->q_vector->rx.total_packets += total_rx_packets;
	rx_ring->q_vector->rx.total_bytes += total_rx_bytes;

	if (cleaned_count)
		i40e_alloc_rx_buffers(rx_ring, cleaned_count);

	return budget > 0;
}

/**
 * i40e_napi_poll - NAPI polling Rx/Tx cleanup routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 * This function will clean all queues associated with a q_vector.
 *
 * Returns the amount of work done
 **/
int i40e_napi_poll(struct napi_struct *napi, int budget)
{
	struct i40e_q_vector *q_vector =
			       container_of(napi, struct i40e_q_vector, napi);
	struct i40e_vsi *vsi = q_vector->vsi;
	bool clean_complete = true;
	int budget_per_ring;
	int i;

	if (test_bit(__I40E_DOWN, &vsi->state)) {
		napi_complete(napi);
		return 0;
	}

	/* We attempt to distribute budget to each Rx queue fairly, but don't
	 * allow the budget to go below 1 because that would exit polling early.
	 * Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	budget_per_ring = max(budget/q_vector->num_ringpairs, 1);
	for (i = 0; i < q_vector->num_ringpairs; i++) {
		clean_complete &= i40e_clean_tx_irq(q_vector->tx.ring[i],
						    vsi->work_limit);
		clean_complete &= i40e_clean_rx_irq(q_vector->rx.ring[i],
						    budget_per_ring);
	}

	/* If work not completed, return budget and polling will return */
	if (!clean_complete)
		return budget;

	/* Work is done so exit the polling mode and re-enable the interrupt */
	napi_complete(napi);
	if (ITR_IS_DYNAMIC(vsi->rx_itr_setting) ||
	    ITR_IS_DYNAMIC(vsi->tx_itr_setting))
		i40e_update_dynamic_itr(q_vector);

	if (!test_bit(__I40E_DOWN, &vsi->state)) {
		if (vsi->back->flags & I40E_FLAG_MSIX_ENABLED) {
			i40e_irq_dynamic_enable(vsi,
					q_vector->v_idx + vsi->base_vector);
		} else {
			struct i40e_hw *hw = &vsi->back->hw;
			/* We re-enable the queue 0 cause, but
			 * don't worry about dynamic_enable
			 * because we left it on for the other
			 * possible interrupts during napi
			 */
			u32 qval = rd32(hw, I40E_QINT_RQCTL(0));
			qval |= I40E_QINT_RQCTL_CAUSE_ENA_MASK;
			wr32(hw, I40E_QINT_RQCTL(0), qval);

			qval = rd32(hw, I40E_QINT_TQCTL(0));
			qval |= I40E_QINT_TQCTL_CAUSE_ENA_MASK;
			wr32(hw, I40E_QINT_TQCTL(0), qval);
			i40e_flush(hw);
		}
	}

	return 0;
}

/**
 * i40e_atr - Add a Flow Director ATR filter
 * @tx_ring:  ring to add programming descriptor to
 * @skb:      send buffer
 * @flags:    send flags
 * @protocol: wire protocol
 **/
static void i40e_atr(struct i40e_ring *tx_ring, struct sk_buff *skb,
		     u32 flags, __be16 protocol)
{
	struct i40e_filter_program_desc *fdir_desc;
	struct i40e_pf *pf = tx_ring->vsi->back;
	union {
		unsigned char *network;
		struct iphdr *ipv4;
		struct ipv6hdr *ipv6;
	} hdr;
	struct tcphdr *th;
	unsigned int hlen;
	u32 flex_ptype, dtype_cmd;

	/* make sure ATR is enabled */
	if (!(pf->flags & I40E_FLAG_FDIR_ATR_ENABLED))
		return;

	/* if sampling is disabled do nothing */
	if (!tx_ring->atr_sample_rate)
		return;

	tx_ring->atr_count++;

	/* snag network header to get L4 type and address */
	hdr.network = skb_network_header(skb);

	/* Currently only IPv4/IPv6 with TCP is supported */
	if (protocol == htons(ETH_P_IP)) {
		if (hdr.ipv4->protocol != IPPROTO_TCP)
			return;

		/* access ihl as a u8 to avoid unaligned access on ia64 */
		hlen = (hdr.network[0] & 0x0F) << 2;
	} else if (protocol == htons(ETH_P_IPV6)) {
		if (hdr.ipv6->nexthdr != IPPROTO_TCP)
			return;

		hlen = sizeof(struct ipv6hdr);
	} else {
		return;
	}

	th = (struct tcphdr *)(hdr.network + hlen);

	/* sample on all syn/fin packets or once every atr sample rate */
	if (!th->fin && !th->syn && (tx_ring->atr_count < tx_ring->atr_sample_rate))
		return;

	tx_ring->atr_count = 0;

	/* grab the next descriptor */
	fdir_desc = I40E_TX_FDIRDESC(tx_ring, tx_ring->next_to_use);
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	flex_ptype = (tx_ring->queue_index << I40E_TXD_FLTR_QW0_QINDEX_SHIFT) &
		      I40E_TXD_FLTR_QW0_QINDEX_MASK;
	flex_ptype |= (protocol == htons(ETH_P_IP)) ?
		      (I40E_FILTER_PCTYPE_NONF_IPV4_TCP <<
		       I40E_TXD_FLTR_QW0_PCTYPE_SHIFT) :
		      (I40E_FILTER_PCTYPE_NONF_IPV6_TCP <<
		       I40E_TXD_FLTR_QW0_PCTYPE_SHIFT);

	flex_ptype |= tx_ring->vsi->id << I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT;

	dtype_cmd = I40E_TX_DESC_DTYPE_FILTER_PROG;

	dtype_cmd |= th->fin ?
		     (I40E_FILTER_PROGRAM_DESC_PCMD_REMOVE <<
		      I40E_TXD_FLTR_QW1_PCMD_SHIFT) :
		     (I40E_FILTER_PROGRAM_DESC_PCMD_ADD_UPDATE <<
		      I40E_TXD_FLTR_QW1_PCMD_SHIFT);

	dtype_cmd |= I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_QINDEX <<
		     I40E_TXD_FLTR_QW1_DEST_SHIFT;

	dtype_cmd |= I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID <<
		     I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT;

	fdir_desc->qindex_flex_ptype_vsi = cpu_to_le32(flex_ptype);
	fdir_desc->dtype_cmd_cntindex = cpu_to_le32(dtype_cmd);
}

#define I40E_TXD_CMD (I40E_TX_DESC_CMD_EOP | I40E_TX_DESC_CMD_RS)
/**
 * i40e_tx_prepare_vlan_flags - prepare generic TX VLAN tagging flags for HW
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 * @flags:   the tx flags to be set
 *
 * Checks the skb and set up correspondingly several generic transmit flags
 * related to VLAN tagging for the HW, such as VLAN, DCB, etc.
 *
 * Returns error code indicate the frame should be dropped upon error and the
 * otherwise  returns 0 to indicate the flags has been set properly.
 **/
static int i40e_tx_prepare_vlan_flags(struct sk_buff *skb,
				      struct i40e_ring *tx_ring,
				      u32 *flags)
{
	__be16 protocol = skb->protocol;
	u32  tx_flags = 0;

	/* if we have a HW VLAN tag being added, default to the HW one */
	if (vlan_tx_tag_present(skb)) {
		tx_flags |= vlan_tx_tag_get(skb) << I40E_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= I40E_TX_FLAGS_HW_VLAN;
	/* else if it is a SW VLAN, check the next protocol and store the tag */
	} else if (protocol == __constant_htons(ETH_P_8021Q)) {
		struct vlan_hdr *vhdr, _vhdr;
		vhdr = skb_header_pointer(skb, ETH_HLEN, sizeof(_vhdr), &_vhdr);
		if (!vhdr)
			return -EINVAL;

		protocol = vhdr->h_vlan_encapsulated_proto;
		tx_flags |= ntohs(vhdr->h_vlan_TCI) << I40E_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= I40E_TX_FLAGS_SW_VLAN;
	}

	/* Insert 802.1p priority into VLAN header */
	if ((tx_ring->vsi->back->flags & I40E_FLAG_DCB_ENABLED) &&
	    ((tx_flags & (I40E_TX_FLAGS_HW_VLAN | I40E_TX_FLAGS_SW_VLAN)) ||
	     (skb->priority != TC_PRIO_CONTROL))) {
		tx_flags &= ~I40E_TX_FLAGS_VLAN_PRIO_MASK;
		tx_flags |= (skb->priority & 0x7) <<
				I40E_TX_FLAGS_VLAN_PRIO_SHIFT;
		if (tx_flags & I40E_TX_FLAGS_SW_VLAN) {
			struct vlan_ethhdr *vhdr;
			if (skb_header_cloned(skb) &&
			    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
				return -ENOMEM;
			vhdr = (struct vlan_ethhdr *)skb->data;
			vhdr->h_vlan_TCI = htons(tx_flags >>
						 I40E_TX_FLAGS_VLAN_SHIFT);
		} else {
			tx_flags |= I40E_TX_FLAGS_HW_VLAN;
		}
	}
	*flags = tx_flags;
	return 0;
}

/**
 * i40e_tx_csum - is checksum offload requested
 * @tx_ring:  ptr to the ring to send
 * @skb:      ptr to the skb we're sending
 * @tx_flags: the collected send information
 * @protocol: the send protocol
 *
 * Returns true if checksum offload is requested
 **/
static bool i40e_tx_csum(struct i40e_ring *tx_ring, struct sk_buff *skb,
			 u32 tx_flags, __be16 protocol)
{
	if ((skb->ip_summed != CHECKSUM_PARTIAL) &&
	    !(tx_flags & I40E_TX_FLAGS_TXSW)) {
		if (!(tx_flags & I40E_TX_FLAGS_HW_VLAN))
			return false;
	}

	return skb->ip_summed == CHECKSUM_PARTIAL;
}

/**
 * i40e_tso - set up the tso context descriptor
 * @tx_ring:  ptr to the ring to send
 * @skb:      ptr to the skb we're sending
 * @tx_flags: the collected send information
 * @protocol: the send protocol
 * @hdr_len:  ptr to the size of the packet header
 * @cd_tunneling: ptr to context descriptor bits
 *
 * Returns 0 if no TSO can happen, 1 if tso is going, or error
 **/
static int i40e_tso(struct i40e_ring *tx_ring, struct sk_buff *skb,
		    u32 tx_flags, __be16 protocol, u8 *hdr_len,
		    u64 *cd_type_cmd_tso_mss, u32 *cd_tunneling)
{
	u32 cd_cmd, cd_tso_len, cd_mss;
	struct tcphdr *tcph;
	struct iphdr *iph;
	u32 l4len;
	int err;
	struct ipv6hdr *ipv6h;

	if (!skb_is_gso(skb))
		return 0;

	if (skb_header_cloned(skb)) {
		err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	if (protocol == __constant_htons(ETH_P_IP)) {
		iph = skb->encapsulation ? inner_ip_hdr(skb) : ip_hdr(skb);
		tcph = skb->encapsulation ? inner_tcp_hdr(skb) : tcp_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcph->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
						 0, IPPROTO_TCP, 0);
	} else if (skb_is_gso_v6(skb)) {

		ipv6h = skb->encapsulation ? inner_ipv6_hdr(skb)
					   : ipv6_hdr(skb);
		tcph = skb->encapsulation ? inner_tcp_hdr(skb) : tcp_hdr(skb);
		ipv6h->payload_len = 0;
		tcph->check = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
					       0, IPPROTO_TCP, 0);
	}

	l4len = skb->encapsulation ? inner_tcp_hdrlen(skb) : tcp_hdrlen(skb);
	*hdr_len = (skb->encapsulation
		    ? (skb_inner_transport_header(skb) - skb->data)
		    : skb_transport_offset(skb)) + l4len;

	/* find the field values */
	cd_cmd = I40E_TX_CTX_DESC_TSO;
	cd_tso_len = skb->len - *hdr_len;
	cd_mss = skb_shinfo(skb)->gso_size;
	*cd_type_cmd_tso_mss |= ((u64)cd_cmd << I40E_TXD_CTX_QW1_CMD_SHIFT)
			     | ((u64)cd_tso_len
				<< I40E_TXD_CTX_QW1_TSO_LEN_SHIFT)
			     | ((u64)cd_mss << I40E_TXD_CTX_QW1_MSS_SHIFT);
	return 1;
}

/**
 * i40e_tx_enable_csum - Enable Tx checksum offloads
 * @skb: send buffer
 * @tx_flags: Tx flags currently set
 * @td_cmd: Tx descriptor command bits to set
 * @td_offset: Tx descriptor header offsets to set
 * @cd_tunneling: ptr to context desc bits
 **/
static void i40e_tx_enable_csum(struct sk_buff *skb, u32 tx_flags,
				u32 *td_cmd, u32 *td_offset,
				struct i40e_ring *tx_ring,
				u32 *cd_tunneling)
{
	struct ipv6hdr *this_ipv6_hdr;
	unsigned int this_tcp_hdrlen;
	struct iphdr *this_ip_hdr;
	u32 network_hdr_len;
	u8 l4_hdr = 0;

	if (skb->encapsulation) {
		network_hdr_len = skb_inner_network_header_len(skb);
		this_ip_hdr = inner_ip_hdr(skb);
		this_ipv6_hdr = inner_ipv6_hdr(skb);
		this_tcp_hdrlen = inner_tcp_hdrlen(skb);

		if (tx_flags & I40E_TX_FLAGS_IPV4) {

			if (tx_flags & I40E_TX_FLAGS_TSO) {
				*cd_tunneling |= I40E_TX_CTX_EXT_IP_IPV4;
				ip_hdr(skb)->check = 0;
			} else {
				*cd_tunneling |=
					 I40E_TX_CTX_EXT_IP_IPV4_NO_CSUM;
			}
		} else if (tx_flags & I40E_TX_FLAGS_IPV6) {
			if (tx_flags & I40E_TX_FLAGS_TSO) {
				*cd_tunneling |= I40E_TX_CTX_EXT_IP_IPV6;
				ip_hdr(skb)->check = 0;
			} else {
				*cd_tunneling |=
					 I40E_TX_CTX_EXT_IP_IPV4_NO_CSUM;
			}
		}

		/* Now set the ctx descriptor fields */
		*cd_tunneling |= (skb_network_header_len(skb) >> 2) <<
					I40E_TXD_CTX_QW0_EXT_IPLEN_SHIFT |
				   I40E_TXD_CTX_UDP_TUNNELING            |
				   ((skb_inner_network_offset(skb) -
					skb_transport_offset(skb)) >> 1) <<
				   I40E_TXD_CTX_QW0_NATLEN_SHIFT;

	} else {
		network_hdr_len = skb_network_header_len(skb);
		this_ip_hdr = ip_hdr(skb);
		this_ipv6_hdr = ipv6_hdr(skb);
		this_tcp_hdrlen = tcp_hdrlen(skb);
	}

	/* Enable IP checksum offloads */
	if (tx_flags & I40E_TX_FLAGS_IPV4) {
		l4_hdr = this_ip_hdr->protocol;
		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (tx_flags & I40E_TX_FLAGS_TSO) {
			*td_cmd |= I40E_TX_DESC_CMD_IIPT_IPV4_CSUM;
			this_ip_hdr->check = 0;
		} else {
			*td_cmd |= I40E_TX_DESC_CMD_IIPT_IPV4;
		}
		/* Now set the td_offset for IP header length */
		*td_offset = (network_hdr_len >> 2) <<
			      I40E_TX_DESC_LENGTH_IPLEN_SHIFT;
	} else if (tx_flags & I40E_TX_FLAGS_IPV6) {
		l4_hdr = this_ipv6_hdr->nexthdr;
		*td_cmd |= I40E_TX_DESC_CMD_IIPT_IPV6;
		/* Now set the td_offset for IP header length */
		*td_offset = (network_hdr_len >> 2) <<
			      I40E_TX_DESC_LENGTH_IPLEN_SHIFT;
	}
	/* words in MACLEN + dwords in IPLEN + dwords in L4Len */
	*td_offset |= (skb_network_offset(skb) >> 1) <<
		       I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

	/* Enable L4 checksum offloads */
	switch (l4_hdr) {
	case IPPROTO_TCP:
		/* enable checksum offloads */
		*td_cmd |= I40E_TX_DESC_CMD_L4T_EOFT_TCP;
		*td_offset |= (this_tcp_hdrlen >> 2) <<
			       I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	case IPPROTO_SCTP:
		/* enable SCTP checksum offload */
		*td_cmd |= I40E_TX_DESC_CMD_L4T_EOFT_SCTP;
		*td_offset |= (sizeof(struct sctphdr) >> 2) <<
			       I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	case IPPROTO_UDP:
		/* enable UDP checksum offload */
		*td_cmd |= I40E_TX_DESC_CMD_L4T_EOFT_UDP;
		*td_offset |= (sizeof(struct udphdr) >> 2) <<
			       I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	default:
		break;
	}
}

/**
 * i40e_create_tx_ctx Build the Tx context descriptor
 * @tx_ring:  ring to create the descriptor on
 * @cd_type_cmd_tso_mss: Quad Word 1
 * @cd_tunneling: Quad Word 0 - bits 0-31
 * @cd_l2tag2: Quad Word 0 - bits 32-63
 **/
static void i40e_create_tx_ctx(struct i40e_ring *tx_ring,
			       const u64 cd_type_cmd_tso_mss,
			       const u32 cd_tunneling, const u32 cd_l2tag2)
{
	struct i40e_tx_context_desc *context_desc;

	if (!cd_type_cmd_tso_mss && !cd_tunneling && !cd_l2tag2)
		return;

	/* grab the next descriptor */
	context_desc = I40E_TX_CTXTDESC(tx_ring, tx_ring->next_to_use);
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	/* cpu_to_le32 and assign to struct fields */
	context_desc->tunneling_params = cpu_to_le32(cd_tunneling);
	context_desc->l2tag2 = cpu_to_le16(cd_l2tag2);
	context_desc->type_cmd_tso_mss = cpu_to_le64(cd_type_cmd_tso_mss);
}

/**
 * i40e_tx_map - Build the Tx descriptor
 * @tx_ring:  ring to send buffer on
 * @skb:      send buffer
 * @first:    first buffer info buffer to use
 * @tx_flags: collected send information
 * @hdr_len:  size of the packet header
 * @td_cmd:   the command field in the descriptor
 * @td_offset: offset for checksum or crc
 **/
static void i40e_tx_map(struct i40e_ring *tx_ring, struct sk_buff *skb,
			struct i40e_tx_buffer *first, u32 tx_flags,
			const u8 hdr_len, u32 td_cmd, u32 td_offset)
{
	struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[0];
	unsigned int data_len = skb->data_len;
	unsigned int size = skb_headlen(skb);
	struct device *dev = tx_ring->dev;
	u32 paylen = skb->len - hdr_len;
	u16 i = tx_ring->next_to_use;
	struct i40e_tx_buffer *tx_bi;
	struct i40e_tx_desc *tx_desc;
	u32 buf_offset = 0;
	u32 td_tag = 0;
	dma_addr_t dma;
	u16 gso_segs;

	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma))
		goto dma_error;

	if (tx_flags & I40E_TX_FLAGS_HW_VLAN) {
		td_cmd |= I40E_TX_DESC_CMD_IL2TAG1;
		td_tag = (tx_flags & I40E_TX_FLAGS_VLAN_MASK) >>
			 I40E_TX_FLAGS_VLAN_SHIFT;
	}

	tx_desc = I40E_TX_DESC(tx_ring, i);
	for (;;) {
		while (size > I40E_MAX_DATA_PER_TXD) {
			tx_desc->buffer_addr = cpu_to_le64(dma + buf_offset);
			tx_desc->cmd_type_offset_bsz =
				build_ctob(td_cmd, td_offset,
					   I40E_MAX_DATA_PER_TXD, td_tag);

			buf_offset += I40E_MAX_DATA_PER_TXD;
			size -= I40E_MAX_DATA_PER_TXD;

			tx_desc++;
			i++;
			if (i == tx_ring->count) {
				tx_desc = I40E_TX_DESC(tx_ring, 0);
				i = 0;
			}
		}

		tx_bi = &tx_ring->tx_bi[i];
		tx_bi->length = buf_offset + size;
		tx_bi->tx_flags = tx_flags;
		tx_bi->dma = dma;

		tx_desc->buffer_addr = cpu_to_le64(dma + buf_offset);
		tx_desc->cmd_type_offset_bsz = build_ctob(td_cmd, td_offset,
							  size, td_tag);

		if (likely(!data_len))
			break;

		size = skb_frag_size(frag);
		data_len -= size;
		buf_offset = 0;
		tx_flags |= I40E_TX_FLAGS_MAPPED_AS_PAGE;

		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto dma_error;

		tx_desc++;
		i++;
		if (i == tx_ring->count) {
			tx_desc = I40E_TX_DESC(tx_ring, 0);
			i = 0;
		}

		frag++;
	}

	tx_desc->cmd_type_offset_bsz |=
		       cpu_to_le64((u64)I40E_TXD_CMD << I40E_TXD_QW1_CMD_SHIFT);

	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	if (tx_flags & (I40E_TX_FLAGS_TSO | I40E_TX_FLAGS_FSO))
		gso_segs = skb_shinfo(skb)->gso_segs;
	else
		gso_segs = 1;

	/* multiply data chunks by size of headers */
	tx_bi->bytecount = paylen + (gso_segs * hdr_len);
	tx_bi->gso_segs = gso_segs;
	tx_bi->skb = skb;

	/* set the timestamp and next to watch values */
	first->time_stamp = jiffies;
	first->next_to_watch = tx_desc;

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	writel(i, tx_ring->tail);
	return;

dma_error:
	dev_info(dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_bi map */
	for (;;) {
		tx_bi = &tx_ring->tx_bi[i];
		i40e_unmap_tx_resource(tx_ring, tx_bi);
		if (tx_bi == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	dev_kfree_skb_any(skb);

	tx_ring->next_to_use = i;
}

/**
 * __i40e_maybe_stop_tx - 2nd level check for tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns -EBUSY if a stop is needed, else 0
 **/
static inline int __i40e_maybe_stop_tx(struct i40e_ring *tx_ring, int size)
{
	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);
	smp_mb();

	/* Check again in a case another CPU has just made room available. */
	if (likely(I40E_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->queue_index);
	++tx_ring->tx_stats.restart_queue;
	return 0;
}

/**
 * i40e_maybe_stop_tx - 1st level check for tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns 0 if stop is not needed
 **/
static int i40e_maybe_stop_tx(struct i40e_ring *tx_ring, int size)
{
	if (likely(I40E_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __i40e_maybe_stop_tx(tx_ring, size);
}

/**
 * i40e_xmit_descriptor_count - calculate number of tx descriptors needed
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 *
 * Returns number of data descriptors needed for this skb. Returns 0 to indicate
 * there is not enough descriptors available in this ring since we need at least
 * one descriptor.
 **/
static int i40e_xmit_descriptor_count(struct sk_buff *skb,
				      struct i40e_ring *tx_ring)
{
#if PAGE_SIZE > I40E_MAX_DATA_PER_TXD
	unsigned int f;
#endif
	int count = 0;

	/* need: 1 descriptor per page * PAGE_SIZE/I40E_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_head_len/I40E_MAX_DATA_PER_TXD,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
#if PAGE_SIZE > I40E_MAX_DATA_PER_TXD
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
#else
	count += skb_shinfo(skb)->nr_frags;
#endif
	count += TXD_USE_COUNT(skb_headlen(skb));
	if (i40e_maybe_stop_tx(tx_ring, count + 3)) {
		tx_ring->tx_stats.tx_busy++;
		return 0;
	}
	return count;
}

/**
 * i40e_xmit_frame_ring - Sends buffer on Tx ring
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 **/
static netdev_tx_t i40e_xmit_frame_ring(struct sk_buff *skb,
					struct i40e_ring *tx_ring)
{
	u64 cd_type_cmd_tso_mss = I40E_TX_DESC_DTYPE_CONTEXT;
	u32 cd_tunneling = 0, cd_l2tag2 = 0;
	struct i40e_tx_buffer *first;
	u32 td_offset = 0;
	u32 tx_flags = 0;
	__be16 protocol;
	u32 td_cmd = 0;
	u8 hdr_len = 0;
	int tso;
	if (0 == i40e_xmit_descriptor_count(skb, tx_ring))
		return NETDEV_TX_BUSY;

	/* prepare the xmit flags */
	if (i40e_tx_prepare_vlan_flags(skb, tx_ring, &tx_flags))
		goto out_drop;

	/* obtain protocol of skb */
	protocol = skb->protocol;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_bi[tx_ring->next_to_use];

	/* setup IPv4/IPv6 offloads */
	if (protocol == __constant_htons(ETH_P_IP))
		tx_flags |= I40E_TX_FLAGS_IPV4;
	else if (protocol == __constant_htons(ETH_P_IPV6))
		tx_flags |= I40E_TX_FLAGS_IPV6;

	tso = i40e_tso(tx_ring, skb, tx_flags, protocol, &hdr_len,
		       &cd_type_cmd_tso_mss, &cd_tunneling);

	if (tso < 0)
		goto out_drop;
	else if (tso)
		tx_flags |= I40E_TX_FLAGS_TSO;

	skb_tx_timestamp(skb);

	/* Always offload the checksum, since it's in the data descriptor */
	if (i40e_tx_csum(tx_ring, skb, tx_flags, protocol))
		tx_flags |= I40E_TX_FLAGS_CSUM;

	/* always enable offload insertion */
	td_cmd |= I40E_TX_DESC_CMD_ICRC;

	if (tx_flags & I40E_TX_FLAGS_CSUM)
		i40e_tx_enable_csum(skb, tx_flags, &td_cmd, &td_offset,
				    tx_ring, &cd_tunneling);

	i40e_create_tx_ctx(tx_ring, cd_type_cmd_tso_mss,
			   cd_tunneling, cd_l2tag2);

	/* Add Flow Director ATR if it's enabled.
	 *
	 * NOTE: this must always be directly before the data descriptor.
	 */
	i40e_atr(tx_ring, skb, tx_flags, protocol);

	i40e_tx_map(tx_ring, skb, first, tx_flags, hdr_len,
		    td_cmd, td_offset);

	i40e_maybe_stop_tx(tx_ring, DESC_NEEDED);

	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * i40e_lan_xmit_frame - Selects the correct VSI and Tx queue to send buffer
 * @skb:    send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 **/
netdev_tx_t i40e_lan_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_ring *tx_ring = &vsi->tx_rings[skb->queue_mapping];

	/* hardware can't handle really short frames, hardware padding works
	 * beyond this point
	 */
	if (unlikely(skb->len < I40E_MIN_TX_LEN)) {
		if (skb_pad(skb, I40E_MIN_TX_LEN - skb->len))
			return NETDEV_TX_OK;
		skb->len = I40E_MIN_TX_LEN;
		skb_set_tail_pointer(skb, I40E_MIN_TX_LEN);
	}

	return i40e_xmit_frame_ring(skb, tx_ring);
}
