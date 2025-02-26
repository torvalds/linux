// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_reg_whitelist.h"

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_oa_regs.h"
#include "regs/xe_regs.h"
#include "xe_gt_types.h"
#include "xe_gt_printk.h"
#include "xe_platform_types.h"
#include "xe_reg_sr.h"
#include "xe_rtp.h"
#include "xe_step.h"

#undef XE_REG_MCR
#define XE_REG_MCR(...)     XE_REG(__VA_ARGS__, .mcr = 1)

static bool match_not_render(const struct xe_gt *gt,
			     const struct xe_hw_engine *hwe)
{
	return hwe->class != XE_ENGINE_CLASS_RENDER;
}

static const struct xe_rtp_entry_sr register_whitelist[] = {
	{ XE_RTP_NAME("WaAllowPMDepthAndInvocationCountAccessFromUMD, 1408556865"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(WHITELIST(PS_INVOCATION_COUNT,
				   RING_FORCE_TO_NONPRIV_ACCESS_RD |
				   RING_FORCE_TO_NONPRIV_RANGE_4))
	},
	{ XE_RTP_NAME("1508744258, 14012131227, 1808121037"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(WHITELIST(COMMON_SLICE_CHICKEN1, 0))
	},
	{ XE_RTP_NAME("1806527549"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(WHITELIST(HIZ_CHICKEN, 0))
	},
	{ XE_RTP_NAME("allow_read_ctx_timestamp"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1260), FUNC(match_not_render)),
	  XE_RTP_ACTIONS(WHITELIST(RING_CTX_TIMESTAMP(0),
				RING_FORCE_TO_NONPRIV_ACCESS_RD,
				XE_RTP_ACTION_FLAG(ENGINE_BASE)))
	},
	{ XE_RTP_NAME("16014440446"),
	  XE_RTP_RULES(PLATFORM(PVC)),
	  XE_RTP_ACTIONS(WHITELIST(XE_REG(0x4400),
				   RING_FORCE_TO_NONPRIV_DENY |
				   RING_FORCE_TO_NONPRIV_RANGE_64),
			 WHITELIST(XE_REG(0x4500),
				   RING_FORCE_TO_NONPRIV_DENY |
				   RING_FORCE_TO_NONPRIV_RANGE_64))
	},
	{ XE_RTP_NAME("16017236439"),
	  XE_RTP_RULES(PLATFORM(PVC), ENGINE_CLASS(COPY)),
	  XE_RTP_ACTIONS(WHITELIST(BCS_SWCTRL(0),
				   RING_FORCE_TO_NONPRIV_DENY,
				   XE_RTP_ACTION_FLAG(ENGINE_BASE)))
	},
	{ XE_RTP_NAME("16020183090"),
	  XE_RTP_RULES(GRAPHICS_VERSION(2004), GRAPHICS_STEP(A0, B0),
		       ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(WHITELIST(CSBE_DEBUG_STATUS(RENDER_RING_BASE), 0))
	},
	{ XE_RTP_NAME("oa_reg_render"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, XE_RTP_END_VERSION_UNDEFINED),
		       ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(WHITELIST(OAG_MMIOTRIGGER,
				   RING_FORCE_TO_NONPRIV_ACCESS_RW),
			 WHITELIST(OAG_OASTATUS,
				   RING_FORCE_TO_NONPRIV_ACCESS_RD),
			 WHITELIST(OAG_OAHEADPTR,
				   RING_FORCE_TO_NONPRIV_ACCESS_RD |
				   RING_FORCE_TO_NONPRIV_RANGE_4))
	},
	{ XE_RTP_NAME("oa_reg_compute"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, XE_RTP_END_VERSION_UNDEFINED),
		       ENGINE_CLASS(COMPUTE)),
	  XE_RTP_ACTIONS(WHITELIST(OAG_MMIOTRIGGER,
				   RING_FORCE_TO_NONPRIV_ACCESS_RW),
			 WHITELIST(OAG_OASTATUS,
				   RING_FORCE_TO_NONPRIV_ACCESS_RD),
			 WHITELIST(OAG_OAHEADPTR,
				   RING_FORCE_TO_NONPRIV_ACCESS_RD |
				   RING_FORCE_TO_NONPRIV_RANGE_4))
	},
	{}
};

static void whitelist_apply_to_hwe(struct xe_hw_engine *hwe)
{
	struct xe_reg_sr *sr = &hwe->reg_whitelist;
	struct xe_reg_sr_entry *entry;
	struct drm_printer p;
	unsigned long reg;
	unsigned int slot;

	xe_gt_dbg(hwe->gt, "Add %s whitelist to engine\n", sr->name);
	p = xe_gt_dbg_printer(hwe->gt);

	slot = 0;
	xa_for_each(&sr->xa, reg, entry) {
		struct xe_reg_sr_entry hwe_entry = {
			.reg = RING_FORCE_TO_NONPRIV(hwe->mmio_base, slot),
			.set_bits = entry->reg.addr | entry->set_bits,
			.clr_bits = ~0u,
			.read_mask = entry->read_mask,
		};

		if (slot == RING_MAX_NONPRIV_SLOTS) {
			xe_gt_err(hwe->gt,
				  "hwe %s: maximum register whitelist slots (%d) reached, refusing to add more\n",
				  hwe->name, RING_MAX_NONPRIV_SLOTS);
			break;
		}

		xe_reg_whitelist_print_entry(&p, 0, reg, entry);
		xe_reg_sr_add(&hwe->reg_sr, &hwe_entry, hwe->gt);

		slot++;
	}
}

/**
 * xe_reg_whitelist_process_engine - process table of registers to whitelist
 * @hwe: engine instance to process whitelist for
 *
 * Process wwhitelist table for this platform, saving in @hwe all the
 * registers that need to be whitelisted by the hardware so they can be accessed
 * by userspace.
 */
void xe_reg_whitelist_process_engine(struct xe_hw_engine *hwe)
{
	struct xe_rtp_process_ctx ctx = XE_RTP_PROCESS_CTX_INITIALIZER(hwe);

	xe_rtp_process_to_sr(&ctx, register_whitelist, &hwe->reg_whitelist);
	whitelist_apply_to_hwe(hwe);
}

/**
 * xe_reg_whitelist_print_entry - print one whitelist entry
 * @p: DRM printer
 * @indent: indent level
 * @reg: register allowed/denied
 * @entry: save-restore entry
 *
 * Print details about the entry added to allow/deny access
 */
void xe_reg_whitelist_print_entry(struct drm_printer *p, unsigned int indent,
				  u32 reg, struct xe_reg_sr_entry *entry)
{
	u32 val = entry->set_bits;
	const char *access_str = "(invalid)";
	unsigned int range_bit = 2;
	u32 range_start, range_end;
	bool deny;

	deny = val & RING_FORCE_TO_NONPRIV_DENY;

	switch (val & RING_FORCE_TO_NONPRIV_RANGE_MASK) {
	case RING_FORCE_TO_NONPRIV_RANGE_4:
		range_bit = 4;
		break;
	case RING_FORCE_TO_NONPRIV_RANGE_16:
		range_bit = 6;
		break;
	case RING_FORCE_TO_NONPRIV_RANGE_64:
		range_bit = 8;
		break;
	}

	range_start = reg & REG_GENMASK(25, range_bit);
	range_end = range_start | REG_GENMASK(range_bit, 0);

	switch (val & RING_FORCE_TO_NONPRIV_ACCESS_MASK) {
	case RING_FORCE_TO_NONPRIV_ACCESS_RW:
		access_str = "rw";
		break;
	case RING_FORCE_TO_NONPRIV_ACCESS_RD:
		access_str = "read";
		break;
	case RING_FORCE_TO_NONPRIV_ACCESS_WR:
		access_str = "write";
		break;
	}

	drm_printf_indent(p, indent, "REG[0x%x-0x%x]: %s %s access\n",
			  range_start, range_end,
			  deny ? "deny" : "allow",
			  access_str);
}

/**
 * xe_reg_whitelist_dump - print all whitelist entries
 * @sr: Save/restore entries
 * @p: DRM printer
 */
void xe_reg_whitelist_dump(struct xe_reg_sr *sr, struct drm_printer *p)
{
	struct xe_reg_sr_entry *entry;
	unsigned long reg;

	if (!sr->name || xa_empty(&sr->xa))
		return;

	drm_printf(p, "%s\n", sr->name);
	xa_for_each(&sr->xa, reg, entry)
		xe_reg_whitelist_print_entry(p, 1, reg, entry);
}
