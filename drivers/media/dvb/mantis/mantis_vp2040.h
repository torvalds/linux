/*
	Mantis VP-2040 driver

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

#ifndef __MANTIS_VP2040_H
#define __MANTIS_VP2040_H

#include "dvb_frontend.h"
#include "mantis_common.h"
#include "tda1002x.h"

#define MANTIS_VP_2040_DVB_C	0x0043
#define TERRATEC_CINERGY_C_PCI	0x1178
#define TECHNISAT_CABLESTAR_HD2	0x0002

extern struct tda10023_config tda10023_cu1216_config;
extern struct mantis_hwconfig vp2040_mantis_config;

#endif //__MANTIS_VP2040_H
