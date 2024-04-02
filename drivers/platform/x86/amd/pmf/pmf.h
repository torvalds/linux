/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#ifndef PMF_H
#define PMF_H

#include <linux/acpi.h>
#include <linux/platform_profile.h>

#define POLICY_BUF_MAX_SZ		0x4b000
#define POLICY_SIGN_COOKIE		0x31535024
#define POLICY_COOKIE_OFFSET		0x10

struct cookie_header {
	u32 sign;
	u32 length;
} __packed;

/* APMF Functions */
#define APMF_FUNC_VERIFY_INTERFACE			0
#define APMF_FUNC_GET_SYS_PARAMS			1
#define APMF_FUNC_SBIOS_REQUESTS			2
#define APMF_FUNC_SBIOS_HEARTBEAT			4
#define APMF_FUNC_AUTO_MODE					5
#define APMF_FUNC_SET_FAN_IDX				7
#define APMF_FUNC_OS_POWER_SLIDER_UPDATE		8
#define APMF_FUNC_STATIC_SLIDER_GRANULAR       9
#define APMF_FUNC_DYN_SLIDER_AC				11
#define APMF_FUNC_DYN_SLIDER_DC				12
#define APMF_FUNC_SBIOS_HEARTBEAT_V2			16

/* Message Definitions */
#define SET_SPL				0x03 /* SPL: Sustained Power Limit */
#define SET_SPPT			0x05 /* SPPT: Slow Package Power Tracking */
#define SET_FPPT			0x07 /* FPPT: Fast Package Power Tracking */
#define GET_SPL				0x0B
#define GET_SPPT			0x0D
#define GET_FPPT			0x0F
#define SET_DRAM_ADDR_HIGH	0x14
#define SET_DRAM_ADDR_LOW	0x15
#define SET_TRANSFER_TABLE	0x16
#define SET_STT_MIN_LIMIT	0x18 /* STT: Skin Temperature Tracking */
#define SET_STT_LIMIT_APU	0x19
#define SET_STT_LIMIT_HS2	0x1A
#define SET_SPPT_APU_ONLY	0x1D
#define GET_SPPT_APU_ONLY	0x1E
#define GET_STT_MIN_LIMIT	0x1F
#define GET_STT_LIMIT_APU	0x20
#define GET_STT_LIMIT_HS2	0x21
#define SET_P3T				0x23 /* P3T: Peak Package Power Limit */
#define SET_PMF_PPT            0x25
#define SET_PMF_PPT_APU_ONLY   0x26

/* OS slider update notification */
#define DC_BEST_PERF		0
#define DC_BETTER_PERF		1
#define DC_BATTERY_SAVER	3
#define AC_BEST_PERF		4
#define AC_BETTER_PERF		5
#define AC_BETTER_BATTERY	6

/* Fan Index for Auto Mode */
#define FAN_INDEX_AUTO		0xFFFFFFFF

#define ARG_NONE 0
#define AVG_SAMPLE_SIZE 3

/* Policy Actions */
#define PMF_POLICY_SPL						2
#define PMF_POLICY_SPPT						3
#define PMF_POLICY_FPPT						4
#define PMF_POLICY_SPPT_APU_ONLY				5
#define PMF_POLICY_STT_MIN					6
#define PMF_POLICY_STT_SKINTEMP_APU				7
#define PMF_POLICY_STT_SKINTEMP_HS2				8
#define PMF_POLICY_SYSTEM_STATE					9
#define PMF_POLICY_P3T						38

/* TA macros */
#define PMF_TA_IF_VERSION_MAJOR				1
#define TA_PMF_ACTION_MAX					32
#define TA_PMF_UNDO_MAX						8
#define TA_OUTPUT_RESERVED_MEM				906
#define MAX_OPERATION_PARAMS					4

#define PMF_IF_V1		1
#define PMF_IF_V2		2

#define APTS_MAX_STATES		16

/* APTS PMF BIOS Interface */
struct amd_pmf_apts_output {
	u16 table_version;
	u32 fan_table_idx;
	u32 pmf_ppt;
	u32 ppt_pmf_apu_only;
	u32 stt_min_limit;
	u8 stt_skin_temp_limit_apu;
	u8 stt_skin_temp_limit_hs2;
} __packed;

struct amd_pmf_apts_granular_output {
	u16 size;
	struct amd_pmf_apts_output val;
} __packed;

struct amd_pmf_apts_granular {
	u16 size;
	struct amd_pmf_apts_output val[APTS_MAX_STATES];
};

struct sbios_hb_event_v2 {
	u16 size;
	u8 load;
	u8 unload;
	u8 suspend;
	u8 resume;
} __packed;

enum sbios_hb_v2 {
	ON_LOAD,
	ON_UNLOAD,
	ON_SUSPEND,
	ON_RESUME,
};

/* AMD PMF BIOS interfaces */
struct apmf_verify_interface {
	u16 size;
	u16 version;
	u32 notification_mask;
	u32 supported_functions;
} __packed;

struct apmf_system_params {
	u16 size;
	u32 valid_mask;
	u32 flags;
	u8 command_code;
	u32 heartbeat_int;
} __packed;

struct apmf_sbios_req {
	u16 size;
	u32 pending_req;
	u8 rsd;
	u8 cql_event;
	u8 amt_event;
	u32 fppt;
	u32 sppt;
	u32 fppt_apu_only;
	u32 spl;
	u32 stt_min_limit;
	u8 skin_temp_apu;
	u8 skin_temp_hs2;
} __packed;

struct apmf_sbios_req_v2 {
	u16 size;
	u32 pending_req;
	u8 rsd;
	u32 ppt_pmf;
	u32 ppt_pmf_apu_only;
	u32 stt_min_limit;
	u8 skin_temp_apu;
	u8 skin_temp_hs2;
	u32 custom_policy[10];
} __packed;

struct apmf_fan_idx {
	u16 size;
	u8 fan_ctl_mode;
	u32 fan_ctl_idx;
} __packed;

struct smu_pmf_metrics {
	u16 gfxclk_freq; /* in MHz */
	u16 socclk_freq; /* in MHz */
	u16 vclk_freq; /* in MHz */
	u16 dclk_freq; /* in MHz */
	u16 memclk_freq; /* in MHz */
	u16 spare;
	u16 gfx_activity; /* in Centi */
	u16 uvd_activity; /* in Centi */
	u16 voltage[2]; /* in mV */
	u16 currents[2]; /* in mA */
	u16 power[2];/* in mW */
	u16 core_freq[8]; /* in MHz */
	u16 core_power[8]; /* in mW */
	u16 core_temp[8]; /* in centi-Celsius */
	u16 l3_freq; /* in MHz */
	u16 l3_temp; /* in centi-Celsius */
	u16 gfx_temp; /* in centi-Celsius */
	u16 soc_temp; /* in centi-Celsius */
	u16 throttler_status;
	u16 current_socketpower; /* in mW */
	u16 stapm_orig_limit; /* in W */
	u16 stapm_cur_limit; /* in W */
	u32 apu_power; /* in mW */
	u32 dgpu_power; /* in mW */
	u16 vdd_tdc_val; /* in mA */
	u16 soc_tdc_val; /* in mA */
	u16 vdd_edc_val; /* in mA */
	u16 soc_edcv_al; /* in mA */
	u16 infra_cpu_maxfreq; /* in MHz */
	u16 infra_gfx_maxfreq; /* in MHz */
	u16 skin_temp; /* in centi-Celsius */
	u16 device_state;
	u16 curtemp; /* in centi-Celsius */
	u16 filter_alpha_value;
	u16 avg_gfx_clkfrequency;
	u16 avg_fclk_frequency;
	u16 avg_gfx_activity;
	u16 avg_socclk_frequency;
	u16 avg_vclk_frequency;
	u16 avg_vcn_activity;
	u16 avg_dram_reads;
	u16 avg_dram_writes;
	u16 avg_socket_power;
	u16 avg_core_power[2];
	u16 avg_core_c0residency[16];
	u16 spare1;
	u32 metrics_counter;
} __packed;

enum amd_stt_skin_temp {
	STT_TEMP_APU,
	STT_TEMP_HS2,
	STT_TEMP_COUNT,
};

enum amd_slider_op {
	SLIDER_OP_GET,
	SLIDER_OP_SET,
};

enum power_source {
	POWER_SOURCE_AC,
	POWER_SOURCE_DC,
	POWER_SOURCE_MAX,
};

enum power_modes {
	POWER_MODE_PERFORMANCE,
	POWER_MODE_BALANCED_POWER,
	POWER_MODE_POWER_SAVER,
	POWER_MODE_MAX,
};

enum power_modes_v2 {
	POWER_MODE_BEST_PERFORMANCE,
	POWER_MODE_BALANCED,
	POWER_MODE_BEST_POWER_EFFICIENCY,
	POWER_MODE_ENERGY_SAVE,
	POWER_MODE_V2_MAX,
};

struct amd_pmf_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void *buf;
	u32 base_addr;
	u32 cpu_id;
	struct device *dev;
	struct mutex lock; /* protects the PMF interface */
	u32 supported_func;
	enum platform_profile_option current_profile;
	struct platform_profile_handler pprof;
	struct dentry *dbgfs_dir;
	int hb_interval; /* SBIOS heartbeat interval */
	struct delayed_work heart_beat;
	struct smu_pmf_metrics m_table;
	struct delayed_work work_buffer;
	ktime_t start_time;
	int socket_power_history[AVG_SAMPLE_SIZE];
	int socket_power_history_idx;
	bool amt_enabled;
	struct mutex update_mutex; /* protects race between ACPI handler and metrics thread */
	bool cnqf_enabled;
	bool cnqf_supported;
	struct notifier_block pwr_src_notifier;
	/* Smart PC solution builder */
	struct dentry *esbin;
	unsigned char *policy_buf;
	u32 policy_sz;
	struct tee_context *tee_ctx;
	struct tee_shm *fw_shm_pool;
	u32 session_id;
	void *shbuf;
	struct delayed_work pb_work;
	struct pmf_action_table *prev_data;
	u64 policy_addr;
	void __iomem *policy_base;
	bool smart_pc_enabled;
	u16 pmf_if_version;
};

struct apmf_sps_prop_granular_v2 {
	u8 power_states[POWER_SOURCE_MAX][POWER_MODE_V2_MAX];
} __packed;

struct apmf_sps_prop_granular {
	u32 fppt;
	u32 sppt;
	u32 sppt_apu_only;
	u32 spl;
	u32 stt_min;
	u8 stt_skin_temp[STT_TEMP_COUNT];
	u32 fan_id;
} __packed;

/* Static Slider */
struct apmf_static_slider_granular_output {
	u16 size;
	struct apmf_sps_prop_granular prop[POWER_SOURCE_MAX * POWER_MODE_MAX];
} __packed;

struct amd_pmf_static_slider_granular {
	u16 size;
	struct apmf_sps_prop_granular prop[POWER_SOURCE_MAX][POWER_MODE_MAX];
};

struct apmf_static_slider_granular_output_v2 {
	u16 size;
	struct apmf_sps_prop_granular_v2 sps_idx;
} __packed;

struct amd_pmf_static_slider_granular_v2 {
	u16 size;
	struct apmf_sps_prop_granular_v2 sps_idx;
};

struct os_power_slider {
	u16 size;
	u8 slider_event;
} __packed;

struct fan_table_control {
	bool manual;
	unsigned long fan_id;
};

struct power_table_control {
	u32 spl;
	u32 sppt;
	u32 fppt;
	u32 sppt_apu_only;
	u32 stt_min;
	u32 stt_skin_temp[STT_TEMP_COUNT];
	u32 reserved[16];
};

/* Auto Mode Layer */
enum auto_mode_transition_priority {
	AUTO_TRANSITION_TO_PERFORMANCE, /* Any other mode to Performance Mode */
	AUTO_TRANSITION_FROM_QUIET_TO_BALANCE, /* Quiet Mode to Balance Mode */
	AUTO_TRANSITION_TO_QUIET, /* Any other mode to Quiet Mode */
	AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE, /* Performance Mode to Balance Mode */
	AUTO_TRANSITION_MAX,
};

enum auto_mode_mode {
	AUTO_QUIET,
	AUTO_BALANCE,
	AUTO_PERFORMANCE_ON_LAP,
	AUTO_PERFORMANCE,
	AUTO_MODE_MAX,
};

struct auto_mode_trans_params {
	u32 time_constant; /* minimum time required to switch to next mode */
	u32 power_delta; /* delta power to shift mode */
	u32 power_threshold;
	u32 timer; /* elapsed time. if timer > TimeThreshold, it will move to next mode */
	u32 applied;
	enum auto_mode_mode target_mode;
	u32 shifting_up;
};

struct auto_mode_mode_settings {
	struct power_table_control power_control;
	struct fan_table_control fan_control;
	u32 power_floor;
};

struct auto_mode_mode_config {
	struct auto_mode_trans_params transition[AUTO_TRANSITION_MAX];
	struct auto_mode_mode_settings mode_set[AUTO_MODE_MAX];
	enum auto_mode_mode current_mode;
};

struct apmf_auto_mode {
	u16 size;
	/* time constant */
	u32 balanced_to_perf;
	u32 perf_to_balanced;
	u32 quiet_to_balanced;
	u32 balanced_to_quiet;
	/* power floor */
	u32 pfloor_perf;
	u32 pfloor_balanced;
	u32 pfloor_quiet;
	/* Power delta for mode change */
	u32 pd_balanced_to_perf;
	u32 pd_perf_to_balanced;
	u32 pd_quiet_to_balanced;
	u32 pd_balanced_to_quiet;
	/* skin temperature limits */
	u8 stt_apu_perf_on_lap; /* CQL ON */
	u8 stt_hs2_perf_on_lap; /* CQL ON */
	u8 stt_apu_perf;
	u8 stt_hs2_perf;
	u8 stt_apu_balanced;
	u8 stt_hs2_balanced;
	u8 stt_apu_quiet;
	u8 stt_hs2_quiet;
	u32 stt_min_limit_perf_on_lap; /* CQL ON */
	u32 stt_min_limit_perf;
	u32 stt_min_limit_balanced;
	u32 stt_min_limit_quiet;
	/* SPL based */
	u32 fppt_perf_on_lap; /* CQL ON */
	u32 sppt_perf_on_lap; /* CQL ON */
	u32 spl_perf_on_lap; /* CQL ON */
	u32 sppt_apu_only_perf_on_lap; /* CQL ON */
	u32 fppt_perf;
	u32 sppt_perf;
	u32 spl_perf;
	u32 sppt_apu_only_perf;
	u32 fppt_balanced;
	u32 sppt_balanced;
	u32 spl_balanced;
	u32 sppt_apu_only_balanced;
	u32 fppt_quiet;
	u32 sppt_quiet;
	u32 spl_quiet;
	u32 sppt_apu_only_quiet;
	/* Fan ID */
	u32 fan_id_perf;
	u32 fan_id_balanced;
	u32 fan_id_quiet;
} __packed;

/* CnQF Layer */
enum cnqf_trans_priority {
	CNQF_TRANSITION_TO_TURBO, /* Any other mode to Turbo Mode */
	CNQF_TRANSITION_FROM_BALANCE_TO_PERFORMANCE, /* quiet/balance to Performance Mode */
	CNQF_TRANSITION_FROM_QUIET_TO_BALANCE, /* Quiet Mode to Balance Mode */
	CNQF_TRANSITION_TO_QUIET, /* Any other mode to Quiet Mode */
	CNQF_TRANSITION_FROM_PERFORMANCE_TO_BALANCE, /* Performance/Turbo to Balance Mode */
	CNQF_TRANSITION_FROM_TURBO_TO_PERFORMANCE, /* Turbo mode to Performance Mode */
	CNQF_TRANSITION_MAX,
};

enum cnqf_mode {
	CNQF_MODE_QUIET,
	CNQF_MODE_BALANCE,
	CNQF_MODE_PERFORMANCE,
	CNQF_MODE_TURBO,
	CNQF_MODE_MAX,
};

enum apmf_cnqf_pos {
	APMF_CNQF_TURBO,
	APMF_CNQF_PERFORMANCE,
	APMF_CNQF_BALANCE,
	APMF_CNQF_QUIET,
	APMF_CNQF_MAX,
};

struct cnqf_mode_settings {
	struct power_table_control power_control;
	struct fan_table_control fan_control;
	u32 power_floor;
};

struct cnqf_tran_params {
	u32 time_constant; /* minimum time required to switch to next mode */
	u32 power_threshold;
	u32 timer; /* elapsed time. if timer > timethreshold, it will move to next mode */
	u32 total_power;
	u32 count;
	bool priority;
	bool shifting_up;
	enum cnqf_mode target_mode;
};

struct cnqf_config {
	struct cnqf_tran_params trans_param[POWER_SOURCE_MAX][CNQF_TRANSITION_MAX];
	struct cnqf_mode_settings mode_set[POWER_SOURCE_MAX][CNQF_MODE_MAX];
	struct power_table_control defaults;
	enum cnqf_mode current_mode;
	u32 power_src;
	u32 avg_power;
};

struct apmf_cnqf_power_set {
	u32 pfloor;
	u32 fppt;
	u32 sppt;
	u32 sppt_apu_only;
	u32 spl;
	u32 stt_min_limit;
	u8 stt_skintemp[STT_TEMP_COUNT];
	u32 fan_id;
} __packed;

struct apmf_dyn_slider_output {
	u16 size;
	u16 flags;
	u32 t_perf_to_turbo;
	u32 t_balanced_to_perf;
	u32 t_quiet_to_balanced;
	u32 t_balanced_to_quiet;
	u32 t_perf_to_balanced;
	u32 t_turbo_to_perf;
	struct apmf_cnqf_power_set ps[APMF_CNQF_MAX];
} __packed;

/* Smart PC - TA internals */
enum system_state {
	SYSTEM_STATE_S0i3,
	SYSTEM_STATE_S4,
	SYSTEM_STATE_SCREEN_LOCK,
	SYSTEM_STATE_MAX,
};

enum ta_slider {
	TA_BEST_BATTERY,
	TA_BETTER_BATTERY,
	TA_BETTER_PERFORMANCE,
	TA_BEST_PERFORMANCE,
	TA_MAX,
};

/* Command ids for TA communication */
enum ta_pmf_command {
	TA_PMF_COMMAND_POLICY_BUILDER_INITIALIZE,
	TA_PMF_COMMAND_POLICY_BUILDER_ENACT_POLICIES,
};

enum ta_pmf_error_type {
	TA_PMF_TYPE_SUCCESS,
	TA_PMF_ERROR_TYPE_GENERIC,
	TA_PMF_ERROR_TYPE_CRYPTO,
	TA_PMF_ERROR_TYPE_CRYPTO_VALIDATE,
	TA_PMF_ERROR_TYPE_CRYPTO_VERIFY_OEM,
	TA_PMF_ERROR_TYPE_POLICY_BUILDER,
	TA_PMF_ERROR_TYPE_PB_CONVERT,
	TA_PMF_ERROR_TYPE_PB_SETUP,
	TA_PMF_ERROR_TYPE_PB_ENACT,
	TA_PMF_ERROR_TYPE_ASD_GET_DEVICE_INFO,
	TA_PMF_ERROR_TYPE_ASD_GET_DEVICE_PCIE_INFO,
	TA_PMF_ERROR_TYPE_SYS_DRV_FW_VALIDATION,
	TA_PMF_ERROR_TYPE_MAX,
};

struct pmf_action_table {
	enum system_state system_state;
	u32 spl;		/* in mW */
	u32 sppt;		/* in mW */
	u32 sppt_apuonly;	/* in mW */
	u32 fppt;		/* in mW */
	u32 stt_minlimit;	/* in mW */
	u32 stt_skintemp_apu;	/* in C */
	u32 stt_skintemp_hs2;	/* in C */
	u32 p3t_limit;		/* in mW */
};

/* Input conditions */
struct ta_pmf_condition_info {
	u32 power_source;
	u32 bat_percentage;
	u32 power_slider;
	u32 lid_state;
	bool user_present;
	u32 rsvd1[2];
	u32 monitor_count;
	u32 rsvd2[2];
	u32 bat_design;
	u32 full_charge_capacity;
	int drain_rate;
	bool user_engaged;
	u32 device_state;
	u32 socket_power;
	u32 skin_temperature;
	u32 rsvd3[5];
	u32 ambient_light;
	u32 length;
	u32 avg_c0residency;
	u32 max_c0residency;
	u32 s0i3_entry;
	u32 gfx_busy;
	u32 rsvd4[7];
	bool camera_state;
	u32 workload_type;
	u32 display_type;
	u32 display_state;
	u32 rsvd5[150];
};

struct ta_pmf_load_policy_table {
	u32 table_size;
	u8 table[POLICY_BUF_MAX_SZ];
};

/* TA initialization params */
struct ta_pmf_init_table {
	u32 frequency; /* SMU sampling frequency */
	bool validate;
	bool sku_check;
	bool metadata_macrocheck;
	struct ta_pmf_load_policy_table policies_table;
};

/* Everything the TA needs to Enact Policies */
struct ta_pmf_enact_table {
	struct ta_pmf_condition_info ev_info;
	u32 name;
};

struct ta_pmf_action {
	u32 action_index;
	u32 value;
};

/* Output actions from TA */
struct ta_pmf_enact_result {
	u32 actions_count;
	struct ta_pmf_action actions_list[TA_PMF_ACTION_MAX];
	u32 undo_count;
	struct ta_pmf_action undo_list[TA_PMF_UNDO_MAX];
};

union ta_pmf_input {
	struct ta_pmf_enact_table enact_table;
	struct ta_pmf_init_table init_table;
};

union ta_pmf_output {
	struct ta_pmf_enact_result policy_apply_table;
	u32 rsvd[TA_OUTPUT_RESERVED_MEM];
};

struct ta_pmf_shared_memory {
	int command_id;
	int resp_id;
	u32 pmf_result;
	u32 if_version;
	union ta_pmf_output pmf_output;
	union ta_pmf_input pmf_input;
};

/* Core Layer */
int apmf_acpi_init(struct amd_pmf_dev *pmf_dev);
void apmf_acpi_deinit(struct amd_pmf_dev *pmf_dev);
int is_apmf_func_supported(struct amd_pmf_dev *pdev, unsigned long index);
int amd_pmf_send_cmd(struct amd_pmf_dev *dev, u8 message, bool get, u32 arg, u32 *data);
int amd_pmf_init_metrics_table(struct amd_pmf_dev *dev);
int amd_pmf_get_power_source(void);
int apmf_install_handler(struct amd_pmf_dev *pmf_dev);
int apmf_os_power_slider_update(struct amd_pmf_dev *dev, u8 flag);
int amd_pmf_set_dram_addr(struct amd_pmf_dev *dev, bool alloc_buffer);
int amd_pmf_notify_sbios_heartbeat_event_v2(struct amd_pmf_dev *dev, u8 flag);

/* SPS Layer */
int amd_pmf_get_pprof_modes(struct amd_pmf_dev *pmf);
void amd_pmf_update_slider(struct amd_pmf_dev *dev, bool op, int idx,
			   struct amd_pmf_static_slider_granular *table);
int amd_pmf_init_sps(struct amd_pmf_dev *dev);
void amd_pmf_deinit_sps(struct amd_pmf_dev *dev);
int apmf_get_static_slider_granular(struct amd_pmf_dev *pdev,
				    struct apmf_static_slider_granular_output *output);
bool is_pprof_balanced(struct amd_pmf_dev *pmf);
int amd_pmf_power_slider_update_event(struct amd_pmf_dev *dev);
const char *amd_pmf_source_as_str(unsigned int state);

const char *amd_pmf_source_as_str(unsigned int state);

int apmf_update_fan_idx(struct amd_pmf_dev *pdev, bool manual, u32 idx);
int amd_pmf_set_sps_power_limits(struct amd_pmf_dev *pmf);
int apmf_get_static_slider_granular_v2(struct amd_pmf_dev *dev,
				       struct apmf_static_slider_granular_output_v2 *data);
int apts_get_static_slider_granular_v2(struct amd_pmf_dev *pdev,
				       struct amd_pmf_apts_granular_output *data, u32 apts_idx);

/* Auto Mode Layer */
int apmf_get_auto_mode_def(struct amd_pmf_dev *pdev, struct apmf_auto_mode *data);
void amd_pmf_init_auto_mode(struct amd_pmf_dev *dev);
void amd_pmf_deinit_auto_mode(struct amd_pmf_dev *dev);
void amd_pmf_trans_automode(struct amd_pmf_dev *dev, int socket_power, ktime_t time_elapsed_ms);
int apmf_get_sbios_requests(struct amd_pmf_dev *pdev, struct apmf_sbios_req *req);
int apmf_get_sbios_requests_v2(struct amd_pmf_dev *pdev, struct apmf_sbios_req_v2 *req);

void amd_pmf_update_2_cql(struct amd_pmf_dev *dev, bool is_cql_event);
int amd_pmf_reset_amt(struct amd_pmf_dev *dev);
void amd_pmf_handle_amt(struct amd_pmf_dev *dev);

/* CnQF Layer */
int apmf_get_dyn_slider_def_ac(struct amd_pmf_dev *pdev, struct apmf_dyn_slider_output *data);
int apmf_get_dyn_slider_def_dc(struct amd_pmf_dev *pdev, struct apmf_dyn_slider_output *data);
int amd_pmf_init_cnqf(struct amd_pmf_dev *dev);
void amd_pmf_deinit_cnqf(struct amd_pmf_dev *dev);
int amd_pmf_trans_cnqf(struct amd_pmf_dev *dev, int socket_power, ktime_t time_lapsed_ms);
extern const struct attribute_group cnqf_feature_attribute_group;

/* Smart PC builder Layer */
int amd_pmf_init_smart_pc(struct amd_pmf_dev *dev);
void amd_pmf_deinit_smart_pc(struct amd_pmf_dev *dev);
int apmf_check_smart_pc(struct amd_pmf_dev *pmf_dev);

/* Smart PC - TA interfaces */
void amd_pmf_populate_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in);
void amd_pmf_dump_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in);

#endif /* PMF_H */
