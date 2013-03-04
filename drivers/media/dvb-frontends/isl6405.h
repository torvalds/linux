/*
 * isl6405.h - driver for dual lnb supply and control ic ISL6405
 *
 * Copyright (C) 2008 Hartmut Hackmann
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
 * the project's page is at http://www.linuxtv.org
 */

#ifndef _ISL6405_H
#define _ISL6405_H

#include <linux/dvb/frontend.h>

/* system register bits */

/* this bit selects register (control) 1 or 2
   note that the bit maps are different */

#define ISL6405_SR	0x80

/* SR = 0 */
#define ISL6405_OLF1	0x01
#define ISL6405_EN1	0x02
#define ISL6405_VSEL1	0x04
#define ISL6405_LLC1	0x08
#define ISL6405_ENT1	0x10
#define ISL6405_ISEL1	0x20
#define ISL6405_DCL	0x40

/* SR = 1 */
#define ISL6405_OLF2	0x01
#define ISL6405_OTF	0x02
#define ISL6405_EN2	0x04
#define ISL6405_VSEL2	0x08
#define ISL6405_LLC2	0x10
#define ISL6405_ENT2	0x20
#define ISL6405_ISEL2	0x40

#if IS_ENABLED(CONFIG_DVB_ISL6405)
/* override_set and override_clear control which system register bits (above)
 * to always set & clear
 */
extern struct dvb_frontend *isl6405_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c,
					   u8 i2c_addr, u8 override_set, u8 override_clear);
#else
static inline struct dvb_frontend *isl6405_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c, u8 i2c_addr,
						  u8 override_set, u8 override_clear)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ISL6405 */

#endif
