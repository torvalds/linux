// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/smiapp/smiapp-limits.c
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#include "smiapp.h"

struct smiapp_reg_limits smiapp_reg_limits[] = {
	{ SMIAPP_REG_U16_ANALOGUE_GAIN_CAPABILITY, "analogue_gain_capability" }, /* 0 */
	{ SMIAPP_REG_U16_ANALOGUE_GAIN_CODE_MIN, "analogue_gain_code_min" },
	{ SMIAPP_REG_U16_ANALOGUE_GAIN_CODE_MAX, "analogue_gain_code_max" },
	{ SMIAPP_REG_U8_THS_ZERO_MIN, "ths_zero_min" },
	{ SMIAPP_REG_U8_TCLK_TRAIL_MIN, "tclk_trail_min" },
	{ SMIAPP_REG_U16_INTEGRATION_TIME_CAPABILITY, "integration_time_capability" }, /* 5 */
	{ SMIAPP_REG_U16_COARSE_INTEGRATION_TIME_MIN, "coarse_integration_time_min" },
	{ SMIAPP_REG_U16_COARSE_INTEGRATION_TIME_MAX_MARGIN, "coarse_integration_time_max_margin" },
	{ SMIAPP_REG_U16_FINE_INTEGRATION_TIME_MIN, "fine_integration_time_min" },
	{ SMIAPP_REG_U16_FINE_INTEGRATION_TIME_MAX_MARGIN, "fine_integration_time_max_margin" },
	{ SMIAPP_REG_U16_DIGITAL_GAIN_CAPABILITY, "digital_gain_capability" }, /* 10 */
	{ SMIAPP_REG_U16_DIGITAL_GAIN_MIN, "digital_gain_min" },
	{ SMIAPP_REG_U16_DIGITAL_GAIN_MAX, "digital_gain_max" },
	{ SMIAPP_REG_F32_MIN_EXT_CLK_FREQ_HZ, "min_ext_clk_freq_hz" },
	{ SMIAPP_REG_F32_MAX_EXT_CLK_FREQ_HZ, "max_ext_clk_freq_hz" },
	{ SMIAPP_REG_U16_MIN_PRE_PLL_CLK_DIV, "min_pre_pll_clk_div" }, /* 15 */
	{ SMIAPP_REG_U16_MAX_PRE_PLL_CLK_DIV, "max_pre_pll_clk_div" },
	{ SMIAPP_REG_F32_MIN_PLL_IP_FREQ_HZ, "min_pll_ip_freq_hz" },
	{ SMIAPP_REG_F32_MAX_PLL_IP_FREQ_HZ, "max_pll_ip_freq_hz" },
	{ SMIAPP_REG_U16_MIN_PLL_MULTIPLIER, "min_pll_multiplier" },
	{ SMIAPP_REG_U16_MAX_PLL_MULTIPLIER, "max_pll_multiplier" }, /* 20 */
	{ SMIAPP_REG_F32_MIN_PLL_OP_FREQ_HZ, "min_pll_op_freq_hz" },
	{ SMIAPP_REG_F32_MAX_PLL_OP_FREQ_HZ, "max_pll_op_freq_hz" },
	{ SMIAPP_REG_U16_MIN_VT_SYS_CLK_DIV, "min_vt_sys_clk_div" },
	{ SMIAPP_REG_U16_MAX_VT_SYS_CLK_DIV, "max_vt_sys_clk_div" },
	{ SMIAPP_REG_F32_MIN_VT_SYS_CLK_FREQ_HZ, "min_vt_sys_clk_freq_hz" }, /* 25 */
	{ SMIAPP_REG_F32_MAX_VT_SYS_CLK_FREQ_HZ, "max_vt_sys_clk_freq_hz" },
	{ SMIAPP_REG_F32_MIN_VT_PIX_CLK_FREQ_HZ, "min_vt_pix_clk_freq_hz" },
	{ SMIAPP_REG_F32_MAX_VT_PIX_CLK_FREQ_HZ, "max_vt_pix_clk_freq_hz" },
	{ SMIAPP_REG_U16_MIN_VT_PIX_CLK_DIV, "min_vt_pix_clk_div" },
	{ SMIAPP_REG_U16_MAX_VT_PIX_CLK_DIV, "max_vt_pix_clk_div" }, /* 30 */
	{ SMIAPP_REG_U16_MIN_FRAME_LENGTH_LINES, "min_frame_length_lines" },
	{ SMIAPP_REG_U16_MAX_FRAME_LENGTH_LINES, "max_frame_length_lines" },
	{ SMIAPP_REG_U16_MIN_LINE_LENGTH_PCK, "min_line_length_pck" },
	{ SMIAPP_REG_U16_MAX_LINE_LENGTH_PCK, "max_line_length_pck" },
	{ SMIAPP_REG_U16_MIN_LINE_BLANKING_PCK, "min_line_blanking_pck" }, /* 35 */
	{ SMIAPP_REG_U16_MIN_FRAME_BLANKING_LINES, "min_frame_blanking_lines" },
	{ SMIAPP_REG_U8_MIN_LINE_LENGTH_PCK_STEP_SIZE, "min_line_length_pck_step_size" },
	{ SMIAPP_REG_U16_MIN_OP_SYS_CLK_DIV, "min_op_sys_clk_div" },
	{ SMIAPP_REG_U16_MAX_OP_SYS_CLK_DIV, "max_op_sys_clk_div" },
	{ SMIAPP_REG_F32_MIN_OP_SYS_CLK_FREQ_HZ, "min_op_sys_clk_freq_hz" }, /* 40 */
	{ SMIAPP_REG_F32_MAX_OP_SYS_CLK_FREQ_HZ, "max_op_sys_clk_freq_hz" },
	{ SMIAPP_REG_U16_MIN_OP_PIX_CLK_DIV, "min_op_pix_clk_div" },
	{ SMIAPP_REG_U16_MAX_OP_PIX_CLK_DIV, "max_op_pix_clk_div" },
	{ SMIAPP_REG_F32_MIN_OP_PIX_CLK_FREQ_HZ, "min_op_pix_clk_freq_hz" },
	{ SMIAPP_REG_F32_MAX_OP_PIX_CLK_FREQ_HZ, "max_op_pix_clk_freq_hz" }, /* 45 */
	{ SMIAPP_REG_U16_X_ADDR_MIN, "x_addr_min" },
	{ SMIAPP_REG_U16_Y_ADDR_MIN, "y_addr_min" },
	{ SMIAPP_REG_U16_X_ADDR_MAX, "x_addr_max" },
	{ SMIAPP_REG_U16_Y_ADDR_MAX, "y_addr_max" },
	{ SMIAPP_REG_U16_MIN_X_OUTPUT_SIZE, "min_x_output_size" }, /* 50 */
	{ SMIAPP_REG_U16_MIN_Y_OUTPUT_SIZE, "min_y_output_size" },
	{ SMIAPP_REG_U16_MAX_X_OUTPUT_SIZE, "max_x_output_size" },
	{ SMIAPP_REG_U16_MAX_Y_OUTPUT_SIZE, "max_y_output_size" },
	{ SMIAPP_REG_U16_MIN_EVEN_INC, "min_even_inc" },
	{ SMIAPP_REG_U16_MAX_EVEN_INC, "max_even_inc" }, /* 55 */
	{ SMIAPP_REG_U16_MIN_ODD_INC, "min_odd_inc" },
	{ SMIAPP_REG_U16_MAX_ODD_INC, "max_odd_inc" },
	{ SMIAPP_REG_U16_SCALING_CAPABILITY, "scaling_capability" },
	{ SMIAPP_REG_U16_SCALER_M_MIN, "scaler_m_min" },
	{ SMIAPP_REG_U16_SCALER_M_MAX, "scaler_m_max" }, /* 60 */
	{ SMIAPP_REG_U16_SCALER_N_MIN, "scaler_n_min" },
	{ SMIAPP_REG_U16_SCALER_N_MAX, "scaler_n_max" },
	{ SMIAPP_REG_U16_SPATIAL_SAMPLING_CAPABILITY, "spatial_sampling_capability" },
	{ SMIAPP_REG_U8_DIGITAL_CROP_CAPABILITY, "digital_crop_capability" },
	{ SMIAPP_REG_U16_COMPRESSION_CAPABILITY, "compression_capability" }, /* 65 */
	{ SMIAPP_REG_U8_FIFO_SUPPORT_CAPABILITY, "fifo_support_capability" },
	{ SMIAPP_REG_U8_DPHY_CTRL_CAPABILITY, "dphy_ctrl_capability" },
	{ SMIAPP_REG_U8_CSI_LANE_MODE_CAPABILITY, "csi_lane_mode_capability" },
	{ SMIAPP_REG_U8_CSI_SIGNALLING_MODE_CAPABILITY, "csi_signalling_mode_capability" },
	{ SMIAPP_REG_U8_FAST_STANDBY_CAPABILITY, "fast_standby_capability" }, /* 70 */
	{ SMIAPP_REG_U8_CCI_ADDRESS_CONTROL_CAPABILITY, "cci_address_control_capability" },
	{ SMIAPP_REG_U32_MAX_PER_LANE_BITRATE_1_LANE_MODE_MBPS, "max_per_lane_bitrate_1_lane_mode_mbps" },
	{ SMIAPP_REG_U32_MAX_PER_LANE_BITRATE_2_LANE_MODE_MBPS, "max_per_lane_bitrate_2_lane_mode_mbps" },
	{ SMIAPP_REG_U32_MAX_PER_LANE_BITRATE_3_LANE_MODE_MBPS, "max_per_lane_bitrate_3_lane_mode_mbps" },
	{ SMIAPP_REG_U32_MAX_PER_LANE_BITRATE_4_LANE_MODE_MBPS, "max_per_lane_bitrate_4_lane_mode_mbps" }, /* 75 */
	{ SMIAPP_REG_U8_TEMP_SENSOR_CAPABILITY, "temp_sensor_capability" },
	{ SMIAPP_REG_U16_MIN_FRAME_LENGTH_LINES_BIN, "min_frame_length_lines_bin" },
	{ SMIAPP_REG_U16_MAX_FRAME_LENGTH_LINES_BIN, "max_frame_length_lines_bin" },
	{ SMIAPP_REG_U16_MIN_LINE_LENGTH_PCK_BIN, "min_line_length_pck_bin" },
	{ SMIAPP_REG_U16_MAX_LINE_LENGTH_PCK_BIN, "max_line_length_pck_bin" }, /* 80 */
	{ SMIAPP_REG_U16_MIN_LINE_BLANKING_PCK_BIN, "min_line_blanking_pck_bin" },
	{ SMIAPP_REG_U16_FINE_INTEGRATION_TIME_MIN_BIN, "fine_integration_time_min_bin" },
	{ SMIAPP_REG_U16_FINE_INTEGRATION_TIME_MAX_MARGIN_BIN, "fine_integration_time_max_margin_bin" },
	{ SMIAPP_REG_U8_BINNING_CAPABILITY, "binning_capability" },
	{ SMIAPP_REG_U8_BINNING_WEIGHTING_CAPABILITY, "binning_weighting_capability" }, /* 85 */
	{ SMIAPP_REG_U8_DATA_TRANSFER_IF_CAPABILITY, "data_transfer_if_capability" },
	{ SMIAPP_REG_U8_SHADING_CORRECTION_CAPABILITY, "shading_correction_capability" },
	{ SMIAPP_REG_U8_GREEN_IMBALANCE_CAPABILITY, "green_imbalance_capability" },
	{ SMIAPP_REG_U8_BLACK_LEVEL_CAPABILITY, "black_level_capability" },
	{ SMIAPP_REG_U8_MODULE_SPECIFIC_CORRECTION_CAPABILITY, "module_specific_correction_capability" }, /* 90 */
	{ SMIAPP_REG_U16_DEFECT_CORRECTION_CAPABILITY, "defect_correction_capability" },
	{ SMIAPP_REG_U16_DEFECT_CORRECTION_CAPABILITY_2, "defect_correction_capability_2" },
	{ SMIAPP_REG_U8_EDOF_CAPABILITY, "edof_capability" },
	{ SMIAPP_REG_U8_COLOUR_FEEDBACK_CAPABILITY, "colour_feedback_capability" },
	{ SMIAPP_REG_U8_ESTIMATION_MODE_CAPABILITY, "estimation_mode_capability" }, /* 95 */
	{ SMIAPP_REG_U8_ESTIMATION_ZONE_CAPABILITY, "estimation_zone_capability" },
	{ SMIAPP_REG_U16_CAPABILITY_TRDY_MIN, "capability_trdy_min" },
	{ SMIAPP_REG_U8_FLASH_MODE_CAPABILITY, "flash_mode_capability" },
	{ SMIAPP_REG_U8_ACTUATOR_CAPABILITY, "actuator_capability" },
	{ SMIAPP_REG_U8_BRACKETING_LUT_CAPABILITY_1, "bracketing_lut_capability_1" }, /* 100 */
	{ SMIAPP_REG_U8_BRACKETING_LUT_CAPABILITY_2, "bracketing_lut_capability_2" },
	{ SMIAPP_REG_U16_ANALOGUE_GAIN_CODE_STEP, "analogue_gain_code_step" },
	{ 0, NULL },
};
