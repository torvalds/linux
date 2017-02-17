#ifndef __IMX219_H__
#define __IMX219_H__
#include "common.h"

#define IMX219_FRAME_LENGTH_LINES		0x0160
#define IMX219_LINE_LENGTH_PIXELS		0x0162
#define IMX219_HORIZONTAL_START_H		0x0164
#define IMX219_VERTICAL_START_H			0x0168
#define IMX219_HORIZONTAL_END_H			0x0166
#define IMX219_VERTICAL_END_H			0x016A
#define IMX219_HORIZONTAL_OUTPUT_SIZE_H	0x016c
#define IMX219_VERTICAL_OUTPUT_SIZE_H	0x016E
#define IMX219_COARSE_INTEGRATION_TIME	0x015A
#define IMX219_IMG_ORIENTATION			0x0172
#define IMX219_GLOBAL_GAIN				0x0157
#define IMX219_DGC_ADJ					0x0158

#define IMX219_DGC_LEN					4

/************************** settings for imx *************************/
static struct imx_reg const imx219_STILL_8M_30fps[] = {
	{IMX_8BIT, 0x30EB, 0x05}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x0C}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x300A, 0xFF}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x300B, 0xFF}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x05}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x09}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x0114, 0x03}, /*CSI_LANE_MODE[1:0}*/
	{IMX_8BIT, 0x0128, 0x00}, /*DPHY_CNTRL*/
	{IMX_8BIT, 0x012A, 0x13}, /*EXCK_FREQ[15:8]*/
	{IMX_8BIT, 0x012B, 0x34}, /*EXCK_FREQ[7:0]*/
	{IMX_8BIT, 0x0160, 0x0A}, /*FRM_LENGTH_A[15:8]*/
	{IMX_8BIT, 0x0161, 0x94}, /*FRM_LENGTH_A[7:0]*/
	{IMX_8BIT, 0x0162, 0x0D}, /*LINE_LENGTH_A[15:8]*/
	{IMX_8BIT, 0x0163, 0x78}, /*LINE_LENGTH_A[7:0]*/
	{IMX_8BIT, 0x0164, 0x00}, /*X_ADD_STA_A[11:8]*/
	{IMX_8BIT, 0x0165, 0x00}, /*X_ADD_STA_A[7:0]*/
	{IMX_8BIT, 0x0166, 0x0C}, /*X_ADD_END_A[11:8]*/
	{IMX_8BIT, 0x0167, 0xCF}, /*X_ADD_END_A[7:0]*/
	{IMX_8BIT, 0x0168, 0x00}, /*Y_ADD_STA_A[11:8]*/
	{IMX_8BIT, 0x0169, 0x00}, /*Y_ADD_STA_A[7:0]*/
	{IMX_8BIT, 0x016A, 0x09}, /*Y_ADD_END_A[11:8]*/
	{IMX_8BIT, 0x016B, 0x9F}, /*Y_ADD_END_A[7:0]*/
	{IMX_8BIT, 0x016C, 0x0C}, /*X_OUTPUT_SIZE_A[11:8]*/
	{IMX_8BIT, 0x016D, 0xD0}, /*X_OUTPUT_SIZE_A[7:0]*/
	{IMX_8BIT, 0x016E, 0x09}, /*Y_OUTPUT_SIZE_A[11:8]*/
	{IMX_8BIT, 0x016F, 0xA0}, /*Y_OUTPUT_SIZE_A[7:0]*/
	{IMX_8BIT, 0x0170, 0x01}, /*X_ODD_INC_A[2:0]*/
	{IMX_8BIT, 0x0171, 0x01}, /*Y_ODD_INC_A[2:0]*/
	{IMX_8BIT, 0x0174, 0x00}, /*BINNING_MODE_H_A*/
	{IMX_8BIT, 0x0175, 0x00}, /*BINNING_MODE_V_A*/
	{IMX_8BIT, 0x018C, 0x0A}, /*CSI_DATA_FORMAT_A[15:8]*/
	{IMX_8BIT, 0x018D, 0x0A}, /*CSI_DATA_FORMAT_A[7:0]*/
	{IMX_8BIT, 0x0301, 0x05}, /*VTPXCK_DIV*/
	{IMX_8BIT, 0x0303, 0x01}, /*VTSYCK_DIV*/
	{IMX_8BIT, 0x0304, 0x02}, /*PREPLLCK_VT_DIV[3:0]*/
	{IMX_8BIT, 0x0305, 0x02}, /*PREPLLCK_OP_DIV[3:0]*/
	{IMX_8BIT, 0x0306, 0x00}, /*PLL_VT_MPY[10:8]*/
	{IMX_8BIT, 0x0307, 0x49}, /*PLL_VT_MPY[7:0]*/
	{IMX_8BIT, 0x0309, 0x0A}, /*OPPXCK_DIV[4:0]*/
	{IMX_8BIT, 0x030B, 0x01}, /*OPSYCK_DIV*/
	{IMX_8BIT, 0x030C, 0x00}, /*PLL_OP_MPY[10:8]*/
	{IMX_8BIT, 0x030D, 0x4C}, /*PLL_OP_MPY[7:0]*/
	{IMX_8BIT, 0x4767, 0x0F}, /*CIS Tuning*/
	{IMX_8BIT, 0x4750, 0x14}, /*CIS Tuning*/
	{IMX_8BIT, 0x47B4, 0x14}, /*CIS Tuning*/
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx219_STILL_6M_30fps[] = {
	{IMX_8BIT, 0x30EB, 0x05}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x0C}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x300A, 0xFF}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x300B, 0xFF}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x05}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x30EB, 0x09}, /*Access Code for address over 0x3000*/
	{IMX_8BIT, 0x0114, 0x03}, /*CSI_LANE_MODE[1:0}*/
	{IMX_8BIT, 0x0128, 0x00}, /*DPHY_CNTRL*/
	{IMX_8BIT, 0x012A, 0x13}, /*EXCK_FREQ[15:8]*/
	{IMX_8BIT, 0x012B, 0x34}, /*EXCK_FREQ[7:0]*/
	{IMX_8BIT, 0x0160, 0x07}, /*FRM_LENGTH_A[15:8]*/
	{IMX_8BIT, 0x0161, 0x64}, /*FRM_LENGTH_A[7:0]*/
	{IMX_8BIT, 0x0162, 0x0D}, /*LINE_LENGTH_A[15:8]*/
	{IMX_8BIT, 0x0163, 0x78}, /*LINE_LENGTH_A[7:0]*/
	{IMX_8BIT, 0x0164, 0x00}, /*X_ADD_STA_A[11:8]*/
	{IMX_8BIT, 0x0165, 0x00}, /*X_ADD_STA_A[7:0]*/
	{IMX_8BIT, 0x0166, 0x0C}, /*X_ADD_END_A[11:8]*/
	{IMX_8BIT, 0x0167, 0xCF}, /*X_ADD_END_A[7:0]*/
	{IMX_8BIT, 0x0168, 0x01}, /*Y_ADD_STA_A[11:8]*/
	{IMX_8BIT, 0x0169, 0x32}, /*Y_ADD_STA_A[7:0]*/
	{IMX_8BIT, 0x016A, 0x08}, /*Y_ADD_END_A[11:8]*/
	{IMX_8BIT, 0x016B, 0x6D}, /*Y_ADD_END_A[7:0]*/
	{IMX_8BIT, 0x016C, 0x0C}, /*X_OUTPUT_SIZE_A[11:8]*/
	{IMX_8BIT, 0x016D, 0xD0}, /*X_OUTPUT_SIZE_A[7:0]*/
	{IMX_8BIT, 0x016E, 0x07}, /*Y_OUTPUT_SIZE_A[11:8]*/
	{IMX_8BIT, 0x016F, 0x3C}, /*Y_OUTPUT_SIZE_A[7:0]*/
	{IMX_8BIT, 0x0170, 0x01}, /*X_ODD_INC_A[2:0]*/
	{IMX_8BIT, 0x0171, 0x01}, /*Y_ODD_INC_A[2:0]*/
	{IMX_8BIT, 0x0174, 0x00}, /*BINNING_MODE_H_A*/
	{IMX_8BIT, 0x0175, 0x00}, /*BINNING_MODE_V_A*/
	{IMX_8BIT, 0x018C, 0x0A}, /*CSI_DATA_FORMAT_A[15:8]*/
	{IMX_8BIT, 0x018D, 0x0A}, /*CSI_DATA_FORMAT_A[7:0]*/
	{IMX_8BIT, 0x0301, 0x05}, /*VTPXCK_DIV*/
	{IMX_8BIT, 0x0303, 0x01}, /*VTSYCK_DIV*/
	{IMX_8BIT, 0x0304, 0x02}, /*PREPLLCK_VT_DIV[3:0]*/
	{IMX_8BIT, 0x0305, 0x02}, /*PREPLLCK_OP_DIV[3:0]*/
	{IMX_8BIT, 0x0306, 0x00}, /*PLL_VT_MPY[10:8]*/
	{IMX_8BIT, 0x0307, 0x33}, /*PLL_VT_MPY[7:0]*/
	{IMX_8BIT, 0x0309, 0x0A}, /*OPPXCK_DIV[4:0]*/
	{IMX_8BIT, 0x030B, 0x01}, /*OPSYCK_DIV*/
	{IMX_8BIT, 0x030C, 0x00}, /*PLL_OP_MPY[10:8]*/
	{IMX_8BIT, 0x030D, 0x36}, /*PLL_OP_MPY[7:0]*/
	{IMX_8BIT, 0x4767, 0x0F}, /*CIS Tuning*/
	{IMX_8BIT, 0x4750, 0x14}, /*CIS Tuning*/
	{IMX_8BIT, 0x47B4, 0x14}, /*CIS Tuning*/
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx219_init_settings[] = {
	{IMX_TOK_TERM, 0, 0}
};

struct imx_resolution imx219_res_preview[] = {
	{
		.desc = "STILL_6M_30fps",
		.regs = imx219_STILL_6M_30fps,
		.width = 3280,
		.height = 1852,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D78,
				 .lines_per_frame = 0x0764,
			},
			{
			}
		},
		.mipi_freq = 259000,
	},
	{
		.desc = "STILL_8M_30fps",
		.regs = imx219_STILL_8M_30fps,
		.width = 3280,
		.height = 2464,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D78,
				 .lines_per_frame = 0x0A94,
			},
			{
			}
		},
		.mipi_freq = 365000,
	},
};

struct imx_resolution imx219_res_still[] = {
	{
		.desc = "STILL_6M_30fps",
		.regs = imx219_STILL_6M_30fps,
		.width = 3280,
		.height = 1852,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D78,
				 .lines_per_frame = 0x0764,
			},
			{
			}
		},
		.mipi_freq = 259000,
	},
	{
		.desc = "STILL_8M_30fps",
		.regs = imx219_STILL_8M_30fps,
		.width = 3280,
		.height = 2464,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D78,
				 .lines_per_frame = 0x0A94,
			},
			{
			}
		},
		.mipi_freq = 365000,
	},
};

struct imx_resolution imx219_res_video[] = {
	{
		.desc = "STILL_6M_30fps",
		.regs = imx219_STILL_6M_30fps,
		.width = 3280,
		.height = 1852,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0D78,
				 .lines_per_frame = 0x0764,
			},
			{
			}
		},
		.mipi_freq = 259000,
	},
};

#endif
