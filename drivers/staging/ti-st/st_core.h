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

/*
 * local data required for ST/KIM/ST-HCI-LL
 */
struct st_data_s {
	unsigned long st_state;
/*
 * an instance of tty_struct & ldisc ops to move around
 */
	struct tty_struct *tty;
	struct tty_ldisc_ops *ldisc_ops;
/*
 * the tx skb -
 * if the skb is already dequeued and the tty failed to write the same
 * maintain the skb to write in the next transaction
 */
	struct sk_buff *tx_skb;
#define ST_TX_SENDING	1
#define ST_TX_WAKEUP	2
	unsigned long tx_state;
/*
 * list of protocol registered
 */
	struct st_proto_s *list[ST_MAX];
/*
 * lock
 */
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
	struct sk_buff_head txq, tx_waitq;
	spinlock_t lock;	/* ST LL state lock  */
	unsigned char	protos_registered;
	unsigned long ll_state;	/* ST LL power state */
};

/* point this to tty->driver->write or tty->ops->write
 * depending upon the kernel version
 */
int st_int_write(struct st_data_s*, const unsigned char*, int);
/* internal write function, passed onto protocol drivers
 * via the write function ptr of protocol struct
 */
long st_write(struct sk_buff *);
/* function to be called from ST-LL
 */
void st_ll_send_frame(enum proto_type, struct sk_buff *);
/* internal wake up function */
void st_tx_wakeup(struct st_data_s *st_data);

int st_core_init(struct st_data_s **);
void st_core_exit(struct st_data_s *);
void st_kim_ref(struct st_data_s **);

#define GPS_STUB_TEST
#ifdef GPS_STUB_TEST
int gps_chrdrv_stub_write(const unsigned char*, int);
void gps_chrdrv_stub_init(void);
#endif

#endif /*ST_CORE_H */
