/*
 * c8sectpfe-common.h - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *   Author: Peter Griffin <peter.griffin@linaro.org>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 */
#ifndef _C8SECTPFE_DVB_H_
#define _C8SECTPFE_DVB_H_

int c8sectpfe_frontend_attach(struct dvb_frontend **fe,
			struct c8sectpfe *c8sectpfe, struct channel_info *tsin,
			int chan_num);

#endif
