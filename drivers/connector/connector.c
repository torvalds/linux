/*
 *	connector.c
 *
 * 2004+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <linux/moduleparam.h>
#include <linux/connector.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Generic userspace <-> kernelspace connector.");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_CONNECTOR);

static struct cn_dev cdev;

static int cn_already_initialized;

/*
 * msg->seq and msg->ack are used to determine message genealogy.
 * When someone sends message it puts there locally unique sequence
 * and random acknowledge numbers.  Sequence number may be copied into
 * nlmsghdr->nlmsg_seq too.
 *
 * Sequence number is incremented with each message to be sent.
 *
 * If we expect reply to our message then the sequence number in
 * received message MUST be the same as in original message, and
 * acknowledge number MUST be the same + 1.
 *
 * If we receive a message and its sequence number is not equal to the
 * one we are expecting then it is a new message.
 *
 * If we receive a message and its sequence number is the same as one
 * we are expecting but it's acknowledgement number is not equal to
 * the acknowledgement number in the original message + 1, then it is
 * a new message.
 *
 */
int cn_netlink_send(struct cn_msg *msg, u32 __group, gfp_t gfp_mask)
{
	struct cn_callback_entry *__cbq;
	unsigned int size;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct cn_msg *data;
	struct cn_dev *dev = &cdev;
	u32 group = 0;
	int found = 0;

	if (!__group) {
		spin_lock_bh(&dev->cbdev->queue_lock);
		list_for_each_entry(__cbq, &dev->cbdev->queue_list,
				    callback_entry) {
			if (cn_cb_equal(&__cbq->id.id, &msg->id)) {
				found = 1;
				group = __cbq->group;
				break;
			}
		}
		spin_unlock_bh(&dev->cbdev->queue_lock);

		if (!found)
			return -ENODEV;
	} else {
		group = __group;
	}

	if (!netlink_has_listeners(dev->nls, group))
		return -ESRCH;

	size = sizeof(*msg) + msg->len;

	skb = nlmsg_new(size, gfp_mask);
	if (!skb)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, msg->seq, NLMSG_DONE, size, 0);
	if (!nlh) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	data = nlmsg_data(nlh);

	memcpy(data, msg, size);

	NETLINK_CB(skb).dst_group = group;

	return netlink_broadcast(dev->nls, skb, 0, group, gfp_mask);
}
EXPORT_SYMBOL_GPL(cn_netlink_send);

/*
 * Callback helper - queues work and setup destructor for given data.
 */
static int cn_call_callback(struct sk_buff *skb)
{
	struct cn_callback_entry *i, *cbq = NULL;
	struct cn_dev *dev = &cdev;
	struct cn_msg *msg = nlmsg_data(nlmsg_hdr(skb));
	struct netlink_skb_parms *nsp = &NETLINK_CB(skb);
	int err = -ENODEV;

	spin_lock_bh(&dev->cbdev->queue_lock);
	list_for_each_entry(i, &dev->cbdev->queue_list, callback_entry) {
		if (cn_cb_equal(&i->id.id, &msg->id)) {
			atomic_inc(&i->refcnt);
			cbq = i;
			break;
		}
	}
	spin_unlock_bh(&dev->cbdev->queue_lock);

	if (cbq != NULL) {
		err = 0;
		cbq->callback(msg, nsp);
		kfree_skb(skb);
		cn_queue_release_callback(cbq);
		err = 0;
	}

	return err;
}

/*
 * Main netlink receiving function.
 *
 * It checks skb, netlink header and msg sizes, and calls callback helper.
 */
static void cn_rx_skb(struct sk_buff *__skb)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len, err;

	skb = skb_get(__skb);

	if (skb->len >= NLMSG_HDRLEN) {
		nlh = nlmsg_hdr(skb);
		len = nlmsg_len(nlh);

		if (len < (int)sizeof(struct cn_msg) ||
		    skb->len < nlh->nlmsg_len ||
		    len > CONNECTOR_MAX_MSG_SIZE) {
			kfree_skb(skb);
			return;
		}

		err = cn_call_callback(skb);
		if (err < 0)
			kfree_skb(skb);
	}
}

/*
 * Callback add routing - adds callback with given ID and name.
 * If there is registered callback with the same ID it will not be added.
 *
 * May sleep.
 */
int cn_add_callback(struct cb_id *id, const char *name,
		    void (*callback)(struct cn_msg *,
				     struct netlink_skb_parms *))
{
	int err;
	struct cn_dev *dev = &cdev;

	if (!cn_already_initialized)
		return -EAGAIN;

	err = cn_queue_add_callback(dev->cbdev, name, id, callback);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(cn_add_callback);

/*
 * Callback remove routing - removes callback
 * with given ID.
 * If there is no registered callback with given
 * ID nothing happens.
 *
 * May sleep while waiting for reference counter to become zero.
 */
void cn_del_callback(struct cb_id *id)
{
	struct cn_dev *dev = &cdev;

	cn_queue_del_callback(dev->cbdev, id);
}
EXPORT_SYMBOL_GPL(cn_del_callback);

static int cn_proc_show(struct seq_file *m, void *v)
{
	struct cn_queue_dev *dev = cdev.cbdev;
	struct cn_callback_entry *cbq;

	seq_printf(m, "Name            ID\n");

	spin_lock_bh(&dev->queue_lock);

	list_for_each_entry(cbq, &dev->queue_list, callback_entry) {
		seq_printf(m, "%-15s %u:%u\n",
			   cbq->id.name,
			   cbq->id.id.idx,
			   cbq->id.id.val);
	}

	spin_unlock_bh(&dev->queue_lock);

	return 0;
}

static int cn_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cn_proc_show, NULL);
}

static const struct file_operations cn_file_ops = {
	.owner   = THIS_MODULE,
	.open    = cn_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static struct cn_dev cdev = {
	.input   = cn_rx_skb,
};

static int cn_init(void)
{
	struct cn_dev *dev = &cdev;
	struct netlink_kernel_cfg cfg = {
		.groups	= CN_NETLINK_USERS + 0xf,
		.input	= dev->input,
	};

	dev->nls = netlink_kernel_create(&init_net, NETLINK_CONNECTOR, &cfg);
	if (!dev->nls)
		return -EIO;

	dev->cbdev = cn_queue_alloc_dev("cqueue", dev->nls);
	if (!dev->cbdev) {
		netlink_kernel_release(dev->nls);
		return -EINVAL;
	}

	cn_already_initialized = 1;

	proc_create("connector", S_IRUGO, init_net.proc_net, &cn_file_ops);

	return 0;
}

static void cn_fini(void)
{
	struct cn_dev *dev = &cdev;

	cn_already_initialized = 0;

	remove_proc_entry("connector", init_net.proc_net);

	cn_queue_free_dev(dev->cbdev);
	netlink_kernel_release(dev->nls);
}

subsys_initcall(cn_init);
module_exit(cn_fini);
