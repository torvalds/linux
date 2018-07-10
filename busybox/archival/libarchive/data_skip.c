/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC data_skip(archive_handle_t *archive_handle)
{
	archive_handle->seek(archive_handle->src_fd, archive_handle->file_header->size);
}
