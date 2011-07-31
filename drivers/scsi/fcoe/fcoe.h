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

#define FCOE_MAX_LUN		0xFFFF
#define FCOE_MAX_FCP_TARGET	256

#define FCOE_MAX_OUTSTANDING_COMMANDS	1024

#define FCOE_MIN_XID		0x0000	/* the min xid supported by fcoe_sw */
#define FCOE_MAX_XID		0x0FFF	/* the max xid supported by fcoe_sw */

/*
 * Max MTU for FCoE: 14 (FCoE header) + 24 (FC header) + 2112 (max FC payload)
 * + 4 (FC CRC) + 4 (FCoE trailer) =  2158 bytes
 */
#define FCOE_MTU	2158

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
 * struct fcoe_percpu_s - The per-CPU context for FCoE receive threads
 * @thread:	    The thread context
 * @fcoe_rx_list:   The queue of pending packets to process
 * @page:	    The memory page for calculating frame trailer CRCs
 * @crc_eof_offset: The offset into the CRC page pointing to available
 *		    memory for a new trailer
 */
struct fcoe_percpu_s {
	struct task_struct *thread;
	struct sk_buff_head fcoe_rx_list;
	struct page *crc_eof_page;
	int crc_eof_offset;
};

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
	struct packet_type fcoe_packet_type;
	struct packet_type fip_packet_type;
	struct fcoe_ctlr   ctlr;
	struct fc_exch_mgr *oem;
	struct kref	   kref;
};

/**
 * struct fcoe_port - The FCoE private structure
 * @fcoe:		       The associated fcoe interface
 * @lport:		       The associated local port
 * @fcoe_pending_queue:	       The pending Rx queue of skbs
 * @fcoe_pending_queue_active: Indicates if the pending queue is active
 * @timer:		       The queue timer
 * @destroy_work:	       Handle for work context
 *			       (to prevent RTNL deadlocks)
 * @data_srt_addr:	       Source address for data
 *
 * An instance of this structure is to be allocated along with the
 * Scsi_Host and libfc fc_lport structures.
 */
struct fcoe_port {
	struct fcoe_interface *fcoe;
	struct fc_lport	      *lport;
	struct sk_buff_head   fcoe_pending_queue;
	u8		      fcoe_pending_queue_active;
	struct timer_list     timer;
	struct work_struct    destroy_work;
	u8		      data_src_addr[ETH_ALEN];
};

#define fcoe_from_ctlr(fip) container_of(fip, struct fcoe_interface, ctlr)

/**
 * fcoe_netdev() - Return the net device associated with a local port
 * @lport: The local port to get the net device from
 */
static inline struct net_device *fcoe_netdev(const struct fc_lport *lport)
{
	return ((struct fcoe_port *)lport_priv(lport))->fcoe->netdev;
}

#endif /* _FCOE_H_ */
