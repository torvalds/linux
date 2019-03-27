/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2010-2012 Michihiro NAKAJIMA
 * Copyright (c) 2016 Martin Matuska
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

struct sparse_block {
	struct sparse_block	*next;
	int		is_hole;
	uint64_t	offset;
	uint64_t	remaining;
};

struct pax {
	uint64_t	entry_bytes_remaining;
	uint64_t	entry_padding;
	struct archive_string	l_url_encoded_name;
	struct archive_string	pax_header;
	struct archive_string	sparse_map;
	size_t			sparse_map_padding;
	struct sparse_block	*sparse_list;
	struct sparse_block	*sparse_tail;
	struct archive_string_conv *sconv_utf8;
	int			 opt_binary;

	unsigned flags;
#define WRITE_SCHILY_XATTR       (1 << 0)
#define WRITE_LIBARCHIVE_XATTR   (1 << 1)
};

static void		 add_pax_attr(struct archive_string *, const char *key,
			     const char *value);
static void		 add_pax_attr_binary(struct archive_string *,
			     const char *key,
			     const char *value, size_t value_len);
static void		 add_pax_attr_int(struct archive_string *,
			     const char *key, int64_t value);
static void		 add_pax_attr_time(struct archive_string *,
			     const char *key, int64_t sec,
			     unsigned long nanos);
static int		 add_pax_acl(struct archive_write *,
			    struct archive_entry *, struct pax *, int);
static ssize_t		 archive_write_pax_data(struct archive_write *,
			     const void *, size_t);
static int		 archive_write_pax_close(struct archive_write *);
static int		 archive_write_pax_free(struct archive_write *);
static int		 archive_write_pax_finish_entry(struct archive_write *);
static int		 archive_write_pax_header(struct archive_write *,
			     struct archive_entry *);
static int		 archive_write_pax_options(struct archive_write *,
			     const char *, const char *);
static char		*base64_encode(const char *src, size_t len);
static char		*build_gnu_sparse_name(char *dest, const char *src);
static char		*build_pax_attribute_name(char *dest, const char *src);
static char		*build_ustar_entry_name(char *dest, const char *src,
			     size_t src_length, const char *insert);
static char		*format_int(char *dest, int64_t);
static int		 has_non_ASCII(const char *);
static void		 sparse_list_clear(struct pax *);
static int		 sparse_list_add(struct pax *, int64_t, int64_t);
static char		*url_encode(const char *in);

/*
 * Set output format to 'restricted pax' format.
 *
 * This is the same as normal 'pax', but tries to suppress
 * the pax header whenever possible.  This is the default for
 * bsdtar, for instance.
 */
int
archive_write_set_format_pax_restricted(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	int r;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_set_format_pax_restricted");

	r = archive_write_set_format_pax(&a->archive);
	a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED;
	a->archive.archive_format_name = "restricted POSIX pax interchange";
	return (r);
}

/*
 * Set output format to 'pax' format.
 */
int
archive_write_set_format_pax(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct pax *pax;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_set_format_pax");

	if (a->format_free != NULL)
		(a->format_free)(a);

	pax = (struct pax *)calloc(1, sizeof(*pax));
	if (pax == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate pax data");
		return (ARCHIVE_FATAL);
	}
	pax->flags = WRITE_LIBARCHIVE_XATTR | WRITE_SCHILY_XATTR;

	a->format_data = pax;
	a->format_name = "pax";
	a->format_options = archive_write_pax_options;
	a->format_write_header = archive_write_pax_header;
	a->format_write_data = archive_write_pax_data;
	a->format_close = archive_write_pax_close;
	a->format_free = archive_write_pax_free;
	a->format_finish_entry = archive_write_pax_finish_entry;
	a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
	a->archive.archive_format_name = "POSIX pax interchange";
	return (ARCHIVE_OK);
}

static int
archive_write_pax_options(struct archive_write *a, const char *key,
    const char *val)
{
	struct pax *pax = (struct pax *)a->format_data;
	int ret = ARCHIVE_FAILED;

	if (strcmp(key, "hdrcharset")  == 0) {
		/*
		 * The character-set we can use are defined in
		 * IEEE Std 1003.1-2001
		 */
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "pax: hdrcharset option needs a character-set name");
		else if (strcmp(val, "BINARY") == 0 ||
		    strcmp(val, "binary") == 0) {
			/*
			 * Specify binary mode. We will not convert
			 * filenames, uname and gname to any charsets.
			 */
			pax->opt_binary = 1;
			ret = ARCHIVE_OK;
		} else if (strcmp(val, "UTF-8") == 0) {
			/*
			 * Specify UTF-8 character-set to be used for
			 * filenames. This is almost the test that
			 * running platform supports the string conversion.
			 * Especially libarchive_test needs this trick for
			 * its test.
			 */
			pax->sconv_utf8 = archive_string_conversion_to_charset(
			    &(a->archive), "UTF-8", 0);
			if (pax->sconv_utf8 == NULL)
				ret = ARCHIVE_FATAL;
			else
				ret = ARCHIVE_OK;
		} else
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "pax: invalid charset name");
		return (ret);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

/*
 * Note: This code assumes that 'nanos' has the same sign as 'sec',
 * which implies that sec=-1, nanos=200000000 represents -1.2 seconds
 * and not -0.8 seconds.  This is a pretty pedantic point, as we're
 * unlikely to encounter many real files created before Jan 1, 1970,
 * much less ones with timestamps recorded to sub-second resolution.
 */
static void
add_pax_attr_time(struct archive_string *as, const char *key,
    int64_t sec, unsigned long nanos)
{
	int digit, i;
	char *t;
	/*
	 * Note that each byte contributes fewer than 3 base-10
	 * digits, so this will always be big enough.
	 */
	char tmp[1 + 3*sizeof(sec) + 1 + 3*sizeof(nanos)];

	tmp[sizeof(tmp) - 1] = 0;
	t = tmp + sizeof(tmp) - 1;

	/* Skip trailing zeros in the fractional part. */
	for (digit = 0, i = 10; i > 0 && digit == 0; i--) {
		digit = nanos % 10;
		nanos /= 10;
	}

	/* Only format the fraction if it's non-zero. */
	if (i > 0) {
		while (i > 0) {
			*--t = "0123456789"[digit];
			digit = nanos % 10;
			nanos /= 10;
			i--;
		}
		*--t = '.';
	}
	t = format_int(t, sec);

	add_pax_attr(as, key, t);
}

static char *
format_int(char *t, int64_t i)
{
	uint64_t ui;

	if (i < 0) 
		ui = (i == INT64_MIN) ? (uint64_t)(INT64_MAX) + 1 : (uint64_t)(-i);
	else
		ui = i;

	do {
		*--t = "0123456789"[ui % 10];
	} while (ui /= 10);
	if (i < 0)
		*--t = '-';
	return (t);
}

static void
add_pax_attr_int(struct archive_string *as, const char *key, int64_t value)
{
	char tmp[1 + 3 * sizeof(value)];

	tmp[sizeof(tmp) - 1] = 0;
	add_pax_attr(as, key, format_int(tmp + sizeof(tmp) - 1, value));
}

/*
 * Add a key/value attribute to the pax header.  This function handles
 * the length field and various other syntactic requirements.
 */
static void
add_pax_attr(struct archive_string *as, const char *key, const char *value)
{
	add_pax_attr_binary(as, key, value, strlen(value));
}

/*
 * Add a key/value attribute to the pax header.  This function handles
 * binary values.
 */
static void
add_pax_attr_binary(struct archive_string *as, const char *key,
		    const char *value, size_t value_len)
{
	int digits, i, len, next_ten;
	char tmp[1 + 3 * sizeof(int)];	/* < 3 base-10 digits per byte */

	/*-
	 * PAX attributes have the following layout:
	 *     <len> <space> <key> <=> <value> <nl>
	 */
	len = 1 + (int)strlen(key) + 1 + (int)value_len + 1;

	/*
	 * The <len> field includes the length of the <len> field, so
	 * computing the correct length is tricky.  I start by
	 * counting the number of base-10 digits in 'len' and
	 * computing the next higher power of 10.
	 */
	next_ten = 1;
	digits = 0;
	i = len;
	while (i > 0) {
		i = i / 10;
		digits++;
		next_ten = next_ten * 10;
	}
	/*
	 * For example, if string without the length field is 99
	 * chars, then adding the 2 digit length "99" will force the
	 * total length past 100, requiring an extra digit.  The next
	 * statement adjusts for this effect.
	 */
	if (len + digits >= next_ten)
		digits++;

	/* Now, we have the right length so we can build the line. */
	tmp[sizeof(tmp) - 1] = 0;	/* Null-terminate the work area. */
	archive_strcat(as, format_int(tmp + sizeof(tmp) - 1, len + digits));
	archive_strappend_char(as, ' ');
	archive_strcat(as, key);
	archive_strappend_char(as, '=');
	archive_array_append(as, value, value_len);
	archive_strappend_char(as, '\n');
}

static void
archive_write_pax_header_xattr(struct pax *pax, const char *encoded_name,
    const void *value, size_t value_len)
{
	struct archive_string s;
	char *encoded_value;

	if (pax->flags & WRITE_LIBARCHIVE_XATTR) {
		encoded_value = base64_encode((const char *)value, value_len);

		if (encoded_name != NULL && encoded_value != NULL) {
			archive_string_init(&s);
			archive_strcpy(&s, "LIBARCHIVE.xattr.");
			archive_strcat(&s, encoded_name);
			add_pax_attr(&(pax->pax_header), s.s, encoded_value);
			archive_string_free(&s);
		}
		free(encoded_value);
	}
	if (pax->flags & WRITE_SCHILY_XATTR) {
		archive_string_init(&s);
		archive_strcpy(&s, "SCHILY.xattr.");
		archive_strcat(&s, encoded_name);
		add_pax_attr_binary(&(pax->pax_header), s.s, value, value_len);
		archive_string_free(&s);
	}
}

static int
archive_write_pax_header_xattrs(struct archive_write *a,
    struct pax *pax, struct archive_entry *entry)
{
	int i = archive_entry_xattr_reset(entry);

	while (i--) {
		const char *name;
		const void *value;
		char *url_encoded_name = NULL, *encoded_name = NULL;
		size_t size;
		int r;

		archive_entry_xattr_next(entry, &name, &value, &size);
		url_encoded_name = url_encode(name);
		if (url_encoded_name != NULL) {
			/* Convert narrow-character to UTF-8. */
			r = archive_strcpy_l(&(pax->l_url_encoded_name),
			    url_encoded_name, pax->sconv_utf8);
			free(url_encoded_name); /* Done with this. */
			if (r == 0)
				encoded_name = pax->l_url_encoded_name.s;
			else if (errno == ENOMEM) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory for Linkname");
				return (ARCHIVE_FATAL);
			}
		}

		archive_write_pax_header_xattr(pax, encoded_name,
		    value, size);

	}
	return (ARCHIVE_OK);
}

static int
get_entry_hardlink(struct archive_write *a, struct archive_entry *entry,
    const char **name, size_t *length, struct archive_string_conv *sc)
{
	int r;
	
	r = archive_entry_hardlink_l(entry, name, length, sc);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Linkname");
			return (ARCHIVE_FATAL);
		}
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

static int
get_entry_pathname(struct archive_write *a, struct archive_entry *entry,
    const char **name, size_t *length, struct archive_string_conv *sc)
{
	int r;

	r = archive_entry_pathname_l(entry, name, length, sc);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathname");
			return (ARCHIVE_FATAL);
		}
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

static int
get_entry_uname(struct archive_write *a, struct archive_entry *entry,
    const char **name, size_t *length, struct archive_string_conv *sc)
{
	int r;

	r = archive_entry_uname_l(entry, name, length, sc);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Uname");
			return (ARCHIVE_FATAL);
		}
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

static int
get_entry_gname(struct archive_write *a, struct archive_entry *entry,
    const char **name, size_t *length, struct archive_string_conv *sc)
{
	int r;

	r = archive_entry_gname_l(entry, name, length, sc);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Gname");
			return (ARCHIVE_FATAL);
		}
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

static int
get_entry_symlink(struct archive_write *a, struct archive_entry *entry,
    const char **name, size_t *length, struct archive_string_conv *sc)
{
	int r;

	r = archive_entry_symlink_l(entry, name, length, sc);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Linkname");
			return (ARCHIVE_FATAL);
		}
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

/* Add ACL to pax header */
static int
add_pax_acl(struct archive_write *a,
    struct archive_entry *entry, struct pax *pax, int flags)
{
	char *p;
	const char *attr;
	int acl_types;

	acl_types = archive_entry_acl_types(entry);

	if ((acl_types & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0)
		attr = "SCHILY.acl.ace";
	else if ((flags & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0)
		attr = "SCHILY.acl.access";
	else if ((flags & ARCHIVE_ENTRY_ACL_TYPE_DEFAULT) != 0)
		attr = "SCHILY.acl.default";
	else
		return (ARCHIVE_FATAL);

	p = archive_entry_acl_to_text_l(entry, NULL, flags, pax->sconv_utf8);
	if (p == NULL) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM, "%s %s",
			    "Can't allocate memory for ", attr);
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT, "%s %s %s",
		    "Can't translate ", attr, " to UTF-8");
		return(ARCHIVE_WARN);
	}

	if (*p != '\0') {
		add_pax_attr(&(pax->pax_header),
		    attr, p);
	}
	free(p);
	return(ARCHIVE_OK);
}

/*
 * TODO: Consider adding 'comment' and 'charset' fields to
 * archive_entry so that clients can specify them.  Also, consider
 * adding generic key/value tags so clients can add arbitrary
 * key/value data.
 *
 * TODO: Break up this 700-line function!!!!  Yowza!
 */
static int
archive_write_pax_header(struct archive_write *a,
    struct archive_entry *entry_original)
{
	struct archive_entry *entry_main;
	const char *p;
	const char *suffix;
	int need_extension, r, ret;
	int acl_types;
	int sparse_count;
	uint64_t sparse_total, real_size;
	struct pax *pax;
	const char *hardlink;
	const char *path = NULL, *linkpath = NULL;
	const char *uname = NULL, *gname = NULL;
	const void *mac_metadata;
	size_t mac_metadata_size;
	struct archive_string_conv *sconv;
	size_t hardlink_length, path_length, linkpath_length;
	size_t uname_length, gname_length;

	char paxbuff[512];
	char ustarbuff[512];
	char ustar_entry_name[256];
	char pax_entry_name[256];
	char gnu_sparse_name[256];
	struct archive_string entry_name;

	ret = ARCHIVE_OK;
	need_extension = 0;
	pax = (struct pax *)a->format_data;

	/* Sanity check. */
	if (archive_entry_pathname(entry_original) == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			  "Can't record entry in tar file without pathname");
		return (ARCHIVE_FAILED);
	}

	/*
	 * Choose a header encoding.
	 */
	if (pax->opt_binary)
		sconv = NULL;/* Binary mode. */
	else {
		/* Header encoding is UTF-8. */
		if (pax->sconv_utf8 == NULL) {
			/* Initialize the string conversion object
			 * we must need */
			pax->sconv_utf8 = archive_string_conversion_to_charset(
			    &(a->archive), "UTF-8", 1);
			if (pax->sconv_utf8 == NULL)
				/* Couldn't allocate memory */
				return (ARCHIVE_FAILED);
		}
		sconv = pax->sconv_utf8;
	}

	r = get_entry_hardlink(a, entry_original, &hardlink,
	    &hardlink_length, sconv);
	if (r == ARCHIVE_FATAL)
		return (r);
	else if (r != ARCHIVE_OK) {
		r = get_entry_hardlink(a, entry_original, &hardlink,
		    &hardlink_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate linkname '%s' to %s", hardlink,
		    archive_string_conversion_charset_name(sconv));
		ret = ARCHIVE_WARN;
		sconv = NULL;/* The header charset switches to binary mode. */
	}

	/* Make sure this is a type of entry that we can handle here */
	if (hardlink == NULL) {
		switch (archive_entry_filetype(entry_original)) {
		case AE_IFBLK:
		case AE_IFCHR:
		case AE_IFIFO:
		case AE_IFLNK:
		case AE_IFREG:
			break;
		case AE_IFDIR:
		{
			/*
			 * Ensure a trailing '/'.  Modify the original
			 * entry so the client sees the change.
			 */
#if defined(_WIN32) && !defined(__CYGWIN__)
			const wchar_t *wp;

			wp = archive_entry_pathname_w(entry_original);
			if (wp != NULL && wp[wcslen(wp) -1] != L'/') {
				struct archive_wstring ws;

				archive_string_init(&ws);
				path_length = wcslen(wp);
				if (archive_wstring_ensure(&ws,
				    path_length + 2) == NULL) {
					archive_set_error(&a->archive, ENOMEM,
					    "Can't allocate pax data");
					archive_wstring_free(&ws);
					return(ARCHIVE_FATAL);
				}
				/* Should we keep '\' ? */
				if (wp[path_length -1] == L'\\')
					path_length--;
				archive_wstrncpy(&ws, wp, path_length);
				archive_wstrappend_wchar(&ws, L'/');
				archive_entry_copy_pathname_w(
				    entry_original, ws.s);
				archive_wstring_free(&ws);
				p = NULL;
			} else
#endif
				p = archive_entry_pathname(entry_original);
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
					    "Can't allocate pax data");
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
				archive_entry_copy_pathname(
				    entry_original, as.s);
				archive_string_free(&as);
			}
			break;
		}
		case AE_IFSOCK:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "tar format cannot archive socket");
			return (ARCHIVE_FAILED);
		default:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "tar format cannot archive this (type=0%lo)",
			    (unsigned long)
			    archive_entry_filetype(entry_original));
			return (ARCHIVE_FAILED);
		}
	}

	/*
	 * If Mac OS metadata blob is here, recurse to write that
	 * as a separate entry.  This is really a pretty poor design:
	 * In particular, it doubles the overhead for long filenames.
	 * TODO: Help Apple folks design something better and figure
	 * out how to transition from this legacy format.
	 *
	 * Note that this code is present on every platform; clients
	 * on non-Mac are unlikely to ever provide this data, but
	 * applications that copy entries from one archive to another
	 * should not lose data just because the local filesystem
	 * can't store it.
	 */
	mac_metadata =
	    archive_entry_mac_metadata(entry_original, &mac_metadata_size);
	if (mac_metadata != NULL) {
		const char *oname;
		char *name, *bname;
		size_t name_length;
		struct archive_entry *extra = archive_entry_new2(&a->archive);

		oname = archive_entry_pathname(entry_original);
		name_length = strlen(oname);
		name = malloc(name_length + 3);
		if (name == NULL || extra == NULL) {
			/* XXX error message */
			archive_entry_free(extra);
			free(name);
			return (ARCHIVE_FAILED);
		}
		strcpy(name, oname);
		/* Find last '/'; strip trailing '/' characters */
		bname = strrchr(name, '/');
		while (bname != NULL && bname[1] == '\0') {
			*bname = '\0';
			bname = strrchr(name, '/');
		}
		if (bname == NULL) {
			memmove(name + 2, name, name_length + 1);
			memmove(name, "._", 2);
		} else {
			bname += 1;
			memmove(bname + 2, bname, strlen(bname) + 1);
			memmove(bname, "._", 2);
		}
		archive_entry_copy_pathname(extra, name);
		free(name);

		archive_entry_set_size(extra, mac_metadata_size);
		archive_entry_set_filetype(extra, AE_IFREG);
		archive_entry_set_perm(extra,
		    archive_entry_perm(entry_original));
		archive_entry_set_mtime(extra,
		    archive_entry_mtime(entry_original),
		    archive_entry_mtime_nsec(entry_original));
		archive_entry_set_gid(extra,
		    archive_entry_gid(entry_original));
		archive_entry_set_gname(extra,
		    archive_entry_gname(entry_original));
		archive_entry_set_uid(extra,
		    archive_entry_uid(entry_original));
		archive_entry_set_uname(extra,
		    archive_entry_uname(entry_original));

		/* Recurse to write the special copyfile entry. */
		r = archive_write_pax_header(a, extra);
		archive_entry_free(extra);
		if (r < ARCHIVE_WARN)
			return (r);
		if (r < ret)
			ret = r;
		r = (int)archive_write_pax_data(a, mac_metadata,
		    mac_metadata_size);
		if (r < ARCHIVE_WARN)
			return (r);
		if (r < ret)
			ret = r;
		r = archive_write_pax_finish_entry(a);
		if (r < ARCHIVE_WARN)
			return (r);
		if (r < ret)
			ret = r;
	}

	/* Copy entry so we can modify it as needed. */
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Make sure the path separators in pathname, hardlink and symlink
	 * are all slash '/', not the Windows path separator '\'. */
	entry_main = __la_win_entry_in_posix_pathseparator(entry_original);
	if (entry_main == entry_original)
		entry_main = archive_entry_clone(entry_original);
#else
	entry_main = archive_entry_clone(entry_original);
#endif
	if (entry_main == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate pax data");
		return(ARCHIVE_FATAL);
	}
	archive_string_empty(&(pax->pax_header)); /* Blank our work area. */
	archive_string_empty(&(pax->sparse_map));
	sparse_total = 0;
	sparse_list_clear(pax);

	if (hardlink == NULL &&
	    archive_entry_filetype(entry_main) == AE_IFREG)
		sparse_count = archive_entry_sparse_reset(entry_main);
	else
		sparse_count = 0;
	if (sparse_count) {
		int64_t offset, length, last_offset = 0;
		/* Get the last entry of sparse block. */
		while (archive_entry_sparse_next(
		    entry_main, &offset, &length) == ARCHIVE_OK)
			last_offset = offset + length;

		/* If the last sparse block does not reach the end of file,
		 * We have to add a empty sparse block as the last entry to
		 * manage storing file data. */
		if (last_offset < archive_entry_size(entry_main))
			archive_entry_sparse_add_entry(entry_main,
			    archive_entry_size(entry_main), 0);
		sparse_count = archive_entry_sparse_reset(entry_main);
	}

	/*
	 * First, check the name fields and see if any of them
	 * require binary coding.  If any of them does, then all of
	 * them do.
	 */
	r = get_entry_pathname(a, entry_main, &path, &path_length, sconv);
	if (r == ARCHIVE_FATAL)
		return (r);
	else if (r != ARCHIVE_OK) {
		r = get_entry_pathname(a, entry_main, &path,
		    &path_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate pathname '%s' to %s", path,
		    archive_string_conversion_charset_name(sconv));
		ret = ARCHIVE_WARN;
		sconv = NULL;/* The header charset switches to binary mode. */
	}
	r = get_entry_uname(a, entry_main, &uname, &uname_length, sconv);
	if (r == ARCHIVE_FATAL)
		return (r);
	else if (r != ARCHIVE_OK) {
		r = get_entry_uname(a, entry_main, &uname, &uname_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate uname '%s' to %s", uname,
		    archive_string_conversion_charset_name(sconv));
		ret = ARCHIVE_WARN;
		sconv = NULL;/* The header charset switches to binary mode. */
	}
	r = get_entry_gname(a, entry_main, &gname, &gname_length, sconv);
	if (r == ARCHIVE_FATAL)
		return (r);
	else if (r != ARCHIVE_OK) {
		r = get_entry_gname(a, entry_main, &gname, &gname_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate gname '%s' to %s", gname,
		    archive_string_conversion_charset_name(sconv));
		ret = ARCHIVE_WARN;
		sconv = NULL;/* The header charset switches to binary mode. */
	}
	linkpath = hardlink;
	linkpath_length = hardlink_length;
	if (linkpath == NULL) {
		r = get_entry_symlink(a, entry_main, &linkpath,
		    &linkpath_length, sconv);
		if (r == ARCHIVE_FATAL)
			return (r);
		else if (r != ARCHIVE_OK) {
			r = get_entry_symlink(a, entry_main, &linkpath,
			    &linkpath_length, NULL);
			if (r == ARCHIVE_FATAL)
				return (r);
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Can't translate linkname '%s' to %s", linkpath,
			    archive_string_conversion_charset_name(sconv));
			ret = ARCHIVE_WARN;
			sconv = NULL;
		}
	}

	/* If any string conversions failed, get all attributes
	 * in binary-mode. */
	if (sconv == NULL && !pax->opt_binary) {
		if (hardlink != NULL) {
			r = get_entry_hardlink(a, entry_main, &hardlink,
			    &hardlink_length, NULL);
			if (r == ARCHIVE_FATAL)
				return (r);
			linkpath = hardlink;
			linkpath_length = hardlink_length;
		}
		r = get_entry_pathname(a, entry_main, &path,
		    &path_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		r = get_entry_uname(a, entry_main, &uname, &uname_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
		r = get_entry_gname(a, entry_main, &gname, &gname_length, NULL);
		if (r == ARCHIVE_FATAL)
			return (r);
	}

	/* Store the header encoding first, to be nice to readers. */
	if (sconv == NULL)
		add_pax_attr(&(pax->pax_header), "hdrcharset", "BINARY");


	/*
	 * If name is too long, or has non-ASCII characters, add
	 * 'path' to pax extended attrs.  (Note that an unconvertible
	 * name must have non-ASCII characters.)
	 */
	if (has_non_ASCII(path)) {
		/* We have non-ASCII characters. */
		add_pax_attr(&(pax->pax_header), "path", path);
		archive_entry_set_pathname(entry_main,
		    build_ustar_entry_name(ustar_entry_name,
			path, path_length, NULL));
		need_extension = 1;
	} else {
		/* We have an all-ASCII path; we'd like to just store
		 * it in the ustar header if it will fit.  Yes, this
		 * duplicates some of the logic in
		 * archive_write_set_format_ustar.c
		 */
		if (path_length <= 100) {
			/* Fits in the old 100-char tar name field. */
		} else {
			/* Find largest suffix that will fit. */
			/* Note: strlen() > 100, so strlen() - 100 - 1 >= 0 */
			suffix = strchr(path + path_length - 100 - 1, '/');
			/* Don't attempt an empty prefix. */
			if (suffix == path)
				suffix = strchr(suffix + 1, '/');
			/* We can put it in the ustar header if it's
			 * all ASCII and it's either <= 100 characters
			 * or can be split at a '/' into a prefix <=
			 * 155 chars and a suffix <= 100 chars.  (Note
			 * the strchr() above will return NULL exactly
			 * when the path can't be split.)
			 */
			if (suffix == NULL       /* Suffix > 100 chars. */
			    || suffix[1] == '\0'    /* empty suffix */
			    || suffix - path > 155)  /* Prefix > 155 chars */
			{
				add_pax_attr(&(pax->pax_header), "path", path);
				archive_entry_set_pathname(entry_main,
				    build_ustar_entry_name(ustar_entry_name,
					path, path_length, NULL));
				need_extension = 1;
			}
		}
	}

	if (linkpath != NULL) {
		/* If link name is too long or has non-ASCII characters, add
		 * 'linkpath' to pax extended attrs. */
		if (linkpath_length > 100 || has_non_ASCII(linkpath)) {
			add_pax_attr(&(pax->pax_header), "linkpath", linkpath);
			if (linkpath_length > 100) {
				if (hardlink != NULL)
					archive_entry_set_hardlink(entry_main,
					    "././@LongHardLink");
				else
					archive_entry_set_symlink(entry_main,
					    "././@LongSymLink");
			}
			need_extension = 1;
		}
	}
	/* Save a pathname since it will be renamed if `entry_main` has
	 * sparse blocks. */
	archive_string_init(&entry_name);
	archive_strcpy(&entry_name, archive_entry_pathname(entry_main));

	/* If file size is too large, add 'size' to pax extended attrs. */
	if (archive_entry_size(entry_main) >= (((int64_t)1) << 33)) {
		add_pax_attr_int(&(pax->pax_header), "size",
		    archive_entry_size(entry_main));
		need_extension = 1;
	}

	/* If numeric GID is too large, add 'gid' to pax extended attrs. */
	if ((unsigned int)archive_entry_gid(entry_main) >= (1 << 18)) {
		add_pax_attr_int(&(pax->pax_header), "gid",
		    archive_entry_gid(entry_main));
		need_extension = 1;
	}

	/* If group name is too large or has non-ASCII characters, add
	 * 'gname' to pax extended attrs. */
	if (gname != NULL) {
		if (gname_length > 31 || has_non_ASCII(gname)) {
			add_pax_attr(&(pax->pax_header), "gname", gname);
			need_extension = 1;
		}
	}

	/* If numeric UID is too large, add 'uid' to pax extended attrs. */
	if ((unsigned int)archive_entry_uid(entry_main) >= (1 << 18)) {
		add_pax_attr_int(&(pax->pax_header), "uid",
		    archive_entry_uid(entry_main));
		need_extension = 1;
	}

	/* Add 'uname' to pax extended attrs if necessary. */
	if (uname != NULL) {
		if (uname_length > 31 || has_non_ASCII(uname)) {
			add_pax_attr(&(pax->pax_header), "uname", uname);
			need_extension = 1;
		}
	}

	/*
	 * POSIX/SUSv3 doesn't provide a standard key for large device
	 * numbers.  I use the same keys here that Joerg Schilling
	 * used for 'star.'  (Which, somewhat confusingly, are called
	 * "devXXX" even though they code "rdev" values.)  No doubt,
	 * other implementations use other keys.  Note that there's no
	 * reason we can't write the same information into a number of
	 * different keys.
	 *
	 * Of course, this is only needed for block or char device entries.
	 */
	if (archive_entry_filetype(entry_main) == AE_IFBLK
	    || archive_entry_filetype(entry_main) == AE_IFCHR) {
		/*
		 * If rdevmajor is too large, add 'SCHILY.devmajor' to
		 * extended attributes.
		 */
		int rdevmajor, rdevminor;
		rdevmajor = archive_entry_rdevmajor(entry_main);
		rdevminor = archive_entry_rdevminor(entry_main);
		if (rdevmajor >= (1 << 18)) {
			add_pax_attr_int(&(pax->pax_header), "SCHILY.devmajor",
			    rdevmajor);
			/*
			 * Non-strict formatting below means we don't
			 * have to truncate here.  Not truncating improves
			 * the chance that some more modern tar archivers
			 * (such as GNU tar 1.13) can restore the full
			 * value even if they don't understand the pax
			 * extended attributes.  See my rant below about
			 * file size fields for additional details.
			 */
			/* archive_entry_set_rdevmajor(entry_main,
			   rdevmajor & ((1 << 18) - 1)); */
			need_extension = 1;
		}

		/*
		 * If devminor is too large, add 'SCHILY.devminor' to
		 * extended attributes.
		 */
		if (rdevminor >= (1 << 18)) {
			add_pax_attr_int(&(pax->pax_header), "SCHILY.devminor",
			    rdevminor);
			/* Truncation is not necessary here, either. */
			/* archive_entry_set_rdevminor(entry_main,
			   rdevminor & ((1 << 18) - 1)); */
			need_extension = 1;
		}
	}

	/*
	 * Technically, the mtime field in the ustar header can
	 * support 33 bits, but many platforms use signed 32-bit time
	 * values.  The cutoff of 0x7fffffff here is a compromise.
	 * Yes, this check is duplicated just below; this helps to
	 * avoid writing an mtime attribute just to handle a
	 * high-resolution timestamp in "restricted pax" mode.
	 */
	if (!need_extension &&
	    ((archive_entry_mtime(entry_main) < 0)
		|| (archive_entry_mtime(entry_main) >= 0x7fffffff)))
		need_extension = 1;

	/* I use a star-compatible file flag attribute. */
	p = archive_entry_fflags_text(entry_main);
	if (!need_extension && p != NULL  &&  *p != '\0')
		need_extension = 1;

	/* If there are extended attributes, we need an extension */
	if (!need_extension && archive_entry_xattr_count(entry_original) > 0)
		need_extension = 1;

	/* If there are sparse info, we need an extension */
	if (!need_extension && sparse_count > 0)
		need_extension = 1;

	acl_types = archive_entry_acl_types(entry_original);

	/* If there are any ACL entries, we need an extension */
	if (!need_extension && acl_types != 0)
		need_extension = 1;

	/*
	 * Libarchive used to include these in extended headers for
	 * restricted pax format, but that confused people who
	 * expected ustar-like time semantics.  So now we only include
	 * them in full pax format.
	 */
	if (a->archive.archive_format != ARCHIVE_FORMAT_TAR_PAX_RESTRICTED) {
		if (archive_entry_ctime(entry_main) != 0  ||
		    archive_entry_ctime_nsec(entry_main) != 0)
			add_pax_attr_time(&(pax->pax_header), "ctime",
			    archive_entry_ctime(entry_main),
			    archive_entry_ctime_nsec(entry_main));

		if (archive_entry_atime(entry_main) != 0 ||
		    archive_entry_atime_nsec(entry_main) != 0)
			add_pax_attr_time(&(pax->pax_header), "atime",
			    archive_entry_atime(entry_main),
			    archive_entry_atime_nsec(entry_main));

		/* Store birth/creationtime only if it's earlier than mtime */
		if (archive_entry_birthtime_is_set(entry_main) &&
		    archive_entry_birthtime(entry_main)
		    < archive_entry_mtime(entry_main))
			add_pax_attr_time(&(pax->pax_header),
			    "LIBARCHIVE.creationtime",
			    archive_entry_birthtime(entry_main),
			    archive_entry_birthtime_nsec(entry_main));
	}

	/*
	 * The following items are handled differently in "pax
	 * restricted" format.  In particular, in "pax restricted"
	 * format they won't be added unless need_extension is
	 * already set (we're already generating an extended header, so
	 * may as well include these).
	 */
	if (a->archive.archive_format != ARCHIVE_FORMAT_TAR_PAX_RESTRICTED ||
	    need_extension) {
		if (archive_entry_mtime(entry_main) < 0  ||
		    archive_entry_mtime(entry_main) >= 0x7fffffff  ||
		    archive_entry_mtime_nsec(entry_main) != 0)
			add_pax_attr_time(&(pax->pax_header), "mtime",
			    archive_entry_mtime(entry_main),
			    archive_entry_mtime_nsec(entry_main));

		/* I use a star-compatible file flag attribute. */
		p = archive_entry_fflags_text(entry_main);
		if (p != NULL  &&  *p != '\0')
			add_pax_attr(&(pax->pax_header), "SCHILY.fflags", p);

		/* I use star-compatible ACL attributes. */
		if ((acl_types & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
			ret = add_pax_acl(a, entry_original, pax,
			    ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID |
			    ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA |
			    ARCHIVE_ENTRY_ACL_STYLE_COMPACT);
			if (ret == ARCHIVE_FATAL)
				return (ARCHIVE_FATAL);
		}
		if (acl_types & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) {
			ret = add_pax_acl(a, entry_original, pax,
			    ARCHIVE_ENTRY_ACL_TYPE_ACCESS |
			    ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID |
			    ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA);
			if (ret == ARCHIVE_FATAL)
				return (ARCHIVE_FATAL);
		}
		if (acl_types & ARCHIVE_ENTRY_ACL_TYPE_DEFAULT) {
			ret = add_pax_acl(a, entry_original, pax,
			    ARCHIVE_ENTRY_ACL_TYPE_DEFAULT |
			    ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID |
			    ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA);
			if (ret == ARCHIVE_FATAL)
				return (ARCHIVE_FATAL);
		}

		/* We use GNU-tar-compatible sparse attributes. */
		if (sparse_count > 0) {
			int64_t soffset, slength;

			add_pax_attr_int(&(pax->pax_header),
			    "GNU.sparse.major", 1);
			add_pax_attr_int(&(pax->pax_header),
			    "GNU.sparse.minor", 0);
			/*
			 * Make sure to store the original path, since
			 * truncation to ustar limit happened already.
			 */
			add_pax_attr(&(pax->pax_header),
			    "GNU.sparse.name", path);
			add_pax_attr_int(&(pax->pax_header),
			    "GNU.sparse.realsize",
			    archive_entry_size(entry_main));

			/* Rename the file name which will be used for
			 * ustar header to a special name, which GNU
			 * PAX Format 1.0 requires */
			archive_entry_set_pathname(entry_main,
			    build_gnu_sparse_name(gnu_sparse_name,
			        entry_name.s));

			/*
			 * - Make a sparse map, which will precede a file data.
			 * - Get the total size of available data of sparse.
			 */
			archive_string_sprintf(&(pax->sparse_map), "%d\n",
			    sparse_count);
			while (archive_entry_sparse_next(entry_main,
			    &soffset, &slength) == ARCHIVE_OK) {
				archive_string_sprintf(&(pax->sparse_map),
				    "%jd\n%jd\n",
				    (intmax_t)soffset,
				    (intmax_t)slength);
				sparse_total += slength;
				if (sparse_list_add(pax, soffset, slength)
				    != ARCHIVE_OK) {
					archive_set_error(&a->archive,
					    ENOMEM,
					    "Can't allocate memory");
					archive_entry_free(entry_main);
					archive_string_free(&entry_name);
					return (ARCHIVE_FATAL);
				}
			}
		}

		/* Store extended attributes */
		if (archive_write_pax_header_xattrs(a, pax, entry_original)
		    == ARCHIVE_FATAL) {
			archive_entry_free(entry_main);
			archive_string_free(&entry_name);
			return (ARCHIVE_FATAL);
		}
	}

	/* Only regular files have data. */
	if (archive_entry_filetype(entry_main) != AE_IFREG)
		archive_entry_set_size(entry_main, 0);

	/*
	 * Pax-restricted does not store data for hardlinks, in order
	 * to improve compatibility with ustar.
	 */
	if (a->archive.archive_format != ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE &&
	    hardlink != NULL)
		archive_entry_set_size(entry_main, 0);

	/*
	 * XXX Full pax interchange format does permit a hardlink
	 * entry to have data associated with it.  I'm not supporting
	 * that here because the client expects me to tell them whether
	 * or not this format expects data for hardlinks.  If I
	 * don't check here, then every pax archive will end up with
	 * duplicated data for hardlinks.  Someday, there may be
	 * need to select this behavior, in which case the following
	 * will need to be revisited. XXX
	 */
	if (hardlink != NULL)
		archive_entry_set_size(entry_main, 0);

	/* Save a real file size. */
	real_size = archive_entry_size(entry_main);
	/*
	 * Overwrite a file size by the total size of sparse blocks and
	 * the size of sparse map info. That file size is the length of
	 * the data, which we will exactly store into an archive file.
	 */
	if (archive_strlen(&(pax->sparse_map))) {
		size_t mapsize = archive_strlen(&(pax->sparse_map));
		pax->sparse_map_padding = 0x1ff & (-(ssize_t)mapsize);
		archive_entry_set_size(entry_main,
		    mapsize + pax->sparse_map_padding + sparse_total);
	}

	/* Format 'ustar' header for main entry.
	 *
	 * The trouble with file size: If the reader can't understand
	 * the file size, they may not be able to locate the next
	 * entry and the rest of the archive is toast.  Pax-compliant
	 * readers are supposed to ignore the file size in the main
	 * header, so the question becomes how to maximize portability
	 * for readers that don't support pax attribute extensions.
	 * For maximum compatibility, I permit numeric extensions in
	 * the main header so that the file size stored will always be
	 * correct, even if it's in a format that only some
	 * implementations understand.  The technique used here is:
	 *
	 *  a) If possible, follow the standard exactly.  This handles
	 *  files up to 8 gigabytes minus 1.
	 *
	 *  b) If that fails, try octal but omit the field terminator.
	 *  That handles files up to 64 gigabytes minus 1.
	 *
	 *  c) Otherwise, use base-256 extensions.  That handles files
	 *  up to 2^63 in this implementation, with the potential to
	 *  go up to 2^94.  That should hold us for a while. ;-)
	 *
	 * The non-strict formatter uses similar logic for other
	 * numeric fields, though they're less critical.
	 */
	if (__archive_write_format_header_ustar(a, ustarbuff, entry_main, -1, 0,
	    NULL) == ARCHIVE_FATAL)
		return (ARCHIVE_FATAL);

	/* If we built any extended attributes, write that entry first. */
	if (archive_strlen(&(pax->pax_header)) > 0) {
		struct archive_entry *pax_attr_entry;
		time_t s;
		int64_t uid, gid;
		int mode;

		pax_attr_entry = archive_entry_new2(&a->archive);
		p = entry_name.s;
		archive_entry_set_pathname(pax_attr_entry,
		    build_pax_attribute_name(pax_entry_name, p));
		archive_entry_set_size(pax_attr_entry,
		    archive_strlen(&(pax->pax_header)));
		/* Copy uid/gid (but clip to ustar limits). */
		uid = archive_entry_uid(entry_main);
		if (uid >= 1 << 18)
			uid = (1 << 18) - 1;
		archive_entry_set_uid(pax_attr_entry, uid);
		gid = archive_entry_gid(entry_main);
		if (gid >= 1 << 18)
			gid = (1 << 18) - 1;
		archive_entry_set_gid(pax_attr_entry, gid);
		/* Copy mode over (but not setuid/setgid bits) */
		mode = archive_entry_mode(entry_main);
#ifdef S_ISUID
		mode &= ~S_ISUID;
#endif
#ifdef S_ISGID
		mode &= ~S_ISGID;
#endif
#ifdef S_ISVTX
		mode &= ~S_ISVTX;
#endif
		archive_entry_set_mode(pax_attr_entry, mode);

		/* Copy uname/gname. */
		archive_entry_set_uname(pax_attr_entry,
		    archive_entry_uname(entry_main));
		archive_entry_set_gname(pax_attr_entry,
		    archive_entry_gname(entry_main));

		/* Copy mtime, but clip to ustar limits. */
		s = archive_entry_mtime(entry_main);
		if (s < 0) { s = 0; }
		if (s >= 0x7fffffff) { s = 0x7fffffff; }
		archive_entry_set_mtime(pax_attr_entry, s, 0);

		/* Standard ustar doesn't support atime. */
		archive_entry_set_atime(pax_attr_entry, 0, 0);

		/* Standard ustar doesn't support ctime. */
		archive_entry_set_ctime(pax_attr_entry, 0, 0);

		r = __archive_write_format_header_ustar(a, paxbuff,
		    pax_attr_entry, 'x', 1, NULL);

		archive_entry_free(pax_attr_entry);

		/* Note that the 'x' header shouldn't ever fail to format */
		if (r < ARCHIVE_WARN) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "archive_write_pax_header: "
			    "'x' header failed?!  This can't happen.\n");
			return (ARCHIVE_FATAL);
		} else if (r < ret)
			ret = r;
		r = __archive_write_output(a, paxbuff, 512);
		if (r != ARCHIVE_OK) {
			sparse_list_clear(pax);
			pax->entry_bytes_remaining = 0;
			pax->entry_padding = 0;
			return (ARCHIVE_FATAL);
		}

		pax->entry_bytes_remaining = archive_strlen(&(pax->pax_header));
		pax->entry_padding =
		    0x1ff & (-(int64_t)pax->entry_bytes_remaining);

		r = __archive_write_output(a, pax->pax_header.s,
		    archive_strlen(&(pax->pax_header)));
		if (r != ARCHIVE_OK) {
			/* If a write fails, we're pretty much toast. */
			return (ARCHIVE_FATAL);
		}
		/* Pad out the end of the entry. */
		r = __archive_write_nulls(a, (size_t)pax->entry_padding);
		if (r != ARCHIVE_OK) {
			/* If a write fails, we're pretty much toast. */
			return (ARCHIVE_FATAL);
		}
		pax->entry_bytes_remaining = pax->entry_padding = 0;
	}

	/* Write the header for main entry. */
	r = __archive_write_output(a, ustarbuff, 512);
	if (r != ARCHIVE_OK)
		return (r);

	/*
	 * Inform the client of the on-disk size we're using, so
	 * they can avoid unnecessarily writing a body for something
	 * that we're just going to ignore.
	 */
	archive_entry_set_size(entry_original, real_size);
	if (pax->sparse_list == NULL && real_size > 0) {
		/* This is not a sparse file but we handle its data as
		 * a sparse block. */
		sparse_list_add(pax, 0, real_size);
		sparse_total = real_size;
	}
	pax->entry_padding = 0x1ff & (-(int64_t)sparse_total);
	archive_entry_free(entry_main);
	archive_string_free(&entry_name);

	return (ret);
}

/*
 * We need a valid name for the regular 'ustar' entry.  This routine
 * tries to hack something more-or-less reasonable.
 *
 * The approach here tries to preserve leading dir names.  We do so by
 * working with four sections:
 *   1) "prefix" directory names,
 *   2) "suffix" directory names,
 *   3) inserted dir name (optional),
 *   4) filename.
 *
 * These sections must satisfy the following requirements:
 *   * Parts 1 & 2 together form an initial portion of the dir name.
 *   * Part 3 is specified by the caller.  (It should not contain a leading
 *     or trailing '/'.)
 *   * Part 4 forms an initial portion of the base filename.
 *   * The filename must be <= 99 chars to fit the ustar 'name' field.
 *   * Parts 2, 3, 4 together must be <= 99 chars to fit the ustar 'name' fld.
 *   * Part 1 must be <= 155 chars to fit the ustar 'prefix' field.
 *   * If the original name ends in a '/', the new name must also end in a '/'
 *   * Trailing '/.' sequences may be stripped.
 *
 * Note: Recall that the ustar format does not store the '/' separating
 * parts 1 & 2, but does store the '/' separating parts 2 & 3.
 */
static char *
build_ustar_entry_name(char *dest, const char *src, size_t src_length,
    const char *insert)
{
	const char *prefix, *prefix_end;
	const char *suffix, *suffix_end;
	const char *filename, *filename_end;
	char *p;
	int need_slash = 0; /* Was there a trailing slash? */
	size_t suffix_length = 99;
	size_t insert_length;

	/* Length of additional dir element to be added. */
	if (insert == NULL)
		insert_length = 0;
	else
		/* +2 here allows for '/' before and after the insert. */
		insert_length = strlen(insert) + 2;

	/* Step 0: Quick bailout in a common case. */
	if (src_length < 100 && insert == NULL) {
		strncpy(dest, src, src_length);
		dest[src_length] = '\0';
		return (dest);
	}

	/* Step 1: Locate filename and enforce the length restriction. */
	filename_end = src + src_length;
	/* Remove trailing '/' chars and '/.' pairs. */
	for (;;) {
		if (filename_end > src && filename_end[-1] == '/') {
			filename_end --;
			need_slash = 1; /* Remember to restore trailing '/'. */
			continue;
		}
		if (filename_end > src + 1 && filename_end[-1] == '.'
		    && filename_end[-2] == '/') {
			filename_end -= 2;
			need_slash = 1; /* "foo/." will become "foo/" */
			continue;
		}
		break;
	}
	if (need_slash)
		suffix_length--;
	/* Find start of filename. */
	filename = filename_end - 1;
	while ((filename > src) && (*filename != '/'))
		filename --;
	if ((*filename == '/') && (filename < filename_end - 1))
		filename ++;
	/* Adjust filename_end so that filename + insert fits in 99 chars. */
	suffix_length -= insert_length;
	if (filename_end > filename + suffix_length)
		filename_end = filename + suffix_length;
	/* Calculate max size for "suffix" section (#3 above). */
	suffix_length -= filename_end - filename;

	/* Step 2: Locate the "prefix" section of the dirname, including
	 * trailing '/'. */
	prefix = src;
	prefix_end = prefix + 155;
	if (prefix_end > filename)
		prefix_end = filename;
	while (prefix_end > prefix && *prefix_end != '/')
		prefix_end--;
	if ((prefix_end < filename) && (*prefix_end == '/'))
		prefix_end++;

	/* Step 3: Locate the "suffix" section of the dirname,
	 * including trailing '/'. */
	suffix = prefix_end;
	suffix_end = suffix + suffix_length; /* Enforce limit. */
	if (suffix_end > filename)
		suffix_end = filename;
	if (suffix_end < suffix)
		suffix_end = suffix;
	while (suffix_end > suffix && *suffix_end != '/')
		suffix_end--;
	if ((suffix_end < filename) && (*suffix_end == '/'))
		suffix_end++;

	/* Step 4: Build the new name. */
	/* The OpenBSD strlcpy function is safer, but less portable. */
	/* Rather than maintain two versions, just use the strncpy version. */
	p = dest;
	if (prefix_end > prefix) {
		strncpy(p, prefix, prefix_end - prefix);
		p += prefix_end - prefix;
	}
	if (suffix_end > suffix) {
		strncpy(p, suffix, suffix_end - suffix);
		p += suffix_end - suffix;
	}
	if (insert != NULL) {
		/* Note: assume insert does not have leading or trailing '/' */
		strcpy(p, insert);
		p += strlen(insert);
		*p++ = '/';
	}
	strncpy(p, filename, filename_end - filename);
	p += filename_end - filename;
	if (need_slash)
		*p++ = '/';
	*p = '\0';

	return (dest);
}

/*
 * The ustar header for the pax extended attributes must have a
 * reasonable name:  SUSv3 requires 'dirname'/PaxHeader.'pid'/'filename'
 * where 'pid' is the PID of the archiving process.  Unfortunately,
 * that makes testing a pain since the output varies for each run,
 * so I'm sticking with the simpler 'dirname'/PaxHeader/'filename'
 * for now.  (Someday, I'll make this settable.  Then I can use the
 * SUS recommendation as default and test harnesses can override it
 * to get predictable results.)
 *
 * Joerg Schilling has argued that this is unnecessary because, in
 * practice, if the pax extended attributes get extracted as regular
 * files, no one is going to bother reading those attributes to
 * manually restore them.  Based on this, 'star' uses
 * /tmp/PaxHeader/'basename' as the ustar header name.  This is a
 * tempting argument, in part because it's simpler than the SUSv3
 * recommendation, but I'm not entirely convinced.  I'm also
 * uncomfortable with the fact that "/tmp" is a Unix-ism.
 *
 * The following routine leverages build_ustar_entry_name() above and
 * so is simpler than you might think.  It just needs to provide the
 * additional path element and handle a few pathological cases).
 */
static char *
build_pax_attribute_name(char *dest, const char *src)
{
	char buff[64];
	const char *p;

	/* Handle the null filename case. */
	if (src == NULL || *src == '\0') {
		strcpy(dest, "PaxHeader/blank");
		return (dest);
	}

	/* Prune final '/' and other unwanted final elements. */
	p = src + strlen(src);
	for (;;) {
		/* Ends in "/", remove the '/' */
		if (p > src && p[-1] == '/') {
			--p;
			continue;
		}
		/* Ends in "/.", remove the '.' */
		if (p > src + 1 && p[-1] == '.'
		    && p[-2] == '/') {
			--p;
			continue;
		}
		break;
	}

	/* Pathological case: After above, there was nothing left.
	 * This includes "/." "/./." "/.//./." etc. */
	if (p == src) {
		strcpy(dest, "/PaxHeader/rootdir");
		return (dest);
	}

	/* Convert unadorned "." into a suitable filename. */
	if (*src == '.' && p == src + 1) {
		strcpy(dest, "PaxHeader/currentdir");
		return (dest);
	}

	/*
	 * TODO: Push this string into the 'pax' structure to avoid
	 * recomputing it every time.  That will also open the door
	 * to having clients override it.
	 */
#if HAVE_GETPID && 0  /* Disable this for now; see above comment. */
	sprintf(buff, "PaxHeader.%d", getpid());
#else
	/* If the platform can't fetch the pid, don't include it. */
	strcpy(buff, "PaxHeader");
#endif
	/* General case: build a ustar-compatible name adding
	 * "/PaxHeader/". */
	build_ustar_entry_name(dest, src, p - src, buff);

	return (dest);
}

/*
 * GNU PAX Format 1.0 requires the special name, which pattern is:
 * <dir>/GNUSparseFile.<pid>/<original file name>
 *
 * Since reproducible archives are more important, use 0 as pid.
 *
 * This function is used for only Sparse file, a file type of which
 * is regular file.
 */
static char *
build_gnu_sparse_name(char *dest, const char *src)
{
	const char *p;

	/* Handle the null filename case. */
	if (src == NULL || *src == '\0') {
		strcpy(dest, "GNUSparseFile/blank");
		return (dest);
	}

	/* Prune final '/' and other unwanted final elements. */
	p = src + strlen(src);
	for (;;) {
		/* Ends in "/", remove the '/' */
		if (p > src && p[-1] == '/') {
			--p;
			continue;
		}
		/* Ends in "/.", remove the '.' */
		if (p > src + 1 && p[-1] == '.'
		    && p[-2] == '/') {
			--p;
			continue;
		}
		break;
	}

	/* General case: build a ustar-compatible name adding
	 * "/GNUSparseFile/". */
	build_ustar_entry_name(dest, src, p - src, "GNUSparseFile.0");

	return (dest);
}

/* Write two null blocks for the end of archive */
static int
archive_write_pax_close(struct archive_write *a)
{
	return (__archive_write_nulls(a, 512 * 2));
}

static int
archive_write_pax_free(struct archive_write *a)
{
	struct pax *pax;

	pax = (struct pax *)a->format_data;
	if (pax == NULL)
		return (ARCHIVE_OK);

	archive_string_free(&pax->pax_header);
	archive_string_free(&pax->sparse_map);
	archive_string_free(&pax->l_url_encoded_name);
	sparse_list_clear(pax);
	free(pax);
	a->format_data = NULL;
	return (ARCHIVE_OK);
}

static int
archive_write_pax_finish_entry(struct archive_write *a)
{
	struct pax *pax;
	uint64_t remaining;
	int ret;

	pax = (struct pax *)a->format_data;
	remaining = pax->entry_bytes_remaining;
	if (remaining == 0) {
		while (pax->sparse_list) {
			struct sparse_block *sb;
			if (!pax->sparse_list->is_hole)
				remaining += pax->sparse_list->remaining;
			sb = pax->sparse_list->next;
			free(pax->sparse_list);
			pax->sparse_list = sb;
		}
	}
	ret = __archive_write_nulls(a, (size_t)(remaining + pax->entry_padding));
	pax->entry_bytes_remaining = pax->entry_padding = 0;
	return (ret);
}

static ssize_t
archive_write_pax_data(struct archive_write *a, const void *buff, size_t s)
{
	struct pax *pax;
	size_t ws;
	size_t total;
	int ret;

	pax = (struct pax *)a->format_data;

	/*
	 * According to GNU PAX format 1.0, write a sparse map
	 * before the body.
	 */
	if (archive_strlen(&(pax->sparse_map))) {
		ret = __archive_write_output(a, pax->sparse_map.s,
		    archive_strlen(&(pax->sparse_map)));
		if (ret != ARCHIVE_OK)
			return (ret);
		ret = __archive_write_nulls(a, pax->sparse_map_padding);
		if (ret != ARCHIVE_OK)
			return (ret);
		archive_string_empty(&(pax->sparse_map));
	}

	total = 0;
	while (total < s) {
		const unsigned char *p;

		while (pax->sparse_list != NULL &&
		    pax->sparse_list->remaining == 0) {
			struct sparse_block *sb = pax->sparse_list->next;
			free(pax->sparse_list);
			pax->sparse_list = sb;
		}

		if (pax->sparse_list == NULL)
			return (total);

		p = ((const unsigned char *)buff) + total;
		ws = s - total;
		if (ws > pax->sparse_list->remaining)
			ws = (size_t)pax->sparse_list->remaining;

		if (pax->sparse_list->is_hole) {
			/* Current block is hole thus we do not write
			 * the body. */
			pax->sparse_list->remaining -= ws;
			total += ws;
			continue;
		}

		ret = __archive_write_output(a, p, ws);
		pax->sparse_list->remaining -= ws;
		total += ws;
		if (ret != ARCHIVE_OK)
			return (ret);
	}
	return (total);
}

static int
has_non_ASCII(const char *_p)
{
	const unsigned char *p = (const unsigned char *)_p;

	if (p == NULL)
		return (1);
	while (*p != '\0' && *p < 128)
		p++;
	return (*p != '\0');
}

/*
 * Used by extended attribute support; encodes the name
 * so that there will be no '=' characters in the result.
 */
static char *
url_encode(const char *in)
{
	const char *s;
	char *d;
	int out_len = 0;
	char *out;

	for (s = in; *s != '\0'; s++) {
		if (*s < 33 || *s > 126 || *s == '%' || *s == '=')
			out_len += 3;
		else
			out_len++;
	}

	out = (char *)malloc(out_len + 1);
	if (out == NULL)
		return (NULL);

	for (s = in, d = out; *s != '\0'; s++) {
		/* encode any non-printable ASCII character or '%' or '=' */
		if (*s < 33 || *s > 126 || *s == '%' || *s == '=') {
			/* URL encoding is '%' followed by two hex digits */
			*d++ = '%';
			*d++ = "0123456789ABCDEF"[0x0f & (*s >> 4)];
			*d++ = "0123456789ABCDEF"[0x0f & *s];
		} else {
			*d++ = *s;
		}
	}
	*d = '\0';
	return (out);
}

/*
 * Encode a sequence of bytes into a C string using base-64 encoding.
 *
 * Returns a null-terminated C string allocated with malloc(); caller
 * is responsible for freeing the result.
 */
static char *
base64_encode(const char *s, size_t len)
{
	static const char digits[64] =
	    { 'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
	      'P','Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d',
	      'e','f','g','h','i','j','k','l','m','n','o','p','q','r','s',
	      't','u','v','w','x','y','z','0','1','2','3','4','5','6','7',
	      '8','9','+','/' };
	int v;
	char *d, *out;

	/* 3 bytes becomes 4 chars, but round up and allow for trailing NUL */
	out = (char *)malloc((len * 4 + 2) / 3 + 1);
	if (out == NULL)
		return (NULL);
	d = out;

	/* Convert each group of 3 bytes into 4 characters. */
	while (len >= 3) {
		v = (((int)s[0] << 16) & 0xff0000)
		    | (((int)s[1] << 8) & 0xff00)
		    | (((int)s[2]) & 0x00ff);
		s += 3;
		len -= 3;
		*d++ = digits[(v >> 18) & 0x3f];
		*d++ = digits[(v >> 12) & 0x3f];
		*d++ = digits[(v >> 6) & 0x3f];
		*d++ = digits[(v) & 0x3f];
	}
	/* Handle final group of 1 byte (2 chars) or 2 bytes (3 chars). */
	switch (len) {
	case 0: break;
	case 1:
		v = (((int)s[0] << 16) & 0xff0000);
		*d++ = digits[(v >> 18) & 0x3f];
		*d++ = digits[(v >> 12) & 0x3f];
		break;
	case 2:
		v = (((int)s[0] << 16) & 0xff0000)
		    | (((int)s[1] << 8) & 0xff00);
		*d++ = digits[(v >> 18) & 0x3f];
		*d++ = digits[(v >> 12) & 0x3f];
		*d++ = digits[(v >> 6) & 0x3f];
		break;
	}
	/* Add trailing NUL character so output is a valid C string. */
	*d = '\0';
	return (out);
}

static void
sparse_list_clear(struct pax *pax)
{
	while (pax->sparse_list != NULL) {
		struct sparse_block *sb = pax->sparse_list;
		pax->sparse_list = sb->next;
		free(sb);
	}
	pax->sparse_tail = NULL;
}

static int
_sparse_list_add_block(struct pax *pax, int64_t offset, int64_t length,
    int is_hole)
{
	struct sparse_block *sb;

	sb = (struct sparse_block *)malloc(sizeof(*sb));
	if (sb == NULL)
		return (ARCHIVE_FATAL);
	sb->next = NULL;
	sb->is_hole = is_hole;
	sb->offset = offset;
	sb->remaining = length;
	if (pax->sparse_list == NULL || pax->sparse_tail == NULL)
		pax->sparse_list = pax->sparse_tail = sb;
	else {
		pax->sparse_tail->next = sb;
		pax->sparse_tail = sb;
	}
	return (ARCHIVE_OK);
}

static int
sparse_list_add(struct pax *pax, int64_t offset, int64_t length)
{
	int64_t last_offset;
	int r;

	if (pax->sparse_tail == NULL)
		last_offset = 0;
	else {
		last_offset = pax->sparse_tail->offset +
		    pax->sparse_tail->remaining;
	}
	if (last_offset < offset) {
		/* Add a hole block. */
		r = _sparse_list_add_block(pax, last_offset,
		    offset - last_offset, 1);
		if (r != ARCHIVE_OK)
			return (r);
	}
	/* Add data block. */
	return (_sparse_list_add_block(pax, offset, length, 0));
}

