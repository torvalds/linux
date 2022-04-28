// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Intel Corporation.*/

#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/ptr_ring.h>
#include <linux/workqueue.h>

#include <linux/i3c/device.h>

#define I3C_TARGET_MCTP_MINORS	32
#define RX_RING_COUNT		16

static struct class *i3c_target_mctp_class;
static dev_t i3c_target_mctp_devt;
static DEFINE_IDA(i3c_target_mctp_ida);

struct mctp_client;

struct i3c_target_mctp {
	struct i3c_device *i3cdev;
	struct cdev cdev;
	int id;
	struct mctp_client *client;
	spinlock_t client_lock; /* to protect client access */
};

struct mctp_client {
	struct kref ref;
	struct i3c_target_mctp *priv;
	struct ptr_ring rx_queue;
	wait_queue_head_t wait_queue;
};

struct mctp_packet {
	u8 *data;
	u16 count;
};

static void *i3c_target_mctp_packet_alloc(u16 count)
{
	struct mctp_packet *packet;
	u8 *data;

	packet = kzalloc(sizeof(*packet), GFP_ATOMIC);
	if (!packet)
		return NULL;

	data = kzalloc(count, GFP_ATOMIC);
	if (!data) {
		kfree(packet);
		return NULL;
	}

	packet->data = data;
	packet->count = count;

	return packet;
}

static void i3c_target_mctp_packet_free(void *data)
{
	struct mctp_packet *packet = data;

	kfree(packet->data);
	kfree(packet);
}

static struct mctp_client *i3c_target_mctp_client_alloc(struct i3c_target_mctp *priv)
{
	struct mctp_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		goto out;

	kref_init(&client->ref);
	client->priv = priv;
	ptr_ring_init(&client->rx_queue, RX_RING_COUNT, GFP_KERNEL);
out:
	return client;
}

static void i3c_target_mctp_client_free(struct kref *ref)
{
	struct mctp_client *client = container_of(ref, typeof(*client), ref);

	ptr_ring_cleanup(&client->rx_queue, &i3c_target_mctp_packet_free);

	kfree(client);
}

static void i3c_target_mctp_client_get(struct mctp_client *client)
{
	kref_get(&client->ref);
}

static void i3c_target_mctp_client_put(struct mctp_client *client)
{
	kref_put(&client->ref, &i3c_target_mctp_client_free);
}

static void
i3c_target_mctp_rx_packet_enqueue(struct i3c_device *i3cdev, const u8 *data, size_t count)
{
	struct i3c_target_mctp *priv = dev_get_drvdata(i3cdev_to_dev(i3cdev));
	struct mctp_client *client;
	struct mctp_packet *packet;
	int ret;

	spin_lock(&priv->client_lock);
	client = priv->client;
	if (client)
		i3c_target_mctp_client_get(client);
	spin_unlock(&priv->client_lock);

	if (!client)
		return;

	packet = i3c_target_mctp_packet_alloc(count);
	if (!packet)
		goto err;

	memcpy(packet->data, data, count);

	ret = ptr_ring_produce(&client->rx_queue, packet);
	if (ret)
		i3c_target_mctp_packet_free(packet);
	else
		wake_up_all(&client->wait_queue);
err:
	i3c_target_mctp_client_put(client);
}

static struct mctp_client *i3c_target_mctp_create_client(struct i3c_target_mctp *priv)
{
	struct mctp_client *client;
	int ret;

	/* Currently, we support just one client. */
	spin_lock_irq(&priv->client_lock);
	ret = priv->client ? -EBUSY : 0;
	spin_unlock_irq(&priv->client_lock);

	if (ret)
		return ERR_PTR(ret);

	client = i3c_target_mctp_client_alloc(priv);
	if (!client)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&client->wait_queue);

	spin_lock_irq(&priv->client_lock);
	priv->client = client;
	spin_unlock_irq(&priv->client_lock);

	return client;
}

static void i3c_target_mctp_delete_client(struct mctp_client *client)
{
	struct i3c_target_mctp *priv = client->priv;

	spin_lock_irq(&priv->client_lock);
	priv->client = NULL;
	spin_unlock_irq(&priv->client_lock);

	i3c_target_mctp_client_put(client);
}

static int i3c_target_mctp_open(struct inode *inode, struct file *file)
{
	struct i3c_target_mctp *priv = container_of(inode->i_cdev, struct i3c_target_mctp, cdev);
	struct mctp_client *client;

	client = i3c_target_mctp_create_client(priv);
	if (IS_ERR(client))
		return PTR_ERR(client);

	file->private_data = client;

	return 0;
}

static int i3c_target_mctp_release(struct inode *inode, struct file *file)
{
	struct mctp_client *client = file->private_data;

	i3c_target_mctp_delete_client(client);

	return 0;
}

static ssize_t i3c_target_mctp_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct mctp_client *client = file->private_data;
	struct mctp_packet *rx_packet;

	rx_packet = ptr_ring_consume_irq(&client->rx_queue);
	if (!rx_packet)
		return -EAGAIN;

	if (count < rx_packet->count) {
		count = -EINVAL;
		goto err_free;
	}
	if (count > rx_packet->count)
		count = rx_packet->count;

	if (copy_to_user(buf, rx_packet->data, count))
		count = -EFAULT;
err_free:
	i3c_target_mctp_packet_free(rx_packet);

	return count;
}

static ssize_t i3c_target_mctp_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct mctp_client *client = file->private_data;
	struct i3c_target_mctp *priv = client->priv;
	struct i3c_priv_xfer xfers[1] = {};
	u8 *tx_data;
	int ret;

	tx_data = kzalloc(count, GFP_KERNEL);
	if (!tx_data)
		return -ENOMEM;

	if (copy_from_user(tx_data, buf, count)) {
		ret = -EFAULT;
		goto out_packet;
	}

	xfers[0].data.out = tx_data;
	xfers[0].len = count;

	ret = i3c_device_do_priv_xfers(priv->i3cdev, xfers, ARRAY_SIZE(xfers));
	if (ret)
		goto out_packet;
	ret = count;

	/*
	 * TODO: Add support for IBI generation - it should be done only if IBI
	 * are enabled (the Active Controller may disabled them using CCC for
	 * that). Otherwise (if IBIs are disabled), we should make sure that when
	 * Active Controller issues GETSTATUS CCC the return value indicates
	 * that data is ready.
	 */
out_packet:
	kfree(tx_data);
	return ret;
}

static __poll_t i3c_target_mctp_poll(struct file *file, struct poll_table_struct *pt)
{
	struct mctp_client *client = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &client->wait_queue, pt);

	if (__ptr_ring_peek(&client->rx_queue))
		ret |= EPOLLIN;

	/*
	 * TODO: Add support for "write" readiness.
	 * DW-I3C has a hardware queue that has finite number of entries.
	 * If we try to issue more writes that space in this queue allows for,
	 * we're in trouble. This should be handled by error from write() and
	 * poll() blocking for write events.
	 */
	return ret;
}

static const struct file_operations i3c_target_mctp_fops = {
	.owner = THIS_MODULE,
	.open = i3c_target_mctp_open,
	.release = i3c_target_mctp_release,
	.read = i3c_target_mctp_read,
	.write = i3c_target_mctp_write,
	.poll = i3c_target_mctp_poll,
};

static struct i3c_target_read_setup i3c_target_mctp_rx_packet_setup = {
	.handler = i3c_target_mctp_rx_packet_enqueue,
};

static int i3c_target_mctp_probe(struct i3c_device *i3cdev)
{
	struct device *parent = i3cdev_to_dev(i3cdev);
	struct i3c_target_mctp *priv;
	struct device *dev;
	int ret;

	priv = devm_kzalloc(parent, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = ida_alloc(&i3c_target_mctp_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;
	priv->id = ret;

	priv->i3cdev = i3cdev;
	spin_lock_init(&priv->client_lock);

	cdev_init(&priv->cdev, &i3c_target_mctp_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, i3c_target_mctp_devt, 1);
	if (ret) {
		ida_free(&i3c_target_mctp_ida, priv->id);
		return ret;
	}

	dev = device_create(i3c_target_mctp_class, parent, i3c_target_mctp_devt,
			    NULL, "i3c-mctp-target-%d", priv->id);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err;
	}

	i3cdev_set_drvdata(i3cdev, priv);

	i3c_target_read_register(i3cdev, &i3c_target_mctp_rx_packet_setup);

	return 0;
err:
	cdev_del(&priv->cdev);
	ida_free(&i3c_target_mctp_ida, priv->id);

	return ret;
}

static void i3c_target_mctp_remove(struct i3c_device *i3cdev)
{
	struct i3c_target_mctp *priv = dev_get_drvdata(i3cdev_to_dev(i3cdev));

	device_destroy(i3c_target_mctp_class, i3c_target_mctp_devt);
	cdev_del(&priv->cdev);
	ida_free(&i3c_target_mctp_ida, priv->id);
}

static const struct i3c_device_id i3c_target_mctp_ids[] = {
	I3C_CLASS(0xcc, 0x0),
	{ },
};

static struct i3c_driver i3c_target_mctp_drv = {
	.driver.name = "i3c-target-mctp",
	.id_table = i3c_target_mctp_ids,
	.probe = i3c_target_mctp_probe,
	.remove = i3c_target_mctp_remove,
	.target = true,
};

static int i3c_target_mctp_init(struct i3c_driver *drv)
{
	int ret;

	ret = alloc_chrdev_region(&i3c_target_mctp_devt, 0,
				  I3C_TARGET_MCTP_MINORS, "i3c-target-mctp");
	if (ret)
		return ret;

	i3c_target_mctp_class = class_create(THIS_MODULE, "i3c-target-mctp");
	if (IS_ERR(i3c_target_mctp_class)) {
		unregister_chrdev_region(i3c_target_mctp_devt, I3C_TARGET_MCTP_MINORS);
		return PTR_ERR(i3c_target_mctp_class);
	}

	return i3c_driver_register(drv);
}

static void i3c_target_mctp_fini(struct i3c_driver *drv)
{
	i3c_driver_unregister(drv);
	class_destroy(i3c_target_mctp_class);
	unregister_chrdev_region(i3c_target_mctp_devt, I3C_TARGET_MCTP_MINORS);
}

module_driver(i3c_target_mctp_drv, i3c_target_mctp_init, i3c_target_mctp_fini);
MODULE_AUTHOR("Iwona Winiarska <iwona.winiarska@intel.com>");
MODULE_DESCRIPTION("I3C Target MCTP driver");
MODULE_LICENSE("GPL");
