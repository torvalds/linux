/*
 * EFI support for Xen.
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2005-2008 Intel Co.
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Bibo Mao <bibo.mao@intel.com>
 *	Chandramouli Narayanan <mouli@linux.intel.com>
 *	Huang Ying <ying.huang@intel.com>
 * Copyright (C) 2011 Novell Co.
 *	Jan Beulich <JBeulich@suse.com>
 * Copyright (C) 2011-2012 Oracle Co.
 *	Liang Tang <liang.tang@oracle.com>
 * Copyright (c) 2014 Oracle Co., Daniel Kiper
 */

#include <linux/bug.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/string.h>

#include <xen/interface/xen.h>
#include <xen/interface/platform.h>
#include <xen/xen.h>

#include <asm/xen/hypercall.h>

#define INIT_EFI_OP(name) \
	{.cmd = XENPF_efi_runtime_call, \
	 .u.efi_runtime_call.function = XEN_EFI_##name, \
	 .u.efi_runtime_call.misc = 0}

#define efi_data(op)	(op.u.efi_runtime_call)

static efi_status_t xen_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	struct xen_platform_op op = INIT_EFI_OP(get_time);

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	if (tm) {
		BUILD_BUG_ON(sizeof(*tm) != sizeof(efi_data(op).u.get_time.time));
		memcpy(tm, &efi_data(op).u.get_time.time, sizeof(*tm));
	}

	if (tc) {
		tc->resolution = efi_data(op).u.get_time.resolution;
		tc->accuracy = efi_data(op).u.get_time.accuracy;
		tc->sets_to_zero = !!(efi_data(op).misc &
				      XEN_EFI_GET_TIME_SET_CLEARS_NS);
	}

	return efi_data(op).status;
}

static efi_status_t xen_efi_set_time(efi_time_t *tm)
{
	struct xen_platform_op op = INIT_EFI_OP(set_time);

	BUILD_BUG_ON(sizeof(*tm) != sizeof(efi_data(op).u.set_time));
	memcpy(&efi_data(op).u.set_time, tm, sizeof(*tm));

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	return efi_data(op).status;
}

static efi_status_t xen_efi_get_wakeup_time(efi_bool_t *enabled,
					    efi_bool_t *pending,
					    efi_time_t *tm)
{
	struct xen_platform_op op = INIT_EFI_OP(get_wakeup_time);

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	if (tm) {
		BUILD_BUG_ON(sizeof(*tm) != sizeof(efi_data(op).u.get_wakeup_time));
		memcpy(tm, &efi_data(op).u.get_wakeup_time, sizeof(*tm));
	}

	if (enabled)
		*enabled = !!(efi_data(op).misc & XEN_EFI_GET_WAKEUP_TIME_ENABLED);

	if (pending)
		*pending = !!(efi_data(op).misc & XEN_EFI_GET_WAKEUP_TIME_PENDING);

	return efi_data(op).status;
}

static efi_status_t xen_efi_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	struct xen_platform_op op = INIT_EFI_OP(set_wakeup_time);

	BUILD_BUG_ON(sizeof(*tm) != sizeof(efi_data(op).u.set_wakeup_time));
	if (enabled)
		efi_data(op).misc = XEN_EFI_SET_WAKEUP_TIME_ENABLE;
	if (tm)
		memcpy(&efi_data(op).u.set_wakeup_time, tm, sizeof(*tm));
	else
		efi_data(op).misc |= XEN_EFI_SET_WAKEUP_TIME_ENABLE_ONLY;

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	return efi_data(op).status;
}

static efi_status_t xen_efi_get_variable(efi_char16_t *name,
					 efi_guid_t *vendor,
					 u32 *attr,
					 unsigned long *data_size,
					 void *data)
{
	struct xen_platform_op op = INIT_EFI_OP(get_variable);

	set_xen_guest_handle(efi_data(op).u.get_variable.name, name);
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(efi_data(op).u.get_variable.vendor_guid));
	memcpy(&efi_data(op).u.get_variable.vendor_guid, vendor, sizeof(*vendor));
	efi_data(op).u.get_variable.size = *data_size;
	set_xen_guest_handle(efi_data(op).u.get_variable.data, data);

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	*data_size = efi_data(op).u.get_variable.size;
	if (attr)
		*attr = efi_data(op).misc;

	return efi_data(op).status;
}

static efi_status_t xen_efi_get_next_variable(unsigned long *name_size,
					      efi_char16_t *name,
					      efi_guid_t *vendor)
{
	struct xen_platform_op op = INIT_EFI_OP(get_next_variable_name);

	efi_data(op).u.get_next_variable_name.size = *name_size;
	set_xen_guest_handle(efi_data(op).u.get_next_variable_name.name, name);
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(efi_data(op).u.get_next_variable_name.vendor_guid));
	memcpy(&efi_data(op).u.get_next_variable_name.vendor_guid, vendor,
	       sizeof(*vendor));

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	*name_size = efi_data(op).u.get_next_variable_name.size;
	memcpy(vendor, &efi_data(op).u.get_next_variable_name.vendor_guid,
	       sizeof(*vendor));

	return efi_data(op).status;
}

static efi_status_t xen_efi_set_variable(efi_char16_t *name,
					 efi_guid_t *vendor,
					 u32 attr,
					 unsigned long data_size,
					 void *data)
{
	struct xen_platform_op op = INIT_EFI_OP(set_variable);

	set_xen_guest_handle(efi_data(op).u.set_variable.name, name);
	efi_data(op).misc = attr;
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(efi_data(op).u.set_variable.vendor_guid));
	memcpy(&efi_data(op).u.set_variable.vendor_guid, vendor, sizeof(*vendor));
	efi_data(op).u.set_variable.size = data_size;
	set_xen_guest_handle(efi_data(op).u.set_variable.data, data);

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	return efi_data(op).status;
}

static efi_status_t xen_efi_query_variable_info(u32 attr,
						u64 *storage_space,
						u64 *remaining_space,
						u64 *max_variable_size)
{
	struct xen_platform_op op = INIT_EFI_OP(query_variable_info);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	efi_data(op).u.query_variable_info.attr = attr;

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	*storage_space = efi_data(op).u.query_variable_info.max_store_size;
	*remaining_space = efi_data(op).u.query_variable_info.remain_store_size;
	*max_variable_size = efi_data(op).u.query_variable_info.max_size;

	return efi_data(op).status;
}

static efi_status_t xen_efi_get_next_high_mono_count(u32 *count)
{
	struct xen_platform_op op = INIT_EFI_OP(get_next_high_monotonic_count);

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	*count = efi_data(op).misc;

	return efi_data(op).status;
}

static efi_status_t xen_efi_update_capsule(efi_capsule_header_t **capsules,
					   unsigned long count,
					   unsigned long sg_list)
{
	struct xen_platform_op op = INIT_EFI_OP(update_capsule);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	set_xen_guest_handle(efi_data(op).u.update_capsule.capsule_header_array,
			     capsules);
	efi_data(op).u.update_capsule.capsule_count = count;
	efi_data(op).u.update_capsule.sg_list = sg_list;

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	return efi_data(op).status;
}

static efi_status_t xen_efi_query_capsule_caps(efi_capsule_header_t **capsules,
					       unsigned long count,
					       u64 *max_size,
					       int *reset_type)
{
	struct xen_platform_op op = INIT_EFI_OP(query_capsule_capabilities);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	set_xen_guest_handle(efi_data(op).u.query_capsule_capabilities.capsule_header_array,
					capsules);
	efi_data(op).u.query_capsule_capabilities.capsule_count = count;

	if (HYPERVISOR_dom0_op(&op) < 0)
		return EFI_UNSUPPORTED;

	*max_size = efi_data(op).u.query_capsule_capabilities.max_capsule_size;
	*reset_type = efi_data(op).u.query_capsule_capabilities.reset_type;

	return efi_data(op).status;
}

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
	.memmap                   = NULL, /* Not used under Xen. */
	.flags			  = 0     /* Initialized later. */
};

efi_system_table_t __init *xen_efi_probe(void)
{
	struct xen_platform_op op = {
		.cmd = XENPF_firmware_info,
		.u.firmware_info = {
			.type = XEN_FW_EFI_INFO,
			.index = XEN_FW_EFI_CONFIG_TABLE
		}
	};
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;

	if (!xen_initial_domain() || HYPERVISOR_dom0_op(&op) < 0)
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

	if (HYPERVISOR_dom0_op(&op) == 0) {
		efi_systab_xen.fw_vendor = __pa_symbol(vendor);
		efi_systab_xen.fw_revision = info->vendor.revision;
	} else
		efi_systab_xen.fw_vendor = __pa_symbol(L"UNKNOWN");

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_VERSION;

	if (HYPERVISOR_dom0_op(&op) == 0)
		efi_systab_xen.hdr.revision = info->version;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_RT_VERSION;

	if (HYPERVISOR_dom0_op(&op) == 0)
		efi.runtime_version = info->version;

	return &efi_systab_xen;
}
