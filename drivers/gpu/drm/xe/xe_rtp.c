// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_rtp.h"

#include <kunit/visibility.h>

#include <uapi/drm/xe_drm.h>

#include "xe_gt.h"
#include "xe_gt_topology.h"
#include "xe_macros.h"
#include "xe_reg_sr.h"
#include "xe_sriov.h"

/**
 * DOC: Register Table Processing
 *
 * Internal infrastructure to define how registers should be updated based on
 * rules and actions. This can be used to define tables with multiple entries
 * (one per register) that will be walked over at some point in time to apply
 * the values to the registers that have matching rules.
 */

static bool has_samedia(const struct xe_device *xe)
{
	return xe->info.media_verx100 >= 1300;
}

static bool rule_matches(const struct xe_device *xe,
			 struct xe_gt *gt,
			 struct xe_hw_engine *hwe,
			 const struct xe_rtp_rule *rules,
			 unsigned int n_rules)
{
	const struct xe_rtp_rule *r;
	unsigned int i, rcount = 0;
	bool match;

	for (r = rules, i = 0; i < n_rules; r = &rules[++i]) {
		switch (r->match_type) {
		case XE_RTP_MATCH_OR:
			/*
			 * This is only reached if a complete set of
			 * rules passed or none were evaluated. For both cases,
			 * shortcut the other rules and return the proper value.
			 */
			goto done;
		case XE_RTP_MATCH_PLATFORM:
			match = xe->info.platform == r->platform;
			break;
		case XE_RTP_MATCH_SUBPLATFORM:
			match = xe->info.platform == r->platform &&
				xe->info.subplatform == r->subplatform;
			break;
		case XE_RTP_MATCH_GRAPHICS_VERSION:
			match = xe->info.graphics_verx100 == r->ver_start &&
				(!has_samedia(xe) || !xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_GRAPHICS_VERSION_RANGE:
			match = xe->info.graphics_verx100 >= r->ver_start &&
				xe->info.graphics_verx100 <= r->ver_end &&
				(!has_samedia(xe) || !xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_GRAPHICS_VERSION_ANY_GT:
			match = xe->info.graphics_verx100 == r->ver_start;
			break;
		case XE_RTP_MATCH_GRAPHICS_STEP:
			match = xe->info.step.graphics >= r->step_start &&
				xe->info.step.graphics < r->step_end &&
				(!has_samedia(xe) || !xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_MEDIA_VERSION:
			match = xe->info.media_verx100 == r->ver_start &&
				(!has_samedia(xe) || xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_MEDIA_VERSION_RANGE:
			match = xe->info.media_verx100 >= r->ver_start &&
				xe->info.media_verx100 <= r->ver_end &&
				(!has_samedia(xe) || xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_MEDIA_STEP:
			match = xe->info.step.media >= r->step_start &&
				xe->info.step.media < r->step_end &&
				(!has_samedia(xe) || xe_gt_is_media_type(gt));
			break;
		case XE_RTP_MATCH_MEDIA_VERSION_ANY_GT:
			match = xe->info.media_verx100 == r->ver_start;
			break;
		case XE_RTP_MATCH_INTEGRATED:
			match = !xe->info.is_dgfx;
			break;
		case XE_RTP_MATCH_DISCRETE:
			match = xe->info.is_dgfx;
			break;
		case XE_RTP_MATCH_ENGINE_CLASS:
			if (drm_WARN_ON(&xe->drm, !hwe))
				return false;

			match = hwe->class == r->engine_class;
			break;
		case XE_RTP_MATCH_NOT_ENGINE_CLASS:
			if (drm_WARN_ON(&xe->drm, !hwe))
				return false;

			match = hwe->class != r->engine_class;
			break;
		case XE_RTP_MATCH_FUNC:
			match = r->match_func(gt, hwe);
			break;
		default:
			drm_warn(&xe->drm, "Invalid RTP match %u\n",
				 r->match_type);
			match = false;
		}

		if (!match) {
			/*
			 * Advance rules until we find XE_RTP_MATCH_OR to check
			 * if there's another set of conditions to check
			 */
			while (++i < n_rules && rules[i].match_type != XE_RTP_MATCH_OR)
				;

			if (i >= n_rules)
				return false;

			rcount = 0;
		} else {
			rcount++;
		}
	}

done:
	if (drm_WARN_ON(&xe->drm, !rcount))
		return false;

	return true;
}

static void rtp_add_sr_entry(const struct xe_rtp_action *action,
			     struct xe_gt *gt,
			     u32 mmio_base,
			     struct xe_reg_sr *sr)
{
	struct xe_reg_sr_entry sr_entry = {
		.reg = action->reg,
		.clr_bits = action->clr_bits,
		.set_bits = action->set_bits,
		.read_mask = action->read_mask,
	};

	sr_entry.reg.addr += mmio_base;
	xe_reg_sr_add(sr, &sr_entry, gt);
}

static bool rtp_process_one_sr(const struct xe_rtp_entry_sr *entry,
			       struct xe_device *xe, struct xe_gt *gt,
			       struct xe_hw_engine *hwe, struct xe_reg_sr *sr)
{
	const struct xe_rtp_action *action;
	u32 mmio_base;
	unsigned int i;

	if (!rule_matches(xe, gt, hwe, entry->rules, entry->n_rules))
		return false;

	for (i = 0, action = &entry->actions[0]; i < entry->n_actions; action++, i++) {
		if ((entry->flags & XE_RTP_ENTRY_FLAG_FOREACH_ENGINE) ||
		    (action->flags & XE_RTP_ACTION_FLAG_ENGINE_BASE))
			mmio_base = hwe->mmio_base;
		else
			mmio_base = 0;

		rtp_add_sr_entry(action, gt, mmio_base, sr);
	}

	return true;
}

static void rtp_get_context(struct xe_rtp_process_ctx *ctx,
			    struct xe_hw_engine **hwe,
			    struct xe_gt **gt,
			    struct xe_device **xe)
{
	switch (ctx->type) {
	case XE_RTP_PROCESS_TYPE_GT:
		*hwe = NULL;
		*gt = ctx->gt;
		*xe = gt_to_xe(*gt);
		break;
	case XE_RTP_PROCESS_TYPE_ENGINE:
		*hwe = ctx->hwe;
		*gt = (*hwe)->gt;
		*xe = gt_to_xe(*gt);
		break;
	}
}

/**
 * xe_rtp_process_ctx_enable_active_tracking - Enable tracking of active entries
 *
 * Set additional metadata to track what entries are considered "active", i.e.
 * their rules match the condition. Bits are never cleared: entries with
 * matching rules set the corresponding bit in the bitmap.
 *
 * @ctx: The context for processing the table
 * @active_entries: bitmap to store the active entries
 * @n_entries: number of entries to be processed
 */
void xe_rtp_process_ctx_enable_active_tracking(struct xe_rtp_process_ctx *ctx,
					       unsigned long *active_entries,
					       size_t n_entries)
{
	ctx->active_entries = active_entries;
	ctx->n_entries = n_entries;
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_process_ctx_enable_active_tracking);

static void rtp_mark_active(struct xe_device *xe,
			    struct xe_rtp_process_ctx *ctx,
			    unsigned int idx)
{
	if (!ctx->active_entries)
		return;

	if (drm_WARN_ON(&xe->drm, idx >= ctx->n_entries))
		return;

	bitmap_set(ctx->active_entries, idx, 1);
}

/**
 * xe_rtp_process_to_sr - Process all rtp @entries, adding the matching ones to
 *                        the save-restore argument.
 * @ctx: The context for processing the table, with one of device, gt or hwe
 * @entries: Table with RTP definitions
 * @sr: Save-restore struct where matching rules execute the action. This can be
 *      viewed as the "coalesced view" of multiple the tables. The bits for each
 *      register set are expected not to collide with previously added entries
 *
 * Walk the table pointed by @entries (with an empty sentinel) and add all
 * entries with matching rules to @sr. If @hwe is not NULL, its mmio_base is
 * used to calculate the right register offset
 */
void xe_rtp_process_to_sr(struct xe_rtp_process_ctx *ctx,
			  const struct xe_rtp_entry_sr *entries,
			  struct xe_reg_sr *sr)
{
	const struct xe_rtp_entry_sr *entry;
	struct xe_hw_engine *hwe = NULL;
	struct xe_gt *gt = NULL;
	struct xe_device *xe = NULL;

	rtp_get_context(ctx, &hwe, &gt, &xe);

	if (IS_SRIOV_VF(xe))
		return;

	for (entry = entries; entry && entry->name; entry++) {
		bool match = false;

		if (entry->flags & XE_RTP_ENTRY_FLAG_FOREACH_ENGINE) {
			struct xe_hw_engine *each_hwe;
			enum xe_hw_engine_id id;

			for_each_hw_engine(each_hwe, gt, id)
				match |= rtp_process_one_sr(entry, xe, gt,
							    each_hwe, sr);
		} else {
			match = rtp_process_one_sr(entry, xe, gt, hwe, sr);
		}

		if (match)
			rtp_mark_active(xe, ctx, entry - entries);
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_process_to_sr);

/**
 * xe_rtp_process - Process all rtp @entries, without running any action
 * @ctx: The context for processing the table, with one of device, gt or hwe
 * @entries: Table with RTP definitions
 *
 * Walk the table pointed by @entries (with an empty sentinel), executing the
 * rules. One difference from xe_rtp_process_to_sr(): there is no action
 * associated with each entry since this uses struct xe_rtp_entry. Its main use
 * is for marking active workarounds via
 * xe_rtp_process_ctx_enable_active_tracking().
 */
void xe_rtp_process(struct xe_rtp_process_ctx *ctx,
		    const struct xe_rtp_entry *entries)
{
	const struct xe_rtp_entry *entry;
	struct xe_hw_engine *hwe;
	struct xe_gt *gt;
	struct xe_device *xe;

	rtp_get_context(ctx, &hwe, &gt, &xe);

	for (entry = entries; entry && entry->rules; entry++) {
		if (!rule_matches(xe, gt, hwe, entry->rules, entry->n_rules))
			continue;

		rtp_mark_active(xe, ctx, entry - entries);
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_process);

bool xe_rtp_match_even_instance(const struct xe_gt *gt,
				const struct xe_hw_engine *hwe)
{
	return hwe->instance % 2 == 0;
}

bool xe_rtp_match_first_render_or_compute(const struct xe_gt *gt,
					  const struct xe_hw_engine *hwe)
{
	u64 render_compute_mask = gt->info.engine_mask &
		(XE_HW_ENGINE_CCS_MASK | XE_HW_ENGINE_RCS_MASK);

	return render_compute_mask &&
		hwe->engine_id == __ffs(render_compute_mask);
}

bool xe_rtp_match_first_gslice_fused_off(const struct xe_gt *gt,
					 const struct xe_hw_engine *hwe)
{
	unsigned int dss_per_gslice = 4;
	unsigned int dss;

	if (drm_WARN(&gt_to_xe(gt)->drm, xe_dss_mask_empty(gt->fuse_topo.g_dss_mask),
		     "Checking gslice for platform without geometry pipeline\n"))
		return false;

	dss = xe_dss_mask_group_ffs(gt->fuse_topo.g_dss_mask, 0, 0);

	return dss >= dss_per_gslice;
}

