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

#define MAX_FILENAME_SIZE	256

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

struct finfo {
	efi_file_info_t info;
	efi_char16_t	filename[MAX_FILENAME_SIZE];
};

static efi_status_t efi_open_file(efi_file_protocol_t *volume,
				  struct finfo *fi,
				  efi_file_protocol_t **handle,
				  unsigned long *file_size)
{
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	efi_file_protocol_t *fh;
	unsigned long info_sz;
	efi_status_t status;
	efi_char16_t *c;

	/* Replace UNIX dir separators with EFI standard ones */
	for (c = fi->filename; *c != L'\0'; c++) {
		if (*c == L'/')
			*c = L'\\';
	}

	status = efi_call_proto(volume, open, &fh, fi->filename,
				EFI_FILE_MODE_READ, 0);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to open file: %ls\n", fi->filename);
		return status;
	}

	info_sz = sizeof(struct finfo);
	status = efi_call_proto(fh, get_info, &info_guid, &info_sz, fi);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to get file info\n");
		efi_call_proto(fh, close);
		return status;
	}

	*handle = fh;
	*file_size = fi->info.file_size;
	return EFI_SUCCESS;
}

static efi_status_t efi_open_volume(efi_loaded_image_t *image,
				    efi_file_protocol_t **fh)
{
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_simple_file_system_protocol_t *io;
	efi_status_t status;

	status = efi_bs_call(handle_protocol, efi_table_attr(image, device_handle),
			     &fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to handle fs_proto\n");
		return status;
	}

	status = efi_call_proto(io, open_volume, fh);
	if (status != EFI_SUCCESS)
		efi_err("Failed to open volume\n");

	return status;
}

static int find_file_option(const efi_char16_t *cmdline, int cmdline_len,
			    const efi_char16_t *prefix, int prefix_size,
			    efi_char16_t *result, int result_len)
{
	int prefix_len = prefix_size / 2;
	bool found = false;
	int i;

	for (i = prefix_len; i < cmdline_len; i++) {
		if (!memcmp(&cmdline[i - prefix_len], prefix, prefix_size)) {
			found = true;
			break;
		}
	}

	if (!found)
		return 0;

	/* Skip any leading slashes */
	while (i < cmdline_len && (cmdline[i] == L'/' || cmdline[i] == L'\\'))
		i++;

	while (--result_len > 0 && i < cmdline_len) {
		efi_char16_t c = cmdline[i++];

		if (c == L'\0' || c == L'\n' || c == L' ')
			break;
		*result++ = c;
	}
	*result = L'\0';
	return i;
}

static efi_status_t efi_open_device_path(efi_file_protocol_t **volume,
					 struct finfo *fi)
{
	efi_guid_t text_to_dp_guid = EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL_GUID;
	static efi_device_path_from_text_protocol_t *text_to_dp = NULL;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_device_path_protocol_t *initrd_dp;
	efi_simple_file_system_protocol_t *io;
	struct efi_file_path_dev_path *fpath;
	efi_handle_t handle;
	efi_status_t status;

	/* See if the text to device path protocol exists */
	if (!text_to_dp &&
	    efi_bs_call(locate_protocol, &text_to_dp_guid, NULL,
			(void **)&text_to_dp) != EFI_SUCCESS)
		return EFI_UNSUPPORTED;


	/* Convert the filename wide string into a device path */
	initrd_dp = efi_fn_call(text_to_dp, convert_text_to_device_path,
				fi->filename);

	/* Check whether the device path in question implements simple FS */
	if ((efi_bs_call(locate_device_path, &fs_proto, &initrd_dp, &handle) ?:
	     efi_bs_call(handle_protocol, handle, &fs_proto, (void **)&io))
	    != EFI_SUCCESS)
		return EFI_NOT_FOUND;

	/* Check whether the remaining device path is a file device path */
	if (initrd_dp->type != EFI_DEV_MEDIA ||
	    initrd_dp->sub_type != EFI_DEV_MEDIA_FILE) {
		efi_warn("Unexpected device path node type: (%x, %x)\n",
			 initrd_dp->type, initrd_dp->sub_type);
		return EFI_LOAD_ERROR;
	}

	/* Copy the remaining file path into the fi structure */
	fpath = (struct efi_file_path_dev_path *)initrd_dp;
	memcpy(fi->filename, fpath->filename,
	       min(sizeof(fi->filename),
		   fpath->header.length - sizeof(fpath->header)));

	status = efi_call_proto(io, open_volume, volume);
	if (status != EFI_SUCCESS)
		efi_err("Failed to open volume\n");

	return status;
}

/*
 * Check the cmdline for a LILO-style file= arguments.
 *
 * We only support loading a file from the same filesystem as
 * the kernel image.
 */
efi_status_t handle_cmdline_files(efi_loaded_image_t *image,
				  const efi_char16_t *optstr,
				  int optstr_size,
				  unsigned long soft_limit,
				  unsigned long hard_limit,
				  unsigned long *load_addr,
				  unsigned long *load_size)
{
	const efi_char16_t *cmdline = efi_table_attr(image, load_options);
	u32 cmdline_len = efi_table_attr(image, load_options_size);
	unsigned long efi_chunk_size = ULONG_MAX;
	efi_file_protocol_t *volume = NULL;
	efi_file_protocol_t *file;
	unsigned long alloc_addr;
	unsigned long alloc_size;
	efi_status_t status;
	int offset;

	if (!load_addr || !load_size)
		return EFI_INVALID_PARAMETER;

	efi_apply_loadoptions_quirk((const void **)&cmdline, &cmdline_len);
	cmdline_len /= sizeof(*cmdline);

	if (IS_ENABLED(CONFIG_X86) && !efi_nochunk)
		efi_chunk_size = EFI_READ_CHUNK_SIZE;

	alloc_addr = alloc_size = 0;
	do {
		struct finfo fi;
		unsigned long size;
		void *addr;

		offset = find_file_option(cmdline, cmdline_len,
					  optstr, optstr_size,
					  fi.filename, ARRAY_SIZE(fi.filename));

		if (!offset)
			break;

		cmdline += offset;
		cmdline_len -= offset;

		status = efi_open_device_path(&volume, &fi);
		if (status == EFI_UNSUPPORTED || status == EFI_NOT_FOUND)
			/* try the volume that holds the kernel itself */
			status = efi_open_volume(image, &volume);

		if (status != EFI_SUCCESS)
			goto err_free_alloc;

		status = efi_open_file(volume, &fi, &file, &size);
		if (status != EFI_SUCCESS)
			goto err_close_volume;

		/*
		 * Check whether the existing allocation can contain the next
		 * file. This condition will also trigger naturally during the
		 * first (and typically only) iteration of the loop, given that
		 * alloc_size == 0 in that case.
		 */
		if (round_up(alloc_size + size, EFI_ALLOC_ALIGN) >
		    round_up(alloc_size, EFI_ALLOC_ALIGN)) {
			unsigned long old_addr = alloc_addr;

			status = EFI_OUT_OF_RESOURCES;
			if (soft_limit < hard_limit)
				status = efi_allocate_pages(alloc_size + size,
							    &alloc_addr,
							    soft_limit);
			if (status == EFI_OUT_OF_RESOURCES)
				status = efi_allocate_pages(alloc_size + size,
							    &alloc_addr,
							    hard_limit);
			if (status != EFI_SUCCESS) {
				efi_err("Failed to allocate memory for files\n");
				goto err_close_file;
			}

			if (old_addr != 0) {
				/*
				 * This is not the first time we've gone
				 * around this loop, and so we are loading
				 * multiple files that need to be concatenated
				 * and returned in a single buffer.
				 */
				memcpy((void *)alloc_addr, (void *)old_addr, alloc_size);
				efi_free(alloc_size, old_addr);
			}
		}

		addr = (void *)alloc_addr + alloc_size;
		alloc_size += size;

		while (size) {
			unsigned long chunksize = min(size, efi_chunk_size);

			status = efi_call_proto(file, read, &chunksize, addr);
			if (status != EFI_SUCCESS) {
				efi_err("Failed to read file\n");
				goto err_close_file;
			}
			addr += chunksize;
			size -= chunksize;
		}
		efi_call_proto(file, close);
		efi_call_proto(volume, close);
	} while (offset > 0);

	*load_addr = alloc_addr;
	*load_size = alloc_size;

	if (*load_size == 0)
		return EFI_NOT_READY;
	return EFI_SUCCESS;

err_close_file:
	efi_call_proto(file, close);

err_close_volume:
	efi_call_proto(volume, close);

err_free_alloc:
	efi_free(alloc_size, alloc_addr);
	return status;
}
