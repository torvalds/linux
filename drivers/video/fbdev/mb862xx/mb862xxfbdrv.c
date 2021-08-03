// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/mb862xx/mb862xxfb.c
 *
 * Fujitsu Carmine/Coral-P(A)/Lime framebuffer driver
 *
 * (C) 2008 Anatolij Gustschin <agust@denx.de>
 * DENX Software Engineering
 */

#undef DEBUG

#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#if defined(CONFIG_OF)
#include <linux/of_platform.h>
#endif
#include "mb862xxfb.h"
#include "mb862xx_reg.h"

#define NR_PALETTE		256
#define MB862XX_MEM_SIZE	0x1000000
#define CORALP_MEM_SIZE		0x2000000
#define CARMINE_MEM_SIZE	0x8000000
#define DRV_NAME		"mb862xxfb"

#if defined(CONFIG_SOCRATES)
static struct mb862xx_gc_mode socrates_gc_mode = {
	/* Mode for Prime View PM070WL4 TFT LCD Panel */
	{ "800x480", 45, 800, 480, 40000, 86, 42, 33, 10, 128, 2, 0, 0, 0 },
	/* 16 bits/pixel, 16MB, 133MHz, SDRAM memory mode value */
	16, 0x1000000, GC_CCF_COT_133, 0x4157ba63
};
#endif

/* Helpers */
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

static inline int hsp(struct fb_var_screeninfo *var)
{
	return var->xres + var->right_margin - 1;
}

static inline int vsp(struct fb_var_screeninfo *var)
{
	return var->yres + var->lower_margin - 1;
}

static inline int d_pitch(struct fb_var_screeninfo *var)
{
	return var->xres * var->bits_per_pixel / 8;
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int mb862xxfb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	struct mb862xxfb_par *par = info->par;
	unsigned int val;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			par->pseudo_palette[regno] = val;
		}
		break;
	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < 256) {
			val = (red >> 8) << 16;
			val |= (green >> 8) << 8;
			val |= blue >> 8;
			outreg(disp, GC_L0PAL0 + (regno * 4), val);
		}
		break;
	default:
		return 1;   /* unsupported type */
	}
	return 0;
}

static int mb862xxfb_check_var(struct fb_var_screeninfo *var,
			       struct fb_info *fbi)
{
	unsigned long tmp;

	if (fbi->dev)
		dev_dbg(fbi->dev, "%s\n", __func__);

	/* check if these values fit into the registers */
	if (var->hsync_len > 255 || var->vsync_len > 255)
		return -EINVAL;

	if ((var->xres + var->right_margin) >= 4096)
		return -EINVAL;

	if ((var->yres + var->lower_margin) > 4096)
		return -EINVAL;

	if (h_total(var) > 4096 || v_total(var) > 4096)
		return -EINVAL;

	if (var->xres_virtual > 4096 || var->yres_virtual > 4096)
		return -EINVAL;

	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;

	/*
	 * can cope with 8,16 or 24/32bpp if resulting
	 * pitch is divisible by 64 without remainder
	 */
	if (d_pitch(&fbi->var) % GC_L0M_L0W_UNIT) {
		int r;

		var->bits_per_pixel = 0;
		do {
			var->bits_per_pixel += 8;
			r = d_pitch(&fbi->var) % GC_L0M_L0W_UNIT;
		} while (r && var->bits_per_pixel <= 32);

		if (d_pitch(&fbi->var) % GC_L0M_L0W_UNIT)
			return -EINVAL;
	}

	/* line length is going to be 128 bit aligned */
	tmp = (var->xres * var->bits_per_pixel) / 8;
	if ((tmp & 15) != 0)
		return -EINVAL;

	/* set r/g/b positions and validate bpp */
	switch (var->bits_per_pixel) {
	case 8:
		var->red.length		= var->bits_per_pixel;
		var->green.length	= var->bits_per_pixel;
		var->blue.length	= var->bits_per_pixel;
		var->red.offset		= 0;
		var->green.offset	= 0;
		var->blue.offset	= 0;
		var->transp.length	= 0;
		break;
	case 16:
		var->red.length		= 5;
		var->green.length	= 5;
		var->blue.length	= 5;
		var->red.offset		= 10;
		var->green.offset	= 5;
		var->blue.offset	= 0;
		var->transp.length	= 0;
		break;
	case 24:
	case 32:
		var->transp.length	= 8;
		var->red.length		= 8;
		var->green.length	= 8;
		var->blue.length	= 8;
		var->transp.offset	= 24;
		var->red.offset		= 16;
		var->green.offset	= 8;
		var->blue.offset	= 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct fb_ops mb862xxfb_ops;

/*
 * set display parameters
 */
static int mb862xxfb_set_par(struct fb_info *fbi)
{
	struct mb862xxfb_par *par = fbi->par;
	unsigned long reg, sc;

	dev_dbg(par->dev, "%s\n", __func__);
	if (par->type == BT_CORALP)
		mb862xxfb_init_accel(fbi, &mb862xxfb_ops, fbi->var.xres);

	if (par->pre_init)
		return 0;

	/* disp off */
	reg = inreg(disp, GC_DCM1);
	reg &= ~GC_DCM01_DEN;
	outreg(disp, GC_DCM1, reg);

	/* set display reference clock div. */
	sc = par->refclk / (1000000 / fbi->var.pixclock) - 1;
	reg = inreg(disp, GC_DCM1);
	reg &= ~(GC_DCM01_CKS | GC_DCM01_RESV | GC_DCM01_SC);
	reg |= sc << 8;
	outreg(disp, GC_DCM1, reg);
	dev_dbg(par->dev, "SC 0x%lx\n", sc);

	/* disp dimension, format */
	reg =  pack(d_pitch(&fbi->var) / GC_L0M_L0W_UNIT,
		    (fbi->var.yres - 1));
	if (fbi->var.bits_per_pixel == 16)
		reg |= GC_L0M_L0C_16;
	outreg(disp, GC_L0M, reg);

	if (fbi->var.bits_per_pixel == 32) {
		reg = inreg(disp, GC_L0EM);
		outreg(disp, GC_L0EM, reg | GC_L0EM_L0EC_24);
	}
	outreg(disp, GC_WY_WX, 0);
	reg = pack(fbi->var.yres - 1, fbi->var.xres);
	outreg(disp, GC_WH_WW, reg);
	outreg(disp, GC_L0OA0, 0);
	outreg(disp, GC_L0DA0, 0);
	outreg(disp, GC_L0DY_L0DX, 0);
	outreg(disp, GC_L0WY_L0WX, 0);
	outreg(disp, GC_L0WH_L0WW, reg);

	/* both HW-cursors off */
	reg = inreg(disp, GC_CPM_CUTC);
	reg &= ~(GC_CPM_CEN0 | GC_CPM_CEN1);
	outreg(disp, GC_CPM_CUTC, reg);

	/* timings */
	reg = pack(fbi->var.xres - 1, fbi->var.xres - 1);
	outreg(disp, GC_HDB_HDP, reg);
	reg = pack((fbi->var.yres - 1), vsp(&fbi->var));
	outreg(disp, GC_VDP_VSP, reg);
	reg = ((fbi->var.vsync_len - 1) << 24) |
	      pack((fbi->var.hsync_len - 1), hsp(&fbi->var));
	outreg(disp, GC_VSW_HSW_HSP, reg);
	outreg(disp, GC_HTP, pack(h_total(&fbi->var) - 1, 0));
	outreg(disp, GC_VTR, pack(v_total(&fbi->var) - 1, 0));

	/* display on */
	reg = inreg(disp, GC_DCM1);
	reg |= GC_DCM01_DEN | GC_DCM01_L0E;
	reg &= ~GC_DCM01_ESY;
	outreg(disp, GC_DCM1, reg);
	return 0;
}

static int mb862xxfb_pan(struct fb_var_screeninfo *var,
			 struct fb_info *info)
{
	struct mb862xxfb_par *par = info->par;
	unsigned long reg;

	reg = pack(var->yoffset, var->xoffset);
	outreg(disp, GC_L0WY_L0WX, reg);

	reg = pack(info->var.yres_virtual, info->var.xres_virtual);
	outreg(disp, GC_L0WH_L0WW, reg);
	return 0;
}

static int mb862xxfb_blank(int mode, struct fb_info *fbi)
{
	struct mb862xxfb_par  *par = fbi->par;
	unsigned long reg;

	dev_dbg(fbi->dev, "blank mode=%d\n", mode);

	switch (mode) {
	case FB_BLANK_POWERDOWN:
		reg = inreg(disp, GC_DCM1);
		reg &= ~GC_DCM01_DEN;
		outreg(disp, GC_DCM1, reg);
		break;
	case FB_BLANK_UNBLANK:
		reg = inreg(disp, GC_DCM1);
		reg |= GC_DCM01_DEN;
		outreg(disp, GC_DCM1, reg);
		break;
	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		return 1;
	}
	return 0;
}

static int mb862xxfb_ioctl(struct fb_info *fbi, unsigned int cmd,
			   unsigned long arg)
{
	struct mb862xxfb_par *par = fbi->par;
	struct mb862xx_l1_cfg *l1_cfg = &par->l1_cfg;
	void __user *argp = (void __user *)arg;
	int *enable;
	u32 l1em = 0;

	switch (cmd) {
	case MB862XX_L1_GET_CFG:
		if (copy_to_user(argp, l1_cfg, sizeof(*l1_cfg)))
			return -EFAULT;
		break;
	case MB862XX_L1_SET_CFG:
		if (copy_from_user(l1_cfg, argp, sizeof(*l1_cfg)))
			return -EFAULT;
		if (l1_cfg->dh == 0 || l1_cfg->dw == 0)
			return -EINVAL;
		if ((l1_cfg->sw >= l1_cfg->dw) && (l1_cfg->sh >= l1_cfg->dh)) {
			/* downscaling */
			outreg(cap, GC_CAP_CSC,
				pack((l1_cfg->sh << 11) / l1_cfg->dh,
				     (l1_cfg->sw << 11) / l1_cfg->dw));
			l1em = inreg(disp, GC_L1EM);
			l1em &= ~GC_L1EM_DM;
		} else if ((l1_cfg->sw <= l1_cfg->dw) &&
			   (l1_cfg->sh <= l1_cfg->dh)) {
			/* upscaling */
			outreg(cap, GC_CAP_CSC,
				pack((l1_cfg->sh << 11) / l1_cfg->dh,
				     (l1_cfg->sw << 11) / l1_cfg->dw));
			outreg(cap, GC_CAP_CMSS,
				pack(l1_cfg->sw >> 1, l1_cfg->sh));
			outreg(cap, GC_CAP_CMDS,
				pack(l1_cfg->dw >> 1, l1_cfg->dh));
			l1em = inreg(disp, GC_L1EM);
			l1em |= GC_L1EM_DM;
		}

		if (l1_cfg->mirror) {
			outreg(cap, GC_CAP_CBM,
				inreg(cap, GC_CAP_CBM) | GC_CBM_HRV);
			l1em |= l1_cfg->dw * 2 - 8;
		} else {
			outreg(cap, GC_CAP_CBM,
				inreg(cap, GC_CAP_CBM) & ~GC_CBM_HRV);
			l1em &= 0xffff0000;
		}
		outreg(disp, GC_L1EM, l1em);
		break;
	case MB862XX_L1_ENABLE:
		enable = (int *)arg;
		if (*enable) {
			outreg(disp, GC_L1DA, par->cap_buf);
			outreg(cap, GC_CAP_IMG_START,
				pack(l1_cfg->sy >> 1, l1_cfg->sx));
			outreg(cap, GC_CAP_IMG_END,
				pack(l1_cfg->sh, l1_cfg->sw));
			outreg(disp, GC_L1M, GC_L1M_16 | GC_L1M_YC | GC_L1M_CS |
					     (par->l1_stride << 16));
			outreg(disp, GC_L1WY_L1WX,
				pack(l1_cfg->dy, l1_cfg->dx));
			outreg(disp, GC_L1WH_L1WW,
				pack(l1_cfg->dh - 1, l1_cfg->dw));
			outreg(disp, GC_DLS, 1);
			outreg(cap, GC_CAP_VCM,
				GC_VCM_VIE | GC_VCM_CM | GC_VCM_VS_PAL);
			outreg(disp, GC_DCM1, inreg(disp, GC_DCM1) |
					      GC_DCM1_DEN | GC_DCM1_L1E);
		} else {
			outreg(cap, GC_CAP_VCM,
				inreg(cap, GC_CAP_VCM) & ~GC_VCM_VIE);
			outreg(disp, GC_DCM1,
				inreg(disp, GC_DCM1) & ~GC_DCM1_L1E);
		}
		break;
	case MB862XX_L1_CAP_CTL:
		enable = (int *)arg;
		if (*enable) {
			outreg(cap, GC_CAP_VCM,
				inreg(cap, GC_CAP_VCM) | GC_VCM_VIE);
		} else {
			outreg(cap, GC_CAP_VCM,
				inreg(cap, GC_CAP_VCM) & ~GC_VCM_VIE);
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* framebuffer ops */
static struct fb_ops mb862xxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= mb862xxfb_check_var,
	.fb_set_par	= mb862xxfb_set_par,
	.fb_setcolreg	= mb862xxfb_setcolreg,
	.fb_blank	= mb862xxfb_blank,
	.fb_pan_display	= mb862xxfb_pan,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_ioctl	= mb862xxfb_ioctl,
};

/* initialize fb_info data */
static int mb862xxfb_init_fbinfo(struct fb_info *fbi)
{
	struct mb862xxfb_par *par = fbi->par;
	struct mb862xx_gc_mode *mode = par->gc_mode;
	unsigned long reg;
	int stride;

	fbi->fbops = &mb862xxfb_ops;
	fbi->pseudo_palette = par->pseudo_palette;
	fbi->screen_base = par->fb_base;
	fbi->screen_size = par->mapped_vram;

	strcpy(fbi->fix.id, DRV_NAME);
	fbi->fix.smem_start = (unsigned long)par->fb_base_phys;
	fbi->fix.mmio_start = (unsigned long)par->mmio_base_phys;
	fbi->fix.mmio_len = par->mmio_len;
	fbi->fix.accel = FB_ACCEL_NONE;
	fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	fbi->fix.type_aux = 0;
	fbi->fix.xpanstep = 1;
	fbi->fix.ypanstep = 1;
	fbi->fix.ywrapstep = 0;

	reg = inreg(disp, GC_DCM1);
	if (reg & GC_DCM01_DEN && reg & GC_DCM01_L0E) {
		/* get the disp mode from active display cfg */
		unsigned long sc = ((reg & GC_DCM01_SC) >> 8) + 1;
		unsigned long hsp, vsp, ht, vt;

		dev_dbg(par->dev, "using bootloader's disp. mode\n");
		fbi->var.pixclock = (sc * 1000000) / par->refclk;
		fbi->var.xres = (inreg(disp, GC_HDB_HDP) & 0x0fff) + 1;
		reg = inreg(disp, GC_VDP_VSP);
		fbi->var.yres = ((reg >> 16) & 0x0fff) + 1;
		vsp = (reg & 0x0fff) + 1;
		fbi->var.xres_virtual = fbi->var.xres;
		fbi->var.yres_virtual = fbi->var.yres;
		reg = inreg(disp, GC_L0EM);
		if (reg & GC_L0EM_L0EC_24) {
			fbi->var.bits_per_pixel = 32;
		} else {
			reg = inreg(disp, GC_L0M);
			if (reg & GC_L0M_L0C_16)
				fbi->var.bits_per_pixel = 16;
			else
				fbi->var.bits_per_pixel = 8;
		}
		reg = inreg(disp, GC_VSW_HSW_HSP);
		fbi->var.hsync_len = ((reg & 0xff0000) >> 16) + 1;
		fbi->var.vsync_len = ((reg & 0x3f000000) >> 24) + 1;
		hsp = (reg & 0xffff) + 1;
		ht = ((inreg(disp, GC_HTP) & 0xfff0000) >> 16) + 1;
		fbi->var.right_margin = hsp - fbi->var.xres;
		fbi->var.left_margin = ht - hsp - fbi->var.hsync_len;
		vt = ((inreg(disp, GC_VTR) & 0xfff0000) >> 16) + 1;
		fbi->var.lower_margin = vsp - fbi->var.yres;
		fbi->var.upper_margin = vt - vsp - fbi->var.vsync_len;
	} else if (mode) {
		dev_dbg(par->dev, "using supplied mode\n");
		fb_videomode_to_var(&fbi->var, (struct fb_videomode *)mode);
		fbi->var.bits_per_pixel = mode->def_bpp ? mode->def_bpp : 8;
	} else {
		int ret;

		ret = fb_find_mode(&fbi->var, fbi, "640x480-16@60",
				   NULL, 0, NULL, 16);
		if (ret == 0 || ret == 4) {
			dev_err(par->dev,
				"failed to get initial mode\n");
			return -EINVAL;
		}
	}

	fbi->var.xoffset = 0;
	fbi->var.yoffset = 0;
	fbi->var.grayscale = 0;
	fbi->var.nonstd = 0;
	fbi->var.height = -1;
	fbi->var.width = -1;
	fbi->var.accel_flags = 0;
	fbi->var.vmode = FB_VMODE_NONINTERLACED;
	fbi->var.activate = FB_ACTIVATE_NOW;
	fbi->flags = FBINFO_DEFAULT |
#ifdef __BIG_ENDIAN
		     FBINFO_FOREIGN_ENDIAN |
#endif
		     FBINFO_HWACCEL_XPAN |
		     FBINFO_HWACCEL_YPAN;

	/* check and possibly fix bpp */
	if ((fbi->fbops->fb_check_var)(&fbi->var, fbi))
		dev_err(par->dev, "check_var() failed on initial setup?\n");

	fbi->fix.visual = fbi->var.bits_per_pixel == 8 ?
			 FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	fbi->fix.line_length = (fbi->var.xres_virtual *
				fbi->var.bits_per_pixel) / 8;
	fbi->fix.smem_len = fbi->fix.line_length * fbi->var.yres_virtual;

	/*
	 * reserve space for capture buffers and two cursors
	 * at the end of vram: 720x576 * 2 * 2.2 + 64x64 * 16.
	 */
	par->cap_buf = par->mapped_vram - 0x1bd800 - 0x10000;
	par->cap_len = 0x1bd800;
	par->l1_cfg.sx = 0;
	par->l1_cfg.sy = 0;
	par->l1_cfg.sw = 720;
	par->l1_cfg.sh = 576;
	par->l1_cfg.dx = 0;
	par->l1_cfg.dy = 0;
	par->l1_cfg.dw = 720;
	par->l1_cfg.dh = 576;
	stride = par->l1_cfg.sw * (fbi->var.bits_per_pixel / 8);
	par->l1_stride = stride / 64 + ((stride % 64) ? 1 : 0);
	outreg(cap, GC_CAP_CBM, GC_CBM_OO | GC_CBM_CBST |
				(par->l1_stride << 16));
	outreg(cap, GC_CAP_CBOA, par->cap_buf);
	outreg(cap, GC_CAP_CBLA, par->cap_buf + par->cap_len);
	return 0;
}

/*
 * show some display controller and cursor registers
 */
static ssize_t dispregs_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct mb862xxfb_par *par = fbi->par;
	char *ptr = buf;
	unsigned int reg;

	for (reg = GC_DCM0; reg <= GC_L0DY_L0DX; reg += 4)
		ptr += sprintf(ptr, "%08x = %08x\n",
			       reg, inreg(disp, reg));

	for (reg = GC_CPM_CUTC; reg <= GC_CUY1_CUX1; reg += 4)
		ptr += sprintf(ptr, "%08x = %08x\n",
			       reg, inreg(disp, reg));

	for (reg = GC_DCM1; reg <= GC_L0WH_L0WW; reg += 4)
		ptr += sprintf(ptr, "%08x = %08x\n",
			       reg, inreg(disp, reg));

	for (reg = 0x400; reg <= 0x410; reg += 4)
		ptr += sprintf(ptr, "geo %08x = %08x\n",
			       reg, inreg(geo, reg));

	for (reg = 0x400; reg <= 0x410; reg += 4)
		ptr += sprintf(ptr, "draw %08x = %08x\n",
			       reg, inreg(draw, reg));

	for (reg = 0x440; reg <= 0x450; reg += 4)
		ptr += sprintf(ptr, "draw %08x = %08x\n",
			       reg, inreg(draw, reg));

	return ptr - buf;
}

static DEVICE_ATTR_RO(dispregs);

static irqreturn_t mb862xx_intr(int irq, void *dev_id)
{
	struct mb862xxfb_par *par = (struct mb862xxfb_par *) dev_id;
	unsigned long reg_ist, mask;

	if (!par)
		return IRQ_NONE;

	if (par->type == BT_CARMINE) {
		/* Get Interrupt Status */
		reg_ist = inreg(ctrl, GC_CTRL_STATUS);
		mask = inreg(ctrl, GC_CTRL_INT_MASK);
		if (reg_ist == 0)
			return IRQ_HANDLED;

		reg_ist &= mask;
		if (reg_ist == 0)
			return IRQ_HANDLED;

		/* Clear interrupt status */
		outreg(ctrl, 0x0, reg_ist);
	} else {
		/* Get status */
		reg_ist = inreg(host, GC_IST);
		mask = inreg(host, GC_IMASK);

		reg_ist &= mask;
		if (reg_ist == 0)
			return IRQ_HANDLED;

		/* Clear status */
		outreg(host, GC_IST, ~reg_ist);
	}
	return IRQ_HANDLED;
}

#if defined(CONFIG_FB_MB862XX_LIME)
/*
 * GDC (Lime, Coral(B/Q), Mint, ...) on host bus
 */
static int mb862xx_gdc_init(struct mb862xxfb_par *par)
{
	unsigned long ccf, mmr;
	unsigned long ver, rev;

	if (!par)
		return -ENODEV;

#if defined(CONFIG_FB_PRE_INIT_FB)
	par->pre_init = 1;
#endif
	par->host = par->mmio_base;
	par->i2c = par->mmio_base + MB862XX_I2C_BASE;
	par->disp = par->mmio_base + MB862XX_DISP_BASE;
	par->cap = par->mmio_base + MB862XX_CAP_BASE;
	par->draw = par->mmio_base + MB862XX_DRAW_BASE;
	par->geo = par->mmio_base + MB862XX_GEO_BASE;
	par->pio = par->mmio_base + MB862XX_PIO_BASE;

	par->refclk = GC_DISP_REFCLK_400;

	ver = inreg(host, GC_CID);
	rev = inreg(pio, GC_REVISION);
	if ((ver == 0x303) && (rev & 0xffffff00) == 0x20050100) {
		dev_info(par->dev, "Fujitsu Lime v1.%d found\n",
			 (int)rev & 0xff);
		par->type = BT_LIME;
		ccf = par->gc_mode ? par->gc_mode->ccf : GC_CCF_COT_100;
		mmr = par->gc_mode ? par->gc_mode->mmr : 0x414fb7f2;
	} else {
		dev_info(par->dev, "? GDC, CID/Rev.: 0x%lx/0x%lx \n", ver, rev);
		return -ENODEV;
	}

	if (!par->pre_init) {
		outreg(host, GC_CCF, ccf);
		udelay(200);
		outreg(host, GC_MMR, mmr);
		udelay(10);
	}

	/* interrupt status */
	outreg(host, GC_IST, 0);
	outreg(host, GC_IMASK, GC_INT_EN);
	return 0;
}

static int of_platform_mb862xx_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct device *dev = &ofdev->dev;
	struct mb862xxfb_par *par;
	struct fb_info *info;
	struct resource res;
	resource_size_t res_size;
	unsigned long ret = -ENODEV;

	if (of_address_to_resource(np, 0, &res)) {
		dev_err(dev, "Invalid address\n");
		return -ENXIO;
	}

	info = framebuffer_alloc(sizeof(struct mb862xxfb_par), dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->info = info;
	par->dev = dev;

	par->irq = irq_of_parse_and_map(np, 0);
	if (par->irq == NO_IRQ) {
		dev_err(dev, "failed to map irq\n");
		ret = -ENODEV;
		goto fbrel;
	}

	res_size = resource_size(&res);
	par->res = request_mem_region(res.start, res_size, DRV_NAME);
	if (par->res == NULL) {
		dev_err(dev, "Cannot claim framebuffer/mmio\n");
		ret = -ENXIO;
		goto irqdisp;
	}

#if defined(CONFIG_SOCRATES)
	par->gc_mode = &socrates_gc_mode;
#endif

	par->fb_base_phys = res.start;
	par->mmio_base_phys = res.start + MB862XX_MMIO_BASE;
	par->mmio_len = MB862XX_MMIO_SIZE;
	if (par->gc_mode)
		par->mapped_vram = par->gc_mode->max_vram;
	else
		par->mapped_vram = MB862XX_MEM_SIZE;

	par->fb_base = ioremap(par->fb_base_phys, par->mapped_vram);
	if (par->fb_base == NULL) {
		dev_err(dev, "Cannot map framebuffer\n");
		goto rel_reg;
	}

	par->mmio_base = ioremap(par->mmio_base_phys, par->mmio_len);
	if (par->mmio_base == NULL) {
		dev_err(dev, "Cannot map registers\n");
		goto fb_unmap;
	}

	dev_dbg(dev, "fb phys 0x%llx 0x%lx\n",
		(u64)par->fb_base_phys, (ulong)par->mapped_vram);
	dev_dbg(dev, "mmio phys 0x%llx 0x%lx, (irq = %d)\n",
		(u64)par->mmio_base_phys, (ulong)par->mmio_len, par->irq);

	if (mb862xx_gdc_init(par))
		goto io_unmap;

	if (request_irq(par->irq, mb862xx_intr, 0,
			DRV_NAME, (void *)par)) {
		dev_err(dev, "Cannot request irq\n");
		goto io_unmap;
	}

	mb862xxfb_init_fbinfo(info);

	if (fb_alloc_cmap(&info->cmap, NR_PALETTE, 0) < 0) {
		dev_err(dev, "Could not allocate cmap for fb_info.\n");
		goto free_irq;
	}

	if ((info->fbops->fb_set_par)(info))
		dev_err(dev, "set_var() failed on initial setup?\n");

	if (register_framebuffer(info)) {
		dev_err(dev, "failed to register framebuffer\n");
		goto rel_cmap;
	}

	dev_set_drvdata(dev, info);

	if (device_create_file(dev, &dev_attr_dispregs))
		dev_err(dev, "Can't create sysfs regdump file\n");
	return 0;

rel_cmap:
	fb_dealloc_cmap(&info->cmap);
free_irq:
	outreg(host, GC_IMASK, 0);
	free_irq(par->irq, (void *)par);
io_unmap:
	iounmap(par->mmio_base);
fb_unmap:
	iounmap(par->fb_base);
rel_reg:
	release_mem_region(res.start, res_size);
irqdisp:
	irq_dispose_mapping(par->irq);
fbrel:
	framebuffer_release(info);
	return ret;
}

static int of_platform_mb862xx_remove(struct platform_device *ofdev)
{
	struct fb_info *fbi = dev_get_drvdata(&ofdev->dev);
	struct mb862xxfb_par *par = fbi->par;
	resource_size_t res_size = resource_size(par->res);
	unsigned long reg;

	dev_dbg(fbi->dev, "%s release\n", fbi->fix.id);

	/* display off */
	reg = inreg(disp, GC_DCM1);
	reg &= ~(GC_DCM01_DEN | GC_DCM01_L0E);
	outreg(disp, GC_DCM1, reg);

	/* disable interrupts */
	outreg(host, GC_IMASK, 0);

	free_irq(par->irq, (void *)par);
	irq_dispose_mapping(par->irq);

	device_remove_file(&ofdev->dev, &dev_attr_dispregs);

	unregister_framebuffer(fbi);
	fb_dealloc_cmap(&fbi->cmap);

	iounmap(par->mmio_base);
	iounmap(par->fb_base);

	release_mem_region(par->res->start, res_size);
	framebuffer_release(fbi);
	return 0;
}

/*
 * common types
 */
static struct of_device_id of_platform_mb862xx_tbl[] = {
	{ .compatible = "fujitsu,MB86276", },
	{ .compatible = "fujitsu,lime", },
	{ .compatible = "fujitsu,MB86277", },
	{ .compatible = "fujitsu,mint", },
	{ .compatible = "fujitsu,MB86293", },
	{ .compatible = "fujitsu,MB86294", },
	{ .compatible = "fujitsu,coral", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_platform_mb862xx_tbl);

static struct platform_driver of_platform_mb862xxfb_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_platform_mb862xx_tbl,
	},
	.probe		= of_platform_mb862xx_probe,
	.remove		= of_platform_mb862xx_remove,
};
#endif

#if defined(CONFIG_FB_MB862XX_PCI_GDC)
static int coralp_init(struct mb862xxfb_par *par)
{
	int cn, ver;

	par->host = par->mmio_base;
	par->i2c = par->mmio_base + MB862XX_I2C_BASE;
	par->disp = par->mmio_base + MB862XX_DISP_BASE;
	par->cap = par->mmio_base + MB862XX_CAP_BASE;
	par->draw = par->mmio_base + MB862XX_DRAW_BASE;
	par->geo = par->mmio_base + MB862XX_GEO_BASE;
	par->pio = par->mmio_base + MB862XX_PIO_BASE;

	par->refclk = GC_DISP_REFCLK_400;

	if (par->mapped_vram >= 0x2000000) {
		/* relocate gdc registers space */
		writel(1, par->fb_base + MB862XX_MMIO_BASE + GC_RSW);
		udelay(1); /* wait at least 20 bus cycles */
	}

	ver = inreg(host, GC_CID);
	cn = (ver & GC_CID_CNAME_MSK) >> 8;
	ver = ver & GC_CID_VERSION_MSK;
	if (cn == 3) {
		unsigned long reg;

		dev_info(par->dev, "Fujitsu Coral-%s GDC Rev.%d found\n",\
			 (ver == 6) ? "P" : (ver == 8) ? "PA" : "?",
			 par->pdev->revision);
		reg = inreg(disp, GC_DCM1);
		if (reg & GC_DCM01_DEN && reg & GC_DCM01_L0E)
			par->pre_init = 1;

		if (!par->pre_init) {
			outreg(host, GC_CCF, GC_CCF_CGE_166 | GC_CCF_COT_133);
			udelay(200);
			outreg(host, GC_MMR, GC_MMR_CORALP_EVB_VAL);
			udelay(10);
		}
		/* Clear interrupt status */
		outreg(host, GC_IST, 0);
	} else {
		return -ENODEV;
	}

	mb862xx_i2c_init(par);
	return 0;
}

static int init_dram_ctrl(struct mb862xxfb_par *par)
{
	unsigned long i = 0;

	/*
	 * Set io mode first! Spec. says IC may be destroyed
	 * if not set to SSTL2/LVCMOS before init.
	 */
	outreg(dram_ctrl, GC_DCTL_IOCONT1_IOCONT0, GC_EVB_DCTL_IOCONT1_IOCONT0);

	/* DRAM init */
	outreg(dram_ctrl, GC_DCTL_MODE_ADD, GC_EVB_DCTL_MODE_ADD);
	outreg(dram_ctrl, GC_DCTL_SETTIME1_EMODE, GC_EVB_DCTL_SETTIME1_EMODE);
	outreg(dram_ctrl, GC_DCTL_REFRESH_SETTIME2,
	       GC_EVB_DCTL_REFRESH_SETTIME2);
	outreg(dram_ctrl, GC_DCTL_RSV2_RSV1, GC_EVB_DCTL_RSV2_RSV1);
	outreg(dram_ctrl, GC_DCTL_DDRIF2_DDRIF1, GC_EVB_DCTL_DDRIF2_DDRIF1);
	outreg(dram_ctrl, GC_DCTL_RSV0_STATES, GC_EVB_DCTL_RSV0_STATES);

	/* DLL reset done? */
	while ((inreg(dram_ctrl, GC_DCTL_RSV0_STATES) & GC_DCTL_STATES_MSK)) {
		udelay(GC_DCTL_INIT_WAIT_INTERVAL);
		if (i++ > GC_DCTL_INIT_WAIT_CNT) {
			dev_err(par->dev, "VRAM init failed.\n");
			return -EINVAL;
		}
	}
	outreg(dram_ctrl, GC_DCTL_MODE_ADD, GC_EVB_DCTL_MODE_ADD_AFT_RST);
	outreg(dram_ctrl, GC_DCTL_RSV0_STATES, GC_EVB_DCTL_RSV0_STATES_AFT_RST);
	return 0;
}

static int carmine_init(struct mb862xxfb_par *par)
{
	unsigned long reg;

	par->ctrl = par->mmio_base + MB86297_CTRL_BASE;
	par->i2c = par->mmio_base + MB86297_I2C_BASE;
	par->disp = par->mmio_base + MB86297_DISP0_BASE;
	par->disp1 = par->mmio_base + MB86297_DISP1_BASE;
	par->cap = par->mmio_base + MB86297_CAP0_BASE;
	par->cap1 = par->mmio_base + MB86297_CAP1_BASE;
	par->draw = par->mmio_base + MB86297_DRAW_BASE;
	par->dram_ctrl = par->mmio_base + MB86297_DRAMCTRL_BASE;
	par->wrback = par->mmio_base + MB86297_WRBACK_BASE;

	par->refclk = GC_DISP_REFCLK_533;

	/* warm up */
	reg = GC_CTRL_CLK_EN_DRAM | GC_CTRL_CLK_EN_2D3D | GC_CTRL_CLK_EN_DISP0;
	outreg(ctrl, GC_CTRL_CLK_ENABLE, reg);

	/* check for engine module revision */
	if (inreg(draw, GC_2D3D_REV) == GC_RE_REVISION)
		dev_info(par->dev, "Fujitsu Carmine GDC Rev.%d found\n",
			 par->pdev->revision);
	else
		goto err_init;

	reg &= ~GC_CTRL_CLK_EN_2D3D;
	outreg(ctrl, GC_CTRL_CLK_ENABLE, reg);

	/* set up vram */
	if (init_dram_ctrl(par) < 0)
		goto err_init;

	outreg(ctrl, GC_CTRL_INT_MASK, 0);
	return 0;

err_init:
	outreg(ctrl, GC_CTRL_CLK_ENABLE, 0);
	return -EINVAL;
}

static inline int mb862xx_pci_gdc_init(struct mb862xxfb_par *par)
{
	switch (par->type) {
	case BT_CORALP:
		return coralp_init(par);
	case BT_CARMINE:
		return carmine_init(par);
	default:
		return -ENODEV;
	}
}

#define CHIP_ID(id)	\
	{ PCI_DEVICE(PCI_VENDOR_ID_FUJITSU_LIMITED, id) }

static const struct pci_device_id mb862xx_pci_tbl[] = {
	/* MB86295/MB86296 */
	CHIP_ID(PCI_DEVICE_ID_FUJITSU_CORALP),
	CHIP_ID(PCI_DEVICE_ID_FUJITSU_CORALPA),
	/* MB86297 */
	CHIP_ID(PCI_DEVICE_ID_FUJITSU_CARMINE),
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, mb862xx_pci_tbl);

static int mb862xx_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	struct mb862xxfb_par *par;
	struct fb_info *info;
	struct device *dev = &pdev->dev;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(dev, "Cannot enable PCI device\n");
		goto out;
	}

	info = framebuffer_alloc(sizeof(struct mb862xxfb_par), dev);
	if (!info) {
		ret = -ENOMEM;
		goto dis_dev;
	}

	par = info->par;
	par->info = info;
	par->dev = dev;
	par->pdev = pdev;
	par->irq = pdev->irq;

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret < 0) {
		dev_err(dev, "Cannot reserve region(s) for PCI device\n");
		goto rel_fb;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_FUJITSU_CORALP:
	case PCI_DEVICE_ID_FUJITSU_CORALPA:
		par->fb_base_phys = pci_resource_start(par->pdev, 0);
		par->mapped_vram = CORALP_MEM_SIZE;
		if (par->mapped_vram >= 0x2000000) {
			par->mmio_base_phys = par->fb_base_phys +
					      MB862XX_MMIO_HIGH_BASE;
		} else {
			par->mmio_base_phys = par->fb_base_phys +
					      MB862XX_MMIO_BASE;
		}
		par->mmio_len = MB862XX_MMIO_SIZE;
		par->type = BT_CORALP;
		break;
	case PCI_DEVICE_ID_FUJITSU_CARMINE:
		par->fb_base_phys = pci_resource_start(par->pdev, 2);
		par->mmio_base_phys = pci_resource_start(par->pdev, 3);
		par->mmio_len = pci_resource_len(par->pdev, 3);
		par->mapped_vram = CARMINE_MEM_SIZE;
		par->type = BT_CARMINE;
		break;
	default:
		/* should never occur */
		ret = -EIO;
		goto rel_reg;
	}

	par->fb_base = ioremap(par->fb_base_phys, par->mapped_vram);
	if (par->fb_base == NULL) {
		dev_err(dev, "Cannot map framebuffer\n");
		ret = -EIO;
		goto rel_reg;
	}

	par->mmio_base = ioremap(par->mmio_base_phys, par->mmio_len);
	if (par->mmio_base == NULL) {
		dev_err(dev, "Cannot map registers\n");
		ret = -EIO;
		goto fb_unmap;
	}

	dev_dbg(dev, "fb phys 0x%llx 0x%lx\n",
		(unsigned long long)par->fb_base_phys, (ulong)par->mapped_vram);
	dev_dbg(dev, "mmio phys 0x%llx 0x%lx\n",
		(unsigned long long)par->mmio_base_phys, (ulong)par->mmio_len);

	ret = mb862xx_pci_gdc_init(par);
	if (ret)
		goto io_unmap;

	ret = request_irq(par->irq, mb862xx_intr, IRQF_SHARED,
			  DRV_NAME, (void *)par);
	if (ret) {
		dev_err(dev, "Cannot request irq\n");
		goto io_unmap;
	}

	mb862xxfb_init_fbinfo(info);

	if (fb_alloc_cmap(&info->cmap, NR_PALETTE, 0) < 0) {
		dev_err(dev, "Could not allocate cmap for fb_info.\n");
		ret = -ENOMEM;
		goto free_irq;
	}

	if ((info->fbops->fb_set_par)(info))
		dev_err(dev, "set_var() failed on initial setup?\n");

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(dev, "failed to register framebuffer\n");
		goto rel_cmap;
	}

	pci_set_drvdata(pdev, info);

	if (device_create_file(dev, &dev_attr_dispregs))
		dev_err(dev, "Can't create sysfs regdump file\n");

	if (par->type == BT_CARMINE)
		outreg(ctrl, GC_CTRL_INT_MASK, GC_CARMINE_INT_EN);
	else
		outreg(host, GC_IMASK, GC_INT_EN);

	return 0;

rel_cmap:
	fb_dealloc_cmap(&info->cmap);
free_irq:
	free_irq(par->irq, (void *)par);
io_unmap:
	iounmap(par->mmio_base);
fb_unmap:
	iounmap(par->fb_base);
rel_reg:
	pci_release_regions(pdev);
rel_fb:
	framebuffer_release(info);
dis_dev:
	pci_disable_device(pdev);
out:
	return ret;
}

static void mb862xx_pci_remove(struct pci_dev *pdev)
{
	struct fb_info *fbi = pci_get_drvdata(pdev);
	struct mb862xxfb_par *par = fbi->par;
	unsigned long reg;

	dev_dbg(fbi->dev, "%s release\n", fbi->fix.id);

	/* display off */
	reg = inreg(disp, GC_DCM1);
	reg &= ~(GC_DCM01_DEN | GC_DCM01_L0E);
	outreg(disp, GC_DCM1, reg);

	if (par->type == BT_CARMINE) {
		outreg(ctrl, GC_CTRL_INT_MASK, 0);
		outreg(ctrl, GC_CTRL_CLK_ENABLE, 0);
	} else {
		outreg(host, GC_IMASK, 0);
	}

	mb862xx_i2c_exit(par);

	device_remove_file(&pdev->dev, &dev_attr_dispregs);

	unregister_framebuffer(fbi);
	fb_dealloc_cmap(&fbi->cmap);

	free_irq(par->irq, (void *)par);
	iounmap(par->mmio_base);
	iounmap(par->fb_base);

	pci_release_regions(pdev);
	framebuffer_release(fbi);
	pci_disable_device(pdev);
}

static struct pci_driver mb862xxfb_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= mb862xx_pci_tbl,
	.probe		= mb862xx_pci_probe,
	.remove		= mb862xx_pci_remove,
};
#endif

static int mb862xxfb_init(void)
{
	int ret = -ENODEV;

#if defined(CONFIG_FB_MB862XX_LIME)
	ret = platform_driver_register(&of_platform_mb862xxfb_driver);
#endif
#if defined(CONFIG_FB_MB862XX_PCI_GDC)
	ret = pci_register_driver(&mb862xxfb_pci_driver);
#endif
	return ret;
}

static void __exit mb862xxfb_exit(void)
{
#if defined(CONFIG_FB_MB862XX_LIME)
	platform_driver_unregister(&of_platform_mb862xxfb_driver);
#endif
#if defined(CONFIG_FB_MB862XX_PCI_GDC)
	pci_unregister_driver(&mb862xxfb_pci_driver);
#endif
}

module_init(mb862xxfb_init);
module_exit(mb862xxfb_exit);

MODULE_DESCRIPTION("Fujitsu MB862xx Framebuffer driver");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_LICENSE("GPL v2");
