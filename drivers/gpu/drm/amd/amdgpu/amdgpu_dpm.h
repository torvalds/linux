/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_DPM_H__
#define __AMDGPU_DPM_H__

enum amdgpu_int_thermal_type {
	THERMAL_TYPE_NONE,
	THERMAL_TYPE_EXTERNAL,
	THERMAL_TYPE_EXTERNAL_GPIO,
	THERMAL_TYPE_RV6XX,
	THERMAL_TYPE_RV770,
	THERMAL_TYPE_ADT7473_WITH_INTERNAL,
	THERMAL_TYPE_EVERGREEN,
	THERMAL_TYPE_SUMO,
	THERMAL_TYPE_NI,
	THERMAL_TYPE_SI,
	THERMAL_TYPE_EMC2103_WITH_INTERNAL,
	THERMAL_TYPE_CI,
	THERMAL_TYPE_KV,
};

enum amdgpu_dpm_auto_throttle_src {
	AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL,
	AMDGPU_DPM_AUTO_THROTTLE_SRC_EXTERNAL
};

enum amdgpu_dpm_event_src {
	AMDGPU_DPM_EVENT_SRC_ANALOG = 0,
	AMDGPU_DPM_EVENT_SRC_EXTERNAL = 1,
	AMDGPU_DPM_EVENT_SRC_DIGITAL = 2,
	AMDGPU_DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,
	AMDGPU_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL = 4
};

struct amdgpu_ps {
	u32 caps; /* vbios flags */
	u32 class; /* vbios flags */
	u32 class2; /* vbios flags */
	/* UVD clocks */
	u32 vclk;
	u32 dclk;
	/* VCE clocks */
	u32 evclk;
	u32 ecclk;
	bool vce_active;
	enum amd_vce_level vce_level;
	/* asic priv */
	void *ps_priv;
};

struct amdgpu_dpm_thermal {
	/* thermal interrupt work */
	struct work_struct work;
	/* low temperature threshold */
	int                min_temp;
	/* high temperature threshold */
	int                max_temp;
	/* edge max emergency(shutdown) temp */
	int                max_edge_emergency_temp;
	/* hotspot low temperature threshold */
	int                min_hotspot_temp;
	/* hotspot high temperature critical threshold */
	int                max_hotspot_crit_temp;
	/* hotspot max emergency(shutdown) temp */
	int                max_hotspot_emergency_temp;
	/* memory low temperature threshold */
	int                min_mem_temp;
	/* memory high temperature critical threshold */
	int                max_mem_crit_temp;
	/* memory max emergency(shutdown) temp */
	int                max_mem_emergency_temp;
	/* was last interrupt low to high or high to low */
	bool               high_to_low;
	/* interrupt source */
	struct amdgpu_irq_src	irq;
};

enum amdgpu_clk_action
{
	AMDGPU_SCLK_UP = 1,
	AMDGPU_SCLK_DOWN
};

struct amdgpu_blacklist_clocks
{
	u32 sclk;
	u32 mclk;
	enum amdgpu_clk_action action;
};

struct amdgpu_clock_and_voltage_limits {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u16 vddci;
};

struct amdgpu_clock_array {
	u32 count;
	u32 *values;
};

struct amdgpu_clock_voltage_dependency_entry {
	u32 clk;
	u16 v;
};

struct amdgpu_clock_voltage_dependency_table {
	u32 count;
	struct amdgpu_clock_voltage_dependency_entry *entries;
};

union amdgpu_cac_leakage_entry {
	struct {
		u16 vddc;
		u32 leakage;
	};
	struct {
		u16 vddc1;
		u16 vddc2;
		u16 vddc3;
	};
};

struct amdgpu_cac_leakage_table {
	u32 count;
	union amdgpu_cac_leakage_entry *entries;
};

struct amdgpu_phase_shedding_limits_entry {
	u16 voltage;
	u32 sclk;
	u32 mclk;
};

struct amdgpu_phase_shedding_limits_table {
	u32 count;
	struct amdgpu_phase_shedding_limits_entry *entries;
};

struct amdgpu_uvd_clock_voltage_dependency_entry {
	u32 vclk;
	u32 dclk;
	u16 v;
};

struct amdgpu_uvd_clock_voltage_dependency_table {
	u8 count;
	struct amdgpu_uvd_clock_voltage_dependency_entry *entries;
};

struct amdgpu_vce_clock_voltage_dependency_entry {
	u32 ecclk;
	u32 evclk;
	u16 v;
};

struct amdgpu_vce_clock_voltage_dependency_table {
	u8 count;
	struct amdgpu_vce_clock_voltage_dependency_entry *entries;
};

struct amdgpu_ppm_table {
	u8 ppm_design;
	u16 cpu_core_number;
	u32 platform_tdp;
	u32 small_ac_platform_tdp;
	u32 platform_tdc;
	u32 small_ac_platform_tdc;
	u32 apu_tdp;
	u32 dgpu_tdp;
	u32 dgpu_ulv_power;
	u32 tj_max;
};

struct amdgpu_cac_tdp_table {
	u16 tdp;
	u16 configurable_tdp;
	u16 tdc;
	u16 battery_power_limit;
	u16 small_power_limit;
	u16 low_cac_leakage;
	u16 high_cac_leakage;
	u16 maximum_power_delivery_limit;
};

struct amdgpu_dpm_dynamic_state {
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_sclk;
	struct amdgpu_clock_voltage_dependency_table vddci_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table mvdd_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table vddc_dependency_on_dispclk;
	struct amdgpu_uvd_clock_voltage_dependency_table uvd_clock_voltage_dependency_table;
	struct amdgpu_vce_clock_voltage_dependency_table vce_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table samu_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table acp_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table vddgfx_dependency_on_sclk;
	struct amdgpu_clock_array valid_sclk_values;
	struct amdgpu_clock_array valid_mclk_values;
	struct amdgpu_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct amdgpu_clock_and_voltage_limits max_clock_voltage_on_ac;
	u32 mclk_sclk_ratio;
	u32 sclk_mclk_delta;
	u16 vddc_vddci_delta;
	u16 min_vddc_for_pcie_gen2;
	struct amdgpu_cac_leakage_table cac_leakage_table;
	struct amdgpu_phase_shedding_limits_table phase_shedding_limits_table;
	struct amdgpu_ppm_table *ppm_table;
	struct amdgpu_cac_tdp_table *cac_tdp_table;
};

struct amdgpu_dpm_fan {
	u16 t_min;
	u16 t_med;
	u16 t_high;
	u16 pwm_min;
	u16 pwm_med;
	u16 pwm_high;
	u8 t_hyst;
	u32 cycle_delay;
	u16 t_max;
	u8 control_mode;
	u16 default_max_fan_pwm;
	u16 default_fan_output_sensitivity;
	u16 fan_output_sensitivity;
	bool ucode_fan_control;
};

enum amdgpu_pcie_gen {
	AMDGPU_PCIE_GEN1 = 0,
	AMDGPU_PCIE_GEN2 = 1,
	AMDGPU_PCIE_GEN3 = 2,
	AMDGPU_PCIE_GEN_INVALID = 0xffff
};

#define amdgpu_dpm_pre_set_power_state(adev) \
		((adev)->powerplay.pp_funcs->pre_set_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_set_power_state(adev) \
		((adev)->powerplay.pp_funcs->set_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_post_set_power_state(adev) \
		((adev)->powerplay.pp_funcs->post_set_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_display_configuration_changed(adev) \
		((adev)->powerplay.pp_funcs->display_configuration_changed((adev)->powerplay.pp_handle))

#define amdgpu_dpm_print_power_state(adev, ps) \
		((adev)->powerplay.pp_funcs->print_power_state((adev)->powerplay.pp_handle, (ps)))

#define amdgpu_dpm_vblank_too_short(adev) \
		((adev)->powerplay.pp_funcs->vblank_too_short((adev)->powerplay.pp_handle))

#define amdgpu_dpm_enable_bapm(adev, e) \
		((adev)->powerplay.pp_funcs->enable_bapm((adev)->powerplay.pp_handle, (e)))

#define amdgpu_dpm_set_fan_control_mode(adev, m) \
		((adev)->powerplay.pp_funcs->set_fan_control_mode((adev)->powerplay.pp_handle, (m)))

#define amdgpu_dpm_get_fan_control_mode(adev) \
		((adev)->powerplay.pp_funcs->get_fan_control_mode((adev)->powerplay.pp_handle))

#define amdgpu_dpm_set_fan_speed_percent(adev, s) \
		((adev)->powerplay.pp_funcs->set_fan_speed_percent((adev)->powerplay.pp_handle, (s)))

#define amdgpu_dpm_get_fan_speed_percent(adev, s) \
		((adev)->powerplay.pp_funcs->get_fan_speed_percent((adev)->powerplay.pp_handle, (s)))

#define amdgpu_dpm_get_fan_speed_rpm(adev, s) \
		((adev)->powerplay.pp_funcs->get_fan_speed_rpm)((adev)->powerplay.pp_handle, (s))

#define amdgpu_dpm_set_fan_speed_rpm(adev, s) \
		((adev)->powerplay.pp_funcs->set_fan_speed_rpm)((adev)->powerplay.pp_handle, (s))

#define amdgpu_dpm_force_performance_level(adev, l) \
		((adev)->powerplay.pp_funcs->force_performance_level((adev)->powerplay.pp_handle, (l)))

#define amdgpu_dpm_get_current_power_state(adev) \
		((adev)->powerplay.pp_funcs->get_current_power_state((adev)->powerplay.pp_handle))

#define amdgpu_dpm_get_pp_num_states(adev, data) \
		((adev)->powerplay.pp_funcs->get_pp_num_states((adev)->powerplay.pp_handle, data))

#define amdgpu_dpm_get_pp_table(adev, table) \
		((adev)->powerplay.pp_funcs->get_pp_table((adev)->powerplay.pp_handle, table))

#define amdgpu_dpm_set_pp_table(adev, buf, size) \
		((adev)->powerplay.pp_funcs->set_pp_table((adev)->powerplay.pp_handle, buf, size))

#define amdgpu_dpm_print_clock_levels(adev, type, buf) \
		((adev)->powerplay.pp_funcs->print_clock_levels((adev)->powerplay.pp_handle, type, buf))

#define amdgpu_dpm_force_clock_level(adev, type, level) \
		((adev)->powerplay.pp_funcs->force_clock_level((adev)->powerplay.pp_handle, type, level))

#define amdgpu_dpm_get_sclk_od(adev) \
		((adev)->powerplay.pp_funcs->get_sclk_od((adev)->powerplay.pp_handle))

#define amdgpu_dpm_set_sclk_od(adev, value) \
		((adev)->powerplay.pp_funcs->set_sclk_od((adev)->powerplay.pp_handle, value))

#define amdgpu_dpm_get_mclk_od(adev) \
		((adev)->powerplay.pp_funcs->get_mclk_od((adev)->powerplay.pp_handle))

#define amdgpu_dpm_set_mclk_od(adev, value) \
		((adev)->powerplay.pp_funcs->set_mclk_od((adev)->powerplay.pp_handle, value))

#define amdgpu_dpm_dispatch_task(adev, task_id, user_state)		\
		((adev)->powerplay.pp_funcs->dispatch_tasks)((adev)->powerplay.pp_handle, (task_id), (user_state))

#define amdgpu_dpm_check_state_equal(adev, cps, rps, equal) \
		((adev)->powerplay.pp_funcs->check_state_equal((adev)->powerplay.pp_handle, (cps), (rps), (equal)))

#define amdgpu_dpm_get_vce_clock_state(adev, i)				\
		((adev)->powerplay.pp_funcs->get_vce_clock_state((adev)->powerplay.pp_handle, (i)))

#define amdgpu_dpm_get_performance_level(adev)				\
		((adev)->powerplay.pp_funcs->get_performance_level((adev)->powerplay.pp_handle))

#define amdgpu_dpm_reset_power_profile_state(adev, request) \
		((adev)->powerplay.pp_funcs->reset_power_profile_state(\
			(adev)->powerplay.pp_handle, request))

#define amdgpu_dpm_switch_power_profile(adev, type, en) \
		((adev)->powerplay.pp_funcs->switch_power_profile(\
			(adev)->powerplay.pp_handle, type, en))

#define amdgpu_dpm_set_clockgating_by_smu(adev, msg_id) \
		((adev)->powerplay.pp_funcs->set_clockgating_by_smu(\
			(adev)->powerplay.pp_handle, msg_id))

#define amdgpu_dpm_get_power_profile_mode(adev, buf) \
		((adev)->powerplay.pp_funcs->get_power_profile_mode(\
			(adev)->powerplay.pp_handle, buf))

#define amdgpu_dpm_set_power_profile_mode(adev, parameter, size) \
		((adev)->powerplay.pp_funcs->set_power_profile_mode(\
			(adev)->powerplay.pp_handle, parameter, size))

#define amdgpu_dpm_odn_edit_dpm_table(adev, type, parameter, size) \
		((adev)->powerplay.pp_funcs->odn_edit_dpm_table(\
			(adev)->powerplay.pp_handle, type, parameter, size))

#define amdgpu_dpm_enable_mgpu_fan_boost(adev) \
		((adev)->powerplay.pp_funcs->enable_mgpu_fan_boost(\
			(adev)->powerplay.pp_handle))

#define amdgpu_dpm_get_ppfeature_status(adev, buf) \
		((adev)->powerplay.pp_funcs->get_ppfeature_status(\
			(adev)->powerplay.pp_handle, (buf)))

#define amdgpu_dpm_set_ppfeature_status(adev, ppfeatures) \
		((adev)->powerplay.pp_funcs->set_ppfeature_status(\
			(adev)->powerplay.pp_handle, (ppfeatures)))

struct amdgpu_dpm {
	struct amdgpu_ps        *ps;
	/* number of valid power states */
	int                     num_ps;
	/* current power state that is active */
	struct amdgpu_ps        *current_ps;
	/* requested power state */
	struct amdgpu_ps        *requested_ps;
	/* boot up power state */
	struct amdgpu_ps        *boot_ps;
	/* default uvd power state */
	struct amdgpu_ps        *uvd_ps;
	/* vce requirements */
	u32                  num_of_vce_states;
	struct amd_vce_state vce_states[AMD_MAX_VCE_LEVELS];
	enum amd_vce_level vce_level;
	enum amd_pm_state_type state;
	enum amd_pm_state_type user_state;
	enum amd_pm_state_type last_state;
	enum amd_pm_state_type last_user_state;
	u32                     platform_caps;
	u32                     voltage_response_time;
	u32                     backbias_response_time;
	void                    *priv;
	u32			new_active_crtcs;
	int			new_active_crtc_count;
	u32			current_active_crtcs;
	int			current_active_crtc_count;
	struct amdgpu_dpm_dynamic_state dyn_state;
	struct amdgpu_dpm_fan fan;
	u32 tdp_limit;
	u32 near_tdp_limit;
	u32 near_tdp_limit_adjusted;
	u32 sq_ramping_threshold;
	u32 cac_leakage;
	u16 tdp_od_limit;
	u32 tdp_adjustment;
	u16 load_line_slope;
	bool power_control;
	/* special states active */
	bool                    thermal_active;
	bool                    uvd_active;
	bool                    vce_active;
	/* thermal handling */
	struct amdgpu_dpm_thermal thermal;
	/* forced levels */
	enum amd_dpm_forced_level forced_level;
};

struct amdgpu_pm {
	struct mutex		mutex;
	u32                     current_sclk;
	u32                     current_mclk;
	u32                     default_sclk;
	u32                     default_mclk;
	struct amdgpu_i2c_chan *i2c_bus;
	/* internal thermal controller on rv6xx+ */
	enum amdgpu_int_thermal_type int_thermal_type;
	struct device	        *int_hwmon_dev;
	/* fan control parameters */
	bool                    no_fan;
	u8                      fan_pulses_per_revolution;
	u8                      fan_min_rpm;
	u8                      fan_max_rpm;
	/* dpm */
	bool                    dpm_enabled;
	bool                    sysfs_initialized;
	struct amdgpu_dpm       dpm;
	const struct firmware	*fw;	/* SMC firmware */
	uint32_t                fw_version;
	uint32_t                pcie_gen_mask;
	uint32_t                pcie_mlw_mask;
	struct amd_pp_display_configuration pm_display_cfg;/* set by dc */
	uint32_t                smu_prv_buffer_size;
	struct amdgpu_bo        *smu_prv_buffer;
	bool ac_power;
	/* powerplay feature */
	uint32_t pp_feature;

};

#define R600_SSTU_DFLT                               0
#define R600_SST_DFLT                                0x00C8

/* XXX are these ok? */
#define R600_TEMP_RANGE_MIN (90 * 1000)
#define R600_TEMP_RANGE_MAX (120 * 1000)

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5

enum amdgpu_td {
	AMDGPU_TD_AUTO,
	AMDGPU_TD_UP,
	AMDGPU_TD_DOWN,
};

enum amdgpu_display_watermark {
	AMDGPU_DISPLAY_WATERMARK_LOW = 0,
	AMDGPU_DISPLAY_WATERMARK_HIGH = 1,
};

enum amdgpu_display_gap
{
    AMDGPU_PM_DISPLAY_GAP_VBLANK_OR_WM = 0,
    AMDGPU_PM_DISPLAY_GAP_VBLANK       = 1,
    AMDGPU_PM_DISPLAY_GAP_WATERMARK    = 2,
    AMDGPU_PM_DISPLAY_GAP_IGNORE       = 3,
};

void amdgpu_dpm_print_class_info(u32 class, u32 class2);
void amdgpu_dpm_print_cap_info(u32 caps);
void amdgpu_dpm_print_ps_status(struct amdgpu_device *adev,
				struct amdgpu_ps *rps);
u32 amdgpu_dpm_get_vblank_time(struct amdgpu_device *adev);
u32 amdgpu_dpm_get_vrefresh(struct amdgpu_device *adev);
void amdgpu_dpm_get_active_displays(struct amdgpu_device *adev);
int amdgpu_dpm_read_sensor(struct amdgpu_device *adev, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);

bool amdgpu_is_internal_thermal_sensor(enum amdgpu_int_thermal_type sensor);

int amdgpu_get_platform_caps(struct amdgpu_device *adev);

int amdgpu_parse_extended_power_table(struct amdgpu_device *adev);
void amdgpu_free_extended_power_table(struct amdgpu_device *adev);

void amdgpu_add_thermal_controller(struct amdgpu_device *adev);

enum amdgpu_pcie_gen amdgpu_get_pcie_gen_support(struct amdgpu_device *adev,
						 u32 sys_mask,
						 enum amdgpu_pcie_gen asic_gen,
						 enum amdgpu_pcie_gen default_gen);

struct amd_vce_state*
amdgpu_get_vce_clock_state(void *handle, u32 idx);

int amdgpu_dpm_set_powergating_by_smu(struct amdgpu_device *adev,
				      uint32_t block_type, bool gate);

extern int amdgpu_dpm_get_sclk(struct amdgpu_device *adev, bool low);

extern int amdgpu_dpm_get_mclk(struct amdgpu_device *adev, bool low);

#endif
