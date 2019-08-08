/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for the internal tuner of Montage M88RS6000
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
 */

#ifndef _M88RS6000T_H_
#define _M88RS6000T_H_

#include <media/dvb_frontend.h>

struct m88rs6000t_config {
	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;
};

#endif
