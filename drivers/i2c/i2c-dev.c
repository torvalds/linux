/*
    i2c-dev.c - i2c-bus driver, char device interface

    Copyright (C) 1995-97 Simon G. Vogl
    Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Note that this is a complete rewrite of Simon Vogl's i2c-dev module.
   But I have used so much of his original code and ideas that it seems
   only fair to recognize him as co-author -- Frodo */

/* The I2C_RDWR ioctl code is written by Kolja Waschk <waschk@telos.de> */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <asm/uaccess.h>

static struct i2c_driver i2cdev_driver;

/*
 * An i2c_dev represents an i2c_adapter ... an I2C or SMBus master, not a
 * slave (i2c_client) with which messages will be exchanged.  It's coupled
 * with a character special file which is accessed by user mode drivers.
 *
 * The list of i2c_dev structures is parallel to the i2c_adapter lists
 * maintained by the driver model, and is updated using notifications
 * delivered to the i2cdev_driver.
 */
struct i2c_dev {
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

#define I2C_MINORS	256
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
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
		       adap->nr);
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

static void return_i2c_dev(struct i2c_dev *i2c_dev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	kfree(i2c_dev);
}

static ssize_t show_adapter_name(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_dev *i2c_dev = i2c_dev_get_by_minor(MINOR(dev->devt));

	if (!i2c_dev)
		return -ENODEV;
	return sprintf(buf, "%s\n", i2c_dev->adap->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_adapter_name, NULL);

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

static ssize_t i2cdev_read (struct file *file, char __user *buf, size_t count,
                            loff_t *offset)
{
	char *tmp;
	int ret;

	struct i2c_client *client = (struct i2c_client *)file->private_data;

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count,GFP_KERNEL);
	if (tmp==NULL)
		return -ENOMEM;

	pr_debug("i2c-dev: i2c-%d reading %zd bytes.\n",
		iminor(file->f_path.dentry->d_inode), count);

	ret = i2c_master_recv(client,tmp,count);
	if (ret >= 0)
		ret = copy_to_user(buf,tmp,count)?-EFAULT:ret;
	kfree(tmp);
	return ret;
}

static ssize_t i2cdev_write (struct file *file, const char __user *buf, size_t count,
                             loff_t *offset)
{
	int ret;
	char *tmp;
	struct i2c_client *client = (struct i2c_client *)file->private_data;

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count,GFP_KERNEL);
	if (tmp==NULL)
		return -ENOMEM;
	if (copy_from_user(tmp,buf,count)) {
		kfree(tmp);
		return -EFAULT;
	}

	pr_debug("i2c-dev: i2c-%d writing %zd bytes.\n",
		iminor(file->f_path.dentry->d_inode), count);

	ret = i2c_master_send(client,tmp,count);
	kfree(tmp);
	return ret;
}

/* This address checking function differs from the one in i2c-core
   in that it considers an address with a registered device, but no
   bounded driver, as NOT busy. */
static int i2cdev_check_addr(struct i2c_adapter *adapter, unsigned int addr)
{
	struct list_head *item;
	struct i2c_client *client;
	int res = 0;

	mutex_lock(&adapter->clist_lock);
	list_for_each(item, &adapter->clients) {
		client = list_entry(item, struct i2c_client, list);
		if (client->addr == addr) {
			if (client->driver)
				res = -EBUSY;
			break;
		}
	}
	mutex_unlock(&adapter->clist_lock);

	return res;
}

static int i2cdev_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	struct i2c_rdwr_ioctl_data rdwr_arg;
	struct i2c_smbus_ioctl_data data_arg;
	union i2c_smbus_data temp;
	struct i2c_msg *rdwr_pa;
	u8 __user **data_ptrs;
	int i,datasize,res;
	unsigned long funcs;

	dev_dbg(&client->adapter->dev, "ioctl, cmd=0x%02x, arg=0x%02lx\n",
		cmd, arg);

	switch ( cmd ) {
	case I2C_SLAVE:
	case I2C_SLAVE_FORCE:
		/* NOTE:  devices set up to work with "new style" drivers
		 * can't use I2C_SLAVE, even when the device node is not
		 * bound to a driver.  Only I2C_SLAVE_FORCE will work.
		 *
		 * Setting the PEC flag here won't affect kernel drivers,
		 * which will be using the i2c_client node registered with
		 * the driver model core.  Likewise, when that client has
		 * the PEC flag already set, the i2c-dev driver won't see
		 * (or use) this setting.
		 */
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
		if (arg)
			client->flags |= I2C_CLIENT_PEC;
		else
			client->flags &= ~I2C_CLIENT_PEC;
		return 0;
	case I2C_FUNCS:
		funcs = i2c_get_functionality(client->adapter);
		return put_user(funcs, (unsigned long __user *)arg);

	case I2C_RDWR:
		if (copy_from_user(&rdwr_arg,
				   (struct i2c_rdwr_ioctl_data __user *)arg,
				   sizeof(rdwr_arg)))
			return -EFAULT;

		/* Put an arbitrary limit on the number of messages that can
		 * be sent at once */
		if (rdwr_arg.nmsgs > I2C_RDRW_IOCTL_MAX_MSGS)
			return -EINVAL;

		rdwr_pa = (struct i2c_msg *)
			kmalloc(rdwr_arg.nmsgs * sizeof(struct i2c_msg),
			GFP_KERNEL);

		if (rdwr_pa == NULL) return -ENOMEM;

		if (copy_from_user(rdwr_pa, rdwr_arg.msgs,
				   rdwr_arg.nmsgs * sizeof(struct i2c_msg))) {
			kfree(rdwr_pa);
			return -EFAULT;
		}

		data_ptrs = kmalloc(rdwr_arg.nmsgs * sizeof(u8 __user *), GFP_KERNEL);
		if (data_ptrs == NULL) {
			kfree(rdwr_pa);
			return -ENOMEM;
		}

		res = 0;
		for( i=0; i<rdwr_arg.nmsgs; i++ ) {
			/* Limit the size of the message to a sane amount;
			 * and don't let length change either. */
			if ((rdwr_pa[i].len > 8192) ||
			    (rdwr_pa[i].flags & I2C_M_RECV_LEN)) {
				res = -EINVAL;
				break;
			}
			data_ptrs[i] = (u8 __user *)rdwr_pa[i].buf;
			rdwr_pa[i].buf = kmalloc(rdwr_pa[i].len, GFP_KERNEL);
			if(rdwr_pa[i].buf == NULL) {
				res = -ENOMEM;
				break;
			}
			if(copy_from_user(rdwr_pa[i].buf,
				data_ptrs[i],
				rdwr_pa[i].len)) {
					++i; /* Needs to be kfreed too */
					res = -EFAULT;
				break;
			}
		}
		if (res < 0) {
			int j;
			for (j = 0; j < i; ++j)
				kfree(rdwr_pa[j].buf);
			kfree(data_ptrs);
			kfree(rdwr_pa);
			return res;
		}

		res = i2c_transfer(client->adapter,
			rdwr_pa,
			rdwr_arg.nmsgs);
		while(i-- > 0) {
			if( res>=0 && (rdwr_pa[i].flags & I2C_M_RD)) {
				if(copy_to_user(
					data_ptrs[i],
					rdwr_pa[i].buf,
					rdwr_pa[i].len)) {
					res = -EFAULT;
				}
			}
			kfree(rdwr_pa[i].buf);
		}
		kfree(data_ptrs);
		kfree(rdwr_pa);
		return res;

	case I2C_SMBUS:
		if (copy_from_user(&data_arg,
		                   (struct i2c_smbus_ioctl_data __user *) arg,
		                   sizeof(struct i2c_smbus_ioctl_data)))
			return -EFAULT;
		if ((data_arg.size != I2C_SMBUS_BYTE) &&
		    (data_arg.size != I2C_SMBUS_QUICK) &&
		    (data_arg.size != I2C_SMBUS_BYTE_DATA) &&
		    (data_arg.size != I2C_SMBUS_WORD_DATA) &&
		    (data_arg.size != I2C_SMBUS_PROC_CALL) &&
		    (data_arg.size != I2C_SMBUS_BLOCK_DATA) &&
		    (data_arg.size != I2C_SMBUS_I2C_BLOCK_BROKEN) &&
		    (data_arg.size != I2C_SMBUS_I2C_BLOCK_DATA) &&
		    (data_arg.size != I2C_SMBUS_BLOCK_PROC_CALL)) {
			dev_dbg(&client->adapter->dev,
				"size out of range (%x) in ioctl I2C_SMBUS.\n",
				data_arg.size);
			return -EINVAL;
		}
		/* Note that I2C_SMBUS_READ and I2C_SMBUS_WRITE are 0 and 1,
		   so the check is valid if size==I2C_SMBUS_QUICK too. */
		if ((data_arg.read_write != I2C_SMBUS_READ) &&
		    (data_arg.read_write != I2C_SMBUS_WRITE)) {
			dev_dbg(&client->adapter->dev,
				"read_write out of range (%x) in ioctl I2C_SMBUS.\n",
				data_arg.read_write);
			return -EINVAL;
		}

		/* Note that command values are always valid! */

		if ((data_arg.size == I2C_SMBUS_QUICK) ||
		    ((data_arg.size == I2C_SMBUS_BYTE) &&
		    (data_arg.read_write == I2C_SMBUS_WRITE)))
			/* These are special: we do not use data */
			return i2c_smbus_xfer(client->adapter, client->addr,
					      client->flags,
					      data_arg.read_write,
					      data_arg.command,
					      data_arg.size, NULL);

		if (data_arg.data == NULL) {
			dev_dbg(&client->adapter->dev,
				"data is NULL pointer in ioctl I2C_SMBUS.\n");
			return -EINVAL;
		}

		if ((data_arg.size == I2C_SMBUS_BYTE_DATA) ||
		    (data_arg.size == I2C_SMBUS_BYTE))
			datasize = sizeof(data_arg.data->byte);
		else if ((data_arg.size == I2C_SMBUS_WORD_DATA) ||
		         (data_arg.size == I2C_SMBUS_PROC_CALL))
			datasize = sizeof(data_arg.data->word);
		else /* size == smbus block, i2c block, or block proc. call */
			datasize = sizeof(data_arg.data->block);

		if ((data_arg.size == I2C_SMBUS_PROC_CALL) ||
		    (data_arg.size == I2C_SMBUS_BLOCK_PROC_CALL) ||
		    (data_arg.size == I2C_SMBUS_I2C_BLOCK_DATA) ||
		    (data_arg.read_write == I2C_SMBUS_WRITE)) {
			if (copy_from_user(&temp, data_arg.data, datasize))
				return -EFAULT;
		}
		if (data_arg.size == I2C_SMBUS_I2C_BLOCK_BROKEN) {
			/* Convert old I2C block commands to the new
			   convention. This preserves binary compatibility. */
			data_arg.size = I2C_SMBUS_I2C_BLOCK_DATA;
			if (data_arg.read_write == I2C_SMBUS_READ)
				temp.block[0] = I2C_SMBUS_BLOCK_MAX;
		}
		res = i2c_smbus_xfer(client->adapter,client->addr,client->flags,
		      data_arg.read_write,
		      data_arg.command,data_arg.size,&temp);
		if (! res && ((data_arg.size == I2C_SMBUS_PROC_CALL) ||
		              (data_arg.size == I2C_SMBUS_BLOCK_PROC_CALL) ||
			      (data_arg.read_write == I2C_SMBUS_READ))) {
			if (copy_to_user(data_arg.data, &temp, datasize))
				return -EFAULT;
		}
		return res;
	case I2C_RETRIES:
		client->adapter->retries = arg;
		break;
	case I2C_TIMEOUT:
		client->adapter->timeout = arg;
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

static int i2cdev_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct i2c_dev *i2c_dev;

	i2c_dev = i2c_dev_get_by_minor(minor);
	if (!i2c_dev)
		return -ENODEV;

	adap = i2c_get_adapter(i2c_dev->adap->nr);
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
	client->driver = &i2cdev_driver;

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
	.ioctl		= i2cdev_ioctl,
	.open		= i2cdev_open,
	.release	= i2cdev_release,
};

/* ------------------------------------------------------------------------- */

/*
 * The legacy "i2cdev_driver" is used primarily to get notifications when
 * I2C adapters are added or removed, so that each one gets an i2c_dev
 * and is thus made available to userspace driver code.
 */

static struct class *i2c_dev_class;

static int i2cdev_attach_adapter(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;
	int res;

	i2c_dev = get_free_i2c_dev(adap);
	if (IS_ERR(i2c_dev))
		return PTR_ERR(i2c_dev);

	/* register this i2c device with the driver core */
	i2c_dev->dev = device_create(i2c_dev_class, &adap->dev,
				     MKDEV(I2C_MAJOR, adap->nr),
				     "i2c-%d", adap->nr);
	if (IS_ERR(i2c_dev->dev)) {
		res = PTR_ERR(i2c_dev->dev);
		goto error;
	}
	res = device_create_file(i2c_dev->dev, &dev_attr_name);
	if (res)
		goto error_destroy;

	pr_debug("i2c-dev: adapter [%s] registered as minor %d\n",
		 adap->name, adap->nr);
	return 0;
error_destroy:
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, adap->nr));
error:
	return_i2c_dev(i2c_dev);
	return res;
}

static int i2cdev_detach_adapter(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	i2c_dev = i2c_dev_get_by_minor(adap->nr);
	if (!i2c_dev) /* attach_adapter must have failed */
		return 0;

	device_remove_file(i2c_dev->dev, &dev_attr_name);
	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, adap->nr));

	pr_debug("i2c-dev: adapter [%s] unregistered\n", adap->name);
	return 0;
}

static int i2cdev_detach_client(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver i2cdev_driver = {
	.driver = {
		.name	= "dev_driver",
	},
	.id		= I2C_DRIVERID_I2CDEV,
	.attach_adapter	= i2cdev_attach_adapter,
	.detach_adapter	= i2cdev_detach_adapter,
	.detach_client	= i2cdev_detach_client,
};

/* ------------------------------------------------------------------------- */

/*
 * module load/unload record keeping
 */

static int __init i2c_dev_init(void)
{
	int res;

	printk(KERN_INFO "i2c /dev entries driver\n");

	res = register_chrdev(I2C_MAJOR, "i2c", &i2cdev_fops);
	if (res)
		goto out;

	i2c_dev_class = class_create(THIS_MODULE, "i2c-dev");
	if (IS_ERR(i2c_dev_class))
		goto out_unreg_chrdev;

	res = i2c_add_driver(&i2cdev_driver);
	if (res)
		goto out_unreg_class;

	return 0;

out_unreg_class:
	class_destroy(i2c_dev_class);
out_unreg_chrdev:
	unregister_chrdev(I2C_MAJOR, "i2c");
out:
	printk(KERN_ERR "%s: Driver Initialisation failed\n", __FILE__);
	return res;
}

static void __exit i2c_dev_exit(void)
{
	i2c_del_driver(&i2cdev_driver);
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"i2c");
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
		"Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C /dev entries driver");
MODULE_LICENSE("GPL");

module_init(i2c_dev_init);
module_exit(i2c_dev_exit);
