/*
* arch/arm/mach-meson6tv/include/mach/cpu.h
*
* Copyright (C) 2012-2013 Amlogic, Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __MACH_MESON6TV_CPU_H
#define __MACH_MESON6TV_CPU_H

#include <plat/cpu.h>

extern int (*get_cpu_temperature_celius)(void);
int get_cpu_temperature(void);

#define MESON_CPU_TYPE	MESON_CPU_TYPE_MESON6TV

extern int meson6tv_cpu_kill(unsigned int cpu);
extern void meson6tv_cpu_die(unsigned int cpu);
extern int meson6tv_cpu_disable(unsigned int cpu);

#endif /* __MACH_MESON6TV_CPU_H */
