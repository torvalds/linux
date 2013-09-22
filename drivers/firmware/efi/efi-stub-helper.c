/*
 * Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2011 Intel Corporation; author Matt Fleming
 *
 * This file is part of the Linux kernel, and is made available
 * under the terms of the GNU General Public License version 2.
 *
 */
#define EFI_READ_CHUNK_SIZE	(1024 * 1024)

struct initrd {
	efi_file_handle_t *handle;
	u64 size;
};




static void efi_char16_printk(efi_system_table_t *sys_table_arg,
			      efi_char16_t *str)
{
	struct efi_simple_text_output_protocol *out;

	out = (struct efi_simple_text_output_protocol *)sys_table_arg->con_out;
	efi_call_phys2(out->output_string, out, str);
}

static void efi_printk(efi_system_table_t *sys_table_arg, char *str)
{
	char *s8;

	for (s8 = str; *s8; s8++) {
		efi_char16_t ch[2] = { 0 };

		ch[0] = *s8;
		if (*s8 == '\n') {
			efi_char16_t nl[2] = { '\r', 0 };
			efi_char16_printk(sys_table_arg, nl);
		}

		efi_char16_printk(sys_table_arg, ch);
	}
}


static efi_status_t __get_map(efi_system_table_t *sys_table_arg,
			      efi_memory_desc_t **map,
			      unsigned long *map_size,
			      unsigned long *desc_size)
{
	efi_memory_desc_t *m = NULL;
	efi_status_t status;
	unsigned long key;
	u32 desc_version;

	*map_size = sizeof(*m) * 32;
again:
	/*
	 * Add an additional efi_memory_desc_t because we're doing an
	 * allocation which may be in a new descriptor region.
	 */
	*map_size += sizeof(*m);
	status = efi_call_phys3(sys_table_arg->boottime->allocate_pool,
				EFI_LOADER_DATA, *map_size, (void **)&m);
	if (status != EFI_SUCCESS)
		goto fail;

	status = efi_call_phys5(sys_table_arg->boottime->get_memory_map,
				map_size, m, &key, desc_size, &desc_version);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_call_phys1(sys_table_arg->boottime->free_pool, m);
		goto again;
	}

	if (status != EFI_SUCCESS)
		efi_call_phys1(sys_table_arg->boottime->free_pool, m);

fail:
	*map = m;
	return status;
}

/*
 * Allocate at the highest possible address that is not above 'max'.
 */
static efi_status_t efi_high_alloc(efi_system_table_t *sys_table_arg,
			       unsigned long size, unsigned long align,
			       unsigned long *addr, unsigned long max)
{
	unsigned long map_size, desc_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	u64 max_addr = 0;
	int i;

	status = __get_map(sys_table_arg, &map, &map_size, &desc_size);
	if (status != EFI_SUCCESS)
		goto fail;

	/*
	 * Enforce minimum alignment that EFI requires when requesting
	 * a specific address.  We are doing page-based allocations,
	 * so we must be aligned to a page.
	 */
	if (align < EFI_PAGE_SIZE)
		align = EFI_PAGE_SIZE;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
again:
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = (efi_memory_desc_t *)(m + (i * desc_size));
		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * (1UL << EFI_PAGE_SHIFT);

		if ((start + size) > end || (start + size) > max)
			continue;

		if (end - size > max)
			end = max;

		if (round_down(end - size, align) < start)
			continue;

		start = round_down(end - size, align);

		/*
		 * Don't allocate at 0x0. It will confuse code that
		 * checks pointers against NULL.
		 */
		if (start == 0x0)
			continue;

		if (start > max_addr)
			max_addr = start;
	}

	if (!max_addr)
		status = EFI_NOT_FOUND;
	else {
		status = efi_call_phys4(sys_table_arg->boottime->allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &max_addr);
		if (status != EFI_SUCCESS) {
			max = max_addr;
			max_addr = 0;
			goto again;
		}

		*addr = max_addr;
	}

free_pool:
	efi_call_phys1(sys_table_arg->boottime->free_pool, map);

fail:
	return status;
}

/*
 * Allocate at the lowest possible address.
 */
static efi_status_t efi_low_alloc(efi_system_table_t *sys_table_arg,
			      unsigned long size, unsigned long align,
			      unsigned long *addr)
{
	unsigned long map_size, desc_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	int i;

	status = __get_map(sys_table_arg, &map, &map_size, &desc_size);
	if (status != EFI_SUCCESS)
		goto fail;

	/*
	 * Enforce minimum alignment that EFI requires when requesting
	 * a specific address.  We are doing page-based allocations,
	 * so we must be aligned to a page.
	 */
	if (align < EFI_PAGE_SIZE)
		align = EFI_PAGE_SIZE;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = (efi_memory_desc_t *)(m + (i * desc_size));

		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * (1UL << EFI_PAGE_SHIFT);

		/*
		 * Don't allocate at 0x0. It will confuse code that
		 * checks pointers against NULL. Skip the first 8
		 * bytes so we start at a nice even number.
		 */
		if (start == 0x0)
			start += 8;

		start = round_up(start, align);
		if ((start + size) > end)
			continue;

		status = efi_call_phys4(sys_table_arg->boottime->allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &start);
		if (status == EFI_SUCCESS) {
			*addr = start;
			break;
		}
	}

	if (i == map_size / desc_size)
		status = EFI_NOT_FOUND;

free_pool:
	efi_call_phys1(sys_table_arg->boottime->free_pool, map);
fail:
	return status;
}

static void efi_free(efi_system_table_t *sys_table_arg, unsigned long size,
		     unsigned long addr)
{
	unsigned long nr_pages;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	efi_call_phys2(sys_table_arg->boottime->free_pages, addr, nr_pages);
}


/*
 * Check the cmdline for a LILO-style initrd= arguments.
 *
 * We only support loading an initrd from the same filesystem as the
 * kernel image.
 */
static efi_status_t handle_ramdisks(efi_system_table_t *sys_table_arg,
				    efi_loaded_image_t *image,
				    struct setup_header *hdr)
{
	struct initrd *initrds;
	unsigned long initrd_addr;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	u64 initrd_total;
	efi_file_io_interface_t *io;
	efi_file_handle_t *fh;
	efi_status_t status;
	int nr_initrds;
	char *str;
	int i, j, k;

	initrd_addr = 0;
	initrd_total = 0;

	str = (char *)(unsigned long)hdr->cmd_line_ptr;

	j = 0;			/* See close_handles */

	if (!str || !*str)
		return EFI_SUCCESS;

	for (nr_initrds = 0; *str; nr_initrds++) {
		str = strstr(str, "initrd=");
		if (!str)
			break;

		str += 7;

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n')
			str++;
	}

	if (!nr_initrds)
		return EFI_SUCCESS;

	status = efi_call_phys3(sys_table_arg->boottime->allocate_pool,
				EFI_LOADER_DATA,
				nr_initrds * sizeof(*initrds),
				&initrds);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to alloc mem for initrds\n");
		goto fail;
	}

	str = (char *)(unsigned long)hdr->cmd_line_ptr;
	for (i = 0; i < nr_initrds; i++) {
		struct initrd *initrd;
		efi_file_handle_t *h;
		efi_file_info_t *info;
		efi_char16_t filename_16[256];
		unsigned long info_sz;
		efi_guid_t info_guid = EFI_FILE_INFO_ID;
		efi_char16_t *p;
		u64 file_sz;

		str = strstr(str, "initrd=");
		if (!str)
			break;

		str += 7;

		initrd = &initrds[i];
		p = filename_16;

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n') {
			if ((u8 *)p >= (u8 *)filename_16 + sizeof(filename_16))
				break;

			if (*str == '/') {
				*p++ = '\\';
				*str++;
			} else {
				*p++ = *str++;
			}
		}

		*p = '\0';

		/* Only open the volume once. */
		if (!i) {
			efi_boot_services_t *boottime;

			boottime = sys_table_arg->boottime;

			status = efi_call_phys3(boottime->handle_protocol,
					image->device_handle, &fs_proto, &io);
			if (status != EFI_SUCCESS) {
				efi_printk(sys_table_arg, "Failed to handle fs_proto\n");
				goto free_initrds;
			}

			status = efi_call_phys2(io->open_volume, io, &fh);
			if (status != EFI_SUCCESS) {
				efi_printk(sys_table_arg, "Failed to open volume\n");
				goto free_initrds;
			}
		}

		status = efi_call_phys5(fh->open, fh, &h, filename_16,
					EFI_FILE_MODE_READ, (u64)0);
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table_arg, "Failed to open initrd file: ");
			efi_char16_printk(sys_table_arg, filename_16);
			efi_printk(sys_table_arg, "\n");
			goto close_handles;
		}

		initrd->handle = h;

		info_sz = 0;
		status = efi_call_phys4(h->get_info, h, &info_guid,
					&info_sz, NULL);
		if (status != EFI_BUFFER_TOO_SMALL) {
			efi_printk(sys_table_arg, "Failed to get initrd info size\n");
			goto close_handles;
		}

grow:
		status = efi_call_phys3(sys_table_arg->boottime->allocate_pool,
					EFI_LOADER_DATA, info_sz, &info);
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table_arg, "Failed to alloc mem for initrd info\n");
			goto close_handles;
		}

		status = efi_call_phys4(h->get_info, h, &info_guid,
					&info_sz, info);
		if (status == EFI_BUFFER_TOO_SMALL) {
			efi_call_phys1(sys_table_arg->boottime->free_pool,
				       info);
			goto grow;
		}

		file_sz = info->file_size;
		efi_call_phys1(sys_table_arg->boottime->free_pool, info);

		if (status != EFI_SUCCESS) {
			efi_printk(sys_table_arg, "Failed to get initrd info\n");
			goto close_handles;
		}

		initrd->size = file_sz;
		initrd_total += file_sz;
	}

	if (initrd_total) {
		unsigned long addr;

		/*
		 * Multiple initrd's need to be at consecutive
		 * addresses in memory, so allocate enough memory for
		 * all the initrd's.
		 */
		status = efi_high_alloc(sys_table_arg, initrd_total, 0x1000,
				    &initrd_addr, hdr->initrd_addr_max);
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table_arg, "Failed to alloc highmem for initrds\n");
			goto close_handles;
		}

		/* We've run out of free low memory. */
		if (initrd_addr > hdr->initrd_addr_max) {
			efi_printk(sys_table_arg, "We've run out of free low memory\n");
			status = EFI_INVALID_PARAMETER;
			goto free_initrd_total;
		}

		addr = initrd_addr;
		for (j = 0; j < nr_initrds; j++) {
			u64 size;

			size = initrds[j].size;
			while (size) {
				u64 chunksize;
				if (size > EFI_READ_CHUNK_SIZE)
					chunksize = EFI_READ_CHUNK_SIZE;
				else
					chunksize = size;
				status = efi_call_phys3(fh->read,
							initrds[j].handle,
							&chunksize, addr);
				if (status != EFI_SUCCESS) {
					efi_printk(sys_table_arg, "Failed to read initrd\n");
					goto free_initrd_total;
				}
				addr += chunksize;
				size -= chunksize;
			}

			efi_call_phys1(fh->close, initrds[j].handle);
		}

	}

	efi_call_phys1(sys_table_arg->boottime->free_pool, initrds);

	hdr->ramdisk_image = initrd_addr;
	hdr->ramdisk_size = initrd_total;

	return status;

free_initrd_total:
	efi_free(sys_table_arg, initrd_total, initrd_addr);

close_handles:
	for (k = j; k < i; k++)
		efi_call_phys1(fh->close, initrds[k].handle);
free_initrds:
	efi_call_phys1(sys_table_arg->boottime->free_pool, initrds);
fail:
	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	return status;
}
/*
 * Relocate a kernel image, either compressed or uncompressed.
 * In the ARM64 case, all kernel images are currently
 * uncompressed, and as such when we relocate it we need to
 * allocate additional space for the BSS segment. Any low
 * memory that this function should avoid needs to be
 * unavailable in the EFI memory map, as if the preferred
 * address is not available the lowest available address will
 * be used.
 */
static efi_status_t efi_relocate_kernel(efi_system_table_t *sys_table_arg,
					unsigned long *image_addr,
					unsigned long image_size,
					unsigned long alloc_size,
					unsigned long preferred_addr,
					unsigned long alignment)
{
	unsigned long cur_image_addr;
	unsigned long new_addr = 0;
	efi_status_t status;
	unsigned long nr_pages;
	efi_physical_addr_t efi_addr = preferred_addr;

	if (!image_addr || !image_size || !alloc_size)
		return EFI_INVALID_PARAMETER;
	if (alloc_size < image_size)
		return EFI_INVALID_PARAMETER;

	cur_image_addr = *image_addr;

	/*
	 * The EFI firmware loader could have placed the kernel image
	 * anywhere in memory, but the kernel has restrictions on the
	 * max physical address it can run at.  Some architectures
	 * also have a prefered address, so first try to relocate
	 * to the preferred address.  If that fails, allocate as low
	 * as possible while respecting the required alignment.
	 */
	nr_pages = round_up(alloc_size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	status = efi_call_phys4(sys_table_arg->boottime->allocate_pages,
				EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
				nr_pages, &efi_addr);
	new_addr = efi_addr;
	/*
	 * If preferred address allocation failed allocate as low as
	 * possible.
	 */
	if (status != EFI_SUCCESS) {
		status = efi_low_alloc(sys_table_arg, alloc_size, alignment,
				       &new_addr);
	}
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "ERROR: Failed to allocate usable memory for kernel.\n");
		return status;
	}

	/*
	 * We know source/dest won't overlap since both memory ranges
	 * have been allocated by UEFI, so we can safely use memcpy.
	 */
	memcpy((void *)new_addr, (void *)cur_image_addr, image_size);
	/* Zero any extra space we may have allocated for BSS. */
	memset((void *)(new_addr + image_size), alloc_size - image_size, 0);

	/* Return the new address of the relocated image. */
	*image_addr = new_addr;

	return status;
}
