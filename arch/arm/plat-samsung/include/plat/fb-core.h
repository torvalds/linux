/*
 * arch/arm/plat-samsung/include/plat/fb-core.h
 *
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *	Pawel Osciak <p.osciak@samsung.com>
 *
 * Samsung framebuffer driver core functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PLAT_FB_CORE_H
#define __ASM_PLAT_FB_CORE_H __FILE__

/*
 * These functions are only for use with the core support code, such as
 * the CPU-specific initialization code.
 */

/* Re-define device name depending on support. */
static inline void s3c_fb_setname(char *name)
{
#ifdef CONFIG_S3C_DEV_FB
	s3c_device_fb.name = name;
#endif
}

/* Re-define device name depending on support. */
static inline void s5p_fb_setname(int id, char *name)
{
	switch (id) {
#ifdef CONFIG_S5P_DEV_FIMD0
	case 0:
		s5p_device_fimd0.name = name;
	break;
#endif
	default:
		printk(KERN_ERR "%s: invalid device id(%d)\n", __func__, id);
	break;
	}
}

#endif /* __ASM_PLAT_FB_CORE_H */
