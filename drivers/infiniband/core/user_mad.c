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

#define pr_fmt(fmt) "user_mad: " fmt

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
#include <linux/nospec.h>

#include <linux/uaccess.h>

#include <rdma/ib_mad.h>
#include <rdma/ib_user_mad.h>
#include <rdma/rdma_netlink.h>

#include "core_priv.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand userspace MAD packet access");
MODULE_LICENSE("Dual BSD/GPL");

enum {
	IB_UMAD_MAX_PORTS  = RDMA_MAX_PORTS,
	IB_UMAD_MAX_AGENTS = 32,

	IB_UMAD_MAJOR      = 231,
	IB_UMAD_MINOR_BASE = 0,
	IB_UMAD_NUM_FIXED_MINOR = 64,
	IB_UMAD_NUM_DYNAMIC_MINOR = IB_UMAD_MAX_PORTS - IB_UMAD_NUM_FIXED_MINOR,
	IB_ISSM_MINOR_BASE        = IB_UMAD_NUM_FIXED_MINOR,
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
	struct device	      dev;
	struct cdev           sm_cdev;
	struct device	      sm_dev;
	struct semaphore       sm_sem;

	struct mutex	       file_mutex;
	struct list_head       file_list;

	struct ib_device      *ib_dev;
	struct ib_umad_device *umad_dev;
	int                    dev_num;
	u8                     port_num;
};

struct ib_umad_device {
	struct kref kref;
	struct ib_umad_port ports[];
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

#define CREATE_TRACE_POINTS
#include <trace/events/ib_umad.h>

static const dev_t base_umad_dev = MKDEV(IB_UMAD_MAJOR, IB_UMAD_MINOR_BASE);
static const dev_t base_issm_dev = MKDEV(IB_UMAD_MAJOR, IB_UMAD_MINOR_BASE) +
				   IB_UMAD_NUM_FIXED_MINOR;
static dev_t dynamic_umad_dev;
static dev_t dynamic_issm_dev;

static DEFINE_IDA(umad_ida);

static int ib_umad_add_one(struct ib_device *device);
static void ib_umad_remove_one(struct ib_device *device, void *client_data);

static void ib_umad_dev_free(struct kref *kref)
{
	struct ib_umad_device *dev =
		container_of(kref, struct ib_umad_device, kref);

	kfree(dev);
}

static void ib_umad_dev_get(struct ib_umad_device *dev)
{
	kref_get(&dev->kref);
}

static void ib_umad_dev_put(struct ib_umad_device *dev)
{
	kref_put(&dev->kref, ib_umad_dev_free);
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
	rdma_destroy_ah(packet->msg->ah, RDMA_DESTROY_AH_SLEEPABLE);
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
			 struct ib_mad_send_buf *send_buf,
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
	/*
	 * On OPA devices it is okay to lose the upper 16 bits of LID as this
	 * information is obtained elsewhere. Mask off the upper 16 bits.
	 */
	if (rdma_cap_opa_mad(agent->device, agent->port_num))
		packet->mad.hdr.lid = ib_lid_be16(0xFFFF &
						  mad_recv_wc->wc->slid);
	else
		packet->mad.hdr.lid = ib_lid_be16(mad_recv_wc->wc->slid);
	packet->mad.hdr.sl	   = mad_recv_wc->wc->sl;
	packet->mad.hdr.path_bits  = mad_recv_wc->wc->dlid_path_bits;
	packet->mad.hdr.pkey_index = mad_recv_wc->wc->pkey_index;
	packet->mad.hdr.grh_present = !!(mad_recv_wc->wc->wc_flags & IB_WC_GRH);
	if (packet->mad.hdr.grh_present) {
		struct rdma_ah_attr ah_attr;
		const struct ib_global_route *grh;
		int ret;

		ret = ib_init_ah_attr_from_wc(agent->device, agent->port_num,
					      mad_recv_wc->wc,
					      mad_recv_wc->recv_buf.grh,
					      &ah_attr);
		if (ret)
			goto err2;

		grh = rdma_ah_read_grh(&ah_attr);
		packet->mad.hdr.gid_index = grh->sgid_index;
		packet->mad.hdr.hop_limit = grh->hop_limit;
		packet->mad.hdr.traffic_class = grh->traffic_class;
		memcpy(packet->mad.hdr.gid, &grh->dgid, 16);
		packet->mad.hdr.flow_label = cpu_to_be32(grh->flow_label);
		rdma_destroy_ah_attr(&ah_attr);
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
	size_t seg_size;

	recv_buf = &packet->recv_wc->recv_buf;
	seg_size = packet->recv_wc->mad_seg_size;

	/* We need enough room to copy the first (or only) MAD segment. */
	if ((packet->length <= seg_size &&
	     count < hdr_size(file) + packet->length) ||
	    (packet->length > seg_size &&
	     count < hdr_size(file) + seg_size))
		return -EINVAL;

	if (copy_to_user(buf, &packet->mad, hdr_size(file)))
		return -EFAULT;

	buf += hdr_size(file);
	seg_payload = min_t(int, packet->length, seg_size);
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
		max_seg_payload = seg_size - offset;

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

	trace_ib_umad_read_recv(file, &packet->mad.hdr, &recv_buf->mad->mad_hdr);

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

	trace_ib_umad_read_send(file, &packet->mad.hdr,
				(struct ib_mad_hdr *)&packet->mad.data);

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

	if (file->agents_dead) {
		mutex_unlock(&file->mutex);
		return -EIO;
	}

	while (list_empty(&file->recv_list)) {
		mutex_unlock(&file->mutex);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(file->recv_wait,
					     !list_empty(&file->recv_list)))
			return -ERESTARTSYS;

		mutex_lock(&file->mutex);
	}

	if (file->agents_dead) {
		mutex_unlock(&file->mutex);
		return -EIO;
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
		if (!ib_response_mad(hdr)) {
			if (!ib_response_mad(sent_hdr))
				return 1;
			continue;
		} else if (!ib_response_mad(sent_hdr))
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
	struct rdma_ah_attr ah_attr;
	struct ib_ah *ah;
	struct ib_rmpp_mad *rmpp_mad;
	__be64 *tid;
	int ret, data_len, hdr_len, copy_offset, rmpp_active;
	u8 base_version;

	if (count < hdr_size(file) + IB_MGMT_RMPP_HDR)
		return -EINVAL;

	packet = kzalloc(sizeof *packet + IB_MGMT_RMPP_HDR, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(&packet->mad, buf, hdr_size(file))) {
		ret = -EFAULT;
		goto err;
	}

	if (packet->mad.hdr.id >= IB_UMAD_MAX_AGENTS) {
		ret = -EINVAL;
		goto err;
	}

	buf += hdr_size(file);

	if (copy_from_user(packet->mad.data, buf, IB_MGMT_RMPP_HDR)) {
		ret = -EFAULT;
		goto err;
	}

	mutex_lock(&file->mutex);

	trace_ib_umad_write(file, &packet->mad.hdr,
			    (struct ib_mad_hdr *)&packet->mad.data);

	agent = __get_agent(file, packet->mad.hdr.id);
	if (!agent) {
		ret = -EIO;
		goto err_up;
	}

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.type = rdma_ah_find_type(agent->device,
					 file->port->port_num);
	rdma_ah_set_dlid(&ah_attr, be16_to_cpu(packet->mad.hdr.lid));
	rdma_ah_set_sl(&ah_attr, packet->mad.hdr.sl);
	rdma_ah_set_path_bits(&ah_attr, packet->mad.hdr.path_bits);
	rdma_ah_set_port_num(&ah_attr, file->port->port_num);
	if (packet->mad.hdr.grh_present) {
		rdma_ah_set_grh(&ah_attr, NULL,
				be32_to_cpu(packet->mad.hdr.flow_label),
				packet->mad.hdr.gid_index,
				packet->mad.hdr.hop_limit,
				packet->mad.hdr.traffic_class);
		rdma_ah_set_dgid_raw(&ah_attr, packet->mad.hdr.gid);
	}

	ah = rdma_create_user_ah(agent->qp->pd, &ah_attr, NULL);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_up;
	}

	rmpp_mad = (struct ib_rmpp_mad *) packet->mad.data;
	hdr_len = ib_get_mad_data_offset(rmpp_mad->mad_hdr.mgmt_class);

	if (ib_is_mad_class_rmpp(rmpp_mad->mad_hdr.mgmt_class)
	    && ib_mad_kernel_rmpp_agent(agent)) {
		copy_offset = IB_MGMT_RMPP_HDR;
		rmpp_active = ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
						IB_MGMT_RMPP_FLAG_ACTIVE;
	} else {
		copy_offset = IB_MGMT_MAD_HDR;
		rmpp_active = 0;
	}

	base_version = ((struct ib_mad_hdr *)&packet->mad.data)->base_version;
	data_len = count - hdr_size(file) - hdr_len;
	packet->msg = ib_create_send_mad(agent,
					 be32_to_cpu(packet->mad.hdr.qpn),
					 packet->mad.hdr.pkey_index, rmpp_active,
					 hdr_len, data_len, GFP_KERNEL,
					 base_version);
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

	if (!ib_mad_kernel_rmpp_agent(agent)
	   && ib_is_mad_class_rmpp(rmpp_mad->mad_hdr.mgmt_class)
	   && (ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) & IB_MGMT_RMPP_FLAG_ACTIVE)) {
		spin_lock_irq(&file->send_lock);
		list_add_tail(&packet->list, &file->send_list);
		spin_unlock_irq(&file->send_lock);
	} else {
		spin_lock_irq(&file->send_lock);
		ret = is_duplicate(file, packet);
		if (!ret)
			list_add_tail(&packet->list, &file->send_list);
		spin_unlock_irq(&file->send_lock);
		if (ret) {
			ret = -EINVAL;
			goto err_msg;
		}
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
	rdma_destroy_ah(ah, RDMA_DESTROY_AH_SLEEPABLE);
err_up:
	mutex_unlock(&file->mutex);
err:
	kfree(packet);
	return ret;
}

static __poll_t ib_umad_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ib_umad_file *file = filp->private_data;

	/* we will always be able to post a MAD send */
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	mutex_lock(&file->mutex);
	poll_wait(filp, &file->recv_wait, wait);

	if (!list_empty(&file->recv_list))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (file->agents_dead)
		mask = EPOLLERR;
	mutex_unlock(&file->mutex);

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
		dev_notice(&file->port->dev,
			   "ib_umad_reg_agent: invalid device\n");
		ret = -EPIPE;
		goto out;
	}

	if (copy_from_user(&ureq, arg, sizeof ureq)) {
		ret = -EFAULT;
		goto out;
	}

	if (ureq.qpn != 0 && ureq.qpn != 1) {
		dev_notice(&file->port->dev,
			   "ib_umad_reg_agent: invalid QPN %d specified\n",
			   ureq.qpn);
		ret = -EINVAL;
		goto out;
	}

	for (agent_id = 0; agent_id < IB_UMAD_MAX_AGENTS; ++agent_id)
		if (!__get_agent(file, agent_id))
			goto found;

	dev_notice(&file->port->dev,
		   "ib_umad_reg_agent: Max Agents (%u) reached\n",
		   IB_UMAD_MAX_AGENTS);
	ret = -ENOMEM;
	goto out;

found:
	if (ureq.mgmt_class) {
		memset(&req, 0, sizeof(req));
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
				      send_handler, recv_handler, file, 0);
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
			dev_warn(&file->port->dev,
				"process %s did not enable P_Key index support.\n",
				current->comm);
			dev_warn(&file->port->dev,
				"   Documentation/infiniband/user_mad.rst has info on the new ABI.\n");
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

static int ib_umad_reg_agent2(struct ib_umad_file *file, void __user *arg)
{
	struct ib_user_mad_reg_req2 ureq;
	struct ib_mad_reg_req req;
	struct ib_mad_agent *agent = NULL;
	int agent_id;
	int ret;

	mutex_lock(&file->port->file_mutex);
	mutex_lock(&file->mutex);

	if (!file->port->ib_dev) {
		dev_notice(&file->port->dev,
			   "ib_umad_reg_agent2: invalid device\n");
		ret = -EPIPE;
		goto out;
	}

	if (copy_from_user(&ureq, arg, sizeof(ureq))) {
		ret = -EFAULT;
		goto out;
	}

	if (ureq.qpn != 0 && ureq.qpn != 1) {
		dev_notice(&file->port->dev,
			   "ib_umad_reg_agent2: invalid QPN %d specified\n",
			   ureq.qpn);
		ret = -EINVAL;
		goto out;
	}

	if (ureq.flags & ~IB_USER_MAD_REG_FLAGS_CAP) {
		dev_notice(&file->port->dev,
			   "ib_umad_reg_agent2 failed: invalid registration flags specified 0x%x; supported 0x%x\n",
			   ureq.flags, IB_USER_MAD_REG_FLAGS_CAP);
		ret = -EINVAL;

		if (put_user((u32)IB_USER_MAD_REG_FLAGS_CAP,
				(u32 __user *) (arg + offsetof(struct
				ib_user_mad_reg_req2, flags))))
			ret = -EFAULT;

		goto out;
	}

	for (agent_id = 0; agent_id < IB_UMAD_MAX_AGENTS; ++agent_id)
		if (!__get_agent(file, agent_id))
			goto found;

	dev_notice(&file->port->dev,
		   "ib_umad_reg_agent2: Max Agents (%u) reached\n",
		   IB_UMAD_MAX_AGENTS);
	ret = -ENOMEM;
	goto out;

found:
	if (ureq.mgmt_class) {
		memset(&req, 0, sizeof(req));
		req.mgmt_class         = ureq.mgmt_class;
		req.mgmt_class_version = ureq.mgmt_class_version;
		if (ureq.oui & 0xff000000) {
			dev_notice(&file->port->dev,
				   "ib_umad_reg_agent2 failed: oui invalid 0x%08x\n",
				   ureq.oui);
			ret = -EINVAL;
			goto out;
		}
		req.oui[2] =  ureq.oui & 0x0000ff;
		req.oui[1] = (ureq.oui & 0x00ff00) >> 8;
		req.oui[0] = (ureq.oui & 0xff0000) >> 16;
		memcpy(req.method_mask, ureq.method_mask,
			sizeof(req.method_mask));
	}

	agent = ib_register_mad_agent(file->port->ib_dev, file->port->port_num,
				      ureq.qpn ? IB_QPT_GSI : IB_QPT_SMI,
				      ureq.mgmt_class ? &req : NULL,
				      ureq.rmpp_version,
				      send_handler, recv_handler, file,
				      ureq.flags);
	if (IS_ERR(agent)) {
		ret = PTR_ERR(agent);
		agent = NULL;
		goto out;
	}

	if (put_user(agent_id,
		     (u32 __user *)(arg +
				offsetof(struct ib_user_mad_reg_req2, id)))) {
		ret = -EFAULT;
		goto out;
	}

	if (!file->already_used) {
		file->already_used = 1;
		file->use_pkey_index = 1;
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
	if (id >= IB_UMAD_MAX_AGENTS)
		return -EINVAL;

	mutex_lock(&file->port->file_mutex);
	mutex_lock(&file->mutex);

	id = array_index_nospec(id, IB_UMAD_MAX_AGENTS);
	if (!__get_agent(file, id)) {
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
	case IB_USER_MAD_REGISTER_AGENT2:
		return ib_umad_reg_agent2(filp->private_data, (void __user *) arg);
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
	case IB_USER_MAD_REGISTER_AGENT2:
		return ib_umad_reg_agent2(filp->private_data, compat_ptr(arg));
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
	int ret = 0;

	port = container_of(inode->i_cdev, struct ib_umad_port, cdev);

	mutex_lock(&port->file_mutex);

	if (!port->ib_dev) {
		ret = -ENXIO;
		goto out;
	}

	if (!rdma_dev_access_netns(port->ib_dev, current->nsproxy->net_ns)) {
		ret = -EPERM;
		goto out;
	}

	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file) {
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

	stream_open(inode, filp);
out:
	mutex_unlock(&port->file_mutex);
	return ret;
}

static int ib_umad_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_file *file = filp->private_data;
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
	mutex_destroy(&file->mutex);
	kfree(file);
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

	if (!rdma_dev_access_netns(port->ib_dev, current->nsproxy->net_ns)) {
		ret = -EPERM;
		goto err_up_sem;
	}

	ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	if (ret)
		goto err_up_sem;

	filp->private_data = port;

	nonseekable_open(inode, filp);
	return 0;

err_up_sem:
	up(&port->sm_sem);

fail:
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

	return ret;
}

static const struct file_operations umad_sm_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ib_umad_sm_open,
	.release = ib_umad_sm_close,
	.llseek	 = no_llseek,
};

static struct ib_umad_port *get_port(struct ib_device *ibdev,
				     struct ib_umad_device *umad_dev,
				     unsigned int port)
{
	if (!umad_dev)
		return ERR_PTR(-EOPNOTSUPP);
	if (!rdma_is_port_valid(ibdev, port))
		return ERR_PTR(-EINVAL);
	if (!rdma_cap_ib_mad(ibdev, port))
		return ERR_PTR(-EOPNOTSUPP);

	return &umad_dev->ports[port - rdma_start_port(ibdev)];
}

static int ib_umad_get_nl_info(struct ib_device *ibdev, void *client_data,
			       struct ib_client_nl_info *res)
{
	struct ib_umad_port *port = get_port(ibdev, client_data, res->port);

	if (IS_ERR(port))
		return PTR_ERR(port);

	res->abi = IB_USER_MAD_ABI_VERSION;
	res->cdev = &port->dev;
	return 0;
}

static struct ib_client umad_client = {
	.name   = "umad",
	.add    = ib_umad_add_one,
	.remove = ib_umad_remove_one,
	.get_nl_info = ib_umad_get_nl_info,
};
MODULE_ALIAS_RDMA_CLIENT("umad");

static int ib_issm_get_nl_info(struct ib_device *ibdev, void *client_data,
			       struct ib_client_nl_info *res)
{
	struct ib_umad_port *port = get_port(ibdev, client_data, res->port);

	if (IS_ERR(port))
		return PTR_ERR(port);

	res->abi = IB_USER_MAD_ABI_VERSION;
	res->cdev = &port->sm_dev;
	return 0;
}

static struct ib_client issm_client = {
	.name = "issm",
	.get_nl_info = ib_issm_get_nl_info,
};
MODULE_ALIAS_RDMA_CLIENT("issm");

static ssize_t ibdev_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ib_umad_port *port = dev_get_drvdata(dev);

	if (!port)
		return -ENODEV;

	return sysfs_emit(buf, "%s\n", dev_name(&port->ib_dev->dev));
}
static DEVICE_ATTR_RO(ibdev);

static ssize_t port_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ib_umad_port *port = dev_get_drvdata(dev);

	if (!port)
		return -ENODEV;

	return sysfs_emit(buf, "%d\n", port->port_num);
}
static DEVICE_ATTR_RO(port);

static struct attribute *umad_class_dev_attrs[] = {
	&dev_attr_ibdev.attr,
	&dev_attr_port.attr,
	NULL,
};
ATTRIBUTE_GROUPS(umad_class_dev);

static char *umad_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}

static ssize_t abi_version_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", IB_USER_MAD_ABI_VERSION);
}
static CLASS_ATTR_RO(abi_version);

static struct attribute *umad_class_attrs[] = {
	&class_attr_abi_version.attr,
	NULL,
};
ATTRIBUTE_GROUPS(umad_class);

static struct class umad_class = {
	.name		= "infiniband_mad",
	.devnode	= umad_devnode,
	.class_groups	= umad_class_groups,
	.dev_groups	= umad_class_dev_groups,
};

static void ib_umad_release_port(struct device *device)
{
	struct ib_umad_port *port = dev_get_drvdata(device);
	struct ib_umad_device *umad_dev = port->umad_dev;

	ib_umad_dev_put(umad_dev);
}

static void ib_umad_init_port_dev(struct device *dev,
				  struct ib_umad_port *port,
				  const struct ib_device *device)
{
	device_initialize(dev);
	ib_umad_dev_get(port->umad_dev);
	dev->class = &umad_class;
	dev->parent = device->dev.parent;
	dev_set_drvdata(dev, port);
	dev->release = ib_umad_release_port;
}

static int ib_umad_init_port(struct ib_device *device, int port_num,
			     struct ib_umad_device *umad_dev,
			     struct ib_umad_port *port)
{
	int devnum;
	dev_t base_umad;
	dev_t base_issm;
	int ret;

	devnum = ida_alloc_max(&umad_ida, IB_UMAD_MAX_PORTS - 1, GFP_KERNEL);
	if (devnum < 0)
		return -1;
	port->dev_num = devnum;
	if (devnum >= IB_UMAD_NUM_FIXED_MINOR) {
		base_umad = dynamic_umad_dev + devnum - IB_UMAD_NUM_FIXED_MINOR;
		base_issm = dynamic_issm_dev + devnum - IB_UMAD_NUM_FIXED_MINOR;
	} else {
		base_umad = devnum + base_umad_dev;
		base_issm = devnum + base_issm_dev;
	}

	port->ib_dev   = device;
	port->umad_dev = umad_dev;
	port->port_num = port_num;
	sema_init(&port->sm_sem, 1);
	mutex_init(&port->file_mutex);
	INIT_LIST_HEAD(&port->file_list);

	ib_umad_init_port_dev(&port->dev, port, device);
	port->dev.devt = base_umad;
	dev_set_name(&port->dev, "umad%d", port->dev_num);
	cdev_init(&port->cdev, &umad_fops);
	port->cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&port->cdev, &port->dev);
	if (ret)
		goto err_cdev;

	ib_umad_init_port_dev(&port->sm_dev, port, device);
	port->sm_dev.devt = base_issm;
	dev_set_name(&port->sm_dev, "issm%d", port->dev_num);
	cdev_init(&port->sm_cdev, &umad_sm_fops);
	port->sm_cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&port->sm_cdev, &port->sm_dev);
	if (ret)
		goto err_dev;

	return 0;

err_dev:
	put_device(&port->sm_dev);
	cdev_device_del(&port->cdev, &port->dev);
err_cdev:
	put_device(&port->dev);
	ida_free(&umad_ida, devnum);
	return ret;
}

static void ib_umad_kill_port(struct ib_umad_port *port)
{
	struct ib_umad_file *file;
	int id;

	cdev_device_del(&port->sm_cdev, &port->sm_dev);
	cdev_device_del(&port->cdev, &port->dev);

	mutex_lock(&port->file_mutex);

	/* Mark ib_dev NULL and block ioctl or other file ops to progress
	 * further.
	 */
	port->ib_dev = NULL;

	list_for_each_entry(file, &port->file_list, port_list) {
		mutex_lock(&file->mutex);
		file->agents_dead = 1;
		wake_up_interruptible(&file->recv_wait);
		mutex_unlock(&file->mutex);

		for (id = 0; id < IB_UMAD_MAX_AGENTS; ++id)
			if (file->agent[id])
				ib_unregister_mad_agent(file->agent[id]);
	}

	mutex_unlock(&port->file_mutex);

	ida_free(&umad_ida, port->dev_num);

	/* balances device_initialize() */
	put_device(&port->sm_dev);
	put_device(&port->dev);
}

static int ib_umad_add_one(struct ib_device *device)
{
	struct ib_umad_device *umad_dev;
	int s, e, i;
	int count = 0;
	int ret;

	s = rdma_start_port(device);
	e = rdma_end_port(device);

	umad_dev = kzalloc(struct_size(umad_dev, ports, e - s + 1), GFP_KERNEL);
	if (!umad_dev)
		return -ENOMEM;

	kref_init(&umad_dev->kref);
	for (i = s; i <= e; ++i) {
		if (!rdma_cap_ib_mad(device, i))
			continue;

		ret = ib_umad_init_port(device, i, umad_dev,
					&umad_dev->ports[i - s]);
		if (ret)
			goto err;

		count++;
	}

	if (!count) {
		ret = -EOPNOTSUPP;
		goto free;
	}

	ib_set_client_data(device, &umad_client, umad_dev);

	return 0;

err:
	while (--i >= s) {
		if (!rdma_cap_ib_mad(device, i))
			continue;

		ib_umad_kill_port(&umad_dev->ports[i - s]);
	}
free:
	/* balances kref_init */
	ib_umad_dev_put(umad_dev);
	return ret;
}

static void ib_umad_remove_one(struct ib_device *device, void *client_data)
{
	struct ib_umad_device *umad_dev = client_data;
	unsigned int i;

	rdma_for_each_port (device, i) {
		if (rdma_cap_ib_mad(device, i))
			ib_umad_kill_port(
				&umad_dev->ports[i - rdma_start_port(device)]);
	}
	/* balances kref_init() */
	ib_umad_dev_put(umad_dev);
}

static int __init ib_umad_init(void)
{
	int ret;

	ret = register_chrdev_region(base_umad_dev,
				     IB_UMAD_NUM_FIXED_MINOR * 2,
				     umad_class.name);
	if (ret) {
		pr_err("couldn't register device number\n");
		goto out;
	}

	ret = alloc_chrdev_region(&dynamic_umad_dev, 0,
				  IB_UMAD_NUM_DYNAMIC_MINOR * 2,
				  umad_class.name);
	if (ret) {
		pr_err("couldn't register dynamic device number\n");
		goto out_alloc;
	}
	dynamic_issm_dev = dynamic_umad_dev + IB_UMAD_NUM_DYNAMIC_MINOR;

	ret = class_register(&umad_class);
	if (ret) {
		pr_err("couldn't create class infiniband_mad\n");
		goto out_chrdev;
	}

	ret = ib_register_client(&umad_client);
	if (ret)
		goto out_class;

	ret = ib_register_client(&issm_client);
	if (ret)
		goto out_client;

	return 0;

out_client:
	ib_unregister_client(&umad_client);
out_class:
	class_unregister(&umad_class);

out_chrdev:
	unregister_chrdev_region(dynamic_umad_dev,
				 IB_UMAD_NUM_DYNAMIC_MINOR * 2);

out_alloc:
	unregister_chrdev_region(base_umad_dev,
				 IB_UMAD_NUM_FIXED_MINOR * 2);

out:
	return ret;
}

static void __exit ib_umad_cleanup(void)
{
	ib_unregister_client(&issm_client);
	ib_unregister_client(&umad_client);
	class_unregister(&umad_class);
	unregister_chrdev_region(base_umad_dev,
				 IB_UMAD_NUM_FIXED_MINOR * 2);
	unregister_chrdev_region(dynamic_umad_dev,
				 IB_UMAD_NUM_DYNAMIC_MINOR * 2);
}

module_init(ib_umad_init);
module_exit(ib_umad_cleanup);
