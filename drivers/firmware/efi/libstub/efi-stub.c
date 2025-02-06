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
#include <linux/screen_info.h>
#include <asm/efi.h>

#include "efistub.h"

/*
 * This is the base address at which to start allocating virtual memory ranges
 * for UEFI Runtime Services.
 *
 * For ARM/ARM64:
 * This is in the low TTBR0 range so that we can use
 * any allocation we choose, and eliminate the risk of a conflict after kexec.
 * The value chosen is the largest non-zero power of 2 suitable for this purpose
 * both on 32-bit and 64-bit ARM CPUs, to maximize the likelihood that it can
 * be mapped efficiently.
 * Since 32-bit ARM could potentially execute with a 1G/3G user/kernel split,
 * map everything below 1 GB. (512 MB is a reasonable upper bound for the
 * entire footprint of the UEFI runtime services memory regions)
 *
 * For RISC-V:
 * There is no specific reason for which, this address (512MB) can't be used
 * EFI runtime virtual address for RISC-V. It also helps to use EFI runtime
 * services on both RV32/RV64. Keep the same runtime virtual address for RISC-V
 * as well to minimize the code churn.
 */
#define EFI_RT_VIRTUAL_BASE	SZ_512M

/*
 * Some architectures map the EFI regions into the kernel's linear map using a
 * fixed offset.
 */
#ifndef EFI_RT_VIRTUAL_OFFSET
#define EFI_RT_VIRTUAL_OFFSET	0
#endif

static u64 virtmap_base = EFI_RT_VIRTUAL_BASE;
static bool flat_va_mapping = (EFI_RT_VIRTUAL_OFFSET != 0);

void __weak free_screen_info(struct screen_info *si)
{
}

static struct screen_info *setup_graphics(void)
{
	struct screen_info *si, tmp = {};

	if (efi_setup_gop(&tmp) != EFI_SUCCESS)
		return NULL;

	si = alloc_screen_info();
	if (!si)
		return NULL;

	*si = tmp;
	return si;
}

static void install_memreserve_table(void)
{
	struct linux_efi_memreserve *rsv;
	efi_guid_t memreserve_table_guid = LINUX_EFI_MEMRESERVE_TABLE_GUID;
	efi_status_t status;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, sizeof(*rsv),
			     (void **)&rsv);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to allocate memreserve entry!\n");
		return;
	}

	rsv->next = 0;
	rsv->size = 0;
	atomic_set(&rsv->count, 0);

	status = efi_bs_call(install_configuration_table,
			     &memreserve_table_guid, rsv);
	if (status != EFI_SUCCESS)
		efi_err("Failed to install memreserve config table!\n");
}

static u32 get_supported_rt_services(void)
{
	const efi_rt_properties_table_t *rt_prop_table;
	u32 supported = EFI_RT_SUPPORTED_ALL;

	rt_prop_table = get_efi_config_table(EFI_RT_PROPERTIES_TABLE_GUID);
	if (rt_prop_table)
		supported &= rt_prop_table->runtime_services_supported;

	return supported;
}

efi_status_t efi_handle_cmdline(efi_loaded_image_t *image, char **cmdline_ptr)
{
	char *cmdline __free(efi_pool) = NULL;
	efi_status_t status;

	/*
	 * Get the command line from EFI, using the LOADED_IMAGE
	 * protocol. We are going to copy the command line into the
	 * device tree, so this can be allocated anywhere.
	 */
	cmdline = efi_convert_cmdline(image);
	if (!cmdline) {
		efi_err("getting command line via LOADED_IMAGE_PROTOCOL\n");
		return EFI_OUT_OF_RESOURCES;
	}

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE)) {
		status = efi_parse_options(cmdline);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to parse EFI load options\n");
			return status;
		}
	}

	if (IS_ENABLED(CONFIG_CMDLINE_EXTEND) ||
	    IS_ENABLED(CONFIG_CMDLINE_FORCE) ||
	    cmdline[0] == 0) {
		status = efi_parse_options(CONFIG_CMDLINE);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to parse built-in command line\n");
			return status;
		}
	}

	*cmdline_ptr = no_free_ptr(cmdline);
	return EFI_SUCCESS;
}

efi_status_t efi_stub_common(efi_handle_t handle,
			     efi_loaded_image_t *image,
			     unsigned long image_addr,
			     char *cmdline_ptr)
{
	struct screen_info *si;
	efi_status_t status;

	status = check_platform_features();
	if (status != EFI_SUCCESS)
		return status;

	si = setup_graphics();

	efi_retrieve_eventlog();

	/* Ask the firmware to clear memory on unclean shutdown */
	efi_enable_reset_attack_mitigation();

	efi_load_initrd(image, ULONG_MAX, efi_get_max_initrd_addr(image_addr),
			NULL);

	efi_random_get_seed();

	/* force efi_novamap if SetVirtualAddressMap() is unsupported */
	efi_novamap |= !(get_supported_rt_services() &
			 EFI_RT_SUPPORTED_SET_VIRTUAL_ADDRESS_MAP);

	install_memreserve_table();

	status = efi_boot_kernel(handle, image, image_addr, cmdline_ptr);

	free_screen_info(si);
	return status;
}

/*
 * efi_allocate_virtmap() - create a pool allocation for the virtmap
 *
 * Create an allocation that is of sufficient size to hold all the memory
 * descriptors that will be passed to SetVirtualAddressMap() to inform the
 * firmware about the virtual mapping that will be used under the OS to call
 * into the firmware.
 */
efi_status_t efi_alloc_virtmap(efi_memory_desc_t **virtmap,
			       unsigned long *desc_size, u32 *desc_ver)
{
	unsigned long size, mmap_key;
	efi_status_t status;

	/*
	 * Use the size of the current memory map as an upper bound for the
	 * size of the buffer we need to pass to SetVirtualAddressMap() to
	 * cover all EFI_MEMORY_RUNTIME regions.
	 */
	size = 0;
	status = efi_bs_call(get_memory_map, &size, NULL, &mmap_key, desc_size,
			     desc_ver);
	if (status != EFI_BUFFER_TOO_SMALL)
		return EFI_LOAD_ERROR;

	return efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
			   (void **)virtmap);
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
	efi_memory_desc_t *in, *out = runtime_map;
	int l;

	*count = 0;

	for (l = 0; l < map_size; l += desc_size) {
		u64 paddr, size;

		in = (void *)memory_map + l;
		if (!(in->attribute & EFI_MEMORY_RUNTIME))
			continue;

		paddr = in->phys_addr;
		size = in->num_pages * EFI_PAGE_SIZE;

		in->virt_addr = in->phys_addr + EFI_RT_VIRTUAL_OFFSET;
		if (efi_novamap) {
			continue;
		}

		/*
		 * Make the mapping compatible with 64k pages: this allows
		 * a 4k page size kernel to kexec a 64k page size kernel and
		 * vice versa.
		 */
		if (!flat_va_mapping) {

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

			in->virt_addr += efi_virt_base - paddr;
			efi_virt_base += size;
		}

		memcpy(out, in, desc_size);
		out = (void *)out + desc_size;
		++*count;
	}
}
