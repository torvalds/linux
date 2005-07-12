/*
 * $Id: lgdt3302.h,v 1.2 2005/06/28 23:50:48 mkrufky Exp $
 *
 *    Support for LGDT3302 (DViCO FustionHDTV 3 Gold) - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
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

#ifndef LGDT3302_H
#define LGDT3302_H

#include <linux/dvb/frontend.h>

struct lgdt3302_config
{
	/* The demodulator's i2c address */
	u8 demod_address;
	u8 pll_address;
	struct dvb_pll_desc *pll_desc;

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);
};

extern struct dvb_frontend* lgdt3302_attach(const struct lgdt3302_config* config,
					    struct i2c_adapter* i2c);

#endif /* LGDT3302_H */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
