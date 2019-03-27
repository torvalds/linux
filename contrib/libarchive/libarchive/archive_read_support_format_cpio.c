/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2010-2012 Michihiro NAKAJIMA
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
/* #include <stdint.h> */ /* See archive_platform.h */
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
#include "archive_read_private.h"

#define	bin_magic_offset 0
#define	bin_magic_size 2
#define	bin_dev_offset 2
#define	bin_dev_size 2
#define	bin_ino_offset 4
#define	bin_ino_size 2
#define	bin_mode_offset 6
#define	bin_mode_size 2
#define	bin_uid_offset 8
#define	bin_uid_size 2
#define	bin_gid_offset 10
#define	bin_gid_size 2
#define	bin_nlink_offset 12
#define	bin_nlink_size 2
#define	bin_rdev_offset 14
#define	bin_rdev_size 2
#define	bin_mtime_offset 16
#define	bin_mtime_size 4
#define	bin_namesize_offset 20
#define	bin_namesize_size 2
#define	bin_filesize_offset 22
#define	bin_filesize_size 4
#define	bin_header_size 26

#define	odc_magic_offset 0
#define	odc_magic_size 6
#define	odc_dev_offset 6
#define	odc_dev_size 6
#define	odc_ino_offset 12
#define	odc_ino_size 6
#define	odc_mode_offset 18
#define	odc_mode_size 6
#define	odc_uid_offset 24
#define	odc_uid_size 6
#define	odc_gid_offset 30
#define	odc_gid_size 6
#define	odc_nlink_offset 36
#define	odc_nlink_size 6
#define	odc_rdev_offset 42
#define	odc_rdev_size 6
#define	odc_mtime_offset 48
#define	odc_mtime_size 11
#define	odc_namesize_offset 59
#define	odc_namesize_size 6
#define	odc_filesize_offset 65
#define	odc_filesize_size 11
#define	odc_header_size 76

#define	newc_magic_offset 0
#define	newc_magic_size 6
#define	newc_ino_offset 6
#define	newc_ino_size 8
#define	newc_mode_offset 14
#define	newc_mode_size 8
#define	newc_uid_offset 22
#define	newc_uid_size 8
#define	newc_gid_offset 30
#define	newc_gid_size 8
#define	newc_nlink_offset 38
#define	newc_nlink_size 8
#define	newc_mtime_offset 46
#define	newc_mtime_size 8
#define	newc_filesize_offset 54
#define	newc_filesize_size 8
#define	newc_devmajor_offset 62
#define	newc_devmajor_size 8
#define	newc_devminor_offset 70
#define	newc_devminor_size 8
#define	newc_rdevmajor_offset 78
#define	newc_rdevmajor_size 8
#define	newc_rdevminor_offset 86
#define	newc_rdevminor_size 8
#define	newc_namesize_offset 94
#define	newc_namesize_size 8
#define	newc_checksum_offset 102
#define	newc_checksum_size 8
#define	newc_header_size 110

/*
 * An afio large ASCII header, which they named itself.
 * afio utility uses this header, if a file size is larger than 2G bytes
 * or inode/uid/gid is bigger than 65535(0xFFFF) or mtime is bigger than
 * 0x7fffffff, which we cannot record to odc header because of its limit.
 * If not, uses odc header.
 */
#define	afiol_magic_offset 0
#define	afiol_magic_size 6
#define	afiol_dev_offset 6
#define	afiol_dev_size 8	/* hex */
#define	afiol_ino_offset 14
#define	afiol_ino_size 16	/* hex */
#define	afiol_ino_m_offset 30	/* 'm' */
#define	afiol_mode_offset 31
#define	afiol_mode_size 6	/* oct */
#define	afiol_uid_offset 37
#define	afiol_uid_size 8	/* hex */
#define	afiol_gid_offset 45
#define	afiol_gid_size 8	/* hex */
#define	afiol_nlink_offset 53
#define	afiol_nlink_size 8	/* hex */
#define	afiol_rdev_offset 61
#define	afiol_rdev_size 8	/* hex */
#define	afiol_mtime_offset 69
#define	afiol_mtime_size 16	/* hex */
#define	afiol_mtime_n_offset 85	/* 'n' */
#define	afiol_namesize_offset 86
#define	afiol_namesize_size 4	/* hex */
#define	afiol_flag_offset 90
#define	afiol_flag_size 4	/* hex */
#define	afiol_xsize_offset 94
#define	afiol_xsize_size 4	/* hex */
#define	afiol_xsize_s_offset 98	/* 's' */
#define	afiol_filesize_offset 99
#define	afiol_filesize_size 16	/* hex */
#define	afiol_filesize_c_offset 115	/* ':' */
#define afiol_header_size 116


struct links_entry {
        struct links_entry      *next;
        struct links_entry      *previous;
        unsigned int             links;
        dev_t                    dev;
        int64_t                  ino;
        char                    *name;
};

#define	CPIO_MAGIC   0x13141516
struct cpio {
	int			  magic;
	int			(*read_header)(struct archive_read *, struct cpio *,
				     struct archive_entry *, size_t *, size_t *);
	struct links_entry	 *links_head;
	int64_t			  entry_bytes_remaining;
	int64_t			  entry_bytes_unconsumed;
	int64_t			  entry_offset;
	int64_t			  entry_padding;

	struct archive_string_conv *opt_sconv;
	struct archive_string_conv *sconv_default;
	int			  init_default_conversion;
};

static int64_t	atol16(const char *, unsigned);
static int64_t	atol8(const char *, unsigned);
static int	archive_read_format_cpio_bid(struct archive_read *, int);
static int	archive_read_format_cpio_options(struct archive_read *,
		    const char *, const char *);
static int	archive_read_format_cpio_cleanup(struct archive_read *);
static int	archive_read_format_cpio_read_data(struct archive_read *,
		    const void **, size_t *, int64_t *);
static int	archive_read_format_cpio_read_header(struct archive_read *,
		    struct archive_entry *);
static int	archive_read_format_cpio_skip(struct archive_read *);
static int64_t	be4(const unsigned char *);
static int	find_odc_header(struct archive_read *);
static int	find_newc_header(struct archive_read *);
static int	header_bin_be(struct archive_read *, struct cpio *,
		    struct archive_entry *, size_t *, size_t *);
static int	header_bin_le(struct archive_read *, struct cpio *,
		    struct archive_entry *, size_t *, size_t *);
static int	header_newc(struct archive_read *, struct cpio *,
		    struct archive_entry *, size_t *, size_t *);
static int	header_odc(struct archive_read *, struct cpio *,
		    struct archive_entry *, size_t *, size_t *);
static int	header_afiol(struct archive_read *, struct cpio *,
		    struct archive_entry *, size_t *, size_t *);
static int	is_octal(const char *, size_t);
static int	is_hex(const char *, size_t);
static int64_t	le4(const unsigned char *);
static int	record_hardlink(struct archive_read *a,
		    struct cpio *cpio, struct archive_entry *entry);

int
archive_read_support_format_cpio(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct cpio *cpio;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_cpio");

	cpio = (struct cpio *)calloc(1, sizeof(*cpio));
	if (cpio == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Can't allocate cpio data");
		return (ARCHIVE_FATAL);
	}
	cpio->magic = CPIO_MAGIC;

	r = __archive_read_register_format(a,
	    cpio,
	    "cpio",
	    archive_read_format_cpio_bid,
	    archive_read_format_cpio_options,
	    archive_read_format_cpio_read_header,
	    archive_read_format_cpio_read_data,
	    archive_read_format_cpio_skip,
	    NULL,
	    archive_read_format_cpio_cleanup,
	    NULL,
	    NULL);

	if (r != ARCHIVE_OK)
		free(cpio);
	return (ARCHIVE_OK);
}


static int
archive_read_format_cpio_bid(struct archive_read *a, int best_bid)
{
	const unsigned char *p;
	struct cpio *cpio;
	int bid;

	(void)best_bid; /* UNUSED */

	cpio = (struct cpio *)(a->format->data);

	if ((p = __archive_read_ahead(a, 6, NULL)) == NULL)
		return (-1);

	bid = 0;
	if (memcmp(p, "070707", 6) == 0) {
		/* ASCII cpio archive (odc, POSIX.1) */
		cpio->read_header = header_odc;
		bid += 48;
		/*
		 * XXX TODO:  More verification; Could check that only octal
		 * digits appear in appropriate header locations. XXX
		 */
	} else if (memcmp(p, "070727", 6) == 0) {
		/* afio large ASCII cpio archive */
		cpio->read_header = header_odc;
		bid += 48;
		/*
		 * XXX TODO:  More verification; Could check that almost hex
		 * digits appear in appropriate header locations. XXX
		 */
	} else if (memcmp(p, "070701", 6) == 0) {
		/* ASCII cpio archive (SVR4 without CRC) */
		cpio->read_header = header_newc;
		bid += 48;
		/*
		 * XXX TODO:  More verification; Could check that only hex
		 * digits appear in appropriate header locations. XXX
		 */
	} else if (memcmp(p, "070702", 6) == 0) {
		/* ASCII cpio archive (SVR4 with CRC) */
		/* XXX TODO: Flag that we should check the CRC. XXX */
		cpio->read_header = header_newc;
		bid += 48;
		/*
		 * XXX TODO:  More verification; Could check that only hex
		 * digits appear in appropriate header locations. XXX
		 */
	} else if (p[0] * 256 + p[1] == 070707) {
		/* big-endian binary cpio archives */
		cpio->read_header = header_bin_be;
		bid += 16;
		/* Is more verification possible here? */
	} else if (p[0] + p[1] * 256 == 070707) {
		/* little-endian binary cpio archives */
		cpio->read_header = header_bin_le;
		bid += 16;
		/* Is more verification possible here? */
	} else
		return (ARCHIVE_WARN);

	return (bid);
}

static int
archive_read_format_cpio_options(struct archive_read *a,
    const char *key, const char *val)
{
	struct cpio *cpio;
	int ret = ARCHIVE_FAILED;

	cpio = (struct cpio *)(a->format->data);
	if (strcmp(key, "compat-2x")  == 0) {
		/* Handle filenames as libarchive 2.x */
		cpio->init_default_conversion = (val != NULL)?1:0;
		return (ARCHIVE_OK);
	} else if (strcmp(key, "hdrcharset")  == 0) {
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "cpio: hdrcharset option needs a character-set name");
		else {
			cpio->opt_sconv =
			    archive_string_conversion_from_charset(
				&a->archive, val, 0);
			if (cpio->opt_sconv != NULL)
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
archive_read_format_cpio_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	struct cpio *cpio;
	const void *h, *hl;
	struct archive_string_conv *sconv;
	size_t namelength;
	size_t name_pad;
	int r;

	cpio = (struct cpio *)(a->format->data);
	sconv = cpio->opt_sconv;
	if (sconv == NULL) {
		if (!cpio->init_default_conversion) {
			cpio->sconv_default =
			    archive_string_default_conversion_for_read(
			      &(a->archive));
			cpio->init_default_conversion = 1;
		}
		sconv = cpio->sconv_default;
	}
	
	r = (cpio->read_header(a, cpio, entry, &namelength, &name_pad));

	if (r < ARCHIVE_WARN)
		return (r);

	/* Read name from buffer. */
	h = __archive_read_ahead(a, namelength + name_pad, NULL);
	if (h == NULL)
	    return (ARCHIVE_FATAL);
	if (archive_entry_copy_pathname_l(entry,
	    (const char *)h, namelength, sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Pathname can't be converted from %s to current locale.",
		    archive_string_conversion_charset_name(sconv));
		r = ARCHIVE_WARN;
	}
	cpio->entry_offset = 0;

	__archive_read_consume(a, namelength + name_pad);

	/* If this is a symlink, read the link contents. */
	if (archive_entry_filetype(entry) == AE_IFLNK) {
		if (cpio->entry_bytes_remaining > 1024 * 1024) {
			archive_set_error(&a->archive, ENOMEM,
			    "Rejecting malformed cpio archive: symlink contents exceed 1 megabyte");
			return (ARCHIVE_FATAL);
		}
		hl = __archive_read_ahead(a,
			(size_t)cpio->entry_bytes_remaining, NULL);
		if (hl == NULL)
			return (ARCHIVE_FATAL);
		if (archive_entry_copy_symlink_l(entry, (const char *)hl,
		    (size_t)cpio->entry_bytes_remaining, sconv) != 0) {
			if (errno == ENOMEM) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory for Linkname");
				return (ARCHIVE_FATAL);
			}
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Linkname can't be converted from %s to "
			    "current locale.",
			    archive_string_conversion_charset_name(sconv));
			r = ARCHIVE_WARN;
		}
		__archive_read_consume(a, cpio->entry_bytes_remaining);
		cpio->entry_bytes_remaining = 0;
	}

	/* XXX TODO: If the full mode is 0160200, then this is a Solaris
	 * ACL description for the following entry.  Read this body
	 * and parse it as a Solaris-style ACL, then read the next
	 * header.  XXX */

	/* Compare name to "TRAILER!!!" to test for end-of-archive. */
	if (namelength == 11 && strncmp((const char *)h, "TRAILER!!!",
	    11) == 0) {
		/* TODO: Store file location of start of block. */
		archive_clear_error(&a->archive);
		return (ARCHIVE_EOF);
	}

	/* Detect and record hardlinks to previously-extracted entries. */
	if (record_hardlink(a, cpio, entry) != ARCHIVE_OK) {
		return (ARCHIVE_FATAL);
	}

	return (r);
}

static int
archive_read_format_cpio_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	ssize_t bytes_read;
	struct cpio *cpio;

	cpio = (struct cpio *)(a->format->data);

	if (cpio->entry_bytes_unconsumed) {
		__archive_read_consume(a, cpio->entry_bytes_unconsumed);
		cpio->entry_bytes_unconsumed = 0;
	}

	if (cpio->entry_bytes_remaining > 0) {
		*buff = __archive_read_ahead(a, 1, &bytes_read);
		if (bytes_read <= 0)
			return (ARCHIVE_FATAL);
		if (bytes_read > cpio->entry_bytes_remaining)
			bytes_read = (ssize_t)cpio->entry_bytes_remaining;
		*size = bytes_read;
		cpio->entry_bytes_unconsumed = bytes_read;
		*offset = cpio->entry_offset;
		cpio->entry_offset += bytes_read;
		cpio->entry_bytes_remaining -= bytes_read;
		return (ARCHIVE_OK);
	} else {
		if (cpio->entry_padding !=
			__archive_read_consume(a, cpio->entry_padding)) {
			return (ARCHIVE_FATAL);
		}
		cpio->entry_padding = 0;
		*buff = NULL;
		*size = 0;
		*offset = cpio->entry_offset;
		return (ARCHIVE_EOF);
	}
}

static int
archive_read_format_cpio_skip(struct archive_read *a)
{
	struct cpio *cpio = (struct cpio *)(a->format->data);
	int64_t to_skip = cpio->entry_bytes_remaining + cpio->entry_padding +
		cpio->entry_bytes_unconsumed;

	if (to_skip != __archive_read_consume(a, to_skip)) {
		return (ARCHIVE_FATAL);
	}
	cpio->entry_bytes_remaining = 0;
	cpio->entry_padding = 0;
	cpio->entry_bytes_unconsumed = 0;
	return (ARCHIVE_OK);
}

/*
 * Skip forward to the next cpio newc header by searching for the
 * 07070[12] string.  This should be generalized and merged with
 * find_odc_header below.
 */
static int
is_hex(const char *p, size_t len)
{
	while (len-- > 0) {
		if ((*p >= '0' && *p <= '9')
		    || (*p >= 'a' && *p <= 'f')
		    || (*p >= 'A' && *p <= 'F'))
			++p;
		else
			return (0);
	}
	return (1);
}

static int
find_newc_header(struct archive_read *a)
{
	const void *h;
	const char *p, *q;
	size_t skip, skipped = 0;
	ssize_t bytes;

	for (;;) {
		h = __archive_read_ahead(a, newc_header_size, &bytes);
		if (h == NULL)
			return (ARCHIVE_FATAL);
		p = h;
		q = p + bytes;

		/* Try the typical case first, then go into the slow search.*/
		if (memcmp("07070", p, 5) == 0
		    && (p[5] == '1' || p[5] == '2')
		    && is_hex(p, newc_header_size))
			return (ARCHIVE_OK);

		/*
		 * Scan ahead until we find something that looks
		 * like a newc header.
		 */
		while (p + newc_header_size <= q) {
			switch (p[5]) {
			case '1':
			case '2':
				if (memcmp("07070", p, 5) == 0
				    && is_hex(p, newc_header_size)) {
					skip = p - (const char *)h;
					__archive_read_consume(a, skip);
					skipped += skip;
					if (skipped > 0) {
						archive_set_error(&a->archive,
						    0,
						    "Skipped %d bytes before "
						    "finding valid header",
						    (int)skipped);
						return (ARCHIVE_WARN);
					}
					return (ARCHIVE_OK);
				}
				p += 2;
				break;
			case '0':
				p++;
				break;
			default:
				p += 6;
				break;
			}
		}
		skip = p - (const char *)h;
		__archive_read_consume(a, skip);
		skipped += skip;
	}
}

static int
header_newc(struct archive_read *a, struct cpio *cpio,
    struct archive_entry *entry, size_t *namelength, size_t *name_pad)
{
	const void *h;
	const char *header;
	int r;

	r = find_newc_header(a);
	if (r < ARCHIVE_WARN)
		return (r);

	/* Read fixed-size portion of header. */
	h = __archive_read_ahead(a, newc_header_size, NULL);
	if (h == NULL)
	    return (ARCHIVE_FATAL);

	/* Parse out hex fields. */
	header = (const char *)h;

	if (memcmp(header + newc_magic_offset, "070701", 6) == 0) {
		a->archive.archive_format = ARCHIVE_FORMAT_CPIO_SVR4_NOCRC;
		a->archive.archive_format_name = "ASCII cpio (SVR4 with no CRC)";
	} else if (memcmp(header + newc_magic_offset, "070702", 6) == 0) {
		a->archive.archive_format = ARCHIVE_FORMAT_CPIO_SVR4_CRC;
		a->archive.archive_format_name = "ASCII cpio (SVR4 with CRC)";
	} else {
		/* TODO: Abort here? */
	}

	archive_entry_set_devmajor(entry,
		(dev_t)atol16(header + newc_devmajor_offset, newc_devmajor_size));
	archive_entry_set_devminor(entry, 
		(dev_t)atol16(header + newc_devminor_offset, newc_devminor_size));
	archive_entry_set_ino(entry, atol16(header + newc_ino_offset, newc_ino_size));
	archive_entry_set_mode(entry, 
		(mode_t)atol16(header + newc_mode_offset, newc_mode_size));
	archive_entry_set_uid(entry, atol16(header + newc_uid_offset, newc_uid_size));
	archive_entry_set_gid(entry, atol16(header + newc_gid_offset, newc_gid_size));
	archive_entry_set_nlink(entry,
		(unsigned int)atol16(header + newc_nlink_offset, newc_nlink_size));
	archive_entry_set_rdevmajor(entry,
		(dev_t)atol16(header + newc_rdevmajor_offset, newc_rdevmajor_size));
	archive_entry_set_rdevminor(entry,
		(dev_t)atol16(header + newc_rdevminor_offset, newc_rdevminor_size));
	archive_entry_set_mtime(entry, atol16(header + newc_mtime_offset, newc_mtime_size), 0);
	*namelength = (size_t)atol16(header + newc_namesize_offset, newc_namesize_size);
	/* Pad name to 2 more than a multiple of 4. */
	*name_pad = (2 - *namelength) & 3;

	/* Make sure that the padded name length fits into size_t. */
	if (*name_pad > SIZE_MAX - *namelength) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "cpio archive has invalid namelength");
		return (ARCHIVE_FATAL);
	}

	/*
	 * Note: entry_bytes_remaining is at least 64 bits and
	 * therefore guaranteed to be big enough for a 33-bit file
	 * size.
	 */
	cpio->entry_bytes_remaining =
	    atol16(header + newc_filesize_offset, newc_filesize_size);
	archive_entry_set_size(entry, cpio->entry_bytes_remaining);
	/* Pad file contents to a multiple of 4. */
	cpio->entry_padding = 3 & -cpio->entry_bytes_remaining;
	__archive_read_consume(a, newc_header_size);
	return (r);
}

/*
 * Skip forward to the next cpio odc header by searching for the
 * 070707 string.  This is a hand-optimized search that could
 * probably be easily generalized to handle all character-based
 * cpio variants.
 */
static int
is_octal(const char *p, size_t len)
{
	while (len-- > 0) {
		if (*p < '0' || *p > '7')
			return (0);
	        ++p;
	}
	return (1);
}

static int
is_afio_large(const char *h, size_t len)
{
	if (len < afiol_header_size)
		return (0);
	if (h[afiol_ino_m_offset] != 'm'
	    || h[afiol_mtime_n_offset] != 'n'
	    || h[afiol_xsize_s_offset] != 's'
	    || h[afiol_filesize_c_offset] != ':')
		return (0);
	if (!is_hex(h + afiol_dev_offset, afiol_ino_m_offset - afiol_dev_offset))
		return (0);
	if (!is_hex(h + afiol_mode_offset, afiol_mtime_n_offset - afiol_mode_offset))
		return (0);
	if (!is_hex(h + afiol_namesize_offset, afiol_xsize_s_offset - afiol_namesize_offset))
		return (0);
	if (!is_hex(h + afiol_filesize_offset, afiol_filesize_size))
		return (0);
	return (1);
}

static int
find_odc_header(struct archive_read *a)
{
	const void *h;
	const char *p, *q;
	size_t skip, skipped = 0;
	ssize_t bytes;

	for (;;) {
		h = __archive_read_ahead(a, odc_header_size, &bytes);
		if (h == NULL)
			return (ARCHIVE_FATAL);
		p = h;
		q = p + bytes;

		/* Try the typical case first, then go into the slow search.*/
		if (memcmp("070707", p, 6) == 0 && is_octal(p, odc_header_size))
			return (ARCHIVE_OK);
		if (memcmp("070727", p, 6) == 0 && is_afio_large(p, bytes)) {
			a->archive.archive_format = ARCHIVE_FORMAT_CPIO_AFIO_LARGE;
			return (ARCHIVE_OK);
		}

		/*
		 * Scan ahead until we find something that looks
		 * like an odc header.
		 */
		while (p + odc_header_size <= q) {
			switch (p[5]) {
			case '7':
				if ((memcmp("070707", p, 6) == 0
				    && is_octal(p, odc_header_size))
				    || (memcmp("070727", p, 6) == 0
				        && is_afio_large(p, q - p))) {
					skip = p - (const char *)h;
					__archive_read_consume(a, skip);
					skipped += skip;
					if (p[4] == '2')
						a->archive.archive_format =
						    ARCHIVE_FORMAT_CPIO_AFIO_LARGE;
					if (skipped > 0) {
						archive_set_error(&a->archive,
						    0,
						    "Skipped %d bytes before "
						    "finding valid header",
						    (int)skipped);
						return (ARCHIVE_WARN);
					}
					return (ARCHIVE_OK);
				}
				p += 2;
				break;
			case '0':
				p++;
				break;
			default:
				p += 6;
				break;
			}
		}
		skip = p - (const char *)h;
		__archive_read_consume(a, skip);
		skipped += skip;
	}
}

static int
header_odc(struct archive_read *a, struct cpio *cpio,
    struct archive_entry *entry, size_t *namelength, size_t *name_pad)
{
	const void *h;
	int r;
	const char *header;

	a->archive.archive_format = ARCHIVE_FORMAT_CPIO_POSIX;
	a->archive.archive_format_name = "POSIX octet-oriented cpio";

	/* Find the start of the next header. */
	r = find_odc_header(a);
	if (r < ARCHIVE_WARN)
		return (r);

	if (a->archive.archive_format == ARCHIVE_FORMAT_CPIO_AFIO_LARGE) {
		int r2 = (header_afiol(a, cpio, entry, namelength, name_pad));
		if (r2 == ARCHIVE_OK)
			return (r);
		else
			return (r2);
	}

	/* Read fixed-size portion of header. */
	h = __archive_read_ahead(a, odc_header_size, NULL);
	if (h == NULL)
	    return (ARCHIVE_FATAL);

	/* Parse out octal fields. */
	header = (const char *)h;

	archive_entry_set_dev(entry, 
		(dev_t)atol8(header + odc_dev_offset, odc_dev_size));
	archive_entry_set_ino(entry, atol8(header + odc_ino_offset, odc_ino_size));
	archive_entry_set_mode(entry, 
		(mode_t)atol8(header + odc_mode_offset, odc_mode_size));
	archive_entry_set_uid(entry, atol8(header + odc_uid_offset, odc_uid_size));
	archive_entry_set_gid(entry, atol8(header + odc_gid_offset, odc_gid_size));
	archive_entry_set_nlink(entry, 
		(unsigned int)atol8(header + odc_nlink_offset, odc_nlink_size));
	archive_entry_set_rdev(entry,
		(dev_t)atol8(header + odc_rdev_offset, odc_rdev_size));
	archive_entry_set_mtime(entry, atol8(header + odc_mtime_offset, odc_mtime_size), 0);
	*namelength = (size_t)atol8(header + odc_namesize_offset, odc_namesize_size);
	*name_pad = 0; /* No padding of filename. */

	/*
	 * Note: entry_bytes_remaining is at least 64 bits and
	 * therefore guaranteed to be big enough for a 33-bit file
	 * size.
	 */
	cpio->entry_bytes_remaining =
	    atol8(header + odc_filesize_offset, odc_filesize_size);
	archive_entry_set_size(entry, cpio->entry_bytes_remaining);
	cpio->entry_padding = 0;
	__archive_read_consume(a, odc_header_size);
	return (r);
}

/*
 * NOTE: if a filename suffix is ".z", it is the file gziped by afio.
 * it would be nice that we can show uncompressed file size and we can
 * uncompressed file contents automatically, unfortunately we have nothing
 * to get a uncompressed file size while reading each header. It means
 * we also cannot uncompress file contents under our framework.
 */
static int
header_afiol(struct archive_read *a, struct cpio *cpio,
    struct archive_entry *entry, size_t *namelength, size_t *name_pad)
{
	const void *h;
	const char *header;

	a->archive.archive_format = ARCHIVE_FORMAT_CPIO_AFIO_LARGE;
	a->archive.archive_format_name = "afio large ASCII";

	/* Read fixed-size portion of header. */
	h = __archive_read_ahead(a, afiol_header_size, NULL);
	if (h == NULL)
	    return (ARCHIVE_FATAL);

	/* Parse out octal fields. */
	header = (const char *)h;

	archive_entry_set_dev(entry, 
		(dev_t)atol16(header + afiol_dev_offset, afiol_dev_size));
	archive_entry_set_ino(entry, atol16(header + afiol_ino_offset, afiol_ino_size));
	archive_entry_set_mode(entry,
		(mode_t)atol8(header + afiol_mode_offset, afiol_mode_size));
	archive_entry_set_uid(entry, atol16(header + afiol_uid_offset, afiol_uid_size));
	archive_entry_set_gid(entry, atol16(header + afiol_gid_offset, afiol_gid_size));
	archive_entry_set_nlink(entry,
		(unsigned int)atol16(header + afiol_nlink_offset, afiol_nlink_size));
	archive_entry_set_rdev(entry,
		(dev_t)atol16(header + afiol_rdev_offset, afiol_rdev_size));
	archive_entry_set_mtime(entry, atol16(header + afiol_mtime_offset, afiol_mtime_size), 0);
	*namelength = (size_t)atol16(header + afiol_namesize_offset, afiol_namesize_size);
	*name_pad = 0; /* No padding of filename. */

	cpio->entry_bytes_remaining =
	    atol16(header + afiol_filesize_offset, afiol_filesize_size);
	archive_entry_set_size(entry, cpio->entry_bytes_remaining);
	cpio->entry_padding = 0;
	__archive_read_consume(a, afiol_header_size);
	return (ARCHIVE_OK);
}


static int
header_bin_le(struct archive_read *a, struct cpio *cpio,
    struct archive_entry *entry, size_t *namelength, size_t *name_pad)
{
	const void *h;
	const unsigned char *header;

	a->archive.archive_format = ARCHIVE_FORMAT_CPIO_BIN_LE;
	a->archive.archive_format_name = "cpio (little-endian binary)";

	/* Read fixed-size portion of header. */
	h = __archive_read_ahead(a, bin_header_size, NULL);
	if (h == NULL) {
	    archive_set_error(&a->archive, 0,
		"End of file trying to read next cpio header");
	    return (ARCHIVE_FATAL);
	}

	/* Parse out binary fields. */
	header = (const unsigned char *)h;

	archive_entry_set_dev(entry, header[bin_dev_offset] + header[bin_dev_offset + 1] * 256);
	archive_entry_set_ino(entry, header[bin_ino_offset] + header[bin_ino_offset + 1] * 256);
	archive_entry_set_mode(entry, header[bin_mode_offset] + header[bin_mode_offset + 1] * 256);
	archive_entry_set_uid(entry, header[bin_uid_offset] + header[bin_uid_offset + 1] * 256);
	archive_entry_set_gid(entry, header[bin_gid_offset] + header[bin_gid_offset + 1] * 256);
	archive_entry_set_nlink(entry, header[bin_nlink_offset] + header[bin_nlink_offset + 1] * 256);
	archive_entry_set_rdev(entry, header[bin_rdev_offset] + header[bin_rdev_offset + 1] * 256);
	archive_entry_set_mtime(entry, le4(header + bin_mtime_offset), 0);
	*namelength = header[bin_namesize_offset] + header[bin_namesize_offset + 1] * 256;
	*name_pad = *namelength & 1; /* Pad to even. */

	cpio->entry_bytes_remaining = le4(header + bin_filesize_offset);
	archive_entry_set_size(entry, cpio->entry_bytes_remaining);
	cpio->entry_padding = cpio->entry_bytes_remaining & 1; /* Pad to even. */
	__archive_read_consume(a, bin_header_size);
	return (ARCHIVE_OK);
}

static int
header_bin_be(struct archive_read *a, struct cpio *cpio,
    struct archive_entry *entry, size_t *namelength, size_t *name_pad)
{
	const void *h;
	const unsigned char *header;

	a->archive.archive_format = ARCHIVE_FORMAT_CPIO_BIN_BE;
	a->archive.archive_format_name = "cpio (big-endian binary)";

	/* Read fixed-size portion of header. */
	h = __archive_read_ahead(a, bin_header_size, NULL);
	if (h == NULL) {
	    archive_set_error(&a->archive, 0,
		"End of file trying to read next cpio header");
	    return (ARCHIVE_FATAL);
	}

	/* Parse out binary fields. */
	header = (const unsigned char *)h;

	archive_entry_set_dev(entry, header[bin_dev_offset] * 256 + header[bin_dev_offset + 1]);
	archive_entry_set_ino(entry, header[bin_ino_offset] * 256 + header[bin_ino_offset + 1]);
	archive_entry_set_mode(entry, header[bin_mode_offset] * 256 + header[bin_mode_offset + 1]);
	archive_entry_set_uid(entry, header[bin_uid_offset] * 256 + header[bin_uid_offset + 1]);
	archive_entry_set_gid(entry, header[bin_gid_offset] * 256 + header[bin_gid_offset + 1]);
	archive_entry_set_nlink(entry, header[bin_nlink_offset] * 256 + header[bin_nlink_offset + 1]);
	archive_entry_set_rdev(entry, header[bin_rdev_offset] * 256 + header[bin_rdev_offset + 1]);
	archive_entry_set_mtime(entry, be4(header + bin_mtime_offset), 0);
	*namelength = header[bin_namesize_offset] * 256 + header[bin_namesize_offset + 1];
	*name_pad = *namelength & 1; /* Pad to even. */

	cpio->entry_bytes_remaining = be4(header + bin_filesize_offset);
	archive_entry_set_size(entry, cpio->entry_bytes_remaining);
	cpio->entry_padding = cpio->entry_bytes_remaining & 1; /* Pad to even. */
	    __archive_read_consume(a, bin_header_size);
	return (ARCHIVE_OK);
}

static int
archive_read_format_cpio_cleanup(struct archive_read *a)
{
	struct cpio *cpio;

	cpio = (struct cpio *)(a->format->data);
        /* Free inode->name map */
        while (cpio->links_head != NULL) {
                struct links_entry *lp = cpio->links_head->next;

                free(cpio->links_head->name);
                free(cpio->links_head);
                cpio->links_head = lp;
        }
	free(cpio);
	(a->format->data) = NULL;
	return (ARCHIVE_OK);
}

static int64_t
le4(const unsigned char *p)
{
	return ((p[0] << 16) + (((int64_t)p[1]) << 24) + (p[2] << 0) + (p[3] << 8));
}


static int64_t
be4(const unsigned char *p)
{
	return ((((int64_t)p[0]) << 24) + (p[1] << 16) + (p[2] << 8) + (p[3]));
}

/*
 * Note that this implementation does not (and should not!) obey
 * locale settings; you cannot simply substitute strtol here, since
 * it does obey locale.
 */
static int64_t
atol8(const char *p, unsigned char_cnt)
{
	int64_t l;
	int digit;

	l = 0;
	while (char_cnt-- > 0) {
		if (*p >= '0' && *p <= '7')
			digit = *p - '0';
		else
			return (l);
		p++;
		l <<= 3;
		l |= digit;
	}
	return (l);
}

static int64_t
atol16(const char *p, unsigned char_cnt)
{
	int64_t l;
	int digit;

	l = 0;
	while (char_cnt-- > 0) {
		if (*p >= 'a' && *p <= 'f')
			digit = *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'F')
			digit = *p - 'A' + 10;
		else if (*p >= '0' && *p <= '9')
			digit = *p - '0';
		else
			return (l);
		p++;
		l <<= 4;
		l |= digit;
	}
	return (l);
}

static int
record_hardlink(struct archive_read *a,
    struct cpio *cpio, struct archive_entry *entry)
{
	struct links_entry      *le;
	dev_t dev;
	int64_t ino;

	if (archive_entry_nlink(entry) <= 1)
		return (ARCHIVE_OK);

	dev = archive_entry_dev(entry);
	ino = archive_entry_ino64(entry);

	/*
	 * First look in the list of multiply-linked files.  If we've
	 * already dumped it, convert this entry to a hard link entry.
	 */
	for (le = cpio->links_head; le; le = le->next) {
		if (le->dev == dev && le->ino == ino) {
			archive_entry_copy_hardlink(entry, le->name);

			if (--le->links <= 0) {
				if (le->previous != NULL)
					le->previous->next = le->next;
				if (le->next != NULL)
					le->next->previous = le->previous;
				if (cpio->links_head == le)
					cpio->links_head = le->next;
				free(le->name);
				free(le);
			}

			return (ARCHIVE_OK);
		}
	}

	le = (struct links_entry *)malloc(sizeof(struct links_entry));
	if (le == NULL) {
		archive_set_error(&a->archive,
		    ENOMEM, "Out of memory adding file to list");
		return (ARCHIVE_FATAL);
	}
	if (cpio->links_head != NULL)
		cpio->links_head->previous = le;
	le->next = cpio->links_head;
	le->previous = NULL;
	cpio->links_head = le;
	le->dev = dev;
	le->ino = ino;
	le->links = archive_entry_nlink(entry) - 1;
	le->name = strdup(archive_entry_pathname(entry));
	if (le->name == NULL) {
		archive_set_error(&a->archive,
		    ENOMEM, "Out of memory adding file to list");
		return (ARCHIVE_FATAL);
	}

	return (ARCHIVE_OK);
}
