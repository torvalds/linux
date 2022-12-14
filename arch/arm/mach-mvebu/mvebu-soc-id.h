/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell EBU SoC ID and revision definitions.
 *
 * Copyright (C) 2014 Marvell Semiconductor
 */

#ifndef __LINUX_MVEBU_SOC_ID_H
#define __LINUX_MVEBU_SOC_ID_H

/* Armada XP ID */
#define MV78230_DEV_ID	    0x7823
#define MV78260_DEV_ID	    0x7826
#define MV78460_DEV_ID	    0x7846

/* Armada XP Revision */
#define MV78XX0_A0_REV	    0x1
#define MV78XX0_B0_REV	    0x2

/* Amada 370 ID */
#define ARMADA_370_DEV_ID   0x6710

/* Amada 370 Revision */
#define ARMADA_370_A1_REV   0x1

/* Armada 375 ID */
#define ARMADA_375_DEV_ID   0x6720

/* Armada 375 */
#define ARMADA_375_Z1_REV   0x0
#define ARMADA_375_A0_REV   0x3

/* Armada 38x ID */
#define ARMADA_380_DEV_ID   0x6810
#define ARMADA_385_DEV_ID   0x6820
#define ARMADA_388_DEV_ID   0x6828

/* Armada 38x Revision */
#define ARMADA_38x_Z1_REV   0x0
#define ARMADA_38x_A0_REV   0x4

#ifdef CONFIG_ARCH_MVEBU
int mvebu_get_soc_id(u32 *dev, u32 *rev);
#else
static inline int mvebu_get_soc_id(u32 *dev, u32 *rev)
{
	return -1;
}
#endif

#endif /* __LINUX_MVEBU_SOC_ID_H */
