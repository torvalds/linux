/*
 * Copyright (C) 2006, 2007, 2009 Rusty Russell, IBM Corporation
 * Copyright (C) 2009, 2010, 2011 Red Hat, Inc.
 * Copyright (C) 2009, 2010, 2011 Amit Shah <amit.shah@redhat.com>
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
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/splice.h>
#include <linux/pagemap.h>
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
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include "../tty/hvc/hvc_console.h"

#define is_rproc_enabled IS_ENABLED(CONFIG_REMOTEPROC)

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

	/* List of all the devices we're handling */
	struct list_head portdevs;

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

static DEFINE_SPINLOCK(pdrvdata_lock);
static DECLARE_COMPLETION(early_console_added);

/* This struct holds information that's relevant only for console ports */
struct console {
	/* We'll place all consoles in a list in the pdrvdata struct */
	struct list_head list;

	/* The hvc device associated with this console port */
	struct hvc_struct *hvc;

	/* The size of the console */
	struct winsize ws;

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

	/* DMA address of buffer */
	dma_addr_t dma;

	/* Device we got DMA memory from */
	struct device *dev;

	/* List of pending dma buffers to free */
	struct list_head list;

	/* If sgpages == 0 then buf is used */
	unsigned int sgpages;

	/* sg is used if spages > 0. sg must be the last in is struct */
	struct scatterlist sg[0];
};

/*
 * This is a per-device struct that stores data common to all the
 * ports for that device (vdev->priv).
 */
struct ports_device {
	/* Next portdev in the list, head is in the pdrvdata struct */
	struct list_head list;

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
	spinlock_t c_ivq_lock;
	spinlock_t c_ovq_lock;

	/* max. number of ports this device can hold */
	u32 max_nr_ports;

	/* The virtio device we're associated with */
	struct virtio_device *vdev;

	/*
	 * A couple of virtqueues for the control channel: one for
	 * guest->host transfers, one for host->guest transfers
	 */
	struct virtqueue *c_ivq, *c_ovq;

	/*
	 * A control packet buffer for guest->host requests, protected
	 * by c_ovq_lock.
	 */
	struct virtio_console_control cpkt;

	/* Array of per-port IO virtqueues */
	struct virtqueue **in_vqs, **out_vqs;

	/* Major number for this device.  Ports will be created as minors. */
	int chr_major;
};

struct port_stats {
	unsigned long bytes_sent, bytes_received, bytes_discarded;
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

	/* Protect the operations on the out_vq. */
	spinlock_t outvq_lock;

	/* The IO vqs for this port */
	struct virtqueue *in_vq, *out_vq;

	/* File in the debugfs directory that exposes this port's information */
	struct dentry *debugfs_file;

	/*
	 * Keep count of the bytes sent, received and discarded for
	 * this port for accounting and debugging purposes.  These
	 * counts are not reset across port open / close events.
	 */
	struct port_stats stats;

	/*
	 * The entries in this struct will be valid if this port is
	 * hooked up to an hvc console
	 */
	struct console cons;

	/* Each port associates with a separate char device */
	struct cdev *cdev;
	struct device *dev;

	/* Reference-counting to handle port hot-unplugs and file operations */
	struct kref kref;

	/* A waitqueue for poll() or blocking read operations */
	wait_queue_head_t waitqueue;

	/* The 'name' of the port that we expose via sysfs properties */
	char *name;

	/* We can notify apps of host connect / disconnect events via SIGIO */
	struct fasync_struct *async_queue;

	/* The 'id' to identify the port with the Host */
	u32 id;

	bool outvq_full;

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

static struct port *find_port_by_devt_in_portdev(struct ports_device *portdev,
						 dev_t dev)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list) {
		if (port->cdev->dev == dev) {
			kref_get(&port->kref);
			goto out;
		}
	}
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);

	return port;
}

static struct port *find_port_by_devt(dev_t dev)
{
	struct ports_device *portdev;
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&pdrvdata_lock, flags);
	list_for_each_entry(portdev, &pdrvdata.portdevs, list) {
		port = find_port_by_devt_in_portdev(portdev, dev);
		if (port)
			goto out;
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

static bool is_rproc_serial(const struct virtio_device *vdev)
{
	return is_rproc_enabled && vdev->id.device == VIRTIO_ID_RPROC_SERIAL;
}

static inline bool use_multiport(struct ports_device *portdev)
{
	/*
	 * This condition can be true when put_chars is called from
	 * early_init
	 */
	if (!portdev->vdev)
		return false;
	return __virtio_test_bit(portdev->vdev, VIRTIO_CONSOLE_F_MULTIPORT);
}

static DEFINE_SPINLOCK(dma_bufs_lock);
static LIST_HEAD(pending_free_dma_bufs);

static void free_buf(struct port_buffer *buf, bool can_sleep)
{
	unsigned int i;

	for (i = 0; i < buf->sgpages; i++) {
		struct page *page = sg_page(&buf->sg[i]);
		if (!page)
			break;
		put_page(page);
	}

	if (!buf->dev) {
		kfree(buf->buf);
	} else if (is_rproc_enabled) {
		unsigned long flags;

		/* dma_free_coherent requires interrupts to be enabled. */
		if (!can_sleep) {
			/* queue up dma-buffers to be freed later */
			spin_lock_irqsave(&dma_bufs_lock, flags);
			list_add_tail(&buf->list, &pending_free_dma_bufs);
			spin_unlock_irqrestore(&dma_bufs_lock, flags);
			return;
		}
		dma_free_coherent(buf->dev, buf->size, buf->buf, buf->dma);

		/* Release device refcnt and allow it to be freed */
		put_device(buf->dev);
	}

	kfree(buf);
}

static void reclaim_dma_bufs(void)
{
	unsigned long flags;
	struct port_buffer *buf, *tmp;
	LIST_HEAD(tmp_list);

	if (list_empty(&pending_free_dma_bufs))
		return;

	/* Create a copy of the pending_free_dma_bufs while holding the lock */
	spin_lock_irqsave(&dma_bufs_lock, flags);
	list_cut_position(&tmp_list, &pending_free_dma_bufs,
			  pending_free_dma_bufs.prev);
	spin_unlock_irqrestore(&dma_bufs_lock, flags);

	/* Release the dma buffers, without irqs enabled */
	list_for_each_entry_safe(buf, tmp, &tmp_list, list) {
		list_del(&buf->list);
		free_buf(buf, true);
	}
}

static struct port_buffer *alloc_buf(struct virtqueue *vq, size_t buf_size,
				     int pages)
{
	struct port_buffer *buf;

	reclaim_dma_bufs();

	/*
	 * Allocate buffer and the sg list. The sg list array is allocated
	 * directly after the port_buffer struct.
	 */
	buf = kmalloc(sizeof(*buf) + sizeof(struct scatterlist) * pages,
		      GFP_KERNEL);
	if (!buf)
		goto fail;

	buf->sgpages = pages;
	if (pages > 0) {
		buf->dev = NULL;
		buf->buf = NULL;
		return buf;
	}

	if (is_rproc_serial(vq->vdev)) {
		/*
		 * Allocate DMA memory from ancestor. When a virtio
		 * device is created by remoteproc, the DMA memory is
		 * associated with the grandparent device:
		 * vdev => rproc => platform-dev.
		 * The code here would have been less quirky if
		 * DMA_MEMORY_INCLUDES_CHILDREN had been supported
		 * in dma-coherent.c
		 */
		if (!vq->vdev->dev.parent || !vq->vdev->dev.parent->parent)
			goto free_buf;
		buf->dev = vq->vdev->dev.parent->parent;

		/* Increase device refcnt to avoid freeing it */
		get_device(buf->dev);
		buf->buf = dma_alloc_coherent(buf->dev, buf_size, &buf->dma,
					      GFP_KERNEL);
	} else {
		buf->dev = NULL;
		buf->buf = kmalloc(buf_size, GFP_KERNEL);
	}

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
static struct port_buffer *get_inbuf(struct port *port)
{
	struct port_buffer *buf;
	unsigned int len;

	if (port->inbuf)
		return port->inbuf;

	buf = virtqueue_get_buf(port->in_vq, &len);
	if (buf) {
		buf->len = len;
		buf->offset = 0;
		port->stats.bytes_received += len;
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

	ret = virtqueue_add_inbuf(vq, sg, 1, buf, GFP_ATOMIC);
	virtqueue_kick(vq);
	if (!ret)
		ret = vq->num_free;
	return ret;
}

/* Discard any unread data this port has. Callers lockers. */
static void discard_port_data(struct port *port)
{
	struct port_buffer *buf;
	unsigned int err;

	if (!port->portdev) {
		/* Device has been unplugged.  vqs are already gone. */
		return;
	}
	buf = get_inbuf(port);

	err = 0;
	while (buf) {
		port->stats.bytes_discarded += buf->len - buf->offset;
		if (add_inbuf(port->in_vq, buf) < 0) {
			err++;
			free_buf(buf, false);
		}
		port->inbuf = NULL;
		buf = get_inbuf(port);
	}
	if (err)
		dev_warn(port->dev, "Errors adding %d buffers back to vq\n",
			 err);
}

static bool port_has_data(struct port *port)
{
	unsigned long flags;
	bool ret;

	ret = false;
	spin_lock_irqsave(&port->inbuf_lock, flags);
	port->inbuf = get_inbuf(port);
	if (port->inbuf)
		ret = true;

	spin_unlock_irqrestore(&port->inbuf_lock, flags);
	return ret;
}

static ssize_t __send_control_msg(struct ports_device *portdev, u32 port_id,
				  unsigned int event, unsigned int value)
{
	struct scatterlist sg[1];
	struct virtqueue *vq;
	unsigned int len;

	if (!use_multiport(portdev))
		return 0;

	vq = portdev->c_ovq;

	spin_lock(&portdev->c_ovq_lock);

	portdev->cpkt.id = cpu_to_virtio32(portdev->vdev, port_id);
	portdev->cpkt.event = cpu_to_virtio16(portdev->vdev, event);
	portdev->cpkt.value = cpu_to_virtio16(portdev->vdev, value);

	sg_init_one(sg, &portdev->cpkt, sizeof(struct virtio_console_control));

	if (virtqueue_add_outbuf(vq, sg, 1, &portdev->cpkt, GFP_ATOMIC) == 0) {
		virtqueue_kick(vq);
		while (!virtqueue_get_buf(vq, &len)
			&& !virtqueue_is_broken(vq))
			cpu_relax();
	}

	spin_unlock(&portdev->c_ovq_lock);
	return 0;
}

static ssize_t send_control_msg(struct port *port, unsigned int event,
				unsigned int value)
{
	/* Did the port get unplugged before userspace closed it? */
	if (port->portdev)
		return __send_control_msg(port->portdev, port->id, event, value);
	return 0;
}


/* Callers must take the port->outvq_lock */
static void reclaim_consumed_buffers(struct port *port)
{
	struct port_buffer *buf;
	unsigned int len;

	if (!port->portdev) {
		/* Device has been unplugged.  vqs are already gone. */
		return;
	}
	while ((buf = virtqueue_get_buf(port->out_vq, &len))) {
		free_buf(buf, false);
		port->outvq_full = false;
	}
}

static ssize_t __send_to_port(struct port *port, struct scatterlist *sg,
			      int nents, size_t in_count,
			      void *data, bool nonblock)
{
	struct virtqueue *out_vq;
	int err;
	unsigned long flags;
	unsigned int len;

	out_vq = port->out_vq;

	spin_lock_irqsave(&port->outvq_lock, flags);

	reclaim_consumed_buffers(port);

	err = virtqueue_add_outbuf(out_vq, sg, nents, data, GFP_ATOMIC);

	/* Tell Host to go! */
	virtqueue_kick(out_vq);

	if (err) {
		in_count = 0;
		goto done;
	}

	if (out_vq->num_free == 0)
		port->outvq_full = true;

	if (nonblock)
		goto done;

	/*
	 * Wait till the host acknowledges it pushed out the data we
	 * sent.  This is done for data from the hvc_console; the tty
	 * operations are performed with spinlocks held so we can't
	 * sleep here.  An alternative would be to copy the data to a
	 * buffer and relax the spinning requirement.  The downside is
	 * we need to kmalloc a GFP_ATOMIC buffer each time the
	 * console driver writes something out.
	 */
	while (!virtqueue_get_buf(out_vq, &len)
		&& !virtqueue_is_broken(out_vq))
		cpu_relax();
done:
	spin_unlock_irqrestore(&port->outvq_lock, flags);

	port->stats.bytes_sent += in_count;
	/*
	 * We're expected to return the amount of data we wrote -- all
	 * of it
	 */
	return in_count;
}

/*
 * Give out the data that's requested from the buffer that we have
 * queued up.
 */
static ssize_t fill_readbuf(struct port *port, char __user *out_buf,
			    size_t out_count, bool to_user)
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
		memcpy((__force char *)out_buf, buf->buf + buf->offset,
		       out_count);
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
static bool will_read_block(struct port *port)
{
	if (!port->guest_connected) {
		/* Port got hot-unplugged. Let's exit. */
		return false;
	}
	return !port_has_data(port) && port->host_connected;
}

static bool will_write_block(struct port *port)
{
	bool ret;

	if (!port->guest_connected) {
		/* Port got hot-unplugged. Let's exit. */
		return false;
	}
	if (!port->host_connected)
		return true;

	spin_lock_irq(&port->outvq_lock);
	/*
	 * Check if the Host has consumed any buffers since we last
	 * sent data (this is only applicable for nonblocking ports).
	 */
	reclaim_consumed_buffers(port);
	ret = port->outvq_full;
	spin_unlock_irq(&port->outvq_lock);

	return ret;
}

static ssize_t port_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp)
{
	struct port *port;
	ssize_t ret;

	port = filp->private_data;

	/* Port is hot-unplugged. */
	if (!port->guest_connected)
		return -ENODEV;

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

		ret = wait_event_freezable(port->waitqueue,
					   !will_read_block(port));
		if (ret < 0)
			return ret;
	}
	/* Port got hot-unplugged while we were waiting above. */
	if (!port->guest_connected)
		return -ENODEV;
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

static int wait_port_writable(struct port *port, bool nonblock)
{
	int ret;

	if (will_write_block(port)) {
		if (nonblock)
			return -EAGAIN;

		ret = wait_event_freezable(port->waitqueue,
					   !will_write_block(port));
		if (ret < 0)
			return ret;
	}
	/* Port got hot-unplugged. */
	if (!port->guest_connected)
		return -ENODEV;

	return 0;
}

static ssize_t port_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp)
{
	struct port *port;
	struct port_buffer *buf;
	ssize_t ret;
	bool nonblock;
	struct scatterlist sg[1];

	/* Userspace could be out to fool us */
	if (!count)
		return 0;

	port = filp->private_data;

	nonblock = filp->f_flags & O_NONBLOCK;

	ret = wait_port_writable(port, nonblock);
	if (ret < 0)
		return ret;

	count = min((size_t)(32 * 1024), count);

	buf = alloc_buf(port->out_vq, count, 0);
	if (!buf)
		return -ENOMEM;

	ret = copy_from_user(buf->buf, ubuf, count);
	if (ret) {
		ret = -EFAULT;
		goto free_buf;
	}

	/*
	 * We now ask send_buf() to not spin for generic ports -- we
	 * can re-use the same code path that non-blocking file
	 * descriptors take for blocking file descriptors since the
	 * wait is already done and we're certain the write will go
	 * through to the host.
	 */
	nonblock = true;
	sg_init_one(sg, buf->buf, count);
	ret = __send_to_port(port, sg, 1, count, buf, nonblock);

	if (nonblock && ret > 0)
		goto out;

free_buf:
	free_buf(buf, true);
out:
	return ret;
}

struct sg_list {
	unsigned int n;
	unsigned int size;
	size_t len;
	struct scatterlist *sg;
};

static int pipe_to_sg(struct pipe_inode_info *pipe, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	struct sg_list *sgl = sd->u.data;
	unsigned int offset, len;

	if (sgl->n == sgl->size)
		return 0;

	/* Try lock this page */
	if (pipe_buf_steal(pipe, buf) == 0) {
		/* Get reference and unlock page for moving */
		get_page(buf->page);
		unlock_page(buf->page);

		len = min(buf->len, sd->len);
		sg_set_page(&(sgl->sg[sgl->n]), buf->page, len, buf->offset);
	} else {
		/* Failback to copying a page */
		struct page *page = alloc_page(GFP_KERNEL);
		char *src;

		if (!page)
			return -ENOMEM;

		offset = sd->pos & ~PAGE_MASK;

		len = sd->len;
		if (len + offset > PAGE_SIZE)
			len = PAGE_SIZE - offset;

		src = kmap_atomic(buf->page);
		memcpy(page_address(page) + offset, src + buf->offset, len);
		kunmap_atomic(src);

		sg_set_page(&(sgl->sg[sgl->n]), page, len, offset);
	}
	sgl->n++;
	sgl->len += len;

	return len;
}

/* Faster zero-copy write by splicing */
static ssize_t port_fops_splice_write(struct pipe_inode_info *pipe,
				      struct file *filp, loff_t *ppos,
				      size_t len, unsigned int flags)
{
	struct port *port = filp->private_data;
	struct sg_list sgl;
	ssize_t ret;
	struct port_buffer *buf;
	struct splice_desc sd = {
		.total_len = len,
		.flags = flags,
		.pos = *ppos,
		.u.data = &sgl,
	};

	/*
	 * Rproc_serial does not yet support splice. To support splice
	 * pipe_to_sg() must allocate dma-buffers and copy content from
	 * regular pages to dma pages. And alloc_buf and free_buf must
	 * support allocating and freeing such a list of dma-buffers.
	 */
	if (is_rproc_serial(port->out_vq->vdev))
		return -EINVAL;

	/*
	 * pipe->nrbufs == 0 means there are no data to transfer,
	 * so this returns just 0 for no data.
	 */
	pipe_lock(pipe);
	if (!pipe->nrbufs) {
		ret = 0;
		goto error_out;
	}

	ret = wait_port_writable(port, filp->f_flags & O_NONBLOCK);
	if (ret < 0)
		goto error_out;

	buf = alloc_buf(port->out_vq, 0, pipe->nrbufs);
	if (!buf) {
		ret = -ENOMEM;
		goto error_out;
	}

	sgl.n = 0;
	sgl.len = 0;
	sgl.size = pipe->nrbufs;
	sgl.sg = buf->sg;
	sg_init_table(sgl.sg, sgl.size);
	ret = __splice_from_pipe(pipe, &sd, pipe_to_sg);
	pipe_unlock(pipe);
	if (likely(ret > 0))
		ret = __send_to_port(port, buf->sg, sgl.n, sgl.len, buf, true);

	if (unlikely(ret <= 0))
		free_buf(buf, true);
	return ret;

error_out:
	pipe_unlock(pipe);
	return ret;
}

static unsigned int port_fops_poll(struct file *filp, poll_table *wait)
{
	struct port *port;
	unsigned int ret;

	port = filp->private_data;
	poll_wait(filp, &port->waitqueue, wait);

	if (!port->guest_connected) {
		/* Port got unplugged */
		return POLLHUP;
	}
	ret = 0;
	if (!will_read_block(port))
		ret |= POLLIN | POLLRDNORM;
	if (!will_write_block(port))
		ret |= POLLOUT;
	if (!port->host_connected)
		ret |= POLLHUP;

	return ret;
}

static void remove_port(struct kref *kref);

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

	spin_lock_irq(&port->outvq_lock);
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);

	reclaim_dma_bufs();
	/*
	 * Locks aren't necessary here as a port can't be opened after
	 * unplug, and if a port isn't unplugged, a kref would already
	 * exist for the port.  Plus, taking ports_lock here would
	 * create a dependency on other locks taken by functions
	 * inside remove_port if we're the last holder of the port,
	 * creating many problems.
	 */
	kref_put(&port->kref, remove_port);

	return 0;
}

static int port_fops_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct port *port;
	int ret;

	/* We get the port with a kref here */
	port = find_port_by_devt(cdev->dev);
	if (!port) {
		/* Port was unplugged before we could proceed */
		return -ENXIO;
	}
	filp->private_data = port;

	/*
	 * Don't allow opening of console port devices -- that's done
	 * via /dev/hvc
	 */
	if (is_console_port(port)) {
		ret = -ENXIO;
		goto out;
	}

	/* Allow only one process to open a particular port at a time */
	spin_lock_irq(&port->inbuf_lock);
	if (port->guest_connected) {
		spin_unlock_irq(&port->inbuf_lock);
		ret = -EBUSY;
		goto out;
	}

	port->guest_connected = true;
	spin_unlock_irq(&port->inbuf_lock);

	spin_lock_irq(&port->outvq_lock);
	/*
	 * There might be a chance that we missed reclaiming a few
	 * buffers in the window of the port getting previously closed
	 * and opening now.
	 */
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);

	nonseekable_open(inode, filp);

	/* Notify host of port being opened */
	send_control_msg(filp->private_data, VIRTIO_CONSOLE_PORT_OPEN, 1);

	return 0;
out:
	kref_put(&port->kref, remove_port);
	return ret;
}

static int port_fops_fasync(int fd, struct file *filp, int mode)
{
	struct port *port;

	port = filp->private_data;
	return fasync_helper(fd, filp, mode, &port->async_queue);
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
	.splice_write = port_fops_splice_write,
	.poll  = port_fops_poll,
	.release = port_fops_release,
	.fasync = port_fops_fasync,
	.llseek = no_llseek,
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
	struct scatterlist sg[1];
	void *data;
	int ret;

	if (unlikely(early_put_chars))
		return early_put_chars(vtermno, buf, count);

	port = find_port_by_vtermno(vtermno);
	if (!port)
		return -EPIPE;

	data = kmemdup(buf, count, GFP_ATOMIC);
	if (!data)
		return -ENOMEM;

	sg_init_one(sg, data, count);
	ret = __send_to_port(port, sg, 1, count, data, false);
	kfree(data);
	return ret;
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

	/* If we've not set up the port yet, we have no input to give. */
	if (unlikely(early_put_chars))
		return 0;

	port = find_port_by_vtermno(vtermno);
	if (!port)
		return -EPIPE;

	/* If we don't have an input queue yet, we can't get input. */
	BUG_ON(!port->in_vq);

	return fill_readbuf(port, (__force char __user *)buf, count, false);
}

static void resize_console(struct port *port)
{
	struct virtio_device *vdev;

	/* The port could have been hot-unplugged */
	if (!port || !is_console_port(port))
		return;

	vdev = port->portdev->vdev;

	/* Don't test F_SIZE at all if we're rproc: not a valid feature! */
	if (!is_rproc_serial(vdev) &&
	    virtio_has_feature(vdev, VIRTIO_CONSOLE_F_SIZE))
		hvc_resize(port->cons.hvc, port->cons.ws);
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

static int init_port_console(struct port *port)
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

	/*
	 * Start using the new console output if this is the first
	 * console to come up.
	 */
	if (early_put_chars)
		early_put_chars = NULL;

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
			       "outvq_full: %d\n", port->outvq_full);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "bytes_sent: %lu\n", port->stats.bytes_sent);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "bytes_received: %lu\n",
			       port->stats.bytes_received);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "bytes_discarded: %lu\n",
			       port->stats.bytes_discarded);
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
	.open  = simple_open,
	.read  = debugfs_read,
};

static void set_console_size(struct port *port, u16 rows, u16 cols)
{
	if (!port || !is_console_port(port))
		return;

	port->cons.ws.ws_row = rows;
	port->cons.ws.ws_col = cols;
}

static unsigned int fill_queue(struct virtqueue *vq, spinlock_t *lock)
{
	struct port_buffer *buf;
	unsigned int nr_added_bufs;
	int ret;

	nr_added_bufs = 0;
	do {
		buf = alloc_buf(vq, PAGE_SIZE, 0);
		if (!buf)
			break;

		spin_lock_irq(lock);
		ret = add_inbuf(vq, buf);
		if (ret < 0) {
			spin_unlock_irq(lock);
			free_buf(buf, true);
			break;
		}
		nr_added_bufs++;
		spin_unlock_irq(lock);
	} while (ret > 0);

	return nr_added_bufs;
}

static void send_sigio_to_port(struct port *port)
{
	if (port->async_queue && port->guest_connected)
		kill_fasync(&port->async_queue, SIGIO, POLL_OUT);
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
	kref_init(&port->kref);

	port->portdev = portdev;
	port->id = id;

	port->name = NULL;
	port->inbuf = NULL;
	port->cons.hvc = NULL;
	port->async_queue = NULL;

	port->cons.ws.ws_row = port->cons.ws.ws_col = 0;

	port->host_connected = port->guest_connected = false;
	port->stats = (struct port_stats) { 0 };

	port->outvq_full = false;

	port->in_vq = portdev->in_vqs[port->id];
	port->out_vq = portdev->out_vqs[port->id];

	port->cdev = cdev_alloc();
	if (!port->cdev) {
		dev_err(&port->portdev->vdev->dev, "Error allocating cdev\n");
		err = -ENOMEM;
		goto free_port;
	}
	port->cdev->ops = &port_fops;

	devt = MKDEV(portdev->chr_major, id);
	err = cdev_add(port->cdev, devt, 1);
	if (err < 0) {
		dev_err(&port->portdev->vdev->dev,
			"Error %d adding cdev for port %u\n", err, id);
		goto free_cdev;
	}
	port->dev = device_create(pdrvdata.class, &port->portdev->vdev->dev,
				  devt, port, "vport%up%u",
				  port->portdev->vdev->index, id);
	if (IS_ERR(port->dev)) {
		err = PTR_ERR(port->dev);
		dev_err(&port->portdev->vdev->dev,
			"Error %d creating device for port %u\n",
			err, id);
		goto free_cdev;
	}

	spin_lock_init(&port->inbuf_lock);
	spin_lock_init(&port->outvq_lock);
	init_waitqueue_head(&port->waitqueue);

	/* Fill the in_vq with buffers so the host can send us data. */
	nr_added_bufs = fill_queue(port->in_vq, &port->inbuf_lock);
	if (!nr_added_bufs) {
		dev_err(port->dev, "Error allocating inbufs\n");
		err = -ENOMEM;
		goto free_device;
	}

	if (is_rproc_serial(port->portdev->vdev))
		/*
		 * For rproc_serial assume remote processor is connected.
		 * rproc_serial does not want the console port, only
		 * the generic port implementation.
		 */
		port->host_connected = true;
	else if (!use_multiport(port->portdev)) {
		/*
		 * If we're not using multiport support,
		 * this has to be a console port.
		 */
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
		snprintf(debugfs_name, sizeof(debugfs_name), "vport%up%u",
			 port->portdev->vdev->index, id);
		port->debugfs_file = debugfs_create_file(debugfs_name, 0444,
							 pdrvdata.debugfs_dir,
							 port,
							 &port_debugfs_ops);
	}
	return 0;

free_inbufs:
	while ((buf = virtqueue_detach_unused_buf(port->in_vq)))
		free_buf(buf, true);
free_device:
	device_destroy(pdrvdata.class, port->dev->devt);
free_cdev:
	cdev_del(port->cdev);
free_port:
	kfree(port);
fail:
	/* The host might want to notify management sw about port add failure */
	__send_control_msg(portdev, id, VIRTIO_CONSOLE_PORT_READY, 0);
	return err;
}

/* No users remain, remove all port-specific data. */
static void remove_port(struct kref *kref)
{
	struct port *port;

	port = container_of(kref, struct port, kref);

	kfree(port);
}

static void remove_port_data(struct port *port)
{
	struct port_buffer *buf;

	spin_lock_irq(&port->inbuf_lock);
	/* Remove unused data this port might have received. */
	discard_port_data(port);
	spin_unlock_irq(&port->inbuf_lock);

	/* Remove buffers we queued up for the Host to send us data in. */
	do {
		spin_lock_irq(&port->inbuf_lock);
		buf = virtqueue_detach_unused_buf(port->in_vq);
		spin_unlock_irq(&port->inbuf_lock);
		if (buf)
			free_buf(buf, true);
	} while (buf);

	spin_lock_irq(&port->outvq_lock);
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);

	/* Free pending buffers from the out-queue. */
	do {
		spin_lock_irq(&port->outvq_lock);
		buf = virtqueue_detach_unused_buf(port->out_vq);
		spin_unlock_irq(&port->outvq_lock);
		if (buf)
			free_buf(buf, true);
	} while (buf);
}

/*
 * Port got unplugged.  Remove port from portdev's list and drop the
 * kref reference.  If no userspace has this port opened, it will
 * result in immediate removal the port.
 */
static void unplug_port(struct port *port)
{
	spin_lock_irq(&port->portdev->ports_lock);
	list_del(&port->list);
	spin_unlock_irq(&port->portdev->ports_lock);

	spin_lock_irq(&port->inbuf_lock);
	if (port->guest_connected) {
		/* Let the app know the port is going down. */
		send_sigio_to_port(port);

		/* Do this after sigio is actually sent */
		port->guest_connected = false;
		port->host_connected = false;

		wake_up_interruptible(&port->waitqueue);
	}
	spin_unlock_irq(&port->inbuf_lock);

	if (is_console_port(port)) {
		spin_lock_irq(&pdrvdata_lock);
		list_del(&port->cons.list);
		spin_unlock_irq(&pdrvdata_lock);
		hvc_remove(port->cons.hvc);
	}

	remove_port_data(port);

	/*
	 * We should just assume the device itself has gone off --
	 * else a close on an open port later will try to send out a
	 * control message.
	 */
	port->portdev = NULL;

	sysfs_remove_group(&port->dev->kobj, &port_attribute_group);
	device_destroy(pdrvdata.class, port->dev->devt);
	cdev_del(port->cdev);

	debugfs_remove(port->debugfs_file);
	kfree(port->name);

	/*
	 * Locks around here are not necessary - a port can't be
	 * opened after we removed the port struct from ports_list
	 * above.
	 */
	kref_put(&port->kref, remove_port);
}

/* Any private messages that the Host and Guest want to share */
static void handle_control_message(struct virtio_device *vdev,
				   struct ports_device *portdev,
				   struct port_buffer *buf)
{
	struct virtio_console_control *cpkt;
	struct port *port;
	size_t name_size;
	int err;

	cpkt = (struct virtio_console_control *)(buf->buf + buf->offset);

	port = find_port_by_id(portdev, virtio32_to_cpu(vdev, cpkt->id));
	if (!port &&
	    cpkt->event != cpu_to_virtio16(vdev, VIRTIO_CONSOLE_PORT_ADD)) {
		/* No valid header at start of buffer.  Drop it. */
		dev_dbg(&portdev->vdev->dev,
			"Invalid index %u in control packet\n", cpkt->id);
		return;
	}

	switch (virtio16_to_cpu(vdev, cpkt->event)) {
	case VIRTIO_CONSOLE_PORT_ADD:
		if (port) {
			dev_dbg(&portdev->vdev->dev,
				"Port %u already added\n", port->id);
			send_control_msg(port, VIRTIO_CONSOLE_PORT_READY, 1);
			break;
		}
		if (virtio32_to_cpu(vdev, cpkt->id) >=
		    portdev->max_nr_ports) {
			dev_warn(&portdev->vdev->dev,
				"Request for adding port with "
				"out-of-bound id %u, max. supported id: %u\n",
				cpkt->id, portdev->max_nr_ports - 1);
			break;
		}
		add_port(portdev, virtio32_to_cpu(vdev, cpkt->id));
		break;
	case VIRTIO_CONSOLE_PORT_REMOVE:
		unplug_port(port);
		break;
	case VIRTIO_CONSOLE_CONSOLE_PORT:
		if (!cpkt->value)
			break;
		if (is_console_port(port))
			break;

		init_port_console(port);
		complete(&early_console_added);
		/*
		 * Could remove the port here in case init fails - but
		 * have to notify the host first.
		 */
		break;
	case VIRTIO_CONSOLE_RESIZE: {
		struct {
			__u16 rows;
			__u16 cols;
		} size;

		if (!is_console_port(port))
			break;

		memcpy(&size, buf->buf + buf->offset + sizeof(*cpkt),
		       sizeof(size));
		set_console_size(port, size.rows, size.cols);

		port->cons.hvc->irq_requested = 1;
		resize_console(port);
		break;
	}
	case VIRTIO_CONSOLE_PORT_OPEN:
		port->host_connected = virtio16_to_cpu(vdev, cpkt->value);
		wake_up_interruptible(&port->waitqueue);
		/*
		 * If the host port got closed and the host had any
		 * unconsumed buffers, we'll be able to reclaim them
		 * now.
		 */
		spin_lock_irq(&port->outvq_lock);
		reclaim_consumed_buffers(port);
		spin_unlock_irq(&port->outvq_lock);

		/*
		 * If the guest is connected, it'll be interested in
		 * knowing the host connection state changed.
		 */
		spin_lock_irq(&port->inbuf_lock);
		send_sigio_to_port(port);
		spin_unlock_irq(&port->inbuf_lock);
		break;
	case VIRTIO_CONSOLE_PORT_NAME:
		/*
		 * If we woke up after hibernation, we can get this
		 * again.  Skip it in that case.
		 */
		if (port->name)
			break;

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

	spin_lock(&portdev->c_ivq_lock);
	while ((buf = virtqueue_get_buf(vq, &len))) {
		spin_unlock(&portdev->c_ivq_lock);

		buf->len = len;
		buf->offset = 0;

		handle_control_message(vq->vdev, portdev, buf);

		spin_lock(&portdev->c_ivq_lock);
		if (add_inbuf(portdev->c_ivq, buf) < 0) {
			dev_warn(&portdev->vdev->dev,
				 "Error adding buffer to queue\n");
			free_buf(buf, false);
		}
	}
	spin_unlock(&portdev->c_ivq_lock);
}

static void out_intr(struct virtqueue *vq)
{
	struct port *port;

	port = find_port_by_vq(vq->vdev->priv, vq);
	if (!port)
		return;

	wake_up_interruptible(&port->waitqueue);
}

static void in_intr(struct virtqueue *vq)
{
	struct port *port;
	unsigned long flags;

	port = find_port_by_vq(vq->vdev->priv, vq);
	if (!port)
		return;

	spin_lock_irqsave(&port->inbuf_lock, flags);
	port->inbuf = get_inbuf(port);

	/*
	 * Normally the port should not accept data when the port is
	 * closed. For generic serial ports, the host won't (shouldn't)
	 * send data till the guest is connected. But this condition
	 * can be reached when a console port is not yet connected (no
	 * tty is spawned) and the other side sends out data over the
	 * vring, or when a remote devices start sending data before
	 * the ports are opened.
	 *
	 * A generic serial port will discard data if not connected,
	 * while console ports and rproc-serial ports accepts data at
	 * any time. rproc-serial is initiated with guest_connected to
	 * false because port_fops_open expects this. Console ports are
	 * hooked up with an HVC console and is initialized with
	 * guest_connected to true.
	 */

	if (!port->guest_connected && !is_rproc_serial(port->portdev->vdev))
		discard_port_data(port);

	/* Send a SIGIO indicating new data in case the process asked for it */
	send_sigio_to_port(port);

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

	if (!use_multiport(portdev))
		schedule_work(&portdev->config_work);
}

static void config_work_handler(struct work_struct *work)
{
	struct ports_device *portdev;

	portdev = container_of(work, struct ports_device, config_work);
	if (!use_multiport(portdev)) {
		struct virtio_device *vdev;
		struct port *port;
		u16 rows, cols;

		vdev = portdev->vdev;
		virtio_cread(vdev, struct virtio_console_config, cols, &cols);
		virtio_cread(vdev, struct virtio_console_config, rows, &rows);

		port = find_port_by_id(portdev, 0);
		set_console_size(port, rows, cols);

		/*
		 * We'll use this way of resizing only for legacy
		 * support.  For newer userspace
		 * (VIRTIO_CONSOLE_F_MULTPORT+), use control messages
		 * to indicate console size changes so that it can be
		 * done per-port.
		 */
		resize_console(port);
	}
}

static int init_vqs(struct ports_device *portdev)
{
	vq_callback_t **io_callbacks;
	char **io_names;
	struct virtqueue **vqs;
	u32 i, j, nr_ports, nr_queues;
	int err;

	nr_ports = portdev->max_nr_ports;
	nr_queues = use_multiport(portdev) ? (nr_ports + 1) * 2 : 2;

	vqs = kmalloc(nr_queues * sizeof(struct virtqueue *), GFP_KERNEL);
	io_callbacks = kmalloc(nr_queues * sizeof(vq_callback_t *), GFP_KERNEL);
	io_names = kmalloc(nr_queues * sizeof(char *), GFP_KERNEL);
	portdev->in_vqs = kmalloc(nr_ports * sizeof(struct virtqueue *),
				  GFP_KERNEL);
	portdev->out_vqs = kmalloc(nr_ports * sizeof(struct virtqueue *),
				   GFP_KERNEL);
	if (!vqs || !io_callbacks || !io_names || !portdev->in_vqs ||
	    !portdev->out_vqs) {
		err = -ENOMEM;
		goto free;
	}

	/*
	 * For backward compat (newer host but older guest), the host
	 * spawns a console port first and also inits the vqs for port
	 * 0 before others.
	 */
	j = 0;
	io_callbacks[j] = in_intr;
	io_callbacks[j + 1] = out_intr;
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
			io_callbacks[j + 1] = out_intr;
			io_names[j] = "input";
			io_names[j + 1] = "output";
		}
	}
	/* Find the queues. */
	err = virtio_find_vqs(portdev->vdev, nr_queues, vqs,
			      io_callbacks,
			      (const char **)io_names, NULL);
	if (err)
		goto free;

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
	kfree(io_names);
	kfree(io_callbacks);
	kfree(vqs);

	return 0;

free:
	kfree(portdev->out_vqs);
	kfree(portdev->in_vqs);
	kfree(io_names);
	kfree(io_callbacks);
	kfree(vqs);

	return err;
}

static const struct file_operations portdev_fops = {
	.owner = THIS_MODULE,
};

static void remove_vqs(struct ports_device *portdev)
{
	portdev->vdev->config->del_vqs(portdev->vdev);
	kfree(portdev->in_vqs);
	kfree(portdev->out_vqs);
}

static void remove_controlq_data(struct ports_device *portdev)
{
	struct port_buffer *buf;
	unsigned int len;

	if (!use_multiport(portdev))
		return;

	while ((buf = virtqueue_get_buf(portdev->c_ivq, &len)))
		free_buf(buf, true);

	while ((buf = virtqueue_detach_unused_buf(portdev->c_ivq)))
		free_buf(buf, true);
}

/*
 * Once we're further in boot, we get probed like any other virtio
 * device.
 *
 * If the host also supports multiple console ports, we check the
 * config space to see how many ports the host has spawned.  We
 * initialize each port found.
 */
static int virtcons_probe(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	int err;
	bool multiport;
	bool early = early_put_chars != NULL;

	/* We only need a config space if features are offered */
	if (!vdev->config->get &&
	    (virtio_has_feature(vdev, VIRTIO_CONSOLE_F_SIZE)
	     || virtio_has_feature(vdev, VIRTIO_CONSOLE_F_MULTIPORT))) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	/* Ensure to read early_put_chars now */
	barrier();

	portdev = kmalloc(sizeof(*portdev), GFP_KERNEL);
	if (!portdev) {
		err = -ENOMEM;
		goto fail;
	}

	/* Attach this portdev to this virtio_device, and vice-versa. */
	portdev->vdev = vdev;
	vdev->priv = portdev;

	portdev->chr_major = register_chrdev(0, "virtio-portsdev",
					     &portdev_fops);
	if (portdev->chr_major < 0) {
		dev_err(&vdev->dev,
			"Error %d registering chrdev for device %u\n",
			portdev->chr_major, vdev->index);
		err = portdev->chr_major;
		goto free;
	}

	multiport = false;
	portdev->max_nr_ports = 1;

	/* Don't test MULTIPORT at all if we're rproc: not a valid feature! */
	if (!is_rproc_serial(vdev) &&
	    virtio_cread_feature(vdev, VIRTIO_CONSOLE_F_MULTIPORT,
				 struct virtio_console_config, max_nr_ports,
				 &portdev->max_nr_ports) == 0) {
		multiport = true;
	}

	err = init_vqs(portdev);
	if (err < 0) {
		dev_err(&vdev->dev, "Error %d initializing vqs\n", err);
		goto free_chrdev;
	}

	spin_lock_init(&portdev->ports_lock);
	INIT_LIST_HEAD(&portdev->ports);

	virtio_device_ready(portdev->vdev);

	INIT_WORK(&portdev->config_work, &config_work_handler);
	INIT_WORK(&portdev->control_work, &control_work_handler);

	if (multiport) {
		unsigned int nr_added_bufs;

		spin_lock_init(&portdev->c_ivq_lock);
		spin_lock_init(&portdev->c_ovq_lock);

		nr_added_bufs = fill_queue(portdev->c_ivq,
					   &portdev->c_ivq_lock);
		if (!nr_added_bufs) {
			dev_err(&vdev->dev,
				"Error allocating buffers for control queue\n");
			err = -ENOMEM;
			goto free_vqs;
		}
	} else {
		/*
		 * For backward compatibility: Create a console port
		 * if we're running on older host.
		 */
		add_port(portdev, 0);
	}

	spin_lock_irq(&pdrvdata_lock);
	list_add_tail(&portdev->list, &pdrvdata.portdevs);
	spin_unlock_irq(&pdrvdata_lock);

	__send_control_msg(portdev, VIRTIO_CONSOLE_BAD_ID,
			   VIRTIO_CONSOLE_DEVICE_READY, 1);

	/*
	 * If there was an early virtio console, assume that there are no
	 * other consoles. We need to wait until the hvc_alloc matches the
	 * hvc_instantiate, otherwise tty_open will complain, resulting in
	 * a "Warning: unable to open an initial console" boot failure.
	 * Without multiport this is done in add_port above. With multiport
	 * this might take some host<->guest communication - thus we have to
	 * wait.
	 */
	if (multiport && early)
		wait_for_completion(&early_console_added);

	return 0;

free_vqs:
	/* The host might want to notify mgmt sw about device add failure */
	__send_control_msg(portdev, VIRTIO_CONSOLE_BAD_ID,
			   VIRTIO_CONSOLE_DEVICE_READY, 0);
	remove_vqs(portdev);
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

	portdev = vdev->priv;

	spin_lock_irq(&pdrvdata_lock);
	list_del(&portdev->list);
	spin_unlock_irq(&pdrvdata_lock);

	/* Disable interrupts for vqs */
	vdev->config->reset(vdev);
	/* Finish up work that's lined up */
	if (use_multiport(portdev))
		cancel_work_sync(&portdev->control_work);
	else
		cancel_work_sync(&portdev->config_work);

	list_for_each_entry_safe(port, port2, &portdev->ports, list)
		unplug_port(port);

	unregister_chrdev(portdev->chr_major, "virtio-portsdev");

	/*
	 * When yanking out a device, we immediately lose the
	 * (device-side) queues.  So there's no point in keeping the
	 * guest side around till we drop our final reference.  This
	 * also means that any ports which are in an open state will
	 * have to just stop using the port, as the vqs are going
	 * away.
	 */
	remove_controlq_data(portdev);
	remove_vqs(portdev);
	kfree(portdev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CONSOLE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CONSOLE_F_SIZE,
	VIRTIO_CONSOLE_F_MULTIPORT,
};

static struct virtio_device_id rproc_serial_id_table[] = {
#if IS_ENABLED(CONFIG_REMOTEPROC)
	{ VIRTIO_ID_RPROC_SERIAL, VIRTIO_DEV_ANY_ID },
#endif
	{ 0 },
};

static unsigned int rproc_serial_features[] = {
};

#ifdef CONFIG_PM_SLEEP
static int virtcons_freeze(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	struct port *port;

	portdev = vdev->priv;

	vdev->config->reset(vdev);

	if (use_multiport(portdev))
		virtqueue_disable_cb(portdev->c_ivq);
	cancel_work_sync(&portdev->control_work);
	cancel_work_sync(&portdev->config_work);
	/*
	 * Once more: if control_work_handler() was running, it would
	 * enable the cb as the last step.
	 */
	if (use_multiport(portdev))
		virtqueue_disable_cb(portdev->c_ivq);
	remove_controlq_data(portdev);

	list_for_each_entry(port, &portdev->ports, list) {
		virtqueue_disable_cb(port->in_vq);
		virtqueue_disable_cb(port->out_vq);
		/*
		 * We'll ask the host later if the new invocation has
		 * the port opened or closed.
		 */
		port->host_connected = false;
		remove_port_data(port);
	}
	remove_vqs(portdev);

	return 0;
}

static int virtcons_restore(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	struct port *port;
	int ret;

	portdev = vdev->priv;

	ret = init_vqs(portdev);
	if (ret)
		return ret;

	virtio_device_ready(portdev->vdev);

	if (use_multiport(portdev))
		fill_queue(portdev->c_ivq, &portdev->c_ivq_lock);

	list_for_each_entry(port, &portdev->ports, list) {
		port->in_vq = portdev->in_vqs[port->id];
		port->out_vq = portdev->out_vqs[port->id];

		fill_queue(port->in_vq, &port->inbuf_lock);

		/* Get port open/close status on the host */
		send_control_msg(port, VIRTIO_CONSOLE_PORT_READY, 1);

		/*
		 * If a port was open at the time of suspending, we
		 * have to let the host know that it's still open.
		 */
		if (port->guest_connected)
			send_control_msg(port, VIRTIO_CONSOLE_PORT_OPEN, 1);
	}
	return 0;
}
#endif

static struct virtio_driver virtio_console = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtcons_probe,
	.remove =	virtcons_remove,
	.config_changed = config_intr,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtcons_freeze,
	.restore =	virtcons_restore,
#endif
};

static struct virtio_driver virtio_rproc_serial = {
	.feature_table = rproc_serial_features,
	.feature_table_size = ARRAY_SIZE(rproc_serial_features),
	.driver.name =	"virtio_rproc_serial",
	.driver.owner =	THIS_MODULE,
	.id_table =	rproc_serial_id_table,
	.probe =	virtcons_probe,
	.remove =	virtcons_remove,
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
	if (!pdrvdata.debugfs_dir)
		pr_warn("Error creating debugfs dir for virtio-ports\n");
	INIT_LIST_HEAD(&pdrvdata.consoles);
	INIT_LIST_HEAD(&pdrvdata.portdevs);

	err = register_virtio_driver(&virtio_console);
	if (err < 0) {
		pr_err("Error %d registering virtio driver\n", err);
		goto free;
	}
	err = register_virtio_driver(&virtio_rproc_serial);
	if (err < 0) {
		pr_err("Error %d registering virtio rproc serial driver\n",
		       err);
		goto unregister;
	}
	return 0;
unregister:
	unregister_virtio_driver(&virtio_console);
free:
	debugfs_remove_recursive(pdrvdata.debugfs_dir);
	class_destroy(pdrvdata.class);
	return err;
}

static void __exit fini(void)
{
	reclaim_dma_bufs();

	unregister_virtio_driver(&virtio_console);
	unregister_virtio_driver(&virtio_rproc_serial);

	class_destroy(pdrvdata.class);
	debugfs_remove_recursive(pdrvdata.debugfs_dir);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio console driver");
MODULE_LICENSE("GPL");
