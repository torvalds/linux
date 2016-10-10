/*
 * Copyright (c) 2014 Oracle Co., Daniel Kiper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
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

static const struct efi efi_xen __initconst = {
	.systab                   = NULL, /* Initialized later. */
	.runtime_version	  = 0,    /* Initialized later. */
	.mps                      = EFI_INVALID_TABLE_ADDR,
	.acpi                     = EFI_INVALID_TABLE_ADDR,
	.acpi20                   = EFI_INVALID_TABLE_ADDR,
	.smbios                   = EFI_INVALID_TABLE_ADDR,
	.smbios3                  = EFI_INVALID_TABLE_ADDR,
	.sal_systab               = EFI_INVALID_TABLE_ADDR,
	.boot_info                = EFI_INVALID_TABLE_ADDR,
	.hcdp                     = EFI_INVALID_TABLE_ADDR,
	.uga                      = EFI_INVALID_TABLE_ADDR,
	.uv_systab                = EFI_INVALID_TABLE_ADDR,
	.fw_vendor                = EFI_INVALID_TABLE_ADDR,
	.runtime                  = EFI_INVALID_TABLE_ADDR,
	.config_table             = EFI_INVALID_TABLE_ADDR,
	.get_time                 = xen_efi_get_time,
	.set_time                 = xen_efi_set_time,
	.get_wakeup_time          = xen_efi_get_wakeup_time,
	.set_wakeup_time          = xen_efi_set_wakeup_time,
	.get_variable             = xen_efi_get_variable,
	.get_next_variable        = xen_efi_get_next_variable,
	.set_variable             = xen_efi_set_variable,
	.query_variable_info      = xen_efi_query_variable_info,
	.update_capsule           = xen_efi_update_capsule,
	.query_capsule_caps       = xen_efi_query_capsule_caps,
	.get_next_high_mono_count = xen_efi_get_next_high_mono_count,
	.reset_system             = NULL, /* Functionality provided by Xen. */
	.set_virtual_address_map  = NULL, /* Not used under Xen. */
	.flags			  = 0     /* Initialized later. */
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

	efi = efi_xen;

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

void __init xen_efi_init(void)
{
	efi_system_table_t *efi_systab_xen;

	efi_systab_xen = xen_efi_probe();

	if (efi_systab_xen == NULL)
		return;

	strncpy((char *)&boot_params.efi_info.efi_loader_signature, "Xen",
			sizeof(boot_params.efi_info.efi_loader_signature));
	boot_params.efi_info.efi_systab = (__u32)__pa(efi_systab_xen);
	boot_params.efi_info.efi_systab_hi = (__u32)(__pa(efi_systab_xen) >> 32);

	set_bit(EFI_BOOT, &efi.flags);
	set_bit(EFI_PARAVIRT, &efi.flags);
	set_bit(EFI_64BIT, &efi.flags);
}
