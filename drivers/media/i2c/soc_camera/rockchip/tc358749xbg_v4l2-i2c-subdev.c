/*
 * drivers/media/i2c/soc_camera/xgold/tc358749xbg.c
 *
 * tc358749xbg sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2008 Texas Instruments.
 * Author: zhoupeng <benjo.zhou@rock-chips.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * Note:
 *
 * v0.1.0:
 *	1. Initialize version;
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>
#include <media/v4l2-controls_rockchip.h>
#include "tc_camera_module.h"

#define TC358749xbg_DRIVER_NAME "tc358749xbg"

/* product ID */
#define TC358749xbg_PID_MAGIC		0x4701
#define TC358749xbg_PID_ADDR		0x0000

#define INIT_END                              0x854A
#define SYS_STATUS                            0x8520
#define CONFCTL                               0x0004
#define VI_REP                                0x8576
#define MASK_VOUT_COLOR_SEL                   0xe0
#define MASK_VOUT_COLOR_RGB_FULL              0x00
#define MASK_VOUT_COLOR_RGB_LIMITED           0x20
#define MASK_VOUT_COLOR_601_YCBCR_FULL        0x40
#define MASK_VOUT_COLOR_601_YCBCR_LIMITED     0x60
#define MASK_VOUT_COLOR_709_YCBCR_FULL        0x80
#define MASK_VOUT_COLOR_709_YCBCR_LIMITED     0xa0
#define MASK_VOUT_COLOR_FULL_TO_LIMITED       0xc0
#define MASK_VOUT_COLOR_LIMITED_TO_FULL       0xe0
#define MASK_IN_REP_HEN                       0x10
#define MASK_IN_REP                           0x0f

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* MCLK:24MHz  1920x1080  30fps   mipi 4lane   800Mbps/lane */
static struct tc_camera_module_reg tc358749xbg_init_tab_1920_1080_60fps[] = {
	   //<!-- Software Reset -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0004, 0x0004, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0002, 0x7F80, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0002, 0x0000, 2},
	    //<!-- PLL Setting -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0020, 0x508A, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0022, 0x0203, 2},
	{TC_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x0001, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0022, 0x0213, 2},
	    //<!-- Misc Setting -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0006, 0x012C, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0060, 0x0001, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x7080, 0x0000, 2},
	    //<!-- Interrupt Control -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0014, 0x0000, 2},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0016, 0x05FF, 2},
	    //<!-- change 749 mipi out drive capability,
	    //to avoid pic error -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00000003, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0104, 0x00000003, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0108, 0x00000003, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x010C, 0x00000003, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0110, 0x00000003, 4},
	    //<!-- CSI Lane Enable -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0140, 0x00000000, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0144, 0x00000000, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0148, 0x00000000, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x014C, 0x00000000, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0150, 0x00000000, 4},
	    //<!-- CSI Transition Timing -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0210, 0x00001770, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0214, 0x00000005, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0218, 0x00001505, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x021C, 0x00000001, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0220, 0x00000105, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0224, 0x0000332C, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0228, 0x00000008, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x022C, 0x00000002, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0230, 0x00000005, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0234, 0x0000001F, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0238, 0x00000000, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x023C, 0x00040005, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0204, 0x00000001, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0518, 0x00000001, 4},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0500, 0xA3008086, 4},
	    //<!-- Data ID Setting -->
	    //<!-- HDMI Interrupt Mask -->
	    //<!-- HDMI Audio REFCLK -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8531, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8532, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8540, 0x8C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8541, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8630, 0xB0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8631, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8632, 0x04, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8670, 0x01, 1},
	    //<!-- HDMI PHY -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8532, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8536, 0x40, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x853F, 0x0A, 1},
	    //<!-- HDMI SYSTEM -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8543, 0x32, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8544, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8545, 0x31, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8546, 0x2D, 1},
	    //<!-- EDID -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85C7, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85CA, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85CB, 0x01, 1},
	    //<!-- EDID Data -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C00, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C01, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C02, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C03, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C04, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C05, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C06, 0xFF, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C07, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C08, 0x52, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C09, 0x62, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0A, 0x88, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0B, 0x88, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0C, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0D, 0x88, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0E, 0x88, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C0F, 0x88, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C10, 0x1C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C11, 0x15, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C12, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C13, 0x03, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C14, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C15, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C16, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C17, 0x78, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C18, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C19, 0x0D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1A, 0xC9, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1B, 0xA0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1C, 0x57, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1D, 0x47, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1E, 0x98, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C1F, 0x27, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C20, 0x12, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C21, 0x48, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C22, 0x4C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C23, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C24, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C25, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C26, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C27, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C28, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C29, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2A, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2B, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2C, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2D, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2E, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C2F, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C30, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C31, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C32, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C33, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C34, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C35, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C36, 0x02, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C37, 0x3A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C38, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C39, 0x18, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3A, 0x71, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3B, 0x38, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3C, 0x2D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3D, 0x40, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3E, 0x58, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C3F, 0x2C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C40, 0x45, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C41, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C42, 0xC4, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C43, 0x8E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C44, 0x21, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C45, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C46, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C47, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C48, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C49, 0x1D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4A, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4B, 0x72, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4C, 0x51, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4D, 0xD0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4E, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C4F, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C50, 0x6E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C51, 0x28, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C52, 0x55, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C53, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C54, 0xC4, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C55, 0x8E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C56, 0x21, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C57, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C58, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C59, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5A, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5B, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5C, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5D, 0xFC, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5E, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C5F, 0x54, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C60, 0x6F, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C61, 0x73, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C62, 0x68, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C63, 0x69, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C64, 0x62, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C65, 0x61, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C66, 0x2D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C67, 0x48, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C68, 0x32, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C69, 0x44, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6A, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6B, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6C, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6D, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6E, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C6F, 0xFD, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C70, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C71, 0x17, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C72, 0x3D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C73, 0x0F, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C74, 0x8C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C75, 0x17, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C76, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C77, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C78, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C79, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7A, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7B, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7C, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7D, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7E, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C7F, 0x92, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C80, 0x02, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C81, 0x03, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C82, 0x1A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C83, 0x74, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C84, 0x47, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C85, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C86, 0x04, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C87, 0x02, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C88, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C89, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8A, 0x04, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8B, 0x04, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8C, 0x23, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8D, 0x09, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8E, 0x07, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C8F, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C90, 0x83, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C91, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C92, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C93, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C94, 0x65, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C95, 0x03, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C96, 0x0C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C97, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C98, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C99, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9A, 0x8C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9B, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9C, 0xD0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9D, 0x8A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9E, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8C9F, 0xE0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA0, 0x2D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA1, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA2, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA3, 0x3E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA4, 0x96, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA5, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA6, 0x13, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA7, 0x8E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA8, 0x21, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CA9, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAA, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAB, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAC, 0xD8, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAD, 0x09, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAE, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CAF, 0xA0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB0, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB1, 0xE0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB2, 0x2D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB3, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB4, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB5, 0x60, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB6, 0xA2, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB7, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB8, 0xC4, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CB9, 0x8E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBA, 0x21, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBB, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBC, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBD, 0x18, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBE, 0x8C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CBF, 0x0A, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC0, 0xD0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC1, 0x90, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC2, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC3, 0x40, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC4, 0x31, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC5, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC6, 0x0C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC7, 0x40, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC8, 0x55, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CC9, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCA, 0x80, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCB, 0xB0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCC, 0x74, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCD, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCE, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CCF, 0x18, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD0, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD1, 0x1D, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD2, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD3, 0x72, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD4, 0x51, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD5, 0xD0, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD6, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD7, 0x20, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD8, 0x6E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CD9, 0x28, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDA, 0x55, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDB, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDC, 0xC4, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDD, 0x8E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDE, 0x21, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CDF, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE0, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE1, 0x1E, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE2, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE3, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE4, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE5, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE6, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE7, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE8, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CE9, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CEA, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CEB, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CEC, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CED, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CEE, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CEF, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF0, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF1, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF2, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF3, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF4, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF5, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF6, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF7, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF8, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CF9, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFA, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFB, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFC, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFD, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFE, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8CFF, 0x86, 1},
	    //<!-- HDCP Setting -->
	    //<!-- Video Setting -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8573, 0xC1, 1},
	    //<!-- HDMI Audio Setting -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8600, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8602, 0xF3, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8603, 0x02, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8604, 0x0C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8606, 0x05, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8607, 0x00, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8620, 0x22, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8640, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8641, 0x65, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8642, 0x07, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8652, 0x02, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x8665, 0x10, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85AA, 0x50, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85AF, 0xC6, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x85AB, 0x00, 1},
	    //<!-- Info Frame Extraction -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x870B, 0x2C, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x870C, 0x53, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x870D, 0x01, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x870E, 0x30, 1},
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x9007, 0x10, 1},
	    //<!-- Let HDMI Source start access -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x854A, 0x01, 1},
	    //<!-- VIP -->
	    //<!-- VIP Main Controls -->
	    //<!-- De-Interlacer IP Controls -->
	    //<!-- LCD Controller -->
	    //<!-- YCbCr to RGB -->
	    //<!-- VIP coeff -->
	    //<!-- Let HDMI Source start access -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x854A, 0x01, 1},
	    //<!-- Wait until HDMI sync is established -->
	    //<i2c_write addr="0x0F" count="2" radix="16">85 20</i2c_write>
	    //<i2c_read addr="0x0F" count="1" radix="16" />
	    //<!-- Sequence: Check bit7 of 8x8520 -->
	{TC_CAMERA_MODULE_REG_TYPE_DATA, 0x0004, 0x0CD7, 2},
};

/* ======================================================================== */
static struct tc_camera_module_config tc358749xbg_configs[] = {
	{
		.name = "1920x1080_60fps",
		.frm_fmt = {
			.width = 1920,
			.height = 1080,
			.code = MEDIA_BUS_FMT_UYVY8_2X8
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 60
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)tc358749xbg_init_tab_1920_1080_60fps,
		.reg_table_num_entries =
			sizeof(tc358749xbg_init_tab_1920_1080_60fps) /
			sizeof(tc358749xbg_init_tab_1920_1080_60fps[0]),
		.v_blanking_time_us = 3078,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 4, 800, 24000000)
	}
};

static struct tc35x_priv tc358749xbg_priv;

static struct tc_camera_module *to_tc_camera_module(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tc_camera_module, sd);
}

/*--------------------------------------------------------------------------*/
static int tc358749xbg_set_flip(
	struct tc_camera_module *cam_mod,
	struct tc_camera_module_reg reglist[],
	int len)
{
	int mode = 0;

	mode = tc_camera_module_get_flip_mirror(cam_mod);

	if (mode == -1) {
		tc_camera_module_pr_debug(
			cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	return 0;
}

static int tc358749xbg_write_aec(struct tc_camera_module *cam_mod)
{
	int ret = 0;

	tc_camera_module_pr_debug(cam_mod,
				  "exp_time=%d lines, gain=%d, flash_mode=%d\n",
				  cam_mod->exp_config.exp_time,
				  cam_mod->exp_config.gain,
				  cam_mod->exp_config.flash_mode);

	if (IS_ERR_VALUE(ret))
		tc_camera_module_pr_err(cam_mod,
					"failed with error (%d)\n", ret);
	return ret;
}

int tc358749xbg_enable_stream(struct tc_camera_module *cam_mod, bool enable)
{
	u8 init_end, sys_status;
	u16 confctl;

	if (enable) {
		init_end = tc_camera_module_read8_reg(cam_mod, INIT_END);
		tc_camera_module_pr_debug(cam_mod,
					  "INIT_END(0x854A) = 0x%02x\n",
					  init_end);
		sys_status = tc_camera_module_read8_reg(cam_mod, SYS_STATUS);
		tc_camera_module_pr_debug(cam_mod,
					  "INIT_END(0x8520) = 0x%02x\n",
					  sys_status);
	}

	confctl = tc_camera_module_read16_reg(cam_mod, CONFCTL);
	tc_camera_module_pr_debug(cam_mod,
				  "CONFCTL(0x0004) = 0x%04x\n",
				  confctl);

	return 0;
}

int tc358749xbg_s_power(struct tc_camera_module *cam_mod, bool enable)
{
	tc_camera_module_pr_debug(cam_mod,
				  "power %d\n",
				  enable);

	if (enable) {
		gpiod_direction_output(tc358749xbg_priv.gpio_reset, 0);
		gpiod_direction_output(tc358749xbg_priv.gpio_int, 0);
		gpiod_direction_output(tc358749xbg_priv.gpio_stanby, 0);
		gpiod_direction_output(tc358749xbg_priv.gpio_power18, 1);
		gpiod_direction_output(tc358749xbg_priv.gpio_power33, 1);
		gpiod_direction_output(tc358749xbg_priv.gpio_power, 1);
		gpiod_direction_output(tc358749xbg_priv.gpio_stanby, 1);
		gpiod_direction_output(tc358749xbg_priv.gpio_reset, 1);
	} else {
		gpiod_direction_output(tc358749xbg_priv.gpio_power, 0);
		gpiod_direction_output(tc358749xbg_priv.gpio_reset, 1);
		gpiod_direction_output(tc358749xbg_priv.gpio_reset, 0);
	}

	return 0;
}

static int tc358749xbg_g_fmt(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
	u8 vi_rep = tc_camera_module_read8_reg(cam_mod, VI_REP);

	pltfrm_camera_module_pr_debug(&cam_mod->sd,
				      "read VI_REP return 0x%x\n", vi_rep);

	if (format->pad != 0)
		return -EINVAL;
	if (cam_mod->active_config) {
		fmt->code = cam_mod->active_config->frm_fmt.code;
		fmt->width = cam_mod->active_config->frm_fmt.width;
		fmt->height = cam_mod->active_config->frm_fmt.height;
		fmt->field = V4L2_FIELD_NONE;

		switch (vi_rep & MASK_VOUT_COLOR_SEL) {
		case MASK_VOUT_COLOR_RGB_FULL:
		case MASK_VOUT_COLOR_RGB_LIMITED:
			fmt->colorspace = V4L2_COLORSPACE_SRGB;
			break;
		case MASK_VOUT_COLOR_601_YCBCR_LIMITED:
		case MASK_VOUT_COLOR_601_YCBCR_FULL:
			fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
			break;
		case MASK_VOUT_COLOR_709_YCBCR_FULL:
		case MASK_VOUT_COLOR_709_YCBCR_LIMITED:
			fmt->colorspace = V4L2_COLORSPACE_REC709;
			break;
		default:
			fmt->colorspace = 0;
			break;
		}

		return 0;
	}

	pltfrm_camera_module_pr_err(&cam_mod->sd, "no active config\n");

	return -1;
}

static int tc358749xbg_g_ctrl(struct tc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	tc_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		tc_camera_module_pr_debug(cam_mod,
					  "failed with error (%d)\n",
					  ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int tc358749xbg_filltimings(
	struct tc_camera_module_custom_config *custom)
{
	return 0;
}

/*--------------------------------------------------------------------------*/

static int tc358749xbg_g_timings(
	struct tc_camera_module *cam_mod,
	struct tc_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	*timings = cam_mod->active_config->timings;

	vts = (!cam_mod->vts_cur) ?
		timings->frame_length_lines :
		cam_mod->vts_cur;
	if (cam_mod->frm_intrvl_valid)
		timings->vt_pix_clk_freq_hz =
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
		cam_mod->active_config->frm_intrvl.interval.denominator *
		vts * timings->line_length_pck;

	timings->frame_length_lines = vts;

	return ret;
err:
	tc_camera_module_pr_err(cam_mod,
				"failed with error (%d)\n",
				ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int tc358749xbg_s_ctrl(struct tc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	tc_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = tc358749xbg_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	/*
	 * case RK_V4L2_CID_FPS_CTRL:
	 * if (cam_mod->auto_adjust_fps)
	 * ret = TC358749xbg_auto_adjust_fps(
	 * cam_mod,
	 * cam_mod->exp_config.exp_time);
	 * break;
	 */
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		tc_camera_module_pr_err(cam_mod,
					"failed with error (%d)\n",
					ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int tc358749xbg_s_ext_ctrls(
	struct tc_camera_module *cam_mod,
	struct tc_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = tc358749xbg_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		tc_camera_module_pr_debug(cam_mod,
					  "failed with error (%d)\n",
					  ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int tc358749xbg_check_camera_id(struct tc_camera_module *cam_mod)
{
	u16 pid;
	int ret = 0;

	tc_camera_module_pr_debug(cam_mod, "in\n");

	pid = tc_camera_module_read16_reg(cam_mod, TC358749xbg_PID_ADDR);
	tc_camera_module_pr_err(cam_mod,
				"read pid return 0x%x\n",
				pid);

	if (pid == TC358749xbg_PID_MAGIC) {
		tc_camera_module_pr_debug(cam_mod,
					  "successfully detected camera ID 0x%04x\n",
					  pid);
	} else {
		tc_camera_module_pr_err(cam_mod,
					"wrong camera ID, expected 0x%04x, detected 0x%04x\n",
					TC358749xbg_PID_MAGIC,
					pid);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	tc_camera_module_pr_err(cam_mod,
				"failed with error (%d)\n",
				ret);
	return ret;
}

/* ======================================================================== */
int tc_camera_358749xbg_module_s_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	return 0;
}

/* ======================================================================== */

int tc_camera_358749xbg_module_s_ext_ctrls(
	struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	return 0;
}

long tc_camera_358749xbg_module_ioctl(
	struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg)
{
	return 0;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops tc358749xbg_camera_module_core_ops = {
	.g_ctrl = tc_camera_module_g_ctrl,
	.s_ctrl = tc_camera_module_s_ctrl,
	.s_ext_ctrls = tc_camera_module_s_ext_ctrls,
	.s_power = tc_camera_module_s_power,
	.ioctl = tc_camera_module_ioctl
};

static struct v4l2_subdev_video_ops tc358749xbg_camera_module_video_ops = {
	.s_frame_interval = tc_camera_module_s_frame_interval,
	.g_frame_interval = tc_camera_module_g_frame_interval,
	.s_stream = tc_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops tc358749xbg_camera_module_pad_ops = {
	.enum_frame_interval = tc_camera_module_enum_frameintervals,
	.get_fmt = tc358749xbg_g_fmt,
	.set_fmt = tc_camera_module_s_fmt,
};

static struct v4l2_subdev_ops tc358749xbg_camera_module_ops = {
	.core = &tc358749xbg_camera_module_core_ops,
	.video = &tc358749xbg_camera_module_video_ops,
	.pad = &tc358749xbg_camera_module_pad_ops
};

static struct tc_camera_module tc358749xbg;

static struct tc_camera_module_custom_config tc358749xbg_custom_config = {
	.s_ctrl = tc358749xbg_s_ctrl,
	.g_ctrl = tc358749xbg_g_ctrl,
	.s_ext_ctrls = tc358749xbg_s_ext_ctrls,
	.g_timings = tc358749xbg_g_timings,
	.set_flip = tc358749xbg_set_flip,
	.check_camera_id = tc358749xbg_check_camera_id,
	.enable_stream = tc358749xbg_enable_stream,
	.s_power = tc358749xbg_s_power,
	.configs = tc358749xbg_configs,
	.num_configs = ARRAY_SIZE(tc358749xbg_configs),
	.power_up_delays_ms = {5, 30, 30},
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	.exposure_valid_frame = {4, 4}
};

static int test_parse_dts(
	struct tc_camera_module *cam_mod,
	struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = of_node_get(client->dev.of_node);

	tc_camera_module_pr_debug(cam_mod, "enter!\n");

	if (!dev)
		tc_camera_module_pr_debug(cam_mod, "dev is NULL!");

	if (!np)
		tc_camera_module_pr_debug(cam_mod, "np is NULL");

	tc358749xbg_priv.gpio_int = devm_gpiod_get_optional(dev, "int",
							    GPIOD_OUT_HIGH);
	tc358749xbg_priv.gpio_power = devm_gpiod_get_optional(dev, "power",
							      GPIOD_OUT_HIGH);
	tc358749xbg_priv.gpio_power18 = devm_gpiod_get_optional(dev, "power18",
								GPIOD_OUT_HIGH);
	tc358749xbg_priv.gpio_power33 = devm_gpiod_get_optional(dev, "power33",
								GPIOD_OUT_HIGH);
	tc358749xbg_priv.gpio_csi_ctl = devm_gpiod_get_optional(dev, "csi-ctl",
								GPIOD_OUT_LOW);
	tc358749xbg_priv.gpio_reset = devm_gpiod_get_optional(dev, "reset",
							      GPIOD_OUT_HIGH);
	tc358749xbg_priv.gpio_stanby = devm_gpiod_get_optional(dev, "stanby",
							       GPIOD_OUT_LOW);
	return 0;
}

static int test_deparse_dts(struct i2c_client *client)
{
	if (tc358749xbg_priv.gpio_int) {
		gpiod_direction_input(tc358749xbg_priv.gpio_int);
		gpiod_put(tc358749xbg_priv.gpio_int);
	}

	if (tc358749xbg_priv.gpio_reset) {
		gpiod_direction_input(tc358749xbg_priv.gpio_reset);
		gpiod_put(tc358749xbg_priv.gpio_reset);
	}

	if (tc358749xbg_priv.gpio_stanby) {
		gpiod_direction_input(tc358749xbg_priv.gpio_stanby);
		gpiod_put(tc358749xbg_priv.gpio_stanby);
	}

	if (tc358749xbg_priv.gpio_csi_ctl) {
		gpiod_direction_input(tc358749xbg_priv.gpio_csi_ctl);
		gpiod_put(tc358749xbg_priv.gpio_csi_ctl);
	}

	if (tc358749xbg_priv.gpio_power) {
		gpiod_direction_input(tc358749xbg_priv.gpio_power);
		gpiod_put(tc358749xbg_priv.gpio_power);
	}

	if (tc358749xbg_priv.gpio_power18) {
		gpiod_direction_input(tc358749xbg_priv.gpio_power18);
		gpiod_put(tc358749xbg_priv.gpio_power18);
	}

	if (tc358749xbg_priv.gpio_power33) {
		gpiod_direction_input(tc358749xbg_priv.gpio_power33);
		gpiod_put(tc358749xbg_priv.gpio_power33);
	}

	return 0;
}

static ssize_t tc358749xbg_debugfs_reg_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct tc_camera_module *cam_mod =
		((struct seq_file *)file->private_data)->private;

	char kbuf[30];
	char rw;
	u32 reg;
	u32 reg_value;
	int len;
	int ret;
	int nbytes = min(count, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, buf, nbytes))
		return -EFAULT;

	kbuf[nbytes] = '\0';
	tc_camera_module_pr_debug(cam_mod, "kbuf is %s\n", kbuf);
	ret = sscanf(kbuf, " %c %x %x %d", &rw, &reg, &reg_value, &len);
	tc_camera_module_pr_err(cam_mod, "ret = %d!\n", ret);
	if (ret != 4) {
		tc_camera_module_pr_err(cam_mod, "sscanf failed!\n");
		return 0;
	}

	if (rw == 'w') {
		if (len == 1) {
			tc_camera_module_write8_reg(cam_mod, reg, reg_value);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): write reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else if (len == 2) {
			tc_camera_module_write16_reg(cam_mod, reg, reg_value);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): write reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else if (len == 4) {
			tc_camera_module_write32_reg(cam_mod, reg, reg_value);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): write reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else {
			tc_camera_module_pr_err(cam_mod,
						"len %d is err!\n",
						len);
		}
	} else if (rw == 'r') {
		if (len == 1) {
			reg_value = tc_camera_module_read8_reg(cam_mod, reg);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): read reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else if (len == 2) {
			reg_value = tc_camera_module_read16_reg(cam_mod, reg);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): read reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else if (len == 4) {
			reg_value = tc_camera_module_read32_reg(cam_mod, reg);
			tc_camera_module_pr_err(cam_mod,
						"%s(%d): read reg 0x%02x ---> 0x%x!\n",
						__func__, __LINE__,
						reg, reg_value);
		} else {
			tc_camera_module_pr_err(cam_mod,
						"len %d is err!\n",
						len);
		}
	} else {
		tc_camera_module_pr_err(cam_mod,
					"%c command is not support\n",
					rw);
	}

	return count;
}

static int tc358749xbg_debugfs_reg_show(struct seq_file *s, void *v)
{
	int i;
	u32 value;
	int reg_table_num_entries =
			sizeof(tc358749xbg_init_tab_1920_1080_60fps) /
			sizeof(tc358749xbg_init_tab_1920_1080_60fps[0]);
	struct tc_camera_module *cam_mod = s->private;

	for (i = 0; i < reg_table_num_entries; i++) {
		switch (tc358749xbg_init_tab_1920_1080_60fps[i].len) {
		case 1:
			value = tc_camera_module_read8_reg(cam_mod,
							   tc358749xbg_init_tab_1920_1080_60fps[i].reg);
			break;
		case 2:
			value =  tc_camera_module_read16_reg(cam_mod,
							     tc358749xbg_init_tab_1920_1080_60fps[i].reg);
			break;
		case 4:
			value =  tc_camera_module_read32_reg(cam_mod,
							     tc358749xbg_init_tab_1920_1080_60fps[i].reg);
			break;
		default:
			printk(KERN_ERR "%s(%d): command no support!\n", __func__, __LINE__);
			break;
		}
		printk(KERN_ERR "%s(%d): reg 0x%04x ---> 0x%x\n", __func__, __LINE__,
		       tc358749xbg_init_tab_1920_1080_60fps[i].reg, value);
	}
	return 0;
}

static int tc358749xbg_debugfs_open(struct inode *inode, struct file *file)
{
	struct specific_sensor *spsensor = inode->i_private;

	return single_open(file, tc358749xbg_debugfs_reg_show, spsensor);
}

static const struct file_operations tc358749xbg_debugfs_fops = {
	.owner			= THIS_MODULE,
	.open			= tc358749xbg_debugfs_open,
	.read			= seq_read,
	.write			= tc358749xbg_debugfs_reg_write,
	.llseek			= seq_lseek,
	.release		= single_release
};

static struct dentry *debugfs_dir;

static int tc358749xbg_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	tc358749xbg_filltimings(&tc358749xbg_custom_config);
	v4l2_i2c_subdev_init(&tc358749xbg.sd, client,
			     &tc358749xbg_camera_module_ops);

	tc358749xbg.custom = tc358749xbg_custom_config;

	test_parse_dts(&tc358749xbg, client);
	debugfs_dir = debugfs_create_dir("hdmiin", NULL);
	if (IS_ERR(debugfs_dir))
		printk(KERN_ERR "%s(%d): create debugfs dir failed!\n",
		       __func__, __LINE__);
	else
		debugfs_create_file("register", S_IRUSR,
				    debugfs_dir, &tc358749xbg,
				    &tc358749xbg_debugfs_fops);

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

/* ======================================================================== */

static int tc358749xbg_remove(
	struct i2c_client *client)
{
	struct tc_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "remtcing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	debugfs_remove_recursive(debugfs_dir);
	test_deparse_dts(client);

	tc_camera_module_release(cam_mod);

	dev_info(&client->dev, "remtced\n");
	return 0;
}

static const struct i2c_device_id tc358749xbg_id[] = {
	{ TC358749xbg_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id tc358749xbg_of_match[] = {
	{.compatible = "toshiba,tc358749xbg-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, tc358749xbg_id);

static struct i2c_driver tc358749xbg_i2c_driver = {
	.driver = {
		.name = TC358749xbg_DRIVER_NAME,
		.of_match_table = tc358749xbg_of_match
	},
	.probe = tc358749xbg_probe,
	.remove = tc358749xbg_remove,
	.id_table = tc358749xbg_id,
};

module_i2c_driver(tc358749xbg_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for tc358749xbg");
MODULE_AUTHOR("Benjo");
MODULE_LICENSE("GPL");
