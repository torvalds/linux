/*
 *    Support for NXT2002 and NXT2004 - VSB/QAM
 *
 *    Copyright (C) 2005 Kirk Lapray (kirk.lapray@gmail.com)
 *    based on nxt2002 by Taylor Jacob <rtjacob@earthlink.net>
 *    and nxt2004 by Jean-Francois Thibert (jeanfrancois@sagetv.com)
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

#ifndef NXT200X_H
#define NXT200X_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

typedef enum nxt_chip_t {
		NXTUNDEFINED,
		NXT2002,
		NXT2004
}nxt_chip_type;

struct nxt200x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);
};

#if defined(CONFIG_DVB_NXT200X) || (defined(CONFIG_DVB_NXT200X_MODULE) && defined(MODULE))
extern struct dvb_frontend* nxt200x_attach(const struct nxt200x_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* nxt200x_attach(const struct nxt200x_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_NXT200X

#endif /* NXT200X_H */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
