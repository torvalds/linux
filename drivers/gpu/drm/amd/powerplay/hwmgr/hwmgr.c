/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "pp_debug.h"
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <drm/amdgpu_drm.h>
#include "cgs_common.h"
#include "power_state.h"
#include "hwmgr.h"
#include "pppcielanes.h"
#include "ppatomctrl.h"
#include "ppsmc.h"
#include "pp_acpi.h"
#include "amd_acpi.h"

extern int cz_init_function_pointers(struct pp_hwmgr *hwmgr);

static int polaris_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static void hwmgr_init_default_caps(struct pp_hwmgr *hwmgr);
static int hwmgr_set_user_specify_caps(struct pp_hwmgr *hwmgr);
static int fiji_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static int tonga_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static int topaz_set_asic_special_caps(struct pp_hwmgr *hwmgr);

uint8_t convert_to_vid(uint16_t vddc)
{
	return (uint8_t) ((6200 - (vddc * VOLTAGE_SCALE)) / 25);
}

int hwmgr_early_init(struct pp_instance *handle)
{
	struct pp_hwmgr *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = kzalloc(sizeof(struct pp_hwmgr), GFP_KERNEL);
	if (hwmgr == NULL)
		return -ENOMEM;

	handle->hwmgr = hwmgr;
	hwmgr->smumgr = handle->smu_mgr;
	hwmgr->device = handle->device;
	hwmgr->chip_family = handle->chip_family;
	hwmgr->chip_id = handle->chip_id;
	hwmgr->feature_mask = handle->feature_mask;
	hwmgr->usec_timeout = AMD_MAX_USEC_TIMEOUT;
	hwmgr->power_source = PP_PowerSource_AC;
	hwmgr->pp_table_version = PP_TABLE_V1;
	hwmgr->dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	hwmgr_init_default_caps(hwmgr);
	hwmgr_set_user_specify_caps(hwmgr);

	switch (hwmgr->chip_family) {
	case AMDGPU_FAMILY_CZ:
		cz_init_function_pointers(hwmgr);
		break;
	case AMDGPU_FAMILY_VI:
		switch (hwmgr->chip_id) {
		case CHIP_TOPAZ:
			topaz_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~ (PP_VBI_TIME_SUPPORT_MASK |
						PP_ENABLE_GFX_CG_THRU_SMU);
			hwmgr->pp_table_version = PP_TABLE_V0;
			break;
		case CHIP_TONGA:
			tonga_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~PP_VBI_TIME_SUPPORT_MASK;
			break;
		case CHIP_FIJI:
			fiji_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~ (PP_VBI_TIME_SUPPORT_MASK |
						PP_ENABLE_GFX_CG_THRU_SMU);
			break;
		case CHIP_POLARIS11:
		case CHIP_POLARIS10:
		case CHIP_POLARIS12:
			polaris_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~(PP_UVD_HANDSHAKE_MASK);
			break;
		default:
			return -EINVAL;
		}
		smu7_init_function_pointers(hwmgr);
		break;
	case AMDGPU_FAMILY_AI:
		switch (hwmgr->chip_id) {
		case CHIP_VEGA10:
			vega10_hwmgr_init(hwmgr);
			break;
		default:
			return -EINVAL;
		}
		break;
	case AMDGPU_FAMILY_RV:
		switch (hwmgr->chip_id) {
		case CHIP_RAVEN:
			rv_init_function_pointers(hwmgr);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hw_init_power_state_table(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned int i;
	unsigned int table_entries;
	struct pp_power_state *state;
	int size;

	if (hwmgr->hwmgr_func->get_num_of_pp_table_entries == NULL)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_power_state_size == NULL)
		return -EINVAL;

	hwmgr->num_ps = table_entries = hwmgr->hwmgr_func->get_num_of_pp_table_entries(hwmgr);

	hwmgr->ps_size = size = hwmgr->hwmgr_func->get_power_state_size(hwmgr) +
					  sizeof(struct pp_power_state);

	hwmgr->ps = kzalloc(size * table_entries, GFP_KERNEL);
	if (hwmgr->ps == NULL)
		return -ENOMEM;

	hwmgr->request_ps = kzalloc(size, GFP_KERNEL);
	if (hwmgr->request_ps == NULL) {
		kfree(hwmgr->ps);
		hwmgr->ps = NULL;
		return -ENOMEM;
	}

	hwmgr->current_ps = kzalloc(size, GFP_KERNEL);
	if (hwmgr->current_ps == NULL) {
		kfree(hwmgr->request_ps);
		kfree(hwmgr->ps);
		hwmgr->request_ps = NULL;
		hwmgr->ps = NULL;
		return -ENOMEM;
	}

	state = hwmgr->ps;

	for (i = 0; i < table_entries; i++) {
		result = hwmgr->hwmgr_func->get_pp_table_entry(hwmgr, i, state);

		if (state->classification.flags & PP_StateClassificationFlag_Boot) {
			hwmgr->boot_ps = state;
			memcpy(hwmgr->current_ps, state, size);
			memcpy(hwmgr->request_ps, state, size);
		}

		state->id = i + 1; /* assigned unique num for every power state id */

		if (state->classification.flags & PP_StateClassificationFlag_Uvd)
			hwmgr->uvd_ps = state;
		state = (struct pp_power_state *)((unsigned long)state + size);
	}

	return 0;
}

static int hw_fini_power_state_table(struct pp_hwmgr *hwmgr)
{
	if (hwmgr == NULL)
		return -EINVAL;

	kfree(hwmgr->current_ps);
	kfree(hwmgr->request_ps);
	kfree(hwmgr->ps);
	hwmgr->request_ps = NULL;
	hwmgr->ps = NULL;
	hwmgr->current_ps = NULL;
	return 0;
}

int hwmgr_hw_init(struct pp_instance *handle)
{
	struct pp_hwmgr *hwmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = handle->hwmgr;

	if (hwmgr->pptable_func == NULL ||
	    hwmgr->pptable_func->pptable_init == NULL ||
	    hwmgr->hwmgr_func->backend_init == NULL)
		return -EINVAL;

	ret = hwmgr->pptable_func->pptable_init(hwmgr);
	if (ret)
		goto err;

	ret = hwmgr->hwmgr_func->backend_init(hwmgr);
	if (ret)
		goto err1;

	ret = hw_init_power_state_table(hwmgr);
	if (ret)
		goto err2;
	return 0;
err2:
	if (hwmgr->hwmgr_func->backend_fini)
		hwmgr->hwmgr_func->backend_fini(hwmgr);
err1:
	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);
err:
	pr_err("amdgpu: powerplay initialization failed\n");
	return ret;
}

int hwmgr_hw_fini(struct pp_instance *handle)
{
	struct pp_hwmgr *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = handle->hwmgr;

	if (hwmgr->hwmgr_func->backend_fini)
		hwmgr->hwmgr_func->backend_fini(hwmgr);
	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);
	return hw_fini_power_state_table(hwmgr);
}


/**
 * Returns once the part of the register indicated by the mask has
 * reached the given value.
 */
int phm_wait_on_register(struct pp_hwmgr *hwmgr, uint32_t index,
			 uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (hwmgr == NULL || hwmgr->device == NULL) {
		pr_err("Invalid Hardware Manager!");
		return -EINVAL;
	}

	for (i = 0; i < hwmgr->usec_timeout; i++) {
		cur_value = cgs_read_register(hwmgr->device, index);
		if ((cur_value & mask) == (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == hwmgr->usec_timeout)
		return -1;
	return 0;
}


/**
 * Returns once the part of the register indicated by the mask has
 * reached the given value.The indirect space is described by giving
 * the memory-mapped index of the indirect index register.
 */
void phm_wait_on_indirect_register(struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask)
{
	if (hwmgr == NULL || hwmgr->device == NULL) {
		pr_err("Invalid Hardware Manager!");
		return;
	}

	cgs_write_register(hwmgr->device, indirect_port, index);
	phm_wait_on_register(hwmgr, indirect_port + 1, mask, value);
}



bool phm_cf_want_uvd_power_gating(struct pp_hwmgr *hwmgr)
{
	return phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UVDPowerGating);
}

bool phm_cf_want_vce_power_gating(struct pp_hwmgr *hwmgr)
{
	return phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_VCEPowerGating);
}


int phm_trim_voltage_table(struct pp_atomctrl_voltage_table *vol_table)
{
	uint32_t i, j;
	uint16_t vvalue;
	bool found = false;
	struct pp_atomctrl_voltage_table *table;

	PP_ASSERT_WITH_CODE((NULL != vol_table),
			"Voltage Table empty.", return -EINVAL);

	table = kzalloc(sizeof(struct pp_atomctrl_voltage_table),
			GFP_KERNEL);

	if (NULL == table)
		return -EINVAL;

	table->mask_low = vol_table->mask_low;
	table->phase_delay = vol_table->phase_delay;

	for (i = 0; i < vol_table->count; i++) {
		vvalue = vol_table->entries[i].value;
		found = false;

		for (j = 0; j < table->count; j++) {
			if (vvalue == table->entries[j].value) {
				found = true;
				break;
			}
		}

		if (!found) {
			table->entries[table->count].value = vvalue;
			table->entries[table->count].smio_low =
					vol_table->entries[i].smio_low;
			table->count++;
		}
	}

	memcpy(vol_table, table, sizeof(struct pp_atomctrl_voltage_table));
	kfree(table);
	table = NULL;
	return 0;
}

int phm_get_svi2_mvdd_voltage_table(struct pp_atomctrl_voltage_table *vol_table,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint32_t i;
	int result;

	PP_ASSERT_WITH_CODE((0 != dep_table->count),
			"Voltage Dependency Table empty.", return -EINVAL);

	PP_ASSERT_WITH_CODE((NULL != vol_table),
			"vol_table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < dep_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].mvdd;
		vol_table->entries[i].smio_low = 0;
	}

	result = phm_trim_voltage_table(vol_table);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to trim MVDD table.", return result);

	return 0;
}

int phm_get_svi2_vddci_voltage_table(struct pp_atomctrl_voltage_table *vol_table,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint32_t i;
	int result;

	PP_ASSERT_WITH_CODE((0 != dep_table->count),
			"Voltage Dependency Table empty.", return -EINVAL);

	PP_ASSERT_WITH_CODE((NULL != vol_table),
			"vol_table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < dep_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].vddci;
		vol_table->entries[i].smio_low = 0;
	}

	result = phm_trim_voltage_table(vol_table);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to trim VDDCI table.", return result);

	return 0;
}

int phm_get_svi2_vdd_voltage_table(struct pp_atomctrl_voltage_table *vol_table,
		phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	int i = 0;

	PP_ASSERT_WITH_CODE((0 != lookup_table->count),
			"Voltage Lookup Table empty.", return -EINVAL);

	PP_ASSERT_WITH_CODE((NULL != vol_table),
			"vol_table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;

	vol_table->count = lookup_table->count;

	for (i = 0; i < vol_table->count; i++) {
		vol_table->entries[i].value = lookup_table->entries[i].us_vdd;
		vol_table->entries[i].smio_low = 0;
	}

	return 0;
}

void phm_trim_voltage_table_to_fit_state_table(uint32_t max_vol_steps,
				struct pp_atomctrl_voltage_table *vol_table)
{
	unsigned int i, diff;

	if (vol_table->count <= max_vol_steps)
		return;

	diff = vol_table->count - max_vol_steps;

	for (i = 0; i < max_vol_steps; i++)
		vol_table->entries[i] = vol_table->entries[i + diff];

	vol_table->count = max_vol_steps;

	return;
}

int phm_reset_single_dpm_table(void *table,
				uint32_t count, int max)
{
	int i;

	struct vi_dpm_table *dpm_table = (struct vi_dpm_table *)table;

	dpm_table->count = count > max ? max : count;

	for (i = 0; i < dpm_table->count; i++)
		dpm_table->dpm_level[i].enabled = false;

	return 0;
}

void phm_setup_pcie_table_entry(
	void *table,
	uint32_t index, uint32_t pcie_gen,
	uint32_t pcie_lanes)
{
	struct vi_dpm_table *dpm_table = (struct vi_dpm_table *)table;
	dpm_table->dpm_level[index].value = pcie_gen;
	dpm_table->dpm_level[index].param1 = pcie_lanes;
	dpm_table->dpm_level[index].enabled = 1;
}

int32_t phm_get_dpm_level_enable_mask_value(void *table)
{
	int32_t i;
	int32_t mask = 0;
	struct vi_dpm_table *dpm_table = (struct vi_dpm_table *)table;

	for (i = dpm_table->count; i > 0; i--) {
		mask = mask << 1;
		if (dpm_table->dpm_level[i - 1].enabled)
			mask |= 0x1;
		else
			mask &= 0xFFFFFFFE;
	}

	return mask;
}

uint8_t phm_get_voltage_index(
		struct phm_ppt_v1_voltage_lookup_table *lookup_table, uint16_t voltage)
{
	uint8_t count = (uint8_t) (lookup_table->count);
	uint8_t i;

	PP_ASSERT_WITH_CODE((NULL != lookup_table),
			"Lookup Table empty.", return 0);
	PP_ASSERT_WITH_CODE((0 != count),
			"Lookup Table empty.", return 0);

	for (i = 0; i < lookup_table->count; i++) {
		/* find first voltage equal or bigger than requested */
		if (lookup_table->entries[i].us_vdd >= voltage)
			return i;
	}
	/* voltage is bigger than max voltage in the table */
	return i - 1;
}

uint8_t phm_get_voltage_id(pp_atomctrl_voltage_table *voltage_table,
		uint32_t voltage)
{
	uint8_t count = (uint8_t) (voltage_table->count);
	uint8_t i = 0;

	PP_ASSERT_WITH_CODE((NULL != voltage_table),
		"Voltage Table empty.", return 0;);
	PP_ASSERT_WITH_CODE((0 != count),
		"Voltage Table empty.", return 0;);

	for (i = 0; i < count; i++) {
		/* find first voltage bigger than requested */
		if (voltage_table->entries[i].value >= voltage)
			return i;
	}

	/* voltage is bigger than max voltage in the table */
	return i - 1;
}

uint16_t phm_find_closest_vddci(struct pp_atomctrl_voltage_table *vddci_table, uint16_t vddci)
{
	uint32_t  i;

	for (i = 0; i < vddci_table->count; i++) {
		if (vddci_table->entries[i].value >= vddci)
			return vddci_table->entries[i].value;
	}

	PP_ASSERT_WITH_CODE(false,
			"VDDCI is larger than max VDDCI in VDDCI Voltage Table!",
			return vddci_table->entries[i-1].value);
}

int phm_find_boot_level(void *table,
		uint32_t value, uint32_t *boot_level)
{
	int result = -EINVAL;
	uint32_t i;
	struct vi_dpm_table *dpm_table = (struct vi_dpm_table *)table;

	for (i = 0; i < dpm_table->count; i++) {
		if (value == dpm_table->dpm_level[i].value) {
			*boot_level = i;
			result = 0;
		}
	}

	return result;
}

int phm_get_sclk_for_voltage_evv(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_voltage_lookup_table *lookup_table,
	uint16_t virtual_voltage_id, int32_t *sclk)
{
	uint8_t entryId;
	uint8_t voltageId;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	PP_ASSERT_WITH_CODE(lookup_table->count != 0, "Lookup table is empty", return -EINVAL);

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sckl */
	for (entryId = 0; entryId < table_info->vdd_dep_on_sclk->count; entryId++) {
		voltageId = table_info->vdd_dep_on_sclk->entries[entryId].vddInd;
		if (lookup_table->entries[voltageId].us_vdd == virtual_voltage_id)
			break;
	}

	PP_ASSERT_WITH_CODE(entryId < table_info->vdd_dep_on_sclk->count,
			"Can't find requested voltage id in vdd_dep_on_sclk table!",
			return -EINVAL;
			);

	*sclk = table_info->vdd_dep_on_sclk->entries[entryId].clk;

	return 0;
}

/**
 * Initialize Dynamic State Adjustment Rule Settings
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 */
int phm_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr)
{
	uint32_t table_size;
	struct phm_clock_voltage_dependency_table *table_clk_vlt;
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	/* initialize vddc_dep_on_dal_pwrl table */
	table_size = sizeof(uint32_t) + 4 * sizeof(struct phm_clock_voltage_dependency_record);
	table_clk_vlt = kzalloc(table_size, GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		pr_err("Can not allocate space for vddc_dep_on_dal_pwrl! \n");
		return -ENOMEM;
	} else {
		table_clk_vlt->count = 4;
		table_clk_vlt->entries[0].clk = PP_DAL_POWERLEVEL_ULTRALOW;
		table_clk_vlt->entries[0].v = 0;
		table_clk_vlt->entries[1].clk = PP_DAL_POWERLEVEL_LOW;
		table_clk_vlt->entries[1].v = 720;
		table_clk_vlt->entries[2].clk = PP_DAL_POWERLEVEL_NOMINAL;
		table_clk_vlt->entries[2].v = 810;
		table_clk_vlt->entries[3].clk = PP_DAL_POWERLEVEL_PERFORMANCE;
		table_clk_vlt->entries[3].v = 900;
		if (pptable_info != NULL)
			pptable_info->vddc_dep_on_dal_pwrl = table_clk_vlt;
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = table_clk_vlt;
	}

	return 0;
}

uint32_t phm_get_lowest_enabled_level(struct pp_hwmgr *hwmgr, uint32_t mask)
{
	uint32_t level = 0;

	while (0 == (mask & (1 << level)))
		level++;

	return level;
}

void phm_apply_dal_min_voltage_request(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	struct phm_clock_voltage_dependency_table *table =
				table_info->vddc_dep_on_dal_pwrl;
	struct phm_ppt_v1_clock_voltage_dependency_table *vddc_table;
	enum PP_DAL_POWERLEVEL dal_power_level = hwmgr->dal_power_level;
	uint32_t req_vddc = 0, req_volt, i;

	if (!table || table->count <= 0
		|| dal_power_level < PP_DAL_POWERLEVEL_ULTRALOW
		|| dal_power_level > PP_DAL_POWERLEVEL_PERFORMANCE)
		return;

	for (i = 0; i < table->count; i++) {
		if (dal_power_level == table->entries[i].clk) {
			req_vddc = table->entries[i].v;
			break;
		}
	}

	vddc_table = table_info->vdd_dep_on_sclk;
	for (i = 0; i < vddc_table->count; i++) {
		if (req_vddc <= vddc_table->entries[i].vddc) {
			req_volt = (((uint32_t)vddc_table->entries[i].vddc) * VOLTAGE_SCALE);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_VddC_Request, req_volt);
			return;
		}
	}
	pr_err("DAL requested level can not"
			" found a available voltage in VDDC DPM Table \n");
}

void hwmgr_init_default_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableVoltageTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableEngineTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMemoryTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMGClockGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMGCGTSSM);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableLSClockGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_Force3DClockSupport);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableLightSleep);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMCLS);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisablePowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableDPM);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableSMUUVDHandshake);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_ThermalAutoThrottling);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_NoOD5Support);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UserMaxClockForMultiDisplays);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_VpuRecoveryInProgress);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UVDDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_VCEDPM);

	if (acpi_atcs_functions_supported(hwmgr->device, ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST) &&
		acpi_atcs_functions_supported(hwmgr->device, ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DynamicPatchPowerState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_EnableSMU7ThermalManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SMC);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_FanSpeedInTableIsRPM);

	return;
}

int hwmgr_set_user_specify_caps(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->feature_mask & PP_SCLK_DEEP_SLEEP_MASK)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	if (hwmgr->feature_mask & PP_POWER_CONTAINMENT_MASK) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_PowerContainment);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);
	} else {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_PowerContainment);
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);
	}

	return 0;
}

int phm_get_voltage_evv_on_sclk(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint32_t sclk, uint16_t id, uint16_t *voltage)
{
	uint32_t vol;
	int ret = 0;

	if (hwmgr->chip_id < CHIP_TONGA) {
		ret = atomctrl_get_voltage_evv(hwmgr, id, voltage);
	} else if (hwmgr->chip_id < CHIP_POLARIS10) {
		ret = atomctrl_get_voltage_evv_on_sclk(hwmgr, voltage_type, sclk, id, voltage);
		if (*voltage >= 2000 || *voltage == 0)
			*voltage = 1150;
	} else {
		ret = atomctrl_get_voltage_evv_on_sclk_ai(hwmgr, voltage_type, sclk, id, &vol);
		*voltage = (uint16_t)(vol/100);
	}
	return ret;
}

int polaris_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_SQRamping);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_RegulatorHot);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_AutomaticDCTransition);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_TablelessHardwareInterface);


	if (hwmgr->chip_id != CHIP_POLARIS10)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SPLLShutdownSupport);

	if (hwmgr->chip_id != CHIP_POLARIS11) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_DBRamping);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_TDRamping);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_TCPRamping);
	}
	return 0;
}

int fiji_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	return 0;
}

int tonga_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_UVDPowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_VCEPowerGating);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			 PHM_PlatformCaps_TablelessHardwareInterface);

	return 0;
}

int topaz_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			 PHM_PlatformCaps_TablelessHardwareInterface);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_EVV);
	return 0;
}
