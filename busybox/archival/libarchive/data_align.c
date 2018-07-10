/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC data_align(archive_handle_t *archive_handle, unsigned boundary)
{
	unsigned skip_amount = (boundary - (archive_handle->offset % boundary)) % boundary;

	archive_handle->seek(archive_handle->src_fd, skip_amount);
	archive_handle->offset += skip_amount;
}
