// SPDX-License-Identifier: GPL-2.0-only
/*
 * Frame buffer driver for the Carmine GPU.
 *
 * The driver configures the GPU as follows
 * - FB0 is display 0 with unique memory area
 * - FB1 is display 1 with unique memory area
 * - both display use 32 bit colors
 */
#include <linux/aperture.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "carminefb.h"
#include "carminefb_regs.h"

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#error  "The endianness of the target host has not been defined."
#endif

/*
 * The initial video mode can be supplied via two different ways:
 * - as a string that is passed to fb_find_mode() (module option fb_mode_str)
 * - as an integer that picks the video mode from carmine_modedb[] (module
 *   option fb_mode)
 *
 * If nothing is used than the initial video mode will be the
 * CARMINEFB_DEFAULT_VIDEO_MODE member of the carmine_modedb[].
 */
#define CARMINEFB_DEFAULT_VIDEO_MODE	1

static unsigned int fb_mode = CARMINEFB_DEFAULT_VIDEO_MODE;
module_param(fb_mode, uint, 0444);
MODULE_PARM_DESC(fb_mode, "Initial video mode as integer.");

static char *fb_mode_str;
module_param(fb_mode_str, charp, 0444);
MODULE_PARM_DESC(fb_mode_str, "Initial video mode in characters.");

/*
 * Carminefb displays:
 * 0b000 None
 * 0b001 Display 0
 * 0b010 Display 1
 */
static int fb_displays = CARMINE_USE_DISPLAY0 | CARMINE_USE_DISPLAY1;
module_param(fb_displays, int, 0444);
MODULE_PARM_DESC(fb_displays, "Bit mode, which displays are used");

struct carmine_hw {
	void __iomem *v_regs;
	void __iomem *screen_mem;
	struct fb_info *fb[MAX_DISPLAY];
};

struct carmine_resolution {
	u32 htp;
	u32 hsp;
	u32 hsw;
	u32 hdp;
	u32 vtr;
	u32 vsp;
	u32 vsw;
	u32 vdp;
	u32 disp_mode;
};

struct carmine_fb {
	void __iomem *display_reg;
	void __iomem *screen_base;
	u32 smem_offset;
	u32 cur_mode;
	u32 new_mode;
	struct carmine_resolution *res;
	u32 pseudo_palette[16];
};

static struct fb_fix_screeninfo carminefb_fix = {
	.id = "Carmine",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.accel = FB_ACCEL_NONE,
};

static const struct fb_videomode carmine_modedb[] = {
	{
		.name		= "640x480",
		.xres		= 640,
		.yres		= 480,
	}, {
		.name		= "800x600",
		.xres		= 800,
		.yres		= 600,
	},
};

static struct carmine_resolution car_modes[] = {
	{
		/* 640x480 */
		.htp = 800,
		.hsp = 672,
		.hsw = 96,
		.hdp = 640,
		.vtr = 525,
		.vsp = 490,
		.vsw = 2,
		.vdp = 480,
		.disp_mode = 0x1400,
	},
	{
		/* 800x600 */
		.htp = 1060,
		.hsp = 864,
		.hsw = 72,
		.hdp = 800,
		.vtr = 628,
		.vsp = 601,
		.vsw = 2,
		.vdp = 600,
		.disp_mode = 0x0d00,
	}
};

static int carmine_find_mode(const struct fb_var_screeninfo *var)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(car_modes); i++)
		if (car_modes[i].hdp == var->xres &&
		    car_modes[i].vdp == var->yres)
			return i;
	return -EINVAL;
}

static void c_set_disp_reg(const struct carmine_fb *par,
		u32 offset, u32 val)
{
	writel(val, par->display_reg + offset);
}

static u32 c_get_disp_reg(const struct carmine_fb *par,
		u32 offset)
{
	return readl(par->display_reg + offset);
}

static void c_set_hw_reg(const struct carmine_hw *hw,
		u32 offset, u32 val)
{
	writel(val, hw->v_regs + offset);
}

static u32 c_get_hw_reg(const struct carmine_hw *hw,
		u32 offset)
{
	return readl(hw->v_regs + offset);
}

static int carmine_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info *info)
{
	if (regno >= 16)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;
	transp >>= 8;

	((__be32 *)info->pseudo_palette)[regno] = cpu_to_be32(transp << 24 |
		red << 0 | green << 8 | blue << 16);
	return 0;
}

static int carmine_check_var(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	int ret;

	ret = carmine_find_mode(var);
	if (ret < 0)
		return ret;

	if (var->grayscale || var->rotate || var->nonstd)
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	var->bits_per_pixel = 32;

#ifdef __BIG_ENDIAN
	var->transp.offset = 24;
	var->red.offset = 0;
	var->green.offset = 8;
	var->blue.offset = 16;
#else
	var->transp.offset = 24;
	var->red.offset = 16;
	var->green.offset = 8;
	var->blue.offset = 0;
#endif

	var->red.length = 8;
	var->green.length = 8;
	var->blue.length = 8;
	var->transp.length = 8;

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
}

static void carmine_init_display_param(struct carmine_fb *par)
{
	u32 width;
	u32 height;
	u32 param;
	u32 window_size;
	u32 soffset = par->smem_offset;

	c_set_disp_reg(par, CARMINE_DISP_REG_C_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_MLMR_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_CURSOR_MODE,
			CARMINE_CURSOR0_PRIORITY_MASK |
			CARMINE_CURSOR1_PRIORITY_MASK |
			CARMINE_CURSOR_CUTZ_MASK);

	/* Set default cursor position */
	c_set_disp_reg(par, CARMINE_DISP_REG_CUR1_POS, 0 << 16 | 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_CUR2_POS, 0 << 16 | 0);

	/* Set default display mode */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_EXT_MODE, CARMINE_WINDOW_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_EXT_MODE,
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_EXT_MODE, CARMINE_EXTEND_MODE |
			CARMINE_EXT_CMODE_DIRECT24_RGBA);

	/* Set default frame size to layer mode register */
	width = par->res->hdp * 4 / CARMINE_DISP_WIDTH_UNIT;
	width = width << CARMINE_DISP_WIDTH_SHIFT;

	height = par->res->vdp - 1;
	param = width | height;

	c_set_disp_reg(par, CARMINE_DISP_REG_L0_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_WIDTH, width);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_MODE_W_H, param);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_MODE_W_H, param);

	/* Set default pos and size */
	window_size = (par->res->vdp - 1) << CARMINE_DISP_WIN_H_SHIFT;
	window_size |= par->res->hdp;

	c_set_disp_reg(par, CARMINE_DISP_REG_L0_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_WIN_SIZE, window_size);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_WIN_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_WIN_SIZE, window_size);

	/* Set default origin address */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_ORG_ADR, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_ORG_ADR, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_ORG_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_ORG_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_ORG_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_ORG_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_ORG_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_ORG_ADR1, soffset);

	/* Set default display address */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_DISP_ADR, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_DISP_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_DISP_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_DISP_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_DISP_ADR1, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_DISP_ADR0, soffset);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_DISP_ADR0, soffset);

	/* Set default display position */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_DISP_POS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_DISP_POS, 0);

	/* Set default blend mode */
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L0, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L1, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L2, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L3, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L4, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L5, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L6, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_BLEND_MODE_L7, 0);

	/* default transparency mode */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L1_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6_TRANS, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7_TRANS, 0);

	/* Set default read skip parameter */
	c_set_disp_reg(par, CARMINE_DISP_REG_L0RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6RM, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7RM, 0);

	c_set_disp_reg(par, CARMINE_DISP_REG_L0PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6PX, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7PX, 0);

	c_set_disp_reg(par, CARMINE_DISP_REG_L0PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L2PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L3PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L4PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L5PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L6PY, 0);
	c_set_disp_reg(par, CARMINE_DISP_REG_L7PY, 0);
}

static void set_display_parameters(struct carmine_fb *par)
{
	u32 mode;
	u32 hdp, vdp, htp, hsp, hsw, vtr, vsp, vsw;

	/*
	 * display timing. Parameters are decreased by one because hardware
	 * spec is 0 to (n - 1)
	 * */
	hdp = par->res->hdp - 1;
	vdp = par->res->vdp - 1;
	htp = par->res->htp - 1;
	hsp = par->res->hsp - 1;
	hsw = par->res->hsw - 1;
	vtr = par->res->vtr - 1;
	vsp = par->res->vsp - 1;
	vsw = par->res->vsw - 1;

	c_set_disp_reg(par, CARMINE_DISP_REG_H_TOTAL,
			htp << CARMINE_DISP_HTP_SHIFT);
	c_set_disp_reg(par, CARMINE_DISP_REG_H_PERIOD,
			(hdp << CARMINE_DISP_HDB_SHIFT)	| hdp);
	c_set_disp_reg(par, CARMINE_DISP_REG_V_H_W_H_POS,
			(vsw << CARMINE_DISP_VSW_SHIFT) |
			(hsw << CARMINE_DISP_HSW_SHIFT) |
			(hsp));
	c_set_disp_reg(par, CARMINE_DISP_REG_V_TOTAL,
			vtr << CARMINE_DISP_VTR_SHIFT);
	c_set_disp_reg(par, CARMINE_DISP_REG_V_PERIOD_POS,
			(vdp << CARMINE_DISP_VDP_SHIFT) | vsp);

	/* clock */
	mode = c_get_disp_reg(par, CARMINE_DISP_REG_DCM1);
	mode = (mode & ~CARMINE_DISP_DCM_MASK) |
		(par->res->disp_mode & CARMINE_DISP_DCM_MASK);
	/* enable video output and layer 0 */
	mode |= CARMINE_DEN | CARMINE_L0E;
	c_set_disp_reg(par, CARMINE_DISP_REG_DCM1, mode);
}

static int carmine_set_par(struct fb_info *info)
{
	struct carmine_fb *par = info->par;
	int ret;

	ret = carmine_find_mode(&info->var);
	if (ret < 0)
		return ret;

	par->new_mode = ret;
	if (par->cur_mode != par->new_mode) {

		par->cur_mode = par->new_mode;
		par->res = &car_modes[par->new_mode];

		carmine_init_display_param(par);
		set_display_parameters(par);
	}

	info->fix.line_length = info->var.xres * info->var.bits_per_pixel / 8;
	return 0;
}

static int init_hardware(struct carmine_hw *hw)
{
	u32 flags;
	u32 loops;
	u32 ret;

	/* Initialize Carmine */
	/* Sets internal clock */
	c_set_hw_reg(hw, CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE,
			CARMINE_DFLT_IP_CLOCK_ENABLE);

	/* Video signal output is turned off */
	c_set_hw_reg(hw, CARMINE_DISP0_REG + CARMINE_DISP_REG_DCM1, 0);
	c_set_hw_reg(hw, CARMINE_DISP1_REG + CARMINE_DISP_REG_DCM1, 0);

	/* Software reset */
	c_set_hw_reg(hw, CARMINE_CTL_REG + CARMINE_CTL_REG_SOFTWARE_RESET, 1);
	c_set_hw_reg(hw, CARMINE_CTL_REG + CARMINE_CTL_REG_SOFTWARE_RESET, 0);

	/* I/O mode settings */
	flags = CARMINE_DFLT_IP_DCTL_IO_CONT1 << 16 |
		CARMINE_DFLT_IP_DCTL_IO_CONT0;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_IOCONT1_IOCONT0,
			flags);

	/* DRAM initial sequence */
	flags = CARMINE_DFLT_IP_DCTL_MODE << 16 | CARMINE_DFLT_IP_DCTL_ADD;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_MODE_ADD,
			flags);

	flags = CARMINE_DFLT_IP_DCTL_SET_TIME1 << 16 |
		CARMINE_DFLT_IP_DCTL_EMODE;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_SETTIME1_EMODE,
			flags);

	flags = CARMINE_DFLT_IP_DCTL_REFRESH << 16 |
		CARMINE_DFLT_IP_DCTL_SET_TIME2;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_REFRESH_SETTIME2,
			flags);

	flags = CARMINE_DFLT_IP_DCTL_RESERVE2 << 16 |
		CARMINE_DFLT_IP_DCTL_FIFO_DEPTH;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV2_RSV1, flags);

	flags = CARMINE_DFLT_IP_DCTL_DDRIF2 << 16 | CARMINE_DFLT_IP_DCTL_DDRIF1;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_DDRIF2_DDRIF1,
			flags);

	flags = CARMINE_DFLT_IP_DCTL_RESERVE0 << 16 |
		CARMINE_DFLT_IP_DCTL_STATES;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV0_STATES,
			flags);

	/* Executes DLL reset */
	if (CARMINE_DCTL_DLL_RESET) {
		for (loops = 0; loops < CARMINE_DCTL_INIT_WAIT_LIMIT; loops++) {

			ret = c_get_hw_reg(hw, CARMINE_DCTL_REG +
					CARMINE_DCTL_REG_RSV0_STATES);
			ret &= CARMINE_DCTL_REG_STATES_MASK;
			if (!ret)
				break;

			mdelay(CARMINE_DCTL_INIT_WAIT_INTERVAL);
		}

		if (loops >= CARMINE_DCTL_INIT_WAIT_LIMIT) {
			printk(KERN_ERR "DRAM init failed\n");
			return -EIO;
		}
	}

	flags = CARMINE_DFLT_IP_DCTL_MODE_AFT_RST << 16 |
		CARMINE_DFLT_IP_DCTL_ADD;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_MODE_ADD, flags);

	flags = CARMINE_DFLT_IP_DCTL_RESERVE0 << 16 |
		CARMINE_DFLT_IP_DCTL_STATES_AFT_RST;
	c_set_hw_reg(hw, CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV0_STATES,
			flags);

	/* Initialize the write back register */
	c_set_hw_reg(hw, CARMINE_WB_REG + CARMINE_WB_REG_WBM,
			CARMINE_WB_REG_WBM_DEFAULT);

	/* Initialize the Kottos registers */
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRINTM, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRERRM, 0);

	/* Set DC offsets */
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PX, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PY, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LX, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LY, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TX, 0);
	c_set_hw_reg(hw, CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TY, 0);
	return 0;
}

static const struct fb_ops carminefb_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,

	.fb_check_var	= carmine_check_var,
	.fb_set_par	= carmine_set_par,
	.fb_setcolreg	= carmine_setcolreg,
};

static int alloc_carmine_fb(void __iomem *regs, void __iomem *smem_base,
			    int smem_offset, struct device *device,
			    struct fb_info **rinfo)
{
	int ret;
	struct fb_info *info;
	struct carmine_fb *par;

	info = framebuffer_alloc(sizeof *par, device);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->display_reg = regs;
	par->smem_offset = smem_offset;

	info->screen_base = smem_base + smem_offset;
	info->screen_size = CARMINE_DISPLAY_MEM;
	info->fbops = &carminefb_ops;

	info->fix = carminefb_fix;
	info->pseudo_palette = par->pseudo_palette;
	info->flags = FBINFO_DEFAULT;

	ret = fb_alloc_cmap(&info->cmap, 256, 1);
	if (ret < 0)
		goto err_free_fb;

	if (fb_mode >= ARRAY_SIZE(carmine_modedb))
		fb_mode = CARMINEFB_DEFAULT_VIDEO_MODE;

	par->cur_mode = par->new_mode = ~0;

	ret = fb_find_mode(&info->var, info, fb_mode_str, carmine_modedb,
			ARRAY_SIZE(carmine_modedb),
			&carmine_modedb[fb_mode], 32);
	if (!ret || ret == 4) {
		ret = -EINVAL;
		goto err_dealloc_cmap;
	}

	fb_videomode_to_modelist(carmine_modedb, ARRAY_SIZE(carmine_modedb),
			&info->modelist);

	ret = register_framebuffer(info);
	if (ret < 0)
		goto err_dealloc_cmap;

	fb_info(info, "%s frame buffer device\n", info->fix.id);

	*rinfo = info;
	return 0;

err_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);
err_free_fb:
	framebuffer_release(info);
	return ret;
}

static void cleanup_fb_device(struct fb_info *info)
{
	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
}

static int carminefb_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{
	struct carmine_hw *hw;
	struct device *device = &dev->dev;
	struct fb_info *info;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(dev, "carminefb");
	if (ret)
		return ret;

	ret = pci_enable_device(dev);
	if (ret)
		return ret;

	ret = -ENOMEM;
	hw = kzalloc(sizeof *hw, GFP_KERNEL);
	if (!hw)
		goto err_enable_pci;

	carminefb_fix.mmio_start = pci_resource_start(dev, CARMINE_CONFIG_BAR);
	carminefb_fix.mmio_len = pci_resource_len(dev, CARMINE_CONFIG_BAR);

	if (!request_mem_region(carminefb_fix.mmio_start,
				carminefb_fix.mmio_len,
				"carminefb regbase")) {
		printk(KERN_ERR "carminefb: Can't reserve regbase.\n");
		ret = -EBUSY;
		goto err_free_hw;
	}
	hw->v_regs = ioremap(carminefb_fix.mmio_start,
			carminefb_fix.mmio_len);
	if (!hw->v_regs) {
		printk(KERN_ERR "carminefb: Can't remap %s register.\n",
				carminefb_fix.id);
		goto err_free_reg_mmio;
	}

	carminefb_fix.smem_start = pci_resource_start(dev, CARMINE_MEMORY_BAR);
	carminefb_fix.smem_len = pci_resource_len(dev, CARMINE_MEMORY_BAR);

	/* The memory area tends to be very large (256 MiB). Remap only what
	 * is required for that largest resolution to avoid remaps at run
	 * time
	 */
	if (carminefb_fix.smem_len > CARMINE_TOTAL_DIPLAY_MEM)
		carminefb_fix.smem_len = CARMINE_TOTAL_DIPLAY_MEM;

	else if (carminefb_fix.smem_len < CARMINE_TOTAL_DIPLAY_MEM) {
		printk(KERN_ERR "carminefb: Memory bar is only %d bytes, %d "
				"are required.", carminefb_fix.smem_len,
				CARMINE_TOTAL_DIPLAY_MEM);
		goto err_unmap_vregs;
	}

	if (!request_mem_region(carminefb_fix.smem_start,
				carminefb_fix.smem_len,	"carminefb smem")) {
		printk(KERN_ERR "carminefb: Can't reserve smem.\n");
		goto err_unmap_vregs;
	}

	hw->screen_mem = ioremap(carminefb_fix.smem_start,
			carminefb_fix.smem_len);
	if (!hw->screen_mem) {
		printk(KERN_ERR "carmine: Can't ioremap smem area.\n");
		goto err_reg_smem;
	}

	ret = init_hardware(hw);
	if (ret)
		goto err_unmap_screen;

	info = NULL;
	if (fb_displays & CARMINE_USE_DISPLAY0) {
		ret = alloc_carmine_fb(hw->v_regs + CARMINE_DISP0_REG,
				hw->screen_mem, CARMINE_DISPLAY_MEM * 0,
				device, &info);
		if (ret)
			goto err_deinit_hw;
	}

	hw->fb[0] = info;

	info = NULL;
	if (fb_displays & CARMINE_USE_DISPLAY1) {
		ret = alloc_carmine_fb(hw->v_regs + CARMINE_DISP1_REG,
				hw->screen_mem, CARMINE_DISPLAY_MEM * 1,
				device, &info);
		if (ret)
			goto err_cleanup_fb0;
	}

	hw->fb[1] = info;
	info = NULL;

	pci_set_drvdata(dev, hw);
	return 0;

err_cleanup_fb0:
	cleanup_fb_device(hw->fb[0]);
err_deinit_hw:
	/* disable clock, etc */
	c_set_hw_reg(hw, CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE, 0);
err_unmap_screen:
	iounmap(hw->screen_mem);
err_reg_smem:
	release_mem_region(carminefb_fix.smem_start, carminefb_fix.smem_len);
err_unmap_vregs:
	iounmap(hw->v_regs);
err_free_reg_mmio:
	release_mem_region(carminefb_fix.mmio_start, carminefb_fix.mmio_len);
err_free_hw:
	kfree(hw);
err_enable_pci:
	pci_disable_device(dev);
	return ret;
}

static void carminefb_remove(struct pci_dev *dev)
{
	struct carmine_hw *hw = pci_get_drvdata(dev);
	struct fb_fix_screeninfo fix;
	int i;

	/* in case we use only fb1 and not fb1 */
	if (hw->fb[0])
		fix = hw->fb[0]->fix;
	else
		fix = hw->fb[1]->fix;

	/* deactivate display(s) and switch clocks */
	c_set_hw_reg(hw, CARMINE_DISP0_REG + CARMINE_DISP_REG_DCM1, 0);
	c_set_hw_reg(hw, CARMINE_DISP1_REG + CARMINE_DISP_REG_DCM1, 0);
	c_set_hw_reg(hw, CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE, 0);

	for (i = 0; i < MAX_DISPLAY; i++)
		cleanup_fb_device(hw->fb[i]);

	iounmap(hw->screen_mem);
	release_mem_region(fix.smem_start, fix.smem_len);
	iounmap(hw->v_regs);
	release_mem_region(fix.mmio_start, fix.mmio_len);

	pci_disable_device(dev);
	kfree(hw);
}

#define PCI_VENDOR_ID_FUJITU_LIMITED 0x10cf
static struct pci_device_id carmine_devices[] = {
{
	PCI_DEVICE(PCI_VENDOR_ID_FUJITU_LIMITED, 0x202b)},
	{0, 0, 0, 0, 0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, carmine_devices);

static struct pci_driver carmine_pci_driver = {
	.name		= "carminefb",
	.id_table	= carmine_devices,
	.probe		= carminefb_probe,
	.remove		= carminefb_remove,
};

static int __init carminefb_init(void)
{
	if (!(fb_displays &
		(CARMINE_USE_DISPLAY0 | CARMINE_USE_DISPLAY1))) {
		printk(KERN_ERR "If you disable both displays than you don't "
				"need the driver at all\n");
		return -EINVAL;
	}
	return pci_register_driver(&carmine_pci_driver);
}
module_init(carminefb_init);

static void __exit carminefb_cleanup(void)
{
	pci_unregister_driver(&carmine_pci_driver);
}
module_exit(carminefb_cleanup);

MODULE_AUTHOR("Sebastian Siewior <bigeasy@linutronix.de>");
MODULE_DESCRIPTION("Framebuffer driver for Fujitsu Carmine based devices");
MODULE_LICENSE("GPL v2");
