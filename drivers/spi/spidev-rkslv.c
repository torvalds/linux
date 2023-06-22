// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#define SPI_OBJ_MAX_XFER_SIZE	0x1040
#define SPI_OBJ_APP_RAM_SIZE	0x10000

#define SPI_OBJ_CTRL_MSG_SIZE	0x8
#define SPI_OBJ_CTRL_CMD_INIT	0x99
#define SPI_OBJ_CTRL_CMD_READ	0x3A
#define SPI_OBJ_CTRL_CMD_WRITE	0x4B
#define SPI_OBJ_CTRL_CMD_DUPLEX	0x5C

struct spi_obj_ctrl {
	u16 cmd;
	u16 addr;
	u32 data;
};

struct spidev_rkslv_data {
	struct device	*dev;
	struct spi_device *spi;
	char *ctrlbuf;
	char *appmem;
	char *tempbuf;
	bool verbose;
	struct task_struct *tsk;
	bool tsk_run;
	struct miscdevice misc_dev;
};

static u32 bit_per_word = 8;

static int spidev_slv_write(struct spidev_rkslv_data *spidev, const void *txbuf, size_t n)
{
	int ret = -1;
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.tx_buf = txbuf,
			.len = n,
			.bits_per_word = bit_per_word,
		};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(spi, &m);

	return ret;
}

static int spidev_slv_read(struct spidev_rkslv_data *spidev, void *rxbuf, size_t n)
{
	int ret = -1;
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.rx_buf = rxbuf,
			.len = n,
			.bits_per_word = bit_per_word,
		};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(spi, &m);

	return ret;
}

static int spidev_slv_write_and_read(struct spidev_rkslv_data *spidev, const void *tx_buf,
				     void *rx_buf, size_t len)
{
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.tx_buf = tx_buf,
			.rx_buf = rx_buf,
			.len = len,
		};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static ssize_t spidev_rkslv_misc_write(struct file *filp, const char __user *buf,
				       size_t n, loff_t *offset)
{
	struct spidev_rkslv_data *spidev;
	struct spi_device *spi;
	int argc = 0;
	char tmp[64];
	char *argv[16];
	char *cmd, *data;

	if (n >= 64)
		return -EINVAL;

	spidev = filp->private_data;

	if (!spidev)
		return -ESHUTDOWN;

	spi = spidev->spi;
	memset(tmp, 0, sizeof(tmp));
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	cmd = tmp;
	data = tmp;

	while (data < (tmp + n)) {
		data = strstr(data, " ");
		if (!data)
			break;
		*data = 0;
		argv[argc] = ++data;
		argc++;
		if (argc >= 16)
			break;
	}

	tmp[n - 1] = 0;

	if (!strcmp(cmd, "verbose")) {
		int val;

		if (argc < 1)
			return -EINVAL;

		if (kstrtoint(argv[0], 0, &val))
			return -EINVAL;

		if (val == 1)
			spidev->verbose = true;
		else
			spidev->verbose = false;
	} else if (!strcmp(cmd, "appmem")) {
		int addr, len;

		if (argc < 2)
			return -EINVAL;

		if (kstrtoint(argv[0], 0, &addr))
			return -EINVAL;
		if (kstrtoint(argv[1], 0, &len))
			return -EINVAL;

		if (!len) {
			dev_err(&spi->dev, "param invalid,%s %s\n", argv[0], argv[1]);
			return -EINVAL;
		}

		if (addr + len > SPI_OBJ_APP_RAM_SIZE) {
			dev_err(&spi->dev, "appmem print out of size\n");
			return -EINVAL;
		}

		print_hex_dump(KERN_ERR, "APPMEM: ",
			       DUMP_PREFIX_OFFSET,
			       16,
			       1,
			       spidev->appmem + addr,
			       len,
			       1);
	} else {
		dev_err(&spi->dev, "unknown command\n");
	}

	return n;
}

static int spidev_rkslv_misc_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *miscdev = filp->private_data;
	struct spidev_rkslv_data *spidev;

	spidev = container_of(miscdev, struct spidev_rkslv_data, misc_dev);
	filp->private_data = spidev;

	return 0;
}

static const struct file_operations spidev_rkslv_misc_fops = {
	.write = spidev_rkslv_misc_write,
	.open = spidev_rkslv_misc_open,
};

static int spidev_rkslv_xfer(struct spidev_rkslv_data *spidev)
{
	char *ctrlbuf = spidev->ctrlbuf, *appmem = spidev->appmem, *tempbuf = spidev->tempbuf;
	struct spi_obj_ctrl *ctrl;
	struct spi_device *spi = spidev->spi;
	u32 len;
	int ret;

	memset(spidev->ctrlbuf, 0, SPI_OBJ_CTRL_MSG_SIZE);
	ret = spidev_slv_read(spidev, spidev->ctrlbuf, SPI_OBJ_CTRL_MSG_SIZE);
	if (ret) {
		dev_err(&spi->dev, "%s ctrl\n", __func__);
		return -EIO;
	}

	ctrl = (struct spi_obj_ctrl *)ctrlbuf;
	if (spidev->verbose)
		dev_err(&spi->dev, "ctrl cmd=%x addr=0x%x data=0x%x\n",
			ctrl->cmd, ctrl->addr, ctrl->data);

	switch (ctrl->cmd) {
	case SPI_OBJ_CTRL_CMD_INIT:
		return 0;
	case SPI_OBJ_CTRL_CMD_READ:
		len = ctrl->data;
		ret = spidev_slv_write(spidev, appmem + ctrl->addr, len);
		if (ret) {
			dev_err(&spi->dev, "%s cmd=%x addr=0x%x data=0x%x\n",
				__func__, ctrl->cmd, ctrl->addr, ctrl->data);
			return -EIO;
		}
		break;
	case SPI_OBJ_CTRL_CMD_WRITE:
		len = ctrl->data;
		ret = spidev_slv_read(spidev, appmem + ctrl->addr, len);
		if (ret) {
			dev_err(&spi->dev, "%s cmd=%x addr=0x%x data=0x%x\n",
				__func__, ctrl->cmd, ctrl->addr, ctrl->data);
			return -EIO;
		}
		if (spidev->verbose) {
			print_hex_dump(KERN_ERR, "s-r: ",
					DUMP_PREFIX_OFFSET,
					16,
					1,
					appmem + ctrl->addr,
					len,
					1);
		}
		break;
	case SPI_OBJ_CTRL_CMD_DUPLEX:
		len = ctrl->data;
		ret = spidev_slv_write_and_read(spidev, appmem + ctrl->addr, tempbuf, len);
		if (ret) {
			dev_err(&spi->dev, "%s cmd=%x addr=0x%x data=0x%x\n",
				__func__, ctrl->cmd, ctrl->addr, ctrl->data);
			return -EIO;
		}
		if (spidev->verbose) {
			print_hex_dump(KERN_ERR, "s-d-t: ",
				DUMP_PREFIX_OFFSET,
				16,
				1,
				appmem + ctrl->addr,
				len,
				1);
			print_hex_dump(KERN_ERR, "s-d-r: ",
					DUMP_PREFIX_OFFSET,
					16,
					1,
					tempbuf,
					len,
					1);
		}
		memcpy(appmem + ctrl->addr, tempbuf, len);
		break;
	default:
		if (spidev->verbose)
			dev_err(&spi->dev, "%s unknown\n", __func__);
		return 0;
	}

	if (spidev->verbose)
		dev_err(&spi->dev, "xfer len=0x%x\n", ctrl->data);

	return 0;
}

static int spidev_rkslv_ctrl_receiver_thread(void *p)
{
	struct spidev_rkslv_data *spidev = (struct spidev_rkslv_data *)p;

	while (spidev->tsk_run)
		spidev_rkslv_xfer(spidev);

	return 0;
}

static int spidev_rkslv_probe(struct spi_device *spi)
{
	struct spidev_rkslv_data *spidev = NULL;
	int ret;

	if (!spi)
		return -ENOMEM;

	spidev = devm_kzalloc(&spi->dev, sizeof(struct spidev_rkslv_data), GFP_KERNEL);
	if (!spidev)
		return -ENOMEM;

	spidev->ctrlbuf = devm_kzalloc(&spi->dev, SPI_OBJ_MAX_XFER_SIZE, GFP_KERNEL);
	if (!spidev->ctrlbuf)
		return -ENOMEM;

	spidev->appmem = devm_kzalloc(&spi->dev, SPI_OBJ_APP_RAM_SIZE, GFP_KERNEL | GFP_DMA);
	if (!spidev->appmem)
		return -ENOMEM;

	spidev->tempbuf = devm_kzalloc(&spi->dev, SPI_OBJ_MAX_XFER_SIZE, GFP_KERNEL);
	if (!spidev->tempbuf)
		return -ENOMEM;

	spidev->spi = spi;
	spidev->dev = &spi->dev;
	dev_set_drvdata(&spi->dev, spidev);

	dev_err(&spi->dev, "mode=%d, max_speed_hz=%d\n", spi->mode, spi->max_speed_hz);

	spidev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	spidev->misc_dev.name = "spidev_rkslv_misc";
	spidev->misc_dev.fops = &spidev_rkslv_misc_fops;
	spidev->misc_dev.parent = &spi->dev;
	ret = misc_register(&spidev->misc_dev);
	if (ret) {
		dev_err(&spi->dev, "fail to register misc device\n");
		return ret;
	}

	spidev->tsk_run = true;
	spidev->tsk = kthread_run(spidev_rkslv_ctrl_receiver_thread, spidev, "spidev-rkslv");
	if (IS_ERR(spidev->tsk)) {
		dev_err(&spi->dev, "start spidev-rkslv thread failed\n");
		return PTR_ERR(spidev->tsk);
	}

	return 0;
}

static int spidev_rkslv_remove(struct spi_device *spi)
{
	struct spidev_rkslv_data *spidev = dev_get_drvdata(&spi->dev);

	spidev->tsk_run = false;
	spi_slave_abort(spi);
	kthread_stop(spidev->tsk);
	misc_deregister(&spidev->misc_dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spidev_rkslv_dt_match[] = {
	{ .compatible = "rockchip,spi-obj-slave", },
	{},
};
MODULE_DEVICE_TABLE(of, spidev_rkslv_dt_match);

#endif /* CONFIG_OF */

static struct spi_driver spidev_rkmst_driver = {
	.driver = {
		.name	= "spidev_rkslv",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(spidev_rkslv_dt_match),
	},
	.probe		= spidev_rkslv_probe,
	.remove		= spidev_rkslv_remove,
};
module_spi_driver(spidev_rkmst_driver);

MODULE_AUTHOR("Jon Lin <jon.lin@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI Object Slave Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev_rkslv");
