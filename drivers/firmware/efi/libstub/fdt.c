// SPDX-License-Identifier: GPL-2.0
/*
 * FDT related Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2013 Linaro Limited; author Roy Franz
 */

#include <linux/efi.h>
#include <linux/libfdt.h>
#include <asm/efi.h>

#include "efistub.h"

#define EFI_DT_ADDR_CELLS_DEFAULT 2
#define EFI_DT_SIZE_CELLS_DEFAULT 2

static void fdt_update_cell_size(void *fdt)
{
	int offset;

	offset = fdt_path_offset(fdt, "/");
	/* Set the #address-cells and #size-cells values for an empty tree */

	fdt_setprop_u32(fdt, offset, "#address-cells", EFI_DT_ADDR_CELLS_DEFAULT);
	fdt_setprop_u32(fdt, offset, "#size-cells",    EFI_DT_SIZE_CELLS_DEFAULT);
}

static efi_status_t update_fdt(void *orig_fdt, unsigned long orig_fdt_size,
			       void *fdt, int new_fdt_size, char *cmdline_ptr)
{
	int node, num_rsv;
	int status;
	u32 fdt_val32;
	u64 fdt_val64;

	/* Do some checks on provided FDT, if it exists: */
	if (orig_fdt) {
		if (fdt_check_header(orig_fdt)) {
			efi_err("Device Tree header not valid!\n");
			return EFI_LOAD_ERROR;
		}
		/*
		 * We don't get the size of the FDT if we get if from a
		 * configuration table:
		 */
		if (orig_fdt_size && fdt_totalsize(orig_fdt) > orig_fdt_size) {
			efi_err("Truncated device tree! foo!\n");
			return EFI_LOAD_ERROR;
		}
	}

	if (orig_fdt) {
		status = fdt_open_into(orig_fdt, fdt, new_fdt_size);
	} else {
		status = fdt_create_empty_tree(fdt, new_fdt_size);
		if (status == 0) {
			/*
			 * Any failure from the following function is
			 * non-critical:
			 */
			fdt_update_cell_size(fdt);
		}
	}

	if (status != 0)
		goto fdt_set_fail;

	/*
	 * Delete all memory reserve map entries. When booting via UEFI,
	 * kernel will use the UEFI memory map to find reserved regions.
	 */
	num_rsv = fdt_num_mem_rsv(fdt);
	while (num_rsv-- > 0)
		fdt_del_mem_rsv(fdt, num_rsv);

	node = fdt_subnode_offset(fdt, 0, "chosen");
	if (node < 0) {
		node = fdt_add_subnode(fdt, 0, "chosen");
		if (node < 0) {
			/* 'node' is an error code when negative: */
			status = node;
			goto fdt_set_fail;
		}
	}

	if (cmdline_ptr != NULL && strlen(cmdline_ptr) > 0) {
		status = fdt_setprop(fdt, node, "bootargs", cmdline_ptr,
				     strlen(cmdline_ptr) + 1);
		if (status)
			goto fdt_set_fail;
	}

	/* Add FDT entries for EFI runtime services in chosen node. */
	node = fdt_subnode_offset(fdt, 0, "chosen");
	fdt_val64 = cpu_to_fdt64((u64)(unsigned long)efi_system_table);

	status = fdt_setprop_var(fdt, node, "linux,uefi-system-table", fdt_val64);
	if (status)
		goto fdt_set_fail;

	fdt_val64 = U64_MAX; /* placeholder */

	status = fdt_setprop_var(fdt, node, "linux,uefi-mmap-start", fdt_val64);
	if (status)
		goto fdt_set_fail;

	fdt_val32 = U32_MAX; /* placeholder */

	status = fdt_setprop_var(fdt, node, "linux,uefi-mmap-size", fdt_val32);
	if (status)
		goto fdt_set_fail;

	status = fdt_setprop_var(fdt, node, "linux,uefi-mmap-desc-size", fdt_val32);
	if (status)
		goto fdt_set_fail;

	status = fdt_setprop_var(fdt, node, "linux,uefi-mmap-desc-ver", fdt_val32);
	if (status)
		goto fdt_set_fail;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && !efi_nokaslr) {
		efi_status_t efi_status;

		efi_status = efi_get_random_bytes(sizeof(fdt_val64),
						  (u8 *)&fdt_val64);
		if (efi_status == EFI_SUCCESS) {
			status = fdt_setprop_var(fdt, node, "kaslr-seed", fdt_val64);
			if (status)
				goto fdt_set_fail;
		}
	}

	/* Shrink the FDT back to its minimum size: */
	fdt_pack(fdt);

	return EFI_SUCCESS;

fdt_set_fail:
	if (status == -FDT_ERR_NOSPACE)
		return EFI_BUFFER_TOO_SMALL;

	return EFI_LOAD_ERROR;
}

static efi_status_t update_fdt_memmap(void *fdt, struct efi_boot_memmap *map)
{
	int node = fdt_path_offset(fdt, "/chosen");
	u64 fdt_val64;
	u32 fdt_val32;
	int err;

	if (node < 0)
		return EFI_LOAD_ERROR;

	fdt_val64 = cpu_to_fdt64((unsigned long)map->map);

	err = fdt_setprop_inplace_var(fdt, node, "linux,uefi-mmap-start", fdt_val64);
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(map->map_size);

	err = fdt_setprop_inplace_var(fdt, node, "linux,uefi-mmap-size", fdt_val32);
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(map->desc_size);

	err = fdt_setprop_inplace_var(fdt, node, "linux,uefi-mmap-desc-size", fdt_val32);
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(map->desc_ver);

	err = fdt_setprop_inplace_var(fdt, node, "linux,uefi-mmap-desc-ver", fdt_val32);
	if (err)
		return EFI_LOAD_ERROR;

	return EFI_SUCCESS;
}

struct exit_boot_struct {
	struct efi_boot_memmap	*boot_memmap;
	efi_memory_desc_t	*runtime_map;
	int			runtime_entry_count;
	void			*new_fdt_addr;
};

static efi_status_t exit_boot_func(struct efi_boot_memmap *map, void *priv)
{
	struct exit_boot_struct *p = priv;

	p->boot_memmap = map;

	/*
	 * Update the memory map with virtual addresses. The function will also
	 * populate @runtime_map with copies of just the EFI_MEMORY_RUNTIME
	 * entries so that we can pass it straight to SetVirtualAddressMap()
	 */
	efi_get_virtmap(map->map, map->map_size, map->desc_size,
			p->runtime_map, &p->runtime_entry_count);

	return update_fdt_memmap(p->new_fdt_addr, map);
}

#ifndef MAX_FDT_SIZE
# define MAX_FDT_SIZE SZ_2M
#endif

/*
 * Allocate memory for a new FDT, then add EFI and commandline related fields
 * to the FDT.  This routine increases the FDT allocation size until the
 * allocated memory is large enough.  EFI allocations are in EFI_PAGE_SIZE
 * granules, which are fixed at 4K bytes, so in most cases the first allocation
 * should succeed.  EFI boot services are exited at the end of this function.
 * There must be no allocations between the get_memory_map() call and the
 * exit_boot_services() call, so the exiting of boot services is very tightly
 * tied to the creation of the FDT with the final memory map in it.
 */
static
efi_status_t allocate_new_fdt_and_exit_boot(void *handle,
					    efi_loaded_image_t *image,
					    unsigned long *new_fdt_addr,
					    char *cmdline_ptr)
{
	unsigned long desc_size;
	u32 desc_ver;
	efi_status_t status;
	struct exit_boot_struct priv;
	unsigned long fdt_addr = 0;
	unsigned long fdt_size = 0;

	if (!efi_novamap) {
		status = efi_alloc_virtmap(&priv.runtime_map, &desc_size,
					   &desc_ver);
		if (status != EFI_SUCCESS) {
			efi_err("Unable to retrieve UEFI memory map.\n");
			return status;
		}
	}

	/*
	 * Unauthenticated device tree data is a security hazard, so ignore
	 * 'dtb=' unless UEFI Secure Boot is disabled.  We assume that secure
	 * boot is enabled if we can't determine its state.
	 */
	if (!IS_ENABLED(CONFIG_EFI_ARMSTUB_DTB_LOADER) ||
	    efi_get_secureboot() != efi_secureboot_mode_disabled) {
		if (strstr(cmdline_ptr, "dtb="))
			efi_err("Ignoring DTB from command line.\n");
	} else {
		status = efi_load_dtb(image, &fdt_addr, &fdt_size);

		if (status != EFI_SUCCESS && status != EFI_NOT_READY) {
			efi_err("Failed to load device tree!\n");
			goto fail;
		}
	}

	if (fdt_addr) {
		efi_info("Using DTB from command line\n");
	} else {
		/* Look for a device tree configuration table entry. */
		fdt_addr = (uintptr_t)get_fdt(&fdt_size);
		if (fdt_addr)
			efi_info("Using DTB from configuration table\n");
	}

	if (!fdt_addr)
		efi_info("Generating empty DTB\n");

	efi_info("Exiting boot services...\n");

	status = efi_allocate_pages(MAX_FDT_SIZE, new_fdt_addr, ULONG_MAX);
	if (status != EFI_SUCCESS) {
		efi_err("Unable to allocate memory for new device tree.\n");
		goto fail;
	}

	status = update_fdt((void *)fdt_addr, fdt_size,
			    (void *)*new_fdt_addr, MAX_FDT_SIZE, cmdline_ptr);

	if (status != EFI_SUCCESS) {
		efi_err("Unable to construct new device tree.\n");
		goto fail_free_new_fdt;
	}

	priv.new_fdt_addr = (void *)*new_fdt_addr;

	status = efi_exit_boot_services(handle, &priv, exit_boot_func);

	if (status == EFI_SUCCESS) {
		efi_set_virtual_address_map_t *svam;

		if (efi_novamap)
			return EFI_SUCCESS;

		/* Install the new virtual address map */
		svam = efi_system_table->runtime->set_virtual_address_map;
		status = svam(priv.runtime_entry_count * desc_size, desc_size,
			      desc_ver, priv.runtime_map);

		/*
		 * We are beyond the point of no return here, so if the call to
		 * SetVirtualAddressMap() failed, we need to signal that to the
		 * incoming kernel but proceed normally otherwise.
		 */
		if (status != EFI_SUCCESS) {
			efi_memory_desc_t *p;
			int l;

			/*
			 * Set the virtual address field of all
			 * EFI_MEMORY_RUNTIME entries to U64_MAX. This will
			 * signal the incoming kernel that no virtual
			 * translation has been installed.
			 */
			for (l = 0; l < priv.boot_memmap->map_size;
			     l += priv.boot_memmap->desc_size) {
				p = (void *)priv.boot_memmap->map + l;

				if (p->attribute & EFI_MEMORY_RUNTIME)
					p->virt_addr = U64_MAX;
			}
		}
		return EFI_SUCCESS;
	}

	efi_err("Exit boot services failed.\n");

fail_free_new_fdt:
	efi_free(MAX_FDT_SIZE, *new_fdt_addr);

fail:
	efi_free(fdt_size, fdt_addr);

	efi_bs_call(free_pool, priv.runtime_map);

	return EFI_LOAD_ERROR;
}

efi_status_t efi_boot_kernel(void *handle, efi_loaded_image_t *image,
			     unsigned long kernel_addr, char *cmdline_ptr)
{
	unsigned long fdt_addr;
	efi_status_t status;

	status = allocate_new_fdt_and_exit_boot(handle, image, &fdt_addr,
						cmdline_ptr);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to update FDT and exit boot services\n");
		return status;
	}

	if (IS_ENABLED(CONFIG_ARM))
		efi_handle_post_ebs_state();

	efi_enter_kernel(kernel_addr, fdt_addr, fdt_totalsize((void *)fdt_addr));
	/* not reached */
}

void *get_fdt(unsigned long *fdt_size)
{
	void *fdt;

	fdt = get_efi_config_table(DEVICE_TREE_GUID);

	if (!fdt)
		return NULL;

	if (fdt_check_header(fdt) != 0) {
		efi_err("Invalid header detected on UEFI supplied FDT, ignoring ...\n");
		return NULL;
	}
	*fdt_size = fdt_totalsize(fdt);
	return fdt;
}
