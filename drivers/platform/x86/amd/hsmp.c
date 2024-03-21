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
#include <linux/acpi.h>

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

/*
 * To access specific HSMP mailbox register, s/w writes the SMN address of HSMP mailbox
 * register into the SMN_INDEX register, and reads/writes the SMN_DATA reg.
 * Below are required SMN address for HSMP Mailbox register offsets in SMU address space
 */
#define SMN_HSMP_BASE		0x3B00000
#define SMN_HSMP_MSG_ID		0x0010534
#define SMN_HSMP_MSG_ID_F1A_M0H	0x0010934
#define SMN_HSMP_MSG_RESP	0x0010980
#define SMN_HSMP_MSG_DATA	0x00109E0

#define HSMP_INDEX_REG		0xc4
#define HSMP_DATA_REG		0xc8

#define HSMP_CDEV_NAME		"hsmp_cdev"
#define HSMP_DEVNODE_NAME	"hsmp"
#define HSMP_METRICS_TABLE_NAME	"metrics_bin"

#define HSMP_ATTR_GRP_NAME_SIZE	10

/* These are the strings specified in ACPI table */
#define MSG_IDOFF_STR		"MsgIdOffset"
#define MSG_ARGOFF_STR		"MsgArgOffset"
#define MSG_RESPOFF_STR		"MsgRspOffset"

#define MAX_AMD_SOCKETS 8

struct hsmp_mbaddr_info {
	u32 base_addr;
	u32 msg_id_off;
	u32 msg_resp_off;
	u32 msg_arg_off;
	u32 size;
};

struct hsmp_socket {
	struct bin_attribute hsmp_attr;
	struct hsmp_mbaddr_info mbinfo;
	void __iomem *metric_tbl_addr;
	void __iomem *virt_base_addr;
	struct semaphore hsmp_sem;
	char name[HSMP_ATTR_GRP_NAME_SIZE];
	struct pci_dev *root;
	struct device *dev;
	u16 sock_ind;
};

struct hsmp_plat_device {
	struct miscdevice hsmp_device;
	struct hsmp_socket *sock;
	u32 proto_ver;
	u16 num_sockets;
	bool is_acpi_device;
	bool is_probed;
};

static struct hsmp_plat_device plat_dev;

static int amd_hsmp_pci_rdwr(struct hsmp_socket *sock, u32 offset,
			     u32 *value, bool write)
{
	int ret;

	if (!sock->root)
		return -ENODEV;

	ret = pci_write_config_dword(sock->root, HSMP_INDEX_REG,
				     sock->mbinfo.base_addr + offset);
	if (ret)
		return ret;

	ret = (write ? pci_write_config_dword(sock->root, HSMP_DATA_REG, *value)
		     : pci_read_config_dword(sock->root, HSMP_DATA_REG, value));

	return ret;
}

static void amd_hsmp_acpi_rdwr(struct hsmp_socket *sock, u32 offset,
			       u32 *value, bool write)
{
	if (write)
		iowrite32(*value, sock->virt_base_addr + offset);
	else
		*value = ioread32(sock->virt_base_addr + offset);
}

static int amd_hsmp_rdwr(struct hsmp_socket *sock, u32 offset,
			 u32 *value, bool write)
{
	if (plat_dev.is_acpi_device)
		amd_hsmp_acpi_rdwr(sock, offset, value, write);
	else
		return amd_hsmp_pci_rdwr(sock, offset, value, write);

	return 0;
}

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
	ret = amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_WR);
	if (ret) {
		pr_err("Error %d clearing mailbox status register\n", ret);
		return ret;
	}

	index = 0;
	/* Write any message arguments */
	while (index < msg->num_args) {
		ret = amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
				    &msg->args[index], HSMP_WR);
		if (ret) {
			pr_err("Error %d writing message argument %d\n", ret, index);
			return ret;
		}
		index++;
	}

	/* Write the message ID which starts the operation */
	ret = amd_hsmp_rdwr(sock, mbinfo->msg_id_off, &msg->msg_id, HSMP_WR);
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
		ret = amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_RD);
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
		ret = amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
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

	if (!plat_dev.sock || msg->sock_ind >= plat_dev.num_sockets)
		return -ENODEV;
	sock = &plat_dev.sock[msg->sock_ind];

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

static int hsmp_test(u16 sock_ind, u32 value)
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
		dev_err(plat_dev.sock[sock_ind].dev,
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

/* This is the UUID used for HSMP */
static const guid_t acpi_hsmp_uuid = GUID_INIT(0xb74d619d, 0x5707, 0x48bd,
						0xa6, 0x9f, 0x4e, 0xa2,
						0x87, 0x1f, 0xc2, 0xf6);

static inline bool is_acpi_hsmp_uuid(union acpi_object *obj)
{
	if (obj->type == ACPI_TYPE_BUFFER && obj->buffer.length == UUID_SIZE)
		return guid_equal((guid_t *)obj->buffer.pointer, &acpi_hsmp_uuid);

	return false;
}

static inline int hsmp_get_uid(struct device *dev, u16 *sock_ind)
{
	char *uid;

	/*
	 * UID (ID00, ID01..IDXX) is used for differentiating sockets,
	 * read it and strip the "ID" part of it and convert the remaining
	 * bytes to integer.
	 */
	uid = acpi_device_uid(ACPI_COMPANION(dev));

	return kstrtou16(uid + 2, 10, sock_ind);
}

static acpi_status hsmp_resource(struct acpi_resource *res, void *data)
{
	struct hsmp_socket *sock = data;
	struct resource r;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		if (!acpi_dev_resource_memory(res, &r))
			return AE_ERROR;
		if (!r.start || r.end < r.start || !(r.flags & IORESOURCE_MEM_WRITEABLE))
			return AE_ERROR;
		sock->mbinfo.base_addr = r.start;
		sock->mbinfo.size = resource_size(&r);
		break;
	case ACPI_RESOURCE_TYPE_END_TAG:
		break;
	default:
		return AE_ERROR;
	}

	return AE_OK;
}

static int hsmp_read_acpi_dsd(struct hsmp_socket *sock)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *guid, *mailbox_package;
	union acpi_object *dsd;
	acpi_status status;
	int ret = 0;
	int j;

	status = acpi_evaluate_object_typed(ACPI_HANDLE(sock->dev), "_DSD", NULL,
					    &buf, ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status)) {
		dev_err(sock->dev, "Failed to read mailbox reg offsets from DSD table, err: %s\n",
			acpi_format_exception(status));
		return -ENODEV;
	}

	dsd = buf.pointer;

	/* HSMP _DSD property should contain 2 objects.
	 * 1. guid which is an acpi object of type ACPI_TYPE_BUFFER
	 * 2. mailbox which is an acpi object of type ACPI_TYPE_PACKAGE
	 *    This mailbox object contains 3 more acpi objects of type
	 *    ACPI_TYPE_PACKAGE for holding msgid, msgresp, msgarg offsets
	 *    these packages inturn contain 2 acpi objects of type
	 *    ACPI_TYPE_STRING and ACPI_TYPE_INTEGER
	 */
	if (!dsd || dsd->type != ACPI_TYPE_PACKAGE || dsd->package.count != 2) {
		ret = -EINVAL;
		goto free_buf;
	}

	guid = &dsd->package.elements[0];
	mailbox_package = &dsd->package.elements[1];
	if (!is_acpi_hsmp_uuid(guid) || mailbox_package->type != ACPI_TYPE_PACKAGE) {
		dev_err(sock->dev, "Invalid hsmp _DSD table data\n");
		ret = -EINVAL;
		goto free_buf;
	}

	for (j = 0; j < mailbox_package->package.count; j++) {
		union acpi_object *msgobj, *msgstr, *msgint;

		msgobj	= &mailbox_package->package.elements[j];
		msgstr	= &msgobj->package.elements[0];
		msgint	= &msgobj->package.elements[1];

		/* package should have 1 string and 1 integer object */
		if (msgobj->type != ACPI_TYPE_PACKAGE ||
		    msgstr->type != ACPI_TYPE_STRING ||
		    msgint->type != ACPI_TYPE_INTEGER) {
			ret = -EINVAL;
			goto free_buf;
		}

		if (!strncmp(msgstr->string.pointer, MSG_IDOFF_STR,
			     msgstr->string.length)) {
			sock->mbinfo.msg_id_off = msgint->integer.value;
		} else if (!strncmp(msgstr->string.pointer, MSG_RESPOFF_STR,
				    msgstr->string.length)) {
			sock->mbinfo.msg_resp_off =  msgint->integer.value;
		} else if (!strncmp(msgstr->string.pointer, MSG_ARGOFF_STR,
				    msgstr->string.length)) {
			sock->mbinfo.msg_arg_off = msgint->integer.value;
		} else {
			ret = -ENOENT;
			goto free_buf;
		}
	}

	if (!sock->mbinfo.msg_id_off || !sock->mbinfo.msg_resp_off ||
	    !sock->mbinfo.msg_arg_off)
		ret = -EINVAL;

free_buf:
	ACPI_FREE(buf.pointer);
	return ret;
}

static int hsmp_read_acpi_crs(struct hsmp_socket *sock)
{
	acpi_status status;

	status = acpi_walk_resources(ACPI_HANDLE(sock->dev), METHOD_NAME__CRS,
				     hsmp_resource, sock);
	if (ACPI_FAILURE(status)) {
		dev_err(sock->dev, "Failed to look up MP1 base address from CRS method, err: %s\n",
			acpi_format_exception(status));
		return -EINVAL;
	}
	if (!sock->mbinfo.base_addr || !sock->mbinfo.size)
		return -EINVAL;

	/* The mapped region should be un cached */
	sock->virt_base_addr = devm_ioremap_uc(sock->dev, sock->mbinfo.base_addr,
					       sock->mbinfo.size);
	if (!sock->virt_base_addr) {
		dev_err(sock->dev, "Failed to ioremap MP1 base address\n");
		return -ENOMEM;
	}

	return 0;
}

/* Parse the ACPI table to read the data */
static int hsmp_parse_acpi_table(struct device *dev, u16 sock_ind)
{
	struct hsmp_socket *sock = &plat_dev.sock[sock_ind];
	int ret;

	sock->sock_ind		= sock_ind;
	sock->dev		= dev;
	plat_dev.is_acpi_device	= true;

	sema_init(&sock->hsmp_sem, 1);

	/* Read MP1 base address from CRS method */
	ret = hsmp_read_acpi_crs(sock);
	if (ret)
		return ret;

	/* Read mailbox offsets from DSD table */
	return hsmp_read_acpi_dsd(sock);
}

static ssize_t hsmp_metric_tbl_read(struct file *filp, struct kobject *kobj,
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
		return hsmp_get_tbl_dram_base(sock_ind);
	else
		return 0;
}

/* One bin sysfs for metrics table */
#define NUM_HSMP_ATTRS		1

static int hsmp_create_attr_list(struct attribute_group *attr_grp,
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

static int hsmp_create_non_acpi_sysfs_if(struct device *dev)
{
	const struct attribute_group **hsmp_attr_grps;
	struct attribute_group *attr_grp;
	u16 i;

	hsmp_attr_grps = devm_kcalloc(dev, plat_dev.num_sockets + 1,
				      sizeof(*hsmp_attr_grps),
				      GFP_KERNEL);
	if (!hsmp_attr_grps)
		return -ENOMEM;

	/* Create a sysfs directory for each socket */
	for (i = 0; i < plat_dev.num_sockets; i++) {
		attr_grp = devm_kzalloc(dev, sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		snprintf(plat_dev.sock[i].name, HSMP_ATTR_GRP_NAME_SIZE, "socket%u", (u8)i);
		attr_grp->name			= plat_dev.sock[i].name;
		attr_grp->is_bin_visible	= hsmp_is_sock_attr_visible;
		hsmp_attr_grps[i]		= attr_grp;

		hsmp_create_attr_list(attr_grp, dev, i);
	}

	return devm_device_add_groups(dev, hsmp_attr_grps);
}

static int hsmp_create_acpi_sysfs_if(struct device *dev)
{
	struct attribute_group *attr_grp;
	u16 sock_ind;
	int ret;

	attr_grp = devm_kzalloc(dev, sizeof(struct attribute_group), GFP_KERNEL);
	if (!attr_grp)
		return -ENOMEM;

	attr_grp->is_bin_visible = hsmp_is_sock_attr_visible;

	ret = hsmp_get_uid(dev, &sock_ind);
	if (ret)
		return ret;

	ret = hsmp_create_attr_list(attr_grp, dev, sock_ind);
	if (ret)
		return ret;

	return devm_device_add_group(dev, attr_grp);
}

static int hsmp_cache_proto_ver(u16 sock_ind)
{
	struct hsmp_message msg = { 0 };
	int ret;

	msg.msg_id	= HSMP_GET_PROTO_VER;
	msg.sock_ind	= sock_ind;
	msg.response_sz = hsmp_msg_desc_table[HSMP_GET_PROTO_VER].response_sz;

	ret = hsmp_send_message(&msg);
	if (!ret)
		plat_dev.proto_ver = msg.args[0];

	return ret;
}

static inline bool is_f1a_m0h(void)
{
	if (boot_cpu_data.x86 == 0x1A && boot_cpu_data.x86_model <= 0x0F)
		return true;

	return false;
}

static int init_platform_device(struct device *dev)
{
	struct hsmp_socket *sock;
	int ret, i;

	for (i = 0; i < plat_dev.num_sockets; i++) {
		if (!node_to_amd_nb(i))
			return -ENODEV;
		sock = &plat_dev.sock[i];
		sock->root			= node_to_amd_nb(i)->root;
		sock->sock_ind			= i;
		sock->dev			= dev;
		sock->mbinfo.base_addr		= SMN_HSMP_BASE;

		/*
		 * This is a transitional change from non-ACPI to ACPI, only
		 * family 0x1A, model 0x00 platform is supported for both ACPI and non-ACPI.
		 */
		if (is_f1a_m0h())
			sock->mbinfo.msg_id_off	= SMN_HSMP_MSG_ID_F1A_M0H;
		else
			sock->mbinfo.msg_id_off	= SMN_HSMP_MSG_ID;

		sock->mbinfo.msg_resp_off	= SMN_HSMP_MSG_RESP;
		sock->mbinfo.msg_arg_off	= SMN_HSMP_MSG_DATA;
		sema_init(&sock->hsmp_sem, 1);

		/* Test the hsmp interface on each socket */
		ret = hsmp_test(i, 0xDEADBEEF);
		if (ret) {
			dev_err(dev, "HSMP test message failed on Fam:%x model:%x\n",
				boot_cpu_data.x86, boot_cpu_data.x86_model);
			dev_err(dev, "Is HSMP disabled in BIOS ?\n");
			return ret;
		}
	}

	return 0;
}

static const struct acpi_device_id amd_hsmp_acpi_ids[] = {
	{ACPI_HSMP_DEVICE_HID, 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, amd_hsmp_acpi_ids);

static int hsmp_pltdrv_probe(struct platform_device *pdev)
{
	struct acpi_device *adev;
	u16 sock_ind = 0;
	int ret;

	/*
	 * On ACPI supported BIOS, there is an ACPI HSMP device added for
	 * each socket, so the per socket probing, but the memory allocated for
	 * sockets should be contiguous to access it as an array,
	 * Hence allocate memory for all the sockets at once instead of allocating
	 * on each probe.
	 */
	if (!plat_dev.is_probed) {
		plat_dev.sock = devm_kcalloc(&pdev->dev, plat_dev.num_sockets,
					     sizeof(*plat_dev.sock),
					     GFP_KERNEL);
		if (!plat_dev.sock)
			return -ENOMEM;
	}
	adev = ACPI_COMPANION(&pdev->dev);
	if (adev && !acpi_match_device_ids(adev, amd_hsmp_acpi_ids)) {
		ret = hsmp_get_uid(&pdev->dev, &sock_ind);
		if (ret)
			return ret;
		if (sock_ind >= plat_dev.num_sockets)
			return -EINVAL;
		ret = hsmp_parse_acpi_table(&pdev->dev, sock_ind);
		if (ret) {
			dev_err(&pdev->dev, "Failed to parse ACPI table\n");
			return ret;
		}
		/* Test the hsmp interface */
		ret = hsmp_test(sock_ind, 0xDEADBEEF);
		if (ret) {
			dev_err(&pdev->dev, "HSMP test message failed on Fam:%x model:%x\n",
				boot_cpu_data.x86, boot_cpu_data.x86_model);
			dev_err(&pdev->dev, "Is HSMP disabled in BIOS ?\n");
			return ret;
		}
	} else {
		ret = init_platform_device(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to init HSMP mailbox\n");
			return ret;
		}
	}

	ret = hsmp_cache_proto_ver(sock_ind);
	if (ret) {
		dev_err(&pdev->dev, "Failed to read HSMP protocol version\n");
		return ret;
	}

	if (plat_dev.is_acpi_device)
		ret = hsmp_create_acpi_sysfs_if(&pdev->dev);
	else
		ret = hsmp_create_non_acpi_sysfs_if(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "Failed to create HSMP sysfs interface\n");

	if (!plat_dev.is_probed) {
		plat_dev.hsmp_device.name	= HSMP_CDEV_NAME;
		plat_dev.hsmp_device.minor	= MISC_DYNAMIC_MINOR;
		plat_dev.hsmp_device.fops	= &hsmp_fops;
		plat_dev.hsmp_device.parent	= &pdev->dev;
		plat_dev.hsmp_device.nodename	= HSMP_DEVNODE_NAME;
		plat_dev.hsmp_device.mode	= 0644;

		ret = misc_register(&plat_dev.hsmp_device);
		if (ret)
			return ret;

		plat_dev.is_probed = true;
	}

	return 0;

}

static void hsmp_pltdrv_remove(struct platform_device *pdev)
{
	/*
	 * We register only one misc_device even on multi socket system.
	 * So, deregister should happen only once.
	 */
	if (plat_dev.is_probed) {
		misc_deregister(&plat_dev.hsmp_device);
		plat_dev.is_probed = false;
	}
}

static struct platform_driver amd_hsmp_driver = {
	.probe		= hsmp_pltdrv_probe,
	.remove_new	= hsmp_pltdrv_remove,
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

static int __init hsmp_plt_init(void)
{
	int ret = -ENODEV;

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
	if (plat_dev.num_sockets == 0 || plat_dev.num_sockets > MAX_AMD_SOCKETS)
		return ret;

	ret = platform_driver_register(&amd_hsmp_driver);
	if (ret)
		return ret;

	if (!plat_dev.is_acpi_device) {
		ret = hsmp_plat_dev_register();
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
