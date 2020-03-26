/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Dummy Frontend
 *
 *  Written by Emard <emard@softhome.net>
 */

#ifndef DDBRIDGE_DUMMY_FE_H
#define DDBRIDGE_DUMMY_FE_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

#if IS_REACHABLE(CONFIG_DDBRIDGE_DUMMY_FE)
struct dvb_frontend *ddbridge_dummy_fe_ofdm_attach(void);
struct dvb_frontend *ddbridge_dummy_fe_qpsk_attach(void);
struct dvb_frontend *ddbridge_dummy_fe_qam_attach(void);
#else
static inline struct dvb_frontend *ddbridge_dummy_fe_ofdm_attach(void)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct dvb_frontend *ddbridge_dummy_fe_qpsk_attach(void)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct dvb_frontend *ddbridge_dummy_fe_qam_attach(void)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DDBRIDGE_DUMMY_FE */

#endif // DDBRIDGE_DUMMY_FE_H
