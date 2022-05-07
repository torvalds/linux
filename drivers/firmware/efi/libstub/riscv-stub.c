// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/efi.h>
#include <linux/libfdt.h>

#include <asm/efi.h>
#include <asm/sections.h>

#include "efistub.h"

/*
 * RISC-V requires the kernel image to placed 2 MB aligned base for 64 bit and
 * 4MB for 32 bit.
 */
#ifdef CONFIG_64BIT
#define MIN_KIMG_ALIGN		SZ_2M
#else
#define MIN_KIMG_ALIGN		SZ_4M
#endif

typedef void __noreturn (*jump_kernel_func)(unsigned int, unsigned long);

static u32 hartid;

static u32 get_boot_hartid_from_fdt(void)
{
	const void *fdt;
	int chosen_node, len;
	const fdt32_t *prop;

	fdt = get_efi_config_table(DEVICE_TREE_GUID);
	if (!fdt)
		return U32_MAX;

	chosen_node = fdt_path_offset(fdt, "/chosen");
	if (chosen_node < 0)
		return U32_MAX;

	prop = fdt_getprop((void *)fdt, chosen_node, "boot-hartid", &len);
	if (!prop || len != sizeof(u32))
		return U32_MAX;

	return fdt32_to_cpu(*prop);
}

efi_status_t check_platform_features(void)
{
	hartid = get_boot_hartid_from_fdt();
	if (hartid == U32_MAX) {
		efi_err("/chosen/boot-hartid missing or invalid!\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

void __noreturn efi_enter_kernel(unsigned long entrypoint, unsigned long fdt,
				 unsigned long fdt_size)
{
	unsigned long stext_offset = _start_kernel - _start;
	unsigned long kernel_entry = entrypoint + stext_offset;
	jump_kernel_func jump_kernel = (jump_kernel_func)kernel_entry;

	/*
	 * Jump to real kernel here with following constraints.
	 * 1. MMU should be disabled.
	 * 2. a0 should contain hartid
	 * 3. a1 should DT address
	 */
	csr_write(CSR_SATP, 0);
	jump_kernel(hartid, fdt);
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 efi_loaded_image_t *image)
{
	unsigned long kernel_size = 0;
	unsigned long preferred_addr;
	efi_status_t status;

	kernel_size = _edata - _start;
	*image_addr = (unsigned long)_start;
	*image_size = kernel_size + (_end - _edata);

	/*
	 * RISC-V kernel maps PAGE_OFFSET virtual address to the same physical
	 * address where kernel is booted. That's why kernel should boot from
	 * as low as possible to avoid wastage of memory. Currently, dram_base
	 * is occupied by the firmware. So the preferred address for kernel to
	 * boot is next aligned address. If preferred address is not available,
	 * relocate_kernel will fall back to efi_low_alloc_above to allocate
	 * lowest possible memory region as long as the address and size meets
	 * the alignment constraints.
	 */
	preferred_addr = MIN_KIMG_ALIGN;
	status = efi_relocate_kernel(image_addr, kernel_size, *image_size,
				     preferred_addr, MIN_KIMG_ALIGN, 0x0);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to relocate kernel\n");
		*image_size = 0;
	}
	return status;
}
