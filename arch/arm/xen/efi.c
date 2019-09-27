// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015, Linaro Limited, Shannon Zhao
 */

#include <linux/efi.h>
#include <xen/xen-ops.h>
#include <asm/xen/xen-ops.h>

/* Set XEN EFI runtime services function pointers. Other fields of struct efi,
 * e.g. efi.systab, will be set like normal EFI.
 */
void __init xen_efi_runtime_setup(void)
{
	efi.get_time                 = xen_efi_get_time;
	efi.set_time                 = xen_efi_set_time;
	efi.get_wakeup_time          = xen_efi_get_wakeup_time;
	efi.set_wakeup_time          = xen_efi_set_wakeup_time;
	efi.get_variable             = xen_efi_get_variable;
	efi.get_next_variable        = xen_efi_get_next_variable;
	efi.set_variable             = xen_efi_set_variable;
	efi.set_variable_nonblocking = xen_efi_set_variable;
	efi.query_variable_info      = xen_efi_query_variable_info;
	efi.query_variable_info_nonblocking = xen_efi_query_variable_info;
	efi.update_capsule           = xen_efi_update_capsule;
	efi.query_capsule_caps       = xen_efi_query_capsule_caps;
	efi.get_next_high_mono_count = xen_efi_get_next_high_mono_count;
	efi.reset_system             = xen_efi_reset_system;
}
EXPORT_SYMBOL_GPL(xen_efi_runtime_setup);
