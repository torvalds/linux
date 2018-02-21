/*
	Mantis VP-3028 driver

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

#ifndef __MANTIS_VP3028_H
#define __MANTIS_VP3028_H

#include <media/dvb_frontend.h>
#include "mantis_common.h"
#include "zl10353.h"

#define MANTIS_VP_3028_DVB_T	0x0028

extern struct zl10353_config mantis_vp3028_config;
extern struct mantis_hwconfig vp3028_mantis_config;

#endif /* __MANTIS_VP3028_H */
