// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_pipe.h"

size_t hab_pipe_calc_required_bytes(const uint32_t shared_buf_size)
{
	return sizeof(struct hab_pipe)
		+ (2 * (sizeof(struct hab_shared_buf) + shared_buf_size));
}

/*
 * Must store the tx and rx ring buf pointers in non-shared/local area and
 * always use such pointers(inaccessible from the remote untrusted side) to
 * read/write the shared ring buffer region. Following reasons to keep it
 * in local:
 * 1. Such kind of local ring buf pointers are of no use for the remote side.
 * 2. There is a info disclosure risk if they are stored and used in share buffer.
 * 3. Furthermore, the untrusted peer can modify it deliberately. It will cause
 *    arbitrary/OOB access on local side.
 */
struct hab_pipe_endpoint *hab_pipe_init(struct hab_pipe *pipe,
		struct hab_shared_buf **tx_buf_p,
		struct hab_shared_buf **rx_buf_p,
		struct dbg_items **itms,
		const uint32_t shared_buf_size, int top)
{
	struct hab_pipe_endpoint *ep = NULL;
	struct hab_shared_buf *buf_a = NULL;
	struct hab_shared_buf *buf_b = NULL;
	struct dbg_items *its = NULL;

	if (!pipe || !tx_buf_p || !rx_buf_p)
		return NULL;

	/* debug only */
	its = kzalloc(sizeof(struct dbg_items), GFP_KERNEL);

	buf_a = (struct hab_shared_buf *) pipe->buf_base;
	buf_b = (struct hab_shared_buf *) (pipe->buf_base
		+ sizeof(struct hab_shared_buf) + shared_buf_size);

	if (top) {
		ep = &pipe->top;
		memset(ep, 0, sizeof(*ep));
		*tx_buf_p = buf_a;
		*rx_buf_p = buf_b;
		pipe->legacy_buf_a = NULL;
	} else {
		ep = &pipe->bottom;
		memset(ep, 0, sizeof(*ep));
		*tx_buf_p = buf_b;
		*rx_buf_p = buf_a;
		memset(buf_b, 0, sizeof(struct hab_shared_buf));
		memset(buf_a, 0, sizeof(struct hab_shared_buf));
		buf_a->size = shared_buf_size;
		buf_b->size = shared_buf_size;

		pipe->legacy_buf_b = NULL;
		pipe->legacy_total_size = 0;
	}

	*itms = its;
	return ep;
}

uint32_t hab_pipe_write(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf,
		const uint32_t buf_size,
		unsigned char *p, uint32_t num_bytes)
{
	/* Save a copy for index and count to avoid ToC-ToU issue */
	uint32_t ep_tx_index = ep->tx_info.index;
	uint32_t ep_tx_wr_count = ep->tx_info.wr_count;
	uint32_t sh_buf_rd_count = sh_buf->rd_count;
	uint32_t space = 0U;
	uint32_t count1, count2;

	if (buf_size < (ep_tx_wr_count - sh_buf_rd_count)) {
		pr_err("rd/wr counter error wr:%u rd:%u\n",
			ep_tx_wr_count, sh_buf_rd_count);
		return 0;
	}
	space = buf_size - (ep_tx_wr_count - sh_buf_rd_count);

	if (!p || num_bytes > space || num_bytes == 0) {
		pr_err("****can not write to pipe p %pK to-write %d space available %d\n",
			p, num_bytes, space);
		return 0;
	}

	asm volatile("dmb ish" ::: "memory");

	if ((buf_size < ep_tx_index) || (buf_size < num_bytes)) {
		pr_err("index in tx ep is out of boundary or number of bytes is larger than the ring buffer size\n");
		return 0;
	}

	count1 = (num_bytes <= (buf_size - ep_tx_index))
		? num_bytes : (buf_size - ep_tx_index);
	count2 = num_bytes - count1;

	if (count1 > 0) {
		memcpy((void *)&sh_buf->data[ep_tx_index], p, count1);
		ep_tx_wr_count += count1;
		ep_tx_index += count1;
		if (ep_tx_index >= buf_size)
			ep_tx_index = 0;
	}
	if (count2 > 0) {/* handle buffer wrapping */
		memcpy((void *)&sh_buf->data[ep_tx_index],
			p + count1, count2);
		ep_tx_wr_count += count2;
		ep_tx_index += count2;
		if (ep_tx_index >= buf_size)
			ep_tx_index = 0;
	}

	ep->tx_info.wr_count = ep_tx_wr_count;
	ep->tx_info.index = ep_tx_index;

	return num_bytes;
}

/* Updates the write index which is shared with the other VM */
void hab_pipe_write_commit(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf)
{
	/* Must commit data before incrementing count */
	asm volatile("dmb ishst" ::: "memory");
	sh_buf->wr_count = ep->tx_info.wr_count;
}

#define HAB_HEAD_CLEAR     0xCC

uint32_t hab_pipe_read(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf,
		const uint32_t buf_size,
		unsigned char *p, uint32_t size, uint32_t clear)
{
	/* Save a copy for index to avoid ToC-ToU issue */
	uint32_t ep_rx_index = ep->rx_info.index;
	/* mb to guarantee wr_count is updated after contents are written */
	uint32_t avail = sh_buf->wr_count - sh_buf->rd_count;
	uint32_t count1, count2, to_read;
	uint32_t index_saved = ep_rx_index; /* store original for retry */
	static uint8_t signature_mismatch;

	if (!p || avail == 0 || size == 0 || ep_rx_index > buf_size)
		return 0;

	asm volatile("dmb ishld" ::: "memory");
	/* error if available is less than size and available is not zero */
	to_read = (avail < size) ? avail : size;

	/*
	 * Generally, the available size should be equal to the expected read size.
	 * But when calling hab_msg_drop() during message recv, available size may
	 * less than expected size.
	 */
	if (to_read < size)
		pr_info("less data available %d than requested %d\n",
			avail, size);

	count1 = (to_read <= (buf_size - ep_rx_index)) ? to_read :
		(buf_size - ep_rx_index);
	count2 = to_read - count1;

	if (count1 > 0) {
		memcpy(p, (void *)&sh_buf->data[ep_rx_index], count1);
		ep_rx_index += count1;
		if (ep_rx_index >= buf_size)
			ep_rx_index = 0;
	}
	if (count2 > 0) { /* handle buffer wrapping */
		memcpy(p + count1, (void *)&sh_buf->data[ep_rx_index],
			count2);
		ep_rx_index += count2;
	}

	ep->rx_info.index = ep_rx_index;

	if (count1 + count2) {
		struct hab_header *head = (struct hab_header *)p;
		int retry_cnt = 0;

		if (clear && (size == sizeof(*head))) {
retry:

			if (unlikely(head->signature != 0xBEE1BEE1)) {
				pr_debug("hab head corruption detected at %pK buf %pK %08X %08X %08X %08X %08X rd %d wr %d index %X saved %X retry %d\n",
					head, &sh_buf->data[0],
					head->id_type,
					head->payload_size,
					head->session_id,
					head->signature, head->sequence,
					sh_buf->rd_count, sh_buf->wr_count,
					ep->rx_info.index, index_saved,
					retry_cnt);
				if (retry_cnt++ <= 1000) {
					memcpy(p, &sh_buf->data[index_saved],
						   count1);
					if (count2)
						memcpy(&p[count1],
						&sh_buf->data[ep_rx_index - count2],
						count2);
					if (!signature_mismatch)
						goto retry;
				} else
					pr_err("quit retry after %d time may fail %X %X %X %X %X rd %d wr %d index %X\n",
						retry_cnt, head->id_type,
						head->payload_size,
						head->session_id,
						head->signature,
						head->sequence,
						sh_buf->rd_count,
						sh_buf->wr_count,
						ep->rx_info.index);

				signature_mismatch = 1;
			} else
				signature_mismatch = 0;

		}

		/* If the signature has mismatched,
		 * don't increment the shared buffer index.
		 */
		if (signature_mismatch) {
			ep->rx_info.index = index_saved + 1;
			if (ep->rx_info.index >= sh_buf->size)
				ep->rx_info.index = 0;

			to_read = (retry_cnt < 1000) ? 0xFFFFFFFE : 0xFFFFFFFF;
		}

		/*Must commit data before incremeting count*/
		asm volatile("dmb ish" ::: "memory");
		sh_buf->rd_count += (signature_mismatch) ? 1 : count1 + count2;
	}

	return to_read;
}

void hab_pipe_rxinfo(struct hab_pipe_endpoint *ep,
			struct hab_shared_buf *sh_buf,
			uint32_t *rd_cnt,
			uint32_t *wr_cnt, uint32_t *idx)
{
	*idx = ep->rx_info.index;
	*rd_cnt = sh_buf->rd_count;
	*wr_cnt = sh_buf->wr_count;
}
