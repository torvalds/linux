// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 * 	   Wangqiang Guo<kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#define _SHOW_PRAGMA_MSG
#include "config.h"
// #include "platform.h"
#include "debug.h"
#include "it66353_drv.h"
#include "it66353_EQ.h"
#include "it66353.h"

/*
 * RK kernel follow
 */
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/delay.h>
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

//#define DEBUG_EN
#define DRIVER_VERSION	KERNEL_VERSION(0, 0x01, 0x00)
#define DRIVER_NAME		"IT66353"
#define DEBUG(...)\
		do {\
			if (debug_on)\
				printk(__VA_ARGS__);\
		} while (0)\
/*
 * ********* compile options ***********
 * CHECK_DEV_PRESENT:
 * 1: FW will restart when device ID check failed.
 */
#define CHECK_DEV_PRESENT 0
#define PR_IO(x)  { if (g_enable_io_log)  dev_dbg(g_it66353->dev, x); }

// ********* compile options end *******

#if DEBUG_FSM_CHANGE
#define __init_str_SYS_FSM_STATE
#include "IT66353_drv_h_str.h"
#endif

/*
 * for CEC
 * #if EN_CEC
 * #include "it66353_cec.h"
 * #include "..\Customer\IT6635_CecSys.h"
 * #endif
 * for CEC
 */

struct it66353_dev {
	struct		device *dev;
	struct		miscdevice miscdev;
	struct		i2c_client *client;
	struct		mutex confctl_mutex;
	struct		timer_list timer;
	struct		delayed_work work_i2c_poll;
	struct		mutex poll_lock;
	struct		task_struct *poll_task;
	bool		auto_switch_en;
	bool		is_chip_ready;
	bool		cec_switch_en;
	bool		nosignal;
	u8			tx_current_5v;
	u32			hdmi_rx_sel;
};
static struct it66353_dev *g_it66353;
static struct it66353_dev *it66353;

static u8 dev_state = DEV_FW_VAR_INIT;
IT6635_DEVICE_DATA it66353_gdev;
static int debug_on;
static void it66353_dev_loop(void);
static void _rx_set_hpd(u8 port, u8 hpd_value, u8 term_value);

/*
 * RK kernel
 */
static void i2c_wr(struct it66353_dev *it66353, u16 reg, u8 *val, u32 n)
{
	struct i2c_msg msg;
	struct i2c_client *client = it66353->client;
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
		dev_err(it66353->dev, "writing register 0x%x from 0x%x failed\n",
			reg, client->addr);
	} else {
		switch (n) {
		case 1:
			dev_dbg(it66353->dev, "I2C write 0x%02x = 0x%02x\n",
				reg, data[1]);
			break;
		case 2:
			dev_dbg(it66353->dev,
				"I2C write 0x%02x = 0x%02x%02x\n",
				reg, data[2], data[1]);
			break;
		case 4:
			dev_dbg(it66353->dev,
				"I2C write 0x%02x = 0x%02x%02x%02x%02x\n",
				reg, data[4], data[3], data[2], data[1]);
			break;
		default:
			dev_dbg(it66353->dev,
				"I2C write %d bytes from address 0x%02x\n",
				n, reg);
		}
	}
}

static void i2c_rd(struct it66353_dev *it66353, u16 reg, u8 *val, u32 n)
{
	struct i2c_msg msg[2];
	struct i2c_client *client = it66353->client;
	int err;
	u8 buf[1] = { reg };
	// msg[0] addr to read
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = 1;
	// msg[1] read data
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = n;
	err = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (err != ARRAY_SIZE(msg)) {
		dev_err(it66353->dev, "reading register 0x%x from 0x%x failed\n",
			reg, client->addr);
	}
}

static void i2c_rd8(u8 i2c_addr, u16 reg, u8 *val)
{
	struct it66353_dev *it66353 = g_it66353;

	it66353->client->addr = (i2c_addr >> 1);
	i2c_rd(it66353, reg, val, 1);
}

static void it66353_i2c_read(u8 i2c_addr, u16 reg, u8 n, u8 *val)
{
	struct it66353_dev *it66353 = g_it66353;

	it66353->client->addr = (i2c_addr >> 1);
	i2c_rd(it66353, reg, val, n);
}

static void i2c_wr8(u8 i2c_addr, u16 reg, u8 buf)
{
	struct it66353_dev *it66353 = g_it66353;

	it66353->client->addr = (i2c_addr >> 1);
	i2c_wr(it66353, reg, &buf, 1);
}

static u8 rx_rd8(u8 offset)
{
	u8 rd_data;

	i2c_rd8(it66353_gdev.opts.dev_opt->RxAddr, offset, &rd_data);
	return rd_data;
}

#if EN_CEC
static u8 cec_rd8(u8 offset)
{
	u8 rd_data;

	i2c_rd8(it66353_gdev.opts.dev_opt->CecAddr, offset, &rd_data);
	return rd_data;
}
#endif

static u8 sw_rd8(u8 offset)
{
	u8 rd_data;

	i2c_rd8(it66353_gdev.opts.dev_opt->SwAddr, offset, &rd_data);
	return rd_data;
}

static void swAddr_updata_bit(u16 reg, u32 mask, u32 val)
{
	u8 val_p;

	i2c_rd8(it66353_gdev.opts.dev_opt->SwAddr, reg, &val_p);
	val_p = (val_p & ((~mask) & 0xFF)) | (mask & val);
	i2c_wr8(it66353_gdev.opts.dev_opt->SwAddr, reg, val_p);
}

static void rxAddr_updata_bit(u16 reg, u32 mask, u32 val)
{
	u8 val_p;

	i2c_rd8(it66353_gdev.opts.dev_opt->RxAddr, reg, &val_p);
	val_p = (val_p & ((~mask) & 0xFF)) | (mask & val);
	i2c_wr8(it66353_gdev.opts.dev_opt->RxAddr, reg, val_p);
}

static void cecAddr_updata_bit(u16 reg, u32 mask, u32 val)
{
	u8 val_p;

	i2c_rd8(it66353_gdev.opts.dev_opt->CecAddr, reg, &val_p);
	val_p = (val_p & ((~mask) & 0xFF)) | (mask & val);
	i2c_wr8(it66353_gdev.opts.dev_opt->CecAddr, reg, val_p);
}

static long it66353_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	return 0;
}

static ssize_t it66353_write(struct file *file, const char __user *buf,
				 size_t size, loff_t *ppos)
{
	return 1;
}

static ssize_t it66353_read(struct file *file, char __user *buf, size_t size,
				loff_t *ppos)
{
	return 1;
}

static void it66353_work_i2c_poll(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct it66353_dev *it66353 =
		container_of(dwork, struct it66353_dev, work_i2c_poll);

	it66353_dev_loop();
	schedule_delayed_work(&it66353->work_i2c_poll, msecs_to_jiffies(50));
}

static ssize_t it66353_hdmirxsel_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct it66353_dev *it66353 = g_it66353;

	dev_info(it66353->dev, "%s: hdmi rx select state: %d\n",
		 __func__, g_it66353->hdmi_rx_sel);

	return sprintf(buf, "%d\n", g_it66353->hdmi_rx_sel);
}

static ssize_t it66353_hdmirxsel_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct it66353_dev *it66353 = g_it66353;
	u32 hdmirxstate = 0;
	int ret;

	ret = kstrtouint(buf, 10, &hdmirxstate);
	if (!ret && hdmirxstate >= 0 && hdmirxstate <= RX_PORT_COUNT) {
		it66353->hdmi_rx_sel = hdmirxstate;
		dev_info(it66353->dev, "%s: state: %d\n", __func__, hdmirxstate);
		/*
		 * _rx_set_hpd(hdmirxstate, 0, TERM_FOLLOW_HPD);
		 * msleep(200);
		 * _rx_set_hpd(hdmirxstate, 1, TERM_FOLLOW_HPD);
		 */
		it66353_set_active_port(hdmirxstate);
	} else {
		dev_info(it66353->dev, "%s: write hdmi_rx_sel failed!!!, hdmirxstate:%d \n",
						__func__, hdmirxstate);
	}

	return count;
}
static DEVICE_ATTR_RW(it66353_hdmirxsel);

static const struct file_operations it66353_fops = {
	.owner = THIS_MODULE,
	.read = it66353_read,
	.write = it66353_write,
	.unlocked_ioctl = it66353_ioctl,
};

static struct miscdevice it66353_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "it66353_dev",
	.fops = &it66353_fops,
};

static int it66353_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct it66353_dev *it66353;
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
	it66353 = devm_kzalloc(dev, sizeof(struct it66353_dev), GFP_KERNEL);
	if (!it66353)
		return -ENOMEM;

	it66353->client = client;
	it66353->dev = dev;
	client->flags |= I2C_CLIENT_SCCB;

	ret = misc_register(&it66353_miscdev);
	if (ret) {
		dev_err(it66353->dev,
			"it66353 ERROR: could not register it66353 device\n");
		return ret;
	}
	INIT_DELAYED_WORK(&it66353->work_i2c_poll, it66353_work_i2c_poll);

	g_it66353 = it66353;

	ret = device_create_file(it66353_miscdev.this_device,
				&dev_attr_it66353_hdmirxsel);
	if (ret) {
		dev_err(it66353->dev, "failed to create attr hdmirxsel!\n");
		return ret;
	}
	it66353_options_init();
	schedule_delayed_work(&it66353->work_i2c_poll, msecs_to_jiffies(10));
	dev_info(it66353->dev, "%s found @ 0x%x (%s)\n",
				client->name, client->addr << 1,
				client->adapter->name);
	return 0;
}

static int it66353_remove(struct i2c_client *client)
{
	cancel_delayed_work_sync(&it66353->work_i2c_poll);

	mutex_destroy(&it66353->confctl_mutex);
	misc_deregister(&it66353_miscdev);

	return 0;
}

static const struct of_device_id it66353_of_match[] = {
	{ .compatible = "ite,it66353" },
	{}
};
MODULE_DEVICE_TABLE(of, it66353_of_match);

static struct i2c_driver it66353_driver = {
	.probe = it66353_probe,
	.remove = it66353_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(it66353_of_match),
	},
};

static int __init it66353_driver_init(void)
{
	return i2c_add_driver(&it66353_driver);
}

static void __exit it66353_driver_exit(void)
{
	i2c_del_driver(&it66353_driver);
}

device_initcall_sync(it66353_driver_init);
module_exit(it66353_driver_exit);
MODULE_DESCRIPTION("ITE IT66353 3 HDMI in switch driver");
MODULE_AUTHOR("Wangqiang Guo <kay.guo@rock-chips.com>");
MODULE_LICENSE("GPL v2");
/*
 * end rk kernel
 */

static void _pr_buf(void *buffer, int length)
{
	int i;
	u8 *buf = (u8 *)buffer;
	u8 data = 0;
	int pr_len = 16;

	while (length) {
		if (length < pr_len)
			pr_len = length;
		for (i = 0; i < pr_len; i++) {
			data = *buf;
			DEBUG("%02x ", data);
			buf++;
		}
		DEBUG("\r\n");
		length -= pr_len;
	}
}

u8 it66353_h2swwr(u8 offset, u8 wdata)
{
	swAddr_updata_bit(offset, 0xFF, wdata);
	return 0;
}

u8 it66353_h2swrd(u8 offset)
{
	u8 rddata;

	rddata = sw_rd8(offset);
	return rddata;
}

u8 it66353_h2swset(u8 offset, u8 mask, u8 wdata)
{
	swAddr_updata_bit(offset, mask, wdata);
	return 0;
}

void it66353_h2swbrd(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (SWAddr >> 1);
		i2c_rd(g_it66353, offset, rddata, length);
	}
}

void it66353_h2swbwr(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (SWAddr >> 1);
		i2c_wr(g_it66353, offset, rddata, length);
	}
}

u8 it66353_h2rxedidwr(u8 offset, u8 *wrdata, u8 length)
{
	#if 0
	u8 i;
	for (i = 0; i < length; i++) {
		PR_IO("w %02x %02x %02x\r\n", offset+i, wrdata[i], RXEDIDAddr);
	}
	#endif
	g_it66353->client->addr = (RXEDIDAddr >> 1);
	i2c_wr(g_it66353, offset, wrdata, length);
	return 0;
}

static u8 it66353_h2rxedidrd(u8 offset, u8 *wrdata, u8 length)
{
	g_it66353->client->addr = (RXEDIDAddr >> 1);
	i2c_wr(g_it66353, offset, wrdata, length);
	return 0;
}

u8 it66353_h2rxwr(u8 offset, u8 rdata)
{
	rxAddr_updata_bit(offset, 0xFF, rdata);
	return 0;
}

u8 it66353_h2rxrd(u8 offset)
{
	u8 rddata;

	rddata = rx_rd8(offset);

	return rddata;
}

u8 it66353_h2rxset(u8 offset, u8 mask, u8 wdata)
{
	rxAddr_updata_bit(offset, mask, wdata);
	return 0;
}

void it66353_h2rxbrd(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (RXAddr >> 1);
		i2c_rd(g_it66353, offset, rddata, length);
	}
}

void it66353_h2rxbwr(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (RXAddr >> 1);
		i2c_wr(g_it66353, offset, rddata, length);
	}
}

#if EN_CEC

u8 it66353_cecwr(u8 offset, u8 wdata)
{
	return cecAddr_updata_bit(g_it66353, offset, 0xFF, wdata);
}

u8 cecrd(u8 offset)
{
	u8 rddata;

	rddata = cec_rd8(offset);

	return rddata;
}

u8 it66353_cecset(u8 offset, u8 mask, u8 rdata)
{
	return cecAddr_updata_bit(offset, mask, rddata);
}

void it66353_cecbrd(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (CecAddr >> 1);
		i2c_rd(g_it66353, offset, rddata, length);
	}
}

void it66353_cecbwr(u8 offset, u8 length, u8 *rddata)
{
	if (length > 0) {
		g_it66353->client->addr = (CecAddr >> 1);
		i2c_wr(g_it66353, offset, rddata, length);
	}
}

#endif

void it66353_chgrxbank(u8 bankno)
{
	it66353_h2rxset(0x0f, 0x07, bankno & 0x07);
}

static bool _tx_is_sink_hpd_high(void)
{
	if (it66353_h2swrd(0x11) & 0x20)
		return FALSE;
	else
		return TRUE;
}

static bool _tx_ddcwait(void)
{
	u8 ddcwaitcnt, ddc_status;

	ddcwaitcnt = 0;
	do {
		ddcwaitcnt++;
		msleep(DDCWAITTIME);
	} while ((it66353_h2swrd(0x1B) & 0x80) == 0x00 && ddcwaitcnt < DDCWAITNUM);

	if (ddcwaitcnt == DDCWAITNUM) {
		ddc_status = it66353_h2swrd(0x1B) & 0xFE;
		dev_err(g_it66353->dev, "** TX DDC Bus Sta=%02x\r\n", ddc_status);
		dev_err(g_it66353->dev, "** TX DDC Bus Wait TimeOut => ");

		if (it66353_h2swrd(0x27) & 0x80) {
			dev_err(g_it66353->dev, "** DDC Bus Hang\r\n");
			// Do not handle the DDC Bus Hang here
			// h2txwr(port, 0x2E, 0x0F);  // Abort DDC Command
			// h2txwr(port, 0x16, 0x08);  // Clear Interrupt
		} else if (ddc_status & 0x20) {
			dev_err(g_it66353->dev, "** DDC NoACK\r\n");
		} else if (ddc_status & 0x10) {
			dev_err(g_it66353->dev, "** DDC WaitBus\r\n");
		} else if (ddc_status & 0x08) {
			dev_err(g_it66353->dev, "** DDC ArbiLose\r\n");
		} else {
			dev_err(g_it66353->dev, "** UnKnown Issue\r\n");
		}

		return FALSE;
	} else {
		return TRUE;
	}
}

static u8 _tx_scdc_write(u8 offset, u8 wdata)
{
	int ddcwaitsts;
	u8 reg3C;

	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass) {
		dev_err(g_it66353->dev, "** EnRxDDCBypass:Abort SCDC write\r\n");
		return FALSE;
	}

	if ((it66353_h2swrd(0x11) & 0x20) == 0x00) {
		dev_err(g_it66353->dev, "** HPD-Low:Abort SCDC write\r\n");
		return FALSE;
	}

	reg3C = it66353_h2swrd(0x3C);
	it66353_h2swset(0x3C, 0x01, 0x01);		// Enable PC DDC Mode
	it66353_h2swwr(0x3D, 0x09);			// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0xA8);			// EDID Address
	it66353_h2swwr(0x3F, offset);			// EDID Offset
	it66353_h2swwr(0x40, 0x01);			// ByteNum[7:0]
	it66353_h2swwr(0x42, wdata);			// WrData
	it66353_h2swwr(0x3D, 0x01);			// Sequential Burst Write
	ddcwaitsts = _tx_ddcwait();
	it66353_h2swwr(0x3C, reg3C);			// Disable PC DDC Mode

	if (ddcwaitsts == 0) {
		DEBUG("SCDC wr %02x %02x, ddcwaitsts = %d\r\n",
		      offset, wdata, ddcwaitsts);
	}

	return ddcwaitsts;
}

static u8 __maybe_unused _tx_scdc_read(u8 offset, u8 *data_buf)
{
	int ddcwaitsts;
	u8 reg3C;

	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass) {
		dev_err(g_it66353->dev, "EnRxDDCBypass:Abort SCDC read\r\n");
		return FALSE;
	}

	if ((it66353_h2swrd(0x11) & 0x20) == 0x00) {
		dev_err(g_it66353->dev, "HPD-Low:Abort SCDC read\r\n");
		return FALSE;
	}

	reg3C = it66353_h2swrd(0x3C);
	it66353_h2swset(0x3C, 0x01, 0x01);		// Enable PC DDC Mode
	it66353_h2swwr(0x3D, 0x09);			// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0xA8);			// EDID Address
	it66353_h2swwr(0x3F, offset);			// EDID Offset
	it66353_h2swwr(0x40, 0x01);			// ByteNum[7:0]
	//it66353_h2swwr(0x42, data);			// WrData
	it66353_h2swwr(0x3D, 0x00);			// Sequential Burst Write
	ddcwaitsts = _tx_ddcwait();
	it66353_h2swwr(0x3C, reg3C);			// Disable PC DDC Mode

	if (ddcwaitsts == 0) {
		dev_err(g_it66353->dev, "SCDC rd %02x ddcwaitsts = %d\r\n",
			offset, ddcwaitsts);
	} else {
		*data_buf = it66353_h2swrd(0x42);
	}

	return ddcwaitsts;
}

static u8 __maybe_unused _tx_hdcp_write(u8 offset, u8 data)
{
	int ddcwaitsts;

	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass) {
		dev_err(g_it66353->dev, "EnRxDDCBypass:Abort HDCP write\r\n");
		return FALSE;
	}

	if ((it66353_h2swrd(0x11) & 0x20) == 0x00) {
		dev_err(g_it66353->dev, "HPD-Low:Abort HDCP write\r\n");
		return FALSE;
	}

	it66353_h2swset(0x3C, 0x01, 0x01);		// Enable PC DDC Mode
	it66353_h2swwr(0x3D, 0x09);			// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0x74);			// EDID Address
	it66353_h2swwr(0x3F, offset);			// EDID Offset
	it66353_h2swwr(0x40, 0x01);			// ByteNum[7:0]
	it66353_h2swwr(0x42, data);			// WrData
	it66353_h2swwr(0x3D, 0x01);			// Sequential Burst Write
	ddcwaitsts = _tx_ddcwait();
	it66353_h2swset(0x3C, 0x01, 0x00);		// Disable PC DDC Mode

	if (ddcwaitsts == 0) {
		DEBUG("SCDC wr %02x %02x, ddcwaitsts = %d\r\n", offset, data, ddcwaitsts);
	}

	return ddcwaitsts;
}

static u8 __maybe_unused _tx_hdcp_read(u8 offset, u8 *data_buf, u8 len)
{
	int ddcwaitsts;

	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass) {
		dev_err(g_it66353->dev, "EnRxDDCBypass:Abort HDCP read\r\n");
		return FALSE;
	}


	if ((it66353_h2swrd(0x11) & 0x20) == 0x00) {
		dev_err(g_it66353->dev, "HPD-Low:Abort HDCP read\r\n");
		return FALSE;
	}

	it66353_h2swset(0x3C, 0x01, 0x01);		// Enable PC DDC Mode
	it66353_h2swwr(0x3D, 0x09);			// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0x74);			// EDID Address
	it66353_h2swwr(0x3F, offset);			// EDID Offset
	it66353_h2swwr(0x40, len);			// ByteNum[7:0]
	// it66353_h2swwr(0x42, data);			// WrData
	it66353_h2swwr(0x3D, 0x00);			// Sequential Burst Write
	ddcwaitsts = _tx_ddcwait();
	it66353_h2swset(0x3C, 0x01, 0x00);		// Disable PC DDC Mode

	if (ddcwaitsts == 0) {
		dev_err(g_it66353->dev, "SCDC rd %02x ddcwaitsts = %d\r\n",
			offset, ddcwaitsts);
	} else {
		u8 i;

		DEBUG("HDCP read - %02X : ", offset);
		for (i = 0; i < len; i++) {
			data_buf[i] = it66353_h2swrd(0x42);
			DEBUG("%02X ", data_buf[i]);
		}

		DEBUG("\r\n");
	}

	return ddcwaitsts;
}

static u8 __maybe_unused _tx_scdc_read_ced(u8 *data_buf)
{
	int ddcwaitsts;
	u8 i;

	if ((it66353_h2swrd(0x11) & 0x20) == 0x00) {
		dev_err(g_it66353->dev, "HPD-Low:Abort SCDC read\r\n");
		return FALSE;
	}

	it66353_h2swset(0x3C, 0x01, 0x01);		// Enable PC DDC Mode
	it66353_h2swwr(0x3D, 0x09);			// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0xA8);			// EDID Address
	it66353_h2swwr(0x3F, 0x50);			// EDID Offset
	it66353_h2swwr(0x40, 0x06);			// ByteNum[7:0]
	// it66353_h2swwr(0x42, data);			// WrData
	it66353_h2swwr(0x3D, 0x00);			// Sequential Burst Write
	ddcwaitsts = _tx_ddcwait();
	it66353_h2swset(0x3C, 0x01, 0x00);		// Disable PC DDC Mode

	if (ddcwaitsts == 0) {
		dev_err(g_it66353->dev, "SCDC rd ced ddcwaitsts = %d\r\n", ddcwaitsts);
	} else {
		for (i = 0; i < 6; i++) {
			data_buf[i] = it66353_h2swrd(0x42);
		}
	}

	return ddcwaitsts;
}

static void _tx_power_down(void)
{
	if (it66353_gdev.opts.dev_opt->DoTxPowerDown) {
		it66353_h2swset(0xD3, 0x80, 0x00);
		it66353_h2swset(0xD1, 0x60, 0x60);
	}
}

static void _tx_power_on(void)
{
	it66353_h2swset(0xD3, 0x80, 0x80);	// Reg_XP_ALPWDB=1
	it66353_h2swset(0xD1, 0x60, 0x00);	// Reg_XP_PWDi = 0, Reg_XP_PWDPLL=0
}

static void __maybe_unused _tx_show_sink_ced(void)
{
	u8 ced_valid;
	u8 i;
	u8 pr_ced = 0;
	u8 ced_value[6];
	// static u8 read_from_scdc;

	ced_valid = it66353_h2swrd(0xB0);
	if (ced_valid) {
		DEBUG("Begin READ CED:\r\n");
		pr_ced = 1;
		for (i = 0; i < 6; i++) {
			if (ced_valid & (0x01 << i)) {	// 0x5? error status is valid
				it66353_h2swset(0xAC, 0xE0, (i<<5));	// offset select
				ced_value[i] = it66353_h2swrd(0xB1);
			}
		}
		it66353_h2swwr(0xAD, 0xFF); // clear CED valid on 0xB0
	} else {
		#if 0
		if (read_from_scdc > 10) {
			#if 0
			for (i = 0; i < 6; i++) {
				_tx_scdc_read(0x50 + i, &ced_value[i]);

			}
			#else
			_tx_scdc_read_ced(&ced_value[0]);
			#endif

			dev_info("SCDC: ");
			pr_ced = 1;
			read_from_scdc = 0;
		} else {
			read_from_scdc++;
		}
		#else
		// read_from_scdc = read_from_scdc;	// suppress warning
		#endif
	}

	if (pr_ced) {
		for (i = 0; i < 3; i++) {
			DEBUG("ced_valid = %02X, ch%d V=%d err=%04X\r\n",
				ced_valid, i, (ced_value[2*i+1]>>7)&0x01,
				((ced_value[2*i+1]&0xEF)<<8) + ced_value[2*i]);
		}
		DEBUG("\r\n");
	}
}

static void __maybe_unused _tx_ovwr_hdmi_clk(u8 ratio)
{
	switch (ratio) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0xB2, 0x03, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0xB2, 0x03, 0x01);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0xB2, 0x03, 0x03);
		break;
	default:
		break;
	}
}

static void __maybe_unused _tx_ovwr_h20_scrb(u8 scrb)
{
	switch (scrb) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0xB2, 0x0C, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0xB2, 0x0C, 0x08);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0xB2, 0x0C, 0x0c);
		break;
	default:
		break;
	}
}


u8 it66353_rx_is_ch_symlock(u8 ch)
{
	if ((it66353_h2rxrd(0x14) & (0x08 << ch))) {
		return 1;
	}
	return 0;
}


static void __maybe_unused it66353it66353_rx_ovwr_hdmi_clk(u8 port, u8 ratio)
{
	switch (ratio) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0x51 + port, 0x28, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0x51 + port, 0x28, 0x20);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0x51 + port, 0x28, 0x28);
		break;
	default:
		break;
	}
}

static void __maybe_unused it66353it66353_rx_ovwr_h20_scrb(u8 port, u8 scrb)
{
	switch (scrb) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0x51 + port, 0x30, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0x51 + port, 0x30, 0x20);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0x51 + port, 0x30, 0x30);
		break;
	default:
		break;
	}
}


static void it66353_sw_config_timer0(u8 count)
{
	// init timer = count[6:0] * 10 ms
	it66353_h2swwr(0x1C, count);
}

static void it66353it66353_sw_enable_timer0(void)
{
	it66353_h2swset(0x38, 0x02, 0x02);
}

static void _sw_clear_timer0_interrupt(void)
{
	it66353_h2swset(0x28, 0x02, 0x02);
}

static void __maybe_unused _sw_enable_txoe_timer_check(void)
{
	it66353_sw_disable_timer0();
	it66353_sw_config_timer0(45);		// 450ms time out
	_sw_clear_timer0_interrupt();
	it66353it66353_sw_enable_timer0();
}

static void __maybe_unused _sw_disable_txoe_timer_check(void)
{
	it66353_sw_disable_timer0();
}

static void __maybe_unused _sw_show_hdcp_status(void)
{
	u8 hdcp_sts;
	if (it66353_gdev.vars.Rev >= 0xC0) {
		hdcp_sts = it66353_h2swrd(0xB3);
		if (hdcp_sts & BIT(5)) {
			DEBUG("HDCP 2 done\r\n");
			it66353_sw_clear_hdcp_status();
		}
		if (hdcp_sts & BIT(6)) {
			DEBUG("HDCP 1 done\r\n");
			it66353_sw_clear_hdcp_status();
		}
	}
}

u8 it66353_get_port_info1(u8 port, u8 info)
{
	u8 tmp;
	tmp = it66353_h2swrd(0x61 + port * 3);

	if ((tmp & info) == info) {
		return 1;
	} else {
		return 0;
	}
}

static void _tx_ovwr_hdmi_mode(u8 mode)
{
	switch (mode) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0xB2, 0x0F, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0xB2, 0x0F, 0x05);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0xB2, 0x0F, 0x0F);
		break;
	default:
		break;
	}
}

static void _tx_setup_afe(u32 vclk)
{
	u8 H2ON_PLL, DRV_TERMON, DRV_RTERM, DRV_ISW, DRV_ISWC, DRV_TPRE, DRV_NOPE, H2ClkRatio;
	u8 DRV_PISW, DRV_PISWC, DRV_HS;

	// vclk = 340000UL;
	DEBUG("_tx_setup_afe %u\r\n", vclk);
	// it66353_h2rxset(0x23, 0x04, 0x04);

	if (vclk > 100000UL)  {			// IP_VCLK05 > 50MHz
		it66353_h2swset(0xD1, 0x07, 0x04);
	} else {
		it66353_h2swset(0xD1, 0x07, 0x03);
	}

	if (vclk > 162000UL) {			// IP_VCLK05 > 81MHz
		// it66353_h2swset(0xD4, 0x04, 0x04);
		DRV_HS = 1;
	} else {
		DRV_HS = 0;
	}

	it66353_h2swset(0xd8, 0xf0, 0x00);
	if (vclk > 300000UL) {			// single-end swing = 520mV
		DRV_TERMON = 1;
		DRV_RTERM = 0x5;
		DRV_ISW = 0x0E;
		DRV_ISWC = 0x0B;
		DRV_TPRE = 0x0;
		DRV_NOPE = 0;
		H2ON_PLL = 1;
		DRV_PISW = 1;
		DRV_PISWC = 1;
		it66353_h2swset(0xd8, 0xf0, 0x30);
	} else if (vclk > 100000UL) {	// single-end swing = 450mV
		DRV_TERMON = 1;
		DRV_RTERM = 0x1;
		DRV_ISW = 0x9;
		DRV_ISWC = 0x9;
		DRV_TPRE = 0;
		DRV_NOPE = 1;
		H2ON_PLL = 0;
		DRV_PISW = 1;
		DRV_PISWC = 1;
	} else {			// single-end swing = 500mV
		DRV_TERMON = 0;
		DRV_RTERM = 0x0;
		DRV_ISW = 0x3;
		DRV_ISWC = 0x3;
		DRV_TPRE = 0;
		DRV_NOPE = 1;
		H2ON_PLL = 0;
		DRV_PISW = 1;
		DRV_PISWC = 1;
	}

	it66353_h2swset(0xD0, 0x08, (H2ON_PLL << 3));
	it66353_h2swset(0xD3, 0x1F, DRV_ISW);
	it66353_h2swset(0xD4, 0xF4, (DRV_PISWC << 6)+(DRV_PISW << 4)+(DRV_HS << 2));
	it66353_h2swset(0xD5, 0xBF, (DRV_NOPE << 7) + (DRV_TERMON << 5) + DRV_RTERM);
	it66353_h2swset(0xD7, 0x1F, DRV_ISWC);
	it66353_h2swset(0xD6, 0x0F, DRV_TPRE);

	H2ClkRatio = (it66353_h2swrd(0xb2) & 0x02) >> 1;// RegH2ClkRatio from TX

	DEBUG("TX Output H2ClkRatio=%d ...\r\n", H2ClkRatio);

	// msleep(10);
	// it66353_h2rxset(0x23, 0x04, 0x00);
}

static u8 _rx_calc_edid_sum(u8 *edid)
{
	u8 i;
	u16 sum = 0x100;

	for (i = 0; i < 127; i++) {
		sum = sum - edid[i];
	}
	return (sum & 0xFF);
}

void it66353_rx_caof_init(u8 port)
{
	u8 reg08;
	u8 failcnt;

	it66353_h2swset(0x05, 0x01, 0x01);
	it66353_h2swset(0x59 + port, 0x20, 0x20);	// new at IT6635B0
	it66353_h2swset(0x05 + port, 0x01, 0x01);	// IPLL RST, it6635
	it66353_rx_auto_power_down_enable(port, 0);
	it66353_rx_term_power_down(port, 0x00);		// disable PWD CHx termination

	msleep(1);
	it66353_chgrxbank(3);
	it66353_h2rxset(0x3A, 0x80, 0x00);		// Reg_CAOFTrg low
	it66353_h2rxset(0xA0, 0x80, 0x80);
	it66353_h2rxset(0xA1, 0x80, 0x80);
	it66353_h2rxset(0xA2, 0x80, 0x80);
	it66353_chgrxbank(0);

	it66353_h2rxset(0x2A, 0x41, 0x41);		// CAOF RST and CAOFCLK inversion
	msleep(1);
	it66353_h2rxset(0x2A, 0x40, 0x00);		// deassert CAOF RST
	it66353_h2rxwr(0x25, 0x00);			// Disable AFE PWD
	it66353_h2rxset(0x3C, 0x10, 0x00);		// disable PLLBufRst

	it66353_chgrxbank(3);
	it66353_h2rxset(0x3B, 0xC0, 0x00);		// Reg_ENSOF, Reg_ENCAOF
	it66353_h2rxset(0x48, 0x80, 0x80);		// for read back sof value registers
	msleep(10);
	it66353_h2rxset(0x3A, 0x80, 0x80);		// Reg_CAOFTrg high

	// wait for INT Done
	it66353_chgrxbank(0);
	reg08 = 0;
	failcnt = 0;

	while (reg08 == 0x00) {
		reg08 = it66353_h2rxrd(0x08) & 0x10;

		if (reg08 == 0) {
			failcnt++;
			if (failcnt >= 10) {
				dev_err(g_it66353->dev, "ERROR: CAOF fail !!!\r\n");

				it66353_chgrxbank(3);
				it66353_h2rxset(0x3A, 0x80, 0x00);// disable CAOF_Trig
				it66353_chgrxbank(0);
				it66353_h2rxset(0x2A, 0x40, 0x40);// reset CAOF when caof fail
				it66353_h2rxset(0x2A, 0x40, 0x00);
				break;
			}
		}

		msleep(2);
	}


	it66353_chgrxbank(3);
	it66353_h2rxset(0x48, 0x80, 0x80);

	DEBUG("CAOF_Int=%02x, Status=%02x\r\n\r\n",
		   (it66353_h2rxrd(0x59) & 0xC0),
		   ((it66353_h2rxrd(0x5A) << 4) + (it66353_h2rxrd(0x59) & 0x0F)));
	it66353_chgrxbank(0);

	it66353_h2swset(0x59+port, 0x20, 0x00);
	it66353_h2swset(0x05+port, 0x01, 0x00);
	it66353_h2swset(0x05, 0x01, 0x00);

	it66353_h2rxset(0x08, 0x30, 0x30);
	it66353_h2rxset(0x3C, 0x10, 0x10);

	it66353_chgrxbank(3);
	it66353_h2rxset(0x3A, 0x80, 0x00);	// Reg_CAOFTrg low
	it66353_h2rxset(0xA0, 0x80, 0x00);
	it66353_h2rxset(0xA1, 0x80, 0x00);
	it66353_h2rxset(0xA2, 0x80, 0x00);
	it66353_chgrxbank(0);

	it66353_rx_auto_power_down_enable(port, it66353_gdev.opts.dev_opt->RxAutoPowerDown);
}

static void _rx_show_ced_info(void)
{
	u8 symlock = (it66353_h2rxrd(0x14) & 0x38) >> 3;
	u8 ch;

	if (0x38 != symlock) {
		DEBUG("symlock = %02x\r\n", symlock);
	} else {
		for (ch = 0; ch < 3; ch++) {
			if (it66353_gdev.vars.RxCEDErrValid & (0x01 << ch)) {
				DEBUG("ch_%d CED=0x%04x\r\n", ch, it66353_gdev.vars.RxCEDErr[ch]);
			} else {
				DEBUG("ch_%d CED=invalid\r\n", ch);
			}
		}
	}
}

static void _rx_setup_afe(u32 vclk)
{
	it66353_chgrxbank(3);

	if (vclk >= (1024UL * 102UL)) {
		it66353_h2rxset(0xA7, 0x40, 0x40);
	} else {
		it66353_h2rxset(0xA7, 0x40, 0x00);
	}
	it66353_chgrxbank(0);
}

static u8 _rx_is_any_ch_symlock(void)
{
	if ((it66353_h2rxrd(0x14) & 0x38)) {
		return 1;
	}
	return 0;
}

u8 it66353_rx_is_all_ch_symlock(void)
{
	if ((it66353_h2rxrd(0x14) & 0x38) == 0x38) {
		DBG_SYMLOCK_1();
		// it66353_txoe(1);
		return 1;
	}
	DBG_SYMLOCK_0();
	return 0;
}

static bool _rx_is_5v_active(void)
{
	return (it66353_h2rxrd(0x13) & 0x01);
}

u8 it66353_rx_is_clock_stable(void)
{
	if (it66353_get_port_info0(it66353_gdev.vars.Rx_active_port,
				  (PI_CLK_STABLE | PI_CLK_VALID | PI_5V))) {
		DBG_CLKSTABLE_1();
		return 1;
	} else {
		DBG_CLKSTABLE_0();
		return 0;
	}
}

#if EN_AUTO_RS
static u8 _rx_need_hpd_toggle(void)
{
	u8 hdcp_sts;
	if (it66353_gdev.vars.Rev >= 0xC0) {
		hdcp_sts = it66353_h2swrd(0xB3);
		if (hdcp_sts & BIT(5)) {
			dev_info(g_it66353->dev, "HDCP 2 done\r\n");
			return 1;
		}
		if (hdcp_sts & BIT(6)) {
			dev_info(g_it66353->dev, "HDCP 1 done\r\n");
			return 1;
		}
		if (hdcp_sts & BIT(7)) {
			dev_info(g_it66353->dev, "HDCP acc\r\n");
			// return 0;
		}
	} else {
		if (it66353_sw_get_timer0_interrupt()) {
			dev_info(g_it66353->dev, "TXOE timeout 2\r\n");
			return 1;
		}
	}

	return 0;

	#if 0
	// todo: need more information
	return 1;
	#endif
}
#endif

static void _rx_int_enable(void)
{
/*
 * Set RX Interrupt Enable
 */
	it66353_h2rxwr(0x53, 0xFF);		// Enable RxIntEn[7:0]
	it66353_h2rxwr(0x54, 0xFF);		// Enable RxIntEn[15:8]
	it66353_h2rxwr(0x55, 0xFF);		// Enable RxIntEn[23:16]
	it66353_h2rxwr(0x56, 0xFF);		// Enable RxIntEn[31:24]
	it66353_h2rxwr(0x57, 0xFF);		// Enable RxIntEn[39:32]
	it66353_h2rxwr(0x5D, 0xF7);		// Enable BKIntEn[7:0], but timer int
	it66353_h2rxwr(0x5E, 0xFF);		// Enable BKIntEn[15:8]
	it66353_h2rxwr(0x5F, 0xFF);		// Enable BKIntEn[23:16]
	it66353_h2rxset(0x60, 0x20, 0x20);	// RegEnIntOut=1
}

static void _rx_wdog_rst(u8 port)
{
	#if 0
	u8 mask;
	mask = (0x10 << port) | (1 << port);
	it66353_h2swset(0x16, mask, mask);
	msleep(1);
	it66353_h2swset(0x16, mask, 0x00);
	#else
	it66353_h2swset(0x2b, 0x01, 0x00);
	msleep(2);
	// it66353_h2swwr(0x20 + port * 2, 0x7C);// clear clock related interrupt
	it66353_h2swset(0x2b, 0x01, 0x01);
	// it66353_h2swwr(0x20 + port * 2, 0x04);
	#endif
}

static void _rx_ovwr_hdmi_mode(u8 port, u8 mode)
{
	switch (mode) {
	case HDMI_MODE_AUTO:
		it66353_h2swset(0x51 + port, 0x38, 0x00);
		it66353_h2swset(0x98 + port, 0xC0, 0x00);
		break;
	case HDMI_MODE_14:
		it66353_h2swset(0x51 + port, 0x38, 0x20);
		it66353_h2swset(0x98 + port, 0xC0, 0x00);
		break;
	case HDMI_MODE_20:
		it66353_h2swset(0x51 + port, 0x38, 0x38);
		it66353_h2swset(0x98 + port, 0xC0, 0xC0);
		break;
	}
}


static void _rx_set_hpd(u8 port, u8 hpd_value, u8 term_value)
{
	if (port < RX_PORT_COUNT) {
		switch (term_value) {
		case TERM_LOW:
			term_value = 0xFF;
			break;
		case TERM_HIGH:
			term_value = 0x00;
			break;
		case TERM_FOLLOW_TX:
			if (it66353_h2swrd(0x11) & 0x40)
				term_value = 0x00;
			else
				term_value = 0xFF;
			break;
		case TERM_FOLLOW_HPD:
		default:
			if (hpd_value) {
				term_value = 0x00;
			} else {
				term_value = 0xFF;
			}
			break;
		}

		// if (it66353_gdev.vars.RxHPDFlag[port] != value)
		// {
			it66353_gdev.vars.RxHPDFlag[port] = hpd_value;
			if (hpd_value) {
				if (it66353_gdev.vars.Rx_active_port == port) {
					DBG_TM(RX_HPD_HIGH);

					if (it66353_gdev.opts.rx_opt[port]->EnRxDDCBypass == 0) {
						it66353_h2swset(0x3C, 0x01, 0x01);
						msleep(1);
						it66353_h2swset(0x3C, 0x01, 0x00);
					}
				}

				if (it66353_gdev.opts.rx_opt[port]->DisableEdidRam == 0) {
					_rx_edid_ram_enable(port);
				}

				if (it66353_gdev.opts.rx_opt[port]->HPDOutputInverse) {
					it66353_h2swset(0x4C + port, 0xC0, 0x40);// RXHPD=0
				} else {
					it66353_h2swset(0x4C + port, 0xC0, 0xC0);// RXHPD=1
				}


				#if 0
				if (it66353_gdev.vars.Rx_active_port == port) {
					// term power down = 0
					it66353_rx_term_power_down(port, 0x7e);
				} else {
					#if NON_ACTIVE_PORT_DETECT_CLOCK
					// term power down = 0
					it66353_rx_term_power_down(port, 0x7e);
					#else
					// term power down = 0
					it66353_rx_term_power_down(port, 0xFF);
					#endif
				}
				#else
				it66353_rx_term_power_down(port, term_value);
				#endif
			} else {
				if (it66353_gdev.vars.Rx_active_port == port) {
					DBG_TM(RX_HPD_LOW);
				}

				_rx_edid_ram_disable(port);

				it66353_rx_term_power_down(port, term_value);

				if (it66353_gdev.opts.rx_opt[port]->HPDOutputInverse) {
					it66353_h2swset(0x4C + port, 0xC0, 0xC0);// RXHPD=1
				} else {
					it66353_h2swset(0x4C + port, 0xC0, 0x40);// RXHPD=0
				}

				if (port == it66353_gdev.vars.Rx_active_port) {
					it66353_h2swset(0xB2, 0x0A, 0x0A); // clear H2Mode
				}
			}
			dev_info(g_it66353->dev, "Set RxP%d HPD = %d %02x\r\n",
				 (int)port, (int)hpd_value, (int)term_value);
		// }
	} else {
		dev_err(g_it66353->dev, "Invaild port %d\r\n", port);
	}
}

static void _rx_set_hpd_all(u8 value)
{
	u8 i;

	for (i = 0; i < RX_PORT_COUNT; i++) {
		_rx_set_hpd(i, value, TERM_FOLLOW_HPD);
	}
}

static void _rx_set_hpd_with_5v_all(u8 non_active_port_only)
{
	u8 i;
	for (i = 0; i < RX_PORT_COUNT; i++) {
		if (non_active_port_only) {
			if (it66353_gdev.vars.Rx_active_port == i) {
				continue;
			}
		}

		if (it66353_gdev.opts.rx_opt[i]->NonActivePortReplyHPD) {
			if (it66353_get_port_info0(i, PI_5V)) {
				_rx_set_hpd(i, 1, TERM_FOLLOW_HPD);
			} else {
				_rx_set_hpd(i, 0, TERM_FOLLOW_HPD);
			}
		}
	}
}

static u8 _rx_get_all_port_5v(void)
{
	u8 i;
	u8 ret = 0;
	for (i = 0; i < RX_PORT_COUNT; i++) {
		if (it66353_get_port_info0(i, PI_5V)) {
			ret |= (1 << i);
		}
	}

	return ret;
}

static void __maybe_unused it66353it66353_rx_handle_output_err(void)
{
#if EN_AUTO_RS
	if (it66353_gdev.opts.active_rx_opt->EnableAutoEQ) {
		if (it66353_gdev.vars.try_fixed_EQ) {
			dev_info(g_it66353->dev, "*** fixed EQ fail\r\n");
			it66353_gdev.vars.try_fixed_EQ = 0;
			it66353_eq_reset_txoe_ready();
			it66353_eq_reset_state();
			it66353_fsm_chg(RX_CHECK_EQ);
		} else {
			it66353_auto_eq_adjust();
		}
	}
#endif
}

void it66353_rx_auto_power_down_enable(u8 port, u8 enable)
{
	if (enable) {
		/*
		 * //will auto power down D0~D2 3.3V
		 * it66353_h2swset(0x90 + port, 0x3D, 0x3D);
		 * // will not power down D0~D2 3.3V
		 * it66353_h2swset(0x90 + port, 0x3D, 0x1D);
		 */
		it66353_h2swset(0x90 + port, 0x3D, 0x1D);
	} else {
		it66353_h2swset(0x90 + port, 0x3D, 0x00);
	}
}

static void it66353_rx_auto_power_down_enable_all(u8 enable)
{
	u8 i;
	for (i = 0; i < RX_PORT_COUNT; i++) {
		it66353_rx_auto_power_down_enable(i, enable);
	}
}

void it66353_rx_term_power_down(u8 port, u8 channel)
{
	// to detect clock,
	// 0x88[7][0] must be '0','0';
	it66353_h2swset(0x88 + port, 0xFF, channel);
}


static void _sw_int_enable(u8 port, u8 enable)
{
	if (enable) {
		// Enable Switch RX Port Interrupt
		it66353_h2swwr(0x30 + port * 2, 0xff);
		it66353_h2swset(0x31 + port * 2, 0x01, 0x01);
	} else {
		// Disable Switch RX Port Interrupt
		it66353_h2swwr(0x30 + port * 2, 0x00);
		it66353_h2swset(0x31 + port * 2, 0x01, 0x00);
		it66353_h2swwr(0x20 + port * 2, 0xff);
		it66353_h2swwr(0x21 + port * 2, 0xff);
	}

}

static void _sw_int_enable_all(u8 enable)
{
	u8 i;
	for (i = 0; i < RX_PORT_COUNT; i++) {
		_sw_int_enable(i, enable);
	}
}

void it66353_sw_disable_timer0(void)
{
	// disable timer will also clear timer interrupt flag
	it66353_h2swset(0x38, 0x02, 0x00);
}

#if EN_AUTO_RS
u8 it66353_sw_get_timer0_interrupt(void)
{
	return ((it66353_h2swrd(0x28)&0x02));
}
#endif


static void _sw_config_timer1(u8 count)
{
	// init timer = count[6:0] * 10 ms
	// init timer = BIT7|count[6:0] * 100 ms
	it66353_h2swwr(0x1D, count);
}

static void _sw_enable_timer1(void)
{
	it66353_h2swset(0x38, 0x04, 0x04);
}

static void _sw_disable_timer1(void)
{
	it66353_h2swset(0x38, 0x04, 0x00);
}

static u8 _sw_get_timer1_interrupt(void)
{
	return ((it66353_h2swrd(0x28)&0x04));
}

static void _sw_clear_timer1_interrupt(void)
{
	it66353_h2swset(0x28, 0x04, 0x04);
}

static void _sw_enable_hpd_toggle_timer(u8 timeout)
{
	// init timer = count[6:0] * 10 ms
	// init timer = BIT7|count[6:0] * 100 ms
	_sw_config_timer1(timeout);		// HPT toggle time out

	_sw_clear_timer1_interrupt();
	_sw_enable_timer1();
}

static void _sw_disable_hpd_toggle_timer(void)
{
	_sw_disable_timer1();
}

static u8 _sw_check_hpd_toggle_timer(void)
{
	return _sw_get_timer1_interrupt();
}

static void _sw_reset_scdc_monitor(void)
{
	it66353_h2swwr(0xAD, 0xFF);
}

static void _sw_monitor_and_fix_scdc_write(void)
{
	u8 reg;

	reg = it66353_h2swrd(0xAD);
	if (reg & 0x10) {		// P0SCDCWrReg20hVld
		dev_info(g_it66353->dev, "## src SCDC wr %02x\r\n", reg);
		if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_20) {
			if ((reg&0x03) != 0x03) {
				_tx_scdc_write(0x20, 0x03);
			}
		} else if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_14) {
			if ((reg&0x03) != 0x00) {
				_tx_scdc_write(0x20, 0x00);
			}
		}
		_sw_reset_scdc_monitor();
	}
}

void it66353_sw_clear_hdcp_status(void)
{
	it66353_h2swwr(0xB0, 0xC0);
}

static void _sw_sdi_check(void)
{
	u8 port;
	u8 reg6C, reg70;
	port = it66353_gdev.vars.Rx_active_port;

	if (it66353_gdev.vars.sdi_stable_count < 8) {
		if (it66353_get_port_info0(port, PI_CLK_STABLE)) {
			it66353_gdev.vars.sdi_stable_count++;
		} else {
			it66353_gdev.vars.sdi_stable_count = 0;
		}
	} else {
		// perform check
		it66353_gdev.vars.sdi_stable_count = 0;
		reg6C = it66353_h2swrd(0x6c + port);
		reg70 = it66353_h2swrd(0x70 + port);

		if (reg70 & BIT(3)) {
			reg6C = reg6C/8;
		} else if (reg70 & BIT(2)) {
			reg6C = reg6C/4;
		} else if (reg70 & BIT(1)) {
			reg6C = reg6C/2;
		} else {
			// reg6C = reg6C/1;
		}

		if (reg6C < 22) {
			reg70 = it66353_h2swrd(0x61 + port * 3);
			if (0 == (reg70 & BIT(1))) {
				// need re-calculate RDetIPLL_HS1P48G
				reg70 = 1 << port;
				it66353_h2swset(0x2A, reg70, reg70);
				DEBUG("check_for_sdi recheck ...\r\n");
			} else {
				it66353_gdev.vars.check_for_sdi = 0;
				DEBUG("check_for_sdi disabled ...%02x\r\n",
				      it66353_h2rxrd(0x13));
			}
		} else {
			it66353_gdev.vars.check_for_sdi = 0;
		}
	}
}

static void _sw_hdcp_access_enable(u8 enable)
{
	if (it66353_gdev.vars.spmon == 2) {
		DEBUG("  >> skip HDCP acc %d\r\n", enable);
		return;
	}

	DEBUG("  >> HDCP acc %d\r\n", enable);

	if (enable) {
		it66353_h2swwr(0xAB, 0x60);
		// it66353_h2swset(0x3C, 0x01, 0x00);
	} else {
		it66353_h2swwr(0xAB, 0x74);
		// it66353_h2swset(0x3C, 0x01, 0x01);
	}
}

static void _tx_init(void)
{
	if (it66353_gdev.opts.dev_opt->ForceRxOn) {
		// for ATC electrical test
		it66353_h2swwr(0xFF, 0xC3);
		it66353_h2swwr(0xFF, 0xA5);
		it66353_h2swset(0xF4, 0x80, it66353_gdev.opts.dev_opt->ForceRxOn << 7);
		it66353_h2swwr(0xFF, 0xFF);
	}

	it66353_h2swset(0x50, 0x0B, 0x08);

	it66353_h2swset(0x3A, 0xC0, (1 << 7) + (0 << 6));
	it66353_h2swset(0x3B, 0x03, 0);			// DDC 75K
	it66353_h2swset(0x43, 0xFC, (0 << 7) + (0 << 5) + (0 << 4) + (2 << 2));
	it66353_h2swset(0xA9, 0xC0, (it66353_gdev.opts.tx_opt->EnTxChSwap << 7) +
			(it66353_gdev.opts.tx_opt->EnTxPNSwap << 6));

	// Enable HPD and RxSen Interrupt
	it66353_h2swwr(0x27, 0xff);
	it66353_h2swset(0x37, 0x78, 0x78);

	_tx_power_down();

	it66353_h2swset(0xBD, 0x01, it66353_gdev.opts.tx_opt->EnTxVCLKInv);
	it66353_h2swset(0xA9, 0x20, it66353_gdev.opts.tx_opt->EnTxOutD1t << 5);

	it66353_h2swset(0x50, 0x03, it66353_gdev.vars.Rx_active_port);
	it66353_enable_tx_port(1);
}

static void _tx_reset(void)
{
	DEBUG("TX Reset\r\n");

	it66353_h2swset(0x09, 0x01, 0x01);		// RegSoftTxVRst=1
	it66353_h2swset(0x09, 0x01, 0x00);		// RegSoftTxVRst=0

	// Enable TX DDC Master Reset
	it66353_h2swset(0x3B, 0x10, 0x10);		// DDC Master Reset
	it66353_h2swset(0x3B, 0x10, 0x00);

	_tx_init();
}

static void _rx_init(void)
{
	// Add RX initial option setting here
	it66353_h2rxset(0x34, 0x01, 0x01);	// Reg_AutoRCLK=1 (default)
	it66353_h2rxset(0x21, 0x40, 0x40);	// Reg_AutoEDIDRst=1
	it66353_h2rxwr(0x3A, 0xCB);		// to reduce RxDeskew Err and Chx LagErr
	it66353_h2rxset(0x3B, 0x20, 0x20);	// CED_Opt
	it66353_h2swset(0x44, 0x08, 0x00);
	it66353_h2rxset(0x29, 0x40, 0x00);
	it66353_h2rxset(0x3C, 0x01, 0x00);

	// it66353_h2rxset(0x3d, 0x02, 0x02);	// Reg_deskewdown = 1
}

void it66353_rx_reset(void)
{
	DEBUG("RX Reset\r\n");

	it66353_h2rxset(0x29, 0x40, 0x00);
	it66353_h2swset(0x44, 0x08, 0x08);
	it66353_h2rxwr(0x23, 0x01);		// SWRst=1
	// it66353_h2rxwr(0x22, 0x08);		// RegRst=1

	it66353_h2rxwr(0x23, 0xAF);
	msleep(1);
	it66353_h2rxwr(0x23, 0xA0);

	_rx_init();
}

static void _sw_init(void)
{
	u8 port;
	// H2SW Initial Setting
	it66353_h2swset(0x44, 0x03, RCLKFreqSel);
	msleep(1);

	it66353_init_rclk();

	// Enable Slave Address
	it66353_h2swwr(0xEF, it66353_gdev.opts.dev_opt->RxAddr | 0x01);

	#if EN_CEC
	if (it66353_gdev.opts.EnCEC) {
		// if CEC is enabled, we should have a accurate RCLK.
		u16 cec_timer_unit;

		it66353_h2swwr(0xEE, (it66353_gdev.opts.dev_opt->CecAddr | 0x01));
		it66353_cecset(0x08, 0x01, 0x01);

		cec_timer_unit = it66353_gdev.vars.RCLK / (16*10);
		Cec_Init(0xff & cec_timer_unit);
	} else
	#endif
	{
		u8 tmp;
		it66353_h2swwr(0xEE, (it66353_gdev.opts.dev_opt->CecAddr | 0x01));
		// it66353_cecset(0x0d, 0x10, 0x00);		// Disable CEC_IOPU
		tmp = 0x40;
		cecAddr_updata_bit(0x10, 0xff, tmp);		// Disable CEC_IOPU
		it66353_h2swwr(0xEE, (it66353_gdev.opts.dev_opt->CecAddr & 0xFE));
	}

	it66353_h2swset(0x44, 0x40, 0x00);			// EnRxPort2Pwd=0
	msleep(10);

	it66353_rx_caof_init(it66353_gdev.vars.Rx_active_port);

	// Setup INT Pin: Active Low & Open-Drain
	it66353_h2swset(0x11, 0x07, 0x03);

	// Enable SW Interrupt
	it66353_h2swset(0x37, 0xE0, 0xE0);
	it66353_h2swset(0x38, 0xF9, 0xF9);

	// enable non main port to power down
	it66353_h2swset(0x15, 0x08, 0 << 3);

	it66353_h2swset(0x2B, 0x02, 0x00);
	it66353_h2swset(0x2C, 0xC0, 0xC0);

	it66353_h2swset(0x50, 0xf0, 0x00);

	it66353_h2swset(0xC4, 0x08, 0x08);
	it66353_h2swset(0xC5, 0x08, 0x08);
	it66353_h2swset(0xC6, 0x08, 0x08);

	// P0~P3 auto power downs
#if 0
	it66353_rx_auto_power_down_enable_all(1);
#else
	it66353_rx_auto_power_down_enable_all(it66353_gdev.opts.dev_opt->RxAutoPowerDown);

	it66353_rx_term_power_down(RX_PORT_0, 0);
	it66353_rx_term_power_down(RX_PORT_1, 0);
	it66353_rx_term_power_down(RX_PORT_2, 0);
	it66353_rx_term_power_down(RX_PORT_3, 0);

#endif

	it66353_h2swset(0xF5, 0xE0,
			(it66353_gdev.opts.active_rx_opt->EnRxDDCBypass << 7)+
			(it66353_gdev.opts.active_rx_opt->EnRxPWR5VBypass << 6)+
			(it66353_gdev.opts.active_rx_opt->EnRxHPDBypass << 5));
	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == 1) {
		it66353_h2swset(0x3C, 0x01, 0x01);// disable DDCRegen by set RegTxMastersel=1
		it66353_h2swset(0xb3, 0x20, 0x20);
		_rx_edid_ram_disable(RX_PORT_0);
		_rx_edid_ram_disable(RX_PORT_1);
		_rx_edid_ram_disable(RX_PORT_2);
		_rx_edid_ram_disable(RX_PORT_3);
	} else {
		// config EDID RAM
		for (port = 0; port < RX_PORT_COUNT; port++) {
			if (it66353_gdev.opts.rx_opt[port]->DisableEdidRam) {
				_rx_edid_ram_disable(port);
				_rx_edid_address_disable(port);
			} else {
				_rx_edid_ram_enable(port);
				_rx_edid_address_enable(port);
			}
		}
	}

	if (it66353_gdev.opts.active_rx_opt->EnRxHPDBypass) {
		it66353_h2swset(0x4c, 0x40, 0x00);
		it66353_h2swset(0x4d, 0x40, 0x00);
		it66353_h2swset(0x4e, 0x40, 0x00);
	}

	// disable EDID read/write to clear P0AutoH2Mode and AutoScrbEn
	it66353_h2swset(0xB2, 0x60, 0x00);
	// it66353_h2swset(0xB2, 0x40, 0x00);

	// enable TX port latch ERROR count
	it66353_h2swset(0xAC, 0x11, 0x11);

	// ddc monitor
	it66353_h2swwr(0xB0, 0x80);

}

static void _sw_reset(void)
{
	DEBUG("Switch Reset\r\n");

	it66353_h2swwr(0xEF, it66353_gdev.opts.dev_opt->RxAddr | 0x01);
	it66353_h2swset(0x0A, 0x01, 0x01);	// SoftRstAll=1
	if (it66353_h2swrd(0xEF) == (it66353_gdev.opts.dev_opt->RxAddr | 0x01)) {
		it66353_h2swset(0x44, 0xA0, 0x80);// ForceWrUpd = 1 and SWGateRCLK = 0
	}
	// it66353_h2swset(0x0A, 0x02, 0x02);	// SoftSWRRst=1

	_sw_init();
}

// To have accurate RCLK,
// we should use "it66353_cal_rclk" instead of "it66353_init_rclk"
#if EN_CEC
static void it66353_cal_rclk(void)
{
	u8 i;
	u8 timer_int, timer_flt, wclk_high_ext;
	u32 wclk_valid_num, wclk_high_num, wclk_high_num_b, wclk_high_num_c;
	u32 sum, rclk_tmp, rclk, rddata;

	sum = 0;
	for (i = 0; i < 5; i++) {
		it66353_h2swset(0x11, 0x80, 0x80);
		msleep(99);
		it66353_h2swset(0x11, 0x80, 0x00);

		rddata = it66353_h2swrd(0x12);
		rddata += (it66353_h2swrd(0x13) << 8);
		rddata += (it66353_h2swrd(0x14) << 16);

		sum += rddata;
	}
	sum /= 5;
	rclk = sum / 100;

	DEBUG("RCLK=%d kHz\r\n\r\n", rclk);

	timer_int = rclk / 1000;
	timer_flt = (rclk - timer_int * 1000) * 256 / 1000;

	it66353_h2swwr(0x1E, timer_int);
	it66353_h2swwr(0x1F, timer_flt);

	rclk_tmp = (rclk) * (1 << RCLKFreqSel);

	wclk_valid_num = (8UL * rclk_tmp + 625) / 1250UL;
	wclk_high_num = (8 * rclk_tmp + 3125) / 6250UL;
	it66353_h2swset(0x2C, 0x3F, (u8)wclk_high_num & 0xFF);
	it66353_h2swwr(0x2D, (u8)wclk_valid_num & 0xFF);

	wclk_high_num_b = 32UL * rclk_tmp / (37125UL);
	wclk_high_num = 32UL * rclk_tmp - (wclk_high_num_b * 37125UL);
	wclk_high_ext = wclk_high_num * 2 / 37125UL;
	it66353_h2swwr(0x2E, (wclk_high_ext << 6) + ((u8)wclk_high_num_b));

	wclk_high_num_c = 4UL * rclk_tmp / 10625UL;
	wclk_high_num = 4UL * rclk_tmp - (wclk_high_num_c * 10625UL);
	wclk_high_ext = wclk_high_num * 4 / 10625UL;
	it66353_h2swwr(0x2F, (wclk_high_ext << 6) + ((u8)wclk_high_num_c));

	it66353_gdev.vars.RCLK = rclk;
}
#endif


void it66353_init_rclk(void)
{
#if EN_CEC
	if (it66353_gdev.opts.EnCEC) {
		it66353_cal_rclk();
	} else
#endif
	{
		#if 0
			// RCLK=20000 kHz
			it66353_h2swwr(0x1e, 0x14);
			it66353_h2swwr(0x1f, 0x00);
			it66353_h2swset(0x2c, 0x3f, 0x1a);
			it66353_h2swwr(0x2d, 0x80);
			it66353_h2swwr(0x2e, 0x11);
			it66353_h2swwr(0x2f, 0x87);
			it66353_gdev.vars.RCLK = 20000;
		#else
			#if 0
			// RCLK=19569 kHz
			it66353_h2swwr(0x1e, 0x13);
			it66353_h2swwr(0x1f, 0x91);
			it66353_h2swset(0x2c, 0x3f, 0x19);
			it66353_h2swwr(0x2d, 0x7d);
			it66353_h2swwr(0x2e, 0x50);
			it66353_h2swwr(0x2f, 0x47);
			it66353_gdev.vars.RCLK = 19569;
			#endif

			// RCLK=18562 kHz
			it66353_h2swwr(0x1e, 0x12);
			it66353_h2swwr(0x1f, 0x90);
			it66353_h2swset(0x2c, 0x3f, 0x18);
			it66353_h2swwr(0x2d, 0x77);
			it66353_h2swwr(0x2e, 0x10);
			it66353_h2swwr(0x2f, 0xc6);
			it66353_gdev.vars.RCLK = 18562;

		#endif
	}
}



u8 it66353_get_port_info0(u8 port, u8 info)
{
	u8 tmp;
	tmp = it66353_h2swrd(0x60 + port * 3);

	if ((tmp & info) == info)
		return 1;
	else
		return 0;
}

void it66353_enable_tx_port(u8 enable)
{
	it66353_h2swset(0x50, 0x08, (enable << 3));
}

void it66353_txoe(u8 enable)
{
	if (it66353_gdev.vars.current_txoe == enable) {
		DEBUG("  >> it66353_txoe return %d \r\n", enable);
		return;
	}

	DEBUG("TXOE=%d align=%d\r\n", enable, it66353_gdev.opts.active_rx_opt->TxOEAlignment);

	if (enable) {
		if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_20) {
			_tx_ovwr_hdmi_mode(HDMI_MODE_20);
			_tx_scdc_write(0x20, 0x03);
		} else if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_14) {
			_tx_ovwr_hdmi_mode(HDMI_MODE_14);
			_tx_scdc_write(0x20, 0x00); // todo: ? check if safe to send this?
		}

		it66353_h2swset(0xD4, 0x03, 0x01);	// Set DRV_RST='0'
		it66353_h2swset(0xD4, 0x03, 0x00);	// Set DRV_RST='0'

		// REPORT_TXOE_1();
	} else {
		// REPORT_TXOE_0();

		it66353_h2swset(0xD4, 0x07, 0x03);	// Set DRV_RST='1'
	}

	it66353_gdev.vars.current_txoe = enable;
}

static void it66353_auto_txoe(u8 enable)
{
	DEBUG("A_TXOE=%d align=%d\r\n", enable, it66353_gdev.opts.active_rx_opt->TxOEAlignment);

	if (enable) {
		it66353_h2swset(0xEB, 0x07, 0x02);	// output when data ready
		// it66353_h2swset(0xEB, 0x07, 0x07);	// output when clock ready
		//[7]Reg_GateTxOut, [5]Disoutdeskew, [1]Reg_EnTxDly
		it66353_h2swset(0xEA, 0xA2, 0x00);
		it66353_h2swset(0xEB, 0x10, 0x00); //[4]RegEnTxDODeskew_doneDly
	} else {
		it66353_h2swset(0xEB, 0x03, 0x01);
	}
}

void it66353_set_tx_5v(u8 output_value)
{
	if (it66353_gdev.vars.Tx_current_5v != output_value) {
		it66353_gdev.vars.Tx_current_5v = output_value;
		DEBUG("TX 5V output=%d\r\n", output_value);
	}

	if (output_value) {
		it66353_h2swset(0xF4, 0x0C, 0x0C);		// TXPWR5V=1
	} else {
		it66353_h2swset(0xF4, 0x0C, 0x08);		// TXPWR5V=0
	}
}


static u32 it66353_get_rx_vclk(u8 port)
{
	u32 tmds_clk;
	#if USING_WDOG
	u16 tmds_clk_speed;
	u8  wdog_clk_div;
	u8  sw_reg20;

	if (port >= RX_PORT_COUNT) {
		dev_err(g_it66353->dev, "it66353_get_rx_vclk p=%u\r\n", port);
		return 0;
	}

	_rx_wdog_rst(it66353_gdev.vars.Rx_active_port);

__RETRY_VCLK:

	wdog_clk_div = it66353_h2swrd(0x70 + port) & 0x07;

	if (wdog_clk_div & 0x04)
		wdog_clk_div = 8;
	else if (wdog_clk_div & 0x02)
		wdog_clk_div = 4;
	else if (wdog_clk_div & 0x01)
		wdog_clk_div = 2;
	else
		wdog_clk_div = 1;

	tmds_clk_speed = it66353_h2swrd(0x6C + port);

	sw_reg20 = it66353_h2swrd(0x20 + port * 2);
	if (sw_reg20 & 0x7C) {
		dev_err(g_it66353->dev, "it66353_get_rx_vclk sw_reg20=%02x\r\n",
			sw_reg20);
		tmds_clk_speed = ((tmds_clk_speed * 2) >> (RCLKFreqSel));
		tmds_clk = it66353_gdev.vars.RCLK * 256 * wdog_clk_div / tmds_clk_speed;

		dev_err(g_it66353->dev,
			"RXP%d WatchDog detect TMDSCLK = %lu kHz (div=%d, 6C=%02x)\r\n",
			port, tmds_clk, wdog_clk_div, tmds_clk_speed);

		tmds_clk_speed = 0;

		it66353_h2swwr(0x20 + port * 2, sw_reg20);

		goto __RETRY_VCLK;
	}

	if (tmds_clk_speed) {
		tmds_clk_speed = ((tmds_clk_speed * 2) >> (RCLKFreqSel));
		tmds_clk = it66353_gdev.vars.RCLK * 256 * wdog_clk_div / tmds_clk_speed;

		DEBUG("RXP%d WatchDog detect TMDSCLK = %lu kHz (div=%d, 6C=%02x)\r\n",
		      port, tmds_clk, wdog_clk_div, tmds_clk_speed);
	} else {
		dev_err(g_it66353->dev, "TMDSCLKSpeed == 0 p=%u\r\n", port);
		tmds_clk = 0;
	}
	#else

	u8 clk;

	if (port >= RX_PORT_COUNT) {
		dev_err(g_it66353->dev, "it66353_get_rx_vclk p=%u\r\n", port);
		return 0;
	}

	clk = it66353_h2swrd(0x61 + port*3);

	// the assigned tmds_clk value should refer to _tx_setup_afe()
	if (clk & 0x04) {
		DEBUG("RXP%d clock > 340M\r\n", port);
		tmds_clk = 340000UL;
	} else if (clk & 0x02) {
		DEBUG("RXP%d clock > 148M\r\n", port);
		tmds_clk = 163000UL;
	} else if (clk & 0x01) {
		DEBUG("RXP%d clock > 100M\r\n", port);
		tmds_clk = 148500UL;
	} else {
		DEBUG("RXP%d clock < 100M\r\n", port);
		tmds_clk = 74000UL;
	}
	#endif

	return tmds_clk;
}

static void it66353_detect_port(u8 port)
{
	u8 sw_reg20;
	u8 sw_reg21;
	u8 rddata;
	u8 sts_off0;

	sw_reg20 = it66353_h2swrd(0x20 + port * 2);
	sw_reg21 = it66353_h2swrd(0x21 + port * 2) & 0x01;

	if (sw_reg20) {
		sts_off0 = 0x60 + port * 3;
		rddata = it66353_h2swrd(sts_off0);

		if (sw_reg20 & 0x01) {
			DEBUG("--RXP-%d 5V Chg => 5V = %d\r\n", port, (rddata & 0x01));
			if (it66353_gdev.vars.Rx_active_port != port) {
				if ((rddata & 0x01)) {
					// 5V presents
					if (it66353_gdev.opts.rx_opt[port]->NonActivePortReplyHPD) {
						_rx_set_hpd(port, 1, TERM_FOLLOW_HPD);
						sw_reg20 &= 0x01;
					} else {
						_rx_set_hpd(port, 0, TERM_FOLLOW_HPD);
					}
				} else {
					_rx_set_hpd(port, 0, TERM_FOLLOW_HPD);
				}
			}
		}

		if (sw_reg20 & 0x02) {
			DEBUG("--RXP-%d RX Clock Valid Chg => RxCLK_Valid = %d\r\n",
				   port, (rddata & 0x08) >> 3);
		}

		if (sw_reg20 & 0x04) {
			DEBUG("--RXP-%d RX Clock Stable Chg => RxCLK_Stb = %d\r\n\r\n",
				   port, (rddata & 0x10) >> 4);
		}

		if (sw_reg20 & 0x08) {
			DEBUG("--RXP-%d RX Clock Frequency Change ...\r\n", port);
		}

		sts_off0 = 0x61 + port * 3;
		rddata = it66353_h2swrd(sts_off0);

		if (sw_reg20 & 0x10) {
			DEBUG("--RXP-%d RX Clock Ratio Chg => Clk_Ratio = %d \r\n",
				   port, (rddata & 0x40) >> 6);
		}

		if (sw_reg20 & 0x20) {
			DEBUG("--RXP%d RX Scrambling Enable Chg => Scr_En = %d \r\n",
				   port, (rddata & 0x80) >> 7);
		}

		sts_off0 = 0x62 + port * 3;
		rddata = it66353_h2swrd(sts_off0);

		if (sw_reg20 & 0x40) {
			DEBUG("--RXP%d RX Scrambling Status Chg => ScrbSts = %d \r\n",
					port, (rddata & 0x02) >> 1);
		}

		if (sw_reg20 & 0x80) {
			DEBUG("--RXP%d RX HDMI2 Detected Interrupt => HDMI2DetSts = %d \r\n",
					port, (rddata & 0x3C) >> 2);
		}

		it66353_h2swwr(0x20 + port * 2, sw_reg20);
	}

	if (sw_reg21) {
		it66353_h2swwr(0x21 + port * 2, sw_reg21);
#if 1
		if (sw_reg21 & 0x01) {
			DEBUG("--RXP%d EDID Bus Hang\r\n", port);
		}
#endif
	}
}

static void it66353_detect_ports(void)
{
	u8 i;

	for (i = 0; i < 4; i++) {
		if (it66353_gdev.vars.Rx_active_port != i) {
			it66353_detect_port(i);
		}
	}
}

static void it66353_rx_irq(void)
{
	u8 rddata, hdmi_int;
	u8 rx_reg05, rx_reg06, rx_reg10;

	rddata = it66353_h2rxrd(0x96);
	hdmi_int = (rddata & 0x40) >> 6;

	if (hdmi_int) {
		rx_reg05 = it66353_h2rxrd(0x05);
		rx_reg06 = it66353_h2rxrd(0x06);
		rx_reg10 = it66353_h2rxrd(0x10);
		it66353_h2rxwr(0x05, rx_reg05);
		it66353_h2rxwr(0x06, rx_reg06);

		if (rx_reg05 & 0x01) {
			DEBUG("..RX5V change\r\n");

			it66353_eq_reset_txoe_ready();
			it66353_eq_reset_state();
			it66353_auto_detect_hdmi_encoding();

			if (it66353_gdev.opts.active_rx_opt->TryFixedEQFirst) {
				it66353_gdev.vars.try_fixed_EQ = 1;
			}

			if (0 == _rx_is_5v_active()) {
				it66353_fsm_chg_delayed(RX_UNPLUG);
			}
		}

		if (rx_reg05 & 0x10) {
			DEBUG("..RX HDMIMode chg => HDMIMode = %d\r\n",
			      (it66353_h2rxrd(0x13) & 0x02) >> 1);
		}

		if (rx_reg05 & 0x40) {
			DEBUG("..RX DeSkew Err\r\n");
			it66353_gdev.vars.rx_deskew_err++;
			if (it66353_gdev.vars.rx_deskew_err > 50) {
				it66353_gdev.vars.rx_deskew_err = 0;
				it66353_toggle_hpd(1000);
			}
		}

		if (rx_reg05 & 0x80) {
			DEBUG("..RXP H2V FIFO Skew Fail\r\n");
		}

		if (rx_reg06 & 0x01) {
			u8 symlock = ((it66353_h2rxrd(0x13) & 0x80) >> 7);
			DEBUG("..RX CHx SymLock Chg => RxSymLock = %d\r\n", symlock);
			if (symlock) {
				// it66353_gdev.vars.count_fsm_err = 0;
			}
		}

		if (rx_reg06 & 0x02) {
			DEBUG("..RX CH0 SymFIFORst\r\n");
		}

		if (rx_reg06 & 0x04) {
			DEBUG("..RX CH1 SymFIFORst\r\n");
		}

		if (rx_reg06 & 0x08) {
			DEBUG("..RX CH2 SymFIFORst\r\n");
		}

		if (rx_reg06 & 0x10) {
			DEBUG("..RX CH0 SymLockRst\r\n");
		}

		if (rx_reg06 & 0x20) {
			DEBUG("..RX CH1 SymLockRst\r\n");
		}

		if (rx_reg06 & 0x40) {
			DEBUG("..RX CH2 SymLockRst\r\n");
		}

		if (rx_reg06 & 0x80) {
			DEBUG("..RX FSM Fail\r\n");
			it66353_gdev.vars.count_fsm_err++;
			if (it66353_gdev.vars.count_fsm_err > 20) {
				if (it66353_gdev.opts.active_rx_opt->FixIncorrectHdmiEnc) {
					it66353_fix_incorrect_hdmi_encoding();
				}
				it66353_eq_reset_txoe_ready();
				it66353_eq_reset_state();
				it66353_fsm_chg(RX_WAIT_CLOCK);
				it66353_gdev.vars.count_fsm_err = 0;
			}
		} else {
			if (it66353_gdev.vars.count_fsm_err > 0) {
				it66353_gdev.vars.count_fsm_err--;
			}
		}

		#if EN_H14_SKEW
		{
			u8 rx_reg07;
			rx_reg07 = it66353_h2rxrd(0x07);
			it66353_h2rxwr(0x07, rx_reg07);

			if (rx_reg07 & 0x01) {
				DEBUG("..RX CH0 Lag Err\r\n");
				it66353_rx_skew_adj(0);
			}
			if (rx_reg07 & 0x02) {
				DEBUG("..RX CH1 Lag Err\r\n");
				it66353_rx_skew_adj(1);
			}
			if (rx_reg07 & 0x04) {
				DEBUG("..RX CH2 Lag Err\r\n");
				it66353_rx_skew_adj(2);
			}
		}
		#endif

		if (rx_reg10 & 0x08) {
			it66353_h2rxwr(0x10, 0x08);
			DEBUG("..RX FW Timer Interrupt ...\r\n");
		}
	}
}

static void it66353_sw_irq(u8 port)
{
	u8 sw_reg20;
	u8 sw_reg21;
	u8 rddata;
	u8 sts_off0;


	sw_reg20 = it66353_h2swrd(0x20 + port * 2);
	sw_reg21 = it66353_h2swrd(0x21 + port * 2) & 0x01;

	if (sw_reg20 || sw_reg21) {
		it66353_h2swwr(0x20 + port * 2, sw_reg20);
		it66353_h2swwr(0x21 + port * 2, sw_reg21);

		sts_off0 = 0x60 + port * 3;

		if (sw_reg20 & 0x01) {
			// not here
			rddata = it66353_h2swrd(sts_off0);
			DEBUG("..RX-P%d PWR5V Chg => PWR5V = %d\r\n", port, (rddata & 0x01));
			// _rx_wdog_rst(port);
			if (0 == (rddata & 0x01)) {
				it66353_fsm_chg_delayed(RX_UNPLUG);
			}
		}

		if (sw_reg20 & 0x02) {
			rddata = it66353_h2swrd(sts_off0);
			DEBUG("..RXP%d RX Clock Valid Chg => RxCLK_Valid = %d\r\n",
			      port, (rddata & 0x08) >> 3);
			if (port == it66353_gdev.vars.Rx_active_port) {
				if (0 == (rddata & 0x08)) {	// clock not valid
					DBG_TM(CLK_UNSTABLE);
					if (it66353_gdev.vars.RxHPDFlag[it66353_gdev.vars.Rx_active_port] > 0) {
						it66353_fsm_chg_delayed(RX_WAIT_CLOCK);
					}
				} else {
					DBG_TM(CLK_STABLE);
				}
			}
		}

		if (sw_reg20 & 0x04) {
			msleep(10);
			rddata = it66353_h2swrd(sts_off0);
			DEBUG("..RXP%d RX Clock Stable Chg => RxCLK_Stb = %d\r\n\r\n",
				   port, (rddata & 0x10) >> 4);
			if (0 == (rddata & 0x10)) {
				DBG_CLKSTABLE_0();
				DBG_SYMLOCK_0();
				if (it66353_gdev.vars.RxHPDFlag[port]) {
					it66353_fsm_chg_delayed(RX_WAIT_CLOCK);
				}
			} else {
				it66353_gdev.vars.vclk = it66353_get_rx_vclk(it66353_gdev.vars.Rx_active_port);
				if ((it66353_gdev.vars.vclk != it66353_gdev.vars.vclk_prev)) {
					it66353_gdev.vars.vclk_prev = it66353_gdev.vars.vclk;
					if (it66353_gdev.vars.RxHPDFlag[port]) {
						it66353_fsm_chg_delayed(RX_WAIT_CLOCK);
					}
				}
			}
		}


		if (sw_reg20 & 0x08) {
			DEBUG("..RXP%d RX Clock Frequency Chg ...\r\n", port);
		}

		if (sw_reg20 & 0x10) {
			u8 new_ratio = (it66353_h2swrd(0x61 + port * 3) & 0x40) >> 6;

			DEBUG("..RXP%d RX Clock Ratio Chg => Clk_Ratio = %d \r\n",
			      port, new_ratio);

			if (it66353_gdev.vars.Rx_active_port == port) {
				if (new_ratio > 0) {
					it66353_auto_txoe(it66353_gdev.opts.active_rx_opt->TxOEAlignment);
				} else {
					it66353_auto_txoe(0);
				}
				it66353_txoe(1);

				if (new_ratio != it66353_gdev.vars.clock_ratio) {
					// it66353_auto_detect_hdmi_encoding();
					// it66353_fsm_chg_delayed(RX_WAIT_CLOCK);
				}
			}
		}

		if (sw_reg20 & 0x20) {
			DEBUG("..RXP%d RX Scrambling Enable Chg => Scr_En = %d \r\n",
			      port, (it66353_h2swrd(0x61 + port * 3) & 0x80) >> 7);
		}

		if (sw_reg20 & 0x40) {
			u8 new_scramble = (it66353_h2swrd(0x62 + port * 3) & 0x02) >> 1;

			DEBUG("..RXP%d RX Scrambling Status Chg => ScrbSts = %d \r\n",
			      port, new_scramble);
			if (it66353_gdev.vars.Rx_active_port == port) {
				if (new_scramble != it66353_gdev.vars.h2_scramble) {
					// it66353_fsm_chg_delayed(RX_WAIT_CLOCK);
				}
			}
		}

		if (sw_reg20 & 0x80) {
			DEBUG("..RXP%d RX HDMI2 Detected Interrupt => HDMI2DetSts = %d \r\n",
			      port, (it66353_h2swrd(0x62 + port * 3) & 0x3C) >> 2);
		}

		if (sw_reg21 & 0x01) {
			DEBUG("..RXP%d EDID Bus Hang\r\n", port);
		}
	}
}


static void it66353_tx_irq(void)
{
	u8 sw_reg27;
	u8 sw_reg28;
	u8 rddata;
	u8 reg3C;

	sw_reg27 = it66353_h2swrd(0x27);
	sw_reg28 = it66353_h2swrd(0x28) & ~(0x02|0x04);
	it66353_h2swwr(0x27, sw_reg27);
	it66353_h2swwr(0x28, sw_reg28);

	if (sw_reg27 & 0x08) {
		// dev_info(g_it66353->dev, " => HDCP 0x74 is detected\r\n");
	}

	if (sw_reg27 & 0x10) {
		DEBUG("  => HDCP 0x74 NOACK\r\n");
	}


	if (sw_reg27 & 0x20) {
		rddata = it66353_h2swrd(0x11);

		if ((rddata & 0x20)) {
			DEBUG("  => HPD High\r\n");
		} else {
			DEBUG("  => HPD Low\r\n");

			if (it66353_gdev.vars.state_sys_fsm != RX_TOGGLE_HPD &&
				 it66353_gdev.vars.state_sys_fsm != RX_UNPLUG) {
				it66353_fsm_chg_delayed(TX_UNPLUG);
			}
		}
	}

	if (sw_reg27 & 0x40) {
		DEBUG("  TX RxSen chg\r\n");

		if (it66353_h2swrd(0x11) & 0x40) {
			// rxsen = 1
		} else {
			// rxsen = 0
			// _rx_int_enable_all(0);
			// _rx_set_hpd_all(0);
			// it66353_fsm_chg(TX_WAIT_HPD);
		}
	}

	if (sw_reg27 & 0x80) {
		// DEBUG("  TX DDC Bus Hang\r\n");

		if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			reg3C = it66353_h2swrd(0x3C);
			it66353_h2swset(0x3C, 0x01, 0x01);
			it66353_h2swwr(0x3D, 0x0A);	// Generate SCL Clock
			it66353_h2swwr(0x3C, reg3C);
		}
	}

	if (sw_reg28 & 0x02) {
		// dev_info(g_it66353->dev, "SW User Timer 0 Interrupt ...\r\n");
	}

	if (sw_reg28 & 0x04) {
		// dev_info(g_it66353->dev, "SW User Timer 1 Interrupt ...\r\n");
	}

	if (sw_reg28 & 0x08) {
		// DEBUG("  TX DDC Command Fail\r\n");
		if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			reg3C = it66353_h2swrd(0x3C);
			it66353_h2swset(0x3C, 0x01, 0x01);
			it66353_h2swwr(0x3D, 0x0F);
			it66353_h2swwr(0x3C, reg3C);
		}

	}

	if (sw_reg28 & 0x80) {
		DEBUG("  TX DDC FIFO Error\r\n");
		if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			reg3C = it66353_h2swrd(0x3C);
			it66353_h2swset(0x3C, 0x01, 0x01);
			it66353_h2swwr(0x3D, 0x09);
			it66353_h2swwr(0x3C, reg3C);
		}
	}
}

static void it66353_wait_for_ddc_idle(void)
{
	u8 ddc_sts;
	u8 idle_cnt = 0;
	u8 busy_cnt = 0;
	u8 chk_dly = 3;

	while (1) {
		ddc_sts = it66353_h2swrd(0xB3);
		if ((ddc_sts & 0x10)) {
			busy_cnt = 0;
			idle_cnt++;
			chk_dly++;
			if (idle_cnt >= 5) {
				break;
			}
		} else {
			busy_cnt++;
			idle_cnt = 0;
			chk_dly = 3;

			msleep(100);
			if (busy_cnt > 10) {
				dev_err(g_it66353->dev, "**Wait DDC idle timeout\n");
				break;
			}
		}

		msleep(chk_dly);
	}
}

#if DEBUG_FSM_CHANGE
void __it66353_fsm_chg(u8 new_state, int caller)
#else
void it66353_fsm_chg(u8 new_state)
#endif
{

#if DEBUG_FSM_CHANGE
	if (new_state <= IDLE && it66353_gdev.vars.state_sys_fsm <= IDLE) {
		DEBUG("state_fsm %s -> %s (%d)\r\n",
			s__SYS_FSM_STATE[it66353_gdev.vars.state_sys_fsm],
			s__SYS_FSM_STATE[new_state], caller);
	} else {
		dev_err(g_it66353->dev, "state_fsm %d, new %d -> %d\r\n",
			it66353_gdev.vars.state_sys_fsm, new_state, caller);
	}
#else
	DEBUG("state_fsm %d -> %d\r\n", it66353_gdev.vars.state_sys_fsm, new_state);
#endif

	if (RX_PORT_CHANGE != new_state) {
		if (it66353_gdev.vars.state_sys_fsm == new_state) {
			DEBUG("skip fsm chg 1\r\n");
			return;
		}
	}


	if (new_state == RX_WAIT_CLOCK) {
		if (it66353_gdev.vars.RxHPDFlag[it66353_gdev.vars.Rx_active_port] == 0) {
			// don't change before HPD High
			DEBUG("skip fsm chg 2\r\n");
			return;
		}
	}


	it66353_gdev.vars.state_sys_fsm = new_state;
	it66353_gdev.vars.fsm_return = 0;

	switch (it66353_gdev.vars.state_sys_fsm) {
	case RX_TOGGLE_HPD:
		_sw_enable_hpd_toggle_timer(it66353_gdev.vars.hpd_toggle_timeout);
		break;
	case RX_PORT_CHANGE:
		it66353_txoe(0);
		DBG_TM(RX_SWITCH_PORT);
		DEBUG("Active port change from P%d to P%d\r\n",
		      it66353_gdev.vars.Rx_active_port, it66353_gdev.vars.Rx_new_port);
		if (it66353_gdev.vars.clock_ratio > 0 &&
		    it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			_tx_scdc_write(0x20, 0x00);
		}
		if (it66353_gdev.opts.tx_opt->TurnOffTx5VWhenSwitchPort) {
			it66353_set_tx_5v(0);
		}
		// _rx_int_enable(it66353_gdev.vars.Rx_active_port, 1);
		// _rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0);
		// _rx_wdog_rst(it66353_gdev.vars.Rx_prev_port);

		// make HPD low to stop DDC traffic
		_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_HPD);
		// wait 200ms for DDC traffic stopped
		msleep(200);

		// set it66353_gdev.vars.force_hpd_state to SW_HPD_AUTO
		// this to reset the force hpd low in previous active port
		// remove this line if you want to keep HPD low after port changing
		it66353_gdev.vars.force_hpd_state = SW_HPD_AUTO;
		it66353_gdev.vars.Rx_active_port = it66353_gdev.vars.Rx_new_port;

		it66353_wait_for_ddc_idle();

		it66353_h2swset(0x50, 0x03, it66353_gdev.vars.Rx_active_port);

		it66353_set_RS(it66353_gdev.opts.active_rx_opt->DefaultEQ[0],
					  it66353_gdev.opts.active_rx_opt->DefaultEQ[1],
					  it66353_gdev.opts.active_rx_opt->DefaultEQ[2]);

		it66353_gdev.EQ.sys_aEQ = SysAEQ_RUN;
		it66353_auto_detect_hdmi_encoding();
		it66353_eq_reset_state();
		it66353_eq_reset_txoe_ready();
		break;

	case TX_OUTPUT:
		it66353_gdev.vars.count_symlock_lost = 0;
		it66353_gdev.vars.count_symlock_unstable = 0;
		_sw_disable_hpd_toggle_timer();
		if ((it66353_gdev.opts.active_rx_opt->FixIncorrectHdmiEnc) &&
		    (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == FALSE)) {
			if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_20) {
				_tx_scdc_write(0x20, 0x03);
			} else if (it66353_gdev.vars.current_hdmi_mode == HDMI_MODE_14) {
				// todo: to check sink support SCDC ?
				_tx_scdc_write(0x20, 0x00);
			}
			it66353_auto_detect_hdmi_encoding();
		}

		_sw_hdcp_access_enable(1);
		it66353_txoe(1);

		if (it66353_gdev.vars.spmon == 1) {
			if ((it66353_gdev.opts.active_rx_opt->DisableEdidRam &
			    (1 << it66353_gdev.vars.Rx_active_port)) == 0) {
				_rx_edid_ram_enable(it66353_gdev.vars.Rx_active_port);
			}
		}
		break;

	case TX_OUTPUT_PREPARE:
		it66353_gdev.vars.count_try_force_hdmi_mode = 0;
		// it66353_auto_txoe(1);
		it66353_h2rxwr(0x05, 0xFF);
		it66353_h2rxwr(0x06, 0xFF);
		it66353_h2rxwr(0x07, 0xFF);
		break;

	#if EN_AUTO_RS
	case RX_CHECK_EQ:
		it66353_gdev.vars.count_symlock_fail = 0;
		// _sw_hdcp_access_enable(0);
		break;
	#endif

	case SETUP_AFE:
		// it66353_gdev.vars.en_count_hdcp = 1;
		// it66353_gdev.vars.tick_set_afe = get_tick_count();
		it66353_rx_term_power_down(it66353_gdev.vars.Rx_active_port, 0x00);
		it66353_gdev.vars.vclk = it66353_get_rx_vclk(it66353_gdev.vars.Rx_active_port);

		if (it66353_gdev.vars.vclk) {
			it66353_gdev.vars.clock_ratio =
				((it66353_h2swrd(0x61 + it66353_gdev.vars.Rx_active_port * 3) >> 6) & 1);
			dev_dbg(g_it66353->dev, "Clk Ratio = %d\r\n",
				it66353_gdev.vars.clock_ratio);

			if (it66353_gdev.vars.clock_ratio > 0) {
				if (it66353_gdev.vars.vclk < 300000UL) {
					it66353_gdev.vars.vclk = 300001UL;
				}
				// CED opt for HDBaseT disabled
				it66353_h2rxset(0x3B, 0x10, 0x00);
			} else {
				if (it66353_gdev.vars.vclk >= 300000UL) {
					it66353_gdev.vars.vclk = 297000UL;
				}
				// CED opt for HDBaseT enabled
				it66353_h2rxset(0x3B, 0x10, 0x10);
			}
			#if 0 // for 8-7 480p
			if (it66353_gdev.vars.vclk < 35000UL) {
				dev_dbg("## ATC 480P\r\n");
				// it66353_h2rxset(0x3c, 0x01, 0x00);
				it66353_h2swset(0x2b, 0x02, 0x00);
			} else {
				// it66353_h2rxset(0x3c, 0x01, 0x01);
				it66353_h2swset(0x2b, 0x02, 0x02);
			}
			#endif

			_tx_power_on();
			_rx_setup_afe(it66353_gdev.vars.vclk);
			_tx_setup_afe(it66353_gdev.vars.vclk);

			if (it66353_gdev.vars.clock_ratio == 0) {
				it66353_auto_txoe(0);
				dev_dbg(g_it66353->dev, "Clk Ratio==0, align=0\n");
			} else {
				it66353_auto_txoe(it66353_gdev.opts.active_rx_opt->TxOEAlignment);
				dev_dbg(g_it66353->dev, "Clk Ratio==1, align=%d\n",
					it66353_gdev.opts.active_rx_opt->TxOEAlignment);
			}

			it66353_txoe(1);

		}
		break;

	case RX_WAIT_CLOCK:
		it66353_txoe(0);
		if (it66353_gdev.opts.dev_opt->TxPowerDownWhileWaitingClock) {
			_tx_power_down();
		}

		it66353_sw_disable_timer0();
		it66353_sw_clear_hdcp_status();
		// _rx_wdog_rst(it66353_gdev.vars.Rx_active_port);

		#if EN_AUTO_RS
		it66353_gdev.vars.RxCEDErrRec[1][0] = 0xffff;
		it66353_gdev.vars.RxCEDErrRec[1][1] = 0xffff;
		it66353_gdev.vars.RxCEDErrRec[1][2] = 0xffff;
		it66353_gdev.EQ.manu_eq_fine_tune_count[0] = 0;
		it66353_gdev.EQ.manu_eq_fine_tune_count[1] = 0;
		it66353_gdev.EQ.manu_eq_fine_tune_count[2] = 0;
		it66353_gdev.EQ.ced_err_avg_prev[0] = 0x8888;
		it66353_gdev.EQ.ced_err_avg_prev[1] = 0x8888;
		it66353_gdev.EQ.ced_err_avg_prev[2] = 0x8888;
		it66353_gdev.EQ.ced_acc_count = 0;
		#endif
		it66353_gdev.vars.count_symlock = 0;
		it66353_gdev.vars.count_unlock = 0;
		it66353_gdev.vars.check_for_hpd_toggle = 0;
		it66353_gdev.vars.sdi_stable_count = 0;
		it66353_gdev.vars.check_for_sdi = 1;
		it66353_gdev.vars.rx_deskew_err = 0;
		break;

	case RX_HPD:
		_rx_int_enable();

		#if 1
		// it66353it66353_rx_ovwr_hdmi_clk(it66353_gdev.vars.Rx_active_port, HDMI_MODE_14);
		// it66353it66353_rx_ovwr_h20_scrb(it66353_gdev.vars.Rx_active_port, 0);
		#else
		it66353it66353_rx_ovwr_hdmi_clk(it66353_gdev.vars.Rx_active_port, RX_CLK_H20);
		it66353it66353_rx_ovwr_h20_scrb(it66353_gdev.vars.Rx_active_port, 1);
		#endif

		// it66353_gdev.vars.Rx_prev_port = it66353_gdev.vars.Rx_active_port;
		// _rx_int_enable_all(0);
		// _rx_set_hpd_all(0);
		_sw_hdcp_access_enable(1);
		_sw_int_enable(it66353_gdev.vars.Rx_active_port, 1);
		// _rx_set_hpd(it66353_gdev.vars.Rx_active_port, 1);
		_rx_wdog_rst(it66353_gdev.vars.Rx_active_port);
		if (it66353_gdev.vars.spmon == 1) {
			if ((it66353_gdev.opts.active_rx_opt->DisableEdidRam &
			    (1<<it66353_gdev.vars.Rx_active_port)) == 0) {
				_rx_edid_ram_disable(it66353_gdev.vars.Rx_active_port);
			}
		}

		_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 1, TERM_FOLLOW_HPD);
		if (it66353_gdev.vars.is_hdmi20_sink == 0) {
			it66353_auto_txoe(0);
		} else {
			it66353_auto_txoe(it66353_gdev.opts.active_rx_opt->TxOEAlignment);
		}
		it66353_txoe(1);

		break;

	case TX_GOT_HPD:
		it66353_txoe(0);

		// _tx_power_on();
		if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			it66353_setup_edid_ram(0);
			it66353_gdev.vars.edid_ready = 1;
		}

		#if EN_CEC
		if (it66353_gdev.opts.EnCEC) {
			u8 u8phyAB = (it66353_gdev.vars.PhyAdr[0] << 4) | (it66353_gdev.vars.PhyAdr[1] & 0xF);
			u8 u8phyCD = (it66353_gdev.vars.PhyAdr[2] << 4) | (it66353_gdev.vars.PhyAdr[3] & 0xF);

			CecSys_Init(u8phyAB, u8phyCD, it66353_gdev.vars.Rx_active_port);
		}
		#endif

		if (it66353_gdev.opts.active_rx_opt->NonActivePortReplyHPD) {
			_rx_set_hpd_with_5v_all(true);
		}

		break;

	case TX_WAIT_HPD:
		it66353_txoe(0);
		it66353_auto_txoe(0);
		if (_rx_is_5v_active()) {
			it66353_set_tx_5v(1);
		} else {
			it66353_set_tx_5v(0);
		}

		// it66353_set_RS(10);
		break;

	case TX_UNPLUG:
		it66353_txoe(0);
		it66353_auto_txoe(0);
		it66353_gdev.vars.edid_ready = 0;
		// _rx_int_enable(it66353_gdev.vars.Rx_active_port, 0);
		_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_TX);
		// _rx_set_hpd_all(0);
		it66353_h2swset(0xB2, 0x0A, 0x0A);  // W1C AutoH2Mode and AutoScrbEn
		_tx_power_down();
		break;

	case RX_UNPLUG:
		it66353_txoe(0);
		it66353_auto_txoe(0);
		// _rx_int_enable(it66353_gdev.vars.Rx_active_port, 0);
		if (it66353_gdev.vars.force_hpd_state == SW_HPD_LOW) {
			_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_HPD);
		} else {
			_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_TX);
		}
		it66353_set_tx_5v(_rx_is_5v_active());
		// it66353_gdev.EQ.sys_aEQ = SysAEQ_RUN;
		it66353_h2swset(0xB2, 0x0A, 0x0A);  // W1C AutoH2Mode and AutoScrbEn
		_tx_power_down();
		break;
	default:
		break;
	}
}

#if DEBUG_FSM_CHANGE
void __it66353_fsm_chg2(u8 new_state, int caller)
#else
void it66353_fsm_chg_delayed(u8 new_state)
#endif
{

#if DEBUG_FSM_CHANGE
	if (new_state <= IDLE && it66353_gdev.vars.state_sys_fsm <= IDLE) {
		dev_dbg(g_it66353->dev, "#state_fsm %s -> %s (%d)\r\n",
				s__SYS_FSM_STATE[it66353_gdev.vars.state_sys_fsm],
				s__SYS_FSM_STATE[new_state], caller);
	} else {
		dev_err(g_it66353->dev, "#state_fsm %d, new %d -> %d\r\n",
				it66353_gdev.vars.state_sys_fsm, new_state, caller);
	}
#else
	dev_dbg(g_it66353->dev, "#state_fsm %d -> %d\r\n",
		it66353_gdev.vars.state_sys_fsm, new_state);
#endif

	it66353_fsm_chg(new_state);
	it66353_gdev.vars.fsm_return = 1;
}

static void _rx_pll_reset(void)
{
	it66353_h2swset(0x06+it66353_gdev.vars.Rx_active_port, 0x01, 0x01);
	msleep(2);
	it66353_h2swset(0x06+it66353_gdev.vars.Rx_active_port, 0x01, 0x00);
}

void it66353_auto_detect_hdmi_encoding(void)
{
	_rx_ovwr_hdmi_mode(it66353_gdev.vars.Rx_active_port, HDMI_MODE_AUTO);
	_tx_ovwr_hdmi_mode(HDMI_MODE_AUTO);
	it66353_gdev.vars.current_hdmi_mode = HDMI_MODE_AUTO;
	// _rx_pll_reset();
	dev_info(g_it66353->dev, "HDMI_MODE=AUTO \r\n");
}

void it66353_force_hdmi20(void)
{
	_rx_ovwr_hdmi_mode(it66353_gdev.vars.Rx_active_port, HDMI_MODE_20);
	_tx_ovwr_hdmi_mode(HDMI_MODE_20);
	it66353_gdev.vars.current_hdmi_mode = HDMI_MODE_20;
	// _rx_pll_reset();
	dev_info(g_it66353->dev, "HDMI_MODE=F20\r\n");
}

static void it66353_force_hdmi14(void)
{
	_rx_ovwr_hdmi_mode(it66353_gdev.vars.Rx_active_port, HDMI_MODE_14);
	_tx_ovwr_hdmi_mode(HDMI_MODE_14);
	it66353_gdev.vars.current_hdmi_mode = HDMI_MODE_14;
	// _rx_pll_reset();
	dev_info(g_it66353->dev, "HDMI_MODE=F14\r\n");
}


void it66353_fix_incorrect_hdmi_encoding(void)
{
	switch (it66353_gdev.vars.current_hdmi_mode) {
	case HDMI_MODE_AUTO:
		// try HDMI 2.0
		it66353_force_hdmi20();
		break;
	case HDMI_MODE_20:
		// try HDMI 1.4
		it66353_force_hdmi14();
		break;
	case HDMI_MODE_14:
		// try HDMI 2.0
		it66353_auto_detect_hdmi_encoding();
		break;
	default:
		// try HDMI 2.0
		it66353_auto_detect_hdmi_encoding();
		break;
	}

	_rx_pll_reset();
}

#if EN_AUTO_RS
static void it66353_fsm_EQ_check(void)
{
	static u8 aeq_retry;
	u8 eq_state;

	if (it66353_rx_is_clock_stable()) {
		_rx_show_ced_info();

		if (it66353_eq_get_txoe_ready() == 1) {
			it66353_eq_load_previous();
			dev_info(g_it66353->dev, "EQ restore2 !\r\n");
			// it66353_fsm_chg(TX_OUTPUT);
			it66353_fsm_chg(TX_OUTPUT_PREPARE);
		} else {
			eq_state = it66353_eq_get_state();
			dev_info(g_it66353->dev, "[%d] eq_state=%d\r\n",
				 __LINE__, (int)eq_state);

			if (eq_state == SysAEQ_RUN) {
				it66353_eq_set_txoe_ready(0);
				if (it66353_auto_eq_adjust()) {
					it66353_gdev.vars.check_for_hpd_toggle = 1;
					it66353_eq_set_state(SysAEQ_DONE);
					it66353_fsm_chg(TX_OUTPUT_PREPARE);
					dev_info(g_it66353->dev, "EQ done !\r\n");
				} else {
					aeq_retry++;
					if (aeq_retry > 5) {
						aeq_retry = 0;
						it66353_gdev.vars.check_for_hpd_toggle = 1;
						it66353_eq_set_state(SysAEQ_DONE);
						it66353_fsm_chg(TX_OUTPUT_PREPARE);
						dev_info(g_it66353->dev, "EQ give up !\r\n");
					}
				}
			} else {
				if (eq_state == SysAEQ_DONE) {
					it66353_eq_load_previous();
					it66353_fsm_chg(TX_OUTPUT_PREPARE);
					dev_info(g_it66353->dev, "EQ restore !\r\n");
				} else if (eq_state == SysAEQ_OFF) {
					it66353_eq_load_default();
					it66353_fsm_chg(TX_OUTPUT_PREPARE);
					dev_info(g_it66353->dev, "EQ default !\r\n");
				} else {
					dev_err(g_it66353->dev, "??eq_state=%d\r\n", eq_state);
				}
			}
		}
	} else {
		it66353_fsm_chg(RX_WAIT_CLOCK);
	}
}
#endif

static void it66353_fsm(void)
{
	static __maybe_unused u8 prep_count;
	static __maybe_unused u8 prep_fail_count;
// LOOP_FSM:

	switch (it66353_gdev.vars.state_sys_fsm) {
	case RX_TOGGLE_HPD:
		if ((it66353_gdev.opts.active_rx_opt->NonActivePortReplyHPD == 0) &&
			(it66353_gdev.opts.tx_opt->TurnOffTx5VWhenSwitchPort == 0)) {
			_sw_disable_hpd_toggle_timer();
			it66353_fsm_chg(RX_UNPLUG);
		} else {
			if (_sw_check_hpd_toggle_timer()) {
				_sw_disable_hpd_toggle_timer();
				it66353_fsm_chg(RX_UNPLUG);
			} else {
				// keep waiting hpd toggle
			}
		}
		break;
	case RX_PORT_CHANGE:
		_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_HPD);
		it66353_rx_reset();
		it66353_rx_caof_init(it66353_gdev.vars.Rx_active_port);
		_rx_pll_reset();
		it66353_gdev.vars.hpd_toggle_timeout = it66353_gdev.opts.active_rx_opt->HPDTogglePeriod;
		it66353_fsm_chg(RX_TOGGLE_HPD);
		break;
	case TX_OUTPUT:

		// if (it66353_gdev.opts.WaitSymlockBeforeTXOE)
		{
			if (0 == it66353_rx_is_all_ch_symlock()) {
				if (0 == _rx_is_any_ch_symlock()) {
					it66353_gdev.vars.count_symlock_lost++;
					dev_err(g_it66353->dev,
						"RX Symlock lost %d\r\n",
						it66353_gdev.vars.count_symlock_lost);
					if (it66353_gdev.vars.count_symlock_lost == 100) {
						_rx_pll_reset();
						it66353_toggle_hpd(1000);
						// it66353_set_tx_5v(0);
						// it66353_gdev.vars.count_symlock_lost = 0;
						// it66353it66353_rx_handle_output_err();
					}
				} else {
					it66353_gdev.vars.count_symlock_unstable++;
					dev_err(g_it66353->dev,
						"RX Symlock unstable %d\r\n",
						it66353_gdev.vars.count_symlock_unstable);
					if (it66353_gdev.vars.count_symlock_unstable > 8) {
						_rx_pll_reset();
						// it66353_fsm_chg(RX_WAIT_CLOCK);
						it66353_toggle_hpd(1000);
					}
				}
			} else {
				it66353_gdev.vars.count_symlock_lost = 0;
				it66353_gdev.vars.count_symlock_unstable = 0;
			}
		}

		if (it66353_rx_monitor_ced_err()) {
			it66353it66353_rx_handle_output_err();
		}

		// _sw_show_hdcp_status();

		if (it66353_gdev.opts.active_rx_opt->FixIncorrectHdmiEnc) {
			/*
			 * check if source send incorrect SCDC clock ratio
			 * after 66353 sent.
			 */
			if (it66353_gdev.vars.current_hdmi_mode !=
			    HDMI_MODE_AUTO && it66353_gdev.opts.active_rx_opt->EnRxDDCBypass ==
			    false) {
				_sw_monitor_and_fix_scdc_write();
			}
		}

		if (it66353_gdev.vars.check_for_sdi) {
			_sw_sdi_check();
		}

		// _tx_show_sink_ced();

		// _pr_port_info(it66353_gdev.vars.Rx_active_port);

		break;

	case TX_OUTPUT_PREPARE:
	#if EN_AUTO_RS
		// check symbol lock before tx output
		if (0 == _rx_is_any_ch_symlock()) {
			dev_err(g_it66353->dev, "RxChk-SymUnlock\r\n");
			it66353_gdev.vars.count_symlock_fail++;
			if (it66353_gdev.vars.count_symlock_fail > 3) {
				it66353_gdev.vars.count_symlock_fail = 0;
				// can not get any channel symbol lock,
				// the HDMI encoding may be incorrect
				if (it66353_gdev.opts.active_rx_opt->FixIncorrectHdmiEnc) {
					if (it66353_gdev.vars.count_try_force_hdmi_mode < 6) {
						it66353_gdev.vars.count_try_force_hdmi_mode++;
						it66353_fix_incorrect_hdmi_encoding();
					} else {
						it66353_gdev.vars.count_try_force_hdmi_mode = 0;
						it66353_fsm_chg(RX_WAIT_CLOCK);
					}
				} else {
					it66353_eq_reset_state();
					it66353_eq_reset_txoe_ready();
					it66353_fsm_chg(RX_CHECK_EQ);
				}
			}
		} else {
			it66353_eq_set_txoe_ready(1);

			if ((it66353_gdev.vars.check_for_hpd_toggle == 1) &&
				(it66353_gdev.vars.current_txoe == 0) &&
				(_rx_need_hpd_toggle())) {
				DBG_TM(AEQ_TOGGLE_HPD);
				it66353_set_tx_5v(0);
				_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_HPD);
				it66353_fsm_chg(RX_TOGGLE_HPD);
			} else {
				#if CHECK_INT_BEFORE_TXOE
				u8 reg05 = it66353_h2rxrd(0x05);
				u8 reg06 = it66353_h2rxrd(0x06);
				u8 reg07 = it66353_h2rxrd(0x07);
				if (reg05 == 0 && reg06 == 0 && reg07 == 0) {
					prep_count++;
				} else {
					dev_err(g_it66353->dev,
						"RX reg: 05=%02x, 06=%02x 07=%02x\r\n",
						reg05, reg06, reg07);
					it66353_h2rxwr(0x05, reg05);
					it66353_h2rxwr(0x06, reg06);
					it66353_h2rxwr(0x07, reg07);
					prep_count = 0;
					prep_fail_count++;
				}

				if (prep_count == 1) {
					_sw_hdcp_access_enable(0);
				}

				if (prep_count >= 4) {
					prep_count = 0;
					it66353_fsm_chg(TX_OUTPUT);
				} else {
					if (prep_fail_count > 20) {
						prep_fail_count = 0;
						it66353_fsm_chg(RX_WAIT_CLOCK);
					}
				}
				#else
				it66353_fsm_chg(TX_OUTPUT);
				#endif
			}
		}
	#endif
		break;

	case RX_CHECK_EQ:
	#if EN_AUTO_RS
		it66353_fsm_EQ_check();
	#endif
		break;

	case SETUP_AFE:
		prep_count = 0;
		prep_fail_count = 0;
		if (it66353_gdev.vars.vclk == 0) {
			it66353_fsm_chg(RX_WAIT_CLOCK);
		} else {
		#if EN_AUTO_RS
			if (it66353_gdev.vars.try_fixed_EQ) {
				it66353_eq_set_txoe_ready(1);
				// it66353_fsm_chg(TX_OUTPUT);
				it66353_fsm_chg(TX_OUTPUT_PREPARE);
			} else {
				if (it66353_gdev.opts.active_rx_opt->EnableAutoEQ) {
					it66353_fsm_chg(RX_CHECK_EQ);
				} else {
					it66353_eq_set_txoe_ready(1);
					// it66353_fsm_chg(TX_OUTPUT);
					it66353_fsm_chg(TX_OUTPUT_PREPARE);
				}
			}
		#else
			it66353_eq_set_txoe_ready(1);
			it66353_fsm_chg(TX_OUTPUT);
			// it66353_fsm_chg(TX_OUTPUT_PREPARE);
		#endif
		}

		break;

	case RX_WAIT_CLOCK:

		if (it66353_rx_is_clock_stable()) {
			it66353_rx_clear_ced_err();
			// _sw_enable_txoe_timer_check();
			// _sw_hdcp_access_enable(0);
			it66353_fsm_chg(SETUP_AFE);
		} else {
			if (it66353_gdev.vars.RxHPDFlag[it66353_gdev.vars.Rx_active_port] == 0) {
				it66353_fsm_chg(RX_UNPLUG);
			}

			if (it66353_gdev.vars.current_hdmi_mode != HDMI_MODE_AUTO) {
				it66353_gdev.vars.count_wait_clock++;
				if (it66353_gdev.vars.count_wait_clock > 100) {
					it66353_gdev.vars.count_wait_clock = 0;
					it66353_auto_detect_hdmi_encoding();
					it66353_fsm_chg(RX_UNPLUG);
				}
			}
		}
		break;

	case RX_HPD:
		it66353_fsm_chg(RX_WAIT_CLOCK);
		break;

	case TX_GOT_HPD:
		it66353_fsm_chg(RX_HPD);
		// scdcwr(0x30, 0x01);
		break;

	case TX_WAIT_HPD:
		if (0 == _rx_is_5v_active()) {
			it66353_fsm_chg_delayed(RX_UNPLUG);
		}
		if (_tx_is_sink_hpd_high()) {
			it66353_fsm_chg(TX_GOT_HPD);
		}
		break;

	case TX_UNPLUG:
		if (_rx_is_5v_active()) {
			it66353_fsm_chg_delayed(TX_WAIT_HPD);
		} else {
			it66353_fsm_chg_delayed(RX_UNPLUG);
		}
		break;

	case RX_UNPLUG:
		if (_rx_is_5v_active()) {
			if (it66353_gdev.vars.force_hpd_state == SW_HPD_LOW) {
				break;
			}

			if (it66353_gdev.vars.state_sys_fsm != RX_TOGGLE_HPD) {
				it66353_fsm_chg_delayed(TX_WAIT_HPD);
			}
		} else {
			if (_rx_get_all_port_5v()) {
				// it66353_fsm_chg2(TX_WAIT_HPD);
			}
		}
		break;

	case IDLE:
		break;
	}

	if (it66353_gdev.vars.fsm_return == 0) {
		it66353_gdev.vars.fsm_return = 1;
		// goto LOOP_FSM;
	}
}


static void it66353_irq(void)
{
	u8 sys_int_sts;
	u8 currBD = 0;

	it66353_detect_ports();

	if (it66353_gdev.vars.state_sys_fsm == RX_TOGGLE_HPD) {
		return;
	}

	// static u8 prevBD = 1;
	currBD = it66353_h2swrd(0xBD);
	// if (currBD != prevBD) {
	if (currBD & 0xe0) {
		// it66353_gdev.vars.tick_hdcp = get_tick_count();
		dev_info(g_it66353->dev, "---HDCP BD=%02x\r\n", currBD);
		// prevBD = currBD;
		it66353_sw_clear_hdcp_status();

		/*
		 * if (currBD & 0x10) {
		 *	if (prevBD) {
		 *		prevBD = 0;
		 *		_sw_hdcp_access_enable(0);
		 *	} else {
		 *		_sw_hdcp_access_enable(1);
		 *	}
		 * }
		 */

	}

	sys_int_sts = it66353_h2swrd(0x0C);

	if (sys_int_sts == 0x00) {
		return;
	}

	if (sys_int_sts & 0x01) {
		it66353_rx_irq();
	}

	if (sys_int_sts & 0x10) {
		it66353_sw_irq(it66353_gdev.vars.Rx_active_port);
		it66353_tx_irq();
	}

	#if EN_CEC
	if (it66353_gdev.opts.EnCEC && (sys_int_sts & 0x80)) {
		Cec_Irq();
	}
	if (it66353_gdev.opts.EnCEC) {
		CecSys_TxHandler();
		CecSys_RxHandler();
	}
	#endif
}

bool it66353_device_init(void)
{
	u8 i;
	bool init_done = 0;

	switch (it66353_gdev.vars.state_dev_init) {
	case 0:
		DBG_CLKSTABLE_0();
		DBG_SYMLOCK_0();

		it66353_set_tx_5v(0);

		_sw_reset();
		_rx_set_hpd_all(0);
		it66353_rx_reset();
		_tx_reset();

		it66353_txoe(0);
		it66353_sw_disable_timer0();
		_sw_disable_timer1();
		// _sw_config_timer1(50);
		// _sw_enable_timer1();
		// config default RS
		it66353_set_RS(it66353_gdev.opts.active_rx_opt->DefaultEQ[0],
					  it66353_gdev.opts.active_rx_opt->DefaultEQ[1],
					  it66353_gdev.opts.active_rx_opt->DefaultEQ[2]);
		if (it66353_gdev.opts.tx_opt->CopyEDIDFromSink) {
			it66353_set_tx_5v(1);
			it66353_gdev.vars.state_dev_init = 1;
			it66353_gdev.vars.hpd_wait_count = 0;
		} else {
			it66353_gdev.vars.state_dev_init = 2;
		}
		break;

	case 1:
		if (_tx_is_sink_hpd_high()) {
			if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
				if (it66353_setup_edid_ram(0)) {
					it66353_gdev.vars.edid_ready = 1;
				}
				it66353_gdev.vars.state_dev_init = 3;
			} else {
				it66353_gdev.vars.edid_ready = 1;
				it66353_gdev.vars.state_dev_init = 3;
			}
		} else {
			it66353_gdev.vars.hpd_wait_count++;
			if (it66353_gdev.vars.hpd_wait_count > 200) {
				// it66353_gdev.vars.state_dev_init = 2;
				it66353_gdev.vars.hpd_wait_count = 0;
				dev_info(g_it66353->dev, "waiting HPD...\r\n");
			}
			// it66353_set_tx_5v(_rx_is_5v_active());
		}
		break;

	case 2:
		// load FW default EDID
		dev_info(g_it66353->dev, "Using internal EDID...\r\n");
		if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == false) {
			it66353_gdev.vars.default_edid[0] = it66353_s_default_edid_port0;
			it66353_gdev.vars.default_edid[1] = it66353_s_default_edid_port0;
			it66353_gdev.vars.default_edid[2] = it66353_s_default_edid_port0;
			it66353_gdev.vars.default_edid[3] = it66353_s_default_edid_port0;

			// note:
			// The EDID can be different from each port.
			// please set default_edid[?] pointer to a desired EDID array
			// if you need different EDID.
			//
			// for example:
			// it66353_gdev.vars.default_edid[1] = s_default_edid_port1;
			// it66353_gdev.vars.default_edid[2] = s_default_edid_port2;
			// it66353_gdev.vars.default_edid[3] = s_default_edid_port3;

			for (i = 0; i < RX_PORT_COUNT; i++) {
				it66353_setup_edid_ram(i);
			}
		}

		it66353_gdev.vars.edid_ready = 1;
		it66353_gdev.vars.state_dev_init = 3;

		break;

	case 3:
		_sw_int_enable_all(1);
		_rx_set_hpd_with_5v_all(1);

		dev_info(g_it66353->dev, "Active port = P%d\r\n",
			 it66353_gdev.vars.Rx_active_port);

		it66353_set_tx_5v(_rx_is_5v_active());

		init_done = 1;
		break;

	default:
		it66353_gdev.vars.state_dev_init = 0;
		break;
	}

	return init_done;
}

void it66353_vars_init(void)
{
	// FW Variables init:
	it66353_gdev.vars.state_dev_init = 0;
	it66353_gdev.vars.VSDBOffset = 0xFF;
	it66353_gdev.vars.PhyAdr[0] = 0;
	it66353_gdev.vars.PhyAdr[1] = 0;
	it66353_gdev.vars.PhyAdr[2] = 0;
	it66353_gdev.vars.PhyAdr[3] = 0;
	it66353_gdev.vars.RxHPDFlag[0] = -1;
	it66353_gdev.vars.RxHPDFlag[1] = -1;
	it66353_gdev.vars.RxHPDFlag[2] = -1;
	it66353_gdev.vars.RxHPDFlag[3] = -1;
	it66353_gdev.vars.Tx_current_5v = -1;
	it66353_gdev.vars.count_eq_check = 0;
	it66353_gdev.vars.count_fsm_err = 0;
	it66353_gdev.vars.count_unlock = 0;
	it66353_gdev.vars.state_sys_fsm = RX_UNPLUG;
	it66353_gdev.EQ.AutoEQ_state = AEQ_OFF;
	it66353_gdev.EQ.sys_aEQ = SysAEQ_RUN;
	it66353_gdev.vars.edid_ready = 0;
	it66353_gdev.vars.current_txoe = 0xFF;
	it66353_gdev.vars.check_for_hpd_toggle = 0;
	it66353_gdev.vars.sdi_stable_count = 0;
	it66353_gdev.vars.check_for_sdi = 1;
	it66353_gdev.vars.force_hpd_state = SW_HPD_AUTO;	// 1 : auto, don't modify here
	it66353_gdev.vars.vclk_prev = 0;
	if (it66353_gdev.opts.active_rx_opt->TryFixedEQFirst) {
		it66353_gdev.vars.try_fixed_EQ = 1;
	}
	it66353_gdev.vars.current_hdmi_mode = HDMI_MODE_AUTO;
	it66353_gdev.vars.rx_deskew_err = 0;
	it66353_eq_reset_state();
	it66353_eq_reset_txoe_ready();
	it66353_dump_opts();
}

#if CHECK_DEV_PRESENT
static bool it66353_is_device_lost(void)
{
	u8 vendor_id[2] = { 0 };

	vendor_id[0] = it66353_h2swrd(0x00);
	vendor_id[1] = it66353_h2swrd(0x01);
	if (vendor_id[0] == 0x54 && vendor_id[1] == 0x49) {
		return FALSE;
	}
	return TRUE;
}
#endif

static bool it66353_is_device_ready(void)
{
	u8 vendor_id[2] = { 0 };

	vendor_id[0] = it66353_h2swrd(0x00);
	vendor_id[1] = it66353_h2swrd(0x01);
	if (vendor_id[0] == 0x54 && vendor_id[1] == 0x49) {
		vendor_id[0] = 0;
		vendor_id[1] = 0;

		vendor_id[1] = it66353_h2swrd(0x03);
		if (vendor_id[1] == 0x66) {
			vendor_id[0] = it66353_h2swrd(0x02);
			if (vendor_id[0] == 0x35) {
				it66353_gdev.vars.Rev = it66353_h2swrd(0x04);
				dev_info(g_it66353->dev, "Find 6635 %02x !! \r\n",
					 (int)it66353_gdev.vars.Rev);
				return TRUE;
			}
		} else if (vendor_id[1] == 0x35) {
			vendor_id[0] = it66353_h2swrd(0x04);
			if (vendor_id[0] == 0x66) {
				it66353_gdev.vars.Rev = it66353_h2swrd(0x05);
				dev_info(g_it66353->dev, "Find 6635x %02x !! \r\n",
					 (int)it66353_gdev.vars.Rev);
				return TRUE;
			}
		}
	}
	dev_info(g_it66353->dev, "Find 6635 fail !!\r\n");

	return FALSE;
}



bool it66353_read_edid(u8 block, u8 offset, int length, u8 *edid_buffer)
{
	bool result = false;
	int off = block * 128 + offset;
	u8 reg3C;
	int retry = 0;

	offset = off % 256;
	reg3C = it66353_h2swrd(0x3C);

__RETRY:

	it66353_h2swset(0x3C, 0x01, 0x01);	// Enable PC DDC Mode
	it66353_h2swset(0x38, 0x08, 0x08);	// Enable DDC Command Fail Interrupt
	it66353_h2swset(0x37, 0x80, 0x80);	// Enable DDC Bus Hang Interrupt

	it66353_h2swwr(0x3D, 0x09);		// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0xA0);		// EDID Address
	it66353_h2swwr(0x3F, offset);		// EDID Offset
	it66353_h2swwr(0x40, length);		// Read ByteNum[7:0]
	it66353_h2swwr(0x41, block/2);		// EDID Segment

	if (_tx_is_sink_hpd_high()) {
		it66353_h2swwr(0x3D, 0x03);			// EDID Read Fire

		if (_tx_ddcwait()) {
			it66353_h2swbrd(0x42, length, edid_buffer);
			result = true;
		} else {
			dev_err(g_it66353->dev, "ERROR: DDC EDID Read Fail !!!\r\n");
			if (retry > 0) {
				retry--;
				msleep(100);
				goto __RETRY;
			}
		}
	} else {
		dev_err(g_it66353->dev,
			"Abort EDID read becasue of detecting unplug !!!\r\n");
	}

	it66353_h2swwr(0x3C, reg3C);		// restore PC DDC Mode

	return result;
}

/**
 * it66353_read_one_block_edid - will read 128 byte EDID data
 *
 * @block: EDID block number. (0,1,2 or 3)
 *
 * @edid_buffer: 128 byte EDID data buffer to store data.
 *
 * Read 128 byte EDID data from assigned block.
 */
bool it66353_read_one_block_edid(u8 block, u8 *edid_buffer)
{
	u8 offset;
	u8 i;
	u8 read_len = 16;

	offset = 0;
	for (i = 0; i < 128 / read_len; i++) {
		if (it66353_read_edid(block, offset, read_len, edid_buffer)) {
			edid_buffer += read_len;
			offset += read_len;
			continue;
		} else {
			dev_err(g_it66353->dev,
				"ERROR: read edid block 0, offset %d, length %d fail.\r\r\n",
				(int)offset, (int)read_len);
			return false;
		}
	}

	return true;
}

static void it66353_parse_edid_for_vsdb(u8 *edid)
{
	int off;
	u8 tag;
	u8 len;

	// to find HDMI2.0 VSDB in EDID

	if (edid[0] == 0x02) {			// CTA ext tag
		off = 4;
		while (off < 0x100) {
			tag = (edid[off] >> 5) & 0x7;
			len = (edid[off] & 0x1F) + 1;

			if (tag == 0x03) {		// VSDB
				if ((edid[off+1] == 0xD8) &&
				    (edid[off+2] == 0x5D) &&
				    (edid[off+3] == 0xC4)) {
					it66353_gdev.vars.is_hdmi20_sink = 1;
					break;
				}
			}

			if (len == 0)
				break;
			off += len;
		}
	}

	dev_info(g_it66353->dev, "HDMI2 sink=%d\n", it66353_gdev.vars.is_hdmi20_sink);
}

/**
 * it66353_parse_edid_for_phyaddr - parse necessary data for RX
 * EDID
 *
 * @edid: 128 byte EDID data buffer that contains HDMI CEA ext
 *
 * Before set RX EDID, must call it66353_parse_edid_cea_ext to
 * initialize some variables.
 */
void it66353_parse_edid_for_phyaddr(u8 *edid)
{
	int off;
	u8 tag;
	u8 len;

	// to find VSDB in EDID

	if (edid[0] == 0x02) {			// CTA ext tag
		off = 4;
		while (off < 0x100) {
			tag = (edid[off] >> 5) & 0x7;
			len = (edid[off] & 0x1F) + 1;

			if (tag == 0x03) {		// VSDB
				if ((edid[off + 1] == 0x03) && (edid[off + 2] == 0x0C) &&
				    (edid[off + 3] == 0x00)) {
					it66353_gdev.vars.VSDBOffset = ((u8)off) + 0x80 + 4;
					it66353_gdev.vars.PhyAdr[0] = (edid[off + 4] >> 4) & 0xF;
					it66353_gdev.vars.PhyAdr[1] = edid[off + 4] & 0xF;
					it66353_gdev.vars.PhyAdr[2] = (edid[off + 5] >> 4) & 0xF;
					it66353_gdev.vars.PhyAdr[3] = edid[off + 5] & 0xF;
					it66353_gdev.vars.EdidChkSum[1] =
						(0x100 - edid[0x7F] -
						edid[off + 4] -
						edid[off + 5]) & 0xFF;
					break;
				}
			}

			if (len == 0)
				break;
			off += len;
		}
	}
}

static void it66353_setup_edid_ram_step2(u8 block)
{
	int i;
	u8 wcount = 16;
	u8 phyAB, phyCD;
	u8 mask;
	u16 sum;

	dev_info(g_it66353->dev, "Set RX EDID step2...\r\n");

	if (block == 0) {
		for (i = 0; i < 4; i++) {
			_rx_edid_set_chksum(i, it66353_gdev.vars.EdidChkSum[0]);
		}
	}

	if (block == 1) {
		it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr | 0x01);
		it66353_h2rxset(0x4C, 0x0F, 0x0F);

		it66353_h2swwr(0xe9, it66353_gdev.vars.VSDBOffset);		// VSDB_Offset
		dev_info(g_it66353->dev, "VSDB=%02x\r\n", it66353_gdev.vars.VSDBOffset);

		// fill 0xF, if there is no zero address
		if (it66353_gdev.vars.PhyAdr[0] && it66353_gdev.vars.PhyAdr[1] &&
		    it66353_gdev.vars.PhyAdr[2] && it66353_gdev.vars.PhyAdr[3]) {
			it66353_gdev.vars.PhyAdr[0] = 0xF;
			it66353_gdev.vars.PhyAdr[1] = 0xF;
			it66353_gdev.vars.PhyAdr[2] = 0xF;
			it66353_gdev.vars.PhyAdr[3] = 0xF;
		}

		phyAB = (it66353_gdev.vars.PhyAdr[0] << 4) | (it66353_gdev.vars.PhyAdr[1] & 0xF);
		phyCD = (it66353_gdev.vars.PhyAdr[2] << 4) | (it66353_gdev.vars.PhyAdr[3] & 0xF);

		for (i = 0; i < 4; i++) {
			it66353_h2swwr(0xd9 + i * 2, phyAB);	// Port0 VSDB_AB
			it66353_h2swwr(0xda + i * 2, phyCD);	// Port0 VSDB_CD
		}

		for (i = 0; i < 4; i++) {
			if (it66353_gdev.vars.PhyAdr[i] == 0) {
				mask = 0xF0 >> (4 * (i & 0x01));
				for (wcount = 0; wcount < 4; wcount++) {
					phyAB = wcount + 1;
					if (mask == 0xF0) {
						phyAB = phyAB << 4;
					}
					it66353_h2swset(0xd9 + wcount * 2 + i / 2,
							mask, phyAB);
				}
				break;
			}
		}

		for (i = 0; i < 4; i++) {
			phyAB = it66353_h2swrd(0xd9 + i * 2);	// Port(i) VSDB_AB
			phyCD = it66353_h2swrd(0xda + i * 2);	// Port(i) VSDB_CD

			// Port(i) Block1_ChkSum
			sum = (0x100 - it66353_gdev.vars.EdidChkSum[1] - phyAB - phyCD) & 0xFF;
			it66353_h2swwr(0xe2 + i * 2, (u8)sum);

			// if (it66353_gdev.vars.Rev >= 0xC) {
				#if 0
				switch (i) {
				case 0:
					mask = 1 << 1;
					break;
				case 1:
					mask = 1 << 2;
					break;
				case 2:
					mask = 1 << 0;
					break;
				case 3:
					mask = 1 << 3;
					break;
				default:
					mask = 1 << 3;
					break;
				}
				#endif

				mask = 1 << i;

				it66353_h2rxset(0x4C, 0x0F, mask);

				it66353_h2rxedidwr(it66353_gdev.vars.VSDBOffset, &phyAB, 1);
				it66353_h2rxedidwr(it66353_gdev.vars.VSDBOffset + 1, &phyCD, 1);
				phyAB = (u8)sum;
				it66353_h2rxedidwr(128 + 127, &phyAB, 1);
			// }

		}

		it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr);
	}
}

static void it66353_ddc_abort(void)
{
	u8 reg3C = it66353_h2swrd(0x3C);
	u8 i, j, uc;

	it66353_h2swset(0x3C, 0x01, 0x01);
	for (i = 0; i < 2; i++) {
		it66353_h2swwr(0x3D, 0x0F);
		for (j = 0; j < 50; j++) {
			uc = it66353_h2swrd(0x1B);
			if (uc & 0x80) {
				// DDC_FW_Stus_DONE
				break;
			}
			if (uc & 0x38) {
				// DDC has something error
				dev_err(g_it66353->dev, "ERROR: DDC 0x1B=%02X\r\n", uc);
				break;
			}
			msleep(1);
		}
	}
	it66353_h2swwr(0x3D, 0x0A);
	it66353_h2swwr(0x3D, 0x09);
	it66353_h2swwr(0x3C, reg3C);
}

static bool it66353_update_edid(u8 block, u8 *edid_buf, u8 flag)
{
	u8 i;
	bool ret;
	u8 retry = 0;

__RETRY_EDID_READ:
	if (it66353_gdev.opts.tx_opt->CopyEDIDFromSink) {
		ret = it66353_read_one_block_edid(block, edid_buf);
		if (false == ret) {
			dev_err(g_it66353->dev, "ERROR: read edid block 0\r\n");
			if (retry < 3) {
				retry++;
				it66353_ddc_abort();
				goto __RETRY_EDID_READ;
			}
		}
	} else {
		u8 *def_edid = it66353_gdev.vars.default_edid[flag];

		if (def_edid) {
			for (i = 0; i < 128; i++) {
				edid_buf[i] = def_edid[i+block*128];
			}
			ret = true;
		} else {
			ret = false;
		}
	}

	return ret;
}

bool it66353_setup_edid_ram(u8 flag)
{
	u8 edid_tmp[128];
	u8 extblock;
	u8 i;

	it66353_gdev.vars.spmon = 0;
	it66353_gdev.vars.is_hdmi20_sink = 0;

	if (false == it66353_update_edid(0, edid_tmp, flag)) {
		goto __err_exit;
	}

	if ((edid_tmp[0x08] == 0x5A) &&
		 (edid_tmp[0x09] == 0x63) &&
		 (edid_tmp[0x0a] == 0x32) &&
		 (edid_tmp[0x0b] == 0x0e)) {
		it66353_gdev.vars.spmon = 1;
	}

	if ((edid_tmp[0x71] == 0x4C) &&
		 (edid_tmp[0x72] == 0x47) &&
		 (edid_tmp[0x74] == 0x54) &&
		 (edid_tmp[0x75] == 0x56) &&
		 (edid_tmp[0x7F] == 0x63)) {
		it66353_gdev.vars.spmon = 2;
	}

	if ((edid_tmp[0x60] == 0x48) &&
		 (edid_tmp[0x61] == 0x4C) &&
		 (edid_tmp[0x63] == 0x32) &&
		 (edid_tmp[0x64] == 0x37) &&
		 (edid_tmp[0x65] == 0x36) &&
		 (edid_tmp[0x66] == 0x45) &&
		 (edid_tmp[0x67] == 0x38) &&
		 (edid_tmp[0x68] == 0x56)) {
		it66353_gdev.vars.spmon = 3;
	}

	// read Ext block no
	extblock = edid_tmp[0x7E];
	it66353_gdev.vars.EdidChkSum[0] = edid_tmp[0x7F];

	#if FIX_EDID_FOR_ATC_4BLOCK_CTS
	if (extblock > 1) {
		edid_tmp[0x7E] = 1;
		it66353_gdev.vars.EdidChkSum[0] = _rx_calc_edid_sum(edid_tmp);
	}
	#endif

	_pr_buf(edid_tmp, 128);

	if (it66353_gdev.opts.tx_opt->CopyEDIDFromSink) {
		// update EDID block 0 for all port
		it66353_set_internal_EDID(0, edid_tmp, EDID_PORT_ALL);
	} else {
		// update EDID block 0 for assigned port
		it66353_set_internal_EDID(0, edid_tmp, (1 << flag));
	}
	it66353_setup_edid_ram_step2(0);

	if (extblock > 3) {
		dev_err(g_it66353->dev, "Warning: Extblock = %d\r\n", extblock);
		extblock = 3;
	}

	for (i = 1; i <= extblock; i++) {
		if (false == it66353_update_edid(i, edid_tmp, flag)) {
			goto __err_exit;
		}

		it66353_gdev.vars.VSDBOffset = 0;
		it66353_parse_edid_for_vsdb(edid_tmp);

		if (i == 1) {			// assume our sink has only 2 block EDID
			if (it66353_gdev.vars.spmon == 2) {
				if (edid_tmp[0x7F] != 0x6A) {
					it66353_gdev.vars.spmon = 0;
				}
			}

			_pr_buf(edid_tmp, 128);

			if (it66353_gdev.opts.tx_opt->CopyEDIDFromSink) {
				// update EDID block 0 for all port
				it66353_set_internal_EDID(1, edid_tmp, EDID_PORT_ALL);
			} else {
				// update EDID block 0 for assigned port
				it66353_set_internal_EDID(1, edid_tmp, (1<<flag));
			}

			if (it66353_gdev.opts.tx_opt->ParsePhysicalAddr) {
				it66353_parse_edid_for_phyaddr(edid_tmp);

				if (it66353_gdev.vars.VSDBOffset) {
					it66353_setup_edid_ram_step2(1);

					// break;
					// break parsing here make the 4 block EDID CTS fail
				}
			}
		}
	} // for i

	return true;

__err_exit:
	return false;
}

static void it66353_dev_loop(void)
{
	#if CHECK_DEV_PRESENT
	if (dev_state < DEV_WAIT_DEVICE_READY) {
		if (it66353_is_device_lost()) {
			dev_state = DEV_FW_VAR_INIT;
		}
	}
	#endif
	switch (dev_state) {
	case DEV_DEVICE_LOOP:
		it66353_fsm();
		it66353_irq();
		it66353_rx_update_ced_err_from_hw();
		break;

	case DEV_DEVICE_INIT:
		if (it66353_device_init()) {
			dev_state = DEV_DEVICE_LOOP;
		}
		break;

	case DEV_WAIT_DEVICE_READY:
		if (it66353_is_device_ready()) {
			dev_state = DEV_DEVICE_INIT;
		}
		break;

	case DEV_FW_VAR_INIT:
		it66353_vars_init();
		dev_state = DEV_WAIT_DEVICE_READY;
		break;

	default:
		break;
	}

}

// APIs:

void it66353_dump_opts(void)
{
	dev_info(g_it66353->dev, ".rx_opt->tag1=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->tag1);
	dev_info(g_it66353->dev, ".rx_opt->EnRxDDCBypass=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->EnRxDDCBypass);
	dev_info(g_it66353->dev, ".rx_opt->EnRxPWR5VBypass=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->EnRxPWR5VBypass);
	dev_info(g_it66353->dev, ".rx_opt->EnRxHPDBypass=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->EnRxHPDBypass);
	dev_info(g_it66353->dev, ".rx_opt->TryFixedEQFirst=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->TryFixedEQFirst);
	dev_info(g_it66353->dev, ".rx_opt->EnableAutoEQ=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->EnableAutoEQ);
	dev_info(g_it66353->dev, ".rx_opt->NonActivePortReplyHPD=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->NonActivePortReplyHPD);
	dev_info(g_it66353->dev, ".rx_opt->DisableEdidRam=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->DisableEdidRam);
	dev_info(g_it66353->dev, ".rx_opt->DefaultEQ=%x %x %x\r\n",
		 it66353_gdev.opts.active_rx_opt->DefaultEQ[0],
		 it66353_gdev.opts.active_rx_opt->DefaultEQ[1],
		 it66353_gdev.opts.active_rx_opt->DefaultEQ[2]);
	dev_info(g_it66353->dev, ".rx_opt->FixIncorrectHdmiEnc=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->FixIncorrectHdmiEnc);
	dev_info(g_it66353->dev, ".rx_opt->HPDOutputInverse=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->HPDOutputInverse);
	dev_info(g_it66353->dev, ".rx_opt->HPDTogglePeriod=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->HPDTogglePeriod);
	dev_info(g_it66353->dev, ".rx_opt->TxOEAlignment=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->TxOEAlignment);
	dev_info(g_it66353->dev, ".rx_opt->str_size=%d\r\n",
		 it66353_gdev.opts.active_rx_opt->str_size);

	dev_info(g_it66353->dev, ".tx_opt->tag1=%d\r\n", it66353_gdev.opts.tx_opt->tag1);
	dev_info(g_it66353->dev, ".tx_opt->EnTxPNSwap=%d\r\n",
		 it66353_gdev.opts.tx_opt->EnTxPNSwap);
	dev_info(g_it66353->dev, ".tx_opt->EnTxChSwap=%d\r\n",
		 it66353_gdev.opts.tx_opt->EnTxChSwap);
	dev_info(g_it66353->dev, ".tx_opt->EnTxVCLKInv=%d\r\n",
		 it66353_gdev.opts.tx_opt->EnTxVCLKInv);
	dev_info(g_it66353->dev, ".tx_opt->EnTxOutD1t=%d\r\n",
		 it66353_gdev.opts.tx_opt->EnTxOutD1t);
	dev_info(g_it66353->dev, ".tx_opt->CopyEDIDFromSink=%d\r\n",
		 it66353_gdev.opts.tx_opt->CopyEDIDFromSink);
	dev_info(g_it66353->dev, ".tx_opt->ParsePhysicalAddr=%d\r\n",
		 it66353_gdev.opts.tx_opt->ParsePhysicalAddr);
	dev_info(g_it66353->dev, ".tx_opt->TurnOffTx5VWhenSwitchPort=%d\r\n",
		 it66353_gdev.opts.tx_opt->TurnOffTx5VWhenSwitchPort);
	dev_info(g_it66353->dev, ".tx_opt->str_size=%d\r\n",
		 it66353_gdev.opts.tx_opt->str_size);

	dev_info(g_it66353->dev, ".dev_opt->tag1=%d\r\n",
		 it66353_gdev.opts.dev_opt->tag1);
	dev_info(g_it66353->dev, ".dev_opt->SwAddr=%d\r\n",
		 it66353_gdev.opts.dev_opt->SwAddr);
	dev_info(g_it66353->dev, ".dev_opt->RxAddr=%d\r\n",
		 it66353_gdev.opts.dev_opt->RxAddr);
	dev_info(g_it66353->dev, ".dev_opt->CecAddr=%d\r\n",
		 it66353_gdev.opts.dev_opt->CecAddr);
	dev_info(g_it66353->dev, ".dev_opt->EdidAddr=%d\r\n",
		 it66353_gdev.opts.dev_opt->EdidAddr);
	dev_info(g_it66353->dev, ".dev_opt->ForceRxOn=%d\r\n",
		 it66353_gdev.opts.dev_opt->ForceRxOn);
	dev_info(g_it66353->dev, ".dev_opt->RxAutoPowerDown=%d\r\n",
		 it66353_gdev.opts.dev_opt->RxAutoPowerDown);
	dev_info(g_it66353->dev, ".dev_opt->DoTxPowerDown=%d\r\n",
		 it66353_gdev.opts.dev_opt->DoTxPowerDown);
	dev_info(g_it66353->dev, ".dev_opt->TxPowerDownWhileWaitingClock=%d\r\n",
		 it66353_gdev.opts.dev_opt->TxPowerDownWhileWaitingClock);
	dev_info(g_it66353->dev, ".dev_opt->str_size=%d\r\n",
		 it66353_gdev.opts.dev_opt->str_size);
}

#define BUF_LEN 16
static void it66353_dump_register(u8 addr, char *reg_desc)
{
	u8 regbuf[BUF_LEN];
	int i, j;

	// print reg description
	dev_info(g_it66353->dev, reg_desc);

	// print table
	dev_info(g_it66353->dev, "   | ");
	for (j = 0; j < BUF_LEN; j++) {
		if (j == (BUF_LEN/2)) {
			dev_info(g_it66353->dev, "- ");
		}
		dev_info(g_it66353->dev, "%02X ", j);
	}
	dev_info(g_it66353->dev, "\n");

	// print split line
	for (j = 0; j < BUF_LEN + 2; j++) {
		dev_info(g_it66353->dev, "---");
	}
	dev_info(g_it66353->dev, "\n");

	// print register values
	for (i = 0; i < 256; i += BUF_LEN) {
		dev_info(g_it66353->dev, "%02X | ", i);
		it66353_i2c_read(addr, i, BUF_LEN, regbuf);
		for (j = 0; j < BUF_LEN; j++) {
			if (j == (BUF_LEN / 2)) {
				dev_info(g_it66353->dev, "- ");
			}
			dev_info(g_it66353->dev, "%02x ", regbuf[j]);
		}
		dev_info(g_it66353->dev, "\n");
	}
	dev_info(g_it66353->dev, "\n");
}

void it66353_dump_register_all(void)
{
	it66353_dump_register(it66353_gdev.opts.dev_opt->SwAddr, "\n*** Switch Register:\n");

	it66353_dump_register(it66353_gdev.opts.dev_opt->RxAddr, "\n*** RX Register(0):\n");
	it66353_chgrxbank(3);
	it66353_dump_register(it66353_gdev.opts.dev_opt->RxAddr, "\n*** RX Register(3):\n");
	it66353_chgrxbank(5);
	it66353_dump_register(it66353_gdev.opts.dev_opt->RxAddr, "\n*** RX Register(5):\n");
	it66353_chgrxbank(0);

	// dump EDID RAM, enable EDID RAM i2c address
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr | 0x01);
	it66353_h2rxset(0x4C, 0x30, 0x00);
	it66353_dump_register(it66353_gdev.opts.dev_opt->EdidAddr, "\n*** EDID Port 0:\n");
	it66353_h2rxset(0x4C, 0x30, 0x10);
	it66353_dump_register(it66353_gdev.opts.dev_opt->EdidAddr, "\n*** EDID Port 1:\n");
	it66353_h2rxset(0x4C, 0x30, 0x20);
	it66353_dump_register(it66353_gdev.opts.dev_opt->EdidAddr, "\n*** EDID Port 2:\n");
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr); // disable EDID RAM i2c address

	#if EN_CEC
	it66353_dump_register(it66353_gdev.opts.dev_opt->CecAddr, "\n*** CEC Register:\n");
	#endif
}

static bool it66353_write_edid(u8 block, u8 offset, int length, u8 *data_buffer)
{
	bool result = false;
	int off = block * 128 + offset;
	u8 reg3C;
	u8 segment = off / 256;

	offset = off % 256;
	reg3C = it66353_h2swrd(0x3C);

	it66353_h2swset(0xF5, 0x80, (1 << 7));
	it66353_h2swset(0x3C, 0x01, 0x01);	// disable DDCRegen by set RegTxMastersel=1

	it66353_h2swset(0x3C, 0x01, 0x01);	// Enable PC DDC Mode
	it66353_h2swset(0x38, 0x08, 0x08);	// Enable DDC Command Fail Interrupt
	it66353_h2swset(0x37, 0x80, 0x80);	// Enable DDC Bus Hang Interrupt

	it66353_h2swwr(0x3D, 0x09);		// DDC FIFO Clear
	it66353_h2swwr(0x3E, 0xA0);		// EDID Address
	it66353_h2swwr(0x3F, offset);		// EDID Offset
	it66353_h2swwr(0x40, length);		// Read ByteNum[7:0]
	it66353_h2swwr(0x41, segment / 2);	// EDID Segment

	while (length) {
		it66353_h2swwr(0x42, *data_buffer);
		length--;
		data_buffer++;
	}

	it66353_h2swwr(0x3D, 0x07);		// EDID Write Fire

	if (_tx_ddcwait()) {
		result = true;
	} else {
		dev_err(g_it66353->dev, "ERROR: DDC EDID Write Fail !!!\r\n");
	}

	it66353_h2swwr(0x3C, reg3C);		// restore PC DDC Mode

	it66353_h2swset(0xF5, 0x80, (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass << 7));
	if (it66353_gdev.opts.active_rx_opt->EnRxDDCBypass == 0) {
		// enable DDCRegen by set RegTxMastersel=1
		it66353_h2swset(0x3C, 0x01, 0x00);
	}

	return result;
}

bool it66353_write_one_block_edid(u8 block, u8 *edid_buffer)
{
	u8 offset;
	u8 i;
	u8 op_len = 16;

	offset = 0;
	for (i = 0; i < 128 / op_len; i++) {
		if (it66353_write_edid(block, offset, op_len, edid_buffer)) {
			edid_buffer += op_len;
			offset += op_len;
			continue;
		} else {
			dev_err(g_it66353->dev,
				"ERROR: write edid block 0, offset %d, length %d fail.\r\r\n",
				(int)offset, (int)op_len);
			return false;
		}
	}

	return true;
}

static u8 __maybe_unused it66353_is_5v_present(u8 port)
{
	if (port < RX_PORT_COUNT) {
		if (it66353_get_port_info0(port, PI_5V)) {
			return 1;
		}
	} else {
		dev_err(g_it66353->dev, "Invalid port number:%d\r\n", port);
	}

	return 0;
}

static u8 __maybe_unused it66353_is_clock_detected(u8 port)
{
	if (port < RX_PORT_COUNT) {
		if (it66353_get_port_info0(port,
					   (PI_CLK_STABLE | PI_CLK_VALID | PI_5V))) {
			return 1;
		}
	} else {
		dev_err(g_it66353->dev, "Invalid port number:%d\r\n", port);
	}

	return 0;
}

bool it66353_set_active_port(u8 port)
{
	if (port < RX_PORT_COUNT) {
		if (it66353_gdev.vars.Rx_active_port != port) {
			it66353_gdev.vars.Rx_new_port = port;
			it66353_fsm_chg_delayed(RX_PORT_CHANGE);

			return 1;
		}
	} else {
		dev_err(g_it66353->dev, "Invalid port number:%d\r\n", port);
	}

	return 0;
}

u8 it66353_get_active_port(void)
{
	return it66353_gdev.vars.Rx_active_port;
}

void it66353_set_option(IT6635_DEVICE_OPTION *Opts)
{
	if (Opts) {
		// it66353_gdev.opts.EnableAutoEQ = Opts->EnableAutoEQ;
		// it66353_gdev.opts.rx_opt->EnRxDDCBypass = Opts->EnRxDDCBypass;
	}
}

void it66353_get_option(IT6635_DEVICE_OPTION *Opts)
{
	if (Opts) {
		// Opts->EnableAutoEQ = it66353_gdev.opts.EnableAutoEQ;
		// Opts->EnRxDDCBypass = it66353_gdev.opts.rx_opt->EnRxDDCBypass;
	}
}

void it66353_dev_restart(void)
{
	// it66353_gdev.vars.Rx_prev_port = -1;
	it66353_gdev.vars.state_sys_fsm = RX_UNPLUG;

	dev_state = DEV_WAIT_DEVICE_READY;
}

u8 it66353_get_RS(void)
{
	// return it66353_gdev.EQ.FixedRsIndex[it66353_gdev.vars.Rx_active_port];
	return 0;
}

void it66353_set_RS(u8 rs_idx0, u8 rs_idx1, u8 rs_idx2)
{
	u8 rs[3];
	if ((rs_idx0 < 14) && (rs_idx1 < 14) && (rs_idx2 < 14)) {
		// it66353_gdev.EQ.FixedRsIndex[it66353_gdev.vars.Rx_active_port] = rs_index;
		rs[0] = it66353_rs_value[rs_idx0] | 0x80;
		rs[1] = it66353_rs_value[rs_idx1] | 0x80;
		rs[2] = it66353_rs_value[rs_idx2] | 0x80;
		it66353_rx_set_rs_3ch(rs);
		it66353_chgrxbank(3);
		it66353_h2rxbrd(0x27, 3, rs);
		// it66353_h2rxset(0x22, 0x40, 0x00);
		it66353_chgrxbank(0);
		dev_info(g_it66353->dev, "==> RS set to %02x %02x %02x\r\n",
			 rs[0], rs[1], rs[2]);

	}
}

void it66353_set_ch_RS(u8 ch, u8 rs_index)
{
	u8 rs;
	if (rs_index < 14) {
		rs = it66353_rs_value[rs_index] | 0x80;
		it66353_rx_set_rs(ch, rs);
	}
}

void it66353_set_rx_hpd(u8 hpd_value)
{
	_rx_set_hpd(it66353_gdev.vars.Rx_active_port, hpd_value, TERM_FOLLOW_HPD);
}

/*
 * it66353_set_internal_EDID - write data to EDID RAM
 *
 * @edid: 128 byte EDID data buffer.
 *
 * @block: EDID block number (0,1 or 2)
 *
 * target_port is a bitmap from 0x1 to 0xF
 *
 * ex: set port 1 EDID: target_port = EDID_PORT_1
 *	 set port 1,3 EDID: target_port = EDID_PORT_1|EDID_PORT_3
 */
void it66353_set_internal_EDID(u8 block, u8 *edid, u8 target_port)
{
	int i;
	u8 wcount = 16;

	if (block > 1) {
		dev_err(g_it66353->dev, "Invalid block %d\r\n", block);
		return;
	}
	// enable EDID RAM i2c address
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr | 0x01);
	// for block 1, select port to be written
	it66353_h2rxset(0x4C, 0x0F, target_port);

	for (i = 0; i < 128; i += wcount) {
		it66353_h2rxedidwr(i + 128 * block, edid, wcount);
		edid += wcount;
	}
	// disable EDID RAM i2c address
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr);
}

/**
 * it66353_get_internal_EDID - read data from EDID RAM
 *
 * @edid: 128 byte EDID data buffer.
 *
 * @block: EDID block number (0,1,2 or 3)
 *
 */
void it66353_get_internal_EDID(u8 block, u8 *edid, u8 target_port)
{
	int i;
	u8 wcount = 16;

	if (block > 1) {
		dev_err(g_it66353->dev, "Invalid block %d\r\n", block);
		return;
	}
	if (target_port > 2) {
		dev_err(g_it66353->dev, "Invalid port %d\r\n", target_port);
		return;
	}
	// enable EDID RAM i2c address
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr | 0x01);
	it66353_h2rxset(0x4C, 0x30, target_port << 4);

	for (i = 0; i < 128; i += wcount) {
		it66353_h2rxedidrd(i + 128 * block, edid, wcount);
		edid += wcount;
	}
	// disable EDID RAM i2c address
	it66353_h2rxwr(0x4B, it66353_gdev.opts.dev_opt->EdidAddr);
}


/*
 * it66353_change_default_RS - set the default RS index for each
 * port
 *
 * @port: port number can be P0~P3.
 *
 * @new_rs_idx: RS index from 0 to 13
 *
 * @update_hw: 0: only update the vaule in RAM
 *			 1: update the value to RAM and Hardware register
 *				(for active port only)
 *
 */
void it66353_change_default_RS(u8 port, u8 new_rs_idx0, u8 new_rs_idx1,
			       u8 new_rs_idx2, u8 update_hw)
{
	if (port <= RX_PORT_3) {
		it66353_gdev.opts.rx_opt[port]->DefaultEQ[0] = new_rs_idx0;
		it66353_gdev.opts.rx_opt[port]->DefaultEQ[1] = new_rs_idx1;
		it66353_gdev.opts.rx_opt[port]->DefaultEQ[2] = new_rs_idx2;
		if (update_hw && (port == it66353_gdev.vars.Rx_active_port)) {
			it66353_set_RS(new_rs_idx0, new_rs_idx1, new_rs_idx2);
		}
	} else {
		dev_err(g_it66353->dev, "Invalid port number:%d\r\n", port);
	}
}


/*
 * it66353_force_rx_hpd :
 * to force active port HPD low or auto control by driver
 *
 * @hpd_state: 0: Force HPD of active port to low
 *			 1: HPD of active port is controlled by it6635
 *				driver
 *
 * it66353_gdev.vars.force_hpd_state will reset to SW_HPD_AUTO when
 * active port changed by it66353_fsm_chg(RX_PORT_CHANGE)
 *
 */
void it66353_force_rx_hpd(u8 hpd_state)
{
	it66353_gdev.vars.force_hpd_state = hpd_state;

	if (hpd_state) {	// hpd 0 --> hpd auto
		// nothing to do here:
		// hpd will be controlled by it66353_fsm()
	} else {			// hpd auto --> hpd 0
		_rx_set_hpd(it66353_gdev.vars.Rx_active_port, hpd_state, TERM_FOLLOW_HPD);
		it66353_fsm_chg_delayed(RX_UNPLUG);
	}
}


/*
 * it66353_toggle_hpd : to make HPD toggle for active port with a
 * given duration.
 *
 * @ms_duration: duration of HPD low in millisecond.
 * range from 10ms to 12700ms
 *
 */
bool it66353_toggle_hpd(u16 ms_duration)
{
	u8 timeout = 0;
	bool ret = true;

	if (ms_duration <= (0x7F * 10)) {
		timeout = ms_duration / 10;
	} else if (ms_duration <= (0x7F * 100)) {
		timeout = ms_duration/100;
		timeout |= (BIT(7));
	} else {
		ret = false;
	}

	_rx_set_hpd(it66353_gdev.vars.Rx_active_port, 0, TERM_FOLLOW_HPD);
	_tx_scdc_write(0x20, 0x00);
	it66353_gdev.vars.hpd_toggle_timeout = timeout;
	it66353_fsm_chg(RX_TOGGLE_HPD);

	return ret;
}
