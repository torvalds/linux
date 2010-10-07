/*
 *  Samsung s5h1432 VSB/QAM demodulator driver
 *
 *  Copyright (C) 2009 Bill Liu <Bill.Liu@Conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __S5H1432_H__
#define __S5H1432_H__

#include <linux/dvb/frontend.h>

#define S5H1432_I2C_TOP_ADDR (0x02 >> 1)

#define TAIWAN_HI_IF_FREQ_44_MHZ 44000000
#define EUROPE_HI_IF_FREQ_36_MHZ 36000000
#define IF_FREQ_6_MHZ             6000000
#define IF_FREQ_3point3_MHZ       3300000
#define IF_FREQ_3point5_MHZ       3500000
#define IF_FREQ_4_MHZ             4000000

struct s5h1432_config {

	/* serial/parallel output */
#define S5H1432_PARALLEL_OUTPUT 0
#define S5H1432_SERIAL_OUTPUT   1
	u8 output_mode;

	/* GPIO Setting */
#define S5H1432_GPIO_OFF 0
#define S5H1432_GPIO_ON  1
	u8 gpio;

	/* MPEG signal timing */
#define S5H1432_MPEGTIMING_CONTINOUS_INVERTING_CLOCK       0
#define S5H1432_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK    1
#define S5H1432_MPEGTIMING_NONCONTINOUS_INVERTING_CLOCK    2
#define S5H1432_MPEGTIMING_NONCONTINOUS_NONINVERTING_CLOCK 3
	u16 mpeg_timing;

	/* IF Freq for QAM and VSB in KHz */
#define S5H1432_IF_3250  3250
#define S5H1432_IF_3500  3500
#define S5H1432_IF_4000  4000
#define S5H1432_IF_5380  5380
#define S5H1432_IF_44000 44000
#define S5H1432_VSB_IF_DEFAULT s5h1432_IF_44000
#define S5H1432_QAM_IF_DEFAULT s5h1432_IF_44000
	u16 qam_if;
	u16 vsb_if;

	/* Spectral Inversion */
#define S5H1432_INVERSION_OFF 0
#define S5H1432_INVERSION_ON  1
	u8 inversion;

	/* Return lock status based on tuner lock, or demod lock */
#define S5H1432_TUNERLOCKING 0
#define S5H1432_DEMODLOCKING 1
	u8 status_mode;
};

#if defined(CONFIG_DVB_S5H1432) || \
	(defined(CONFIG_DVB_S5H1432_MODULE) && defined(MODULE))
extern struct dvb_frontend *s5h1432_attach(const struct s5h1432_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *s5h1432_attach(const struct s5h1432_config
						  *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_s5h1432 */

#endif /* __s5h1432_H__ */
