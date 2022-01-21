// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 add device attr hdmirxsel.
 * V0.0X01.0X02 add device attr hdmiautoswitch.
 *
 */

// #define DEBUG
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/printk.h>
#include <linux/kobject.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x02)
#define DRIVER_NAME		"EP9461E"

/*control reg*/
#define RX_SIGNAL_DETECT	0x00
#define GENERAL_CONTROL		0x08
#define RX_SEL_CONTROL		0x09
#define EDID_ENABLE		0x0B
#define ENTER_CODE		0x07

/*control mask*/
#define MASK_RX0_SIGNAL		0x10
#define MASK_AUTO_SWITCH	0x40
#define MASK_CEC_SWITCH		0x20
#define MASK_POWER		0x80
#define MASK_RX_SEL		0x0f

struct ep9461e_dev {
	struct		device *dev;
	struct		miscdevice miscdev;
	struct		i2c_client *client;
	struct		mutex confctl_mutex;
	struct		timer_list timer;
	struct		delayed_work work_i2c_poll;
	bool		auto_switch_en;
	bool		power_up_chip_en;
	bool		cec_switch_en;
	bool		nosignal;
	u32		hdmi_rx_sel;
	int		err_cnt;
};

static struct ep9461e_dev *g_ep9461e;
static struct ep9461e_dev *ep9461e;

static void ep9461e_rx_select(struct ep9461e_dev *ep9461e);
static void ep9461e_rx_manual_select(struct ep9461e_dev *ep9461e);

static void i2c_wr(struct ep9461e_dev *ep9461e, u16 reg, u8 *val, u32 n)
{
	struct i2c_msg msg;
	struct i2c_client *client = ep9461e->client;
	int err;
	u8 data[128];

	data[0] = reg;
	memcpy(&data[1], val, n);
	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = data;
	msg.len = n + 1;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (err != 1) {
		dev_err(ep9461e->dev, "writing register 0x%x from 0x%x failed\n",
			reg, client->addr);
	} else {
		switch (n) {
		case 1:
			dev_dbg(ep9461e->dev, "I2C write 0x%02x = 0x%02x\n",
				reg, data[1]);
			break;
		case 2:
			dev_dbg(ep9461e->dev,
				"I2C write 0x%02x = 0x%02x%02x\n",
				reg, data[2], data[1]);
			break;
		case 4:
			dev_dbg(ep9461e->dev,
				"I2C write 0x%02x = 0x%02x%02x%02x%02x\n",
				reg, data[4], data[3], data[2], data[1]);
			break;
		default:
			dev_dbg(ep9461e->dev,
				"I2C write %d bytes from address 0x%02x\n",
				n, reg);
		}
	}
}

static void i2c_rd(struct ep9461e_dev *ep9461e, u16 reg, u8 *val, u32 n)
{
	struct i2c_msg msg[2];
	struct i2c_client *client = ep9461e->client;
	int err;
	u8 buf[1] = { reg };

	/*msg[0] addr to read*/
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = 1;

	/*msg[1] read data*/
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = n;

	err = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (err != ARRAY_SIZE(msg)) {
		dev_err(ep9461e->dev, "reading register 0x%x from 0x%x failed\n",
			reg, client->addr);
	}
}

static void i2c_rd8(struct ep9461e_dev *ep9461e, u16 reg, u8 *val)
{
	i2c_rd(ep9461e, reg, val, 1);
}

static void i2c_wr8(struct ep9461e_dev *ep9461e, u16 reg, u8 buf)
{
	i2c_wr(ep9461e, reg, &buf, 1);
}

static void i2c_wr8_and_or(struct ep9461e_dev *ep9461e, u16 reg, u32 mask,
			   u32 val)
{
	u8 val_p;

	i2c_rd8(ep9461e, reg, &val_p);
	i2c_wr8(ep9461e, reg, (val_p & mask) | val);
}

static long ep9461e_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	return 0;
}

static ssize_t ep9461e_write(struct file *file, const char __user *buf,
			     size_t size, loff_t *ppos)
{
	return 1;
}

static ssize_t ep9461e_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	return 1;
}

static ssize_t hdmirxsel_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ep9461e_dev *ep9461e = g_ep9461e;

	dev_info(ep9461e->dev, "%s: hdmi rx select state: %d\n",
			__func__, ep9461e->hdmi_rx_sel);

	return sprintf(buf, "%d\n", ep9461e->hdmi_rx_sel);
}

static ssize_t hdmirxsel_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct ep9461e_dev *ep9461e = g_ep9461e;
	u32 hdmirxstate = 0;
	int ret;

	ret = kstrtouint(buf, 10, &hdmirxstate);
	if (!ret) {
		dev_dbg(ep9461e->dev, "state: %d\n", hdmirxstate);
		ep9461e->hdmi_rx_sel = hdmirxstate;
		if (ep9461e->auto_switch_en | ep9461e->cec_switch_en)
			ep9461e->auto_switch_en = ep9461e->cec_switch_en = 0;
		ep9461e_rx_select(ep9461e);
	} else {
		dev_err(ep9461e->dev, "write hdmi_rx_sel failed!!!\n");
	}

	return count;
}

static ssize_t hdmiautoswitch_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ep9461e_dev *ep9461e = g_ep9461e;

	dev_info(ep9461e->dev, "hdmi rx select auto_switch state: %d\n",
				ep9461e->auto_switch_en);

	return sprintf(buf, "%d\n", ep9461e->auto_switch_en);
}

static ssize_t hdmiautoswitch_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct ep9461e_dev *ep9461e = g_ep9461e;
	u32 hdmiautoswitch = 0;
	int ret;

	ret = kstrtouint(buf, 10, &hdmiautoswitch);
	if (!ret) {
		dev_dbg(ep9461e->dev, "state: %d\n", hdmiautoswitch);
		ep9461e->auto_switch_en = hdmiautoswitch;
		ep9461e_rx_select(ep9461e);
	} else {
		dev_err(ep9461e->dev, "write hdmi auto switch failed!!!\n");
	}

	return count;
}

static DEVICE_ATTR_RW(hdmirxsel);
static DEVICE_ATTR_RW(hdmiautoswitch);

static inline bool detect_rx_signal(struct ep9461e_dev *ep9461e)
{
	u8 val;

	i2c_rd8(ep9461e, RX_SIGNAL_DETECT, &val);
	if (!(val & MASK_RX0_SIGNAL))
		return false;
	else
		return true;
}

static void ep9461e_init(struct ep9461e_dev *ep9461e)
{
	ep9461e->power_up_chip_en = false;
	ep9461e->auto_switch_en = false;
	ep9461e->hdmi_rx_sel = 0;
	ep9461e->err_cnt = 0;

	if (ep9461e->power_up_chip_en) {
		i2c_wr8_and_or(ep9461e, GENERAL_CONTROL, ~MASK_POWER,
			       MASK_POWER);
	}
	ep9461e_rx_select(ep9461e);
	schedule_delayed_work(&ep9461e->work_i2c_poll, msecs_to_jiffies(1000));
}

static void ep9461e_rx_manual_select(struct ep9461e_dev *ep9461e)
{
	i2c_wr8(ep9461e, RX_SEL_CONTROL, ep9461e->hdmi_rx_sel);
}

static void ep9461e_rx_select(struct ep9461e_dev *ep9461e)
{
	int ret;

	if (ep9461e->auto_switch_en) {
		i2c_wr8_and_or(ep9461e, GENERAL_CONTROL, ~MASK_AUTO_SWITCH,
			       MASK_AUTO_SWITCH);
	} else {
		ep9461e_rx_manual_select(ep9461e);
	}

	ret = detect_rx_signal(ep9461e);
	if (ret)
		dev_info(ep9461e->dev, "Detect HDMI RX valid signal!\n");
	else
		dev_err(ep9461e->dev, "HDMI RX has no valid signal!\n");
}


static void ep9461e_work_i2c_poll(struct work_struct *work)
{
	int ret;
	struct delayed_work *dwork = to_delayed_work(work);
	struct ep9461e_dev *ep9461e =
		container_of(dwork, struct ep9461e_dev, work_i2c_poll);

	ret = detect_rx_signal(ep9461e);
	if (!ret && (ep9461e->err_cnt < 10)) {
		ep9461e->err_cnt++;
		dev_err(ep9461e->dev,
			"ERROR: HDMI RX has no valid signal, err cnt: %d\n",
			ep9461e->err_cnt);
		if (ep9461e->err_cnt >= 10)
			dev_err(ep9461e->dev,
				"error count greater than 10, please check HDMIRX!");
	} else if (ret) {
		ep9461e->err_cnt = 0;
	}
	schedule_delayed_work(&ep9461e->work_i2c_poll, msecs_to_jiffies(1000));
}

static const struct file_operations ep9461e_fops = {
	.owner = THIS_MODULE,
	.read = ep9461e_read,
	.write = ep9461e_write,
	.unlocked_ioctl = ep9461e_ioctl,
};

struct miscdevice ep9461e_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ep9461e_dev",
	.fops = &ep9461e_fops,
};

static int ep9461e_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ep9461e_dev *ep9461e;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;
	dev_info(dev, "chip found @ 0x%x (%s)\n", client->addr << 1,
		client->adapter->name);

	ep9461e = devm_kzalloc(dev, sizeof(struct ep9461e_dev), GFP_KERNEL);
	if (!ep9461e)
		return -ENOMEM;

	ep9461e->client = client;
	ep9461e->dev = dev;
	client->flags |= I2C_CLIENT_SCCB;

	ret = misc_register(&ep9461e_miscdev);
	if (ret) {
		dev_err(ep9461e->dev,
			"EP9461E ERROR: could not register ep9461e device\n");
		return ret;
	}

	mutex_init(&ep9461e->confctl_mutex);
	ret = device_create_file(ep9461e_miscdev.this_device,
				&dev_attr_hdmirxsel);
	if (ret) {
		dev_err(ep9461e->dev, "failed to create attr hdmirxsel!\n");
		goto err1;
	}

	ret = device_create_file(ep9461e_miscdev.this_device,
				&dev_attr_hdmiautoswitch);
	if (ret) {
		dev_err(ep9461e->dev,
			"failed to create attr hdmiautoswitch!\n");
		goto err;
	}

	INIT_DELAYED_WORK(&ep9461e->work_i2c_poll, ep9461e_work_i2c_poll);

	ep9461e_init(ep9461e);
	g_ep9461e = ep9461e;

	dev_info(ep9461e->dev, "%s found @ 0x%x (%s)\n",
				client->name, client->addr << 1,
				client->adapter->name);

	return 0;

err:
	device_remove_file(ep9461e_miscdev.this_device,
			  &dev_attr_hdmirxsel);
err1:
	misc_deregister(&ep9461e_miscdev);
	return ret;
}

static int ep9461e_remove(struct i2c_client *client)
{
	cancel_delayed_work_sync(&ep9461e->work_i2c_poll);
	device_remove_file(ep9461e_miscdev.this_device,
			  &dev_attr_hdmirxsel);
	device_remove_file(ep9461e_miscdev.this_device,
			  &dev_attr_hdmiautoswitch);
	mutex_destroy(&ep9461e->confctl_mutex);
	misc_deregister(&ep9461e_miscdev);

	return 0;
}

static const struct of_device_id ep9461e_of_match[] = {
	{ .compatible = "semiconn,ep9461e" },
	{}
};
MODULE_DEVICE_TABLE(of, ep9461e_of_match);

static struct i2c_driver ep9461e_driver = {
	.probe = ep9461e_probe,
	.remove = ep9461e_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(ep9461e_of_match),
	},
};

static int __init ep9461e_driver_init(void)
{
	return i2c_add_driver(&ep9461e_driver);
}

static void __exit ep9461e_driver_exit(void)
{
	i2c_del_driver(&ep9461e_driver);
}

device_initcall_sync(ep9461e_driver_init);
module_exit(ep9461e_driver_exit);

MODULE_DESCRIPTION("semiconn EP9461E 4 HDMI in switch driver");
MODULE_AUTHOR("Jianwei Fan <jianwei.fan@rock-chips.com>");
MODULE_LICENSE("GPL v2");
