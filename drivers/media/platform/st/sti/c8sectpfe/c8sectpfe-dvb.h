/* SPDX-License-Identifier: GPL-2.0 */
/*
 * c8sectpfe-common.h - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *   Author: Peter Griffin <peter.griffin@linaro.org>
 *
 */
#ifndef _C8SECTPFE_DVB_H_
#define _C8SECTPFE_DVB_H_

int c8sectpfe_frontend_attach(struct dvb_frontend **fe,
			struct c8sectpfe *c8sectpfe, struct channel_info *tsin,
			int chan_num);

#endif
