#ifndef _INTELFBDRV_H
#define _INTELFBDRV_H

/*
 ******************************************************************************
 * intelfb
 *
 * Linux framebuffer driver for Intel(R) 830M/845G/852GM/855GM/865G/915G
 * integrated graphics chips.
 *
 * Copyright © 2004 Sylvain Meyer
 *
 * Author: Sylvain Meyer
 *
 ******************************************************************************
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

static void __devinit get_initial_mode(struct intelfb_info *dinfo);
static void update_dinfo(struct intelfb_info *dinfo,
			 struct fb_var_screeninfo *var);
static int intelfb_get_fix(struct fb_fix_screeninfo *fix,
			   struct fb_info *info);

static int intelfb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info);
static int intelfb_set_par(struct fb_info *info);
static int intelfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info);

static int intelfb_blank(int blank, struct fb_info *info);
static int intelfb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info);

static void intelfb_fillrect(struct fb_info *info,
			     const struct fb_fillrect *rect);
static void intelfb_copyarea(struct fb_info *info,
			     const struct fb_copyarea *region);
static void intelfb_imageblit(struct fb_info *info,
			      const struct fb_image *image);
static int intelfb_cursor(struct fb_info *info,
			   struct fb_cursor *cursor);

static int intelfb_sync(struct fb_info *info);

static int intelfb_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg,
			 struct fb_info *info);

static int __devinit intelfb_pci_register(struct pci_dev *pdev,
					  const struct pci_device_id *ent);
static void __devexit intelfb_pci_unregister(struct pci_dev *pdev);
static int __devinit intelfb_set_fbinfo(struct intelfb_info *dinfo);

#endif
