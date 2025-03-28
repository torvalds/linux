/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/bitfield.h>
#include <linux/hid.h>
#include <linux/hid-over-i2c.h>

#include "intel-thc-dev.h"
#include "intel-thc-dma.h"

#include "quicki2c-dev.h"
#include "quicki2c-hid.h"
#include "quicki2c-protocol.h"

static int quicki2c_init_write_buf(struct quicki2c_device *qcdev, u32 cmd, int cmd_len,
				   bool append_data_reg, u8 *data, int data_len,
				   u8 *write_buf, int write_buf_len)
{
	int buf_len, offset = 0;

	buf_len = HIDI2C_REG_LEN + cmd_len;

	if (append_data_reg)
		buf_len += HIDI2C_REG_LEN;

	if (data && data_len)
		buf_len += data_len + HIDI2C_LENGTH_LEN;

	if (buf_len > write_buf_len)
		return -EINVAL;

	memcpy(write_buf, &qcdev->dev_desc.cmd_reg, HIDI2C_REG_LEN);
	offset += HIDI2C_REG_LEN;
	memcpy(write_buf + offset, &cmd, cmd_len);
	offset += cmd_len;

	if (append_data_reg) {
		memcpy(write_buf + offset, &qcdev->dev_desc.data_reg, HIDI2C_REG_LEN);
		offset += HIDI2C_REG_LEN;
	}

	if (data && data_len) {
		__le16 len = cpu_to_le16(data_len + HIDI2C_LENGTH_LEN);

		memcpy(write_buf + offset, &len, HIDI2C_LENGTH_LEN);
		offset += HIDI2C_LENGTH_LEN;
		memcpy(write_buf + offset, data, data_len);
	}

	return buf_len;
}

static int quicki2c_encode_cmd(struct quicki2c_device *qcdev, u32 *cmd_buf,
			       u8 opcode, u8 report_type, u8 report_id)
{
	int cmd_len;

	*cmd_buf = FIELD_PREP(HIDI2C_CMD_OPCODE, opcode) |
		   FIELD_PREP(HIDI2C_CMD_REPORT_TYPE, report_type);

	if (report_id < HIDI2C_CMD_MAX_RI) {
		*cmd_buf |= FIELD_PREP(HIDI2C_CMD_REPORT_ID, report_id);
		cmd_len = HIDI2C_CMD_LEN;
	} else {
		*cmd_buf |= FIELD_PREP(HIDI2C_CMD_REPORT_ID, HIDI2C_CMD_MAX_RI) |
			    FIELD_PREP(HIDI2C_CMD_3RD_BYTE, report_id);
		cmd_len = HIDI2C_CMD_LEN_OPT;
	}

	return cmd_len;
}

static int write_cmd_to_txdma(struct quicki2c_device *qcdev, int opcode,
			      int report_type, int report_id, u8 *buf, int buf_len)
{
	size_t write_buf_len;
	int cmd_len, ret;
	u32 cmd;

	cmd_len = quicki2c_encode_cmd(qcdev, &cmd, opcode, report_type, report_id);

	ret = quicki2c_init_write_buf(qcdev, cmd, cmd_len, buf ? true : false, buf,
				      buf_len, qcdev->report_buf, qcdev->report_len);
	if (ret < 0)
		return ret;

	write_buf_len = ret;

	return thc_dma_write(qcdev->thc_hw, qcdev->report_buf, write_buf_len);
}

int quicki2c_set_power(struct quicki2c_device *qcdev, enum hidi2c_power_state power_state)
{
	return write_cmd_to_txdma(qcdev, HIDI2C_SET_POWER, HIDI2C_RESERVED, power_state, NULL, 0);
}

int quicki2c_get_device_descriptor(struct quicki2c_device *qcdev)
{
	u32 read_len = 0;
	int ret;

	ret = thc_tic_pio_write_and_read(qcdev->thc_hw, qcdev->hid_desc_addr,
					 HIDI2C_REG_LEN, NULL, HIDI2C_DEV_DESC_LEN,
					 &read_len, (u32 *)&qcdev->dev_desc);
	if (ret || HIDI2C_DEV_DESC_LEN != read_len) {
		dev_err_once(qcdev->dev, "Get device descriptor failed, ret %d, read len %u\n",
			     ret, read_len);
		return -EIO;
	}

	if (le16_to_cpu(qcdev->dev_desc.bcd_ver) != HIDI2C_HID_DESC_BCDVERSION)
		return -EOPNOTSUPP;

	return 0;
}

int quicki2c_get_report_descriptor(struct quicki2c_device *qcdev)
{
	u16 desc_reg = le16_to_cpu(qcdev->dev_desc.report_desc_reg);
	size_t read_len = le16_to_cpu(qcdev->dev_desc.report_desc_len);
	u32 prd_len = read_len;

	return thc_swdma_read(qcdev->thc_hw, (u8 *)&desc_reg, HIDI2C_REG_LEN,
			      &prd_len, qcdev->report_descriptor, &read_len);
}

int quicki2c_get_report(struct quicki2c_device *qcdev, u8 report_type,
			unsigned int reportnum, void *buf, u32 buf_len)
{
	struct hidi2c_report_packet *rpt;
	size_t write_buf_len, read_len = 0;
	int cmd_len, rep_type;
	u32 cmd;
	int ret;

	if (report_type == HID_INPUT_REPORT) {
		rep_type = HIDI2C_INPUT;
	} else if (report_type == HID_FEATURE_REPORT) {
		rep_type = HIDI2C_FEATURE;
	} else {
		dev_err(qcdev->dev, "Unsupported report type for GET REPORT: %d\n", report_type);
		return -EINVAL;
	}

	cmd_len = quicki2c_encode_cmd(qcdev, &cmd, HIDI2C_GET_REPORT, rep_type, reportnum);

	ret = quicki2c_init_write_buf(qcdev, cmd, cmd_len, true, NULL, 0,
				      qcdev->report_buf, qcdev->report_len);
	if (ret < 0)
		return ret;

	write_buf_len = ret;

	rpt = (struct hidi2c_report_packet *)qcdev->input_buf;

	ret = thc_swdma_read(qcdev->thc_hw, qcdev->report_buf, write_buf_len,
			     NULL, rpt, &read_len);
	if (ret) {
		dev_err_once(qcdev->dev, "Get report failed, ret %d, read len (%zu vs %d)\n",
			     ret, read_len, buf_len);
		return ret;
	}

	if (HIDI2C_DATA_LEN(le16_to_cpu(rpt->len)) != buf_len || rpt->data[0] != reportnum) {
		dev_err_once(qcdev->dev, "Invalid packet, len (%d vs %d) report id (%d vs %d)\n",
			     le16_to_cpu(rpt->len), buf_len, rpt->data[0], reportnum);
		return -EINVAL;
	}

	memcpy(buf, rpt->data, buf_len);

	return buf_len;
}

int quicki2c_set_report(struct quicki2c_device *qcdev, u8 report_type,
			unsigned int reportnum, void *buf, u32 buf_len)
{
	int rep_type;
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		rep_type = HIDI2C_OUTPUT;
	} else if (report_type == HID_FEATURE_REPORT) {
		rep_type = HIDI2C_FEATURE;
	} else {
		dev_err(qcdev->dev, "Unsupported report type for SET REPORT: %d\n", report_type);
		return -EINVAL;
	}

	ret = write_cmd_to_txdma(qcdev, HIDI2C_SET_REPORT, rep_type, reportnum, buf, buf_len);
	if (ret) {
		dev_err_once(qcdev->dev, "Set Report failed, ret %d\n", ret);
		return ret;
	}

	return buf_len;
}

#define HIDI2C_RESET_TIMEOUT		5

int quicki2c_reset(struct quicki2c_device *qcdev)
{
	int ret;

	qcdev->reset_ack = false;
	qcdev->state = QUICKI2C_RESETING;

	ret = write_cmd_to_txdma(qcdev, HIDI2C_RESET, HIDI2C_RESERVED, 0, NULL, 0);
	if (ret) {
		dev_err_once(qcdev->dev, "Send reset command failed, ret %d\n", ret);
		return ret;
	}

	ret = wait_event_interruptible_timeout(qcdev->reset_ack_wq, qcdev->reset_ack,
					       HIDI2C_RESET_TIMEOUT * HZ);
	if (ret <= 0 || !qcdev->reset_ack) {
		dev_err_once(qcdev->dev,
			     "Wait reset response timed out ret:%d timeout:%ds\n",
			     ret, HIDI2C_RESET_TIMEOUT);
		return -ETIMEDOUT;
	}

	return 0;
}
