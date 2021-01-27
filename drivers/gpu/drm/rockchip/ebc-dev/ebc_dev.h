// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef __EBC_DEV_H__
#define __EBC_DEV_H__

#include <linux/notifier.h>

/*
* max support panel size 2232x1680
* ebc module display buf use 4bit per pixel
* eink module display buf use 8bit per pixel
* ebc module direct mode display buf use 2bit per pixel
*/
#define EBC_FB_SIZE		0x200000 /* 2M */
#define EINK_FB_SIZE		0x400000 /* 4M */
#define DIRECT_FB_SIZE		0x100000 /* 1M */

#define MAX_FB_NUM		4

#define EBC_SUCCESS		(0)
#define EBC_ERROR		(-1)

/*
 * ebc status notify
 */
#define EBC_OFF			(0)
#define EBC_ON			(1)
#define EBC_FB_BLANK		(2)
#define EBC_FB_UNBLANK		(3)

/*
 * ebc system ioctl command
 */
#define EBC_GET_BUFFER		(0x7000)
#define EBC_SEND_BUFFER		(0x7001)
#define EBC_GET_BUFFER_INFO	(0x7002)
#define EBC_SET_FULL_MODE_NUM	(0x7003)
#define EBC_ENABLE_OVERLAY	(0x7004)
#define EBC_DISABLE_OVERLAY	(0x7005)
#define EBC_GET_OSD_BUFFER	(0x7006)
#define EBC_SEND_OSD_BUFFER	(0x7007)

/*
 * IMPORTANT: Those values is corresponding to android hardware program,
 * so *FORBID* to changes bellow values, unless you know what you're doing.
 * And if you want to add new refresh modes, please appended to the tail.
 */
enum panel_refresh_mode {
	EPD_AUTO		= 0,
	EPD_OVERLAY		= 1,
	EPD_FULL_GC16		= 2,
	EPD_FULL_GL16		= 3,
	EPD_FULL_GLR16		= 4,
	EPD_FULL_GLD16		= 5,
	EPD_FULL_GCC16		= 6,
	EPD_PART_GC16		= 7,
	EPD_PART_GL16		= 8,
	EPD_PART_GLR16		= 9,
	EPD_PART_GLD16		= 10,
	EPD_PART_GCC16		= 11,
	EPD_A2			= 12,
	EPD_DU			= 13,
	EPD_RESET		= 14,
	EPD_SUSPEND		= 15,
	EPD_RESUME		= 16,
	EPD_POWER_OFF		= 17,
	EPD_PART_EINK		= 18,
	EPD_FULL_EINK		= 19,
};

/*
 * IMPORTANT: android hardware use struct, so *FORBID* to changes this, unless you know what you're doing.
 */
struct ebc_buf_info {
	int offset;
	int epd_mode;
	int height;
	int width;
	int panel_color;
	int win_x1;
	int win_y1;
	int win_x2;
	int win_y2;
	int width_mm;
	int height_mm;
};

#if IS_ENABLED(CONFIG_ROCKCHIP_EBC_DEV)
int ebc_register_notifier(struct notifier_block *nb);
int ebc_unregister_notifier(struct notifier_block *nb);
int ebc_notify(unsigned long event);
#else
static inline int ebc_register_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int ebc_unregister_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int ebc_notify(unsigned long event)
{
	return 0;
}
#endif

#endif
