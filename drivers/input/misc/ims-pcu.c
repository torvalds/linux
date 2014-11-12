/*
 * Driver for IMS Passenger Control Unit Devices
 *
 * Copyright (C) 2013 The IMS Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb/input.h>
#include <linux/usb/cdc.h>
#include <asm/unaligned.h>

#define IMS_PCU_KEYMAP_LEN		32

struct ims_pcu_buttons {
	struct input_dev *input;
	char name[32];
	char phys[32];
	unsigned short keymap[IMS_PCU_KEYMAP_LEN];
};

struct ims_pcu_gamepad {
	struct input_dev *input;
	char name[32];
	char phys[32];
};

struct ims_pcu_backlight {
	struct led_classdev cdev;
	struct work_struct work;
	enum led_brightness desired_brightness;
	char name[32];
};

#define IMS_PCU_PART_NUMBER_LEN		15
#define IMS_PCU_SERIAL_NUMBER_LEN	8
#define IMS_PCU_DOM_LEN			8
#define IMS_PCU_FW_VERSION_LEN		(9 + 1)
#define IMS_PCU_BL_VERSION_LEN		(9 + 1)
#define IMS_PCU_BL_RESET_REASON_LEN	(2 + 1)

#define IMS_PCU_PCU_B_DEVICE_ID		5

#define IMS_PCU_BUF_SIZE		128

struct ims_pcu {
	struct usb_device *udev;
	struct device *dev; /* control interface's device, used for logging */

	unsigned int device_no;

	bool bootloader_mode;

	char part_number[IMS_PCU_PART_NUMBER_LEN];
	char serial_number[IMS_PCU_SERIAL_NUMBER_LEN];
	char date_of_manufacturing[IMS_PCU_DOM_LEN];
	char fw_version[IMS_PCU_FW_VERSION_LEN];
	char bl_version[IMS_PCU_BL_VERSION_LEN];
	char reset_reason[IMS_PCU_BL_RESET_REASON_LEN];
	int update_firmware_status;
	u8 device_id;

	u8 ofn_reg_addr;

	struct usb_interface *ctrl_intf;

	struct usb_endpoint_descriptor *ep_ctrl;
	struct urb *urb_ctrl;
	u8 *urb_ctrl_buf;
	dma_addr_t ctrl_dma;
	size_t max_ctrl_size;

	struct usb_interface *data_intf;

	struct usb_endpoint_descriptor *ep_in;
	struct urb *urb_in;
	u8 *urb_in_buf;
	dma_addr_t read_dma;
	size_t max_in_size;

	struct usb_endpoint_descriptor *ep_out;
	u8 *urb_out_buf;
	size_t max_out_size;

	u8 read_buf[IMS_PCU_BUF_SIZE];
	u8 read_pos;
	u8 check_sum;
	bool have_stx;
	bool have_dle;

	u8 cmd_buf[IMS_PCU_BUF_SIZE];
	u8 ack_id;
	u8 expected_response;
	u8 cmd_buf_len;
	struct completion cmd_done;
	struct mutex cmd_mutex;

	u32 fw_start_addr;
	u32 fw_end_addr;
	struct completion async_firmware_done;

	struct ims_pcu_buttons buttons;
	struct ims_pcu_gamepad *gamepad;
	struct ims_pcu_backlight backlight;

	bool setup_complete; /* Input and LED devices have been created */
};


/*********************************************************************
 *             Buttons Input device support                          *
 *********************************************************************/

static const unsigned short ims_pcu_keymap_1[] = {
	[1] = KEY_ATTENDANT_OFF,
	[2] = KEY_ATTENDANT_ON,
	[3] = KEY_LIGHTS_TOGGLE,
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_INFO,
};

static const unsigned short ims_pcu_keymap_2[] = {
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_INFO,
};

static const unsigned short ims_pcu_keymap_3[] = {
	[1] = KEY_HOMEPAGE,
	[2] = KEY_ATTENDANT_TOGGLE,
	[3] = KEY_LIGHTS_TOGGLE,
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_DISPLAYTOGGLE,
	[18] = KEY_PLAYPAUSE,
};

static const unsigned short ims_pcu_keymap_4[] = {
	[1] = KEY_ATTENDANT_OFF,
	[2] = KEY_ATTENDANT_ON,
	[3] = KEY_LIGHTS_TOGGLE,
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_INFO,
	[18] = KEY_PLAYPAUSE,
};

static const unsigned short ims_pcu_keymap_5[] = {
	[1] = KEY_ATTENDANT_OFF,
	[2] = KEY_ATTENDANT_ON,
	[3] = KEY_LIGHTS_TOGGLE,
};

struct ims_pcu_device_info {
	const unsigned short *keymap;
	size_t keymap_len;
	bool has_gamepad;
};

#define IMS_PCU_DEVINFO(_n, _gamepad)				\
	[_n] = {						\
		.keymap = ims_pcu_keymap_##_n,			\
		.keymap_len = ARRAY_SIZE(ims_pcu_keymap_##_n),	\
		.has_gamepad = _gamepad,			\
	}

static const struct ims_pcu_device_info ims_pcu_device_info[] = {
	IMS_PCU_DEVINFO(1, true),
	IMS_PCU_DEVINFO(2, true),
	IMS_PCU_DEVINFO(3, true),
	IMS_PCU_DEVINFO(4, true),
	IMS_PCU_DEVINFO(5, false),
};

static void ims_pcu_buttons_report(struct ims_pcu *pcu, u32 data)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;
	struct input_dev *input = buttons->input;
	int i;

	for (i = 0; i < 32; i++) {
		unsigned short keycode = buttons->keymap[i];

		if (keycode != KEY_RESERVED)
			input_report_key(input, keycode, data & (1UL << i));
	}

	input_sync(input);
}

static int ims_pcu_setup_buttons(struct ims_pcu *pcu,
				 const unsigned short *keymap,
				 size_t keymap_len)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;
	struct input_dev *input;
	int i;
	int error;

	input = input_allocate_device();
	if (!input) {
		dev_err(pcu->dev,
			"Not enough memory for input input device\n");
		return -ENOMEM;
	}

	snprintf(buttons->name, sizeof(buttons->name),
		 "IMS PCU#%d Button Interface", pcu->device_no);

	usb_make_path(pcu->udev, buttons->phys, sizeof(buttons->phys));
	strlcat(buttons->phys, "/input0", sizeof(buttons->phys));

	memcpy(buttons->keymap, keymap, sizeof(*keymap) * keymap_len);

	input->name = buttons->name;
	input->phys = buttons->phys;
	usb_to_input_id(pcu->udev, &input->id);
	input->dev.parent = &pcu->ctrl_intf->dev;

	input->keycode = buttons->keymap;
	input->keycodemax = ARRAY_SIZE(buttons->keymap);
	input->keycodesize = sizeof(buttons->keymap[0]);

	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < IMS_PCU_KEYMAP_LEN; i++)
		__set_bit(buttons->keymap[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	error = input_register_device(input);
	if (error) {
		dev_err(pcu->dev,
			"Failed to register buttons input device: %d\n",
			error);
		input_free_device(input);
		return error;
	}

	buttons->input = input;
	return 0;
}

static void ims_pcu_destroy_buttons(struct ims_pcu *pcu)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;

	input_unregister_device(buttons->input);
}


/*********************************************************************
 *             Gamepad Input device support                          *
 *********************************************************************/

static void ims_pcu_gamepad_report(struct ims_pcu *pcu, u32 data)
{
	struct ims_pcu_gamepad *gamepad = pcu->gamepad;
	struct input_dev *input = gamepad->input;
	int x, y;

	x = !!(data & (1 << 14)) - !!(data & (1 << 13));
	y = !!(data & (1 << 12)) - !!(data & (1 << 11));

	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);

	input_report_key(input, BTN_A, data & (1 << 7));
	input_report_key(input, BTN_B, data & (1 << 8));
	input_report_key(input, BTN_X, data & (1 << 9));
	input_report_key(input, BTN_Y, data & (1 << 10));
	input_report_key(input, BTN_START, data & (1 << 15));
	input_report_key(input, BTN_SELECT, data & (1 << 16));

	input_sync(input);
}

static int ims_pcu_setup_gamepad(struct ims_pcu *pcu)
{
	struct ims_pcu_gamepad *gamepad;
	struct input_dev *input;
	int error;

	gamepad = kzalloc(sizeof(struct ims_pcu_gamepad), GFP_KERNEL);
	input = input_allocate_device();
	if (!gamepad || !input) {
		dev_err(pcu->dev,
			"Not enough memory for gamepad device\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	gamepad->input = input;

	snprintf(gamepad->name, sizeof(gamepad->name),
		 "IMS PCU#%d Gamepad Interface", pcu->device_no);

	usb_make_path(pcu->udev, gamepad->phys, sizeof(gamepad->phys));
	strlcat(gamepad->phys, "/input1", sizeof(gamepad->phys));

	input->name = gamepad->name;
	input->phys = gamepad->phys;
	usb_to_input_id(pcu->udev, &input->id);
	input->dev.parent = &pcu->ctrl_intf->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_A, input->keybit);
	__set_bit(BTN_B, input->keybit);
	__set_bit(BTN_X, input->keybit);
	__set_bit(BTN_Y, input->keybit);
	__set_bit(BTN_START, input->keybit);
	__set_bit(BTN_SELECT, input->keybit);

	__set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_X, -1, 1, 0, 0);
	input_set_abs_params(input, ABS_Y, -1, 1, 0, 0);

	error = input_register_device(input);
	if (error) {
		dev_err(pcu->dev,
			"Failed to register gamepad input device: %d\n",
			error);
		goto err_free_mem;
	}

	pcu->gamepad = gamepad;
	return 0;

err_free_mem:
	input_free_device(input);
	kfree(gamepad);
	return -ENOMEM;
}

static void ims_pcu_destroy_gamepad(struct ims_pcu *pcu)
{
	struct ims_pcu_gamepad *gamepad = pcu->gamepad;

	input_unregister_device(gamepad->input);
	kfree(gamepad);
}


/*********************************************************************
 *             PCU Communication protocol handling                   *
 *********************************************************************/

#define IMS_PCU_PROTOCOL_STX		0x02
#define IMS_PCU_PROTOCOL_ETX		0x03
#define IMS_PCU_PROTOCOL_DLE		0x10

/* PCU commands */
#define IMS_PCU_CMD_STATUS		0xa0
#define IMS_PCU_CMD_PCU_RESET		0xa1
#define IMS_PCU_CMD_RESET_REASON	0xa2
#define IMS_PCU_CMD_SEND_BUTTONS	0xa3
#define IMS_PCU_CMD_JUMP_TO_BTLDR	0xa4
#define IMS_PCU_CMD_GET_INFO		0xa5
#define IMS_PCU_CMD_SET_BRIGHTNESS	0xa6
#define IMS_PCU_CMD_EEPROM		0xa7
#define IMS_PCU_CMD_GET_FW_VERSION	0xa8
#define IMS_PCU_CMD_GET_BL_VERSION	0xa9
#define IMS_PCU_CMD_SET_INFO		0xab
#define IMS_PCU_CMD_GET_BRIGHTNESS	0xac
#define IMS_PCU_CMD_GET_DEVICE_ID	0xae
#define IMS_PCU_CMD_SPECIAL_INFO	0xb0
#define IMS_PCU_CMD_BOOTLOADER		0xb1	/* Pass data to bootloader */
#define IMS_PCU_CMD_OFN_SET_CONFIG	0xb3
#define IMS_PCU_CMD_OFN_GET_CONFIG	0xb4

/* PCU responses */
#define IMS_PCU_RSP_STATUS		0xc0
#define IMS_PCU_RSP_PCU_RESET		0	/* Originally 0xc1 */
#define IMS_PCU_RSP_RESET_REASON	0xc2
#define IMS_PCU_RSP_SEND_BUTTONS	0xc3
#define IMS_PCU_RSP_JUMP_TO_BTLDR	0	/* Originally 0xc4 */
#define IMS_PCU_RSP_GET_INFO		0xc5
#define IMS_PCU_RSP_SET_BRIGHTNESS	0xc6
#define IMS_PCU_RSP_EEPROM		0xc7
#define IMS_PCU_RSP_GET_FW_VERSION	0xc8
#define IMS_PCU_RSP_GET_BL_VERSION	0xc9
#define IMS_PCU_RSP_SET_INFO		0xcb
#define IMS_PCU_RSP_GET_BRIGHTNESS	0xcc
#define IMS_PCU_RSP_CMD_INVALID		0xcd
#define IMS_PCU_RSP_GET_DEVICE_ID	0xce
#define IMS_PCU_RSP_SPECIAL_INFO	0xd0
#define IMS_PCU_RSP_BOOTLOADER		0xd1	/* Bootloader response */
#define IMS_PCU_RSP_OFN_SET_CONFIG	0xd2
#define IMS_PCU_RSP_OFN_GET_CONFIG	0xd3


#define IMS_PCU_RSP_EVNT_BUTTONS	0xe0	/* Unsolicited, button state */
#define IMS_PCU_GAMEPAD_MASK		0x0001ff80UL	/* Bits 7 through 16 */


#define IMS_PCU_MIN_PACKET_LEN		3
#define IMS_PCU_DATA_OFFSET		2

#define IMS_PCU_CMD_WRITE_TIMEOUT	100 /* msec */
#define IMS_PCU_CMD_RESPONSE_TIMEOUT	500 /* msec */

static void ims_pcu_report_events(struct ims_pcu *pcu)
{
	u32 data = get_unaligned_be32(&pcu->read_buf[3]);

	ims_pcu_buttons_report(pcu, data & ~IMS_PCU_GAMEPAD_MASK);
	if (pcu->gamepad)
		ims_pcu_gamepad_report(pcu, data);
}

static void ims_pcu_handle_response(struct ims_pcu *pcu)
{
	switch (pcu->read_buf[0]) {
	case IMS_PCU_RSP_EVNT_BUTTONS:
		if (likely(pcu->setup_complete))
			ims_pcu_report_events(pcu);
		break;

	default:
		/*
		 * See if we got command completion.
		 * If both the sequence and response code match save
		 * the data and signal completion.
		 */
		if (pcu->read_buf[0] == pcu->expected_response &&
		    pcu->read_buf[1] == pcu->ack_id - 1) {

			memcpy(pcu->cmd_buf, pcu->read_buf, pcu->read_pos);
			pcu->cmd_buf_len = pcu->read_pos;
			complete(&pcu->cmd_done);
		}
		break;
	}
}

static void ims_pcu_process_data(struct ims_pcu *pcu, struct urb *urb)
{
	int i;

	for (i = 0; i < urb->actual_length; i++) {
		u8 data = pcu->urb_in_buf[i];

		/* Skip everything until we get Start Xmit */
		if (!pcu->have_stx && data != IMS_PCU_PROTOCOL_STX)
			continue;

		if (pcu->have_dle) {
			pcu->have_dle = false;
			pcu->read_buf[pcu->read_pos++] = data;
			pcu->check_sum += data;
			continue;
		}

		switch (data) {
		case IMS_PCU_PROTOCOL_STX:
			if (pcu->have_stx)
				dev_warn(pcu->dev,
					 "Unexpected STX at byte %d, discarding old data\n",
					 pcu->read_pos);
			pcu->have_stx = true;
			pcu->have_dle = false;
			pcu->read_pos = 0;
			pcu->check_sum = 0;
			break;

		case IMS_PCU_PROTOCOL_DLE:
			pcu->have_dle = true;
			break;

		case IMS_PCU_PROTOCOL_ETX:
			if (pcu->read_pos < IMS_PCU_MIN_PACKET_LEN) {
				dev_warn(pcu->dev,
					 "Short packet received (%d bytes), ignoring\n",
					 pcu->read_pos);
			} else if (pcu->check_sum != 0) {
				dev_warn(pcu->dev,
					 "Invalid checksum in packet (%d bytes), ignoring\n",
					 pcu->read_pos);
			} else {
				ims_pcu_handle_response(pcu);
			}

			pcu->have_stx = false;
			pcu->have_dle = false;
			pcu->read_pos = 0;
			break;

		default:
			pcu->read_buf[pcu->read_pos++] = data;
			pcu->check_sum += data;
			break;
		}
	}
}

static bool ims_pcu_byte_needs_escape(u8 byte)
{
	return byte == IMS_PCU_PROTOCOL_STX ||
	       byte == IMS_PCU_PROTOCOL_ETX ||
	       byte == IMS_PCU_PROTOCOL_DLE;
}

static int ims_pcu_send_cmd_chunk(struct ims_pcu *pcu,
				  u8 command, int chunk, int len)
{
	int error;

	error = usb_bulk_msg(pcu->udev,
			     usb_sndbulkpipe(pcu->udev,
					     pcu->ep_out->bEndpointAddress),
			     pcu->urb_out_buf, len,
			     NULL, IMS_PCU_CMD_WRITE_TIMEOUT);
	if (error < 0) {
		dev_dbg(pcu->dev,
			"Sending 0x%02x command failed at chunk %d: %d\n",
			command, chunk, error);
		return error;
	}

	return 0;
}

static int ims_pcu_send_command(struct ims_pcu *pcu,
				u8 command, const u8 *data, int len)
{
	int count = 0;
	int chunk = 0;
	int delta;
	int i;
	int error;
	u8 csum = 0;
	u8 ack_id;

	pcu->urb_out_buf[count++] = IMS_PCU_PROTOCOL_STX;

	/* We know the command need not be escaped */
	pcu->urb_out_buf[count++] = command;
	csum += command;

	ack_id = pcu->ack_id++;
	if (ack_id == 0xff)
		ack_id = pcu->ack_id++;

	if (ims_pcu_byte_needs_escape(ack_id))
		pcu->urb_out_buf[count++] = IMS_PCU_PROTOCOL_DLE;

	pcu->urb_out_buf[count++] = ack_id;
	csum += ack_id;

	for (i = 0; i < len; i++) {

		delta = ims_pcu_byte_needs_escape(data[i]) ? 2 : 1;
		if (count + delta >= pcu->max_out_size) {
			error = ims_pcu_send_cmd_chunk(pcu, command,
						       ++chunk, count);
			if (error)
				return error;

			count = 0;
		}

		if (delta == 2)
			pcu->urb_out_buf[count++] = IMS_PCU_PROTOCOL_DLE;

		pcu->urb_out_buf[count++] = data[i];
		csum += data[i];
	}

	csum = 1 + ~csum;

	delta = ims_pcu_byte_needs_escape(csum) ? 3 : 2;
	if (count + delta >= pcu->max_out_size) {
		error = ims_pcu_send_cmd_chunk(pcu, command, ++chunk, count);
		if (error)
			return error;

		count = 0;
	}

	if (delta == 3)
		pcu->urb_out_buf[count++] = IMS_PCU_PROTOCOL_DLE;

	pcu->urb_out_buf[count++] = csum;
	pcu->urb_out_buf[count++] = IMS_PCU_PROTOCOL_ETX;

	return ims_pcu_send_cmd_chunk(pcu, command, ++chunk, count);
}

static int __ims_pcu_execute_command(struct ims_pcu *pcu,
				     u8 command, const void *data, size_t len,
				     u8 expected_response, int response_time)
{
	int error;

	pcu->expected_response = expected_response;
	init_completion(&pcu->cmd_done);

	error = ims_pcu_send_command(pcu, command, data, len);
	if (error)
		return error;

	if (expected_response &&
	    !wait_for_completion_timeout(&pcu->cmd_done,
					 msecs_to_jiffies(response_time))) {
		dev_dbg(pcu->dev, "Command 0x%02x timed out\n", command);
		return -ETIMEDOUT;
	}

	return 0;
}

#define ims_pcu_execute_command(pcu, code, data, len)			\
	__ims_pcu_execute_command(pcu,					\
				  IMS_PCU_CMD_##code, data, len,	\
				  IMS_PCU_RSP_##code,			\
				  IMS_PCU_CMD_RESPONSE_TIMEOUT)

#define ims_pcu_execute_query(pcu, code)				\
	ims_pcu_execute_command(pcu, code, NULL, 0)

/* Bootloader commands */
#define IMS_PCU_BL_CMD_QUERY_DEVICE	0xa1
#define IMS_PCU_BL_CMD_UNLOCK_CONFIG	0xa2
#define IMS_PCU_BL_CMD_ERASE_APP	0xa3
#define IMS_PCU_BL_CMD_PROGRAM_DEVICE	0xa4
#define IMS_PCU_BL_CMD_PROGRAM_COMPLETE	0xa5
#define IMS_PCU_BL_CMD_READ_APP		0xa6
#define IMS_PCU_BL_CMD_RESET_DEVICE	0xa7
#define IMS_PCU_BL_CMD_LAUNCH_APP	0xa8

/* Bootloader commands */
#define IMS_PCU_BL_RSP_QUERY_DEVICE	0xc1
#define IMS_PCU_BL_RSP_UNLOCK_CONFIG	0xc2
#define IMS_PCU_BL_RSP_ERASE_APP	0xc3
#define IMS_PCU_BL_RSP_PROGRAM_DEVICE	0xc4
#define IMS_PCU_BL_RSP_PROGRAM_COMPLETE	0xc5
#define IMS_PCU_BL_RSP_READ_APP		0xc6
#define IMS_PCU_BL_RSP_RESET_DEVICE	0	/* originally 0xa7 */
#define IMS_PCU_BL_RSP_LAUNCH_APP	0	/* originally 0xa8 */

#define IMS_PCU_BL_DATA_OFFSET		3

static int __ims_pcu_execute_bl_command(struct ims_pcu *pcu,
				        u8 command, const void *data, size_t len,
				        u8 expected_response, int response_time)
{
	int error;

	pcu->cmd_buf[0] = command;
	if (data)
		memcpy(&pcu->cmd_buf[1], data, len);

	error = __ims_pcu_execute_command(pcu,
				IMS_PCU_CMD_BOOTLOADER, pcu->cmd_buf, len + 1,
				expected_response ? IMS_PCU_RSP_BOOTLOADER : 0,
				response_time);
	if (error) {
		dev_err(pcu->dev,
			"Failure when sending 0x%02x command to bootloader, error: %d\n",
			pcu->cmd_buf[0], error);
		return error;
	}

	if (expected_response && pcu->cmd_buf[2] != expected_response) {
		dev_err(pcu->dev,
			"Unexpected response from bootloader: 0x%02x, wanted 0x%02x\n",
			pcu->cmd_buf[2], expected_response);
		return -EINVAL;
	}

	return 0;
}

#define ims_pcu_execute_bl_command(pcu, code, data, len, timeout)	\
	__ims_pcu_execute_bl_command(pcu,				\
				     IMS_PCU_BL_CMD_##code, data, len,	\
				     IMS_PCU_BL_RSP_##code, timeout)	\

#define IMS_PCU_INFO_PART_OFFSET	2
#define IMS_PCU_INFO_DOM_OFFSET		17
#define IMS_PCU_INFO_SERIAL_OFFSET	25

#define IMS_PCU_SET_INFO_SIZE		31

static int ims_pcu_get_info(struct ims_pcu *pcu)
{
	int error;

	error = ims_pcu_execute_query(pcu, GET_INFO);
	if (error) {
		dev_err(pcu->dev,
			"GET_INFO command failed, error: %d\n", error);
		return error;
	}

	memcpy(pcu->part_number,
	       &pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET],
	       sizeof(pcu->part_number));
	memcpy(pcu->date_of_manufacturing,
	       &pcu->cmd_buf[IMS_PCU_INFO_DOM_OFFSET],
	       sizeof(pcu->date_of_manufacturing));
	memcpy(pcu->serial_number,
	       &pcu->cmd_buf[IMS_PCU_INFO_SERIAL_OFFSET],
	       sizeof(pcu->serial_number));

	return 0;
}

static int ims_pcu_set_info(struct ims_pcu *pcu)
{
	int error;

	memcpy(&pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET],
	       pcu->part_number, sizeof(pcu->part_number));
	memcpy(&pcu->cmd_buf[IMS_PCU_INFO_DOM_OFFSET],
	       pcu->date_of_manufacturing, sizeof(pcu->date_of_manufacturing));
	memcpy(&pcu->cmd_buf[IMS_PCU_INFO_SERIAL_OFFSET],
	       pcu->serial_number, sizeof(pcu->serial_number));

	error = ims_pcu_execute_command(pcu, SET_INFO,
					&pcu->cmd_buf[IMS_PCU_DATA_OFFSET],
					IMS_PCU_SET_INFO_SIZE);
	if (error) {
		dev_err(pcu->dev,
			"Failed to update device information, error: %d\n",
			error);
		return error;
	}

	return 0;
}

static int ims_pcu_switch_to_bootloader(struct ims_pcu *pcu)
{
	int error;

	/* Execute jump to the bootoloader */
	error = ims_pcu_execute_command(pcu, JUMP_TO_BTLDR, NULL, 0);
	if (error) {
		dev_err(pcu->dev,
			"Failure when sending JUMP TO BOOLTLOADER command, error: %d\n",
			error);
		return error;
	}

	return 0;
}

/*********************************************************************
 *             Firmware Update handling                              *
 *********************************************************************/

#define IMS_PCU_FIRMWARE_NAME	"imspcu.fw"

struct ims_pcu_flash_fmt {
	__le32 addr;
	u8 len;
	u8 data[];
};

static unsigned int ims_pcu_count_fw_records(const struct firmware *fw)
{
	const struct ihex_binrec *rec = (const struct ihex_binrec *)fw->data;
	unsigned int count = 0;

	while (rec) {
		count++;
		rec = ihex_next_binrec(rec);
	}

	return count;
}

static int ims_pcu_verify_block(struct ims_pcu *pcu,
				u32 addr, u8 len, const u8 *data)
{
	struct ims_pcu_flash_fmt *fragment;
	int error;

	fragment = (void *)&pcu->cmd_buf[1];
	put_unaligned_le32(addr, &fragment->addr);
	fragment->len = len;

	error = ims_pcu_execute_bl_command(pcu, READ_APP, NULL, 5,
					IMS_PCU_CMD_RESPONSE_TIMEOUT);
	if (error) {
		dev_err(pcu->dev,
			"Failed to retrieve block at 0x%08x, len %d, error: %d\n",
			addr, len, error);
		return error;
	}

	fragment = (void *)&pcu->cmd_buf[IMS_PCU_BL_DATA_OFFSET];
	if (get_unaligned_le32(&fragment->addr) != addr ||
	    fragment->len != len) {
		dev_err(pcu->dev,
			"Wrong block when retrieving 0x%08x (0x%08x), len %d (%d)\n",
			addr, get_unaligned_le32(&fragment->addr),
			len, fragment->len);
		return -EINVAL;
	}

	if (memcmp(fragment->data, data, len)) {
		dev_err(pcu->dev,
			"Mismatch in block at 0x%08x, len %d\n",
			addr, len);
		return -EINVAL;
	}

	return 0;
}

static int ims_pcu_flash_firmware(struct ims_pcu *pcu,
				  const struct firmware *fw,
				  unsigned int n_fw_records)
{
	const struct ihex_binrec *rec = (const struct ihex_binrec *)fw->data;
	struct ims_pcu_flash_fmt *fragment;
	unsigned int count = 0;
	u32 addr;
	u8 len;
	int error;

	error = ims_pcu_execute_bl_command(pcu, ERASE_APP, NULL, 0, 2000);
	if (error) {
		dev_err(pcu->dev,
			"Failed to erase application image, error: %d\n",
			error);
		return error;
	}

	while (rec) {
		/*
		 * The firmware format is messed up for some reason.
		 * The address twice that of what is needed for some
		 * reason and we end up overwriting half of the data
		 * with the next record.
		 */
		addr = be32_to_cpu(rec->addr) / 2;
		len = be16_to_cpu(rec->len);

		fragment = (void *)&pcu->cmd_buf[1];
		put_unaligned_le32(addr, &fragment->addr);
		fragment->len = len;
		memcpy(fragment->data, rec->data, len);

		error = ims_pcu_execute_bl_command(pcu, PROGRAM_DEVICE,
						NULL, len + 5,
						IMS_PCU_CMD_RESPONSE_TIMEOUT);
		if (error) {
			dev_err(pcu->dev,
				"Failed to write block at 0x%08x, len %d, error: %d\n",
				addr, len, error);
			return error;
		}

		if (addr >= pcu->fw_start_addr && addr < pcu->fw_end_addr) {
			error = ims_pcu_verify_block(pcu, addr, len, rec->data);
			if (error)
				return error;
		}

		count++;
		pcu->update_firmware_status = (count * 100) / n_fw_records;

		rec = ihex_next_binrec(rec);
	}

	error = ims_pcu_execute_bl_command(pcu, PROGRAM_COMPLETE,
					    NULL, 0, 2000);
	if (error)
		dev_err(pcu->dev,
			"Failed to send PROGRAM_COMPLETE, error: %d\n",
			error);

	return 0;
}

static int ims_pcu_handle_firmware_update(struct ims_pcu *pcu,
					  const struct firmware *fw)
{
	unsigned int n_fw_records;
	int retval;

	dev_info(pcu->dev, "Updating firmware %s, size: %zu\n",
		 IMS_PCU_FIRMWARE_NAME, fw->size);

	n_fw_records = ims_pcu_count_fw_records(fw);

	retval = ims_pcu_flash_firmware(pcu, fw, n_fw_records);
	if (retval)
		goto out;

	retval = ims_pcu_execute_bl_command(pcu, LAUNCH_APP, NULL, 0, 0);
	if (retval)
		dev_err(pcu->dev,
			"Failed to start application image, error: %d\n",
			retval);

out:
	pcu->update_firmware_status = retval;
	sysfs_notify(&pcu->dev->kobj, NULL, "update_firmware_status");
	return retval;
}

static void ims_pcu_process_async_firmware(const struct firmware *fw,
					   void *context)
{
	struct ims_pcu *pcu = context;
	int error;

	if (!fw) {
		dev_err(pcu->dev, "Failed to get firmware %s\n",
			IMS_PCU_FIRMWARE_NAME);
		goto out;
	}

	error = ihex_validate_fw(fw);
	if (error) {
		dev_err(pcu->dev, "Firmware %s is invalid\n",
			IMS_PCU_FIRMWARE_NAME);
		goto out;
	}

	mutex_lock(&pcu->cmd_mutex);
	ims_pcu_handle_firmware_update(pcu, fw);
	mutex_unlock(&pcu->cmd_mutex);

	release_firmware(fw);

out:
	complete(&pcu->async_firmware_done);
}

/*********************************************************************
 *             Backlight LED device support                          *
 *********************************************************************/

#define IMS_PCU_MAX_BRIGHTNESS		31998

static void ims_pcu_backlight_work(struct work_struct *work)
{
	struct ims_pcu_backlight *backlight =
			container_of(work, struct ims_pcu_backlight, work);
	struct ims_pcu *pcu =
			container_of(backlight, struct ims_pcu, backlight);
	int desired_brightness = backlight->desired_brightness;
	__le16 br_val = cpu_to_le16(desired_brightness);
	int error;

	mutex_lock(&pcu->cmd_mutex);

	error = ims_pcu_execute_command(pcu, SET_BRIGHTNESS,
					&br_val, sizeof(br_val));
	if (error && error != -ENODEV)
		dev_warn(pcu->dev,
			 "Failed to set desired brightness %u, error: %d\n",
			 desired_brightness, error);

	mutex_unlock(&pcu->cmd_mutex);
}

static void ims_pcu_backlight_set_brightness(struct led_classdev *cdev,
					     enum led_brightness value)
{
	struct ims_pcu_backlight *backlight =
			container_of(cdev, struct ims_pcu_backlight, cdev);

	backlight->desired_brightness = value;
	schedule_work(&backlight->work);
}

static enum led_brightness
ims_pcu_backlight_get_brightness(struct led_classdev *cdev)
{
	struct ims_pcu_backlight *backlight =
			container_of(cdev, struct ims_pcu_backlight, cdev);
	struct ims_pcu *pcu =
			container_of(backlight, struct ims_pcu, backlight);
	int brightness;
	int error;

	mutex_lock(&pcu->cmd_mutex);

	error = ims_pcu_execute_query(pcu, GET_BRIGHTNESS);
	if (error) {
		dev_warn(pcu->dev,
			 "Failed to get current brightness, error: %d\n",
			 error);
		/* Assume the LED is OFF */
		brightness = LED_OFF;
	} else {
		brightness =
			get_unaligned_le16(&pcu->cmd_buf[IMS_PCU_DATA_OFFSET]);
	}

	mutex_unlock(&pcu->cmd_mutex);

	return brightness;
}

static int ims_pcu_setup_backlight(struct ims_pcu *pcu)
{
	struct ims_pcu_backlight *backlight = &pcu->backlight;
	int error;

	INIT_WORK(&backlight->work, ims_pcu_backlight_work);
	snprintf(backlight->name, sizeof(backlight->name),
		 "pcu%d::kbd_backlight", pcu->device_no);

	backlight->cdev.name = backlight->name;
	backlight->cdev.max_brightness = IMS_PCU_MAX_BRIGHTNESS;
	backlight->cdev.brightness_get = ims_pcu_backlight_get_brightness;
	backlight->cdev.brightness_set = ims_pcu_backlight_set_brightness;

	error = led_classdev_register(pcu->dev, &backlight->cdev);
	if (error) {
		dev_err(pcu->dev,
			"Failed to register backlight LED device, error: %d\n",
			error);
		return error;
	}

	return 0;
}

static void ims_pcu_destroy_backlight(struct ims_pcu *pcu)
{
	struct ims_pcu_backlight *backlight = &pcu->backlight;

	led_classdev_unregister(&backlight->cdev);
	cancel_work_sync(&backlight->work);
}


/*********************************************************************
 *             Sysfs attributes handling                             *
 *********************************************************************/

struct ims_pcu_attribute {
	struct device_attribute dattr;
	size_t field_offset;
	int field_length;
};

static ssize_t ims_pcu_attribute_show(struct device *dev,
				      struct device_attribute *dattr,
				      char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_attribute *attr =
			container_of(dattr, struct ims_pcu_attribute, dattr);
	char *field = (char *)pcu + attr->field_offset;

	return scnprintf(buf, PAGE_SIZE, "%.*s\n", attr->field_length, field);
}

static ssize_t ims_pcu_attribute_store(struct device *dev,
				       struct device_attribute *dattr,
				       const char *buf, size_t count)
{

	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_attribute *attr =
			container_of(dattr, struct ims_pcu_attribute, dattr);
	char *field = (char *)pcu + attr->field_offset;
	size_t data_len;
	int error;

	if (count > attr->field_length)
		return -EINVAL;

	data_len = strnlen(buf, attr->field_length);
	if (data_len > attr->field_length)
		return -EINVAL;

	error = mutex_lock_interruptible(&pcu->cmd_mutex);
	if (error)
		return error;

	memset(field, 0, attr->field_length);
	memcpy(field, buf, data_len);

	error = ims_pcu_set_info(pcu);

	/*
	 * Even if update failed, let's fetch the info again as we just
	 * clobbered one of the fields.
	 */
	ims_pcu_get_info(pcu);

	mutex_unlock(&pcu->cmd_mutex);

	return error < 0 ? error : count;
}

#define IMS_PCU_ATTR(_field, _mode)					\
struct ims_pcu_attribute ims_pcu_attr_##_field = {			\
	.dattr = __ATTR(_field, _mode,					\
			ims_pcu_attribute_show,				\
			ims_pcu_attribute_store),			\
	.field_offset = offsetof(struct ims_pcu, _field),		\
	.field_length = sizeof(((struct ims_pcu *)NULL)->_field),	\
}

#define IMS_PCU_RO_ATTR(_field)						\
		IMS_PCU_ATTR(_field, S_IRUGO)
#define IMS_PCU_RW_ATTR(_field)						\
		IMS_PCU_ATTR(_field, S_IRUGO | S_IWUSR)

static IMS_PCU_RW_ATTR(part_number);
static IMS_PCU_RW_ATTR(serial_number);
static IMS_PCU_RW_ATTR(date_of_manufacturing);

static IMS_PCU_RO_ATTR(fw_version);
static IMS_PCU_RO_ATTR(bl_version);
static IMS_PCU_RO_ATTR(reset_reason);

static ssize_t ims_pcu_reset_device(struct device *dev,
				    struct device_attribute *dattr,
				    const char *buf, size_t count)
{
	static const u8 reset_byte = 1;
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int value;
	int error;

	error = kstrtoint(buf, 0, &value);
	if (error)
		return error;

	if (value != 1)
		return -EINVAL;

	dev_info(pcu->dev, "Attempting to reset device\n");

	error = ims_pcu_execute_command(pcu, PCU_RESET, &reset_byte, 1);
	if (error) {
		dev_info(pcu->dev,
			 "Failed to reset device, error: %d\n",
			 error);
		return error;
	}

	return count;
}

static DEVICE_ATTR(reset_device, S_IWUSR, NULL, ims_pcu_reset_device);

static ssize_t ims_pcu_update_firmware_store(struct device *dev,
					     struct device_attribute *dattr,
					     const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	const struct firmware *fw = NULL;
	int value;
	int error;

	error = kstrtoint(buf, 0, &value);
	if (error)
		return error;

	if (value != 1)
		return -EINVAL;

	error = mutex_lock_interruptible(&pcu->cmd_mutex);
	if (error)
		return error;

	error = request_ihex_firmware(&fw, IMS_PCU_FIRMWARE_NAME, pcu->dev);
	if (error) {
		dev_err(pcu->dev, "Failed to request firmware %s, error: %d\n",
			IMS_PCU_FIRMWARE_NAME, error);
		goto out;
	}

	/*
	 * If we are already in bootloader mode we can proceed with
	 * flashing the firmware.
	 *
	 * If we are in application mode, then we need to switch into
	 * bootloader mode, which will cause the device to disconnect
	 * and reconnect as different device.
	 */
	if (pcu->bootloader_mode)
		error = ims_pcu_handle_firmware_update(pcu, fw);
	else
		error = ims_pcu_switch_to_bootloader(pcu);

	release_firmware(fw);

out:
	mutex_unlock(&pcu->cmd_mutex);
	return error ?: count;
}

static DEVICE_ATTR(update_firmware, S_IWUSR,
		   NULL, ims_pcu_update_firmware_store);

static ssize_t
ims_pcu_update_firmware_status_show(struct device *dev,
				    struct device_attribute *dattr,
				    char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pcu->update_firmware_status);
}

static DEVICE_ATTR(update_firmware_status, S_IRUGO,
		   ims_pcu_update_firmware_status_show, NULL);

static struct attribute *ims_pcu_attrs[] = {
	&ims_pcu_attr_part_number.dattr.attr,
	&ims_pcu_attr_serial_number.dattr.attr,
	&ims_pcu_attr_date_of_manufacturing.dattr.attr,
	&ims_pcu_attr_fw_version.dattr.attr,
	&ims_pcu_attr_bl_version.dattr.attr,
	&ims_pcu_attr_reset_reason.dattr.attr,
	&dev_attr_reset_device.attr,
	&dev_attr_update_firmware.attr,
	&dev_attr_update_firmware_status.attr,
	NULL
};

static umode_t ims_pcu_is_attr_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	umode_t mode = attr->mode;

	if (pcu->bootloader_mode) {
		if (attr != &dev_attr_update_firmware_status.attr &&
		    attr != &dev_attr_update_firmware.attr &&
		    attr != &dev_attr_reset_device.attr) {
			mode = 0;
		}
	} else {
		if (attr == &dev_attr_update_firmware_status.attr)
			mode = 0;
	}

	return mode;
}

static struct attribute_group ims_pcu_attr_group = {
	.is_visible	= ims_pcu_is_attr_visible,
	.attrs		= ims_pcu_attrs,
};

/* Support for a separate OFN attribute group */

#define OFN_REG_RESULT_OFFSET	2

static int ims_pcu_read_ofn_config(struct ims_pcu *pcu, u8 addr, u8 *data)
{
	int error;
	s16 result;

	error = ims_pcu_execute_command(pcu, OFN_GET_CONFIG,
					&addr, sizeof(addr));
	if (error)
		return error;

	result = (s16)get_unaligned_le16(pcu->cmd_buf + OFN_REG_RESULT_OFFSET);
	if (result < 0)
		return -EIO;

	/* We only need LSB */
	*data = pcu->cmd_buf[OFN_REG_RESULT_OFFSET];
	return 0;
}

static int ims_pcu_write_ofn_config(struct ims_pcu *pcu, u8 addr, u8 data)
{
	u8 buffer[] = { addr, data };
	int error;
	s16 result;

	error = ims_pcu_execute_command(pcu, OFN_SET_CONFIG,
					&buffer, sizeof(buffer));
	if (error)
		return error;

	result = (s16)get_unaligned_le16(pcu->cmd_buf + OFN_REG_RESULT_OFFSET);
	if (result < 0)
		return -EIO;

	return 0;
}

static ssize_t ims_pcu_ofn_reg_data_show(struct device *dev,
					 struct device_attribute *dattr,
					 char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int error;
	u8 data;

	mutex_lock(&pcu->cmd_mutex);
	error = ims_pcu_read_ofn_config(pcu, pcu->ofn_reg_addr, &data);
	mutex_unlock(&pcu->cmd_mutex);

	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%x\n", data);
}

static ssize_t ims_pcu_ofn_reg_data_store(struct device *dev,
					  struct device_attribute *dattr,
					  const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int error;
	u8 value;

	error = kstrtou8(buf, 0, &value);
	if (error)
		return error;

	mutex_lock(&pcu->cmd_mutex);
	error = ims_pcu_write_ofn_config(pcu, pcu->ofn_reg_addr, value);
	mutex_unlock(&pcu->cmd_mutex);

	return error ?: count;
}

static DEVICE_ATTR(reg_data, S_IRUGO | S_IWUSR,
		   ims_pcu_ofn_reg_data_show, ims_pcu_ofn_reg_data_store);

static ssize_t ims_pcu_ofn_reg_addr_show(struct device *dev,
					 struct device_attribute *dattr,
					 char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int error;

	mutex_lock(&pcu->cmd_mutex);
	error = scnprintf(buf, PAGE_SIZE, "%x\n", pcu->ofn_reg_addr);
	mutex_unlock(&pcu->cmd_mutex);

	return error;
}

static ssize_t ims_pcu_ofn_reg_addr_store(struct device *dev,
					  struct device_attribute *dattr,
					  const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int error;
	u8 value;

	error = kstrtou8(buf, 0, &value);
	if (error)
		return error;

	mutex_lock(&pcu->cmd_mutex);
	pcu->ofn_reg_addr = value;
	mutex_unlock(&pcu->cmd_mutex);

	return count;
}

static DEVICE_ATTR(reg_addr, S_IRUGO | S_IWUSR,
		   ims_pcu_ofn_reg_addr_show, ims_pcu_ofn_reg_addr_store);

struct ims_pcu_ofn_bit_attribute {
	struct device_attribute dattr;
	u8 addr;
	u8 nr;
};

static ssize_t ims_pcu_ofn_bit_show(struct device *dev,
				    struct device_attribute *dattr,
				    char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_ofn_bit_attribute *attr =
		container_of(dattr, struct ims_pcu_ofn_bit_attribute, dattr);
	int error;
	u8 data;

	mutex_lock(&pcu->cmd_mutex);
	error = ims_pcu_read_ofn_config(pcu, attr->addr, &data);
	mutex_unlock(&pcu->cmd_mutex);

	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%d\n", !!(data & (1 << attr->nr)));
}

static ssize_t ims_pcu_ofn_bit_store(struct device *dev,
				     struct device_attribute *dattr,
				     const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_ofn_bit_attribute *attr =
		container_of(dattr, struct ims_pcu_ofn_bit_attribute, dattr);
	int error;
	int value;
	u8 data;

	error = kstrtoint(buf, 0, &value);
	if (error)
		return error;

	if (value > 1)
		return -EINVAL;

	mutex_lock(&pcu->cmd_mutex);

	error = ims_pcu_read_ofn_config(pcu, attr->addr, &data);
	if (!error) {
		if (value)
			data |= 1U << attr->nr;
		else
			data &= ~(1U << attr->nr);

		error = ims_pcu_write_ofn_config(pcu, attr->addr, data);
	}

	mutex_unlock(&pcu->cmd_mutex);

	return error ?: count;
}

#define IMS_PCU_OFN_BIT_ATTR(_field, _addr, _nr)			\
struct ims_pcu_ofn_bit_attribute ims_pcu_ofn_attr_##_field = {		\
	.dattr = __ATTR(_field, S_IWUSR | S_IRUGO,			\
			ims_pcu_ofn_bit_show, ims_pcu_ofn_bit_store),	\
	.addr = _addr,							\
	.nr = _nr,							\
}

static IMS_PCU_OFN_BIT_ATTR(engine_enable,   0x60, 7);
static IMS_PCU_OFN_BIT_ATTR(speed_enable,    0x60, 6);
static IMS_PCU_OFN_BIT_ATTR(assert_enable,   0x60, 5);
static IMS_PCU_OFN_BIT_ATTR(xyquant_enable,  0x60, 4);
static IMS_PCU_OFN_BIT_ATTR(xyscale_enable,  0x60, 1);

static IMS_PCU_OFN_BIT_ATTR(scale_x2,        0x63, 6);
static IMS_PCU_OFN_BIT_ATTR(scale_y2,        0x63, 7);

static struct attribute *ims_pcu_ofn_attrs[] = {
	&dev_attr_reg_data.attr,
	&dev_attr_reg_addr.attr,
	&ims_pcu_ofn_attr_engine_enable.dattr.attr,
	&ims_pcu_ofn_attr_speed_enable.dattr.attr,
	&ims_pcu_ofn_attr_assert_enable.dattr.attr,
	&ims_pcu_ofn_attr_xyquant_enable.dattr.attr,
	&ims_pcu_ofn_attr_xyscale_enable.dattr.attr,
	&ims_pcu_ofn_attr_scale_x2.dattr.attr,
	&ims_pcu_ofn_attr_scale_y2.dattr.attr,
	NULL
};

static struct attribute_group ims_pcu_ofn_attr_group = {
	.name	= "ofn",
	.attrs	= ims_pcu_ofn_attrs,
};

static void ims_pcu_irq(struct urb *urb)
{
	struct ims_pcu *pcu = urb->context;
	int retval, status;

	status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(pcu->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(pcu->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	dev_dbg(pcu->dev, "%s: received %d: %*ph\n", __func__,
		urb->actual_length, urb->actual_length, pcu->urb_in_buf);

	if (urb == pcu->urb_in)
		ims_pcu_process_data(pcu, urb);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval && retval != -ENODEV)
		dev_err(pcu->dev, "%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
}

static int ims_pcu_buffers_alloc(struct ims_pcu *pcu)
{
	int error;

	pcu->urb_in_buf = usb_alloc_coherent(pcu->udev, pcu->max_in_size,
					     GFP_KERNEL, &pcu->read_dma);
	if (!pcu->urb_in_buf) {
		dev_err(pcu->dev,
			"Failed to allocate memory for read buffer\n");
		return -ENOMEM;
	}

	pcu->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!pcu->urb_in) {
		dev_err(pcu->dev, "Failed to allocate input URB\n");
		error = -ENOMEM;
		goto err_free_urb_in_buf;
	}

	pcu->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	pcu->urb_in->transfer_dma = pcu->read_dma;

	usb_fill_bulk_urb(pcu->urb_in, pcu->udev,
			  usb_rcvbulkpipe(pcu->udev,
					  pcu->ep_in->bEndpointAddress),
			  pcu->urb_in_buf, pcu->max_in_size,
			  ims_pcu_irq, pcu);

	/*
	 * We are using usb_bulk_msg() for sending so there is no point
	 * in allocating memory with usb_alloc_coherent().
	 */
	pcu->urb_out_buf = kmalloc(pcu->max_out_size, GFP_KERNEL);
	if (!pcu->urb_out_buf) {
		dev_err(pcu->dev, "Failed to allocate memory for write buffer\n");
		error = -ENOMEM;
		goto err_free_in_urb;
	}

	pcu->urb_ctrl_buf = usb_alloc_coherent(pcu->udev, pcu->max_ctrl_size,
					       GFP_KERNEL, &pcu->ctrl_dma);
	if (!pcu->urb_ctrl_buf) {
		dev_err(pcu->dev,
			"Failed to allocate memory for read buffer\n");
		error = -ENOMEM;
		goto err_free_urb_out_buf;
	}

	pcu->urb_ctrl = usb_alloc_urb(0, GFP_KERNEL);
	if (!pcu->urb_ctrl) {
		dev_err(pcu->dev, "Failed to allocate input URB\n");
		error = -ENOMEM;
		goto err_free_urb_ctrl_buf;
	}

	pcu->urb_ctrl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	pcu->urb_ctrl->transfer_dma = pcu->ctrl_dma;

	usb_fill_int_urb(pcu->urb_ctrl, pcu->udev,
			  usb_rcvintpipe(pcu->udev,
					 pcu->ep_ctrl->bEndpointAddress),
			  pcu->urb_ctrl_buf, pcu->max_ctrl_size,
			  ims_pcu_irq, pcu, pcu->ep_ctrl->bInterval);

	return 0;

err_free_urb_ctrl_buf:
	usb_free_coherent(pcu->udev, pcu->max_ctrl_size,
			  pcu->urb_ctrl_buf, pcu->ctrl_dma);
err_free_urb_out_buf:
	kfree(pcu->urb_out_buf);
err_free_in_urb:
	usb_free_urb(pcu->urb_in);
err_free_urb_in_buf:
	usb_free_coherent(pcu->udev, pcu->max_in_size,
			  pcu->urb_in_buf, pcu->read_dma);
	return error;
}

static void ims_pcu_buffers_free(struct ims_pcu *pcu)
{
	usb_kill_urb(pcu->urb_in);
	usb_free_urb(pcu->urb_in);

	usb_free_coherent(pcu->udev, pcu->max_out_size,
			  pcu->urb_in_buf, pcu->read_dma);

	kfree(pcu->urb_out_buf);

	usb_kill_urb(pcu->urb_ctrl);
	usb_free_urb(pcu->urb_ctrl);

	usb_free_coherent(pcu->udev, pcu->max_ctrl_size,
			  pcu->urb_ctrl_buf, pcu->ctrl_dma);
}

static const struct usb_cdc_union_desc *
ims_pcu_get_cdc_union_desc(struct usb_interface *intf)
{
	const void *buf = intf->altsetting->extra;
	size_t buflen = intf->altsetting->extralen;
	struct usb_cdc_union_desc *union_desc;

	if (!buf) {
		dev_err(&intf->dev, "Missing descriptor data\n");
		return NULL;
	}

	if (!buflen) {
		dev_err(&intf->dev, "Zero length descriptor\n");
		return NULL;
	}

	while (buflen > 0) {
		union_desc = (struct usb_cdc_union_desc *)buf;

		if (union_desc->bDescriptorType == USB_DT_CS_INTERFACE &&
		    union_desc->bDescriptorSubType == USB_CDC_UNION_TYPE) {
			dev_dbg(&intf->dev, "Found union header\n");
			return union_desc;
		}

		buflen -= union_desc->bLength;
		buf += union_desc->bLength;
	}

	dev_err(&intf->dev, "Missing CDC union descriptor\n");
	return NULL;
}

static int ims_pcu_parse_cdc_data(struct usb_interface *intf, struct ims_pcu *pcu)
{
	const struct usb_cdc_union_desc *union_desc;
	struct usb_host_interface *alt;

	union_desc = ims_pcu_get_cdc_union_desc(intf);
	if (!union_desc)
		return -EINVAL;

	pcu->ctrl_intf = usb_ifnum_to_if(pcu->udev,
					 union_desc->bMasterInterface0);

	alt = pcu->ctrl_intf->cur_altsetting;
	pcu->ep_ctrl = &alt->endpoint[0].desc;
	pcu->max_ctrl_size = usb_endpoint_maxp(pcu->ep_ctrl);

	pcu->data_intf = usb_ifnum_to_if(pcu->udev,
					 union_desc->bSlaveInterface0);

	alt = pcu->data_intf->cur_altsetting;
	if (alt->desc.bNumEndpoints != 2) {
		dev_err(pcu->dev,
			"Incorrect number of endpoints on data interface (%d)\n",
			alt->desc.bNumEndpoints);
		return -EINVAL;
	}

	pcu->ep_out = &alt->endpoint[0].desc;
	if (!usb_endpoint_is_bulk_out(pcu->ep_out)) {
		dev_err(pcu->dev,
			"First endpoint on data interface is not BULK OUT\n");
		return -EINVAL;
	}

	pcu->max_out_size = usb_endpoint_maxp(pcu->ep_out);
	if (pcu->max_out_size < 8) {
		dev_err(pcu->dev,
			"Max OUT packet size is too small (%zd)\n",
			pcu->max_out_size);
		return -EINVAL;
	}

	pcu->ep_in = &alt->endpoint[1].desc;
	if (!usb_endpoint_is_bulk_in(pcu->ep_in)) {
		dev_err(pcu->dev,
			"Second endpoint on data interface is not BULK IN\n");
		return -EINVAL;
	}

	pcu->max_in_size = usb_endpoint_maxp(pcu->ep_in);
	if (pcu->max_in_size < 8) {
		dev_err(pcu->dev,
			"Max IN packet size is too small (%zd)\n",
			pcu->max_in_size);
		return -EINVAL;
	}

	return 0;
}

static int ims_pcu_start_io(struct ims_pcu *pcu)
{
	int error;

	error = usb_submit_urb(pcu->urb_ctrl, GFP_KERNEL);
	if (error) {
		dev_err(pcu->dev,
			"Failed to start control IO - usb_submit_urb failed with result: %d\n",
			error);
		return -EIO;
	}

	error = usb_submit_urb(pcu->urb_in, GFP_KERNEL);
	if (error) {
		dev_err(pcu->dev,
			"Failed to start IO - usb_submit_urb failed with result: %d\n",
			error);
		usb_kill_urb(pcu->urb_ctrl);
		return -EIO;
	}

	return 0;
}

static void ims_pcu_stop_io(struct ims_pcu *pcu)
{
	usb_kill_urb(pcu->urb_in);
	usb_kill_urb(pcu->urb_ctrl);
}

static int ims_pcu_line_setup(struct ims_pcu *pcu)
{
	struct usb_host_interface *interface = pcu->ctrl_intf->cur_altsetting;
	struct usb_cdc_line_coding *line = (void *)pcu->cmd_buf;
	int error;

	memset(line, 0, sizeof(*line));
	line->dwDTERate = cpu_to_le32(57600);
	line->bDataBits = 8;

	error = usb_control_msg(pcu->udev, usb_sndctrlpipe(pcu->udev, 0),
				USB_CDC_REQ_SET_LINE_CODING,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				0, interface->desc.bInterfaceNumber,
				line, sizeof(struct usb_cdc_line_coding),
				5000);
	if (error < 0) {
		dev_err(pcu->dev, "Failed to set line coding, error: %d\n",
			error);
		return error;
	}

	error = usb_control_msg(pcu->udev, usb_sndctrlpipe(pcu->udev, 0),
				USB_CDC_REQ_SET_CONTROL_LINE_STATE,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				0x03, interface->desc.bInterfaceNumber,
				NULL, 0, 5000);
	if (error < 0) {
		dev_err(pcu->dev, "Failed to set line state, error: %d\n",
			error);
		return error;
	}

	return 0;
}

static int ims_pcu_get_device_info(struct ims_pcu *pcu)
{
	int error;

	error = ims_pcu_get_info(pcu);
	if (error)
		return error;

	error = ims_pcu_execute_query(pcu, GET_FW_VERSION);
	if (error) {
		dev_err(pcu->dev,
			"GET_FW_VERSION command failed, error: %d\n", error);
		return error;
	}

	snprintf(pcu->fw_version, sizeof(pcu->fw_version),
		 "%02d%02d%02d%02d.%c%c",
		 pcu->cmd_buf[2], pcu->cmd_buf[3], pcu->cmd_buf[4], pcu->cmd_buf[5],
		 pcu->cmd_buf[6], pcu->cmd_buf[7]);

	error = ims_pcu_execute_query(pcu, GET_BL_VERSION);
	if (error) {
		dev_err(pcu->dev,
			"GET_BL_VERSION command failed, error: %d\n", error);
		return error;
	}

	snprintf(pcu->bl_version, sizeof(pcu->bl_version),
		 "%02d%02d%02d%02d.%c%c",
		 pcu->cmd_buf[2], pcu->cmd_buf[3], pcu->cmd_buf[4], pcu->cmd_buf[5],
		 pcu->cmd_buf[6], pcu->cmd_buf[7]);

	error = ims_pcu_execute_query(pcu, RESET_REASON);
	if (error) {
		dev_err(pcu->dev,
			"RESET_REASON command failed, error: %d\n", error);
		return error;
	}

	snprintf(pcu->reset_reason, sizeof(pcu->reset_reason),
		 "%02x", pcu->cmd_buf[IMS_PCU_DATA_OFFSET]);

	dev_dbg(pcu->dev,
		"P/N: %s, MD: %s, S/N: %s, FW: %s, BL: %s, RR: %s\n",
		pcu->part_number,
		pcu->date_of_manufacturing,
		pcu->serial_number,
		pcu->fw_version,
		pcu->bl_version,
		pcu->reset_reason);

	return 0;
}

static int ims_pcu_identify_type(struct ims_pcu *pcu, u8 *device_id)
{
	int error;

	error = ims_pcu_execute_query(pcu, GET_DEVICE_ID);
	if (error) {
		dev_err(pcu->dev,
			"GET_DEVICE_ID command failed, error: %d\n", error);
		return error;
	}

	*device_id = pcu->cmd_buf[IMS_PCU_DATA_OFFSET];
	dev_dbg(pcu->dev, "Detected device ID: %d\n", *device_id);

	return 0;
}

static int ims_pcu_init_application_mode(struct ims_pcu *pcu)
{
	static atomic_t device_no = ATOMIC_INIT(0);

	const struct ims_pcu_device_info *info;
	int error;

	error = ims_pcu_get_device_info(pcu);
	if (error) {
		/* Device does not respond to basic queries, hopeless */
		return error;
	}

	error = ims_pcu_identify_type(pcu, &pcu->device_id);
	if (error) {
		dev_err(pcu->dev,
			"Failed to identify device, error: %d\n", error);
		/*
		 * Do not signal error, but do not create input nor
		 * backlight devices either, let userspace figure this
		 * out (flash a new firmware?).
		 */
		return 0;
	}

	if (pcu->device_id >= ARRAY_SIZE(ims_pcu_device_info) ||
	    !ims_pcu_device_info[pcu->device_id].keymap) {
		dev_err(pcu->dev, "Device ID %d is not valid\n", pcu->device_id);
		/* Same as above, punt to userspace */
		return 0;
	}

	/* Device appears to be operable, complete initialization */
	pcu->device_no = atomic_inc_return(&device_no) - 1;

	/*
	 * PCU-B devices, both GEN_1 and GEN_2 do not have OFN sensor
	 */
	if (pcu->device_id != IMS_PCU_PCU_B_DEVICE_ID) {
		error = sysfs_create_group(&pcu->dev->kobj,
					   &ims_pcu_ofn_attr_group);
		if (error)
			return error;
	}

	error = ims_pcu_setup_backlight(pcu);
	if (error)
		return error;

	info = &ims_pcu_device_info[pcu->device_id];
	error = ims_pcu_setup_buttons(pcu, info->keymap, info->keymap_len);
	if (error)
		goto err_destroy_backlight;

	if (info->has_gamepad) {
		error = ims_pcu_setup_gamepad(pcu);
		if (error)
			goto err_destroy_buttons;
	}

	pcu->setup_complete = true;

	return 0;

err_destroy_buttons:
	ims_pcu_destroy_buttons(pcu);
err_destroy_backlight:
	ims_pcu_destroy_backlight(pcu);
	return error;
}

static void ims_pcu_destroy_application_mode(struct ims_pcu *pcu)
{
	if (pcu->setup_complete) {
		pcu->setup_complete = false;
		mb(); /* make sure flag setting is not reordered */

		if (pcu->gamepad)
			ims_pcu_destroy_gamepad(pcu);
		ims_pcu_destroy_buttons(pcu);
		ims_pcu_destroy_backlight(pcu);

		if (pcu->device_id != IMS_PCU_PCU_B_DEVICE_ID)
			sysfs_remove_group(&pcu->dev->kobj,
					   &ims_pcu_ofn_attr_group);
	}
}

static int ims_pcu_init_bootloader_mode(struct ims_pcu *pcu)
{
	int error;

	error = ims_pcu_execute_bl_command(pcu, QUERY_DEVICE, NULL, 0,
					   IMS_PCU_CMD_RESPONSE_TIMEOUT);
	if (error) {
		dev_err(pcu->dev, "Bootloader does not respond, aborting\n");
		return error;
	}

	pcu->fw_start_addr =
		get_unaligned_le32(&pcu->cmd_buf[IMS_PCU_DATA_OFFSET + 11]);
	pcu->fw_end_addr =
		get_unaligned_le32(&pcu->cmd_buf[IMS_PCU_DATA_OFFSET + 15]);

	dev_info(pcu->dev,
		 "Device is in bootloader mode (addr 0x%08x-0x%08x), requesting firmware\n",
		 pcu->fw_start_addr, pcu->fw_end_addr);

	error = request_firmware_nowait(THIS_MODULE, true,
					IMS_PCU_FIRMWARE_NAME,
					pcu->dev, GFP_KERNEL, pcu,
					ims_pcu_process_async_firmware);
	if (error) {
		/* This error is not fatal, let userspace have another chance */
		complete(&pcu->async_firmware_done);
	}

	return 0;
}

static void ims_pcu_destroy_bootloader_mode(struct ims_pcu *pcu)
{
	/* Make sure our initial firmware request has completed */
	wait_for_completion(&pcu->async_firmware_done);
}

#define IMS_PCU_APPLICATION_MODE	0
#define IMS_PCU_BOOTLOADER_MODE		1

static struct usb_driver ims_pcu_driver;

static int ims_pcu_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ims_pcu *pcu;
	int error;

	pcu = kzalloc(sizeof(struct ims_pcu), GFP_KERNEL);
	if (!pcu)
		return -ENOMEM;

	pcu->dev = &intf->dev;
	pcu->udev = udev;
	pcu->bootloader_mode = id->driver_info == IMS_PCU_BOOTLOADER_MODE;
	mutex_init(&pcu->cmd_mutex);
	init_completion(&pcu->cmd_done);
	init_completion(&pcu->async_firmware_done);

	error = ims_pcu_parse_cdc_data(intf, pcu);
	if (error)
		goto err_free_mem;

	error = usb_driver_claim_interface(&ims_pcu_driver,
					   pcu->data_intf, pcu);
	if (error) {
		dev_err(&intf->dev,
			"Unable to claim corresponding data interface: %d\n",
			error);
		goto err_free_mem;
	}

	usb_set_intfdata(pcu->ctrl_intf, pcu);
	usb_set_intfdata(pcu->data_intf, pcu);

	error = ims_pcu_buffers_alloc(pcu);
	if (error)
		goto err_unclaim_intf;

	error = ims_pcu_start_io(pcu);
	if (error)
		goto err_free_buffers;

	error = ims_pcu_line_setup(pcu);
	if (error)
		goto err_stop_io;

	error = sysfs_create_group(&intf->dev.kobj, &ims_pcu_attr_group);
	if (error)
		goto err_stop_io;

	error = pcu->bootloader_mode ?
			ims_pcu_init_bootloader_mode(pcu) :
			ims_pcu_init_application_mode(pcu);
	if (error)
		goto err_remove_sysfs;

	return 0;

err_remove_sysfs:
	sysfs_remove_group(&intf->dev.kobj, &ims_pcu_attr_group);
err_stop_io:
	ims_pcu_stop_io(pcu);
err_free_buffers:
	ims_pcu_buffers_free(pcu);
err_unclaim_intf:
	usb_driver_release_interface(&ims_pcu_driver, pcu->data_intf);
err_free_mem:
	kfree(pcu);
	return error;
}

static void ims_pcu_disconnect(struct usb_interface *intf)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;

	usb_set_intfdata(intf, NULL);

	/*
	 * See if we are dealing with control or data interface. The cleanup
	 * happens when we unbind primary (control) interface.
	 */
	if (alt->desc.bInterfaceClass != USB_CLASS_COMM)
		return;

	sysfs_remove_group(&intf->dev.kobj, &ims_pcu_attr_group);

	ims_pcu_stop_io(pcu);

	if (pcu->bootloader_mode)
		ims_pcu_destroy_bootloader_mode(pcu);
	else
		ims_pcu_destroy_application_mode(pcu);

	ims_pcu_buffers_free(pcu);
	kfree(pcu);
}

#ifdef CONFIG_PM
static int ims_pcu_suspend(struct usb_interface *intf,
			   pm_message_t message)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;

	if (alt->desc.bInterfaceClass == USB_CLASS_COMM)
		ims_pcu_stop_io(pcu);

	return 0;
}

static int ims_pcu_resume(struct usb_interface *intf)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;
	int retval = 0;

	if (alt->desc.bInterfaceClass == USB_CLASS_COMM) {
		retval = ims_pcu_start_io(pcu);
		if (retval == 0)
			retval = ims_pcu_line_setup(pcu);
	}

	return retval;
}
#endif

static const struct usb_device_id ims_pcu_id_table[] = {
	{
		USB_DEVICE_AND_INTERFACE_INFO(0x04d8, 0x0082,
					USB_CLASS_COMM,
					USB_CDC_SUBCLASS_ACM,
					USB_CDC_ACM_PROTO_AT_V25TER),
		.driver_info = IMS_PCU_APPLICATION_MODE,
	},
	{
		USB_DEVICE_AND_INTERFACE_INFO(0x04d8, 0x0083,
					USB_CLASS_COMM,
					USB_CDC_SUBCLASS_ACM,
					USB_CDC_ACM_PROTO_AT_V25TER),
		.driver_info = IMS_PCU_BOOTLOADER_MODE,
	},
	{ }
};

static struct usb_driver ims_pcu_driver = {
	.name			= "ims_pcu",
	.id_table		= ims_pcu_id_table,
	.probe			= ims_pcu_probe,
	.disconnect		= ims_pcu_disconnect,
#ifdef CONFIG_PM
	.suspend		= ims_pcu_suspend,
	.resume			= ims_pcu_resume,
	.reset_resume		= ims_pcu_resume,
#endif
};

module_usb_driver(ims_pcu_driver);

MODULE_DESCRIPTION("IMS Passenger Control Unit driver");
MODULE_AUTHOR("Dmitry Torokhov <dmitry.torokhov@gmail.com>");
MODULE_LICENSE("GPL");
