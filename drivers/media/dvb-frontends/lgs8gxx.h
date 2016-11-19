/*
 *    Support for Legend Silicon GB20600 (a.k.a DMB-TH) demodulator
 *    LGS8913, LGS8GL5, LGS8G75
 *    experimental support LGS8G42, LGS8G52
 *
 *    Copyright (C) 2007-2009 David T.L. Wong <davidtlwong@gmail.com>
 *    Copyright (C) 2008 Sirius International (Hong Kong) Limited
 *    Timothy Lee <timothy.lee@siriushk.com> (for initial work on LGS8GL5)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __LGS8GXX_H__
#define __LGS8GXX_H__

#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#define LGS8GXX_PROD_LGS8913 0
#define LGS8GXX_PROD_LGS8GL5 1
#define LGS8GXX_PROD_LGS8G42 3
#define LGS8GXX_PROD_LGS8G52 4
#define LGS8GXX_PROD_LGS8G54 5
#define LGS8GXX_PROD_LGS8G75 6

struct lgs8gxx_config {

	/* product type */
	u8 prod;

	/* the demodulator's i2c address */
	u8 demod_address;

	/* parallel or serial transport stream */
	u8 serial_ts;

	/* transport stream polarity*/
	u8 ts_clk_pol;

	/* transport stream clock gated by ts_valid */
	u8 ts_clk_gated;

	/* A/D Clock frequency */
	u32 if_clk_freq; /* in kHz */

	/* IF frequency */
	u32 if_freq; /* in kHz */

	/*Use External ADC*/
	u8 ext_adc;

	/*External ADC output two's complement*/
	u8 adc_signed;

	/*Sample IF data at falling edge of IF_CLK*/
	u8 if_neg_edge;

	/*IF use Negative center frequency*/
	u8 if_neg_center;

	/*8G75 internal ADC input range selection*/
	/*0: 0.8Vpp, 1: 1.0Vpp, 2: 1.6Vpp, 3: 2.0Vpp*/
	u8 adc_vpp;

	/* slave address and configuration of the tuner */
	u8 tuner_address;
};

#if IS_REACHABLE(CONFIG_DVB_LGS8GXX)
extern struct dvb_frontend *lgs8gxx_attach(const struct lgs8gxx_config *config,
					   struct i2c_adapter *i2c);
#else
static inline
struct dvb_frontend *lgs8gxx_attach(const struct lgs8gxx_config *config,
				    struct i2c_adapter *i2c) {
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LGS8GXX */

#endif /* __LGS8GXX_H__ */
