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

#define SMU13_MODE1_RESET_WAIT_TIME_IN_MS 500  //500ms

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
#define smnMP1_V13_0_4_FIRMWARE_FLAGS	0x3010028
#define smnMP0_FW_INTF			0x30101c0
#define smnMP1_PUB_CTRL			0x3010b14

#define TEMP_RANGE_MIN			(0)
#define TEMP_RANGE_MAX			(80 * 1000)

#define SMU13_TOOL_SIZE			0x19000

#define MAX_DPM_LEVELS 16
#define MAX_PCIE_CONF 3

#define CTF_OFFSET_EDGE			5
#define CTF_OFFSET_HOTSPOT		5
#define CTF_OFFSET_MEM			5

#define SMU_13_VCLK_SHIFT		16

extern const int pmfw_decoded_link_speed[5];
extern const int pmfw_decoded_link_width[7];

#define DECODE_GEN_SPEED(gen_speed_idx)		(pmfw_decoded_link_speed[gen_speed_idx])
#define DECODE_LANE_WIDTH(lane_width_idx)	(pmfw_decoded_link_width[lane_width_idx])

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
	bool				is_fine_grained;
	struct smu_13_0_dpm_clk_level	dpm_levels[MAX_DPM_LEVELS];
};

struct smu_13_0_pcie_table {
	uint8_t  pcie_gen[MAX_PCIE_CONF];
	uint8_t  pcie_lane[MAX_PCIE_CONF];
	uint16_t clk_freq[MAX_PCIE_CONF];
	uint32_t num_of_link_levels;
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
	uint64_t                    caps;
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
	atomic_t	throttle_status;
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

int smu_v13_0_set_power_limit(struct smu_context *smu,
			      enum smu_ppt_limit_type limit_type,
			      uint32_t limit);

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

int smu_v13_0_set_fan_speed_pwm(struct smu_context *smu,
				uint32_t speed);

int smu_v13_0_set_fan_speed_rpm(struct smu_context *smu,
				uint32_t speed);

int smu_v13_0_set_xgmi_pstate(struct smu_context *smu,
			      uint32_t pstate);

int smu_v13_0_gfx_off_control(struct smu_context *smu, bool enable);

int smu_v13_0_register_irq_handler(struct smu_context *smu);

int smu_v13_0_set_azalia_d3_pme(struct smu_context *smu);

int smu_v13_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					       struct pp_smu_nv_clock_table *max_clocks);

int smu_v13_0_get_bamaco_support(struct smu_context *smu);

int smu_v13_0_baco_enter(struct smu_context *smu);
int smu_v13_0_baco_exit(struct smu_context *smu);

int smu_v13_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
				    uint32_t *min, uint32_t *max);

int smu_v13_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
					  uint32_t min, uint32_t max, bool automatic);

int smu_v13_0_set_hard_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max);

int smu_v13_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level);

int smu_v13_0_set_power_source(struct smu_context *smu,
			       enum smu_power_src_type power_src);

int smu_v13_0_set_single_dpm_table(struct smu_context *smu,
				   enum smu_clk_type clk_type,
				   struct smu_13_0_dpm_table *single_dpm_table);

int smu_v13_0_get_dpm_freq_by_index(struct smu_context *smu,
				    enum smu_clk_type clk_type, uint16_t level,
				    uint32_t *value);

int smu_v13_0_get_current_pcie_link_width_level(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_width(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_speed_level(struct smu_context *smu);

int smu_v13_0_get_current_pcie_link_speed(struct smu_context *smu);

int smu_v13_0_gfx_ulv_control(struct smu_context *smu,
			      bool enablement);

int smu_v13_0_wait_for_event(struct smu_context *smu, enum smu_event_type event,
			     uint64_t event_arg);

int smu_v13_0_set_vcn_enable(struct smu_context *smu,
			      bool enable,
			      int inst);

int smu_v13_0_set_jpeg_enable(struct smu_context *smu,
			      bool enable);

int smu_v13_0_init_pptable_microcode(struct smu_context *smu);

int smu_v13_0_run_btc(struct smu_context *smu);

int smu_v13_0_gpo_control(struct smu_context *smu,
			  bool enablement);

int smu_v13_0_deep_sleep_control(struct smu_context *smu,
				 bool enablement);

int smu_v13_0_set_gfx_power_up_by_imu(struct smu_context *smu);

int smu_v13_0_od_edit_dpm_table(struct smu_context *smu,
				enum PP_OD_DPM_TABLE_COMMAND type,
				long input[],
				uint32_t size);

int smu_v13_0_set_default_dpm_tables(struct smu_context *smu);

void smu_v13_0_set_smu_mailbox_registers(struct smu_context *smu);

int smu_v13_0_mode1_reset(struct smu_context *smu);

int smu_v13_0_get_pptable_from_firmware(struct smu_context *smu,
					void **table,
					uint32_t *size,
					uint32_t pptable_id);

int smu_v13_0_update_pcie_parameters(struct smu_context *smu,
				     uint8_t pcie_gen_cap,
				     uint8_t pcie_width_cap);

int smu_v13_0_disable_pmfw_state(struct smu_context *smu);

int smu_v13_0_enable_uclk_shadow(struct smu_context *smu, bool enable);

int smu_v13_0_set_wbrf_exclusion_ranges(struct smu_context *smu,
						 struct freq_band_range *exclusion_ranges);

int smu_v13_0_get_boot_freq_by_index(struct smu_context *smu,
				     enum smu_clk_type clk_type,
				     uint32_t *value);

void smu_v13_0_interrupt_work(struct smu_context *smu);
#endif
#endif
