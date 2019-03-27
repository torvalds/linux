/*-
 * Copyright (c) 2009-2012,2014 Michihiro NAKAJIMA
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

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif
#ifdef HAVE_LZMA_H
#include <lzma.h>
#endif
#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif
#ifdef HAVE_LZ4_H
#include <lz4.h>
#endif
#ifdef HAVE_ZSTD_H
#include <zstd.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_string.h"

const char *
archive_version_details(void)
{
	static struct archive_string str;
	static int init = 0;
	const char *zlib = archive_zlib_version();
	const char *liblzma = archive_liblzma_version();
	const char *bzlib = archive_bzlib_version();
	const char *liblz4 = archive_liblz4_version();
	const char *libzstd = archive_libzstd_version();

	if (!init) {
		archive_string_init(&str);

		archive_strcat(&str, ARCHIVE_VERSION_STRING);
		if (zlib != NULL) {
			archive_strcat(&str, " zlib/");
			archive_strcat(&str, zlib);
		}
		if (liblzma) {
			archive_strcat(&str, " liblzma/");
			archive_strcat(&str, liblzma);
		}
		if (bzlib) {
			const char *p = bzlib;
			const char *sep = strchr(p, ',');
			if (sep == NULL)
				sep = p + strlen(p);
			archive_strcat(&str, " bz2lib/");
			archive_strncat(&str, p, sep - p);
		}
		if (liblz4) {
			archive_strcat(&str, " liblz4/");
			archive_strcat(&str, liblz4);
		}
		if (libzstd) {
			archive_strcat(&str, " libzstd/");
			archive_strcat(&str, libzstd);
		}
	}
	return str.s;
}

const char *
archive_zlib_version(void)
{
#ifdef HAVE_ZLIB_H
	return ZLIB_VERSION;
#else
	return NULL;
#endif
}

const char *
archive_liblzma_version(void)
{
#ifdef HAVE_LZMA_H
	return LZMA_VERSION_STRING;
#else
	return NULL;
#endif
}

const char *
archive_bzlib_version(void)
{
#ifdef HAVE_BZLIB_H
	return BZ2_bzlibVersion();
#else
	return NULL;
#endif
}

const char *
archive_liblz4_version(void)
{
#if defined(HAVE_LZ4_H) && defined(HAVE_LIBLZ4)
#define str(s) #s
#define NUMBER(x) str(x)
	return NUMBER(LZ4_VERSION_MAJOR) "." NUMBER(LZ4_VERSION_MINOR) "." NUMBER(LZ4_VERSION_RELEASE);
#undef NUMBER
#undef str
#else
	return NULL;
#endif
}

const char *
archive_libzstd_version(void)
{
#if HAVE_ZSTD_H && HAVE_LIBZSTD
	return ZSTD_VERSION_STRING;
#else
	return NULL;
#endif
}
