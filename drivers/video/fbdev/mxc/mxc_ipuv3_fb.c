/*
 * Copyright 2004-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @defgroup Framebuffer Framebuffer Driver for SDC and ADC.
 */

/*!
 * @file mxcfb.c
 *
 * @brief MXC Frame buffer driver for SDC
 *
 * @ingroup Framebuffer
 */

/*!
 * Include files
 */
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/fsl_devices.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/ipu.h>
#include <linux/ipu-v3.h>
#include <linux/ipu-v3-pre.h>
#include <linux/ipu-v3-prg.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#include "mxc_dispdrv.h"

/*
 * Driver name
 */
#define MXCFB_NAME      "mxc_sdc_fb"

/* Display port number */
#define MXCFB_PORT_NUM	2
/*!
 * Structure containing the MXC specific framebuffer information.
 */
struct mxcfb_info {
	int default_bpp;
	int cur_blank;
	int next_blank;
	ipu_channel_t ipu_ch;
	int ipu_id;
	int ipu_di;
	int pre_num;
	u32 ipu_di_pix_fmt;
	bool ipu_int_clk;
	bool overlay;
	bool alpha_chan_en;
	bool late_init;
	bool first_set_par;
	bool resolve;
	bool prefetch;
	bool on_the_fly;
	uint32_t final_pfmt;
	unsigned long gpu_sec_buf_off;
	unsigned long base;
	uint32_t x_crop;
	uint32_t y_crop;
	unsigned int sec_buf_off;
	unsigned int trd_buf_off;
	dma_addr_t store_addr;
	dma_addr_t alpha_phy_addr0;
	dma_addr_t alpha_phy_addr1;
	void *alpha_virt_addr0;
	void *alpha_virt_addr1;
	uint32_t alpha_mem_len;
	uint32_t ipu_ch_irq;
	uint32_t ipu_ch_nf_irq;
	uint32_t ipu_alp_ch_irq;
	uint32_t cur_ipu_buf;
	uint32_t cur_ipu_alpha_buf;

	u32 pseudo_palette[16];

	bool mode_found;
	struct completion flip_complete;
	struct completion alpha_flip_complete;
	struct completion vsync_complete;
	struct completion otf_complete;	/* on the fly */

	void *ipu;
	struct fb_info *ovfbi;

	struct mxc_dispdrv_handle *dispdrv;

	struct fb_var_screeninfo cur_var;
	uint32_t cur_ipu_pfmt;
	uint32_t cur_fb_pfmt;
	bool cur_prefetch;
	spinlock_t spin_lock;	/* for PRE small yres cases */
	struct ipu_pre_context *pre_config;
};

struct mxcfb_pfmt {
	u32 fb_pix_fmt;
	int bpp;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
};

struct mxcfb_tile_block {
       u32 fb_pix_fmt;
       int bw; /* in pixel */
       int bh; /* in pixel */
};

#define NA	(~0x0UL)
static const struct mxcfb_pfmt mxcfb_pfmts[] = {
	/*     pixel         bpp    red         green        blue      transp */
	{IPU_PIX_FMT_RGB565,   16, {11, 5, 0}, { 5, 6, 0}, { 0, 5, 0}, {  0, 0, 0} },
	{IPU_PIX_FMT_BGRA4444, 16, { 8, 4, 0}, { 4, 4, 0}, { 0, 4, 0}, { 12, 4, 0} },
	{IPU_PIX_FMT_BGRA5551, 16, {10, 5, 0}, { 5, 5, 0}, { 0, 5, 0}, { 15, 1, 0} },
	{IPU_PIX_FMT_RGB24,  24, { 0, 8, 0}, { 8, 8, 0}, {16, 8, 0}, { 0, 0, 0} },
	{IPU_PIX_FMT_BGR24,  24, {16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, { 0, 0, 0} },
	{IPU_PIX_FMT_RGB32,  32, { 0, 8, 0}, { 8, 8, 0}, {16, 8, 0}, {24, 8, 0} },
	{IPU_PIX_FMT_BGR32,  32, {16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, {24, 8, 0} },
	{IPU_PIX_FMT_ABGR32, 32, {24, 8, 0}, {16, 8, 0}, { 8, 8, 0}, { 0, 8, 0} },
	/*     pixel           bpp      red         green          blue         transp */
	{IPU_PIX_FMT_YUV420P2, 12, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YUV420P,  12, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YVU420P,  12, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_NV12,     12, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{PRE_PIX_FMT_NV21,     12, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_NV16,     16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{PRE_PIX_FMT_NV61,     16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YUV422P,  16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YVU422P,  16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_UYVY,     16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YUYV,     16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YUV444,   24, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_YUV444P,  24, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_AYUV,     32, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	/*     pixel              bpp      red          green          blue         transp */
	{IPU_PIX_FMT_GPU32_SB_ST,  32, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU32_SB_SRT, 32, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU32_ST,     32, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU32_SRT,    32, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU16_SB_ST,  16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU16_SB_SRT, 16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU16_ST,     16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
	{IPU_PIX_FMT_GPU16_SRT,    16, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA}, {NA, NA, NA} },
};

/* Tile fb alignment */
static const struct mxcfb_tile_block tas[] = {
	{IPU_PIX_FMT_GPU32_SB_ST,   16,   8},
	{IPU_PIX_FMT_GPU32_SB_SRT,  64, 128},
	{IPU_PIX_FMT_GPU32_ST,      16,   4},
	{IPU_PIX_FMT_GPU32_SRT,     64,  64},
	{IPU_PIX_FMT_GPU16_SB_ST,   16,   8},
	{IPU_PIX_FMT_GPU16_SB_SRT,  64, 128},
	{IPU_PIX_FMT_GPU16_ST,      16,   4},
	{IPU_PIX_FMT_GPU16_SRT,     64,  64},
};

/* The block can be resolved */
static const struct mxcfb_tile_block trs[] = {
	/*     pixel                 w    h */
	{IPU_PIX_FMT_GPU32_SB_ST,   16,   4},
	{IPU_PIX_FMT_GPU32_SB_SRT,  16,   4},
	{IPU_PIX_FMT_GPU32_ST,      16,   4},
	{IPU_PIX_FMT_GPU32_SRT,     16,   4},
	{IPU_PIX_FMT_GPU16_SB_ST,   16,   4},
	{IPU_PIX_FMT_GPU16_SB_SRT,  16,   4},
	{IPU_PIX_FMT_GPU16_ST,      16,   4},
	{IPU_PIX_FMT_GPU16_SRT,     16,   4},
};

struct mxcfb_alloc_list {
	struct list_head list;
	dma_addr_t phy_addr;
	void *cpu_addr;
	u32 size;
};

enum {
	BOTH_ON,
	SRC_ON,
	TGT_ON,
	BOTH_OFF
};

static bool g_dp_in_use[2];
LIST_HEAD(fb_alloc_list);

/* Return default standard(RGB) pixel format */
static uint32_t bpp_to_pixfmt(int bpp)
{
	uint32_t pixfmt = 0;

	switch (bpp) {
	case 24:
		pixfmt = IPU_PIX_FMT_BGR24;
		break;
	case 32:
		pixfmt = IPU_PIX_FMT_BGR32;
		break;
	case 16:
		pixfmt = IPU_PIX_FMT_RGB565;
		break;
	}
	return pixfmt;
}

static inline int bitfield_is_equal(struct fb_bitfield f1,
				    struct fb_bitfield f2)
{
	return !memcmp(&f1, &f2, sizeof(f1));
}

static int pixfmt_to_var(uint32_t pixfmt, struct fb_var_screeninfo *var)
{
	int i, ret = -1;

	for (i = 0; i < ARRAY_SIZE(mxcfb_pfmts); i++) {
		if (pixfmt == mxcfb_pfmts[i].fb_pix_fmt) {
			var->red    = mxcfb_pfmts[i].red;
			var->green  = mxcfb_pfmts[i].green;
			var->blue   = mxcfb_pfmts[i].blue;
			var->transp = mxcfb_pfmts[i].transp;
			var->bits_per_pixel = mxcfb_pfmts[i].bpp;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int bpp_to_var(int bpp, struct fb_var_screeninfo *var)
{
	uint32_t pixfmt = 0;

	if (var->nonstd)
		return -1;

	pixfmt = bpp_to_pixfmt(bpp);
	if (pixfmt)
		return pixfmt_to_var(pixfmt, var);
	else
		return -1;
}

static int check_var_pixfmt(struct fb_var_screeninfo *var)
{
	int i, ret = -1;

	if (var->nonstd) {
		for (i = 0; i < ARRAY_SIZE(mxcfb_pfmts); i++) {
			if (mxcfb_pfmts[i].fb_pix_fmt == var->nonstd) {
				var->bits_per_pixel = mxcfb_pfmts[i].bpp;
				ret = 0;
				break;
			}
		}
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(mxcfb_pfmts); i++) {
		if (bitfield_is_equal(var->red, mxcfb_pfmts[i].red) &&
		    bitfield_is_equal(var->green, mxcfb_pfmts[i].green) &&
		    bitfield_is_equal(var->blue, mxcfb_pfmts[i].blue) &&
		    bitfield_is_equal(var->transp, mxcfb_pfmts[i].transp) &&
		    var->bits_per_pixel == mxcfb_pfmts[i].bpp) {
			ret = 0;
			break;
		}
	}
	return ret;
}

static uint32_t fb_to_store_pixfmt(uint32_t fb_pixfmt)
{
	switch (fb_pixfmt) {
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_ABGR32:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGRA4444:
	case IPU_PIX_FMT_BGRA5551:
	case IPU_PIX_FMT_UYVY:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_YUV444:
	case IPU_PIX_FMT_AYUV:
		return fb_pixfmt;
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_NV12:
	case PRE_PIX_FMT_NV21:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV61:
	case IPU_PIX_FMT_YUV420P:
		return IPU_PIX_FMT_UYVY;
	case IPU_PIX_FMT_YUV444P:
		return IPU_PIX_FMT_AYUV;
	default:
		return 0;
	}
}

static uint32_t fbi_to_pixfmt(struct fb_info *fbi, bool original_fb)
{
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;
	int i;
	uint32_t pixfmt = 0;

	if (fbi->var.nonstd) {
		if (mxc_fbi->prefetch && !original_fb) {
			if (ipu_pixel_format_is_gpu_tile(fbi->var.nonstd))
				goto next;

			return fb_to_store_pixfmt(fbi->var.nonstd);
		} else {
			return fbi->var.nonstd;
		}
	}

next:
	for (i = 0; i < ARRAY_SIZE(mxcfb_pfmts); i++) {
		if (bitfield_is_equal(fbi->var.red, mxcfb_pfmts[i].red) &&
		    bitfield_is_equal(fbi->var.green, mxcfb_pfmts[i].green) &&
		    bitfield_is_equal(fbi->var.blue, mxcfb_pfmts[i].blue) &&
		    bitfield_is_equal(fbi->var.transp, mxcfb_pfmts[i].transp)) {
			pixfmt = mxcfb_pfmts[i].fb_pix_fmt;
			break;
		}
	}

	if (pixfmt == 0)
		dev_err(fbi->device, "cannot get pixel format\n");

	return pixfmt;
}

static void fmt_to_tile_alignment(uint32_t fmt, int *bw, int *bh)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tas); i++) {
		if (tas[i].fb_pix_fmt == fmt) {
			*bw = tas[i].bw;
			*bh = tas[i].bh;
		}
	}

	BUG_ON(!(*bw) || !(*bh));
}

static void fmt_to_tile_block(uint32_t fmt, int *bw, int *bh)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trs); i++) {
		if (trs[i].fb_pix_fmt == fmt) {
			*bw = trs[i].bw;
			*bh = trs[i].bh;
		}
	}

	BUG_ON(!(*bw) || !(*bh));
}

static struct fb_info *found_registered_fb(ipu_channel_t ipu_ch, int ipu_id)
{
	int i;
	struct mxcfb_info *mxc_fbi;
	struct fb_info *fbi = NULL;

	for (i = 0; i < num_registered_fb; i++) {
		mxc_fbi =
			((struct mxcfb_info *)(registered_fb[i]->par));

		if ((mxc_fbi->ipu_ch == ipu_ch) &&
			(mxc_fbi->ipu_id == ipu_id)) {
			fbi = registered_fb[i];
			break;
		}
	}
	return fbi;
}

static irqreturn_t mxcfb_irq_handler(int irq, void *dev_id);
static irqreturn_t mxcfb_nf_irq_handler(int irq, void *dev_id);
static int mxcfb_blank(int blank, struct fb_info *info);
static int mxcfb_map_video_memory(struct fb_info *fbi);
static int mxcfb_unmap_video_memory(struct fb_info *fbi);
static int mxcfb_ioctl(struct fb_info *fbi, unsigned int cmd,
			unsigned long arg);

/*
 * Set fixed framebuffer parameters based on variable settings.
 *
 * @param       info     framebuffer information pointer
 */
static int mxcfb_set_fix(struct fb_info *info)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_var_screeninfo *var = &info->var;

	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 1;
	fix->ywrapstep = 1;
	fix->ypanstep = 1;

	return 0;
}

static int _setup_disp_channel1(struct fb_info *fbi)
{
	ipu_channel_params_t params;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;

	memset(&params, 0, sizeof(params));

	if (mxc_fbi->ipu_ch == MEM_DC_SYNC) {
		params.mem_dc_sync.di = mxc_fbi->ipu_di;
		if (fbi->var.vmode & FB_VMODE_INTERLACED)
			params.mem_dc_sync.interlaced = true;
		params.mem_dc_sync.out_pixel_fmt = mxc_fbi->ipu_di_pix_fmt;
		params.mem_dc_sync.in_pixel_fmt = mxc_fbi->on_the_fly ?
						  mxc_fbi->final_pfmt :
						  fbi_to_pixfmt(fbi, false);
	} else {
		params.mem_dp_bg_sync.di = mxc_fbi->ipu_di;
		if (fbi->var.vmode & FB_VMODE_INTERLACED)
			params.mem_dp_bg_sync.interlaced = true;
		params.mem_dp_bg_sync.out_pixel_fmt = mxc_fbi->ipu_di_pix_fmt;
		params.mem_dp_bg_sync.in_pixel_fmt = mxc_fbi->on_the_fly ?
						     mxc_fbi->final_pfmt :
						     fbi_to_pixfmt(fbi, false);
		if (mxc_fbi->alpha_chan_en)
			params.mem_dp_bg_sync.alpha_chan_en = true;
	}
	ipu_init_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch, &params);

	return 0;
}

static int _setup_disp_channel2(struct fb_info *fbi)
{
	int retval = 0;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;
	int fb_stride, ipu_stride, bw = 0, bh = 0;
	unsigned long base, ipu_base;
	unsigned int fr_xoff, fr_yoff, fr_w, fr_h;
	unsigned int prg_width;
	struct ipu_pre_context pre;
	bool post_pre_disable = false;

	switch (fbi_to_pixfmt(fbi, true)) {
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_NV12:
	case PRE_PIX_FMT_NV21:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV61:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YUV444P:
		fb_stride = fbi->var.xres_virtual;
		break;
	default:
		fb_stride = fbi->fix.line_length;
	}

	base = fbi->fix.smem_start;
	fr_xoff = fbi->var.xoffset;
	fr_w = fbi->var.xres_virtual;
	if (!(fbi->var.vmode & FB_VMODE_YWRAP)) {
		dev_dbg(fbi->device, "Y wrap disabled\n");
		fr_yoff = fbi->var.yoffset % fbi->var.yres;
		fr_h = fbi->var.yres;
		base += fbi->fix.line_length * fbi->var.yres *
			(fbi->var.yoffset / fbi->var.yres);
		if (ipu_pixel_format_is_split_gpu_tile(fbi->var.nonstd))
			base += (mxc_fbi->gpu_sec_buf_off -
				 fbi->fix.line_length * fbi->var.yres / 2) *
				(fbi->var.yoffset / fbi->var.yres);
	} else {
		dev_dbg(fbi->device, "Y wrap enabled\n");
		fr_yoff = fbi->var.yoffset;
		fr_h = fbi->var.yres_virtual;
	}

	/* pixel block alignment for resolving cases */
	if (mxc_fbi->resolve) {
		fmt_to_tile_block(fbi->var.nonstd, &bw, &bh);
	} else {
		base += fr_yoff * fb_stride + fr_xoff *
			bytes_per_pixel(fbi_to_pixfmt(fbi, true));
	}

	if (!mxc_fbi->on_the_fly)
		mxc_fbi->cur_ipu_buf = 2;
	init_completion(&mxc_fbi->flip_complete);
	/*
	 * We don't need to wait for vsync at the first time
	 * we do pan display after fb is initialized, as IPU will
	 * switch to the newly selected buffer automatically,
	 * so we call complete() for both mxc_fbi->flip_complete
	 * and mxc_fbi->alpha_flip_complete.
	 */
	if (!mxc_fbi->prefetch ||
	    (mxc_fbi->prefetch && !ipu_pre_yres_is_small(fbi->var.yres)))
		complete(&mxc_fbi->flip_complete);
	if (mxc_fbi->alpha_chan_en) {
		mxc_fbi->cur_ipu_alpha_buf = 1;
		init_completion(&mxc_fbi->alpha_flip_complete);
		complete(&mxc_fbi->alpha_flip_complete);
	}

	if (mxc_fbi->prefetch) {
		struct ipu_prg_config prg;
		struct fb_var_screeninfo from_var, to_var;

		if (mxc_fbi->pre_num < 0) {
			mxc_fbi->pre_num = ipu_pre_alloc(mxc_fbi->ipu_id,
							 mxc_fbi->ipu_ch);
			if (mxc_fbi->pre_num < 0) {
				dev_dbg(fbi->device, "failed to alloc PRE\n");
				mxc_fbi->prefetch = mxc_fbi->cur_prefetch;
				mxc_fbi->resolve = false;
				if (!mxc_fbi->on_the_fly)
					mxc_fbi->cur_blank = FB_BLANK_POWERDOWN;
				return mxc_fbi->pre_num;
			}
		}
		pre.repeat = true;
		pre.vflip = fbi->var.rotate ? true : false;
		pre.handshake_en = true;
		pre.hsk_abort_en = true;
		pre.hsk_line_num = 0;
		pre.sdw_update = true;
		pre.cur_buf = base;
		pre.next_buf = pre.cur_buf;
		if (fbi->var.vmode & FB_VMODE_INTERLACED) {
			pre.interlaced = 2;
			if (mxc_fbi->resolve) {
				pre.field_inverse = fbi->var.rotate;
				pre.interlace_offset = 0;
			} else {
				pre.field_inverse = 0;
				if (fbi->var.rotate) {
					pre.interlace_offset = ~(fbi->var.xres_virtual *
							  bytes_per_pixel(fbi_to_pixfmt(fbi, true))) + 1;
					pre.cur_buf += fbi->var.xres_virtual * bytes_per_pixel(fbi_to_pixfmt(fbi, true));
					pre.next_buf = pre.cur_buf;
				} else {
					pre.interlace_offset = fbi->var.xres_virtual *
							  bytes_per_pixel(fbi_to_pixfmt(fbi, true));
				}
			}
		} else {
			pre.interlaced = 0;
			pre.interlace_offset = 0;
		}
		pre.prefetch_mode = mxc_fbi->resolve ? 1 : 0;
		pre.tile_fmt = mxc_fbi->resolve ? fbi->var.nonstd : 0;
		pre.read_burst = mxc_fbi->resolve ? 0x4 : 0x3;
		if (fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_RGB24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_BGR24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_YUV444) {
			if ((fbi->var.xres * 3) % 8 == 0 &&
			    (fbi->var.xres_virtual * 3) % 8 == 0)
				pre.prefetch_input_bpp = 64;
			else if ((fbi->var.xres * 3) % 4 == 0 &&
				 (fbi->var.xres_virtual * 3) % 4 == 0)
				pre.prefetch_input_bpp = 32;
			else if ((fbi->var.xres * 3) % 2 == 0 &&
				 (fbi->var.xres_virtual * 3) % 2 == 0)
				pre.prefetch_input_bpp = 16;
			else
				pre.prefetch_input_bpp = 8;
		} else {
			pre.prefetch_input_bpp =
					8 * bytes_per_pixel(fbi_to_pixfmt(fbi, true));
		}
		pre.prefetch_input_pixel_fmt = mxc_fbi->resolve ?
					0x1 : (fbi->var.nonstd ? fbi->var.nonstd : 0);
		pre.shift_bypass = (mxc_fbi->on_the_fly &&
				    mxc_fbi->final_pfmt != fbi_to_pixfmt(fbi, false)) ?
					false : true;
		pixfmt_to_var(fbi_to_pixfmt(fbi, false), &from_var);
		pixfmt_to_var(mxc_fbi->final_pfmt, &to_var);
		if (mxc_fbi->on_the_fly &&
		    (format_to_colorspace(fbi_to_pixfmt(fbi, true)) == RGB) &&
		    (bytes_per_pixel(fbi_to_pixfmt(fbi, true)) == 4)) {
			pre.prefetch_shift_offset = (from_var.red.offset    << to_var.red.offset)   |
						    (from_var.green.offset  << to_var.green.offset) |
						    (from_var.blue.offset   << to_var.blue.offset)  |
						    (from_var.transp.offset << to_var.transp.offset);
			pre.prefetch_shift_width =  (to_var.red.length    << (to_var.red.offset/2))   |
						    (to_var.green.length  << (to_var.green.offset/2)) |
						    (to_var.blue.length   << (to_var.blue.offset/2))  |
						    (to_var.transp.length << (to_var.transp.offset/2));
		} else {
			pre.prefetch_shift_offset = 0;
			pre.prefetch_shift_width = 0;
		}
		pre.tpr_coor_offset_en = mxc_fbi->resolve ? true : false;
		pre.prefetch_output_size.left = mxc_fbi->resolve ? (fr_xoff & ~(bw - 1)) : 0;
		pre.prefetch_output_size.top = mxc_fbi->resolve ? (fr_yoff & ~(bh - 1)) : 0;
		if (fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_RGB24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_BGR24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_YUV444) {
			pre.prefetch_output_size.width = (fbi->var.xres * 3) /
						(pre.prefetch_input_bpp / 8);
			pre.store_output_bpp = pre.prefetch_input_bpp;
		} else {
			pre.prefetch_output_size.width = fbi->var.xres;
			pre.store_output_bpp = 8 *
					bytes_per_pixel(fbi_to_pixfmt(fbi, false));
		}
		pre.prefetch_output_size.height = fbi->var.yres;
		pre.prefetch_input_active_width = pre.prefetch_output_size.width;
		if (fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_RGB24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_BGR24 ||
		    fbi_to_pixfmt(fbi, true) == IPU_PIX_FMT_YUV444)
			pre.prefetch_input_width = (fbi->var.xres_virtual * 3) /
						   (pre.prefetch_input_bpp / 8);
		else
			pre.prefetch_input_width = fbi->var.xres_virtual;
		pre.prefetch_input_height = fbi->var.yres;
		prg_width = pre.prefetch_output_size.width;
		if (!(pre.prefetch_input_active_width % 32))
			pre.block_size = 0;
		else if (!(pre.prefetch_input_active_width % 16))
			pre.block_size = 1;
		else
			pre.block_size = 0;
		if (mxc_fbi->resolve) {
			int bs = pre.block_size ? 16 : 32;
			pre.prefetch_input_active_width += fr_xoff % bw;
			if (((fr_xoff % bw) + pre.prefetch_input_active_width) % bs)
				pre.prefetch_input_active_width =
					ALIGN(pre.prefetch_input_active_width, bs);
			pre.prefetch_output_size.width =
				pre.prefetch_input_active_width;
			prg_width = pre.prefetch_output_size.width;
			pre.prefetch_input_height += fr_yoff % bh;
			if (((fr_yoff % bh) + fbi->var.yres) % 4) {
				pre.prefetch_input_height =
					(fbi->var.vmode & FB_VMODE_INTERLACED) ?
							ALIGN(pre.prefetch_input_height, 8) :
							ALIGN(pre.prefetch_input_height, 4);
			} else {
				if (fbi->var.vmode & FB_VMODE_INTERLACED)
					pre.prefetch_input_height =
							ALIGN(pre.prefetch_input_height, 8);
			}
			pre.prefetch_output_size.height = pre.prefetch_input_height;
		}

		/* store output pitch 8-byte aligned */
		while ((pre.store_output_bpp * prg_width) % 64)
			prg_width++;

		pre.store_pitch = (pre.store_output_bpp * prg_width) / 8;

		pre.store_en = true;
		pre.write_burst = 0x3;
		ipu_get_channel_offset(fbi_to_pixfmt(fbi, true),
				       fbi->var.xres,
				       fr_h,
				       fr_w,
				       0, 0,
				       fr_yoff,
				       fr_xoff,
				       &pre.sec_buf_off,
				       &pre.trd_buf_off);
		if (mxc_fbi->resolve)
			pre.sec_buf_off = mxc_fbi->gpu_sec_buf_off;

		ipu_pre_config(mxc_fbi->pre_num, &pre);
		ipu_stride = pre.store_pitch;
		ipu_base = pre.store_addr;
		mxc_fbi->store_addr = ipu_base;

		if (mxc_fbi->cur_prefetch && mxc_fbi->on_the_fly) {
			/*
			 * Make sure any pending interrupt is handled so that
			 * the buffer panned can start to be scanned out.
			 */
			if (!ipu_pre_yres_is_small(fbi->var.yres)) {
				mxcfb_ioctl(fbi, MXCFB_WAIT_FOR_VSYNC,
						(unsigned long)fbi->par);
				mxcfb_ioctl(fbi, MXCFB_WAIT_FOR_VSYNC,
						(unsigned long)fbi->par);
			}

			mxc_fbi->pre_config = &pre;

			/*
			 * Write the PRE control register in the flip interrupt
			 * handler in this on-the-fly case to workaround the
			 * SoC design bug recorded by errata ERR009624.
			 */
			init_completion(&mxc_fbi->otf_complete);
			ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
			ipu_enable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
			retval = wait_for_completion_timeout(
						&mxc_fbi->otf_complete, HZ/2);
			if (retval == 0) {
				dev_err(fbi->device, "timeout when waiting "
						"for on the fly config irq\n");
				return -ETIMEDOUT;
			} else {
				retval = 0;
			}
		} else {
			retval = ipu_pre_set_ctrl(mxc_fbi->pre_num, &pre);
			if (retval < 0)
				return retval;
		}

		if (!mxc_fbi->on_the_fly || !mxc_fbi->cur_prefetch) {
			prg.id = mxc_fbi->ipu_id;
			prg.pre_num = mxc_fbi->pre_num;
			prg.ipu_ch = mxc_fbi->ipu_ch;
			prg.so = (fbi->var.vmode & FB_VMODE_INTERLACED) ?
				  PRG_SO_INTERLACE : PRG_SO_PROGRESSIVE;
			prg.vflip = fbi->var.rotate ? true : false;
			prg.block_mode = mxc_fbi->resolve ? PRG_BLOCK_MODE : PRG_SCAN_MODE;
			prg.stride = (fbi->var.vmode & FB_VMODE_INTERLACED) ?
				     ipu_stride * 2 : ipu_stride;
			prg.ilo = (fbi->var.vmode & FB_VMODE_INTERLACED) ?
				   ipu_stride : 0;
			prg.height = mxc_fbi->resolve ?
					pre.prefetch_output_size.height : fbi->var.yres;
			prg.ipu_height = fbi->var.yres;
			prg.crop_line = mxc_fbi->resolve ?
					((fbi->var.vmode & FB_VMODE_INTERLACED) ? (fr_yoff % bh) / 2 : fr_yoff % bh) : 0;
			prg.baddr = pre.store_addr;
			prg.offset = mxc_fbi->resolve ? (prg.crop_line * prg.stride +
				     (fr_xoff % bw) *
				     bytes_per_pixel(fbi_to_pixfmt(fbi, false))) : 0;
			ipu_base += prg.offset;
			if (ipu_base % 8) {
				dev_err(fbi->device,
					"IPU base address is not 8byte aligned\n");
				return -EINVAL;
			}
			mxc_fbi->store_addr = ipu_base;

			if (!mxc_fbi->on_the_fly) {
				retval = ipu_init_channel_buffer(mxc_fbi->ipu,
								 mxc_fbi->ipu_ch, IPU_INPUT_BUFFER,
								 fbi_to_pixfmt(fbi, false),
								 fbi->var.xres, fbi->var.yres,
								 ipu_stride,
								 fbi->var.rotate,
								 ipu_base,
								 ipu_base,
								 fbi->var.accel_flags &
									FB_ACCEL_DOUBLE_FLAG ? 0 : ipu_base,
								 0, 0);
				if (retval) {
					dev_err(fbi->device,
						"ipu_init_channel_buffer error %d\n", retval);
					return retval;
				}
			}

			retval = ipu_prg_config(&prg);
			if (retval < 0) {
				dev_err(fbi->device,
					"failed to configure PRG %d\n", retval);
				return retval;
			}

			retval = ipu_pre_enable(mxc_fbi->pre_num);
			if (retval < 0) {
				ipu_prg_disable(mxc_fbi->ipu_id, mxc_fbi->pre_num);
				dev_err(fbi->device,
					"failed to enable PRE %d\n", retval);
				return retval;
			}

			retval = ipu_prg_wait_buf_ready(mxc_fbi->ipu_id,
							mxc_fbi->pre_num,
							pre.hsk_line_num,
							pre.prefetch_output_size.height);
			if (retval < 0) {
				ipu_prg_disable(mxc_fbi->ipu_id, mxc_fbi->pre_num);
				ipu_pre_disable(mxc_fbi->pre_num);
				ipu_pre_free(&mxc_fbi->pre_num);
				dev_err(fbi->device, "failed to wait PRG ready %d\n", retval);
				return retval;
			}
		}
	} else {
		ipu_stride = fb_stride;
		ipu_base = base;
		if (mxc_fbi->on_the_fly)
			post_pre_disable = true;
	}

	if (mxc_fbi->on_the_fly && ((mxc_fbi->cur_prefetch && !mxc_fbi->prefetch) ||
	    (!mxc_fbi->cur_prefetch && mxc_fbi->prefetch))) {
		int htotal = fbi->var.xres + fbi->var.right_margin +
			     fbi->var.hsync_len + fbi->var.left_margin;
		int vtotal = fbi->var.yres + fbi->var.lower_margin +
			     fbi->var.vsync_len + fbi->var.upper_margin;
		int timeout = ((htotal * vtotal) / PICOS2KHZ(fbi->var.pixclock)) * 2 ;
		int cur_buf = 0;

		BUG_ON(timeout <= 0);

		++mxc_fbi->cur_ipu_buf;
		mxc_fbi->cur_ipu_buf %= 3;
		if (ipu_update_channel_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					      IPU_INPUT_BUFFER,
					      mxc_fbi->cur_ipu_buf,
					      ipu_base) == 0) {
			if (!mxc_fbi->prefetch)
				ipu_update_channel_offset(mxc_fbi->ipu,
						mxc_fbi->ipu_ch,
						IPU_INPUT_BUFFER,
						fbi_to_pixfmt(fbi, true),
						fr_w,
						fr_h,
						fr_w,
						0, 0,
						fr_yoff,
						fr_xoff);
			ipu_select_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					  IPU_INPUT_BUFFER, mxc_fbi->cur_ipu_buf);
			for (; timeout > 0; timeout--) {
				cur_buf = ipu_get_cur_buffer_idx(mxc_fbi->ipu,
							mxc_fbi->ipu_ch,
							IPU_INPUT_BUFFER);
				if (cur_buf == mxc_fbi->cur_ipu_buf)
					break;

				udelay(1000);
			}
			if (!timeout)
				dev_err(fbi->device, "Timeout for switch to buf %d "
					"to address=0x%08lX, current buf %d, "
					"buf0 ready %d, buf1 ready %d, buf2 ready "
					"%d\n", mxc_fbi->cur_ipu_buf, ipu_base,
					ipu_get_cur_buffer_idx(mxc_fbi->ipu,
							       mxc_fbi->ipu_ch,
							       IPU_INPUT_BUFFER),
					ipu_check_buffer_ready(mxc_fbi->ipu,
							       mxc_fbi->ipu_ch,
							       IPU_INPUT_BUFFER, 0),
					ipu_check_buffer_ready(mxc_fbi->ipu,
							       mxc_fbi->ipu_ch,
							       IPU_INPUT_BUFFER, 1),
					ipu_check_buffer_ready(mxc_fbi->ipu,
							       mxc_fbi->ipu_ch,
							       IPU_INPUT_BUFFER, 2));
		}
	} else if (!mxc_fbi->on_the_fly && !mxc_fbi->prefetch) {
		retval = ipu_init_channel_buffer(mxc_fbi->ipu,
						 mxc_fbi->ipu_ch, IPU_INPUT_BUFFER,
						 mxc_fbi->on_the_fly ? mxc_fbi->final_pfmt :
						 fbi_to_pixfmt(fbi, false),
						 fbi->var.xres, fbi->var.yres,
						 ipu_stride,
						 fbi->var.rotate,
						 ipu_base,
						 ipu_base,
						 fbi->var.accel_flags &
							FB_ACCEL_DOUBLE_FLAG ? 0 : ipu_base,
						 0, 0);
		if (retval) {
			dev_err(fbi->device,
				"ipu_init_channel_buffer error %d\n", retval);
			return retval;
		}
		/* update u/v offset */
		if (!mxc_fbi->prefetch)
			ipu_update_channel_offset(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					IPU_INPUT_BUFFER,
					fbi_to_pixfmt(fbi, true),
					fr_w,
					fr_h,
					fr_w,
					0, 0,
					fr_yoff,
					fr_xoff);
	}

	if (post_pre_disable) {
		ipu_prg_disable(mxc_fbi->ipu_id, mxc_fbi->pre_num);
		ipu_pre_disable(mxc_fbi->pre_num);
		ipu_pre_free(&mxc_fbi->pre_num);
	}

	if (mxc_fbi->alpha_chan_en) {
		retval = ipu_init_channel_buffer(mxc_fbi->ipu,
						 mxc_fbi->ipu_ch,
						 IPU_ALPHA_IN_BUFFER,
						 IPU_PIX_FMT_GENERIC,
						 fbi->var.xres, fbi->var.yres,
						 fbi->var.xres,
						 fbi->var.rotate,
						 mxc_fbi->alpha_phy_addr1,
						 mxc_fbi->alpha_phy_addr0,
						 0,
						 0, 0);
		if (retval) {
			dev_err(fbi->device,
				"ipu_init_channel_buffer error %d\n", retval);
			return retval;
		}
	}

	return retval;
}

static bool mxcfb_need_to_set_par(struct fb_info *fbi)
{
	struct mxcfb_info *mxc_fbi = fbi->par;

	if ((fbi->var.activate & FB_ACTIVATE_FORCE) &&
	    (fbi->var.activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		return true;

	/*
	 * Ignore xoffset and yoffset update,
	 * because pan display handles this case.
	 */
	mxc_fbi->cur_var.xoffset = fbi->var.xoffset;
	mxc_fbi->cur_var.yoffset = fbi->var.yoffset;

	return !!memcmp(&mxc_fbi->cur_var, &fbi->var,
			sizeof(struct fb_var_screeninfo));
}

static bool mxcfb_can_set_par_on_the_fly(struct fb_info *fbi,
					 uint32_t *final_pfmt)
{
	struct mxcfb_info *mxc_fbi = fbi->par;
	struct fb_var_screeninfo cur_var = mxc_fbi->cur_var;
	uint32_t cur_pfmt = mxc_fbi->cur_ipu_pfmt;
	uint32_t new_pfmt = fbi_to_pixfmt(fbi, false);
	uint32_t new_fb_pfmt = fbi_to_pixfmt(fbi, true);
	int cur_bpp, new_bpp, cur_bw, cur_bh, new_bw, new_bh;
	unsigned int mem_len;
	ipu_color_space_t cur_space, new_space;

	cur_space = format_to_colorspace(cur_pfmt);
	new_space = format_to_colorspace(new_pfmt);

	cur_bpp = bytes_per_pixel(cur_pfmt);
	new_bpp = bytes_per_pixel(new_pfmt);

	if (mxc_fbi->first_set_par || mxc_fbi->cur_blank != FB_BLANK_UNBLANK)
		return false;

	if (!mxc_fbi->prefetch && !mxc_fbi->cur_prefetch)
		return false;

	if (!mxc_fbi->prefetch && cur_pfmt != new_pfmt)
		return false;

	if (cur_space == RGB && (cur_bpp == 2 || cur_bpp == 3) &&
	    cur_pfmt != new_pfmt)
		return false;

	if (!(mxc_fbi->cur_prefetch && mxc_fbi->prefetch) &&
	    ((cur_var.xres_virtual != fbi->var.xres_virtual) ||
	     (cur_var.xres != cur_var.xres_virtual) ||
	     (fbi->var.xres != fbi->var.xres_virtual)))
		return false;

	if (cur_space != new_space ||
	    (new_space == RGB && cur_bpp != new_bpp))
		return false;

	if (new_space == YCbCr)
		return false;

	mem_len = fbi->var.yres_virtual * fbi->fix.line_length;
	if (mxc_fbi->resolve && mxc_fbi->gpu_sec_buf_off) {
		if (fbi->var.vmode & FB_VMODE_YWRAP)
			mem_len = mxc_fbi->gpu_sec_buf_off + mem_len / 2;
		else
			mem_len = mxc_fbi->gpu_sec_buf_off *
				 (fbi->var.yres_virtual / fbi->var.yres) + mem_len / 2;
	}
	if (mem_len > fbi->fix.smem_len)
		return false;

	if (mxc_fbi->resolve && ipu_pixel_format_is_gpu_tile(mxc_fbi->cur_fb_pfmt)) {
		fmt_to_tile_block(mxc_fbi->cur_fb_pfmt, &cur_bw, &cur_bh);
		fmt_to_tile_block(new_fb_pfmt, &new_bw, &new_bh);

		if (cur_bw != new_bw || cur_bh != new_bh ||
		    cur_var.xoffset % cur_bw != fbi->var.xoffset % new_bw ||
		    cur_var.yoffset % cur_bh != fbi->var.yoffset % new_bh)
			return false;
	} else if (mxc_fbi->resolve && mxc_fbi->cur_prefetch) {
		fmt_to_tile_block(new_fb_pfmt, &new_bw, &new_bh);
		if (fbi->var.xoffset % new_bw || fbi->var.yoffset % new_bh ||
		    fbi->var.xres % 16 || fbi->var.yres %
		     (fbi->var.vmode & FB_VMODE_INTERLACED ? 8 : 4))
			return false;
	} else if (mxc_fbi->prefetch && ipu_pixel_format_is_gpu_tile(mxc_fbi->cur_fb_pfmt)) {
		fmt_to_tile_block(mxc_fbi->cur_fb_pfmt, &cur_bw, &cur_bh);
		if (cur_var.xoffset % cur_bw || cur_var.yoffset % cur_bh ||
		    cur_var.xres % 16 || cur_var.yres %
		     (cur_var.vmode & FB_VMODE_INTERLACED ? 8 : 4))
			return false;
	}

	cur_var.xres_virtual = fbi->var.xres_virtual;
	cur_var.yres_virtual = fbi->var.yres_virtual;
	cur_var.xoffset      = fbi->var.xoffset;
	cur_var.yoffset      = fbi->var.yoffset;
	cur_var.red          = fbi->var.red;
	cur_var.green        = fbi->var.green;
	cur_var.blue         = fbi->var.blue;
	cur_var.transp       = fbi->var.transp;
	cur_var.nonstd       = fbi->var.nonstd;
	if (memcmp(&cur_var, &fbi->var,
		   sizeof(struct fb_var_screeninfo)))
		return false;

	*final_pfmt = cur_pfmt;

	return true;
}

static void mxcfb_check_resolve(struct fb_info *fbi)
{
	struct mxcfb_info *mxc_fbi = fbi->par;

	switch (fbi->var.nonstd) {
	case IPU_PIX_FMT_GPU32_ST:
	case IPU_PIX_FMT_GPU32_SRT:
	case IPU_PIX_FMT_GPU16_ST:
	case IPU_PIX_FMT_GPU16_SRT:
		mxc_fbi->gpu_sec_buf_off = 0;
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
		mxc_fbi->prefetch = true;
		mxc_fbi->resolve = true;
		break;
	default:
		mxc_fbi->resolve = false;
	}
}

static void mxcfb_check_yuv(struct fb_info *fbi)
{
	struct mxcfb_info *mxc_fbi = fbi->par;

	if (fbi->var.vmode & FB_VMODE_INTERLACED) {
		if (ipu_pixel_format_is_multiplanar_yuv(fbi_to_pixfmt(fbi, true)))
			mxc_fbi->prefetch = false;
	} else {
		if (fbi->var.nonstd == PRE_PIX_FMT_NV21 ||
		    fbi->var.nonstd == PRE_PIX_FMT_NV61)
			mxc_fbi->prefetch = true;
	}
}

/*
 * Set framebuffer parameters and change the operating mode.
 *
 * @param       info     framebuffer information pointer
 */
static int mxcfb_set_par(struct fb_info *fbi)
{
	int retval = 0;
	u32 mem_len, alpha_mem_len;
	ipu_di_signal_cfg_t sig_cfg;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;
	uint32_t final_pfmt = 0;
	int16_t ov_pos_x = 0, ov_pos_y = 0;
	int ov_pos_ret = 0;
	struct mxcfb_info *mxc_fbi_fg = NULL;
	bool ovfbi_enable = false, on_the_fly;

	if (ipu_ch_param_bad_alpha_pos(fbi_to_pixfmt(fbi, true)) &&
	    mxc_fbi->alpha_chan_en) {
		dev_err(fbi->device, "Bad pixel format for "
				"graphics plane fb\n");
		return -EINVAL;
	}

	if (mxc_fbi->ovfbi)
		mxc_fbi_fg = (struct mxcfb_info *)mxc_fbi->ovfbi->par;

	if (mxc_fbi->ovfbi && mxc_fbi_fg)
		if (mxc_fbi_fg->next_blank == FB_BLANK_UNBLANK)
			ovfbi_enable = true;

	if (!mxcfb_need_to_set_par(fbi))
		return 0;

	dev_dbg(fbi->device, "Reconfiguring framebuffer\n");

	if (fbi->var.xres == 0 || fbi->var.yres == 0)
		return 0;

	mxcfb_set_fix(fbi);

	mxcfb_check_resolve(fbi);

	mxcfb_check_yuv(fbi);

	on_the_fly = mxcfb_can_set_par_on_the_fly(fbi, &final_pfmt);
	mxc_fbi->on_the_fly = on_the_fly;
	mxc_fbi->final_pfmt = final_pfmt;

	if (on_the_fly)
		dev_dbg(fbi->device, "Reconfiguring framebuffer on the fly\n");

	if (ovfbi_enable) {
		ov_pos_ret = ipu_disp_get_window_pos(
						mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch,
						&ov_pos_x, &ov_pos_y);
		if (ov_pos_ret < 0)
			dev_err(fbi->device, "Get overlay pos failed, dispdrv:%s.\n",
					mxc_fbi->dispdrv->drv->name);

		if (!on_the_fly) {
			ipu_clear_irq(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch_irq);
			ipu_disable_irq(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch_irq);
			ipu_clear_irq(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch_nf_irq);
			ipu_disable_irq(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch_nf_irq);
			ipu_disable_channel(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch, true);
			ipu_uninit_channel(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch);
			if (mxc_fbi_fg->cur_prefetch) {
				ipu_prg_disable(mxc_fbi_fg->ipu_id, mxc_fbi_fg->pre_num);
				ipu_pre_disable(mxc_fbi_fg->pre_num);
				ipu_pre_free(&mxc_fbi_fg->pre_num);
			}
		}
	}

	if (!on_the_fly) {
		ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
		ipu_disable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
		ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_nf_irq);
		ipu_disable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_nf_irq);
		ipu_disable_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch, true);
		ipu_uninit_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch);
		if (mxc_fbi->cur_prefetch) {
			ipu_prg_disable(mxc_fbi->ipu_id, mxc_fbi->pre_num);
			ipu_pre_disable(mxc_fbi->pre_num);
			ipu_pre_free(&mxc_fbi->pre_num);
		}
	}

	/*
	 * Disable IPU hsp clock if it is enabled for an
	 * additional time in ipu common driver.
	 */
	if (mxc_fbi->first_set_par && mxc_fbi->late_init)
		ipu_disable_hsp_clk(mxc_fbi->ipu);

	mem_len = fbi->var.yres_virtual * fbi->fix.line_length;
	if (mxc_fbi->resolve && mxc_fbi->gpu_sec_buf_off) {
		if (fbi->var.vmode & FB_VMODE_YWRAP)
			mem_len = mxc_fbi->gpu_sec_buf_off + mem_len / 2;
		else
			mem_len = mxc_fbi->gpu_sec_buf_off *
				 (fbi->var.yres_virtual / fbi->var.yres) + mem_len / 2;
	}
	if (!fbi->fix.smem_start || (mem_len > fbi->fix.smem_len)) {
		if (fbi->fix.smem_start)
			mxcfb_unmap_video_memory(fbi);

		if (mxcfb_map_video_memory(fbi) < 0)
			return -ENOMEM;
	}

	if (mxc_fbi->first_set_par) {
		/*
		 * Clear the screen in case uboot fb pixel format is not
		 * the same to kernel fb pixel format.
		 */
		if (mxc_fbi->late_init)
			memset((char *)fbi->screen_base, 0, fbi->fix.smem_len);

		mxc_fbi->first_set_par = false;
	}

	if (mxc_fbi->alpha_chan_en) {
		alpha_mem_len = fbi->var.xres * fbi->var.yres;
		if ((!mxc_fbi->alpha_phy_addr0 && !mxc_fbi->alpha_phy_addr1) ||
		    (alpha_mem_len > mxc_fbi->alpha_mem_len)) {
			if (mxc_fbi->alpha_phy_addr0)
				dma_free_coherent(fbi->device,
						  mxc_fbi->alpha_mem_len,
						  mxc_fbi->alpha_virt_addr0,
						  mxc_fbi->alpha_phy_addr0);
			if (mxc_fbi->alpha_phy_addr1)
				dma_free_coherent(fbi->device,
						  mxc_fbi->alpha_mem_len,
						  mxc_fbi->alpha_virt_addr1,
						  mxc_fbi->alpha_phy_addr1);

			mxc_fbi->alpha_virt_addr0 =
					dma_alloc_coherent(fbi->device,
						  alpha_mem_len,
						  &mxc_fbi->alpha_phy_addr0,
						  GFP_DMA | GFP_KERNEL);

			mxc_fbi->alpha_virt_addr1 =
					dma_alloc_coherent(fbi->device,
						  alpha_mem_len,
						  &mxc_fbi->alpha_phy_addr1,
						  GFP_DMA | GFP_KERNEL);
			if (mxc_fbi->alpha_virt_addr0 == NULL ||
			    mxc_fbi->alpha_virt_addr1 == NULL) {
				dev_err(fbi->device, "mxcfb: dma alloc for"
					" alpha buffer failed.\n");
				if (mxc_fbi->alpha_virt_addr0)
					dma_free_coherent(fbi->device,
						  mxc_fbi->alpha_mem_len,
						  mxc_fbi->alpha_virt_addr0,
						  mxc_fbi->alpha_phy_addr0);
				if (mxc_fbi->alpha_virt_addr1)
					dma_free_coherent(fbi->device,
						  mxc_fbi->alpha_mem_len,
						  mxc_fbi->alpha_virt_addr1,
						  mxc_fbi->alpha_phy_addr1);
				return -ENOMEM;
			}
			mxc_fbi->alpha_mem_len = alpha_mem_len;
		}
	}

	if (mxc_fbi->next_blank != FB_BLANK_UNBLANK)
		return retval;

	if (!on_the_fly && mxc_fbi->dispdrv && mxc_fbi->dispdrv->drv->setup) {
		retval = mxc_fbi->dispdrv->drv->setup(mxc_fbi->dispdrv, fbi);
		if (retval < 0) {
			dev_err(fbi->device, "setup error, dispdrv:%s.\n",
					mxc_fbi->dispdrv->drv->name);
			return -EINVAL;
		}
	}

	if (!on_the_fly) {
		_setup_disp_channel1(fbi);
		if (ovfbi_enable)
			_setup_disp_channel1(mxc_fbi->ovfbi);
	}

	if (!mxc_fbi->overlay && !on_the_fly) {
		uint32_t out_pixel_fmt;

		memset(&sig_cfg, 0, sizeof(sig_cfg));
		if (fbi->var.vmode & FB_VMODE_INTERLACED)
			sig_cfg.interlaced = true;
		out_pixel_fmt = mxc_fbi->ipu_di_pix_fmt;
		if (fbi->var.vmode & FB_VMODE_ODD_FLD_FIRST) /* PAL */
			sig_cfg.odd_field_first = true;
		if (mxc_fbi->ipu_int_clk)
			sig_cfg.int_clk = true;
		if (fbi->var.sync & FB_SYNC_HOR_HIGH_ACT)
			sig_cfg.Hsync_pol = true;
		if (fbi->var.sync & FB_SYNC_VERT_HIGH_ACT)
			sig_cfg.Vsync_pol = true;
		if (!(fbi->var.sync & FB_SYNC_CLK_LAT_FALL))
			sig_cfg.clk_pol = true;
		if (fbi->var.sync & FB_SYNC_DATA_INVERT)
			sig_cfg.data_pol = true;
		if (!(fbi->var.sync & FB_SYNC_OE_LOW_ACT))
			sig_cfg.enable_pol = true;
		if (fbi->var.sync & FB_SYNC_CLK_IDLE_EN)
			sig_cfg.clkidle_en = true;

		dev_dbg(fbi->device, "pixclock = %ul Hz\n",
			(u32) (PICOS2KHZ(fbi->var.pixclock) * 1000UL));

		if (ipu_init_sync_panel(mxc_fbi->ipu, mxc_fbi->ipu_di,
					(PICOS2KHZ(fbi->var.pixclock)) * 1000UL,
					fbi->var.xres, fbi->var.yres,
					out_pixel_fmt,
					fbi->var.left_margin,
					fbi->var.hsync_len,
					fbi->var.right_margin,
					fbi->var.upper_margin,
					fbi->var.vsync_len,
					fbi->var.lower_margin,
					0, sig_cfg) != 0) {
			dev_err(fbi->device,
				"mxcfb: Error initializing panel.\n");
			return -EINVAL;
		}

		fbi->mode =
		    (struct fb_videomode *)fb_match_mode(&fbi->var,
							 &fbi->modelist);

		ipu_disp_set_window_pos(mxc_fbi->ipu, mxc_fbi->ipu_ch, 0, 0);
	}

	retval = _setup_disp_channel2(fbi);
	if (retval) {
		if (!on_the_fly)
			ipu_uninit_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch);
		return retval;
	}

	if (ovfbi_enable && !on_the_fly) {
		if (ov_pos_ret >= 0)
			ipu_disp_set_window_pos(
					mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch,
					ov_pos_x, ov_pos_y);
		retval = _setup_disp_channel2(mxc_fbi->ovfbi);
		if (retval) {
			ipu_uninit_channel(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch);
			ipu_uninit_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch);
			return retval;
		}
	}

	if (!on_the_fly) {
		ipu_enable_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch);
		if (ovfbi_enable)
			ipu_enable_channel(mxc_fbi_fg->ipu, mxc_fbi_fg->ipu_ch);
	}

	if (!on_the_fly && mxc_fbi->dispdrv && mxc_fbi->dispdrv->drv->enable) {
		retval = mxc_fbi->dispdrv->drv->enable(mxc_fbi->dispdrv, fbi);
		if (retval < 0) {
			dev_err(fbi->device, "enable error, dispdrv:%s.\n",
					mxc_fbi->dispdrv->drv->name);
			return -EINVAL;
		}
	}

	mxc_fbi->cur_var = fbi->var;
	mxc_fbi->cur_ipu_pfmt = on_the_fly ? mxc_fbi->final_pfmt :
					     fbi_to_pixfmt(fbi, false);
	mxc_fbi->cur_fb_pfmt = fbi_to_pixfmt(fbi, true);
	mxc_fbi->cur_prefetch = mxc_fbi->prefetch;

	return retval;
}

static int _swap_channels(struct fb_info *fbi_from,
			  struct fb_info *fbi_to, bool both_on)
{
	int retval, tmp;
	ipu_channel_t old_ch;
	struct fb_info *ovfbi;
	struct mxcfb_info *mxc_fbi_from = (struct mxcfb_info *)fbi_from->par;
	struct mxcfb_info *mxc_fbi_to = (struct mxcfb_info *)fbi_to->par;

	if (both_on) {
		ipu_disable_channel(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch, true);
		ipu_uninit_channel(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch);
	}

	/* switch the mxc fbi parameters */
	old_ch = mxc_fbi_from->ipu_ch;
	mxc_fbi_from->ipu_ch = mxc_fbi_to->ipu_ch;
	mxc_fbi_to->ipu_ch = old_ch;
	tmp = mxc_fbi_from->ipu_ch_irq;
	mxc_fbi_from->ipu_ch_irq = mxc_fbi_to->ipu_ch_irq;
	mxc_fbi_to->ipu_ch_irq = tmp;
	tmp = mxc_fbi_from->ipu_ch_nf_irq;
	mxc_fbi_from->ipu_ch_nf_irq = mxc_fbi_to->ipu_ch_nf_irq;
	mxc_fbi_to->ipu_ch_nf_irq = tmp;
	ovfbi = mxc_fbi_from->ovfbi;
	mxc_fbi_from->ovfbi = mxc_fbi_to->ovfbi;
	mxc_fbi_to->ovfbi = ovfbi;

	_setup_disp_channel1(fbi_from);
	retval = _setup_disp_channel2(fbi_from);
	if (retval)
		return retval;

	/* switch between dp and dc, disable old idmac, enable new idmac */
	retval = ipu_swap_channel(mxc_fbi_from->ipu, old_ch, mxc_fbi_from->ipu_ch);
	ipu_uninit_channel(mxc_fbi_from->ipu, old_ch);

	if (both_on) {
		_setup_disp_channel1(fbi_to);
		retval = _setup_disp_channel2(fbi_to);
		if (retval)
			return retval;
		ipu_enable_channel(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch);
	}

	return retval;
}

static int swap_channels(struct fb_info *fbi_from)
{
	int i;
	int swap_mode;
	ipu_channel_t ch_to;
	struct mxcfb_info *mxc_fbi_from = (struct mxcfb_info *)fbi_from->par;
	struct fb_info *fbi_to = NULL;
	struct mxcfb_info *mxc_fbi_to;

	/* what's the target channel? */
	if (mxc_fbi_from->ipu_ch == MEM_BG_SYNC)
		ch_to = MEM_DC_SYNC;
	else
		ch_to = MEM_BG_SYNC;

	fbi_to = found_registered_fb(ch_to, mxc_fbi_from->ipu_id);
	if (!fbi_to)
		return -1;
	mxc_fbi_to = (struct mxcfb_info *)fbi_to->par;

	ipu_clear_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_irq);
	ipu_clear_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_irq);
	ipu_free_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_irq, fbi_from);
	ipu_free_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_irq, fbi_to);
	ipu_clear_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_nf_irq);
	ipu_clear_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_nf_irq);
	ipu_free_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_nf_irq, fbi_from);
	ipu_free_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_nf_irq, fbi_to);

	if (mxc_fbi_from->cur_blank == FB_BLANK_UNBLANK) {
		if (mxc_fbi_to->cur_blank == FB_BLANK_UNBLANK)
			swap_mode = BOTH_ON;
		else
			swap_mode = SRC_ON;
	} else {
		if (mxc_fbi_to->cur_blank == FB_BLANK_UNBLANK)
			swap_mode = TGT_ON;
		else
			swap_mode = BOTH_OFF;
	}

	switch (swap_mode) {
	case BOTH_ON:
		/* disable target->switch src->enable target */
		_swap_channels(fbi_from, fbi_to, true);
		break;
	case SRC_ON:
		/* just switch src */
		_swap_channels(fbi_from, fbi_to, false);
		break;
	case TGT_ON:
		/* just switch target */
		_swap_channels(fbi_to, fbi_from, false);
		break;
	case BOTH_OFF:
		/* switch directly, no more need to do */
		mxc_fbi_to->ipu_ch = mxc_fbi_from->ipu_ch;
		mxc_fbi_from->ipu_ch = ch_to;
		i = mxc_fbi_from->ipu_ch_irq;
		mxc_fbi_from->ipu_ch_irq = mxc_fbi_to->ipu_ch_irq;
		mxc_fbi_to->ipu_ch_irq = i;
		i = mxc_fbi_from->ipu_ch_nf_irq;
		mxc_fbi_from->ipu_ch_nf_irq = mxc_fbi_to->ipu_ch_nf_irq;
		mxc_fbi_to->ipu_ch_nf_irq = i;
		break;
	default:
		break;
	}

	if (ipu_request_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_irq,
		mxcfb_irq_handler, IPU_IRQF_ONESHOT,
		MXCFB_NAME, fbi_from) != 0) {
		dev_err(fbi_from->device, "Error registering irq %d\n",
			mxc_fbi_from->ipu_ch_irq);
		return -EBUSY;
	}
	ipu_disable_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_irq);
	if (ipu_request_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_irq,
		mxcfb_irq_handler, IPU_IRQF_ONESHOT,
		MXCFB_NAME, fbi_to) != 0) {
		dev_err(fbi_to->device, "Error registering irq %d\n",
			mxc_fbi_to->ipu_ch_irq);
		return -EBUSY;
	}
	ipu_disable_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_irq);
	if (ipu_request_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_nf_irq,
		mxcfb_nf_irq_handler, IPU_IRQF_ONESHOT,
		MXCFB_NAME, fbi_from) != 0) {
		dev_err(fbi_from->device, "Error registering irq %d\n",
			mxc_fbi_from->ipu_ch_nf_irq);
		return -EBUSY;
	}
	ipu_disable_irq(mxc_fbi_from->ipu, mxc_fbi_from->ipu_ch_nf_irq);
	if (ipu_request_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_nf_irq,
		mxcfb_nf_irq_handler, IPU_IRQF_ONESHOT,
		MXCFB_NAME, fbi_to) != 0) {
		dev_err(fbi_to->device, "Error registering irq %d\n",
			mxc_fbi_to->ipu_ch_nf_irq);
		return -EBUSY;
	}
	ipu_disable_irq(mxc_fbi_to->ipu, mxc_fbi_to->ipu_ch_nf_irq);

	return 0;
}

/*
 * Check framebuffer variable parameters and adjust to valid values.
 *
 * @param       var      framebuffer variable parameters
 *
 * @param       info     framebuffer information pointer
 */
static int mxcfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 vtotal;
	u32 htotal;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)info->par;
	struct fb_info tmp_fbi;
	unsigned int fr_xoff, fr_yoff, fr_w, fr_h, line_length;
	unsigned long base = 0;
	int ret, bw = 0, bh = 0;
	bool triple_buffer = false;

	if (var->xres == 0 || var->yres == 0)
		return 0;

	/* fg should not bigger than bg */
	if (mxc_fbi->ipu_ch == MEM_FG_SYNC) {
		struct fb_info *fbi_tmp;
		int bg_xres = 0, bg_yres = 0;
		int16_t pos_x, pos_y;

		bg_xres = var->xres;
		bg_yres = var->yres;

		fbi_tmp = found_registered_fb(MEM_BG_SYNC, mxc_fbi->ipu_id);
		if (!fbi_tmp) {
			dev_err(info->device,
				"cannot find background fb for overlay fb\n");
			return -EINVAL;
		}

		bg_xres = fbi_tmp->var.xres;
		bg_yres = fbi_tmp->var.yres;

		ipu_disp_get_window_pos(mxc_fbi->ipu, mxc_fbi->ipu_ch, &pos_x, &pos_y);

		if ((var->xres + pos_x) > bg_xres)
			var->xres = bg_xres - pos_x;
		if ((var->yres + pos_y) > bg_yres)
			var->yres = bg_yres - pos_y;

		if (fbi_tmp->var.vmode & FB_VMODE_INTERLACED)
			var->vmode |= FB_VMODE_INTERLACED;
		else
			var->vmode &= ~FB_VMODE_INTERLACED;

		var->pixclock = fbi_tmp->var.pixclock;
		var->right_margin = fbi_tmp->var.right_margin;
		var->hsync_len = fbi_tmp->var.hsync_len;
		var->left_margin = fbi_tmp->var.left_margin +
				   fbi_tmp->var.xres - var->xres;
		var->upper_margin = fbi_tmp->var.upper_margin;
		var->vsync_len = fbi_tmp->var.vsync_len;
		var->lower_margin = fbi_tmp->var.lower_margin +
				    fbi_tmp->var.yres - var->yres;
	}

	if (var->rotate > IPU_ROTATE_VERT_FLIP)
		var->rotate = IPU_ROTATE_NONE;

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	if (var->yres_virtual < var->yres) {
		var->yres_virtual = var->yres * 3;
		triple_buffer = true;
	}

	if ((var->bits_per_pixel != 32) && (var->bits_per_pixel != 24) &&
	    (var->bits_per_pixel != 16) && (var->bits_per_pixel != 12) &&
	    (var->bits_per_pixel != 8))
		var->bits_per_pixel = 16;

	if (check_var_pixfmt(var)) {
		/* Fall back to default */
		ret = bpp_to_var(var->bits_per_pixel, var);
		if (ret < 0)
			return ret;
	}

	if (ipu_pixel_format_is_gpu_tile(var->nonstd)) {
		fmt_to_tile_alignment(var->nonstd, &bw, &bh);
		var->xres_virtual = ALIGN(var->xres_virtual, bw);
		if (triple_buffer)
			var->yres_virtual = 3 * ALIGN(var->yres, bh);
		else
			var->yres_virtual = ALIGN(var->yres_virtual, bh);
	}

	line_length = var->xres_virtual * var->bits_per_pixel / 8;
	fr_xoff = var->xoffset;
	fr_w = var->xres_virtual;
	if (!(var->vmode & FB_VMODE_YWRAP)) {
		fr_yoff = var->yoffset % var->yres;
		fr_h = var->yres;
		base = line_length * var->yres *
		       (var->yoffset / var->yres);
		if (ipu_pixel_format_is_split_gpu_tile(var->nonstd))
			base += (mxc_fbi->gpu_sec_buf_off -
				 line_length * var->yres / 2) *
				(var->yoffset / var->yres);
	} else {
		fr_yoff = var->yoffset;
		fr_h = var->yres_virtual;
	}

	tmp_fbi.device = info->device;
	tmp_fbi.var = *var;
	tmp_fbi.par = mxc_fbi;
	if (ipu_pixel_format_is_gpu_tile(var->nonstd)) {
		unsigned int crop_line, prg_width = var->xres, offset;
		int ipu_stride, prg_stride, bs;
		bool tmp_prefetch = mxc_fbi->prefetch;

		if (!(var->xres % 32))
			bs = 32;
		else if (!(var->xres % 16))
			bs = 16;
		else
			bs = 32;

		prg_width += fr_xoff % bw;
		if (((fr_xoff % bw) + prg_width) % bs)
			prg_width = ALIGN(prg_width, bs);

		mxc_fbi->prefetch = true;
		ipu_stride = prg_width *
				bytes_per_pixel(fbi_to_pixfmt(&tmp_fbi, false));

		if (var->vmode & FB_VMODE_INTERLACED) {
			if ((fr_yoff % bh) % 2) {
				dev_err(info->device,
					"wrong crop value in interlaced mode\n");
				return -EINVAL;
			}
			crop_line = (fr_yoff % bh) / 2;
			prg_stride = ipu_stride * 2;
		} else {
			crop_line = fr_yoff % bh;
			prg_stride = ipu_stride;
		}

		offset = crop_line * prg_stride +
			 (fr_xoff % bw) *
			 bytes_per_pixel(fbi_to_pixfmt(&tmp_fbi, false));
		mxc_fbi->prefetch = tmp_prefetch;
		if (offset % 8) {
			dev_err(info->device,
				"IPU base address is not 8byte aligned\n");
			return -EINVAL;
		}
	} else {
		unsigned int uoff = 0, voff = 0;
		int fb_stride;

		switch (fbi_to_pixfmt(&tmp_fbi, true)) {
		case IPU_PIX_FMT_YUV420P2:
		case IPU_PIX_FMT_YVU420P:
		case IPU_PIX_FMT_NV12:
		case PRE_PIX_FMT_NV21:
		case IPU_PIX_FMT_NV16:
		case PRE_PIX_FMT_NV61:
		case IPU_PIX_FMT_YUV422P:
		case IPU_PIX_FMT_YVU422P:
		case IPU_PIX_FMT_YUV420P:
		case IPU_PIX_FMT_YUV444P:
			fb_stride = var->xres_virtual;
			break;
		default:
			fb_stride = line_length;
		}
		base += fr_yoff * fb_stride +
			fr_xoff * var->bits_per_pixel / 8;

		ipu_get_channel_offset(fbi_to_pixfmt(&tmp_fbi, true),
				       var->xres,
				       fr_h,
				       fr_w,
				       0, 0,
				       fr_yoff,
				       fr_xoff,
				       &uoff,
				       &voff);
		if (base % 8 || uoff % 8 || voff % 8) {
			dev_err(info->device,
				"IPU base address is not 8byte aligned\n");
			return -EINVAL;
		}
	}

	if (var->pixclock < 1000) {
		htotal = var->xres + var->right_margin + var->hsync_len +
		    var->left_margin;
		vtotal = var->yres + var->lower_margin + var->vsync_len +
		    var->upper_margin;
		var->pixclock = (vtotal * htotal * 6UL) / 100UL;
		var->pixclock = KHZ2PICOS(var->pixclock);
		dev_dbg(info->device,
			"pixclock set for 60Hz refresh = %u ps\n",
			var->pixclock);
	}

	var->height = -1;
	var->width = -1;
	var->grayscale = 0;

	return 0;
}

static inline u_int _chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int mxcfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int trans, struct fb_info *fbi)
{
	unsigned int val;
	int ret = 1;

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no matter what visual we are using.
	 */
	if (fbi->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
				      7471 * blue) >> 16;
	switch (fbi->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 *pal = fbi->pseudo_palette;

			val = _chan_to_field(red, &fbi->var.red);
			val |= _chan_to_field(green, &fbi->var.green);
			val |= _chan_to_field(blue, &fbi->var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}

	return ret;
}

/*
 * Function to handle custom ioctls for MXC framebuffer.
 *
 * @param       inode   inode struct
 *
 * @param       file    file struct
 *
 * @param       cmd     Ioctl command to handle
 *
 * @param       arg     User pointer to command arguments
 *
 * @param       fbi     framebuffer information pointer
 */
static int mxcfb_ioctl(struct fb_info *fbi, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int __user *argp = (void __user *)arg;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;

	switch (cmd) {
	case MXCFB_SET_GBL_ALPHA:
		{
			struct mxcfb_gbl_alpha ga;

			if (copy_from_user(&ga, (void *)arg, sizeof(ga))) {
				retval = -EFAULT;
				break;
			}

			if (ipu_disp_set_global_alpha(mxc_fbi->ipu,
						      mxc_fbi->ipu_ch,
						      (bool)ga.enable,
						      ga.alpha)) {
				retval = -EINVAL;
				break;
			}

			if (ga.enable)
				mxc_fbi->alpha_chan_en = false;

			if (ga.enable)
				dev_dbg(fbi->device,
					"Set global alpha of %s to %d\n",
					fbi->fix.id, ga.alpha);
			break;
		}
	case MXCFB_SET_LOC_ALPHA:
		{
			struct mxcfb_loc_alpha la;
			bool bad_pixfmt =
				ipu_ch_param_bad_alpha_pos(fbi_to_pixfmt(fbi, true));

			if (copy_from_user(&la, (void *)arg, sizeof(la))) {
				retval = -EFAULT;
				break;
			}

			if (la.enable && !la.alpha_in_pixel) {
				struct fb_info *fbi_tmp;
				ipu_channel_t ipu_ch;

				if (bad_pixfmt) {
					dev_err(fbi->device, "Bad pixel format "
						"for graphics plane fb\n");
					retval = -EINVAL;
					break;
				}

				mxc_fbi->alpha_chan_en = true;

				if (mxc_fbi->ipu_ch == MEM_FG_SYNC)
					ipu_ch = MEM_BG_SYNC;
				else if (mxc_fbi->ipu_ch == MEM_BG_SYNC)
					ipu_ch = MEM_FG_SYNC;
				else {
					retval = -EINVAL;
					break;
				}

				fbi_tmp = found_registered_fb(ipu_ch, mxc_fbi->ipu_id);
				if (fbi_tmp)
					((struct mxcfb_info *)(fbi_tmp->par))->alpha_chan_en = false;
			} else
				mxc_fbi->alpha_chan_en = false;

			if (ipu_disp_set_global_alpha(mxc_fbi->ipu,
						      mxc_fbi->ipu_ch,
						      !(bool)la.enable, 0)) {
				retval = -EINVAL;
				break;
			}

			fbi->var.activate = (fbi->var.activate & ~FB_ACTIVATE_MASK) |
						FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
			mxcfb_set_par(fbi);

			la.alpha_phy_addr0 = mxc_fbi->alpha_phy_addr0;
			la.alpha_phy_addr1 = mxc_fbi->alpha_phy_addr1;
			if (copy_to_user((void *)arg, &la, sizeof(la))) {
				retval = -EFAULT;
				break;
			}

			if (la.enable)
				dev_dbg(fbi->device,
					"Enable DP local alpha for %s\n",
					fbi->fix.id);
			break;
		}
	case MXCFB_SET_LOC_ALP_BUF:
		{
			unsigned long base;
			uint32_t ipu_alp_ch_irq;

			if (!(((mxc_fbi->ipu_ch == MEM_FG_SYNC) ||
			     (mxc_fbi->ipu_ch == MEM_BG_SYNC)) &&
			     (mxc_fbi->alpha_chan_en))) {
				dev_err(fbi->device,
					"Should use background or overlay "
					"framebuffer to set the alpha buffer "
					"number\n");
				return -EINVAL;
			}

			if (get_user(base, argp))
				return -EFAULT;

			if (base != mxc_fbi->alpha_phy_addr0 &&
			    base != mxc_fbi->alpha_phy_addr1) {
				dev_err(fbi->device,
					"Wrong alpha buffer physical address "
					"%lu\n", base);
				return -EINVAL;
			}

			if (mxc_fbi->ipu_ch == MEM_FG_SYNC)
				ipu_alp_ch_irq = IPU_IRQ_FG_ALPHA_SYNC_EOF;
			else
				ipu_alp_ch_irq = IPU_IRQ_BG_ALPHA_SYNC_EOF;

			retval = wait_for_completion_timeout(
				&mxc_fbi->alpha_flip_complete, HZ/2);
			if (retval == 0) {
				dev_err(fbi->device, "timeout when waiting for alpha flip irq\n");
				retval = -ETIMEDOUT;
				break;
			}

			mxc_fbi->cur_ipu_alpha_buf =
						!mxc_fbi->cur_ipu_alpha_buf;
			if (ipu_update_channel_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch,
						      IPU_ALPHA_IN_BUFFER,
						      mxc_fbi->
							cur_ipu_alpha_buf,
						      base) == 0) {
				ipu_select_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch,
						  IPU_ALPHA_IN_BUFFER,
						  mxc_fbi->cur_ipu_alpha_buf);
				ipu_clear_irq(mxc_fbi->ipu, ipu_alp_ch_irq);
				ipu_enable_irq(mxc_fbi->ipu, ipu_alp_ch_irq);
			} else {
				dev_err(fbi->device,
					"Error updating %s SDC alpha buf %d "
					"to address=0x%08lX\n",
					fbi->fix.id,
					mxc_fbi->cur_ipu_alpha_buf, base);
			}
			break;
		}
	case MXCFB_SET_CLR_KEY:
		{
			struct mxcfb_color_key key;
			if (copy_from_user(&key, (void *)arg, sizeof(key))) {
				retval = -EFAULT;
				break;
			}
			retval = ipu_disp_set_color_key(mxc_fbi->ipu, mxc_fbi->ipu_ch,
							key.enable,
							key.color_key);
			dev_dbg(fbi->device, "Set color key to 0x%08X\n",
				key.color_key);
			break;
		}
	case MXCFB_SET_GAMMA:
		{
			struct mxcfb_gamma gamma;
			if (copy_from_user(&gamma, (void *)arg, sizeof(gamma))) {
				retval = -EFAULT;
				break;
			}
			retval = ipu_disp_set_gamma_correction(mxc_fbi->ipu,
							mxc_fbi->ipu_ch,
							gamma.enable,
							gamma.constk,
							gamma.slopek);
			break;
		}
	case MXCFB_SET_GPU_SPLIT_FMT:
		{
			struct mxcfb_gpu_split_fmt fmt;

			if (copy_from_user(&fmt, (void *)arg, sizeof(fmt))) {
				retval = -EFAULT;
				break;
			}

			if (fmt.var.nonstd != IPU_PIX_FMT_GPU32_SB_ST &&
			    fmt.var.nonstd != IPU_PIX_FMT_GPU32_SB_SRT &&
			    fmt.var.nonstd != IPU_PIX_FMT_GPU16_SB_ST &&
			    fmt.var.nonstd != IPU_PIX_FMT_GPU16_SB_SRT) {
				retval = -EINVAL;
				break;
			}

			mxc_fbi->gpu_sec_buf_off = fmt.offset;
			fmt.var.activate = (fbi->var.activate & ~FB_ACTIVATE_MASK) |
						FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
			console_lock();
			fbi->flags |= FBINFO_MISC_USEREVENT;
			retval = fb_set_var(fbi, &fmt.var);
			fbi->flags &= ~FBINFO_MISC_USEREVENT;
			console_unlock();
			break;
		}
	case MXCFB_SET_PREFETCH:
		{
			int enable;

			if (copy_from_user(&enable, (void *)arg, sizeof(enable))) {
				retval = -EFAULT;
				break;
			}

			if (!enable) {
				if (ipu_pixel_format_is_gpu_tile(fbi_to_pixfmt(fbi, true))) {
					dev_err(fbi->device, "Cannot disable prefetch in "
						"resolving mode\n");
					retval = -EINVAL;
					break;
				}
				if (ipu_pixel_format_is_pre_yuv(fbi_to_pixfmt(fbi, true))) {
					dev_err(fbi->device, "Cannot disable prefetch when "
						"PRE gets NV61 or NV21\n");
					retval = -EINVAL;
					break;
				}
			} else {
				if ((fbi->var.vmode & FB_VMODE_INTERLACED) &&
				    ipu_pixel_format_is_multiplanar_yuv(fbi_to_pixfmt(fbi, true))) {
					dev_err(fbi->device, "Cannot enable prefetch when "
						"PRE gets multiplanar YUV frames\n");
					retval = -EINVAL;
					break;
				}
			}

			retval = mxcfb_check_var(&fbi->var, fbi);
			if (retval)
				break;

			mxc_fbi->prefetch = !!enable;

			if (mxc_fbi->cur_prefetch == mxc_fbi->prefetch)
				break;

			fbi->var.activate = (fbi->var.activate & ~FB_ACTIVATE_MASK) |
						FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
			retval = mxcfb_set_par(fbi);
			break;
		}
	case MXCFB_GET_PREFETCH:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (put_user(mxc_fbi->cur_prefetch, argp))
				return -EFAULT;
			break;
		}
	case MXCFB_WAIT_FOR_VSYNC:
		{
			if (mxc_fbi->ipu_ch == MEM_FG_SYNC) {
				/* BG should poweron */
				struct mxcfb_info *bg_mxcfbi = NULL;
				struct fb_info *fbi_tmp;

				fbi_tmp = found_registered_fb(MEM_BG_SYNC, mxc_fbi->ipu_id);
				if (fbi_tmp)
					bg_mxcfbi = ((struct mxcfb_info *)(fbi_tmp->par));

				if (!bg_mxcfbi) {
					retval = -EINVAL;
					break;
				}
				if (bg_mxcfbi->cur_blank != FB_BLANK_UNBLANK) {
					retval = -EINVAL;
					break;
				}
			}
			if (mxc_fbi->cur_blank != FB_BLANK_UNBLANK) {
				retval = -EINVAL;
				break;
			}

			init_completion(&mxc_fbi->vsync_complete);
			ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_nf_irq);
			ipu_enable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_nf_irq);
			retval = wait_for_completion_interruptible_timeout(
				&mxc_fbi->vsync_complete, 1 * HZ);
			if (retval == 0) {
				dev_err(fbi->device,
					"MXCFB_WAIT_FOR_VSYNC: timeout %d\n",
					retval);
				retval = -ETIME;
			} else if (retval > 0) {
				retval = 0;
			}
			break;
		}
	case FBIO_ALLOC:
		{
			int size;
			struct mxcfb_alloc_list *mem;

			mem = kzalloc(sizeof(*mem), GFP_KERNEL);
			if (mem == NULL)
				return -ENOMEM;

			if (get_user(size, argp)) {
				kfree(mem);
				return -EFAULT;
			}

			mem->size = PAGE_ALIGN(size);

			mem->cpu_addr = dma_alloc_coherent(fbi->device, size,
							   &mem->phy_addr,
							   GFP_KERNEL);
			if (mem->cpu_addr == NULL) {
				kfree(mem);
				return -ENOMEM;
			}

			list_add(&mem->list, &fb_alloc_list);

			if (put_user(mem->phy_addr, argp)) {
				list_del(&mem->list);
				dma_free_coherent(fbi->device,
						  mem->size,
						  mem->cpu_addr,
						  mem->phy_addr);
				kfree(mem);
				return -EFAULT;
			}

			dev_dbg(fbi->device, "allocated %d bytes @ 0x%08X\n",
				mem->size, mem->phy_addr);

			break;
		}
	case FBIO_FREE:
		{
			unsigned long offset;
			struct mxcfb_alloc_list *mem;

			if (get_user(offset, argp))
				return -EFAULT;

			retval = -EINVAL;
			list_for_each_entry(mem, &fb_alloc_list, list) {
				if (mem->phy_addr == offset) {
					list_del(&mem->list);
					dma_free_coherent(fbi->device,
							  mem->size,
							  mem->cpu_addr,
							  mem->phy_addr);
					kfree(mem);
					retval = 0;
					break;
				}
			}

			break;
		}
	case MXCFB_SET_OVERLAY_POS:
		{
			struct mxcfb_pos pos;
			struct fb_info *bg_fbi = NULL;
			struct mxcfb_info *bg_mxcfbi = NULL;

			if (mxc_fbi->ipu_ch != MEM_FG_SYNC) {
				dev_err(fbi->device, "Should use the overlay "
					"framebuffer to set the position of "
					"the overlay window\n");
				retval = -EINVAL;
				break;
			}

			if (copy_from_user(&pos, (void *)arg, sizeof(pos))) {
				retval = -EFAULT;
				break;
			}

			bg_fbi = found_registered_fb(MEM_BG_SYNC, mxc_fbi->ipu_id);
			if (bg_fbi)
				bg_mxcfbi = ((struct mxcfb_info *)(bg_fbi->par));

			if (bg_fbi == NULL) {
				dev_err(fbi->device, "Cannot find the "
					"background framebuffer\n");
				retval = -ENOENT;
				break;
			}

			/* if fb is unblank, check if the pos fit the display */
			if (mxc_fbi->cur_blank == FB_BLANK_UNBLANK) {
				if (fbi->var.xres + pos.x > bg_fbi->var.xres) {
					if (bg_fbi->var.xres < fbi->var.xres)
						pos.x = 0;
					else
						pos.x = bg_fbi->var.xres - fbi->var.xres;
				}
				if (fbi->var.yres + pos.y > bg_fbi->var.yres) {
					if (bg_fbi->var.yres < fbi->var.yres)
						pos.y = 0;
					else
						pos.y = bg_fbi->var.yres - fbi->var.yres;
				}
			}

			retval = ipu_disp_set_window_pos(mxc_fbi->ipu, mxc_fbi->ipu_ch,
							 pos.x, pos.y);

			if (copy_to_user((void *)arg, &pos, sizeof(pos))) {
				retval = -EFAULT;
				break;
			}
			break;
		}
	case MXCFB_GET_FB_IPU_CHAN:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (put_user(mxc_fbi->ipu_ch, argp))
				return -EFAULT;
			break;
		}
	case MXCFB_GET_DIFMT:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (put_user(mxc_fbi->ipu_di_pix_fmt, argp))
				return -EFAULT;
			break;
		}
	case MXCFB_GET_FB_IPU_DI:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (put_user(mxc_fbi->ipu_di, argp))
				return -EFAULT;
			break;
		}
	case MXCFB_GET_FB_BLANK:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (put_user(mxc_fbi->cur_blank, argp))
				return -EFAULT;
			break;
		}
	case MXCFB_SET_DIFMT:
		{
			struct mxcfb_info *mxc_fbi =
				(struct mxcfb_info *)fbi->par;

			if (get_user(mxc_fbi->ipu_di_pix_fmt, argp))
				return -EFAULT;

			break;
		}
	case MXCFB_CSC_UPDATE:
		{
			struct mxcfb_csc_matrix csc;

			if (copy_from_user(&csc, (void *) arg, sizeof(csc)))
				return -EFAULT;

			if ((mxc_fbi->ipu_ch != MEM_FG_SYNC) &&
				(mxc_fbi->ipu_ch != MEM_BG_SYNC) &&
				(mxc_fbi->ipu_ch != MEM_BG_ASYNC0))
				return -EFAULT;
			ipu_set_csc_coefficients(mxc_fbi->ipu, mxc_fbi->ipu_ch,
						csc.param);
			break;
		}
	default:
		retval = -EINVAL;
	}
	return retval;
}

/*
 * mxcfb_blank():
 *      Blank the display.
 */
static int mxcfb_blank(int blank, struct fb_info *info)
{
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)info->par;
	int ret = 0;

	dev_dbg(info->device, "blank = %d\n", blank);

	if (mxc_fbi->cur_blank == blank)
		return 0;

	mxc_fbi->next_blank = blank;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		if (mxc_fbi->dispdrv && mxc_fbi->dispdrv->drv->disable)
			mxc_fbi->dispdrv->drv->disable(mxc_fbi->dispdrv, info);
		ipu_disable_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch, true);
		if (mxc_fbi->ipu_di >= 0)
			ipu_uninit_sync_panel(mxc_fbi->ipu, mxc_fbi->ipu_di);
		ipu_uninit_channel(mxc_fbi->ipu, mxc_fbi->ipu_ch);
		if (mxc_fbi->cur_prefetch) {
			ipu_prg_disable(mxc_fbi->ipu_id, mxc_fbi->pre_num);
			ipu_pre_disable(mxc_fbi->pre_num);
			ipu_pre_free(&mxc_fbi->pre_num);
		}
		break;
	case FB_BLANK_UNBLANK:
		info->var.activate = (info->var.activate & ~FB_ACTIVATE_MASK) |
				FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
		ret = mxcfb_set_par(info);
		break;
	}
	if (!ret)
		mxc_fbi->cur_blank = blank;
	return ret;
}

/*
 * Pan or Wrap the Display
 *
 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 *
 * @param               var     Variable screen buffer information
 * @param               info    Framebuffer information pointer
 */
static int
mxcfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)info->par,
			  *mxc_graphic_fbi = NULL;
	u_int y_bottom;
	unsigned int fr_xoff, fr_yoff, fr_w, fr_h;
	unsigned long base, ipu_base = 0, active_alpha_phy_addr = 0;
	bool loc_alpha_en = false;
	int fb_stride;
	int bw = 0, bh = 0;
	int i;
	int ret;

	/* no pan display during fb blank */
	if (mxc_fbi->ipu_ch == MEM_FG_SYNC) {
		struct mxcfb_info *bg_mxcfbi = NULL;
		struct fb_info *fbi_tmp;

		fbi_tmp = found_registered_fb(MEM_BG_SYNC, mxc_fbi->ipu_id);
		if (fbi_tmp)
			bg_mxcfbi = ((struct mxcfb_info *)(fbi_tmp->par));
		if (!bg_mxcfbi)
			return -EINVAL;
		if (bg_mxcfbi->cur_blank != FB_BLANK_UNBLANK)
			return -EINVAL;
	}
	if (mxc_fbi->cur_blank != FB_BLANK_UNBLANK)
		return -EINVAL;

	if (mxc_fbi->resolve) {
		fmt_to_tile_block(info->var.nonstd, &bw, &bh);

		if (mxc_fbi->cur_var.xoffset % bw != var->xoffset % bw ||
		    mxc_fbi->cur_var.yoffset % bh != var->yoffset % bh) {
			dev_err(info->device, "do not support panning "
				"with tile crop settings changed\n");
			return -EINVAL;
		}
	}

	y_bottom = var->yoffset;

	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	switch (fbi_to_pixfmt(info, true)) {
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_NV12:
	case PRE_PIX_FMT_NV21:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV61:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YUV444P:
		fb_stride = info->var.xres_virtual;
		break;
	default:
		fb_stride = info->fix.line_length;
	}

	base = info->fix.smem_start;
	fr_xoff = var->xoffset;
	fr_w = info->var.xres_virtual;
	if (!(var->vmode & FB_VMODE_YWRAP)) {
		dev_dbg(info->device, "Y wrap disabled\n");
		fr_yoff = var->yoffset % info->var.yres;
		fr_h = info->var.yres;
		base += info->fix.line_length * info->var.yres *
			(var->yoffset / info->var.yres);
		if (ipu_pixel_format_is_split_gpu_tile(var->nonstd))
			base += (mxc_fbi->gpu_sec_buf_off -
				 info->fix.line_length * info->var.yres / 2) *
				(var->yoffset / info->var.yres);
	} else {
		dev_dbg(info->device, "Y wrap enabled\n");
		fr_yoff = var->yoffset;
		fr_h = info->var.yres_virtual;
	}
	if (!mxc_fbi->resolve) {
		base += fr_yoff * fb_stride + fr_xoff *
			bytes_per_pixel(fbi_to_pixfmt(info, true));

		if (mxc_fbi->cur_prefetch && (info->var.vmode & FB_VMODE_INTERLACED))
			base += info->var.rotate ?
				fr_w * bytes_per_pixel(fbi_to_pixfmt(info, true)) : 0;
	}

	if (mxc_fbi->cur_prefetch) {
		unsigned long lock_flags = 0;

		if (ipu_pre_yres_is_small(info->var.yres))
			/*
			 * Update the PRE buffer address in the flip interrupt
			 * handler in this case to workaround the SoC design
			 * bug recorded by errata ERR009624.
			 */
			spin_lock_irqsave(&mxc_fbi->spin_lock, lock_flags);

		if (mxc_fbi->resolve) {
			mxc_fbi->x_crop = fr_xoff & ~(bw - 1);
			mxc_fbi->y_crop = fr_yoff & ~(bh - 1);
		} else {
			mxc_fbi->x_crop = 0;
			mxc_fbi->y_crop = 0;
		}

		ipu_get_channel_offset(fbi_to_pixfmt(info, true),
				       info->var.xres,
				       fr_h,
				       fr_w,
				       0, 0,
				       fr_yoff,
				       fr_xoff,
				       &mxc_fbi->sec_buf_off,
				       &mxc_fbi->trd_buf_off);
		if (mxc_fbi->resolve)
			mxc_fbi->sec_buf_off = mxc_fbi->gpu_sec_buf_off;

		if (ipu_pre_yres_is_small(info->var.yres)) {
			mxc_fbi->base = base;
			spin_unlock_irqrestore(&mxc_fbi->spin_lock, lock_flags);
		}
	} else {
		ipu_base = base;
	}

	/* Check if DP local alpha is enabled and find the graphic fb */
	if (mxc_fbi->ipu_ch == MEM_BG_SYNC || mxc_fbi->ipu_ch == MEM_FG_SYNC) {
		for (i = 0; i < num_registered_fb; i++) {
			char bg_id[] = "DISP3 BG";
			char fg_id[] = "DISP3 FG";
			char *idstr = registered_fb[i]->fix.id;
			bg_id[4] += mxc_fbi->ipu_id;
			fg_id[4] += mxc_fbi->ipu_id;
			if ((strcmp(idstr, bg_id) == 0 ||
			     strcmp(idstr, fg_id) == 0) &&
			    ((struct mxcfb_info *)
			      (registered_fb[i]->par))->alpha_chan_en) {
				loc_alpha_en = true;
				mxc_graphic_fbi = (struct mxcfb_info *)
						(registered_fb[i]->par);
				active_alpha_phy_addr =
					mxc_fbi->cur_ipu_alpha_buf ?
					mxc_graphic_fbi->alpha_phy_addr1 :
					mxc_graphic_fbi->alpha_phy_addr0;
				dev_dbg(info->device, "Updating SDC alpha "
					"buf %d address=0x%08lX\n",
					!mxc_fbi->cur_ipu_alpha_buf,
					active_alpha_phy_addr);
				break;
			}
		}
	}

	if (!mxc_fbi->cur_prefetch ||
	    (mxc_fbi->cur_prefetch && !ipu_pre_yres_is_small(info->var.yres))) {
		ret = wait_for_completion_timeout(&mxc_fbi->flip_complete,
						  HZ/2);
		if (ret == 0) {
			dev_err(info->device, "timeout when waiting for flip "
						"irq\n");
			return -ETIMEDOUT;
		}
	}

	if (!mxc_fbi->cur_prefetch) {
		++mxc_fbi->cur_ipu_buf;
		mxc_fbi->cur_ipu_buf %= 3;
		dev_dbg(info->device, "Updating SDC %s buf %d address=0x%08lX\n",
			info->fix.id, mxc_fbi->cur_ipu_buf, base);
	}
	mxc_fbi->cur_ipu_alpha_buf = !mxc_fbi->cur_ipu_alpha_buf;

	if (mxc_fbi->cur_prefetch)
		goto next;

	if (ipu_update_channel_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch, IPU_INPUT_BUFFER,
				      mxc_fbi->cur_ipu_buf, ipu_base) == 0) {
next:
		/* Update the DP local alpha buffer only for graphic plane */
		if (loc_alpha_en && mxc_graphic_fbi == mxc_fbi &&
		    ipu_update_channel_buffer(mxc_graphic_fbi->ipu, mxc_graphic_fbi->ipu_ch,
					      IPU_ALPHA_IN_BUFFER,
					      mxc_fbi->cur_ipu_alpha_buf,
					      active_alpha_phy_addr) == 0) {
			ipu_select_buffer(mxc_graphic_fbi->ipu, mxc_graphic_fbi->ipu_ch,
					  IPU_ALPHA_IN_BUFFER,
					  mxc_fbi->cur_ipu_alpha_buf);
		}

		/* update u/v offset */
		if (!mxc_fbi->cur_prefetch) {
			ipu_update_channel_offset(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					IPU_INPUT_BUFFER,
					fbi_to_pixfmt(info, true),
					fr_w,
					fr_h,
					fr_w,
					0, 0,
					fr_yoff,
					fr_xoff);

			ipu_select_buffer(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					  IPU_INPUT_BUFFER, mxc_fbi->cur_ipu_buf);
		} else if (!ipu_pre_yres_is_small(info->var.yres)) {
			ipu_pre_set_fb_buffer(mxc_fbi->pre_num,
					      mxc_fbi->resolve,
					      base, info->var.yres,
					      mxc_fbi->x_crop,
					      mxc_fbi->y_crop,
					      mxc_fbi->sec_buf_off,
					      mxc_fbi->trd_buf_off);
		}
		ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
		ipu_enable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
	} else {
		dev_err(info->device,
			"Error updating SDC buf %d to address=0x%08lX, "
			"current buf %d, buf0 ready %d, buf1 ready %d, "
			"buf2 ready %d\n", mxc_fbi->cur_ipu_buf, base,
			ipu_get_cur_buffer_idx(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					       IPU_INPUT_BUFFER),
			ipu_check_buffer_ready(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					       IPU_INPUT_BUFFER, 0),
			ipu_check_buffer_ready(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					       IPU_INPUT_BUFFER, 1),
			ipu_check_buffer_ready(mxc_fbi->ipu, mxc_fbi->ipu_ch,
					       IPU_INPUT_BUFFER, 2));
		if (!mxc_fbi->cur_prefetch) {
			++mxc_fbi->cur_ipu_buf;
			mxc_fbi->cur_ipu_buf %= 3;
			++mxc_fbi->cur_ipu_buf;
			mxc_fbi->cur_ipu_buf %= 3;
		}
		mxc_fbi->cur_ipu_alpha_buf = !mxc_fbi->cur_ipu_alpha_buf;
		ipu_clear_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
		ipu_enable_irq(mxc_fbi->ipu, mxc_fbi->ipu_ch_irq);
		return -EBUSY;
	}

	if (mxc_fbi->cur_prefetch && ipu_pre_yres_is_small(info->var.yres)) {
		ret = wait_for_completion_timeout(&mxc_fbi->flip_complete,
						  HZ/2);
		if (ret == 0) {
			dev_err(info->device, "timeout when waiting for flip "
						"irq\n");
			return -ETIMEDOUT;
		}
	}

	dev_dbg(info->device, "Update complete\n");

	info->var.yoffset = var->yoffset;
	mxc_fbi->cur_var.xoffset = var->xoffset;
	mxc_fbi->cur_var.yoffset = var->yoffset;

	return 0;
}

/*
 * Function to handle custom mmap for MXC framebuffer.
 *
 * @param       fbi     framebuffer information pointer
 *
 * @param       vma     Pointer to vm_area_struct
 */
static int mxcfb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	bool found = false;
	u32 len;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct mxcfb_alloc_list *mem;
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;

	if (offset < fbi->fix.smem_len) {
		/* mapping framebuffer memory */
		len = fbi->fix.smem_len - offset;
		vma->vm_pgoff = (fbi->fix.smem_start + offset) >> PAGE_SHIFT;
	} else if ((vma->vm_pgoff ==
			(mxc_fbi->alpha_phy_addr0 >> PAGE_SHIFT)) ||
		   (vma->vm_pgoff ==
			(mxc_fbi->alpha_phy_addr1 >> PAGE_SHIFT))) {
		len = mxc_fbi->alpha_mem_len;
	} else {
		list_for_each_entry(mem, &fb_alloc_list, list) {
			if (offset == mem->phy_addr) {
				found = true;
				len = mem->size;
				break;
			}
		}
		if (!found)
			return -EINVAL;
	}

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len)
		return -EINVAL;

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	vma->vm_flags |= VM_IO;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dev_dbg(fbi->device, "mmap remap_pfn_range failed\n");
		return -ENOBUFS;
	}

	return 0;
}

/*!
 * This structure contains the pointers to the control functions that are
 * invoked by the core framebuffer driver to perform operations like
 * blitting, rectangle filling, copy regions and cursor definition.
 */
static struct fb_ops mxcfb_ops = {
	.owner = THIS_MODULE,
	.fb_set_par = mxcfb_set_par,
	.fb_check_var = mxcfb_check_var,
	.fb_setcolreg = mxcfb_setcolreg,
	.fb_pan_display = mxcfb_pan_display,
	.fb_ioctl = mxcfb_ioctl,
	.fb_mmap = mxcfb_mmap,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_blank = mxcfb_blank,
};

static irqreturn_t mxcfb_irq_handler(int irq, void *dev_id)
{
	struct fb_info *fbi = dev_id;
	struct mxcfb_info *mxc_fbi = fbi->par;

	if (mxc_fbi->pre_config) {
		ipu_pre_set_ctrl(mxc_fbi->pre_num, mxc_fbi->pre_config);
		mxc_fbi->pre_config = NULL;
		complete(&mxc_fbi->otf_complete);
		return IRQ_HANDLED;
	}

	if (mxc_fbi->cur_prefetch && ipu_pre_yres_is_small(fbi->var.yres)) {
		spin_lock(&mxc_fbi->spin_lock);
		ipu_pre_set_fb_buffer(mxc_fbi->pre_num,
				      mxc_fbi->resolve,
				      mxc_fbi->base, fbi->var.yres,
				      mxc_fbi->x_crop, mxc_fbi->y_crop,
				      mxc_fbi->sec_buf_off,
				      mxc_fbi->trd_buf_off);
		spin_unlock(&mxc_fbi->spin_lock);
	}

	complete(&mxc_fbi->flip_complete);
	return IRQ_HANDLED;
}

static irqreturn_t mxcfb_nf_irq_handler(int irq, void *dev_id)
{
	struct fb_info *fbi = dev_id;
	struct mxcfb_info *mxc_fbi = fbi->par;

	complete(&mxc_fbi->vsync_complete);
	return IRQ_HANDLED;
}

static irqreturn_t mxcfb_alpha_irq_handler(int irq, void *dev_id)
{
	struct fb_info *fbi = dev_id;
	struct mxcfb_info *mxc_fbi = fbi->par;

	complete(&mxc_fbi->alpha_flip_complete);
	return IRQ_HANDLED;
}

/*
 * Suspends the framebuffer and blanks the screen. Power management support
 */
static int mxcfb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fb_info *fbi = platform_get_drvdata(pdev);
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;
	int saved_blank;
#ifdef CONFIG_FB_MXC_LOW_PWR_DISPLAY
	void *fbmem;
#endif

	if (mxc_fbi->ovfbi) {
		struct mxcfb_info *mxc_fbi_fg =
			(struct mxcfb_info *)mxc_fbi->ovfbi->par;

		console_lock();
		fb_set_suspend(mxc_fbi->ovfbi, 1);
		saved_blank = mxc_fbi_fg->cur_blank;
		mxcfb_blank(FB_BLANK_POWERDOWN, mxc_fbi->ovfbi);
		mxc_fbi_fg->next_blank = saved_blank;
		console_unlock();
	}

	console_lock();
	fb_set_suspend(fbi, 1);
	saved_blank = mxc_fbi->cur_blank;
	mxcfb_blank(FB_BLANK_POWERDOWN, fbi);
	mxc_fbi->next_blank = saved_blank;
	console_unlock();

	return 0;
}

/*
 * Resumes the framebuffer and unblanks the screen. Power management support
 */
static int mxcfb_resume(struct platform_device *pdev)
{
	struct fb_info *fbi = platform_get_drvdata(pdev);
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;

	console_lock();
	mxcfb_blank(mxc_fbi->next_blank, fbi);
	fb_set_suspend(fbi, 0);
	console_unlock();

	if (mxc_fbi->ovfbi) {
		struct mxcfb_info *mxc_fbi_fg =
			(struct mxcfb_info *)mxc_fbi->ovfbi->par;
		console_lock();
		mxcfb_blank(mxc_fbi_fg->next_blank, mxc_fbi->ovfbi);
		fb_set_suspend(mxc_fbi->ovfbi, 0);
		console_unlock();
	}

	return 0;
}

/*
 * Main framebuffer functions
 */

/*!
 * Allocates the DRAM memory for the frame buffer.      This buffer is remapped
 * into a non-cached, non-buffered, memory region to allow palette and pixel
 * writes to occur without flushing the cache.  Once this area is remapped,
 * all virtual memory access to the video memory should occur at the new region.
 *
 * @param       fbi     framebuffer information pointer
 *
 * @return      Error code indicating success or failure
 */
static int mxcfb_map_video_memory(struct fb_info *fbi)
{
	struct mxcfb_info *mxc_fbi = (struct mxcfb_info *)fbi->par;

	if (fbi->fix.smem_len < fbi->var.yres_virtual * fbi->fix.line_length)
		fbi->fix.smem_len = fbi->var.yres_virtual *
				    fbi->fix.line_length;

	if (mxc_fbi->resolve && mxc_fbi->gpu_sec_buf_off) {
		if (fbi->var.vmode & FB_VMODE_YWRAP)
			fbi->fix.smem_len = mxc_fbi->gpu_sec_buf_off +
					fbi->fix.smem_len / 2;
		else
			fbi->fix.smem_len = mxc_fbi->gpu_sec_buf_off *
					(fbi->var.yres_virtual / fbi->var.yres) +
					fbi->fix.smem_len / 2;
	}

	fbi->screen_base = dma_alloc_writecombine(fbi->device,
				fbi->fix.smem_len,
				(dma_addr_t *)&fbi->fix.smem_start,
				GFP_DMA | GFP_KERNEL);
	if (fbi->screen_base == 0) {
		dev_err(fbi->device, "Unable to allocate framebuffer memory\n");
		fbi->fix.smem_len = 0;
		fbi->fix.smem_start = 0;
		return -EBUSY;
	}

	dev_dbg(fbi->device, "allocated fb @ paddr=0x%08X, size=%d.\n",
		(uint32_t) fbi->fix.smem_start, fbi->fix.smem_len);

	fbi->screen_size = fbi->fix.smem_len;

	/* Clear the screen */
	memset((char *)fbi->screen_base, 0, fbi->fix.smem_len);

	return 0;
}

/*!
 * De-allocates the DRAM memory for the frame buffer.
 *
 * @param       fbi     framebuffer information pointer
 *
 * @return      Error code indicating success or failure
 */
static int mxcfb_unmap_video_memory(struct fb_info *fbi)
{
	dma_free_writecombine(fbi->device, fbi->fix.smem_len,
			      fbi->screen_base, fbi->fix.smem_start);
	fbi->screen_base = 0;
	fbi->fix.smem_start = 0;
	fbi->fix.smem_len = 0;
	return 0;
}

/*!
 * Initializes the framebuffer information pointer. After allocating
 * sufficient memory for the framebuffer structure, the fields are
 * filled with custom information passed in from the configurable
 * structures.  This includes information such as bits per pixel,
 * color maps, screen width/height and RGBA offsets.
 *
 * @return      Framebuffer structure initialized with our information
 */
static struct fb_info *mxcfb_init_fbinfo(struct device *dev, struct fb_ops *ops)
{
	struct fb_info *fbi;
	struct mxcfb_info *mxcfbi;
	struct ipuv3_fb_platform_data *plat_data = dev->platform_data;

	/*
	 * Allocate sufficient memory for the fb structure
	 */
	fbi = framebuffer_alloc(sizeof(struct mxcfb_info), dev);
	if (!fbi)
		return NULL;

	mxcfbi = (struct mxcfb_info *)fbi->par;

	fbi->var.activate = FB_ACTIVATE_NOW;

	bpp_to_var(plat_data->default_bpp, &fbi->var);

	fbi->fbops = ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = mxcfbi->pseudo_palette;

	/*
	 * Allocate colormap
	 */
	fb_alloc_cmap(&fbi->cmap, 16, 0);

	return fbi;
}

static ssize_t show_disp_chan(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)info->par;

	if (mxcfbi->ipu_ch == MEM_BG_SYNC)
		return sprintf(buf, "2-layer-fb-bg\n");
	else if (mxcfbi->ipu_ch == MEM_FG_SYNC)
		return sprintf(buf, "2-layer-fb-fg\n");
	else if (mxcfbi->ipu_ch == MEM_DC_SYNC)
		return sprintf(buf, "1-layer-fb\n");
	else
		return sprintf(buf, "err: no display chan\n");
}

static ssize_t swap_disp_chan(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)info->par;
	struct mxcfb_info *fg_mxcfbi = NULL;

	console_lock();
	/* swap only happen between DP-BG and DC, while DP-FG disable */
	if (((mxcfbi->ipu_ch == MEM_BG_SYNC) &&
	     (strstr(buf, "1-layer-fb") != NULL)) ||
	    ((mxcfbi->ipu_ch == MEM_DC_SYNC) &&
	     (strstr(buf, "2-layer-fb-bg") != NULL))) {
		struct fb_info *fbi_fg;

		fbi_fg = found_registered_fb(MEM_FG_SYNC, mxcfbi->ipu_id);
		if (fbi_fg)
			fg_mxcfbi = (struct mxcfb_info *)fbi_fg->par;

		if (!fg_mxcfbi ||
			fg_mxcfbi->cur_blank == FB_BLANK_UNBLANK) {
			dev_err(dev,
				"Can not switch while fb2(fb-fg) is on.\n");
			console_unlock();
			return count;
		}

		if (swap_channels(info) < 0)
			dev_err(dev, "Swap display channel failed.\n");
	}

	console_unlock();
	return count;
}
static DEVICE_ATTR(fsl_disp_property, S_IWUSR | S_IRUGO,
		   show_disp_chan, swap_disp_chan);

static ssize_t show_disp_dev(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)info->par;

	if (mxcfbi->ipu_ch == MEM_FG_SYNC)
		return sprintf(buf, "overlay\n");
	else
		return sprintf(buf, "%s\n", mxcfbi->dispdrv->drv->name);
}
static DEVICE_ATTR(fsl_disp_dev_property, S_IRUGO, show_disp_dev, NULL);

static int mxcfb_get_crtc(struct device *dev, struct mxcfb_info *mxcfbi,
			  enum crtc crtc)
{
	int i = 0;

	for (; i < ARRAY_SIZE(ipu_di_crtc_maps); i++)
		if (ipu_di_crtc_maps[i].crtc == crtc) {
			mxcfbi->ipu_id = ipu_di_crtc_maps[i].ipu_id;
			mxcfbi->ipu_di = ipu_di_crtc_maps[i].ipu_di;
			return 0;
		}

	dev_err(dev, "failed to get valid crtc\n");
	return -EINVAL;
}

static int mxcfb_dispdrv_init(struct platform_device *pdev,
		struct fb_info *fbi)
{
	struct ipuv3_fb_platform_data *plat_data = pdev->dev.platform_data;
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)fbi->par;
	struct mxc_dispdrv_setting setting;
	char disp_dev[32], *default_dev = "lcd";
	int ret = 0;

	setting.if_fmt = plat_data->interface_pix_fmt;
	setting.dft_mode_str = plat_data->mode_str;
	setting.default_bpp = plat_data->default_bpp;
	if (!setting.default_bpp)
		setting.default_bpp = 16;
	setting.fbi = fbi;
	if (!strlen(plat_data->disp_dev)) {
		memcpy(disp_dev, default_dev, strlen(default_dev));
		disp_dev[strlen(default_dev)] = '\0';
	} else {
		memcpy(disp_dev, plat_data->disp_dev,
				strlen(plat_data->disp_dev));
		disp_dev[strlen(plat_data->disp_dev)] = '\0';
	}

	mxcfbi->dispdrv = mxc_dispdrv_gethandle(disp_dev, &setting);
	if (IS_ERR(mxcfbi->dispdrv)) {
		ret = PTR_ERR(mxcfbi->dispdrv);
		dev_err(&pdev->dev, "NO mxc display driver found!\n");
		return ret;
	} else {
		/* fix-up  */
		mxcfbi->ipu_di_pix_fmt = setting.if_fmt;
		mxcfbi->default_bpp = setting.default_bpp;

		ret = mxcfb_get_crtc(&pdev->dev, mxcfbi, setting.crtc);
		if (ret)
			return ret;

		dev_dbg(&pdev->dev, "di_pixfmt:0x%x, bpp:0x%x, di:%d, ipu:%d\n",
				setting.if_fmt, setting.default_bpp,
				mxcfbi->ipu_di, mxcfbi->ipu_id);
	}

	dev_info(&pdev->dev, "registered mxc display driver %s\n", disp_dev);

	return ret;
}

/*
 * Parse user specified options (`video=trident:')
 * example:
 * 	video=mxcfb0:dev=lcd,800x480M-16@55,if=RGB565,bpp=16,noaccel
 *	video=mxcfb0:dev=lcd,800x480M-16@55,if=RGB565,fbpix=RGB565
 */
static int mxcfb_option_setup(struct platform_device *pdev, struct fb_info *fbi)
{
	struct ipuv3_fb_platform_data *pdata = pdev->dev.platform_data;
	char *options, *opt, *fb_mode_str = NULL;
	char name[] = "mxcfb0";
	uint32_t fb_pix_fmt = 0;

	name[5] += pdev->id;
	if (fb_get_options(name, &options)) {
		dev_err(&pdev->dev, "Can't get fb option for %s!\n", name);
		return -ENODEV;
	}

	if (!options || !*options)
		return 0;

	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;

		if (!strncmp(opt, "dev=", 4)) {
			memcpy(pdata->disp_dev, opt + 4, strlen(opt) - 4);
			pdata->disp_dev[strlen(opt) - 4] = '\0';
		} else if (!strncmp(opt, "if=", 3)) {
			if (!strncmp(opt+3, "RGB24", 5))
				pdata->interface_pix_fmt = IPU_PIX_FMT_RGB24;
			else if (!strncmp(opt+3, "BGR24", 5))
				pdata->interface_pix_fmt = IPU_PIX_FMT_BGR24;
			else if (!strncmp(opt+3, "GBR24", 5))
				pdata->interface_pix_fmt = IPU_PIX_FMT_GBR24;
			else if (!strncmp(opt+3, "RGB565", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_RGB565;
			else if (!strncmp(opt+3, "RGB666", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_RGB666;
			else if (!strncmp(opt+3, "YUV444", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_YUV444;
			else if (!strncmp(opt+3, "LVDS666", 7))
				pdata->interface_pix_fmt = IPU_PIX_FMT_LVDS666;
			else if (!strncmp(opt+3, "YUYV16", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_YUYV;
			else if (!strncmp(opt+3, "UYVY16", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_UYVY;
			else if (!strncmp(opt+3, "YVYU16", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_YVYU;
			else if (!strncmp(opt+3, "VYUY16", 6))
				pdata->interface_pix_fmt = IPU_PIX_FMT_VYUY;
		} else if (!strncmp(opt, "fbpix=", 6)) {
			if (!strncmp(opt+6, "RGB24", 5))
				fb_pix_fmt = IPU_PIX_FMT_RGB24;
			else if (!strncmp(opt+6, "BGR24", 5))
				fb_pix_fmt = IPU_PIX_FMT_BGR24;
			else if (!strncmp(opt+6, "RGB32", 5))
				fb_pix_fmt = IPU_PIX_FMT_RGB32;
			else if (!strncmp(opt+6, "BGR32", 5))
				fb_pix_fmt = IPU_PIX_FMT_BGR32;
			else if (!strncmp(opt+6, "ABGR32", 6))
				fb_pix_fmt = IPU_PIX_FMT_ABGR32;
			else if (!strncmp(opt+6, "RGB565", 6))
				fb_pix_fmt = IPU_PIX_FMT_RGB565;
			else if (!strncmp(opt+6, "BGRA4444", 8))
				fb_pix_fmt = IPU_PIX_FMT_BGRA4444;
			else if (!strncmp(opt+6, "BGRA5551", 8))
				fb_pix_fmt = IPU_PIX_FMT_BGRA5551;

			if (fb_pix_fmt) {
				pixfmt_to_var(fb_pix_fmt, &fbi->var);
				pdata->default_bpp =
					fbi->var.bits_per_pixel;
			}
		} else if (!strncmp(opt, "int_clk", 7)) {
			pdata->int_clk = true;
			continue;
		} else if (!strncmp(opt, "bpp=", 4)) {
			/* bpp setting cannot overwirte fbpix setting */
			if (fb_pix_fmt)
				continue;

			pdata->default_bpp =
				simple_strtoul(opt + 4, NULL, 0);

			fb_pix_fmt = bpp_to_pixfmt(pdata->default_bpp);
			if (fb_pix_fmt)
				pixfmt_to_var(fb_pix_fmt, &fbi->var);
		} else
			fb_mode_str = opt;
	}

	if (fb_mode_str)
		pdata->mode_str = fb_mode_str;

	return 0;
}

static int mxcfb_register(struct fb_info *fbi)
{
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)fbi->par;
	struct fb_videomode m;
	int ret = 0;
	char bg0_id[] = "DISP3 BG";
	char bg1_id[] = "DISP3 BG - DI1";
	char fg_id[] = "DISP3 FG";

	if (mxcfbi->ipu_di == 0) {
		bg0_id[4] += mxcfbi->ipu_id;
		strcpy(fbi->fix.id, bg0_id);
	} else if (mxcfbi->ipu_di == 1) {
		bg1_id[4] += mxcfbi->ipu_id;
		strcpy(fbi->fix.id, bg1_id);
	} else { /* Overlay */
		fg_id[4] += mxcfbi->ipu_id;
		strcpy(fbi->fix.id, fg_id);
	}

	mxcfb_check_var(&fbi->var, fbi);

	mxcfb_set_fix(fbi);

	/* Added first mode to fbi modelist. */
	if (!fbi->modelist.next || !fbi->modelist.prev)
		INIT_LIST_HEAD(&fbi->modelist);
	fb_var_to_videomode(&m, &fbi->var);
	fb_add_videomode(&m, &fbi->modelist);

	if (ipu_request_irq(mxcfbi->ipu, mxcfbi->ipu_ch_irq,
		mxcfb_irq_handler, IPU_IRQF_ONESHOT, MXCFB_NAME, fbi) != 0) {
		dev_err(fbi->device, "Error registering EOF irq handler.\n");
		ret = -EBUSY;
		goto err0;
	}
	ipu_disable_irq(mxcfbi->ipu, mxcfbi->ipu_ch_irq);
	if (ipu_request_irq(mxcfbi->ipu, mxcfbi->ipu_ch_nf_irq,
		mxcfb_nf_irq_handler, IPU_IRQF_ONESHOT, MXCFB_NAME, fbi) != 0) {
		dev_err(fbi->device, "Error registering NFACK irq handler.\n");
		ret = -EBUSY;
		goto err1;
	}
	ipu_disable_irq(mxcfbi->ipu, mxcfbi->ipu_ch_nf_irq);

	if (mxcfbi->ipu_alp_ch_irq != -1)
		if (ipu_request_irq(mxcfbi->ipu, mxcfbi->ipu_alp_ch_irq,
				mxcfb_alpha_irq_handler, IPU_IRQF_ONESHOT,
					MXCFB_NAME, fbi) != 0) {
			dev_err(fbi->device, "Error registering alpha irq "
					"handler.\n");
			ret = -EBUSY;
			goto err2;
		}

	if (!mxcfbi->late_init) {
		fbi->var.activate |= FB_ACTIVATE_FORCE;
		console_lock();
		fbi->flags |= FBINFO_MISC_USEREVENT;
		ret = fb_set_var(fbi, &fbi->var);
		fbi->flags &= ~FBINFO_MISC_USEREVENT;
		console_unlock();
		if (ret < 0) {
			dev_err(fbi->device, "Error fb_set_var ret:%d\n", ret);
			goto err3;
		}

		if (mxcfbi->next_blank == FB_BLANK_UNBLANK) {
			console_lock();
			ret = fb_blank(fbi, FB_BLANK_UNBLANK);
			console_unlock();
			if (ret < 0) {
				dev_err(fbi->device,
					"Error fb_blank ret:%d\n", ret);
				goto err4;
			}
		}
	} else {
		/*
		 * Setup the channel again though bootloader
		 * has done this, then set_par() can stop the
		 * channel neatly and re-initialize it .
		 */
		if (mxcfbi->next_blank == FB_BLANK_UNBLANK) {
			console_lock();
			_setup_disp_channel1(fbi);
			ipu_enable_channel(mxcfbi->ipu, mxcfbi->ipu_ch);
			console_unlock();
		}
	}


	ret = register_framebuffer(fbi);
	if (ret < 0)
		goto err5;

	return ret;
err5:
	if (mxcfbi->next_blank == FB_BLANK_UNBLANK) {
		console_lock();
		if (!mxcfbi->late_init)
			fb_blank(fbi, FB_BLANK_POWERDOWN);
		else {
			ipu_disable_channel(mxcfbi->ipu, mxcfbi->ipu_ch,
					    true);
			ipu_uninit_channel(mxcfbi->ipu, mxcfbi->ipu_ch);
		}
		console_unlock();
	}
err4:
err3:
	if (mxcfbi->ipu_alp_ch_irq != -1)
		ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_alp_ch_irq, fbi);
err2:
	ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_ch_nf_irq, fbi);
err1:
	ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_ch_irq, fbi);
err0:
	return ret;
}

static void mxcfb_unregister(struct fb_info *fbi)
{
	struct mxcfb_info *mxcfbi = (struct mxcfb_info *)fbi->par;

	if (mxcfbi->ipu_alp_ch_irq != -1)
		ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_alp_ch_irq, fbi);
	if (mxcfbi->ipu_ch_irq)
		ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_ch_irq, fbi);
	if (mxcfbi->ipu_ch_nf_irq)
		ipu_free_irq(mxcfbi->ipu, mxcfbi->ipu_ch_nf_irq, fbi);

	unregister_framebuffer(fbi);
}

static int mxcfb_setup_overlay(struct platform_device *pdev,
		struct fb_info *fbi_bg, struct resource *res)
{
	struct fb_info *ovfbi;
	struct mxcfb_info *mxcfbi_bg = (struct mxcfb_info *)fbi_bg->par;
	struct mxcfb_info *mxcfbi_fg;
	int ret = 0;

	ovfbi = mxcfb_init_fbinfo(&pdev->dev, &mxcfb_ops);
	if (!ovfbi) {
		ret = -ENOMEM;
		goto init_ovfbinfo_failed;
	}
	mxcfbi_fg = (struct mxcfb_info *)ovfbi->par;

	mxcfbi_fg->ipu = ipu_get_soc(mxcfbi_bg->ipu_id);
	if (IS_ERR(mxcfbi_fg->ipu)) {
		ret = -ENODEV;
		goto get_ipu_failed;
	}
	mxcfbi_fg->ipu_id = mxcfbi_bg->ipu_id;
	mxcfbi_fg->ipu_ch_irq = IPU_IRQ_FG_SYNC_EOF;
	mxcfbi_fg->ipu_ch_nf_irq = IPU_IRQ_FG_SYNC_NFACK;
	mxcfbi_fg->ipu_alp_ch_irq = IPU_IRQ_FG_ALPHA_SYNC_EOF;
	mxcfbi_fg->ipu_ch = MEM_FG_SYNC;
	mxcfbi_fg->ipu_di = -1;
	mxcfbi_fg->ipu_di_pix_fmt = mxcfbi_bg->ipu_di_pix_fmt;
	mxcfbi_fg->overlay = true;
	mxcfbi_fg->cur_blank = mxcfbi_fg->next_blank = FB_BLANK_POWERDOWN;
	mxcfbi_fg->prefetch = false;
	mxcfbi_fg->resolve = false;
	mxcfbi_fg->pre_num = -1;

	/* Need dummy values until real panel is configured */
	ovfbi->var.xres = 240;
	ovfbi->var.yres = 320;

	if (res && res->start && res->end) {
		ovfbi->fix.smem_len = res->end - res->start + 1;
		ovfbi->fix.smem_start = res->start;
		ovfbi->screen_base = ioremap(
					ovfbi->fix.smem_start,
					ovfbi->fix.smem_len);
	}

	ret = mxcfb_register(ovfbi);
	if (ret < 0)
		goto register_ov_failed;

	mxcfbi_bg->ovfbi = ovfbi;

	return ret;

register_ov_failed:
get_ipu_failed:
	fb_dealloc_cmap(&ovfbi->cmap);
	framebuffer_release(ovfbi);
init_ovfbinfo_failed:
	return ret;
}

static void mxcfb_unsetup_overlay(struct fb_info *fbi_bg)
{
	struct mxcfb_info *mxcfbi_bg = (struct mxcfb_info *)fbi_bg->par;
	struct fb_info *ovfbi = mxcfbi_bg->ovfbi;

	mxcfb_unregister(ovfbi);

	if (&ovfbi->cmap)
		fb_dealloc_cmap(&ovfbi->cmap);
	framebuffer_release(ovfbi);
}

static bool ipu_usage[2][2];
static int ipu_test_set_usage(int ipu, int di)
{
	if (ipu_usage[ipu][di])
		return -EBUSY;
	else
		ipu_usage[ipu][di] = true;
	return 0;
}

static void ipu_clear_usage(int ipu, int di)
{
	ipu_usage[ipu][di] = false;
}

static int mxcfb_get_of_property(struct platform_device *pdev,
				struct ipuv3_fb_platform_data *plat_data)
{
	struct device_node *np = pdev->dev.of_node;
	const char *disp_dev;
	const char *mode_str = NULL;
	const char *pixfmt;
	int err;
	int len;
	u32 bpp, int_clk;
	u32 late_init;

	err = of_property_read_string(np, "disp_dev", &disp_dev);
	if (err < 0) {
		dev_dbg(&pdev->dev, "get of property disp_dev fail\n");
		return err;
	}
	err = of_property_read_string(np, "mode_str", &mode_str);
	if (err < 0)
		dev_dbg(&pdev->dev, "get of property mode_str fail\n");
	err = of_property_read_string(np, "interface_pix_fmt", &pixfmt);
	if (err) {
		dev_dbg(&pdev->dev, "get of property pix fmt fail\n");
		return err;
	}
	err = of_property_read_u32(np, "default_bpp", &bpp);
	if (err) {
		dev_dbg(&pdev->dev, "get of property bpp fail\n");
		return err;
	}
	err = of_property_read_u32(np, "int_clk", &int_clk);
	if (err) {
		dev_dbg(&pdev->dev, "get of property int_clk fail\n");
		return err;
	}
	err = of_property_read_u32(np, "late_init", &late_init);
	if (err) {
		dev_dbg(&pdev->dev, "get of property late_init fail\n");
		return err;
	}

	plat_data->prefetch = of_property_read_bool(np, "prefetch");

	if (!strncmp(pixfmt, "RGB24", 5))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_RGB24;
	else if (!strncmp(pixfmt, "BGR24", 5))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_BGR24;
	else if (!strncmp(pixfmt, "GBR24", 5))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_GBR24;
	else if (!strncmp(pixfmt, "RGB565", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_RGB565;
	else if (!strncmp(pixfmt, "RGB666", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_RGB666;
	else if (!strncmp(pixfmt, "YUV444", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_YUV444;
	else if (!strncmp(pixfmt, "LVDS666", 7))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_LVDS666;
	else if (!strncmp(pixfmt, "YUYV16", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_YUYV;
	else if (!strncmp(pixfmt, "UYVY16", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_UYVY;
	else if (!strncmp(pixfmt, "YVYU16", 6))
		plat_data->interface_pix_fmt = IPU_PIX_FMT_YVYU;
	else if (!strncmp(pixfmt, "VYUY16", 6))
				plat_data->interface_pix_fmt = IPU_PIX_FMT_VYUY;
	else {
		dev_err(&pdev->dev, "err interface_pix_fmt!\n");
		return -ENOENT;
	}

	len = min(sizeof(plat_data->disp_dev) - 1, strlen(disp_dev));
	memcpy(plat_data->disp_dev, disp_dev, len);
	plat_data->disp_dev[len] = '\0';
	plat_data->mode_str = (char *)mode_str;
	plat_data->default_bpp = bpp;
	plat_data->int_clk = (bool)int_clk;
	plat_data->late_init = (bool)late_init;
	return err;
}

/*!
 * Probe routine for the framebuffer driver. It is called during the
 * driver binding process.      The following functions are performed in
 * this routine: Framebuffer initialization, Memory allocation and
 * mapping, Framebuffer registration, IPU initialization.
 *
 * @return      Appropriate error code to the kernel common code
 */
static int mxcfb_probe(struct platform_device *pdev)
{
	struct ipuv3_fb_platform_data *plat_data;
	struct fb_info *fbi;
	struct mxcfb_info *mxcfbi;
	struct resource *res;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s enter\n", __func__);
	pdev->id = of_alias_get_id(pdev->dev.of_node, "mxcfb");
	if (pdev->id < 0) {
		dev_err(&pdev->dev, "can not get alias id\n");
		return pdev->id;
	}

	plat_data = devm_kzalloc(&pdev->dev, sizeof(struct
					ipuv3_fb_platform_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;
	pdev->dev.platform_data = plat_data;

	ret = mxcfb_get_of_property(pdev, plat_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "get mxcfb of property fail\n");
		return ret;
	}

	/* Initialize FB structures */
	fbi = mxcfb_init_fbinfo(&pdev->dev, &mxcfb_ops);
	if (!fbi) {
		ret = -ENOMEM;
		goto init_fbinfo_failed;
	}

	ret = mxcfb_option_setup(pdev, fbi);
	if (ret)
		goto get_fb_option_failed;

	mxcfbi = (struct mxcfb_info *)fbi->par;
	mxcfbi->ipu_int_clk = plat_data->int_clk;
	mxcfbi->late_init = plat_data->late_init;
	mxcfbi->first_set_par = true;
	mxcfbi->prefetch = plat_data->prefetch;
	mxcfbi->pre_num = -1;
	spin_lock_init(&mxcfbi->spin_lock);

	ret = mxcfb_dispdrv_init(pdev, fbi);
	if (ret < 0)
		goto init_dispdrv_failed;

	ret = ipu_test_set_usage(mxcfbi->ipu_id, mxcfbi->ipu_di);
	if (ret < 0) {
		dev_err(&pdev->dev, "ipu%d-di%d already in use\n",
				mxcfbi->ipu_id, mxcfbi->ipu_di);
		goto ipu_in_busy;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res && res->start && res->end) {
		fbi->fix.smem_len = res->end - res->start + 1;
		fbi->fix.smem_start = res->start;
		fbi->screen_base = ioremap(fbi->fix.smem_start, fbi->fix.smem_len);
		/* Do not clear the fb content drawn in bootloader. */
		if (!mxcfbi->late_init)
			memset(fbi->screen_base, 0, fbi->fix.smem_len);
	}

	mxcfbi->ipu = ipu_get_soc(mxcfbi->ipu_id);
	if (IS_ERR(mxcfbi->ipu)) {
		ret = -ENODEV;
		goto get_ipu_failed;
	}

	/* first user uses DP with alpha feature */
	if (!g_dp_in_use[mxcfbi->ipu_id]) {
		mxcfbi->ipu_ch_irq = IPU_IRQ_BG_SYNC_EOF;
		mxcfbi->ipu_ch_nf_irq = IPU_IRQ_BG_SYNC_NFACK;
		mxcfbi->ipu_alp_ch_irq = IPU_IRQ_BG_ALPHA_SYNC_EOF;
		mxcfbi->ipu_ch = MEM_BG_SYNC;
		/* Unblank the primary fb only by default */
		if (pdev->id == 0)
			mxcfbi->cur_blank = mxcfbi->next_blank = FB_BLANK_UNBLANK;
		else
			mxcfbi->cur_blank = mxcfbi->next_blank = FB_BLANK_POWERDOWN;

		ret = mxcfb_register(fbi);
		if (ret < 0)
			goto mxcfb_register_failed;

		ipu_disp_set_global_alpha(mxcfbi->ipu, mxcfbi->ipu_ch,
					  true, 0x80);
		ipu_disp_set_color_key(mxcfbi->ipu, mxcfbi->ipu_ch, false, 0);

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		ret = mxcfb_setup_overlay(pdev, fbi, res);

		if (ret < 0) {
			mxcfb_unregister(fbi);
			goto mxcfb_setupoverlay_failed;
		}

		g_dp_in_use[mxcfbi->ipu_id] = true;

		ret = device_create_file(mxcfbi->ovfbi->dev,
					 &dev_attr_fsl_disp_property);
		if (ret)
			dev_err(mxcfbi->ovfbi->dev, "Error %d on creating "
						    "file for disp property\n",
						    ret);

		ret = device_create_file(mxcfbi->ovfbi->dev,
					 &dev_attr_fsl_disp_dev_property);
		if (ret)
			dev_err(mxcfbi->ovfbi->dev, "Error %d on creating "
						    "file for disp device "
						    "propety\n", ret);
	} else {
		mxcfbi->ipu_ch_irq = IPU_IRQ_DC_SYNC_EOF;
		mxcfbi->ipu_ch_nf_irq = IPU_IRQ_DC_SYNC_NFACK;
		mxcfbi->ipu_alp_ch_irq = -1;
		mxcfbi->ipu_ch = MEM_DC_SYNC;
		mxcfbi->cur_blank = mxcfbi->next_blank = FB_BLANK_POWERDOWN;

		ret = mxcfb_register(fbi);
		if (ret < 0)
			goto mxcfb_register_failed;
	}

	platform_set_drvdata(pdev, fbi);

	ret = device_create_file(fbi->dev, &dev_attr_fsl_disp_property);
	if (ret)
		dev_err(&pdev->dev, "Error %d on creating file for disp "
				    "property\n", ret);

	ret = device_create_file(fbi->dev, &dev_attr_fsl_disp_dev_property);
	if (ret)
		dev_err(&pdev->dev, "Error %d on creating file for disp "
				    " device propety\n", ret);

	return 0;

mxcfb_setupoverlay_failed:
mxcfb_register_failed:
get_ipu_failed:
	ipu_clear_usage(mxcfbi->ipu_id, mxcfbi->ipu_di);
ipu_in_busy:
init_dispdrv_failed:
	fb_dealloc_cmap(&fbi->cmap);
	framebuffer_release(fbi);
get_fb_option_failed:
init_fbinfo_failed:
	return ret;
}

static int mxcfb_remove(struct platform_device *pdev)
{
	struct fb_info *fbi = platform_get_drvdata(pdev);
	struct mxcfb_info *mxc_fbi = fbi->par;

	device_remove_file(fbi->dev, &dev_attr_fsl_disp_dev_property);
	device_remove_file(fbi->dev, &dev_attr_fsl_disp_property);
	mxcfb_blank(FB_BLANK_POWERDOWN, fbi);
	mxcfb_unregister(fbi);
	mxcfb_unmap_video_memory(fbi);

	if (mxc_fbi->ovfbi) {
		device_remove_file(mxc_fbi->ovfbi->dev,
				   &dev_attr_fsl_disp_dev_property);
		device_remove_file(mxc_fbi->ovfbi->dev,
				   &dev_attr_fsl_disp_property);
		mxcfb_blank(FB_BLANK_POWERDOWN, mxc_fbi->ovfbi);
		mxcfb_unsetup_overlay(fbi);
		mxcfb_unmap_video_memory(mxc_fbi->ovfbi);
	}

	ipu_clear_usage(mxc_fbi->ipu_id, mxc_fbi->ipu_di);
	if (&fbi->cmap)
		fb_dealloc_cmap(&fbi->cmap);
	framebuffer_release(fbi);
	return 0;
}

static const struct of_device_id imx_mxcfb_dt_ids[] = {
	{ .compatible = "fsl,mxc_sdc_fb"},
	{ /* sentinel */ }
};

/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mxcfb_driver = {
	.driver = {
		.name = MXCFB_NAME,
		.of_match_table	= imx_mxcfb_dt_ids,
	},
	.probe = mxcfb_probe,
	.remove = mxcfb_remove,
	.suspend = mxcfb_suspend,
	.resume = mxcfb_resume,
};

/*!
 * Main entry function for the framebuffer. The function registers the power
 * management callback functions with the kernel and also registers the MXCFB
 * callback functions with the core Linux framebuffer driver \b fbmem.c
 *
 * @return      Error code indicating success or failure
 */
int __init mxcfb_init(void)
{
	return platform_driver_register(&mxcfb_driver);
}

void mxcfb_exit(void)
{
	platform_driver_unregister(&mxcfb_driver);
}

module_init(mxcfb_init);
module_exit(mxcfb_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("fb");
