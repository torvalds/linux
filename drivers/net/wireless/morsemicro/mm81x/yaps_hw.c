// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include "yaps_hw.h"
#include "bus.h"
#include "hif.h"
#include "yaps.h"

#define YAPS_HW_WINDOW_SIZE_BYTES 32768
#define YAPS_MAX_PKT_SIZE_BYTES 16128
#define YAPS_METADATA_PAGE_COUNT 1

#define YAPS_PHANDLE_CORRUPTION_WAR_EXTRA_PAGE 1

#define YAPS_PAGE_SIZE 256

/* Calculate padding required for yaps transaction */
#define YAPS_CALC_PADDING(_bytes) ((_bytes) & 0x3 ? (4 - ((_bytes) & 0x3)) : 0)

#define YAPS_RESERVED_PAGE_SIZE 256

/*
 * Yaps data stream delimiter is a 32 bit word with the following fields:
 *
 * pkt_size (14 bits) - Packet size not including delimiter or padding
 * pool_id  (3  bits) - Pool that pages should be allocated from.
 * padding  (2  bits) - Padding required to bring packet to word (4 byte)
 * irq      (1  bit ) - Raise a PKT_IRQ on the YDS this is sent to
 * reserved (5  bits) - Reserved, must write as 0
 * crc      (7  bits) - YAPS CRC
 */

/* Packet size not including delimiter or padding */
#define YAPS_DELIM_GET_PKT_SIZE(_delim) \
	(((_delim) & 0x3FFF) - YAPS_RESERVED_PAGE_SIZE)
#define YAPS_DELIM_SET_PKT_SIZE(_pkt_size) \
	(((_pkt_size) & 0x3FFF) + YAPS_RESERVED_PAGE_SIZE)
#define YAPS_DELIM_GET_PHANDLE_SIZE(_delim) (((_delim) & 0x3FFF))

/* Pool that pages should be allocated from. */
#define YAPS_DELIM_SET_POOL_ID(_pool_id) (((_pool_id) & 0x7) << 14)

/* Padding required to bring packet to word (4 byte) boundary */
#define YAPS_DELIM_GET_PADDING(_delim) (((_delim) >> 17) & 0x3)
#define YAPS_DELIM_SET_PADDING(_padding) (((_padding) & 0x3) << 17)

/* Raise a PKT_IRQ on the YDS this is sent to */
#define YAPS_DELIM_SET_IRQ(_irq) (((_irq) & 0x1) << 19)

/* YAPS CRC */
#define YAPS_DELIM_GET_CRC(_delim) (((_delim) >> 25) & 0x7F)
#define YAPS_DELIM_SET_CRC(_crc) (((_crc) & 0x7F) << 25)

struct mm81x_yaps_status_regs {
	/* Allocation pools */
	u32 tc_tx_pool_num_pages;
	u32 tc_cmd_pool_num_pages;
	u32 tc_beacon_pool_num_pages;
	u32 tc_mgmt_pool_num_pages;
	u32 fc_rx_pool_num_pages;
	u32 fc_resp_pool_num_pages;
	u32 fc_tx_sts_pool_num_pages;
	u32 fc_aux_pool_num_pages;
	u32 tc_tx_num_pkts;
	u32 tc_cmd_num_pkts;
	u32 tc_beacon_num_pkts;
	u32 tc_mgmt_num_pkts;
	u32 fc_num_pkts;
	u32 fc_done_num_pkts;
	u32 fc_rx_bytes_in_queue;
	u32 tc_delim_crc_fail_detected;
	u32 fc_host_ysl_status;
	u32 lock;
} __packed __aligned(8);

struct mm81x_yaps_hw_status_regs {
	__le32 tc_tx_pool_num_pages;
	__le32 tc_cmd_pool_num_pages;
	__le32 tc_beacon_pool_num_pages;
	__le32 tc_mgmt_pool_num_pages;
	__le32 fc_rx_pool_num_pages;
	__le32 fc_resp_pool_num_pages;
	__le32 fc_tx_sts_pool_num_pages;
	__le32 fc_aux_pool_num_pages;
	__le32 tc_tx_num_pkts;
	__le32 tc_cmd_num_pkts;
	__le32 tc_beacon_num_pkts;
	__le32 tc_mgmt_num_pkts;
	__le32 fc_num_pkts;
	__le32 fc_done_num_pkts;
	__le32 fc_rx_bytes_in_queue;
	__le32 tc_delim_crc_fail_detected;
	__le32 fc_host_ysl_status;
	__le32 lock;
} __packed __aligned(8);

struct mm81x_yaps_hw_aux_data {
	unsigned long access_lock;

	u32 yds_addr;
	u32 ysl_addr;
	u32 status_regs_addr;

	/* Alloc pool sizes */
	u16 tc_tx_pool_size;
	u16 tc_cmd_pool_size;
	u8 tc_beacon_pool_size;
	u8 tc_mgmt_pool_size;
	u8 fc_rx_pool_size;
	u8 fc_resp_pool_size;
	u8 fc_tx_sts_pool_size;
	u8 fc_aux_pool_size;

	/* To chip/from chip queue sizes */
	u8 tc_tx_q_size;
	u8 tc_cmd_q_size;
	u8 tc_beacon_q_size;
	u8 tc_mgmt_q_size;
	u8 fc_q_size;
	u8 fc_done_q_size;

	u16 reserved_yaps_page_size;

	/* Buffers to/from chip to support large contiguous reads/writes */
	char *to_chip_buffer;
	char *from_chip_buffer;

	/* status registers in host endian */
	struct mm81x_yaps_status_regs status_regs;

	/* DMA target buffer in firmware endian */
	struct mm81x_yaps_hw_status_regs hw_status_regs;
};

static int mm81x_yaps_hw_lock(struct mm81x_yaps *yaps)
{
	if (test_and_set_bit_lock(0, &yaps->aux_data->access_lock))
		return -1;
	return 0;
}

static void mm81x_yaps_hw_unlock(struct mm81x_yaps *yaps)
{
	clear_bit_unlock(0, &yaps->aux_data->access_lock);
}

static void
mm81x_yaps_hw_fill_aux_data_from_hw_tbl(struct mm81x_yaps_hw_aux_data *a,
					struct mm81x_yaps_hw_table *t)
{
	a->ysl_addr = __le32_to_cpu(t->ysl_addr);
	a->yds_addr = __le32_to_cpu(t->yds_addr);
	a->status_regs_addr = __le32_to_cpu(t->status_regs_addr);
	a->tc_tx_pool_size = __le16_to_cpu(t->tc_tx_pool_size);
	a->fc_rx_pool_size = __le16_to_cpu(t->fc_rx_pool_size);
	a->tc_cmd_pool_size = t->tc_cmd_pool_size;
	a->tc_beacon_pool_size = t->tc_beacon_pool_size;
	a->tc_mgmt_pool_size = t->tc_mgmt_pool_size;
	a->fc_resp_pool_size = t->fc_resp_pool_size;
	a->fc_tx_sts_pool_size = t->fc_tx_sts_pool_size;
	a->fc_aux_pool_size = t->fc_aux_pool_size;
	a->tc_tx_q_size = t->tc_tx_q_size;
	a->tc_cmd_q_size = t->tc_cmd_q_size;
	a->tc_beacon_q_size = t->tc_beacon_q_size;
	a->tc_mgmt_q_size = t->tc_mgmt_q_size;
	a->fc_q_size = t->fc_q_size;
	a->fc_done_q_size = t->fc_done_q_size;
	a->reserved_yaps_page_size = le16_to_cpu(t->yaps_reserved_page_size);
}

static u8 mm81x_yaps_hw_crc(u32 word)
{
	u8 crc = 0;
	u8 byte;
	int i;

	/* Mask to look at only non-CRC bits */
	word &= 0x1ffffff;

	for (i = 0; i < 4; i++) {
		byte = (word >> 24) & 0xff;
		crc = crc7_be(crc, &byte, 1);
		word <<= 8;
	}

	return crc >> 1;
}

static u32 mm81x_write_pkts_h_build_delim(struct mm81x_yaps *yaps,
					  unsigned int size, u8 pool_id,
					  bool irq)
{
	u32 delim = 0;

	delim |= YAPS_DELIM_SET_PKT_SIZE(size);
	delim |= YAPS_DELIM_SET_PADDING(YAPS_CALC_PADDING(size));
	delim |= YAPS_DELIM_SET_POOL_ID(pool_id);
	delim |= YAPS_DELIM_SET_IRQ(irq);
	delim |= YAPS_DELIM_SET_CRC(mm81x_yaps_hw_crc(delim));
	return delim;
}

void mm81x_yaps_hw_enable_irqs(struct mm81x *mors, bool enable)
{
	mm81x_hw_irq_enable(mors, MM81X_INT_YAPS_FC_PKT_WAITING_IRQN, enable);
	mm81x_hw_irq_enable(mors, MM81X_INT_YAPS_FC_PACKET_FREED_UP_IRQN,
			    enable);
}

void mm81x_yaps_hw_read_table(struct mm81x *mors,
			      struct mm81x_yaps_hw_table *tbl_ptr)
{
	mm81x_yaps_hw_fill_aux_data_from_hw_tbl(mors->hif.u.yaps.aux_data,
						tbl_ptr);
	mm81x_yaps_hw_enable_irqs(mors, true);
}

static unsigned int mm81x_write_pkts_h_pages_required(struct mm81x_yaps *yaps,
						      unsigned int size_bytes)
{
	/* Always account for the first metadata page */
	return DIV_ROUND_UP(size_bytes +
				    yaps->aux_data->reserved_yaps_page_size,
			    YAPS_PAGE_SIZE) +
	       YAPS_METADATA_PAGE_COUNT +
	       YAPS_PHANDLE_CORRUPTION_WAR_EXTRA_PAGE;
}

/*
 * Checks if a single pkt will fit in the chip using the pool/alloc holding
 * information from the last status register read.
 */
static bool mm81x_write_pkts_h_will_fit(struct mm81x_yaps *yaps,
					struct mm81x_yaps_pkt *pkt, bool update)
{
	bool will_fit = true;
	const int pages_required =
		mm81x_write_pkts_h_pages_required(yaps, pkt->skb->len);
	int *pool_pages_avail = NULL;
	int *pkts_in_queue = NULL;
	int queue_pkts_avail = 0;

	switch (pkt->tc_queue) {
	case MM81X_YAPS_TX_Q:
		pool_pages_avail =
			&yaps->aux_data->status_regs.tc_tx_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_tx_num_pkts;
		queue_pkts_avail =
			yaps->aux_data->tc_tx_q_size - *pkts_in_queue;
		break;
	case MM81X_YAPS_CMD_Q:
		pool_pages_avail =
			&yaps->aux_data->status_regs.tc_cmd_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_cmd_num_pkts;
		queue_pkts_avail =
			yaps->aux_data->tc_cmd_q_size - *pkts_in_queue;
		break;
	case MM81X_YAPS_BEACON_Q:
		pool_pages_avail =
			&yaps->aux_data->status_regs.tc_beacon_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_beacon_num_pkts;
		queue_pkts_avail =
			yaps->aux_data->tc_beacon_q_size - *pkts_in_queue;
		break;
	case MM81X_YAPS_MGMT_Q:
		pool_pages_avail =
			&yaps->aux_data->status_regs.tc_mgmt_pool_num_pages;
		pkts_in_queue = &yaps->aux_data->status_regs.tc_mgmt_num_pkts;
		queue_pkts_avail =
			yaps->aux_data->tc_mgmt_q_size - *pkts_in_queue;
		break;
	default:
		dev_err(yaps->mors->dev, "yaps invalid tc queue");
		return false;
	}

	WARN_ON(queue_pkts_avail < 0);

	if (pages_required > *pool_pages_avail)
		will_fit = false;

	if (queue_pkts_avail == 0)
		will_fit = false;

	if (will_fit && update) {
		*pool_pages_avail -= pages_required;
		*pkts_in_queue += 1;
	}

	return will_fit;
}

static int mm81x_write_pkts_h_err_check(struct mm81x_yaps *yaps,
					struct mm81x_yaps_pkt *pkt)
{
	if (pkt->skb->len + yaps->aux_data->reserved_yaps_page_size >
	    YAPS_MAX_PKT_SIZE_BYTES)
		return -EMSGSIZE;
	if (pkt->tc_queue >= MM81X_YAPS_NUM_TC_Q)
		return -EINVAL;
	if (!mm81x_write_pkts_h_will_fit(yaps, pkt, true))
		return -EAGAIN;

	return 0;
}

static int mm81x_yaps_hw_write_pkts(struct mm81x_yaps *yaps,
				    struct mm81x_yaps_pkt *pkts, int num_pkts,
				    int *num_pkts_sent)
{
	int ret = 0;
	int i;
	u32 delim = 0;
	int tx_len;
	int batch_txn_len = 0;
	int pkts_pending = 0;
	bool delim_irq = false;
	char *to_chip_buffer_aligned =
		PTR_ALIGN(yaps->aux_data->to_chip_buffer,
			  mm81x_bus_get_alignment(yaps->mors));
	char *write_buf = to_chip_buffer_aligned;

	ret = mm81x_yaps_hw_lock(yaps);
	if (ret) {
		dev_dbg(yaps->mors->dev, "yaps lock failed %d", ret);
		return ret;
	}

	*num_pkts_sent = 0;

	/* Check packet conditions */
	ret = mm81x_write_pkts_h_err_check(yaps, &pkts[0]);
	if (ret)
		goto exit;

	/* Batch packets into larger transactions */
	for (i = 0; i < num_pkts; ++i) {
		u32 pkt_size =
			pkts[i].skb->len + YAPS_CALC_PADDING(pkts[i].skb->len);
		tx_len = pkt_size + sizeof(delim);

		/*
		 * Send when we have reached window size, don't split pkt over
		 * boundary
		 */
		if ((batch_txn_len + tx_len) > YAPS_HW_WINDOW_SIZE_BYTES) {
			ret = mm81x_dm_write(yaps->mors,
					     yaps->aux_data->yds_addr,
					     to_chip_buffer_aligned,
					     batch_txn_len);

			batch_txn_len = 0;
			if (ret)
				goto exit;
			write_buf = to_chip_buffer_aligned;
			*num_pkts_sent += pkts_pending;
			pkts_pending = 0;
		}

		if ((i + 1) == num_pkts) {
			/* The last packet in the queue has IRQ set */
			delim_irq = true;
		} else {
			/*
			 * Since this is not the last packet, we can check for
			 * the next one. In case of errors in the next packet
			 * set the IRQ
			 */
			ret = mm81x_write_pkts_h_err_check(yaps, &pkts[i + 1]);
			if (ret)
				delim_irq = true;
		}

		/* Build stream header*/
		delim = mm81x_write_pkts_h_build_delim(
			yaps, pkt_size, pkts[i].tc_queue, delim_irq);
		*((__le32 *)write_buf) = cpu_to_le32(delim);
		memcpy(write_buf + sizeof(delim), pkts[i].skb->data,
		       pkts[i].skb->len);

		write_buf += tx_len;
		batch_txn_len += tx_len;
		pkts_pending++;

		if (ret)
			goto exit;
	}

exit:
	if (batch_txn_len > 0) {
		ret = mm81x_dm_write(yaps->mors, yaps->aux_data->yds_addr,
				     to_chip_buffer_aligned, batch_txn_len);
		*num_pkts_sent += pkts_pending;
	}

	mm81x_yaps_hw_unlock(yaps);
	return ret;
}

static bool mm81x_read_pkts_h_is_valid_delim(u32 delim)
{
	u8 calc_crc = mm81x_yaps_hw_crc(delim);
	int pkt_size = YAPS_DELIM_GET_PHANDLE_SIZE(delim);
	int padding = YAPS_DELIM_GET_PADDING(delim);

	if (calc_crc != YAPS_DELIM_GET_CRC(delim))
		return false;

	if (pkt_size == 0)
		return false;

	if ((pkt_size + padding) > YAPS_MAX_PKT_SIZE_BYTES)
		return false;

	/* Pkt length + padding should not require more padding */
	if (YAPS_CALC_PADDING(pkt_size) != padding)
		return false;

	return true;
}

static int mm81x_read_pkts_h_bytes_remaining(struct mm81x_yaps *yaps)
{
	u32 bytes_in_queue = yaps->aux_data->status_regs.fc_rx_bytes_in_queue;
	u32 delim_overhead =
		yaps->aux_data->status_regs.fc_num_pkts * sizeof(u32);
	u32 reserved_bytes = yaps->aux_data->status_regs.fc_num_pkts *
			     yaps->aux_data->reserved_yaps_page_size;

	if (WARN_ON(bytes_in_queue > INT_MAX) ||
	    WARN_ON(delim_overhead > INT_MAX) ||
	    WARN_ON(reserved_bytes > INT_MAX))
		return -EIO;

	return (int)bytes_in_queue;
}

static int mm81x_yaps_hw_read_pkts(struct mm81x_yaps *yaps,
				   struct mm81x_yaps_pkt *pkts,
				   int num_pkts_max, int *num_pkts_received)
{
	int ret;
	int i = 0;
	char *from_chip_buffer_aligned =
		PTR_ALIGN(yaps->aux_data->from_chip_buffer,
			  mm81x_bus_get_alignment(yaps->mors));
	char *read_ptr = from_chip_buffer_aligned;
	int bytes_remaining = mm81x_read_pkts_h_bytes_remaining(yaps);
	bool again = false;

	*num_pkts_received = 0;

	if (num_pkts_max == 0 || bytes_remaining == 0)
		return 0;
	if (bytes_remaining < 0)
		return bytes_remaining;

	if (bytes_remaining > YAPS_HW_WINDOW_SIZE_BYTES) {
		bytes_remaining = YAPS_HW_WINDOW_SIZE_BYTES;
		again = true;
	}

	/*
	 * This is more coarse-grained than it needs to be - once the data
	 * is read into a local buffer the lock can be released, however
	 * access to from_chip_buffer will need to be protected with its
	 * own lock
	 */
	ret = mm81x_yaps_hw_lock(yaps);
	if (ret) {
		dev_dbg(yaps->mors->dev, "yaps lock failed %d", ret);
		return ret;
	}

	/* Read all available packets to the buffer */
	ret = mm81x_dm_read(yaps->mors, yaps->aux_data->ysl_addr,
			    from_chip_buffer_aligned, bytes_remaining);

	if (ret)
		goto exit;

	/* Split serialised packets from buffer */
	while (i < num_pkts_max && bytes_remaining > 0) {
		u32 delim;
		int total_len;
		int pkt_size;

		delim = le32_to_cpu(*((__le32 *)read_ptr));
		read_ptr += sizeof(delim);
		bytes_remaining -= sizeof(delim);

		/* End of stream */
		if (!delim)
			break;

		if (!mm81x_read_pkts_h_is_valid_delim(delim)) {
			/*
			 * This will start a hunt for a valid delimiter. Given
			 * the CRC is only 7 bit it's possible to find an
			 * invalid block with a valid delimiter, leading to
			 * desynchronisation.
			 */
			dev_warn(yaps->mors->dev, "yaps invalid delim");
			break;
		}

		/* Total length in chip */
		pkt_size = YAPS_DELIM_GET_PKT_SIZE(delim);
		total_len = pkt_size + YAPS_DELIM_GET_PADDING(delim);

		if (pkts[i].skb)
			dev_err(yaps->mors->dev, "yaps packet leak");

		/* SKB doesn't want padding */
		pkts[i].skb = dev_alloc_skb(pkt_size);
		if (!pkts[i].skb) {
			ret = -ENOMEM;
			dev_err(yaps->mors->dev, "yaps no mem for skb");
			goto exit;
		}
		skb_put(pkts[i].skb, pkt_size);

		if (total_len <= bytes_remaining) {
			memcpy(pkts[i].skb->data, read_ptr, pkt_size);
			read_ptr += total_len;
			bytes_remaining -= total_len;
		} else {
			const int read_overhang_len =
				total_len - bytes_remaining;
			const int pkt_overhang_len = pkt_size - bytes_remaining;

			memcpy(pkts[i].skb->data, read_ptr, bytes_remaining);
			read_ptr = from_chip_buffer_aligned;

			ret = mm81x_dm_read(
				yaps->mors,
				/* Offset by 4 to avoid retry logic */
				yaps->aux_data->ysl_addr + 4, read_ptr,
				read_overhang_len);

			if (ret)
				goto exit;

			memcpy(pkts[i].skb->data + bytes_remaining, read_ptr,
			       pkt_overhang_len);
			read_ptr += read_overhang_len;
			bytes_remaining = 0;
		}

		*num_pkts_received += 1;
		i++;
	}

	if (again)
		ret = -EAGAIN;

exit:
	mm81x_yaps_hw_unlock(yaps);
	return ret;
}

static int mm81x_yaps_hw_update_status(struct mm81x_yaps *yaps)
{
	int ret;
	int tc_total_pkt_count;
	unsigned long reg_read_timeout;
	struct mm81x_yaps_status_regs *r = &yaps->aux_data->status_regs;
	struct mm81x_yaps_hw_status_regs *hw_r = &yaps->aux_data->hw_status_regs;

	ret = mm81x_yaps_hw_lock(yaps);
	if (ret) {
		dev_dbg(yaps->mors->dev, "yaps lock failed %d", ret);
		return ret;
	}

	reg_read_timeout = jiffies + msecs_to_jiffies(100);
	do {
		if (time_after(jiffies, reg_read_timeout)) {
			dev_err(yaps->mors->dev,
				"timed out reading status registers: %d", ret);
			ret = -ETIMEDOUT;
			break;
		}

		ret = mm81x_dm_read(yaps->mors,
				    yaps->aux_data->status_regs_addr,
				    (u8 *)hw_r, sizeof(*hw_r));
	} while (!ret && le32_to_cpu(hw_r->lock));

	if (ret) {
		if (ret != -ENODEV) {
			dev_err(yaps->mors->dev,
				"error reading yaps status registers: %d", ret);
		}
		goto exit_unlock;
	}

	r->tc_tx_pool_num_pages = le32_to_cpu(hw_r->tc_tx_pool_num_pages);
	r->tc_cmd_pool_num_pages = le32_to_cpu(hw_r->tc_cmd_pool_num_pages);
	r->tc_beacon_pool_num_pages = le32_to_cpu(hw_r->tc_beacon_pool_num_pages);
	r->tc_mgmt_pool_num_pages = le32_to_cpu(hw_r->tc_mgmt_pool_num_pages);
	r->fc_rx_pool_num_pages = le32_to_cpu(hw_r->fc_rx_pool_num_pages);
	r->fc_resp_pool_num_pages = le32_to_cpu(hw_r->fc_resp_pool_num_pages);
	r->fc_tx_sts_pool_num_pages = le32_to_cpu(hw_r->fc_tx_sts_pool_num_pages);
	r->fc_aux_pool_num_pages = le32_to_cpu(hw_r->fc_aux_pool_num_pages);
	r->tc_tx_num_pkts = le32_to_cpu(hw_r->tc_tx_num_pkts);
	r->tc_cmd_num_pkts = le32_to_cpu(hw_r->tc_cmd_num_pkts);
	r->tc_beacon_num_pkts = le32_to_cpu(hw_r->tc_beacon_num_pkts);
	r->tc_mgmt_num_pkts = le32_to_cpu(hw_r->tc_mgmt_num_pkts);
	r->fc_num_pkts = le32_to_cpu(hw_r->fc_num_pkts);
	r->fc_done_num_pkts = le32_to_cpu(hw_r->fc_done_num_pkts);
	r->fc_rx_bytes_in_queue = le32_to_cpu(hw_r->fc_rx_bytes_in_queue);
	r->tc_delim_crc_fail_detected = le32_to_cpu(hw_r->tc_delim_crc_fail_detected);
	r->lock = le32_to_cpu(hw_r->lock);
	r->fc_host_ysl_status = le32_to_cpu(hw_r->fc_host_ysl_status);

	tc_total_pkt_count = r->tc_tx_num_pkts + r->tc_cmd_num_pkts +
			     r->tc_beacon_num_pkts + r->tc_mgmt_num_pkts;

	if (r->tc_delim_crc_fail_detected) {
		/*
		 * Host and chip have become desynchronised. This can happen if
		 * the chip crashes during a YAPS transaction. We cannot
		 * recover from this.
		 */
		dev_err(yaps->mors->dev,
			"to-chip yaps delimiter CRC fail, pkt_count=%d",
			tc_total_pkt_count);
		ret = -EIO;
	}

	if (mm81x_read_pkts_h_bytes_remaining(yaps))
		set_bit(MM81X_HIF_EVT_RX_PEND, &yaps->mors->hif.event_flags);

exit_unlock:
	mm81x_yaps_hw_unlock(yaps);
	return ret;
}

static const struct mm81x_yaps_ops mm81x_yaps_hw_ops = {
	.write_pkts = mm81x_yaps_hw_write_pkts,
	.read_pkts = mm81x_yaps_hw_read_pkts,
	.update_status = mm81x_yaps_hw_update_status,
};

int mm81x_yaps_hw_init(struct mm81x *mors)
{
	int ret = 0;
	struct mm81x_yaps *yaps = NULL;
	int aux_data_len = sizeof(struct mm81x_yaps_hw_aux_data);
	int alignment = mm81x_bus_get_alignment(mors);

	yaps = &mors->hif.u.yaps;
	yaps->aux_data = kzalloc(aux_data_len, GFP_KERNEL);
	if (!yaps->aux_data) {
		ret = -ENOMEM;
		goto err_exit;
	}

	yaps->aux_data->to_chip_buffer =
		kzalloc(YAPS_HW_WINDOW_SIZE_BYTES + alignment - 1, GFP_KERNEL);
	if (!yaps->aux_data->to_chip_buffer) {
		ret = -ENOMEM;
		goto err_exit;
	}

	yaps->aux_data->from_chip_buffer =
		kzalloc(YAPS_HW_WINDOW_SIZE_BYTES + alignment - 1, GFP_KERNEL);
	if (!yaps->aux_data->from_chip_buffer) {
		ret = -ENOMEM;
		goto err_exit;
	}

	if (!IS_ALIGNED((uintptr_t)&yaps->aux_data->status_regs, alignment)) {
		dev_warn(mors->dev,
			 "Status registers are not aligned to %d bytes",
			 alignment);
	}

	yaps->ops = &mm81x_yaps_hw_ops;
	return ret;

err_exit:
	mm81x_yaps_hw_finish(mors);
	return ret;
}

void mm81x_yaps_hw_finish(struct mm81x *mors)
{
	struct mm81x_yaps *yaps;

	yaps = &mors->hif.u.yaps;
	if (yaps->aux_data) {
		kfree(yaps->aux_data->from_chip_buffer);
		yaps->aux_data->from_chip_buffer = NULL;
		kfree(yaps->aux_data->to_chip_buffer);
		yaps->aux_data->to_chip_buffer = NULL;
		kfree(yaps->aux_data);
		yaps->aux_data = NULL;
	}
}
