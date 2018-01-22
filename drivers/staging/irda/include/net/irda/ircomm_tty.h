/*********************************************************************
 *                
 * Filename:      ircomm_tty.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 23:24:22 1999
 * Modified at:   Fri Jan 28 13:16:57 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *     
 ********************************************************************/

#ifndef IRCOMM_TTY_H
#define IRCOMM_TTY_H

#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/timer.h>
#include <linux/tty.h>		/* struct tty_struct */

#include <net/irda/irias_object.h>
#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>

#define IRCOMM_TTY_PORTS 32
#define IRCOMM_TTY_MAGIC 0x3432
#define IRCOMM_TTY_MAJOR 161
#define IRCOMM_TTY_MINOR 0

/* This is used as an initial value to max_header_size before the proper
 * value is filled in (5 for ttp, 4 for lmp). This allow us to detect
 * the state of the underlying connection. - Jean II */
#define IRCOMM_TTY_HDR_UNINITIALISED	16
/* Same for payload size. See qos.c for the smallest max data size */
#define IRCOMM_TTY_DATA_UNINITIALISED	(64 - IRCOMM_TTY_HDR_UNINITIALISED)

/*
 * IrCOMM TTY driver state
 */
struct ircomm_tty_cb {
	irda_queue_t queue;            /* Must be first */
	struct tty_port port;
	magic_t magic;

	int state;                /* Connect state */

	struct ircomm_cb *ircomm; /* IrCOMM layer instance */

	struct sk_buff *tx_skb;   /* Transmit buffer */
	struct sk_buff *ctrl_skb; /* Control data buffer */

	/* Parameters */
	struct ircomm_params settings;

	__u8 service_type;        /* The service that we support */
	int client;               /* True if we are a client */
	LOCAL_FLOW flow;          /* IrTTP flow status */

	int line;

	__u8 dlsap_sel;
	__u8 slsap_sel;

	__u32 saddr;
	__u32 daddr;

	__u32 max_data_size;   /* Max data we can transmit in one packet */
	__u32 max_header_size; /* The amount of header space we must reserve */
	__u32 tx_data_size;	/* Max data size of current tx_skb */

	struct iriap_cb *iriap; /* Instance used for querying remote IAS */
	struct ias_object* obj;
	void *skey;
	void *ckey;

	struct timer_list watchdog_timer;
	struct work_struct  tqueue;

	/* Protect concurent access to :
	 *	o self->ctrl_skb
	 *	o self->tx_skb
	 * Maybe other things may gain to be protected as well...
	 * Jean II */
	spinlock_t spinlock;
};

void ircomm_tty_start(struct tty_struct *tty);
void ircomm_tty_check_modem_status(struct ircomm_tty_cb *self);

int ircomm_tty_tiocmget(struct tty_struct *tty);
int ircomm_tty_tiocmset(struct tty_struct *tty, unsigned int set,
			unsigned int clear);
int ircomm_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
		     unsigned long arg);
void ircomm_tty_set_termios(struct tty_struct *tty,
			    struct ktermios *old_termios);

#endif







