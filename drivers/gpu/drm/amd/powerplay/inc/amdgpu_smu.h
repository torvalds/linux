/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_SMU_H__
#define __AMDGPU_SMU_H__

#include "amdgpu.h"
#include "kgd_pp_interface.h"
#include "dm_pp_interface.h"
#include "dm_pp_smu.h"
#include "smu_types.h"

#define SMU_THERMAL_MINIMUM_ALERT_TEMP		0
#define SMU_THERMAL_MAXIMUM_ALERT_TEMP		255
#define SMU_TEMPERATURE_UNITS_PER_CENTIGRADES	1000

struct smu_hw_power_state {
	unsigned int magic;
};

struct smu_power_state;

enum smu_state_ui_label {
	SMU_STATE_UI_LABEL_NONE,
	SMU_STATE_UI_LABEL_BATTERY,
	SMU_STATE_UI_TABEL_MIDDLE_LOW,
	SMU_STATE_UI_LABEL_BALLANCED,
	SMU_STATE_UI_LABEL_MIDDLE_HIGHT,
	SMU_STATE_UI_LABEL_PERFORMANCE,
	SMU_STATE_UI_LABEL_BACO,
};

enum smu_state_classification_flag {
	SMU_STATE_CLASSIFICATION_FLAG_BOOT                     = 0x0001,
	SMU_STATE_CLASSIFICATION_FLAG_THERMAL                  = 0x0002,
	SMU_STATE_CLASSIFICATIN_FLAG_LIMITED_POWER_SOURCE      = 0x0004,
	SMU_STATE_CLASSIFICATION_FLAG_RESET                    = 0x0008,
	SMU_STATE_CLASSIFICATION_FLAG_FORCED                   = 0x0010,
	SMU_STATE_CLASSIFICATION_FLAG_USER_3D_PERFORMANCE      = 0x0020,
	SMU_STATE_CLASSIFICATION_FLAG_USER_2D_PERFORMANCE      = 0x0040,
	SMU_STATE_CLASSIFICATION_FLAG_3D_PERFORMANCE           = 0x0080,
	SMU_STATE_CLASSIFICATION_FLAG_AC_OVERDIRVER_TEMPLATE   = 0x0100,
	SMU_STATE_CLASSIFICATION_FLAG_UVD                      = 0x0200,
	SMU_STATE_CLASSIFICATION_FLAG_3D_PERFORMANCE_LOW       = 0x0400,
	SMU_STATE_CLASSIFICATION_FLAG_ACPI                     = 0x0800,
	SMU_STATE_CLASSIFICATION_FLAG_HD2                      = 0x1000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_HD                   = 0x2000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_SD                   = 0x4000,
	SMU_STATE_CLASSIFICATION_FLAG_USER_DC_PERFORMANCE      = 0x8000,
	SMU_STATE_CLASSIFICATION_FLAG_DC_OVERDIRVER_TEMPLATE   = 0x10000,
	SMU_STATE_CLASSIFICATION_FLAG_BACO                     = 0x20000,
	SMU_STATE_CLASSIFICATIN_FLAG_LIMITED_POWER_SOURCE2      = 0x40000,
	SMU_STATE_CLASSIFICATION_FLAG_ULV                      = 0x80000,
	SMU_STATE_CLASSIFICATION_FLAG_UVD_MVC                  = 0x100000,
};

struct smu_state_classification_block {
	enum smu_state_ui_label         ui_label;
	enum smu_state_classification_flag  flags;
	int                          bios_index;
	bool                      temporary_state;
	bool                      to_be_deleted;
};

struct smu_state_pcie_block {
	unsigned int lanes;
};

enum smu_refreshrate_source {
	SMU_REFRESHRATE_SOURCE_EDID,
	SMU_REFRESHRATE_SOURCE_EXPLICIT
};

struct smu_state_display_block {
	bool              disable_frame_modulation;
	bool              limit_refreshrate;
	enum smu_refreshrate_source refreshrate_source;
	int                  explicit_refreshrate;
	int                  edid_refreshrate_index;
	bool              enable_vari_bright;
};

struct smu_state_memroy_block {
	bool              dll_off;
	uint8_t                 m3arb;
	uint8_t                 unused[3];
};

struct smu_state_software_algorithm_block {
	bool disable_load_balancing;
	bool enable_sleep_for_timestamps;
};

struct smu_temperature_range {
	int min;
	int max;
	int edge_emergency_max;
	int hotspot_min;
	int hotspot_crit_max;
	int hotspot_emergency_max;
	int mem_min;
	int mem_crit_max;
	int mem_emergency_max;
};

struct smu_state_validation_block {
	bool single_display_only;
	bool disallow_on_dc;
	uint8_t supported_power_levels;
};

struct smu_uvd_clocks {
	uint32_t vclk;
	uint32_t dclk;
};

/**
* Structure to hold a SMU Power State.
*/
struct smu_power_state {
	uint32_t                                      id;
	struct list_head                              ordered_list;
	struct list_head                              all_states_list;

	struct smu_state_classification_block         classification;
	struct smu_state_validation_block             validation;
	struct smu_state_pcie_block                   pcie;
	struct smu_state_display_block                display;
	struct smu_state_memroy_block                 memory;
	struct smu_temperature_range                  temperatures;
	struct smu_state_software_algorithm_block     software;
	struct smu_uvd_clocks                         uvd_clocks;
	struct smu_hw_power_state                     hardware;
};

enum smu_power_src_type
{
	SMU_POWER_SOURCE_AC,
	SMU_POWER_SOURCE_DC,
	SMU_POWER_SOURCE_COUNT,
};

enum smu_memory_pool_size
{
    SMU_MEMORY_POOL_SIZE_ZERO   = 0,
    SMU_MEMORY_POOL_SIZE_256_MB = 0x10000000,
    SMU_MEMORY_POOL_SIZE_512_MB = 0x20000000,
    SMU_MEMORY_POOL_SIZE_1_GB   = 0x40000000,
    SMU_MEMORY_POOL_SIZE_2_GB   = 0x80000000,
};

#define SMU_TABLE_INIT(tables, table_id, s, a, d)	\
	do {						\
		tables[table_id].size = s;		\
		tables[table_id].align = a;		\
		tables[table_id].domain = d;		\
	} while (0)

struct smu_table {
	uint64_t size;
	uint32_t align;
	uint8_t domain;
	uint64_t mc_address;
	void *cpu_addr;
	struct amdgpu_bo *bo;
};

enum smu_perf_level_designation {
	PERF_LEVEL_ACTIVITY,
	PERF_LEVEL_POWER_CONTAINMENT,
};

struct smu_performance_level {
	uint32_t core_clock;
	uint32_t memory_clock;
	uint32_t vddc;
	uint32_t vddci;
	uint32_t non_local_mem_freq;
	uint32_t non_local_mem_width;
};

struct smu_clock_info {
	uint32_t min_mem_clk;
	uint32_t max_mem_clk;
	uint32_t min_eng_clk;
	uint32_t max_eng_clk;
	uint32_t min_bus_bandwidth;
	uint32_t max_bus_bandwidth;
};

struct smu_bios_boot_up_values
{
	uint32_t			revision;
	uint32_t			gfxclk;
	uint32_t			uclk;
	uint32_t			socclk;
	uint32_t			dcefclk;
	uint32_t			eclk;
	uint32_t			vclk;
	uint32_t			dclk;
	uint16_t			vddc;
	uint16_t			vddci;
	uint16_t			mvddc;
	uint16_t			vdd_gfx;
	uint8_t				cooling_id;
	uint32_t			pp_table_id;
	uint32_t			format_revision;
	uint32_t			content_revision;
	uint32_t			fclk;
};

enum smu_table_id
{
	SMU_TABLE_PPTABLE = 0,
	SMU_TABLE_WATERMARKS,
	SMU_TABLE_CUSTOM_DPM,
	SMU_TABLE_DPMCLOCKS,
	SMU_TABLE_AVFS,
	SMU_TABLE_AVFS_PSM_DEBUG,
	SMU_TABLE_AVFS_FUSE_OVERRIDE,
	SMU_TABLE_PMSTATUSLOG,
	SMU_TABLE_SMU_METRICS,
	SMU_TABLE_DRIVER_SMU_CONFIG,
	SMU_TABLE_ACTIVITY_MONITOR_COEFF,
	SMU_TABLE_OVERDRIVE,
	SMU_TABLE_I2C_COMMANDS,
	SMU_TABLE_PACE,
	SMU_TABLE_COUNT,
};

struct smu_table_context
{
	void				*power_play_table;
	uint32_t			power_play_table_size;
	void				*hardcode_pptable;
	unsigned long			metrics_time;
	void				*metrics_table;
	void				*clocks_table;

	void				*max_sustainable_clocks;
	struct smu_bios_boot_up_values	boot_values;
	void                            *driver_pptable;
	struct smu_table		*tables;
	struct smu_table		memory_pool;
	uint8_t                         thermal_controller_type;

	void				*overdrive_table;
};

struct smu_dpm_context {
	uint32_t dpm_context_size;
	void *dpm_context;
	void *golden_dpm_context;
	bool enable_umd_pstate;
	enum amd_dpm_forced_level dpm_level;
	enum amd_dpm_forced_level saved_dpm_level;
	enum amd_dpm_forced_level requested_dpm_level;
	struct smu_power_state *dpm_request_power_state;
	struct smu_power_state *dpm_current_power_state;
	struct mclock_latency_table *mclk_latency_table;
};

struct smu_power_gate {
	bool uvd_gated;
	bool vce_gated;
	bool vcn_gated;
};

struct smu_power_context {
	void *power_context;
	uint32_t power_context_size;
	struct smu_power_gate power_gate;
};


#define SMU_FEATURE_MAX	(64)
struct smu_feature
{
	uint32_t feature_num;
	DECLARE_BITMAP(supported, SMU_FEATURE_MAX);
	DECLARE_BITMAP(allowed, SMU_FEATURE_MAX);
	DECLARE_BITMAP(enabled, SMU_FEATURE_MAX);
	struct mutex mutex;
};

struct smu_clocks {
	uint32_t engine_clock;
	uint32_t memory_clock;
	uint32_t bus_bandwidth;
	uint32_t engine_clock_in_sr;
	uint32_t dcef_clock;
	uint32_t dcef_clock_in_sr;
};

#define MAX_REGULAR_DPM_NUM 16
struct mclk_latency_entries {
	uint32_t  frequency;
	uint32_t  latency;
};
struct mclock_latency_table {
	uint32_t  count;
	struct mclk_latency_entries  entries[MAX_REGULAR_DPM_NUM];
};

enum smu_reset_mode
{
    SMU_RESET_MODE_0,
    SMU_RESET_MODE_1,
    SMU_RESET_MODE_2,
};

enum smu_baco_state
{
	SMU_BACO_STATE_ENTER = 0,
	SMU_BACO_STATE_EXIT,
};

struct smu_baco_context
{
	struct mutex mutex;
	uint32_t state;
	bool platform_support;
};

#define WORKLOAD_POLICY_MAX 7
struct smu_context
{
	struct amdgpu_device            *adev;
	struct amdgpu_irq_src		*irq_source;

	const struct pptable_funcs	*ppt_funcs;
	struct mutex			mutex;
	struct mutex			sensor_lock;
	struct mutex			metrics_lock;
	uint64_t pool_size;

	struct smu_table_context	smu_table;
	struct smu_dpm_context		smu_dpm;
	struct smu_power_context	smu_power;
	struct smu_feature		smu_feature;
	struct amd_pp_display_configuration  *display_config;
	struct smu_baco_context		smu_baco;
	void *od_settings;

	uint32_t pstate_sclk;
	uint32_t pstate_mclk;

	bool od_enabled;
	uint32_t power_limit;
	uint32_t default_power_limit;

	/* soft pptable */
	uint32_t ppt_offset_bytes;
	uint32_t ppt_size_bytes;
	uint8_t  *ppt_start_addr;

	bool support_power_containment;
	bool disable_watermark;

#define WATERMARKS_EXIST	(1 << 0)
#define WATERMARKS_LOADED	(1 << 1)
	uint32_t watermarks_bitmap;
	uint32_t hard_min_uclk_req_from_dal;
	bool disable_uclk_switch;

	uint32_t workload_mask;
	uint32_t workload_prority[WORKLOAD_POLICY_MAX];
	uint32_t workload_setting[WORKLOAD_POLICY_MAX];
	uint32_t power_profile_mode;
	uint32_t default_power_profile_mode;
	bool pm_enabled;
	bool is_apu;

	uint32_t smc_if_version;

	bool uploading_custom_pp_table;
};

struct i2c_adapter;

struct pptable_funcs {
	int (*alloc_dpm_context)(struct smu_context *smu);
	int (*store_powerplay_table)(struct smu_context *smu);
	int (*check_powerplay_table)(struct smu_context *smu);
	int (*append_powerplay_table)(struct smu_context *smu);
	int (*get_smu_msg_index)(struct smu_context *smu, uint32_t index);
	int (*get_smu_clk_index)(struct smu_context *smu, uint32_t index);
	int (*get_smu_feature_index)(struct smu_context *smu, uint32_t index);
	int (*get_smu_table_index)(struct smu_context *smu, uint32_t index);
	int (*get_smu_power_index)(struct smu_context *smu, uint32_t index);
	int (*get_workload_type)(struct smu_context *smu, enum PP_SMC_POWER_PROFILE profile);
	int (*run_btc)(struct smu_context *smu);
	int (*get_allowed_feature_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);
	enum amd_pm_state_type (*get_current_power_state)(struct smu_context *smu);
	int (*set_default_dpm_table)(struct smu_context *smu);
	int (*set_power_state)(struct smu_context *smu);
	int (*populate_umd_state_clk)(struct smu_context *smu);
	int (*print_clk_levels)(struct smu_context *smu, enum smu_clk_type clk_type, char *buf);
	int (*force_clk_levels)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t mask);
	int (*set_default_od8_settings)(struct smu_context *smu);
	int (*get_od_percentage)(struct smu_context *smu, enum smu_clk_type clk_type);
	int (*set_od_percentage)(struct smu_context *smu,
				 enum smu_clk_type clk_type,
				 uint32_t value);
	int (*od_edit_dpm_table)(struct smu_context *smu,
				 enum PP_OD_DPM_TABLE_COMMAND type,
				 long *input, uint32_t size);
	int (*get_clock_by_type_with_latency)(struct smu_context *smu,
					      enum smu_clk_type clk_type,
					      struct
					      pp_clock_levels_with_latency
					      *clocks);
	int (*get_clock_by_type_with_voltage)(struct smu_context *smu,
					      enum amd_pp_clock_type type,
					      struct
					      pp_clock_levels_with_voltage
					      *clocks);
	int (*get_power_profile_mode)(struct smu_context *smu, char *buf);
	int (*set_power_profile_mode)(struct smu_context *smu, long *input, uint32_t size);
	int (*dpm_set_uvd_enable)(struct smu_context *smu, bool enable);
	int (*dpm_set_vce_enable)(struct smu_context *smu, bool enable);
	int (*read_sensor)(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);
	int (*pre_display_config_changed)(struct smu_context *smu);
	int (*display_config_changed)(struct smu_context *smu);
	int (*apply_clocks_adjust_rules)(struct smu_context *smu);
	int (*notify_smc_dispaly_config)(struct smu_context *smu);
	int (*force_dpm_limit_value)(struct smu_context *smu, bool highest);
	int (*unforce_dpm_levels)(struct smu_context *smu);
	int (*get_profiling_clk_mask)(struct smu_context *smu,
				      enum amd_dpm_forced_level level,
				      uint32_t *sclk_mask,
				      uint32_t *mclk_mask,
				      uint32_t *soc_mask);
	int (*set_cpu_power_state)(struct smu_context *smu);
	bool (*is_dpm_running)(struct smu_context *smu);
	int (*tables_init)(struct smu_context *smu, struct smu_table *tables);
	int (*set_thermal_fan_table)(struct smu_context *smu);
	int (*get_fan_speed_percent)(struct smu_context *smu, uint32_t *speed);
	int (*get_fan_speed_rpm)(struct smu_context *smu, uint32_t *speed);
	int (*set_watermarks_table)(struct smu_context *smu, void *watermarks,
				    struct dm_pp_wm_sets_with_clock_ranges_soc15 *clock_ranges);
	int (*get_current_clk_freq_by_table)(struct smu_context *smu,
					     enum smu_clk_type clk_type,
					     uint32_t *value);
	int (*get_thermal_temperature_range)(struct smu_context *smu, struct smu_temperature_range *range);
	int (*get_uclk_dpm_states)(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states);
	int (*set_default_od_settings)(struct smu_context *smu, bool initialize);
	int (*set_performance_level)(struct smu_context *smu, enum amd_dpm_forced_level level);
	int (*display_disable_memory_clock_switch)(struct smu_context *smu, bool disable_memory_clock_switch);
	void (*dump_pptable)(struct smu_context *smu);
	int (*get_power_limit)(struct smu_context *smu, uint32_t *limit, bool asic_default);
	int (*get_dpm_clk_limited)(struct smu_context *smu, enum smu_clk_type clk_type,
				   uint32_t dpm_level, uint32_t *freq);
	int (*set_df_cstate)(struct smu_context *smu, enum pp_df_cstate state);
	int (*update_pcie_parameters)(struct smu_context *smu, uint32_t pcie_gen_cap, uint32_t pcie_width_cap);
	int (*i2c_eeprom_init)(struct i2c_adapter *control);
	void (*i2c_eeprom_fini)(struct i2c_adapter *control);
	int (*get_dpm_clock_table)(struct smu_context *smu, struct dpm_clocks *clock_table);
	int (*init_microcode)(struct smu_context *smu);
	int (*load_microcode)(struct smu_context *smu);
	int (*init_smc_tables)(struct smu_context *smu);
	int (*fini_smc_tables)(struct smu_context *smu);
	int (*init_power)(struct smu_context *smu);
	int (*fini_power)(struct smu_context *smu);
	int (*check_fw_status)(struct smu_context *smu);
	int (*setup_pptable)(struct smu_context *smu);
	int (*get_vbios_bootup_values)(struct smu_context *smu);
	int (*get_clk_info_from_vbios)(struct smu_context *smu);
	int (*check_pptable)(struct smu_context *smu);
	int (*parse_pptable)(struct smu_context *smu);
	int (*populate_smc_tables)(struct smu_context *smu);
	int (*check_fw_version)(struct smu_context *smu);
	int (*powergate_sdma)(struct smu_context *smu, bool gate);
	int (*powergate_vcn)(struct smu_context *smu, bool gate);
	int (*set_gfx_cgpg)(struct smu_context *smu, bool enable);
	int (*write_pptable)(struct smu_context *smu);
	int (*set_min_dcef_deep_sleep)(struct smu_context *smu);
	int (*set_tool_table_location)(struct smu_context *smu);
	int (*notify_memory_pool_location)(struct smu_context *smu);
	int (*set_last_dcef_min_deep_sleep_clk)(struct smu_context *smu);
	int (*system_features_control)(struct smu_context *smu, bool en);
	int (*send_smc_msg_with_param)(struct smu_context *smu,
				       enum smu_message_type msg, uint32_t param);
	int (*read_smc_arg)(struct smu_context *smu, uint32_t *arg);
	int (*init_display_count)(struct smu_context *smu, uint32_t count);
	int (*set_allowed_mask)(struct smu_context *smu);
	int (*get_enabled_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);
	int (*notify_display_change)(struct smu_context *smu);
	int (*set_power_limit)(struct smu_context *smu, uint32_t n);
	int (*get_current_clk_freq)(struct smu_context *smu, enum smu_clk_type clk_id, uint32_t *value);
	int (*init_max_sustainable_clocks)(struct smu_context *smu);
	int (*start_thermal_control)(struct smu_context *smu);
	int (*stop_thermal_control)(struct smu_context *smu);
	int (*set_deep_sleep_dcefclk)(struct smu_context *smu, uint32_t clk);
	int (*set_active_display_count)(struct smu_context *smu, uint32_t count);
	int (*store_cc6_data)(struct smu_context *smu, uint32_t separation_time,
			      bool cc6_disable, bool pstate_disable,
			      bool pstate_switch_disable);
	int (*get_clock_by_type)(struct smu_context *smu,
				 enum amd_pp_clock_type type,
				 struct amd_pp_clocks *clocks);
	int (*get_max_high_clocks)(struct smu_context *smu,
				   struct amd_pp_simple_clock_info *clocks);
	int (*display_clock_voltage_request)(struct smu_context *smu, struct
					     pp_display_clock_request
					     *clock_req);
	int (*get_dal_power_level)(struct smu_context *smu,
				   struct amd_pp_simple_clock_info *clocks);
	int (*get_perf_level)(struct smu_context *smu,
			      enum smu_perf_level_designation designation,
			      struct smu_performance_level *level);
	int (*get_current_shallow_sleep_clocks)(struct smu_context *smu,
						struct smu_clock_info *clocks);
	int (*notify_smu_enable_pwe)(struct smu_context *smu);
	int (*conv_power_profile_to_pplib_workload)(int power_profile);
	uint32_t (*get_fan_control_mode)(struct smu_context *smu);
	int (*set_fan_control_mode)(struct smu_context *smu, uint32_t mode);
	int (*set_fan_speed_percent)(struct smu_context *smu, uint32_t speed);
	int (*set_fan_speed_rpm)(struct smu_context *smu, uint32_t speed);
	int (*set_xgmi_pstate)(struct smu_context *smu, uint32_t pstate);
	int (*gfx_off_control)(struct smu_context *smu, bool enable);
	int (*register_irq_handler)(struct smu_context *smu);
	int (*set_azalia_d3_pme)(struct smu_context *smu);
	int (*get_max_sustainable_clocks_by_dc)(struct smu_context *smu, struct pp_smu_nv_clock_table *max_clocks);
	bool (*baco_is_support)(struct smu_context *smu);
	enum smu_baco_state (*baco_get_state)(struct smu_context *smu);
	int (*baco_set_state)(struct smu_context *smu, enum smu_baco_state state);
	int (*baco_reset)(struct smu_context *smu);
	int (*mode2_reset)(struct smu_context *smu);
	int (*get_dpm_ultimate_freq)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t *min, uint32_t *max);
	int (*set_soft_freq_limited_range)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t min, uint32_t max);
	int (*override_pcie_parameters)(struct smu_context *smu);
	uint32_t (*get_pptable_power_limit)(struct smu_context *smu);
};

int smu_load_microcode(struct smu_context *smu);

int smu_check_fw_status(struct smu_context *smu);

int smu_set_gfx_cgpg(struct smu_context *smu, bool enabled);

#define smu_i2c_eeprom_init(smu, control) \
		((smu)->ppt_funcs->i2c_eeprom_init ? (smu)->ppt_funcs->i2c_eeprom_init((control)) : -EINVAL)
#define smu_i2c_eeprom_fini(smu, control) \
		((smu)->ppt_funcs->i2c_eeprom_fini ? (smu)->ppt_funcs->i2c_eeprom_fini((control)) : -EINVAL)

int smu_set_fan_speed_rpm(struct smu_context *smu, uint32_t speed);

int smu_get_power_limit(struct smu_context *smu,
			uint32_t *limit,
			bool def,
			bool lock_needed);

int smu_set_power_limit(struct smu_context *smu, uint32_t limit);
int smu_print_clk_levels(struct smu_context *smu, enum smu_clk_type clk_type, char *buf);
int smu_get_od_percentage(struct smu_context *smu, enum smu_clk_type type);
int smu_set_od_percentage(struct smu_context *smu, enum smu_clk_type type, uint32_t value);

int smu_od_edit_dpm_table(struct smu_context *smu,
			  enum PP_OD_DPM_TABLE_COMMAND type,
			  long *input, uint32_t size);

int smu_read_sensor(struct smu_context *smu,
		    enum amd_pp_sensors sensor,
		    void *data, uint32_t *size);
int smu_get_power_profile_mode(struct smu_context *smu, char *buf);

int smu_set_power_profile_mode(struct smu_context *smu,
			       long *param,
			       uint32_t param_size,
			       bool lock_needed);
int smu_get_fan_control_mode(struct smu_context *smu);
int smu_set_fan_control_mode(struct smu_context *smu, int value);
int smu_get_fan_speed_percent(struct smu_context *smu, uint32_t *speed);
int smu_set_fan_speed_percent(struct smu_context *smu, uint32_t speed);
int smu_get_fan_speed_rpm(struct smu_context *smu, uint32_t *speed);

int smu_set_deep_sleep_dcefclk(struct smu_context *smu, int clk);
int smu_set_active_display_count(struct smu_context *smu, uint32_t count);

int smu_get_clock_by_type(struct smu_context *smu,
			  enum amd_pp_clock_type type,
			  struct amd_pp_clocks *clocks);

int smu_get_max_high_clocks(struct smu_context *smu,
			    struct amd_pp_simple_clock_info *clocks);

int smu_get_clock_by_type_with_latency(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       struct pp_clock_levels_with_latency *clocks);

int smu_get_clock_by_type_with_voltage(struct smu_context *smu,
				       enum amd_pp_clock_type type,
				       struct pp_clock_levels_with_voltage *clocks);

int smu_display_clock_voltage_request(struct smu_context *smu,
				      struct pp_display_clock_request *clock_req);
int smu_display_disable_memory_clock_switch(struct smu_context *smu, bool disable_memory_clock_switch);
int smu_notify_smu_enable_pwe(struct smu_context *smu);

int smu_set_xgmi_pstate(struct smu_context *smu,
			uint32_t pstate);

int smu_set_azalia_d3_pme(struct smu_context *smu);

bool smu_baco_is_support(struct smu_context *smu);

int smu_baco_get_state(struct smu_context *smu, enum smu_baco_state *state);

int smu_baco_reset(struct smu_context *smu);

int smu_mode2_reset(struct smu_context *smu);

extern int smu_get_atom_data_table(struct smu_context *smu, uint32_t table,
				   uint16_t *size, uint8_t *frev, uint8_t *crev,
				   uint8_t **addr);

extern const struct amd_ip_funcs smu_ip_funcs;

extern const struct amdgpu_ip_block_version smu_v11_0_ip_block;
extern const struct amdgpu_ip_block_version smu_v12_0_ip_block;

extern int smu_feature_init_dpm(struct smu_context *smu);

extern int smu_feature_is_enabled(struct smu_context *smu,
				  enum smu_feature_mask mask);
extern int smu_feature_set_enabled(struct smu_context *smu,
				   enum smu_feature_mask mask, bool enable);
extern int smu_feature_is_supported(struct smu_context *smu,
				    enum smu_feature_mask mask);
extern int smu_feature_set_supported(struct smu_context *smu,
				     enum smu_feature_mask mask, bool enable);

int smu_update_table(struct smu_context *smu, enum smu_table_id table_index, int argument,
		     void *table_data, bool drv2smu);

bool is_support_sw_smu(struct amdgpu_device *adev);
bool is_support_sw_smu_xgmi(struct amdgpu_device *adev);
int smu_reset(struct smu_context *smu);
int smu_common_read_sensor(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);
int smu_sys_get_pp_table(struct smu_context *smu, void **table);
int smu_sys_set_pp_table(struct smu_context *smu,  void *buf, size_t size);
int smu_get_power_num_states(struct smu_context *smu, struct pp_states_info *state_info);
enum amd_pm_state_type smu_get_current_power_state(struct smu_context *smu);
int smu_write_watermarks_table(struct smu_context *smu);
int smu_set_watermarks_for_clock_ranges(
		struct smu_context *smu,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *clock_ranges);

/* smu to display interface */
extern int smu_display_configuration_change(struct smu_context *smu, const
					    struct amd_pp_display_configuration
					    *display_config);
extern int smu_get_current_clocks(struct smu_context *smu,
				  struct amd_pp_clock_info *clocks);
extern int smu_dpm_set_power_gate(struct smu_context *smu,uint32_t block_type, bool gate);
extern int smu_handle_task(struct smu_context *smu,
			   enum amd_dpm_forced_level level,
			   enum amd_pp_task task_id,
			   bool lock_needed);
int smu_switch_power_profile(struct smu_context *smu,
			     enum PP_SMC_POWER_PROFILE type,
			     bool en);
int smu_get_smc_version(struct smu_context *smu, uint32_t *if_version, uint32_t *smu_version);
int smu_get_dpm_freq_by_index(struct smu_context *smu, enum smu_clk_type clk_type,
			      uint16_t level, uint32_t *value);
int smu_get_dpm_level_count(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t *value);
int smu_get_dpm_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			   uint32_t *min, uint32_t *max, bool lock_needed);
int smu_set_soft_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max);
int smu_set_hard_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max);
enum amd_dpm_forced_level smu_get_performance_level(struct smu_context *smu);
int smu_force_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level);
int smu_set_display_count(struct smu_context *smu, uint32_t count);
bool smu_clk_dpm_is_enabled(struct smu_context *smu, enum smu_clk_type clk_type);
const char *smu_get_message_name(struct smu_context *smu, enum smu_message_type type);
const char *smu_get_feature_name(struct smu_context *smu, enum smu_feature_mask feature);
size_t smu_sys_get_pp_feature_mask(struct smu_context *smu, char *buf);
int smu_sys_set_pp_feature_mask(struct smu_context *smu, uint64_t new_mask);
int smu_force_clk_levels(struct smu_context *smu,
			 enum smu_clk_type clk_type,
			 uint32_t mask,
			 bool lock_needed);
int smu_set_mp1_state(struct smu_context *smu,
		      enum pp_mp1_state mp1_state);
int smu_set_df_cstate(struct smu_context *smu,
		      enum pp_df_cstate state);

int smu_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					 struct pp_smu_nv_clock_table *max_clocks);

int smu_get_uclk_dpm_states(struct smu_context *smu,
			    unsigned int *clock_values_in_khz,
			    unsigned int *num_states);

int smu_get_dpm_clock_table(struct smu_context *smu,
			    struct dpm_clocks *clock_table);

uint32_t smu_get_pptable_power_limit(struct smu_context *smu);

#endif
