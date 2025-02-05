// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus driver for the Raw protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/uaccess.h>
#include <linux/greybus.h>

struct gb_raw {
	struct gb_connection *connection;

	struct list_head list;
	int list_data;
	struct mutex list_lock;
	dev_t dev;
	struct cdev cdev;
	struct device *device;
};

struct raw_data {
	struct list_head entry;
	u32 len;
	u8 data[] __counted_by(len);
};

static const struct class raw_class = {
	.name = "gb_raw",
};

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
	struct device *dev = &raw->connection->bundle->dev;
	int retval = 0;

	if (len > MAX_PACKET_SIZE) {
		dev_err(dev, "Too big of a data packet, rejected\n");
		return -EINVAL;
	}

	mutex_lock(&raw->list_lock);
	if ((raw->list_data + len) > MAX_DATA_SIZE) {
		dev_err(dev, "Too much data in receive buffer, now dropping packets\n");
		retval = -EINVAL;
		goto exit;
	}

	raw_data = kmalloc(struct_size(raw_data, data, len), GFP_KERNEL);
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

static int gb_raw_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct device *dev = &connection->bundle->dev;
	struct gb_raw *raw = greybus_get_drvdata(connection->bundle);
	struct gb_raw_send_request *receive;
	u32 len;

	if (op->type != GB_RAW_TYPE_SEND) {
		dev_err(dev, "unknown request type 0x%02x\n", op->type);
		return -EINVAL;
	}

	/* Verify size of payload */
	if (op->request->payload_size < sizeof(*receive)) {
		dev_err(dev, "raw receive request too small (%zu < %zu)\n",
			op->request->payload_size, sizeof(*receive));
		return -EINVAL;
	}
	receive = op->request->payload;
	len = le32_to_cpu(receive->len);
	if (len != (int)(op->request->payload_size - sizeof(__le32))) {
		dev_err(dev, "raw receive request wrong size %d vs %d\n", len,
			(int)(op->request->payload_size - sizeof(__le32)));
		return -EINVAL;
	}
	if (len == 0) {
		dev_err(dev, "raw receive request of 0 bytes?\n");
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

static int gb_raw_probe(struct gb_bundle *bundle,
			const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_raw *raw;
	int retval;
	int minor;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_RAW)
		return -ENODEV;

	raw = kzalloc(sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gb_raw_request_handler);
	if (IS_ERR(connection)) {
		retval = PTR_ERR(connection);
		goto error_free;
	}

	INIT_LIST_HEAD(&raw->list);
	mutex_init(&raw->list_lock);

	raw->connection = connection;
	greybus_set_drvdata(bundle, raw);

	minor = ida_alloc(&minors, GFP_KERNEL);
	if (minor < 0) {
		retval = minor;
		goto error_connection_destroy;
	}

	raw->dev = MKDEV(raw_major, minor);
	cdev_init(&raw->cdev, &raw_fops);

	retval = gb_connection_enable(connection);
	if (retval)
		goto error_remove_ida;

	retval = cdev_add(&raw->cdev, raw->dev, 1);
	if (retval)
		goto error_connection_disable;

	raw->device = device_create(&raw_class, &connection->bundle->dev,
				    raw->dev, raw, "gb!raw%d", minor);
	if (IS_ERR(raw->device)) {
		retval = PTR_ERR(raw->device);
		goto error_del_cdev;
	}

	return 0;

error_del_cdev:
	cdev_del(&raw->cdev);

error_connection_disable:
	gb_connection_disable(connection);

error_remove_ida:
	ida_free(&minors, minor);

error_connection_destroy:
	gb_connection_destroy(connection);

error_free:
	kfree(raw);
	return retval;
}

static void gb_raw_disconnect(struct gb_bundle *bundle)
{
	struct gb_raw *raw = greybus_get_drvdata(bundle);
	struct gb_connection *connection = raw->connection;
	struct raw_data *raw_data;
	struct raw_data *temp;

	// FIXME - handle removing a connection when the char device node is open.
	device_destroy(&raw_class, raw->dev);
	cdev_del(&raw->cdev);
	gb_connection_disable(connection);
	ida_free(&minors, MINOR(raw->dev));
	gb_connection_destroy(connection);

	mutex_lock(&raw->list_lock);
	list_for_each_entry_safe(raw_data, temp, &raw->list, entry) {
		list_del(&raw_data->entry);
		kfree(raw_data);
	}
	mutex_unlock(&raw->list_lock);

	kfree(raw);
}

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

static const struct greybus_bundle_id gb_raw_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_RAW) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_raw_id_table);

static struct greybus_driver gb_raw_driver = {
	.name		= "raw",
	.probe		= gb_raw_probe,
	.disconnect	= gb_raw_disconnect,
	.id_table	= gb_raw_id_table,
};

static int raw_init(void)
{
	dev_t dev;
	int retval;

	retval = class_register(&raw_class);
	if (retval)
		goto error_class;

	retval = alloc_chrdev_region(&dev, 0, NUM_MINORS, "gb_raw");
	if (retval < 0)
		goto error_chrdev;

	raw_major = MAJOR(dev);

	retval = greybus_register(&gb_raw_driver);
	if (retval)
		goto error_gb;

	return 0;

error_gb:
	unregister_chrdev_region(dev, NUM_MINORS);
error_chrdev:
	class_unregister(&raw_class);
error_class:
	return retval;
}
module_init(raw_init);

static void __exit raw_exit(void)
{
	greybus_deregister(&gb_raw_driver);
	unregister_chrdev_region(MKDEV(raw_major, 0), NUM_MINORS);
	class_unregister(&raw_class);
	ida_destroy(&minors);
}
module_exit(raw_exit);

MODULE_DESCRIPTION("Greybus driver for the Raw protocol");
MODULE_LICENSE("GPL v2");
