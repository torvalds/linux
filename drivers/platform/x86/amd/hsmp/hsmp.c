// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2022, AMD.
 * All Rights Reserved.
 *
 * This file provides a device implementation for HSMP interface
 */

#include <asm/amd/hsmp.h>

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/sysfs.h>

#include "hsmp.h"

/* HSMP Status / Error codes */
#define HSMP_STATUS_NOT_READY	0x00
#define HSMP_STATUS_OK		0x01
#define HSMP_ERR_INVALID_MSG	0xFE
#define HSMP_ERR_INVALID_INPUT	0xFF
#define HSMP_ERR_PREREQ_NOT_SATISFIED	0xFD
#define HSMP_ERR_SMU_BUSY		0xFC

/* Timeout in millsec */
#define HSMP_MSG_TIMEOUT	100
#define HSMP_SHORT_SLEEP	1

#define HSMP_WR			true
#define HSMP_RD			false

/*
 * When same message numbers are used for both GET and SET operation,
 * bit:31 indicates whether its SET or GET operation.
 */
#define CHECK_GET_BIT		BIT(31)

static struct hsmp_plat_device hsmp_pdev;

/*
 * Send a message to the HSMP port via PCI-e config space registers
 * or by writing to MMIO space.
 *
 * The caller is expected to zero out any unused arguments.
 * If a response is expected, the number of response words should be greater than 0.
 *
 * Returns 0 for success and populates the requested number of arguments.
 * Returns a negative error code for failure.
 */
static int __hsmp_send_message(struct hsmp_socket *sock, struct hsmp_message *msg)
{
	struct hsmp_mbaddr_info *mbinfo;
	unsigned long timeout, short_sleep;
	u32 mbox_status;
	u32 index;
	int ret;

	mbinfo = &sock->mbinfo;

	/* Clear the status register */
	mbox_status = HSMP_STATUS_NOT_READY;
	ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_WR);
	if (ret) {
		dev_err(sock->dev, "Error %d clearing mailbox status register\n", ret);
		return ret;
	}

	index = 0;
	/* Write any message arguments */
	while (index < msg->num_args) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
					  &msg->args[index], HSMP_WR);
		if (ret) {
			dev_err(sock->dev, "Error %d writing message argument %d\n", ret, index);
			return ret;
		}
		index++;
	}

	/* Write the message ID which starts the operation */
	ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_id_off, &msg->msg_id, HSMP_WR);
	if (ret) {
		dev_err(sock->dev, "Error %d writing message ID %u\n", ret, msg->msg_id);
		return ret;
	}

	/*
	 * Depending on when the trigger write completes relative to the SMU
	 * firmware 1 ms cycle, the operation may take from tens of us to 1 ms
	 * to complete. Some operations may take more. Therefore we will try
	 * a few short duration sleeps and switch to long sleeps if we don't
	 * succeed quickly.
	 */
	short_sleep = jiffies + msecs_to_jiffies(HSMP_SHORT_SLEEP);
	timeout	= jiffies + msecs_to_jiffies(HSMP_MSG_TIMEOUT);

	while (true) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_RD);
		if (ret) {
			dev_err(sock->dev, "Error %d reading mailbox status\n", ret);
			return ret;
		}

		if (mbox_status != HSMP_STATUS_NOT_READY)
			break;

		if (!time_before(jiffies, timeout))
			break;

		if (time_before(jiffies, short_sleep))
			usleep_range(50, 100);
		else
			usleep_range(1000, 2000);
	}

	if (unlikely(mbox_status == HSMP_STATUS_NOT_READY)) {
		dev_err(sock->dev, "Message ID 0x%X failure : SMU tmeout (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -ETIMEDOUT;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_MSG)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Invalid message (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -ENOMSG;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_INPUT)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Invalid arguments (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EINVAL;
	} else if (unlikely(mbox_status == HSMP_ERR_PREREQ_NOT_SATISFIED)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Prerequisite not satisfied (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EREMOTEIO;
	} else if (unlikely(mbox_status == HSMP_ERR_SMU_BUSY)) {
		dev_err(sock->dev, "Message ID 0x%X failure : SMU BUSY (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EBUSY;
	} else if (unlikely(mbox_status != HSMP_STATUS_OK)) {
		dev_err(sock->dev, "Message ID 0x%X unknown failure (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EIO;
	}

	/*
	 * SMU has responded OK. Read response data.
	 * SMU reads the input arguments from eight 32 bit registers starting
	 * from SMN_HSMP_MSG_DATA and writes the response data to the same
	 * SMN_HSMP_MSG_DATA address.
	 * We copy the response data if any, back to the args[].
	 */
	index = 0;
	while (index < msg->response_sz) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
					  &msg->args[index], HSMP_RD);
		if (ret) {
			dev_err(sock->dev, "Error %d reading response %u for message ID:%u\n",
				ret, index, msg->msg_id);
			break;
		}
		index++;
	}

	return ret;
}

static int validate_message(struct hsmp_message *msg)
{
	/* msg_id against valid range of message IDs */
	if (msg->msg_id < HSMP_TEST || msg->msg_id >= HSMP_MSG_ID_MAX)
		return -ENOMSG;

	/* msg_id is a reserved message ID */
	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_RSVD)
		return -ENOMSG;

	/*
	 * num_args passed by user should match the num_args specified in
	 * message description table.
	 */
	if (msg->num_args != hsmp_msg_desc_table[msg->msg_id].num_args)
		return -EINVAL;

	/*
	 * Some older HSMP SET messages are updated to add GET in the same message.
	 * In these messages, GET returns the current value and SET also returns
	 * the successfully set value. To support this GET and SET in same message
	 * while maintaining backward compatibility for the HSMP users,
	 * hsmp_msg_desc_table[] indicates only maximum allowed response_sz.
	 */
	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_SET_GET) {
		if (msg->response_sz > hsmp_msg_desc_table[msg->msg_id].response_sz)
			return -EINVAL;
	} else {
		/* only HSMP_SET or HSMP_GET messages go through this strict check */
		if (msg->response_sz != hsmp_msg_desc_table[msg->msg_id].response_sz)
			return -EINVAL;
	}
	return 0;
}

int hsmp_send_message(struct hsmp_message *msg)
{
	struct hsmp_socket *sock;
	int ret;

	if (!msg)
		return -EINVAL;
	ret = validate_message(msg);
	if (ret)
		return ret;

	if (!hsmp_pdev.sock || msg->sock_ind >= hsmp_pdev.num_sockets)
		return -ENODEV;
	sock = &hsmp_pdev.sock[msg->sock_ind];

	ret = down_interruptible(&sock->hsmp_sem);
	if (ret < 0)
		return ret;

	ret = __hsmp_send_message(sock, msg);

	up(&sock->hsmp_sem);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hsmp_send_message, "AMD_HSMP");

int hsmp_msg_get_nargs(u16 sock_ind, u32 msg_id, u32 *data, u8 num_args)
{
	struct hsmp_message msg = {};
	unsigned int i;
	int ret;

	if (!data)
		return -EINVAL;
	msg.msg_id = msg_id;
	msg.sock_ind = sock_ind;
	msg.response_sz = num_args;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	for (i = 0; i < num_args; i++)
		data[i] = msg.args[i];

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hsmp_msg_get_nargs, "AMD_HSMP");

int hsmp_test(u16 sock_ind, u32 value)
{
	struct hsmp_message msg = { 0 };
	int ret;

	/*
	 * Test the hsmp port by performing TEST command. The test message
	 * takes one argument and returns the value of that argument + 1.
	 */
	msg.msg_id	= HSMP_TEST;
	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.args[0]	= value;
	msg.sock_ind	= sock_ind;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	/* Check the response value */
	if (msg.args[0] != (value + 1)) {
		dev_err(hsmp_pdev.sock[sock_ind].dev,
			"Socket %d test message failed, Expected 0x%08X, received 0x%08X\n",
			sock_ind, (value + 1), msg.args[0]);
		return -EBADE;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hsmp_test, "AMD_HSMP");

static bool is_get_msg(struct hsmp_message *msg)
{
	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_GET)
		return true;

	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_SET_GET &&
	    (msg->args[0] & CHECK_GET_BIT))
		return true;

	return false;
}

long hsmp_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int __user *arguser = (int  __user *)arg;
	struct hsmp_message msg = { 0 };
	int ret;

	if (copy_struct_from_user(&msg, sizeof(msg), arguser, sizeof(struct hsmp_message)))
		return -EFAULT;

	/*
	 * Check msg_id is within the range of supported msg ids
	 * i.e within the array bounds of hsmp_msg_desc_table
	 */
	if (msg.msg_id < HSMP_TEST || msg.msg_id >= HSMP_MSG_ID_MAX)
		return -ENOMSG;

	switch (fp->f_mode & (FMODE_WRITE | FMODE_READ)) {
	case FMODE_WRITE:
		/*
		 * Device is opened in O_WRONLY mode
		 * Execute only set/configure commands
		 */
		if (is_get_msg(&msg))
			return -EPERM;
		break;
	case FMODE_READ:
		/*
		 * Device is opened in O_RDONLY mode
		 * Execute only get/monitor commands
		 */
		if (!is_get_msg(&msg))
			return -EPERM;
		break;
	case FMODE_READ | FMODE_WRITE:
		/*
		 * Device is opened in O_RDWR mode
		 * Execute both get/monitor and set/configure commands
		 */
		break;
	default:
		return -EPERM;
	}

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	if (hsmp_msg_desc_table[msg.msg_id].response_sz > 0) {
		/* Copy results back to user for get/monitor commands */
		if (copy_to_user(arguser, &msg, sizeof(struct hsmp_message)))
			return -EFAULT;
	}

	return 0;
}

ssize_t hsmp_metric_tbl_read(struct hsmp_socket *sock, char *buf, size_t size)
{
	struct hsmp_message msg = { 0 };
	int ret;

	if (!sock || !buf)
		return -EINVAL;

	if (!sock->metric_tbl_addr) {
		dev_err(sock->dev, "Metrics table address not available\n");
		return -ENOMEM;
	}

	/* Do not support lseek(), also don't allow more than the size of metric table */
	if (size != sizeof(struct hsmp_metric_table)) {
		dev_err(sock->dev, "Wrong buffer size\n");
		return -EINVAL;
	}

	msg.msg_id	= HSMP_GET_METRIC_TABLE;
	msg.sock_ind	= sock->sock_ind;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;
	memcpy_fromio(buf, sock->metric_tbl_addr, size);

	return size;
}
EXPORT_SYMBOL_NS_GPL(hsmp_metric_tbl_read, "AMD_HSMP");

int hsmp_get_tbl_dram_base(u16 sock_ind)
{
	struct hsmp_socket *sock = &hsmp_pdev.sock[sock_ind];
	struct hsmp_message msg = { 0 };
	phys_addr_t dram_addr;
	int ret;

	msg.sock_ind	= sock_ind;
	msg.response_sz	= hsmp_msg_desc_table[HSMP_GET_METRIC_TABLE_DRAM_ADDR].response_sz;
	msg.msg_id	= HSMP_GET_METRIC_TABLE_DRAM_ADDR;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	/*
	 * calculate the metric table DRAM address from lower and upper 32 bits
	 * sent from SMU and ioremap it to virtual address.
	 */
	dram_addr = msg.args[0] | ((u64)(msg.args[1]) << 32);
	if (!dram_addr) {
		dev_err(sock->dev, "Invalid DRAM address for metric table\n");
		return -ENOMEM;
	}
	sock->metric_tbl_addr = devm_ioremap(sock->dev, dram_addr,
					     sizeof(struct hsmp_metric_table));
	if (!sock->metric_tbl_addr) {
		dev_err(sock->dev, "Failed to ioremap metric table addr\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(hsmp_get_tbl_dram_base, "AMD_HSMP");

int hsmp_cache_proto_ver(u16 sock_ind)
{
	struct hsmp_message msg = { 0 };
	int ret;

	msg.msg_id	= HSMP_GET_PROTO_VER;
	msg.sock_ind	= sock_ind;
	msg.response_sz = hsmp_msg_desc_table[HSMP_GET_PROTO_VER].response_sz;

	ret = hsmp_send_message(&msg);
	if (!ret)
		hsmp_pdev.proto_ver = msg.args[0];

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hsmp_cache_proto_ver, "AMD_HSMP");

static const struct file_operations hsmp_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= hsmp_ioctl,
	.compat_ioctl	= hsmp_ioctl,
};

int hsmp_misc_register(struct device *dev)
{
	hsmp_pdev.mdev.name	= HSMP_CDEV_NAME;
	hsmp_pdev.mdev.minor	= MISC_DYNAMIC_MINOR;
	hsmp_pdev.mdev.fops	= &hsmp_fops;
	hsmp_pdev.mdev.parent	= dev;
	hsmp_pdev.mdev.nodename	= HSMP_DEVNODE_NAME;
	hsmp_pdev.mdev.mode	= 0644;

	return misc_register(&hsmp_pdev.mdev);
}
EXPORT_SYMBOL_NS_GPL(hsmp_misc_register, "AMD_HSMP");

void hsmp_misc_deregister(void)
{
	misc_deregister(&hsmp_pdev.mdev);
}
EXPORT_SYMBOL_NS_GPL(hsmp_misc_deregister, "AMD_HSMP");

struct hsmp_plat_device *get_hsmp_pdev(void)
{
	return &hsmp_pdev;
}
EXPORT_SYMBOL_NS_GPL(get_hsmp_pdev, "AMD_HSMP");

MODULE_DESCRIPTION("AMD HSMP Common driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
