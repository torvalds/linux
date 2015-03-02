/*
 *  drivers/char/mm-debug-link.c
 *
 * MM DEBUG_LINK driver
 *
 * Adapted from sld-hub driver written by Graham Moore (grmoore@altera.com)
 *
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/mm-debug-link.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

#define MM_DEBUG_LINK_NAME "mm_debug_link"

#define MM_DEBUG_LINK_DATA_WRITE        0x00
#define MM_DEBUG_LINK_WRITE_CAPACITY    0x04
#define MM_DEBUG_LINK_DATA_READ         0x08
#define MM_DEBUG_LINK_READ_CAPACITY     0x0C
#define MM_DEBUG_LINK_FIFO_WRITE_COUNT  0x20
#define MM_DEBUG_LINK_FIFO_READ_COUNT   0x40
#define MM_DEBUG_LINK_ID_ROM            0x60
#define MM_DEBUG_LINK_SIGNATURE         0x70
#define MM_DEBUG_LINK_VERSION           0x74
#define MM_DEBUG_LINK_DEBUG_RESET       0x78
#define MM_DEBUG_LINK_MGMT_INTF         0x7C

/*
 * The value to expect at offset MM_DEBUG_LINK_SIGNATURE, aka "SysC".
 */
#define EXPECT_SIGNATURE 0x53797343

/*
 * The maximum version this driver supports.
 */
#define MAX_SUPPORTED_VERSION 1

/*
 * The size of mm_debug_link_pdata.read_kfifo. It must be a power of 2 to
 *  satisfy kfifo_alloc(). Data is transferred from the read FIFO within
 *  altera_mm_debug_link into this kfifo. The value was determined by
 *  trial and error; it must be large enough to avoid overflow when
 *  reading while writing.
 */
#define MM_DEBUG_LINK_READ_BUF_SIZE     4096

#define MM_DEBUG_LINK_FLAG_BUSY         0

struct mm_debug_link_pdata {
	struct platform_device *pdev;

	unsigned int base_reg_phy;
	void __iomem *base_reg;

	unsigned long flags;

	struct kfifo read_kfifo;
	unsigned char *kbuf;
	size_t fifo_capacity;

	struct cdev mmdebuglink_cdev;
};

static int mm_debug_link_remove(struct platform_device *pdev);

static struct class *mm_debug_link_class;

/*
 * _read_mmdebuglink_into_kfifo()
 *
 * Private helper function.
 *
 * Read all available bytes from the mm debug link's read FIFO into
 * pdata->read_kfifo.
 *
 * Return: the number of bytes written into pdata->read_kfifo.
 */
static int _read_mmdebuglink_into_kfifo(struct mm_debug_link_pdata *pdata)
{
	int num_bytes;
	int n;
	char chr;


	num_bytes = readb(pdata->base_reg + MM_DEBUG_LINK_FIFO_READ_COUNT);
	for (n = 0; n < num_bytes; n++) {
		if (kfifo_is_full(&pdata->read_kfifo))
			/*
			 * The read FIFO is full.
			 *
			 */
			break;
		chr = readb(pdata->base_reg + MM_DEBUG_LINK_DATA_READ);
		kfifo_in(&pdata->read_kfifo, &chr, 1);
	}

	return n;
}

/*
 * mm_debug_link_write() - file_operations API write function
 *
 * Return: the number of bytes written.
 */
static ssize_t mm_debug_link_write(
	struct file *file,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct mm_debug_link_pdata *pdata = file->private_data;
	int num_bytes;
	int n;

	/*
	 * If the debug link's read FIFO fills, the write FIFO will eventually
	 * fill, and then the hardware will stop accepting write data. Avoid
	 * deadlock by servicing the read FIFO before writing.
	 */
	_read_mmdebuglink_into_kfifo(pdata);

	/*
	 * num_bytes is the number of unused byte locations in the write FIFO.
	 */
	num_bytes = pdata->fifo_capacity -
		readb(pdata->base_reg + MM_DEBUG_LINK_FIFO_WRITE_COUNT);

	if (num_bytes == 0)
		/*
		 * The write FIFO is full: don't accept data.
		 */
		return 0;

	*ppos = 0;
	num_bytes = simple_write_to_buffer(pdata->kbuf,
				num_bytes, ppos, user_buf, count);

	for (n = 0; n < num_bytes; n++)
		writeb(pdata->kbuf[n],
		       pdata->base_reg + MM_DEBUG_LINK_DATA_WRITE);

	return num_bytes;
}

/*
 * mm_debug_link_read() - file_operations API read function
 *
 * Return: the number of bytes read.
 */
static ssize_t mm_debug_link_read(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	int num_bytes;
	struct mm_debug_link_pdata *pdata = file->private_data;

	_read_mmdebuglink_into_kfifo(pdata);

	if (kfifo_to_user(&pdata->read_kfifo, user_buf, count, &num_bytes))
		dev_err(&pdata->pdev->dev,
			"Error copying fifo data to user data! %d bytes copied\n",
			num_bytes);
	return num_bytes;
}

/*
 * mm_debug_link_open() - file_operations API open function
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int mm_debug_link_open(struct inode *inode, struct file *file)
{
	struct mm_debug_link_pdata *pdata;

	pdata = container_of(inode->i_cdev, struct mm_debug_link_pdata,
		mmdebuglink_cdev);

	if (test_and_set_bit_lock(MM_DEBUG_LINK_FLAG_BUSY, &pdata->flags))
		return -EBUSY;
	file->private_data = pdata;

	return 0;
}

/*
 * mm_debug_link_release() - file_operations API release function
 *
 * Return: 0 on success, error code on error.
 */
static int mm_debug_link_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct mm_debug_link_pdata *pdata;

	pdata = container_of(inode->i_cdev, struct mm_debug_link_pdata,
		mmdebuglink_cdev);
	file->private_data = NULL;
	clear_bit_unlock(MM_DEBUG_LINK_FLAG_BUSY, &pdata->flags);

	return ret;
}

static long mm_debug_link_read_romid(struct mm_debug_link_pdata *pdata,
	unsigned long arg)
{
	int i;

	for (i = 0; i < MM_DEBUG_LINK_ID_SIZE; i++)
		pdata->kbuf[i] = readb(pdata->base_reg +
				MM_DEBUG_LINK_ID_ROM + i);

	if (copy_to_user((void __user *)arg, pdata->kbuf,
			 MM_DEBUG_LINK_ID_SIZE))
		return -EFAULT;

	return 0;
}

static long mm_debug_link_write_mixer(struct mm_debug_link_pdata *pdata,
	unsigned long arg)
{
	writeb(arg, pdata->base_reg + MM_DEBUG_LINK_ID_ROM);
	return 0;
}

static long mm_debug_link_enable(struct mm_debug_link_pdata *pdata,
	unsigned long arg)
{
	writel(arg, pdata->base_reg + MM_DEBUG_LINK_MGMT_INTF);
	return 0;
}

static long mm_debug_link_debug_reset(struct mm_debug_link_pdata *pdata,
	unsigned long arg)
{
	writel(arg, pdata->base_reg + MM_DEBUG_LINK_DEBUG_RESET);
	return 0;
}

static long mm_debug_link_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct mm_debug_link_pdata *pdata = file->private_data;
	long result = -ENOIOCTLCMD;

	switch (cmd) {
	case MM_DEBUG_LINK_IOCTL_READ_ID:
		result = mm_debug_link_read_romid(pdata, arg);
		break;
	case MM_DEBUG_LINK_IOCTL_WRITE_MIXER:
		result = mm_debug_link_write_mixer(pdata, arg);
		break;
	case MM_DEBUG_LINK_IOCTL_ENABLE:
		result = mm_debug_link_enable(pdata, arg);
		break;
	case MM_DEBUG_LINK_IOCTL_DEBUG_RESET:
		result = mm_debug_link_debug_reset(pdata, arg);
		break;
	}

	return result;
}

static const struct file_operations mm_debug_link_fops = {
	.write = mm_debug_link_write,
	.read = mm_debug_link_read,
	.open = mm_debug_link_open,
	.release = mm_debug_link_release,
	.unlocked_ioctl = mm_debug_link_ioctl,
	.llseek = no_llseek,
};

/*
 * mm_debug_link_probe() - platform device API probe function
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int mm_debug_link_probe(struct platform_device *pdev)
{
	struct resource *areg;
	struct mm_debug_link_pdata *pdata;
	int ret;
	unsigned long sig = 0L, version = 0L;
	size_t kbuf_size;
	dev_t dev;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct mm_debug_link_pdata),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	areg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->base_reg = devm_request_and_ioremap(&pdev->dev, areg);
	if (!pdata->base_reg)
		return -EADDRNOTAVAIL;

	/* Check the signature, fail if not found. */
	sig = readl(pdata->base_reg + MM_DEBUG_LINK_SIGNATURE);
	if (sig != EXPECT_SIGNATURE)
		return -ENODEV;

	/* Check the version, fail if not compatible */
	version = readl(pdata->base_reg + MM_DEBUG_LINK_VERSION);
	if (version > MAX_SUPPORTED_VERSION)
		return -ENODEV;

	pdata->fifo_capacity = readl(pdata->base_reg +
				  MM_DEBUG_LINK_WRITE_CAPACITY);
	/*
	 * kbuf is used both for the link ID value, and for data on its way
	 * into the write FIFO. Allocate a buffer large enough for either.
	 */
	kbuf_size = max(MM_DEBUG_LINK_ID_SIZE, pdata->fifo_capacity);
	pdata->kbuf = devm_kzalloc(&pdev->dev, kbuf_size, GFP_KERNEL);

	if (!pdata->kbuf)
		return -ENOMEM;

	pdata->base_reg_phy = areg->start;

	pdata->pdev = pdev;
	platform_set_drvdata(pdev, pdata);

	if (kfifo_alloc(&pdata->read_kfifo,
			MM_DEBUG_LINK_READ_BUF_SIZE, GFP_KERNEL))
		return -ENOMEM;

	mm_debug_link_class = class_create(THIS_MODULE, MM_DEBUG_LINK_NAME);
	if (IS_ERR(mm_debug_link_class)) {
		ret = PTR_ERR(mm_debug_link_class);
		goto free_kfifo;
	}

	ret = alloc_chrdev_region(&dev, 0, 1, MM_DEBUG_LINK_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "error from alloc_chrdev_region %d\n", ret);
		goto free_class;
	}

	cdev_init(&pdata->mmdebuglink_cdev, &mm_debug_link_fops);
	ret = cdev_add(&pdata->mmdebuglink_cdev, dev, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "error from cdev_add %d\n", ret);
		goto free_region;
	}

	if (IS_ERR(device_create(mm_debug_link_class, &pdev->dev,
				 dev, NULL, MM_DEBUG_LINK_NAME))) {
		dev_err(&pdev->dev, "can't create device in /dev\n");
		ret = -ENODEV;
		goto free_cdev;
	}

	return 0;

free_cdev:
	cdev_del(&pdata->mmdebuglink_cdev);
free_region:
	unregister_chrdev_region(pdata->mmdebuglink_cdev.dev, 1);
free_class:
	class_destroy(mm_debug_link_class);
free_kfifo:
	kfifo_free(&pdata->read_kfifo);

	return ret;
}

/*
 * mm_debug_link_remove() - platform device API remove function
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int mm_debug_link_remove(struct platform_device *pdev)
{
	struct mm_debug_link_pdata *pdata = platform_get_drvdata(pdev);
	device_destroy(mm_debug_link_class, pdata->mmdebuglink_cdev.dev);
	cdev_del(&pdata->mmdebuglink_cdev);
	unregister_chrdev_region(pdata->mmdebuglink_cdev.dev, 1);
	class_destroy(mm_debug_link_class);
	kfifo_free(&pdata->read_kfifo);

	return 0;
}

static const struct of_device_id mm_debug_link_of_match[] = {
	{.compatible = "altr,mm-debug-link-1.0",},
	{},
};

MODULE_DEVICE_TABLE(of, mm_debug_link_of_match);

static struct platform_driver mm_debug_link_driver = {
	.probe = mm_debug_link_probe,
	.remove = mm_debug_link_remove,
	.driver = {
		   .name = MM_DEBUG_LINK_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = mm_debug_link_of_match,
		   },
};

/*
 * mm_debug_link_init() - module API init function
 *
 */
static int __init mm_debug_link_init(void)
{
	return platform_driver_probe(&mm_debug_link_driver,
		mm_debug_link_probe);
}

/*
 * mm_debug_link_exit() - module API exit function
 *
 */
static void __exit mm_debug_link_exit(void)
{
	platform_driver_unregister(&mm_debug_link_driver);
}

module_init(mm_debug_link_init);
module_exit(mm_debug_link_exit);

MODULE_AUTHOR("Aaron Ferrucci (Altera)");
MODULE_DESCRIPTION("Altera MM DEBUG_LINK Driver");
MODULE_LICENSE("GPL v2");
