/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __ARCH_ARM_MACH_MSM_CPU_H__
#define __ARCH_ARM_MACH_MSM_CPU_H__

/* TODO: For now, only one CPU can be compiled at a time. */

#define cpu_is_msm7x01()	0
#define cpu_is_msm7x30()	0
#define cpu_is_qsd8x50()	0
#define cpu_is_msm8x60()	0
#define cpu_is_msm8960()	0

#ifdef CONFIG_ARCH_MSM7X00A
# undef cpu_is_msm7x01
# define cpu_is_msm7x01()	1
#endif

#ifdef CONFIG_ARCH_MSM7X30
# undef cpu_is_msm7x30
# define cpu_is_msm7x30()	1
#endif

#ifdef CONFIG_ARCH_QSD8X50
# undef cpu_is_qsd8x50
# define cpu_is_qsd8x50()	1
#endif

#ifdef CONFIG_ARCH_MSM8X60
# undef cpu_is_msm8x60
# define cpu_is_msm8x60()	1
#endif

#ifdef CONFIG_ARCH_MSM8960
# undef cpu_is_msm8960
# define cpu_is_msm8960()	1
#endif

#endif
