/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fitipower FC0013 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#ifndef _FC0013_PRIV_H_
#define _FC0013_PRIV_H_

#define LOG_PREFIX "fc0013"

#undef err
#define err(f, arg...)  printk(KERN_ERR     LOG_PREFIX": " f "\n" , ## arg)
#undef info
#define info(f, arg...) printk(KERN_INFO    LOG_PREFIX": " f "\n" , ## arg)
#undef warn
#define warn(f, arg...) printk(KERN_WARNING LOG_PREFIX": " f "\n" , ## arg)

struct fc0013_priv {
	struct i2c_adapter *i2c;
	u8 addr;
	u8 dual_master;
	u8 xtal_freq;

	u32 frequency;
	u32 bandwidth;
};

#endif
