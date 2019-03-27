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
#include "archive_read_private.h"

static void
add_passphrase_to_tail(struct archive_read *a,
    struct archive_read_passphrase *p)
{
	*a->passphrases.last = p;
	a->passphrases.last = &p->next;
	p->next = NULL;
}

static struct archive_read_passphrase *
remove_passphrases_from_head(struct archive_read *a)
{
	struct archive_read_passphrase *p;

	p = a->passphrases.first;
	if (p != NULL)
		a->passphrases.first = p->next;
	return (p);
}

static void
insert_passphrase_to_head(struct archive_read *a,
    struct archive_read_passphrase *p)
{
	p->next = a->passphrases.first;
	a->passphrases.first = p;
}

static struct archive_read_passphrase *
new_read_passphrase(struct archive_read *a, const char *passphrase)
{
	struct archive_read_passphrase *p;

	p = malloc(sizeof(*p));
	if (p == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory");
		return (NULL);
	}
	p->passphrase = strdup(passphrase);
	if (p->passphrase == NULL) {
		free(p);
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory");
		return (NULL);
	}
	return (p);
}

int
archive_read_add_passphrase(struct archive *_a, const char *passphrase)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_passphrase *p;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
		"archive_read_add_passphrase");

	if (passphrase == NULL || passphrase[0] == '\0') {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Empty passphrase is unacceptable");
		return (ARCHIVE_FAILED);
	}

	p = new_read_passphrase(a, passphrase);
	if (p == NULL)
		return (ARCHIVE_FATAL);
	add_passphrase_to_tail(a, p);

	return (ARCHIVE_OK);
}

int
archive_read_set_passphrase_callback(struct archive *_a, void *client_data,
    archive_passphrase_callback *cb)
{
	struct archive_read *a = (struct archive_read *)_a;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
		"archive_read_set_passphrase_callback");

	a->passphrases.callback = cb;
	a->passphrases.client_data = client_data;
	return (ARCHIVE_OK);
}

/*
 * Call this in advance when you start to get a passphrase for decryption
 * for a entry.
 */
void
__archive_read_reset_passphrase(struct archive_read *a)
{

	a->passphrases.candidate = -1;
}

/*
 * Get a passphrase for decryption.
 */
const char *
__archive_read_next_passphrase(struct archive_read *a)
{
	struct archive_read_passphrase *p;
	const char *passphrase;

	if (a->passphrases.candidate < 0) {
		/* Count out how many passphrases we have. */
		int cnt = 0;

		for (p = a->passphrases.first; p != NULL; p = p->next)
			cnt++;
		a->passphrases.candidate = cnt;
		p = a->passphrases.first;
	} else if (a->passphrases.candidate > 1) {
		/* Rotate a passphrase list. */
		a->passphrases.candidate--;
		p = remove_passphrases_from_head(a);
		add_passphrase_to_tail(a, p);
		/* Pick a new passphrase candidate up. */
		p = a->passphrases.first;
	} else if (a->passphrases.candidate == 1) {
		/* This case is that all candidates failed to decrypt. */
		a->passphrases.candidate = 0;
		if (a->passphrases.first->next != NULL) {
			/* Rotate a passphrase list. */
			p = remove_passphrases_from_head(a);
			add_passphrase_to_tail(a, p);
		}
		p = NULL;
	} else  /* There is no passphrase candidate. */
		p = NULL;

	if (p != NULL)
		passphrase = p->passphrase;
	else if (a->passphrases.callback != NULL) {
		/* Get a passphrase through a call-back function
		 * since we tried all passphrases out or we don't
		 * have it. */
		passphrase = a->passphrases.callback(&a->archive,
		    a->passphrases.client_data);
		if (passphrase != NULL) {
			p = new_read_passphrase(a, passphrase);
			if (p == NULL)
				return (NULL);
			insert_passphrase_to_head(a, p);
			a->passphrases.candidate = 1;
		}
	} else
		passphrase = NULL;

	return (passphrase);
}
