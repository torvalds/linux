/*
 * Linux-DVB Driver for DiBcom's DiB0090 base-band RF Tuner.
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#ifndef DIB0090_H
#define DIB0090_H

struct dvb_frontend;
struct i2c_adapter;

#define DEFAULT_DIB0090_I2C_ADDRESS 0x60

struct dib0090_io_config {
	u32 clock_khz;

	u8 pll_bypass:1;
	u8 pll_range:1;
	u8 pll_prediv:6;
	u8 pll_loopdiv:6;

	u8 adc_clock_ratio;	/* valid is 8, 7 ,6 */
	u16 pll_int_loop_filt;
};

struct dib0090_config {
	struct dib0090_io_config io;
	int (*reset) (struct dvb_frontend *, int);
	int (*sleep) (struct dvb_frontend *, int);

	/*  offset in kHz */
	int freq_offset_khz_uhf;
	int freq_offset_khz_vhf;

	int (*get_adc_power) (struct dvb_frontend *);

	u8 clkouttobamse:1;	/* activate or deactivate clock output */
	u8 analog_output;

	u8 i2c_address;
	/* add drives and other things if necessary */
	u16 wbd_vhf_offset;
	u16 wbd_cband_offset;
	u8 use_pwm_agc;
	u8 clkoutdrive;
};

#if defined(CONFIG_DVB_TUNER_DIB0090) || (defined(CONFIG_DVB_TUNER_DIB0090_MODULE) && defined(MODULE))
extern struct dvb_frontend *dib0090_register(struct dvb_frontend *fe, struct i2c_adapter *i2c, const struct dib0090_config *config);
extern void dib0090_dcc_freq(struct dvb_frontend *fe, u8 fast);
extern void dib0090_pwm_gain_reset(struct dvb_frontend *fe);
extern u16 dib0090_get_wbd_offset(struct dvb_frontend *tuner);
extern int dib0090_gain_control(struct dvb_frontend *fe);
extern enum frontend_tune_state dib0090_get_tune_state(struct dvb_frontend *fe);
extern int dib0090_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state);
extern void dib0090_get_current_gain(struct dvb_frontend *fe, u16 * rf, u16 * bb, u16 * rf_gain_limit, u16 * rflt);
#else
static inline struct dvb_frontend *dib0090_register(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct dib0090_config *config)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline void dib0090_dcc_freq(struct dvb_frontend *fe, u8 fast)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
}

static inline void dib0090_pwm_gain_reset(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
}

static inline u16 dib0090_get_wbd_offset(struct dvb_frontend *tuner)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}

static inline int dib0090_gain_control(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline enum frontend_tune_state dib0090_get_tune_state(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return CT_DONE;
}

static inline int dib0090_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline void dib0090_get_current_gain(struct dvb_frontend *fe, u16 * rf, u16 * bb, u16 * rf_gain_limit, u16 * rflt)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
}
#endif

#endif
