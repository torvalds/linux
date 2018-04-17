/*
 * Elan Microelectronics touch panels with I2C interface
 *
 * Copyright (C) 2014 Elan Microelectronics Corporation.
 * Scott Liu <scott.liu@emc.com.tw>
 *
 * This code is partly based on hid-multitouch.c:
 *
 *  Copyright (c) 2010-2012 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010-2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2010-2012 Ecole Nationale de l'Aviation Civile, France
 *
 *
 * This code is partly based on i2c-hid.c:
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 */

/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <asm/unaligned.h>

/* Device, Driver information */
#define DEVICE_NAME	"elants_i2c"
#define DRV_VERSION	"1.0.9"

/* Convert from rows or columns into resolution */
#define ELAN_TS_RESOLUTION(n, m)   (((n) - 1) * (m))

/* FW header data */
#define HEADER_SIZE		4
#define FW_HDR_TYPE		0
#define FW_HDR_COUNT		1
#define FW_HDR_LENGTH		2

/* Buffer mode Queue Header information */
#define QUEUE_HEADER_SINGLE	0x62
#define QUEUE_HEADER_NORMAL	0X63
#define QUEUE_HEADER_WAIT	0x64

/* Command header definition */
#define CMD_HEADER_WRITE	0x54
#define CMD_HEADER_READ		0x53
#define CMD_HEADER_6B_READ	0x5B
#define CMD_HEADER_RESP		0x52
#define CMD_HEADER_6B_RESP	0x9B
#define CMD_HEADER_HELLO	0x55
#define CMD_HEADER_REK		0x66

/* FW position data */
#define PACKET_SIZE		55
#define MAX_CONTACT_NUM		10
#define FW_POS_HEADER		0
#define FW_POS_STATE		1
#define FW_POS_TOTAL		2
#define FW_POS_XY		3
#define FW_POS_CHECKSUM		34
#define FW_POS_WIDTH		35
#define FW_POS_PRESSURE		45

#define HEADER_REPORT_10_FINGER	0x62

/* Header (4 bytes) plus 3 fill 10-finger packets */
#define MAX_PACKET_SIZE		169

#define BOOT_TIME_DELAY_MS	50

/* FW read command, 0x53 0x?? 0x0, 0x01 */
#define E_ELAN_INFO_FW_VER	0x00
#define E_ELAN_INFO_BC_VER	0x10
#define E_ELAN_INFO_TEST_VER	0xE0
#define E_ELAN_INFO_FW_ID	0xF0
#define E_INFO_OSR		0xD6
#define E_INFO_PHY_SCAN		0xD7
#define E_INFO_PHY_DRIVER	0xD8

#define MAX_RETRIES		3
#define MAX_FW_UPDATE_RETRIES	30

#define ELAN_FW_PAGESIZE	132

/* calibration timeout definition */
#define ELAN_CALI_TIMEOUT_MSEC	12000

#define ELAN_POWERON_DELAY_USEC	500
#define ELAN_RESET_DELAY_MSEC	20

enum elants_state {
	ELAN_STATE_NORMAL,
	ELAN_WAIT_QUEUE_HEADER,
	ELAN_WAIT_RECALIBRATION,
};

enum elants_iap_mode {
	ELAN_IAP_OPERATIONAL,
	ELAN_IAP_RECOVERY,
};

/* struct elants_data - represents state of Elan touchscreen device */
struct elants_data {
	struct i2c_client *client;
	struct input_dev *input;

	struct regulator *vcc33;
	struct regulator *vccio;
	struct gpio_desc *reset_gpio;

	u16 fw_version;
	u8 test_version;
	u8 solution_version;
	u8 bc_version;
	u8 iap_version;
	u16 hw_version;
	unsigned int x_res;	/* resolution in units/mm */
	unsigned int y_res;
	unsigned int x_max;
	unsigned int y_max;

	enum elants_state state;
	enum elants_iap_mode iap_mode;

	/* Guards against concurrent access to the device via sysfs */
	struct mutex sysfs_mutex;

	u8 cmd_resp[HEADER_SIZE];
	struct completion cmd_done;

	u8 buf[MAX_PACKET_SIZE];

	bool wake_irq_enabled;
	bool keep_power_in_suspend;
};

static int elants_i2c_send(struct i2c_client *client,
			   const void *data, size_t size)
{
	int ret;

	ret = i2c_master_send(client, data, size);
	if (ret == size)
		return 0;

	if (ret >= 0)
		ret = -EIO;

	dev_err(&client->dev, "%s failed (%*ph): %d\n",
		__func__, (int)size, data, ret);

	return ret;
}

static int elants_i2c_read(struct i2c_client *client, void *data, size_t size)
{
	int ret;

	ret = i2c_master_recv(client, data, size);
	if (ret == size)
		return 0;

	if (ret >= 0)
		ret = -EIO;

	dev_err(&client->dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int elants_i2c_execute_command(struct i2c_client *client,
				      const u8 *cmd, size_t cmd_size,
				      u8 *resp, size_t resp_size)
{
	struct i2c_msg msgs[2];
	int ret;
	u8 expected_response;

	switch (cmd[0]) {
	case CMD_HEADER_READ:
		expected_response = CMD_HEADER_RESP;
		break;

	case CMD_HEADER_6B_READ:
		expected_response = CMD_HEADER_6B_RESP;
		break;

	default:
		dev_err(&client->dev, "%s: invalid command %*ph\n",
			__func__, (int)cmd_size, cmd);
		return -EINVAL;
	}

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags & I2C_M_TEN;
	msgs[0].len = cmd_size;
	msgs[0].buf = (u8 *)cmd;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags & I2C_M_TEN;
	msgs[1].flags |= I2C_M_RD;
	msgs[1].len = resp_size;
	msgs[1].buf = resp;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	if (ret != ARRAY_SIZE(msgs) || resp[FW_HDR_TYPE] != expected_response)
		return -EIO;

	return 0;
}

static int elants_i2c_calibrate(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int ret, error;
	static const u8 w_flashkey[] = { 0x54, 0xC0, 0xE1, 0x5A };
	static const u8 rek[] = { 0x54, 0x29, 0x00, 0x01 };
	static const u8 rek_resp[] = { CMD_HEADER_REK, 0x66, 0x66, 0x66 };

	disable_irq(client->irq);

	ts->state = ELAN_WAIT_RECALIBRATION;
	reinit_completion(&ts->cmd_done);

	elants_i2c_send(client, w_flashkey, sizeof(w_flashkey));
	elants_i2c_send(client, rek, sizeof(rek));

	enable_irq(client->irq);

	ret = wait_for_completion_interruptible_timeout(&ts->cmd_done,
				msecs_to_jiffies(ELAN_CALI_TIMEOUT_MSEC));

	ts->state = ELAN_STATE_NORMAL;

	if (ret <= 0) {
		error = ret < 0 ? ret : -ETIMEDOUT;
		dev_err(&client->dev,
			"error while waiting for calibration to complete: %d\n",
			error);
		return error;
	}

	if (memcmp(rek_resp, ts->cmd_resp, sizeof(rek_resp))) {
		dev_err(&client->dev,
			"unexpected calibration response: %*ph\n",
			(int)sizeof(ts->cmd_resp), ts->cmd_resp);
		return -EINVAL;
	}

	return 0;
}

static int elants_i2c_sw_reset(struct i2c_client *client)
{
	const u8 soft_rst_cmd[] = { 0x77, 0x77, 0x77, 0x77 };
	int error;

	error = elants_i2c_send(client, soft_rst_cmd,
				sizeof(soft_rst_cmd));
	if (error) {
		dev_err(&client->dev, "software reset failed: %d\n", error);
		return error;
	}

	/*
	 * We should wait at least 10 msec (but no more than 40) before
	 * sending fastboot or IAP command to the device.
	 */
	msleep(30);

	return 0;
}

static u16 elants_i2c_parse_version(u8 *buf)
{
	return get_unaligned_be32(buf) >> 4;
}

static int elants_i2c_query_hw_version(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int error, retry_cnt;
	const u8 cmd[] = { CMD_HEADER_READ, E_ELAN_INFO_FW_ID, 0x00, 0x01 };
	u8 resp[HEADER_SIZE];

	for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
		error = elants_i2c_execute_command(client, cmd, sizeof(cmd),
						   resp, sizeof(resp));
		if (!error) {
			ts->hw_version = elants_i2c_parse_version(resp);
			if (ts->hw_version != 0xffff)
				return 0;
		}

		dev_dbg(&client->dev, "read fw id error=%d, buf=%*phC\n",
			error, (int)sizeof(resp), resp);
	}

	if (error) {
		dev_err(&client->dev,
			"Failed to read fw id: %d\n", error);
		return error;
	}

	dev_err(&client->dev, "Invalid fw id: %#04x\n", ts->hw_version);

	return -EINVAL;
}

static int elants_i2c_query_fw_version(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int error, retry_cnt;
	const u8 cmd[] = { CMD_HEADER_READ, E_ELAN_INFO_FW_VER, 0x00, 0x01 };
	u8 resp[HEADER_SIZE];

	for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
		error = elants_i2c_execute_command(client, cmd, sizeof(cmd),
						   resp, sizeof(resp));
		if (!error) {
			ts->fw_version = elants_i2c_parse_version(resp);
			if (ts->fw_version != 0x0000 &&
			    ts->fw_version != 0xffff)
				return 0;
		}

		dev_dbg(&client->dev, "read fw version error=%d, buf=%*phC\n",
			error, (int)sizeof(resp), resp);
	}

	dev_err(&client->dev,
		"Failed to read fw version or fw version is invalid\n");

	return -EINVAL;
}

static int elants_i2c_query_test_version(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int error, retry_cnt;
	u16 version;
	const u8 cmd[] = { CMD_HEADER_READ, E_ELAN_INFO_TEST_VER, 0x00, 0x01 };
	u8 resp[HEADER_SIZE];

	for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
		error = elants_i2c_execute_command(client, cmd, sizeof(cmd),
						   resp, sizeof(resp));
		if (!error) {
			version = elants_i2c_parse_version(resp);
			ts->test_version = version >> 8;
			ts->solution_version = version & 0xff;

			return 0;
		}

		dev_dbg(&client->dev,
			"read test version error rc=%d, buf=%*phC\n",
			error, (int)sizeof(resp), resp);
	}

	dev_err(&client->dev, "Failed to read test version\n");

	return -EINVAL;
}

static int elants_i2c_query_bc_version(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	const u8 cmd[] = { CMD_HEADER_READ, E_ELAN_INFO_BC_VER, 0x00, 0x01 };
	u8 resp[HEADER_SIZE];
	u16 version;
	int error;

	error = elants_i2c_execute_command(client, cmd, sizeof(cmd),
					   resp, sizeof(resp));
	if (error) {
		dev_err(&client->dev,
			"read BC version error=%d, buf=%*phC\n",
			error, (int)sizeof(resp), resp);
		return error;
	}

	version = elants_i2c_parse_version(resp);
	ts->bc_version = version >> 8;
	ts->iap_version = version & 0xff;

	return 0;
}

static int elants_i2c_query_ts_info(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int error;
	u8 resp[17];
	u16 phy_x, phy_y, rows, cols, osr;
	const u8 get_resolution_cmd[] = {
		CMD_HEADER_6B_READ, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	const u8 get_osr_cmd[] = {
		CMD_HEADER_READ, E_INFO_OSR, 0x00, 0x01
	};
	const u8 get_physical_scan_cmd[] = {
		CMD_HEADER_READ, E_INFO_PHY_SCAN, 0x00, 0x01
	};
	const u8 get_physical_drive_cmd[] = {
		CMD_HEADER_READ, E_INFO_PHY_DRIVER, 0x00, 0x01
	};

	/* Get trace number */
	error = elants_i2c_execute_command(client,
					   get_resolution_cmd,
					   sizeof(get_resolution_cmd),
					   resp, sizeof(resp));
	if (error) {
		dev_err(&client->dev, "get resolution command failed: %d\n",
			error);
		return error;
	}

	rows = resp[2] + resp[6] + resp[10];
	cols = resp[3] + resp[7] + resp[11];

	/* Process mm_to_pixel information */
	error = elants_i2c_execute_command(client,
					   get_osr_cmd, sizeof(get_osr_cmd),
					   resp, sizeof(resp));
	if (error) {
		dev_err(&client->dev, "get osr command failed: %d\n",
			error);
		return error;
	}

	osr = resp[3];

	error = elants_i2c_execute_command(client,
					   get_physical_scan_cmd,
					   sizeof(get_physical_scan_cmd),
					   resp, sizeof(resp));
	if (error) {
		dev_err(&client->dev, "get physical scan command failed: %d\n",
			error);
		return error;
	}

	phy_x = get_unaligned_be16(&resp[2]);

	error = elants_i2c_execute_command(client,
					   get_physical_drive_cmd,
					   sizeof(get_physical_drive_cmd),
					   resp, sizeof(resp));
	if (error) {
		dev_err(&client->dev, "get physical drive command failed: %d\n",
			error);
		return error;
	}

	phy_y = get_unaligned_be16(&resp[2]);

	dev_dbg(&client->dev, "phy_x=%d, phy_y=%d\n", phy_x, phy_y);

	if (rows == 0 || cols == 0 || osr == 0) {
		dev_warn(&client->dev,
			 "invalid trace number data: %d, %d, %d\n",
			 rows, cols, osr);
	} else {
		/* translate trace number to TS resolution */
		ts->x_max = ELAN_TS_RESOLUTION(rows, osr);
		ts->x_res = DIV_ROUND_CLOSEST(ts->x_max, phy_x);
		ts->y_max = ELAN_TS_RESOLUTION(cols, osr);
		ts->y_res = DIV_ROUND_CLOSEST(ts->y_max, phy_y);
	}

	return 0;
}

static int elants_i2c_fastboot(struct i2c_client *client)
{
	const u8 boot_cmd[] = { 0x4D, 0x61, 0x69, 0x6E };
	int error;

	error = elants_i2c_send(client, boot_cmd, sizeof(boot_cmd));
	if (error) {
		dev_err(&client->dev, "boot failed: %d\n", error);
		return error;
	}

	dev_dbg(&client->dev, "boot success -- 0x%x\n", client->addr);
	return 0;
}

static int elants_i2c_initialize(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	int error, error2, retry_cnt;
	const u8 hello_packet[] = { 0x55, 0x55, 0x55, 0x55 };
	const u8 recov_packet[] = { 0x55, 0x55, 0x80, 0x80 };
	u8 buf[HEADER_SIZE];

	for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
		error = elants_i2c_sw_reset(client);
		if (error) {
			/* Continue initializing if it's the last try */
			if (retry_cnt < MAX_RETRIES - 1)
				continue;
		}

		error = elants_i2c_fastboot(client);
		if (error) {
			/* Continue initializing if it's the last try */
			if (retry_cnt < MAX_RETRIES - 1)
				continue;
		}

		/* Wait for Hello packet */
		msleep(BOOT_TIME_DELAY_MS);

		error = elants_i2c_read(client, buf, sizeof(buf));
		if (error) {
			dev_err(&client->dev,
				"failed to read 'hello' packet: %d\n", error);
		} else if (!memcmp(buf, hello_packet, sizeof(hello_packet))) {
			ts->iap_mode = ELAN_IAP_OPERATIONAL;
			break;
		} else if (!memcmp(buf, recov_packet, sizeof(recov_packet))) {
			/*
			 * Setting error code will mark device
			 * in recovery mode below.
			 */
			error = -EIO;
			break;
		} else {
			error = -EINVAL;
			dev_err(&client->dev,
				"invalid 'hello' packet: %*ph\n",
				(int)sizeof(buf), buf);
		}
	}

	/* hw version is available even if device in recovery state */
	error2 = elants_i2c_query_hw_version(ts);
	if (!error)
		error = error2;

	if (!error)
		error = elants_i2c_query_fw_version(ts);
	if (!error)
		error = elants_i2c_query_test_version(ts);
	if (!error)
		error = elants_i2c_query_bc_version(ts);
	if (!error)
		error = elants_i2c_query_ts_info(ts);

	if (error)
		ts->iap_mode = ELAN_IAP_RECOVERY;

	return 0;
}

/*
 * Firmware update interface.
 */

static int elants_i2c_fw_write_page(struct i2c_client *client,
				    const void *page)
{
	const u8 ack_ok[] = { 0xaa, 0xaa };
	u8 buf[2];
	int retry;
	int error;

	for (retry = 0; retry < MAX_FW_UPDATE_RETRIES; retry++) {
		error = elants_i2c_send(client, page, ELAN_FW_PAGESIZE);
		if (error) {
			dev_err(&client->dev,
				"IAP Write Page failed: %d\n", error);
			continue;
		}

		error = elants_i2c_read(client, buf, 2);
		if (error) {
			dev_err(&client->dev,
				"IAP Ack read failed: %d\n", error);
			return error;
		}

		if (!memcmp(buf, ack_ok, sizeof(ack_ok)))
			return 0;

		error = -EIO;
		dev_err(&client->dev,
			"IAP Get Ack Error [%02x:%02x]\n",
			buf[0], buf[1]);
	}

	return error;
}

static int elants_i2c_do_update_firmware(struct i2c_client *client,
					 const struct firmware *fw,
					 bool force)
{
	const u8 enter_iap[] = { 0x45, 0x49, 0x41, 0x50 };
	const u8 enter_iap2[] = { 0x54, 0x00, 0x12, 0x34 };
	const u8 iap_ack[] = { 0x55, 0xaa, 0x33, 0xcc };
	const u8 close_idle[] = {0x54, 0x2c, 0x01, 0x01};
	u8 buf[HEADER_SIZE];
	u16 send_id;
	int page, n_fw_pages;
	int error;

	/* Recovery mode detection! */
	if (force) {
		dev_dbg(&client->dev, "Recovery mode procedure\n");
		error = elants_i2c_send(client, enter_iap2, sizeof(enter_iap2));
	} else {
		/* Start IAP Procedure */
		dev_dbg(&client->dev, "Normal IAP procedure\n");
		/* Close idle mode */
		error = elants_i2c_send(client, close_idle, sizeof(close_idle));
		if (error)
			dev_err(&client->dev, "Failed close idle: %d\n", error);
		msleep(60);
		elants_i2c_sw_reset(client);
		msleep(20);
		error = elants_i2c_send(client, enter_iap, sizeof(enter_iap));
	}

	if (error) {
		dev_err(&client->dev, "failed to enter IAP mode: %d\n", error);
		return error;
	}

	msleep(20);

	/* check IAP state */
	error = elants_i2c_read(client, buf, 4);
	if (error) {
		dev_err(&client->dev,
			"failed to read IAP acknowledgement: %d\n",
			error);
		return error;
	}

	if (memcmp(buf, iap_ack, sizeof(iap_ack))) {
		dev_err(&client->dev,
			"failed to enter IAP: %*ph (expected %*ph)\n",
			(int)sizeof(buf), buf, (int)sizeof(iap_ack), iap_ack);
		return -EIO;
	}

	dev_info(&client->dev, "successfully entered IAP mode");

	send_id = client->addr;
	error = elants_i2c_send(client, &send_id, 1);
	if (error) {
		dev_err(&client->dev, "sending dummy byte failed: %d\n",
			error);
		return error;
	}

	/* Clear the last page of Master */
	error = elants_i2c_send(client, fw->data, ELAN_FW_PAGESIZE);
	if (error) {
		dev_err(&client->dev, "clearing of the last page failed: %d\n",
			error);
		return error;
	}

	error = elants_i2c_read(client, buf, 2);
	if (error) {
		dev_err(&client->dev,
			"failed to read ACK for clearing the last page: %d\n",
			error);
		return error;
	}

	n_fw_pages = fw->size / ELAN_FW_PAGESIZE;
	dev_dbg(&client->dev, "IAP Pages = %d\n", n_fw_pages);

	for (page = 0; page < n_fw_pages; page++) {
		error = elants_i2c_fw_write_page(client,
					fw->data + page * ELAN_FW_PAGESIZE);
		if (error) {
			dev_err(&client->dev,
				"failed to write FW page %d: %d\n",
				page, error);
			return error;
		}
	}

	/* Old iap needs to wait 200ms for WDT and rest is for hello packets */
	msleep(300);

	dev_info(&client->dev, "firmware update completed\n");
	return 0;
}

static int elants_i2c_fw_update(struct elants_data *ts)
{
	struct i2c_client *client = ts->client;
	const struct firmware *fw;
	char *fw_name;
	int error;

	fw_name = kasprintf(GFP_KERNEL, "elants_i2c_%04x.bin", ts->hw_version);
	if (!fw_name)
		return -ENOMEM;

	dev_info(&client->dev, "requesting fw name = %s\n", fw_name);
	error = request_firmware(&fw, fw_name, &client->dev);
	kfree(fw_name);
	if (error) {
		dev_err(&client->dev, "failed to request firmware: %d\n",
			error);
		return error;
	}

	if (fw->size % ELAN_FW_PAGESIZE) {
		dev_err(&client->dev, "invalid firmware length: %zu\n",
			fw->size);
		error = -EINVAL;
		goto out;
	}

	disable_irq(client->irq);

	error = elants_i2c_do_update_firmware(client, fw,
					ts->iap_mode == ELAN_IAP_RECOVERY);
	if (error) {
		dev_err(&client->dev, "firmware update failed: %d\n", error);
		ts->iap_mode = ELAN_IAP_RECOVERY;
		goto out_enable_irq;
	}

	error = elants_i2c_initialize(ts);
	if (error) {
		dev_err(&client->dev,
			"failed to initialize device after firmware update: %d\n",
			error);
		ts->iap_mode = ELAN_IAP_RECOVERY;
		goto out_enable_irq;
	}

	ts->iap_mode = ELAN_IAP_OPERATIONAL;

out_enable_irq:
	ts->state = ELAN_STATE_NORMAL;
	enable_irq(client->irq);
	msleep(100);

	if (!error)
		elants_i2c_calibrate(ts);
out:
	release_firmware(fw);
	return error;
}

/*
 * Event reporting.
 */

static void elants_i2c_mt_event(struct elants_data *ts, u8 *buf)
{
	struct input_dev *input = ts->input;
	unsigned int n_fingers;
	u16 finger_state;
	int i;

	n_fingers = buf[FW_POS_STATE + 1] & 0x0f;
	finger_state = ((buf[FW_POS_STATE + 1] & 0x30) << 4) |
			buf[FW_POS_STATE];

	dev_dbg(&ts->client->dev,
		"n_fingers: %u, state: %04x\n",  n_fingers, finger_state);

	for (i = 0; i < MAX_CONTACT_NUM && n_fingers; i++) {
		if (finger_state & 1) {
			unsigned int x, y, p, w;
			u8 *pos;

			pos = &buf[FW_POS_XY + i * 3];
			x = (((u16)pos[0] & 0xf0) << 4) | pos[1];
			y = (((u16)pos[0] & 0x0f) << 8) | pos[2];
			p = buf[FW_POS_PRESSURE + i];
			w = buf[FW_POS_WIDTH + i];

			dev_dbg(&ts->client->dev, "i=%d x=%d y=%d p=%d w=%d\n",
				i, x, y, p, w);

			input_mt_slot(input, i);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
			input_event(input, EV_ABS, ABS_MT_POSITION_X, x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, y);
			input_event(input, EV_ABS, ABS_MT_PRESSURE, p);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, w);

			n_fingers--;
		}

		finger_state >>= 1;
	}

	input_mt_sync_frame(input);
	input_sync(input);
}

static u8 elants_i2c_calculate_checksum(u8 *buf)
{
	u8 checksum = 0;
	u8 i;

	for (i = 0; i < FW_POS_CHECKSUM; i++)
		checksum += buf[i];

	return checksum;
}

static void elants_i2c_event(struct elants_data *ts, u8 *buf)
{
	u8 checksum = elants_i2c_calculate_checksum(buf);

	if (unlikely(buf[FW_POS_CHECKSUM] != checksum))
		dev_warn(&ts->client->dev,
			 "%s: invalid checksum for packet %02x: %02x vs. %02x\n",
			 __func__, buf[FW_POS_HEADER],
			 checksum, buf[FW_POS_CHECKSUM]);
	else if (unlikely(buf[FW_POS_HEADER] != HEADER_REPORT_10_FINGER))
		dev_warn(&ts->client->dev,
			 "%s: unknown packet type: %02x\n",
			 __func__, buf[FW_POS_HEADER]);
	else
		elants_i2c_mt_event(ts, buf);
}

static irqreturn_t elants_i2c_irq(int irq, void *_dev)
{
	const u8 wait_packet[] = { 0x64, 0x64, 0x64, 0x64 };
	struct elants_data *ts = _dev;
	struct i2c_client *client = ts->client;
	int report_count, report_len;
	int i;
	int len;

	len = i2c_master_recv(client, ts->buf, sizeof(ts->buf));
	if (len < 0) {
		dev_err(&client->dev, "%s: failed to read data: %d\n",
			__func__, len);
		goto out;
	}

	dev_dbg(&client->dev, "%s: packet %*ph\n",
		__func__, HEADER_SIZE, ts->buf);

	switch (ts->state) {
	case ELAN_WAIT_RECALIBRATION:
		if (ts->buf[FW_HDR_TYPE] == CMD_HEADER_REK) {
			memcpy(ts->cmd_resp, ts->buf, sizeof(ts->cmd_resp));
			complete(&ts->cmd_done);
			ts->state = ELAN_STATE_NORMAL;
		}
		break;

	case ELAN_WAIT_QUEUE_HEADER:
		if (ts->buf[FW_HDR_TYPE] != QUEUE_HEADER_NORMAL)
			break;

		ts->state = ELAN_STATE_NORMAL;
		/* fall through */

	case ELAN_STATE_NORMAL:

		switch (ts->buf[FW_HDR_TYPE]) {
		case CMD_HEADER_HELLO:
		case CMD_HEADER_RESP:
		case CMD_HEADER_REK:
			break;

		case QUEUE_HEADER_WAIT:
			if (memcmp(ts->buf, wait_packet, sizeof(wait_packet))) {
				dev_err(&client->dev,
					"invalid wait packet %*ph\n",
					HEADER_SIZE, ts->buf);
			} else {
				ts->state = ELAN_WAIT_QUEUE_HEADER;
				udelay(30);
			}
			break;

		case QUEUE_HEADER_SINGLE:
			elants_i2c_event(ts, &ts->buf[HEADER_SIZE]);
			break;

		case QUEUE_HEADER_NORMAL:
			report_count = ts->buf[FW_HDR_COUNT];
			if (report_count == 0 || report_count > 3) {
				dev_err(&client->dev,
					"bad report count: %*ph\n",
					HEADER_SIZE, ts->buf);
				break;
			}

			report_len = ts->buf[FW_HDR_LENGTH] / report_count;
			if (report_len != PACKET_SIZE) {
				dev_err(&client->dev,
					"mismatching report length: %*ph\n",
					HEADER_SIZE, ts->buf);
				break;
			}

			for (i = 0; i < report_count; i++) {
				u8 *buf = ts->buf + HEADER_SIZE +
							i * PACKET_SIZE;
				elants_i2c_event(ts, buf);
			}
			break;

		default:
			dev_err(&client->dev, "unknown packet %*ph\n",
				HEADER_SIZE, ts->buf);
			break;
		}
		break;
	}

out:
	return IRQ_HANDLED;
}

/*
 * sysfs interface
 */
static ssize_t calibrate_store(struct device *dev,
			       struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);
	int error;

	error = mutex_lock_interruptible(&ts->sysfs_mutex);
	if (error)
		return error;

	error = elants_i2c_calibrate(ts);

	mutex_unlock(&ts->sysfs_mutex);
	return error ?: count;
}

static ssize_t write_update_fw(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);
	int error;

	error = mutex_lock_interruptible(&ts->sysfs_mutex);
	if (error)
		return error;

	error = elants_i2c_fw_update(ts);
	dev_dbg(dev, "firmware update result: %d\n", error);

	mutex_unlock(&ts->sysfs_mutex);
	return error ?: count;
}

static ssize_t show_iap_mode(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "%s\n",
		       ts->iap_mode == ELAN_IAP_OPERATIONAL ?
				"Normal" : "Recovery");
}

static DEVICE_ATTR(calibrate, S_IWUSR, NULL, calibrate_store);
static DEVICE_ATTR(iap_mode, S_IRUGO, show_iap_mode, NULL);
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, write_update_fw);

struct elants_version_attribute {
	struct device_attribute dattr;
	size_t field_offset;
	size_t field_size;
};

#define __ELANTS_FIELD_SIZE(_field)					\
	sizeof(((struct elants_data *)NULL)->_field)
#define __ELANTS_VERIFY_SIZE(_field)					\
	(BUILD_BUG_ON_ZERO(__ELANTS_FIELD_SIZE(_field) > 2) +		\
	 __ELANTS_FIELD_SIZE(_field))
#define ELANTS_VERSION_ATTR(_field)					\
	struct elants_version_attribute elants_ver_attr_##_field = {	\
		.dattr = __ATTR(_field, S_IRUGO,			\
				elants_version_attribute_show, NULL),	\
		.field_offset = offsetof(struct elants_data, _field),	\
		.field_size = __ELANTS_VERIFY_SIZE(_field),		\
	}

static ssize_t elants_version_attribute_show(struct device *dev,
					     struct device_attribute *dattr,
					     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);
	struct elants_version_attribute *attr =
		container_of(dattr, struct elants_version_attribute, dattr);
	u8 *field = (u8 *)((char *)ts + attr->field_offset);
	unsigned int fmt_size;
	unsigned int val;

	if (attr->field_size == 1) {
		val = *field;
		fmt_size = 2; /* 2 HEX digits */
	} else {
		val = *(u16 *)field;
		fmt_size = 4; /* 4 HEX digits */
	}

	return sprintf(buf, "%0*x\n", fmt_size, val);
}

static ELANTS_VERSION_ATTR(fw_version);
static ELANTS_VERSION_ATTR(hw_version);
static ELANTS_VERSION_ATTR(test_version);
static ELANTS_VERSION_ATTR(solution_version);
static ELANTS_VERSION_ATTR(bc_version);
static ELANTS_VERSION_ATTR(iap_version);

static struct attribute *elants_attributes[] = {
	&dev_attr_calibrate.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_iap_mode.attr,

	&elants_ver_attr_fw_version.dattr.attr,
	&elants_ver_attr_hw_version.dattr.attr,
	&elants_ver_attr_test_version.dattr.attr,
	&elants_ver_attr_solution_version.dattr.attr,
	&elants_ver_attr_bc_version.dattr.attr,
	&elants_ver_attr_iap_version.dattr.attr,
	NULL
};

static const struct attribute_group elants_attribute_group = {
	.attrs = elants_attributes,
};

static int elants_i2c_power_on(struct elants_data *ts)
{
	int error;

	/*
	 * If we do not have reset gpio assume platform firmware
	 * controls regulators and does power them on for us.
	 */
	if (IS_ERR_OR_NULL(ts->reset_gpio))
		return 0;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);

	error = regulator_enable(ts->vcc33);
	if (error) {
		dev_err(&ts->client->dev,
			"failed to enable vcc33 regulator: %d\n",
			error);
		goto release_reset_gpio;
	}

	error = regulator_enable(ts->vccio);
	if (error) {
		dev_err(&ts->client->dev,
			"failed to enable vccio regulator: %d\n",
			error);
		regulator_disable(ts->vcc33);
		goto release_reset_gpio;
	}

	/*
	 * We need to wait a bit after powering on controller before
	 * we are allowed to release reset GPIO.
	 */
	udelay(ELAN_POWERON_DELAY_USEC);

release_reset_gpio:
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	if (error)
		return error;

	msleep(ELAN_RESET_DELAY_MSEC);

	return 0;
}

static void elants_i2c_power_off(void *_data)
{
	struct elants_data *ts = _data;

	if (!IS_ERR_OR_NULL(ts->reset_gpio)) {
		/*
		 * Activate reset gpio to prevent leakage through the
		 * pin once we shut off power to the controller.
		 */
		gpiod_set_value_cansleep(ts->reset_gpio, 1);
		regulator_disable(ts->vccio);
		regulator_disable(ts->vcc33);
	}
}

static int elants_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	union i2c_smbus_data dummy;
	struct elants_data *ts;
	unsigned long irqflags;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"%s: i2c check functionality error\n", DEVICE_NAME);
		return -ENXIO;
	}

	ts = devm_kzalloc(&client->dev, sizeof(struct elants_data), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	mutex_init(&ts->sysfs_mutex);
	init_completion(&ts->cmd_done);

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->vcc33 = devm_regulator_get(&client->dev, "vcc33");
	if (IS_ERR(ts->vcc33)) {
		error = PTR_ERR(ts->vcc33);
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev,
				"Failed to get 'vcc33' regulator: %d\n",
				error);
		return error;
	}

	ts->vccio = devm_regulator_get(&client->dev, "vccio");
	if (IS_ERR(ts->vccio)) {
		error = PTR_ERR(ts->vccio);
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev,
				"Failed to get 'vccio' regulator: %d\n",
				error);
		return error;
	}

	ts->reset_gpio = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);

		if (error == -EPROBE_DEFER)
			return error;

		if (error != -ENOENT && error != -ENOSYS) {
			dev_err(&client->dev,
				"failed to get reset gpio: %d\n",
				error);
			return error;
		}

		ts->keep_power_in_suspend = true;
	}

	error = elants_i2c_power_on(ts);
	if (error)
		return error;

	error = devm_add_action(&client->dev, elants_i2c_power_off, ts);
	if (error) {
		dev_err(&client->dev,
			"failed to install power off action: %d\n", error);
		elants_i2c_power_off(ts);
		return error;
	}

	/* Make sure there is something at this address */
	if (i2c_smbus_xfer(client->adapter, client->addr, 0,
			   I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &dummy) < 0) {
		dev_err(&client->dev, "nothing at this address\n");
		return -ENXIO;
	}

	error = elants_i2c_initialize(ts);
	if (error) {
		dev_err(&client->dev, "failed to initialize: %d\n", error);
		return error;
	}

	ts->input = devm_input_allocate_device(&client->dev);
	if (!ts->input) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input->name = "Elan Touchscreen";
	ts->input->id.bustype = BUS_I2C;

	__set_bit(BTN_TOUCH, ts->input->keybit);
	__set_bit(EV_ABS, ts->input->evbit);
	__set_bit(EV_KEY, ts->input->evbit);

	/* Single touch input params setup */
	input_set_abs_params(ts->input, ABS_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(ts->input, ABS_Y, 0, ts->y_max, 0, 0);
	input_set_abs_params(ts->input, ABS_PRESSURE, 0, 255, 0, 0);
	input_abs_set_res(ts->input, ABS_X, ts->x_res);
	input_abs_set_res(ts->input, ABS_Y, ts->y_res);

	/* Multitouch input params setup */
	error = input_mt_init_slots(ts->input, MAX_CONTACT_NUM,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&client->dev,
			"failed to initialize MT slots: %d\n", error);
		return error;
	}

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->y_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_abs_set_res(ts->input, ABS_MT_POSITION_X, ts->x_res);
	input_abs_set_res(ts->input, ABS_MT_POSITION_Y, ts->y_res);

	error = input_register_device(ts->input);
	if (error) {
		dev_err(&client->dev,
			"unable to register input device: %d\n", error);
		return error;
	}

	/*
	 * Platform code (ACPI, DTS) should normally set up interrupt
	 * for us, but in case it did not let's fall back to using falling
	 * edge to be compatible with older Chromebooks.
	 */
	irqflags = irq_get_trigger_type(client->irq);
	if (!irqflags)
		irqflags = IRQF_TRIGGER_FALLING;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, elants_i2c_irq,
					  irqflags | IRQF_ONESHOT,
					  client->name, ts);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	/*
	 * Systems using device tree should set up wakeup via DTS,
	 * the rest will configure device as wakeup source by default.
	 */
	if (!client->dev.of_node)
		device_init_wakeup(&client->dev, true);

	error = devm_device_add_group(&client->dev, &elants_attribute_group);
	if (error) {
		dev_err(&client->dev, "failed to create sysfs attributes: %d\n",
			error);
		return error;
	}

	return 0;
}

static int __maybe_unused elants_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);
	const u8 set_sleep_cmd[] = { 0x54, 0x50, 0x00, 0x01 };
	int retry_cnt;
	int error;

	/* Command not support in IAP recovery mode */
	if (ts->iap_mode != ELAN_IAP_OPERATIONAL)
		return -EBUSY;

	disable_irq(client->irq);

	if (device_may_wakeup(dev)) {
		/*
		 * The device will automatically enter idle mode
		 * that has reduced power consumption.
		 */
		ts->wake_irq_enabled = (enable_irq_wake(client->irq) == 0);
	} else if (ts->keep_power_in_suspend) {
		for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
			error = elants_i2c_send(client, set_sleep_cmd,
						sizeof(set_sleep_cmd));
			if (!error)
				break;

			dev_err(&client->dev,
				"suspend command failed: %d\n", error);
		}
	} else {
		elants_i2c_power_off(ts);
	}

	return 0;
}

static int __maybe_unused elants_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elants_data *ts = i2c_get_clientdata(client);
	const u8 set_active_cmd[] = { 0x54, 0x58, 0x00, 0x01 };
	int retry_cnt;
	int error;

	if (device_may_wakeup(dev)) {
		if (ts->wake_irq_enabled)
			disable_irq_wake(client->irq);
		elants_i2c_sw_reset(client);
	} else if (ts->keep_power_in_suspend) {
		for (retry_cnt = 0; retry_cnt < MAX_RETRIES; retry_cnt++) {
			error = elants_i2c_send(client, set_active_cmd,
						sizeof(set_active_cmd));
			if (!error)
				break;

			dev_err(&client->dev,
				"resume command failed: %d\n", error);
		}
	} else {
		elants_i2c_power_on(ts);
		elants_i2c_initialize(ts);
	}

	ts->state = ELAN_STATE_NORMAL;
	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(elants_i2c_pm_ops,
			 elants_i2c_suspend, elants_i2c_resume);

static const struct i2c_device_id elants_i2c_id[] = {
	{ DEVICE_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, elants_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id elants_acpi_id[] = {
	{ "ELAN0001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, elants_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id elants_of_match[] = {
	{ .compatible = "elan,ekth3500" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, elants_of_match);
#endif

static struct i2c_driver elants_i2c_driver = {
	.probe = elants_i2c_probe,
	.id_table = elants_i2c_id,
	.driver = {
		.name = DEVICE_NAME,
		.pm = &elants_i2c_pm_ops,
		.acpi_match_table = ACPI_PTR(elants_acpi_id),
		.of_match_table = of_match_ptr(elants_of_match),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_i2c_driver(elants_i2c_driver);

MODULE_AUTHOR("Scott Liu <scott.liu@emc.com.tw>");
MODULE_DESCRIPTION("Elan I2c Touchscreen driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
