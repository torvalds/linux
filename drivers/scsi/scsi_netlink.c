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
#include <net/sock.h>
#include <net/netlink.h>

#include <scsi/scsi_netlink.h>
#include "scsi_priv.h"

struct sock *scsi_nl_sock = NULL;
EXPORT_SYMBOL_GPL(scsi_nl_sock);


/**
 * scsi_nl_rcv_msg -
 *    Receive message handler. Extracts message from a receive buffer.
 *    Validates message header and calls appropriate transport message handler
 *
 * @skb:		socket receive buffer
 *
 **/
static void
scsi_nl_rcv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct scsi_nl_hdr *hdr;
	uint32_t rlen;
	int err;

	while (skb->len >= NLMSG_SPACE(0)) {
		err = 0;

		nlh = nlmsg_hdr(skb);
		if ((nlh->nlmsg_len < (sizeof(*nlh) + sizeof(*hdr))) ||
		    (skb->len < nlh->nlmsg_len)) {
			printk(KERN_WARNING "%s: discarding partial skb\n",
				 __FUNCTION__);
			return;
		}

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		if (nlh->nlmsg_type != SCSI_TRANSPORT_MSG) {
			err = -EBADMSG;
			return;
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
				 __FUNCTION__);
			return;
		}

		/*
		 * We currently don't support anyone sending us a message
		 */

next_msg:
		if ((err) || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		skb_pull(skb, rlen);
	}
}


/**
 * scsi_nl_rcv_event -
 *    Event handler for a netlink socket.
 *
 * @this:		event notifier block
 * @event:		event type
 * @ptr:		event payload
 *
 **/
static int
scsi_nl_rcv_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;

	if (n->protocol != NETLINK_SCSITRANSPORT)
		return NOTIFY_DONE;

	/*
	 * Currently, we are not tracking PID's, etc. There is nothing
	 * to handle.
	 */

	return NOTIFY_DONE;
}

static struct notifier_block scsi_netlink_notifier = {
	.notifier_call  = scsi_nl_rcv_event,
};


/**
 * scsi_netlink_init -
 *    Called by SCSI subsystem to intialize the SCSI transport netlink
 *    interface
 *
 **/
void
scsi_netlink_init(void)
{
	int error;

	error = netlink_register_notifier(&scsi_netlink_notifier);
	if (error) {
		printk(KERN_ERR "%s: register of event handler failed - %d\n",
				__FUNCTION__, error);
		return;
	}

	scsi_nl_sock = netlink_kernel_create(&init_net, NETLINK_SCSITRANSPORT,
				SCSI_NL_GRP_CNT, scsi_nl_rcv_msg, NULL,
				THIS_MODULE);
	if (!scsi_nl_sock) {
		printk(KERN_ERR "%s: register of recieve handler failed\n",
				__FUNCTION__);
		netlink_unregister_notifier(&scsi_netlink_notifier);
	}

	return;
}


/**
 * scsi_netlink_exit -
 *    Called by SCSI subsystem to disable the SCSI transport netlink
 *    interface
 *
 **/
void
scsi_netlink_exit(void)
{
	if (scsi_nl_sock) {
		sock_release(scsi_nl_sock->sk_socket);
		netlink_unregister_notifier(&scsi_netlink_notifier);
	}

	return;
}


