/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Driver for VES1893 and VES1993 QPSK Demodulators

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2001 Ronny Strutz <3des@elitedvb.de>
    Copyright (C) 2002 Dennis Noermann <dennis.noermann@noernet.de>
    Copyright (C) 2002-2003 Andreas Oberritter <obi@linuxtv.org>


*/

#ifndef VES1X93_H
#define VES1X93_H

#include <linux/dvb/frontend.h>

struct ves1x93_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* value of XIN to use */
	u32 xin;

	/* should PWM be inverted? */
	u8 invert_pwm:1;
};

#if IS_REACHABLE(CONFIG_DVB_VES1X93)
extern struct dvb_frontend* ves1x93_attach(const struct ves1x93_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* ves1x93_attach(const struct ves1x93_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_VES1X93

#endif // VES1X93_H
