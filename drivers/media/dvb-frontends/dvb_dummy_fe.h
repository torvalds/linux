/*
 *  Driver for Dummy Frontend
 *
 *  Written by Emard <emard@softhome.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef DVB_DUMMY_FE_H
#define DVB_DUMMY_FE_H

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#if IS_ENABLED(CONFIG_DVB_DUMMY_FE)
extern struct dvb_frontend* dvb_dummy_fe_ofdm_attach(void);
extern struct dvb_frontend* dvb_dummy_fe_qpsk_attach(void);
extern struct dvb_frontend* dvb_dummy_fe_qam_attach(void);
#else
static inline struct dvb_frontend *dvb_dummy_fe_ofdm_attach(void)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct dvb_frontend *dvb_dummy_fe_qpsk_attach(void)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct dvb_frontend *dvb_dummy_fe_qam_attach(void)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_DUMMY_FE */

#endif // DVB_DUMMY_FE_H
