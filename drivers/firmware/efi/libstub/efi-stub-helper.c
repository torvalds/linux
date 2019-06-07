// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2011 Intel Corporation; author Matt Fleming
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

/*
 * Some firmware implementations have problems reading files in one go.
 * A read chunk size of 1MB seems to work for most platforms.
 *
 * Unfortunately, reading files in chunks triggers *other* bugs on some
 * platforms, so we provide a way to disable this workaround, which can
 * be done by passing "efi=nochunk" on the EFI boot stub command line.
 *
 * If you experience issues with initrd images being corrupt it's worth
 * trying efi=nochunk, but chunking is enabled by default because there
 * are far more machines that require the workaround than those that
 * break with it enabled.
 */
#define EFI_READ_CHUNK_SIZE	(1024 * 1024)

static unsigned long __chunk_size = EFI_READ_CHUNK_SIZE;

static int __section(.data) __nokaslr;
static int __section(.data) __quiet;
static int __section(.data) __novamap;

int __pure nokaslr(void)
{
	return __nokaslr;
}
int __pure is_quiet(void)
{
	return __quiet;
}
int __pure novamap(void)
{
	return __novamap;
}

#define EFI_MMAP_NR_SLACK_SLOTS	8

struct file_info {
	efi_file_handle_t *handle;
	u64 size;
};

void efi_printk(efi_system_table_t *sys_table_arg, char *str)
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

static inline bool mmap_has_headroom(unsigned long buff_size,
				     unsigned long map_size,
				     unsigned long desc_size)
{
	unsigned long slack = buff_size - map_size;

	return slack / desc_size >= EFI_MMAP_NR_SLACK_SLOTS;
}

efi_status_t efi_get_memory_map(efi_system_table_t *sys_table_arg,
				struct efi_boot_memmap *map)
{
	efi_memory_desc_t *m = NULL;
	efi_status_t status;
	unsigned long key;
	u32 desc_version;

	*map->desc_size =	sizeof(*m);
	*map->map_size =	*map->desc_size * 32;
	*map->buff_size =	*map->map_size;
again:
	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				*map->map_size, (void **)&m);
	if (status != EFI_SUCCESS)
		goto fail;

	*map->desc_size = 0;
	key = 0;
	status = efi_call_early(get_memory_map, map->map_size, m,
				&key, map->desc_size, &desc_version);
	if (status == EFI_BUFFER_TOO_SMALL ||
	    !mmap_has_headroom(*map->buff_size, *map->map_size,
			       *map->desc_size)) {
		efi_call_early(free_pool, m);
		/*
		 * Make sure there is some entries of headroom so that the
		 * buffer can be reused for a new map after allocations are
		 * no longer permitted.  Its unlikely that the map will grow to
		 * exceed this headroom once we are ready to trigger
		 * ExitBootServices()
		 */
		*map->map_size += *map->desc_size * EFI_MMAP_NR_SLACK_SLOTS;
		*map->buff_size = *map->map_size;
		goto again;
	}

	if (status != EFI_SUCCESS)
		efi_call_early(free_pool, m);

	if (map->key_ptr && status == EFI_SUCCESS)
		*map->key_ptr = key;
	if (map->desc_ver && status == EFI_SUCCESS)
		*map->desc_ver = desc_version;

fail:
	*map->map = m;
	return status;
}


unsigned long get_dram_base(efi_system_table_t *sys_table_arg)
{
	efi_status_t status;
	unsigned long map_size, buff_size;
	unsigned long membase  = EFI_ERROR;
	struct efi_memory_map map;
	efi_memory_desc_t *md;
	struct efi_boot_memmap boot_map;

	boot_map.map =		(efi_memory_desc_t **)&map.map;
	boot_map.map_size =	&map_size;
	boot_map.desc_size =	&map.desc_size;
	boot_map.desc_ver =	NULL;
	boot_map.key_ptr =	NULL;
	boot_map.buff_size =	&buff_size;

	status = efi_get_memory_map(sys_table_arg, &boot_map);
	if (status != EFI_SUCCESS)
		return membase;

	map.map_end = map.map + map_size;

	for_each_efi_memory_desc_in_map(&map, md) {
		if (md->attribute & EFI_MEMORY_WB) {
			if (membase > md->phys_addr)
				membase = md->phys_addr;
		}
	}

	efi_call_early(free_pool, map.map);

	return membase;
}

/*
 * Allocate at the highest possible address that is not above 'max'.
 */
efi_status_t efi_high_alloc(efi_system_table_t *sys_table_arg,
			    unsigned long size, unsigned long align,
			    unsigned long *addr, unsigned long max)
{
	unsigned long map_size, desc_size, buff_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	u64 max_addr = 0;
	int i;
	struct efi_boot_memmap boot_map;

	boot_map.map =		&map;
	boot_map.map_size =	&map_size;
	boot_map.desc_size =	&desc_size;
	boot_map.desc_ver =	NULL;
	boot_map.key_ptr =	NULL;
	boot_map.buff_size =	&buff_size;

	status = efi_get_memory_map(sys_table_arg, &boot_map);
	if (status != EFI_SUCCESS)
		goto fail;

	/*
	 * Enforce minimum alignment that EFI or Linux requires when
	 * requesting a specific address.  We are doing page-based (or
	 * larger) allocations, and both the address and size must meet
	 * alignment constraints.
	 */
	if (align < EFI_ALLOC_ALIGN)
		align = EFI_ALLOC_ALIGN;

	size = round_up(size, EFI_ALLOC_ALIGN);
	nr_pages = size / EFI_PAGE_SIZE;
again:
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = efi_early_memdesc_ptr(m, desc_size, i);
		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * EFI_PAGE_SIZE;

		if (end > max)
			end = max;

		if ((start + size) > end)
			continue;

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
		status = efi_call_early(allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &max_addr);
		if (status != EFI_SUCCESS) {
			max = max_addr;
			max_addr = 0;
			goto again;
		}

		*addr = max_addr;
	}

	efi_call_early(free_pool, map);
fail:
	return status;
}

/*
 * Allocate at the lowest possible address.
 */
efi_status_t efi_low_alloc(efi_system_table_t *sys_table_arg,
			   unsigned long size, unsigned long align,
			   unsigned long *addr)
{
	unsigned long map_size, desc_size, buff_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	int i;
	struct efi_boot_memmap boot_map;

	boot_map.map =		&map;
	boot_map.map_size =	&map_size;
	boot_map.desc_size =	&desc_size;
	boot_map.desc_ver =	NULL;
	boot_map.key_ptr =	NULL;
	boot_map.buff_size =	&buff_size;

	status = efi_get_memory_map(sys_table_arg, &boot_map);
	if (status != EFI_SUCCESS)
		goto fail;

	/*
	 * Enforce minimum alignment that EFI or Linux requires when
	 * requesting a specific address.  We are doing page-based (or
	 * larger) allocations, and both the address and size must meet
	 * alignment constraints.
	 */
	if (align < EFI_ALLOC_ALIGN)
		align = EFI_ALLOC_ALIGN;

	size = round_up(size, EFI_ALLOC_ALIGN);
	nr_pages = size / EFI_PAGE_SIZE;
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = efi_early_memdesc_ptr(m, desc_size, i);

		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * EFI_PAGE_SIZE;

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

		status = efi_call_early(allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &start);
		if (status == EFI_SUCCESS) {
			*addr = start;
			break;
		}
	}

	if (i == map_size / desc_size)
		status = EFI_NOT_FOUND;

	efi_call_early(free_pool, map);
fail:
	return status;
}

void efi_free(efi_system_table_t *sys_table_arg, unsigned long size,
	      unsigned long addr)
{
	unsigned long nr_pages;

	if (!size)
		return;

	nr_pages = round_up(size, EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;
	efi_call_early(free_pages, addr, nr_pages);
}

static efi_status_t efi_file_size(efi_system_table_t *sys_table_arg, void *__fh,
				  efi_char16_t *filename_16, void **handle,
				  u64 *file_sz)
{
	efi_file_handle_t *h, *fh = __fh;
	efi_file_info_t *info;
	efi_status_t status;
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	unsigned long info_sz;

	status = efi_call_proto(efi_file_handle, open, fh, &h, filename_16,
				EFI_FILE_MODE_READ, (u64)0);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to open file: ");
		efi_char16_printk(sys_table_arg, filename_16);
		efi_printk(sys_table_arg, "\n");
		return status;
	}

	*handle = h;

	info_sz = 0;
	status = efi_call_proto(efi_file_handle, get_info, h, &info_guid,
				&info_sz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		efi_printk(sys_table_arg, "Failed to get file info size\n");
		return status;
	}

grow:
	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				info_sz, (void **)&info);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to alloc mem for file info\n");
		return status;
	}

	status = efi_call_proto(efi_file_handle, get_info, h, &info_guid,
				&info_sz, info);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_call_early(free_pool, info);
		goto grow;
	}

	*file_sz = info->file_size;
	efi_call_early(free_pool, info);

	if (status != EFI_SUCCESS)
		efi_printk(sys_table_arg, "Failed to get initrd info\n");

	return status;
}

static efi_status_t efi_file_read(void *handle, unsigned long *size, void *addr)
{
	return efi_call_proto(efi_file_handle, read, handle, size, addr);
}

static efi_status_t efi_file_close(void *handle)
{
	return efi_call_proto(efi_file_handle, close, handle);
}

static efi_status_t efi_open_volume(efi_system_table_t *sys_table_arg,
				    efi_loaded_image_t *image,
				    efi_file_handle_t **__fh)
{
	efi_file_io_interface_t *io;
	efi_file_handle_t *fh;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_status_t status;
	void *handle = (void *)(unsigned long)efi_table_attr(efi_loaded_image,
							     device_handle,
							     image);

	status = efi_call_early(handle_protocol, handle,
				&fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg, "Failed to handle fs_proto\n");
		return status;
	}

	status = efi_call_proto(efi_file_io_interface, open_volume, io, &fh);
	if (status != EFI_SUCCESS)
		efi_printk(sys_table_arg, "Failed to open volume\n");
	else
		*__fh = fh;

	return status;
}

/*
 * Parse the ASCII string 'cmdline' for EFI options, denoted by the efi=
 * option, e.g. efi=nochunk.
 *
 * It should be noted that efi= is parsed in two very different
 * environments, first in the early boot environment of the EFI boot
 * stub, and subsequently during the kernel boot.
 */
efi_status_t efi_parse_options(char const *cmdline)
{
	char *str;

	str = strstr(cmdline, "nokaslr");
	if (str == cmdline || (str && str > cmdline && *(str - 1) == ' '))
		__nokaslr = 1;

	str = strstr(cmdline, "quiet");
	if (str == cmdline || (str && str > cmdline && *(str - 1) == ' '))
		__quiet = 1;

	/*
	 * If no EFI parameters were specified on the cmdline we've got
	 * nothing to do.
	 */
	str = strstr(cmdline, "efi=");
	if (!str)
		return EFI_SUCCESS;

	/* Skip ahead to first argument */
	str += strlen("efi=");

	/*
	 * Remember, because efi= is also used by the kernel we need to
	 * skip over arguments we don't understand.
	 */
	while (*str && *str != ' ') {
		if (!strncmp(str, "nochunk", 7)) {
			str += strlen("nochunk");
			__chunk_size = -1UL;
		}

		if (!strncmp(str, "novamap", 7)) {
			str += strlen("novamap");
			__novamap = 1;
		}

		/* Group words together, delimited by "," */
		while (*str && *str != ' ' && *str != ',')
			str++;

		if (*str == ',')
			str++;
	}

	return EFI_SUCCESS;
}

/*
 * Check the cmdline for a LILO-style file= arguments.
 *
 * We only support loading a file from the same filesystem as
 * the kernel image.
 */
efi_status_t handle_cmdline_files(efi_system_table_t *sys_table_arg,
				  efi_loaded_image_t *image,
				  char *cmd_line, char *option_string,
				  unsigned long max_addr,
				  unsigned long *load_addr,
				  unsigned long *load_size)
{
	struct file_info *files;
	unsigned long file_addr;
	u64 file_size_total;
	efi_file_handle_t *fh = NULL;
	efi_status_t status;
	int nr_files;
	char *str;
	int i, j, k;

	file_addr = 0;
	file_size_total = 0;

	str = cmd_line;

	j = 0;			/* See close_handles */

	if (!load_addr || !load_size)
		return EFI_INVALID_PARAMETER;

	*load_addr = 0;
	*load_size = 0;

	if (!str || !*str)
		return EFI_SUCCESS;

	for (nr_files = 0; *str; nr_files++) {
		str = strstr(str, option_string);
		if (!str)
			break;

		str += strlen(option_string);

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n')
			str++;
	}

	if (!nr_files)
		return EFI_SUCCESS;

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				nr_files * sizeof(*files), (void **)&files);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table_arg, "Failed to alloc mem for file handle list\n");
		goto fail;
	}

	str = cmd_line;
	for (i = 0; i < nr_files; i++) {
		struct file_info *file;
		efi_char16_t filename_16[256];
		efi_char16_t *p;

		str = strstr(str, option_string);
		if (!str)
			break;

		str += strlen(option_string);

		file = &files[i];
		p = filename_16;

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n') {
			if ((u8 *)p >= (u8 *)filename_16 + sizeof(filename_16))
				break;

			if (*str == '/') {
				*p++ = '\\';
				str++;
			} else {
				*p++ = *str++;
			}
		}

		*p = '\0';

		/* Only open the volume once. */
		if (!i) {
			status = efi_open_volume(sys_table_arg, image, &fh);
			if (status != EFI_SUCCESS)
				goto free_files;
		}

		status = efi_file_size(sys_table_arg, fh, filename_16,
				       (void **)&file->handle, &file->size);
		if (status != EFI_SUCCESS)
			goto close_handles;

		file_size_total += file->size;
	}

	if (file_size_total) {
		unsigned long addr;

		/*
		 * Multiple files need to be at consecutive addresses in memory,
		 * so allocate enough memory for all the files.  This is used
		 * for loading multiple files.
		 */
		status = efi_high_alloc(sys_table_arg, file_size_total, 0x1000,
				    &file_addr, max_addr);
		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table_arg, "Failed to alloc highmem for files\n");
			goto close_handles;
		}

		/* We've run out of free low memory. */
		if (file_addr > max_addr) {
			pr_efi_err(sys_table_arg, "We've run out of free low memory\n");
			status = EFI_INVALID_PARAMETER;
			goto free_file_total;
		}

		addr = file_addr;
		for (j = 0; j < nr_files; j++) {
			unsigned long size;

			size = files[j].size;
			while (size) {
				unsigned long chunksize;

				if (IS_ENABLED(CONFIG_X86) && size > __chunk_size)
					chunksize = __chunk_size;
				else
					chunksize = size;

				status = efi_file_read(files[j].handle,
						       &chunksize,
						       (void *)addr);
				if (status != EFI_SUCCESS) {
					pr_efi_err(sys_table_arg, "Failed to read file\n");
					goto free_file_total;
				}
				addr += chunksize;
				size -= chunksize;
			}

			efi_file_close(files[j].handle);
		}

	}

	efi_call_early(free_pool, files);

	*load_addr = file_addr;
	*load_size = file_size_total;

	return status;

free_file_total:
	efi_free(sys_table_arg, file_size_total, file_addr);

close_handles:
	for (k = j; k < i; k++)
		efi_file_close(files[k].handle);
free_files:
	efi_call_early(free_pool, files);
fail:
	*load_addr = 0;
	*load_size = 0;

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
efi_status_t efi_relocate_kernel(efi_system_table_t *sys_table_arg,
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
	nr_pages = round_up(alloc_size, EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;
	status = efi_call_early(allocate_pages,
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
		pr_efi_err(sys_table_arg, "Failed to allocate usable memory for kernel.\n");
		return status;
	}

	/*
	 * We know source/dest won't overlap since both memory ranges
	 * have been allocated by UEFI, so we can safely use memcpy.
	 */
	memcpy((void *)new_addr, (void *)cur_image_addr, image_size);

	/* Return the new address of the relocated image. */
	*image_addr = new_addr;

	return status;
}

/*
 * Get the number of UTF-8 bytes corresponding to an UTF-16 character.
 * This overestimates for surrogates, but that is okay.
 */
static int efi_utf8_bytes(u16 c)
{
	return 1 + (c >= 0x80) + (c >= 0x800);
}

/*
 * Convert an UTF-16 string, not necessarily null terminated, to UTF-8.
 */
static u8 *efi_utf16_to_utf8(u8 *dst, const u16 *src, int n)
{
	unsigned int c;

	while (n--) {
		c = *src++;
		if (n && c >= 0xd800 && c <= 0xdbff &&
		    *src >= 0xdc00 && *src <= 0xdfff) {
			c = 0x10000 + ((c & 0x3ff) << 10) + (*src & 0x3ff);
			src++;
			n--;
		}
		if (c >= 0xd800 && c <= 0xdfff)
			c = 0xfffd; /* Unmatched surrogate */
		if (c < 0x80) {
			*dst++ = c;
			continue;
		}
		if (c < 0x800) {
			*dst++ = 0xc0 + (c >> 6);
			goto t1;
		}
		if (c < 0x10000) {
			*dst++ = 0xe0 + (c >> 12);
			goto t2;
		}
		*dst++ = 0xf0 + (c >> 18);
		*dst++ = 0x80 + ((c >> 12) & 0x3f);
	t2:
		*dst++ = 0x80 + ((c >> 6) & 0x3f);
	t1:
		*dst++ = 0x80 + (c & 0x3f);
	}

	return dst;
}

#ifndef MAX_CMDLINE_ADDRESS
#define MAX_CMDLINE_ADDRESS	ULONG_MAX
#endif

/*
 * Convert the unicode UEFI command line to ASCII to pass to kernel.
 * Size of memory allocated return in *cmd_line_len.
 * Returns NULL on error.
 */
char *efi_convert_cmdline(efi_system_table_t *sys_table_arg,
			  efi_loaded_image_t *image,
			  int *cmd_line_len)
{
	const u16 *s2;
	u8 *s1 = NULL;
	unsigned long cmdline_addr = 0;
	int load_options_chars = image->load_options_size / 2; /* UTF-16 */
	const u16 *options = image->load_options;
	int options_bytes = 0;  /* UTF-8 bytes */
	int options_chars = 0;  /* UTF-16 chars */
	efi_status_t status;
	u16 zero = 0;

	if (options) {
		s2 = options;
		while (*s2 && *s2 != '\n'
		       && options_chars < load_options_chars) {
			options_bytes += efi_utf8_bytes(*s2++);
			options_chars++;
		}
	}

	if (!options_chars) {
		/* No command line options, so return empty string*/
		options = &zero;
	}

	options_bytes++;	/* NUL termination */

	status = efi_high_alloc(sys_table_arg, options_bytes, 0,
				&cmdline_addr, MAX_CMDLINE_ADDRESS);
	if (status != EFI_SUCCESS)
		return NULL;

	s1 = (u8 *)cmdline_addr;
	s2 = (const u16 *)options;

	s1 = efi_utf16_to_utf8(s1, s2, options_chars);
	*s1 = '\0';

	*cmd_line_len = options_bytes;
	return (char *)cmdline_addr;
}

/*
 * Handle calling ExitBootServices according to the requirements set out by the
 * spec.  Obtains the current memory map, and returns that info after calling
 * ExitBootServices.  The client must specify a function to perform any
 * processing of the memory map data prior to ExitBootServices.  A client
 * specific structure may be passed to the function via priv.  The client
 * function may be called multiple times.
 */
efi_status_t efi_exit_boot_services(efi_system_table_t *sys_table_arg,
				    void *handle,
				    struct efi_boot_memmap *map,
				    void *priv,
				    efi_exit_boot_map_processing priv_func)
{
	efi_status_t status;

	status = efi_get_memory_map(sys_table_arg, map);

	if (status != EFI_SUCCESS)
		goto fail;

	status = priv_func(sys_table_arg, map, priv);
	if (status != EFI_SUCCESS)
		goto free_map;

	status = efi_call_early(exit_boot_services, handle, *map->key_ptr);

	if (status == EFI_INVALID_PARAMETER) {
		/*
		 * The memory map changed between efi_get_memory_map() and
		 * exit_boot_services().  Per the UEFI Spec v2.6, Section 6.4:
		 * EFI_BOOT_SERVICES.ExitBootServices we need to get the
		 * updated map, and try again.  The spec implies one retry
		 * should be sufficent, which is confirmed against the EDK2
		 * implementation.  Per the spec, we can only invoke
		 * get_memory_map() and exit_boot_services() - we cannot alloc
		 * so efi_get_memory_map() cannot be used, and we must reuse
		 * the buffer.  For all practical purposes, the headroom in the
		 * buffer should account for any changes in the map so the call
		 * to get_memory_map() is expected to succeed here.
		 */
		*map->map_size = *map->buff_size;
		status = efi_call_early(get_memory_map,
					map->map_size,
					*map->map,
					map->key_ptr,
					map->desc_size,
					map->desc_ver);

		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			goto fail;

		status = priv_func(sys_table_arg, map, priv);
		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			goto fail;

		status = efi_call_early(exit_boot_services, handle, *map->key_ptr);
	}

	/* exit_boot_services() was called, thus cannot free */
	if (status != EFI_SUCCESS)
		goto fail;

	return EFI_SUCCESS;

free_map:
	efi_call_early(free_pool, *map->map);
fail:
	return status;
}
