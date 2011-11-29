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
#define FCOE_MIN_QUEUE_DEPTH	32

#define FCOE_WORD_TO_BYTE	4

#define FCOE_VERSION	"0.1"
#define FCOE_NAME	"fcoe"
#define FCOE_VENDOR	"Open-FCoE.org"

#define FCOE_MAX_LUN		0xFFFF
#define FCOE_MAX_FCP_TARGET	256

#define FCOE_MAX_OUTSTANDING_COMMANDS	1024

#define FCOE_MIN_XID		0x0000	/* the min xid supported by fcoe_sw */
#define FCOE_MAX_XID		0x0FFF	/* the max xid supported by fcoe_sw */

unsigned int fcoe_debug_logging;
module_param_named(debug_logging, fcoe_debug_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug_logging, "a bit mask of logging levels");

#define FCOE_LOGGING	    0x01 /* General logging, not categorized */
#define FCOE_NETDEV_LOGGING 0x02 /* Netdevice logging */

#define FCOE_CHECK_LOGGING(LEVEL, CMD)					\
do {                                                            	\
	if (unlikely(fcoe_debug_logging & LEVEL))			\
		do {							\
			CMD;						\
		} while (0);						\
} while (0)

#define FCOE_DBG(fmt, args...)						\
	FCOE_CHECK_LOGGING(FCOE_LOGGING,				\
			   printk(KERN_INFO "fcoe: " fmt, ##args);)

#define FCOE_NETDEV_DBG(netdev, fmt, args...)			\
	FCOE_CHECK_LOGGING(FCOE_NETDEV_LOGGING,			\
			   printk(KERN_INFO "fcoe: %s: " fmt,	\
				  netdev->name, ##args);)

/**
 * struct fcoe_interface - A FCoE interface
 * @list:	      Handle for a list of FCoE interfaces
 * @netdev:	      The associated net device
 * @fcoe_packet_type: FCoE packet type
 * @fip_packet_type:  FIP packet type
 * @ctlr:	      The FCoE controller (for FIP)
 * @oem:	      The offload exchange manager for all local port
 *		      instances associated with this port
 * @kref:	      The kernel reference
 *
 * This structure is 1:1 with a net devive.
 */
struct fcoe_interface {
	struct list_head   list;
	struct net_device  *netdev;
	struct net_device  *realdev;
	struct packet_type fcoe_packet_type;
	struct packet_type fip_packet_type;
	struct fcoe_ctlr   ctlr;
	struct fc_exch_mgr *oem;
	struct kref	   kref;
};

#define fcoe_from_ctlr(fip) container_of(fip, struct fcoe_interface, ctlr)

/**
 * fcoe_netdev() - Return the net device associated with a local port
 * @lport: The local port to get the net device from
 */
static inline struct net_device *fcoe_netdev(const struct fc_lport *lport)
{
	return ((struct fcoe_interface *)
			((struct fcoe_port *)lport_priv(lport))->priv)->netdev;
}

#endif /* _FCOE_H_ */
