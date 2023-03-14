// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_wa.h"

#include <linux/compiler_types.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_hw_engine_types.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_rtp.h"
#include "xe_step.h"

/**
 * DOC: Hardware workarounds
 *
 * Hardware workarounds are register programming documented to be executed in
 * the driver that fall outside of the normal programming sequences for a
 * platform. There are some basic categories of workarounds, depending on
 * how/when they are applied:
 *
 * - LRC workarounds: workarounds that touch registers that are
 *   saved/restored to/from the HW context image. The list is emitted (via Load
 *   Register Immediate commands) once when initializing the device and saved in
 *   the default context. That default context is then used on every context
 *   creation to have a "primed golden context", i.e. a context image that
 *   already contains the changes needed to all the registers.
 *
 * - Engine workarounds: the list of these WAs is applied whenever the specific
 *   engine is reset. It's also possible that a set of engine classes share a
 *   common power domain and they are reset together. This happens on some
 *   platforms with render and compute engines. In this case (at least) one of
 *   them need to keeep the workaround programming: the approach taken in the
 *   driver is to tie those workarounds to the first compute/render engine that
 *   is registered.  When executing with GuC submission, engine resets are
 *   outside of kernel driver control, hence the list of registers involved in
 *   written once, on engine initialization, and then passed to GuC, that
 *   saves/restores their values before/after the reset takes place. See
 *   ``drivers/gpu/drm/xe/xe_guc_ads.c`` for reference.
 *
 * - GT workarounds: the list of these WAs is applied whenever these registers
 *   revert to their default values: on GPU reset, suspend/resume [1]_, etc.
 *
 * - Register whitelist: some workarounds need to be implemented in userspace,
 *   but need to touch privileged registers. The whitelist in the kernel
 *   instructs the hardware to allow the access to happen. From the kernel side,
 *   this is just a special case of a MMIO workaround (as we write the list of
 *   these to/be-whitelisted registers to some special HW registers).
 *
 * - Workaround batchbuffers: buffers that get executed automatically by the
 *   hardware on every HW context restore. These buffers are created and
 *   programmed in the default context so the hardware always go through those
 *   programming sequences when switching contexts. The support for workaround
 *   batchbuffers is enabled these hardware mechanisms:
 *
 *   #. INDIRECT_CTX: A batchbuffer and an offset are provided in the default
 *      context, pointing the hardware to jump to that location when that offset
 *      is reached in the context restore. Workaround batchbuffer in the driver
 *      currently uses this mechanism for all platforms.
 *
 *   #. BB_PER_CTX_PTR: A batchbuffer is provided in the default context,
 *      pointing the hardware to a buffer to continue executing after the
 *      engine registers are restored in a context restore sequence. This is
 *      currently not used in the driver.
 *
 * - Other:  There are WAs that, due to their nature, cannot be applied from a
 *   central place. Those are peppered around the rest of the code, as needed.
 *   Workarounds related to the display IP are the main example.
 *
 * .. [1] Technically, some registers are powercontext saved & restored, so they
 *    survive a suspend/resume. In practice, writing them again is not too
 *    costly and simplifies things, so it's the approach taken in the driver.
 *
 * .. note::
 *    Hardware workarounds in xe work the same way as in i915, with the
 *    difference of how they are maintained in the code. In xe it uses the
 *    xe_rtp infrastructure so the workarounds can be kept in tables, following
 *    a more declarative approach rather than procedural.
 */

#undef _MMIO
#undef MCR_REG
#define _MMIO(x)	_XE_RTP_REG(x)
#define MCR_REG(x)	_XE_RTP_MCR_REG(x)

static const struct xe_rtp_entry gt_was[] = {
	{ XE_RTP_NAME("14011060649"),
	  XE_RTP_RULES(MEDIA_VERSION_RANGE(1200, 1255),
		       ENGINE_CLASS(VIDEO_DECODE),
		       FUNC(xe_rtp_match_even_instance)),
	  XE_RTP_ACTIONS(SET(VDBOX_CGCTL3F10(0), IECPUNIT_CLKGATE_DIS)),
	  XE_RTP_ENTRY_FLAG(FOREACH_ENGINE),
	},
	{ XE_RTP_NAME("14011059788"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210)),
	  XE_RTP_ACTIONS(SET(GEN10_DFR_RATIO_EN_AND_CHICKEN, DFR_DISABLE))
	},

	/* DG1 */

	{ XE_RTP_NAME("1409420604"),
	  XE_RTP_RULES(PLATFORM(DG1)),
	  XE_RTP_ACTIONS(SET(SUBSLICE_UNIT_LEVEL_CLKGATE2, CPSSUNIT_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("1408615072"),
	  XE_RTP_RULES(PLATFORM(DG1)),
	  XE_RTP_ACTIONS(SET(UNSLICE_UNIT_LEVEL_CLKGATE2, VSUNIT_CLKGATE_DIS_TGL))
	},

	/* DG2 */

	{ XE_RTP_NAME("16010515920"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10),
		       STEP(A0, B0),
		       ENGINE_CLASS(VIDEO_DECODE)),
	  XE_RTP_ACTIONS(SET(VDBOX_CGCTL3F18(0), ALNUNIT_CLKGATE_DIS)),
	  XE_RTP_ENTRY_FLAG(FOREACH_ENGINE),
	},
	{ XE_RTP_NAME("22010523718"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10)),
	  XE_RTP_ACTIONS(SET(UNSLICE_UNIT_LEVEL_CLKGATE, CG3DDISCFEG_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14011006942"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10)),
	  XE_RTP_ACTIONS(SET(GEN11_SUBSLICE_UNIT_LEVEL_CLKGATE, DSS_ROUTER_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14010948348"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(UNSLCGCTL9430, MSQDUNIT_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14011037102"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(UNSLCGCTL9444, LTCDD_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14011371254"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(GEN11_SLICE_UNIT_LEVEL_CLKGATE, NODEDSS_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14011431319"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(UNSLCGCTL9440,
			     GAMTLBOACS_CLKGATE_DIS |
			     GAMTLBVDBOX7_CLKGATE_DIS | GAMTLBVDBOX6_CLKGATE_DIS |
			     GAMTLBVDBOX5_CLKGATE_DIS | GAMTLBVDBOX4_CLKGATE_DIS |
			     GAMTLBVDBOX3_CLKGATE_DIS | GAMTLBVDBOX2_CLKGATE_DIS |
			     GAMTLBVDBOX1_CLKGATE_DIS | GAMTLBVDBOX0_CLKGATE_DIS |
			     GAMTLBKCR_CLKGATE_DIS | GAMTLBGUC_CLKGATE_DIS |
			     GAMTLBBLT_CLKGATE_DIS),
			 SET(UNSLCGCTL9444,
			     GAMTLBGFXA0_CLKGATE_DIS | GAMTLBGFXA1_CLKGATE_DIS |
			     GAMTLBCOMPA0_CLKGATE_DIS | GAMTLBCOMPA1_CLKGATE_DIS |
			     GAMTLBCOMPB0_CLKGATE_DIS | GAMTLBCOMPB1_CLKGATE_DIS |
			     GAMTLBCOMPC0_CLKGATE_DIS | GAMTLBCOMPC1_CLKGATE_DIS |
			     GAMTLBCOMPD0_CLKGATE_DIS | GAMTLBCOMPD1_CLKGATE_DIS |
			     GAMTLBMERT_CLKGATE_DIS |
			     GAMTLBVEBOX3_CLKGATE_DIS | GAMTLBVEBOX2_CLKGATE_DIS |
			     GAMTLBVEBOX1_CLKGATE_DIS | GAMTLBVEBOX0_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14010569222"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(UNSLICE_UNIT_LEVEL_CLKGATE, GAMEDIA_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14011028019"),
	  XE_RTP_RULES(SUBPLATFORM(DG2, G10), STEP(A0, B0)),
	  XE_RTP_ACTIONS(SET(SSMCGCTL9530, RTFUNIT_CLKGATE_DIS))
	},
	{ XE_RTP_NAME("14014830051"),
	  XE_RTP_RULES(PLATFORM(DG2)),
	  XE_RTP_ACTIONS(CLR(SARB_CHICKEN1, COMP_CKN_IN))
	},
	{ XE_RTP_NAME("14015795083"),
	  XE_RTP_RULES(PLATFORM(DG2)),
	  XE_RTP_ACTIONS(CLR(GEN7_MISCCPCTL, GEN12_DOP_CLOCK_GATE_RENDER_ENABLE))
	},
	{}
};

static const struct xe_rtp_entry engine_was[] = {
	{ XE_RTP_NAME("22010931296, 18011464164, 14010919138"),
	  XE_RTP_RULES(GRAPHICS_VERSION(1200), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN7_FF_THREAD_MODE,
			     GEN12_FF_TESSELATION_DOP_GATE_DISABLE))
	},
	{ XE_RTP_NAME("1409804808"),
	  XE_RTP_RULES(GRAPHICS_VERSION(1200),
		       ENGINE_CLASS(RENDER),
		       IS_INTEGRATED),
	  XE_RTP_ACTIONS(SET(GEN8_ROW_CHICKEN2, GEN12_PUSH_CONST_DEREF_HOLD_DIS,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("14010229206, 1409085225"),
	  XE_RTP_RULES(GRAPHICS_VERSION(1200),
		       ENGINE_CLASS(RENDER),
		       IS_INTEGRATED),
	  XE_RTP_ACTIONS(SET(GEN9_ROW_CHICKEN4, GEN12_DISABLE_TDL_PUSH,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("1606931601"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN8_ROW_CHICKEN2, GEN12_DISABLE_EARLY_READ,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("14010826681, 1606700617, 22010271021"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN9_CS_DEBUG_MODE1, FF_DOP_CLOCK_GATE_DISABLE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("1406941453"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN10_SAMPLER_MODE, ENABLE_SMALLPL,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("FtrPerCtxtPreemptionGranularityControl"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1250), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN7_FF_SLICE_CS_CHICKEN1,
			     GEN9_FFSC_PERCTX_PREEMPT_CTRL,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},

	/* TGL */

	{ XE_RTP_NAME("1607297627, 1607030317, 1607186500"),
	  XE_RTP_RULES(PLATFORM(TIGERLAKE), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(RING_PSMI_CTL(RENDER_RING_BASE),
			     GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE |
			     GEN8_RC_SEMA_IDLE_MSG_DISABLE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},

	/* RKL */

	{ XE_RTP_NAME("1607297627, 1607030317, 1607186500"),
	  XE_RTP_RULES(PLATFORM(ROCKETLAKE), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(RING_PSMI_CTL(RENDER_RING_BASE),
			     GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE |
			     GEN8_RC_SEMA_IDLE_MSG_DISABLE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},

	/* DG2 */

	{ XE_RTP_NAME("14015227452"),
	  XE_RTP_RULES(PLATFORM(DG2), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN9_ROW_CHICKEN4, XEHP_DIS_BBL_SYSPIPE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("18019627453"),
	  XE_RTP_RULES(PLATFORM(DG2), ENGINE_CLASS(RENDER)),
	  XE_RTP_ACTIONS(SET(GEN9_CS_DEBUG_MODE1, FF_DOP_CLOCK_GATE_DISABLE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{}
};

static const struct xe_rtp_entry lrc_was[] = {
	{ XE_RTP_NAME("1409342910, 14010698770, 14010443199, 1408979724, 1409178076, 1409207793, 1409217633, 1409252684, 1409347922, 1409142259"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210)),
	  XE_RTP_ACTIONS(SET(GEN11_COMMON_SLICE_CHICKEN3,
			     GEN12_DISABLE_CPS_AWARE_COLOR_PIPE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("WaDisableGPGPUMidThreadPreemption"),
	  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, 1210)),
	  XE_RTP_ACTIONS(FIELD_SET(GEN8_CS_CHICKEN1,
				   GEN9_PREEMPT_GPGPU_LEVEL_MASK,
				   GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL,
				   XE_RTP_ACTION_FLAG(MASKED_REG)))
	},

	/* DG1 */

	{ XE_RTP_NAME("1409044764"),
	  XE_RTP_RULES(PLATFORM(DG1)),
	  XE_RTP_ACTIONS(CLR(GEN11_COMMON_SLICE_CHICKEN3,
			     DG1_FLOAT_POINT_BLEND_OPT_STRICT_MODE_EN,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{ XE_RTP_NAME("22010493298"),
	  XE_RTP_RULES(PLATFORM(DG1)),
	  XE_RTP_ACTIONS(SET(HIZ_CHICKEN,
			     DG1_HZ_READ_SUPPRESSION_OPTIMIZATION_DISABLE,
			     XE_RTP_ACTION_FLAG(MASKED_REG)))
	},
	{}
};

/**
 * xe_wa_process_gt - process GT workaround table
 * @gt: GT instance to process workarounds for
 *
 * Process GT workaround table for this platform, saving in @gt all the
 * workarounds that need to be applied at the GT level.
 */
void xe_wa_process_gt(struct xe_gt *gt)
{
	xe_rtp_process(gt_was, &gt->reg_sr, gt, NULL);
}

/**
 * xe_wa_process_engine - process engine workaround table
 * @hwe: engine instance to process workarounds for
 *
 * Process engine workaround table for this platform, saving in @hwe all the
 * workarounds that need to be applied at the engine level that match this
 * engine.
 */
void xe_wa_process_engine(struct xe_hw_engine *hwe)
{
	xe_rtp_process(engine_was, &hwe->reg_sr, hwe->gt, hwe);
}

/**
 * xe_wa_process_lrc - process context workaround table
 * @hwe: engine instance to process workarounds for
 *
 * Process context workaround table for this platform, saving in @hwe all the
 * workarounds that need to be applied on context restore. These are workarounds
 * touching registers that are part of the HW context image.
 */
void xe_wa_process_lrc(struct xe_hw_engine *hwe)
{
	xe_rtp_process(lrc_was, &hwe->reg_lrc, hwe->gt, hwe);
}
