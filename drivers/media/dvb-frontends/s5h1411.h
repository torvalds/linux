/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Samsung S5H1411 VSB/QAM demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>


*/

#ifndef __S5H1411_H__
#define __S5H1411_H__

#include <linux/dvb/frontend.h>

#define S5H1411_I2C_TOP_ADDR (0x32 >> 1)
#define S5H1411_I2C_QAM_ADDR (0x34 >> 1)

struct s5h1411_config {

	/* serial/parallel output */
#define S5H1411_PARALLEL_OUTPUT 0
#define S5H1411_SERIAL_OUTPUT   1
	u8 output_mode;

	/* GPIO Setting */
#define S5H1411_GPIO_OFF 0
#define S5H1411_GPIO_ON  1
	u8 gpio;

	/* MPEG signal timing */
#define S5H1411_MPEGTIMING_CONTINUOUS_INVERTING_CLOCK       0
#define S5H1411_MPEGTIMING_CONTINUOUS_NONINVERTING_CLOCK    1
#define S5H1411_MPEGTIMING_NONCONTINUOUS_INVERTING_CLOCK    2
#define S5H1411_MPEGTIMING_NONCONTINUOUS_NONINVERTING_CLOCK 3
	u16 mpeg_timing;

	/* IF Freq for QAM and VSB in KHz */
#define S5H1411_IF_3250  3250
#define S5H1411_IF_3500  3500
#define S5H1411_IF_4000  4000
#define S5H1411_IF_5380  5380
#define S5H1411_IF_44000 44000
#define S5H1411_VSB_IF_DEFAULT S5H1411_IF_44000
#define S5H1411_QAM_IF_DEFAULT S5H1411_IF_44000
	u16 qam_if;
	u16 vsb_if;

	/* Spectral Inversion */
#define S5H1411_INVERSION_OFF 0
#define S5H1411_INVERSION_ON  1
	u8 inversion;

	/* Return lock status based on tuner lock, or demod lock */
#define S5H1411_TUNERLOCKING 0
#define S5H1411_DEMODLOCKING 1
	u8 status_mode;
};

#if IS_REACHABLE(CONFIG_DVB_S5H1411)
extern struct dvb_frontend *s5h1411_attach(const struct s5h1411_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *s5h1411_attach(
	const struct s5h1411_config *config,
	struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_S5H1411 */

#endif /* __S5H1411_H__ */
