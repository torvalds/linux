/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 * Further cleanup and restructuring by:
 *   Daniel Kurtz <djkurtz@chromium.org>
 *   Benson Leung <bleung@chromium.org>
 *
 * Copyright (C) 2011-2014 Cypress Semiconductor, Inc.
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/unaligned/access_ok.h>
#include "cyapa.h"


#define GEN3_MAX_FINGERS 5
#define GEN3_FINGER_NUM(x) (((x) >> 4) & 0x07)

#define BLK_HEAD_BYTES 32

/* Macro for register map group offset. */
#define PRODUCT_ID_SIZE  16
#define QUERY_DATA_SIZE  27
#define REG_PROTOCOL_GEN_QUERY_OFFSET  20

#define REG_OFFSET_DATA_BASE     0x0000
#define REG_OFFSET_COMMAND_BASE  0x0028
#define REG_OFFSET_QUERY_BASE    0x002a

#define CYAPA_OFFSET_SOFT_RESET  REG_OFFSET_COMMAND_BASE
#define OP_RECALIBRATION_MASK    0x80
#define OP_REPORT_BASELINE_MASK  0x40
#define REG_OFFSET_MAX_BASELINE  0x0026
#define REG_OFFSET_MIN_BASELINE  0x0027

#define REG_OFFSET_POWER_MODE (REG_OFFSET_COMMAND_BASE + 1)
#define SET_POWER_MODE_DELAY   10000  /* Unit: us */
#define SET_POWER_MODE_TRIES   5

#define GEN3_BL_CMD_CHECKSUM_SEED 0xff
#define GEN3_BL_CMD_INITIATE_BL   0x38
#define GEN3_BL_CMD_WRITE_BLOCK   0x39
#define GEN3_BL_CMD_VERIFY_BLOCK  0x3a
#define GEN3_BL_CMD_TERMINATE_BL  0x3b
#define GEN3_BL_CMD_LAUNCH_APP    0xa5

/*
 * CYAPA trackpad device states.
 * Used in register 0x00, bit1-0, DeviceStatus field.
 * Other values indicate device is in an abnormal state and must be reset.
 */
#define CYAPA_DEV_NORMAL  0x03
#define CYAPA_DEV_BUSY    0x01

#define CYAPA_FW_BLOCK_SIZE	64
#define CYAPA_FW_READ_SIZE	16
#define CYAPA_FW_HDR_START	0x0780
#define CYAPA_FW_HDR_BLOCK_COUNT  2
#define CYAPA_FW_HDR_BLOCK_START  (CYAPA_FW_HDR_START / CYAPA_FW_BLOCK_SIZE)
#define CYAPA_FW_HDR_SIZE	  (CYAPA_FW_HDR_BLOCK_COUNT * \
					CYAPA_FW_BLOCK_SIZE)
#define CYAPA_FW_DATA_START	0x0800
#define CYAPA_FW_DATA_BLOCK_COUNT  480
#define CYAPA_FW_DATA_BLOCK_START  (CYAPA_FW_DATA_START / CYAPA_FW_BLOCK_SIZE)
#define CYAPA_FW_DATA_SIZE	(CYAPA_FW_DATA_BLOCK_COUNT * \
				 CYAPA_FW_BLOCK_SIZE)
#define CYAPA_FW_SIZE		(CYAPA_FW_HDR_SIZE + CYAPA_FW_DATA_SIZE)
#define CYAPA_CMD_LEN		16

#define GEN3_BL_IDLE_FW_MAJ_VER_OFFSET 0x0b
#define GEN3_BL_IDLE_FW_MIN_VER_OFFSET (GEN3_BL_IDLE_FW_MAJ_VER_OFFSET + 1)


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

struct gen3_write_block_cmd {
	u8 checksum_seed;  /* Always be 0xff */
	u8 cmd_code;       /* command code: 0x39 */
	u8 key[8];         /* 8-byte security key */
	__be16 block_num;
	u8 block_data[CYAPA_FW_BLOCK_SIZE];
	u8 block_checksum;  /* Calculated using bytes 12 - 75 */
	u8 cmd_checksum;    /* Calculated using bytes 0-76 */
} __packed;

static const u8 security_key[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
static const u8 bl_activate[] = { 0x00, 0xff, 0x38, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07 };
static const u8 bl_deactivate[] = { 0x00, 0xff, 0x3b, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07 };
static const u8 bl_exit[] = { 0x00, 0xff, 0xa5, 0x00, 0x01, 0x02, 0x03, 0x04,
		0x05, 0x06, 0x07 };


 /* for byte read/write command */
#define CMD_RESET      0
#define CMD_POWER_MODE 1
#define CMD_DEV_STATUS 2
#define CMD_REPORT_MAX_BASELINE 3
#define CMD_REPORT_MIN_BASELINE 4
#define SMBUS_BYTE_CMD(cmd) (((cmd) & 0x3f) << 1)
#define CYAPA_SMBUS_RESET         SMBUS_BYTE_CMD(CMD_RESET)
#define CYAPA_SMBUS_POWER_MODE    SMBUS_BYTE_CMD(CMD_POWER_MODE)
#define CYAPA_SMBUS_DEV_STATUS    SMBUS_BYTE_CMD(CMD_DEV_STATUS)
#define CYAPA_SMBUS_MAX_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MAX_BASELINE)
#define CYAPA_SMBUS_MIN_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MIN_BASELINE)

 /* for group registers read/write command */
#define REG_GROUP_DATA 0
#define REG_GROUP_CMD 2
#define REG_GROUP_QUERY 3
#define SMBUS_GROUP_CMD(grp) (0x80 | (((grp) & 0x07) << 3))
#define CYAPA_SMBUS_GROUP_DATA	 SMBUS_GROUP_CMD(REG_GROUP_DATA)
#define CYAPA_SMBUS_GROUP_CMD	 SMBUS_GROUP_CMD(REG_GROUP_CMD)
#define CYAPA_SMBUS_GROUP_QUERY	 SMBUS_GROUP_CMD(REG_GROUP_QUERY)

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
#define CYAPA_SMBUS_BL_STATUS  SMBUS_BLOCK_CMD(CMD_BL_STATUS)
#define CYAPA_SMBUS_BL_HEAD    SMBUS_BLOCK_CMD(CMD_BL_HEAD)
#define CYAPA_SMBUS_BL_CMD     SMBUS_BLOCK_CMD(CMD_BL_CMD)
#define CYAPA_SMBUS_BL_DATA    SMBUS_BLOCK_CMD(CMD_BL_DATA)
#define CYAPA_SMBUS_BL_ALL     SMBUS_BLOCK_CMD(CMD_BL_ALL)

/* register block read/write command in operational mode */
#define CYAPA_SMBUS_BLK_PRODUCT_ID SMBUS_BLOCK_CMD(CMD_BLK_PRODUCT_ID)
#define CYAPA_SMBUS_BLK_HEAD SMBUS_BLOCK_CMD(CMD_BLK_HEAD)

 /* for byte read/write command */
#define CMD_RESET 0
#define CMD_POWER_MODE 1
#define CMD_DEV_STATUS 2
#define CMD_REPORT_MAX_BASELINE 3
#define CMD_REPORT_MIN_BASELINE 4
#define SMBUS_BYTE_CMD(cmd) (((cmd) & 0x3f) << 1)
#define CYAPA_SMBUS_RESET         SMBUS_BYTE_CMD(CMD_RESET)
#define CYAPA_SMBUS_POWER_MODE    SMBUS_BYTE_CMD(CMD_POWER_MODE)
#define CYAPA_SMBUS_DEV_STATUS    SMBUS_BYTE_CMD(CMD_DEV_STATUS)
#define CYAPA_SMBUS_MAX_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MAX_BASELINE)
#define CYAPA_SMBUS_MIN_BASELINE  SMBUS_BYTE_CMD(CMD_REPORT_MIN_BASELINE)

 /* for group registers read/write command */
#define REG_GROUP_DATA  0
#define REG_GROUP_CMD   2
#define REG_GROUP_QUERY 3
#define SMBUS_GROUP_CMD(grp) (0x80 | (((grp) & 0x07) << 3))
#define CYAPA_SMBUS_GROUP_DATA  SMBUS_GROUP_CMD(REG_GROUP_DATA)
#define CYAPA_SMBUS_GROUP_CMD   SMBUS_GROUP_CMD(REG_GROUP_CMD)
#define CYAPA_SMBUS_GROUP_QUERY SMBUS_GROUP_CMD(REG_GROUP_QUERY)

 /* for register block read/write command */
#define CMD_BL_STATUS		0
#define CMD_BL_HEAD		1
#define CMD_BL_CMD		2
#define CMD_BL_DATA		3
#define CMD_BL_ALL		4
#define CMD_BLK_PRODUCT_ID	5
#define CMD_BLK_HEAD		6
#define SMBUS_BLOCK_CMD(cmd) (0xc0 | (((cmd) & 0x1f) << 1))

/* register block read/write command in bootloader mode */
#define CYAPA_SMBUS_BL_STATUS SMBUS_BLOCK_CMD(CMD_BL_STATUS)
#define CYAPA_SMBUS_BL_HEAD   SMBUS_BLOCK_CMD(CMD_BL_HEAD)
#define CYAPA_SMBUS_BL_CMD    SMBUS_BLOCK_CMD(CMD_BL_CMD)
#define CYAPA_SMBUS_BL_DATA   SMBUS_BLOCK_CMD(CMD_BL_DATA)
#define CYAPA_SMBUS_BL_ALL    SMBUS_BLOCK_CMD(CMD_BL_ALL)

/* register block read/write command in operational mode */
#define CYAPA_SMBUS_BLK_PRODUCT_ID SMBUS_BLOCK_CMD(CMD_BLK_PRODUCT_ID)
#define CYAPA_SMBUS_BLK_HEAD       SMBUS_BLOCK_CMD(CMD_BLK_HEAD)

struct cyapa_cmd_len {
	u8 cmd;
	u8 len;
};

/* maps generic CYAPA_CMD_* code to the I2C equivalent */
static const struct cyapa_cmd_len cyapa_i2c_cmds[] = {
	{ CYAPA_OFFSET_SOFT_RESET, 1 },		/* CYAPA_CMD_SOFT_RESET */
	{ REG_OFFSET_COMMAND_BASE + 1, 1 },	/* CYAPA_CMD_POWER_MODE */
	{ REG_OFFSET_DATA_BASE, 1 },		/* CYAPA_CMD_DEV_STATUS */
	{ REG_OFFSET_DATA_BASE, sizeof(struct cyapa_reg_data) },
						/* CYAPA_CMD_GROUP_DATA */
	{ REG_OFFSET_COMMAND_BASE, 0 },		/* CYAPA_CMD_GROUP_CMD */
	{ REG_OFFSET_QUERY_BASE, QUERY_DATA_SIZE }, /* CYAPA_CMD_GROUP_QUERY */
	{ BL_HEAD_OFFSET, 3 },			/* CYAPA_CMD_BL_STATUS */
	{ BL_HEAD_OFFSET, 16 },			/* CYAPA_CMD_BL_HEAD */
	{ BL_HEAD_OFFSET, 16 },			/* CYAPA_CMD_BL_CMD */
	{ BL_DATA_OFFSET, 16 },			/* CYAPA_CMD_BL_DATA */
	{ BL_HEAD_OFFSET, 32 },			/* CYAPA_CMD_BL_ALL */
	{ REG_OFFSET_QUERY_BASE, PRODUCT_ID_SIZE },
						/* CYAPA_CMD_BLK_PRODUCT_ID */
	{ REG_OFFSET_DATA_BASE, 32 },		/* CYAPA_CMD_BLK_HEAD */
	{ REG_OFFSET_MAX_BASELINE, 1 },		/* CYAPA_CMD_MAX_BASELINE */
	{ REG_OFFSET_MIN_BASELINE, 1 },		/* CYAPA_CMD_MIN_BASELINE */
};

static const struct cyapa_cmd_len cyapa_smbus_cmds[] = {
	{ CYAPA_SMBUS_RESET, 1 },		/* CYAPA_CMD_SOFT_RESET */
	{ CYAPA_SMBUS_POWER_MODE, 1 },		/* CYAPA_CMD_POWER_MODE */
	{ CYAPA_SMBUS_DEV_STATUS, 1 },		/* CYAPA_CMD_DEV_STATUS */
	{ CYAPA_SMBUS_GROUP_DATA, sizeof(struct cyapa_reg_data) },
						/* CYAPA_CMD_GROUP_DATA */
	{ CYAPA_SMBUS_GROUP_CMD, 2 },		/* CYAPA_CMD_GROUP_CMD */
	{ CYAPA_SMBUS_GROUP_QUERY, QUERY_DATA_SIZE },
						/* CYAPA_CMD_GROUP_QUERY */
	{ CYAPA_SMBUS_BL_STATUS, 3 },		/* CYAPA_CMD_BL_STATUS */
	{ CYAPA_SMBUS_BL_HEAD, 16 },		/* CYAPA_CMD_BL_HEAD */
	{ CYAPA_SMBUS_BL_CMD, 16 },		/* CYAPA_CMD_BL_CMD */
	{ CYAPA_SMBUS_BL_DATA, 16 },		/* CYAPA_CMD_BL_DATA */
	{ CYAPA_SMBUS_BL_ALL, 32 },		/* CYAPA_CMD_BL_ALL */
	{ CYAPA_SMBUS_BLK_PRODUCT_ID, PRODUCT_ID_SIZE },
						/* CYAPA_CMD_BLK_PRODUCT_ID */
	{ CYAPA_SMBUS_BLK_HEAD, 16 },		/* CYAPA_CMD_BLK_HEAD */
	{ CYAPA_SMBUS_MAX_BASELINE, 1 },	/* CYAPA_CMD_MAX_BASELINE */
	{ CYAPA_SMBUS_MIN_BASELINE, 1 },	/* CYAPA_CMD_MIN_BASELINE */
};


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
ssize_t cyapa_smbus_read_block(struct cyapa *cyapa, u8 cmd, size_t len,
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

ssize_t cyapa_i2c_reg_read_block(struct cyapa *cyapa, u8 reg, size_t len,
					u8 *values)
{
	return i2c_smbus_read_i2c_block_data(cyapa->client, reg, len, values);
}

static ssize_t cyapa_i2c_reg_write_block(struct cyapa *cyapa, u8 reg,
					 size_t len, const u8 *values)
{
	return i2c_smbus_write_i2c_block_data(cyapa->client, reg, len, values);
}

ssize_t cyapa_read_block(struct cyapa *cyapa, u8 cmd_idx, u8 *values)
{
	u8 cmd;
	size_t len;

	if (cyapa->smbus) {
		cmd = cyapa_smbus_cmds[cmd_idx].cmd;
		len = cyapa_smbus_cmds[cmd_idx].len;
		return cyapa_smbus_read_block(cyapa, cmd, len, values);
	}
	cmd = cyapa_i2c_cmds[cmd_idx].cmd;
	len = cyapa_i2c_cmds[cmd_idx].len;
	return cyapa_i2c_reg_read_block(cyapa, cmd, len, values);
}

/*
 * Determine the Gen3 trackpad device's current operating state.
 *
 */
static int cyapa_gen3_state_parse(struct cyapa *cyapa, u8 *reg_data, int len)
{
	cyapa->state = CYAPA_STATE_NO_DEVICE;

	/* Parse based on Gen3 characteristic registers and bits */
	if (reg_data[REG_BL_FILE] == BL_FILE &&
		reg_data[REG_BL_ERROR] == BL_ERROR_NO_ERR_IDLE &&
		(reg_data[REG_BL_STATUS] ==
			(BL_STATUS_RUNNING | BL_STATUS_CSUM_VALID) ||
			reg_data[REG_BL_STATUS] == BL_STATUS_RUNNING)) {
		/*
		 * Normal state after power on or reset,
		 * REG_BL_STATUS == 0x11, firmware image checksum is valid.
		 * REG_BL_STATUS == 0x10, firmware image checksum is invalid.
		 */
		cyapa->gen = CYAPA_GEN3;
		cyapa->state = CYAPA_STATE_BL_IDLE;
	} else if (reg_data[REG_BL_FILE] == BL_FILE &&
		(reg_data[REG_BL_STATUS] & BL_STATUS_RUNNING) ==
			BL_STATUS_RUNNING) {
		cyapa->gen = CYAPA_GEN3;
		if (reg_data[REG_BL_STATUS] & BL_STATUS_BUSY) {
			cyapa->state = CYAPA_STATE_BL_BUSY;
		} else {
			if ((reg_data[REG_BL_ERROR] & BL_ERROR_BOOTLOADING) ==
					BL_ERROR_BOOTLOADING)
				cyapa->state = CYAPA_STATE_BL_ACTIVE;
			else
				cyapa->state = CYAPA_STATE_BL_IDLE;
		}
	} else if ((reg_data[REG_OP_STATUS] & OP_STATUS_SRC) &&
			(reg_data[REG_OP_DATA1] & OP_DATA_VALID)) {
		/*
		 * Normal state when running in operational mode,
		 * may also not in full power state or
		 * busying in command process.
		 */
		if (GEN3_FINGER_NUM(reg_data[REG_OP_DATA1]) <=
				GEN3_MAX_FINGERS) {
			/* Finger number data is valid. */
			cyapa->gen = CYAPA_GEN3;
			cyapa->state = CYAPA_STATE_OP;
		}
	} else if (reg_data[REG_OP_STATUS] == 0x0C &&
			reg_data[REG_OP_DATA1] == 0x08) {
		/* Op state when first two registers overwritten with 0x00 */
		cyapa->gen = CYAPA_GEN3;
		cyapa->state = CYAPA_STATE_OP;
	} else if (reg_data[REG_BL_STATUS] &
			(BL_STATUS_RUNNING | BL_STATUS_BUSY)) {
		cyapa->gen = CYAPA_GEN3;
		cyapa->state = CYAPA_STATE_BL_BUSY;
	}

	if (cyapa->gen == CYAPA_GEN3 && (cyapa->state == CYAPA_STATE_OP ||
		cyapa->state == CYAPA_STATE_BL_IDLE ||
		cyapa->state == CYAPA_STATE_BL_ACTIVE ||
		cyapa->state == CYAPA_STATE_BL_BUSY))
		return 0;

	return -EAGAIN;
}

/*
 * Enter bootloader by soft resetting the device.
 *
 * If device is already in the bootloader, the function just returns.
 * Otherwise, reset the device; after reset, device enters bootloader idle
 * state immediately.
 *
 * Returns:
 *   0        on success
 *   -EAGAIN  device was reset, but is not now in bootloader idle state
 *   < 0      if the device never responds within the timeout
 */
static int cyapa_gen3_bl_enter(struct cyapa *cyapa)
{
	int error;
	int waiting_time;

	error = cyapa_poll_state(cyapa, 500);
	if (error)
		return error;
	if (cyapa->state == CYAPA_STATE_BL_IDLE) {
		/* Already in BL_IDLE. Skipping reset. */
		return 0;
	}

	if (cyapa->state != CYAPA_STATE_OP)
		return -EAGAIN;

	cyapa->operational = false;
	cyapa->state = CYAPA_STATE_NO_DEVICE;
	error = cyapa_write_byte(cyapa, CYAPA_CMD_SOFT_RESET, 0x01);
	if (error)
		return -EIO;

	usleep_range(25000, 50000);
	waiting_time = 2000;  /* For some shipset, max waiting time is 1~2s. */
	do {
		error = cyapa_poll_state(cyapa, 500);
		if (error) {
			if (error == -ETIMEDOUT) {
				waiting_time -= 500;
				continue;
			}
			return error;
		}

		if ((cyapa->state == CYAPA_STATE_BL_IDLE) &&
			!(cyapa->status[REG_BL_STATUS] & BL_STATUS_WATCHDOG))
			break;

		msleep(100);
		waiting_time -= 100;
	} while (waiting_time > 0);

	if ((cyapa->state != CYAPA_STATE_BL_IDLE) ||
		(cyapa->status[REG_BL_STATUS] & BL_STATUS_WATCHDOG))
		return -EAGAIN;

	return 0;
}

static int cyapa_gen3_bl_activate(struct cyapa *cyapa)
{
	int error;

	error = cyapa_i2c_reg_write_block(cyapa, 0, sizeof(bl_activate),
					bl_activate);
	if (error)
		return error;

	/* Wait for bootloader to activate; takes between 2 and 12 seconds */
	msleep(2000);
	error = cyapa_poll_state(cyapa, 11000);
	if (error)
		return error;
	if (cyapa->state != CYAPA_STATE_BL_ACTIVE)
		return -EAGAIN;

	return 0;
}

static int cyapa_gen3_bl_deactivate(struct cyapa *cyapa)
{
	int error;

	error = cyapa_i2c_reg_write_block(cyapa, 0, sizeof(bl_deactivate),
					bl_deactivate);
	if (error)
		return error;

	/* Wait for bootloader to switch to idle state; should take < 100ms */
	msleep(100);
	error = cyapa_poll_state(cyapa, 500);
	if (error)
		return error;
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
static int cyapa_gen3_bl_exit(struct cyapa *cyapa)
{
	int error;

	error = cyapa_i2c_reg_write_block(cyapa, 0, sizeof(bl_exit), bl_exit);
	if (error)
		return error;

	/*
	 * Wait for bootloader to exit, and operation mode to start.
	 * Normally, this takes at least 50 ms.
	 */
	usleep_range(50000, 100000);
	/*
	 * In addition, when a device boots for the first time after being
	 * updated to new firmware, it must first calibrate its sensors, which
	 * can take up to an additional 2 seconds. If the device power is
	 * running low, this may take even longer.
	 */
	error = cyapa_poll_state(cyapa, 4000);
	if (error < 0)
		return error;
	if (cyapa->state != CYAPA_STATE_OP)
		return -EAGAIN;

	return 0;
}

static u16 cyapa_gen3_csum(const u8 *buf, size_t count)
{
	int i;
	u16 csum = 0;

	for (i = 0; i < count; i++)
		csum += buf[i];

	return csum;
}

/*
 * Verify the integrity of a CYAPA firmware image file.
 *
 * The firmware image file is 30848 bytes, composed of 482 64-byte blocks.
 *
 * The first 2 blocks are the firmware header.
 * The next 480 blocks are the firmware image.
 *
 * The first two bytes of the header hold the header checksum, computed by
 * summing the other 126 bytes of the header.
 * The last two bytes of the header hold the firmware image checksum, computed
 * by summing the 30720 bytes of the image modulo 0xffff.
 *
 * Both checksums are stored little-endian.
 */
static int cyapa_gen3_check_fw(struct cyapa *cyapa, const struct firmware *fw)
{
	struct device *dev = &cyapa->client->dev;
	u16 csum;
	u16 csum_expected;

	/* Firmware must match exact 30848 bytes = 482 64-byte blocks. */
	if (fw->size != CYAPA_FW_SIZE) {
		dev_err(dev, "invalid firmware size = %zu, expected %u.\n",
			fw->size, CYAPA_FW_SIZE);
		return -EINVAL;
	}

	/* Verify header block */
	csum_expected = (fw->data[0] << 8) | fw->data[1];
	csum = cyapa_gen3_csum(&fw->data[2], CYAPA_FW_HDR_SIZE - 2);
	if (csum != csum_expected) {
		dev_err(dev, "%s %04x, expected: %04x\n",
			"invalid firmware header checksum = ",
			csum, csum_expected);
		return -EINVAL;
	}

	/* Verify firmware image */
	csum_expected = (fw->data[CYAPA_FW_HDR_SIZE - 2] << 8) |
			 fw->data[CYAPA_FW_HDR_SIZE - 1];
	csum = cyapa_gen3_csum(&fw->data[CYAPA_FW_HDR_SIZE],
			CYAPA_FW_DATA_SIZE);
	if (csum != csum_expected) {
		dev_err(dev, "%s %04x, expected: %04x\n",
			"invalid firmware header checksum = ",
			csum, csum_expected);
		return -EINVAL;
	}
	return 0;
}

/*
 * Write a |len| byte long buffer |buf| to the device, by chopping it up into a
 * sequence of smaller |CYAPA_CMD_LEN|-length write commands.
 *
 * The data bytes for a write command are prepended with the 1-byte offset
 * of the data relative to the start of |buf|.
 */
static int cyapa_gen3_write_buffer(struct cyapa *cyapa,
		const u8 *buf, size_t len)
{
	int error;
	size_t i;
	unsigned char cmd[CYAPA_CMD_LEN + 1];
	size_t cmd_len;

	for (i = 0; i < len; i += CYAPA_CMD_LEN) {
		const u8 *payload = &buf[i];

		cmd_len = (len - i >= CYAPA_CMD_LEN) ? CYAPA_CMD_LEN : len - i;
		cmd[0] = i;
		memcpy(&cmd[1], payload, cmd_len);

		error = cyapa_i2c_reg_write_block(cyapa, 0, cmd_len + 1, cmd);
		if (error)
			return error;
	}
	return 0;
}

/*
 * A firmware block write command writes 64 bytes of data to a single flash
 * page in the device.  The 78-byte block write command has the format:
 *   <0xff> <CMD> <Key> <Start> <Data> <Data-Checksum> <CMD Checksum>
 *
 *  <0xff>  - every command starts with 0xff
 *  <CMD>   - the write command value is 0x39
 *  <Key>   - write commands include an 8-byte key: { 00 01 02 03 04 05 06 07 }
 *  <Block> - Memory Block number (address / 64) (16-bit, big-endian)
 *  <Data>  - 64 bytes of firmware image data
 *  <Data Checksum> - sum of 64 <Data> bytes, modulo 0xff
 *  <CMD Checksum> - sum of 77 bytes, from 0xff to <Data Checksum>
 *
 * Each write command is split into 5 i2c write transactions of up to 16 bytes.
 * Each transaction starts with an i2c register offset: (00, 10, 20, 30, 40).
 */
static int cyapa_gen3_write_fw_block(struct cyapa *cyapa,
		u16 block, const u8 *data)
{
	int ret;
	struct gen3_write_block_cmd write_block_cmd;
	u8 status[BL_STATUS_SIZE];
	int tries;
	u8 bl_status, bl_error;

	/* Set write command and security key bytes. */
	write_block_cmd.checksum_seed = GEN3_BL_CMD_CHECKSUM_SEED;
	write_block_cmd.cmd_code = GEN3_BL_CMD_WRITE_BLOCK;
	memcpy(write_block_cmd.key, security_key, sizeof(security_key));
	put_unaligned_be16(block, &write_block_cmd.block_num);
	memcpy(write_block_cmd.block_data, data, CYAPA_FW_BLOCK_SIZE);
	write_block_cmd.block_checksum = cyapa_gen3_csum(
			write_block_cmd.block_data, CYAPA_FW_BLOCK_SIZE);
	write_block_cmd.cmd_checksum = cyapa_gen3_csum((u8 *)&write_block_cmd,
			sizeof(write_block_cmd) - 1);

	ret = cyapa_gen3_write_buffer(cyapa, (u8 *)&write_block_cmd,
			sizeof(write_block_cmd));
	if (ret)
		return ret;

	/* Wait for write to finish */
	tries = 11;  /* Programming for one block can take about 100ms. */
	do {
		usleep_range(10000, 20000);

		/* Check block write command result status. */
		ret = cyapa_i2c_reg_read_block(cyapa, BL_HEAD_OFFSET,
					       BL_STATUS_SIZE, status);
		if (ret != BL_STATUS_SIZE)
			return (ret < 0) ? ret : -EIO;
	} while ((status[REG_BL_STATUS] & BL_STATUS_BUSY) && --tries);

	/* Ignore WATCHDOG bit and reserved bits. */
	bl_status = status[REG_BL_STATUS] & ~BL_STATUS_REV_MASK;
	bl_error = status[REG_BL_ERROR] & ~BL_ERROR_RESERVED;

	if (bl_status & BL_STATUS_BUSY)
		ret = -ETIMEDOUT;
	else if (bl_status != BL_STATUS_RUNNING ||
		bl_error != BL_ERROR_BOOTLOADING)
		ret = -EIO;
	else
		ret = 0;

	return ret;
}

static int cyapa_gen3_write_blocks(struct cyapa *cyapa,
		size_t start_block, size_t block_count,
		const u8 *image_data)
{
	int error;
	int i;

	for (i = 0; i < block_count; i++) {
		size_t block = start_block + i;
		size_t addr = i * CYAPA_FW_BLOCK_SIZE;
		const u8 *data = &image_data[addr];

		error = cyapa_gen3_write_fw_block(cyapa, block, data);
		if (error)
			return error;
	}
	return 0;
}

static int cyapa_gen3_do_fw_update(struct cyapa *cyapa,
		const struct firmware *fw)
{
	struct device *dev = &cyapa->client->dev;
	int error;

	/* First write data, starting at byte 128 of fw->data */
	error = cyapa_gen3_write_blocks(cyapa,
		CYAPA_FW_DATA_BLOCK_START, CYAPA_FW_DATA_BLOCK_COUNT,
		&fw->data[CYAPA_FW_HDR_BLOCK_COUNT * CYAPA_FW_BLOCK_SIZE]);
	if (error) {
		dev_err(dev, "FW update aborted, write image: %d\n", error);
		return error;
	}

	/* Then write checksum */
	error = cyapa_gen3_write_blocks(cyapa,
		CYAPA_FW_HDR_BLOCK_START, CYAPA_FW_HDR_BLOCK_COUNT,
		&fw->data[0]);
	if (error) {
		dev_err(dev, "FW update aborted, write checksum: %d\n", error);
		return error;
	}

	return 0;
}

static ssize_t cyapa_gen3_do_calibrate(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct cyapa *cyapa = dev_get_drvdata(dev);
	int tries;
	int ret;

	ret = cyapa_read_byte(cyapa, CYAPA_CMD_DEV_STATUS);
	if (ret < 0) {
		dev_err(dev, "Error reading dev status: %d\n", ret);
		goto out;
	}
	if ((ret & CYAPA_DEV_NORMAL) != CYAPA_DEV_NORMAL) {
		dev_warn(dev, "Trackpad device is busy, device state: 0x%02x\n",
			 ret);
		ret = -EAGAIN;
		goto out;
	}

	ret = cyapa_write_byte(cyapa, CYAPA_CMD_SOFT_RESET,
			       OP_RECALIBRATION_MASK);
	if (ret < 0) {
		dev_err(dev, "Failed to send calibrate command: %d\n",
			ret);
		goto out;
	}

	tries = 20;  /* max recalibration timeout 2s. */
	do {
		/*
		 * For this recalibration, the max time will not exceed 2s.
		 * The average time is approximately 500 - 700 ms, and we
		 * will check the status every 100 - 200ms.
		 */
		usleep_range(100000, 200000);

		ret = cyapa_read_byte(cyapa, CYAPA_CMD_DEV_STATUS);
		if (ret < 0) {
			dev_err(dev, "Error reading dev status: %d\n",
				ret);
			goto out;
		}
		if ((ret & CYAPA_DEV_NORMAL) == CYAPA_DEV_NORMAL)
			break;
	} while (--tries);

	if (tries == 0) {
		dev_err(dev, "Failed to calibrate. Timeout.\n");
		ret = -ETIMEDOUT;
		goto out;
	}
	dev_dbg(dev, "Calibration successful.\n");

out:
	return ret < 0 ? ret : count;
}

static ssize_t cyapa_gen3_show_baseline(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct cyapa *cyapa = dev_get_drvdata(dev);
	int max_baseline, min_baseline;
	int tries;
	int ret;

	ret = cyapa_read_byte(cyapa, CYAPA_CMD_DEV_STATUS);
	if (ret < 0) {
		dev_err(dev, "Error reading dev status. err = %d\n", ret);
		goto out;
	}
	if ((ret & CYAPA_DEV_NORMAL) != CYAPA_DEV_NORMAL) {
		dev_warn(dev, "Trackpad device is busy. device state = 0x%x\n",
			 ret);
		ret = -EAGAIN;
		goto out;
	}

	ret = cyapa_write_byte(cyapa, CYAPA_CMD_SOFT_RESET,
			       OP_REPORT_BASELINE_MASK);
	if (ret < 0) {
		dev_err(dev, "Failed to send report baseline command. %d\n",
			ret);
		goto out;
	}

	tries = 3;  /* Try for 30 to 60 ms */
	do {
		usleep_range(10000, 20000);

		ret = cyapa_read_byte(cyapa, CYAPA_CMD_DEV_STATUS);
		if (ret < 0) {
			dev_err(dev, "Error reading dev status. err = %d\n",
				ret);
			goto out;
		}
		if ((ret & CYAPA_DEV_NORMAL) == CYAPA_DEV_NORMAL)
			break;
	} while (--tries);

	if (tries == 0) {
		dev_err(dev, "Device timed out going to Normal state.\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = cyapa_read_byte(cyapa, CYAPA_CMD_MAX_BASELINE);
	if (ret < 0) {
		dev_err(dev, "Failed to read max baseline. err = %d\n", ret);
		goto out;
	}
	max_baseline = ret;

	ret = cyapa_read_byte(cyapa, CYAPA_CMD_MIN_BASELINE);
	if (ret < 0) {
		dev_err(dev, "Failed to read min baseline. err = %d\n", ret);
		goto out;
	}
	min_baseline = ret;

	dev_dbg(dev, "Baseline report successful. Max: %d Min: %d\n",
		max_baseline, min_baseline);
	ret = scnprintf(buf, PAGE_SIZE, "%d %d\n", max_baseline, min_baseline);

out:
	return ret;
}

/*
 * cyapa_get_wait_time_for_pwr_cmd
 *
 * Compute the amount of time we need to wait after updating the touchpad
 * power mode. The touchpad needs to consume the incoming power mode set
 * command at the current clock rate.
 */

static u16 cyapa_get_wait_time_for_pwr_cmd(u8 pwr_mode)
{
	switch (pwr_mode) {
	case PWR_MODE_FULL_ACTIVE: return 20;
	case PWR_MODE_BTN_ONLY: return 20;
	case PWR_MODE_OFF: return 20;
	default: return cyapa_pwr_cmd_to_sleep_time(pwr_mode) + 50;
	}
}

/*
 * Set device power mode
 *
 * Write to the field to configure power state. Power states include :
 *   Full : Max scans and report rate.
 *   Idle : Report rate set by user specified time.
 *   ButtonOnly : No scans for fingers. When the button is triggered,
 *     a slave interrupt is asserted to notify host to wake up.
 *   Off : Only awake for i2c commands from host. No function for button
 *     or touch sensors.
 *
 * The power_mode command should conform to the following :
 *   Full : 0x3f
 *   Idle : Configurable from 20 to 1000ms. See note below for
 *     cyapa_sleep_time_to_pwr_cmd and cyapa_pwr_cmd_to_sleep_time
 *   ButtonOnly : 0x01
 *   Off : 0x00
 *
 * Device power mode can only be set when device is in operational mode.
 */
static int cyapa_gen3_set_power_mode(struct cyapa *cyapa, u8 power_mode,
		u16 always_unused)
{
	int ret;
	u8 power;
	int tries;
	u16 sleep_time;

	always_unused = 0;
	if (cyapa->state != CYAPA_STATE_OP)
		return 0;

	tries = SET_POWER_MODE_TRIES;
	while (tries--) {
		ret = cyapa_read_byte(cyapa, CYAPA_CMD_POWER_MODE);
		if (ret >= 0)
			break;
		usleep_range(SET_POWER_MODE_DELAY, 2 * SET_POWER_MODE_DELAY);
	}
	if (ret < 0)
		return ret;

	/*
	 * Return early if the power mode to set is the same as the current
	 * one.
	 */
	if ((ret & PWR_MODE_MASK) == power_mode)
		return 0;

	sleep_time = cyapa_get_wait_time_for_pwr_cmd(ret & PWR_MODE_MASK);
	power = ret;
	power &= ~PWR_MODE_MASK;
	power |= power_mode & PWR_MODE_MASK;
	tries = SET_POWER_MODE_TRIES;
	while (tries--) {
		ret = cyapa_write_byte(cyapa, CYAPA_CMD_POWER_MODE, power);
		if (!ret)
			break;
		usleep_range(SET_POWER_MODE_DELAY, 2 * SET_POWER_MODE_DELAY);
	}

	/*
	 * Wait for the newly set power command to go in at the previous
	 * clock speed (scanrate) used by the touchpad firmware. Not
	 * doing so before issuing the next command may result in errors
	 * depending on the command's content.
	 */
	msleep(sleep_time);
	return ret;
}

static int cyapa_gen3_get_query_data(struct cyapa *cyapa)
{
	u8 query_data[QUERY_DATA_SIZE];
	int ret;

	if (cyapa->state != CYAPA_STATE_OP)
		return -EBUSY;

	ret = cyapa_read_block(cyapa, CYAPA_CMD_GROUP_QUERY, query_data);
	if (ret != QUERY_DATA_SIZE)
		return (ret < 0) ? ret : -EIO;

	memcpy(&cyapa->product_id[0], &query_data[0], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &query_data[5], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &query_data[11], 2);
	cyapa->product_id[15] = '\0';

	cyapa->fw_maj_ver = query_data[15];
	cyapa->fw_min_ver = query_data[16];

	cyapa->btn_capability = query_data[19] & CAPABILITY_BTN_MASK;

	cyapa->gen = query_data[20] & 0x0f;

	cyapa->max_abs_x = ((query_data[21] & 0xf0) << 4) | query_data[22];
	cyapa->max_abs_y = ((query_data[21] & 0x0f) << 8) | query_data[23];

	cyapa->physical_size_x =
		((query_data[24] & 0xf0) << 4) | query_data[25];
	cyapa->physical_size_y =
		((query_data[24] & 0x0f) << 8) | query_data[26];

	cyapa->max_z = 255;

	return 0;
}

static int cyapa_gen3_bl_query_data(struct cyapa *cyapa)
{
	u8 bl_data[CYAPA_CMD_LEN];
	int ret;

	ret = cyapa_i2c_reg_read_block(cyapa, 0, CYAPA_CMD_LEN, bl_data);
	if (ret != CYAPA_CMD_LEN)
		return (ret < 0) ? ret : -EIO;

	/*
	 * This value will be updated again when entered application mode.
	 * If TP failed to enter application mode, this fw version values
	 * can be used as a reference.
	 * This firmware version valid when fw image checksum is valid.
	 */
	if (bl_data[REG_BL_STATUS] ==
			(BL_STATUS_RUNNING | BL_STATUS_CSUM_VALID)) {
		cyapa->fw_maj_ver = bl_data[GEN3_BL_IDLE_FW_MAJ_VER_OFFSET];
		cyapa->fw_min_ver = bl_data[GEN3_BL_IDLE_FW_MIN_VER_OFFSET];
	}

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
static int cyapa_gen3_do_operational_check(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	int error;

	switch (cyapa->state) {
	case CYAPA_STATE_BL_ACTIVE:
		error = cyapa_gen3_bl_deactivate(cyapa);
		if (error) {
			dev_err(dev, "failed to bl_deactivate: %d\n", error);
			return error;
		}

	/* Fallthrough state */
	case CYAPA_STATE_BL_IDLE:
		/* Try to get firmware version in bootloader mode. */
		cyapa_gen3_bl_query_data(cyapa);

		error = cyapa_gen3_bl_exit(cyapa);
		if (error) {
			dev_err(dev, "failed to bl_exit: %d\n", error);
			return error;
		}

	/* Fallthrough state */
	case CYAPA_STATE_OP:
		/*
		 * Reading query data before going back to the full mode
		 * may cause problems, so we set the power mode first here.
		 */
		error = cyapa_gen3_set_power_mode(cyapa,
				PWR_MODE_FULL_ACTIVE, 0);
		if (error)
			dev_err(dev, "%s: set full power mode failed: %d\n",
				__func__, error);
		error = cyapa_gen3_get_query_data(cyapa);
		if (error < 0)
			return error;

		/* Only support firmware protocol gen3 */
		if (cyapa->gen != CYAPA_GEN3) {
			dev_err(dev, "unsupported protocol version (%d)",
				cyapa->gen);
			return -EINVAL;
		}

		/* Only support product ID starting with CYTRA */
		if (memcmp(cyapa->product_id, product_id,
				strlen(product_id)) != 0) {
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

/*
 * Return false, do not continue process
 * Return true, continue process.
 */
static bool cyapa_gen3_irq_cmd_handler(struct cyapa *cyapa)
{
	/* Not gen3 irq command response, skip for continue. */
	if (cyapa->gen != CYAPA_GEN3)
		return true;

	if (cyapa->operational)
		return true;

	/*
	 * Driver in detecting or other interface function processing,
	 * so, stop cyapa_gen3_irq_handler to continue process to
	 * avoid unwanted to error detecting and processing.
	 *
	 * And also, avoid the periodicly accerted interrupts to be processed
	 * as touch inputs when gen3 failed to launch into application mode,
	 * which will cause gen3 stays in bootloader mode.
	 */
	return false;
}

static int cyapa_gen3_irq_handler(struct cyapa *cyapa)
{
	struct input_dev *input = cyapa->input;
	struct device *dev = &cyapa->client->dev;
	struct cyapa_reg_data data;
	int num_fingers;
	int ret;
	int i;

	ret = cyapa_read_block(cyapa, CYAPA_CMD_GROUP_DATA, (u8 *)&data);
	if (ret != sizeof(data)) {
		dev_err(dev, "failed to read report data, (%d)\n", ret);
		return -EINVAL;
	}

	if ((data.device_status & OP_STATUS_SRC) != OP_STATUS_SRC ||
	    (data.device_status & OP_STATUS_DEV) != CYAPA_DEV_NORMAL ||
	    (data.finger_btn & OP_DATA_VALID) != OP_DATA_VALID) {
		dev_err(dev, "invalid device state bytes, %02x %02x\n",
			data.device_status, data.finger_btn);
		return -EINVAL;
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
				 !!(data.finger_btn & OP_DATA_LEFT_BTN));
	if (cyapa->btn_capability & CAPABILITY_MIDDLE_BTN_MASK)
		input_report_key(input, BTN_MIDDLE,
				 !!(data.finger_btn & OP_DATA_MIDDLE_BTN));
	if (cyapa->btn_capability & CAPABILITY_RIGHT_BTN_MASK)
		input_report_key(input, BTN_RIGHT,
				 !!(data.finger_btn & OP_DATA_RIGHT_BTN));
	input_sync(input);

	return 0;
}

static int cyapa_gen3_initialize(struct cyapa *cyapa) { return 0; }
static int cyapa_gen3_bl_initiate(struct cyapa *cyapa,
		const struct firmware *fw) { return 0; }
static int cyapa_gen3_empty_output_data(struct cyapa *cyapa,
		u8 *buf, int *len, cb_sort func) { return 0; }

const struct cyapa_dev_ops cyapa_gen3_ops = {
	.check_fw = cyapa_gen3_check_fw,
	.bl_enter = cyapa_gen3_bl_enter,
	.bl_activate = cyapa_gen3_bl_activate,
	.update_fw = cyapa_gen3_do_fw_update,
	.bl_deactivate = cyapa_gen3_bl_deactivate,
	.bl_initiate = cyapa_gen3_bl_initiate,

	.show_baseline = cyapa_gen3_show_baseline,
	.calibrate_store = cyapa_gen3_do_calibrate,

	.initialize = cyapa_gen3_initialize,

	.state_parse = cyapa_gen3_state_parse,
	.operational_check = cyapa_gen3_do_operational_check,

	.irq_handler = cyapa_gen3_irq_handler,
	.irq_cmd_handler = cyapa_gen3_irq_cmd_handler,
	.sort_empty_output_data = cyapa_gen3_empty_output_data,
	.set_power_mode = cyapa_gen3_set_power_mode,
};
