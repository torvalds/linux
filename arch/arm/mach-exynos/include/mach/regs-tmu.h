/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * EXYNOS - Thermal Management support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGS_THERMAL_H
#define __ASM_ARCH_REGS_THERMAL_H __FILE__

#define TRIMINFO		(0x0)
#define TRIMINFO_CON		(0x14)

#define TMU_CON			(0x20)
#define TMU_STATUS		(0x28)
#define SAMPLING_INTERNAL	(0x2C)
#define CNT_VALUE0		(0x30)
#define CNT_VALUE1		(0x34)
#define CURRENT_TEMP		(0x40)

#define THD_TEMP_RISE		(0x50)
#define THD_TEMP_FALL		(0x54)

#define PAST_TMEP0		(0x60)
#define PAST_TMEP1		(0x64)
#define PAST_TMEP2		(0x68)
#define PAST_TMEP3		(0x6C)

#define INTEN			(0x70)
#define INTSTAT			(0x74)
#define INTCLEAR		(0x78)

#define EMUL_CON		(0x80)

#define TRIMINFO_RELOAD		(1)

#define CORE_EN			(1)
#define TRIP_EN			(1<<12)
#define TRIP_ONLYCURRENT	(0<<13)
#define TRIP_CUR_PAST3_0	(4<<13)
#define TRIP_CUR_PAST7_0	(5<<13)
#define TRIP_CUR_PAST11_0	(6<<13)
#define TRIP_CUR_PAST15_0	(7<<13)

#define INTEN_RISE0		(1)
#define INTEN_RISE1		(1<<4)
#define INTEN_RISE2		(1<<8)
#define INTEN_FALL0		(1<<16)
#define INTEN_FALL1		(1<<20)
#define INTEN_FALL2		(1<<24)

#define INTSTAT_RISE0		(1)
#define INTSTAT_RISE1		(1<<4)
#define INTSTAT_RISE2		(1<<8)
#define INTSTAT_FALL0		(1<<16)
#define INTSTAT_FALL1		(1<<20)
#define INTSTAT_FALL2		(1<<24)

#define TRIM_INFO_MASK		(0xFF)

#define INTCLEAR_RISE0		(1)
#define INTCLEAR_RISE1		(1<<4)
#define INTCLEAR_RISE2		(1<<8)
#define INTCLEAR_FALL0		(1<<16)
#define INTCLEAR_FALL1		(1<<20)
#define INTCLEAR_FALL2		(1<<24)
#define CLEAR_RISE_INT		(INTCLEAR_RISE0 | INTCLEAR_RISE1 | \
				 INTCLEAR_RISE2)
#define CLEAR_FALL_INT		(INTCLEAR_FALL0 | INTCLEAR_FALL1 | \
				 INTCLEAR_FALL2)
#define EMUL_EN		(1)

#define FREQ_IN_PLL		24000000	/* 24MHz in Hz */
#endif
