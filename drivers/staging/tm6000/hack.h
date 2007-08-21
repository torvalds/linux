/*
   hack.h - hackish code that needs to be improved (or removed) at a
	    later point

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef HACK_H
#define HACK_H

#include <linux/i2c.h>

#include "zl10353.h"
#include "dvb_frontend.h"

struct tm6000_core;

int pseudo_zl103530_init(struct dvb_frontend *fe);

int pseudo_zl10353_set_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *p);

int pseudo_zl10353_read_status(struct dvb_frontend *fe, fe_status_t *status);

int pseudo_zl10353_read_signal_strength(struct dvb_frontend* fe, u16* strength);

int pseudo_zl10353_read_snr(struct dvb_frontend *fe, u16 *snr);

struct dvb_frontend* pseudo_zl10353_attach(struct tm6000_core *dev,
					   const struct zl10353_config *config,
								   struct i2c_adapter *i2c);

#endif
