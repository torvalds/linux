// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_tuning.h"

#include "xe_gt_types.h"
#include "xe_platform_types.h"
#include "xe_rtp.h"

#include "gt/intel_gt_regs.h"

#undef _MMIO
#undef MCR_REG
#define _MMIO(x)	_XE_RTP_REG(x)
#define MCR_REG(x)	_XE_RTP_MCR_REG(x)

static const struct xe_rtp_entry gt_tunings[] = {
	{ XE_RTP_NAME("Tuning: 32B Access Enable"),
	  XE_RTP_RULES(PLATFORM(DG2)),
	  XE_RTP_ACTIONS(SET(XEHP_SQCM, EN_32B_ACCESS))
	},
	{}
};

static const struct xe_rtp_entry lrc_tunings[] = {
	{ XE_RTP_NAME("1604555607"),
	  XE_RTP_RULES(GRAPHICS_VERSION(1200)),
	  XE_RTP_ACTIONS(FIELD_SET_NO_READ_MASK(XEHP_FF_MODE2,
						FF_MODE2_TDS_TIMER_MASK,
						FF_MODE2_TDS_TIMER_128))
	},
	{}
};

void xe_tuning_process_gt(struct xe_gt *gt)
{
	xe_rtp_process(gt_tunings, &gt->reg_sr, gt, NULL);
}

/**
 * xe_tuning_process_lrc - process lrc tunings
 * @hwe: engine instance to process tunings for
 *
 * Process LRC table for this platform, saving in @hwe all the tunings that need
 * to be applied on context restore. These are tunings touching registers that
 * are part of the HW context image.
 */
void xe_tuning_process_lrc(struct xe_hw_engine *hwe)
{
	xe_rtp_process(lrc_tunings, &hwe->reg_lrc, hwe->gt, hwe);
}
