/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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
 * $Id: user_mad.c 1389 2004-12-27 22:56:47Z roland $
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

#include <ib_mad.h>
#include <ib_user_mad.h>

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
	struct ib_user_mad mad;
	struct ib_ah      *ah;
	struct list_head   list;
	DECLARE_PCI_UNMAP_ADDR(mapping)
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
	for (packet->mad.id = 0;
	     packet->mad.id < IB_UMAD_MAX_AGENTS;
	     packet->mad.id++)
		if (agent == file->agent[packet->mad.id]) {
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
	struct ib_umad_packet *packet =
		(void *) (unsigned long) send_wc->wr_id;

	dma_unmap_single(agent->device->dma_device,
			 pci_unmap_addr(packet, mapping),
			 sizeof packet->mad.data,
			 DMA_TO_DEVICE);
	ib_destroy_ah(packet->ah);

	if (send_wc->status == IB_WC_RESP_TIMEOUT_ERR) {
		packet->mad.status = ETIMEDOUT;

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
		goto out;

	packet = kmalloc(sizeof *packet, GFP_KERNEL);
	if (!packet)
		goto out;

	memset(packet, 0, sizeof *packet);

	memcpy(packet->mad.data, mad_recv_wc->recv_buf.mad, sizeof packet->mad.data);
	packet->mad.status        = 0;
	packet->mad.qpn 	  = cpu_to_be32(mad_recv_wc->wc->src_qp);
	packet->mad.lid 	  = cpu_to_be16(mad_recv_wc->wc->slid);
	packet->mad.sl  	  = mad_recv_wc->wc->sl;
	packet->mad.path_bits 	  = mad_recv_wc->wc->dlid_path_bits;
	packet->mad.grh_present   = !!(mad_recv_wc->wc->wc_flags & IB_WC_GRH);
	if (packet->mad.grh_present) {
		/* XXX parse GRH */
		packet->mad.gid_index 	  = 0;
		packet->mad.hop_limit 	  = 0;
		packet->mad.traffic_class = 0;
		memset(packet->mad.gid, 0, 16);
		packet->mad.flow_label 	  = 0;
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

	if (count < sizeof (struct ib_user_mad))
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

	if (copy_to_user(buf, &packet->mad, sizeof packet->mad))
		ret = -EFAULT;
	else
		ret = sizeof packet->mad;

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
	struct ib_sge      gather_list;
	struct ib_send_wr *bad_wr, wr = {
		.opcode      = IB_WR_SEND,
		.sg_list     = &gather_list,
		.num_sge     = 1,
		.send_flags  = IB_SEND_SIGNALED,
	};
	u8 method;
	u64 *tid;
	int ret;

	if (count < sizeof (struct ib_user_mad))
		return -EINVAL;

	packet = kmalloc(sizeof *packet, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	if (copy_from_user(&packet->mad, buf, sizeof packet->mad)) {
		kfree(packet);
		return -EFAULT;
	}

	if (packet->mad.id < 0 || packet->mad.id >= IB_UMAD_MAX_AGENTS) {
		ret = -EINVAL;
		goto err;
	}

	down_read(&file->agent_mutex);

	agent = file->agent[packet->mad.id];
	if (!agent) {
		ret = -EINVAL;
		goto err_up;
	}

	/*
	 * If userspace is generating a request that will generate a
	 * response, we need to make sure the high-order part of the
	 * transaction ID matches the agent being used to send the
	 * MAD.
	 */
	method = ((struct ib_mad_hdr *) packet->mad.data)->method;

	if (!(method & IB_MGMT_METHOD_RESP)       &&
	    method != IB_MGMT_METHOD_TRAP_REPRESS &&
	    method != IB_MGMT_METHOD_SEND) {
		tid = &((struct ib_mad_hdr *) packet->mad.data)->tid;
		*tid = cpu_to_be64(((u64) agent->hi_tid) << 32 |
				   (be64_to_cpup(tid) & 0xffffffff));
	}

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid          = be16_to_cpu(packet->mad.lid);
	ah_attr.sl            = packet->mad.sl;
	ah_attr.src_path_bits = packet->mad.path_bits;
	ah_attr.port_num      = file->port->port_num;
	if (packet->mad.grh_present) {
		ah_attr.ah_flags = IB_AH_GRH;
		memcpy(ah_attr.grh.dgid.raw, packet->mad.gid, 16);
		ah_attr.grh.flow_label 	   = packet->mad.flow_label;
		ah_attr.grh.hop_limit  	   = packet->mad.hop_limit;
		ah_attr.grh.traffic_class  = packet->mad.traffic_class;
	}

	packet->ah = ib_create_ah(agent->qp->pd, &ah_attr);
	if (IS_ERR(packet->ah)) {
		ret = PTR_ERR(packet->ah);
		goto err_up;
	}

	gather_list.addr = dma_map_single(agent->device->dma_device,
					  packet->mad.data,
					  sizeof packet->mad.data,
					  DMA_TO_DEVICE);
	gather_list.length = sizeof packet->mad.data;
	gather_list.lkey   = file->mr[packet->mad.id]->lkey;
	pci_unmap_addr_set(packet, mapping, gather_list.addr);

	wr.wr.ud.mad_hdr     = (struct ib_mad_hdr *) packet->mad.data;
	wr.wr.ud.ah          = packet->ah;
	wr.wr.ud.remote_qpn  = be32_to_cpu(packet->mad.qpn);
	wr.wr.ud.remote_qkey = be32_to_cpu(packet->mad.qkey);
	wr.wr.ud.timeout_ms  = packet->mad.timeout_ms;

	wr.wr_id            = (unsigned long) packet;

	ret = ib_post_send_mad(agent, &wr, &bad_wr);
	if (ret) {
		dma_unmap_single(agent->device->dma_device,
				 pci_unmap_addr(packet, mapping),
				 sizeof packet->mad.data,
				 DMA_TO_DEVICE);
		goto err_up;
	}

	up_read(&file->agent_mutex);

	return sizeof packet->mad;

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
	req.mgmt_class         = ureq.mgmt_class;
	req.mgmt_class_version = ureq.mgmt_class_version;
	memcpy(req.method_mask, ureq.method_mask, sizeof req.method_mask);
	memcpy(req.oui,         ureq.oui,         sizeof req.oui);

	agent = ib_register_mad_agent(file->port->ib_dev, file->port->port_num,
				      ureq.qpn ? IB_QPT_GSI : IB_QPT_SMI,
				      &req, 0, send_handler, recv_handler,
				      file);
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

static long ib_umad_ioctl(struct file *filp,
			 unsigned int cmd, unsigned long arg)
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
	int i;

	for (i = 0; i < IB_UMAD_MAX_AGENTS; ++i)
		if (file->agent[i]) {
			ib_dereg_mr(file->mr[i]);
			ib_unregister_mad_agent(file->agent[i]);
		}

	kfree(file);

	return 0;
}

static struct file_operations umad_fops = {
	.owner 	        = THIS_MODULE,
	.read 	        = ib_umad_read,
	.write 	        = ib_umad_write,
	.poll 	        = ib_umad_poll,
	.unlocked_ioctl = ib_umad_ioctl,
	.compat_ioctl   = ib_umad_ioctl,
	.open 	        = ib_umad_open,
	.release        = ib_umad_close
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
