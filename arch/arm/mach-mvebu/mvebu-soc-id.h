/*
 * Marvell EBU SoC ID and revision definitions.
 *
 * Copyright (C) 2014 Marvell Semiconductor
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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

/* Armada 375 */
#define ARMADA_375_Z1_REV   0x0
#define ARMADA_375_A0_REV   0x3

#ifdef CONFIG_ARCH_MVEBU
int mvebu_get_soc_id(u32 *dev, u32 *rev);
#else
static inline int mvebu_get_soc_id(u32 *dev, u32 *rev)
{
	return -1;
}
#endif

#endif /* __LINUX_MVEBU_SOC_ID_H */
