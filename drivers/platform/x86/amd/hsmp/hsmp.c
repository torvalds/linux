// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2022, AMD.
 * All Rights Reserved.
 *
 * This file provides a device implementation for HSMP interface
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd_hsmp.h>
#include <asm/amd_nb.h>

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/sysfs.h>

#include "hsmp.h"

#define DRIVER_NAME		"amd_hsmp"
#define DRIVER_VERSION		"2.2"
#define ACPI_HSMP_DEVICE_HID	"AMDI0097"

/* HSMP Status / Error codes */
#define HSMP_STATUS_NOT_READY	0x00
#define HSMP_STATUS_OK		0x01
#define HSMP_ERR_INVALID_MSG	0xFE
#define HSMP_ERR_INVALID_INPUT	0xFF

/* Timeout in millsec */
#define HSMP_MSG_TIMEOUT	100
#define HSMP_SHORT_SLEEP	1

#define HSMP_WR			true
#define HSMP_RD			false

struct hsmp_plat_device hsmp_pdev;

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
		pr_err("Error %d clearing mailbox status register\n", ret);
		return ret;
	}

	index = 0;
	/* Write any message arguments */
	while (index < msg->num_args) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
					  &msg->args[index], HSMP_WR);
		if (ret) {
			pr_err("Error %d writing message argument %d\n", ret, index);
			return ret;
		}
		index++;
	}

	/* Write the message ID which starts the operation */
	ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_id_off, &msg->msg_id, HSMP_WR);
	if (ret) {
		pr_err("Error %d writing message ID %u\n", ret, msg->msg_id);
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

	while (time_before(jiffies, timeout)) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_RD);
		if (ret) {
			pr_err("Error %d reading mailbox status\n", ret);
			return ret;
		}

		if (mbox_status != HSMP_STATUS_NOT_READY)
			break;
		if (time_before(jiffies, short_sleep))
			usleep_range(50, 100);
		else
			usleep_range(1000, 2000);
	}

	if (unlikely(mbox_status == HSMP_STATUS_NOT_READY)) {
		return -ETIMEDOUT;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_MSG)) {
		return -ENOMSG;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_INPUT)) {
		return -EINVAL;
	} else if (unlikely(mbox_status != HSMP_STATUS_OK)) {
		pr_err("Message ID %u unknown failure (status = 0x%X)\n",
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
			pr_err("Error %d reading response %u for message ID:%u\n",
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

	/* num_args and response_sz against the HSMP spec */
	if (msg->num_args != hsmp_msg_desc_table[msg->msg_id].num_args ||
	    msg->response_sz != hsmp_msg_desc_table[msg->msg_id].response_sz)
		return -EINVAL;

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

	/*
	 * The time taken by smu operation to complete is between
	 * 10us to 1ms. Sometime it may take more time.
	 * In SMP system timeout of 100 millisecs should
	 * be enough for the previous thread to finish the operation
	 */
	ret = down_timeout(&sock->hsmp_sem, msecs_to_jiffies(HSMP_MSG_TIMEOUT));
	if (ret < 0)
		return ret;

	ret = __hsmp_send_message(sock, msg);

	up(&sock->hsmp_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(hsmp_send_message);

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

static long hsmp_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
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
		if (hsmp_msg_desc_table[msg.msg_id].type != HSMP_SET)
			return -EINVAL;
		break;
	case FMODE_READ:
		/*
		 * Device is opened in O_RDONLY mode
		 * Execute only get/monitor commands
		 */
		if (hsmp_msg_desc_table[msg.msg_id].type != HSMP_GET)
			return -EINVAL;
		break;
	case FMODE_READ | FMODE_WRITE:
		/*
		 * Device is opened in O_RDWR mode
		 * Execute both get/monitor and set/configure commands
		 */
		break;
	default:
		return -EINVAL;
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

static const struct file_operations hsmp_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= hsmp_ioctl,
	.compat_ioctl	= hsmp_ioctl,
};

ssize_t hsmp_metric_tbl_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct hsmp_socket *sock = bin_attr->private;
	struct hsmp_message msg = { 0 };
	int ret;

	if (!sock)
		return -EINVAL;

	/* Do not support lseek(), reads entire metric table */
	if (count < bin_attr->size) {
		dev_err(sock->dev, "Wrong buffer size\n");
		return -EINVAL;
	}

	msg.msg_id	= HSMP_GET_METRIC_TABLE;
	msg.sock_ind	= sock->sock_ind;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;
	memcpy_fromio(buf, sock->metric_tbl_addr, bin_attr->size);

	return bin_attr->size;
}

static int hsmp_get_tbl_dram_base(u16 sock_ind)
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

umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
				  struct bin_attribute *battr, int id)
{
	if (hsmp_pdev.proto_ver == HSMP_PROTO_VER6)
		return battr->attr.mode;
	else
		return 0;
}

static int hsmp_init_metric_tbl_bin_attr(struct bin_attribute **hattrs, u16 sock_ind)
{
	struct bin_attribute *hattr = &hsmp_pdev.sock[sock_ind].hsmp_attr;

	sysfs_bin_attr_init(hattr);
	hattr->attr.name	= HSMP_METRICS_TABLE_NAME;
	hattr->attr.mode	= 0444;
	hattr->read		= hsmp_metric_tbl_read;
	hattr->size		= sizeof(struct hsmp_metric_table);
	hattr->private		= &hsmp_pdev.sock[sock_ind];
	hattrs[0]		= hattr;

	if (hsmp_pdev.proto_ver == HSMP_PROTO_VER6)
		return hsmp_get_tbl_dram_base(sock_ind);
	else
		return 0;
}

/* One bin sysfs for metrics table */
#define NUM_HSMP_ATTRS		1

int hsmp_create_attr_list(struct attribute_group *attr_grp,
			  struct device *dev, u16 sock_ind)
{
	struct bin_attribute **hsmp_bin_attrs;

	/* Null terminated list of attributes */
	hsmp_bin_attrs = devm_kcalloc(dev, NUM_HSMP_ATTRS + 1,
				      sizeof(*hsmp_bin_attrs),
				      GFP_KERNEL);
	if (!hsmp_bin_attrs)
		return -ENOMEM;

	attr_grp->bin_attrs = hsmp_bin_attrs;

	return hsmp_init_metric_tbl_bin_attr(hsmp_bin_attrs, sock_ind);
}

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

static const struct acpi_device_id amd_hsmp_acpi_ids[] = {
	{ACPI_HSMP_DEVICE_HID, 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, amd_hsmp_acpi_ids);

static bool check_acpi_support(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (adev && !acpi_match_device_ids(adev, amd_hsmp_acpi_ids))
		return true;

	return false;
}

static int hsmp_pltdrv_probe(struct platform_device *pdev)
{
	int ret;

	/*
	 * On ACPI supported BIOS, there is an ACPI HSMP device added for
	 * each socket, so the per socket probing, but the memory allocated for
	 * sockets should be contiguous to access it as an array,
	 * Hence allocate memory for all the sockets at once instead of allocating
	 * on each probe.
	 */
	if (!hsmp_pdev.is_probed) {
		hsmp_pdev.sock = devm_kcalloc(&pdev->dev, hsmp_pdev.num_sockets,
					      sizeof(*hsmp_pdev.sock),
					      GFP_KERNEL);
		if (!hsmp_pdev.sock)
			return -ENOMEM;
	}
	if (check_acpi_support(&pdev->dev)) {
		ret = init_acpi(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to init HSMP mailbox\n");
			return ret;
		}
		ret = hsmp_create_acpi_sysfs_if(&pdev->dev);
		if (ret)
			dev_err(&pdev->dev, "Failed to create HSMP sysfs interface\n");
	} else {
		ret = init_platform_device(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to init HSMP mailbox\n");
			return ret;
		}
		ret = hsmp_create_non_acpi_sysfs_if(&pdev->dev);
		if (ret)
			dev_err(&pdev->dev, "Failed to create HSMP sysfs interface\n");
	}

	if (!hsmp_pdev.is_probed) {
		hsmp_pdev.mdev.name	= HSMP_CDEV_NAME;
		hsmp_pdev.mdev.minor	= MISC_DYNAMIC_MINOR;
		hsmp_pdev.mdev.fops	= &hsmp_fops;
		hsmp_pdev.mdev.parent	= &pdev->dev;
		hsmp_pdev.mdev.nodename	= HSMP_DEVNODE_NAME;
		hsmp_pdev.mdev.mode	= 0644;

		ret = misc_register(&hsmp_pdev.mdev);
		if (ret)
			return ret;

		hsmp_pdev.is_probed = true;
	}

	return 0;

}

static void hsmp_pltdrv_remove(struct platform_device *pdev)
{
	/*
	 * We register only one misc_device even on multi socket system.
	 * So, deregister should happen only once.
	 */
	if (hsmp_pdev.is_probed) {
		misc_deregister(&hsmp_pdev.mdev);
		hsmp_pdev.is_probed = false;
	}
}

static struct platform_driver amd_hsmp_driver = {
	.probe		= hsmp_pltdrv_probe,
	.remove		= hsmp_pltdrv_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.acpi_match_table = amd_hsmp_acpi_ids,
	},
};

static struct platform_device *amd_hsmp_platdev;

static int hsmp_plat_dev_register(void)
{
	int ret;

	amd_hsmp_platdev = platform_device_alloc(DRIVER_NAME, PLATFORM_DEVID_NONE);
	if (!amd_hsmp_platdev)
		return -ENOMEM;

	ret = platform_device_add(amd_hsmp_platdev);
	if (ret)
		platform_device_put(amd_hsmp_platdev);

	return ret;
}

/*
 * This check is only needed for backward compatibility of previous platforms.
 * All new platforms are expected to support ACPI based probing.
 */
static bool legacy_hsmp_support(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return false;

	switch (boot_cpu_data.x86) {
	case 0x19:
		switch (boot_cpu_data.x86_model) {
		case 0x00 ... 0x1F:
		case 0x30 ... 0x3F:
		case 0x90 ... 0x9F:
		case 0xA0 ... 0xAF:
			return true;
		default:
			return false;
		}
	case 0x1A:
		switch (boot_cpu_data.x86_model) {
		case 0x00 ... 0x1F:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}

	return false;
}

static int __init hsmp_plt_init(void)
{
	int ret = -ENODEV;

	/*
	 * amd_nb_num() returns number of SMN/DF interfaces present in the system
	 * if we have N SMN/DF interfaces that ideally means N sockets
	 */
	hsmp_pdev.num_sockets = amd_nb_num();
	if (hsmp_pdev.num_sockets == 0 || hsmp_pdev.num_sockets > MAX_AMD_SOCKETS)
		return ret;

	ret = platform_driver_register(&amd_hsmp_driver);
	if (ret)
		return ret;

	if (!hsmp_pdev.is_acpi_device) {
		if (legacy_hsmp_support()) {
			/* Not ACPI device, but supports HSMP, register a plat_dev */
			ret = hsmp_plat_dev_register();
		} else {
			/* Not ACPI, Does not support HSMP */
			pr_info("HSMP is not supported on Family:%x model:%x\n",
				boot_cpu_data.x86, boot_cpu_data.x86_model);
			ret = -ENODEV;
		}
		if (ret)
			platform_driver_unregister(&amd_hsmp_driver);
	}

	return ret;
}

static void __exit hsmp_plt_exit(void)
{
	platform_device_unregister(amd_hsmp_platdev);
	platform_driver_unregister(&amd_hsmp_driver);
}

device_initcall(hsmp_plt_init);
module_exit(hsmp_plt_exit);

MODULE_DESCRIPTION("AMD HSMP Platform Interface Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
