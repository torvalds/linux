/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */
/*
 * IBM eServer iSeries Virtual Ethernet Device Driver
 * Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.
 * Substantially cleaned up by:
 * Copyright (C) 2003 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 * Copyright (C) 2004-2005 Michael Ellerman, IBM Corporation.
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

#include <linux/module.h>
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
#include <linux/if_ether.h>

#include <asm/abs_addr.h>
#include <asm/iseries/mf.h>
#include <asm/uaccess.h>
#include <asm/firmware.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iommu.h>
#include <asm/vio.h>

#undef DEBUG

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

#define VETH_EVENT_CAP	(0)
#define VETH_EVENT_FRAMES	(1)
#define VETH_EVENT_MONITOR	(2)
#define VETH_EVENT_FRAMES_ACK	(3)

#define VETH_MAX_ACKS_PER_MSG	(20)
#define VETH_MAX_FRAMES_PER_MSG	(6)

struct veth_frames_data {
	u32 addr[VETH_MAX_FRAMES_PER_MSG];
	u16 len[VETH_MAX_FRAMES_PER_MSG];
	u32 eofmask;
};
#define VETH_EOF_SHIFT		(32-VETH_MAX_FRAMES_PER_MSG)

struct veth_frames_ack_data {
	u16 token[VETH_MAX_ACKS_PER_MSG];
};

struct veth_cap_data {
	u8 caps_version;
	u8 rsvd1;
	u16 num_buffers;
	u16 ack_threshold;
	u16 rsvd2;
	u32 ack_timeout;
	u32 rsvd3;
	u64 rsvd4[3];
};

struct veth_lpevent {
	struct HvLpEvent base_event;
	union {
		struct veth_cap_data caps_data;
		struct veth_frames_data frames_data;
		struct veth_frames_ack_data frames_ack_data;
	} u;

};

#define DRV_NAME	"iseries_veth"
#define DRV_VERSION	"2.0"

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
	struct veth_frames_data data;
	int token;
	int in_use;
	struct sk_buff *skb;
	struct device *dev;
};

struct veth_lpar_connection {
	HvLpIndex remote_lp;
	struct delayed_work statemachine_wq;
	struct veth_msg *msgs;
	int num_events;
	struct veth_cap_data local_caps;

	struct kobject kobject;
	struct timer_list ack_timer;

	struct timer_list reset_timer;
	unsigned int reset_timeout;
	unsigned long last_contact;
	int outstanding_tx;

	spinlock_t lock;
	unsigned long state;
	HvLpInstanceId src_inst;
	HvLpInstanceId dst_inst;
	struct veth_lpevent cap_event, cap_ack_event;
	u16 pending_acks[VETH_MAX_ACKS_PER_MSG];
	u32 num_pending_acks;

	int num_ack_events;
	struct veth_cap_data remote_caps;
	u32 ack_timeout;

	struct veth_msg *msg_stack_head;
};

struct veth_port {
	struct device *dev;
	u64 mac_addr;
	HvLpIndexMap lpar_map;

	/* queue_lock protects the stopped_map and dev's queue. */
	spinlock_t queue_lock;
	HvLpIndexMap stopped_map;

	/* mcast_gate protects promiscuous, num_mcast & mcast_addr. */
	rwlock_t mcast_gate;
	int promiscuous;
	int num_mcast;
	u64 mcast_addr[VETH_MAX_MCAST];

	struct kobject kobject;
};

static HvLpIndex this_lp;
static struct veth_lpar_connection *veth_cnx[HVMAXARCHITECTEDLPS]; /* = 0 */
static struct net_device *veth_dev[HVMAXARCHITECTEDVIRTUALLANS]; /* = 0 */

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void veth_recycle_msg(struct veth_lpar_connection *, struct veth_msg *);
static void veth_wake_queues(struct veth_lpar_connection *cnx);
static void veth_stop_queues(struct veth_lpar_connection *cnx);
static void veth_receive(struct veth_lpar_connection *, struct veth_lpevent *);
static void veth_release_connection(struct kobject *kobject);
static void veth_timed_ack(unsigned long ptr);
static void veth_timed_reset(unsigned long ptr);

/*
 * Utility functions
 */

#define veth_info(fmt, args...) \
	printk(KERN_INFO DRV_NAME ": " fmt, ## args)

#define veth_error(fmt, args...) \
	printk(KERN_ERR DRV_NAME ": Error: " fmt, ## args)

#ifdef DEBUG
#define veth_debug(fmt, args...) \
	printk(KERN_DEBUG DRV_NAME ": " fmt, ## args)
#else
#define veth_debug(fmt, args...) do {} while (0)
#endif

/* You must hold the connection's lock when you call this function. */
static inline void veth_stack_push(struct veth_lpar_connection *cnx,
				   struct veth_msg *msg)
{
	msg->next = cnx->msg_stack_head;
	cnx->msg_stack_head = msg;
}

/* You must hold the connection's lock when you call this function. */
static inline struct veth_msg *veth_stack_pop(struct veth_lpar_connection *cnx)
{
	struct veth_msg *msg;

	msg = cnx->msg_stack_head;
	if (msg)
		cnx->msg_stack_head = cnx->msg_stack_head->next;

	return msg;
}

/* You must hold the connection's lock when you call this function. */
static inline int veth_stack_is_empty(struct veth_lpar_connection *cnx)
{
	return cnx->msg_stack_head == NULL;
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
	struct veth_allocation vc =
		{ COMPLETION_INITIALIZER_ONSTACK(vc.c), 0 };

	mf_allocate_lp_events(rlp, HvLpEvent_Type_VirtualLan,
			    sizeof(struct veth_lpevent), number,
			    &veth_complete_allocation, &vc);
	wait_for_completion(&vc.c);

	return vc.num;
}

/*
 * sysfs support
 */

struct veth_cnx_attribute {
	struct attribute attr;
	ssize_t (*show)(struct veth_lpar_connection *, char *buf);
	ssize_t (*store)(struct veth_lpar_connection *, const char *buf);
};

static ssize_t veth_cnx_attribute_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct veth_cnx_attribute *cnx_attr;
	struct veth_lpar_connection *cnx;

	cnx_attr = container_of(attr, struct veth_cnx_attribute, attr);
	cnx = container_of(kobj, struct veth_lpar_connection, kobject);

	if (!cnx_attr->show)
		return -EIO;

	return cnx_attr->show(cnx, buf);
}

#define CUSTOM_CNX_ATTR(_name, _format, _expression)			\
static ssize_t _name##_show(struct veth_lpar_connection *cnx, char *buf)\
{									\
	return sprintf(buf, _format, _expression);			\
}									\
struct veth_cnx_attribute veth_cnx_attr_##_name = __ATTR_RO(_name)

#define SIMPLE_CNX_ATTR(_name)	\
	CUSTOM_CNX_ATTR(_name, "%lu\n", (unsigned long)cnx->_name)

SIMPLE_CNX_ATTR(outstanding_tx);
SIMPLE_CNX_ATTR(remote_lp);
SIMPLE_CNX_ATTR(num_events);
SIMPLE_CNX_ATTR(src_inst);
SIMPLE_CNX_ATTR(dst_inst);
SIMPLE_CNX_ATTR(num_pending_acks);
SIMPLE_CNX_ATTR(num_ack_events);
CUSTOM_CNX_ATTR(ack_timeout, "%d\n", jiffies_to_msecs(cnx->ack_timeout));
CUSTOM_CNX_ATTR(reset_timeout, "%d\n", jiffies_to_msecs(cnx->reset_timeout));
CUSTOM_CNX_ATTR(state, "0x%.4lX\n", cnx->state);
CUSTOM_CNX_ATTR(last_contact, "%d\n", cnx->last_contact ?
		jiffies_to_msecs(jiffies - cnx->last_contact) : 0);

#define GET_CNX_ATTR(_name)	(&veth_cnx_attr_##_name.attr)

static struct attribute *veth_cnx_default_attrs[] = {
	GET_CNX_ATTR(outstanding_tx),
	GET_CNX_ATTR(remote_lp),
	GET_CNX_ATTR(num_events),
	GET_CNX_ATTR(reset_timeout),
	GET_CNX_ATTR(last_contact),
	GET_CNX_ATTR(state),
	GET_CNX_ATTR(src_inst),
	GET_CNX_ATTR(dst_inst),
	GET_CNX_ATTR(num_pending_acks),
	GET_CNX_ATTR(num_ack_events),
	GET_CNX_ATTR(ack_timeout),
	NULL
};

static struct sysfs_ops veth_cnx_sysfs_ops = {
		.show = veth_cnx_attribute_show
};

static struct kobj_type veth_lpar_connection_ktype = {
	.release	= veth_release_connection,
	.sysfs_ops	= &veth_cnx_sysfs_ops,
	.default_attrs	= veth_cnx_default_attrs
};

struct veth_port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct veth_port *, char *buf);
	ssize_t (*store)(struct veth_port *, const char *buf);
};

static ssize_t veth_port_attribute_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct veth_port_attribute *port_attr;
	struct veth_port *port;

	port_attr = container_of(attr, struct veth_port_attribute, attr);
	port = container_of(kobj, struct veth_port, kobject);

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(port, buf);
}

#define CUSTOM_PORT_ATTR(_name, _format, _expression)			\
static ssize_t _name##_show(struct veth_port *port, char *buf)		\
{									\
	return sprintf(buf, _format, _expression);			\
}									\
struct veth_port_attribute veth_port_attr_##_name = __ATTR_RO(_name)

#define SIMPLE_PORT_ATTR(_name)	\
	CUSTOM_PORT_ATTR(_name, "%lu\n", (unsigned long)port->_name)

SIMPLE_PORT_ATTR(promiscuous);
SIMPLE_PORT_ATTR(num_mcast);
CUSTOM_PORT_ATTR(lpar_map, "0x%X\n", port->lpar_map);
CUSTOM_PORT_ATTR(stopped_map, "0x%X\n", port->stopped_map);
CUSTOM_PORT_ATTR(mac_addr, "0x%lX\n", port->mac_addr);

#define GET_PORT_ATTR(_name)	(&veth_port_attr_##_name.attr)
static struct attribute *veth_port_default_attrs[] = {
	GET_PORT_ATTR(mac_addr),
	GET_PORT_ATTR(lpar_map),
	GET_PORT_ATTR(stopped_map),
	GET_PORT_ATTR(promiscuous),
	GET_PORT_ATTR(num_mcast),
	NULL
};

static struct sysfs_ops veth_port_sysfs_ops = {
	.show = veth_port_attribute_show
};

static struct kobj_type veth_port_ktype = {
	.sysfs_ops	= &veth_port_sysfs_ops,
	.default_attrs	= veth_port_default_attrs
};

/*
 * LPAR connection code
 */

static inline void veth_kick_statemachine(struct veth_lpar_connection *cnx)
{
	schedule_delayed_work(&cnx->statemachine_wq, 0);
}

static void veth_take_cap(struct veth_lpar_connection *cnx,
			  struct veth_lpevent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	/* Receiving caps may mean the other end has just come up, so
	 * we need to reload the instance ID of the far end */
	cnx->dst_inst =
		HvCallEvent_getTargetLpInstanceId(cnx->remote_lp,
						  HvLpEvent_Type_VirtualLan);

	if (cnx->state & VETH_STATE_GOTCAPS) {
		veth_error("Received a second capabilities from LPAR %d.\n",
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
			      struct veth_lpevent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	if (cnx->state & VETH_STATE_GOTCAPACK) {
		veth_error("Received a second capabilities ack from LPAR %d.\n",
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
				  struct veth_lpevent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->lock, flags);
	veth_debug("cnx %d: lost connection.\n", cnx->remote_lp);

	/* Avoid kicking the statemachine once we're shutdown.
	 * It's unnecessary and it could break veth_stop_connection(). */

	if (! (cnx->state & VETH_STATE_SHUTDOWN)) {
		cnx->state |= VETH_STATE_RESET;
		veth_kick_statemachine(cnx);
	}
	spin_unlock_irqrestore(&cnx->lock, flags);
}

static void veth_handle_ack(struct veth_lpevent *event)
{
	HvLpIndex rlp = event->base_event.xTargetLp;
	struct veth_lpar_connection *cnx = veth_cnx[rlp];

	BUG_ON(! cnx);

	switch (event->base_event.xSubtype) {
	case VETH_EVENT_CAP:
		veth_take_cap_ack(cnx, event);
		break;
	case VETH_EVENT_MONITOR:
		veth_take_monitor_ack(cnx, event);
		break;
	default:
		veth_error("Unknown ack type %d from LPAR %d.\n",
				event->base_event.xSubtype, rlp);
	};
}

static void veth_handle_int(struct veth_lpevent *event)
{
	HvLpIndex rlp = event->base_event.xSourceLp;
	struct veth_lpar_connection *cnx = veth_cnx[rlp];
	unsigned long flags;
	int i, acked = 0;

	BUG_ON(! cnx);

	switch (event->base_event.xSubtype) {
	case VETH_EVENT_CAP:
		veth_take_cap(cnx, event);
		break;
	case VETH_EVENT_MONITOR:
		/* do nothing... this'll hang out here til we're dead,
		 * and the hypervisor will return it for us. */
		break;
	case VETH_EVENT_FRAMES_ACK:
		spin_lock_irqsave(&cnx->lock, flags);

		for (i = 0; i < VETH_MAX_ACKS_PER_MSG; ++i) {
			u16 msgnum = event->u.frames_ack_data.token[i];

			if (msgnum < VETH_NUMBUFFERS) {
				veth_recycle_msg(cnx, cnx->msgs + msgnum);
				cnx->outstanding_tx--;
				acked++;
			}
		}

		if (acked > 0) {
			cnx->last_contact = jiffies;
			veth_wake_queues(cnx);
		}

		spin_unlock_irqrestore(&cnx->lock, flags);
		break;
	case VETH_EVENT_FRAMES:
		veth_receive(cnx, event);
		break;
	default:
		veth_error("Unknown interrupt type %d from LPAR %d.\n",
				event->base_event.xSubtype, rlp);
	};
}

static void veth_handle_event(struct HvLpEvent *event)
{
	struct veth_lpevent *veth_event = (struct veth_lpevent *)event;

	if (hvlpevent_is_ack(event))
		veth_handle_ack(veth_event);
	else
		veth_handle_int(veth_event);
}

static int veth_process_caps(struct veth_lpar_connection *cnx)
{
	struct veth_cap_data *remote_caps = &cnx->remote_caps;
	int num_acks_needed;

	/* Convert timer to jiffies */
	cnx->ack_timeout = remote_caps->ack_timeout * HZ / 1000000;

	if ( (remote_caps->num_buffers == 0)
	     || (remote_caps->ack_threshold > VETH_MAX_ACKS_PER_MSG)
	     || (remote_caps->ack_threshold == 0)
	     || (cnx->ack_timeout == 0) ) {
		veth_error("Received incompatible capabilities from LPAR %d.\n",
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
			veth_error("Couldn't allocate enough ack events "
					"for LPAR %d.\n", cnx->remote_lp);

			return HvLpEvent_Rc_BufferNotAvailable;
		}
	}


	return HvLpEvent_Rc_Good;
}

/* FIXME: The gotos here are a bit dubious */
static void veth_statemachine(struct work_struct *work)
{
	struct veth_lpar_connection *cnx =
		container_of(work, struct veth_lpar_connection,
			     statemachine_wq.work);
	int rlp = cnx->remote_lp;
	int rc;

	spin_lock_irq(&cnx->lock);

 restart:
	if (cnx->state & VETH_STATE_RESET) {
		if (cnx->state & VETH_STATE_OPEN)
			HvCallEvent_closeLpEventPath(cnx->remote_lp,
						     HvLpEvent_Type_VirtualLan);

		/*
		 * Reset ack data. This prevents the ack_timer actually
		 * doing anything, even if it runs one more time when
		 * we drop the lock below.
		 */
		memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
		cnx->num_pending_acks = 0;

		cnx->state &= ~(VETH_STATE_RESET | VETH_STATE_SENTMON
				| VETH_STATE_OPEN | VETH_STATE_SENTCAPS
				| VETH_STATE_GOTCAPACK | VETH_STATE_GOTCAPS
				| VETH_STATE_SENTCAPACK | VETH_STATE_READY);

		/* Clean up any leftover messages */
		if (cnx->msgs) {
			int i;
			for (i = 0; i < VETH_NUMBUFFERS; ++i)
				veth_recycle_msg(cnx, cnx->msgs + i);
		}

		cnx->outstanding_tx = 0;
		veth_wake_queues(cnx);

		/* Drop the lock so we can do stuff that might sleep or
		 * take other locks. */
		spin_unlock_irq(&cnx->lock);

		del_timer_sync(&cnx->ack_timer);
		del_timer_sync(&cnx->reset_timer);

		spin_lock_irq(&cnx->lock);

		if (cnx->state & VETH_STATE_RESET)
			goto restart;

		/* Hack, wait for the other end to reset itself. */
		if (! (cnx->state & VETH_STATE_SHUTDOWN)) {
			schedule_delayed_work(&cnx->statemachine_wq, 5 * HZ);
			goto out;
		}
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
		rc = veth_signalevent(cnx, VETH_EVENT_MONITOR,
				      HvLpEvent_AckInd_DoAck,
				      HvLpEvent_AckType_DeferredAck,
				      0, 0, 0, 0, 0, 0);

		if (rc == HvLpEvent_Rc_Good) {
			cnx->state |= VETH_STATE_SENTMON;
		} else {
			if ( (rc != HvLpEvent_Rc_PartitionDead)
			     && (rc != HvLpEvent_Rc_PathClosed) )
				veth_error("Error sending monitor to LPAR %d, "
						"rc = %d\n", rlp, rc);

			/* Oh well, hope we get a cap from the other
			 * end and do better when that kicks us */
			goto out;
		}
	}

	if ( (cnx->state & VETH_STATE_OPEN)
	     && !(cnx->state & VETH_STATE_SENTCAPS)) {
		u64 *rawcap = (u64 *)&cnx->local_caps;

		rc = veth_signalevent(cnx, VETH_EVENT_CAP,
				      HvLpEvent_AckInd_DoAck,
				      HvLpEvent_AckType_ImmediateAck,
				      0, rawcap[0], rawcap[1], rawcap[2],
				      rawcap[3], rawcap[4]);

		if (rc == HvLpEvent_Rc_Good) {
			cnx->state |= VETH_STATE_SENTCAPS;
		} else {
			if ( (rc != HvLpEvent_Rc_PartitionDead)
			     && (rc != HvLpEvent_Rc_PathClosed) )
				veth_error("Error sending caps to LPAR %d, "
						"rc = %d\n", rlp, rc);

			/* Oh well, hope we get a cap from the other
			 * end and do better when that kicks us */
			goto out;
		}
	}

	if ((cnx->state & VETH_STATE_GOTCAPS)
	    && !(cnx->state & VETH_STATE_SENTCAPACK)) {
		struct veth_cap_data *remote_caps = &cnx->remote_caps;

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
			veth_error("Caps rejected by LPAR %d, rc = %d\n",
					rlp, cnx->cap_ack_event.base_event.xRc);
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
	veth_error("Unrecoverable error on connection to LPAR %d, shutting down"
			" (state = 0x%04lx)\n", rlp, cnx->state);
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

	cnx = kzalloc(sizeof(*cnx), GFP_KERNEL);
	if (! cnx)
		return -ENOMEM;

	cnx->remote_lp = rlp;
	spin_lock_init(&cnx->lock);
	INIT_DELAYED_WORK(&cnx->statemachine_wq, veth_statemachine);

	init_timer(&cnx->ack_timer);
	cnx->ack_timer.function = veth_timed_ack;
	cnx->ack_timer.data = (unsigned long) cnx;

	init_timer(&cnx->reset_timer);
	cnx->reset_timer.function = veth_timed_reset;
	cnx->reset_timer.data = (unsigned long) cnx;
	cnx->reset_timeout = 5 * HZ * (VETH_ACKTIMEOUT / 1000000);

	memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));

	veth_cnx[rlp] = cnx;

	/* This gets us 1 reference, which is held on behalf of the driver
	 * infrastructure. It's released at module unload. */
	kobject_init(&cnx->kobject, &veth_lpar_connection_ktype);

	msgs = kcalloc(VETH_NUMBUFFERS, sizeof(struct veth_msg), GFP_KERNEL);
	if (! msgs) {
		veth_error("Can't allocate buffers for LPAR %d.\n", rlp);
		return -ENOMEM;
	}

	cnx->msgs = msgs;

	for (i = 0; i < VETH_NUMBUFFERS; i++) {
		msgs[i].token = i;
		veth_stack_push(cnx, msgs + i);
	}

	cnx->num_events = veth_allocate_events(rlp, 2 + VETH_NUMBUFFERS);

	if (cnx->num_events < (2 + VETH_NUMBUFFERS)) {
		veth_error("Can't allocate enough events for LPAR %d.\n", rlp);
		return -ENOMEM;
	}

	cnx->local_caps.num_buffers = VETH_NUMBUFFERS;
	cnx->local_caps.ack_threshold = ACK_THRESHOLD;
	cnx->local_caps.ack_timeout = VETH_ACKTIMEOUT;

	return 0;
}

static void veth_stop_connection(struct veth_lpar_connection *cnx)
{
	if (!cnx)
		return;

	spin_lock_irq(&cnx->lock);
	cnx->state |= VETH_STATE_RESET | VETH_STATE_SHUTDOWN;
	veth_kick_statemachine(cnx);
	spin_unlock_irq(&cnx->lock);

	/* There's a slim chance the reset code has just queued the
	 * statemachine to run in five seconds. If so we need to cancel
	 * that and requeue the work to run now. */
	if (cancel_delayed_work(&cnx->statemachine_wq)) {
		spin_lock_irq(&cnx->lock);
		veth_kick_statemachine(cnx);
		spin_unlock_irq(&cnx->lock);
	}

	/* Wait for the state machine to run. */
	flush_scheduled_work();
}

static void veth_destroy_connection(struct veth_lpar_connection *cnx)
{
	if (!cnx)
		return;

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

	kfree(cnx->msgs);
	veth_cnx[cnx->remote_lp] = NULL;
	kfree(cnx);
}

static void veth_release_connection(struct kobject *kobj)
{
	struct veth_lpar_connection *cnx;
	cnx = container_of(kobj, struct veth_lpar_connection, kobject);
	veth_stop_connection(cnx);
	veth_destroy_connection(cnx);
}

/*
 * net_device code
 */

static int veth_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int veth_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
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

	if ((dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
			(dev->mc_count > VETH_MAX_MCAST)) {
		port->promiscuous = 1;
	} else {
		struct dev_mc_list *dmi = dev->mc_list;
		int i;

		port->promiscuous = 0;

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
	strncpy(info->driver, DRV_NAME, sizeof(info->driver) - 1);
	info->driver[sizeof(info->driver) - 1] = '\0';
	strncpy(info->version, DRV_VERSION, sizeof(info->version) - 1);
	info->version[sizeof(info->version) - 1] = '\0';
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

static const struct ethtool_ops ops = {
	.get_drvinfo = veth_get_drvinfo,
	.get_settings = veth_get_settings,
	.get_link = veth_get_link,
};

static struct net_device *veth_probe_one(int vlan,
		struct vio_dev *vio_dev)
{
	struct net_device *dev;
	struct veth_port *port;
	struct device *vdev = &vio_dev->dev;
	int i, rc;
	const unsigned char *mac_addr;

	mac_addr = vio_get_attribute(vio_dev, "local-mac-address", NULL);
	if (mac_addr == NULL)
		mac_addr = vio_get_attribute(vio_dev, "mac-address", NULL);
	if (mac_addr == NULL) {
		veth_error("Unable to fetch MAC address from device tree.\n");
		return NULL;
	}

	dev = alloc_etherdev(sizeof (struct veth_port));
	if (! dev) {
		veth_error("Unable to allocate net_device structure!\n");
		return NULL;
	}

	port = (struct veth_port *) dev->priv;

	spin_lock_init(&port->queue_lock);
	rwlock_init(&port->mcast_gate);
	port->stopped_map = 0;

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		HvLpVirtualLanIndexMap map;

		if (i == this_lp)
			continue;
		map = HvLpConfig_getVirtualLanIndexMapForLp(i);
		if (map & (0x8000 >> vlan))
			port->lpar_map |= (1 << i);
	}
	port->dev = vdev;

	memcpy(dev->dev_addr, mac_addr, ETH_ALEN);

	dev->mtu = VETH_MAX_MTU;

	memcpy(&port->mac_addr, mac_addr, ETH_ALEN);

	dev->open = veth_open;
	dev->hard_start_xmit = veth_start_xmit;
	dev->stop = veth_close;
	dev->change_mtu = veth_change_mtu;
	dev->set_mac_address = NULL;
	dev->set_multicast_list = veth_set_multicast_list;
	SET_ETHTOOL_OPS(dev, &ops);

	SET_NETDEV_DEV(dev, vdev);

	rc = register_netdev(dev);
	if (rc != 0) {
		veth_error("Failed registering net device for vlan%d.\n", vlan);
		free_netdev(dev);
		return NULL;
	}

	kobject_init(&port->kobject, &veth_port_ktype);
	if (0 != kobject_add(&port->kobject, &dev->dev.kobj, "veth_port"))
		veth_error("Failed adding port for %s to sysfs.\n", dev->name);

	veth_info("%s attached to iSeries vlan %d (LPAR map = 0x%.4X)\n",
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
	struct veth_msg *msg = NULL;
	unsigned long flags;

	if (! cnx)
		return 0;

	spin_lock_irqsave(&cnx->lock, flags);

	if (! (cnx->state & VETH_STATE_READY))
		goto no_error;

	if ((skb->len - ETH_HLEN) > VETH_MAX_MTU)
		goto drop;

	msg = veth_stack_pop(cnx);
	if (! msg)
		goto drop;

	msg->in_use = 1;
	msg->skb = skb_get(skb);

	msg->data.addr[0] = dma_map_single(port->dev, skb->data,
				skb->len, DMA_TO_DEVICE);

	if (dma_mapping_error(msg->data.addr[0]))
		goto recycle_and_drop;

	msg->dev = port->dev;
	msg->data.len[0] = skb->len;
	msg->data.eofmask = 1 << VETH_EOF_SHIFT;

	rc = veth_signaldata(cnx, VETH_EVENT_FRAMES, msg->token, &msg->data);

	if (rc != HvLpEvent_Rc_Good)
		goto recycle_and_drop;

	/* If the timer's not already running, start it now. */
	if (0 == cnx->outstanding_tx)
		mod_timer(&cnx->reset_timer, jiffies + cnx->reset_timeout);

	cnx->last_contact = jiffies;
	cnx->outstanding_tx++;

	if (veth_stack_is_empty(cnx))
		veth_stop_queues(cnx);

 no_error:
	spin_unlock_irqrestore(&cnx->lock, flags);
	return 0;

 recycle_and_drop:
	veth_recycle_msg(cnx, msg);
 drop:
	spin_unlock_irqrestore(&cnx->lock, flags);
	return 1;
}

static void veth_transmit_to_many(struct sk_buff *skb,
					  HvLpIndexMap lpmask,
					  struct net_device *dev)
{
	int i, success, error;

	success = error = 0;

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		if ((lpmask & (1 << i)) == 0)
			continue;

		if (veth_transmit_to_one(skb, i, dev))
			error = 1;
		else
			success = 1;
	}

	if (error)
		dev->stats.tx_errors++;

	if (success) {
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
	}
}

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *frame = skb->data;
	struct veth_port *port = (struct veth_port *) dev->priv;
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

	veth_transmit_to_many(skb, lpmask, dev);

	dev_kfree_skb(skb);

	return 0;
}

/* You must hold the connection's lock when you call this function. */
static void veth_recycle_msg(struct veth_lpar_connection *cnx,
			     struct veth_msg *msg)
{
	u32 dma_address, dma_length;

	if (msg->in_use) {
		msg->in_use = 0;
		dma_address = msg->data.addr[0];
		dma_length = msg->data.len[0];

		if (!dma_mapping_error(dma_address))
			dma_unmap_single(msg->dev, dma_address, dma_length,
					DMA_TO_DEVICE);

		if (msg->skb) {
			dev_kfree_skb_any(msg->skb);
			msg->skb = NULL;
		}

		memset(&msg->data, 0, sizeof(msg->data));
		veth_stack_push(cnx, msg);
	} else if (cnx->state & VETH_STATE_OPEN) {
		veth_error("Non-pending frame (# %d) acked by LPAR %d.\n",
				cnx->remote_lp, msg->token);
	}
}

static void veth_wake_queues(struct veth_lpar_connection *cnx)
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

		spin_lock_irqsave(&port->queue_lock, flags);

		port->stopped_map &= ~(1 << cnx->remote_lp);

		if (0 == port->stopped_map && netif_queue_stopped(dev)) {
			veth_debug("cnx %d: woke queue for %s.\n",
					cnx->remote_lp, dev->name);
			netif_wake_queue(dev);
		}
		spin_unlock_irqrestore(&port->queue_lock, flags);
	}
}

static void veth_stop_queues(struct veth_lpar_connection *cnx)
{
	int i;

	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; i++) {
		struct net_device *dev = veth_dev[i];
		struct veth_port *port;

		if (! dev)
			continue;

		port = (struct veth_port *)dev->priv;

		/* If this cnx is not on the vlan for this port, continue */
		if (! (port->lpar_map & (1 << cnx->remote_lp)))
			continue;

		spin_lock(&port->queue_lock);

		netif_stop_queue(dev);
		port->stopped_map |= (1 << cnx->remote_lp);

		veth_debug("cnx %d: stopped queue for %s, map = 0x%x.\n",
				cnx->remote_lp, dev->name, port->stopped_map);

		spin_unlock(&port->queue_lock);
	}
}

static void veth_timed_reset(unsigned long ptr)
{
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *)ptr;
	unsigned long trigger_time, flags;

	/* FIXME is it possible this fires after veth_stop_connection()?
	 * That would reschedule the statemachine for 5 seconds and probably
	 * execute it after the module's been unloaded. Hmm. */

	spin_lock_irqsave(&cnx->lock, flags);

	if (cnx->outstanding_tx > 0) {
		trigger_time = cnx->last_contact + cnx->reset_timeout;

		if (trigger_time < jiffies) {
			cnx->state |= VETH_STATE_RESET;
			veth_kick_statemachine(cnx);
			veth_error("%d packets not acked by LPAR %d within %d "
					"seconds, resetting.\n",
					cnx->outstanding_tx, cnx->remote_lp,
					cnx->reset_timeout / HZ);
		} else {
			/* Reschedule the timer */
			trigger_time = jiffies + cnx->reset_timeout;
			mod_timer(&cnx->reset_timer, trigger_time);
		}
	}

	spin_unlock_irqrestore(&cnx->lock, flags);
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

	read_lock_irqsave(&port->mcast_gate, flags);

	if (port->promiscuous) {
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
	list[0].addr = iseries_hv_addr(p);
	list[0].size = min(length,
			   PAGE_SIZE - ((unsigned long)p & ~PAGE_MASK));

	done = list[0].size;
	while (done < length) {
		list[i].addr = iseries_hv_addr(p + done);
		list[i].size = min(length-done, PAGE_SIZE);
		done += list[i].size;
		i++;
	}
}

static void veth_flush_acks(struct veth_lpar_connection *cnx)
{
	HvLpEvent_Rc rc;

	rc = veth_signaldata(cnx, VETH_EVENT_FRAMES_ACK,
			     0, &cnx->pending_acks);

	if (rc != HvLpEvent_Rc_Good)
		veth_error("Failed acking frames from LPAR %d, rc = %d\n",
				cnx->remote_lp, (int)rc);

	cnx->num_pending_acks = 0;
	memset(&cnx->pending_acks, 0xff, sizeof(cnx->pending_acks));
}

static void veth_receive(struct veth_lpar_connection *cnx,
			 struct veth_lpevent *event)
{
	struct veth_frames_data *senddata = &event->u.frames_data;
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
			veth_error("Missing EOF fragment in event "
					"eofmask = 0x%x startchunk = %d\n",
					(unsigned)senddata->eofmask,
					startchunk);
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
			veth_error("Received oversize frame from LPAR %d "
					"(length = %d)\n",
					cnx->remote_lp, length);
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
					    iseries_hv_addr(&local_list),
					    iseries_hv_addr(&remote_list),
					    length);
		if (rc != HvLpDma_Rc_Good) {
			dev_kfree_skb_irq(skb);
			continue;
		}

		vlan = skb->data[9];
		dev = veth_dev[vlan];
		if (! dev) {
			/*
			 * Some earlier versions of the driver sent
			 * broadcasts down all connections, even to lpars
			 * that weren't on the relevant vlan. So ignore
			 * packets belonging to a vlan we're not on.
			 * We can also be here if we receive packets while
			 * the driver is going down, because then dev is NULL.
			 */
			dev_kfree_skb_irq(skb);
			continue;
		}

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
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_NONE;
		netif_rx(skb);	/* send it up */
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += length;
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
	struct veth_lpar_connection *cnx;
	struct net_device *dev;
	struct veth_port *port;
	int i;

	dev = veth_dev[vdev->unit_address];

	if (! dev)
		return 0;

	port = netdev_priv(dev);

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		cnx = veth_cnx[i];

		if (cnx && (port->lpar_map & (1 << i))) {
			/* Drop our reference to connections on our VLAN */
			kobject_put(&cnx->kobject);
		}
	}

	veth_dev[vdev->unit_address] = NULL;
	kobject_del(&port->kobject);
	kobject_put(&port->kobject);
	unregister_netdev(dev);
	free_netdev(dev);

	return 0;
}

static int veth_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	int i = vdev->unit_address;
	struct net_device *dev;
	struct veth_port *port;

	dev = veth_probe_one(i, vdev);
	if (dev == NULL) {
		veth_remove(vdev);
		return 1;
	}
	veth_dev[i] = dev;

	port = (struct veth_port*)netdev_priv(dev);

	/* Start the state machine on each connection on this vlan. If we're
	 * the first dev to do so this will commence link negotiation */
	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		struct veth_lpar_connection *cnx;

		if (! (port->lpar_map & (1 << i)))
			continue;

		cnx = veth_cnx[i];
		if (!cnx)
			continue;

		kobject_get(&cnx->kobject);
		veth_kick_statemachine(cnx);
	}

	return 0;
}

/**
 * veth_device_table: Used by vio.c to match devices that we
 * support.
 */
static struct vio_device_id veth_device_table[] __devinitdata = {
	{ "network", "IBM,iSeries-l-lan" },
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, veth_device_table);

static struct vio_driver veth_driver = {
	.id_table = veth_device_table,
	.probe = veth_probe,
	.remove = veth_remove,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	}
};

/*
 * Module initialization/cleanup
 */

static void __exit veth_module_cleanup(void)
{
	int i;
	struct veth_lpar_connection *cnx;

	/* Disconnect our "irq" to stop events coming from the Hypervisor. */
	HvLpEvent_unregisterHandler(HvLpEvent_Type_VirtualLan);

	/* Make sure any work queued from Hypervisor callbacks is finished. */
	flush_scheduled_work();

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		cnx = veth_cnx[i];

		if (!cnx)
			continue;

		/* Remove the connection from sysfs */
		kobject_del(&cnx->kobject);
		/* Drop the driver's reference to the connection */
		kobject_put(&cnx->kobject);
	}

	/* Unregister the driver, which will close all the netdevs and stop
	 * the connections when they're no longer referenced. */
	vio_unregister_driver(&veth_driver);
}
module_exit(veth_module_cleanup);

static int __init veth_module_init(void)
{
	int i;
	int rc;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return -ENODEV;

	this_lp = HvLpConfig_getLpIndex_outline();

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		rc = veth_init_connection(i);
		if (rc != 0)
			goto error;
	}

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan,
				  &veth_handle_event);

	rc = vio_register_driver(&veth_driver);
	if (rc != 0)
		goto error;

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct kobject *kobj;

		if (!veth_cnx[i])
			continue;

		kobj = &veth_cnx[i]->kobject;
		/* If the add failes, complain but otherwise continue */
		if (0 != driver_add_kobj(&veth_driver.driver, kobj,
					"cnx%.2d", veth_cnx[i]->remote_lp))
			veth_error("cnx %d: Failed adding to sysfs.\n", i);
	}

	return 0;

error:
	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		veth_destroy_connection(veth_cnx[i]);
	}

	return rc;
}
module_init(veth_module_init);
