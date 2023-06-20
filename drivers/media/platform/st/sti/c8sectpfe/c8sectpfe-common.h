/* SPDX-License-Identifier: GPL-2.0 */
/*
 * c8sectpfe-common.h - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *   Author: Peter Griffin <peter.griffin@linaro.org>
 *
 */
#ifndef _C8SECTPFE_COMMON_H_
#define _C8SECTPFE_COMMON_H_

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/gpio.h>

#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

/* Maximum number of channels */
#define C8SECTPFE_MAXADAPTER (4)
#define C8SECTPFE_MAXCHANNEL 64
#define STPTI_MAXCHANNEL 64

#define MAX_INPUTBLOCKS 7

struct c8sectpfe;
struct stdemux;

struct stdemux {
	struct dvb_demux	dvb_demux;
	struct dmxdev		dmxdev;
	struct dmx_frontend	hw_frontend;
	struct dmx_frontend	mem_frontend;
	int			tsin_index;
	int			running_feed_count;
	struct			c8sectpfei *c8sectpfei;
};

struct c8sectpfe {
	struct stdemux demux[MAX_INPUTBLOCKS];
	struct mutex lock;
	struct dvb_adapter adapter;
	struct device *device;
	int mapping;
	int num_feeds;
};

/* Channel registration */
int c8sectpfe_tuner_register_frontend(struct c8sectpfe **c8sectpfe,
					struct c8sectpfei *fei,
					void *start_feed,
					void *stop_feed);

void c8sectpfe_tuner_unregister_frontend(struct c8sectpfe *c8sectpfe,
						struct c8sectpfei *fei);

#endif
