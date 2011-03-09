/*
 *  scsi_netlink.c  - SCSI Transport Netlink Interface
 *
 *  Copyright (C) 2006   James Smart, Emulex Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/security.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/netlink.h>

#include <scsi/scsi_netlink.h>
#include "scsi_priv.h"

struct sock *scsi_nl_sock = NULL;
EXPORT_SYMBOL_GPL(scsi_nl_sock);

static DEFINE_SPINLOCK(scsi_nl_lock);
static struct list_head scsi_nl_drivers;

static u32	scsi_nl_state;
#define STATE_EHANDLER_BSY		0x00000001

struct scsi_nl_transport {
	int (*msg_handler)(struct sk_buff *);
	void (*event_handler)(struct notifier_block *, unsigned long, void *);
	unsigned int refcnt;
	int flags;
};

/* flags values (bit flags) */
#define HANDLER_DELETING		0x1

static struct scsi_nl_transport transports[SCSI_NL_MAX_TRANSPORTS] =
	{ {NULL, }, };


struct scsi_nl_drvr {
	struct list_head next;
	int (*dmsg_handler)(struct Scsi_Host *shost, void *payload,
				 u32 len, u32 pid);
	void (*devt_handler)(struct notifier_block *nb,
				 unsigned long event, void *notify_ptr);
	struct scsi_host_template *hostt;
	u64 vendor_id;
	unsigned int refcnt;
	int flags;
};



/**
 * scsi_nl_rcv_msg - Receive message handler.
 * @skb:		socket receive buffer
 *
 * Description: Extracts message from a receive buffer.
 *    Validates message header and calls appropriate transport message handler
 *
 *
 **/
static void
scsi_nl_rcv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct scsi_nl_hdr *hdr;
	unsigned long flags;
	u32 rlen;
	int err, tport;

	while (skb->len >= NLMSG_SPACE(0)) {
		err = 0;

		nlh = nlmsg_hdr(skb);
		if ((nlh->nlmsg_len < (sizeof(*nlh) + sizeof(*hdr))) ||
		    (skb->len < nlh->nlmsg_len)) {
			printk(KERN_WARNING "%s: discarding partial skb\n",
				 __func__);
			return;
		}

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		if (nlh->nlmsg_type != SCSI_TRANSPORT_MSG) {
			err = -EBADMSG;
			goto next_msg;
		}

		hdr = NLMSG_DATA(nlh);
		if ((hdr->version != SCSI_NL_VERSION) ||
		    (hdr->magic != SCSI_NL_MAGIC)) {
			err = -EPROTOTYPE;
			goto next_msg;
		}

		if (security_netlink_recv(skb, CAP_SYS_ADMIN)) {
			err = -EPERM;
			goto next_msg;
		}

		if (nlh->nlmsg_len < (sizeof(*nlh) + hdr->msglen)) {
			printk(KERN_WARNING "%s: discarding partial message\n",
				 __func__);
			goto next_msg;
		}

		/*
		 * Deliver message to the appropriate transport
		 */
		spin_lock_irqsave(&scsi_nl_lock, flags);

		tport = hdr->transport;
		if ((tport < SCSI_NL_MAX_TRANSPORTS) &&
		    !(transports[tport].flags & HANDLER_DELETING) &&
		    (transports[tport].msg_handler)) {
			transports[tport].refcnt++;
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			err = transports[tport].msg_handler(skb);
			spin_lock_irqsave(&scsi_nl_lock, flags);
			transports[tport].refcnt--;
		} else
			err = -ENOENT;

		spin_unlock_irqrestore(&scsi_nl_lock, flags);

next_msg:
		if ((err) || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		skb_pull(skb, rlen);
	}
}


/**
 * scsi_nl_rcv_event - Event handler for a netlink socket.
 * @this:		event notifier block
 * @event:		event type
 * @ptr:		event payload
 *
 **/
static int
scsi_nl_rcv_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;
	struct scsi_nl_drvr *driver;
	unsigned long flags;
	int tport;

	if (n->protocol != NETLINK_SCSITRANSPORT)
		return NOTIFY_DONE;

	spin_lock_irqsave(&scsi_nl_lock, flags);
	scsi_nl_state |= STATE_EHANDLER_BSY;

	/*
	 * Pass event on to any transports that may be listening
	 */
	for (tport = 0; tport < SCSI_NL_MAX_TRANSPORTS; tport++) {
		if (!(transports[tport].flags & HANDLER_DELETING) &&
		    (transports[tport].event_handler)) {
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			transports[tport].event_handler(this, event, ptr);
			spin_lock_irqsave(&scsi_nl_lock, flags);
		}
	}

	/*
	 * Pass event on to any drivers that may be listening
	 */
	list_for_each_entry(driver, &scsi_nl_drivers, next) {
		if (!(driver->flags & HANDLER_DELETING) &&
		    (driver->devt_handler)) {
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			driver->devt_handler(this, event, ptr);
			spin_lock_irqsave(&scsi_nl_lock, flags);
		}
	}

	scsi_nl_state &= ~STATE_EHANDLER_BSY;
	spin_unlock_irqrestore(&scsi_nl_lock, flags);

	return NOTIFY_DONE;
}

static struct notifier_block scsi_netlink_notifier = {
	.notifier_call  = scsi_nl_rcv_event,
};


/*
 * GENERIC SCSI transport receive and event handlers
 */

/**
 * scsi_generic_msg_handler - receive message handler for GENERIC transport messages
 * @skb:		socket receive buffer
 **/
static int
scsi_generic_msg_handler(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct scsi_nl_hdr *snlh = NLMSG_DATA(nlh);
	struct scsi_nl_drvr *driver;
	struct Scsi_Host *shost;
	unsigned long flags;
	int err = 0, match, pid;

	pid = NETLINK_CREDS(skb)->pid;

	switch (snlh->msgtype) {
	case SCSI_NL_SHOST_VENDOR:
		{
		struct scsi_nl_host_vendor_msg *msg = NLMSG_DATA(nlh);

		/* Locate the driver that corresponds to the message */
		spin_lock_irqsave(&scsi_nl_lock, flags);
		match = 0;
		list_for_each_entry(driver, &scsi_nl_drivers, next) {
			if (driver->vendor_id == msg->vendor_id) {
				match = 1;
				break;
			}
		}

		if ((!match) || (!driver->dmsg_handler)) {
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			err = -ESRCH;
			goto rcv_exit;
		}

		if (driver->flags & HANDLER_DELETING) {
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			err = -ESHUTDOWN;
			goto rcv_exit;
		}

		driver->refcnt++;
		spin_unlock_irqrestore(&scsi_nl_lock, flags);


		/* if successful, scsi_host_lookup takes a shost reference */
		shost = scsi_host_lookup(msg->host_no);
		if (!shost) {
			err = -ENODEV;
			goto driver_exit;
		}

		/* is this host owned by the vendor ? */
		if (shost->hostt != driver->hostt) {
			err = -EINVAL;
			goto vendormsg_put;
		}

		/* pass message on to the driver */
		err = driver->dmsg_handler(shost, (void *)&msg[1],
					 msg->vmsg_datalen, pid);

vendormsg_put:
		/* release reference by scsi_host_lookup */
		scsi_host_put(shost);

driver_exit:
		/* release our own reference on the registration object */
		spin_lock_irqsave(&scsi_nl_lock, flags);
		driver->refcnt--;
		spin_unlock_irqrestore(&scsi_nl_lock, flags);
		break;
		}

	default:
		err = -EBADR;
		break;
	}

rcv_exit:
	if (err)
		printk(KERN_WARNING "%s: Msgtype %d failed - err %d\n",
			 __func__, snlh->msgtype, err);
	return err;
}


/**
 * scsi_nl_add_transport -
 *    Registers message and event handlers for a transport. Enables
 *    receipt of netlink messages and events to a transport.
 *
 * @tport:		transport registering handlers
 * @msg_handler:	receive message handler callback
 * @event_handler:	receive event handler callback
 **/
int
scsi_nl_add_transport(u8 tport,
	int (*msg_handler)(struct sk_buff *),
	void (*event_handler)(struct notifier_block *, unsigned long, void *))
{
	unsigned long flags;
	int err = 0;

	if (tport >= SCSI_NL_MAX_TRANSPORTS)
		return -EINVAL;

	spin_lock_irqsave(&scsi_nl_lock, flags);

	if (scsi_nl_state & STATE_EHANDLER_BSY) {
		spin_unlock_irqrestore(&scsi_nl_lock, flags);
		msleep(1);
		spin_lock_irqsave(&scsi_nl_lock, flags);
	}

	if (transports[tport].msg_handler || transports[tport].event_handler) {
		err = -EALREADY;
		goto register_out;
	}

	transports[tport].msg_handler = msg_handler;
	transports[tport].event_handler = event_handler;
	transports[tport].flags = 0;
	transports[tport].refcnt = 0;

register_out:
	spin_unlock_irqrestore(&scsi_nl_lock, flags);

	return err;
}
EXPORT_SYMBOL_GPL(scsi_nl_add_transport);


/**
 * scsi_nl_remove_transport -
 *    Disable transport receiption of messages and events
 *
 * @tport:		transport deregistering handlers
 *
 **/
void
scsi_nl_remove_transport(u8 tport)
{
	unsigned long flags;

	spin_lock_irqsave(&scsi_nl_lock, flags);
	if (scsi_nl_state & STATE_EHANDLER_BSY) {
		spin_unlock_irqrestore(&scsi_nl_lock, flags);
		msleep(1);
		spin_lock_irqsave(&scsi_nl_lock, flags);
	}

	if (tport < SCSI_NL_MAX_TRANSPORTS) {
		transports[tport].flags |= HANDLER_DELETING;

		while (transports[tport].refcnt != 0) {
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			schedule_timeout_uninterruptible(HZ/4);
			spin_lock_irqsave(&scsi_nl_lock, flags);
		}
		transports[tport].msg_handler = NULL;
		transports[tport].event_handler = NULL;
		transports[tport].flags = 0;
	}

	spin_unlock_irqrestore(&scsi_nl_lock, flags);

	return;
}
EXPORT_SYMBOL_GPL(scsi_nl_remove_transport);


/**
 * scsi_nl_add_driver -
 *    A driver is registering its interfaces for SCSI netlink messages
 *
 * @vendor_id:          A unique identification value for the driver.
 * @hostt:		address of the driver's host template. Used
 *			to verify an shost is bound to the driver
 * @nlmsg_handler:	receive message handler callback
 * @nlevt_handler:	receive event handler callback
 *
 * Returns:
 *   0 on Success
 *   error result otherwise
 **/
int
scsi_nl_add_driver(u64 vendor_id, struct scsi_host_template *hostt,
	int (*nlmsg_handler)(struct Scsi_Host *shost, void *payload,
				 u32 len, u32 pid),
	void (*nlevt_handler)(struct notifier_block *nb,
				 unsigned long event, void *notify_ptr))
{
	struct scsi_nl_drvr *driver;
	unsigned long flags;

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (unlikely(!driver)) {
		printk(KERN_ERR "%s: allocation failure\n", __func__);
		return -ENOMEM;
	}

	driver->dmsg_handler = nlmsg_handler;
	driver->devt_handler = nlevt_handler;
	driver->hostt = hostt;
	driver->vendor_id = vendor_id;

	spin_lock_irqsave(&scsi_nl_lock, flags);
	if (scsi_nl_state & STATE_EHANDLER_BSY) {
		spin_unlock_irqrestore(&scsi_nl_lock, flags);
		msleep(1);
		spin_lock_irqsave(&scsi_nl_lock, flags);
	}
	list_add_tail(&driver->next, &scsi_nl_drivers);
	spin_unlock_irqrestore(&scsi_nl_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(scsi_nl_add_driver);


/**
 * scsi_nl_remove_driver -
 *    An driver is unregistering with the SCSI netlink messages
 *
 * @vendor_id:          The unique identification value for the driver.
 **/
void
scsi_nl_remove_driver(u64 vendor_id)
{
	struct scsi_nl_drvr *driver;
	unsigned long flags;

	spin_lock_irqsave(&scsi_nl_lock, flags);
	if (scsi_nl_state & STATE_EHANDLER_BSY) {
		spin_unlock_irqrestore(&scsi_nl_lock, flags);
		msleep(1);
		spin_lock_irqsave(&scsi_nl_lock, flags);
	}

	list_for_each_entry(driver, &scsi_nl_drivers, next) {
		if (driver->vendor_id == vendor_id) {
			driver->flags |= HANDLER_DELETING;
			while (driver->refcnt != 0) {
				spin_unlock_irqrestore(&scsi_nl_lock, flags);
				schedule_timeout_uninterruptible(HZ/4);
				spin_lock_irqsave(&scsi_nl_lock, flags);
			}
			list_del(&driver->next);
			kfree(driver);
			spin_unlock_irqrestore(&scsi_nl_lock, flags);
			return;
		}
	}

	spin_unlock_irqrestore(&scsi_nl_lock, flags);

	printk(KERN_ERR "%s: removal of driver failed - vendor_id 0x%llx\n",
	       __func__, (unsigned long long)vendor_id);
	return;
}
EXPORT_SYMBOL_GPL(scsi_nl_remove_driver);


/**
 * scsi_netlink_init - Called by SCSI subsystem to initialize
 * 	the SCSI transport netlink interface
 *
 **/
void
scsi_netlink_init(void)
{
	int error;

	INIT_LIST_HEAD(&scsi_nl_drivers);

	error = netlink_register_notifier(&scsi_netlink_notifier);
	if (error) {
		printk(KERN_ERR "%s: register of event handler failed - %d\n",
				__func__, error);
		return;
	}

	scsi_nl_sock = netlink_kernel_create(&init_net, NETLINK_SCSITRANSPORT,
				SCSI_NL_GRP_CNT, scsi_nl_rcv_msg, NULL,
				THIS_MODULE);
	if (!scsi_nl_sock) {
		printk(KERN_ERR "%s: register of recieve handler failed\n",
				__func__);
		netlink_unregister_notifier(&scsi_netlink_notifier);
		return;
	}

	/* Register the entry points for the generic SCSI transport */
	error = scsi_nl_add_transport(SCSI_NL_TRANSPORT,
				scsi_generic_msg_handler, NULL);
	if (error)
		printk(KERN_ERR "%s: register of GENERIC transport handler"
				"  failed - %d\n", __func__, error);
	return;
}


/**
 * scsi_netlink_exit - Called by SCSI subsystem to disable the SCSI transport netlink interface
 *
 **/
void
scsi_netlink_exit(void)
{
	scsi_nl_remove_transport(SCSI_NL_TRANSPORT);

	if (scsi_nl_sock) {
		netlink_kernel_release(scsi_nl_sock);
		netlink_unregister_notifier(&scsi_netlink_notifier);
	}

	return;
}


/*
 * Exported Interfaces
 */

/**
 * scsi_nl_send_transport_msg -
 *    Generic function to send a single message from a SCSI transport to
 *    a single process
 *
 * @pid:		receiving pid
 * @hdr:		message payload
 *
 **/
void
scsi_nl_send_transport_msg(u32 pid, struct scsi_nl_hdr *hdr)
{
	struct sk_buff *skb;
	struct nlmsghdr	*nlh;
	const char *fn;
	char *datab;
	u32 len, skblen;
	int err;

	if (!scsi_nl_sock) {
		err = -ENOENT;
		fn = "netlink socket";
		goto msg_fail;
	}

	len = NLMSG_SPACE(hdr->msglen);
	skblen = NLMSG_SPACE(len);

	skb = alloc_skb(skblen, GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		fn = "alloc_skb";
		goto msg_fail;
	}

	nlh = nlmsg_put(skb, pid, 0, SCSI_TRANSPORT_MSG, len - sizeof(*nlh), 0);
	if (!nlh) {
		err = -ENOBUFS;
		fn = "nlmsg_put";
		goto msg_fail_skb;
	}
	datab = NLMSG_DATA(nlh);
	memcpy(datab, hdr, hdr->msglen);

	err = nlmsg_unicast(scsi_nl_sock, skb, pid);
	if (err < 0) {
		fn = "nlmsg_unicast";
		/* nlmsg_unicast already kfree_skb'd */
		goto msg_fail;
	}

	return;

msg_fail_skb:
	kfree_skb(skb);
msg_fail:
	printk(KERN_WARNING
		"%s: Dropped Message : pid %d Transport %d, msgtype x%x, "
		"msglen %d: %s : err %d\n",
		__func__, pid, hdr->transport, hdr->msgtype, hdr->msglen,
		fn, err);
	return;
}
EXPORT_SYMBOL_GPL(scsi_nl_send_transport_msg);


/**
 * scsi_nl_send_vendor_msg - called to send a shost vendor unique message
 *                      to a specific process id.
 *
 * @pid:		process id of the receiver
 * @host_no:		host # sending the message
 * @vendor_id:		unique identifier for the driver's vendor
 * @data_len:		amount, in bytes, of vendor unique payload data
 * @data_buf:		pointer to vendor unique data buffer
 *
 * Returns:
 *   0 on successful return
 *   otherwise, failing error code
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 */
int
scsi_nl_send_vendor_msg(u32 pid, unsigned short host_no, u64 vendor_id,
			 char *data_buf, u32 data_len)
{
	struct sk_buff *skb;
	struct nlmsghdr	*nlh;
	struct scsi_nl_host_vendor_msg *msg;
	u32 len, skblen;
	int err;

	if (!scsi_nl_sock) {
		err = -ENOENT;
		goto send_vendor_fail;
	}

	len = SCSI_NL_MSGALIGN(sizeof(*msg) + data_len);
	skblen = NLMSG_SPACE(len);

	skb = alloc_skb(skblen, GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		goto send_vendor_fail;
	}

	nlh = nlmsg_put(skb, 0, 0, SCSI_TRANSPORT_MSG,
				skblen - sizeof(*nlh), 0);
	if (!nlh) {
		err = -ENOBUFS;
		goto send_vendor_fail_skb;
	}
	msg = NLMSG_DATA(nlh);

	INIT_SCSI_NL_HDR(&msg->snlh, SCSI_NL_TRANSPORT,
				SCSI_NL_SHOST_VENDOR, len);
	msg->vendor_id = vendor_id;
	msg->host_no = host_no;
	msg->vmsg_datalen = data_len;	/* bytes */
	memcpy(&msg[1], data_buf, data_len);

	err = nlmsg_unicast(scsi_nl_sock, skb, pid);
	if (err)
		/* nlmsg_multicast already kfree_skb'd */
		goto send_vendor_fail;

	return 0;

send_vendor_fail_skb:
	kfree_skb(skb);
send_vendor_fail:
	printk(KERN_WARNING
		"%s: Dropped SCSI Msg : host %d vendor_unique - err %d\n",
		__func__, host_no, err);
	return err;
}
EXPORT_SYMBOL(scsi_nl_send_vendor_msg);


