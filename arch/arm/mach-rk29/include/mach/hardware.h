/* arch/arm/mach-rk29/include/mach/hardware.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#ifndef __ASM_ARCH_RK29_HARDWARE_H


#ifndef __ASSEMBLY__
# define __REG(x)       (*((volatile u32 *)IO_ADDRESS(x)))

# define __REG2(x,y)        (*(volatile u32 *)((u32)&__REG(x) + (y)))
#endif

#endif
