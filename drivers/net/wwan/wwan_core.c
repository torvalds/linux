// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */

#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/termios.h>
#include <linux/wwan.h>
#include <net/rtnetlink.h>
#include <uapi/linux/wwan.h>

/* Maximum number of minors in use */
#define WWAN_MAX_MINORS		(1 << MINORBITS)

static DEFINE_MUTEX(wwan_register_lock); /* WWAN device create|remove lock */
static DEFINE_IDA(minors); /* minors for WWAN port chardevs */
static DEFINE_IDA(wwan_dev_ids); /* for unique WWAN device IDs */
static const struct class wwan_class = {
	.name = "wwan",
};
static int wwan_major;
static struct dentry *wwan_debugfs_dir;

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
 * @ops: wwan device ops
 * @ops_ctxt: context to pass to ops
 * @debugfs_dir:  WWAN device debugfs dir
 */
struct wwan_device {
	unsigned int id;
	struct device dev;
	atomic_t port_id;
	const struct wwan_ops *ops;
	void *ops_ctxt;
#ifdef CONFIG_WWAN_DEBUGFS
	struct dentry *debugfs_dir;
#endif
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
 * @data_lock: Port specific data access serialization
 * @headroom_len: SKB reserved headroom size
 * @frag_len: Length to fragment packet
 * @at_data: AT port specific data
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
	struct mutex data_lock;	/* Port specific data access serialization */
	size_t headroom_len;
	size_t frag_len;
	union {
		struct {
			struct ktermios termios;
			int mdmbits;
		} at_data;
	};
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
	return (dev->type == &wwan_dev_type &&
		(dev->parent == parent || dev == parent));
}

static struct wwan_device *wwan_dev_get_by_parent(struct device *parent)
{
	struct device *dev;

	dev = class_find_device(&wwan_class, NULL, parent, wwan_dev_parent_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_dev(dev);
}

static int wwan_dev_name_match(struct device *dev, const void *name)
{
	return dev->type == &wwan_dev_type &&
	       strcmp(dev_name(dev), name) == 0;
}

static struct wwan_device *wwan_dev_get_by_name(const char *name)
{
	struct device *dev;

	dev = class_find_device(&wwan_class, NULL, name, wwan_dev_name_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_dev(dev);
}

#ifdef CONFIG_WWAN_DEBUGFS
struct dentry *wwan_get_debugfs_dir(struct device *parent)
{
	struct wwan_device *wwandev;

	wwandev = wwan_dev_get_by_parent(parent);
	if (IS_ERR(wwandev))
		return ERR_CAST(wwandev);

	return wwandev->debugfs_dir;
}
EXPORT_SYMBOL_GPL(wwan_get_debugfs_dir);

static int wwan_dev_debugfs_match(struct device *dev, const void *dir)
{
	struct wwan_device *wwandev;

	if (dev->type != &wwan_dev_type)
		return 0;

	wwandev = to_wwan_dev(dev);

	return wwandev->debugfs_dir == dir;
}

static struct wwan_device *wwan_dev_get_by_debugfs(struct dentry *dir)
{
	struct device *dev;

	dev = class_find_device(&wwan_class, NULL, dir, wwan_dev_debugfs_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_dev(dev);
}

void wwan_put_debugfs_dir(struct dentry *dir)
{
	struct wwan_device *wwandev = wwan_dev_get_by_debugfs(dir);

	if (WARN_ON(IS_ERR(wwandev)))
		return;

	/* wwan_dev_get_by_debugfs() also got a reference */
	put_device(&wwandev->dev);
	put_device(&wwandev->dev);
}
EXPORT_SYMBOL_GPL(wwan_put_debugfs_dir);
#endif

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
	if (id < 0) {
		wwandev = ERR_PTR(id);
		goto done_unlock;
	}

	wwandev = kzalloc(sizeof(*wwandev), GFP_KERNEL);
	if (!wwandev) {
		wwandev = ERR_PTR(-ENOMEM);
		ida_free(&wwan_dev_ids, id);
		goto done_unlock;
	}

	wwandev->dev.parent = parent;
	wwandev->dev.class = &wwan_class;
	wwandev->dev.type = &wwan_dev_type;
	wwandev->id = id;
	dev_set_name(&wwandev->dev, "wwan%d", wwandev->id);

	err = device_register(&wwandev->dev);
	if (err) {
		put_device(&wwandev->dev);
		wwandev = ERR_PTR(err);
		goto done_unlock;
	}

#ifdef CONFIG_WWAN_DEBUGFS
	wwandev->debugfs_dir =
			debugfs_create_dir(kobject_name(&wwandev->dev.kobj),
					   wwan_debugfs_dir);
#endif

done_unlock:
	mutex_unlock(&wwan_register_lock);

	return wwandev;
}

static int is_wwan_child(struct device *dev, void *data)
{
	return dev->class == &wwan_class;
}

static void wwan_remove_dev(struct wwan_device *wwandev)
{
	int ret;

	/* Prevent concurrent picking from wwan_create_dev */
	mutex_lock(&wwan_register_lock);

	/* WWAN device is created and registered (get+add) along with its first
	 * child port, and subsequent port registrations only grab a reference
	 * (get). The WWAN device must then be unregistered (del+put) along with
	 * its last port, and reference simply dropped (put) otherwise. In the
	 * same fashion, we must not unregister it when the ops are still there.
	 */
	if (wwandev->ops)
		ret = 1;
	else
		ret = device_for_each_child(&wwandev->dev, NULL, is_wwan_child);

	if (!ret) {
#ifdef CONFIG_WWAN_DEBUGFS
		debugfs_remove_recursive(wwandev->debugfs_dir);
#endif
		device_unregister(&wwandev->dev);
	} else {
		put_device(&wwandev->dev);
	}

	mutex_unlock(&wwan_register_lock);
}

/* ------- WWAN port management ------- */

static const struct {
	const char * const name;	/* Port type name */
	const char * const devsuf;	/* Port device name suffix */
} wwan_port_types[WWAN_PORT_MAX + 1] = {
	[WWAN_PORT_AT] = {
		.name = "AT",
		.devsuf = "at",
	},
	[WWAN_PORT_MBIM] = {
		.name = "MBIM",
		.devsuf = "mbim",
	},
	[WWAN_PORT_QMI] = {
		.name = "QMI",
		.devsuf = "qmi",
	},
	[WWAN_PORT_QCDM] = {
		.name = "QCDM",
		.devsuf = "qcdm",
	},
	[WWAN_PORT_FIREHOSE] = {
		.name = "FIREHOSE",
		.devsuf = "firehose",
	},
	[WWAN_PORT_XMMRPC] = {
		.name = "XMMRPC",
		.devsuf = "xmmrpc",
	},
	[WWAN_PORT_FASTBOOT] = {
		.name = "FASTBOOT",
		.devsuf = "fastboot",
	},
};

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct wwan_port *port = to_wwan_port(dev);

	return sprintf(buf, "%s\n", wwan_port_types[port->type].name);
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
	mutex_destroy(&port->data_lock);
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

	dev = class_find_device(&wwan_class, NULL, &minor, wwan_port_minor_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_port(dev);
}

/* Allocate and set unique name based on passed format
 *
 * Name allocation approach is highly inspired by the __dev_alloc_name()
 * function.
 *
 * To avoid names collision, the caller must prevent the new port device
 * registration as well as concurrent invocation of this function.
 */
static int __wwan_port_dev_assign_name(struct wwan_port *port, const char *fmt)
{
	struct wwan_device *wwandev = to_wwan_dev(port->dev.parent);
	const unsigned int max_ports = PAGE_SIZE * 8;
	struct class_dev_iter iter;
	unsigned long *idmap;
	struct device *dev;
	char buf[0x20];
	int id;

	idmap = bitmap_zalloc(max_ports, GFP_KERNEL);
	if (!idmap)
		return -ENOMEM;

	/* Collect ids of same name format ports */
	class_dev_iter_init(&iter, &wwan_class, NULL, &wwan_port_dev_type);
	while ((dev = class_dev_iter_next(&iter))) {
		if (dev->parent != &wwandev->dev)
			continue;
		if (sscanf(dev_name(dev), fmt, &id) != 1)
			continue;
		if (id < 0 || id >= max_ports)
			continue;
		set_bit(id, idmap);
	}
	class_dev_iter_exit(&iter);

	/* Allocate unique id */
	id = find_first_zero_bit(idmap, max_ports);
	bitmap_free(idmap);

	snprintf(buf, sizeof(buf), fmt, id);	/* Name generation */

	dev = device_find_child_by_name(&wwandev->dev, buf);
	if (dev) {
		put_device(dev);
		return -ENFILE;
	}

	return dev_set_name(&port->dev, buf);
}

struct wwan_port *wwan_create_port(struct device *parent,
				   enum wwan_port_type type,
				   const struct wwan_port_ops *ops,
				   struct wwan_port_caps *caps,
				   void *drvdata)
{
	struct wwan_device *wwandev;
	struct wwan_port *port;
	char namefmt[0x20];
	int minor, err;

	if (type > WWAN_PORT_MAX || !ops)
		return ERR_PTR(-EINVAL);

	/* A port is always a child of a WWAN device, retrieve (allocate or
	 * pick) the WWAN device based on the provided parent device.
	 */
	wwandev = wwan_create_dev(parent);
	if (IS_ERR(wwandev))
		return ERR_CAST(wwandev);

	/* A port is exposed as character device, get a minor */
	minor = ida_alloc_range(&minors, 0, WWAN_MAX_MINORS - 1, GFP_KERNEL);
	if (minor < 0) {
		err = minor;
		goto error_wwandev_remove;
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		ida_free(&minors, minor);
		goto error_wwandev_remove;
	}

	port->type = type;
	port->ops = ops;
	port->frag_len = caps ? caps->frag_len : SIZE_MAX;
	port->headroom_len = caps ? caps->headroom_len : 0;
	mutex_init(&port->ops_lock);
	skb_queue_head_init(&port->rxq);
	init_waitqueue_head(&port->waitqueue);
	mutex_init(&port->data_lock);

	port->dev.parent = &wwandev->dev;
	port->dev.class = &wwan_class;
	port->dev.type = &wwan_port_dev_type;
	port->dev.devt = MKDEV(wwan_major, minor);
	dev_set_drvdata(&port->dev, drvdata);

	/* allocate unique name based on wwan device id, port type and number */
	snprintf(namefmt, sizeof(namefmt), "wwan%u%s%%d", wwandev->id,
		 wwan_port_types[port->type].devsuf);

	/* Serialize ports registration */
	mutex_lock(&wwan_register_lock);

	__wwan_port_dev_assign_name(port, namefmt);
	err = device_register(&port->dev);

	mutex_unlock(&wwan_register_lock);

	if (err)
		goto error_put_device;

	dev_info(&wwandev->dev, "port %s attached\n", dev_name(&port->dev));
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

	dev_info(&wwandev->dev, "port %s disconnected\n", dev_name(&port->dev));
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
	if (!port->start_count) {
		if (port->ops)
			port->ops->stop(port);
		skb_queue_purge(&port->rxq);
	}
	mutex_unlock(&port->ops_lock);
}

static int wwan_port_op_tx(struct wwan_port *port, struct sk_buff *skb,
			   bool nonblock)
{
	int ret;

	mutex_lock(&port->ops_lock);
	if (!port->ops) { /* Port got unplugged */
		ret = -ENODEV;
		goto out_unlock;
	}

	if (nonblock || !port->ops->tx_blocking)
		ret = port->ops->tx(port, skb);
	else
		ret = port->ops->tx_blocking(port, skb);

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
	struct sk_buff *skb, *head = NULL, *tail = NULL;
	struct wwan_port *port = filp->private_data;
	size_t frag_len, remain = count;
	int ret;

	ret = wwan_wait_tx(port, !!(filp->f_flags & O_NONBLOCK));
	if (ret)
		return ret;

	do {
		frag_len = min(remain, port->frag_len);
		skb = alloc_skb(frag_len + port->headroom_len, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto freeskb;
		}
		skb_reserve(skb, port->headroom_len);

		if (!head) {
			head = skb;
		} else if (!tail) {
			skb_shinfo(head)->frag_list = skb;
			tail = skb;
		} else {
			tail->next = skb;
			tail = skb;
		}

		if (copy_from_user(skb_put(skb, frag_len), buf + count - remain, frag_len)) {
			ret = -EFAULT;
			goto freeskb;
		}

		if (skb != head) {
			head->data_len += skb->len;
			head->len += skb->len;
			head->truesize += skb->truesize;
		}
	} while (remain -= frag_len);

	ret = wwan_port_op_tx(port, head, !!(filp->f_flags & O_NONBLOCK));
	if (!ret)
		return count;

freeskb:
	kfree_skb(head);
	return ret;
}

static __poll_t wwan_port_fops_poll(struct file *filp, poll_table *wait)
{
	struct wwan_port *port = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &port->waitqueue, wait);

	mutex_lock(&port->ops_lock);
	if (port->ops && port->ops->tx_poll)
		mask |= port->ops->tx_poll(port, filp, wait);
	else if (!is_write_blocked(port))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (!is_read_blocked(port))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!port->ops)
		mask |= EPOLLHUP | EPOLLERR;
	mutex_unlock(&port->ops_lock);

	return mask;
}

/* Implements minimalistic stub terminal IOCTLs support */
static long wwan_port_fops_at_ioctl(struct wwan_port *port, unsigned int cmd,
				    unsigned long arg)
{
	int ret = 0;

	mutex_lock(&port->data_lock);

	switch (cmd) {
	case TCFLSH:
		break;

	case TCGETS:
		if (copy_to_user((void __user *)arg, &port->at_data.termios,
				 sizeof(struct termios)))
			ret = -EFAULT;
		break;

	case TCSETS:
	case TCSETSW:
	case TCSETSF:
		if (copy_from_user(&port->at_data.termios, (void __user *)arg,
				   sizeof(struct termios)))
			ret = -EFAULT;
		break;

#ifdef TCGETS2
	case TCGETS2:
		if (copy_to_user((void __user *)arg, &port->at_data.termios,
				 sizeof(struct termios2)))
			ret = -EFAULT;
		break;

	case TCSETS2:
	case TCSETSW2:
	case TCSETSF2:
		if (copy_from_user(&port->at_data.termios, (void __user *)arg,
				   sizeof(struct termios2)))
			ret = -EFAULT;
		break;
#endif

	case TIOCMGET:
		ret = put_user(port->at_data.mdmbits, (int __user *)arg);
		break;

	case TIOCMSET:
	case TIOCMBIC:
	case TIOCMBIS: {
		int mdmbits;

		if (copy_from_user(&mdmbits, (int __user *)arg, sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		if (cmd == TIOCMBIC)
			port->at_data.mdmbits &= ~mdmbits;
		else if (cmd == TIOCMBIS)
			port->at_data.mdmbits |= mdmbits;
		else
			port->at_data.mdmbits = mdmbits;
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&port->data_lock);

	return ret;
}

static long wwan_port_fops_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long arg)
{
	struct wwan_port *port = filp->private_data;
	int res;

	if (port->type == WWAN_PORT_AT) {	/* AT port specific IOCTLs */
		res = wwan_port_fops_at_ioctl(port, cmd, arg);
		if (res != -ENOIOCTLCMD)
			return res;
	}

	switch (cmd) {
	case TIOCINQ: {	/* aka SIOCINQ aka FIONREAD */
		unsigned long flags;
		struct sk_buff *skb;
		int amount = 0;

		spin_lock_irqsave(&port->rxq.lock, flags);
		skb_queue_walk(&port->rxq, skb)
			amount += skb->len;
		spin_unlock_irqrestore(&port->rxq.lock, flags);

		return put_user(amount, (int __user *)arg);
	}

	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations wwan_port_fops = {
	.owner = THIS_MODULE,
	.open = wwan_port_fops_open,
	.release = wwan_port_fops_release,
	.read = wwan_port_fops_read,
	.write = wwan_port_fops_write,
	.poll = wwan_port_fops_poll,
	.unlocked_ioctl = wwan_port_fops_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.llseek = noop_llseek,
};

static int wwan_rtnl_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	if (!data)
		return -EINVAL;

	if (!tb[IFLA_PARENT_DEV_NAME])
		return -EINVAL;

	if (!data[IFLA_WWAN_LINK_ID])
		return -EINVAL;

	return 0;
}

static const struct device_type wwan_type = { .name = "wwan" };

static struct net_device *wwan_rtnl_alloc(struct nlattr *tb[],
					  const char *ifname,
					  unsigned char name_assign_type,
					  unsigned int num_tx_queues,
					  unsigned int num_rx_queues)
{
	const char *devname = nla_data(tb[IFLA_PARENT_DEV_NAME]);
	struct wwan_device *wwandev = wwan_dev_get_by_name(devname);
	struct net_device *dev;
	unsigned int priv_size;

	if (IS_ERR(wwandev))
		return ERR_CAST(wwandev);

	/* only supported if ops were registered (not just ports) */
	if (!wwandev->ops) {
		dev = ERR_PTR(-EOPNOTSUPP);
		goto out;
	}

	priv_size = sizeof(struct wwan_netdev_priv) + wwandev->ops->priv_size;
	dev = alloc_netdev_mqs(priv_size, ifname, name_assign_type,
			       wwandev->ops->setup, num_tx_queues, num_rx_queues);

	if (dev) {
		SET_NETDEV_DEV(dev, &wwandev->dev);
		SET_NETDEV_DEVTYPE(dev, &wwan_type);
	}

out:
	/* release the reference */
	put_device(&wwandev->dev);
	return dev;
}

static int wwan_rtnl_newlink(struct net *src_net, struct net_device *dev,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct wwan_device *wwandev = wwan_dev_get_by_parent(dev->dev.parent);
	u32 link_id = nla_get_u32(data[IFLA_WWAN_LINK_ID]);
	struct wwan_netdev_priv *priv = netdev_priv(dev);
	int ret;

	if (IS_ERR(wwandev))
		return PTR_ERR(wwandev);

	/* shouldn't have a netdev (left) with us as parent so WARN */
	if (WARN_ON(!wwandev->ops)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	priv->link_id = link_id;
	if (wwandev->ops->newlink)
		ret = wwandev->ops->newlink(wwandev->ops_ctxt, dev,
					    link_id, extack);
	else
		ret = register_netdevice(dev);

out:
	/* release the reference */
	put_device(&wwandev->dev);
	return ret;
}

static void wwan_rtnl_dellink(struct net_device *dev, struct list_head *head)
{
	struct wwan_device *wwandev = wwan_dev_get_by_parent(dev->dev.parent);

	if (IS_ERR(wwandev))
		return;

	/* shouldn't have a netdev (left) with us as parent so WARN */
	if (WARN_ON(!wwandev->ops))
		goto out;

	if (wwandev->ops->dellink)
		wwandev->ops->dellink(wwandev->ops_ctxt, dev, head);
	else
		unregister_netdevice_queue(dev, head);

out:
	/* release the reference */
	put_device(&wwandev->dev);
}

static size_t wwan_rtnl_get_size(const struct net_device *dev)
{
	return
		nla_total_size(4) +	/* IFLA_WWAN_LINK_ID */
		0;
}

static int wwan_rtnl_fill_info(struct sk_buff *skb,
			       const struct net_device *dev)
{
	struct wwan_netdev_priv *priv = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_WWAN_LINK_ID, priv->link_id))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy wwan_rtnl_policy[IFLA_WWAN_MAX + 1] = {
	[IFLA_WWAN_LINK_ID] = { .type = NLA_U32 },
};

static struct rtnl_link_ops wwan_rtnl_link_ops __read_mostly = {
	.kind = "wwan",
	.maxtype = __IFLA_WWAN_MAX,
	.alloc = wwan_rtnl_alloc,
	.validate = wwan_rtnl_validate,
	.newlink = wwan_rtnl_newlink,
	.dellink = wwan_rtnl_dellink,
	.get_size = wwan_rtnl_get_size,
	.fill_info = wwan_rtnl_fill_info,
	.policy = wwan_rtnl_policy,
};

static void wwan_create_default_link(struct wwan_device *wwandev,
				     u32 def_link_id)
{
	struct nlattr *tb[IFLA_MAX + 1], *linkinfo[IFLA_INFO_MAX + 1];
	struct nlattr *data[IFLA_WWAN_MAX + 1];
	struct net_device *dev;
	struct nlmsghdr *nlh;
	struct sk_buff *msg;

	/* Forge attributes required to create a WWAN netdev. We first
	 * build a netlink message and then parse it. This looks
	 * odd, but such approach is less error prone.
	 */
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (WARN_ON(!msg))
		return;
	nlh = nlmsg_put(msg, 0, 0, RTM_NEWLINK, 0, 0);
	if (WARN_ON(!nlh))
		goto free_attrs;

	if (nla_put_string(msg, IFLA_PARENT_DEV_NAME, dev_name(&wwandev->dev)))
		goto free_attrs;
	tb[IFLA_LINKINFO] = nla_nest_start(msg, IFLA_LINKINFO);
	if (!tb[IFLA_LINKINFO])
		goto free_attrs;
	linkinfo[IFLA_INFO_DATA] = nla_nest_start(msg, IFLA_INFO_DATA);
	if (!linkinfo[IFLA_INFO_DATA])
		goto free_attrs;
	if (nla_put_u32(msg, IFLA_WWAN_LINK_ID, def_link_id))
		goto free_attrs;
	nla_nest_end(msg, linkinfo[IFLA_INFO_DATA]);
	nla_nest_end(msg, tb[IFLA_LINKINFO]);

	nlmsg_end(msg, nlh);

	/* The next three parsing calls can not fail */
	nlmsg_parse_deprecated(nlh, 0, tb, IFLA_MAX, NULL, NULL);
	nla_parse_nested_deprecated(linkinfo, IFLA_INFO_MAX, tb[IFLA_LINKINFO],
				    NULL, NULL);
	nla_parse_nested_deprecated(data, IFLA_WWAN_MAX,
				    linkinfo[IFLA_INFO_DATA], NULL, NULL);

	rtnl_lock();

	dev = rtnl_create_link(&init_net, "wwan%d", NET_NAME_ENUM,
			       &wwan_rtnl_link_ops, tb, NULL);
	if (WARN_ON(IS_ERR(dev)))
		goto unlock;

	if (WARN_ON(wwan_rtnl_newlink(&init_net, dev, tb, data, NULL))) {
		free_netdev(dev);
		goto unlock;
	}

	rtnl_configure_link(dev, NULL, 0, NULL); /* Link initialized, notify new link */

unlock:
	rtnl_unlock();

free_attrs:
	nlmsg_free(msg);
}

/**
 * wwan_register_ops - register WWAN device ops
 * @parent: Device to use as parent and shared by all WWAN ports and
 *	created netdevs
 * @ops: operations to register
 * @ctxt: context to pass to operations
 * @def_link_id: id of the default link that will be automatically created by
 *	the WWAN core for the WWAN device. The default link will not be created
 *	if the passed value is WWAN_NO_DEFAULT_LINK.
 *
 * Returns: 0 on success, a negative error code on failure
 */
int wwan_register_ops(struct device *parent, const struct wwan_ops *ops,
		      void *ctxt, u32 def_link_id)
{
	struct wwan_device *wwandev;

	if (WARN_ON(!parent || !ops || !ops->setup))
		return -EINVAL;

	wwandev = wwan_create_dev(parent);
	if (IS_ERR(wwandev))
		return PTR_ERR(wwandev);

	if (WARN_ON(wwandev->ops)) {
		wwan_remove_dev(wwandev);
		return -EBUSY;
	}

	wwandev->ops = ops;
	wwandev->ops_ctxt = ctxt;

	/* NB: we do not abort ops registration in case of default link
	 * creation failure. Link ops is the management interface, while the
	 * default link creation is a service option. And we should not prevent
	 * a user from manually creating a link latter if service option failed
	 * now.
	 */
	if (def_link_id != WWAN_NO_DEFAULT_LINK)
		wwan_create_default_link(wwandev, def_link_id);

	return 0;
}
EXPORT_SYMBOL_GPL(wwan_register_ops);

/* Enqueue child netdev deletion */
static int wwan_child_dellink(struct device *dev, void *data)
{
	struct list_head *kill_list = data;

	if (dev->type == &wwan_type)
		wwan_rtnl_dellink(to_net_dev(dev), kill_list);

	return 0;
}

/**
 * wwan_unregister_ops - remove WWAN device ops
 * @parent: Device to use as parent and shared by all WWAN ports and
 *	created netdevs
 */
void wwan_unregister_ops(struct device *parent)
{
	struct wwan_device *wwandev = wwan_dev_get_by_parent(parent);
	LIST_HEAD(kill_list);

	if (WARN_ON(IS_ERR(wwandev)))
		return;
	if (WARN_ON(!wwandev->ops)) {
		put_device(&wwandev->dev);
		return;
	}

	/* put the reference obtained by wwan_dev_get_by_parent(),
	 * we should still have one (that the owner is giving back
	 * now) due to the ops being assigned.
	 */
	put_device(&wwandev->dev);

	rtnl_lock();	/* Prevent concurrent netdev(s) creation/destroying */

	/* Remove all child netdev(s), using batch removing */
	device_for_each_child(&wwandev->dev, &kill_list,
			      wwan_child_dellink);
	unregister_netdevice_many(&kill_list);

	wwandev->ops = NULL;	/* Finally remove ops */

	rtnl_unlock();

	wwandev->ops_ctxt = NULL;
	wwan_remove_dev(wwandev);
}
EXPORT_SYMBOL_GPL(wwan_unregister_ops);

static int __init wwan_init(void)
{
	int err;

	err = rtnl_link_register(&wwan_rtnl_link_ops);
	if (err)
		return err;

	err = class_register(&wwan_class);
	if (err)
		goto unregister;

	/* chrdev used for wwan ports */
	wwan_major = __register_chrdev(0, 0, WWAN_MAX_MINORS, "wwan_port",
				       &wwan_port_fops);
	if (wwan_major < 0) {
		err = wwan_major;
		goto destroy;
	}

#ifdef CONFIG_WWAN_DEBUGFS
	wwan_debugfs_dir = debugfs_create_dir("wwan", NULL);
#endif

	return 0;

destroy:
	class_unregister(&wwan_class);
unregister:
	rtnl_link_unregister(&wwan_rtnl_link_ops);
	return err;
}

static void __exit wwan_exit(void)
{
	debugfs_remove_recursive(wwan_debugfs_dir);
	__unregister_chrdev(wwan_major, 0, WWAN_MAX_MINORS, "wwan_port");
	rtnl_link_unregister(&wwan_rtnl_link_ops);
	class_unregister(&wwan_class);
}

module_init(wwan_init);
module_exit(wwan_exit);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("WWAN core");
MODULE_LICENSE("GPL v2");
