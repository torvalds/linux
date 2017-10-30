/*
 * Support for Sony IMX camera sensor.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __IMX208_H__
#define __IMX208_H__
#include "common.h"

/********************** settings for imx from vendor*********************/
static struct imx_reg imx208_1080p_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0305, 0x02},    /* PREPLLCK DIV */
	{IMX_8BIT, 0x0307, 0x54},    /* PLL MPY */
	{IMX_8BIT, 0x303C, 0x3C},    /* PLL oscillation stable wait time */
	{IMX_8BIT, 0x30A4, 0x02},    /* Default */
	{IMX_8BIT, 0x0112, 0x0A},    /* CCP_data_format : RAW 10bit */
	{IMX_8BIT, 0x0113, 0x0A},    /* CCP_data_format :  RAW 10bit */
	{IMX_8BIT, 0x0340, 0x04},    /* frame length line [15:8] */
	{IMX_8BIT, 0x0341, 0xAA},    /* frame length line [7:0] */
	{IMX_8BIT, 0x0342, 0x08},    /* line length pck [15:8] */
	{IMX_8BIT, 0x0343, 0xC8},    /* line length pck [7:0] */
	{IMX_8BIT, 0x0344, 0x00},    /* x_addr_start[12:8] */
	{IMX_8BIT, 0x0345, 0x00},    /* x_addr_start[7:0] */
	{IMX_8BIT, 0x0346, 0x00},    /* y_addr_start[12:8] */
	{IMX_8BIT, 0x0347, 0x00},    /* y_addr_start[7:0] */
	{IMX_8BIT, 0x0348, 0x07},    /* x_addr_end [12:8] */
	{IMX_8BIT, 0x0349, 0x8F},    /* x_addr_end [7:0] */
	{IMX_8BIT, 0x034A, 0x04},    /* y_addr_end [12:8] */
	{IMX_8BIT, 0x034B, 0x47},    /* y_addr_end [7:0] */
	{IMX_8BIT, 0x034C, 0x07},    /* x_output_size [ 12:8] */
	{IMX_8BIT, 0x034D, 0x90},    /* x_output_size [7:0] */
	{IMX_8BIT, 0x034E, 0x04},    /* y_output_size [11:8] */
	{IMX_8BIT, 0x034F, 0x48},    /* y_output_size [7:0] */
	{IMX_8BIT, 0x0381, 0x01},    /* x_even_inc */
	{IMX_8BIT, 0x0383, 0x01},    /* x_odd_inc */
	{IMX_8BIT, 0x0385, 0x01},    /* y_even_inc */
	{IMX_8BIT, 0x0387, 0x01},    /* y_odd_inc */
	{IMX_8BIT, 0x3048, 0x00},    /* VMODEFDS  binning operation */
	{IMX_8BIT, 0x304E, 0x0A},    /* VTPXCK_DIV */
	{IMX_8BIT, 0x3050, 0x02},    /* OPSYCK_DIV */
	{IMX_8BIT, 0x309B, 0x00},    /* RGDAFDSUMEN */
	{IMX_8BIT, 0x30D5, 0x00},    /* HADDEN ( binning ) */
	{IMX_8BIT, 0x3301, 0x01},    /* RGLANESEL */
	{IMX_8BIT, 0x3318, 0x61},    /* MIPI Global Timing */
	{IMX_8BIT, 0x0202, 0x01},    /* coarse integration time */
	{IMX_8BIT, 0x0203, 0x90},    /* coarse integration time */
	{IMX_8BIT, 0x0205, 0x00},    /* ana global gain */

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx208_1296x736_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0305, 0x02},    /* PREPLLCK DIV */
	{IMX_8BIT, 0x0307, 0x54},    /* PLL MPY */
	{IMX_8BIT, 0x303C, 0x3C},    /* PLL oscillation stable wait time */
	{IMX_8BIT, 0x30A4, 0x02},    /* Default */
	{IMX_8BIT, 0x0112, 0x0A},    /* CCP_data_format : RAW 10bit */
	{IMX_8BIT, 0x0113, 0x0A},    /* CCP_data_format :  RAW 10bit */
	{IMX_8BIT, 0x0340, 0x04},    /* frame length line [15:8] */
	{IMX_8BIT, 0x0341, 0xAA},    /* frame length line [7:0] */
	{IMX_8BIT, 0x0342, 0x08},    /* line length pck [15:8] */
	{IMX_8BIT, 0x0343, 0xC8},    /* line length pck [7:0] */
	{IMX_8BIT, 0x0344, 0x01},    /* x_addr_start[12:8] */
	{IMX_8BIT, 0x0345, 0x40},    /* x_addr_start[7:0] */
	{IMX_8BIT, 0x0346, 0x00},    /* y_addr_start[12:8] */
	{IMX_8BIT, 0x0347, 0xB4},    /* y_addr_start[7:0] */
	{IMX_8BIT, 0x0348, 0x06},    /* x_addr_end [12:8] */
	{IMX_8BIT, 0x0349, 0x4F},    /* x_addr_end [7:0] */
	{IMX_8BIT, 0x034A, 0x03},    /* y_addr_end [12:8] */
	{IMX_8BIT, 0x034B, 0x93},    /* y_addr_end [7:0] */
	{IMX_8BIT, 0x034C, 0x05},    /* x_output_size [ 12:8] */
	{IMX_8BIT, 0x034D, 0x10},    /* x_output_size [7:0] */
	{IMX_8BIT, 0x034E, 0x02},    /* y_output_size [11:8] */
	{IMX_8BIT, 0x034F, 0xE0},    /* y_output_size [7:0] */
	{IMX_8BIT, 0x0381, 0x01},    /* x_even_inc */
	{IMX_8BIT, 0x0383, 0x01},    /* x_odd_inc */
	{IMX_8BIT, 0x0385, 0x01},    /* y_even_inc */
	{IMX_8BIT, 0x0387, 0x01},    /* y_odd_inc */
	{IMX_8BIT, 0x3048, 0x00},    /* VMODEFDS  binning operation */
	{IMX_8BIT, 0x304E, 0x0A},    /* VTPXCK_DIV */
	{IMX_8BIT, 0x3050, 0x02},    /* OPSYCK_DIV */
	{IMX_8BIT, 0x309B, 0x00},    /* RGDAFDSUMEN */
	{IMX_8BIT, 0x30D5, 0x00},    /* HADDEN ( binning ) */
	{IMX_8BIT, 0x3301, 0x01},    /* RGLANESEL */
	{IMX_8BIT, 0x3318, 0x61},    /* MIPI Global Timing */
	{IMX_8BIT, 0x0202, 0x01},    /* coarse integration time */
	{IMX_8BIT, 0x0203, 0x90},    /* coarse integration time */
	{IMX_8BIT, 0x0205, 0x00},    /* ana global gain */

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx208_1296x976_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0305, 0x02},    /* PREPLLCK DIV */
	{IMX_8BIT, 0x0307, 0x54},    /* PLL MPY */
	{IMX_8BIT, 0x303C, 0x3C},    /* PLL oscillation stable wait time */
	{IMX_8BIT, 0x30A4, 0x02},    /* Default */
	{IMX_8BIT, 0x0112, 0x0A},    /* CCP_data_format : RAW 10bit */
	{IMX_8BIT, 0x0113, 0x0A},    /* CCP_data_format :  RAW 10bit */
	{IMX_8BIT, 0x0340, 0x04},    /* frame length line [15:8] */
	{IMX_8BIT, 0x0341, 0xAA},    /* frame length line [7:0] */
	{IMX_8BIT, 0x0342, 0x08},    /* line length pck [15:8] */
	{IMX_8BIT, 0x0343, 0xC8},    /* line length pck [7:0] */
	{IMX_8BIT, 0x0344, 0x01},    /* x_addr_start[12:8] */
	{IMX_8BIT, 0x0345, 0x40},    /* x_addr_start[7:0] */
	{IMX_8BIT, 0x0346, 0x00},    /* y_addr_start[12:8] */
	{IMX_8BIT, 0x0347, 0x3C},    /* y_addr_start[7:0] */
	{IMX_8BIT, 0x0348, 0x06},    /* x_addr_end [12:8] */
	{IMX_8BIT, 0x0349, 0x4F},    /* x_addr_end [7:0] */
	{IMX_8BIT, 0x034A, 0x04},    /* y_addr_end [12:8] */
	{IMX_8BIT, 0x034B, 0x0B},    /* y_addr_end [7:0] */
	{IMX_8BIT, 0x034C, 0x05},    /* x_output_size [ 12:8] */
	{IMX_8BIT, 0x034D, 0x10},    /* x_output_size [7:0] */
	{IMX_8BIT, 0x034E, 0x03},    /* y_output_size [11:8] */
	{IMX_8BIT, 0x034F, 0xD0},    /* y_output_size [7:0] */
	{IMX_8BIT, 0x0381, 0x01},    /* x_even_inc */
	{IMX_8BIT, 0x0383, 0x01},    /* x_odd_inc */
	{IMX_8BIT, 0x0385, 0x01},    /* y_even_inc */
	{IMX_8BIT, 0x0387, 0x01},    /* y_odd_inc */
	{IMX_8BIT, 0x3048, 0x00},    /* VMODEFDS  binning operation */
	{IMX_8BIT, 0x304E, 0x0A},    /* VTPXCK_DIV */
	{IMX_8BIT, 0x3050, 0x02},    /* OPSYCK_DIV */
	{IMX_8BIT, 0x309B, 0x00},    /* RGDAFDSUMEN */
	{IMX_8BIT, 0x30D5, 0x00},    /* HADDEN ( binning ) */
	{IMX_8BIT, 0x3301, 0x01},    /* RGLANESEL */
	{IMX_8BIT, 0x3318, 0x61},    /* MIPI Global Timing */
	{IMX_8BIT, 0x0202, 0x01},    /* coarse integration time */
	{IMX_8BIT, 0x0203, 0x90},    /* coarse integration time */
	{IMX_8BIT, 0x0205, 0x00},    /* ana global gain */

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx208_336x256_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0305, 0x02},    /* PREPLLCK DIV */
	{IMX_8BIT, 0x0307, 0x54},    /* PLL MPY */
	{IMX_8BIT, 0x303C, 0x3C},    /* PLL oscillation stable wait time */
	{IMX_8BIT, 0x30A4, 0x02},    /* Default */
	{IMX_8BIT, 0x0112, 0x0A},    /* CCP_data_format : RAW 10bit */
	{IMX_8BIT, 0x0113, 0x0A},    /* CCP_data_format :  RAW 10bit */
	{IMX_8BIT, 0x0340, 0x04},    /* frame length line [15:8] */
	{IMX_8BIT, 0x0341, 0xAA},    /* frame length line [7:0] */
	{IMX_8BIT, 0x0342, 0x08},    /* line length pck [15:8] */
	{IMX_8BIT, 0x0343, 0xC8},    /* line length pck [7:0] */
	{IMX_8BIT, 0x0344, 0x02},    /* x_addr_start[12:8] */
	{IMX_8BIT, 0x0345, 0x78},    /* x_addr_start[7:0] */
	{IMX_8BIT, 0x0346, 0x01},    /* y_addr_start[12:8] */
	{IMX_8BIT, 0x0347, 0x24},    /* y_addr_start[7:0] */
	{IMX_8BIT, 0x0348, 0x05},    /* x_addr_end [12:8] */
	{IMX_8BIT, 0x0349, 0x17},    /* x_addr_end [7:0] */
	{IMX_8BIT, 0x034A, 0x03},    /* y_addr_end [12:8] */
	{IMX_8BIT, 0x034B, 0x23},    /* y_addr_end [7:0] */
	{IMX_8BIT, 0x034C, 0x01},    /* x_output_size [ 12:8] */
	{IMX_8BIT, 0x034D, 0x50},    /* x_output_size [7:0] */
	{IMX_8BIT, 0x034E, 0x01},    /* y_output_size [11:8] */
	{IMX_8BIT, 0x034F, 0x00},    /* y_output_size [7:0] */
	{IMX_8BIT, 0x0381, 0x01},    /* x_even_inc */
	{IMX_8BIT, 0x0383, 0x03},    /* x_odd_inc */
	{IMX_8BIT, 0x0385, 0x01},    /* y_even_inc */
	{IMX_8BIT, 0x0387, 0x03},    /* y_odd_inc */
	{IMX_8BIT, 0x3048, 0x01},    /* VMODEFDS  binning operation */
	{IMX_8BIT, 0x304E, 0x0A},    /* VTPXCK_DIV */
	{IMX_8BIT, 0x3050, 0x02},    /* OPSYCK_DIV */
	{IMX_8BIT, 0x309B, 0x00},    /* RGDAFDSUMEN */
	{IMX_8BIT, 0x30D5, 0x03},    /* HADDEN ( binning ) */
	{IMX_8BIT, 0x3301, 0x01},    /* RGLANESEL */
	{IMX_8BIT, 0x3318, 0x66},    /* MIPI Global Timing */
	{IMX_8BIT, 0x0202, 0x01},    /* coarse integration time */
	{IMX_8BIT, 0x0203, 0x90},    /* coarse integration time */
	{IMX_8BIT, 0x0205, 0x00},    /* ana global gain */

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx208_192x160_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0305, 0x02},    /* PREPLLCK DIV */
	{IMX_8BIT, 0x0307, 0x54},    /* PLL MPY */
	{IMX_8BIT, 0x303C, 0x3C},    /* PLL oscillation stable wait time */
	{IMX_8BIT, 0x30A4, 0x02},    /* Default */
	{IMX_8BIT, 0x0112, 0x0A},    /* CCP_data_format : RAW 10bit */
	{IMX_8BIT, 0x0113, 0x0A},    /* CCP_data_format :  RAW 10bit */
	{IMX_8BIT, 0x0340, 0x04},    /* frame length line [15:8] */
	{IMX_8BIT, 0x0341, 0xAA},    /* frame length line [7:0] */
	{IMX_8BIT, 0x0342, 0x08},    /* line length pck [15:8] */
	{IMX_8BIT, 0x0343, 0xC8},    /* line length pck [7:0] */
	{IMX_8BIT, 0x0344, 0x02},    /* x_addr_start[12:8] */
	{IMX_8BIT, 0x0345, 0x48},    /* x_addr_start[7:0] */
	{IMX_8BIT, 0x0346, 0x00},    /* y_addr_start[12:8] */
	{IMX_8BIT, 0x0347, 0xE4},    /* y_addr_start[7:0] */
	{IMX_8BIT, 0x0348, 0x05},    /* x_addr_end [12:8] */
	{IMX_8BIT, 0x0349, 0x47},    /* x_addr_end [7:0] */
	{IMX_8BIT, 0x034A, 0x03},    /* y_addr_end [12:8] */
	{IMX_8BIT, 0x034B, 0x63},    /* y_addr_end [7:0] */
	{IMX_8BIT, 0x034C, 0x00},    /* x_output_size [ 12:8] */
	{IMX_8BIT, 0x034D, 0xC0},    /* x_output_size [7:0] */
	{IMX_8BIT, 0x034E, 0x00},    /* y_output_size [11:8] */
	{IMX_8BIT, 0x034F, 0xA0},    /* y_output_size [7:0] */
	{IMX_8BIT, 0x0381, 0x03},    /* x_even_inc */
	{IMX_8BIT, 0x0383, 0x05},    /* x_odd_inc */
	{IMX_8BIT, 0x0385, 0x03},    /* y_even_inc */
	{IMX_8BIT, 0x0387, 0x05},    /* y_odd_inc */
	{IMX_8BIT, 0x3048, 0x01},    /* VMODEFDS  binning operation */
	{IMX_8BIT, 0x304E, 0x0A},    /* VTPXCK_DIV */
	{IMX_8BIT, 0x3050, 0x02},    /* OPSYCK_DIV */
	{IMX_8BIT, 0x309B, 0x00},    /* RGDAFDSUMEN */
	{IMX_8BIT, 0x30D5, 0x03},    /* HADDEN ( binning ) */
	{IMX_8BIT, 0x3301, 0x11},    /* RGLANESEL */
	{IMX_8BIT, 0x3318, 0x74},    /* MIPI Global Timing */
	{IMX_8BIT, 0x0202, 0x01},    /* coarse integration time */
	{IMX_8BIT, 0x0203, 0x90},    /* coarse integration time */
	{IMX_8BIT, 0x0205, 0x00},    /* ana global gain */

	{IMX_TOK_TERM, 0, 0},
};
/********************** settings for imx - reference *********************/
static struct imx_reg const imx208_init_settings[] = {
	{ IMX_TOK_TERM, 0, 0}
};

struct imx_resolution imx208_res_preview[] = {
	{
		.desc = "imx208_1080p_30fps",
		.regs = imx208_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x976_30fps",
		.regs = imx208_1296x976_30fps,
		.width = 1296,
		.height = 976,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x736_30fps",
		.regs = imx208_1296x736_30fps,
		.width = 1296,
		.height = 736,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_336x256_30fps",
		.regs = imx208_336x256_30fps,
		.width = 336,
		.height = 256,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 201600,
	},
	{
		.desc = "imx208_192x160_30fps",
		.regs = imx208_192x160_30fps,
		.width = 192,
		.height = 160,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 100800,
	},
};

struct imx_resolution imx208_res_still[] = {
	{
		.desc = "imx208_1080p_30fps",
		.regs = imx208_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x976_30fps",
		.regs = imx208_1296x976_30fps,
		.width = 1296,
		.height = 976,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x736_30fps",
		.regs = imx208_1296x736_30fps,
		.width = 1296,
		.height = 736,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_336x256_30fps",
		.regs = imx208_336x256_30fps,
		.width = 336,
		.height = 256,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 201600,
	},
	{
		.desc = "imx208_192x160_30fps",
		.regs = imx208_192x160_30fps,
		.width = 192,
		.height = 160,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 100800,
	},
};

struct imx_resolution imx208_res_video[] = {
	{
		.desc = "imx208_1080p_30fps",
		.regs = imx208_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x976_30fps",
		.regs = imx208_1296x976_30fps,
		.width = 1296,
		.height = 976,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_1296x736_30fps",
		.regs = imx208_1296x736_30fps,
		.width = 1296,
		.height = 736,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 403200,
	},
	{
		.desc = "imx208_336x256_30fps",
		.regs = imx208_336x256_30fps,
		.width = 336,
		.height = 256,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 201600,
	},
	{
		.desc = "imx208_192x160_30fps",
		.regs = imx208_192x160_30fps,
		.width = 192,
		.height = 160,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08C8,
				.lines_per_frame = 0x04AA,
			},
			{
			}
		},
		.bin_factor_x = 4,
		.bin_factor_y = 4,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 100800,
	},
};
#endif

