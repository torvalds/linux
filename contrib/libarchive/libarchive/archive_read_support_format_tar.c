/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2011-2012 Michihiro NAKAJIMA
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
#include <stddef.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_acl_private.h" /* For ACL parsing routines. */
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_read_private.h"

#define tar_min(a,b) ((a) < (b) ? (a) : (b))

/*
 * Layout of POSIX 'ustar' tar header.
 */
struct archive_entry_header_ustar {
	char	name[100];
	char	mode[8];
	char	uid[8];
	char	gid[8];
	char	size[12];
	char	mtime[12];
	char	checksum[8];
	char	typeflag[1];
	char	linkname[100];	/* "old format" header ends here */
	char	magic[6];	/* For POSIX: "ustar\0" */
	char	version[2];	/* For POSIX: "00" */
	char	uname[32];
	char	gname[32];
	char	rdevmajor[8];
	char	rdevminor[8];
	char	prefix[155];
};

/*
 * Structure of GNU tar header
 */
struct gnu_sparse {
	char	offset[12];
	char	numbytes[12];
};

struct archive_entry_header_gnutar {
	char	name[100];
	char	mode[8];
	char	uid[8];
	char	gid[8];
	char	size[12];
	char	mtime[12];
	char	checksum[8];
	char	typeflag[1];
	char	linkname[100];
	char	magic[8];  /* "ustar  \0" (note blank/blank/null at end) */
	char	uname[32];
	char	gname[32];
	char	rdevmajor[8];
	char	rdevminor[8];
	char	atime[12];
	char	ctime[12];
	char	offset[12];
	char	longnames[4];
	char	unused[1];
	struct gnu_sparse sparse[4];
	char	isextended[1];
	char	realsize[12];
	/*
	 * Old GNU format doesn't use POSIX 'prefix' field; they use
	 * the 'L' (longname) entry instead.
	 */
};

/*
 * Data specific to this format.
 */
struct sparse_block {
	struct sparse_block	*next;
	int64_t	offset;
	int64_t	remaining;
	int hole;
};

struct tar {
	struct archive_string	 acl_text;
	struct archive_string	 entry_pathname;
	/* For "GNU.sparse.name" and other similar path extensions. */
	struct archive_string	 entry_pathname_override;
	struct archive_string	 entry_linkpath;
	struct archive_string	 entry_uname;
	struct archive_string	 entry_gname;
	struct archive_string	 longlink;
	struct archive_string	 longname;
	struct archive_string	 pax_header;
	struct archive_string	 pax_global;
	struct archive_string	 line;
	int			 pax_hdrcharset_binary;
	int			 header_recursion_depth;
	int64_t			 entry_bytes_remaining;
	int64_t			 entry_offset;
	int64_t			 entry_padding;
	int64_t 		 entry_bytes_unconsumed;
	int64_t			 realsize;
	int			 sparse_allowed;
	struct sparse_block	*sparse_list;
	struct sparse_block	*sparse_last;
	int64_t			 sparse_offset;
	int64_t			 sparse_numbytes;
	int			 sparse_gnu_major;
	int			 sparse_gnu_minor;
	char			 sparse_gnu_pending;

	struct archive_string	 localname;
	struct archive_string_conv *opt_sconv;
	struct archive_string_conv *sconv;
	struct archive_string_conv *sconv_acl;
	struct archive_string_conv *sconv_default;
	int			 init_default_conversion;
	int			 compat_2x;
	int			 process_mac_extensions;
	int			 read_concatenated_archives;
	int			 realsize_override;
};

static int	archive_block_is_null(const char *p);
static char	*base64_decode(const char *, size_t, size_t *);
static int	gnu_add_sparse_entry(struct archive_read *, struct tar *,
		    int64_t offset, int64_t remaining);

static void	gnu_clear_sparse_list(struct tar *);
static int	gnu_sparse_old_read(struct archive_read *, struct tar *,
		    const struct archive_entry_header_gnutar *header, size_t *);
static int	gnu_sparse_old_parse(struct archive_read *, struct tar *,
		    const struct gnu_sparse *sparse, int length);
static int	gnu_sparse_01_parse(struct archive_read *, struct tar *,
		    const char *);
static ssize_t	gnu_sparse_10_read(struct archive_read *, struct tar *,
			size_t *);
static int	header_Solaris_ACL(struct archive_read *,  struct tar *,
		    struct archive_entry *, const void *, size_t *);
static int	header_common(struct archive_read *,  struct tar *,
		    struct archive_entry *, const void *);
static int	header_old_tar(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *);
static int	header_pax_extensions(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *, size_t *);
static int	header_pax_global(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	header_longlink(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	header_longname(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	read_mac_metadata_blob(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	header_volume(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	header_ustar(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h);
static int	header_gnutar(struct archive_read *, struct tar *,
		    struct archive_entry *, const void *h, size_t *);
static int	archive_read_format_tar_bid(struct archive_read *, int);
static int	archive_read_format_tar_options(struct archive_read *,
		    const char *, const char *);
static int	archive_read_format_tar_cleanup(struct archive_read *);
static int	archive_read_format_tar_read_data(struct archive_read *a,
		    const void **buff, size_t *size, int64_t *offset);
static int	archive_read_format_tar_skip(struct archive_read *a);
static int	archive_read_format_tar_read_header(struct archive_read *,
		    struct archive_entry *);
static int	checksum(struct archive_read *, const void *);
static int 	pax_attribute(struct archive_read *, struct tar *,
		    struct archive_entry *, const char *key, const char *value,
		    size_t value_length);
static int	pax_attribute_acl(struct archive_read *, struct tar *,
		    struct archive_entry *, const char *, int);
static int	pax_attribute_xattr(struct archive_entry *, const char *,
		    const char *);
static int 	pax_header(struct archive_read *, struct tar *,
		    struct archive_entry *, struct archive_string *);
static void	pax_time(const char *, int64_t *sec, long *nanos);
static ssize_t	readline(struct archive_read *, struct tar *, const char **,
		    ssize_t limit, size_t *);
static int	read_body_to_string(struct archive_read *, struct tar *,
		    struct archive_string *, const void *h, size_t *);
static int	solaris_sparse_parse(struct archive_read *, struct tar *,
		    struct archive_entry *, const char *);
static int64_t	tar_atol(const char *, size_t);
static int64_t	tar_atol10(const char *, size_t);
static int64_t	tar_atol256(const char *, size_t);
static int64_t	tar_atol8(const char *, size_t);
static int	tar_read_header(struct archive_read *, struct tar *,
		    struct archive_entry *, size_t *);
static int	tohex(int c);
static char	*url_decode(const char *);
static void	tar_flush_unconsumed(struct archive_read *, size_t *);


int
archive_read_support_format_gnutar(struct archive *a)
{
	archive_check_magic(a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_gnutar");
	return (archive_read_support_format_tar(a));
}


int
archive_read_support_format_tar(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct tar *tar;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_tar");

	tar = (struct tar *)calloc(1, sizeof(*tar));
	if (tar == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate tar data");
		return (ARCHIVE_FATAL);
	}
#ifdef HAVE_COPYFILE_H
	/* Set this by default on Mac OS. */
	tar->process_mac_extensions = 1;
#endif

	r = __archive_read_register_format(a, tar, "tar",
	    archive_read_format_tar_bid,
	    archive_read_format_tar_options,
	    archive_read_format_tar_read_header,
	    archive_read_format_tar_read_data,
	    archive_read_format_tar_skip,
	    NULL,
	    archive_read_format_tar_cleanup,
	    NULL,
	    NULL);

	if (r != ARCHIVE_OK)
		free(tar);
	return (ARCHIVE_OK);
}

static int
archive_read_format_tar_cleanup(struct archive_read *a)
{
	struct tar *tar;

	tar = (struct tar *)(a->format->data);
	gnu_clear_sparse_list(tar);
	archive_string_free(&tar->acl_text);
	archive_string_free(&tar->entry_pathname);
	archive_string_free(&tar->entry_pathname_override);
	archive_string_free(&tar->entry_linkpath);
	archive_string_free(&tar->entry_uname);
	archive_string_free(&tar->entry_gname);
	archive_string_free(&tar->line);
	archive_string_free(&tar->pax_global);
	archive_string_free(&tar->pax_header);
	archive_string_free(&tar->longname);
	archive_string_free(&tar->longlink);
	archive_string_free(&tar->localname);
	free(tar);
	(a->format->data) = NULL;
	return (ARCHIVE_OK);
}

/*
 * Validate number field
 *
 * This has to be pretty lenient in order to accommodate the enormous
 * variety of tar writers in the world:
 *  = POSIX (IEEE Std 1003.1-1988) ustar requires octal values with leading
 *    zeros and allows fields to be terminated with space or null characters
 *  = Many writers use different termination (in particular, libarchive
 *    omits terminator bytes to squeeze one or two more digits)
 *  = Many writers pad with space and omit leading zeros
 *  = GNU tar and star write base-256 values if numbers are too
 *    big to be represented in octal
 *
 *  Examples of specific tar headers that we should support:
 *  = Perl Archive::Tar terminates uid, gid, devminor and devmajor with two
 *    null bytes, pads size with spaces and other numeric fields with zeroes
 *  = plexus-archiver prior to 2.6.3 (before switching to commons-compress)
 *    may have uid and gid fields filled with spaces without any octal digits
 *    at all and pads all numeric fields with spaces
 *
 * This should tolerate all variants in use.  It will reject a field
 * where the writer just left garbage after a trailing NUL.
 */
static int
validate_number_field(const char* p_field, size_t i_size)
{
	unsigned char marker = (unsigned char)p_field[0];
	if (marker == 128 || marker == 255 || marker == 0) {
		/* Base-256 marker, there's nothing we can check. */
		return 1;
	} else {
		/* Must be octal */
		size_t i = 0;
		/* Skip any leading spaces */
		while (i < i_size && p_field[i] == ' ') {
			++i;
		}
		/* Skip octal digits. */
		while (i < i_size && p_field[i] >= '0' && p_field[i] <= '7') {
			++i;
		}
		/* Any remaining characters must be space or NUL padding. */
		while (i < i_size) {
			if (p_field[i] != ' ' && p_field[i] != 0) {
				return 0;
			}
			++i;
		}
		return 1;
	}
}

static int
archive_read_format_tar_bid(struct archive_read *a, int best_bid)
{
	int bid;
	const char *h;
	const struct archive_entry_header_ustar *header;

	(void)best_bid; /* UNUSED */

	bid = 0;

	/* Now let's look at the actual header and see if it matches. */
	h = __archive_read_ahead(a, 512, NULL);
	if (h == NULL)
		return (-1);

	/* If it's an end-of-archive mark, we can handle it. */
	if (h[0] == 0 && archive_block_is_null(h)) {
		/*
		 * Usually, I bid the number of bits verified, but
		 * in this case, 4096 seems excessive so I picked 10 as
		 * an arbitrary but reasonable-seeming value.
		 */
		return (10);
	}

	/* If it's not an end-of-archive mark, it must have a valid checksum.*/
	if (!checksum(a, h))
		return (0);
	bid += 48;  /* Checksum is usually 6 octal digits. */

	header = (const struct archive_entry_header_ustar *)h;

	/* Recognize POSIX formats. */
	if ((memcmp(header->magic, "ustar\0", 6) == 0)
	    && (memcmp(header->version, "00", 2) == 0))
		bid += 56;

	/* Recognize GNU tar format. */
	if ((memcmp(header->magic, "ustar ", 6) == 0)
	    && (memcmp(header->version, " \0", 2) == 0))
		bid += 56;

	/* Type flag must be null, digit or A-Z, a-z. */
	if (header->typeflag[0] != 0 &&
	    !( header->typeflag[0] >= '0' && header->typeflag[0] <= '9') &&
	    !( header->typeflag[0] >= 'A' && header->typeflag[0] <= 'Z') &&
	    !( header->typeflag[0] >= 'a' && header->typeflag[0] <= 'z') )
		return (0);
	bid += 2;  /* 6 bits of variation in an 8-bit field leaves 2 bits. */

	/*
	 * Check format of mode/uid/gid/mtime/size/rdevmajor/rdevminor fields.
	 */
	if (bid > 0 && (
	    validate_number_field(header->mode, sizeof(header->mode)) == 0
	    || validate_number_field(header->uid, sizeof(header->uid)) == 0
	    || validate_number_field(header->gid, sizeof(header->gid)) == 0
	    || validate_number_field(header->mtime, sizeof(header->mtime)) == 0
	    || validate_number_field(header->size, sizeof(header->size)) == 0
	    || validate_number_field(header->rdevmajor, sizeof(header->rdevmajor)) == 0
	    || validate_number_field(header->rdevminor, sizeof(header->rdevminor)) == 0)) {
		bid = 0;
	}

	return (bid);
}

static int
archive_read_format_tar_options(struct archive_read *a,
    const char *key, const char *val)
{
	struct tar *tar;
	int ret = ARCHIVE_FAILED;

	tar = (struct tar *)(a->format->data);
	if (strcmp(key, "compat-2x")  == 0) {
		/* Handle UTF-8 filenames as libarchive 2.x */
		tar->compat_2x = (val != NULL && val[0] != 0);
		tar->init_default_conversion = tar->compat_2x;
		return (ARCHIVE_OK);
	} else if (strcmp(key, "hdrcharset")  == 0) {
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "tar: hdrcharset option needs a character-set name");
		else {
			tar->opt_sconv =
			    archive_string_conversion_from_charset(
				&a->archive, val, 0);
			if (tar->opt_sconv != NULL)
				ret = ARCHIVE_OK;
			else
				ret = ARCHIVE_FATAL;
		}
		return (ret);
	} else if (strcmp(key, "mac-ext") == 0) {
		tar->process_mac_extensions = (val != NULL && val[0] != 0);
		return (ARCHIVE_OK);
	} else if (strcmp(key, "read_concatenated_archives") == 0) {
		tar->read_concatenated_archives = (val != NULL && val[0] != 0);
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

/* utility function- this exists to centralize the logic of tracking
 * how much unconsumed data we have floating around, and to consume
 * anything outstanding since we're going to do read_aheads
 */
static void
tar_flush_unconsumed(struct archive_read *a, size_t *unconsumed)
{
	if (*unconsumed) {
/*
		void *data = (void *)__archive_read_ahead(a, *unconsumed, NULL);
		 * this block of code is to poison claimed unconsumed space, ensuring
		 * things break if it is in use still.
		 * currently it WILL break things, so enable it only for debugging this issue
		if (data) {
			memset(data, 0xff, *unconsumed);
		}
*/
		__archive_read_consume(a, *unconsumed);
		*unconsumed = 0;
	}
}

/*
 * The function invoked by archive_read_next_header().  This
 * just sets up a few things and then calls the internal
 * tar_read_header() function below.
 */
static int
archive_read_format_tar_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	/*
	 * When converting tar archives to cpio archives, it is
	 * essential that each distinct file have a distinct inode
	 * number.  To simplify this, we keep a static count here to
	 * assign fake dev/inode numbers to each tar entry.  Note that
	 * pax format archives may overwrite this with something more
	 * useful.
	 *
	 * Ideally, we would track every file read from the archive so
	 * that we could assign the same dev/ino pair to hardlinks,
	 * but the memory required to store a complete lookup table is
	 * probably not worthwhile just to support the relatively
	 * obscure tar->cpio conversion case.
	 */
	static int default_inode;
	static int default_dev;
	struct tar *tar;
	const char *p;
	const wchar_t *wp;
	int r;
	size_t l, unconsumed = 0;

	/* Assign default device/inode values. */
	archive_entry_set_dev(entry, 1 + default_dev); /* Don't use zero. */
	archive_entry_set_ino(entry, ++default_inode); /* Don't use zero. */
	/* Limit generated st_ino number to 16 bits. */
	if (default_inode >= 0xffff) {
		++default_dev;
		default_inode = 0;
	}

	tar = (struct tar *)(a->format->data);
	tar->entry_offset = 0;
	gnu_clear_sparse_list(tar);
	tar->realsize = -1; /* Mark this as "unset" */
	tar->realsize_override = 0;

	/* Setup default string conversion. */
	tar->sconv = tar->opt_sconv;
	if (tar->sconv == NULL) {
		if (!tar->init_default_conversion) {
			tar->sconv_default =
			    archive_string_default_conversion_for_read(&(a->archive));
			tar->init_default_conversion = 1;
		}
		tar->sconv = tar->sconv_default;
	}

	r = tar_read_header(a, tar, entry, &unconsumed);

	tar_flush_unconsumed(a, &unconsumed);

	/*
	 * "non-sparse" files are really just sparse files with
	 * a single block.
	 */
	if (tar->sparse_list == NULL) {
		if (gnu_add_sparse_entry(a, tar, 0, tar->entry_bytes_remaining)
		    != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	} else {
		struct sparse_block *sb;

		for (sb = tar->sparse_list; sb != NULL; sb = sb->next) {
			if (!sb->hole)
				archive_entry_sparse_add_entry(entry,
				    sb->offset, sb->remaining);
		}
	}

	if (r == ARCHIVE_OK && archive_entry_filetype(entry) == AE_IFREG) {
		/*
		 * "Regular" entry with trailing '/' is really
		 * directory: This is needed for certain old tar
		 * variants and even for some broken newer ones.
		 */
		if ((wp = archive_entry_pathname_w(entry)) != NULL) {
			l = wcslen(wp);
			if (l > 0 && wp[l - 1] == L'/') {
				archive_entry_set_filetype(entry, AE_IFDIR);
			}
		} else if ((p = archive_entry_pathname(entry)) != NULL) {
			l = strlen(p);
			if (l > 0 && p[l - 1] == '/') {
				archive_entry_set_filetype(entry, AE_IFDIR);
			}
		}
	}
	return (r);
}

static int
archive_read_format_tar_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	ssize_t bytes_read;
	struct tar *tar;
	struct sparse_block *p;

	tar = (struct tar *)(a->format->data);

	for (;;) {
		/* Remove exhausted entries from sparse list. */
		while (tar->sparse_list != NULL &&
		    tar->sparse_list->remaining == 0) {
			p = tar->sparse_list;
			tar->sparse_list = p->next;
			free(p);
		}

		if (tar->entry_bytes_unconsumed) {
			__archive_read_consume(a, tar->entry_bytes_unconsumed);
			tar->entry_bytes_unconsumed = 0;
		}

		/* If we're at end of file, return EOF. */
		if (tar->sparse_list == NULL ||
		    tar->entry_bytes_remaining == 0) {
			if (__archive_read_consume(a, tar->entry_padding) < 0)
				return (ARCHIVE_FATAL);
			tar->entry_padding = 0;
			*buff = NULL;
			*size = 0;
			*offset = tar->realsize;
			return (ARCHIVE_EOF);
		}

		*buff = __archive_read_ahead(a, 1, &bytes_read);
		if (bytes_read < 0)
			return (ARCHIVE_FATAL);
		if (*buff == NULL) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Truncated tar archive");
			return (ARCHIVE_FATAL);
		}
		if (bytes_read > tar->entry_bytes_remaining)
			bytes_read = (ssize_t)tar->entry_bytes_remaining;
		/* Don't read more than is available in the
		 * current sparse block. */
		if (tar->sparse_list->remaining < bytes_read)
			bytes_read = (ssize_t)tar->sparse_list->remaining;
		*size = bytes_read;
		*offset = tar->sparse_list->offset;
		tar->sparse_list->remaining -= bytes_read;
		tar->sparse_list->offset += bytes_read;
		tar->entry_bytes_remaining -= bytes_read;
		tar->entry_bytes_unconsumed = bytes_read;

		if (!tar->sparse_list->hole)
			return (ARCHIVE_OK);
		/* Current is hole data and skip this. */
	}
}

static int
archive_read_format_tar_skip(struct archive_read *a)
{
	int64_t bytes_skipped;
	int64_t request;
	struct sparse_block *p;
	struct tar* tar;

	tar = (struct tar *)(a->format->data);

	/* Do not consume the hole of a sparse file. */
	request = 0;
	for (p = tar->sparse_list; p != NULL; p = p->next) {
		if (!p->hole) {
			if (p->remaining >= INT64_MAX - request) {
				return ARCHIVE_FATAL;
			}
			request += p->remaining;
		}
	}
	if (request > tar->entry_bytes_remaining)
		request = tar->entry_bytes_remaining;
	request += tar->entry_padding + tar->entry_bytes_unconsumed;

	bytes_skipped = __archive_read_consume(a, request);
	if (bytes_skipped < 0)
		return (ARCHIVE_FATAL);

	tar->entry_bytes_remaining = 0;
	tar->entry_bytes_unconsumed = 0;
	tar->entry_padding = 0;

	/* Free the sparse list. */
	gnu_clear_sparse_list(tar);

	return (ARCHIVE_OK);
}

/*
 * This function recursively interprets all of the headers associated
 * with a single entry.
 */
static int
tar_read_header(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, size_t *unconsumed)
{
	ssize_t bytes;
	int err;
	const char *h;
	const struct archive_entry_header_ustar *header;
	const struct archive_entry_header_gnutar *gnuheader;

	/* Loop until we find a workable header record. */
	for (;;) {
		tar_flush_unconsumed(a, unconsumed);

		/* Read 512-byte header record */
		h = __archive_read_ahead(a, 512, &bytes);
		if (bytes < 0)
			return ((int)bytes);
		if (bytes == 0) { /* EOF at a block boundary. */
			/* Some writers do omit the block of nulls. <sigh> */
			return (ARCHIVE_EOF);
		}
		if (bytes < 512) {  /* Short block at EOF; this is bad. */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Truncated tar archive");
			return (ARCHIVE_FATAL);
		}
		*unconsumed = 512;

		/* Header is workable if it's not an end-of-archive mark. */
		if (h[0] != 0 || !archive_block_is_null(h))
			break;

		/* Ensure format is set for archives with only null blocks. */
		if (a->archive.archive_format_name == NULL) {
			a->archive.archive_format = ARCHIVE_FORMAT_TAR;
			a->archive.archive_format_name = "tar";
		}

		if (!tar->read_concatenated_archives) {
			/* Try to consume a second all-null record, as well. */
			tar_flush_unconsumed(a, unconsumed);
			h = __archive_read_ahead(a, 512, NULL);
			if (h != NULL && h[0] == 0 && archive_block_is_null(h))
				__archive_read_consume(a, 512);
			archive_clear_error(&a->archive);
			return (ARCHIVE_EOF);
		}

		/*
		 * We're reading concatenated archives, ignore this block and
		 * loop to get the next.
		 */
	}

	/*
	 * Note: If the checksum fails and we return ARCHIVE_RETRY,
	 * then the client is likely to just retry.  This is a very
	 * crude way to search for the next valid header!
	 *
	 * TODO: Improve this by implementing a real header scan.
	 */
	if (!checksum(a, h)) {
		tar_flush_unconsumed(a, unconsumed);
		archive_set_error(&a->archive, EINVAL, "Damaged tar archive");
		return (ARCHIVE_RETRY); /* Retryable: Invalid header */
	}

	if (++tar->header_recursion_depth > 32) {
		tar_flush_unconsumed(a, unconsumed);
		archive_set_error(&a->archive, EINVAL, "Too many special headers");
		return (ARCHIVE_WARN);
	}

	/* Determine the format variant. */
	header = (const struct archive_entry_header_ustar *)h;

	switch(header->typeflag[0]) {
	case 'A': /* Solaris tar ACL */
		a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
		a->archive.archive_format_name = "Solaris tar";
		err = header_Solaris_ACL(a, tar, entry, h, unconsumed);
		break;
	case 'g': /* POSIX-standard 'g' header. */
		a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
		a->archive.archive_format_name = "POSIX pax interchange format";
		err = header_pax_global(a, tar, entry, h, unconsumed);
		if (err == ARCHIVE_EOF)
			return (err);
		break;
	case 'K': /* Long link name (GNU tar, others) */
		err = header_longlink(a, tar, entry, h, unconsumed);
		break;
	case 'L': /* Long filename (GNU tar, others) */
		err = header_longname(a, tar, entry, h, unconsumed);
		break;
	case 'V': /* GNU volume header */
		err = header_volume(a, tar, entry, h, unconsumed);
		break;
	case 'X': /* Used by SUN tar; same as 'x'. */
		a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
		a->archive.archive_format_name =
		    "POSIX pax interchange format (Sun variant)";
		err = header_pax_extensions(a, tar, entry, h, unconsumed);
		break;
	case 'x': /* POSIX-standard 'x' header. */
		a->archive.archive_format = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
		a->archive.archive_format_name = "POSIX pax interchange format";
		err = header_pax_extensions(a, tar, entry, h, unconsumed);
		break;
	default:
		gnuheader = (const struct archive_entry_header_gnutar *)h;
		if (memcmp(gnuheader->magic, "ustar  \0", 8) == 0) {
			a->archive.archive_format = ARCHIVE_FORMAT_TAR_GNUTAR;
			a->archive.archive_format_name = "GNU tar format";
			err = header_gnutar(a, tar, entry, h, unconsumed);
		} else if (memcmp(header->magic, "ustar", 5) == 0) {
			if (a->archive.archive_format != ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE) {
				a->archive.archive_format = ARCHIVE_FORMAT_TAR_USTAR;
				a->archive.archive_format_name = "POSIX ustar format";
			}
			err = header_ustar(a, tar, entry, h);
		} else {
			a->archive.archive_format = ARCHIVE_FORMAT_TAR;
			a->archive.archive_format_name = "tar (non-POSIX)";
			err = header_old_tar(a, tar, entry, h);
		}
	}
	if (err == ARCHIVE_FATAL)
		return (err);

	tar_flush_unconsumed(a, unconsumed);

	h = NULL;
	header = NULL;

	--tar->header_recursion_depth;
	/* Yuck.  Apple's design here ends up storing long pathname
	 * extensions for both the AppleDouble extension entry and the
	 * regular entry.
	 */
	if ((err == ARCHIVE_WARN || err == ARCHIVE_OK) &&
	    tar->header_recursion_depth == 0 &&
	    tar->process_mac_extensions) {
		int err2 = read_mac_metadata_blob(a, tar, entry, h, unconsumed);
		if (err2 < err)
			err = err2;
	}

	/* We return warnings or success as-is.  Anything else is fatal. */
	if (err == ARCHIVE_WARN || err == ARCHIVE_OK) {
		if (tar->sparse_gnu_pending) {
			if (tar->sparse_gnu_major == 1 &&
			    tar->sparse_gnu_minor == 0) {
				ssize_t bytes_read;

				tar->sparse_gnu_pending = 0;
				/* Read initial sparse map. */
				bytes_read = gnu_sparse_10_read(a, tar, unconsumed);
				if (bytes_read < 0)
					return ((int)bytes_read);
				tar->entry_bytes_remaining -= bytes_read;
			} else {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Unrecognized GNU sparse file format");
				return (ARCHIVE_WARN);
			}
			tar->sparse_gnu_pending = 0;
		}
		return (err);
	}
	if (err == ARCHIVE_EOF)
		/* EOF when recursively reading a header is bad. */
		archive_set_error(&a->archive, EINVAL, "Damaged tar archive");
	return (ARCHIVE_FATAL);
}

/*
 * Return true if block checksum is correct.
 */
static int
checksum(struct archive_read *a, const void *h)
{
	const unsigned char *bytes;
	const struct archive_entry_header_ustar	*header;
	int check, sum;
	size_t i;

	(void)a; /* UNUSED */
	bytes = (const unsigned char *)h;
	header = (const struct archive_entry_header_ustar *)h;

	/* Checksum field must hold an octal number */
	for (i = 0; i < sizeof(header->checksum); ++i) {
		char c = header->checksum[i];
		if (c != ' ' && c != '\0' && (c < '0' || c > '7'))
			return 0;
	}

	/*
	 * Test the checksum.  Note that POSIX specifies _unsigned_
	 * bytes for this calculation.
	 */
	sum = (int)tar_atol(header->checksum, sizeof(header->checksum));
	check = 0;
	for (i = 0; i < 148; i++)
		check += (unsigned char)bytes[i];
	for (; i < 156; i++)
		check += 32;
	for (; i < 512; i++)
		check += (unsigned char)bytes[i];
	if (sum == check)
		return (1);

	/*
	 * Repeat test with _signed_ bytes, just in case this archive
	 * was created by an old BSD, Solaris, or HP-UX tar with a
	 * broken checksum calculation.
	 */
	check = 0;
	for (i = 0; i < 148; i++)
		check += (signed char)bytes[i];
	for (; i < 156; i++)
		check += 32;
	for (; i < 512; i++)
		check += (signed char)bytes[i];
	if (sum == check)
		return (1);

	return (0);
}

/*
 * Return true if this block contains only nulls.
 */
static int
archive_block_is_null(const char *p)
{
	unsigned i;

	for (i = 0; i < 512; i++)
		if (*p++)
			return (0);
	return (1);
}

/*
 * Interpret 'A' Solaris ACL header
 */
static int
header_Solaris_ACL(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	const struct archive_entry_header_ustar *header;
	size_t size;
	int err, acl_type;
	int64_t type;
	char *acl, *p;

	/*
	 * read_body_to_string adds a NUL terminator, but we need a little
	 * more to make sure that we don't overrun acl_text later.
	 */
	header = (const struct archive_entry_header_ustar *)h;
	size = (size_t)tar_atol(header->size, sizeof(header->size));
	err = read_body_to_string(a, tar, &(tar->acl_text), h, unconsumed);
	if (err != ARCHIVE_OK)
		return (err);

	/* Recursively read next header */
	err = tar_read_header(a, tar, entry, unconsumed);
	if ((err != ARCHIVE_OK) && (err != ARCHIVE_WARN))
		return (err);

	/* TODO: Examine the first characters to see if this
	 * is an AIX ACL descriptor.  We'll likely never support
	 * them, but it would be polite to recognize and warn when
	 * we do see them. */

	/* Leading octal number indicates ACL type and number of entries. */
	p = acl = tar->acl_text.s;
	type = 0;
	while (*p != '\0' && p < acl + size) {
		if (*p < '0' || *p > '7') {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Malformed Solaris ACL attribute (invalid digit)");
			return(ARCHIVE_WARN);
		}
		type <<= 3;
		type += *p - '0';
		if (type > 077777777) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Malformed Solaris ACL attribute (count too large)");
			return (ARCHIVE_WARN);
		}
		p++;
	}
	switch ((int)type & ~0777777) {
	case 01000000:
		/* POSIX.1e ACL */
		acl_type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
		break;
	case 03000000:
		/* NFSv4 ACL */
		acl_type = ARCHIVE_ENTRY_ACL_TYPE_NFS4;
		break;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Malformed Solaris ACL attribute (unsupported type %o)",
		    (int)type);
		return (ARCHIVE_WARN);
	}
	p++;

	if (p >= acl + size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Malformed Solaris ACL attribute (body overflow)");
		return(ARCHIVE_WARN);
	}

	/* ACL text is null-terminated; find the end. */
	size -= (p - acl);
	acl = p;

	while (*p != '\0' && p < acl + size)
		p++;

	if (tar->sconv_acl == NULL) {
		tar->sconv_acl = archive_string_conversion_from_charset(
		    &(a->archive), "UTF-8", 1);
		if (tar->sconv_acl == NULL)
			return (ARCHIVE_FATAL);
	}
	archive_strncpy(&(tar->localname), acl, p - acl);
	err = archive_acl_from_text_l(archive_entry_acl(entry),
	    tar->localname.s, acl_type, tar->sconv_acl);
	if (err != ARCHIVE_OK) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for ACL");
		} else
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Malformed Solaris ACL attribute (unparsable)");
	}
	return (err);
}

/*
 * Interpret 'K' long linkname header.
 */
static int
header_longlink(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	int err;

	err = read_body_to_string(a, tar, &(tar->longlink), h, unconsumed);
	if (err != ARCHIVE_OK)
		return (err);
	err = tar_read_header(a, tar, entry, unconsumed);
	if ((err != ARCHIVE_OK) && (err != ARCHIVE_WARN))
		return (err);
	/* Set symlink if symlink already set, else hardlink. */
	archive_entry_copy_link(entry, tar->longlink.s);
	return (ARCHIVE_OK);
}

static int
set_conversion_failed_error(struct archive_read *a,
    struct archive_string_conv *sconv, const char *name)
{
	if (errno == ENOMEM) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory for %s", name);
		return (ARCHIVE_FATAL);
	}
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "%s can't be converted from %s to current locale.",
	    name, archive_string_conversion_charset_name(sconv));
	return (ARCHIVE_WARN);
}

/*
 * Interpret 'L' long filename header.
 */
static int
header_longname(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	int err;

	err = read_body_to_string(a, tar, &(tar->longname), h, unconsumed);
	if (err != ARCHIVE_OK)
		return (err);
	/* Read and parse "real" header, then override name. */
	err = tar_read_header(a, tar, entry, unconsumed);
	if ((err != ARCHIVE_OK) && (err != ARCHIVE_WARN))
		return (err);
	if (archive_entry_copy_pathname_l(entry, tar->longname.s,
	    archive_strlen(&(tar->longname)), tar->sconv) != 0)
		err = set_conversion_failed_error(a, tar->sconv, "Pathname");
	return (err);
}


/*
 * Interpret 'V' GNU tar volume header.
 */
static int
header_volume(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	(void)h;

	/* Just skip this and read the next header. */
	return (tar_read_header(a, tar, entry, unconsumed));
}

/*
 * Read body of an archive entry into an archive_string object.
 */
static int
read_body_to_string(struct archive_read *a, struct tar *tar,
    struct archive_string *as, const void *h, size_t *unconsumed)
{
	int64_t size;
	const struct archive_entry_header_ustar *header;
	const void *src;

	(void)tar; /* UNUSED */
	header = (const struct archive_entry_header_ustar *)h;
	size  = tar_atol(header->size, sizeof(header->size));
	if ((size > 1048576) || (size < 0)) {
		archive_set_error(&a->archive, EINVAL,
		    "Special header too large");
		return (ARCHIVE_FATAL);
	}

	/* Fail if we can't make our buffer big enough. */
	if (archive_string_ensure(as, (size_t)size+1) == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "No memory");
		return (ARCHIVE_FATAL);
	}

	tar_flush_unconsumed(a, unconsumed);

	/* Read the body into the string. */
	*unconsumed = (size_t)((size + 511) & ~ 511);
	src = __archive_read_ahead(a, *unconsumed, NULL);
	if (src == NULL) {
		*unconsumed = 0;
		return (ARCHIVE_FATAL);
	}
	memcpy(as->s, src, (size_t)size);
	as->s[size] = '\0';
	as->length = (size_t)size;
	return (ARCHIVE_OK);
}

/*
 * Parse out common header elements.
 *
 * This would be the same as header_old_tar, except that the
 * filename is handled slightly differently for old and POSIX
 * entries  (POSIX entries support a 'prefix').  This factoring
 * allows header_old_tar and header_ustar
 * to handle filenames differently, while still putting most of the
 * common parsing into one place.
 */
static int
header_common(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h)
{
	const struct archive_entry_header_ustar	*header;
	char	tartype;
	int     err = ARCHIVE_OK;

	header = (const struct archive_entry_header_ustar *)h;
	if (header->linkname[0])
		archive_strncpy(&(tar->entry_linkpath),
		    header->linkname, sizeof(header->linkname));
	else
		archive_string_empty(&(tar->entry_linkpath));

	/* Parse out the numeric fields (all are octal) */
	archive_entry_set_mode(entry,
		(mode_t)tar_atol(header->mode, sizeof(header->mode)));
	archive_entry_set_uid(entry, tar_atol(header->uid, sizeof(header->uid)));
	archive_entry_set_gid(entry, tar_atol(header->gid, sizeof(header->gid)));
	tar->entry_bytes_remaining = tar_atol(header->size, sizeof(header->size));
	if (tar->entry_bytes_remaining < 0) {
		tar->entry_bytes_remaining = 0;
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Tar entry has negative size");
		return (ARCHIVE_FATAL);
	}
	if (tar->entry_bytes_remaining == INT64_MAX) {
		/* Note: tar_atol returns INT64_MAX on overflow */
		tar->entry_bytes_remaining = 0;
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Tar entry size overflow");
		return (ARCHIVE_FATAL);
	}
	tar->realsize = tar->entry_bytes_remaining;
	archive_entry_set_size(entry, tar->entry_bytes_remaining);
	archive_entry_set_mtime(entry, tar_atol(header->mtime, sizeof(header->mtime)), 0);

	/* Handle the tar type flag appropriately. */
	tartype = header->typeflag[0];

	switch (tartype) {
	case '1': /* Hard link */
		if (archive_entry_copy_hardlink_l(entry, tar->entry_linkpath.s,
		    archive_strlen(&(tar->entry_linkpath)), tar->sconv) != 0) {
			err = set_conversion_failed_error(a, tar->sconv,
			    "Linkname");
			if (err == ARCHIVE_FATAL)
				return (err);
		}
		/*
		 * The following may seem odd, but: Technically, tar
		 * does not store the file type for a "hard link"
		 * entry, only the fact that it is a hard link.  So, I
		 * leave the type zero normally.  But, pax interchange
		 * format allows hard links to have data, which
		 * implies that the underlying entry is a regular
		 * file.
		 */
		if (archive_entry_size(entry) > 0)
			archive_entry_set_filetype(entry, AE_IFREG);

		/*
		 * A tricky point: Traditionally, tar readers have
		 * ignored the size field when reading hardlink
		 * entries, and some writers put non-zero sizes even
		 * though the body is empty.  POSIX blessed this
		 * convention in the 1988 standard, but broke with
		 * this tradition in 2001 by permitting hardlink
		 * entries to store valid bodies in pax interchange
		 * format, but not in ustar format.  Since there is no
		 * hard and fast way to distinguish pax interchange
		 * from earlier archives (the 'x' and 'g' entries are
		 * optional, after all), we need a heuristic.
		 */
		if (archive_entry_size(entry) == 0) {
			/* If the size is already zero, we're done. */
		}  else if (a->archive.archive_format
		    == ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE) {
			/* Definitely pax extended; must obey hardlink size. */
		} else if (a->archive.archive_format == ARCHIVE_FORMAT_TAR
		    || a->archive.archive_format == ARCHIVE_FORMAT_TAR_GNUTAR)
		{
			/* Old-style or GNU tar: we must ignore the size. */
			archive_entry_set_size(entry, 0);
			tar->entry_bytes_remaining = 0;
		} else if (archive_read_format_tar_bid(a, 50) > 50) {
			/*
			 * We don't know if it's pax: If the bid
			 * function sees a valid ustar header
			 * immediately following, then let's ignore
			 * the hardlink size.
			 */
			archive_entry_set_size(entry, 0);
			tar->entry_bytes_remaining = 0;
		}
		/*
		 * TODO: There are still two cases I'd like to handle:
		 *   = a ustar non-pax archive with a hardlink entry at
		 *     end-of-archive.  (Look for block of nulls following?)
		 *   = a pax archive that has not seen any pax headers
		 *     and has an entry which is a hardlink entry storing
		 *     a body containing an uncompressed tar archive.
		 * The first is worth addressing; I don't see any reliable
		 * way to deal with the second possibility.
		 */
		break;
	case '2': /* Symlink */
		archive_entry_set_filetype(entry, AE_IFLNK);
		archive_entry_set_size(entry, 0);
		tar->entry_bytes_remaining = 0;
		if (archive_entry_copy_symlink_l(entry, tar->entry_linkpath.s,
		    archive_strlen(&(tar->entry_linkpath)), tar->sconv) != 0) {
			err = set_conversion_failed_error(a, tar->sconv,
			    "Linkname");
			if (err == ARCHIVE_FATAL)
				return (err);
		}
		break;
	case '3': /* Character device */
		archive_entry_set_filetype(entry, AE_IFCHR);
		archive_entry_set_size(entry, 0);
		tar->entry_bytes_remaining = 0;
		break;
	case '4': /* Block device */
		archive_entry_set_filetype(entry, AE_IFBLK);
		archive_entry_set_size(entry, 0);
		tar->entry_bytes_remaining = 0;
		break;
	case '5': /* Dir */
		archive_entry_set_filetype(entry, AE_IFDIR);
		archive_entry_set_size(entry, 0);
		tar->entry_bytes_remaining = 0;
		break;
	case '6': /* FIFO device */
		archive_entry_set_filetype(entry, AE_IFIFO);
		archive_entry_set_size(entry, 0);
		tar->entry_bytes_remaining = 0;
		break;
	case 'D': /* GNU incremental directory type */
		/*
		 * No special handling is actually required here.
		 * It might be nice someday to preprocess the file list and
		 * provide it to the client, though.
		 */
		archive_entry_set_filetype(entry, AE_IFDIR);
		break;
	case 'M': /* GNU "Multi-volume" (remainder of file from last archive)*/
		/*
		 * As far as I can tell, this is just like a regular file
		 * entry, except that the contents should be _appended_ to
		 * the indicated file at the indicated offset.  This may
		 * require some API work to fully support.
		 */
		break;
	case 'N': /* Old GNU "long filename" entry. */
		/* The body of this entry is a script for renaming
		 * previously-extracted entries.  Ugh.  It will never
		 * be supported by libarchive. */
		archive_entry_set_filetype(entry, AE_IFREG);
		break;
	case 'S': /* GNU sparse files */
		/*
		 * Sparse files are really just regular files with
		 * sparse information in the extended area.
		 */
		/* FALLTHROUGH */
	case '0':
		/*
		 * Enable sparse file "read" support only for regular
		 * files and explicit GNU sparse files.  However, we
		 * don't allow non-standard file types to be sparse.
		 */
		tar->sparse_allowed = 1;
		/* FALLTHROUGH */
	default: /* Regular file  and non-standard types */
		/*
		 * Per POSIX: non-recognized types should always be
		 * treated as regular files.
		 */
		archive_entry_set_filetype(entry, AE_IFREG);
		break;
	}
	return (err);
}

/*
 * Parse out header elements for "old-style" tar archives.
 */
static int
header_old_tar(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h)
{
	const struct archive_entry_header_ustar	*header;
	int err = ARCHIVE_OK, err2;

	/* Copy filename over (to ensure null termination). */
	header = (const struct archive_entry_header_ustar *)h;
	if (archive_entry_copy_pathname_l(entry,
	    header->name, sizeof(header->name), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Pathname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	/* Grab rest of common fields */
	err2 = header_common(a, tar, entry, h);
	if (err > err2)
		err = err2;

	tar->entry_padding = 0x1ff & (-tar->entry_bytes_remaining);
	return (err);
}

/*
 * Read a Mac AppleDouble-encoded blob of file metadata,
 * if there is one.
 */
static int
read_mac_metadata_blob(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	int64_t size;
	const void *data;
	const char *p, *name;
	const wchar_t *wp, *wname;

	(void)h; /* UNUSED */

	wname = wp = archive_entry_pathname_w(entry);
	if (wp != NULL) {
		/* Find the last path element. */
		for (; *wp != L'\0'; ++wp) {
			if (wp[0] == '/' && wp[1] != L'\0')
				wname = wp + 1;
		}
		/*
		 * If last path element starts with "._", then
		 * this is a Mac extension.
		 */
		if (wname[0] != L'.' || wname[1] != L'_' || wname[2] == L'\0')
			return ARCHIVE_OK;
	} else {
		/* Find the last path element. */
		name = p = archive_entry_pathname(entry);
		if (p == NULL)
			return (ARCHIVE_FAILED);
		for (; *p != '\0'; ++p) {
			if (p[0] == '/' && p[1] != '\0')
				name = p + 1;
		}
		/*
		 * If last path element starts with "._", then
		 * this is a Mac extension.
		 */
		if (name[0] != '.' || name[1] != '_' || name[2] == '\0')
			return ARCHIVE_OK;
	}

 	/* Read the body as a Mac OS metadata blob. */
	size = archive_entry_size(entry);

	/*
	 * TODO: Look beyond the body here to peek at the next header.
	 * If it's a regular header (not an extension header)
	 * that has the wrong name, just return the current
	 * entry as-is, without consuming the body here.
	 * That would reduce the risk of us mis-identifying
	 * an ordinary file that just happened to have
	 * a name starting with "._".
	 *
	 * Q: Is the above idea really possible?  Even
	 * when there are GNU or pax extension entries?
	 */
	data = __archive_read_ahead(a, (size_t)size, NULL);
	if (data == NULL) {
		*unconsumed = 0;
		return (ARCHIVE_FATAL);
	}
	archive_entry_copy_mac_metadata(entry, data, (size_t)size);
	*unconsumed = (size_t)((size + 511) & ~ 511);
	tar_flush_unconsumed(a, unconsumed);
	return (tar_read_header(a, tar, entry, unconsumed));
}

/*
 * Parse a file header for a pax extended archive entry.
 */
static int
header_pax_global(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	int err;

	err = read_body_to_string(a, tar, &(tar->pax_global), h, unconsumed);
	if (err != ARCHIVE_OK)
		return (err);
	err = tar_read_header(a, tar, entry, unconsumed);
	return (err);
}

static int
header_pax_extensions(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	int err, err2;

	err = read_body_to_string(a, tar, &(tar->pax_header), h, unconsumed);
	if (err != ARCHIVE_OK)
		return (err);

	/* Parse the next header. */
	err = tar_read_header(a, tar, entry, unconsumed);
	if ((err != ARCHIVE_OK) && (err != ARCHIVE_WARN))
		return (err);

	/*
	 * TODO: Parse global/default options into 'entry' struct here
	 * before handling file-specific options.
	 *
	 * This design (parse standard header, then overwrite with pax
	 * extended attribute data) usually works well, but isn't ideal;
	 * it would be better to parse the pax extended attributes first
	 * and then skip any fields in the standard header that were
	 * defined in the pax header.
	 */
	err2 = pax_header(a, tar, entry, &tar->pax_header);
	err =  err_combine(err, err2);
	tar->entry_padding = 0x1ff & (-tar->entry_bytes_remaining);
	return (err);
}


/*
 * Parse a file header for a Posix "ustar" archive entry.  This also
 * handles "pax" or "extended ustar" entries.
 */
static int
header_ustar(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h)
{
	const struct archive_entry_header_ustar	*header;
	struct archive_string *as;
	int err = ARCHIVE_OK, r;

	header = (const struct archive_entry_header_ustar *)h;

	/* Copy name into an internal buffer to ensure null-termination. */
	as = &(tar->entry_pathname);
	if (header->prefix[0]) {
		archive_strncpy(as, header->prefix, sizeof(header->prefix));
		if (as->s[archive_strlen(as) - 1] != '/')
			archive_strappend_char(as, '/');
		archive_strncat(as, header->name, sizeof(header->name));
	} else {
		archive_strncpy(as, header->name, sizeof(header->name));
	}
	if (archive_entry_copy_pathname_l(entry, as->s, archive_strlen(as),
	    tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Pathname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	/* Handle rest of common fields. */
	r = header_common(a, tar, entry, h);
	if (r == ARCHIVE_FATAL)
		return (r);
	if (r < err)
		err = r;

	/* Handle POSIX ustar fields. */
	if (archive_entry_copy_uname_l(entry,
	    header->uname, sizeof(header->uname), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Uname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	if (archive_entry_copy_gname_l(entry,
	    header->gname, sizeof(header->gname), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Gname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	/* Parse out device numbers only for char and block specials. */
	if (header->typeflag[0] == '3' || header->typeflag[0] == '4') {
		archive_entry_set_rdevmajor(entry, (dev_t)
		    tar_atol(header->rdevmajor, sizeof(header->rdevmajor)));
		archive_entry_set_rdevminor(entry, (dev_t)
		    tar_atol(header->rdevminor, sizeof(header->rdevminor)));
	}

	tar->entry_padding = 0x1ff & (-tar->entry_bytes_remaining);

	return (err);
}


/*
 * Parse the pax extended attributes record.
 *
 * Returns non-zero if there's an error in the data.
 */
static int
pax_header(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, struct archive_string *in_as)
{
	size_t attr_length, l, line_length, value_length;
	char *p;
	char *key, *value;
	struct archive_string *as;
	struct archive_string_conv *sconv;
	int err, err2;
	char *attr = in_as->s;

	attr_length = in_as->length;
	tar->pax_hdrcharset_binary = 0;
	archive_string_empty(&(tar->entry_gname));
	archive_string_empty(&(tar->entry_linkpath));
	archive_string_empty(&(tar->entry_pathname));
	archive_string_empty(&(tar->entry_pathname_override));
	archive_string_empty(&(tar->entry_uname));
	err = ARCHIVE_OK;
	while (attr_length > 0) {
		/* Parse decimal length field at start of line. */
		line_length = 0;
		l = attr_length;
		p = attr; /* Record start of line. */
		while (l>0) {
			if (*p == ' ') {
				p++;
				l--;
				break;
			}
			if (*p < '0' || *p > '9') {
				archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
				    "Ignoring malformed pax extended attributes");
				return (ARCHIVE_WARN);
			}
			line_length *= 10;
			line_length += *p - '0';
			if (line_length > 999999) {
				archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
				    "Rejecting pax extended attribute > 1MB");
				return (ARCHIVE_WARN);
			}
			p++;
			l--;
		}

		/*
		 * Parsed length must be no bigger than available data,
		 * at least 1, and the last character of the line must
		 * be '\n'.
		 */
		if (line_length > attr_length
		    || line_length < 1
		    || attr[line_length - 1] != '\n')
		{
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Ignoring malformed pax extended attribute");
			return (ARCHIVE_WARN);
		}

		/* Null-terminate the line. */
		attr[line_length - 1] = '\0';

		/* Find end of key and null terminate it. */
		key = p;
		if (key[0] == '=')
			return (-1);
		while (*p && *p != '=')
			++p;
		if (*p == '\0') {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Invalid pax extended attributes");
			return (ARCHIVE_WARN);
		}
		*p = '\0';

		value = p + 1;

		/* Some values may be binary data */
		value_length = attr + line_length - 1 - value;

		/* Identify this attribute and set it in the entry. */
		err2 = pax_attribute(a, tar, entry, key, value, value_length);
		if (err2 == ARCHIVE_FATAL)
			return (err2);
		err = err_combine(err, err2);

		/* Skip to next line */
		attr += line_length;
		attr_length -= line_length;
	}

	/*
	 * PAX format uses UTF-8 as default charset for its metadata
	 * unless hdrcharset=BINARY is present in its header.
	 * We apply the charset specified by the hdrcharset option only
	 * when the hdrcharset attribute(in PAX header) is BINARY because
	 * we respect the charset described in PAX header and BINARY also
	 * means that metadata(filename,uname and gname) character-set
	 * is unknown.
	 */
	if (tar->pax_hdrcharset_binary)
		sconv = tar->opt_sconv;
	else {
		sconv = archive_string_conversion_from_charset(
		    &(a->archive), "UTF-8", 1);
		if (sconv == NULL)
			return (ARCHIVE_FATAL);
		if (tar->compat_2x)
			archive_string_conversion_set_opt(sconv,
			    SCONV_SET_OPT_UTF8_LIBARCHIVE2X);
	}

	if (archive_strlen(&(tar->entry_gname)) > 0) {
		if (archive_entry_copy_gname_l(entry, tar->entry_gname.s,
		    archive_strlen(&(tar->entry_gname)), sconv) != 0) {
			err = set_conversion_failed_error(a, sconv, "Gname");
			if (err == ARCHIVE_FATAL)
				return (err);
			/* Use a converted an original name. */
			archive_entry_copy_gname(entry, tar->entry_gname.s);
		}
	}
	if (archive_strlen(&(tar->entry_linkpath)) > 0) {
		if (archive_entry_copy_link_l(entry, tar->entry_linkpath.s,
		    archive_strlen(&(tar->entry_linkpath)), sconv) != 0) {
			err = set_conversion_failed_error(a, sconv, "Linkname");
			if (err == ARCHIVE_FATAL)
				return (err);
			/* Use a converted an original name. */
			archive_entry_copy_link(entry, tar->entry_linkpath.s);
		}
	}
	/*
	 * Some extensions (such as the GNU sparse file extensions)
	 * deliberately store a synthetic name under the regular 'path'
	 * attribute and the real file name under a different attribute.
	 * Since we're supposed to not care about the order, we
	 * have no choice but to store all of the various filenames
	 * we find and figure it all out afterwards.  This is the
	 * figuring out part.
	 */
	as = NULL;
	if (archive_strlen(&(tar->entry_pathname_override)) > 0)
		as = &(tar->entry_pathname_override);
	else if (archive_strlen(&(tar->entry_pathname)) > 0)
		as = &(tar->entry_pathname);
	if (as != NULL) {
		if (archive_entry_copy_pathname_l(entry, as->s,
		    archive_strlen(as), sconv) != 0) {
			err = set_conversion_failed_error(a, sconv, "Pathname");
			if (err == ARCHIVE_FATAL)
				return (err);
			/* Use a converted an original name. */
			archive_entry_copy_pathname(entry, as->s);
		}
	}
	if (archive_strlen(&(tar->entry_uname)) > 0) {
		if (archive_entry_copy_uname_l(entry, tar->entry_uname.s,
		    archive_strlen(&(tar->entry_uname)), sconv) != 0) {
			err = set_conversion_failed_error(a, sconv, "Uname");
			if (err == ARCHIVE_FATAL)
				return (err);
			/* Use a converted an original name. */
			archive_entry_copy_uname(entry, tar->entry_uname.s);
		}
	}
	return (err);
}

static int
pax_attribute_xattr(struct archive_entry *entry,
	const char *name, const char *value)
{
	char *name_decoded;
	void *value_decoded;
	size_t value_len;

	if (strlen(name) < 18 || (memcmp(name, "LIBARCHIVE.xattr.", 17)) != 0)
		return 3;

	name += 17;

	/* URL-decode name */
	name_decoded = url_decode(name);
	if (name_decoded == NULL)
		return 2;

	/* Base-64 decode value */
	value_decoded = base64_decode(value, strlen(value), &value_len);
	if (value_decoded == NULL) {
		free(name_decoded);
		return 1;
	}

	archive_entry_xattr_add_entry(entry, name_decoded,
		value_decoded, value_len);

	free(name_decoded);
	free(value_decoded);
	return 0;
}

static int
pax_attribute_schily_xattr(struct archive_entry *entry,
	const char *name, const char *value, size_t value_length)
{
	if (strlen(name) < 14 || (memcmp(name, "SCHILY.xattr.", 13)) != 0)
		return 1;

	name += 13;

	archive_entry_xattr_add_entry(entry, name, value, value_length);

	return 0;
}

static int
pax_attribute_acl(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const char *value, int type)
{
	int r;
	const char* errstr;

	switch (type) {
	case ARCHIVE_ENTRY_ACL_TYPE_ACCESS:
		errstr = "SCHILY.acl.access";
		break;
	case ARCHIVE_ENTRY_ACL_TYPE_DEFAULT:
		errstr = "SCHILY.acl.default";
		break;
	case ARCHIVE_ENTRY_ACL_TYPE_NFS4:
		errstr = "SCHILY.acl.ace";
		break;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Unknown ACL type: %d", type);
		return(ARCHIVE_FATAL);
	}

	if (tar->sconv_acl == NULL) {
		tar->sconv_acl =
		    archive_string_conversion_from_charset(
			&(a->archive), "UTF-8", 1);
		if (tar->sconv_acl == NULL)
			return (ARCHIVE_FATAL);
	}

	r = archive_acl_from_text_l(archive_entry_acl(entry), value, type,
	    tar->sconv_acl);
	if (r != ARCHIVE_OK) {
		if (r == ARCHIVE_FATAL) {
			archive_set_error(&a->archive, ENOMEM,
			    "%s %s", "Can't allocate memory for ",
			    errstr);
			return (r);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC, "%s %s", "Parse error: ", errstr);
	}
	return (r);
}

/*
 * Parse a single key=value attribute.  key/value pointers are
 * assumed to point into reasonably long-lived storage.
 *
 * Note that POSIX reserves all-lowercase keywords.  Vendor-specific
 * extensions should always have keywords of the form "VENDOR.attribute"
 * In particular, it's quite feasible to support many different
 * vendor extensions here.  I'm using "LIBARCHIVE" for extensions
 * unique to this library.
 *
 * Investigate other vendor-specific extensions and see if
 * any of them look useful.
 */
static int
pax_attribute(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const char *key, const char *value, size_t value_length)
{
	int64_t s;
	long n;
	int err = ARCHIVE_OK, r;

#ifndef __FreeBSD__
	if (value == NULL)
		value = "";	/* Disable compiler warning; do not pass
				 * NULL pointer to strlen().  */
#endif
	switch (key[0]) {
	case 'G':
		/* Reject GNU.sparse.* headers on non-regular files. */
		if (strncmp(key, "GNU.sparse", 10) == 0 &&
		    !tar->sparse_allowed) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Non-regular file cannot be sparse");
			return (ARCHIVE_FATAL);
		}

		/* GNU "0.0" sparse pax format. */
		if (strcmp(key, "GNU.sparse.numblocks") == 0) {
			tar->sparse_offset = -1;
			tar->sparse_numbytes = -1;
			tar->sparse_gnu_major = 0;
			tar->sparse_gnu_minor = 0;
		}
		if (strcmp(key, "GNU.sparse.offset") == 0) {
			tar->sparse_offset = tar_atol10(value, strlen(value));
			if (tar->sparse_numbytes != -1) {
				if (gnu_add_sparse_entry(a, tar,
				    tar->sparse_offset, tar->sparse_numbytes)
				    != ARCHIVE_OK)
					return (ARCHIVE_FATAL);
				tar->sparse_offset = -1;
				tar->sparse_numbytes = -1;
			}
		}
		if (strcmp(key, "GNU.sparse.numbytes") == 0) {
			tar->sparse_numbytes = tar_atol10(value, strlen(value));
			if (tar->sparse_numbytes != -1) {
				if (gnu_add_sparse_entry(a, tar,
				    tar->sparse_offset, tar->sparse_numbytes)
				    != ARCHIVE_OK)
					return (ARCHIVE_FATAL);
				tar->sparse_offset = -1;
				tar->sparse_numbytes = -1;
			}
		}
		if (strcmp(key, "GNU.sparse.size") == 0) {
			tar->realsize = tar_atol10(value, strlen(value));
			archive_entry_set_size(entry, tar->realsize);
			tar->realsize_override = 1;
		}

		/* GNU "0.1" sparse pax format. */
		if (strcmp(key, "GNU.sparse.map") == 0) {
			tar->sparse_gnu_major = 0;
			tar->sparse_gnu_minor = 1;
			if (gnu_sparse_01_parse(a, tar, value) != ARCHIVE_OK)
				return (ARCHIVE_WARN);
		}

		/* GNU "1.0" sparse pax format */
		if (strcmp(key, "GNU.sparse.major") == 0) {
			tar->sparse_gnu_major = (int)tar_atol10(value, strlen(value));
			tar->sparse_gnu_pending = 1;
		}
		if (strcmp(key, "GNU.sparse.minor") == 0) {
			tar->sparse_gnu_minor = (int)tar_atol10(value, strlen(value));
			tar->sparse_gnu_pending = 1;
		}
		if (strcmp(key, "GNU.sparse.name") == 0) {
			/*
			 * The real filename; when storing sparse
			 * files, GNU tar puts a synthesized name into
			 * the regular 'path' attribute in an attempt
			 * to limit confusion. ;-)
			 */
			archive_strcpy(&(tar->entry_pathname_override), value);
		}
		if (strcmp(key, "GNU.sparse.realsize") == 0) {
			tar->realsize = tar_atol10(value, strlen(value));
			archive_entry_set_size(entry, tar->realsize);
			tar->realsize_override = 1;
		}
		break;
	case 'L':
		/* Our extensions */
/* TODO: Handle arbitrary extended attributes... */
/*
		if (strcmp(key, "LIBARCHIVE.xxxxxxx") == 0)
			archive_entry_set_xxxxxx(entry, value);
*/
		if (strcmp(key, "LIBARCHIVE.creationtime") == 0) {
			pax_time(value, &s, &n);
			archive_entry_set_birthtime(entry, s, n);
		}
		if (memcmp(key, "LIBARCHIVE.xattr.", 17) == 0)
			pax_attribute_xattr(entry, key, value);
		break;
	case 'S':
		/* We support some keys used by the "star" archiver */
		if (strcmp(key, "SCHILY.acl.access") == 0) {
			r = pax_attribute_acl(a, tar, entry, value,
			    ARCHIVE_ENTRY_ACL_TYPE_ACCESS);
			if (r == ARCHIVE_FATAL)
				return (r);
		} else if (strcmp(key, "SCHILY.acl.default") == 0) {
			r = pax_attribute_acl(a, tar, entry, value,
			    ARCHIVE_ENTRY_ACL_TYPE_DEFAULT);
			if (r == ARCHIVE_FATAL)
				return (r);
		} else if (strcmp(key, "SCHILY.acl.ace") == 0) {
			r = pax_attribute_acl(a, tar, entry, value,
			    ARCHIVE_ENTRY_ACL_TYPE_NFS4);
			if (r == ARCHIVE_FATAL)
				return (r);
		} else if (strcmp(key, "SCHILY.devmajor") == 0) {
			archive_entry_set_rdevmajor(entry,
			    (dev_t)tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "SCHILY.devminor") == 0) {
			archive_entry_set_rdevminor(entry,
			    (dev_t)tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "SCHILY.fflags") == 0) {
			archive_entry_copy_fflags_text(entry, value);
		} else if (strcmp(key, "SCHILY.dev") == 0) {
			archive_entry_set_dev(entry,
			    (dev_t)tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "SCHILY.ino") == 0) {
			archive_entry_set_ino(entry,
			    tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "SCHILY.nlink") == 0) {
			archive_entry_set_nlink(entry, (unsigned)
			    tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "SCHILY.realsize") == 0) {
			tar->realsize = tar_atol10(value, strlen(value));
			tar->realsize_override = 1;
			archive_entry_set_size(entry, tar->realsize);
		} else if (strncmp(key, "SCHILY.xattr.", 13) == 0) {
			pax_attribute_schily_xattr(entry, key, value,
			    value_length);
		} else if (strcmp(key, "SUN.holesdata") == 0) {
			/* A Solaris extension for sparse. */
			r = solaris_sparse_parse(a, tar, entry, value);
			if (r < err) {
				if (r == ARCHIVE_FATAL)
					return (r);
				err = r;
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Parse error: SUN.holesdata");
			}
		}
		break;
	case 'a':
		if (strcmp(key, "atime") == 0) {
			pax_time(value, &s, &n);
			archive_entry_set_atime(entry, s, n);
		}
		break;
	case 'c':
		if (strcmp(key, "ctime") == 0) {
			pax_time(value, &s, &n);
			archive_entry_set_ctime(entry, s, n);
		} else if (strcmp(key, "charset") == 0) {
			/* TODO: Publish charset information in entry. */
		} else if (strcmp(key, "comment") == 0) {
			/* TODO: Publish comment in entry. */
		}
		break;
	case 'g':
		if (strcmp(key, "gid") == 0) {
			archive_entry_set_gid(entry,
			    tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "gname") == 0) {
			archive_strcpy(&(tar->entry_gname), value);
		}
		break;
	case 'h':
		if (strcmp(key, "hdrcharset") == 0) {
			if (strcmp(value, "BINARY") == 0)
				/* Binary  mode. */
				tar->pax_hdrcharset_binary = 1;
			else if (strcmp(value, "ISO-IR 10646 2000 UTF-8") == 0)
				tar->pax_hdrcharset_binary = 0;
		}
		break;
	case 'l':
		/* pax interchange doesn't distinguish hardlink vs. symlink. */
		if (strcmp(key, "linkpath") == 0) {
			archive_strcpy(&(tar->entry_linkpath), value);
		}
		break;
	case 'm':
		if (strcmp(key, "mtime") == 0) {
			pax_time(value, &s, &n);
			archive_entry_set_mtime(entry, s, n);
		}
		break;
	case 'p':
		if (strcmp(key, "path") == 0) {
			archive_strcpy(&(tar->entry_pathname), value);
		}
		break;
	case 'r':
		/* POSIX has reserved 'realtime.*' */
		break;
	case 's':
		/* POSIX has reserved 'security.*' */
		/* Someday: if (strcmp(key, "security.acl") == 0) { ... } */
		if (strcmp(key, "size") == 0) {
			/* "size" is the size of the data in the entry. */
			tar->entry_bytes_remaining
			    = tar_atol10(value, strlen(value));
			/*
			 * The "size" pax header keyword always overrides the
			 * "size" field in the tar header.
			 * GNU.sparse.realsize, GNU.sparse.size and
			 * SCHILY.realsize override this value.
			 */
			if (!tar->realsize_override) {
				archive_entry_set_size(entry,
				    tar->entry_bytes_remaining);
				tar->realsize
				    = tar->entry_bytes_remaining;
			}
		}
		break;
	case 'u':
		if (strcmp(key, "uid") == 0) {
			archive_entry_set_uid(entry,
			    tar_atol10(value, strlen(value)));
		} else if (strcmp(key, "uname") == 0) {
			archive_strcpy(&(tar->entry_uname), value);
		}
		break;
	}
	return (err);
}



/*
 * parse a decimal time value, which may include a fractional portion
 */
static void
pax_time(const char *p, int64_t *ps, long *pn)
{
	char digit;
	int64_t	s;
	unsigned long l;
	int sign;
	int64_t limit, last_digit_limit;

	limit = INT64_MAX / 10;
	last_digit_limit = INT64_MAX % 10;

	s = 0;
	sign = 1;
	if (*p == '-') {
		sign = -1;
		p++;
	}
	while (*p >= '0' && *p <= '9') {
		digit = *p - '0';
		if (s > limit ||
		    (s == limit && digit > last_digit_limit)) {
			s = INT64_MAX;
			break;
		}
		s = (s * 10) + digit;
		++p;
	}

	*ps = s * sign;

	/* Calculate nanoseconds. */
	*pn = 0;

	if (*p != '.')
		return;

	l = 100000000UL;
	do {
		++p;
		if (*p >= '0' && *p <= '9')
			*pn += (*p - '0') * l;
		else
			break;
	} while (l /= 10);
}

/*
 * Parse GNU tar header
 */
static int
header_gnutar(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const void *h, size_t *unconsumed)
{
	const struct archive_entry_header_gnutar *header;
	int64_t t;
	int err = ARCHIVE_OK;

	/*
	 * GNU header is like POSIX ustar, except 'prefix' is
	 * replaced with some other fields. This also means the
	 * filename is stored as in old-style archives.
	 */

	/* Grab fields common to all tar variants. */
	err = header_common(a, tar, entry, h);
	if (err == ARCHIVE_FATAL)
		return (err);

	/* Copy filename over (to ensure null termination). */
	header = (const struct archive_entry_header_gnutar *)h;
	if (archive_entry_copy_pathname_l(entry,
	    header->name, sizeof(header->name), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Pathname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	/* Fields common to ustar and GNU */
	/* XXX Can the following be factored out since it's common
	 * to ustar and gnu tar?  Is it okay to move it down into
	 * header_common, perhaps?  */
	if (archive_entry_copy_uname_l(entry,
	    header->uname, sizeof(header->uname), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Uname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	if (archive_entry_copy_gname_l(entry,
	    header->gname, sizeof(header->gname), tar->sconv) != 0) {
		err = set_conversion_failed_error(a, tar->sconv, "Gname");
		if (err == ARCHIVE_FATAL)
			return (err);
	}

	/* Parse out device numbers only for char and block specials */
	if (header->typeflag[0] == '3' || header->typeflag[0] == '4') {
		archive_entry_set_rdevmajor(entry, (dev_t)
		    tar_atol(header->rdevmajor, sizeof(header->rdevmajor)));
		archive_entry_set_rdevminor(entry, (dev_t)
		    tar_atol(header->rdevminor, sizeof(header->rdevminor)));
	} else
		archive_entry_set_rdev(entry, 0);

	tar->entry_padding = 0x1ff & (-tar->entry_bytes_remaining);

	/* Grab GNU-specific fields. */
	t = tar_atol(header->atime, sizeof(header->atime));
	if (t > 0)
		archive_entry_set_atime(entry, t, 0);
	t = tar_atol(header->ctime, sizeof(header->ctime));
	if (t > 0)
		archive_entry_set_ctime(entry, t, 0);

	if (header->realsize[0] != 0) {
		tar->realsize
		    = tar_atol(header->realsize, sizeof(header->realsize));
		archive_entry_set_size(entry, tar->realsize);
		tar->realsize_override = 1;
	}

	if (header->sparse[0].offset[0] != 0) {
		if (gnu_sparse_old_read(a, tar, header, unconsumed)
		    != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	} else {
		if (header->isextended[0] != 0) {
			/* XXX WTF? XXX */
		}
	}

	return (err);
}

static int
gnu_add_sparse_entry(struct archive_read *a, struct tar *tar,
    int64_t offset, int64_t remaining)
{
	struct sparse_block *p;

	p = (struct sparse_block *)calloc(1, sizeof(*p));
	if (p == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	if (tar->sparse_last != NULL)
		tar->sparse_last->next = p;
	else
		tar->sparse_list = p;
	tar->sparse_last = p;
	if (remaining < 0 || offset < 0 || offset > INT64_MAX - remaining) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC, "Malformed sparse map data");
		return (ARCHIVE_FATAL);
	}
	p->offset = offset;
	p->remaining = remaining;
	return (ARCHIVE_OK);
}

static void
gnu_clear_sparse_list(struct tar *tar)
{
	struct sparse_block *p;

	while (tar->sparse_list != NULL) {
		p = tar->sparse_list;
		tar->sparse_list = p->next;
		free(p);
	}
	tar->sparse_last = NULL;
}

/*
 * GNU tar old-format sparse data.
 *
 * GNU old-format sparse data is stored in a fixed-field
 * format.  Offset/size values are 11-byte octal fields (same
 * format as 'size' field in ustart header).  These are
 * stored in the header, allocating subsequent header blocks
 * as needed.  Extending the header in this way is a pretty
 * severe POSIX violation; this design has earned GNU tar a
 * lot of criticism.
 */

static int
gnu_sparse_old_read(struct archive_read *a, struct tar *tar,
    const struct archive_entry_header_gnutar *header, size_t *unconsumed)
{
	ssize_t bytes_read;
	const void *data;
	struct extended {
		struct gnu_sparse sparse[21];
		char	isextended[1];
		char	padding[7];
	};
	const struct extended *ext;

	if (gnu_sparse_old_parse(a, tar, header->sparse, 4) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	if (header->isextended[0] == 0)
		return (ARCHIVE_OK);

	do {
		tar_flush_unconsumed(a, unconsumed);
		data = __archive_read_ahead(a, 512, &bytes_read);
		if (bytes_read < 0)
			return (ARCHIVE_FATAL);
		if (bytes_read < 512) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Truncated tar archive "
			    "detected while reading sparse file data");
			return (ARCHIVE_FATAL);
		}
		*unconsumed = 512;
		ext = (const struct extended *)data;
		if (gnu_sparse_old_parse(a, tar, ext->sparse, 21) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	} while (ext->isextended[0] != 0);
	if (tar->sparse_list != NULL)
		tar->entry_offset = tar->sparse_list->offset;
	return (ARCHIVE_OK);
}

static int
gnu_sparse_old_parse(struct archive_read *a, struct tar *tar,
    const struct gnu_sparse *sparse, int length)
{
	while (length > 0 && sparse->offset[0] != 0) {
		if (gnu_add_sparse_entry(a, tar,
		    tar_atol(sparse->offset, sizeof(sparse->offset)),
		    tar_atol(sparse->numbytes, sizeof(sparse->numbytes)))
		    != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
		sparse++;
		length--;
	}
	return (ARCHIVE_OK);
}

/*
 * GNU tar sparse format 0.0
 *
 * Beginning with GNU tar 1.15, sparse files are stored using
 * information in the pax extended header.  The GNU tar maintainers
 * have gone through a number of variations in the process of working
 * out this scheme; fortunately, they're all numbered.
 *
 * Sparse format 0.0 uses attribute GNU.sparse.numblocks to store the
 * number of blocks, and GNU.sparse.offset/GNU.sparse.numbytes to
 * store offset/size for each block.  The repeated instances of these
 * latter fields violate the pax specification (which frowns on
 * duplicate keys), so this format was quickly replaced.
 */

/*
 * GNU tar sparse format 0.1
 *
 * This version replaced the offset/numbytes attributes with
 * a single "map" attribute that stored a list of integers.  This
 * format had two problems: First, the "map" attribute could be very
 * long, which caused problems for some implementations.  More
 * importantly, the sparse data was lost when extracted by archivers
 * that didn't recognize this extension.
 */

static int
gnu_sparse_01_parse(struct archive_read *a, struct tar *tar, const char *p)
{
	const char *e;
	int64_t offset = -1, size = -1;

	for (;;) {
		e = p;
		while (*e != '\0' && *e != ',') {
			if (*e < '0' || *e > '9')
				return (ARCHIVE_WARN);
			e++;
		}
		if (offset < 0) {
			offset = tar_atol10(p, e - p);
			if (offset < 0)
				return (ARCHIVE_WARN);
		} else {
			size = tar_atol10(p, e - p);
			if (size < 0)
				return (ARCHIVE_WARN);
			if (gnu_add_sparse_entry(a, tar, offset, size)
			    != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			offset = -1;
		}
		if (*e == '\0')
			return (ARCHIVE_OK);
		p = e + 1;
	}
}

/*
 * GNU tar sparse format 1.0
 *
 * The idea: The offset/size data is stored as a series of base-10
 * ASCII numbers prepended to the file data, so that dearchivers that
 * don't support this format will extract the block map along with the
 * data and a separate post-process can restore the sparseness.
 *
 * Unfortunately, GNU tar 1.16 had a bug that added unnecessary
 * padding to the body of the file when using this format.  GNU tar
 * 1.17 corrected this bug without bumping the version number, so
 * it's not possible to support both variants.  This code supports
 * the later variant at the expense of not supporting the former.
 *
 * This variant also replaced GNU.sparse.size with GNU.sparse.realsize
 * and introduced the GNU.sparse.major/GNU.sparse.minor attributes.
 */

/*
 * Read the next line from the input, and parse it as a decimal
 * integer followed by '\n'.  Returns positive integer value or
 * negative on error.
 */
static int64_t
gnu_sparse_10_atol(struct archive_read *a, struct tar *tar,
    int64_t *remaining, size_t *unconsumed)
{
	int64_t l, limit, last_digit_limit;
	const char *p;
	ssize_t bytes_read;
	int base, digit;

	base = 10;
	limit = INT64_MAX / base;
	last_digit_limit = INT64_MAX % base;

	/*
	 * Skip any lines starting with '#'; GNU tar specs
	 * don't require this, but they should.
	 */
	do {
		bytes_read = readline(a, tar, &p,
			(ssize_t)tar_min(*remaining, 100), unconsumed);
		if (bytes_read <= 0)
			return (ARCHIVE_FATAL);
		*remaining -= bytes_read;
	} while (p[0] == '#');

	l = 0;
	while (bytes_read > 0) {
		if (*p == '\n')
			return (l);
		if (*p < '0' || *p >= '0' + base)
			return (ARCHIVE_WARN);
		digit = *p - '0';
		if (l > limit || (l == limit && digit > last_digit_limit))
			l = INT64_MAX; /* Truncate on overflow. */
		else
			l = (l * base) + digit;
		p++;
		bytes_read--;
	}
	/* TODO: Error message. */
	return (ARCHIVE_WARN);
}

/*
 * Returns length (in bytes) of the sparse data description
 * that was read.
 */
static ssize_t
gnu_sparse_10_read(struct archive_read *a, struct tar *tar, size_t *unconsumed)
{
	ssize_t bytes_read;
	int entries;
	int64_t offset, size, to_skip, remaining;

	/* Clear out the existing sparse list. */
	gnu_clear_sparse_list(tar);

	remaining = tar->entry_bytes_remaining;

	/* Parse entries. */
	entries = (int)gnu_sparse_10_atol(a, tar, &remaining, unconsumed);
	if (entries < 0)
		return (ARCHIVE_FATAL);
	/* Parse the individual entries. */
	while (entries-- > 0) {
		/* Parse offset/size */
		offset = gnu_sparse_10_atol(a, tar, &remaining, unconsumed);
		if (offset < 0)
			return (ARCHIVE_FATAL);
		size = gnu_sparse_10_atol(a, tar, &remaining, unconsumed);
		if (size < 0)
			return (ARCHIVE_FATAL);
		/* Add a new sparse entry. */
		if (gnu_add_sparse_entry(a, tar, offset, size) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	}
	/* Skip rest of block... */
	tar_flush_unconsumed(a, unconsumed);
	bytes_read = (ssize_t)(tar->entry_bytes_remaining - remaining);
	to_skip = 0x1ff & -bytes_read;
	/* Fail if tar->entry_bytes_remaing would get negative */
	if (to_skip > remaining)
		return (ARCHIVE_FATAL);
	if (to_skip != __archive_read_consume(a, to_skip))
		return (ARCHIVE_FATAL);
	return ((ssize_t)(bytes_read + to_skip));
}

/*
 * Solaris pax extension for a sparse file. This is recorded with the
 * data and hole pairs. The way recording sparse information by Solaris'
 * pax simply indicates where data and sparse are, so the stored contents
 * consist of both data and hole.
 */
static int
solaris_sparse_parse(struct archive_read *a, struct tar *tar,
    struct archive_entry *entry, const char *p)
{
	const char *e;
	int64_t start, end;
	int hole = 1;

	(void)entry; /* UNUSED */

	end = 0;
	if (*p == ' ')
		p++;
	else
		return (ARCHIVE_WARN);
	for (;;) {
		e = p;
		while (*e != '\0' && *e != ' ') {
			if (*e < '0' || *e > '9')
				return (ARCHIVE_WARN);
			e++;
		}
		start = end;
		end = tar_atol10(p, e - p);
		if (end < 0)
			return (ARCHIVE_WARN);
		if (start < end) {
			if (gnu_add_sparse_entry(a, tar, start,
			    end - start) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			tar->sparse_last->hole = hole;
		}
		if (*e == '\0')
			return (ARCHIVE_OK);
		p = e + 1;
		hole = hole == 0;
	}
}

/*-
 * Convert text->integer.
 *
 * Traditional tar formats (including POSIX) specify base-8 for
 * all of the standard numeric fields.  This is a significant limitation
 * in practice:
 *   = file size is limited to 8GB
 *   = rdevmajor and rdevminor are limited to 21 bits
 *   = uid/gid are limited to 21 bits
 *
 * There are two workarounds for this:
 *   = pax extended headers, which use variable-length string fields
 *   = GNU tar and STAR both allow either base-8 or base-256 in
 *      most fields.  The high bit is set to indicate base-256.
 *
 * On read, this implementation supports both extensions.
 */
static int64_t
tar_atol(const char *p, size_t char_cnt)
{
	/*
	 * Technically, GNU tar considers a field to be in base-256
	 * only if the first byte is 0xff or 0x80.
	 */
	if (*p & 0x80)
		return (tar_atol256(p, char_cnt));
	return (tar_atol8(p, char_cnt));
}

/*
 * Note that this implementation does not (and should not!) obey
 * locale settings; you cannot simply substitute strtol here, since
 * it does obey locale.
 */
static int64_t
tar_atol_base_n(const char *p, size_t char_cnt, int base)
{
	int64_t	l, maxval, limit, last_digit_limit;
	int digit, sign;

	maxval = INT64_MAX;
	limit = INT64_MAX / base;
	last_digit_limit = INT64_MAX % base;

	/* the pointer will not be dereferenced if char_cnt is zero
	 * due to the way the && operator is evaluated.
	 */
	while (char_cnt != 0 && (*p == ' ' || *p == '\t')) {
		p++;
		char_cnt--;
	}

	sign = 1;
	if (char_cnt != 0 && *p == '-') {
		sign = -1;
		p++;
		char_cnt--;

		maxval = INT64_MIN;
		limit = -(INT64_MIN / base);
		last_digit_limit = INT64_MIN % base;
	}

	l = 0;
	if (char_cnt != 0) {
		digit = *p - '0';
		while (digit >= 0 && digit < base  && char_cnt != 0) {
			if (l>limit || (l == limit && digit > last_digit_limit)) {
				return maxval; /* Truncate on overflow. */
			}
			l = (l * base) + digit;
			digit = *++p - '0';
			char_cnt--;
		}
	}
	return (sign < 0) ? -l : l;
}

static int64_t
tar_atol8(const char *p, size_t char_cnt)
{
	return tar_atol_base_n(p, char_cnt, 8);
}

static int64_t
tar_atol10(const char *p, size_t char_cnt)
{
	return tar_atol_base_n(p, char_cnt, 10);
}

/*
 * Parse a base-256 integer.  This is just a variable-length
 * twos-complement signed binary value in big-endian order, except
 * that the high-order bit is ignored.  The values here can be up to
 * 12 bytes, so we need to be careful about overflowing 64-bit
 * (8-byte) integers.
 *
 * This code unashamedly assumes that the local machine uses 8-bit
 * bytes and twos-complement arithmetic.
 */
static int64_t
tar_atol256(const char *_p, size_t char_cnt)
{
	uint64_t l;
	const unsigned char *p = (const unsigned char *)_p;
	unsigned char c, neg;

	/* Extend 7-bit 2s-comp to 8-bit 2s-comp, decide sign. */
	c = *p;
	if (c & 0x40) {
		neg = 0xff;
		c |= 0x80;
		l = ~ARCHIVE_LITERAL_ULL(0);
	} else {
		neg = 0;
		c &= 0x7f;
		l = 0;
	}

	/* If more than 8 bytes, check that we can ignore
	 * high-order bits without overflow. */
	while (char_cnt > sizeof(int64_t)) {
		--char_cnt;
		if (c != neg)
			return neg ? INT64_MIN : INT64_MAX;
		c = *++p;
	}

	/* c is first byte that fits; if sign mismatch, return overflow */
	if ((c ^ neg) & 0x80) {
		return neg ? INT64_MIN : INT64_MAX;
	}

	/* Accumulate remaining bytes. */
	while (--char_cnt > 0) {
		l = (l << 8) | c;
		c = *++p;
	}
	l = (l << 8) | c;
	/* Return signed twos-complement value. */
	return (int64_t)(l);
}

/*
 * Returns length of line (including trailing newline)
 * or negative on error.  'start' argument is updated to
 * point to first character of line.  This avoids copying
 * when possible.
 */
static ssize_t
readline(struct archive_read *a, struct tar *tar, const char **start,
    ssize_t limit, size_t *unconsumed)
{
	ssize_t bytes_read;
	ssize_t total_size = 0;
	const void *t;
	const char *s;
	void *p;

	tar_flush_unconsumed(a, unconsumed);

	t = __archive_read_ahead(a, 1, &bytes_read);
	if (bytes_read <= 0)
		return (ARCHIVE_FATAL);
	s = t;  /* Start of line? */
	p = memchr(t, '\n', bytes_read);
	/* If we found '\n' in the read buffer, return pointer to that. */
	if (p != NULL) {
		bytes_read = 1 + ((const char *)p) - s;
		if (bytes_read > limit) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Line too long");
			return (ARCHIVE_FATAL);
		}
		*unconsumed = bytes_read;
		*start = s;
		return (bytes_read);
	}
	*unconsumed = bytes_read;
	/* Otherwise, we need to accumulate in a line buffer. */
	for (;;) {
		if (total_size + bytes_read > limit) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Line too long");
			return (ARCHIVE_FATAL);
		}
		if (archive_string_ensure(&tar->line, total_size + bytes_read) == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate working buffer");
			return (ARCHIVE_FATAL);
		}
		memcpy(tar->line.s + total_size, t, bytes_read);
		tar_flush_unconsumed(a, unconsumed);
		total_size += bytes_read;
		/* If we found '\n', clean up and return. */
		if (p != NULL) {
			*start = tar->line.s;
			return (total_size);
		}
		/* Read some more. */
		t = __archive_read_ahead(a, 1, &bytes_read);
		if (bytes_read <= 0)
			return (ARCHIVE_FATAL);
		s = t;  /* Start of line? */
		p = memchr(t, '\n', bytes_read);
		/* If we found '\n', trim the read. */
		if (p != NULL) {
			bytes_read = 1 + ((const char *)p) - s;
		}
		*unconsumed = bytes_read;
	}
}

/*
 * base64_decode - Base64 decode
 *
 * This accepts most variations of base-64 encoding, including:
 *    * with or without line breaks
 *    * with or without the final group padded with '=' or '_' characters
 * (The most economical Base-64 variant does not pad the last group and
 * omits line breaks; RFC1341 used for MIME requires both.)
 */
static char *
base64_decode(const char *s, size_t len, size_t *out_len)
{
	static const unsigned char digits[64] = {
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N',
		'O','P','Q','R','S','T','U','V','W','X','Y','Z','a','b',
		'c','d','e','f','g','h','i','j','k','l','m','n','o','p',
		'q','r','s','t','u','v','w','x','y','z','0','1','2','3',
		'4','5','6','7','8','9','+','/' };
	static unsigned char decode_table[128];
	char *out, *d;
	const unsigned char *src = (const unsigned char *)s;

	/* If the decode table is not yet initialized, prepare it. */
	if (decode_table[digits[1]] != 1) {
		unsigned i;
		memset(decode_table, 0xff, sizeof(decode_table));
		for (i = 0; i < sizeof(digits); i++)
			decode_table[digits[i]] = i;
	}

	/* Allocate enough space to hold the entire output. */
	/* Note that we may not use all of this... */
	out = (char *)malloc(len - len / 4 + 1);
	if (out == NULL) {
		*out_len = 0;
		return (NULL);
	}
	d = out;

	while (len > 0) {
		/* Collect the next group of (up to) four characters. */
		int v = 0;
		int group_size = 0;
		while (group_size < 4 && len > 0) {
			/* '=' or '_' padding indicates final group. */
			if (*src == '=' || *src == '_') {
				len = 0;
				break;
			}
			/* Skip illegal characters (including line breaks) */
			if (*src > 127 || *src < 32
			    || decode_table[*src] == 0xff) {
				len--;
				src++;
				continue;
			}
			v <<= 6;
			v |= decode_table[*src++];
			len --;
			group_size++;
		}
		/* Align a short group properly. */
		v <<= 6 * (4 - group_size);
		/* Unpack the group we just collected. */
		switch (group_size) {
		case 4: d[2] = v & 0xff;
			/* FALLTHROUGH */
		case 3: d[1] = (v >> 8) & 0xff;
			/* FALLTHROUGH */
		case 2: d[0] = (v >> 16) & 0xff;
			break;
		case 1: /* this is invalid! */
			break;
		}
		d += group_size * 3 / 4;
	}

	*out_len = d - out;
	return (out);
}

static char *
url_decode(const char *in)
{
	char *out, *d;
	const char *s;

	out = (char *)malloc(strlen(in) + 1);
	if (out == NULL)
		return (NULL);
	for (s = in, d = out; *s != '\0'; ) {
		if (s[0] == '%' && s[1] != '\0' && s[2] != '\0') {
			/* Try to convert % escape */
			int digit1 = tohex(s[1]);
			int digit2 = tohex(s[2]);
			if (digit1 >= 0 && digit2 >= 0) {
				/* Looks good, consume three chars */
				s += 3;
				/* Convert output */
				*d++ = ((digit1 << 4) | digit2);
				continue;
			}
			/* Else fall through and treat '%' as normal char */
		}
		*d++ = *s++;
	}
	*d = '\0';
	return (out);
}

static int
tohex(int c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	else if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	else if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	else
		return (-1);
}
