// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#include "dcn303_hwseq.h"
#include "dcn30/dcn30_init.h"
#include "dc.h"

#include "dcn303_init.h"

void dcn303_hw_sequencer_construct(struct dc *dc)
{
	dcn30_hw_sequencer_construct(dc);

	dc->hwseq->funcs.dpp_pg_control = dcn303_dpp_pg_control;
	dc->hwseq->funcs.hubp_pg_control = dcn303_hubp_pg_control;
	dc->hwseq->funcs.dsc_pg_control = dcn303_dsc_pg_control;
	dc->hwseq->funcs.enable_power_gating_plane = dcn303_enable_power_gating_plane;
}
