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

#include "dvb_frontend.h"
#include "mantis_common.h"
#include "stb0899_drv.h"
#include "stb6100.h"
#include "lnbp21.h"

#define MANTIS_VP_1041_DVB_S2	0x0031
#define TECHNISAT_SKYSTAR_HD2	0x0001

extern struct mantis_hwconfig vp1041_mantis_config;
extern struct stb0899_config vp1041_config;
extern struct stb6100_config vp1041_stb6100_config;

#endif // __MANTIS_VP1041_H
