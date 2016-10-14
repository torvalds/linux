/*
 * Copyright(c) 2016 Intel Corporation.
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

#ifndef HFI1_SDMA_TXREQ_H
#define HFI1_SDMA_TXREQ_H

/* increased for AHG */
#define NUM_DESC 6

/*
 * struct sdma_desc - canonical fragment descriptor
 *
 * This is the descriptor carried in the tx request
 * corresponding to each fragment.
 *
 */
struct sdma_desc {
	/* private:  don't use directly */
	u64 qw[2];
};

/**
 * struct sdma_txreq - the sdma_txreq structure (one per packet)
 * @list: for use by user and by queuing for wait
 *
 * This is the representation of a packet which consists of some
 * number of fragments.   Storage is provided to within the structure.
 * for all fragments.
 *
 * The storage for the descriptors are automatically extended as needed
 * when the currently allocation is exceeded.
 *
 * The user (Verbs or PSM) may overload this structure with fields
 * specific to their use by putting this struct first in their struct.
 * The method of allocation of the overloaded structure is user dependent
 *
 * The list is the only public field in the structure.
 *
 */

#define SDMA_TXREQ_S_OK        0
#define SDMA_TXREQ_S_SENDERROR 1
#define SDMA_TXREQ_S_ABORTED   2
#define SDMA_TXREQ_S_SHUTDOWN  3

/* flags bits */
#define SDMA_TXREQ_F_URGENT       0x0001
#define SDMA_TXREQ_F_AHG_COPY     0x0002
#define SDMA_TXREQ_F_USE_AHG      0x0004

struct sdma_txreq;
typedef void (*callback_t)(struct sdma_txreq *, int);

struct iowait;
struct sdma_txreq {
	struct list_head list;
	/* private: */
	struct sdma_desc *descp;
	/* private: */
	void *coalesce_buf;
	/* private: */
	struct iowait *wait;
	/* private: */
	callback_t                  complete;
#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
	u64 sn;
#endif
	/* private: - used in coalesce/pad processing */
	u16                         packet_len;
	/* private: - down-counted to trigger last */
	u16                         tlen;
	/* private: */
	u16                         num_desc;
	/* private: */
	u16                         desc_limit;
	/* private: */
	u16                         next_descq_idx;
	/* private: */
	u16 coalesce_idx;
	/* private: flags */
	u16                         flags;
	/* private: */
	struct sdma_desc descs[NUM_DESC];
};

static inline int sdma_txreq_built(struct sdma_txreq *tx)
{
	return tx->num_desc;
}

#endif                          /* HFI1_SDMA_TXREQ_H */
