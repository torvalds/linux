/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-w90x900/include/mach/mfp.h
 *
 * Copyright (c) 2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/map.h
 */

#ifndef __ASM_ARCH_MFP_H
#define __ASM_ARCH_MFP_H

extern void mfp_set_groupf(struct device *dev);
extern void mfp_set_groupc(struct device *dev);
extern void mfp_set_groupi(struct device *dev);
extern void mfp_set_groupg(struct device *dev, const char *subname);
extern void mfp_set_groupd(struct device *dev, const char *subname);

#endif /* __ASM_ARCH_MFP_H */
