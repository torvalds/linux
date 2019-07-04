/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux-DVB Driver for DiBcom's DiB0070 base-band RF Tuner.
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 */
#ifndef DIB0070_H
#define DIB0070_H

struct dvb_frontend;
struct i2c_adapter;

#define DEFAULT_DIB0070_I2C_ADDRESS 0x60

struct dib0070_wbd_gain_cfg {
	u16 freq;
	u16 wbd_gain_val;
};

struct dib0070_config {
	u8 i2c_address;

	/* tuner pins controlled externally */
	int (*reset) (struct dvb_frontend *, int);
	int (*sleep) (struct dvb_frontend *, int);

	/*  offset in kHz */
	int freq_offset_khz_uhf;
	int freq_offset_khz_vhf;

	u8 osc_buffer_state;	/* 0= normal, 1= tri-state */
	u32 clock_khz;
	u8 clock_pad_drive;	/* (Drive + 1) * 2mA */

	u8 invert_iq;		/* invert Q - in case I or Q is inverted on the board */

	u8 force_crystal_mode;	/* if == 0 -> decision is made in the driver default: <24 -> 2, >=24 -> 1 */

	u8 flip_chip;
	u8 enable_third_order_filter;
	u8 charge_pump;

	const struct dib0070_wbd_gain_cfg *wbd_gain;

	u8 vga_filter;
};

#if IS_REACHABLE(CONFIG_DVB_TUNER_DIB0070)
extern struct dvb_frontend *dib0070_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct dib0070_config *cfg);
extern u16 dib0070_wbd_offset(struct dvb_frontend *);
extern void dib0070_ctrl_agc_filter(struct dvb_frontend *, u8 open);
extern u8 dib0070_get_rf_output(struct dvb_frontend *fe);
extern int dib0070_set_rf_output(struct dvb_frontend *fe, u8 no);
#else
static inline struct dvb_frontend *dib0070_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct dib0070_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline u16 dib0070_wbd_offset(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}

static inline void dib0070_ctrl_agc_filter(struct dvb_frontend *fe, u8 open)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
}
#endif

#endif
