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

struct xe_bo;

/**
 * struct guc_ctb_info - GuC command transport buffer (CTB) info
 */
struct guc_ctb_info {
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
 * struct guc_ctb - GuC command transport buffer (CTB)
 */
struct guc_ctb {
	/** @desc: dma buffer map for CTB descriptor */
	struct iosys_map desc;
	/** @cmds: dma buffer map for CTB commands */
	struct iosys_map cmds;
	/** @info: CTB info */
	struct guc_ctb_info info;
};

/**
 * struct guc_ctb_snapshot - GuC command transport buffer (CTB) snapshot
 */
struct guc_ctb_snapshot {
	/** @desc: snapshot of the CTB descriptor */
	struct guc_ct_buffer_desc desc;
	/** @cmds: snapshot of the CTB commands */
	u32 *cmds;
	/** @info: snapshot of the CTB info */
	struct guc_ctb_info info;
};

/**
 * struct xe_guc_ct_snapshot - GuC command transport (CT) snapshot
 */
struct xe_guc_ct_snapshot {
	/** @ct_enabled: CT enabled info at capture time. */
	bool ct_enabled;
	/** @g2h_outstanding: G2H outstanding info at the capture time */
	u32 g2h_outstanding;
	/** @g2h: G2H CTB snapshot */
	struct guc_ctb_snapshot g2h;
	/** @h2g: H2G CTB snapshot */
	struct guc_ctb_snapshot h2g;
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
	/** @fence_lookup: G2H fence lookup */
	struct xarray fence_lookup;
	/** @wq: wait queue used for reliable CT sends and freeing G2H credits */
	wait_queue_head_t wq;
	/** @g2h_fence_wq: wait queue used for G2H fencing */
	wait_queue_head_t g2h_fence_wq;
	/** @msg: Message buffer */
	u32 msg[GUC_CTB_MSG_MAX_LEN];
	/** @fast_msg: Message buffer */
	u32 fast_msg[GUC_CTB_MSG_MAX_LEN];
};

#endif
