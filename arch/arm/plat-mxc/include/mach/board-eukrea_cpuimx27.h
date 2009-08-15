/*
 * Copyright (C) 2009 Eric Benard - eric@eukrea.com
 *
 * Based on board-pcm038.h which is :
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __ASM_ARCH_MXC_BOARD_EUKREA_CPUIMX27_H__
#define __ASM_ARCH_MXC_BOARD_EUKREA_CPUIMX27_H__

#ifndef __ASSEMBLY__
/*
 * This CPU module needs a baseboard to work. After basic initializing
 * its own devices, it calls baseboard's init function.
 * TODO: Add your own baseboard init function and call it from
 * inside eukrea_cpuimx27_init().
 *
 * This example here is for the development board. Refer
 * eukrea_mbimx27-baseboard.c
 */

extern void eukrea_mbimx27_baseboard_init(void);

#endif

#endif /* __ASM_ARCH_MXC_BOARD_EUKREA_CPUIMX27_H__ */
