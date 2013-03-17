/*
 * HDMI CEC character device code
 *
 * Copyright (C), 2011 Florian Fainelli <f.fainelli@gmail.com>
 *
 * This file is subject to the GPLv2 licensing terms
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/sched.h>

#include <linux/hdmi-cec/hdmi-cec.h>
#include <linux/hdmi-cec/dev.h>

static unsigned num_cec_devs;
static int cec_major;
static struct list_head cec_devs_list;
static DEFINE_SPINLOCK(cec_devs_list_lock);
static struct class *cec_class;

static int cec_dev_open(struct inode *i, struct file *f)
{
	struct cdev *cdev = i->i_cdev;
	struct cec_device *cec_dev =
			container_of(cdev, struct cec_device, cdev);
	struct cec_driver *driver = to_cec_driver(cec_dev->dev.driver);

	if (f->private_data)
		return -EBUSY;

	f->private_data = cec_dev;

	return cec_attach_host(driver);
}

static int cec_dev_close(struct inode *i, struct file *f)
{
	struct cdev *cdev = i->i_cdev;
	struct cec_device *cec_dev =
			container_of(cdev, struct cec_device, cdev);
	struct cec_driver *driver = to_cec_driver(cec_dev->dev.driver);
	int ret;

	ret = cec_detach_host(driver);

	cec_flush_queues(driver);

	f->private_data = NULL;

	return ret;
}

static long cec_dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct cec_device *cec_dev;
	struct cec_driver *driver;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int val, ret = -EFAULT;
	struct cec_msg msg;
	struct cec_counters cnt;

	if (!f->private_data)
		return -ENODEV;

	cec_dev = f->private_data;
	driver = to_cec_driver(cec_dev->dev.driver);

	switch (cmd) {
	case CEC_SET_LOGICAL_ADDRESS:
		if (get_user(val, p))
			return -EFAULT;

		ret = cec_set_logical_address(driver, (u8)val);
		break;

	/* cecd compatibility ioctls, should use poll() + read() */
	case CEC_SEND_MESSAGE:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;

		if (!msg.len)
			return -EINVAL;

		ret = cec_send_message(driver, &msg);
		break;

	/* cecd compatibility ioctls, should use poll() + read() */
	case CEC_RECV_MESSAGE:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;

		if (msg.flags & CEC_MSG_NONBLOCK)
			ret = cec_dequeue_message(driver, &msg);
		else
			ret = cec_read_message(driver, &msg);

		if (ret)
			return ret;

		if (copy_to_user(argp, &msg, sizeof(msg)))
			return -EFAULT;

		break;

	case CEC_RESET_DEVICE:
		ret = cec_reset_device(driver);
		break;

	case CEC_GET_COUNTERS:
		memset(&cnt, 0, sizeof(cnt));

		ret = cec_get_counters(driver, &cnt);
		if (ret)
			return ret;

		if (copy_to_user(argp, &cnt, sizeof(cnt)))
			return -EFAULT;

		break;

	case CEC_SET_RX_MODE:
		if (get_user(val, p))
			return -EFAULT;

		ret = cec_set_rx_mode(driver, (enum cec_rx_mode)val);
		break;

	default:
		dev_err(&cec_dev->dev, "unsupported ioctl: %08x\n", cmd);
		break;
	}

	return ret;
}

static int cec_dev_write(struct file *f, const char __user *buf,
			size_t count, loff_t *pos)
{
	struct cec_device *cec_dev = f->private_data;
	struct cec_driver *driver = to_cec_driver(cec_dev->dev.driver);
	struct cec_msg msg;
	int ret;

	memset(&msg, 0, sizeof(msg));

	if (count > CEC_MAX_MSG_LEN || !count)
		return -EINVAL;

	if (copy_from_user(&msg.data, buf, count))
		return -EFAULT;

	msg.len = count;

	ret = cec_send_message(driver, &msg);
	if (ret)
		return ret;

	return count;
}

static int cec_dev_read(struct file *f, char __user *buf,
			size_t count, loff_t *pos)
{
	struct cec_device *cec_dev = f->private_data;
	struct cec_driver *driver = to_cec_driver(cec_dev->dev.driver);
	int ret;
	struct cec_msg msg;

	ret = wait_event_interruptible(driver->rx_wait,
					__cec_rx_queue_len(driver) != 0);
	if (ret)
		return ret;

	ret = cec_dequeue_message(driver, &msg);
	if (ret)
		return ret;

	if (copy_to_user(buf, &msg.data, msg.len))
		return -EFAULT;

	return msg.len;
}

static unsigned int cec_dev_poll(struct file *f, poll_table *wait)
{
	struct cec_device *cec_dev = f->private_data;
	struct cec_driver *driver = to_cec_driver(cec_dev->dev.driver);

	if (__cec_rx_queue_len(driver))
		return POLLIN | POLLRDNORM;

	poll_wait(f, &driver->rx_wait, wait);

	return 0;
}

static const struct file_operations cec_dev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= cec_dev_open,
	.release	= cec_dev_close,
	.unlocked_ioctl	= cec_dev_ioctl,
	.read		= cec_dev_read,
	.write		= cec_dev_write,
	.poll		= cec_dev_poll,
};

int cec_create_dev_node(struct cec_device *cec_dev)
{
	int ret;
	dev_t devno;

	devno = MKDEV(cec_major, num_cec_devs);

	cdev_init(&cec_dev->cdev, &cec_dev_fops);
	cec_dev->cdev.owner = THIS_MODULE;
	cec_dev->minor = num_cec_devs;
	cec_dev->major = cec_major;

	ret = cdev_add(&cec_dev->cdev, devno, 1);
	if (ret) {
		dev_err(&cec_dev->dev, "failed to add char device\n");
		return ret;
	}

	cec_dev->class_dev = device_create(cec_class, NULL, devno,
						cec_dev, cec_dev->name);
	if (IS_ERR(cec_dev->class_dev)) {
		ret = PTR_ERR(cec_dev->class_dev);
		dev_err(&cec_dev->dev, "failed to create device\n");
		goto out_err;
	}

	spin_lock(&cec_devs_list_lock);
	list_add_tail(&cec_dev->list, &cec_devs_list);
	num_cec_devs++;
	spin_unlock(&cec_devs_list_lock);

	return 0;

out_err:
	cdev_del(&cec_dev->cdev);
	return ret;
}

static void cec_remove_one_device(unsigned minor)
{
	struct cec_device *cur, *n;

	list_for_each_entry_safe(cur, n, &cec_devs_list, list) {
		if (cur->minor != minor)
			continue;

		device_del(cur->class_dev);
		cdev_del(&cur->cdev);
		list_del(&cur->list);
	}
}

void cec_remove_dev_node(struct cec_device *cec_dev)
{
	cec_remove_one_device(cec_dev->minor);
}

static void cec_cleanup_devs(void)
{
	unsigned i;

	for (i = 0; i < num_cec_devs; i++)
		cec_remove_one_device(i);
}

int __init cec_dev_init(void)
{
	dev_t dev = 0;
	int ret;

	INIT_LIST_HEAD(&cec_devs_list);

	ret = alloc_chrdev_region(&dev, 0, CEC_MAX_DEVS, "cec");
	if (ret < 0) {
		printk(KERN_ERR "alloc_chrdev_region() failed for cec\n");
		goto out;
	}

	cec_major = MAJOR(dev);

	cec_class = class_create(THIS_MODULE, "cec");
	if (IS_ERR(cec_class)) {
		printk(KERN_ERR "class_create failed\n");
		ret = PTR_ERR(cec_class);
		goto out;
	}

	return 0;

out:
	unregister_chrdev_region(MKDEV(cec_major, 0), CEC_MAX_DEVS);
	return ret;
}

void __exit cec_dev_exit(void)
{
	class_unregister(cec_class);
	cec_cleanup_devs();

	if (cec_major)
		unregister_chrdev_region(MKDEV(cec_major, 0), CEC_MAX_DEVS);
}

