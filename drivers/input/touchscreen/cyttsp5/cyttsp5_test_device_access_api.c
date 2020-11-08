/*
 * cyttsp5_test_device_access_api.c
 * Parade TrueTouch(TM) Standard Product V5 Device Access API test module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include <linux/module.h>
#include <linux/cyttsp5_device_access-api.h>
#include <asm/unaligned.h>

#define BUFFER_SIZE		256

#define COMMAND_GET_SYSTEM_INFO		2
#define COMMAND_SUSPEND_SCANNING	3
#define COMMAND_RESUME_SCANNING		4
#define COMMAND_GET_PARAMETER		5
#define COMMAND_SET_PARAMETER		6

#define PARAMETER_ACTIVE_DISTANCE_2	0x0B

struct tt_output_report {
	__le16 reg_address;
	__le16 length;
	u8 report_id;
	u8 reserved;
	u8 command;
	u8 parameters[0];
} __packed;

struct tt_input_report {
	__le16 length;
	u8 report_id;
	u8 reserved;
	u8 command;
	u8 return_data[0];
} __packed;

static int prepare_tt_output_report(struct tt_output_report *out,
		u16 length, u8 command)
{
	put_unaligned_le16(0x04, &out->reg_address);
	put_unaligned_le16(5 + length, &out->length);

	out->report_id = 0x2f;
	out->reserved = 0x00;
	out->command = command;

	return 7 + length;
}

static int check_and_parse_tt_input_report(struct tt_input_report *in,
		u16 *length, u8 *command)
{
	if (in->report_id != 0x1f)
		return -EINVAL;

	*length = get_unaligned_le16(&in->length);
	*command = in->command & 0x7f;

	return 0;
}

static int prepare_get_system_info_report(u8 *buf)
{
	struct tt_output_report *out = (struct tt_output_report *)buf;

	return prepare_tt_output_report(out, 0, COMMAND_GET_SYSTEM_INFO);
}

static int check_get_system_info_response(u8 *buf, u16 read_length)
{
	struct tt_input_report *in = (struct tt_input_report *)buf;
	u16 length = 0;
	u8 command = 0;

	if (read_length != 51)
		return -EINVAL;

	if (check_and_parse_tt_input_report(in, &length, &command)
			|| command != COMMAND_GET_SYSTEM_INFO
			|| length != 51)
		return -EINVAL;

	pr_info("PIP Major Version: %d\n", in->return_data[0]);
	pr_info("PIP Minor Version: %d\n", in->return_data[1]);
	pr_info("Touch Firmware Product Id: %d\n",
			get_unaligned_le16(&in->return_data[2]));
	pr_info("Touch Firmware Major Version: %d\n", in->return_data[4]);
	pr_info("Touch Firmware Minor Version: %d\n", in->return_data[5]);
	pr_info("Touch Firmware Internal Revision Control Number: %d\n",
			get_unaligned_le32(&in->return_data[6]));
	pr_info("Customer Specified Firmware/Configuration Version: %d\n",
			get_unaligned_le16(&in->return_data[10]));
	pr_info("Bootloader Major Version: %d\n", in->return_data[12]);
	pr_info("Bootloader Minor Version: %d\n", in->return_data[13]);
	pr_info("Family ID: 0x%02x\n", in->return_data[14]);
	pr_info("Revision ID: 0x%02x\n", in->return_data[15]);
	pr_info("Silicon ID: 0x%02x\n",
			get_unaligned_le16(&in->return_data[16]));
	pr_info("Parade Manufacturing ID[0]: 0x%02x\n", in->return_data[18]);
	pr_info("Parade Manufacturing ID[1]: 0x%02x\n", in->return_data[19]);
	pr_info("Parade Manufacturing ID[2]: 0x%02x\n", in->return_data[20]);
	pr_info("Parade Manufacturing ID[3]: 0x%02x\n", in->return_data[21]);
	pr_info("Parade Manufacturing ID[4]: 0x%02x\n", in->return_data[22]);
	pr_info("Parade Manufacturing ID[5]: 0x%02x\n", in->return_data[23]);
	pr_info("Parade Manufacturing ID[6]: 0x%02x\n", in->return_data[24]);
	pr_info("Parade Manufacturing ID[7]: 0x%02x\n", in->return_data[25]);
	pr_info("POST Result Code: 0x%02x\n",
			get_unaligned_le16(&in->return_data[26]));

	pr_info("Number of X Electrodes: %d\n", in->return_data[28]);
	pr_info("Number of Y Electrodes: %d\n", in->return_data[29]);
	pr_info("Panel X Axis Length: %d\n",
			get_unaligned_le16(&in->return_data[30]));
	pr_info("Panel Y Axis Length: %d\n",
			get_unaligned_le16(&in->return_data[32]));
	pr_info("Panel X Axis Resolution: %d\n",
			get_unaligned_le16(&in->return_data[34]));
	pr_info("Panel Y Axis Resolution: %d\n",
			get_unaligned_le16(&in->return_data[36]));
	pr_info("Panel Pressure Resolution: %d\n",
			get_unaligned_le16(&in->return_data[38]));
	pr_info("X_ORG: %d\n", in->return_data[40]);
	pr_info("Y_ORG: %d\n", in->return_data[41]);
	pr_info("Panel ID: %d\n", in->return_data[42]);
	pr_info("Buttons: 0x%02x\n", in->return_data[43]);
	pr_info("BAL SELF MC: 0x%02x\n", in->return_data[44]);
	pr_info("Max Number of Touch Records per Refresh Cycle: %d\n",
			in->return_data[45]);

	return 0;
}

static int prepare_get_parameter_report(u8 *buf, u8 parameter_id)
{
	struct tt_output_report *out = (struct tt_output_report *)buf;

	out->parameters[0] = parameter_id;

	return prepare_tt_output_report(out, 1, COMMAND_GET_PARAMETER);
}

static int check_get_parameter_response(u8 *buf, u16 read_length,
		u32 *parameter_value)
{
	struct tt_input_report *in = (struct tt_input_report *)buf;
	u16 length = 0;
	u8 command = 0;
	u32 param_value = 0;
	u8 param_id;
	u8 param_size = 0;

	if (read_length != 8 && read_length != 9 && read_length != 11)
		return -EINVAL;

	if (check_and_parse_tt_input_report(in, &length, &command)
			|| command != COMMAND_GET_PARAMETER
			|| (length != 8 && length != 9 && length != 11))
		return -EINVAL;

	param_id = in->return_data[0];

	param_size = in->return_data[1];

	if (param_size == 1)
		param_value = in->return_data[2];
	else if (param_size == 2)
		param_value = get_unaligned_le16(&in->return_data[2]);
	else if (param_size == 4)
		param_value = get_unaligned_le32(&in->return_data[2]);
	else
		return -EINVAL;

	pr_info("%s: Parameter ID: 0x%02x Value: 0x%02x\n",
		__func__, param_id, param_value);

	if (parameter_value)
		*parameter_value = param_value;

	return 0;
}

static int prepare_set_parameter_report(u8 *buf, u8 parameter_id,
		u8 parameter_size, u32 parameter_value)
{
	struct tt_output_report *out = (struct tt_output_report *)buf;

	out->parameters[0] = parameter_id;
	out->parameters[1] = parameter_size;

	if (parameter_size == 1)
		out->parameters[2] = (u8)parameter_value;
	else if (parameter_size == 2)
		put_unaligned_le16(parameter_value, &out->parameters[2]);
	else if (parameter_size == 4)
		put_unaligned_le32(parameter_value, &out->parameters[2]);
	else
		return -EINVAL;

	return prepare_tt_output_report(out, 2 + parameter_size,
			COMMAND_SET_PARAMETER);
}

static int check_set_parameter_response(u8 *buf, u16 read_length)
{
	struct tt_input_report *in = (struct tt_input_report *)buf;
	u16 length = 0;
	u8 command = 0;
	u8 param_id;
	u8 param_size = 0;

	if (read_length != 7)
		return -EINVAL;

	if (check_and_parse_tt_input_report(in, &length, &command)
			|| command != COMMAND_SET_PARAMETER
			|| length != 7)
		return -EINVAL;

	param_id = in->return_data[0];
	param_size = in->return_data[1];

	pr_info("%s: Parameter ID: 0x%02x Size: 0x%02x\n",
		__func__, param_id, param_size);

	return 0;
}

static int prepare_suspend_scanning_report(u8 *buf)
{
	struct tt_output_report *out = (struct tt_output_report *)buf;

	return prepare_tt_output_report(out, 0, COMMAND_SUSPEND_SCANNING);
}

static int check_suspend_scanning_response(u8 *buf, u16 read_length)
{
	struct tt_input_report *in = (struct tt_input_report *)buf;
	u16 length = 0;
	u8 command = 0;

	if (read_length != 5)
		return -EINVAL;

	if (check_and_parse_tt_input_report(in, &length, &command)
			|| command != COMMAND_SUSPEND_SCANNING
			|| length != 5)
		return -EINVAL;

	return 0;
}

static int prepare_resume_scanning_report(u8 *buf)
{
	struct tt_output_report *out = (struct tt_output_report *)buf;

	return prepare_tt_output_report(out, 0, COMMAND_RESUME_SCANNING);
}

static int check_resume_scanning_response(u8 *buf, u16 read_length)
{
	struct tt_input_report *in = (struct tt_input_report *)buf;
	u16 length = 0;
	u8 command = 0;

	if (read_length != 5)
		return -EINVAL;

	if (check_and_parse_tt_input_report(in, &length, &command)
			|| command != COMMAND_RESUME_SCANNING
			|| length != 5)
		return -EINVAL;

	return 0;
}

void cyttsp5_user_command_async_cont(const char *core_name,
		u16 read_len, u8 *read_buf, u16 write_len, u8 *write_buf,
		u16 actual_read_length, int rc)
{
	if (rc) {
		pr_err("%s: suspend scan fails\n", __func__);
		goto exit;
	}

	rc = check_suspend_scanning_response(read_buf, actual_read_length);
	if (rc) {
		pr_err("%s: check suspend scanning response fails\n", __func__);
		goto exit;
	}

	pr_info("%s: suspend scanning succeeds\n", __func__);
exit:
	return;
}

/* Read and write buffers */
static u8 write_buf[BUFFER_SIZE];
static u8 read_buf[BUFFER_SIZE];

static uint active_distance;
module_param(active_distance, uint, 0);

static int __init cyttsp5_test_device_access_api_init(void)
{
	u32 initial_active_distance;
	u16 actual_read_len;
	int write_len;
	int rc;

	pr_info("%s: Enter\n", __func__);

	/* CASE	1: Run get system information */
	write_len = prepare_get_system_info_report(write_buf);

	rc = cyttsp5_device_access_user_command(NULL, sizeof(read_buf),
			read_buf, write_len, write_buf,
			&actual_read_len);
	if (rc)
		goto exit;

	rc = check_get_system_info_response(read_buf, actual_read_len);
	if (rc)
		goto exit;

	/* CASE 2: Run get parameter (Active distance) */
	write_len = prepare_get_parameter_report(write_buf,
			PARAMETER_ACTIVE_DISTANCE_2);

	rc = cyttsp5_device_access_user_command(NULL, sizeof(read_buf),
			read_buf, write_len, write_buf,
			&actual_read_len);
	if (rc)
		goto exit;

	rc = check_get_parameter_response(read_buf, actual_read_len,
			&initial_active_distance);
	if (rc)
		goto exit;

	pr_info("%s: Initial Active Distance: %d\n",
		__func__, initial_active_distance);

	/* CASE	3: Run set parameter (Active distance) */
	write_len = prepare_set_parameter_report(write_buf,
			PARAMETER_ACTIVE_DISTANCE_2, 1,
			active_distance);

	rc = cyttsp5_device_access_user_command(NULL, sizeof(read_buf),
			read_buf, write_len, write_buf,
			&actual_read_len);
	if (rc)
		goto exit;

	rc = check_set_parameter_response(read_buf, actual_read_len);
	if (rc)
		goto exit;

	pr_info("%s: Active Distance set to %d\n", __func__, active_distance);

	/* CASE	4: Run get parameter (Active distance) */
	write_len = prepare_get_parameter_report(write_buf,
			PARAMETER_ACTIVE_DISTANCE_2);

	rc = cyttsp5_device_access_user_command(NULL, sizeof(read_buf),
			read_buf, write_len, write_buf,
			&actual_read_len);
	if (rc)
		goto exit;

	rc = check_get_parameter_response(read_buf, actual_read_len,
			&active_distance);
	if (rc)
		goto exit;

	pr_info("%s: New Active Distance: %d\n", __func__, active_distance);

	/* CASE 5: Run suspend scanning asynchronously */
	write_len = prepare_suspend_scanning_report(write_buf);

	preempt_disable();
	rc = cyttsp5_device_access_user_command_async(NULL,
			sizeof(read_buf), read_buf, write_len, write_buf,
			cyttsp5_user_command_async_cont);
	preempt_enable();
	if (rc)
		goto exit;
exit:
	return rc;
}
module_init(cyttsp5_test_device_access_api_init);

static void __exit cyttsp5_test_device_access_api_exit(void)
{
	u16 actual_read_len;
	int write_len;
	int rc;

	/* CASE 6: Run resume scanning */
	write_len = prepare_resume_scanning_report(write_buf);

	rc = cyttsp5_device_access_user_command(NULL, sizeof(read_buf),
			read_buf, write_len, write_buf,
			&actual_read_len);
	if (rc)
		goto exit;

	rc = check_resume_scanning_response(read_buf, actual_read_len);
	if (rc)
		goto exit;

	pr_info("%s: resume scanning succeeds\n", __func__);
exit:
	return;
}
module_exit(cyttsp5_test_device_access_api_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product Device Access Driver API Tester");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
