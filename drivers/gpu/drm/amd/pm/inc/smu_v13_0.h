/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __SMU_V13_0_H__
#define __SMU_V13_0_H__

#include "amdgpu_smu.h"

#define SMU13_DRIVER_IF_VERSION_INV 0xFFFFFFFF
#define SMU13_DRIVER_IF_VERSION_YELLOW_CARP 0x03
#define SMU13_DRIVER_IF_VERSION_ALDE 0x07

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
#define smnMP0_FW_INTF			0x30101c0
#define smnMP1_PUB_CTRL			0x3010b14

#define TEMP_RANGE_MIN			(0)
#define TEMP_RANGE_MAX			(80 * 1000)

#define SMU13_TOOL_SIZE			0x19000

#define MAX_DPM_LEVELS 16
#define MAX_PCIE_CONF 2

#define CTF_OFFSET_EDGE			5
#define CTF_OFFSET_HOTSPOT		5
#define CTF_OFFSET_MEM			5

struct smu_13_0_max_sustainable_clocks {
	uint32_t display_clock;
	uint32_t phy_clock;
	uint32_t pixel_clock;
	uint32_t uclock;
	uint32_t dcef_clock;
	uint32_t soc_clock;
};

struct smu_13_0_dpm_clk_level {
	bool				enabled;
	uint32_t			value;
};

struct smu_13_0_dpm_table {
	uint32_t			min;        /* MHz */
	uint32_t			max;        /* MHz */
	uint32_t			count;
	struct smu_13_0_dpm_clk_level	dpm_levels[MAX_DPM_LEVELS];
};

struct smu_13_0_pcie_table {
	uint8_t  pcie_gen[MAX_PCIE_CONF];
	uint8_t  pcie_lane[MAX_PCIE_CONF];
};

struct smu_13_0_dpm_tables {
	struct smu_13_0_dpm_table        soc_table;
	struct smu_13_0_dpm_table        gfx_table;
	struct smu_13_0_dpm_table        uclk_table;
	struct smu_13_0_dpm_table        eclk_table;
	struct smu_13_0_dpm_table        vclk_table;
	struct smu_13_0_dpm_table        dclk_table;
	struct smu_13_0_dpm_table        dcef_table;
	struct smu_13_0_dpm_table        pixel_table;
	struct smu_13_0_dpm_table        display_table;
	struct smu_13_0_dpm_table        phy_table;
	struct smu_13_0_dpm_table        fclk_table;
	struct smu_13_0_pcie_table       pcie_table;
};

struct smu_13_0_dpm_context {
	struct smu_13_0_dpm_tables  dpm_tables;
	uint32_t                    workload_policy_mask;
	uint32_t                    dcef_min_ds_clk;
};

enum smu_13_0_power_state {
	SMU_13_0_POWER_STATE__D0 = 0,
	SMU_13_0_POWER_STATE__D1,
	SMU_13_0_POWER_STATE__D3, /* Sleep*/
	SMU_13_0_POWER_STATE__D4, /* Hibernate*/
	SMU_13_0_POWER_STATE__D5, /* Power off*/
};

struct smu_13_0_power_context {
	uint32_t	power_source;
	uint8_t		in_power_limit_boost_mode;
	enum smu_13_0_power_state power_state;
};

enum smu_v13_0_baco_seq {
	BACO_SEQ_BACO = 0,
	BACO_SEQ_MSR,
	BACO_SEQ_BAMACO,
	BACO_SEQ_ULPS,
	BACO_SEQ_COUNT,
};

#if defined(SWSMU_CODE_LAYER_L2) || defined(SWSMU_CODE_LAYER_L3)

int smu_v13_0_init_microcode(struct smu_context *smu);

void smu_v13_0_fini_microcode(struct smu_context *smu);

int smu_v13_0_load_microcode(struct smu_context *smu);

int smu_v13_0_init_smc_tables(struct smu_context *smu);

int smu_v13_0_fini_smc_tables(struct smu_context *smu);

int smu_v13_0_init_power(struct smu_context *smu);

int smu_v13_0_fini_power(struct smu_context *smu);

int smu_v13_0_check_fw_status(struct smu_context *smu);

int smu_v13_0_setup_pptable(struct smu_context *smu);

int smu_v13_0_get_vbios_bootup_values(struct smu_context *smu);

int smu_v13_0_check_fw_version(struct smu_context *smu);

int smu_v13_0_set_driver_table_location(struct smu_context *smu);

int smu_v13_0_set_tool_table_location(struct smu_context *smu);

int smu_v13_0_notify_memory_pool_location(struct smu_context *smu);

int smu_v13_0_system_features_control(struct smu_context *smu,
				      bool en);

int smu_v13_0_init_display_count(struct smu_context *smu, uint32_t count);

int smu_v13_0_set_allowed_mask(struct smu_context *smu);

int smu_v13_0_notify_display_change(struct smu_context *smu);

int smu_v13_0_get_current_power_limit(struct smu_context *smu,
				      uint32_t *power_limit);

int smu_v13_0_set_power_limit(struct smu_context *smu, uint32_t n);

int smu_v13_0_init_max_sustainable_clocks(struct smu_context *smu);

int smu_v13_0_enable_thermal_alert(struct smu_context *smu);

int smu_v13_0_disable_thermal_alert(struct smu_context *smu);

int smu_v13_0_get_gfx_vdd(struct smu_context *smu, uint32_t *value);

int smu_v13_0_set_min_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk);

int
smu_v13_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req);

uint32_t
smu_v13_0_get_fan_control_mode(struct smu_context *smu);

int
smu_v13_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode);

int
smu_v13_0_set_fan_speed_percent(struct smu_context *smu, uint32_t speed);

int smu_v13_0_set_fan_speed_rpm(struct smu_context *smu,
				uint32_t speed);

int smu_v13_0_set_xgmi_pstate(struct smu_context *smu,
			      uint32_t pstate);

int smu_v13_0_gfx_off_control(struct smu_context *smu, bool enable);

int smu_v13_0_register_irq_handler(struct smu_context *smu);

int smu_v13_0_set_azalia_d3_pme(struct smu_context *smu);

int smu_v13_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					       struct pp_smu_nv_clock_table *max_clocks);

bool smu_v13_0_baco_is_support(struct smu_context *smu);

enum smu_baco_state smu_v13_0_baco_get_state(struct smu_context *smu);

int smu_v13_0_baco_set_state(struct smu_context *smu, enum smu_baco_state state);

int smu_v13_0_baco_enter(struct smu_context *smu);
int smu_v13_0_baco_exit(struct smu_context *smu);

int smu_v13_0_mode1_reset(struct smu_context *smu);
int smu_v13_0_mode2_reset(struct smu_context *smu);

int smu_v13_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
				    uint32_t *min, uint32_t *max);

int smu_v13_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
					  uint32_t min, uint32_t max);

int smu_v13_0_set_hard_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max);

int smu_v13_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level);

int smu_v13_0_set_power_source(struct smu_context *smu,
			       enum smu_power_src_type power_src);

int smu_v13_0_get_dpm_freq_by_index(struct smu_context *smu,
				    enum smu_clk_type clk_type,
				    uint16_t level,
				    uint32_t *value);

int smu_v13_0_get_dpm_level_count(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *value);

int smu_v13_0_set_single_dpm_table(struct smu_context *smu,
				   enum smu_clk_type clk_type,
				   struct smu_13_0_dpm_table *single_dpm_table);

int smu_v13_0_get_dpm_level_range(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *min_value,
				  uint32_t *max_value);

int smu_v13_0_get_current_pcie_link_width_level(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_width(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_speed_level(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_speed(struct smu_context *smu);

int smu_v13_0_gfx_ulv_control(struct smu_context *smu,
			      bool enablement);

int smu_v13_0_wait_for_event(struct smu_context *smu, enum smu_event_type event,
			     uint64_t event_arg);

#endif
#endif
