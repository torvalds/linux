/*
 * Common support header for virtex ppc405 platforms
 *
 * Copyright 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_VIRTEX_DEVICES_H__
#define __ASM_VIRTEX_DEVICES_H__

#include <linux/platform_device.h>

/* ML300/403 reference design framebuffer driver platform data struct */
struct xilinxfb_platform_data {
	u32 rotate_screen;
	u32 screen_height_mm;
	u32 screen_width_mm;
};

void __init virtex_early_serial_map(void);

/* Prototype for device fixup routine.  Implement this routine in the
 * board specific fixup code and the generic setup code will call it for
 * each device is the platform device list.
 *
 * If the hook returns a non-zero value, then the device will not get
 * registered with the platform bus
 */
int virtex_device_fixup(struct platform_device *dev);

#endif  /* __ASM_VIRTEX_DEVICES_H__ */
