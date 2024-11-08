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

/* Argument for PPSMC_MSG_GpuChangeState */
enum gfx_change_state {
	sGpuChangeState_D0Entry = 1,
	sGpuChangeState_D3Entry,
};

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

enum amdgpu_runpm_mode {
	AMDGPU_RUNPM_NONE,
	AMDGPU_RUNPM_PX,
	AMDGPU_RUNPM_BOCO,
	AMDGPU_RUNPM_BACO,
	AMDGPU_RUNPM_BAMACO,
};

#define BACO_SUPPORT (1<<0)
#define MACO_SUPPORT (1<<1)

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
	/* SWCTF threshold */
	int                sw_ctf_threshold;
	/* was last interrupt low to high or high to low */
	bool               high_to_low;
	/* interrupt source */
	struct amdgpu_irq_src	irq;
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

enum ip_power_state {
	POWER_STATE_UNKNOWN,
	POWER_STATE_ON,
	POWER_STATE_OFF,
};

/* Used to mask smu debug modes */
#define SMU_DEBUG_HALT_ON_ERROR		0x1

#define MAX_SMU_I2C_BUSES       2

struct amdgpu_smu_i2c_bus {
	struct i2c_adapter adapter;
	struct amdgpu_device *adev;
	int port;
	struct mutex mutex;
};

struct config_table_setting
{
	uint16_t gfxclk_average_tau;
	uint16_t socclk_average_tau;
	uint16_t uclk_average_tau;
	uint16_t gfx_activity_average_tau;
	uint16_t mem_activity_average_tau;
	uint16_t socket_power_average_tau;
	uint16_t apu_socket_power_average_tau;
	uint16_t fclk_average_tau;
};

#define OD_OPS_SUPPORT_FAN_CURVE_RETRIEVE		BIT(0)
#define OD_OPS_SUPPORT_FAN_CURVE_SET			BIT(1)
#define OD_OPS_SUPPORT_ACOUSTIC_LIMIT_THRESHOLD_RETRIEVE	BIT(2)
#define OD_OPS_SUPPORT_ACOUSTIC_LIMIT_THRESHOLD_SET		BIT(3)
#define OD_OPS_SUPPORT_ACOUSTIC_TARGET_THRESHOLD_RETRIEVE	BIT(4)
#define OD_OPS_SUPPORT_ACOUSTIC_TARGET_THRESHOLD_SET		BIT(5)
#define OD_OPS_SUPPORT_FAN_TARGET_TEMPERATURE_RETRIEVE		BIT(6)
#define OD_OPS_SUPPORT_FAN_TARGET_TEMPERATURE_SET		BIT(7)
#define OD_OPS_SUPPORT_FAN_MINIMUM_PWM_RETRIEVE		BIT(8)
#define OD_OPS_SUPPORT_FAN_MINIMUM_PWM_SET		BIT(9)
#define OD_OPS_SUPPORT_FAN_ZERO_RPM_ENABLE_RETRIEVE	BIT(10)
#define OD_OPS_SUPPORT_FAN_ZERO_RPM_ENABLE_SET		BIT(11)
#define OD_OPS_SUPPORT_FAN_ZERO_RPM_STOP_TEMP_RETRIEVE	BIT(12)
#define OD_OPS_SUPPORT_FAN_ZERO_RPM_STOP_TEMP_SET	BIT(13)

struct amdgpu_pm {
	struct mutex		mutex;
	u32                     current_sclk;
	u32                     current_mclk;
	u32                     default_sclk;
	u32                     default_mclk;
	struct amdgpu_i2c_chan *i2c_bus;
	bool                    bus_locked;
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

	/* Used for I2C access to various EEPROMs on relevant ASICs */
	struct amdgpu_smu_i2c_bus smu_i2c[MAX_SMU_I2C_BUSES];
	struct i2c_adapter     *ras_eeprom_i2c_bus;
	struct i2c_adapter     *fru_eeprom_i2c_bus;
	struct list_head	pm_attr_list;

	atomic_t		pwr_state[AMD_IP_BLOCK_TYPE_NUM];

	/*
	 * 0 = disabled (default), otherwise enable corresponding debug mode
	 */
	uint32_t		smu_debug_mask;

	bool			pp_force_state_enabled;

	struct mutex            stable_pstate_ctx_lock;
	struct amdgpu_ctx       *stable_pstate_ctx;

	struct config_table_setting config_table;
	/* runtime mode */
	enum amdgpu_runpm_mode rpm_mode;

	struct list_head	od_kobj_list;
	uint32_t		od_feature_mask;
};

int amdgpu_dpm_read_sensor(struct amdgpu_device *adev, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);

int amdgpu_dpm_get_apu_thermal_limit(struct amdgpu_device *adev, uint32_t *limit);
int amdgpu_dpm_set_apu_thermal_limit(struct amdgpu_device *adev, uint32_t limit);

int amdgpu_dpm_set_powergating_by_smu(struct amdgpu_device *adev,
				      uint32_t block_type, bool gate);

extern int amdgpu_dpm_get_sclk(struct amdgpu_device *adev, bool low);

extern int amdgpu_dpm_get_mclk(struct amdgpu_device *adev, bool low);

int amdgpu_dpm_set_xgmi_pstate(struct amdgpu_device *adev,
			       uint32_t pstate);

int amdgpu_dpm_switch_power_profile(struct amdgpu_device *adev,
				    enum PP_SMC_POWER_PROFILE type,
				    bool en);

int amdgpu_dpm_baco_reset(struct amdgpu_device *adev);

int amdgpu_dpm_mode2_reset(struct amdgpu_device *adev);
int amdgpu_dpm_enable_gfx_features(struct amdgpu_device *adev);

int amdgpu_dpm_is_baco_supported(struct amdgpu_device *adev);

bool amdgpu_dpm_is_mode1_reset_supported(struct amdgpu_device *adev);
int amdgpu_dpm_mode1_reset(struct amdgpu_device *adev);

int amdgpu_dpm_set_mp1_state(struct amdgpu_device *adev,
			     enum pp_mp1_state mp1_state);

int amdgpu_dpm_notify_rlc_state(struct amdgpu_device *adev, bool en);

int amdgpu_dpm_set_gfx_power_up_by_imu(struct amdgpu_device *adev);

int amdgpu_dpm_baco_exit(struct amdgpu_device *adev);

int amdgpu_dpm_baco_enter(struct amdgpu_device *adev);

int amdgpu_dpm_set_df_cstate(struct amdgpu_device *adev,
			     uint32_t cstate);

int amdgpu_dpm_enable_mgpu_fan_boost(struct amdgpu_device *adev);

int amdgpu_dpm_set_clockgating_by_smu(struct amdgpu_device *adev,
				      uint32_t msg_id);

int amdgpu_dpm_smu_i2c_bus_access(struct amdgpu_device *adev,
				  bool acquire);

void amdgpu_pm_acpi_event_handler(struct amdgpu_device *adev);

void amdgpu_dpm_compute_clocks(struct amdgpu_device *adev);
void amdgpu_dpm_enable_uvd(struct amdgpu_device *adev, bool enable);
void amdgpu_dpm_enable_vce(struct amdgpu_device *adev, bool enable);
void amdgpu_dpm_enable_jpeg(struct amdgpu_device *adev, bool enable);
void amdgpu_dpm_enable_vpe(struct amdgpu_device *adev, bool enable);
int amdgpu_pm_load_smu_firmware(struct amdgpu_device *adev, uint32_t *smu_version);
int amdgpu_dpm_handle_passthrough_sbr(struct amdgpu_device *adev, bool enable);
int amdgpu_dpm_send_hbm_bad_pages_num(struct amdgpu_device *adev, uint32_t size);
int amdgpu_dpm_send_hbm_bad_channel_flag(struct amdgpu_device *adev, uint32_t size);
int amdgpu_dpm_send_rma_reason(struct amdgpu_device *adev);
int amdgpu_dpm_get_dpm_freq_range(struct amdgpu_device *adev,
				       enum pp_clock_type type,
				       uint32_t *min,
				       uint32_t *max);
int amdgpu_dpm_set_soft_freq_range(struct amdgpu_device *adev,
				        enum pp_clock_type type,
				        uint32_t min,
				        uint32_t max);
int amdgpu_dpm_write_watermarks_table(struct amdgpu_device *adev);
int amdgpu_dpm_wait_for_event(struct amdgpu_device *adev, enum smu_event_type event,
		       uint64_t event_arg);
int amdgpu_dpm_get_residency_gfxoff(struct amdgpu_device *adev, u32 *value);
int amdgpu_dpm_set_residency_gfxoff(struct amdgpu_device *adev, bool value);
int amdgpu_dpm_get_entrycount_gfxoff(struct amdgpu_device *adev, u64 *value);
int amdgpu_dpm_get_status_gfxoff(struct amdgpu_device *adev, uint32_t *value);
uint64_t amdgpu_dpm_get_thermal_throttling_counter(struct amdgpu_device *adev);
void amdgpu_dpm_gfx_state_change(struct amdgpu_device *adev,
				 enum gfx_change_state state);
int amdgpu_dpm_get_ecc_info(struct amdgpu_device *adev,
			    void *umc_ecc);
struct amd_vce_state *amdgpu_dpm_get_vce_clock_state(struct amdgpu_device *adev,
						     uint32_t idx);
void amdgpu_dpm_get_current_power_state(struct amdgpu_device *adev, enum amd_pm_state_type *state);
void amdgpu_dpm_set_power_state(struct amdgpu_device *adev,
				enum amd_pm_state_type state);
enum amd_dpm_forced_level amdgpu_dpm_get_performance_level(struct amdgpu_device *adev);
int amdgpu_dpm_force_performance_level(struct amdgpu_device *adev,
				       enum amd_dpm_forced_level level);
int amdgpu_dpm_get_pp_num_states(struct amdgpu_device *adev,
				 struct pp_states_info *states);
int amdgpu_dpm_dispatch_task(struct amdgpu_device *adev,
			      enum amd_pp_task task_id,
			      enum amd_pm_state_type *user_state);
int amdgpu_dpm_get_pp_table(struct amdgpu_device *adev, char **table);
int amdgpu_dpm_set_fine_grain_clk_vol(struct amdgpu_device *adev,
				      uint32_t type,
				      long *input,
				      uint32_t size);
int amdgpu_dpm_odn_edit_dpm_table(struct amdgpu_device *adev,
				  uint32_t type,
				  long *input,
				  uint32_t size);
int amdgpu_dpm_print_clock_levels(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  char *buf);
int amdgpu_dpm_emit_clock_levels(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  char *buf,
				  int *offset);
int amdgpu_dpm_set_ppfeature_status(struct amdgpu_device *adev,
				    uint64_t ppfeature_masks);
int amdgpu_dpm_get_ppfeature_status(struct amdgpu_device *adev, char *buf);
int amdgpu_dpm_force_clock_level(struct amdgpu_device *adev,
				 enum pp_clock_type type,
				 uint32_t mask);
int amdgpu_dpm_get_sclk_od(struct amdgpu_device *adev);
int amdgpu_dpm_set_sclk_od(struct amdgpu_device *adev, uint32_t value);
int amdgpu_dpm_get_mclk_od(struct amdgpu_device *adev);
int amdgpu_dpm_set_mclk_od(struct amdgpu_device *adev, uint32_t value);
int amdgpu_dpm_get_power_profile_mode(struct amdgpu_device *adev,
				      char *buf);
int amdgpu_dpm_set_power_profile_mode(struct amdgpu_device *adev,
				      long *input, uint32_t size);
int amdgpu_dpm_get_gpu_metrics(struct amdgpu_device *adev, void **table);

/**
 * @get_pm_metrics: Get one snapshot of power management metrics from PMFW. The
 * sample is copied to pm_metrics buffer. It's expected to be allocated by the
 * caller and size of the allocated buffer is passed. Max size expected for a
 * metrics sample is 4096 bytes.
 *
 * Return: Actual size of the metrics sample
 */
ssize_t amdgpu_dpm_get_pm_metrics(struct amdgpu_device *adev, void *pm_metrics,
				  size_t size);

int amdgpu_dpm_get_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t *fan_mode);
int amdgpu_dpm_set_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t speed);
int amdgpu_dpm_get_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t *speed);
int amdgpu_dpm_get_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t *speed);
int amdgpu_dpm_set_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t speed);
int amdgpu_dpm_set_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t mode);
int amdgpu_dpm_get_power_limit(struct amdgpu_device *adev,
			       uint32_t *limit,
			       enum pp_power_limit_level pp_limit_level,
			       enum pp_power_type power_type);
int amdgpu_dpm_set_power_limit(struct amdgpu_device *adev,
			       uint32_t limit);
int amdgpu_dpm_is_cclk_dpm_supported(struct amdgpu_device *adev);
int amdgpu_dpm_debugfs_print_current_performance_level(struct amdgpu_device *adev,
						       struct seq_file *m);
int amdgpu_dpm_get_smu_prv_buf_details(struct amdgpu_device *adev,
				       void **addr,
				       size_t *size);
int amdgpu_dpm_is_overdrive_supported(struct amdgpu_device *adev);
int amdgpu_dpm_set_pp_table(struct amdgpu_device *adev,
			    const char *buf,
			    size_t size);
int amdgpu_dpm_get_num_cpu_cores(struct amdgpu_device *adev);
void amdgpu_dpm_stb_debug_fs_init(struct amdgpu_device *adev);
int amdgpu_dpm_display_configuration_change(struct amdgpu_device *adev,
					    const struct amd_pp_display_configuration *input);
int amdgpu_dpm_get_clock_by_type(struct amdgpu_device *adev,
				 enum amd_pp_clock_type type,
				 struct amd_pp_clocks *clocks);
int amdgpu_dpm_get_display_mode_validation_clks(struct amdgpu_device *adev,
						struct amd_pp_simple_clock_info *clocks);
int amdgpu_dpm_get_clock_by_type_with_latency(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_latency *clocks);
int amdgpu_dpm_get_clock_by_type_with_voltage(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_voltage *clocks);
int amdgpu_dpm_set_watermarks_for_clocks_ranges(struct amdgpu_device *adev,
					       void *clock_ranges);
int amdgpu_dpm_display_clock_voltage_request(struct amdgpu_device *adev,
					     struct pp_display_clock_request *clock);
int amdgpu_dpm_get_current_clocks(struct amdgpu_device *adev,
				  struct amd_pp_clock_info *clocks);
void amdgpu_dpm_notify_smu_enable_pwe(struct amdgpu_device *adev);
int amdgpu_dpm_set_active_display_count(struct amdgpu_device *adev,
					uint32_t count);
int amdgpu_dpm_set_min_deep_sleep_dcefclk(struct amdgpu_device *adev,
					  uint32_t clock);
void amdgpu_dpm_set_hard_min_dcefclk_by_freq(struct amdgpu_device *adev,
					     uint32_t clock);
void amdgpu_dpm_set_hard_min_fclk_by_freq(struct amdgpu_device *adev,
					  uint32_t clock);
int amdgpu_dpm_display_disable_memory_clock_switch(struct amdgpu_device *adev,
						   bool disable_memory_clock_switch);
int amdgpu_dpm_get_max_sustainable_clocks_by_dc(struct amdgpu_device *adev,
						struct pp_smu_nv_clock_table *max_clocks);
enum pp_smu_status amdgpu_dpm_get_uclk_dpm_states(struct amdgpu_device *adev,
						  unsigned int *clock_values_in_khz,
						  unsigned int *num_states);
int amdgpu_dpm_get_dpm_clock_table(struct amdgpu_device *adev,
				   struct dpm_clocks *clock_table);
int amdgpu_dpm_set_pm_policy(struct amdgpu_device *adev, int policy_type,
			     int policy_level);
ssize_t amdgpu_dpm_get_pm_policy_info(struct amdgpu_device *adev,
				      enum pp_pm_policy p_type, char *buf);

#endif
