/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
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
	void *pinning_ctx;
	/* Release reference to @pinning_ctx. May be called in interrupt context. Must not sleep. */
	void (*ctx_put)(void *ctx);
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
#define SDMA_TXREQ_F_VIP          0x0010

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
