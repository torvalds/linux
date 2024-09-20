// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCP2221A - Microchip USB to I2C Host Protocol Bridge
 *
 * Copyright (c) 2020, Rishi Gupta <gupt21@gmail.com>
 *
 * Datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/20005565B.pdf
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/i2c.h>
#include <linux/gpio/driver.h>
#include <linux/iio/iio.h>
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
	MCP2221_GPIO_SET = 0x50,
	MCP2221_GPIO_GET = 0x51,
	MCP2221_SET_SRAM_SETTINGS = 0x60,
	MCP2221_GET_SRAM_SETTINGS = 0x61,
	MCP2221_READ_FLASH_DATA = 0xb0,
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
	MCP2221_I2C_READ_PARTIAL = 0x54,
	MCP2221_I2C_READ_COMPL = 0x55,
	MCP2221_ALT_F_NOT_GPIOV = 0xEE,
	MCP2221_ALT_F_NOT_GPIOD = 0xEF,
};

/* MCP GPIO direction encoding */
enum {
	MCP2221_DIR_OUT = 0x00,
	MCP2221_DIR_IN = 0x01,
};

#define MCP_NGPIO	4

/* MCP GPIO set command layout */
struct mcp_set_gpio {
	u8 cmd;
	u8 dummy;
	struct {
		u8 change_value;
		u8 value;
		u8 change_direction;
		u8 direction;
	} gpio[MCP_NGPIO];
} __packed;

/* MCP GPIO get command layout */
struct mcp_get_gpio {
	u8 cmd;
	u8 dummy;
	struct {
		u8 value;
		u8 direction;
	} gpio[MCP_NGPIO];
} __packed;

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
	struct delayed_work init_work;
	u8 *rxbuf;
	u8 txbuf[64];
	int rxbuf_idx;
	int status;
	u8 cur_i2c_clk_div;
	struct gpio_chip *gc;
	u8 gp_idx;
	u8 gpio_dir;
	u8 mode[4];
#if IS_REACHABLE(CONFIG_IIO)
	struct iio_chan_spec iio_channels[3];
	u16 adc_values[3];
	u8 adc_scale;
	u8 dac_value;
	u16 dac_scale;
#endif
};

struct mcp2221_iio {
	struct mcp2221 *mcp;
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

/* Check if the last command succeeded or failed and return the result.
 * If the command did fail, cancel that command which will free the i2c bus.
 */
static int mcp_chk_last_cmd_status_free_bus(struct mcp2221 *mcp)
{
	int ret;

	ret = mcp_chk_last_cmd_status(mcp);
	if (ret) {
		/* The last command was a failure.
		 * Send a cancel which will also free the bus.
		 */
		usleep_range(980, 1000);
		mcp_cancel_last_cmd(mcp);
	}

	return ret;
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
			ret = mcp_chk_last_cmd_status_free_bus(mcp);
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
	int retries = 0;

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
		/* Wait for the data to be read by the device */
		usleep_range(980, 1000);

		memset(mcp->txbuf, 0, 4);
		mcp->txbuf[0] = MCP2221_I2C_GET_DATA;

		ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
		if (ret) {
			if (retries < 5) {
				/* The data wasn't ready to read.
				 * Wait a bit longer and try again.
				 */
				usleep_range(90, 100);
				retries++;
			} else {
				return ret;
			}
		} else {
			retries = 0;
		}
	} while (mcp->rxbuf_idx < total_len);

	usleep_range(980, 1000);
	ret = mcp_chk_last_cmd_status_free_bus(mcp);

	return ret;
}

static int mcp_i2c_xfer(struct i2c_adapter *adapter,
				struct i2c_msg msgs[], int num)
{
	int ret;
	struct mcp2221 *mcp = i2c_get_adapdata(adapter);

	hid_hw_power(mcp->hdev, PM_HINT_FULLON);

	mutex_lock(&mcp->lock);

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
		if (len > I2C_SMBUS_BLOCK_MAX)
			return -EINVAL;

		memcpy(&mcp->txbuf[5], buf, len);
		data_len = len + 5;
	}

	ret = mcp_send_data_req_status(mcp, mcp->txbuf, data_len);
	if (ret)
		return ret;

	if (last_status) {
		usleep_range(980, 1000);

		ret = mcp_chk_last_cmd_status_free_bus(mcp);
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

#if IS_REACHABLE(CONFIG_GPIOLIB)
static int mcp_gpio_get(struct gpio_chip *gc,
				unsigned int offset)
{
	int ret;
	struct mcp2221 *mcp = gpiochip_get_data(gc);

	mcp->txbuf[0] = MCP2221_GPIO_GET;

	mcp->gp_idx = offsetof(struct mcp_get_gpio, gpio[offset]);

	mutex_lock(&mcp->lock);
	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
	mutex_unlock(&mcp->lock);

	return ret;
}

static void mcp_gpio_set(struct gpio_chip *gc,
				unsigned int offset, int value)
{
	struct mcp2221 *mcp = gpiochip_get_data(gc);

	memset(mcp->txbuf, 0, 18);
	mcp->txbuf[0] = MCP2221_GPIO_SET;

	mcp->gp_idx = offsetof(struct mcp_set_gpio, gpio[offset].value);

	mcp->txbuf[mcp->gp_idx - 1] = 1;
	mcp->txbuf[mcp->gp_idx] = !!value;

	mutex_lock(&mcp->lock);
	mcp_send_data_req_status(mcp, mcp->txbuf, 18);
	mutex_unlock(&mcp->lock);
}

static int mcp_gpio_dir_set(struct mcp2221 *mcp,
				unsigned int offset, u8 val)
{
	memset(mcp->txbuf, 0, 18);
	mcp->txbuf[0] = MCP2221_GPIO_SET;

	mcp->gp_idx = offsetof(struct mcp_set_gpio, gpio[offset].direction);

	mcp->txbuf[mcp->gp_idx - 1] = 1;
	mcp->txbuf[mcp->gp_idx] = val;

	return mcp_send_data_req_status(mcp, mcp->txbuf, 18);
}

static int mcp_gpio_direction_input(struct gpio_chip *gc,
				unsigned int offset)
{
	int ret;
	struct mcp2221 *mcp = gpiochip_get_data(gc);

	mutex_lock(&mcp->lock);
	ret = mcp_gpio_dir_set(mcp, offset, MCP2221_DIR_IN);
	mutex_unlock(&mcp->lock);

	return ret;
}

static int mcp_gpio_direction_output(struct gpio_chip *gc,
				unsigned int offset, int value)
{
	int ret;
	struct mcp2221 *mcp = gpiochip_get_data(gc);

	mutex_lock(&mcp->lock);
	ret = mcp_gpio_dir_set(mcp, offset, MCP2221_DIR_OUT);
	mutex_unlock(&mcp->lock);

	/* Can't configure as output, bailout early */
	if (ret)
		return ret;

	mcp_gpio_set(gc, offset, value);

	return 0;
}

static int mcp_gpio_get_direction(struct gpio_chip *gc,
				unsigned int offset)
{
	int ret;
	struct mcp2221 *mcp = gpiochip_get_data(gc);

	mcp->txbuf[0] = MCP2221_GPIO_GET;

	mcp->gp_idx = offsetof(struct mcp_get_gpio, gpio[offset]);

	mutex_lock(&mcp->lock);
	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);
	mutex_unlock(&mcp->lock);

	if (ret)
		return ret;

	if (mcp->gpio_dir == MCP2221_DIR_IN)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}
#endif

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
#if IS_REACHABLE(CONFIG_IIO)
			memcpy(&mcp->adc_values, &data[50], sizeof(mcp->adc_values));
#endif
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
			if (data[2] == MCP2221_I2C_READ_COMPL ||
			    data[2] == MCP2221_I2C_READ_PARTIAL) {
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

	case MCP2221_GPIO_GET:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			if ((data[mcp->gp_idx] == MCP2221_ALT_F_NOT_GPIOV) ||
				(data[mcp->gp_idx + 1] == MCP2221_ALT_F_NOT_GPIOD)) {
				mcp->status = -ENOENT;
			} else {
				mcp->status = !!data[mcp->gp_idx];
				mcp->gpio_dir = data[mcp->gp_idx + 1];
			}
			break;
		default:
			mcp->status = -EAGAIN;
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_GPIO_SET:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			if ((data[mcp->gp_idx] == MCP2221_ALT_F_NOT_GPIOV) ||
				(data[mcp->gp_idx - 1] == MCP2221_ALT_F_NOT_GPIOV)) {
				mcp->status = -ENOENT;
			} else {
				mcp->status = 0;
			}
			break;
		default:
			mcp->status = -EAGAIN;
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_SET_SRAM_SETTINGS:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			mcp->status = 0;
			break;
		default:
			mcp->status = -EAGAIN;
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_GET_SRAM_SETTINGS:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			memcpy(&mcp->mode, &data[22], 4);
#if IS_REACHABLE(CONFIG_IIO)
			mcp->dac_value = data[6] & GENMASK(4, 0);
#endif
			mcp->status = 0;
			break;
		default:
			mcp->status = -EAGAIN;
		}
		complete(&mcp->wait_in_report);
		break;

	case MCP2221_READ_FLASH_DATA:
		switch (data[1]) {
		case MCP2221_SUCCESS:
			mcp->status = 0;

			/* Only handles CHIP SETTINGS subpage currently */
			if (mcp->txbuf[1] != 0) {
				mcp->status = -EIO;
				break;
			}

#if IS_REACHABLE(CONFIG_IIO)
			{
				u8 tmp;
				/* DAC scale value */
				tmp = FIELD_GET(GENMASK(7, 6), data[6]);
				if ((data[6] & BIT(5)) && tmp)
					mcp->dac_scale = tmp + 4;
				else
					mcp->dac_scale = 5;

				/* ADC scale value */
				tmp = FIELD_GET(GENMASK(4, 3), data[7]);
				if ((data[7] & BIT(2)) && tmp)
					mcp->adc_scale = tmp - 1;
				else
					mcp->adc_scale = 0;
			}
#endif

			break;
		default:
			mcp->status = -EAGAIN;
		}
		complete(&mcp->wait_in_report);
		break;

	default:
		mcp->status = -EIO;
		complete(&mcp->wait_in_report);
	}

	return 1;
}

/* Device resource managed function for HID unregistration */
static void mcp2221_hid_unregister(void *ptr)
{
	struct hid_device *hdev = ptr;

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

/* This is needed to be sure hid_hw_stop() isn't called twice by the subsystem */
static void mcp2221_remove(struct hid_device *hdev)
{
#if IS_REACHABLE(CONFIG_IIO)
	struct mcp2221 *mcp = hid_get_drvdata(hdev);

	cancel_delayed_work_sync(&mcp->init_work);
#endif
}

#if IS_REACHABLE(CONFIG_IIO)
static int mcp2221_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mcp2221_iio *priv = iio_priv(indio_dev);
	struct mcp2221 *mcp = priv->mcp;
	int ret;

	if (mask == IIO_CHAN_INFO_SCALE) {
		if (channel->output)
			*val = 1 << mcp->dac_scale;
		else
			*val = 1 << mcp->adc_scale;

		return IIO_VAL_INT;
	}

	mutex_lock(&mcp->lock);

	if (channel->output) {
		*val = mcp->dac_value;
		ret = IIO_VAL_INT;
	} else {
		/* Read ADC values */
		ret = mcp_chk_last_cmd_status(mcp);

		if (!ret) {
			*val = le16_to_cpu((__force __le16) mcp->adc_values[channel->address]);
			if (*val >= BIT(10))
				ret =  -EINVAL;
			else
				ret = IIO_VAL_INT;
		}
	}

	mutex_unlock(&mcp->lock);

	return ret;
}

static int mcp2221_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mcp2221_iio *priv = iio_priv(indio_dev);
	struct mcp2221 *mcp = priv->mcp;
	int ret;

	if (val < 0 || val >= BIT(5))
		return -EINVAL;

	mutex_lock(&mcp->lock);

	memset(mcp->txbuf, 0, 12);
	mcp->txbuf[0] = MCP2221_SET_SRAM_SETTINGS;
	mcp->txbuf[4] = BIT(7) | val;

	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 12);
	if (!ret)
		mcp->dac_value = val;

	mutex_unlock(&mcp->lock);

	return ret;
}

static const struct iio_info mcp2221_info = {
	.read_raw = &mcp2221_read_raw,
	.write_raw = &mcp2221_write_raw,
};

static int mcp_iio_channels(struct mcp2221 *mcp)
{
	int idx, cnt = 0;
	bool dac_created = false;

	/* GP0 doesn't have ADC/DAC alternative function */
	for (idx = 1; idx < MCP_NGPIO; idx++) {
		struct iio_chan_spec *chan = &mcp->iio_channels[cnt];

		switch (mcp->mode[idx]) {
		case 2:
			chan->address = idx - 1;
			chan->channel = cnt++;
			break;
		case 3:
			/* GP1 doesn't have DAC alternative function */
			if (idx == 1 || dac_created)
				continue;
			/* DAC1 and DAC2 outputs are connected to the same DAC */
			dac_created = true;
			chan->output = 1;
			cnt++;
			break;
		default:
			continue;
		}

		chan->type = IIO_VOLTAGE;
		chan->indexed = 1;
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
		chan->scan_index = -1;
	}

	return cnt;
}

static void mcp_init_work(struct work_struct *work)
{
	struct iio_dev *indio_dev;
	struct mcp2221 *mcp = container_of(work, struct mcp2221, init_work.work);
	struct mcp2221_iio *data;
	static int retries = 5;
	int ret, num_channels;

	hid_hw_power(mcp->hdev, PM_HINT_FULLON);
	mutex_lock(&mcp->lock);

	mcp->txbuf[0] = MCP2221_GET_SRAM_SETTINGS;
	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 1);

	if (ret == -EAGAIN)
		goto reschedule_task;

	num_channels = mcp_iio_channels(mcp);
	if (!num_channels)
		goto unlock;

	mcp->txbuf[0] = MCP2221_READ_FLASH_DATA;
	mcp->txbuf[1] = 0;
	ret = mcp_send_data_req_status(mcp, mcp->txbuf, 2);

	if (ret == -EAGAIN)
		goto reschedule_task;

	indio_dev = devm_iio_device_alloc(&mcp->hdev->dev, sizeof(*data));
	if (!indio_dev)
		goto unlock;

	data = iio_priv(indio_dev);
	data->mcp = mcp;

	indio_dev->name = "mcp2221";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp2221_info;
	indio_dev->channels = mcp->iio_channels;
	indio_dev->num_channels = num_channels;

	devm_iio_device_register(&mcp->hdev->dev, indio_dev);

unlock:
	mutex_unlock(&mcp->lock);
	hid_hw_power(mcp->hdev, PM_HINT_NORMAL);

	return;

reschedule_task:
	mutex_unlock(&mcp->lock);
	hid_hw_power(mcp->hdev, PM_HINT_NORMAL);

	if (!retries--)
		return;

	/* Device is not ready to read SRAM or FLASH data, try again */
	schedule_delayed_work(&mcp->init_work, msecs_to_jiffies(100));
}
#endif

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

	/*
	 * This driver uses the .raw_event callback and therefore does not need any
	 * HID_CONNECT_xxx flags.
	 */
	ret = hid_hw_start(hdev, 0);
	if (ret) {
		hid_err(hdev, "can't start hardware\n");
		return ret;
	}

	hid_info(hdev, "USB HID v%x.%02x Device [%s] on %s\n", hdev->version >> 8,
			hdev->version & 0xff, hdev->name, hdev->phys);

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "can't open device\n");
		hid_hw_stop(hdev);
		return ret;
	}

	mutex_init(&mcp->lock);
	init_completion(&mcp->wait_in_report);
	hid_set_drvdata(hdev, mcp);
	mcp->hdev = hdev;

	ret = devm_add_action_or_reset(&hdev->dev, mcp2221_hid_unregister, hdev);
	if (ret)
		return ret;

	hid_device_io_start(hdev);

	/* Set I2C bus clock diviser */
	if (i2c_clk_freq > 400)
		i2c_clk_freq = 400;
	if (i2c_clk_freq < 50)
		i2c_clk_freq = 50;
	mcp->cur_i2c_clk_div = (12000000 / (i2c_clk_freq * 1000)) - 3;
	ret = mcp_set_i2c_speed(mcp);
	if (ret) {
		hid_err(hdev, "can't set i2c speed: %d\n", ret);
		return ret;
	}

	mcp->adapter.owner = THIS_MODULE;
	mcp->adapter.class = I2C_CLASS_HWMON;
	mcp->adapter.algo = &mcp_i2c_algo;
	mcp->adapter.retries = 1;
	mcp->adapter.dev.parent = &hdev->dev;
	ACPI_COMPANION_SET(&mcp->adapter.dev, ACPI_COMPANION(hdev->dev.parent));
	snprintf(mcp->adapter.name, sizeof(mcp->adapter.name),
			"MCP2221 usb-i2c bridge");

	i2c_set_adapdata(&mcp->adapter, mcp);
	ret = devm_i2c_add_adapter(&hdev->dev, &mcp->adapter);
	if (ret) {
		hid_err(hdev, "can't add usb-i2c adapter: %d\n", ret);
		return ret;
	}

#if IS_REACHABLE(CONFIG_GPIOLIB)
	/* Setup GPIO chip */
	mcp->gc = devm_kzalloc(&hdev->dev, sizeof(*mcp->gc), GFP_KERNEL);
	if (!mcp->gc)
		return -ENOMEM;

	mcp->gc->label = "mcp2221_gpio";
	mcp->gc->direction_input = mcp_gpio_direction_input;
	mcp->gc->direction_output = mcp_gpio_direction_output;
	mcp->gc->get_direction = mcp_gpio_get_direction;
	mcp->gc->set = mcp_gpio_set;
	mcp->gc->get = mcp_gpio_get;
	mcp->gc->ngpio = MCP_NGPIO;
	mcp->gc->base = -1;
	mcp->gc->can_sleep = 1;
	mcp->gc->parent = &hdev->dev;

	ret = devm_gpiochip_add_data(&hdev->dev, mcp->gc, mcp);
	if (ret)
		return ret;
#endif

#if IS_REACHABLE(CONFIG_IIO)
	INIT_DELAYED_WORK(&mcp->init_work, mcp_init_work);
	schedule_delayed_work(&mcp->init_work, msecs_to_jiffies(100));
#endif

	return 0;
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
