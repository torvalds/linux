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

#include "mantis_common.h"
#include "mantis_vp2040.h"

#define MANTIS_MODEL_NAME	"VP-2040"
#define MANTIS_DEV_TYPE		"DVB-C"

struct mantis_hwconfig vp2040_mantis_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_204,
};

struct tda1002x_config tda10023_cu1216_config = {
	.demod_address = 0x18 >> 1,
	.invert = 1,
};
