/*
 * CIMaX SP2/HF CI driver
 *
 * Copyright (C) 2014 Olli Salonen <olli.salonen@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#ifndef SP2_PRIV_H
#define SP2_PRIV_H

#include "sp2.h"
#include "dvb_frontend.h"

/* state struct */
struct sp2 {
	int status;
	struct i2c_client *client;
	struct dvb_adapter *dvb_adap;
	struct dvb_ca_en50221 ca;
	int module_access_type;
	unsigned long next_status_checked_time;
	void *priv;
	void *ci_control;
};

#define SP2_CI_ATTR_ACS		0x00
#define SP2_CI_IO_ACS		0x04
#define SP2_CI_WR		0
#define SP2_CI_RD		1

/* Module control register (0x00 module A, 0x09 module B) bits */
#define SP2_MOD_CTL_DET		0x01
#define SP2_MOD_CTL_AUTO	0x02
#define SP2_MOD_CTL_ACS0	0x04
#define SP2_MOD_CTL_ACS1	0x08
#define SP2_MOD_CTL_HAD		0x10
#define SP2_MOD_CTL_TSIEN	0x20
#define SP2_MOD_CTL_TSOEN	0x40
#define SP2_MOD_CTL_RST		0x80

#endif
