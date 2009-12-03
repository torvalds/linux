/*
	Mantis VP-1033 driver

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

#ifndef __MANTIS_VP1033_H
#define __MANTIS_VP1033_H

#include "dvb_frontend.h"
#include "mantis_common.h"
#include "stv0299.h"

#define MANTIS_VP_1033_DVB_S	0x0016

extern struct stv0299_config lgtdqcs001f_config;
extern struct mantis_hwconfig vp1033_mantis_config;

extern int lgtdqcs001f_tuner_set(struct dvb_frontend *fe,
				 struct dvb_frontend_parameters *params);

extern int lgtdqcs001f_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio);


#endif // __MANTIS_VP1033_H
