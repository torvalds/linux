/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2008 Cisco. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include <rdma/ib_mad.h>
#include <rdma/ib_user_mad.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand userspace MAD packet access");
MODULE_LICENSE("Dual BSD/GPL");

enum {
	IB_UMAD_MAX_PORTS  = 64,
	IB_UMAD_MAX_AGENTS = 32,

	IB_UMAD_MAJOR      = 231,
	IB_UMAD_MINOR_BASE = 0
};

/*
 * Our lifetime rules for these structs are the following:
 * device special file is opened, we take a reference on the
 * ib_umad_port's struct ib_umad_device. We drop these
 * references in the corresponding close().
 *
 * In addition to references coming from open character devices, there
 * is one more reference to each ib_umad_device representing the
 * module's reference taken when allocating the ib_umad_device in
 * ib_umad_add_one().
 *
 * When destroying an ib_umad_device, we drop the module's reference.
 */

struct ib_umad_port {
	struct cdev           cdev;
	struct device	      *dev;

	struct cdev           sm_cdev;
	struct device	      *sm_dev;
	struct semaphore       sm_sem;

	struct mutex	       file_mutex;
	struct list_head       file_list;

	struct ib_device      *ib_dev;
	struct ib_umad_device *umad_dev;
	int                    dev_num;
	u8                     port_num;
};

struct ib_umad_device {
	int                  start_port, end_port;
	struct kref          ref;
	struct ib_umad_port  port[0];
};

struct ib_umad_file {
	struct mutex		mutex;
	struct ib_umad_port    *port;
	struct list_head	recv_list;
	struct list_head	send_list;
	struct list_head	port_list;
	spinlock_t		send_lock;
	wait_queue_head_t	recv_wait;
	struct ib_mad_agent    *agent[IB_UMAD_MAX_AGENTS];
	int			agents_dead;
	u8			use_pkey_index;
	u8			already_used;
};

struct ib_umad_packet {
	struct ib_mad_send_buf *msg;
	struct ib_mad_recv_wc  *recv_wc;
	struct list_head   list;
	int		   length;
	struct ib_user_mad mad;
};

static struct class *umad_class;

static const dev_t base_dev = MKDEV(IB_UMAD_MAJOR, IB_UMAD_MINOR_BASE);

static DEFINE_SPINLOCK(port_lock);
static DECLARE_BITMAP(dev_map, IB_UMAD_MAX_PORTS);

static void ib_umad_add_one(struct ib_device *device);
static void ib_umad_remove_one(struct ib_device *device);

static void ib_umad_release_dev(struct kref *ref)
{
	struct ib_umad_device *dev =
		container_of(ref, struct ib_umad_device, ref);

	kfree(dev);
}

static int hdr_size(struct ib_umad_file *file)
{
	return file->use_pkey_index ? sizeof (struct ib_user_mad_hdr) :
		sizeof (struct ib_user_mad_hdr_old);
}

/* caller must hold file->mutex */
static struct ib_mad_agent *__get_agent(struct ib_umad_file *file, int id)
{
	return file->agents_dead ? NULL : file->agent[id];
}

static int queue_packet(struct ib_umad_file *file,
			struct ib_mad_agent *agent,
			struct ib_umad_packet *packet)
{
	int ret = 1;

	mutex_lock(&file->mutex);

	for (packet->mad.hdr.id = 0;
	     packet->mad.hdr.id < IB_UMAD_MAX_AGENTS;
	     packet->mad.hdr.id++)
		if (agent == __get_agent(file, packet->mad.hdr.id)) {
			list_add_tail(&packet->list, &file->recv_list);
			wake_up_interruptible(&file->recv_wait);
			ret = 0;
			break;
		}

	mutex_unlock(&file->mutex);

	return ret;
}

static void dequeue_send(struct ib_umad_file *file,
			 struct ib_umad_packet *packet)
{
	spin_lock_irq(&file->send_lock);
	list_del(&packet->list);
	spin_unlock_irq(&file->send_lock);
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *send_wc)
{
	struct ib_umad_file *file = agent->context;
	struct ib_umad_packet *packet = send_wc->send_buf->context[0];

	dequeue_send(file, packet);
	ib_destroy_ah(packet->msg->ah);
	ib_free_send_mad(packet->msg);

	if (send_wc->status == IB_WC_RESP_TIMEOUT_ERR) {
		packet->length = IB_MGMT_MAD_HDR;
		packet->mad.hdr.status = ETIMEDOUT;
		if (!queue_packet(file, agent, packet))
			return;
	}
	kfree(packet);
}

static void recv_handler(struct ib_mad_agent *agent,
			 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_umad_file *file = agent->context;
	struct ib_umad_packet *packet;

	if (mad_recv_wc->wc->status != IB_WC_SUCCESS)
		goto err1;

	packet = kzalloc(sizeof *packet, GFP_KERNEL);
	if (!packet)
		goto err1;

	packet->length = mad_recv_wc->mad_len;
	packet->recv_wc = mad_recv_wc;

	packet->mad.hdr.status	   = 0;
	packet->mad.hdr.length	   = hdr_size(file) + mad_recv_wc->mad_len;
	packet->mad.hdr.qpn	   = cpu_to_be32(mad_recv_wc->wc->src_qp);
	packet->mad.hdr.lid	   = cpu_to_be16(mad_recv_wc->wc->slid);
	packet->mad.hdr.sl	   = mad_recv_wc->wc->sl;
	packet->mad.hdr.path_bits  = mad_recv_wc->wc->dlid_path_bits;
	packet->mad.hdr.pkey_index = mad_recv_wc->wc->pkey_index;
	packet->mad.hdr.grh_present = !!(mad_recv_wc->wc->wc_flags & IB_WC_GRH);
	if (packet->mad.hdr.grh_present) {
		struct ib_ah_attr ah_attr;

		ib_init_ah_from_wc(agent->device, agent->port_num,
				   mad_recv_wc->wc, mad_recv_wc->recv_buf.grh,
				   &ah_attr);

		packet->mad.hdr.gid_index = ah_attr.grh.sgid_index;
		packet->mad.hdr.hop_limit = ah_attr.grh.hop_limit;
		packet->mad.hdr.traffic_class = ah_attr.grh.traffic_class;
		memcpy(packet->mad.hdr.gid, &ah_attr.grh.dgid, 16);
		packet->mad.hdr.flow_label = cpu_to_be32(ah_attr.grh.flow_label);
	}

	if (queue_packet(file, agent, packet))
		goto err2;
	return;

err2:
	kfree(packet);
err1:
	ib_free_recv_mad(mad_recv_wc);
}

static ssize_t copy_recv_mad(struct ib_umad_file *file, char __user *buf,
			     struct ib_umad_packet *packet, size_t count)
{
	struct ib_mad_recv_buf *recv_buf;
	int left, seg_payload, offset, max_seg_payload;

	/* We need enough room to copy the first (or only) MAD segment. */
	recv_buf = &packet->recv_wc->recv_buf;
	if ((packet->length <= sizeof (*recv_buf->mad) &&
	     count < hdr_size(file) + packet->length) ||
	    (packet->length > sizeof (*recv_buf->mad) &&
	     count < hdr_size(file) + sizeof (*recv_buf->mad)))
		return -EINVAL;

	if (copy_to_user(buf, &packet->mad, hdr_size(file)))
		return -EFAULT;

	buf += hdr_size(file);
	seg_payload = min_t(int, packet->length, sizeof (*recv_buf->mad));
	if (copy_to_user(buf, recv_buf->mad, seg_payload))
		return -EFAULT;

	if (seg_payload < packet->length) {
		/*
		 * Multipacket RMPP MAD message. Copy remainder of message.
		 * Note that last segment may have a shorter payload.
		 */
		if (count < hdr_size(file) + packet->length) {
			/*
			 * The buffer is too small, return the first RMPP segment,
			 * which includes the RMPP message length.
			 */
			return -ENOSPC;
		}
		offset = ib_get_mad_data_offset(recv_buf->mad->mad_hdr.mgmt_class);
		max_seg_payload = sizeof (struct ib_mad) - offset;

		for (left = packet->length - seg_payload, buf += seg_payload;
		     left; left -= seg_payload, buf += seg_payload) {
			recv_buf = container_of(recv_buf->list.next,
						struct ib_mad_recv_buf, list);
			seg_payload = min(left, max_seg_payload);
			if (copy_to_user(buf, ((void *) recv_buf->mad) + offset,
					 seg_payload))
				return -EFAULT;
		}
	}
	return hdr_size(file) + packet->length;
}

static ssize_t copy_send_mad(struct ib_umad_file *file, char __user *buf,
			     struct ib_umad_packet *packet, size_t count)
{
	ssize_t size = hdr_size(file) + packet->length;

	if (count < size)
		return -EINVAL;

	if (copy_to_user(buf, &packet->mad, hdr_size(file)))
		return -EFAULT;

	buf += hdr_size(file);

	if (copy_to_user(buf, packet->mad.data, packet->length))
		return -EFAULT;

	return size;
}

static ssize_t ib_umad_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_packet *packet;
	ssize_t ret;

	if (count < hdr_size(file))
		return -EINVAL;

	mutex_lock(&file->mutex);

	while (list_empty(&file->recv_list)) {
		mutex_unlock(&file->mutex);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(file->recv_wait,
					     !list_empty(&file->recv_list)))
			return -ERESTARTSYS;

		mutex_lock(&file->mutex);
	}

	packet = list_entry(file->recv_list.next, struct ib_umad_packet, list);
	list_del(&packet->list);

	mutex_unlock(&file->mutex);

	if (packet->recv_wc)
		ret = copy_recv_mad(file, buf, packet, count);
	else
		ret = copy_send_mad(file, buf, packet, count);

	if (ret < 0) {
		/* Requeue packet */
		mutex_lock(&file->mutex);
		list_add(&packet->list, &file->recv_list);
		mutex_unlock(&file->mutex);
	} else {
		if (packet->recv_wc)
			ib_free_recv_mad(packet->recv_wc);
		kfree(packet);
	}
	return ret;
}

static int copy_rmpp_mad(struct ib_mad_send_buf *msg, const char __user *buf)
{
	int left, seg;

	/* Copy class specific header */
	if ((msg->hdr_len > IB_MGMT_RMPP_HDR) &&
	    copy_from_user(msg->mad + IB_MGMT_RMPP_HDR, buf + IB_MGMT_RMPP_HDR,
			   msg->hdr_len - IB_MGMT_RMPP_HDR))
		return -EFAULT;

	/* All headers are in place.  Copy data segments. */
	for (seg = 1, left = msg->data_len, buf += msg->hdr_len; left > 0;
	     seg++, left -= msg->seg_size, buf += msg->seg_size) {
		if (copy_from_user(ib_get_rmpp_segment(msg, seg), buf,
				   min(left, msg->seg_size)))
			return -EFAULT;
	}
	return 0;
}

static int same_destination(struct ib_user_mad_hdr *hdr1,
			    struct ib_user_mad_hdr *hdr2)
{
	if (!hdr1->grh_present && !hdr2->grh_present)
	   return (hdr1->lid == hdr2->lid);

	if (hdr1->grh_present && hdr2->grh_present)
	   return !memcmp(hdr1->gid, hdr2->gid, 16);

	return 0;
}

static int is_duplicate(struct ib_umad_file *file,
			struct ib_umad_packet *packet)
{
	struct ib_umad_packet *sent_packet;
	struct ib_mad_hdr *sent_hdr, *hdr;

	hdr = (struct ib_mad_hdr *) packet->mad.data;
	list_for_each_entry(sent_packet, &file->send_list, list) {
		sent_hdr = (struct ib_mad_hdr *) sent_packet->mad.data;

		if ((hdr->tid != sent_hdr->tid) ||
		    (hdr->mgmt_class != sent_hdr->mgmt_class))
			continue;

		/*
		 * No need to be overly clever here.  If two new operations have
		 * the same TID, reject the second as a duplicate.  This is more
		 * restrictive than required by the spec.
		 */
		if (!ib_response_mad((struct ib_mad *) hdr)) {
			if (!ib_response_mad((struct ib_mad *) sent_hdr))
				return 1;
			continue;
		} else if (!ib_response_mad((struct ib_mad *) sent_hdr))
			continue;

		if (same_destination(&packet->mad.hdr, &sent_packet->mad.hdr))
			return 1;
	}

	return 0;
}

static ssize_t ib_umad_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_packet *packet;
	struct ib_mad_agent *agent;
	struct ib_ah_attr ah_attr;
	struct ib_ah *ah;
	struct ib_rmpp_mad *rmpp_mad;
	__be64 *tid;
	int ret, data_len, hdr_len, copy_offset, rmpp_active;

	if (count < hdr_size(file) + IB_MGMT_RMPP_HDR)
		return -EINVAL;

	packet = kzalloc(sizeof *packet + IB_MGMT_RMPP_HDR, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(&packet->mad, buf, hdr_size(file))) {
		ret = -EFAULT;
		goto err;
	}

	if (packet->mad.hdr.id < 0 ||
	    packet->mad.hdr.id >= IB_UMAD_MAX_AGENTS) {
		ret = -EINVAL;
		goto err;
	}

	buf += hdr_size(file);

	if (copy_from_user(packet->mad.data, buf, IB_MGMT_RMPP_HDR)) {
		ret = -EFAULT;
		goto err;
	}

	mutex_lock(&file->mutex);

	agent = __get_agent(file, packet->mad.hdr.id);
	if (!agent) {
		ret = -EINVAL;
		goto err_up;
	}

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid          = be16_to_cpu(packet->mad.hdr.lid);
	ah_attr.sl            = packet->mad.hdr.sl;
	ah_attr.src_path_bits = packet->mad.hdr.path_bits;
	ah_attr.port_num      = file->port->port_num;
	if (packet->mad.hdr.grh_present) {
		ah_attr.ah_flags = IB_AH_GRH;
		memcpy(ah_attr.grh.dgid.raw, packet->mad.hdr.gid, 16);
		ah_attr.grh.sgid_index	   = packet->mad.hdr.gid_index;
		ah_attr.grh.flow_label	   = be32_to_cpu(packet->mad.hdr.flow_label);
		ah_attr.grh.hop_limit	   = packet->mad.hdr.hop_limit;
		ah_attr.grh.traffic_class  = packet->mad.hdr.traffic_class;
	}

	ah = ib_create_ah(agent->qp->pd, &ah_attr);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_up;
	}

	rmpp_mad = (struct ib_rmpp_mad *) packet->mad.data;
	hdr_len = ib_get_mad_data_offset(rmpp_mad->mad_hdr.mgmt_class);
	if (!ib_is_mad_class_rmpp(rmpp_mad->mad_hdr.mgmt_class)) {
		copy_offset = IB_MGMT_MAD_HDR;
		rmpp_active = 0;
	} else {
		copy_offset = IB_MGMT_RMPP_HDR;
		rmpp_active = ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
			      IB_MGMT_RMPP_FLAG_ACTIVE;
	}

	data_len = count - hdr_size(file) - hdr_len;
	packet->msg = ib_create_send_mad(agent,
					 be32_to_cpu(packet->mad.hdr.qpn),
					 packet->mad.hdr.pkey_index, rmpp_active,
					 hdr_len, data_len, GFP_KERNEL);
	if (IS_ERR(packet->msg)) {
		ret = PTR_ERR(packet->msg);
		goto err_ah;
	}

	packet->msg->ah		= ah;
	packet->msg->timeout_ms = packet->mad.hdr.timeout_ms;
	packet->msg->retries	= packet->mad.hdr.retries;
	packet->msg->context[0] = packet;

	/* Copy MAD header.  Any RMPP header is already in place. */
	memcpy(packet->msg->mad, packet->mad.data, IB_MGMT_MAD_HDR);

	if (!rmpp_active) {
		if (copy_from_user(packet->msg->mad + copy_offset,
				   buf + copy_offset,
				   hdr_len + data_len - copy_offset)) {
			ret = -EFAULT;
			goto err_msg;
		}
	} else {
		ret = copy_rmpp_mad(packet->msg, buf);
		if (ret)
			goto err_msg;
	}

	/*
	 * Set the high-order part of the transaction ID to make MADs from
	 * different agents unique, and allow routing responses back to the
	 * original requestor.
	 */
	if (!ib_response_mad(packet->msg->mad)) {
		tid = &((struct ib_mad_hdr *) packet->msg->mad)->tid;
		*tid = cpu_to_be64(((u64) agent->hi_tid) << 32 |
				   (be64_to_cpup(tid) & 0xffffffff));
		rmpp_mad->mad_hdr.tid = *tid;
	}

	spin_lock_irq(&file->send_lock);
	ret = is_duplicate(file, packet);
	if (!ret)
		list_add_tail(&packet->list, &file->send_list);
	spin_unlock_irq(&file->send_lock);
	if (ret) {
		ret = -EINVAL;
		goto err_msg;
	}

	ret = ib_post_send_mad(packet->msg, NULL);
	if (ret)
		goto err_send;

	mutex_unlock(&file->mutex);
	return count;

err_send:
	dequeue_send(file, packet);
err_msg:
	ib_free_send_mad(packet->msg);
err_ah:
	ib_destroy_ah(ah);
err_up:
	mutex_unlock(&file->mutex);
err:
	kfree(packet);
	return ret;
}

static unsigned int ib_umad_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ib_umad_file *file = filp->private_data;

	/* we will always be able to post a MAD send */
	unsigned int mask = POLLOUT | POLLWRNORM;

	poll_wait(filp, &file->recv_wait, wait);

	if (!list_empty(&file->recv_list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static int ib_umad_reg_agent(struct ib_umad_file *file, void __user *arg,
			     int compat_method_mask)
{
	struct ib_user_mad_reg_req ureq;
	struct ib_mad_reg_req req;
	struct ib_mad_agent *agent = NULL;
	int agent_id;
	int ret;

	mutex_lock(&file->port->file_mutex);
	mutex_lock(&file->mutex);

	if (!file->port->ib_dev) {
		ret = -EPIPE;
		goto out;
	}

	if (copy_from_user(&ureq, arg, sizeof ureq)) {
		ret = -EFAULT;
		goto out;
	}

	if (ureq.qpn != 0 && ureq.qpn != 1) {
		ret = -EINVAL;
		goto out;
	}

	for (agent_id = 0; agent_id < IB_UMAD_MAX_AGENTS; ++agent_id)
		if (!__get_agent(file, agent_id))
			goto found;

	ret = -ENOMEM;
	goto out;

found:
	if (ureq.mgmt_class) {
		req.mgmt_class         = ureq.mgmt_class;
		req.mgmt_class_version = ureq.mgmt_class_version;
		memcpy(req.oui, ureq.oui, sizeof req.oui);

		if (compat_method_mask) {
			u32 *umm = (u32 *) ureq.method_mask;
			int i;

			for (i = 0; i < BITS_TO_LONGS(IB_MGMT_MAX_METHODS); ++i)
				req.method_mask[i] =
					umm[i * 2] | ((u64) umm[i * 2 + 1] << 32);
		} else
			memcpy(req.method_mask, ureq.method_mask,
			       sizeof req.method_mask);
	}

	agent = ib_register_mad_agent(file->port->ib_dev, file->port->port_num,
				      ureq.qpn ? IB_QPT_GSI : IB_QPT_SMI,
				      ureq.mgmt_class ? &req : NULL,
				      ureq.rmpp_version,
				      send_handler, recv_handler, file);
	if (IS_ERR(agent)) {
		ret = PTR_ERR(agent);
		agent = NULL;
		goto out;
	}

	if (put_user(agent_id,
		     (u32 __user *) (arg + offsetof(struct ib_user_mad_reg_req, id)))) {
		ret = -EFAULT;
		goto out;
	}

	if (!file->already_used) {
		file->already_used = 1;
		if (!file->use_pkey_index) {
			printk(KERN_WARNING "user_mad: process %s did not enable "
			       "P_Key index support.\n", current->comm);
			printk(KERN_WARNING "user_mad:   Documentation/infiniband/user_mad.txt "
			       "has info on the new ABI.\n");
		}
	}

	file->agent[agent_id] = agent;
	ret = 0;

out:
	mutex_unlock(&file->mutex);

	if (ret && agent)
		ib_unregister_mad_agent(agent);

	mutex_unlock(&file->port->file_mutex);

	return ret;
}

static int ib_umad_unreg_agent(struct ib_umad_file *file, u32 __user *arg)
{
	struct ib_mad_agent *agent = NULL;
	u32 id;
	int ret = 0;

	if (get_user(id, arg))
		return -EFAULT;

	mutex_lock(&file->port->file_mutex);
	mutex_lock(&file->mutex);

	if (id < 0 || id >= IB_UMAD_MAX_AGENTS || !__get_agent(file, id)) {
		ret = -EINVAL;
		goto out;
	}

	agent = file->agent[id];
	file->agent[id] = NULL;

out:
	mutex_unlock(&file->mutex);

	if (agent)
		ib_unregister_mad_agent(agent);

	mutex_unlock(&file->port->file_mutex);

	return ret;
}

static long ib_umad_enable_pkey(struct ib_umad_file *file)
{
	int ret = 0;

	mutex_lock(&file->mutex);
	if (file->already_used)
		ret = -EINVAL;
	else
		file->use_pkey_index = 1;
	mutex_unlock(&file->mutex);

	return ret;
}

static long ib_umad_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	switch (cmd) {
	case IB_USER_MAD_REGISTER_AGENT:
		return ib_umad_reg_agent(filp->private_data, (void __user *) arg, 0);
	case IB_USER_MAD_UNREGISTER_AGENT:
		return ib_umad_unreg_agent(filp->private_data, (__u32 __user *) arg);
	case IB_USER_MAD_ENABLE_PKEY:
		return ib_umad_enable_pkey(filp->private_data);
	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static long ib_umad_compat_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long arg)
{
	switch (cmd) {
	case IB_USER_MAD_REGISTER_AGENT:
		return ib_umad_reg_agent(filp->private_data, compat_ptr(arg), 1);
	case IB_USER_MAD_UNREGISTER_AGENT:
		return ib_umad_unreg_agent(filp->private_data, compat_ptr(arg));
	case IB_USER_MAD_ENABLE_PKEY:
		return ib_umad_enable_pkey(filp->private_data);
	default:
		return -ENOIOCTLCMD;
	}
}
#endif

/*
 * ib_umad_open() does not need the BKL:
 *
 *  - the ib_umad_port structures are properly reference counted, and
 *    everything else is purely local to the file being created, so
 *    races against other open calls are not a problem;
 *  - the ioctl method does not affect any global state outside of the
 *    file structure being operated on;
 */
static int ib_umad_open(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port;
	struct ib_umad_file *file;
	int ret;

	port = container_of(inode->i_cdev, struct ib_umad_port, cdev);
	if (port)
		kref_get(&port->umad_dev->ref);
	else
		return -ENXIO;

	mutex_lock(&port->file_mutex);

	if (!port->ib_dev) {
		ret = -ENXIO;
		goto out;
	}

	file = kzalloc(sizeof *file, GFP_KERNEL);
	if (!file) {
		kref_put(&port->umad_dev->ref, ib_umad_release_dev);
		ret = -ENOMEM;
		goto out;
	}

	mutex_init(&file->mutex);
	spin_lock_init(&file->send_lock);
	INIT_LIST_HEAD(&file->recv_list);
	INIT_LIST_HEAD(&file->send_list);
	init_waitqueue_head(&file->recv_wait);

	file->port = port;
	filp->private_data = file;

	list_add_tail(&file->port_list, &port->file_list);

	ret = nonseekable_open(inode, filp);

out:
	mutex_unlock(&port->file_mutex);
	return ret;
}

static int ib_umad_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_device *dev = file->port->umad_dev;
	struct ib_umad_packet *packet, *tmp;
	int already_dead;
	int i;

	mutex_lock(&file->port->file_mutex);
	mutex_lock(&file->mutex);

	already_dead = file->agents_dead;
	file->agents_dead = 1;

	list_for_each_entry_safe(packet, tmp, &file->recv_list, list) {
		if (packet->recv_wc)
			ib_free_recv_mad(packet->recv_wc);
		kfree(packet);
	}

	list_del(&file->port_list);

	mutex_unlock(&file->mutex);

	if (!already_dead)
		for (i = 0; i < IB_UMAD_MAX_AGENTS; ++i)
			if (file->agent[i])
				ib_unregister_mad_agent(file->agent[i]);

	mutex_unlock(&file->port->file_mutex);

	kfree(file);
	kref_put(&dev->ref, ib_umad_release_dev);

	return 0;
}

static const struct file_operations umad_fops = {
	.owner		= THIS_MODULE,
	.read		= ib_umad_read,
	.write		= ib_umad_write,
	.poll		= ib_umad_poll,
	.unlocked_ioctl = ib_umad_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ib_umad_compat_ioctl,
#endif
	.open		= ib_umad_open,
	.release	= ib_umad_close,
	.llseek		= no_llseek,
};

static int ib_umad_sm_open(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port;
	struct ib_port_modify props = {
		.set_port_cap_mask = IB_PORT_SM
	};
	int ret;

	port = container_of(inode->i_cdev, struct ib_umad_port, sm_cdev);
	if (port)
		kref_get(&port->umad_dev->ref);
	else
		return -ENXIO;

	if (filp->f_flags & O_NONBLOCK) {
		if (down_trylock(&port->sm_sem)) {
			ret = -EAGAIN;
			goto fail;
		}
	} else {
		if (down_interruptible(&port->sm_sem)) {
			ret = -ERESTARTSYS;
			goto fail;
		}
	}

	ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	if (ret) {
		up(&port->sm_sem);
		goto fail;
	}

	filp->private_data = port;

	return nonseekable_open(inode, filp);

fail:
	kref_put(&port->umad_dev->ref, ib_umad_release_dev);
	return ret;
}

static int ib_umad_sm_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port = filp->private_data;
	struct ib_port_modify props = {
		.clr_port_cap_mask = IB_PORT_SM
	};
	int ret = 0;

	mutex_lock(&port->file_mutex);
	if (port->ib_dev)
		ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	mutex_unlock(&port->file_mutex);

	up(&port->sm_sem);

	kref_put(&port->umad_dev->ref, ib_umad_release_dev);

	return ret;
}

static const struct file_operations umad_sm_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ib_umad_sm_open,
	.release = ib_umad_sm_close,
	.llseek	 = no_llseek,
};

static struct ib_client umad_client = {
	.name   = "umad",
	.add    = ib_umad_add_one,
	.remove = ib_umad_remove_one
};

static ssize_t show_ibdev(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ib_umad_port *port = dev_get_drvdata(dev);

	if (!port)
		return -ENODEV;

	return sprintf(buf, "%s\n", port->ib_dev->name);
}
static DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_port(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ib_umad_port *port = dev_get_drvdata(dev);

	if (!port)
		return -ENODEV;

	return sprintf(buf, "%d\n", port->port_num);
}
static DEVICE_ATTR(port, S_IRUGO, show_port, NULL);

static CLASS_ATTR_STRING(abi_version, S_IRUGO,
			 __stringify(IB_USER_MAD_ABI_VERSION));

static dev_t overflow_maj;
static DECLARE_BITMAP(overflow_map, IB_UMAD_MAX_PORTS);
static int find_overflow_devnum(void)
{
	int ret;

	if (!overflow_maj) {
		ret = alloc_chrdev_region(&overflow_maj, 0, IB_UMAD_MAX_PORTS * 2,
					  "infiniband_mad");
		if (ret) {
			printk(KERN_ERR "user_mad: couldn't register dynamic device number\n");
			return ret;
		}
	}

	ret = find_first_zero_bit(overflow_map, IB_UMAD_MAX_PORTS);
	if (ret >= IB_UMAD_MAX_PORTS)
		return -1;

	return ret;
}

static int ib_umad_init_port(struct ib_device *device, int port_num,
			     struct ib_umad_port *port)
{
	int devnum;
	dev_t base;

	spin_lock(&port_lock);
	devnum = find_first_zero_bit(dev_map, IB_UMAD_MAX_PORTS);
	if (devnum >= IB_UMAD_MAX_PORTS) {
		spin_unlock(&port_lock);
		devnum = find_overflow_devnum();
		if (devnum < 0)
			return -1;

		spin_lock(&port_lock);
		port->dev_num = devnum + IB_UMAD_MAX_PORTS;
		base = devnum + overflow_maj;
		set_bit(devnum, overflow_map);
	} else {
		port->dev_num = devnum;
		base = devnum + base_dev;
		set_bit(devnum, dev_map);
	}
	spin_unlock(&port_lock);

	port->ib_dev   = device;
	port->port_num = port_num;
	sema_init(&port->sm_sem, 1);
	mutex_init(&port->file_mutex);
	INIT_LIST_HEAD(&port->file_list);

	cdev_init(&port->cdev, &umad_fops);
	port->cdev.owner = THIS_MODULE;
	kobject_set_name(&port->cdev.kobj, "umad%d", port->dev_num);
	if (cdev_add(&port->cdev, base, 1))
		goto err_cdev;

	port->dev = device_create(umad_class, device->dma_device,
				  port->cdev.dev, port,
				  "umad%d", port->dev_num);
	if (IS_ERR(port->dev))
		goto err_cdev;

	if (device_create_file(port->dev, &dev_attr_ibdev))
		goto err_dev;
	if (device_create_file(port->dev, &dev_attr_port))
		goto err_dev;

	base += IB_UMAD_MAX_PORTS;
	cdev_init(&port->sm_cdev, &umad_sm_fops);
	port->sm_cdev.owner = THIS_MODULE;
	kobject_set_name(&port->sm_cdev.kobj, "issm%d", port->dev_num);
	if (cdev_add(&port->sm_cdev, base, 1))
		goto err_sm_cdev;

	port->sm_dev = device_create(umad_class, device->dma_device,
				     port->sm_cdev.dev, port,
				     "issm%d", port->dev_num);
	if (IS_ERR(port->sm_dev))
		goto err_sm_cdev;

	if (device_create_file(port->sm_dev, &dev_attr_ibdev))
		goto err_sm_dev;
	if (device_create_file(port->sm_dev, &dev_attr_port))
		goto err_sm_dev;

	return 0;

err_sm_dev:
	device_destroy(umad_class, port->sm_cdev.dev);

err_sm_cdev:
	cdev_del(&port->sm_cdev);

err_dev:
	device_destroy(umad_class, port->cdev.dev);

err_cdev:
	cdev_del(&port->cdev);
	if (port->dev_num < IB_UMAD_MAX_PORTS)
		clear_bit(devnum, dev_map);
	else
		clear_bit(devnum, overflow_map);

	return -1;
}

static void ib_umad_kill_port(struct ib_umad_port *port)
{
	struct ib_umad_file *file;
	int id;

	dev_set_drvdata(port->dev,    NULL);
	dev_set_drvdata(port->sm_dev, NULL);

	device_destroy(umad_class, port->cdev.dev);
	device_destroy(umad_class, port->sm_cdev.dev);

	cdev_del(&port->cdev);
	cdev_del(&port->sm_cdev);

	mutex_lock(&port->file_mutex);

	port->ib_dev = NULL;

	list_for_each_entry(file, &port->file_list, port_list) {
		mutex_lock(&file->mutex);
		file->agents_dead = 1;
		mutex_unlock(&file->mutex);

		for (id = 0; id < IB_UMAD_MAX_AGENTS; ++id)
			if (file->agent[id])
				ib_unregister_mad_agent(file->agent[id]);
	}

	mutex_unlock(&port->file_mutex);

	if (port->dev_num < IB_UMAD_MAX_PORTS)
		clear_bit(port->dev_num, dev_map);
	else
		clear_bit(port->dev_num - IB_UMAD_MAX_PORTS, overflow_map);
}

static void ib_umad_add_one(struct ib_device *device)
{
	struct ib_umad_device *umad_dev;
	int s, e, i;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	if (device->node_type == RDMA_NODE_IB_SWITCH)
		s = e = 0;
	else {
		s = 1;
		e = device->phys_port_cnt;
	}

	umad_dev = kzalloc(sizeof *umad_dev +
			   (e - s + 1) * sizeof (struct ib_umad_port),
			   GFP_KERNEL);
	if (!umad_dev)
		return;

	kref_init(&umad_dev->ref);

	umad_dev->start_port = s;
	umad_dev->end_port   = e;

	for (i = s; i <= e; ++i) {
		umad_dev->port[i - s].umad_dev = umad_dev;

		if (ib_umad_init_port(device, i, &umad_dev->port[i - s]))
			goto err;
	}

	ib_set_client_data(device, &umad_client, umad_dev);

	return;

err:
	while (--i >= s)
		ib_umad_kill_port(&umad_dev->port[i - s]);

	kref_put(&umad_dev->ref, ib_umad_release_dev);
}

static void ib_umad_remove_one(struct ib_device *device)
{
	struct ib_umad_device *umad_dev = ib_get_client_data(device, &umad_client);
	int i;

	if (!umad_dev)
		return;

	for (i = 0; i <= umad_dev->end_port - umad_dev->start_port; ++i)
		ib_umad_kill_port(&umad_dev->port[i]);

	kref_put(&umad_dev->ref, ib_umad_release_dev);
}

static char *umad_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}

static int __init ib_umad_init(void)
{
	int ret;

	ret = register_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2,
				     "infiniband_mad");
	if (ret) {
		printk(KERN_ERR "user_mad: couldn't register device number\n");
		goto out;
	}

	umad_class = class_create(THIS_MODULE, "infiniband_mad");
	if (IS_ERR(umad_class)) {
		ret = PTR_ERR(umad_class);
		printk(KERN_ERR "user_mad: couldn't create class infiniband_mad\n");
		goto out_chrdev;
	}

	umad_class->devnode = umad_devnode;

	ret = class_create_file(umad_class, &class_attr_abi_version.attr);
	if (ret) {
		printk(KERN_ERR "user_mad: couldn't create abi_version attribute\n");
		goto out_class;
	}

	ret = ib_register_client(&umad_client);
	if (ret) {
		printk(KERN_ERR "user_mad: couldn't register ib_umad client\n");
		goto out_class;
	}

	return 0;

out_class:
	class_destroy(umad_class);

out_chrdev:
	unregister_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2);

out:
	return ret;
}

static void __exit ib_umad_cleanup(void)
{
	ib_unregister_client(&umad_client);
	class_destroy(umad_class);
	unregister_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2);
	if (overflow_maj)
		unregister_chrdev_region(overflow_maj, IB_UMAD_MAX_PORTS * 2);
}

module_init(ib_umad_init);
module_exit(ib_umad_cleanup);
