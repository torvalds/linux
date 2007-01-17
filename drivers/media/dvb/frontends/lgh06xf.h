/*
 *  lgh06xf.h - ATSC Tuner support for LG TDVS-H06xF
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LGH06XF_H_
#define _LGH06XF_H_
#include "dvb_frontend.h"

#if defined(CONFIG_DVB_TUNER_LGH06XF) || (defined(CONFIG_DVB_TUNER_LGH06XF_MODULE) && defined(MODULE))
extern struct dvb_frontend* lgh06xf_attach(struct dvb_frontend* fe,
					    struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend* lgh06xf_attach(struct dvb_frontend* fe,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif /* CONFIG_DVB_TUNER_LGH06XF */

#endif /* _LGH06XF_H_ */
