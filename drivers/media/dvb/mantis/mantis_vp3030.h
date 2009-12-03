/*
	Mantis VP-3030 driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

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

#ifndef __MANTIS_VP3030_H
#define __MANTIS_VP3030_H

#include "dvb_frontend.h"
#include "mantis_common.h"
#include "dvb-pll.h"
#include "zl10353.h"

#define MANTIS_VP_3030_DVB_T	0x0024

extern struct zl10353_config mantis_vp3030_config;
extern struct mantis_hwconfig vp3030_mantis_config;

#endif // __MANTIS_VP3030_H
