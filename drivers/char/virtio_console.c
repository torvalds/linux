/*
 * Copyright (C) 2006, 2007, 2009 Rusty Russell, IBM Corporation
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
#include <linux/err.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>
#include "hvc_console.h"

/*
 * This is a global struct for storing common data for all the devices
 * this driver handles.
 *
 * Mainly, it has a linked list for all the consoles in one place so
 * that callbacks from hvc for get_chars(), put_chars() work properly
 * across multiple devices and multiple ports per device.
 */
struct ports_driver_data {
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

struct port_buffer {
	char *buf;

	/* size of the buffer in *buf above */
	size_t size;

	/* used length of the buffer */
	size_t len;
	/* offset in the buf from which to consume data */
	size_t offset;
};

struct port {
	struct virtqueue *in_vq, *out_vq;
	struct virtio_device *vdev;

	/* The current buffer from which data has to be fed to readers */
	struct port_buffer *inbuf;

	/* For console ports, hvc != NULL and these are valid. */
	/* The hvc device */
	struct hvc_struct *hvc;

	/* We'll place all consoles in a list in the pdrvdata struct */
	struct list_head list;

	/* Our vterm number. */
	u32 vtermno;
};

/* This is the very early arch-specified put chars function. */
static int (*early_put_chars)(u32, const char *, int);

static struct port *find_port_by_vtermno(u32 vtermno)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&pdrvdata_lock, flags);
	list_for_each_entry(port, &pdrvdata.consoles, list) {
		if (port->vtermno == vtermno)
			goto out;
	}
	port = NULL;
out:
	spin_unlock_irqrestore(&pdrvdata_lock, flags);
	return port;
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
static void add_inbuf(struct virtqueue *vq, struct port_buffer *buf)
{
	struct scatterlist sg[1];
	sg_init_one(sg, buf->buf, buf->size);

	if (vq->vq_ops->add_buf(vq, sg, 0, 1, buf) < 0)
		BUG();
	vq->vq_ops->kick(vq);
}

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
	struct scatterlist sg[1];
	struct port *port;
	unsigned int len;

	port = find_port_by_vtermno(vtermno);
	if (!port)
		return 0;

	if (unlikely(early_put_chars))
		return early_put_chars(vtermno, buf, count);

	/* This is a convenient routine to initialize a single-elem sg list */
	sg_init_one(sg, buf, count);

	/* This shouldn't fail: if it does, we lose chars. */
	if (port->out_vq->vq_ops->add_buf(port->out_vq, sg, 1, 0, port) >= 0) {
		/* Tell Host to go! */
		port->out_vq->vq_ops->kick(port->out_vq);
		while (!port->out_vq->vq_ops->get_buf(port->out_vq, &len))
			cpu_relax();
	}

	/* We're expected to return the amount of data we wrote: all of it. */
	return count;
}

/*
 * get_chars() is the callback from the hvc_console infrastructure
 * when an interrupt is received.
 *
 * Most of the code deals with the fact that the hvc_console()
 * infrastructure only asks us for 16 bytes at a time.  We keep
 * in_offset and in_used fields for partially-filled buffers.
 */
static int get_chars(u32 vtermno, char *buf, int count)
{
	struct port *port;


	port = find_port_by_vtermno(vtermno);
	if (!port)
		return 0;

	/* If we don't have an input queue yet, we can't get input. */
	BUG_ON(!port->in_vq);

	/* No more in buffer?  See if they've (re)used it. */
	if (port->inbuf->offset == port->inbuf->len) {
		if (!get_inbuf(port))
			return 0;
	}

	/* You want more than we have to give?  Well, try wanting less! */
	if (port->inbuf->offset + count > port->inbuf->len)
		count = port->inbuf->len - port->inbuf->offset;

	/* Copy across to their buffer and increment offset. */
	memcpy(buf, port->inbuf->buf + port->inbuf->offset, count);
	port->inbuf->offset += count;

	/* Finished?  Re-register buffer so Host will use it again. */
	if (port->inbuf->offset == port->inbuf->len)
		add_inbuf(port->in_vq, port->inbuf);

	return count;
}

/*
 * virtio console configuration. This supports:
 * - console resize
 */
static void virtcons_apply_config(struct virtio_device *dev)
{
	struct port *port = dev->priv;
	struct winsize ws;

	if (virtio_has_feature(dev, VIRTIO_CONSOLE_F_SIZE)) {
		dev->config->get(dev,
				 offsetof(struct virtio_console_config, cols),
				 &ws.ws_col, sizeof(u16));
		dev->config->get(dev,
				 offsetof(struct virtio_console_config, rows),
				 &ws.ws_row, sizeof(u16));
		hvc_resize(port->hvc, ws);
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
	virtcons_apply_config(port->vdev);

	return 0;
}

static void notifier_del_vio(struct hvc_struct *hp, int data)
{
	hp->irq_requested = 0;
}

static void hvc_handle_input(struct virtqueue *vq)
{
	struct port *port = vq->vdev->priv;

	if (hvc_poll(port->hvc))
		hvc_kick();
}

/* The operations for the console. */
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

static struct port *__devinit add_port(u32 vtermno)
{
	struct port *port;

	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return NULL;

	port->inbuf = alloc_buf(PAGE_SIZE);
	if (!port->inbuf) {
		kfree(port);
		return NULL;
	}
	port->hvc = NULL;
	port->vtermno = vtermno;
	return port;
}

static void free_port(struct port *port)
{
	free_buf(port->inbuf);
	kfree(port);
}

/*
 * Once we're further in boot, we get probed like any other virtio
 * device.  At this stage we set up the output virtqueue.
 *
 * To set up and manage our virtual console, we call hvc_alloc().
 * Since we never remove the console device we never need this pointer
 * again.
 *
 * Finally we put our input buffer in the input queue, ready to
 * receive.
 */
static int __devinit virtcons_probe(struct virtio_device *vdev)
{
	vq_callback_t *callbacks[] = { hvc_handle_input, NULL};
	const char *names[] = { "input", "output" };
	struct virtqueue *vqs[2];
	struct port *port;
	int err;

	port = add_port(pdrvdata.next_vtermno);
	if (!port) {
		err = -ENOMEM;
		goto fail;
	}

	/* Attach this port to this virtio_device, and vice-versa. */
	port->vdev = vdev;
	vdev->priv = port;

	/* Find the queues. */
	err = vdev->config->find_vqs(vdev, 2, vqs, callbacks, names);
	if (err)
		goto free;

	port->in_vq = vqs[0];
	port->out_vq = vqs[1];

	/*
	 * The first argument of hvc_alloc() is the virtual console
	 * number.  The second argument is the parameter for the
	 * notification mechanism (like irq number).  We currently
	 * leave this as zero, virtqueues have implicit notifications.
	 *
	 * The third argument is a "struct hv_ops" containing the
	 * put_chars(), get_chars(), notifier_add() and notifier_del()
	 * pointers.  The final argument is the output buffer size: we
	 * can do any size, so we put PAGE_SIZE here.
	 */
	port->hvc = hvc_alloc(port->vtermno, 0, &hv_ops, PAGE_SIZE);
	if (IS_ERR(port->hvc)) {
		err = PTR_ERR(port->hvc);
		goto free_vqs;
	}

	/* Add to vtermno list. */
	spin_lock_irq(&pdrvdata_lock);
	pdrvdata.next_vtermno++;
	list_add(&port->list, &pdrvdata.consoles);
	spin_unlock_irq(&pdrvdata_lock);

	/* Register the input buffer the first time. */
	add_inbuf(port->in_vq, port->inbuf);

	/* Start using the new console output. */
	early_put_chars = NULL;
	return 0;

free_vqs:
	vdev->config->del_vqs(vdev);
free:
	free_port(port);
fail:
	return err;
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
	.config_changed = virtcons_apply_config,
};

static int __init init(void)
{
	INIT_LIST_HEAD(&pdrvdata.consoles);

	return register_virtio_driver(&virtio_console);
}
module_init(init);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio console driver");
MODULE_LICENSE("GPL");
