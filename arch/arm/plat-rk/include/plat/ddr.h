/*
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __PLAT_DDR_H
#define __PLAT_DDR_H

#include <linux/types.h>
#include <mach/sram.h>

#ifdef CONFIG_DDR_SDRAM_FREQ
#define DDR_FREQ          (CONFIG_DDR_SDRAM_FREQ)
#else
#define DDR_FREQ 360
#endif

#define DDR3_800D   (0)     // 5-5-5
#define DDR3_800E   (1)     // 6-6-6
#define DDR3_1066E  (2)     // 6-6-6
#define DDR3_1066F  (3)     // 7-7-7
#define DDR3_1066G  (4)     // 8-8-8
#define DDR3_1333F  (5)     // 7-7-7
#define DDR3_1333G  (6)     // 8-8-8
#define DDR3_1333H  (7)     // 9-9-9
#define DDR3_1333J  (8)     // 10-10-10
#define DDR3_1600G  (9)     // 8-8-8
#define DDR3_1600H  (10)    // 9-9-9
#define DDR3_1600J  (11)    // 10-10-10
#define DDR3_1600K  (12)    // 11-11-11
#define DDR3_1866J  (13)    // 10-10-10
#define DDR3_1866K  (14)    // 11-11-11
#define DDR3_1866L  (15)    // 12-12-12
#define DDR3_1866M  (16)    // 13-13-13
#define DDR3_2133K  (17)    // 11-11-11
#define DDR3_2133L  (18)    // 12-12-12
#define DDR3_2133M  (19)    // 13-13-13
#define DDR3_2133N  (20)    // 14-14-14
#define DDR3_DEFAULT (21)
#define DDR_DDR2     (22)
#define DDR_LPDDR    (23)
#define DDR_LPDDR2   (24)

#ifdef CONFIG_DDR_TYPE_DDR3_800D
#define DDR_TYPE DDR3_800D
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_800E
#define DDR_TYPE DDR3_800E
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066E
#define DDR_TYPE DDR3_1066E
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066F
#define DDR_TYPE DDR3_1066F
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066G
#define DDR_TYPE DDR3_1066G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333F
#define DDR_TYPE DDR3_1333F
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333G
#define DDR_TYPE DDR3_1333G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333H
#define DDR_TYPE DDR3_1333H
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333J
#define DDR_TYPE DDR3_1333J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600G
#define DDR_TYPE DDR3_1600G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600H
#define DDR_TYPE DDR3_1600H
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600J
#define DDR_TYPE DDR3_1600J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866J
#define DDR_TYPE DDR3_1866J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866K
#define DDR_TYPE DDR3_1866K
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866L
#define DDR_TYPE DDR3_1866L
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866M
#define DDR_TYPE DDR3_1866M
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133K
#define DDR_TYPE DDR3_2133K
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133L
#define DDR_TYPE DDR3_2133L
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133M
#define DDR_TYPE DDR3_2133M
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133N
#define DDR_TYPE DDR3_2133N
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_DEFAULT
#define DDR_TYPE DDR3_DEFAULT
#endif

#ifdef CONFIG_DDR_TYPE_DDRII
#define DDR_TYPE DDR_DDRII
#endif

#ifdef CONFIG_DDR_TYPE_LPDDR
#define DDR_TYPE DDR_LPDDR
#endif

void __sramfunc ddr_suspend(void);
void __sramfunc ddr_resume(void);
//void __sramlocalfunc delayus(uint32_t us);
uint32_t __sramfunc ddr_change_freq(uint32_t nMHz);
int ddr_init(uint32_t dram_type, uint32_t freq);
void ddr_set_auto_self_refresh(bool en);
uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set);


#endif
