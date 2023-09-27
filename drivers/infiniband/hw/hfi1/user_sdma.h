#ifndef _HFI1_USER_SDMA_H
#define _HFI1_USER_SDMA_H
/*
 * Copyright(c) 2020 - Cornelis Networks, Inc.
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
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
 */
#include <linux/device.h>
#include <linux/wait.h>

#include "common.h"
#include "iowait.h"
#include "user_exp_rcv.h"
#include "mmu_rb.h"

/* The maximum number of Data io vectors per message/request */
#define MAX_VECTORS_PER_REQ 8
/*
 * Maximum number of packet to send from each message/request
 * before moving to the next one.
 */
#define MAX_PKTS_PER_QUEUE 16

#define num_pages(x) (1 + ((((x) - 1) & PAGE_MASK) >> PAGE_SHIFT))

#define req_opcode(x) \
	(((x) >> HFI1_SDMA_REQ_OPCODE_SHIFT) & HFI1_SDMA_REQ_OPCODE_MASK)
#define req_version(x) \
	(((x) >> HFI1_SDMA_REQ_VERSION_SHIFT) & HFI1_SDMA_REQ_OPCODE_MASK)
#define req_iovcnt(x) \
	(((x) >> HFI1_SDMA_REQ_IOVCNT_SHIFT) & HFI1_SDMA_REQ_IOVCNT_MASK)

/* Number of BTH.PSN bits used for sequence number in expected rcvs */
#define BTH_SEQ_MASK 0x7ffull

#define AHG_KDETH_INTR_SHIFT 12
#define AHG_KDETH_SH_SHIFT   13
#define AHG_KDETH_ARRAY_SIZE  9

#define PBC2LRH(x) ((((x) & 0xfff) << 2) - 4)
#define LRH2PBC(x) ((((x) >> 2) + 1) & 0xfff)

/**
 * Build an SDMA AHG header update descriptor and save it to an array.
 * @arr        - Array to save the descriptor to.
 * @idx        - Index of the array at which the descriptor will be saved.
 * @array_size - Size of the array arr.
 * @dw         - Update index into the header in DWs.
 * @bit        - Start bit.
 * @width      - Field width.
 * @value      - 16 bits of immediate data to write into the field.
 * Returns -ERANGE if idx is invalid. If successful, returns the next index
 * (idx + 1) of the array to be used for the next descriptor.
 */
static inline int ahg_header_set(u32 *arr, int idx, size_t array_size,
				 u8 dw, u8 bit, u8 width, u16 value)
{
	if ((size_t)idx >= array_size)
		return -ERANGE;
	arr[idx++] = sdma_build_ahg_descriptor(value, dw, bit, width);
	return idx;
}

/* Tx request flag bits */
#define TXREQ_FLAGS_REQ_ACK   BIT(0)      /* Set the ACK bit in the header */
#define TXREQ_FLAGS_REQ_DISABLE_SH BIT(1) /* Disable header suppression */

enum pkt_q_sdma_state {
	SDMA_PKT_Q_ACTIVE,
	SDMA_PKT_Q_DEFERRED,
};

#define SDMA_IOWAIT_TIMEOUT 1000 /* in milliseconds */

#define SDMA_DBG(req, fmt, ...)				     \
	hfi1_cdbg(SDMA, "[%u:%u:%u:%u] " fmt, (req)->pq->dd->unit, \
		 (req)->pq->ctxt, (req)->pq->subctxt, (req)->info.comp_idx, \
		 ##__VA_ARGS__)

struct hfi1_user_sdma_pkt_q {
	u16 ctxt;
	u16 subctxt;
	u16 n_max_reqs;
	atomic_t n_reqs;
	u16 reqidx;
	struct hfi1_devdata *dd;
	struct kmem_cache *txreq_cache;
	struct user_sdma_request *reqs;
	unsigned long *req_in_use;
	struct iowait busy;
	enum pkt_q_sdma_state state;
	wait_queue_head_t wait;
	unsigned long unpinned;
	struct mmu_rb_handler *handler;
	atomic_t n_locked;
};

struct hfi1_user_sdma_comp_q {
	u16 nentries;
	struct hfi1_sdma_comp_entry *comps;
};

struct sdma_mmu_node {
	struct mmu_rb_node rb;
	struct hfi1_user_sdma_pkt_q *pq;
	struct page **pages;
	unsigned int npages;
};

struct user_sdma_iovec {
	struct list_head list;
	struct iovec iov;
	/*
	 * offset into the virtual address space of the vector at
	 * which we last left off.
	 */
	u64 offset;
};

/* evict operation argument */
struct evict_data {
	u32 cleared;	/* count evicted so far */
	u32 target;	/* target count to evict */
};

struct user_sdma_request {
	/* This is the original header from user space */
	struct hfi1_pkt_header hdr;

	/* Read mostly fields */
	struct hfi1_user_sdma_pkt_q *pq ____cacheline_aligned_in_smp;
	struct hfi1_user_sdma_comp_q *cq;
	/*
	 * Pointer to the SDMA engine for this request.
	 * Since different request could be on different VLs,
	 * each request will need it's own engine pointer.
	 */
	struct sdma_engine *sde;
	struct sdma_req_info info;
	/* TID array values copied from the tid_iov vector */
	u32 *tids;
	/* total length of the data in the request */
	u32 data_len;
	/* number of elements copied to the tids array */
	u16 n_tids;
	/*
	 * We copy the iovs for this request (based on
	 * info.iovcnt). These are only the data vectors
	 */
	u8 data_iovs;
	s8 ahg_idx;

	/* Writeable fields shared with interrupt */
	u16 seqcomp ____cacheline_aligned_in_smp;
	u16 seqsubmitted;

	/* Send side fields */
	struct list_head txps ____cacheline_aligned_in_smp;
	u16 seqnum;
	/*
	 * KDETH.OFFSET (TID) field
	 * The offset can cover multiple packets, depending on the
	 * size of the TID entry.
	 */
	u32 tidoffset;
	/*
	 * KDETH.Offset (Eager) field
	 * We need to remember the initial value so the headers
	 * can be updated properly.
	 */
	u32 koffset;
	u32 sent;
	/* TID index copied from the tid_iov vector */
	u16 tididx;
	/* progress index moving along the iovs array */
	u8 iov_idx;
	u8 has_error;

	struct user_sdma_iovec iovs[MAX_VECTORS_PER_REQ];
} ____cacheline_aligned_in_smp;

/*
 * A single txreq could span up to 3 physical pages when the MTU
 * is sufficiently large (> 4K). Each of the IOV pointers also
 * needs it's own set of flags so the vector has been handled
 * independently of each other.
 */
struct user_sdma_txreq {
	/* Packet header for the txreq */
	struct hfi1_pkt_header hdr;
	struct sdma_txreq txreq;
	struct list_head list;
	struct user_sdma_request *req;
	u16 flags;
	u16 seqnum;
};

int hfi1_user_sdma_alloc_queues(struct hfi1_ctxtdata *uctxt,
				struct hfi1_filedata *fd);
int hfi1_user_sdma_free_queues(struct hfi1_filedata *fd,
			       struct hfi1_ctxtdata *uctxt);
int hfi1_user_sdma_process_request(struct hfi1_filedata *fd,
				   struct iovec *iovec, unsigned long dim,
				   unsigned long *count);

static inline struct mm_struct *mm_from_sdma_node(struct sdma_mmu_node *node)
{
	return node->rb.handler->mn.mm;
}

#endif /* _HFI1_USER_SDMA_H */
