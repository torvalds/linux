/* linux/arch/arm/mach-exynos/include/mach/regs-tmu-4210.h

* Copyright (c) 2012 Samsung Electronics Co., Ltd.
*      http://www.samsung.com/
*
* EXYNOS4210 - Thermal Management support
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_TMU_4210_H
#define __ASM_ARCH_REGS_TMU_4210_H __FILE__

#define THRESHOLD_TEMP	(0x44)
#define TRIG_LEV0	(0x50)
#define TRIG_LEV1	(0x54)
#define TRIG_LEV2	(0x58)
#define TRIG_LEV3	(0x5C)

#define INTEN0		(1)
#define INTEN1		(1<<4)
#define INTEN2		(1<<8)
#define INTEN3		(1<<12)

#define INTSTAT0	(1)
#define INTSTAT1	(1<<4)
#define INTSTAT2	(1<<8)
#define INTSTAT3	(1<<12)

#define INTCLEAR0	(1)
#define INTCLEAR1	(1<<4)
#define INTCLEAR2	(1<<8)
#define INTCLEAR3	(1<<12)

#define INTCLEARALL	(INTCLEAR0 | INTCLEAR1 | \
			INTCLEAR2 | INTCLEAR3)
#endif
