// SPDX-License-Identifier: GPL-2.0-only

#include <linux/efi.h>
#include <linux/screen_info.h>

#include <asm/efi.h>

#include "efistub.h"

static unsigned long screen_info_offset;

struct screen_info *alloc_screen_info(void)
{
	if (IS_ENABLED(CONFIG_ARM))
		return __alloc_screen_info();
	return (void *)&screen_info + screen_info_offset;
}

/*
 * EFI entry point for the generic EFI stub used by ARM, arm64, RISC-V and
 * LoongArch. This is the entrypoint that is described in the PE/COFF header
 * of the core kernel.
 */
efi_status_t __efiapi efi_pe_entry(efi_handle_t handle,
				   efi_system_table_t *systab)
{
	efi_loaded_image_t *image;
	efi_status_t status;
	unsigned long image_addr;
	unsigned long image_size = 0;
	/* addr/point and size pairs for memory management*/
	char *cmdline_ptr = NULL;
	efi_guid_t loaded_image_proto = LOADED_IMAGE_PROTOCOL_GUID;
	unsigned long reserve_addr = 0;
	unsigned long reserve_size = 0;

	WRITE_ONCE(efi_system_table, systab);

	/* Check if we were booted by the EFI firmware */
	if (efi_system_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	/*
	 * Get a handle to the loaded image protocol.  This is used to get
	 * information about the running image, such as size and the command
	 * line.
	 */
	status = efi_bs_call(handle_protocol, handle, &loaded_image_proto,
			     (void *)&image);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to get loaded image protocol\n");
		return status;
	}

	status = efi_handle_cmdline(image, &cmdline_ptr);
	if (status != EFI_SUCCESS)
		return status;

	efi_info("Booting Linux Kernel...\n");

	status = handle_kernel_image(&image_addr, &image_size,
				     &reserve_addr,
				     &reserve_size,
				     image, handle);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to relocate kernel\n");
		return status;
	}

	screen_info_offset = image_addr - (unsigned long)image->image_base;

	status = efi_stub_common(handle, image, image_addr, cmdline_ptr);

	efi_free(image_size, image_addr);
	efi_free(reserve_size, reserve_addr);

	return status;
}
