/*
 * drxd.h: DRXD DVB-T demodulator driver
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _DRXD_H_
#define _DRXD_H_

#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/i2c.h>

struct drxd_config {
	u8 index;

	u8 pll_address;
	u8 pll_type;
#define DRXD_PLL_NONE     0
#define DRXD_PLL_DTT7520X 1
#define DRXD_PLL_MT3X0823 2

	u32 clock;
	u8 insert_rs_byte;

	u8 demod_address;
	u8 demoda_address;
	u8 demod_revision;

	/* If the tuner is not behind an i2c gate, be sure to flip this bit
	   or else the i2c bus could get wedged */
	u8 disable_i2c_gate_ctrl;

	u32 IF;
	 s16(*osc_deviation) (void *priv, s16 dev, int flag);
};

#if IS_REACHABLE(CONFIG_DVB_DRXD)
extern
struct dvb_frontend *drxd_attach(const struct drxd_config *config,
				 void *priv, struct i2c_adapter *i2c,
				 struct device *dev);
#else
static inline
struct dvb_frontend *drxd_attach(const struct drxd_config *config,
				 void *priv, struct i2c_adapter *i2c,
				 struct device *dev)
{
	printk(KERN_INFO "%s: not probed - driver disabled by Kconfig\n",
	       __func__);
	return NULL;
}
#endif

#endif
