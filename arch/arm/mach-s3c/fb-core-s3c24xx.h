/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *	Pawel Osciak <p.osciak@samsung.com>
 *
 * Samsung framebuffer driver core functions
 */
#ifndef __ASM_PLAT_FB_CORE_S3C24XX_H
#define __ASM_PLAT_FB_CORE_S3C24XX_H __FILE__

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

#endif /* __ASM_PLAT_FB_CORE_S3C24XX_H */
