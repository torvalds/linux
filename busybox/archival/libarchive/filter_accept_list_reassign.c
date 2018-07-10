/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2002 by Glenn McGrath
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

/* Built and used only if ENABLE_DPKG || ENABLE_DPKG_DEB */

/*
 * Reassign the subarchive metadata parser based on the filename extension
 * e.g. if its a .tar.gz modify archive_handle->sub_archive to process a .tar.gz
 * or if its a .tar.bz2 make archive_handle->sub_archive handle that
 */
char FAST_FUNC filter_accept_list_reassign(archive_handle_t *archive_handle)
{
	/* Check the file entry is in the accept list */
	if (find_list_entry(archive_handle->accept, archive_handle->file_header->name)) {
		const char *name_ptr;

		/* Find extension */
		name_ptr = strrchr(archive_handle->file_header->name, '.');
		if (!name_ptr)
			return EXIT_FAILURE;
		name_ptr++;

		/* Modify the subarchive handler based on the extension */
		if (strcmp(name_ptr, "tar") == 0) {
			archive_handle->dpkg__action_data_subarchive = get_header_tar;
			return EXIT_SUCCESS;
		}
		if (ENABLE_FEATURE_SEAMLESS_GZ
		 && strcmp(name_ptr, "gz") == 0
		) {
			archive_handle->dpkg__action_data_subarchive = get_header_tar_gz;
			return EXIT_SUCCESS;
		}
		if (ENABLE_FEATURE_SEAMLESS_BZ2
		 && strcmp(name_ptr, "bz2") == 0
		) {
			archive_handle->dpkg__action_data_subarchive = get_header_tar_bz2;
			return EXIT_SUCCESS;
		}
		if (ENABLE_FEATURE_SEAMLESS_LZMA
		 && strcmp(name_ptr, "lzma") == 0
		) {
			archive_handle->dpkg__action_data_subarchive = get_header_tar_lzma;
			return EXIT_SUCCESS;
		}
		if (ENABLE_FEATURE_SEAMLESS_XZ
		 && strcmp(name_ptr, "xz") == 0
		) {
			archive_handle->dpkg__action_data_subarchive = get_header_tar_xz;
			return EXIT_SUCCESS;
		}
	}
	return EXIT_FAILURE;
}
