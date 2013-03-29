/*
	STV0900/0903 Multistandard Broadcast Frontend driver
	Copyright (C) Manu Abraham <abraham.manu@gmail.com>

	Copyright (C) ST Microelectronics

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __STV090x_H
#define __STV090x_H

enum stv090x_demodulator {
	STV090x_DEMODULATOR_0 = 1,
	STV090x_DEMODULATOR_1
};

enum stv090x_device {
	STV0903	=  0,
	STV0900,
};

enum stv090x_mode {
	STV090x_DUAL = 0,
	STV090x_SINGLE
};

enum stv090x_tsmode {
	STV090x_TSMODE_SERIAL_PUNCTURED	= 1,
	STV090x_TSMODE_SERIAL_CONTINUOUS,
	STV090x_TSMODE_PARALLEL_PUNCTURED,
	STV090x_TSMODE_DVBCI
};

enum stv090x_clkmode {
	STV090x_CLK_INT = 0, /* Clk i/p = CLKI */
	STV090x_CLK_EXT = 2 /* Clk i/p = XTALI */
};

enum stv090x_i2crpt {
	STV090x_RPTLEVEL_256	= 0,
	STV090x_RPTLEVEL_128	= 1,
	STV090x_RPTLEVEL_64	= 2,
	STV090x_RPTLEVEL_32	= 3,
	STV090x_RPTLEVEL_16	= 4,
	STV090x_RPTLEVEL_8	= 5,
	STV090x_RPTLEVEL_4	= 6,
	STV090x_RPTLEVEL_2	= 7,
};

enum stv090x_adc_range {
	STV090x_ADC_2Vpp	= 0,
	STV090x_ADC_1Vpp	= 1
};

struct stv090x_config {
	enum stv090x_device	device;
	enum stv090x_mode	demod_mode;
	enum stv090x_clkmode	clk_mode;

	u32 xtal; /* default: 8000000 */
	u8 address; /* default: 0x68 */

	u8 ts1_mode;
	u8 ts2_mode;
	u32 ts1_clk;
	u32 ts2_clk;

	u8 ts1_tei : 1;
	u8 ts2_tei : 1;

	enum stv090x_i2crpt	repeater_level;

	u8			tuner_bbgain; /* default: 10db */
	enum stv090x_adc_range	adc1_range; /* default: 2Vpp */
	enum stv090x_adc_range	adc2_range; /* default: 2Vpp */

	bool diseqc_envelope_mode;

	int (*tuner_init) (struct dvb_frontend *fe);
	int (*tuner_sleep) (struct dvb_frontend *fe);
	int (*tuner_set_mode) (struct dvb_frontend *fe, enum tuner_mode mode);
	int (*tuner_set_frequency) (struct dvb_frontend *fe, u32 frequency);
	int (*tuner_get_frequency) (struct dvb_frontend *fe, u32 *frequency);
	int (*tuner_set_bandwidth) (struct dvb_frontend *fe, u32 bandwidth);
	int (*tuner_get_bandwidth) (struct dvb_frontend *fe, u32 *bandwidth);
	int (*tuner_set_bbgain) (struct dvb_frontend *fe, u32 gain);
	int (*tuner_get_bbgain) (struct dvb_frontend *fe, u32 *gain);
	int (*tuner_set_refclk)  (struct dvb_frontend *fe, u32 refclk);
	int (*tuner_get_status) (struct dvb_frontend *fe, u32 *status);
	void (*tuner_i2c_lock) (struct dvb_frontend *fe, int lock);
};

#if IS_ENABLED(CONFIG_DVB_STV090x)

extern struct dvb_frontend *stv090x_attach(const struct stv090x_config *config,
					   struct i2c_adapter *i2c,
					   enum stv090x_demodulator demod);

/* dir = 0 -> output, dir = 1 -> input/open-drain */
extern int stv090x_set_gpio(struct dvb_frontend *fe, u8 gpio,
		u8 dir, u8 value, u8 xor_value);

#else

static inline struct dvb_frontend *stv090x_attach(const struct stv090x_config *config,
						  struct i2c_adapter *i2c,
						  enum stv090x_demodulator demod)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int stv090x_set_gpio(struct dvb_frontend *fe, u8 gpio,
		u8 opd, u8 value, u8 xor_value)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
#endif /* CONFIG_DVB_STV090x */

#endif /* __STV090x_H */
