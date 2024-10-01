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

struct dvb_frontend *ddbridge_dummy_fe_qam_attach(void);

#endif // DDBRIDGE_DUMMY_FE_H
