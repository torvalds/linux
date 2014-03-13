 /*
  *  drivers/char/sld-hub.c
  *
  * SLD HUB driver
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
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/sld-hub.h>
#include <linux/uaccess.h>

#define SLD_HUB_NAME "sld_hub"

#define ALT_SLD_HUB_DATA_WRITE        0x00
#define ALT_SLD_HUB_DATA_READ         0x08
#define ALT_SLD_HUB_FIFO_WRITE_COUNT  0x20
#define ALT_SLD_HUB_FIFO_READ_COUNT   0x40
#define ALT_SLD_HUB_ID_ROM            0x60
#define ALT_SLD_HUB_MGMT_INTF         0x70

#define ALT_SLD_HUB_FIFO_SIZE         32
#define ALT_SLD_HUB_READ_BUF_SIZE     4096
#define ALT_SLD_HUB_ROMID_SIZE        16

#define ALT_SLD_HUB_FLAG_BUSY         0

struct sld_hub_pdata {
	struct platform_device *pdev;

	unsigned int base_reg_phy;
	void __iomem *base_reg;

	unsigned long flags;

	struct kfifo read_kfifo;
	unsigned char kbuf[ALT_SLD_HUB_FIFO_SIZE];

	struct cdev sld_cdev;
};

static int sld_hub_remove(struct platform_device *pdev);

static struct class *sld_hub_class;

static int _read_sldfifo_into_kfifo(struct sld_hub_pdata *pdata)
{
	int num_bytes;
	int n;
	char chr;

	num_bytes = readb(pdata->base_reg + ALT_SLD_HUB_FIFO_READ_COUNT);
	for (n = 0; n < num_bytes; n++) {
		if (!kfifo_is_full(&pdata->read_kfifo)) {
			chr = readb(pdata->base_reg + ALT_SLD_HUB_DATA_READ);
			kfifo_in(&pdata->read_kfifo, &chr, 1);
		} else {
			dev_err(&pdata->pdev->dev, "Read Buffer FULL!\n");
			break;
		}
	}

	return n;
}

static ssize_t sld_hub_write(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct sld_hub_pdata *pdata = file->private_data;
	int num_bytes;
	int n;

	/*
	 * The design of the SLD HUB controller requires keeping the read
	 * FIFO not full, so we empty it every time we write
	 */
	_read_sldfifo_into_kfifo(pdata);

	num_bytes =
	    ALT_SLD_HUB_FIFO_SIZE - readb(pdata->base_reg +
					  ALT_SLD_HUB_FIFO_WRITE_COUNT);

	if (num_bytes == 0) {
		dev_err(&pdata->pdev->dev,
			"SLD HUB write fifo full!\n");
		return 0;
	}

	*ppos = 0;
	num_bytes = simple_write_to_buffer(pdata->kbuf,
				num_bytes, ppos, user_buf, count);

	for (n = 0; n < num_bytes; n++)
		writeb(pdata->kbuf[n],
		       pdata->base_reg + ALT_SLD_HUB_DATA_WRITE);

	return num_bytes;
}

static ssize_t sld_hub_read(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	int num_bytes;
	struct sld_hub_pdata *pdata = file->private_data;

	_read_sldfifo_into_kfifo(pdata);

	if (kfifo_to_user(&pdata->read_kfifo, user_buf, count, &num_bytes))
		dev_err(&pdata->pdev->dev,
			"Error copying fifo data to user data! %d bytes copied\n",
			num_bytes);

	return num_bytes;
}

static int sld_hub_open(struct inode *inode, struct file *file)
{
	struct sld_hub_pdata *pdata;

	pdata = container_of(inode->i_cdev, struct sld_hub_pdata, sld_cdev);
	if (test_and_set_bit_lock(ALT_SLD_HUB_FLAG_BUSY, &pdata->flags))
		return -EBUSY;
	file->private_data = pdata;

	return 0;
}

static int sld_hub_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct sld_hub_pdata *pdata;

	pdata = container_of(inode->i_cdev, struct sld_hub_pdata, sld_cdev);
	file->private_data = NULL;
	clear_bit_unlock(ALT_SLD_HUB_FLAG_BUSY, &pdata->flags);

	return ret;
}

static long sld_hub_read_romid(struct file *file, unsigned long arg)
{
	struct sld_hub_pdata *pdata = file->private_data;
	int i;

	for (i = 0; i < ALT_SLD_HUB_ROMID_SIZE; i++)
		pdata->kbuf[i] = readb(pdata->base_reg +
				ALT_SLD_HUB_ID_ROM + i);

	if (copy_to_user((void __user *)arg, pdata->kbuf,
			 ALT_SLD_HUB_ROMID_SIZE))
		return -EFAULT;

	return 0;
}

static long sld_hub_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	switch (cmd) {
	case SLDHUB_IO_ROMID:
		return sld_hub_read_romid(file, arg);
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations sld_hub_fops = {
	.write = sld_hub_write,
	.read = sld_hub_read,
	.open = sld_hub_open,
	.release = sld_hub_release,
	.unlocked_ioctl = sld_hub_ioctl,
	.llseek = no_llseek,
};

static int sld_hub_probe(struct platform_device *pdev)
{
	struct resource *areg;
	struct sld_hub_pdata *pdata;
	int ret;
	dev_t dev;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct sld_hub_pdata),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	areg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->base_reg = devm_request_and_ioremap(&pdev->dev, areg);
	if (!pdata->base_reg)
		return -EADDRNOTAVAIL;
	pdata->base_reg_phy = areg->start;

	pdata->pdev = pdev;
	platform_set_drvdata(pdev, pdata);

	if (kfifo_alloc(&pdata->read_kfifo,
			ALT_SLD_HUB_READ_BUF_SIZE, GFP_KERNEL))
		return -ENOMEM;

	sld_hub_class = class_create(THIS_MODULE, SLD_HUB_NAME);
	if (IS_ERR(sld_hub_class)) {
		ret = PTR_ERR(sld_hub_class);
		goto free_kfifo;
	}

	ret = alloc_chrdev_region(&dev, 0, 1, SLD_HUB_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "error from alloc_chrdev_region %d\n", ret);
		goto free_class;
	}

	cdev_init(&pdata->sld_cdev, &sld_hub_fops);
	ret = cdev_add(&pdata->sld_cdev, dev, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "error from cdev_add %d\n", ret);
		goto free_region;
	}

	if (IS_ERR(device_create(sld_hub_class, &pdev->dev,
				 dev, NULL, SLD_HUB_NAME))) {
		dev_err(&pdev->dev, "can't create device in /dev\n");
		ret = -ENODEV;
		goto free_cdev;
	}

	return 0;

free_cdev:
	cdev_del(&pdata->sld_cdev);
free_region:
	unregister_chrdev_region(pdata->sld_cdev.dev, 1);
free_class:
	class_destroy(sld_hub_class);
free_kfifo:
	kfifo_free(&pdata->read_kfifo);

	return ret;
}

static int sld_hub_remove(struct platform_device *pdev)
{
	struct sld_hub_pdata *pdata = platform_get_drvdata(pdev);

	device_destroy(sld_hub_class, pdata->sld_cdev.dev);
	cdev_del(&pdata->sld_cdev);
	unregister_chrdev_region(pdata->sld_cdev.dev, 1);
	class_destroy(sld_hub_class);
	kfifo_free(&pdata->read_kfifo);

	return 0;
}

static const struct of_device_id sld_hub_of_match[] = {
	{.compatible = "altr,sld-hub",},
	{},
};

MODULE_DEVICE_TABLE(of, sld_hub_of_match);

static struct platform_driver sld_hub_driver = {
	.probe = sld_hub_probe,
	.remove = sld_hub_remove,
	.driver = {
		   .name = SLD_HUB_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = sld_hub_of_match,
		   },
};

static int __init sld_hub_init(void)
{
	return platform_driver_probe(&sld_hub_driver, sld_hub_probe);
}

static void __exit sld_hub_exit(void)
{
	platform_driver_unregister(&sld_hub_driver);
}

module_init(sld_hub_init);
module_exit(sld_hub_exit);

MODULE_AUTHOR("Graham Moore (Altera)");
MODULE_DESCRIPTION("Altera SLD HUB Driver");
MODULE_LICENSE("GPL v2");
