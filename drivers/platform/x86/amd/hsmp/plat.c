// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2024, AMD.
 * All Rights Reserved.
 *
 * This file provides platform device implementations.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd_nb.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

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

#define HSMP_INDEX_REG		0xc4
#define HSMP_DATA_REG		0xc8

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

static int hsmp_create_non_acpi_sysfs_if(struct device *dev)
{
	const struct attribute_group **hsmp_attr_grps;
	struct attribute_group *attr_grp;
	u16 i;

	hsmp_attr_grps = devm_kcalloc(dev, hsmp_pdev.num_sockets + 1,
				      sizeof(*hsmp_attr_grps),
				      GFP_KERNEL);
	if (!hsmp_attr_grps)
		return -ENOMEM;

	/* Create a sysfs directory for each socket */
	for (i = 0; i < hsmp_pdev.num_sockets; i++) {
		attr_grp = devm_kzalloc(dev, sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		snprintf(hsmp_pdev.sock[i].name, HSMP_ATTR_GRP_NAME_SIZE, "socket%u", (u8)i);
		attr_grp->name			= hsmp_pdev.sock[i].name;
		attr_grp->is_bin_visible	= hsmp_is_sock_attr_visible;
		hsmp_attr_grps[i]		= attr_grp;

		hsmp_create_attr_list(attr_grp, dev, i);
	}

	return device_add_groups(dev, hsmp_attr_grps);
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

	for (i = 0; i < hsmp_pdev.num_sockets; i++) {
		if (!node_to_amd_nb(i))
			return -ENODEV;
		sock = &hsmp_pdev.sock[i];
		sock->root			= node_to_amd_nb(i)->root;
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
	}

	return 0;
}

static int hsmp_pltdrv_probe(struct platform_device *pdev)
{
	int ret;

	hsmp_pdev.sock = devm_kcalloc(&pdev->dev, hsmp_pdev.num_sockets,
				      sizeof(*hsmp_pdev.sock),
				      GFP_KERNEL);
	if (!hsmp_pdev.sock)
		return -ENOMEM;

	ret = init_platform_device(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init HSMP mailbox\n");
		return ret;
	}

	ret = hsmp_create_non_acpi_sysfs_if(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "Failed to create HSMP sysfs interface\n");

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

	if (!legacy_hsmp_support()) {
		pr_info("HSMP is not supported on Family:%x model:%x\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);
		return ret;
	}

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

MODULE_IMPORT_NS(AMD_HSMP);
MODULE_DESCRIPTION("AMD HSMP Platform Interface Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
