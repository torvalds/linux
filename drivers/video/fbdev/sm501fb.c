// SPDX-License-Identifier: GPL-2.0-only
/* linux/drivers/video/sm501fb.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Vincent Sanders <vince@simtec.co.uk>
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Framebuffer driver for the Silicon Motion SM501
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>

#include <linux/uaccess.h>
#include <asm/div64.h>

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include <linux/sm501.h>
#include <linux/sm501-regs.h>

#include "edid.h"

static char *fb_mode = "640x480-16@60";
static unsigned long default_bpp = 16;

static const struct fb_videomode sm501_default_mode = {
	.refresh	= 60,
	.xres		= 640,
	.yres		= 480,
	.pixclock	= 20833,
	.left_margin	= 142,
	.right_margin	= 13,
	.upper_margin	= 21,
	.lower_margin	= 1,
	.hsync_len	= 69,
	.vsync_len	= 3,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode		= FB_VMODE_NONINTERLACED
};

#define NR_PALETTE	256

enum sm501_controller {
	HEAD_CRT	= 0,
	HEAD_PANEL	= 1,
};

/* SM501 memory address.
 *
 * This structure is used to track memory usage within the SM501 framebuffer
 * allocation. The sm_addr field is stored as an offset as it is often used
 * against both the physical and mapped addresses.
 */
struct sm501_mem {
	unsigned long	 size;
	unsigned long	 sm_addr;	/* offset from base of sm501 fb. */
	void __iomem	*k_addr;
};

/* private data that is shared between all frambuffers* */
struct sm501fb_info {
	struct device		*dev;
	struct fb_info		*fb[2];		/* fb info for both heads */
	struct resource		*fbmem_res;	/* framebuffer resource */
	struct resource		*regs_res;	/* registers resource */
	struct resource		*regs2d_res;	/* 2d registers resource */
	struct sm501_platdata_fb *pdata;	/* our platform data */

	unsigned long		 pm_crt_ctrl;	/* pm: crt ctrl save */

	int			 irq;
	int			 swap_endian;	/* set to swap rgb=>bgr */
	void __iomem		*regs;		/* remapped registers */
	void __iomem		*regs2d;	/* 2d remapped registers */
	void __iomem		*fbmem;		/* remapped framebuffer */
	size_t			 fbmem_len;	/* length of remapped region */
	u8 *edid_data;
};

/* per-framebuffer private data */
struct sm501fb_par {
	u32			 pseudo_palette[16];

	enum sm501_controller	 head;
	struct sm501_mem	 cursor;
	struct sm501_mem	 screen;
	struct fb_ops		 ops;

	void			*store_fb;
	void			*store_cursor;
	void __iomem		*cursor_regs;
	struct sm501fb_info	*info;
};

/* Helper functions */

static inline int h_total(struct fb_var_screeninfo *var)
{
	return var->xres + var->left_margin +
		var->right_margin + var->hsync_len;
}

static inline int v_total(struct fb_var_screeninfo *var)
{
	return var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len;
}

/* sm501fb_sync_regs()
 *
 * This call is mainly for PCI bus systems where we need to
 * ensure that any writes to the bus are completed before the
 * next phase, or after completing a function.
*/

static inline void sm501fb_sync_regs(struct sm501fb_info *info)
{
	smc501_readl(info->regs);
}

/* sm501_alloc_mem
 *
 * This is an attempt to lay out memory for the two framebuffers and
 * everything else
 *
 * |fbmem_res->start					       fbmem_res->end|
 * |									     |
 * |fb[0].fix.smem_start    |	      |fb[1].fix.smem_start    |     2K	     |
 * |-> fb[0].fix.smem_len <-| spare   |-> fb[1].fix.smem_len <-|-> cursors <-|
 *
 * The "spare" space is for the 2d engine data
 * the fixed is space for the cursors (2x1Kbyte)
 *
 * we need to allocate memory for the 2D acceleration engine
 * command list and the data for the engine to deal with.
 *
 * - all allocations must be 128bit aligned
 * - cursors are 64x64x2 bits (1Kbyte)
 *
 */

#define SM501_MEMF_CURSOR		(1)
#define SM501_MEMF_PANEL		(2)
#define SM501_MEMF_CRT			(4)
#define SM501_MEMF_ACCEL		(8)

static int sm501_alloc_mem(struct sm501fb_info *inf, struct sm501_mem *mem,
			   unsigned int why, size_t size, u32 smem_len)
{
	struct sm501fb_par *par;
	struct fb_info *fbi;
	unsigned int ptr;
	unsigned int end;

	switch (why) {
	case SM501_MEMF_CURSOR:
		ptr = inf->fbmem_len - size;
		inf->fbmem_len = ptr;	/* adjust available memory. */
		break;

	case SM501_MEMF_PANEL:
		if (size > inf->fbmem_len)
			return -ENOMEM;

		ptr = inf->fbmem_len - size;
		fbi = inf->fb[HEAD_CRT];

		/* round down, some programs such as directfb do not draw
		 * 0,0 correctly unless the start is aligned to a page start.
		 */

		if (ptr > 0)
			ptr &= ~(PAGE_SIZE - 1);

		if (fbi && ptr < smem_len)
			return -ENOMEM;

		break;

	case SM501_MEMF_CRT:
		ptr = 0;

		/* check to see if we have panel memory allocated
		 * which would put an limit on available memory. */

		fbi = inf->fb[HEAD_PANEL];
		if (fbi) {
			par = fbi->par;
			end = par->screen.k_addr ? par->screen.sm_addr : inf->fbmem_len;
		} else
			end = inf->fbmem_len;

		if ((ptr + size) > end)
			return -ENOMEM;

		break;

	case SM501_MEMF_ACCEL:
		fbi = inf->fb[HEAD_CRT];
		ptr = fbi ? smem_len : 0;

		fbi = inf->fb[HEAD_PANEL];
		if (fbi) {
			par = fbi->par;
			end = par->screen.sm_addr;
		} else
			end = inf->fbmem_len;

		if ((ptr + size) > end)
			return -ENOMEM;

		break;

	default:
		return -EINVAL;
	}

	mem->size    = size;
	mem->sm_addr = ptr;
	mem->k_addr  = inf->fbmem + ptr;

	dev_dbg(inf->dev, "%s: result %08lx, %p - %u, %zd\n",
		__func__, mem->sm_addr, mem->k_addr, why, size);

	return 0;
}

/* sm501fb_ps_to_hz
 *
 * Converts a period in picoseconds to Hz.
 *
 * Note, we try to keep this in Hz to minimise rounding with
 * the limited PLL settings on the SM501.
*/

static unsigned long sm501fb_ps_to_hz(unsigned long psvalue)
{
	unsigned long long numerator=1000000000000ULL;

	/* 10^12 / picosecond period gives frequency in Hz */
	do_div(numerator, psvalue);
	return (unsigned long)numerator;
}

/* sm501fb_hz_to_ps is identical to the opposite transform */

#define sm501fb_hz_to_ps(x) sm501fb_ps_to_hz(x)

/* sm501fb_setup_gamma
 *
 * Programs a linear 1.0 gamma ramp in case the gamma
 * correction is enabled without programming anything else.
*/

static void sm501fb_setup_gamma(struct sm501fb_info *fbi,
				unsigned long palette)
{
	unsigned long value = 0;
	int offset;

	/* set gamma values */
	for (offset = 0; offset < 256 * 4; offset += 4) {
		smc501_writel(value, fbi->regs + palette + offset);
		value += 0x010101; 	/* Advance RGB by 1,1,1.*/
	}
}

/* sm501fb_check_var
 *
 * check common variables for both panel and crt
*/

static int sm501fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *sm  = par->info;
	unsigned long tmp;

	/* check we can fit these values into the registers */

	if (var->hsync_len > 255 || var->vsync_len > 63)
		return -EINVAL;

	/* hdisplay end and hsync start */
	if ((var->xres + var->right_margin) > 4096)
		return -EINVAL;

	/* vdisplay end and vsync start */
	if ((var->yres + var->lower_margin) > 2048)
		return -EINVAL;

	/* hard limits of device */

	if (h_total(var) > 4096 || v_total(var) > 2048)
		return -EINVAL;

	/* check our line length is going to be 128 bit aligned */

	tmp = (var->xres * var->bits_per_pixel) / 8;
	if ((tmp & 15) != 0)
		return -EINVAL;

	/* check the virtual size */

	if (var->xres_virtual > 4096 || var->yres_virtual > 2048)
		return -EINVAL;

	/* can cope with 8,16 or 32bpp */

	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel == 24)
		var->bits_per_pixel = 32;

	/* set r/g/b positions and validate bpp */
	switch(var->bits_per_pixel) {
	case 8:
		var->red.length		= var->bits_per_pixel;
		var->red.offset		= 0;
		var->green.length	= var->bits_per_pixel;
		var->green.offset	= 0;
		var->blue.length	= var->bits_per_pixel;
		var->blue.offset	= 0;
		var->transp.length	= 0;
		var->transp.offset	= 0;

		break;

	case 16:
		if (sm->pdata->flags & SM501_FBPD_SWAP_FB_ENDIAN) {
			var->blue.offset	= 11;
			var->green.offset	= 5;
			var->red.offset		= 0;
		} else {
			var->red.offset		= 11;
			var->green.offset	= 5;
			var->blue.offset	= 0;
		}
		var->transp.offset	= 0;

		var->red.length		= 5;
		var->green.length	= 6;
		var->blue.length	= 5;
		var->transp.length	= 0;
		break;

	case 32:
		if (sm->pdata->flags & SM501_FBPD_SWAP_FB_ENDIAN) {
			var->transp.offset	= 0;
			var->red.offset		= 8;
			var->green.offset	= 16;
			var->blue.offset	= 24;
		} else {
			var->transp.offset	= 24;
			var->red.offset		= 16;
			var->green.offset	= 8;
			var->blue.offset	= 0;
		}

		var->red.length		= 8;
		var->green.length	= 8;
		var->blue.length	= 8;
		var->transp.length	= 0;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * sm501fb_check_var_crt():
 *
 * check the parameters for the CRT head, and either bring them
 * back into range, or return -EINVAL.
*/

static int sm501fb_check_var_crt(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	return sm501fb_check_var(var, info);
}

/* sm501fb_check_var_pnl():
 *
 * check the parameters for the CRT head, and either bring them
 * back into range, or return -EINVAL.
*/

static int sm501fb_check_var_pnl(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	return sm501fb_check_var(var, info);
}

/* sm501fb_set_par_common
 *
 * set common registers for framebuffers
*/

static int sm501fb_set_par_common(struct fb_info *info,
				  struct fb_var_screeninfo *var)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	unsigned long pixclock;      /* pixelclock in Hz */
	unsigned long sm501pixclock; /* pixelclock the 501 can achieve in Hz */
	unsigned int mem_type;
	unsigned int clock_type;
	unsigned int head_addr;
	unsigned int smem_len;

	dev_dbg(fbi->dev, "%s: %dx%d, bpp = %d, virtual %dx%d\n",
		__func__, var->xres, var->yres, var->bits_per_pixel,
		var->xres_virtual, var->yres_virtual);

	switch (par->head) {
	case HEAD_CRT:
		mem_type = SM501_MEMF_CRT;
		clock_type = SM501_CLOCK_V2XCLK;
		head_addr = SM501_DC_CRT_FB_ADDR;
		break;

	case HEAD_PANEL:
		mem_type = SM501_MEMF_PANEL;
		clock_type = SM501_CLOCK_P2XCLK;
		head_addr = SM501_DC_PANEL_FB_ADDR;
		break;

	default:
		mem_type = 0;		/* stop compiler warnings */
		head_addr = 0;
		clock_type = 0;
	}

	switch (var->bits_per_pixel) {
	case 8:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;

	case 16:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;

	case 32:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	}

	/* allocate fb memory within 501 */
	info->fix.line_length = (var->xres_virtual * var->bits_per_pixel)/8;
	smem_len = info->fix.line_length * var->yres_virtual;

	dev_dbg(fbi->dev, "%s: line length = %u\n", __func__,
		info->fix.line_length);

	if (sm501_alloc_mem(fbi, &par->screen, mem_type, smem_len, smem_len)) {
		dev_err(fbi->dev, "no memory available\n");
		return -ENOMEM;
	}

	mutex_lock(&info->mm_lock);
	info->fix.smem_start = fbi->fbmem_res->start + par->screen.sm_addr;
	info->fix.smem_len   = smem_len;
	mutex_unlock(&info->mm_lock);

	info->screen_base = fbi->fbmem + par->screen.sm_addr;
	info->screen_size = info->fix.smem_len;

	/* set start of framebuffer to the screen */

	smc501_writel(par->screen.sm_addr | SM501_ADDR_FLIP,
			fbi->regs + head_addr);

	/* program CRT clock  */

	pixclock = sm501fb_ps_to_hz(var->pixclock);

	sm501pixclock = sm501_set_clock(fbi->dev->parent, clock_type,
					pixclock);

	/* update fb layer with actual clock used */
	var->pixclock = sm501fb_hz_to_ps(sm501pixclock);

	dev_dbg(fbi->dev, "%s: pixclock(ps) = %u, pixclock(Hz)  = %lu, "
	       "sm501pixclock = %lu,  error = %ld%%\n",
	       __func__, var->pixclock, pixclock, sm501pixclock,
	       ((pixclock - sm501pixclock)*100)/pixclock);

	return 0;
}

/* sm501fb_set_par_geometry
 *
 * set the geometry registers for specified framebuffer.
*/

static void sm501fb_set_par_geometry(struct fb_info *info,
				     struct fb_var_screeninfo *var)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	void __iomem *base = fbi->regs;
	unsigned long reg;

	if (par->head == HEAD_CRT)
		base += SM501_DC_CRT_H_TOT;
	else
		base += SM501_DC_PANEL_H_TOT;

	/* set framebuffer width and display width */

	reg = info->fix.line_length;
	reg |= ((var->xres * var->bits_per_pixel)/8) << 16;

	smc501_writel(reg, fbi->regs + (par->head == HEAD_CRT ?
		    SM501_DC_CRT_FB_OFFSET :  SM501_DC_PANEL_FB_OFFSET));

	/* program horizontal total */

	reg  = (h_total(var) - 1) << 16;
	reg |= (var->xres - 1);

	smc501_writel(reg, base + SM501_OFF_DC_H_TOT);

	/* program horizontal sync */

	reg  = var->hsync_len << 16;
	reg |= var->xres + var->right_margin - 1;

	smc501_writel(reg, base + SM501_OFF_DC_H_SYNC);

	/* program vertical total */

	reg  = (v_total(var) - 1) << 16;
	reg |= (var->yres - 1);

	smc501_writel(reg, base + SM501_OFF_DC_V_TOT);

	/* program vertical sync */
	reg  = var->vsync_len << 16;
	reg |= var->yres + var->lower_margin - 1;

	smc501_writel(reg, base + SM501_OFF_DC_V_SYNC);
}

/* sm501fb_pan_crt
 *
 * pan the CRT display output within an virtual framebuffer
*/

static int sm501fb_pan_crt(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	unsigned int bytes_pixel = info->var.bits_per_pixel / 8;
	unsigned long reg;
	unsigned long xoffs;

	xoffs = var->xoffset * bytes_pixel;

	reg = smc501_readl(fbi->regs + SM501_DC_CRT_CONTROL);

	reg &= ~SM501_DC_CRT_CONTROL_PIXEL_MASK;
	reg |= ((xoffs & 15) / bytes_pixel) << 4;
	smc501_writel(reg, fbi->regs + SM501_DC_CRT_CONTROL);

	reg = (par->screen.sm_addr + xoffs +
	       var->yoffset * info->fix.line_length);
	smc501_writel(reg | SM501_ADDR_FLIP, fbi->regs + SM501_DC_CRT_FB_ADDR);

	sm501fb_sync_regs(fbi);
	return 0;
}

/* sm501fb_pan_pnl
 *
 * pan the panel display output within an virtual framebuffer
*/

static int sm501fb_pan_pnl(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	unsigned long reg;

	reg = var->xoffset | (info->var.xres_virtual << 16);
	smc501_writel(reg, fbi->regs + SM501_DC_PANEL_FB_WIDTH);

	reg = var->yoffset | (info->var.yres_virtual << 16);
	smc501_writel(reg, fbi->regs + SM501_DC_PANEL_FB_HEIGHT);

	sm501fb_sync_regs(fbi);
	return 0;
}

/* sm501fb_set_par_crt
 *
 * Set the CRT video mode from the fb_info structure
*/

static int sm501fb_set_par_crt(struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long control;       /* control register */
	int ret;

	/* activate new configuration */

	dev_dbg(fbi->dev, "%s(%p)\n", __func__, info);

	/* enable CRT DAC - note 0 is on!*/
	sm501_misc_control(fbi->dev->parent, 0, SM501_MISC_DAC_POWER);

	control = smc501_readl(fbi->regs + SM501_DC_CRT_CONTROL);

	control &= (SM501_DC_CRT_CONTROL_PIXEL_MASK |
		    SM501_DC_CRT_CONTROL_GAMMA |
		    SM501_DC_CRT_CONTROL_BLANK |
		    SM501_DC_CRT_CONTROL_SEL |
		    SM501_DC_CRT_CONTROL_CP |
		    SM501_DC_CRT_CONTROL_TVP);

	/* set the sync polarities before we check data source  */

	if ((var->sync & FB_SYNC_HOR_HIGH_ACT) == 0)
		control |= SM501_DC_CRT_CONTROL_HSP;

	if ((var->sync & FB_SYNC_VERT_HIGH_ACT) == 0)
		control |= SM501_DC_CRT_CONTROL_VSP;

	if ((control & SM501_DC_CRT_CONTROL_SEL) == 0) {
		/* the head is displaying panel data... */

		sm501_alloc_mem(fbi, &par->screen, SM501_MEMF_CRT, 0,
				info->fix.smem_len);
		goto out_update;
	}

	ret = sm501fb_set_par_common(info, var);
	if (ret) {
		dev_err(fbi->dev, "failed to set common parameters\n");
		return ret;
	}

	sm501fb_pan_crt(var, info);
	sm501fb_set_par_geometry(info, var);

	control |= SM501_FIFO_3;	/* fill if >3 free slots */

	switch(var->bits_per_pixel) {
	case 8:
		control |= SM501_DC_CRT_CONTROL_8BPP;
		break;

	case 16:
		control |= SM501_DC_CRT_CONTROL_16BPP;
		sm501fb_setup_gamma(fbi, SM501_DC_CRT_PALETTE);
		break;

	case 32:
		control |= SM501_DC_CRT_CONTROL_32BPP;
		sm501fb_setup_gamma(fbi, SM501_DC_CRT_PALETTE);
		break;

	default:
		BUG();
	}

	control |= SM501_DC_CRT_CONTROL_SEL;	/* CRT displays CRT data */
	control |= SM501_DC_CRT_CONTROL_TE;	/* enable CRT timing */
	control |= SM501_DC_CRT_CONTROL_ENABLE;	/* enable CRT plane */

 out_update:
	dev_dbg(fbi->dev, "new control is %08lx\n", control);

	smc501_writel(control, fbi->regs + SM501_DC_CRT_CONTROL);
	sm501fb_sync_regs(fbi);

	return 0;
}

static void sm501fb_panel_power(struct sm501fb_info *fbi, int to)
{
	unsigned long control;
	void __iomem *ctrl_reg = fbi->regs + SM501_DC_PANEL_CONTROL;
	struct sm501_platdata_fbsub *pd = fbi->pdata->fb_pnl;

	control = smc501_readl(ctrl_reg);

	if (to && (control & SM501_DC_PANEL_CONTROL_VDD) == 0) {
		/* enable panel power */

		control |= SM501_DC_PANEL_CONTROL_VDD;	/* FPVDDEN */
		smc501_writel(control, ctrl_reg);
		sm501fb_sync_regs(fbi);
		mdelay(10);

		control |= SM501_DC_PANEL_CONTROL_DATA;	/* DATA */
		smc501_writel(control, ctrl_reg);
		sm501fb_sync_regs(fbi);
		mdelay(10);

		/* VBIASEN */

		if (!(pd->flags & SM501FB_FLAG_PANEL_NO_VBIASEN)) {
			if (pd->flags & SM501FB_FLAG_PANEL_INV_VBIASEN)
				control &= ~SM501_DC_PANEL_CONTROL_BIAS;
			else
				control |= SM501_DC_PANEL_CONTROL_BIAS;

			smc501_writel(control, ctrl_reg);
			sm501fb_sync_regs(fbi);
			mdelay(10);
		}

		if (!(pd->flags & SM501FB_FLAG_PANEL_NO_FPEN)) {
			if (pd->flags & SM501FB_FLAG_PANEL_INV_FPEN)
				control &= ~SM501_DC_PANEL_CONTROL_FPEN;
			else
				control |= SM501_DC_PANEL_CONTROL_FPEN;

			smc501_writel(control, ctrl_reg);
			sm501fb_sync_regs(fbi);
			mdelay(10);
		}
	} else if (!to && (control & SM501_DC_PANEL_CONTROL_VDD) != 0) {
		/* disable panel power */
		if (!(pd->flags & SM501FB_FLAG_PANEL_NO_FPEN)) {
			if (pd->flags & SM501FB_FLAG_PANEL_INV_FPEN)
				control |= SM501_DC_PANEL_CONTROL_FPEN;
			else
				control &= ~SM501_DC_PANEL_CONTROL_FPEN;

			smc501_writel(control, ctrl_reg);
			sm501fb_sync_regs(fbi);
			mdelay(10);
		}

		if (!(pd->flags & SM501FB_FLAG_PANEL_NO_VBIASEN)) {
			if (pd->flags & SM501FB_FLAG_PANEL_INV_VBIASEN)
				control |= SM501_DC_PANEL_CONTROL_BIAS;
			else
				control &= ~SM501_DC_PANEL_CONTROL_BIAS;

			smc501_writel(control, ctrl_reg);
			sm501fb_sync_regs(fbi);
			mdelay(10);
		}

		control &= ~SM501_DC_PANEL_CONTROL_DATA;
		smc501_writel(control, ctrl_reg);
		sm501fb_sync_regs(fbi);
		mdelay(10);

		control &= ~SM501_DC_PANEL_CONTROL_VDD;
		smc501_writel(control, ctrl_reg);
		sm501fb_sync_regs(fbi);
		mdelay(10);
	}

	sm501fb_sync_regs(fbi);
}

/* sm501fb_set_par_pnl
 *
 * Set the panel video mode from the fb_info structure
*/

static int sm501fb_set_par_pnl(struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long control;
	unsigned long reg;
	int ret;

	dev_dbg(fbi->dev, "%s(%p)\n", __func__, info);

	/* activate this new configuration */

	ret = sm501fb_set_par_common(info, var);
	if (ret)
		return ret;

	sm501fb_pan_pnl(var, info);
	sm501fb_set_par_geometry(info, var);

	/* update control register */

	control = smc501_readl(fbi->regs + SM501_DC_PANEL_CONTROL);
	control &= (SM501_DC_PANEL_CONTROL_GAMMA |
		    SM501_DC_PANEL_CONTROL_VDD  |
		    SM501_DC_PANEL_CONTROL_DATA |
		    SM501_DC_PANEL_CONTROL_BIAS |
		    SM501_DC_PANEL_CONTROL_FPEN |
		    SM501_DC_PANEL_CONTROL_CP |
		    SM501_DC_PANEL_CONTROL_CK |
		    SM501_DC_PANEL_CONTROL_HP |
		    SM501_DC_PANEL_CONTROL_VP |
		    SM501_DC_PANEL_CONTROL_HPD |
		    SM501_DC_PANEL_CONTROL_VPD);

	control |= SM501_FIFO_3;	/* fill if >3 free slots */

	switch(var->bits_per_pixel) {
	case 8:
		control |= SM501_DC_PANEL_CONTROL_8BPP;
		break;

	case 16:
		control |= SM501_DC_PANEL_CONTROL_16BPP;
		sm501fb_setup_gamma(fbi, SM501_DC_PANEL_PALETTE);
		break;

	case 32:
		control |= SM501_DC_PANEL_CONTROL_32BPP;
		sm501fb_setup_gamma(fbi, SM501_DC_PANEL_PALETTE);
		break;

	default:
		BUG();
	}

	smc501_writel(0x0, fbi->regs + SM501_DC_PANEL_PANNING_CONTROL);

	/* panel plane top left and bottom right location */

	smc501_writel(0x00, fbi->regs + SM501_DC_PANEL_TL_LOC);

	reg  = var->xres - 1;
	reg |= (var->yres - 1) << 16;

	smc501_writel(reg, fbi->regs + SM501_DC_PANEL_BR_LOC);

	/* program panel control register */

	control |= SM501_DC_PANEL_CONTROL_TE;	/* enable PANEL timing */
	control |= SM501_DC_PANEL_CONTROL_EN;	/* enable PANEL gfx plane */

	if ((var->sync & FB_SYNC_HOR_HIGH_ACT) == 0)
		control |= SM501_DC_PANEL_CONTROL_HSP;

	if ((var->sync & FB_SYNC_VERT_HIGH_ACT) == 0)
		control |= SM501_DC_PANEL_CONTROL_VSP;

	smc501_writel(control, fbi->regs + SM501_DC_PANEL_CONTROL);
	sm501fb_sync_regs(fbi);

	/* ensure the panel interface is not tristated at this point */

	sm501_modify_reg(fbi->dev->parent, SM501_SYSTEM_CONTROL,
			 0, SM501_SYSCTRL_PANEL_TRISTATE);

	/* power the panel up */
	sm501fb_panel_power(fbi, 1);
	return 0;
}


/* chan_to_field
 *
 * convert a colour value into a field position
 *
 * from pxafb.c
*/

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

/* sm501fb_setcolreg
 *
 * set the colour mapping for modes that support palettised data
*/

static int sm501fb_setcolreg(unsigned regno,
			     unsigned red, unsigned green, unsigned blue,
			     unsigned transp, struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	void __iomem *base = fbi->regs;
	unsigned int val;

	if (par->head == HEAD_CRT)
		base += SM501_DC_CRT_PALETTE;
	else
		base += SM501_DC_PANEL_PALETTE;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseuo-palette */

		if (regno < 16) {
			u32 *pal = par->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
		}
		break;

	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < 256) {
			val = (red >> 8) << 16;
			val |= (green >> 8) << 8;
			val |= blue >> 8;

			smc501_writel(val, base + (regno * 4));
		}

		break;

	default:
		return 1;   /* unknown type */
	}

	return 0;
}

/* sm501fb_blank_pnl
 *
 * Blank or un-blank the panel interface
*/

static int sm501fb_blank_pnl(int blank_mode, struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;

	dev_dbg(fbi->dev, "%s(mode=%d, %p)\n", __func__, blank_mode, info);

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
		sm501fb_panel_power(fbi, 0);
		break;

	case FB_BLANK_UNBLANK:
		sm501fb_panel_power(fbi, 1);
		break;

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		return 1;
	}

	return 0;
}

/* sm501fb_blank_crt
 *
 * Blank or un-blank the crt interface
*/

static int sm501fb_blank_crt(int blank_mode, struct fb_info *info)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	unsigned long ctrl;

	dev_dbg(fbi->dev, "%s(mode=%d, %p)\n", __func__, blank_mode, info);

	ctrl = smc501_readl(fbi->regs + SM501_DC_CRT_CONTROL);

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
		ctrl &= ~SM501_DC_CRT_CONTROL_ENABLE;
		sm501_misc_control(fbi->dev->parent, SM501_MISC_DAC_POWER, 0);
		fallthrough;

	case FB_BLANK_NORMAL:
		ctrl |= SM501_DC_CRT_CONTROL_BLANK;
		break;

	case FB_BLANK_UNBLANK:
		ctrl &= ~SM501_DC_CRT_CONTROL_BLANK;
		ctrl |=  SM501_DC_CRT_CONTROL_ENABLE;
		sm501_misc_control(fbi->dev->parent, 0, SM501_MISC_DAC_POWER);
		break;

	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		return 1;

	}

	smc501_writel(ctrl, fbi->regs + SM501_DC_CRT_CONTROL);
	sm501fb_sync_regs(fbi);

	return 0;
}

/* sm501fb_cursor
 *
 * set or change the hardware cursor parameters
*/

static int sm501fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	void __iomem *base = fbi->regs;
	unsigned long hwc_addr;
	unsigned long fg, bg;

	dev_dbg(fbi->dev, "%s(%p,%p)\n", __func__, info, cursor);

	if (par->head == HEAD_CRT)
		base += SM501_DC_CRT_HWC_BASE;
	else
		base += SM501_DC_PANEL_HWC_BASE;

	/* check not being asked to exceed capabilities */

	if (cursor->image.width > 64)
		return -EINVAL;

	if (cursor->image.height > 64)
		return -EINVAL;

	if (cursor->image.depth > 1)
		return -EINVAL;

	hwc_addr = smc501_readl(base + SM501_OFF_HWC_ADDR);

	if (cursor->enable)
		smc501_writel(hwc_addr | SM501_HWC_EN,
				base + SM501_OFF_HWC_ADDR);
	else
		smc501_writel(hwc_addr & ~SM501_HWC_EN,
				base + SM501_OFF_HWC_ADDR);

	/* set data */
	if (cursor->set & FB_CUR_SETPOS) {
		unsigned int x = cursor->image.dx;
		unsigned int y = cursor->image.dy;

		if (x >= 2048 || y >= 2048 )
			return -EINVAL;

		dev_dbg(fbi->dev, "set position %d,%d\n", x, y);

		//y += cursor->image.height;

		smc501_writel(x | (y << 16), base + SM501_OFF_HWC_LOC);
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		unsigned int bg_col = cursor->image.bg_color;
		unsigned int fg_col = cursor->image.fg_color;

		dev_dbg(fbi->dev, "%s: update cmap (%08x,%08x)\n",
			__func__, bg_col, fg_col);

		bg = ((info->cmap.red[bg_col] & 0xF8) << 8) |
			((info->cmap.green[bg_col] & 0xFC) << 3) |
			((info->cmap.blue[bg_col] & 0xF8) >> 3);

		fg = ((info->cmap.red[fg_col] & 0xF8) << 8) |
			((info->cmap.green[fg_col] & 0xFC) << 3) |
			((info->cmap.blue[fg_col] & 0xF8) >> 3);

		dev_dbg(fbi->dev, "fgcol %08lx, bgcol %08lx\n", fg, bg);

		smc501_writel(bg, base + SM501_OFF_HWC_COLOR_1_2);
		smc501_writel(fg, base + SM501_OFF_HWC_COLOR_3);
	}

	if (cursor->set & FB_CUR_SETSIZE ||
	    cursor->set & (FB_CUR_SETIMAGE | FB_CUR_SETSHAPE)) {
		/* SM501 cursor is a two bpp 64x64 bitmap this routine
		 * clears it to transparent then combines the cursor
		 * shape plane with the colour plane to set the
		 * cursor */
		int x, y;
		const unsigned char *pcol = cursor->image.data;
		const unsigned char *pmsk = cursor->mask;
		void __iomem   *dst = par->cursor.k_addr;
		unsigned char  dcol = 0;
		unsigned char  dmsk = 0;
		unsigned int   op;

		dev_dbg(fbi->dev, "%s: setting shape (%d,%d)\n",
			__func__, cursor->image.width, cursor->image.height);

		for (op = 0; op < (64*64*2)/8; op+=4)
			smc501_writel(0x0, dst + op);

		for (y = 0; y < cursor->image.height; y++) {
			for (x = 0; x < cursor->image.width; x++) {
				if ((x % 8) == 0) {
					dcol = *pcol++;
					dmsk = *pmsk++;
				} else {
					dcol >>= 1;
					dmsk >>= 1;
				}

				if (dmsk & 1) {
					op = (dcol & 1) ? 1 : 3;
					op <<= ((x % 4) * 2);

					op |= readb(dst + (x / 4));
					writeb(op, dst + (x / 4));
				}
			}
			dst += (64*2)/8;
		}
	}

	sm501fb_sync_regs(fbi);	/* ensure cursor data flushed */
	return 0;
}

/* sm501fb_crtsrc_show
 *
 * device attribute code to show where the crt output is sourced from
*/

static ssize_t sm501fb_crtsrc_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct sm501fb_info *info = dev_get_drvdata(dev);
	unsigned long ctrl;

	ctrl = smc501_readl(info->regs + SM501_DC_CRT_CONTROL);
	ctrl &= SM501_DC_CRT_CONTROL_SEL;

	return sysfs_emit(buf, "%s\n", ctrl ? "crt" : "panel");
}

/* sm501fb_crtsrc_show
 *
 * device attribute code to set where the crt output is sourced from
*/

static ssize_t sm501fb_crtsrc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct sm501fb_info *info = dev_get_drvdata(dev);
	enum sm501_controller head;
	unsigned long ctrl;

	if (len < 1)
		return -EINVAL;

	if (strncasecmp(buf, "crt", 3) == 0)
		head = HEAD_CRT;
	else if (strncasecmp(buf, "panel", 5) == 0)
		head = HEAD_PANEL;
	else
		return -EINVAL;

	dev_info(dev, "setting crt source to head %d\n", head);

	ctrl = smc501_readl(info->regs + SM501_DC_CRT_CONTROL);

	if (head == HEAD_CRT) {
		ctrl |= SM501_DC_CRT_CONTROL_SEL;
		ctrl |= SM501_DC_CRT_CONTROL_ENABLE;
		ctrl |= SM501_DC_CRT_CONTROL_TE;
	} else {
		ctrl &= ~SM501_DC_CRT_CONTROL_SEL;
		ctrl &= ~SM501_DC_CRT_CONTROL_ENABLE;
		ctrl &= ~SM501_DC_CRT_CONTROL_TE;
	}

	smc501_writel(ctrl, info->regs + SM501_DC_CRT_CONTROL);
	sm501fb_sync_regs(info);

	return len;
}

/* Prepare the device_attr for registration with sysfs later */
static DEVICE_ATTR(crt_src, 0664, sm501fb_crtsrc_show, sm501fb_crtsrc_store);

/* sm501fb_show_regs
 *
 * show the primary sm501 registers
*/
static int sm501fb_show_regs(struct sm501fb_info *info, char *ptr,
			     unsigned int start, unsigned int len)
{
	void __iomem *mem = info->regs;
	char *buf = ptr;
	unsigned int reg;

	for (reg = start; reg < (len + start); reg += 4)
		ptr += sprintf(ptr, "%08x = %08x\n", reg,
				smc501_readl(mem + reg));

	return ptr - buf;
}

/* sm501fb_debug_show_crt
 *
 * show the crt control and cursor registers
*/

static ssize_t sm501fb_debug_show_crt(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sm501fb_info *info = dev_get_drvdata(dev);
	char *ptr = buf;

	ptr += sm501fb_show_regs(info, ptr, SM501_DC_CRT_CONTROL, 0x40);
	ptr += sm501fb_show_regs(info, ptr, SM501_DC_CRT_HWC_BASE, 0x10);

	return ptr - buf;
}

static DEVICE_ATTR(fbregs_crt, 0444, sm501fb_debug_show_crt, NULL);

/* sm501fb_debug_show_pnl
 *
 * show the panel control and cursor registers
*/

static ssize_t sm501fb_debug_show_pnl(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sm501fb_info *info = dev_get_drvdata(dev);
	char *ptr = buf;

	ptr += sm501fb_show_regs(info, ptr, 0x0, 0x40);
	ptr += sm501fb_show_regs(info, ptr, SM501_DC_PANEL_HWC_BASE, 0x10);

	return ptr - buf;
}

static DEVICE_ATTR(fbregs_pnl, 0444, sm501fb_debug_show_pnl, NULL);

static struct attribute *sm501fb_attrs[] = {
	&dev_attr_crt_src.attr,
	&dev_attr_fbregs_pnl.attr,
	&dev_attr_fbregs_crt.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sm501fb);

/* acceleration operations */
static int sm501fb_sync(struct fb_info *info)
{
	int count = 1000000;
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;

	/* wait for the 2d engine to be ready */
	while ((count > 0) &&
	       (smc501_readl(fbi->regs + SM501_SYSTEM_CONTROL) &
		SM501_SYSCTRL_2D_ENGINE_STATUS) != 0)
		count--;

	if (count <= 0) {
		fb_err(info, "Timeout waiting for 2d engine sync\n");
		return 1;
	}
	return 0;
}

static void sm501fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	int width = area->width;
	int height = area->height;
	int sx = area->sx;
	int sy = area->sy;
	int dx = area->dx;
	int dy = area->dy;
	unsigned long rtl = 0;

	/* source clip */
	if ((sx >= info->var.xres_virtual) ||
	    (sy >= info->var.yres_virtual))
		/* source Area not within virtual screen, skipping */
		return;
	if ((sx + width) >= info->var.xres_virtual)
		width = info->var.xres_virtual - sx - 1;
	if ((sy + height) >= info->var.yres_virtual)
		height = info->var.yres_virtual - sy - 1;

	/* dest clip */
	if ((dx >= info->var.xres_virtual) ||
	    (dy >= info->var.yres_virtual))
		/* Destination Area not within virtual screen, skipping */
		return;
	if ((dx + width) >= info->var.xres_virtual)
		width = info->var.xres_virtual - dx - 1;
	if ((dy + height) >= info->var.yres_virtual)
		height = info->var.yres_virtual - dy - 1;

	if ((sx < dx) || (sy < dy)) {
		rtl = 1 << 27;
		sx += width - 1;
		dx += width - 1;
		sy += height - 1;
		dy += height - 1;
	}

	if (sm501fb_sync(info))
		return;

	/* set the base addresses */
	smc501_writel(par->screen.sm_addr, fbi->regs2d + SM501_2D_SOURCE_BASE);
	smc501_writel(par->screen.sm_addr,
			fbi->regs2d + SM501_2D_DESTINATION_BASE);

	/* set the window width */
	smc501_writel((info->var.xres << 16) | info->var.xres,
	       fbi->regs2d + SM501_2D_WINDOW_WIDTH);

	/* set window stride */
	smc501_writel((info->var.xres_virtual << 16) | info->var.xres_virtual,
	       fbi->regs2d + SM501_2D_PITCH);

	/* set data format */
	switch (info->var.bits_per_pixel) {
	case 8:
		smc501_writel(0, fbi->regs2d + SM501_2D_STRETCH);
		break;
	case 16:
		smc501_writel(0x00100000, fbi->regs2d + SM501_2D_STRETCH);
		break;
	case 32:
		smc501_writel(0x00200000, fbi->regs2d + SM501_2D_STRETCH);
		break;
	}

	/* 2d compare mask */
	smc501_writel(0xffffffff, fbi->regs2d + SM501_2D_COLOR_COMPARE_MASK);

	/* 2d mask */
	smc501_writel(0xffffffff, fbi->regs2d + SM501_2D_MASK);

	/* source and destination x y */
	smc501_writel((sx << 16) | sy, fbi->regs2d + SM501_2D_SOURCE);
	smc501_writel((dx << 16) | dy, fbi->regs2d + SM501_2D_DESTINATION);

	/* w/h */
	smc501_writel((width << 16) | height, fbi->regs2d + SM501_2D_DIMENSION);

	/* do area move */
	smc501_writel(0x800000cc | rtl, fbi->regs2d + SM501_2D_CONTROL);
}

static void sm501fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct sm501fb_par  *par = info->par;
	struct sm501fb_info *fbi = par->info;
	int width = rect->width, height = rect->height;

	if ((rect->dx >= info->var.xres_virtual) ||
	    (rect->dy >= info->var.yres_virtual))
		/* Rectangle not within virtual screen, skipping */
		return;
	if ((rect->dx + width) >= info->var.xres_virtual)
		width = info->var.xres_virtual - rect->dx - 1;
	if ((rect->dy + height) >= info->var.yres_virtual)
		height = info->var.yres_virtual - rect->dy - 1;

	if (sm501fb_sync(info))
		return;

	/* set the base addresses */
	smc501_writel(par->screen.sm_addr, fbi->regs2d + SM501_2D_SOURCE_BASE);
	smc501_writel(par->screen.sm_addr,
			fbi->regs2d + SM501_2D_DESTINATION_BASE);

	/* set the window width */
	smc501_writel((info->var.xres << 16) | info->var.xres,
	       fbi->regs2d + SM501_2D_WINDOW_WIDTH);

	/* set window stride */
	smc501_writel((info->var.xres_virtual << 16) | info->var.xres_virtual,
	       fbi->regs2d + SM501_2D_PITCH);

	/* set data format */
	switch (info->var.bits_per_pixel) {
	case 8:
		smc501_writel(0, fbi->regs2d + SM501_2D_STRETCH);
		break;
	case 16:
		smc501_writel(0x00100000, fbi->regs2d + SM501_2D_STRETCH);
		break;
	case 32:
		smc501_writel(0x00200000, fbi->regs2d + SM501_2D_STRETCH);
		break;
	}

	/* 2d compare mask */
	smc501_writel(0xffffffff, fbi->regs2d + SM501_2D_COLOR_COMPARE_MASK);

	/* 2d mask */
	smc501_writel(0xffffffff, fbi->regs2d + SM501_2D_MASK);

	/* colour */
	smc501_writel(rect->color, fbi->regs2d + SM501_2D_FOREGROUND);

	/* x y */
	smc501_writel((rect->dx << 16) | rect->dy,
			fbi->regs2d + SM501_2D_DESTINATION);

	/* w/h */
	smc501_writel((width << 16) | height, fbi->regs2d + SM501_2D_DIMENSION);

	/* do rectangle fill */
	smc501_writel(0x800100cc, fbi->regs2d + SM501_2D_CONTROL);
}


static struct fb_ops sm501fb_ops_crt = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_check_var	= sm501fb_check_var_crt,
	.fb_set_par	= sm501fb_set_par_crt,
	.fb_blank	= sm501fb_blank_crt,
	.fb_setcolreg	= sm501fb_setcolreg,
	.fb_pan_display	= sm501fb_pan_crt,
	.fb_cursor	= sm501fb_cursor,
	.fb_fillrect	= sm501fb_fillrect,
	.fb_copyarea	= sm501fb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_sync	= sm501fb_sync,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

static struct fb_ops sm501fb_ops_pnl = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_check_var	= sm501fb_check_var_pnl,
	.fb_set_par	= sm501fb_set_par_pnl,
	.fb_pan_display	= sm501fb_pan_pnl,
	.fb_blank	= sm501fb_blank_pnl,
	.fb_setcolreg	= sm501fb_setcolreg,
	.fb_cursor	= sm501fb_cursor,
	.fb_fillrect	= sm501fb_fillrect,
	.fb_copyarea	= sm501fb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_sync	= sm501fb_sync,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

/* sm501_init_cursor
 *
 * initialise hw cursor parameters
*/

static int sm501_init_cursor(struct fb_info *fbi, unsigned int reg_base)
{
	struct sm501fb_par *par;
	struct sm501fb_info *info;
	int ret;

	if (fbi == NULL)
		return 0;

	par = fbi->par;
	info = par->info;

	par->cursor_regs = info->regs + reg_base;

	ret = sm501_alloc_mem(info, &par->cursor, SM501_MEMF_CURSOR, 1024,
			      fbi->fix.smem_len);
	if (ret < 0)
		return ret;

	/* initialise the colour registers */

	smc501_writel(par->cursor.sm_addr,
			par->cursor_regs + SM501_OFF_HWC_ADDR);

	smc501_writel(0x00, par->cursor_regs + SM501_OFF_HWC_LOC);
	smc501_writel(0x00, par->cursor_regs + SM501_OFF_HWC_COLOR_1_2);
	smc501_writel(0x00, par->cursor_regs + SM501_OFF_HWC_COLOR_3);
	sm501fb_sync_regs(info);

	return 0;
}

/* sm501fb_info_start
 *
 * fills the par structure claiming resources and remapping etc.
*/

static int sm501fb_start(struct sm501fb_info *info,
			 struct platform_device *pdev)
{
	struct resource	*res;
	struct device *dev = &pdev->dev;
	int k;
	int ret;

	info->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		/* we currently do not use the IRQ */
		dev_warn(dev, "no irq for device\n");
	}

	/* allocate, reserve and remap resources for display
	 * controller registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "no resource definition for registers\n");
		ret = -ENOENT;
		goto err_release;
	}

	info->regs_res = request_mem_region(res->start,
					    resource_size(res),
					    pdev->name);

	if (info->regs_res == NULL) {
		dev_err(dev, "cannot claim registers\n");
		ret = -ENXIO;
		goto err_release;
	}

	info->regs = ioremap(res->start, resource_size(res));
	if (info->regs == NULL) {
		dev_err(dev, "cannot remap registers\n");
		ret = -ENXIO;
		goto err_regs_res;
	}

	/* allocate, reserve and remap resources for 2d
	 * controller registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res == NULL) {
		dev_err(dev, "no resource definition for 2d registers\n");
		ret = -ENOENT;
		goto err_regs_map;
	}

	info->regs2d_res = request_mem_region(res->start,
					      resource_size(res),
					      pdev->name);

	if (info->regs2d_res == NULL) {
		dev_err(dev, "cannot claim registers\n");
		ret = -ENXIO;
		goto err_regs_map;
	}

	info->regs2d = ioremap(res->start, resource_size(res));
	if (info->regs2d == NULL) {
		dev_err(dev, "cannot remap registers\n");
		ret = -ENXIO;
		goto err_regs2d_res;
	}

	/* allocate, reserve resources for framebuffer */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res == NULL) {
		dev_err(dev, "no memory resource defined\n");
		ret = -ENXIO;
		goto err_regs2d_map;
	}

	info->fbmem_res = request_mem_region(res->start,
					     resource_size(res),
					     pdev->name);
	if (info->fbmem_res == NULL) {
		dev_err(dev, "cannot claim framebuffer\n");
		ret = -ENXIO;
		goto err_regs2d_map;
	}

	info->fbmem = ioremap(res->start, resource_size(res));
	if (info->fbmem == NULL) {
		dev_err(dev, "cannot remap framebuffer\n");
		ret = -ENXIO;
		goto err_mem_res;
	}

	info->fbmem_len = resource_size(res);

	/* clear framebuffer memory - avoids garbage data on unused fb */
	memset_io(info->fbmem, 0, info->fbmem_len);

	/* clear palette ram - undefined at power on */
	for (k = 0; k < (256 * 3); k++)
		smc501_writel(0, info->regs + SM501_DC_PANEL_PALETTE + (k * 4));

	/* enable display controller */
	sm501_unit_power(dev->parent, SM501_GATE_DISPLAY, 1);

	/* enable 2d controller */
	sm501_unit_power(dev->parent, SM501_GATE_2D_ENGINE, 1);

	/* setup cursors */
	sm501_init_cursor(info->fb[HEAD_CRT], SM501_DC_CRT_HWC_ADDR);
	sm501_init_cursor(info->fb[HEAD_PANEL], SM501_DC_PANEL_HWC_ADDR);

	return 0; /* everything is setup */

 err_mem_res:
	release_mem_region(info->fbmem_res->start,
			   resource_size(info->fbmem_res));

 err_regs2d_map:
	iounmap(info->regs2d);

 err_regs2d_res:
	release_mem_region(info->regs2d_res->start,
			   resource_size(info->regs2d_res));

 err_regs_map:
	iounmap(info->regs);

 err_regs_res:
	release_mem_region(info->regs_res->start,
			   resource_size(info->regs_res));

 err_release:
	return ret;
}

static void sm501fb_stop(struct sm501fb_info *info)
{
	/* disable display controller */
	sm501_unit_power(info->dev->parent, SM501_GATE_DISPLAY, 0);

	iounmap(info->fbmem);
	release_mem_region(info->fbmem_res->start,
			   resource_size(info->fbmem_res));

	iounmap(info->regs2d);
	release_mem_region(info->regs2d_res->start,
			   resource_size(info->regs2d_res));

	iounmap(info->regs);
	release_mem_region(info->regs_res->start,
			   resource_size(info->regs_res));
}

static int sm501fb_init_fb(struct fb_info *fb, enum sm501_controller head,
			   const char *fbname)
{
	struct sm501_platdata_fbsub *pd;
	struct sm501fb_par *par = fb->par;
	struct sm501fb_info *info = par->info;
	unsigned long ctrl;
	unsigned int enable;
	int ret;

	switch (head) {
	case HEAD_CRT:
		pd = info->pdata->fb_crt;
		ctrl = smc501_readl(info->regs + SM501_DC_CRT_CONTROL);
		enable = (ctrl & SM501_DC_CRT_CONTROL_ENABLE) ? 1 : 0;

		/* ensure we set the correct source register */
		if (info->pdata->fb_route != SM501_FB_CRT_PANEL) {
			ctrl |= SM501_DC_CRT_CONTROL_SEL;
			smc501_writel(ctrl, info->regs + SM501_DC_CRT_CONTROL);
		}

		break;

	case HEAD_PANEL:
		pd = info->pdata->fb_pnl;
		ctrl = smc501_readl(info->regs + SM501_DC_PANEL_CONTROL);
		enable = (ctrl & SM501_DC_PANEL_CONTROL_EN) ? 1 : 0;
		break;

	default:
		pd = NULL;		/* stop compiler warnings */
		ctrl = 0;
		enable = 0;
		BUG();
	}

	dev_info(info->dev, "fb %s %sabled at start\n",
		 fbname, enable ? "en" : "dis");

	/* check to see if our routing allows this */

	if (head == HEAD_CRT && info->pdata->fb_route == SM501_FB_CRT_PANEL) {
		ctrl &= ~SM501_DC_CRT_CONTROL_SEL;
		smc501_writel(ctrl, info->regs + SM501_DC_CRT_CONTROL);
		enable = 0;
	}

	strscpy(fb->fix.id, fbname, sizeof(fb->fix.id));

	memcpy(&par->ops,
	       (head == HEAD_CRT) ? &sm501fb_ops_crt : &sm501fb_ops_pnl,
	       sizeof(struct fb_ops));

	/* update ops dependent on what we've been passed */

	if ((pd->flags & SM501FB_FLAG_USE_HWCURSOR) == 0)
		par->ops.fb_cursor = NULL;

	fb->fbops = &par->ops;
	fb->flags = FBINFO_READS_FAST |
		FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;

#if defined(CONFIG_OF)
#ifdef __BIG_ENDIAN
	if (of_property_read_bool(info->dev->parent->of_node, "little-endian"))
		fb->flags |= FBINFO_FOREIGN_ENDIAN;
#else
	if (of_property_read_bool(info->dev->parent->of_node, "big-endian"))
		fb->flags |= FBINFO_FOREIGN_ENDIAN;
#endif
#endif
	/* fixed data */

	fb->fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->fix.type_aux	= 0;
	fb->fix.xpanstep	= 1;
	fb->fix.ypanstep	= 1;
	fb->fix.ywrapstep	= 0;
	fb->fix.accel		= FB_ACCEL_NONE;

	/* screenmode */

	fb->var.nonstd		= 0;
	fb->var.activate	= FB_ACTIVATE_NOW;
	fb->var.accel_flags	= 0;
	fb->var.vmode		= FB_VMODE_NONINTERLACED;
	fb->var.bits_per_pixel  = 16;

	if (info->edid_data) {
			/* Now build modedb from EDID */
			fb_edid_to_monspecs(info->edid_data, &fb->monspecs);
			fb_videomode_to_modelist(fb->monspecs.modedb,
						 fb->monspecs.modedb_len,
						 &fb->modelist);
	}

	if (enable && (pd->flags & SM501FB_FLAG_USE_INIT_MODE) && 0) {
		/* TODO read the mode from the current display */
	} else {
		if (pd->def_mode) {
			dev_info(info->dev, "using supplied mode\n");
			fb_videomode_to_var(&fb->var, pd->def_mode);

			fb->var.bits_per_pixel = pd->def_bpp ? pd->def_bpp : 8;
			fb->var.xres_virtual = fb->var.xres;
			fb->var.yres_virtual = fb->var.yres;
		} else {
			if (info->edid_data) {
				ret = fb_find_mode(&fb->var, fb, fb_mode,
					fb->monspecs.modedb,
					fb->monspecs.modedb_len,
					&sm501_default_mode, default_bpp);
				/* edid_data is no longer needed, free it */
				kfree(info->edid_data);
			} else {
				ret = fb_find_mode(&fb->var, fb,
					   NULL, NULL, 0, NULL, 8);
			}

			switch (ret) {
			case 1:
				dev_info(info->dev, "using mode specified in "
						"@mode\n");
				break;
			case 2:
				dev_info(info->dev, "using mode specified in "
					"@mode with ignored refresh rate\n");
				break;
			case 3:
				dev_info(info->dev, "using mode default "
					"mode\n");
				break;
			case 4:
				dev_info(info->dev, "using mode from list\n");
				break;
			default:
				dev_info(info->dev, "ret = %d\n", ret);
				dev_info(info->dev, "failed to find mode\n");
				return -EINVAL;
			}
		}
	}

	/* initialise and set the palette */
	if (fb_alloc_cmap(&fb->cmap, NR_PALETTE, 0)) {
		dev_err(info->dev, "failed to allocate cmap memory\n");
		return -ENOMEM;
	}
	fb_set_cmap(&fb->cmap, fb);

	ret = (fb->fbops->fb_check_var)(&fb->var, fb);
	if (ret)
		dev_err(info->dev, "check_var() failed on initial setup?\n");

	return 0;
}

/* default platform data if none is supplied (ie, PCI device) */

static struct sm501_platdata_fbsub sm501fb_pdata_crt = {
	.flags		= (SM501FB_FLAG_USE_INIT_MODE |
			   SM501FB_FLAG_USE_HWCURSOR |
			   SM501FB_FLAG_USE_HWACCEL |
			   SM501FB_FLAG_DISABLE_AT_EXIT),

};

static struct sm501_platdata_fbsub sm501fb_pdata_pnl = {
	.flags		= (SM501FB_FLAG_USE_INIT_MODE |
			   SM501FB_FLAG_USE_HWCURSOR |
			   SM501FB_FLAG_USE_HWACCEL |
			   SM501FB_FLAG_DISABLE_AT_EXIT),
};

static struct sm501_platdata_fb sm501fb_def_pdata = {
	.fb_route		= SM501_FB_OWN,
	.fb_crt			= &sm501fb_pdata_crt,
	.fb_pnl			= &sm501fb_pdata_pnl,
};

static char driver_name_crt[] = "sm501fb-crt";
static char driver_name_pnl[] = "sm501fb-panel";

static int sm501fb_probe_one(struct sm501fb_info *info,
			     enum sm501_controller head)
{
	unsigned char *name = (head == HEAD_CRT) ? "crt" : "panel";
	struct sm501_platdata_fbsub *pd;
	struct sm501fb_par *par;
	struct fb_info *fbi;

	pd = (head == HEAD_CRT) ? info->pdata->fb_crt : info->pdata->fb_pnl;

	/* Do not initialise if we've not been given any platform data */
	if (pd == NULL) {
		dev_info(info->dev, "no data for fb %s (disabled)\n", name);
		return 0;
	}

	fbi = framebuffer_alloc(sizeof(struct sm501fb_par), info->dev);
	if (!fbi)
		return -ENOMEM;

	par = fbi->par;
	par->info = info;
	par->head = head;
	fbi->pseudo_palette = &par->pseudo_palette;

	info->fb[head] = fbi;

	return 0;
}

/* Free up anything allocated by sm501fb_init_fb */

static void sm501_free_init_fb(struct sm501fb_info *info,
				enum sm501_controller head)
{
	struct fb_info *fbi = info->fb[head];

	if (!fbi)
		return;

	fb_dealloc_cmap(&fbi->cmap);
}

static int sm501fb_start_one(struct sm501fb_info *info,
			     enum sm501_controller head, const char *drvname)
{
	struct fb_info *fbi = info->fb[head];
	int ret;

	if (!fbi)
		return 0;

	mutex_init(&info->fb[head]->mm_lock);

	ret = sm501fb_init_fb(info->fb[head], head, drvname);
	if (ret) {
		dev_err(info->dev, "cannot initialise fb %s\n", drvname);
		return ret;
	}

	ret = register_framebuffer(info->fb[head]);
	if (ret) {
		dev_err(info->dev, "failed to register fb %s\n", drvname);
		sm501_free_init_fb(info, head);
		return ret;
	}

	dev_info(info->dev, "fb%d: %s frame buffer\n", fbi->node, fbi->fix.id);

	return 0;
}

static int sm501fb_probe(struct platform_device *pdev)
{
	struct sm501fb_info *info;
	struct device *dev = &pdev->dev;
	int ret;

	/* allocate our framebuffers */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "failed to allocate state\n");
		return -ENOMEM;
	}

	info->dev = dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	if (dev->parent->platform_data) {
		struct sm501_platdata *pd = dev->parent->platform_data;
		info->pdata = pd->fb;
	}

	if (info->pdata == NULL) {
		int found = 0;
#if defined(CONFIG_OF)
		struct device_node *np = pdev->dev.parent->of_node;
		const u8 *prop;
		const char *cp;
		int len;

		info->pdata = &sm501fb_def_pdata;
		if (np) {
			/* Get EDID */
			cp = of_get_property(np, "mode", &len);
			if (cp)
				strcpy(fb_mode, cp);
			prop = of_get_property(np, "edid", &len);
			if (prop && len == EDID_LENGTH) {
				info->edid_data = kmemdup(prop, EDID_LENGTH,
							  GFP_KERNEL);
				if (info->edid_data)
					found = 1;
			}
		}
#endif
		if (!found) {
			dev_info(dev, "using default configuration data\n");
			info->pdata = &sm501fb_def_pdata;
		}
	}

	/* probe for the presence of each panel */

	ret = sm501fb_probe_one(info, HEAD_CRT);
	if (ret < 0) {
		dev_err(dev, "failed to probe CRT\n");
		goto err_alloc;
	}

	ret = sm501fb_probe_one(info, HEAD_PANEL);
	if (ret < 0) {
		dev_err(dev, "failed to probe PANEL\n");
		goto err_probed_crt;
	}

	if (info->fb[HEAD_PANEL] == NULL &&
	    info->fb[HEAD_CRT] == NULL) {
		dev_err(dev, "no framebuffers found\n");
		ret = -ENODEV;
		goto err_alloc;
	}

	/* get the resources for both of the framebuffers */

	ret = sm501fb_start(info, pdev);
	if (ret) {
		dev_err(dev, "cannot initialise SM501\n");
		goto err_probed_panel;
	}

	ret = sm501fb_start_one(info, HEAD_CRT, driver_name_crt);
	if (ret) {
		dev_err(dev, "failed to start CRT\n");
		goto err_started;
	}

	ret = sm501fb_start_one(info, HEAD_PANEL, driver_name_pnl);
	if (ret) {
		dev_err(dev, "failed to start Panel\n");
		goto err_started_crt;
	}

	/* we registered, return ok */
	return 0;

err_started_crt:
	unregister_framebuffer(info->fb[HEAD_CRT]);
	sm501_free_init_fb(info, HEAD_CRT);

err_started:
	sm501fb_stop(info);

err_probed_panel:
	framebuffer_release(info->fb[HEAD_PANEL]);

err_probed_crt:
	framebuffer_release(info->fb[HEAD_CRT]);

err_alloc:
	kfree(info);

	return ret;
}


/*
 *  Cleanup
 */
static void sm501fb_remove(struct platform_device *pdev)
{
	struct sm501fb_info *info = platform_get_drvdata(pdev);
	struct fb_info	   *fbinfo_crt = info->fb[0];
	struct fb_info	   *fbinfo_pnl = info->fb[1];

	sm501_free_init_fb(info, HEAD_CRT);
	sm501_free_init_fb(info, HEAD_PANEL);

	if (fbinfo_crt)
		unregister_framebuffer(fbinfo_crt);
	if (fbinfo_pnl)
		unregister_framebuffer(fbinfo_pnl);

	sm501fb_stop(info);
	kfree(info);

	framebuffer_release(fbinfo_pnl);
	framebuffer_release(fbinfo_crt);
}

#ifdef CONFIG_PM

static int sm501fb_suspend_fb(struct sm501fb_info *info,
			      enum sm501_controller head)
{
	struct fb_info *fbi = info->fb[head];
	struct sm501fb_par *par;

	if (!fbi)
		return 0;

	par = fbi->par;
	if (par->screen.size == 0)
		return 0;

	/* blank the relevant interface to ensure unit power minimised */
	(par->ops.fb_blank)(FB_BLANK_POWERDOWN, fbi);

	/* tell console/fb driver we are suspending */

	console_lock();
	fb_set_suspend(fbi, 1);
	console_unlock();

	/* backup copies in case chip is powered down over suspend */

	par->store_fb = vmalloc(par->screen.size);
	if (par->store_fb == NULL) {
		dev_err(info->dev, "no memory to store screen\n");
		return -ENOMEM;
	}

	par->store_cursor = vmalloc(par->cursor.size);
	if (par->store_cursor == NULL) {
		dev_err(info->dev, "no memory to store cursor\n");
		goto err_nocursor;
	}

	dev_dbg(info->dev, "suspending screen to %p\n", par->store_fb);
	dev_dbg(info->dev, "suspending cursor to %p\n", par->store_cursor);

	memcpy_fromio(par->store_fb, par->screen.k_addr, par->screen.size);
	memcpy_fromio(par->store_cursor, par->cursor.k_addr, par->cursor.size);

	return 0;

 err_nocursor:
	vfree(par->store_fb);
	par->store_fb = NULL;

	return -ENOMEM;
}

static void sm501fb_resume_fb(struct sm501fb_info *info,
			      enum sm501_controller head)
{
	struct fb_info *fbi = info->fb[head];
	struct sm501fb_par *par;

	if (!fbi)
		return;

	par = fbi->par;
	if (par->screen.size == 0)
		return;

	/* re-activate the configuration */

	(par->ops.fb_set_par)(fbi);

	/* restore the data */

	dev_dbg(info->dev, "restoring screen from %p\n", par->store_fb);
	dev_dbg(info->dev, "restoring cursor from %p\n", par->store_cursor);

	if (par->store_fb)
		memcpy_toio(par->screen.k_addr, par->store_fb,
			    par->screen.size);

	if (par->store_cursor)
		memcpy_toio(par->cursor.k_addr, par->store_cursor,
			    par->cursor.size);

	console_lock();
	fb_set_suspend(fbi, 0);
	console_unlock();

	vfree(par->store_fb);
	vfree(par->store_cursor);
}


/* suspend and resume support */

static int sm501fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sm501fb_info *info = platform_get_drvdata(pdev);

	/* store crt control to resume with */
	info->pm_crt_ctrl = smc501_readl(info->regs + SM501_DC_CRT_CONTROL);

	sm501fb_suspend_fb(info, HEAD_CRT);
	sm501fb_suspend_fb(info, HEAD_PANEL);

	/* turn off the clocks, in case the device is not powered down */
	sm501_unit_power(info->dev->parent, SM501_GATE_DISPLAY, 0);

	return 0;
}

#define SM501_CRT_CTRL_SAVE (SM501_DC_CRT_CONTROL_TVP |        \
			     SM501_DC_CRT_CONTROL_SEL)


static int sm501fb_resume(struct platform_device *pdev)
{
	struct sm501fb_info *info = platform_get_drvdata(pdev);
	unsigned long crt_ctrl;

	sm501_unit_power(info->dev->parent, SM501_GATE_DISPLAY, 1);

	/* restore the items we want to be saved for crt control */

	crt_ctrl = smc501_readl(info->regs + SM501_DC_CRT_CONTROL);
	crt_ctrl &= ~SM501_CRT_CTRL_SAVE;
	crt_ctrl |= info->pm_crt_ctrl & SM501_CRT_CTRL_SAVE;
	smc501_writel(crt_ctrl, info->regs + SM501_DC_CRT_CONTROL);

	sm501fb_resume_fb(info, HEAD_CRT);
	sm501fb_resume_fb(info, HEAD_PANEL);

	return 0;
}

#else
#define sm501fb_suspend NULL
#define sm501fb_resume  NULL
#endif

static struct platform_driver sm501fb_driver = {
	.probe		= sm501fb_probe,
	.remove_new	= sm501fb_remove,
	.suspend	= sm501fb_suspend,
	.resume		= sm501fb_resume,
	.driver		= {
		.name	= "sm501-fb",
		.dev_groups	= sm501fb_groups,
	},
};

module_platform_driver(sm501fb_driver);

module_param_named(mode, fb_mode, charp, 0);
MODULE_PARM_DESC(mode,
	"Specify resolution as \"<xres>x<yres>[-<bpp>][@<refresh>]\" ");
module_param_named(bpp, default_bpp, ulong, 0);
MODULE_PARM_DESC(bpp, "Specify bit-per-pixel if not specified mode");
MODULE_AUTHOR("Ben Dooks, Vincent Sanders");
MODULE_DESCRIPTION("SM501 Framebuffer driver");
MODULE_LICENSE("GPL v2");
