// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Oracle Co., Daniel Kiper
 */

#include <linux/bitops.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/string.h>

#include <xen/xen.h>
#include <xen/xen-ops.h>
#include <xen/interface/platform.h>

#include <asm/page.h>
#include <asm/setup.h>
#include <asm/xen/hypercall.h>

static efi_char16_t vendor[100] __initdata;

static efi_system_table_t efi_systab_xen __initdata = {
	.hdr = {
		.signature	= EFI_SYSTEM_TABLE_SIGNATURE,
		.revision	= 0, /* Initialized later. */
		.headersize	= 0, /* Ignored by Linux Kernel. */
		.crc32		= 0, /* Ignored by Linux Kernel. */
		.reserved	= 0
	},
	.fw_vendor	= EFI_INVALID_TABLE_ADDR, /* Initialized later. */
	.fw_revision	= 0,			  /* Initialized later. */
	.con_in_handle	= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.con_in		= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.con_out_handle	= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.con_out	= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.stderr_handle	= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.stderr		= EFI_INVALID_TABLE_ADDR, /* Not used under Xen. */
	.runtime	= (efi_runtime_services_t *)EFI_INVALID_TABLE_ADDR,
						  /* Not used under Xen. */
	.boottime	= (efi_boot_services_t *)EFI_INVALID_TABLE_ADDR,
						  /* Not used under Xen. */
	.nr_tables	= 0,			  /* Initialized later. */
	.tables		= EFI_INVALID_TABLE_ADDR  /* Initialized later. */
};

static efi_system_table_t __init *xen_efi_probe(void)
{
	struct xen_platform_op op = {
		.cmd = XENPF_firmware_info,
		.u.firmware_info = {
			.type = XEN_FW_EFI_INFO,
			.index = XEN_FW_EFI_CONFIG_TABLE
		}
	};
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;

	if (!xen_initial_domain() || HYPERVISOR_platform_op(&op) < 0)
		return NULL;

	/* Here we know that Xen runs on EFI platform. */
	xen_efi_runtime_setup();

	efi_systab_xen.tables = info->cfg.addr;
	efi_systab_xen.nr_tables = info->cfg.nent;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_VENDOR;
	info->vendor.bufsz = sizeof(vendor);
	set_xen_guest_handle(info->vendor.name, vendor);

	if (HYPERVISOR_platform_op(&op) == 0) {
		efi_systab_xen.fw_vendor = __pa_symbol(vendor);
		efi_systab_xen.fw_revision = info->vendor.revision;
	} else
		efi_systab_xen.fw_vendor = __pa_symbol(L"UNKNOWN");

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_VERSION;

	if (HYPERVISOR_platform_op(&op) == 0)
		efi_systab_xen.hdr.revision = info->version;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_RT_VERSION;

	if (HYPERVISOR_platform_op(&op) == 0)
		efi.runtime_version = info->version;

	return &efi_systab_xen;
}

/*
 * Determine whether we're in secure boot mode.
 *
 * Please keep the logic in sync with
 * drivers/firmware/efi/libstub/secureboot.c:efi_get_secureboot().
 */
static enum efi_secureboot_mode xen_efi_get_secureboot(void)
{
	static efi_guid_t efi_variable_guid = EFI_GLOBAL_VARIABLE_GUID;
	static efi_guid_t shim_guid = EFI_SHIM_LOCK_GUID;
	efi_status_t status;
	u8 moksbstate, secboot, setupmode;
	unsigned long size;

	size = sizeof(secboot);
	status = efi.get_variable(L"SecureBoot", &efi_variable_guid,
				  NULL, &size, &secboot);

	if (status == EFI_NOT_FOUND)
		return efi_secureboot_mode_disabled;

	if (status != EFI_SUCCESS)
		goto out_efi_err;

	size = sizeof(setupmode);
	status = efi.get_variable(L"SetupMode", &efi_variable_guid,
				  NULL, &size, &setupmode);

	if (status != EFI_SUCCESS)
		goto out_efi_err;

	if (secboot == 0 || setupmode == 1)
		return efi_secureboot_mode_disabled;

	/* See if a user has put the shim into insecure mode. */
	size = sizeof(moksbstate);
	status = efi.get_variable(L"MokSBStateRT", &shim_guid,
				  NULL, &size, &moksbstate);

	/* If it fails, we don't care why. Default to secure. */
	if (status != EFI_SUCCESS)
		goto secure_boot_enabled;

	if (moksbstate == 1)
		return efi_secureboot_mode_disabled;

 secure_boot_enabled:
	pr_info("UEFI Secure Boot is enabled.\n");
	return efi_secureboot_mode_enabled;

 out_efi_err:
	pr_err("Could not determine UEFI Secure Boot status.\n");
	return efi_secureboot_mode_unknown;
}

void __init xen_efi_init(struct boot_params *boot_params)
{
	efi_system_table_t *efi_systab_xen;

	efi_systab_xen = xen_efi_probe();

	if (efi_systab_xen == NULL)
		return;

	strncpy((char *)&boot_params->efi_info.efi_loader_signature, "Xen",
			sizeof(boot_params->efi_info.efi_loader_signature));
	boot_params->efi_info.efi_systab = (__u32)__pa(efi_systab_xen);
	boot_params->efi_info.efi_systab_hi = (__u32)(__pa(efi_systab_xen) >> 32);

	boot_params->secure_boot = xen_efi_get_secureboot();

	set_bit(EFI_BOOT, &efi.flags);
	set_bit(EFI_PARAVIRT, &efi.flags);
	set_bit(EFI_64BIT, &efi.flags);
}
