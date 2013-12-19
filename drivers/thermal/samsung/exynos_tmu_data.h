/*
 * exynos_tmu_data.h - Samsung EXYNOS tmu data header file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _EXYNOS_TMU_DATA_H
#define _EXYNOS_TMU_DATA_H

/* Exynos generic registers */
#define EXYNOS_TMU_REG_TRIMINFO		0x0
#define EXYNOS_TMU_REG_CONTROL		0x20
#define EXYNOS_TMU_REG_STATUS		0x28
#define EXYNOS_TMU_REG_CURRENT_TEMP	0x40
#define EXYNOS_TMU_REG_INTEN		0x70
#define EXYNOS_TMU_REG_INTSTAT		0x74
#define EXYNOS_TMU_REG_INTCLEAR		0x78

#define EXYNOS_TMU_TEMP_MASK		0xff
#define EXYNOS_TMU_REF_VOLTAGE_SHIFT	24
#define EXYNOS_TMU_REF_VOLTAGE_MASK	0x1f
#define EXYNOS_TMU_BUF_SLOPE_SEL_MASK	0xf
#define EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT	8
#define EXYNOS_TMU_CORE_EN_SHIFT	0

/* Exynos4210 specific registers */
#define EXYNOS4210_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYNOS4210_TMU_REG_TRIG_LEVEL0	0x50
#define EXYNOS4210_TMU_REG_TRIG_LEVEL1	0x54
#define EXYNOS4210_TMU_REG_TRIG_LEVEL2	0x58
#define EXYNOS4210_TMU_REG_TRIG_LEVEL3	0x5C
#define EXYNOS4210_TMU_REG_PAST_TEMP0	0x60
#define EXYNOS4210_TMU_REG_PAST_TEMP1	0x64
#define EXYNOS4210_TMU_REG_PAST_TEMP2	0x68
#define EXYNOS4210_TMU_REG_PAST_TEMP3	0x6C

#define EXYNOS4210_TMU_TRIG_LEVEL0_MASK	0x1
#define EXYNOS4210_TMU_TRIG_LEVEL1_MASK	0x10
#define EXYNOS4210_TMU_TRIG_LEVEL2_MASK	0x100
#define EXYNOS4210_TMU_TRIG_LEVEL3_MASK	0x1000
#define EXYNOS4210_TMU_TRIG_LEVEL_MASK	0x1111
#define EXYNOS4210_TMU_INTCLEAR_VAL	0x1111

/* Exynos5250 and Exynos4412 specific registers */
#define EXYNOS_TMU_TRIMINFO_CON	0x14
#define EXYNOS_THD_TEMP_RISE		0x50
#define EXYNOS_THD_TEMP_FALL		0x54
#define EXYNOS_EMUL_CON		0x80

#define EXYNOS_TRIMINFO_RELOAD_SHIFT	1
#define EXYNOS_TRIMINFO_25_SHIFT	0
#define EXYNOS_TRIMINFO_85_SHIFT	8
#define EXYNOS_TMU_RISE_INT_MASK	0x111
#define EXYNOS_TMU_RISE_INT_SHIFT	0
#define EXYNOS_TMU_FALL_INT_MASK	0x111
#define EXYNOS_TMU_FALL_INT_SHIFT	12
#define EXYNOS_TMU_CLEAR_RISE_INT	0x111
#define EXYNOS_TMU_CLEAR_FALL_INT	(0x111 << 12)
#define EXYNOS_TMU_TRIP_MODE_SHIFT	13
#define EXYNOS_TMU_TRIP_MODE_MASK	0x7
#define EXYNOS_TMU_THERM_TRIP_EN_SHIFT	12
#define EXYNOS_TMU_CALIB_MODE_SHIFT	4
#define EXYNOS_TMU_CALIB_MODE_MASK	0x3

#define EXYNOS_TMU_INTEN_RISE0_SHIFT	0
#define EXYNOS_TMU_INTEN_RISE1_SHIFT	4
#define EXYNOS_TMU_INTEN_RISE2_SHIFT	8
#define EXYNOS_TMU_INTEN_RISE3_SHIFT	12
#define EXYNOS_TMU_INTEN_FALL0_SHIFT	16
#define EXYNOS_TMU_INTEN_FALL1_SHIFT	20
#define EXYNOS_TMU_INTEN_FALL2_SHIFT	24

#define EXYNOS_EMUL_TIME	0x57F0
#define EXYNOS_EMUL_TIME_MASK	0xffff
#define EXYNOS_EMUL_TIME_SHIFT	16
#define EXYNOS_EMUL_DATA_SHIFT	8
#define EXYNOS_EMUL_DATA_MASK	0xFF
#define EXYNOS_EMUL_ENABLE	0x1

#define EXYNOS_MAX_TRIGGER_PER_REG	4

/* Exynos4412 specific */
#define EXYNOS4412_MUX_ADDR_VALUE          6
#define EXYNOS4412_MUX_ADDR_SHIFT          20

/*exynos5440 specific registers*/
#define EXYNOS5440_TMU_S0_7_TRIM		0x000
#define EXYNOS5440_TMU_S0_7_CTRL		0x020
#define EXYNOS5440_TMU_S0_7_DEBUG		0x040
#define EXYNOS5440_TMU_S0_7_STATUS		0x060
#define EXYNOS5440_TMU_S0_7_TEMP		0x0f0
#define EXYNOS5440_TMU_S0_7_TH0			0x110
#define EXYNOS5440_TMU_S0_7_TH1			0x130
#define EXYNOS5440_TMU_S0_7_TH2			0x150
#define EXYNOS5440_TMU_S0_7_EVTEN		0x1F0
#define EXYNOS5440_TMU_S0_7_IRQEN		0x210
#define EXYNOS5440_TMU_S0_7_IRQ			0x230
/* exynos5440 common registers */
#define EXYNOS5440_TMU_IRQ_STATUS		0x000
#define EXYNOS5440_TMU_PMIN			0x004
#define EXYNOS5440_TMU_TEMP			0x008

#define EXYNOS5440_TMU_RISE_INT_MASK		0xf
#define EXYNOS5440_TMU_RISE_INT_SHIFT		0
#define EXYNOS5440_TMU_FALL_INT_MASK		0xf
#define EXYNOS5440_TMU_FALL_INT_SHIFT		4
#define EXYNOS5440_TMU_INTEN_RISE0_SHIFT	0
#define EXYNOS5440_TMU_INTEN_RISE1_SHIFT	1
#define EXYNOS5440_TMU_INTEN_RISE2_SHIFT	2
#define EXYNOS5440_TMU_INTEN_RISE3_SHIFT	3
#define EXYNOS5440_TMU_INTEN_FALL0_SHIFT	4
#define EXYNOS5440_TMU_INTEN_FALL1_SHIFT	5
#define EXYNOS5440_TMU_INTEN_FALL2_SHIFT	6
#define EXYNOS5440_TMU_INTEN_FALL3_SHIFT	7
#define EXYNOS5440_TMU_TH_RISE0_SHIFT		0
#define EXYNOS5440_TMU_TH_RISE1_SHIFT		8
#define EXYNOS5440_TMU_TH_RISE2_SHIFT		16
#define EXYNOS5440_TMU_TH_RISE3_SHIFT		24
#define EXYNOS5440_TMU_TH_RISE4_SHIFT		24
#define EXYNOS5440_EFUSE_SWAP_OFFSET		8

#if defined(CONFIG_CPU_EXYNOS4210)
extern struct exynos_tmu_init_data const exynos4210_default_tmu_data;
#define EXYNOS4210_TMU_DRV_DATA (&exynos4210_default_tmu_data)
#else
#define EXYNOS4210_TMU_DRV_DATA (NULL)
#endif

#if defined(CONFIG_SOC_EXYNOS4412)
extern struct exynos_tmu_init_data const exynos4412_default_tmu_data;
#define EXYNOS4412_TMU_DRV_DATA (&exynos4412_default_tmu_data)
#else
#define EXYNOS4412_TMU_DRV_DATA (NULL)
#endif

#if defined(CONFIG_SOC_EXYNOS5250)
extern struct exynos_tmu_init_data const exynos5250_default_tmu_data;
#define EXYNOS5250_TMU_DRV_DATA (&exynos5250_default_tmu_data)
#else
#define EXYNOS5250_TMU_DRV_DATA (NULL)
#endif

#if defined(CONFIG_SOC_EXYNOS5440)
extern struct exynos_tmu_init_data const exynos5440_default_tmu_data;
#define EXYNOS5440_TMU_DRV_DATA (&exynos5440_default_tmu_data)
#else
#define EXYNOS5440_TMU_DRV_DATA (NULL)
#endif

#endif /*_EXYNOS_TMU_DATA_H*/
