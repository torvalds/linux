/*-
 * Copyright (c) 2014 Michihiro NAKAJIMA
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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "archive_write_private.h"

int
archive_write_set_passphrase(struct archive *_a, const char *p)
{
	struct archive_write *a = (struct archive_write *)_a;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC, ARCHIVE_STATE_NEW,
		"archive_write_set_passphrase");

	if (p == NULL || p[0] == '\0') {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Empty passphrase is unacceptable");
		return (ARCHIVE_FAILED);
	}
	free(a->passphrase);
	a->passphrase = strdup(p);
	if (a->passphrase == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate data for passphrase");
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}


int
archive_write_set_passphrase_callback(struct archive *_a, void *client_data,
    archive_passphrase_callback *cb)
{
	struct archive_write *a = (struct archive_write *)_a;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC, ARCHIVE_STATE_NEW,
		"archive_write_set_passphrase_callback");

	a->passphrase_callback = cb;
	a->passphrase_client_data = client_data;
	return (ARCHIVE_OK);
}


const char *
__archive_write_get_passphrase(struct archive_write *a)
{

	if (a->passphrase != NULL)
		return (a->passphrase);

	if (a->passphrase_callback != NULL) {
		const char *p;
		p = a->passphrase_callback(&a->archive,
		    a->passphrase_client_data);
		if (p != NULL) {
			a->passphrase = strdup(p);
			if (a->passphrase == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate data for passphrase");
				return (NULL);
			}
			return (a->passphrase);
		}
	}
	return (NULL);
}
