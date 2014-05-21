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
#include <linux/export.h>
#include <net/sock.h>
#include <net/netlink.h>

#include <scsi/scsi_netlink.h>
#include "scsi_priv.h"

struct sock *scsi_nl_sock = NULL;
EXPORT_SYMBOL_GPL(scsi_nl_sock);

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
	u32 rlen;
	int err, tport;

	while (skb->len >= NLMSG_HDRLEN) {
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

		hdr = nlmsg_data(nlh);
		if ((hdr->version != SCSI_NL_VERSION) ||
		    (hdr->magic != SCSI_NL_MAGIC)) {
			err = -EPROTOTYPE;
			goto next_msg;
		}

		if (!netlink_capable(skb, CAP_SYS_ADMIN)) {
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
		tport = hdr->transport;
		if (tport == SCSI_NL_TRANSPORT) {
			switch (hdr->msgtype) {
			case SCSI_NL_SHOST_VENDOR:
				/* Locate the driver that corresponds to the message */
				err = -ESRCH;
				break;
			default:
				err = -EBADR;
				break;
			}
			if (err)
				printk(KERN_WARNING "%s: Msgtype %d failed - err %d\n",
				       __func__, hdr->msgtype, err);
		}
		else
			err = -ENOENT;

next_msg:
		if ((err) || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		skb_pull(skb, rlen);
	}
}

/**
 * scsi_netlink_init - Called by SCSI subsystem to initialize
 * 	the SCSI transport netlink interface
 *
 **/
void
scsi_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= scsi_nl_rcv_msg,
		.groups	= SCSI_NL_GRP_CNT,
	};

	scsi_nl_sock = netlink_kernel_create(&init_net, NETLINK_SCSITRANSPORT,
					     &cfg);
	if (!scsi_nl_sock) {
		printk(KERN_ERR "%s: register of receive handler failed\n",
				__func__);
		return;
	}

	return;
}


/**
 * scsi_netlink_exit - Called by SCSI subsystem to disable the SCSI transport netlink interface
 *
 **/
void
scsi_netlink_exit(void)
{
	if (scsi_nl_sock) {
		netlink_kernel_release(scsi_nl_sock);
	}

	return;
}

