// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2024, AMD.
 * All Rights Reserved.
 *
 * This file provides an ACPI based driver implementation for HSMP interface.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd_hsmp.h>
#include <asm/amd_nb.h>

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/ioport.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/uuid.h>

#include <uapi/asm-generic/errno-base.h>

#include "hsmp.h"

#define DRIVER_NAME		"amd_hsmp"
#define DRIVER_VERSION		"2.3"
#define ACPI_HSMP_DEVICE_HID	"AMDI0097"

/* These are the strings specified in ACPI table */
#define MSG_IDOFF_STR		"MsgIdOffset"
#define MSG_ARGOFF_STR		"MsgArgOffset"
#define MSG_RESPOFF_STR		"MsgRspOffset"

static struct hsmp_plat_device *hsmp_pdev;

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
					 struct bin_attribute *bin_attr, char *buf,
					 loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct hsmp_socket *sock = dev_get_drvdata(dev);

	return hsmp_metric_tbl_read(sock, buf, count);
}

static umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
					 struct bin_attribute *battr, int id)
{
	if (hsmp_pdev->proto_ver == HSMP_PROTO_VER6)
		return battr->attr.mode;

	return 0;
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
			dev_err(dev, "Failed to init metric table\n");
	}

	return ret;
}

static struct bin_attribute  hsmp_metric_tbl_attr = {
	.attr = { .name = HSMP_METRICS_TABLE_NAME, .mode = 0444},
	.read = hsmp_metric_tbl_acpi_read,
	.size = sizeof(struct hsmp_metric_table),
};

static struct bin_attribute *hsmp_attr_list[] = {
	&hsmp_metric_tbl_attr,
	NULL
};

static struct attribute_group hsmp_attr_grp = {
	.bin_attrs = hsmp_attr_list,
	.is_bin_visible = hsmp_is_sock_attr_visible,
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
		hsmp_pdev->num_sockets = amd_nb_num();
		if (hsmp_pdev->num_sockets == 0 || hsmp_pdev->num_sockets > MAX_AMD_SOCKETS)
			return -ENODEV;

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
		if (ret)
			return ret;
		hsmp_pdev->is_probed = true;
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

MODULE_IMPORT_NS(AMD_HSMP);
MODULE_DESCRIPTION("AMD HSMP Platform Interface Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
