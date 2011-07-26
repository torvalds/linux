/*
 *  platinumfb.c -- frame buffer device for the PowerMac 'platinum' display
 *
 *  Copyright (C) 1998 Franz Sirl
 *
 *  Frame buffer structure from:
 *    drivers/video/controlfb.c -- frame buffer device for
 *    Apple 'control' display chip.
 *    Copyright (C) 1998 Dan Jacobowitz
 *
 *  Hardware information from:
 *    platinum.c: Console support for PowerMac "platinum" display adaptor.
 *    Copyright (C) 1996 Paul Mackerras and Mark Abene
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/nvram.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pgtable.h>

#include "macmodes.h"
#include "platinumfb.h"

static int default_vmode = VMODE_NVRAM;
static int default_cmode = CMODE_NVRAM;

struct fb_info_platinum {
	struct fb_info			*info;

	int				vmode, cmode;
	int				xres, yres;
	int				vxres, vyres;
	int				xoffset, yoffset;

	struct {
		__u8 red, green, blue;
	}				palette[256];
	u32				pseudo_palette[16];
	
	volatile struct cmap_regs	__iomem *cmap_regs;
	unsigned long			cmap_regs_phys;
	
	volatile struct platinum_regs	__iomem *platinum_regs;
	unsigned long			platinum_regs_phys;
	
	__u8				__iomem *frame_buffer;
	volatile __u8			__iomem *base_frame_buffer;
	unsigned long			frame_buffer_phys;
	
	unsigned long			total_vram;
	int				clktype;
	int				dactype;

	struct resource			rsrc_fb, rsrc_reg;
};

/*
 * Frame buffer device API
 */

static int platinumfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
	u_int transp, struct fb_info *info);
static int platinumfb_blank(int blank_mode, struct fb_info *info);
static int platinumfb_set_par (struct fb_info *info);
static int platinumfb_check_var (struct fb_var_screeninfo *var, struct fb_info *info);

/*
 * internal functions
 */

static inline int platinum_vram_reqd(int video_mode, int color_mode);
static int read_platinum_sense(struct fb_info_platinum *pinfo);
static void set_platinum_clock(struct fb_info_platinum *pinfo);
static void platinum_set_hardware(struct fb_info_platinum *pinfo);
static int platinum_var_to_par(struct fb_var_screeninfo *var,
			       struct fb_info_platinum *pinfo,
			       int check_only);

/*
 * Interface used by the world
 */

static struct fb_ops platinumfb_ops = {
	.owner =	THIS_MODULE,
	.fb_check_var	= platinumfb_check_var,
	.fb_set_par	= platinumfb_set_par,
	.fb_setcolreg	= platinumfb_setcolreg,
	.fb_blank	= platinumfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/*
 * Checks a var structure
 */
static int platinumfb_check_var (struct fb_var_screeninfo *var, struct fb_info *info)
{
	return platinum_var_to_par(var, info->par, 1);
}

/*
 * Applies current var to display
 */
static int platinumfb_set_par (struct fb_info *info)
{
	struct fb_info_platinum *pinfo = info->par;
	struct platinum_regvals *init;
	int err, offset = 0x20;

	if((err = platinum_var_to_par(&info->var, pinfo, 0))) {
		printk (KERN_ERR "platinumfb_set_par: error calling"
				 " platinum_var_to_par: %d.\n", err);
		return err;
	}

	platinum_set_hardware(pinfo);

	init = platinum_reg_init[pinfo->vmode-1];
	
 	if ((pinfo->vmode == VMODE_832_624_75) && (pinfo->cmode > CMODE_8))
  		offset = 0x10;

	info->screen_base = pinfo->frame_buffer + init->fb_offset + offset;
	mutex_lock(&info->mm_lock);
	info->fix.smem_start = (pinfo->frame_buffer_phys) + init->fb_offset + offset;
	mutex_unlock(&info->mm_lock);
	info->fix.visual = (pinfo->cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
 	info->fix.line_length = vmode_attrs[pinfo->vmode-1].hres * (1<<pinfo->cmode)
		+ offset;
	printk("line_length: %x\n", info->fix.line_length);
	return 0;
}

static int platinumfb_blank(int blank,  struct fb_info *fb)
{
/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
/* [danj] I think there's something fishy about those constants... */
/*
	struct fb_info_platinum *info = (struct fb_info_platinum *) fb;
	int	ctrl;

	ctrl = ld_le32(&info->platinum_regs->ctrl.r) | 0x33;
	if (blank)
		--blank_mode;
	if (blank & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&info->platinum_regs->ctrl.r, ctrl);
*/
/* TODO: Figure out how the heck to powerdown this thing! */
	return 0;
}

static int platinumfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	struct fb_info_platinum *pinfo = info->par;
	volatile struct cmap_regs __iomem *cmap_regs = pinfo->cmap_regs;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	pinfo->palette[regno].red = red;
	pinfo->palette[regno].green = green;
	pinfo->palette[regno].blue = blue;

	out_8(&cmap_regs->addr, regno);		/* tell clut what addr to fill	*/
	out_8(&cmap_regs->lut, red);		/* send one color channel at	*/
	out_8(&cmap_regs->lut, green);		/* a time...			*/
	out_8(&cmap_regs->lut, blue);

	if (regno < 16) {
		int i;
		u32 *pal = info->pseudo_palette;
		switch (pinfo->cmode) {
		case CMODE_16:
			pal[regno] = (regno << 10) | (regno << 5) | regno;
			break;
		case CMODE_32:
			i = (regno << 8) | regno;
			pal[regno] = (i << 16) | i;
			break;
		}
	}
	
	return 0;
}

static inline int platinum_vram_reqd(int video_mode, int color_mode)
{
	int baseval = vmode_attrs[video_mode-1].hres * (1<<color_mode);

	if ((video_mode == VMODE_832_624_75) && (color_mode > CMODE_8))
		baseval += 0x10;
	else
		baseval += 0x20;

	return vmode_attrs[video_mode-1].vres * baseval + 0x1000;
}

#define STORE_D2(a, d) { \
	out_8(&cmap_regs->addr, (a+32)); \
	out_8(&cmap_regs->d2, (d)); \
}

static void set_platinum_clock(struct fb_info_platinum *pinfo)
{
	volatile struct cmap_regs __iomem *cmap_regs = pinfo->cmap_regs;
	struct platinum_regvals	*init;

	init = platinum_reg_init[pinfo->vmode-1];

	STORE_D2(6, 0xc6);
	out_8(&cmap_regs->addr,3+32);

	if (in_8(&cmap_regs->d2) == 2) {
		STORE_D2(7, init->clock_params[pinfo->clktype][0]);
		STORE_D2(8, init->clock_params[pinfo->clktype][1]);
		STORE_D2(3, 3);
	} else {
		STORE_D2(4, init->clock_params[pinfo->clktype][0]);
		STORE_D2(5, init->clock_params[pinfo->clktype][1]);
		STORE_D2(3, 2);
	}

	__delay(5000);
	STORE_D2(9, 0xa6);
}


/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void platinum_set_hardware(struct fb_info_platinum *pinfo)
{
	volatile struct platinum_regs	__iomem *platinum_regs = pinfo->platinum_regs;
	volatile struct cmap_regs	__iomem *cmap_regs = pinfo->cmap_regs;
	struct platinum_regvals		*init;
	int				i;
	int				vmode, cmode;
	
	vmode = pinfo->vmode;
	cmode = pinfo->cmode;

	init = platinum_reg_init[vmode - 1];

	/* Initialize display timing registers */
	out_be32(&platinum_regs->reg[24].r, 7);	/* turn display off */

	for (i = 0; i < 26; ++i)
		out_be32(&platinum_regs->reg[i+32].r, init->regs[i]);

	out_be32(&platinum_regs->reg[26+32].r, (pinfo->total_vram == 0x100000 ?
						init->offset[cmode] + 4 - cmode :
						init->offset[cmode]));
	out_be32(&platinum_regs->reg[16].r, (unsigned) pinfo->frame_buffer_phys+init->fb_offset+0x10);
	out_be32(&platinum_regs->reg[18].r, init->pitch[cmode]);
	out_be32(&platinum_regs->reg[19].r, (pinfo->total_vram == 0x100000 ?
					     init->mode[cmode+1] :
					     init->mode[cmode]));
	out_be32(&platinum_regs->reg[20].r, (pinfo->total_vram == 0x100000 ? 0x11 : 0x1011));
	out_be32(&platinum_regs->reg[21].r, 0x100);
	out_be32(&platinum_regs->reg[22].r, 1);
	out_be32(&platinum_regs->reg[23].r, 1);
	out_be32(&platinum_regs->reg[26].r, 0xc00);
	out_be32(&platinum_regs->reg[27].r, 0x235);
	/* out_be32(&platinum_regs->reg[27].r, 0x2aa); */

	STORE_D2(0, (pinfo->total_vram == 0x100000 ?
		     init->dacula_ctrl[cmode] & 0xf :
		     init->dacula_ctrl[cmode]));
	STORE_D2(1, 4);
	STORE_D2(2, 0);

	set_platinum_clock(pinfo);

	out_be32(&platinum_regs->reg[24].r, 0);	/* turn display on */
}

/*
 * Set misc info vars for this driver
 */
static void __devinit platinum_init_info(struct fb_info *info, struct fb_info_platinum *pinfo)
{
	/* Fill fb_info */
	info->fbops = &platinumfb_ops;
	info->pseudo_palette = pinfo->pseudo_palette;
        info->flags = FBINFO_DEFAULT;
	info->screen_base = pinfo->frame_buffer + 0x20;

	fb_alloc_cmap(&info->cmap, 256, 0);

	/* Fill fix common fields */
	strcpy(info->fix.id, "platinum");
	info->fix.mmio_start = pinfo->platinum_regs_phys;
	info->fix.mmio_len = 0x1000;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.smem_start = pinfo->frame_buffer_phys + 0x20; /* will be updated later */
	info->fix.smem_len = pinfo->total_vram - 0x20;
        info->fix.ywrapstep = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
        info->fix.type_aux = 0;
        info->fix.accel = FB_ACCEL_NONE;
}


static int __devinit platinum_init_fb(struct fb_info *info)
{
	struct fb_info_platinum *pinfo = info->par;
	struct fb_var_screeninfo var;
	int sense, rc;

	sense = read_platinum_sense(pinfo);
	printk(KERN_INFO "platinumfb: Monitor sense value = 0x%x, ", sense);
	if (default_vmode == VMODE_NVRAM) {
#ifdef CONFIG_NVRAM
		default_vmode = nvram_read_byte(NV_VMODE);
		if (default_vmode <= 0 || default_vmode > VMODE_MAX ||
		    !platinum_reg_init[default_vmode-1])
#endif
			default_vmode = VMODE_CHOOSE;
	}
	if (default_vmode == VMODE_CHOOSE) {
		default_vmode = mac_map_monitor_sense(sense);
	}
	if (default_vmode <= 0 || default_vmode > VMODE_MAX)
		default_vmode = VMODE_640_480_60;
#ifdef CONFIG_NVRAM
	if (default_cmode == CMODE_NVRAM)
		default_cmode = nvram_read_byte(NV_CMODE);
#endif
	if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
		default_cmode = CMODE_8;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while(default_cmode > CMODE_8 &&
	      platinum_vram_reqd(default_vmode, default_cmode) > pinfo->total_vram)
		default_cmode--;

	printk("platinumfb:  Using video mode %d and color mode %d.\n", default_vmode, default_cmode);

	/* Setup default var */
	if (mac_vmode_to_var(default_vmode, default_cmode, &var) < 0) {
		/* This shouldn't happen! */
		printk("mac_vmode_to_var(%d, %d,) failed\n", default_vmode, default_cmode);
try_again:
		default_vmode = VMODE_640_480_60;
		default_cmode = CMODE_8;
		if (mac_vmode_to_var(default_vmode, default_cmode, &var) < 0) {
			printk(KERN_ERR "platinumfb: mac_vmode_to_var() failed\n");
			return -ENXIO;
		}
	}

	/* Initialize info structure */
	platinum_init_info(info, pinfo);

	/* Apply default var */
	info->var = var;
	var.activate = FB_ACTIVATE_NOW;
	rc = fb_set_var(info, &var);
	if (rc && (default_vmode != VMODE_640_480_60 || default_cmode != CMODE_8))
		goto try_again;

	/* Register with fbdev layer */
	rc = register_framebuffer(info);
	if (rc < 0)
		return rc;

	printk(KERN_INFO "fb%d: Apple Platinum frame buffer device\n", info->node);

	return 0;
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int read_platinum_sense(struct fb_info_platinum *info)
{
	volatile struct platinum_regs __iomem *platinum_regs = info->platinum_regs;
	int sense;

	out_be32(&platinum_regs->reg[23].r, 7);	/* turn off drivers */
	__delay(2000);
	sense = (~in_be32(&platinum_regs->reg[23].r) & 7) << 8;

	/* drive each sense line low in turn and collect the other 2 */
	out_be32(&platinum_regs->reg[23].r, 3);	/* drive A low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 3) << 4;
	out_be32(&platinum_regs->reg[23].r, 5);	/* drive B low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 4) << 1;
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 1) << 2;
	out_be32(&platinum_regs->reg[23].r, 6);	/* drive C low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 6) >> 1;

	out_be32(&platinum_regs->reg[23].r, 7);	/* turn off drivers */

	return sense;
}

/*
 * This routine takes a user-supplied var, and picks the best vmode/cmode from it.
 * It also updates the var structure to the actual mode data obtained
 */
static int platinum_var_to_par(struct fb_var_screeninfo *var, 
			       struct fb_info_platinum *pinfo,
			       int check_only)
{
	int vmode, cmode;

	if (mac_var_to_vmode(var, &vmode, &cmode) != 0) {
		printk(KERN_ERR "platinum_var_to_par: mac_var_to_vmode unsuccessful.\n");
		printk(KERN_ERR "platinum_var_to_par: var->xres = %d\n", var->xres);
		printk(KERN_ERR "platinum_var_to_par: var->yres = %d\n", var->yres);
		printk(KERN_ERR "platinum_var_to_par: var->xres_virtual = %d\n", var->xres_virtual);
		printk(KERN_ERR "platinum_var_to_par: var->yres_virtual = %d\n", var->yres_virtual);
		printk(KERN_ERR "platinum_var_to_par: var->bits_per_pixel = %d\n", var->bits_per_pixel);
		printk(KERN_ERR "platinum_var_to_par: var->pixclock = %d\n", var->pixclock);
		printk(KERN_ERR "platinum_var_to_par: var->vmode = %d\n", var->vmode);
		return -EINVAL;
	}

	if (!platinum_reg_init[vmode-1]) {
		printk(KERN_ERR "platinum_var_to_par, vmode %d not valid.\n", vmode);
		return -EINVAL;
	}

	if (platinum_vram_reqd(vmode, cmode) > pinfo->total_vram) {
		printk(KERN_ERR "platinum_var_to_par, not enough ram for vmode %d, cmode %d.\n", vmode, cmode);
		return -EINVAL;
	}

	if (mac_vmode_to_var(vmode, cmode, var))
		return -EINVAL;

	if (check_only)
		return 0;

	pinfo->vmode = vmode;
	pinfo->cmode = cmode;
	pinfo->xres = vmode_attrs[vmode-1].hres;
	pinfo->yres = vmode_attrs[vmode-1].vres;
	pinfo->xoffset = 0;
	pinfo->yoffset = 0;
	pinfo->vxres = pinfo->xres;
	pinfo->vyres = pinfo->yres;
	
	return 0;
}


/* 
 * Parse user speficied options (`video=platinumfb:')
 */
static int __init platinumfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "vmode:", 6)) {
	    		int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			int depth = simple_strtoul(this_opt+6, NULL, 0);
			switch (depth) {
			 case 0:
			 case 8:
			    default_cmode = CMODE_8;
			    break;
			 case 15:
			 case 16:
			    default_cmode = CMODE_16;
			    break;
			 case 24:
			 case 32:
			    default_cmode = CMODE_32;
			    break;
			}
		}
	}
	return 0;
}

#ifdef __powerpc__
#define invalidate_cache(addr) \
	asm volatile("eieio; dcbf 0,%1" \
	: "=m" (*(addr)) : "r" (addr) : "memory");
#else
#define invalidate_cache(addr)
#endif

static int __devinit platinumfb_probe(struct platform_device* odev)
{
	struct device_node	*dp = odev->dev.of_node;
	struct fb_info		*info;
	struct fb_info_platinum	*pinfo;
	volatile __u8		*fbuffer;
	int			bank0, bank1, bank2, bank3, rc;

	dev_info(&odev->dev, "Found Apple Platinum video hardware\n");

	info = framebuffer_alloc(sizeof(*pinfo), &odev->dev);
	if (info == NULL) {
		dev_err(&odev->dev, "Failed to allocate fbdev !\n");
		return -ENOMEM;
	}
	pinfo = info->par;

	if (of_address_to_resource(dp, 0, &pinfo->rsrc_reg) ||
	    of_address_to_resource(dp, 1, &pinfo->rsrc_fb)) {
		dev_err(&odev->dev, "Can't get resources\n");
		framebuffer_release(info);
		return -ENXIO;
	}
	dev_dbg(&odev->dev, " registers  : 0x%llx...0x%llx\n",
		(unsigned long long)pinfo->rsrc_reg.start,
		(unsigned long long)pinfo->rsrc_reg.end);
	dev_dbg(&odev->dev, " framebuffer: 0x%llx...0x%llx\n",
		(unsigned long long)pinfo->rsrc_fb.start,
		(unsigned long long)pinfo->rsrc_fb.end);

	/* Do not try to request register space, they overlap with the
	 * northbridge and that can fail. Only request framebuffer
	 */
	if (!request_mem_region(pinfo->rsrc_fb.start,
				resource_size(&pinfo->rsrc_fb),
				"platinumfb framebuffer")) {
		printk(KERN_ERR "platinumfb: Can't request framebuffer !\n");
		framebuffer_release(info);
		return -ENXIO;
	}

	/* frame buffer - map only 4MB */
	pinfo->frame_buffer_phys = pinfo->rsrc_fb.start;
	pinfo->frame_buffer = __ioremap(pinfo->rsrc_fb.start, 0x400000,
					_PAGE_WRITETHRU);
	pinfo->base_frame_buffer = pinfo->frame_buffer;

	/* registers */
	pinfo->platinum_regs_phys = pinfo->rsrc_reg.start;
	pinfo->platinum_regs = ioremap(pinfo->rsrc_reg.start, 0x1000);

	pinfo->cmap_regs_phys = 0xf301b000;	/* XXX not in prom? */
	request_mem_region(pinfo->cmap_regs_phys, 0x1000, "platinumfb cmap");
	pinfo->cmap_regs = ioremap(pinfo->cmap_regs_phys, 0x1000);

	/* Grok total video ram */
	out_be32(&pinfo->platinum_regs->reg[16].r, (unsigned)pinfo->frame_buffer_phys);
	out_be32(&pinfo->platinum_regs->reg[20].r, 0x1011);	/* select max vram */
	out_be32(&pinfo->platinum_regs->reg[24].r, 0);	/* switch in vram */

	fbuffer = pinfo->base_frame_buffer;
	fbuffer[0x100000] = 0x34;
	fbuffer[0x100008] = 0x0;
	invalidate_cache(&fbuffer[0x100000]);
	fbuffer[0x200000] = 0x56;
	fbuffer[0x200008] = 0x0;
	invalidate_cache(&fbuffer[0x200000]);
	fbuffer[0x300000] = 0x78;
	fbuffer[0x300008] = 0x0;
	invalidate_cache(&fbuffer[0x300000]);
	bank0 = 1; /* builtin 1MB vram, always there */
	bank1 = fbuffer[0x100000] == 0x34;
	bank2 = fbuffer[0x200000] == 0x56;
	bank3 = fbuffer[0x300000] == 0x78;
	pinfo->total_vram = (bank0 + bank1 + bank2 + bank3) * 0x100000;
	printk(KERN_INFO "platinumfb: Total VRAM = %dMB (%d%d%d%d)\n",
	       (unsigned int) (pinfo->total_vram / 1024 / 1024),
	       bank3, bank2, bank1, bank0);

	/*
	 * Try to determine whether we have an old or a new DACula.
	 */
	out_8(&pinfo->cmap_regs->addr, 0x40);
	pinfo->dactype = in_8(&pinfo->cmap_regs->d2);
	switch (pinfo->dactype) {
	case 0x3c:
		pinfo->clktype = 1;
		printk(KERN_INFO "platinumfb: DACula type 0x3c\n");
		break;
	case 0x84:
		pinfo->clktype = 0;
		printk(KERN_INFO "platinumfb: DACula type 0x84\n");
		break;
	default:
		pinfo->clktype = 0;
		printk(KERN_INFO "platinumfb: Unknown DACula type: %x\n", pinfo->dactype);
		break;
	}
	dev_set_drvdata(&odev->dev, info);
	
	rc = platinum_init_fb(info);
	if (rc != 0) {
		iounmap(pinfo->frame_buffer);
		iounmap(pinfo->platinum_regs);
		iounmap(pinfo->cmap_regs);
		dev_set_drvdata(&odev->dev, NULL);
		framebuffer_release(info);
	}

	return rc;
}

static int __devexit platinumfb_remove(struct platform_device* odev)
{
	struct fb_info		*info = dev_get_drvdata(&odev->dev);
	struct fb_info_platinum	*pinfo = info->par;
	
        unregister_framebuffer (info);
	
	/* Unmap frame buffer and registers */
	iounmap(pinfo->frame_buffer);
	iounmap(pinfo->platinum_regs);
	iounmap(pinfo->cmap_regs);

	release_mem_region(pinfo->rsrc_fb.start,
			   resource_size(&pinfo->rsrc_fb));

	release_mem_region(pinfo->cmap_regs_phys, 0x1000);

	framebuffer_release(info);

	return 0;
}

static struct of_device_id platinumfb_match[] = 
{
	{
	.name 		= "platinum",
	},
	{},
};

static struct platform_driver platinum_driver = 
{
	.driver = {
		.name = "platinumfb",
		.owner = THIS_MODULE,
		.of_match_table = platinumfb_match,
	},
	.probe		= platinumfb_probe,
	.remove		= platinumfb_remove,
};

static int __init platinumfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("platinumfb", &option))
		return -ENODEV;
	platinumfb_setup(option);
#endif
	platform_driver_register(&platinum_driver);

	return 0;
}

static void __exit platinumfb_exit(void)
{
	platform_driver_unregister(&platinum_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("framebuffer driver for Apple Platinum video");
module_init(platinumfb_init);

#ifdef MODULE
module_exit(platinumfb_exit);
#endif
