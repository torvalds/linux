/*
 * Greybus driver for the Raw protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/idr.h>

#include "greybus.h"

struct gb_raw {
	struct gb_connection *connection;
	u8 version_major;
	u8 version_minor;

	struct list_head list;
	int list_data;
	struct mutex list_lock;
	dev_t dev;
	struct cdev cdev;
	struct device *device;
};

/* Version of the Greybus raw protocol we support */
#define	GB_RAW_VERSION_MAJOR			0x00
#define	GB_RAW_VERSION_MINOR			0x01

/* Greybus raw request types */
#define	GB_RAW_TYPE_INVALID			0x00
#define	GB_RAW_TYPE_PROTOCOL_VERSION		0x01
#define	GB_RAW_TYPE_SEND			0x02

/* Define get_version() routine */
define_get_version(gb_raw, RAW);

struct gb_raw_send_request {
	__le32	len;
	__u8	data[0];
};

struct raw_data {
	struct list_head entry;
	u32 len;
	u8 data[0];
};

static struct class *raw_class;
static int raw_major;
static const struct file_operations raw_fops;
static DEFINE_IDA(minors);

/* Number of minor devices this driver supports */
#define NUM_MINORS	256

/* Maximum size of any one send data buffer we support */
#define MAX_PACKET_SIZE	(PAGE_SIZE * 2)

/*
 * Maximum size of the data in the receive buffer we allow before we start to
 * drop messages on the floor
 */
#define MAX_DATA_SIZE	(MAX_PACKET_SIZE * 8)

/*
 * Add the raw data message to the list of received messages.
 */
static int receive_data(struct gb_raw *raw, u32 len, u8 *data)
{
	struct raw_data *raw_data;
	int retval = 0;

	if (len > MAX_PACKET_SIZE) {
		dev_err(raw->device, "Too big of a data packet, rejected\n");
		return -EINVAL;
	}

	mutex_lock(&raw->list_lock);
	if ((raw->list_data + len) > MAX_DATA_SIZE) {
		dev_err(raw->device,
			"Too much data in receive buffer, now dropping packets\n");
		retval = -EINVAL;
		goto exit;
	}

	raw_data = kmalloc(sizeof(*raw_data) + len, GFP_KERNEL);
	if (!raw_data) {
		retval = -ENOMEM;
		goto exit;
	}

	raw->list_data += len;
	raw_data->len = len;
	memcpy(&raw_data->data[0], data, len);

	list_add_tail(&raw_data->entry, &raw->list);
exit:
	mutex_unlock(&raw->list_lock);
	return retval;
}

static int gb_raw_receive(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_raw *raw = connection->private;
	struct gb_raw_send_request *receive;
	u32 len;

	if (type != GB_RAW_TYPE_SEND) {
		dev_err(raw->device, "unknown request type %d\n", type);
		return -EINVAL;
	}

	/* Verify size of payload */
	if (op->request->payload_size < sizeof(*receive)) {
		dev_err(raw->device, "raw receive request too small\n");
		return -EINVAL;
	}
	receive = op->request->payload;
	len = le32_to_cpu(receive->len);
	if (len != (int)(op->request->payload_size - sizeof(__le32))) {
		dev_err(raw->device,
			"raw receive request wrong size %d vs %d\n",
			len,
			(int)(op->request->payload_size - sizeof(__le32)));
		return -EINVAL;
	}
	if (len == 0) {
		dev_err(raw->device, "raw receive request of 0 bytes?\n");
		return -EINVAL;
	}

	return receive_data(raw, len, receive->data);
}

static int gb_raw_send(struct gb_raw *raw, u32 len, const char __user *data)
{
	struct gb_connection *connection = raw->connection;
	struct gb_raw_send_request *request;
	int retval;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	if (copy_from_user(&request->data[0], data, len)) {
		kfree(request);
		return -EFAULT;
	}

	request->len = cpu_to_le32(len);

	retval = gb_operation_sync(connection, GB_RAW_TYPE_SEND,
				   request, len + sizeof(*request),
				   NULL, 0);

	kfree(request);
	return retval;
}

static int gb_raw_connection_init(struct gb_connection *connection)
{
	struct gb_raw *raw;
	int retval;
	int minor;

	raw = kzalloc(sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	raw->connection = connection;
	connection->private = raw;

	/* Check the protocol version */
	retval = get_version(raw);
	if (retval)
		goto error_free;

	INIT_LIST_HEAD(&raw->list);
	mutex_init(&raw->list_lock);

	minor = ida_simple_get(&minors, 0, 0, GFP_KERNEL);
	if (minor < 0) {
		retval = minor;
		goto error_free;
	}

	raw->dev = MKDEV(raw_major, minor);
	cdev_init(&raw->cdev, &raw_fops);
	retval = cdev_add(&raw->cdev, raw->dev, 1);
	if (retval)
		goto error_cdev;

	raw->device = device_create(raw_class, &connection->dev, raw->dev, raw,
				    "gb!raw%d", minor);
	if (IS_ERR(raw->device)) {
		retval = PTR_ERR(raw->device);
		goto error_device;
	}

	return 0;

error_device:
	cdev_del(&raw->cdev);

error_cdev:
	ida_simple_remove(&minors, minor);

error_free:
	kfree(raw);
	return retval;
}

static void gb_raw_connection_exit(struct gb_connection *connection)
{
	struct gb_raw *raw = connection->private;
	struct raw_data *raw_data;
	struct raw_data *temp;

	// FIXME - handle removing a connection when the char device node is open.
	cdev_del(&raw->cdev);
	ida_simple_remove(&minors, MINOR(raw->dev));
	device_del(raw->device);
	mutex_lock(&raw->list_lock);
	list_for_each_entry_safe(raw_data, temp, &raw->list, entry) {
		list_del(&raw_data->entry);
		kfree(raw_data);
	}
	mutex_unlock(&raw->list_lock);

	kfree(raw);
}

static struct gb_protocol raw_protocol = {
	.name			= "raw",
	.id			= GREYBUS_PROTOCOL_RAW,
	.major			= GB_RAW_VERSION_MAJOR,
	.minor			= GB_RAW_VERSION_MINOR,
	.connection_init	= gb_raw_connection_init,
	.connection_exit	= gb_raw_connection_exit,
	.request_recv		= gb_raw_receive,
};

/*
 * Character device node interfaces.
 *
 * Note, we are using read/write to only allow a single read/write per message.
 * This means for read(), you have to provide a big enough buffer for the full
 * message to be copied into.  If the buffer isn't big enough, the read() will
 * fail with -ENOSPC.
 */

static int raw_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct gb_raw *raw = container_of(cdev, struct gb_raw, cdev);

	file->private_data = raw;
	return 0;
}

static ssize_t raw_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct gb_raw *raw = file->private_data;
	int retval;

	if (!count)
		return 0;

	if (count > MAX_PACKET_SIZE)
		return -E2BIG;

	retval = gb_raw_send(raw, count, buf);
	if (retval)
		return retval;

	return count;
}

static ssize_t raw_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct gb_raw *raw = file->private_data;
	int retval = 0;
	struct raw_data *raw_data;

	mutex_lock(&raw->list_lock);
	if (list_empty(&raw->list))
		goto exit;

	raw_data = list_first_entry(&raw->list, struct raw_data, entry);
	if (raw_data->len > count) {
		retval = -ENOSPC;
		goto exit;
	}

	if (copy_to_user(buf, &raw_data->data[0], raw_data->len)) {
		retval = -EFAULT;
		goto exit;
	}

	list_del(&raw_data->entry);
	raw->list_data -= raw_data->len;
	retval = raw_data->len;
	kfree(raw_data);

exit:
	mutex_unlock(&raw->list_lock);
	return retval;
}

static const struct file_operations raw_fops = {
	.owner		= THIS_MODULE,
	.write		= raw_write,
	.read		= raw_read,
	.open		= raw_open,
	.llseek		= noop_llseek,
};

static int raw_init(void)
{
	dev_t dev;
	int retval;

	raw_class = class_create(THIS_MODULE, "gb_raw");
	if (IS_ERR(raw_class)) {
		retval = PTR_ERR(raw_class);
		goto error_class;
	}

	retval = alloc_chrdev_region(&dev, 0, NUM_MINORS, "gb_raw");
	if (retval < 0)
		goto error_chrdev;


	raw_major = MAJOR(dev);

	retval = gb_protocol_register(&raw_protocol);
	if (retval)
		goto error_gb;

	return 0;

error_gb:
	unregister_chrdev_region(dev, NUM_MINORS);
error_chrdev:
	class_destroy(raw_class);
error_class:
	return retval;
}

static void __exit raw_exit(void)
{
	gb_protocol_deregister(&raw_protocol);
	unregister_chrdev_region(MKDEV(raw_major, 0), NUM_MINORS);
	class_destroy(raw_class);
}

module_init(raw_init);
module_exit(raw_exit);

MODULE_LICENSE("GPL v2");
