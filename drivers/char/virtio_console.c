/*
 * Copyright (C) 2006, 2007, 2009 Rusty Russell, IBM Corporation
 * Copyright (C) 2009, 2010 Red Hat, Inc.
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
#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "hvc_console.h"

/* Moved here from .h file in order to disable MULTIPORT. */
#define VIRTIO_CONSOLE_F_MULTIPORT 1	/* Does host provide multiple ports? */

struct virtio_console_multiport_conf {
	struct virtio_console_config config;
	/* max. number of ports this device can hold */
	__u32 max_nr_ports;
	/* number of ports added so far */
	__u32 nr_ports;
} __attribute__((packed));

/*
 * A message that's passed between the Host and the Guest for a
 * particular port.
 */
struct virtio_console_control {
	__u32 id;		/* Port number */
	__u16 event;		/* The kind of control event (see below) */
	__u16 value;		/* Extra information for the key */
};

/* Some events for control messages */
#define VIRTIO_CONSOLE_PORT_READY	0
#define VIRTIO_CONSOLE_CONSOLE_PORT	1
#define VIRTIO_CONSOLE_RESIZE		2
#define VIRTIO_CONSOLE_PORT_OPEN	3
#define VIRTIO_CONSOLE_PORT_NAME	4
#define VIRTIO_CONSOLE_PORT_REMOVE	5

/*
 * This is a global struct for storing common data for all the devices
 * this driver handles.
 *
 * Mainly, it has a linked list for all the consoles in one place so
 * that callbacks from hvc for get_chars(), put_chars() work properly
 * across multiple devices and multiple ports per device.
 */
struct ports_driver_data {
	/* Used for registering chardevs */
	struct class *class;

	/* Used for exporting per-port information to debugfs */
	struct dentry *debugfs_dir;

	/* Number of devices this driver is handling */
	unsigned int index;

	/*
	 * This is used to keep track of the number of hvc consoles
	 * spawned by this driver.  This number is given as the first
	 * argument to hvc_alloc().  To correctly map an initial
	 * console spawned via hvc_instantiate to the console being
	 * hooked up via hvc_alloc, we need to pass the same vtermno.
	 *
	 * We also just assume the first console being initialised was
	 * the first one that got used as the initial console.
	 */
	unsigned int next_vtermno;

	/* All the console devices handled by this driver */
	struct list_head consoles;
};
static struct ports_driver_data pdrvdata;

DEFINE_SPINLOCK(pdrvdata_lock);

/* This struct holds information that's relevant only for console ports */
struct console {
	/* We'll place all consoles in a list in the pdrvdata struct */
	struct list_head list;

	/* The hvc device associated with this console port */
	struct hvc_struct *hvc;

	/*
	 * This number identifies the number that we used to register
	 * with hvc in hvc_instantiate() and hvc_alloc(); this is the
	 * number passed on by the hvc callbacks to us to
	 * differentiate between the other console ports handled by
	 * this driver
	 */
	u32 vtermno;
};

struct port_buffer {
	char *buf;

	/* size of the buffer in *buf above */
	size_t size;

	/* used length of the buffer */
	size_t len;
	/* offset in the buf from which to consume data */
	size_t offset;
};

/*
 * This is a per-device struct that stores data common to all the
 * ports for that device (vdev->priv).
 */
struct ports_device {
	/*
	 * Workqueue handlers where we process deferred work after
	 * notification
	 */
	struct work_struct control_work;
	struct work_struct config_work;

	struct list_head ports;

	/* To protect the list of ports */
	spinlock_t ports_lock;

	/* To protect the vq operations for the control channel */
	spinlock_t cvq_lock;

	/* The current config space is stored here */
	struct virtio_console_multiport_conf config;

	/* The virtio device we're associated with */
	struct virtio_device *vdev;

	/*
	 * A couple of virtqueues for the control channel: one for
	 * guest->host transfers, one for host->guest transfers
	 */
	struct virtqueue *c_ivq, *c_ovq;

	/* Array of per-port IO virtqueues */
	struct virtqueue **in_vqs, **out_vqs;

	/* Used for numbering devices for sysfs and debugfs */
	unsigned int drv_index;

	/* Major number for this device.  Ports will be created as minors. */
	int chr_major;
};

/* This struct holds the per-port data */
struct port {
	/* Next port in the list, head is in the ports_device */
	struct list_head list;

	/* Pointer to the parent virtio_console device */
	struct ports_device *portdev;

	/* The current buffer from which data has to be fed to readers */
	struct port_buffer *inbuf;

	/*
	 * To protect the operations on the in_vq associated with this
	 * port.  Has to be a spinlock because it can be called from
	 * interrupt context (get_char()).
	 */
	spinlock_t inbuf_lock;

	/* The IO vqs for this port */
	struct virtqueue *in_vq, *out_vq;

	/* File in the debugfs directory that exposes this port's information */
	struct dentry *debugfs_file;

	/*
	 * The entries in this struct will be valid if this port is
	 * hooked up to an hvc console
	 */
	struct console cons;

	/* Each port associates with a separate char device */
	struct cdev cdev;
	struct device *dev;

	/* A waitqueue for poll() or blocking read operations */
	wait_queue_head_t waitqueue;

	/* The 'name' of the port that we expose via sysfs properties */
	char *name;

	/* The 'id' to identify the port with the Host */
	u32 id;

	/* Is the host device open */
	bool host_connected;

	/* We should allow only one process to open a port */
	bool guest_connected;
};

/* This is the very early arch-specified put chars function. */
static int (*early_put_chars)(u32, const char *, int);

static struct port *find_port_by_vtermno(u32 vtermno)
{
	struct port *port;
	struct console *cons;
	unsigned long flags;

	spin_lock_irqsave(&pdrvdata_lock, flags);
	list_for_each_entry(cons, &pdrvdata.consoles, list) {
		if (cons->vtermno == vtermno) {
			port = container_of(cons, struct port, cons);
			goto out;
		}
	}
	port = NULL;
out:
	spin_unlock_irqrestore(&pdrvdata_lock, flags);
	return port;
}

static struct port *find_port_by_id(struct ports_device *portdev, u32 id)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list)
		if (port->id == id)
			goto out;
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);

	return port;
}

static struct port *find_port_by_vq(struct ports_device *portdev,
				    struct virtqueue *vq)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list)
		if (port->in_vq == vq || port->out_vq == vq)
			goto out;
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);
	return port;
}

static bool is_console_port(struct port *port)
{
	if (port->cons.hvc)
		return true;
	return false;
}

static inline bool use_multiport(struct ports_device *portdev)
{
	/*
	 * This condition can be true when put_chars is called from
	 * early_init
	 */
	if (!portdev->vdev)
		return 0;
	return portdev->vdev->features[0] & (1 << VIRTIO_CONSOLE_F_MULTIPORT);
}

static void free_buf(struct port_buffer *buf)
{
	kfree(buf->buf);
	kfree(buf);
}

static struct port_buffer *alloc_buf(size_t buf_size)
{
	struct port_buffer *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		goto fail;
	buf->buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf->buf)
		goto free_buf;
	buf->len = 0;
	buf->offset = 0;
	buf->size = buf_size;
	return buf;

free_buf:
	kfree(buf);
fail:
	return NULL;
}

/* Callers should take appropriate locks */
static void *get_inbuf(struct port *port)
{
	struct port_buffer *buf;
	struct virtqueue *vq;
	unsigned int len;

	vq = port->in_vq;
	buf = vq->vq_ops->get_buf(vq, &len);
	if (buf) {
		buf->len = len;
		buf->offset = 0;
	}
	return buf;
}

/*
 * Create a scatter-gather list representing our input buffer and put
 * it in the queue.
 *
 * Callers should take appropriate locks.
 */
static int add_inbuf(struct virtqueue *vq, struct port_buffer *buf)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, buf->buf, buf->size);

	ret = vq->vq_ops->add_buf(vq, sg, 0, 1, buf);
	vq->vq_ops->kick(vq);
	return ret;
}

/* Discard any unread data this port has. Callers lockers. */
static void discard_port_data(struct port *port)
{
	struct port_buffer *buf;
	struct virtqueue *vq;
	unsigned int len;
	int ret;

	vq = port->in_vq;
	if (port->inbuf)
		buf = port->inbuf;
	else
		buf = vq->vq_ops->get_buf(vq, &len);

	ret = 0;
	while (buf) {
		if (add_inbuf(vq, buf) < 0) {
			ret++;
			free_buf(buf);
		}
		buf = vq->vq_ops->get_buf(vq, &len);
	}
	port->inbuf = NULL;
	if (ret)
		dev_warn(port->dev, "Errors adding %d buffers back to vq\n",
			 ret);
}

static bool port_has_data(struct port *port)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&port->inbuf_lock, flags);
	if (port->inbuf) {
		ret = true;
		goto out;
	}
	port->inbuf = get_inbuf(port);
	if (port->inbuf) {
		ret = true;
		goto out;
	}
	ret = false;
out:
	spin_unlock_irqrestore(&port->inbuf_lock, flags);
	return ret;
}

static ssize_t send_control_msg(struct port *port, unsigned int event,
				unsigned int value)
{
	struct scatterlist sg[1];
	struct virtio_console_control cpkt;
	struct virtqueue *vq;
	unsigned int len;

	if (!use_multiport(port->portdev))
		return 0;

	cpkt.id = port->id;
	cpkt.event = event;
	cpkt.value = value;

	vq = port->portdev->c_ovq;

	sg_init_one(sg, &cpkt, sizeof(cpkt));
	if (vq->vq_ops->add_buf(vq, sg, 1, 0, &cpkt) >= 0) {
		vq->vq_ops->kick(vq);
		while (!vq->vq_ops->get_buf(vq, &len))
			cpu_relax();
	}
	return 0;
}

static ssize_t send_buf(struct port *port, void *in_buf, size_t in_count)
{
	struct scatterlist sg[1];
	struct virtqueue *out_vq;
	ssize_t ret;
	unsigned int len;

	out_vq = port->out_vq;

	sg_init_one(sg, in_buf, in_count);
	ret = out_vq->vq_ops->add_buf(out_vq, sg, 1, 0, in_buf);

	/* Tell Host to go! */
	out_vq->vq_ops->kick(out_vq);

	if (ret < 0) {
		in_count = 0;
		goto fail;
	}

	/* Wait till the host acknowledges it pushed out the data we sent. */
	while (!out_vq->vq_ops->get_buf(out_vq, &len))
		cpu_relax();
fail:
	/* We're expected to return the amount of data we wrote */
	return in_count;
}

/*
 * Give out the data that's requested from the buffer that we have
 * queued up.
 */
static ssize_t fill_readbuf(struct port *port, char *out_buf, size_t out_count,
			    bool to_user)
{
	struct port_buffer *buf;
	unsigned long flags;

	if (!out_count || !port_has_data(port))
		return 0;

	buf = port->inbuf;
	out_count = min(out_count, buf->len - buf->offset);

	if (to_user) {
		ssize_t ret;

		ret = copy_to_user(out_buf, buf->buf + buf->offset, out_count);
		if (ret)
			return -EFAULT;
	} else {
		memcpy(out_buf, buf->buf + buf->offset, out_count);
	}

	buf->offset += out_count;

	if (buf->offset == buf->len) {
		/*
		 * We're done using all the data in this buffer.
		 * Re-queue so that the Host can send us more data.
		 */
		spin_lock_irqsave(&port->inbuf_lock, flags);
		port->inbuf = NULL;

		if (add_inbuf(port->in_vq, buf) < 0)
			dev_warn(port->dev, "failed add_buf\n");

		spin_unlock_irqrestore(&port->inbuf_lock, flags);
	}
	/* Return the number of bytes actually copied */
	return out_count;
}

/* The condition that must be true for polling to end */
static bool wait_is_over(struct port *port)
{
	return port_has_data(port) || !port->host_connected;
}

static ssize_t port_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp)
{
	struct port *port;
	ssize_t ret;

	port = filp->private_data;

	if (!port_has_data(port)) {
		/*
		 * If nothing's connected on the host just return 0 in
		 * case of list_empty; this tells the userspace app
		 * that there's no connection
		 */
		if (!port->host_connected)
			return 0;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(port->waitqueue,
					       wait_is_over(port));
		if (ret < 0)
			return ret;
	}
	/*
	 * We could've received a disconnection message while we were
	 * waiting for more data.
	 *
	 * This check is not clubbed in the if() statement above as we
	 * might receive some data as well as the host could get
	 * disconnected after we got woken up from our wait.  So we
	 * really want to give off whatever data we have and only then
	 * check for host_connected.
	 */
	if (!port_has_data(port) && !port->host_connected)
		return 0;

	return fill_readbuf(port, ubuf, count, true);
}

static ssize_t port_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp)
{
	struct port *port;
	char *buf;
	ssize_t ret;

	port = filp->private_data;

	count = min((size_t)(32 * 1024), count);

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = copy_from_user(buf, ubuf, count);
	if (ret) {
		ret = -EFAULT;
		goto free_buf;
	}

	ret = send_buf(port, buf, count);
free_buf:
	kfree(buf);
	return ret;
}

static unsigned int port_fops_poll(struct file *filp, poll_table *wait)
{
	struct port *port;
	unsigned int ret;

	port = filp->private_data;
	poll_wait(filp, &port->waitqueue, wait);

	ret = 0;
	if (port->inbuf)
		ret |= POLLIN | POLLRDNORM;
	if (port->host_connected)
		ret |= POLLOUT;
	if (!port->host_connected)
		ret |= POLLHUP;

	return ret;
}

static int port_fops_release(struct inode *inode, struct file *filp)
{
	struct port *port;

	port = filp->private_data;

	/* Notify host of port being closed */
	send_control_msg(port, VIRTIO_CONSOLE_PORT_OPEN, 0);

	spin_lock_irq(&port->inbuf_lock);
	port->guest_connected = false;

	discard_port_data(port);

	spin_unlock_irq(&port->inbuf_lock);

	return 0;
}

static int port_fops_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct port *port;

	port = container_of(cdev, struct port, cdev);
	filp->private_data = port;

	/*
	 * Don't allow opening of console port devices -- that's done
	 * via /dev/hvc
	 */
	if (is_console_port(port))
		return -ENXIO;

	/* Allow only one process to open a particular port at a time */
	spin_lock_irq(&port->inbuf_lock);
	if (port->guest_connected) {
		spin_unlock_irq(&port->inbuf_lock);
		return -EMFILE;
	}

	port->guest_connected = true;
	spin_unlock_irq(&port->inbuf_lock);

	/* Notify host of port being opened */
	send_control_msg(filp->private_data, VIRTIO_CONSOLE_PORT_OPEN, 1);

	return 0;
}

/*
 * The file operations that we support: programs in the guest can open
 * a console device, read from it, write to it, poll for data and
 * close it.  The devices are at
 *   /dev/vport<device number>p<port number>
 */
static const struct file_operations port_fops = {
	.owner = THIS_MODULE,
	.open  = port_fops_open,
	.read  = port_fops_read,
	.write = port_fops_write,
	.poll  = port_fops_poll,
	.release = port_fops_release,
};

/*
 * The put_chars() callback is pretty straightforward.
 *
 * We turn the characters into a scatter-gather list, add it to the
 * output queue and then kick the Host.  Then we sit here waiting for
 * it to finish: inefficient in theory, but in practice
 * implementations will do it immediately (lguest's Launcher does).
 */
static int put_chars(u32 vtermno, const char *buf, int count)
{
	struct port *port;

	if (unlikely(early_put_chars))
		return early_put_chars(vtermno, buf, count);

	port = find_port_by_vtermno(vtermno);
	if (!port)
		return 0;

	return send_buf(port, (void *)buf, count);
}

/*
 * get_chars() is the callback from the hvc_console infrastructure
 * when an interrupt is received.
 *
 * We call out to fill_readbuf that gets us the required data from the
 * buffers that are queued up.
 */
static int get_chars(u32 vtermno, char *buf, int count)
{
	struct port *port;

	port = find_port_by_vtermno(vtermno);
	if (!port)
		return 0;

	/* If we don't have an input queue yet, we can't get input. */
	BUG_ON(!port->in_vq);

	return fill_readbuf(port, buf, count, false);
}

static void resize_console(struct port *port)
{
	struct virtio_device *vdev;
	struct winsize ws;

	/* The port could have been hot-unplugged */
	if (!port)
		return;

	vdev = port->portdev->vdev;
	if (virtio_has_feature(vdev, VIRTIO_CONSOLE_F_SIZE)) {
		vdev->config->get(vdev,
				  offsetof(struct virtio_console_config, cols),
				  &ws.ws_col, sizeof(u16));
		vdev->config->get(vdev,
				  offsetof(struct virtio_console_config, rows),
				  &ws.ws_row, sizeof(u16));
		hvc_resize(port->cons.hvc, ws);
	}
}

/* We set the configuration at this point, since we now have a tty */
static int notifier_add_vio(struct hvc_struct *hp, int data)
{
	struct port *port;

	port = find_port_by_vtermno(hp->vtermno);
	if (!port)
		return -EINVAL;

	hp->irq_requested = 1;
	resize_console(port);

	return 0;
}

static void notifier_del_vio(struct hvc_struct *hp, int data)
{
	hp->irq_requested = 0;
}

/* The operations for console ports. */
static const struct hv_ops hv_ops = {
	.get_chars = get_chars,
	.put_chars = put_chars,
	.notifier_add = notifier_add_vio,
	.notifier_del = notifier_del_vio,
	.notifier_hangup = notifier_del_vio,
};

/*
 * Console drivers are initialized very early so boot messages can go
 * out, so we do things slightly differently from the generic virtio
 * initialization of the net and block drivers.
 *
 * At this stage, the console is output-only.  It's too early to set
 * up a virtqueue, so we let the drivers do some boutique early-output
 * thing.
 */
int __init virtio_cons_early_init(int (*put_chars)(u32, const char *, int))
{
	early_put_chars = put_chars;
	return hvc_instantiate(0, 0, &hv_ops);
}

int init_port_console(struct port *port)
{
	int ret;

	/*
	 * The Host's telling us this port is a console port.  Hook it
	 * up with an hvc console.
	 *
	 * To set up and manage our virtual console, we call
	 * hvc_alloc().
	 *
	 * The first argument of hvc_alloc() is the virtual console
	 * number.  The second argument is the parameter for the
	 * notification mechanism (like irq number).  We currently
	 * leave this as zero, virtqueues have implicit notifications.
	 *
	 * The third argument is a "struct hv_ops" containing the
	 * put_chars() get_chars(), notifier_add() and notifier_del()
	 * pointers.  The final argument is the output buffer size: we
	 * can do any size, so we put PAGE_SIZE here.
	 */
	port->cons.vtermno = pdrvdata.next_vtermno;

	port->cons.hvc = hvc_alloc(port->cons.vtermno, 0, &hv_ops, PAGE_SIZE);
	if (IS_ERR(port->cons.hvc)) {
		ret = PTR_ERR(port->cons.hvc);
		dev_err(port->dev,
			"error %d allocating hvc for port\n", ret);
		port->cons.hvc = NULL;
		return ret;
	}
	spin_lock_irq(&pdrvdata_lock);
	pdrvdata.next_vtermno++;
	list_add_tail(&port->cons.list, &pdrvdata.consoles);
	spin_unlock_irq(&pdrvdata_lock);
	port->guest_connected = true;

	/* Notify host of port being opened */
	send_control_msg(port, VIRTIO_CONSOLE_PORT_OPEN, 1);

	return 0;
}

static ssize_t show_port_name(struct device *dev,
			      struct device_attribute *attr, char *buffer)
{
	struct port *port;

	port = dev_get_drvdata(dev);

	return sprintf(buffer, "%s\n", port->name);
}

static DEVICE_ATTR(name, S_IRUGO, show_port_name, NULL);

static struct attribute *port_sysfs_entries[] = {
	&dev_attr_name.attr,
	NULL
};

static struct attribute_group port_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = port_sysfs_entries,
};

static int debugfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t debugfs_read(struct file *filp, char __user *ubuf,
			    size_t count, loff_t *offp)
{
	struct port *port;
	char *buf;
	ssize_t ret, out_offset, out_count;

	out_count = 1024;
	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	port = filp->private_data;
	out_offset = 0;
	out_offset += snprintf(buf + out_offset, out_count,
			       "name: %s\n", port->name ? port->name : "");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "guest_connected: %d\n", port->guest_connected);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "host_connected: %d\n", port->host_connected);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "is_console: %s\n",
			       is_console_port(port) ? "yes" : "no");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "console_vtermno: %u\n", port->cons.vtermno);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, out_offset);
	kfree(buf);
	return ret;
}

static const struct file_operations port_debugfs_ops = {
	.owner = THIS_MODULE,
	.open  = debugfs_open,
	.read  = debugfs_read,
};

/* Remove all port-specific data. */
static int remove_port(struct port *port)
{
	struct port_buffer *buf;

	spin_lock_irq(&port->portdev->ports_lock);
	list_del(&port->list);
	spin_unlock_irq(&port->portdev->ports_lock);

	if (is_console_port(port)) {
		spin_lock_irq(&pdrvdata_lock);
		list_del(&port->cons.list);
		spin_unlock_irq(&pdrvdata_lock);
		hvc_remove(port->cons.hvc);
	}
	if (port->guest_connected)
		send_control_msg(port, VIRTIO_CONSOLE_PORT_OPEN, 0);

	sysfs_remove_group(&port->dev->kobj, &port_attribute_group);
	device_destroy(pdrvdata.class, port->dev->devt);
	cdev_del(&port->cdev);

	/* Remove unused data this port might have received. */
	discard_port_data(port);

	/* Remove buffers we queued up for the Host to send us data in. */
	while ((buf = port->in_vq->vq_ops->detach_unused_buf(port->in_vq)))
		free_buf(buf);

	kfree(port->name);

	debugfs_remove(port->debugfs_file);

	kfree(port);
	return 0;
}

/* Any private messages that the Host and Guest want to share */
static void handle_control_message(struct ports_device *portdev,
				   struct port_buffer *buf)
{
	struct virtio_console_control *cpkt;
	struct port *port;
	size_t name_size;
	int err;

	cpkt = (struct virtio_console_control *)(buf->buf + buf->offset);

	port = find_port_by_id(portdev, cpkt->id);
	if (!port) {
		/* No valid header at start of buffer.  Drop it. */
		dev_dbg(&portdev->vdev->dev,
			"Invalid index %u in control packet\n", cpkt->id);
		return;
	}

	switch (cpkt->event) {
	case VIRTIO_CONSOLE_CONSOLE_PORT:
		if (!cpkt->value)
			break;
		if (is_console_port(port))
			break;

		init_port_console(port);
		/*
		 * Could remove the port here in case init fails - but
		 * have to notify the host first.
		 */
		break;
	case VIRTIO_CONSOLE_RESIZE:
		if (!is_console_port(port))
			break;
		port->cons.hvc->irq_requested = 1;
		resize_console(port);
		break;
	case VIRTIO_CONSOLE_PORT_OPEN:
		port->host_connected = cpkt->value;
		wake_up_interruptible(&port->waitqueue);
		break;
	case VIRTIO_CONSOLE_PORT_NAME:
		/*
		 * Skip the size of the header and the cpkt to get the size
		 * of the name that was sent
		 */
		name_size = buf->len - buf->offset - sizeof(*cpkt) + 1;

		port->name = kmalloc(name_size, GFP_KERNEL);
		if (!port->name) {
			dev_err(port->dev,
				"Not enough space to store port name\n");
			break;
		}
		strncpy(port->name, buf->buf + buf->offset + sizeof(*cpkt),
			name_size - 1);
		port->name[name_size - 1] = 0;

		/*
		 * Since we only have one sysfs attribute, 'name',
		 * create it only if we have a name for the port.
		 */
		err = sysfs_create_group(&port->dev->kobj,
					 &port_attribute_group);
		if (err) {
			dev_err(port->dev,
				"Error %d creating sysfs device attributes\n",
				err);
		} else {
			/*
			 * Generate a udev event so that appropriate
			 * symlinks can be created based on udev
			 * rules.
			 */
			kobject_uevent(&port->dev->kobj, KOBJ_CHANGE);
		}
		break;
	case VIRTIO_CONSOLE_PORT_REMOVE:
		/*
		 * Hot unplug the port.  We don't decrement nr_ports
		 * since we don't want to deal with extra complexities
		 * of using the lowest-available port id: We can just
		 * pick up the nr_ports number as the id and not have
		 * userspace send it to us.  This helps us in two
		 * ways:
		 *
		 * - We don't need to have a 'port_id' field in the
		 *   config space when a port is hot-added.  This is a
		 *   good thing as we might queue up multiple hotplug
		 *   requests issued in our workqueue.
		 *
		 * - Another way to deal with this would have been to
		 *   use a bitmap of the active ports and select the
		 *   lowest non-active port from that map.  That
		 *   bloats the already tight config space and we
		 *   would end up artificially limiting the
		 *   max. number of ports to sizeof(bitmap).  Right
		 *   now we can support 2^32 ports (as the port id is
		 *   stored in a u32 type).
		 *
		 */
		remove_port(port);
		break;
	}
}

static void control_work_handler(struct work_struct *work)
{
	struct ports_device *portdev;
	struct virtqueue *vq;
	struct port_buffer *buf;
	unsigned int len;

	portdev = container_of(work, struct ports_device, control_work);
	vq = portdev->c_ivq;

	spin_lock(&portdev->cvq_lock);
	while ((buf = vq->vq_ops->get_buf(vq, &len))) {
		spin_unlock(&portdev->cvq_lock);

		buf->len = len;
		buf->offset = 0;

		handle_control_message(portdev, buf);

		spin_lock(&portdev->cvq_lock);
		if (add_inbuf(portdev->c_ivq, buf) < 0) {
			dev_warn(&portdev->vdev->dev,
				 "Error adding buffer to queue\n");
			free_buf(buf);
		}
	}
	spin_unlock(&portdev->cvq_lock);
}

static void in_intr(struct virtqueue *vq)
{
	struct port *port;
	unsigned long flags;

	port = find_port_by_vq(vq->vdev->priv, vq);
	if (!port)
		return;

	spin_lock_irqsave(&port->inbuf_lock, flags);
	if (!port->inbuf)
		port->inbuf = get_inbuf(port);

	/*
	 * Don't queue up data when port is closed.  This condition
	 * can be reached when a console port is not yet connected (no
	 * tty is spawned) and the host sends out data to console
	 * ports.  For generic serial ports, the host won't
	 * (shouldn't) send data till the guest is connected.
	 */
	if (!port->guest_connected)
		discard_port_data(port);

	spin_unlock_irqrestore(&port->inbuf_lock, flags);

	wake_up_interruptible(&port->waitqueue);

	if (is_console_port(port) && hvc_poll(port->cons.hvc))
		hvc_kick();
}

static void control_intr(struct virtqueue *vq)
{
	struct ports_device *portdev;

	portdev = vq->vdev->priv;
	schedule_work(&portdev->control_work);
}

static void config_intr(struct virtio_device *vdev)
{
	struct ports_device *portdev;

	portdev = vdev->priv;
	if (use_multiport(portdev)) {
		/* Handle port hot-add */
		schedule_work(&portdev->config_work);
	}
	/*
	 * We'll use this way of resizing only for legacy support.
	 * For newer userspace (VIRTIO_CONSOLE_F_MULTPORT+), use
	 * control messages to indicate console size changes so that
	 * it can be done per-port
	 */
	resize_console(find_port_by_id(portdev, 0));
}

static unsigned int fill_queue(struct virtqueue *vq, spinlock_t *lock)
{
	struct port_buffer *buf;
	unsigned int nr_added_bufs;
	int ret;

	nr_added_bufs = 0;
	do {
		buf = alloc_buf(PAGE_SIZE);
		if (!buf)
			break;

		spin_lock_irq(lock);
		ret = add_inbuf(vq, buf);
		if (ret < 0) {
			spin_unlock_irq(lock);
			free_buf(buf);
			break;
		}
		nr_added_bufs++;
		spin_unlock_irq(lock);
	} while (ret > 0);

	return nr_added_bufs;
}

static int add_port(struct ports_device *portdev, u32 id)
{
	char debugfs_name[16];
	struct port *port;
	struct port_buffer *buf;
	dev_t devt;
	unsigned int nr_added_bufs;
	int err;

	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		goto fail;
	}

	port->portdev = portdev;
	port->id = id;

	port->name = NULL;
	port->inbuf = NULL;
	port->cons.hvc = NULL;

	port->host_connected = port->guest_connected = false;

	port->in_vq = portdev->in_vqs[port->id];
	port->out_vq = portdev->out_vqs[port->id];

	cdev_init(&port->cdev, &port_fops);

	devt = MKDEV(portdev->chr_major, id);
	err = cdev_add(&port->cdev, devt, 1);
	if (err < 0) {
		dev_err(&port->portdev->vdev->dev,
			"Error %d adding cdev for port %u\n", err, id);
		goto free_port;
	}
	port->dev = device_create(pdrvdata.class, &port->portdev->vdev->dev,
				  devt, port, "vport%up%u",
				  port->portdev->drv_index, id);
	if (IS_ERR(port->dev)) {
		err = PTR_ERR(port->dev);
		dev_err(&port->portdev->vdev->dev,
			"Error %d creating device for port %u\n",
			err, id);
		goto free_cdev;
	}

	spin_lock_init(&port->inbuf_lock);
	init_waitqueue_head(&port->waitqueue);

	/* Fill the in_vq with buffers so the host can send us data. */
	nr_added_bufs = fill_queue(port->in_vq, &port->inbuf_lock);
	if (!nr_added_bufs) {
		dev_err(port->dev, "Error allocating inbufs\n");
		err = -ENOMEM;
		goto free_device;
	}

	/*
	 * If we're not using multiport support, this has to be a console port
	 */
	if (!use_multiport(port->portdev)) {
		err = init_port_console(port);
		if (err)
			goto free_inbufs;
	}

	spin_lock_irq(&portdev->ports_lock);
	list_add_tail(&port->list, &port->portdev->ports);
	spin_unlock_irq(&portdev->ports_lock);

	/*
	 * Tell the Host we're set so that it can send us various
	 * configuration parameters for this port (eg, port name,
	 * caching, whether this is a console port, etc.)
	 */
	send_control_msg(port, VIRTIO_CONSOLE_PORT_READY, 1);

	if (pdrvdata.debugfs_dir) {
		/*
		 * Finally, create the debugfs file that we can use to
		 * inspect a port's state at any time
		 */
		sprintf(debugfs_name, "vport%up%u",
			port->portdev->drv_index, id);
		port->debugfs_file = debugfs_create_file(debugfs_name, 0444,
							 pdrvdata.debugfs_dir,
							 port,
							 &port_debugfs_ops);
	}
	return 0;

free_inbufs:
	while ((buf = port->in_vq->vq_ops->detach_unused_buf(port->in_vq)))
		free_buf(buf);
free_device:
	device_destroy(pdrvdata.class, port->dev->devt);
free_cdev:
	cdev_del(&port->cdev);
free_port:
	kfree(port);
fail:
	return err;
}

/*
 * The workhandler for config-space updates.
 *
 * This is called when ports are hot-added.
 */
static void config_work_handler(struct work_struct *work)
{
	struct virtio_console_multiport_conf virtconconf;
	struct ports_device *portdev;
	struct virtio_device *vdev;
	int err;

	portdev = container_of(work, struct ports_device, config_work);

	vdev = portdev->vdev;
	vdev->config->get(vdev,
			  offsetof(struct virtio_console_multiport_conf,
				   nr_ports),
			  &virtconconf.nr_ports,
			  sizeof(virtconconf.nr_ports));

	if (portdev->config.nr_ports == virtconconf.nr_ports) {
		/*
		 * Port 0 got hot-added.  Since we already did all the
		 * other initialisation for it, just tell the Host
		 * that the port is ready if we find the port.  In
		 * case the port was hot-removed earlier, we call
		 * add_port to add the port.
		 */
		struct port *port;

		port = find_port_by_id(portdev, 0);
		if (!port)
			add_port(portdev, 0);
		else
			send_control_msg(port, VIRTIO_CONSOLE_PORT_READY, 1);
		return;
	}
	if (virtconconf.nr_ports > portdev->config.max_nr_ports) {
		dev_warn(&vdev->dev,
			 "More ports specified (%u) than allowed (%u)",
			 portdev->config.nr_ports + 1,
			 portdev->config.max_nr_ports);
		return;
	}
	if (virtconconf.nr_ports < portdev->config.nr_ports)
		return;

	/* Hot-add ports */
	while (virtconconf.nr_ports - portdev->config.nr_ports) {
		err = add_port(portdev, portdev->config.nr_ports);
		if (err)
			break;
		portdev->config.nr_ports++;
	}
}

static int init_vqs(struct ports_device *portdev)
{
	vq_callback_t **io_callbacks;
	char **io_names;
	struct virtqueue **vqs;
	u32 i, j, nr_ports, nr_queues;
	int err;

	nr_ports = portdev->config.max_nr_ports;
	nr_queues = use_multiport(portdev) ? (nr_ports + 1) * 2 : 2;

	vqs = kmalloc(nr_queues * sizeof(struct virtqueue *), GFP_KERNEL);
	if (!vqs) {
		err = -ENOMEM;
		goto fail;
	}
	io_callbacks = kmalloc(nr_queues * sizeof(vq_callback_t *), GFP_KERNEL);
	if (!io_callbacks) {
		err = -ENOMEM;
		goto free_vqs;
	}
	io_names = kmalloc(nr_queues * sizeof(char *), GFP_KERNEL);
	if (!io_names) {
		err = -ENOMEM;
		goto free_callbacks;
	}
	portdev->in_vqs = kmalloc(nr_ports * sizeof(struct virtqueue *),
				  GFP_KERNEL);
	if (!portdev->in_vqs) {
		err = -ENOMEM;
		goto free_names;
	}
	portdev->out_vqs = kmalloc(nr_ports * sizeof(struct virtqueue *),
				   GFP_KERNEL);
	if (!portdev->out_vqs) {
		err = -ENOMEM;
		goto free_invqs;
	}

	/*
	 * For backward compat (newer host but older guest), the host
	 * spawns a console port first and also inits the vqs for port
	 * 0 before others.
	 */
	j = 0;
	io_callbacks[j] = in_intr;
	io_callbacks[j + 1] = NULL;
	io_names[j] = "input";
	io_names[j + 1] = "output";
	j += 2;

	if (use_multiport(portdev)) {
		io_callbacks[j] = control_intr;
		io_callbacks[j + 1] = NULL;
		io_names[j] = "control-i";
		io_names[j + 1] = "control-o";

		for (i = 1; i < nr_ports; i++) {
			j += 2;
			io_callbacks[j] = in_intr;
			io_callbacks[j + 1] = NULL;
			io_names[j] = "input";
			io_names[j + 1] = "output";
		}
	}
	/* Find the queues. */
	err = portdev->vdev->config->find_vqs(portdev->vdev, nr_queues, vqs,
					      io_callbacks,
					      (const char **)io_names);
	if (err)
		goto free_outvqs;

	j = 0;
	portdev->in_vqs[0] = vqs[0];
	portdev->out_vqs[0] = vqs[1];
	j += 2;
	if (use_multiport(portdev)) {
		portdev->c_ivq = vqs[j];
		portdev->c_ovq = vqs[j + 1];

		for (i = 1; i < nr_ports; i++) {
			j += 2;
			portdev->in_vqs[i] = vqs[j];
			portdev->out_vqs[i] = vqs[j + 1];
		}
	}
	kfree(io_callbacks);
	kfree(io_names);
	kfree(vqs);

	return 0;

free_names:
	kfree(io_names);
free_callbacks:
	kfree(io_callbacks);
free_outvqs:
	kfree(portdev->out_vqs);
free_invqs:
	kfree(portdev->in_vqs);
free_vqs:
	kfree(vqs);
fail:
	return err;
}

static const struct file_operations portdev_fops = {
	.owner = THIS_MODULE,
};

/*
 * Once we're further in boot, we get probed like any other virtio
 * device.
 *
 * If the host also supports multiple console ports, we check the
 * config space to see how many ports the host has spawned.  We
 * initialize each port found.
 */
static int __devinit virtcons_probe(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	u32 i;
	int err;
	bool multiport;

	portdev = kmalloc(sizeof(*portdev), GFP_KERNEL);
	if (!portdev) {
		err = -ENOMEM;
		goto fail;
	}

	/* Attach this portdev to this virtio_device, and vice-versa. */
	portdev->vdev = vdev;
	vdev->priv = portdev;

	spin_lock_irq(&pdrvdata_lock);
	portdev->drv_index = pdrvdata.index++;
	spin_unlock_irq(&pdrvdata_lock);

	portdev->chr_major = register_chrdev(0, "virtio-portsdev",
					     &portdev_fops);
	if (portdev->chr_major < 0) {
		dev_err(&vdev->dev,
			"Error %d registering chrdev for device %u\n",
			portdev->chr_major, portdev->drv_index);
		err = portdev->chr_major;
		goto free;
	}

	multiport = false;
	portdev->config.nr_ports = 1;
	portdev->config.max_nr_ports = 1;
#if 0 /* Multiport is not quite ready yet --RR */
	if (virtio_has_feature(vdev, VIRTIO_CONSOLE_F_MULTIPORT)) {
		multiport = true;
		vdev->features[0] |= 1 << VIRTIO_CONSOLE_F_MULTIPORT;

		vdev->config->get(vdev,
				  offsetof(struct virtio_console_multiport_conf,
					   nr_ports),
				  &portdev->config.nr_ports,
				  sizeof(portdev->config.nr_ports));
		vdev->config->get(vdev,
				  offsetof(struct virtio_console_multiport_conf,
					   max_nr_ports),
				  &portdev->config.max_nr_ports,
				  sizeof(portdev->config.max_nr_ports));
		if (portdev->config.nr_ports > portdev->config.max_nr_ports) {
			dev_warn(&vdev->dev,
				 "More ports (%u) specified than allowed (%u). Will init %u ports.",
				 portdev->config.nr_ports,
				 portdev->config.max_nr_ports,
				 portdev->config.max_nr_ports);

			portdev->config.nr_ports = portdev->config.max_nr_ports;
		}
	}

	/* Let the Host know we support multiple ports.*/
	vdev->config->finalize_features(vdev);
#endif

	err = init_vqs(portdev);
	if (err < 0) {
		dev_err(&vdev->dev, "Error %d initializing vqs\n", err);
		goto free_chrdev;
	}

	spin_lock_init(&portdev->ports_lock);
	INIT_LIST_HEAD(&portdev->ports);

	if (multiport) {
		unsigned int nr_added_bufs;

		spin_lock_init(&portdev->cvq_lock);
		INIT_WORK(&portdev->control_work, &control_work_handler);
		INIT_WORK(&portdev->config_work, &config_work_handler);

		nr_added_bufs = fill_queue(portdev->c_ivq, &portdev->cvq_lock);
		if (!nr_added_bufs) {
			dev_err(&vdev->dev,
				"Error allocating buffers for control queue\n");
			err = -ENOMEM;
			goto free_vqs;
		}
	}

	for (i = 0; i < portdev->config.nr_ports; i++)
		add_port(portdev, i);

	/* Start using the new console output. */
	early_put_chars = NULL;
	return 0;

free_vqs:
	vdev->config->del_vqs(vdev);
	kfree(portdev->in_vqs);
	kfree(portdev->out_vqs);
free_chrdev:
	unregister_chrdev(portdev->chr_major, "virtio-portsdev");
free:
	kfree(portdev);
fail:
	return err;
}

static void virtcons_remove(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	struct port *port, *port2;
	struct port_buffer *buf;
	unsigned int len;

	portdev = vdev->priv;

	cancel_work_sync(&portdev->control_work);
	cancel_work_sync(&portdev->config_work);

	list_for_each_entry_safe(port, port2, &portdev->ports, list)
		remove_port(port);

	unregister_chrdev(portdev->chr_major, "virtio-portsdev");

	while ((buf = portdev->c_ivq->vq_ops->get_buf(portdev->c_ivq, &len)))
		free_buf(buf);

	while ((buf = portdev->c_ivq->vq_ops->detach_unused_buf(portdev->c_ivq)))
		free_buf(buf);

	vdev->config->del_vqs(vdev);
	kfree(portdev->in_vqs);
	kfree(portdev->out_vqs);

	kfree(portdev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CONSOLE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CONSOLE_F_SIZE,
};

static struct virtio_driver virtio_console = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtcons_probe,
	.remove =	virtcons_remove,
	.config_changed = config_intr,
};

static int __init init(void)
{
	int err;

	pdrvdata.class = class_create(THIS_MODULE, "virtio-ports");
	if (IS_ERR(pdrvdata.class)) {
		err = PTR_ERR(pdrvdata.class);
		pr_err("Error %d creating virtio-ports class\n", err);
		return err;
	}

	pdrvdata.debugfs_dir = debugfs_create_dir("virtio-ports", NULL);
	if (!pdrvdata.debugfs_dir) {
		pr_warning("Error %ld creating debugfs dir for virtio-ports\n",
			   PTR_ERR(pdrvdata.debugfs_dir));
	}
	INIT_LIST_HEAD(&pdrvdata.consoles);

	return register_virtio_driver(&virtio_console);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_console);

	class_destroy(pdrvdata.class);
	if (pdrvdata.debugfs_dir)
		debugfs_remove_recursive(pdrvdata.debugfs_dir);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio console driver");
MODULE_LICENSE("GPL");
