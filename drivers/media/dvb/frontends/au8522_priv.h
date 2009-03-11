/*
    Auvitek AU8522 QAM/8VSB demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>
    Copyright (C) 2008 Devin Heitmueller <dheitmueller@linuxtv.org>
    Copyright (C) 2005-2008 Auvitek International, Ltd.

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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <linux/i2c.h>
#include "dvb_frontend.h"
#include "au8522.h"
#include "tuner-i2c.h"

struct au8522_state {
	struct i2c_adapter *i2c;

	/* Used for sharing of the state between analog and digital mode */
	struct tuner_i2c_props i2c_props;
	struct list_head hybrid_tuner_instance_list;

	/* configuration settings */
	const struct au8522_config *config;

	struct dvb_frontend frontend;

	u32 current_frequency;
	fe_modulation_t current_modulation;

	u32 fe_status;
	unsigned int led_state;
};

/* These are routines shared by both the VSB/QAM demodulator and the analog
   decoder */
int au8522_writereg(struct au8522_state *state, u16 reg, u8 data);
u8 au8522_readreg(struct au8522_state *state, u16 reg);
int au8522_init(struct dvb_frontend *fe);
int au8522_sleep(struct dvb_frontend *fe);

int au8522_get_state(struct au8522_state **state, struct i2c_adapter *i2c,
		     u8 client_address);
void au8522_release_state(struct au8522_state *state);
