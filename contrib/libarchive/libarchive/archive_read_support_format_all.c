/*-
 * Copyright (c) 2003-2011 Tim Kientzle
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

#include "archive.h"
#include "archive_private.h"

int
archive_read_support_format_all(struct archive *a)
{
	archive_check_magic(a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_all");

	/* TODO: It would be nice to compute the ordering
	 * here automatically so that people who enable just
	 * a few formats can still get the benefits.  That
	 * may just require the format registration to include
	 * a "maximum read-ahead" value (anything that uses seek
	 * would be essentially infinite read-ahead).  The core
	 * bid management can then sort the bidders before calling
	 * them.
	 *
	 * If you implement the above, please return the list below
	 * to alphabetic order.
	 */

	/*
	 * These bidders are all pretty cheap; they just examine a
	 * small initial part of the archive.  If one of these bids
	 * high, we can maybe avoid running any of the more expensive
	 * bidders below.
	 */
	archive_read_support_format_ar(a);
	archive_read_support_format_cpio(a);
	archive_read_support_format_empty(a);
	archive_read_support_format_lha(a);
	archive_read_support_format_mtree(a);
	archive_read_support_format_tar(a);
	archive_read_support_format_xar(a);
	archive_read_support_format_warc(a);

	/*
	 * Install expensive bidders last.  By doing them last, we
	 * increase the chance that a high bid from someone else will
	 * make it unnecessary for these to do anything at all.
	 */
	/* These three have potentially large look-ahead. */
	archive_read_support_format_7zip(a);
	archive_read_support_format_cab(a);
	archive_read_support_format_rar(a);
	archive_read_support_format_rar5(a);
	archive_read_support_format_iso9660(a);
	/* Seek is really bad, since it forces the read-ahead
	 * logic to discard buffered data. */
	archive_read_support_format_zip(a);

	/* Note: We always return ARCHIVE_OK here, even if some of the
	 * above return ARCHIVE_WARN.  The intent here is to enable
	 * "as much as possible."  Clients who need specific
	 * compression should enable those individually so they can
	 * verify the level of support. */
	/* Clear any warning messages set by the above functions. */
	archive_clear_error(a);
	return (ARCHIVE_OK);
}
