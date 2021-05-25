// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wwan.h>

#define WWAN_MAX_MINORS 256 /* 256 minors allowed with register_chrdev() */

static DEFINE_MUTEX(wwan_register_lock); /* WWAN device create|remove lock */
static DEFINE_IDA(minors); /* minors for WWAN port chardevs */
static DEFINE_IDA(wwan_dev_ids); /* for unique WWAN device IDs */
static struct class *wwan_class;
static int wwan_major;

#define to_wwan_dev(d) container_of(d, struct wwan_device, dev)
#define to_wwan_port(d) container_of(d, struct wwan_port, dev)

/* WWAN port flags */
#define WWAN_PORT_TX_OFF	0

/**
 * struct wwan_device - The structure that defines a WWAN device
 *
 * @id: WWAN device unique ID.
 * @dev: Underlying device.
 * @port_id: Current available port ID to pick.
 */
struct wwan_device {
	unsigned int id;
	struct device dev;
	atomic_t port_id;
};

/**
 * struct wwan_port - The structure that defines a WWAN port
 * @type: Port type
 * @start_count: Port start counter
 * @flags: Store port state and capabilities
 * @ops: Pointer to WWAN port operations
 * @ops_lock: Protect port ops
 * @dev: Underlying device
 * @rxq: Buffer inbound queue
 * @waitqueue: The waitqueue for port fops (read/write/poll)
 */
struct wwan_port {
	enum wwan_port_type type;
	unsigned int start_count;
	unsigned long flags;
	const struct wwan_port_ops *ops;
	struct mutex ops_lock; /* Serialize ops + protect against removal */
	struct device dev;
	struct sk_buff_head rxq;
	wait_queue_head_t waitqueue;
};

static ssize_t index_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct wwan_device *wwan = to_wwan_dev(dev);

	return sprintf(buf, "%d\n", wwan->id);
}
static DEVICE_ATTR_RO(index);

static struct attribute *wwan_dev_attrs[] = {
	&dev_attr_index.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wwan_dev);

static void wwan_dev_destroy(struct device *dev)
{
	struct wwan_device *wwandev = to_wwan_dev(dev);

	ida_free(&wwan_dev_ids, wwandev->id);
	kfree(wwandev);
}

static const struct device_type wwan_dev_type = {
	.name    = "wwan_dev",
	.release = wwan_dev_destroy,
	.groups = wwan_dev_groups,
};

static int wwan_dev_parent_match(struct device *dev, const void *parent)
{
	return (dev->type == &wwan_dev_type && dev->parent == parent);
}

static struct wwan_device *wwan_dev_get_by_parent(struct device *parent)
{
	struct device *dev;

	dev = class_find_device(wwan_class, NULL, parent, wwan_dev_parent_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_dev(dev);
}

/* This function allocates and registers a new WWAN device OR if a WWAN device
 * already exist for the given parent, it gets a reference and return it.
 * This function is not exported (for now), it is called indirectly via
 * wwan_create_port().
 */
static struct wwan_device *wwan_create_dev(struct device *parent)
{
	struct wwan_device *wwandev;
	int err, id;

	/* The 'find-alloc-register' operation must be protected against
	 * concurrent execution, a WWAN device is possibly shared between
	 * multiple callers or concurrently unregistered from wwan_remove_dev().
	 */
	mutex_lock(&wwan_register_lock);

	/* If wwandev already exists, return it */
	wwandev = wwan_dev_get_by_parent(parent);
	if (!IS_ERR(wwandev))
		goto done_unlock;

	id = ida_alloc(&wwan_dev_ids, GFP_KERNEL);
	if (id < 0)
		goto done_unlock;

	wwandev = kzalloc(sizeof(*wwandev), GFP_KERNEL);
	if (!wwandev) {
		ida_free(&wwan_dev_ids, id);
		goto done_unlock;
	}

	wwandev->dev.parent = parent;
	wwandev->dev.class = wwan_class;
	wwandev->dev.type = &wwan_dev_type;
	wwandev->id = id;
	dev_set_name(&wwandev->dev, "wwan%d", wwandev->id);

	err = device_register(&wwandev->dev);
	if (err) {
		put_device(&wwandev->dev);
		wwandev = NULL;
	}

done_unlock:
	mutex_unlock(&wwan_register_lock);

	return wwandev;
}

static int is_wwan_child(struct device *dev, void *data)
{
	return dev->class == wwan_class;
}

static void wwan_remove_dev(struct wwan_device *wwandev)
{
	int ret;

	/* Prevent concurrent picking from wwan_create_dev */
	mutex_lock(&wwan_register_lock);

	/* WWAN device is created and registered (get+add) along with its first
	 * child port, and subsequent port registrations only grab a reference
	 * (get). The WWAN device must then be unregistered (del+put) along with
	 * its latest port, and reference simply dropped (put) otherwise.
	 */
	ret = device_for_each_child(&wwandev->dev, NULL, is_wwan_child);
	if (!ret)
		device_unregister(&wwandev->dev);
	else
		put_device(&wwandev->dev);

	mutex_unlock(&wwan_register_lock);
}

/* ------- WWAN port management ------- */

/* Keep aligned with wwan_port_type enum */
static const char * const wwan_port_type_str[] = {
	"AT",
	"MBIM",
	"QMI",
	"QCDM",
	"FIREHOSE"
};

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct wwan_port *port = to_wwan_port(dev);

	return sprintf(buf, "%s\n", wwan_port_type_str[port->type]);
}
static DEVICE_ATTR_RO(type);

static struct attribute *wwan_port_attrs[] = {
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wwan_port);

static void wwan_port_destroy(struct device *dev)
{
	struct wwan_port *port = to_wwan_port(dev);

	ida_free(&minors, MINOR(port->dev.devt));
	skb_queue_purge(&port->rxq);
	mutex_destroy(&port->ops_lock);
	kfree(port);
}

static const struct device_type wwan_port_dev_type = {
	.name = "wwan_port",
	.release = wwan_port_destroy,
	.groups = wwan_port_groups,
};

static int wwan_port_minor_match(struct device *dev, const void *minor)
{
	return (dev->type == &wwan_port_dev_type &&
		MINOR(dev->devt) == *(unsigned int *)minor);
}

static struct wwan_port *wwan_port_get_by_minor(unsigned int minor)
{
	struct device *dev;

	dev = class_find_device(wwan_class, NULL, &minor, wwan_port_minor_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_port(dev);
}

struct wwan_port *wwan_create_port(struct device *parent,
				   enum wwan_port_type type,
				   const struct wwan_port_ops *ops,
				   void *drvdata)
{
	struct wwan_device *wwandev;
	struct wwan_port *port;
	int minor, err = -ENOMEM;

	if (type >= WWAN_PORT_MAX || !ops)
		return ERR_PTR(-EINVAL);

	/* A port is always a child of a WWAN device, retrieve (allocate or
	 * pick) the WWAN device based on the provided parent device.
	 */
	wwandev = wwan_create_dev(parent);
	if (IS_ERR(wwandev))
		return ERR_CAST(wwandev);

	/* A port is exposed as character device, get a minor */
	minor = ida_alloc_range(&minors, 0, WWAN_MAX_MINORS - 1, GFP_KERNEL);
	if (minor < 0)
		goto error_wwandev_remove;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		ida_free(&minors, minor);
		goto error_wwandev_remove;
	}

	port->type = type;
	port->ops = ops;
	mutex_init(&port->ops_lock);
	skb_queue_head_init(&port->rxq);
	init_waitqueue_head(&port->waitqueue);

	port->dev.parent = &wwandev->dev;
	port->dev.class = wwan_class;
	port->dev.type = &wwan_port_dev_type;
	port->dev.devt = MKDEV(wwan_major, minor);
	dev_set_drvdata(&port->dev, drvdata);

	/* create unique name based on wwan device id, port index and type */
	dev_set_name(&port->dev, "wwan%up%u%s", wwandev->id,
		     atomic_inc_return(&wwandev->port_id),
		     wwan_port_type_str[port->type]);

	err = device_register(&port->dev);
	if (err)
		goto error_put_device;

	return port;

error_put_device:
	put_device(&port->dev);
error_wwandev_remove:
	wwan_remove_dev(wwandev);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(wwan_create_port);

void wwan_remove_port(struct wwan_port *port)
{
	struct wwan_device *wwandev = to_wwan_dev(port->dev.parent);

	mutex_lock(&port->ops_lock);
	if (port->start_count)
		port->ops->stop(port);
	port->ops = NULL; /* Prevent any new port operations (e.g. from fops) */
	mutex_unlock(&port->ops_lock);

	wake_up_interruptible(&port->waitqueue);

	skb_queue_purge(&port->rxq);
	dev_set_drvdata(&port->dev, NULL);
	device_unregister(&port->dev);

	/* Release related wwan device */
	wwan_remove_dev(wwandev);
}
EXPORT_SYMBOL_GPL(wwan_remove_port);

void wwan_port_rx(struct wwan_port *port, struct sk_buff *skb)
{
	skb_queue_tail(&port->rxq, skb);
	wake_up_interruptible(&port->waitqueue);
}
EXPORT_SYMBOL_GPL(wwan_port_rx);

void wwan_port_txon(struct wwan_port *port)
{
	clear_bit(WWAN_PORT_TX_OFF, &port->flags);
	wake_up_interruptible(&port->waitqueue);
}
EXPORT_SYMBOL_GPL(wwan_port_txon);

void wwan_port_txoff(struct wwan_port *port)
{
	set_bit(WWAN_PORT_TX_OFF, &port->flags);
}
EXPORT_SYMBOL_GPL(wwan_port_txoff);

void *wwan_port_get_drvdata(struct wwan_port *port)
{
	return dev_get_drvdata(&port->dev);
}
EXPORT_SYMBOL_GPL(wwan_port_get_drvdata);

static int wwan_port_op_start(struct wwan_port *port)
{
	int ret = 0;

	mutex_lock(&port->ops_lock);
	if (!port->ops) { /* Port got unplugged */
		ret = -ENODEV;
		goto out_unlock;
	}

	/* If port is already started, don't start again */
	if (!port->start_count)
		ret = port->ops->start(port);

	if (!ret)
		port->start_count++;

out_unlock:
	mutex_unlock(&port->ops_lock);

	return ret;
}

static void wwan_port_op_stop(struct wwan_port *port)
{
	mutex_lock(&port->ops_lock);
	port->start_count--;
	if (port->ops && !port->start_count)
		port->ops->stop(port);
	mutex_unlock(&port->ops_lock);
}

static int wwan_port_op_tx(struct wwan_port *port, struct sk_buff *skb)
{
	int ret;

	mutex_lock(&port->ops_lock);
	if (!port->ops) { /* Port got unplugged */
		ret = -ENODEV;
		goto out_unlock;
	}

	ret = port->ops->tx(port, skb);

out_unlock:
	mutex_unlock(&port->ops_lock);

	return ret;
}

static bool is_read_blocked(struct wwan_port *port)
{
	return skb_queue_empty(&port->rxq) && port->ops;
}

static bool is_write_blocked(struct wwan_port *port)
{
	return test_bit(WWAN_PORT_TX_OFF, &port->flags) && port->ops;
}

static int wwan_wait_rx(struct wwan_port *port, bool nonblock)
{
	if (!is_read_blocked(port))
		return 0;

	if (nonblock)
		return -EAGAIN;

	if (wait_event_interruptible(port->waitqueue, !is_read_blocked(port)))
		return -ERESTARTSYS;

	return 0;
}

static int wwan_wait_tx(struct wwan_port *port, bool nonblock)
{
	if (!is_write_blocked(port))
		return 0;

	if (nonblock)
		return -EAGAIN;

	if (wait_event_interruptible(port->waitqueue, !is_write_blocked(port)))
		return -ERESTARTSYS;

	return 0;
}

static int wwan_port_fops_open(struct inode *inode, struct file *file)
{
	struct wwan_port *port;
	int err = 0;

	port = wwan_port_get_by_minor(iminor(inode));
	if (IS_ERR(port))
		return PTR_ERR(port);

	file->private_data = port;
	stream_open(inode, file);

	err = wwan_port_op_start(port);
	if (err)
		put_device(&port->dev);

	return err;
}

static int wwan_port_fops_release(struct inode *inode, struct file *filp)
{
	struct wwan_port *port = filp->private_data;

	wwan_port_op_stop(port);
	put_device(&port->dev);

	return 0;
}

static ssize_t wwan_port_fops_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct wwan_port *port = filp->private_data;
	struct sk_buff *skb;
	size_t copied;
	int ret;

	ret = wwan_wait_rx(port, !!(filp->f_flags & O_NONBLOCK));
	if (ret)
		return ret;

	skb = skb_dequeue(&port->rxq);
	if (!skb)
		return -EIO;

	copied = min_t(size_t, count, skb->len);
	if (copy_to_user(buf, skb->data, copied)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	skb_pull(skb, copied);

	/* skb is not fully consumed, keep it in the queue */
	if (skb->len)
		skb_queue_head(&port->rxq, skb);
	else
		consume_skb(skb);

	return copied;
}

static ssize_t wwan_port_fops_write(struct file *filp, const char __user *buf,
				    size_t count, loff_t *offp)
{
	struct wwan_port *port = filp->private_data;
	struct sk_buff *skb;
	int ret;

	ret = wwan_wait_tx(port, !!(filp->f_flags & O_NONBLOCK));
	if (ret)
		return ret;

	skb = alloc_skb(count, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	ret = wwan_port_op_tx(port, skb);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return count;
}

static __poll_t wwan_port_fops_poll(struct file *filp, poll_table *wait)
{
	struct wwan_port *port = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &port->waitqueue, wait);

	if (!is_write_blocked(port))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (!is_read_blocked(port))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!port->ops)
		mask |= EPOLLHUP | EPOLLERR;

	return mask;
}

static const struct file_operations wwan_port_fops = {
	.owner = THIS_MODULE,
	.open = wwan_port_fops_open,
	.release = wwan_port_fops_release,
	.read = wwan_port_fops_read,
	.write = wwan_port_fops_write,
	.poll = wwan_port_fops_poll,
	.llseek = noop_llseek,
};

static int __init wwan_init(void)
{
	wwan_class = class_create(THIS_MODULE, "wwan");
	if (IS_ERR(wwan_class))
		return PTR_ERR(wwan_class);

	/* chrdev used for wwan ports */
	wwan_major = register_chrdev(0, "wwan_port", &wwan_port_fops);
	if (wwan_major < 0) {
		class_destroy(wwan_class);
		return wwan_major;
	}

	return 0;
}

static void __exit wwan_exit(void)
{
	unregister_chrdev(wwan_major, "wwan_port");
	class_destroy(wwan_class);
}

module_init(wwan_init);
module_exit(wwan_exit);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("WWAN core");
MODULE_LICENSE("GPL v2");
