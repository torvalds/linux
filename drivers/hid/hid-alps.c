/*
 *  Copyright (c) 2016 Masaki Ota <masaki.ota@jp.alps.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include "hid-ids.h"

/* ALPS Device Product ID */
#define HID_PRODUCT_ID_T3_BTNLESS	0xD0C0
#define HID_PRODUCT_ID_COSMO		0x1202
#define HID_PRODUCT_ID_U1_PTP_1		0x1207
#define HID_PRODUCT_ID_U1			0x1209
#define HID_PRODUCT_ID_U1_PTP_2		0x120A
#define HID_PRODUCT_ID_U1_DUAL		0x120B
#define HID_PRODUCT_ID_T4_BTNLESS	0x120C

#define DEV_SINGLEPOINT				0x01
#define DEV_DUALPOINT				0x02

#define U1_MOUSE_REPORT_ID			0x01 /* Mouse data ReportID */
#define U1_ABSOLUTE_REPORT_ID		0x03 /* Absolute data ReportID */
#define U1_FEATURE_REPORT_ID		0x05 /* Feature ReportID */
#define U1_SP_ABSOLUTE_REPORT_ID	0x06 /* Feature ReportID */

#define U1_FEATURE_REPORT_LEN		0x08 /* Feature Report Length */
#define U1_FEATURE_REPORT_LEN_ALL	0x0A
#define U1_CMD_REGISTER_READ		0xD1
#define U1_CMD_REGISTER_WRITE		0xD2

#define	U1_DEVTYPE_SP_SUPPORT		0x10 /* SP Support */
#define	U1_DISABLE_DEV				0x01
#define U1_TP_ABS_MODE				0x02
#define	U1_SP_ABS_MODE				0x80

#define ADDRESS_U1_DEV_CTRL_1	0x00800040
#define ADDRESS_U1_DEVICE_TYP	0x00800043
#define ADDRESS_U1_NUM_SENS_X	0x00800047
#define ADDRESS_U1_NUM_SENS_Y	0x00800048
#define ADDRESS_U1_PITCH_SENS_X	0x00800049
#define ADDRESS_U1_PITCH_SENS_Y	0x0080004A
#define ADDRESS_U1_RESO_DWN_ABS 0x0080004E
#define ADDRESS_U1_PAD_BTN		0x00800052
#define ADDRESS_U1_SP_BTN		0x0080009F

#define MAX_TOUCHES	5

/**
 * struct u1_data
 *
 * @input: pointer to the kernel input device
 * @input2: pointer to the kernel input2 device
 * @hdev: pointer to the struct hid_device
 *
 * @dev_ctrl: device control parameter
 * @dev_type: device type
 * @sen_line_num_x: number of sensor line of X
 * @sen_line_num_y: number of sensor line of Y
 * @pitch_x: sensor pitch of X
 * @pitch_y: sensor pitch of Y
 * @resolution: resolution
 * @btn_info: button information
 * @x_active_len_mm: active area length of X (mm)
 * @y_active_len_mm: active area length of Y (mm)
 * @x_max: maximum x coordinate value
 * @y_max: maximum y coordinate value
 * @btn_cnt: number of buttons
 * @sp_btn_cnt: number of stick buttons
 */
struct u1_dev {
	struct input_dev *input;
	struct input_dev *input2;
	struct hid_device *hdev;

	u8	dev_ctrl;
	u8	dev_type;
	u8	sen_line_num_x;
	u8	sen_line_num_y;
	u8	pitch_x;
	u8	pitch_y;
	u8	resolution;
	u8	btn_info;
	u8	sp_btn_info;
	u32	x_active_len_mm;
	u32	y_active_len_mm;
	u32	x_max;
	u32	y_max;
	u32	btn_cnt;
	u32	sp_btn_cnt;
};

static int u1_read_write_register(struct hid_device *hdev, u32 address,
	u8 *read_val, u8 write_val, bool read_flag)
{
	int ret, i;
	u8 check_sum;
	u8 *input;
	u8 *readbuf;

	input = kzalloc(U1_FEATURE_REPORT_LEN, GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input[0] = U1_FEATURE_REPORT_ID;
	if (read_flag) {
		input[1] = U1_CMD_REGISTER_READ;
		input[6] = 0x00;
	} else {
		input[1] = U1_CMD_REGISTER_WRITE;
		input[6] = write_val;
	}

	put_unaligned_le32(address, input + 2);

	/* Calculate the checksum */
	check_sum = U1_FEATURE_REPORT_LEN_ALL;
	for (i = 0; i < U1_FEATURE_REPORT_LEN - 1; i++)
		check_sum += input[i];

	input[7] = check_sum;
	ret = hid_hw_raw_request(hdev, U1_FEATURE_REPORT_ID, input,
			U1_FEATURE_REPORT_LEN,
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (ret < 0) {
		dev_err(&hdev->dev, "failed to read command (%d)\n", ret);
		goto exit;
	}

	if (read_flag) {
		readbuf = kzalloc(U1_FEATURE_REPORT_LEN, GFP_KERNEL);
		if (!readbuf) {
			kfree(input);
			return -ENOMEM;
		}

		ret = hid_hw_raw_request(hdev, U1_FEATURE_REPORT_ID, readbuf,
				U1_FEATURE_REPORT_LEN,
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

		if (ret < 0) {
			dev_err(&hdev->dev, "failed read register (%d)\n", ret);
			goto exit;
		}

		*read_val = readbuf[6];

		kfree(readbuf);
	}

	ret = 0;

exit:
	kfree(input);
	return ret;
}

static int alps_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	unsigned int x, y, z;
	int i;
	short sp_x, sp_y;
	struct u1_dev *hdata = hid_get_drvdata(hdev);

	switch (data[0]) {
	case U1_MOUSE_REPORT_ID:
		break;
	case U1_FEATURE_REPORT_ID:
		break;
	case U1_ABSOLUTE_REPORT_ID:
		for (i = 0; i < MAX_TOUCHES; i++) {
			u8 *contact = &data[i * 5];

			x = get_unaligned_le16(contact + 3);
			y = get_unaligned_le16(contact + 5);
			z = contact[7] & 0x7F;

			input_mt_slot(hdata->input, i);

			if (z != 0) {
				input_mt_report_slot_state(hdata->input,
					MT_TOOL_FINGER, 1);
				input_report_abs(hdata->input,
					ABS_MT_POSITION_X, x);
				input_report_abs(hdata->input,
					ABS_MT_POSITION_Y, y);
				input_report_abs(hdata->input,
					ABS_MT_PRESSURE, z);
			} else {
				input_mt_report_slot_state(hdata->input,
					MT_TOOL_FINGER, 0);
			}
		}

		input_mt_sync_frame(hdata->input);

		input_report_key(hdata->input, BTN_LEFT,
			data[1] & 0x1);
		input_report_key(hdata->input, BTN_RIGHT,
			(data[1] & 0x2));
		input_report_key(hdata->input, BTN_MIDDLE,
			(data[1] & 0x4));

		input_sync(hdata->input);

		return 1;

	case U1_SP_ABSOLUTE_REPORT_ID:
		sp_x = get_unaligned_le16(data+2);
		sp_y = get_unaligned_le16(data+4);

		sp_x = sp_x / 8;
		sp_y = sp_y / 8;

		input_report_rel(hdata->input2, REL_X, sp_x);
		input_report_rel(hdata->input2, REL_Y, sp_y);

		input_report_key(hdata->input2, BTN_LEFT,
			data[1] & 0x1);
		input_report_key(hdata->input2, BTN_RIGHT,
			(data[1] & 0x2));
		input_report_key(hdata->input2, BTN_MIDDLE,
			(data[1] & 0x4));

		input_sync(hdata->input2);

		return 1;
	}

	return 0;
}

#ifdef CONFIG_PM
static int alps_post_reset(struct hid_device *hdev)
{
	return u1_read_write_register(hdev, ADDRESS_U1_DEV_CTRL_1,
				NULL, U1_TP_ABS_MODE, false);
}

static int alps_post_resume(struct hid_device *hdev)
{
	return u1_read_write_register(hdev, ADDRESS_U1_DEV_CTRL_1,
				NULL, U1_TP_ABS_MODE, false);
}
#endif /* CONFIG_PM */

static int alps_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct u1_dev *data = hid_get_drvdata(hdev);
	struct input_dev *input = hi->input, *input2;
	struct u1_dev devInfo;
	int ret;
	int res_x, res_y, i;

	data->input = input;

	hid_dbg(hdev, "Opening low level driver\n");
	ret = hid_hw_open(hdev);
	if (ret)
		return ret;

	/* Allow incoming hid reports */
	hid_device_io_start(hdev);

	/* Device initialization */
	ret = u1_read_write_register(hdev, ADDRESS_U1_DEV_CTRL_1,
			&devInfo.dev_ctrl, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_DEV_CTRL_1 (%d)\n", ret);
		goto exit;
	}

	devInfo.dev_ctrl &= ~U1_DISABLE_DEV;
	devInfo.dev_ctrl |= U1_TP_ABS_MODE;
	ret = u1_read_write_register(hdev, ADDRESS_U1_DEV_CTRL_1,
			NULL, devInfo.dev_ctrl, false);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to change TP mode (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_NUM_SENS_X,
			&devInfo.sen_line_num_x, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_NUM_SENS_X (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_NUM_SENS_Y,
			&devInfo.sen_line_num_y, 0, true);
		if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_NUM_SENS_Y (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_PITCH_SENS_X,
			&devInfo.pitch_x, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_PITCH_SENS_X (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_PITCH_SENS_Y,
			&devInfo.pitch_y, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_PITCH_SENS_Y (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_RESO_DWN_ABS,
		&devInfo.resolution, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_RESO_DWN_ABS (%d)\n", ret);
		goto exit;
	}

	ret = u1_read_write_register(hdev, ADDRESS_U1_PAD_BTN,
			&devInfo.btn_info, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_PAD_BTN (%d)\n", ret);
		goto exit;
	}

	/* Check StickPointer device */
	ret = u1_read_write_register(hdev, ADDRESS_U1_DEVICE_TYP,
			&devInfo.dev_type, 0, true);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed U1_DEVICE_TYP (%d)\n", ret);
		goto exit;
	}

	devInfo.x_active_len_mm =
		(devInfo.pitch_x * (devInfo.sen_line_num_x - 1)) / 10;
	devInfo.y_active_len_mm =
		(devInfo.pitch_y * (devInfo.sen_line_num_y - 1)) / 10;

	devInfo.x_max =
		(devInfo.resolution << 2) * (devInfo.sen_line_num_x - 1);
	devInfo.y_max =
		(devInfo.resolution << 2) * (devInfo.sen_line_num_y - 1);

	__set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_MT_POSITION_X, 1, devInfo.x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 1, devInfo.y_max, 0, 0);

	if (devInfo.x_active_len_mm && devInfo.y_active_len_mm) {
		res_x = (devInfo.x_max - 1) / devInfo.x_active_len_mm;
		res_y = (devInfo.y_max - 1) / devInfo.y_active_len_mm;

		input_abs_set_res(input, ABS_MT_POSITION_X, res_x);
		input_abs_set_res(input, ABS_MT_POSITION_Y, res_y);
	}

	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 64, 0, 0);

	input_mt_init_slots(input, MAX_TOUCHES, INPUT_MT_POINTER);

	__set_bit(EV_KEY, input->evbit);
	if ((devInfo.btn_info & 0x0F) == (devInfo.btn_info & 0xF0) >> 4) {
		devInfo.btn_cnt = (devInfo.btn_info & 0x0F);
	} else {
		/* Button pad */
		devInfo.btn_cnt = 1;
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);
	}

	for (i = 0; i < devInfo.btn_cnt; i++)
		__set_bit(BTN_LEFT + i, input->keybit);


	/* Stick device initialization */
	if (devInfo.dev_type & U1_DEVTYPE_SP_SUPPORT) {

		input2 = input_allocate_device();
		if (!input2) {
			input_free_device(input2);
			goto exit;
		}

		data->input2 = input2;

		devInfo.dev_ctrl |= U1_SP_ABS_MODE;
		ret = u1_read_write_register(hdev, ADDRESS_U1_DEV_CTRL_1,
			NULL, devInfo.dev_ctrl, false);
		if (ret < 0) {
			dev_err(&hdev->dev, "failed SP mode (%d)\n", ret);
			input_free_device(input2);
			goto exit;
		}

		ret = u1_read_write_register(hdev, ADDRESS_U1_SP_BTN,
			&devInfo.sp_btn_info, 0, true);
		if (ret < 0) {
			dev_err(&hdev->dev, "failed U1_SP_BTN (%d)\n", ret);
			input_free_device(input2);
			goto exit;
		}

		input2->phys = input->phys;
		input2->name = "DualPoint Stick";
		input2->id.bustype = BUS_I2C;
		input2->id.vendor  = input->id.vendor;
		input2->id.product = input->id.product;
		input2->id.version = input->id.version;
		input2->dev.parent = input->dev.parent;

		__set_bit(EV_KEY, input2->evbit);
		devInfo.sp_btn_cnt = (devInfo.sp_btn_info & 0x0F);
		for (i = 0; i < devInfo.sp_btn_cnt; i++)
			__set_bit(BTN_LEFT + i, input2->keybit);

		__set_bit(EV_REL, input2->evbit);
		__set_bit(REL_X, input2->relbit);
		__set_bit(REL_Y, input2->relbit);
		__set_bit(INPUT_PROP_POINTER, input2->propbit);
		__set_bit(INPUT_PROP_POINTING_STICK, input2->propbit);

		if (input_register_device(data->input2)) {
			input_free_device(input2);
			goto exit;
		}
	}

exit:
	hid_device_io_stop(hdev);
	hid_hw_close(hdev);
	return ret;
}

static int alps_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	return -1;
}

static int alps_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct u1_dev *data = NULL;
	int ret;

	data = devm_kzalloc(&hdev->dev, sizeof(struct u1_dev), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->hdev = hdev;
	hid_set_drvdata(hdev, data);

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static void alps_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
}

static const struct hid_device_id alps_id[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY,
		USB_VENDOR_ID_ALPS_JP, HID_DEVICE_ID_ALPS_U1_DUAL) },
	{ }
};
MODULE_DEVICE_TABLE(hid, alps_id);

static struct hid_driver alps_driver = {
	.name = "hid-alps",
	.id_table		= alps_id,
	.probe			= alps_probe,
	.remove			= alps_remove,
	.raw_event		= alps_raw_event,
	.input_mapping		= alps_input_mapping,
	.input_configured	= alps_input_configured,
#ifdef CONFIG_PM
	.resume			= alps_post_resume,
	.reset_resume		= alps_post_reset,
#endif
};

module_hid_driver(alps_driver);

MODULE_AUTHOR("Masaki Ota <masaki.ota@jp.alps.com>");
MODULE_DESCRIPTION("ALPS HID driver");
MODULE_LICENSE("GPL");
