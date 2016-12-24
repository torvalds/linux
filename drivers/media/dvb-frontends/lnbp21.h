/*
 * lnbp21.h - driver for lnb supply and control ic lnbp21
 *
 * Copyright (C) 2006 Oliver Endriss
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at https://linuxtv.org
 */

#ifndef _LNBP21_H
#define _LNBP21_H

/* system register bits */
/* [RO] 0=OK; 1=over current limit flag */
#define LNBP21_OLF	0x01
/* [RO] 0=OK; 1=over temperature flag (150 C) */
#define LNBP21_OTF	0x02
/* [RW] 0=disable LNB power, enable loopthrough
	1=enable LNB power, disable loopthrough */
#define LNBP21_EN	0x04
/* [RW] 0=low voltage (13/14V, vert pol)
	1=high voltage (18/19V,horiz pol) */
#define LNBP21_VSEL	0x08
/* [RW] increase LNB voltage by 1V:
	0=13/18V; 1=14/19V */
#define LNBP21_LLC	0x10
/* [RW] 0=tone controlled by DSQIN pin
	1=tone enable, disable DSQIN */
#define LNBP21_TEN	0x20
/* [RW] current limit select:
	0:Iout=500-650mA Isc=300mA
	1:Iout=400-550mA Isc=200mA */
#define LNBP21_ISEL	0x40
/* [RW] short-circuit protect:
	0=pulsed (dynamic) curr limiting
	1=static curr limiting */
#define LNBP21_PCL	0x80

#include <linux/dvb/frontend.h>

#if IS_REACHABLE(CONFIG_DVB_LNBP21)
/* override_set and override_clear control which
 system register bits (above) to always set & clear */
extern struct dvb_frontend *lnbp21_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 override_set,
				u8 override_clear);
#else
static inline struct dvb_frontend *lnbp21_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 override_set,
				u8 override_clear)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
