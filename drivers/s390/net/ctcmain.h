/*
 * $Id: ctcmain.h,v 1.4 2005/03/24 09:04:17 mschwide Exp $
 *
 * CTC / ESCON network driver
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
	      Peter Tiedemann (ptiedem@de.ibm.com)
 *
 *
 * Documentation used:
 *  - Principles of Operation (IBM doc#: SA22-7201-06)
 *  - Common IO/-Device Commands and Self Description (IBM doc#: SA22-7204-02)
 *  - Common IO/-Device Commands and Self Description (IBM doc#: SN22-5535)
 *  - ESCON Channel-to-Channel Adapter (IBM doc#: SA22-7203-00)
 *  - ESCON I/O Interface (IBM doc#: SA22-7202-029
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * RELEASE-TAG: CTC/ESCON network driver $Revision: 1.4 $
 *
 */

#ifndef _CTCMAIN_H_
#define _CTCMAIN_H_

#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

#include "ctctty.h"
#include "fsm.h"
#include "cu3088.h"


/**
 * CCW commands, used in this driver.
 */
#define CCW_CMD_WRITE		0x01
#define CCW_CMD_READ		0x02
#define CCW_CMD_SET_EXTENDED	0xc3
#define CCW_CMD_PREPARE		0xe3

#define CTC_PROTO_S390          0
#define CTC_PROTO_LINUX         1
#define CTC_PROTO_LINUX_TTY     2
#define CTC_PROTO_OS390         3
#define CTC_PROTO_MAX           3

#define CTC_BUFSIZE_LIMIT       65535
#define CTC_BUFSIZE_DEFAULT     32768

#define CTC_TIMEOUT_5SEC        5000

#define CTC_INITIAL_BLOCKLEN    2

#define READ			0
#define WRITE			1

#define CTC_ID_SIZE             BUS_ID_SIZE+3


struct ctc_profile {
	unsigned long maxmulti;
	unsigned long maxcqueue;
	unsigned long doios_single;
	unsigned long doios_multi;
	unsigned long txlen;
	unsigned long tx_time;
	struct timespec send_stamp;
};

/**
 * Definition of one channel
 */
struct channel {

	/**
	 * Pointer to next channel in list.
	 */
	struct channel *next;
	char id[CTC_ID_SIZE];
	struct ccw_device *cdev;

	/**
	 * Type of this channel.
	 * CTC/A or Escon for valid channels.
	 */
	enum channel_types type;

	/**
	 * Misc. flags. See CHANNEL_FLAGS_... below
	 */
	__u32 flags;

	/**
	 * The protocol of this channel
	 */
	__u16 protocol;

	/**
	 * I/O and irq related stuff
	 */
	struct ccw1 *ccw;
	struct irb *irb;

	/**
	 * RX/TX buffer size
	 */
	int max_bufsize;

	/**
	 * Transmit/Receive buffer.
	 */
	struct sk_buff *trans_skb;

	/**
	 * Universal I/O queue.
	 */
	struct sk_buff_head io_queue;

	/**
	 * TX queue for collecting skb's during busy.
	 */
	struct sk_buff_head collect_queue;

	/**
	 * Amount of data in collect_queue.
	 */
	int collect_len;

	/**
	 * spinlock for collect_queue and collect_len
	 */
	spinlock_t collect_lock;

	/**
	 * Timer for detecting unresposive
	 * I/O operations.
	 */
	fsm_timer timer;

	/**
	 * Retry counter for misc. operations.
	 */
	int retry;

	/**
	 * The finite state machine of this channel
	 */
	fsm_instance *fsm;

	/**
	 * The corresponding net_device this channel
	 * belongs to.
	 */
	struct net_device *netdev;

	struct ctc_profile prof;

	unsigned char *trans_skb_data;

	__u16 logflags;
};

#define CHANNEL_FLAGS_READ            0
#define CHANNEL_FLAGS_WRITE           1
#define CHANNEL_FLAGS_INUSE           2
#define CHANNEL_FLAGS_BUFSIZE_CHANGED 4
#define CHANNEL_FLAGS_FAILED          8
#define CHANNEL_FLAGS_WAITIRQ        16
#define CHANNEL_FLAGS_RWMASK 1
#define CHANNEL_DIRECTION(f) (f & CHANNEL_FLAGS_RWMASK)

#define LOG_FLAG_ILLEGALPKT  1
#define LOG_FLAG_ILLEGALSIZE 2
#define LOG_FLAG_OVERRUN     4
#define LOG_FLAG_NOMEM       8

#define CTC_LOGLEVEL_INFO     1
#define CTC_LOGLEVEL_NOTICE   2
#define CTC_LOGLEVEL_WARN     4
#define CTC_LOGLEVEL_EMERG    8
#define CTC_LOGLEVEL_ERR     16
#define CTC_LOGLEVEL_DEBUG   32
#define CTC_LOGLEVEL_CRIT    64

#define CTC_LOGLEVEL_DEFAULT \
(CTC_LOGLEVEL_INFO | CTC_LOGLEVEL_NOTICE | CTC_LOGLEVEL_WARN | CTC_LOGLEVEL_CRIT)

#define CTC_LOGLEVEL_MAX     ((CTC_LOGLEVEL_CRIT<<1)-1)

#define ctc_pr_debug(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_DEBUG) printk(KERN_DEBUG fmt,##arg); } while (0)

#define ctc_pr_info(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_INFO) printk(KERN_INFO fmt,##arg); } while (0)

#define ctc_pr_notice(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_NOTICE) printk(KERN_NOTICE fmt,##arg); } while (0)

#define ctc_pr_warn(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_WARN) printk(KERN_WARNING fmt,##arg); } while (0)

#define ctc_pr_emerg(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_EMERG) printk(KERN_EMERG fmt,##arg); } while (0)

#define ctc_pr_err(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_ERR) printk(KERN_ERR fmt,##arg); } while (0)

#define ctc_pr_crit(fmt, arg...) \
do { if (loglevel & CTC_LOGLEVEL_CRIT) printk(KERN_CRIT fmt,##arg); } while (0)

struct ctc_priv {
	struct net_device_stats stats;
	unsigned long tbusy;
	/**
	 * The finite state machine of this interface.
	 */
	fsm_instance *fsm;
	/**
	 * The protocol of this device
	 */
	__u16 protocol;
 	/**
 	 * Timer for restarting after I/O Errors
 	 */
 	fsm_timer               restart_timer;

	int buffer_size;

	struct channel *channel[2];
};

/**
 * Definition of our link level header.
 */
struct ll_header {
	__u16 length;
	__u16 type;
	__u16 unused;
};
#define LL_HEADER_LENGTH (sizeof(struct ll_header))

/**
 * Compatibility macros for busy handling
 * of network devices.
 */
static __inline__ void
ctc_clear_busy(struct net_device * dev)
{
	clear_bit(0, &(((struct ctc_priv *) dev->priv)->tbusy));
	if (((struct ctc_priv *)dev->priv)->protocol != CTC_PROTO_LINUX_TTY)
		netif_wake_queue(dev);
}

static __inline__ int
ctc_test_and_set_busy(struct net_device * dev)
{
	if (((struct ctc_priv *)dev->priv)->protocol != CTC_PROTO_LINUX_TTY)
		netif_stop_queue(dev);
	return test_and_set_bit(0, &((struct ctc_priv *) dev->priv)->tbusy);
}

#endif
