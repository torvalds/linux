/*
	Mantis VP-1041 driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __MANTIS_VP1041_H
#define __MANTIS_VP1041_H

#include "mantis_common.h"

#define MANTIS_VP_1041_DVB_S2	0x0031
#define SKYSTAR_HD2_10		0x0001
#define SKYSTAR_HD2_20		0x0003
#define CINERGY_S2_PCI_HD	0x1179

extern struct mantis_hwconfig vp1041_config;

#endif /* __MANTIS_VP1041_H */
