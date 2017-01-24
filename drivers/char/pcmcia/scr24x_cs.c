/*
 * SCR24x PCMCIA Smart Card Reader Driver
 *
 * Copyright (C) 2005-2006 TL Sudheendran
 * Copyright (C) 2016 Lubomir Rintel
 *
 * Derived from "scr24x_v4.2.6_Release.tar.gz" driver by TL Sudheendran.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#define CCID_HEADER_SIZE	10
#define CCID_LENGTH_OFFSET	1
#define CCID_MAX_LEN		271

#define SCR24X_DATA(n)		(1 + n)
#define SCR24X_CMD_STATUS	7
#define CMD_START		0x40
#define CMD_WRITE_BYTE		0x41
#define CMD_READ_BYTE		0x42
#define STATUS_BUSY		0x80

struct scr24x_dev {
	struct device *dev;
	struct cdev c_dev;
	unsigned char buf[CCID_MAX_LEN];
	int devno;
	struct mutex lock;
	struct kref refcnt;
	u8 __iomem *regs;
};

#define SCR24X_DEVS 8
static DECLARE_BITMAP(scr24x_minors, SCR24X_DEVS);

static struct class *scr24x_class;
static dev_t scr24x_devt;

static void scr24x_delete(struct kref *kref)
{
	struct scr24x_dev *dev = container_of(kref, struct scr24x_dev,
								refcnt);

	kfree(dev);
}

static int scr24x_wait_ready(struct scr24x_dev *dev)
{
	u_char status;
	int timeout = 100;

	do {
		status = ioread8(dev->regs + SCR24X_CMD_STATUS);
		if (!(status & STATUS_BUSY))
			return 0;

		msleep(20);
	} while (--timeout);

	return -EIO;
}

static int scr24x_open(struct inode *inode, struct file *filp)
{
	struct scr24x_dev *dev = container_of(inode->i_cdev,
				struct scr24x_dev, c_dev);

	kref_get(&dev->refcnt);
	filp->private_data = dev;

	return nonseekable_open(inode, filp);
}

static int scr24x_release(struct inode *inode, struct file *filp)
{
	struct scr24x_dev *dev = filp->private_data;

	/* We must not take the dev->lock here as scr24x_delete()
	 * might be called to remove the dev structure altogether.
	 * We don't need the lock anyway, since after the reference
	 * acquired in probe() is released in remove() the chrdev
	 * is already unregistered and noone can possibly acquire
	 * a reference via open() anymore. */
	kref_put(&dev->refcnt, scr24x_delete);
	return 0;
}

static int read_chunk(struct scr24x_dev *dev, size_t offset, size_t limit)
{
	size_t i, y;
	int ret;

	for (i = offset; i < limit; i += 5) {
		iowrite8(CMD_READ_BYTE, dev->regs + SCR24X_CMD_STATUS);
		ret = scr24x_wait_ready(dev);
		if (ret < 0)
			return ret;

		for (y = 0; y < 5 && i + y < limit; y++)
			dev->buf[i + y] = ioread8(dev->regs + SCR24X_DATA(y));
	}

	return 0;
}

static ssize_t scr24x_read(struct file *filp, char __user *buf, size_t count,
								loff_t *ppos)
{
	struct scr24x_dev *dev = filp->private_data;
	int ret;
	int len;

	if (count < CCID_HEADER_SIZE)
		return -EINVAL;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	if (!dev->dev) {
		ret = -ENODEV;
		goto out;
	}

	ret = scr24x_wait_ready(dev);
	if (ret < 0)
		goto out;
	len = CCID_HEADER_SIZE;
	ret = read_chunk(dev, 0, len);
	if (ret < 0)
		goto out;

	len += le32_to_cpu(*(__le32 *)(&dev->buf[CCID_LENGTH_OFFSET]));
	if (len > sizeof(dev->buf)) {
		ret = -EIO;
		goto out;
	}
	ret = read_chunk(dev, CCID_HEADER_SIZE, len);
	if (ret < 0)
		goto out;

	if (len < count)
		count = len;

	if (copy_to_user(buf, dev->buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t scr24x_write(struct file *filp, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct scr24x_dev *dev = filp->private_data;
	size_t i, y;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	if (!dev->dev) {
		ret = -ENODEV;
		goto out;
	}

	if (count > sizeof(dev->buf)) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(dev->buf, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = scr24x_wait_ready(dev);
	if (ret < 0)
		goto out;

	iowrite8(CMD_START, dev->regs + SCR24X_CMD_STATUS);
	ret = scr24x_wait_ready(dev);
	if (ret < 0)
		goto out;

	for (i = 0; i < count; i += 5) {
		for (y = 0; y < 5 && i + y < count; y++)
			iowrite8(dev->buf[i + y], dev->regs + SCR24X_DATA(y));

		iowrite8(CMD_WRITE_BYTE, dev->regs + SCR24X_CMD_STATUS);
		ret = scr24x_wait_ready(dev);
		if (ret < 0)
			goto out;
	}

	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static const struct file_operations scr24x_fops = {
	.owner		= THIS_MODULE,
	.read		= scr24x_read,
	.write		= scr24x_write,
	.open		= scr24x_open,
	.release	= scr24x_release,
	.llseek		= no_llseek,
};

static int scr24x_config_check(struct pcmcia_device *link, void *priv_data)
{
	if (resource_size(link->resource[PCMCIA_IOPORT_0]) != 0x11)
		return -ENODEV;
	return pcmcia_request_io(link);
}

static int scr24x_probe(struct pcmcia_device *link)
{
	struct scr24x_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->devno = find_first_zero_bit(scr24x_minors, SCR24X_DEVS);
	if (dev->devno >= SCR24X_DEVS) {
		ret = -EBUSY;
		goto err;
	}

	mutex_init(&dev->lock);
	kref_init(&dev->refcnt);

	link->priv = dev;
	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, scr24x_config_check, NULL);
	if (ret < 0)
		goto err;

	dev->dev = &link->dev;
	dev->regs = devm_ioport_map(&link->dev,
				link->resource[PCMCIA_IOPORT_0]->start,
				resource_size(link->resource[PCMCIA_IOPORT_0]));
	if (!dev->regs) {
		ret = -EIO;
		goto err;
	}

	cdev_init(&dev->c_dev, &scr24x_fops);
	dev->c_dev.owner = THIS_MODULE;
	dev->c_dev.ops = &scr24x_fops;
	ret = cdev_add(&dev->c_dev, MKDEV(MAJOR(scr24x_devt), dev->devno), 1);
	if (ret < 0)
		goto err;

	ret = pcmcia_enable_device(link);
	if (ret < 0) {
		pcmcia_disable_device(link);
		goto err;
	}

	device_create(scr24x_class, NULL, MKDEV(MAJOR(scr24x_devt), dev->devno),
		      NULL, "scr24x%d", dev->devno);

	dev_info(&link->dev, "SCR24x Chip Card Interface\n");
	return 0;

err:
	if (dev->devno < SCR24X_DEVS)
		clear_bit(dev->devno, scr24x_minors);
	kfree (dev);
	return ret;
}

static void scr24x_remove(struct pcmcia_device *link)
{
	struct scr24x_dev *dev = (struct scr24x_dev *)link->priv;

	device_destroy(scr24x_class, MKDEV(MAJOR(scr24x_devt), dev->devno));
	mutex_lock(&dev->lock);
	pcmcia_disable_device(link);
	cdev_del(&dev->c_dev);
	clear_bit(dev->devno, scr24x_minors);
	dev->dev = NULL;
	mutex_unlock(&dev->lock);

	kref_put(&dev->refcnt, scr24x_delete);
}

static const struct pcmcia_device_id scr24x_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("HP", "PC Card Smart Card Reader",
					0x53cb94f9, 0xbfdf89a5),
	PCMCIA_DEVICE_PROD_ID1("SCR241 PCMCIA", 0x6271efa3),
	PCMCIA_DEVICE_PROD_ID1("SCR243 PCMCIA", 0x2054e8de),
	PCMCIA_DEVICE_PROD_ID1("SCR24x PCMCIA", 0x54a33665),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, scr24x_ids);

static struct pcmcia_driver scr24x_driver = {
	.owner		= THIS_MODULE,
	.name		= "scr24x_cs",
	.probe		= scr24x_probe,
	.remove		= scr24x_remove,
	.id_table	= scr24x_ids,
};

static int __init scr24x_init(void)
{
	int ret;

	scr24x_class = class_create(THIS_MODULE, "scr24x");
	if (IS_ERR(scr24x_class))
		return PTR_ERR(scr24x_class);

	ret = alloc_chrdev_region(&scr24x_devt, 0, SCR24X_DEVS, "scr24x");
	if (ret < 0)  {
		class_destroy(scr24x_class);
		return ret;
	}

	ret = pcmcia_register_driver(&scr24x_driver);
	if (ret < 0) {
		unregister_chrdev_region(scr24x_devt, SCR24X_DEVS);
		class_destroy(scr24x_class);
	}

	return ret;
}

static void __exit scr24x_exit(void)
{
	pcmcia_unregister_driver(&scr24x_driver);
	unregister_chrdev_region(scr24x_devt, SCR24X_DEVS);
	class_destroy(scr24x_class);
}

module_init(scr24x_init);
module_exit(scr24x_exit);

MODULE_AUTHOR("Lubomir Rintel");
MODULE_DESCRIPTION("SCR24x PCMCIA Smart Card Reader Driver");
MODULE_LICENSE("GPL");
