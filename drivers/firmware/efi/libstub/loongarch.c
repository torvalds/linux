// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Yun Liu <liuyun@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/efi.h>
#include <asm/addrspace.h>
#include "efistub.h"
#include "loongarch-stub.h"

typedef void __noreturn (*kernel_entry_t)(bool efi, unsigned long cmdline,
					  unsigned long systab);

efi_status_t check_platform_features(void)
{
	return EFI_SUCCESS;
}

struct exit_boot_struct {
	efi_memory_desc_t	*runtime_map;
	int			runtime_entry_count;
};

static efi_status_t exit_boot_func(struct efi_boot_memmap *map, void *priv)
{
	struct exit_boot_struct *p = priv;

	/*
	 * Update the memory map with virtual addresses. The function will also
	 * populate @runtime_map with copies of just the EFI_MEMORY_RUNTIME
	 * entries so that we can pass it straight to SetVirtualAddressMap()
	 */
	efi_get_virtmap(map->map, map->map_size, map->desc_size,
			p->runtime_map, &p->runtime_entry_count);

	return EFI_SUCCESS;
}

unsigned long __weak kernel_entry_address(unsigned long kernel_addr,
		efi_loaded_image_t *image)
{
	return *(unsigned long *)(kernel_addr + 8) - VMLINUX_LOAD_ADDRESS + kernel_addr;
}

efi_status_t efi_boot_kernel(void *handle, efi_loaded_image_t *image,
			     unsigned long kernel_addr, char *cmdline_ptr)
{
	kernel_entry_t real_kernel_entry;
	struct exit_boot_struct priv;
	unsigned long desc_size;
	efi_status_t status;
	u32 desc_ver;

	status = efi_alloc_virtmap(&priv.runtime_map, &desc_size, &desc_ver);
	if (status != EFI_SUCCESS) {
		efi_err("Unable to retrieve UEFI memory map.\n");
		return status;
	}

	efi_info("Exiting boot services\n");

	efi_novamap = false;
	status = efi_exit_boot_services(handle, &priv, exit_boot_func);
	if (status != EFI_SUCCESS)
		return status;

	/* Install the new virtual address map */
	efi_rt_call(set_virtual_address_map,
		    priv.runtime_entry_count * desc_size, desc_size,
		    desc_ver, priv.runtime_map);

	/* Config Direct Mapping */
	csr_write64(CSR_DMW0_INIT, LOONGARCH_CSR_DMWIN0);
	csr_write64(CSR_DMW1_INIT, LOONGARCH_CSR_DMWIN1);

	real_kernel_entry = (void *)kernel_entry_address(kernel_addr, image);

	real_kernel_entry(true, (unsigned long)cmdline_ptr,
			  (unsigned long)efi_system_table);
}
