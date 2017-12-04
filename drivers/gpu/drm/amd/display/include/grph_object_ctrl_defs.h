/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_GRPH_OBJECT_CTRL_DEFS_H__
#define __DAL_GRPH_OBJECT_CTRL_DEFS_H__

#include "grph_object_defs.h"

/*
 * #####################################################
 * #####################################################
 *
 * These defines shared between asic_control/bios_parser and other
 * DAL components
 *
 * #####################################################
 * #####################################################
 */

enum display_output_bit_depth {
	PANEL_UNDEFINE = 0,
	PANEL_6BIT_COLOR = 1,
	PANEL_8BIT_COLOR = 2,
	PANEL_10BIT_COLOR = 3,
	PANEL_12BIT_COLOR = 4,
	PANEL_16BIT_COLOR = 5,
};


/* Device type as abstracted by ATOM BIOS */
enum dal_device_type {
	DEVICE_TYPE_UNKNOWN = 0,
	DEVICE_TYPE_LCD,
	DEVICE_TYPE_CRT,
	DEVICE_TYPE_DFP,
	DEVICE_TYPE_CV,
	DEVICE_TYPE_TV,
	DEVICE_TYPE_CF,
	DEVICE_TYPE_WIRELESS
};

/* Device ID as abstracted by ATOM BIOS */
struct device_id {
	enum dal_device_type device_type:16;
	uint32_t enum_id:16;	/* 1 based enum */
	uint16_t raw_device_tag;
};

struct graphics_object_i2c_info {
	struct gpio_info {
		uint32_t clk_mask_register_index;
		uint32_t clk_en_register_index;
		uint32_t clk_y_register_index;
		uint32_t clk_a_register_index;
		uint32_t data_mask_register_index;
		uint32_t data_en_register_index;
		uint32_t data_y_register_index;
		uint32_t data_a_register_index;

		uint32_t clk_mask_shift;
		uint32_t clk_en_shift;
		uint32_t clk_y_shift;
		uint32_t clk_a_shift;
		uint32_t data_mask_shift;
		uint32_t data_en_shift;
		uint32_t data_y_shift;
		uint32_t data_a_shift;
	} gpio_info;

	bool i2c_hw_assist;
	uint32_t i2c_line;
	uint32_t i2c_engine_id;
	uint32_t i2c_slave_address;
};

struct graphics_object_hpd_info {
	uint8_t hpd_int_gpio_uid;
	uint8_t hpd_active;
};

struct connector_device_tag_info {
	uint32_t acpi_device;
	struct device_id dev_id;
};

struct device_timing {
	struct misc_info {
		uint32_t HORIZONTAL_CUT_OFF:1;
		/* 0=Active High, 1=Active Low */
		uint32_t H_SYNC_POLARITY:1;
		/* 0=Active High, 1=Active Low */
		uint32_t V_SYNC_POLARITY:1;
		uint32_t VERTICAL_CUT_OFF:1;
		uint32_t H_REPLICATION_BY2:1;
		uint32_t V_REPLICATION_BY2:1;
		uint32_t COMPOSITE_SYNC:1;
		uint32_t INTERLACE:1;
		uint32_t DOUBLE_CLOCK:1;
		uint32_t RGB888:1;
		uint32_t GREY_LEVEL:2;
		uint32_t SPATIAL:1;
		uint32_t TEMPORAL:1;
		uint32_t API_ENABLED:1;
	} misc_info;

	uint32_t pixel_clk; /* in KHz */
	uint32_t horizontal_addressable;
	uint32_t horizontal_blanking_time;
	uint32_t vertical_addressable;
	uint32_t vertical_blanking_time;
	uint32_t horizontal_sync_offset;
	uint32_t horizontal_sync_width;
	uint32_t vertical_sync_offset;
	uint32_t vertical_sync_width;
	uint32_t horizontal_border;
	uint32_t vertical_border;
};

struct supported_refresh_rate {
	uint32_t REFRESH_RATE_30HZ:1;
	uint32_t REFRESH_RATE_40HZ:1;
	uint32_t REFRESH_RATE_48HZ:1;
	uint32_t REFRESH_RATE_50HZ:1;
	uint32_t REFRESH_RATE_60HZ:1;
};

struct embedded_panel_info {
	struct device_timing lcd_timing;
	uint32_t ss_id;
	struct supported_refresh_rate supported_rr;
	uint32_t drr_enabled;
	uint32_t min_drr_refresh_rate;
	bool realtek_eDPToLVDS;
};

struct dc_firmware_info {
	struct pll_info {
		uint32_t crystal_frequency; /* in KHz */
		uint32_t min_input_pxl_clk_pll_frequency; /* in KHz */
		uint32_t max_input_pxl_clk_pll_frequency; /* in KHz */
		uint32_t min_output_pxl_clk_pll_frequency; /* in KHz */
		uint32_t max_output_pxl_clk_pll_frequency; /* in KHz */
	} pll_info;

	struct firmware_feature {
		uint32_t memory_clk_ss_percentage;
		uint32_t engine_clk_ss_percentage;
	} feature;

	uint32_t default_display_engine_pll_frequency; /* in KHz */
	uint32_t external_clock_source_frequency_for_dp; /* in KHz */
	uint32_t smu_gpu_pll_output_freq; /* in KHz */
	uint8_t min_allowed_bl_level;
	uint8_t remote_display_config;
	uint32_t default_memory_clk; /* in KHz */
	uint32_t default_engine_clk; /* in KHz */
	uint32_t dp_phy_ref_clk; /* in KHz - DCE12 only */
	uint32_t i2c_engine_ref_clk; /* in KHz - DCE12 only */


};

struct step_and_delay_info {
	uint32_t step;
	uint32_t delay;
	uint32_t recommended_ref_div;
};

struct spread_spectrum_info {
	struct spread_spectrum_type {
		bool CENTER_MODE:1;
		bool EXTERNAL:1;
		bool STEP_AND_DELAY_INFO:1;
	} type;

	/* in unit of 0.01% (spreadPercentageDivider = 100),
	otherwise in 0.001% units (spreadPercentageDivider = 1000); */
	uint32_t spread_spectrum_percentage;
	uint32_t spread_percentage_divider; /* 100 or 1000 */
	uint32_t spread_spectrum_range; /* modulation freq (HZ)*/

	union {
		struct step_and_delay_info step_and_delay_info;
		/* For mem/engine/uvd, Clock Out frequence (VCO ),
		in unit of kHz. For TMDS/HDMI/LVDS, it is pixel clock,
		for DP, it is link clock ( 270000 or 162000 ) */
		uint32_t target_clock_range; /* in KHz */
	};

};

struct graphics_object_encoder_cap_info {
	uint32_t dp_hbr2_cap:1;
	uint32_t dp_hbr2_validated:1;
	/*
	 * TODO: added MST and HDMI 6G capable flags
	 */
	uint32_t reserved:15;
};

struct din_connector_info {
	uint32_t gpio_id;
	bool gpio_tv_active_state;
};

/* Invalid channel mapping */
enum { INVALID_DDI_CHANNEL_MAPPING = 0x0 };

/**
 * DDI PHY channel mapping reflecting XBAR setting
 */
union ddi_channel_mapping {
	struct mapping {
		uint8_t lane0:2;	/* Mapping for lane 0 */
		uint8_t lane1:2;	/* Mapping for lane 1 */
		uint8_t lane2:2;	/* Mapping for lane 2 */
		uint8_t lane3:2;	/* Mapping for lane 3 */
	} mapping;
	uint8_t raw;
};

/**
* Transmitter output configuration description
*/
struct transmitter_configuration_info {
	/* DDI PHY ID for the transmitter */
	enum transmitter transmitter_phy_id;
	/* DDI PHY channel mapping reflecting crossbar setting */
	union ddi_channel_mapping output_channel_mapping;
};

struct transmitter_configuration {
	/* Configuration for the primary transmitter */
	struct transmitter_configuration_info primary_transmitter_config;
	/* Secondary transmitter configuration for Dual-link DVI */
	struct transmitter_configuration_info secondary_transmitter_config;
};

/* These size should be sufficient to store info coming from BIOS */
#define NUMBER_OF_UCHAR_FOR_GUID 16
#define MAX_NUMBER_OF_EXT_DISPLAY_PATH 7
#define NUMBER_OF_CSR_M3_ARB 10
#define NUMBER_OF_DISP_CLK_VOLTAGE 4
#define NUMBER_OF_AVAILABLE_SCLK 5

struct i2c_reg_info {
	unsigned char       i2c_reg_index;
	unsigned char       i2c_reg_val;
};

struct ext_hdmi_settings {
	unsigned char   slv_addr;
	unsigned char   reg_num;
	struct i2c_reg_info      reg_settings[9];
	unsigned char   reg_num_6g;
	struct i2c_reg_info      reg_settings_6g[3];
};


/* V6 */
struct integrated_info {
	struct clock_voltage_caps {
		/* The Voltage Index indicated by FUSE, same voltage index
		shared with SCLK DPM fuse table */
		uint32_t voltage_index;
		/* Maximum clock supported with specified voltage index */
		uint32_t max_supported_clk; /* in KHz */
	} disp_clk_voltage[NUMBER_OF_DISP_CLK_VOLTAGE];

	struct display_connection_info {
		struct external_display_path {
			/* A bit vector to show what devices are supported */
			uint32_t device_tag;
			/* 16bit device ACPI id. */
			uint32_t device_acpi_enum;
			/* A physical connector for displays to plug in,
			using object connector definitions */
			struct graphics_object_id device_connector_id;
			/* An index into external AUX/DDC channel LUT */
			uint8_t ext_aux_ddc_lut_index;
			/* An index into external HPD pin LUT */
			uint8_t ext_hpd_pin_lut_index;
			/* external encoder object id */
			struct graphics_object_id ext_encoder_obj_id;
			/* XBAR mapping of the PHY channels */
			union ddi_channel_mapping channel_mapping;

			unsigned short caps;
		} path[MAX_NUMBER_OF_EXT_DISPLAY_PATH];

		uint8_t gu_id[NUMBER_OF_UCHAR_FOR_GUID];
		uint8_t checksum;
	} ext_disp_conn_info; /* exiting long long time */

	struct available_s_clk_list {
		/* Maximum clock supported with specified voltage index */
		uint32_t supported_s_clk; /* in KHz */
		/* The Voltage Index indicated by FUSE for specified SCLK */
		uint32_t voltage_index;
		/* The Voltage ID indicated by FUSE for specified SCLK */
		uint32_t voltage_id;
	} avail_s_clk[NUMBER_OF_AVAILABLE_SCLK];

	uint8_t memory_type;
	uint8_t ma_channel_number;
	uint32_t boot_up_engine_clock; /* in KHz */
	uint32_t dentist_vco_freq; /* in KHz */
	uint32_t boot_up_uma_clock; /* in KHz */
	uint32_t boot_up_req_display_vector;
	uint32_t other_display_misc;
	uint32_t gpu_cap_info;
	uint32_t sb_mmio_base_addr;
	uint32_t system_config;
	uint32_t cpu_cap_info;
	uint32_t max_nb_voltage;
	uint32_t min_nb_voltage;
	uint32_t boot_up_nb_voltage;
	uint32_t ext_disp_conn_info_offset;
	uint32_t csr_m3_arb_cntl_default[NUMBER_OF_CSR_M3_ARB];
	uint32_t csr_m3_arb_cntl_uvd[NUMBER_OF_CSR_M3_ARB];
	uint32_t csr_m3_arb_cntl_fs3d[NUMBER_OF_CSR_M3_ARB];
	uint32_t gmc_restore_reset_time;
	uint32_t minimum_n_clk;
	uint32_t idle_n_clk;
	uint32_t ddr_dll_power_up_time;
	uint32_t ddr_pll_power_up_time;
	/* start for V6 */
	uint32_t pcie_clk_ss_type;
	uint32_t lvds_ss_percentage;
	uint32_t lvds_sspread_rate_in_10hz;
	uint32_t hdmi_ss_percentage;
	uint32_t hdmi_sspread_rate_in_10hz;
	uint32_t dvi_ss_percentage;
	uint32_t dvi_sspread_rate_in_10_hz;
	uint32_t sclk_dpm_boost_margin;
	uint32_t sclk_dpm_throttle_margin;
	uint32_t sclk_dpm_tdp_limit_pg;
	uint32_t sclk_dpm_tdp_limit_boost;
	uint32_t boost_engine_clock;
	uint32_t boost_vid_2bit;
	uint32_t enable_boost;
	uint32_t gnb_tdp_limit;
	/* Start from V7 */
	uint32_t max_lvds_pclk_freq_in_single_link;
	uint32_t lvds_misc;
	uint32_t lvds_pwr_on_seq_dig_on_to_de_in_4ms;
	uint32_t lvds_pwr_on_seq_de_to_vary_bl_in_4ms;
	uint32_t lvds_pwr_off_seq_vary_bl_to_de_in4ms;
	uint32_t lvds_pwr_off_seq_de_to_dig_on_in4ms;
	uint32_t lvds_off_to_on_delay_in_4ms;
	uint32_t lvds_pwr_on_seq_vary_bl_to_blon_in_4ms;
	uint32_t lvds_pwr_off_seq_blon_to_vary_bl_in_4ms;
	uint32_t lvds_reserved1;
	uint32_t lvds_bit_depth_control_val;
	//Start from V9
	unsigned char dp0_ext_hdmi_slv_addr;
	unsigned char dp0_ext_hdmi_reg_num;
	struct i2c_reg_info dp0_ext_hdmi_reg_settings[9];
	unsigned char dp0_ext_hdmi_6g_reg_num;
	struct i2c_reg_info dp0_ext_hdmi_6g_reg_settings[3];
	unsigned char dp1_ext_hdmi_slv_addr;
	unsigned char dp1_ext_hdmi_reg_num;
	struct i2c_reg_info dp1_ext_hdmi_reg_settings[9];
	unsigned char dp1_ext_hdmi_6g_reg_num;
	struct i2c_reg_info dp1_ext_hdmi_6g_reg_settings[3];
	unsigned char dp2_ext_hdmi_slv_addr;
	unsigned char dp2_ext_hdmi_reg_num;
	struct i2c_reg_info dp2_ext_hdmi_reg_settings[9];
	unsigned char dp2_ext_hdmi_6g_reg_num;
	struct i2c_reg_info dp2_ext_hdmi_6g_reg_settings[3];
	unsigned char dp3_ext_hdmi_slv_addr;
	unsigned char dp3_ext_hdmi_reg_num;
	struct i2c_reg_info dp3_ext_hdmi_reg_settings[9];
	unsigned char dp3_ext_hdmi_6g_reg_num;
	struct i2c_reg_info dp3_ext_hdmi_6g_reg_settings[3];
};

/**
* Power source ids.
*/
enum power_source {
	POWER_SOURCE_AC = 0,
	POWER_SOURCE_DC,
	POWER_SOURCE_LIMITED_POWER,
	POWER_SOURCE_LIMITED_POWER_2,
	POWER_SOURCE_MAX
};

struct bios_event_info {
	uint32_t thermal_state;
	uint32_t backlight_level;
	enum power_source powerSource;
	bool has_thermal_state_changed;
	bool has_power_source_changed;
	bool has_forced_mode_changed;
	bool forced_mode;
	bool backlight_changed;
};

enum {
	HDMI_PIXEL_CLOCK_IN_KHZ_297 = 297000,
	TMDS_PIXEL_CLOCK_IN_KHZ_165 = 165000
};

/*
 * DFS-bypass flag
 */
/* Copy of SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS from atombios.h */
enum {
	DFS_BYPASS_ENABLE = 0x10
};

enum {
	INVALID_BACKLIGHT = -1
};

struct panel_backlight_boundaries {
	uint32_t min_signal_level;
	uint32_t max_signal_level;
};


#endif
