/*
 * Copyright (c) 2015, Linaro Limited, Shannon Zhao
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
