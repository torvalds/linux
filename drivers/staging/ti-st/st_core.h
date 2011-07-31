/*
 *  Shared Transport Core header file
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

#ifndef ST_CORE_H
#define ST_CORE_H

#include <linux/skbuff.h>
#include "st.h"

/* states of protocol list */
#define ST_NOTEMPTY	1
#define ST_EMPTY	0

/*
 * possible st_states
 */
#define ST_INITIALIZING		1
#define ST_REG_IN_PROGRESS	2
#define ST_REG_PENDING		3
#define ST_WAITING_FOR_RESP	4

/**
 * struct st_data_s - ST core internal structure
 * @st_state: different states of ST like initializing, registration
 *	in progress, this is mainly used to return relevant err codes
 *	when protocol drivers are registering. It is also used to track
 *	the recv function, as in during fw download only HCI events
 *	can occur , where as during other times other events CH8, CH9
 *	can occur.
 * @tty: tty provided by the TTY core for line disciplines.
 * @ldisc_ops: the procedures that this line discipline registers with TTY.
 * @tx_skb: If for some reason the tty's write returns lesser bytes written
 *	then to maintain the rest of data to be written on next instance.
 *	This needs to be protected, hence the lock inside wakeup func.
 * @tx_state: if the data is being written onto the TTY and protocol driver
 *	wants to send more, queue up data and mark that there is
 *	more data to send.
 * @list: the list of protocols registered, only MAX can exist, one protocol
 *	can register only once.
 * @rx_state: states to be maintained inside st's tty receive
 * @rx_count: count to be maintained inside st's tty receieve
 * @rx_skb: the skb where all data for a protocol gets accumulated,
 *	since tty might not call receive when a complete event packet
 *	is received, the states, count and the skb needs to be maintained.
 * @txq: the list of skbs which needs to be sent onto the TTY.
 * @tx_waitq: if the chip is not in AWAKE state, the skbs needs to be queued
 *	up in here, PM(WAKEUP_IND) data needs to be sent and then the skbs
 *	from waitq can be moved onto the txq.
 *	Needs locking too.
 * @lock: the lock to protect skbs, queues, and ST states.
 * @protos_registered: count of the protocols registered, also when 0 the
 *	chip enable gpio can be toggled, and when it changes to 1 the fw
 *	needs to be downloaded to initialize chip side ST.
 * @ll_state: the various PM states the chip can be, the states are notified
 *	to us, when the chip sends relevant PM packets(SLEEP_IND, WAKE_IND).
 * @kim_data: reference to the parent encapsulating structure.
 *
 */
struct st_data_s {
	unsigned long st_state;
	struct tty_struct *tty;
	struct tty_ldisc_ops *ldisc_ops;
	struct sk_buff *tx_skb;
#define ST_TX_SENDING	1
#define ST_TX_WAKEUP	2
	unsigned long tx_state;
	struct st_proto_s *list[ST_MAX];
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
	struct sk_buff_head txq, tx_waitq;
	spinlock_t lock;
	unsigned char	protos_registered;
	unsigned long ll_state;
	void *kim_data;
};

/**
 * st_int_write -
 * point this to tty->driver->write or tty->ops->write
 * depending upon the kernel version
 */
int st_int_write(struct st_data_s*, const unsigned char*, int);

/**
 * st_write -
 * internal write function, passed onto protocol drivers
 * via the write function ptr of protocol struct
 */
long st_write(struct sk_buff *);

/* function to be called from ST-LL */
void st_ll_send_frame(enum proto_type, struct sk_buff *);

/* internal wake up function */
void st_tx_wakeup(struct st_data_s *st_data);

/* init, exit entry funcs called from KIM */
int st_core_init(struct st_data_s **);
void st_core_exit(struct st_data_s *);

/* ask for reference from KIM */
void st_kim_ref(struct st_data_s **, int);

#define GPS_STUB_TEST
#ifdef GPS_STUB_TEST
int gps_chrdrv_stub_write(const unsigned char*, int);
void gps_chrdrv_stub_init(void);
#endif

#endif /*ST_CORE_H */
