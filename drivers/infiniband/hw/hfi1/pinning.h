/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2023 Cornelis Networks, Inc.
 */
#ifndef _HFI1_PINNING_H
#define _HFI1_PINNING_H

struct hfi1_user_sdma_pkt_q;
struct user_sdma_request;
struct user_sdma_txreq;
struct user_sdma_iovec;

int hfi1_init_system_pinning(struct hfi1_user_sdma_pkt_q *pq);
void hfi1_free_system_pinning(struct hfi1_user_sdma_pkt_q *pq);
int hfi1_add_pages_to_sdma_packet(struct user_sdma_request *req,
				  struct user_sdma_txreq *tx,
				  struct user_sdma_iovec *iovec,
				  u32 *pkt_data_remaining);

#endif /* _HFI1_PINNING_H */
