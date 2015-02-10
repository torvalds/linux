/*
 * EFI stub implementation that is shared by arm and arm64 architectures.
 * This should be #included by the EFI stub implementation files.
 *
 * Copyright (C) 2013,2014 Linaro Limited
 *     Roy Franz <roy.franz@linaro.org
 * Copyright (C) 2013 Red Hat, Inc.
 *     Mark Salter <msalter@redhat.com>
 *
 * This file is part of the Linux kernel, and is made available under the
 * terms of the GNU General Public License version 2.
 *
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

static int __init efi_secureboot_enabled(efi_system_table_t *sys_table_arg)
{
	static efi_guid_t const var_guid __initconst = EFI_GLOBAL_VARIABLE_GUID;
	static efi_char16_t const var_name[] __initconst = {
		'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0 };

	efi_get_variable_t *f_getvar = sys_table_arg->runtime->get_variable;
	unsigned long size = sizeof(u8);
	efi_status_t status;
	u8 val;

	status = f_getvar((efi_char16_t *)var_name, (efi_guid_t *)&var_guid,
			  NULL, &size, &val);

	switch (status) {
	case EFI_SUCCESS:
		return val;
	case EFI_NOT_FOUND:
		return 0;
	default:
		return 1;
	}
}

efi_status_t efi_open_volume(efi_system_table_t *sys_table_arg,
			     void *__image, void **__fh)
{
	efi_file_io_interface_t *io;
	efi_loaded_image_t *image = __image;
	efi_file_handle_t *fh;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_status_t status;
	void *handle = (void *)(unsigned long)image->device_handle;

	status = sys_table_arg->boottime->handle_protocol(handle,
				 &fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to handle fs_proto\n");
		return status;
	}

	status = io->open_volume(io, &fh);
	if (status != EFI_SUCCESS)
		efi_printk(sys_table_arg, "Failed to open volume\n");

	*__fh = fh;
	return status;
}

efi_status_t efi_file_close(void *handle)
{
	efi_file_handle_t *fh = handle;

	return fh->close(handle);
}

efi_status_t
efi_file_read(void *handle, unsigned long *size, void *addr)
{
	efi_file_handle_t *fh = handle;

	return fh->read(handle, size, addr);
}


efi_status_t
efi_file_size(efi_system_table_t *sys_table_arg, void *__fh,
	      efi_char16_t *filename_16, void **handle, u64 *file_sz)
{
	efi_file_handle_t *h, *fh = __fh;
	efi_file_info_t *info;
	efi_status_t status;
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	unsigned long info_sz;

	status = fh->open(fh, &h, filename_16, EFI_FILE_MODE_READ, (u64)0);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to open file: ");
		efi_char16_printk(sys_table_arg, filename_16);
		efi_printk(sys_table_arg, "\n");
		return status;
	}

	*handle = h;

	info_sz = 0;
	status = h->get_info(h, &info_guid, &info_sz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		efi_printk(sys_table_arg, "Failed to get file info size\n");
		return status;
	}

grow:
	status = sys_table_arg->boottime->allocate_pool(EFI_LOADER_DATA,
				 info_sz, (void **)&info);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to alloc mem for file info\n");
		return status;
	}

	status = h->get_info(h, &info_guid, &info_sz,
						   info);
	if (status == EFI_BUFFER_TOO_SMALL) {
		sys_table_arg->boottime->free_pool(info);
		goto grow;
	}

	*file_sz = info->file_size;
	sys_table_arg->boottime->free_pool(info);

	if (status != EFI_SUCCESS)
		efi_printk(sys_table_arg, "Failed to get initrd info\n");

	return status;
}



void efi_char16_printk(efi_system_table_t *sys_table_arg,
			      efi_char16_t *str)
{
	struct efi_simple_text_output_protocol *out;

	out = (struct efi_simple_text_output_protocol *)sys_table_arg->con_out;
	out->output_string(out, str);
}


/*
 * This function handles the architcture specific differences between arm and
 * arm64 regarding where the kernel image must be loaded and any memory that
 * must be reserved. On failure it is required to free all
 * all allocations it has made.
 */
efi_status_t handle_kernel_image(efi_system_table_t *sys_table,
				 unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 unsigned long dram_base,
				 efi_loaded_image_t *image);
/*
 * EFI entry point for the arm/arm64 EFI stubs.  This is the entrypoint
 * that is described in the PE/COFF header.  Most of the code is the same
 * for both archictectures, with the arch-specific code provided in the
 * handle_kernel_image() function.
 */
unsigned long __init efi_entry(void *handle, efi_system_table_t *sys_table,
			       unsigned long *image_addr)
{
	efi_loaded_image_t *image;
	efi_status_t status;
	unsigned long image_size = 0;
	unsigned long dram_base;
	/* addr/point and size pairs for memory management*/
	unsigned long initrd_addr;
	u64 initrd_size = 0;
	unsigned long fdt_addr = 0;  /* Original DTB */
	u64 fdt_size = 0;  /* We don't get size from configuration table */
	char *cmdline_ptr = NULL;
	int cmdline_size = 0;
	unsigned long new_fdt_addr;
	efi_guid_t loaded_image_proto = LOADED_IMAGE_PROTOCOL_GUID;
	unsigned long reserve_addr = 0;
	unsigned long reserve_size = 0;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	pr_efi(sys_table, "Booting Linux Kernel...\n");

	/*
	 * Get a handle to the loaded image protocol.  This is used to get
	 * information about the running image, such as size and the command
	 * line.
	 */
	status = sys_table->boottime->handle_protocol(handle,
					&loaded_image_proto, (void *)&image);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Failed to get loaded image protocol\n");
		goto fail;
	}

	dram_base = get_dram_base(sys_table);
	if (dram_base == EFI_ERROR) {
		pr_efi_err(sys_table, "Failed to find DRAM base\n");
		goto fail;
	}
	status = handle_kernel_image(sys_table, image_addr, &image_size,
				     &reserve_addr,
				     &reserve_size,
				     dram_base, image);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Failed to relocate kernel\n");
		goto fail;
	}

	/*
	 * Get the command line from EFI, using the LOADED_IMAGE
	 * protocol. We are going to copy the command line into the
	 * device tree, so this can be allocated anywhere.
	 */
	cmdline_ptr = efi_convert_cmdline(sys_table, image, &cmdline_size);
	if (!cmdline_ptr) {
		pr_efi_err(sys_table, "getting command line via LOADED_IMAGE_PROTOCOL\n");
		goto fail_free_image;
	}

	status = efi_parse_options(cmdline_ptr);
	if (status != EFI_SUCCESS)
		pr_efi_err(sys_table, "Failed to parse EFI cmdline options\n");

	/*
	 * Unauthenticated device tree data is a security hazard, so
	 * ignore 'dtb=' unless UEFI Secure Boot is disabled.
	 */
	if (efi_secureboot_enabled(sys_table)) {
		pr_efi(sys_table, "UEFI Secure Boot is enabled.\n");
	} else {
		status = handle_cmdline_files(sys_table, image, cmdline_ptr,
					      "dtb=",
					      ~0UL, (unsigned long *)&fdt_addr,
					      (unsigned long *)&fdt_size);

		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table, "Failed to load device tree!\n");
			goto fail_free_cmdline;
		}
	}

	if (fdt_addr) {
		pr_efi(sys_table, "Using DTB from command line\n");
	} else {
		/* Look for a device tree configuration table entry. */
		fdt_addr = (uintptr_t)get_fdt(sys_table);
		if (fdt_addr)
			pr_efi(sys_table, "Using DTB from configuration table\n");
	}

	if (!fdt_addr)
		pr_efi(sys_table, "Generating empty DTB\n");

	status = handle_cmdline_files(sys_table, image, cmdline_ptr,
				      "initrd=", dram_base + SZ_512M,
				      (unsigned long *)&initrd_addr,
				      (unsigned long *)&initrd_size);
	if (status != EFI_SUCCESS)
		pr_efi_err(sys_table, "Failed initrd from command line!\n");

	new_fdt_addr = fdt_addr;
	status = allocate_new_fdt_and_exit_boot(sys_table, handle,
				&new_fdt_addr, dram_base + MAX_FDT_OFFSET,
				initrd_addr, initrd_size, cmdline_ptr,
				fdt_addr, fdt_size);

	/*
	 * If all went well, we need to return the FDT address to the
	 * calling function so it can be passed to kernel as part of
	 * the kernel boot protocol.
	 */
	if (status == EFI_SUCCESS)
		return new_fdt_addr;

	pr_efi_err(sys_table, "Failed to update FDT and exit boot services\n");

	efi_free(sys_table, initrd_size, initrd_addr);
	efi_free(sys_table, fdt_size, fdt_addr);

fail_free_cmdline:
	efi_free(sys_table, cmdline_size, (unsigned long)cmdline_ptr);

fail_free_image:
	efi_free(sys_table, image_size, *image_addr);
	efi_free(sys_table, reserve_size, reserve_addr);
fail:
	return EFI_ERROR;
}
