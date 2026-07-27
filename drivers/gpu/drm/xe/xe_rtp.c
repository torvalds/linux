// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_rtp.h"

#include <kunit/visibility.h>

#include <uapi/drm/xe_drm.h>

#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_topology.h"
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

struct rule_match_ctx {
	const struct xe_device *xe;
	struct xe_gt *gt;
	struct xe_hw_engine *hwe;
	const struct xe_rtp_rule *rules;
	const unsigned int n_rules;
	unsigned int head;
	int err;
};

static bool rule_is_item(const struct xe_rtp_rule *r)
{
	return r->match_type != XE_RTP_MATCH_OR;
}

static bool rule_match_item(struct rule_match_ctx *match_ctx)
{
	const struct xe_device *xe = match_ctx->xe;
	struct xe_gt *gt = match_ctx->gt;
	struct xe_hw_engine *hwe = match_ctx->hwe;
	const struct xe_rtp_rule *r = &match_ctx->rules[match_ctx->head];

	switch (r->match_type) {
	case XE_RTP_MATCH_PLATFORM:
		return xe->info.platform == r->platform;
	case XE_RTP_MATCH_SUBPLATFORM:
		return xe->info.platform == r->platform &&
			xe->info.subplatform == r->subplatform;
	case XE_RTP_MATCH_PLATFORM_STEP:
		if (drm_WARN_ON(&xe->drm, xe->info.step.platform == STEP_NONE))
			return false;

		return xe->info.step.platform >= r->step_start &&
			xe->info.step.platform < r->step_end;
	case XE_RTP_MATCH_GRAPHICS_VERSION:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.graphics_verx100 == r->ver_start &&
			(!has_samedia(xe) || !xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_GRAPHICS_VERSION_RANGE:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.graphics_verx100 >= r->ver_start &&
			xe->info.graphics_verx100 <= r->ver_end &&
			(!has_samedia(xe) || !xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_GRAPHICS_VERSION_ANY_GT:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.graphics_verx100 == r->ver_start;
	case XE_RTP_MATCH_GRAPHICS_STEP:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.step.graphics >= r->step_start &&
			xe->info.step.graphics < r->step_end &&
			(!has_samedia(xe) || !xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_MEDIA_VERSION:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.media_verx100 == r->ver_start &&
			(!has_samedia(xe) || xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_MEDIA_VERSION_RANGE:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.media_verx100 >= r->ver_start &&
			xe->info.media_verx100 <= r->ver_end &&
			(!has_samedia(xe) || xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_MEDIA_STEP:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.step.media >= r->step_start &&
			xe->info.step.media < r->step_end &&
			(!has_samedia(xe) || xe_gt_is_media_type(gt));
	case XE_RTP_MATCH_MEDIA_VERSION_ANY_GT:
		if (drm_WARN_ON(&xe->drm, !gt))
			return false;

		return xe->info.media_verx100 == r->ver_start;
	case XE_RTP_MATCH_INTEGRATED:
		return !xe->info.is_dgfx;
	case XE_RTP_MATCH_DISCRETE:
		return xe->info.is_dgfx;
	case XE_RTP_MATCH_ENGINE_CLASS:
		if (drm_WARN_ON(&xe->drm, !hwe))
			return false;

		return hwe->class == r->engine_class;
	case XE_RTP_MATCH_NOT_ENGINE_CLASS:
		if (drm_WARN_ON(&xe->drm, !hwe))
			return false;

		return hwe->class != r->engine_class;
	case XE_RTP_MATCH_FUNC:
		return r->match_func(xe, gt, hwe);
	default:
		drm_warn(&xe->drm, "Invalid RTP match %u\n",
			 r->match_type);
		return false;
	}
}

/*
 * Match a conjunctive set of rules (rules joined by an implicit "AND").
 *
 * Once one item evaluates to false, the remaining items are not evaluated
 * anymore.  Nevetheless, all rules are consumed to allow detecting syntax
 * errors.
 */
static bool rule_match_and(struct rule_match_ctx *match_ctx, bool parse_only)
{
	bool match = true;
	unsigned int count = 0;

	while (match_ctx->head < match_ctx->n_rules &&
	       rule_is_item(&match_ctx->rules[match_ctx->head])) {
		if (!parse_only)
			match = rule_match_item(match_ctx);

		if (!match)
			parse_only = true;

		match_ctx->head++;
		count++;
	}

	if (drm_WARN_ON(&match_ctx->xe->drm, !count)) {
		match_ctx->err = -EINVAL;

		if (!parse_only)
			match = false;
	}

	return match;
}

/*
 * Match a disjunctive set of rules (subset of rules joined by
 * "XE_RTP_MATCH_OR").
 *
 * Once one subset evaluates to true, the remaining items are not evaluated
 * anymore. Nevetheless, all rules are consumed to allow detecting syntax
 * errors.
 */
static bool rule_match_or(struct rule_match_ctx *match_ctx)
{
	bool match = rule_match_and(match_ctx, false);

	while (match_ctx->head < match_ctx->n_rules &&
	       match_ctx->rules[match_ctx->head].match_type == XE_RTP_MATCH_OR) {
		/* Consume XE_RTP_MATCH_OR. */
		match_ctx->head++;

		match = rule_match_and(match_ctx, match);
	}

	return match;
}

static bool rule_matches_with_err(const struct xe_device *xe,
				  struct xe_gt *gt,
				  struct xe_hw_engine *hwe,
				  const struct xe_rtp_rule *rules,
				  unsigned int n_rules,
				  int *err)
{
	struct rule_match_ctx match_ctx = {
		.xe = xe,
		.gt = gt,
		.hwe = hwe,
		.rules = rules,
		.n_rules = n_rules,
	};
	bool match = rule_match_or(&match_ctx);

	if (err)
		*err = match_ctx.err;

	return match;
}

static bool rule_matches(const struct xe_device *xe,
			 struct xe_gt *gt,
			 struct xe_hw_engine *hwe,
			 const struct xe_rtp_rule *rules,
			 unsigned int n_rules)
{
	return rule_matches_with_err(xe, gt, hwe, rules, n_rules, NULL);
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
	case XE_RTP_PROCESS_TYPE_DEVICE:
		*hwe = NULL;
		*gt = NULL;
		*xe = ctx->xe;
		break;
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
 * @table: Table with RTP definitions
 * @sr: Save-restore struct where matching rules execute the action. This can be
 *      viewed as the "coalesced view" of multiple the tables. The bits for each
 *      register set are expected not to collide with previously added entries
 * @process_in_vf: Whether this RTP table should get processed for SR-IOV VF
 *      devices.  Should generally only be 'true' for LRC tables.
 *
 * Walk the table pointed by @entries (with an empty sentinel) and add all
 * entries with matching rules to @sr. If @hwe is not NULL, its mmio_base is
 * used to calculate the right register offset
 */
void xe_rtp_process_to_sr(struct xe_rtp_process_ctx *ctx,
			  const struct xe_rtp_table_sr *table,
			  struct xe_reg_sr *sr,
			  bool process_in_vf)
{
	struct xe_hw_engine *hwe = NULL;
	struct xe_gt *gt = NULL;
	struct xe_device *xe = NULL;

	rtp_get_context(ctx, &hwe, &gt, &xe);

	if (!process_in_vf && IS_SRIOV_VF(xe))
		return;

	xe_assert(xe, table->entries);

	for (size_t i = 0; i < table->n_entries; i++) {
		const struct xe_rtp_entry_sr *entry = &table->entries[i];
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
			rtp_mark_active(xe, ctx, i);
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_process_to_sr);

/**
 * xe_rtp_process - Process all entries in rtp @table, without running any action
 * @ctx: The context for processing the table, with one of device, gt or hwe
 * @table: Table with RTP definitions
 *
 * Walk the table pointed by @table, executing the
 * rules. One difference from xe_rtp_process_to_sr(): there is no action
 * associated with each entry since this uses struct xe_rtp_entry. Its main use
 * is for marking active workarounds via
 * xe_rtp_process_ctx_enable_active_tracking().
 */
void xe_rtp_process(struct xe_rtp_process_ctx *ctx,
		    const struct xe_rtp_table *table)
{
	struct xe_hw_engine *hwe;
	struct xe_gt *gt;
	struct xe_device *xe;

	rtp_get_context(ctx, &hwe, &gt, &xe);

	xe_assert(xe, table->entries);

	for (size_t i = 0; i < table->n_entries; i++) {
		const struct xe_rtp_entry *entry = &table->entries[i];

		if (!rule_matches(xe, gt, hwe, entry->rules, entry->n_rules))
			continue;

		rtp_mark_active(xe, ctx, i);
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_process);

bool xe_rtp_match_always(const struct xe_device *xe,
			 const struct xe_gt *gt,
			 const struct xe_hw_engine *hwe)
{
	return true;
}

bool xe_rtp_match_even_instance(const struct xe_device *xe,
				const struct xe_gt *gt,
				const struct xe_hw_engine *hwe)
{
	return hwe->instance % 2 == 0;
}

bool xe_rtp_match_first_render_or_compute(const struct xe_device *xe,
					  const struct xe_gt *gt,
					  const struct xe_hw_engine *hwe)
{
	u64 render_compute_mask = gt->info.engine_mask &
		(XE_HW_ENGINE_CCS_MASK | XE_HW_ENGINE_RCS_MASK);

	return render_compute_mask &&
		hwe->engine_id == __ffs(render_compute_mask);
}

bool xe_rtp_match_not_sriov_vf(const struct xe_device *xe,
			       const struct xe_gt *gt,
			       const struct xe_hw_engine *hwe)
{
	return !IS_SRIOV_VF(xe);
}

bool xe_rtp_match_psmi_enabled(const struct xe_device *xe,
			       const struct xe_gt *gt,
			       const struct xe_hw_engine *hwe)
{
	return xe_configfs_get_psmi_enabled(to_pci_dev(xe->drm.dev));
}

bool xe_rtp_match_gt_has_discontiguous_dss_groups(const struct xe_device *xe,
						  const struct xe_gt *gt,
						  const struct xe_hw_engine *hwe)
{
	return xe_gt_has_discontiguous_dss_groups(gt);
}

bool xe_rtp_match_has_flat_ccs(const struct xe_device *xe,
			       const struct xe_gt *gt,
			       const struct xe_hw_engine *hwe)
{
	return xe->info.has_flat_ccs;
}

bool xe_rtp_match_has_msix(const struct xe_device *xe,
			   const struct xe_gt *gt,
			   const struct xe_hw_engine *hwe)
{
	return xe_device_has_msix(xe);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_rtp.c"
#endif
