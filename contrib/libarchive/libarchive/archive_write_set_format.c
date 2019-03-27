/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "archive.h"
#include "archive_private.h"

/* A table that maps format codes to functions. */
static const
struct { int code; int (*setter)(struct archive *); } codes[] =
{
	{ ARCHIVE_FORMAT_7ZIP,		archive_write_set_format_7zip },
	{ ARCHIVE_FORMAT_CPIO,		archive_write_set_format_cpio },
	{ ARCHIVE_FORMAT_CPIO_POSIX,	archive_write_set_format_cpio },
	{ ARCHIVE_FORMAT_CPIO_SVR4_NOCRC,	archive_write_set_format_cpio_newc },
	{ ARCHIVE_FORMAT_ISO9660,	archive_write_set_format_iso9660 },
	{ ARCHIVE_FORMAT_MTREE,		archive_write_set_format_mtree },
	{ ARCHIVE_FORMAT_RAW,		archive_write_set_format_raw },
	{ ARCHIVE_FORMAT_SHAR,		archive_write_set_format_shar },
	{ ARCHIVE_FORMAT_SHAR_BASE,	archive_write_set_format_shar },
	{ ARCHIVE_FORMAT_SHAR_DUMP,	archive_write_set_format_shar_dump },
	{ ARCHIVE_FORMAT_TAR,	archive_write_set_format_pax_restricted },
	{ ARCHIVE_FORMAT_TAR_GNUTAR,	archive_write_set_format_gnutar },
	{ ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, archive_write_set_format_pax },
	{ ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
				archive_write_set_format_pax_restricted },
	{ ARCHIVE_FORMAT_TAR_USTAR,	archive_write_set_format_ustar },
	{ ARCHIVE_FORMAT_WARC,		archive_write_set_format_warc },
	{ ARCHIVE_FORMAT_XAR,		archive_write_set_format_xar },
	{ ARCHIVE_FORMAT_ZIP,		archive_write_set_format_zip },
	{ 0,		NULL }
};

int
archive_write_set_format(struct archive *a, int code)
{
	int i;

	for (i = 0; codes[i].code != 0; i++) {
		if (code == codes[i].code)
			return ((codes[i].setter)(a));
	}

	archive_set_error(a, EINVAL, "No such format");
	return (ARCHIVE_FATAL);
}
