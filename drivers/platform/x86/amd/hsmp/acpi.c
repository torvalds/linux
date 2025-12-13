// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2024, AMD.
 * All Rights Reserved.
 *
 * This file provides an ACPI based driver implementation for HSMP interface.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd/hsmp.h>

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/ioport.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/uuid.h>

#include <uapi/asm-generic/errno-base.h>

#include <asm/amd/node.h>

#include "hsmp.h"

#define DRIVER_NAME		"hsmp_acpi"

/* These are the strings specified in ACPI table */
#define MSG_IDOFF_STR		"MsgIdOffset"
#define MSG_ARGOFF_STR		"MsgArgOffset"
#define MSG_RESPOFF_STR		"MsgRspOffset"

static struct hsmp_plat_device *hsmp_pdev;

struct hsmp_sys_attr {
	struct device_attribute dattr;
	u32 msg_id;
};

static int amd_hsmp_acpi_rdwr(struct hsmp_socket *sock, u32 offset,
			      u32 *value, bool write)
{
	if (write)
		iowrite32(*value, sock->virt_base_addr + offset);
	else
		*value = ioread32(sock->virt_base_addr + offset);

	return 0;
}

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

	/* The mapped region should be un-cached */
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
	struct hsmp_socket *sock = &hsmp_pdev->sock[sock_ind];
	int ret;

	sock->sock_ind		= sock_ind;
	sock->dev		= dev;
	sock->amd_hsmp_rdwr	= amd_hsmp_acpi_rdwr;

	sema_init(&sock->hsmp_sem, 1);

	dev_set_drvdata(dev, sock);

	/* Read MP1 base address from CRS method */
	ret = hsmp_read_acpi_crs(sock);
	if (ret)
		return ret;

	/* Read mailbox offsets from DSD table */
	return hsmp_read_acpi_dsd(sock);
}

static ssize_t hsmp_metric_tbl_acpi_read(struct file *filp, struct kobject *kobj,
					 const struct bin_attribute *bin_attr, char *buf,
					 loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct hsmp_socket *sock = dev_get_drvdata(dev);

	return hsmp_metric_tbl_read(sock, buf, count);
}

static umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
					 const struct bin_attribute *battr, int id)
{
	if (hsmp_pdev->proto_ver == HSMP_PROTO_VER6)
		return battr->attr.mode;

	return 0;
}

static umode_t hsmp_is_sock_dev_attr_visible(struct kobject *kobj,
					     struct attribute *attr, int id)
{
	return attr->mode;
}

#define to_hsmp_sys_attr(_attr) container_of(_attr, struct hsmp_sys_attr, dattr)

static ssize_t hsmp_msg_resp32_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", data);
}

#define DDR_MAX_BW_MASK		GENMASK(31, 20)
#define DDR_UTIL_BW_MASK	GENMASK(19, 8)
#define DDR_UTIL_BW_PERC_MASK	GENMASK(7, 0)
#define FW_VER_MAJOR_MASK	GENMASK(23, 16)
#define FW_VER_MINOR_MASK	GENMASK(15, 8)
#define FW_VER_DEBUG_MASK	GENMASK(7, 0)
#define FMAX_MASK		GENMASK(31, 16)
#define FMIN_MASK		GENMASK(15, 0)
#define FREQ_LIMIT_MASK		GENMASK(31, 16)
#define FREQ_SRC_IND_MASK	GENMASK(15, 0)

static ssize_t hsmp_ddr_max_bw_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(DDR_MAX_BW_MASK, data));
}

static ssize_t hsmp_ddr_util_bw_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(DDR_UTIL_BW_MASK, data));
}

static ssize_t hsmp_ddr_util_bw_perc_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(DDR_UTIL_BW_PERC_MASK, data));
}

static ssize_t hsmp_msg_fw_ver_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu.%lu.%lu\n",
			  FIELD_GET(FW_VER_MAJOR_MASK, data),
			  FIELD_GET(FW_VER_MINOR_MASK, data),
			  FIELD_GET(FW_VER_DEBUG_MASK, data));
}

static ssize_t hsmp_fclk_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data[2];
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, data, 2);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", data[0]);
}

static ssize_t hsmp_mclk_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data[2];
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, data, 2);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", data[1]);
}

static ssize_t hsmp_clk_fmax_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(FMAX_MASK, data));
}

static ssize_t hsmp_clk_fmin_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(FMIN_MASK, data));
}

static ssize_t hsmp_freq_limit_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(FREQ_LIMIT_MASK, data));
}

static const char * const freqlimit_srcnames[] = {
	"cHTC-Active",
	"PROCHOT",
	"TDC limit",
	"PPT Limit",
	"OPN Max",
	"Reliability Limit",
	"APML Agent",
	"HSMP Agent",
};

static ssize_t hsmp_freq_limit_source_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct hsmp_sys_attr *hattr = to_hsmp_sys_attr(attr);
	struct hsmp_socket *sock = dev_get_drvdata(dev);
	unsigned int index;
	int len = 0;
	u16 src_ind;
	u32 data;
	int ret;

	ret = hsmp_msg_get_nargs(sock->sock_ind, hattr->msg_id, &data, 1);
	if (ret)
		return ret;

	src_ind = FIELD_GET(FREQ_SRC_IND_MASK, data);
	for (index = 0; index < ARRAY_SIZE(freqlimit_srcnames); index++) {
		if (!src_ind)
			break;
		if (src_ind & 1)
			len += sysfs_emit_at(buf, len, "%s\n", freqlimit_srcnames[index]);
		src_ind >>= 1;
	}
	return len;
}

static int init_acpi(struct device *dev)
{
	u16 sock_ind;
	int ret;

	ret = hsmp_get_uid(dev, &sock_ind);
	if (ret)
		return ret;
	if (sock_ind >= hsmp_pdev->num_sockets)
		return -EINVAL;

	ret = hsmp_parse_acpi_table(dev, sock_ind);
	if (ret) {
		dev_err(dev, "Failed to parse ACPI table\n");
		return ret;
	}

	/* Test the hsmp interface */
	ret = hsmp_test(sock_ind, 0xDEADBEEF);
	if (ret) {
		dev_err(dev, "HSMP test message failed on Fam:%x model:%x\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);
		dev_err(dev, "Is HSMP disabled in BIOS ?\n");
		return ret;
	}

	ret = hsmp_cache_proto_ver(sock_ind);
	if (ret) {
		dev_err(dev, "Failed to read HSMP protocol version\n");
		return ret;
	}

	if (hsmp_pdev->proto_ver == HSMP_PROTO_VER6) {
		ret = hsmp_get_tbl_dram_base(sock_ind);
		if (ret)
			dev_info(dev, "Failed to init metric table\n");
	}

	ret = hsmp_create_sensor(dev, sock_ind);
	if (ret)
		dev_info(dev, "Failed to register HSMP sensors with hwmon\n");

	dev_set_drvdata(dev, &hsmp_pdev->sock[sock_ind]);

	return 0;
}

static const struct bin_attribute  hsmp_metric_tbl_attr = {
	.attr = { .name = HSMP_METRICS_TABLE_NAME, .mode = 0444},
	.read = hsmp_metric_tbl_acpi_read,
	.size = sizeof(struct hsmp_metric_table),
};

static const struct bin_attribute *hsmp_attr_list[] = {
	&hsmp_metric_tbl_attr,
	NULL
};

#define HSMP_DEV_ATTR(_name, _msg_id, _show, _mode)	\
static struct hsmp_sys_attr hattr_##_name = {		\
	.dattr = __ATTR(_name, _mode, _show, NULL),	\
	.msg_id = _msg_id,				\
}

HSMP_DEV_ATTR(c0_residency_input, HSMP_GET_C0_PERCENT, hsmp_msg_resp32_show, 0444);
HSMP_DEV_ATTR(prochot_status, HSMP_GET_PROC_HOT, hsmp_msg_resp32_show, 0444);
HSMP_DEV_ATTR(smu_fw_version, HSMP_GET_SMU_VER, hsmp_msg_fw_ver_show, 0444);
HSMP_DEV_ATTR(protocol_version, HSMP_GET_PROTO_VER, hsmp_msg_resp32_show, 0444);
HSMP_DEV_ATTR(cclk_freq_limit_input, HSMP_GET_CCLK_THROTTLE_LIMIT, hsmp_msg_resp32_show, 0444);
HSMP_DEV_ATTR(ddr_max_bw, HSMP_GET_DDR_BANDWIDTH, hsmp_ddr_max_bw_show, 0444);
HSMP_DEV_ATTR(ddr_utilised_bw_input, HSMP_GET_DDR_BANDWIDTH, hsmp_ddr_util_bw_show, 0444);
HSMP_DEV_ATTR(ddr_utilised_bw_perc_input, HSMP_GET_DDR_BANDWIDTH, hsmp_ddr_util_bw_perc_show, 0444);
HSMP_DEV_ATTR(fclk_input, HSMP_GET_FCLK_MCLK, hsmp_fclk_show, 0444);
HSMP_DEV_ATTR(mclk_input, HSMP_GET_FCLK_MCLK, hsmp_mclk_show, 0444);
HSMP_DEV_ATTR(clk_fmax, HSMP_GET_SOCKET_FMAX_FMIN, hsmp_clk_fmax_show, 0444);
HSMP_DEV_ATTR(clk_fmin, HSMP_GET_SOCKET_FMAX_FMIN, hsmp_clk_fmin_show, 0444);
HSMP_DEV_ATTR(pwr_current_active_freq_limit, HSMP_GET_SOCKET_FREQ_LIMIT,
	      hsmp_freq_limit_show, 0444);
HSMP_DEV_ATTR(pwr_current_active_freq_limit_source, HSMP_GET_SOCKET_FREQ_LIMIT,
	      hsmp_freq_limit_source_show, 0444);

static struct attribute *hsmp_dev_attr_list[] = {
	&hattr_c0_residency_input.dattr.attr,
	&hattr_prochot_status.dattr.attr,
	&hattr_smu_fw_version.dattr.attr,
	&hattr_protocol_version.dattr.attr,
	&hattr_cclk_freq_limit_input.dattr.attr,
	&hattr_ddr_max_bw.dattr.attr,
	&hattr_ddr_utilised_bw_input.dattr.attr,
	&hattr_ddr_utilised_bw_perc_input.dattr.attr,
	&hattr_fclk_input.dattr.attr,
	&hattr_mclk_input.dattr.attr,
	&hattr_clk_fmax.dattr.attr,
	&hattr_clk_fmin.dattr.attr,
	&hattr_pwr_current_active_freq_limit.dattr.attr,
	&hattr_pwr_current_active_freq_limit_source.dattr.attr,
	NULL
};

static const struct attribute_group hsmp_attr_grp = {
	.bin_attrs = hsmp_attr_list,
	.attrs = hsmp_dev_attr_list,
	.is_bin_visible = hsmp_is_sock_attr_visible,
	.is_visible = hsmp_is_sock_dev_attr_visible,
};

static const struct attribute_group *hsmp_groups[] = {
	&hsmp_attr_grp,
	NULL
};

static const struct acpi_device_id amd_hsmp_acpi_ids[] = {
	{ACPI_HSMP_DEVICE_HID, 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, amd_hsmp_acpi_ids);

static int hsmp_acpi_probe(struct platform_device *pdev)
{
	int ret;

	hsmp_pdev = get_hsmp_pdev();
	if (!hsmp_pdev)
		return -ENOMEM;

	if (!hsmp_pdev->is_probed) {
		hsmp_pdev->num_sockets = amd_num_nodes();
		if (hsmp_pdev->num_sockets == 0 || hsmp_pdev->num_sockets > MAX_AMD_NUM_NODES) {
			dev_err(&pdev->dev, "Wrong number of sockets\n");
			return -ENODEV;
		}

		hsmp_pdev->sock = devm_kcalloc(&pdev->dev, hsmp_pdev->num_sockets,
					       sizeof(*hsmp_pdev->sock),
					       GFP_KERNEL);
		if (!hsmp_pdev->sock)
			return -ENOMEM;
	}

	ret = init_acpi(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize HSMP interface.\n");
		return ret;
	}

	if (!hsmp_pdev->is_probed) {
		ret = hsmp_misc_register(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register misc device\n");
			return ret;
		}
		hsmp_pdev->is_probed = true;
		dev_dbg(&pdev->dev, "AMD HSMP ACPI is probed successfully\n");
	}

	return 0;
}

static void hsmp_acpi_remove(struct platform_device *pdev)
{
	/*
	 * We register only one misc_device even on multi-socket system.
	 * So, deregister should happen only once.
	 */
	if (hsmp_pdev->is_probed) {
		hsmp_misc_deregister();
		hsmp_pdev->is_probed = false;
	}
}

static struct platform_driver amd_hsmp_driver = {
	.probe		= hsmp_acpi_probe,
	.remove		= hsmp_acpi_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.acpi_match_table = amd_hsmp_acpi_ids,
		.dev_groups = hsmp_groups,
	},
};

module_platform_driver(amd_hsmp_driver);

MODULE_IMPORT_NS("AMD_HSMP");
MODULE_DESCRIPTION("AMD HSMP Platform Interface Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
