/*
 * Bt8xx based DVB adapter driver
 *
 * Copyright (C) 2002,2003 Florian Schirmer <jolt@tuxbox.org>
 * Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de>
 * Copyright (C) 1999-2001 Ralph  Metzler & Marcus Metzler for convergence integrated media GmbH
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef DVB_BT8XX_H
#define DVB_BT8XX_H

#include <linux/i2c.h>
#include "dvbdev.h"
#include "dvb_net.h"
#include "bttv.h"
#include "mt352.h"
#include "sp887x.h"
#include "dst_common.h"
#include "nxt6000.h"
#include "cx24110.h"
#include "or51211.h"

struct dvb_bt8xx_card {
	struct semaphore lock;
	int nfeeds;
	char card_name[32];
	struct dvb_adapter dvb_adapter;
	struct bt878 *bt;
	unsigned int bttv_nr;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	u32 gpio_mode;
	u32 op_sync_orin;
	u32 irq_err_ignore;
	struct i2c_adapter *i2c_adapter;
	struct dvb_net dvbnet;

	struct dvb_frontend* fe;
};

#endif /* DVB_BT8XX_H */
