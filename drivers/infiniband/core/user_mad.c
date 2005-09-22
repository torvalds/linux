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

struct ib_umad_port {
	int                    devnum;
	struct cdev            dev;
	struct class_device    class_dev;

	int                    sm_devnum;
	struct cdev            sm_dev;
	struct class_device    sm_class_dev;
	struct semaphore       sm_sem;

	struct ib_device      *ib_dev;
	struct ib_umad_device *umad_dev;
	u8                     port_num;
};

struct ib_umad_device {
	int                  start_port, end_port;
	struct kref          ref;
	struct ib_umad_port  port[0];
};

struct ib_umad_file {
	struct ib_umad_port *port;
	spinlock_t           recv_lock;
	struct list_head     recv_list;
	wait_queue_head_t    recv_wait;
	struct rw_semaphore  agent_mutex;
	struct ib_mad_agent *agent[IB_UMAD_MAX_AGENTS];
	struct ib_mr        *mr[IB_UMAD_MAX_AGENTS];
};

struct ib_umad_packet {
	struct ib_ah      *ah;
	struct ib_mad_send_buf *msg;
	struct list_head   list;
	int		   length;
	DECLARE_PCI_UNMAP_ADDR(mapping)
	struct ib_user_mad mad;
};

static const dev_t base_dev = MKDEV(IB_UMAD_MAJOR, IB_UMAD_MINOR_BASE);
static spinlock_t map_lock;
static DECLARE_BITMAP(dev_map, IB_UMAD_MAX_PORTS * 2);

static void ib_umad_add_one(struct ib_device *device);
static void ib_umad_remove_one(struct ib_device *device);

static int queue_packet(struct ib_umad_file *file,
			struct ib_mad_agent *agent,
			struct ib_umad_packet *packet)
{
	int ret = 1;

	down_read(&file->agent_mutex);
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

	up_read(&file->agent_mutex);

	return ret;
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *send_wc)
{
	struct ib_umad_file *file = agent->context;
	struct ib_umad_packet *timeout, *packet =
		(void *) (unsigned long) send_wc->wr_id;

	ib_destroy_ah(packet->msg->send_wr.wr.ud.ah);
	ib_free_send_mad(packet->msg);

	if (send_wc->status == IB_WC_RESP_TIMEOUT_ERR) {
		timeout = kmalloc(sizeof *timeout + sizeof (struct ib_mad_hdr),
				  GFP_KERNEL);
		if (!timeout)
			goto out;

		memset(timeout, 0, sizeof *timeout + sizeof (struct ib_mad_hdr));

		timeout->length = sizeof (struct ib_mad_hdr);
		timeout->mad.hdr.id = packet->mad.hdr.id;
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
	packet = kmalloc(sizeof *packet + length, GFP_KERNEL);
	if (!packet)
		goto out;

	memset(packet, 0, sizeof *packet + length);
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
	struct ib_send_wr *bad_wr;
	struct ib_rmpp_mad *rmpp_mad;
	u8 method;
	__be64 *tid;
	int ret, length, hdr_len, data_len, rmpp_hdr_size;
	int rmpp_active = 0;

	if (count < sizeof (struct ib_user_mad))
		return -EINVAL;

	length = count - sizeof (struct ib_user_mad);
	packet = kmalloc(sizeof *packet + sizeof(struct ib_mad_hdr) +
			 sizeof(struct ib_rmpp_hdr), GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(&packet->mad, buf,
			    sizeof (struct ib_user_mad) +
			    sizeof(struct ib_mad_hdr) +
			    sizeof(struct ib_rmpp_hdr))) {
		ret = -EFAULT;
		goto err;
	}

	if (packet->mad.hdr.id < 0 ||
	    packet->mad.hdr.id >= IB_UMAD_MAX_AGENTS) {
		ret = -EINVAL;
		goto err;
	}

	packet->length = length;

	down_read(&file->agent_mutex);

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

	packet->ah = ib_create_ah(agent->qp->pd, &ah_attr);
	if (IS_ERR(packet->ah)) {
		ret = PTR_ERR(packet->ah);
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
			hdr_len = offsetof(struct ib_sa_mad, data);
			data_len = length - hdr_len;
		} else if ((rmpp_mad->mad_hdr.mgmt_class >= IB_MGMT_CLASS_VENDOR_RANGE2_START) &&
			    (rmpp_mad->mad_hdr.mgmt_class <= IB_MGMT_CLASS_VENDOR_RANGE2_END)) {
				hdr_len = offsetof(struct ib_vendor_mad, data);
				data_len = length - hdr_len;
		} else {
			ret = -EINVAL;
			goto err_ah;
		}
		rmpp_active = 1;
	} else {
		if (length > sizeof(struct ib_mad)) {
			ret = -EINVAL;
			goto err_ah;
		}
		hdr_len = offsetof(struct ib_mad, data);
		data_len = length - hdr_len;
	}

	packet->msg = ib_create_send_mad(agent,
					 be32_to_cpu(packet->mad.hdr.qpn),
					 0, packet->ah, rmpp_active,
					 hdr_len, data_len,
					 GFP_KERNEL);
	if (IS_ERR(packet->msg)) {
		ret = PTR_ERR(packet->msg);
		goto err_ah;
	}

	packet->msg->send_wr.wr.ud.timeout_ms  = packet->mad.hdr.timeout_ms;
	packet->msg->send_wr.wr.ud.retries = packet->mad.hdr.retries;

	/* Override send WR WRID initialized in ib_create_send_mad */
	packet->msg->send_wr.wr_id = (unsigned long) packet;

	if (!rmpp_active) {
		/* Copy message from user into send buffer */
		if (copy_from_user(packet->msg->mad,
				   buf + sizeof(struct ib_user_mad), length)) {
			ret = -EFAULT;
			goto err_msg;
		}
	} else {
		rmpp_hdr_size = sizeof(struct ib_mad_hdr) +
				sizeof(struct ib_rmpp_hdr);

		/* Only copy MAD headers (RMPP header in place) */
		memcpy(packet->msg->mad, packet->mad.data,
		       sizeof(struct ib_mad_hdr));

		/* Now, copy rest of message from user into send buffer */
		if (copy_from_user(((struct ib_rmpp_mad *) packet->msg->mad)->data,
				   buf + sizeof (struct ib_user_mad) + rmpp_hdr_size,
				   length - rmpp_hdr_size)) {
			ret = -EFAULT;
			goto err_msg;
		}
	}

	/*
	 * If userspace is generating a request that will generate a
	 * response, we need to make sure the high-order part of the
	 * transaction ID matches the agent being used to send the
	 * MAD.
	 */
	method = packet->msg->mad->mad_hdr.method;

	if (!(method & IB_MGMT_METHOD_RESP)       &&
	    method != IB_MGMT_METHOD_TRAP_REPRESS &&
	    method != IB_MGMT_METHOD_SEND) {
		tid = &packet->msg->mad->mad_hdr.tid;
		*tid = cpu_to_be64(((u64) agent->hi_tid) << 32 |
				   (be64_to_cpup(tid) & 0xffffffff));
	}

	ret = ib_post_send_mad(agent, &packet->msg->send_wr, &bad_wr);
	if (ret)
		goto err_msg;

	up_read(&file->agent_mutex);

	return sizeof (struct ib_user_mad_hdr) + packet->length;

err_msg:
	ib_free_send_mad(packet->msg);

err_ah:
	ib_destroy_ah(packet->ah);

err_up:
	up_read(&file->agent_mutex);

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

	down_write(&file->agent_mutex);

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
	up_write(&file->agent_mutex);
	return ret;
}

static int ib_umad_unreg_agent(struct ib_umad_file *file, unsigned long arg)
{
	u32 id;
	int ret = 0;

	down_write(&file->agent_mutex);

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
	up_write(&file->agent_mutex);
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
	struct ib_umad_port *port =
		container_of(inode->i_cdev, struct ib_umad_port, dev);
	struct ib_umad_file *file;

	file = kmalloc(sizeof *file, GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	memset(file, 0, sizeof *file);

	spin_lock_init(&file->recv_lock);
	init_rwsem(&file->agent_mutex);
	INIT_LIST_HEAD(&file->recv_list);
	init_waitqueue_head(&file->recv_wait);

	file->port = port;
	filp->private_data = file;

	return 0;
}

static int ib_umad_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_file *file = filp->private_data;
	struct ib_umad_packet *packet, *tmp;
	int i;

	for (i = 0; i < IB_UMAD_MAX_AGENTS; ++i)
		if (file->agent[i]) {
			ib_dereg_mr(file->mr[i]);
			ib_unregister_mad_agent(file->agent[i]);
		}

	list_for_each_entry_safe(packet, tmp, &file->recv_list, list)
		kfree(packet);

	kfree(file);

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
	struct ib_umad_port *port =
		container_of(inode->i_cdev, struct ib_umad_port, sm_dev);
	struct ib_port_modify props = {
		.set_port_cap_mask = IB_PORT_SM
	};
	int ret;

	if (filp->f_flags & O_NONBLOCK) {
		if (down_trylock(&port->sm_sem))
			return -EAGAIN;
	} else {
		if (down_interruptible(&port->sm_sem))
			return -ERESTARTSYS;
	}

	ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	if (ret) {
		up(&port->sm_sem);
		return ret;
	}

	filp->private_data = port;

	return 0;
}

static int ib_umad_sm_close(struct inode *inode, struct file *filp)
{
	struct ib_umad_port *port = filp->private_data;
	struct ib_port_modify props = {
		.clr_port_cap_mask = IB_PORT_SM
	};
	int ret;

	ret = ib_modify_port(port->ib_dev, port->port_num, 0, &props);
	up(&port->sm_sem);

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

static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	if (class_dev == &port->class_dev)
		return print_dev_t(buf, port->dev.dev);
	else
		return print_dev_t(buf, port->sm_dev.dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);

static ssize_t show_ibdev(struct class_device *class_dev, char *buf)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	return sprintf(buf, "%s\n", port->ib_dev->name);
}
static CLASS_DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_port(struct class_device *class_dev, char *buf)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	return sprintf(buf, "%d\n", port->port_num);
}
static CLASS_DEVICE_ATTR(port, S_IRUGO, show_port, NULL);

static void ib_umad_release_dev(struct kref *ref)
{
	struct ib_umad_device *dev =
		container_of(ref, struct ib_umad_device, ref);

	kfree(dev);
}

static void ib_umad_release_port(struct class_device *class_dev)
{
	struct ib_umad_port *port = class_get_devdata(class_dev);

	if (class_dev == &port->class_dev) {
		cdev_del(&port->dev);
		clear_bit(port->devnum, dev_map);
	} else {
		cdev_del(&port->sm_dev);
		clear_bit(port->sm_devnum, dev_map);
	}

	kref_put(&port->umad_dev->ref, ib_umad_release_dev);
}

static struct class umad_class = {
	.name    = "infiniband_mad",
	.release = ib_umad_release_port
};

static ssize_t show_abi_version(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", IB_USER_MAD_ABI_VERSION);
}
static CLASS_ATTR(abi_version, S_IRUGO, show_abi_version, NULL);

static int ib_umad_init_port(struct ib_device *device, int port_num,
			     struct ib_umad_port *port)
{
	spin_lock(&map_lock);
	port->devnum = find_first_zero_bit(dev_map, IB_UMAD_MAX_PORTS);
	if (port->devnum >= IB_UMAD_MAX_PORTS) {
		spin_unlock(&map_lock);
		return -1;
	}
	port->sm_devnum = find_next_zero_bit(dev_map, IB_UMAD_MAX_PORTS * 2, IB_UMAD_MAX_PORTS);
	if (port->sm_devnum >= IB_UMAD_MAX_PORTS * 2) {
		spin_unlock(&map_lock);
		return -1;
	}
	set_bit(port->devnum, dev_map);
	set_bit(port->sm_devnum, dev_map);
	spin_unlock(&map_lock);

	port->ib_dev   = device;
	port->port_num = port_num;
	init_MUTEX(&port->sm_sem);

	cdev_init(&port->dev, &umad_fops);
	port->dev.owner = THIS_MODULE;
	kobject_set_name(&port->dev.kobj, "umad%d", port->devnum);
	if (cdev_add(&port->dev, base_dev + port->devnum, 1))
		return -1;

	port->class_dev.class = &umad_class;
	port->class_dev.dev   = device->dma_device;

	snprintf(port->class_dev.class_id, BUS_ID_SIZE, "umad%d", port->devnum);

	if (class_device_register(&port->class_dev))
		goto err_cdev;

	class_set_devdata(&port->class_dev, port);
	kref_get(&port->umad_dev->ref);

	if (class_device_create_file(&port->class_dev, &class_device_attr_dev))
		goto err_class;
	if (class_device_create_file(&port->class_dev, &class_device_attr_ibdev))
		goto err_class;
	if (class_device_create_file(&port->class_dev, &class_device_attr_port))
		goto err_class;

	cdev_init(&port->sm_dev, &umad_sm_fops);
	port->sm_dev.owner = THIS_MODULE;
	kobject_set_name(&port->dev.kobj, "issm%d", port->sm_devnum - IB_UMAD_MAX_PORTS);
	if (cdev_add(&port->sm_dev, base_dev + port->sm_devnum, 1))
		return -1;

	port->sm_class_dev.class = &umad_class;
	port->sm_class_dev.dev   = device->dma_device;

	snprintf(port->sm_class_dev.class_id, BUS_ID_SIZE, "issm%d", port->sm_devnum - IB_UMAD_MAX_PORTS);

	if (class_device_register(&port->sm_class_dev))
		goto err_sm_cdev;

	class_set_devdata(&port->sm_class_dev, port);
	kref_get(&port->umad_dev->ref);

	if (class_device_create_file(&port->sm_class_dev, &class_device_attr_dev))
		goto err_sm_class;
	if (class_device_create_file(&port->sm_class_dev, &class_device_attr_ibdev))
		goto err_sm_class;
	if (class_device_create_file(&port->sm_class_dev, &class_device_attr_port))
		goto err_sm_class;

	return 0;

err_sm_class:
	class_device_unregister(&port->sm_class_dev);

err_sm_cdev:
	cdev_del(&port->sm_dev);

err_class:
	class_device_unregister(&port->class_dev);

err_cdev:
	cdev_del(&port->dev);
	clear_bit(port->devnum, dev_map);

	return -1;
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

	umad_dev = kmalloc(sizeof *umad_dev +
			   (e - s + 1) * sizeof (struct ib_umad_port),
			   GFP_KERNEL);
	if (!umad_dev)
		return;

	memset(umad_dev, 0, sizeof *umad_dev +
	       (e - s + 1) * sizeof (struct ib_umad_port));

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
	while (--i >= s) {
		class_device_unregister(&umad_dev->port[i - s].class_dev);
		class_device_unregister(&umad_dev->port[i - s].sm_class_dev);
	}

	kref_put(&umad_dev->ref, ib_umad_release_dev);
}

static void ib_umad_remove_one(struct ib_device *device)
{
	struct ib_umad_device *umad_dev = ib_get_client_data(device, &umad_client);
	int i;

	if (!umad_dev)
		return;

	for (i = 0; i <= umad_dev->end_port - umad_dev->start_port; ++i) {
		class_device_unregister(&umad_dev->port[i].class_dev);
		class_device_unregister(&umad_dev->port[i].sm_class_dev);
	}

	kref_put(&umad_dev->ref, ib_umad_release_dev);
}

static int __init ib_umad_init(void)
{
	int ret;

	spin_lock_init(&map_lock);

	ret = register_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2,
				     "infiniband_mad");
	if (ret) {
		printk(KERN_ERR "user_mad: couldn't register device number\n");
		goto out;
	}

	ret = class_register(&umad_class);
	if (ret) {
		printk(KERN_ERR "user_mad: couldn't create class infiniband_mad\n");
		goto out_chrdev;
	}

	ret = class_create_file(&umad_class, &class_attr_abi_version);
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
	class_unregister(&umad_class);

out_chrdev:
	unregister_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2);

out:
	return ret;
}

static void __exit ib_umad_cleanup(void)
{
	ib_unregister_client(&umad_client);
	class_unregister(&umad_class);
	unregister_chrdev_region(base_dev, IB_UMAD_MAX_PORTS * 2);
}

module_init(ib_umad_init);
module_exit(ib_umad_cleanup);
