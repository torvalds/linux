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

#ifndef __VIAFBDEV_H__
#define __VIAFBDEV_H__

#include <linux/proc_fs.h>
#include <linux/fb.h>

#include "ioctl.h"
#include "share.h"
#include "chip.h"
#include "hw.h"
#include "via_i2c.h"

#define VERSION_MAJOR       2
#define VERSION_KERNEL      6	/* For kernel 2.6 */

#define VERSION_OS          0	/* 0: for 32 bits OS, 1: for 64 bits OS */
#define VERSION_MINOR       4

struct viafb_par {
	int bpp;
	int hres;
	int vres;
	int linelength;
	u32 xoffset;
	u32 yoffset;

	void __iomem *fbmem_virt;	/*framebuffer virtual memory address */
	void __iomem *io_virt;	/*iospace virtual memory address */
	unsigned int fbmem;	/*framebuffer physical memory address */
	unsigned int memsize;	/*size of fbmem */
	unsigned int io;	/*io space address */
	unsigned long mmio_base;	/*mmio base address */
	unsigned long mmio_len;	/*mmio base length */
	u32 fbmem_free;		/* Free FB memory */
	u32 fbmem_used;		/* Use FB memory size */
	u32 cursor_start;	/* Cursor Start Address */
	u32 VQ_start;		/* Virtual Queue Start Address */
	u32 VQ_end;		/* Virtual Queue End Address */
	u32 iga_path;
	struct proc_dir_entry *proc_entry;	/*viafb proc entry */
	u8 duoview;		/*Is working in duoview mode? */

	/* I2C stuff */
	struct via_i2c_stuff i2c_stuff;

	/* All the information will be needed to set engine */
	struct tmds_setting_information *tmds_setting_info;
	struct crt_setting_information *crt_setting_info;
	struct lvds_setting_information *lvds_setting_info;
	struct lvds_setting_information *lvds_setting_info2;
	struct chip_information *chip_info;

	/* some information related to video playing */
	int video_on_crt;
	int video_on_dvi;
	int video_on_lcd;

};
struct viafb_modeinfo {
	u32 xres;
	u32 yres;
	int mode_index;
	char *mode_res;
};
extern unsigned int viafb_second_virtual_yres;
extern unsigned int viafb_second_virtual_xres;
extern unsigned int viafb_second_offset;
extern int viafb_second_size;
extern int viafb_SAMM_ON;
extern int viafb_dual_fb;
extern int viafb_LCD2_ON;
extern int viafb_LCD_ON;
extern int viafb_DVI_ON;
extern int viafb_accel;
extern int viafb_hotplug;
extern int viafb_memsize;

extern int strict_strtoul(const char *cp, unsigned int base,
	unsigned long *res);

void viafb_memory_pitch_patch(struct fb_info *info);
void viafb_fill_var_timing_info(struct fb_var_screeninfo *var, int refresh,
			  int mode_index);
int viafb_get_mode_index(int hres, int vres, int flag);
u8 viafb_gpio_i2c_read_lvds(struct lvds_setting_information
	*plvds_setting_info, struct lvds_chip_information
	*plvds_chip_info, u8 index);
void viafb_gpio_i2c_write_mask_lvds(struct lvds_setting_information
			      *plvds_setting_info, struct lvds_chip_information
			      *plvds_chip_info, struct IODATA io_data);
#endif /* __VIAFBDEV_H__ */
