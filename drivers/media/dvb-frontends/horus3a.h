/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * horus3a.h
 *
 * Sony Horus3A DVB-S/S2 tuner driver
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
  */

#ifndef __DVB_HORUS3A_H__
#define __DVB_HORUS3A_H__

#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

/**
 * struct horus3a_config - the configuration of Horus3A tuner driver
 * @i2c_address:    I2C address of the tuner
 * @xtal_freq_mhz:  Oscillator frequency, MHz
 * @set_tuner_priv: Callback function private context
 * @set_tuner_callback: Callback function that notifies the parent driver
 *          which tuner is active now
 */
struct horus3a_config {
	u8	i2c_address;
	u8	xtal_freq_mhz;
	void	*set_tuner_priv;
	int	(*set_tuner_callback)(void *, int);
};

#if IS_REACHABLE(CONFIG_DVB_HORUS3A)
/**
 * horus3a_attach - Attach a horus3a tuner
 *
 * @fe: frontend to be attached
 * @config: pointer to &struct helene_config with tuner configuration.
 * @i2c: i2c adapter to use.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *horus3a_attach(struct dvb_frontend *fe,
					const struct horus3a_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *horus3a_attach(struct dvb_frontend *fe,
					const struct horus3a_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
