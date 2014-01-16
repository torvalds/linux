/*
 *  Driver for Micronas DRX39xx family (drx3933j)
 *
 *  Written by Devin Heitmueller <devin.heitmueller@kernellabs.com>
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

#ifndef DRX39XXJ_H
#define DRX39XXJ_H

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"
#include "drx_driver.h"

struct drx39xxj_state {
	struct i2c_adapter *i2c;
	DRXDemodInstance_t *demod;
	enum drx_standard current_standard;
	struct dvb_frontend frontend;
	int powered_up:1;
	unsigned int i2c_gate_open:1;
};

extern struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c);

#endif /* DVB_DUMMY_FE_H */
