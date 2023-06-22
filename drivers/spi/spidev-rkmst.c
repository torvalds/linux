// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/platform_data/spi-rockchip.h>

#define SPI_OBJ_MAX_XFER_SIZE	0x1040
#define SPI_OBJ_APP_RAM_SIZE	0x10000

#define SPI_OBJ_CTRL_MSG_SIZE	0x8
#define SPI_OBJ_CTRL_CMD_INIT	0x99
#define SPI_OBJ_CTRL_CMD_READ	0x3A
#define SPI_OBJ_CTRL_CMD_WRITE	0x4B
#define SPI_OBJ_CTRL_CMD_DUPLEX	0x5C

#define SPI_OBJ_DEFAULT_TIMEOUT_US	100000

struct spi_obj_ctrl {
	u16 cmd;
	u16 addr;
	u32 data;
};

struct spidev_rkmst_data {
	struct device	*dev;
	struct spi_device *spi;
	char *ctrlbuf;
	char *rxbuf;
	char *txbuf;
	struct gpio_desc *ready;
	int ready_irqnum;
	bool ready_status;
	bool verbose;
	struct miscdevice misc_dev;
};

static u32 bit_per_word = 8;

static inline void spidev_mst_slave_ready_status(struct spidev_rkmst_data *spidev, bool status)
{
	spidev->ready_status = status;
}

static irqreturn_t spidev_mst_slave_ready_interrupt(int irq, void *arg)
{
	struct spidev_rkmst_data *spidev = (struct spidev_rkmst_data *)arg;

	spidev_mst_slave_ready_status(spidev, true);

	return IRQ_HANDLED;
}

static bool spidev_mst_check_slave_ready(struct spidev_rkmst_data *spidev)
{
	return spidev->ready_status;
}

static int spidev_mst_wait_for_slave_ready(struct spidev_rkmst_data *spidev,
					   u32 timeout_us)
{
	bool ready;
	int ret;

	ret = read_poll_timeout(spidev_mst_check_slave_ready, ready,
				ready, 100, timeout_us + 100, false, spidev);
	if (ret) {
		dev_err(&spidev->spi->dev, "timeout and reset slave\n");

		return -ETIMEDOUT;
	}

	return true;
}

static int spidev_mst_write(struct spidev_rkmst_data *spidev, const void *txbuf, size_t n)
{
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.tx_buf = txbuf,
			.len = n,
			.bits_per_word = bit_per_word,
		};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spidev_mst_wait_for_slave_ready(spidev, SPI_OBJ_DEFAULT_TIMEOUT_US);
	if (ret < 0)
		return ret;

	spidev_mst_slave_ready_status(spidev, false);
	ret = spi_sync(spi, &m);

	return ret;
}

static int spidev_mst_write_bypass(struct spidev_rkmst_data *spidev, const void *txbuf, size_t n)
{
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.tx_buf = txbuf,
			.len = n,
			.bits_per_word = bit_per_word,
		};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spi_sync(spi, &m);

	return ret;
}

static int spidev_mst_read(struct spidev_rkmst_data *spidev, void *rxbuf, size_t n)
{
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.rx_buf = rxbuf,
			.len = n,
			.bits_per_word = bit_per_word,
		};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spidev_mst_wait_for_slave_ready(spidev, SPI_OBJ_MAX_XFER_SIZE);
	if (ret < 0)
		return ret;

	spidev_mst_slave_ready_status(spidev, false);
	ret = spi_sync(spi, &m);

	return ret;
}

static int spidev_slv_write_and_read(struct spidev_rkmst_data *spidev,
				     const void *tx_buf, void *rx_buf,
				     size_t len)
{
	struct spi_device *spi = spidev->spi;
	struct spi_transfer t = {
			.tx_buf = tx_buf,
			.rx_buf = rx_buf,
			.len = len,
		};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spidev_mst_wait_for_slave_ready(spidev, SPI_OBJ_MAX_XFER_SIZE);
	if (ret < 0)
		return ret;

	spidev_mst_slave_ready_status(spidev, false);
	ret = spi_sync(spi, &m);

	return ret;
}

static void spidev_rkmst_reset_slave(struct spidev_rkmst_data *spidev)
{
	struct spi_obj_ctrl *ctrl = (struct spi_obj_ctrl *)spidev->ctrlbuf;

	ctrl->cmd = SPI_OBJ_CTRL_CMD_INIT;

	spidev_mst_write_bypass(spidev, ctrl, SPI_OBJ_MAX_XFER_SIZE);
	msleep(100);
	spidev_mst_write_bypass(spidev, ctrl, SPI_OBJ_MAX_XFER_SIZE);
}


static int spidev_rkmst_ctrl(struct spidev_rkmst_data *spidev, u32 cmd, u16 addr, u32 data)
{
	struct spi_obj_ctrl *ctrl = (struct spi_obj_ctrl *)spidev->ctrlbuf;
	struct spi_device *spi = spidev->spi;
	int ret;

	if (spidev->verbose)
		dev_err(&spi->dev, "ctrl cmd=%x addr=0x%x data=0x%x\n", cmd, addr, data);

	/* ctrl_xfer */
	ctrl->cmd = cmd;
	ctrl->addr = addr;
	ctrl->data = data;
	ret = spidev_mst_write(spidev, ctrl, SPI_OBJ_CTRL_MSG_SIZE);
	if (ret) {
		dev_err(&spi->dev, "ctrl cmd=%x addr=0x%x data=0x%x, ret=%d\n", cmd, addr, data, ret);
		return -EIO;
	}

	return 0;
}

static int spidev_rkmst_xfer(struct spidev_rkmst_data *spidev, void *tx,
			     void *rx, u16 addr, u32 len)
{
	struct spi_device *spi = spidev->spi;
	int ret;
	u32 cmd;

	if (tx && rx)
		cmd = SPI_OBJ_CTRL_CMD_DUPLEX;
	else if (rx)
		cmd = SPI_OBJ_CTRL_CMD_READ;
	else if (tx)
		cmd = SPI_OBJ_CTRL_CMD_WRITE;
	else
		return -EINVAL;

	/* ctrl_xfer */
	ret = spidev_rkmst_ctrl(spidev, cmd, addr, len);
	if (ret) {
		spidev_rkmst_reset_slave(spidev);
		return -EIO;
	}

	if (spidev->verbose)
		dev_err(&spi->dev, "xfer len=0x%x\n", len);
	/* data_xfer */
	switch (cmd) {
	case SPI_OBJ_CTRL_CMD_READ:
		ret = spidev_mst_read(spidev, rx, len);
		if (ret)
			goto err_out;
		break;
	case SPI_OBJ_CTRL_CMD_WRITE:
		ret = spidev_mst_write(spidev, tx, len);
		if (ret)
			goto err_out;
		break;
	case SPI_OBJ_CTRL_CMD_DUPLEX:
		ret = spidev_slv_write_and_read(spidev, tx, rx, len);
		if (ret)
			goto err_out;
		break;
	default:
		dev_err(&spi->dev, "%s unknown\n", __func__);
	}

	return 0;
err_out:
	dev_err(&spi->dev, "xfer cmd=%x addr=0x%x len=0x%x, ret=%d\n",
		cmd, addr, len, ret);

	return ret;
}

static ssize_t spidev_rkmst_misc_write(struct file *filp, const char __user *buf,
				       size_t n, loff_t *offset)
{
	struct spidev_rkmst_data *spidev;
	struct spi_device *spi;
	int argc = 0, i;
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
	} else if (!strcmp(cmd, "init")) {
		spidev_rkmst_ctrl(spidev, SPI_OBJ_CTRL_CMD_INIT, 0x55AA, 0x1234567);
	} else if (!strcmp(cmd, "read")) {
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
			dev_err(&spi->dev, "rxbuf print out of size\n");
			return -EINVAL;
		}

		spidev_rkmst_xfer(spidev, NULL, spidev->rxbuf, addr, len);

		print_hex_dump(KERN_ERR, "m-r: ",
			       DUMP_PREFIX_OFFSET,
			       16,
			       1,
			       spidev->rxbuf,
			       len,
			       1);
	} else if (!strcmp(cmd, "write")) {
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
			dev_err(&spi->dev, "rxbuf print out of size\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++)
			spidev->txbuf[i] = i & 0xFF;
		((u32 *)spidev->txbuf)[0] = addr;

		spidev_rkmst_xfer(spidev, spidev->txbuf, NULL, addr, len);
	} else if (!strcmp(cmd, "duplex")) {
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
			dev_err(&spi->dev, "rxbuf print out of size\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++)
			spidev->txbuf[i] = i & 0xFF;
		((u32 *)spidev->txbuf)[0] = addr;

		spidev_rkmst_xfer(spidev, spidev->txbuf, spidev->rxbuf, addr, len);

		print_hex_dump(KERN_ERR, "m-d: ",
			       DUMP_PREFIX_OFFSET,
			       16,
			       1,
			       spidev->rxbuf,
			       len,
			       1);
	} else if (!strcmp(cmd, "autotest")) {
		int addr = 0, len, loops, i;
		unsigned long long bytes = 0;
		unsigned long us = 0;
		ktime_t start_time;
		ktime_t end_time;
		ktime_t cost_time;
		char *tempbuf;

		if (argc < 2)
			return -EINVAL;

		if (kstrtoint(argv[0], 0, &len))
			return -EINVAL;

		if (kstrtoint(argv[1], 0, &loops))
			return -EINVAL;

		if (!len) {
			dev_err(&spi->dev, "param invalid,%s %s\n", argv[0], argv[1]);
			return -EINVAL;
		}

		if (len > SPI_OBJ_APP_RAM_SIZE) {
			dev_err(&spi->dev, "rxbuf print out of size\n");
			return -EINVAL;
		}

		tempbuf = kzalloc(len, GFP_KERNEL);
		if (!tempbuf)
			return -ENOMEM;

		prandom_bytes(tempbuf, len);
		spidev_rkmst_xfer(spidev, tempbuf, NULL, addr, len);
		start_time = ktime_get();
		for (i = 0; i < loops; i++) {
			prandom_bytes(spidev->txbuf, len);
			spidev_rkmst_xfer(spidev, spidev->txbuf, spidev->rxbuf, addr, len);
			if (memcmp(spidev->rxbuf, tempbuf, len)) {
				dev_err(&spi->dev, "dulplex autotest failed, loops=%d\n", i);
				print_hex_dump(KERN_ERR, "m-d-t: ",
					       DUMP_PREFIX_OFFSET,
					       16,
					       1,
					       spidev->txbuf,
					       len,
					       1);
				print_hex_dump(KERN_ERR, "m-d-r: ",
					       DUMP_PREFIX_OFFSET,
					       16,
					       1,
					       spidev->rxbuf,
					       len,
					       1);
				print_hex_dump(KERN_ERR, "m-d-c: ",
					       DUMP_PREFIX_OFFSET,
					       16,
					       1,
					       tempbuf,
					       len,
					       1);
				break;
			}
			memcpy(tempbuf, spidev->txbuf, len);
		}
		end_time = ktime_get();
		cost_time = ktime_sub(end_time, start_time);
		us = ktime_to_us(cost_time);

		bytes = (u64)len * (u64)loops * 1000;
		do_div(bytes, us);
		if (i >= loops)
			dev_err(&spi->dev, "dulplex test pass, cost=%ldus, speed=%lldKB/S, %ldus/loops\n",
				us, bytes, us / loops);

		start_time = ktime_get();
		for (i = 0; i < loops; i++) {
			prandom_bytes(spidev->txbuf, len);
			spidev_rkmst_xfer(spidev, spidev->txbuf, NULL, addr, len);
			spidev_rkmst_xfer(spidev, NULL, spidev->rxbuf, addr, len);
			if (memcmp(spidev->rxbuf, spidev->txbuf, len)) {
				dev_err(&spi->dev, "read/write autotest failed, loops=%d\n", i);
				print_hex_dump(KERN_ERR, "m-d-t: ",
					       DUMP_PREFIX_OFFSET,
					       16,
					       1,
					       spidev->txbuf,
					       len,
					       1);
				print_hex_dump(KERN_ERR, "m-d-r: ",
					       DUMP_PREFIX_OFFSET,
					       16,
					       1,
					       spidev->rxbuf,
					       len,
					       1);
				break;
			}
		}
		end_time = ktime_get();
		cost_time = ktime_sub(end_time, start_time);
		us = ktime_to_us(cost_time);

		bytes = (u64)len * (u64)loops * 2 * 1000; /* multi 2 for both write and read */
		do_div(bytes, us);
		if (i >= loops)
			dev_err(&spi->dev, "read/write test pass, cost=%ldus, speed=%lldKB/S, %ldus/loops\n",
				us, bytes, us / loops);
		kfree(tempbuf);
	} else {
		dev_err(&spi->dev, "unknown command\n");
	}

	return n;
}

static int spidev_rkmst_misc_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *miscdev = filp->private_data;
	struct spidev_rkmst_data *spidev;

	spidev = container_of(miscdev, struct spidev_rkmst_data, misc_dev);
	filp->private_data = spidev;

	return 0;
}

static const struct file_operations spidev_rkmst_misc_fops = {
	.write = spidev_rkmst_misc_write,
	.open = spidev_rkmst_misc_open,
};

static int spidev_rkmst_probe(struct spi_device *spi)
{
	struct spidev_rkmst_data *spidev = NULL;
	int ret;

	if (!spi)
		return -ENOMEM;

	spidev = devm_kzalloc(&spi->dev, sizeof(struct spidev_rkmst_data), GFP_KERNEL);
	if (!spidev)
		return -ENOMEM;

	spidev->ctrlbuf = devm_kzalloc(&spi->dev, SPI_OBJ_MAX_XFER_SIZE, GFP_KERNEL);
	if (!spidev->ctrlbuf)
		return -ENOMEM;

	spidev->rxbuf = devm_kzalloc(&spi->dev, SPI_OBJ_APP_RAM_SIZE, GFP_KERNEL | GFP_DMA);
	if (!spidev->rxbuf)
		return -ENOMEM;

	spidev->txbuf = devm_kzalloc(&spi->dev, SPI_OBJ_MAX_XFER_SIZE, GFP_KERNEL);
	if (!spidev->txbuf)
		return -ENOMEM;

	spidev->spi = spi;
	spidev->dev = &spi->dev;

	spidev_mst_slave_ready_status(spidev, false);
	spidev->ready = devm_gpiod_get_optional(&spi->dev, "ready", GPIOD_IN);
	if (IS_ERR(spidev->ready))
		return dev_err_probe(&spi->dev, PTR_ERR(spidev->ready),
				     "invalid ready-gpios property in node\n");

	spidev->ready_irqnum = gpiod_to_irq(spidev->ready);
	ret = devm_request_irq(&spi->dev, spidev->ready_irqnum, spidev_mst_slave_ready_interrupt,
		IRQF_TRIGGER_FALLING, "spidev-mst-ready-in", spidev);
	if (ret < 0) {
		dev_err(&spi->dev, "request ready irq failed\n");
		return ret;
	}
	dev_set_drvdata(&spi->dev, spidev);

	dev_err(&spi->dev, "mode=%d, max_speed_hz=%d\n", spi->mode, spi->max_speed_hz);

	spidev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	spidev->misc_dev.name = "spidev_rkmst_misc";
	spidev->misc_dev.fops = &spidev_rkmst_misc_fops;
	spidev->misc_dev.parent = &spi->dev;
	ret = misc_register(&spidev->misc_dev);
	if (ret) {
		dev_err(&spi->dev, "fail to register misc device\n");
		return ret;
	}

	spidev_rkmst_reset_slave(spidev);

	return 0;
}

static int spidev_rkmst_remove(struct spi_device *spi)
{
	struct spidev_rkmst_data *spidev = dev_get_drvdata(&spi->dev);

	misc_deregister(&spidev->misc_dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spidev_rkmst_dt_match[] = {
	{ .compatible = "rockchip,spi-obj-master", },
	{},
};
MODULE_DEVICE_TABLE(of, spidev_rkmst_dt_match);

#endif /* CONFIG_OF */

static struct spi_driver spidev_rkmst_driver = {
	.driver = {
		.name	= "spidev_rkmst",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(spidev_rkmst_dt_match),
	},
	.probe		= spidev_rkmst_probe,
	.remove		= spidev_rkmst_remove,
};
module_spi_driver(spidev_rkmst_driver);

MODULE_AUTHOR("Jon Lin <jon.lin@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI Object Master Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev_rkmst");
