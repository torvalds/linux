/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 *
 * Copyright (C) 2014-2015 Cypress Semiconductor, Inc.
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
#define CYAPA_GEN6   0x06   /* support TrueTouch GEN6 trackpad device. */

#define CYAPA_NAME   "Cypress APA Trackpad (cyapa)"

/*
 * Macros for SMBus communication
 */
#define SMBUS_READ  0x01
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

#define BTN_ONLY_MODE_NAME   "buttononly"
#define OFF_MODE_NAME        "off"

/* Common macros for PIP interface. */
#define PIP_HID_DESCRIPTOR_ADDR		0x0001
#define PIP_REPORT_DESCRIPTOR_ADDR	0x0002
#define PIP_INPUT_REPORT_ADDR		0x0003
#define PIP_OUTPUT_REPORT_ADDR		0x0004
#define PIP_CMD_DATA_ADDR		0x0006

#define PIP_RETRIEVE_DATA_STRUCTURE	0x24
#define PIP_CMD_CALIBRATE		0x28
#define PIP_BL_CMD_VERIFY_APP_INTEGRITY	0x31
#define PIP_BL_CMD_GET_BL_INFO		0x38
#define PIP_BL_CMD_PROGRAM_VERIFY_ROW	0x39
#define PIP_BL_CMD_LAUNCH_APP		0x3b
#define PIP_BL_CMD_INITIATE_BL		0x48
#define PIP_INVALID_CMD			0xff

#define PIP_HID_DESCRIPTOR_SIZE		32
#define PIP_HID_APP_REPORT_ID		0xf7
#define PIP_HID_BL_REPORT_ID		0xff

#define PIP_BL_CMD_REPORT_ID		0x40
#define PIP_BL_RESP_REPORT_ID		0x30
#define PIP_APP_CMD_REPORT_ID		0x2f
#define PIP_APP_RESP_REPORT_ID		0x1f

#define PIP_READ_SYS_INFO_CMD_LENGTH	7
#define PIP_BL_READ_APP_INFO_CMD_LENGTH	13
#define PIP_MIN_BL_CMD_LENGTH		13
#define PIP_MIN_BL_RESP_LENGTH		11
#define PIP_MIN_APP_CMD_LENGTH		7
#define PIP_MIN_APP_RESP_LENGTH		5
#define PIP_UNSUPPORTED_CMD_RESP_LENGTH	6
#define PIP_READ_SYS_INFO_RESP_LENGTH	71
#define PIP_BL_APP_INFO_RESP_LENGTH	30
#define PIP_BL_GET_INFO_RESP_LENGTH	19

#define PIP_BL_PLATFORM_VER_SHIFT	4
#define PIP_BL_PLATFORM_VER_MASK	0x0f

#define PIP_PRODUCT_FAMILY_MASK		0xf000
#define PIP_PRODUCT_FAMILY_TRACKPAD	0x1000

#define PIP_DEEP_SLEEP_STATE_ON		0x00
#define PIP_DEEP_SLEEP_STATE_OFF	0x01
#define PIP_DEEP_SLEEP_STATE_MASK	0x03
#define PIP_APP_DEEP_SLEEP_REPORT_ID	0xf0
#define PIP_DEEP_SLEEP_RESP_LENGTH	5
#define PIP_DEEP_SLEEP_OPCODE		0x08
#define PIP_DEEP_SLEEP_OPCODE_MASK	0x0f

#define PIP_RESP_LENGTH_OFFSET		0
#define	    PIP_RESP_LENGTH_SIZE	2
#define PIP_RESP_REPORT_ID_OFFSET	2
#define PIP_RESP_RSVD_OFFSET		3
#define     PIP_RESP_RSVD_KEY		0x00
#define PIP_RESP_BL_SOP_OFFSET		4
#define     PIP_SOP_KEY			0x01  /* Start of Packet */
#define     PIP_EOP_KEY			0x17  /* End of Packet */
#define PIP_RESP_APP_CMD_OFFSET		4
#define     GET_PIP_CMD_CODE(reg)	((reg) & 0x7f)
#define PIP_RESP_STATUS_OFFSET		5

#define VALID_CMD_RESP_HEADER(resp, cmd)				  \
	(((resp)[PIP_RESP_REPORT_ID_OFFSET] == PIP_APP_RESP_REPORT_ID) && \
	((resp)[PIP_RESP_RSVD_OFFSET] == PIP_RESP_RSVD_KEY) &&		  \
	(GET_PIP_CMD_CODE((resp)[PIP_RESP_APP_CMD_OFFSET]) == (cmd)))

#define PIP_CMD_COMPLETE_SUCCESS(resp_data) \
	((resp_data)[PIP_RESP_STATUS_OFFSET] == 0x00)

/* Variables to record latest gen5 trackpad power states. */
#define UNINIT_SLEEP_TIME	0xffff
#define UNINIT_PWR_MODE		0xff
#define PIP_DEV_SET_PWR_STATE(cyapa, s)		((cyapa)->dev_pwr_mode = (s))
#define PIP_DEV_GET_PWR_STATE(cyapa)		((cyapa)->dev_pwr_mode)
#define PIP_DEV_SET_SLEEP_TIME(cyapa, t)	((cyapa)->dev_sleep_time = (t))
#define PIP_DEV_GET_SLEEP_TIME(cyapa)		((cyapa)->dev_sleep_time)
#define PIP_DEV_UNINIT_SLEEP_TIME(cyapa)	\
		(((cyapa)->dev_sleep_time) == UNINIT_SLEEP_TIME)

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

	int (*set_power_mode)(struct cyapa *, u8, u16, bool);

	int (*set_proximity)(struct cyapa *, bool);
};

struct cyapa_pip_cmd_states {
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
	struct cyapa_pip_cmd_states pip;
};

enum cyapa_state {
	CYAPA_STATE_NO_DEVICE,
	CYAPA_STATE_BL_BUSY,
	CYAPA_STATE_BL_IDLE,
	CYAPA_STATE_BL_ACTIVE,
	CYAPA_STATE_OP,
	CYAPA_STATE_GEN5_BL,
	CYAPA_STATE_GEN5_APP,
	CYAPA_STATE_GEN6_BL,
	CYAPA_STATE_GEN6_APP,
};

struct gen6_interval_setting {
	u16 active_interval;
	u16 lp1_interval;
	u16 lp2_interval;
};

/* The main device structure */
struct cyapa {
	enum cyapa_state state;
	u8 status[BL_STATUS_SIZE];
	bool operational; /* true: ready for data reporting; false: not. */

	struct regulator *vcc;
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
	struct gen6_interval_setting gen6_interval_setting;

	/* Read from query data region. */
	char product_id[16];
	u8 platform_ver;  /* Platform version. */
	u8 fw_maj_ver;  /* Firmware major version. */
	u8 fw_min_ver;  /* Firmware minor version. */
	u8 btn_capability;
	u8 gen;
	int max_abs_x;
	int max_abs_y;
	int physical_size_x;
	int physical_size_y;

	/* Used in ttsp and truetouch based trackpad devices. */
	u8 x_origin;  /* X Axis Origin: 0 = left side; 1 = right side. */
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

ssize_t cyapa_i2c_pip_read(struct cyapa *cyapa, u8 *buf, size_t size);
ssize_t cyapa_i2c_pip_write(struct cyapa *cyapa, u8 *buf, size_t size);
int cyapa_empty_pip_output_data(struct cyapa *cyapa,
				u8 *buf, int *len, cb_sort func);
int cyapa_i2c_pip_cmd_irq_sync(struct cyapa *cyapa,
			       u8 *cmd, int cmd_len,
			       u8 *resp_data, int *resp_len,
			       unsigned long timeout,
			       cb_sort func,
			       bool irq_mode);
int cyapa_pip_state_parse(struct cyapa *cyapa, u8 *reg_data, int len);
bool cyapa_pip_sort_system_info_data(struct cyapa *cyapa, u8 *buf, int len);
bool cyapa_sort_tsg_pip_bl_resp_data(struct cyapa *cyapa, u8 *data, int len);
int cyapa_pip_deep_sleep(struct cyapa *cyapa, u8 state);
bool cyapa_sort_tsg_pip_app_resp_data(struct cyapa *cyapa, u8 *data, int len);
int cyapa_pip_bl_exit(struct cyapa *cyapa);
int cyapa_pip_bl_enter(struct cyapa *cyapa);


bool cyapa_is_pip_bl_mode(struct cyapa *cyapa);
bool cyapa_is_pip_app_mode(struct cyapa *cyapa);
int cyapa_pip_cmd_state_initialize(struct cyapa *cyapa);

int cyapa_pip_resume_scanning(struct cyapa *cyapa);
int cyapa_pip_suspend_scanning(struct cyapa *cyapa);

int cyapa_pip_check_fw(struct cyapa *cyapa, const struct firmware *fw);
int cyapa_pip_bl_initiate(struct cyapa *cyapa, const struct firmware *fw);
int cyapa_pip_do_fw_update(struct cyapa *cyapa, const struct firmware *fw);
int cyapa_pip_bl_activate(struct cyapa *cyapa);
int cyapa_pip_bl_deactivate(struct cyapa *cyapa);
ssize_t cyapa_pip_do_calibrate(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count);
int cyapa_pip_set_proximity(struct cyapa *cyapa, bool enable);

bool cyapa_pip_irq_cmd_handler(struct cyapa *cyapa);
int cyapa_pip_irq_handler(struct cyapa *cyapa);


extern u8 pip_read_sys_info[];
extern u8 pip_bl_read_app_info[];
extern const char product_id[];
extern const struct cyapa_dev_ops cyapa_gen3_ops;
extern const struct cyapa_dev_ops cyapa_gen5_ops;
extern const struct cyapa_dev_ops cyapa_gen6_ops;

#endif
