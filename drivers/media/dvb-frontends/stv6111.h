/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the ST STV6111 tuner
 *
 * Copyright (C) 2014 Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _STV6111_H_
#define _STV6111_H_

#if IS_REACHABLE(CONFIG_DVB_STV6111)

struct dvb_frontend *stv6111_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c, u8 adr);

#else

static inline struct dvb_frontend *stv6111_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c,
						  u8 adr)
{
	pr_warn("%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_STV6111 */

#endif /* _STV6111_H_ */
