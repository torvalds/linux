// SPDX-License-Identifier: GPL-2.0-only
/*
 * EFI stub implementation that is shared by arm and arm64 architectures.
 * This should be #included by the EFI stub implementation files.
 *
 * Copyright (C) 2013,2014 Linaro Limited
 *     Roy Franz <roy.franz@linaro.org
 * Copyright (C) 2013 Red Hat, Inc.
 *     Mark Salter <msalter@redhat.com>
 */

#include <linux/efi.h>
#include <linux/sort.h>
#include <asm/efi.h>

#include "efistub.h"

/*
 * This is the base address at which to start allocating virtual memory ranges
 * for UEFI Runtime Services. This is in the low TTBR0 range so that we can use
 * any allocation we choose, and eliminate the risk of a conflict after kexec.
 * The value chosen is the largest non-zero power of 2 suitable for this purpose
 * both on 32-bit and 64-bit ARM CPUs, to maximize the likelihood that it can
 * be mapped efficiently.
 * Since 32-bit ARM could potentially execute with a 1G/3G user/kernel split,
 * map everything below 1 GB. (512 MB is a reasonable upper bound for the
 * entire footprint of the UEFI runtime services memory regions)
 */
#define EFI_RT_VIRTUAL_BASE	SZ_512M
#define EFI_RT_VIRTUAL_SIZE	SZ_512M

#ifdef CONFIG_ARM64
# define EFI_RT_VIRTUAL_LIMIT	DEFAULT_MAP_WINDOW_64
#else
# define EFI_RT_VIRTUAL_LIMIT	TASK_SIZE
#endif

static u64 virtmap_base = EFI_RT_VIRTUAL_BASE;

static efi_system_table_t *__efistub_global sys_table;

__pure efi_system_table_t *efi_system_table(void)
{
	return sys_table;
}

static struct screen_info *setup_graphics(void)
{
	efi_guid_t gop_proto = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	efi_status_t status;
	unsigned long size;
	void **gop_handle = NULL;
	struct screen_info *si = NULL;

	size = 0;
	status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
			     &gop_proto, NULL, &size, gop_handle);
	if (status == EFI_BUFFER_TOO_SMALL) {
		si = alloc_screen_info();
		if (!si)
			return NULL;
		efi_setup_gop(si, &gop_proto, size);
	}
	return si;
}

void install_memreserve_table(void)
{
	struct linux_efi_memreserve *rsv;
	efi_guid_t memreserve_table_guid = LINUX_EFI_MEMRESERVE_TABLE_GUID;
	efi_status_t status;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, sizeof(*rsv),
			     (void **)&rsv);
	if (status != EFI_SUCCESS) {
		pr_efi_err("Failed to allocate memreserve entry!\n");
		return;
	}

	rsv->next = 0;
	rsv->size = 0;
	atomic_set(&rsv->count, 0);

	status = efi_bs_call(install_configuration_table,
			     &memreserve_table_guid, rsv);
	if (status != EFI_SUCCESS)
		pr_efi_err("Failed to install memreserve config table!\n");
}


/*
 * This function handles the architcture specific differences between arm and
 * arm64 regarding where the kernel image must be loaded and any memory that
 * must be reserved. On failure it is required to free all
 * all allocations it has made.
 */
efi_status_t handle_kernel_image(unsigned long *image_addr,
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
unsigned long efi_entry(void *handle, efi_system_table_t *sys_table_arg,
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
	enum efi_secureboot_mode secure_boot;
	struct screen_info *si;

	sys_table = sys_table_arg;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	status = check_platform_features();
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
		pr_efi_err("Failed to get loaded image protocol\n");
		goto fail;
	}

	dram_base = get_dram_base();
	if (dram_base == EFI_ERROR) {
		pr_efi_err("Failed to find DRAM base\n");
		goto fail;
	}

	/*
	 * Get the command line from EFI, using the LOADED_IMAGE
	 * protocol. We are going to copy the command line into the
	 * device tree, so this can be allocated anywhere.
	 */
	cmdline_ptr = efi_convert_cmdline(image, &cmdline_size);
	if (!cmdline_ptr) {
		pr_efi_err("getting command line via LOADED_IMAGE_PROTOCOL\n");
		goto fail;
	}

	if (IS_ENABLED(CONFIG_CMDLINE_EXTEND) ||
	    IS_ENABLED(CONFIG_CMDLINE_FORCE) ||
	    cmdline_size == 0)
		efi_parse_options(CONFIG_CMDLINE);

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE) && cmdline_size > 0)
		efi_parse_options(cmdline_ptr);

	pr_efi("Booting Linux Kernel...\n");

	si = setup_graphics();

	status = handle_kernel_image(image_addr, &image_size,
				     &reserve_addr,
				     &reserve_size,
				     dram_base, image);
	if (status != EFI_SUCCESS) {
		pr_efi_err("Failed to relocate kernel\n");
		goto fail_free_cmdline;
	}

	efi_retrieve_tpm2_eventlog();

	/* Ask the firmware to clear memory on unclean shutdown */
	efi_enable_reset_attack_mitigation();

	secure_boot = efi_get_secureboot();

	/*
	 * Unauthenticated device tree data is a security hazard, so ignore
	 * 'dtb=' unless UEFI Secure Boot is disabled.  We assume that secure
	 * boot is enabled if we can't determine its state.
	 */
	if (!IS_ENABLED(CONFIG_EFI_ARMSTUB_DTB_LOADER) ||
	     secure_boot != efi_secureboot_mode_disabled) {
		if (strstr(cmdline_ptr, "dtb="))
			pr_efi("Ignoring DTB from command line.\n");
	} else {
		status = handle_cmdline_files(image, cmdline_ptr, "dtb=",
					      ~0UL, &fdt_addr, &fdt_size);

		if (status != EFI_SUCCESS) {
			pr_efi_err("Failed to load device tree!\n");
			goto fail_free_image;
		}
	}

	if (fdt_addr) {
		pr_efi("Using DTB from command line\n");
	} else {
		/* Look for a device tree configuration table entry. */
		fdt_addr = (uintptr_t)get_fdt(&fdt_size);
		if (fdt_addr)
			pr_efi("Using DTB from configuration table\n");
	}

	if (!fdt_addr)
		pr_efi("Generating empty DTB\n");

	status = handle_cmdline_files(image, cmdline_ptr, "initrd=",
				      efi_get_max_initrd_addr(dram_base,
							      *image_addr),
				      (unsigned long *)&initrd_addr,
				      (unsigned long *)&initrd_size);
	if (status != EFI_SUCCESS)
		pr_efi_err("Failed initrd from command line!\n");

	efi_random_get_seed();

	/* hibernation expects the runtime regions to stay in the same place */
	if (!IS_ENABLED(CONFIG_HIBERNATION) && !nokaslr()) {
		/*
		 * Randomize the base of the UEFI runtime services region.
		 * Preserve the 2 MB alignment of the region by taking a
		 * shift of 21 bit positions into account when scaling
		 * the headroom value using a 32-bit random value.
		 */
		static const u64 headroom = EFI_RT_VIRTUAL_LIMIT -
					    EFI_RT_VIRTUAL_BASE -
					    EFI_RT_VIRTUAL_SIZE;
		u32 rnd;

		status = efi_get_random_bytes(sizeof(rnd), (u8 *)&rnd);
		if (status == EFI_SUCCESS) {
			virtmap_base = EFI_RT_VIRTUAL_BASE +
				       (((headroom >> 21) * rnd) >> (32 - 21));
		}
	}

	install_memreserve_table();

	new_fdt_addr = fdt_addr;
	status = allocate_new_fdt_and_exit_boot(handle,
				&new_fdt_addr, efi_get_max_fdt_addr(dram_base),
				initrd_addr, initrd_size, cmdline_ptr,
				fdt_addr, fdt_size);

	/*
	 * If all went well, we need to return the FDT address to the
	 * calling function so it can be passed to kernel as part of
	 * the kernel boot protocol.
	 */
	if (status == EFI_SUCCESS)
		return new_fdt_addr;

	pr_efi_err("Failed to update FDT and exit boot services\n");

	efi_free(initrd_size, initrd_addr);
	efi_free(fdt_size, fdt_addr);

fail_free_image:
	efi_free(image_size, *image_addr);
	efi_free(reserve_size, reserve_addr);
fail_free_cmdline:
	free_screen_info(si);
	efi_free(cmdline_size, (unsigned long)cmdline_ptr);
fail:
	return EFI_ERROR;
}

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
	u64 efi_virt_base = virtmap_base;
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
	if (IS_ENABLED(CONFIG_ARM64))
		sort(memory_map, map_size / desc_size, desc_size, cmp_mem_desc,
		     NULL);

	for (l = 0; l < map_size; l += desc_size, prev = in) {
		u64 paddr, size;

		in = (void *)memory_map + l;
		if (!(in->attribute & EFI_MEMORY_RUNTIME))
			continue;

		paddr = in->phys_addr;
		size = in->num_pages * EFI_PAGE_SIZE;

		if (novamap()) {
			in->virt_addr = in->phys_addr;
			continue;
		}

		/*
		 * Make the mapping compatible with 64k pages: this allows
		 * a 4k page size kernel to kexec a 64k page size kernel and
		 * vice versa.
		 */
		if ((IS_ENABLED(CONFIG_ARM64) &&
		     !regions_are_adjacent(prev, in)) ||
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
