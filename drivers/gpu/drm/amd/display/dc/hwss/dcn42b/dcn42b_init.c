/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dcn20/dcn20_hwseq.h"
#include "dcn42/dcn42_init.h"
#include "dcn42b_hwseq.h"
#include "dcn42b_init.h"

void dcn42b_hw_sequencer_init_functions(struct dc *dc)
{
	/* Initialize with dcn42 functions first */
	dcn42_hw_sequencer_init_functions(dc);

	/* Override only init_pipes with dcn42b version */
	dc->hwseq->funcs.init_pipes = dcn42b_init_pipes;

}
