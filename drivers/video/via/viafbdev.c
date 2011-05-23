/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
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
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/via-core.h>

#define _MASTER_FILE
#include "global.h"

static char *viafb_name = "Via";
static u32 pseudo_pal[17];

/* video mode */
static char *viafb_mode;
static char *viafb_mode1;
static int viafb_bpp = 32;
static int viafb_bpp1 = 32;

static unsigned int viafb_second_offset;
static int viafb_second_size;

static int viafb_accel = 1;

/* Added for specifying active devices.*/
static char *viafb_active_dev;

/*Added for specify lcd output port*/
static char *viafb_lcd_port = "";
static char *viafb_dvi_port = "";

static void retrieve_device_setting(struct viafb_ioctl_setting
	*setting_info);
static int viafb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info);

static struct fb_ops viafb_ops;

/* supported output devices on each IGP
 * only CX700, VX800, VX855, VX900 were documented
 * VIA_CRT should be everywhere
 * VIA_6C can be onle pre-CX700 (probably only on CLE266) as 6C is used for PLL
 * source selection on CX700 and later
 * K400 seems to support VIA_96, VIA_DVP1, VIA_LVDS{1,2} as in viamode.c
 */
static const u32 supported_odev_map[] = {
	[UNICHROME_CLE266]	= VIA_CRT | VIA_LDVP0 | VIA_LDVP1,
	[UNICHROME_K400]	= VIA_CRT | VIA_DVP0 | VIA_DVP1 | VIA_LVDS1
				| VIA_LVDS2,
	[UNICHROME_K800]	= VIA_CRT | VIA_DVP0 | VIA_DVP1 | VIA_LVDS1
				| VIA_LVDS2,
	[UNICHROME_PM800]	= VIA_CRT | VIA_DVP0 | VIA_DVP1 | VIA_LVDS1
				| VIA_LVDS2,
	[UNICHROME_CN700]	= VIA_CRT | VIA_DVP0 | VIA_DVP1 | VIA_LVDS1
				| VIA_LVDS2,
	[UNICHROME_CX700]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_CN750]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_K8M890]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_P4M890]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_P4M900]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_VX800]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_VX855]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
	[UNICHROME_VX900]	= VIA_CRT | VIA_DVP1 | VIA_LVDS1 | VIA_LVDS2,
};

static void viafb_fill_var_color_info(struct fb_var_screeninfo *var, u8 depth)
{
	var->grayscale = 0;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;
	var->nonstd = 0;
	switch (depth) {
	case 8:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->green.offset = 0;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case 15:
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 5;
		break;
	case 16:
		var->bits_per_pixel = 16;
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case 24:
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case 30:
		var->bits_per_pixel = 32;
		var->red.offset = 20;
		var->green.offset = 10;
		var->blue.offset = 0;
		var->red.length = 10;
		var->green.length = 10;
		var->blue.length = 10;
		break;
	}
}

static void viafb_update_fix(struct fb_info *info)
{
	u32 bpp = info->var.bits_per_pixel;

	info->fix.visual =
		bpp == 8 ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = (info->var.xres_virtual * bpp / 8 + 7) & ~7;
}

static void viafb_setup_fixinfo(struct fb_fix_screeninfo *fix,
	struct viafb_par *viaparinfo)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, viafb_name);

	fix->smem_start = viaparinfo->fbmem;
	fix->smem_len = viaparinfo->fbmem_free;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	fix->visual = FB_VISUAL_TRUECOLOR;

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

static inline int get_var_refresh(struct fb_var_screeninfo *var)
{
	u32 htotal, vtotal;

	htotal = var->left_margin + var->xres + var->right_margin
		+ var->hsync_len;
	vtotal = var->upper_margin + var->yres + var->lower_margin
		+ var->vsync_len;
	return PICOS2KHZ(var->pixclock) * 1000 / (htotal * vtotal);
}

static int viafb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	int depth, refresh;
	struct VideoModeTable *vmode_entry;
	struct viafb_par *ppar = info->par;
	u32 line;

	DEBUG_MSG(KERN_INFO "viafb_check_var!\n");
	/* Sanity check */
	/* HW neither support interlacte nor double-scaned mode */
	if (var->vmode & FB_VMODE_INTERLACED || var->vmode & FB_VMODE_DOUBLE)
		return -EINVAL;

	vmode_entry = viafb_get_mode(var->xres, var->yres);
	if (!vmode_entry) {
		DEBUG_MSG(KERN_INFO
			  "viafb: Mode %dx%dx%d not supported!!\n",
			  var->xres, var->yres, var->bits_per_pixel);
		return -EINVAL;
	}

	depth = fb_get_color_depth(var, &info->fix);
	if (!depth)
		depth = var->bits_per_pixel;

	if (depth < 0 || depth > 32)
		return -EINVAL;
	else if (!depth)
		depth = 24;
	else if (depth == 15 && viafb_dual_fb && ppar->iga_path == IGA1)
		depth = 15;
	else if (depth == 30)
		depth = 30;
	else if (depth <= 8)
		depth = 8;
	else if (depth <= 16)
		depth = 16;
	else
		depth = 24;

	viafb_fill_var_color_info(var, depth);
	line = (var->xres_virtual * var->bits_per_pixel / 8 + 7) & ~7;
	if (line * var->yres_virtual > ppar->memsize)
		return -EINVAL;

	/* Based on var passed in to calculate the refresh,
	 * because our driver use some modes special.
	 */
	refresh = viafb_get_refresh(var->xres, var->yres,
		get_var_refresh(var));

	/* Adjust var according to our driver's own table */
	viafb_fill_var_timing_info(var, refresh, vmode_entry);
	if (var->accel_flags & FB_ACCELF_TEXT &&
		!ppar->shared->vdev->engine_mmio)
		var->accel_flags = 0;

	return 0;
}

static int viafb_set_par(struct fb_info *info)
{
	struct viafb_par *viapar = info->par;
	struct VideoModeTable *vmode_entry, *vmode_entry1 = NULL;
	int refresh;
	DEBUG_MSG(KERN_INFO "viafb_set_par!\n");

	viafb_update_fix(info);
	viapar->depth = fb_get_color_depth(&info->var, &info->fix);
	viafb_update_device_setting(viafbinfo->var.xres, viafbinfo->var.yres,
		viafbinfo->var.bits_per_pixel, 0);

	vmode_entry = viafb_get_mode(viafbinfo->var.xres, viafbinfo->var.yres);
	if (viafb_dual_fb) {
		vmode_entry1 = viafb_get_mode(viafbinfo1->var.xres,
			viafbinfo1->var.yres);
		viafb_update_device_setting(viafbinfo1->var.xres,
			viafbinfo1->var.yres, viafbinfo1->var.bits_per_pixel,
			1);
	} else if (viafb_SAMM_ON == 1) {
		DEBUG_MSG(KERN_INFO
		"viafb_second_xres = %d, viafb_second_yres = %d, bpp = %d\n",
			  viafb_second_xres, viafb_second_yres, viafb_bpp1);
		vmode_entry1 = viafb_get_mode(viafb_second_xres,
			viafb_second_yres);

		viafb_update_device_setting(viafb_second_xres,
			viafb_second_yres, viafb_bpp1, 1);
	}

	refresh = viafb_get_refresh(info->var.xres, info->var.yres,
		get_var_refresh(&info->var));
	if (vmode_entry) {
		if (viafb_dual_fb && viapar->iga_path == IGA2) {
			viafb_bpp1 = info->var.bits_per_pixel;
			viafb_refresh1 = refresh;
		} else {
			viafb_bpp = info->var.bits_per_pixel;
			viafb_refresh = refresh;
		}

		if (info->var.accel_flags & FB_ACCELF_TEXT)
			info->flags &= ~FBINFO_HWACCEL_DISABLED;
		else
			info->flags |= FBINFO_HWACCEL_DISABLED;
		viafb_setmode(vmode_entry, info->var.bits_per_pixel,
			vmode_entry1, viafb_bpp1);
		viafb_pan_display(&info->var, info);
	}

	return 0;
}

/* Set one color register */
static int viafb_setcolreg(unsigned regno, unsigned red, unsigned green,
unsigned blue, unsigned transp, struct fb_info *info)
{
	struct viafb_par *viapar = info->par;
	u32 r, g, b;

	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		if (regno > 255)
			return -EINVAL;

		if (!viafb_dual_fb || viapar->iga_path == IGA1)
			viafb_set_primary_color_register(regno, red >> 8,
				green >> 8, blue >> 8);

		if (!viafb_dual_fb || viapar->iga_path == IGA2)
			viafb_set_secondary_color_register(regno, red >> 8,
				green >> 8, blue >> 8);
	} else {
		if (regno > 15)
			return -EINVAL;

		r = (red >> (16 - info->var.red.length))
			<< info->var.red.offset;
		b = (blue >> (16 - info->var.blue.length))
			<< info->var.blue.offset;
		g = (green >> (16 - info->var.green.length))
			<< info->var.green.offset;
		((u32 *) info->pseudo_palette)[regno] = r | g | b;
	}

	return 0;
}

static int viafb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct viafb_par *viapar = info->par;
	u32 vram_addr = (var->yoffset * var->xres_virtual + var->xoffset)
		* (var->bits_per_pixel / 8) + viapar->vram_addr;

	DEBUG_MSG(KERN_DEBUG "viafb_pan_display, address = %d\n", vram_addr);
	if (!viafb_dual_fb) {
		via_set_primary_address(vram_addr);
		via_set_secondary_address(vram_addr);
	} else if (viapar->iga_path == IGA1)
		via_set_primary_address(vram_addr);
	else
		via_set_secondary_address(vram_addr);

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
		via_set_state(VIA_CRT, VIA_STATE_ON);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		/* Screen: Off, HSync: Off, VSync: On */
		/* control CRT monitor power management */
		via_set_state(VIA_CRT, VIA_STATE_STANDBY);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		/* Screen: Off, HSync: On, VSync: Off */
		/* control CRT monitor power management */
		via_set_state(VIA_CRT, VIA_STATE_SUSPEND);
		break;
	case FB_BLANK_POWERDOWN:
		/* Screen: Off, HSync: Off, VSync: Off */
		/* control CRT monitor power management */
		via_set_state(VIA_CRT, VIA_STATE_OFF);
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

	DEBUG_MSG(KERN_INFO "viafb_ioctl: 0x%X !!\n", cmd);
	printk(KERN_WARNING "viafb_ioctl: Please avoid this interface as it is unstable and might change or vanish at any time!\n");
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
			via_set_state(VIA_CRT, VIA_STATE_ON);
		if (gpu32 & DVI_Device)
			viafb_dvi_enable();
		if (gpu32 & LCD_Device)
			viafb_lcd_enable();
		break;
	case VIAFB_TURN_OFF_OUTPUT_DEVICE:
		if (copy_from_user(&gpu32, argp, sizeof(gpu32)))
			return -EFAULT;
		if (gpu32 & CRT_Device)
			via_set_state(VIA_CRT, VIA_STATE_OFF);
		if (gpu32 & DVI_Device)
			viafb_dvi_disable();
		if (gpu32 & LCD_Device)
			viafb_lcd_disable();
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
		viafb_gamma_table = memdup_user(argp, 256 * sizeof(u32));
		if (IS_ERR(viafb_gamma_table))
			return PTR_ERR(viafb_gamma_table);
		viafb_set_gamma_table(viafb_bpp, viafb_gamma_table);
		kfree(viafb_gamma_table);
		break;

	case VIAFB_GET_GAMMA_LUT:
		viafb_gamma_table = kmalloc(256 * sizeof(u32), GFP_KERNEL);
		if (!viafb_gamma_table)
			return -ENOMEM;
		viafb_get_gamma_table(viafb_gamma_table);
		if (copy_to_user(argp, viafb_gamma_table,
			256 * sizeof(u32))) {
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
	struct viafb_par *viapar = info->par;
	struct viafb_shared *shared = viapar->shared;
	u32 fg_color;
	u8 rop;

	if (info->flags & FBINFO_HWACCEL_DISABLED || !shared->hw_bitblt) {
		cfb_fillrect(info, rect);
		return;
	}

	if (!rect->width || !rect->height)
		return;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR)
		fg_color = ((u32 *)info->pseudo_palette)[rect->color];
	else
		fg_color = rect->color;

	if (rect->rop == ROP_XOR)
		rop = 0x5A;
	else
		rop = 0xF0;

	DEBUG_MSG(KERN_DEBUG "viafb 2D engine: fillrect\n");
	if (shared->hw_bitblt(shared->vdev->engine_mmio, VIA_BITBLT_FILL,
		rect->width, rect->height, info->var.bits_per_pixel,
		viapar->vram_addr, info->fix.line_length, rect->dx, rect->dy,
		NULL, 0, 0, 0, 0, fg_color, 0, rop))
		cfb_fillrect(info, rect);
}

static void viafb_copyarea(struct fb_info *info,
	const struct fb_copyarea *area)
{
	struct viafb_par *viapar = info->par;
	struct viafb_shared *shared = viapar->shared;

	if (info->flags & FBINFO_HWACCEL_DISABLED || !shared->hw_bitblt) {
		cfb_copyarea(info, area);
		return;
	}

	if (!area->width || !area->height)
		return;

	DEBUG_MSG(KERN_DEBUG "viafb 2D engine: copyarea\n");
	if (shared->hw_bitblt(shared->vdev->engine_mmio, VIA_BITBLT_COLOR,
		area->width, area->height, info->var.bits_per_pixel,
		viapar->vram_addr, info->fix.line_length, area->dx, area->dy,
		NULL, viapar->vram_addr, info->fix.line_length,
		area->sx, area->sy, 0, 0, 0))
		cfb_copyarea(info, area);
}

static void viafb_imageblit(struct fb_info *info,
	const struct fb_image *image)
{
	struct viafb_par *viapar = info->par;
	struct viafb_shared *shared = viapar->shared;
	u32 fg_color = 0, bg_color = 0;
	u8 op;

	if (info->flags & FBINFO_HWACCEL_DISABLED || !shared->hw_bitblt ||
		(image->depth != 1 && image->depth != viapar->depth)) {
		cfb_imageblit(info, image);
		return;
	}

	if (image->depth == 1) {
		op = VIA_BITBLT_MONO;
		if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
			fg_color =
				((u32 *)info->pseudo_palette)[image->fg_color];
			bg_color =
				((u32 *)info->pseudo_palette)[image->bg_color];
		} else {
			fg_color = image->fg_color;
			bg_color = image->bg_color;
		}
	} else
		op = VIA_BITBLT_COLOR;

	DEBUG_MSG(KERN_DEBUG "viafb 2D engine: imageblit\n");
	if (shared->hw_bitblt(shared->vdev->engine_mmio, op,
		image->width, image->height, info->var.bits_per_pixel,
		viapar->vram_addr, info->fix.line_length, image->dx, image->dy,
		(u32 *)image->data, 0, 0, 0, 0, fg_color, bg_color, 0))
		cfb_imageblit(info, image);
}

static int viafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct viafb_par *viapar = info->par;
	void __iomem *engine = viapar->shared->vdev->engine_mmio;
	u32 temp, xx, yy, bg_color = 0, fg_color = 0,
		chip_name = viapar->shared->chip_info.gfx_chip_name;
	int i, j = 0, cur_size = 64;

	if (info->flags & FBINFO_HWACCEL_DISABLED || info != viafbinfo)
		return -ENODEV;

	/* LCD ouput does not support hw cursors (at least on VN896) */
	if ((chip_name == UNICHROME_CLE266 && viapar->iga_path == IGA2) ||
		viafb_LCD_ON)
		return -ENODEV;

	viafb_show_hw_cursor(info, HW_Cursor_OFF);

	if (cursor->set & FB_CUR_SETHOT) {
		temp = (cursor->hot.x << 16) + cursor->hot.y;
		writel(temp, engine + VIA_REG_CURSOR_ORG);
	}

	if (cursor->set & FB_CUR_SETPOS) {
		yy = cursor->image.dy - info->var.yoffset;
		xx = cursor->image.dx - info->var.xoffset;
		temp = yy & 0xFFFF;
		temp |= (xx << 16);
		writel(temp, engine + VIA_REG_CURSOR_POS);
	}

	if (cursor->image.width <= 32 && cursor->image.height <= 32)
		cur_size = 32;
	else if (cursor->image.width <= 64 && cursor->image.height <= 64)
		cur_size = 64;
	else {
		printk(KERN_WARNING "viafb_cursor: The cursor is too large "
			"%dx%d", cursor->image.width, cursor->image.height);
		return -ENXIO;
	}

	if (cursor->set & FB_CUR_SETSIZE) {
		temp = readl(engine + VIA_REG_CURSOR_MODE);
		if (cur_size == 32)
			temp |= 0x2;
		else
			temp &= ~0x2;

		writel(temp, engine + VIA_REG_CURSOR_MODE);
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		fg_color = cursor->image.fg_color;
		bg_color = cursor->image.bg_color;
		if (chip_name == UNICHROME_CX700 ||
			chip_name == UNICHROME_VX800 ||
			chip_name == UNICHROME_VX855 ||
			chip_name == UNICHROME_VX900) {
			fg_color =
				((info->cmap.red[fg_color] & 0xFFC0) << 14) |
				((info->cmap.green[fg_color] & 0xFFC0) << 4) |
				((info->cmap.blue[fg_color] & 0xFFC0) >> 6);
			bg_color =
				((info->cmap.red[bg_color] & 0xFFC0) << 14) |
				((info->cmap.green[bg_color] & 0xFFC0) << 4) |
				((info->cmap.blue[bg_color] & 0xFFC0) >> 6);
		} else {
			fg_color =
				((info->cmap.red[fg_color] & 0xFF00) << 8) |
				(info->cmap.green[fg_color] & 0xFF00) |
				((info->cmap.blue[fg_color] & 0xFF00) >> 8);
			bg_color =
				((info->cmap.red[bg_color] & 0xFF00) << 8) |
				(info->cmap.green[bg_color] & 0xFF00) |
				((info->cmap.blue[bg_color] & 0xFF00) >> 8);
		}

		writel(bg_color, engine + VIA_REG_CURSOR_BG);
		writel(fg_color, engine + VIA_REG_CURSOR_FG);
	}

	if (cursor->set & FB_CUR_SETSHAPE) {
		struct {
			u8 data[CURSOR_SIZE];
			u32 bak[CURSOR_SIZE / 4];
		} *cr_data = kzalloc(sizeof(*cr_data), GFP_ATOMIC);
		int size = ((cursor->image.width + 7) >> 3) *
			cursor->image.height;

		if (!cr_data)
			return -ENOMEM;

		if (cur_size == 32) {
			for (i = 0; i < (CURSOR_SIZE / 4); i++) {
				cr_data->bak[i] = 0x0;
				cr_data->bak[i + 1] = 0xFFFFFFFF;
				i += 1;
			}
		} else {
			for (i = 0; i < (CURSOR_SIZE / 4); i++) {
				cr_data->bak[i] = 0x0;
				cr_data->bak[i + 1] = 0x0;
				cr_data->bak[i + 2] = 0xFFFFFFFF;
				cr_data->bak[i + 3] = 0xFFFFFFFF;
				i += 3;
			}
		}

		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				cr_data->data[i] = cursor->mask[i];
			break;
		case ROP_COPY:

			for (i = 0; i < size; i++)
				cr_data->data[i] = cursor->mask[i];
			break;
		default:
			break;
		}

		if (cur_size == 32) {
			for (i = 0; i < size; i++) {
				cr_data->bak[j] = (u32) cr_data->data[i];
				cr_data->bak[j + 1] = ~cr_data->bak[j];
				j += 2;
			}
		} else {
			for (i = 0; i < size; i++) {
				cr_data->bak[j] = (u32) cr_data->data[i];
				cr_data->bak[j + 1] = 0x0;
				cr_data->bak[j + 2] = ~cr_data->bak[j];
				cr_data->bak[j + 3] = ~cr_data->bak[j + 1];
				j += 4;
			}
		}

		memcpy_toio(viafbinfo->screen_base + viapar->shared->
			cursor_vram_addr, cr_data->bak, CURSOR_SIZE);
		kfree(cr_data);
	}

	if (cursor->enable)
		viafb_show_hw_cursor(info, HW_Cursor_ON);

	return 0;
}

static int viafb_sync(struct fb_info *info)
{
	if (!(info->flags & FBINFO_HWACCEL_DISABLED))
		viafb_wait_engine_idle(info);
	return 0;
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

static int __init parse_active_dev(void)
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
	if (!viafb_active_dev) {
		viafb_CRT_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else if (!strcmp(viafb_active_dev, "CRT+DVI")) {
		/* CRT+DVI */
		viafb_CRT_ON = STATE_ON;
		viafb_DVI_ON = STATE_ON;
		viafb_primary_dev = CRT_Device;
	} else if (!strcmp(viafb_active_dev, "DVI+CRT")) {
		/* DVI+CRT */
		viafb_CRT_ON = STATE_ON;
		viafb_DVI_ON = STATE_ON;
		viafb_primary_dev = DVI_Device;
	} else if (!strcmp(viafb_active_dev, "CRT+LCD")) {
		/* CRT+LCD */
		viafb_CRT_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = CRT_Device;
	} else if (!strcmp(viafb_active_dev, "LCD+CRT")) {
		/* LCD+CRT */
		viafb_CRT_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strcmp(viafb_active_dev, "DVI+LCD")) {
		/* DVI+LCD */
		viafb_DVI_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = DVI_Device;
	} else if (!strcmp(viafb_active_dev, "LCD+DVI")) {
		/* LCD+DVI */
		viafb_DVI_ON = STATE_ON;
		viafb_LCD_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strcmp(viafb_active_dev, "LCD+LCD2")) {
		viafb_LCD_ON = STATE_ON;
		viafb_LCD2_ON = STATE_ON;
		viafb_primary_dev = LCD_Device;
	} else if (!strcmp(viafb_active_dev, "LCD2+LCD")) {
		viafb_LCD_ON = STATE_ON;
		viafb_LCD2_ON = STATE_ON;
		viafb_primary_dev = LCD2_Device;
	} else if (!strcmp(viafb_active_dev, "CRT")) {
		/* CRT only */
		viafb_CRT_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else if (!strcmp(viafb_active_dev, "DVI")) {
		/* DVI only */
		viafb_DVI_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else if (!strcmp(viafb_active_dev, "LCD")) {
		/* LCD only */
		viafb_LCD_ON = STATE_ON;
		viafb_SAMM_ON = STATE_OFF;
	} else
		return -EINVAL;

	return 0;
}

static int __devinit parse_port(char *opt_str, int *output_interface)
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

static void __devinit parse_lcd_port(void)
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

static void __devinit parse_dvi_port(void)
{
	parse_port(viafb_dvi_port, &viaparinfo->chip_info->tmds_chip_info.
		output_interface);

	DEBUG_MSG(KERN_INFO "parse_dvi_port: viafb_dvi_port:%s,interface:%d\n",
		  viafb_dvi_port, viaparinfo->chip_info->tmds_chip_info.
		  output_interface);
}

#ifdef CONFIG_FB_VIA_DIRECT_PROCFS

/*
 * The proc filesystem read/write function, a simple proc implement to
 * get/set the value of DPA  DVP0,   DVP0DataDriving,  DVP0ClockDriving, DVP1,
 * DVP1Driving, DFPHigh, DFPLow CR96,   SR2A[5], SR1B[1], SR2A[4], SR1E[2],
 * CR9B,    SR65,    CR97,    CR99
 */
static int viafb_dvp0_proc_show(struct seq_file *m, void *v)
{
	u8 dvp0_data_dri = 0, dvp0_clk_dri = 0, dvp0 = 0;
	dvp0_data_dri =
	    (viafb_read_reg(VIASR, SR2A) & BIT5) >> 4 |
	    (viafb_read_reg(VIASR, SR1B) & BIT1) >> 1;
	dvp0_clk_dri =
	    (viafb_read_reg(VIASR, SR2A) & BIT4) >> 3 |
	    (viafb_read_reg(VIASR, SR1E) & BIT2) >> 2;
	dvp0 = viafb_read_reg(VIACR, CR96) & 0x0f;
	seq_printf(m, "%x %x %x\n", dvp0, dvp0_data_dri, dvp0_clk_dri);
	return 0;
}

static int viafb_dvp0_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_dvp0_proc_show, NULL);
}

static ssize_t viafb_dvp0_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
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

static const struct file_operations viafb_dvp0_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_dvp0_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_dvp0_proc_write,
};

static int viafb_dvp1_proc_show(struct seq_file *m, void *v)
{
	u8 dvp1 = 0, dvp1_data_dri = 0, dvp1_clk_dri = 0;
	dvp1 = viafb_read_reg(VIACR, CR9B) & 0x0f;
	dvp1_data_dri = (viafb_read_reg(VIASR, SR65) & 0x0c) >> 2;
	dvp1_clk_dri = viafb_read_reg(VIASR, SR65) & 0x03;
	seq_printf(m, "%x %x %x\n", dvp1, dvp1_data_dri, dvp1_clk_dri);
	return 0;
}

static int viafb_dvp1_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_dvp1_proc_show, NULL);
}

static ssize_t viafb_dvp1_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
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

static const struct file_operations viafb_dvp1_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_dvp1_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_dvp1_proc_write,
};

static int viafb_dfph_proc_show(struct seq_file *m, void *v)
{
	u8 dfp_high = 0;
	dfp_high = viafb_read_reg(VIACR, CR97) & 0x0f;
	seq_printf(m, "%x\n", dfp_high);
	return 0;
}

static int viafb_dfph_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_dfph_proc_show, NULL);
}

static ssize_t viafb_dfph_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
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

static const struct file_operations viafb_dfph_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_dfph_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_dfph_proc_write,
};

static int viafb_dfpl_proc_show(struct seq_file *m, void *v)
{
	u8 dfp_low = 0;
	dfp_low = viafb_read_reg(VIACR, CR99) & 0x0f;
	seq_printf(m, "%x\n", dfp_low);
	return 0;
}

static int viafb_dfpl_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_dfpl_proc_show, NULL);
}

static ssize_t viafb_dfpl_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
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

static const struct file_operations viafb_dfpl_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_dfpl_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_dfpl_proc_write,
};

static int viafb_vt1636_proc_show(struct seq_file *m, void *v)
{
	u8 vt1636_08 = 0, vt1636_09 = 0;
	switch (viaparinfo->chip_info->lvds_chip_info.lvds_chip_name) {
	case VT1636_LVDS:
		vt1636_08 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info,
		    &viaparinfo->chip_info->lvds_chip_info, 0x08) & 0x0f;
		vt1636_09 =
		    viafb_gpio_i2c_read_lvds(viaparinfo->lvds_setting_info,
		    &viaparinfo->chip_info->lvds_chip_info, 0x09) & 0x1f;
		seq_printf(m, "%x %x\n", vt1636_08, vt1636_09);
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
		seq_printf(m, " %x %x\n", vt1636_08, vt1636_09);
		break;
	default:
		break;
	}
	return 0;
}

static int viafb_vt1636_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_vt1636_proc_show, NULL);
}

static ssize_t viafb_vt1636_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
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

static const struct file_operations viafb_vt1636_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_vt1636_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_vt1636_proc_write,
};

#endif /* CONFIG_FB_VIA_DIRECT_PROCFS */

static int viafb_sup_odev_proc_show(struct seq_file *m, void *v)
{
	via_odev_to_seq(m, supported_odev_map[
		viaparinfo->shared->chip_info.gfx_chip_name]);
	return 0;
}

static int viafb_sup_odev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_sup_odev_proc_show, NULL);
}

static const struct file_operations viafb_sup_odev_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_sup_odev_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t odev_update(const char __user *buffer, size_t count, u32 *odev)
{
	char buf[64], *ptr = buf;
	u32 devices;
	bool add, sub;

	if (count < 1 || count > 63)
		return -EINVAL;
	if (copy_from_user(&buf[0], buffer, count))
		return -EFAULT;
	buf[count] = '\0';
	add = buf[0] == '+';
	sub = buf[0] == '-';
	if (add || sub)
		ptr++;
	devices = via_parse_odev(ptr, &ptr);
	if (*ptr == '\n')
		ptr++;
	if (*ptr != 0)
		return -EINVAL;
	if (add)
		*odev |= devices;
	else if (sub)
		*odev &= ~devices;
	else
		*odev = devices;
	return count;
}

static int viafb_iga1_odev_proc_show(struct seq_file *m, void *v)
{
	via_odev_to_seq(m, viaparinfo->shared->iga1_devices);
	return 0;
}

static int viafb_iga1_odev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_iga1_odev_proc_show, NULL);
}

static ssize_t viafb_iga1_odev_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	u32 dev_on, dev_off, dev_old, dev_new;
	ssize_t res;

	dev_old = dev_new = viaparinfo->shared->iga1_devices;
	res = odev_update(buffer, count, &dev_new);
	if (res != count)
		return res;
	dev_off = dev_old & ~dev_new;
	dev_on = dev_new & ~dev_old;
	viaparinfo->shared->iga1_devices = dev_new;
	viaparinfo->shared->iga2_devices &= ~dev_new;
	via_set_state(dev_off, VIA_STATE_OFF);
	via_set_source(dev_new, IGA1);
	via_set_state(dev_on, VIA_STATE_ON);
	return res;
}

static const struct file_operations viafb_iga1_odev_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_iga1_odev_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_iga1_odev_proc_write,
};

static int viafb_iga2_odev_proc_show(struct seq_file *m, void *v)
{
	via_odev_to_seq(m, viaparinfo->shared->iga2_devices);
	return 0;
}

static int viafb_iga2_odev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, viafb_iga2_odev_proc_show, NULL);
}

static ssize_t viafb_iga2_odev_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	u32 dev_on, dev_off, dev_old, dev_new;
	ssize_t res;

	dev_old = dev_new = viaparinfo->shared->iga2_devices;
	res = odev_update(buffer, count, &dev_new);
	if (res != count)
		return res;
	dev_off = dev_old & ~dev_new;
	dev_on = dev_new & ~dev_old;
	viaparinfo->shared->iga2_devices = dev_new;
	viaparinfo->shared->iga1_devices &= ~dev_new;
	via_set_state(dev_off, VIA_STATE_OFF);
	via_set_source(dev_new, IGA2);
	via_set_state(dev_on, VIA_STATE_ON);
	return res;
}

static const struct file_operations viafb_iga2_odev_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= viafb_iga2_odev_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= viafb_iga2_odev_proc_write,
};

#define IS_VT1636(lvds_chip)	((lvds_chip).lvds_chip_name == VT1636_LVDS)
static void viafb_init_proc(struct viafb_shared *shared)
{
	struct proc_dir_entry *iga1_entry, *iga2_entry,
		*viafb_entry = proc_mkdir("viafb", NULL);

	shared->proc_entry = viafb_entry;
	if (viafb_entry) {
#ifdef CONFIG_FB_VIA_DIRECT_PROCFS
		proc_create("dvp0", 0, viafb_entry, &viafb_dvp0_proc_fops);
		proc_create("dvp1", 0, viafb_entry, &viafb_dvp1_proc_fops);
		proc_create("dfph", 0, viafb_entry, &viafb_dfph_proc_fops);
		proc_create("dfpl", 0, viafb_entry, &viafb_dfpl_proc_fops);
		if (IS_VT1636(shared->chip_info.lvds_chip_info)
			|| IS_VT1636(shared->chip_info.lvds_chip_info2))
			proc_create("vt1636", 0, viafb_entry,
				&viafb_vt1636_proc_fops);
#endif /* CONFIG_FB_VIA_DIRECT_PROCFS */

		proc_create("supported_output_devices", 0, viafb_entry,
			&viafb_sup_odev_proc_fops);
		iga1_entry = proc_mkdir("iga1", viafb_entry);
		shared->iga1_proc_entry = iga1_entry;
		proc_create("output_devices", 0, iga1_entry,
			&viafb_iga1_odev_proc_fops);
		iga2_entry = proc_mkdir("iga2", viafb_entry);
		shared->iga2_proc_entry = iga2_entry;
		proc_create("output_devices", 0, iga2_entry,
			&viafb_iga2_odev_proc_fops);
	}
}
static void viafb_remove_proc(struct viafb_shared *shared)
{
	struct proc_dir_entry *viafb_entry = shared->proc_entry,
		*iga1_entry = shared->iga1_proc_entry,
		*iga2_entry = shared->iga2_proc_entry;

	if (!viafb_entry)
		return;

	remove_proc_entry("output_devices", iga2_entry);
	remove_proc_entry("iga2", viafb_entry);
	remove_proc_entry("output_devices", iga1_entry);
	remove_proc_entry("iga1", viafb_entry);
	remove_proc_entry("supported_output_devices", viafb_entry);

#ifdef CONFIG_FB_VIA_DIRECT_PROCFS
	remove_proc_entry("dvp0", viafb_entry);/* parent dir */
	remove_proc_entry("dvp1", viafb_entry);
	remove_proc_entry("dfph", viafb_entry);
	remove_proc_entry("dfpl", viafb_entry);
	if (IS_VT1636(shared->chip_info.lvds_chip_info)
		|| IS_VT1636(shared->chip_info.lvds_chip_info2))
		remove_proc_entry("vt1636", viafb_entry);
#endif /* CONFIG_FB_VIA_DIRECT_PROCFS */

	remove_proc_entry("viafb", NULL);
}
#undef IS_VT1636

static int parse_mode(const char *str, u32 *xres, u32 *yres)
{
	char *ptr;

	if (!str) {
		*xres = 640;
		*yres = 480;
		return 0;
	}

	*xres = simple_strtoul(str, &ptr, 10);
	if (ptr[0] != 'x')
		return -EINVAL;

	*yres = simple_strtoul(&ptr[1], &ptr, 10);
	if (ptr[0])
		return -EINVAL;

	return 0;
}


#ifdef CONFIG_PM
static int viafb_suspend(void *unused)
{
	console_lock();
	fb_set_suspend(viafbinfo, 1);
	viafb_sync(viafbinfo);
	console_unlock();

	return 0;
}

static int viafb_resume(void *unused)
{
	console_lock();
	if (viaparinfo->shared->vdev->engine_mmio)
		viafb_reset_engine(viaparinfo);
	viafb_set_par(viafbinfo);
	if (viafb_dual_fb)
		viafb_set_par(viafbinfo1);
	fb_set_suspend(viafbinfo, 0);

	console_unlock();
	return 0;
}

static struct viafb_pm_hooks viafb_fb_pm_hooks = {
	.suspend = viafb_suspend,
	.resume = viafb_resume
};

#endif


int __devinit via_fb_pci_probe(struct viafb_dev *vdev)
{
	u32 default_xres, default_yres;
	struct VideoModeTable *vmode_entry;
	struct fb_var_screeninfo default_var;
	int rc;
	u32 viafb_par_length;

	DEBUG_MSG(KERN_INFO "VIAFB PCI Probe!!\n");
	memset(&default_var, 0, sizeof(default_var));
	viafb_par_length = ALIGN(sizeof(struct viafb_par), BITS_PER_LONG/8);

	/* Allocate fb_info and ***_par here, also including some other needed
	 * variables
	*/
	viafbinfo = framebuffer_alloc(viafb_par_length +
		ALIGN(sizeof(struct viafb_shared), BITS_PER_LONG/8),
		&vdev->pdev->dev);
	if (!viafbinfo) {
		printk(KERN_ERR"Could not allocate memory for viafb_info.\n");
		return -ENOMEM;
	}

	viaparinfo = (struct viafb_par *)viafbinfo->par;
	viaparinfo->shared = viafbinfo->par + viafb_par_length;
	viaparinfo->shared->vdev = vdev;
	viaparinfo->vram_addr = 0;
	viaparinfo->tmds_setting_info = &viaparinfo->shared->tmds_setting_info;
	viaparinfo->lvds_setting_info = &viaparinfo->shared->lvds_setting_info;
	viaparinfo->lvds_setting_info2 =
		&viaparinfo->shared->lvds_setting_info2;
	viaparinfo->crt_setting_info = &viaparinfo->shared->crt_setting_info;
	viaparinfo->chip_info = &viaparinfo->shared->chip_info;

	if (viafb_dual_fb)
		viafb_SAMM_ON = 1;
	parse_lcd_port();
	parse_dvi_port();

	viafb_init_chip_info(vdev->chip_type);
	/*
	 * The framebuffer will have been successfully mapped by
	 * the core (or we'd not be here), but we still need to
	 * set up our own accounting.
	 */
	viaparinfo->fbmem = vdev->fbmem_start;
	viaparinfo->memsize = vdev->fbmem_len;
	viaparinfo->fbmem_free = viaparinfo->memsize;
	viaparinfo->fbmem_used = 0;
	viafbinfo->screen_base = vdev->fbmem;

	viafbinfo->fix.mmio_start = vdev->engine_start;
	viafbinfo->fix.mmio_len = vdev->engine_len;
	viafbinfo->node = 0;
	viafbinfo->fbops = &viafb_ops;
	viafbinfo->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	viafbinfo->pseudo_palette = pseudo_pal;
	if (viafb_accel && !viafb_setup_engine(viafbinfo)) {
		viafbinfo->flags |= FBINFO_HWACCEL_COPYAREA |
			FBINFO_HWACCEL_FILLRECT |  FBINFO_HWACCEL_IMAGEBLIT;
		default_var.accel_flags = FB_ACCELF_TEXT;
	} else {
		viafbinfo->flags |= FBINFO_HWACCEL_DISABLED;
		default_var.accel_flags = 0;
	}

	if (viafb_second_size && (viafb_second_size < 8)) {
		viafb_second_offset = viaparinfo->fbmem_free -
			viafb_second_size * 1024 * 1024;
	} else {
		viafb_second_size = 8;
		viafb_second_offset = viaparinfo->fbmem_free -
			viafb_second_size * 1024 * 1024;
	}

	parse_mode(viafb_mode, &default_xres, &default_yres);
	vmode_entry = viafb_get_mode(default_xres, default_yres);
	if (viafb_SAMM_ON == 1) {
		parse_mode(viafb_mode1, &viafb_second_xres,
			&viafb_second_yres);

		viafb_second_virtual_xres = viafb_second_xres;
		viafb_second_virtual_yres = viafb_second_yres;
	}

	default_var.xres = default_xres;
	default_var.yres = default_yres;
	default_var.xres_virtual = default_xres;
	default_var.yres_virtual = default_yres;
	default_var.bits_per_pixel = viafb_bpp;
	viafb_fill_var_timing_info(&default_var, viafb_get_refresh(
		default_var.xres, default_var.yres, viafb_refresh),
		viafb_get_mode(default_var.xres, default_var.yres));
	viafb_setup_fixinfo(&viafbinfo->fix, viaparinfo);
	viafbinfo->var = default_var;

	if (viafb_dual_fb) {
		viafbinfo1 = framebuffer_alloc(viafb_par_length,
				&vdev->pdev->dev);
		if (!viafbinfo1) {
			printk(KERN_ERR
			"allocate the second framebuffer struct error\n");
			rc = -ENOMEM;
			goto out_fb_release;
		}
		viaparinfo1 = viafbinfo1->par;
		memcpy(viaparinfo1, viaparinfo, viafb_par_length);
		viaparinfo1->vram_addr = viafb_second_offset;
		viaparinfo1->memsize = viaparinfo->memsize -
			viafb_second_offset;
		viaparinfo->memsize = viafb_second_offset;
		viaparinfo1->fbmem = viaparinfo->fbmem + viafb_second_offset;

		viaparinfo1->fbmem_used = viaparinfo->fbmem_used;
		viaparinfo1->fbmem_free = viaparinfo1->memsize -
			viaparinfo1->fbmem_used;
		viaparinfo->fbmem_free = viaparinfo->memsize;
		viaparinfo->fbmem_used = 0;

		viaparinfo->iga_path = IGA1;
		viaparinfo1->iga_path = IGA2;
		memcpy(viafbinfo1, viafbinfo, sizeof(struct fb_info));
		viafbinfo1->par = viaparinfo1;
		viafbinfo1->screen_base = viafbinfo->screen_base +
			viafb_second_offset;

		default_var.xres = viafb_second_xres;
		default_var.yres = viafb_second_yres;
		default_var.xres_virtual = viafb_second_virtual_xres;
		default_var.yres_virtual = viafb_second_virtual_yres;
		default_var.bits_per_pixel = viafb_bpp1;
		viafb_fill_var_timing_info(&default_var, viafb_get_refresh(
			default_var.xres, default_var.yres, viafb_refresh1),
			viafb_get_mode(default_var.xres, default_var.yres));

		viafb_setup_fixinfo(&viafbinfo1->fix, viaparinfo1);
		viafb_check_var(&default_var, viafbinfo1);
		viafbinfo1->var = default_var;
		viafb_update_fix(viafbinfo1);
		viaparinfo1->depth = fb_get_color_depth(&viafbinfo1->var,
			&viafbinfo1->fix);
	}

	viafb_check_var(&viafbinfo->var, viafbinfo);
	viafb_update_fix(viafbinfo);
	viaparinfo->depth = fb_get_color_depth(&viafbinfo->var,
		&viafbinfo->fix);
	default_var.activate = FB_ACTIVATE_NOW;
	rc = fb_alloc_cmap(&viafbinfo->cmap, 256, 0);
	if (rc)
		goto out_fb1_release;

	if (viafb_dual_fb && (viafb_primary_dev == LCD_Device)
	    && (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)) {
		rc = register_framebuffer(viafbinfo1);
		if (rc)
			goto out_dealloc_cmap;
	}
	rc = register_framebuffer(viafbinfo);
	if (rc)
		goto out_fb1_unreg_lcd_cle266;

	if (viafb_dual_fb && ((viafb_primary_dev != LCD_Device)
			|| (viaparinfo->chip_info->gfx_chip_name !=
			UNICHROME_CLE266))) {
		rc = register_framebuffer(viafbinfo1);
		if (rc)
			goto out_fb_unreg;
	}
	DEBUG_MSG(KERN_INFO "fb%d: %s frame buffer device %dx%d-%dbpp\n",
		  viafbinfo->node, viafbinfo->fix.id, default_var.xres,
		  default_var.yres, default_var.bits_per_pixel);

	viafb_init_proc(viaparinfo->shared);
	viafb_init_dac(IGA2);

#ifdef CONFIG_PM
	viafb_pm_register(&viafb_fb_pm_hooks);
#endif
	return 0;

out_fb_unreg:
	unregister_framebuffer(viafbinfo);
out_fb1_unreg_lcd_cle266:
	if (viafb_dual_fb && (viafb_primary_dev == LCD_Device)
	    && (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266))
		unregister_framebuffer(viafbinfo1);
out_dealloc_cmap:
	fb_dealloc_cmap(&viafbinfo->cmap);
out_fb1_release:
	if (viafbinfo1)
		framebuffer_release(viafbinfo1);
out_fb_release:
	framebuffer_release(viafbinfo);
	return rc;
}

void __devexit via_fb_pci_remove(struct pci_dev *pdev)
{
	DEBUG_MSG(KERN_INFO "via_pci_remove!\n");
	fb_dealloc_cmap(&viafbinfo->cmap);
	unregister_framebuffer(viafbinfo);
	if (viafb_dual_fb)
		unregister_framebuffer(viafbinfo1);
	viafb_remove_proc(viaparinfo->shared);
	framebuffer_release(viafbinfo);
	if (viafb_dual_fb)
		framebuffer_release(viafbinfo1);
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
		else if (!strncmp(this_opt, "viafb_lcd_port=", 15))
			viafb_lcd_port = kstrdup(this_opt + 15, GFP_KERNEL);
		else if (!strncmp(this_opt, "viafb_dvi_port=", 15))
			viafb_dvi_port = kstrdup(this_opt + 15, GFP_KERNEL);
	}
	return 0;
}
#endif

/*
 * These are called out of via-core for now.
 */
int __init viafb_init(void)
{
	u32 dummy_x, dummy_y;
#ifndef MODULE
	char *option = NULL;
	if (fb_get_options("viafb", &option))
		return -ENODEV;
	viafb_setup(option);
#endif
	if (parse_mode(viafb_mode, &dummy_x, &dummy_y)
		|| !viafb_get_mode(dummy_x, dummy_y)
		|| parse_mode(viafb_mode1, &dummy_x, &dummy_y)
		|| !viafb_get_mode(dummy_x, dummy_y)
		|| viafb_bpp < 0 || viafb_bpp > 32
		|| viafb_bpp1 < 0 || viafb_bpp1 > 32
		|| parse_active_dev())
		return -EINVAL;

	printk(KERN_INFO
       "VIA Graphics Integration Chipset framebuffer %d.%d initializing\n",
	       VERSION_MAJOR, VERSION_MINOR);
	return 0;
}

void __exit viafb_exit(void)
{
	DEBUG_MSG(KERN_INFO "viafb_exit!\n");
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
};


#ifdef MODULE
module_param(viafb_mode, charp, S_IRUSR);
MODULE_PARM_DESC(viafb_mode, "Set resolution (default=640x480)");

module_param(viafb_mode1, charp, S_IRUSR);
MODULE_PARM_DESC(viafb_mode1, "Set resolution (default=640x480)");

module_param(viafb_bpp, int, S_IRUSR);
MODULE_PARM_DESC(viafb_bpp, "Set color depth (default=32bpp)");

module_param(viafb_bpp1, int, S_IRUSR);
MODULE_PARM_DESC(viafb_bpp1, "Set color depth (default=32bpp)");

module_param(viafb_refresh, int, S_IRUSR);
MODULE_PARM_DESC(viafb_refresh,
	"Set CRT viafb_refresh rate (default = 60)");

module_param(viafb_refresh1, int, S_IRUSR);
MODULE_PARM_DESC(viafb_refresh1,
	"Set CRT refresh rate (default = 60)");

module_param(viafb_lcd_panel_id, int, S_IRUSR);
MODULE_PARM_DESC(viafb_lcd_panel_id,
	"Set Flat Panel type(Default=1024x768)");

module_param(viafb_lcd_dsp_method, int, S_IRUSR);
MODULE_PARM_DESC(viafb_lcd_dsp_method,
	"Set Flat Panel display scaling method.(Default=Expandsion)");

module_param(viafb_SAMM_ON, int, S_IRUSR);
MODULE_PARM_DESC(viafb_SAMM_ON,
	"Turn on/off flag of SAMM(Default=OFF)");

module_param(viafb_accel, int, S_IRUSR);
MODULE_PARM_DESC(viafb_accel,
	"Set 2D Hardware Acceleration: 0 = OFF, 1 = ON (default)");

module_param(viafb_active_dev, charp, S_IRUSR);
MODULE_PARM_DESC(viafb_active_dev, "Specify active devices.");

module_param(viafb_display_hardware_layout, int, S_IRUSR);
MODULE_PARM_DESC(viafb_display_hardware_layout,
	"Display Hardware Layout (LCD Only, DVI Only...,etc)");

module_param(viafb_second_size, int, S_IRUSR);
MODULE_PARM_DESC(viafb_second_size,
	"Set secondary device memory size");

module_param(viafb_dual_fb, int, S_IRUSR);
MODULE_PARM_DESC(viafb_dual_fb,
	"Turn on/off flag of dual framebuffer devices.(Default = OFF)");

module_param(viafb_platform_epia_dvi, int, S_IRUSR);
MODULE_PARM_DESC(viafb_platform_epia_dvi,
	"Turn on/off flag of DVI devices on EPIA board.(Default = OFF)");

module_param(viafb_device_lcd_dualedge, int, S_IRUSR);
MODULE_PARM_DESC(viafb_device_lcd_dualedge,
	"Turn on/off flag of dual edge panel.(Default = OFF)");

module_param(viafb_bus_width, int, S_IRUSR);
MODULE_PARM_DESC(viafb_bus_width,
	"Set bus width of panel.(Default = 12)");

module_param(viafb_lcd_mode, int, S_IRUSR);
MODULE_PARM_DESC(viafb_lcd_mode,
	"Set Flat Panel mode(Default=OPENLDI)");

module_param(viafb_lcd_port, charp, S_IRUSR);
MODULE_PARM_DESC(viafb_lcd_port, "Specify LCD output port.");

module_param(viafb_dvi_port, charp, S_IRUSR);
MODULE_PARM_DESC(viafb_dvi_port, "Specify DVI output port.");

MODULE_LICENSE("GPL");
#endif
