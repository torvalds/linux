/*
 * cxd2099.h: Driver for the CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
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

#ifndef _CXD2099_H_
#define _CXD2099_H_

#include <dvb_ca_en50221.h>

struct cxd2099_cfg {
	u32 bitrate;
	u8  adr;
	u8  polarity;
	u8  clock_mode;

	u32 max_i2c;
};

#if defined(CONFIG_DVB_CXD2099) || \
	(defined(CONFIG_DVB_CXD2099_MODULE) && defined(MODULE))
struct dvb_ca_en50221 *cxd2099_attach(struct cxd2099_cfg *cfg,
				      void *priv, struct i2c_adapter *i2c);
#else

static inline struct dvb_ca_en50221 *cxd2099_attach(struct cxd2099_cfg *cfg,
					void *priv, struct i2c_adapter *i2c)
{
	dev_warn(&i2c->dev, "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
