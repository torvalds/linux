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
 */

#include <linux/pci.h>
#include <linux/reboot.h>

#include "hwmgr.h"
#include "pp_debug.h"
#include "ppatomctrl.h"
#include "ppsmc.h"
#include "atom.h"
#include "ivsrcid/thm/irqsrcs_thm_9_0.h"
#include "ivsrcid/smuio/irqsrcs_smuio_9_0.h"
#include "ivsrcid/ivsrcid_vislands30.h"

uint8_t convert_to_vid(uint16_t vddc)
{
	return (uint8_t) ((6200 - (vddc * VOLTAGE_SCALE)) / 25);
}

uint16_t convert_to_vddc(uint8_t vid)
{
	return (uint16_t) ((6200 - (vid * 25)) / VOLTAGE_SCALE);
}

int phm_copy_clock_limits_array(
	struct pp_hwmgr *hwmgr,
	uint32_t **pptable_info_array,
	const uint32_t *pptable_array,
	uint32_t power_saving_clock_count)
{
	uint32_t array_size, i;
	uint32_t *table;

	array_size = sizeof(uint32_t) * power_saving_clock_count;
	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < power_saving_clock_count; i++)
		table[i] = le32_to_cpu(pptable_array[i]);

	*pptable_info_array = table;

	return 0;
}

int phm_copy_overdrive_settings_limits_array(
	struct pp_hwmgr *hwmgr,
	uint32_t **pptable_info_array,
	const uint32_t *pptable_array,
	uint32_t od_setting_count)
{
	uint32_t array_size, i;
	uint32_t *table;

	array_size = sizeof(uint32_t) * od_setting_count;
	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < od_setting_count; i++)
		table[i] = le32_to_cpu(pptable_array[i]);

	*pptable_info_array = table;

	return 0;
}

uint32_t phm_set_field_to_u32(u32 offset, u32 original_data, u32 field, u32 size)
{
	u32 mask = 0;
	u32 shift = 0;

	shift = (offset % 4) << 3;
	if (size == sizeof(uint8_t))
		mask = 0xFF << shift;
	else if (size == sizeof(uint16_t))
		mask = 0xFFFF << shift;

	original_data &= ~mask;
	original_data |= (field << shift);
	return original_data;
}

/*
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


/*
 * Returns once the part of the register indicated by the mask has
 * reached the given value.The indirect space is described by giving
 * the memory-mapped index of the indirect index register.
 */
int phm_wait_on_indirect_register(struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask)
{
	if (hwmgr == NULL || hwmgr->device == NULL) {
		pr_err("Invalid Hardware Manager!");
		return -EINVAL;
	}

	cgs_write_register(hwmgr->device, indirect_port, index);
	return phm_wait_on_register(hwmgr, indirect_port + 1, value, mask);
}

int phm_wait_for_register_unequal(struct pp_hwmgr *hwmgr,
					uint32_t index,
					uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	for (i = 0; i < hwmgr->usec_timeout; i++) {
		cur_value = cgs_read_register(hwmgr->device,
									index);
		if ((cur_value & mask) != (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == hwmgr->usec_timeout)
		return -ETIME;
	return 0;
}

int phm_wait_for_indirect_register_unequal(struct pp_hwmgr *hwmgr,
						uint32_t indirect_port,
						uint32_t index,
						uint32_t value,
						uint32_t mask)
{
	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	cgs_write_register(hwmgr->device, indirect_port, index);
	return phm_wait_for_register_unequal(hwmgr, indirect_port + 1,
						value, mask);
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

	pr_debug("vddci is larger than max value in vddci_table\n");
	return vddci_table->entries[i-1].value;
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
	uint8_t entry_id;
	uint8_t voltage_id;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	PP_ASSERT_WITH_CODE(lookup_table->count != 0, "Lookup table is empty", return -EINVAL);

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sckl */
	for (entry_id = 0; entry_id < table_info->vdd_dep_on_sclk->count; entry_id++) {
		voltage_id = table_info->vdd_dep_on_sclk->entries[entry_id].vddInd;
		if (lookup_table->entries[voltage_id].us_vdd == virtual_voltage_id)
			break;
	}

	if (entry_id >= table_info->vdd_dep_on_sclk->count) {
		pr_debug("Can't find requested voltage id in vdd_dep_on_sclk table\n");
		return -EINVAL;
	}

	*sclk = table_info->vdd_dep_on_sclk->entries[entry_id].clk;

	return 0;
}

/**
 * phm_initializa_dynamic_state_adjustment_rule_settings - Initialize Dynamic State Adjustment Rule Settings
 *
 * @hwmgr:  the address of the powerplay hardware manager.
 */
int phm_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr)
{
	struct phm_clock_voltage_dependency_table *table_clk_vlt;
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	/* initialize vddc_dep_on_dal_pwrl table */
	table_clk_vlt = kzalloc(struct_size(table_clk_vlt, entries, 4),
				GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		pr_err("Can not allocate space for vddc_dep_on_dal_pwrl! \n");
		return -ENOMEM;
	} else {
		table_clk_vlt->count = 4;
		table_clk_vlt->entries[0].clk = PP_DAL_POWERLEVEL_ULTRALOW;
		if (hwmgr->chip_id >= CHIP_POLARIS10 &&
		    hwmgr->chip_id <= CHIP_VEGAM)
			table_clk_vlt->entries[0].v = 700;
		else
			table_clk_vlt->entries[0].v = 0;
		table_clk_vlt->entries[1].clk = PP_DAL_POWERLEVEL_LOW;
		if (hwmgr->chip_id >= CHIP_POLARIS10 &&
		    hwmgr->chip_id <= CHIP_VEGAM)
			table_clk_vlt->entries[1].v = 740;
		else
			table_clk_vlt->entries[1].v = 720;
		table_clk_vlt->entries[2].clk = PP_DAL_POWERLEVEL_NOMINAL;
		if (hwmgr->chip_id >= CHIP_POLARIS10 &&
		    hwmgr->chip_id <= CHIP_VEGAM)
			table_clk_vlt->entries[2].v = 800;
		else
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
			smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_VddC_Request,
					req_volt,
					NULL);
			return;
		}
	}
	pr_err("DAL requested level can not"
			" found a available voltage in VDDC DPM Table \n");
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


int phm_irq_process(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *source,
			   struct amdgpu_iv_entry *entry)
{
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;
	uint32_t client_id = entry->client_id;
	uint32_t src_id = entry->src_id;

	if (client_id == AMDGPU_IRQ_CLIENTID_LEGACY) {
		if (src_id == VISLANDS30_IV_SRCID_CG_TSS_THERMAL_LOW_TO_HIGH) {
			schedule_delayed_work(&hwmgr->swctf_delayed_work,
					      msecs_to_jiffies(AMDGPU_SWCTF_EXTRA_DELAY));
		} else if (src_id == VISLANDS30_IV_SRCID_CG_TSS_THERMAL_HIGH_TO_LOW) {
			dev_emerg(adev->dev, "ERROR: GPU under temperature range detected!\n");
		} else if (src_id == VISLANDS30_IV_SRCID_GPIO_19) {
			dev_emerg(adev->dev, "ERROR: GPU HW Critical Temperature Fault(aka CTF) detected!\n");
			/*
			 * HW CTF just occurred. Shutdown to prevent further damage.
			 */
			dev_emerg(adev->dev, "ERROR: System is going to shutdown due to GPU HW CTF!\n");
			orderly_poweroff(true);
		}
	} else if (client_id == SOC15_IH_CLIENTID_THM) {
		if (src_id == 0)
			schedule_delayed_work(&hwmgr->swctf_delayed_work,
					      msecs_to_jiffies(AMDGPU_SWCTF_EXTRA_DELAY));
		else
			dev_emerg(adev->dev, "ERROR: GPU under temperature range detected!\n");
	} else if (client_id == SOC15_IH_CLIENTID_ROM_SMUIO) {
		dev_emerg(adev->dev, "ERROR: GPU HW Critical Temperature Fault(aka CTF) detected!\n");
		/*
		 * HW CTF just occurred. Shutdown to prevent further damage.
		 */
		dev_emerg(adev->dev, "ERROR: System is going to shutdown due to GPU HW CTF!\n");
		orderly_poweroff(true);
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu9_irq_funcs = {
	.process = phm_irq_process,
};

int smu9_register_irq_handlers(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_irq_src *source =
		kzalloc(sizeof(struct amdgpu_irq_src), GFP_KERNEL);

	if (!source)
		return -ENOMEM;

	source->funcs = &smu9_irq_funcs;

	amdgpu_irq_add_id((struct amdgpu_device *)(hwmgr->adev),
			SOC15_IH_CLIENTID_THM,
			THM_9_0__SRCID__THM_DIG_THERM_L2H,
			source);
	amdgpu_irq_add_id((struct amdgpu_device *)(hwmgr->adev),
			SOC15_IH_CLIENTID_THM,
			THM_9_0__SRCID__THM_DIG_THERM_H2L,
			source);

	/* Register CTF(GPIO_19) interrupt */
	amdgpu_irq_add_id((struct amdgpu_device *)(hwmgr->adev),
			SOC15_IH_CLIENTID_ROM_SMUIO,
			SMUIO_9_0__SRCID__SMUIO_GPIO19,
			source);

	return 0;
}

void *smu_atom_get_data_table(void *dev, uint32_t table, uint16_t *size,
						uint8_t *frev, uint8_t *crev)
{
	struct amdgpu_device *adev = dev;
	uint16_t data_start;

	if (amdgpu_atom_parse_data_header(
		    adev->mode_info.atom_context, table, size,
		    frev, crev, &data_start))
		return (uint8_t *)adev->mode_info.atom_context->bios +
			data_start;

	return NULL;
}

int smu_get_voltage_dependency_table_ppt_v1(
			const struct phm_ppt_v1_clock_voltage_dependency_table *allowed_dep_table,
			struct phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint8_t i = 0;
	PP_ASSERT_WITH_CODE((0 != allowed_dep_table->count),
				"Voltage Lookup Table empty",
				return -EINVAL);

	dep_table->count = allowed_dep_table->count;
	for (i = 0; i < dep_table->count; i++) {
		dep_table->entries[i].clk = allowed_dep_table->entries[i].clk;
		dep_table->entries[i].vddInd = allowed_dep_table->entries[i].vddInd;
		dep_table->entries[i].vdd_offset = allowed_dep_table->entries[i].vdd_offset;
		dep_table->entries[i].vddc = allowed_dep_table->entries[i].vddc;
		dep_table->entries[i].vddgfx = allowed_dep_table->entries[i].vddgfx;
		dep_table->entries[i].vddci = allowed_dep_table->entries[i].vddci;
		dep_table->entries[i].mvdd = allowed_dep_table->entries[i].mvdd;
		dep_table->entries[i].phases = allowed_dep_table->entries[i].phases;
		dep_table->entries[i].cks_enable = allowed_dep_table->entries[i].cks_enable;
		dep_table->entries[i].cks_voffset = allowed_dep_table->entries[i].cks_voffset;
	}

	return 0;
}

int smu_set_watermarks_for_clocks_ranges(void *wt_table,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges)
{
	uint32_t i;
	struct watermarks *table = wt_table;

	if (!table || !wm_with_clock_ranges)
		return -EINVAL;

	if (wm_with_clock_ranges->num_wm_dmif_sets > 4 || wm_with_clock_ranges->num_wm_mcif_sets > 4)
		return -EINVAL;

	for (i = 0; i < wm_with_clock_ranges->num_wm_dmif_sets; i++) {
		table->WatermarkRow[1][i].MinClock =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxClock =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MinUclk =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].WmSetting = (uint8_t)
				wm_with_clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
	}

	for (i = 0; i < wm_with_clock_ranges->num_wm_mcif_sets; i++) {
		table->WatermarkRow[0][i].MinClock =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxClock =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MinUclk =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(wm_with_clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].WmSetting = (uint8_t)
				wm_with_clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
	}
	return 0;
}
