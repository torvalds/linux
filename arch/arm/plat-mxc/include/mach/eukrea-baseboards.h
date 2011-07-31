/*
 * Copyright (C) 2010 Eric Benard - eric@eukrea.com
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

#ifndef __MACH_EUKREA_BASEBOARDS_H__
#define __MACH_EUKREA_BASEBOARDS_H__

#ifndef __ASSEMBLY__
/*
 * This CPU module needs a baseboard to work. After basic initializing
 * its own devices, it calls baseboard's init function.
 * TODO: Add your own baseboard init function and call it from
 * inside eukrea_cpuimx25_init() eukrea_cpuimx27_init()
 * eukrea_cpuimx35_init() or eukrea_cpuimx51_init().
 *
 * This example here is for the development board. Refer
 * mach-mx25/eukrea_mbimxsd-baseboard.c for cpuimx25
 * mach-imx/eukrea_mbimx27-baseboard.c for cpuimx27
 * mach-mx3/eukrea_mbimxsd-baseboard.c for cpuimx35
 * mach-mx5/eukrea_mbimx51-baseboard.c for cpuimx51
 */

extern void eukrea_mbimxsd25_baseboard_init(void);
extern void eukrea_mbimx27_baseboard_init(void);
extern void eukrea_mbimxsd35_baseboard_init(void);
extern void eukrea_mbimx51_baseboard_init(void);

#endif

#endif /* __MACH_EUKREA_BASEBOARDS_H__ */
