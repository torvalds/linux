/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */
/*
 * IBM eServer iSeries Virtual Ethernet Device Driver
 * Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.
 * Substantially cleaned up by:
 * Copyright (C) 2003 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 *
 * This module implements the virtual ethernet device for iSeries LPAR
 * Linux.  It uses hypervisor message passing to implement an
 * ethernet-like network device communicating between partitions on
 * the iSeries.
 *
 * The iSeries LPAR hypervisor currently allows for up to 16 different
 * virtual ethernets.  These are all dynamically configurable on
 * OS/400 partitions, but dynamic configuration is not supported under
 * Linux yet.  An ethXX network device will be created for each
 * virtual ethernet this partition is connected to.
 *
 * - This driver is responsible for routing packets to and from other
 *   partitions.  The MAC addresses used by the virtual ethernets
 *   contains meaning and must not be modified.
 *
 * - Having 2 virtual ethernets to the same remote partition DOES NOT
 *   double the available bandwidth.  The 2 devices will share the
 *   available hypervisor bandwidth.
 *
 * - If you send a packet to your own mac address, it will just be
 *   dropped, you won't get it on the receive side.
 *
 * - Multicast is implemented by sending the frame frame to every
 *   other partition.  It is the responsibility of the receiving
 *   partition to filter the addresses desired.
 *
 * Tunable parameters:
 *
 * VETH_NUMBUFFERS: This compile time option defaults to 120.  It
 * controls how much memory Linux will allocate per remote partition
 * it is communicating with.  It can be thought of as the maximum
 * number of packets outstanding to a remote partition at a time.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/ethtool.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/uaccess.h>

#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iommu.h>
#include <asm/vio.h>

#include "iseries_veth.h"

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

#define VETH_NUMBUFFERS		(120)
#define VETH_ACKTIMEOUT 	(1000000) /* microseconds */
#define VETH_MAX_MCAST		(12)

#define VETH_MAX_MTU		(9000)

#if VETH_NUMBUFFERS < 10
#define ACK_THRESHOLD 		(1)
#elif VETH_NUMBUFFERS < 20
#define ACK_THRESHOLD 		(4)
#elif VETH_NUMBUFFERS < 40
#define ACK_THRESHOLD 		(10)
#else
#define ACK_THRESHOLD 		(20)
#endif

#define	VETH_STATE_SHUTDOWN	(0x0001)
#define VETH_STATE_OPEN		(0x0002)
#define VETH_STATE_RESET	(0x0004)
#define VETH_STATE_SENTMON	(0x0008)
#define VETH_STATE_SENTCAPS	(0x0010)
#define VETH_STATE_GOTCAPACK	(0x0020)
#define VETH_STATE_GOTCAPS	(0x0040)
#define VETH_STATE_SENTCAPACK	(0x0080)
#define VETH_STATE_READY	(0x0100)

struct veth_msg {
	struct veth_msg *next;
	struct VethFramesData data;
	int token;
	unsigned long in_use;
	struct sk_buff *skb;
	struct device *dev;
};

struct veth_lpar_connection {
	HvLpIndex remote_lp;
	struct work_struct statemachine_wq;
	struct veth_msg *msgs;
	int num_events;
	struct VethCapData local_caps;

	struct timer_list ack_timer;

	spinlock_t lock;
	unsigned long state;
	HvLpInstanceId src_inst;
	HvLpInstanceId dst_inst;
	struct VethLpEvent cap_event, cap_ack_event;
	u16 pending_acks[VETH_MAX_ACKS_PER_MSG];
	u32 num_pending_acks;

	int num_ack_events;
	struct VethCapData remote_caps;
	u32 ack_timeout;

	spinlock_t msg_stack_lock;
	struct veth_msg *msg_stack_head;
};

struct veth_port {
	struct device *dev;
	struct net_device_stats stats;
	u64 mac_addr;
	HvLpIndexMap lpar_map;

	spinlock_t pending_gate;
	struct sk_buff *pending_skb;
	HvLpIndexMap pending_lpmask;

	rwlock_t mcast_gate;
	int promiscuous;
	int all_mcast;
	int num_mcast;
	u64 mcast_addr[VETH_MAX_MCAST];
};

static HvLpIndex this_lp;
static struct veth_lpar_connection *veth_cnx[HVMAXARCHITECTEDLPS]; /* = 0 */
static struct net_device *veth_dev[HVMAXARCHITECTEDVIRTUALLANS]; /* = 0 */

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void veth_recycle_msg(struct veth_lpar_connection *, struct veth_msg *);
static void veth_flush_pending(struct veth_lpar_connection *cnx);
static void veth_receive(struct veth_lpar_connection *, struct VethLpEvent *);
static void veth_timed_ack(unsigned long connectionPtr);

/*
 * Utility functions
 */

#define veth_printk(prio, fmt, args...) \
	printk(prio "%s: " fmt, __FILE__, ## args)

#define veth_error(fmt, args...) \
	printk(KERN_ERR "(%s:%3.3d) ERROR: " fmt, __FILE__, __LINE__ , ## args)

static inline void veth_stack_push(struct veth_lpar_connection *cnx,
				   struct veth_msg *msg)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->msg_stack_lock, flags);
	msg->next = cnx->msg_stack_head;
	cnx->msg_stack_head = msg;
	spin_unlock_irqrestore(&cnx->msg_stack_lock, flags);
}

static inline struct veth_msg *veth_stack_pop(struct veth_lpar_connection *cnx)
{
	unsigned long flags;
	struct veth_msg *msg;

	spin_lock_irqsave(&cnx->msg_stack_lock, flags);
	msg = cnx->msg_stack_head;
	if (msg)
		cnx->msg_stack_head = cnx->msg_stack_head->next;
	spin_unlock_irqrestore(&cnx->msg_stack_lock, flags);
	return msg;
}

static inline HvLpEvent_Rc
veth_signalevent(struct veth_lpar_connection *cnx, u16 subtype,
		 HvLpEvent_AckInd ackind, HvLpEvent_AckType acktype,
		 u64 token,
		 u64 data1, u64 data2, u64 data3, u64 data4, u64 data5)
{
	return HvCallEvent_signalLpEventFast(cnx->remote_lp,
					     HvLpEvent_Type_VirtualLan,
					     subtype, ackind, acktype,
					     cnx->src_inst,
					     cnx->dst_inst,
					     token, data1, data2, data3,
					     data4, data5);
}

static inline HvLpEvent_Rc veth_signaldata(struct veth_lpar_connection *cnx,
					   u16 subtype, u64 token, void *data)
{
	u64 *p = (u64 *) data;

	return veth_signalevent(cnx, subtype, HvLpEvent_AckInd_NoAck,
				HvLpEvent_AckType_ImmediateAck,
				token, p[0], p[1], p[2], p[3], p[4]);
}

struct veth_allocation {
	struct completion c;
	int num;
};

static void veth_complete_allocation(void *parm, int number)
{
	struct veth_allocation *vc = (struct veth_allocation *)parm;

	vc->num = number;
	complete(&vc->c);
}

static int veth_allocate_events(HvLpIndex rlp, int number)
{
	struct veth_allocation vc = { COMPLETION_INITIALIZER(vc.c), 0 };

	mf_allocate_lp_events(rlp, HvLpEvent_Type_VirtualLan,
			    sizeof(struct VethLpEvent), number,
			    &veth_complete_allocation, &vc);
	wait_for_completion(&vc.c);

	return vc.num;
}

/*
 * LPAR connection code
 */

static inline void veth_kick_statemachine(struct veth_lpar_connection *cnx)
{
	schedule_work(&cnx->statemachine_wq);
}

static void veth_take_cap(struct veth_lpar_connection *cnx,
			  struct VethLpEvent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	/* Receiving caps may mean the other end has just come up, so
	 * we need to reload the instance ID of the far end */
	cnx->dst_inst =
		HvCallEvent_getTargetLpInstanceId(cnx->remote_lp,
						  HvLpEvent_Type_VirtualLan);

	if (cnx->state & VETH_STATE_GOTCAPS) {
		veth_error("Received a second capabilities from lpar %d\n",
			   cnx->remote_lp);
		event->base_event.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	} else {
		memcpy(&cnx->cap_event, event, sizeof(cnx->cap_event));
		cnx->state |= VETH_STATE_GOTCAPS;
		veth_kick_statemachine(cnx);
	}
	spin_unlock_irqrestore(&cnx->lock, flags);
}

static void veth_take_cap_ack(struct veth_lpar_connection *cnx,
			      struct VethLpEvent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	if (cnx->state & VETH_STATE_GOTCAPACK) {
		veth_error("Received a second capabilities ack from lpar %d\n",
			   cnx->remote_lp);
	} else {
		memcpy(&cnx->cap_ack_event, event,
		       sizeof(&cnx->cap_ack_event));
		cnx->state |= VETH_STATE_GOTCAPACK;
		veth_kick_statemachine(cnx);
	}
	spin_unlock_irqrestore(&cnx->lock, flags);
}

static void veth_take_monitor_ack(struct veth_lpar_connection *cnx,
				  struct VethLpEvent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	veth_printk(KERN_DEBUG, "Monitor ack returned for lpar %d\n",
		    cnx->remote_lp);
	cnx->state |= VETH_STATE_RESET;
	veth_kick_statemachine(cnx);
	spin_unlock_irqrestore(&cnx->lock, flags);
}

static void veth_handle_ack(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->base_event.xTargetLp;
	struct veth_lpar_connection *cnx = veth_cnx[rlp];

	BUG_ON(! cnx);

	switch (event->base_event.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap_ack(cnx, event);
		break;
	case VethEventTypeMonitor:
		veth_take_monitor_ack(cnx, event);
		break;
	default:
		veth_error("Unknown ack type %d from lpar %d\n",
			   event->base_event.xSubtype, rlp);
	};
}

static void veth_handle_int(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->base_event.xSourceLp;
	struct veth_lpar_connection *cnx = veth_cnx[rlp];
	unsigned long flags;
	int i;

	BUG_ON(! cnx);

	switch (event->base_event.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap(cnx, event);
		break;
	case VethEventTypeMonitor:
		/* do nothing... this'll hang out here til we're dead,
		 * and the hypervisor will return it for us. */
		break;
	case VethEventTypeFramesAck:
		spin_lock_irqsave(&cnx->lock, flags);
		for (i = 0; i < VETH_MAX_ACKS_PER_MSG; ++i) {
			u16 msgnum = event->u.frames_ack_data.token[i];

			if (msgnum < VETH_NUMBUFFERS)
				veth_recycle_msg(cnx, cnx->msgs + msgnum);
		}
		spin_unlock_irqrestore(&cnx->lock, flags);
		veth_flush_pending(cnx);
		break;
	case VethEventTypeFrames:
		veth_receive(cnx, event);
		break;
	default:
		veth_error("Unknown interrupt type %d from lpar %d\n",
			   event->base_event.xSubtype, rlp);
	};
}

static void veth_handle_event(struct HvLpEvent *event, struct pt_regs *regs)
{
	struct VethLpEvent *veth_event = (struct VethLpEvent *)event;

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack)
		veth_handle_ack(veth_event);
	else if (event->xFlags.xFunction == HvLpEvent_Function_Int)
		veth_handle_int(veth_event);
}

static int veth_process_caps(struct veth_lpar_connection *cnx)
{
	struct VethCapData *remote_caps = &cnx->remote_caps;
	int num_acks_needed;

	/* Convert timer to jiffies */
	cnx->ack_timeout = remote_caps->ack_timeout * HZ / 1000000;

	if ( (remote_caps->num_buffers == 0)
	     || (remote_caps->ack_threshold > VETH_MAX_ACKS_PER_MSG)
	     || (remote_caps->ack_threshold == 0)
	     || (cnx->ack_timeout == 0) ) {
		veth_error("Received incompatible capabilities from lpar %d\n",
			   cnx->remote_lp);
		return HvLpEvent_Rc_InvalidSubtypeData;
	}

	num_acks_needed = (remote_caps->num_buffers
			   / remote_caps->ack_threshold) + 1;

	/* FIXME: locking on num_ack_events? */
	if (cnx->num_ack_events < num_acks_needed) {
		int num;

		num = veth_allocate_events(cnx->remote_lp,
					   num_acks_needed-cnx->num_ack_events);
		if (num > 0)
			cnx->num_ack_events += num;

		if (cnx->num_ack_events < num_acks_needed) {
			veth_error("Couldn't allocate enough ack events for lpar %d\n",
				   cnx->remote_lp);

			return HvLpEvent_Rc_BufferNotAvailable;
		}
	}


	return HvLpEvent_Rc_Good;
}

/* FIXME: The gotos here are a bit dubious */
static void veth_statemachine(void *p)
{
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *)p;
	int rlp = cnx->remote_lp;
	int rc;

	spin_lock_irq(&cnx->lock);

 restart:
	if (cnx->state & VETH_STATE_RESET) {
		int i;

		del_timer(&cnx->ack_timer);

		if (cnx->state & VETH_STATE_OPEN)
			HvCallEvent_closeLpEventPath(cnx->remote_lp,
						     HvLpEvent_Type_VirtualLan);

		/* reset ack data */
		memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
		cnx->num_pending_acks = 0;

		cnx->state &= ~(VETH_STATE_RESET | VETH_STATE_SENTMON
				| VETH_STATE_OPEN | VETH_STATE_SENTCAPS
				| VETH_STATE_GOTCAPACK | VETH_STATE_GOTCAPS
				| VETH_STATE_SENTCAPACK | VETH_STATE_READY);

		/* Clean up any leftover messages */
		if (cnx->msgs)
			for (i = 0; i < VETH_NUMBUFFERS; ++i)
				veth_recycle_msg(cnx, cnx->msgs + i);
		spin_unlock_irq(&cnx->lock);
		veth_flush_pending(cnx);
		spin_lock_irq(&cnx->lock);
		if (cnx->state & VETH_STATE_RESET)
			goto restart;
	}

	if (cnx->state & VETH_STATE_SHUTDOWN)
		/* It's all over, do nothing */
		goto out;

	if ( !(cnx->state & VETH_STATE_OPEN) ) {
		if (! cnx->msgs || (cnx->num_events < (2 + VETH_NUMBUFFERS)) )
			goto cant_cope;

		HvCallEvent_openLpEventPath(rlp, HvLpEvent_Type_VirtualLan);
		cnx->src_inst =
			HvCallEvent_getSourceLpInstanceId(rlp,
							  HvLpEvent_Type_VirtualLan);
		cnx->dst_inst =
			HvCallEvent_getTargetLpInstanceId(rlp,
							  HvLpEvent_Type_VirtualLan);
		cnx->state |= VETH_STATE_OPEN;
	}

	if ( (cnx->state & VETH_STATE_OPEN)
	     && !(cnx->state & VETH_STATE_SENTMON) ) {
		rc = veth_signalevent(cnx, VethEventTypeMonitor,
				      HvLpEvent_AckInd_DoAck,
				      HvLpEvent_AckType_DeferredAck,
				      0, 0, 0, 0, 0, 0);

		if (rc == HvLpEvent_Rc_Good) {
			cnx->state |= VETH_STATE_SENTMON;
		} else {
			if ( (rc != HvLpEvent_Rc_PartitionDead)
			     && (rc != HvLpEvent_Rc_PathClosed) )
				veth_error("Error sending monitor to "
					   "lpar %d, rc=%x\n",
					   rlp, (int) rc);

			/* Oh well, hope we get a cap from the other
			 * end and do better when that kicks us */
			goto out;
		}
	}

	if ( (cnx->state & VETH_STATE_OPEN)
	     && !(cnx->state & VETH_STATE_SENTCAPS)) {
		u64 *rawcap = (u64 *)&cnx->local_caps;

		rc = veth_signalevent(cnx, VethEventTypeCap,
				      HvLpEvent_AckInd_DoAck,
				      HvLpEvent_AckType_ImmediateAck,
				      0, rawcap[0], rawcap[1], rawcap[2],
				      rawcap[3], rawcap[4]);

		if (rc == HvLpEvent_Rc_Good) {
			cnx->state |= VETH_STATE_SENTCAPS;
		} else {
			if ( (rc != HvLpEvent_Rc_PartitionDead)
			     && (rc != HvLpEvent_Rc_PathClosed) )
				veth_error("Error sending caps to "
					   "lpar %d, rc=%x\n",
					   rlp, (int) rc);
			/* Oh well, hope we get a cap from the other
			 * end and do better when that kicks us */
			goto out;
		}
	}

	if ((cnx->state & VETH_STATE_GOTCAPS)
	    && !(cnx->state & VETH_STATE_SENTCAPACK)) {
		struct VethCapData *remote_caps = &cnx->remote_caps;

		memcpy(remote_caps, &cnx->cap_event.u.caps_data,
		       sizeof(*remote_caps));

		spin_unlock_irq(&cnx->lock);
		rc = veth_process_caps(cnx);
		spin_lock_irq(&cnx->lock);

		/* We dropped the lock, so recheck for anything which
		 * might mess us up */
		if (cnx->state & (VETH_STATE_RESET|VETH_STATE_SHUTDOWN))
			goto restart;

		cnx->cap_event.base_event.xRc = rc;
		HvCallEvent_ackLpEvent((struct HvLpEvent *)&cnx->cap_event);
		if (rc == HvLpEvent_Rc_Good)
			cnx->state |= VETH_STATE_SENTCAPACK;
		else
			goto cant_cope;
	}

	if ((cnx->state & VETH_STATE_GOTCAPACK)
	    && (cnx->state & VETH_STATE_GOTCAPS)
	    && !(cnx->state & VETH_STATE_READY)) {
		if (cnx->cap_ack_event.base_event.xRc == HvLpEvent_Rc_Good) {
			/* Start the ACK timer */
			cnx->ack_timer.expires = jiffies + cnx->ack_timeout;
			add_timer(&cnx->ack_timer);
			cnx->state |= VETH_STATE_READY;
		} else {
			veth_printk(KERN_ERR, "Caps rejected (rc=%d) by "
				    "lpar %d\n",
				    cnx->cap_ack_event.base_event.xRc,
				    rlp);
			goto cant_cope;
		}
	}

 out:
	spin_unlock_irq(&cnx->lock);
	return;

 cant_cope:
	/* FIXME: we get here if something happens we really can't
	 * cope with.  The link will never work once we get here, and
	 * all we can do is not lock the rest of the system up */
	veth_error("Badness on connection to lpar %d (state=%04lx) "
		   " - shutting down\n", rlp, cnx->state);
	cnx->state |= VETH_STATE_SHUTDOWN;
	spin_unlock_irq(&cnx->lock);
}

static int veth_init_connection(u8 rlp)
{
	struct veth_lpar_connection *cnx;
	struct veth_msg *msgs;
	int i;

	if ( (rlp == this_lp)
	     || ! HvLpConfig_doLpsCommunicateOnVirtualLan(this_lp, rlp) )
		return 0;

	cnx = kmalloc(sizeof(*cnx), GFP_KERNEL);
	if (! cnx)
		return -ENOMEM;
	memset(cnx, 0, sizeof(*cnx));

	cnx->remote_lp = rlp;
	spin_lock_init(&cnx->lock);
	INIT_WORK(&cnx->statemachine_wq, veth_statemachine, cnx);
	init_timer(&cnx->ack_timer);
	cnx->ack_timer.function = veth_timed_ack;
	cnx->ack_timer.data = (unsigned long) cnx;
	memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));

	veth_cnx[rlp] = cnx;

	msgs = kmalloc(VETH_NUMBUFFERS * sizeof(struct veth_msg), GFP_KERNEL);
	if (! msgs) {
		veth_error("Can't allocate buffers for lpar %d\n", rlp);
		return -ENOMEM;
	}

	cnx->msgs = msgs;
	memset(msgs, 0, VETH_NUMBUFFERS * sizeof(struct veth_msg));
	spin_lock_init(&cnx->msg_stack_lock);

	for (i = 0; i < VETH_NUMBUFFERS; i++) {
		msgs[i].token = i;
		veth_stack_push(cnx, msgs + i);
	}

	cnx->num_events = veth_allocate_events(rlp, 2 + VETH_NUMBUFFERS);

	if (cnx->num_events < (2 + VETH_NUMBUFFERS)) {
		veth_error("Can't allocate events for lpar %d, only got %d\n",
			   rlp, cnx->num_events);
		return -ENOMEM;
	}

	cnx->local_caps.num_buffers = VETH_NUMBUFFERS;
	cnx->local_caps.ack_threshold = ACK_THRESHOLD;
	cnx->local_caps.ack_timeout = VETH_ACKTIMEOUT;

	return 0;
}

static void veth_stop_connection(u8 rlp)
{
	struct veth_lpar_connection *cnx = veth_cnx[rlp];

	if (! cnx)
		return;

	spin_lock_irq(&cnx->lock);
	cnx->state |= VETH_STATE_RESET | VETH_STATE_SHUTDOWN;
	veth_kick_statemachine(cnx);
	spin_unlock_irq(&cnx->lock);

	flush_scheduled_work();

	/* FIXME: not sure if this is necessary - will already have
	 * been deleted by the state machine, just want to make sure
	 * its not running any more */
	del_timer_sync(&cnx->ack_timer);

	if (cnx->num_events > 0)
		mf_deallocate_lp_events(cnx->remote_lp,
				      HvLpEvent_Type_VirtualLan,
				      cnx->num_events,
				      NULL, NULL);
	if (cnx->num_ack_events > 0)
		mf_deallocate_lp_events(cnx->remote_lp,
				      HvLpEvent_Type_VirtualLan,
				      cnx->num_ack_events,
				      NULL, NULL);
}

static void veth_destroy_connection(u8 rlp)
{
	struct veth_lpar_connection *cnx = veth_cnx[rlp];

	if (! cnx)
		return;

	kfree(cnx->msgs);
	kfree(cnx);
	veth_cnx[rlp] = NULL;
}

/*
 * net_device code
 */

static int veth_open(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	memset(&port->stats, 0, sizeof (port->stats));
	netif_start_queue(dev);
	return 0;
}

static int veth_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static struct net_device_stats *veth_get_stats(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	return &port->stats;
}

static int veth_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > VETH_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static void veth_set_multicast_list(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;
	unsigned long flags;

	write_lock_irqsave(&port->mcast_gate, flags);

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n",
		       dev->name);
		port->promiscuous = 1;
	} else if ( (dev->flags & IFF_ALLMULTI)
		    || (dev->mc_count > VETH_MAX_MCAST) ) {
		port->all_mcast = 1;
	} else {
		struct dev_mc_list *dmi = dev->mc_list;
		int i;

		/* Update table */
		port->num_mcast = 0;

		for (i = 0; i < dev->mc_count; i++) {
			u8 *addr = dmi->dmi_addr;
			u64 xaddr = 0;

			if (addr[0] & 0x01) {/* multicast address? */
				memcpy(&xaddr, addr, ETH_ALEN);
				port->mcast_addr[port->num_mcast] = xaddr;
				port->num_mcast++;
			}
			dmi = dmi->next;
		}
	}

	write_unlock_irqrestore(&port->mcast_gate, flags);
}

static void veth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strncpy(info->driver, "veth", sizeof(info->driver) - 1);
	info->driver[sizeof(info->driver) - 1] = '\0';
	strncpy(info->version, "1.0", sizeof(info->version) - 1);
}

static int veth_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	ecmd->supported = (SUPPORTED_1000baseT_Full
			  | SUPPORTED_Autoneg | SUPPORTED_FIBRE);
	ecmd->advertising = (SUPPORTED_1000baseT_Full
			    | SUPPORTED_Autoneg | SUPPORTED_FIBRE);
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->phy_address = 0;
	ecmd->speed = SPEED_1000;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->autoneg = AUTONEG_ENABLE;
	ecmd->maxtxpkt = 120;
	ecmd->maxrxpkt = 120;
	return 0;
}

static u32 veth_get_link(struct net_device *dev)
{
	return 1;
}

static struct ethtool_ops ops = {
	.get_drvinfo = veth_get_drvinfo,
	.get_settings = veth_get_settings,
	.get_link = veth_get_link,
};

static void veth_tx_timeout(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *)dev->priv;
	struct net_device_stats *stats = &port->stats;
	unsigned long flags;
	int i;

	stats->tx_errors++;

	spin_lock_irqsave(&port->pending_gate, flags);

	printk(KERN_WARNING "%s: Tx timeout!  Resetting lp connections: %08x\n",
	       dev->name, port->pending_lpmask);

	/* If we've timed out the queue must be stopped, which should
	 * only ever happen when there is a pending packet. */
	WARN_ON(! port->pending_lpmask);

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		struct veth_lpar_connection *cnx = veth_cnx[i];

		if (! (port->pending_lpmask & (1<<i)))
			continue;

		/* If we're pending on it, we must be connected to it,
		 * so we should certainly have a structure for it. */
		BUG_ON(! cnx);

		/* Theoretically we could be kicking a connection
		 * which doesn't deserve it, but in practice if we've
		 * had a Tx timeout, the pending_lpmask will have
		 * exactly one bit set - the connection causing the
		 * problem. */
		spin_lock(&cnx->lock);
		cnx->state |= VETH_STATE_RESET;
		veth_kick_statemachine(cnx);
		spin_unlock(&cnx->lock);
	}

	spin_unlock_irqrestore(&port->pending_gate, flags);
}

static struct net_device * __init veth_probe_one(int vlan, struct device *vdev)
{
	struct net_device *dev;
	struct veth_port *port;
	int i, rc;

	dev = alloc_etherdev(sizeof (struct veth_port));
	if (! dev) {
		veth_error("Unable to allocate net_device structure!\n");
		return NULL;
	}

	port = (struct veth_port *) dev->priv;

	spin_lock_init(&port->pending_gate);
	rwlock_init(&port->mcast_gate);

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		HvLpVirtualLanIndexMap map;

		if (i == this_lp)
			continue;
		map = HvLpConfig_getVirtualLanIndexMapForLp(i);
		if (map & (0x8000 >> vlan))
			port->lpar_map |= (1 << i);
	}
	port->dev = vdev;

	dev->dev_addr[0] = 0x02;
	dev->dev_addr[1] = 0x01;
	dev->dev_addr[2] = 0xff;
	dev->dev_addr[3] = vlan;
	dev->dev_addr[4] = 0xff;
	dev->dev_addr[5] = this_lp;

	dev->mtu = VETH_MAX_MTU;

	memcpy(&port->mac_addr, dev->dev_addr, 6);

	dev->open = veth_open;
	dev->hard_start_xmit = veth_start_xmit;
	dev->stop = veth_close;
	dev->get_stats = veth_get_stats;
	dev->change_mtu = veth_change_mtu;
	dev->set_mac_address = NULL;
	dev->set_multicast_list = veth_set_multicast_list;
	SET_ETHTOOL_OPS(dev, &ops);

	dev->watchdog_timeo = 2 * (VETH_ACKTIMEOUT * HZ / 1000000);
	dev->tx_timeout = veth_tx_timeout;

	SET_NETDEV_DEV(dev, vdev);

	rc = register_netdev(dev);
	if (rc != 0) {
		veth_printk(KERN_ERR,
			    "Failed to register ethernet device for vlan %d\n",
			    vlan);
		free_netdev(dev);
		return NULL;
	}

	veth_printk(KERN_DEBUG, "%s attached to iSeries vlan %d (lpar_map=0x%04x)\n",
		    dev->name, vlan, port->lpar_map);

	return dev;
}

/*
 * Tx path
 */

static int veth_transmit_to_one(struct sk_buff *skb, HvLpIndex rlp,
				struct net_device *dev)
{
	struct veth_lpar_connection *cnx = veth_cnx[rlp];
	struct veth_port *port = (struct veth_port *) dev->priv;
	HvLpEvent_Rc rc;
	u32 dma_address, dma_length;
	struct veth_msg *msg = NULL;
	int err = 0;
	unsigned long flags;

	if (! cnx) {
		port->stats.tx_errors++;
		dev_kfree_skb(skb);
		return 0;
	}

	spin_lock_irqsave(&cnx->lock, flags);

	if (! cnx->state & VETH_STATE_READY)
		goto drop;

	if ((skb->len - 14) > VETH_MAX_MTU)
		goto drop;

	msg = veth_stack_pop(cnx);

	if (! msg) {
		err = 1;
		goto drop;
	}

	dma_length = skb->len;
	dma_address = dma_map_single(port->dev, skb->data,
				     dma_length, DMA_TO_DEVICE);

	if (dma_mapping_error(dma_address))
		goto recycle_and_drop;

	/* Is it really necessary to check the length and address
	 * fields of the first entry here? */
	msg->skb = skb;
	msg->dev = port->dev;
	msg->data.addr[0] = dma_address;
	msg->data.len[0] = dma_length;
	msg->data.eofmask = 1 << VETH_EOF_SHIFT;
	set_bit(0, &(msg->in_use));
	rc = veth_signaldata(cnx, VethEventTypeFrames, msg->token, &msg->data);

	if (rc != HvLpEvent_Rc_Good)
		goto recycle_and_drop;

	spin_unlock_irqrestore(&cnx->lock, flags);
	return 0;

 recycle_and_drop:
	msg->skb = NULL;
	/* need to set in use to make veth_recycle_msg in case this
	 * was a mapping failure */
	set_bit(0, &msg->in_use);
	veth_recycle_msg(cnx, msg);
 drop:
	port->stats.tx_errors++;
	dev_kfree_skb(skb);
	spin_unlock_irqrestore(&cnx->lock, flags);
	return err;
}

static HvLpIndexMap veth_transmit_to_many(struct sk_buff *skb,
					  HvLpIndexMap lpmask,
					  struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;
	int i;
	int rc;

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		if ((lpmask & (1 << i)) == 0)
			continue;

		rc = veth_transmit_to_one(skb_get(skb), i, dev);
		if (! rc)
			lpmask &= ~(1<<i);
	}

	if (! lpmask) {
		port->stats.tx_packets++;
		port->stats.tx_bytes += skb->len;
	}

	return lpmask;
}

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *frame = skb->data;
	struct veth_port *port = (struct veth_port *) dev->priv;
	unsigned long flags;
	HvLpIndexMap lpmask;

	if (! (frame[0] & 0x01)) {
		/* unicast packet */
		HvLpIndex rlp = frame[5];

		if ( ! ((1 << rlp) & port->lpar_map) ) {
			dev_kfree_skb(skb);
			return 0;
		}

		lpmask = 1 << rlp;
	} else {
		lpmask = port->lpar_map;
	}

	spin_lock_irqsave(&port->pending_gate, flags);

	lpmask = veth_transmit_to_many(skb, lpmask, dev);

	if (! lpmask) {
		dev_kfree_skb(skb);
	} else {
		if (port->pending_skb) {
			veth_error("%s: Tx while skb was pending!\n",
				   dev->name);
			dev_kfree_skb(skb);
			spin_unlock_irqrestore(&port->pending_gate, flags);
			return 1;
		}

		port->pending_skb = skb;
		port->pending_lpmask = lpmask;
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&port->pending_gate, flags);

	return 0;
}

static void veth_recycle_msg(struct veth_lpar_connection *cnx,
			     struct veth_msg *msg)
{
	u32 dma_address, dma_length;

	if (test_and_clear_bit(0, &msg->in_use)) {
		dma_address = msg->data.addr[0];
		dma_length = msg->data.len[0];

		dma_unmap_single(msg->dev, dma_address, dma_length,
				 DMA_TO_DEVICE);

		if (msg->skb) {
			dev_kfree_skb_any(msg->skb);
			msg->skb = NULL;
		}

		memset(&msg->data, 0, sizeof(msg->data));
		veth_stack_push(cnx, msg);
	} else
		if (cnx->state & VETH_STATE_OPEN)
			veth_error("Bogus frames ack from lpar %d (#%d)\n",
				   cnx->remote_lp, msg->token);
}

static void veth_flush_pending(struct veth_lpar_connection *cnx)
{
	int i;
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; i++) {
		struct net_device *dev = veth_dev[i];
		struct veth_port *port;
		unsigned long flags;

		if (! dev)
			continue;

		port = (struct veth_port *)dev->priv;

		if (! (port->lpar_map & (1<<cnx->remote_lp)))
			continue;

		spin_lock_irqsave(&port->pending_gate, flags);
		if (port->pending_skb) {
			port->pending_lpmask =
				veth_transmit_to_many(port->pending_skb,
						      port->pending_lpmask,
						      dev);
			if (! port->pending_lpmask) {
				dev_kfree_skb_any(port->pending_skb);
				port->pending_skb = NULL;
				netif_wake_queue(dev);
			}
		}
		spin_unlock_irqrestore(&port->pending_gate, flags);
	}
}

/*
 * Rx path
 */

static inline int veth_frame_wanted(struct veth_port *port, u64 mac_addr)
{
	int wanted = 0;
	int i;
	unsigned long flags;

	if ( (mac_addr == port->mac_addr) || (mac_addr == 0xffffffffffff0000) )
		return 1;

	if (! (((char *) &mac_addr)[0] & 0x01))
		return 0;

	read_lock_irqsave(&port->mcast_gate, flags);

	if (port->promiscuous || port->all_mcast) {
		wanted = 1;
		goto out;
	}

	for (i = 0; i < port->num_mcast; ++i) {
		if (port->mcast_addr[i] == mac_addr) {
			wanted = 1;
			break;
		}
	}

 out:
	read_unlock_irqrestore(&port->mcast_gate, flags);

	return wanted;
}

struct dma_chunk {
	u64 addr;
	u64 size;
};

#define VETH_MAX_PAGES_PER_FRAME ( (VETH_MAX_MTU+PAGE_SIZE-2)/PAGE_SIZE + 1 )

static inline void veth_build_dma_list(struct dma_chunk *list,
				       unsigned char *p, unsigned long length)
{
	unsigned long done;
	int i = 1;

	/* FIXME: skbs are continguous in real addresses.  Do we
	 * really need to break it into PAGE_SIZE chunks, or can we do
	 * it just at the granularity of iSeries real->absolute
	 * mapping?  Indeed, given the way the allocator works, can we
	 * count on them being absolutely contiguous? */
	list[0].addr = ISERIES_HV_ADDR(p);
	list[0].size = min(length,
			   PAGE_SIZE - ((unsigned long)p & ~PAGE_MASK));

	done = list[0].size;
	while (done < length) {
		list[i].addr = ISERIES_HV_ADDR(p + done);
		list[i].size = min(length-done, PAGE_SIZE);
		done += list[i].size;
		i++;
	}
}

static void veth_flush_acks(struct veth_lpar_connection *cnx)
{
	HvLpEvent_Rc rc;

	rc = veth_signaldata(cnx, VethEventTypeFramesAck,
			     0, &cnx->pending_acks);

	if (rc != HvLpEvent_Rc_Good)
		veth_error("Error 0x%x acking frames from lpar %d!\n",
			   (unsigned)rc, cnx->remote_lp);

	cnx->num_pending_acks = 0;
	memset(&cnx->pending_acks, 0xff, sizeof(cnx->pending_acks));
}

static void veth_receive(struct veth_lpar_connection *cnx,
			 struct VethLpEvent *event)
{
	struct VethFramesData *senddata = &event->u.frames_data;
	int startchunk = 0;
	int nchunks;
	unsigned long flags;
	HvLpDma_Rc rc;

	do {
		u16 length = 0;
		struct sk_buff *skb;
		struct dma_chunk local_list[VETH_MAX_PAGES_PER_FRAME];
		struct dma_chunk remote_list[VETH_MAX_FRAMES_PER_MSG];
		u64 dest;
		HvLpVirtualLanIndex vlan;
		struct net_device *dev;
		struct veth_port *port;

		/* FIXME: do we need this? */
		memset(local_list, 0, sizeof(local_list));
		memset(remote_list, 0, sizeof(VETH_MAX_FRAMES_PER_MSG));

		/* a 0 address marks the end of the valid entries */
		if (senddata->addr[startchunk] == 0)
			break;

		/* make sure that we have at least 1 EOF entry in the
		 * remaining entries */
		if (! (senddata->eofmask >> (startchunk + VETH_EOF_SHIFT))) {
			veth_error("missing EOF frag in event "
				   "eofmask=0x%x startchunk=%d\n",
				   (unsigned) senddata->eofmask, startchunk);
			break;
		}

		/* build list of chunks in this frame */
		nchunks = 0;
		do {
			remote_list[nchunks].addr =
				(u64) senddata->addr[startchunk+nchunks] << 32;
			remote_list[nchunks].size =
				senddata->len[startchunk+nchunks];
			length += remote_list[nchunks].size;
		} while (! (senddata->eofmask &
			    (1 << (VETH_EOF_SHIFT + startchunk + nchunks++))));

		/* length == total length of all chunks */
		/* nchunks == # of chunks in this frame */

		if ((length - ETH_HLEN) > VETH_MAX_MTU) {
			veth_error("Received oversize frame from lpar %d "
				   "(length=%d)\n", cnx->remote_lp, length);
			continue;
		}

		skb = alloc_skb(length, GFP_ATOMIC);
		if (!skb)
			continue;

		veth_build_dma_list(local_list, skb->data, length);

		rc = HvCallEvent_dmaBufList(HvLpEvent_Type_VirtualLan,
					    event->base_event.xSourceLp,
					    HvLpDma_Direction_RemoteToLocal,
					    cnx->src_inst,
					    cnx->dst_inst,
					    HvLpDma_AddressType_RealAddress,
					    HvLpDma_AddressType_TceIndex,
					    ISERIES_HV_ADDR(&local_list),
					    ISERIES_HV_ADDR(&remote_list),
					    length);
		if (rc != HvLpDma_Rc_Good) {
			dev_kfree_skb_irq(skb);
			continue;
		}

		vlan = skb->data[9];
		dev = veth_dev[vlan];
		if (! dev)
			/* Some earlier versions of the driver sent
			   broadcasts down all connections, even to
			   lpars that weren't on the relevant vlan.
			   So ignore packets belonging to a vlan we're
			   not on. */
			continue;

		port = (struct veth_port *)dev->priv;
		dest = *((u64 *) skb->data) & 0xFFFFFFFFFFFF0000;

		if ((vlan > HVMAXARCHITECTEDVIRTUALLANS) || !port) {
			dev_kfree_skb_irq(skb);
			continue;
		}
		if (! veth_frame_wanted(port, dest)) {
			dev_kfree_skb_irq(skb);
			continue;
		}

		skb_put(skb, length);
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_NONE;
		netif_rx(skb);	/* send it up */
		port->stats.rx_packets++;
		port->stats.rx_bytes += length;
	} while (startchunk += nchunks, startchunk < VETH_MAX_FRAMES_PER_MSG);

	/* Ack it */
	spin_lock_irqsave(&cnx->lock, flags);
	BUG_ON(cnx->num_pending_acks > VETH_MAX_ACKS_PER_MSG);

	cnx->pending_acks[cnx->num_pending_acks++] =
		event->base_event.xCorrelationToken;

	if ( (cnx->num_pending_acks >= cnx->remote_caps.ack_threshold)
	     || (cnx->num_pending_acks >= VETH_MAX_ACKS_PER_MSG) )
		veth_flush_acks(cnx);

	spin_unlock_irqrestore(&cnx->lock, flags);
}

static void veth_timed_ack(unsigned long ptr)
{
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *) ptr;
	unsigned long flags;

	/* Ack all the events */
	spin_lock_irqsave(&cnx->lock, flags);
	if (cnx->num_pending_acks > 0)
		veth_flush_acks(cnx);

	/* Reschedule the timer */
	cnx->ack_timer.expires = jiffies + cnx->ack_timeout;
	add_timer(&cnx->ack_timer);
	spin_unlock_irqrestore(&cnx->lock, flags);
}

static int veth_remove(struct vio_dev *vdev)
{
	int i = vdev->unit_address;
	struct net_device *dev;

	dev = veth_dev[i];
	if (dev != NULL) {
		veth_dev[i] = NULL;
		unregister_netdev(dev);
		free_netdev(dev);
	}
	return 0;
}

static int veth_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	int i = vdev->unit_address;
	struct net_device *dev;

	dev = veth_probe_one(i, &vdev->dev);
	if (dev == NULL) {
		veth_remove(vdev);
		return 1;
	}
	veth_dev[i] = dev;

	/* Start the state machine on each connection, to commence
	 * link negotiation */
	for (i = 0; i < HVMAXARCHITECTEDLPS; i++)
		if (veth_cnx[i])
			veth_kick_statemachine(veth_cnx[i]);

	return 0;
}

/**
 * veth_device_table: Used by vio.c to match devices that we
 * support.
 */
static struct vio_device_id veth_device_table[] __devinitdata = {
	{ "vlan", "" },
	{ NULL, NULL }
};
MODULE_DEVICE_TABLE(vio, veth_device_table);

static struct vio_driver veth_driver = {
	.name = "iseries_veth",
	.id_table = veth_device_table,
	.probe = veth_probe,
	.remove = veth_remove
};

/*
 * Module initialization/cleanup
 */

void __exit veth_module_cleanup(void)
{
	int i;

	vio_unregister_driver(&veth_driver);

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i)
		veth_stop_connection(i);

	HvLpEvent_unregisterHandler(HvLpEvent_Type_VirtualLan);

	/* Hypervisor callbacks may have scheduled more work while we
	 * were destroying connections. Now that we've disconnected from
	 * the hypervisor make sure everything's finished. */
	flush_scheduled_work();

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i)
		veth_destroy_connection(i);

}
module_exit(veth_module_cleanup);

int __init veth_module_init(void)
{
	int i;
	int rc;

	this_lp = HvLpConfig_getLpIndex_outline();

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		rc = veth_init_connection(i);
		if (rc != 0) {
			veth_module_cleanup();
			return rc;
		}
	}

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan,
				  &veth_handle_event);

	return vio_register_driver(&veth_driver);
}
module_init(veth_module_init);
