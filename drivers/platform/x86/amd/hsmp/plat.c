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
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "hsmp.h"

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

int hsmp_create_non_acpi_sysfs_if(struct device *dev)
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

	return device_add_groups(dev, hsmp_attr_grps);
}

static inline bool is_f1a_m0h(void)
{
	if (boot_cpu_data.x86 == 0x1A && boot_cpu_data.x86_model <= 0x0F)
		return true;

	return false;
}

int init_platform_device(struct device *dev)
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
