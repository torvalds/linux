/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved. 
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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
 *
 * $Id: user_mad.c 2814 2005-07-06 19:14:09Z halr $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/poll.h>
#include <linux/rwsem.h>
#include <linux/kref.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>

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
 * Our lifetime rules for these structs are the following: each time a
 * device special file is opened, we look up the corresponding struct
 * ib_umad_port by minor in the umad_port[] table while holding the
 * port_lock.  If this lookup succeeds, we take a reference on the
 * ib_umad_port's struct ib_umad_device while still holding the
 * port_lock; if the lookup fails, we fail the open().  We drop these
 * references in the corresponding close().
 *
 * In addition to references coming from open character devices, there
 * is one more reference to each ib_umad_device representing the
 * module's reference taken when allocating the ib_umad_device in
 * ib_umad_add_one().
 *
 * When destroying an ib_umad_device, we clear all of its
 * ib_umad_ports from umad_port[] while holding port_lock before
 * dropping the module's reference to the ib_umad_device.  This is
 * always safe because any open() calls will either succeed and obtain
 * a reference before we clear the umad_port[] entries, or fail after
 * we clear the umad_port[] entries.
 */

struct ib_umad_port {
	struct cdev           *dev;
	struct class_device   *class_dev;

	struct cdev           *sm_dev;
	struct class_device   *sm_class_dev;
	struct semaphore       sm_sem;

	struct rw_semaphore    mutex;
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
	struct ib_umad_port *port;
	struct list_head     recv_list;
	struct list_head     port_list;
	spinlock_t           recv_lock;
	wait_queue_head_t    recv_wait;
	struct ib_mad_agent *agent[IB_UMAD_MAX_AGENTS];
	struct ib_mr        *mr[IB_UMAD_MAX_AGENTS];
};

struct ib_umad_packet {
	struct ib_mad_send_buf *msg;
	struct list_head   list;
	int		   length;
	struct ib_user_mad mad;
};

static struct class *umad_class;

static const dev_t base_dev = MKDEV(IB_UMAD_MAJOR, IB_UMAD_MINOR_BASE);

static DEFINE_SPINLOCK(port_lock);
static struct ib_umad_port *umad_port[IB_UMAD_MAX_PORTS];
static DECLARE_BITMAP(dev_map, IB_UMAD_MAX_PORTS * 2);

static void ib_umad_add_one(struct ib_device *device);
static void ib_umad_remove_one(struct ib_device *device);

static void ib_umad_release_dev(struct kref *ref)
{
	struct ib_umad_device *dev =
		container_of(ref, struct ib_umad_device, ref);

	kfree(dev);
}

static int queue_packet(struct ib_umad_file *file,
			struct ib_mad_agent *agent,
			struct ib_umad_packet *packet)
{
	int ret = 1;

	down_read(&file->port->mutex);
	for (packet->mad.hdr.id = 0;
	     packet->mad.hdr.id < IB_UMAD_MAX_AGENTS;
	     packet->mad.hdr.id++)
		if (agent == file->agent[packet->mad.hdr.id]) {
			spin_lock_irq(&file->recv_lock);
			list_add_tail(&packet->list, &file->recv_list);
			spin_unlock_irq(&file->recv_lock);
			wake_up_interruptible(&file->recv_wait);
			ret = 0;
			break;
		}

	up_read(&file->port->mutex);

	return ret;
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *send_wc)
{
	struct ib_umad_file *file = agent->context;
	struct ib_umad_packet *timeout;
	struct ib_umad_packet *packet = send_wc->send_buf->context[0];

	ib_destroy_ah(packet->msg->ah);
	ib_free_send_mad(packet->msg);

	if (send_wc->status == IB_WC_RESP_TIMEOUT_ERR) {
		timeout = kzalloc(sizeof *timeout + IB_MGMT_MAD_HDR, GFP_KERNEL);
		if (!timeout)
			goto out;

		timeout->length 	= IB_MGMT_MAD_HDR;
		timeout->mad.hdr.id 	= packet->mad.hdr.id;
		timeout->mad.hdr.status = ETIMEDOUT;
		memcpy(timeout->mad.data, packet->mad.data,
		       sizeof (struct ib_mad_hdr));

		if (!queue_packet(file, agent, timeout))
				return;
	}
out:
	kfree(packet);
}

static void recv_handler(struct ib_mad_agent *agent,
			 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_umad_file *file = agent->context;
	struct ib_umad_packet *packet;
	int length;

	if (mad_recv_wc->wc->status != IB_WC_SUCCESS)
		goto out;

	length = mad_recv_wc->mad_len;
	packet = kzalloc(sizeof *packet + length, GFP_KERNEL);
	if (!packet)
		goto out;

	packet->length = length;

	ib_coalesce_recv_mad(mad_recv_wc, packet->mad.data);

	packet->mad.hdr.status    = 0;
	packet->mad.hdr.length    = length + sizeof (struct ib_user_mad);
	packet->mad.hdr.qpn 	  = cpu_to_be32(mad_recv_wc->wc->src_qp);
	packet->mad.hdr.lid 	  = cpu_to_be16(mad_recv_wc->wc->slid);
	packet->mad.hdr.sl  	  = mad_recv_wc->wc->sl;
	packet->mad.hdr.path_bits = mad_recv_wc->wc->dlid_path_bits;
	packet->mad.hdr.grh_present = !!(mad_recv_wc->wc->wc_flags & IB_WC_GRH);
	if (packet->mad.hdr.grh_present) {
		/* XXX parse GRH */
		packet->mad.hdr.gid_index 	= 0;
		packet->mad.hdr.hop_limit 	= 0;
		packet->mad.hdr.traffic_class	= 0;
		memset(packet->mad.hdr.gid, 0, 16);
		packet->mad.hdr.flow_label	= 0;
	}

	if (queue_packet(file, agent, packet))
		kfree(packet);

out:
	ib_free_recv_mad(mad_recv_wc);
}

static ssize_t ib_umad_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_packet *packet;
	ssize_t ret;

	if (count < sizeof (struct ib_user_mad) + sizeof (struct ib_mad))
		return -EINVAL;

	spin_lock_irq(&file->recv_lock);

	while (list_empty(&file->recv_list)) {
		spin_unlock_irq(&file->recv_lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(file->recv_wait,
					     !list_empty(&file->recv_list)))
			return -ERESTARTSYS;

		spin_lock_irq(&file->recv_lock);
	}

	packet = list_entry(file->recv_list.next, struct ib_umad_packet, list);
	list_del(&packet->list);

	spin_unlock_irq(&file->recv_lock);

	if (count < packet->length + sizeof (struct ib_user_mad)) {
		/* Return length needed (and first RMPP segment) if too small */
		if (copy_to_user(buf, &packet->mad,
				 sizeof (struct ib_user_mad) + sizeof (struct ib_mad)))
			ret = -EFAULT;
		else
			ret = -ENOSPC;
	} else if (copy_to_user(buf, &packet->mad,
				packet->length + sizeof (struct ib_user_mad)))
		ret = -EFAULT;
	else
		ret = packet->length + sizeof (struct ib_user_mad);
	if (ret < 0) {
		/* Requeue packet */
		spin_lock_irq(&file->recv_lock);
		list_add(&packet->list, &file->recv_list);
		spin_unlock_irq(&file->recv_lock);
	} else
		kfree(packet);
	return ret;
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
	u8 method;
	__be64 *tid;
	int ret, length, hdr_len, copy_offset;
	int rmpp_active = 0;

	if (count < sizeof (struct ib_user_mad))
		return -EINVAL;

	length = count - sizeof (struct ib_user_mad);
	packet = kmalloc(sizeof *packet + IB_MGMT_RMPP_HDR, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(&packet->mad, buf,
			    sizeof (struct ib_user_mad) + IB_MGMT_RMPP_HDR)) {
		ret = -EFAULT;
		goto err;
	}

	if (packet->mad.hdr.id < 0 ||
	    packet->mad.hdr.id >= IB_UMAD_MAX_AGENTS) {
		ret = -EINVAL;
		goto err;
	}

	down_read(&file->port->mutex);

	agent = file->agent[packet->mad.hdr.id];
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
		ah_attr.grh.flow_label 	   = be32_to_cpu(packet->mad.hdr.flow_label);
		ah_attr.grh.hop_limit  	   = packet->mad.hdr.hop_limit;
		ah_attr.grh.traffic_class  = packet->mad.hdr.traffic_class;
	}

	ah = ib_create_ah(agent->qp->pd, &ah_attr);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_up;
	}

	rmpp_mad = (struct ib_rmpp_mad *) packet->mad.data;
	if (ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) & IB_MGMT_RMPP_FLAG_ACTIVE) {
		/* RMPP active */
		if (!agent->rmpp_version) {
			ret = -EINVAL;
			goto err_ah;
		}

		/* Validate that the management class can support RMPP */
		if (rmpp_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_ADM) {
			hdr_len = IB_MGMT_SA_HDR;
		} else if ((rmpp_mad->mad_hdr.mgmt_class >= IB_MGMT_CLASS_VENDOR_RANGE2_START) &&
			    (rmpp_mad->mad_hdr.mgmt_class <= IB_MGMT_CLASS_VENDOR_RANGE2_END)) {
				hdr_len = IB_MGMT_VENDOR_HDR;
		} else {
			ret = -EINVAL;
			goto err_ah;
		}
		rmpp_active = 1;
		copy_offset = IB_MGMT_RMPP_HDR;
	} else {
		hdr_len = IB_MGMT_MAD_HDR;
		copy_offset = IB_MGMT_MAD_HDR;
	}

	packet->msg = ib_create_send_mad(agent,
					 be32_to_cpu(packet->mad.hdr.qpn),
					 0, rmpp_active,
					 hdr_len, length - hdr_len,
					 GFP_KERNEL);
	if (IS_ERR(packet->msg)) {
		ret = PTR_ERR(packet->msg);
		goto err_ah;
	}

	packet->msg->ah 	= ah;
	packet->msg->timeout_ms = packet->mad.hdr.timeout_ms;
	packet->msg->retries 	= packet->mad.hdr.retries;
	packet->msg->context[0] = packet;

	/* Copy MAD headers (RMPP header in place) */
	memcpy(packet->msg->mad, packet->mad.data, IB_MGMT_MAD_HDR);
	/* Now, copy rest of message from user into send buffer */
	if (copy_from_user(packet->msg->mad + copy_offset,
			   buf + sizeof (struct ib_user_mad) + copy_offset,
			   length - copy_offset)) {
		ret = -EFAULT;
		goto err_msg;
	}

	/*
	 * If userspace is generating a request that will generate a
	 * response, we need to make sure the high-order part of the
	 * transaction ID matches the agent being used to send the
	 * MAD.
	 */
	method = ((struct ib_mad_hdr *) packet->msg->mad)->method;

	if (!(method & IB_MGMT_METHOD_RESP)       &&
	    method != IB_MGMT_METHOD_TRAP_REPRESS &&
	    method != IB_MGMT_METHOD_SEND) {
		tid = &((struct ib_mad_hdr *) packet->msg->mad)->tid;
		*tid = cpu_to_be64(((u64) agent->hi_tid) << 32 |
				   (be64_to_cpup(tid) & 0xffffffff));
	}

	ret = ib_post_send_mad(packet->msg, NULL);
	if (ret)
		goto err_msg;

	up_read(&file->port->mutex);

	return count;

err_msg:
	ib_free_send_mad(packet->msg);

err_ah:
	ib_destroy_ah(ah);

err_up:
	up_read(&file->port->mutex);

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

static int ib_umad_reg_agent(struct ib_umad_file *file, unsigned long arg)
{
	struct ib_user_mad_reg_req ureq;
	struct ib_mad_reg_req req;
	struct ib_mad_agent *agent;
	int agent_id;
	int ret;

	down_write(&file->port->mutex);

	if (!file->port->ib_dev) {
		ret = -EPIPE;
		goto out;
	}

	if (copy_from_user(&ureq, (void __user *) arg, sizeof ureq)) {
		ret = -EFAULT;
		goto out;
	}

	if (ureq.qpn != 0 && ureq.qpn != 1) {
		ret = -EINVAL;
		goto out;
	}

	for (agent_id = 0; agent_id < IB_UMAD_MAX_AGENTS; ++agent_id)
		if (!file->agent[agent_id])
			goto found;

	ret = -ENOMEM;
	goto out;

found:
	if (ureq.mgmt_class) {
		req.mgmt_class         = ureq.mgmt_class;
		req.mgmt_class_version = ureq.mgmt_class_version;
		memcpy(req.method_mask, ureq.method_mask, sizeof req.method_mask);
		memcpy(req.oui,         ureq.oui,         sizeof req.oui);
	}

	agent = ib_register_mad_agent(file->port->ib_dev, file->port->port_num,
				      ureq.qpn ? IB_QPT_GSI : IB_QPT_SMI,
				      ureq.mgmt_class ? &req : NULL,
				      ureq.rmpp_version,
				      send_handler, recv_handler, file);
	if (IS_ERR(agent)) {
		ret = PTR_ERR(agent);
		goto out;
	}

	file->agent[agent_id] = agent;

	file->mr[agent_id] = ib_get_dma_mr(agent->qp->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(file->mr[agent_id])) {
		ret = -ENOMEM;
		goto err;
	}

	if (put_user(agent_id,
		     (u32 __user *) (arg + offsetof(struct ib_user_mad_reg_req, id)))) {
		ret = -EFAULT;
		goto err_mr;
	}

	ret = 0;
	goto out;

err_mr:
	ib_dereg_mr(file->mr[agent_id]);

err:
	file->agent[agent_id] = NULL;
	ib_unregister_mad_agent(agent);

out:
	up_write(&file->port->mutex);
	return ret;
}

static int ib_umad_unreg_agent(struct ib_umad_file *file, unsigned long arg)
{
	u32 id;
	int ret = 0;

	down_write(&file->port->mutex);

	if (get_user(id, (u32 __user *) arg)) {
		ret = -EFAULT;
		goto out;
	}

	if (id < 0 || id >= IB_UMAD_MAX_AGENTS || !file->agent[id]) {
		ret = -EINVAL;
		goto out;
	}

	ib_dereg_mr(file->mr[id]);
	ib_unregister_mad_agent(file->agent[id]);
	file->agent[id] = NULL;

out:
	up_write(&file->port->mutex);
	return ret;
}

static long ib_umad_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	switch (cmd) {
	case IB_USER_MAD_REGISTER_AGENT:
		return ib_umad_reg_agent(filp->private_data, arg);
	case IB_USER_MAD_UNREGISTER_AGENT:
		return ib_umad_unreg_agent(filp->private_data, arg);
	default:
		return -ENOIOCTLCMD;
	}
}

static int ib_umad_open(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port;
	struct ib_umad_file *file;
	int ret = 0;

	spin_lock(&port_lock);
	port = umad_port[iminor(inode) - IB_UMAD_MINOR_BASE];
	if (port)
		kref_get(&port->umad_dev->ref);
	spin_unlock(&port_lock);

	if (!port)
		return -ENXIO;

	down_write(&port->mutex);

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

	spin_lock_init(&file->recv_lock);
	INIT_LIST_HEAD(&file->recv_list);
	init_waitqueue_head(&file->recv_wait);

	file->port = port;
	filp->private_data = file;

	list_add_tail(&file->port_list, &port->file_list);

out:
	up_write(&port->mutex);
	return ret;
}

static int ib_umad_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_device *dev = file->port->umad_dev;
	struct ib_umad_packet *packet, *tmp;
	int i;

	down_write(&file->port->mutex);
	for (i = 0; i < IB_UMAD_MAX_AGENTS; ++i)
		if (file->agent[i]) {
			ib_dereg_mr(file->mr[i]);
			ib_unregister_mad_agent(file->agent[i]);
		}

	list_for_each_entry_safe(packet, tmp, &file->recv_list, list)
		kfree(packet);

	list_del(&file->port_list);
	up_write(&file->port->mutex);

	kfree(file);

	kref_put(&dev->ref, ib_umad_release_dev);

	return 0;
}

static struct file_operations umad_fops = {
	.owner 	 	= THIS_MODULE,
	.read 	 	= ib_umad_read,
	.write 	 	= ib_umad_write,
	.poll 	 	= ib_umad_poll,
	.unlocked_ioctl = ib_umad_ioctl,
	.compat_ioctl 	= ib_umad_ioctl,
	.open 	 	= ib_umad_open,
	.release 	= ib_umad_close
};

static int ib_umad_sm_open(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port;
	struct ib_port_modify props = {
		.set_port_cap_mask = IB_PORT_SM
	};
	int ret;

	spin_lock(&port_lock);
	port = umad_port[iminor(inode) - IB_UMAD_MINOR_BASE - IB_UMAD_MAX_PORTS];
	if (port)
		kref_get(&port->umad_dev->ref);
	spin_unlock(&port_lock);

	if (!port)
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

	return 0;

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

	down_write(&port->mutex);
	if (port->ib_dev)
		ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	up_write(&port->mutex);

	up(&port->sm_sem);

	kref_put(&port->umad_dev->ref, ib_umad_release_dev);

	return ret;
}

static struct file_operations umad_sm_fops = {
	.owner 	 = THIS_MODULE,
	.open 	 = ib_umad_sm_open,
	.release = ib_umad_sm_close
};

static struct ib_client umad_client = {
	.name   = "umad",
	.add    = ib_umad_add_one,
	.remove = ib_umad_remove_one
};

static ssize_t show_ibdev(struct class_device *class_dev, char *buf)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	if (!port)
		return -ENODEV;

	return sprintf(buf, "%s\n", port->ib_dev->name);
}
static CLASS_DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_port(struct class_device *class_dev, char *buf)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	if (!port)
		return -ENODEV;

	return sprintf(buf, "%d\n", port->port_num);
}
static CLASS_DEVICE_ATTR(port, S_IRUGO, show_port, NULL);

static ssize_t show_abi_version(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", IB_USER_MAD_ABI_VERSION);
}
static CLASS_ATTR(abi_version, S_IRUGO, show_abi_version, NULL);

static int ib_umad_init_port(struct ib_device *device, int port_num,
			     struct ib_umad_port *port)
{
	spin_lock(&port_lock);
	port->dev_num = find_first_zero_bit(dev_map, IB_UMAD_MAX_PORTS);
	if (port->dev_num >= IB_UMAD_MAX_PORTS) {
		spin_unlock(&port_lock);
		return -1;
	}
	set_bit(port->dev_num, dev_map);
	spin_unlock(&port_lock);

	port->ib_dev   = device;
	port->port_num = port_num;
	init_MUTEX(&port->sm_sem);
	init_rwsem(&port->mutex);
	INIT_LIST_HEAD(&port->file_list);

	port->dev = cdev_alloc();
	if (!port->dev)
		return -1;
	port->dev->owner = THIS_MODULE;
	port->dev->ops   = &umad_fops;
	kobject_set_name(&port->dev->kobj, "umad%d", port->dev_num);
	if (cdev_add(port->dev, base_dev + port->dev_num, 1))
		goto err_cdev;

	port->class_dev = class_device_create(umad_class, NULL, port->dev->dev,
					      device->dma_device,
					      "umad%d", port->dev_num);
	if (IS_ERR(port->class_dev))
		goto err_cdev;

	if (class_device_create_file(port->class_dev, &class_device_attr_ibdev))
		goto err_class;
	if (class_device_create_file(port->class_dev, &class_device_attr_port))
		goto err_class;

	port->sm_dev = cdev_alloc();
	if (!port->sm_dev)
		goto err_class;
	port->sm_dev->owner = THIS_MODULE;
	port->sm_dev->ops   = &umad_sm_fops;
	kobject_set_name(&port->dev->kobj, "issm%d", port->dev_num);
	if (cdev_add(port->sm_dev, base_dev + port->dev_num + IB_UMAD_MAX_PORTS, 1))
		goto err_sm_cdev;

	port->sm_class_dev = class_device_create(umad_class, NULL, port->sm_dev->dev,
						 device->dma_device,
						 "issm%d", port->dev_num);
	if (IS_ERR(port->sm_class_dev))
		goto err_sm_cdev;

	class_set_devdata(port->class_dev,    port);
	class_set_devdata(port->sm_class_dev, port);

	if (class_device_create_file(port->sm_class_dev, &class_device_attr_ibdev))
		goto err_sm_class;
	if (class_device_create_file(port->sm_class_dev, &class_device_attr_port))
		goto err_sm_class;

	spin_lock(&port_lock);
	umad_port[port->dev_num] = port;
	spin_unlock(&port_lock);

	return 0;

err_sm_class:
	class_device_destroy(umad_class, port->sm_dev->dev);

err_sm_cdev:
	cdev_del(port->sm_dev);

err_class:
	class_device_destroy(umad_class, port->dev->dev);

err_cdev:
	cdev_del(port->dev);
	clear_bit(port->dev_num, dev_map);

	return -1;
}

static void ib_umad_kill_port(struct ib_umad_port *port)
{
	struct ib_umad_file *file;
	int id;

	class_set_devdata(port->class_dev,    NULL);
	class_set_devdata(port->sm_class_dev, NULL);

	class_device_destroy(umad_class, port->dev->dev);
	class_device_destroy(umad_class, port->sm_dev->dev);

	cdev_del(port->dev);
	cdev_del(port->sm_dev);

	spin_lock(&port_lock);
	umad_port[port->dev_num] = NULL;
	spin_unlock(&port_lock);

	down_write(&port->mutex);

	port->ib_dev = NULL;

	list_for_each_entry(file, &port->file_list, port_list)
		for (id = 0; id < IB_UMAD_MAX_AGENTS; ++id) {
			if (!file->agent[id])
				continue;
			ib_dereg_mr(file->mr[id]);
			ib_unregister_mad_agent(file->agent[id]);
			file->agent[id] = NULL;
		}

	up_write(&port->mutex);

	clear_bit(port->dev_num, dev_map);
}

static void ib_umad_add_one(struct ib_device *device)
{
	struct ib_umad_device *umad_dev;
	int s, e, i;

	if (device->node_type == IB_NODE_SWITCH)
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
		ib_umad_kill_port(&umad_dev->port[i]);

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

	ret = class_create_file(umad_class, &class_attr_abi_version);
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
}

module_init(ib_umad_init);
module_exit(ib_umad_cleanup);
