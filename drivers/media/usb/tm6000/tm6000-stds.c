/*
 *  tm6000-stds.c - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 *  Copyright (C) 2007 Mauro Carvalho Chehab
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "tm6000.h"
#include "tm6000-regs.h"

static unsigned int tm6010_a_mode;
module_param(tm6010_a_mode, int, 0644);
MODULE_PARM_DESC(tm6010_a_mode, "set tm6010 sif audio mode");

struct tm6000_reg_settings {
	unsigned char req;
	unsigned char reg;
	unsigned char value;
};


struct tm6000_std_settings {
	v4l2_std_id id;
	struct tm6000_reg_settings *common;
};

static struct tm6000_reg_settings composite_pal_m[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x04 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x00 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x83 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x0a },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe0 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x20 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings composite_pal_nc[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x36 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x02 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x91 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x1f },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0x0c },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x8c },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x2c },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings composite_pal[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x32 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x02 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x25 },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0xd5 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x63 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0x50 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x8c },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x2c },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings composite_secam[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x38 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x02 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x24 },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x92 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xe8 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xed },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x8c },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x2c },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x2c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x18 },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0xff },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings composite_ntsc[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x00 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0f },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x00 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x8b },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xa2 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe9 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x1c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdd },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_std_settings composite_stds[] = {
	{ .id = V4L2_STD_PAL_M, .common = composite_pal_m, },
	{ .id = V4L2_STD_PAL_Nc, .common = composite_pal_nc, },
	{ .id = V4L2_STD_PAL, .common = composite_pal, },
	{ .id = V4L2_STD_SECAM, .common = composite_secam, },
	{ .id = V4L2_STD_NTSC, .common = composite_ntsc, },
};

static struct tm6000_reg_settings svideo_pal_m[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x05 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x04 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x83 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x0a },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe0 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings svideo_pal_nc[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x37 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x04 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x91 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x1f },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0x0c },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings svideo_pal[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x33 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x04 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x30 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x25 },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0xd5 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0x63 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0x50 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x8c },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x2a },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x0c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x52 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdc },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings svideo_secam[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x39 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0e },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x03 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x31 },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x24 },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x92 },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xe8 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xed },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x8c },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x2a },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0xc1 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x2c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x18 },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0xff },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_reg_settings svideo_ntsc[] = {
	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x01 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x0f },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x03 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x30 },
	{ TM6010_REQ07_R17_HLOOP_MAXSTATE, 0x8b },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x8b },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xa2 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe9 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x1c },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_R83_CHROMA_LOCK_CONFIG, 0x6f },
	{ TM6010_REQ07_R04_LUMA_HAGC_CONTROL, 0xdd },
	{ TM6010_REQ07_R0D_CHROMA_KILL_LEVEL, 0x07 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },
	{ 0, 0, 0 }
};

static struct tm6000_std_settings svideo_stds[] = {
	{ .id = V4L2_STD_PAL_M, .common = svideo_pal_m, },
	{ .id = V4L2_STD_PAL_Nc, .common = svideo_pal_nc, },
	{ .id = V4L2_STD_PAL, .common = svideo_pal, },
	{ .id = V4L2_STD_SECAM, .common = svideo_secam, },
	{ .id = V4L2_STD_NTSC, .common = svideo_ntsc, },
};

static int tm6000_set_audio_std(struct tm6000_core *dev)
{
	uint8_t areg_02 = 0x04; /* GC1 Fixed gain 0dB */
	uint8_t areg_05 = 0x01; /* Auto 4.5 = M Japan, Auto 6.5 = DK */
	uint8_t areg_06 = 0x02; /* Auto de-emphasis, mannual channel mode */

	if (dev->radio) {
		tm6000_set_reg(dev, TM6010_REQ08_R01_A_INIT, 0x00);
		tm6000_set_reg(dev, TM6010_REQ08_R02_A_FIX_GAIN_CTRL, 0x04);
		tm6000_set_reg(dev, TM6010_REQ08_R03_A_AUTO_GAIN_CTRL, 0x00);
		tm6000_set_reg(dev, TM6010_REQ08_R04_A_SIF_AMP_CTRL, 0x80);
		tm6000_set_reg(dev, TM6010_REQ08_R05_A_STANDARD_MOD, 0x0c);
		/* set mono or stereo */
		if (dev->amode == V4L2_TUNER_MODE_MONO)
			tm6000_set_reg(dev, TM6010_REQ08_R06_A_SOUND_MOD, 0x00);
		else if (dev->amode == V4L2_TUNER_MODE_STEREO)
			tm6000_set_reg(dev, TM6010_REQ08_R06_A_SOUND_MOD, 0x02);
		tm6000_set_reg(dev, TM6010_REQ08_R09_A_MAIN_VOL, 0x18);
		tm6000_set_reg(dev, TM6010_REQ08_R0C_A_ASD_THRES2, 0x0a);
		tm6000_set_reg(dev, TM6010_REQ08_R0D_A_AMD_THRES, 0x40);
		tm6000_set_reg(dev, TM6010_REQ08_RF1_AADC_POWER_DOWN, 0xfe);
		tm6000_set_reg(dev, TM6010_REQ08_R1E_A_GAIN_DEEMPH_OUT, 0x13);
		tm6000_set_reg(dev, TM6010_REQ08_R01_A_INIT, 0x80);
		tm6000_set_reg(dev, TM6010_REQ07_RFE_POWER_DOWN, 0xff);
		return 0;
	}

	/*
	 * STD/MN shouldn't be affected by tm6010_a_mode, as there's just one
	 * audio standard for each V4L2_STD type.
	 */
	if ((dev->norm & V4L2_STD_NTSC) == V4L2_STD_NTSC_M_KR) {
		areg_05 |= 0x04;
	} else if ((dev->norm & V4L2_STD_NTSC) == V4L2_STD_NTSC_M_JP) {
		areg_05 |= 0x43;
	} else if (dev->norm & V4L2_STD_MN) {
		areg_05 |= 0x22;
	} else switch (tm6010_a_mode) {
	/* auto */
	case 0:
		if ((dev->norm & V4L2_STD_SECAM) == V4L2_STD_SECAM_L)
			areg_05 |= 0x00;
		else	/* Other PAL/SECAM standards */
			areg_05 |= 0x10;
		break;
	/* A2 */
	case 1:
		if (dev->norm & V4L2_STD_DK)
			areg_05 = 0x09;
		else
			areg_05 = 0x05;
		break;
	/* NICAM */
	case 2:
		if (dev->norm & V4L2_STD_DK) {
			areg_05 = 0x06;
		} else if (dev->norm & V4L2_STD_PAL_I) {
			areg_05 = 0x08;
		} else if (dev->norm & V4L2_STD_SECAM_L) {
			areg_05 = 0x0a;
			areg_02 = 0x02;
		} else {
			areg_05 = 0x07;
		}
		break;
	/* other */
	case 3:
		if (dev->norm & V4L2_STD_DK) {
			areg_05 = 0x0b;
		} else {
			areg_05 = 0x02;
		}
		break;
	}

	tm6000_set_reg(dev, TM6010_REQ08_R01_A_INIT, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R02_A_FIX_GAIN_CTRL, areg_02);
	tm6000_set_reg(dev, TM6010_REQ08_R03_A_AUTO_GAIN_CTRL, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R04_A_SIF_AMP_CTRL, 0xa0);
	tm6000_set_reg(dev, TM6010_REQ08_R05_A_STANDARD_MOD, areg_05);
	tm6000_set_reg(dev, TM6010_REQ08_R06_A_SOUND_MOD, areg_06);
	tm6000_set_reg(dev, TM6010_REQ08_R07_A_LEFT_VOL, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R08_A_RIGHT_VOL, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R09_A_MAIN_VOL, 0x08);
	tm6000_set_reg(dev, TM6010_REQ08_R0A_A_I2S_MOD, 0x91);
	tm6000_set_reg(dev, TM6010_REQ08_R0B_A_ASD_THRES1, 0x20);
	tm6000_set_reg(dev, TM6010_REQ08_R0C_A_ASD_THRES2, 0x12);
	tm6000_set_reg(dev, TM6010_REQ08_R0D_A_AMD_THRES, 0x20);
	tm6000_set_reg(dev, TM6010_REQ08_R0E_A_MONO_THRES1, 0xf0);
	tm6000_set_reg(dev, TM6010_REQ08_R0F_A_MONO_THRES2, 0x80);
	tm6000_set_reg(dev, TM6010_REQ08_R10_A_MUTE_THRES1, 0xc0);
	tm6000_set_reg(dev, TM6010_REQ08_R11_A_MUTE_THRES2, 0x80);
	tm6000_set_reg(dev, TM6010_REQ08_R12_A_AGC_U, 0x12);
	tm6000_set_reg(dev, TM6010_REQ08_R13_A_AGC_ERR_T, 0xfe);
	tm6000_set_reg(dev, TM6010_REQ08_R14_A_AGC_GAIN_INIT, 0x20);
	tm6000_set_reg(dev, TM6010_REQ08_R15_A_AGC_STEP_THR, 0x14);
	tm6000_set_reg(dev, TM6010_REQ08_R16_A_AGC_GAIN_MAX, 0xfe);
	tm6000_set_reg(dev, TM6010_REQ08_R17_A_AGC_GAIN_MIN, 0x01);
	tm6000_set_reg(dev, TM6010_REQ08_R18_A_TR_CTRL, 0xa0);
	tm6000_set_reg(dev, TM6010_REQ08_R19_A_FH_2FH_GAIN, 0x32);
	tm6000_set_reg(dev, TM6010_REQ08_R1A_A_NICAM_SER_MAX, 0x64);
	tm6000_set_reg(dev, TM6010_REQ08_R1B_A_NICAM_SER_MIN, 0x20);
	tm6000_set_reg(dev, REQ_08_SET_GET_AVREG_BIT, 0x1c, 0x00);
	tm6000_set_reg(dev, REQ_08_SET_GET_AVREG_BIT, 0x1d, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R1E_A_GAIN_DEEMPH_OUT, 0x13);
	tm6000_set_reg(dev, TM6010_REQ08_R1F_A_TEST_INTF_SEL, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R20_A_TEST_PIN_SEL, 0x00);
	tm6000_set_reg(dev, TM6010_REQ08_R01_A_INIT, 0x80);

	return 0;
}

void tm6000_get_std_res(struct tm6000_core *dev)
{
	/* Currently, those are the only supported resoltions */
	if (dev->norm & V4L2_STD_525_60)
		dev->height = 480;
	else
		dev->height = 576;

	dev->width = 720;
}

static int tm6000_load_std(struct tm6000_core *dev, struct tm6000_reg_settings *set)
{
	int i, rc;

	/* Load board's initialization table */
	for (i = 0; set[i].req; i++) {
		rc = tm6000_set_reg(dev, set[i].req, set[i].reg, set[i].value);
		if (rc < 0) {
			printk(KERN_ERR "Error %i while setting req %d, reg %d to value %d\n",
			       rc, set[i].req, set[i].reg, set[i].value);
			return rc;
		}
	}

	return 0;
}

int tm6000_set_standard(struct tm6000_core *dev)
{
	struct tm6000_input *input;
	int i, rc = 0;
	u8 reg_07_fe = 0x8a;
	u8 reg_08_f1 = 0xfc;
	u8 reg_08_e2 = 0xf0;
	u8 reg_08_e6 = 0x0f;

	tm6000_get_std_res(dev);

	if (!dev->radio)
		input = &dev->vinput[dev->input];
	else
		input = &dev->rinput;

	if (dev->dev_type == TM6010) {
		switch (input->vmux) {
		case TM6000_VMUX_VIDEO_A:
			tm6000_set_reg(dev, TM6010_REQ08_RE3_ADC_IN1_SEL, 0xf4);
			tm6000_set_reg(dev, TM6010_REQ08_REA_BUFF_DRV_CTRL, 0xf1);
			tm6000_set_reg(dev, TM6010_REQ08_REB_SIF_GAIN_CTRL, 0xe0);
			tm6000_set_reg(dev, TM6010_REQ08_REC_REVERSE_YC_CTRL, 0xc2);
			tm6000_set_reg(dev, TM6010_REQ08_RED_GAIN_SEL, 0xe8);
			reg_07_fe |= 0x01;
			break;
		case TM6000_VMUX_VIDEO_B:
			tm6000_set_reg(dev, TM6010_REQ08_RE3_ADC_IN1_SEL, 0xf8);
			tm6000_set_reg(dev, TM6010_REQ08_REA_BUFF_DRV_CTRL, 0xf1);
			tm6000_set_reg(dev, TM6010_REQ08_REB_SIF_GAIN_CTRL, 0xe0);
			tm6000_set_reg(dev, TM6010_REQ08_REC_REVERSE_YC_CTRL, 0xc2);
			tm6000_set_reg(dev, TM6010_REQ08_RED_GAIN_SEL, 0xe8);
			reg_07_fe |= 0x01;
			break;
		case TM6000_VMUX_VIDEO_AB:
			tm6000_set_reg(dev, TM6010_REQ08_RE3_ADC_IN1_SEL, 0xfc);
			tm6000_set_reg(dev, TM6010_REQ08_RE4_ADC_IN2_SEL, 0xf8);
			reg_08_e6 = 0x00;
			tm6000_set_reg(dev, TM6010_REQ08_REA_BUFF_DRV_CTRL, 0xf2);
			tm6000_set_reg(dev, TM6010_REQ08_REB_SIF_GAIN_CTRL, 0xf0);
			tm6000_set_reg(dev, TM6010_REQ08_REC_REVERSE_YC_CTRL, 0xc2);
			tm6000_set_reg(dev, TM6010_REQ08_RED_GAIN_SEL, 0xe0);
			break;
		default:
			break;
		}
		switch (input->amux) {
		case TM6000_AMUX_ADC1:
			tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
				0x00, 0x0f);
			/* Mux overflow workaround */
			tm6000_set_reg_mask(dev, TM6010_REQ07_R07_OUTPUT_CONTROL,
				0x10, 0xf0);
			break;
		case TM6000_AMUX_ADC2:
			tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
				0x08, 0x0f);
			/* Mux overflow workaround */
			tm6000_set_reg_mask(dev, TM6010_REQ07_R07_OUTPUT_CONTROL,
				0x10, 0xf0);
			break;
		case TM6000_AMUX_SIF1:
			reg_08_e2 |= 0x02;
			reg_08_e6 = 0x08;
			reg_07_fe |= 0x40;
			reg_08_f1 |= 0x02;
			tm6000_set_reg(dev, TM6010_REQ08_RE4_ADC_IN2_SEL, 0xf3);
			tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
				0x02, 0x0f);
			/* Mux overflow workaround */
			tm6000_set_reg_mask(dev, TM6010_REQ07_R07_OUTPUT_CONTROL,
				0x30, 0xf0);
			break;
		case TM6000_AMUX_SIF2:
			reg_08_e2 |= 0x02;
			reg_08_e6 = 0x08;
			reg_07_fe |= 0x40;
			reg_08_f1 |= 0x02;
			tm6000_set_reg(dev, TM6010_REQ08_RE4_ADC_IN2_SEL, 0xf7);
			tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
				0x02, 0x0f);
			/* Mux overflow workaround */
			tm6000_set_reg_mask(dev, TM6010_REQ07_R07_OUTPUT_CONTROL,
				0x30, 0xf0);
			break;
		default:
			break;
		}
		tm6000_set_reg(dev, TM6010_REQ08_RE2_POWER_DOWN_CTRL1, reg_08_e2);
		tm6000_set_reg(dev, TM6010_REQ08_RE6_POWER_DOWN_CTRL2, reg_08_e6);
		tm6000_set_reg(dev, TM6010_REQ08_RF1_AADC_POWER_DOWN, reg_08_f1);
		tm6000_set_reg(dev, TM6010_REQ07_RFE_POWER_DOWN, reg_07_fe);
	} else {
		switch (input->vmux) {
		case TM6000_VMUX_VIDEO_A:
			tm6000_set_reg(dev, TM6000_REQ07_RE3_VADC_INP_LPF_SEL1, 0x10);
			tm6000_set_reg(dev, TM6000_REQ07_RE5_VADC_INP_LPF_SEL2, 0x00);
			tm6000_set_reg(dev, TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0x0f);
			tm6000_set_reg(dev,
			    REQ_03_SET_GET_MCU_PIN, input->v_gpio, 0);
			break;
		case TM6000_VMUX_VIDEO_B:
			tm6000_set_reg(dev, TM6000_REQ07_RE3_VADC_INP_LPF_SEL1, 0x00);
			tm6000_set_reg(dev, TM6000_REQ07_RE5_VADC_INP_LPF_SEL2, 0x00);
			tm6000_set_reg(dev, TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0x0f);
			tm6000_set_reg(dev,
			    REQ_03_SET_GET_MCU_PIN, input->v_gpio, 0);
			break;
		case TM6000_VMUX_VIDEO_AB:
			tm6000_set_reg(dev, TM6000_REQ07_RE3_VADC_INP_LPF_SEL1, 0x10);
			tm6000_set_reg(dev, TM6000_REQ07_RE5_VADC_INP_LPF_SEL2, 0x10);
			tm6000_set_reg(dev, TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0x00);
			tm6000_set_reg(dev,
			    REQ_03_SET_GET_MCU_PIN, input->v_gpio, 1);
			break;
		default:
			break;
		}
		switch (input->amux) {
		case TM6000_AMUX_ADC1:
			tm6000_set_reg_mask(dev,
				TM6000_REQ07_REB_VADC_AADC_MODE, 0x00, 0x0f);
			break;
		case TM6000_AMUX_ADC2:
			tm6000_set_reg_mask(dev,
				TM6000_REQ07_REB_VADC_AADC_MODE, 0x04, 0x0f);
			break;
		default:
			break;
		}
	}
	if (input->type == TM6000_INPUT_SVIDEO) {
		for (i = 0; i < ARRAY_SIZE(svideo_stds); i++) {
			if (dev->norm & svideo_stds[i].id) {
				rc = tm6000_load_std(dev, svideo_stds[i].common);
				goto ret;
			}
		}
		return -EINVAL;
	} else {
		for (i = 0; i < ARRAY_SIZE(composite_stds); i++) {
			if (dev->norm & composite_stds[i].id) {
				rc = tm6000_load_std(dev, composite_stds[i].common);
				goto ret;
			}
		}
		return -EINVAL;
	}

ret:
	if (rc < 0)
		return rc;

	if ((dev->dev_type == TM6010) &&
	    ((input->amux == TM6000_AMUX_SIF1) ||
	    (input->amux == TM6000_AMUX_SIF2)))
		tm6000_set_audio_std(dev);

	msleep(40);

	return 0;
}
