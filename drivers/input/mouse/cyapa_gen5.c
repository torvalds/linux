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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/crc-itu-t.h>
#include <linux/pm_runtime.h>
#include "cyapa.h"


/* Macro of TSG firmware image */
#define CYAPA_TSG_FLASH_MAP_BLOCK_SIZE      0x80
#define CYAPA_TSG_IMG_FW_HDR_SIZE           13
#define CYAPA_TSG_FW_ROW_SIZE               (CYAPA_TSG_FLASH_MAP_BLOCK_SIZE)
#define CYAPA_TSG_IMG_START_ROW_NUM         0x002e
#define CYAPA_TSG_IMG_END_ROW_NUM           0x01fe
#define CYAPA_TSG_IMG_APP_INTEGRITY_ROW_NUM 0x01ff
#define CYAPA_TSG_IMG_MAX_RECORDS           (CYAPA_TSG_IMG_END_ROW_NUM - \
				CYAPA_TSG_IMG_START_ROW_NUM + 1 + 1)
#define CYAPA_TSG_IMG_READ_SIZE             (CYAPA_TSG_FLASH_MAP_BLOCK_SIZE / 2)
#define CYAPA_TSG_START_OF_APPLICATION      0x1700
#define CYAPA_TSG_APP_INTEGRITY_SIZE        60
#define CYAPA_TSG_FLASH_MAP_METADATA_SIZE   60
#define CYAPA_TSG_BL_KEY_SIZE               8

#define CYAPA_TSG_MAX_CMD_SIZE              256

/* Macro of PIP interface */
#define PIP_BL_INITIATE_RESP_LEN            11
#define PIP_BL_FAIL_EXIT_RESP_LEN           11
#define PIP_BL_FAIL_EXIT_STATUS_CODE        0x0c
#define PIP_BL_VERIFY_INTEGRITY_RESP_LEN    12
#define PIP_BL_INTEGRITY_CHEKC_PASS         0x00
#define PIP_BL_BLOCK_WRITE_RESP_LEN         11

#define PIP_TOUCH_REPORT_ID         0x01
#define PIP_BTN_REPORT_ID           0x03
#define PIP_WAKEUP_EVENT_REPORT_ID  0x04
#define PIP_PUSH_BTN_REPORT_ID      0x06
#define GEN5_OLD_PUSH_BTN_REPORT_ID 0x05  /* Special for old Gen5 TP. */
#define PIP_PROXIMITY_REPORT_ID     0x07

#define PIP_PROXIMITY_REPORT_SIZE	6
#define PIP_PROXIMITY_DISTANCE_OFFSET	0x05
#define PIP_PROXIMITY_DISTANCE_MASK	0x01

#define PIP_TOUCH_REPORT_HEAD_SIZE     7
#define PIP_TOUCH_REPORT_MAX_SIZE      127
#define PIP_BTN_REPORT_HEAD_SIZE       6
#define PIP_BTN_REPORT_MAX_SIZE        14
#define PIP_WAKEUP_EVENT_SIZE          4

#define PIP_NUMBER_OF_TOUCH_OFFSET  5
#define PIP_NUMBER_OF_TOUCH_MASK    0x1f
#define PIP_BUTTONS_OFFSET          5
#define PIP_BUTTONS_MASK            0x0f
#define PIP_GET_EVENT_ID(reg)       (((reg) >> 5) & 0x03)
#define PIP_GET_TOUCH_ID(reg)       ((reg) & 0x1f)
#define PIP_TOUCH_TYPE_FINGER	    0x00
#define PIP_TOUCH_TYPE_PROXIMITY    0x01
#define PIP_TOUCH_TYPE_HOVER	    0x02
#define PIP_GET_TOUCH_TYPE(reg)     ((reg) & 0x07)

#define RECORD_EVENT_NONE        0
#define RECORD_EVENT_TOUCHDOWN	 1
#define RECORD_EVENT_DISPLACE    2
#define RECORD_EVENT_LIFTOFF     3

#define PIP_SENSING_MODE_MUTUAL_CAP_FINE   0x00
#define PIP_SENSING_MODE_SELF_CAP          0x02

#define PIP_SET_PROXIMITY	0x49

/* Macro of Gen5 */
#define GEN5_BL_MAX_OUTPUT_LENGTH     0x0100
#define GEN5_APP_MAX_OUTPUT_LENGTH    0x00fe

#define GEN5_POWER_STATE_ACTIVE              0x01
#define GEN5_POWER_STATE_LOOK_FOR_TOUCH      0x02
#define GEN5_POWER_STATE_READY               0x03
#define GEN5_POWER_STATE_IDLE                0x04
#define GEN5_POWER_STATE_BTN_ONLY            0x05
#define GEN5_POWER_STATE_OFF                 0x06

#define GEN5_POWER_READY_MAX_INTRVL_TIME  50   /* Unit: ms */
#define GEN5_POWER_IDLE_MAX_INTRVL_TIME   250  /* Unit: ms */

#define GEN5_CMD_GET_PARAMETER		     0x05
#define GEN5_CMD_SET_PARAMETER		     0x06
#define GEN5_PARAMETER_ACT_INTERVL_ID        0x4d
#define GEN5_PARAMETER_ACT_INTERVL_SIZE      1
#define GEN5_PARAMETER_ACT_LFT_INTERVL_ID    0x4f
#define GEN5_PARAMETER_ACT_LFT_INTERVL_SIZE  2
#define GEN5_PARAMETER_LP_INTRVL_ID          0x4c
#define GEN5_PARAMETER_LP_INTRVL_SIZE        2

#define GEN5_PARAMETER_DISABLE_PIP_REPORT    0x08

#define GEN5_BL_REPORT_DESCRIPTOR_SIZE            0x1d
#define GEN5_BL_REPORT_DESCRIPTOR_ID              0xfe
#define GEN5_APP_REPORT_DESCRIPTOR_SIZE           0xee
#define GEN5_APP_CONTRACT_REPORT_DESCRIPTOR_SIZE  0xfa
#define GEN5_APP_REPORT_DESCRIPTOR_ID             0xf6

#define GEN5_RETRIEVE_MUTUAL_PWC_DATA        0x00
#define GEN5_RETRIEVE_SELF_CAP_PWC_DATA      0x01

#define GEN5_RETRIEVE_DATA_ELEMENT_SIZE_MASK 0x07

#define GEN5_CMD_EXECUTE_PANEL_SCAN          0x2a
#define GEN5_CMD_RETRIEVE_PANEL_SCAN         0x2b
#define GEN5_PANEL_SCAN_MUTUAL_RAW_DATA      0x00
#define GEN5_PANEL_SCAN_MUTUAL_BASELINE      0x01
#define GEN5_PANEL_SCAN_MUTUAL_DIFFCOUNT     0x02
#define GEN5_PANEL_SCAN_SELF_RAW_DATA        0x03
#define GEN5_PANEL_SCAN_SELF_BASELINE        0x04
#define GEN5_PANEL_SCAN_SELF_DIFFCOUNT       0x05

/* The offset only valid for retrieve PWC and panel scan commands */
#define GEN5_RESP_DATA_STRUCTURE_OFFSET      10
#define GEN5_PWC_DATA_ELEMENT_SIZE_MASK      0x07


struct cyapa_pip_touch_record {
	/*
	 * Bit 7 - 3: reserved
	 * Bit 2 - 0: touch type;
	 *            0 : standard finger;
	 *            1 : proximity (Start supported in Gen5 TP).
	 *            2 : finger hover (defined, but not used yet.)
	 *            3 - 15 : reserved.
	 */
	u8 touch_type;

	/*
	 * Bit 7: indicates touch liftoff status.
	 *		0 : touch is currently on the panel.
	 *		1 : touch record indicates a liftoff.
	 * Bit 6 - 5: indicates an event associated with this touch instance
	 *		0 : no event
	 *		1 : touchdown
	 *		2 : significant displacement (> active distance)
	 *		3 : liftoff (record reports last known coordinates)
	 * Bit 4 - 0: An arbitrary ID tag associated with a finger
	 *		to allow tracking a touch as it moves around the panel.
	 */
	u8 touch_tip_event_id;

	/* Bit 7 - 0 of X-axis coordinate of the touch in pixel. */
	u8 x_lo;

	/* Bit 15 - 8 of X-axis coordinate of the touch in pixel. */
	u8 x_hi;

	/* Bit 7 - 0 of Y-axis coordinate of the touch in pixel. */
	u8 y_lo;

	/* Bit 15 - 8 of Y-axis coordinate of the touch in pixel. */
	u8 y_hi;

	/*
	 * The meaning of this value is different when touch_type is different.
	 * For standard finger type:
	 *	Touch intensity in counts, pressure value.
	 * For proximity type (Start supported in Gen5 TP):
	 *	The distance, in surface units, between the contact and
	 *	the surface.
	 **/
	u8 z;

	/*
	 * The length of the major axis of the ellipse of contact between
	 * the finger and the panel (ABS_MT_TOUCH_MAJOR).
	 */
	u8 major_axis_len;

	/*
	 * The length of the minor axis of the ellipse of contact between
	 * the finger and the panel (ABS_MT_TOUCH_MINOR).
	 */
	u8 minor_axis_len;

	/*
	 * The length of the major axis of the approaching tool.
	 * (ABS_MT_WIDTH_MAJOR)
	 */
	u8 major_tool_len;

	/*
	 * The length of the minor axis of the approaching tool.
	 * (ABS_MT_WIDTH_MINOR)
	 */
	u8 minor_tool_len;

	/*
	 * The angle between the panel vertical axis and
	 * the major axis of the contact ellipse. This value is an 8-bit
	 * signed integer. The range is -127 to +127 (corresponding to
	 * -90 degree and +90 degree respectively).
	 * The positive direction is clockwise from the vertical axis.
	 * If the ellipse of contact degenerates into a circle,
	 * orientation is reported as 0.
	 */
	u8 orientation;
} __packed;

struct cyapa_pip_report_data {
	u8 report_head[PIP_TOUCH_REPORT_HEAD_SIZE];
	struct cyapa_pip_touch_record touch_records[10];
} __packed;

struct cyapa_tsg_bin_image_head {
	u8 head_size;  /* Unit: bytes, including itself. */
	u8 ttda_driver_major_version;  /* Reserved as 0. */
	u8 ttda_driver_minor_version;  /* Reserved as 0. */
	u8 fw_major_version;
	u8 fw_minor_version;
	u8 fw_revision_control_number[8];
	u8 silicon_id_hi;
	u8 silicon_id_lo;
	u8 chip_revision;
	u8 family_id;
	u8 bl_ver_maj;
	u8 bl_ver_min;
} __packed;

struct cyapa_tsg_bin_image_data_record {
	u8 flash_array_id;
	__be16 row_number;
	/* The number of bytes of flash data contained in this record. */
	__be16 record_len;
	/* The flash program data. */
	u8 record_data[CYAPA_TSG_FW_ROW_SIZE];
} __packed;

struct cyapa_tsg_bin_image {
	struct cyapa_tsg_bin_image_head image_head;
	struct cyapa_tsg_bin_image_data_record records[];
} __packed;

struct pip_bl_packet_start {
	u8 sop;  /* Start of packet, must be 01h */
	u8 cmd_code;
	__le16 data_length;  /* Size of data parameter start from data[0] */
} __packed;

struct pip_bl_packet_end {
	__le16 crc;
	u8 eop;  /* End of packet, must be 17h */
} __packed;

struct pip_bl_cmd_head {
	__le16 addr;   /* Output report register address, must be 0004h */
	/* Size of packet not including output report register address */
	__le16 length;
	u8 report_id;  /* Bootloader output report id, must be 40h */
	u8 rsvd;  /* Reserved, must be 0 */
	struct pip_bl_packet_start packet_start;
	u8 data[];  /* Command data variable based on commands */
} __packed;

/* Initiate bootload command data structure. */
struct pip_bl_initiate_cmd_data {
	/* Key must be "A5h 01h 02h 03h FFh FEh FDh 5Ah" */
	u8 key[CYAPA_TSG_BL_KEY_SIZE];
	u8 metadata_raw_parameter[CYAPA_TSG_FLASH_MAP_METADATA_SIZE];
	__le16 metadata_crc;
} __packed;

struct tsg_bl_metadata_row_params {
	__le16 size;
	__le16 maximum_size;
	__le32 app_start;
	__le16 app_len;
	__le16 app_crc;
	__le32 app_entry;
	__le32 upgrade_start;
	__le16 upgrade_len;
	__le16 entry_row_crc;
	u8 padding[36];  /* Padding data must be 0 */
	__le16 metadata_crc;  /* CRC starts at offset of 60 */
} __packed;

/* Bootload program and verify row command data structure */
struct tsg_bl_flash_row_head {
	u8 flash_array_id;
	__le16 flash_row_id;
	u8 flash_data[];
} __packed;

struct pip_app_cmd_head {
	__le16 addr;   /* Output report register address, must be 0004h */
	/* Size of packet not including output report register address */
	__le16 length;
	u8 report_id;  /* Application output report id, must be 2Fh */
	u8 rsvd;  /* Reserved, must be 0 */
	/*
	 * Bit 7: reserved, must be 0.
	 * Bit 6-0: command code.
	 */
	u8 cmd_code;
	u8 parameter_data[];  /* Parameter data variable based on cmd_code */
} __packed;

/* Application get/set parameter command data structure */
struct gen5_app_set_parameter_data {
	u8 parameter_id;
	u8 parameter_size;
	__le32 value;
} __packed;

struct gen5_app_get_parameter_data {
	u8 parameter_id;
} __packed;

struct gen5_retrieve_panel_scan_data {
	__le16 read_offset;
	__le16 read_elements;
	u8 data_id;
} __packed;

u8 pip_read_sys_info[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00, 0x02 };
u8 pip_bl_read_app_info[] = { 0x04, 0x00, 0x0b, 0x00, 0x40, 0x00,
		0x01, 0x3c, 0x00, 0x00, 0xb0, 0x42, 0x17
	};

static u8 cyapa_pip_bl_cmd_key[] = { 0xa5, 0x01, 0x02, 0x03,
	0xff, 0xfe, 0xfd, 0x5a };

static int cyapa_pip_event_process(struct cyapa *cyapa,
				   struct cyapa_pip_report_data *report_data);

int cyapa_pip_cmd_state_initialize(struct cyapa *cyapa)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;

	init_completion(&pip->cmd_ready);
	atomic_set(&pip->cmd_issued, 0);
	mutex_init(&pip->cmd_lock);

	mutex_init(&pip->pm_stage_lock);
	pip->pm_stage = CYAPA_PM_DEACTIVE;

	pip->resp_sort_func = NULL;
	pip->in_progress_cmd = PIP_INVALID_CMD;
	pip->resp_data = NULL;
	pip->resp_len = NULL;

	cyapa->dev_pwr_mode = UNINIT_PWR_MODE;
	cyapa->dev_sleep_time = UNINIT_SLEEP_TIME;

	return 0;
}

/* Return negative errno, or else the number of bytes read. */
ssize_t cyapa_i2c_pip_read(struct cyapa *cyapa, u8 *buf, size_t size)
{
	int ret;

	if (size == 0)
		return 0;

	if (!buf || size > CYAPA_REG_MAP_SIZE)
		return -EINVAL;

	ret = i2c_master_recv(cyapa->client, buf, size);

	if (ret != size)
		return (ret < 0) ? ret : -EIO;
	return size;
}

/**
 * Return a negative errno code else zero on success.
 */
ssize_t cyapa_i2c_pip_write(struct cyapa *cyapa, u8 *buf, size_t size)
{
	int ret;

	if (!buf || !size)
		return -EINVAL;

	ret = i2c_master_send(cyapa->client, buf, size);

	if (ret != size)
		return (ret < 0) ? ret : -EIO;

	return 0;
}

static void cyapa_set_pip_pm_state(struct cyapa *cyapa,
				   enum cyapa_pm_stage pm_stage)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;

	mutex_lock(&pip->pm_stage_lock);
	pip->pm_stage = pm_stage;
	mutex_unlock(&pip->pm_stage_lock);
}

static void cyapa_reset_pip_pm_state(struct cyapa *cyapa)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;

	/* Indicates the pip->pm_stage is not valid. */
	mutex_lock(&pip->pm_stage_lock);
	pip->pm_stage = CYAPA_PM_DEACTIVE;
	mutex_unlock(&pip->pm_stage_lock);
}

static enum cyapa_pm_stage cyapa_get_pip_pm_state(struct cyapa *cyapa)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	enum cyapa_pm_stage pm_stage;

	mutex_lock(&pip->pm_stage_lock);
	pm_stage = pip->pm_stage;
	mutex_unlock(&pip->pm_stage_lock);

	return pm_stage;
}

/**
 * This function is aimed to dump all not read data in Gen5 trackpad
 * before send any command, otherwise, the interrupt line will be blocked.
 */
int cyapa_empty_pip_output_data(struct cyapa *cyapa,
		u8 *buf, int *len, cb_sort func)
{
	struct input_dev *input = cyapa->input;
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	enum cyapa_pm_stage pm_stage = cyapa_get_pip_pm_state(cyapa);
	int length;
	int report_count;
	int empty_count;
	int buf_len;
	int error;

	buf_len = 0;
	if (len) {
		buf_len = (*len < CYAPA_REG_MAP_SIZE) ?
				*len : CYAPA_REG_MAP_SIZE;
		*len = 0;
	}

	report_count = 8;  /* max 7 pending data before command response data */
	empty_count = 0;
	do {
		/*
		 * Depending on testing in cyapa driver, there are max 5 "02 00"
		 * packets between two valid buffered data report in firmware.
		 * So in order to dump all buffered data out and
		 * make interrupt line release for reassert again,
		 * we must set the empty_count check value bigger than 5 to
		 * make it work. Otherwise, in some situation,
		 * the interrupt line may unable to reactive again,
		 * which will cause trackpad device unable to
		 * report data any more.
		 * for example, it may happen in EFT and ESD testing.
		 */
		if (empty_count > 5)
			return 0;

		error = cyapa_i2c_pip_read(cyapa, pip->empty_buf,
				PIP_RESP_LENGTH_SIZE);
		if (error < 0)
			return error;

		length = get_unaligned_le16(pip->empty_buf);
		if (length == PIP_RESP_LENGTH_SIZE) {
			empty_count++;
			continue;
		} else if (length > CYAPA_REG_MAP_SIZE) {
			/* Should not happen */
			return -EINVAL;
		} else if (length == 0) {
			/* Application or bootloader launch data polled out. */
			length = PIP_RESP_LENGTH_SIZE;
			if (buf && buf_len && func &&
				func(cyapa, pip->empty_buf, length)) {
				length = min(buf_len, length);
				memcpy(buf, pip->empty_buf, length);
				*len = length;
				/* Response found, success. */
				return 0;
			}
			continue;
		}

		error = cyapa_i2c_pip_read(cyapa, pip->empty_buf, length);
		if (error < 0)
			return error;

		report_count--;
		empty_count = 0;
		length = get_unaligned_le16(pip->empty_buf);
		if (length <= PIP_RESP_LENGTH_SIZE) {
			empty_count++;
		} else if (buf && buf_len && func &&
			func(cyapa, pip->empty_buf, length)) {
			length = min(buf_len, length);
			memcpy(buf, pip->empty_buf, length);
			*len = length;
			/* Response found, success. */
			return 0;
		} else if (cyapa->operational && input && input->users &&
			   (pm_stage == CYAPA_PM_RUNTIME_RESUME ||
			    pm_stage == CYAPA_PM_RUNTIME_SUSPEND)) {
			/* Parse the data and report it if it's valid. */
			cyapa_pip_event_process(cyapa,
			       (struct cyapa_pip_report_data *)pip->empty_buf);
		}

		error = -EINVAL;
	} while (report_count);

	return error;
}

static int cyapa_do_i2c_pip_cmd_irq_sync(
		struct cyapa *cyapa,
		u8 *cmd, size_t cmd_len,
		unsigned long timeout)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int error;

	/* Wait for interrupt to set ready completion */
	init_completion(&pip->cmd_ready);

	atomic_inc(&pip->cmd_issued);
	error = cyapa_i2c_pip_write(cyapa, cmd, cmd_len);
	if (error) {
		atomic_dec(&pip->cmd_issued);
		return (error < 0) ? error : -EIO;
	}

	/* Wait for interrupt to indicate command is completed. */
	timeout = wait_for_completion_timeout(&pip->cmd_ready,
				msecs_to_jiffies(timeout));
	if (timeout == 0) {
		atomic_dec(&pip->cmd_issued);
		return -ETIMEDOUT;
	}

	return 0;
}

static int cyapa_do_i2c_pip_cmd_polling(
		struct cyapa *cyapa,
		u8 *cmd, size_t cmd_len,
		u8 *resp_data, int *resp_len,
		unsigned long timeout,
		cb_sort func)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int tries;
	int length;
	int error;

	atomic_inc(&pip->cmd_issued);
	error = cyapa_i2c_pip_write(cyapa, cmd, cmd_len);
	if (error) {
		atomic_dec(&pip->cmd_issued);
		return error < 0 ? error : -EIO;
	}

	length = resp_len ? *resp_len : 0;
	if (resp_data && resp_len && length != 0 && func) {
		tries = timeout / 5;
		do {
			usleep_range(3000, 5000);
			*resp_len = length;
			error = cyapa_empty_pip_output_data(cyapa,
					resp_data, resp_len, func);
			if (error || *resp_len == 0)
				continue;
			else
				break;
		} while (--tries > 0);
		if ((error || *resp_len == 0) || tries <= 0)
			error = error ? error : -ETIMEDOUT;
	}

	atomic_dec(&pip->cmd_issued);
	return error;
}

int cyapa_i2c_pip_cmd_irq_sync(
		struct cyapa *cyapa,
		u8 *cmd, int cmd_len,
		u8 *resp_data, int *resp_len,
		unsigned long timeout,
		cb_sort func,
		bool irq_mode)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int error;

	if (!cmd || !cmd_len)
		return -EINVAL;

	/* Commands must be serialized. */
	error = mutex_lock_interruptible(&pip->cmd_lock);
	if (error)
		return error;

	pip->resp_sort_func = func;
	pip->resp_data = resp_data;
	pip->resp_len = resp_len;

	if (cmd_len >= PIP_MIN_APP_CMD_LENGTH &&
			cmd[4] == PIP_APP_CMD_REPORT_ID) {
		/* Application command */
		pip->in_progress_cmd = cmd[6] & 0x7f;
	} else if (cmd_len >= PIP_MIN_BL_CMD_LENGTH &&
			cmd[4] == PIP_BL_CMD_REPORT_ID) {
		/* Bootloader command */
		pip->in_progress_cmd = cmd[7];
	}

	/* Send command data, wait and read output response data's length. */
	if (irq_mode) {
		pip->is_irq_mode = true;
		error = cyapa_do_i2c_pip_cmd_irq_sync(cyapa, cmd, cmd_len,
							timeout);
		if (error == -ETIMEDOUT && resp_data &&
				resp_len && *resp_len != 0 && func) {
			/*
			 * For some old version, there was no interrupt for
			 * the command response data, so need to poll here
			 * to try to get the response data.
			 */
			error = cyapa_empty_pip_output_data(cyapa,
					resp_data, resp_len, func);
			if (error || *resp_len == 0)
				error = error ? error : -ETIMEDOUT;
		}
	} else {
		pip->is_irq_mode = false;
		error = cyapa_do_i2c_pip_cmd_polling(cyapa, cmd, cmd_len,
				resp_data, resp_len, timeout, func);
	}

	pip->resp_sort_func = NULL;
	pip->resp_data = NULL;
	pip->resp_len = NULL;
	pip->in_progress_cmd = PIP_INVALID_CMD;

	mutex_unlock(&pip->cmd_lock);
	return error;
}

bool cyapa_sort_tsg_pip_bl_resp_data(struct cyapa *cyapa,
		u8 *data, int len)
{
	if (!data || len < PIP_MIN_BL_RESP_LENGTH)
		return false;

	/* Bootloader input report id 30h */
	if (data[PIP_RESP_REPORT_ID_OFFSET] == PIP_BL_RESP_REPORT_ID &&
			data[PIP_RESP_RSVD_OFFSET] == PIP_RESP_RSVD_KEY &&
			data[PIP_RESP_BL_SOP_OFFSET] == PIP_SOP_KEY)
		return true;

	return false;
}

bool cyapa_sort_tsg_pip_app_resp_data(struct cyapa *cyapa,
		u8 *data, int len)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int resp_len;

	if (!data || len < PIP_MIN_APP_RESP_LENGTH)
		return false;

	if (data[PIP_RESP_REPORT_ID_OFFSET] == PIP_APP_RESP_REPORT_ID &&
			data[PIP_RESP_RSVD_OFFSET] == PIP_RESP_RSVD_KEY) {
		resp_len = get_unaligned_le16(&data[PIP_RESP_LENGTH_OFFSET]);
		if (GET_PIP_CMD_CODE(data[PIP_RESP_APP_CMD_OFFSET]) == 0x00 &&
			resp_len == PIP_UNSUPPORTED_CMD_RESP_LENGTH &&
			data[5] == pip->in_progress_cmd) {
			/* Unsupported command code */
			return false;
		} else if (GET_PIP_CMD_CODE(data[PIP_RESP_APP_CMD_OFFSET]) ==
				pip->in_progress_cmd) {
			/* Correct command response received */
			return true;
		}
	}

	return false;
}

static bool cyapa_sort_pip_application_launch_data(struct cyapa *cyapa,
		u8 *buf, int len)
{
	if (buf == NULL || len < PIP_RESP_LENGTH_SIZE)
		return false;

	/*
	 * After reset or power on, trackpad device always sets to 0x00 0x00
	 * to indicate a reset or power on event.
	 */
	if (buf[0] == 0 && buf[1] == 0)
		return true;

	return false;
}

static bool cyapa_sort_gen5_hid_descriptor_data(struct cyapa *cyapa,
		u8 *buf, int len)
{
	int resp_len;
	int max_output_len;

	/* Check hid descriptor. */
	if (len != PIP_HID_DESCRIPTOR_SIZE)
		return false;

	resp_len = get_unaligned_le16(&buf[PIP_RESP_LENGTH_OFFSET]);
	max_output_len = get_unaligned_le16(&buf[16]);
	if (resp_len == PIP_HID_DESCRIPTOR_SIZE) {
		if (buf[PIP_RESP_REPORT_ID_OFFSET] == PIP_HID_BL_REPORT_ID &&
				max_output_len == GEN5_BL_MAX_OUTPUT_LENGTH) {
			/* BL mode HID Descriptor */
			return true;
		} else if ((buf[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_HID_APP_REPORT_ID) &&
				max_output_len == GEN5_APP_MAX_OUTPUT_LENGTH) {
			/* APP mode HID Descriptor */
			return true;
		}
	}

	return false;
}

static bool cyapa_sort_pip_deep_sleep_data(struct cyapa *cyapa,
		u8 *buf, int len)
{
	if (len == PIP_DEEP_SLEEP_RESP_LENGTH &&
		buf[PIP_RESP_REPORT_ID_OFFSET] ==
			PIP_APP_DEEP_SLEEP_REPORT_ID &&
		(buf[4] & PIP_DEEP_SLEEP_OPCODE_MASK) ==
			PIP_DEEP_SLEEP_OPCODE)
		return true;
	return false;
}

static int gen5_idle_state_parse(struct cyapa *cyapa)
{
	u8 resp_data[PIP_HID_DESCRIPTOR_SIZE];
	int max_output_len;
	int length;
	u8 cmd[2];
	int ret;
	int error;

	/*
	 * Dump all buffered data firstly for the situation
	 * when the trackpad is just power on the cyapa go here.
	 */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	memset(resp_data, 0, sizeof(resp_data));
	ret = cyapa_i2c_pip_read(cyapa, resp_data, 3);
	if (ret != 3)
		return ret < 0 ? ret : -EIO;

	length = get_unaligned_le16(&resp_data[PIP_RESP_LENGTH_OFFSET]);
	if (length == PIP_RESP_LENGTH_SIZE) {
		/* Normal state of Gen5 with no data to response */
		cyapa->gen = CYAPA_GEN5;

		cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

		/* Read description from trackpad device */
		cmd[0] = 0x01;
		cmd[1] = 0x00;
		length = PIP_HID_DESCRIPTOR_SIZE;
		error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
				cmd, PIP_RESP_LENGTH_SIZE,
				resp_data, &length,
				300,
				cyapa_sort_gen5_hid_descriptor_data,
				false);
		if (error)
			return error;

		length = get_unaligned_le16(
				&resp_data[PIP_RESP_LENGTH_OFFSET]);
		max_output_len = get_unaligned_le16(&resp_data[16]);
		if ((length == PIP_HID_DESCRIPTOR_SIZE ||
				length == PIP_RESP_LENGTH_SIZE) &&
			(resp_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_HID_BL_REPORT_ID) &&
			max_output_len == GEN5_BL_MAX_OUTPUT_LENGTH) {
			/* BL mode HID Description read */
			cyapa->state = CYAPA_STATE_GEN5_BL;
		} else if ((length == PIP_HID_DESCRIPTOR_SIZE ||
				length == PIP_RESP_LENGTH_SIZE) &&
			(resp_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_HID_APP_REPORT_ID) &&
			max_output_len == GEN5_APP_MAX_OUTPUT_LENGTH) {
			/* APP mode HID Description read */
			cyapa->state = CYAPA_STATE_GEN5_APP;
		} else {
			/* Should not happen!!! */
			cyapa->state = CYAPA_STATE_NO_DEVICE;
		}
	}

	return 0;
}

static int gen5_hid_description_header_parse(struct cyapa *cyapa, u8 *reg_data)
{
	int length;
	u8 resp_data[32];
	int max_output_len;
	int ret;

	/* 0x20 0x00 0xF7 is Gen5 Application HID Description Header;
	 * 0x20 0x00 0xFF is Gen5 Bootloader HID Description Header.
	 *
	 * Must read HID Description content through out,
	 * otherwise Gen5 trackpad cannot response next command
	 * or report any touch or button data.
	 */
	ret = cyapa_i2c_pip_read(cyapa, resp_data,
			PIP_HID_DESCRIPTOR_SIZE);
	if (ret != PIP_HID_DESCRIPTOR_SIZE)
		return ret < 0 ? ret : -EIO;
	length = get_unaligned_le16(&resp_data[PIP_RESP_LENGTH_OFFSET]);
	max_output_len = get_unaligned_le16(&resp_data[16]);
	if (length == PIP_RESP_LENGTH_SIZE) {
		if (reg_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_HID_BL_REPORT_ID) {
			/*
			 * BL mode HID Description has been previously
			 * read out.
			 */
			cyapa->gen = CYAPA_GEN5;
			cyapa->state = CYAPA_STATE_GEN5_BL;
		} else {
			/*
			 * APP mode HID Description has been previously
			 * read out.
			 */
			cyapa->gen = CYAPA_GEN5;
			cyapa->state = CYAPA_STATE_GEN5_APP;
		}
	} else if (length == PIP_HID_DESCRIPTOR_SIZE &&
			resp_data[2] == PIP_HID_BL_REPORT_ID &&
			max_output_len == GEN5_BL_MAX_OUTPUT_LENGTH) {
		/* BL mode HID Description read. */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_BL;
	} else if (length == PIP_HID_DESCRIPTOR_SIZE &&
			(resp_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_HID_APP_REPORT_ID) &&
			max_output_len == GEN5_APP_MAX_OUTPUT_LENGTH) {
		/* APP mode HID Description read. */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_APP;
	} else {
		/* Should not happen!!! */
		cyapa->state = CYAPA_STATE_NO_DEVICE;
	}

	return 0;
}

static int gen5_report_data_header_parse(struct cyapa *cyapa, u8 *reg_data)
{
	int length;

	length = get_unaligned_le16(&reg_data[PIP_RESP_LENGTH_OFFSET]);
	switch (reg_data[PIP_RESP_REPORT_ID_OFFSET]) {
	case PIP_TOUCH_REPORT_ID:
		if (length < PIP_TOUCH_REPORT_HEAD_SIZE ||
			length > PIP_TOUCH_REPORT_MAX_SIZE)
			return -EINVAL;
		break;
	case PIP_BTN_REPORT_ID:
	case GEN5_OLD_PUSH_BTN_REPORT_ID:
	case PIP_PUSH_BTN_REPORT_ID:
		if (length < PIP_BTN_REPORT_HEAD_SIZE ||
			length > PIP_BTN_REPORT_MAX_SIZE)
			return -EINVAL;
		break;
	case PIP_WAKEUP_EVENT_REPORT_ID:
		if (length != PIP_WAKEUP_EVENT_SIZE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	cyapa->gen = CYAPA_GEN5;
	cyapa->state = CYAPA_STATE_GEN5_APP;
	return 0;
}

static int gen5_cmd_resp_header_parse(struct cyapa *cyapa, u8 *reg_data)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int length;
	int ret;

	/*
	 * Must read report data through out,
	 * otherwise Gen5 trackpad cannot response next command
	 * or report any touch or button data.
	 */
	length = get_unaligned_le16(&reg_data[PIP_RESP_LENGTH_OFFSET]);
	ret = cyapa_i2c_pip_read(cyapa, pip->empty_buf, length);
	if (ret != length)
		return ret < 0 ? ret : -EIO;

	if (length == PIP_RESP_LENGTH_SIZE) {
		/* Previous command has read the data through out. */
		if (reg_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_BL_RESP_REPORT_ID) {
			/* Gen5 BL command response data detected */
			cyapa->gen = CYAPA_GEN5;
			cyapa->state = CYAPA_STATE_GEN5_BL;
		} else {
			/* Gen5 APP command response data detected */
			cyapa->gen = CYAPA_GEN5;
			cyapa->state = CYAPA_STATE_GEN5_APP;
		}
	} else if ((pip->empty_buf[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_BL_RESP_REPORT_ID) &&
			(pip->empty_buf[PIP_RESP_RSVD_OFFSET] ==
				PIP_RESP_RSVD_KEY) &&
			(pip->empty_buf[PIP_RESP_BL_SOP_OFFSET] ==
				PIP_SOP_KEY) &&
			(pip->empty_buf[length - 1] ==
				PIP_EOP_KEY)) {
		/* Gen5 BL command response data detected */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_BL;
	} else if (pip->empty_buf[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_APP_RESP_REPORT_ID &&
			pip->empty_buf[PIP_RESP_RSVD_OFFSET] ==
				PIP_RESP_RSVD_KEY) {
		/* Gen5 APP command response data detected */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_APP;
	} else {
		/* Should not happen!!! */
		cyapa->state = CYAPA_STATE_NO_DEVICE;
	}

	return 0;
}

static int cyapa_gen5_state_parse(struct cyapa *cyapa, u8 *reg_data, int len)
{
	int length;

	if (!reg_data || len < 3)
		return -EINVAL;

	cyapa->state = CYAPA_STATE_NO_DEVICE;

	/* Parse based on Gen5 characteristic registers and bits */
	length = get_unaligned_le16(&reg_data[PIP_RESP_LENGTH_OFFSET]);
	if (length == 0 || length == PIP_RESP_LENGTH_SIZE) {
		gen5_idle_state_parse(cyapa);
	} else if (length == PIP_HID_DESCRIPTOR_SIZE &&
			(reg_data[2] == PIP_HID_BL_REPORT_ID ||
				reg_data[2] == PIP_HID_APP_REPORT_ID)) {
		gen5_hid_description_header_parse(cyapa, reg_data);
	} else if ((length == GEN5_APP_REPORT_DESCRIPTOR_SIZE ||
			length == GEN5_APP_CONTRACT_REPORT_DESCRIPTOR_SIZE) &&
			reg_data[2] == GEN5_APP_REPORT_DESCRIPTOR_ID) {
		/* 0xEE 0x00 0xF6 is Gen5 APP report description header. */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_APP;
	} else if (length == GEN5_BL_REPORT_DESCRIPTOR_SIZE &&
			reg_data[2] == GEN5_BL_REPORT_DESCRIPTOR_ID) {
		/* 0x1D 0x00 0xFE is Gen5 BL report descriptor header. */
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = CYAPA_STATE_GEN5_BL;
	} else if (reg_data[2] == PIP_TOUCH_REPORT_ID ||
			reg_data[2] == PIP_BTN_REPORT_ID ||
			reg_data[2] == GEN5_OLD_PUSH_BTN_REPORT_ID ||
			reg_data[2] == PIP_PUSH_BTN_REPORT_ID ||
			reg_data[2] == PIP_WAKEUP_EVENT_REPORT_ID) {
		gen5_report_data_header_parse(cyapa, reg_data);
	} else if (reg_data[2] == PIP_BL_RESP_REPORT_ID ||
			reg_data[2] == PIP_APP_RESP_REPORT_ID) {
		gen5_cmd_resp_header_parse(cyapa, reg_data);
	}

	if (cyapa->gen == CYAPA_GEN5) {
		/*
		 * Must read the content (e.g.: report description and so on)
		 * from trackpad device throughout. Otherwise,
		 * Gen5 trackpad cannot response to next command or
		 * report any touch or button data later.
		 */
		cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

		if (cyapa->state == CYAPA_STATE_GEN5_APP ||
			cyapa->state == CYAPA_STATE_GEN5_BL)
			return 0;
	}

	return -EAGAIN;
}

static struct cyapa_tsg_bin_image_data_record *
cyapa_get_image_record_data_num(const struct firmware *fw,
		int *record_num)
{
	int head_size;

	head_size = fw->data[0] + 1;
	*record_num = (fw->size - head_size) /
			sizeof(struct cyapa_tsg_bin_image_data_record);
	return (struct cyapa_tsg_bin_image_data_record *)&fw->data[head_size];
}

int cyapa_pip_bl_initiate(struct cyapa *cyapa, const struct firmware *fw)
{
	struct cyapa_tsg_bin_image_data_record *image_records;
	struct pip_bl_cmd_head *bl_cmd_head;
	struct pip_bl_packet_start *bl_packet_start;
	struct pip_bl_initiate_cmd_data *cmd_data;
	struct pip_bl_packet_end *bl_packet_end;
	u8 cmd[CYAPA_TSG_MAX_CMD_SIZE];
	int cmd_len;
	u16 cmd_data_len;
	u16 cmd_crc = 0;
	u16 meta_data_crc = 0;
	u8 resp_data[11];
	int resp_len;
	int records_num;
	u8 *data;
	int error;

	/* Try to dump all buffered report data before any send command. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	memset(cmd, 0, CYAPA_TSG_MAX_CMD_SIZE);
	bl_cmd_head = (struct pip_bl_cmd_head *)cmd;
	cmd_data_len = CYAPA_TSG_BL_KEY_SIZE + CYAPA_TSG_FLASH_MAP_BLOCK_SIZE;
	cmd_len = sizeof(struct pip_bl_cmd_head) + cmd_data_len +
		  sizeof(struct pip_bl_packet_end);

	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &bl_cmd_head->addr);
	put_unaligned_le16(cmd_len - 2, &bl_cmd_head->length);
	bl_cmd_head->report_id = PIP_BL_CMD_REPORT_ID;

	bl_packet_start = &bl_cmd_head->packet_start;
	bl_packet_start->sop = PIP_SOP_KEY;
	bl_packet_start->cmd_code = PIP_BL_CMD_INITIATE_BL;
	/* 8 key bytes and 128 bytes block size */
	put_unaligned_le16(cmd_data_len, &bl_packet_start->data_length);

	cmd_data = (struct pip_bl_initiate_cmd_data *)bl_cmd_head->data;
	memcpy(cmd_data->key, cyapa_pip_bl_cmd_key, CYAPA_TSG_BL_KEY_SIZE);

	image_records = cyapa_get_image_record_data_num(fw, &records_num);

	/* APP_INTEGRITY row is always the last row block */
	data = image_records[records_num - 1].record_data;
	memcpy(cmd_data->metadata_raw_parameter, data,
		CYAPA_TSG_FLASH_MAP_METADATA_SIZE);

	meta_data_crc = crc_itu_t(0xffff, cmd_data->metadata_raw_parameter,
				CYAPA_TSG_FLASH_MAP_METADATA_SIZE);
	put_unaligned_le16(meta_data_crc, &cmd_data->metadata_crc);

	bl_packet_end = (struct pip_bl_packet_end *)(bl_cmd_head->data +
				cmd_data_len);
	cmd_crc = crc_itu_t(0xffff, (u8 *)bl_packet_start,
		sizeof(struct pip_bl_packet_start) + cmd_data_len);
	put_unaligned_le16(cmd_crc, &bl_packet_end->crc);
	bl_packet_end->eop = PIP_EOP_KEY;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, cmd_len,
			resp_data, &resp_len, 12000,
			cyapa_sort_tsg_pip_bl_resp_data, true);
	if (error || resp_len != PIP_BL_INITIATE_RESP_LEN ||
			resp_data[2] != PIP_BL_RESP_REPORT_ID ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error ? error : -EAGAIN;

	return 0;
}

static bool cyapa_sort_pip_bl_exit_data(struct cyapa *cyapa, u8 *buf, int len)
{
	if (buf == NULL || len < PIP_RESP_LENGTH_SIZE)
		return false;

	if (buf[0] == 0 && buf[1] == 0)
		return true;

	/* Exit bootloader failed for some reason. */
	if (len == PIP_BL_FAIL_EXIT_RESP_LEN &&
			buf[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_BL_RESP_REPORT_ID &&
			buf[PIP_RESP_RSVD_OFFSET] == PIP_RESP_RSVD_KEY &&
			buf[PIP_RESP_BL_SOP_OFFSET] == PIP_SOP_KEY &&
			buf[10] == PIP_EOP_KEY)
		return true;

	return false;
}

int cyapa_pip_bl_exit(struct cyapa *cyapa)
{

	u8 bl_gen5_bl_exit[] = { 0x04, 0x00,
		0x0B, 0x00, 0x40, 0x00, 0x01, 0x3b, 0x00, 0x00,
		0x20, 0xc7, 0x17
	};
	u8 resp_data[11];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			bl_gen5_bl_exit, sizeof(bl_gen5_bl_exit),
			resp_data, &resp_len,
			5000, cyapa_sort_pip_bl_exit_data, false);
	if (error)
		return error;

	if (resp_len == PIP_BL_FAIL_EXIT_RESP_LEN ||
			resp_data[PIP_RESP_REPORT_ID_OFFSET] ==
				PIP_BL_RESP_REPORT_ID)
		return -EAGAIN;

	if (resp_data[0] == 0x00 && resp_data[1] == 0x00)
		return 0;

	return -ENODEV;
}

int cyapa_pip_bl_enter(struct cyapa *cyapa)
{
	u8 cmd[] = { 0x04, 0x00, 0x05, 0x00, 0x2F, 0x00, 0x01 };
	u8 resp_data[2];
	int resp_len;
	int error;

	error = cyapa_poll_state(cyapa, 500);
	if (error < 0)
		return error;

	/* Already in bootloader mode, Skipping exit. */
	if (cyapa_is_pip_bl_mode(cyapa))
		return 0;
	else if (!cyapa_is_pip_app_mode(cyapa))
		return -EINVAL;

	/* Try to dump all buffered report data before any send command. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	/*
	 * Send bootloader enter command to trackpad device,
	 * after enter bootloader, the response data is two bytes of 0x00 0x00.
	 */
	resp_len = sizeof(resp_data);
	memset(resp_data, 0, resp_len);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			5000, cyapa_sort_pip_application_launch_data,
			true);
	if (error || resp_data[0] != 0x00 || resp_data[1] != 0x00)
		return error < 0 ? error : -EAGAIN;

	cyapa->operational = false;
	if (cyapa->gen == CYAPA_GEN5)
		cyapa->state = CYAPA_STATE_GEN5_BL;
	else if (cyapa->gen == CYAPA_GEN6)
		cyapa->state = CYAPA_STATE_GEN6_BL;
	return 0;
}

static int cyapa_pip_fw_head_check(struct cyapa *cyapa,
		struct cyapa_tsg_bin_image_head *image_head)
{
	if (image_head->head_size != 0x0C && image_head->head_size != 0x12)
		return -EINVAL;

	switch (cyapa->gen) {
	case CYAPA_GEN6:
		if (image_head->family_id != 0x9B ||
		    image_head->silicon_id_hi != 0x0B)
			return -EINVAL;
		break;
	case CYAPA_GEN5:
		/* Gen5 without proximity support. */
		if (cyapa->platform_ver < 2) {
			if (image_head->head_size == 0x0C)
				break;
			return -EINVAL;
		}

		if (image_head->family_id != 0x91 ||
		    image_head->silicon_id_hi != 0x02)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int cyapa_pip_check_fw(struct cyapa *cyapa, const struct firmware *fw)
{
	struct device *dev = &cyapa->client->dev;
	struct cyapa_tsg_bin_image_data_record *image_records;
	const struct cyapa_tsg_bin_image_data_record *app_integrity;
	const struct tsg_bl_metadata_row_params *metadata;
	int flash_records_count;
	u32 fw_app_start, fw_upgrade_start;
	u16 fw_app_len, fw_upgrade_len;
	u16 app_crc;
	u16 app_integrity_crc;
	int i;

	/* Verify the firmware image not miss-used for Gen5 and Gen6. */
	if (cyapa_pip_fw_head_check(cyapa,
		(struct cyapa_tsg_bin_image_head *)fw->data)) {
		dev_err(dev, "%s: firmware image not match TP device.\n",
			     __func__);
		return -EINVAL;
	}

	image_records =
		cyapa_get_image_record_data_num(fw, &flash_records_count);

	/*
	 * APP_INTEGRITY row is always the last row block,
	 * and the row id must be 0x01ff.
	 */
	app_integrity = &image_records[flash_records_count - 1];

	if (app_integrity->flash_array_id != 0x00 ||
	    get_unaligned_be16(&app_integrity->row_number) != 0x01ff) {
		dev_err(dev, "%s: invalid app_integrity data.\n", __func__);
		return -EINVAL;
	}

	metadata = (const void *)app_integrity->record_data;

	/* Verify app_integrity crc */
	app_integrity_crc = crc_itu_t(0xffff, app_integrity->record_data,
				      CYAPA_TSG_APP_INTEGRITY_SIZE);
	if (app_integrity_crc != get_unaligned_le16(&metadata->metadata_crc)) {
		dev_err(dev, "%s: invalid app_integrity crc.\n", __func__);
		return -EINVAL;
	}

	fw_app_start = get_unaligned_le32(&metadata->app_start);
	fw_app_len = get_unaligned_le16(&metadata->app_len);
	fw_upgrade_start = get_unaligned_le32(&metadata->upgrade_start);
	fw_upgrade_len = get_unaligned_le16(&metadata->upgrade_len);

	if (fw_app_start % CYAPA_TSG_FW_ROW_SIZE ||
	    fw_app_len % CYAPA_TSG_FW_ROW_SIZE ||
	    fw_upgrade_start % CYAPA_TSG_FW_ROW_SIZE ||
	    fw_upgrade_len % CYAPA_TSG_FW_ROW_SIZE) {
		dev_err(dev, "%s: invalid image alignment.\n", __func__);
		return -EINVAL;
	}

	/* Verify application image CRC. */
	app_crc = 0xffffU;
	for (i = 0; i < fw_app_len / CYAPA_TSG_FW_ROW_SIZE; i++) {
		const u8 *data = image_records[i].record_data;

		app_crc = crc_itu_t(app_crc, data, CYAPA_TSG_FW_ROW_SIZE);
	}

	if (app_crc != get_unaligned_le16(&metadata->app_crc)) {
		dev_err(dev, "%s: invalid firmware app crc check.\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int cyapa_pip_write_fw_block(struct cyapa *cyapa,
		struct cyapa_tsg_bin_image_data_record *flash_record)
{
	struct pip_bl_cmd_head *bl_cmd_head;
	struct pip_bl_packet_start *bl_packet_start;
	struct tsg_bl_flash_row_head *flash_row_head;
	struct pip_bl_packet_end *bl_packet_end;
	u8 cmd[CYAPA_TSG_MAX_CMD_SIZE];
	u16 cmd_len;
	u8 flash_array_id;
	u16 flash_row_id;
	u16 record_len;
	u8 *record_data;
	u16 data_len;
	u16 crc;
	u8 resp_data[11];
	int resp_len;
	int error;

	flash_array_id = flash_record->flash_array_id;
	flash_row_id = get_unaligned_be16(&flash_record->row_number);
	record_len = get_unaligned_be16(&flash_record->record_len);
	record_data = flash_record->record_data;

	memset(cmd, 0, CYAPA_TSG_MAX_CMD_SIZE);
	bl_cmd_head = (struct pip_bl_cmd_head *)cmd;
	bl_packet_start = &bl_cmd_head->packet_start;
	cmd_len = sizeof(struct pip_bl_cmd_head) +
		  sizeof(struct tsg_bl_flash_row_head) +
		  CYAPA_TSG_FLASH_MAP_BLOCK_SIZE +
		  sizeof(struct pip_bl_packet_end);

	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &bl_cmd_head->addr);
	/* Don't include 2 bytes register address */
	put_unaligned_le16(cmd_len - 2, &bl_cmd_head->length);
	bl_cmd_head->report_id = PIP_BL_CMD_REPORT_ID;
	bl_packet_start->sop = PIP_SOP_KEY;
	bl_packet_start->cmd_code = PIP_BL_CMD_PROGRAM_VERIFY_ROW;

	/* 1 (Flash Array ID) + 2 (Flash Row ID) + 128 (flash data) */
	data_len = sizeof(struct tsg_bl_flash_row_head) + record_len;
	put_unaligned_le16(data_len, &bl_packet_start->data_length);

	flash_row_head = (struct tsg_bl_flash_row_head *)bl_cmd_head->data;
	flash_row_head->flash_array_id = flash_array_id;
	put_unaligned_le16(flash_row_id, &flash_row_head->flash_row_id);
	memcpy(flash_row_head->flash_data, record_data, record_len);

	bl_packet_end = (struct pip_bl_packet_end *)(bl_cmd_head->data +
						      data_len);
	crc = crc_itu_t(0xffff, (u8 *)bl_packet_start,
		sizeof(struct pip_bl_packet_start) + data_len);
	put_unaligned_le16(crc, &bl_packet_end->crc);
	bl_packet_end->eop = PIP_EOP_KEY;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, cmd_len,
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_bl_resp_data, true);
	if (error || resp_len != PIP_BL_BLOCK_WRITE_RESP_LEN ||
			resp_data[2] != PIP_BL_RESP_REPORT_ID ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error < 0 ? error : -EAGAIN;

	return 0;
}

int cyapa_pip_do_fw_update(struct cyapa *cyapa,
		const struct firmware *fw)
{
	struct device *dev = &cyapa->client->dev;
	struct cyapa_tsg_bin_image_data_record *image_records;
	int flash_records_count;
	int i;
	int error;

	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	image_records =
		cyapa_get_image_record_data_num(fw, &flash_records_count);

	/*
	 * The last flash row 0x01ff has been written through bl_initiate
	 * command, so DO NOT write flash 0x01ff to trackpad device.
	 */
	for (i = 0; i < (flash_records_count - 1); i++) {
		error = cyapa_pip_write_fw_block(cyapa, &image_records[i]);
		if (error) {
			dev_err(dev, "%s: Gen5 FW update aborted: %d\n",
				__func__, error);
			return error;
		}
	}

	return 0;
}

static int cyapa_gen5_change_power_state(struct cyapa *cyapa, u8 power_state)
{
	u8 cmd[8] = { 0x04, 0x00, 0x06, 0x00, 0x2f, 0x00, 0x08, 0x01 };
	u8 resp_data[6];
	int resp_len;
	int error;

	cmd[7] = power_state;
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, 0x08) ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error < 0 ? error : -EINVAL;

	return 0;
}

static int cyapa_gen5_set_interval_time(struct cyapa *cyapa,
		u8 parameter_id, u16 interval_time)
{
	struct pip_app_cmd_head *app_cmd_head;
	struct gen5_app_set_parameter_data *parameter_data;
	u8 cmd[CYAPA_TSG_MAX_CMD_SIZE];
	int cmd_len;
	u8 resp_data[7];
	int resp_len;
	u8 parameter_size;
	int error;

	memset(cmd, 0, CYAPA_TSG_MAX_CMD_SIZE);
	app_cmd_head = (struct pip_app_cmd_head *)cmd;
	parameter_data = (struct gen5_app_set_parameter_data *)
			 app_cmd_head->parameter_data;
	cmd_len = sizeof(struct pip_app_cmd_head) +
		  sizeof(struct gen5_app_set_parameter_data);

	switch (parameter_id) {
	case GEN5_PARAMETER_ACT_INTERVL_ID:
		parameter_size = GEN5_PARAMETER_ACT_INTERVL_SIZE;
		break;
	case GEN5_PARAMETER_ACT_LFT_INTERVL_ID:
		parameter_size = GEN5_PARAMETER_ACT_LFT_INTERVL_SIZE;
		break;
	case GEN5_PARAMETER_LP_INTRVL_ID:
		parameter_size = GEN5_PARAMETER_LP_INTRVL_SIZE;
		break;
	default:
		return -EINVAL;
	}

	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	/*
	 * Don't include unused parameter value bytes and
	 * 2 bytes register address.
	 */
	put_unaligned_le16(cmd_len - (4 - parameter_size) - 2,
			   &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = GEN5_CMD_SET_PARAMETER;
	parameter_data->parameter_id = parameter_id;
	parameter_data->parameter_size = parameter_size;
	put_unaligned_le32((u32)interval_time, &parameter_data->value);
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, cmd_len,
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || resp_data[5] != parameter_id ||
		resp_data[6] != parameter_size ||
		!VALID_CMD_RESP_HEADER(resp_data, GEN5_CMD_SET_PARAMETER))
		return error < 0 ? error : -EINVAL;

	return 0;
}

static int cyapa_gen5_get_interval_time(struct cyapa *cyapa,
		u8 parameter_id, u16 *interval_time)
{
	struct pip_app_cmd_head *app_cmd_head;
	struct gen5_app_get_parameter_data *parameter_data;
	u8 cmd[CYAPA_TSG_MAX_CMD_SIZE];
	int cmd_len;
	u8 resp_data[11];
	int resp_len;
	u8 parameter_size;
	u16 mask, i;
	int error;

	memset(cmd, 0, CYAPA_TSG_MAX_CMD_SIZE);
	app_cmd_head = (struct pip_app_cmd_head *)cmd;
	parameter_data = (struct gen5_app_get_parameter_data *)
			 app_cmd_head->parameter_data;
	cmd_len = sizeof(struct pip_app_cmd_head) +
		  sizeof(struct gen5_app_get_parameter_data);

	*interval_time = 0;
	switch (parameter_id) {
	case GEN5_PARAMETER_ACT_INTERVL_ID:
		parameter_size = GEN5_PARAMETER_ACT_INTERVL_SIZE;
		break;
	case GEN5_PARAMETER_ACT_LFT_INTERVL_ID:
		parameter_size = GEN5_PARAMETER_ACT_LFT_INTERVL_SIZE;
		break;
	case GEN5_PARAMETER_LP_INTRVL_ID:
		parameter_size = GEN5_PARAMETER_LP_INTRVL_SIZE;
		break;
	default:
		return -EINVAL;
	}

	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	/* Don't include 2 bytes register address */
	put_unaligned_le16(cmd_len - 2, &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = GEN5_CMD_GET_PARAMETER;
	parameter_data->parameter_id = parameter_id;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, cmd_len,
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || resp_data[5] != parameter_id || resp_data[6] == 0 ||
		!VALID_CMD_RESP_HEADER(resp_data, GEN5_CMD_GET_PARAMETER))
		return error < 0 ? error : -EINVAL;

	mask = 0;
	for (i = 0; i < parameter_size; i++)
		mask |= (0xff << (i * 8));
	*interval_time = get_unaligned_le16(&resp_data[7]) & mask;

	return 0;
}

static int cyapa_gen5_disable_pip_report(struct cyapa *cyapa)
{
	struct pip_app_cmd_head *app_cmd_head;
	u8 cmd[10];
	u8 resp_data[7];
	int resp_len;
	int error;

	memset(cmd, 0, sizeof(cmd));
	app_cmd_head = (struct pip_app_cmd_head *)cmd;

	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	put_unaligned_le16(sizeof(cmd) - 2, &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = GEN5_CMD_SET_PARAMETER;
	app_cmd_head->parameter_data[0] = GEN5_PARAMETER_DISABLE_PIP_REPORT;
	app_cmd_head->parameter_data[1] = 0x01;
	app_cmd_head->parameter_data[2] = 0x01;
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || resp_data[5] != GEN5_PARAMETER_DISABLE_PIP_REPORT ||
		!VALID_CMD_RESP_HEADER(resp_data, GEN5_CMD_SET_PARAMETER) ||
		resp_data[6] != 0x01)
		return error < 0 ? error : -EINVAL;

	return 0;
}

int cyapa_pip_set_proximity(struct cyapa *cyapa, bool enable)
{
	u8 cmd[] = { 0x04, 0x00, 0x06, 0x00, 0x2f, 0x00, PIP_SET_PROXIMITY,
		     (u8)!!enable
	};
	u8 resp_data[6];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, PIP_SET_PROXIMITY) ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data)) {
		error = (error == -ETIMEDOUT) ? -EOPNOTSUPP : error;
		return error < 0 ? error : -EINVAL;
	}

	return 0;
}

int cyapa_pip_deep_sleep(struct cyapa *cyapa, u8 state)
{
	u8 cmd[] = { 0x05, 0x00, 0x00, 0x08};
	u8 resp_data[5];
	int resp_len;
	int error;

	cmd[2] = state & PIP_DEEP_SLEEP_STATE_MASK;
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_pip_deep_sleep_data, false);
	if (error || ((resp_data[3] & PIP_DEEP_SLEEP_STATE_MASK) != state))
		return -EINVAL;

	return 0;
}

static int cyapa_gen5_set_power_mode(struct cyapa *cyapa,
		u8 power_mode, u16 sleep_time, enum cyapa_pm_stage pm_stage)
{
	struct device *dev = &cyapa->client->dev;
	u8 power_state;
	int error = 0;

	if (cyapa->state != CYAPA_STATE_GEN5_APP)
		return 0;

	cyapa_set_pip_pm_state(cyapa, pm_stage);

	if (PIP_DEV_GET_PWR_STATE(cyapa) == UNINIT_PWR_MODE) {
		/*
		 * Assume TP in deep sleep mode when driver is loaded,
		 * avoid driver unload and reload command IO issue caused by TP
		 * has been set into deep sleep mode when unloading.
		 */
		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_OFF);
	}

	if (PIP_DEV_UNINIT_SLEEP_TIME(cyapa) &&
			PIP_DEV_GET_PWR_STATE(cyapa) != PWR_MODE_OFF)
		if (cyapa_gen5_get_interval_time(cyapa,
				GEN5_PARAMETER_LP_INTRVL_ID,
				&cyapa->dev_sleep_time) != 0)
			PIP_DEV_SET_SLEEP_TIME(cyapa, UNINIT_SLEEP_TIME);

	if (PIP_DEV_GET_PWR_STATE(cyapa) == power_mode) {
		if (power_mode == PWR_MODE_OFF ||
			power_mode == PWR_MODE_FULL_ACTIVE ||
			power_mode == PWR_MODE_BTN_ONLY ||
			PIP_DEV_GET_SLEEP_TIME(cyapa) == sleep_time) {
			/* Has in correct power mode state, early return. */
			goto out;
		}
	}

	if (power_mode == PWR_MODE_OFF) {
		error = cyapa_pip_deep_sleep(cyapa, PIP_DEEP_SLEEP_STATE_OFF);
		if (error) {
			dev_err(dev, "enter deep sleep fail: %d\n", error);
			goto out;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_OFF);
		goto out;
	}

	/*
	 * When trackpad in power off mode, it cannot change to other power
	 * state directly, must be wake up from sleep firstly, then
	 * continue to do next power sate change.
	 */
	if (PIP_DEV_GET_PWR_STATE(cyapa) == PWR_MODE_OFF) {
		error = cyapa_pip_deep_sleep(cyapa, PIP_DEEP_SLEEP_STATE_ON);
		if (error) {
			dev_err(dev, "deep sleep wake fail: %d\n", error);
			goto out;
		}
	}

	if (power_mode == PWR_MODE_FULL_ACTIVE) {
		error = cyapa_gen5_change_power_state(cyapa,
				GEN5_POWER_STATE_ACTIVE);
		if (error) {
			dev_err(dev, "change to active fail: %d\n", error);
			goto out;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_FULL_ACTIVE);
	} else if (power_mode == PWR_MODE_BTN_ONLY) {
		error = cyapa_gen5_change_power_state(cyapa,
				GEN5_POWER_STATE_BTN_ONLY);
		if (error) {
			dev_err(dev, "fail to button only mode: %d\n", error);
			goto out;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_BTN_ONLY);
	} else {
		/*
		 * Continue to change power mode even failed to set
		 * interval time, it won't affect the power mode change.
		 * except the sleep interval time is not correct.
		 */
		if (PIP_DEV_UNINIT_SLEEP_TIME(cyapa) ||
				sleep_time != PIP_DEV_GET_SLEEP_TIME(cyapa))
			if (cyapa_gen5_set_interval_time(cyapa,
					GEN5_PARAMETER_LP_INTRVL_ID,
					sleep_time) == 0)
				PIP_DEV_SET_SLEEP_TIME(cyapa, sleep_time);

		if (sleep_time <= GEN5_POWER_READY_MAX_INTRVL_TIME)
			power_state = GEN5_POWER_STATE_READY;
		else
			power_state = GEN5_POWER_STATE_IDLE;
		error = cyapa_gen5_change_power_state(cyapa, power_state);
		if (error) {
			dev_err(dev, "set power state to 0x%02x failed: %d\n",
				power_state, error);
			goto out;
		}

		/*
		 * Disable pip report for a little time, firmware will
		 * re-enable it automatically. It's used to fix the issue
		 * that trackpad unable to report signal to wake system up
		 * in the special situation that system is in suspending, and
		 * at the same time, user touch trackpad to wake system up.
		 * This function can avoid the data to be buffered when system
		 * is suspending which may cause interrupt line unable to be
		 * asserted again.
		 */
		if (pm_stage == CYAPA_PM_SUSPEND)
			cyapa_gen5_disable_pip_report(cyapa);

		PIP_DEV_SET_PWR_STATE(cyapa,
			cyapa_sleep_time_to_pwr_cmd(sleep_time));
	}

out:
	cyapa_reset_pip_pm_state(cyapa);
	return error;
}

int cyapa_pip_resume_scanning(struct cyapa *cyapa)
{
	u8 cmd[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00, 0x04 };
	u8 resp_data[6];
	int resp_len;
	int error;

	/* Try to dump all buffered data before doing command. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, true);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, 0x04))
		return -EINVAL;

	/* Try to dump all buffered data when resuming scanning. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	return 0;
}

int cyapa_pip_suspend_scanning(struct cyapa *cyapa)
{
	u8 cmd[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00, 0x03 };
	u8 resp_data[6];
	int resp_len;
	int error;

	/* Try to dump all buffered data before doing command. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, true);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, 0x03))
		return -EINVAL;

	/* Try to dump all buffered data when suspending scanning. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	return 0;
}

static int cyapa_pip_calibrate_pwcs(struct cyapa *cyapa,
		u8 calibrate_sensing_mode_type)
{
	struct pip_app_cmd_head *app_cmd_head;
	u8 cmd[8];
	u8 resp_data[6];
	int resp_len;
	int error;

	/* Try to dump all buffered data before doing command. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	memset(cmd, 0, sizeof(cmd));
	app_cmd_head = (struct pip_app_cmd_head *)cmd;
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	put_unaligned_le16(sizeof(cmd) - 2, &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = PIP_CMD_CALIBRATE;
	app_cmd_head->parameter_data[0] = calibrate_sensing_mode_type;
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			5000, cyapa_sort_tsg_pip_app_resp_data, true);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, PIP_CMD_CALIBRATE) ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error < 0 ? error : -EAGAIN;

	return 0;
}

ssize_t cyapa_pip_do_calibrate(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct cyapa *cyapa = dev_get_drvdata(dev);
	int error, calibrate_error;

	/* 1. Suspend Scanning*/
	error = cyapa_pip_suspend_scanning(cyapa);
	if (error)
		return error;

	/* 2. Do mutual capacitance fine calibrate. */
	calibrate_error = cyapa_pip_calibrate_pwcs(cyapa,
				PIP_SENSING_MODE_MUTUAL_CAP_FINE);
	if (calibrate_error)
		goto resume_scanning;

	/* 3. Do self capacitance calibrate. */
	calibrate_error = cyapa_pip_calibrate_pwcs(cyapa,
				PIP_SENSING_MODE_SELF_CAP);
	if (calibrate_error)
		goto resume_scanning;

resume_scanning:
	/* 4. Resume Scanning*/
	error = cyapa_pip_resume_scanning(cyapa);
	if (error || calibrate_error)
		return error ? error : calibrate_error;

	return count;
}

static s32 twos_complement_to_s32(s32 value, int num_bits)
{
	if (value >> (num_bits - 1))
		value |=  -1 << num_bits;
	return value;
}

static s32 cyapa_parse_structure_data(u8 data_format, u8 *buf, int buf_len)
{
	int data_size;
	bool big_endian;
	bool unsigned_type;
	s32 value;

	data_size = (data_format & 0x07);
	big_endian = ((data_format & 0x10) == 0x00);
	unsigned_type = ((data_format & 0x20) == 0x00);

	if (buf_len < data_size)
		return 0;

	switch (data_size) {
	case 1:
		value  = buf[0];
		break;
	case 2:
		if (big_endian)
			value = get_unaligned_be16(buf);
		else
			value = get_unaligned_le16(buf);
		break;
	case 4:
		if (big_endian)
			value = get_unaligned_be32(buf);
		else
			value = get_unaligned_le32(buf);
		break;
	default:
		/* Should not happen, just as default case here. */
		value = 0;
		break;
	}

	if (!unsigned_type)
		value = twos_complement_to_s32(value, data_size * 8);

	return value;
}

static void cyapa_gen5_guess_electrodes(struct cyapa *cyapa,
		int *electrodes_rx, int *electrodes_tx)
{
	if (cyapa->electrodes_rx != 0) {
		*electrodes_rx = cyapa->electrodes_rx;
		*electrodes_tx = (cyapa->electrodes_x == *electrodes_rx) ?
				cyapa->electrodes_y : cyapa->electrodes_x;
	} else {
		*electrodes_tx = min(cyapa->electrodes_x, cyapa->electrodes_y);
		*electrodes_rx = max(cyapa->electrodes_x, cyapa->electrodes_y);
	}
}

/*
 * Read all the global mutual or self idac data or mutual or self local PWC
 * data based on the @idac_data_type.
 * If the input value of @data_size is 0, then means read global mutual or
 * self idac data. For read global mutual idac data, @idac_max, @idac_min and
 * @idac_ave are in order used to return the max value of global mutual idac
 * data, the min value of global mutual idac and the average value of the
 * global mutual idac data. For read global self idac data, @idac_max is used
 * to return the global self cap idac data in Rx direction, @idac_min is used
 * to return the global self cap idac data in Tx direction. @idac_ave is not
 * used.
 * If the input value of @data_size is not 0, than means read the mutual or
 * self local PWC data. The @idac_max, @idac_min and @idac_ave are used to
 * return the max, min and average value of the mutual or self local PWC data.
 * Note, in order to read mutual local PWC data, must read invoke this function
 * to read the mutual global idac data firstly to set the correct Rx number
 * value, otherwise, the read mutual idac and PWC data may not correct.
 */
static int cyapa_gen5_read_idac_data(struct cyapa *cyapa,
		u8 cmd_code, u8 idac_data_type, int *data_size,
		int *idac_max, int *idac_min, int *idac_ave)
{
	struct pip_app_cmd_head *cmd_head;
	u8 cmd[12];
	u8 resp_data[256];
	int resp_len;
	int read_len;
	int value;
	u16 offset;
	int read_elements;
	bool read_global_idac;
	int sum, count, max_element_cnt;
	int tmp_max, tmp_min, tmp_ave, tmp_sum, tmp_count;
	int electrodes_rx, electrodes_tx;
	int i;
	int error;

	if (cmd_code != PIP_RETRIEVE_DATA_STRUCTURE ||
		(idac_data_type != GEN5_RETRIEVE_MUTUAL_PWC_DATA &&
		idac_data_type != GEN5_RETRIEVE_SELF_CAP_PWC_DATA) ||
		!data_size || !idac_max || !idac_min || !idac_ave)
		return -EINVAL;

	*idac_max = INT_MIN;
	*idac_min = INT_MAX;
	sum = count = tmp_count = 0;
	electrodes_rx = electrodes_tx = 0;
	if (*data_size == 0) {
		/*
		 * Read global idac values firstly.
		 * Currently, no idac data exceed 4 bytes.
		 */
		read_global_idac = true;
		offset = 0;
		*data_size = 4;
		tmp_max = INT_MIN;
		tmp_min = INT_MAX;
		tmp_ave = tmp_sum = tmp_count = 0;

		if (idac_data_type == GEN5_RETRIEVE_MUTUAL_PWC_DATA) {
			if (cyapa->aligned_electrodes_rx == 0) {
				cyapa_gen5_guess_electrodes(cyapa,
					&electrodes_rx, &electrodes_tx);
				cyapa->aligned_electrodes_rx =
					(electrodes_rx + 3) & ~3u;
			}
			max_element_cnt =
				(cyapa->aligned_electrodes_rx + 7) & ~7u;
		} else {
			max_element_cnt = 2;
		}
	} else {
		read_global_idac = false;
		if (*data_size > 4)
			*data_size = 4;
		/* Calculate the start offset in bytes of local PWC data. */
		if (idac_data_type == GEN5_RETRIEVE_MUTUAL_PWC_DATA) {
			offset = cyapa->aligned_electrodes_rx * (*data_size);
			if (cyapa->electrodes_rx == cyapa->electrodes_x)
				electrodes_tx = cyapa->electrodes_y;
			else
				electrodes_tx = cyapa->electrodes_x;
			max_element_cnt = ((cyapa->aligned_electrodes_rx + 7) &
						~7u) * electrodes_tx;
		} else {
			offset = 2;
			max_element_cnt = cyapa->electrodes_x +
						cyapa->electrodes_y;
			max_element_cnt = (max_element_cnt + 3) & ~3u;
		}
	}

	memset(cmd, 0, sizeof(cmd));
	cmd_head = (struct pip_app_cmd_head *)cmd;
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &cmd_head->addr);
	put_unaligned_le16(sizeof(cmd) - 2, &cmd_head->length);
	cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	cmd_head->cmd_code = cmd_code;
	do {
		read_elements = (256 - GEN5_RESP_DATA_STRUCTURE_OFFSET) /
				(*data_size);
		read_elements = min(read_elements, max_element_cnt - count);
		read_len = read_elements * (*data_size);

		put_unaligned_le16(offset, &cmd_head->parameter_data[0]);
		put_unaligned_le16(read_len, &cmd_head->parameter_data[2]);
		cmd_head->parameter_data[4] = idac_data_type;
		resp_len = GEN5_RESP_DATA_STRUCTURE_OFFSET + read_len;
		error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
				cmd, sizeof(cmd),
				resp_data, &resp_len,
				500, cyapa_sort_tsg_pip_app_resp_data,
				true);
		if (error || resp_len < GEN5_RESP_DATA_STRUCTURE_OFFSET ||
				!VALID_CMD_RESP_HEADER(resp_data, cmd_code) ||
				!PIP_CMD_COMPLETE_SUCCESS(resp_data) ||
				resp_data[6] != idac_data_type)
			return (error < 0) ? error : -EAGAIN;
		read_len = get_unaligned_le16(&resp_data[7]);
		if (read_len == 0)
			break;

		*data_size = (resp_data[9] & GEN5_PWC_DATA_ELEMENT_SIZE_MASK);
		if (read_len < *data_size)
			return -EINVAL;

		if (read_global_idac &&
			idac_data_type == GEN5_RETRIEVE_SELF_CAP_PWC_DATA) {
			/* Rx's self global idac data. */
			*idac_max = cyapa_parse_structure_data(
				resp_data[9],
				&resp_data[GEN5_RESP_DATA_STRUCTURE_OFFSET],
				*data_size);
			/* Tx's self global idac data. */
			*idac_min = cyapa_parse_structure_data(
				resp_data[9],
				&resp_data[GEN5_RESP_DATA_STRUCTURE_OFFSET +
					   *data_size],
				*data_size);
			break;
		}

		/* Read mutual global idac or local mutual/self PWC data. */
		offset += read_len;
		for (i = 10; i < (read_len + GEN5_RESP_DATA_STRUCTURE_OFFSET);
				i += *data_size) {
			value = cyapa_parse_structure_data(resp_data[9],
					&resp_data[i], *data_size);
			*idac_min = min(value, *idac_min);
			*idac_max = max(value, *idac_max);

			if (idac_data_type == GEN5_RETRIEVE_MUTUAL_PWC_DATA &&
				tmp_count < cyapa->aligned_electrodes_rx &&
				read_global_idac) {
				/*
				 * The value gap between global and local mutual
				 * idac data must bigger than 50%.
				 * Normally, global value bigger than 50,
				 * local values less than 10.
				 */
				if (!tmp_ave || value > tmp_ave / 2) {
					tmp_min = min(value, tmp_min);
					tmp_max = max(value, tmp_max);
					tmp_sum += value;
					tmp_count++;

					tmp_ave = tmp_sum / tmp_count;
				}
			}

			sum += value;
			count++;

			if (count >= max_element_cnt)
				goto out;
		}
	} while (true);

out:
	*idac_ave = count ? (sum / count) : 0;

	if (read_global_idac &&
		idac_data_type == GEN5_RETRIEVE_MUTUAL_PWC_DATA) {
		if (tmp_count == 0)
			return 0;

		if (tmp_count == cyapa->aligned_electrodes_rx) {
			cyapa->electrodes_rx = cyapa->electrodes_rx ?
				cyapa->electrodes_rx : electrodes_rx;
		} else if (tmp_count == electrodes_rx) {
			cyapa->electrodes_rx = cyapa->electrodes_rx ?
				cyapa->electrodes_rx : electrodes_rx;
			cyapa->aligned_electrodes_rx = electrodes_rx;
		} else {
			cyapa->electrodes_rx = cyapa->electrodes_rx ?
				cyapa->electrodes_rx : electrodes_tx;
			cyapa->aligned_electrodes_rx = tmp_count;
		}

		*idac_min = tmp_min;
		*idac_max = tmp_max;
		*idac_ave = tmp_ave;
	}

	return 0;
}

static int cyapa_gen5_read_mutual_idac_data(struct cyapa *cyapa,
	int *gidac_mutual_max, int *gidac_mutual_min, int *gidac_mutual_ave,
	int *lidac_mutual_max, int *lidac_mutual_min, int *lidac_mutual_ave)
{
	int data_size;
	int error;

	*gidac_mutual_max = *gidac_mutual_min = *gidac_mutual_ave = 0;
	*lidac_mutual_max = *lidac_mutual_min = *lidac_mutual_ave = 0;

	data_size = 0;
	error = cyapa_gen5_read_idac_data(cyapa,
		PIP_RETRIEVE_DATA_STRUCTURE,
		GEN5_RETRIEVE_MUTUAL_PWC_DATA,
		&data_size,
		gidac_mutual_max, gidac_mutual_min, gidac_mutual_ave);
	if (error)
		return error;

	error = cyapa_gen5_read_idac_data(cyapa,
		PIP_RETRIEVE_DATA_STRUCTURE,
		GEN5_RETRIEVE_MUTUAL_PWC_DATA,
		&data_size,
		lidac_mutual_max, lidac_mutual_min, lidac_mutual_ave);
	return error;
}

static int cyapa_gen5_read_self_idac_data(struct cyapa *cyapa,
		int *gidac_self_rx, int *gidac_self_tx,
		int *lidac_self_max, int *lidac_self_min, int *lidac_self_ave)
{
	int data_size;
	int error;

	*gidac_self_rx = *gidac_self_tx = 0;
	*lidac_self_max = *lidac_self_min = *lidac_self_ave = 0;

	data_size = 0;
	error = cyapa_gen5_read_idac_data(cyapa,
		PIP_RETRIEVE_DATA_STRUCTURE,
		GEN5_RETRIEVE_SELF_CAP_PWC_DATA,
		&data_size,
		lidac_self_max, lidac_self_min, lidac_self_ave);
	if (error)
		return error;
	*gidac_self_rx = *lidac_self_max;
	*gidac_self_tx = *lidac_self_min;

	error = cyapa_gen5_read_idac_data(cyapa,
		PIP_RETRIEVE_DATA_STRUCTURE,
		GEN5_RETRIEVE_SELF_CAP_PWC_DATA,
		&data_size,
		lidac_self_max, lidac_self_min, lidac_self_ave);
	return error;
}

static ssize_t cyapa_gen5_execute_panel_scan(struct cyapa *cyapa)
{
	struct pip_app_cmd_head *app_cmd_head;
	u8 cmd[7];
	u8 resp_data[6];
	int resp_len;
	int error;

	memset(cmd, 0, sizeof(cmd));
	app_cmd_head = (struct pip_app_cmd_head *)cmd;
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	put_unaligned_le16(sizeof(cmd) - 2, &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = GEN5_CMD_EXECUTE_PANEL_SCAN;
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, true);
	if (error || resp_len != sizeof(resp_data) ||
			!VALID_CMD_RESP_HEADER(resp_data,
				GEN5_CMD_EXECUTE_PANEL_SCAN) ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error ? error : -EAGAIN;

	return 0;
}

static int cyapa_gen5_read_panel_scan_raw_data(struct cyapa *cyapa,
		u8 cmd_code, u8 raw_data_type, int raw_data_max_num,
		int *raw_data_max, int *raw_data_min, int *raw_data_ave,
		u8 *buffer)
{
	struct pip_app_cmd_head *app_cmd_head;
	struct gen5_retrieve_panel_scan_data *panel_sacn_data;
	u8 cmd[12];
	u8 resp_data[256];  /* Max bytes can transfer one time. */
	int resp_len;
	int read_elements;
	int read_len;
	u16 offset;
	s32 value;
	int sum, count;
	int data_size;
	s32 *intp;
	int i;
	int error;

	if (cmd_code != GEN5_CMD_RETRIEVE_PANEL_SCAN ||
		(raw_data_type > GEN5_PANEL_SCAN_SELF_DIFFCOUNT) ||
		!raw_data_max || !raw_data_min || !raw_data_ave)
		return -EINVAL;

	intp = (s32 *)buffer;
	*raw_data_max = INT_MIN;
	*raw_data_min = INT_MAX;
	sum = count = 0;
	offset = 0;
	/* Assume max element size is 4 currently. */
	read_elements = (256 - GEN5_RESP_DATA_STRUCTURE_OFFSET) / 4;
	read_len = read_elements * 4;
	app_cmd_head = (struct pip_app_cmd_head *)cmd;
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &app_cmd_head->addr);
	put_unaligned_le16(sizeof(cmd) - 2, &app_cmd_head->length);
	app_cmd_head->report_id = PIP_APP_CMD_REPORT_ID;
	app_cmd_head->cmd_code = cmd_code;
	panel_sacn_data = (struct gen5_retrieve_panel_scan_data *)
			app_cmd_head->parameter_data;
	do {
		put_unaligned_le16(offset, &panel_sacn_data->read_offset);
		put_unaligned_le16(read_elements,
			&panel_sacn_data->read_elements);
		panel_sacn_data->data_id = raw_data_type;

		resp_len = GEN5_RESP_DATA_STRUCTURE_OFFSET + read_len;
		error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, true);
		if (error || resp_len < GEN5_RESP_DATA_STRUCTURE_OFFSET ||
				!VALID_CMD_RESP_HEADER(resp_data, cmd_code) ||
				!PIP_CMD_COMPLETE_SUCCESS(resp_data) ||
				resp_data[6] != raw_data_type)
			return error ? error : -EAGAIN;

		read_elements = get_unaligned_le16(&resp_data[7]);
		if (read_elements == 0)
			break;

		data_size = (resp_data[9] & GEN5_PWC_DATA_ELEMENT_SIZE_MASK);
		offset += read_elements;
		if (read_elements) {
			for (i = GEN5_RESP_DATA_STRUCTURE_OFFSET;
			     i < (read_elements * data_size +
					GEN5_RESP_DATA_STRUCTURE_OFFSET);
			     i += data_size) {
				value = cyapa_parse_structure_data(resp_data[9],
						&resp_data[i], data_size);
				*raw_data_min = min(value, *raw_data_min);
				*raw_data_max = max(value, *raw_data_max);

				if (intp)
					put_unaligned_le32(value, &intp[count]);

				sum += value;
				count++;

			}
		}

		if (count >= raw_data_max_num)
			break;

		read_elements = (sizeof(resp_data) -
				GEN5_RESP_DATA_STRUCTURE_OFFSET) / data_size;
		read_len = read_elements * data_size;
	} while (true);

	*raw_data_ave = count ? (sum / count) : 0;

	return 0;
}

static ssize_t cyapa_gen5_show_baseline(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct cyapa *cyapa = dev_get_drvdata(dev);
	int gidac_mutual_max, gidac_mutual_min, gidac_mutual_ave;
	int lidac_mutual_max, lidac_mutual_min, lidac_mutual_ave;
	int gidac_self_rx, gidac_self_tx;
	int lidac_self_max, lidac_self_min, lidac_self_ave;
	int raw_cap_mutual_max, raw_cap_mutual_min, raw_cap_mutual_ave;
	int raw_cap_self_max, raw_cap_self_min, raw_cap_self_ave;
	int mutual_diffdata_max, mutual_diffdata_min, mutual_diffdata_ave;
	int self_diffdata_max, self_diffdata_min, self_diffdata_ave;
	int mutual_baseline_max, mutual_baseline_min, mutual_baseline_ave;
	int self_baseline_max, self_baseline_min, self_baseline_ave;
	int error, resume_error;
	int size;

	if (!cyapa_is_pip_app_mode(cyapa))
		return -EBUSY;

	/* 1. Suspend Scanning*/
	error = cyapa_pip_suspend_scanning(cyapa);
	if (error)
		return error;

	/* 2.  Read global and local mutual IDAC data. */
	gidac_self_rx = gidac_self_tx = 0;
	error = cyapa_gen5_read_mutual_idac_data(cyapa,
				&gidac_mutual_max, &gidac_mutual_min,
				&gidac_mutual_ave, &lidac_mutual_max,
				&lidac_mutual_min, &lidac_mutual_ave);
	if (error)
		goto resume_scanning;

	/* 3.  Read global and local self IDAC data. */
	error = cyapa_gen5_read_self_idac_data(cyapa,
				&gidac_self_rx, &gidac_self_tx,
				&lidac_self_max, &lidac_self_min,
				&lidac_self_ave);
	if (error)
		goto resume_scanning;

	/* 4. Execute panel scan. It must be executed before read data. */
	error = cyapa_gen5_execute_panel_scan(cyapa);
	if (error)
		goto resume_scanning;

	/* 5. Retrieve panel scan, mutual cap raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_MUTUAL_RAW_DATA,
				cyapa->electrodes_x * cyapa->electrodes_y,
				&raw_cap_mutual_max, &raw_cap_mutual_min,
				&raw_cap_mutual_ave,
				NULL);
	if (error)
		goto resume_scanning;

	/* 6. Retrieve panel scan, self cap raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_SELF_RAW_DATA,
				cyapa->electrodes_x + cyapa->electrodes_y,
				&raw_cap_self_max, &raw_cap_self_min,
				&raw_cap_self_ave,
				NULL);
	if (error)
		goto resume_scanning;

	/* 7. Retrieve panel scan, mutual cap diffcount raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_MUTUAL_DIFFCOUNT,
				cyapa->electrodes_x * cyapa->electrodes_y,
				&mutual_diffdata_max, &mutual_diffdata_min,
				&mutual_diffdata_ave,
				NULL);
	if (error)
		goto resume_scanning;

	/* 8. Retrieve panel scan, self cap diffcount raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_SELF_DIFFCOUNT,
				cyapa->electrodes_x + cyapa->electrodes_y,
				&self_diffdata_max, &self_diffdata_min,
				&self_diffdata_ave,
				NULL);
	if (error)
		goto resume_scanning;

	/* 9. Retrieve panel scan, mutual cap baseline raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_MUTUAL_BASELINE,
				cyapa->electrodes_x * cyapa->electrodes_y,
				&mutual_baseline_max, &mutual_baseline_min,
				&mutual_baseline_ave,
				NULL);
	if (error)
		goto resume_scanning;

	/* 10. Retrieve panel scan, self cap baseline raw data. */
	error = cyapa_gen5_read_panel_scan_raw_data(cyapa,
				GEN5_CMD_RETRIEVE_PANEL_SCAN,
				GEN5_PANEL_SCAN_SELF_BASELINE,
				cyapa->electrodes_x + cyapa->electrodes_y,
				&self_baseline_max, &self_baseline_min,
				&self_baseline_ave,
				NULL);
	if (error)
		goto resume_scanning;

resume_scanning:
	/* 11. Resume Scanning*/
	resume_error = cyapa_pip_resume_scanning(cyapa);
	if (resume_error || error)
		return resume_error ? resume_error : error;

	/* 12. Output data strings */
	size = scnprintf(buf, PAGE_SIZE, "%d %d %d %d %d %d %d %d %d %d %d ",
		gidac_mutual_min, gidac_mutual_max, gidac_mutual_ave,
		lidac_mutual_min, lidac_mutual_max, lidac_mutual_ave,
		gidac_self_rx, gidac_self_tx,
		lidac_self_min, lidac_self_max, lidac_self_ave);
	size += scnprintf(buf + size, PAGE_SIZE - size,
		"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
		raw_cap_mutual_min, raw_cap_mutual_max, raw_cap_mutual_ave,
		raw_cap_self_min, raw_cap_self_max, raw_cap_self_ave,
		mutual_diffdata_min, mutual_diffdata_max, mutual_diffdata_ave,
		self_diffdata_min, self_diffdata_max, self_diffdata_ave,
		mutual_baseline_min, mutual_baseline_max, mutual_baseline_ave,
		self_baseline_min, self_baseline_max, self_baseline_ave);
	return size;
}

bool cyapa_pip_sort_system_info_data(struct cyapa *cyapa,
		u8 *buf, int len)
{
	/* Check the report id and command code */
	if (VALID_CMD_RESP_HEADER(buf, 0x02))
		return true;

	return false;
}

static int cyapa_gen5_bl_query_data(struct cyapa *cyapa)
{
	u8 resp_data[PIP_BL_APP_INFO_RESP_LENGTH];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			pip_bl_read_app_info, PIP_BL_READ_APP_INFO_CMD_LENGTH,
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_bl_resp_data, false);
	if (error || resp_len < PIP_BL_APP_INFO_RESP_LENGTH ||
		!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error ? error : -EIO;

	memcpy(&cyapa->product_id[0], &resp_data[8], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &resp_data[13], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &resp_data[19], 2);
	cyapa->product_id[15] = '\0';

	cyapa->fw_maj_ver = resp_data[22];
	cyapa->fw_min_ver = resp_data[23];

	cyapa->platform_ver = (resp_data[26] >> PIP_BL_PLATFORM_VER_SHIFT) &
			      PIP_BL_PLATFORM_VER_MASK;

	return 0;
}

static int cyapa_gen5_get_query_data(struct cyapa *cyapa)
{
	u8 resp_data[PIP_READ_SYS_INFO_RESP_LENGTH];
	int resp_len;
	u16 product_family;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			pip_read_sys_info, PIP_READ_SYS_INFO_CMD_LENGTH,
			resp_data, &resp_len,
			2000, cyapa_pip_sort_system_info_data, false);
	if (error || resp_len < sizeof(resp_data))
		return error ? error : -EIO;

	product_family = get_unaligned_le16(&resp_data[7]);
	if ((product_family & PIP_PRODUCT_FAMILY_MASK) !=
		PIP_PRODUCT_FAMILY_TRACKPAD)
		return -EINVAL;

	cyapa->platform_ver = (resp_data[49] >> PIP_BL_PLATFORM_VER_SHIFT) &
			      PIP_BL_PLATFORM_VER_MASK;
	if (cyapa->gen == CYAPA_GEN5 && cyapa->platform_ver < 2) {
		/* Gen5 firmware that does not support proximity. */
		cyapa->fw_maj_ver = resp_data[15];
		cyapa->fw_min_ver = resp_data[16];
	} else {
		cyapa->fw_maj_ver = resp_data[9];
		cyapa->fw_min_ver = resp_data[10];
	}

	cyapa->electrodes_x = resp_data[52];
	cyapa->electrodes_y = resp_data[53];

	cyapa->physical_size_x =  get_unaligned_le16(&resp_data[54]) / 100;
	cyapa->physical_size_y = get_unaligned_le16(&resp_data[56]) / 100;

	cyapa->max_abs_x = get_unaligned_le16(&resp_data[58]);
	cyapa->max_abs_y = get_unaligned_le16(&resp_data[60]);

	cyapa->max_z = get_unaligned_le16(&resp_data[62]);

	cyapa->x_origin = resp_data[64] & 0x01;
	cyapa->y_origin = resp_data[65] & 0x01;

	cyapa->btn_capability = (resp_data[70] << 3) & CAPABILITY_BTN_MASK;

	memcpy(&cyapa->product_id[0], &resp_data[33], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &resp_data[38], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &resp_data[44], 2);
	cyapa->product_id[15] = '\0';

	if (!cyapa->electrodes_x || !cyapa->electrodes_y ||
		!cyapa->physical_size_x || !cyapa->physical_size_y ||
		!cyapa->max_abs_x || !cyapa->max_abs_y || !cyapa->max_z)
		return -EINVAL;

	return 0;
}

static int cyapa_gen5_do_operational_check(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	int error;

	if (cyapa->gen != CYAPA_GEN5)
		return -ENODEV;

	switch (cyapa->state) {
	case CYAPA_STATE_GEN5_BL:
		error = cyapa_pip_bl_exit(cyapa);
		if (error) {
			/* Try to update trackpad product information. */
			cyapa_gen5_bl_query_data(cyapa);
			goto out;
		}

		cyapa->state = CYAPA_STATE_GEN5_APP;
		fallthrough;

	case CYAPA_STATE_GEN5_APP:
		/*
		 * If trackpad device in deep sleep mode,
		 * the app command will fail.
		 * So always try to reset trackpad device to full active when
		 * the device state is required.
		 */
		error = cyapa_gen5_set_power_mode(cyapa,
				PWR_MODE_FULL_ACTIVE, 0, CYAPA_PM_ACTIVE);
		if (error)
			dev_warn(dev, "%s: failed to set power active mode.\n",
				__func__);

		/* By default, the trackpad proximity function is enabled. */
		if (cyapa->platform_ver >= 2) {
			error = cyapa_pip_set_proximity(cyapa, true);
			if (error)
				dev_warn(dev,
					"%s: failed to enable proximity.\n",
					__func__);
		}

		/* Get trackpad product information. */
		error = cyapa_gen5_get_query_data(cyapa);
		if (error)
			goto out;
		/* Only support product ID starting with CYTRA */
		if (memcmp(cyapa->product_id, product_id,
				strlen(product_id)) != 0) {
			dev_err(dev, "%s: unknown product ID (%s)\n",
				__func__, cyapa->product_id);
			error = -EINVAL;
		}
		break;
	default:
		error = -EINVAL;
	}

out:
	return error;
}

/*
 * Return false, do not continue process
 * Return true, continue process.
 */
bool cyapa_pip_irq_cmd_handler(struct cyapa *cyapa)
{
	struct cyapa_pip_cmd_states *pip = &cyapa->cmd_states.pip;
	int length;

	if (atomic_read(&pip->cmd_issued)) {
		/* Polling command response data. */
		if (pip->is_irq_mode == false)
			return false;

		/*
		 * Read out all none command response data.
		 * these output data may caused by user put finger on
		 * trackpad when host waiting the command response.
		 */
		cyapa_i2c_pip_read(cyapa, pip->irq_cmd_buf,
			PIP_RESP_LENGTH_SIZE);
		length = get_unaligned_le16(pip->irq_cmd_buf);
		length = (length <= PIP_RESP_LENGTH_SIZE) ?
				PIP_RESP_LENGTH_SIZE : length;
		if (length > PIP_RESP_LENGTH_SIZE)
			cyapa_i2c_pip_read(cyapa,
				pip->irq_cmd_buf, length);
		if (!(pip->resp_sort_func &&
			pip->resp_sort_func(cyapa,
				pip->irq_cmd_buf, length))) {
			/*
			 * Cover the Gen5 V1 firmware issue.
			 * The issue is no interrupt would be asserted from
			 * trackpad device to host for the command response
			 * ready event. Because when there was a finger touch
			 * on trackpad device, and the firmware output queue
			 * won't be empty (always with touch report data), so
			 * the interrupt signal won't be asserted again until
			 * the output queue was previous emptied.
			 * This issue would happen in the scenario that
			 * user always has his/her fingers touched on the
			 * trackpad device during system booting/rebooting.
			 */
			length = 0;
			if (pip->resp_len)
				length = *pip->resp_len;
			cyapa_empty_pip_output_data(cyapa,
					pip->resp_data,
					&length,
					pip->resp_sort_func);
			if (pip->resp_len && length != 0) {
				*pip->resp_len = length;
				atomic_dec(&pip->cmd_issued);
				complete(&pip->cmd_ready);
			}
			return false;
		}

		if (pip->resp_data && pip->resp_len) {
			*pip->resp_len = (*pip->resp_len < length) ?
				*pip->resp_len : length;
			memcpy(pip->resp_data, pip->irq_cmd_buf,
				*pip->resp_len);
		}
		atomic_dec(&pip->cmd_issued);
		complete(&pip->cmd_ready);
		return false;
	}

	return true;
}

static void cyapa_pip_report_buttons(struct cyapa *cyapa,
		const struct cyapa_pip_report_data *report_data)
{
	struct input_dev *input = cyapa->input;
	u8 buttons = report_data->report_head[PIP_BUTTONS_OFFSET];

	buttons = (buttons << CAPABILITY_BTN_SHIFT) & CAPABILITY_BTN_MASK;

	if (cyapa->btn_capability & CAPABILITY_LEFT_BTN_MASK) {
		input_report_key(input, BTN_LEFT,
			!!(buttons & CAPABILITY_LEFT_BTN_MASK));
	}
	if (cyapa->btn_capability & CAPABILITY_MIDDLE_BTN_MASK) {
		input_report_key(input, BTN_MIDDLE,
			!!(buttons & CAPABILITY_MIDDLE_BTN_MASK));
	}
	if (cyapa->btn_capability & CAPABILITY_RIGHT_BTN_MASK) {
		input_report_key(input, BTN_RIGHT,
			!!(buttons & CAPABILITY_RIGHT_BTN_MASK));
	}

	input_sync(input);
}

static void cyapa_pip_report_proximity(struct cyapa *cyapa,
		const struct cyapa_pip_report_data *report_data)
{
	struct input_dev *input = cyapa->input;
	u8 distance = report_data->report_head[PIP_PROXIMITY_DISTANCE_OFFSET] &
			PIP_PROXIMITY_DISTANCE_MASK;

	input_report_abs(input, ABS_DISTANCE, distance);
	input_sync(input);
}

static void cyapa_pip_report_slot_data(struct cyapa *cyapa,
		const struct cyapa_pip_touch_record *touch)
{
	struct input_dev *input = cyapa->input;
	u8 event_id = PIP_GET_EVENT_ID(touch->touch_tip_event_id);
	int slot = PIP_GET_TOUCH_ID(touch->touch_tip_event_id);
	int x, y;

	if (event_id == RECORD_EVENT_LIFTOFF)
		return;

	input_mt_slot(input, slot);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
	x = (touch->x_hi << 8) | touch->x_lo;
	if (cyapa->x_origin)
		x = cyapa->max_abs_x - x;
	y = (touch->y_hi << 8) | touch->y_lo;
	if (cyapa->y_origin)
		y = cyapa->max_abs_y - y;
	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);
	input_report_abs(input, ABS_DISTANCE, 0);
	input_report_abs(input, ABS_MT_PRESSURE,
		touch->z);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR,
		touch->major_axis_len);
	input_report_abs(input, ABS_MT_TOUCH_MINOR,
		touch->minor_axis_len);

	input_report_abs(input, ABS_MT_WIDTH_MAJOR,
		touch->major_tool_len);
	input_report_abs(input, ABS_MT_WIDTH_MINOR,
		touch->minor_tool_len);

	input_report_abs(input, ABS_MT_ORIENTATION,
		touch->orientation);
}

static void cyapa_pip_report_touches(struct cyapa *cyapa,
		const struct cyapa_pip_report_data *report_data)
{
	struct input_dev *input = cyapa->input;
	unsigned int touch_num;
	int i;

	touch_num = report_data->report_head[PIP_NUMBER_OF_TOUCH_OFFSET] &
			PIP_NUMBER_OF_TOUCH_MASK;

	for (i = 0; i < touch_num; i++)
		cyapa_pip_report_slot_data(cyapa,
			&report_data->touch_records[i]);

	input_mt_sync_frame(input);
	input_sync(input);
}

int cyapa_pip_irq_handler(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	struct cyapa_pip_report_data report_data;
	unsigned int report_len;
	int ret;

	if (!cyapa_is_pip_app_mode(cyapa)) {
		dev_err(dev, "invalid device state, gen=%d, state=0x%02x\n",
			cyapa->gen, cyapa->state);
		return -EINVAL;
	}

	ret = cyapa_i2c_pip_read(cyapa, (u8 *)&report_data,
			PIP_RESP_LENGTH_SIZE);
	if (ret != PIP_RESP_LENGTH_SIZE) {
		dev_err(dev, "failed to read length bytes, (%d)\n", ret);
		return -EINVAL;
	}

	report_len = get_unaligned_le16(
			&report_data.report_head[PIP_RESP_LENGTH_OFFSET]);
	if (report_len < PIP_RESP_LENGTH_SIZE) {
		/* Invalid length or internal reset happened. */
		dev_err(dev, "invalid report_len=%d. bytes: %02x %02x\n",
			report_len, report_data.report_head[0],
			report_data.report_head[1]);
		return -EINVAL;
	}

	/* Idle, no data for report. */
	if (report_len == PIP_RESP_LENGTH_SIZE)
		return 0;

	ret = cyapa_i2c_pip_read(cyapa, (u8 *)&report_data, report_len);
	if (ret != report_len) {
		dev_err(dev, "failed to read %d bytes report data, (%d)\n",
			report_len, ret);
		return -EINVAL;
	}

	return cyapa_pip_event_process(cyapa, &report_data);
}

static int cyapa_pip_event_process(struct cyapa *cyapa,
				   struct cyapa_pip_report_data *report_data)
{
	struct device *dev = &cyapa->client->dev;
	unsigned int report_len;
	u8 report_id;

	report_len = get_unaligned_le16(
			&report_data->report_head[PIP_RESP_LENGTH_OFFSET]);
	/* Idle, no data for report. */
	if (report_len == PIP_RESP_LENGTH_SIZE)
		return 0;

	report_id = report_data->report_head[PIP_RESP_REPORT_ID_OFFSET];
	if (report_id == PIP_WAKEUP_EVENT_REPORT_ID &&
			report_len == PIP_WAKEUP_EVENT_SIZE) {
		/*
		 * Device wake event from deep sleep mode for touch.
		 * This interrupt event is used to wake system up.
		 *
		 * Note:
		 * It will introduce about 20~40 ms additional delay
		 * time in receiving for first valid touch report data.
		 * The time is used to execute device runtime resume
		 * process.
		 */
		pm_runtime_get_sync(dev);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_sync_autosuspend(dev);
		return 0;
	} else if (report_id != PIP_TOUCH_REPORT_ID &&
			report_id != PIP_BTN_REPORT_ID &&
			report_id != GEN5_OLD_PUSH_BTN_REPORT_ID &&
			report_id != PIP_PUSH_BTN_REPORT_ID &&
			report_id != PIP_PROXIMITY_REPORT_ID) {
		/* Running in BL mode or unknown response data read. */
		dev_err(dev, "invalid report_id=0x%02x\n", report_id);
		return -EINVAL;
	}

	if (report_id == PIP_TOUCH_REPORT_ID &&
		(report_len < PIP_TOUCH_REPORT_HEAD_SIZE ||
			report_len > PIP_TOUCH_REPORT_MAX_SIZE)) {
		/* Invalid report data length for finger packet. */
		dev_err(dev, "invalid touch packet length=%d\n", report_len);
		return 0;
	}

	if ((report_id == PIP_BTN_REPORT_ID ||
			report_id == GEN5_OLD_PUSH_BTN_REPORT_ID ||
			report_id == PIP_PUSH_BTN_REPORT_ID) &&
		(report_len < PIP_BTN_REPORT_HEAD_SIZE ||
			report_len > PIP_BTN_REPORT_MAX_SIZE)) {
		/* Invalid report data length of button packet. */
		dev_err(dev, "invalid button packet length=%d\n", report_len);
		return 0;
	}

	if (report_id == PIP_PROXIMITY_REPORT_ID &&
			report_len != PIP_PROXIMITY_REPORT_SIZE) {
		/* Invalid report data length of proximity packet. */
		dev_err(dev, "invalid proximity data, length=%d\n", report_len);
		return 0;
	}

	if (report_id == PIP_TOUCH_REPORT_ID)
		cyapa_pip_report_touches(cyapa, report_data);
	else if (report_id == PIP_PROXIMITY_REPORT_ID)
		cyapa_pip_report_proximity(cyapa, report_data);
	else
		cyapa_pip_report_buttons(cyapa, report_data);

	return 0;
}

int cyapa_pip_bl_activate(struct cyapa *cyapa) { return 0; }
int cyapa_pip_bl_deactivate(struct cyapa *cyapa) { return 0; }


const struct cyapa_dev_ops cyapa_gen5_ops = {
	.check_fw = cyapa_pip_check_fw,
	.bl_enter = cyapa_pip_bl_enter,
	.bl_initiate = cyapa_pip_bl_initiate,
	.update_fw = cyapa_pip_do_fw_update,
	.bl_activate = cyapa_pip_bl_activate,
	.bl_deactivate = cyapa_pip_bl_deactivate,

	.show_baseline = cyapa_gen5_show_baseline,
	.calibrate_store = cyapa_pip_do_calibrate,

	.initialize = cyapa_pip_cmd_state_initialize,

	.state_parse = cyapa_gen5_state_parse,
	.operational_check = cyapa_gen5_do_operational_check,

	.irq_handler = cyapa_pip_irq_handler,
	.irq_cmd_handler = cyapa_pip_irq_cmd_handler,
	.sort_empty_output_data = cyapa_empty_pip_output_data,
	.set_power_mode = cyapa_gen5_set_power_mode,

	.set_proximity = cyapa_pip_set_proximity,
};
