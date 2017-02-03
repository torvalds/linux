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
#include <linux/sort.h>
#include <asm/efi.h>

#include "efistub.h"

bool __nokaslr;

static int efi_get_secureboot(efi_system_table_t *sys_table_arg)
{
	static efi_char16_t const sb_var_name[] = {
		'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0 };
	static efi_char16_t const sm_var_name[] = {
		'S', 'e', 't', 'u', 'p', 'M', 'o', 'd', 'e', 0 };

	efi_guid_t var_guid = EFI_GLOBAL_VARIABLE_GUID;
	efi_get_variable_t *f_getvar = sys_table_arg->runtime->get_variable;
	u8 val;
	unsigned long size = sizeof(val);
	efi_status_t status;

	status = f_getvar((efi_char16_t *)sb_var_name, (efi_guid_t *)&var_guid,
			  NULL, &size, &val);

	if (status != EFI_SUCCESS)
		goto out_efi_err;

	if (val == 0)
		return 0;

	status = f_getvar((efi_char16_t *)sm_var_name, (efi_guid_t *)&var_guid,
			  NULL, &size, &val);

	if (status != EFI_SUCCESS)
		goto out_efi_err;

	if (val == 1)
		return 0;

	return 1;

out_efi_err:
	switch (status) {
	case EFI_NOT_FOUND:
		return 0;
	case EFI_DEVICE_ERROR:
		return -EIO;
	case EFI_SECURITY_VIOLATION:
		return -EACCES;
	default:
		return -EINVAL;
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

static struct screen_info *setup_graphics(efi_system_table_t *sys_table_arg)
{
	efi_guid_t gop_proto = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	efi_status_t status;
	unsigned long size;
	void **gop_handle = NULL;
	struct screen_info *si = NULL;

	size = 0;
	status = efi_call_early(locate_handle, EFI_LOCATE_BY_PROTOCOL,
				&gop_proto, NULL, &size, gop_handle);
	if (status == EFI_BUFFER_TOO_SMALL) {
		si = alloc_screen_info(sys_table_arg);
		if (!si)
			return NULL;
		efi_setup_gop(sys_table_arg, si, &gop_proto, size);
	}
	return si;
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
unsigned long efi_entry(void *handle, efi_system_table_t *sys_table,
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
	unsigned long fdt_size = 0;
	char *cmdline_ptr = NULL;
	int cmdline_size = 0;
	unsigned long new_fdt_addr;
	efi_guid_t loaded_image_proto = LOADED_IMAGE_PROTOCOL_GUID;
	unsigned long reserve_addr = 0;
	unsigned long reserve_size = 0;
	int secure_boot = 0;
	struct screen_info *si;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	pr_efi(sys_table, "Booting Linux Kernel...\n");

	status = check_platform_features(sys_table);
	if (status != EFI_SUCCESS)
		goto fail;

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

	/*
	 * Get the command line from EFI, using the LOADED_IMAGE
	 * protocol. We are going to copy the command line into the
	 * device tree, so this can be allocated anywhere.
	 */
	cmdline_ptr = efi_convert_cmdline(sys_table, image, &cmdline_size);
	if (!cmdline_ptr) {
		pr_efi_err(sys_table, "getting command line via LOADED_IMAGE_PROTOCOL\n");
		goto fail;
	}

	/* check whether 'nokaslr' was passed on the command line */
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		static const u8 default_cmdline[] = CONFIG_CMDLINE;
		const u8 *str, *cmdline = cmdline_ptr;

		if (IS_ENABLED(CONFIG_CMDLINE_FORCE))
			cmdline = default_cmdline;
		str = strstr(cmdline, "nokaslr");
		if (str == cmdline || (str > cmdline && *(str - 1) == ' '))
			__nokaslr = true;
	}

	si = setup_graphics(sys_table);

	status = handle_kernel_image(sys_table, image_addr, &image_size,
				     &reserve_addr,
				     &reserve_size,
				     dram_base, image);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Failed to relocate kernel\n");
		goto fail_free_cmdline;
	}

	status = efi_parse_options(cmdline_ptr);
	if (status != EFI_SUCCESS)
		pr_efi_err(sys_table, "Failed to parse EFI cmdline options\n");

	secure_boot = efi_get_secureboot(sys_table);
	if (secure_boot > 0)
		pr_efi(sys_table, "UEFI Secure Boot is enabled.\n");

	if (secure_boot < 0) {
		pr_efi_err(sys_table,
			"could not determine UEFI Secure Boot status.\n");
	}

	/*
	 * Unauthenticated device tree data is a security hazard, so
	 * ignore 'dtb=' unless UEFI Secure Boot is disabled.
	 */
	if (secure_boot != 0 && strstr(cmdline_ptr, "dtb=")) {
		pr_efi(sys_table, "Ignoring DTB from command line.\n");
	} else {
		status = handle_cmdline_files(sys_table, image, cmdline_ptr,
					      "dtb=",
					      ~0UL, &fdt_addr, &fdt_size);

		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table, "Failed to load device tree!\n");
			goto fail_free_image;
		}
	}

	if (fdt_addr) {
		pr_efi(sys_table, "Using DTB from command line\n");
	} else {
		/* Look for a device tree configuration table entry. */
		fdt_addr = (uintptr_t)get_fdt(sys_table, &fdt_size);
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

	efi_random_get_seed(sys_table);

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

fail_free_image:
	efi_free(sys_table, image_size, *image_addr);
	efi_free(sys_table, reserve_size, reserve_addr);
fail_free_cmdline:
	free_screen_info(sys_table, si);
	efi_free(sys_table, cmdline_size, (unsigned long)cmdline_ptr);
fail:
	return EFI_ERROR;
}

/*
 * This is the base address at which to start allocating virtual memory ranges
 * for UEFI Runtime Services. This is in the low TTBR0 range so that we can use
 * any allocation we choose, and eliminate the risk of a conflict after kexec.
 * The value chosen is the largest non-zero power of 2 suitable for this purpose
 * both on 32-bit and 64-bit ARM CPUs, to maximize the likelihood that it can
 * be mapped efficiently.
 * Since 32-bit ARM could potentially execute with a 1G/3G user/kernel split,
 * map everything below 1 GB.
 */
#define EFI_RT_VIRTUAL_BASE	SZ_512M

static int cmp_mem_desc(const void *l, const void *r)
{
	const efi_memory_desc_t *left = l, *right = r;

	return (left->phys_addr > right->phys_addr) ? 1 : -1;
}

/*
 * Returns whether region @left ends exactly where region @right starts,
 * or false if either argument is NULL.
 */
static bool regions_are_adjacent(efi_memory_desc_t *left,
				 efi_memory_desc_t *right)
{
	u64 left_end;

	if (left == NULL || right == NULL)
		return false;

	left_end = left->phys_addr + left->num_pages * EFI_PAGE_SIZE;

	return left_end == right->phys_addr;
}

/*
 * Returns whether region @left and region @right have compatible memory type
 * mapping attributes, and are both EFI_MEMORY_RUNTIME regions.
 */
static bool regions_have_compatible_memory_type_attrs(efi_memory_desc_t *left,
						      efi_memory_desc_t *right)
{
	static const u64 mem_type_mask = EFI_MEMORY_WB | EFI_MEMORY_WT |
					 EFI_MEMORY_WC | EFI_MEMORY_UC |
					 EFI_MEMORY_RUNTIME;

	return ((left->attribute ^ right->attribute) & mem_type_mask) == 0;
}

/*
 * efi_get_virtmap() - create a virtual mapping for the EFI memory map
 *
 * This function populates the virt_addr fields of all memory region descriptors
 * in @memory_map whose EFI_MEMORY_RUNTIME attribute is set. Those descriptors
 * are also copied to @runtime_map, and their total count is returned in @count.
 */
void efi_get_virtmap(efi_memory_desc_t *memory_map, unsigned long map_size,
		     unsigned long desc_size, efi_memory_desc_t *runtime_map,
		     int *count)
{
	u64 efi_virt_base = EFI_RT_VIRTUAL_BASE;
	efi_memory_desc_t *in, *prev = NULL, *out = runtime_map;
	int l;

	/*
	 * To work around potential issues with the Properties Table feature
	 * introduced in UEFI 2.5, which may split PE/COFF executable images
	 * in memory into several RuntimeServicesCode and RuntimeServicesData
	 * regions, we need to preserve the relative offsets between adjacent
	 * EFI_MEMORY_RUNTIME regions with the same memory type attributes.
	 * The easiest way to find adjacent regions is to sort the memory map
	 * before traversing it.
	 */
	sort(memory_map, map_size / desc_size, desc_size, cmp_mem_desc, NULL);

	for (l = 0; l < map_size; l += desc_size, prev = in) {
		u64 paddr, size;

		in = (void *)memory_map + l;
		if (!(in->attribute & EFI_MEMORY_RUNTIME))
			continue;

		paddr = in->phys_addr;
		size = in->num_pages * EFI_PAGE_SIZE;

		/*
		 * Make the mapping compatible with 64k pages: this allows
		 * a 4k page size kernel to kexec a 64k page size kernel and
		 * vice versa.
		 */
		if (!regions_are_adjacent(prev, in) ||
		    !regions_have_compatible_memory_type_attrs(prev, in)) {

			paddr = round_down(in->phys_addr, SZ_64K);
			size += in->phys_addr - paddr;

			/*
			 * Avoid wasting memory on PTEs by choosing a virtual
			 * base that is compatible with section mappings if this
			 * region has the appropriate size and physical
			 * alignment. (Sections are 2 MB on 4k granule kernels)
			 */
			if (IS_ALIGNED(in->phys_addr, SZ_2M) && size >= SZ_2M)
				efi_virt_base = round_up(efi_virt_base, SZ_2M);
			else
				efi_virt_base = round_up(efi_virt_base, SZ_64K);
		}

		in->virt_addr = efi_virt_base + in->phys_addr - paddr;
		efi_virt_base += size;

		memcpy(out, in, desc_size);
		out = (void *)out + desc_size;
		++*count;
	}
}
