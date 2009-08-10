/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#define _MASTER_FILE

#include "global.h"

static int MAX_CURS = 32;
static struct fb_var_screeninfo default_var;
static char *viafb_name = "Via";
static u32 pseudo_pal[17];

/* video mode */
static char *viafb_mode = "640x480";
static char *viafb_mode1 = "640x480";

/* Added for specifying active devices.*/
char *viafb_active_dev = "";

/* Added for specifying video on devices.*/
char *viafb_video_dev = "";

/*Added for specify lcd output port*/
char *viafb_lcd_port = "";
char *viafb_dvi_port = "";

static void viafb_set_device(struct device_t active_dev);
static int apply_device_setting(struct viafb_ioctl_setting setting_info,
			 struct fb_info *info);
static void apply_second_mode_setting(struct fb_var_screeninfo
	*sec_var);
static void retrieve_device_setting(struct viafb_ioctl_setting
	*setting_info);
static void viafb_set_video_device(u32 video_dev_info);
static void viafb_get_video_device(u32 *video_dev_info);

/* Mode information */
static const struct viafb_modeinfo viafb_modentry[] = {
	{480, 640, VIA_RES_480X640},
	{640, 480, VIA_RES_640X480},
	{800, 480, VIA_RES_800X480},
	{800, 600, VIA_RES_800X600},
	{1024, 768, VIA_RES_1024X768},
	{1152, 864, VIA_RES_1152X864},
	{1280, 1024, VIA_RES_1280X1024},
	{1600, 1200, VIA_RES_1600X1200},
	{1440, 1050, VIA_RES_1440X1050},
	{1280, 768, VIA_RES_1280X768,},
	{1280, 800, VIA_RES_1280X800},
	{1280, 960, VIA_RES_1280X960},
	{1920, 1440, VIA_RES_1920X1440},
	{848, 480, VIA_RES_848X480},
	{1400, 1050, VIA_RES_1400X1050},
	{720, 480, VIA_RES_720X480},
	{720, 576, VIA_RES_720X576},
	{1024, 512, VIA_RES_1024X512},
	{1024, 576, VIA_RES_1024X576},
	{1024, 600, VIA_RES_1024X600},
	{1280, 720, VIA_RES_1280X720},
	{1920, 1080, VIA_RES_1920X1080},
	{1366, 768, VIA_RES_1368X768},
	{1680, 1050, VIA_RES_1680X1050},
	{960, 600, VIA_RES_960X600},
	{1000, 600, VIA_RES_1000X600},
	{1024, 576, VIA_RES_1024X576},
	{1024, 600, VIA_RES_1024X600},
	{1088, 612, VIA_RES_1088X612},
	{1152, 720, VIA_RES_1152X720},
	{1200, 720, VIA_RES_1200X720},
	{1280, 600, VIA_RES_1280X600},
	{1360, 768, VIA_RES_1360X768},
	{1440, 900, VIA_RES_1440X900},
	{1600, 900, VIA_RES_1600X900},
	{1600, 1024, VIA_RES_1600X1024},
	{1792, 1344, VIA_RES_1792X1344},
	{1856, 1392, VIA_RES_1856X1392},
	{1920, 1200, VIA_RES_1920X1200},
	{2048, 1536, VIA_RES_2048X1536},
	{0, 0, VIA_RES_INVALID}
};

static struct fb_ops viafb_ops;

static int viafb_update_fix(struct fb_fix_screeninfo *fix, struct fb_info *info)
{
	struct viafb_par *ppar;
	ppar = info->par;

	DEBUG_MSG(KERN_INFO "viafb_update_fix!\n");

	fix->visual =
	    ppar->bpp == 8 ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	fix->line_length = ppar->linelength;

	return 0;
}


static void viafb_setup_fixinfo(struct fb_fix_screeninfo *fix,
	struct viafb_par *viaparinfo)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, viafb_name);

	fix->smem_start = viaparinfo->fbmem;
	fix->smem_len = viaparinfo->fbmem_free;
	fix->mmio_start = viaparinfo->mmio_base;
	fix->mmio_len = viaparinfo->mmio_len;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;

	fix->xpanstep = fix->ywrapstep = 0;
	fix->ypanstep = 1;

	/* Just tell the accel name */
	viafbinfo->fix.accel = FB_ACCEL_VIA_UNICHROME;
}
static int viafb_open(struct fb_info *info, int user)
{
	DEBUG_MSG(KERN_INFO "viafb_open!\n");
	return 0;
}

static int viafb_release(struct fb_info *info, int user)
{
	DEBUG_MSG(KERN_INFO "viafb_release!\n");
	return 0;
}

static void viafb_update_viafb_par(struct fb_info *info)
{
	struct viafb_par *ppar;

	ppar = info->par;
	ppar->bpp = info->var.bits_per_pixel;
	ppar->linelength = ((info->var.xres_virtual + 7) & ~7) * ppar->bpp / 8;
	ppar->hres = info->var.xres;
	ppar->vres = info->var.yres;
	ppar->xoffset = info->var.xoffset;
	ppar->yoffset = info->var.yoffset;
}

static int viafb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	int vmode_index, htotal, vtotal;
	struct viafb_par *ppar;
	u32 long_refresh;
	struct viafb_par *p_viafb_par;
	ppar = info->par;


	DEBUG_MSG(KERN_INFO "viafb_check_var!\n");
	/* Sanity check */
	/* HW neither support interlacte nor double-scaned mode */
	if (var->vmode & FB_VMODE_INTERLACED || var->vmode & FB_VMODE_DOUBLE)
		return -EINVAL;

	vmode_index = viafb_get_mode_index(var->xres, var->yres);
	if (vmode_index == VIA_RES_INVALID) {
		DEBUG_MSG(KERN_INFO
			  "viafb: Mode %dx%dx%d not supported!!\n",
			  var->xres, var->yres, var->bits_per_pixel);
		return -EINVAL;
	}

	if (24 == var->bits_per_pixel)
		var->bits_per_pixel = 32;

	if (var->bits_per_pixel != 8 && var->bits_per_pixel != 16 &&
		var->bits_per_pixel != 32)
		return -EINVAL;

	if ((var->xres_virtual * (var->bits_per_pixel >> 3)) & 0x1F)
		/*32 pixel alignment */
		var->xres_virtual = (var->xres_virtual + 31) & ~31;
	if (var->xres_virtual * var->yres_virtual * var->bits_per_pixel / 8 >
		ppar->memsize)
		return -EINVAL;

	/* Based on var passed in to calculate the refresh,
	 * because our driver use some modes special.
	 */
	htotal = var->xres + var->left_margin +
	var->right_margin + var->hsync_len;
	vtotal = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len;
	long_refresh = 1000000000UL / var->pixclock * 1000;
	long_refresh /= (htotal * vtotal);

	viafb_refresh = viafb_get_refresh(var->xres, var->yres, long_refresh);

	/* Adjust var according to our driver's own table */
	viafb_fill_var_timing_info(var, viafb_refresh, vmode_index);

	/* This is indeed a patch for VT3353 */
	if (!info->par)
		return -1;
	p_viafb_par = (struct viafb_par *)info->par;
	if (p_viafb_par->chip_info->gfx_chip_name == UNICHROME_VX800)
		var->accel_flags = 0;

	return 0;
}

static int viafb_set_par(struct fb_info *info)
{
	int vmode_index;
	int vmode_index1 = 0;
	DEBUG_MSG(KERN_INFO "viafb_set_par!\n");

	viafb_update_device_setting(info->var.xres, info->var.yres,
			      info->var.bits_per_pixel, viafb_refresh, 0);

	vmode_index = viafb_get_mode_index(info->var.xres, info->var.yres);

	if (viafb_SAMM_ON == 1) {
		DEBUG_MSG(KERN_INFO
		"viafb_second_xres = %d, viafb_second_yres = %d, bpp = %d\n",
			  viafb_second_xres, viafb_second_yres, viafb_bpp1);
		vmode_index1 = viafb_get_mode_index(viafb_second_xres,
			viafb_second_yres);
		DEBUG_MSG(KERN_INFO "->viafb_SAMM_ON: index=%d\n",
			vmode_index1);

		viafb_update_device_setting(viafb_second_xres,
			viafb_second_yres, viafb_bpp1, viafb_refresh1, 1);
	}

	if (vmode_index != VIA_RES_INVALID) {
		viafb_setmode(vmode_index, info->var.xres, info->var.yres,
			info->var.bits_per_pixel, vmode_index1,
			viafb_second_xres, viafb_second_yres, viafb_bpp1);

		/*We should set memory offset according virtual_x */
		/*Fix me:put this function into viafb_setmode */
		viafb_memory_pitch_patch(info);

		/* Update ***fb_par information */
		viafb_update_viafb_par(info);

		/* Update other fixed information */
		viafb_update_fix(&info->fix, info);
		viafb_bpp = info->var.bits_per_pixel;
		/* Update viafb_accel, it is necessary to our 2D accelerate */
		viafb_accel = info->var.accel_flags;

		if (viafb_accel)
			viafb_set_2d_color_depth(info->var.bits_per_pixel);
	}

	return 0;
}

/* Set one color register */
static int viafb_setcolreg(unsigned regno, unsigned red, unsigned green,
unsigned blue, unsigned transp, struct fb_info *info)
{
	u8 sr1a, sr1b, cr67, cr6a, rev = 0, shift = 10;
	unsigned cmap_entries = (info->var.bits_per_pixel == 8) ? 256 : 16;
	DEBUG_MSG(KERN_INFO "viafb_setcolreg!\n");
	if (regno >= cmap_entries)
		return 1;
	if (UNICHROME_CLE266 == viaparinfo->chip_info->gfx_chip_name) {
		/*
		 * Read PCI bus 0,dev 0,function 0,index 0xF6 to get chip rev.
		 */
		outl(0x80000000 | (0xf6 & ~3), (unsigned long)0xCF8);
		rev = (inl((unsigned long)0xCFC) >> ((0xf6 & 3) * 8)) & 0xff;
	}
	switch (info->var.bits_per_pixel) {
	case 8:
		outb(0x1A, 0x3C4);
		sr1a = inb(0x3C5);
		outb(0x1B, 0x3C4);
		sr1b = inb(0x3C5);
		outb(0x67, 0x3D4);
		cr67 = inb(0x3D5);
		outb(0x6A, 0x3D4);
		cr6a = inb(0x3D5);

		/* Map the 3C6/7/8/9 to the IGA2 */
		outb(0x1A, 0x3C4);
		outb(sr1a | 0x01, 0x3C5);
		/* Second Display Engine colck always on */
		outb(0x1B, 0x3C4);
		outb(sr1b | 0x80, 0x3C5);
		/* Second Display Color Depth 8 */
		outb(0x67, 0x3D4);
		outb(cr67 & 0x3F, 0x3D5);
		outb(0x6A, 0x3D4);
		/* Second Display Channel Reset CR6A[6]) */
		outb(cr6a & 0xBF, 0x3D5);
		/* Second Display Channel Enable CR6A[7] */
		outb(cr6a | 0x80, 0x3D5);
		/* Second Display Channel stop reset) */
		outb(cr6a | 0x40, 0x3D5);

		/* Bit mask of palette */
		outb(0xFF, 0x3c6);
		/* Write one register of IGA2 */
		outb(regno, 0x3C8);
		if (UNICHROME_CLE266 == viaparinfo->chip_info->gfx_chip_name &&
			rev >= 15) {
			shift = 8;
			viafb_write_reg_mask(CR6A, VIACR, BIT5, BIT5);
			viafb_write_reg_mask(SR15, VIASR, BIT7, BIT7);
		} else {
			shift = 10;
			viafb_write_reg_mask(CR6A, VIACR, 0, BIT5);
			viafb_write_reg_mask(SR15, VIASR, 0, BIT7);
		}
		outb(red >> shift, 0x3C9);
		outb(green >> shift, 0x3C9);
		outb(blue >> shift, 0x3C9);

		/* Map the 3C6/7/8/9 to the IGA1 */
		outb(0x1A, 0x3C4);
		outb(sr1a & 0xFE, 0x3C5);
		/* Bit mask of palette */
		outb(0xFF, 0x3c6);
		/* Write one register of IGA1 */
		outb(regno, 0x3C8);
		outb(red >> shift, 0x3C9);
		outb(green >> shift, 0x3C9);
		outb(blue >> shift, 0x3C9);

		outb(0x1A, 0x3C4);
		outb(sr1a, 0x3C5);
		outb(0x1B, 0x3C4);
		outb(sr1b, 0x3C5);
		outb(0x67, 0x3D4);
		outb(cr67, 0x3D5);
		outb(0x6A, 0x3D4);
		outb(cr6a, 0x3D5);
		break;
	case 16:
		((u32 *) info->pseudo_palette)[regno] = (red & 0xF800) |
		    ((green & 0xFC00) >> 5) | ((blue & 0xF800) >> 11);
		break;
	case 32:
		((u32 *) info->pseudo_palette)[regno] =
		    ((transp & 0xFF00) << 16) |
		    ((red & 0xFF00) << 8) |
		    ((green & 0xFF00)) | ((blue & 0xFF00) >> 8);
		break;
	}

	return 0;

}

/*CALLED BY: fb_set_cmap */
/*           fb_set_var, pass 256 colors */
/*CALLED BY: fb_set_cmap */
/*           fbcon_set_palette, pass 16 colors */
static int viafb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	u32 len = cmap->len;
	u32 i;
	u16 *pred = cmap->red;
	u16 *pgreen = cmap->green;
	u16 *pblue = cmap->blue;
	u16 *ptransp = cmap->transp;
	u8 sr1a, sr1b, cr67, cr6a, rev = 0, shift = 10;
	if (len > 256)
		return 1;
	if (UNICHROME_CLE266 == viaparinfo->chip_info->gfx_chip_name) {
		/*
		 * Read PCI bus 0, dev 0, function 0, index 0xF6 to get chip
		 * rev.
		 */
		outl(0x80000000 | (0xf6 & ~3), (unsigned long)0xCF8);
		rev = (inl((unsigned long)0xCFC) >> ((0xf6 & 3) * 8)) & 0xff;
	}
	switch (info->var.bits_per_pixel) {
	case 8:
		outb(0x1A, 0x3C4);
		sr1a = inb(0x3C5);
		outb(0x1B, 0x3C4);
		sr1b = inb(0x3C5);
		outb(0x67, 0x3D4);
		cr67 = inb(0x3D5);
		outb(0x6A, 0x3D4);
		cr6a = inb(0x3D5);
		/* Map the 3C6/7/8/9 to the IGA2 */
		outb(0x1A, 0x3C4);
		outb(sr1a | 0x01, 0x3C5);
		outb(0x1B, 0x3C4);
		/* Second Display Engine colck always on */
		outb(sr1b | 0x80, 0x3C5);
		outb(0x67, 0x3D4);
		/* Second Display Color Depth 8 */
		outb(cr67 & 0x3F, 0x3D5);
		outb(0x6A, 0x3D4);
		/* Second Display Channel Reset CR6A[6]) */
		outb(cr6a & 0xBF, 0x3D5);
		/* Second Display Channel Enable CR6A[7] */
		outb(cr6a | 0x80, 0x3D5);
		/* Second Display Channel stop reset) */
		outb(cr6a | 0xC0, 0x3D5);

		/* Bit mask of palette */
		outb(0xFF, 0x3c6);
		outb(0x00, 0x3C8);
		if (UNICHROME_CLE266 == viaparinfo->chip_info->gfx_chip_name &&
			rev >= 15) {
			shift = 8;
			viafb_write_reg_mask(CR6A, VIACR, BIT5, BIT5);
			viafb_write_reg_mask(SR15, VIASR, BIT7, BIT7);
		} else {
			shift = 10;
			viafb_write_reg_mask(CR6A, VIACR, 0, BIT5);
			viafb_write_reg_mask(SR15, VIASR, 0, BIT7);
		}
		for (i = 0; i < len; i++) {
			outb((*(pred + i)) >> shift, 0x3C9);
			outb((*(pgreen + i)) >> shift, 0x3C9);
			outb((*(pblue + i)) >> shift, 0x3C9);
		}

		outb(0x1A, 0x3C4);
		/* Map the 3C6/7/8/9 to the IGA1 */
		outb(sr1a & 0xFE, 0x3C5);
		/* Bit mask of palette */
		outb(0xFF, 0x3c6);
		outb(0x00, 0x3C8);
		for (i = 0; i < len; i++) {
			outb((*(pred + i)) >> shift, 0x3C9);
			outb((*(pgreen + i)) >> shift, 0x3C9);
			outb((*(pblue + i)) >> shift, 0x3C9);
		}

		outb(0x1A, 0x3C4);
		outb(sr1a, 0x3C5);
		outb(0x1B, 0x3C4);
		outb(sr1b, 0x3C5);
		outb(0x67, 0x3D4);
		outb(cr67, 0x3D5);
		outb(0x6A, 0x3D4);
		outb(cr6a, 0x3D5);
		break;
	case 16:
		if (len > 17)
			return 0;	/* Because static u32 pseudo_pal[17]; */
		for (i = 0; i < len; i++)
			((u32 *) info->pseudo_palette)[i] =
			    (*(pred + i) & 0xF800) |
			    ((*(pgreen + i) & 0xFC00) >> 5) |
			    ((*(pblue + i) & 0xF800) >> 11);
		break;
	case 32:
		if (len > 17)
			return 0;
		if (ptransp) {
			for (i = 0; i < len; i++)
				((u32 *) info->pseudo_palette)[i] =
				    ((*(ptransp + i) & 0xFF00) << 16) |
				    ((*(pred + i) & 0xFF00) << 8) |
				    ((*(pgreen + i) & 0xFF00)) |
				    ((*(pblue + i) & 0xFF00) >> 8);
		} else {
			for (i = 0; i < len; i++)
				((u32 *) info->pseudo_palette)[i] =
				    0x00000000 |
				    ((*(pred + i) & 0xFF00) << 8) |
				    ((*(pgreen + i) & 0xFF00)) |
				    ((*(pblue + i) & 0xFF00) >> 8);
		}
		break;
	}
	return 0;
}

static int viafb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	unsigned int offset;

	DEBUG_MSG(KERN_INFO "viafb_pan_display!\n");

	offset = (var->xoffset + (var->yoffset * var->xres_virtual)) *
	    var->bits_per_pixel / 16;

	DEBUG_MSG(KERN_INFO "\nviafb_pan_display,offset =%d ", offset);

	viafb_write_reg_mask(0x48, 0x3d4, ((offset >> 24) & 0x3), 0x3);
	viafb_write_reg_mask(0x34, 0x3d4, ((offset >> 16) & 0xff), 0xff);
	viafb_write_reg_mask(0x0c, 0x3d4, ((offset >> 8) & 0xff), 0xff);
	viafb_write_reg_mask(0x0d, 0x3d4, (offset & 0xff), 0xff);

	return 0;
}

static int viafb_blank(int blank_mode, struct fb_info *info)
{
	DEBUG_MSG(KERN_INFO "viafb_blank!\n");
	/* clear DPMS setting */

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* Screen: On, HSync: On, VSync: On */
		/* control CRT monitor power management */
		viafb_write_reg_mask(CR36, VIACR, 0x00, BIT4 + BIT5);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		/* Screen: Off, HSync: Off, VSync: On */
		/* control CRT monitor power management */
		viafb_write_reg_mask(CR36, VIACR, 0x10, BIT4 + BIT5);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		/* Screen: Off, HSync: On, VSync: Off */
		/* control CRT monitor power management */
		viafb_write_reg_mask(CR36, VIACR, 0x20, BIT4 + BIT5);
		break;
	case FB_BLANK_POWERDOWN:
		/* Screen: Off, HSync: Off, VSync: Off */
		/* control CRT monitor power management */
		viafb_write_reg_mask(CR36, VIACR, 0x30, BIT4 + BIT5);
		break;
	}

	return 0;
}

static int viafb_ioctl(struct fb_info *info, u_int cmd, u_long arg)
{
	union {
		struct viafb_ioctl_mode viamode;
		struct viafb_ioctl_samm viasamm;
		struct viafb_driver_version driver_version;
		struct fb_var_screeninfo sec_var;
		struct _panel_size_pos_info panel_pos_size_para;
		struct viafb_ioctl_setting viafb_setting;
		struct device_t active_dev;
	} u;
	u32 state_info = 0;
	u32 *viafb_gamma_table;
	char driver_name[] = "viafb";

	u32 __user *argp = (u32 __user *) arg;
	u32 gpu32;
	u32 video_dev_info = 0;

	DEBUG_MSG(KERN_INFO "viafb_ioctl: 0x%X !!\n", cmd);
	memset(&u, 0, sizeof(u));

	switch (cmd) {
	case VIAFB_GET_CHIP_INFO:
		if (copy_to_user(argp, viaparinfo->chip_info,
				sizeof(struct chip_information)))
			return -EFAULT;
		break;
	case VIAFB_GET_INFO_SIZE:
		return put_user((u32)sizeof(struct viafb_ioctl_info), argp);
	case VIAFB_GET_INFO:
		return viafb_ioctl_get_viafb_info(arg);
	case VIAFB_HOTPLUG:
		return put_user(viafb_ioctl_hotplug(info->var.xres,
					      info->var.yres,
					      info->var.bits_per_pixel), argp);
	case VIAFB_SET_HOTPLUG_FLAG:
		if (copy_from_user(&gpu32, argp, sizeof(gpu32)))
			return -EFAULT;
		viafb_hotplug = (gpu32) ? 1 : 0;
		break;
	case VIAFB_GET_RESOLUTION:
		u.viamode.xres = (u32) viafb_hotplug_Xres;
		u.viamode.yres = (u32) viafb_hotplug_Yres;
		u.viamode.refresh = (u32) viafb_hotplug_refresh;
		u.viamode.bpp = (u32) viafb_hotplug_bpp;
		if (viafb_SAMM_ON == 1) {
			u.viamode.xres_sec = viafb_second_xres;
			u.viamode.yres_sec = viafb_second_yres;
			u.viamode.virtual_xres_sec = viafb_second_virtual_xres;
			u.viamode.virtual_yres_sec = viafb_second_virtual_yres;
			u.viamode.refresh_sec = viafb_refresh1;
			u.viamode.bpp_sec = viafb_bpp1;
		} else {
			u.viamode.xres_sec = 0;
			u.viamode.yres_sec = 0;
			u.viamode.virtual_xres_sec = 0;
			u.viamode.virtual_yres_sec = 0;
			u.viamode.refresh_sec = 0;
			u.viamode.bpp_sec = 0;
		}
		if (copy_to_user(argp, &u.viamode, sizeof(u.viamode)))
			return -EFAULT;
		break;
	case VIAFB_GET_SAMM_INFO:
		u.viasamm.samm_status = viafb_SAMM_ON;

		if (viafb_SAMM_ON == 1) {
			if (viafb_dual_fb) {
				u.viasamm.size_prim = viaparinfo->fbmem_free;
				u.viasamm.size_sec = viaparinfo1->fbmem_free;
			} else {
				if (viafb_second_size) {
					u.viasamm.size_prim =
					    viaparinfo->fbmem_free -
					    viafb_second_size * 1024 * 1024;
					u.viasamm.size_sec =
					    viafb_second_size * 1024 * 1024;
				} else {
					u.viasamm.size_prim =
					    viaparinfo->fbmem_free >> 1;
					u.viasamm.size_sec =
					    (viaparinfo->fbmem_free >> 1);
				}
			}
			u.viasamm.mem_base = viaparinfo->fbmem;
			u.viasamm.offset_sec = viafb_second_offset;
		} else {
			u.viasamm.size_prim =
			    viaparinfo->memsize - viaparinfo->fbmem_used;
			u.viasamm.size_sec = 0;
			u.viasamm.mem_base = viaparinfo->fbmem;
			u.viasamm.offset_sec = 0;
		}

		if (copy_to_user(argp, &u.viasamm, sizeof(u.viasamm)))
			return -EFAULT;

		break;
	case VIAFB_TURN_ON_OUTPUT_DEVICE:
		if (copy_from_user(&gpu32, argp, sizeof(gpu32)))
			return -EFAULT;
		if (gpu32 & CRT_Device)
			viafb_crt_enable();
		if (gpu32 & DVI_Device)
			viafb_dvi_enable();
		if (gpu32 & LCD_Device)
			viafb_lcd_enable();
		break;
	case VIAFB_TURN_OFF_OUTPUT_DEVICE:
		if (copy_from_user(&gpu32, argp, sizeof(gpu32)))
			return -EFAULT;
		if (gpu32 & CRT_Device)
			viafb_crt_disable();
		if (gpu32 & DVI_Device)
			viafb_dvi_disable();
		if (gpu32 & LCD_Device)
			viafb_lcd_disable();
		break;
	case VIAFB_SET_DEVICE:
		if (copy_from_user(&u.active_dev, (void *)argp,
			sizeof(u.active_dev)))
			return -EFAULT;
		viafb_set_device(u.active_dev);
		viafb_set_par(info);
		break;
	case VIAFB_GET_DEVICE:
		u.active_dev.crt = viafb_CRT_ON;
		u.active_dev.dvi = viafb_DVI_ON;
		u.active_dev.lcd = viafb_LCD_ON;
		u.active_dev.samm = viafb_SAMM_ON;
		u.active_dev.primary_dev = viafb_primary_dev;

		u.active_dev.lcd_dsp_cent = viafb_lcd_dsp_method;
		u.active_dev.lcd_panel_id = viafb_lcd_panel_id;
		u.active_dev.lcd_mode = viafb_lcd_mode;

		u.active_dev.xres = viafb_hotplug_Xres;
		u.active_dev.yres = viafb_hotplug_Yres;

		u.active_dev.xres1 = viafb_second_xres;
		u.active_dev.yres1 = viafb_second_yres;

		u.active_dev.bpp = viafb_bpp;
		u.active_dev.bpp1 = viafb_bpp1;
		u.active_dev.refresh = viafb_refresh;
		u.active_dev.refresh1 = viafb_refresh1;

		u.active_dev.epia_dvi = viafb_platform_epia_dvi;
		u.active_dev.lcd_dual_edge = viafb_device_lcd_dualedge;
		u.active_dev.bus_width = viafb_bus_width;

		if (copy_to_user(argp, &u.active_dev, sizeof(u.active_dev)))
			return -EFAULT;
		break;

	case VIAFB_GET_DRIVER_VERSION:
		u.driver_version.iMajorNum = VERSION_MAJOR;
		u.driver_version.iKernelNum = VERSION_KERNEL;
		u.driver_version.iOSNum = VERSION_OS;
		u.driver_version.iMinorNum = VERSION_MINOR;

		if (copy_to_user(argp, &u.driver_version,
			sizeof(u.driver_version)))
			return -EFAULT;

		break;

	case VIAFB_SET_DEVICE_INFO:
		if (copy_from_user(&u.viafb_setting,
			argp, sizeof(u.viafb_setting)))
			return -EFAULT;
		if (apply_device_setting(u.viafb_setting, info) < 0)
			return -EINVAL;

		break;

	case VIAFB_SET_SECOND_MODE:
		if (copy_from_user(&u.sec_var, argp, sizeof(u.sec_var)))
			return -EFAULT;
		apply_second_mode_setting(&u.sec_var);
		break;

	case VIAFB_GET_DEVICE_INFO:

		retrieve_device_setting(&u.viafb_setting);

		if (copy_to_user(argp, &u.viafb_setting,
				 sizeof(u.viafb_setting)))
			return -EFAULT;

		break;

	case VIAFB_GET_DEVICE_SUPPORT:
		viafb_get_device_support_state(&state_info);
		if (put_user(state_info, argp))
			return -EFAULT;
		break;

	case VIAFB_GET_DEVICE_CONNECT:
		viafb_get_device_connect_state(&state_info);
		if (put_user(state_info, argp))
			return -EFAULT;
		break;

	case VIAFB_GET_PANEL_SUPPORT_EXPAND:
		state_info =
		    viafb_lcd_get_support_expand_state(info->var.xres,
						 info->var.yres);
		if (put_user(state_info, argp))
			return -EFAULT;
		break;

	case VIAFB_GET_DRIVER_NAME:
		if (copy_to_user(argp, driver_name, sizeof(driver_name)))
			return -EFAULT;
		break;

	case VIAFB_SET_GAMMA_LUT:
		viafb_gamma_table = kmalloc(256 * sizeof(u32), GFP_KERNEL);
		if (!viafb_gamma_table)
			return -ENOMEM;
		if (copy_from_user(viafb_gamma_table, argp,
				sizeof(viafb_gamma_table))) {
			kfree(viafb_gamma_table);
			return -EFAULT;
		}
		viafb_set_gamma_table(viafb_bpp, viafb_gamma_table);
		kfree(viafb_gamma_table);
		break;

	case VIAFB_GET_GAMMA_LUT:
		viafb_gamma_table = kmalloc(256 * sizeof(u32), GFP_KERNEL);
		if (!viafb_gamma_table)
			return -ENOMEM;
		viafb_get_gamma_table(viafb_gamma_table);
		if (copy_to_user(argp, viafb_gamma_table,
			sizeof(viafb_gamma_table))) {
			kfree(viafb_gamma_table);
			return -EFAULT;
		}
		kfree(viafb_gamma_table);
		break;

	case VIAFB_GET_GAMMA_SUPPORT_STATE:
		viafb_get_gamma_support_state(viafb_bpp, &state_info);
		if (put_user(state_info, argp))
			return -EFAULT;
		break;
	case VIAFB_SET_VIDEO_DEVICE:
		get_user(video_dev_info, argp);
		viafb_set_video_device(video_dev_info);
		break;
	case VIAFB_GET_VIDEO_DEVICE:
		viafb_get_video_device(&video_dev_info);
		if (put_user(video_dev_info, argp))
			return -EFAULT;
		break;
	case VIAFB_SYNC_SURFACE:
		DEBUG_MSG(KERN_INFO "lobo VIAFB_SYNC_SURFACE\n");
		break;
	case VIAFB_GET_DRIVER_CAPS:
		break;

	case VIAFB_GET_PANEL_MAX_SIZE:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		u.panel_pos_size_para.x = u.panel_pos_size_para.y = 0;
		if (copy_to_user(argp, &u.panel_pos_size_para,
		     sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;
	case VIAFB_GET_PANEL_MAX_POSITION:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		u.panel_pos_size_para.x = u.panel_pos_size_para.y = 0;
		if (copy_to_user(argp, &u.panel_pos_size_para,
				 sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;

	case VIAFB_GET_PANEL_POSITION:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		u.panel_pos_size_para.x = u.panel_pos_size_para.y = 0;
		if (copy_to_user(argp, &u.panel_pos_size_para,
				 sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;
	case VIAFB_GET_PANEL_SIZE:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		u.panel_pos_size_para.x = u.panel_pos_size_para.y = 0;
		if (copy_to_user(argp, &u.panel_pos_size_para,
				 sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;

	case VIAFB_SET_PANEL_POSITION:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;
	case VIAFB_SET_PANEL_SIZE:
		if (copy_from_user(&u.panel_pos_size_para, argp,
				   sizeof(u.panel_pos_size_para)))
			return -EFAULT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void viafb_fillrect(struct fb_info *info,
	const struct fb_fillrect *rect)
{
	u32 col = 0, rop = 0;
	int pitch;

	if (!viafb_accel) {
		cfb_fillrect(info, rect);
		return;
	}

	if (!rect->width || !rect->height)
		return;

	switch (rect->rop) {
	case ROP_XOR:
		rop = 0x5A;
		break;
	case ROP_COPY:
	default:
		rop = 0xF0;
		break;
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		col = rect->color;
		break;
	case 16:
		col = ((u32 *) (info->pseudo_palette))[rect->color];
		break;
	case 32:
		col = ((u32 *) (info->pseudo_palette))[rect->color];
		break;
	}

	/* BitBlt Source Address */
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCPOS);
	/* Source Base Address */
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCBASE);
	/* Destination Base Address */
	writel(((unsigned long) (info->screen_base) -
		   (unsigned long) viafb_FB_MM) >> 3,
		   viaparinfo->io_virt + VIA_REG_DSTBASE);
	/* Pitch */
	pitch = (info->var.xres_virtual + 7) & ~7;
	writel(VIA_PITCH_ENABLE |
		   (((pitch *
		      info->var.bits_per_pixel >> 3) >> 3) |
		      (((pitch * info->
		      var.bits_per_pixel >> 3) >> 3) << 16)),
		      viaparinfo->io_virt + VIA_REG_PITCH);
	/* BitBlt Destination Address */
	writel(((rect->dy << 16) | rect->dx),
		viaparinfo->io_virt + VIA_REG_DSTPOS);
	/* Dimension: width & height */
	writel((((rect->height - 1) << 16) | (rect->width - 1)),
		viaparinfo->io_virt + VIA_REG_DIMENSION);
	/* Forground color or Destination color */
	writel(col, viaparinfo->io_virt + VIA_REG_FGCOLOR);
	/* GE Command */
	writel((0x01 | 0x2000 | (rop << 24)),
		viaparinfo->io_virt + VIA_REG_GECMD);

}

static void viafb_copyarea(struct fb_info *info,
	const struct fb_copyarea *area)
{
	u32 dy = area->dy, sy = area->sy, direction = 0x0;
	u32 sx = area->sx, dx = area->dx, width = area->width;
	int pitch;

	DEBUG_MSG(KERN_INFO "viafb_copyarea!!\n");

	if (!viafb_accel) {
		cfb_copyarea(info, area);
		return;
	}

	if (!area->width || !area->height)
		return;

	if (sy < dy) {
		dy += area->height - 1;
		sy += area->height - 1;
		direction |= 0x4000;
	}

	if (sx < dx) {
		dx += width - 1;
		sx += width - 1;
		direction |= 0x8000;
	}

	/* Source Base Address */
	writel(((unsigned long) (info->screen_base) -
		   (unsigned long) viafb_FB_MM) >> 3,
		   viaparinfo->io_virt + VIA_REG_SRCBASE);
	/* Destination Base Address */
	writel(((unsigned long) (info->screen_base) -
		   (unsigned long) viafb_FB_MM) >> 3,
		   viaparinfo->io_virt + VIA_REG_DSTBASE);
	/* Pitch */
	pitch = (info->var.xres_virtual + 7) & ~7;
	/* VIA_PITCH_ENABLE can be omitted now. */
	writel(VIA_PITCH_ENABLE |
		   (((pitch *
		      info->var.bits_per_pixel >> 3) >> 3) | (((pitch *
								info->var.
								bits_per_pixel
								>> 3) >> 3)
							      << 16)),
				viaparinfo->io_virt + VIA_REG_PITCH);
	/* BitBlt Source Address */
	writel(((sy << 16) | sx), viaparinfo->io_virt + VIA_REG_SRCPOS);
	/* BitBlt Destination Address */
	writel(((dy << 16) | dx), viaparinfo->io_virt + VIA_REG_DSTPOS);
	/* Dimension: width & height */
	writel((((area->height - 1) << 16) | (area->width - 1)),
		   viaparinfo->io_virt + VIA_REG_DIMENSION);
	/* GE Command */
	writel((0x01 | direction | (0xCC << 24)),
		viaparinfo->io_virt + VIA_REG_GECMD);

}

static void viafb_imageblit(struct fb_info *info,
	const struct fb_image *image)
{
	u32 size, bg_col = 0, fg_col = 0, *udata;
	int i;
	int pitch;

	if (!viafb_accel) {
		cfb_imageblit(info, image);
		return;
	}

	udata = (u32 *) image->data;

	switch (info->var.bits_per_pixel) {
	case 8:
		bg_col = image->bg_color;
		fg_col = image->fg_color;
		break;
	case 16:
		bg_col = ((u32 *) (info->pseudo_palette))[image->bg_color];
		fg_col = ((u32 *) (info->pseudo_palette))[image->fg_color];
		break;
	case 32:
		bg_col = ((u32 *) (info->pseudo_palette))[image->bg_color];
		fg_col = ((u32 *) (info->pseudo_palette))[image->fg_color];
		break;
	}
	size = image->width * image->height;

	/* Source Base Address */
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCBASE);
	/* Destination Base Address */
	writel(((unsigned long) (info->screen_base) -
		   (unsigned long) viafb_FB_MM) >> 3,
		   viaparinfo->io_virt + VIA_REG_DSTBASE);
	/* Pitch */
	pitch = (info->var.xres_virtual + 7) & ~7;
	writel(VIA_PITCH_ENABLE |
		   (((pitch *
		      info->var.bits_per_pixel >> 3) >> 3) | (((pitch *
								info->var.
								bits_per_pixel
								>> 3) >> 3)
							      << 16)),
				viaparinfo->io_virt + VIA_REG_PITCH);
	/* BitBlt Source Address */
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCPOS);
	/* BitBlt Destination Address */
	writel(((image->dy << 16) | image->dx),
		viaparinfo->io_virt + VIA_REG_DSTPOS);
	/* Dimension: width & height */
	writel((((image->height - 1) << 16) | (image->width - 1)),
		   viaparinfo->io_virt + VIA_REG_DIMENSION);
	/* fb color */
	writel(fg_col, viaparinfo->io_virt + VIA_REG_FGCOLOR);
	/* bg color */
	writel(bg_col, viaparinfo->io_virt + VIA_REG_BGCOLOR);
	/* GE Command */
	writel(0xCC020142, viaparinfo->io_virt + VIA_REG_GECMD);

	for (i = 0; i < size / 4; i++) {
		writel(*udata, viaparinfo->io_virt + VIA_MMIO_BLTBASE);
		udata++;
	}

}

static int viafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	u32 temp, xx, yy, bg_col = 0, fg_col = 0;
	int i, j = 0;
	static int hw_cursor;
	struct viafb_par *p_viafb_par;

	if (viafb_accel)
		hw_cursor = 1;

	if (!viafb_accel) {
		if (hw_cursor) {
			viafb_show_hw_cursor(info, HW_Cursor_OFF);
			hw_cursor = 0;
		}
		return -ENODEV;
	}

	if ((((struct viafb_par *)(info->par))->iga_path == IGA2)
	    && (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266))
		return -ENODEV;

	/* When duoview and using lcd , use soft cursor */
	if (viafb_LCD_ON || ((struct viafb_par *)(info->par))->duoview)
		return -ENODEV;

	viafb_show_hw_cursor(info, HW_Cursor_OFF);
	viacursor = *cursor;

	if (cursor->set & FB_CUR_SETHOT) {
		viacursor.hot = cursor->hot;
		temp = ((viacursor.hot.x) << 16) + viacursor.hot.y;
		writel(temp, viaparinfo->io_virt + VIA_REG_CURSOR_ORG);
	}

	if (cursor->set & FB_CUR_SETPOS) {
		viacursor.image.dx = cursor->image.dx;
		viacursor.image.dy = cursor->image.dy;
		yy = cursor->image.dy - info->var.yoffset;
		xx = cursor->image.dx - info->var.xoffset;
		temp = yy & 0xFFFF;
		temp |= (xx << 16);
		writel(temp, viaparinfo->io_virt + VIA_REG_CURSOR_POS);
	}

	if (cursor->set & FB_CUR_SETSIZE) {
		temp = readl(viaparinfo->io_virt + VIA_REG_CURSOR_MODE);

		if ((cursor->image.width <= 32)
		    && (cursor->image.height <= 32)) {
			MAX_CURS = 32;
			temp |= 0x2;
		} else if ((cursor->image.width <= 64)
			   && (cursor->image.height <= 64)) {
			MAX_CURS = 64;
			temp &= 0xFFFFFFFD;
		} else {
			DEBUG_MSG(KERN_INFO
			"The cursor image is biger than 64x64 bits...\n");
			return -ENXIO;
		}
		writel(temp, viaparinfo->io_virt + VIA_REG_CURSOR_MODE);

		viacursor.image.height = cursor->image.height;
		viacursor.image.width = cursor->image.width;
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		viacursor.image.fg_color = cursor->image.fg_color;
		viacursor.image.bg_color = cursor->image.bg_color;

		switch (info->var.bits_per_pixel) {
		case 8:
		case 16:
		case 32:
			bg_col =
			    (0xFF << 24) |
			    (((info->cmap.red)[viacursor.image.bg_color] &
			    0xFF00) << 8) |
			    ((info->cmap.green)[viacursor.image.bg_color] &
			    0xFF00) |
			    (((info->cmap.blue)[viacursor.image.bg_color] &
			    0xFF00) >> 8);
			fg_col =
			    (0xFF << 24) |
			    (((info->cmap.red)[viacursor.image.fg_color] &
			    0xFF00) << 8) |
			    ((info->cmap.green)[viacursor.image.fg_color] &
			    0xFF00) |
			    (((info->cmap.blue)[viacursor.image.fg_color] &
			    0xFF00) >> 8);
			break;
		default:
			return 0;
		}

		/* This is indeed a patch for VT3324/VT3353 */
		if (!info->par)
			return 0;
		p_viafb_par = (struct viafb_par *)info->par;

		if ((p_viafb_par->chip_info->gfx_chip_name ==
			UNICHROME_CX700) ||
			((p_viafb_par->chip_info->gfx_chip_name ==
			UNICHROME_VX800))) {
			bg_col =
			    (((info->cmap.red)[viacursor.image.bg_color] &
			    0xFFC0) << 14) |
			    (((info->cmap.green)[viacursor.image.bg_color] &
			    0xFFC0) << 4) |
			    (((info->cmap.blue)[viacursor.image.bg_color] &
			    0xFFC0) >> 6);
			fg_col =
			    (((info->cmap.red)[viacursor.image.fg_color] &
			    0xFFC0) << 14) |
			    (((info->cmap.green)[viacursor.image.fg_color] &
			    0xFFC0) << 4) |
			    (((info->cmap.blue)[viacursor.image.fg_color] &
			    0xFFC0) >> 6);
		}

		writel(bg_col, viaparinfo->io_virt + VIA_REG_CURSOR_BG);
		writel(fg_col, viaparinfo->io_virt + VIA_REG_CURSOR_FG);
	}

	if (cursor->set & FB_CUR_SETSHAPE) {
		struct {
			u8 data[CURSOR_SIZE / 8];
			u32 bak[CURSOR_SIZE / 32];
		} *cr_data = kzalloc(sizeof(*cr_data), GFP_ATOMIC);
		int size =
		    ((viacursor.image.width + 7) >> 3) *
		    viacursor.image.height;

		if (cr_data == NULL)
			goto out;

		if (MAX_CURS == 32) {
			for (i = 0; i < (CURSOR_SIZE / 32); i++) {
				cr_data->bak[i] = 0x0;
				cr_data->bak[i + 1] = 0xFFFFFFFF;
				i += 1;
			}
		} else if (MAX_CURS == 64) {
			for (i = 0; i < (CURSOR_SIZE / 32); i++) {
				cr_data->bak[i] = 0x0;
				cr_data->bak[i + 1] = 0x0;
				cr_data->bak[i + 2] = 0xFFFFFFFF;
				cr_data->bak[i + 3] = 0xFFFFFFFF;
				i += 3;
			}
		}

		switch (viacursor.rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				cr_data->data[i] = viacursor.mask[i];
			break;
		case ROP_COPY:

			for (i = 0; i < size; i++)
				cr_data->data[i] = viacursor.mask[i];
			break;
		default:
			break;
		}

		if (MAX_CURS == 32) {
			for (i = 0; i < size; i++) {
				cr_data->bak[j] = (u32) cr_data->data[i];
				cr_data->bak[j + 1] = ~cr_data->bak[j];
				j += 2;
			}
		} else if (MAX_CURS == 64) {
			for (i = 0; i < size; i++) {
				cr_data->bak[j] = (u32) cr_data->data[i];
				cr_data->bak[j + 1] = 0x0;
				cr_data->bak[j + 2] = ~cr_data->bak[j];
				cr_data->bak[j + 3] = ~cr_data->bak[j + 1];
				j += 4;
			}
		}

		memcpy(((struct viafb_par *)(info->par))->fbmem_virt +
		       ((struct viafb_par *)(info->par))->cursor_start,
		       cr_data->bak, CURSOR_SIZE);
out:
		kfree(cr_data);
	}

	if (viacursor.enable)
		viafb_show_hw_cursor(info, HW_Cursor_ON);

	return 0;
}

static int viafb_sync(struct fb_info *info)
{
	if (viafb_accel)
		viafb_wait_engine_idle();
	return 0;
}

int viafb_get_mode_index(int hres, int vres)
{
	u32 i;
	DEBUG_MSG(KERN_INFO "viafb_get_mode_index!\n");

	for (i = 0; viafb_modentry[i].mode_index != VIA_RES_INVALID; i++)
		if (viafb_modentry[i].xres == hres &&
			viafb_modentry[i].yres == vres)
			break;

	return viafb_modentry[i].mode_index;
}

static void check_available_device_to_enable(int device_id)
{
	int device_num = 0;

	/* Initialize: */
	viafb_CRT_ON = STATE_OFF;
	viafb_DVI_ON = STATE_OFF;
	viafb_LCD_ON = STATE_OFF;
	viafb_LCD2_ON = STATE_OFF;
	viafb_DeviceStatus = None_Device;

	if ((device_id & CRT_Device) && (device_num < MAX_ACTIVE_DEV_NUM)) {
		viafb_CRT_ON = STATE_ON;
		device_num++;
		viafb_DeviceStatus |= CRT_Device;
	}

	if ((device_id & DVI_Device) && (device_num < MAX_ACTIVE_DEV_NUM)) {
		viafb_DVI_ON = STATE_ON;
		device_num++;
		viafb_DeviceStatus |= DVI_Device;
	}

	if ((device_id & LCD_Device) && (device_num < MAX_ACTIVE_DEV_NUM)) {
		viafb_LCD_ON = STATE_ON;
		device_num++;
		viafb_DeviceStatus |= LCD_Device;
	}

	if ((device_id & LCD2_Device) && (device_num < MAX_ACTIVE_DEV_NUM)) {
		viafb_LCD2_ON = STATE_ON;
		device_num++;
		viafb_DeviceStatus |= LCD2_Device;
	}

	if (viafb_DeviceStatus == None_Device) {
		/* Use CRT as default active device: */
		viafb_CRT_ON = STATE_ON;
		viafb_DeviceStatus = CRT_Device;
	}
	DEBUG_MSG(KERN_INFO "Device Status:%x", viafb_DeviceStatus);
}

static void viafb_set_device(struct device_t active_dev)
{
	/* Check available device to enable: */
	int device_id = None_Device;
	if (active_dev.crt)
		device_id |= CRT_Device;
	if (active_dev.dvi)
		device_id |= DVI_Device;
	if (active_dev.lcd)
		device_id |= LCD_Device;

	check_available_device_to_enable(device_id);

	/* Check property of LCD: */
	if (viafb_LCD_ON) {
		if (active_dev.lcd_dsp_cent) {
			viaparinfo->lvds_setting_info->display_method =
				viafb_lcd_dsp_method = LCD_CENTERING;
		} else {
			viaparinfo->lvds_setting_info->display_method =
				viafb_lcd_dsp_method = LCD_EXPANDSION;
		}

		if (active_dev.lcd_mode == LCD_SPWG) {
			viaparinfo->lvds_setting_info->lcd_mode =
				viafb_lcd_mode = LCD_SPWG;
		} else {
			viaparinfo->lvds_setting_info->lcd_mode =
				viafb_lcd_mode = LCD_OPENLDI;
		}

		if (active_dev.lcd_panel_id <= LCD_PANEL_ID_MAXIMUM) {
			viafb_lcd_panel_id = active_dev.lcd_panel_id;
			viafb_init_lcd_size();
		}
	}

	/* Check property of mode: */
	if (!active_dev.xres1)
		viafb_second_xres = 640;
	else
		viafb_second_xres = active_dev.xres1;
	if (!active_dev.yres1)
		viafb_second_yres = 480;
	else
		viafb_second_yres = active_dev.yres1;
	if (active_dev.bpp != 0)
		viafb_bpp = active_dev.bpp;
	if (active_dev.bpp1 != 0)
		viafb_bpp1 = active_dev.bpp1;
	if (active_dev.refresh != 0)
		viafb_refresh = active_dev.refresh;
	if (active_dev.refresh1 != 0)
		viafb_refresh1 = active_dev.refresh1;
	if ((active_dev.samm == STATE_OFF) || (active_dev.samm == STATE_ON))
		viafb_SAMM_ON = active_dev.samm;
	viafb_primary_dev = active_dev.primary_dev;

	viafb_set_start_addr();
	viafb_set_iga_path();
}

static void viafb_set_video_device(u32 video_dev_info)
{
	viaparinfo->video_on_crt = STATE_OFF;
	viaparinfo->video_on_dvi = STATE_OFF;
	viaparinfo->video_on_lcd = STATE_OFF;

	/* Check available device to enable: */
	if ((video_dev_info & CRT_Device) == CRT_Device)
		viaparinfo->video_on_crt = STATE_ON;
	else if ((video_dev_info & DVI_Device) == DVI_Device)
		viaparinfo->video_on_dvi = STATE_ON;
	else if ((video_dev_info & LCD_Device) == LCD_Device)
		viaparinfo->video_on_lcd = STATE_ON;
}

static void viafb_get_video_device(u32 *video_dev_info)
{
	*video_dev_info = None_Device;
	if (viaparinfo->video_on_crt == STATE_ON)
		*video_dev_info |= CRT_Device;
	else if (viaparinfo->video_on_dvi == STATE_ON)
		*video_dev_info |= DVI_Device;
	else if (viaparinfo->video_on_lcd == STATE_ON)
		*video_dev_info |= LCD_Device;
}

static int get_primary_device(void)
{
	int primary_device = 0;
	/* Rule: device on iga1 path are the primary device. */
	if (viafb_SAMM_ON) {
		if (viafb_CRT_ON) {
			if (viaparinfo->crt_setting_info->iga_path == IGA1) {
				DEBUG_MSG(KERN_INFO "CRT IGA Path:%d\n",
					viaparinfo->
					crt_setting_info->iga_path);
				primary_device = CRT_Device;
			}
		}
		if (viafb_DVI_ON) {
			if (viaparinfo->tmds_setting_info->iga_path == IGA1) {
				DEBUG_MSG(KERN_INFO "DVI IGA Path:%d\n",
					viaparinfo->
					tmds_setting_info->iga_path);
				primary_device = DVI_Device;
			}
		}
		if (viafb_LCD_ON) {
			if (viaparinfo->lvds_setting_info->iga_path == IGA1) {
				DEBUG_MSG(KERN_INFO "LCD IGA Path:%d\n",
					viaparinfo->
					lvds_setting_info->iga_path);
				primary_device = LCD_Device;
			}
		}
		if (viafb_LCD2_ON) {
			if (viaparinfo->lvds_setting_info2->iga_path == IGA1) {
				DEBUG_MSG(KERN_INFO "LCD2 IGA Path:%d\n",
					viaparinfo->
					lvds_setting_info2->iga_path);
				primary_device = LCD2_Device;
			}
		}
	}
	return primary_device;
}

static u8 is_duoview(void)
{
	if (0 == viafb_SAMM_ON) {
		if (viafb_LCD_ON + viafb_LCD2_ON +
			viafb_DVI_ON + viafb_CRT_ON == 2)
			return true;
		return false;
	} else {
		return false;
	}
}

static void apply_second_mode_setting(struct fb_var_screeninfo
	*sec_var)
{
	u32 htotal, vtotal, long_refresh;

	htotal = sec_var->xres + sec_var->left_margin +
		sec_var->right_margin + sec_var->hsync_len;
	vtotal = sec_var->yres + sec_var->upper_margin +
		sec_var->lower_margin + sec_var->vsync_len;
	if ((sec_var->xres_virtual * (sec_var->bits_per_pixel >> 3)) & 0x1F) {
		/*Is 32 bytes alignment? */
		/*32 pixel alignment */
		sec_var->xres_virtual = (sec_var->xres_virtual + 31) & ~31;
	}

	htotal = sec_var->xres + sec_var->left_margin +
		sec_var->right_margin + sec_var->hsync_len;
	vtotal = sec_var->yres + sec_var->upper_margin +
		sec_var->lower_margin + sec_var->vsync_len;
	long_refresh = 1000000000UL / sec_var->pixclock * 1000;
	long_refresh /= (htotal * vtotal);

	viafb_second_xres = sec_var->xres;
	viafb_second_yres = sec_var->yres;
	viafb_second_virtual_xres = sec_var->xres_virtual;
	viafb_second_virtual_yres = sec_var->yres_virtual;
	viafb_bpp1 = sec_var->bits_per_pixel;
	viafb_refresh1 = viafb_get_refresh(sec_var->xres, sec_var->yres,
		long_refresh);
}

static int apply_device_setting(struct viafb_ioctl_setting setting_info,
	struct fb_info *info)
{
	int need_set_mode = 0;
	DEBUG_MSG(KERN_INFO "apply_device_setting\n");

	if (setting_info.device_flag) {
		need_set_mode = 1;
		check_available_device_to_enable(setting_info.device_status);
	}

	/* Unlock LCD's operation according to LCD flag
	   and check if the setting value is valid. */
	/* If the value is valid, apply the new setting value to the device. */
	if (viafb_LCD_ON) {
		if (setting_info.lcd_operation_flag & OP_LCD_CENTERING) {
			need_set_mode = 1;
			if (setting_info.lcd_attributes.display_center) {
				/* Centering */
				viaparinfo->lvds_setting_info->display_method =
				    LCD_CENTERING;
				viafb_lcd_dsp_method = LCD_CENTERING;
				viaparinfo->lvds_setting_info2->display_method =
				    viafb_lcd_dsp_method = LCD_CENTERING;
			} else {
				/* expandsion */
				viaparinfo->lvds_setting_info->display_method =
				    LCD_EXPANDSION;
				viafb_lcd_dsp_method = LCD_EXPANDSION;
				viaparinfo->lvds_setting_info2->display_method =
				    LCD_EXPANDSION;
				viafb_lcd_dsp_method = LCD_EXPANDSION;
			}
		}

		if (setting_info.lcd_operation_flag & OP_LCD_MODE) {
			need_set_mode = 1;
			if (setting_info.lcd_attributes.lcd_mode ==
				LCD_SPWG) {
				viaparinfo->lvds_setting_info->lcd_mode =
					viafb_lcd_mode = LCD_SPWG;
			} else {
				viaparinfo->lvds_setting_info->lcd_mode =
					viafb_lcd_mode = LCD_OPENLDI;
			}
			viaparinfo->lvds_setting_info2->lcd_mode =
			    viaparinfo->lvds_setting_info->lcd_mode;
		}

		if (setting_info.lcd_operation_flag & OP_LCD_PANEL_ID) {
			need_set_mode = 1;
			if (setting_info.lcd_attributes.panel_id <=
			    LCD_PANEL_ID_MAXIMUM) {
				viafb_lcd_panel_id =
				    setting_info.lcd_attributes.panel_id;
				viafb_init_lcd_size();
			}
		}
	}

	if (0 != (setting_info.samm_status & OP_SAMM)) {
		setting_info.samm_status =
		    setting_info.samm_status & (~OP_SAMM);
		if (setting_info.samm_status == 0
		    || setting_info.samm_status == 1) {
			viafb_SAMM_ON = setting_info.samm_status;

			if (viafb_SAMM_ON)
				viafb_primary_dev = setting_info.primary_device;

			viafb_set_start_addr();
			viafb_set_iga_path();
		}
		need_set_mode = 1;
	}

	viaparinfo->duoview = is_duoview();

	if (!need_set_mode) {
		;
	} else {
		viafb_set_iga_path();
		viafb_set_par(info);
	}
	return true;
}

static void retrieve_device_setting(struct viafb_ioctl_setting
	*setting_info)
{

	/* get device status */
	if (viafb_CRT_ON == 1)
		setting_info->device_status = CRT_Device;
	if (viafb_DVI_ON == 1)
		setting_info->device_status |= DVI_Device;
	if (viafb_LCD_ON == 1)
		setting_info->device_status |= LCD_Device;
	if (viafb_LCD2_ON == 1)
		setting_info->device_status |= LCD2_Device;
	if ((viaparinfo->video_on_crt == 1) && (viafb_CRT_ON == 1)) {
		setting_info->video_device_status =
			viaparinfo->crt_setting_info->iga_path;
	} else if ((viaparinfo->video_on_dvi == 1) && (viafb_DVI_ON == 1)) {
		setting_info->video_device_status =
			viaparinfo->tmds_setting_info->iga_path;
	} else if ((viaparinfo->video_on_lcd == 1) && (viafb_LCD_ON == 1)) {
		setting_info->video_device_status =
			viaparinfo->lvds_setting_info->iga_path;
	} else {
		setting_info->video_device_status = 0;
	}

	setting_info->samm_status = viafb_SAMM_ON;
	setting_info->primary_device = get_primary_device();

	setting_info->first_dev_bpp = viafb_bpp;
	setting_info->second_dev_bpp = viafb_bpp1;

	setting_info->first_dev_refresh = viafb_refresh;
	setting_info->second_dev_refresh = viafb_refresh1;

	setting_info->first_dev_hor_res = viafb_hotplug_Xres;
	setting_info->first_dev_ver_res = viafb_hotplug_Yres;
	setting_info->second_dev_hor_res = viafb_second_xres;
	setting_info->second_dev_ver_res = viafb_second_yres;

	/* Get lcd attributes */
	setting_info->lcd_attributes.display_center = viafb_lcd_dsp_method;
	setting_info->lcd_attributes.panel_id = viafb_lcd_panel_id;
	setting_info->lcd_attributes.lcd_mode = viafb_lcd_mode;
}

static void parse_active_dev(void)
{
	viafb_CRT_ON = STATE_OFF;
	viafb_DVI_ON = STATE_OFF;
	viafb_LCD_ON = STATE_OFF;
	viafb_LCD2_ON = STATE_OFF;
	/* 1. Modify the active status of devices. */
	/* 2. Keep the order of devices, so we can set corresponding
	   IGA path to devices in SAMM case. */
	/*    Note: The previous of active_dev is primary device,
	   and the following is secondary device. */
	if (!strncmp(viafb_active_dev, "CRT+DVI", 7)) {
		/* CRT+DVI */
		viafb_CRT_ON = STATE_ON;
		viafb_DVI_ON = STATE_ON;
		viafb_primary_dev = CRT_Device;
	} else if (!strncmp(viafb_active_dev, "DVI+CRT", 7)) {
		/* DVI+CRT */
		viafb_CRT_ON = STATE_ON;
		viafb_DVI_ON = STATE_ON;
		viafb_primary_dev = DVI_Device;
	} else if (!strncmp(viafb_active_dev, "CRT+LCD", 7)) {
		/* CRT+LCD */
		viafb_CRT_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = CRT_Device;
	} else if (!strncmp(viafb_active_dev, "LCD+CRT", 7)) {
		/* LCD+CRT */
		viafb_CRT_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strncmp(viafb_active_dev, "DVI+LCD", 7)) {
		/* DVI+LCD */
		viafb_DVI_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = DVI_Device;
	} else if (!strncmp(viafb_active_dev, "LCD+DVI", 7)) {
		/* LCD+DVI */
		viafb_DVI_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strncmp(viafb_active_dev, "LCD+LCD2", 8)) {
		viafb_LCD_ON = STATE_ON;
		viafb_LCD2_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strncmp(viafb_active_dev, "LCD2+LCD", 8)) {
		viafb_LCD_ON = STATE_ON;
		viafb_LCD2_ON = STATE_ON;
		viafb_primary_dev = LCD2_Device;
	} else if (!strncmp(viafb_active_dev, "CRT", 3)) {
		/* CRT only */
		viafb_CRT_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else if (!strncmp(viafb_active_dev, "DVI", 3)) {
		/* DVI only */
		viafb_DVI_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else if (!strncmp(viafb_active_dev, "LCD", 3)) {
		/* LCD only */
		viafb_LCD_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else {
		viafb_CRT_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	}
	viaparinfo->duoview = is_duoview();
}

static void parse_video_dev(void)
{
	viaparinfo->video_on_crt = STATE_OFF;
	viaparinfo->video_on_dvi = STATE_OFF;
	viaparinfo->video_on_lcd = STATE_OFF;

	if (!strncmp(viafb_video_dev, "CRT", 3)) {
		/* Video on CRT */
		viaparinfo->video_on_crt = STATE_ON;
	} else if (!strncmp(viafb_video_dev, "DVI", 3)) {
		/* Video on DVI */
		viaparinfo->video_on_dvi = STATE_ON;
	} else if (!strncmp(viafb_video_dev, "LCD", 3)) {
		/* Video on LCD */
		viaparinfo->video_on_lcd = STATE_ON;
	}
}

static int parse_port(char *opt_str, int *output_interface)
{
	if (!strncmp(opt_str, "DVP0", 4))
		*output_interface = INTERFACE_DVP0;
	else if (!strncmp(opt_str, "DVP1", 4))
		*output_interface = INTERFACE_DVP1;
	else if (!strncmp(opt_str, "DFP_HIGHLOW", 11))
		*output_interface = INTERFACE_DFP;
	else if (!strncmp(opt_str, "DFP_HIGH", 8))
		*output_interface = INTERFACE_DFP_HIGH;
	else if (!strncmp(opt_str, "DFP_LOW", 7))
		*output_interface = INTERFACE_DFP_LOW;
	else
		*output_interface = INTERFACE_NONE;
	return 0;
}

static void parse_lcd_port(void)
{
	parse_port(viafb_lcd_port, &viaparinfo->chip_info->lvds_chip_info.
		output_interface);
	/*Initialize to avoid unexpected behavior */
	viaparinfo->chip_info->lvds_chip_info2.output_interface =
	INTERFACE_NONE;

	DEBUG_MSG(KERN_INFO "parse_lcd_port: viafb_lcd_port:%s,interface:%d\n",
		  viafb_lcd_port, viaparinfo->chip_info->lvds_chip_info.
		  output_interface);
}

static void parse_dvi_port(void)
{
	parse_port(viafb_dvi_port, &viaparinfo->chip_info->tmds_chip_info.
		output_interface);

	DEBUG_MSG(KERN_INFO "parse_dvi_port: viafb_dvi_port:%s,interface:%d\n",
		  viafb_dvi_port, viaparinfo->chip_info->tmds_chip_info.
		  output_interface);
}

/*
 * The proc filesystem read/write function, a simple proc implement to
 * get/set the value of DPA  DVP0,   DVP0DataDriving,  DVP0ClockDriving, DVP1,
 * DVP1Driving, DFPHigh, DFPLow CR96,   SR2A[5], SR1B[1], SR2A[4], SR1E[2],
 * CR9B,    SR65,    CR97,    CR99
 */
static int viafb_dvp0_proc_read(char *buf, char **start, off_t offset,
int count, int *eof, void *data)
{
	int len = 0;
	u8 dvp0_data_dri = 0, dvp0_clk_dri = 0, dvp0 = 0;
	dvp0_data_dri =
	    (viafb_read_reg(VIASR, SR2A) & BIT5) >> 4 |
	    (viafb_read_reg(VIASR, SR1B) & BIT1) >> 1;
	dvp0_clk_dri =
	    (viafb_read_reg(VIASR, SR2A) & BIT4) >> 3 |
	    (viafb_read_reg(VIASR, SR1E) & BIT2) >> 2;
	dvp0 = viafb_read_reg(VIACR, CR96) & 0x0f;
	len +=
	    sprintf(buf + len, "%x %x %x\n", dvp0, dvp0_data_dri, dvp0_clk_dri);
	*eof = 1;		/*Inform kernel end of data */
	return len;
}
static int viafb_dvp0_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[20], *value, *pbuf;
	u8 reg_val = 0;
	unsigned long length, i;
	if (count < 1)
		return -EINVAL;
	length = count > 20 ? 20 : count;
	if (copy_from_user(&buf[0], buffer, length))
		return -EFAULT;
	buf[length - 1] = '\0';	/*Ensure end string */
	pbuf = &buf[0];
	for (i = 0; i < 3; i++) {
		value = strsep(&pbuf, " ");
		if (value != NULL) {
			strict_strtoul(value, 0, (unsigned long *)&reg_val);
			DEBUG_MSG(KERN_INFO "DVP0:reg_val[%l]=:%x\n", i,
				  reg_val);
			switch (i) {
			case 0:
				viafb_write_reg_mask(CR96, VIACR,
					reg_val, 0x0f);
				break;
			case 1:
				viafb_write_reg_mask(SR2A, VIASR,
					reg_val << 4, BIT5);
				viafb_write_reg_mask(SR1B, VIASR,
					reg_val << 1, BIT1);
				break;
			case 2:
				viafb_write_reg_mask(SR2A, VIASR,
					reg_val << 3, BIT4);
				viafb_write_reg_mask(SR1E, VIASR,
					reg_val << 2, BIT2);
				break;
			default:
				break;
			}
		} else {
			break;
		}
	}
	return count;
}
static int viafb_dvp1_proc_read(char *buf, char **start, off_t offset,
	int count, int *eof, void *data)
{
	int len = 0;
	u8 dvp1 = 0, dvp1_data_dri = 0, dvp1_clk_dri = 0;
	dvp1 = viafb_read_reg(VIACR, CR9B) & 0x0f;
	dvp1_data_dri = (viafb_read_reg(VIASR, SR65) & 0x0c) >> 2;
	dvp1_clk_dri = viafb_read_reg(VIASR, SR65) & 0x03;
	len +=
	    sprintf(buf + len, "%x %x %x\n", dvp1, dvp1_data_dri, dvp1_clk_dri);
	*eof = 1;		/*Inform kernel end of data */
	return len;
}
static int viafb_dvp1_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[20], *value, *pbuf;
	u8 reg_val = 0;
	unsigned long length, i;
	if (count < 1)
		return -EINVAL;
	length = count > 20 ? 20 : count;
	if (copy_from_user(&buf[0], buffer, length))
		return -EFAULT;
	buf[length - 1] = '\0';	/*Ensure end string */
	pbuf = &buf[0];
	for (i = 0; i < 3; i++) {
		value = strsep(&pbuf, " ");
		if (value != NULL) {
			strict_strtoul(value, 0, (unsigned long *)&reg_val);
			switch (i) {
			case 0:
				viafb_write_reg_mask(CR9B, VIACR,
					reg_val, 0x0f);
				break;
			case 1:
				viafb_write_reg_mask(SR65, VIASR,
					reg_val << 2, 0x0c);
				break;
			case 2:
				viafb_write_reg_mask(SR65, VIASR,
					reg_val, 0x03);
				break;
			default:
				break;
			}
		} else {
			break;
		}
	}
	return count;
}

static int viafb_dfph_proc_read(char *buf, char **start, off_t offset,
	int count, int *eof, void *data)
{
	int len = 0;
	u8 dfp_high = 0;
	dfp_high = viafb_read_reg(VIACR, CR97) & 0x0f;
	len += sprintf(buf + len, "%x\n", dfp_high);
	*eof = 1;		/*Inform kernel end of data */
	return len;
}
static int viafb_dfph_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[20];
	u8 reg_val = 0;
	unsigned long length;
	if (count < 1)
		return -EINVAL;
	length = count > 20 ? 20 : count;
	if (copy_from_user(&buf[0], buffer, length))
		return -EFAULT;
	buf[length - 1] = '\0';	/*Ensure end string */
	strict_strtoul(&buf[0], 0, (unsigned long *)&reg_val);
	viafb_write_reg_mask(CR97, VIACR, reg_val, 0x0f);
	return count;
}
static int viafb_dfpl_proc_read(char *buf, char **start, off_t offset,
	int count, int *eof, void *data)
{
	int len = 0;
	u8 dfp_low = 0;
	dfp_low = viafb_read_reg(VIACR, CR99) & 0x0f;
	len += sprintf(buf + len, "%x\n", dfp_low);
	*eof = 1;		/*Inform kernel end of data */
	return len;
}
static int viafb_dfpl_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[20];
	u8 reg_val = 0;
	unsigned long length;
	if (count < 1)
		return -EINVAL;
	length = count > 20 ? 20 : count;
	if (copy_from_user(&buf[0], buffer, length))
		return -EFAULT;
	buf[length - 1] = '\0';	/*Ensure end string */
	strict_strtoul(&buf[0], 0, (unsigned long *)&reg_val);
	viafb_write_reg_mask(CR99, VIACR, reg_val, 0x0f);
	return count;
}
static int viafb_vt1636_proc_read(char *buf, char **start,
	off_t offset, int count, int *eof, void *data)
{
	int len = 0;
	u8 vt1636_08 = 0, vt1636_09 = 0;
	switch (viaparinfo->chip_info->lvds_chip_info.lvds_chip_name) {
	case VT1636_LVDS:
		vt1636_08 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info,
		    &viaparinfo->chip_info->lvds_chip_info, 0x08) & 0x0f;
		vt1636_09 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info,
		    &viaparinfo->chip_info->lvds_chip_info, 0x09) & 0x1f;
		len += sprintf(buf + len, "%x %x\n", vt1636_08, vt1636_09);
		break;
	default:
		break;
	}
	switch (viaparinfo->chip_info->lvds_chip_info2.lvds_chip_name) {
	case VT1636_LVDS:
		vt1636_08 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info2,
			&viaparinfo->chip_info->lvds_chip_info2, 0x08) & 0x0f;
		vt1636_09 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info2,
			&viaparinfo->chip_info->lvds_chip_info2, 0x09) & 0x1f;
		len += sprintf(buf + len, " %x %x\n", vt1636_08, vt1636_09);
		break;
	default:
		break;
	}
	*eof = 1;		/*Inform kernel end of data */
	return len;
}
static int viafb_vt1636_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[30], *value, *pbuf;
	struct IODATA reg_val;
	unsigned long length, i;
	if (count < 1)
		return -EINVAL;
	length = count > 30 ? 30 : count;
	if (copy_from_user(&buf[0], buffer, length))
		return -EFAULT;
	buf[length - 1] = '\0';	/*Ensure end string */
	pbuf = &buf[0];
	switch (viaparinfo->chip_info->lvds_chip_info.lvds_chip_name) {
	case VT1636_LVDS:
		for (i = 0; i < 2; i++) {
			value = strsep(&pbuf, " ");
			if (value != NULL) {
				strict_strtoul(value, 0,
					(unsigned long *)&reg_val.Data);
				switch (i) {
				case 0:
					reg_val.Index = 0x08;
					reg_val.Mask = 0x0f;
					viafb_gpio_i2c_write_mask_lvds
					    (viaparinfo->lvds_setting_info,
					    &viaparinfo->
					    chip_info->lvds_chip_info,
					     reg_val);
					break;
				case 1:
					reg_val.Index = 0x09;
					reg_val.Mask = 0x1f;
					viafb_gpio_i2c_write_mask_lvds
					    (viaparinfo->lvds_setting_info,
					    &viaparinfo->
					    chip_info->lvds_chip_info,
					     reg_val);
					break;
				default:
					break;
				}
			} else {
				break;
			}
		}
		break;
	default:
		break;
	}
	switch (viaparinfo->chip_info->lvds_chip_info2.lvds_chip_name) {
	case VT1636_LVDS:
		for (i = 0; i < 2; i++) {
			value = strsep(&pbuf, " ");
			if (value != NULL) {
				strict_strtoul(value, 0,
					(unsigned long *)&reg_val.Data);
				switch (i) {
				case 0:
					reg_val.Index = 0x08;
					reg_val.Mask = 0x0f;
					viafb_gpio_i2c_write_mask_lvds
					    (viaparinfo->lvds_setting_info2,
					    &viaparinfo->
					    chip_info->lvds_chip_info2,
					     reg_val);
					break;
				case 1:
					reg_val.Index = 0x09;
					reg_val.Mask = 0x1f;
					viafb_gpio_i2c_write_mask_lvds
					    (viaparinfo->lvds_setting_info2,
					    &viaparinfo->
					    chip_info->lvds_chip_info2,
					     reg_val);
					break;
				default:
					break;
				}
			} else {
				break;
			}
		}
		break;
	default:
		break;
	}
	return count;
}

static void viafb_init_proc(struct proc_dir_entry **viafb_entry)
{
	struct proc_dir_entry *entry;
	*viafb_entry = proc_mkdir("viafb", NULL);
	if (viafb_entry) {
		entry = create_proc_entry("dvp0", 0, *viafb_entry);
		if (entry) {
			entry->read_proc = viafb_dvp0_proc_read;
			entry->write_proc = viafb_dvp0_proc_write;
		}
		entry = create_proc_entry("dvp1", 0, *viafb_entry);
		if (entry) {
			entry->read_proc = viafb_dvp1_proc_read;
			entry->write_proc = viafb_dvp1_proc_write;
		}
		entry = create_proc_entry("dfph", 0, *viafb_entry);
		if (entry) {
			entry->read_proc = viafb_dfph_proc_read;
			entry->write_proc = viafb_dfph_proc_write;
		}
		entry = create_proc_entry("dfpl", 0, *viafb_entry);
		if (entry) {
			entry->read_proc = viafb_dfpl_proc_read;
			entry->write_proc = viafb_dfpl_proc_write;
		}
		if (VT1636_LVDS == viaparinfo->chip_info->lvds_chip_info.
			lvds_chip_name || VT1636_LVDS ==
		    viaparinfo->chip_info->lvds_chip_info2.lvds_chip_name) {
			entry = create_proc_entry("vt1636", 0, *viafb_entry);
			if (entry) {
				entry->read_proc = viafb_vt1636_proc_read;
				entry->write_proc = viafb_vt1636_proc_write;
			}
		}

	}
}
static void viafb_remove_proc(struct proc_dir_entry *viafb_entry)
{
	/* no problem if it was not registered */
	remove_proc_entry("dvp0", viafb_entry);/* parent dir */
	remove_proc_entry("dvp1", viafb_entry);
	remove_proc_entry("dfph", viafb_entry);
	remove_proc_entry("dfpl", viafb_entry);
	remove_proc_entry("vt1636", viafb_entry);
	remove_proc_entry("vt1625", viafb_entry);
	remove_proc_entry("viafb", NULL);
}

static int __devinit via_pci_probe(void)
{
	unsigned long default_xres, default_yres;
	char *tmpc, *tmpm;
	char *tmpc_sec, *tmpm_sec;
	int vmode_index;
	u32 tmds_length, lvds_length, crt_length, chip_length, viafb_par_length;

	DEBUG_MSG(KERN_INFO "VIAFB PCI Probe!!\n");

	viafb_par_length = ALIGN(sizeof(struct viafb_par), BITS_PER_LONG/8);
	tmds_length = ALIGN(sizeof(struct tmds_setting_information),
		BITS_PER_LONG/8);
	lvds_length = ALIGN(sizeof(struct lvds_setting_information),
		BITS_PER_LONG/8);
	crt_length = ALIGN(sizeof(struct lvds_setting_information),
		BITS_PER_LONG/8);
	chip_length = ALIGN(sizeof(struct chip_information), BITS_PER_LONG/8);

	/* Allocate fb_info and ***_par here, also including some other needed
	 * variables
	*/
	viafbinfo = framebuffer_alloc(viafb_par_length + 2 * lvds_length +
	tmds_length + crt_length + chip_length, NULL);
	if (!viafbinfo) {
		printk(KERN_ERR"Could not allocate memory for viafb_info.\n");
		return -ENODEV;
	}

	viaparinfo = (struct viafb_par *)viafbinfo->par;
	viaparinfo->tmds_setting_info = (struct tmds_setting_information *)
		((unsigned long)viaparinfo + viafb_par_length);
	viaparinfo->lvds_setting_info = (struct lvds_setting_information *)
		((unsigned long)viaparinfo->tmds_setting_info + tmds_length);
	viaparinfo->lvds_setting_info2 = (struct lvds_setting_information *)
		((unsigned long)viaparinfo->lvds_setting_info + lvds_length);
	viaparinfo->crt_setting_info = (struct crt_setting_information *)
		((unsigned long)viaparinfo->lvds_setting_info2 + lvds_length);
	viaparinfo->chip_info = (struct chip_information *)
		((unsigned long)viaparinfo->crt_setting_info + crt_length);

	if (viafb_dual_fb)
		viafb_SAMM_ON = 1;
	parse_active_dev();
	parse_video_dev();
	parse_lcd_port();
	parse_dvi_port();

	/* for dual-fb must viafb_SAMM_ON=1 and viafb_dual_fb=1 */
	if (!viafb_SAMM_ON)
		viafb_dual_fb = 0;

	/* Set up I2C bus stuff */
	viafb_create_i2c_bus(viaparinfo);

	viafb_init_chip_info();
	viafb_get_fb_info(&viaparinfo->fbmem, &viaparinfo->memsize);
	viaparinfo->fbmem_free = viaparinfo->memsize;
	viaparinfo->fbmem_used = 0;
	viaparinfo->fbmem_virt = ioremap_nocache(viaparinfo->fbmem,
		viaparinfo->memsize);
	viafbinfo->screen_base = (char *)viaparinfo->fbmem_virt;

	if (!viaparinfo->fbmem_virt) {
		printk(KERN_INFO "ioremap failed\n");
		return -1;
	}

	viafb_get_mmio_info(&viaparinfo->mmio_base, &viaparinfo->mmio_len);
	viaparinfo->io_virt = ioremap_nocache(viaparinfo->mmio_base,
		viaparinfo->mmio_len);

	viafbinfo->node = 0;
	viafbinfo->fbops = &viafb_ops;
	viafbinfo->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	viafbinfo->pseudo_palette = pseudo_pal;
	if (viafb_accel) {
		viafb_init_accel();
		viafb_init_2d_engine();
		viafb_hw_cursor_init();
	}

	if (viafb_second_size && (viafb_second_size < 8)) {
		viafb_second_offset = viaparinfo->fbmem_free -
			viafb_second_size * 1024 * 1024;
	} else {
		viafb_second_size = 8;
		viafb_second_offset = viaparinfo->fbmem_free -
			viafb_second_size * 1024 * 1024;
	}

	viafb_FB_MM = viaparinfo->fbmem_virt;
	tmpm = viafb_mode;
	tmpc = strsep(&tmpm, "x");
	strict_strtoul(tmpc, 0, &default_xres);
	strict_strtoul(tmpm, 0, &default_yres);

	vmode_index = viafb_get_mode_index(default_xres, default_yres);
	DEBUG_MSG(KERN_INFO "0->index=%d\n", vmode_index);

	if (viafb_SAMM_ON == 1) {
		if (strcmp(viafb_mode, viafb_mode1)) {
			tmpm_sec = viafb_mode1;
			tmpc_sec = strsep(&tmpm_sec, "x");
			strict_strtoul(tmpc_sec, 0,
				(unsigned long *)&viafb_second_xres);
			strict_strtoul(tmpm_sec, 0,
				(unsigned long *)&viafb_second_yres);
		} else {
			viafb_second_xres = default_xres;
			viafb_second_yres = default_yres;
		}
		if (0 == viafb_second_virtual_xres) {
			switch (viafb_second_xres) {
			case 1400:
				viafb_second_virtual_xres = 1408;
				break;
			default:
				viafb_second_virtual_xres = viafb_second_xres;
				break;
			}
		}
		if (0 == viafb_second_virtual_yres)
			viafb_second_virtual_yres = viafb_second_yres;
	}

	switch (viafb_bpp) {
	case 0 ... 8:
		viafb_bpp = 8;
		break;
	case 9 ... 16:
		viafb_bpp = 16;
		break;
	case 17 ... 32:
		viafb_bpp = 32;
		break;
	default:
		viafb_bpp = 8;
	}
	default_var.xres = default_xres;
	default_var.yres = default_yres;
	switch (default_xres) {
	case 1400:
		default_var.xres_virtual = 1408;
		break;
	default:
		default_var.xres_virtual = default_xres;
		break;
	}
	default_var.yres_virtual = default_yres;
	default_var.bits_per_pixel = viafb_bpp;
	if (default_var.bits_per_pixel == 15)
		default_var.bits_per_pixel = 16;
	default_var.pixclock =
	    viafb_get_pixclock(default_xres, default_yres, viafb_refresh);
	default_var.left_margin = (default_xres >> 3) & 0xf8;
	default_var.right_margin = 32;
	default_var.upper_margin = 16;
	default_var.lower_margin = 4;
	default_var.hsync_len = default_var.left_margin;
	default_var.vsync_len = 4;
	default_var.accel_flags = 0;

	if (viafb_accel) {
		viafbinfo->flags |=
		    (FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
		     FBINFO_HWACCEL_IMAGEBLIT);
		default_var.accel_flags |= FB_ACCELF_TEXT;
	} else
		viafbinfo->flags |= FBINFO_HWACCEL_DISABLED;

	if (viafb_dual_fb) {
		viafbinfo1 = framebuffer_alloc(viafb_par_length, NULL);
		if (!viafbinfo1) {
			printk(KERN_ERR
			"allocate the second framebuffer struct error\n");
			framebuffer_release(viafbinfo);
			return -ENOMEM;
		}
		viaparinfo1 = viafbinfo1->par;
		memcpy(viaparinfo1, viaparinfo, viafb_par_length);
		viaparinfo1->memsize = viaparinfo->memsize -
			viafb_second_offset;
		viaparinfo->memsize = viafb_second_offset;
		viaparinfo1->fbmem_virt = viaparinfo->fbmem_virt +
			viafb_second_offset;
		viaparinfo1->fbmem = viaparinfo->fbmem + viafb_second_offset;

		viaparinfo1->fbmem_used = viaparinfo->fbmem_used;
		viaparinfo1->fbmem_free = viaparinfo1->memsize -
			viaparinfo1->fbmem_used;
		viaparinfo->fbmem_free = viaparinfo->memsize;
		viaparinfo->fbmem_used = 0;
		if (viafb_accel) {
			viaparinfo1->cursor_start =
			    viaparinfo->cursor_start - viafb_second_offset;
			viaparinfo1->VQ_start = viaparinfo->VQ_start -
				viafb_second_offset;
			viaparinfo1->VQ_end = viaparinfo->VQ_end -
				viafb_second_offset;
		}

		memcpy(viafbinfo1, viafbinfo, sizeof(struct fb_info));
		viafbinfo1->screen_base = viafbinfo->screen_base +
			viafb_second_offset;
		viafbinfo1->fix.smem_start = viaparinfo1->fbmem;
		viafbinfo1->fix.smem_len = viaparinfo1->fbmem_free;

		default_var.xres = viafb_second_xres;
		default_var.yres = viafb_second_yres;
		default_var.xres_virtual = viafb_second_virtual_xres;
		default_var.yres_virtual = viafb_second_virtual_yres;
		if (viafb_bpp1 != viafb_bpp)
			viafb_bpp1 = viafb_bpp;
		default_var.bits_per_pixel = viafb_bpp1;
		default_var.pixclock =
		    viafb_get_pixclock(viafb_second_xres, viafb_second_yres,
		    viafb_refresh);
		default_var.left_margin = (viafb_second_xres >> 3) & 0xf8;
		default_var.right_margin = 32;
		default_var.upper_margin = 16;
		default_var.lower_margin = 4;
		default_var.hsync_len = default_var.left_margin;
		default_var.vsync_len = 4;

		viafb_setup_fixinfo(&viafbinfo1->fix, viaparinfo1);
		viafb_check_var(&default_var, viafbinfo1);
		viafbinfo1->var = default_var;
		viafb_update_viafb_par(viafbinfo);
		viafb_update_fix(&viafbinfo1->fix, viafbinfo1);
	}

	viafb_setup_fixinfo(&viafbinfo->fix, viaparinfo);
	viafb_check_var(&default_var, viafbinfo);
	viafbinfo->var = default_var;
	viafb_update_viafb_par(viafbinfo);
	viafb_update_fix(&viafbinfo->fix, viafbinfo);
	default_var.activate = FB_ACTIVATE_NOW;
	fb_alloc_cmap(&viafbinfo->cmap, 256, 0);

	if (viafb_dual_fb && (viafb_primary_dev == LCD_Device)
	    && (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)) {
		if (register_framebuffer(viafbinfo1) < 0)
			return -EINVAL;
	}
	if (register_framebuffer(viafbinfo) < 0)
		return -EINVAL;

	if (viafb_dual_fb && ((viafb_primary_dev != LCD_Device)
			|| (viaparinfo->chip_info->gfx_chip_name !=
			UNICHROME_CLE266))) {
		if (register_framebuffer(viafbinfo1) < 0)
			return -EINVAL;
	}
	DEBUG_MSG(KERN_INFO "fb%d: %s frame buffer device %dx%d-%dbpp\n",
		  viafbinfo->node, viafbinfo->fix.id, default_var.xres,
		  default_var.yres, default_var.bits_per_pixel);

	viafb_init_proc(&viaparinfo->proc_entry);
	viafb_init_dac(IGA2);
	return 0;
}

static void __devexit via_pci_remove(void)
{
	DEBUG_MSG(KERN_INFO "via_pci_remove!\n");
	fb_dealloc_cmap(&viafbinfo->cmap);
	unregister_framebuffer(viafbinfo);
	if (viafb_dual_fb)
		unregister_framebuffer(viafbinfo1);
	iounmap((void *)viaparinfo->fbmem_virt);
	iounmap(viaparinfo->io_virt);

	viafb_delete_i2c_buss(viaparinfo);

	framebuffer_release(viafbinfo);
	if (viafb_dual_fb)
		framebuffer_release(viafbinfo1);

	viafb_remove_proc(viaparinfo->proc_entry);
}

#ifndef MODULE
static int __init viafb_setup(char *options)
{
	char *this_opt;
	DEBUG_MSG(KERN_INFO "viafb_setup!\n");

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "viafb_mode1=", 12))
			viafb_mode1 = kstrdup(this_opt + 12, GFP_KERNEL);
		else if (!strncmp(this_opt, "viafb_mode=", 11))
			viafb_mode = kstrdup(this_opt + 11, GFP_KERNEL);
		else if (!strncmp(this_opt, "viafb_bpp1=", 11))
			strict_strtoul(this_opt + 11, 0,
				(unsigned long *)&viafb_bpp1);
		else if (!strncmp(this_opt, "viafb_bpp=", 10))
			strict_strtoul(this_opt + 10, 0,
				(unsigned long *)&viafb_bpp);
		else if (!strncmp(this_opt, "viafb_refresh1=", 15))
			strict_strtoul(this_opt + 15, 0,
				(unsigned long *)&viafb_refresh1);
		else if (!strncmp(this_opt, "viafb_refresh=", 14))
			strict_strtoul(this_opt + 14, 0,
				(unsigned long *)&viafb_refresh);
		else if (!strncmp(this_opt, "viafb_lcd_dsp_method=", 21))
			strict_strtoul(this_opt + 21, 0,
				(unsigned long *)&viafb_lcd_dsp_method);
		else if (!strncmp(this_opt, "viafb_lcd_panel_id=", 19))
			strict_strtoul(this_opt + 19, 0,
				(unsigned long *)&viafb_lcd_panel_id);
		else if (!strncmp(this_opt, "viafb_accel=", 12))
			strict_strtoul(this_opt + 12, 0,
				(unsigned long *)&viafb_accel);
		else if (!strncmp(this_opt, "viafb_SAMM_ON=", 14))
			strict_strtoul(this_opt + 14, 0,
				(unsigned long *)&viafb_SAMM_ON);
		else if (!strncmp(this_opt, "viafb_active_dev=", 17))
			viafb_active_dev = kstrdup(this_opt + 17, GFP_KERNEL);
		else if (!strncmp(this_opt,
			"viafb_display_hardware_layout=", 30))
			strict_strtoul(this_opt + 30, 0,
			(unsigned long *)&viafb_display_hardware_layout);
		else if (!strncmp(this_opt, "viafb_second_size=", 18))
			strict_strtoul(this_opt + 18, 0,
				(unsigned long *)&viafb_second_size);
		else if (!strncmp(this_opt,
			"viafb_platform_epia_dvi=", 24))
			strict_strtoul(this_opt + 24, 0,
				(unsigned long *)&viafb_platform_epia_dvi);
		else if (!strncmp(this_opt,
			"viafb_device_lcd_dualedge=", 26))
			strict_strtoul(this_opt + 26, 0,
				(unsigned long *)&viafb_device_lcd_dualedge);
		else if (!strncmp(this_opt, "viafb_bus_width=", 16))
			strict_strtoul(this_opt + 16, 0,
				(unsigned long *)&viafb_bus_width);
		else if (!strncmp(this_opt, "viafb_lcd_mode=", 15))
			strict_strtoul(this_opt + 15, 0,
				(unsigned long *)&viafb_lcd_mode);
		else if (!strncmp(this_opt, "viafb_video_dev=", 16))
			viafb_video_dev = kstrdup(this_opt + 16, GFP_KERNEL);
		else if (!strncmp(this_opt, "viafb_lcd_port=", 15))
			viafb_lcd_port = kstrdup(this_opt + 15, GFP_KERNEL);
		else if (!strncmp(this_opt, "viafb_dvi_port=", 15))
			viafb_dvi_port = kstrdup(this_opt + 15, GFP_KERNEL);
	}
	return 0;
}
#endif

static int __init viafb_init(void)
{
#ifndef MODULE
	char *option = NULL;
	if (fb_get_options("viafb", &option))
		return -ENODEV;
	viafb_setup(option);
#endif
	printk(KERN_INFO
       "VIA Graphics Intergration Chipset framebuffer %d.%d initializing\n",
	       VERSION_MAJOR, VERSION_MINOR);
	return via_pci_probe();
}

static void __exit viafb_exit(void)
{
	DEBUG_MSG(KERN_INFO "viafb_exit!\n");
	via_pci_remove();
}

static struct fb_ops viafb_ops = {
	.owner = THIS_MODULE,
	.fb_open = viafb_open,
	.fb_release = viafb_release,
	.fb_check_var = viafb_check_var,
	.fb_set_par = viafb_set_par,
	.fb_setcolreg = viafb_setcolreg,
	.fb_pan_display = viafb_pan_display,
	.fb_blank = viafb_blank,
	.fb_fillrect = viafb_fillrect,
	.fb_copyarea = viafb_copyarea,
	.fb_imageblit = viafb_imageblit,
	.fb_cursor = viafb_cursor,
	.fb_ioctl = viafb_ioctl,
	.fb_sync = viafb_sync,
	.fb_setcmap = viafb_setcmap,
};

module_init(viafb_init);
module_exit(viafb_exit);

#ifdef MODULE
module_param(viafb_memsize, int, 0);

module_param(viafb_mode, charp, 0);
MODULE_PARM_DESC(viafb_mode, "Set resolution (default=640x480)");

module_param(viafb_mode1, charp, 0);
MODULE_PARM_DESC(viafb_mode1, "Set resolution (default=640x480)");

module_param(viafb_bpp, int, 0);
MODULE_PARM_DESC(viafb_bpp, "Set color depth (default=32bpp)");

module_param(viafb_bpp1, int, 0);
MODULE_PARM_DESC(viafb_bpp1, "Set color depth (default=32bpp)");

module_param(viafb_refresh, int, 0);
MODULE_PARM_DESC(viafb_refresh,
	"Set CRT viafb_refresh rate (default = 60)");

module_param(viafb_refresh1, int, 0);
MODULE_PARM_DESC(viafb_refresh1,
	"Set CRT refresh rate (default = 60)");

module_param(viafb_lcd_panel_id, int, 0);
MODULE_PARM_DESC(viafb_lcd_panel_id,
	"Set Flat Panel type(Default=1024x768)");

module_param(viafb_lcd_dsp_method, int, 0);
MODULE_PARM_DESC(viafb_lcd_dsp_method,
	"Set Flat Panel display scaling method.(Default=Expandsion)");

module_param(viafb_SAMM_ON, int, 0);
MODULE_PARM_DESC(viafb_SAMM_ON,
	"Turn on/off flag of SAMM(Default=OFF)");

module_param(viafb_accel, int, 0);
MODULE_PARM_DESC(viafb_accel,
	"Set 2D Hardware Acceleration.(Default = OFF)");

module_param(viafb_active_dev, charp, 0);
MODULE_PARM_DESC(viafb_active_dev, "Specify active devices.");

module_param(viafb_display_hardware_layout, int, 0);
MODULE_PARM_DESC(viafb_display_hardware_layout,
	"Display Hardware Layout (LCD Only, DVI Only...,etc)");

module_param(viafb_second_size, int, 0);
MODULE_PARM_DESC(viafb_second_size,
	"Set secondary device memory size");

module_param(viafb_dual_fb, int, 0);
MODULE_PARM_DESC(viafb_dual_fb,
	"Turn on/off flag of dual framebuffer devices.(Default = OFF)");

module_param(viafb_platform_epia_dvi, int, 0);
MODULE_PARM_DESC(viafb_platform_epia_dvi,
	"Turn on/off flag of DVI devices on EPIA board.(Default = OFF)");

module_param(viafb_device_lcd_dualedge, int, 0);
MODULE_PARM_DESC(viafb_device_lcd_dualedge,
	"Turn on/off flag of dual edge panel.(Default = OFF)");

module_param(viafb_bus_width, int, 0);
MODULE_PARM_DESC(viafb_bus_width,
	"Set bus width of panel.(Default = 12)");

module_param(viafb_lcd_mode, int, 0);
MODULE_PARM_DESC(viafb_lcd_mode,
	"Set Flat Panel mode(Default=OPENLDI)");

module_param(viafb_video_dev, charp, 0);
MODULE_PARM_DESC(viafb_video_dev, "Specify video devices.");

module_param(viafb_lcd_port, charp, 0);
MODULE_PARM_DESC(viafb_lcd_port, "Specify LCD output port.");

module_param(viafb_dvi_port, charp, 0);
MODULE_PARM_DESC(viafb_dvi_port, "Specify DVI output port.");

MODULE_LICENSE("GPL");
#endif
