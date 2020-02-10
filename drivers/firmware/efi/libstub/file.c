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
 * trying efi=nochunk, but chunking is enabled by default on x86 because
 * there are far more machines that require the workaround than those that
 * break with it enabled.
 */
#define EFI_READ_CHUNK_SIZE	SZ_1M

struct file_info {
	efi_file_protocol_t *handle;
	u64 size;
};

static efi_status_t efi_file_size(void *__fh, efi_char16_t *filename_16,
				  void **handle, u64 *file_sz)
{
	efi_file_protocol_t *h, *fh = __fh;
	efi_file_info_t *info;
	efi_status_t status;
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	unsigned long info_sz;

	status = fh->open(fh, &h, filename_16, EFI_FILE_MODE_READ, 0);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to open file: ");
		efi_char16_printk(filename_16);
		efi_printk("\n");
		return status;
	}

	*handle = h;

	info_sz = 0;
	status = h->get_info(h, &info_guid, &info_sz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		efi_printk("Failed to get file info size\n");
		return status;
	}

grow:
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, info_sz,
			     (void **)&info);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc mem for file info\n");
		return status;
	}

	status = h->get_info(h, &info_guid, &info_sz, info);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_bs_call(free_pool, info);
		goto grow;
	}

	*file_sz = info->file_size;
	efi_bs_call(free_pool, info);

	if (status != EFI_SUCCESS)
		efi_printk("Failed to get initrd info\n");

	return status;
}

static efi_status_t efi_file_read(efi_file_protocol_t *handle,
				  unsigned long *size, void *addr)
{
	return handle->read(handle, size, addr);
}

static efi_status_t efi_file_close(efi_file_protocol_t *handle)
{
	return handle->close(handle);
}

static efi_status_t efi_open_volume(efi_loaded_image_t *image,
				    efi_file_protocol_t **__fh)
{
	efi_simple_file_system_protocol_t *io;
	efi_file_protocol_t *fh;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_status_t status;
	efi_handle_t handle = image->device_handle;

	status = efi_bs_call(handle_protocol, handle, &fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to handle fs_proto\n");
		return status;
	}

	status = io->open_volume(io, &fh);
	if (status != EFI_SUCCESS)
		efi_printk("Failed to open volume\n");
	else
		*__fh = fh;

	return status;
}

/*
 * Check the cmdline for a LILO-style file= arguments.
 *
 * We only support loading a file from the same filesystem as
 * the kernel image.
 */
efi_status_t handle_cmdline_files(efi_loaded_image_t *image,
				  char *cmd_line, char *option_string,
				  unsigned long max_addr,
				  unsigned long *load_addr,
				  unsigned long *load_size)
{
	unsigned long efi_chunk_size = ULONG_MAX;
	struct file_info *files;
	unsigned long file_addr;
	u64 file_size_total;
	efi_file_protocol_t *fh = NULL;
	efi_status_t status;
	int nr_files;
	char *str;
	int i, j, k;

	if (IS_ENABLED(CONFIG_X86) && !nochunk())
		efi_chunk_size = EFI_READ_CHUNK_SIZE;

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

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
			     nr_files * sizeof(*files), (void **)&files);
	if (status != EFI_SUCCESS) {
		pr_efi_err("Failed to alloc mem for file handle list\n");
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
			status = efi_open_volume(image, &fh);
			if (status != EFI_SUCCESS)
				goto free_files;
		}

		status = efi_file_size(fh, filename_16, (void **)&file->handle,
				       &file->size);
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
		status = efi_allocate_pages(file_size_total, &file_addr, max_addr);
		if (status != EFI_SUCCESS) {
			pr_efi_err("Failed to alloc highmem for files\n");
			goto close_handles;
		}

		/* We've run out of free low memory. */
		if (file_addr > max_addr) {
			pr_efi_err("We've run out of free low memory\n");
			status = EFI_INVALID_PARAMETER;
			goto free_file_total;
		}

		addr = file_addr;
		for (j = 0; j < nr_files; j++) {
			unsigned long size;

			size = files[j].size;
			while (size) {
				unsigned long chunksize;

				if (size > efi_chunk_size)
					chunksize = efi_chunk_size;
				else
					chunksize = size;

				status = efi_file_read(files[j].handle,
						       &chunksize,
						       (void *)addr);
				if (status != EFI_SUCCESS) {
					pr_efi_err("Failed to read file\n");
					goto free_file_total;
				}
				addr += chunksize;
				size -= chunksize;
			}

			efi_file_close(files[j].handle);
		}

	}

	efi_bs_call(free_pool, files);

	*load_addr = file_addr;
	*load_size = file_size_total;

	return status;

free_file_total:
	efi_free(file_size_total, file_addr);

close_handles:
	for (k = j; k < i; k++)
		efi_file_close(files[k].handle);
free_files:
	efi_bs_call(free_pool, files);
fail:
	*load_addr = 0;
	*load_size = 0;

	return status;
}
