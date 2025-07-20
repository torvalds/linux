/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright © 2024 Intel Corporation */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/hid.h>

#include "intel-thc-dev.h"
#include "intel-thc-dma.h"

#include "quickspi-dev.h"
#include "quickspi-hid.h"
#include "quickspi-protocol.h"

/* THC uses HW to accelerate HID over SPI protocol, THC_M_PRT_DEV_INT_CAUSE
 * register is used to store message header and body header, below definition
 * let driver retrieve needed data filed easier from THC_M_PRT_DEV_INT_CAUSE
 * register.
 */
#define HIDSPI_IN_REP_BDY_HDR_REP_TYPE     GENMASK(7, 0)

static int write_cmd_to_txdma(struct quickspi_device *qsdev,
			      int report_type, int report_id,
			      u8 *report_buf, const int report_buf_len)
{
	struct output_report *write_buf;
	int write_buf_len;
	int ret;

	write_buf = (struct output_report *)qsdev->report_buf;

	write_buf->output_hdr.report_type = report_type;
	write_buf->output_hdr.content_len = cpu_to_le16(report_buf_len);
	write_buf->output_hdr.content_id = report_id;

	if (report_buf && report_buf_len > 0)
		memcpy(write_buf->content, report_buf, report_buf_len);

	write_buf_len = HIDSPI_OUTPUT_REPORT_SIZE(report_buf_len);

	ret = thc_dma_write(qsdev->thc_hw, write_buf, write_buf_len);
	if (ret)
		dev_err_once(qsdev->dev, "DMA write failed, ret = %d\n", ret);

	return ret;
}

static int quickspi_get_device_descriptor(struct quickspi_device *qsdev)
{
	u8 read_buf[HIDSPI_INPUT_DEVICE_DESCRIPTOR_SIZE];
	struct output_report output_rep;
	u32 input_len, read_len = 0;
	u32 int_cause_val;
	u8 input_rep_type;
	int ret;

	output_rep.output_hdr.report_type = DEVICE_DESCRIPTOR;
	output_rep.output_hdr.content_len = 0;
	output_rep.output_hdr.content_id = 0;

	qsdev->nondma_int_received = false;

	ret = thc_tic_pio_write(qsdev->thc_hw, qsdev->output_report_addr,
				HIDSPI_OUTPUT_REPORT_SIZE(0), (u32 *)&output_rep);
	if (ret) {
		dev_err_once(qsdev->dev,
			     "Write DEVICE_DESCRIPTOR command failed, ret = %d\n", ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(qsdev->nondma_int_received_wq,
					       qsdev->nondma_int_received,
					       QUICKSPI_ACK_WAIT_TIMEOUT * HZ);
	if (ret <= 0 || !qsdev->nondma_int_received) {
		dev_err_once(qsdev->dev, "Wait DEVICE_DESCRIPTOR timeout, ret:%d\n", ret);
		return -ETIMEDOUT;
	}
	qsdev->nondma_int_received = false;

	int_cause_val = thc_int_cause_read(qsdev->thc_hw);
	input_len = FIELD_GET(HIDSPI_INPUT_HEADER_REPORT_LEN, int_cause_val);

	input_len = input_len * sizeof(u32);
	if (input_len != HIDSPI_INPUT_DEVICE_DESCRIPTOR_SIZE) {
		dev_err_once(qsdev->dev, "Receive wrong DEVICE_DESCRIPTOR length, len = %u\n",
			     input_len);
		return -EINVAL;
	}

	ret = thc_tic_pio_read(qsdev->thc_hw, qsdev->input_report_bdy_addr,
			       input_len, &read_len, (u32 *)read_buf);
	if (ret || read_len != input_len) {
		dev_err_once(qsdev->dev, "Read DEVICE_DESCRIPTOR failed, ret = %d\n", ret);
		dev_err_once(qsdev->dev, "DEVICE_DESCRIPTOR expected len = %u, actual read = %u\n",
			     input_len, read_len);
		return ret;
	}

	input_rep_type = ((struct input_report_body_header *)read_buf)->input_report_type;

	if (input_rep_type == DEVICE_DESCRIPTOR_RESPONSE) {
		memcpy(&qsdev->dev_desc,
		       read_buf + HIDSPI_INPUT_BODY_HEADER_SIZE,
		       HIDSPI_DEVICE_DESCRIPTOR_SIZE);

		return 0;
	}

	dev_err_once(qsdev->dev, "Unexpected input report type: %d\n", input_rep_type);
	return -EINVAL;
}

int quickspi_get_report_descriptor(struct quickspi_device *qsdev)
{
	int ret;

	ret = write_cmd_to_txdma(qsdev, REPORT_DESCRIPTOR, 0, NULL, 0);
	if (ret) {
		dev_err_once(qsdev->dev,
			     "Write REPORT_DESCRIPTOR command failed, ret = %d\n", ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(qsdev->report_desc_got_wq,
					       qsdev->report_desc_got,
					       QUICKSPI_ACK_WAIT_TIMEOUT * HZ);
	if (ret <= 0 || !qsdev->report_desc_got) {
		dev_err_once(qsdev->dev, "Wait Report Descriptor timeout, ret:%d\n", ret);
		return -ETIMEDOUT;
	}
	qsdev->report_desc_got = false;

	return 0;
}

int quickspi_set_power(struct quickspi_device *qsdev,
		       enum hidspi_power_state power_state)
{
	u8 cmd_content = power_state;
	int ret;

	ret = write_cmd_to_txdma(qsdev, COMMAND_CONTENT,
				 HIDSPI_SET_POWER_CMD_ID,
				 &cmd_content,
				 sizeof(cmd_content));
	if (ret) {
		dev_err_once(qsdev->dev, "Write SET_POWER command failed, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

void quickspi_handle_input_data(struct quickspi_device *qsdev, u32 buf_len)
{
	struct input_report_body_header *body_hdr;
	struct input_report_body *input_body;
	u8 *input_report;
	u32 input_len;
	int ret = 0;

	input_body = (struct input_report_body *)qsdev->input_buf;
	body_hdr = &input_body->body_hdr;
	input_len = le16_to_cpu(body_hdr->content_len);

	if (HIDSPI_INPUT_BODY_SIZE(input_len) > buf_len) {
		dev_err_once(qsdev->dev, "Wrong input report length: %u",
			     input_len);
		return;
	}

	switch (body_hdr->input_report_type) {
	case REPORT_DESCRIPTOR_RESPONSE:
		if (input_len != le16_to_cpu(qsdev->dev_desc.rep_desc_len)) {
			dev_err_once(qsdev->dev, "Unexpected report descriptor length: %u\n",
				     input_len);
			return;
		}

		memcpy(qsdev->report_descriptor, input_body->content, input_len);

		qsdev->report_desc_got = true;
		wake_up_interruptible(&qsdev->report_desc_got_wq);

		break;

	case COMMAND_RESPONSE:
		if (body_hdr->content_id == HIDSPI_SET_POWER_CMD_ID) {
			dev_dbg(qsdev->dev, "Receive set power on response\n");
		} else {
			dev_err_once(qsdev->dev, "Unknown command response type: %u\n",
				     body_hdr->content_id);
		}

		break;

	case RESET_RESPONSE:
		if (qsdev->state == QUICKSPI_RESETING) {
			qsdev->reset_ack = true;
			wake_up_interruptible(&qsdev->reset_ack_wq);
			dev_dbg(qsdev->dev, "Receive HIR reset response\n");
		} else {
			dev_info(qsdev->dev, "Receive DIR\n");
		}
		break;

	case GET_FEATURE_RESPONSE:
	case GET_INPUT_REPORT_RESPONSE:
		qsdev->report_len = sizeof(body_hdr->content_id) + input_len;
		input_report = input_body->content - sizeof(body_hdr->content_id);

		memcpy(qsdev->report_buf, input_report, qsdev->report_len);

		qsdev->get_report_cmpl = true;
		wake_up_interruptible(&qsdev->get_report_cmpl_wq);

		break;

	case SET_FEATURE_RESPONSE:
	case OUTPUT_REPORT_RESPONSE:
		qsdev->set_report_cmpl = true;
		wake_up_interruptible(&qsdev->set_report_cmpl_wq);

		break;

	case DATA:
		if (qsdev->state != QUICKSPI_ENABLED)
			return;

		if (input_len > le16_to_cpu(qsdev->dev_desc.max_input_len)) {
			dev_err_once(qsdev->dev, "Unexpected too large input report length: %u\n",
				     input_len);
			return;
		}

		input_len = sizeof(body_hdr->content_id) + input_len;
		input_report = input_body->content - sizeof(body_hdr->content_id);

		ret = quickspi_hid_send_report(qsdev, input_report, input_len);
		if (ret)
			dev_err_once(qsdev->dev, "Failed to send HID input report: %d\n", ret);

		break;

	default:
		dev_err_once(qsdev->dev, "Unsupported input report type: %u\n",
			     body_hdr->input_report_type);
		break;
	}
}

static int acpi_tic_reset(struct quickspi_device *qsdev)
{
	acpi_status status = 0;
	acpi_handle handle;

	if (!qsdev->acpi_dev)
		return -ENODEV;

	handle = acpi_device_handle(qsdev->acpi_dev);
	status = acpi_execute_simple_method(handle, "_RST", 0);
	if (ACPI_FAILURE(status)) {
		dev_err_once(qsdev->dev,
			     "Failed to reset device through ACPI method, ret = %d\n", status);
		return -EIO;
	}

	return 0;
}

int reset_tic(struct quickspi_device *qsdev)
{
	u32 actual_read_len, read_len = 0;
	u32 input_report_len, reset_response, int_cause_val;
	u8  input_rep_type;
	int ret;

	qsdev->state = QUICKSPI_RESETING;

	qsdev->reset_ack = false;

	/* First interrupt uses level trigger to avoid missing interrupt */
	thc_int_trigger_type_select(qsdev->thc_hw, false);

	ret = acpi_tic_reset(qsdev);
	if (ret)
		return ret;

	ret = thc_interrupt_quiesce(qsdev->thc_hw, false);
	if (ret)
		return ret;

	ret = wait_event_interruptible_timeout(qsdev->reset_ack_wq,
					       qsdev->reset_ack,
					       QUICKSPI_ACK_WAIT_TIMEOUT * HZ);
	if (ret <= 0 || !qsdev->reset_ack) {
		dev_err_once(qsdev->dev, "Wait RESET_RESPONSE timeout, ret:%d\n", ret);
		return -ETIMEDOUT;
	}

	int_cause_val = thc_int_cause_read(qsdev->thc_hw);
	input_report_len = FIELD_GET(HIDSPI_INPUT_HEADER_REPORT_LEN, int_cause_val);

	read_len = input_report_len * sizeof(u32);
	if (read_len != HIDSPI_INPUT_BODY_SIZE(0)) {
		dev_err_once(qsdev->dev, "Receive wrong RESET_RESPONSE, len = %u\n",
			     read_len);
		return -EINVAL;
	}

	/* Switch to edge trigger matching with HIDSPI protocol definition */
	thc_int_trigger_type_select(qsdev->thc_hw, true);

	ret = thc_tic_pio_read(qsdev->thc_hw, qsdev->input_report_bdy_addr,
			       read_len, &actual_read_len,
			       (u32 *)&reset_response);
	if (ret || actual_read_len != read_len) {
		dev_err_once(qsdev->dev, "Read RESET_RESPONSE body failed, ret = %d\n", ret);
		dev_err_once(qsdev->dev, "RESET_RESPONSE body expected len = %u, actual = %u\n",
			     read_len, actual_read_len);
		return ret;
	}

	input_rep_type = FIELD_GET(HIDSPI_IN_REP_BDY_HDR_REP_TYPE, reset_response);

	if (input_rep_type == RESET_RESPONSE) {
		dev_dbg(qsdev->dev, "RESET_RESPONSE received\n");
	} else {
		dev_err_once(qsdev->dev,
			     "Unexpected input report type: %d, expect RESET_RESPONSE\n",
			     input_rep_type);
		return -EINVAL;
	}

	qsdev->state = QUICKSPI_RESET;

	ret = quickspi_get_device_descriptor(qsdev);
	if (ret)
		return ret;

	return 0;
}

int quickspi_get_report(struct quickspi_device *qsdev,
			u8 report_type, unsigned int report_id, void *buf)
{
	int rep_type;
	int ret;

	if (report_type == HID_INPUT_REPORT) {
		rep_type = GET_INPUT_REPORT;
	} else if (report_type == HID_FEATURE_REPORT) {
		rep_type = GET_FEATURE;
	} else {
		dev_err_once(qsdev->dev, "Unsupported report type for GET REPORT: %d\n",
			     report_type);
		return -EINVAL;
	}

	ret = write_cmd_to_txdma(qsdev, rep_type, report_id, NULL, 0);
	if (ret) {
		dev_err_once(qsdev->dev, "Write GET_REPORT command failed, ret = %d\n", ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(qsdev->get_report_cmpl_wq,
					       qsdev->get_report_cmpl,
					       QUICKSPI_ACK_WAIT_TIMEOUT * HZ);
	if (ret <= 0 || !qsdev->get_report_cmpl) {
		dev_err_once(qsdev->dev, "Wait Get Report Response timeout, ret:%d\n", ret);
		return -ETIMEDOUT;
	}
	qsdev->get_report_cmpl = false;

	memcpy(buf, qsdev->report_buf, qsdev->report_len);

	return qsdev->report_len;
}

int quickspi_set_report(struct quickspi_device *qsdev,
			u8 report_type, unsigned int report_id,
			void *buf, u32 buf_len)
{
	int rep_type;
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		rep_type = OUTPUT_REPORT;
	} else if (report_type == HID_FEATURE_REPORT) {
		rep_type = SET_FEATURE;
	} else {
		dev_err_once(qsdev->dev, "Unsupported report type for SET REPORT: %d\n",
			     report_type);
		return -EINVAL;
	}

	ret = write_cmd_to_txdma(qsdev, rep_type, report_id, buf + 1, buf_len - 1);
	if (ret) {
		dev_err_once(qsdev->dev, "Write SET_REPORT command failed, ret = %d\n", ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(qsdev->set_report_cmpl_wq,
					       qsdev->set_report_cmpl,
					       QUICKSPI_ACK_WAIT_TIMEOUT * HZ);
	if (ret <= 0 || !qsdev->set_report_cmpl) {
		dev_err_once(qsdev->dev, "Wait Set Report Response timeout, ret:%d\n", ret);
		return -ETIMEDOUT;
	}
	qsdev->set_report_cmpl = false;

	return buf_len;
}
