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
 * Authors: AMD
 *
 */

#include "dml2_policy.h"

static void get_optimal_ntuple(
	const struct soc_bounding_box_st *socbb,
	struct soc_state_bounding_box_st *entry)
{
	if (entry->dcfclk_mhz > 0) {
		float bw_on_sdp = (float)(entry->dcfclk_mhz * socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_sdp_bw_after_urgent / 100));

		entry->fabricclk_mhz = bw_on_sdp / (socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_fabric_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_sdp / (socbb->num_chans *
			socbb->dram_channel_width_bytes * ((float)socbb->pct_ideal_dram_bw_after_urgent_pixel_only / 100));
	} else if (entry->fabricclk_mhz > 0) {
		float bw_on_fabric = (float)(entry->fabricclk_mhz * socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_fabric_bw_after_urgent / 100));

		entry->dcfclk_mhz = bw_on_fabric / (socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_sdp_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_fabric / (socbb->num_chans *
			socbb->dram_channel_width_bytes * ((float)socbb->pct_ideal_dram_bw_after_urgent_pixel_only / 100));
	} else if (entry->dram_speed_mts > 0) {
		float bw_on_dram = (float)(entry->dram_speed_mts * socbb->num_chans *
			socbb->dram_channel_width_bytes * ((float)socbb->pct_ideal_dram_bw_after_urgent_pixel_only / 100));

		entry->fabricclk_mhz = bw_on_dram / (socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_fabric_bw_after_urgent / 100));
		entry->dcfclk_mhz = bw_on_dram / (socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_sdp_bw_after_urgent / 100));
	}
}

static float calculate_net_bw_in_mbytes_sec(const struct soc_bounding_box_st *socbb,
	struct soc_state_bounding_box_st *entry)
{
	float memory_bw_mbytes_sec = (float)(entry->dram_speed_mts *  socbb->num_chans *
		socbb->dram_channel_width_bytes * ((float)socbb->pct_ideal_dram_bw_after_urgent_pixel_only / 100));

	float fabric_bw_mbytes_sec = (float)(entry->fabricclk_mhz * socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_fabric_bw_after_urgent / 100));

	float sdp_bw_mbytes_sec = (float)(entry->dcfclk_mhz * socbb->return_bus_width_bytes * ((float)socbb->pct_ideal_sdp_bw_after_urgent / 100));

	float limiting_bw_mbytes_sec = memory_bw_mbytes_sec;

	if (fabric_bw_mbytes_sec < limiting_bw_mbytes_sec)
		limiting_bw_mbytes_sec = fabric_bw_mbytes_sec;

	if (sdp_bw_mbytes_sec < limiting_bw_mbytes_sec)
		limiting_bw_mbytes_sec = sdp_bw_mbytes_sec;

	return limiting_bw_mbytes_sec;
}

static void insert_entry_into_table_sorted(const struct soc_bounding_box_st *socbb,
	struct soc_states_st *table,
	struct soc_state_bounding_box_st *entry)
{
	int index = 0;
	int i = 0;
	float net_bw_of_new_state = 0;

	get_optimal_ntuple(socbb, entry);

	if (table->num_states == 0) {
		index = 0;
	} else {
		net_bw_of_new_state = calculate_net_bw_in_mbytes_sec(socbb, entry);
		while (net_bw_of_new_state > calculate_net_bw_in_mbytes_sec(socbb, &table->state_array[index])) {
			index++;
			if (index >= (int) table->num_states)
				break;
		}

		for (i = table->num_states; i > index; i--) {
			table->state_array[i] = table->state_array[i - 1];
		}
		//ASSERT(index < MAX_CLK_TABLE_SIZE);
	}

	table->state_array[index] = *entry;
	table->state_array[index].dcfclk_mhz = (int)entry->dcfclk_mhz;
	table->state_array[index].fabricclk_mhz = (int)entry->fabricclk_mhz;
	table->state_array[index].dram_speed_mts = (int)entry->dram_speed_mts;
	table->num_states++;
}

static void remove_entry_from_table_at_index(struct soc_states_st *table,
	unsigned int index)
{
	int i;

	if (table->num_states == 0)
		return;

	for (i = index; i < (int) table->num_states - 1; i++) {
		table->state_array[i] = table->state_array[i + 1];
	}
	memset(&table->state_array[--table->num_states], 0, sizeof(struct soc_state_bounding_box_st));
}

int dml2_policy_build_synthetic_soc_states(struct dml2_policy_build_synthetic_soc_states_scratch *s,
	struct dml2_policy_build_synthetic_soc_states_params *p)
{
	int i, j;
	unsigned int min_fclk_mhz = p->in_states->state_array[0].fabricclk_mhz;
	unsigned int min_dcfclk_mhz = p->in_states->state_array[0].dcfclk_mhz;
	unsigned int min_socclk_mhz = p->in_states->state_array[0].socclk_mhz;

	int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0,
		max_phyclk_mhz = 0, max_dtbclk_mhz = 0, max_fclk_mhz = 0,
		max_uclk_mhz = 0, max_socclk_mhz = 0;

	int num_uclk_dpms = 0, num_fclk_dpms = 0;

	for (i = 0; i < __DML_MAX_STATE_ARRAY_SIZE__; i++) {
		if (p->in_states->state_array[i].dcfclk_mhz > max_dcfclk_mhz)
			max_dcfclk_mhz = (int) p->in_states->state_array[i].dcfclk_mhz;
		if (p->in_states->state_array[i].fabricclk_mhz > max_fclk_mhz)
			max_fclk_mhz = (int) p->in_states->state_array[i].fabricclk_mhz;
		if (p->in_states->state_array[i].socclk_mhz > max_socclk_mhz)
			max_socclk_mhz = (int) p->in_states->state_array[i].socclk_mhz;
		if (p->in_states->state_array[i].dram_speed_mts > max_uclk_mhz)
			max_uclk_mhz = (int) p->in_states->state_array[i].dram_speed_mts;
		if (p->in_states->state_array[i].dispclk_mhz > max_dispclk_mhz)
			max_dispclk_mhz = (int) p->in_states->state_array[i].dispclk_mhz;
		if (p->in_states->state_array[i].dppclk_mhz > max_dppclk_mhz)
			max_dppclk_mhz = (int) p->in_states->state_array[i].dppclk_mhz;
		if (p->in_states->state_array[i].phyclk_mhz > max_phyclk_mhz)
			max_phyclk_mhz = (int)p->in_states->state_array[i].phyclk_mhz;
		if (p->in_states->state_array[i].dtbclk_mhz > max_dtbclk_mhz)
			max_dtbclk_mhz = (int)p->in_states->state_array[i].dtbclk_mhz;

		if (p->in_states->state_array[i].fabricclk_mhz > 0)
			num_fclk_dpms++;
		if (p->in_states->state_array[i].dram_speed_mts > 0)
			num_uclk_dpms++;
	}

	if (!max_dcfclk_mhz || !max_dispclk_mhz || !max_dppclk_mhz || !max_phyclk_mhz || !max_dtbclk_mhz)
		return -1;

	p->out_states->num_states = 0;

	s->entry = p->in_states->state_array[0];

	s->entry.dispclk_mhz = max_dispclk_mhz;
	s->entry.dppclk_mhz = max_dppclk_mhz;
	s->entry.dtbclk_mhz = max_dtbclk_mhz;
	s->entry.phyclk_mhz = max_phyclk_mhz;

	s->entry.dscclk_mhz = max_dispclk_mhz / 3;
	s->entry.phyclk_mhz = max_phyclk_mhz;
	s->entry.dtbclk_mhz = max_dtbclk_mhz;

	// Insert all the DCFCLK STAs first
	for (i = 0; i < p->num_dcfclk_stas; i++) {
		s->entry.dcfclk_mhz = p->dcfclk_stas_mhz[i];
		s->entry.fabricclk_mhz = 0;
		s->entry.dram_speed_mts = 0;
		if (i > 0)
			s->entry.socclk_mhz = max_socclk_mhz;

		insert_entry_into_table_sorted(p->in_bbox, p->out_states, &s->entry);
	}

	// Insert the UCLK DPMS
	for (i = 0; i < num_uclk_dpms; i++) {
		s->entry.dcfclk_mhz = 0;
		s->entry.fabricclk_mhz = 0;
		s->entry.dram_speed_mts = p->in_states->state_array[i].dram_speed_mts;
		if (i == 0) {
			s->entry.socclk_mhz = min_socclk_mhz;
		} else {
			s->entry.socclk_mhz = max_socclk_mhz;
		}

		insert_entry_into_table_sorted(p->in_bbox, p->out_states, &s->entry);
	}

	// Insert FCLK DPMs (if present)
	if (num_fclk_dpms > 2) {
		for (i = 0; i < num_fclk_dpms; i++) {
			s->entry.dcfclk_mhz = 0;
			s->entry.fabricclk_mhz = p->in_states->state_array[i].fabricclk_mhz;
			s->entry.dram_speed_mts = 0;

		insert_entry_into_table_sorted(p->in_bbox, p->out_states, &s->entry);
		}
	}
	// Add max FCLK
	else {
		s->entry.dcfclk_mhz = 0;
		s->entry.fabricclk_mhz = p->in_states->state_array[num_fclk_dpms - 1].fabricclk_mhz;
		s->entry.dram_speed_mts = 0;

		insert_entry_into_table_sorted(p->in_bbox, p->out_states, &s->entry);
	}

	// Remove states that require higher clocks than are supported
	for (i = p->out_states->num_states - 1; i >= 0; i--) {
		if (p->out_states->state_array[i].dcfclk_mhz > max_dcfclk_mhz ||
			p->out_states->state_array[i].fabricclk_mhz > max_fclk_mhz ||
			p->out_states->state_array[i].dram_speed_mts > max_uclk_mhz)
			remove_entry_from_table_at_index(p->out_states, i);
	}

	// At this point, the table contains all "points of interest" based on
	// DPMs from PMFW, and STAs. Table is sorted by BW, and all clock
	// ratios (by derate, are exact).

	// Round up UCLK to DPMs
	for (i = p->out_states->num_states - 1; i >= 0; i--) {
		for (j = 0; j < num_uclk_dpms; j++) {
			if (p->in_states->state_array[j].dram_speed_mts >= p->out_states->state_array[i].dram_speed_mts) {
				p->out_states->state_array[i].dram_speed_mts = p->in_states->state_array[j].dram_speed_mts;
				break;
			}
		}
	}

	// If FCLK is coarse grained, round up to next DPMs
	if (num_fclk_dpms > 2) {
		for (i = p->out_states->num_states - 1; i >= 0; i--) {
			for (j = 0; j < num_fclk_dpms; j++) {
				if (p->in_states->state_array[j].fabricclk_mhz >= p->out_states->state_array[i].fabricclk_mhz) {
					p->out_states->state_array[i].fabricclk_mhz = p->in_states->state_array[j].fabricclk_mhz;
					break;
				}
			}
		}
	}

	// Clamp to min FCLK/DCFCLK
	for (i = p->out_states->num_states - 1; i >= 0; i--) {
		if (p->out_states->state_array[i].fabricclk_mhz < min_fclk_mhz) {
			p->out_states->state_array[i].fabricclk_mhz = min_fclk_mhz;
		}
		if (p->out_states->state_array[i].dcfclk_mhz < min_dcfclk_mhz) {
			p->out_states->state_array[i].dcfclk_mhz = min_dcfclk_mhz;
		}
	}

	// Remove duplicate states, note duplicate states are always neighbouring since table is sorted.
	i = 0;
	while (i < (int) p->out_states->num_states - 1) {
		if (p->out_states->state_array[i].dcfclk_mhz == p->out_states->state_array[i + 1].dcfclk_mhz &&
			p->out_states->state_array[i].fabricclk_mhz == p->out_states->state_array[i + 1].fabricclk_mhz &&
			p->out_states->state_array[i].dram_speed_mts == p->out_states->state_array[i + 1].dram_speed_mts)
			remove_entry_from_table_at_index(p->out_states, i);
	else
		i++;
	}

	return 0;
}

void build_unoptimized_policy_settings(enum dml_project_id project, struct dml_mode_eval_policy_st *policy)
{
	for (int i = 0; i < __DML_NUM_PLANES__; i++) {
		policy->MPCCombineUse[i] = dml_mpc_as_needed_for_voltage; // TOREVIEW: Is this still needed?  When is MPCC useful for pstate given CRB?
		policy->ODMUse[i] = dml_odm_use_policy_combine_as_needed;
		policy->ImmediateFlipRequirement[i] = dml_immediate_flip_required;
		policy->AllowForPStateChangeOrStutterInVBlank[i] = dml_prefetch_support_uclk_fclk_and_stutter_if_possible;
	}

	/* Change the default policy initializations as per spreadsheet. We might need to
	 * review and change them later on as per Jun's earlier comments.
	 */
	policy->UseUnboundedRequesting = dml_unbounded_requesting_enable;
	policy->UseMinimumRequiredDCFCLK = false;
	policy->DRAMClockChangeRequirementFinal = true; // TOREVIEW: What does this mean?
	policy->FCLKChangeRequirementFinal = true; // TOREVIEW: What does this mean?
	policy->USRRetrainingRequiredFinal = true;
	policy->EnhancedPrefetchScheduleAccelerationFinal = true; // TOREVIEW: What does this mean?
	policy->NomDETInKByteOverrideEnable = false;
	policy->NomDETInKByteOverrideValue = 0;
	policy->DCCProgrammingAssumesScanDirectionUnknownFinal = true;
	policy->SynchronizeTimingsFinal = true;
	policy->SynchronizeDRRDisplaysForUCLKPStateChangeFinal = true;
	policy->AssumeModeSupportAtMaxPwrStateEvenDRAMClockChangeNotSupported = true; // TOREVIEW: What does this mean?
	policy->AssumeModeSupportAtMaxPwrStateEvenFClockChangeNotSupported = true; // TOREVIEW: What does this mean?
	if (project == dml_project_dcn35 ||
		project == dml_project_dcn351) {
		policy->DCCProgrammingAssumesScanDirectionUnknownFinal = false;
		policy->AllowForPStateChangeOrStutterInVBlankFinal = dml_prefetch_support_uclk_fclk_and_stutter_if_possible; /*new*/
		policy->UseOnlyMaxPrefetchModes = 1;
	}
}
