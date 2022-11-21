// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#include "dcn303_hwseq.h"

#include "dce/dce_hwseq.h"

#include "reg_helper.h"
#include "dc.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name


void dcn303_dpp_pg_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool power_on)
{
	/*DCN303 removes PG registers*/
}

void dcn303_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on)
{
	/*DCN303 removes PG registers*/
}

void dcn303_dsc_pg_control(struct dce_hwseq *hws, unsigned int dsc_inst, bool power_on)
{
	/*DCN303 removes PG registers*/
}

void dcn303_enable_power_gating_plane(struct dce_hwseq *hws, bool enable)
{
	/*DCN303 removes PG registers*/
}
