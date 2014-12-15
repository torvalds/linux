/*
 * arch/arm/plat-meson/include/mach/cpu.h
 *
 * MESON cpu type detection
 *
 * Copyright (C) 2012 Amlogic
 *
 * Written by Victor Wan <victor.wan@amlogic.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __PLAT_MESON_CPU_H
#define __PLAT_MESON_CPU_H

#define MESON_CPU_TYPE_MESON1		0x10
#define MESON_CPU_TYPE_MESON2		0x20
#define MESON_CPU_TYPE_MESON3		0x30
#define MESON_CPU_TYPE_MESON6		0x60
#define MESON_CPU_TYPE_MESON6TV		0x70
#define MESON_CPU_TYPE_MESON6TVD	0x75
#define MESON_CPU_TYPE_MESON8		0x80
#define MESON_CPU_TYPE_MESON8B		0x8B
#define MESON_CPU_TYPE_MESONG9TV	0x90

/*
 *	Read back value for P_ASSIST_HW_REV
 *
 *	Please note: M8M2 readback value same as M8 (0x19)
 *			     We changed it to 0x1D in software,
 *			     Please ALWAYS use get_meson_cpu_version()
 *			     to get the version of Meson CPU
 */
#define MESON_CPU_MAJOR_ID_M6		0x16
#define MESON_CPU_MAJOR_ID_M6TV		0x17
#define MESON_CPU_MAJOR_ID_M6TVL	0x18
#define MESON_CPU_MAJOR_ID_M8		0x19
#define MESON_CPU_MAJOR_ID_MTVD		0x1A
#define MESON_CPU_MAJOR_ID_M8B		0x1B
#define MESON_CPU_MAJOR_ID_MG9TV	0x1C
#define MESON_CPU_MAJOR_ID_M8M2		0x1D


#define MESON_CPU_VERSION_LVL_MAJOR	0
#define MESON_CPU_VERSION_LVL_MINOR	1
#define MESON_CPU_VERSION_LVL_PACK	2
#define MESON_CPU_VERSION_LVL_MISC	3
#define MESON_CPU_VERSION_LVL_MAX	MESON_CPU_VERSION_LVL_MISC

int  meson_cpu_version_init(void);
int get_meson_cpu_version(int level);

#define IS_MESON_M8_CPU 		(get_meson_cpu_version(MESON_CPU_VERSION_LVL_MAJOR) == MESON_CPU_MAJOR_ID_M8)
#define IS_MESON_MTVD_CPU		(get_meson_cpu_version(MESON_CPU_VERSION_LVL_MAJOR) == MESON_CPU_MAJOR_ID_MTVD)
#define IS_MESON_M8B_CPU		(get_meson_cpu_version(MESON_CPU_VERSION_LVL_MAJOR) == MESON_CPU_MAJOR_ID_M8B)
#define IS_MESON_M8M2_CPU		(get_meson_cpu_version(MESON_CPU_VERSION_LVL_MAJOR) == MESON_CPU_MAJOR_ID_M8M2)
#define IS_MESON_MG9TV_CPU		(get_meson_cpu_version(MESON_CPU_VERSION_LVL_MAJOR) == MESON_CPU_MAJOR_ID_MG9TV)

#endif
