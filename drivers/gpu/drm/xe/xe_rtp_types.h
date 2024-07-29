/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_RTP_TYPES_
#define _XE_RTP_TYPES_

#include <linux/types.h>

#include "regs/xe_reg_defs.h"

struct xe_hw_engine;
struct xe_gt;

/**
 * struct xe_rtp_action - action to take for any matching rule
 *
 * This struct records what action should be taken in a register that has a
 * matching rule. Example of actions: set/clear bits.
 */
struct xe_rtp_action {
	/** @reg: Register */
	struct xe_reg		reg;
	/**
	 * @clr_bits: bits to clear when updating register. It's always a
	 * superset of bits being modified
	 */
	u32			clr_bits;
	/** @set_bits: bits to set when updating register */
	u32			set_bits;
#define XE_RTP_NOCHECK		.read_mask = 0
	/** @read_mask: mask for bits to consider when reading value back */
	u32			read_mask;
#define XE_RTP_ACTION_FLAG_ENGINE_BASE		BIT(0)
	/** @flags: flags to apply on rule evaluation or action */
	u8			flags;
};

enum {
	XE_RTP_MATCH_PLATFORM,
	XE_RTP_MATCH_SUBPLATFORM,
	XE_RTP_MATCH_GRAPHICS_VERSION,
	XE_RTP_MATCH_GRAPHICS_VERSION_RANGE,
	XE_RTP_MATCH_GRAPHICS_VERSION_ANY_GT,
	XE_RTP_MATCH_GRAPHICS_STEP,
	XE_RTP_MATCH_MEDIA_VERSION,
	XE_RTP_MATCH_MEDIA_VERSION_RANGE,
	XE_RTP_MATCH_MEDIA_VERSION_ANY_GT,
	XE_RTP_MATCH_MEDIA_STEP,
	XE_RTP_MATCH_INTEGRATED,
	XE_RTP_MATCH_DISCRETE,
	XE_RTP_MATCH_ENGINE_CLASS,
	XE_RTP_MATCH_NOT_ENGINE_CLASS,
	XE_RTP_MATCH_FUNC,
	XE_RTP_MATCH_OR,
};

/** struct xe_rtp_rule - match rule for processing entry */
struct xe_rtp_rule {
	u8 match_type;

	/* match filters */
	union {
		/* MATCH_PLATFORM / MATCH_SUBPLATFORM */
		struct {
			u8 platform;
			u8 subplatform;
		};
		/*
		 * MATCH_GRAPHICS_VERSION / XE_RTP_MATCH_GRAPHICS_VERSION_RANGE /
		 * MATCH_MEDIA_VERSION  / XE_RTP_MATCH_MEDIA_VERSION_RANGE
		 */
		struct {
			u32 ver_start;
#define XE_RTP_END_VERSION_UNDEFINED	U32_MAX
			u32 ver_end;
		};
		/* MATCH_STEP */
		struct {
			u8 step_start;
			u8 step_end;
		};
		/* MATCH_ENGINE_CLASS / MATCH_NOT_ENGINE_CLASS */
		struct {
			u8 engine_class;
		};
		/* MATCH_FUNC */
		bool (*match_func)(const struct xe_gt *gt,
				   const struct xe_hw_engine *hwe);
	};
};

/** struct xe_rtp_entry_sr - Entry in an rtp table */
struct xe_rtp_entry_sr {
	const char *name;
	const struct xe_rtp_action *actions;
	const struct xe_rtp_rule *rules;
	u8 n_rules;
	u8 n_actions;
#define XE_RTP_ENTRY_FLAG_FOREACH_ENGINE	BIT(0)
	u8 flags;
};

/** struct xe_rtp_entry - Entry in an rtp table, with no action associated */
struct xe_rtp_entry {
	const char *name;
	const struct xe_rtp_rule *rules;
	u8 n_rules;
};

enum xe_rtp_process_type {
	XE_RTP_PROCESS_TYPE_GT,
	XE_RTP_PROCESS_TYPE_ENGINE,
};

struct xe_rtp_process_ctx {
	union {
		struct xe_gt *gt;
		struct xe_hw_engine *hwe;
	};
	enum xe_rtp_process_type type;
	unsigned long *active_entries;
	size_t n_entries;
};

#endif
