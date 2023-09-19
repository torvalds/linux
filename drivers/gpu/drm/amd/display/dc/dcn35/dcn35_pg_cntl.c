/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "reg_helper.h"
#include "core_types.h"
#include "dcn35_pg_cntl.h"
#include "dccg.h"

#define TO_DCN_PG_CNTL(pg_cntl)\
	container_of(pg_cntl, struct dcn_pg_cntl, base)

#define REG(reg) \
	(pg_cntl_dcn->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	pg_cntl_dcn->pg_cntl_shift->field_name, pg_cntl_dcn->pg_cntl_mask->field_name

#define CTX \
	pg_cntl_dcn->base.ctx
#define DC_LOGGER \
	pg_cntl->ctx->logger

static bool pg_cntl35_dsc_pg_status(struct pg_cntl *pg_cntl, unsigned int dsc_inst)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	if (pg_cntl->ctx->dc->debug.ignore_pg)
		return true;

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_GET(DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 1: /* DSC1 */
		REG_GET(DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 2: /* DSC2 */
		REG_GET(DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 3: /* DSC3 */
		REG_GET(DOMAIN19_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	return pwr_status == 0;
}

void pg_cntl35_dsc_pg_control(struct pg_cntl *pg_cntl, unsigned int dsc_inst, bool power_on)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;
	bool block_enabled;

	/*need to enable dscclk regardless DSC_PG*/
	if (pg_cntl->ctx->dc->res_pool->dccg->funcs->enable_dsc && power_on)
		pg_cntl->ctx->dc->res_pool->dccg->funcs->enable_dsc(
				pg_cntl->ctx->dc->res_pool->dccg, dsc_inst);

	if (pg_cntl->ctx->dc->debug.ignore_pg ||
		pg_cntl->ctx->dc->debug.disable_dsc_power_gate ||
		pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	block_enabled = pg_cntl35_dsc_pg_status(pg_cntl, dsc_inst);
	if (power_on) {
		if (block_enabled)
			return;
	} else {
		if (!block_enabled)
			return;
	}

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (dsc_inst < MAX_PIPES)
		pg_cntl->pg_pipe_res_enable[PG_DSC][dsc_inst] = power_on;

	if (pg_cntl->ctx->dc->res_pool->dccg->funcs->disable_dsc && !power_on) {
		/*this is to disable dscclk*/
		pg_cntl->ctx->dc->res_pool->dccg->funcs->disable_dsc(
			pg_cntl->ctx->dc->res_pool->dccg, dsc_inst);
	}
}

static bool pg_cntl35_hubp_dpp_pg_status(struct pg_cntl *pg_cntl, unsigned int hubp_dpp_inst)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	switch (hubp_dpp_inst) {
	case 0:
		/* DPP0 & HUBP0 */
		REG_GET(DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 1:
		/* DPP1 & HUBP1 */
		REG_GET(DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 2:
		/* DPP2 & HUBP2 */
		REG_GET(DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 3:
		/* DPP3 & HUBP3 */
		REG_GET(DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	return pwr_status == 0;
}

void pg_cntl35_hubp_dpp_pg_control(struct pg_cntl *pg_cntl, unsigned int hubp_dpp_inst, bool power_on)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl;
	bool block_enabled;

	if (pg_cntl->ctx->dc->debug.ignore_pg ||
		pg_cntl->ctx->dc->debug.disable_hubp_power_gate ||
		pg_cntl->ctx->dc->debug.disable_dpp_power_gate ||
		pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	block_enabled = pg_cntl35_hubp_dpp_pg_status(pg_cntl, hubp_dpp_inst);
	if (power_on) {
		if (block_enabled)
			return;
	} else {
		if (!block_enabled)
			return;
	}

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (hubp_dpp_inst) {
	case 0:
		/* DPP0 & HUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 1:
		/* DPP1 & HUBP1 */
		REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 2:
		/* DPP2 & HUBP2 */
		REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 3:
		/* DPP3 & HUBP3 */
		REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	DC_LOG_DEBUG("HUBP DPP instance %d, power %s", hubp_dpp_inst,
		power_on ? "ON" : "OFF");

	if (hubp_dpp_inst < MAX_PIPES) {
		pg_cntl->pg_pipe_res_enable[PG_HUBP][hubp_dpp_inst] = power_on;
		pg_cntl->pg_pipe_res_enable[PG_DPP][hubp_dpp_inst] = power_on;
	}
}

static bool pg_cntl35_hpo_pg_status(struct pg_cntl *pg_cntl)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	REG_GET(DOMAIN25_PG_STATUS,
			DOMAIN_PGFSM_PWR_STATUS, &pwr_status);

	return pwr_status == 0;
}

void pg_cntl35_hpo_pg_control(struct pg_cntl *pg_cntl, bool power_on)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl;
	bool block_enabled;

	if (pg_cntl->ctx->dc->debug.ignore_pg ||
		pg_cntl->ctx->dc->debug.disable_hpo_power_gate ||
		pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	block_enabled = pg_cntl35_hpo_pg_status(pg_cntl);
	if (power_on) {
		if (block_enabled)
			return;
	} else {
		if (!block_enabled)
			return;
	}

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	REG_UPDATE(DOMAIN25_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
	REG_WAIT(DOMAIN25_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);

	pg_cntl->pg_res_enable[PG_HPO] = power_on;
}

static bool pg_cntl35_io_clk_status(struct pg_cntl *pg_cntl)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	REG_GET(DOMAIN22_PG_STATUS,
		DOMAIN_PGFSM_PWR_STATUS, &pwr_status);

	return pwr_status == 0;
}

void pg_cntl35_io_clk_pg_control(struct pg_cntl *pg_cntl, bool power_on)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl;
	bool block_enabled;

	if (pg_cntl->ctx->dc->debug.ignore_pg ||
		pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	block_enabled = pg_cntl35_io_clk_status(pg_cntl);
	if (power_on) {
		if (block_enabled)
			return;
	} else {
		if (!block_enabled)
			return;
	}

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	/* DCCG, DIO, DCIO */
	REG_UPDATE(DOMAIN22_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
	REG_WAIT(DOMAIN22_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);

	pg_cntl->pg_res_enable[PG_DCCG] = power_on;
	pg_cntl->pg_res_enable[PG_DIO] = power_on;
	pg_cntl->pg_res_enable[PG_DCIO] = power_on;
}

static bool pg_cntl35_plane_otg_status(struct pg_cntl *pg_cntl)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	REG_GET(DOMAIN24_PG_STATUS,
		DOMAIN_PGFSM_PWR_STATUS, &pwr_status);

	return pwr_status == 0;
}

void pg_cntl35_mpcc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int mpcc_inst, bool power_on)
{
	if (pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	if (mpcc_inst >= 0 && mpcc_inst < MAX_PIPES)
		pg_cntl->pg_pipe_res_enable[PG_MPCC][mpcc_inst] = power_on;
}

void pg_cntl35_opp_pg_control(struct pg_cntl *pg_cntl,
	unsigned int opp_inst, bool power_on)
{
	if (pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	if (opp_inst >= 0 && opp_inst < MAX_PIPES)
		pg_cntl->pg_pipe_res_enable[PG_OPP][opp_inst] = power_on;
}

void pg_cntl35_optc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int optc_inst, bool power_on)
{
	if (pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	if (optc_inst >= 0 && optc_inst < MAX_PIPES)
		pg_cntl->pg_pipe_res_enable[PG_OPTC][optc_inst] = power_on;
}

void pg_cntl35_plane_otg_pg_control(struct pg_cntl *pg_cntl, bool power_on)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl;
	int i;
	bool block_enabled;
	bool all_mpcc_disabled = true, all_opp_disabled = true;
	bool all_optc_disabled = true, all_stream_disabled = true;

	if (pg_cntl->ctx->dc->debug.ignore_pg ||
		pg_cntl->ctx->dc->debug.disable_optc_power_gate ||
		pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	block_enabled = pg_cntl35_plane_otg_status(pg_cntl);
	if (power_on) {
		if (block_enabled)
			return;
	} else {
		if (!block_enabled)
			return;
	}

	for (i = 0; i < pg_cntl->ctx->dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &pg_cntl->ctx->dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe_ctx) {
			if (pipe_ctx->stream)
				all_stream_disabled = false;
		}

		if (pg_cntl->pg_pipe_res_enable[PG_MPCC][i])
			all_mpcc_disabled = false;

		if (pg_cntl->pg_pipe_res_enable[PG_OPP][i])
			all_opp_disabled = false;

		if (pg_cntl->pg_pipe_res_enable[PG_OPTC][i])
			all_optc_disabled = false;
	}

	if (!power_on) {
		if (!all_mpcc_disabled || !all_opp_disabled || !all_optc_disabled
			|| !all_stream_disabled || pg_cntl->pg_res_enable[PG_DWB])
			return;
	}

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	/* MPC, OPP, OPTC, DWB */
	REG_UPDATE(DOMAIN24_PG_CONFIG, DOMAIN_POWER_GATE, power_gate);
	REG_WAIT(DOMAIN24_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);

	for (i = 0; i < pg_cntl->ctx->dc->res_pool->pipe_count; i++) {
		pg_cntl->pg_pipe_res_enable[PG_MPCC][i] = power_on;
		pg_cntl->pg_pipe_res_enable[PG_OPP][i] = power_on;
		pg_cntl->pg_pipe_res_enable[PG_OPTC][i] = power_on;
	}
	pg_cntl->pg_res_enable[PG_DWB] = power_on;
}

void pg_cntl35_dwb_pg_control(struct pg_cntl *pg_cntl, bool power_on)
{
	if (pg_cntl->ctx->dc->idle_optimizations_allowed)
		return;

	pg_cntl->pg_res_enable[PG_DWB] = power_on;
}

static bool pg_cntl35_mem_status(struct pg_cntl *pg_cntl)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(pg_cntl);
	uint32_t pwr_status = 0;

	REG_GET(DOMAIN23_PG_STATUS,
		DOMAIN_PGFSM_PWR_STATUS, &pwr_status);

	return pwr_status == 0;
}

void pg_cntl35_init_pg_status(struct pg_cntl *pg_cntl)
{
	int i = 0;
	bool block_enabled;

	pg_cntl->pg_res_enable[PG_HPO] = pg_cntl35_hpo_pg_status(pg_cntl);

	block_enabled = pg_cntl35_io_clk_status(pg_cntl);
	pg_cntl->pg_res_enable[PG_DCCG] = block_enabled;
	pg_cntl->pg_res_enable[PG_DIO] = block_enabled;
	pg_cntl->pg_res_enable[PG_DCIO] = block_enabled;

	block_enabled = pg_cntl35_mem_status(pg_cntl);
	pg_cntl->pg_res_enable[PG_DCHUBBUB] = block_enabled;
	pg_cntl->pg_res_enable[PG_DCHVM] = block_enabled;

	for (i = 0; i < pg_cntl->ctx->dc->res_pool->pipe_count; i++) {
		block_enabled = pg_cntl35_hubp_dpp_pg_status(pg_cntl, i);
		pg_cntl->pg_pipe_res_enable[PG_HUBP][i] = block_enabled;
		pg_cntl->pg_pipe_res_enable[PG_DPP][i] = block_enabled;

		block_enabled = pg_cntl35_dsc_pg_status(pg_cntl, i);
		pg_cntl->pg_pipe_res_enable[PG_DSC][i] = block_enabled;
	}

	block_enabled = pg_cntl35_plane_otg_status(pg_cntl);
	for (i = 0; i < pg_cntl->ctx->dc->res_pool->pipe_count; i++) {
		pg_cntl->pg_pipe_res_enable[PG_MPCC][i] = block_enabled;
		pg_cntl->pg_pipe_res_enable[PG_OPP][i] = block_enabled;
		pg_cntl->pg_pipe_res_enable[PG_OPTC][i] = block_enabled;
	}
	pg_cntl->pg_res_enable[PG_DWB] = block_enabled;
}

static const struct pg_cntl_funcs pg_cntl35_funcs = {
	.init_pg_status = pg_cntl35_init_pg_status,
	.dsc_pg_control = pg_cntl35_dsc_pg_control,
	.hubp_dpp_pg_control = pg_cntl35_hubp_dpp_pg_control,
	.hpo_pg_control = pg_cntl35_hpo_pg_control,
	.io_clk_pg_control = pg_cntl35_io_clk_pg_control,
	.plane_otg_pg_control = pg_cntl35_plane_otg_pg_control,
	.mpcc_pg_control = pg_cntl35_mpcc_pg_control,
	.opp_pg_control = pg_cntl35_opp_pg_control,
	.optc_pg_control = pg_cntl35_optc_pg_control,
	.dwb_pg_control = pg_cntl35_dwb_pg_control
};

struct pg_cntl *pg_cntl35_create(
	struct dc_context *ctx,
	const struct pg_cntl_registers *regs,
	const struct pg_cntl_shift *pg_cntl_shift,
	const struct pg_cntl_mask *pg_cntl_mask)
{
	struct dcn_pg_cntl *pg_cntl_dcn = kzalloc(sizeof(*pg_cntl_dcn), GFP_KERNEL);
	struct pg_cntl *base;

	if (pg_cntl_dcn == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	base = &pg_cntl_dcn->base;
	base->ctx = ctx;
	base->funcs = &pg_cntl35_funcs;

	pg_cntl_dcn->regs = regs;
	pg_cntl_dcn->pg_cntl_shift = pg_cntl_shift;
	pg_cntl_dcn->pg_cntl_mask = pg_cntl_mask;

	memset(base->pg_pipe_res_enable, 0, PG_HW_PIPE_RESOURCES_NUM_ELEMENT * MAX_PIPES * sizeof(bool));
	memset(base->pg_res_enable, 0, PG_HW_RESOURCES_NUM_ELEMENT * sizeof(bool));

	return &pg_cntl_dcn->base;
}

void dcn_pg_cntl_destroy(struct pg_cntl **pg_cntl)
{
	struct dcn_pg_cntl *pg_cntl_dcn = TO_DCN_PG_CNTL(*pg_cntl);

	kfree(pg_cntl_dcn);
	*pg_cntl = NULL;
}
