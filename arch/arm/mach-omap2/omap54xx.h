/*:
 * Address mappings and base address for OMAP5 interconnects
 * and peripherals.
 *
 * Copyright (C) 2012 Texas Instruments
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	Sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_SOC_OMAP54XX_H
#define __ASM_SOC_OMAP54XX_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers.
 */
#define L4_54XX_BASE			0x4a000000
#define L4_WK_54XX_BASE			0x4ae00000
#define L4_PER_54XX_BASE		0x48000000
#define L3_54XX_BASE			0x44000000
#define OMAP54XX_32KSYNCT_BASE		0x4ae04000
#define OMAP54XX_CM_CORE_AON_BASE	0x4a004000
#define OMAP54XX_CM_CORE_BASE		0x4a008000
#define OMAP54XX_PRM_BASE		0x4ae06000
#define OMAP54XX_PRCM_MPU_BASE		0x48243000
#define OMAP54XX_SCM_BASE		0x4a002000
#define OMAP54XX_CTRL_BASE		0x4a002800
#define OMAP54XX_SAR_RAM_BASE		0x4ae26000

#endif /* __ASM_SOC_OMAP555554XX_H */
