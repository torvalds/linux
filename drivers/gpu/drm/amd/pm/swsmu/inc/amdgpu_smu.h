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
#include "linux/firmware.h"

#define SMU_THERMAL_MINIMUM_ALERT_TEMP		0
#define SMU_THERMAL_MAXIMUM_ALERT_TEMP		255
#define SMU_TEMPERATURE_UNITS_PER_CENTIGRADES	1000
#define SMU_FW_NAME_LEN			0x24

#define SMU_DPM_USER_PROFILE_RESTORE (1 << 0)
#define SMU_CUSTOM_FAN_SPEED_RPM     (1 << 1)
#define SMU_CUSTOM_FAN_SPEED_PWM     (1 << 2)

// Power Throttlers
#define SMU_THROTTLER_PPT0_BIT			0
#define SMU_THROTTLER_PPT1_BIT			1
#define SMU_THROTTLER_PPT2_BIT			2
#define SMU_THROTTLER_PPT3_BIT			3
#define SMU_THROTTLER_SPL_BIT			4
#define SMU_THROTTLER_FPPT_BIT			5
#define SMU_THROTTLER_SPPT_BIT			6
#define SMU_THROTTLER_SPPT_APU_BIT		7

// Current Throttlers
#define SMU_THROTTLER_TDC_GFX_BIT		16
#define SMU_THROTTLER_TDC_SOC_BIT		17
#define SMU_THROTTLER_TDC_MEM_BIT		18
#define SMU_THROTTLER_TDC_VDD_BIT		19
#define SMU_THROTTLER_TDC_CVIP_BIT		20
#define SMU_THROTTLER_EDC_CPU_BIT		21
#define SMU_THROTTLER_EDC_GFX_BIT		22
#define SMU_THROTTLER_APCC_BIT			23

// Temperature
#define SMU_THROTTLER_TEMP_GPU_BIT		32
#define SMU_THROTTLER_TEMP_CORE_BIT		33
#define SMU_THROTTLER_TEMP_MEM_BIT		34
#define SMU_THROTTLER_TEMP_EDGE_BIT		35
#define SMU_THROTTLER_TEMP_HOTSPOT_BIT		36
#define SMU_THROTTLER_TEMP_SOC_BIT		37
#define SMU_THROTTLER_TEMP_VR_GFX_BIT		38
#define SMU_THROTTLER_TEMP_VR_SOC_BIT		39
#define SMU_THROTTLER_TEMP_VR_MEM0_BIT		40
#define SMU_THROTTLER_TEMP_VR_MEM1_BIT		41
#define SMU_THROTTLER_TEMP_LIQUID0_BIT		42
#define SMU_THROTTLER_TEMP_LIQUID1_BIT		43
#define SMU_THROTTLER_VRHOT0_BIT		44
#define SMU_THROTTLER_VRHOT1_BIT		45
#define SMU_THROTTLER_PROCHOT_CPU_BIT		46
#define SMU_THROTTLER_PROCHOT_GFX_BIT		47

// Other
#define SMU_THROTTLER_PPM_BIT			56
#define SMU_THROTTLER_FIT_BIT			57

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

struct smu_state_memory_block {
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
	int software_shutdown_temp;
	int software_shutdown_temp_offset;
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
	struct smu_state_memory_block                 memory;
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

enum smu_ppt_limit_type
{
	SMU_DEFAULT_PPT_LIMIT = 0,
	SMU_FAST_PPT_LIMIT,
};

enum smu_ppt_limit_level
{
	SMU_PPT_LIMIT_MIN = -1,
	SMU_PPT_LIMIT_CURRENT,
	SMU_PPT_LIMIT_DEFAULT,
	SMU_PPT_LIMIT_MAX,
};

enum smu_memory_pool_size
{
    SMU_MEMORY_POOL_SIZE_ZERO   = 0,
    SMU_MEMORY_POOL_SIZE_256_MB = 0x10000000,
    SMU_MEMORY_POOL_SIZE_512_MB = 0x20000000,
    SMU_MEMORY_POOL_SIZE_1_GB   = 0x40000000,
    SMU_MEMORY_POOL_SIZE_2_GB   = 0x80000000,
};

struct smu_user_dpm_profile {
	uint32_t fan_mode;
	uint32_t power_limit;
	uint32_t fan_speed_pwm;
	uint32_t fan_speed_rpm;
	uint32_t flags;
	uint32_t user_od;

	/* user clock state information */
	uint32_t clk_mask[SMU_CLK_COUNT];
	uint32_t clk_dependency;
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
	uint32_t			lclk;
	uint32_t			firmware_caps;
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
	SMU_TABLE_ECCINFO,
	SMU_TABLE_COMBO_PPTABLE,
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
	void				*watermarks_table;

	void				*max_sustainable_clocks;
	struct smu_bios_boot_up_values	boot_values;
	void				*driver_pptable;
	void				*combo_pptable;
	void                            *ecc_table;
	void				*driver_smu_config_table;
	struct smu_table		tables[SMU_TABLE_COUNT];
	/*
	 * The driver table is just a staging buffer for
	 * uploading/downloading content from the SMU.
	 *
	 * And the table_id for SMU_MSG_TransferTableSmu2Dram/
	 * SMU_MSG_TransferTableDram2Smu instructs SMU
	 * which content driver is interested.
	 */
	struct smu_table		driver_table;
	struct smu_table		memory_pool;
	struct smu_table		dummy_read_1_table;
	uint8_t                         thermal_controller_type;

	void				*overdrive_table;
	void                            *boot_overdrive_table;
	void				*user_overdrive_table;

	uint32_t			gpu_metrics_table_size;
	void				*gpu_metrics_table;
};

struct smu_dpm_context {
	uint32_t dpm_context_size;
	void *dpm_context;
	void *golden_dpm_context;
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
	atomic_t vcn_gated;
	atomic_t jpeg_gated;
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
	uint32_t state;
	bool platform_support;
	bool maco_support;
};

struct smu_freq_info {
	uint32_t min;
	uint32_t max;
	uint32_t freq_level;
};

struct pstates_clk_freq {
	uint32_t			min;
	uint32_t			standard;
	uint32_t			peak;
	struct smu_freq_info		custom;
	struct smu_freq_info		curr;
};

struct smu_umd_pstate_table {
	struct pstates_clk_freq		gfxclk_pstate;
	struct pstates_clk_freq		socclk_pstate;
	struct pstates_clk_freq		uclk_pstate;
	struct pstates_clk_freq		vclk_pstate;
	struct pstates_clk_freq		dclk_pstate;
	struct pstates_clk_freq		fclk_pstate;
};

struct cmn2asic_msg_mapping {
	int	valid_mapping;
	int	map_to;
	int	valid_in_vf;
};

struct cmn2asic_mapping {
	int	valid_mapping;
	int	map_to;
};

struct stb_context {
	uint32_t stb_buf_size;
	bool enabled;
	spinlock_t lock;
};

#define WORKLOAD_POLICY_MAX 7

struct smu_context
{
	struct amdgpu_device            *adev;
	struct amdgpu_irq_src		irq_source;

	const struct pptable_funcs	*ppt_funcs;
	const struct cmn2asic_msg_mapping	*message_map;
	const struct cmn2asic_mapping	*clock_map;
	const struct cmn2asic_mapping	*feature_map;
	const struct cmn2asic_mapping	*table_map;
	const struct cmn2asic_mapping	*pwr_src_map;
	const struct cmn2asic_mapping	*workload_map;
	struct mutex			message_lock;
	uint64_t pool_size;

	struct smu_table_context	smu_table;
	struct smu_dpm_context		smu_dpm;
	struct smu_power_context	smu_power;
	struct smu_feature		smu_feature;
	struct amd_pp_display_configuration  *display_config;
	struct smu_baco_context		smu_baco;
	struct smu_temperature_range	thermal_range;
	void *od_settings;

	struct smu_umd_pstate_table	pstate_table;
	uint32_t pstate_sclk;
	uint32_t pstate_mclk;

	bool od_enabled;
	uint32_t current_power_limit;
	uint32_t default_power_limit;
	uint32_t max_power_limit;

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

	uint32_t smc_driver_if_version;
	uint32_t smc_fw_if_version;
	uint32_t smc_fw_version;

	bool uploading_custom_pp_table;
	bool dc_controlled_by_gpio;

	struct work_struct throttling_logging_work;
	atomic64_t throttle_int_counter;
	struct work_struct interrupt_work;

	unsigned fan_max_rpm;
	unsigned manual_fan_speed_pwm;

	uint32_t gfx_default_hard_min_freq;
	uint32_t gfx_default_soft_max_freq;
	uint32_t gfx_actual_hard_min_freq;
	uint32_t gfx_actual_soft_max_freq;

	/* APU only */
	uint32_t cpu_default_soft_min_freq;
	uint32_t cpu_default_soft_max_freq;
	uint32_t cpu_actual_soft_min_freq;
	uint32_t cpu_actual_soft_max_freq;
	uint32_t cpu_core_id_select;
	uint16_t cpu_core_num;

	struct smu_user_dpm_profile user_dpm_profile;

	struct stb_context stb_context;

	struct firmware pptable_firmware;

	u32 param_reg;
	u32 msg_reg;
	u32 resp_reg;

	u32 debug_param_reg;
	u32 debug_msg_reg;
	u32 debug_resp_reg;

	struct delayed_work		swctf_delayed_work;
};

struct i2c_adapter;

/**
 * struct pptable_funcs - Callbacks used to interact with the SMU.
 */
struct pptable_funcs {
	/**
	 * @run_btc: Calibrate voltage/frequency curve to fit the system's
	 *           power delivery and voltage margins. Required for adaptive
	 *           voltage frequency scaling (AVFS).
	 */
	int (*run_btc)(struct smu_context *smu);

	/**
	 * @get_allowed_feature_mask: Get allowed feature mask.
	 * &feature_mask: Array to store feature mask.
	 * &num: Elements in &feature_mask.
	 */
	int (*get_allowed_feature_mask)(struct smu_context *smu, uint32_t *feature_mask, uint32_t num);

	/**
	 * @get_current_power_state: Get the current power state.
	 *
	 * Return: Current power state on success, negative errno on failure.
	 */
	enum amd_pm_state_type (*get_current_power_state)(struct smu_context *smu);

	/**
	 * @set_default_dpm_table: Retrieve the default overdrive settings from
	 *                         the SMU.
	 */
	int (*set_default_dpm_table)(struct smu_context *smu);

	int (*set_power_state)(struct smu_context *smu);

	/**
	 * @populate_umd_state_clk: Populate the UMD power state table with
	 *                          defaults.
	 */
	int (*populate_umd_state_clk)(struct smu_context *smu);

	/**
	 * @print_clk_levels: Print DPM clock levels for a clock domain
	 *                    to buffer. Star current level.
	 *
	 * Used for sysfs interfaces.
	 * Return: Number of characters written to the buffer
	 */
	int (*print_clk_levels)(struct smu_context *smu, enum smu_clk_type clk_type, char *buf);

	/**
	 * @emit_clk_levels: Print DPM clock levels for a clock domain
	 *                    to buffer using sysfs_emit_at. Star current level.
	 *
	 * Used for sysfs interfaces.
	 * &buf: sysfs buffer
	 * &offset: offset within buffer to start printing, which is updated by the
	 * function.
	 *
	 * Return: 0 on Success or Negative to indicate an error occurred.
	 */
	int (*emit_clk_levels)(struct smu_context *smu, enum smu_clk_type clk_type, char *buf, int *offset);

	/**
	 * @force_clk_levels: Set a range of allowed DPM levels for a clock
	 *                    domain.
	 * &clk_type: Clock domain.
	 * &mask: Range of allowed DPM levels.
	 */
	int (*force_clk_levels)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t mask);

	/**
	 * @od_edit_dpm_table: Edit the custom overdrive DPM table.
	 * &type: Type of edit.
	 * &input: Edit parameters.
	 * &size: Size of &input.
	 */
	int (*od_edit_dpm_table)(struct smu_context *smu,
				 enum PP_OD_DPM_TABLE_COMMAND type,
				 long *input, uint32_t size);

	/**
	 * @restore_user_od_settings: Restore the user customized
	 *                            OD settings on S3/S4/Runpm resume.
	 */
	int (*restore_user_od_settings)(struct smu_context *smu);

	/**
	 * @get_clock_by_type_with_latency: Get the speed and latency of a clock
	 *                                  domain.
	 */
	int (*get_clock_by_type_with_latency)(struct smu_context *smu,
					      enum smu_clk_type clk_type,
					      struct
					      pp_clock_levels_with_latency
					      *clocks);
	/**
	 * @get_clock_by_type_with_voltage: Get the speed and voltage of a clock
	 *                                  domain.
	 */
	int (*get_clock_by_type_with_voltage)(struct smu_context *smu,
					      enum amd_pp_clock_type type,
					      struct
					      pp_clock_levels_with_voltage
					      *clocks);

	/**
	 * @get_power_profile_mode: Print all power profile modes to
	 *                          buffer. Star current mode.
	 */
	int (*get_power_profile_mode)(struct smu_context *smu, char *buf);

	/**
	 * @set_power_profile_mode: Set a power profile mode. Also used to
	 *                          create/set custom power profile modes.
	 * &input: Power profile mode parameters.
	 * &size: Size of &input.
	 */
	int (*set_power_profile_mode)(struct smu_context *smu, long *input, uint32_t size);

	/**
	 * @dpm_set_vcn_enable: Enable/disable VCN engine dynamic power
	 *                      management.
	 */
	int (*dpm_set_vcn_enable)(struct smu_context *smu, bool enable);

	/**
	 * @dpm_set_jpeg_enable: Enable/disable JPEG engine dynamic power
	 *                       management.
	 */
	int (*dpm_set_jpeg_enable)(struct smu_context *smu, bool enable);

	/**
	 * @set_gfx_power_up_by_imu: Enable GFX engine with IMU
	 */
	int (*set_gfx_power_up_by_imu)(struct smu_context *smu);

	/**
	 * @read_sensor: Read data from a sensor.
	 * &sensor: Sensor to read data from.
	 * &data: Sensor reading.
	 * &size: Size of &data.
	 */
	int (*read_sensor)(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size);

	/**
	 * @get_apu_thermal_limit: get apu core limit from smu
	 * &limit: current limit temperature in millidegrees Celsius
	 */
	int (*get_apu_thermal_limit)(struct smu_context *smu, uint32_t *limit);

	/**
	 * @set_apu_thermal_limit: update all controllers with new limit
	 * &limit: limit temperature to be setted, in millidegrees Celsius
	 */
	int (*set_apu_thermal_limit)(struct smu_context *smu, uint32_t limit);

	/**
	 * @pre_display_config_changed: Prepare GPU for a display configuration
	 *                              change.
	 *
	 * Disable display tracking and pin memory clock speed to maximum. Used
	 * in display component synchronization.
	 */
	int (*pre_display_config_changed)(struct smu_context *smu);

	/**
	 * @display_config_changed: Notify the SMU of the current display
	 *                          configuration.
	 *
	 * Allows SMU to properly track blanking periods for memory clock
	 * adjustment. Used in display component synchronization.
	 */
	int (*display_config_changed)(struct smu_context *smu);

	int (*apply_clocks_adjust_rules)(struct smu_context *smu);

	/**
	 * @notify_smc_display_config: Applies display requirements to the
	 *                             current power state.
	 *
	 * Optimize deep sleep DCEFclk and mclk for the current display
	 * configuration. Used in display component synchronization.
	 */
	int (*notify_smc_display_config)(struct smu_context *smu);

	/**
	 * @is_dpm_running: Check if DPM is running.
	 *
	 * Return: True if DPM is running, false otherwise.
	 */
	bool (*is_dpm_running)(struct smu_context *smu);

	/**
	 * @get_fan_speed_pwm: Get the current fan speed in PWM.
	 */
	int (*get_fan_speed_pwm)(struct smu_context *smu, uint32_t *speed);

	/**
	 * @get_fan_speed_rpm: Get the current fan speed in rpm.
	 */
	int (*get_fan_speed_rpm)(struct smu_context *smu, uint32_t *speed);

	/**
	 * @set_watermarks_table: Configure and upload the watermarks tables to
	 *                        the SMU.
	 */
	int (*set_watermarks_table)(struct smu_context *smu,
				    struct pp_smu_wm_range_sets *clock_ranges);

	/**
	 * @get_thermal_temperature_range: Get safe thermal limits in Celcius.
	 */
	int (*get_thermal_temperature_range)(struct smu_context *smu, struct smu_temperature_range *range);

	/**
	 * @get_uclk_dpm_states: Get memory clock DPM levels in kHz.
	 * &clocks_in_khz: Array of DPM levels.
	 * &num_states: Elements in &clocks_in_khz.
	 */
	int (*get_uclk_dpm_states)(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states);

	/**
	 * @set_default_od_settings: Set the overdrive tables to defaults.
	 */
	int (*set_default_od_settings)(struct smu_context *smu);

	/**
	 * @set_performance_level: Set a performance level.
	 */
	int (*set_performance_level)(struct smu_context *smu, enum amd_dpm_forced_level level);

	/**
	 * @display_disable_memory_clock_switch: Enable/disable dynamic memory
	 *                                       clock switching.
	 *
	 * Disabling this feature forces memory clock speed to maximum.
	 * Enabling sets the minimum memory clock capable of driving the
	 * current display configuration.
	 */
	int (*display_disable_memory_clock_switch)(struct smu_context *smu, bool disable_memory_clock_switch);

	/**
	 * @dump_pptable: Print the power play table to the system log.
	 */
	void (*dump_pptable)(struct smu_context *smu);

	/**
	 * @get_power_limit: Get the device's power limits.
	 */
	int (*get_power_limit)(struct smu_context *smu,
			       uint32_t *current_power_limit,
			       uint32_t *default_power_limit,
			       uint32_t *max_power_limit);

	/**
	 * @get_ppt_limit: Get the device's ppt limits.
	 */
	int (*get_ppt_limit)(struct smu_context *smu, uint32_t *ppt_limit,
			enum smu_ppt_limit_type limit_type, enum smu_ppt_limit_level limit_level);

	/**
	 * @set_df_cstate: Set data fabric cstate.
	 */
	int (*set_df_cstate)(struct smu_context *smu, enum pp_df_cstate state);

	/**
	 * @allow_xgmi_power_down: Enable/disable external global memory
	 *                         interconnect power down.
	 */
	int (*allow_xgmi_power_down)(struct smu_context *smu, bool en);

	/**
	 * @update_pcie_parameters: Update and upload the system's PCIe
	 *                          capabilites to the SMU.
	 * &pcie_gen_cap: Maximum allowed PCIe generation.
	 * &pcie_width_cap: Maximum allowed PCIe width.
	 */
	int (*update_pcie_parameters)(struct smu_context *smu, uint32_t pcie_gen_cap, uint32_t pcie_width_cap);

	/**
	 * @i2c_init: Initialize i2c.
	 *
	 * The i2c bus is used internally by the SMU voltage regulators and
	 * other devices. The i2c's EEPROM also stores bad page tables on boards
	 * with ECC.
	 */
	int (*i2c_init)(struct smu_context *smu);

	/**
	 * @i2c_fini: Tear down i2c.
	 */
	void (*i2c_fini)(struct smu_context *smu);

	/**
	 * @get_unique_id: Get the GPU's unique id. Used for asset tracking.
	 */
	void (*get_unique_id)(struct smu_context *smu);

	/**
	 * @get_dpm_clock_table: Get a copy of the DPM clock table.
	 *
	 * Used by display component in bandwidth and watermark calculations.
	 */
	int (*get_dpm_clock_table)(struct smu_context *smu, struct dpm_clocks *clock_table);

	/**
	 * @init_microcode: Request the SMU's firmware from the kernel.
	 */
	int (*init_microcode)(struct smu_context *smu);

	/**
	 * @load_microcode: Load firmware onto the SMU.
	 */
	int (*load_microcode)(struct smu_context *smu);

	/**
	 * @fini_microcode: Release the SMU's firmware.
	 */
	void (*fini_microcode)(struct smu_context *smu);

	/**
	 * @init_smc_tables: Initialize the SMU tables.
	 */
	int (*init_smc_tables)(struct smu_context *smu);

	/**
	 * @fini_smc_tables: Release the SMU tables.
	 */
	int (*fini_smc_tables)(struct smu_context *smu);

	/**
	 * @init_power: Initialize the power gate table context.
	 */
	int (*init_power)(struct smu_context *smu);

	/**
	 * @fini_power: Release the power gate table context.
	 */
	int (*fini_power)(struct smu_context *smu);

	/**
	 * @check_fw_status: Check the SMU's firmware status.
	 *
	 * Return: Zero if check passes, negative errno on failure.
	 */
	int (*check_fw_status)(struct smu_context *smu);

	/**
	 * @set_mp1_state: put SMU into a correct state for comming
	 *                 resume from runpm or gpu reset.
	 */
	int (*set_mp1_state)(struct smu_context *smu,
			     enum pp_mp1_state mp1_state);

	/**
	 * @setup_pptable: Initialize the power play table and populate it with
	 *                 default values.
	 */
	int (*setup_pptable)(struct smu_context *smu);

	/**
	 * @get_vbios_bootup_values: Get default boot values from the VBIOS.
	 */
	int (*get_vbios_bootup_values)(struct smu_context *smu);

	/**
	 * @check_fw_version: Print driver and SMU interface versions to the
	 *                    system log.
	 *
	 * Interface mismatch is not a critical failure.
	 */
	int (*check_fw_version)(struct smu_context *smu);

	/**
	 * @powergate_sdma: Power up/down system direct memory access.
	 */
	int (*powergate_sdma)(struct smu_context *smu, bool gate);

	/**
	 * @set_gfx_cgpg: Enable/disable graphics engine course grain power
	 *                gating.
	 */
	int (*set_gfx_cgpg)(struct smu_context *smu, bool enable);

	/**
	 * @write_pptable: Write the power play table to the SMU.
	 */
	int (*write_pptable)(struct smu_context *smu);

	/**
	 * @set_driver_table_location: Send the location of the driver table to
	 *                             the SMU.
	 */
	int (*set_driver_table_location)(struct smu_context *smu);

	/**
	 * @set_tool_table_location: Send the location of the tool table to the
	 *                           SMU.
	 */
	int (*set_tool_table_location)(struct smu_context *smu);

	/**
	 * @notify_memory_pool_location: Send the location of the memory pool to
	 *                               the SMU.
	 */
	int (*notify_memory_pool_location)(struct smu_context *smu);

	/**
	 * @system_features_control: Enable/disable all SMU features.
	 */
	int (*system_features_control)(struct smu_context *smu, bool en);

	/**
	 * @send_smc_msg_with_param: Send a message with a parameter to the SMU.
	 * &msg: Type of message.
	 * &param: Message parameter.
	 * &read_arg: SMU response (optional).
	 */
	int (*send_smc_msg_with_param)(struct smu_context *smu,
				       enum smu_message_type msg, uint32_t param, uint32_t *read_arg);

	/**
	 * @send_smc_msg: Send a message to the SMU.
	 * &msg: Type of message.
	 * &read_arg: SMU response (optional).
	 */
	int (*send_smc_msg)(struct smu_context *smu,
			    enum smu_message_type msg,
			    uint32_t *read_arg);

	/**
	 * @init_display_count: Notify the SMU of the number of display
	 *                      components in current display configuration.
	 */
	int (*init_display_count)(struct smu_context *smu, uint32_t count);

	/**
	 * @set_allowed_mask: Notify the SMU of the features currently allowed
	 *                    by the driver.
	 */
	int (*set_allowed_mask)(struct smu_context *smu);

	/**
	 * @get_enabled_mask: Get a mask of features that are currently enabled
	 *                    on the SMU.
	 * &feature_mask: Enabled feature mask.
	 */
	int (*get_enabled_mask)(struct smu_context *smu, uint64_t *feature_mask);

	/**
	 * @feature_is_enabled: Test if a feature is enabled.
	 *
	 * Return: One if enabled, zero if disabled.
	 */
	int (*feature_is_enabled)(struct smu_context *smu, enum smu_feature_mask mask);

	/**
	 * @disable_all_features_with_exception: Disable all features with
	 *                                       exception to those in &mask.
	 */
	int (*disable_all_features_with_exception)(struct smu_context *smu,
						   enum smu_feature_mask mask);

	/**
	 * @notify_display_change: Enable fast memory clock switching.
	 *
	 * Allows for fine grained memory clock switching but has more stringent
	 * timing requirements.
	 */
	int (*notify_display_change)(struct smu_context *smu);

	/**
	 * @set_power_limit: Set power limit in watts.
	 */
	int (*set_power_limit)(struct smu_context *smu,
			       enum smu_ppt_limit_type limit_type,
			       uint32_t limit);

	/**
	 * @init_max_sustainable_clocks: Populate max sustainable clock speed
	 *                               table with values from the SMU.
	 */
	int (*init_max_sustainable_clocks)(struct smu_context *smu);

	/**
	 * @enable_thermal_alert: Enable thermal alert interrupts.
	 */
	int (*enable_thermal_alert)(struct smu_context *smu);

	/**
	 * @disable_thermal_alert: Disable thermal alert interrupts.
	 */
	int (*disable_thermal_alert)(struct smu_context *smu);

	/**
	 * @set_min_dcef_deep_sleep: Set a minimum display fabric deep sleep
	 *                           clock speed in MHz.
	 */
	int (*set_min_dcef_deep_sleep)(struct smu_context *smu, uint32_t clk);

	/**
	 * @display_clock_voltage_request: Set a hard minimum frequency
	 * for a clock domain.
	 */
	int (*display_clock_voltage_request)(struct smu_context *smu, struct
					     pp_display_clock_request
					     *clock_req);

	/**
	 * @get_fan_control_mode: Get the current fan control mode.
	 */
	uint32_t (*get_fan_control_mode)(struct smu_context *smu);

	/**
	 * @set_fan_control_mode: Set the fan control mode.
	 */
	int (*set_fan_control_mode)(struct smu_context *smu, uint32_t mode);

	/**
	 * @set_fan_speed_pwm: Set a static fan speed in PWM.
	 */
	int (*set_fan_speed_pwm)(struct smu_context *smu, uint32_t speed);

	/**
	 * @set_fan_speed_rpm: Set a static fan speed in rpm.
	 */
	int (*set_fan_speed_rpm)(struct smu_context *smu, uint32_t speed);

	/**
	 * @set_xgmi_pstate: Set inter-chip global memory interconnect pstate.
	 * &pstate: Pstate to set. D0 if Nonzero, D3 otherwise.
	 */
	int (*set_xgmi_pstate)(struct smu_context *smu, uint32_t pstate);

	/**
	 * @gfx_off_control: Enable/disable graphics engine poweroff.
	 */
	int (*gfx_off_control)(struct smu_context *smu, bool enable);


	/**
	 * @get_gfx_off_status: Get graphics engine poweroff status.
	 *
	 * Return:
	 * 0 - GFXOFF(default).
	 * 1 - Transition out of GFX State.
	 * 2 - Not in GFXOFF.
	 * 3 - Transition into GFXOFF.
	 */
	uint32_t (*get_gfx_off_status)(struct smu_context *smu);

	/**
	 * @gfx_off_entrycount: total GFXOFF entry count at the time of
	 * query since system power-up
	 */
	u32 (*get_gfx_off_entrycount)(struct smu_context *smu, uint64_t *entrycount);

	/**
	 * @set_gfx_off_residency: set 1 to start logging, 0 to stop logging
	 */
	u32 (*set_gfx_off_residency)(struct smu_context *smu, bool start);

	/**
	 * @get_gfx_off_residency: Average GFXOFF residency % during the logging interval
	 */
	u32 (*get_gfx_off_residency)(struct smu_context *smu, uint32_t *residency);

	/**
	 * @register_irq_handler: Register interupt request handlers.
	 */
	int (*register_irq_handler)(struct smu_context *smu);

	/**
	 * @set_azalia_d3_pme: Wake the audio decode engine from d3 sleep.
	 */
	int (*set_azalia_d3_pme)(struct smu_context *smu);

	/**
	 * @get_max_sustainable_clocks_by_dc: Get a copy of the max sustainable
	 *                                    clock speeds table.
	 *
	 * Provides a way for the display component (DC) to get the max
	 * sustainable clocks from the SMU.
	 */
	int (*get_max_sustainable_clocks_by_dc)(struct smu_context *smu, struct pp_smu_nv_clock_table *max_clocks);

	/**
	 * @baco_is_support: Check if GPU supports BACO (Bus Active, Chip Off).
	 */
	bool (*baco_is_support)(struct smu_context *smu);

	/**
	 * @baco_get_state: Get the current BACO state.
	 *
	 * Return: Current BACO state.
	 */
	enum smu_baco_state (*baco_get_state)(struct smu_context *smu);

	/**
	 * @baco_set_state: Enter/exit BACO.
	 */
	int (*baco_set_state)(struct smu_context *smu, enum smu_baco_state state);

	/**
	 * @baco_enter: Enter BACO.
	 */
	int (*baco_enter)(struct smu_context *smu);

	/**
	 * @baco_exit: Exit Baco.
	 */
	int (*baco_exit)(struct smu_context *smu);

	/**
	 * @mode1_reset_is_support: Check if GPU supports mode1 reset.
	 */
	bool (*mode1_reset_is_support)(struct smu_context *smu);
	/**
	 * @mode2_reset_is_support: Check if GPU supports mode2 reset.
	 */
	bool (*mode2_reset_is_support)(struct smu_context *smu);

	/**
	 * @mode1_reset: Perform mode1 reset.
	 *
	 * Complete GPU reset.
	 */
	int (*mode1_reset)(struct smu_context *smu);

	/**
	 * @mode2_reset: Perform mode2 reset.
	 *
	 * Mode2 reset generally does not reset as many IPs as mode1 reset. The
	 * IPs reset varies by asic.
	 */
	int (*mode2_reset)(struct smu_context *smu);
	/* for gfx feature enablement after mode2 reset */
	int (*enable_gfx_features)(struct smu_context *smu);

	/**
	 * @get_dpm_ultimate_freq: Get the hard frequency range of a clock
	 *                         domain in MHz.
	 */
	int (*get_dpm_ultimate_freq)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t *min, uint32_t *max);

	/**
	 * @set_soft_freq_limited_range: Set the soft frequency range of a clock
	 *                               domain in MHz.
	 */
	int (*set_soft_freq_limited_range)(struct smu_context *smu, enum smu_clk_type clk_type, uint32_t min, uint32_t max);

	/**
	 * @set_power_source: Notify the SMU of the current power source.
	 */
	int (*set_power_source)(struct smu_context *smu, enum smu_power_src_type power_src);

	/**
	 * @log_thermal_throttling_event: Print a thermal throttling warning to
	 *                                the system's log.
	 */
	void (*log_thermal_throttling_event)(struct smu_context *smu);

	/**
	 * @get_pp_feature_mask: Print a human readable table of enabled
	 *                       features to buffer.
	 */
	size_t (*get_pp_feature_mask)(struct smu_context *smu, char *buf);

	/**
	 * @set_pp_feature_mask: Request the SMU enable/disable features to
	 *                       match those enabled in &new_mask.
	 */
	int (*set_pp_feature_mask)(struct smu_context *smu, uint64_t new_mask);

	/**
	 * @get_gpu_metrics: Get a copy of the GPU metrics table from the SMU.
	 *
	 * Return: Size of &table
	 */
	ssize_t (*get_gpu_metrics)(struct smu_context *smu, void **table);

	/**
	 * @enable_mgpu_fan_boost: Enable multi-GPU fan boost.
	 */
	int (*enable_mgpu_fan_boost)(struct smu_context *smu);

	/**
	 * @gfx_ulv_control: Enable/disable ultra low voltage.
	 */
	int (*gfx_ulv_control)(struct smu_context *smu, bool enablement);

	/**
	 * @deep_sleep_control: Enable/disable deep sleep.
	 */
	int (*deep_sleep_control)(struct smu_context *smu, bool enablement);

	/**
	 * @get_fan_parameters: Get fan parameters.
	 *
	 * Get maximum fan speed from the power play table.
	 */
	int (*get_fan_parameters)(struct smu_context *smu);

	/**
	 * @post_init: Helper function for asic specific workarounds.
	 */
	int (*post_init)(struct smu_context *smu);

	/**
	 * @interrupt_work: Work task scheduled from SMU interrupt handler.
	 */
	void (*interrupt_work)(struct smu_context *smu);

	/**
	 * @gpo_control: Enable/disable graphics power optimization if supported.
	 */
	int (*gpo_control)(struct smu_context *smu, bool enablement);

	/**
	 * @gfx_state_change_set: Send the current graphics state to the SMU.
	 */
	int (*gfx_state_change_set)(struct smu_context *smu, uint32_t state);

	/**
	 * @set_fine_grain_gfx_freq_parameters: Set fine grain graphics clock
	 *                                      parameters to defaults.
	 */
	int (*set_fine_grain_gfx_freq_parameters)(struct smu_context *smu);

	/**
	 * @smu_handle_passthrough_sbr:  Send message to SMU about special handling for SBR.
	 */
	int (*smu_handle_passthrough_sbr)(struct smu_context *smu, bool enable);

	/**
	 * @wait_for_event:  Wait for events from SMU.
	 */
	int (*wait_for_event)(struct smu_context *smu,
			      enum smu_event_type event, uint64_t event_arg);

	/**
	 * @sned_hbm_bad_pages_num:  message SMU to update bad page number
	 *										of SMUBUS table.
	 */
	int (*send_hbm_bad_pages_num)(struct smu_context *smu, uint32_t size);

	/**
	 * @get_ecc_table:  message SMU to get ECC INFO table.
	 */
	ssize_t (*get_ecc_info)(struct smu_context *smu, void *table);
	
	
	/**
	 * @stb_collect_info: Collects Smart Trace Buffers data.
	 */
	int (*stb_collect_info)(struct smu_context *smu, void *buf, uint32_t size);

	/**
	 * @get_default_config_table_settings: Get the ASIC default DriverSmuConfig table settings.
	 */
	int (*get_default_config_table_settings)(struct smu_context *smu, struct config_table_setting *table);

	/**
	 * @set_config_table: Apply the input DriverSmuConfig table settings.
	 */
	int (*set_config_table)(struct smu_context *smu, struct config_table_setting *table);

	/**
	 * @sned_hbm_bad_channel_flag:  message SMU to update bad channel info
	 *										of SMUBUS table.
	 */
	int (*send_hbm_bad_channel_flag)(struct smu_context *smu, uint32_t size);

	/**
	 * @init_pptable_microcode: Prepare the pptable microcode to upload via PSP
	 */
	int (*init_pptable_microcode)(struct smu_context *smu);
};

typedef enum {
	METRICS_CURR_GFXCLK,
	METRICS_CURR_SOCCLK,
	METRICS_CURR_UCLK,
	METRICS_CURR_VCLK,
	METRICS_CURR_VCLK1,
	METRICS_CURR_DCLK,
	METRICS_CURR_DCLK1,
	METRICS_CURR_FCLK,
	METRICS_CURR_DCEFCLK,
	METRICS_AVERAGE_CPUCLK,
	METRICS_AVERAGE_GFXCLK,
	METRICS_AVERAGE_SOCCLK,
	METRICS_AVERAGE_FCLK,
	METRICS_AVERAGE_UCLK,
	METRICS_AVERAGE_VCLK,
	METRICS_AVERAGE_DCLK,
	METRICS_AVERAGE_VCLK1,
	METRICS_AVERAGE_DCLK1,
	METRICS_AVERAGE_GFXACTIVITY,
	METRICS_AVERAGE_MEMACTIVITY,
	METRICS_AVERAGE_VCNACTIVITY,
	METRICS_AVERAGE_SOCKETPOWER,
	METRICS_TEMPERATURE_EDGE,
	METRICS_TEMPERATURE_HOTSPOT,
	METRICS_TEMPERATURE_MEM,
	METRICS_TEMPERATURE_VRGFX,
	METRICS_TEMPERATURE_VRSOC,
	METRICS_TEMPERATURE_VRMEM,
	METRICS_THROTTLER_STATUS,
	METRICS_CURR_FANSPEED,
	METRICS_VOLTAGE_VDDSOC,
	METRICS_VOLTAGE_VDDGFX,
	METRICS_SS_APU_SHARE,
	METRICS_SS_DGPU_SHARE,
	METRICS_UNIQUE_ID_UPPER32,
	METRICS_UNIQUE_ID_LOWER32,
	METRICS_PCIE_RATE,
	METRICS_PCIE_WIDTH,
	METRICS_CURR_FANPWM,
} MetricsMember_t;

enum smu_cmn2asic_mapping_type {
	CMN2ASIC_MAPPING_MSG,
	CMN2ASIC_MAPPING_CLK,
	CMN2ASIC_MAPPING_FEATURE,
	CMN2ASIC_MAPPING_TABLE,
	CMN2ASIC_MAPPING_PWR,
	CMN2ASIC_MAPPING_WORKLOAD,
};

enum smu_baco_seq {
	BACO_SEQ_BACO = 0,
	BACO_SEQ_MSR,
	BACO_SEQ_BAMACO,
	BACO_SEQ_ULPS,
	BACO_SEQ_COUNT,
};

#define MSG_MAP(msg, index, valid_in_vf) \
	[SMU_MSG_##msg] = {1, (index), (valid_in_vf)}

#define CLK_MAP(clk, index) \
	[SMU_##clk] = {1, (index)}

#define FEA_MAP(fea) \
	[SMU_FEATURE_##fea##_BIT] = {1, FEATURE_##fea##_BIT}

#define FEA_MAP_REVERSE(fea) \
	[SMU_FEATURE_DPM_##fea##_BIT] = {1, FEATURE_##fea##_DPM_BIT}

#define FEA_MAP_HALF_REVERSE(fea) \
	[SMU_FEATURE_DPM_##fea##CLK_BIT] = {1, FEATURE_##fea##_DPM_BIT}

#define TAB_MAP(tab) \
	[SMU_TABLE_##tab] = {1, TABLE_##tab}

#define TAB_MAP_VALID(tab) \
	[SMU_TABLE_##tab] = {1, TABLE_##tab}

#define TAB_MAP_INVALID(tab) \
	[SMU_TABLE_##tab] = {0, TABLE_##tab}

#define PWR_MAP(tab) \
	[SMU_POWER_SOURCE_##tab] = {1, POWER_SOURCE_##tab}

#define WORKLOAD_MAP(profile, workload) \
	[profile] = {1, (workload)}

/**
 * smu_memcpy_trailing - Copy the end of one structure into the middle of another
 *
 * @dst: Pointer to destination struct
 * @first_dst_member: The member name in @dst where the overwrite begins
 * @last_dst_member: The member name in @dst where the overwrite ends after
 * @src: Pointer to the source struct
 * @first_src_member: The member name in @src where the copy begins
 *
 */
#define smu_memcpy_trailing(dst, first_dst_member, last_dst_member,	   \
			    src, first_src_member)			   \
({									   \
	size_t __src_offset = offsetof(typeof(*(src)), first_src_member);  \
	size_t __src_size = sizeof(*(src)) - __src_offset;		   \
	size_t __dst_offset = offsetof(typeof(*(dst)), first_dst_member);  \
	size_t __dst_size = offsetofend(typeof(*(dst)), last_dst_member) - \
			    __dst_offset;				   \
	BUILD_BUG_ON(__src_size != __dst_size);				   \
	__builtin_memcpy((u8 *)(dst) + __dst_offset,			   \
			 (u8 *)(src) + __src_offset,			   \
			 __dst_size);					   \
})

#if !defined(SWSMU_CODE_LAYER_L2) && !defined(SWSMU_CODE_LAYER_L3) && !defined(SWSMU_CODE_LAYER_L4)
int smu_get_power_limit(void *handle,
			uint32_t *limit,
			enum pp_power_limit_level pp_limit_level,
			enum pp_power_type pp_power_type);

bool smu_mode1_reset_is_support(struct smu_context *smu);
bool smu_mode2_reset_is_support(struct smu_context *smu);
int smu_mode1_reset(struct smu_context *smu);

extern const struct amd_ip_funcs smu_ip_funcs;

bool is_support_sw_smu(struct amdgpu_device *adev);
bool is_support_cclk_dpm(struct amdgpu_device *adev);
int smu_write_watermarks_table(struct smu_context *smu);

int smu_get_dpm_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			   uint32_t *min, uint32_t *max);

int smu_set_soft_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max);

int smu_set_gfx_power_up_by_imu(struct smu_context *smu);

int smu_set_ac_dc(struct smu_context *smu);

int smu_allow_xgmi_power_down(struct smu_context *smu, bool en);

int smu_get_entrycount_gfxoff(struct smu_context *smu, u64 *value);

int smu_get_residency_gfxoff(struct smu_context *smu, u32 *value);

int smu_set_residency_gfxoff(struct smu_context *smu, bool value);

int smu_get_status_gfxoff(struct smu_context *smu, uint32_t *value);

int smu_handle_passthrough_sbr(struct smu_context *smu, bool enable);

int smu_wait_for_event(struct smu_context *smu, enum smu_event_type event,
		       uint64_t event_arg);
int smu_get_ecc_info(struct smu_context *smu, void *umc_ecc);
int smu_stb_collect_info(struct smu_context *smu, void *buff, uint32_t size);
void amdgpu_smu_stb_debug_fs_init(struct amdgpu_device *adev);
int smu_send_hbm_bad_pages_num(struct smu_context *smu, uint32_t size);
int smu_send_hbm_bad_channel_flag(struct smu_context *smu, uint32_t size);
#endif
#endif
