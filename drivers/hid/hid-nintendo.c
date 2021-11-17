// SPDX-License-Identifier: GPL-2.0+
/*
 * HID driver for Nintendo Switch Joy-Cons and Pro Controllers
 *
 * Copyright (c) 2019 Daniel J. Ogorchock <djogorchock@gmail.com>
 *
 * The following resources/projects were referenced for this driver:
 *   https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering
 *   https://gitlab.com/pjranki/joycon-linux-kernel (Peter Rankin)
 *   https://github.com/FrotBot/SwitchProConLinuxUSB
 *   https://github.com/MTCKC/ProconXInput
 *   hid-wiimote kernel hid driver
 *   hid-logitech-hidpp driver
 *
 * This driver supports the Nintendo Switch Joy-Cons and Pro Controllers. The
 * Pro Controllers can either be used over USB or Bluetooth.
 *
 * The driver will retrieve the factory calibration info from the controllers,
 * so little to no user calibration should be required.
 *
 */

#include "hid-ids.h"
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/spinlock.h>

/*
 * Reference the url below for the following HID report defines:
 * https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering
 */

/* Output Reports */
static const u8 JC_OUTPUT_RUMBLE_AND_SUBCMD	= 0x01;
static const u8 JC_OUTPUT_FW_UPDATE_PKT		= 0x03;
static const u8 JC_OUTPUT_RUMBLE_ONLY		= 0x10;
static const u8 JC_OUTPUT_MCU_DATA		= 0x11;
static const u8 JC_OUTPUT_USB_CMD		= 0x80;

/* Subcommand IDs */
static const u8 JC_SUBCMD_STATE			/*= 0x00*/;
static const u8 JC_SUBCMD_MANUAL_BT_PAIRING	= 0x01;
static const u8 JC_SUBCMD_REQ_DEV_INFO		= 0x02;
static const u8 JC_SUBCMD_SET_REPORT_MODE	= 0x03;
static const u8 JC_SUBCMD_TRIGGERS_ELAPSED	= 0x04;
static const u8 JC_SUBCMD_GET_PAGE_LIST_STATE	= 0x05;
static const u8 JC_SUBCMD_SET_HCI_STATE		= 0x06;
static const u8 JC_SUBCMD_RESET_PAIRING_INFO	= 0x07;
static const u8 JC_SUBCMD_LOW_POWER_MODE	= 0x08;
static const u8 JC_SUBCMD_SPI_FLASH_READ	= 0x10;
static const u8 JC_SUBCMD_SPI_FLASH_WRITE	= 0x11;
static const u8 JC_SUBCMD_RESET_MCU		= 0x20;
static const u8 JC_SUBCMD_SET_MCU_CONFIG	= 0x21;
static const u8 JC_SUBCMD_SET_MCU_STATE		= 0x22;
static const u8 JC_SUBCMD_SET_PLAYER_LIGHTS	= 0x30;
static const u8 JC_SUBCMD_GET_PLAYER_LIGHTS	= 0x31;
static const u8 JC_SUBCMD_SET_HOME_LIGHT	= 0x38;
static const u8 JC_SUBCMD_ENABLE_IMU		= 0x40;
static const u8 JC_SUBCMD_SET_IMU_SENSITIVITY	= 0x41;
static const u8 JC_SUBCMD_WRITE_IMU_REG		= 0x42;
static const u8 JC_SUBCMD_READ_IMU_REG		= 0x43;
static const u8 JC_SUBCMD_ENABLE_VIBRATION	= 0x48;
static const u8 JC_SUBCMD_GET_REGULATED_VOLTAGE	= 0x50;

/* Input Reports */
static const u8 JC_INPUT_BUTTON_EVENT		= 0x3F;
static const u8 JC_INPUT_SUBCMD_REPLY		= 0x21;
static const u8 JC_INPUT_IMU_DATA		= 0x30;
static const u8 JC_INPUT_MCU_DATA		= 0x31;
static const u8 JC_INPUT_USB_RESPONSE		= 0x81;

/* Feature Reports */
static const u8 JC_FEATURE_LAST_SUBCMD		= 0x02;
static const u8 JC_FEATURE_OTA_FW_UPGRADE	= 0x70;
static const u8 JC_FEATURE_SETUP_MEM_READ	= 0x71;
static const u8 JC_FEATURE_MEM_READ		= 0x72;
static const u8 JC_FEATURE_ERASE_MEM_SECTOR	= 0x73;
static const u8 JC_FEATURE_MEM_WRITE		= 0x74;
static const u8 JC_FEATURE_LAUNCH		= 0x75;

/* USB Commands */
static const u8 JC_USB_CMD_CONN_STATUS		= 0x01;
static const u8 JC_USB_CMD_HANDSHAKE		= 0x02;
static const u8 JC_USB_CMD_BAUDRATE_3M		= 0x03;
static const u8 JC_USB_CMD_NO_TIMEOUT		= 0x04;
static const u8 JC_USB_CMD_EN_TIMEOUT		= 0x05;
static const u8 JC_USB_RESET			= 0x06;
static const u8 JC_USB_PRE_HANDSHAKE		= 0x91;
static const u8 JC_USB_SEND_UART		= 0x92;

/* SPI storage addresses of factory calibration data */
static const u16 JC_CAL_DATA_START		= 0x603d;
static const u16 JC_CAL_DATA_END		= 0x604e;
#define JC_CAL_DATA_SIZE	(JC_CAL_DATA_END - JC_CAL_DATA_START + 1)


/* The raw analog joystick values will be mapped in terms of this magnitude */
static const u16 JC_MAX_STICK_MAG		= 32767;
static const u16 JC_STICK_FUZZ			= 250;
static const u16 JC_STICK_FLAT			= 500;

/* States for controller state machine */
enum joycon_ctlr_state {
	JOYCON_CTLR_STATE_INIT,
	JOYCON_CTLR_STATE_READ,
};

struct joycon_stick_cal {
	s32 max;
	s32 min;
	s32 center;
};

/*
 * All the controller's button values are stored in a u32.
 * They can be accessed with bitwise ANDs.
 */
static const u32 JC_BTN_Y	= BIT(0);
static const u32 JC_BTN_X	= BIT(1);
static const u32 JC_BTN_B	= BIT(2);
static const u32 JC_BTN_A	= BIT(3);
static const u32 JC_BTN_SR_R	= BIT(4);
static const u32 JC_BTN_SL_R	= BIT(5);
static const u32 JC_BTN_R	= BIT(6);
static const u32 JC_BTN_ZR	= BIT(7);
static const u32 JC_BTN_MINUS	= BIT(8);
static const u32 JC_BTN_PLUS	= BIT(9);
static const u32 JC_BTN_RSTICK	= BIT(10);
static const u32 JC_BTN_LSTICK	= BIT(11);
static const u32 JC_BTN_HOME	= BIT(12);
static const u32 JC_BTN_CAP	= BIT(13); /* capture button */
static const u32 JC_BTN_DOWN	= BIT(16);
static const u32 JC_BTN_UP	= BIT(17);
static const u32 JC_BTN_RIGHT	= BIT(18);
static const u32 JC_BTN_LEFT	= BIT(19);
static const u32 JC_BTN_SR_L	= BIT(20);
static const u32 JC_BTN_SL_L	= BIT(21);
static const u32 JC_BTN_L	= BIT(22);
static const u32 JC_BTN_ZL	= BIT(23);

enum joycon_msg_type {
	JOYCON_MSG_TYPE_NONE,
	JOYCON_MSG_TYPE_USB,
	JOYCON_MSG_TYPE_SUBCMD,
};

struct joycon_subcmd_request {
	u8 output_id; /* must be 0x01 for subcommand, 0x10 for rumble only */
	u8 packet_num; /* incremented every send */
	u8 rumble_data[8];
	u8 subcmd_id;
	u8 data[0]; /* length depends on the subcommand */
} __packed;

struct joycon_subcmd_reply {
	u8 ack; /* MSB 1 for ACK, 0 for NACK */
	u8 id; /* id of requested subcmd */
	u8 data[0]; /* will be at most 35 bytes */
} __packed;

struct joycon_input_report {
	u8 id;
	u8 timer;
	u8 bat_con; /* battery and connection info */
	u8 button_status[3];
	u8 left_stick[3];
	u8 right_stick[3];
	u8 vibrator_report;

	/*
	 * If support for firmware updates, gyroscope data, and/or NFC/IR
	 * are added in the future, this can be swapped for a union.
	 */
	struct joycon_subcmd_reply reply;
} __packed;

#define JC_MAX_RESP_SIZE	(sizeof(struct joycon_input_report) + 35)

/* Each physical controller is associated with a joycon_ctlr struct */
struct joycon_ctlr {
	struct hid_device *hdev;
	struct input_dev *input;
	enum joycon_ctlr_state ctlr_state;

	/* The following members are used for synchronous sends/receives */
	enum joycon_msg_type msg_type;
	u8 subcmd_num;
	struct mutex output_mutex;
	u8 input_buf[JC_MAX_RESP_SIZE];
	wait_queue_head_t wait;
	bool received_resp;
	u8 usb_ack_match;
	u8 subcmd_ack_match;

	/* factory calibration data */
	struct joycon_stick_cal left_stick_cal_x;
	struct joycon_stick_cal left_stick_cal_y;
	struct joycon_stick_cal right_stick_cal_x;
	struct joycon_stick_cal right_stick_cal_y;

};

static int __joycon_hid_send(struct hid_device *hdev, u8 *data, size_t len)
{
	u8 *buf;
	int ret;

	buf = kmemdup(data, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = hid_hw_output_report(hdev, buf, len);
	kfree(buf);
	if (ret < 0)
		hid_dbg(hdev, "Failed to send output report ret=%d\n", ret);
	return ret;
}

static int joycon_hid_send_sync(struct joycon_ctlr *ctlr, u8 *data, size_t len)
{
	int ret;

	ret = __joycon_hid_send(ctlr->hdev, data, len);
	if (ret < 0) {
		memset(ctlr->input_buf, 0, JC_MAX_RESP_SIZE);
		return ret;
	}

	if (!wait_event_timeout(ctlr->wait, ctlr->received_resp, HZ)) {
		hid_dbg(ctlr->hdev, "synchronous send/receive timed out\n");
		memset(ctlr->input_buf, 0, JC_MAX_RESP_SIZE);
		return -ETIMEDOUT;
	}

	ctlr->received_resp = false;
	return 0;
}

static int joycon_send_usb(struct joycon_ctlr *ctlr, u8 cmd)
{
	int ret;
	u8 buf[2] = {JC_OUTPUT_USB_CMD};

	buf[1] = cmd;
	ctlr->usb_ack_match = cmd;
	ctlr->msg_type = JOYCON_MSG_TYPE_USB;
	ret = joycon_hid_send_sync(ctlr, buf, sizeof(buf));
	if (ret)
		hid_dbg(ctlr->hdev, "send usb command failed; ret=%d\n", ret);
	return ret;
}

static int joycon_send_subcmd(struct joycon_ctlr *ctlr,
			      struct joycon_subcmd_request *subcmd,
			      size_t data_len)
{
	int ret;

	subcmd->output_id = JC_OUTPUT_RUMBLE_AND_SUBCMD;
	subcmd->packet_num = ctlr->subcmd_num;
	if (++ctlr->subcmd_num > 0xF)
		ctlr->subcmd_num = 0;
	ctlr->subcmd_ack_match = subcmd->subcmd_id;
	ctlr->msg_type = JOYCON_MSG_TYPE_SUBCMD;

	ret = joycon_hid_send_sync(ctlr, (u8 *)subcmd,
				   sizeof(*subcmd) + data_len);
	if (ret < 0)
		hid_dbg(ctlr->hdev, "send subcommand failed; ret=%d\n", ret);
	else
		ret = 0;
	return ret;
}

/* Supply nibbles for flash and on. Ones correspond to active */
static int joycon_set_player_leds(struct joycon_ctlr *ctlr, u8 flash, u8 on)
{
	struct joycon_subcmd_request *req;
	u8 buffer[sizeof(*req) + 1] = { 0 };

	req = (struct joycon_subcmd_request *)buffer;
	req->subcmd_id = JC_SUBCMD_SET_PLAYER_LIGHTS;
	req->data[0] = (flash << 4) | on;

	hid_dbg(ctlr->hdev, "setting player leds\n");
	return joycon_send_subcmd(ctlr, req, 1);
}

static const u16 DFLT_STICK_CAL_CEN = 2000;
static const u16 DFLT_STICK_CAL_MAX = 3500;
static const u16 DFLT_STICK_CAL_MIN = 500;
static int joycon_request_calibration(struct joycon_ctlr *ctlr)
{
	struct joycon_subcmd_request *req;
	u8 buffer[sizeof(*req) + 5] = { 0 };
	struct joycon_input_report *report;
	struct joycon_stick_cal *cal_x;
	struct joycon_stick_cal *cal_y;
	s32 x_max_above;
	s32 x_min_below;
	s32 y_max_above;
	s32 y_min_below;
	u8 *data;
	u8 *raw_cal;
	int ret;

	req = (struct joycon_subcmd_request *)buffer;
	req->subcmd_id = JC_SUBCMD_SPI_FLASH_READ;
	data = req->data;
	data[0] = 0xFF & JC_CAL_DATA_START;
	data[1] = 0xFF & (JC_CAL_DATA_START >> 8);
	data[2] = 0xFF & (JC_CAL_DATA_START >> 16);
	data[3] = 0xFF & (JC_CAL_DATA_START >> 24);
	data[4] = JC_CAL_DATA_SIZE;

	hid_dbg(ctlr->hdev, "requesting cal data\n");
	ret = joycon_send_subcmd(ctlr, req, 5);
	if (ret) {
		hid_warn(ctlr->hdev,
			 "Failed to read stick cal, using defaults; ret=%d\n",
			 ret);

		ctlr->left_stick_cal_x.center = DFLT_STICK_CAL_CEN;
		ctlr->left_stick_cal_x.max = DFLT_STICK_CAL_MAX;
		ctlr->left_stick_cal_x.min = DFLT_STICK_CAL_MIN;

		ctlr->left_stick_cal_y.center = DFLT_STICK_CAL_CEN;
		ctlr->left_stick_cal_y.max = DFLT_STICK_CAL_MAX;
		ctlr->left_stick_cal_y.min = DFLT_STICK_CAL_MIN;

		ctlr->right_stick_cal_x.center = DFLT_STICK_CAL_CEN;
		ctlr->right_stick_cal_x.max = DFLT_STICK_CAL_MAX;
		ctlr->right_stick_cal_x.min = DFLT_STICK_CAL_MIN;

		ctlr->right_stick_cal_y.center = DFLT_STICK_CAL_CEN;
		ctlr->right_stick_cal_y.max = DFLT_STICK_CAL_MAX;
		ctlr->right_stick_cal_y.min = DFLT_STICK_CAL_MIN;

		return ret;
	}

	report = (struct joycon_input_report *)ctlr->input_buf;
	raw_cal = &report->reply.data[5];

	/* left stick calibration parsing */
	cal_x = &ctlr->left_stick_cal_x;
	cal_y = &ctlr->left_stick_cal_y;

	x_max_above = hid_field_extract(ctlr->hdev, (raw_cal + 0), 0, 12);
	y_max_above = hid_field_extract(ctlr->hdev, (raw_cal + 1), 4, 12);
	cal_x->center = hid_field_extract(ctlr->hdev, (raw_cal + 3), 0, 12);
	cal_y->center = hid_field_extract(ctlr->hdev, (raw_cal + 4), 4, 12);
	x_min_below = hid_field_extract(ctlr->hdev, (raw_cal + 6), 0, 12);
	y_min_below = hid_field_extract(ctlr->hdev, (raw_cal + 7), 4, 12);
	cal_x->max = cal_x->center + x_max_above;
	cal_x->min = cal_x->center - x_min_below;
	cal_y->max = cal_y->center + y_max_above;
	cal_y->min = cal_y->center - y_min_below;

	/* right stick calibration parsing */
	raw_cal += 9;
	cal_x = &ctlr->right_stick_cal_x;
	cal_y = &ctlr->right_stick_cal_y;

	cal_x->center = hid_field_extract(ctlr->hdev, (raw_cal + 0), 0, 12);
	cal_y->center = hid_field_extract(ctlr->hdev, (raw_cal + 1), 4, 12);
	x_min_below = hid_field_extract(ctlr->hdev, (raw_cal + 3), 0, 12);
	y_min_below = hid_field_extract(ctlr->hdev, (raw_cal + 4), 4, 12);
	x_max_above = hid_field_extract(ctlr->hdev, (raw_cal + 6), 0, 12);
	y_max_above = hid_field_extract(ctlr->hdev, (raw_cal + 7), 4, 12);
	cal_x->max = cal_x->center + x_max_above;
	cal_x->min = cal_x->center - x_min_below;
	cal_y->max = cal_y->center + y_max_above;
	cal_y->min = cal_y->center - y_min_below;

	hid_dbg(ctlr->hdev, "calibration:\n"
			    "l_x_c=%d l_x_max=%d l_x_min=%d\n"
			    "l_y_c=%d l_y_max=%d l_y_min=%d\n"
			    "r_x_c=%d r_x_max=%d r_x_min=%d\n"
			    "r_y_c=%d r_y_max=%d r_y_min=%d\n",
			    ctlr->left_stick_cal_x.center,
			    ctlr->left_stick_cal_x.max,
			    ctlr->left_stick_cal_x.min,
			    ctlr->left_stick_cal_y.center,
			    ctlr->left_stick_cal_y.max,
			    ctlr->left_stick_cal_y.min,
			    ctlr->right_stick_cal_x.center,
			    ctlr->right_stick_cal_x.max,
			    ctlr->right_stick_cal_x.min,
			    ctlr->right_stick_cal_y.center,
			    ctlr->right_stick_cal_y.max,
			    ctlr->right_stick_cal_y.min);

	return 0;
}

static int joycon_set_report_mode(struct joycon_ctlr *ctlr)
{
	struct joycon_subcmd_request *req;
	u8 buffer[sizeof(*req) + 1] = { 0 };

	req = (struct joycon_subcmd_request *)buffer;
	req->subcmd_id = JC_SUBCMD_SET_REPORT_MODE;
	req->data[0] = 0x30; /* standard, full report mode */

	hid_dbg(ctlr->hdev, "setting controller report mode\n");
	return joycon_send_subcmd(ctlr, req, 1);
}

static s32 joycon_map_stick_val(struct joycon_stick_cal *cal, s32 val)
{
	s32 center = cal->center;
	s32 min = cal->min;
	s32 max = cal->max;
	s32 new_val;

	if (val > center) {
		new_val = (val - center) * JC_MAX_STICK_MAG;
		new_val /= (max - center);
	} else {
		new_val = (center - val) * -JC_MAX_STICK_MAG;
		new_val /= (center - min);
	}
	new_val = clamp(new_val, (s32)-JC_MAX_STICK_MAG, (s32)JC_MAX_STICK_MAG);
	return new_val;
}

static void joycon_parse_report(struct joycon_ctlr *ctlr,
				struct joycon_input_report *rep)
{
	struct input_dev *dev = ctlr->input;
	u32 btns;
	u32 id = ctlr->hdev->product;

	btns = hid_field_extract(ctlr->hdev, rep->button_status, 0, 24);

	if (id != USB_DEVICE_ID_NINTENDO_JOYCONR) {
		u16 raw_x;
		u16 raw_y;
		s32 x;
		s32 y;

		/* get raw stick values */
		raw_x = hid_field_extract(ctlr->hdev, rep->left_stick, 0, 12);
		raw_y = hid_field_extract(ctlr->hdev,
					  rep->left_stick + 1, 4, 12);
		/* map the stick values */
		x = joycon_map_stick_val(&ctlr->left_stick_cal_x, raw_x);
		y = -joycon_map_stick_val(&ctlr->left_stick_cal_y, raw_y);
		/* report sticks */
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);

		/* report buttons */
		input_report_key(dev, BTN_TL, btns & JC_BTN_L);
		input_report_key(dev, BTN_TL2, btns & JC_BTN_ZL);
		if (id != USB_DEVICE_ID_NINTENDO_PROCON) {
			/* Report the S buttons as the non-existent triggers */
			input_report_key(dev, BTN_TR, btns & JC_BTN_SL_L);
			input_report_key(dev, BTN_TR2, btns & JC_BTN_SR_L);
		}
		input_report_key(dev, BTN_SELECT, btns & JC_BTN_MINUS);
		input_report_key(dev, BTN_THUMBL, btns & JC_BTN_LSTICK);
		input_report_key(dev, BTN_Z, btns & JC_BTN_CAP);
		input_report_key(dev, BTN_DPAD_DOWN, btns & JC_BTN_DOWN);
		input_report_key(dev, BTN_DPAD_UP, btns & JC_BTN_UP);
		input_report_key(dev, BTN_DPAD_RIGHT, btns & JC_BTN_RIGHT);
		input_report_key(dev, BTN_DPAD_LEFT, btns & JC_BTN_LEFT);
	}
	if (id != USB_DEVICE_ID_NINTENDO_JOYCONL) {
		u16 raw_x;
		u16 raw_y;
		s32 x;
		s32 y;

		/* get raw stick values */
		raw_x = hid_field_extract(ctlr->hdev, rep->right_stick, 0, 12);
		raw_y = hid_field_extract(ctlr->hdev,
					  rep->right_stick + 1, 4, 12);
		/* map stick values */
		x = joycon_map_stick_val(&ctlr->right_stick_cal_x, raw_x);
		y = -joycon_map_stick_val(&ctlr->right_stick_cal_y, raw_y);
		/* report sticks */
		input_report_abs(dev, ABS_RX, x);
		input_report_abs(dev, ABS_RY, y);

		/* report buttons */
		input_report_key(dev, BTN_TR, btns & JC_BTN_R);
		input_report_key(dev, BTN_TR2, btns & JC_BTN_ZR);
		if (id != USB_DEVICE_ID_NINTENDO_PROCON) {
			/* Report the S buttons as the non-existent triggers */
			input_report_key(dev, BTN_TL, btns & JC_BTN_SL_R);
			input_report_key(dev, BTN_TL2, btns & JC_BTN_SR_R);
		}
		input_report_key(dev, BTN_START, btns & JC_BTN_PLUS);
		input_report_key(dev, BTN_THUMBR, btns & JC_BTN_RSTICK);
		input_report_key(dev, BTN_MODE, btns & JC_BTN_HOME);
		input_report_key(dev, BTN_WEST, btns & JC_BTN_Y);
		input_report_key(dev, BTN_NORTH, btns & JC_BTN_X);
		input_report_key(dev, BTN_EAST, btns & JC_BTN_A);
		input_report_key(dev, BTN_SOUTH, btns & JC_BTN_B);
	}

	input_sync(dev);
}


static const unsigned int joycon_button_inputs_l[] = {
	BTN_SELECT, BTN_Z, BTN_THUMBL,
	BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
	BTN_TL, BTN_TL2,
	0 /* 0 signals end of array */
};

static const unsigned int joycon_button_inputs_r[] = {
	BTN_START, BTN_MODE, BTN_THUMBR,
	BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
	BTN_TR, BTN_TR2,
	0 /* 0 signals end of array */
};

static DEFINE_MUTEX(joycon_input_num_mutex);
static int joycon_input_create(struct joycon_ctlr *ctlr)
{
	struct hid_device *hdev;
	static int input_num = 1;
	const char *name;
	int ret;
	int i;

	hdev = ctlr->hdev;

	switch (hdev->product) {
	case USB_DEVICE_ID_NINTENDO_PROCON:
		name = "Nintendo Switch Pro Controller";
		break;
	case USB_DEVICE_ID_NINTENDO_JOYCONL:
		name = "Nintendo Switch Left Joy-Con";
		break;
	case USB_DEVICE_ID_NINTENDO_JOYCONR:
		name = "Nintendo Switch Right Joy-Con";
		break;
	default: /* Should be impossible */
		hid_err(hdev, "Invalid hid product\n");
		return -EINVAL;
	}

	ctlr->input = devm_input_allocate_device(&hdev->dev);
	if (!ctlr->input)
		return -ENOMEM;
	ctlr->input->id.bustype = hdev->bus;
	ctlr->input->id.vendor = hdev->vendor;
	ctlr->input->id.product = hdev->product;
	ctlr->input->id.version = hdev->version;
	ctlr->input->name = name;
	input_set_drvdata(ctlr->input, ctlr);


	/* set up sticks */
	if (hdev->product != USB_DEVICE_ID_NINTENDO_JOYCONR) {
		input_set_abs_params(ctlr->input, ABS_X,
				     -JC_MAX_STICK_MAG, JC_MAX_STICK_MAG,
				     JC_STICK_FUZZ, JC_STICK_FLAT);
		input_set_abs_params(ctlr->input, ABS_Y,
				     -JC_MAX_STICK_MAG, JC_MAX_STICK_MAG,
				     JC_STICK_FUZZ, JC_STICK_FLAT);
	}
	if (hdev->product != USB_DEVICE_ID_NINTENDO_JOYCONL) {
		input_set_abs_params(ctlr->input, ABS_RX,
				     -JC_MAX_STICK_MAG, JC_MAX_STICK_MAG,
				     JC_STICK_FUZZ, JC_STICK_FLAT);
		input_set_abs_params(ctlr->input, ABS_RY,
				     -JC_MAX_STICK_MAG, JC_MAX_STICK_MAG,
				     JC_STICK_FUZZ, JC_STICK_FLAT);
	}

	/* set up buttons */
	if (hdev->product != USB_DEVICE_ID_NINTENDO_JOYCONR) {
		for (i = 0; joycon_button_inputs_l[i] > 0; i++)
			input_set_capability(ctlr->input, EV_KEY,
					     joycon_button_inputs_l[i]);
	}
	if (hdev->product != USB_DEVICE_ID_NINTENDO_JOYCONL) {
		for (i = 0; joycon_button_inputs_r[i] > 0; i++)
			input_set_capability(ctlr->input, EV_KEY,
					     joycon_button_inputs_r[i]);
	}

	ret = input_register_device(ctlr->input);
	if (ret)
		return ret;

	/* Set the default controller player leds based on controller number */
	mutex_lock(&joycon_input_num_mutex);
	mutex_lock(&ctlr->output_mutex);
	ret = joycon_set_player_leds(ctlr, 0, 0xF >> (4 - input_num));
	if (ret)
		hid_warn(ctlr->hdev, "Failed to set leds; ret=%d\n", ret);
	mutex_unlock(&ctlr->output_mutex);
	if (++input_num > 4)
		input_num = 1;
	mutex_unlock(&joycon_input_num_mutex);

	return 0;
}

/* Common handler for parsing inputs */
static int joycon_ctlr_read_handler(struct joycon_ctlr *ctlr, u8 *data,
							      int size)
{
	int ret = 0;

	if (data[0] == JC_INPUT_SUBCMD_REPLY || data[0] == JC_INPUT_IMU_DATA ||
	    data[0] == JC_INPUT_MCU_DATA) {
		if (size >= 12) /* make sure it contains the input report */
			joycon_parse_report(ctlr,
					    (struct joycon_input_report *)data);
	}

	return ret;
}

static int joycon_ctlr_handle_event(struct joycon_ctlr *ctlr, u8 *data,
							      int size)
{
	int ret = 0;
	bool match = false;
	struct joycon_input_report *report;

	if (unlikely(mutex_is_locked(&ctlr->output_mutex)) &&
	    ctlr->msg_type != JOYCON_MSG_TYPE_NONE) {
		switch (ctlr->msg_type) {
		case JOYCON_MSG_TYPE_USB:
			if (size < 2)
				break;
			if (data[0] == JC_INPUT_USB_RESPONSE &&
			    data[1] == ctlr->usb_ack_match)
				match = true;
			break;
		case JOYCON_MSG_TYPE_SUBCMD:
			if (size < sizeof(struct joycon_input_report) ||
			    data[0] != JC_INPUT_SUBCMD_REPLY)
				break;
			report = (struct joycon_input_report *)data;
			if (report->reply.id == ctlr->subcmd_ack_match)
				match = true;
			break;
		default:
			break;
		}

		if (match) {
			memcpy(ctlr->input_buf, data,
			       min(size, (int)JC_MAX_RESP_SIZE));
			ctlr->msg_type = JOYCON_MSG_TYPE_NONE;
			ctlr->received_resp = true;
			wake_up(&ctlr->wait);

			/* This message has been handled */
			return 1;
		}
	}

	if (ctlr->ctlr_state == JOYCON_CTLR_STATE_READ)
		ret = joycon_ctlr_read_handler(ctlr, data, size);

	return ret;
}

static int nintendo_hid_event(struct hid_device *hdev,
			      struct hid_report *report, u8 *raw_data, int size)
{
	struct joycon_ctlr *ctlr = hid_get_drvdata(hdev);

	if (size < 1)
		return -EINVAL;

	return joycon_ctlr_handle_event(ctlr, raw_data, size);
}

static int nintendo_hid_probe(struct hid_device *hdev,
			    const struct hid_device_id *id)
{
	int ret;
	struct joycon_ctlr *ctlr;

	hid_dbg(hdev, "probe - start\n");

	ctlr = devm_kzalloc(&hdev->dev, sizeof(*ctlr), GFP_KERNEL);
	if (!ctlr) {
		ret = -ENOMEM;
		goto err;
	}

	ctlr->hdev = hdev;
	ctlr->ctlr_state = JOYCON_CTLR_STATE_INIT;
	hid_set_drvdata(hdev, ctlr);
	mutex_init(&ctlr->output_mutex);
	init_waitqueue_head(&ctlr->wait);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		goto err;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "cannot start hardware I/O\n");
		goto err_stop;
	}

	hid_device_io_start(hdev);

	/* Initialize the controller */
	mutex_lock(&ctlr->output_mutex);
	/* if handshake command fails, assume ble pro controller */
	if (hdev->product == USB_DEVICE_ID_NINTENDO_PROCON &&
	    !joycon_send_usb(ctlr, JC_USB_CMD_HANDSHAKE)) {
		hid_dbg(hdev, "detected USB controller\n");
		/* set baudrate for improved latency */
		ret = joycon_send_usb(ctlr, JC_USB_CMD_BAUDRATE_3M);
		if (ret) {
			hid_err(hdev, "Failed to set baudrate; ret=%d\n", ret);
			goto err_mutex;
		}
		/* handshake */
		ret = joycon_send_usb(ctlr, JC_USB_CMD_HANDSHAKE);
		if (ret) {
			hid_err(hdev, "Failed handshake; ret=%d\n", ret);
			goto err_mutex;
		}
		/*
		 * Set no timeout (to keep controller in USB mode).
		 * This doesn't send a response, so ignore the timeout.
		 */
		joycon_send_usb(ctlr, JC_USB_CMD_NO_TIMEOUT);
	}

	/* get controller calibration data, and parse it */
	ret = joycon_request_calibration(ctlr);
	if (ret) {
		/*
		 * We can function with default calibration, but it may be
		 * inaccurate. Provide a warning, and continue on.
		 */
		hid_warn(hdev, "Analog stick positions may be inaccurate\n");
	}

	/* Set the reporting mode to 0x30, which is the full report mode */
	ret = joycon_set_report_mode(ctlr);
	if (ret) {
		hid_err(hdev, "Failed to set report mode; ret=%d\n", ret);
		goto err_mutex;
	}

	mutex_unlock(&ctlr->output_mutex);

	ret = joycon_input_create(ctlr);
	if (ret) {
		hid_err(hdev, "Failed to create input device; ret=%d\n", ret);
		goto err_close;
	}

	ctlr->ctlr_state = JOYCON_CTLR_STATE_READ;

	hid_dbg(hdev, "probe - success\n");
	return 0;

err_mutex:
	mutex_unlock(&ctlr->output_mutex);
err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
err:
	hid_err(hdev, "probe - fail = %d\n", ret);
	return ret;
}

static void nintendo_hid_remove(struct hid_device *hdev)
{
	hid_dbg(hdev, "remove\n");
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id nintendo_hid_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_PROCON) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_PROCON) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_JOYCONL) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_JOYCONR) },
	{ }
};
MODULE_DEVICE_TABLE(hid, nintendo_hid_devices);

static struct hid_driver nintendo_hid_driver = {
	.name		= "nintendo",
	.id_table	= nintendo_hid_devices,
	.probe		= nintendo_hid_probe,
	.remove		= nintendo_hid_remove,
	.raw_event	= nintendo_hid_event,
};
module_hid_driver(nintendo_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel J. Ogorchock <djogorchock@gmail.com>");
MODULE_DESCRIPTION("Driver for Nintendo Switch Controllers");
