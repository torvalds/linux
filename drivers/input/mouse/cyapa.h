/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 *
 * Copyright (C) 2014 Cypress Semiconductor, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef _CYAPA_H
#define _CYAPA_H

#include <linux/firmware.h>

/* APA trackpad firmware generation number. */
#define CYAPA_GEN_UNKNOWN   0x00   /* unknown protocol. */
#define CYAPA_GEN3   0x03   /* support MT-protocol B with tracking ID. */
#define CYAPA_GEN5   0x05   /* support TrueTouch GEN5 trackpad device. */

#define CYAPA_NAME   "Cypress APA Trackpad (cyapa)"

/*
 * Macros for SMBus communication
 */
#define SMBUS_READ   0x01
#define SMBUS_WRITE 0x00
#define SMBUS_ENCODE_IDX(cmd, idx) ((cmd) | (((idx) & 0x03) << 1))
#define SMBUS_ENCODE_RW(cmd, rw) ((cmd) | ((rw) & 0x01))
#define SMBUS_BYTE_BLOCK_CMD_MASK 0x80
#define SMBUS_GROUP_BLOCK_CMD_MASK 0x40

/* Commands for read/write registers of Cypress trackpad */
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
#define CYAPA_CMD_MAX_BASELINE     0x0d
#define CYAPA_CMD_MIN_BASELINE     0x0e

#define BL_HEAD_OFFSET 0x00
#define BL_DATA_OFFSET 0x10

#define BL_STATUS_SIZE  3  /* Length of gen3 bootloader status registers */
#define CYAPA_REG_MAP_SIZE  256

/*
 * Gen3 Operational Device Status Register
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
 * Write-only command file register used to issue commands and
 * parameters to the bootloader.
 * The default value read from it is always 0x00.
 */
#define REG_BL_FILE	0x00
#define BL_FILE		0x00

/*
 * Bootloader Status Register
 *
 * bit 7: Busy
 * bit 6 - 5: Reserved
 * bit 4: Bootloader running
 * bit 3 - 2: Reserved
 * bit 1: Watchdog Reset
 * bit 0: Checksum valid
 */
#define REG_BL_STATUS        0x01
#define BL_STATUS_REV_6_5    0x60
#define BL_STATUS_BUSY       0x80
#define BL_STATUS_RUNNING    0x10
#define BL_STATUS_REV_3_2    0x0c
#define BL_STATUS_WATCHDOG   0x02
#define BL_STATUS_CSUM_VALID 0x01
#define BL_STATUS_REV_MASK (BL_STATUS_WATCHDOG | BL_STATUS_REV_3_2 | \
			    BL_STATUS_REV_6_5)

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
#define BL_ERROR_RESERVED    0x03
#define BL_ERROR_NO_ERR_IDLE    0x00
#define BL_ERROR_NO_ERR_ACTIVE  (BL_ERROR_BOOTLOADING)

#define CAPABILITY_BTN_SHIFT            3
#define CAPABILITY_LEFT_BTN_MASK	(0x01 << 3)
#define CAPABILITY_RIGHT_BTN_MASK	(0x01 << 4)
#define CAPABILITY_MIDDLE_BTN_MASK	(0x01 << 5)
#define CAPABILITY_BTN_MASK  (CAPABILITY_LEFT_BTN_MASK | \
			      CAPABILITY_RIGHT_BTN_MASK | \
			      CAPABILITY_MIDDLE_BTN_MASK)

#define PWR_MODE_MASK   0xfc
#define PWR_MODE_FULL_ACTIVE (0x3f << 2)
#define PWR_MODE_IDLE        (0x03 << 2) /* Default rt suspend scanrate: 30ms */
#define PWR_MODE_SLEEP       (0x05 << 2) /* Default suspend scanrate: 50ms */
#define PWR_MODE_BTN_ONLY    (0x01 << 2)
#define PWR_MODE_OFF         (0x00 << 2)

#define PWR_STATUS_MASK      0x0c
#define PWR_STATUS_ACTIVE    (0x03 << 2)
#define PWR_STATUS_IDLE      (0x02 << 2)
#define PWR_STATUS_BTN_ONLY  (0x01 << 2)
#define PWR_STATUS_OFF       (0x00 << 2)

#define AUTOSUSPEND_DELAY   2000 /* unit : ms */

#define UNINIT_SLEEP_TIME 0xFFFF
#define UNINIT_PWR_MODE   0xFF

#define BTN_ONLY_MODE_NAME   "buttononly"
#define OFF_MODE_NAME        "off"

/* The touch.id is used as the MT slot id, thus max MT slot is 15 */
#define CYAPA_MAX_MT_SLOTS  15

struct cyapa;

typedef bool (*cb_sort)(struct cyapa *, u8 *, int);

struct cyapa_dev_ops {
	int (*check_fw)(struct cyapa *, const struct firmware *);
	int (*bl_enter)(struct cyapa *);
	int (*bl_activate)(struct cyapa *);
	int (*bl_initiate)(struct cyapa *, const struct firmware *);
	int (*update_fw)(struct cyapa *, const struct firmware *);
	int (*bl_deactivate)(struct cyapa *);

	ssize_t (*show_baseline)(struct device *,
			struct device_attribute *, char *);
	ssize_t (*calibrate_store)(struct device *,
			struct device_attribute *, const char *, size_t);

	int (*initialize)(struct cyapa *cyapa);

	int (*state_parse)(struct cyapa *cyapa, u8 *reg_status, int len);
	int (*operational_check)(struct cyapa *cyapa);

	int (*irq_handler)(struct cyapa *);
	bool (*irq_cmd_handler)(struct cyapa *);
	int (*sort_empty_output_data)(struct cyapa *,
			u8 *, int *, cb_sort);

	int (*set_power_mode)(struct cyapa *, u8, u16);
};

struct cyapa_gen5_cmd_states {
	struct mutex cmd_lock;
	struct completion cmd_ready;
	atomic_t cmd_issued;
	u8 in_progress_cmd;
	bool is_irq_mode;

	cb_sort resp_sort_func;
	u8 *resp_data;
	int *resp_len;

	u8 irq_cmd_buf[CYAPA_REG_MAP_SIZE];
	u8 empty_buf[CYAPA_REG_MAP_SIZE];
};

union cyapa_cmd_states {
	struct cyapa_gen5_cmd_states gen5;
};

enum cyapa_state {
	CYAPA_STATE_NO_DEVICE,
	CYAPA_STATE_BL_BUSY,
	CYAPA_STATE_BL_IDLE,
	CYAPA_STATE_BL_ACTIVE,
	CYAPA_STATE_OP,
	CYAPA_STATE_GEN5_BL,
	CYAPA_STATE_GEN5_APP,
};

/* The main device structure */
struct cyapa {
	enum cyapa_state state;
	u8 status[BL_STATUS_SIZE];
	bool operational; /* true: ready for data reporting; false: not. */

	struct i2c_client *client;
	struct input_dev *input;
	char phys[32];	/* Device physical location */
	bool irq_wake;  /* Irq wake is enabled */
	bool smbus;

	/* power mode settings */
	u8 suspend_power_mode;
	u16 suspend_sleep_time;
	u8 runtime_suspend_power_mode;
	u16 runtime_suspend_sleep_time;
	u8 dev_pwr_mode;
	u16 dev_sleep_time;

	/* Read from query data region. */
	char product_id[16];
	u8 fw_maj_ver;  /* Firmware major version. */
	u8 fw_min_ver;  /* Firmware minor version. */
	u8 btn_capability;
	u8 gen;
	int max_abs_x;
	int max_abs_y;
	int physical_size_x;
	int physical_size_y;

	/* Used in ttsp and truetouch based trackpad devices. */
	u8 x_origin;  /* X Axis Origin: 0 = left side; 1 = rigth side. */
	u8 y_origin;  /* Y Axis Origin: 0 = top; 1 = bottom. */
	int electrodes_x;  /* Number of electrodes on the X Axis*/
	int electrodes_y;  /* Number of electrodes on the Y Axis*/
	int electrodes_rx;  /* Number of Rx electrodes */
	int aligned_electrodes_rx;  /* 4 aligned */
	int max_z;

	/*
	 * Used to synchronize the access or update the device state.
	 * And since update firmware and read firmware image process will take
	 * quite long time, maybe more than 10 seconds, so use mutex_lock
	 * to sync and wait other interface and detecting are done or ready.
	 */
	struct mutex state_sync_lock;

	const struct cyapa_dev_ops *ops;

	union cyapa_cmd_states cmd_states;
};


ssize_t cyapa_i2c_reg_read_block(struct cyapa *cyapa, u8 reg, size_t len,
				u8 *values);
ssize_t cyapa_smbus_read_block(struct cyapa *cyapa, u8 cmd, size_t len,
				u8 *values);

ssize_t cyapa_read_block(struct cyapa *cyapa, u8 cmd_idx, u8 *values);

int cyapa_poll_state(struct cyapa *cyapa, unsigned int timeout);

u8 cyapa_sleep_time_to_pwr_cmd(u16 sleep_time);
u16 cyapa_pwr_cmd_to_sleep_time(u8 pwr_mode);


extern const char product_id[];
extern const struct cyapa_dev_ops cyapa_gen3_ops;
extern const struct cyapa_dev_ops cyapa_gen5_ops;

#endif
