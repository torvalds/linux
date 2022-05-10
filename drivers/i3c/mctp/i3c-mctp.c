// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Intel Corporation.*/

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/preempt.h>
#include <linux/ptr_ring.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/i3c/device.h>

#include <linux/i3c/mctp/i3c-mctp.h>

#define I3C_MCTP_MINORS				32
#define CCC_DEVICE_STATUS_PENDING_INTR(x)	(((x) & GENMASK(3, 0)) >> 0)
#define POLLING_TIMEOUT_MS			50
#define MCTP_INTERRUPT_NUMBER			1
#define RX_RING_COUNT				16

struct i3c_mctp {
	struct i3c_device *i3c;
	struct cdev cdev;
	struct device *dev;
	struct delayed_work polling_work;
	int id;
	/*
	 * Restrict an access to the /dev descriptor to one
	 * user at a time.
	 */
	spinlock_t device_file_lock;
	int device_open;
	/* Currently only one userspace client is supported */
	struct i3c_mctp_client *default_client;
};

struct i3c_mctp_client {
	struct i3c_mctp *priv;
	struct ptr_ring rx_queue;
	wait_queue_head_t wait_queue;
};

static struct class *i3c_mctp_class;
static dev_t i3c_mctp_devt;
static DEFINE_IDA(i3c_mctp_ida);

static struct kmem_cache *packet_cache;

/**
 * i3c_mctp_packet_alloc() - allocates i3c_mctp_packet
 *
 * @flags: the type of memory to allocate
 *
 * Allocates i3c_mctp_packet via slab allocation
 * Return: pointer to the packet, NULL if some error occurred
 */
void *i3c_mctp_packet_alloc(gfp_t flags)
{
	return kmem_cache_alloc(packet_cache, flags);
}
EXPORT_SYMBOL_GPL(i3c_mctp_packet_alloc);

/**
 * i3c_mctp_packet_free() - frees i3c_mctp_packet
 *
 * @packet: pointer to the packet which should be freed
 *
 * Frees i3c_mctp_packet previously allocated via slab allocation
 */
void i3c_mctp_packet_free(void *packet)
{
	kmem_cache_free(packet_cache, packet);
}
EXPORT_SYMBOL_GPL(i3c_mctp_packet_free);

static void i3c_mctp_client_free(struct i3c_mctp_client *client)
{
	ptr_ring_cleanup(&client->rx_queue, &i3c_mctp_packet_free);

	kfree(client);
}

static struct i3c_mctp_client *i3c_mctp_client_alloc(struct i3c_mctp *priv)
{
	struct i3c_mctp_client *client;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		goto out;

	client->priv = priv;
	ret = ptr_ring_init(&client->rx_queue, RX_RING_COUNT, GFP_KERNEL);
	if (ret)
		return ERR_PTR(ret);
	init_waitqueue_head(&client->wait_queue);
out:
	return client;
}

static struct i3c_mctp_client *i3c_mctp_find_client(struct i3c_mctp *priv,
						    struct i3c_mctp_packet *packet)
{
	return priv->default_client;
}

static struct i3c_mctp_packet *i3c_mctp_read_packet(struct i3c_device *i3c)
{
	struct i3c_mctp_packet *rx_packet;
	struct i3c_priv_xfer xfers = {
		.rnw = true,
	};
	int ret;

	rx_packet = i3c_mctp_packet_alloc(GFP_KERNEL);
	if (!rx_packet)
		return ERR_PTR(-ENOMEM);

	rx_packet->size = I3C_MCTP_PACKET_SIZE;
	xfers.len = rx_packet->size;
	xfers.data.in = &rx_packet->data;

	ret = i3c_device_do_priv_xfers(i3c, &xfers, 1);
	if (ret) {
		i3c_mctp_packet_free(rx_packet);
		return ERR_PTR(ret);
	}

	return rx_packet;
}

static void i3c_mctp_dispatch_packet(struct i3c_mctp *priv, struct i3c_mctp_packet *packet)
{
	struct i3c_mctp_client *client = i3c_mctp_find_client(priv, packet);
	int ret;

	ret = ptr_ring_produce(&client->rx_queue, packet);
	if (ret)
		i3c_mctp_packet_free(packet);
	else
		wake_up_all(&client->wait_queue);
}

static void i3c_mctp_polling_work(struct work_struct *work)
{
	struct i3c_mctp *priv = container_of(to_delayed_work(work), struct i3c_mctp, polling_work);
	struct i3c_device *i3cdev = priv->i3c;
	struct i3c_mctp_packet *rx_packet;
	struct i3c_device_info info;
	int ret;

	i3c_device_get_info(i3cdev, &info);
	ret = i3c_device_getstatus_ccc(i3cdev, &info);
	if (ret)
		return;

	if (CCC_DEVICE_STATUS_PENDING_INTR(info.status) != MCTP_INTERRUPT_NUMBER)
		return;

	rx_packet = i3c_mctp_read_packet(i3cdev);
	if (IS_ERR(rx_packet))
		goto out;

	i3c_mctp_dispatch_packet(priv, rx_packet);
out:
	schedule_delayed_work(&priv->polling_work, msecs_to_jiffies(POLLING_TIMEOUT_MS));
}

static ssize_t i3c_mctp_write(struct file *file, const char __user *buf, size_t count,
			      loff_t *f_pos)
{
	struct i3c_mctp *priv = file->private_data;
	struct i3c_device *i3c = priv->i3c;
	struct i3c_priv_xfer xfers = {
		.rnw = false,
		.len = count,
	};
	u8 *data;
	int ret;

	data = memdup_user(buf, count);
	if (IS_ERR(data))
		return PTR_ERR(data);

	xfers.data.out = data;

	ret = i3c_device_do_priv_xfers(i3c, &xfers, 1);
	kfree(data);
	return ret ?: count;
}

static ssize_t i3c_mctp_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
	struct i3c_mctp *priv = file->private_data;
	struct i3c_mctp_client *client = priv->default_client;
	struct i3c_mctp_packet *rx_packet;

	rx_packet = ptr_ring_consume(&client->rx_queue);
	if (!rx_packet)
		return -EAGAIN;

	if (copy_to_user(buf, &rx_packet->data, count))
		return -EFAULT;

	i3c_mctp_packet_free(rx_packet);

	return count;
}

static int i3c_mctp_open(struct inode *inode, struct file *file)
{
	struct i3c_mctp *priv = container_of(inode->i_cdev, struct i3c_mctp, cdev);

	spin_lock(&priv->device_file_lock);
	if (priv->device_open) {
		spin_unlock(&priv->device_file_lock);
		return -EBUSY;
	}
	priv->device_open++;
	spin_unlock(&priv->device_file_lock);

	file->private_data = priv;

	return 0;
}

static int i3c_mctp_release(struct inode *inode, struct file *file)
{
	struct i3c_mctp *priv = file->private_data;

	spin_lock(&priv->device_file_lock);
	priv->device_open--;
	spin_unlock(&priv->device_file_lock);

	file->private_data = NULL;

	return 0;
}

static __poll_t i3c_mctp_poll(struct file *file, struct poll_table_struct *pt)
{
	struct i3c_mctp *priv = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &priv->default_client->wait_queue, pt);

	if (__ptr_ring_peek(&priv->default_client->rx_queue))
		ret |= EPOLLIN;

	return ret;
}

static const struct file_operations i3c_mctp_fops = {
	.owner = THIS_MODULE,
	.read = i3c_mctp_read,
	.write = i3c_mctp_write,
	.poll = i3c_mctp_poll,
	.open = i3c_mctp_open,
	.release = i3c_mctp_release,
};

static struct i3c_mctp *i3c_mctp_alloc(struct i3c_device *i3c)
{
	struct i3c_mctp *priv;
	int id;

	priv = devm_kzalloc(i3cdev_to_dev(i3c), sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&i3c_mctp_ida, GFP_KERNEL);
	if (id < 0) {
		pr_err("i3c_mctp: no minor number available!\n");
		return ERR_PTR(id);
	}

	priv->id = id;
	priv->i3c = i3c;

	spin_lock_init(&priv->device_file_lock);

	return priv;
}

static void i3c_mctp_ibi_handler(struct i3c_device *dev, const struct i3c_ibi_payload *payload)
{
	struct i3c_mctp *priv = dev_get_drvdata(i3cdev_to_dev(dev));
	struct i3c_mctp_packet *rx_packet;

	rx_packet = i3c_mctp_read_packet(dev);
	if (IS_ERR(rx_packet))
		return;

	i3c_mctp_dispatch_packet(priv, rx_packet);
}

static int i3c_mctp_init(struct i3c_driver *drv)
{
	int ret;

	packet_cache = kmem_cache_create_usercopy("mctp-i3c-packet",
						  sizeof(struct i3c_mctp_packet), 0, 0, 0,
						  sizeof(struct i3c_mctp_packet), NULL);
	if (IS_ERR(packet_cache)) {
		ret = PTR_ERR(packet_cache);
		goto out;
	}

	/* Dynamically request unused major number */
	ret = alloc_chrdev_region(&i3c_mctp_devt, 0, I3C_MCTP_MINORS, "i3c-mctp");
	if (ret)
		goto out;

	/* Create a class to populate sysfs entries*/
	i3c_mctp_class = class_create(THIS_MODULE, "i3c-mctp");
	if (IS_ERR(i3c_mctp_class)) {
		ret = PTR_ERR(i3c_mctp_class);
		goto out_unreg_chrdev;
	}

	i3c_driver_register(drv);

	return 0;

out_unreg_chrdev:
	unregister_chrdev_region(i3c_mctp_devt, I3C_MCTP_MINORS);
out:
	pr_err("i3c_mctp: driver initialisation failed\n");
	return ret;
}

static void i3c_mctp_free(struct i3c_driver *drv)
{
	i3c_driver_unregister(drv);
	class_destroy(i3c_mctp_class);
	unregister_chrdev_region(i3c_mctp_devt, I3C_MCTP_MINORS);
	kmem_cache_destroy(packet_cache);
}

static int i3c_mctp_enable_ibi(struct i3c_device *i3cdev)
{
	struct i3c_ibi_setup ibireq = {
		.handler = i3c_mctp_ibi_handler,
		.max_payload_len = 2,
		.num_slots = 10,
	};
	int ret;

	ret = i3c_device_request_ibi(i3cdev, &ibireq);
	if (ret)
		return ret;
	ret = i3c_device_enable_ibi(i3cdev);
	if (ret)
		i3c_device_free_ibi(i3cdev);

	return ret;
}

/**
 * i3c_mctp_get_eid() - receive MCTP EID assigned to the device
 *
 * @client: client for the device to get the EID for
 * @domain_id: requested domain ID
 * @eid: pointer to store EID value
 *
 * Receive MCTP endpoint ID dynamically assigned by the MCTP Bus Owner
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_mctp_get_eid(struct i3c_mctp_client *client, u8 domain_id, u8 *eid)
{
	/* TODO: Implement EID assignment basing on domain ID */
	*eid = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(i3c_mctp_get_eid);

/**
 * i3c_mctp_send_packet() - send mctp packet
 *
 * @tx_packet: the allocated packet that needs to be send via I3C
 * @i3c: i3c device to send the packet to
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_mctp_send_packet(struct i3c_device *i3c, struct i3c_mctp_packet *tx_packet)
{
	struct i3c_priv_xfer xfers = {
		.rnw = false,
		.len = tx_packet->size,
		.data.out = &tx_packet->data,
	};

	return i3c_device_do_priv_xfers(i3c, &xfers, 1);
}
EXPORT_SYMBOL_GPL(i3c_mctp_send_packet);

/**
 * i3c_mctp_receive_packet() - receive mctp packet
 *
 * @client: i3c_mctp_client to receive the packet from
 * @timeout: timeout, in jiffies
 *
 * The function will sleep for up to @timeout if no packet is ready to read.
 *
 * Returns struct i3c_mctp_packet from or ERR_PTR in case of error or the
 * timeout elapsed.
 */
struct i3c_mctp_packet *i3c_mctp_receive_packet(struct i3c_mctp_client *client,
						unsigned long timeout)
{
	struct i3c_mctp_packet *rx_packet;
	int ret;

	ret = wait_event_interruptible_timeout(client->wait_queue,
					       __ptr_ring_peek(&client->rx_queue), timeout);
	if (ret < 0)
		return ERR_PTR(ret);
	else if (ret == 0)
		return ERR_PTR(-ETIME);

	rx_packet = ptr_ring_consume(&client->rx_queue);
	if (!rx_packet)
		return ERR_PTR(-EAGAIN);

	return rx_packet;
}
EXPORT_SYMBOL_GPL(i3c_mctp_receive_packet);

static int i3c_mctp_probe(struct i3c_device *i3cdev)
{
	struct i3c_mctp *priv;
	struct device *dev = i3cdev_to_dev(i3cdev);
	int ret;

	priv = i3c_mctp_alloc(i3cdev);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	cdev_init(&priv->cdev, &i3c_mctp_fops);

	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, MKDEV(MAJOR(i3c_mctp_devt), priv->id), 1);
	if (ret)
		goto error_cdev;

	/* register this i3c device with the driver core */
	priv->dev = device_create(i3c_mctp_class, dev,
				  MKDEV(MAJOR(i3c_mctp_devt), priv->id),
				  NULL, "i3c-mctp-%d", priv->id);
	if (IS_ERR(priv->dev)) {
		ret = PTR_ERR(priv->dev);
		goto error;
	}

	ret = i3c_device_control_pec(i3cdev, true);
	if (ret)
		goto error;

	priv->default_client = i3c_mctp_client_alloc(priv);
	if (IS_ERR(priv->default_client))
		goto error;

	dev_set_drvdata(i3cdev_to_dev(i3cdev), priv);

	if (i3c_mctp_enable_ibi(i3cdev)) {
		INIT_DELAYED_WORK(&priv->polling_work, i3c_mctp_polling_work);
		schedule_delayed_work(&priv->polling_work, msecs_to_jiffies(POLLING_TIMEOUT_MS));
	}

	return 0;

error:
	cdev_del(&priv->cdev);
error_cdev:
	put_device(dev);
	return ret;
}

static void i3c_mctp_remove(struct i3c_device *i3cdev)
{
	struct i3c_mctp *priv = dev_get_drvdata(i3cdev_to_dev(i3cdev));

	i3c_device_disable_ibi(i3cdev);
	i3c_device_free_ibi(i3cdev);
	i3c_mctp_client_free(priv->default_client);
	priv->default_client = NULL;

	device_destroy(i3c_mctp_class, MKDEV(MAJOR(i3c_mctp_devt), priv->id));
	cdev_del(&priv->cdev);
	ida_free(&i3c_mctp_ida, priv->id);
}

static const struct i3c_device_id i3c_mctp_ids[] = {
	I3C_CLASS(0xCC, 0x0),
	{ },
};

static struct i3c_driver i3c_mctp_drv = {
	.driver.name = "i3c-mctp",
	.id_table = i3c_mctp_ids,
	.probe = i3c_mctp_probe,
	.remove = i3c_mctp_remove,
};

module_driver(i3c_mctp_drv, i3c_mctp_init, i3c_mctp_free);
MODULE_AUTHOR("Oleksandr Shulzhenko <oleksandr.shulzhenko.viktorovych@intel.com>");
MODULE_DESCRIPTION("I3C MCTP driver");
MODULE_LICENSE("GPL");
