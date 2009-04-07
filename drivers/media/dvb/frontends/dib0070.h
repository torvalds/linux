/*
 * Linux-DVB Driver for DiBcom's DiB0070 base-band RF Tuner.
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#ifndef DIB0070_H
#define DIB0070_H

struct dvb_frontend;
struct i2c_adapter;

#define DEFAULT_DIB0070_I2C_ADDRESS 0x60

struct dib0070_config {
	u8 i2c_address;

	/* tuner pins controlled externally */
	int (*reset) (struct dvb_frontend *, int);
	int (*sleep) (struct dvb_frontend *, int);

	/*  offset in kHz */
	int freq_offset_khz_uhf;
	int freq_offset_khz_vhf;

	u8 osc_buffer_state; /* 0= normal, 1= tri-state */
	u32  clock_khz;
	u8 clock_pad_drive; /* (Drive + 1) * 2mA */

	u8 invert_iq; /* invert Q - in case I or Q is inverted on the board */

	u8 force_crystal_mode; /* if == 0 -> decision is made in the driver default: <24 -> 2, >=24 -> 1 */

	u8 flip_chip;
};

#if defined(CONFIG_DVB_TUNER_DIB0070) || (defined(CONFIG_DVB_TUNER_DIB0070_MODULE) && defined(MODULE))
extern struct dvb_frontend *dib0070_attach(struct dvb_frontend *fe,
					   struct i2c_adapter *i2c,
					   struct dib0070_config *cfg);
extern u16 dib0070_wbd_offset(struct dvb_frontend *);
#else
static inline struct dvb_frontend *dib0070_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c,
						  struct dib0070_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline u16 dib0070_wbd_offset(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
#endif

#endif
