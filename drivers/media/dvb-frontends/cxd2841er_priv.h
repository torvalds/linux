/*
 * cxd2841er_priv.h
 *
 * Sony CXD2441ER digital demodulator driver internal definitions
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CXD2841ER_PRIV_H
#define CXD2841ER_PRIV_H

#define I2C_SLVX			0
#define I2C_SLVT			1

#define CXD2837ER_CHIP_ID		0xb1
#define CXD2838ER_CHIP_ID		0xb0
#define CXD2841ER_CHIP_ID		0xa7
#define CXD2843ER_CHIP_ID		0xa4
#define CXD2854ER_CHIP_ID		0xc1

#define CXD2841ER_DVBS_POLLING_INVL	10

struct cxd2841er_cnr_data {
	u32 value;
	int cnr_x1000;
};

enum cxd2841er_dvbt2_profile_t {
	DVBT2_PROFILE_ANY = 0,
	DVBT2_PROFILE_BASE = 1,
	DVBT2_PROFILE_LITE = 2
};

#endif
