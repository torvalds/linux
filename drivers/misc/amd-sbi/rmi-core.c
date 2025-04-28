// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sbrmi-core.c - file defining SB-RMI protocols compliant
 *		  AMD SoC device.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include "rmi-core.h"

/* Mask for Status Register bit[1] */
#define SW_ALERT_MASK	0x2

/* Software Interrupt for triggering */
#define START_CMD	0x80
#define TRIGGER_MAILBOX	0x01

int rmi_mailbox_xfer(struct sbrmi_data *data,
		     struct sbrmi_mailbox_msg *msg)
{
	unsigned int bytes;
	int i, ret, retry = 10;
	int sw_status;
	u8 byte;

	mutex_lock(&data->lock);

	/* Indicate firmware a command is to be serviced */
	ret = regmap_write(data->regmap, SBRMI_INBNDMSG7, START_CMD);
	if (ret < 0)
		goto exit_unlock;

	/* Write the command to SBRMI::InBndMsg_inst0 */
	ret = regmap_write(data->regmap, SBRMI_INBNDMSG0, msg->cmd);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * For both read and write the initiator (BMC) writes
	 * Command Data In[31:0] to SBRMI::InBndMsg_inst[4:1]
	 * SBRMI_x3C(MSB):SBRMI_x39(LSB)
	 */
	for (i = 0; i < 4; i++) {
		byte = (msg->data_in >> i * 8) & 0xff;
		ret = regmap_write(data->regmap, SBRMI_INBNDMSG1 + i, byte);
		if (ret < 0)
			goto exit_unlock;
	}

	/*
	 * Write 0x01 to SBRMI::SoftwareInterrupt to notify firmware to
	 * perform the requested read or write command
	 */
	ret = regmap_write(data->regmap, SBRMI_SW_INTERRUPT, TRIGGER_MAILBOX);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * Firmware will write SBRMI::Status[SwAlertSts]=1 to generate
	 * an ALERT (if enabled) to initiator (BMC) to indicate completion
	 * of the requested command
	 */
	do {
		ret = regmap_read(data->regmap, SBRMI_STATUS, &sw_status);
		if (sw_status < 0) {
			ret = sw_status;
			goto exit_unlock;
		}
		if (sw_status & SW_ALERT_MASK)
			break;
		usleep_range(50, 100);
	} while (retry--);

	if (retry < 0) {
		ret = -EIO;
		goto exit_unlock;
	}

	/*
	 * For a read operation, the initiator (BMC) reads the firmware
	 * response Command Data Out[31:0] from SBRMI::OutBndMsg_inst[4:1]
	 * {SBRMI_x34(MSB):SBRMI_x31(LSB)}.
	 */
	if (msg->read) {
		for (i = 0; i < 4; i++) {
			ret = regmap_read(data->regmap,
					  SBRMI_OUTBNDMSG1 + i, &bytes);
			if (ret < 0)
				goto exit_unlock;
			msg->data_out |= bytes << i * 8;
		}
	}

	/*
	 * BMC must write 1'b1 to SBRMI::Status[SwAlertSts] to clear the
	 * ALERT to initiator
	 */
	ret = regmap_write(data->regmap, SBRMI_STATUS,
			   sw_status | SW_ALERT_MASK);

exit_unlock:
	mutex_unlock(&data->lock);
	return ret;
}
