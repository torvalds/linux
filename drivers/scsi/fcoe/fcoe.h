/*
 * Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FCOE_H_
#define _FCOE_H_

#include <linux/skbuff.h>
#include <linux/kthread.h>

#define FCOE_MAX_QUEUE_DEPTH	256
#define FCOE_LOW_QUEUE_DEPTH	32

#define FCOE_WORD_TO_BYTE	4

#define FCOE_VERSION	"0.1"
#define FCOE_NAME	"fcoe"
#define FCOE_VENDOR	"Open-FCoE.org"

#define FCOE_MAX_LUN		255
#define FCOE_MAX_FCP_TARGET	256

#define FCOE_MAX_OUTSTANDING_COMMANDS	1024

#define FCOE_MIN_XID		0x0001	/* the min xid supported by fcoe_sw */
#define FCOE_MAX_XID		0x07ef	/* the max xid supported by fcoe_sw */

unsigned int fcoe_debug_logging;
module_param_named(debug_logging, fcoe_debug_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug_logging, "a bit mask of logging levels");

#define FCOE_LOGGING        0x01 /* General logging, not categorized */
#define FCOE_NETDEV_LOGGING 0x02 /* Netdevice logging */

#define FCOE_CHECK_LOGGING(LEVEL, CMD)					\
do {                                                            	\
	if (unlikely(fcoe_debug_logging & LEVEL))			\
		do {							\
			CMD;						\
		} while (0);						\
} while (0);

#define FCOE_DBG(fmt, args...)						\
	FCOE_CHECK_LOGGING(FCOE_LOGGING,				\
			   printk(KERN_INFO "fcoe: " fmt, ##args);)

#define FCOE_NETDEV_DBG(netdev, fmt, args...)			\
	FCOE_CHECK_LOGGING(FCOE_NETDEV_LOGGING,			\
			   printk(KERN_INFO "fcoe: %s" fmt,	\
				  netdev->name, ##args);)

/*
 * this percpu struct for fcoe
 */
struct fcoe_percpu_s {
	struct task_struct *thread;
	struct sk_buff_head fcoe_rx_list;
	struct page *crc_eof_page;
	int crc_eof_offset;
};

/*
 * the fcoe sw transport private data
 */
struct fcoe_softc {
	struct list_head list;
	struct net_device *real_dev;
	struct net_device *phys_dev;		/* device with ethtool_ops */
	struct packet_type  fcoe_packet_type;
	struct packet_type  fip_packet_type;
	struct sk_buff_head fcoe_pending_queue;
	u8	fcoe_pending_queue_active;
	struct timer_list timer;		/* queue timer */
	struct fcoe_ctlr ctlr;
};

#define fcoe_from_ctlr(fc) container_of(fc, struct fcoe_softc, ctlr)

static inline struct net_device *fcoe_netdev(
	const struct fc_lport *lp)
{
	return ((struct fcoe_softc *)lport_priv(lp))->real_dev;
}

#endif /* _FCOE_H_ */
