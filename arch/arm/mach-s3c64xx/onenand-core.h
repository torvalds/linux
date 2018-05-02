/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Samsung OneNAD Controller core functions
 */

#ifndef __ASM_ARCH_ONENAND_CORE_H
#define __ASM_ARCH_ONENAND_CORE_H __FILE__

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c_onenand_setname(char *name)
{
#ifdef CONFIG_S3C_DEV_ONENAND
	s3c_device_onenand.name = name;
#endif
}

static inline void s3c64xx_onenand1_setname(char *name)
{
#ifdef CONFIG_S3C64XX_DEV_ONENAND1
	s3c64xx_device_onenand1.name = name;
#endif
}

#endif /* __ASM_ARCH_ONENAND_CORE_H */
