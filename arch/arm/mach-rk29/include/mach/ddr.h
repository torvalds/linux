/* arch/arm/mach-rk29/include/mach/ddr.h
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

#ifndef __ARCH_ARM_MACH_RK29_DDR_H
#define __ARCH_ARM_MACH_RK29_DDR_H

#include <linux/types.h>
#include <mach/sram.h>

void __sramfunc ddr_suspend(void);
void __sramfunc ddr_resume(void);
void __sramlocalfunc delayus(uint32_t us);
void __sramfunc ddr_change_freq(uint32_t nMHz);

#endif
