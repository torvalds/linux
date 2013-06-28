/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
#ifndef __R600_DPM_H__
#define __R600_DPM_H__

#define R600_ASI_DFLT                                10000
#define R600_BSP_DFLT                                0x41EB
#define R600_BSU_DFLT                                0x2
#define R600_AH_DFLT                                 5
#define R600_RLP_DFLT                                25
#define R600_RMP_DFLT                                65
#define R600_LHP_DFLT                                40
#define R600_LMP_DFLT                                15
#define R600_TD_DFLT                                 0
#define R600_UTC_DFLT_00                             0x24
#define R600_UTC_DFLT_01                             0x22
#define R600_UTC_DFLT_02                             0x22
#define R600_UTC_DFLT_03                             0x22
#define R600_UTC_DFLT_04                             0x22
#define R600_UTC_DFLT_05                             0x22
#define R600_UTC_DFLT_06                             0x22
#define R600_UTC_DFLT_07                             0x22
#define R600_UTC_DFLT_08                             0x22
#define R600_UTC_DFLT_09                             0x22
#define R600_UTC_DFLT_10                             0x22
#define R600_UTC_DFLT_11                             0x22
#define R600_UTC_DFLT_12                             0x22
#define R600_UTC_DFLT_13                             0x22
#define R600_UTC_DFLT_14                             0x22
#define R600_DTC_DFLT_00                             0x24
#define R600_DTC_DFLT_01                             0x22
#define R600_DTC_DFLT_02                             0x22
#define R600_DTC_DFLT_03                             0x22
#define R600_DTC_DFLT_04                             0x22
#define R600_DTC_DFLT_05                             0x22
#define R600_DTC_DFLT_06                             0x22
#define R600_DTC_DFLT_07                             0x22
#define R600_DTC_DFLT_08                             0x22
#define R600_DTC_DFLT_09                             0x22
#define R600_DTC_DFLT_10                             0x22
#define R600_DTC_DFLT_11                             0x22
#define R600_DTC_DFLT_12                             0x22
#define R600_DTC_DFLT_13                             0x22
#define R600_DTC_DFLT_14                             0x22
#define R600_VRC_DFLT                                0x0000C003
#define R600_VOLTAGERESPONSETIME_DFLT                1000
#define R600_BACKBIASRESPONSETIME_DFLT               1000
#define R600_VRU_DFLT                                0x3
#define R600_SPLLSTEPTIME_DFLT                       0x1000
#define R600_SPLLSTEPUNIT_DFLT                       0x3
#define R600_TPU_DFLT                                0
#define R600_TPC_DFLT                                0x200
#define R600_SSTU_DFLT                               0
#define R600_SST_DFLT                                0x00C8
#define R600_GICST_DFLT                              0x200
#define R600_FCT_DFLT                                0x0400
#define R600_FCTU_DFLT                               0
#define R600_CTXCGTT3DRPHC_DFLT                      0x20
#define R600_CTXCGTT3DRSDC_DFLT                      0x40
#define R600_VDDC3DOORPHC_DFLT                       0x100
#define R600_VDDC3DOORSDC_DFLT                       0x7
#define R600_VDDC3DOORSU_DFLT                        0
#define R600_MPLLLOCKTIME_DFLT                       100
#define R600_MPLLRESETTIME_DFLT                      150
#define R600_VCOSTEPPCT_DFLT                          20
#define R600_ENDINGVCOSTEPPCT_DFLT                    5
#define R600_REFERENCEDIVIDER_DFLT                    4

#define R600_PM_NUMBER_OF_TC 15
#define R600_PM_NUMBER_OF_SCLKS 20
#define R600_PM_NUMBER_OF_MCLKS 4
#define R600_PM_NUMBER_OF_VOLTAGE_LEVELS 4
#define R600_PM_NUMBER_OF_ACTIVITY_LEVELS 3

/* XXX are these ok? */
#define R600_TEMP_RANGE_MIN (90 * 1000)
#define R600_TEMP_RANGE_MAX (120 * 1000)

enum r600_power_level {
	R600_POWER_LEVEL_LOW = 0,
	R600_POWER_LEVEL_MEDIUM = 1,
	R600_POWER_LEVEL_HIGH = 2,
	R600_POWER_LEVEL_CTXSW = 3,
};

enum r600_td {
	R600_TD_AUTO,
	R600_TD_UP,
	R600_TD_DOWN,
};

enum r600_display_watermark {
	R600_DISPLAY_WATERMARK_LOW = 0,
	R600_DISPLAY_WATERMARK_HIGH = 1,
};

enum r600_display_gap
{
    R600_PM_DISPLAY_GAP_VBLANK_OR_WM = 0,
    R600_PM_DISPLAY_GAP_VBLANK       = 1,
    R600_PM_DISPLAY_GAP_WATERMARK    = 2,
    R600_PM_DISPLAY_GAP_IGNORE       = 3,
};

extern const u32 r600_utc[R600_PM_NUMBER_OF_TC];
extern const u32 r600_dtc[R600_PM_NUMBER_OF_TC];

void r600_dpm_print_class_info(u32 class, u32 class2);
void r600_dpm_print_cap_info(u32 caps);
void r600_dpm_print_ps_status(struct radeon_device *rdev,
			      struct radeon_ps *rps);
bool r600_is_uvd_state(u32 class, u32 class2);
void r600_calculate_u_and_p(u32 i, u32 r_c, u32 p_b,
			    u32 *p, u32 *u);
int r600_calculate_at(u32 t, u32 h, u32 fh, u32 fl, u32 *tl, u32 *th);
void r600_gfx_clockgating_enable(struct radeon_device *rdev, bool enable);
void r600_dynamicpm_enable(struct radeon_device *rdev, bool enable);
void r600_enable_thermal_protection(struct radeon_device *rdev, bool enable);
void r600_enable_acpi_pm(struct radeon_device *rdev);
void r600_enable_dynamic_pcie_gen2(struct radeon_device *rdev, bool enable);
bool r600_dynamicpm_enabled(struct radeon_device *rdev);
void r600_enable_sclk_control(struct radeon_device *rdev, bool enable);
void r600_enable_mclk_control(struct radeon_device *rdev, bool enable);
void r600_enable_spll_bypass(struct radeon_device *rdev, bool enable);
void r600_wait_for_spll_change(struct radeon_device *rdev);
void r600_set_bsp(struct radeon_device *rdev, u32 u, u32 p);
void r600_set_at(struct radeon_device *rdev,
		 u32 l_to_m, u32 m_to_h,
		 u32 h_to_m, u32 m_to_l);
void r600_set_tc(struct radeon_device *rdev, u32 index, u32 u_t, u32 d_t);
void r600_select_td(struct radeon_device *rdev, enum r600_td td);
void r600_set_vrc(struct radeon_device *rdev, u32 vrv);
void r600_set_tpu(struct radeon_device *rdev, u32 u);
void r600_set_tpc(struct radeon_device *rdev, u32 c);
void r600_set_sstu(struct radeon_device *rdev, u32 u);
void r600_set_sst(struct radeon_device *rdev, u32 t);
void r600_set_git(struct radeon_device *rdev, u32 t);
void r600_set_fctu(struct radeon_device *rdev, u32 u);
void r600_set_fct(struct radeon_device *rdev, u32 t);
void r600_set_ctxcgtt3d_rphc(struct radeon_device *rdev, u32 p);
void r600_set_ctxcgtt3d_rsdc(struct radeon_device *rdev, u32 s);
void r600_set_vddc3d_oorsu(struct radeon_device *rdev, u32 u);
void r600_set_vddc3d_oorphc(struct radeon_device *rdev, u32 p);
void r600_set_vddc3d_oorsdc(struct radeon_device *rdev, u32 s);
void r600_set_mpll_lock_time(struct radeon_device *rdev, u32 lock_time);
void r600_set_mpll_reset_time(struct radeon_device *rdev, u32 reset_time);
void r600_engine_clock_entry_enable(struct radeon_device *rdev,
				    u32 index, bool enable);
void r600_engine_clock_entry_enable_pulse_skipping(struct radeon_device *rdev,
						   u32 index, bool enable);
void r600_engine_clock_entry_enable_post_divider(struct radeon_device *rdev,
						 u32 index, bool enable);
void r600_engine_clock_entry_set_post_divider(struct radeon_device *rdev,
					      u32 index, u32 divider);
void r600_engine_clock_entry_set_reference_divider(struct radeon_device *rdev,
						   u32 index, u32 divider);
void r600_engine_clock_entry_set_feedback_divider(struct radeon_device *rdev,
						  u32 index, u32 divider);
void r600_engine_clock_entry_set_step_time(struct radeon_device *rdev,
					   u32 index, u32 step_time);
void r600_vid_rt_set_ssu(struct radeon_device *rdev, u32 u);
void r600_vid_rt_set_vru(struct radeon_device *rdev, u32 u);
void r600_vid_rt_set_vrt(struct radeon_device *rdev, u32 rt);
void r600_voltage_control_enable_pins(struct radeon_device *rdev,
				      u64 mask);
void r600_voltage_control_program_voltages(struct radeon_device *rdev,
					   enum r600_power_level index, u64 pins);
void r600_voltage_control_deactivate_static_control(struct radeon_device *rdev,
						    u64 mask);
void r600_power_level_enable(struct radeon_device *rdev,
			     enum r600_power_level index, bool enable);
void r600_power_level_set_voltage_index(struct radeon_device *rdev,
					enum r600_power_level index, u32 voltage_index);
void r600_power_level_set_mem_clock_index(struct radeon_device *rdev,
					  enum r600_power_level index, u32 mem_clock_index);
void r600_power_level_set_eng_clock_index(struct radeon_device *rdev,
					  enum r600_power_level index, u32 eng_clock_index);
void r600_power_level_set_watermark_id(struct radeon_device *rdev,
				       enum r600_power_level index,
				       enum r600_display_watermark watermark_id);
void r600_power_level_set_pcie_gen2(struct radeon_device *rdev,
				    enum r600_power_level index, bool compatible);
enum r600_power_level r600_power_level_get_current_index(struct radeon_device *rdev);
enum r600_power_level r600_power_level_get_target_index(struct radeon_device *rdev);
void r600_power_level_set_enter_index(struct radeon_device *rdev,
				      enum r600_power_level index);
void r600_wait_for_power_level_unequal(struct radeon_device *rdev,
				       enum r600_power_level index);
void r600_wait_for_power_level(struct radeon_device *rdev,
			       enum r600_power_level index);
void r600_start_dpm(struct radeon_device *rdev);
void r600_stop_dpm(struct radeon_device *rdev);

int r600_set_thermal_temperature_range(struct radeon_device *rdev,
				       int min_temp, int max_temp);
bool r600_is_internal_thermal_sensor(enum radeon_int_thermal_type sensor);

int r600_parse_extended_power_table(struct radeon_device *rdev);
void r600_free_extended_power_table(struct radeon_device *rdev);

enum radeon_pcie_gen r600_get_pcie_gen_support(struct radeon_device *rdev,
					       u32 sys_mask,
					       enum radeon_pcie_gen asic_gen,
					       enum radeon_pcie_gen default_gen);

#endif
