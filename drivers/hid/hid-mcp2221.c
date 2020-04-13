// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCP2221A - Microchip USB to I2C Host Protocol Bridge
 *
 * Copyright (c) 2020, Rishi Gupta <gupt21@gmail.com>
 *
 * Datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/20005565B.pdf
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/i2c.h>
#include "hid-ids.h"

/* Commands codes in a raw output report */
enum {
	MCP2221_I2C_WR_DATA = 0x90,
	MCP2221_I2C_WR_NO_STOP = 0x94,
	MCP2221_I2C_RD_DATA = 0x91,
	MCP2221_I2C_RD_RPT_START = 0x93,
	MCP2221_I2C_GET_DATA = 0x40,
	MCP2221_I2C_PARAM_OR_STATUS	= 0x10,
	MCP2221_I2C_SET_SPEED = 0x20,
	MCP2221_I2C_CANCEL = 0x10,
};

/* Response codes in a raw input report */
enum {
	MCP2221_SUCCESS = 0x00,
	MCP2221_I2C_ENG_BUSY = 0x01,
	MCP2221_I2C_START_TOUT = 0x12,
	MCP2221_I2C_STOP_TOUT = 0x62,
	MCP2221_I2C_WRADDRL_TOUT = 0x23,
	MCP2221_I2C_WRDATA_TOUT = 0x44,
	MCP2221_I2C_WRADDRL_NACK = 0x25,
	MCP2221_I2C_MASK_ADDR_NACK = 0x40,
	MCP2221_I2C_WRADDRL_SEND = 0x21,
	MCP2221_I2C_ADDR_NACK = 0x25,
	MCP2221_I2C_READ_COMPL = 0x55,
};

/*
 * There is no way to distinguish responses. Therefore next command
 * is sent only after response to previous has been received. Mutex
 * lock is used for this purpose mainly.
 */
struct mcp2221 {
	struct hid_device *hdev;
	struct i2c_adapter adapter;
	struct mutex lock;
	struct completion wait_in_report;
	u8 *rxbuf;
	u8 txbuf[64];
	int rxbuf_idx;
	int status;
	u8 cur_i2c_clk_div;
};

/*
 * Default i2c bus clock frequency 400 kHz. Modify this if you
 * want to set some other frequency (min 50 kHz - max 400 kHz).
 */
static uint i2c_clk_freq = 400;

/* Synchronously send output report to the device */
static int mcp_send_report(struct mcp2221 *mcp,
					u8 *out_report, size_t len)
{
	u8 *buf;
	int ret;

	buf = kmemdup(out_report, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* mcp2221 uses interrupt endpoint for out reports */
	ret = hid_hw_output_report(mcp->hdev, buf, len);
	kfree(buf);

	if (ret < 0)
		return ret;
	return 0;
}

/*
 * Send o/p report to the device and wait for i/p report to be
 * received from the device. If the device does not respond,
 * we timeout.
 */
static int mcp_send_data_req_status(struct mcp2221 *mcp,
			u8 *out_report, int len)
{
	int ret;
	unsigned long t;

	reinit_completion(&mcp->wait_in_report);

	ret = mcp_send_report(mcp, out_report, len);
	if (ret)
		return ret;

	t = wait_for_completion_timeout(&mcp->wait_in_report,
							msecs_to_jiffies(4000));
	if (!t)
		return -ETIMEDOUT;

	return mcp->status;
}

/* Check pass/fail for actual communication with i2c slave */
static int mcp_chk_last_cmd_status(struct mcp2221 *mcp)
{
	memset(mcp->txbuf, 0, 8);
	mcp->txbuf[0] = MCP2221_I2C_PARAM_OR_STATUS;

	return mcp_send_data_req_status(mcp, mcp->txbuf, 8);
}

/* Cancels last command releasing i2c bus just in case occupied */
static int mcp_cancel_last_cmd(struct mcp2221 *mcp)
{
	memset(mcp->txbuf, 0, 8);
	mcp->txbuf[0] = MCP2221_I2C_PARAM_OR_STATUS;
	mcp->txbuf[2] = MCP2221_I2C_CANCEL;

	return mcp_send_data_req_status(mcp, mcp->txbuf, 8);
}

static int mcp_set_i2c_speed(struct mcp2221 *mcp)
{
	int ret;

	memset(mcp->txbuf, 0, 8);
	mcp->txbuf[0] = MCP2221_I2C_PARAM_OR_STATUS;
	mcp->txbuf[3] = MCP2221_I2C_SET_SPEED;
	mcp->txbuf[4] = mcp->cur_i2c_clk_div;

	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 8);
	if (ret) {
		/* Small delay is needed here */
		usleep_range(980, 1000);
		mcp_cancel_last_cmd(mcp);
	}

	return 0;
}

/*
 * An output report can contain minimum 1 and maximum 60 user data
 * bytes. If the number of data bytes is more then 60, we send it
 * in chunks of 60 bytes. Last chunk may contain exactly 60 or less
 * bytes. Total number of bytes is informed in very first report to
 * mcp2221, from that point onwards it first collect all the data
 * from host and then send to i2c slave device.
 */
static int mcp_i2c_write(struct mcp2221 *mcp,
				struct i2c_msg *msg, int type, u8 last_status)
{
	int ret, len, idx, sent;

	idx = 0;
	sent  = 0;
	if (msg->len < 60)
		len = msg->len;
	else
		len = 60;

	do {
		mcp->txbuf[0] = type;
		mcp->txbuf[1] = msg->len & 0xff;
		mcp->txbuf[2] = msg->len >> 8;
		mcp->txbuf[3] = (u8)(msg->addr << 1);

		memcpy(&mcp->txbuf[4], &msg->buf[idx], len);

		ret = mcp_send_data_req_status(mcp, mcp->txbuf, len + 4);
		if (ret)
			return ret;

		usleep_range(980, 1000);

		if (last_status) {
			ret = mcp_chk_last_cmd_status(mcp);
			if (ret)
				return ret;
		}

		sent = sent + len;
		if (sent >= msg->len)
			break;

		idx = idx + len;
		if ((msg->len - sent) < 60)
			len = msg->len - sent;
		else
			len = 60;

		/*
		 * Testing shows delay is needed between successive writes
		 * otherwise next write fails on first-try from i2c core.
		 * This value is obtained through automated stress testing.
		 */
		usleep_range(980, 1000);
	} while (len > 0);

	return ret;
}

/*
 * Device reads all data (0 - 65535 bytes) from i2c slave device and
 * stores it in device itself. This data is read back from device to
 * host in multiples of 60 bytes using input reports.
 */
static int mcp_i2c_smbus_read(struct mcp2221 *mcp,
				struct i2c_msg *msg, int type, u16 smbus_addr,
				u8 smbus_len, u8 *smbus_buf)
{
	int ret;
	u16 total_len;

	mcp->txbuf[0] = type;
	if (msg) {
		mcp->txbuf[1] = msg->len & 0xff;
		mcp->txbuf[2] = msg->len >> 8;
		mcp->txbuf[3] = (u8)(msg->addr << 1);
		total_len = msg->len;
		mcp->rxbuf = msg->buf;
	} else {
		mcp->txbuf[1] = smbus_len;
		mcp->txbuf[2] = 0;
		mcp->txbuf[3] = (u8)(smbus_addr << 1);
		total_len = smbus_len;
		mcp->rxbuf = smbus_buf;
	}

	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 4);
	if (ret)
		return ret;

	mcp->rxbuf_idx = 0;

	do {
		memset(mcp->txbuf, 0, 4);
		mcp->txbuf[0] = MCP2221_I2C_GET_DATA;

		ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
		if (ret)
			return ret;

		ret = mcp_chk_last_cmd_status(mcp);
		if (ret)
			return ret;

		usleep_range(980, 1000);
	} while (mcp->rxbuf_idx < total_len);

	return ret;
}

static int mcp_i2c_xfer(struct i2c_adapter *adapter,
				struct i2c_msg msgs[], int num)
{
	int ret;
	struct mcp2221 *mcp = i2c_get_adapdata(adapter);

	hid_hw_power(mcp->hdev, PM_HINT_FULLON);

	mutex_lock(&mcp->lock);

	/* Setting speed before every transaction is required for mcp2221 */
	ret = mcp_set_i2c_speed(mcp);
	if (ret)
		goto exit;

	if (num == 1) {
		if (msgs->flags & I2C_M_RD) {
			ret = mcp_i2c_smbus_read(mcp, msgs, MCP2221_I2C_RD_DATA,
							0, 0, NULL);
		} else {
			ret = mcp_i2c_write(mcp, msgs, MCP2221_I2C_WR_DATA, 1);
		}
		if (ret)
			goto exit;
		ret = num;
	} else if (num == 2) {
		/* Ex transaction; send reg address and read its contents */
		if (msgs[0].addr == msgs[1].addr &&
			!(msgs[0].flags & I2C_M_RD) &&
			 (msgs[1].flags & I2C_M_RD)) {

			ret = mcp_i2c_write(mcp, &msgs[0],
						MCP2221_I2C_WR_NO_STOP, 0);
			if (ret)
				goto exit;

			ret = mcp_i2c_smbus_read(mcp, &msgs[1],
						MCP2221_I2C_RD_RPT_START,
						0, 0, NULL);
			if (ret)
				goto exit;
			ret = num;
		} else {
			dev_err(&adapter->dev,
				"unsupported multi-msg i2c transaction\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		dev_err(&adapter->dev,
			"unsupported multi-msg i2c transaction\n");
		ret = -EOPNOTSUPP;
	}

exit:
	hid_hw_power(mcp->hdev, PM_HINT_NORMAL);
	mutex_unlock(&mcp->lock);
	return ret;
}

static int mcp_smbus_write(struct mcp2221 *mcp, u16 addr,
				u8 command, u8 *buf, u8 len, int type,
				u8 last_status)
{
	int data_len, ret;

	mcp->txbuf[0] = type;
	mcp->txbuf[1] = len + 1; /* 1 is due to command byte itself */
	mcp->txbuf[2] = 0;
	mcp->txbuf[3] = (u8)(addr << 1);
	mcp->txbuf[4] = command;

	switch (len) {
	case 0:
		data_len = 5;
		break;
	case 1:
		mcp->txbuf[5] = buf[0];
		data_len = 6;
		break;
	case 2:
		mcp->txbuf[5] = buf[0];
		mcp->txbuf[6] = buf[1];
		data_len = 7;
		break;
	default:
		memcpy(&mcp->txbuf[5], buf, len);
		data_len = len + 5;
	}

	ret = mcp_send_data_req_status(mcp, mcp->txbuf, data_len);
	if (ret)
		return ret;

	if (last_status) {
		usleep_range(980, 1000);

		ret = mcp_chk_last_cmd_status(mcp);
		if (ret)
			return ret;
	}

	return ret;
}

static int mcp_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	int ret;
	struct mcp2221 *mcp = i2c_get_adapdata(adapter);

	hid_hw_power(mcp->hdev, PM_HINT_FULLON);

	mutex_lock(&mcp->lock);

	ret = mcp_set_i2c_speed(mcp);
	if (ret)
		goto exit;

	switch (size) {

	case I2C_SMBUS_QUICK:
		if (read_write == I2C_SMBUS_READ)
			ret = mcp_i2c_smbus_read(mcp, NULL, MCP2221_I2C_RD_DATA,
						addr, 0, &data->byte);
		else
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_DATA, 1);
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ)
			ret = mcp_i2c_smbus_read(mcp, NULL, MCP2221_I2C_RD_DATA,
						addr, 1, &data->byte);
		else
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_DATA, 1);
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_NO_STOP, 0);
			if (ret)
				goto exit;

			ret = mcp_i2c_smbus_read(mcp, NULL,
						MCP2221_I2C_RD_RPT_START,
						addr, 1, &data->byte);
		} else {
			ret = mcp_smbus_write(mcp, addr, command, &data->byte,
						1, MCP2221_I2C_WR_DATA, 1);
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_NO_STOP, 0);
			if (ret)
				goto exit;

			ret = mcp_i2c_smbus_read(mcp, NULL,
						MCP2221_I2C_RD_RPT_START,
						addr, 2, (u8 *)&data->word);
		} else {
			ret = mcp_smbus_write(mcp, addr, command,
						(u8 *)&data->word, 2,
						MCP2221_I2C_WR_DATA, 1);
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_NO_STOP, 1);
			if (ret)
				goto exit;

			mcp->rxbuf_idx = 0;
			mcp->rxbuf = data->block;
			mcp->txbuf[0] = MCP2221_I2C_GET_DATA;
			ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
			if (ret)
				goto exit;
		} else {
			if (!data->block[0]) {
				ret = -EINVAL;
				goto exit;
			}
			ret = mcp_smbus_write(mcp, addr, command, data->block,
						data->block[0] + 1,
						MCP2221_I2C_WR_DATA, 1);
		}
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = mcp_smbus_write(mcp, addr, command, NULL,
						0, MCP2221_I2C_WR_NO_STOP, 1);
			if (ret)
				goto exit;

			mcp->rxbuf_idx = 0;
			mcp->rxbuf = data->block;
			mcp->txbuf[0] = MCP2221_I2C_GET_DATA;
			ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
			if (ret)
				goto exit;
		} else {
			if (!data->block[0]) {
				ret = -EINVAL;
				goto exit;
			}
			ret = mcp_smbus_write(mcp, addr, command,
						&data->block[1], data->block[0],
						MCP2221_I2C_WR_DATA, 1);
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		ret = mcp_smbus_write(mcp, addr, command,
						(u8 *)&data->word,
						2, MCP2221_I2C_WR_NO_STOP, 0);
		if (ret)
			goto exit;

		ret = mcp_i2c_smbus_read(mcp, NULL,
						MCP2221_I2C_RD_RPT_START,
						addr, 2, (u8 *)&data->word);
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		ret = mcp_smbus_write(mcp, addr, command, data->block,
						data->block[0] + 1,
						MCP2221_I2C_WR_NO_STOP, 0);
		if (ret)
			goto exit;

		ret = mcp_i2c_smbus_read(mcp, NULL,
						MCP2221_I2C_RD_RPT_START,
						addr, I2C_SMBUS_BLOCK_MAX,
						data->block);
		break;
	default:
		dev_err(&mcp->adapter.dev,
			"unsupported smbus transaction size:%d\n", size);
		ret = -EOPNOTSUPP;
	}

exit:
	hid_hw_power(mcp->hdev, PM_HINT_NORMAL);
	mutex_unlock(&mcp->lock);
	return ret;
}

static u32 mcp_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C |
			I2C_FUNC_SMBUS_READ_BLOCK_DATA |
			I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
			(I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_PEC);
}

static const struct i2c_algorithm mcp_i2c_algo = {
	.master_xfer = mcp_i2c_xfer,
	.smbus_xfer = mcp_smbus_xfer,
	.functionality = mcp_i2c_func,
};

/* Gives current state of i2c engine inside mcp2221 */
static int mcp_get_i2c_eng_state(struct mcp2221 *mcp,
				u8 *data, u8 idx)
{
	int ret;

	switch (data[idx]) {
	case MCP2221_I2C_WRADDRL_NACK:
	case MCP2221_I2C_WRADDRL_SEND:
		ret = -ENXIO;
		break;
	case MCP2221_I2C_START_TOUT:
	case MCP2221_I2C_STOP_TOUT:
	case MCP2221_I2C_WRADDRL_TOUT:
	case MCP2221_I2C_WRDATA_TOUT:
		ret = -ETIMEDOUT;
		break;
	case MCP2221_I2C_ENG_BUSY:
		ret = -EAGAIN;
		break;
	case MCP2221_SUCCESS:
		ret = 0x00;
		break;
	default:
		ret = -EIO;
	}

	return ret;
}

/*
 * MCP2221 uses interrupt endpoint for input reports. This function
 * is called by HID layer when it receives i/p report from mcp2221,
 * which is actually a response to the previously sent command.
 *
 * MCP2221A firmware specific return codes are parsed and 0 or
 * appropriate negative error code is returned. Delayed response
 * results in timeout error and stray reponses results in -EIO.
 */
static int mcp2221_raw_event(struct hid_device *hdev,
				struct hid_report *report, u8 *data, int size)
{
	u8 *buf;
	struct mcp2221 *mcp = hid_get_drvdata(hdev);

	switch (data[0]) {

	case MCP2221_I2C_WR_DATA:
	case MCP2221_I2C_WR_NO_STOP:
	case MCP2221_I2C_RD_DATA:
	case MCP2221_I2C_RD_RPT_START:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			mcp->status = 0;
			break;
		default:
			mcp->status = mcp_get_i2c_eng_state(mcp, data, 2);
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_I2C_PARAM_OR_STATUS:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			if ((mcp->txbuf[3] == MCP2221_I2C_SET_SPEED) &&
				(data[3] != MCP2221_I2C_SET_SPEED)) {
				mcp->status = -EAGAIN;
				break;
			}
			if (data[20] & MCP2221_I2C_MASK_ADDR_NACK) {
				mcp->status = -ENXIO;
				break;
			}
			mcp->status = mcp_get_i2c_eng_state(mcp, data, 8);
			break;
		default:
			mcp->status = -EIO;
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_I2C_GET_DATA:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			if (data[2] == MCP2221_I2C_ADDR_NACK) {
				mcp->status = -ENXIO;
				break;
			}
			if (!mcp_get_i2c_eng_state(mcp, data, 2)
				&& (data[3] == 0)) {
				mcp->status = 0;
				break;
			}
			if (data[3] == 127) {
				mcp->status = -EIO;
				break;
			}
			if (data[2] == MCP2221_I2C_READ_COMPL) {
				buf = mcp->rxbuf;
				memcpy(&buf[mcp->rxbuf_idx], &data[4], data[3]);
				mcp->rxbuf_idx = mcp->rxbuf_idx + data[3];
				mcp->status = 0;
				break;
			}
			mcp->status = -EIO;
			break;
		default:
			mcp->status = -EIO;
		}
		complete(&mcp->wait_in_report);
		break;

	default:
		mcp->status = -EIO;
		complete(&mcp->wait_in_report);
	}

	return 1;
}

static int mcp2221_probe(struct hid_device *hdev,
					const struct hid_device_id *id)
{
	int ret;
	struct mcp2221 *mcp;

	mcp = devm_kzalloc(&hdev->dev, sizeof(*mcp), GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "can't parse reports\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "can't start hardware\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "can't open device\n");
		goto err_hstop;
	}

	mutex_init(&mcp->lock);
	init_completion(&mcp->wait_in_report);
	hid_set_drvdata(hdev, mcp);
	mcp->hdev = hdev;

	/* Set I2C bus clock diviser */
	if (i2c_clk_freq > 400)
		i2c_clk_freq = 400;
	if (i2c_clk_freq < 50)
		i2c_clk_freq = 50;
	mcp->cur_i2c_clk_div = (12000000 / (i2c_clk_freq * 1000)) - 3;

	mcp->adapter.owner = THIS_MODULE;
	mcp->adapter.class = I2C_CLASS_HWMON;
	mcp->adapter.algo = &mcp_i2c_algo;
	mcp->adapter.retries = 1;
	mcp->adapter.dev.parent = &hdev->dev;
	snprintf(mcp->adapter.name, sizeof(mcp->adapter.name),
			"MCP2221 usb-i2c bridge on hidraw%d",
			((struct hidraw *)hdev->hidraw)->minor);

	ret = i2c_add_adapter(&mcp->adapter);
	if (ret) {
		hid_err(hdev, "can't add usb-i2c adapter: %d\n", ret);
		goto err_i2c;
	}
	i2c_set_adapdata(&mcp->adapter, mcp);

	return 0;

err_i2c:
	hid_hw_close(mcp->hdev);
err_hstop:
	hid_hw_stop(mcp->hdev);
	return ret;
}

static void mcp2221_remove(struct hid_device *hdev)
{
	struct mcp2221 *mcp = hid_get_drvdata(hdev);

	i2c_del_adapter(&mcp->adapter);
	hid_hw_close(mcp->hdev);
	hid_hw_stop(mcp->hdev);
}

static const struct hid_device_id mcp2221_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_MCP2221) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mcp2221_devices);

static struct hid_driver mcp2221_driver = {
	.name		= "mcp2221",
	.id_table	= mcp2221_devices,
	.probe		= mcp2221_probe,
	.remove		= mcp2221_remove,
	.raw_event	= mcp2221_raw_event,
};

/* Register with HID core */
module_hid_driver(mcp2221_driver);

MODULE_AUTHOR("Rishi Gupta <gupt21@gmail.com>");
MODULE_DESCRIPTION("MCP2221 Microchip HID USB to I2C master bridge");
MODULE_LICENSE("GPL v2");
