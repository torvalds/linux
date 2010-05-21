/*
 *  Shared Transport Header file
 *	To be included by the protocol stack drivers for
 *	Texas Instruments BT,FM and GPS combo chip drivers
 *
 *  Copyright (C) 2009 Texas Instruments
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef ST_H
#define ST_H

#include <linux/skbuff.h>
/*
 * st.h
 */

/* TODO:
 * Move the following to tty.h upon acceptance
 */
#define N_TI_WL	20	/* Ldisc for TI's WL BT, FM, GPS combo chips */

/* some gpios have active high, others like fm have
 * active low
 */
enum kim_gpio_state {
	KIM_GPIO_INACTIVE,
	KIM_GPIO_ACTIVE,
};
/*
 * the list of protocols on chip
 */
enum proto_type {
	ST_BT,
	ST_FM,
	ST_GPS,
	ST_MAX,
};

enum {
	ST_ERR_FAILURE = -1,	/* check struct */
	ST_SUCCESS,
	ST_ERR_PENDING = -5,	/* to call reg_complete_cb */
	ST_ERR_ALREADY,		/* already registered */
	ST_ERR_INPROGRESS,
	ST_ERR_NOPROTO,		/* protocol not supported */
};

/* per protocol structure
 * for BT/FM and GPS
 */
struct st_proto_s {
	enum proto_type type;
/*
 * to be called by ST when data arrives
 */
	long (*recv) (struct sk_buff *);
/*
 * for future use, logic now to be in ST
 */
	unsigned char (*match_packet) (const unsigned char *data);
/*
 * subsequent registration return PENDING,
 * signalled complete by this callback function
 */
	void (*reg_complete_cb) (char data);
/*
 * write function, sent in as NULL and to be returned to
 * protocol drivers
 */
	long (*write) (struct sk_buff *skb);
};

extern long st_register(struct st_proto_s *new_proto);
extern long st_unregister(enum proto_type type);

#endif /* ST_H */
