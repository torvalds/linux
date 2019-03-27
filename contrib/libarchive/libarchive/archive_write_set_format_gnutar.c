/*-
 * Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Author: Jonas Gastal <jgastal@profusion.mobi>
 * Copyright (c) 2011-2012 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD: head/lib/libarchive/archive_write_set_format_gnu_tar.c 191579 2009-04-27 18:35:03Z gastal $");


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

#include "archive.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_write_private.h"

struct gnutar {
	uint64_t	entry_bytes_remaining;
	uint64_t	entry_padding;
	const char *	linkname;
	size_t		linkname_length;
	const char *	pathname;
	size_t		pathname_length;
	const char *	uname;
	size_t		uname_length;
	const char *	gname;
	size_t		gname_length;
	struct archive_string_conv *opt_sconv;
	struct archive_string_conv *sconv_default;
	int init_default_conversion;
};

/*
 * Define structure of GNU tar header.
 */
#define	GNUTAR_name_offset 0
#define	GNUTAR_name_size 100
#define	GNUTAR_mode_offset 100
#define	GNUTAR_mode_size 7
#define	GNUTAR_mode_max_size 8
#define	GNUTAR_uid_offset 108
#define	GNUTAR_uid_size 7
#define	GNUTAR_uid_max_size 8
#define	GNUTAR_gid_offset 116
#define	GNUTAR_gid_size 7
#define	GNUTAR_gid_max_size 8
#define	GNUTAR_size_offset 124
#define	GNUTAR_size_size 11
#define	GNUTAR_size_max_size 12
#define	GNUTAR_mtime_offset 136
#define	GNUTAR_mtime_size 11
#define	GNUTAR_mtime_max_size 11
#define	GNUTAR_checksum_offset 148
#define	GNUTAR_checksum_size 8
#define	GNUTAR_typeflag_offset 156
#define	GNUTAR_typeflag_size 1
#define	GNUTAR_linkname_offset 157
#define	GNUTAR_linkname_size 100
#define	GNUTAR_magic_offset 257
#define	GNUTAR_magic_size 6
#define	GNUTAR_version_offset 263
#define	GNUTAR_version_size 2
#define	GNUTAR_uname_offset 265
#define	GNUTAR_uname_size 32
#define	GNUTAR_gname_offset 297
#define	GNUTAR_gname_size 32
#define	GNUTAR_rdevmajor_offset 329
#define	GNUTAR_rdevmajor_size 6
#define	GNUTAR_rdevmajor_max_size 8
#define	GNUTAR_rdevminor_offset 337
#define	GNUTAR_rdevminor_size 6
#define	GNUTAR_rdevminor_max_size 8

/*
 * A filled-in copy of the header for initialization.
 */
static const char template_header[] = {
	/* name: 100 bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,
	/* Mode, null termination: 8 bytes */
	'0','0','0','0','0','0', '0','\0',
	/* uid, null termination: 8 bytes */
	'0','0','0','0','0','0', '0','\0',
	/* gid, null termination: 8 bytes */
	'0','0','0','0','0','0', '0','\0',
	/* size, space termination: 12 bytes */
	'0','0','0','0','0','0','0','0','0','0','0', '\0',
	/* mtime, space termination: 12 bytes */
	'0','0','0','0','0','0','0','0','0','0','0', '\0',
	/* Initial checksum value: 8 spaces */
	' ',' ',' ',' ',' ',' ',' ',' ',
	/* Typeflag: 1 byte */
	'0',			/* '0' = regular file */
	/* Linkname: 100 bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,
	/* Magic: 8 bytes */
	'u','s','t','a','r',' ', ' ','\0',
	/* Uname: 32 bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	/* Gname: 32 bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	/* rdevmajor + null padding: 8 bytes */
	'\0','\0','\0','\0','\0','\0', '\0','\0',
	/* rdevminor + null padding: 8 bytes */
	'\0','\0','\0','\0','\0','\0', '\0','\0',
	/* Padding: 167 bytes */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0
};

static int      archive_write_gnutar_options(struct archive_write *,
		    const char *, const char *);
static int	archive_format_gnutar_header(struct archive_write *, char h[512],
		    struct archive_entry *, int tartype);
static int      archive_write_gnutar_header(struct archive_write *,
		    struct archive_entry *entry);
static ssize_t	archive_write_gnutar_data(struct archive_write *a, const void *buff,
		    size_t s);
static int	archive_write_gnutar_free(struct archive_write *);
static int	archive_write_gnutar_close(struct archive_write *);
static int	archive_write_gnutar_finish_entry(struct archive_write *);
static int	format_256(int64_t, char *, int);
static int	format_number(int64_t, char *, int size, int maxsize);
static int	format_octal(int64_t, char *, int);

/*
 * Set output format to 'GNU tar' format.
 */
int
archive_write_set_format_gnutar(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct gnutar *gnutar;

	gnutar = (struct gnutar *)calloc(1, sizeof(*gnutar));
	if (gnutar == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate gnutar data");
		return (ARCHIVE_FATAL);
	}
	a->format_data = gnutar;
	a->format_name = "gnutar";
	a->format_options = archive_write_gnutar_options;
	a->format_write_header = archive_write_gnutar_header;
	a->format_write_data = archive_write_gnutar_data;
	a->format_close = archive_write_gnutar_close;
	a->format_free = archive_write_gnutar_free;
	a->format_finish_entry = archive_write_gnutar_finish_entry;
	a->archive.archive_format = ARCHIVE_FORMAT_TAR_GNUTAR;
	a->archive.archive_format_name = "GNU tar";
	return (ARCHIVE_OK);
}

static int
archive_write_gnutar_options(struct archive_write *a, const char *key,
    const char *val)
{
	struct gnutar *gnutar = (struct gnutar *)a->format_data;
	int ret = ARCHIVE_FAILED;

	if (strcmp(key, "hdrcharset")  == 0) {
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "%s: hdrcharset option needs a character-set name",
			    a->format_name);
		else {
			gnutar->opt_sconv = archive_string_conversion_to_charset(
			    &a->archive, val, 0);
			if (gnutar->opt_sconv != NULL)
				ret = ARCHIVE_OK;
			else
				ret = ARCHIVE_FATAL;
		}
		return (ret);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
archive_write_gnutar_close(struct archive_write *a)
{
	return (__archive_write_nulls(a, 512*2));
}

static int
archive_write_gnutar_free(struct archive_write *a)
{
	struct gnutar *gnutar;

	gnutar = (struct gnutar *)a->format_data;
	free(gnutar);
	a->format_data = NULL;
	return (ARCHIVE_OK);
}

static int
archive_write_gnutar_finish_entry(struct archive_write *a)
{
	struct gnutar *gnutar;
	int ret;

	gnutar = (struct gnutar *)a->format_data;
	ret = __archive_write_nulls(a, (size_t)
	    (gnutar->entry_bytes_remaining + gnutar->entry_padding));
	gnutar->entry_bytes_remaining = gnutar->entry_padding = 0;
	return (ret);
}

static ssize_t
archive_write_gnutar_data(struct archive_write *a, const void *buff, size_t s)
{
	struct gnutar *gnutar;
	int ret;

	gnutar = (struct gnutar *)a->format_data;
	if (s > gnutar->entry_bytes_remaining)
		s = (size_t)gnutar->entry_bytes_remaining;
	ret = __archive_write_output(a, buff, s);
	gnutar->entry_bytes_remaining -= s;
	if (ret != ARCHIVE_OK)
		return (ret);
	return (s);
}

static int
archive_write_gnutar_header(struct archive_write *a,
     struct archive_entry *entry)
{
	char buff[512];
	int r, ret, ret2 = ARCHIVE_OK;
	int tartype;
	struct gnutar *gnutar;
	struct archive_string_conv *sconv;
	struct archive_entry *entry_main;

	gnutar = (struct gnutar *)a->format_data;

	/* Setup default string conversion. */
	if (gnutar->opt_sconv == NULL) {
		if (!gnutar->init_default_conversion) {
			gnutar->sconv_default =
			    archive_string_default_conversion_for_write(
				&(a->archive));
			gnutar->init_default_conversion = 1;
		}
		sconv = gnutar->sconv_default;
	} else
		sconv = gnutar->opt_sconv;

	/* Only regular files (not hardlinks) have data. */
	if (archive_entry_hardlink(entry) != NULL ||
	    archive_entry_symlink(entry) != NULL ||
	    !(archive_entry_filetype(entry) == AE_IFREG))
		archive_entry_set_size(entry, 0);

	if (AE_IFDIR == archive_entry_filetype(entry)) {
		const char *p;
		size_t path_length;
		/*
		 * Ensure a trailing '/'.  Modify the entry so
		 * the client sees the change.
		 */
#if defined(_WIN32) && !defined(__CYGWIN__)
		const wchar_t *wp;

		wp = archive_entry_pathname_w(entry);
		if (wp != NULL && wp[wcslen(wp) -1] != L'/') {
			struct archive_wstring ws;

			archive_string_init(&ws);
			path_length = wcslen(wp);
			if (archive_wstring_ensure(&ws,
			    path_length + 2) == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate ustar data");
				archive_wstring_free(&ws);
				return(ARCHIVE_FATAL);
			}
			/* Should we keep '\' ? */
			if (wp[path_length -1] == L'\\')
				path_length--;
			archive_wstrncpy(&ws, wp, path_length);
			archive_wstrappend_wchar(&ws, L'/');
			archive_entry_copy_pathname_w(entry, ws.s);
			archive_wstring_free(&ws);
			p = NULL;
		} else
#endif
			p = archive_entry_pathname(entry);
		/*
		 * On Windows, this is a backup operation just in
		 * case getting WCS failed. On POSIX, this is a
		 * normal operation.
		 */
		if (p != NULL && p[0] != '\0' && p[strlen(p) - 1] != '/') {
			struct archive_string as;

			archive_string_init(&as);
			path_length = strlen(p);
			if (archive_string_ensure(&as,
			    path_length + 2) == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate ustar data");
				archive_string_free(&as);
				return(ARCHIVE_FATAL);
			}
#if defined(_WIN32) && !defined(__CYGWIN__)
			/* NOTE: This might break the pathname
			 * if the current code page is CP932 and
			 * the pathname includes a character '\'
			 * as a part of its multibyte pathname. */
			if (p[strlen(p) -1] == '\\')
				path_length--;
			else
#endif
			archive_strncpy(&as, p, path_length);
			archive_strappend_char(&as, '/');
			archive_entry_copy_pathname(entry, as.s);
			archive_string_free(&as);
		}
	}

#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Make sure the path separators in pathname, hardlink and symlink
	 * are all slash '/', not the Windows path separator '\'. */
	entry_main = __la_win_entry_in_posix_pathseparator(entry);
	if (entry_main == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate ustar data");
		return(ARCHIVE_FATAL);
	}
	if (entry != entry_main)
		entry = entry_main;
	else
		entry_main = NULL;
#else
	entry_main = NULL;
#endif
	r = archive_entry_pathname_l(entry, &(gnutar->pathname),
	    &(gnutar->pathname_length), sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathame");
			ret = ARCHIVE_FATAL;
			goto exit_write_header;
		}
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate pathname '%s' to %s",
		    archive_entry_pathname(entry),
		    archive_string_conversion_charset_name(sconv));
		ret2 = ARCHIVE_WARN;
	}
	r = archive_entry_uname_l(entry, &(gnutar->uname),
	    &(gnutar->uname_length), sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Uname");
			ret = ARCHIVE_FATAL;
			goto exit_write_header;
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate uname '%s' to %s",
		    archive_entry_uname(entry),
		    archive_string_conversion_charset_name(sconv));
		ret2 = ARCHIVE_WARN;
	}
	r = archive_entry_gname_l(entry, &(gnutar->gname),
	    &(gnutar->gname_length), sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Gname");
			ret = ARCHIVE_FATAL;
			goto exit_write_header;
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate gname '%s' to %s",
		    archive_entry_gname(entry),
		    archive_string_conversion_charset_name(sconv));
		ret2 = ARCHIVE_WARN;
	}

	/* If linkname is longer than 100 chars we need to add a 'K' header. */
	r = archive_entry_hardlink_l(entry, &(gnutar->linkname),
	    &(gnutar->linkname_length), sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Linkname");
			ret = ARCHIVE_FATAL;
			goto exit_write_header;
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate linkname '%s' to %s",
		    archive_entry_hardlink(entry),
		    archive_string_conversion_charset_name(sconv));
		ret2 = ARCHIVE_WARN;
	}
	if (gnutar->linkname_length == 0) {
		r = archive_entry_symlink_l(entry, &(gnutar->linkname),
		    &(gnutar->linkname_length), sconv);
		if (r != 0) {
			if (errno == ENOMEM) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory for Linkname");
				ret = ARCHIVE_FATAL;
				goto exit_write_header;
			}
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Can't translate linkname '%s' to %s",
			    archive_entry_hardlink(entry),
			    archive_string_conversion_charset_name(sconv));
			ret2 = ARCHIVE_WARN;
		}
	}
	if (gnutar->linkname_length > GNUTAR_linkname_size) {
		size_t length = gnutar->linkname_length + 1;
		struct archive_entry *temp = archive_entry_new2(&a->archive);

		/* Uname/gname here don't really matter since no one reads them;
		 * these are the values that GNU tar happens to use on FreeBSD. */
		archive_entry_set_uname(temp, "root");
		archive_entry_set_gname(temp, "wheel");

		archive_entry_set_pathname(temp, "././@LongLink");
		archive_entry_set_size(temp, length);
		ret = archive_format_gnutar_header(a, buff, temp, 'K');
		archive_entry_free(temp);
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
		ret = __archive_write_output(a, buff, 512);
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
		/* Write name and trailing null byte. */
		ret = __archive_write_output(a, gnutar->linkname, length);
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
		/* Pad to 512 bytes */
		ret = __archive_write_nulls(a, 0x1ff & (-(ssize_t)length));
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
	}

	/* If pathname is longer than 100 chars we need to add an 'L' header. */
	if (gnutar->pathname_length > GNUTAR_name_size) {
		const char *pathname = gnutar->pathname;
		size_t length = gnutar->pathname_length + 1;
		struct archive_entry *temp = archive_entry_new2(&a->archive);

		/* Uname/gname here don't really matter since no one reads them;
		 * these are the values that GNU tar happens to use on FreeBSD. */
		archive_entry_set_uname(temp, "root");
		archive_entry_set_gname(temp, "wheel");

		archive_entry_set_pathname(temp, "././@LongLink");
		archive_entry_set_size(temp, length);
		ret = archive_format_gnutar_header(a, buff, temp, 'L');
		archive_entry_free(temp);
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
		ret = __archive_write_output(a, buff, 512);
		if(ret < ARCHIVE_WARN)
			goto exit_write_header;
		/* Write pathname + trailing null byte. */
		ret = __archive_write_output(a, pathname, length);
		if(ret < ARCHIVE_WARN)
			goto exit_write_header;
		/* Pad to multiple of 512 bytes. */
		ret = __archive_write_nulls(a, 0x1ff & (-(ssize_t)length));
		if (ret < ARCHIVE_WARN)
			goto exit_write_header;
	}

	if (archive_entry_hardlink(entry) != NULL) {
		tartype = '1';
	} else
		switch (archive_entry_filetype(entry)) {
		case AE_IFREG: tartype = '0' ; break;
		case AE_IFLNK: tartype = '2' ; break;
		case AE_IFCHR: tartype = '3' ; break;
		case AE_IFBLK: tartype = '4' ; break;
		case AE_IFDIR: tartype = '5' ; break;
		case AE_IFIFO: tartype = '6' ; break;
		case AE_IFSOCK:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "tar format cannot archive socket");
			ret = ARCHIVE_FAILED;
			goto exit_write_header;
		default:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "tar format cannot archive this (mode=0%lo)",
			    (unsigned long)archive_entry_mode(entry));
			ret = ARCHIVE_FAILED;
			goto exit_write_header;
		}

	ret = archive_format_gnutar_header(a, buff, entry, tartype);
	if (ret < ARCHIVE_WARN)
		goto exit_write_header;
	if (ret2 < ret)
		ret = ret2;
	ret2 = __archive_write_output(a, buff, 512);
	if (ret2 < ARCHIVE_WARN) {
		ret = ret2;
		goto exit_write_header;
	}
	if (ret2 < ret)
		ret = ret2;

	gnutar->entry_bytes_remaining = archive_entry_size(entry);
	gnutar->entry_padding = 0x1ff & (-(int64_t)gnutar->entry_bytes_remaining);
exit_write_header:
	archive_entry_free(entry_main);
	return (ret);
}

static int
archive_format_gnutar_header(struct archive_write *a, char h[512],
    struct archive_entry *entry, int tartype)
{
	unsigned int checksum;
	int i, ret;
	size_t copy_length;
	const char *p;
	struct gnutar *gnutar;

	gnutar = (struct gnutar *)a->format_data;

	ret = 0;

	/*
	 * The "template header" already includes the signature,
	 * various end-of-field markers, and other required elements.
	 */
	memcpy(h, &template_header, 512);

	/*
	 * Because the block is already null-filled, and strings
	 * are allowed to exactly fill their destination (without null),
	 * I use memcpy(dest, src, strlen()) here a lot to copy strings.
	 */

	if (tartype == 'K' || tartype == 'L') {
		p = archive_entry_pathname(entry);
		copy_length = strlen(p);
	} else {
		p = gnutar->pathname;
		copy_length = gnutar->pathname_length;
	}
	if (copy_length > GNUTAR_name_size)
		copy_length = GNUTAR_name_size;
	memcpy(h + GNUTAR_name_offset, p, copy_length);

	if ((copy_length = gnutar->linkname_length) > 0) {
		if (copy_length > GNUTAR_linkname_size)
			copy_length = GNUTAR_linkname_size;
		memcpy(h + GNUTAR_linkname_offset, gnutar->linkname,
		    copy_length);
	}

	/* TODO: How does GNU tar handle unames longer than GNUTAR_uname_size? */
	if (tartype == 'K' || tartype == 'L') {
		p = archive_entry_uname(entry);
		copy_length = strlen(p);
	} else {
		p = gnutar->uname;
		copy_length = gnutar->uname_length;
	}
	if (copy_length > 0) {
		if (copy_length > GNUTAR_uname_size)
			copy_length = GNUTAR_uname_size;
		memcpy(h + GNUTAR_uname_offset, p, copy_length);
	}

	/* TODO: How does GNU tar handle gnames longer than GNUTAR_gname_size? */
	if (tartype == 'K' || tartype == 'L') {
		p = archive_entry_gname(entry);
		copy_length = strlen(p);
	} else {
		p = gnutar->gname;
		copy_length = gnutar->gname_length;
	}
	if (copy_length > 0) {
		if (strlen(p) > GNUTAR_gname_size)
			copy_length = GNUTAR_gname_size;
		memcpy(h + GNUTAR_gname_offset, p, copy_length);
	}

	/* By truncating the mode here, we ensure it always fits. */
	format_octal(archive_entry_mode(entry) & 07777,
	    h + GNUTAR_mode_offset, GNUTAR_mode_size);

	/* GNU tar supports base-256 here, so should never overflow. */
	if (format_number(archive_entry_uid(entry), h + GNUTAR_uid_offset,
		GNUTAR_uid_size, GNUTAR_uid_max_size)) {
		archive_set_error(&a->archive, ERANGE,
		    "Numeric user ID %jd too large",
		    (intmax_t)archive_entry_uid(entry));
		ret = ARCHIVE_FAILED;
	}

	/* GNU tar supports base-256 here, so should never overflow. */
	if (format_number(archive_entry_gid(entry), h + GNUTAR_gid_offset,
		GNUTAR_gid_size, GNUTAR_gid_max_size)) {
		archive_set_error(&a->archive, ERANGE,
		    "Numeric group ID %jd too large",
		    (intmax_t)archive_entry_gid(entry));
		ret = ARCHIVE_FAILED;
	}

	/* GNU tar supports base-256 here, so should never overflow. */
	if (format_number(archive_entry_size(entry), h + GNUTAR_size_offset,
		GNUTAR_size_size, GNUTAR_size_max_size)) {
		archive_set_error(&a->archive, ERANGE,
		    "File size out of range");
		ret = ARCHIVE_FAILED;
	}

	/* Shouldn't overflow before 2106, since mtime field is 33 bits. */
	format_octal(archive_entry_mtime(entry),
	    h + GNUTAR_mtime_offset, GNUTAR_mtime_size);

	if (archive_entry_filetype(entry) == AE_IFBLK
	    || archive_entry_filetype(entry) == AE_IFCHR) {
		if (format_octal(archive_entry_rdevmajor(entry),
		    h + GNUTAR_rdevmajor_offset,
			GNUTAR_rdevmajor_size)) {
			archive_set_error(&a->archive, ERANGE,
			    "Major device number too large");
			ret = ARCHIVE_FAILED;
		}

		if (format_octal(archive_entry_rdevminor(entry),
		    h + GNUTAR_rdevminor_offset,
			GNUTAR_rdevminor_size)) {
			archive_set_error(&a->archive, ERANGE,
			    "Minor device number too large");
			ret = ARCHIVE_FAILED;
		}
	}

	h[GNUTAR_typeflag_offset] = tartype;

	checksum = 0;
	for (i = 0; i < 512; i++)
		checksum += 255 & (unsigned int)h[i];
	h[GNUTAR_checksum_offset + 6] = '\0'; /* Can't be pre-set in the template. */
	/* h[GNUTAR_checksum_offset + 7] = ' '; */ /* This is pre-set in the template. */
	format_octal(checksum, h + GNUTAR_checksum_offset, 6);
	return (ret);
}

/*
 * Format a number into a field, falling back to base-256 if necessary.
 */
static int
format_number(int64_t v, char *p, int s, int maxsize)
{
	int64_t limit = ((int64_t)1 << (s*3));

	if (v < limit)
		return (format_octal(v, p, s));
	return (format_256(v, p, maxsize));
}

/*
 * Format a number into the specified field using base-256.
 */
static int
format_256(int64_t v, char *p, int s)
{
	p += s;
	while (s-- > 0) {
		*--p = (char)(v & 0xff);
		v >>= 8;
	}
	*p |= 0x80; /* Set the base-256 marker bit. */
	return (0);
}

/*
 * Format a number into the specified field using octal.
 */
static int
format_octal(int64_t v, char *p, int s)
{
	int len = s;

	/* Octal values can't be negative, so use 0. */
	if (v < 0)
		v = 0;

	p += s;		/* Start at the end and work backwards. */
	while (s-- > 0) {
		*--p = (char)('0' + (v & 7));
		v >>= 3;
	}

	if (v == 0)
		return (0);

	/* If it overflowed, fill field with max value. */
	while (len-- > 0)
		*p++ = '7';

	return (-1);
}
