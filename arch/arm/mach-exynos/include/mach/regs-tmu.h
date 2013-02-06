/* linux/arch/arm/mach-exynos/include/mach/regs-tmu.h

* Copyright (c) 2010 Samsung Electronics Co., Ltd.
*      http://www.samsung.com/
*
* EXYNOS4 - Thermal Management support
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_TMU_H
#define __ASM_ARCH_REGS_TMU_H __FILE__

enum tmu_state_t {
	TMU_STATUS_NORMAL = 0,
	TMU_STATUS_THROTTLED,
	TMU_STATUS_WARNING,
	TMU_STATUS_TRIPPED,
	TMU_STATUS_INIT,
	TMU_STATUS_TC,
};

#define FREQ_IN_PLL       24000000  /* 24MHZ in Hz */
#define AUTO_REFRESH_PERIOD_TQ0    1950
#define AUTO_REFRESH_PERIOD_NORMAL 3900

/* TMU Common registers */
#define EXYNOS4_TMU_TRIMINFO		(0x0000)
#define TMU_TRIMINFO_MASK		(0xFF)

#define EXYNOS4_TMU_CONTROL		(0x0020)
#define TMUCORE_ENABLE      (1 << 0)

#define EXYNOS4_TMU_STATUS		(0x0028)
#define TMU_IDLE	(1 << 0)
#define TMU_BUSY	(0 << 0)

#define EXYNOS4_TMU_SAMPLING_INTERNAL	(0x002C)
#define EXYNOS4_TMU_COUNTER_VALUE0	(0x0030)
#define EXYNOS4_TMU_COUNTER_VALUE1	(0x0034)

#define EXYNOS4_TMU_CURRENT_TEMP	(0x0040)
/* TMU has temperature code within a temperature range of 25C to 125C */
#define TEMP_MIN_CELCIUS	25
#define TEMP_MAX_CELCIUS	125
#define TMU_DC_VALUE		25

#define EXYNOS4_TMU_INTEN		(0x0070)
#define EXYNOS4_TMU_INTSTAT		(0x0074)
#define EXYNOS4_TMU_INTCLEAR		(0x0078)

/*
 * The below registers and definition is used
 * in only EXYNOS4210
 *
*/
/* Below 3line is used to exynos4210 evt0 without right e-fusing */
#define EFUSE_MIN_VALUE 60
#define EFUSE_AVG_VALUE 80
#define EFUSE_MAX_VALUE 100

/* To get rightly current temperature, EXYNOS4_TMU_CONTROL setting value */
#define VREF_SLOPE     0x07000F02

#define EXYNOS4210_TMU_THRESHOLD_TEMP	(0x0044)
#define EXYNOS4210_TMU_TRIG_LEVEL0	(0x0050)
#define EXYNOS4210_TMU_TRIG_LEVEL1	(0x0054)
#define EXYNOS4210_TMU_TRIG_LEVEL2	(0x0058)
#define EXYNOS4210_TMU_TRIG_LEVEL3	(0x005C)
#define TRIGGER_LEV_MAX   (0xFF)

#define EXYNOS4210_TMU_PAST_TMEP0	(0x0060)
#define EXYNOS4210_TMU_PAST_TMEP1		(0x0064)
#define EXYNOS4210_TMU_PAST_TMEP2		(0x0068)
#define EXYNOS4210_TMU_PAST_TMEP3		(0x006C)

/* bit definition at EXYNOS4_TMU_INTEN reg of exynos4210 */
#define INTEN0			(1 << 0)
#define INTEN1			(1 << 4)
#define INTEN2			(1 << 8)
#define INTEN3			(1 << 12)

/* bit definition at EXYNOS4_TMU_INTSTAT reg of exynos4210 */
#define TMU_INTSTAT0		(1 << 0)
#define TMU_INTSTAT1		(1 << 4)
#define TMU_INTSTAT2		(1 << 8)
#define TMU_INTSTAT3		(1 << 12)


/* bit definition at EXYNOS4_TMU_INTCLEAR regof exynos4210 */
#define INTCLEAR0		(1 << 0)
#define INTCLEAR1		(1 << 4)
#define INTCLEAR2		(1 << 8)
#define INTCLEAR3		(1 << 12)

#ifdef CONFIG_CPU_EXYNOS4210
#define INTCLEARALL	(INTCLEAR0 | INTCLEAR1 | INTCLEAR2 | INTCLEAR3)
#else
#define INTCLEARALL	(INTCLEAR_RISE0 | INTCLEAR_RISE1 | INTCLEAR_RISE2 \
			| INTCLEAR_FALL0 | INTCLEAR_FALL1 | INTCLEAR_FALL2)
#endif

/*
 * The below registers and definition is added or changed
 * in EXYNOS4x12
 *
*/
#define EXYNOS4x12_TMU_TRIMINFO_CONROL	(0x0014)
#define TMU_RELOAD			(1 << 0)
#define TMU_ACTIME			(1 << 0)

/* Newly added bit definition at EXYNOS4_TMU_CONTROL register,  */
#define THERM_TRIP_ENABLE	(1 << 12)
#define THERM_TRIP_MODE_000	(0 << 13)
#define THERM_TRIP_MODE_100	(4 << 13)
#define THERM_TRIP_MODE_101	(5 << 13)
#define THERM_TRIP_MODE_110	(6 << 13)
#define THERM_TRIP_MODE_111	(7 << 13)

#define EXYNOS4x12_TMU_TRESHOLD_TEMP_RISE	(0x0050)
#define THRES_LEVEL_RISE0_SHIFT		(1 << 0)
#define THRES_LEVEL_RISE1_SHIFT		(1 << 8)
#define THRES_LEVEL_RISE2_SHIFT		(1 << 16)
#define THRES_LEVEL_RISE3_SHIFT		(1 << 24)

#define EXYNOS4x12_TMU_TRESHOLD_TEMP_FALL	(0x0054)
#define THRES_LEVEL_FALL0_SHIFT		(1 << 0)
#define THRES_LEVEL_FALL1_SHIFT		(1 << 8)
#define THRES_LEVEL_FALL2_SHIFT		(1 << 16)

#define EXYNOS4x12_TMU_PAST_TMEP3_0	(0x0060)
#define EXYNOS4x12_TMU_PAST_TMEP7_4	(0x0064)
#define EXYNOS4x12_TMU_PAST_TMEP11_8	(0x0068)
#define EXYNOS4x12_TMU_PAST_TMEP15_12	(0x006C)

/* Newly changed bit definition at EXYNOS4_TMU_INTEN register,  */
#define INTEN_RISE0			(1 << 0)
#define INTEN_RISE1			(1 << 4)
#define INTEN_RISE2			(1 << 8)
#define INTEN_FALL0			(1 << 16)
#define INTEN_FALL1			(1 << 20)
#define INTEN_FALL2			(1 << 24)

/* Newly changed bit definition at EXYNOS4_TMU_INSTAT */
#define INTSTAT_RISE0		(1 << 0)
#define INTSTAT_RISE1		(1 << 4)
#define INTSTAT_RISE2		(1 << 8)
#define INTSTAT_FALL0		(1 << 16)
#define INTSTAT_FALL1		(1 << 20)
#define INTSTAT_FALL2		(1 << 24)

/* Newly changed bit definition at EXYNOS4_TMU_CLEAR */
#define INTCLEAR_RISE0		(1 << 0)
#define INTCLEAR_RISE1		(1 << 4)
#define INTCLEAR_RISE2		(1 << 8)
#define INTCLEAR_FALL0		(1 << 12)
#define INTCLEAR_FALL1		(1 << 16)
#define INTCLEAR_FALL2		(1 << 20)

#define EXYNOS4x12_TMU_EMUL_CON	(0x0080)

#endif /* ___ASM_ARCH_REGS_TMU_H */
