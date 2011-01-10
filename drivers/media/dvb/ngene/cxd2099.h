/*
 * cxd2099.h: Driver for the CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010 DigitalDevices UG
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

struct dvb_ca_en50221 *cxd2099_attach(u8 adr, void *priv, struct i2c_adapter *i2c);

#endif
