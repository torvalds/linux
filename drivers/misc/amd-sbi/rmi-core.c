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
/* Mask to check H/W Alert status bit */
#define HW_ALERT_MASK	0x80

/* Software Interrupt for triggering */
#define START_CMD	0x80
#define TRIGGER_MAILBOX	0x01

/* Default message lengths as per APML command protocol */
/* CPUID */
#define CPUID_RD_DATA_LEN	0x8
#define CPUID_WR_DATA_LEN	0x8
#define CPUID_RD_REG_LEN	0xa
#define CPUID_WR_REG_LEN	0x9
/* MSR */
#define MSR_RD_REG_LEN		0xa
#define MSR_WR_REG_LEN		0x8
#define MSR_RD_DATA_LEN		0x8
#define MSR_WR_DATA_LEN		0x7

/* CPUID MSR Command Ids */
#define CPUID_MCA_CMD	0x73
#define RD_CPUID_CMD	0x91
#define RD_MCA_CMD	0x86

/* CPUID MCAMSR mask & index */
#define CPUID_MCA_THRD_INDEX	32
#define CPUID_MCA_FUNC_MASK	GENMASK(31, 0)
#define CPUID_EXT_FUNC_INDEX	56

/* input for bulk write to CPUID protocol */
struct cpu_msr_indata {
	u8 wr_len;	/* const value */
	u8 rd_len;	/* const value */
	u8 proto_cmd;	/* const value */
	u8 thread;	/* thread number */
	union {
		u8 reg_offset[4];	/* input value */
		u32 value;
	} __packed;
	u8 ext; /* extended function */
};

/* output for bulk read from CPUID protocol */
struct cpu_msr_outdata {
	u8 num_bytes;	/* number of bytes return */
	u8 status;	/* Protocol status code */
	union {
		u64 value;
		u8 reg_data[8];
	} __packed;
};

static inline void prepare_cpuid_input_message(struct cpu_msr_indata *input,
					       u8 thread_id, u32 func,
					       u8 ext_func)
{
	input->rd_len		= CPUID_RD_DATA_LEN;
	input->wr_len		= CPUID_WR_DATA_LEN;
	input->proto_cmd	= RD_CPUID_CMD;
	input->thread		= thread_id << 1;
	input->value		= func;
	input->ext		= ext_func;
}

static inline void prepare_mca_msr_input_message(struct cpu_msr_indata *input,
						 u8 thread_id, u32 data_in)
{
	input->rd_len		= MSR_RD_DATA_LEN;
	input->wr_len		= MSR_WR_DATA_LEN;
	input->proto_cmd	= RD_MCA_CMD;
	input->thread		= thread_id << 1;
	input->value		= data_in;
}

static int sbrmi_get_rev(struct sbrmi_data *data)
{
	unsigned int rev;
	u16 offset = SBRMI_REV;
	int ret;

	ret = regmap_read(data->regmap, offset, &rev);
	if (ret < 0)
		return ret;

	data->rev = rev;
	return 0;
}

/* Read CPUID function protocol */
static int rmi_cpuid_read(struct sbrmi_data *data,
			  struct apml_cpuid_msg *msg)
{
	struct cpu_msr_indata input = {0};
	struct cpu_msr_outdata output = {0};
	int val = 0;
	int ret, hw_status;
	u16 thread;

	mutex_lock(&data->lock);
	/* cache the rev value to identify if protocol is supported or not */
	if (!data->rev) {
		ret = sbrmi_get_rev(data);
		if (ret < 0)
			goto exit_unlock;
	}
	/* CPUID protocol for REV 0x10 is not supported*/
	if (data->rev == 0x10) {
		ret = -EOPNOTSUPP;
		goto exit_unlock;
	}

	thread = msg->cpu_in_out >> CPUID_MCA_THRD_INDEX;

	/* Thread > 127, Thread128 CS register, 1'b1 needs to be set to 1 */
	if (thread > 127) {
		thread -= 128;
		val = 1;
	}
	ret = regmap_write(data->regmap, SBRMI_THREAD128CS, val);
	if (ret < 0)
		goto exit_unlock;

	prepare_cpuid_input_message(&input, thread,
				    msg->cpu_in_out & CPUID_MCA_FUNC_MASK,
				    msg->cpu_in_out >> CPUID_EXT_FUNC_INDEX);

	ret = regmap_bulk_write(data->regmap, CPUID_MCA_CMD,
				&input, CPUID_WR_REG_LEN);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * For RMI Rev 0x20, new h/w status bit is introduced. which is used
	 * by firmware to indicate completion of commands (0x71, 0x72, 0x73).
	 * wait for the status bit to be set by the hardware before
	 * reading the data out.
	 */
	ret = regmap_read_poll_timeout(data->regmap, SBRMI_STATUS, hw_status,
				       hw_status & HW_ALERT_MASK, 500, 2000000);
	if (ret)
		goto exit_unlock;

	ret = regmap_bulk_read(data->regmap, CPUID_MCA_CMD,
			       &output, CPUID_RD_REG_LEN);
	if (ret < 0)
		goto exit_unlock;

	ret = regmap_write(data->regmap, SBRMI_STATUS,
			   HW_ALERT_MASK);
	if (ret < 0)
		goto exit_unlock;

	if (output.num_bytes != CPUID_RD_REG_LEN - 1) {
		ret = -EMSGSIZE;
		goto exit_unlock;
	}
	if (output.status) {
		ret = -EPROTOTYPE;
		msg->fw_ret_code = output.status;
		goto exit_unlock;
	}
	msg->cpu_in_out = output.value;
exit_unlock:
	if (ret < 0)
		msg->cpu_in_out = 0;
	mutex_unlock(&data->lock);
	return ret;
}

/* MCA MSR protocol */
static int rmi_mca_msr_read(struct sbrmi_data *data,
			    struct apml_mcamsr_msg  *msg)
{
	struct cpu_msr_outdata output = {0};
	struct cpu_msr_indata input = {0};
	int ret, val = 0;
	int hw_status;
	u16 thread;

	mutex_lock(&data->lock);
	/* cache the rev value to identify if protocol is supported or not */
	if (!data->rev) {
		ret = sbrmi_get_rev(data);
		if (ret < 0)
			goto exit_unlock;
	}
	/* MCA MSR protocol for REV 0x10 is not supported*/
	if (data->rev == 0x10) {
		ret = -EOPNOTSUPP;
		goto exit_unlock;
	}

	thread = msg->mcamsr_in_out >> CPUID_MCA_THRD_INDEX;

	/* Thread > 127, Thread128 CS register, 1'b1 needs to be set to 1 */
	if (thread > 127) {
		thread -= 128;
		val = 1;
	}
	ret = regmap_write(data->regmap, SBRMI_THREAD128CS, val);
	if (ret < 0)
		goto exit_unlock;

	prepare_mca_msr_input_message(&input, thread,
				      msg->mcamsr_in_out & CPUID_MCA_FUNC_MASK);

	ret = regmap_bulk_write(data->regmap, CPUID_MCA_CMD,
				&input, MSR_WR_REG_LEN);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * For RMI Rev 0x20, new h/w status bit is introduced. which is used
	 * by firmware to indicate completion of commands (0x71, 0x72, 0x73).
	 * wait for the status bit to be set by the hardware before
	 * reading the data out.
	 */
	ret = regmap_read_poll_timeout(data->regmap, SBRMI_STATUS, hw_status,
				       hw_status & HW_ALERT_MASK, 500, 2000000);
	if (ret)
		goto exit_unlock;

	ret = regmap_bulk_read(data->regmap, CPUID_MCA_CMD,
			       &output, MSR_RD_REG_LEN);
	if (ret < 0)
		goto exit_unlock;

	ret = regmap_write(data->regmap, SBRMI_STATUS,
			   HW_ALERT_MASK);
	if (ret < 0)
		goto exit_unlock;

	if (output.num_bytes != MSR_RD_REG_LEN - 1) {
		ret = -EMSGSIZE;
		goto exit_unlock;
	}
	if (output.status) {
		ret = -EPROTOTYPE;
		msg->fw_ret_code = output.status;
		goto exit_unlock;
	}
	msg->mcamsr_in_out = output.value;

exit_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

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

	/* Clear the input value before updating the output data */
	msg->mb_in_out = 0;

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

static int apml_rmi_reg_xfer(struct sbrmi_data *data,
			     struct apml_reg_xfer_msg __user *arg)
{
	struct apml_reg_xfer_msg msg = { 0 };
	unsigned int data_read;
	int ret;

	/* Copy the structure from user */
	if (copy_from_user(&msg, arg, sizeof(struct apml_reg_xfer_msg)))
		return -EFAULT;

	mutex_lock(&data->lock);
	if (msg.rflag) {
		ret = regmap_read(data->regmap, msg.reg_addr, &data_read);
		if (!ret)
			msg.data_in_out = data_read;
	} else {
		ret = regmap_write(data->regmap, msg.reg_addr, msg.data_in_out);
	}

	mutex_unlock(&data->lock);

	if (msg.rflag && !ret)
		if (copy_to_user(arg, &msg, sizeof(struct apml_reg_xfer_msg)))
			return -EFAULT;
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

	if (copy_to_user(arg, &msg, sizeof(struct apml_mbox_msg)))
		return -EFAULT;
	return ret;
}

static int apml_cpuid_xfer(struct sbrmi_data *data, struct apml_cpuid_msg __user *arg)
{
	struct apml_cpuid_msg msg = { 0 };
	int ret;

	/* Copy the structure from user */
	if (copy_from_user(&msg, arg, sizeof(struct apml_cpuid_msg)))
		return -EFAULT;

	/* CPUID Protocol */
	ret = rmi_cpuid_read(data, &msg);
	if (ret && ret != -EPROTOTYPE)
		return ret;

	if (copy_to_user(arg, &msg, sizeof(struct apml_cpuid_msg)))
		return -EFAULT;
	return ret;
}

static int apml_mcamsr_xfer(struct sbrmi_data *data, struct apml_mcamsr_msg __user *arg)
{
	struct apml_mcamsr_msg msg = { 0 };
	int ret;

	/* Copy the structure from user */
	if (copy_from_user(&msg, arg, sizeof(struct apml_mcamsr_msg)))
		return -EFAULT;

	/* MCAMSR Protocol */
	ret = rmi_mca_msr_read(data, &msg);
	if (ret && ret != -EPROTOTYPE)
		return ret;

	if (copy_to_user(arg, &msg, sizeof(struct apml_mcamsr_msg)))
		return -EFAULT;
	return ret;
}

static long sbrmi_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sbrmi_data *data;

	data = container_of(fp->private_data, struct sbrmi_data, sbrmi_misc_dev);
	switch (cmd) {
	case SBRMI_IOCTL_MBOX_CMD:
		return apml_mailbox_xfer(data, argp);
	case SBRMI_IOCTL_CPUID_CMD:
		return apml_cpuid_xfer(data, argp);
	case SBRMI_IOCTL_MCAMSR_CMD:
		return apml_mcamsr_xfer(data, argp);
	case SBRMI_IOCTL_REG_XFER_CMD:
		return apml_rmi_reg_xfer(data, argp);
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
