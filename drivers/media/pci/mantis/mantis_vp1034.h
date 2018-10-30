/*
	Mantis VP-1034 driver

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

#ifndef __MANTIS_VP1034_H
#define __MANTIS_VP1034_H

#include <media/dvb_frontend.h>
#include "mantis_common.h"


#define MANTIS_VP_1034_DVB_S	0x0014

extern struct mantis_hwconfig vp1034_config;
extern int vp1034_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage);

#endif /* __MANTIS_VP1034_H */
