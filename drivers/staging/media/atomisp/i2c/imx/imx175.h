/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IMX175_H__
#define __IMX175_H__
#include "common.h"

/************************** settings for imx *************************/
static struct imx_reg const imx_STILL_8M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xC4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x66},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0C},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0xD0},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x09},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xA0},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_8M_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0B},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xB8},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x16},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x44},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0C},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0xD0},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x09},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xA0},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_3M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xC4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x66},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x08},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x06},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x10},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x19}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_3M_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0B},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xB8},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x16},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x44},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x08},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x06},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x10},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x19}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};


static struct imx_reg const imx_STILL_5M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xC4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x66},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0A},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x07},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x90},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x14}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_5M_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0B},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xB8},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x16},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x44},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0A},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x07},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x90},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x14}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_6M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xC4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x66},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x32},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x6D},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0C},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0xD0},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x07},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x3C},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_6M_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEF},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0B},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xB8},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x16},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x44},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x32},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x6D},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x0C},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0xD0},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x07},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x3C},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_2M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x8C},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xC4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x66},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x68},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x04},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xD0},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_2M_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x0A},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x8C},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x2c},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0B},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xB8},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x16},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x44},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x68},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x04},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xD0},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x77},
	{IMX_8BIT, 0x3371, 0x2F},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x2F},
	{IMX_8BIT, 0x3375, 0x37},
	{IMX_8BIT, 0x3376, 0x9F},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x09},
	{IMX_8BIT, 0x33D7, 0xA0},

	{IMX_8BIT, 0x030e, 0x01},
	{IMX_8BIT, 0x41c0, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_PREVIEW_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x06},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x05},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x48},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x70},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x03},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x34},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x02},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x68},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x03},
	{IMX_8BIT, 0x33D5, 0x34},
	{IMX_8BIT, 0x33D6, 0x02},
	{IMX_8BIT, 0x33D7, 0x68},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_WIDE_PREVIEW_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x06},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x0D},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x70},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x10},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x00},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x14},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x8C},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x68},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x03},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xBC},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x68},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0xBC},
	{IMX_TOK_TERM, 0, 0}
};

/*****************************video************************/
static struct imx_reg const imx_1080p_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x06},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x4C},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xA4},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x11},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0xC6},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x01},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0xDB},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x02},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x42},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0A},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xEA},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x07},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x61},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x09},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x05},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x20},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_1080p_no_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x08},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xD5},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x07},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xD0},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0F},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x3C},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x34},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x6B},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x07},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x94},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x04},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x44},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x1B}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_1080p_no_dvs_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x08},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xD5},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x09},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xA6},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x18},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x9C},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x34},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x6B},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x07},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x94},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x04},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x44},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x1B}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};
/*****************************video************************/
static struct imx_reg const imx_720p_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x00},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x13},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x9C},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x01},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0xD7},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x02},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x3E},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0A},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xEE},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x07},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x65},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x10},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x03},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x70},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x00}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x18}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_480p_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x00},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x13},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x9C},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x01},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0xD4},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0xC8},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0A},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xF1},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x07},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0xDB},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x03},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x70},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x02},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x50},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x02}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x15}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_720p_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x04},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x05},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x48},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x14},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x28},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x48},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x64},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0x87},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x3B},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x20},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x03},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x6C},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x20},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0x6C},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_STILL_720p_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x04},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x08},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0xCA},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x18},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x38},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x48},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x01},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x64},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0x87},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x3B},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x20},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x03},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x6C},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x20},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0x6C},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_WVGA_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xEC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x09},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x01},
	{IMX_8BIT, 0x030D, 0x12},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x00},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x13},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x9C},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0xD0},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x08},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0xCF},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x06},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x68},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x04},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x00},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x01}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x57},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x6F},
	{IMX_8BIT, 0x3371, 0x27},
	{IMX_8BIT, 0x3372, 0x4F},
	{IMX_8BIT, 0x3373, 0x2F},
	{IMX_8BIT, 0x3374, 0x27},
	{IMX_8BIT, 0x3375, 0x2F},
	{IMX_8BIT, 0x3376, 0x97},
	{IMX_8BIT, 0x3377, 0x37},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x0C},
	{IMX_8BIT, 0x33D5, 0xD0},
	{IMX_8BIT, 0x33D6, 0x07},
	{IMX_8BIT, 0x33D7, 0x38},
	{IMX_TOK_TERM, 0, 0}
};
static struct imx_reg const imx_CIF_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x04},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x00},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x11},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0xDB},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x01},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x70},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x01},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x30},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x20},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0x6C},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_VGA_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x04},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x06},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x00},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x11},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x94},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x03},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x34},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x02},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x68},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x20},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0x6C},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_VGA_strong_dvs_15fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0xFC},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x04},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x07},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x9E},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x1C},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0xB6},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x00},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x00},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x00},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x00},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x0C},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0xCF},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x09},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x9F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x03},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x34},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x02},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x68},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x06},
	{IMX_8BIT, 0x33D5, 0x20},
	{IMX_8BIT, 0x33D6, 0x03},
	{IMX_8BIT, 0x33D7, 0x6C},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_QVGA_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x06},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x05},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x48},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x70},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x03},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0x38},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x02},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x68},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x09},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0x97},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x07},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x37},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x01},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0x98},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x01},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0x34},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x01},
	{IMX_8BIT, 0x33D5, 0x98},
	{IMX_8BIT, 0x33D6, 0x01},
	{IMX_8BIT, 0x33D7, 0x34},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_QCIF_strong_dvs_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0100, 0x00},  /*	mode_select	*/
	/* shutter */
	{IMX_8BIT, 0x0202, 0x05},  /* coarse _integration_time[15:8] */
	{IMX_8BIT, 0x0203, 0x44},  /* coarse _integration_time[7:0] */
	/* pll */
	{IMX_8BIT, 0x0301, 0x05},  /*	vt_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x0303, 0x01},  /*	vt_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x0305, 0x06},  /*	pre_pll_clk_div[7:0]	*/
	{IMX_8BIT, 0x0309, 0x05},  /*	op_pix_clk_div[7:0]	*/
	{IMX_8BIT, 0x030B, 0x01},  /*	op_sys_clk_div[7:0]	*/
	{IMX_8BIT, 0x030C, 0x00},
	{IMX_8BIT, 0x030D, 0x6D},
	/* image sizing */
	{IMX_8BIT, 0x0340, 0x05},  /* frame_length_lines[15:8] */
	{IMX_8BIT, 0x0341, 0x48},  /*	frame_length_lines[7:0]	*/
	{IMX_8BIT, 0x0342, 0x0D},  /*	line_length_pck[15:8]	*/
	{IMX_8BIT, 0x0343, 0x70},  /*	line_length_pck[7:0]	*/
	{IMX_8BIT, 0x0344, 0x04},  /*	x_addr_start[15:8]	*/
	{IMX_8BIT, 0x0345, 0xB8},  /*	x_addr_start[7:0]	*/
	{IMX_8BIT, 0x0346, 0x03},  /*	y_addr_start[15:8]	*/
	{IMX_8BIT, 0x0347, 0x70},  /*	y_addr_start[7:0]	*/
	{IMX_8BIT, 0x0348, 0x08},  /*	x_addr_end[15:8]	*/
	{IMX_8BIT, 0x0349, 0x17},  /*	x_addr_end[7:0]	*/
	{IMX_8BIT, 0x034A, 0x06},  /*	y_addr_end[15:8]	*/
	{IMX_8BIT, 0x034B, 0x2F},  /*	y_addr_end[7:0]	*/
	{IMX_8BIT, 0x034C, 0x00},  /*	x_output_size[15:8]	*/
	{IMX_8BIT, 0x034D, 0xD8},  /*	x_output_size[7:0]	*/
	{IMX_8BIT, 0x034E, 0x00},  /*	y_output_size[15:8]	*/
	{IMX_8BIT, 0x034F, 0xB0},  /*	y_output_size[7:0]	*/
	/* binning & scaling */
	{IMX_8BIT, 0x0390, 0x02}, /* binning mode */
	{IMX_8BIT, 0x0401, 0x00}, /* scaling mode*/
	{IMX_8BIT, 0x0405, 0x10}, /* scale_m[7:0] */
	/* timer */
	{IMX_8BIT, 0x3344, 0x37},
	{IMX_8BIT, 0x3345, 0x1F},
	/* timing */
	{IMX_8BIT, 0x3370, 0x5F},
	{IMX_8BIT, 0x3371, 0x17},
	{IMX_8BIT, 0x3372, 0x37},
	{IMX_8BIT, 0x3373, 0x17},
	{IMX_8BIT, 0x3374, 0x17},
	{IMX_8BIT, 0x3375, 0x0F},
	{IMX_8BIT, 0x3376, 0x57},
	{IMX_8BIT, 0x3377, 0x27},
	{IMX_8BIT, 0x33C8, 0x01},
	{IMX_8BIT, 0x33D4, 0x00},
	{IMX_8BIT, 0x33D5, 0xD8},
	{IMX_8BIT, 0x33D6, 0x00},
	{IMX_8BIT, 0x33D7, 0xB0},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx175_init_settings[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0103, 0x01},
	/* misc control */
	{IMX_8BIT, 0x3020, 0x10},
	{IMX_8BIT, 0x302D, 0x02},
	{IMX_8BIT, 0x302F, 0x80},
	{IMX_8BIT, 0x3032, 0xA3},
	{IMX_8BIT, 0x3033, 0x20},
	{IMX_8BIT, 0x3034, 0x24},
	{IMX_8BIT, 0x3041, 0x15},
	{IMX_8BIT, 0x3042, 0x87},
	{IMX_8BIT, 0x3050, 0x35},
	{IMX_8BIT, 0x3056, 0x57},
	{IMX_8BIT, 0x305D, 0x41},
	{IMX_8BIT, 0x3097, 0x69},
	{IMX_8BIT, 0x3109, 0x41},
	{IMX_8BIT, 0x3148, 0x3F},
	{IMX_8BIT, 0x330F, 0x07},
	/* csi & inck */
	{IMX_8BIT, 0x3364, 0x00},
	{IMX_8BIT, 0x3368, 0x13},
	{IMX_8BIT, 0x3369, 0x33},
	/* znr */
	{IMX_8BIT, 0x4100, 0x0E},
	{IMX_8BIT, 0x4104, 0x32},
	{IMX_8BIT, 0x4105, 0x32},
	{IMX_8BIT, 0x4108, 0x01},
	{IMX_8BIT, 0x4109, 0x7C},
	{IMX_8BIT, 0x410A, 0x00},
	{IMX_8BIT, 0x410B, 0x00},
	GROUPED_PARAMETER_HOLD_DISABLE,
	{IMX_TOK_TERM, 0, 0}
};
/* TODO settings of preview/still/video will be updated with new use case */
struct imx_resolution imx175_res_preview[] = {
	{
		.desc = "CIF_strong_dvs_30fps",
		.regs = imx_CIF_strong_dvs_30fps,
		.width = 368,
		.height = 304,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x11DB,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 261500,

	},
	{
		.desc = "VGA_strong_dvs_30fps",
		.regs = imx_VGA_strong_dvs_30fps,
		.width = 820,
		.height = 616,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x11DB,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 261500,
	},
	{
		.desc = "WIDE_PREVIEW_30fps",
		.regs = imx_WIDE_PREVIEW_30fps,
		.width = 1640,
		.height = 956,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x1000,
				 .lines_per_frame = 0x0D70,
			},
			{
			}
		},
		.mipi_freq = 174500,
	},
	{
		.desc = "STILL_720p_30fps",
		.regs = imx_STILL_720p_30fps,
		.width = 1568,
		.height = 876,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x1428,
				 .lines_per_frame = 0x0548,
			},
			{
			}
		},
		.mipi_freq = 261500,
	},
	{
		.desc = "STILL_2M_30fps",
		.regs = imx_STILL_2M_30fps,
		.width = 1640,
		.height = 1232,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D66,
				 .lines_per_frame = 0x09C4,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "1080p_strong_dvs_30fps",
		.regs = imx_1080p_no_dvs_30fps,
		.width = 1940,
		.height = 1092,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0F3C,
				 .lines_per_frame = 0x07D0,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
	{
		.desc = "STILL_3M_30fps",
		.regs = imx_STILL_3M_30fps,
		.width = 2064,
		.height = 1552,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D66,
				 .lines_per_frame = 0x09C4,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_5M_30fps",
		.regs = imx_STILL_5M_30fps,
		.width = 2576,
		.height = 1936,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D66,
				 .lines_per_frame = 0x09C4,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_6M_30fps",
		.regs = imx_STILL_6M_30fps,
		.width = 3280,
		.height = 1852,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D66,
				 .lines_per_frame = 0x09C4,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_8M_30fps",
		.regs = imx_STILL_8M_30fps,
		.width = 3280,
		.height = 2464,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D66,
				 .lines_per_frame = 0x09C4,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
};

struct imx_resolution imx175_res_still[] = {
	{
		.desc = "CIF_strong_dvs_30fps",
		.regs = imx_CIF_strong_dvs_30fps,
		.width = 368,
		.height = 304,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x11DB,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 261000,
	},
	{
		.desc = "VGA_strong_dvs_15fps",
		.regs = imx_VGA_strong_dvs_15fps,
		.width = 820,
		.height = 616,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1C86,
				 .lines_per_frame = 0x079E,
			},
			{
			}
		},
		.mipi_freq = 261500,
	},
	{
		.desc = "imx_STILL_720p_15fps",
		.regs = imx_STILL_720p_15fps,
		.width = 1568,
		.height = 876,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1838,
				 .lines_per_frame = 0x08CA,
			},
			{
			}
		},
		.mipi_freq = 261500,
	},
	{
		.desc = "STILL_2M_15fps",
		.regs = imx_STILL_2M_15fps,
		.width = 1640,
		.height = 1232,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1646,
				 .lines_per_frame = 0x0BB8,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "1080p_strong_dvs_15fps",
		.regs = imx_1080p_no_dvs_15fps,
		.width = 1940,
		.height = 1092,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x189C,
				 .lines_per_frame = 0x09A6,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
	{
		.desc = "STILL_3M_15fps",
		.regs = imx_STILL_3M_15fps,
		.width = 2064,
		.height = 1552,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1646,
				 .lines_per_frame = 0x0BB8,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_5M_15fps",
		.regs = imx_STILL_5M_15fps,
		.width = 2576,
		.height = 1936,
		.fps = 15,
		.pixels_per_line = 0x1646, /* consistent with regs arrays */
		.lines_per_frame = 0x0BB8, /* consistent with regs arrays */
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1646,
				 .lines_per_frame = 0x0BB8,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_6M_15fps",
		.regs = imx_STILL_6M_15fps,
		.width = 3280,
		.height = 1852,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1646,
				 .lines_per_frame = 0x0BB8,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
	{
		.desc = "STILL_8M_15fps",
		.regs = imx_STILL_8M_15fps,
		.width = 3280,
		.height = 2464,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 15,
				 .pixels_per_line = 0x1646,
				 .lines_per_frame = 0x0BB8,
			},
			{
			}
		},
		.mipi_freq = 320000,
	},
};

struct imx_resolution imx175_res_video[] = {
	{
		.desc = "QCIF_strong_dvs_30fps",
		.regs = imx_QCIF_strong_dvs_30fps,
		.width = 216,
		.height = 176,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D70,
				 .lines_per_frame = 0x0548,
			},
			{
			}
		},
		.mipi_freq = 174500,
	},
	{
		.desc =	"QVGA_strong_dvs_30fps",
		.regs = imx_QVGA_strong_dvs_30fps,
		.width = 408,
		.height = 308,
		.bin_factor_x =	4,
		.bin_factor_y =	4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D70,
				 .lines_per_frame = 0x0548,
			},
			{
			}
		},
		.mipi_freq = 174500,
	},
	{
		.desc = "VGA_strong_dvs_30fps",
		.regs = imx_VGA_strong_dvs_30fps,
		.width = 820,
		.height = 616,
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x1194,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 261500,
	},
	{
		.desc = "720p_strong_dvs_30fps",
		.regs = imx_720p_strong_dvs_30fps,
		.width = 1552,
		.height = 880,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x139C,
				 .lines_per_frame = 0x0600,
			},
			{
				 .fps = 60,
				 .pixels_per_line = 0xD70,
				 .lines_per_frame = 0x444,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
	{
		.desc = "480p_strong_dvs_30fps",
		.regs = imx_480p_strong_dvs_30fps,
		.width = 880,
		.height = 592,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x139C,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
	{
		.desc = "WVGA_strong_dvs_30fps",
		.regs = imx_WVGA_strong_dvs_30fps,
		.width = 1640,
		.height = 1024,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x139C,
				 .lines_per_frame = 0x0600,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
	{
		.desc = "1080p_strong_dvs_30fps",
		.regs = imx_1080p_strong_dvs_30fps,
		.width = 2320,
		.height = 1312,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x11C6,
				 .lines_per_frame = 0x06A4,
			},
			{
			}
		},
		.mipi_freq = 292500,
	},
};

#endif
