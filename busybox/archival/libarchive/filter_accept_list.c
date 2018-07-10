/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2002 by Glenn McGrath
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

/*
 * Accept names that are in the accept list, ignoring reject list.
 */
char FAST_FUNC filter_accept_list(archive_handle_t *archive_handle)
{
	if (find_list_entry(archive_handle->accept, archive_handle->file_header->name))
		return EXIT_SUCCESS;
	return EXIT_FAILURE;
}
