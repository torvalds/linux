/*
 * HDMI Consumer Electronics Control, core module
 *
 * Copyright (C) 2011, Florian Fainelli <f.fainelli@gmail.com>
 *
 * This file is subject to the GPLv2 licensing terms.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#include <linux/hdmi-cec/hdmi-cec.h>
#include <linux/hdmi-cec/dev.h>

#define PFX	KBUILD_MODNAME ": "

#define CEC_RX_QUEUE_MAX_LEN	(20)

/*
 * CEC bus
 */
static int cec_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return 0;
}

static int cec_bus_match(struct device *dev, struct device_driver *driver)
{
	/* we have no way of matching a device with a driver yet */
	return 1;
}

static struct bus_type cec_bus_type = {
	.name		= "cec",
	.match		= cec_bus_match,
	.uevent		= cec_uevent,
};

/*
 * CEC driver
 */
static int cec_driver_probe(struct device *dev)
{
	struct cec_device *cec_dev = to_cec_device(dev);

	return cec_create_dev_node(cec_dev);
}

static int cec_driver_remove(struct device *dev)
{
	struct cec_driver *drv = to_cec_driver(dev->driver);

	cec_detach_host(drv);
	cec_flush_queues(drv);

	return 0;
}

/**
 * cec_set_logical_address() - sets the cec logical address
 * @drv:	driver pointer
 * @addr:	logical address
 *
 * calls the driver spefific set_logical_address callback to
 * set the cec adapter logical address.
 */
int cec_set_logical_address(struct cec_driver *drv, const u8 addr)
{
	int ret;

	if (addr > CEC_ADDR_MAX)
		return -EINVAL;

	mutex_lock(&drv->lock);
	ret = drv->ops->set_logical_address(drv, addr);
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 *  __cec_rx_queue_len() - returns the lenght of a cec driver rx queue
 * @drv:	driver pointer
 */
unsigned __cec_rx_queue_len(struct cec_driver *drv)
{
	unsigned qlen;

	mutex_lock(&drv->rx_msg_list_lock);
	qlen = drv->rx_msg_len;
	mutex_unlock(&drv->rx_msg_list_lock);

	return qlen;
}

/**
 * cec_flush_queues() - flushes a cec driver rx queue
 * @drv:	driver pointer
 */
void cec_flush_queues(struct cec_driver *drv)
{
	struct cec_kmsg *cur, *next;

	mutex_lock(&drv->lock);

	cancel_work_sync(&drv->tx_work);
	list_for_each_entry_safe(cur, next, &drv->rx_msg_list, next) {
		list_del(&cur->next);
		kfree(cur);
		drv->rx_msg_len--;
	}

	mutex_unlock(&drv->lock);
}

/**
 * cec_receive_message() - receive a cec message for a given driver
 * @drv::	driver pointer
 * @data:	message blob
 * @len:	message length
 *
 * Called by drivers to add a message to the driver's RX queue
 */
int cec_receive_message(struct cec_driver *drv, const u8 *data, const u8 len)
{
	struct cec_kmsg *kmsg;
	int ret = 0;

	if (!len || len > CEC_MAX_MSG_LEN)
		return -EINVAL;

	mutex_lock(&drv->lock);
	if (!drv->attached) {
		pr_debug("%s: no client attached, dropping", drv->name);
		goto out;
	}

	mutex_lock(&drv->rx_msg_list_lock);
	if (drv->rx_msg_len >= CEC_RX_QUEUE_MAX_LEN) {
		pr_debug("%s: queue full!\n", drv->name);
		ret = -ENOSPC;
		goto out_unlock;
	}

	kmsg = list_entry(drv->rx_msg_list.next,
				struct cec_kmsg,
				next);
	if (kmsg->status == CEC_MSG_SENT) {
		kmsg->status = CEC_MSG_COMPLETED;
		complete(&kmsg->completion);
	} else {
		kmsg = kzalloc(sizeof(*kmsg), GFP_KERNEL);
		if (!kmsg) {
			ret = ENOMEM;
			goto out_unlock;
		}

		list_add_tail(&kmsg->next, &drv->rx_msg_list);
		drv->rx_msg_len++;
	}

	memcpy(kmsg->msg.data, data, len);
	kmsg->msg.len = len;
out_unlock:
	mutex_unlock(&drv->rx_msg_list_lock);
out:
	mutex_unlock(&drv->lock);

	/* wake up clients, they can dequeue a buffer now */
	wake_up_interruptible(&drv->rx_wait);

	return ret;
}
EXPORT_SYMBOL(cec_receive_message);

/**
 * cec_dequeue_message() - dequeue a message from the driver's rx queue
 * @drv:	driver pointer
 * @msg:	cec user-space exposed message pointer
 *
 * Dequeue a message from the driver's RX queue
 */
int cec_dequeue_message(struct cec_driver *drv, struct cec_msg *msg)
{
	int ret = 0;
	struct cec_kmsg *kmsg;

	mutex_lock(&drv->lock);
	mutex_lock(&drv->rx_msg_list_lock);
	if (list_empty(&drv->rx_msg_list)) {
		pr_debug("%s: no message in queue\n", drv->name);
		ret = -ENOENT;
		goto out;
	}

	kmsg = list_entry(drv->rx_msg_list.next,
				struct cec_kmsg, next);
	memcpy(msg, &kmsg->msg, sizeof(*msg));
	list_del(&kmsg->next);
	kfree(kmsg);
	drv->rx_msg_len--;
out:
	mutex_unlock(&drv->rx_msg_list_lock);
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 * cec_read_message() - reads a cec message from the driver's rx queue
 * @drv:	driver pointer
 * @msg:	cec user-space exposed message pointer
 *
 * Reads a CEC message from the driver's RX queue in blocking mode with
 * either a finite or inifinite timeout
 */
int cec_read_message(struct cec_driver *drv, struct cec_msg *msg)
{
	int ret = 0;
	struct cec_kmsg *kmsg;
	unsigned timeout = 1;

	kmsg = kzalloc(sizeof(*kmsg), GFP_KERNEL);
	if (!kmsg)
		return -ENOMEM;

	kmsg->msg.timeout = msg->timeout;
	kmsg->status = CEC_MSG_SENT;
	init_completion(&kmsg->completion);

	mutex_lock(&drv->rx_msg_list_lock);
	list_add_tail(&kmsg->next, &drv->rx_msg_list);
	drv->rx_msg_len++;
	mutex_unlock(&drv->rx_msg_list_lock);

	/* timeout for a fixed duration or infinite */
	if (kmsg->msg.timeout > 0)
		timeout = wait_for_completion_interruptible_timeout(
						&kmsg->completion,
						kmsg->msg.timeout * HZ);
	else
		wait_for_completion_interruptible(&kmsg->completion);

	if (!__cec_rx_queue_len(drv))
		ret = -ENOENT;
	else if (!timeout || kmsg->status != CEC_MSG_COMPLETED)
		ret = -ETIMEDOUT;

	mutex_lock(&drv->rx_msg_list_lock);
	/* copy answer back to caller */
	memcpy(msg, &kmsg->msg, sizeof(*msg));
	list_del(&kmsg->next);
	kfree(kmsg);
	drv->rx_msg_len--;
	mutex_unlock(&drv->rx_msg_list_lock);

	return ret;
}

/*
 * CEC transmit work queue callback
 */
static void cec_tx_work(struct work_struct *work)
{
	struct cec_driver *drv;
	struct cec_kmsg *pending;

	drv = container_of(work, struct cec_driver, tx_work);

	mutex_lock(&drv->tx_msg_list_lock);
	if (list_empty(&drv->tx_msg_list))
		goto out_exit;

	pending = list_entry(drv->tx_msg_list.next,
				struct cec_kmsg, next);
	if (pending->status != CEC_MSG_QUEUED)
		goto out_exit;

	pending->status = CEC_MSG_SENT;
	pending->ret = drv->ops->send(drv, pending->msg.data,
					pending->msg.len);
	if (!pending->ret)
		pending->status = CEC_MSG_COMPLETED;

	/* complete the message anyway */
	complete(&pending->completion);

out_exit:
	mutex_unlock(&drv->tx_msg_list_lock);
	return;
}

/**
 * cec_send_message() - sends an user fed cec message
 * @drv:	driver pointer
 * @msg:	user-exposed cec message pointer
 *
 * Send a message using the specific adapter real sending done in workqueue
 */
int cec_send_message(struct cec_driver *drv,
			struct cec_msg *msg)
{
	int ret = 0;
	unsigned int timeout;
	struct cec_kmsg *kmsg;

	if (!msg->len || msg->len > CEC_MAX_MSG_LEN)
		return -EINVAL;

	mutex_lock(&drv->lock);
	kmsg = kzalloc(sizeof(*kmsg), GFP_KERNEL);
	if (!kmsg) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(&kmsg->msg, msg, sizeof(*msg));

	/* specification says there is a maximum 1s desired response time */
	if (!kmsg->msg.timeout)
		kmsg->msg.timeout = HZ;
	init_completion(&kmsg->completion);

	mutex_lock(&drv->tx_msg_list_lock);
	list_add_tail(&kmsg->next, &drv->tx_msg_list);
	mutex_unlock(&drv->tx_msg_list_lock);

	/* kick transmission */
	schedule_work(&drv->tx_work);
	timeout = wait_for_completion_interruptible_timeout(
						&kmsg->completion,
						kmsg->msg.timeout);
	mutex_lock(&drv->tx_msg_list_lock);
	if (kmsg->status != CEC_MSG_COMPLETED) {
		if (!timeout && kmsg->msg.timeout)
			ret = -ETIMEDOUT;
		else if (kmsg->msg.timeout)
			ret = kmsg->ret;
		else
			ret = 0;
	}

	list_del(&kmsg->next);
	kfree(kmsg);
	mutex_unlock(&drv->tx_msg_list_lock);
out:
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 * cec_reset_device() - resets a cec driver
 * @drv:	driver pointer
 *
 * Resets a CEC device to a sane state
 */
int cec_reset_device(struct cec_driver *drv)
{
	int ret;

	mutex_lock(&drv->lock);
	cancel_work_sync(&drv->tx_work);
	ret = drv->ops->reset(drv);
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 * cec_get_counters() - gets counters from a cec driver
 * @drv:	driver pointer
 * @cnt:	struct cec_counters pointer
 *
 * Get counters from the CEC adapter if supported, driver should advertise
 * CEC_HW_HAS_COUNTERS flag
 */
int cec_get_counters(struct cec_driver *drv, struct cec_counters *cnt)
{
	int ret = -ENOTSUPP;

	mutex_lock(&drv->lock);
	if (drv->flags & CEC_HW_HAS_COUNTERS)
		ret = drv->ops->get_counters(drv, cnt);
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 * cec_set_rx_mode() - sets the adapter receive mode
 * @drv:	driver pointer
 * @mode:	receive mode (accept all, unicast only)
 *
 * Set the receive mode filter of the driver
 */
int cec_set_rx_mode(struct cec_driver *drv, enum cec_rx_mode mode)
{
	int ret;

	if (~drv->flags & CEC_HW_HAS_RX_FILTER)
		return -ENOTSUPP;

	if (mode >= CEC_RX_MODE_MAX)
		return -EINVAL;

	mutex_lock(&drv->lock);
	ret = drv->ops->set_rx_mode(drv, mode);
	mutex_unlock(&drv->lock);

	return ret;
}

/**
 * cec_attach_host - attaches a host to the driver
 * @drv:	driver pointer
 *
 * Attaches the host to the driver. In case the hardware is able
 * to process CEC messages itself, it should now send them to the
 * host for processing
 */
int cec_attach_host(struct cec_driver *drv)
{
	int ret = 0;

	if (drv->attached)
		return -EBUSY;

	mutex_lock(&drv->lock);
	if (drv->ops->attach)
		ret =  drv->ops->attach(drv);
	if (!ret)
		drv->attached = true;
	mutex_unlock(&drv->lock);
	return ret;
}

/**
 * cec_detach_host - detaches a host from the driver
 * @drv:	driver pointer
 *
 * Detaches the host from the driver. In case the hardware is able
 * to process CEC messages itself, it should now keep the messages for
 * itself and no longer send them to the host
 */
int cec_detach_host(struct cec_driver *drv)
{
	mutex_lock(&drv->lock);
	if (drv->ops->detach)
		drv->ops->detach(drv);
	drv->attached = false;
	mutex_unlock(&drv->lock);

	return 0;
}

/**
 * register_cec_driver() - registers a new cec driver
 * @cec_drv:	cec_driver pointer
 */
int register_cec_driver(struct cec_driver *cec_drv)
{
	cec_drv->driver.bus = &cec_bus_type;
	cec_drv->driver.name = cec_drv->name;
	cec_drv->driver.probe = cec_driver_probe;
	cec_drv->driver.remove = cec_driver_remove;

	mutex_init(&cec_drv->lock);
	init_waitqueue_head(&cec_drv->rx_wait);

	mutex_init(&cec_drv->tx_msg_list_lock);
	INIT_LIST_HEAD(&cec_drv->tx_msg_list);
	INIT_WORK(&cec_drv->tx_work, cec_tx_work);

	mutex_init(&cec_drv->rx_msg_list_lock);
	INIT_LIST_HEAD(&cec_drv->rx_msg_list);
	cec_drv->rx_msg_len = 0;

	return driver_register(&cec_drv->driver);
}
EXPORT_SYMBOL(register_cec_driver);

/**
 * unregister_cec_driver() - unregisters a cec driver
 * @cec_drv:	cec_driver pointer
 */
void unregister_cec_driver(struct cec_driver *cec_drv)
{
	driver_unregister(&cec_drv->driver);
}
EXPORT_SYMBOL(unregister_cec_driver);


/*
 * CEC device
 */
static void cec_device_release(struct device *dev)
{
	return;
}

static unsigned cec_device_count;

/**
 * register_cec_device() - registers a new cec device
 * @cec_dev:	cec_device pointer
 */
int register_cec_device(struct cec_device *cec_dev)
{
	cec_dev->dev.bus = &cec_bus_type;
	dev_set_name(&cec_dev->dev, "cec%d", cec_device_count++);
	cec_dev->dev.release = cec_device_release;

	return device_register(&cec_dev->dev);
}
EXPORT_SYMBOL(register_cec_device);

/**
 * unregister_cec_device() - unregister a cec device
 * @cec_dev:	cec_device pointer
 */
void unregister_cec_device(struct cec_device *cec_dev)
{
	cec_remove_dev_node(cec_dev);
	device_unregister(&cec_dev->dev);
	memset(&cec_dev->dev, 0, sizeof(cec_dev->dev));
	memset(&cec_dev->cdev, 0, sizeof(cec_dev->cdev));
	cec_device_count--;
}
EXPORT_SYMBOL(unregister_cec_device);

static int __init cec_init(void)
{
	int ret;

	ret = bus_register(&cec_bus_type);
	if (ret) {
		pr_err(PFX "unable to register cec bus type\n");
		return ret;
	}

	ret = cec_dev_init();
	if (ret) {
		pr_err(PFX "failed to create devices\n");
		goto out_bus;
	}

	pr_info(PFX "bus registered\n");

	return 0;

out_bus:
	bus_unregister(&cec_bus_type);
	return ret;
}

static void __exit cec_exit(void)
{
	cec_dev_exit();
	bus_unregister(&cec_bus_type);
	printk(KERN_INFO PFX "bus unregistered\n");
}

module_init(cec_init);
module_exit(cec_exit);

MODULE_AUTHOR("Florian Fainelli <f.fainelli@gmail.com>");
MODULE_DESCRIPTION("HDMI CEC core driver");
MODULE_LICENSE("GPL");
