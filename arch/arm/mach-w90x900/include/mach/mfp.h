/*
 * arch/arm/mach-w90x900/include/mach/mfp.h
 *
 * Copyright (c) 2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/map.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_MFP_H
#define __ASM_ARCH_MFP_H

extern void mfp_set_groupf(struct device *dev);
extern void mfp_set_groupc(struct device *dev);
extern void mfp_set_groupi(struct device *dev);
extern void mfp_set_groupg(struct device *dev);

#endif /* __ASM_ARCH_MFP_H */
