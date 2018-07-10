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
#ifndef _SMU_HELPER_H_
#define _SMU_HELPER_H_

struct pp_atomctrl_voltage_table;
struct pp_hwmgr;
struct phm_ppt_v1_voltage_lookup_table;
struct Watermarks_t;
struct pp_wm_sets_with_clock_ranges_soc15;

uint8_t convert_to_vid(uint16_t vddc);
uint16_t convert_to_vddc(uint8_t vid);

struct watermark_row_generic_t {
	uint16_t MinClock;
	uint16_t MaxClock;
	uint16_t MinUclk;
	uint16_t MaxUclk;

	uint8_t  WmSetting;
	uint8_t  Padding[3];
};

struct watermarks {
	struct watermark_row_generic_t WatermarkRow[2][4];
	uint32_t     padding[7];
};

extern int phm_wait_for_register_unequal(struct pp_hwmgr *hwmgr,
					uint32_t index,
					uint32_t value, uint32_t mask);
extern int phm_wait_for_indirect_register_unequal(
				struct pp_hwmgr *hwmgr,
				uint32_t indirect_port, uint32_t index,
				uint32_t value, uint32_t mask);


extern bool phm_cf_want_uvd_power_gating(struct pp_hwmgr *hwmgr);
extern bool phm_cf_want_vce_power_gating(struct pp_hwmgr *hwmgr);
extern bool phm_cf_want_microcode_fan_ctrl(struct pp_hwmgr *hwmgr);

extern int phm_trim_voltage_table(struct pp_atomctrl_voltage_table *vol_table);
extern int phm_get_svi2_mvdd_voltage_table(struct pp_atomctrl_voltage_table *vol_table, phm_ppt_v1_clock_voltage_dependency_table *dep_table);
extern int phm_get_svi2_vddci_voltage_table(struct pp_atomctrl_voltage_table *vol_table, phm_ppt_v1_clock_voltage_dependency_table *dep_table);
extern int phm_get_svi2_vdd_voltage_table(struct pp_atomctrl_voltage_table *vol_table, phm_ppt_v1_voltage_lookup_table *lookup_table);
extern void phm_trim_voltage_table_to_fit_state_table(uint32_t max_vol_steps, struct pp_atomctrl_voltage_table *vol_table);
extern int phm_reset_single_dpm_table(void *table, uint32_t count, int max);
extern void phm_setup_pcie_table_entry(void *table, uint32_t index, uint32_t pcie_gen, uint32_t pcie_lanes);
extern int32_t phm_get_dpm_level_enable_mask_value(void *table);
extern uint8_t phm_get_voltage_id(struct pp_atomctrl_voltage_table *voltage_table,
		uint32_t voltage);
extern uint8_t phm_get_voltage_index(struct phm_ppt_v1_voltage_lookup_table *lookup_table, uint16_t voltage);
extern uint16_t phm_find_closest_vddci(struct pp_atomctrl_voltage_table *vddci_table, uint16_t vddci);
extern int phm_find_boot_level(void *table, uint32_t value, uint32_t *boot_level);
extern int phm_get_sclk_for_voltage_evv(struct pp_hwmgr *hwmgr, phm_ppt_v1_voltage_lookup_table *lookup_table,
								uint16_t virtual_voltage_id, int32_t *sclk);
extern int phm_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr);
extern uint32_t phm_get_lowest_enabled_level(struct pp_hwmgr *hwmgr, uint32_t mask);
extern void phm_apply_dal_min_voltage_request(struct pp_hwmgr *hwmgr);

extern int phm_get_voltage_evv_on_sclk(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint32_t sclk, uint16_t id, uint16_t *voltage);

extern uint32_t phm_set_field_to_u32(u32 offset, u32 original_data, u32 field, u32 size);

extern int phm_wait_on_register(struct pp_hwmgr *hwmgr, uint32_t index,
				uint32_t value, uint32_t mask);

extern int phm_wait_on_indirect_register(struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask);

int phm_irq_process(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *source,
			   struct amdgpu_iv_entry *entry);

int smu9_register_irq_handlers(struct pp_hwmgr *hwmgr);

void *smu_atom_get_data_table(void *dev, uint32_t table, uint16_t *size,
						uint8_t *frev, uint8_t *crev);

int smu_get_voltage_dependency_table_ppt_v1(
	const struct phm_ppt_v1_clock_voltage_dependency_table *allowed_dep_table,
		struct phm_ppt_v1_clock_voltage_dependency_table *dep_table);

int smu_set_watermarks_for_clocks_ranges(void *wt_table,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges);

#define PHM_FIELD_SHIFT(reg, field) reg##__##field##__SHIFT
#define PHM_FIELD_MASK(reg, field) reg##__##field##_MASK

#define PHM_SET_FIELD(origval, reg, field, fieldval)	\
	(((origval) & ~PHM_FIELD_MASK(reg, field)) |	\
	 (PHM_FIELD_MASK(reg, field) & ((fieldval) << PHM_FIELD_SHIFT(reg, field))))

#define PHM_GET_FIELD(value, reg, field)	\
	(((value) & PHM_FIELD_MASK(reg, field)) >>	\
	 PHM_FIELD_SHIFT(reg, field))


/* Operations on named fields. */

#define PHM_READ_FIELD(device, reg, field)	\
	PHM_GET_FIELD(cgs_read_register(device, mm##reg), reg, field)

#define PHM_READ_INDIRECT_FIELD(device, port, reg, field)	\
	PHM_GET_FIELD(cgs_read_ind_register(device, port, ix##reg),	\
			reg, field)

#define PHM_READ_VFPF_INDIRECT_FIELD(device, port, reg, field)	\
	PHM_GET_FIELD(cgs_read_ind_register(device, port, ix##reg),	\
			reg, field)

#define PHM_WRITE_FIELD(device, reg, field, fieldval)	\
	cgs_write_register(device, mm##reg, PHM_SET_FIELD(	\
				cgs_read_register(device, mm##reg), reg, field, fieldval))

#define PHM_WRITE_INDIRECT_FIELD(device, port, reg, field, fieldval)	\
	cgs_write_ind_register(device, port, ix##reg,	\
			PHM_SET_FIELD(cgs_read_ind_register(device, port, ix##reg),	\
				reg, field, fieldval))

#define PHM_WRITE_VFPF_INDIRECT_FIELD(device, port, reg, field, fieldval)	\
	cgs_write_ind_register(device, port, ix##reg,	\
			PHM_SET_FIELD(cgs_read_ind_register(device, port, ix##reg),	\
				reg, field, fieldval))

#define PHM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, index, value, mask)        \
       phm_wait_on_indirect_register(hwmgr, mm##port##_INDEX, index, value, mask)


#define PHM_WAIT_INDIRECT_REGISTER(hwmgr, port, reg, value, mask)      \
       PHM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_INDIRECT_FIELD(hwmgr, port, reg, field, fieldval)	\
	PHM_WAIT_INDIRECT_REGISTER(hwmgr, port, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, index, value, mask)    \
		phm_wait_for_indirect_register_unequal(hwmgr,                   \
				mm##port##_INDEX, index, value, mask)

#define PHM_WAIT_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, value, mask)    \
		PHM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_INDIRECT_FIELD_UNEQUAL(hwmgr, port, reg, field, fieldval)                          \
		PHM_WAIT_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, \
				(fieldval) << PHM_FIELD_SHIFT(reg, field), \
					PHM_FIELD_MASK(reg, field) )


#define PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr,	\
				port, index, value, mask)		\
	phm_wait_for_indirect_register_unequal(hwmgr,			\
		mm##port##_INDEX_11, index, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, value, mask)     \
		PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, port, reg, field, fieldval) \
	PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg,	\
		(fieldval) << PHM_FIELD_SHIFT(reg, field),		\
		PHM_FIELD_MASK(reg, field))


#define PHM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr,		\
				port, index, value, mask)		\
	phm_wait_on_indirect_register(hwmgr,				\
		mm##port##_INDEX_11, index, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER(hwmgr, port, reg, value, mask) \
	PHM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, port, reg, field, fieldval) \
	PHM_WAIT_VFPF_INDIRECT_REGISTER(hwmgr, port, reg,		\
		(fieldval) << PHM_FIELD_SHIFT(reg, field),		\
		PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr,         \
							index, value, mask) \
		phm_wait_for_register_unequal(hwmgr,            \
					index, value, mask)

#define PHM_WAIT_REGISTER_UNEQUAL(hwmgr, reg, value, mask)		\
	PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr,			\
				mm##reg, value, mask)

#define PHM_WAIT_FIELD_UNEQUAL(hwmgr, reg, field, fieldval)		\
	PHM_WAIT_REGISTER_UNEQUAL(hwmgr, reg,				\
		(fieldval) << PHM_FIELD_SHIFT(reg, field),		\
		PHM_FIELD_MASK(reg, field))

#endif /* _SMU_HELPER_H_ */
