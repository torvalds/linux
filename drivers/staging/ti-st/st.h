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

/* TODO:
 * Move the following to tty.h upon acceptance
 */
#define N_TI_WL	20	/* Ldisc for TI's WL BT, FM, GPS combo chips */

/**
 * enum kim_gpio_state - Few protocols such as FM have ACTIVE LOW
 *	gpio states for their chip/core enable gpios
 */
enum kim_gpio_state {
	KIM_GPIO_INACTIVE,
	KIM_GPIO_ACTIVE,
};

/**
 * enum proto-type - The protocol on WiLink chips which share a
 *	common physical interface like UART.
 */
enum proto_type {
	ST_BT,
	ST_FM,
	ST_GPS,
	ST_MAX,
};

/**
 * struct st_proto_s - Per Protocol structure from BT/FM/GPS to ST
 * @type: type of the protocol being registered among the
 *	available proto_type(BT, FM, GPS the protocol which share TTY).
 * @recv: the receiver callback pointing to a function in the
 *	protocol drivers called by the ST driver upon receiving
 *	relevant data.
 * @match_packet: reserved for future use, to make ST more generic
 * @reg_complete_cb: callback handler pointing to a function in protocol
 *	handler called by ST when the pending registrations are complete.
 *	The registrations are marked pending, in situations when fw
 *	download is in progress.
 * @write: pointer to function in ST provided to protocol drivers from ST,
 *	to be made use when protocol drivers have data to send to TTY.
 * @priv_data: privdate data holder for the protocol drivers, sent
 *	from the protocol drivers during registration, and sent back on
 *	reg_complete_cb and recv.
 */
struct st_proto_s {
	enum proto_type type;
	long (*recv) (void *, struct sk_buff *);
	unsigned char (*match_packet) (const unsigned char *data);
	void (*reg_complete_cb) (void *, char data);
	long (*write) (struct sk_buff *skb);
	void *priv_data;
};

extern long st_register(struct st_proto_s *);
extern long st_unregister(enum proto_type);

#endif /* ST_H */
