// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

/**
 * idpf_rx_singleq_buf_hw_alloc_all - Replace used receive buffers
 * @rx_q: queue for which the hw buffers are allocated
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 */
bool idpf_rx_singleq_buf_hw_alloc_all(struct idpf_queue *rx_q,
				      u16 cleaned_count)
{
	struct virtchnl2_singleq_rx_buf_desc *desc;
	u16 nta = rx_q->next_to_alloc;
	struct idpf_rx_buf *buf;

	if (!cleaned_count)
		return false;

	desc = IDPF_SINGLEQ_RX_BUF_DESC(rx_q, nta);
	buf = &rx_q->rx_buf.buf[nta];

	do {
		dma_addr_t addr;

		addr = idpf_alloc_page(rx_q->pp, buf, rx_q->rx_buf_size);
		if (unlikely(addr == DMA_MAPPING_ERROR))
			break;

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		desc->pkt_addr = cpu_to_le64(addr);
		desc->hdr_addr = 0;
		desc++;

		buf++;
		nta++;
		if (unlikely(nta == rx_q->desc_count)) {
			desc = IDPF_SINGLEQ_RX_BUF_DESC(rx_q, 0);
			buf = rx_q->rx_buf.buf;
			nta = 0;
		}

		cleaned_count--;
	} while (cleaned_count);

	if (rx_q->next_to_alloc != nta) {
		idpf_rx_buf_hw_update(rx_q, nta);
		rx_q->next_to_alloc = nta;
	}

	return !!cleaned_count;
}
