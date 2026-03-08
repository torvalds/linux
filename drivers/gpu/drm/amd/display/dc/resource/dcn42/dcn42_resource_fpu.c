// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.



#include "dc.h"
#include "dcn42_resource_fpu.h"

#define DC_LOGGER_INIT(logger)


void dcn42_decide_zstate_support(struct dc *dc, struct dc_state *context)
{
	enum dcn_zstate_support_state support = DCN_ZSTATE_SUPPORT_DISALLOW;
	unsigned int i, plane_count = 0;

	DC_LOGGER_INIT(dc->ctx->logger);

	dc_assert_fp_enabled();
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].plane_state)
			plane_count++;
	}
	/*dcn42 has no z10*/
	if (context->stream_count == 0 || plane_count == 0) {
		support = DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY;
	} else if (context->stream_count == 1 && context->streams[0]->signal == SIGNAL_TYPE_EDP) {
		struct dc_link *link = context->streams[0]->sink->link;
		bool is_psr = (link && (link->psr_settings.psr_version == DC_PSR_VERSION_1 ||
								link->psr_settings.psr_version == DC_PSR_VERSION_SU_1) && !link->panel_config.psr.disable_psr);
		bool is_replay = link && link->replay_settings.replay_feature_enabled;

		if (is_psr || is_replay)
			support = DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY;
		else {
			/*here we allow z8 for eDP based on dml21 output*/
			support = context->bw_ctx.bw.dcn.clk.zstate_support ? DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY : DCN_ZSTATE_SUPPORT_DISALLOW;
		}

		DC_LOG_SMU("zstate_support: %d, StutterPeriod: %d\n, z8_stutter_efficiency: %d\n",
			 support, (int)context->bw_ctx.bw.dcn.clk.stutter_efficiency.z8_stutter_period,
			 (int)context->bw_ctx.bw.dcn.clk.stutter_efficiency.z8_stutter_efficiency);
	}
	context->bw_ctx.bw.dcn.clk.zstate_support = support;

}
