/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dcn302_hwseq.h"

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


void dcn302_dpp_pg_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_dpp_power_gate)
		return;
	if (REG(DOMAIN1_PG_CONFIG) == 0)
		return;

	switch (dpp_inst) {
	case 0: /* DPP0 */
		REG_UPDATE(DOMAIN1_PG_CONFIG,
				DOMAIN1_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN1_PG_STATUS,
				DOMAIN1_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DPP1 */
		REG_UPDATE(DOMAIN3_PG_CONFIG,
				DOMAIN3_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN3_PG_STATUS,
				DOMAIN3_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DPP2 */
		REG_UPDATE(DOMAIN5_PG_CONFIG,
				DOMAIN5_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN5_PG_STATUS,
				DOMAIN5_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DPP3 */
		REG_UPDATE(DOMAIN7_PG_CONFIG,
				DOMAIN7_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN7_PG_STATUS,
				DOMAIN7_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DPP4 */
		/*
		 * Do not power gate DPP4, should be left at HW default, power on permanently.
		 * PG on Pipe4 is De-featured, attempting to put it to PG state may result in hard
		 * reset.
		 * REG_UPDATE(DOMAIN9_PG_CONFIG,
		 *		DOMAIN9_POWER_GATE, power_gate);
		 *
		 * REG_WAIT(DOMAIN9_PG_STATUS,
		 *		DOMAIN9_PGFSM_PWR_STATUS, pwr_status,
		 *		1, 1000);
		 */
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void dcn302_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;
	if (REG(DOMAIN0_PG_CONFIG) == 0)
		return;

	switch (hubp_inst) {
	case 0: /* DCHUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG,
				DOMAIN0_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN0_PG_STATUS,
				DOMAIN0_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DCHUBP1 */
		REG_UPDATE(DOMAIN2_PG_CONFIG,
				DOMAIN2_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN2_PG_STATUS,
				DOMAIN2_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DCHUBP2 */
		REG_UPDATE(DOMAIN4_PG_CONFIG,
				DOMAIN4_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN4_PG_STATUS,
				DOMAIN4_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DCHUBP3 */
		REG_UPDATE(DOMAIN6_PG_CONFIG,
				DOMAIN6_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN6_PG_STATUS,
				DOMAIN6_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DCHUBP4 */
		/*
		 * Do not power gate DCHUB4, should be left at HW default, power on permanently.
		 * PG on Pipe4 is De-featured, attempting to put it to PG state may result in hard
		 * reset.
		 * REG_UPDATE(DOMAIN8_PG_CONFIG,
		 *		DOMAIN8_POWER_GATE, power_gate);
		 *
		 * REG_WAIT(DOMAIN8_PG_STATUS,
		 *		DOMAIN8_PGFSM_PWR_STATUS, pwr_status,
		 *		1, 1000);
		 */
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void dcn302_dsc_pg_control(struct dce_hwseq *hws, unsigned int dsc_inst, bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_dsc_power_gate)
		return;

	if (REG(DOMAIN16_PG_CONFIG) == 0)
		return;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN16_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN16_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN17_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN17_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN18_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN18_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN19_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
				DOMAIN19_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DSC4 */
		REG_UPDATE(DOMAIN20_PG_CONFIG,
				DOMAIN20_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN20_PG_STATUS,
				DOMAIN20_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}
