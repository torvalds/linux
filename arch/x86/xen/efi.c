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

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/string.h>

#include <xen/xen-ops.h>

#include <asm/setup.h>

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
