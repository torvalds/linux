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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>

#define DRIVER_NAME		"amd_hsmp"
#define DRIVER_VERSION		"2.0"

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

/*
 * To access specific HSMP mailbox register, s/w writes the SMN address of HSMP mailbox
 * register into the SMN_INDEX register, and reads/writes the SMN_DATA reg.
 * Below are required SMN address for HSMP Mailbox register offsets in SMU address space
 */
#define SMN_HSMP_MSG_ID		0x3B10534
#define SMN_HSMP_MSG_RESP	0x3B10980
#define SMN_HSMP_MSG_DATA	0x3B109E0

#define HSMP_INDEX_REG		0xc4
#define HSMP_DATA_REG		0xc8

#define HSMP_CDEV_NAME		"hsmp_cdev"
#define HSMP_DEVNODE_NAME	"hsmp"
#define HSMP_METRICS_TABLE_NAME	"metrics_bin"

#define HSMP_ATTR_GRP_NAME_SIZE	10

struct hsmp_socket {
	struct bin_attribute hsmp_attr;
	void __iomem *metric_tbl_addr;
	struct semaphore hsmp_sem;
	char name[HSMP_ATTR_GRP_NAME_SIZE];
	u16 sock_ind;
};

struct hsmp_plat_device {
	struct miscdevice hsmp_device;
	struct hsmp_socket *sock;
	struct device *dev;
	u32 proto_ver;
	u16 num_sockets;
};

static struct hsmp_plat_device plat_dev;

static int amd_hsmp_rdwr(struct pci_dev *root, u32 address,
			 u32 *value, bool write)
{
	int ret;

	ret = pci_write_config_dword(root, HSMP_INDEX_REG, address);
	if (ret)
		return ret;

	ret = (write ? pci_write_config_dword(root, HSMP_DATA_REG, *value)
		     : pci_read_config_dword(root, HSMP_DATA_REG, value));

	return ret;
}

/*
 * Send a message to the HSMP port via PCI-e config space registers.
 *
 * The caller is expected to zero out any unused arguments.
 * If a response is expected, the number of response words should be greater than 0.
 *
 * Returns 0 for success and populates the requested number of arguments.
 * Returns a negative error code for failure.
 */
static int __hsmp_send_message(struct pci_dev *root, struct hsmp_message *msg)
{
	unsigned long timeout, short_sleep;
	u32 mbox_status;
	u32 index;
	int ret;

	/* Clear the status register */
	mbox_status = HSMP_STATUS_NOT_READY;
	ret = amd_hsmp_rdwr(root, SMN_HSMP_MSG_RESP, &mbox_status, HSMP_WR);
	if (ret) {
		pr_err("Error %d clearing mailbox status register\n", ret);
		return ret;
	}

	index = 0;
	/* Write any message arguments */
	while (index < msg->num_args) {
		ret = amd_hsmp_rdwr(root, SMN_HSMP_MSG_DATA + (index << 2),
				    &msg->args[index], HSMP_WR);
		if (ret) {
			pr_err("Error %d writing message argument %d\n", ret, index);
			return ret;
		}
		index++;
	}

	/* Write the message ID which starts the operation */
	ret = amd_hsmp_rdwr(root, SMN_HSMP_MSG_ID, &msg->msg_id, HSMP_WR);
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
		ret = amd_hsmp_rdwr(root, SMN_HSMP_MSG_RESP, &mbox_status, HSMP_RD);
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
		ret = amd_hsmp_rdwr(root, SMN_HSMP_MSG_DATA + (index << 2),
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
	struct hsmp_socket *sock = &plat_dev.sock[msg->sock_ind];
	struct amd_northbridge *nb;
	int ret;

	if (!msg)
		return -EINVAL;

	nb = node_to_amd_nb(msg->sock_ind);
	if (!nb || !nb->root)
		return -ENODEV;

	ret = validate_message(msg);
	if (ret)
		return ret;

	/*
	 * The time taken by smu operation to complete is between
	 * 10us to 1ms. Sometime it may take more time.
	 * In SMP system timeout of 100 millisecs should
	 * be enough for the previous thread to finish the operation
	 */
	ret = down_timeout(&sock->hsmp_sem, msecs_to_jiffies(HSMP_MSG_TIMEOUT));
	if (ret < 0)
		return ret;

	ret = __hsmp_send_message(nb->root, msg);

	up(&sock->hsmp_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(hsmp_send_message);

static int hsmp_test(u16 sock_ind, u32 value)
{
	struct hsmp_message msg = { 0 };
	struct amd_northbridge *nb;
	int ret = -ENODEV;

	nb = node_to_amd_nb(sock_ind);
	if (!nb || !nb->root)
		return ret;

	/*
	 * Test the hsmp port by performing TEST command. The test message
	 * takes one argument and returns the value of that argument + 1.
	 */
	msg.msg_id	= HSMP_TEST;
	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.args[0]	= value;
	msg.sock_ind	= sock_ind;

	ret = __hsmp_send_message(nb->root, &msg);
	if (ret)
		return ret;

	/* Check the response value */
	if (msg.args[0] != (value + 1)) {
		pr_err("Socket %d test message failed, Expected 0x%08X, received 0x%08X\n",
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

static ssize_t hsmp_metric_tbl_read(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr, char *buf,
				    loff_t off, size_t count)
{
	struct hsmp_socket *sock = bin_attr->private;
	struct hsmp_message msg = { 0 };
	int ret;

	/* Do not support lseek(), reads entire metric table */
	if (count < bin_attr->size) {
		dev_err(plat_dev.dev, "Wrong buffer size\n");
		return -EINVAL;
	}

	if (!sock) {
		dev_err(plat_dev.dev, "Failed to read attribute private data\n");
		return -EINVAL;
	}

	msg.msg_id	= HSMP_GET_METRIC_TABLE;
	msg.sock_ind	= sock->sock_ind;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;
	memcpy(buf, sock->metric_tbl_addr, bin_attr->size);

	return bin_attr->size;
}

static int hsmp_get_tbl_dram_base(u16 sock_ind)
{
	struct hsmp_socket *sock = &plat_dev.sock[sock_ind];
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
		dev_err(plat_dev.dev, "Invalid DRAM address for metric table\n");
		return -ENOMEM;
	}
	sock->metric_tbl_addr = devm_ioremap(plat_dev.dev, dram_addr,
					     sizeof(struct hsmp_metric_table));
	if (!sock->metric_tbl_addr) {
		dev_err(plat_dev.dev, "Failed to ioremap metric table addr\n");
		return -ENOMEM;
	}
	return 0;
}

static umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
					 struct bin_attribute *battr, int id)
{
	if (plat_dev.proto_ver == HSMP_PROTO_VER6)
		return battr->attr.mode;
	else
		return 0;
}

static int hsmp_init_metric_tbl_bin_attr(struct bin_attribute **hattrs, u16 sock_ind)
{
	struct bin_attribute *hattr = &plat_dev.sock[sock_ind].hsmp_attr;

	sysfs_bin_attr_init(hattr);
	hattr->attr.name	= HSMP_METRICS_TABLE_NAME;
	hattr->attr.mode	= 0444;
	hattr->read		= hsmp_metric_tbl_read;
	hattr->size		= sizeof(struct hsmp_metric_table);
	hattr->private		= &plat_dev.sock[sock_ind];
	hattrs[0]		= hattr;

	if (plat_dev.proto_ver == HSMP_PROTO_VER6)
		return (hsmp_get_tbl_dram_base(sock_ind));
	else
		return 0;
}

/* One bin sysfs for metrics table*/
#define NUM_HSMP_ATTRS		1

static int hsmp_create_sysfs_interface(void)
{
	const struct attribute_group **hsmp_attr_grps;
	struct bin_attribute **hsmp_bin_attrs;
	struct attribute_group *attr_grp;
	int ret;
	u16 i;

	/* String formatting is currently limited to u8 sockets */
	if (WARN_ON(plat_dev.num_sockets > U8_MAX))
		return -ERANGE;

	hsmp_attr_grps = devm_kzalloc(plat_dev.dev, sizeof(struct attribute_group *) *
				      (plat_dev.num_sockets + 1), GFP_KERNEL);
	if (!hsmp_attr_grps)
		return -ENOMEM;

	/* Create a sysfs directory for each socket */
	for (i = 0; i < plat_dev.num_sockets; i++) {
		attr_grp = devm_kzalloc(plat_dev.dev, sizeof(struct attribute_group), GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		snprintf(plat_dev.sock[i].name, HSMP_ATTR_GRP_NAME_SIZE, "socket%u", (u8)i);
		attr_grp->name = plat_dev.sock[i].name;

		/* Null terminated list of attributes */
		hsmp_bin_attrs = devm_kzalloc(plat_dev.dev, sizeof(struct bin_attribute *) *
					      (NUM_HSMP_ATTRS + 1), GFP_KERNEL);
		if (!hsmp_bin_attrs)
			return -ENOMEM;

		attr_grp->bin_attrs		= hsmp_bin_attrs;
		attr_grp->is_bin_visible	= hsmp_is_sock_attr_visible;
		hsmp_attr_grps[i]		= attr_grp;

		/* Now create the leaf nodes */
		ret = hsmp_init_metric_tbl_bin_attr(hsmp_bin_attrs, i);
		if (ret)
			return ret;
	}
	return devm_device_add_groups(plat_dev.dev, hsmp_attr_grps);
}

static int hsmp_cache_proto_ver(void)
{
	struct hsmp_message msg = { 0 };
	int ret;

	msg.msg_id	= HSMP_GET_PROTO_VER;
	msg.sock_ind	= 0;
	msg.response_sz = hsmp_msg_desc_table[HSMP_GET_PROTO_VER].response_sz;

	ret = hsmp_send_message(&msg);
	if (!ret)
		plat_dev.proto_ver = msg.args[0];

	return ret;
}

static int hsmp_pltdrv_probe(struct platform_device *pdev)
{
	int ret, i;

	plat_dev.sock = devm_kzalloc(&pdev->dev,
				     (plat_dev.num_sockets * sizeof(struct hsmp_socket)),
				     GFP_KERNEL);
	if (!plat_dev.sock)
		return -ENOMEM;
	plat_dev.dev = &pdev->dev;

	for (i = 0; i < plat_dev.num_sockets; i++) {
		sema_init(&plat_dev.sock[i].hsmp_sem, 1);
		plat_dev.sock[i].sock_ind = i;
	}

	plat_dev.hsmp_device.name	= HSMP_CDEV_NAME;
	plat_dev.hsmp_device.minor	= MISC_DYNAMIC_MINOR;
	plat_dev.hsmp_device.fops	= &hsmp_fops;
	plat_dev.hsmp_device.parent	= &pdev->dev;
	plat_dev.hsmp_device.nodename	= HSMP_DEVNODE_NAME;
	plat_dev.hsmp_device.mode	= 0644;

	ret = hsmp_cache_proto_ver();
	if (ret) {
		dev_err(plat_dev.dev, "Failed to read HSMP protocol version\n");
		return ret;
	}

	ret = hsmp_create_sysfs_interface();
	if (ret)
		dev_err(plat_dev.dev, "Failed to create HSMP sysfs interface\n");

	return misc_register(&plat_dev.hsmp_device);
}

static void hsmp_pltdrv_remove(struct platform_device *pdev)
{
	misc_deregister(&plat_dev.hsmp_device);
}

static struct platform_driver amd_hsmp_driver = {
	.probe		= hsmp_pltdrv_probe,
	.remove_new	= hsmp_pltdrv_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static struct platform_device *amd_hsmp_platdev;

static int __init hsmp_plt_init(void)
{
	int ret = -ENODEV;
	int i;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD || boot_cpu_data.x86 < 0x19) {
		pr_err("HSMP is not supported on Family:%x model:%x\n",
		       boot_cpu_data.x86, boot_cpu_data.x86_model);
		return ret;
	}

	/*
	 * amd_nb_num() returns number of SMN/DF interfaces present in the system
	 * if we have N SMN/DF interfaces that ideally means N sockets
	 */
	plat_dev.num_sockets = amd_nb_num();
	if (plat_dev.num_sockets == 0)
		return ret;

	/* Test the hsmp interface on each socket */
	for (i = 0; i < plat_dev.num_sockets; i++) {
		ret = hsmp_test(i, 0xDEADBEEF);
		if (ret) {
			pr_err("HSMP test message failed on Fam:%x model:%x\n",
			       boot_cpu_data.x86, boot_cpu_data.x86_model);
			pr_err("Is HSMP disabled in BIOS ?\n");
			return ret;
		}
	}

	ret = platform_driver_register(&amd_hsmp_driver);
	if (ret)
		return ret;

	amd_hsmp_platdev = platform_device_alloc(DRIVER_NAME, PLATFORM_DEVID_NONE);
	if (!amd_hsmp_platdev) {
		ret = -ENOMEM;
		goto drv_unregister;
	}

	ret = platform_device_add(amd_hsmp_platdev);
	if (ret) {
		platform_device_put(amd_hsmp_platdev);
		goto drv_unregister;
	}

	return 0;

drv_unregister:
	platform_driver_unregister(&amd_hsmp_driver);
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
