/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Micronas DRX39xx family (drx3933j)
 *
 *  Written by Devin Heitmueller <devin.heitmueller@kernellabs.com>
 */

#ifndef DRX39XXJ_H
#define DRX39XXJ_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>
#include "drx_driver.h"

struct drx39xxj_state {
	struct i2c_adapter *i2c;
	struct drx_demod_instance *demod;
	struct dvb_frontend frontend;
	unsigned int i2c_gate_open:1;
	const struct firmware *fw;
};

#if IS_REACHABLE(CONFIG_DVB_DRX39XYJ)
struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c) {
	return NULL;
};
#endif

#endif /* DVB_DUMMY_FE_H */
