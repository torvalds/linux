// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/usb/usb_ctrl_qti.h>
#include <linux/cdev.h>
#include <linux/debugfs.h>

#include <soc/qcom/bam_dmux.h>

#include "u_rmnet.h"
#include "f_qdss.h"

#define RMNET_CTRL_QTI_NAME "rmnet_ctrl"
#define DPL_CTRL_QTI_NAME "dpl_ctrl"
#define MODULE_NAME "usb_ctrl"
/*
 * Use size of gadget's qti control name. Here currently RMNET and DPL
 * gadget is using QTI as control transport. Hence using RMNET ctrl name
 * (as it is bigger in size) for QTI_CTRL_NAME_LEN.
 */
#define QTI_CTRL_NAME_LEN (sizeof(RMNET_CTRL_QTI_NAME)+2)

static struct class *rmnet_class;
static dev_t qti_ctrl_dev;

struct qti_ctrl_port {
	void		*port_usb;
	char		name[QTI_CTRL_NAME_LEN];
	struct cdev	ctrl_device;
	struct device	*dev;

	bool		is_open;
	int index;
	unsigned int	intf;
	int		ipa_prod_idx;
	int		ipa_cons_idx;
	enum peripheral_ep_type	ep_type;

	atomic_t	connected;
	atomic_t	line_state;

	atomic_t	open_excl;
	atomic_t	read_excl;
	atomic_t	write_excl;
	atomic_t	ioctl_excl;

	wait_queue_head_t	read_wq;

	struct list_head	cpkt_req_q;

	spinlock_t	lock;
	enum qti_port_type	port_type;
	unsigned int	host_to_modem;
	unsigned int	copied_to_modem;
	unsigned int	copied_from_modem;
	unsigned int	modem_to_host;
	unsigned int	drp_cpkt_cnt;
};
static struct qti_ctrl_port *ctrl_port[QTI_NUM_PORTS];

static inline int qti_ctrl_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1)
		return 0;
	atomic_dec(excl);
	return -EBUSY;
}

static inline void qti_ctrl_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

static struct
rmnet_ctrl_pkt *alloc_rmnet_ctrl_pkt(unsigned int len, gfp_t flags)
{
	struct rmnet_ctrl_pkt *pkt;

	pkt = kzalloc(sizeof(struct rmnet_ctrl_pkt), flags);
	if (!pkt)
		return ERR_PTR(-ENOMEM);

	pkt->buf = kmalloc(len, flags);
	if (!pkt->buf) {
		kfree(pkt);
		return ERR_PTR(-ENOMEM);
	}

	pkt->len = len;

	return pkt;
}

static void free_rmnet_ctrl_pkt(struct rmnet_ctrl_pkt *pkt)
{
	kfree(pkt->buf);
	kfree(pkt);
}


static void qti_ctrl_queue_notify(struct qti_ctrl_port *port)
{
	unsigned long		flags;
	struct rmnet_ctrl_pkt	*cpkt = NULL;

	pr_debug("%s: Queue empty packet for QTI for port%d\n",
		 __func__, port->index);

	spin_lock_irqsave(&port->lock, flags);
	if (!port->is_open) {
		pr_err("%s: rmnet ctrl file handler %pK is not open\n",
			   __func__, port);
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}

	cpkt = alloc_rmnet_ctrl_pkt(0, GFP_ATOMIC);
	if (IS_ERR(cpkt)) {
		pr_err("%s: Unable to allocate reset function pkt\n", __func__);
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}

	list_add_tail(&cpkt->list, &port->cpkt_req_q);
	spin_unlock_irqrestore(&port->lock, flags);

	pr_debug("%s: Wake up read queue\n", __func__);
	wake_up(&port->read_wq);
}

static int gqti_ctrl_send_cpkt_tomodem(enum qti_port_type qport,
					void *buf, size_t len)
{
	unsigned long		flags;
	struct qti_ctrl_port	*port;
	struct rmnet_ctrl_pkt *cpkt;

	if (len > MAX_QTI_PKT_SIZE) {
		pr_err("given pkt size too big:%zu > max_pkt_size:%d\n",
				len, MAX_QTI_PKT_SIZE);
		return -EINVAL;
	}

	if (qport >= QTI_NUM_PORTS) {
		pr_err("%s: Invalid QTI port %d\n", __func__, qport);
		return -ENODEV;
	}
	port = ctrl_port[qport];
	cpkt = alloc_rmnet_ctrl_pkt(len, GFP_ATOMIC);
	if (IS_ERR(cpkt)) {
		pr_err("%s: Unable to allocate ctrl pkt\n", __func__);
		return -ENOMEM;
	}

	memcpy(cpkt->buf, buf, len);
	cpkt->len = len;

	pr_debug("%s: port type:%d: Add to cpkt_req_q packet with len = %zu\n",
			__func__, port->port_type, len);
	spin_lock_irqsave(&port->lock, flags);

	/* drop cpkt if port is not open */
	if (!port->is_open) {
		pr_debug("rmnet file handler %pK(index=%d) is not open\n",
		       port, port->index);
		port->drp_cpkt_cnt++;
		spin_unlock_irqrestore(&port->lock, flags);
		free_rmnet_ctrl_pkt(cpkt);
		return 0;
	}

	list_add_tail(&cpkt->list, &port->cpkt_req_q);
	port->host_to_modem++;
	spin_unlock_irqrestore(&port->lock, flags);

	/* wakeup read thread */
	pr_debug("%s: Wake up read queue\n", __func__);
	wake_up(&port->read_wq);

	return 0;
}

static void
gqti_ctrl_notify_modem(void *gptr, enum qti_port_type qport, int val)
{
	struct qti_ctrl_port *port;

	if (qport >= QTI_NUM_PORTS) {
		pr_err("%s: Invalid QTI port %d\n", __func__, qport);
		return;
	}
	port = ctrl_port[qport];
	atomic_set(&port->line_state, val);

	/* send 0 len pkt to qti to notify state change */
	qti_ctrl_queue_notify(port);
}

int gqti_ctrl_connect(void *gr, enum qti_port_type qport, unsigned int intf,
						enum data_xport_type dxport)
{
	struct qti_ctrl_port	*port;
	struct grmnet *g_rmnet = NULL;
	unsigned long flags;

	pr_debug("%s: port type:%d gadget:%pK\n", __func__, qport, gr);
	if (qport >= QTI_NUM_PORTS) {
		pr_err("%s: Invalid QTI port %d\n", __func__, qport);
		return -ENODEV;
	}

	port = ctrl_port[qport];
	if (!port) {
		pr_err("%s: gadget port is null\n", __func__);
		return -ENODEV;
	}

	spin_lock_irqsave(&port->lock, flags);
	port->port_type = qport;
	if (dxport == BAM_DMUX) {
		port->ep_type = DATA_EP_TYPE_BAM_DMUX;
		port->intf = (qport == QTI_PORT_RMNET) ?
			BAM_DMUX_USB_RMNET_0 : BAM_DMUX_USB_DPL;
		port->ipa_prod_idx = 0;
		port->ipa_cons_idx = 0;
	} else {
		port->ep_type = DATA_EP_TYPE_HSUSB;
		port->intf = intf;
	}

	if (gr) {
		port->port_usb = gr;
		g_rmnet = gr;
		g_rmnet->send_encap_cmd = gqti_ctrl_send_cpkt_tomodem;
		g_rmnet->notify_modem = gqti_ctrl_notify_modem;
		if (port->port_type == QTI_PORT_DPL)
			atomic_set(&port->line_state, 1);
	} else {
		spin_unlock_irqrestore(&port->lock, flags);
		pr_err("%s(): Port is used without port type.\n", __func__);
		return -ENODEV;
	}

	port->host_to_modem = 0;
	port->copied_to_modem = 0;
	port->copied_from_modem = 0;
	port->modem_to_host = 0;
	port->drp_cpkt_cnt = 0;

	spin_unlock_irqrestore(&port->lock, flags);

	atomic_set(&port->connected, 1);
	wake_up(&port->read_wq);
	if (port->port_usb && g_rmnet && g_rmnet->connect)
		g_rmnet->connect(port->port_usb);

	return 0;
}

void gqti_ctrl_disconnect(void *gr, enum qti_port_type qport)
{
	struct qti_ctrl_port	*port;
	unsigned long		flags;
	struct rmnet_ctrl_pkt	*cpkt;
	struct grmnet *g_rmnet = NULL;

	pr_debug("%s: gadget:%pK\n", __func__, gr);

	if (qport >= QTI_NUM_PORTS) {
		pr_err("%s: Invalid QTI port %d\n", __func__, qport);
		return;
	}

	port = ctrl_port[qport];
	if (!port) {
		pr_err("%s: gadget port is null\n", __func__);
		return;
	}

	atomic_set(&port->connected, 0);
	atomic_set(&port->line_state, 0);
	spin_lock_irqsave(&port->lock, flags);

	/* reset ipa eps to -1 */
	port->ipa_prod_idx = -1;
	port->ipa_cons_idx = -1;
	port->port_usb = NULL;

	if (gr) {
		g_rmnet = gr;
		g_rmnet->send_encap_cmd = NULL;
		g_rmnet->notify_modem = NULL;
	} else {
		pr_err("%s(): unrecognized gadget type(%d).\n",
					__func__, port->port_type);
	}

	while (!list_empty(&port->cpkt_req_q)) {
		cpkt = list_first_entry(&port->cpkt_req_q,
					struct rmnet_ctrl_pkt, list);

		list_del(&cpkt->list);
		free_rmnet_ctrl_pkt(cpkt);
	}

	spin_unlock_irqrestore(&port->lock, flags);

	/* send 0 len pkt to qti to notify state change */
	qti_ctrl_queue_notify(port);
}

void gqti_ctrl_update_ipa_pipes(void *gr, enum qti_port_type qport,
				u32 ipa_prod, u32 ipa_cons)
{
	struct qti_ctrl_port	*port;

	if (qport >= QTI_NUM_PORTS) {
		pr_err("%s: Invalid QTI port %d\n", __func__, qport);
		return;
	}

	port = ctrl_port[qport];
	port->ipa_prod_idx = ipa_prod;
	port->ipa_cons_idx = ipa_cons;

}


static int qti_ctrl_open(struct inode *ip, struct file *fp)
{
	unsigned long		flags;
	struct qti_ctrl_port *port = container_of(ip->i_cdev,
						struct qti_ctrl_port,
						ctrl_device);

	pr_debug("Open rmnet_ctrl_qti device file name=%s(index=%d)\n",
		port->name, port->index);

	if (qti_ctrl_lock(&port->open_excl)) {
		pr_err("Already opened\n");
		return -EBUSY;
	}

	fp->private_data = port;
	spin_lock_irqsave(&port->lock, flags);
	port->is_open = true;
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int qti_ctrl_release(struct inode *ip, struct file *fp)
{
	unsigned long		flags;
	struct qti_ctrl_port *port = container_of(ip->i_cdev,
						struct qti_ctrl_port,
						ctrl_device);

	pr_debug("Close rmnet control file\n");

	spin_lock_irqsave(&port->lock, flags);
	port->is_open = false;
	spin_unlock_irqrestore(&port->lock, flags);

	qti_ctrl_unlock(&port->open_excl);

	return 0;
}

static ssize_t
qti_ctrl_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct qti_ctrl_port *port = fp->private_data;
	struct rmnet_ctrl_pkt *cpkt = NULL;
	unsigned long flags;
	int ret = 0;

	pr_debug("%s: Enter(%zu)\n", __func__, count);

	if (count > MAX_QTI_PKT_SIZE) {
		pr_err("Buffer size is too big %zu, should be at most %d\n",
			count, MAX_QTI_PKT_SIZE);
		return -EINVAL;
	}

	if (qti_ctrl_lock(&port->read_excl)) {
		pr_err("Previous reading is not finished yet\n");
		return -EBUSY;
	}

	/* block until a new packet is available */
	do {
		spin_lock_irqsave(&port->lock, flags);
		if (!list_empty(&port->cpkt_req_q))
			break;
		spin_unlock_irqrestore(&port->lock, flags);

		pr_debug("%s: Requests list is empty. Wait.\n", __func__);
		ret = wait_event_interruptible(port->read_wq,
					!list_empty(&port->cpkt_req_q));
		if (ret < 0) {
			pr_debug("Waiting failed\n");
			qti_ctrl_unlock(&port->read_excl);
			return -ERESTARTSYS;
		}
	} while (1);

	cpkt = list_first_entry(&port->cpkt_req_q, struct rmnet_ctrl_pkt,
							list);
	list_del(&cpkt->list);
	spin_unlock_irqrestore(&port->lock, flags);

	if (cpkt->len > count) {
		pr_err("cpkt size too big:%d > buf size:%zu\n",
				cpkt->len, count);
		qti_ctrl_unlock(&port->read_excl);
		free_rmnet_ctrl_pkt(cpkt);
		return -ENOMEM;
	}

	pr_debug("%s: cpkt size:%d\n", __func__, cpkt->len);


	qti_ctrl_unlock(&port->read_excl);

	ret = copy_to_user(buf, cpkt->buf, cpkt->len);
	if (ret) {
		pr_err("copy_to_user failed: err %d\n", ret);
		ret = -EFAULT;
	} else {
		pr_debug("%s: copied %d bytes to user\n", __func__, cpkt->len);
		ret = cpkt->len;
		port->copied_to_modem++;
	}

	free_rmnet_ctrl_pkt(cpkt);

	return ret;
}

static ssize_t
qti_ctrl_write(struct file *fp, const char __user *buf, size_t count,
		   loff_t *pos)
{
	struct qti_ctrl_port *port = fp->private_data;
	void *kbuf;
	unsigned long flags;
	int ret = 0;
	struct grmnet *g_rmnet = NULL;

	pr_debug("%s: Enter(%zu) port_index=%d\n", __func__, count, port->index);

	if (!count) {
		pr_debug("zero length ctrl pkt\n");
		return -EINVAL;
	}

	if (count > MAX_QTI_PKT_SIZE) {
		pr_debug("given pkt size too big:%zu > max_pkt_size:%d\n",
				count, MAX_QTI_PKT_SIZE);
		return -EINVAL;
	}

	if (qti_ctrl_lock(&port->write_excl)) {
		pr_err("Previous writing not finished yet\n");
		return -EBUSY;
	}

	if (!atomic_read(&port->connected)) {
		pr_debug("USB cable not connected\n");
		qti_ctrl_unlock(&port->write_excl);
		return -EPIPE;
	}

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf) {
		qti_ctrl_unlock(&port->write_excl);
		return -ENOMEM;
	}
	ret = copy_from_user(kbuf, buf, count);
	if (ret) {
		pr_err("copy_from_user failed err:%d\n", ret);
		kfree(kbuf);
		qti_ctrl_unlock(&port->write_excl);
		return -EFAULT;
	}
	port->copied_from_modem++;

	spin_lock_irqsave(&port->lock, flags);
	if (port->port_usb) {
		if (port->port_type == QTI_PORT_RMNET) {
			g_rmnet = port->port_usb;
		} else {
			spin_unlock_irqrestore(&port->lock, flags);
			pr_err("%s(): unrecognized gadget type(%d).\n",
						__func__, port->port_type);
			return -EINVAL;
		}

		if (g_rmnet && g_rmnet->send_cpkt_response) {
			ret = g_rmnet->send_cpkt_response(port->port_usb,
							kbuf, count);
			if (ret)
				pr_err("%d failed to send ctrl packet.\n", ret);
			port->modem_to_host++;
		} else {
			pr_err("send_cpkt_response callback is NULL\n");
			ret = -EINVAL;
		}
	}

	spin_unlock_irqrestore(&port->lock, flags);
	kfree(kbuf);
	qti_ctrl_unlock(&port->write_excl);

	pr_debug("%s: Exit(%zu)\n", __func__, count);
	return (ret) ? ret : count;
}

static long qti_ctrl_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct qti_ctrl_port *port = fp->private_data;
	struct grmnet *gr = NULL;
	struct ep_info info;
	int val, ret = 0;

	pr_debug("%s: Received command %d for port type:%d\n",
				__func__, cmd, port->port_type);

	if (qti_ctrl_lock(&port->ioctl_excl))
		return -EBUSY;

	switch (cmd) {
	case QTI_CTRL_MODEM_OFFLINE:
		if (port && (port->port_type == QTI_PORT_DPL)) {
			pr_err("%s(): Modem Offline not handled\n", __func__);
			goto exit_ioctl;
		}

		if (port && port->port_usb)
			gr = port->port_usb;

		if (gr && gr->disconnect)
			gr->disconnect(gr);
		break;
	case QTI_CTRL_MODEM_ONLINE:
		if (port && (port->port_type == QTI_PORT_DPL)) {
			pr_err("%s(): Modem Online not handled\n", __func__);
			goto exit_ioctl;
		}

		if (port && port->port_usb)
			gr = port->port_usb;

		if (gr && gr->connect)
			gr->connect(gr);
		break;
	case QTI_CTRL_GET_LINE_STATE:
		val = atomic_read(&port->line_state);
		ret = copy_to_user((void __user *)arg, &val, sizeof(val));
		if (ret) {
			pr_err("copying to user space failed\n");
			ret = -EFAULT;
		}
		pr_debug("%s: Sent line_state: %d for port type:%d\n", __func__,
			atomic_read(&port->line_state), port->port_type);
		break;
	case QTI_CTRL_EP_LOOKUP:

		pr_debug("%s(): EP_LOOKUP for port type:%d\n", __func__,
							port->port_type);
		val = atomic_read(&port->connected);
		if (!val) {
			pr_err_ratelimited("EP_LOOKUP failed: not connected\n");
			ret = -EAGAIN;
			break;
		}

		if (port->ipa_prod_idx == -1 && port->ipa_cons_idx == -1) {
			pr_err_ratelimited("EP_LOOKUP ipa pipes not updated\n");
			ret = -EAGAIN;
			break;
		}

		info.ph_ep_info.ep_type = port->ep_type;
		info.ph_ep_info.peripheral_iface_id = port->intf;
		info.ipa_ep_pair.cons_pipe_num = port->ipa_cons_idx;
		info.ipa_ep_pair.prod_pipe_num = port->ipa_prod_idx;

		pr_debug("%s(): port type:%d ep_type:%d intf:%d\n",
			__func__, port->port_type, info.ph_ep_info.ep_type,
			info.ph_ep_info.peripheral_iface_id);

		pr_debug("%s(): ipa_cons_idx:%d ipa_prod_idx:%d\n",
				__func__, info.ipa_ep_pair.cons_pipe_num,
				info.ipa_ep_pair.prod_pipe_num);

		ret = copy_to_user((void __user *)arg, &info,
			sizeof(info));
		if (ret) {
			pr_err("copying to user space failed\n");
			ret = -EFAULT;
		}
		break;
	default:
		pr_err("wrong parameter\n");
		ret = -EINVAL;
	}

exit_ioctl:
	qti_ctrl_unlock(&port->ioctl_excl);

	return ret;
}

static unsigned int qti_ctrl_poll(struct file *file, poll_table *wait)
{
	struct qti_ctrl_port *port = file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	if (!port) {
		pr_err("%s on a NULL device\n", __func__);
		return POLLERR;
	}

	poll_wait(file, &port->read_wq, wait);

	spin_lock_irqsave(&port->lock, flags);
	if (!list_empty(&port->cpkt_req_q)) {
		mask |= POLLIN | POLLRDNORM;
		pr_debug("%s sets POLLIN for rmnet_ctrl_qti_port\n", __func__);
	}
	spin_unlock_irqrestore(&port->lock, flags);

	return mask;
}

static int qti_ctrl_read_stats(struct seq_file *s, void *unused)
{
	struct qti_ctrl_port	*port = s->private;
	unsigned long		flags;
	int			i;

	for (i = 0; i < QTI_NUM_PORTS; i++) {
		port = ctrl_port[i];
		if (!port)
			continue;
		spin_lock_irqsave(&port->lock, flags);

		seq_printf(s, "\n#PORT:%d port: %pK\n", i, port);
		seq_printf(s, "name:			%s\n", port->name);
		seq_printf(s, "host_to_modem:		%d\n",
				port->host_to_modem);
		seq_printf(s, "copied_to_modem:	%d\n",
				port->copied_to_modem);
		seq_printf(s, "copied_from_modem:	%d\n",
				port->copied_from_modem);
		seq_printf(s, "modem_to_host:		%d\n",
				port->modem_to_host);
		seq_printf(s, "cpkt_drp_cnt:		%d\n",
				port->drp_cpkt_cnt);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return 0;
}

static int qti_ctrl_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, qti_ctrl_read_stats, inode->i_private);
}

static ssize_t qti_ctrl_reset_stats(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct qti_ctrl_port *port = s->private;
	int                     i;
	unsigned long           flags;

	for (i = 0; i < QTI_NUM_PORTS; i++) {
		port = ctrl_port[i];
		if (!port)
			continue;

		spin_lock_irqsave(&port->lock, flags);
		port->host_to_modem = 0;
		port->copied_to_modem = 0;
		port->copied_from_modem = 0;
		port->modem_to_host = 0;
		port->drp_cpkt_cnt = 0;
		spin_unlock_irqrestore(&port->lock, flags);
	}
	return count;
}

const struct file_operations qti_ctrl_stats_ops = {
	.open = qti_ctrl_stats_open,
	.read = seq_read,
	.write = qti_ctrl_reset_stats,
};

static struct dentry   *qti_ctrl_dent;
static void qti_ctrl_debugfs_init(void)
{
	struct dentry   *qti_ctrl_dfile;

	qti_ctrl_dent = debugfs_create_dir("usb_qti", 0);
	if (IS_ERR(qti_ctrl_dent))
		return;

	qti_ctrl_dfile =
		debugfs_create_file("status", 0444, qti_ctrl_dent, 0,
				&qti_ctrl_stats_ops);
	if (!qti_ctrl_dfile || IS_ERR(qti_ctrl_dfile))
		debugfs_remove(qti_ctrl_dent);
}

static void qti_ctrl_debugfs_exit(void)
{
	debugfs_remove_recursive(qti_ctrl_dent);
}

/* file operations for rmnet device /dev/rmnet_ctrl */
static const struct file_operations qti_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = qti_ctrl_open,
	.release = qti_ctrl_release,
	.read = qti_ctrl_read,
	.write = qti_ctrl_write,
	.unlocked_ioctl = qti_ctrl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = qti_ctrl_ioctl,
#endif
	.poll = qti_ctrl_poll,
};
/* file operations for DPL device /dev/dpl_ctrl */
static const struct file_operations dpl_qti_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = qti_ctrl_open,
	.release = qti_ctrl_release,
	.read = qti_ctrl_read,
	.write = NULL,
	.unlocked_ioctl = qti_ctrl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = qti_ctrl_ioctl,
#endif
	.poll = qti_ctrl_poll,
};
#define RMNET_MODULE_NAME "rmnet_ctrl"

static int qti_ctrl_alloc_chardev_region(void)
{
	int ret;

	rmnet_class = class_create(THIS_MODULE, RMNET_MODULE_NAME);
	if (IS_ERR(rmnet_class)) {
		pr_err("class_create() failed ENOMEM\n");
		ret = -ENOMEM;
	}
	ret = alloc_chrdev_region(&qti_ctrl_dev, 0, QTI_NUM_PORTS,
		RMNET_MODULE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region() failed ret:%i\n", ret);
		return ret;
	}

	return 0;
}

int gqti_ctrl_init(void)
{
	int ret, i, sz = QTI_CTRL_NAME_LEN;
	struct qti_ctrl_port *port = NULL;

	ret = qti_ctrl_alloc_chardev_region();
	if (ret) {
		pr_err("qti_ctrl_alloc_chardev_region() failed ret:%d\n", ret);
		return ret;
	}

	for (i = 0; i < QTI_NUM_PORTS; i++) {
		port = kzalloc(sizeof(struct qti_ctrl_port), GFP_KERNEL);
		if (!port) {
			ret = -ENOMEM;
			goto fail_init;
		}

		INIT_LIST_HEAD(&port->cpkt_req_q);
		spin_lock_init(&port->lock);

		atomic_set(&port->open_excl, 0);
		atomic_set(&port->read_excl, 0);
		atomic_set(&port->write_excl, 0);
		atomic_set(&port->ioctl_excl, 0);
		atomic_set(&port->connected, 0);
		atomic_set(&port->line_state, 0);

		init_waitqueue_head(&port->read_wq);

		ctrl_port[i] = port;
		port->index = i;
		port->ipa_prod_idx = -1;
		port->ipa_cons_idx = -1;

		if (i == QTI_PORT_RMNET)
			strlcat(port->name, RMNET_CTRL_QTI_NAME, sz);
		else if (i == QTI_PORT_DPL)
			strlcat(port->name, DPL_CTRL_QTI_NAME, sz);
		else
			snprintf(port->name, sz, "%s%d",
				RMNET_CTRL_QTI_NAME, i);

		port->dev = device_create(rmnet_class, NULL, qti_ctrl_dev+i,
				port->dev, port->name);
		if (IS_ERR(port->dev)) {
			pr_err("device_create() failed for port(%d)\n", i);
			ret = -ENOMEM;
			goto fail_device_create;
		}

		if (i == QTI_PORT_DPL)
			cdev_init(&port->ctrl_device, &dpl_qti_ctrl_fops);
		else
			cdev_init(&port->ctrl_device, &qti_ctrl_fops);

		port->ctrl_device.owner = THIS_MODULE;
		ret = cdev_add(&port->ctrl_device, qti_ctrl_dev + i, 1);
		if (ret < 0) {
			pr_err("cdev_add() failed ret:%d\n", ret);
			goto fail_cdev;
		}
	}
	qti_ctrl_debugfs_init();
	return ret;
fail_cdev:
	cdev_del(&port->ctrl_device);
fail_device_create:
	class_destroy(rmnet_class);
	unregister_chrdev_region(MAJOR(qti_ctrl_dev), QTI_NUM_PORTS);
fail_init:
	for (i--; i >= 0; i--) {
		kfree(ctrl_port[i]);
		ctrl_port[i] = NULL;
	}
	return ret;
}

void gqti_ctrl_cleanup(void)
{
	int i;

	for (i = 0; i < QTI_NUM_PORTS; i++) {
		cdev_del(&ctrl_port[i]->ctrl_device);
		device_unregister(ctrl_port[i]->dev);
		kfree(ctrl_port[i]);
		ctrl_port[i] = NULL;
	}

	device_destroy(rmnet_class, qti_ctrl_dev);
	class_destroy(rmnet_class);
	unregister_chrdev_region(MAJOR(qti_ctrl_dev), QTI_NUM_PORTS);
	qti_ctrl_debugfs_exit();
}
