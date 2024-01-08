// SPDX-License-Identifier: GPL-2.0-or-later
/*
    i2c-dev.c - i2c-bus driver, char device interface

    Copyright (C) 1995-97 Simon G. Vogl
    Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>

*/

/* Note that this is a complete rewrite of Simon Vogl's i2c-dev module.
   But I have used so much of his original code and ideas that it seems
   only fair to recognize him as co-author -- Frodo */

/* The I2C_RDWR ioctl code is written by Kolja Waschk <waschk@telos.de> */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/*
 * An i2c_dev represents an i2c_adapter ... an I2C or SMBus master, not a
 * slave (i2c_client) with which messages will be exchanged.  It's coupled
 * with a character special file which is accessed by user mode drivers.
 *
 * The list of i2c_dev structures is parallel to the i2c_adapter lists
 * maintained by the driver model, and is updated using bus notifications.
 */
struct i2c_dev {
	struct list_head list;
	struct i2c_adapter *adap;
	struct device dev;
	struct cdev cdev;
};

#define I2C_MINORS	(MINORMASK + 1)
static LIST_HEAD(i2c_dev_list);
static DEFINE_SPINLOCK(i2c_dev_list_lock);

static struct i2c_dev *i2c_dev_get_by_minor(unsigned index)
{
	struct i2c_dev *i2c_dev;

	spin_lock(&i2c_dev_list_lock);
	list_for_each_entry(i2c_dev, &i2c_dev_list, list) {
		if (i2c_dev->adap->nr == index)
			goto found;
	}
	i2c_dev = NULL;
found:
	spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}

static struct i2c_dev *get_free_i2c_dev(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	if (adap->nr >= I2C_MINORS) {
		pr_err("Out of device minors (%d)\n", adap->nr);
		return ERR_PTR(-ENODEV);
	}

	i2c_dev = kzalloc(sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return ERR_PTR(-ENOMEM);
	i2c_dev->adap = adap;

	spin_lock(&i2c_dev_list_lock);
	list_add_tail(&i2c_dev->list, &i2c_dev_list);
	spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}

static void put_i2c_dev(struct i2c_dev *i2c_dev, bool del_cdev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	if (del_cdev)
		cdev_device_del(&i2c_dev->cdev, &i2c_dev->dev);
	put_device(&i2c_dev->dev);
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct i2c_dev *i2c_dev = i2c_dev_get_by_minor(MINOR(dev->devt));

	if (!i2c_dev)
		return -ENODEV;
	return sysfs_emit(buf, "%s\n", i2c_dev->adap->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *i2c_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(i2c);

/* ------------------------------------------------------------------------- */

/*
 * After opening an instance of this character special file, a file
 * descriptor starts out associated only with an i2c_adapter (and bus).
 *
 * Using the I2C_RDWR ioctl(), you can then *immediately* issue i2c_msg
 * traffic to any devices on the bus used by that adapter.  That's because
 * the i2c_msg vectors embed all the addressing information they need, and
 * are submitted directly to an i2c_adapter.  However, SMBus-only adapters
 * don't support that interface.
 *
 * To use read()/write() system calls on that file descriptor, or to use
 * SMBus interfaces (and work with SMBus-only hosts!), you must first issue
 * an I2C_SLAVE (or I2C_SLAVE_FORCE) ioctl.  That configures an anonymous
 * (never registered) i2c_client so it holds the addressing information
 * needed by those system calls and by this SMBus interface.
 */

static ssize_t i2cdev_read(struct file *file, char __user *buf, size_t count,
		loff_t *offset)
{
	char *tmp;
	int ret;

	struct i2c_client *client = file->private_data;

	if (count > 8192)
		count = 8192;

	tmp = kzalloc(count, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	pr_debug("i2c-%d reading %zu bytes.\n", iminor(file_inode(file)), count);

	ret = i2c_master_recv(client, tmp, count);
	if (ret >= 0)
		if (copy_to_user(buf, tmp, ret))
			ret = -EFAULT;
	kfree(tmp);
	return ret;
}

static ssize_t i2cdev_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
	int ret;
	char *tmp;
	struct i2c_client *client = file->private_data;

	if (count > 8192)
		count = 8192;

	tmp = memdup_user(buf, count);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	pr_debug("i2c-%d writing %zu bytes.\n", iminor(file_inode(file)), count);

	ret = i2c_master_send(client, tmp, count);
	kfree(tmp);
	return ret;
}

static int i2cdev_check(struct device *dev, void *addrp)
{
	struct i2c_client *client = i2c_verify_client(dev);

	if (!client || client->addr != *(unsigned int *)addrp)
		return 0;

	return dev->driver ? -EBUSY : 0;
}

/* walk up mux tree */
static int i2cdev_check_mux_parents(struct i2c_adapter *adapter, int addr)
{
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(adapter);
	int result;

	result = device_for_each_child(&adapter->dev, &addr, i2cdev_check);
	if (!result && parent)
		result = i2cdev_check_mux_parents(parent, addr);

	return result;
}

/* recurse down mux tree */
static int i2cdev_check_mux_children(struct device *dev, void *addrp)
{
	int result;

	if (dev->type == &i2c_adapter_type)
		result = device_for_each_child(dev, addrp,
						i2cdev_check_mux_children);
	else
		result = i2cdev_check(dev, addrp);

	return result;
}

/* This address checking function differs from the one in i2c-core
   in that it considers an address with a registered device, but no
   driver bound to it, as NOT busy. */
static int i2cdev_check_addr(struct i2c_adapter *adapter, unsigned int addr)
{
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(adapter);
	int result = 0;

	if (parent)
		result = i2cdev_check_mux_parents(parent, addr);

	if (!result)
		result = device_for_each_child(&adapter->dev, &addr,
						i2cdev_check_mux_children);

	return result;
}

static noinline int i2cdev_ioctl_rdwr(struct i2c_client *client,
		unsigned nmsgs, struct i2c_msg *msgs)
{
	u8 __user **data_ptrs;
	int i, res;

	data_ptrs = kmalloc_array(nmsgs, sizeof(u8 __user *), GFP_KERNEL);
	if (data_ptrs == NULL) {
		kfree(msgs);
		return -ENOMEM;
	}

	res = 0;
	for (i = 0; i < nmsgs; i++) {
		/* Limit the size of the message to a sane amount */
		if (msgs[i].len > 8192) {
			res = -EINVAL;
			break;
		}

		data_ptrs[i] = (u8 __user *)msgs[i].buf;
		msgs[i].buf = memdup_user(data_ptrs[i], msgs[i].len);
		if (IS_ERR(msgs[i].buf)) {
			res = PTR_ERR(msgs[i].buf);
			break;
		}
		/* memdup_user allocates with GFP_KERNEL, so DMA is ok */
		msgs[i].flags |= I2C_M_DMA_SAFE;

		/*
		 * If the message length is received from the slave (similar
		 * to SMBus block read), we must ensure that the buffer will
		 * be large enough to cope with a message length of
		 * I2C_SMBUS_BLOCK_MAX as this is the maximum underlying bus
		 * drivers allow. The first byte in the buffer must be
		 * pre-filled with the number of extra bytes, which must be
		 * at least one to hold the message length, but can be
		 * greater (for example to account for a checksum byte at
		 * the end of the message.)
		 */
		if (msgs[i].flags & I2C_M_RECV_LEN) {
			if (!(msgs[i].flags & I2C_M_RD) ||
			    msgs[i].len < 1 || msgs[i].buf[0] < 1 ||
			    msgs[i].len < msgs[i].buf[0] +
					     I2C_SMBUS_BLOCK_MAX) {
				i++;
				res = -EINVAL;
				break;
			}

			msgs[i].len = msgs[i].buf[0];
		}
	}
	if (res < 0) {
		int j;
		for (j = 0; j < i; ++j)
			kfree(msgs[j].buf);
		kfree(data_ptrs);
		kfree(msgs);
		return res;
	}

	res = i2c_transfer(client->adapter, msgs, nmsgs);
	while (i-- > 0) {
		if (res >= 0 && (msgs[i].flags & I2C_M_RD)) {
			if (copy_to_user(data_ptrs[i], msgs[i].buf,
					 msgs[i].len))
				res = -EFAULT;
		}
		kfree(msgs[i].buf);
	}
	kfree(data_ptrs);
	kfree(msgs);
	return res;
}

static noinline int i2cdev_ioctl_smbus(struct i2c_client *client,
		u8 read_write, u8 command, u32 size,
		union i2c_smbus_data __user *data)
{
	union i2c_smbus_data temp = {};
	int datasize, res;

	if ((size != I2C_SMBUS_BYTE) &&
	    (size != I2C_SMBUS_QUICK) &&
	    (size != I2C_SMBUS_BYTE_DATA) &&
	    (size != I2C_SMBUS_WORD_DATA) &&
	    (size != I2C_SMBUS_PROC_CALL) &&
	    (size != I2C_SMBUS_BLOCK_DATA) &&
	    (size != I2C_SMBUS_I2C_BLOCK_BROKEN) &&
	    (size != I2C_SMBUS_I2C_BLOCK_DATA) &&
	    (size != I2C_SMBUS_BLOCK_PROC_CALL)) {
		dev_dbg(&client->adapter->dev,
			"size out of range (%x) in ioctl I2C_SMBUS.\n",
			size);
		return -EINVAL;
	}
	/* Note that I2C_SMBUS_READ and I2C_SMBUS_WRITE are 0 and 1,
	   so the check is valid if size==I2C_SMBUS_QUICK too. */
	if ((read_write != I2C_SMBUS_READ) &&
	    (read_write != I2C_SMBUS_WRITE)) {
		dev_dbg(&client->adapter->dev,
			"read_write out of range (%x) in ioctl I2C_SMBUS.\n",
			read_write);
		return -EINVAL;
	}

	/* Note that command values are always valid! */

	if ((size == I2C_SMBUS_QUICK) ||
	    ((size == I2C_SMBUS_BYTE) &&
	    (read_write == I2C_SMBUS_WRITE)))
		/* These are special: we do not use data */
		return i2c_smbus_xfer(client->adapter, client->addr,
				      client->flags, read_write,
				      command, size, NULL);

	if (data == NULL) {
		dev_dbg(&client->adapter->dev,
			"data is NULL pointer in ioctl I2C_SMBUS.\n");
		return -EINVAL;
	}

	if ((size == I2C_SMBUS_BYTE_DATA) ||
	    (size == I2C_SMBUS_BYTE))
		datasize = sizeof(data->byte);
	else if ((size == I2C_SMBUS_WORD_DATA) ||
		 (size == I2C_SMBUS_PROC_CALL))
		datasize = sizeof(data->word);
	else /* size == smbus block, i2c block, or block proc. call */
		datasize = sizeof(data->block);

	if ((size == I2C_SMBUS_PROC_CALL) ||
	    (size == I2C_SMBUS_BLOCK_PROC_CALL) ||
	    (size == I2C_SMBUS_I2C_BLOCK_DATA) ||
	    (read_write == I2C_SMBUS_WRITE)) {
		if (copy_from_user(&temp, data, datasize))
			return -EFAULT;
	}
	if (size == I2C_SMBUS_I2C_BLOCK_BROKEN) {
		/* Convert old I2C block commands to the new
		   convention. This preserves binary compatibility. */
		size = I2C_SMBUS_I2C_BLOCK_DATA;
		if (read_write == I2C_SMBUS_READ)
			temp.block[0] = I2C_SMBUS_BLOCK_MAX;
	}
	res = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
	      read_write, command, size, &temp);
	if (!res && ((size == I2C_SMBUS_PROC_CALL) ||
		     (size == I2C_SMBUS_BLOCK_PROC_CALL) ||
		     (read_write == I2C_SMBUS_READ))) {
		if (copy_to_user(data, &temp, datasize))
			return -EFAULT;
	}
	return res;
}

static long i2cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = file->private_data;
	unsigned long funcs;

	dev_dbg(&client->adapter->dev, "ioctl, cmd=0x%02x, arg=0x%02lx\n",
		cmd, arg);

	switch (cmd) {
	case I2C_SLAVE:
	case I2C_SLAVE_FORCE:
		if ((arg > 0x3ff) ||
		    (((client->flags & I2C_M_TEN) == 0) && arg > 0x7f))
			return -EINVAL;
		if (cmd == I2C_SLAVE && i2cdev_check_addr(client->adapter, arg))
			return -EBUSY;
		/* REVISIT: address could become busy later */
		client->addr = arg;
		return 0;
	case I2C_TENBIT:
		if (arg)
			client->flags |= I2C_M_TEN;
		else
			client->flags &= ~I2C_M_TEN;
		return 0;
	case I2C_PEC:
		/*
		 * Setting the PEC flag here won't affect kernel drivers,
		 * which will be using the i2c_client node registered with
		 * the driver model core.  Likewise, when that client has
		 * the PEC flag already set, the i2c-dev driver won't see
		 * (or use) this setting.
		 */
		if (arg)
			client->flags |= I2C_CLIENT_PEC;
		else
			client->flags &= ~I2C_CLIENT_PEC;
		return 0;
	case I2C_FUNCS:
		funcs = i2c_get_functionality(client->adapter);
		return put_user(funcs, (unsigned long __user *)arg);

	case I2C_RDWR: {
		struct i2c_rdwr_ioctl_data rdwr_arg;
		struct i2c_msg *rdwr_pa;

		if (copy_from_user(&rdwr_arg,
				   (struct i2c_rdwr_ioctl_data __user *)arg,
				   sizeof(rdwr_arg)))
			return -EFAULT;

		if (!rdwr_arg.msgs || rdwr_arg.nmsgs == 0)
			return -EINVAL;

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (rdwr_arg.nmsgs > I2C_RDWR_IOCTL_MAX_MSGS)
			return -EINVAL;

		rdwr_pa = memdup_array_user(rdwr_arg.msgs,
					    rdwr_arg.nmsgs, sizeof(struct i2c_msg));
		if (IS_ERR(rdwr_pa))
			return PTR_ERR(rdwr_pa);

		return i2cdev_ioctl_rdwr(client, rdwr_arg.nmsgs, rdwr_pa);
	}

	case I2C_SMBUS: {
		struct i2c_smbus_ioctl_data data_arg;
		if (copy_from_user(&data_arg,
				   (struct i2c_smbus_ioctl_data __user *) arg,
				   sizeof(struct i2c_smbus_ioctl_data)))
			return -EFAULT;
		return i2cdev_ioctl_smbus(client, data_arg.read_write,
					  data_arg.command,
					  data_arg.size,
					  data_arg.data);
	}
	case I2C_RETRIES:
		if (arg > INT_MAX)
			return -EINVAL;

		client->adapter->retries = arg;
		break;
	case I2C_TIMEOUT:
		if (arg > INT_MAX)
			return -EINVAL;

		/* For historical reasons, user-space sets the timeout
		 * value in units of 10 ms.
		 */
		client->adapter->timeout = msecs_to_jiffies(arg * 10);
		break;
	default:
		/* NOTE:  returning a fault code here could cause trouble
		 * in buggy userspace code.  Some old kernel bugs returned
		 * zero in this case, and userspace code might accidentally
		 * have depended on that bug.
		 */
		return -ENOTTY;
	}
	return 0;
}

#ifdef CONFIG_COMPAT

struct i2c_smbus_ioctl_data32 {
	u8 read_write;
	u8 command;
	u32 size;
	compat_caddr_t data; /* union i2c_smbus_data *data */
};

struct i2c_msg32 {
	u16 addr;
	u16 flags;
	u16 len;
	compat_caddr_t buf;
};

struct i2c_rdwr_ioctl_data32 {
	compat_caddr_t msgs; /* struct i2c_msg __user *msgs */
	u32 nmsgs;
};

static long compat_i2cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = file->private_data;
	unsigned long funcs;
	switch (cmd) {
	case I2C_FUNCS:
		funcs = i2c_get_functionality(client->adapter);
		return put_user(funcs, (compat_ulong_t __user *)arg);
	case I2C_RDWR: {
		struct i2c_rdwr_ioctl_data32 rdwr_arg;
		struct i2c_msg32 __user *p;
		struct i2c_msg *rdwr_pa;
		int i;

		if (copy_from_user(&rdwr_arg,
				   (struct i2c_rdwr_ioctl_data32 __user *)arg,
				   sizeof(rdwr_arg)))
			return -EFAULT;

		if (!rdwr_arg.msgs || rdwr_arg.nmsgs == 0)
			return -EINVAL;

		if (rdwr_arg.nmsgs > I2C_RDWR_IOCTL_MAX_MSGS)
			return -EINVAL;

		rdwr_pa = kmalloc_array(rdwr_arg.nmsgs, sizeof(struct i2c_msg),
				      GFP_KERNEL);
		if (!rdwr_pa)
			return -ENOMEM;

		p = compat_ptr(rdwr_arg.msgs);
		for (i = 0; i < rdwr_arg.nmsgs; i++) {
			struct i2c_msg32 umsg;
			if (copy_from_user(&umsg, p + i, sizeof(umsg))) {
				kfree(rdwr_pa);
				return -EFAULT;
			}
			rdwr_pa[i] = (struct i2c_msg) {
				.addr = umsg.addr,
				.flags = umsg.flags,
				.len = umsg.len,
				.buf = (__force __u8 *)compat_ptr(umsg.buf),
			};
		}

		return i2cdev_ioctl_rdwr(client, rdwr_arg.nmsgs, rdwr_pa);
	}
	case I2C_SMBUS: {
		struct i2c_smbus_ioctl_data32	data32;
		if (copy_from_user(&data32,
				   (void __user *) arg,
				   sizeof(data32)))
			return -EFAULT;
		return i2cdev_ioctl_smbus(client, data32.read_write,
					  data32.command,
					  data32.size,
					  compat_ptr(data32.data));
	}
	default:
		return i2cdev_ioctl(file, cmd, arg);
	}
}
#else
#define compat_i2cdev_ioctl NULL
#endif

static int i2cdev_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct i2c_client *client;
	struct i2c_adapter *adap;

	adap = i2c_get_adapter(minor);
	if (!adap)
		return -ENODEV;

	/* This creates an anonymous i2c_client, which may later be
	 * pointed to some address using I2C_SLAVE or I2C_SLAVE_FORCE.
	 *
	 * This client is ** NEVER REGISTERED ** with the driver model
	 * or I2C core code!!  It just holds private copies of addressing
	 * information and maybe a PEC flag.
	 */
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		i2c_put_adapter(adap);
		return -ENOMEM;
	}
	snprintf(client->name, I2C_NAME_SIZE, "i2c-dev %d", adap->nr);

	client->adapter = adap;
	file->private_data = client;

	return 0;
}

static int i2cdev_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;

	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations i2cdev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= i2cdev_read,
	.write		= i2cdev_write,
	.unlocked_ioctl	= i2cdev_ioctl,
	.compat_ioctl	= compat_i2cdev_ioctl,
	.open		= i2cdev_open,
	.release	= i2cdev_release,
};

/* ------------------------------------------------------------------------- */

static const struct class i2c_dev_class = {
	.name = "i2c-dev",
	.dev_groups = i2c_groups,
};

static void i2cdev_dev_release(struct device *dev)
{
	struct i2c_dev *i2c_dev;

	i2c_dev = container_of(dev, struct i2c_dev, dev);
	kfree(i2c_dev);
}

static int i2cdev_attach_adapter(struct device *dev)
{
	struct i2c_adapter *adap;
	struct i2c_dev *i2c_dev;
	int res;

	if (dev->type != &i2c_adapter_type)
		return NOTIFY_DONE;
	adap = to_i2c_adapter(dev);

	i2c_dev = get_free_i2c_dev(adap);
	if (IS_ERR(i2c_dev))
		return NOTIFY_DONE;

	cdev_init(&i2c_dev->cdev, &i2cdev_fops);
	i2c_dev->cdev.owner = THIS_MODULE;

	device_initialize(&i2c_dev->dev);
	i2c_dev->dev.devt = MKDEV(I2C_MAJOR, adap->nr);
	i2c_dev->dev.class = &i2c_dev_class;
	i2c_dev->dev.parent = &adap->dev;
	i2c_dev->dev.release = i2cdev_dev_release;

	res = dev_set_name(&i2c_dev->dev, "i2c-%d", adap->nr);
	if (res)
		goto err_put_i2c_dev;

	res = cdev_device_add(&i2c_dev->cdev, &i2c_dev->dev);
	if (res)
		goto err_put_i2c_dev;

	pr_debug("adapter [%s] registered as minor %d\n", adap->name, adap->nr);
	return NOTIFY_OK;

err_put_i2c_dev:
	put_i2c_dev(i2c_dev, false);
	return NOTIFY_DONE;
}

static int i2cdev_detach_adapter(struct device *dev)
{
	struct i2c_adapter *adap;
	struct i2c_dev *i2c_dev;

	if (dev->type != &i2c_adapter_type)
		return NOTIFY_DONE;
	adap = to_i2c_adapter(dev);

	i2c_dev = i2c_dev_get_by_minor(adap->nr);
	if (!i2c_dev) /* attach_adapter must have failed */
		return NOTIFY_DONE;

	put_i2c_dev(i2c_dev, true);

	pr_debug("adapter [%s] unregistered\n", adap->name);
	return NOTIFY_OK;
}

static int i2cdev_notifier_call(struct notifier_block *nb, unsigned long action,
			 void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		return i2cdev_attach_adapter(dev);
	case BUS_NOTIFY_DEL_DEVICE:
		return i2cdev_detach_adapter(dev);
	}

	return NOTIFY_DONE;
}

static struct notifier_block i2cdev_notifier = {
	.notifier_call = i2cdev_notifier_call,
};

/* ------------------------------------------------------------------------- */

static int __init i2c_dev_attach_adapter(struct device *dev, void *dummy)
{
	i2cdev_attach_adapter(dev);
	return 0;
}

static int __exit i2c_dev_detach_adapter(struct device *dev, void *dummy)
{
	i2cdev_detach_adapter(dev);
	return 0;
}

/*
 * module load/unload record keeping
 */

static int __init i2c_dev_init(void)
{
	int res;

	pr_info("i2c /dev entries driver\n");

	res = register_chrdev_region(MKDEV(I2C_MAJOR, 0), I2C_MINORS, "i2c");
	if (res)
		goto out;

	res = class_register(&i2c_dev_class);
	if (res)
		goto out_unreg_chrdev;

	/* Keep track of adapters which will be added or removed later */
	res = bus_register_notifier(&i2c_bus_type, &i2cdev_notifier);
	if (res)
		goto out_unreg_class;

	/* Bind to already existing adapters right away */
	i2c_for_each_dev(NULL, i2c_dev_attach_adapter);

	return 0;

out_unreg_class:
	class_unregister(&i2c_dev_class);
out_unreg_chrdev:
	unregister_chrdev_region(MKDEV(I2C_MAJOR, 0), I2C_MINORS);
out:
	pr_err("Driver Initialisation failed\n");
	return res;
}

static void __exit i2c_dev_exit(void)
{
	bus_unregister_notifier(&i2c_bus_type, &i2cdev_notifier);
	i2c_for_each_dev(NULL, i2c_dev_detach_adapter);
	class_unregister(&i2c_dev_class);
	unregister_chrdev_region(MKDEV(I2C_MAJOR, 0), I2C_MINORS);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C /dev entries driver");
MODULE_LICENSE("GPL");

module_init(i2c_dev_init);
module_exit(i2c_dev_exit);
