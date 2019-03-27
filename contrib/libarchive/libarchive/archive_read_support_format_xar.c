/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
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
#if HAVE_LIBXML_XMLREADER_H
#include <libxml/xmlreader.h>
#elif HAVE_BSDXML_H
#include <bsdxml.h>
#elif HAVE_EXPAT_H
#include <expat.h>
#endif
#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif
#if HAVE_LZMA_H
#include <lzma.h>
#endif
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include "archive.h"
#include "archive_digest_private.h"
#include "archive_endian.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_read_private.h"

#if (!defined(HAVE_LIBXML_XMLREADER_H) && \
     !defined(HAVE_BSDXML_H) && !defined(HAVE_EXPAT_H)) ||\
	!defined(HAVE_ZLIB_H) || \
	!defined(ARCHIVE_HAS_MD5) || !defined(ARCHIVE_HAS_SHA1)
/*
 * xar needs several external libraries.
 *   o libxml2 or expat --- XML parser
 *   o openssl or MD5/SHA1 hash function
 *   o zlib
 *   o bzlib2 (option)
 *   o liblzma (option)
 */
int
archive_read_support_format_xar(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_xar");

	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "Xar not supported on this platform");
	return (ARCHIVE_WARN);
}

#else	/* Support xar format */

/* #define DEBUG 1 */
/* #define DEBUG_PRINT_TOC 1 */
#if DEBUG_PRINT_TOC
#define PRINT_TOC(d, outbytes)	do {				\
	unsigned char *x = (unsigned char *)(uintptr_t)d;	\
	unsigned char c = x[outbytes-1];			\
	x[outbytes - 1] = 0;					\
	fprintf(stderr, "%s", x);				\
	fprintf(stderr, "%c", c);				\
	x[outbytes - 1] = c;					\
} while (0)
#else
#define PRINT_TOC(d, outbytes)
#endif

#define HEADER_MAGIC	0x78617221
#define HEADER_SIZE	28
#define HEADER_VERSION	1
#define CKSUM_NONE	0
#define CKSUM_SHA1	1
#define CKSUM_MD5	2

#define MD5_SIZE	16
#define SHA1_SIZE	20
#define MAX_SUM_SIZE	20

enum enctype {
	NONE,
	GZIP,
	BZIP2,
	LZMA,
	XZ,
};

struct chksumval {
	int			 alg;
	size_t			 len;
	unsigned char		 val[MAX_SUM_SIZE];
};

struct chksumwork {
	int			 alg;
#ifdef ARCHIVE_HAS_MD5
	archive_md5_ctx		 md5ctx;
#endif
#ifdef ARCHIVE_HAS_SHA1
	archive_sha1_ctx	 sha1ctx;
#endif
};

struct xattr {
	struct xattr		*next;
	struct archive_string	 name;
	uint64_t		 id;
	uint64_t		 length;
	uint64_t		 offset;
	uint64_t		 size;
	enum enctype		 encoding;
	struct chksumval	 a_sum;
	struct chksumval	 e_sum;
	struct archive_string	 fstype;
};

struct xar_file {
	struct xar_file		*next;
	struct xar_file		*hdnext;
	struct xar_file		*parent;
	int			 subdirs;

	unsigned int		 has;
#define HAS_DATA		0x00001
#define HAS_PATHNAME		0x00002
#define HAS_SYMLINK		0x00004
#define HAS_TIME		0x00008
#define HAS_UID			0x00010
#define HAS_GID			0x00020
#define HAS_MODE		0x00040
#define HAS_TYPE		0x00080
#define HAS_DEV			0x00100
#define HAS_DEVMAJOR		0x00200
#define HAS_DEVMINOR		0x00400
#define HAS_INO			0x00800
#define HAS_FFLAGS		0x01000
#define HAS_XATTR		0x02000
#define HAS_ACL			0x04000
#define HAS_CTIME		0x08000
#define HAS_MTIME		0x10000
#define HAS_ATIME		0x20000

	uint64_t		 id;
	uint64_t		 length;
	uint64_t		 offset;
	uint64_t		 size;
	enum enctype		 encoding;
	struct chksumval	 a_sum;
	struct chksumval	 e_sum;
	struct archive_string	 pathname;
	struct archive_string	 symlink;
	time_t			 ctime;
	time_t			 mtime;
	time_t			 atime;
	struct archive_string	 uname;
	int64_t			 uid;
	struct archive_string	 gname;
	int64_t			 gid;
	mode_t			 mode;
	dev_t			 dev;
	dev_t			 devmajor;
	dev_t			 devminor;
	int64_t			 ino64;
	struct archive_string	 fflags_text;
	unsigned int		 link;
	unsigned int		 nlink;
	struct archive_string	 hardlink;
	struct xattr		*xattr_list;
};

struct hdlink {
	struct hdlink		 *next;

	unsigned int		 id;
	int			 cnt;
	struct xar_file		 *files;
};

struct heap_queue {
	struct xar_file		**files;
	int			 allocated;
	int			 used;
};

enum xmlstatus {
	INIT,
	XAR,
	TOC,
	TOC_CREATION_TIME,
	TOC_CHECKSUM,
	TOC_CHECKSUM_OFFSET,
	TOC_CHECKSUM_SIZE,
	TOC_FILE,
	FILE_DATA,
	FILE_DATA_LENGTH,
	FILE_DATA_OFFSET,
	FILE_DATA_SIZE,
	FILE_DATA_ENCODING,
	FILE_DATA_A_CHECKSUM,
	FILE_DATA_E_CHECKSUM,
	FILE_DATA_CONTENT,
	FILE_EA,
	FILE_EA_LENGTH,
	FILE_EA_OFFSET,
	FILE_EA_SIZE,
	FILE_EA_ENCODING,
	FILE_EA_A_CHECKSUM,
	FILE_EA_E_CHECKSUM,
	FILE_EA_NAME,
	FILE_EA_FSTYPE,
	FILE_CTIME,
	FILE_MTIME,
	FILE_ATIME,
	FILE_GROUP,
	FILE_GID,
	FILE_USER,
	FILE_UID,
	FILE_MODE,
	FILE_DEVICE,
	FILE_DEVICE_MAJOR,
	FILE_DEVICE_MINOR,
	FILE_DEVICENO,
	FILE_INODE,
	FILE_LINK,
	FILE_TYPE,
	FILE_NAME,
	FILE_ACL,
	FILE_ACL_DEFAULT,
	FILE_ACL_ACCESS,
	FILE_ACL_APPLEEXTENDED,
	/* BSD file flags. */
	FILE_FLAGS,
	FILE_FLAGS_USER_NODUMP,
	FILE_FLAGS_USER_IMMUTABLE,
	FILE_FLAGS_USER_APPEND,
	FILE_FLAGS_USER_OPAQUE,
	FILE_FLAGS_USER_NOUNLINK,
	FILE_FLAGS_SYS_ARCHIVED,
	FILE_FLAGS_SYS_IMMUTABLE,
	FILE_FLAGS_SYS_APPEND,
	FILE_FLAGS_SYS_NOUNLINK,
	FILE_FLAGS_SYS_SNAPSHOT,
	/* Linux file flags. */
	FILE_EXT2,
	FILE_EXT2_SecureDeletion,
	FILE_EXT2_Undelete,
	FILE_EXT2_Compress,
	FILE_EXT2_Synchronous,
	FILE_EXT2_Immutable,
	FILE_EXT2_AppendOnly,
	FILE_EXT2_NoDump,
	FILE_EXT2_NoAtime,
	FILE_EXT2_CompDirty,
	FILE_EXT2_CompBlock,
	FILE_EXT2_NoCompBlock,
	FILE_EXT2_CompError,
	FILE_EXT2_BTree,
	FILE_EXT2_HashIndexed,
	FILE_EXT2_iMagic,
	FILE_EXT2_Journaled,
	FILE_EXT2_NoTail,
	FILE_EXT2_DirSync,
	FILE_EXT2_TopDir,
	FILE_EXT2_Reserved,
	UNKNOWN,
};

struct unknown_tag {
	struct unknown_tag	*next;
	struct archive_string	 name;
};

struct xar {
	uint64_t		 offset; /* Current position in the file. */
	int64_t			 total;
	uint64_t		 h_base;
	int			 end_of_file;
#define OUTBUFF_SIZE	(1024 * 64)
	unsigned char		*outbuff;

	enum xmlstatus		 xmlsts;
	enum xmlstatus		 xmlsts_unknown;
	struct unknown_tag	*unknowntags;
	int			 base64text;

	/*
	 * TOC
	 */
	uint64_t		 toc_remaining;
	uint64_t		 toc_total;
	uint64_t		 toc_chksum_offset;
	uint64_t		 toc_chksum_size;

	/*
	 * For Decoding data.
	 */
	enum enctype 		 rd_encoding;
	z_stream		 stream;
	int			 stream_valid;
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	bz_stream		 bzstream;
	int			 bzstream_valid;
#endif
#if HAVE_LZMA_H && HAVE_LIBLZMA
	lzma_stream		 lzstream;
	int			 lzstream_valid;
#endif
	/*
	 * For Checksum data.
	 */
	struct chksumwork	 a_sumwrk;
	struct chksumwork	 e_sumwrk;

	struct xar_file		*file;	/* current reading file. */
	struct xattr		*xattr; /* current reading extended attribute. */
	struct heap_queue	 file_queue;
	struct xar_file		*hdlink_orgs;
	struct hdlink		*hdlink_list;

	int	 		 entry_init;
	uint64_t		 entry_total;
	uint64_t		 entry_remaining;
	size_t			 entry_unconsumed;
	uint64_t		 entry_size;
	enum enctype 		 entry_encoding;
	struct chksumval	 entry_a_sum;
	struct chksumval	 entry_e_sum;

	struct archive_string_conv *sconv;
};

struct xmlattr {
	struct xmlattr	*next;
	char		*name;
	char		*value;
};

struct xmlattr_list {
	struct xmlattr	*first;
	struct xmlattr	**last;
};

static int	xar_bid(struct archive_read *, int);
static int	xar_read_header(struct archive_read *,
		    struct archive_entry *);
static int	xar_read_data(struct archive_read *,
		    const void **, size_t *, int64_t *);
static int	xar_read_data_skip(struct archive_read *);
static int	xar_cleanup(struct archive_read *);
static int	move_reading_point(struct archive_read *, uint64_t);
static int	rd_contents_init(struct archive_read *,
		    enum enctype, int, int);
static int	rd_contents(struct archive_read *, const void **,
		    size_t *, size_t *, uint64_t);
static uint64_t	atol10(const char *, size_t);
static int64_t	atol8(const char *, size_t);
static size_t	atohex(unsigned char *, size_t, const char *, size_t);
static time_t	parse_time(const char *p, size_t n);
static int	heap_add_entry(struct archive_read *a,
    struct heap_queue *, struct xar_file *);
static struct xar_file *heap_get_entry(struct heap_queue *);
static int	add_link(struct archive_read *,
    struct xar *, struct xar_file *);
static void	checksum_init(struct archive_read *, int, int);
static void	checksum_update(struct archive_read *, const void *,
		    size_t, const void *, size_t);
static int	checksum_final(struct archive_read *, const void *,
		    size_t, const void *, size_t);
static void	checksum_cleanup(struct archive_read *);
static int	decompression_init(struct archive_read *, enum enctype);
static int	decompress(struct archive_read *, const void **,
		    size_t *, const void *, size_t *);
static int	decompression_cleanup(struct archive_read *);
static void	xmlattr_cleanup(struct xmlattr_list *);
static int	file_new(struct archive_read *,
    struct xar *, struct xmlattr_list *);
static void	file_free(struct xar_file *);
static int	xattr_new(struct archive_read *,
    struct xar *, struct xmlattr_list *);
static void	xattr_free(struct xattr *);
static int	getencoding(struct xmlattr_list *);
static int	getsumalgorithm(struct xmlattr_list *);
static int	unknowntag_start(struct archive_read *,
    struct xar *, const char *);
static void	unknowntag_end(struct xar *, const char *);
static int	xml_start(struct archive_read *,
    const char *, struct xmlattr_list *);
static void	xml_end(void *, const char *);
static void	xml_data(void *, const char *, int);
static int	xml_parse_file_flags(struct xar *, const char *);
static int	xml_parse_file_ext2(struct xar *, const char *);
#if defined(HAVE_LIBXML_XMLREADER_H)
static int	xml2_xmlattr_setup(struct archive_read *,
    struct xmlattr_list *, xmlTextReaderPtr);
static int	xml2_read_cb(void *, char *, int);
static int	xml2_close_cb(void *);
static void	xml2_error_hdr(void *, const char *, xmlParserSeverities,
		    xmlTextReaderLocatorPtr);
static int	xml2_read_toc(struct archive_read *);
#elif defined(HAVE_BSDXML_H) || defined(HAVE_EXPAT_H)
struct expat_userData {
	int state;
	struct archive_read *archive;
};
static int	expat_xmlattr_setup(struct archive_read *,
    struct xmlattr_list *, const XML_Char **);
static void	expat_start_cb(void *, const XML_Char *, const XML_Char **);
static void	expat_end_cb(void *, const XML_Char *);
static void	expat_data_cb(void *, const XML_Char *, int);
static int	expat_read_toc(struct archive_read *);
#endif

int
archive_read_support_format_xar(struct archive *_a)
{
	struct xar *xar;
	struct archive_read *a = (struct archive_read *)_a;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_xar");

	xar = (struct xar *)calloc(1, sizeof(*xar));
	if (xar == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate xar data");
		return (ARCHIVE_FATAL);
	}

	r = __archive_read_register_format(a,
	    xar,
	    "xar",
	    xar_bid,
	    NULL,
	    xar_read_header,
	    xar_read_data,
	    xar_read_data_skip,
	    NULL,
	    xar_cleanup,
	    NULL,
	    NULL);
	if (r != ARCHIVE_OK)
		free(xar);
	return (r);
}

static int
xar_bid(struct archive_read *a, int best_bid)
{
	const unsigned char *b;
	int bid;

	(void)best_bid; /* UNUSED */

	b = __archive_read_ahead(a, HEADER_SIZE, NULL);
	if (b == NULL)
		return (-1);

	bid = 0;
	/*
	 * Verify magic code
	 */
	if (archive_be32dec(b) != HEADER_MAGIC)
		return (0);
	bid += 32;
	/*
	 * Verify header size
	 */
	if (archive_be16dec(b+4) != HEADER_SIZE)
		return (0);
	bid += 16;
	/*
	 * Verify header version
	 */
	if (archive_be16dec(b+6) != HEADER_VERSION)
		return (0);
	bid += 16;
	/*
	 * Verify type of checksum
	 */
	switch (archive_be32dec(b+24)) {
	case CKSUM_NONE:
	case CKSUM_SHA1:
	case CKSUM_MD5:
		bid += 32;
		break;
	default:
		return (0);
	}

	return (bid);
}

static int
read_toc(struct archive_read *a)
{
	struct xar *xar;
	struct xar_file *file;
	const unsigned char *b;
	uint64_t toc_compressed_size;
	uint64_t toc_uncompressed_size;
	uint32_t toc_chksum_alg;
	ssize_t bytes;
	int r;

	xar = (struct xar *)(a->format->data);

	/*
	 * Read xar header.
	 */
	b = __archive_read_ahead(a, HEADER_SIZE, &bytes);
	if (bytes < 0)
		return ((int)bytes);
	if (bytes < HEADER_SIZE) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Truncated archive header");
		return (ARCHIVE_FATAL);
	}

	if (archive_be32dec(b) != HEADER_MAGIC) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid header magic");
		return (ARCHIVE_FATAL);
	}
	if (archive_be16dec(b+6) != HEADER_VERSION) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unsupported header version(%d)",
		    archive_be16dec(b+6));
		return (ARCHIVE_FATAL);
	}
	toc_compressed_size = archive_be64dec(b+8);
	xar->toc_remaining = toc_compressed_size;
	toc_uncompressed_size = archive_be64dec(b+16);
	toc_chksum_alg = archive_be32dec(b+24);
	__archive_read_consume(a, HEADER_SIZE);
	xar->offset += HEADER_SIZE;
	xar->toc_total = 0;

	/*
	 * Read TOC(Table of Contents).
	 */
	/* Initialize reading contents. */
	r = move_reading_point(a, HEADER_SIZE);
	if (r != ARCHIVE_OK)
		return (r);
	r = rd_contents_init(a, GZIP, toc_chksum_alg, CKSUM_NONE);
	if (r != ARCHIVE_OK)
		return (r);

#ifdef HAVE_LIBXML_XMLREADER_H
	r = xml2_read_toc(a);
#elif defined(HAVE_BSDXML_H) || defined(HAVE_EXPAT_H)
	r = expat_read_toc(a);
#endif
	if (r != ARCHIVE_OK)
		return (r);

	/* Set 'The HEAP' base. */
	xar->h_base = xar->offset;
	if (xar->toc_total != toc_uncompressed_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "TOC uncompressed size error");
		return (ARCHIVE_FATAL);
	}

	/*
	 * Checksum TOC
	 */
	if (toc_chksum_alg != CKSUM_NONE) {
		r = move_reading_point(a, xar->toc_chksum_offset);
		if (r != ARCHIVE_OK)
			return (r);
		b = __archive_read_ahead(a,
			(size_t)xar->toc_chksum_size, &bytes);
		if (bytes < 0)
			return ((int)bytes);
		if ((uint64_t)bytes < xar->toc_chksum_size) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Truncated archive file");
			return (ARCHIVE_FATAL);
		}
		r = checksum_final(a, b,
			(size_t)xar->toc_chksum_size, NULL, 0);
		__archive_read_consume(a, xar->toc_chksum_size);
		xar->offset += xar->toc_chksum_size;
		if (r != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Connect hardlinked files.
	 */
	for (file = xar->hdlink_orgs; file != NULL; file = file->hdnext) {
		struct hdlink **hdlink;

		for (hdlink = &(xar->hdlink_list); *hdlink != NULL;
		    hdlink = &((*hdlink)->next)) {
			if ((*hdlink)->id == file->id) {
				struct hdlink *hltmp;
				struct xar_file *f2;
				int nlink = (*hdlink)->cnt + 1;

				file->nlink = nlink;
				for (f2 = (*hdlink)->files; f2 != NULL;
				    f2 = f2->hdnext) {
					f2->nlink = nlink;
					archive_string_copy(
					    &(f2->hardlink), &(file->pathname));
				}
				/* Remove resolved files from hdlist_list. */
				hltmp = *hdlink;
				*hdlink = hltmp->next;
				free(hltmp);
				break;
			}
		}
	}
	a->archive.archive_format = ARCHIVE_FORMAT_XAR;
	a->archive.archive_format_name = "xar";

	return (ARCHIVE_OK);
}

static int
xar_read_header(struct archive_read *a, struct archive_entry *entry)
{
	struct xar *xar;
	struct xar_file *file;
	struct xattr *xattr;
	int r;

	xar = (struct xar *)(a->format->data);
	r = ARCHIVE_OK;

	if (xar->offset == 0) {
		/* Create a character conversion object. */
		if (xar->sconv == NULL) {
			xar->sconv = archive_string_conversion_from_charset(
			    &(a->archive), "UTF-8", 1);
			if (xar->sconv == NULL)
				return (ARCHIVE_FATAL);
		}

		/* Read TOC. */
		r = read_toc(a);
		if (r != ARCHIVE_OK)
			return (r);
	}

	for (;;) {
		file = xar->file = heap_get_entry(&(xar->file_queue));
		if (file == NULL) {
			xar->end_of_file = 1;
			return (ARCHIVE_EOF);
		}
		if ((file->mode & AE_IFMT) != AE_IFDIR)
			break;
		if (file->has != (HAS_PATHNAME | HAS_TYPE))
			break;
		/*
		 * If a file type is a directory and it does not have
		 * any metadata, do not export.
		 */
		file_free(file);
	}
        if (file->has & HAS_ATIME) {
          archive_entry_set_atime(entry, file->atime, 0);
        }
        if (file->has & HAS_CTIME) {
          archive_entry_set_ctime(entry, file->ctime, 0);
        }
        if (file->has & HAS_MTIME) {
          archive_entry_set_mtime(entry, file->mtime, 0);
        }
	archive_entry_set_gid(entry, file->gid);
	if (file->gname.length > 0 &&
	    archive_entry_copy_gname_l(entry, file->gname.s,
		archive_strlen(&(file->gname)), xar->sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Gname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Gname cannot be converted from %s to current locale.",
		    archive_string_conversion_charset_name(xar->sconv));
		r = ARCHIVE_WARN;
	}
	archive_entry_set_uid(entry, file->uid);
	if (file->uname.length > 0 &&
	    archive_entry_copy_uname_l(entry, file->uname.s,
		archive_strlen(&(file->uname)), xar->sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Uname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Uname cannot be converted from %s to current locale.",
		    archive_string_conversion_charset_name(xar->sconv));
		r = ARCHIVE_WARN;
	}
	archive_entry_set_mode(entry, file->mode);
	if (archive_entry_copy_pathname_l(entry, file->pathname.s,
	    archive_strlen(&(file->pathname)), xar->sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Pathname cannot be converted from %s to current locale.",
		    archive_string_conversion_charset_name(xar->sconv));
		r = ARCHIVE_WARN;
	}


	if (file->symlink.length > 0 &&
	    archive_entry_copy_symlink_l(entry, file->symlink.s,
		archive_strlen(&(file->symlink)), xar->sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Linkname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Linkname cannot be converted from %s to current locale.",
		    archive_string_conversion_charset_name(xar->sconv));
		r = ARCHIVE_WARN;
	}
	/* Set proper nlink. */
	if ((file->mode & AE_IFMT) == AE_IFDIR)
		archive_entry_set_nlink(entry, file->subdirs + 2);
	else
		archive_entry_set_nlink(entry, file->nlink);
	archive_entry_set_size(entry, file->size);
	if (archive_strlen(&(file->hardlink)) > 0)
		archive_entry_set_hardlink(entry, file->hardlink.s);
	archive_entry_set_ino64(entry, file->ino64);
	if (file->has & HAS_DEV)
		archive_entry_set_dev(entry, file->dev);
	if (file->has & HAS_DEVMAJOR)
		archive_entry_set_devmajor(entry, file->devmajor);
	if (file->has & HAS_DEVMINOR)
		archive_entry_set_devminor(entry, file->devminor);
	if (archive_strlen(&(file->fflags_text)) > 0)
		archive_entry_copy_fflags_text(entry, file->fflags_text.s);

	xar->entry_init = 1;
	xar->entry_total = 0;
	xar->entry_remaining = file->length;
	xar->entry_size = file->size;
	xar->entry_encoding = file->encoding;
	xar->entry_a_sum = file->a_sum;
	xar->entry_e_sum = file->e_sum;
	/*
	 * Read extended attributes.
	 */
	xattr = file->xattr_list;
	while (xattr != NULL) {
		const void *d;
		size_t outbytes, used;

		r = move_reading_point(a, xattr->offset);
		if (r != ARCHIVE_OK)
			break;
		r = rd_contents_init(a, xattr->encoding,
		    xattr->a_sum.alg, xattr->e_sum.alg);
		if (r != ARCHIVE_OK)
			break;
		d = NULL;
		r = rd_contents(a, &d, &outbytes, &used, xattr->length);
		if (r != ARCHIVE_OK)
			break;
		if (outbytes != xattr->size) {
			archive_set_error(&(a->archive), ARCHIVE_ERRNO_MISC,
			    "Decompressed size error");
			r = ARCHIVE_FATAL;
			break;
		}
		r = checksum_final(a,
		    xattr->a_sum.val, xattr->a_sum.len,
		    xattr->e_sum.val, xattr->e_sum.len);
		if (r != ARCHIVE_OK)
			break;
		archive_entry_xattr_add_entry(entry,
		    xattr->name.s, d, outbytes);
		xattr = xattr->next;
	}
	if (r != ARCHIVE_OK) {
		file_free(file);
		return (r);
	}

	if (xar->entry_remaining > 0)
		/* Move reading point to the beginning of current
		 * file contents. */
		r = move_reading_point(a, file->offset);
	else
		r = ARCHIVE_OK;

	file_free(file);
	return (r);
}

static int
xar_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	struct xar *xar;
	size_t used;
	int r;

	xar = (struct xar *)(a->format->data);

	if (xar->entry_unconsumed) {
		__archive_read_consume(a, xar->entry_unconsumed);
		xar->entry_unconsumed = 0;
	}

	if (xar->end_of_file || xar->entry_remaining <= 0) {
		r = ARCHIVE_EOF;
		goto abort_read_data;
	}

	if (xar->entry_init) {
		r = rd_contents_init(a, xar->entry_encoding,
		    xar->entry_a_sum.alg, xar->entry_e_sum.alg);
		if (r != ARCHIVE_OK) {
			xar->entry_remaining = 0;
			return (r);
		}
		xar->entry_init = 0;
	}

	*buff = NULL;
	r = rd_contents(a, buff, size, &used, xar->entry_remaining);
	if (r != ARCHIVE_OK)
		goto abort_read_data;

	*offset = xar->entry_total;
	xar->entry_total += *size;
	xar->total += *size;
	xar->offset += used;
	xar->entry_remaining -= used;
	xar->entry_unconsumed = used;

	if (xar->entry_remaining == 0) {
		if (xar->entry_total != xar->entry_size) {
			archive_set_error(&(a->archive), ARCHIVE_ERRNO_MISC,
			    "Decompressed size error");
			r = ARCHIVE_FATAL;
			goto abort_read_data;
		}
		r = checksum_final(a,
		    xar->entry_a_sum.val, xar->entry_a_sum.len,
		    xar->entry_e_sum.val, xar->entry_e_sum.len);
		if (r != ARCHIVE_OK)
			goto abort_read_data;
	}

	return (ARCHIVE_OK);
abort_read_data:
	*buff = NULL;
	*size = 0;
	*offset = xar->total;
	return (r);
}

static int
xar_read_data_skip(struct archive_read *a)
{
	struct xar *xar;
	int64_t bytes_skipped;

	xar = (struct xar *)(a->format->data);
	if (xar->end_of_file)
		return (ARCHIVE_EOF);
	bytes_skipped = __archive_read_consume(a, xar->entry_remaining +
		xar->entry_unconsumed);
	if (bytes_skipped < 0)
		return (ARCHIVE_FATAL);
	xar->offset += bytes_skipped;
	xar->entry_unconsumed = 0;
	return (ARCHIVE_OK);
}

static int
xar_cleanup(struct archive_read *a)
{
	struct xar *xar;
	struct hdlink *hdlink;
	int i;
	int r;

	xar = (struct xar *)(a->format->data);
	checksum_cleanup(a);
	r = decompression_cleanup(a);
	hdlink = xar->hdlink_list;
	while (hdlink != NULL) {
		struct hdlink *next = hdlink->next;

		free(hdlink);
		hdlink = next;
	}
	for (i = 0; i < xar->file_queue.used; i++)
		file_free(xar->file_queue.files[i]);
	free(xar->file_queue.files);
	while (xar->unknowntags != NULL) {
		struct unknown_tag *tag;

		tag = xar->unknowntags;
		xar->unknowntags = tag->next;
		archive_string_free(&(tag->name));
		free(tag);
	}
	free(xar->outbuff);
	free(xar);
	a->format->data = NULL;
	return (r);
}

static int
move_reading_point(struct archive_read *a, uint64_t offset)
{
	struct xar *xar;

	xar = (struct xar *)(a->format->data);
	if (xar->offset - xar->h_base != offset) {
		/* Seek forward to the start of file contents. */
		int64_t step;

		step = offset - (xar->offset - xar->h_base);
		if (step > 0) {
			step = __archive_read_consume(a, step);
			if (step < 0)
				return ((int)step);
			xar->offset += step;
		} else {
			int64_t pos = __archive_read_seek(a, xar->h_base + offset, SEEK_SET);
			if (pos == ARCHIVE_FAILED) {
				archive_set_error(&(a->archive),
				    ARCHIVE_ERRNO_MISC,
				    "Cannot seek.");
				return (ARCHIVE_FAILED);
			}
			xar->offset = pos;
		}
	}
	return (ARCHIVE_OK);
}

static int
rd_contents_init(struct archive_read *a, enum enctype encoding,
    int a_sum_alg, int e_sum_alg)
{
	int r;

	/* Init decompress library. */
	if ((r = decompression_init(a, encoding)) != ARCHIVE_OK)
		return (r);
	/* Init checksum library. */
	checksum_init(a, a_sum_alg, e_sum_alg);
	return (ARCHIVE_OK);
}

static int
rd_contents(struct archive_read *a, const void **buff, size_t *size,
    size_t *used, uint64_t remaining)
{
	const unsigned char *b;
	ssize_t bytes;

	/* Get whatever bytes are immediately available. */
	b = __archive_read_ahead(a, 1, &bytes);
	if (bytes < 0)
		return ((int)bytes);
	if (bytes == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Truncated archive file");
		return (ARCHIVE_FATAL);
	}
	if ((uint64_t)bytes > remaining)
		bytes = (ssize_t)remaining;

	/*
	 * Decompress contents of file.
	 */
	*used = bytes;
	if (decompress(a, buff, size, b, used) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	/*
	 * Update checksum of a compressed data and a extracted data.
	 */
	checksum_update(a, b, *used, *buff, *size);

	return (ARCHIVE_OK);
}

/*
 * Note that this implementation does not (and should not!) obey
 * locale settings; you cannot simply substitute strtol here, since
 * it does obey locale.
 */

static uint64_t
atol10(const char *p, size_t char_cnt)
{
	uint64_t l;
	int digit;

	if (char_cnt == 0)
		return (0);

	l = 0;
	digit = *p - '0';
	while (digit >= 0 && digit < 10  && char_cnt-- > 0) {
		l = (l * 10) + digit;
		digit = *++p - '0';
	}
	return (l);
}

static int64_t
atol8(const char *p, size_t char_cnt)
{
	int64_t l;
	int digit;

	if (char_cnt == 0)
		return (0);

	l = 0;
	while (char_cnt-- > 0) {
		if (*p >= '0' && *p <= '7')
			digit = *p - '0';
		else
			break;
		p++;
		l <<= 3;
		l |= digit;
	}
	return (l);
}

static size_t
atohex(unsigned char *b, size_t bsize, const char *p, size_t psize)
{
	size_t fbsize = bsize;

	while (bsize && psize > 1) {
		unsigned char x;

		if (p[0] >= 'a' && p[0] <= 'z')
			x = (p[0] - 'a' + 0x0a) << 4;
		else if (p[0] >= 'A' && p[0] <= 'Z')
			x = (p[0] - 'A' + 0x0a) << 4;
		else if (p[0] >= '0' && p[0] <= '9')
			x = (p[0] - '0') << 4;
		else
			return (-1);
		if (p[1] >= 'a' && p[1] <= 'z')
			x |= p[1] - 'a' + 0x0a;
		else if (p[1] >= 'A' && p[1] <= 'Z')
			x |= p[1] - 'A' + 0x0a;
		else if (p[1] >= '0' && p[1] <= '9')
			x |= p[1] - '0';
		else
			return (-1);
		
		*b++ = x;
		bsize--;
		p += 2;
		psize -= 2;
	}
	return (fbsize - bsize);
}

static time_t
time_from_tm(struct tm *t)
{
#if HAVE_TIMEGM
        /* Use platform timegm() if available. */
        return (timegm(t));
#elif HAVE__MKGMTIME64
        return (_mkgmtime64(t));
#else
        /* Else use direct calculation using POSIX assumptions. */
        /* First, fix up tm_yday based on the year/month/day. */
        mktime(t);
        /* Then we can compute timegm() from first principles. */
        return (t->tm_sec
            + t->tm_min * 60
            + t->tm_hour * 3600
            + t->tm_yday * 86400
            + (t->tm_year - 70) * 31536000
            + ((t->tm_year - 69) / 4) * 86400
            - ((t->tm_year - 1) / 100) * 86400
            + ((t->tm_year + 299) / 400) * 86400);
#endif
}

static time_t
parse_time(const char *p, size_t n)
{
	struct tm tm;
	time_t t = 0;
	int64_t data;

	memset(&tm, 0, sizeof(tm));
	if (n != 20)
		return (t);
	data = atol10(p, 4);
	if (data < 1900)
		return (t);
	tm.tm_year = (int)data - 1900;
	p += 4;
	if (*p++ != '-')
		return (t);
	data = atol10(p, 2);
	if (data < 1 || data > 12)
		return (t);
	tm.tm_mon = (int)data -1;
	p += 2;
	if (*p++ != '-')
		return (t);
	data = atol10(p, 2);
	if (data < 1 || data > 31)
		return (t);
	tm.tm_mday = (int)data;
	p += 2;
	if (*p++ != 'T')
		return (t);
	data = atol10(p, 2);
	if (data < 0 || data > 23)
		return (t);
	tm.tm_hour = (int)data;
	p += 2;
	if (*p++ != ':')
		return (t);
	data = atol10(p, 2);
	if (data < 0 || data > 59)
		return (t);
	tm.tm_min = (int)data;
	p += 2;
	if (*p++ != ':')
		return (t);
	data = atol10(p, 2);
	if (data < 0 || data > 60)
		return (t);
	tm.tm_sec = (int)data;
#if 0
	p += 2;
	if (*p != 'Z')
		return (t);
#endif

	t = time_from_tm(&tm);

	return (t);
}

static int
heap_add_entry(struct archive_read *a,
    struct heap_queue *heap, struct xar_file *file)
{
	uint64_t file_id, parent_id;
	int hole, parent;

	/* Expand our pending files list as necessary. */
	if (heap->used >= heap->allocated) {
		struct xar_file **new_pending_files;
		int new_size = heap->allocated * 2;

		if (heap->allocated < 1024)
			new_size = 1024;
		/* Overflow might keep us from growing the list. */
		if (new_size <= heap->allocated) {
			archive_set_error(&a->archive,
			    ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		new_pending_files = (struct xar_file **)
		    malloc(new_size * sizeof(new_pending_files[0]));
		if (new_pending_files == NULL) {
			archive_set_error(&a->archive,
			    ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		memcpy(new_pending_files, heap->files,
		    heap->allocated * sizeof(new_pending_files[0]));
		free(heap->files);
		heap->files = new_pending_files;
		heap->allocated = new_size;
	}

	file_id = file->id;

	/*
	 * Start with hole at end, walk it up tree to find insertion point.
	 */
	hole = heap->used++;
	while (hole > 0) {
		parent = (hole - 1)/2;
		parent_id = heap->files[parent]->id;
		if (file_id >= parent_id) {
			heap->files[hole] = file;
			return (ARCHIVE_OK);
		}
		/* Move parent into hole <==> move hole up tree. */
		heap->files[hole] = heap->files[parent];
		hole = parent;
	}
	heap->files[0] = file;

	return (ARCHIVE_OK);
}

static struct xar_file *
heap_get_entry(struct heap_queue *heap)
{
	uint64_t a_id, b_id, c_id;
	int a, b, c;
	struct xar_file *r, *tmp;

	if (heap->used < 1)
		return (NULL);

	/*
	 * The first file in the list is the earliest; we'll return this.
	 */
	r = heap->files[0];

	/*
	 * Move the last item in the heap to the root of the tree
	 */
	heap->files[0] = heap->files[--(heap->used)];

	/*
	 * Rebalance the heap.
	 */
	a = 0; /* Starting element and its heap key */
	a_id = heap->files[a]->id;
	for (;;) {
		b = a + a + 1; /* First child */
		if (b >= heap->used)
			return (r);
		b_id = heap->files[b]->id;
		c = b + 1; /* Use second child if it is smaller. */
		if (c < heap->used) {
			c_id = heap->files[c]->id;
			if (c_id < b_id) {
				b = c;
				b_id = c_id;
			}
		}
		if (a_id <= b_id)
			return (r);
		tmp = heap->files[a];
		heap->files[a] = heap->files[b];
		heap->files[b] = tmp;
		a = b;
	}
}

static int
add_link(struct archive_read *a, struct xar *xar, struct xar_file *file)
{
	struct hdlink *hdlink;

	for (hdlink = xar->hdlink_list; hdlink != NULL; hdlink = hdlink->next) {
		if (hdlink->id == file->link) {
			file->hdnext = hdlink->files;
			hdlink->cnt++;
			hdlink->files = file;
			return (ARCHIVE_OK);
		}
	}
	hdlink = malloc(sizeof(*hdlink));
	if (hdlink == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	file->hdnext = NULL;
	hdlink->id = file->link;
	hdlink->cnt = 1;
	hdlink->files = file;
	hdlink->next = xar->hdlink_list;
	xar->hdlink_list = hdlink;
	return (ARCHIVE_OK);
}

static void
_checksum_init(struct chksumwork *sumwrk, int sum_alg)
{
	sumwrk->alg = sum_alg;
	switch (sum_alg) {
	case CKSUM_NONE:
		break;
	case CKSUM_SHA1:
		archive_sha1_init(&(sumwrk->sha1ctx));
		break;
	case CKSUM_MD5:
		archive_md5_init(&(sumwrk->md5ctx));
		break;
	}
}

static void
_checksum_update(struct chksumwork *sumwrk, const void *buff, size_t size)
{

	switch (sumwrk->alg) {
	case CKSUM_NONE:
		break;
	case CKSUM_SHA1:
		archive_sha1_update(&(sumwrk->sha1ctx), buff, size);
		break;
	case CKSUM_MD5:
		archive_md5_update(&(sumwrk->md5ctx), buff, size);
		break;
	}
}

static int
_checksum_final(struct chksumwork *sumwrk, const void *val, size_t len)
{
	unsigned char sum[MAX_SUM_SIZE];
	int r = ARCHIVE_OK;

	switch (sumwrk->alg) {
	case CKSUM_NONE:
		break;
	case CKSUM_SHA1:
		archive_sha1_final(&(sumwrk->sha1ctx), sum);
		if (len != SHA1_SIZE ||
		    memcmp(val, sum, SHA1_SIZE) != 0)
			r = ARCHIVE_FAILED;
		break;
	case CKSUM_MD5:
		archive_md5_final(&(sumwrk->md5ctx), sum);
		if (len != MD5_SIZE ||
		    memcmp(val, sum, MD5_SIZE) != 0)
			r = ARCHIVE_FAILED;
		break;
	}
	return (r);
}

static void
checksum_init(struct archive_read *a, int a_sum_alg, int e_sum_alg)
{
	struct xar *xar;

	xar = (struct xar *)(a->format->data);
	_checksum_init(&(xar->a_sumwrk), a_sum_alg);
	_checksum_init(&(xar->e_sumwrk), e_sum_alg);
}

static void
checksum_update(struct archive_read *a, const void *abuff, size_t asize,
    const void *ebuff, size_t esize)
{
	struct xar *xar;

	xar = (struct xar *)(a->format->data);
	_checksum_update(&(xar->a_sumwrk), abuff, asize);
	_checksum_update(&(xar->e_sumwrk), ebuff, esize);
}

static int
checksum_final(struct archive_read *a, const void *a_sum_val,
    size_t a_sum_len, const void *e_sum_val, size_t e_sum_len)
{
	struct xar *xar;
	int r;

	xar = (struct xar *)(a->format->data);
	r = _checksum_final(&(xar->a_sumwrk), a_sum_val, a_sum_len);
	if (r == ARCHIVE_OK)
		r = _checksum_final(&(xar->e_sumwrk), e_sum_val, e_sum_len);
	if (r != ARCHIVE_OK)
		archive_set_error(&(a->archive), ARCHIVE_ERRNO_MISC,
		    "Sumcheck error");
	return (r);
}

static int
decompression_init(struct archive_read *a, enum enctype encoding)
{
	struct xar *xar;
	const char *detail;
	int r;

	xar = (struct xar *)(a->format->data);
	xar->rd_encoding = encoding;
	switch (encoding) {
	case NONE:
		break;
	case GZIP:
		if (xar->stream_valid)
			r = inflateReset(&(xar->stream));
		else
			r = inflateInit(&(xar->stream));
		if (r != Z_OK) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Couldn't initialize zlib stream.");
			return (ARCHIVE_FATAL);
		}
		xar->stream_valid = 1;
		xar->stream.total_in = 0;
		xar->stream.total_out = 0;
		break;
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	case BZIP2:
		if (xar->bzstream_valid) {
			BZ2_bzDecompressEnd(&(xar->bzstream));
			xar->bzstream_valid = 0;
		}
		r = BZ2_bzDecompressInit(&(xar->bzstream), 0, 0);
		if (r == BZ_MEM_ERROR)
			r = BZ2_bzDecompressInit(&(xar->bzstream), 0, 1);
		if (r != BZ_OK) {
			int err = ARCHIVE_ERRNO_MISC;
			detail = NULL;
			switch (r) {
			case BZ_PARAM_ERROR:
				detail = "invalid setup parameter";
				break;
			case BZ_MEM_ERROR:
				err = ENOMEM;
				detail = "out of memory";
				break;
			case BZ_CONFIG_ERROR:
				detail = "mis-compiled library";
				break;
			}
			archive_set_error(&a->archive, err,
			    "Internal error initializing decompressor: %s",
			    detail == NULL ? "??" : detail);
			xar->bzstream_valid = 0;
			return (ARCHIVE_FATAL);
		}
		xar->bzstream_valid = 1;
		xar->bzstream.total_in_lo32 = 0;
		xar->bzstream.total_in_hi32 = 0;
		xar->bzstream.total_out_lo32 = 0;
		xar->bzstream.total_out_hi32 = 0;
		break;
#endif
#if defined(HAVE_LZMA_H) && defined(HAVE_LIBLZMA)
#if LZMA_VERSION_MAJOR >= 5
/* Effectively disable the limiter. */
#define LZMA_MEMLIMIT   UINT64_MAX
#else
/* NOTE: This needs to check memory size which running system has. */
#define LZMA_MEMLIMIT   (1U << 30)
#endif
	case XZ:
	case LZMA:
		if (xar->lzstream_valid) {
			lzma_end(&(xar->lzstream));
			xar->lzstream_valid = 0;
		}
		if (xar->entry_encoding == XZ)
			r = lzma_stream_decoder(&(xar->lzstream),
			    LZMA_MEMLIMIT,/* memlimit */
			    LZMA_CONCATENATED);
		else
			r = lzma_alone_decoder(&(xar->lzstream),
			    LZMA_MEMLIMIT);/* memlimit */
		if (r != LZMA_OK) {
			switch (r) {
			case LZMA_MEM_ERROR:
				archive_set_error(&a->archive,
				    ENOMEM,
				    "Internal error initializing "
				    "compression library: "
				    "Cannot allocate memory");
				break;
			case LZMA_OPTIONS_ERROR:
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Internal error initializing "
				    "compression library: "
				    "Invalid or unsupported options");
				break;
			default:
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Internal error initializing "
				    "lzma library");
				break;
			}
			return (ARCHIVE_FATAL);
		}
		xar->lzstream_valid = 1;
		xar->lzstream.total_in = 0;
		xar->lzstream.total_out = 0;
		break;
#endif
	/*
	 * Unsupported compression.
	 */
	default:
#if !defined(HAVE_BZLIB_H) || !defined(BZ_CONFIG_ERROR)
	case BZIP2:
#endif
#if !defined(HAVE_LZMA_H) || !defined(HAVE_LIBLZMA)
	case LZMA:
	case XZ:
#endif
		switch (xar->entry_encoding) {
		case BZIP2: detail = "bzip2"; break;
		case LZMA: detail = "lzma"; break;
		case XZ: detail = "xz"; break;
		default: detail = "??"; break;
		}
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "%s compression not supported on this platform",
		    detail);
		return (ARCHIVE_FAILED);
	}
	return (ARCHIVE_OK);
}

static int
decompress(struct archive_read *a, const void **buff, size_t *outbytes,
    const void *b, size_t *used)
{
	struct xar *xar;
	void *outbuff;
	size_t avail_in, avail_out;
	int r;

	xar = (struct xar *)(a->format->data);
	avail_in = *used;
	outbuff = (void *)(uintptr_t)*buff;
	if (outbuff == NULL) {
		if (xar->outbuff == NULL) {
			xar->outbuff = malloc(OUTBUFF_SIZE);
			if (xar->outbuff == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "Couldn't allocate memory for out buffer");
				return (ARCHIVE_FATAL);
			}
		}
		outbuff = xar->outbuff;
		*buff = outbuff;
		avail_out = OUTBUFF_SIZE;
	} else
		avail_out = *outbytes;
	switch (xar->rd_encoding) {
	case GZIP:
		xar->stream.next_in = (Bytef *)(uintptr_t)b;
		xar->stream.avail_in = avail_in;
		xar->stream.next_out = (unsigned char *)outbuff;
		xar->stream.avail_out = avail_out;
		r = inflate(&(xar->stream), 0);
		switch (r) {
		case Z_OK: /* Decompressor made some progress.*/
		case Z_STREAM_END: /* Found end of stream. */
			break;
		default:
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "File decompression failed (%d)", r);
			return (ARCHIVE_FATAL);
		}
		*used = avail_in - xar->stream.avail_in;
		*outbytes = avail_out - xar->stream.avail_out;
		break;
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	case BZIP2:
		xar->bzstream.next_in = (char *)(uintptr_t)b;
		xar->bzstream.avail_in = avail_in;
		xar->bzstream.next_out = (char *)outbuff;
		xar->bzstream.avail_out = avail_out;
		r = BZ2_bzDecompress(&(xar->bzstream));
		switch (r) {
		case BZ_STREAM_END: /* Found end of stream. */
			switch (BZ2_bzDecompressEnd(&(xar->bzstream))) {
			case BZ_OK:
				break;
			default:
				archive_set_error(&(a->archive),
				    ARCHIVE_ERRNO_MISC,
				    "Failed to clean up decompressor");
				return (ARCHIVE_FATAL);
			}
			xar->bzstream_valid = 0;
			/* FALLTHROUGH */
		case BZ_OK: /* Decompressor made some progress. */
			break;
		default:
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "bzip decompression failed");
			return (ARCHIVE_FATAL);
		}
		*used = avail_in - xar->bzstream.avail_in;
		*outbytes = avail_out - xar->bzstream.avail_out;
		break;
#endif
#if defined(HAVE_LZMA_H) && defined(HAVE_LIBLZMA)
	case LZMA:
	case XZ:
		xar->lzstream.next_in = b;
		xar->lzstream.avail_in = avail_in;
		xar->lzstream.next_out = (unsigned char *)outbuff;
		xar->lzstream.avail_out = avail_out;
		r = lzma_code(&(xar->lzstream), LZMA_RUN);
		switch (r) {
		case LZMA_STREAM_END: /* Found end of stream. */
			lzma_end(&(xar->lzstream));
			xar->lzstream_valid = 0;
			/* FALLTHROUGH */
		case LZMA_OK: /* Decompressor made some progress. */
			break;
		default:
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "%s decompression failed(%d)",
			    (xar->entry_encoding == XZ)?"xz":"lzma",
			    r);
			return (ARCHIVE_FATAL);
		}
		*used = avail_in - xar->lzstream.avail_in;
		*outbytes = avail_out - xar->lzstream.avail_out;
		break;
#endif
#if !defined(HAVE_BZLIB_H) || !defined(BZ_CONFIG_ERROR)
	case BZIP2:
#endif
#if !defined(HAVE_LZMA_H) || !defined(HAVE_LIBLZMA)
	case LZMA:
	case XZ:
#endif
	case NONE:
	default:
		if (outbuff == xar->outbuff) {
			*buff = b;
			*used = avail_in;
			*outbytes = avail_in;
		} else {
			if (avail_out > avail_in)
				avail_out = avail_in;
			memcpy(outbuff, b, avail_out);
			*used = avail_out;
			*outbytes = avail_out;
		}
		break;
	}
	return (ARCHIVE_OK);
}

static int
decompression_cleanup(struct archive_read *a)
{
	struct xar *xar;
	int r;

	xar = (struct xar *)(a->format->data);
	r = ARCHIVE_OK;
	if (xar->stream_valid) {
		if (inflateEnd(&(xar->stream)) != Z_OK) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "Failed to clean up zlib decompressor");
			r = ARCHIVE_FATAL;
		}
	}
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	if (xar->bzstream_valid) {
		if (BZ2_bzDecompressEnd(&(xar->bzstream)) != BZ_OK) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "Failed to clean up bzip2 decompressor");
			r = ARCHIVE_FATAL;
		}
	}
#endif
#if defined(HAVE_LZMA_H) && defined(HAVE_LIBLZMA)
	if (xar->lzstream_valid)
		lzma_end(&(xar->lzstream));
#elif defined(HAVE_LZMA_H) && defined(HAVE_LIBLZMA)
	if (xar->lzstream_valid) {
		if (lzmadec_end(&(xar->lzstream)) != LZMADEC_OK) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "Failed to clean up lzmadec decompressor");
			r = ARCHIVE_FATAL;
		}
	}
#endif
	return (r);
}

static void
checksum_cleanup(struct archive_read *a) {
	struct xar *xar;

	xar = (struct xar *)(a->format->data);

	_checksum_final(&(xar->a_sumwrk), NULL, 0);
	_checksum_final(&(xar->e_sumwrk), NULL, 0);
}

static void
xmlattr_cleanup(struct xmlattr_list *list)
{
	struct xmlattr *attr, *next;

	attr = list->first;
	while (attr != NULL) {
		next = attr->next;
		free(attr->name);
		free(attr->value);
		free(attr);
		attr = next;
	}
	list->first = NULL;
	list->last = &(list->first);
}

static int
file_new(struct archive_read *a, struct xar *xar, struct xmlattr_list *list)
{
	struct xar_file *file;
	struct xmlattr *attr;

	file = calloc(1, sizeof(*file));
	if (file == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	file->parent = xar->file;
	file->mode = 0777 | AE_IFREG;
	file->atime =  0;
	file->mtime = 0;
	xar->file = file;
	xar->xattr = NULL;
	for (attr = list->first; attr != NULL; attr = attr->next) {
		if (strcmp(attr->name, "id") == 0)
			file->id = atol10(attr->value, strlen(attr->value));
	}
	file->nlink = 1;
	if (heap_add_entry(a, &(xar->file_queue), file) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	return (ARCHIVE_OK);
}

static void
file_free(struct xar_file *file)
{
	struct xattr *xattr;

	archive_string_free(&(file->pathname));
	archive_string_free(&(file->symlink));
	archive_string_free(&(file->uname));
	archive_string_free(&(file->gname));
	archive_string_free(&(file->hardlink));
	xattr = file->xattr_list;
	while (xattr != NULL) {
		struct xattr *next;

		next = xattr->next;
		xattr_free(xattr);
		xattr = next;
	}

	free(file);
}

static int
xattr_new(struct archive_read *a, struct xar *xar, struct xmlattr_list *list)
{
	struct xattr *xattr, **nx;
	struct xmlattr *attr;

	xattr = calloc(1, sizeof(*xattr));
	if (xattr == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	xar->xattr = xattr;
	for (attr = list->first; attr != NULL; attr = attr->next) {
		if (strcmp(attr->name, "id") == 0)
			xattr->id = atol10(attr->value, strlen(attr->value));
	}
	/* Chain to xattr list. */
	for (nx = &(xar->file->xattr_list);
	    *nx != NULL; nx = &((*nx)->next)) {
		if (xattr->id < (*nx)->id)
			break;
	}
	xattr->next = *nx;
	*nx = xattr;

	return (ARCHIVE_OK);
}

static void
xattr_free(struct xattr *xattr)
{
	archive_string_free(&(xattr->name));
	free(xattr);
}

static int
getencoding(struct xmlattr_list *list)
{
	struct xmlattr *attr;
	enum enctype encoding = NONE;

	for (attr = list->first; attr != NULL; attr = attr->next) {
		if (strcmp(attr->name, "style") == 0) {
			if (strcmp(attr->value, "application/octet-stream") == 0)
				encoding = NONE;
			else if (strcmp(attr->value, "application/x-gzip") == 0)
				encoding = GZIP;
			else if (strcmp(attr->value, "application/x-bzip2") == 0)
				encoding = BZIP2;
			else if (strcmp(attr->value, "application/x-lzma") == 0)
				encoding = LZMA;
			else if (strcmp(attr->value, "application/x-xz") == 0)
				encoding = XZ;
		}
	}
	return (encoding);
}

static int
getsumalgorithm(struct xmlattr_list *list)
{
	struct xmlattr *attr;
	int alg = CKSUM_NONE;

	for (attr = list->first; attr != NULL; attr = attr->next) {
		if (strcmp(attr->name, "style") == 0) {
			const char *v = attr->value;
			if ((v[0] == 'S' || v[0] == 's') &&
			    (v[1] == 'H' || v[1] == 'h') &&
			    (v[2] == 'A' || v[2] == 'a') &&
			    v[3] == '1' && v[4] == '\0')
				alg = CKSUM_SHA1;
			if ((v[0] == 'M' || v[0] == 'm') &&
			    (v[1] == 'D' || v[1] == 'd') &&
			    v[2] == '5' && v[3] == '\0')
				alg = CKSUM_MD5;
		}
	}
	return (alg);
}

static int
unknowntag_start(struct archive_read *a, struct xar *xar, const char *name)
{
	struct unknown_tag *tag;

	tag = malloc(sizeof(*tag));
	if (tag == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	tag->next = xar->unknowntags;
	archive_string_init(&(tag->name));
	archive_strcpy(&(tag->name), name);
	if (xar->unknowntags == NULL) {
#if DEBUG
		fprintf(stderr, "UNKNOWNTAG_START:%s\n", name);
#endif
		xar->xmlsts_unknown = xar->xmlsts;
		xar->xmlsts = UNKNOWN;
	}
	xar->unknowntags = tag;
	return (ARCHIVE_OK);
}

static void
unknowntag_end(struct xar *xar, const char *name)
{
	struct unknown_tag *tag;

	tag = xar->unknowntags;
	if (tag == NULL || name == NULL)
		return;
	if (strcmp(tag->name.s, name) == 0) {
		xar->unknowntags = tag->next;
		archive_string_free(&(tag->name));
		free(tag);
		if (xar->unknowntags == NULL) {
#if DEBUG
			fprintf(stderr, "UNKNOWNTAG_END:%s\n", name);
#endif
			xar->xmlsts = xar->xmlsts_unknown;
		}
	}
}

static int
xml_start(struct archive_read *a, const char *name, struct xmlattr_list *list)
{
	struct xar *xar;
	struct xmlattr *attr;

	xar = (struct xar *)(a->format->data);

#if DEBUG
	fprintf(stderr, "xml_sta:[%s]\n", name);
	for (attr = list->first; attr != NULL; attr = attr->next)
		fprintf(stderr, "    attr:\"%s\"=\"%s\"\n",
		    attr->name, attr->value);
#endif
	xar->base64text = 0;
	switch (xar->xmlsts) {
	case INIT:
		if (strcmp(name, "xar") == 0)
			xar->xmlsts = XAR;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case XAR:
		if (strcmp(name, "toc") == 0)
			xar->xmlsts = TOC;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case TOC:
		if (strcmp(name, "creation-time") == 0)
			xar->xmlsts = TOC_CREATION_TIME;
		else if (strcmp(name, "checksum") == 0)
			xar->xmlsts = TOC_CHECKSUM;
		else if (strcmp(name, "file") == 0) {
			if (file_new(a, xar, list) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			xar->xmlsts = TOC_FILE;
		}
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case TOC_CHECKSUM:
		if (strcmp(name, "offset") == 0)
			xar->xmlsts = TOC_CHECKSUM_OFFSET;
		else if (strcmp(name, "size") == 0)
			xar->xmlsts = TOC_CHECKSUM_SIZE;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case TOC_FILE:
		if (strcmp(name, "file") == 0) {
			if (file_new(a, xar, list) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		}
		else if (strcmp(name, "data") == 0)
			xar->xmlsts = FILE_DATA;
		else if (strcmp(name, "ea") == 0) {
			if (xattr_new(a, xar, list) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			xar->xmlsts = FILE_EA;
		}
		else if (strcmp(name, "ctime") == 0)
			xar->xmlsts = FILE_CTIME;
		else if (strcmp(name, "mtime") == 0)
			xar->xmlsts = FILE_MTIME;
		else if (strcmp(name, "atime") == 0)
			xar->xmlsts = FILE_ATIME;
		else if (strcmp(name, "group") == 0)
			xar->xmlsts = FILE_GROUP;
		else if (strcmp(name, "gid") == 0)
			xar->xmlsts = FILE_GID;
		else if (strcmp(name, "user") == 0)
			xar->xmlsts = FILE_USER;
		else if (strcmp(name, "uid") == 0)
			xar->xmlsts = FILE_UID;
		else if (strcmp(name, "mode") == 0)
			xar->xmlsts = FILE_MODE;
		else if (strcmp(name, "device") == 0)
			xar->xmlsts = FILE_DEVICE;
		else if (strcmp(name, "deviceno") == 0)
			xar->xmlsts = FILE_DEVICENO;
		else if (strcmp(name, "inode") == 0)
			xar->xmlsts = FILE_INODE;
		else if (strcmp(name, "link") == 0)
			xar->xmlsts = FILE_LINK;
		else if (strcmp(name, "type") == 0) {
			xar->xmlsts = FILE_TYPE;
			for (attr = list->first; attr != NULL;
			    attr = attr->next) {
				if (strcmp(attr->name, "link") != 0)
					continue;
				if (strcmp(attr->value, "original") == 0) {
					xar->file->hdnext = xar->hdlink_orgs;
					xar->hdlink_orgs = xar->file;
				} else {
					xar->file->link = (unsigned)atol10(attr->value,
					    strlen(attr->value));
					if (xar->file->link > 0)
						if (add_link(a, xar, xar->file) != ARCHIVE_OK) {
							return (ARCHIVE_FATAL);
						};
				}
			}
		}
		else if (strcmp(name, "name") == 0) {
			xar->xmlsts = FILE_NAME;
			for (attr = list->first; attr != NULL;
			    attr = attr->next) {
				if (strcmp(attr->name, "enctype") == 0 &&
				    strcmp(attr->value, "base64") == 0)
					xar->base64text = 1;
			}
		}
		else if (strcmp(name, "acl") == 0)
			xar->xmlsts = FILE_ACL;
		else if (strcmp(name, "flags") == 0)
			xar->xmlsts = FILE_FLAGS;
		else if (strcmp(name, "ext2") == 0)
			xar->xmlsts = FILE_EXT2;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_DATA:
		if (strcmp(name, "length") == 0)
			xar->xmlsts = FILE_DATA_LENGTH;
		else if (strcmp(name, "offset") == 0)
			xar->xmlsts = FILE_DATA_OFFSET;
		else if (strcmp(name, "size") == 0)
			xar->xmlsts = FILE_DATA_SIZE;
		else if (strcmp(name, "encoding") == 0) {
			xar->xmlsts = FILE_DATA_ENCODING;
			xar->file->encoding = getencoding(list);
		}
		else if (strcmp(name, "archived-checksum") == 0) {
			xar->xmlsts = FILE_DATA_A_CHECKSUM;
			xar->file->a_sum.alg = getsumalgorithm(list);
		}
		else if (strcmp(name, "extracted-checksum") == 0) {
			xar->xmlsts = FILE_DATA_E_CHECKSUM;
			xar->file->e_sum.alg = getsumalgorithm(list);
		}
		else if (strcmp(name, "content") == 0)
			xar->xmlsts = FILE_DATA_CONTENT;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_DEVICE:
		if (strcmp(name, "major") == 0)
			xar->xmlsts = FILE_DEVICE_MAJOR;
		else if (strcmp(name, "minor") == 0)
			xar->xmlsts = FILE_DEVICE_MINOR;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_DATA_CONTENT:
		if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
		break;
	case FILE_EA:
		if (strcmp(name, "length") == 0)
			xar->xmlsts = FILE_EA_LENGTH;
		else if (strcmp(name, "offset") == 0)
			xar->xmlsts = FILE_EA_OFFSET;
		else if (strcmp(name, "size") == 0)
			xar->xmlsts = FILE_EA_SIZE;
		else if (strcmp(name, "encoding") == 0) {
			xar->xmlsts = FILE_EA_ENCODING;
			xar->xattr->encoding = getencoding(list);
		} else if (strcmp(name, "archived-checksum") == 0)
			xar->xmlsts = FILE_EA_A_CHECKSUM;
		else if (strcmp(name, "extracted-checksum") == 0)
			xar->xmlsts = FILE_EA_E_CHECKSUM;
		else if (strcmp(name, "name") == 0)
			xar->xmlsts = FILE_EA_NAME;
		else if (strcmp(name, "fstype") == 0)
			xar->xmlsts = FILE_EA_FSTYPE;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_ACL:
		if (strcmp(name, "appleextended") == 0)
			xar->xmlsts = FILE_ACL_APPLEEXTENDED;
		else if (strcmp(name, "default") == 0)
			xar->xmlsts = FILE_ACL_DEFAULT;
		else if (strcmp(name, "access") == 0)
			xar->xmlsts = FILE_ACL_ACCESS;
		else
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_FLAGS:
		if (!xml_parse_file_flags(xar, name))
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case FILE_EXT2:
		if (!xml_parse_file_ext2(xar, name))
			if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		break;
	case TOC_CREATION_TIME:
	case TOC_CHECKSUM_OFFSET:
	case TOC_CHECKSUM_SIZE:
	case FILE_DATA_LENGTH:
	case FILE_DATA_OFFSET:
	case FILE_DATA_SIZE:
	case FILE_DATA_ENCODING:
	case FILE_DATA_A_CHECKSUM:
	case FILE_DATA_E_CHECKSUM:
	case FILE_EA_LENGTH:
	case FILE_EA_OFFSET:
	case FILE_EA_SIZE:
	case FILE_EA_ENCODING:
	case FILE_EA_A_CHECKSUM:
	case FILE_EA_E_CHECKSUM:
	case FILE_EA_NAME:
	case FILE_EA_FSTYPE:
	case FILE_CTIME:
	case FILE_MTIME:
	case FILE_ATIME:
	case FILE_GROUP:
	case FILE_GID:
	case FILE_USER:
	case FILE_UID:
	case FILE_INODE:
	case FILE_DEVICE_MAJOR:
	case FILE_DEVICE_MINOR:
	case FILE_DEVICENO:
	case FILE_MODE:
	case FILE_TYPE:
	case FILE_LINK:
	case FILE_NAME:
	case FILE_ACL_DEFAULT:
	case FILE_ACL_ACCESS:
	case FILE_ACL_APPLEEXTENDED:
	case FILE_FLAGS_USER_NODUMP:
	case FILE_FLAGS_USER_IMMUTABLE:
	case FILE_FLAGS_USER_APPEND:
	case FILE_FLAGS_USER_OPAQUE:
	case FILE_FLAGS_USER_NOUNLINK:
	case FILE_FLAGS_SYS_ARCHIVED:
	case FILE_FLAGS_SYS_IMMUTABLE:
	case FILE_FLAGS_SYS_APPEND:
	case FILE_FLAGS_SYS_NOUNLINK:
	case FILE_FLAGS_SYS_SNAPSHOT:
	case FILE_EXT2_SecureDeletion:
	case FILE_EXT2_Undelete:
	case FILE_EXT2_Compress:
	case FILE_EXT2_Synchronous:
	case FILE_EXT2_Immutable:
	case FILE_EXT2_AppendOnly:
	case FILE_EXT2_NoDump:
	case FILE_EXT2_NoAtime:
	case FILE_EXT2_CompDirty:
	case FILE_EXT2_CompBlock:
	case FILE_EXT2_NoCompBlock:
	case FILE_EXT2_CompError:
	case FILE_EXT2_BTree:
	case FILE_EXT2_HashIndexed:
	case FILE_EXT2_iMagic:
	case FILE_EXT2_Journaled:
	case FILE_EXT2_NoTail:
	case FILE_EXT2_DirSync:
	case FILE_EXT2_TopDir:
	case FILE_EXT2_Reserved:
	case UNKNOWN:
		if (unknowntag_start(a, xar, name) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
		break;
	}
	return (ARCHIVE_OK);
}

static void
xml_end(void *userData, const char *name)
{
	struct archive_read *a;
	struct xar *xar;

	a = (struct archive_read *)userData;
	xar = (struct xar *)(a->format->data);

#if DEBUG
	fprintf(stderr, "xml_end:[%s]\n", name);
#endif
	switch (xar->xmlsts) {
	case INIT:
		break;
	case XAR:
		if (strcmp(name, "xar") == 0)
			xar->xmlsts = INIT;
		break;
	case TOC:
		if (strcmp(name, "toc") == 0)
			xar->xmlsts = XAR;
		break;
	case TOC_CREATION_TIME:
		if (strcmp(name, "creation-time") == 0)
			xar->xmlsts = TOC;
		break;
	case TOC_CHECKSUM:
		if (strcmp(name, "checksum") == 0)
			xar->xmlsts = TOC;
		break;
	case TOC_CHECKSUM_OFFSET:
		if (strcmp(name, "offset") == 0)
			xar->xmlsts = TOC_CHECKSUM;
		break;
	case TOC_CHECKSUM_SIZE:
		if (strcmp(name, "size") == 0)
			xar->xmlsts = TOC_CHECKSUM;
		break;
	case TOC_FILE:
		if (strcmp(name, "file") == 0) {
			if (xar->file->parent != NULL &&
			    ((xar->file->mode & AE_IFMT) == AE_IFDIR))
				xar->file->parent->subdirs++;
			xar->file = xar->file->parent;
			if (xar->file == NULL)
				xar->xmlsts = TOC;
		}
		break;
	case FILE_DATA:
		if (strcmp(name, "data") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_DATA_LENGTH:
		if (strcmp(name, "length") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_OFFSET:
		if (strcmp(name, "offset") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_SIZE:
		if (strcmp(name, "size") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_ENCODING:
		if (strcmp(name, "encoding") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_A_CHECKSUM:
		if (strcmp(name, "archived-checksum") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_E_CHECKSUM:
		if (strcmp(name, "extracted-checksum") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_DATA_CONTENT:
		if (strcmp(name, "content") == 0)
			xar->xmlsts = FILE_DATA;
		break;
	case FILE_EA:
		if (strcmp(name, "ea") == 0) {
			xar->xmlsts = TOC_FILE;
			xar->xattr = NULL;
		}
		break;
	case FILE_EA_LENGTH:
		if (strcmp(name, "length") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_OFFSET:
		if (strcmp(name, "offset") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_SIZE:
		if (strcmp(name, "size") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_ENCODING:
		if (strcmp(name, "encoding") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_A_CHECKSUM:
		if (strcmp(name, "archived-checksum") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_E_CHECKSUM:
		if (strcmp(name, "extracted-checksum") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_NAME:
		if (strcmp(name, "name") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_EA_FSTYPE:
		if (strcmp(name, "fstype") == 0)
			xar->xmlsts = FILE_EA;
		break;
	case FILE_CTIME:
		if (strcmp(name, "ctime") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_MTIME:
		if (strcmp(name, "mtime") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_ATIME:
		if (strcmp(name, "atime") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_GROUP:
		if (strcmp(name, "group") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_GID:
		if (strcmp(name, "gid") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_USER:
		if (strcmp(name, "user") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_UID:
		if (strcmp(name, "uid") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_MODE:
		if (strcmp(name, "mode") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_DEVICE:
		if (strcmp(name, "device") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_DEVICE_MAJOR:
		if (strcmp(name, "major") == 0)
			xar->xmlsts = FILE_DEVICE;
		break;
	case FILE_DEVICE_MINOR:
		if (strcmp(name, "minor") == 0)
			xar->xmlsts = FILE_DEVICE;
		break;
	case FILE_DEVICENO:
		if (strcmp(name, "deviceno") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_INODE:
		if (strcmp(name, "inode") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_LINK:
		if (strcmp(name, "link") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_TYPE:
		if (strcmp(name, "type") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_NAME:
		if (strcmp(name, "name") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_ACL:
		if (strcmp(name, "acl") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_ACL_DEFAULT:
		if (strcmp(name, "default") == 0)
			xar->xmlsts = FILE_ACL;
		break;
	case FILE_ACL_ACCESS:
		if (strcmp(name, "access") == 0)
			xar->xmlsts = FILE_ACL;
		break;
	case FILE_ACL_APPLEEXTENDED:
		if (strcmp(name, "appleextended") == 0)
			xar->xmlsts = FILE_ACL;
		break;
	case FILE_FLAGS:
		if (strcmp(name, "flags") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_FLAGS_USER_NODUMP:
		if (strcmp(name, "UserNoDump") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_USER_IMMUTABLE:
		if (strcmp(name, "UserImmutable") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_USER_APPEND:
		if (strcmp(name, "UserAppend") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_USER_OPAQUE:
		if (strcmp(name, "UserOpaque") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_USER_NOUNLINK:
		if (strcmp(name, "UserNoUnlink") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_SYS_ARCHIVED:
		if (strcmp(name, "SystemArchived") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_SYS_IMMUTABLE:
		if (strcmp(name, "SystemImmutable") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_SYS_APPEND:
		if (strcmp(name, "SystemAppend") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_SYS_NOUNLINK:
		if (strcmp(name, "SystemNoUnlink") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_FLAGS_SYS_SNAPSHOT:
		if (strcmp(name, "SystemSnapshot") == 0)
			xar->xmlsts = FILE_FLAGS;
		break;
	case FILE_EXT2:
		if (strcmp(name, "ext2") == 0)
			xar->xmlsts = TOC_FILE;
		break;
	case FILE_EXT2_SecureDeletion:
		if (strcmp(name, "SecureDeletion") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Undelete:
		if (strcmp(name, "Undelete") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Compress:
		if (strcmp(name, "Compress") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Synchronous:
		if (strcmp(name, "Synchronous") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Immutable:
		if (strcmp(name, "Immutable") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_AppendOnly:
		if (strcmp(name, "AppendOnly") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_NoDump:
		if (strcmp(name, "NoDump") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_NoAtime:
		if (strcmp(name, "NoAtime") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_CompDirty:
		if (strcmp(name, "CompDirty") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_CompBlock:
		if (strcmp(name, "CompBlock") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_NoCompBlock:
		if (strcmp(name, "NoCompBlock") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_CompError:
		if (strcmp(name, "CompError") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_BTree:
		if (strcmp(name, "BTree") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_HashIndexed:
		if (strcmp(name, "HashIndexed") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_iMagic:
		if (strcmp(name, "iMagic") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Journaled:
		if (strcmp(name, "Journaled") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_NoTail:
		if (strcmp(name, "NoTail") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_DirSync:
		if (strcmp(name, "DirSync") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_TopDir:
		if (strcmp(name, "TopDir") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case FILE_EXT2_Reserved:
		if (strcmp(name, "Reserved") == 0)
			xar->xmlsts = FILE_EXT2;
		break;
	case UNKNOWN:
		unknowntag_end(xar, name);
		break;
	}
}

static const int base64[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* 00 - 0F */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* 10 - 1F */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, 62, -1, -1, -1, 63, /* 20 - 2F */
	52, 53, 54, 55, 56, 57, 58, 59,
	60, 61, -1, -1, -1, -1, -1, -1, /* 30 - 3F */
	-1,  0,  1,  2,  3,  4,  5,  6,
	 7,  8,  9, 10, 11, 12, 13, 14, /* 40 - 4F */
	15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, -1, -1, -1, -1, -1, /* 50 - 5F */
	-1, 26, 27, 28, 29, 30, 31, 32,
	33, 34, 35, 36, 37, 38, 39, 40, /* 60 - 6F */
	41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, -1, -1, -1, -1, -1, /* 70 - 7F */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* 80 - 8F */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* 90 - 9F */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* A0 - AF */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* B0 - BF */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* C0 - CF */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* D0 - DF */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* E0 - EF */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, /* F0 - FF */
};

static void
strappend_base64(struct xar *xar,
    struct archive_string *as, const char *s, size_t l)
{
	unsigned char buff[256];
	unsigned char *out;
	const unsigned char *b;
	size_t len;

	(void)xar; /* UNUSED */
	len = 0;
	out = buff;
	b = (const unsigned char *)s;
	while (l > 0) {
		int n = 0;

		if (l > 0) {
			if (base64[b[0]] < 0 || base64[b[1]] < 0)
				break;
			n = base64[*b++] << 18;
			n |= base64[*b++] << 12;
			*out++ = n >> 16;
			len++;
			l -= 2;
		}
		if (l > 0) {
			if (base64[*b] < 0)
				break;
			n |= base64[*b++] << 6;
			*out++ = (n >> 8) & 0xFF;
			len++;
			--l;
		}
		if (l > 0) {
			if (base64[*b] < 0)
				break;
			n |= base64[*b++];
			*out++ = n & 0xFF;
			len++;
			--l;
		}
		if (len+3 >= sizeof(buff)) {
			archive_strncat(as, (const char *)buff, len);
			len = 0;
			out = buff;
		}
	}
	if (len > 0)
		archive_strncat(as, (const char *)buff, len);
}

static int
is_string(const char *known, const char *data, size_t len)
{
	if (strlen(known) != len)
		return -1;
	return memcmp(data, known, len);
}

static void
xml_data(void *userData, const char *s, int len)
{
	struct archive_read *a;
	struct xar *xar;

	a = (struct archive_read *)userData;
	xar = (struct xar *)(a->format->data);

#if DEBUG
	{
		char buff[1024];
		if (len > (int)(sizeof(buff)-1))
			len = (int)(sizeof(buff)-1);
		strncpy(buff, s, len);
		buff[len] = 0;
		fprintf(stderr, "\tlen=%d:\"%s\"\n", len, buff);
	}
#endif
	switch (xar->xmlsts) {
	case TOC_CHECKSUM_OFFSET:
		xar->toc_chksum_offset = atol10(s, len);
		break;
	case TOC_CHECKSUM_SIZE:
		xar->toc_chksum_size = atol10(s, len);
		break;
	default:
		break;
	}
	if (xar->file == NULL)
		return;

	switch (xar->xmlsts) {
	case FILE_NAME:
		if (xar->file->parent != NULL) {
			archive_string_concat(&(xar->file->pathname),
			    &(xar->file->parent->pathname));
			archive_strappend_char(&(xar->file->pathname), '/');
		}
		xar->file->has |= HAS_PATHNAME;
		if (xar->base64text) {
			strappend_base64(xar,
			    &(xar->file->pathname), s, len);
		} else
			archive_strncat(&(xar->file->pathname), s, len);
		break;
	case FILE_LINK:
		xar->file->has |= HAS_SYMLINK;
		archive_strncpy(&(xar->file->symlink), s, len);
		break;
	case FILE_TYPE:
		if (is_string("file", s, len) == 0 ||
		    is_string("hardlink", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFREG;
		if (is_string("directory", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFDIR;
		if (is_string("symlink", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFLNK;
		if (is_string("character special", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFCHR;
		if (is_string("block special", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFBLK;
		if (is_string("socket", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFSOCK;
		if (is_string("fifo", s, len) == 0)
			xar->file->mode =
			    (xar->file->mode & ~AE_IFMT) | AE_IFIFO;
		xar->file->has |= HAS_TYPE;
		break;
	case FILE_INODE:
		xar->file->has |= HAS_INO;
		xar->file->ino64 = atol10(s, len);
		break;
	case FILE_DEVICE_MAJOR:
		xar->file->has |= HAS_DEVMAJOR;
		xar->file->devmajor = (dev_t)atol10(s, len);
		break;
	case FILE_DEVICE_MINOR:
		xar->file->has |= HAS_DEVMINOR;
		xar->file->devminor = (dev_t)atol10(s, len);
		break;
	case FILE_DEVICENO:
		xar->file->has |= HAS_DEV;
		xar->file->dev = (dev_t)atol10(s, len);
		break;
	case FILE_MODE:
		xar->file->has |= HAS_MODE;
		xar->file->mode =
		    (xar->file->mode & AE_IFMT) |
		    ((mode_t)(atol8(s, len)) & ~AE_IFMT);
		break;
	case FILE_GROUP:
		xar->file->has |= HAS_GID;
		archive_strncpy(&(xar->file->gname), s, len);
		break;
	case FILE_GID:
		xar->file->has |= HAS_GID;
		xar->file->gid = atol10(s, len);
		break;
	case FILE_USER:
		xar->file->has |= HAS_UID;
		archive_strncpy(&(xar->file->uname), s, len);
		break;
	case FILE_UID:
		xar->file->has |= HAS_UID;
		xar->file->uid = atol10(s, len);
		break;
	case FILE_CTIME:
		xar->file->has |= HAS_TIME | HAS_CTIME;
		xar->file->ctime = parse_time(s, len);
		break;
	case FILE_MTIME:
		xar->file->has |= HAS_TIME | HAS_MTIME;
		xar->file->mtime = parse_time(s, len);
		break;
	case FILE_ATIME:
		xar->file->has |= HAS_TIME | HAS_ATIME;
		xar->file->atime = parse_time(s, len);
		break;
	case FILE_DATA_LENGTH:
		xar->file->has |= HAS_DATA;
		xar->file->length = atol10(s, len);
		break;
	case FILE_DATA_OFFSET:
		xar->file->has |= HAS_DATA;
		xar->file->offset = atol10(s, len);
		break;
	case FILE_DATA_SIZE:
		xar->file->has |= HAS_DATA;
		xar->file->size = atol10(s, len);
		break;
	case FILE_DATA_A_CHECKSUM:
		xar->file->a_sum.len = atohex(xar->file->a_sum.val,
		    sizeof(xar->file->a_sum.val), s, len);
		break;
	case FILE_DATA_E_CHECKSUM:
		xar->file->e_sum.len = atohex(xar->file->e_sum.val,
		    sizeof(xar->file->e_sum.val), s, len);
		break;
	case FILE_EA_LENGTH:
		xar->file->has |= HAS_XATTR;
		xar->xattr->length = atol10(s, len);
		break;
	case FILE_EA_OFFSET:
		xar->file->has |= HAS_XATTR;
		xar->xattr->offset = atol10(s, len);
		break;
	case FILE_EA_SIZE:
		xar->file->has |= HAS_XATTR;
		xar->xattr->size = atol10(s, len);
		break;
	case FILE_EA_A_CHECKSUM:
		xar->file->has |= HAS_XATTR;
		xar->xattr->a_sum.len = atohex(xar->xattr->a_sum.val,
		    sizeof(xar->xattr->a_sum.val), s, len);
		break;
	case FILE_EA_E_CHECKSUM:
		xar->file->has |= HAS_XATTR;
		xar->xattr->e_sum.len = atohex(xar->xattr->e_sum.val,
		    sizeof(xar->xattr->e_sum.val), s, len);
		break;
	case FILE_EA_NAME:
		xar->file->has |= HAS_XATTR;
		archive_strncpy(&(xar->xattr->name), s, len);
		break;
	case FILE_EA_FSTYPE:
		xar->file->has |= HAS_XATTR;
		archive_strncpy(&(xar->xattr->fstype), s, len);
		break;
		break;
	case FILE_ACL_DEFAULT:
	case FILE_ACL_ACCESS:
	case FILE_ACL_APPLEEXTENDED:
		xar->file->has |= HAS_ACL;
		/* TODO */
		break;
	case INIT:
	case XAR:
	case TOC:
	case TOC_CREATION_TIME:
	case TOC_CHECKSUM:
	case TOC_CHECKSUM_OFFSET:
	case TOC_CHECKSUM_SIZE:
	case TOC_FILE:
	case FILE_DATA:
	case FILE_DATA_ENCODING:
	case FILE_DATA_CONTENT:
	case FILE_DEVICE:
	case FILE_EA:
	case FILE_EA_ENCODING:
	case FILE_ACL:
	case FILE_FLAGS:
	case FILE_FLAGS_USER_NODUMP:
	case FILE_FLAGS_USER_IMMUTABLE:
	case FILE_FLAGS_USER_APPEND:
	case FILE_FLAGS_USER_OPAQUE:
	case FILE_FLAGS_USER_NOUNLINK:
	case FILE_FLAGS_SYS_ARCHIVED:
	case FILE_FLAGS_SYS_IMMUTABLE:
	case FILE_FLAGS_SYS_APPEND:
	case FILE_FLAGS_SYS_NOUNLINK:
	case FILE_FLAGS_SYS_SNAPSHOT:
	case FILE_EXT2:
	case FILE_EXT2_SecureDeletion:
	case FILE_EXT2_Undelete:
	case FILE_EXT2_Compress:
	case FILE_EXT2_Synchronous:
	case FILE_EXT2_Immutable:
	case FILE_EXT2_AppendOnly:
	case FILE_EXT2_NoDump:
	case FILE_EXT2_NoAtime:
	case FILE_EXT2_CompDirty:
	case FILE_EXT2_CompBlock:
	case FILE_EXT2_NoCompBlock:
	case FILE_EXT2_CompError:
	case FILE_EXT2_BTree:
	case FILE_EXT2_HashIndexed:
	case FILE_EXT2_iMagic:
	case FILE_EXT2_Journaled:
	case FILE_EXT2_NoTail:
	case FILE_EXT2_DirSync:
	case FILE_EXT2_TopDir:
	case FILE_EXT2_Reserved:
	case UNKNOWN:
		break;
	}
}

/*
 * BSD file flags.
 */
static int
xml_parse_file_flags(struct xar *xar, const char *name)
{
	const char *flag = NULL;

	if (strcmp(name, "UserNoDump") == 0) {
		xar->xmlsts = FILE_FLAGS_USER_NODUMP;
		flag = "nodump";
	}
	else if (strcmp(name, "UserImmutable") == 0) {
		xar->xmlsts = FILE_FLAGS_USER_IMMUTABLE;
		flag = "uimmutable";
	}
	else if (strcmp(name, "UserAppend") == 0) {
		xar->xmlsts = FILE_FLAGS_USER_APPEND;
		flag = "uappend";
	}
	else if (strcmp(name, "UserOpaque") == 0) {
		xar->xmlsts = FILE_FLAGS_USER_OPAQUE;
		flag = "opaque";
	}
	else if (strcmp(name, "UserNoUnlink") == 0) {
		xar->xmlsts = FILE_FLAGS_USER_NOUNLINK;
		flag = "nouunlink";
	}
	else if (strcmp(name, "SystemArchived") == 0) {
		xar->xmlsts = FILE_FLAGS_SYS_ARCHIVED;
		flag = "archived";
	}
	else if (strcmp(name, "SystemImmutable") == 0) {
		xar->xmlsts = FILE_FLAGS_SYS_IMMUTABLE;
		flag = "simmutable";
	}
	else if (strcmp(name, "SystemAppend") == 0) {
		xar->xmlsts = FILE_FLAGS_SYS_APPEND;
		flag = "sappend";
	}
	else if (strcmp(name, "SystemNoUnlink") == 0) {
		xar->xmlsts = FILE_FLAGS_SYS_NOUNLINK;
		flag = "nosunlink";
	}
	else if (strcmp(name, "SystemSnapshot") == 0) {
		xar->xmlsts = FILE_FLAGS_SYS_SNAPSHOT;
		flag = "snapshot";
	}

	if (flag == NULL)
		return (0);
	xar->file->has |= HAS_FFLAGS;
	if (archive_strlen(&(xar->file->fflags_text)) > 0)
		archive_strappend_char(&(xar->file->fflags_text), ',');
	archive_strcat(&(xar->file->fflags_text), flag);
	return (1);
}

/*
 * Linux file flags.
 */
static int
xml_parse_file_ext2(struct xar *xar, const char *name)
{
	const char *flag = NULL;

	if (strcmp(name, "SecureDeletion") == 0) {
		xar->xmlsts = FILE_EXT2_SecureDeletion;
		flag = "securedeletion";
	}
	else if (strcmp(name, "Undelete") == 0) {
		xar->xmlsts = FILE_EXT2_Undelete;
		flag = "nouunlink";
	}
	else if (strcmp(name, "Compress") == 0) {
		xar->xmlsts = FILE_EXT2_Compress;
		flag = "compress";
	}
	else if (strcmp(name, "Synchronous") == 0) {
		xar->xmlsts = FILE_EXT2_Synchronous;
		flag = "sync";
	}
	else if (strcmp(name, "Immutable") == 0) {
		xar->xmlsts = FILE_EXT2_Immutable;
		flag = "simmutable";
	}
	else if (strcmp(name, "AppendOnly") == 0) {
		xar->xmlsts = FILE_EXT2_AppendOnly;
		flag = "sappend";
	}
	else if (strcmp(name, "NoDump") == 0) {
		xar->xmlsts = FILE_EXT2_NoDump;
		flag = "nodump";
	}
	else if (strcmp(name, "NoAtime") == 0) {
		xar->xmlsts = FILE_EXT2_NoAtime;
		flag = "noatime";
	}
	else if (strcmp(name, "CompDirty") == 0) {
		xar->xmlsts = FILE_EXT2_CompDirty;
		flag = "compdirty";
	}
	else if (strcmp(name, "CompBlock") == 0) {
		xar->xmlsts = FILE_EXT2_CompBlock;
		flag = "comprblk";
	}
	else if (strcmp(name, "NoCompBlock") == 0) {
		xar->xmlsts = FILE_EXT2_NoCompBlock;
		flag = "nocomprblk";
	}
	else if (strcmp(name, "CompError") == 0) {
		xar->xmlsts = FILE_EXT2_CompError;
		flag = "comperr";
	}
	else if (strcmp(name, "BTree") == 0) {
		xar->xmlsts = FILE_EXT2_BTree;
		flag = "btree";
	}
	else if (strcmp(name, "HashIndexed") == 0) {
		xar->xmlsts = FILE_EXT2_HashIndexed;
		flag = "hashidx";
	}
	else if (strcmp(name, "iMagic") == 0) {
		xar->xmlsts = FILE_EXT2_iMagic;
		flag = "imagic";
	}
	else if (strcmp(name, "Journaled") == 0) {
		xar->xmlsts = FILE_EXT2_Journaled;
		flag = "journal";
	}
	else if (strcmp(name, "NoTail") == 0) {
		xar->xmlsts = FILE_EXT2_NoTail;
		flag = "notail";
	}
	else if (strcmp(name, "DirSync") == 0) {
		xar->xmlsts = FILE_EXT2_DirSync;
		flag = "dirsync";
	}
	else if (strcmp(name, "TopDir") == 0) {
		xar->xmlsts = FILE_EXT2_TopDir;
		flag = "topdir";
	}
	else if (strcmp(name, "Reserved") == 0) {
		xar->xmlsts = FILE_EXT2_Reserved;
		flag = "reserved";
	}

	if (flag == NULL)
		return (0);
	if (archive_strlen(&(xar->file->fflags_text)) > 0)
		archive_strappend_char(&(xar->file->fflags_text), ',');
	archive_strcat(&(xar->file->fflags_text), flag);
	return (1);
}

#ifdef HAVE_LIBXML_XMLREADER_H

static int
xml2_xmlattr_setup(struct archive_read *a,
    struct xmlattr_list *list, xmlTextReaderPtr reader)
{
	struct xmlattr *attr;
	int r;

	list->first = NULL;
	list->last = &(list->first);
	r = xmlTextReaderMoveToFirstAttribute(reader);
	while (r == 1) {
		attr = malloc(sizeof*(attr));
		if (attr == NULL) {
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		attr->name = strdup(
		    (const char *)xmlTextReaderConstLocalName(reader));
		if (attr->name == NULL) {
			free(attr);
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		attr->value = strdup(
		    (const char *)xmlTextReaderConstValue(reader));
		if (attr->value == NULL) {
			free(attr->name);
			free(attr);
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		attr->next = NULL;
		*list->last = attr;
		list->last = &(attr->next);
		r = xmlTextReaderMoveToNextAttribute(reader);
	}
	return (r);
}

static int
xml2_read_cb(void *context, char *buffer, int len)
{
	struct archive_read *a;
	struct xar *xar;
	const void *d;
	size_t outbytes;
	size_t used = 0;
	int r;

	a = (struct archive_read *)context;
	xar = (struct xar *)(a->format->data);

	if (xar->toc_remaining <= 0)
		return (0);
	d = buffer;
	outbytes = len;
	r = rd_contents(a, &d, &outbytes, &used, xar->toc_remaining);
	if (r != ARCHIVE_OK)
		return (r);
	__archive_read_consume(a, used);
	xar->toc_remaining -= used;
	xar->offset += used;
	xar->toc_total += outbytes;
	PRINT_TOC(buffer, len);

	return ((int)outbytes);
}

static int
xml2_close_cb(void *context)
{

	(void)context; /* UNUSED */
	return (0);
}

static void
xml2_error_hdr(void *arg, const char *msg, xmlParserSeverities severity,
    xmlTextReaderLocatorPtr locator)
{
	struct archive_read *a;

	(void)locator; /* UNUSED */
	a = (struct archive_read *)arg;
	switch (severity) {
	case XML_PARSER_SEVERITY_VALIDITY_WARNING:
	case XML_PARSER_SEVERITY_WARNING:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "XML Parsing error: %s", msg);
		break;
	case XML_PARSER_SEVERITY_VALIDITY_ERROR:
	case XML_PARSER_SEVERITY_ERROR:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "XML Parsing error: %s", msg);
		break;
	}
}

static int
xml2_read_toc(struct archive_read *a)
{
	xmlTextReaderPtr reader;
	struct xmlattr_list list;
	int r;

	reader = xmlReaderForIO(xml2_read_cb, xml2_close_cb, a, NULL, NULL, 0);
	if (reader == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Couldn't allocate memory for xml parser");
		return (ARCHIVE_FATAL);
	}
	xmlTextReaderSetErrorHandler(reader, xml2_error_hdr, a);

	while ((r = xmlTextReaderRead(reader)) == 1) {
		const char *name, *value;
		int type, empty;

		type = xmlTextReaderNodeType(reader);
		name = (const char *)xmlTextReaderConstLocalName(reader);
		switch (type) {
		case XML_READER_TYPE_ELEMENT:
			empty = xmlTextReaderIsEmptyElement(reader);
			r = xml2_xmlattr_setup(a, &list, reader);
			if (r == ARCHIVE_OK)
				r = xml_start(a, name, &list);
			xmlattr_cleanup(&list);
			if (r != ARCHIVE_OK)
				return (r);
			if (empty)
				xml_end(a, name);
			break;
		case XML_READER_TYPE_END_ELEMENT:
			xml_end(a, name);
			break;
		case XML_READER_TYPE_TEXT:
			value = (const char *)xmlTextReaderConstValue(reader);
			xml_data(a, value, strlen(value));
			break;
		case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:
		default:
			break;
		}
		if (r < 0)
			break;
	}
	xmlFreeTextReader(reader);
	xmlCleanupParser();

	return ((r == 0)?ARCHIVE_OK:ARCHIVE_FATAL);
}

#elif defined(HAVE_BSDXML_H) || defined(HAVE_EXPAT_H)

static int
expat_xmlattr_setup(struct archive_read *a,
    struct xmlattr_list *list, const XML_Char **atts)
{
	struct xmlattr *attr;
	char *name, *value;

	list->first = NULL;
	list->last = &(list->first);
	if (atts == NULL)
		return (ARCHIVE_OK);
	while (atts[0] != NULL && atts[1] != NULL) {
		attr = malloc(sizeof*(attr));
		name = strdup(atts[0]);
		value = strdup(atts[1]);
		if (attr == NULL || name == NULL || value == NULL) {
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			free(attr);
			free(name);
			free(value);
			return (ARCHIVE_FATAL);
		}
		attr->name = name;
		attr->value = value;
		attr->next = NULL;
		*list->last = attr;
		list->last = &(attr->next);
		atts += 2;
	}
	return (ARCHIVE_OK);
}

static void
expat_start_cb(void *userData, const XML_Char *name, const XML_Char **atts)
{
	struct expat_userData *ud = (struct expat_userData *)userData;
	struct archive_read *a = ud->archive;
	struct xmlattr_list list;
	int r;

	r = expat_xmlattr_setup(a, &list, atts);
	if (r == ARCHIVE_OK)
		r = xml_start(a, (const char *)name, &list);
	xmlattr_cleanup(&list);
	ud->state = r;
}

static void
expat_end_cb(void *userData, const XML_Char *name)
{
	struct expat_userData *ud = (struct expat_userData *)userData;

	xml_end(ud->archive, (const char *)name);
}

static void
expat_data_cb(void *userData, const XML_Char *s, int len)
{
	struct expat_userData *ud = (struct expat_userData *)userData;

	xml_data(ud->archive, s, len);
}

static int
expat_read_toc(struct archive_read *a)
{
	struct xar *xar;
	XML_Parser parser;
	struct expat_userData ud;

	ud.state = ARCHIVE_OK;
	ud.archive = a;

	xar = (struct xar *)(a->format->data);

	/* Initialize XML Parser library. */
	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Couldn't allocate memory for xml parser");
		return (ARCHIVE_FATAL);
	}
	XML_SetUserData(parser, &ud);
	XML_SetElementHandler(parser, expat_start_cb, expat_end_cb);
	XML_SetCharacterDataHandler(parser, expat_data_cb);
	xar->xmlsts = INIT;

	while (xar->toc_remaining && ud.state == ARCHIVE_OK) {
		enum XML_Status xr;
		const void *d;
		size_t outbytes;
		size_t used;
		int r;

		d = NULL;
		r = rd_contents(a, &d, &outbytes, &used, xar->toc_remaining);
		if (r != ARCHIVE_OK)
			return (r);
		xar->toc_remaining -= used;
		xar->offset += used;
		xar->toc_total += outbytes;
		PRINT_TOC(d, outbytes);

		xr = XML_Parse(parser, d, outbytes, xar->toc_remaining == 0);
		__archive_read_consume(a, used);
		if (xr == XML_STATUS_ERROR) {
			XML_ParserFree(parser);
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "XML Parsing failed");
			return (ARCHIVE_FATAL);
		}
	}
	XML_ParserFree(parser);
	return (ud.state);
}
#endif /* defined(HAVE_BSDXML_H) || defined(HAVE_EXPAT_H) */

#endif /* Support xar format */
