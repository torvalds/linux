/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ITE Tech IT9137 silicon tuner driver
 *
 *  Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 *  IT9137 Copyright (C) ITE Tech Inc.
 */

#ifndef IT913X_H
#define IT913X_H

#include <media/dvb_frontend.h>

/**
 * struct it913x_platform_data - Platform data for the it913x driver
 * @regmap: af9033 demod driver regmap.
 * @fe: af9033 demod driver DVB frontend.
 * @role: Chip role, single or dual configuration.
 */

struct it913x_platform_data {
	struct regmap *regmap;
	struct dvb_frontend *fe;
#define IT913X_ROLE_SINGLE         0
#define IT913X_ROLE_DUAL_MASTER    1
#define IT913X_ROLE_DUAL_SLAVE     2
	unsigned int role:2;
};

#endif
