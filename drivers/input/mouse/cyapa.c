/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 * Further cleanup and restructuring by:
 *   Daniel Kurtz <djkurtz@chromium.org>
 *   Benson Leung <bleung@chromium.org>
 *
 * Copyright (C) 2011-2012 Cypress Semiconductor, Inc.
 * Copyright (C) 2011-2012 Google, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>

/* APA trackpad firmware generation */
#define CYAPA_GEN3   0x03   /* support MT-protocol B with tracking ID. */

#define CYAPA_NAME   "Cypress APA Trackpad (cyapa)"

/* commands for read/write registers of Cypress trackpad */
#define CYAPA_CMD_SOFT_RESET       0x00
#define CYAPA_CMD_POWER_MODE       0x01
#define CYAPA_CMD_DEV_STATUS       0x02
#define CYAPA_CMD_GROUP_DATA       0x03
#define CYAPA_CMD_GROUP_CMD        0x04
#define CYAPA_CMD_GROUP_QUERY      0x05
#define CYAPA_CMD_BL_STATUS        0x06
#define CYAPA_CMD_BL_HEAD          0x07
#define CYAPA_CMD_BL_CMD           0x08
#define CYAPA_CMD_BL_DATA          0x09
#define CYAPA_CMD_BL_ALL           0x0a
#define CYAPA_CMD_BLK_PRODUCT_ID   0x0b
#define CYAPA_CMD_BLK_HEAD         0x0c

/* report data start reg offset address. */
#define DATA_REG_START_OFFSET  0x0000

#define BL_HEAD_OFFSET 0x00
#define BL_DATA_OFFSET 0x10

/*
 * Operational Device Status Register
 *
 * bit 7: Valid interrupt source
 * bit 6 - 4: Reserved
 * bit 3 - 2: Power status
 * bit 1 - 0: Device status
 */
#define REG_OP_STATUS     0x00
#define OP_STATUS_SRC     0x80
#define OP_STATUS_POWER   0x0c
#define OP_STATUS_DEV     0x03
#define OP_STATUS_MASK (OP_STATUS_SRC | OP_STATUS_POWER | OP_STATUS_DEV)

/*
 * Operational Finger Count/Button Flags Register
 *
 * bit 7 - 4: Number of touched finger
 * bit 3: Valid data
 * bit 2: Middle Physical Button
 * bit 1: Right Physical Button
 * bit 0: Left physical Button
 */
#define REG_OP_DATA1       0x01
#define OP_DATA_VALID      0x08
#define OP_DATA_MIDDLE_BTN 0x04
#define OP_DATA_RIGHT_BTN  0x02
#define OP_DATA_LEFT_BTN   0x01
#define OP_DATA_BTN_MASK (OP_DATA_MIDDLE_BTN | OP_DATA_RIGHT_BTN | \
			  OP_DATA_LEFT_BTN)

/*
 * Bootloader Status Register
 *
 * bit 7: Busy
 * bit 6 - 5: Reserved
 * bit 4: Bootloader running
 * bit 3 - 1: Reserved
 * bit 0: Checksum valid
 */
#define REG_BL_STATUS        0x01
#define BL_STATUS_BUSY       0x80
#define BL_STATUS_RUNNING    0x10
#define BL_STATUS_DATA_VALID 0x08
#define BL_STATUS_CSUM_VALID 0x01

/*
 * Bootloader Error Register
 *
 * bit 7: Invalid
 * bit 6: Invalid security key
 * bit 5: Bootloading
 * bit 4: Command checksum
 * bit 3: Flash protection error
 * bit 2: Flash checksum error
 * bit 1 - 0: Reserved
 */
#define REG_BL_ERROR         0x02
#define BL_ERROR_INVALID     0x80
#define BL_ERROR_INVALID_KEY 0x40
#define BL_ERROR_BOOTLOADING 0x20
#define BL_ERROR_CMD_CSUM    0x10
#define BL_ERROR_FLASH_PROT  0x08
#define BL_ERROR_FLASH_CSUM  0x04

#define BL_STATUS_SIZE  3  /* length of bootloader status registers */
#define BLK_HEAD_BYTES 32

#define PRODUCT_ID_SIZE  16
#define QUERY_DATA_SIZE  27
#define REG_PROTOCOL_GEN_QUERY_OFFSET  20

#define REG_OFFSET_DATA_BASE     0x0000
#define REG_OFFSET_COMMAND_BASE  0x0028
#define REG_OFFSET_QUERY_BASE    0x002a

#define CAPABILITY_LEFT_BTN_MASK	(0x01 << 3)
#define CAPABILITY_RIGHT_BTN_MASK	(0x01 << 4)
#define CAPABILITY_MIDDLE_BTN_MASK	(0x01 << 5)
#define CAPABILITY_BTN_MASK  (CAPABILITY_LEFT_BTN_MASK | \
			      CAPABILITY_RIGHT_BTN_MASK | \
			      CAPABILITY_MIDDLE_BTN_MASK)

#define CYAPA_OFFSET_SOFT_RESET  REG_OFFSET_COMMAND_BASE

#define REG_OFFSET_POWER_MODE (REG_OFFSET_COMMAND_BASE + 1)

#define PWR_MODE_MASK   0xfc
#define PWR_MODE_FULL_ACTIVE (0x3f << 2)
#define PWR_MODE_IDLE        (0x05 << 2) /* default sleep time is 50 ms. */
#define PWR_MODE_OFF         (0x00 << 2)

#define PWR_STATUS_MASK      0x0c
#define PWR_STATUS_ACTIVE    (0x03 << 2)
#define PWR_STATUS_IDLE      (0x02 << 2)
#define PWR_STATUS_OFF       (0x00 << 2)

/*
 * CYAPA trackpad device states.
 * Used in register 0x00, bit1-0, DeviceStatus field.
 * Other values indicate device is in an abnormal state and must be reset.
 */
#define CYAPA_DEV_NORMAL  0x03
#define CYAPA_DEV_BUSY    0x01

enum cyapa_state {
	CYAPA_STATE_OP,
	CYAPA_STATE_BL_IDLE,
	CYAPA_STATE_BL_ACTIVE,
	CYAPA_STATE_BL_BUSY,
	CYAPA_STATE_NO_DEVICE,
};


struct cyapa_touch {
	/*
	 * high bits or x/y position value
	 * bit 7 - 4: high 4 bits of x position value
	 * bit 3 - 0: high 4 bits of y position value
	 */
	u8 xy_hi;
	u8 x_lo;  /* low 8 bits of x position value. */
	u8 y_lo;  /* low 8 bits of y position value. */
	u8 pressure;
	/* id range is 1 - 15.  It is incremented with every new touch. */
	u8 id;
} __packed;

/* The touch.id is used as the MT slot id, thus max MT slot is 15 */
#define CYAPA_MAX_MT_SLOTS  15

struct cyapa_reg_data {
	/*
	 * bit 0 - 1: device status
	 * bit 3 - 2: power mode
	 * bit 6 - 4: reserved
	 * bit 7: interrupt valid bit
	 */
	u8 device_status;
	/*
	 * bit 7 - 4: number of fingers currently touching pad
	 * bit 3: valid data check bit
	 * bit 2: middle mechanism button state if exists
	 * bit 1: right mechanism button state if exists
	 * bit 0: left mechanism button state if exists
	 */
	u8 finger_btn;
	/* CYAPA reports up to 5 touches per packet. */
	struct cyapa_touch touches[5];
} __packed;

/* The main device structure */
struct cyapa {
	enum cyapa_state state;

	struct i2c_client *client;
	struct input_dev *input;
	char phys[32];	/* device physical location */
	int irq;
	bool irq_wake;  /* irq wake is enabled */
	bool smbus;

	/* read from query data region. */
	char product_id[16];
	u8 btn_capability;
	u8 gen;
	int max_abs_x;
	int max_abs_y;
	int physical_size_x;
	int physical_size_y;
};

static const u8 bl_deactivate[] = { 0x00, 0xff, 0x3b, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07 };
static const u8 bl_exit[] = { 0x00, 0xff, 0xa5, 0x00, 0x01, 0x02, 0x03, 0x04,
		0x05, 0x06, 0x07 };

struct cyapa_cmd_len {
	u8 cmd;
	u8 len;
};

#define CYAPA_ADAPTER_FUNC_NONE   0
#define CYAPA_ADAPTER_FUNC_I2C    1
#define CYAPA_ADAPTER_FUNC_SMBUS  2
#define CYAPA_ADAPTER_FUNC_BOTH   3

/*
 * macros for SMBus communication
 */
#define SMBUS_READ   0x01
#define SMBUS_WRITE 0x00
#define SMBUS_ENCODE_IDX(cmd, idx) ((cmd) | (((idx) & 0x03) << 1))
#define SMBUS_ENCODE_RW(cmd, rw) ((cmd) | ((rw) & 0x01))
#define SMBUS_BYTE_BLOCK_CMD_MASK 0x80
#define SMBUS_GROUP_BLOCK_CMD_MASK 0x40

 /* for byte read/write command */
#define CMD_RESET 0
#define CMD_POWER_MODE 1
#define CMD_DEV_STATUS 2
#define SMBUS_BYTE_CMD(cmd) (((cmd) & 0x3f) << 1)
#define CYAPA_SMBUS_RESET SMBUS_BYTE_CMD(CMD_RESET)
#define CYAPA_SMBUS_POWER_MODE SMBUS_BYTE_CMD(CMD_POWER_MODE)
#define CYAPA_SMBUS_DEV_STATUS SMBUS_BYTE_CMD(CMD_DEV_STATUS)

 /* for group registers read/write command */
#define REG_GROUP_DATA 0
#define REG_GROUP_CMD 2
#define REG_GROUP_QUERY 3
#define SMBUS_GROUP_CMD(grp) (0x80 | (((grp) & 0x07) << 3))
#define CYAPA_SMBUS_GROUP_DATA SMBUS_GROUP_CMD(REG_GROUP_DATA)
#define CYAPA_SMBUS_GROUP_CMD SMBUS_GROUP_CMD(REG_GROUP_CMD)
#define CYAPA_SMBUS_GROUP_QUERY SMBUS_GROUP_CMD(REG_GROUP_QUERY)

 /* for register block read/write command */
#define CMD_BL_STATUS 0
#define CMD_BL_HEAD 1
#define CMD_BL_CMD 2
#define CMD_BL_DATA 3
#define CMD_BL_ALL 4
#define CMD_BLK_PRODUCT_ID 5
#define CMD_BLK_HEAD 6
#define SMBUS_BLOCK_CMD(cmd) (0xc0 | (((cmd) & 0x1f) << 1))

/* register block read/write command in bootloader mode */
#define CYAPA_SMBUS_BL_STATUS SMBUS_BLOCK_CMD(CMD_BL_STATUS)
#define CYAPA_SMBUS_BL_HEAD SMBUS_BLOCK_CMD(CMD_BL_HEAD)
#define CYAPA_SMBUS_BL_CMD SMBUS_BLOCK_CMD(CMD_BL_CMD)
#define CYAPA_SMBUS_BL_DATA SMBUS_BLOCK_CMD(CMD_BL_DATA)
#define CYAPA_SMBUS_BL_ALL SMBUS_BLOCK_CMD(CMD_BL_ALL)

/* register block read/write command in operational mode */
#define CYAPA_SMBUS_BLK_PRODUCT_ID SMBUS_BLOCK_CMD(CMD_BLK_PRODUCT_ID)
#define CYAPA_SMBUS_BLK_HEAD SMBUS_BLOCK_CMD(CMD_BLK_HEAD)

static const struct cyapa_cmd_len cyapa_i2c_cmds[] = {
	{ CYAPA_OFFSET_SOFT_RESET, 1 },
	{ REG_OFFSET_COMMAND_BASE + 1, 1 },
	{ REG_OFFSET_DATA_BASE, 1 },
	{ REG_OFFSET_DATA_BASE, sizeof(struct cyapa_reg_data) },
	{ REG_OFFSET_COMMAND_BASE, 0 },
	{ REG_OFFSET_QUERY_BASE, QUERY_DATA_SIZE },
	{ BL_HEAD_OFFSET, 3 },
	{ BL_HEAD_OFFSET, 16 },
	{ BL_HEAD_OFFSET, 16 },
	{ BL_DATA_OFFSET, 16 },
	{ BL_HEAD_OFFSET, 32 },
	{ REG_OFFSET_QUERY_BASE, PRODUCT_ID_SIZE },
	{ REG_OFFSET_DATA_BASE, 32 }
};

static const struct cyapa_cmd_len cyapa_smbus_cmds[] = {
	{ CYAPA_SMBUS_RESET, 1 },
	{ CYAPA_SMBUS_POWER_MODE, 1 },
	{ CYAPA_SMBUS_DEV_STATUS, 1 },
	{ CYAPA_SMBUS_GROUP_DATA, sizeof(struct cyapa_reg_data) },
	{ CYAPA_SMBUS_GROUP_CMD, 2 },
	{ CYAPA_SMBUS_GROUP_QUERY, QUERY_DATA_SIZE },
	{ CYAPA_SMBUS_BL_STATUS, 3 },
	{ CYAPA_SMBUS_BL_HEAD, 16 },
	{ CYAPA_SMBUS_BL_CMD, 16 },
	{ CYAPA_SMBUS_BL_DATA, 16 },
	{ CYAPA_SMBUS_BL_ALL, 32 },
	{ CYAPA_SMBUS_BLK_PRODUCT_ID, PRODUCT_ID_SIZE },
	{ CYAPA_SMBUS_BLK_HEAD, 16 },
};

static ssize_t cyapa_i2c_reg_read_block(struct cyapa *cyapa, u8 reg, size_t len,
					u8 *values)
{
	return i2c_smbus_read_i2c_block_data(cyapa->client, reg, len, values);
}

static ssize_t cyapa_i2c_reg_write_block(struct cyapa *cyapa, u8 reg,
					 size_t len, const u8 *values)
{
	return i2c_smbus_write_i2c_block_data(cyapa->client, reg, len, values);
}

/*
 * cyapa_smbus_read_block - perform smbus block read command
 * @cyapa  - private data structure of the driver
 * @cmd    - the properly encoded smbus command
 * @len    - expected length of smbus command result
 * @values - buffer to store smbus command result
 *
 * Returns negative errno, else the number of bytes written.
 *
 * Note:
 * In trackpad device, the memory block allocated for I2C register map
 * is 256 bytes, so the max read block for I2C bus is 256 bytes.
 */
static ssize_t cyapa_smbus_read_block(struct cyapa *cyapa, u8 cmd, size_t len,
				      u8 *values)
{
	ssize_t ret;
	u8 index;
	u8 smbus_cmd;
	u8 *buf;
	struct i2c_client *client = cyapa->client;

	if (!(SMBUS_BYTE_BLOCK_CMD_MASK & cmd))
		return -EINVAL;

	if (SMBUS_GROUP_BLOCK_CMD_MASK & cmd) {
		/* read specific block registers command. */
		smbus_cmd = SMBUS_ENCODE_RW(cmd, SMBUS_READ);
		ret = i2c_smbus_read_block_data(client, smbus_cmd, values);
		goto out;
	}

	ret = 0;
	for (index = 0; index * I2C_SMBUS_BLOCK_MAX < len; index++) {
		smbus_cmd = SMBUS_ENCODE_IDX(cmd, index);
		smbus_cmd = SMBUS_ENCODE_RW(smbus_cmd, SMBUS_READ);
		buf = values + I2C_SMBUS_BLOCK_MAX * index;
		ret = i2c_smbus_read_block_data(client, smbus_cmd, buf);
		if (ret < 0)
			goto out;
	}

out:
	return ret > 0 ? len : ret;
}

static s32 cyapa_read_byte(struct cyapa *cyapa, u8 cmd_idx)
{
	u8 cmd;

	if (cyapa->smbus) {
		cmd = cyapa_smbus_cmds[cmd_idx].cmd;
		cmd = SMBUS_ENCODE_RW(cmd, SMBUS_READ);
	} else {
		cmd = cyapa_i2c_cmds[cmd_idx].cmd;
	}
	return i2c_smbus_read_byte_data(cyapa->client, cmd);
}

static s32 cyapa_write_byte(struct cyapa *cyapa, u8 cmd_idx, u8 value)
{
	u8 cmd;

	if (cyapa->smbus) {
		cmd = cyapa_smbus_cmds[cmd_idx].cmd;
		cmd = SMBUS_ENCODE_RW(cmd, SMBUS_WRITE);
	} else {
		cmd = cyapa_i2c_cmds[cmd_idx].cmd;
	}
	return i2c_smbus_write_byte_data(cyapa->client, cmd, value);
}

static ssize_t cyapa_read_block(struct cyapa *cyapa, u8 cmd_idx, u8 *values)
{
	u8 cmd;
	size_t len;

	if (cyapa->smbus) {
		cmd = cyapa_smbus_cmds[cmd_idx].cmd;
		len = cyapa_smbus_cmds[cmd_idx].len;
		return cyapa_smbus_read_block(cyapa, cmd, len, values);
	} else {
		cmd = cyapa_i2c_cmds[cmd_idx].cmd;
		len = cyapa_i2c_cmds[cmd_idx].len;
		return cyapa_i2c_reg_read_block(cyapa, cmd, len, values);
	}
}

/*
 * Query device for its current operating state.
 *
 */
static int cyapa_get_state(struct cyapa *cyapa)
{
	int ret;
	u8 status[BL_STATUS_SIZE];

	cyapa->state = CYAPA_STATE_NO_DEVICE;

	/*
	 * Get trackpad status by reading 3 registers starting from 0.
	 * If the device is in the bootloader, this will be BL_HEAD.
	 * If the device is in operation mode, this will be the DATA regs.
	 *
	 */
	ret = cyapa_i2c_reg_read_block(cyapa, BL_HEAD_OFFSET, BL_STATUS_SIZE,
				       status);

	/*
	 * On smbus systems in OP mode, the i2c_reg_read will fail with
	 * -ETIMEDOUT.  In this case, try again using the smbus equivalent
	 * command.  This should return a BL_HEAD indicating CYAPA_STATE_OP.
	 */
	if (cyapa->smbus && (ret == -ETIMEDOUT || ret == -ENXIO))
		ret = cyapa_read_block(cyapa, CYAPA_CMD_BL_STATUS, status);

	if (ret != BL_STATUS_SIZE)
		goto error;

	if ((status[REG_OP_STATUS] & OP_STATUS_SRC) == OP_STATUS_SRC) {
		switch (status[REG_OP_STATUS] & OP_STATUS_DEV) {
		case CYAPA_DEV_NORMAL:
		case CYAPA_DEV_BUSY:
			cyapa->state = CYAPA_STATE_OP;
			break;
		default:
			ret = -EAGAIN;
			goto error;
		}
	} else {
		if (status[REG_BL_STATUS] & BL_STATUS_BUSY)
			cyapa->state = CYAPA_STATE_BL_BUSY;
		else if (status[REG_BL_ERROR] & BL_ERROR_BOOTLOADING)
			cyapa->state = CYAPA_STATE_BL_ACTIVE;
		else
			cyapa->state = CYAPA_STATE_BL_IDLE;
	}

	return 0;
error:
	return (ret < 0) ? ret : -EAGAIN;
}

/*
 * Poll device for its status in a loop, waiting up to timeout for a response.
 *
 * When the device switches state, it usually takes ~300 ms.
 * However, when running a new firmware image, the device must calibrate its
 * sensors, which can take as long as 2 seconds.
 *
 * Note: The timeout has granularity of the polling rate, which is 100 ms.
 *
 * Returns:
 *   0 when the device eventually responds with a valid non-busy state.
 *   -ETIMEDOUT if device never responds (too many -EAGAIN)
 *   < 0    other errors
 */
static int cyapa_poll_state(struct cyapa *cyapa, unsigned int timeout)
{
	int ret;
	int tries = timeout / 100;

	ret = cyapa_get_state(cyapa);
	while ((ret || cyapa->state >= CYAPA_STATE_BL_BUSY) && tries--) {
		msleep(100);
		ret = cyapa_get_state(cyapa);
	}
	return (ret == -EAGAIN || ret == -ETIMEDOUT) ? -ETIMEDOUT : ret;
}

static int cyapa_bl_deactivate(struct cyapa *cyapa)
{
	int ret;

	ret = cyapa_i2c_reg_write_block(cyapa, 0, sizeof(bl_deactivate),
					bl_deactivate);
	if (ret < 0)
		return ret;

	/* wait for bootloader to switch to idle state; should take < 100ms */
	msleep(100);
	ret = cyapa_poll_state(cyapa, 500);
	if (ret < 0)
		return ret;
	if (cyapa->state != CYAPA_STATE_BL_IDLE)
		return -EAGAIN;
	return 0;
}

/*
 * Exit bootloader
 *
 * Send bl_exit command, then wait 50 - 100 ms to let device transition to
 * operational mode.  If this is the first time the device's firmware is
 * running, it can take up to 2 seconds to calibrate its sensors.  So, poll
 * the device's new state for up to 2 seconds.
 *
 * Returns:
 *   -EIO    failure while reading from device
 *   -EAGAIN device is stuck in bootloader, b/c it has invalid firmware
 *   0       device is supported and in operational mode
 */
static int cyapa_bl_exit(struct cyapa *cyapa)
{
	int ret;

	ret = cyapa_i2c_reg_write_block(cyapa, 0, sizeof(bl_exit), bl_exit);
	if (ret < 0)
		return ret;

	/*
	 * Wait for bootloader to exit, and operation mode to start.
	 * Normally, this takes at least 50 ms.
	 */
	usleep_range(50000, 100000);
	/*
	 * In addition, when a device boots for the first time after being
	 * updated to new firmware, it must first calibrate its sensors, which
	 * can take up to an additional 2 seconds.
	 */
	ret = cyapa_poll_state(cyapa, 2000);
	if (ret < 0)
		return ret;
	if (cyapa->state != CYAPA_STATE_OP)
		return -EAGAIN;

	return 0;
}

/*
 * Set device power mode
 *
 */
static int cyapa_set_power_mode(struct cyapa *cyapa, u8 power_mode)
{
	struct device *dev = &cyapa->client->dev;
	int ret;
	u8 power;

	if (cyapa->state != CYAPA_STATE_OP)
		return 0;

	ret = cyapa_read_byte(cyapa, CYAPA_CMD_POWER_MODE);
	if (ret < 0)
		return ret;

	power = ret & ~PWR_MODE_MASK;
	power |= power_mode & PWR_MODE_MASK;
	ret = cyapa_write_byte(cyapa, CYAPA_CMD_POWER_MODE, power);
	if (ret < 0)
		dev_err(dev, "failed to set power_mode 0x%02x err = %d\n",
			power_mode, ret);
	return ret;
}

static int cyapa_get_query_data(struct cyapa *cyapa)
{
	u8 query_data[QUERY_DATA_SIZE];
	int ret;

	if (cyapa->state != CYAPA_STATE_OP)
		return -EBUSY;

	ret = cyapa_read_block(cyapa, CYAPA_CMD_GROUP_QUERY, query_data);
	if (ret < 0)
		return ret;
	if (ret != QUERY_DATA_SIZE)
		return -EIO;

	memcpy(&cyapa->product_id[0], &query_data[0], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &query_data[5], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &query_data[11], 2);
	cyapa->product_id[15] = '\0';

	cyapa->btn_capability = query_data[19] & CAPABILITY_BTN_MASK;

	cyapa->gen = query_data[20] & 0x0f;

	cyapa->max_abs_x = ((query_data[21] & 0xf0) << 4) | query_data[22];
	cyapa->max_abs_y = ((query_data[21] & 0x0f) << 8) | query_data[23];

	cyapa->physical_size_x =
		((query_data[24] & 0xf0) << 4) | query_data[25];
	cyapa->physical_size_y =
		((query_data[24] & 0x0f) << 8) | query_data[26];

	return 0;
}

/*
 * Check if device is operational.
 *
 * An operational device is responding, has exited bootloader, and has
 * firmware supported by this driver.
 *
 * Returns:
 *   -EBUSY  no device or in bootloader
 *   -EIO    failure while reading from device
 *   -EAGAIN device is still in bootloader
 *           if ->state = CYAPA_STATE_BL_IDLE, device has invalid firmware
 *   -EINVAL device is in operational mode, but not supported by this driver
 *   0       device is supported
 */
static int cyapa_check_is_operational(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	static const char unique_str[] = "CYTRA";
	int ret;

	ret = cyapa_poll_state(cyapa, 2000);
	if (ret < 0)
		return ret;
	switch (cyapa->state) {
	case CYAPA_STATE_BL_ACTIVE:
		ret = cyapa_bl_deactivate(cyapa);
		if (ret)
			return ret;

	/* Fallthrough state */
	case CYAPA_STATE_BL_IDLE:
		ret = cyapa_bl_exit(cyapa);
		if (ret)
			return ret;

	/* Fallthrough state */
	case CYAPA_STATE_OP:
		ret = cyapa_get_query_data(cyapa);
		if (ret < 0)
			return ret;

		/* only support firmware protocol gen3 */
		if (cyapa->gen != CYAPA_GEN3) {
			dev_err(dev, "unsupported protocol version (%d)",
				cyapa->gen);
			return -EINVAL;
		}

		/* only support product ID starting with CYTRA */
		if (memcmp(cyapa->product_id, unique_str,
			   sizeof(unique_str) - 1) != 0) {
			dev_err(dev, "unsupported product ID (%s)\n",
				cyapa->product_id);
			return -EINVAL;
		}
		return 0;

	default:
		return -EIO;
	}
	return 0;
}

static irqreturn_t cyapa_irq(int irq, void *dev_id)
{
	struct cyapa *cyapa = dev_id;
	struct device *dev = &cyapa->client->dev;
	struct input_dev *input = cyapa->input;
	struct cyapa_reg_data data;
	int i;
	int ret;
	int num_fingers;

	if (device_may_wakeup(dev))
		pm_wakeup_event(dev, 0);

	ret = cyapa_read_block(cyapa, CYAPA_CMD_GROUP_DATA, (u8 *)&data);
	if (ret != sizeof(data))
		goto out;

	if ((data.device_status & OP_STATUS_SRC) != OP_STATUS_SRC ||
	    (data.device_status & OP_STATUS_DEV) != CYAPA_DEV_NORMAL ||
	    (data.finger_btn & OP_DATA_VALID) != OP_DATA_VALID) {
		goto out;
	}

	num_fingers = (data.finger_btn >> 4) & 0x0f;
	for (i = 0; i < num_fingers; i++) {
		const struct cyapa_touch *touch = &data.touches[i];
		/* Note: touch->id range is 1 to 15; slots are 0 to 14. */
		int slot = touch->id - 1;

		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X,
				 ((touch->xy_hi & 0xf0) << 4) | touch->x_lo);
		input_report_abs(input, ABS_MT_POSITION_Y,
				 ((touch->xy_hi & 0x0f) << 8) | touch->y_lo);
		input_report_abs(input, ABS_MT_PRESSURE, touch->pressure);
	}

	input_mt_sync_frame(input);

	if (cyapa->btn_capability & CAPABILITY_LEFT_BTN_MASK)
		input_report_key(input, BTN_LEFT,
				 data.finger_btn & OP_DATA_LEFT_BTN);

	if (cyapa->btn_capability & CAPABILITY_MIDDLE_BTN_MASK)
		input_report_key(input, BTN_MIDDLE,
				 data.finger_btn & OP_DATA_MIDDLE_BTN);

	if (cyapa->btn_capability & CAPABILITY_RIGHT_BTN_MASK)
		input_report_key(input, BTN_RIGHT,
				 data.finger_btn & OP_DATA_RIGHT_BTN);

	input_sync(input);

out:
	return IRQ_HANDLED;
}

static u8 cyapa_check_adapter_functionality(struct i2c_client *client)
{
	u8 ret = CYAPA_ADAPTER_FUNC_NONE;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		ret |= CYAPA_ADAPTER_FUNC_I2C;
	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_BLOCK_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		ret |= CYAPA_ADAPTER_FUNC_SMBUS;
	return ret;
}

static int cyapa_create_input_dev(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	int ret;
	struct input_dev *input;

	if (!cyapa->physical_size_x || !cyapa->physical_size_y)
		return -EINVAL;

	input = cyapa->input = input_allocate_device();
	if (!input) {
		dev_err(dev, "allocate memory for input device failed\n");
		return -ENOMEM;
	}

	input->name = CYAPA_NAME;
	input->phys = cyapa->phys;
	input->id.bustype = BUS_I2C;
	input->id.version = 1;
	input->id.product = 0;  /* means any product in eventcomm. */
	input->dev.parent = &cyapa->client->dev;

	input_set_drvdata(input, cyapa);

	__set_bit(EV_ABS, input->evbit);

	/* finger position */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, cyapa->max_abs_x, 0,
			     0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, cyapa->max_abs_y, 0,
			     0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	input_abs_set_res(input, ABS_MT_POSITION_X,
			  cyapa->max_abs_x / cyapa->physical_size_x);
	input_abs_set_res(input, ABS_MT_POSITION_Y,
			  cyapa->max_abs_y / cyapa->physical_size_y);

	if (cyapa->btn_capability & CAPABILITY_LEFT_BTN_MASK)
		__set_bit(BTN_LEFT, input->keybit);
	if (cyapa->btn_capability & CAPABILITY_MIDDLE_BTN_MASK)
		__set_bit(BTN_MIDDLE, input->keybit);
	if (cyapa->btn_capability & CAPABILITY_RIGHT_BTN_MASK)
		__set_bit(BTN_RIGHT, input->keybit);

	if (cyapa->btn_capability == CAPABILITY_LEFT_BTN_MASK)
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

	/* handle pointer emulation and unused slots in core */
	ret = input_mt_init_slots(input, CYAPA_MAX_MT_SLOTS,
				  INPUT_MT_POINTER | INPUT_MT_DROP_UNUSED);
	if (ret) {
		dev_err(dev, "allocate memory for MT slots failed, %d\n", ret);
		goto err_free_device;
	}

	/* Register the device in input subsystem */
	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "input device register failed, %d\n", ret);
		goto err_free_device;
	}
	return 0;

err_free_device:
	input_free_device(input);
	cyapa->input = NULL;
	return ret;
}

static int cyapa_probe(struct i2c_client *client,
		       const struct i2c_device_id *dev_id)
{
	int ret;
	u8 adapter_func;
	struct cyapa *cyapa;
	struct device *dev = &client->dev;

	adapter_func = cyapa_check_adapter_functionality(client);
	if (adapter_func == CYAPA_ADAPTER_FUNC_NONE) {
		dev_err(dev, "not a supported I2C/SMBus adapter\n");
		return -EIO;
	}

	cyapa = kzalloc(sizeof(struct cyapa), GFP_KERNEL);
	if (!cyapa) {
		dev_err(dev, "allocate memory for cyapa failed\n");
		return -ENOMEM;
	}

	cyapa->gen = CYAPA_GEN3;
	cyapa->client = client;
	i2c_set_clientdata(client, cyapa);
	sprintf(cyapa->phys, "i2c-%d-%04x/input0", client->adapter->nr,
		client->addr);

	/* i2c isn't supported, use smbus */
	if (adapter_func == CYAPA_ADAPTER_FUNC_SMBUS)
		cyapa->smbus = true;
	cyapa->state = CYAPA_STATE_NO_DEVICE;
	ret = cyapa_check_is_operational(cyapa);
	if (ret) {
		dev_err(dev, "device not operational, %d\n", ret);
		goto err_mem_free;
	}

	ret = cyapa_create_input_dev(cyapa);
	if (ret) {
		dev_err(dev, "create input_dev instance failed, %d\n", ret);
		goto err_mem_free;
	}

	ret = cyapa_set_power_mode(cyapa, PWR_MODE_FULL_ACTIVE);
	if (ret) {
		dev_err(dev, "set active power failed, %d\n", ret);
		goto err_unregister_device;
	}

	cyapa->irq = client->irq;
	ret = request_threaded_irq(cyapa->irq,
				   NULL,
				   cyapa_irq,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "cyapa",
				   cyapa);
	if (ret) {
		dev_err(dev, "IRQ request failed: %d\n, ", ret);
		goto err_unregister_device;
	}

	return 0;

err_unregister_device:
	input_unregister_device(cyapa->input);
err_mem_free:
	kfree(cyapa);

	return ret;
}

static int cyapa_remove(struct i2c_client *client)
{
	struct cyapa *cyapa = i2c_get_clientdata(client);

	free_irq(cyapa->irq, cyapa);
	input_unregister_device(cyapa->input);
	cyapa_set_power_mode(cyapa, PWR_MODE_OFF);
	kfree(cyapa);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cyapa_suspend(struct device *dev)
{
	int ret;
	u8 power_mode;
	struct cyapa *cyapa = dev_get_drvdata(dev);

	disable_irq(cyapa->irq);

	/*
	 * Set trackpad device to idle mode if wakeup is allowed,
	 * otherwise turn off.
	 */
	power_mode = device_may_wakeup(dev) ? PWR_MODE_IDLE
					    : PWR_MODE_OFF;
	ret = cyapa_set_power_mode(cyapa, power_mode);
	if (ret < 0)
		dev_err(dev, "set power mode failed, %d\n", ret);

	if (device_may_wakeup(dev))
		cyapa->irq_wake = (enable_irq_wake(cyapa->irq) == 0);
	return 0;
}

static int cyapa_resume(struct device *dev)
{
	int ret;
	struct cyapa *cyapa = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && cyapa->irq_wake)
		disable_irq_wake(cyapa->irq);

	ret = cyapa_set_power_mode(cyapa, PWR_MODE_FULL_ACTIVE);
	if (ret)
		dev_warn(dev, "resume active power failed, %d\n", ret);

	enable_irq(cyapa->irq);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(cyapa_pm_ops, cyapa_suspend, cyapa_resume);

static const struct i2c_device_id cyapa_id_table[] = {
	{ "cyapa", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cyapa_id_table);

static struct i2c_driver cyapa_driver = {
	.driver = {
		.name = "cyapa",
		.owner = THIS_MODULE,
		.pm = &cyapa_pm_ops,
	},

	.probe = cyapa_probe,
	.remove = cyapa_remove,
	.id_table = cyapa_id_table,
};

module_i2c_driver(cyapa_driver);

MODULE_DESCRIPTION("Cypress APA I2C Trackpad Driver");
MODULE_AUTHOR("Dudley Du <dudl@cypress.com>");
MODULE_LICENSE("GPL");
