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
 * the project's page is at http://www.linuxtv.org
 */

#ifndef _LNBP21_H
#define _LNBP21_H

/* system register bits */
#define LNBP21_OLF	0x01
#define LNBP21_OTF	0x02
#define LNBP21_EN	0x04
#define LNBP21_VSEL	0x08
#define LNBP21_LLC	0x10
#define LNBP21_TEN	0x20
#define LNBP21_ISEL	0x40
#define LNBP21_PCL	0x80

#include <linux/dvb/frontend.h>

/* override_set and override_clear control which system register bits (above) to always set & clear */
extern int lnbp21_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, u8 override_set, u8 override_clear);

#endif
