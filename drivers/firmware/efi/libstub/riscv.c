// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/efi.h>
#include <linux/libfdt.h>

#include <asm/efi.h>
#include <asm/unaligned.h>

#include "efistub.h"

typedef void __noreturn (*jump_kernel_func)(unsigned long, unsigned long);

static unsigned long hartid;

static int get_boot_hartid_from_fdt(void)
{
	const void *fdt;
	int chosen_node, len;
	const void *prop;

	fdt = get_efi_config_table(DEVICE_TREE_GUID);
	if (!fdt)
		return -EINVAL;

	chosen_node = fdt_path_offset(fdt, "/chosen");
	if (chosen_node < 0)
		return -EINVAL;

	prop = fdt_getprop((void *)fdt, chosen_node, "boot-hartid", &len);
	if (!prop)
		return -EINVAL;

	if (len == sizeof(u32))
		hartid = (unsigned long) fdt32_to_cpu(*(fdt32_t *)prop);
	else if (len == sizeof(u64))
		hartid = (unsigned long) fdt64_to_cpu(__get_unaligned_t(fdt64_t, prop));
	else
		return -EINVAL;

	return 0;
}

static efi_status_t get_boot_hartid_from_efi(void)
{
	efi_guid_t boot_protocol_guid = RISCV_EFI_BOOT_PROTOCOL_GUID;
	struct riscv_efi_boot_protocol *boot_protocol;
	efi_status_t status;

	status = efi_bs_call(locate_protocol, &boot_protocol_guid, NULL,
			     (void **)&boot_protocol);
	if (status != EFI_SUCCESS)
		return status;
	return efi_call_proto(boot_protocol, get_boot_hartid, &hartid);
}

efi_status_t check_platform_features(void)
{
	efi_status_t status;
	int ret;

	status = get_boot_hartid_from_efi();
	if (status != EFI_SUCCESS) {
		ret = get_boot_hartid_from_fdt();
		if (ret) {
			efi_err("Failed to get boot hartid!\n");
			return EFI_UNSUPPORTED;
		}
	}
	return EFI_SUCCESS;
}

unsigned long __weak stext_offset(void)
{
	/*
	 * This fallback definition is used by the EFI zboot stub, which loads
	 * the entire image so it can branch via the image header at offset #0.
	 */
	return 0;
}

void __noreturn efi_enter_kernel(unsigned long entrypoint, unsigned long fdt,
				 unsigned long fdt_size)
{
	unsigned long kernel_entry = entrypoint + stext_offset();
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
