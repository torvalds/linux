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
 *
 */
#ifndef __SMU_V11_0_H__
#define __SMU_V11_0_H__

#include "amdgpu_smu.h"

#define SMU11_DRIVER_IF_VERSION_INV 0xFFFFFFFF
#define SMU11_DRIVER_IF_VERSION_VG20 0x13
#define SMU11_DRIVER_IF_VERSION_ARCT 0x12
#define SMU11_DRIVER_IF_VERSION_NV10 0x35
#define SMU11_DRIVER_IF_VERSION_NV14 0x36

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004
#define MP1_SMC_SIZE		0x40000

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
#define smnMP0_FW_INTF			0x30101c0
#define smnMP1_PUB_CTRL			0x3010b14

#define TEMP_RANGE_MIN			(0)
#define TEMP_RANGE_MAX			(80 * 1000)

#define SMU11_TOOL_SIZE			0x19000

#define MAX_PCIE_CONF 2

#define CLK_MAP(clk, index) \
	[SMU_##clk] = {1, (index)}

#define FEA_MAP(fea) \
	[SMU_FEATURE_##fea##_BIT] = {1, FEATURE_##fea##_BIT}

#define TAB_MAP(tab) \
	[SMU_TABLE_##tab] = {1, TABLE_##tab}

#define PWR_MAP(tab) \
	[SMU_POWER_SOURCE_##tab] = {1, POWER_SOURCE_##tab}

#define WORKLOAD_MAP(profile, workload) \
	[profile] = {1, (workload)}

static const struct smu_temperature_range smu11_thermal_policy[] =
{
	{-273150,  99000, 99000, -273150, 99000, 99000, -273150, 99000, 99000},
	{ 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000},
};

struct smu_11_0_cmn2aisc_mapping {
	int	valid_mapping;
	int	map_to;
};

struct smu_11_0_max_sustainable_clocks {
	uint32_t display_clock;
	uint32_t phy_clock;
	uint32_t pixel_clock;
	uint32_t uclock;
	uint32_t dcef_clock;
	uint32_t soc_clock;
};

struct smu_11_0_dpm_table {
	uint32_t    min;        /* MHz */
	uint32_t    max;        /* MHz */
};

struct smu_11_0_pcie_table {
        uint8_t  pcie_gen[MAX_PCIE_CONF];
        uint8_t  pcie_lane[MAX_PCIE_CONF];
};

struct smu_11_0_dpm_tables {
	struct smu_11_0_dpm_table        soc_table;
	struct smu_11_0_dpm_table        gfx_table;
	struct smu_11_0_dpm_table        uclk_table;
	struct smu_11_0_dpm_table        eclk_table;
	struct smu_11_0_dpm_table        vclk_table;
	struct smu_11_0_dpm_table        dclk_table;
	struct smu_11_0_dpm_table        dcef_table;
	struct smu_11_0_dpm_table        pixel_table;
	struct smu_11_0_dpm_table        display_table;
	struct smu_11_0_dpm_table        phy_table;
	struct smu_11_0_dpm_table        fclk_table;
	struct smu_11_0_pcie_table       pcie_table;
};

struct smu_11_0_dpm_context {
	struct smu_11_0_dpm_tables  dpm_tables;
	uint32_t                    workload_policy_mask;
	uint32_t                    dcef_min_ds_clk;
};

enum smu_11_0_power_state {
	SMU_11_0_POWER_STATE__D0 = 0,
	SMU_11_0_POWER_STATE__D1,
	SMU_11_0_POWER_STATE__D3, /* Sleep*/
	SMU_11_0_POWER_STATE__D4, /* Hibernate*/
	SMU_11_0_POWER_STATE__D5, /* Power off*/
};

struct smu_11_0_power_context {
	uint32_t	power_source;
	uint8_t		in_power_limit_boost_mode;
	enum smu_11_0_power_state power_state;
};

enum smu_v11_0_baco_seq {
	BACO_SEQ_BACO = 0,
	BACO_SEQ_MSR,
	BACO_SEQ_BAMACO,
	BACO_SEQ_ULPS,
	BACO_SEQ_COUNT,
};

int smu_v11_0_init_microcode(struct smu_context *smu);

int smu_v11_0_load_microcode(struct smu_context *smu);

int smu_v11_0_init_smc_tables(struct smu_context *smu);

int smu_v11_0_fini_smc_tables(struct smu_context *smu);

int smu_v11_0_init_power(struct smu_context *smu);

int smu_v11_0_fini_power(struct smu_context *smu);

int smu_v11_0_check_fw_status(struct smu_context *smu);

int smu_v11_0_setup_pptable(struct smu_context *smu);

int smu_v11_0_get_vbios_bootup_values(struct smu_context *smu);

int smu_v11_0_get_clk_info_from_vbios(struct smu_context *smu);

int smu_v11_0_check_pptable(struct smu_context *smu);

int smu_v11_0_parse_pptable(struct smu_context *smu);

int smu_v11_0_populate_smc_pptable(struct smu_context *smu);

int smu_v11_0_check_fw_version(struct smu_context *smu);

int smu_v11_0_write_pptable(struct smu_context *smu);

int smu_v11_0_set_min_dcef_deep_sleep(struct smu_context *smu);

int smu_v11_0_set_driver_table_location(struct smu_context *smu);

int smu_v11_0_set_tool_table_location(struct smu_context *smu);

int smu_v11_0_notify_memory_pool_location(struct smu_context *smu);

int smu_v11_0_system_features_control(struct smu_context *smu,
					     bool en);

int
smu_v11_0_send_msg_with_param(struct smu_context *smu,
			      enum smu_message_type msg,
			      uint32_t param);

int smu_v11_0_read_arg(struct smu_context *smu, uint32_t *arg);

int smu_v11_0_init_display_count(struct smu_context *smu, uint32_t count);

int smu_v11_0_set_allowed_mask(struct smu_context *smu);

int smu_v11_0_get_enabled_mask(struct smu_context *smu,
				      uint32_t *feature_mask, uint32_t num);

int smu_v11_0_notify_display_change(struct smu_context *smu);

int smu_v11_0_set_power_limit(struct smu_context *smu, uint32_t n);

int smu_v11_0_get_current_clk_freq(struct smu_context *smu,
					  enum smu_clk_type clk_id,
					  uint32_t *value);

int smu_v11_0_init_max_sustainable_clocks(struct smu_context *smu);

int smu_v11_0_start_thermal_control(struct smu_context *smu);

int smu_v11_0_stop_thermal_control(struct smu_context *smu);

int smu_v11_0_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size);

int smu_v11_0_set_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk);

int
smu_v11_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req);

uint32_t
smu_v11_0_get_fan_control_mode(struct smu_context *smu);

int
smu_v11_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode);

int
smu_v11_0_set_fan_speed_percent(struct smu_context *smu, uint32_t speed);

int smu_v11_0_set_fan_speed_rpm(struct smu_context *smu,
				       uint32_t speed);

int smu_v11_0_set_xgmi_pstate(struct smu_context *smu,
				     uint32_t pstate);

int smu_v11_0_gfx_off_control(struct smu_context *smu, bool enable);

int smu_v11_0_register_irq_handler(struct smu_context *smu);

int smu_v11_0_set_azalia_d3_pme(struct smu_context *smu);

int smu_v11_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
		struct pp_smu_nv_clock_table *max_clocks);

bool smu_v11_0_baco_is_support(struct smu_context *smu);

enum smu_baco_state smu_v11_0_baco_get_state(struct smu_context *smu);

int smu_v11_0_baco_set_state(struct smu_context *smu, enum smu_baco_state state);

int smu_v11_0_baco_enter(struct smu_context *smu);
int smu_v11_0_baco_exit(struct smu_context *smu);

int smu_v11_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
						 uint32_t *min, uint32_t *max);

int smu_v11_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max);

int smu_v11_0_override_pcie_parameters(struct smu_context *smu);

int smu_v11_0_set_default_od_settings(struct smu_context *smu, bool initialize, size_t overdrive_table_size);

uint32_t smu_v11_0_get_max_power_limit(struct smu_context *smu);

int smu_v11_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level);

#endif
