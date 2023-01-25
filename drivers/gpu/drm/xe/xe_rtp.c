// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_rtp.h"

#include <drm/xe_drm.h>

#include "xe_gt.h"
#include "xe_macros.h"
#include "xe_reg_sr.h"

/**
 * DOC: Register Table Processing
 *
 * Internal infrastructure to define how registers should be updated based on
 * rules and actions. This can be used to define tables with multiple entries
 * (one per register) that will be walked over at some point in time to apply
 * the values to the registers that have matching rules.
 */

static bool rule_matches(struct xe_gt *gt,
			 struct xe_hw_engine *hwe,
			 const struct xe_rtp_entry *entry)
{
	const struct xe_device *xe = gt_to_xe(gt);
	const struct xe_rtp_rule *r;
	unsigned int i;
	bool match;

	for (r = entry->rules, i = 0; i < entry->n_rules;
	     r = &entry->rules[++i]) {
		switch (r->match_type) {
		case XE_RTP_MATCH_PLATFORM:
			match = xe->info.platform == r->platform;
			break;
		case XE_RTP_MATCH_SUBPLATFORM:
			match = xe->info.platform == r->platform &&
				xe->info.subplatform == r->subplatform;
			break;
		case XE_RTP_MATCH_GRAPHICS_VERSION:
			/* TODO: match display */
			match = xe->info.graphics_verx100 == r->ver_start;
			break;
		case XE_RTP_MATCH_GRAPHICS_VERSION_RANGE:
			match = xe->info.graphics_verx100 >= r->ver_start &&
				xe->info.graphics_verx100 <= r->ver_end;
			break;
		case XE_RTP_MATCH_MEDIA_VERSION:
			match = xe->info.media_verx100 == r->ver_start;
			break;
		case XE_RTP_MATCH_MEDIA_VERSION_RANGE:
			match = xe->info.media_verx100 >= r->ver_start &&
				xe->info.media_verx100 <= r->ver_end;
			break;
		case XE_RTP_MATCH_STEP:
			/* TODO: match media/display */
			match = xe->info.step.graphics >= r->step_start &&
				xe->info.step.graphics < r->step_end;
			break;
		case XE_RTP_MATCH_ENGINE_CLASS:
			match = hwe->class == r->engine_class;
			break;
		case XE_RTP_MATCH_NOT_ENGINE_CLASS:
			match = hwe->class != r->engine_class;
			break;
		case XE_RTP_MATCH_FUNC:
			match = r->match_func(gt, hwe);
			break;
		case XE_RTP_MATCH_INTEGRATED:
			match = !xe->info.is_dgfx;
			break;
		case XE_RTP_MATCH_DISCRETE:
			match = xe->info.is_dgfx;
			break;

		default:
			XE_WARN_ON(r->match_type);
		}

		if (!match)
			return false;
	}

	return true;
}

static void rtp_add_sr_entry(const struct xe_rtp_entry *entry,
			     struct xe_gt *gt,
			     u32 mmio_base,
			     struct xe_reg_sr *sr)
{
	u32 reg = entry->action.reg + mmio_base;
	struct xe_reg_sr_entry sr_entry = {
		.clr_bits = entry->action.clr_bits,
		.set_bits = entry->action.set_bits,
		.read_mask = entry->action.read_mask,
		.masked_reg = entry->action.flags & XE_RTP_FLAG_MASKED_REG,
		.reg_type = entry->action.reg_type,
	};

	xe_reg_sr_add(sr, reg, &sr_entry);
}

/**
 * xe_rtp_process - Process all rtp @entries, adding the matching ones to @sr
 * @entries: Table with RTP definitions
 * @sr: Where to add an entry to with the values for matching. This can be
 *      viewed as the "coalesced view" of multiple the tables. The bits for each
 *      register set are expected not to collide with previously added entries
 * @gt: The GT to be used for matching rules
 * @hwe: Engine instance to use for matching rules and as mmio base
 *
 * Walk the table pointed by @entries (with an empty sentinel) and add all
 * entries with matching rules to @sr. If @hwe is not NULL, its mmio_base is
 * used to calculate the right register offset
 */
void xe_rtp_process(const struct xe_rtp_entry *entries, struct xe_reg_sr *sr,
		    struct xe_gt *gt, struct xe_hw_engine *hwe)
{
	const struct xe_rtp_entry *entry;

	for (entry = entries; entry && entry->name; entry++) {
		u32 mmio_base = 0;

		if (entry->action.flags & XE_RTP_FLAG_FOREACH_ENGINE) {
			struct xe_hw_engine *each_hwe;
			enum xe_hw_engine_id id;

			for_each_hw_engine(each_hwe, gt, id) {
				mmio_base = each_hwe->mmio_base;

				if (rule_matches(gt, each_hwe, entry))
					rtp_add_sr_entry(entry, gt, mmio_base, sr);
			}
		} else if (rule_matches(gt, hwe, entry)) {
			if (entry->action.flags & XE_RTP_FLAG_ENGINE_BASE)
				mmio_base = hwe->mmio_base;

			rtp_add_sr_entry(entry, gt, mmio_base, sr);
		}
	}
}
