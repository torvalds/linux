/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_CT_TYPES_H_
#define _XE_GUC_CT_TYPES_H_

#include <linux/interrupt.h>
#include <linux/iosys-map.h>
#include <linux/spinlock_types.h>
#include <linux/stackdepot.h>
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
	/** @ctb_size: size of the snapshot of the CTB */
	size_t ctb_size;
	/** @ctb: snapshot of the entire CTB */
	u32 *ctb;
};

/**
 * enum xe_guc_ct_state - CT state
 * @XE_GUC_CT_STATE_NOT_INITIALIZED: CT not initialized, messages not expected in this state
 * @XE_GUC_CT_STATE_DISABLED: CT disabled, messages not expected in this state
 * @XE_GUC_CT_STATE_STOPPED: CT stopped, drop messages without errors
 * @XE_GUC_CT_STATE_ENABLED: CT enabled, messages sent / received in this state
 */
enum xe_guc_ct_state {
	XE_GUC_CT_STATE_NOT_INITIALIZED = 0,
	XE_GUC_CT_STATE_DISABLED,
	XE_GUC_CT_STATE_STOPPED,
	XE_GUC_CT_STATE_ENABLED,
};

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
/** struct xe_dead_ct - Information for debugging a dead CT */
struct xe_dead_ct {
	/** @lock: protects memory allocation/free operations, and @reason updates */
	spinlock_t lock;
	/** @reason: bit mask of CT_DEAD_* reason codes */
	unsigned int reason;
	/** @reported: for preventing multiple dumps per error sequence */
	bool reported;
	/** @worker: worker thread to get out of interrupt context before dumping */
	struct work_struct worker;
	/** snapshot_ct: copy of CT state and CTB content at point of error */
	struct xe_guc_ct_snapshot *snapshot_ct;
	/** snapshot_log: copy of GuC log at point of error */
	struct xe_guc_log_snapshot *snapshot_log;
};

/** struct xe_fast_req_fence - Used to track FAST_REQ messages by fence to match error responses */
struct xe_fast_req_fence {
	/** @fence: sequence number sent in H2G and return in G2H error */
	u16 fence;
	/** @action: H2G action code */
	u16 action;
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
	/** @stack: call stack from when the H2G was sent */
	depot_stack_handle_t stack;
#endif
};
#endif

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
		/** @ctbs.send: Host to GuC (H2G, send) channel */
		struct guc_ctb h2g;
		/** @ctbs.recv: GuC to Host (G2H, receive) channel */
		struct guc_ctb g2h;
	} ctbs;
	/** @g2h_outstanding: number of outstanding G2H */
	u32 g2h_outstanding;
	/** @g2h_worker: worker to process G2H messages */
	struct work_struct g2h_worker;
	/** @safe_mode_worker: worker to check G2H messages with IRQ disabled */
	struct delayed_work safe_mode_worker;
	/** @state: CT state */
	enum xe_guc_ct_state state;
	/** @fence_seqno: G2H fence seqno - 16 bits used by CT */
	u32 fence_seqno;
	/** @fence_lookup: G2H fence lookup */
	struct xarray fence_lookup;
	/** @wq: wait queue used for reliable CT sends and freeing G2H credits */
	wait_queue_head_t wq;
	/** @g2h_fence_wq: wait queue used for G2H fencing */
	wait_queue_head_t g2h_fence_wq;
	/** @g2h_wq: used to process G2H */
	struct workqueue_struct *g2h_wq;
	/** @msg: Message buffer */
	u32 msg[GUC_CTB_MSG_MAX_LEN];
	/** @fast_msg: Message buffer */
	u32 fast_msg[GUC_CTB_MSG_MAX_LEN];

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
	/** @dead: information for debugging dead CTs */
	struct xe_dead_ct dead;
	/** @fast_req: history of FAST_REQ messages for matching with G2H error responses */
	struct xe_fast_req_fence fast_req[SZ_32];
#endif
};

#endif
