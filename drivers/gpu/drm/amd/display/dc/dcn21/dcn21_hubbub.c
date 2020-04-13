/*
* Copyright 2018 Advanced Micro Devices, Inc.
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
#include <linux/delay.h>
#include "dm_services.h"
#include "dcn20/dcn20_hubbub.h"
#include "dcn21_hubbub.h"
#include "reg_helper.h"

#define REG(reg)\
	hubbub1->regs->reg
#define DC_LOGGER \
	hubbub1->base.ctx->logger
#define CTX \
	hubbub1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

#define REG(reg)\
	hubbub1->regs->reg

#define CTX \
	hubbub1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

#ifdef NUM_VMID
#undef NUM_VMID
#endif
#define NUM_VMID 16

static uint32_t convert_and_clamp(
	uint32_t wm_ns,
	uint32_t refclk_mhz,
	uint32_t clamp_value)
{
	uint32_t ret_val = 0;
	ret_val = wm_ns * refclk_mhz;
	ret_val /= 1000;

	if (ret_val > clamp_value)
		ret_val = clamp_value;

	return ret_val;
}

void dcn21_dchvm_init(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t riommu_active;
	int i;

	//Init DCHVM block
	REG_UPDATE(DCHVM_CTRL0, HOSTVM_INIT_REQ, 1);

	//Poll until RIOMMU_ACTIVE = 1
	for (i = 0; i < 100; i++) {
		REG_GET(DCHVM_RIOMMU_STAT0, RIOMMU_ACTIVE, &riommu_active);

		if (riommu_active)
			break;
		else
			udelay(5);
	}

	if (riommu_active) {
		//Reflect the power status of DCHUBBUB
		REG_UPDATE(DCHVM_RIOMMU_CTRL0, HOSTVM_POWERSTATUS, 1);

		//Start rIOMMU prefetching
		REG_UPDATE(DCHVM_RIOMMU_CTRL0, HOSTVM_PREFETCH_REQ, 1);

		// Enable dynamic clock gating
		REG_UPDATE_4(DCHVM_CLK_CTRL,
						HVM_DISPCLK_R_GATE_DIS, 0,
						HVM_DISPCLK_G_GATE_DIS, 0,
						HVM_DCFCLK_R_GATE_DIS, 0,
						HVM_DCFCLK_G_GATE_DIS, 0);

		//Poll until HOSTVM_PREFETCH_DONE = 1
		REG_WAIT(DCHVM_RIOMMU_STAT0, HOSTVM_PREFETCH_DONE, 1, 5, 100);
	}
}

int hubbub21_init_dchub(struct hubbub *hubbub,
		struct dcn_hubbub_phys_addr_config *pa_config)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_vmid_page_table_config phys_config;

	REG_SET(DCN_VM_FB_LOCATION_BASE, 0,
			FB_BASE, pa_config->system_aperture.fb_base >> 24);
	REG_SET(DCN_VM_FB_LOCATION_TOP, 0,
			FB_TOP, pa_config->system_aperture.fb_top >> 24);
	REG_SET(DCN_VM_FB_OFFSET, 0,
			FB_OFFSET, pa_config->system_aperture.fb_offset >> 24);
	REG_SET(DCN_VM_AGP_BOT, 0,
			AGP_BOT, pa_config->system_aperture.agp_bot >> 24);
	REG_SET(DCN_VM_AGP_TOP, 0,
			AGP_TOP, pa_config->system_aperture.agp_top >> 24);
	REG_SET(DCN_VM_AGP_BASE, 0,
			AGP_BASE, pa_config->system_aperture.agp_base >> 24);

	if (pa_config->gart_config.page_table_start_addr != pa_config->gart_config.page_table_end_addr) {
		phys_config.page_table_start_addr = pa_config->gart_config.page_table_start_addr >> 12;
		phys_config.page_table_end_addr = pa_config->gart_config.page_table_end_addr >> 12;
		phys_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr | 1; //Note: hack
		phys_config.depth = 0;
		phys_config.block_size = 0;
		// Init VMID 0 based on PA config
		dcn20_vmid_setup(&hubbub1->vmid[0], &phys_config);
	}

	dcn21_dchvm_init(hubbub);

	return NUM_VMID;
}

bool hubbub21_program_urgent_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	if (safe_to_lower || watermarks->a.urgent_ns > hubbub1->watermarks.a.urgent_ns) {
		hubbub1->watermarks.a.urgent_ns = watermarks->a.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_URGENCY_WATERMARK_A, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.urgent_ns, prog_wm_value);
	} else if (watermarks->a.urgent_ns < hubbub1->watermarks.a.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->a.frac_urg_bw_flip
			> hubbub1->watermarks.a.frac_urg_bw_flip) {
		hubbub1->watermarks.a.frac_urg_bw_flip = watermarks->a.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, watermarks->a.frac_urg_bw_flip);
	} else if (watermarks->a.frac_urg_bw_flip
			< hubbub1->watermarks.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.frac_urg_bw_nom
			> hubbub1->watermarks.a.frac_urg_bw_nom) {
		hubbub1->watermarks.a.frac_urg_bw_nom = watermarks->a.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, watermarks->a.frac_urg_bw_nom);
	} else if (watermarks->a.frac_urg_bw_nom
			< hubbub1->watermarks.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.urgent_latency_ns > hubbub1->watermarks.a.urgent_latency_ns) {
		hubbub1->watermarks.a.urgent_latency_ns = watermarks->a.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.urgent_latency_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, prog_wm_value);
	} else if (watermarks->a.urgent_latency_ns < hubbub1->watermarks.a.urgent_latency_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.urgent_ns > hubbub1->watermarks.b.urgent_ns) {
		hubbub1->watermarks.b.urgent_ns = watermarks->b.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_URGENCY_WATERMARK_B, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.urgent_ns, prog_wm_value);
	} else if (watermarks->b.urgent_ns < hubbub1->watermarks.b.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->a.frac_urg_bw_flip
			> hubbub1->watermarks.a.frac_urg_bw_flip) {
		hubbub1->watermarks.a.frac_urg_bw_flip = watermarks->a.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, watermarks->a.frac_urg_bw_flip);
	} else if (watermarks->a.frac_urg_bw_flip
			< hubbub1->watermarks.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.frac_urg_bw_nom
			> hubbub1->watermarks.a.frac_urg_bw_nom) {
		hubbub1->watermarks.a.frac_urg_bw_nom = watermarks->a.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, watermarks->a.frac_urg_bw_nom);
	} else if (watermarks->a.frac_urg_bw_nom
			< hubbub1->watermarks.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.urgent_latency_ns > hubbub1->watermarks.b.urgent_latency_ns) {
		hubbub1->watermarks.b.urgent_latency_ns = watermarks->b.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.urgent_latency_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, prog_wm_value);
	} else if (watermarks->b.urgent_latency_ns < hubbub1->watermarks.b.urgent_latency_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.urgent_ns > hubbub1->watermarks.c.urgent_ns) {
		hubbub1->watermarks.c.urgent_ns = watermarks->c.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_URGENCY_WATERMARK_C, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.urgent_ns, prog_wm_value);
	} else if (watermarks->c.urgent_ns < hubbub1->watermarks.c.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->a.frac_urg_bw_flip
			> hubbub1->watermarks.a.frac_urg_bw_flip) {
		hubbub1->watermarks.a.frac_urg_bw_flip = watermarks->a.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, watermarks->a.frac_urg_bw_flip);
	} else if (watermarks->a.frac_urg_bw_flip
			< hubbub1->watermarks.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.frac_urg_bw_nom
			> hubbub1->watermarks.a.frac_urg_bw_nom) {
		hubbub1->watermarks.a.frac_urg_bw_nom = watermarks->a.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, watermarks->a.frac_urg_bw_nom);
	} else if (watermarks->a.frac_urg_bw_nom
			< hubbub1->watermarks.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.urgent_latency_ns > hubbub1->watermarks.c.urgent_latency_ns) {
		hubbub1->watermarks.c.urgent_latency_ns = watermarks->c.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.urgent_latency_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, prog_wm_value);
	} else if (watermarks->c.urgent_latency_ns < hubbub1->watermarks.c.urgent_latency_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.urgent_ns > hubbub1->watermarks.d.urgent_ns) {
		hubbub1->watermarks.d.urgent_ns = watermarks->d.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_URGENCY_WATERMARK_D, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.urgent_ns, prog_wm_value);
	} else if (watermarks->d.urgent_ns < hubbub1->watermarks.d.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->a.frac_urg_bw_flip
			> hubbub1->watermarks.a.frac_urg_bw_flip) {
		hubbub1->watermarks.a.frac_urg_bw_flip = watermarks->a.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, watermarks->a.frac_urg_bw_flip);
	} else if (watermarks->a.frac_urg_bw_flip
			< hubbub1->watermarks.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.frac_urg_bw_nom
			> hubbub1->watermarks.a.frac_urg_bw_nom) {
		hubbub1->watermarks.a.frac_urg_bw_nom = watermarks->a.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, watermarks->a.frac_urg_bw_nom);
	} else if (watermarks->a.frac_urg_bw_nom
			< hubbub1->watermarks.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.urgent_latency_ns > hubbub1->watermarks.d.urgent_latency_ns) {
		hubbub1->watermarks.d.urgent_latency_ns = watermarks->d.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.urgent_latency_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, prog_wm_value);
	} else if (watermarks->d.urgent_latency_ns < hubbub1->watermarks.d.urgent_latency_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub21_program_stutter_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns =
				watermarks->a.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns =
				watermarks->b.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns =
				watermarks->c.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns =
				watermarks->d.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub21_program_pstate_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;

	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.a.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.a.cstate_pstate.pstate_change_ns =
				watermarks->a.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.a.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.b.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.b.cstate_pstate.pstate_change_ns =
				watermarks->b.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.b.cstate_pstate.pstate_change_ns)
		wm_pending = false;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.c.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.c.cstate_pstate.pstate_change_ns =
				watermarks->c.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.c.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.d.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.d.cstate_pstate.pstate_change_ns =
				watermarks->d.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET_2(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value,
				DCHUBBUB_ARB_VM_ROW_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.d.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub21_program_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	if (hubbub21_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub21_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub21_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	/*
	 * The DCHub arbiter has a mechanism to dynamically rate limit the DCHub request stream to the fabric.
	 * If the memory controller is fully utilized and the DCHub requestors are
	 * well ahead of their amortized schedule, then it is safe to prevent the next winner
	 * from being committed and sent to the fabric.
	 * The utilization of the memory controller is approximated by ensuring that
	 * the number of outstanding requests is greater than a threshold specified
	 * by the ARB_MIN_REQ_OUTSTANDING. To determine that the DCHub requestors are well ahead of the amortized schedule,
	 * the slack of the next winner is compared with the ARB_SAT_LEVEL in DLG RefClk cycles.
	 *
	 * TODO: Revisit request limit after figure out right number. request limit for Renoir isn't decided yet, set maximum value (0x1FF)
	 * to turn off it for now.
	 */
	REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE_2(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0x1FF,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND_COMMIT_THRESHOLD, 0xA);
	REG_UPDATE(DCHUBBUB_ARB_HOSTVM_CNTL,
			DCHUBBUB_ARB_MAX_QOS_COMMIT_THRESHOLD, 0xF);

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	return wm_pending;
}

void hubbub21_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_hubbub_wm_set *s;

	memset(wm, 0, sizeof(struct dcn_hubbub_wm));

	s = &wm->sets[0];
	s->wm_set = 0;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A,
			 DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, &s->dram_clk_chanage);

	s = &wm->sets[1];
	s->wm_set = 1;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, &s->dram_clk_chanage);

	s = &wm->sets[2];
	s->wm_set = 2;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C,
			DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, &s->dram_clk_chanage);

	s = &wm->sets[3];
	s->wm_set = 3;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D,
			DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, &s->dram_clk_chanage);
}

void hubbub21_apply_DEDCN21_147_wa(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub1 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;

	prog_wm_value = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);
}

static const struct hubbub_funcs hubbub21_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub21_init_dchub,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub2_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub2_get_dcc_compression_cap,
	.wm_read_state = hubbub21_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub21_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.apply_DEDCN21_147_wa = hubbub21_apply_DEDCN21_147_wa,
};

void hubbub21_construct(struct dcn20_hubbub *hubbub,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask)
{
	hubbub->base.ctx = ctx;

	hubbub->base.funcs = &hubbub21_funcs;

	hubbub->regs = hubbub_regs;
	hubbub->shifts = hubbub_shift;
	hubbub->masks = hubbub_mask;

	hubbub->debug_test_index_pstate = 0xB;
	hubbub->detile_buf_size = 164 * 1024; /* 164KB for DCN2.0 */
}
