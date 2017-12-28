/*
 * MaxLinear MxL301RF OFDM tuner driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MXL301RF_H
#define MXL301RF_H

#include <media/dvb_frontend.h>

struct mxl301rf_config {
	struct dvb_frontend *fe;
};

#endif /* MXL301RF_H */
