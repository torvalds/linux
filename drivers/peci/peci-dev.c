// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/peci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/*
 * A peci_dev represents an peci_adapter ... an PECI or SMBus master, not a
 * slave (peci_client) with which messages will be exchanged.  It's coupled
 * with a character special file which is accessed by user mode drivers.
 *
 * The list of peci_dev structures is parallel to the peci_adapter lists
 * maintained by the driver model, and is updated using bus notifications.
 */
struct peci_dev {
	struct list_head	list;
	struct peci_adapter	*adapter;
	struct device		*dev;
	struct cdev		cdev;
};

#define PECI_MINORS		MINORMASK

static dev_t peci_devt;
static LIST_HEAD(peci_dev_list);
static DEFINE_SPINLOCK(peci_dev_list_lock);

static struct peci_dev *peci_dev_get_by_minor(uint index)
{
	struct peci_dev *peci_dev;

	spin_lock(&peci_dev_list_lock);
	list_for_each_entry(peci_dev, &peci_dev_list, list) {
		if (peci_dev->adapter->nr == index)
			goto found;
	}
	peci_dev = NULL;
found:
	spin_unlock(&peci_dev_list_lock);

	return peci_dev;
}

static struct peci_dev *peci_dev_alloc(struct peci_adapter *adapter)
{
	struct peci_dev *peci_dev;

	if (adapter->nr >= PECI_MINORS) {
		dev_err(&adapter->dev, "Out of device minors (%d)\n",
			adapter->nr);
		return ERR_PTR(-ENODEV);
	}

	peci_dev = kzalloc(sizeof(*peci_dev), GFP_KERNEL);
	if (!peci_dev)
		return ERR_PTR(-ENOMEM);
	peci_dev->adapter = adapter;

	spin_lock(&peci_dev_list_lock);
	list_add_tail(&peci_dev->list, &peci_dev_list);
	spin_unlock(&peci_dev_list_lock);

	return peci_dev;
}

static void peci_dev_put(struct peci_dev *peci_dev)
{
	spin_lock(&peci_dev_list_lock);
	list_del(&peci_dev->list);
	spin_unlock(&peci_dev_list_lock);
	kfree(peci_dev);
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct peci_dev *peci_dev = peci_dev_get_by_minor(MINOR(dev->devt));

	if (!peci_dev)
		return -ENODEV;

	return sprintf(buf, "%s\n", peci_dev->adapter->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *peci_dev_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(peci_dev);

static long peci_dev_ioctl(struct file *file, uint iocmd, ulong arg)
{
	struct peci_dev *peci_dev = file->private_data;
	void __user *umsg = (void __user *)arg;
	struct peci_xfer_msg *xmsg = NULL;
	struct peci_xfer_msg uxmsg;
	enum peci_cmd cmd;
	u8 *msg = NULL;
	uint msg_len;
	int ret;

	cmd = _IOC_NR(iocmd);
	msg_len = _IOC_SIZE(iocmd);

	switch (cmd) {
	case PECI_CMD_XFER:
		if (msg_len != sizeof(struct peci_xfer_msg)) {
			ret = -EFAULT;
			break;
		}

		if (copy_from_user(&uxmsg, umsg, msg_len)) {
			ret = -EFAULT;
			break;
		}

		xmsg = peci_get_xfer_msg(uxmsg.tx_len, uxmsg.rx_len);
		if (!xmsg) {
			ret = -ENOMEM;
			break;
		}

		if (uxmsg.tx_len &&
		    copy_from_user(xmsg->tx_buf, (__u8 __user *)uxmsg.tx_buf,
				   uxmsg.tx_len)) {
			ret = -EFAULT;
			break;
		}

		xmsg->addr = uxmsg.addr;
		xmsg->tx_len = uxmsg.tx_len;
		xmsg->rx_len = uxmsg.rx_len;

		/*
		 * Send the command and copy the results back to user space on
		 * either success or timeout to provide the completion code to
		 * the caller.
		 */
		ret = peci_command(peci_dev->adapter, cmd, xmsg);
		if ((!ret || ret == -ETIMEDOUT) && xmsg->rx_len &&
		    copy_to_user((__u8 __user *)uxmsg.rx_buf, xmsg->rx_buf,
				 xmsg->rx_len))
			ret = -EFAULT;

		break;

	default:
		msg = memdup_user(umsg, msg_len);
		if (IS_ERR(msg)) {
			ret = PTR_ERR(msg);
			break;
		}

		/*
		 * Send the command and copy the results back to user space on
		 * either success or timeout to provide the completion code to
		 * the caller.
		 */
		ret = peci_command(peci_dev->adapter, cmd, msg);
		if ((!ret || ret == -ETIMEDOUT) &&
		    copy_to_user(umsg, msg, msg_len))
			ret = -EFAULT;

		break;
	}

	peci_put_xfer_msg(xmsg);
	if (!IS_ERR(msg))
		kfree(msg);

	return (long)ret;
}

static int peci_dev_open(struct inode *inode, struct file *file)
{
	struct peci_adapter *adapter;
	struct peci_dev *peci_dev;

	peci_dev = peci_dev_get_by_minor(iminor(inode));
	if (!peci_dev)
		return -ENODEV;

	adapter = peci_get_adapter(peci_dev->adapter->nr);
	if (!adapter)
		return -ENODEV;

	file->private_data = peci_dev;

	return 0;
}

static int peci_dev_release(struct inode *inode, struct file *file)
{
	struct peci_dev *peci_dev = file->private_data;

	peci_put_adapter(peci_dev->adapter);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations peci_dev_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= peci_dev_ioctl,
	.open		= peci_dev_open,
	.release	= peci_dev_release,
	.llseek		= no_llseek,
};

static struct class *peci_dev_class;

static int peci_dev_attach_adapter(struct device *dev, void *dummy)
{
	struct peci_adapter *adapter;
	struct peci_dev *peci_dev;
	dev_t devt;
	int ret;

	if (dev->type != &peci_adapter_type)
		return 0;

	adapter = to_peci_adapter(dev);
	peci_dev = peci_dev_alloc(adapter);
	if (IS_ERR(peci_dev))
		return PTR_ERR(peci_dev);

	cdev_init(&peci_dev->cdev, &peci_dev_fops);
	peci_dev->cdev.owner = THIS_MODULE;
	devt = MKDEV(MAJOR(peci_devt), adapter->nr);

	ret = cdev_add(&peci_dev->cdev, devt, 1);
	if (ret)
		goto err_put_dev;

	/* register this peci device with the driver core */
	peci_dev->dev = device_create(peci_dev_class, &adapter->dev, devt, NULL,
				      "peci-%d", adapter->nr);
	if (IS_ERR(peci_dev->dev)) {
		ret = PTR_ERR(peci_dev->dev);
		goto err_del_cdev;
	}

	dev_info(dev, "cdev of adapter [%s] registered as minor %d\n",
		 adapter->name, adapter->nr);

	return 0;

err_del_cdev:
	cdev_del(&peci_dev->cdev);
err_put_dev:
	peci_dev_put(peci_dev);

	return ret;
}

static int peci_dev_detach_adapter(struct device *dev, void *dummy)
{
	struct peci_adapter *adapter;
	struct peci_dev *peci_dev;
	dev_t devt;

	if (dev->type != &peci_adapter_type)
		return 0;

	adapter = to_peci_adapter(dev);
	peci_dev = peci_dev_get_by_minor(adapter->nr);
	if (!peci_dev)
		return 0;

	cdev_del(&peci_dev->cdev);
	devt = peci_dev->dev->devt;
	peci_dev_put(peci_dev);
	device_destroy(peci_dev_class, devt);

	dev_info(dev, "cdev of adapter [%s] unregistered\n", adapter->name);

	return 0;
}

static int peci_dev_notifier_call(struct notifier_block *nb, ulong action,
				  void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		return peci_dev_attach_adapter(dev, NULL);
	case BUS_NOTIFY_DEL_DEVICE:
		return peci_dev_detach_adapter(dev, NULL);
	}

	return 0;
}

static struct notifier_block peci_dev_notifier = {
	.notifier_call = peci_dev_notifier_call,
};

static int __init peci_dev_init(void)
{
	int ret;

	pr_debug("peci /dev entries driver\n");

	ret = alloc_chrdev_region(&peci_devt, 0, PECI_MINORS, "peci");
	if (ret < 0) {
		pr_err("peci: Failed to allocate chr dev region!\n");
		bus_unregister(&peci_bus_type);
		goto err;
	}

	peci_dev_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(peci_dev_class)) {
		ret = PTR_ERR(peci_dev_class);
		goto err_unreg_chrdev;
	}
	peci_dev_class->dev_groups = peci_dev_groups;

	/* Keep track of adapters which will be added or removed later */
	ret = bus_register_notifier(&peci_bus_type, &peci_dev_notifier);
	if (ret)
		goto err_destroy_class;

	/* Bind to already existing adapters right away */
	peci_for_each_dev(NULL, peci_dev_attach_adapter);

	return 0;

err_destroy_class:
	class_destroy(peci_dev_class);
err_unreg_chrdev:
	unregister_chrdev_region(peci_devt, PECI_MINORS);
err:
	pr_err("%s: Driver Initialization failed\n", __FILE__);

	return ret;
}

static void __exit peci_dev_exit(void)
{
	bus_unregister_notifier(&peci_bus_type, &peci_dev_notifier);
	peci_for_each_dev(NULL, peci_dev_detach_adapter);
	class_destroy(peci_dev_class);
	unregister_chrdev_region(peci_devt, PECI_MINORS);
}

module_init(peci_dev_init);
module_exit(peci_dev_exit);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("PECI /dev entries driver");
MODULE_LICENSE("GPL v2");
