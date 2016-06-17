/* linux/arch/arm/plat-samsung/include/plat/map-s5p.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_MAP_S5P_H
#define __ASM_PLAT_MAP_S5P_H __FILE__

#define S5P_VA_CHIPID		S3C_ADDR(0x02000000)
#define S5P_VA_CMU		S3C_ADDR(0x02100000)

#define S5P_VA_DMC0		S3C_ADDR(0x02440000)
#define S5P_VA_DMC1		S3C_ADDR(0x02480000)

#define S5P_VA_COREPERI_BASE	S3C_ADDR(0x02800000)
#define S5P_VA_COREPERI(x)	(S5P_VA_COREPERI_BASE + (x))
#define S5P_VA_SCU		S5P_VA_COREPERI(0x0)

#define VA_VIC(x)		(S3C_VA_IRQ + ((x) * 0x10000))
#define VA_VIC0			VA_VIC(0)
#define VA_VIC1			VA_VIC(1)
#define VA_VIC2			VA_VIC(2)
#define VA_VIC3			VA_VIC(3)

#include <plat/map-s3c.h>

#endif /* __ASM_PLAT_MAP_S5P_H */
