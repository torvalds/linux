/*
 * FDT related Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2013 Linaro Limited; author Roy Franz
 *
 * This file is part of the Linux kernel, and is made available
 * under the terms of the GNU General Public License version 2.
 *
 */

#include <linux/efi.h>
#include <linux/libfdt.h>
#include <asm/efi.h>

#include "efistub.h"

#define EFI_DT_ADDR_CELLS_DEFAULT 2
#define EFI_DT_SIZE_CELLS_DEFAULT 2

static void fdt_update_cell_size(efi_system_table_t *sys_table, void *fdt)
{
	int offset;

	offset = fdt_path_offset(fdt, "/");
	/* Set the #address-cells and #size-cells values for an empty tree */

	fdt_setprop_u32(fdt, offset, "#address-cells",
			EFI_DT_ADDR_CELLS_DEFAULT);

	fdt_setprop_u32(fdt, offset, "#size-cells", EFI_DT_SIZE_CELLS_DEFAULT);
}

static efi_status_t update_fdt(efi_system_table_t *sys_table, void *orig_fdt,
			       unsigned long orig_fdt_size,
			       void *fdt, int new_fdt_size, char *cmdline_ptr,
			       u64 initrd_addr, u64 initrd_size)
{
	int node, num_rsv;
	int status;
	u32 fdt_val32;
	u64 fdt_val64;

	/* Do some checks on provided FDT, if it exists*/
	if (orig_fdt) {
		if (fdt_check_header(orig_fdt)) {
			pr_efi_err(sys_table, "Device Tree header not valid!\n");
			return EFI_LOAD_ERROR;
		}
		/*
		 * We don't get the size of the FDT if we get if from a
		 * configuration table.
		 */
		if (orig_fdt_size && fdt_totalsize(orig_fdt) > orig_fdt_size) {
			pr_efi_err(sys_table, "Truncated device tree! foo!\n");
			return EFI_LOAD_ERROR;
		}
	}

	if (orig_fdt) {
		status = fdt_open_into(orig_fdt, fdt, new_fdt_size);
	} else {
		status = fdt_create_empty_tree(fdt, new_fdt_size);
		if (status == 0) {
			/*
			 * Any failure from the following function is non
			 * critical
			 */
			fdt_update_cell_size(sys_table, fdt);
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
			status = node; /* node is error code when negative */
			goto fdt_set_fail;
		}
	}

	if ((cmdline_ptr != NULL) && (strlen(cmdline_ptr) > 0)) {
		status = fdt_setprop(fdt, node, "bootargs", cmdline_ptr,
				     strlen(cmdline_ptr) + 1);
		if (status)
			goto fdt_set_fail;
	}

	/* Set initrd address/end in device tree, if present */
	if (initrd_size != 0) {
		u64 initrd_image_end;
		u64 initrd_image_start = cpu_to_fdt64(initrd_addr);

		status = fdt_setprop(fdt, node, "linux,initrd-start",
				     &initrd_image_start, sizeof(u64));
		if (status)
			goto fdt_set_fail;
		initrd_image_end = cpu_to_fdt64(initrd_addr + initrd_size);
		status = fdt_setprop(fdt, node, "linux,initrd-end",
				     &initrd_image_end, sizeof(u64));
		if (status)
			goto fdt_set_fail;
	}

	/* Add FDT entries for EFI runtime services in chosen node. */
	node = fdt_subnode_offset(fdt, 0, "chosen");
	fdt_val64 = cpu_to_fdt64((u64)(unsigned long)sys_table);
	status = fdt_setprop(fdt, node, "linux,uefi-system-table",
			     &fdt_val64, sizeof(fdt_val64));
	if (status)
		goto fdt_set_fail;

	fdt_val64 = U64_MAX; /* placeholder */
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-start",
			     &fdt_val64,  sizeof(fdt_val64));
	if (status)
		goto fdt_set_fail;

	fdt_val32 = U32_MAX; /* placeholder */
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-size",
			     &fdt_val32,  sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	status = fdt_setprop(fdt, node, "linux,uefi-mmap-desc-size",
			     &fdt_val32, sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	status = fdt_setprop(fdt, node, "linux,uefi-mmap-desc-ver",
			     &fdt_val32, sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		efi_status_t efi_status;

		efi_status = efi_get_random_bytes(sys_table, sizeof(fdt_val64),
						  (u8 *)&fdt_val64);
		if (efi_status == EFI_SUCCESS) {
			status = fdt_setprop(fdt, node, "kaslr-seed",
					     &fdt_val64, sizeof(fdt_val64));
			if (status)
				goto fdt_set_fail;
		} else if (efi_status != EFI_NOT_FOUND) {
			return efi_status;
		}
	}

	/* shrink the FDT back to its minimum size */
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

	fdt_val64 = cpu_to_fdt64((unsigned long)*map->map);
	err = fdt_setprop_inplace(fdt, node, "linux,uefi-mmap-start",
				  &fdt_val64, sizeof(fdt_val64));
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(*map->map_size);
	err = fdt_setprop_inplace(fdt, node, "linux,uefi-mmap-size",
				  &fdt_val32, sizeof(fdt_val32));
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(*map->desc_size);
	err = fdt_setprop_inplace(fdt, node, "linux,uefi-mmap-desc-size",
				  &fdt_val32, sizeof(fdt_val32));
	if (err)
		return EFI_LOAD_ERROR;

	fdt_val32 = cpu_to_fdt32(*map->desc_ver);
	err = fdt_setprop_inplace(fdt, node, "linux,uefi-mmap-desc-ver",
				  &fdt_val32, sizeof(fdt_val32));
	if (err)
		return EFI_LOAD_ERROR;

	return EFI_SUCCESS;
}

#ifndef EFI_FDT_ALIGN
#define EFI_FDT_ALIGN EFI_PAGE_SIZE
#endif

struct exit_boot_struct {
	efi_memory_desc_t *runtime_map;
	int *runtime_entry_count;
	void *new_fdt_addr;
};

static efi_status_t exit_boot_func(efi_system_table_t *sys_table_arg,
				   struct efi_boot_memmap *map,
				   void *priv)
{
	struct exit_boot_struct *p = priv;
	/*
	 * Update the memory map with virtual addresses. The function will also
	 * populate @runtime_map with copies of just the EFI_MEMORY_RUNTIME
	 * entries so that we can pass it straight to SetVirtualAddressMap()
	 */
	efi_get_virtmap(*map->map, *map->map_size, *map->desc_size,
			p->runtime_map, p->runtime_entry_count);

	return update_fdt_memmap(p->new_fdt_addr, map);
}

#ifndef MAX_FDT_SIZE
#define MAX_FDT_SIZE	SZ_2M
#endif

/*
 * Allocate memory for a new FDT, then add EFI, commandline, and
 * initrd related fields to the FDT.  This routine increases the
 * FDT allocation size until the allocated memory is large
 * enough.  EFI allocations are in EFI_PAGE_SIZE granules,
 * which are fixed at 4K bytes, so in most cases the first
 * allocation should succeed.
 * EFI boot services are exited at the end of this function.
 * There must be no allocations between the get_memory_map()
 * call and the exit_boot_services() call, so the exiting of
 * boot services is very tightly tied to the creation of the FDT
 * with the final memory map in it.
 */

efi_status_t allocate_new_fdt_and_exit_boot(efi_system_table_t *sys_table,
					    void *handle,
					    unsigned long *new_fdt_addr,
					    unsigned long max_addr,
					    u64 initrd_addr, u64 initrd_size,
					    char *cmdline_ptr,
					    unsigned long fdt_addr,
					    unsigned long fdt_size)
{
	unsigned long map_size, desc_size, buff_size;
	u32 desc_ver;
	unsigned long mmap_key;
	efi_memory_desc_t *memory_map, *runtime_map;
	efi_status_t status;
	int runtime_entry_count = 0;
	struct efi_boot_memmap map;
	struct exit_boot_struct priv;

	map.map =	&runtime_map;
	map.map_size =	&map_size;
	map.desc_size =	&desc_size;
	map.desc_ver =	&desc_ver;
	map.key_ptr =	&mmap_key;
	map.buff_size =	&buff_size;

	/*
	 * Get a copy of the current memory map that we will use to prepare
	 * the input for SetVirtualAddressMap(). We don't have to worry about
	 * subsequent allocations adding entries, since they could not affect
	 * the number of EFI_MEMORY_RUNTIME regions.
	 */
	status = efi_get_memory_map(sys_table, &map);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Unable to retrieve UEFI memory map.\n");
		return status;
	}

	pr_efi(sys_table,
	       "Exiting boot services and installing virtual address map...\n");

	map.map = &memory_map;
	status = efi_high_alloc(sys_table, MAX_FDT_SIZE, EFI_FDT_ALIGN,
				new_fdt_addr, max_addr);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table,
			   "Unable to allocate memory for new device tree.\n");
		goto fail;
	}

	/*
	 * Now that we have done our final memory allocation (and free)
	 * we can get the memory map key needed for exit_boot_services().
	 */
	status = efi_get_memory_map(sys_table, &map);
	if (status != EFI_SUCCESS)
		goto fail_free_new_fdt;

	status = update_fdt(sys_table, (void *)fdt_addr, fdt_size,
			    (void *)*new_fdt_addr, MAX_FDT_SIZE, cmdline_ptr,
			    initrd_addr, initrd_size);

	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Unable to construct new device tree.\n");
		goto fail_free_new_fdt;
	}

	priv.runtime_map = runtime_map;
	priv.runtime_entry_count = &runtime_entry_count;
	priv.new_fdt_addr = (void *)*new_fdt_addr;
	status = efi_exit_boot_services(sys_table, handle, &map, &priv,
					exit_boot_func);

	if (status == EFI_SUCCESS) {
		efi_set_virtual_address_map_t *svam;

		if (novamap())
			return EFI_SUCCESS;

		/* Install the new virtual address map */
		svam = sys_table->runtime->set_virtual_address_map;
		status = svam(runtime_entry_count * desc_size, desc_size,
			      desc_ver, runtime_map);

		/*
		 * We are beyond the point of no return here, so if the call to
		 * SetVirtualAddressMap() failed, we need to signal that to the
		 * incoming kernel but proceed normally otherwise.
		 */
		if (status != EFI_SUCCESS) {
			int l;

			/*
			 * Set the virtual address field of all
			 * EFI_MEMORY_RUNTIME entries to 0. This will signal
			 * the incoming kernel that no virtual translation has
			 * been installed.
			 */
			for (l = 0; l < map_size; l += desc_size) {
				efi_memory_desc_t *p = (void *)memory_map + l;

				if (p->attribute & EFI_MEMORY_RUNTIME)
					p->virt_addr = 0;
			}
		}
		return EFI_SUCCESS;
	}

	pr_efi_err(sys_table, "Exit boot services failed.\n");

fail_free_new_fdt:
	efi_free(sys_table, MAX_FDT_SIZE, *new_fdt_addr);

fail:
	sys_table->boottime->free_pool(runtime_map);
	return EFI_LOAD_ERROR;
}

void *get_fdt(efi_system_table_t *sys_table, unsigned long *fdt_size)
{
	efi_guid_t fdt_guid = DEVICE_TREE_GUID;
	efi_config_table_t *tables;
	void *fdt;
	int i;

	tables = (efi_config_table_t *) sys_table->tables;
	fdt = NULL;

	for (i = 0; i < sys_table->nr_tables; i++)
		if (efi_guidcmp(tables[i].guid, fdt_guid) == 0) {
			fdt = (void *) tables[i].table;
			if (fdt_check_header(fdt) != 0) {
				pr_efi_err(sys_table, "Invalid header detected on UEFI supplied FDT, ignoring ...\n");
				return NULL;
			}
			*fdt_size = fdt_totalsize(fdt);
			break;
	 }

	return fdt;
}
