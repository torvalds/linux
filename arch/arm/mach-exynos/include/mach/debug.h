/*
 * linux/arch/arm/mach-exynos/include/mach/debug.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS5410 - Debug architecture support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_DEBUG_H
#define __ASM_ARCH_DEBUG_H __FILE__

#define UNLOCK_MAGIC	(0xc5acce55)
#define CORE_CNT	(4)
#define CLUSTER_CNT	(2)

/*
 * Debug register base address
 * (UM 13.Coresight, Figure 13-7)
 */
#define ROM_CA15_CPU0	(0x1088e000)
#define ROM_CA7_CPU0	(0x1089f000)

#define OFFSET_ROM	(0x2000)

#define OFFSET_LAR	(0xfb0)
#define OFFSET_LSR	(0xfb4)
#define OFFSET_ECR	(0x024)
#define OFFSET_DBGBCR0	(0x140)
#define OFFSET_DBGBCR1	(0x144)
#define OFFSET_DBGBCR2	(0x148)
#define OFFSET_DBGBCR3	(0x14c)
#define OFFSET_DBGBCR4	(0x150)
#define OFFSET_DBGBCR5	(0x154)
#define OFFSET_DBGBVR0	(0x100)
#define OFFSET_DBGBVR1	(0x104)
#define OFFSET_DBGBVR2	(0x108)
#define OFFSET_DBGBVR3	(0x10c)
#define OFFSET_DBGBVR4	(0x110)
#define OFFSET_DBGBVR5	(0x114)

#define OFFSET_DBGBXVR0	(0x250)
#define OFFSET_DBGBXVR1	(0x254)

#define OFFSET_DBGWVR0	(0x180)
#define OFFSET_DBGWVR1	(0x184)
#define OFFSET_DBGWVR2	(0x188)
#define OFFSET_DBGWVR3	(0x18c)
#define OFFSET_DBGWCR0	(0x1c0)
#define OFFSET_DBGWCR1	(0x1c4)
#define OFFSET_DBGWCR2	(0x1c8)
#define OFFSET_DBGWCR3	(0x1cc)
#define OFFSET_DBGVCR	(0x01c)

enum BREAK_REG {
	DBGBCR0 = 0,
	DBGBCR1,
	DBGBCR2,
	DBGBCR3,
	DBGBCR4,
	DBGBCR5,
	DBGBVR0,
	DBGBVR1,
	DBGBVR2,
	DBGBVR3,
	DBGBVR4,
	DBGBVR5,

	DBGBXVR0,
	DBGBXVR1,

	DBGWVR0,
	DBGWVR1,
	DBGWVR2,
	DBGWVR3,
	DBGWCR0,
	DBGWCR1,
	DBGWCR2,
	DBGWCR3,
	DBGVCR,

	NR_BRK_REG,
};

enum COMMON_REG {
	DBGDSCR = 0,
	DBGWFAR,
	DBGDSCCR,
	DBGDSMCR,
	DBGCLAIM,	/* DBGCLAIMCLR for read, DBGCLAIMSET for write. */
	DBGTRTX,
	DBGTRRX,

	FLAG_SAVED,
	NR_COMM_REG,
};

extern bool FLAG_T32_EN;

#endif /* __ASM_ARCH_DEBUG_H */
