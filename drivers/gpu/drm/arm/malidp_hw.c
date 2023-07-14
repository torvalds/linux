// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * ARM Mali DP500/DP550/DP650 hardware manipulation routines. This is where
 * the difference between various versions of the hardware is being dealt with
 * in an attempt to provide to the rest of the driver code a unified view
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/io.h>

#include <video/videomode.h>
#include <video/display_timing.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>
#include <drm/drm_print.h>

#include "malidp_drv.h"
#include "malidp_hw.h"
#include "malidp_mw.h"

enum {
	MW_NOT_ENABLED = 0,	/* SE writeback not enabled */
	MW_ONESHOT,		/* SE in one-shot mode for writeback */
	MW_START,		/* SE started writeback */
	MW_RESTART,		/* SE will start another writeback after this one */
	MW_STOP,		/* SE needs to stop after this writeback */
};

static const struct malidp_format_id malidp500_de_formats[] = {
	/*    fourcc,   layers supporting the format,     internal id  */
	{ DRM_FORMAT_ARGB2101010, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2 | SE_MEMWRITE,  0 },
	{ DRM_FORMAT_ABGR2101010, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2 | SE_MEMWRITE,  1 },
	{ DRM_FORMAT_ARGB8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  2 },
	{ DRM_FORMAT_ABGR8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  3 },
	{ DRM_FORMAT_XRGB8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2 | SE_MEMWRITE,  4 },
	{ DRM_FORMAT_XBGR8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2 | SE_MEMWRITE,  5 },
	{ DRM_FORMAT_RGB888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  6 },
	{ DRM_FORMAT_BGR888, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  7 },
	{ DRM_FORMAT_RGBA5551, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  8 },
	{ DRM_FORMAT_ABGR1555, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2,  9 },
	{ DRM_FORMAT_RGB565, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2, 10 },
	{ DRM_FORMAT_BGR565, DE_VIDEO1 | DE_GRAPHICS1 | DE_GRAPHICS2, 11 },
	{ DRM_FORMAT_UYVY, DE_VIDEO1, 12 },
	{ DRM_FORMAT_YUYV, DE_VIDEO1, 13 },
	{ DRM_FORMAT_NV12, DE_VIDEO1 | SE_MEMWRITE, 14 },
	{ DRM_FORMAT_YUV420, DE_VIDEO1, 15 },
	{ DRM_FORMAT_XYUV8888, DE_VIDEO1, 16 },
	/* These are supported with AFBC only */
	{ DRM_FORMAT_YUV420_8BIT, DE_VIDEO1, 14 },
	{ DRM_FORMAT_VUY888, DE_VIDEO1, 16 },
	{ DRM_FORMAT_VUY101010, DE_VIDEO1, 17 },
	{ DRM_FORMAT_YUV420_10BIT, DE_VIDEO1, 18 }
};

#define MALIDP_ID(__group, __format) \
	((((__group) & 0x7) << 3) | ((__format) & 0x7))

#define AFBC_YUV_422_FORMAT_ID	MALIDP_ID(5, 1)

#define MALIDP_COMMON_FORMATS \
	/*    fourcc,   layers supporting the format,      internal id   */ \
	{ DRM_FORMAT_ARGB2101010, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(0, 0) }, \
	{ DRM_FORMAT_ABGR2101010, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(0, 1) }, \
	{ DRM_FORMAT_RGBA1010102, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(0, 2) }, \
	{ DRM_FORMAT_BGRA1010102, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(0, 3) }, \
	{ DRM_FORMAT_ARGB8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART, MALIDP_ID(1, 0) }, \
	{ DRM_FORMAT_ABGR8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART, MALIDP_ID(1, 1) }, \
	{ DRM_FORMAT_RGBA8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART, MALIDP_ID(1, 2) }, \
	{ DRM_FORMAT_BGRA8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART, MALIDP_ID(1, 3) }, \
	{ DRM_FORMAT_XRGB8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART | SE_MEMWRITE, MALIDP_ID(2, 0) }, \
	{ DRM_FORMAT_XBGR8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART | SE_MEMWRITE, MALIDP_ID(2, 1) }, \
	{ DRM_FORMAT_RGBX8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART | SE_MEMWRITE, MALIDP_ID(2, 2) }, \
	{ DRM_FORMAT_BGRX8888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | DE_SMART | SE_MEMWRITE, MALIDP_ID(2, 3) }, \
	{ DRM_FORMAT_RGB888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(3, 0) }, \
	{ DRM_FORMAT_BGR888, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(3, 1) }, \
	{ DRM_FORMAT_RGBA5551, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2, MALIDP_ID(4, 0) }, \
	{ DRM_FORMAT_ABGR1555, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2, MALIDP_ID(4, 1) }, \
	{ DRM_FORMAT_RGB565, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2, MALIDP_ID(4, 2) }, \
	{ DRM_FORMAT_BGR565, DE_VIDEO1 | DE_GRAPHICS1 | DE_VIDEO2, MALIDP_ID(4, 3) }, \
	/* This is only supported with linear modifier */	\
	{ DRM_FORMAT_XYUV8888, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 0) },\
	/* This is only supported with AFBC modifier */		\
	{ DRM_FORMAT_VUY888, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 0) }, \
	{ DRM_FORMAT_YUYV, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 2) },	\
	/* This is only supported with linear modifier */ \
	{ DRM_FORMAT_UYVY, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 3) },	\
	{ DRM_FORMAT_NV12, DE_VIDEO1 | DE_VIDEO2 | SE_MEMWRITE, MALIDP_ID(5, 6) },	\
	/* This is only supported with AFBC modifier */ \
	{ DRM_FORMAT_YUV420_8BIT, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 6) }, \
	{ DRM_FORMAT_YUV420, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 7) }, \
	/* This is only supported with linear modifier */ \
	{ DRM_FORMAT_XVYU2101010, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(6, 0)}, \
	/* This is only supported with AFBC modifier */ \
	{ DRM_FORMAT_VUY101010, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(6, 0)}, \
	{ DRM_FORMAT_X0L2, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(6, 6)}, \
	/* This is only supported with AFBC modifier */ \
	{ DRM_FORMAT_YUV420_10BIT, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(6, 7)}, \
	{ DRM_FORMAT_P010, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(6, 7)}

static const struct malidp_format_id malidp550_de_formats[] = {
	MALIDP_COMMON_FORMATS,
};

static const struct malidp_format_id malidp650_de_formats[] = {
	MALIDP_COMMON_FORMATS,
	{ DRM_FORMAT_X0L0, DE_VIDEO1 | DE_VIDEO2, MALIDP_ID(5, 4)},
};

static const struct malidp_layer malidp500_layers[] = {
	/* id, base address, fb pointer address base, stride offset,
	 *	yuv2rgb matrix offset, mmu control register offset, rotation_features
	 */
	{ DE_VIDEO1, MALIDP500_DE_LV_BASE, MALIDP500_DE_LV_PTR_BASE,
		MALIDP_DE_LV_STRIDE0, MALIDP500_LV_YUV2RGB, 0, ROTATE_ANY,
		MALIDP500_DE_LV_AD_CTRL },
	{ DE_GRAPHICS1, MALIDP500_DE_LG1_BASE, MALIDP500_DE_LG1_PTR_BASE,
		MALIDP_DE_LG_STRIDE, 0, 0, ROTATE_ANY,
		MALIDP500_DE_LG1_AD_CTRL },
	{ DE_GRAPHICS2, MALIDP500_DE_LG2_BASE, MALIDP500_DE_LG2_PTR_BASE,
		MALIDP_DE_LG_STRIDE, 0, 0, ROTATE_ANY,
		MALIDP500_DE_LG2_AD_CTRL },
};

static const struct malidp_layer malidp550_layers[] = {
	/* id, base address, fb pointer address base, stride offset,
	 *	yuv2rgb matrix offset, mmu control register offset, rotation_features
	 */
	{ DE_VIDEO1, MALIDP550_DE_LV1_BASE, MALIDP550_DE_LV1_PTR_BASE,
		MALIDP_DE_LV_STRIDE0, MALIDP550_LV_YUV2RGB, 0, ROTATE_ANY,
		MALIDP550_DE_LV1_AD_CTRL },
	{ DE_GRAPHICS1, MALIDP550_DE_LG_BASE, MALIDP550_DE_LG_PTR_BASE,
		MALIDP_DE_LG_STRIDE, 0, 0, ROTATE_ANY,
		MALIDP550_DE_LG_AD_CTRL },
	{ DE_VIDEO2, MALIDP550_DE_LV2_BASE, MALIDP550_DE_LV2_PTR_BASE,
		MALIDP_DE_LV_STRIDE0, MALIDP550_LV_YUV2RGB, 0, ROTATE_ANY,
		MALIDP550_DE_LV2_AD_CTRL },
	{ DE_SMART, MALIDP550_DE_LS_BASE, MALIDP550_DE_LS_PTR_BASE,
		MALIDP550_DE_LS_R1_STRIDE, 0, 0, ROTATE_NONE, 0 },
};

static const struct malidp_layer malidp650_layers[] = {
	/* id, base address, fb pointer address base, stride offset,
	 *	yuv2rgb matrix offset, mmu control register offset,
	 *	rotation_features
	 */
	{ DE_VIDEO1, MALIDP550_DE_LV1_BASE, MALIDP550_DE_LV1_PTR_BASE,
		MALIDP_DE_LV_STRIDE0, MALIDP550_LV_YUV2RGB,
		MALIDP650_DE_LV_MMU_CTRL, ROTATE_ANY,
		MALIDP550_DE_LV1_AD_CTRL },
	{ DE_GRAPHICS1, MALIDP550_DE_LG_BASE, MALIDP550_DE_LG_PTR_BASE,
		MALIDP_DE_LG_STRIDE, 0, MALIDP650_DE_LG_MMU_CTRL,
		ROTATE_COMPRESSED, MALIDP550_DE_LG_AD_CTRL },
	{ DE_VIDEO2, MALIDP550_DE_LV2_BASE, MALIDP550_DE_LV2_PTR_BASE,
		MALIDP_DE_LV_STRIDE0, MALIDP550_LV_YUV2RGB,
		MALIDP650_DE_LV_MMU_CTRL, ROTATE_ANY,
		MALIDP550_DE_LV2_AD_CTRL },
	{ DE_SMART, MALIDP550_DE_LS_BASE, MALIDP550_DE_LS_PTR_BASE,
		MALIDP550_DE_LS_R1_STRIDE, 0, MALIDP650_DE_LS_MMU_CTRL,
		ROTATE_NONE, 0 },
};

const u64 malidp_format_modifiers[] = {
	/* All RGB formats (except XRGB, RGBX, XBGR, BGRX) */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_YTR | AFBC_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_YTR),

	/* All RGB formats > 16bpp (except XRGB, RGBX, XBGR, BGRX) */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_YTR | AFBC_SPARSE | AFBC_SPLIT),

	/* All 8 or 10 bit YUV 444 formats. */
	/* In DP550, 10 bit YUV 420 format also supported */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_SPARSE | AFBC_SPLIT),

	/* YUV 420, 422 P1 8 bit and YUV 444 8 bit/10 bit formats */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16),

	/* YUV 420, 422 P1 8, 10 bit formats */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_CBR | AFBC_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_SIZE_16X16 | AFBC_CBR),

	/* All formats */
	DRM_FORMAT_MOD_LINEAR,

	DRM_FORMAT_MOD_INVALID
};

#define SE_N_SCALING_COEFFS	96
static const u16 dp500_se_scaling_coeffs[][SE_N_SCALING_COEFFS] = {
	[MALIDP_UPSCALING_COEFFS - 1] = {
		0x0000, 0x0001, 0x0007, 0x0011, 0x001e, 0x002e, 0x003f, 0x0052,
		0x0064, 0x0073, 0x007d, 0x0080, 0x007a, 0x006c, 0x0053, 0x002f,
		0x0000, 0x3fc6, 0x3f83, 0x3f39, 0x3eea, 0x3e9b, 0x3e4f, 0x3e0a,
		0x3dd4, 0x3db0, 0x3da2, 0x3db1, 0x3dde, 0x3e2f, 0x3ea5, 0x3f40,
		0x0000, 0x00e5, 0x01ee, 0x0315, 0x0456, 0x05aa, 0x0709, 0x086c,
		0x09c9, 0x0b15, 0x0c4a, 0x0d5d, 0x0e4a, 0x0f06, 0x0f91, 0x0fe5,
		0x1000, 0x0fe5, 0x0f91, 0x0f06, 0x0e4a, 0x0d5d, 0x0c4a, 0x0b15,
		0x09c9, 0x086c, 0x0709, 0x05aa, 0x0456, 0x0315, 0x01ee, 0x00e5,
		0x0000, 0x3f40, 0x3ea5, 0x3e2f, 0x3dde, 0x3db1, 0x3da2, 0x3db0,
		0x3dd4, 0x3e0a, 0x3e4f, 0x3e9b, 0x3eea, 0x3f39, 0x3f83, 0x3fc6,
		0x0000, 0x002f, 0x0053, 0x006c, 0x007a, 0x0080, 0x007d, 0x0073,
		0x0064, 0x0052, 0x003f, 0x002e, 0x001e, 0x0011, 0x0007, 0x0001
	},
	[MALIDP_DOWNSCALING_1_5_COEFFS - 1] = {
		0x0059, 0x004f, 0x0041, 0x002e, 0x0016, 0x3ffb, 0x3fd9, 0x3fb4,
		0x3f8c, 0x3f62, 0x3f36, 0x3f09, 0x3edd, 0x3eb3, 0x3e8d, 0x3e6c,
		0x3e52, 0x3e3f, 0x3e35, 0x3e37, 0x3e46, 0x3e61, 0x3e8c, 0x3ec5,
		0x3f0f, 0x3f68, 0x3fd1, 0x004a, 0x00d3, 0x0169, 0x020b, 0x02b8,
		0x036e, 0x042d, 0x04f2, 0x05b9, 0x0681, 0x0745, 0x0803, 0x08ba,
		0x0965, 0x0a03, 0x0a91, 0x0b0d, 0x0b75, 0x0bc6, 0x0c00, 0x0c20,
		0x0c28, 0x0c20, 0x0c00, 0x0bc6, 0x0b75, 0x0b0d, 0x0a91, 0x0a03,
		0x0965, 0x08ba, 0x0803, 0x0745, 0x0681, 0x05b9, 0x04f2, 0x042d,
		0x036e, 0x02b8, 0x020b, 0x0169, 0x00d3, 0x004a, 0x3fd1, 0x3f68,
		0x3f0f, 0x3ec5, 0x3e8c, 0x3e61, 0x3e46, 0x3e37, 0x3e35, 0x3e3f,
		0x3e52, 0x3e6c, 0x3e8d, 0x3eb3, 0x3edd, 0x3f09, 0x3f36, 0x3f62,
		0x3f8c, 0x3fb4, 0x3fd9, 0x3ffb, 0x0016, 0x002e, 0x0041, 0x004f
	},
	[MALIDP_DOWNSCALING_2_COEFFS - 1] = {
		0x3f19, 0x3f03, 0x3ef0, 0x3edf, 0x3ed0, 0x3ec5, 0x3ebd, 0x3eb9,
		0x3eb9, 0x3ebf, 0x3eca, 0x3ed9, 0x3eef, 0x3f0a, 0x3f2c, 0x3f52,
		0x3f7f, 0x3fb0, 0x3fe8, 0x0026, 0x006a, 0x00b4, 0x0103, 0x0158,
		0x01b1, 0x020d, 0x026c, 0x02cd, 0x032f, 0x0392, 0x03f4, 0x0455,
		0x04b4, 0x051e, 0x0585, 0x05eb, 0x064c, 0x06a8, 0x06fe, 0x074e,
		0x0796, 0x07d5, 0x080c, 0x0839, 0x085c, 0x0875, 0x0882, 0x0887,
		0x0881, 0x0887, 0x0882, 0x0875, 0x085c, 0x0839, 0x080c, 0x07d5,
		0x0796, 0x074e, 0x06fe, 0x06a8, 0x064c, 0x05eb, 0x0585, 0x051e,
		0x04b4, 0x0455, 0x03f4, 0x0392, 0x032f, 0x02cd, 0x026c, 0x020d,
		0x01b1, 0x0158, 0x0103, 0x00b4, 0x006a, 0x0026, 0x3fe8, 0x3fb0,
		0x3f7f, 0x3f52, 0x3f2c, 0x3f0a, 0x3eef, 0x3ed9, 0x3eca, 0x3ebf,
		0x3eb9, 0x3eb9, 0x3ebd, 0x3ec5, 0x3ed0, 0x3edf, 0x3ef0, 0x3f03
	},
	[MALIDP_DOWNSCALING_2_75_COEFFS - 1] = {
		0x3f51, 0x3f60, 0x3f71, 0x3f84, 0x3f98, 0x3faf, 0x3fc8, 0x3fe3,
		0x0000, 0x001f, 0x0040, 0x0064, 0x008a, 0x00b1, 0x00da, 0x0106,
		0x0133, 0x0160, 0x018e, 0x01bd, 0x01ec, 0x021d, 0x024e, 0x0280,
		0x02b2, 0x02e4, 0x0317, 0x0349, 0x037c, 0x03ad, 0x03df, 0x0410,
		0x0440, 0x0468, 0x048f, 0x04b3, 0x04d6, 0x04f8, 0x0516, 0x0533,
		0x054e, 0x0566, 0x057c, 0x0590, 0x05a0, 0x05ae, 0x05ba, 0x05c3,
		0x05c9, 0x05c3, 0x05ba, 0x05ae, 0x05a0, 0x0590, 0x057c, 0x0566,
		0x054e, 0x0533, 0x0516, 0x04f8, 0x04d6, 0x04b3, 0x048f, 0x0468,
		0x0440, 0x0410, 0x03df, 0x03ad, 0x037c, 0x0349, 0x0317, 0x02e4,
		0x02b2, 0x0280, 0x024e, 0x021d, 0x01ec, 0x01bd, 0x018e, 0x0160,
		0x0133, 0x0106, 0x00da, 0x00b1, 0x008a, 0x0064, 0x0040, 0x001f,
		0x0000, 0x3fe3, 0x3fc8, 0x3faf, 0x3f98, 0x3f84, 0x3f71, 0x3f60
	},
	[MALIDP_DOWNSCALING_4_COEFFS - 1] = {
		0x0094, 0x00a9, 0x00be, 0x00d4, 0x00ea, 0x0101, 0x0118, 0x012f,
		0x0148, 0x0160, 0x017a, 0x0193, 0x01ae, 0x01c8, 0x01e4, 0x01ff,
		0x021c, 0x0233, 0x024a, 0x0261, 0x0278, 0x028f, 0x02a6, 0x02bd,
		0x02d4, 0x02eb, 0x0302, 0x0319, 0x032f, 0x0346, 0x035d, 0x0374,
		0x038a, 0x0397, 0x03a3, 0x03af, 0x03bb, 0x03c6, 0x03d1, 0x03db,
		0x03e4, 0x03ed, 0x03f6, 0x03fe, 0x0406, 0x040d, 0x0414, 0x041a,
		0x0420, 0x041a, 0x0414, 0x040d, 0x0406, 0x03fe, 0x03f6, 0x03ed,
		0x03e4, 0x03db, 0x03d1, 0x03c6, 0x03bb, 0x03af, 0x03a3, 0x0397,
		0x038a, 0x0374, 0x035d, 0x0346, 0x032f, 0x0319, 0x0302, 0x02eb,
		0x02d4, 0x02bd, 0x02a6, 0x028f, 0x0278, 0x0261, 0x024a, 0x0233,
		0x021c, 0x01ff, 0x01e4, 0x01c8, 0x01ae, 0x0193, 0x017a, 0x0160,
		0x0148, 0x012f, 0x0118, 0x0101, 0x00ea, 0x00d4, 0x00be, 0x00a9
	},
};

#define MALIDP_DE_DEFAULT_PREFETCH_START	5

static int malidp500_query_hw(struct malidp_hw_device *hwdev)
{
	u32 conf = malidp_hw_read(hwdev, MALIDP500_CONFIG_ID);
	/* bit 4 of the CONFIG_ID register holds the line size multiplier */
	u8 ln_size_mult = conf & 0x10 ? 2 : 1;

	hwdev->min_line_size = 2;
	hwdev->max_line_size = SZ_2K * ln_size_mult;
	hwdev->rotation_memory[0] = SZ_1K * 64 * ln_size_mult;
	hwdev->rotation_memory[1] = 0; /* no second rotation memory bank */

	return 0;
}

static void malidp500_enter_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status, count = 100;

	malidp_hw_setbits(hwdev, MALIDP500_DC_CONFIG_REQ, MALIDP500_DC_CONTROL);
	while (count) {
		status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
		if ((status & MALIDP500_DC_CONFIG_REQ) == MALIDP500_DC_CONFIG_REQ)
			break;
		/*
		 * entering config mode can take as long as the rendering
		 * of a full frame, hence the long sleep here
		 */
		usleep_range(1000, 10000);
		count--;
	}
	WARN(count == 0, "timeout while entering config mode");
}

static void malidp500_leave_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status, count = 100;

	malidp_hw_clearbits(hwdev, MALIDP_CFG_VALID, MALIDP500_CONFIG_VALID);
	malidp_hw_clearbits(hwdev, MALIDP500_DC_CONFIG_REQ, MALIDP500_DC_CONTROL);
	while (count) {
		status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
		if ((status & MALIDP500_DC_CONFIG_REQ) == 0)
			break;
		usleep_range(100, 1000);
		count--;
	}
	WARN(count == 0, "timeout while leaving config mode");
}

static bool malidp500_in_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status;

	status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
	if ((status & MALIDP500_DC_CONFIG_REQ) == MALIDP500_DC_CONFIG_REQ)
		return true;

	return false;
}

static void malidp500_set_config_valid(struct malidp_hw_device *hwdev, u8 value)
{
	if (value)
		malidp_hw_setbits(hwdev, MALIDP_CFG_VALID, MALIDP500_CONFIG_VALID);
	else
		malidp_hw_clearbits(hwdev, MALIDP_CFG_VALID, MALIDP500_CONFIG_VALID);
}

static void malidp500_modeset(struct malidp_hw_device *hwdev, struct videomode *mode)
{
	u32 val = 0;

	malidp_hw_write(hwdev, hwdev->output_color_depth,
		hwdev->hw->map.out_depth_base);
	malidp_hw_clearbits(hwdev, MALIDP500_DC_CLEAR_MASK, MALIDP500_DC_CONTROL);
	if (mode->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		val |= MALIDP500_HSYNCPOL;
	if (mode->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		val |= MALIDP500_VSYNCPOL;
	val |= MALIDP_DE_DEFAULT_PREFETCH_START;
	malidp_hw_setbits(hwdev, val, MALIDP500_DC_CONTROL);

	/*
	 * Mali-DP500 encodes the background color like this:
	 *    - red   @ MALIDP500_BGND_COLOR[12:0]
	 *    - green @ MALIDP500_BGND_COLOR[27:16]
	 *    - blue  @ (MALIDP500_BGND_COLOR + 4)[12:0]
	 */
	val = ((MALIDP_BGND_COLOR_G & 0xfff) << 16) |
	      (MALIDP_BGND_COLOR_R & 0xfff);
	malidp_hw_write(hwdev, val, MALIDP500_BGND_COLOR);
	malidp_hw_write(hwdev, MALIDP_BGND_COLOR_B, MALIDP500_BGND_COLOR + 4);

	val = MALIDP_DE_H_FRONTPORCH(mode->hfront_porch) |
		MALIDP_DE_H_BACKPORCH(mode->hback_porch);
	malidp_hw_write(hwdev, val, MALIDP500_TIMINGS_BASE + MALIDP_DE_H_TIMINGS);

	val = MALIDP500_DE_V_FRONTPORCH(mode->vfront_porch) |
		MALIDP_DE_V_BACKPORCH(mode->vback_porch);
	malidp_hw_write(hwdev, val, MALIDP500_TIMINGS_BASE + MALIDP_DE_V_TIMINGS);

	val = MALIDP_DE_H_SYNCWIDTH(mode->hsync_len) |
		MALIDP_DE_V_SYNCWIDTH(mode->vsync_len);
	malidp_hw_write(hwdev, val, MALIDP500_TIMINGS_BASE + MALIDP_DE_SYNC_WIDTH);

	val = MALIDP_DE_H_ACTIVE(mode->hactive) | MALIDP_DE_V_ACTIVE(mode->vactive);
	malidp_hw_write(hwdev, val, MALIDP500_TIMINGS_BASE + MALIDP_DE_HV_ACTIVE);

	if (mode->flags & DISPLAY_FLAGS_INTERLACED)
		malidp_hw_setbits(hwdev, MALIDP_DISP_FUNC_ILACED, MALIDP_DE_DISPLAY_FUNC);
	else
		malidp_hw_clearbits(hwdev, MALIDP_DISP_FUNC_ILACED, MALIDP_DE_DISPLAY_FUNC);

	/*
	 * Program the RQoS register to avoid high resolutions flicker
	 * issue on the LS1028A.
	 */
	if (hwdev->arqos_value) {
		val = hwdev->arqos_value;
		malidp_hw_setbits(hwdev, val, MALIDP500_RQOS_QUALITY);
	}
}

int malidp_format_get_bpp(u32 fmt)
{
	const struct drm_format_info *info = drm_format_info(fmt);
	int bpp = info->cpp[0] * 8;

	if (bpp == 0) {
		switch (fmt) {
		case DRM_FORMAT_VUY101010:
			bpp = 30;
			break;
		case DRM_FORMAT_YUV420_10BIT:
			bpp = 15;
			break;
		case DRM_FORMAT_YUV420_8BIT:
			bpp = 12;
			break;
		default:
			bpp = 0;
		}
	}

	return bpp;
}

static int malidp500_rotmem_required(struct malidp_hw_device *hwdev, u16 w,
				     u16 h, u32 fmt, bool has_modifier)
{
	/*
	 * Each layer needs enough rotation memory to fit 8 lines
	 * worth of pixel data. Required size is then:
	 *    size = rotated_width * (bpp / 8) * 8;
	 */
	int bpp = malidp_format_get_bpp(fmt);

	return w * bpp;
}

static void malidp500_se_write_pp_coefftab(struct malidp_hw_device *hwdev,
					   u32 direction,
					   u16 addr,
					   u8 coeffs_id)
{
	int i;
	u16 scaling_control = MALIDP500_SE_CONTROL + MALIDP_SE_SCALING_CONTROL;

	malidp_hw_write(hwdev,
			direction | (addr & MALIDP_SE_COEFFTAB_ADDR_MASK),
			scaling_control + MALIDP_SE_COEFFTAB_ADDR);
	for (i = 0; i < ARRAY_SIZE(dp500_se_scaling_coeffs); ++i)
		malidp_hw_write(hwdev, MALIDP_SE_SET_COEFFTAB_DATA(
				dp500_se_scaling_coeffs[coeffs_id][i]),
				scaling_control + MALIDP_SE_COEFFTAB_DATA);
}

static int malidp500_se_set_scaling_coeffs(struct malidp_hw_device *hwdev,
					   struct malidp_se_config *se_config,
					   struct malidp_se_config *old_config)
{
	/* Get array indices into dp500_se_scaling_coeffs. */
	u8 h = (u8)se_config->hcoeff - 1;
	u8 v = (u8)se_config->vcoeff - 1;

	if (WARN_ON(h >= ARRAY_SIZE(dp500_se_scaling_coeffs) ||
		    v >= ARRAY_SIZE(dp500_se_scaling_coeffs)))
		return -EINVAL;

	if ((h == v) && (se_config->hcoeff != old_config->hcoeff ||
			 se_config->vcoeff != old_config->vcoeff)) {
		malidp500_se_write_pp_coefftab(hwdev,
					       (MALIDP_SE_V_COEFFTAB |
						MALIDP_SE_H_COEFFTAB),
					       0, v);
	} else {
		if (se_config->vcoeff != old_config->vcoeff)
			malidp500_se_write_pp_coefftab(hwdev,
						       MALIDP_SE_V_COEFFTAB,
						       0, v);
		if (se_config->hcoeff != old_config->hcoeff)
			malidp500_se_write_pp_coefftab(hwdev,
						       MALIDP_SE_H_COEFFTAB,
						       0, h);
	}

	return 0;
}

static long malidp500_se_calc_mclk(struct malidp_hw_device *hwdev,
				   struct malidp_se_config *se_config,
				   struct videomode *vm)
{
	unsigned long mclk;
	unsigned long pxlclk = vm->pixelclock; /* Hz */
	unsigned long htotal = vm->hactive + vm->hfront_porch +
			       vm->hback_porch + vm->hsync_len;
	unsigned long input_size = se_config->input_w * se_config->input_h;
	unsigned long a = 10;
	long ret;

	/*
	 * mclk = max(a, 1.5) * pxlclk
	 *
	 * To avoid float calculaiton, using 15 instead of 1.5 and div by
	 * 10 to get mclk.
	 */
	if (se_config->scale_enable) {
		a = 15 * input_size / (htotal * se_config->output_h);
		if (a < 15)
			a = 15;
	}
	mclk = a * pxlclk / 10;
	ret = clk_get_rate(hwdev->mclk);
	if (ret < mclk) {
		DRM_DEBUG_DRIVER("mclk requirement of %lu kHz can't be met.\n",
				 mclk / 1000);
		return -EINVAL;
	}
	return ret;
}

static int malidp500_enable_memwrite(struct malidp_hw_device *hwdev,
				     dma_addr_t *addrs, s32 *pitches,
				     int num_planes, u16 w, u16 h, u32 fmt_id,
				     const s16 *rgb2yuv_coeffs)
{
	u32 base = MALIDP500_SE_MEMWRITE_BASE;
	u32 de_base = malidp_get_block_base(hwdev, MALIDP_DE_BLOCK);

	/* enable the scaling engine block */
	malidp_hw_setbits(hwdev, MALIDP_SCALE_ENGINE_EN, de_base + MALIDP_DE_DISPLAY_FUNC);

	/* restart the writeback if already enabled */
	if (hwdev->mw_state != MW_NOT_ENABLED)
		hwdev->mw_state = MW_RESTART;
	else
		hwdev->mw_state = MW_START;

	malidp_hw_write(hwdev, fmt_id, base + MALIDP_MW_FORMAT);
	switch (num_planes) {
	case 2:
		malidp_hw_write(hwdev, lower_32_bits(addrs[1]), base + MALIDP_MW_P2_PTR_LOW);
		malidp_hw_write(hwdev, upper_32_bits(addrs[1]), base + MALIDP_MW_P2_PTR_HIGH);
		malidp_hw_write(hwdev, pitches[1], base + MALIDP_MW_P2_STRIDE);
		fallthrough;
	case 1:
		malidp_hw_write(hwdev, lower_32_bits(addrs[0]), base + MALIDP_MW_P1_PTR_LOW);
		malidp_hw_write(hwdev, upper_32_bits(addrs[0]), base + MALIDP_MW_P1_PTR_HIGH);
		malidp_hw_write(hwdev, pitches[0], base + MALIDP_MW_P1_STRIDE);
		break;
	default:
		WARN(1, "Invalid number of planes");
	}

	malidp_hw_write(hwdev, MALIDP_DE_H_ACTIVE(w) | MALIDP_DE_V_ACTIVE(h),
			MALIDP500_SE_MEMWRITE_OUT_SIZE);

	if (rgb2yuv_coeffs) {
		int i;

		for (i = 0; i < MALIDP_COLORADJ_NUM_COEFFS; i++) {
			malidp_hw_write(hwdev, rgb2yuv_coeffs[i],
					MALIDP500_SE_RGB_YUV_COEFFS + i * 4);
		}
	}

	malidp_hw_setbits(hwdev, MALIDP_SE_MEMWRITE_EN, MALIDP500_SE_CONTROL);

	return 0;
}

static void malidp500_disable_memwrite(struct malidp_hw_device *hwdev)
{
	u32 base = malidp_get_block_base(hwdev, MALIDP_DE_BLOCK);

	if (hwdev->mw_state == MW_START || hwdev->mw_state == MW_RESTART)
		hwdev->mw_state = MW_STOP;
	malidp_hw_clearbits(hwdev, MALIDP_SE_MEMWRITE_EN, MALIDP500_SE_CONTROL);
	malidp_hw_clearbits(hwdev, MALIDP_SCALE_ENGINE_EN, base + MALIDP_DE_DISPLAY_FUNC);
}

static int malidp550_query_hw(struct malidp_hw_device *hwdev)
{
	u32 conf = malidp_hw_read(hwdev, MALIDP550_CONFIG_ID);
	u8 ln_size = (conf >> 4) & 0x3, rsize;

	hwdev->min_line_size = 2;

	switch (ln_size) {
	case 0:
		hwdev->max_line_size = SZ_2K;
		/* two banks of 64KB for rotation memory */
		rsize = 64;
		break;
	case 1:
		hwdev->max_line_size = SZ_4K;
		/* two banks of 128KB for rotation memory */
		rsize = 128;
		break;
	case 2:
		hwdev->max_line_size = 1280;
		/* two banks of 40KB for rotation memory */
		rsize = 40;
		break;
	case 3:
		/* reserved value */
		hwdev->max_line_size = 0;
		return -EINVAL;
	}

	hwdev->rotation_memory[0] = hwdev->rotation_memory[1] = rsize * SZ_1K;
	return 0;
}

static void malidp550_enter_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status, count = 100;

	malidp_hw_setbits(hwdev, MALIDP550_DC_CONFIG_REQ, MALIDP550_DC_CONTROL);
	while (count) {
		status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
		if ((status & MALIDP550_DC_CONFIG_REQ) == MALIDP550_DC_CONFIG_REQ)
			break;
		/*
		 * entering config mode can take as long as the rendering
		 * of a full frame, hence the long sleep here
		 */
		usleep_range(1000, 10000);
		count--;
	}
	WARN(count == 0, "timeout while entering config mode");
}

static void malidp550_leave_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status, count = 100;

	malidp_hw_clearbits(hwdev, MALIDP_CFG_VALID, MALIDP550_CONFIG_VALID);
	malidp_hw_clearbits(hwdev, MALIDP550_DC_CONFIG_REQ, MALIDP550_DC_CONTROL);
	while (count) {
		status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
		if ((status & MALIDP550_DC_CONFIG_REQ) == 0)
			break;
		usleep_range(100, 1000);
		count--;
	}
	WARN(count == 0, "timeout while leaving config mode");
}

static bool malidp550_in_config_mode(struct malidp_hw_device *hwdev)
{
	u32 status;

	status = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_REG_STATUS);
	if ((status & MALIDP550_DC_CONFIG_REQ) == MALIDP550_DC_CONFIG_REQ)
		return true;

	return false;
}

static void malidp550_set_config_valid(struct malidp_hw_device *hwdev, u8 value)
{
	if (value)
		malidp_hw_setbits(hwdev, MALIDP_CFG_VALID, MALIDP550_CONFIG_VALID);
	else
		malidp_hw_clearbits(hwdev, MALIDP_CFG_VALID, MALIDP550_CONFIG_VALID);
}

static void malidp550_modeset(struct malidp_hw_device *hwdev, struct videomode *mode)
{
	u32 val = MALIDP_DE_DEFAULT_PREFETCH_START;

	malidp_hw_write(hwdev, hwdev->output_color_depth,
		hwdev->hw->map.out_depth_base);
	malidp_hw_write(hwdev, val, MALIDP550_DE_CONTROL);
	/*
	 * Mali-DP550 and Mali-DP650 encode the background color like this:
	 *   - red   @ MALIDP550_DE_BGND_COLOR[23:16]
	 *   - green @ MALIDP550_DE_BGND_COLOR[15:8]
	 *   - blue  @ MALIDP550_DE_BGND_COLOR[7:0]
	 *
	 * We need to truncate the least significant 4 bits from the default
	 * MALIDP_BGND_COLOR_x values
	 */
	val = (((MALIDP_BGND_COLOR_R >> 4) & 0xff) << 16) |
	      (((MALIDP_BGND_COLOR_G >> 4) & 0xff) << 8) |
	      ((MALIDP_BGND_COLOR_B >> 4) & 0xff);
	malidp_hw_write(hwdev, val, MALIDP550_DE_BGND_COLOR);

	val = MALIDP_DE_H_FRONTPORCH(mode->hfront_porch) |
		MALIDP_DE_H_BACKPORCH(mode->hback_porch);
	malidp_hw_write(hwdev, val, MALIDP550_TIMINGS_BASE + MALIDP_DE_H_TIMINGS);

	val = MALIDP550_DE_V_FRONTPORCH(mode->vfront_porch) |
		MALIDP_DE_V_BACKPORCH(mode->vback_porch);
	malidp_hw_write(hwdev, val, MALIDP550_TIMINGS_BASE + MALIDP_DE_V_TIMINGS);

	val = MALIDP_DE_H_SYNCWIDTH(mode->hsync_len) |
		MALIDP_DE_V_SYNCWIDTH(mode->vsync_len);
	if (mode->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		val |= MALIDP550_HSYNCPOL;
	if (mode->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		val |= MALIDP550_VSYNCPOL;
	malidp_hw_write(hwdev, val, MALIDP550_TIMINGS_BASE + MALIDP_DE_SYNC_WIDTH);

	val = MALIDP_DE_H_ACTIVE(mode->hactive) | MALIDP_DE_V_ACTIVE(mode->vactive);
	malidp_hw_write(hwdev, val, MALIDP550_TIMINGS_BASE + MALIDP_DE_HV_ACTIVE);

	if (mode->flags & DISPLAY_FLAGS_INTERLACED)
		malidp_hw_setbits(hwdev, MALIDP_DISP_FUNC_ILACED, MALIDP_DE_DISPLAY_FUNC);
	else
		malidp_hw_clearbits(hwdev, MALIDP_DISP_FUNC_ILACED, MALIDP_DE_DISPLAY_FUNC);
}

static int malidpx50_get_bytes_per_column(u32 fmt)
{
	u32 bytes_per_column;

	switch (fmt) {
	/* 8 lines at 4 bytes per pixel */
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	/* 16 lines at 2 bytes per pixel */
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_X0L0:
		bytes_per_column = 32;
		break;
	/* 16 lines at 1.5 bytes per pixel */
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_YUV420:
	/* 8 lines at 3 bytes per pixel */
	case DRM_FORMAT_VUY888:
	/* 16 lines at 12 bits per pixel */
	case DRM_FORMAT_YUV420_8BIT:
	/* 8 lines at 3 bytes per pixel */
	case DRM_FORMAT_P010:
		bytes_per_column = 24;
		break;
	/* 8 lines at 30 bits per pixel */
	case DRM_FORMAT_VUY101010:
	/* 16 lines at 15 bits per pixel */
	case DRM_FORMAT_YUV420_10BIT:
		bytes_per_column = 30;
		break;
	default:
		return -EINVAL;
	}

	return bytes_per_column;
}

static int malidp550_rotmem_required(struct malidp_hw_device *hwdev, u16 w,
				     u16 h, u32 fmt, bool has_modifier)
{
	int bytes_per_column = 0;

	switch (fmt) {
	/* 8 lines at 15 bits per pixel */
	case DRM_FORMAT_YUV420_10BIT:
		bytes_per_column = 15;
		break;
	/* Uncompressed YUV 420 10 bit single plane cannot be rotated */
	case DRM_FORMAT_X0L2:
		if (has_modifier)
			bytes_per_column = 8;
		else
			return -EINVAL;
		break;
	default:
		bytes_per_column = malidpx50_get_bytes_per_column(fmt);
	}

	if (bytes_per_column == -EINVAL)
		return bytes_per_column;

	return w * bytes_per_column;
}

static int malidp650_rotmem_required(struct malidp_hw_device *hwdev, u16 w,
				     u16 h, u32 fmt, bool has_modifier)
{
	int bytes_per_column = 0;

	switch (fmt) {
	/* 16 lines at 2 bytes per pixel */
	case DRM_FORMAT_X0L2:
		bytes_per_column = 32;
		break;
	default:
		bytes_per_column = malidpx50_get_bytes_per_column(fmt);
	}

	if (bytes_per_column == -EINVAL)
		return bytes_per_column;

	return w * bytes_per_column;
}

static int malidp550_se_set_scaling_coeffs(struct malidp_hw_device *hwdev,
					   struct malidp_se_config *se_config,
					   struct malidp_se_config *old_config)
{
	u32 mask = MALIDP550_SE_CTL_VCSEL(MALIDP550_SE_CTL_SEL_MASK) |
		   MALIDP550_SE_CTL_HCSEL(MALIDP550_SE_CTL_SEL_MASK);
	u32 new_value = MALIDP550_SE_CTL_VCSEL(se_config->vcoeff) |
			MALIDP550_SE_CTL_HCSEL(se_config->hcoeff);

	malidp_hw_clearbits(hwdev, mask, MALIDP550_SE_CONTROL);
	malidp_hw_setbits(hwdev, new_value, MALIDP550_SE_CONTROL);
	return 0;
}

static long malidp550_se_calc_mclk(struct malidp_hw_device *hwdev,
				   struct malidp_se_config *se_config,
				   struct videomode *vm)
{
	unsigned long mclk;
	unsigned long pxlclk = vm->pixelclock;
	unsigned long htotal = vm->hactive + vm->hfront_porch +
			       vm->hback_porch + vm->hsync_len;
	unsigned long numerator = 1, denominator = 1;
	long ret;

	if (se_config->scale_enable) {
		numerator = max(se_config->input_w, se_config->output_w) *
			    se_config->input_h;
		numerator += se_config->output_w *
			     (se_config->output_h -
			      min(se_config->input_h, se_config->output_h));
		denominator = (htotal - 2) * se_config->output_h;
	}

	/* mclk can't be slower than pxlclk. */
	if (numerator < denominator)
		numerator = denominator = 1;
	mclk = (pxlclk * numerator) / denominator;
	ret = clk_get_rate(hwdev->mclk);
	if (ret < mclk) {
		DRM_DEBUG_DRIVER("mclk requirement of %lu kHz can't be met.\n",
				 mclk / 1000);
		return -EINVAL;
	}
	return ret;
}

static int malidp550_enable_memwrite(struct malidp_hw_device *hwdev,
				     dma_addr_t *addrs, s32 *pitches,
				     int num_planes, u16 w, u16 h, u32 fmt_id,
				     const s16 *rgb2yuv_coeffs)
{
	u32 base = MALIDP550_SE_MEMWRITE_BASE;
	u32 de_base = malidp_get_block_base(hwdev, MALIDP_DE_BLOCK);

	/* enable the scaling engine block */
	malidp_hw_setbits(hwdev, MALIDP_SCALE_ENGINE_EN, de_base + MALIDP_DE_DISPLAY_FUNC);

	hwdev->mw_state = MW_ONESHOT;

	malidp_hw_write(hwdev, fmt_id, base + MALIDP_MW_FORMAT);
	switch (num_planes) {
	case 2:
		malidp_hw_write(hwdev, lower_32_bits(addrs[1]), base + MALIDP_MW_P2_PTR_LOW);
		malidp_hw_write(hwdev, upper_32_bits(addrs[1]), base + MALIDP_MW_P2_PTR_HIGH);
		malidp_hw_write(hwdev, pitches[1], base + MALIDP_MW_P2_STRIDE);
		fallthrough;
	case 1:
		malidp_hw_write(hwdev, lower_32_bits(addrs[0]), base + MALIDP_MW_P1_PTR_LOW);
		malidp_hw_write(hwdev, upper_32_bits(addrs[0]), base + MALIDP_MW_P1_PTR_HIGH);
		malidp_hw_write(hwdev, pitches[0], base + MALIDP_MW_P1_STRIDE);
		break;
	default:
		WARN(1, "Invalid number of planes");
	}

	malidp_hw_write(hwdev, MALIDP_DE_H_ACTIVE(w) | MALIDP_DE_V_ACTIVE(h),
			MALIDP550_SE_MEMWRITE_OUT_SIZE);
	malidp_hw_setbits(hwdev, MALIDP550_SE_MEMWRITE_ONESHOT | MALIDP_SE_MEMWRITE_EN,
			  MALIDP550_SE_CONTROL);

	if (rgb2yuv_coeffs) {
		int i;

		for (i = 0; i < MALIDP_COLORADJ_NUM_COEFFS; i++) {
			malidp_hw_write(hwdev, rgb2yuv_coeffs[i],
					MALIDP550_SE_RGB_YUV_COEFFS + i * 4);
		}
	}

	return 0;
}

static void malidp550_disable_memwrite(struct malidp_hw_device *hwdev)
{
	u32 base = malidp_get_block_base(hwdev, MALIDP_DE_BLOCK);

	malidp_hw_clearbits(hwdev, MALIDP550_SE_MEMWRITE_ONESHOT | MALIDP_SE_MEMWRITE_EN,
			    MALIDP550_SE_CONTROL);
	malidp_hw_clearbits(hwdev, MALIDP_SCALE_ENGINE_EN, base + MALIDP_DE_DISPLAY_FUNC);
}

static int malidp650_query_hw(struct malidp_hw_device *hwdev)
{
	u32 conf = malidp_hw_read(hwdev, MALIDP550_CONFIG_ID);
	u8 ln_size = (conf >> 4) & 0x3, rsize;

	hwdev->min_line_size = 4;

	switch (ln_size) {
	case 0:
	case 2:
		/* reserved values */
		hwdev->max_line_size = 0;
		return -EINVAL;
	case 1:
		hwdev->max_line_size = SZ_4K;
		/* two banks of 128KB for rotation memory */
		rsize = 128;
		break;
	case 3:
		hwdev->max_line_size = 2560;
		/* two banks of 80KB for rotation memory */
		rsize = 80;
	}

	hwdev->rotation_memory[0] = hwdev->rotation_memory[1] = rsize * SZ_1K;
	return 0;
}

const struct malidp_hw malidp_device[MALIDP_MAX_DEVICES] = {
	[MALIDP_500] = {
		.map = {
			.coeffs_base = MALIDP500_COEFFS_BASE,
			.se_base = MALIDP500_SE_BASE,
			.dc_base = MALIDP500_DC_BASE,
			.out_depth_base = MALIDP500_OUTPUT_DEPTH,
			.features = 0,	/* no CLEARIRQ register */
			.n_layers = ARRAY_SIZE(malidp500_layers),
			.layers = malidp500_layers,
			.de_irq_map = {
				.irq_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP500_DE_IRQ_AXI_ERR |
					    MALIDP500_DE_IRQ_VSYNC |
					    MALIDP500_DE_IRQ_GLOBAL,
				.vsync_irq = MALIDP500_DE_IRQ_VSYNC,
				.err_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP500_DE_IRQ_AXI_ERR |
					    MALIDP500_DE_IRQ_SATURATION,
			},
			.se_irq_map = {
				.irq_mask = MALIDP500_SE_IRQ_CONF_MODE |
					    MALIDP500_SE_IRQ_CONF_VALID |
					    MALIDP500_SE_IRQ_GLOBAL,
				.vsync_irq = MALIDP500_SE_IRQ_CONF_VALID,
				.err_mask = MALIDP500_SE_IRQ_INIT_BUSY |
					    MALIDP500_SE_IRQ_AXI_ERROR |
					    MALIDP500_SE_IRQ_OVERRUN,
			},
			.dc_irq_map = {
				.irq_mask = MALIDP500_DE_IRQ_CONF_VALID,
				.vsync_irq = MALIDP500_DE_IRQ_CONF_VALID,
			},
			.pixel_formats = malidp500_de_formats,
			.n_pixel_formats = ARRAY_SIZE(malidp500_de_formats),
			.bus_align_bytes = 8,
		},
		.query_hw = malidp500_query_hw,
		.enter_config_mode = malidp500_enter_config_mode,
		.leave_config_mode = malidp500_leave_config_mode,
		.in_config_mode = malidp500_in_config_mode,
		.set_config_valid = malidp500_set_config_valid,
		.modeset = malidp500_modeset,
		.rotmem_required = malidp500_rotmem_required,
		.se_set_scaling_coeffs = malidp500_se_set_scaling_coeffs,
		.se_calc_mclk = malidp500_se_calc_mclk,
		.enable_memwrite = malidp500_enable_memwrite,
		.disable_memwrite = malidp500_disable_memwrite,
		.features = MALIDP_DEVICE_LV_HAS_3_STRIDES,
	},
	[MALIDP_550] = {
		.map = {
			.coeffs_base = MALIDP550_COEFFS_BASE,
			.se_base = MALIDP550_SE_BASE,
			.dc_base = MALIDP550_DC_BASE,
			.out_depth_base = MALIDP550_DE_OUTPUT_DEPTH,
			.features = MALIDP_REGMAP_HAS_CLEARIRQ |
				    MALIDP_DEVICE_AFBC_SUPPORT_SPLIT |
				    MALIDP_DEVICE_AFBC_YUV_420_10_SUPPORT_SPLIT |
				    MALIDP_DEVICE_AFBC_YUYV_USE_422_P2,
			.n_layers = ARRAY_SIZE(malidp550_layers),
			.layers = malidp550_layers,
			.de_irq_map = {
				.irq_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP550_DE_IRQ_VSYNC,
				.vsync_irq = MALIDP550_DE_IRQ_VSYNC,
				.err_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP550_DE_IRQ_SATURATION |
					    MALIDP550_DE_IRQ_AXI_ERR,
			},
			.se_irq_map = {
				.irq_mask = MALIDP550_SE_IRQ_EOW,
				.vsync_irq = MALIDP550_SE_IRQ_EOW,
				.err_mask  = MALIDP550_SE_IRQ_AXI_ERR |
					     MALIDP550_SE_IRQ_OVR |
					     MALIDP550_SE_IRQ_IBSY,
			},
			.dc_irq_map = {
				.irq_mask = MALIDP550_DC_IRQ_CONF_VALID |
					    MALIDP550_DC_IRQ_SE,
				.vsync_irq = MALIDP550_DC_IRQ_CONF_VALID,
			},
			.pixel_formats = malidp550_de_formats,
			.n_pixel_formats = ARRAY_SIZE(malidp550_de_formats),
			.bus_align_bytes = 8,
		},
		.query_hw = malidp550_query_hw,
		.enter_config_mode = malidp550_enter_config_mode,
		.leave_config_mode = malidp550_leave_config_mode,
		.in_config_mode = malidp550_in_config_mode,
		.set_config_valid = malidp550_set_config_valid,
		.modeset = malidp550_modeset,
		.rotmem_required = malidp550_rotmem_required,
		.se_set_scaling_coeffs = malidp550_se_set_scaling_coeffs,
		.se_calc_mclk = malidp550_se_calc_mclk,
		.enable_memwrite = malidp550_enable_memwrite,
		.disable_memwrite = malidp550_disable_memwrite,
		.features = 0,
	},
	[MALIDP_650] = {
		.map = {
			.coeffs_base = MALIDP550_COEFFS_BASE,
			.se_base = MALIDP550_SE_BASE,
			.dc_base = MALIDP550_DC_BASE,
			.out_depth_base = MALIDP550_DE_OUTPUT_DEPTH,
			.features = MALIDP_REGMAP_HAS_CLEARIRQ |
				    MALIDP_DEVICE_AFBC_SUPPORT_SPLIT |
				    MALIDP_DEVICE_AFBC_YUYV_USE_422_P2,
			.n_layers = ARRAY_SIZE(malidp650_layers),
			.layers = malidp650_layers,
			.de_irq_map = {
				.irq_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP650_DE_IRQ_DRIFT |
					    MALIDP550_DE_IRQ_VSYNC,
				.vsync_irq = MALIDP550_DE_IRQ_VSYNC,
				.err_mask = MALIDP_DE_IRQ_UNDERRUN |
					    MALIDP650_DE_IRQ_DRIFT |
					    MALIDP550_DE_IRQ_SATURATION |
					    MALIDP550_DE_IRQ_AXI_ERR |
					    MALIDP650_DE_IRQ_ACEV1 |
					    MALIDP650_DE_IRQ_ACEV2 |
					    MALIDP650_DE_IRQ_ACEG |
					    MALIDP650_DE_IRQ_AXIEP,
			},
			.se_irq_map = {
				.irq_mask = MALIDP550_SE_IRQ_EOW,
				.vsync_irq = MALIDP550_SE_IRQ_EOW,
				.err_mask = MALIDP550_SE_IRQ_AXI_ERR |
					    MALIDP550_SE_IRQ_OVR |
					    MALIDP550_SE_IRQ_IBSY,
			},
			.dc_irq_map = {
				.irq_mask = MALIDP550_DC_IRQ_CONF_VALID |
					    MALIDP550_DC_IRQ_SE,
				.vsync_irq = MALIDP550_DC_IRQ_CONF_VALID,
			},
			.pixel_formats = malidp650_de_formats,
			.n_pixel_formats = ARRAY_SIZE(malidp650_de_formats),
			.bus_align_bytes = 16,
		},
		.query_hw = malidp650_query_hw,
		.enter_config_mode = malidp550_enter_config_mode,
		.leave_config_mode = malidp550_leave_config_mode,
		.in_config_mode = malidp550_in_config_mode,
		.set_config_valid = malidp550_set_config_valid,
		.modeset = malidp550_modeset,
		.rotmem_required = malidp650_rotmem_required,
		.se_set_scaling_coeffs = malidp550_se_set_scaling_coeffs,
		.se_calc_mclk = malidp550_se_calc_mclk,
		.enable_memwrite = malidp550_enable_memwrite,
		.disable_memwrite = malidp550_disable_memwrite,
		.features = 0,
	},
};

u8 malidp_hw_get_format_id(const struct malidp_hw_regmap *map,
			   u8 layer_id, u32 format, bool has_modifier)
{
	unsigned int i;

	for (i = 0; i < map->n_pixel_formats; i++) {
		if (((map->pixel_formats[i].layer & layer_id) == layer_id) &&
		    (map->pixel_formats[i].format == format)) {
			/*
			 * In some DP550 and DP650, DRM_FORMAT_YUYV + AFBC modifier
			 * is supported by a different h/w format id than
			 * DRM_FORMAT_YUYV (only).
			 */
			if (format == DRM_FORMAT_YUYV &&
			    (has_modifier) &&
			    (map->features & MALIDP_DEVICE_AFBC_YUYV_USE_422_P2))
				return AFBC_YUV_422_FORMAT_ID;
			else
				return map->pixel_formats[i].id;
		}
	}

	return MALIDP_INVALID_FORMAT_ID;
}

bool malidp_hw_format_is_linear_only(u32 format)
{
	switch (format) {
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_X0L2:
	case DRM_FORMAT_X0L0:
		return true;
	default:
		return false;
	}
}

bool malidp_hw_format_is_afbc_only(u32 format)
{
	switch (format) {
	case DRM_FORMAT_VUY888:
	case DRM_FORMAT_VUY101010:
	case DRM_FORMAT_YUV420_8BIT:
	case DRM_FORMAT_YUV420_10BIT:
		return true;
	default:
		return false;
	}
}

static void malidp_hw_clear_irq(struct malidp_hw_device *hwdev, u8 block, u32 irq)
{
	u32 base = malidp_get_block_base(hwdev, block);

	if (hwdev->hw->map.features & MALIDP_REGMAP_HAS_CLEARIRQ)
		malidp_hw_write(hwdev, irq, base + MALIDP_REG_CLEARIRQ);
	else
		malidp_hw_write(hwdev, irq, base + MALIDP_REG_STATUS);
}

static irqreturn_t malidp_de_irq(int irq, void *arg)
{
	struct drm_device *drm = arg;
	struct malidp_drm *malidp = drm_to_malidp(drm);
	struct malidp_hw_device *hwdev;
	struct malidp_hw *hw;
	const struct malidp_irq_map *de;
	u32 status, mask, dc_status;
	irqreturn_t ret = IRQ_NONE;

	hwdev = malidp->dev;
	hw = hwdev->hw;
	de = &hw->map.de_irq_map;

	/*
	 * if we are suspended it is likely that we were invoked because
	 * we share an interrupt line with some other driver, don't try
	 * to read the hardware registers
	 */
	if (hwdev->pm_suspended)
		return IRQ_NONE;

	/* first handle the config valid IRQ */
	dc_status = malidp_hw_read(hwdev, hw->map.dc_base + MALIDP_REG_STATUS);
	if (dc_status & hw->map.dc_irq_map.vsync_irq) {
		malidp_hw_clear_irq(hwdev, MALIDP_DC_BLOCK, dc_status);
		/* do we have a page flip event? */
		if (malidp->event != NULL) {
			spin_lock(&drm->event_lock);
			drm_crtc_send_vblank_event(&malidp->crtc, malidp->event);
			malidp->event = NULL;
			spin_unlock(&drm->event_lock);
		}
		atomic_set(&malidp->config_valid, MALIDP_CONFIG_VALID_DONE);
		ret = IRQ_WAKE_THREAD;
	}

	status = malidp_hw_read(hwdev, MALIDP_REG_STATUS);
	if (!(status & de->irq_mask))
		return ret;

	mask = malidp_hw_read(hwdev, MALIDP_REG_MASKIRQ);
	/* keep the status of the enabled interrupts, plus the error bits */
	status &= (mask | de->err_mask);
	if ((status & de->vsync_irq) && malidp->crtc.enabled)
		drm_crtc_handle_vblank(&malidp->crtc);

#ifdef CONFIG_DEBUG_FS
	if (status & de->err_mask) {
		malidp_error(malidp, &malidp->de_errors, status,
			     drm_crtc_vblank_count(&malidp->crtc));
	}
#endif
	malidp_hw_clear_irq(hwdev, MALIDP_DE_BLOCK, status);

	return (ret == IRQ_NONE) ? IRQ_HANDLED : ret;
}

static irqreturn_t malidp_de_irq_thread_handler(int irq, void *arg)
{
	struct drm_device *drm = arg;
	struct malidp_drm *malidp = drm_to_malidp(drm);

	wake_up(&malidp->wq);

	return IRQ_HANDLED;
}

void malidp_de_irq_hw_init(struct malidp_hw_device *hwdev)
{
	/* ensure interrupts are disabled */
	malidp_hw_disable_irq(hwdev, MALIDP_DE_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_DE_BLOCK, 0xffffffff);
	malidp_hw_disable_irq(hwdev, MALIDP_DC_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_DC_BLOCK, 0xffffffff);

	/* first enable the DC block IRQs */
	malidp_hw_enable_irq(hwdev, MALIDP_DC_BLOCK,
			     hwdev->hw->map.dc_irq_map.irq_mask);

	/* now enable the DE block IRQs */
	malidp_hw_enable_irq(hwdev, MALIDP_DE_BLOCK,
			     hwdev->hw->map.de_irq_map.irq_mask);
}

int malidp_de_irq_init(struct drm_device *drm, int irq)
{
	struct malidp_drm *malidp = drm_to_malidp(drm);
	struct malidp_hw_device *hwdev = malidp->dev;
	int ret;

	/* ensure interrupts are disabled */
	malidp_hw_disable_irq(hwdev, MALIDP_DE_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_DE_BLOCK, 0xffffffff);
	malidp_hw_disable_irq(hwdev, MALIDP_DC_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_DC_BLOCK, 0xffffffff);

	ret = devm_request_threaded_irq(drm->dev, irq, malidp_de_irq,
					malidp_de_irq_thread_handler,
					IRQF_SHARED, "malidp-de", drm);
	if (ret < 0) {
		DRM_ERROR("failed to install DE IRQ handler\n");
		return ret;
	}

	malidp_de_irq_hw_init(hwdev);

	return 0;
}

void malidp_de_irq_fini(struct malidp_hw_device *hwdev)
{
	malidp_hw_disable_irq(hwdev, MALIDP_DE_BLOCK,
			      hwdev->hw->map.de_irq_map.irq_mask);
	malidp_hw_disable_irq(hwdev, MALIDP_DC_BLOCK,
			      hwdev->hw->map.dc_irq_map.irq_mask);
}

static irqreturn_t malidp_se_irq(int irq, void *arg)
{
	struct drm_device *drm = arg;
	struct malidp_drm *malidp = drm_to_malidp(drm);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct malidp_hw *hw = hwdev->hw;
	const struct malidp_irq_map *se = &hw->map.se_irq_map;
	u32 status, mask;

	/*
	 * if we are suspended it is likely that we were invoked because
	 * we share an interrupt line with some other driver, don't try
	 * to read the hardware registers
	 */
	if (hwdev->pm_suspended)
		return IRQ_NONE;

	status = malidp_hw_read(hwdev, hw->map.se_base + MALIDP_REG_STATUS);
	if (!(status & (se->irq_mask | se->err_mask)))
		return IRQ_NONE;

#ifdef CONFIG_DEBUG_FS
	if (status & se->err_mask)
		malidp_error(malidp, &malidp->se_errors, status,
			     drm_crtc_vblank_count(&malidp->crtc));
#endif
	mask = malidp_hw_read(hwdev, hw->map.se_base + MALIDP_REG_MASKIRQ);
	status &= mask;

	if (status & se->vsync_irq) {
		switch (hwdev->mw_state) {
		case MW_ONESHOT:
			drm_writeback_signal_completion(&malidp->mw_connector, 0);
			break;
		case MW_STOP:
			drm_writeback_signal_completion(&malidp->mw_connector, 0);
			/* disable writeback after stop */
			hwdev->mw_state = MW_NOT_ENABLED;
			break;
		case MW_RESTART:
			drm_writeback_signal_completion(&malidp->mw_connector, 0);
			fallthrough;	/* to a new start */
		case MW_START:
			/* writeback started, need to emulate one-shot mode */
			hw->disable_memwrite(hwdev);
			/*
			 * only set config_valid HW bit if there is no other update
			 * in progress or if we raced ahead of the DE IRQ handler
			 * and config_valid flag will not be update until later
			 */
			status = malidp_hw_read(hwdev, hw->map.dc_base + MALIDP_REG_STATUS);
			if ((atomic_read(&malidp->config_valid) != MALIDP_CONFIG_START) ||
			    (status & hw->map.dc_irq_map.vsync_irq))
				hw->set_config_valid(hwdev, 1);
			break;
		}
	}

	malidp_hw_clear_irq(hwdev, MALIDP_SE_BLOCK, status);

	return IRQ_HANDLED;
}

void malidp_se_irq_hw_init(struct malidp_hw_device *hwdev)
{
	/* ensure interrupts are disabled */
	malidp_hw_disable_irq(hwdev, MALIDP_SE_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_SE_BLOCK, 0xffffffff);

	malidp_hw_enable_irq(hwdev, MALIDP_SE_BLOCK,
			     hwdev->hw->map.se_irq_map.irq_mask);
}

static irqreturn_t malidp_se_irq_thread_handler(int irq, void *arg)
{
	return IRQ_HANDLED;
}

int malidp_se_irq_init(struct drm_device *drm, int irq)
{
	struct malidp_drm *malidp = drm_to_malidp(drm);
	struct malidp_hw_device *hwdev = malidp->dev;
	int ret;

	/* ensure interrupts are disabled */
	malidp_hw_disable_irq(hwdev, MALIDP_SE_BLOCK, 0xffffffff);
	malidp_hw_clear_irq(hwdev, MALIDP_SE_BLOCK, 0xffffffff);

	ret = devm_request_threaded_irq(drm->dev, irq, malidp_se_irq,
					malidp_se_irq_thread_handler,
					IRQF_SHARED, "malidp-se", drm);
	if (ret < 0) {
		DRM_ERROR("failed to install SE IRQ handler\n");
		return ret;
	}

	hwdev->mw_state = MW_NOT_ENABLED;
	malidp_se_irq_hw_init(hwdev);

	return 0;
}

void malidp_se_irq_fini(struct malidp_hw_device *hwdev)
{
	malidp_hw_disable_irq(hwdev, MALIDP_SE_BLOCK,
			      hwdev->hw->map.se_irq_map.irq_mask);
}
