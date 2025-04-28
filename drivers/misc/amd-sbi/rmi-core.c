// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sbrmi-core.c - file defining SB-RMI protocols compliant
 *		  AMD SoC device.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include "rmi-core.h"

/* Mask for Status Register bit[1] */
#define SW_ALERT_MASK	0x2

/* Software Interrupt for triggering */
#define START_CMD	0x80
#define TRIGGER_MAILBOX	0x01

int rmi_mailbox_xfer(struct sbrmi_data *data,
		     struct apml_mbox_msg *msg)
{
	unsigned int bytes, ec;
	int i, ret;
	int sw_status;
	u8 byte;

	mutex_lock(&data->lock);

	msg->fw_ret_code = 0;

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
	for (i = 0; i < AMD_SBI_MB_DATA_SIZE; i++) {
		byte = (msg->mb_in_out >> i * 8) & 0xff;
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
	ret = regmap_read_poll_timeout(data->regmap, SBRMI_STATUS, sw_status,
				       sw_status & SW_ALERT_MASK, 500, 2000000);
	if (ret)
		goto exit_unlock;

	ret = regmap_read(data->regmap, SBRMI_OUTBNDMSG7, &ec);
	if (ret || ec)
		goto exit_clear_alert;
	/*
	 * For a read operation, the initiator (BMC) reads the firmware
	 * response Command Data Out[31:0] from SBRMI::OutBndMsg_inst[4:1]
	 * {SBRMI_x34(MSB):SBRMI_x31(LSB)}.
	 */
	for (i = 0; i < AMD_SBI_MB_DATA_SIZE; i++) {
		ret = regmap_read(data->regmap,
				  SBRMI_OUTBNDMSG1 + i, &bytes);
		if (ret < 0)
			break;
		msg->mb_in_out |= bytes << i * 8;
	}

exit_clear_alert:
	/*
	 * BMC must write 1'b1 to SBRMI::Status[SwAlertSts] to clear the
	 * ALERT to initiator
	 */
	ret = regmap_write(data->regmap, SBRMI_STATUS,
			   sw_status | SW_ALERT_MASK);
	if (ec) {
		ret = -EPROTOTYPE;
		msg->fw_ret_code = ec;
	}
exit_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int apml_mailbox_xfer(struct sbrmi_data *data, struct apml_mbox_msg __user *arg)
{
	struct apml_mbox_msg msg = { 0 };
	int ret;

	/* Copy the structure from user */
	if (copy_from_user(&msg, arg, sizeof(struct apml_mbox_msg)))
		return -EFAULT;

	/* Mailbox protocol */
	ret = rmi_mailbox_xfer(data, &msg);
	if (ret && ret != -EPROTOTYPE)
		return ret;

	return copy_to_user(arg, &msg, sizeof(struct apml_mbox_msg));
}

static long sbrmi_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sbrmi_data *data;

	data = container_of(fp->private_data, struct sbrmi_data, sbrmi_misc_dev);
	switch (cmd) {
	case SBRMI_IOCTL_MBOX_CMD:
		return apml_mailbox_xfer(data, argp);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations sbrmi_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= sbrmi_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

int create_misc_rmi_device(struct sbrmi_data *data,
			   struct device *dev)
{
	data->sbrmi_misc_dev.name	= devm_kasprintf(dev,
							 GFP_KERNEL,
							 "sbrmi-%x",
							 data->dev_static_addr);
	data->sbrmi_misc_dev.minor	= MISC_DYNAMIC_MINOR;
	data->sbrmi_misc_dev.fops	= &sbrmi_fops;
	data->sbrmi_misc_dev.parent	= dev;
	data->sbrmi_misc_dev.nodename	= devm_kasprintf(dev,
							 GFP_KERNEL,
							 "sbrmi-%x",
							 data->dev_static_addr);
	data->sbrmi_misc_dev.mode	= 0600;

	return misc_register(&data->sbrmi_misc_dev);
}
