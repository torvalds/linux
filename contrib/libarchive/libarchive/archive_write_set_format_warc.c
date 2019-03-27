/*-
 * Copyright (c) 2014 Sebastian Freundt
 * Author: Sebastian Freundt  <devel@fresse.org>
 *
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
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_random_private.h"
#include "archive_write_private.h"

struct warc_s {
	unsigned int omit_warcinfo:1;

	time_t now;
	mode_t typ;
	unsigned int rng;
	/* populated size */
	uint64_t populz;
};

static const char warcinfo[] =
    "software: libarchive/" ARCHIVE_VERSION_ONLY_STRING "\r\n"
    "format: WARC file version 1.0\r\n";

typedef enum {
	WT_NONE,
	/* warcinfo */
	WT_INFO,
	/* metadata */
	WT_META,
	/* resource */
	WT_RSRC,
	/* request, unsupported */
	WT_REQ,
	/* response, unsupported */
	WT_RSP,
	/* revisit, unsupported */
	WT_RVIS,
	/* conversion, unsupported */
	WT_CONV,
	/* continuation, unsupported at the moment */
	WT_CONT,
	/* invalid type */
	LAST_WT
} warc_type_t;

typedef struct {
	warc_type_t type;
	const char *tgturi;
	const char *recid;
	time_t rtime;
	time_t mtime;
	const char *cnttyp;
	uint64_t cntlen;
} warc_essential_hdr_t;

typedef struct {
	unsigned int u[4U];
} warc_uuid_t;

static int _warc_options(struct archive_write*, const char *key, const char *v);
static int _warc_header(struct archive_write *a, struct archive_entry *entry);
static ssize_t _warc_data(struct archive_write *a, const void *buf, size_t sz);
static int _warc_finish_entry(struct archive_write *a);
static int _warc_close(struct archive_write *a);
static int _warc_free(struct archive_write *a);

/* private routines */
static ssize_t _popul_ehdr(struct archive_string *t, size_t z, warc_essential_hdr_t);
static int _gen_uuid(warc_uuid_t *tgt);


/*
 * Set output format to ISO 28500 (aka WARC) format.
 */
int
archive_write_set_format_warc(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct warc_s *w;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_set_format_warc");

	/* If another format was already registered, unregister it. */
	if (a->format_free != NULL) {
		(a->format_free)(a);
	}

	w = malloc(sizeof(*w));
	if (w == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate warc data");
		return (ARCHIVE_FATAL);
	}
	/* by default we're emitting a file wide header */
	w->omit_warcinfo = 0U;
	/* obtain current time for date fields */
	w->now = time(NULL);
	/* reset file type info */
	w->typ = 0;
	/* also initialise our rng */
	w->rng = (unsigned int)w->now;

	a->format_data = w;
	a->format_name = "WARC/1.0";
	a->format_options = _warc_options;
	a->format_write_header = _warc_header;
	a->format_write_data = _warc_data;
	a->format_close = _warc_close;
	a->format_free = _warc_free;
	a->format_finish_entry = _warc_finish_entry;
	a->archive.archive_format = ARCHIVE_FORMAT_WARC;
	a->archive.archive_format_name = "WARC/1.0";
	return (ARCHIVE_OK);
}


/* archive methods */
static int
_warc_options(struct archive_write *a, const char *key, const char *val)
{
	struct warc_s *w = a->format_data;

	if (strcmp(key, "omit-warcinfo") == 0) {
		if (val == NULL || strcmp(val, "true") == 0) {
			/* great */
			w->omit_warcinfo = 1U;
			return (ARCHIVE_OK);
		}
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
_warc_header(struct archive_write *a, struct archive_entry *entry)
{
	struct warc_s *w = a->format_data;
	struct archive_string hdr;
#define MAX_HDR_SIZE 512

	/* check whether warcinfo record needs outputting */
	if (!w->omit_warcinfo) {
		ssize_t r;
		warc_essential_hdr_t wi = {
			WT_INFO,
			/*uri*/NULL,
			/*urn*/NULL,
			/*rtm*/0,
			/*mtm*/0,
			/*cty*/"application/warc-fields",
			/*len*/sizeof(warcinfo) - 1U,
		};
		wi.rtime = w->now;
		wi.mtime = w->now;

		archive_string_init(&hdr);
		r = _popul_ehdr(&hdr, MAX_HDR_SIZE, wi);
		if (r >= 0) {
			/* jackpot! */
			/* now also use HDR buffer for the actual warcinfo */
			archive_strncat(&hdr, warcinfo, sizeof(warcinfo) -1);

			/* append end-of-record indicator */
			archive_strncat(&hdr, "\r\n\r\n", 4);

			/* write to output stream */
			__archive_write_output(a, hdr.s, archive_strlen(&hdr));
		}
		/* indicate we're done with file header writing */
		w->omit_warcinfo = 1U;
		archive_string_free(&hdr);
	}

	if (archive_entry_pathname(entry) == NULL) {
		archive_set_error(&a->archive, EINVAL,
		    "Invalid filename");
		return (ARCHIVE_WARN);
	}

	w->typ = archive_entry_filetype(entry);
	w->populz = 0U;
	if (w->typ == AE_IFREG) {
		warc_essential_hdr_t rh = {
			WT_RSRC,
			/*uri*/NULL,
			/*urn*/NULL,
			/*rtm*/0,
			/*mtm*/0,
			/*cty*/NULL,
			/*len*/0,
		};
		ssize_t r;
		rh.tgturi = archive_entry_pathname(entry);
		rh.rtime = w->now;
		rh.mtime = archive_entry_mtime(entry);
		rh.cntlen = (size_t)archive_entry_size(entry);

		archive_string_init(&hdr);
		r = _popul_ehdr(&hdr, MAX_HDR_SIZE, rh);
		if (r < 0) {
			/* don't bother */
			archive_set_error(
				&a->archive,
				ARCHIVE_ERRNO_FILE_FORMAT,
				"cannot archive file");
			return (ARCHIVE_WARN);
		}
		/* otherwise append to output stream */
		__archive_write_output(a, hdr.s, r);
		/* and let subsequent calls to _data() know about the size */
		w->populz = rh.cntlen;
		archive_string_free(&hdr);
		return (ARCHIVE_OK);
	}
	/* just resort to erroring as per Tim's advice */
	archive_set_error(
		&a->archive,
		ARCHIVE_ERRNO_FILE_FORMAT,
		"WARC can only process regular files");
	return (ARCHIVE_FAILED);
}

static ssize_t
_warc_data(struct archive_write *a, const void *buf, size_t len)
{
	struct warc_s *w = a->format_data;

	if (w->typ == AE_IFREG) {
		int rc;

		/* never write more bytes than announced */
		if (len > w->populz) {
			len = (size_t)w->populz;
		}

		/* now then, out we put the whole shebang */
		rc = __archive_write_output(a, buf, len);
		if (rc != ARCHIVE_OK) {
			return rc;
		}
	}
	return len;
}

static int
_warc_finish_entry(struct archive_write *a)
{
	static const char _eor[] = "\r\n\r\n";
	struct warc_s *w = a->format_data;

	if (w->typ == AE_IFREG) {
		int rc = __archive_write_output(a, _eor, sizeof(_eor) - 1U);

		if (rc != ARCHIVE_OK) {
			return rc;
		}
	}
	/* reset type info */
	w->typ = 0;
	return (ARCHIVE_OK);
}

static int
_warc_close(struct archive_write *a)
{
	(void)a; /* UNUSED */
	return (ARCHIVE_OK);
}

static int
_warc_free(struct archive_write *a)
{
	struct warc_s *w = a->format_data;

	free(w);
	a->format_data = NULL;
	return (ARCHIVE_OK);
}


/* private routines */
static void
xstrftime(struct archive_string *as, const char *fmt, time_t t)
{
/** like strftime(3) but for time_t objects */
	struct tm *rt;
#if defined(HAVE_GMTIME_R) || defined(HAVE__GMTIME64_S)
	struct tm timeHere;
#endif
	char strtime[100];
	size_t len;

#ifdef HAVE_GMTIME_R
	if ((rt = gmtime_r(&t, &timeHere)) == NULL)
		return;
#elif defined(HAVE__GMTIME64_S)
	_gmtime64_s(&timeHere, &t);
#else
	if ((rt = gmtime(&t)) == NULL)
		return;
#endif
	/* leave the hard yacker to our role model strftime() */
	len = strftime(strtime, sizeof(strtime)-1, fmt, rt);
	archive_strncat(as, strtime, len);
}

static ssize_t
_popul_ehdr(struct archive_string *tgt, size_t tsz, warc_essential_hdr_t hdr)
{
	static const char _ver[] = "WARC/1.0\r\n";
	static const char * const _typ[LAST_WT] = {
		NULL, "warcinfo", "metadata", "resource", NULL
	};
	char std_uuid[48U];

	if (hdr.type == WT_NONE || hdr.type > WT_RSRC) {
		/* brilliant, how exactly did we get here? */
		return -1;
	}

	archive_strcpy(tgt, _ver);

	archive_string_sprintf(tgt, "WARC-Type: %s\r\n", _typ[hdr.type]);

	if (hdr.tgturi != NULL) {
		/* check if there's a xyz:// */
		static const char _uri[] = "";
		static const char _fil[] = "file://";
		const char *u;
		char *chk = strchr(hdr.tgturi, ':');

		if (chk != NULL && chk[1U] == '/' && chk[2U] == '/') {
			/* yep, it's definitely a URI */
			u = _uri;
		} else {
			/* hm, best to prepend file:// then */
			u = _fil;
		}
		archive_string_sprintf(tgt,
			"WARC-Target-URI: %s%s\r\n", u, hdr.tgturi);
	}

	/* record time is usually when the http is sent off,
	 * just treat the archive writing as such for a moment */
	xstrftime(tgt, "WARC-Date: %Y-%m-%dT%H:%M:%SZ\r\n", hdr.rtime);

	/* while we're at it, record the mtime */
	xstrftime(tgt, "Last-Modified: %Y-%m-%dT%H:%M:%SZ\r\n", hdr.mtime);

	if (hdr.recid == NULL) {
		/* generate one, grrrr */
		warc_uuid_t u;

		_gen_uuid(&u);
		/* Unfortunately, archive_string_sprintf does not
		 * handle the minimum number following '%'.
		 * So we have to use snprintf function here instead
		 * of archive_string_snprintf function. */
#if defined(_WIN32) && !defined(__CYGWIN__) && !( defined(_MSC_VER) && _MSC_VER >= 1900)
#define snprintf _snprintf
#endif
		snprintf(
			std_uuid, sizeof(std_uuid),
			"<urn:uuid:%08x-%04x-%04x-%04x-%04x%08x>",
			u.u[0U],
			u.u[1U] >> 16U, u.u[1U] & 0xffffU,
			u.u[2U] >> 16U, u.u[2U] & 0xffffU,
			u.u[3U]);
		hdr.recid = std_uuid;
	}

	/* record-id is mandatory, fingers crossed we won't fail */
	archive_string_sprintf(tgt, "WARC-Record-ID: %s\r\n", hdr.recid);

	if (hdr.cnttyp != NULL) {
		archive_string_sprintf(tgt, "Content-Type: %s\r\n", hdr.cnttyp);
	}

	/* next one is mandatory */
	archive_string_sprintf(tgt, "Content-Length: %ju\r\n", (uintmax_t)hdr.cntlen);
	/**/
	archive_strncat(tgt, "\r\n", 2);

	return (archive_strlen(tgt) >= tsz)? -1: (ssize_t)archive_strlen(tgt);
}

static int
_gen_uuid(warc_uuid_t *tgt)
{
	archive_random(tgt->u, sizeof(tgt->u));
	/* obey uuid version 4 rules */
	tgt->u[1U] &= 0xffff0fffU;
	tgt->u[1U] |= 0x4000U;
	tgt->u[2U] &= 0x3fffffffU;
	tgt->u[2U] |= 0x80000000U;
	return 0;
}

/* archive_write_set_format_warc.c ends here */
