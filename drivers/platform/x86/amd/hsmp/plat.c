// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2024, AMD.
 * All Rights Reserved.
 *
 * This file provides platform device implementations.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd_hsmp.h>

#include <linux/acpi.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include <asm/amd_node.h>

#include "hsmp.h"

#define DRIVER_NAME		"amd_hsmp"
#define DRIVER_VERSION		"2.3"

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

static struct hsmp_plat_device *hsmp_pdev;

static int amd_hsmp_pci_rdwr(struct hsmp_socket *sock, u32 offset,
			     u32 *value, bool write)
{
	return amd_smn_hsmp_rdwr(sock->sock_ind, sock->mbinfo.base_addr + offset, value, write);
}

static ssize_t hsmp_metric_tbl_plat_read(struct file *filp, struct kobject *kobj,
					 const struct bin_attribute *bin_attr, char *buf,
					 loff_t off, size_t count)
{
	struct hsmp_socket *sock;
	u16 sock_ind;

	sock_ind = (uintptr_t)bin_attr->private;
	if (sock_ind >= hsmp_pdev->num_sockets)
		return -EINVAL;

	sock = &hsmp_pdev->sock[sock_ind];

	return hsmp_metric_tbl_read(sock, buf, count);
}

static umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
					 const struct bin_attribute *battr, int id)
{
	u16 sock_ind;

	sock_ind = (uintptr_t)battr->private;

	if (id == 0 && sock_ind >= hsmp_pdev->num_sockets)
		return SYSFS_GROUP_INVISIBLE;

	if (hsmp_pdev->proto_ver == HSMP_PROTO_VER6)
		return battr->attr.mode;

	return 0;
}

/*
 * AMD supports maximum of 8 sockets in a system.
 * Static array of 8 + 1(for NULL) elements is created below
 * to create sysfs groups for sockets.
 * is_bin_visible function is used to show / hide the necessary groups.
 *
 * Validate the maximum number against MAX_AMD_NUM_NODES. If this changes,
 * then the attributes and groups below must be adjusted.
 */
static_assert(MAX_AMD_NUM_NODES == 8);

#define HSMP_BIN_ATTR(index, _list)					\
static const struct bin_attribute attr##index = {			\
	.attr = { .name = HSMP_METRICS_TABLE_NAME, .mode = 0444},	\
	.private = (void *)index,					\
	.read_new = hsmp_metric_tbl_plat_read,				\
	.size = sizeof(struct hsmp_metric_table),			\
};									\
static const struct bin_attribute _list[] = {				\
	&attr##index,							\
	NULL								\
}

HSMP_BIN_ATTR(0, *sock0_attr_list);
HSMP_BIN_ATTR(1, *sock1_attr_list);
HSMP_BIN_ATTR(2, *sock2_attr_list);
HSMP_BIN_ATTR(3, *sock3_attr_list);
HSMP_BIN_ATTR(4, *sock4_attr_list);
HSMP_BIN_ATTR(5, *sock5_attr_list);
HSMP_BIN_ATTR(6, *sock6_attr_list);
HSMP_BIN_ATTR(7, *sock7_attr_list);

#define HSMP_BIN_ATTR_GRP(index, _list, _name)			\
static const struct attribute_group sock##index##_attr_grp = {	\
	.bin_attrs_new = _list,					\
	.is_bin_visible = hsmp_is_sock_attr_visible,		\
	.name = #_name,						\
}

HSMP_BIN_ATTR_GRP(0, sock0_attr_list, socket0);
HSMP_BIN_ATTR_GRP(1, sock1_attr_list, socket1);
HSMP_BIN_ATTR_GRP(2, sock2_attr_list, socket2);
HSMP_BIN_ATTR_GRP(3, sock3_attr_list, socket3);
HSMP_BIN_ATTR_GRP(4, sock4_attr_list, socket4);
HSMP_BIN_ATTR_GRP(5, sock5_attr_list, socket5);
HSMP_BIN_ATTR_GRP(6, sock6_attr_list, socket6);
HSMP_BIN_ATTR_GRP(7, sock7_attr_list, socket7);

static const struct attribute_group *hsmp_groups[] = {
	&sock0_attr_grp,
	&sock1_attr_grp,
	&sock2_attr_grp,
	&sock3_attr_grp,
	&sock4_attr_grp,
	&sock5_attr_grp,
	&sock6_attr_grp,
	&sock7_attr_grp,
	NULL
};

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

	for (i = 0; i < hsmp_pdev->num_sockets; i++) {
		sock = &hsmp_pdev->sock[i];
		sock->sock_ind			= i;
		sock->dev			= dev;
		sock->mbinfo.base_addr		= SMN_HSMP_BASE;
		sock->amd_hsmp_rdwr		= amd_hsmp_pci_rdwr;

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

		ret = hsmp_cache_proto_ver(i);
		if (ret) {
			dev_err(dev, "Failed to read HSMP protocol version\n");
			return ret;
		}

		if (hsmp_pdev->proto_ver == HSMP_PROTO_VER6) {
			ret = hsmp_get_tbl_dram_base(i);
			if (ret)
				dev_err(dev, "Failed to init metric table\n");
		}
	}

	return 0;
}

static int hsmp_pltdrv_probe(struct platform_device *pdev)
{
	int ret;

	hsmp_pdev->sock = devm_kcalloc(&pdev->dev, hsmp_pdev->num_sockets,
				       sizeof(*hsmp_pdev->sock),
				       GFP_KERNEL);
	if (!hsmp_pdev->sock)
		return -ENOMEM;

	ret = init_platform_device(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init HSMP mailbox\n");
		return ret;
	}

	return hsmp_misc_register(&pdev->dev);
}

static void hsmp_pltdrv_remove(struct platform_device *pdev)
{
	hsmp_misc_deregister();
}

static struct platform_driver amd_hsmp_driver = {
	.probe		= hsmp_pltdrv_probe,
	.remove		= hsmp_pltdrv_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.dev_groups = hsmp_groups,
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
		case 0x00 ... 0x0F:
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

	if (!legacy_hsmp_support()) {
		pr_info("HSMP is not supported on Family:%x model:%x\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);
		return ret;
	}

	if (acpi_dev_present(ACPI_HSMP_DEVICE_HID, NULL, -1))
		return -ENODEV;

	hsmp_pdev = get_hsmp_pdev();
	if (!hsmp_pdev)
		return -ENOMEM;

	/*
	 * amd_num_nodes() returns number of SMN/DF interfaces present in the system
	 * if we have N SMN/DF interfaces that ideally means N sockets
	 */
	hsmp_pdev->num_sockets = amd_num_nodes();
	if (hsmp_pdev->num_sockets == 0 || hsmp_pdev->num_sockets > MAX_AMD_NUM_NODES)
		return ret;

	ret = platform_driver_register(&amd_hsmp_driver);
	if (ret)
		return ret;

	ret = hsmp_plat_dev_register();
	if (ret)
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

MODULE_IMPORT_NS("AMD_HSMP");
MODULE_DESCRIPTION("AMD HSMP Platform Interface Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
