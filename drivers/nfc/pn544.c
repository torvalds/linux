/*
 * Driver for the PN544 NFC chip.
 *
 * Copyright (C) Nokia Corporation
 *
 * Author: Jari Vanhala <ext-jari.vanhala@nokia.com>
 * Contact: Matti Aaltonen <matti.j.aaltonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/completion.h>
#include <linux/crc-ccitt.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nfc/pn544.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/serial_core.h> /* for TCGETS */
#include <linux/slab.h>

#define DRIVER_CARD	"PN544 NFC"
#define DRIVER_DESC	"NFC driver for PN544"

static struct i2c_device_id pn544_id_table[] = {
	{ PN544_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pn544_id_table);

#define HCI_MODE	0
#define FW_MODE		1

enum pn544_state {
	PN544_ST_COLD,
	PN544_ST_FW_READY,
	PN544_ST_READY,
};

enum pn544_irq {
	PN544_NONE,
	PN544_INT,
};

struct pn544_info {
	struct miscdevice miscdev;
	struct i2c_client *i2c_dev;
	struct regulator_bulk_data regs[3];

	enum pn544_state state;
	wait_queue_head_t read_wait;
	loff_t read_offset;
	enum pn544_irq read_irq;
	struct mutex read_mutex; /* Serialize read_irq access */
	struct mutex mutex; /* Serialize info struct access */
	u8 *buf;
	size_t buflen;
};

static const char reg_vdd_io[]	= "Vdd_IO";
static const char reg_vbat[]	= "VBat";
static const char reg_vsim[]	= "VSim";

/* sysfs interface */
static ssize_t pn544_test(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct pn544_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->i2c_dev;
	struct pn544_nfc_platform_data *pdata = client->dev.platform_data;

	return snprintf(buf, PAGE_SIZE, "%d\n", pdata->test());
}

static int pn544_enable(struct pn544_info *info, int mode)
{
	struct pn544_nfc_platform_data *pdata;
	struct i2c_client *client = info->i2c_dev;

	int r;

	r = regulator_bulk_enable(ARRAY_SIZE(info->regs), info->regs);
	if (r < 0)
		return r;

	pdata = client->dev.platform_data;
	info->read_irq = PN544_NONE;
	if (pdata->enable)
		pdata->enable(mode);

	if (mode) {
		info->state = PN544_ST_FW_READY;
		dev_dbg(&client->dev, "now in FW-mode\n");
	} else {
		info->state = PN544_ST_READY;
		dev_dbg(&client->dev, "now in HCI-mode\n");
	}

	usleep_range(10000, 15000);

	return 0;
}

static void pn544_disable(struct pn544_info *info)
{
	struct pn544_nfc_platform_data *pdata;
	struct i2c_client *client = info->i2c_dev;

	pdata = client->dev.platform_data;
	if (pdata->disable)
		pdata->disable();

	info->state = PN544_ST_COLD;

	dev_dbg(&client->dev, "Now in OFF-mode\n");

	msleep(PN544_RESETVEN_TIME);

	info->read_irq = PN544_NONE;
	regulator_bulk_disable(ARRAY_SIZE(info->regs), info->regs);
}

static int check_crc(u8 *buf, int buflen)
{
	u8 len;
	u16 crc;

	len = buf[0] + 1;
	if (len < 4 || len != buflen || len > PN544_MSG_MAX_SIZE) {
		pr_err(PN544_DRIVER_NAME
		       ": CRC; corrupt packet len %u (%d)\n", len, buflen);
		print_hex_dump(KERN_DEBUG, "crc: ", DUMP_PREFIX_NONE,
			       16, 2, buf, buflen, false);
		return -EPERM;
	}
	crc = crc_ccitt(0xffff, buf, len - 2);
	crc = ~crc;

	if (buf[len-2] != (crc & 0xff) || buf[len-1] != (crc >> 8)) {
		pr_err(PN544_DRIVER_NAME ": CRC error 0x%x != 0x%x 0x%x\n",
		       crc, buf[len-1], buf[len-2]);

		print_hex_dump(KERN_DEBUG, "crc: ", DUMP_PREFIX_NONE,
			       16, 2, buf, buflen, false);
		return -EPERM;
	}
	return 0;
}

static int pn544_i2c_write(struct i2c_client *client, u8 *buf, int len)
{
	int r;

	if (len < 4 || len != (buf[0] + 1)) {
		dev_err(&client->dev, "%s: Illegal message length: %d\n",
			__func__, len);
		return -EINVAL;
	}

	if (check_crc(buf, len))
		return -EINVAL;

	usleep_range(3000, 6000);

	r = i2c_master_send(client, buf, len);
	dev_dbg(&client->dev, "send: %d\n", r);

	if (r == -EREMOTEIO) { /* Retry, chip was in standby */
		usleep_range(6000, 10000);
		r = i2c_master_send(client, buf, len);
		dev_dbg(&client->dev, "send2: %d\n", r);
	}

	if (r != len)
		return -EREMOTEIO;

	return r;
}

static int pn544_i2c_read(struct i2c_client *client, u8 *buf, int buflen)
{
	int r;
	u8 len;

	/*
	 * You could read a packet in one go, but then you'd need to read
	 * max size and rest would be 0xff fill, so we do split reads.
	 */
	r = i2c_master_recv(client, &len, 1);
	dev_dbg(&client->dev, "recv1: %d\n", r);

	if (r != 1)
		return -EREMOTEIO;

	if (len < PN544_LLC_HCI_OVERHEAD)
		len = PN544_LLC_HCI_OVERHEAD;
	else if (len > (PN544_MSG_MAX_SIZE - 1))
		len = PN544_MSG_MAX_SIZE - 1;

	if (1 + len > buflen) /* len+(data+crc16) */
		return -EMSGSIZE;

	buf[0] = len;

	r = i2c_master_recv(client, buf + 1, len);
	dev_dbg(&client->dev, "recv2: %d\n", r);

	if (r != len)
		return -EREMOTEIO;

	usleep_range(3000, 6000);

	return r + 1;
}

static int pn544_fw_write(struct i2c_client *client, u8 *buf, int len)
{
	int r;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (len < PN544_FW_HEADER_SIZE ||
	    (PN544_FW_HEADER_SIZE + (buf[1] << 8) + buf[2]) != len)
		return -EINVAL;

	r = i2c_master_send(client, buf, len);
	dev_dbg(&client->dev, "fw send: %d\n", r);

	if (r == -EREMOTEIO) { /* Retry, chip was in standby */
		usleep_range(6000, 10000);
		r = i2c_master_send(client, buf, len);
		dev_dbg(&client->dev, "fw send2: %d\n", r);
	}

	if (r != len)
		return -EREMOTEIO;

	return r;
}

static int pn544_fw_read(struct i2c_client *client, u8 *buf, int buflen)
{
	int r, len;

	if (buflen < PN544_FW_HEADER_SIZE)
		return -EINVAL;

	r = i2c_master_recv(client, buf, PN544_FW_HEADER_SIZE);
	dev_dbg(&client->dev, "FW recv1: %d\n", r);

	if (r < 0)
		return r;

	if (r < PN544_FW_HEADER_SIZE)
		return -EINVAL;

	len = (buf[1] << 8) + buf[2];
	if (len == 0) /* just header, no additional data */
		return r;

	if (len > buflen - PN544_FW_HEADER_SIZE)
		return -EMSGSIZE;

	r = i2c_master_recv(client, buf + PN544_FW_HEADER_SIZE, len);
	dev_dbg(&client->dev, "fw recv2: %d\n", r);

	if (r != len)
		return -EINVAL;

	return r + PN544_FW_HEADER_SIZE;
}

static irqreturn_t pn544_irq_thread_fn(int irq, void *dev_id)
{
	struct pn544_info *info = dev_id;
	struct i2c_client *client = info->i2c_dev;

	BUG_ON(!info);
	BUG_ON(irq != info->i2c_dev->irq);

	dev_dbg(&client->dev, "IRQ\n");

	mutex_lock(&info->read_mutex);
	info->read_irq = PN544_INT;
	mutex_unlock(&info->read_mutex);

	wake_up_interruptible(&info->read_wait);

	return IRQ_HANDLED;
}

static enum pn544_irq pn544_irq_state(struct pn544_info *info)
{
	enum pn544_irq irq;

	mutex_lock(&info->read_mutex);
	irq = info->read_irq;
	mutex_unlock(&info->read_mutex);
	/*
	 * XXX: should we check GPIO-line status directly?
	 * return pdata->irq_status() ? PN544_INT : PN544_NONE;
	 */

	return irq;
}

static ssize_t pn544_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;
	enum pn544_irq irq;
	size_t len;
	int r = 0;

	dev_dbg(&client->dev, "%s: info: %p, count: %zu\n", __func__,
		info, count);

	mutex_lock(&info->mutex);

	if (info->state == PN544_ST_COLD) {
		r = -ENODEV;
		goto out;
	}

	irq = pn544_irq_state(info);
	if (irq == PN544_NONE) {
		if (file->f_flags & O_NONBLOCK) {
			r = -EAGAIN;
			goto out;
		}

		if (wait_event_interruptible(info->read_wait,
					     (info->read_irq == PN544_INT))) {
			r = -ERESTARTSYS;
			goto out;
		}
	}

	if (info->state == PN544_ST_FW_READY) {
		len = min(count, info->buflen);

		mutex_lock(&info->read_mutex);
		r = pn544_fw_read(info->i2c_dev, info->buf, len);
		info->read_irq = PN544_NONE;
		mutex_unlock(&info->read_mutex);

		if (r < 0) {
			dev_err(&info->i2c_dev->dev, "FW read failed: %d\n", r);
			goto out;
		}

		print_hex_dump(KERN_DEBUG, "FW read: ", DUMP_PREFIX_NONE,
			       16, 2, info->buf, r, false);

		*offset += r;
		if (copy_to_user(buf, info->buf, r)) {
			r = -EFAULT;
			goto out;
		}
	} else {
		len = min(count, info->buflen);

		mutex_lock(&info->read_mutex);
		r = pn544_i2c_read(info->i2c_dev, info->buf, len);
		info->read_irq = PN544_NONE;
		mutex_unlock(&info->read_mutex);

		if (r < 0) {
			dev_err(&info->i2c_dev->dev, "read failed (%d)\n", r);
			goto out;
		}
		print_hex_dump(KERN_DEBUG, "read: ", DUMP_PREFIX_NONE,
			       16, 2, info->buf, r, false);

		*offset += r;
		if (copy_to_user(buf, info->buf, r)) {
			r = -EFAULT;
			goto out;
		}
	}

out:
	mutex_unlock(&info->mutex);

	return r;
}

static unsigned int pn544_poll(struct file *file, poll_table *wait)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;
	int r = 0;

	dev_dbg(&client->dev, "%s: info: %p\n", __func__, info);

	mutex_lock(&info->mutex);

	if (info->state == PN544_ST_COLD) {
		r = -ENODEV;
		goto out;
	}

	poll_wait(file, &info->read_wait, wait);

	if (pn544_irq_state(info) == PN544_INT) {
		r = POLLIN | POLLRDNORM;
		goto out;
	}
out:
	mutex_unlock(&info->mutex);

	return r;
}

static ssize_t pn544_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;
	ssize_t	len;
	int r;

	dev_dbg(&client->dev, "%s: info: %p, count %zu\n", __func__,
		info, count);

	mutex_lock(&info->mutex);

	if (info->state == PN544_ST_COLD) {
		r = -ENODEV;
		goto out;
	}

	/*
	 * XXX: should we detect rset-writes and clean possible
	 * read_irq state
	 */
	if (info->state == PN544_ST_FW_READY) {
		size_t fw_len;

		if (count < PN544_FW_HEADER_SIZE) {
			r = -EINVAL;
			goto out;
		}

		len = min(count, info->buflen);
		if (copy_from_user(info->buf, buf, len)) {
			r = -EFAULT;
			goto out;
		}

		print_hex_dump(KERN_DEBUG, "FW write: ", DUMP_PREFIX_NONE,
			       16, 2, info->buf, len, false);

		fw_len = PN544_FW_HEADER_SIZE + (info->buf[1] << 8) +
			info->buf[2];

		if (len > fw_len) /* 1 msg at a time */
			len = fw_len;

		r = pn544_fw_write(info->i2c_dev, info->buf, len);
	} else {
		if (count < PN544_LLC_MIN_SIZE) {
			r = -EINVAL;
			goto out;
		}

		len = min(count, info->buflen);
		if (copy_from_user(info->buf, buf, len)) {
			r = -EFAULT;
			goto out;
		}

		print_hex_dump(KERN_DEBUG, "write: ", DUMP_PREFIX_NONE,
			       16, 2, info->buf, len, false);

		if (len > (info->buf[0] + 1)) /* 1 msg at a time */
			len  = info->buf[0] + 1;

		r = pn544_i2c_write(info->i2c_dev, info->buf, len);
	}
out:
	mutex_unlock(&info->mutex);

	return r;

}

static long pn544_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;
	struct pn544_nfc_platform_data *pdata;
	unsigned int val;
	int r = 0;

	dev_dbg(&client->dev, "%s: info: %p, cmd: 0x%x\n", __func__, info, cmd);

	mutex_lock(&info->mutex);

	if (info->state == PN544_ST_COLD) {
		r = -ENODEV;
		goto out;
	}

	pdata = info->i2c_dev->dev.platform_data;
	switch (cmd) {
	case PN544_GET_FW_MODE:
		dev_dbg(&client->dev, "%s:  PN544_GET_FW_MODE\n", __func__);

		val = (info->state == PN544_ST_FW_READY);
		if (copy_to_user((void __user *)arg, &val, sizeof(val))) {
			r = -EFAULT;
			goto out;
		}

		break;

	case PN544_SET_FW_MODE:
		dev_dbg(&client->dev, "%s:  PN544_SET_FW_MODE\n", __func__);

		if (copy_from_user(&val, (void __user *)arg, sizeof(val))) {
			r = -EFAULT;
			goto out;
		}

		if (val) {
			if (info->state == PN544_ST_FW_READY)
				break;

			pn544_disable(info);
			r = pn544_enable(info, FW_MODE);
			if (r < 0)
				goto out;
		} else {
			if (info->state == PN544_ST_READY)
				break;
			pn544_disable(info);
			r = pn544_enable(info, HCI_MODE);
			if (r < 0)
				goto out;
		}
		file->f_pos = info->read_offset;
		break;

	case TCGETS:
		dev_dbg(&client->dev, "%s:  TCGETS\n", __func__);

		r = -ENOIOCTLCMD;
		break;

	default:
		dev_err(&client->dev, "Unknown ioctl 0x%x\n", cmd);
		r = -ENOIOCTLCMD;
		break;
	}

out:
	mutex_unlock(&info->mutex);

	return r;
}

static int pn544_open(struct inode *inode, struct file *file)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;
	int r = 0;

	dev_dbg(&client->dev, "%s: info: %p, client %p\n", __func__,
		info, info->i2c_dev);

	mutex_lock(&info->mutex);

	/*
	 * Only 1 at a time.
	 * XXX: maybe user (counter) would work better
	 */
	if (info->state != PN544_ST_COLD) {
		r = -EBUSY;
		goto out;
	}

	file->f_pos = info->read_offset;
	r = pn544_enable(info, HCI_MODE);

out:
	mutex_unlock(&info->mutex);
	return r;
}

static int pn544_close(struct inode *inode, struct file *file)
{
	struct pn544_info *info = container_of(file->private_data,
					       struct pn544_info, miscdev);
	struct i2c_client *client = info->i2c_dev;

	dev_dbg(&client->dev, "%s: info: %p, client %p\n",
		__func__, info, info->i2c_dev);

	mutex_lock(&info->mutex);
	pn544_disable(info);
	mutex_unlock(&info->mutex);

	return 0;
}

static const struct file_operations pn544_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= pn544_read,
	.write		= pn544_write,
	.poll		= pn544_poll,
	.open		= pn544_open,
	.release	= pn544_close,
	.unlocked_ioctl	= pn544_ioctl,
};

#ifdef CONFIG_PM
static int pn544_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pn544_info *info;
	int r = 0;

	dev_info(&client->dev, "***\n%s: client %p\n***\n", __func__, client);

	info = i2c_get_clientdata(client);
	dev_info(&client->dev, "%s: info: %p, client %p\n", __func__,
		 info, client);

	mutex_lock(&info->mutex);

	switch (info->state) {
	case PN544_ST_FW_READY:
		/* Do not suspend while upgrading FW, please! */
		r = -EPERM;
		break;

	case PN544_ST_READY:
		/*
		 * CHECK: Device should be in standby-mode. No way to check?
		 * Allowing low power mode for the regulator is potentially
		 * dangerous if pn544 does not go to suspension.
		 */
		break;

	case PN544_ST_COLD:
		break;
	};

	mutex_unlock(&info->mutex);
	return r;
}

static int pn544_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pn544_info *info = i2c_get_clientdata(client);
	int r = 0;

	dev_dbg(&client->dev, "%s: info: %p, client %p\n", __func__,
		info, client);

	mutex_lock(&info->mutex);

	switch (info->state) {
	case PN544_ST_READY:
		/*
		 * CHECK: If regulator low power mode is allowed in
		 * pn544_suspend, we should go back to normal mode
		 * here.
		 */
		break;

	case PN544_ST_COLD:
		break;

	case PN544_ST_FW_READY:
		break;
	};

	mutex_unlock(&info->mutex);

	return r;
}

static SIMPLE_DEV_PM_OPS(pn544_pm_ops, pn544_suspend, pn544_resume);
#endif

static struct device_attribute pn544_attr =
	__ATTR(nfc_test, S_IRUGO, pn544_test, NULL);

static int __devinit pn544_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct pn544_info *info;
	struct pn544_nfc_platform_data *pdata;
	int r = 0;

	dev_dbg(&client->dev, "%s\n", __func__);
	dev_dbg(&client->dev, "IRQ: %d\n", client->irq);

	/* private data allocation */
	info = kzalloc(sizeof(struct pn544_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev,
			"Cannot allocate memory for pn544_info.\n");
		r = -ENOMEM;
		goto err_info_alloc;
	}

	info->buflen = max(PN544_MSG_MAX_SIZE, PN544_MAX_I2C_TRANSFER);
	info->buf = kzalloc(info->buflen, GFP_KERNEL);
	if (!info->buf) {
		dev_err(&client->dev,
			"Cannot allocate memory for pn544_info->buf.\n");
		r = -ENOMEM;
		goto err_buf_alloc;
	}

	info->regs[0].supply = reg_vdd_io;
	info->regs[1].supply = reg_vbat;
	info->regs[2].supply = reg_vsim;
	r = regulator_bulk_get(&client->dev, ARRAY_SIZE(info->regs),
				 info->regs);
	if (r < 0)
		goto err_kmalloc;

	info->i2c_dev = client;
	info->state = PN544_ST_COLD;
	info->read_irq = PN544_NONE;
	mutex_init(&info->read_mutex);
	mutex_init(&info->mutex);
	init_waitqueue_head(&info->read_wait);
	i2c_set_clientdata(client, info);
	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "No platform data\n");
		r = -EINVAL;
		goto err_reg;
	}

	if (!pdata->request_resources) {
		dev_err(&client->dev, "request_resources() missing\n");
		r = -EINVAL;
		goto err_reg;
	}

	r = pdata->request_resources(client);
	if (r) {
		dev_err(&client->dev, "Cannot get platform resources\n");
		goto err_reg;
	}

	r = request_threaded_irq(client->irq, NULL, pn544_irq_thread_fn,
				 IRQF_TRIGGER_RISING, PN544_DRIVER_NAME,
				 info);
	if (r < 0) {
		dev_err(&client->dev, "Unable to register IRQ handler\n");
		goto err_res;
	}

	/* If we don't have the test we don't need the sysfs file */
	if (pdata->test) {
		r = device_create_file(&client->dev, &pn544_attr);
		if (r) {
			dev_err(&client->dev,
				"sysfs registration failed, error %d\n", r);
			goto err_irq;
		}
	}

	info->miscdev.minor = MISC_DYNAMIC_MINOR;
	info->miscdev.name = PN544_DRIVER_NAME;
	info->miscdev.fops = &pn544_fops;
	info->miscdev.parent = &client->dev;
	r = misc_register(&info->miscdev);
	if (r < 0) {
		dev_err(&client->dev, "Device registration failed\n");
		goto err_sysfs;
	}

	dev_dbg(&client->dev, "%s: info: %p, pdata %p, client %p\n",
		__func__, info, pdata, client);

	return 0;

err_sysfs:
	if (pdata->test)
		device_remove_file(&client->dev, &pn544_attr);
err_irq:
	free_irq(client->irq, info);
err_res:
	if (pdata->free_resources)
		pdata->free_resources();
err_reg:
	regulator_bulk_free(ARRAY_SIZE(info->regs), info->regs);
err_kmalloc:
	kfree(info->buf);
err_buf_alloc:
	kfree(info);
err_info_alloc:
	return r;
}

static __devexit int pn544_remove(struct i2c_client *client)
{
	struct pn544_info *info = i2c_get_clientdata(client);
	struct pn544_nfc_platform_data *pdata = client->dev.platform_data;

	dev_dbg(&client->dev, "%s\n", __func__);

	misc_deregister(&info->miscdev);
	if (pdata->test)
		device_remove_file(&client->dev, &pn544_attr);

	if (info->state != PN544_ST_COLD) {
		if (pdata->disable)
			pdata->disable();

		info->read_irq = PN544_NONE;
	}

	free_irq(client->irq, info);
	if (pdata->free_resources)
		pdata->free_resources();

	regulator_bulk_free(ARRAY_SIZE(info->regs), info->regs);
	kfree(info->buf);
	kfree(info);

	return 0;
}

static struct i2c_driver pn544_driver = {
	.driver = {
		.name = PN544_DRIVER_NAME,
#ifdef CONFIG_PM
		.pm = &pn544_pm_ops,
#endif
	},
	.probe = pn544_probe,
	.id_table = pn544_id_table,
	.remove = __devexit_p(pn544_remove),
};

static int __init pn544_init(void)
{
	int r;

	pr_debug(DRIVER_DESC ": %s\n", __func__);

	r = i2c_add_driver(&pn544_driver);
	if (r) {
		pr_err(PN544_DRIVER_NAME ": driver registration failed\n");
		return r;
	}

	return 0;
}

static void __exit pn544_exit(void)
{
	i2c_del_driver(&pn544_driver);
	pr_info(DRIVER_DESC ", Exiting.\n");
}

module_init(pn544_init);
module_exit(pn544_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
