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
#ifndef _HWMGR_H_
#define _HWMGR_H_

#include <linux/seq_file.h>
#include "amd_powerplay.h"
#include "hardwaremanager.h"
#include "hwmgr_ppt.h"
#include "ppatomctrl.h"
#include "power_state.h"
#include "smu_helper.h"

struct pp_hwmgr;
struct phm_fan_speed_info;
struct pp_atomctrl_voltage_table;

#define VOLTAGE_SCALE 4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

enum DISPLAY_GAP {
	DISPLAY_GAP_VBLANK_OR_WM = 0,   /* Wait for vblank or MCHG watermark. */
	DISPLAY_GAP_VBLANK       = 1,   /* Wait for vblank. */
	DISPLAY_GAP_WATERMARK    = 2,   /* Wait for MCHG watermark. (Note that HW may deassert WM in VBI depending on DC_STUTTER_CNTL.) */
	DISPLAY_GAP_IGNORE       = 3    /* Do not wait. */
};
typedef enum DISPLAY_GAP DISPLAY_GAP;

enum BACO_STATE {
	BACO_STATE_OUT = 0,
	BACO_STATE_IN,
};

struct vi_dpm_level {
	bool enabled;
	uint32_t value;
	uint32_t param1;
};

struct vi_dpm_table {
	uint32_t count;
	struct vi_dpm_level dpm_level[];
};

#define PCIE_PERF_REQ_REMOVE_REGISTRY   0
#define PCIE_PERF_REQ_FORCE_LOWPOWER    1
#define PCIE_PERF_REQ_GEN1         2
#define PCIE_PERF_REQ_GEN2         3
#define PCIE_PERF_REQ_GEN3         4

enum PHM_BackEnd_Magic {
	PHM_Dummy_Magic       = 0xAA5555AA,
	PHM_RV770_Magic       = 0xDCBAABCD,
	PHM_Kong_Magic        = 0x239478DF,
	PHM_NIslands_Magic    = 0x736C494E,
	PHM_Sumo_Magic        = 0x8339FA11,
	PHM_SIslands_Magic    = 0x369431AC,
	PHM_Trinity_Magic     = 0x96751873,
	PHM_CIslands_Magic    = 0x38AC78B0,
	PHM_Kv_Magic          = 0xDCBBABC0,
	PHM_VIslands_Magic    = 0x20130307,
	PHM_Cz_Magic          = 0x67DCBA25,
	PHM_Rv_Magic          = 0x20161121
};

struct phm_set_power_state_input {
	const struct pp_hw_power_state *pcurrent_state;
	const struct pp_hw_power_state *pnew_state;
};

struct phm_clock_array {
	uint32_t count;
	uint32_t values[];
};

struct phm_clock_voltage_dependency_record {
	uint32_t clk;
	uint32_t v;
};

struct phm_vceclock_voltage_dependency_record {
	uint32_t ecclk;
	uint32_t evclk;
	uint32_t v;
};

struct phm_uvdclock_voltage_dependency_record {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t v;
};

struct phm_samuclock_voltage_dependency_record {
	uint32_t samclk;
	uint32_t v;
};

struct phm_acpclock_voltage_dependency_record {
	uint32_t acpclk;
	uint32_t v;
};

struct phm_clock_voltage_dependency_table {
	uint32_t count;							/* Number of entries. */
	struct phm_clock_voltage_dependency_record entries[];		/* Dynamically allocate count entries. */
};

struct phm_phase_shedding_limits_record {
	uint32_t  Voltage;
	uint32_t    Sclk;
	uint32_t    Mclk;
};

struct phm_uvd_clock_voltage_dependency_record {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t v;
};

struct phm_uvd_clock_voltage_dependency_table {
	uint8_t count;
	struct phm_uvd_clock_voltage_dependency_record entries[];
};

struct phm_acp_clock_voltage_dependency_record {
	uint32_t acpclk;
	uint32_t v;
};

struct phm_acp_clock_voltage_dependency_table {
	uint32_t count;
	struct phm_acp_clock_voltage_dependency_record entries[];
};

struct phm_vce_clock_voltage_dependency_record {
	uint32_t ecclk;
	uint32_t evclk;
	uint32_t v;
};

struct phm_phase_shedding_limits_table {
	uint32_t                           count;
	struct phm_phase_shedding_limits_record  entries[];
};

struct phm_vceclock_voltage_dependency_table {
	uint8_t count;                                    /* Number of entries. */
	struct phm_vceclock_voltage_dependency_record entries[1]; /* Dynamically allocate count entries. */
};

struct phm_uvdclock_voltage_dependency_table {
	uint8_t count;                                    /* Number of entries. */
	struct phm_uvdclock_voltage_dependency_record entries[1]; /* Dynamically allocate count entries. */
};

struct phm_samuclock_voltage_dependency_table {
	uint8_t count;                                    /* Number of entries. */
	struct phm_samuclock_voltage_dependency_record entries[1]; /* Dynamically allocate count entries. */
};

struct phm_acpclock_voltage_dependency_table {
	uint32_t count;                                    /* Number of entries. */
	struct phm_acpclock_voltage_dependency_record entries[1]; /* Dynamically allocate count entries. */
};

struct phm_vce_clock_voltage_dependency_table {
	uint8_t count;
	struct phm_vce_clock_voltage_dependency_record entries[];
};


enum SMU_ASIC_RESET_MODE {
    SMU_ASIC_RESET_MODE_0,
    SMU_ASIC_RESET_MODE_1,
    SMU_ASIC_RESET_MODE_2,
};

struct pp_smumgr_func {
	char *name;
	int (*smu_init)(struct pp_hwmgr  *hwmgr);
	int (*smu_fini)(struct pp_hwmgr  *hwmgr);
	int (*start_smu)(struct pp_hwmgr  *hwmgr);
	int (*check_fw_load_finish)(struct pp_hwmgr  *hwmgr,
				    uint32_t firmware);
	int (*request_smu_load_fw)(struct pp_hwmgr  *hwmgr);
	int (*request_smu_load_specific_fw)(struct pp_hwmgr  *hwmgr,
					    uint32_t firmware);
	uint32_t (*get_argument)(struct pp_hwmgr  *hwmgr);
	int (*send_msg_to_smc)(struct pp_hwmgr  *hwmgr, uint16_t msg);
	int (*send_msg_to_smc_with_parameter)(struct pp_hwmgr  *hwmgr,
					  uint16_t msg, uint32_t parameter);
	int (*download_pptable_settings)(struct pp_hwmgr  *hwmgr,
					 void **table);
	int (*upload_pptable_settings)(struct pp_hwmgr  *hwmgr);
	int (*update_smc_table)(struct pp_hwmgr *hwmgr, uint32_t type);
	int (*process_firmware_header)(struct pp_hwmgr *hwmgr);
	int (*update_sclk_threshold)(struct pp_hwmgr *hwmgr);
	int (*thermal_setup_fan_table)(struct pp_hwmgr *hwmgr);
	int (*thermal_avfs_enable)(struct pp_hwmgr *hwmgr);
	int (*init_smc_table)(struct pp_hwmgr *hwmgr);
	int (*populate_all_graphic_levels)(struct pp_hwmgr *hwmgr);
	int (*populate_all_memory_levels)(struct pp_hwmgr *hwmgr);
	int (*initialize_mc_reg_table)(struct pp_hwmgr *hwmgr);
	uint32_t (*get_offsetof)(uint32_t type, uint32_t member);
	uint32_t (*get_mac_definition)(uint32_t value);
	bool (*is_dpm_running)(struct pp_hwmgr *hwmgr);
	bool (*is_hw_avfs_present)(struct pp_hwmgr  *hwmgr);
	int (*update_dpm_settings)(struct pp_hwmgr *hwmgr, void *profile_setting);
	int (*smc_table_manager)(struct pp_hwmgr *hwmgr, uint8_t *table, uint16_t table_id, bool rw); /*rw: true for read, false for write */
	int (*stop_smc)(struct pp_hwmgr *hwmgr);
};

struct pp_hwmgr_func {
	int (*backend_init)(struct pp_hwmgr *hw_mgr);
	int (*backend_fini)(struct pp_hwmgr *hw_mgr);
	int (*asic_setup)(struct pp_hwmgr *hw_mgr);
	int (*get_power_state_size)(struct pp_hwmgr *hw_mgr);

	int (*apply_state_adjust_rules)(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps);

	int (*apply_clocks_adjust_rules)(struct pp_hwmgr *hwmgr);

	int (*force_dpm_level)(struct pp_hwmgr *hw_mgr,
					enum amd_dpm_forced_level level);

	int (*dynamic_state_management_enable)(
						struct pp_hwmgr *hw_mgr);
	int (*dynamic_state_management_disable)(
						struct pp_hwmgr *hw_mgr);

	int (*patch_boot_state)(struct pp_hwmgr *hwmgr,
				     struct pp_hw_power_state *hw_ps);

	int (*get_pp_table_entry)(struct pp_hwmgr *hwmgr,
			    unsigned long, struct pp_power_state *);
	int (*get_num_of_pp_table_entries)(struct pp_hwmgr *hwmgr);
	int (*powerdown_uvd)(struct pp_hwmgr *hwmgr);
	void (*powergate_vce)(struct pp_hwmgr *hwmgr, bool bgate);
	void (*powergate_uvd)(struct pp_hwmgr *hwmgr, bool bgate);
	void (*powergate_acp)(struct pp_hwmgr *hwmgr, bool bgate);
	uint32_t (*get_mclk)(struct pp_hwmgr *hwmgr, bool low);
	uint32_t (*get_sclk)(struct pp_hwmgr *hwmgr, bool low);
	int (*power_state_set)(struct pp_hwmgr *hwmgr,
						const void *state);
	int (*notify_smc_display_config_after_ps_adjustment)(struct pp_hwmgr *hwmgr);
	int (*pre_display_config_changed)(struct pp_hwmgr *hwmgr);
	int (*display_config_changed)(struct pp_hwmgr *hwmgr);
	int (*disable_clock_power_gating)(struct pp_hwmgr *hwmgr);
	int (*update_clock_gatings)(struct pp_hwmgr *hwmgr,
						const uint32_t *msg_id);
	int (*set_max_fan_rpm_output)(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm);
	int (*set_max_fan_pwm_output)(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm);
	int (*stop_thermal_controller)(struct pp_hwmgr *hwmgr);
	int (*get_fan_speed_info)(struct pp_hwmgr *hwmgr, struct phm_fan_speed_info *fan_speed_info);
	void (*set_fan_control_mode)(struct pp_hwmgr *hwmgr, uint32_t mode);
	uint32_t (*get_fan_control_mode)(struct pp_hwmgr *hwmgr);
	int (*set_fan_speed_pwm)(struct pp_hwmgr *hwmgr, uint32_t speed);
	int (*get_fan_speed_pwm)(struct pp_hwmgr *hwmgr, uint32_t *speed);
	int (*set_fan_speed_rpm)(struct pp_hwmgr *hwmgr, uint32_t speed);
	int (*get_fan_speed_rpm)(struct pp_hwmgr *hwmgr, uint32_t *speed);
	int (*reset_fan_speed_to_default)(struct pp_hwmgr *hwmgr);
	int (*uninitialize_thermal_controller)(struct pp_hwmgr *hwmgr);
	int (*register_irq_handlers)(struct pp_hwmgr *hwmgr);
	bool (*check_smc_update_required_for_display_configuration)(struct pp_hwmgr *hwmgr);
	int (*check_states_equal)(struct pp_hwmgr *hwmgr,
					const struct pp_hw_power_state *pstate1,
					const struct pp_hw_power_state *pstate2,
					bool *equal);
	int (*set_cpu_power_state)(struct pp_hwmgr *hwmgr);
	int (*store_cc6_data)(struct pp_hwmgr *hwmgr, uint32_t separation_time,
				bool cc6_disable, bool pstate_disable,
				bool pstate_switch_disable);
	int (*get_dal_power_level)(struct pp_hwmgr *hwmgr,
			struct amd_pp_simple_clock_info *info);
	int (*get_performance_level)(struct pp_hwmgr *, const struct pp_hw_power_state *,
			PHM_PerformanceLevelDesignation, uint32_t, PHM_PerformanceLevel *);
	int (*get_current_shallow_sleep_clocks)(struct pp_hwmgr *hwmgr,
				const struct pp_hw_power_state *state, struct pp_clock_info *clock_info);
	int (*get_clock_by_type)(struct pp_hwmgr *hwmgr, enum amd_pp_clock_type type, struct amd_pp_clocks *clocks);
	int (*get_clock_by_type_with_latency)(struct pp_hwmgr *hwmgr,
			enum amd_pp_clock_type type,
			struct pp_clock_levels_with_latency *clocks);
	int (*get_clock_by_type_with_voltage)(struct pp_hwmgr *hwmgr,
			enum amd_pp_clock_type type,
			struct pp_clock_levels_with_voltage *clocks);
	int (*set_watermarks_for_clocks_ranges)(struct pp_hwmgr *hwmgr, void *clock_ranges);
	int (*display_clock_voltage_request)(struct pp_hwmgr *hwmgr,
			struct pp_display_clock_request *clock);
	int (*get_max_high_clocks)(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks);
	int (*power_off_asic)(struct pp_hwmgr *hwmgr);
	int (*force_clock_level)(struct pp_hwmgr *hwmgr, enum pp_clock_type type, uint32_t mask);
	int (*emit_clock_levels)(struct pp_hwmgr *hwmgr,
				 enum pp_clock_type type, char *buf, int *offset);
	int (*print_clock_levels)(struct pp_hwmgr *hwmgr, enum pp_clock_type type, char *buf);
	int (*powergate_gfx)(struct pp_hwmgr *hwmgr, bool enable);
	int (*get_sclk_od)(struct pp_hwmgr *hwmgr);
	int (*set_sclk_od)(struct pp_hwmgr *hwmgr, uint32_t value);
	int (*get_mclk_od)(struct pp_hwmgr *hwmgr);
	int (*set_mclk_od)(struct pp_hwmgr *hwmgr, uint32_t value);
	int (*read_sensor)(struct pp_hwmgr *hwmgr, int idx, void *value, int *size);
	int (*avfs_control)(struct pp_hwmgr *hwmgr, bool enable);
	int (*disable_smc_firmware_ctf)(struct pp_hwmgr *hwmgr);
	int (*set_active_display_count)(struct pp_hwmgr *hwmgr, uint32_t count);
	int (*set_min_deep_sleep_dcefclk)(struct pp_hwmgr *hwmgr, uint32_t clock);
	int (*start_thermal_controller)(struct pp_hwmgr *hwmgr, struct PP_TemperatureRange *range);
	int (*notify_cac_buffer_info)(struct pp_hwmgr *hwmgr,
					uint32_t virtual_addr_low,
					uint32_t virtual_addr_hi,
					uint32_t mc_addr_low,
					uint32_t mc_addr_hi,
					uint32_t size);
	int (*get_thermal_temperature_range)(struct pp_hwmgr *hwmgr,
					struct PP_TemperatureRange *range);
	int (*get_power_profile_mode)(struct pp_hwmgr *hwmgr, char *buf);
	int (*set_power_profile_mode)(struct pp_hwmgr *hwmgr, long *input, uint32_t size);
	int (*odn_edit_dpm_table)(struct pp_hwmgr *hwmgr,
					enum PP_OD_DPM_TABLE_COMMAND type,
					long *input, uint32_t size);
	int (*set_fine_grain_clk_vol)(struct pp_hwmgr *hwmgr,
				      enum PP_OD_DPM_TABLE_COMMAND type,
				      long *input, uint32_t size);
	int (*set_power_limit)(struct pp_hwmgr *hwmgr, uint32_t n);
	int (*powergate_mmhub)(struct pp_hwmgr *hwmgr);
	int (*smus_notify_pwe)(struct pp_hwmgr *hwmgr);
	int (*powergate_sdma)(struct pp_hwmgr *hwmgr, bool bgate);
	int (*enable_mgpu_fan_boost)(struct pp_hwmgr *hwmgr);
	int (*set_hard_min_dcefclk_by_freq)(struct pp_hwmgr *hwmgr, uint32_t clock);
	int (*set_hard_min_fclk_by_freq)(struct pp_hwmgr *hwmgr, uint32_t clock);
	int (*set_hard_min_gfxclk_by_freq)(struct pp_hwmgr *hwmgr, uint32_t clock);
	int (*set_soft_max_gfxclk_by_freq)(struct pp_hwmgr *hwmgr, uint32_t clock);
	int (*get_asic_baco_capability)(struct pp_hwmgr *hwmgr, bool *cap);
	int (*get_asic_baco_state)(struct pp_hwmgr *hwmgr, enum BACO_STATE *state);
	int (*set_asic_baco_state)(struct pp_hwmgr *hwmgr, enum BACO_STATE state);
	int (*get_ppfeature_status)(struct pp_hwmgr *hwmgr, char *buf);
	int (*set_ppfeature_status)(struct pp_hwmgr *hwmgr, uint64_t ppfeature_masks);
	int (*set_mp1_state)(struct pp_hwmgr *hwmgr, enum pp_mp1_state mp1_state);
	int (*asic_reset)(struct pp_hwmgr *hwmgr, enum SMU_ASIC_RESET_MODE mode);
	int (*smu_i2c_bus_access)(struct pp_hwmgr *hwmgr, bool acquire);
	int (*set_df_cstate)(struct pp_hwmgr *hwmgr, enum pp_df_cstate state);
	int (*set_xgmi_pstate)(struct pp_hwmgr *hwmgr, uint32_t pstate);
	int (*disable_power_features_for_compute_performance)(struct pp_hwmgr *hwmgr,
					bool disable);
	ssize_t (*get_gpu_metrics)(struct pp_hwmgr *hwmgr, void **table);
	int (*gfx_state_change)(struct pp_hwmgr *hwmgr, uint32_t state);
};

struct pp_table_func {
	int (*pptable_init)(struct pp_hwmgr *hw_mgr);
	int (*pptable_fini)(struct pp_hwmgr *hw_mgr);
	int (*pptable_get_number_of_vce_state_table_entries)(struct pp_hwmgr *hw_mgr);
	int (*pptable_get_vce_state_table_entry)(
						struct pp_hwmgr *hwmgr,
						unsigned long i,
						struct amd_vce_state *vce_state,
						void **clock_info,
						unsigned long *flag);
};

union phm_cac_leakage_record {
	struct {
		uint16_t Vddc;          /* in CI, we use it for StdVoltageHiSidd */
		uint32_t Leakage;       /* in CI, we use it for StdVoltageLoSidd */
	};
	struct {
		uint16_t Vddc1;
		uint16_t Vddc2;
		uint16_t Vddc3;
	};
};

struct phm_cac_leakage_table {
	uint32_t count;
	union phm_cac_leakage_record entries[];
};

struct phm_samu_clock_voltage_dependency_record {
	uint32_t samclk;
	uint32_t v;
};


struct phm_samu_clock_voltage_dependency_table {
	uint8_t count;
	struct phm_samu_clock_voltage_dependency_record entries[];
};

struct phm_cac_tdp_table {
	uint16_t usTDP;
	uint16_t usConfigurableTDP;
	uint16_t usTDC;
	uint16_t usBatteryPowerLimit;
	uint16_t usSmallPowerLimit;
	uint16_t usLowCACLeakage;
	uint16_t usHighCACLeakage;
	uint16_t usMaximumPowerDeliveryLimit;
	uint16_t usEDCLimit;
	uint16_t usOperatingTempMinLimit;
	uint16_t usOperatingTempMaxLimit;
	uint16_t usOperatingTempStep;
	uint16_t usOperatingTempHyst;
	uint16_t usDefaultTargetOperatingTemp;
	uint16_t usTargetOperatingTemp;
	uint16_t usPowerTuneDataSetID;
	uint16_t usSoftwareShutdownTemp;
	uint16_t usClockStretchAmount;
	uint16_t usTemperatureLimitHotspot;
	uint16_t usTemperatureLimitLiquid1;
	uint16_t usTemperatureLimitLiquid2;
	uint16_t usTemperatureLimitVrVddc;
	uint16_t usTemperatureLimitVrMvdd;
	uint16_t usTemperatureLimitPlx;
	uint8_t  ucLiquid1_I2C_address;
	uint8_t  ucLiquid2_I2C_address;
	uint8_t  ucLiquid_I2C_Line;
	uint8_t  ucVr_I2C_address;
	uint8_t  ucVr_I2C_Line;
	uint8_t  ucPlx_I2C_address;
	uint8_t  ucPlx_I2C_Line;
	uint32_t usBoostPowerLimit;
	uint8_t  ucCKS_LDO_REFSEL;
	uint8_t  ucHotSpotOnly;
};

struct phm_tdp_table {
	uint16_t usTDP;
	uint16_t usConfigurableTDP;
	uint16_t usTDC;
	uint16_t usBatteryPowerLimit;
	uint16_t usSmallPowerLimit;
	uint16_t usLowCACLeakage;
	uint16_t usHighCACLeakage;
	uint16_t usMaximumPowerDeliveryLimit;
	uint16_t usEDCLimit;
	uint16_t usOperatingTempMinLimit;
	uint16_t usOperatingTempMaxLimit;
	uint16_t usOperatingTempStep;
	uint16_t usOperatingTempHyst;
	uint16_t usDefaultTargetOperatingTemp;
	uint16_t usTargetOperatingTemp;
	uint16_t usPowerTuneDataSetID;
	uint16_t usSoftwareShutdownTemp;
	uint16_t usClockStretchAmount;
	uint16_t usTemperatureLimitTedge;
	uint16_t usTemperatureLimitHotspot;
	uint16_t usTemperatureLimitLiquid1;
	uint16_t usTemperatureLimitLiquid2;
	uint16_t usTemperatureLimitHBM;
	uint16_t usTemperatureLimitVrVddc;
	uint16_t usTemperatureLimitVrMvdd;
	uint16_t usTemperatureLimitPlx;
	uint8_t  ucLiquid1_I2C_address;
	uint8_t  ucLiquid2_I2C_address;
	uint8_t  ucLiquid_I2C_Line;
	uint8_t  ucVr_I2C_address;
	uint8_t  ucVr_I2C_Line;
	uint8_t  ucPlx_I2C_address;
	uint8_t  ucPlx_I2C_Line;
	uint8_t  ucLiquid_I2C_LineSDA;
	uint8_t  ucVr_I2C_LineSDA;
	uint8_t  ucPlx_I2C_LineSDA;
	uint32_t usBoostPowerLimit;
	uint16_t usBoostStartTemperature;
	uint16_t usBoostStopTemperature;
	uint32_t  ulBoostClock;
};

struct phm_ppm_table {
	uint8_t   ppm_design;
	uint16_t  cpu_core_number;
	uint32_t  platform_tdp;
	uint32_t  small_ac_platform_tdp;
	uint32_t  platform_tdc;
	uint32_t  small_ac_platform_tdc;
	uint32_t  apu_tdp;
	uint32_t  dgpu_tdp;
	uint32_t  dgpu_ulv_power;
	uint32_t  tj_max;
};

struct phm_vq_budgeting_record {
	uint32_t ulCUs;
	uint32_t ulSustainableSOCPowerLimitLow;
	uint32_t ulSustainableSOCPowerLimitHigh;
	uint32_t ulMinSclkLow;
	uint32_t ulMinSclkHigh;
	uint8_t  ucDispConfig;
	uint32_t ulDClk;
	uint32_t ulEClk;
	uint32_t ulSustainableSclk;
	uint32_t ulSustainableCUs;
};

struct phm_vq_budgeting_table {
	uint8_t numEntries;
	struct phm_vq_budgeting_record entries[0];
};

struct phm_clock_and_voltage_limits {
	uint32_t sclk;
	uint32_t mclk;
	uint32_t gfxclk;
	uint16_t vddc;
	uint16_t vddci;
	uint16_t vddgfx;
	uint16_t vddmem;
};

/* Structure to hold PPTable information */

struct phm_ppt_v1_information {
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_mclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_socclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_dcefclk;
	struct phm_clock_array *valid_sclk_values;
	struct phm_clock_array *valid_mclk_values;
	struct phm_clock_array *valid_socclk_values;
	struct phm_clock_array *valid_dcefclk_values;
	struct phm_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct phm_clock_and_voltage_limits max_clock_voltage_on_ac;
	struct phm_clock_voltage_dependency_table *vddc_dep_on_dal_pwrl;
	struct phm_ppm_table *ppm_parameter_table;
	struct phm_cac_tdp_table *cac_dtp_table;
	struct phm_tdp_table *tdp_table;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_dep_table;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddgfx_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddmem_lookup_table;
	struct phm_ppt_v1_pcie_table *pcie_table;
	struct phm_ppt_v1_gpio_table *gpio_table;
	uint16_t us_ulv_voltage_offset;
	uint16_t us_ulv_smnclk_did;
	uint16_t us_ulv_mp1clk_did;
	uint16_t us_ulv_gfxclk_bypass;
	uint16_t us_gfxclk_slew_rate;
	uint16_t us_min_gfxclk_freq_limit;
};

struct phm_ppt_v2_information {
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_mclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_socclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_dcefclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_pixclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_dispclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_phyclk;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_dep_table;

	struct phm_clock_voltage_dependency_table *vddc_dep_on_dalpwrl;

	struct phm_clock_array *valid_sclk_values;
	struct phm_clock_array *valid_mclk_values;
	struct phm_clock_array *valid_socclk_values;
	struct phm_clock_array *valid_dcefclk_values;

	struct phm_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct phm_clock_and_voltage_limits max_clock_voltage_on_ac;

	struct phm_ppm_table *ppm_parameter_table;
	struct phm_cac_tdp_table *cac_dtp_table;
	struct phm_tdp_table *tdp_table;

	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddgfx_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddmem_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddci_lookup_table;

	struct phm_ppt_v1_pcie_table *pcie_table;

	uint16_t us_ulv_voltage_offset;
	uint16_t us_ulv_smnclk_did;
	uint16_t us_ulv_mp1clk_did;
	uint16_t us_ulv_gfxclk_bypass;
	uint16_t us_gfxclk_slew_rate;
	uint16_t us_min_gfxclk_freq_limit;

	uint8_t  uc_gfx_dpm_voltage_mode;
	uint8_t  uc_soc_dpm_voltage_mode;
	uint8_t  uc_uclk_dpm_voltage_mode;
	uint8_t  uc_uvd_dpm_voltage_mode;
	uint8_t  uc_vce_dpm_voltage_mode;
	uint8_t  uc_mp0_dpm_voltage_mode;
	uint8_t  uc_dcef_dpm_voltage_mode;
};

struct phm_ppt_v3_information {
	uint8_t uc_thermal_controller_type;

	uint16_t us_small_power_limit1;
	uint16_t us_small_power_limit2;
	uint16_t us_boost_power_limit;

	uint16_t us_od_turbo_power_limit;
	uint16_t us_od_powersave_power_limit;
	uint16_t us_software_shutdown_temp;

	uint32_t *power_saving_clock_max;
	uint32_t *power_saving_clock_min;

	uint8_t *od_feature_capabilities;
	uint32_t *od_settings_max;
	uint32_t *od_settings_min;

	void *smc_pptable;
};

struct phm_dynamic_state_info {
	struct phm_clock_voltage_dependency_table *vddc_dependency_on_sclk;
	struct phm_clock_voltage_dependency_table *vddci_dependency_on_mclk;
	struct phm_clock_voltage_dependency_table *vddc_dependency_on_mclk;
	struct phm_clock_voltage_dependency_table *mvdd_dependency_on_mclk;
	struct phm_clock_voltage_dependency_table *vddc_dep_on_dal_pwrl;
	struct phm_clock_array                    *valid_sclk_values;
	struct phm_clock_array                    *valid_mclk_values;
	struct phm_clock_and_voltage_limits       max_clock_voltage_on_dc;
	struct phm_clock_and_voltage_limits       max_clock_voltage_on_ac;
	uint32_t                                  mclk_sclk_ratio;
	uint32_t                                  sclk_mclk_delta;
	uint32_t                                  vddc_vddci_delta;
	uint32_t                                  min_vddc_for_pcie_gen2;
	struct phm_cac_leakage_table              *cac_leakage_table;
	struct phm_phase_shedding_limits_table  *vddc_phase_shed_limits_table;

	struct phm_vce_clock_voltage_dependency_table
					    *vce_clock_voltage_dependency_table;
	struct phm_uvd_clock_voltage_dependency_table
					    *uvd_clock_voltage_dependency_table;
	struct phm_acp_clock_voltage_dependency_table
					    *acp_clock_voltage_dependency_table;
	struct phm_samu_clock_voltage_dependency_table
					   *samu_clock_voltage_dependency_table;

	struct phm_ppm_table                          *ppm_parameter_table;
	struct phm_cac_tdp_table                      *cac_dtp_table;
	struct phm_clock_voltage_dependency_table	*vdd_gfx_dependency_on_sclk;
};

struct pp_fan_info {
	bool bNoFan;
	uint8_t   ucTachometerPulsesPerRevolution;
	uint32_t   ulMinRPM;
	uint32_t   ulMaxRPM;
};

struct pp_advance_fan_control_parameters {
	uint16_t  usTMin;                          /* The temperature, in 0.01 centigrades, below which we just run at a minimal PWM. */
	uint16_t  usTMed;                          /* The middle temperature where we change slopes. */
	uint16_t  usTHigh;                         /* The high temperature for setting the second slope. */
	uint16_t  usPWMMin;                        /* The minimum PWM value in percent (0.01% increments). */
	uint16_t  usPWMMed;                        /* The PWM value (in percent) at TMed. */
	uint16_t  usPWMHigh;                       /* The PWM value at THigh. */
	uint8_t   ucTHyst;                         /* Temperature hysteresis. Integer. */
	uint32_t   ulCycleDelay;                   /* The time between two invocations of the fan control routine in microseconds. */
	uint16_t  usTMax;                          /* The max temperature */
	uint8_t   ucFanControlMode;
	uint16_t  usFanPWMMinLimit;
	uint16_t  usFanPWMMaxLimit;
	uint16_t  usFanPWMStep;
	uint16_t  usDefaultMaxFanPWM;
	uint16_t  usFanOutputSensitivity;
	uint16_t  usDefaultFanOutputSensitivity;
	uint16_t  usMaxFanPWM;                     /* The max Fan PWM value for Fuzzy Fan Control feature */
	uint16_t  usFanRPMMinLimit;                /* Minimum limit range in percentage, need to calculate based on minRPM/MaxRpm */
	uint16_t  usFanRPMMaxLimit;                /* Maximum limit range in percentage, usually set to 100% by default */
	uint16_t  usFanRPMStep;                    /* Step increments/decerements, in percent */
	uint16_t  usDefaultMaxFanRPM;              /* The max Fan RPM value for Fuzzy Fan Control feature, default from PPTable */
	uint16_t  usMaxFanRPM;                     /* The max Fan RPM value for Fuzzy Fan Control feature, user defined */
	uint16_t  usFanCurrentLow;                 /* Low current */
	uint16_t  usFanCurrentHigh;                /* High current */
	uint16_t  usFanRPMLow;                     /* Low RPM */
	uint16_t  usFanRPMHigh;                    /* High RPM */
	uint32_t   ulMinFanSCLKAcousticLimit;      /* Minimum Fan Controller SCLK Frequency Acoustic Limit. */
	uint8_t   ucTargetTemperature;             /* Advanced fan controller target temperature. */
	uint8_t   ucMinimumPWMLimit;               /* The minimum PWM that the advanced fan controller can set.  This should be set to the highest PWM that will run the fan at its lowest RPM. */
	uint16_t  usFanGainEdge;                   /* The following is added for Fiji */
	uint16_t  usFanGainHotspot;
	uint16_t  usFanGainLiquid;
	uint16_t  usFanGainVrVddc;
	uint16_t  usFanGainVrMvdd;
	uint16_t  usFanGainPlx;
	uint16_t  usFanGainHbm;
	uint8_t   ucEnableZeroRPM;
	uint8_t   ucFanStopTemperature;
	uint8_t   ucFanStartTemperature;
	uint32_t  ulMaxFanSCLKAcousticLimit;       /* Maximum Fan Controller SCLK Frequency Acoustic Limit. */
	uint32_t  ulTargetGfxClk;
	uint16_t  usZeroRPMStartTemperature;
	uint16_t  usZeroRPMStopTemperature;
	uint16_t  usMGpuThrottlingRPMLimit;
};

struct pp_thermal_controller_info {
	uint8_t ucType;
	uint8_t ucI2cLine;
	uint8_t ucI2cAddress;
	uint8_t use_hw_fan_control;
	struct pp_fan_info fanInfo;
	struct pp_advance_fan_control_parameters advanceFanControlParameters;
};

struct phm_microcode_version_info {
	uint32_t SMC;
	uint32_t DMCU;
	uint32_t MC;
	uint32_t NB;
};

enum PP_TABLE_VERSION {
	PP_TABLE_V0 = 0,
	PP_TABLE_V1,
	PP_TABLE_V2,
	PP_TABLE_MAX
};

/**
 * The main hardware manager structure.
 */
#define Workload_Policy_Max 6

struct pp_hwmgr {
	void *adev;
	uint32_t chip_family;
	uint32_t chip_id;
	uint32_t smu_version;
	bool not_vf;
	bool pm_en;
	bool pp_one_vf;
	struct mutex msg_lock;

	uint32_t pp_table_version;
	void *device;
	struct pp_smumgr *smumgr;
	const void *soft_pp_table;
	uint32_t soft_pp_table_size;
	void *hardcode_pp_table;
	bool need_pp_table_upload;

	struct amd_vce_state vce_states[AMD_MAX_VCE_LEVELS];
	uint32_t num_vce_state_tables;

	enum amd_dpm_forced_level dpm_level;
	enum amd_dpm_forced_level saved_dpm_level;
	enum amd_dpm_forced_level request_dpm_level;
	uint32_t usec_timeout;
	void *pptable;
	struct phm_platform_descriptor platform_descriptor;
	void *backend;

	void *smu_backend;
	const struct pp_smumgr_func *smumgr_funcs;
	bool is_kicker;

	enum PP_DAL_POWERLEVEL dal_power_level;
	struct phm_dynamic_state_info dyn_state;
	const struct pp_hwmgr_func *hwmgr_func;
	const struct pp_table_func *pptable_func;

	struct pp_power_state    *ps;
	uint32_t num_ps;
	struct pp_thermal_controller_info thermal_controller;
	bool fan_ctrl_is_in_default_mode;
	uint32_t fan_ctrl_default_mode;
	bool fan_ctrl_enabled;
	uint32_t tmin;
	struct phm_microcode_version_info microcode_version_info;
	uint32_t ps_size;
	struct pp_power_state    *current_ps;
	struct pp_power_state    *request_ps;
	struct pp_power_state    *boot_ps;
	struct pp_power_state    *uvd_ps;
	const struct amd_pp_display_configuration *display_config;
	uint32_t feature_mask;
	bool avfs_supported;
	/* UMD Pstate */
	bool en_umd_pstate;
	uint32_t power_profile_mode;
	uint32_t default_power_profile_mode;
	uint32_t pstate_sclk;
	uint32_t pstate_mclk;
	bool od_enabled;
	uint32_t power_limit;
	uint32_t default_power_limit;
	uint32_t workload_mask;
	uint32_t workload_prority[Workload_Policy_Max];
	uint32_t workload_setting[Workload_Policy_Max];
	bool gfxoff_state_changed_by_workload;
	uint32_t pstate_sclk_peak;
	uint32_t pstate_mclk_peak;

	struct delayed_work swctf_delayed_work;
};

int hwmgr_early_init(struct pp_hwmgr *hwmgr);
int hwmgr_sw_init(struct pp_hwmgr *hwmgr);
int hwmgr_sw_fini(struct pp_hwmgr *hwmgr);
int hwmgr_hw_init(struct pp_hwmgr *hwmgr);
int hwmgr_hw_fini(struct pp_hwmgr *hwmgr);
int hwmgr_suspend(struct pp_hwmgr *hwmgr);
int hwmgr_resume(struct pp_hwmgr *hwmgr);

int hwmgr_handle_task(struct pp_hwmgr *hwmgr,
				enum amd_pp_task task_id,
				enum amd_pm_state_type *user_state);


#define PHM_ENTIRE_REGISTER_MASK 0xFFFFFFFFU

int smu7_init_function_pointers(struct pp_hwmgr *hwmgr);
int smu8_init_function_pointers(struct pp_hwmgr *hwmgr);
int vega12_hwmgr_init(struct pp_hwmgr *hwmgr);
int vega20_hwmgr_init(struct pp_hwmgr *hwmgr);

#endif /* _HWMGR_H_ */
