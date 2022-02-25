/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 */

#include "amdgpu.h"
#include "amdgpu_i2c.h"
#include "amdgpu_atombios.h"
#include "atom.h"
#include "amd_pcie.h"
#include "legacy_dpm.h"
#include "amdgpu_dpm_internal.h"
#include "amdgpu_display.h"

#define amdgpu_dpm_pre_set_power_state(adev) \
		((adev)->powerplay.pp_funcs->pre_set_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_post_set_power_state(adev) \
		((adev)->powerplay.pp_funcs->post_set_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_display_configuration_changed(adev) \
		((adev)->powerplay.pp_funcs->display_configuration_changed((adev)->powerplay.pp_handle))

#define amdgpu_dpm_print_power_state(adev, ps) \
		((adev)->powerplay.pp_funcs->print_power_state((adev)->powerplay.pp_handle, (ps)))

#define amdgpu_dpm_vblank_too_short(adev) \
		((adev)->powerplay.pp_funcs->vblank_too_short((adev)->powerplay.pp_handle))

#define amdgpu_dpm_check_state_equal(adev, cps, rps, equal) \
		((adev)->powerplay.pp_funcs->check_state_equal((adev)->powerplay.pp_handle, (cps), (rps), (equal)))

void amdgpu_dpm_print_class_info(u32 class, u32 class2)
{
	const char *s;

	switch (class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) {
	case ATOM_PPLIB_CLASSIFICATION_UI_NONE:
	default:
		s = "none";
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_BATTERY:
		s = "battery";
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_BALANCED:
		s = "balanced";
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE:
		s = "performance";
		break;
	}
	printk("\tui class: %s\n", s);
	printk("\tinternal class:");
	if (((class & ~ATOM_PPLIB_CLASSIFICATION_UI_MASK) == 0) &&
	    (class2 == 0))
		pr_cont(" none");
	else {
		if (class & ATOM_PPLIB_CLASSIFICATION_BOOT)
			pr_cont(" boot");
		if (class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
			pr_cont(" thermal");
		if (class & ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE)
			pr_cont(" limited_pwr");
		if (class & ATOM_PPLIB_CLASSIFICATION_REST)
			pr_cont(" rest");
		if (class & ATOM_PPLIB_CLASSIFICATION_FORCED)
			pr_cont(" forced");
		if (class & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
			pr_cont(" 3d_perf");
		if (class & ATOM_PPLIB_CLASSIFICATION_OVERDRIVETEMPLATE)
			pr_cont(" ovrdrv");
		if (class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
			pr_cont(" uvd");
		if (class & ATOM_PPLIB_CLASSIFICATION_3DLOW)
			pr_cont(" 3d_low");
		if (class & ATOM_PPLIB_CLASSIFICATION_ACPI)
			pr_cont(" acpi");
		if (class & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
			pr_cont(" uvd_hd2");
		if (class & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
			pr_cont(" uvd_hd");
		if (class & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
			pr_cont(" uvd_sd");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2)
			pr_cont(" limited_pwr2");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_ULV)
			pr_cont(" ulv");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
			pr_cont(" uvd_mvc");
	}
	pr_cont("\n");
}

void amdgpu_dpm_print_cap_info(u32 caps)
{
	printk("\tcaps:");
	if (caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY)
		pr_cont(" single_disp");
	if (caps & ATOM_PPLIB_SUPPORTS_VIDEO_PLAYBACK)
		pr_cont(" video");
	if (caps & ATOM_PPLIB_DISALLOW_ON_DC)
		pr_cont(" no_dc");
	pr_cont("\n");
}

void amdgpu_dpm_print_ps_status(struct amdgpu_device *adev,
				struct amdgpu_ps *rps)
{
	printk("\tstatus:");
	if (rps == adev->pm.dpm.current_ps)
		pr_cont(" c");
	if (rps == adev->pm.dpm.requested_ps)
		pr_cont(" r");
	if (rps == adev->pm.dpm.boot_ps)
		pr_cont(" b");
	pr_cont("\n");
}

void amdgpu_pm_print_power_states(struct amdgpu_device *adev)
{
	int i;

	if (adev->powerplay.pp_funcs->print_power_state == NULL)
		return;

	for (i = 0; i < adev->pm.dpm.num_ps; i++)
		amdgpu_dpm_print_power_state(adev, &adev->pm.dpm.ps[i]);

}

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
	struct _ATOM_PPLIB_POWERPLAYTABLE4 pplib4;
	struct _ATOM_PPLIB_POWERPLAYTABLE5 pplib5;
};

int amdgpu_get_platform_caps(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	u16 data_offset;
	u8 frev, crev;

	if (!amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	adev->pm.dpm.platform_caps = le32_to_cpu(power_info->pplib.ulPlatformCaps);
	adev->pm.dpm.backbias_response_time = le16_to_cpu(power_info->pplib.usBackbiasTime);
	adev->pm.dpm.voltage_response_time = le16_to_cpu(power_info->pplib.usVoltageTime);

	return 0;
}

union fan_info {
	struct _ATOM_PPLIB_FANTABLE fan;
	struct _ATOM_PPLIB_FANTABLE2 fan2;
	struct _ATOM_PPLIB_FANTABLE3 fan3;
};

static int amdgpu_parse_clk_voltage_dep_table(struct amdgpu_clock_voltage_dependency_table *amdgpu_table,
					      ATOM_PPLIB_Clock_Voltage_Dependency_Table *atom_table)
{
	u32 size = atom_table->ucNumEntries *
		sizeof(struct amdgpu_clock_voltage_dependency_entry);
	int i;
	ATOM_PPLIB_Clock_Voltage_Dependency_Record *entry;

	amdgpu_table->entries = kzalloc(size, GFP_KERNEL);
	if (!amdgpu_table->entries)
		return -ENOMEM;

	entry = &atom_table->entries[0];
	for (i = 0; i < atom_table->ucNumEntries; i++) {
		amdgpu_table->entries[i].clk = le16_to_cpu(entry->usClockLow) |
			(entry->ucClockHigh << 16);
		amdgpu_table->entries[i].v = le16_to_cpu(entry->usVoltage);
		entry = (ATOM_PPLIB_Clock_Voltage_Dependency_Record *)
			((u8 *)entry + sizeof(ATOM_PPLIB_Clock_Voltage_Dependency_Record));
	}
	amdgpu_table->count = atom_table->ucNumEntries;

	return 0;
}

/* sizeof(ATOM_PPLIB_EXTENDEDHEADER) */
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2 12
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V3 14
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V4 16
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5 18
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V6 20
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7 22
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V8 24
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V9 26

int amdgpu_parse_extended_power_table(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	union power_info *power_info;
	union fan_info *fan_info;
	ATOM_PPLIB_Clock_Voltage_Dependency_Table *dep_table;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	u16 data_offset;
	u8 frev, crev;
	int ret, i;

	if (!amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	/* fan table */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE3)) {
		if (power_info->pplib3.usFanTableOffset) {
			fan_info = (union fan_info *)(mode_info->atom_context->bios + data_offset +
						      le16_to_cpu(power_info->pplib3.usFanTableOffset));
			adev->pm.dpm.fan.t_hyst = fan_info->fan.ucTHyst;
			adev->pm.dpm.fan.t_min = le16_to_cpu(fan_info->fan.usTMin);
			adev->pm.dpm.fan.t_med = le16_to_cpu(fan_info->fan.usTMed);
			adev->pm.dpm.fan.t_high = le16_to_cpu(fan_info->fan.usTHigh);
			adev->pm.dpm.fan.pwm_min = le16_to_cpu(fan_info->fan.usPWMMin);
			adev->pm.dpm.fan.pwm_med = le16_to_cpu(fan_info->fan.usPWMMed);
			adev->pm.dpm.fan.pwm_high = le16_to_cpu(fan_info->fan.usPWMHigh);
			if (fan_info->fan.ucFanTableFormat >= 2)
				adev->pm.dpm.fan.t_max = le16_to_cpu(fan_info->fan2.usTMax);
			else
				adev->pm.dpm.fan.t_max = 10900;
			adev->pm.dpm.fan.cycle_delay = 100000;
			if (fan_info->fan.ucFanTableFormat >= 3) {
				adev->pm.dpm.fan.control_mode = fan_info->fan3.ucFanControlMode;
				adev->pm.dpm.fan.default_max_fan_pwm =
					le16_to_cpu(fan_info->fan3.usFanPWMMax);
				adev->pm.dpm.fan.default_fan_output_sensitivity = 4836;
				adev->pm.dpm.fan.fan_output_sensitivity =
					le16_to_cpu(fan_info->fan3.usFanOutputSensitivity);
			}
			adev->pm.dpm.fan.ucode_fan_control = true;
		}
	}

	/* clock dependancy tables, shedding tables */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE4)) {
		if (power_info->pplib4.usVddcDependencyOnSCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcDependencyOnSCLKOffset));
			ret = amdgpu_parse_clk_voltage_dep_table(&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
								 dep_table);
			if (ret) {
				amdgpu_free_extended_power_table(adev);
				return ret;
			}
		}
		if (power_info->pplib4.usVddciDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddciDependencyOnMCLKOffset));
			ret = amdgpu_parse_clk_voltage_dep_table(&adev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
								 dep_table);
			if (ret) {
				amdgpu_free_extended_power_table(adev);
				return ret;
			}
		}
		if (power_info->pplib4.usVddcDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcDependencyOnMCLKOffset));
			ret = amdgpu_parse_clk_voltage_dep_table(&adev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
								 dep_table);
			if (ret) {
				amdgpu_free_extended_power_table(adev);
				return ret;
			}
		}
		if (power_info->pplib4.usMvddDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usMvddDependencyOnMCLKOffset));
			ret = amdgpu_parse_clk_voltage_dep_table(&adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
								 dep_table);
			if (ret) {
				amdgpu_free_extended_power_table(adev);
				return ret;
			}
		}
		if (power_info->pplib4.usMaxClockVoltageOnDCOffset) {
			ATOM_PPLIB_Clock_Voltage_Limit_Table *clk_v =
				(ATOM_PPLIB_Clock_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usMaxClockVoltageOnDCOffset));
			if (clk_v->ucNumEntries) {
				adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.sclk =
					le16_to_cpu(clk_v->entries[0].usSclkLow) |
					(clk_v->entries[0].ucSclkHigh << 16);
				adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.mclk =
					le16_to_cpu(clk_v->entries[0].usMclkLow) |
					(clk_v->entries[0].ucMclkHigh << 16);
				adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc =
					le16_to_cpu(clk_v->entries[0].usVddc);
				adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddci =
					le16_to_cpu(clk_v->entries[0].usVddci);
			}
		}
		if (power_info->pplib4.usVddcPhaseShedLimitsTableOffset) {
			ATOM_PPLIB_PhaseSheddingLimits_Table *psl =
				(ATOM_PPLIB_PhaseSheddingLimits_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcPhaseShedLimitsTableOffset));
			ATOM_PPLIB_PhaseSheddingLimits_Record *entry;

			adev->pm.dpm.dyn_state.phase_shedding_limits_table.entries =
				kcalloc(psl->ucNumEntries,
					sizeof(struct amdgpu_phase_shedding_limits_entry),
					GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.phase_shedding_limits_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}

			entry = &psl->entries[0];
			for (i = 0; i < psl->ucNumEntries; i++) {
				adev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].sclk =
					le16_to_cpu(entry->usSclkLow) | (entry->ucSclkHigh << 16);
				adev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].mclk =
					le16_to_cpu(entry->usMclkLow) | (entry->ucMclkHigh << 16);
				adev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].voltage =
					le16_to_cpu(entry->usVoltage);
				entry = (ATOM_PPLIB_PhaseSheddingLimits_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_PhaseSheddingLimits_Record));
			}
			adev->pm.dpm.dyn_state.phase_shedding_limits_table.count =
				psl->ucNumEntries;
		}
	}

	/* cac data */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE5)) {
		adev->pm.dpm.tdp_limit = le32_to_cpu(power_info->pplib5.ulTDPLimit);
		adev->pm.dpm.near_tdp_limit = le32_to_cpu(power_info->pplib5.ulNearTDPLimit);
		adev->pm.dpm.near_tdp_limit_adjusted = adev->pm.dpm.near_tdp_limit;
		adev->pm.dpm.tdp_od_limit = le16_to_cpu(power_info->pplib5.usTDPODLimit);
		if (adev->pm.dpm.tdp_od_limit)
			adev->pm.dpm.power_control = true;
		else
			adev->pm.dpm.power_control = false;
		adev->pm.dpm.tdp_adjustment = 0;
		adev->pm.dpm.sq_ramping_threshold = le32_to_cpu(power_info->pplib5.ulSQRampingThreshold);
		adev->pm.dpm.cac_leakage = le32_to_cpu(power_info->pplib5.ulCACLeakage);
		adev->pm.dpm.load_line_slope = le16_to_cpu(power_info->pplib5.usLoadLineSlope);
		if (power_info->pplib5.usCACLeakageTableOffset) {
			ATOM_PPLIB_CAC_Leakage_Table *cac_table =
				(ATOM_PPLIB_CAC_Leakage_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib5.usCACLeakageTableOffset));
			ATOM_PPLIB_CAC_Leakage_Record *entry;
			u32 size = cac_table->ucNumEntries * sizeof(struct amdgpu_cac_leakage_table);
			adev->pm.dpm.dyn_state.cac_leakage_table.entries = kzalloc(size, GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.cac_leakage_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			entry = &cac_table->entries[0];
			for (i = 0; i < cac_table->ucNumEntries; i++) {
				if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc1 =
						le16_to_cpu(entry->usVddc1);
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc2 =
						le16_to_cpu(entry->usVddc2);
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc3 =
						le16_to_cpu(entry->usVddc3);
				} else {
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc =
						le16_to_cpu(entry->usVddc);
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].leakage =
						le32_to_cpu(entry->ulLeakageValue);
				}
				entry = (ATOM_PPLIB_CAC_Leakage_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_CAC_Leakage_Record));
			}
			adev->pm.dpm.dyn_state.cac_leakage_table.count = cac_table->ucNumEntries;
		}
	}

	/* ext tables */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE3)) {
		ATOM_PPLIB_EXTENDEDHEADER *ext_hdr = (ATOM_PPLIB_EXTENDEDHEADER *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib3.usExtendendedHeaderOffset));
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2) &&
			ext_hdr->usVCETableOffset) {
			VCEClockInfoArray *array = (VCEClockInfoArray *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usVCETableOffset) + 1);
			ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *limits =
				(ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usVCETableOffset) + 1 +
				 1 + array->ucNumEntries * sizeof(VCEClockInfo));
			ATOM_PPLIB_VCE_State_Table *states =
				(ATOM_PPLIB_VCE_State_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usVCETableOffset) + 1 +
				 1 + (array->ucNumEntries * sizeof (VCEClockInfo)) +
				 1 + (limits->numEntries * sizeof(ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record)));
			ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record *entry;
			ATOM_PPLIB_VCE_State_Record *state_entry;
			VCEClockInfo *vce_clk;
			u32 size = limits->numEntries *
				sizeof(struct amdgpu_vce_clock_voltage_dependency_entry);
			adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries =
				kzalloc(size, GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count =
				limits->numEntries;
			entry = &limits->entries[0];
			state_entry = &states->entries[0];
			for (i = 0; i < limits->numEntries; i++) {
				vce_clk = (VCEClockInfo *)
					((u8 *)&array->entries[0] +
					 (entry->ucVCEClockInfoIndex * sizeof(VCEClockInfo)));
				adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].evclk =
					le16_to_cpu(vce_clk->usEVClkLow) | (vce_clk->ucEVClkHigh << 16);
				adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].ecclk =
					le16_to_cpu(vce_clk->usECClkLow) | (vce_clk->ucECClkHigh << 16);
				adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].v =
					le16_to_cpu(entry->usVoltage);
				entry = (ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record));
			}
			adev->pm.dpm.num_of_vce_states =
					states->numEntries > AMD_MAX_VCE_LEVELS ?
					AMD_MAX_VCE_LEVELS : states->numEntries;
			for (i = 0; i < adev->pm.dpm.num_of_vce_states; i++) {
				vce_clk = (VCEClockInfo *)
					((u8 *)&array->entries[0] +
					 (state_entry->ucVCEClockInfoIndex * sizeof(VCEClockInfo)));
				adev->pm.dpm.vce_states[i].evclk =
					le16_to_cpu(vce_clk->usEVClkLow) | (vce_clk->ucEVClkHigh << 16);
				adev->pm.dpm.vce_states[i].ecclk =
					le16_to_cpu(vce_clk->usECClkLow) | (vce_clk->ucECClkHigh << 16);
				adev->pm.dpm.vce_states[i].clk_idx =
					state_entry->ucClockInfoIndex & 0x3f;
				adev->pm.dpm.vce_states[i].pstate =
					(state_entry->ucClockInfoIndex & 0xc0) >> 6;
				state_entry = (ATOM_PPLIB_VCE_State_Record *)
					((u8 *)state_entry + sizeof(ATOM_PPLIB_VCE_State_Record));
			}
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V3) &&
			ext_hdr->usUVDTableOffset) {
			UVDClockInfoArray *array = (UVDClockInfoArray *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usUVDTableOffset) + 1);
			ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table *limits =
				(ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usUVDTableOffset) + 1 +
				 1 + (array->ucNumEntries * sizeof (UVDClockInfo)));
			ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record *entry;
			u32 size = limits->numEntries *
				sizeof(struct amdgpu_uvd_clock_voltage_dependency_entry);
			adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries =
				kzalloc(size, GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count =
				limits->numEntries;
			entry = &limits->entries[0];
			for (i = 0; i < limits->numEntries; i++) {
				UVDClockInfo *uvd_clk = (UVDClockInfo *)
					((u8 *)&array->entries[0] +
					 (entry->ucUVDClockInfoIndex * sizeof(UVDClockInfo)));
				adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[i].vclk =
					le16_to_cpu(uvd_clk->usVClkLow) | (uvd_clk->ucVClkHigh << 16);
				adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[i].dclk =
					le16_to_cpu(uvd_clk->usDClkLow) | (uvd_clk->ucDClkHigh << 16);
				adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[i].v =
					le16_to_cpu(entry->usVoltage);
				entry = (ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record));
			}
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V4) &&
			ext_hdr->usSAMUTableOffset) {
			ATOM_PPLIB_SAMClk_Voltage_Limit_Table *limits =
				(ATOM_PPLIB_SAMClk_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usSAMUTableOffset) + 1);
			ATOM_PPLIB_SAMClk_Voltage_Limit_Record *entry;
			u32 size = limits->numEntries *
				sizeof(struct amdgpu_clock_voltage_dependency_entry);
			adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries =
				kzalloc(size, GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.count =
				limits->numEntries;
			entry = &limits->entries[0];
			for (i = 0; i < limits->numEntries; i++) {
				adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[i].clk =
					le16_to_cpu(entry->usSAMClockLow) | (entry->ucSAMClockHigh << 16);
				adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[i].v =
					le16_to_cpu(entry->usVoltage);
				entry = (ATOM_PPLIB_SAMClk_Voltage_Limit_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_SAMClk_Voltage_Limit_Record));
			}
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5) &&
		    ext_hdr->usPPMTableOffset) {
			ATOM_PPLIB_PPM_Table *ppm = (ATOM_PPLIB_PPM_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usPPMTableOffset));
			adev->pm.dpm.dyn_state.ppm_table =
				kzalloc(sizeof(struct amdgpu_ppm_table), GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.ppm_table) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			adev->pm.dpm.dyn_state.ppm_table->ppm_design = ppm->ucPpmDesign;
			adev->pm.dpm.dyn_state.ppm_table->cpu_core_number =
				le16_to_cpu(ppm->usCpuCoreNumber);
			adev->pm.dpm.dyn_state.ppm_table->platform_tdp =
				le32_to_cpu(ppm->ulPlatformTDP);
			adev->pm.dpm.dyn_state.ppm_table->small_ac_platform_tdp =
				le32_to_cpu(ppm->ulSmallACPlatformTDP);
			adev->pm.dpm.dyn_state.ppm_table->platform_tdc =
				le32_to_cpu(ppm->ulPlatformTDC);
			adev->pm.dpm.dyn_state.ppm_table->small_ac_platform_tdc =
				le32_to_cpu(ppm->ulSmallACPlatformTDC);
			adev->pm.dpm.dyn_state.ppm_table->apu_tdp =
				le32_to_cpu(ppm->ulApuTDP);
			adev->pm.dpm.dyn_state.ppm_table->dgpu_tdp =
				le32_to_cpu(ppm->ulDGpuTDP);
			adev->pm.dpm.dyn_state.ppm_table->dgpu_ulv_power =
				le32_to_cpu(ppm->ulDGpuUlvPower);
			adev->pm.dpm.dyn_state.ppm_table->tj_max =
				le32_to_cpu(ppm->ulTjmax);
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V6) &&
			ext_hdr->usACPTableOffset) {
			ATOM_PPLIB_ACPClk_Voltage_Limit_Table *limits =
				(ATOM_PPLIB_ACPClk_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usACPTableOffset) + 1);
			ATOM_PPLIB_ACPClk_Voltage_Limit_Record *entry;
			u32 size = limits->numEntries *
				sizeof(struct amdgpu_clock_voltage_dependency_entry);
			adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries =
				kzalloc(size, GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.count =
				limits->numEntries;
			entry = &limits->entries[0];
			for (i = 0; i < limits->numEntries; i++) {
				adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[i].clk =
					le16_to_cpu(entry->usACPClockLow) | (entry->ucACPClockHigh << 16);
				adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[i].v =
					le16_to_cpu(entry->usVoltage);
				entry = (ATOM_PPLIB_ACPClk_Voltage_Limit_Record *)
					((u8 *)entry + sizeof(ATOM_PPLIB_ACPClk_Voltage_Limit_Record));
			}
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7) &&
			ext_hdr->usPowerTuneTableOffset) {
			u8 rev = *(u8 *)(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
			ATOM_PowerTune_Table *pt;
			adev->pm.dpm.dyn_state.cac_tdp_table =
				kzalloc(sizeof(struct amdgpu_cac_tdp_table), GFP_KERNEL);
			if (!adev->pm.dpm.dyn_state.cac_tdp_table) {
				amdgpu_free_extended_power_table(adev);
				return -ENOMEM;
			}
			if (rev > 0) {
				ATOM_PPLIB_POWERTUNE_Table_V1 *ppt = (ATOM_PPLIB_POWERTUNE_Table_V1 *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
				adev->pm.dpm.dyn_state.cac_tdp_table->maximum_power_delivery_limit =
					ppt->usMaximumPowerDeliveryLimit;
				pt = &ppt->power_tune_table;
			} else {
				ATOM_PPLIB_POWERTUNE_Table *ppt = (ATOM_PPLIB_POWERTUNE_Table *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
				adev->pm.dpm.dyn_state.cac_tdp_table->maximum_power_delivery_limit = 255;
				pt = &ppt->power_tune_table;
			}
			adev->pm.dpm.dyn_state.cac_tdp_table->tdp = le16_to_cpu(pt->usTDP);
			adev->pm.dpm.dyn_state.cac_tdp_table->configurable_tdp =
				le16_to_cpu(pt->usConfigurableTDP);
			adev->pm.dpm.dyn_state.cac_tdp_table->tdc = le16_to_cpu(pt->usTDC);
			adev->pm.dpm.dyn_state.cac_tdp_table->battery_power_limit =
				le16_to_cpu(pt->usBatteryPowerLimit);
			adev->pm.dpm.dyn_state.cac_tdp_table->small_power_limit =
				le16_to_cpu(pt->usSmallPowerLimit);
			adev->pm.dpm.dyn_state.cac_tdp_table->low_cac_leakage =
				le16_to_cpu(pt->usLowCACLeakage);
			adev->pm.dpm.dyn_state.cac_tdp_table->high_cac_leakage =
				le16_to_cpu(pt->usHighCACLeakage);
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V8) &&
				ext_hdr->usSclkVddgfxTableOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usSclkVddgfxTableOffset));
			ret = amdgpu_parse_clk_voltage_dep_table(
					&adev->pm.dpm.dyn_state.vddgfx_dependency_on_sclk,
					dep_table);
			if (ret) {
				kfree(adev->pm.dpm.dyn_state.vddgfx_dependency_on_sclk.entries);
				return ret;
			}
		}
	}

	return 0;
}

void amdgpu_free_extended_power_table(struct amdgpu_device *adev)
{
	struct amdgpu_dpm_dynamic_state *dyn_state = &adev->pm.dpm.dyn_state;

	kfree(dyn_state->vddc_dependency_on_sclk.entries);
	kfree(dyn_state->vddci_dependency_on_mclk.entries);
	kfree(dyn_state->vddc_dependency_on_mclk.entries);
	kfree(dyn_state->mvdd_dependency_on_mclk.entries);
	kfree(dyn_state->cac_leakage_table.entries);
	kfree(dyn_state->phase_shedding_limits_table.entries);
	kfree(dyn_state->ppm_table);
	kfree(dyn_state->cac_tdp_table);
	kfree(dyn_state->vce_clock_voltage_dependency_table.entries);
	kfree(dyn_state->uvd_clock_voltage_dependency_table.entries);
	kfree(dyn_state->samu_clock_voltage_dependency_table.entries);
	kfree(dyn_state->acp_clock_voltage_dependency_table.entries);
	kfree(dyn_state->vddgfx_dependency_on_sclk.entries);
}

static const char *pp_lib_thermal_controller_names[] = {
	"NONE",
	"lm63",
	"adm1032",
	"adm1030",
	"max6649",
	"lm64",
	"f75375",
	"RV6xx",
	"RV770",
	"adt7473",
	"NONE",
	"External GPIO",
	"Evergreen",
	"emc2103",
	"Sumo",
	"Northern Islands",
	"Southern Islands",
	"lm96163",
	"Sea Islands",
	"Kaveri/Kabini",
};

void amdgpu_add_thermal_controller(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	ATOM_PPLIB_POWERPLAYTABLE *power_table;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	ATOM_PPLIB_THERMALCONTROLLER *controller;
	struct amdgpu_i2c_bus_rec i2c_bus;
	u16 data_offset;
	u8 frev, crev;

	if (!amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return;
	power_table = (ATOM_PPLIB_POWERPLAYTABLE *)
		(mode_info->atom_context->bios + data_offset);
	controller = &power_table->sThermalController;

	/* add the i2c bus for thermal/fan chip */
	if (controller->ucType > 0) {
		if (controller->ucFanParameters & ATOM_PP_FANPARAMETERS_NOFAN)
			adev->pm.no_fan = true;
		adev->pm.fan_pulses_per_revolution =
			controller->ucFanParameters & ATOM_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;
		if (adev->pm.fan_pulses_per_revolution) {
			adev->pm.fan_min_rpm = controller->ucFanMinRPM;
			adev->pm.fan_max_rpm = controller->ucFanMaxRPM;
		}
		if (controller->ucType == ATOM_PP_THERMALCONTROLLER_RV6xx) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_RV6XX;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_RV770) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_RV770;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_EVERGREEN) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_EVERGREEN;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_SUMO) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_SUMO;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_NISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_NI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_SISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_SI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_CISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_CI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_KAVERI) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_KV;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_EXTERNAL_GPIO) {
			DRM_INFO("External GPIO thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_EXTERNAL_GPIO;
		} else if (controller->ucType ==
			   ATOM_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL) {
			DRM_INFO("ADT7473 with internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_ADT7473_WITH_INTERNAL;
		} else if (controller->ucType ==
			   ATOM_PP_THERMALCONTROLLER_EMC2103_WITH_INTERNAL) {
			DRM_INFO("EMC2103 with internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_EMC2103_WITH_INTERNAL;
		} else if (controller->ucType < ARRAY_SIZE(pp_lib_thermal_controller_names)) {
			DRM_INFO("Possible %s thermal controller at 0x%02x %s fan control\n",
				 pp_lib_thermal_controller_names[controller->ucType],
				 controller->ucI2cAddress >> 1,
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			adev->pm.int_thermal_type = THERMAL_TYPE_EXTERNAL;
			i2c_bus = amdgpu_atombios_lookup_i2c_gpio(adev, controller->ucI2cLine);
			adev->pm.i2c_bus = amdgpu_i2c_lookup(adev, &i2c_bus);
			if (adev->pm.i2c_bus) {
				struct i2c_board_info info = { };
				const char *name = pp_lib_thermal_controller_names[controller->ucType];
				info.addr = controller->ucI2cAddress >> 1;
				strlcpy(info.type, name, sizeof(info.type));
				i2c_new_client_device(&adev->pm.i2c_bus->adapter, &info);
			}
		} else {
			DRM_INFO("Unknown thermal controller type %d at 0x%02x %s fan control\n",
				 controller->ucType,
				 controller->ucI2cAddress >> 1,
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
		}
	}
}

struct amd_vce_state* amdgpu_get_vce_clock_state(void *handle, u32 idx)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (idx < adev->pm.dpm.num_of_vce_states)
		return &adev->pm.dpm.vce_states[idx];

	return NULL;
}

static struct amdgpu_ps *amdgpu_dpm_pick_power_state(struct amdgpu_device *adev,
						     enum amd_pm_state_type dpm_state)
{
	int i;
	struct amdgpu_ps *ps;
	u32 ui_class;
	bool single_display = (adev->pm.dpm.new_active_crtc_count < 2) ?
		true : false;

	/* check if the vblank period is too short to adjust the mclk */
	if (single_display && adev->powerplay.pp_funcs->vblank_too_short) {
		if (amdgpu_dpm_vblank_too_short(adev))
			single_display = false;
	}

	/* certain older asics have a separare 3D performance state,
	 * so try that first if the user selected performance
	 */
	if (dpm_state == POWER_STATE_TYPE_PERFORMANCE)
		dpm_state = POWER_STATE_TYPE_INTERNAL_3DPERF;
	/* balanced states don't exist at the moment */
	if (dpm_state == POWER_STATE_TYPE_BALANCED)
		dpm_state = POWER_STATE_TYPE_PERFORMANCE;

restart_search:
	/* Pick the best power state based on current conditions */
	for (i = 0; i < adev->pm.dpm.num_ps; i++) {
		ps = &adev->pm.dpm.ps[i];
		ui_class = ps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK;
		switch (dpm_state) {
		/* user states */
		case POWER_STATE_TYPE_BATTERY:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		case POWER_STATE_TYPE_BALANCED:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_BALANCED) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		case POWER_STATE_TYPE_PERFORMANCE:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		/* internal states */
		case POWER_STATE_TYPE_INTERNAL_UVD:
			if (adev->pm.dpm.uvd_ps)
				return adev->pm.dpm.uvd_ps;
			else
				break;
		case POWER_STATE_TYPE_INTERNAL_UVD_SD:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_HD:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_HD2:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_MVC:
			if (ps->class2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_BOOT:
			return adev->pm.dpm.boot_ps;
		case POWER_STATE_TYPE_INTERNAL_THERMAL:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_ACPI:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_ACPI)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_ULV:
			if (ps->class2 & ATOM_PPLIB_CLASSIFICATION2_ULV)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_3DPERF:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
				return ps;
			break;
		default:
			break;
		}
	}
	/* use a fallback state if we didn't match */
	switch (dpm_state) {
	case POWER_STATE_TYPE_INTERNAL_UVD_SD:
		dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_HD;
		goto restart_search;
	case POWER_STATE_TYPE_INTERNAL_UVD_HD:
	case POWER_STATE_TYPE_INTERNAL_UVD_HD2:
	case POWER_STATE_TYPE_INTERNAL_UVD_MVC:
		if (adev->pm.dpm.uvd_ps) {
			return adev->pm.dpm.uvd_ps;
		} else {
			dpm_state = POWER_STATE_TYPE_PERFORMANCE;
			goto restart_search;
		}
	case POWER_STATE_TYPE_INTERNAL_THERMAL:
		dpm_state = POWER_STATE_TYPE_INTERNAL_ACPI;
		goto restart_search;
	case POWER_STATE_TYPE_INTERNAL_ACPI:
		dpm_state = POWER_STATE_TYPE_BATTERY;
		goto restart_search;
	case POWER_STATE_TYPE_BATTERY:
	case POWER_STATE_TYPE_BALANCED:
	case POWER_STATE_TYPE_INTERNAL_3DPERF:
		dpm_state = POWER_STATE_TYPE_PERFORMANCE;
		goto restart_search;
	default:
		break;
	}

	return NULL;
}

static int amdgpu_dpm_change_power_state_locked(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	struct amdgpu_ps *ps;
	enum amd_pm_state_type dpm_state;
	int ret;
	bool equal = false;

	/* if dpm init failed */
	if (!adev->pm.dpm_enabled)
		return 0;

	if (adev->pm.dpm.user_state != adev->pm.dpm.state) {
		/* add other state override checks here */
		if ((!adev->pm.dpm.thermal_active) &&
		    (!adev->pm.dpm.uvd_active))
			adev->pm.dpm.state = adev->pm.dpm.user_state;
	}
	dpm_state = adev->pm.dpm.state;

	ps = amdgpu_dpm_pick_power_state(adev, dpm_state);
	if (ps)
		adev->pm.dpm.requested_ps = ps;
	else
		return -EINVAL;

	if (amdgpu_dpm == 1 && pp_funcs->print_power_state) {
		printk("switching from power state:\n");
		amdgpu_dpm_print_power_state(adev, adev->pm.dpm.current_ps);
		printk("switching to power state:\n");
		amdgpu_dpm_print_power_state(adev, adev->pm.dpm.requested_ps);
	}

	/* update whether vce is active */
	ps->vce_active = adev->pm.dpm.vce_active;
	if (pp_funcs->display_configuration_changed)
		amdgpu_dpm_display_configuration_changed(adev);

	ret = amdgpu_dpm_pre_set_power_state(adev);
	if (ret)
		return ret;

	if (pp_funcs->check_state_equal) {
		if (0 != amdgpu_dpm_check_state_equal(adev, adev->pm.dpm.current_ps, adev->pm.dpm.requested_ps, &equal))
			equal = false;
	}

	if (equal)
		return 0;

	if (pp_funcs->set_power_state)
		pp_funcs->set_power_state(adev->powerplay.pp_handle);

	amdgpu_dpm_post_set_power_state(adev);

	adev->pm.dpm.current_active_crtcs = adev->pm.dpm.new_active_crtcs;
	adev->pm.dpm.current_active_crtc_count = adev->pm.dpm.new_active_crtc_count;

	if (pp_funcs->force_performance_level) {
		if (adev->pm.dpm.thermal_active) {
			enum amd_dpm_forced_level level = adev->pm.dpm.forced_level;
			/* force low perf level for thermal */
			pp_funcs->force_performance_level(adev, AMD_DPM_FORCED_LEVEL_LOW);
			/* save the user's level */
			adev->pm.dpm.forced_level = level;
		} else {
			/* otherwise, user selected level */
			pp_funcs->force_performance_level(adev, adev->pm.dpm.forced_level);
		}
	}

	return 0;
}

void amdgpu_legacy_dpm_compute_clocks(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i = 0;

	if (adev->mode_info.num_crtc)
		amdgpu_display_bandwidth_update(adev);

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (ring && ring->sched.ready)
			amdgpu_fence_wait_empty(ring);
	}

	amdgpu_dpm_get_active_displays(adev);

	amdgpu_dpm_change_power_state_locked(adev);
}

void amdgpu_dpm_thermal_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device,
			     pm.dpm.thermal.work);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	/* switch to the thermal state */
	enum amd_pm_state_type dpm_state = POWER_STATE_TYPE_INTERNAL_THERMAL;
	int temp, size = sizeof(temp);

	if (!adev->pm.dpm_enabled)
		return;

	if (!pp_funcs->read_sensor(adev->powerplay.pp_handle,
				   AMDGPU_PP_SENSOR_GPU_TEMP,
				   (void *)&temp,
				   &size)) {
		if (temp < adev->pm.dpm.thermal.min_temp)
			/* switch back the user state */
			dpm_state = adev->pm.dpm.user_state;
	} else {
		if (adev->pm.dpm.thermal.high_to_low)
			/* switch back the user state */
			dpm_state = adev->pm.dpm.user_state;
	}

	if (dpm_state == POWER_STATE_TYPE_INTERNAL_THERMAL)
		adev->pm.dpm.thermal_active = true;
	else
		adev->pm.dpm.thermal_active = false;

	adev->pm.dpm.state = dpm_state;

	amdgpu_legacy_dpm_compute_clocks(adev->powerplay.pp_handle);
}
