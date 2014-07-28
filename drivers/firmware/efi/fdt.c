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

static efi_status_t update_fdt(efi_system_table_t *sys_table, void *orig_fdt,
			       unsigned long orig_fdt_size,
			       void *fdt, int new_fdt_size, char *cmdline_ptr,
			       u64 initrd_addr, u64 initrd_size,
			       efi_memory_desc_t *memory_map,
			       unsigned long map_size, unsigned long desc_size,
			       u32 desc_ver)
{
	int node, prev;
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

	if (orig_fdt)
		status = fdt_open_into(orig_fdt, fdt, new_fdt_size);
	else
		status = fdt_create_empty_tree(fdt, new_fdt_size);

	if (status != 0)
		goto fdt_set_fail;

	/*
	 * Delete any memory nodes present. We must delete nodes which
	 * early_init_dt_scan_memory may try to use.
	 */
	prev = 0;
	for (;;) {
		const char *type;
		int len;

		node = fdt_next_node(fdt, prev, NULL);
		if (node < 0)
			break;

		type = fdt_getprop(fdt, node, "device_type", &len);
		if (type && strncmp(type, "memory", len) == 0) {
			fdt_del_node(fdt, node);
			continue;
		}

		prev = node;
	}

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

	fdt_val64 = cpu_to_fdt64((u64)(unsigned long)memory_map);
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-start",
			     &fdt_val64,  sizeof(fdt_val64));
	if (status)
		goto fdt_set_fail;

	fdt_val32 = cpu_to_fdt32(map_size);
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-size",
			     &fdt_val32,  sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	fdt_val32 = cpu_to_fdt32(desc_size);
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-desc-size",
			     &fdt_val32, sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	fdt_val32 = cpu_to_fdt32(desc_ver);
	status = fdt_setprop(fdt, node, "linux,uefi-mmap-desc-ver",
			     &fdt_val32, sizeof(fdt_val32));
	if (status)
		goto fdt_set_fail;

	/*
	 * Add kernel version banner so stub/kernel match can be
	 * verified.
	 */
	status = fdt_setprop_string(fdt, node, "linux,uefi-stub-kern-ver",
			     linux_banner);
	if (status)
		goto fdt_set_fail;

	return EFI_SUCCESS;

fdt_set_fail:
	if (status == -FDT_ERR_NOSPACE)
		return EFI_BUFFER_TOO_SMALL;

	return EFI_LOAD_ERROR;
}

#ifndef EFI_FDT_ALIGN
#define EFI_FDT_ALIGN EFI_PAGE_SIZE
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
	unsigned long map_size, desc_size;
	u32 desc_ver;
	unsigned long mmap_key;
	efi_memory_desc_t *memory_map;
	unsigned long new_fdt_size;
	efi_status_t status;

	/*
	 * Estimate size of new FDT, and allocate memory for it. We
	 * will allocate a bigger buffer if this ends up being too
	 * small, so a rough guess is OK here.
	 */
	new_fdt_size = fdt_size + EFI_PAGE_SIZE;
	while (1) {
		status = efi_high_alloc(sys_table, new_fdt_size, EFI_FDT_ALIGN,
					new_fdt_addr, max_addr);
		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table, "Unable to allocate memory for new device tree.\n");
			goto fail;
		}

		/*
		 * Now that we have done our final memory allocation (and free)
		 * we can get the memory map key  needed for
		 * exit_boot_services().
		 */
		status = efi_get_memory_map(sys_table, &memory_map, &map_size,
					    &desc_size, &desc_ver, &mmap_key);
		if (status != EFI_SUCCESS)
			goto fail_free_new_fdt;

		status = update_fdt(sys_table,
				    (void *)fdt_addr, fdt_size,
				    (void *)*new_fdt_addr, new_fdt_size,
				    cmdline_ptr, initrd_addr, initrd_size,
				    memory_map, map_size, desc_size, desc_ver);

		/* Succeeding the first time is the expected case. */
		if (status == EFI_SUCCESS)
			break;

		if (status == EFI_BUFFER_TOO_SMALL) {
			/*
			 * We need to allocate more space for the new
			 * device tree, so free existing buffer that is
			 * too small.  Also free memory map, as we will need
			 * to get new one that reflects the free/alloc we do
			 * on the device tree buffer.
			 */
			efi_free(sys_table, new_fdt_size, *new_fdt_addr);
			sys_table->boottime->free_pool(memory_map);
			new_fdt_size += EFI_PAGE_SIZE;
		} else {
			pr_efi_err(sys_table, "Unable to constuct new device tree.\n");
			goto fail_free_mmap;
		}
	}

	/* Now we are ready to exit_boot_services.*/
	status = sys_table->boottime->exit_boot_services(handle, mmap_key);


	if (status == EFI_SUCCESS)
		return status;

	pr_efi_err(sys_table, "Exit boot services failed.\n");

fail_free_mmap:
	sys_table->boottime->free_pool(memory_map);

fail_free_new_fdt:
	efi_free(sys_table, new_fdt_size, *new_fdt_addr);

fail:
	return EFI_LOAD_ERROR;
}

static void *get_fdt(efi_system_table_t *sys_table)
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
			break;
	 }

	return fdt;
}
