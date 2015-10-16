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
#include "pp_instance.h"
#include "hardwaremanager.h"
#include "pp_power_source.h"
#include "hwmgr_ppt.h"

struct pp_instance;
struct pp_hwmgr;
struct pp_hw_power_state;
struct pp_power_state;
struct PP_VCEState;

enum PP_Result {
	PP_Result_TableImmediateExit = 0x13,
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
	PHM_Cz_Magic          = 0x67DCBA25
};

enum PP_DAL_POWERLEVEL {
	PP_DAL_POWERLEVEL_INVALID = 0,
	PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_PERFORMANCE,

	PP_DAL_POWERLEVEL_0 = PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_1 = PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_2 = PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_3 = PP_DAL_POWERLEVEL_PERFORMANCE,
	PP_DAL_POWERLEVEL_4 = PP_DAL_POWERLEVEL_3+1,
	PP_DAL_POWERLEVEL_5 = PP_DAL_POWERLEVEL_4+1,
	PP_DAL_POWERLEVEL_6 = PP_DAL_POWERLEVEL_5+1,
	PP_DAL_POWERLEVEL_7 = PP_DAL_POWERLEVEL_6+1,
};

#define PHM_PCIE_POWERGATING_TARGET_GFX            0
#define PHM_PCIE_POWERGATING_TARGET_DDI            1
#define PHM_PCIE_POWERGATING_TARGET_PLLCASCADE     2
#define PHM_PCIE_POWERGATING_TARGET_PHY            3

typedef int (*phm_table_function)(struct pp_hwmgr *hwmgr, void *input,
				  void *output, void *storage, int result);

typedef bool (*phm_check_function)(struct pp_hwmgr *hwmgr);

struct phm_set_power_state_input {
	const struct pp_hw_power_state *pcurrent_state;
	const struct pp_hw_power_state *pnew_state;
};

struct phm_acp_arbiter {
	uint32_t acpclk;
};

struct phm_uvd_arbiter {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t vclk_ceiling;
	uint32_t dclk_ceiling;
};

struct phm_vce_arbiter {
	uint32_t   evclk;
	uint32_t   ecclk;
};

struct phm_gfx_arbiter {
	uint32_t sclk;
	uint32_t mclk;
	uint32_t sclk_over_drive;
	uint32_t mclk_over_drive;
	uint32_t sclk_threshold;
	uint32_t num_cus;
};

/* Entries in the master tables */
struct phm_master_table_item {
	phm_check_function isFunctionNeededInRuntimeTable;
	phm_table_function tableFunction;
};

enum phm_master_table_flag {
	PHM_MasterTableFlag_None         = 0,
	PHM_MasterTableFlag_ExitOnError  = 1,
};

/* The header of the master tables */
struct phm_master_table_header {
	uint32_t storage_size;
	uint32_t flags;
	struct phm_master_table_item *master_list;
};

struct phm_runtime_table_header {
	uint32_t storage_size;
	bool exit_error;
	phm_table_function *function_list;
};

struct phm_clock_array {
	uint32_t count;
	uint32_t values[1];
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
	uint32_t count;										/* Number of entries. */
	struct phm_clock_voltage_dependency_record entries[1];		/* Dynamically allocate count entries. */
};

struct phm_phase_shedding_limits_record {
	uint32_t  Voltage;
	uint32_t    Sclk;
	uint32_t    Mclk;
};


extern int phm_dispatch_table(struct pp_hwmgr *hwmgr,
			      struct phm_runtime_table_header *rt_table,
			      void *input, void *output);

extern int phm_construct_table(struct pp_hwmgr *hwmgr,
			       struct phm_master_table_header *master_table,
			       struct phm_runtime_table_header *rt_table);

extern int phm_destroy_table(struct pp_hwmgr *hwmgr,
			     struct phm_runtime_table_header *rt_table);


struct phm_uvd_clock_voltage_dependency_record {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t v;
};

struct phm_uvd_clock_voltage_dependency_table {
	uint8_t count;
	struct phm_uvd_clock_voltage_dependency_record entries[1];
};

struct phm_acp_clock_voltage_dependency_record {
	uint32_t acpclk;
	uint32_t v;
};

struct phm_acp_clock_voltage_dependency_table {
	uint32_t count;
	struct phm_acp_clock_voltage_dependency_record entries[1];
};

struct phm_vce_clock_voltage_dependency_record {
	uint32_t ecclk;
	uint32_t evclk;
	uint32_t v;
};

struct phm_phase_shedding_limits_table {
	uint32_t                           count;
	struct phm_phase_shedding_limits_record  entries[1];
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
	struct phm_vce_clock_voltage_dependency_record entries[1];
};

struct pp_hwmgr_func {
	int (*backend_init)(struct pp_hwmgr *hw_mgr);
	int (*backend_fini)(struct pp_hwmgr *hw_mgr);
	int (*asic_setup)(struct pp_hwmgr *hw_mgr);
	int (*get_power_state_size)(struct pp_hwmgr *hw_mgr);

	int (*apply_state_adjust_rules)(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps);

	int (*force_dpm_level)(struct pp_hwmgr *hw_mgr,
					enum amd_dpm_forced_level level);

	int (*dynamic_state_management_enable)(
						struct pp_hwmgr *hw_mgr);

	int (*patch_boot_state)(struct pp_hwmgr *hwmgr,
				     struct pp_hw_power_state *hw_ps);

	int (*get_pp_table_entry)(struct pp_hwmgr *hwmgr,
			    unsigned long, struct pp_power_state *);
	int (*get_num_of_pp_table_entries)(struct pp_hwmgr *hwmgr);
	int (*powerdown_uvd)(struct pp_hwmgr *hwmgr);
	int (*powergate_vce)(struct pp_hwmgr *hwmgr, bool bgate);
	int (*powergate_uvd)(struct pp_hwmgr *hwmgr, bool bgate);
	int (*get_mclk)(struct pp_hwmgr *hwmgr, bool low);
	int (*get_sclk)(struct pp_hwmgr *hwmgr, bool low);
	int (*power_state_set)(struct pp_hwmgr *hwmgr,
						const void *state);
	void (*print_current_perforce_level)(struct pp_hwmgr *hwmgr,
							struct seq_file *m);
	int (*enable_clock_power_gating)(struct pp_hwmgr *hwmgr);
	int (*notify_smc_display_config_after_ps_adjustment)(struct pp_hwmgr *hwmgr);
	int (*display_config_changed)(struct pp_hwmgr *hwmgr);
};

struct pp_table_func {
	int (*pptable_init)(struct pp_hwmgr *hw_mgr);
	int (*pptable_fini)(struct pp_hwmgr *hw_mgr);
	int (*pptable_get_number_of_vce_state_table_entries)(struct pp_hwmgr *hw_mgr);
	int (*pptable_get_vce_state_table_entry)(
						struct pp_hwmgr *hwmgr,
						unsigned long i,
						struct PP_VCEState *vce_state,
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
	union phm_cac_leakage_record entries[1];
};

struct phm_samu_clock_voltage_dependency_record {
	uint32_t samclk;
	uint32_t v;
};


struct phm_samu_clock_voltage_dependency_table {
	uint8_t count;
	struct phm_samu_clock_voltage_dependency_record entries[1];
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
	struct phm_vq_budgeting_record entries[1];
};

struct phm_clock_and_voltage_limits {
	uint32_t sclk;
	uint32_t mclk;
	uint16_t vddc;
	uint16_t vddci;
	uint16_t vddgfx;
};

/* Structure to hold PPTable information */

struct phm_ppt_v1_information {
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_on_mclk;
	struct phm_clock_array *valid_sclk_values;
	struct phm_clock_array *valid_mclk_values;
	struct phm_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct phm_clock_and_voltage_limits max_clock_voltage_on_ac;
	struct phm_clock_voltage_dependency_table *vddc_dep_on_dal_pwrl;
	struct phm_ppm_table *ppm_parameter_table;
	struct phm_cac_tdp_table *cac_dtp_table;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_dep_table;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddgfx_lookup_table;
	struct phm_ppt_v1_pcie_table *pcie_table;
	uint16_t us_ulv_voltage_offset;
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
	struct phm_phase_shedding_limits_table	  *vddc_phase_shed_limits_table;

	struct phm_vce_clock_voltage_dependency_table
					    *vce_clocl_voltage_dependency_table;
	struct phm_uvd_clock_voltage_dependency_table
					    *uvd_clocl_voltage_dependency_table;
	struct phm_acp_clock_voltage_dependency_table
					    *acp_clock_voltage_dependency_table;
	struct phm_samu_clock_voltage_dependency_table
					   *samu_clock_voltage_dependency_table;

	struct phm_ppm_table                          *ppm_parameter_table;
	struct phm_cac_tdp_table                      *cac_dtp_table;
	struct phm_clock_voltage_dependency_table	  *vdd_gfx_dependency_on_sclk;
	struct phm_vq_budgeting_table		  		  *vq_budgeting_table;
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
};

struct pp_thermal_controller_info {
	uint8_t ucType;
	uint8_t ucI2cLine;
	uint8_t ucI2cAddress;
	struct pp_fan_info fanInfo;
	struct pp_advance_fan_control_parameters advanceFanControlParameters;
};

struct phm_microcode_version_info {
	uint32_t SMC;
	uint32_t DMCU;
	uint32_t MC;
	uint32_t NB;
};

/**
 * The main hardware manager structure.
 */
struct pp_hwmgr {
	uint32_t chip_family;
	uint32_t chip_id;
	uint32_t hw_revision;
	uint32_t sub_sys_id;
	uint32_t sub_vendor_id;

	void *device;
	struct pp_smumgr *smumgr;
	const void *soft_pp_table;
	enum amd_dpm_forced_level dpm_level;
	bool block_hw_access;
	struct phm_gfx_arbiter gfx_arbiter;
	struct phm_acp_arbiter acp_arbiter;
	struct phm_uvd_arbiter uvd_arbiter;
	struct phm_vce_arbiter vce_arbiter;
	uint32_t usec_timeout;
	void *pptable;
	struct phm_platform_descriptor platform_descriptor;
	void *backend;
	enum PP_DAL_POWERLEVEL	dal_power_level;
	struct phm_dynamic_state_info dyn_state;
	struct phm_runtime_table_header setup_asic;
	struct phm_runtime_table_header disable_dynamic_state_management;
	struct phm_runtime_table_header enable_dynamic_state_management;
	struct phm_runtime_table_header set_power_state;
	struct phm_runtime_table_header enable_clock_power_gatings;
	struct phm_runtime_table_header display_configuration_changed;
	const struct pp_hwmgr_func *hwmgr_func;
	const struct pp_table_func *pptable_func;
	struct pp_power_state    *ps;
        enum pp_power_source  power_source;
	uint32_t num_ps;
	struct pp_thermal_controller_info thermal_controller;
	struct phm_microcode_version_info microcode_version_info;
	uint32_t ps_size;
	struct pp_power_state    *current_ps;
	struct pp_power_state    *request_ps;
	struct pp_power_state    *boot_ps;
	struct pp_power_state    *uvd_ps;
};


extern int hwmgr_init(struct amd_pp_init *pp_init,
		      struct pp_instance *handle);

extern int hwmgr_fini(struct pp_hwmgr *hwmgr);

extern int hw_init_power_state_table(struct pp_hwmgr *hwmgr);

extern int phm_wait_on_register(struct pp_hwmgr *hwmgr, uint32_t index,
				uint32_t value, uint32_t mask);

extern int phm_wait_for_register_unequal(struct pp_hwmgr *hwmgr,
				uint32_t index, uint32_t value, uint32_t mask);

extern uint32_t phm_read_indirect_register(struct pp_hwmgr *hwmgr,
		uint32_t indirect_port, uint32_t index);

extern void phm_write_indirect_register(struct pp_hwmgr *hwmgr,
		uint32_t indirect_port,
		uint32_t index,
		uint32_t value);

extern void phm_wait_on_indirect_register(struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask);

extern void phm_wait_for_indirect_register_unequal(
				struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask);

bool phm_cf_want_uvd_power_gating(struct pp_hwmgr *hwmgr);
bool phm_cf_want_vce_power_gating(struct pp_hwmgr *hwmgr);

#define PHM_ENTIRE_REGISTER_MASK 0xFFFFFFFFU

#define PHM_FIELD_SHIFT(reg, field) reg##__##field##__SHIFT
#define PHM_FIELD_MASK(reg, field) reg##__##field##_MASK

#define PHM_SET_FIELD(origval, reg, field, fieldval)	\
	(((origval) & ~PHM_FIELD_MASK(reg, field)) |	\
	 (PHM_FIELD_MASK(reg, field) & ((fieldval) << PHM_FIELD_SHIFT(reg, field))))

#define PHM_GET_FIELD(value, reg, field)	\
	(((value) & PHM_FIELD_MASK(reg, field)) >>	\
	 PHM_FIELD_SHIFT(reg, field))


#define PHM_WAIT_REGISTER_GIVEN_INDEX(hwmgr, index, value, mask)	\
	phm_wait_on_register(hwmgr, index, value, mask)

#define PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, index, value, mask)	\
	phm_wait_for_register_unequal(hwmgr, index, value, mask)

#define PHM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, index, value, mask)	\
	phm_wait_on_indirect_register(hwmgr, mm##port##_INDEX, index, value, mask)

#define PHM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, index, value, mask)	\
	phm_wait_for_indirect_register_unequal(hwmgr, mm##port##_INDEX, index, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, index, value, mask)	\
	phm_wait_on_indirect_register(hwmgr, mm##port##_INDEX_0, index, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, index, value, mask)	\
	phm_wait_for_indirect_register_unequal(hwmgr, mm##port##_INDEX_0, index, value, mask)

/* Operations on named registers. */

#define PHM_WAIT_REGISTER(hwmgr, reg, value, mask)	\
	PHM_WAIT_REGISTER_GIVEN_INDEX(hwmgr, mm##reg, value, mask)

#define PHM_WAIT_REGISTER_UNEQUAL(hwmgr, reg, value, mask)	\
	PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, mm##reg, value, mask)

#define PHM_WAIT_INDIRECT_REGISTER(hwmgr, port, reg, value, mask)	\
	PHM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, value, mask)	\
	PHM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER(hwmgr, port, reg, value, mask)	\
	PHM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

#define PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, value, mask)	\
	PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, port, ix##reg, value, mask)

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

#define PHM_WAIT_FIELD(hwmgr, reg, field, fieldval)	\
	PHM_WAIT_REGISTER(hwmgr, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_INDIRECT_FIELD(hwmgr, port, reg, field, fieldval)	\
	PHM_WAIT_INDIRECT_REGISTER(hwmgr, port, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, port, reg, field, fieldval)	\
	PHM_WAIT_VFPF_INDIRECT_REGISTER(hwmgr, port, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_FIELD_UNEQUAL(hwmgr, reg, field, fieldval)	\
	PHM_WAIT_REGISTER_UNEQUAL(hwmgr, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_INDIRECT_FIELD_UNEQUAL(hwmgr, port, reg, field, fieldval)	\
	PHM_WAIT_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, port, reg, field, fieldval)	\
	PHM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(hwmgr, port, reg, (fieldval)	\
			<< PHM_FIELD_SHIFT(reg, field), PHM_FIELD_MASK(reg, field))

/* Operations on arrays of registers & fields. */

#define PHM_READ_ARRAY_REGISTER(device, reg, offset)	\
	cgs_read_register(device, mm##reg + (offset))

#define PHM_WRITE_ARRAY_REGISTER(device, reg, offset, value)	\
	cgs_write_register(device, mm##reg + (offset), value)

#define PHM_WAIT_ARRAY_REGISTER(hwmgr, reg, offset, value, mask)	\
	PHM_WAIT_REGISTER_GIVEN_INDEX(hwmgr, mm##reg + (offset), value, mask)

#define PHM_WAIT_ARRAY_REGISTER_UNEQUAL(hwmgr, reg, offset, value, mask)	\
	PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, mm##reg + (offset), value, mask)

#define PHM_READ_ARRAY_FIELD(hwmgr, reg, offset, field) \
	PHM_GET_FIELD(PHM_READ_ARRAY_REGISTER(hwmgr->device, reg, offset), reg, field)

#define PHM_WRITE_ARRAY_FIELD(hwmgr, reg, offset, field, fieldvalue)	\
	PHM_WRITE_ARRAY_REGISTER(hwmgr->device, reg, offset,	\
			PHM_SET_FIELD(PHM_READ_ARRAY_REGISTER(hwmgr->device, reg, offset),	\
				reg, field, fieldvalue))

#define PHM_WAIT_ARRAY_FIELD(hwmgr, reg, offset, field, fieldvalue)	\
	PHM_WAIT_REGISTER_GIVEN_INDEX(hwmgr, mm##reg + (offset),	\
			(fieldvalue) << PHM_FIELD_SHIFT(reg, field),	\
			PHM_FIELD_MASK(reg, field))

#define PHM_WAIT_ARRAY_FIELD_UNEQUAL(hwmgr, reg, offset, field, fieldvalue)	\
	PHM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(hwmgr, mm##reg + (offset),	\
			(fieldvalue) << PHM_FIELD_SHIFT(reg, field),	\
			PHM_FIELD_MASK(reg, field))

#endif /* _HWMGR_H_ */
