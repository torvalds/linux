/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_CT_TYPES_H_
#define _XE_GUC_CT_TYPES_H_

#include <linux/interrupt.h>
#include <linux/iosys-map.h>
#include <linux/spinlock_types.h>
#include <linux/wait.h>
#include <linux/xarray.h>

#include "abi/guc_communication_ctb_abi.h"

#define XE_GUC_CT_SELFTEST

struct xe_bo;

/**
 * struct guc_ctb - GuC command transport buffer (CTB)
 */
struct guc_ctb {
	/** @desc: dma buffer map for CTB descriptor */
	struct iosys_map desc;
	/** @cmds: dma buffer map for CTB commands */
	struct iosys_map cmds;
	/** @size: size of CTB commands (DW) */
	u32 size;
	/** @resv_space: reserved space of CTB commands (DW) */
	u32 resv_space;
	/** @head: head of CTB commands (DW) */
	u32 head;
	/** @tail: tail of CTB commands (DW) */
	u32 tail;
	/** @space: space in CTB commands (DW) */
	u32 space;
	/** @broken: channel broken */
	bool broken;
};

/**
 * struct xe_guc_ct - GuC command transport (CT) layer
 *
 * Includes a pair of CT buffers for bi-directional communication and tracking
 * for the H2G and G2H requests sent and received through the buffers.
 */
struct xe_guc_ct {
	/** @bo: XE BO for CT */
	struct xe_bo *bo;
	/** @lock: protects everything in CT layer */
	struct mutex lock;
	/** @fast_lock: protects G2H channel and credits */
	spinlock_t fast_lock;
	/** @ctbs: buffers for sending and receiving commands */
	struct {
		/** @send: Host to GuC (H2G, send) channel */
		struct guc_ctb h2g;
		/** @recv: GuC to Host (G2H, receive) channel */
		struct guc_ctb g2h;
	} ctbs;
	/** @g2h_outstanding: number of outstanding G2H */
	u32 g2h_outstanding;
	/** @g2h_worker: worker to process G2H messages */
	struct work_struct g2h_worker;
	/** @enabled: CT enabled */
	bool enabled;
	/** @fence_seqno: G2H fence seqno - 16 bits used by CT */
	u32 fence_seqno;
	/** @fence_context: context for G2H fence */
	u64 fence_context;
	/** @fence_lookup: G2H fence lookup */
	struct xarray fence_lookup;
	/** @wq: wait queue used for reliable CT sends and freeing G2H credits */
	wait_queue_head_t wq;
#ifdef XE_GUC_CT_SELFTEST
	/** @suppress_irq_handler: force flow control to sender */
	bool suppress_irq_handler;
#endif
	/** @msg: Message buffer */
	u32 msg[GUC_CTB_MSG_MAX_LEN];
	/** @fast_msg: Message buffer */
	u32 fast_msg[GUC_CTB_MSG_MAX_LEN];
};

#endif
