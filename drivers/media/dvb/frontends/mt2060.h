/*
 *  Driver for Microtune MT2060 "Single chip dual conversion broadband tuner"
 *
 *  Copyright (c) 2006 Olivier DANET <odanet@caramail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef MT2060_H
#define MT2060_H

#include <linux/i2c.h>
#include <linux/dvb/frontend.h>

struct mt2060_config {
	u8 i2c_address;
	/* Shall we add settings for the discrete outputs ? */
};

struct mt2060_state {
	struct mt2060_config *config;
	struct i2c_adapter *i2c;
	u16 if1_freq;
	u8 fmfreq;
};

extern int mt2060_init(struct mt2060_state *state);
extern int mt2060_set(struct mt2060_state *state, struct dvb_frontend_parameters *fep);
extern int mt2060_attach(struct mt2060_state *state, struct mt2060_config *config, struct i2c_adapter *i2c,u16 if1);

#endif
