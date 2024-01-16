/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */

#ifndef __VIAFBDEV_H__
#define __VIAFBDEV_H__

#include <linux/proc_fs.h>
#include <linux/fb.h>
#include <linux/spinlock.h>

#include "via_aux.h"
#include "ioctl.h"
#include "share.h"
#include "chip.h"
#include "hw.h"

#define VERSION_MAJOR       2
#define VERSION_KERNEL      6	/* For kernel 2.6 */

#define VERSION_OS          0	/* 0: for 32 bits OS, 1: for 64 bits OS */
#define VERSION_MINOR       4

#define VIAFB_NUM_I2C		5

struct viafb_shared {
	u32 iga1_devices;
	u32 iga2_devices;

	struct proc_dir_entry *proc_entry;	/*viafb proc entry */
	struct proc_dir_entry *iga1_proc_entry;
	struct proc_dir_entry *iga2_proc_entry;
	struct viafb_dev *vdev;			/* Global dev info */

	/* I2C busses that may have auxiliary devices */
	struct via_aux_bus *i2c_26;
	struct via_aux_bus *i2c_31;
	struct via_aux_bus *i2c_2C;

	/* All the information will be needed to set engine */
	struct tmds_setting_information tmds_setting_info;
	struct lvds_setting_information lvds_setting_info;
	struct lvds_setting_information lvds_setting_info2;
	struct chip_information chip_info;

	/* hardware acceleration stuff */
	u32 cursor_vram_addr;
	u32 vq_vram_addr;	/* virtual queue address in video ram */
	int (*hw_bitblt)(void __iomem *engine, u8 op, u32 width, u32 height,
		u8 dst_bpp, u32 dst_addr, u32 dst_pitch, u32 dst_x, u32 dst_y,
		u32 *src_mem, u32 src_addr, u32 src_pitch, u32 src_x, u32 src_y,
		u32 fg_color, u32 bg_color, u8 fill_rop);
};

struct viafb_par {
	u8 depth;
	u32 vram_addr;

	unsigned int fbmem;	/*framebuffer physical memory address */
	unsigned int memsize;	/*size of fbmem */
	u32 fbmem_free;		/* Free FB memory */
	u32 fbmem_used;		/* Use FB memory size */
	u32 iga_path;

	struct viafb_shared *shared;

	/* All the information will be needed to set engine */
	/* depreciated, use the ones in shared directly */
	struct tmds_setting_information *tmds_setting_info;
	struct lvds_setting_information *lvds_setting_info;
	struct lvds_setting_information *lvds_setting_info2;
	struct chip_information *chip_info;
};

extern int viafb_SAMM_ON;
extern int viafb_dual_fb;
extern int viafb_LCD2_ON;
extern int viafb_LCD_ON;
extern int viafb_DVI_ON;
extern int viafb_hotplug;

u8 viafb_gpio_i2c_read_lvds(struct lvds_setting_information
	*plvds_setting_info, struct lvds_chip_information
	*plvds_chip_info, u8 index);
void viafb_gpio_i2c_write_mask_lvds(struct lvds_setting_information
			      *plvds_setting_info, struct lvds_chip_information
			      *plvds_chip_info, struct IODATA io_data);
int via_fb_pci_probe(struct viafb_dev *vdev);
void via_fb_pci_remove(struct pci_dev *pdev);
/* Temporary */
int viafb_init(void);
void viafb_exit(void);
#endif /* __VIAFBDEV_H__ */
