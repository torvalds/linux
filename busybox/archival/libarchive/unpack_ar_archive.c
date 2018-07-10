/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"
#include "ar.h"

void FAST_FUNC unpack_ar_archive(archive_handle_t *ar_archive)
{
	char magic[7];

	xread(ar_archive->src_fd, magic, AR_MAGIC_LEN);
	if (!is_prefixed_with(magic, AR_MAGIC)) {
		bb_error_msg_and_die("invalid ar magic");
	}
	ar_archive->offset += AR_MAGIC_LEN;

	while (get_header_ar(ar_archive) == EXIT_SUCCESS)
		continue;
}
