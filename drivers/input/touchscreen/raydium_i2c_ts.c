// SPDX-License-Identifier: GPL-2.0-only
/*
 * Raydium touchscreen I2C driver.
 *
 * Copyright (C) 2012-2014, Raydium Semiconductor Corporation.
 *
 * Raydium reserves the right to make changes without further notice
 * to the materials described herein. Raydium does not assume any
 * liability arising out of the application described herein.
 *
 * Contact Raydium Semiconductor Corporation at www.rad-ic.com
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_wakeirq.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

/* Slave I2C mode */
#define RM_BOOT_BLDR		0x02
#define RM_BOOT_MAIN		0x03

/* I2C bootoloader commands */
#define RM_CMD_BOOT_PAGE_WRT	0x0B		/* send bl page write */
#define RM_CMD_BOOT_WRT		0x11		/* send bl write */
#define RM_CMD_BOOT_ACK		0x22		/* send ack*/
#define RM_CMD_BOOT_CHK		0x33		/* send data check */
#define RM_CMD_BOOT_READ	0x44		/* send wait bl data ready*/

#define RM_BOOT_RDY		0xFF		/* bl data ready */
#define RM_BOOT_CMD_READHWID	0x0E		/* read hwid */

/* I2C main commands */
#define RM_CMD_QUERY_BANK	0x2B
#define RM_CMD_DATA_BANK	0x4D
#define RM_CMD_ENTER_SLEEP	0x4E
#define RM_CMD_BANK_SWITCH	0xAA

#define RM_RESET_MSG_ADDR	0x40000004

#define RM_MAX_READ_SIZE	56
#define RM_PACKET_CRC_SIZE	2

/* Touch relative info */
#define RM_MAX_RETRIES		3
#define RM_RETRY_DELAY_MS	20
#define RM_MAX_TOUCH_NUM	10
#define RM_BOOT_DELAY_MS	100

/* Offsets in contact data */
#define RM_CONTACT_STATE_POS	0
#define RM_CONTACT_X_POS	1
#define RM_CONTACT_Y_POS	3
#define RM_CONTACT_PRESSURE_POS	5
#define RM_CONTACT_WIDTH_X_POS	6
#define RM_CONTACT_WIDTH_Y_POS	7

/* Bootloader relative info */
#define RM_BL_WRT_CMD_SIZE	3	/* bl flash wrt cmd size */
#define RM_BL_WRT_PKG_SIZE	32	/* bl wrt pkg size */
#define RM_BL_WRT_LEN		(RM_BL_WRT_PKG_SIZE + RM_BL_WRT_CMD_SIZE)
#define RM_FW_PAGE_SIZE		128
#define RM_MAX_FW_RETRIES	30
#define RM_MAX_FW_SIZE		0xD000

#define RM_POWERON_DELAY_USEC	500
#define RM_RESET_DELAY_MSEC	50

enum raydium_bl_cmd {
	BL_HEADER = 0,
	BL_PAGE_STR,
	BL_PKG_IDX,
	BL_DATA_STR,
};

enum raydium_bl_ack {
	RAYDIUM_ACK_NULL = 0,
	RAYDIUM_WAIT_READY,
	RAYDIUM_PATH_READY,
};

enum raydium_boot_mode {
	RAYDIUM_TS_MAIN = 0,
	RAYDIUM_TS_BLDR,
};

/* Response to RM_CMD_DATA_BANK request */
struct raydium_data_info {
	__le32 data_bank_addr;
	u8 pkg_size;
	u8 tp_info_size;
};

struct raydium_info {
	__le32 hw_ver;		/*device version */
	u8 main_ver;
	u8 sub_ver;
	__le16 ft_ver;		/* test version */
	u8 x_num;
	u8 y_num;
	__le16 x_max;
	__le16 y_max;
	u8 x_res;		/* units/mm */
	u8 y_res;		/* units/mm */
};

/* struct raydium_data - represents state of Raydium touchscreen device */
struct raydium_data {
	struct i2c_client *client;
	struct input_dev *input;

	struct regulator *avdd;
	struct regulator *vccio;
	struct gpio_desc *reset_gpio;

	struct raydium_info info;

	struct mutex sysfs_mutex;

	u8 *report_data;

	u32 data_bank_addr;
	u8 report_size;
	u8 contact_size;
	u8 pkg_size;

	enum raydium_boot_mode boot_mode;
};

/*
 * Header to be sent for RM_CMD_BANK_SWITCH command. This is used by
 * raydium_i2c_{read|send} below.
 */
struct __packed raydium_bank_switch_header {
	u8 cmd;
	__be32 be_addr;
};

static int raydium_i2c_xfer(struct i2c_client *client, u32 addr,
			    struct i2c_msg *xfer, size_t xfer_count)
{
	int ret;
	/*
	 * If address is greater than 255, then RM_CMD_BANK_SWITCH needs to be
	 * sent first. Else, skip the header i.e. xfer[0].
	 */
	int xfer_start_idx = (addr > 0xff) ? 0 : 1;
	xfer_count -= xfer_start_idx;

	ret = i2c_transfer(client->adapter, &xfer[xfer_start_idx], xfer_count);
	if (likely(ret == xfer_count))
		return 0;

	return ret < 0 ? ret : -EIO;
}

static int raydium_i2c_send(struct i2c_client *client,
			    u32 addr, const void *data, size_t len)
{
	int tries = 0;
	int error;
	u8 *tx_buf;
	u8 reg_addr = addr & 0xff;

	tx_buf = kmalloc(len + 1, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	tx_buf[0] = reg_addr;
	memcpy(tx_buf + 1, data, len);

	do {
		struct raydium_bank_switch_header header = {
			.cmd = RM_CMD_BANK_SWITCH,
			.be_addr = cpu_to_be32(addr),
		};

		/*
		 * Perform as a single i2c_transfer transaction to ensure that
		 * no other I2C transactions are initiated on the bus to any
		 * other device in between. Initiating transacations to other
		 * devices after RM_CMD_BANK_SWITCH is sent is known to cause
		 * issues. This is also why regmap infrastructure cannot be used
		 * for this driver. Regmap handles page(bank) switch and reads
		 * as separate i2c_transfer() operations. This can result in
		 * problems if the Raydium device is on a shared I2C bus.
		 */
		struct i2c_msg xfer[] = {
			{
				.addr = client->addr,
				.len = sizeof(header),
				.buf = (u8 *)&header,
			},
			{
				.addr = client->addr,
				.len = len + 1,
				.buf = tx_buf,
			},
		};

		error = raydium_i2c_xfer(client, addr, xfer, ARRAY_SIZE(xfer));
		if (likely(!error))
			goto out;

		msleep(RM_RETRY_DELAY_MS);
	} while (++tries < RM_MAX_RETRIES);

	dev_err(&client->dev, "%s failed: %d\n", __func__, error);
out:
	kfree(tx_buf);
	return error;
}

static int raydium_i2c_read(struct i2c_client *client,
			    u32 addr, void *data, size_t len)
{
	int error;

	while (len) {
		u8 reg_addr = addr & 0xff;
		struct raydium_bank_switch_header header = {
			.cmd = RM_CMD_BANK_SWITCH,
			.be_addr = cpu_to_be32(addr),
		};
		size_t xfer_len = min_t(size_t, len, RM_MAX_READ_SIZE);

		/*
		 * Perform as a single i2c_transfer transaction to ensure that
		 * no other I2C transactions are initiated on the bus to any
		 * other device in between. Initiating transacations to other
		 * devices after RM_CMD_BANK_SWITCH is sent is known to cause
		 * issues. This is also why regmap infrastructure cannot be used
		 * for this driver. Regmap handles page(bank) switch and writes
		 * as separate i2c_transfer() operations. This can result in
		 * problems if the Raydium device is on a shared I2C bus.
		 */
		struct i2c_msg xfer[] = {
			{
				.addr = client->addr,
				.len = sizeof(header),
				.buf = (u8 *)&header,
			},
			{
				.addr = client->addr,
				.len = 1,
				.buf = &reg_addr,
			},
			{
				.addr = client->addr,
				.len = xfer_len,
				.buf = data,
				.flags = I2C_M_RD,
			}
		};

		error = raydium_i2c_xfer(client, addr, xfer, ARRAY_SIZE(xfer));
		if (unlikely(error))
			return error;

		len -= xfer_len;
		data += xfer_len;
		addr += xfer_len;
	}

	return 0;
}

static int raydium_i2c_sw_reset(struct i2c_client *client)
{
	const u8 soft_rst_cmd = 0x01;
	int error;

	error = raydium_i2c_send(client, RM_RESET_MSG_ADDR, &soft_rst_cmd,
				 sizeof(soft_rst_cmd));
	if (error) {
		dev_err(&client->dev, "software reset failed: %d\n", error);
		return error;
	}

	msleep(RM_RESET_DELAY_MSEC);

	return 0;
}

static int raydium_i2c_query_ts_bootloader_info(struct raydium_data *ts)
{
	struct i2c_client *client = ts->client;
	static const u8 get_hwid[] = { RM_BOOT_CMD_READHWID,
				       0x10, 0xc0, 0x01, 0x00, 0x04, 0x00 };
	u8 rbuf[5] = { 0 };
	u32 hw_ver;
	int error;

	error = raydium_i2c_send(client, RM_CMD_BOOT_WRT,
				 get_hwid, sizeof(get_hwid));
	if (error) {
		dev_err(&client->dev, "WRT HWID command failed: %d\n", error);
		return error;
	}

	error = raydium_i2c_send(client, RM_CMD_BOOT_ACK, rbuf, 1);
	if (error) {
		dev_err(&client->dev, "Ack HWID command failed: %d\n", error);
		return error;
	}

	error = raydium_i2c_read(client, RM_CMD_BOOT_CHK, rbuf, sizeof(rbuf));
	if (error) {
		dev_err(&client->dev, "Read HWID command failed: %d (%4ph)\n",
			error, rbuf + 1);
		hw_ver = 0xffffffffUL;
	} else {
		hw_ver = get_unaligned_be32(rbuf + 1);
	}

	ts->info.hw_ver = cpu_to_le32(hw_ver);
	ts->info.main_ver = 0xff;
	ts->info.sub_ver = 0xff;

	return error;
}

static int raydium_i2c_query_ts_info(struct raydium_data *ts)
{
	struct i2c_client *client = ts->client;
	struct raydium_data_info data_info;
	__le32 query_bank_addr;

	int error, retry_cnt;

	for (retry_cnt = 0; retry_cnt < RM_MAX_RETRIES; retry_cnt++) {
		error = raydium_i2c_read(client, RM_CMD_DATA_BANK,
					 &data_info, sizeof(data_info));
		if (error)
			continue;

		/*
		 * Warn user if we already allocated memory for reports and
		 * then the size changed (due to firmware update?) and keep
		 * old size instead.
		 */
		if (ts->report_data && ts->pkg_size != data_info.pkg_size) {
			dev_warn(&client->dev,
				 "report size changes, was: %d, new: %d\n",
				 ts->pkg_size, data_info.pkg_size);
		} else {
			ts->pkg_size = data_info.pkg_size;
			ts->report_size = ts->pkg_size - RM_PACKET_CRC_SIZE;
		}

		ts->contact_size = data_info.tp_info_size;
		ts->data_bank_addr = le32_to_cpu(data_info.data_bank_addr);

		dev_dbg(&client->dev,
			"data_bank_addr: %#08x, report_size: %d, contact_size: %d\n",
			ts->data_bank_addr, ts->report_size, ts->contact_size);

		error = raydium_i2c_read(client, RM_CMD_QUERY_BANK,
					 &query_bank_addr,
					 sizeof(query_bank_addr));
		if (error)
			continue;

		error = raydium_i2c_read(client, le32_to_cpu(query_bank_addr),
					 &ts->info, sizeof(ts->info));
		if (error)
			continue;

		return 0;
	}

	dev_err(&client->dev, "failed to query device parameters: %d\n", error);
	return error;
}

static int raydium_i2c_check_fw_status(struct raydium_data *ts)
{
	struct i2c_client *client = ts->client;
	static const u8 bl_ack = 0x62;
	static const u8 main_ack = 0x66;
	u8 buf[4];
	int error;

	error = raydium_i2c_read(client, RM_CMD_BOOT_READ, buf, sizeof(buf));
	if (!error) {
		if (buf[0] == bl_ack)
			ts->boot_mode = RAYDIUM_TS_BLDR;
		else if (buf[0] == main_ack)
			ts->boot_mode = RAYDIUM_TS_MAIN;
		return 0;
	}

	return error;
}

static int raydium_i2c_initialize(struct raydium_data *ts)
{
	struct i2c_client *client = ts->client;
	int error, retry_cnt;

	for (retry_cnt = 0; retry_cnt < RM_MAX_RETRIES; retry_cnt++) {
		/* Wait for Hello packet */
		msleep(RM_BOOT_DELAY_MS);

		error = raydium_i2c_check_fw_status(ts);
		if (error) {
			dev_err(&client->dev,
				"failed to read 'hello' packet: %d\n", error);
			continue;
		}

		if (ts->boot_mode == RAYDIUM_TS_BLDR ||
		    ts->boot_mode == RAYDIUM_TS_MAIN) {
			break;
		}
	}

	if (error)
		ts->boot_mode = RAYDIUM_TS_BLDR;

	if (ts->boot_mode == RAYDIUM_TS_BLDR)
		raydium_i2c_query_ts_bootloader_info(ts);
	else
		raydium_i2c_query_ts_info(ts);

	return error;
}

static int raydium_i2c_bl_chk_state(struct i2c_client *client,
				    enum raydium_bl_ack state)
{
	static const u8 ack_ok[] = { 0xFF, 0x39, 0x30, 0x30, 0x54 };
	u8 rbuf[sizeof(ack_ok)];
	u8 retry;
	int error;

	for (retry = 0; retry < RM_MAX_FW_RETRIES; retry++) {
		switch (state) {
		case RAYDIUM_ACK_NULL:
			return 0;

		case RAYDIUM_WAIT_READY:
			error = raydium_i2c_read(client, RM_CMD_BOOT_CHK,
						 &rbuf[0], 1);
			if (!error && rbuf[0] == RM_BOOT_RDY)
				return 0;

			break;

		case RAYDIUM_PATH_READY:
			error = raydium_i2c_read(client, RM_CMD_BOOT_CHK,
						 rbuf, sizeof(rbuf));
			if (!error && !memcmp(rbuf, ack_ok, sizeof(ack_ok)))
				return 0;

			break;

		default:
			dev_err(&client->dev, "%s: invalid target state %d\n",
				__func__, state);
			return -EINVAL;
		}

		msleep(20);
	}

	return -ETIMEDOUT;
}

static int raydium_i2c_write_object(struct i2c_client *client,
				    const void *data, size_t len,
				    enum raydium_bl_ack state)
{
	int error;
	static const u8 cmd[] = { 0xFF, 0x39 };

	error = raydium_i2c_send(client, RM_CMD_BOOT_WRT, data, len);
	if (error) {
		dev_err(&client->dev, "WRT obj command failed: %d\n",
			error);
		return error;
	}

	error = raydium_i2c_send(client, RM_CMD_BOOT_ACK, cmd, sizeof(cmd));
	if (error) {
		dev_err(&client->dev, "Ack obj command failed: %d\n", error);
		return error;
	}

	error = raydium_i2c_bl_chk_state(client, state);
	if (error) {
		dev_err(&client->dev, "BL check state failed: %d\n", error);
		return error;
	}
	return 0;
}

static int raydium_i2c_boot_trigger(struct i2c_client *client)
{
	static const u8 cmd[7][6] = {
		{ 0x08, 0x0C, 0x09, 0x00, 0x50, 0xD7 },
		{ 0x08, 0x04, 0x09, 0x00, 0x50, 0xA5 },
		{ 0x08, 0x04, 0x09, 0x00, 0x50, 0x00 },
		{ 0x08, 0x04, 0x09, 0x00, 0x50, 0xA5 },
		{ 0x08, 0x0C, 0x09, 0x00, 0x50, 0x00 },
		{ 0x06, 0x01, 0x00, 0x00, 0x00, 0x00 },
		{ 0x02, 0xA2, 0x00, 0x00, 0x00, 0x00 },
	};
	int i;
	int error;

	for (i = 0; i < 7; i++) {
		error = raydium_i2c_write_object(client, cmd[i], sizeof(cmd[i]),
						 RAYDIUM_WAIT_READY);
		if (error) {
			dev_err(&client->dev,
				"boot trigger failed at step %d: %d\n",
				i, error);
			return error;
		}
	}

	return 0;
}

static int raydium_i2c_fw_trigger(struct i2c_client *client)
{
	static const u8 cmd[5][11] = {
		{ 0, 0x09, 0x71, 0x0C, 0x09, 0x00, 0x50, 0xD7, 0, 0, 0 },
		{ 0, 0x09, 0x71, 0x04, 0x09, 0x00, 0x50, 0xA5, 0, 0, 0 },
		{ 0, 0x09, 0x71, 0x04, 0x09, 0x00, 0x50, 0x00, 0, 0, 0 },
		{ 0, 0x09, 0x71, 0x04, 0x09, 0x00, 0x50, 0xA5, 0, 0, 0 },
		{ 0, 0x09, 0x71, 0x0C, 0x09, 0x00, 0x50, 0x00, 0, 0, 0 },
	};
	int i;
	int error;

	for (i = 0; i < 5; i++) {
		error = raydium_i2c_write_object(client, cmd[i], sizeof(cmd[i]),
						 RAYDIUM_ACK_NULL);
		if (error) {
			dev_err(&client->dev,
				"fw trigger failed at step %d: %d\n",
				i, error);
			return error;
		}
	}

	return 0;
}

static int raydium_i2c_check_path(struct i2c_client *client)
{
	static const u8 cmd[] = { 0x09, 0x00, 0x09, 0x00, 0x50, 0x10, 0x00 };
	int error;

	error = raydium_i2c_write_object(client, cmd, sizeof(cmd),
					 RAYDIUM_PATH_READY);
	if (error) {
		dev_err(&client->dev, "check path command failed: %d\n", error);
		return error;
	}

	return 0;
}

static int raydium_i2c_enter_bl(struct i2c_client *client)
{
	static const u8 cal_cmd[] = { 0x00, 0x01, 0x52 };
	int error;

	error = raydium_i2c_write_object(client, cal_cmd, sizeof(cal_cmd),
					 RAYDIUM_ACK_NULL);
	if (error) {
		dev_err(&client->dev, "enter bl command failed: %d\n", error);
		return error;
	}

	msleep(RM_BOOT_DELAY_MS);
	return 0;
}

static int raydium_i2c_leave_bl(struct i2c_client *client)
{
	static const u8 leave_cmd[] = { 0x05, 0x00 };
	int error;

	error = raydium_i2c_write_object(client, leave_cmd, sizeof(leave_cmd),
					 RAYDIUM_ACK_NULL);
	if (error) {
		dev_err(&client->dev, "leave bl command failed: %d\n", error);
		return error;
	}

	msleep(RM_BOOT_DELAY_MS);
	return 0;
}

static int raydium_i2c_write_checksum(struct i2c_client *client,
				      size_t length, u16 checksum)
{
	u8 checksum_cmd[] = { 0x00, 0x05, 0x6D, 0x00, 0x00, 0x00, 0x00 };
	int error;

	put_unaligned_le16(length, &checksum_cmd[3]);
	put_unaligned_le16(checksum, &checksum_cmd[5]);

	error = raydium_i2c_write_object(client,
					 checksum_cmd, sizeof(checksum_cmd),
					 RAYDIUM_ACK_NULL);
	if (error) {
		dev_err(&client->dev, "failed to write checksum: %d\n",
			error);
		return error;
	}

	return 0;
}

static int raydium_i2c_disable_watch_dog(struct i2c_client *client)
{
	static const u8 cmd[] = { 0x0A, 0xAA };
	int error;

	error = raydium_i2c_write_object(client, cmd, sizeof(cmd),
					 RAYDIUM_WAIT_READY);
	if (error) {
		dev_err(&client->dev, "disable watchdog command failed: %d\n",
			error);
		return error;
	}

	return 0;
}

static int raydium_i2c_fw_write_page(struct i2c_client *client,
				     u16 page_idx, const void *data, size_t len)
{
	u8 buf[RM_BL_WRT_LEN];
	size_t xfer_len;
	int error;
	int i;

	BUILD_BUG_ON((RM_FW_PAGE_SIZE % RM_BL_WRT_PKG_SIZE) != 0);

	for (i = 0; i < RM_FW_PAGE_SIZE / RM_BL_WRT_PKG_SIZE; i++) {
		buf[BL_HEADER] = RM_CMD_BOOT_PAGE_WRT;
		buf[BL_PAGE_STR] = page_idx ? 0xff : 0;
		buf[BL_PKG_IDX] = i + 1;

		xfer_len = min_t(size_t, len, RM_BL_WRT_PKG_SIZE);
		memcpy(&buf[BL_DATA_STR], data, xfer_len);
		if (len < RM_BL_WRT_PKG_SIZE)
			memset(&buf[BL_DATA_STR + xfer_len], 0xff,
				RM_BL_WRT_PKG_SIZE - xfer_len);

		error = raydium_i2c_write_object(client, buf, RM_BL_WRT_LEN,
						 RAYDIUM_WAIT_READY);
		if (error) {
			dev_err(&client->dev,
				"page write command failed for page %d, chunk %d: %d\n",
				page_idx, i, error);
			return error;
		}

		data += xfer_len;
		len -= xfer_len;
	}

	return error;
}

static u16 raydium_calc_chksum(const u8 *buf, u16 len)
{
	u16 checksum = 0;
	u16 i;

	for (i = 0; i < len; i++)
		checksum += buf[i];

	return checksum;
}

static int raydium_i2c_do_update_firmware(struct raydium_data *ts,
					 const struct firmware *fw)
{
	struct i2c_client *client = ts->client;
	const void *data;
	size_t data_len;
	size_t len;
	int page_nr;
	int i;
	int error;
	u16 fw_checksum;

	if (fw->size == 0 || fw->size > RM_MAX_FW_SIZE) {
		dev_err(&client->dev, "Invalid firmware length\n");
		return -EINVAL;
	}

	error = raydium_i2c_check_fw_status(ts);
	if (error) {
		dev_err(&client->dev, "Unable to access IC %d\n", error);
		return error;
	}

	if (ts->boot_mode == RAYDIUM_TS_MAIN) {
		for (i = 0; i < RM_MAX_RETRIES; i++) {
			error = raydium_i2c_enter_bl(client);
			if (!error) {
				error = raydium_i2c_check_fw_status(ts);
				if (error) {
					dev_err(&client->dev,
						"unable to access IC: %d\n",
						error);
					return error;
				}

				if (ts->boot_mode == RAYDIUM_TS_BLDR)
					break;
			}
		}

		if (ts->boot_mode == RAYDIUM_TS_MAIN) {
			dev_err(&client->dev,
				"failed to jump to boot loader: %d\n",
				error);
			return -EIO;
		}
	}

	error = raydium_i2c_disable_watch_dog(client);
	if (error)
		return error;

	error = raydium_i2c_check_path(client);
	if (error)
		return error;

	error = raydium_i2c_boot_trigger(client);
	if (error) {
		dev_err(&client->dev, "send boot trigger fail: %d\n", error);
		return error;
	}

	msleep(RM_BOOT_DELAY_MS);

	data = fw->data;
	data_len = fw->size;
	page_nr = 0;

	while (data_len) {
		len = min_t(size_t, data_len, RM_FW_PAGE_SIZE);

		error = raydium_i2c_fw_write_page(client, page_nr++, data, len);
		if (error)
			return error;

		msleep(20);

		data += len;
		data_len -= len;
	}

	error = raydium_i2c_leave_bl(client);
	if (error) {
		dev_err(&client->dev,
			"failed to leave boot loader: %d\n", error);
		return error;
	}

	dev_dbg(&client->dev, "left boot loader mode\n");
	msleep(RM_BOOT_DELAY_MS);

	error = raydium_i2c_check_fw_status(ts);
	if (error) {
		dev_err(&client->dev,
			"failed to check fw status after write: %d\n",
			error);
		return error;
	}

	if (ts->boot_mode != RAYDIUM_TS_MAIN) {
		dev_err(&client->dev,
			"failed to switch to main fw after writing firmware: %d\n",
			error);
		return -EINVAL;
	}

	error = raydium_i2c_fw_trigger(client);
	if (error) {
		dev_err(&client->dev, "failed to trigger fw: %d\n", error);
		return error;
	}

	fw_checksum = raydium_calc_chksum(fw->data, fw->size);

	error = raydium_i2c_write_checksum(client, fw->size, fw_checksum);
	if (error)
		return error;

	return 0;
}

static int raydium_i2c_fw_update(struct raydium_data *ts)
{
	struct i2c_client *client = ts->client;
	const struct firmware *fw = NULL;
	char *fw_file;
	int error;

	fw_file = kasprintf(GFP_KERNEL, "raydium_%#04x.fw",
			    le32_to_cpu(ts->info.hw_ver));
	if (!fw_file)
		return -ENOMEM;

	dev_dbg(&client->dev, "firmware name: %s\n", fw_file);

	error = request_firmware(&fw, fw_file, &client->dev);
	if (error) {
		dev_err(&client->dev, "Unable to open firmware %s\n", fw_file);
		goto out_free_fw_file;
	}

	disable_irq(client->irq);

	error = raydium_i2c_do_update_firmware(ts, fw);
	if (error) {
		dev_err(&client->dev, "firmware update failed: %d\n", error);
		ts->boot_mode = RAYDIUM_TS_BLDR;
		goto out_enable_irq;
	}

	error = raydium_i2c_initialize(ts);
	if (error) {
		dev_err(&client->dev,
			"failed to initialize device after firmware update: %d\n",
			error);
		ts->boot_mode = RAYDIUM_TS_BLDR;
		goto out_enable_irq;
	}

	ts->boot_mode = RAYDIUM_TS_MAIN;

out_enable_irq:
	enable_irq(client->irq);
	msleep(100);

	release_firmware(fw);

out_free_fw_file:
	kfree(fw_file);

	return error;
}

static void raydium_mt_event(struct raydium_data *ts)
{
	int i;

	for (i = 0; i < ts->report_size / ts->contact_size; i++) {
		u8 *contact = &ts->report_data[ts->contact_size * i];
		bool state = contact[RM_CONTACT_STATE_POS];
		u8 wx, wy;

		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, state);

		if (!state)
			continue;

		input_report_abs(ts->input, ABS_MT_POSITION_X,
				get_unaligned_le16(&contact[RM_CONTACT_X_POS]));
		input_report_abs(ts->input, ABS_MT_POSITION_Y,
				get_unaligned_le16(&contact[RM_CONTACT_Y_POS]));
		input_report_abs(ts->input, ABS_MT_PRESSURE,
				contact[RM_CONTACT_PRESSURE_POS]);

		wx = contact[RM_CONTACT_WIDTH_X_POS];
		wy = contact[RM_CONTACT_WIDTH_Y_POS];

		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, max(wx, wy));
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, min(wx, wy));
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t raydium_i2c_irq(int irq, void *_dev)
{
	struct raydium_data *ts = _dev;
	int error;
	u16 fw_crc;
	u16 calc_crc;

	if (ts->boot_mode != RAYDIUM_TS_MAIN)
		goto out;

	error = raydium_i2c_read(ts->client, ts->data_bank_addr,
				 ts->report_data, ts->pkg_size);
	if (error)
		goto out;

	fw_crc = get_unaligned_le16(&ts->report_data[ts->report_size]);
	calc_crc = raydium_calc_chksum(ts->report_data, ts->report_size);
	if (unlikely(fw_crc != calc_crc)) {
		dev_warn(&ts->client->dev,
			 "%s: invalid packet crc %#04x vs %#04x\n",
			 __func__, calc_crc, fw_crc);
		goto out;
	}

	raydium_mt_event(ts);

out:
	return IRQ_HANDLED;
}

static ssize_t raydium_i2c_fw_ver_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "%d.%d\n", ts->info.main_ver, ts->info.sub_ver);
}

static ssize_t raydium_i2c_hw_ver_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "%#04x\n", le32_to_cpu(ts->info.hw_ver));
}

static ssize_t raydium_i2c_boot_mode_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "%s\n",
		       ts->boot_mode == RAYDIUM_TS_MAIN ?
				"Normal" : "Recovery");
}

static ssize_t raydium_i2c_update_fw_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);
	int error;

	error = mutex_lock_interruptible(&ts->sysfs_mutex);
	if (error)
		return error;

	error = raydium_i2c_fw_update(ts);

	mutex_unlock(&ts->sysfs_mutex);

	return error ?: count;
}

static ssize_t raydium_i2c_calibrate_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);
	static const u8 cal_cmd[] = { 0x00, 0x01, 0x9E };
	int error;

	error = mutex_lock_interruptible(&ts->sysfs_mutex);
	if (error)
		return error;

	error = raydium_i2c_write_object(client, cal_cmd, sizeof(cal_cmd),
					 RAYDIUM_WAIT_READY);
	if (error)
		dev_err(&client->dev, "calibrate command failed: %d\n", error);

	mutex_unlock(&ts->sysfs_mutex);
	return error ?: count;
}

static DEVICE_ATTR(fw_version, S_IRUGO, raydium_i2c_fw_ver_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, raydium_i2c_hw_ver_show, NULL);
static DEVICE_ATTR(boot_mode, S_IRUGO, raydium_i2c_boot_mode_show, NULL);
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, raydium_i2c_update_fw_store);
static DEVICE_ATTR(calibrate, S_IWUSR, NULL, raydium_i2c_calibrate_store);

static struct attribute *raydium_i2c_attrs[] = {
	&dev_attr_update_fw.attr,
	&dev_attr_boot_mode.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_calibrate.attr,
	NULL
};
ATTRIBUTE_GROUPS(raydium_i2c);

static int raydium_i2c_power_on(struct raydium_data *ts)
{
	int error;

	if (!ts->reset_gpio)
		return 0;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);

	error = regulator_enable(ts->avdd);
	if (error) {
		dev_err(&ts->client->dev,
			"failed to enable avdd regulator: %d\n", error);
		goto release_reset_gpio;
	}

	error = regulator_enable(ts->vccio);
	if (error) {
		regulator_disable(ts->avdd);
		dev_err(&ts->client->dev,
			"failed to enable vccio regulator: %d\n", error);
		goto release_reset_gpio;
	}

	udelay(RM_POWERON_DELAY_USEC);

release_reset_gpio:
	gpiod_set_value_cansleep(ts->reset_gpio, 0);

	if (error)
		return error;

	msleep(RM_RESET_DELAY_MSEC);

	return 0;
}

static void raydium_i2c_power_off(void *_data)
{
	struct raydium_data *ts = _data;

	if (ts->reset_gpio) {
		gpiod_set_value_cansleep(ts->reset_gpio, 1);
		regulator_disable(ts->vccio);
		regulator_disable(ts->avdd);
	}
}

static int raydium_i2c_probe(struct i2c_client *client)
{
	union i2c_smbus_data dummy;
	struct raydium_data *ts;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"i2c check functionality error (need I2C_FUNC_I2C)\n");
		return -ENXIO;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	mutex_init(&ts->sysfs_mutex);

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->avdd = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(ts->avdd))
		return dev_err_probe(&client->dev, PTR_ERR(ts->avdd),
				     "Failed to get 'avdd' regulator\n");

	ts->vccio = devm_regulator_get(&client->dev, "vccio");
	if (IS_ERR(ts->vccio))
		return dev_err_probe(&client->dev, PTR_ERR(ts->vccio),
				     "Failed to get 'vccio' regulator\n");

	ts->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						 GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(ts->reset_gpio),
				     "Failed to get reset gpio\n");

	error = raydium_i2c_power_on(ts);
	if (error)
		return error;

	error = devm_add_action_or_reset(&client->dev,
					 raydium_i2c_power_off, ts);
	if (error) {
		dev_err(&client->dev,
			"failed to install power off action: %d\n", error);
		return error;
	}

	/* Make sure there is something at this address */
	if (i2c_smbus_xfer(client->adapter, client->addr, 0,
			   I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &dummy) < 0) {
		dev_err(&client->dev, "nothing at this address\n");
		return -ENXIO;
	}

	error = raydium_i2c_initialize(ts);
	if (error) {
		dev_err(&client->dev, "failed to initialize: %d\n", error);
		return error;
	}

	ts->report_data = devm_kmalloc(&client->dev,
				       ts->pkg_size, GFP_KERNEL);
	if (!ts->report_data)
		return -ENOMEM;

	ts->input = devm_input_allocate_device(&client->dev);
	if (!ts->input) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input->name = "Raydium Touchscreen";
	ts->input->id.bustype = BUS_I2C;

	input_set_abs_params(ts->input, ABS_MT_POSITION_X,
			     0, le16_to_cpu(ts->info.x_max), 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y,
			     0, le16_to_cpu(ts->info.y_max), 0, 0);
	input_abs_set_res(ts->input, ABS_MT_POSITION_X, ts->info.x_res);
	input_abs_set_res(ts->input, ABS_MT_POSITION_Y, ts->info.y_res);

	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	error = input_mt_init_slots(ts->input, RM_MAX_TOUCH_NUM,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&client->dev,
			"failed to initialize MT slots: %d\n", error);
		return error;
	}

	error = input_register_device(ts->input);
	if (error) {
		dev_err(&client->dev,
			"unable to register input device: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, raydium_i2c_irq,
					  IRQF_ONESHOT, client->name, ts);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	return 0;
}

static void raydium_enter_sleep(struct i2c_client *client)
{
	static const u8 sleep_cmd[] = { 0x5A, 0xff, 0x00, 0x0f };
	int error;

	error = raydium_i2c_send(client, RM_CMD_ENTER_SLEEP,
				 sleep_cmd, sizeof(sleep_cmd));
	if (error)
		dev_err(&client->dev,
			"sleep command failed: %d\n", error);
}

static int raydium_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);

	/* Sleep is not available in BLDR recovery mode */
	if (ts->boot_mode != RAYDIUM_TS_MAIN)
		return -EBUSY;

	disable_irq(client->irq);

	if (device_may_wakeup(dev)) {
		raydium_enter_sleep(client);
	} else {
		raydium_i2c_power_off(ts);
	}

	return 0;
}

static int raydium_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct raydium_data *ts = i2c_get_clientdata(client);

	if (device_may_wakeup(dev)) {
		raydium_i2c_sw_reset(client);
	} else {
		raydium_i2c_power_on(ts);
		raydium_i2c_initialize(ts);
	}

	enable_irq(client->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(raydium_i2c_pm_ops,
				raydium_i2c_suspend, raydium_i2c_resume);

static const struct i2c_device_id raydium_i2c_id[] = {
	{ "raydium_i2c", 0 },
	{ "rm32380", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, raydium_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id raydium_acpi_id[] = {
	{ "RAYD0001", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, raydium_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id raydium_of_match[] = {
	{ .compatible = "raydium,rm32380", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, raydium_of_match);
#endif

static struct i2c_driver raydium_i2c_driver = {
	.probe = raydium_i2c_probe,
	.id_table = raydium_i2c_id,
	.driver = {
		.name = "raydium_ts",
		.dev_groups = raydium_i2c_groups,
		.pm = pm_sleep_ptr(&raydium_i2c_pm_ops),
		.acpi_match_table = ACPI_PTR(raydium_acpi_id),
		.of_match_table = of_match_ptr(raydium_of_match),
	},
};
module_i2c_driver(raydium_i2c_driver);

MODULE_AUTHOR("Raydium");
MODULE_DESCRIPTION("Raydium I2c Touchscreen driver");
MODULE_LICENSE("GPL v2");
