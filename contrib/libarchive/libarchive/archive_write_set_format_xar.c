/*-
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
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <stdlib.h>
#if HAVE_LIBXML_XMLWRITER_H
#include <libxml/xmlwriter.h>
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
#include "archive_rb.h"
#include "archive_string.h"
#include "archive_write_private.h"

/*
 * Differences to xar utility.
 * - Subdocument is not supported yet.
 * - ACL is not supported yet.
 * - When writing an XML element <link type="<file-type>">, <file-type>
 *   which is a file type a symbolic link is referencing is always marked
 *   as "broken". Xar utility uses stat(2) to get the file type, but, in
 *   libarchive format writer, we should not use it; if it is needed, we
 *   should get about it at archive_read_disk.c.
 * - It is possible to appear both <flags> and <ext2> elements.
 *   Xar utility generates <flags> on BSD platform and <ext2> on Linux
 *   platform.
 *
 */

#if !(defined(HAVE_LIBXML_XMLWRITER_H) && defined(LIBXML_VERSION) &&\
	LIBXML_VERSION >= 20703) ||\
	!defined(HAVE_ZLIB_H) || \
	!defined(ARCHIVE_HAS_MD5) || !defined(ARCHIVE_HAS_SHA1)
/*
 * xar needs several external libraries.
 *   o libxml2
 *   o openssl or MD5/SHA1 hash function
 *   o zlib
 *   o bzlib2 (option)
 *   o liblzma (option)
 */
int
archive_write_set_format_xar(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;

	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "Xar not supported on this platform");
	return (ARCHIVE_WARN);
}

#else	/* Support xar format */

/*#define DEBUG_PRINT_TOC		1 */

#define BAD_CAST_CONST (const xmlChar *)

#define HEADER_MAGIC	0x78617221
#define HEADER_SIZE	28
#define HEADER_VERSION	1

enum sumalg {
	CKSUM_NONE = 0,
	CKSUM_SHA1 = 1,
	CKSUM_MD5 = 2
};

#define MD5_SIZE	16
#define SHA1_SIZE	20
#define MAX_SUM_SIZE	20
#define MD5_NAME	"md5"
#define SHA1_NAME	"sha1"

enum enctype {
	NONE,
	GZIP,
	BZIP2,
	LZMA,
	XZ,
};

struct chksumwork {
	enum sumalg		 alg;
#ifdef ARCHIVE_HAS_MD5
	archive_md5_ctx		 md5ctx;
#endif
#ifdef ARCHIVE_HAS_SHA1
	archive_sha1_ctx	 sha1ctx;
#endif
};

enum la_zaction {
	ARCHIVE_Z_FINISH,
	ARCHIVE_Z_RUN
};

/*
 * Universal zstream.
 */
struct la_zstream {
	const unsigned char	*next_in;
	size_t			 avail_in;
	uint64_t		 total_in;

	unsigned char		*next_out;
	size_t			 avail_out;
	uint64_t		 total_out;

	int			 valid;
	void			*real_stream;
	int			 (*code) (struct archive *a,
				    struct la_zstream *lastrm,
				    enum la_zaction action);
	int			 (*end)(struct archive *a,
				    struct la_zstream *lastrm);
};

struct chksumval {
	enum sumalg		 alg;
	size_t			 len;
	unsigned char		 val[MAX_SUM_SIZE];
};

struct heap_data {
	int			 id;
	struct heap_data	*next;
	uint64_t		 temp_offset;
	uint64_t		 length;	/* archived size.	*/
	uint64_t		 size;		/* extracted size.	*/
	enum enctype		 compression;
	struct chksumval	 a_sum;		/* archived checksum.	*/
	struct chksumval	 e_sum;		/* extracted checksum.	*/
};

struct file {
	struct archive_rb_node	 rbnode;

	int			 id;
	struct archive_entry	*entry;

	struct archive_rb_tree	 rbtree;
	struct file		*next;
	struct file		*chnext;
	struct file		*hlnext;
	/* For hardlinked files.
	 * Use only when archive_entry_nlink() > 1 */
	struct file		*hardlink_target;
	struct file		*parent;	/* parent directory entry */
	/*
	 * To manage sub directory files.
	 * We use 'chnext' (a member of struct file) to chain.
	 */
	struct {
		struct file	*first;
		struct file	**last;
	}			 children;

	/* For making a directory tree. */
        struct archive_string    parentdir;
        struct archive_string    basename;
        struct archive_string    symlink;

	int			 ea_idx;
	struct {
		struct heap_data *first;
		struct heap_data **last;
	}			 xattr;
	struct heap_data	 data;
        struct archive_string    script;

	int			 virtual:1;
	int			 dir:1;
};

struct hardlink {
	struct archive_rb_node	 rbnode;
	int			 nlink;
	struct {
		struct file	*first;
		struct file	**last;
	}			 file_list;
};

struct xar {
	int			 temp_fd;
	uint64_t		 temp_offset;

	int			 file_idx;
	struct file		*root;
	struct file		*cur_dirent;
	struct archive_string	 cur_dirstr;
	struct file		*cur_file;
	uint64_t		 bytes_remaining;
	struct archive_string	 tstr;
	struct archive_string	 vstr;

	enum sumalg		 opt_toc_sumalg;
	enum sumalg		 opt_sumalg;
	enum enctype		 opt_compression;
	int			 opt_compression_level;
	uint32_t		 opt_threads;

	struct chksumwork	 a_sumwrk;	/* archived checksum.	*/
	struct chksumwork	 e_sumwrk;	/* extracted checksum.	*/
	struct la_zstream	 stream;
	struct archive_string_conv *sconv;
	/*
	 * Compressed data buffer.
	 */
	unsigned char		 wbuff[1024 * 64];
	size_t			 wbuff_remaining;

	struct heap_data	 toc;
	/*
	 * The list of all file entries is used to manage struct file
	 * objects.
	 * We use 'next' (a member of struct file) to chain.
	 */
	struct {
		struct file	*first;
		struct file	**last;
	}			 file_list;
	/*
	 * The list of hard-linked file entries.
	 * We use 'hlnext' (a member of struct file) to chain.
	 */
	struct archive_rb_tree	 hardlink_rbtree;
};

static int	xar_options(struct archive_write *,
		    const char *, const char *);
static int	xar_write_header(struct archive_write *,
		    struct archive_entry *);
static ssize_t	xar_write_data(struct archive_write *,
		    const void *, size_t);
static int	xar_finish_entry(struct archive_write *);
static int	xar_close(struct archive_write *);
static int	xar_free(struct archive_write *);

static struct file *file_new(struct archive_write *a, struct archive_entry *);
static void	file_free(struct file *);
static struct file *file_create_virtual_dir(struct archive_write *a, struct xar *,
		    const char *);
static int	file_add_child_tail(struct file *, struct file *);
static struct file *file_find_child(struct file *, const char *);
static int	file_gen_utility_names(struct archive_write *,
		    struct file *);
static int	get_path_component(char *, int, const char *);
static int	file_tree(struct archive_write *, struct file **);
static void	file_register(struct xar *, struct file *);
static void	file_init_register(struct xar *);
static void	file_free_register(struct xar *);
static int	file_register_hardlink(struct archive_write *,
		    struct file *);
static void	file_connect_hardlink_files(struct xar *);
static void	file_init_hardlinks(struct xar *);
static void	file_free_hardlinks(struct xar *);

static void	checksum_init(struct chksumwork *, enum sumalg);
static void	checksum_update(struct chksumwork *, const void *, size_t);
static void	checksum_final(struct chksumwork *, struct chksumval *);
static int	compression_init_encoder_gzip(struct archive *,
		    struct la_zstream *, int, int);
static int	compression_code_gzip(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_gzip(struct archive *, struct la_zstream *);
static int	compression_init_encoder_bzip2(struct archive *,
		    struct la_zstream *, int);
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
static int	compression_code_bzip2(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_bzip2(struct archive *, struct la_zstream *);
#endif
static int	compression_init_encoder_lzma(struct archive *,
		    struct la_zstream *, int);
static int	compression_init_encoder_xz(struct archive *,
		    struct la_zstream *, int, int);
#if defined(HAVE_LZMA_H)
static int	compression_code_lzma(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_lzma(struct archive *, struct la_zstream *);
#endif
static int	xar_compression_init_encoder(struct archive_write *);
static int	compression_code(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end(struct archive *,
		    struct la_zstream *);
static int	save_xattrs(struct archive_write *, struct file *);
static int	getalgsize(enum sumalg);
static const char *getalgname(enum sumalg);

int
archive_write_set_format_xar(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct xar *xar;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_set_format_xar");

	/* If another format was already registered, unregister it. */
	if (a->format_free != NULL)
		(a->format_free)(a);

	xar = calloc(1, sizeof(*xar));
	if (xar == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate xar data");
		return (ARCHIVE_FATAL);
	}
	xar->temp_fd = -1;
	file_init_register(xar);
	file_init_hardlinks(xar);
	archive_string_init(&(xar->tstr));
	archive_string_init(&(xar->vstr));

	/*
	 * Create the root directory.
	 */
	xar->root = file_create_virtual_dir(a, xar, "");
	if (xar->root == NULL) {
		free(xar);
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate xar data");
		return (ARCHIVE_FATAL);
	}
	xar->root->parent = xar->root;
	file_register(xar, xar->root);
	xar->cur_dirent = xar->root;
	archive_string_init(&(xar->cur_dirstr));
	archive_string_ensure(&(xar->cur_dirstr), 1);
	xar->cur_dirstr.s[0] = 0;

	/*
	 * Initialize option.
	 */
	/* Set default checksum type. */
	xar->opt_toc_sumalg = CKSUM_SHA1;
	xar->opt_sumalg = CKSUM_SHA1;
	/* Set default compression type, level, and number of threads. */
	xar->opt_compression = GZIP;
	xar->opt_compression_level = 6;
	xar->opt_threads = 1;

	a->format_data = xar;

	a->format_name = "xar";
	a->format_options = xar_options;
	a->format_write_header = xar_write_header;
	a->format_write_data = xar_write_data;
	a->format_finish_entry = xar_finish_entry;
	a->format_close = xar_close;
	a->format_free = xar_free;
	a->archive.archive_format = ARCHIVE_FORMAT_XAR;
	a->archive.archive_format_name = "xar";

	return (ARCHIVE_OK);
}

static int
xar_options(struct archive_write *a, const char *key, const char *value)
{
	struct xar *xar;

	xar = (struct xar *)a->format_data;

	if (strcmp(key, "checksum") == 0) {
		if (value == NULL)
			xar->opt_sumalg = CKSUM_NONE;
		else if (strcmp(value, "sha1") == 0)
			xar->opt_sumalg = CKSUM_SHA1;
		else if (strcmp(value, "md5") == 0)
			xar->opt_sumalg = CKSUM_MD5;
		else {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Unknown checksum name: `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "compression") == 0) {
		const char *name = NULL;

		if (value == NULL)
			xar->opt_compression = NONE;
		else if (strcmp(value, "gzip") == 0)
			xar->opt_compression = GZIP;
		else if (strcmp(value, "bzip2") == 0)
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
			xar->opt_compression = BZIP2;
#else
			name = "bzip2";
#endif
		else if (strcmp(value, "lzma") == 0)
#if HAVE_LZMA_H
			xar->opt_compression = LZMA;
#else
			name = "lzma";
#endif
		else if (strcmp(value, "xz") == 0)
#if HAVE_LZMA_H
			xar->opt_compression = XZ;
#else
			name = "xz";
#endif
		else {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Unknown compression name: `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		if (name != NULL) {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "`%s' compression not supported "
			    "on this platform",
			    name);
			return (ARCHIVE_FAILED);
		}
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "compression-level") == 0) {
		if (value == NULL ||
		    !(value[0] >= '0' && value[0] <= '9') ||
		    value[1] != '\0') {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Illegal value `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		xar->opt_compression_level = value[0] - '0';
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "toc-checksum") == 0) {
		if (value == NULL)
			xar->opt_toc_sumalg = CKSUM_NONE;
		else if (strcmp(value, "sha1") == 0)
			xar->opt_toc_sumalg = CKSUM_SHA1;
		else if (strcmp(value, "md5") == 0)
			xar->opt_toc_sumalg = CKSUM_MD5;
		else {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Unknown checksum name: `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "threads") == 0) {
		if (value == NULL)
			return (ARCHIVE_FAILED);
		xar->opt_threads = (int)strtoul(value, NULL, 10);
		if (xar->opt_threads == 0 && errno != 0) {
			xar->opt_threads = 1;
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Illegal value `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		if (xar->opt_threads == 0) {
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
			xar->opt_threads = lzma_cputhreads();
#else
			xar->opt_threads = 1;
#endif
		}
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
xar_write_header(struct archive_write *a, struct archive_entry *entry)
{
	struct xar *xar;
	struct file *file;
	struct archive_entry *file_entry;
	int r, r2;

	xar = (struct xar *)a->format_data;
	xar->cur_file = NULL;
	xar->bytes_remaining = 0;

	if (xar->sconv == NULL) {
		xar->sconv = archive_string_conversion_to_charset(
		    &a->archive, "UTF-8", 1);
		if (xar->sconv == NULL)
			return (ARCHIVE_FATAL);
	}

	file = file_new(a, entry);
	if (file == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate data");
		return (ARCHIVE_FATAL);
	}
	r2 = file_gen_utility_names(a, file);
	if (r2 < ARCHIVE_WARN)
		return (r2);

	/*
	 * Ignore a path which looks like the top of directory name
	 * since we have already made the root directory of an Xar archive.
	 */
	if (archive_strlen(&(file->parentdir)) == 0 &&
	    archive_strlen(&(file->basename)) == 0) {
		file_free(file);
		return (r2);
	}

	/* Add entry into tree */
	file_entry = file->entry;
	r = file_tree(a, &file);
	if (r != ARCHIVE_OK)
		return (r);
	/* There is the same file in tree and
	 * the current file is older than the file in tree.
	 * So we don't need the current file data anymore. */
	if (file->entry != file_entry)
		return (r2);
	if (file->id == 0)
		file_register(xar, file);

	/* A virtual file, which is a directory, does not have
	 * any contents and we won't store it into a archive
	 * file other than its name. */
	if (file->virtual)
		return (r2);

	/*
	 * Prepare to save the contents of the file.
	 */
	if (xar->temp_fd == -1) {
		int algsize;
		xar->temp_offset = 0;
		xar->temp_fd = __archive_mktemp(NULL);
		if (xar->temp_fd < 0) {
			archive_set_error(&a->archive, errno,
			    "Couldn't create temporary file");
			return (ARCHIVE_FATAL);
		}
		algsize = getalgsize(xar->opt_toc_sumalg);
		if (algsize > 0) {
			if (lseek(xar->temp_fd, algsize, SEEK_SET) < 0) {
				archive_set_error(&(a->archive), errno,
				    "lseek failed");
				return (ARCHIVE_FATAL);
			}
			xar->temp_offset = algsize;
		}
	}

	if (archive_entry_hardlink(file->entry) == NULL) {
		r = save_xattrs(a, file);
		if (r != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	}

	/* Non regular files contents are unneeded to be saved to
	 * a temporary file. */
	if (archive_entry_filetype(file->entry) != AE_IFREG)
		return (r2);

	/*
	 * Set the current file to cur_file to read its contents.
	 */
	xar->cur_file = file;

	if (archive_entry_nlink(file->entry) > 1) {
		r = file_register_hardlink(a, file);
		if (r != ARCHIVE_OK)
			return (r);
		if (archive_entry_hardlink(file->entry) != NULL) {
			archive_entry_unset_size(file->entry);
			return (r2);
		}
	}

	/* Save a offset of current file in temporary file. */
	file->data.temp_offset = xar->temp_offset;
	file->data.size = archive_entry_size(file->entry);
	file->data.compression = xar->opt_compression;
	xar->bytes_remaining = archive_entry_size(file->entry);
	checksum_init(&(xar->a_sumwrk), xar->opt_sumalg);
	checksum_init(&(xar->e_sumwrk), xar->opt_sumalg);
	r = xar_compression_init_encoder(a);

	if (r != ARCHIVE_OK)
		return (r);
	else
		return (r2);
}

static int
write_to_temp(struct archive_write *a, const void *buff, size_t s)
{
	struct xar *xar;
	const unsigned char *p;
	ssize_t ws;

	xar = (struct xar *)a->format_data;
	p = (const unsigned char *)buff;
	while (s) {
		ws = write(xar->temp_fd, p, s);
		if (ws < 0) {
			archive_set_error(&(a->archive), errno,
			    "fwrite function failed");
			return (ARCHIVE_FATAL);
		}
		s -= ws;
		p += ws;
		xar->temp_offset += ws;
	}
	return (ARCHIVE_OK);
}

static ssize_t
xar_write_data(struct archive_write *a, const void *buff, size_t s)
{
	struct xar *xar;
	enum la_zaction run;
	size_t size, rsize;
	int r;

	xar = (struct xar *)a->format_data;

	if (s > xar->bytes_remaining)
		s = (size_t)xar->bytes_remaining;
	if (s == 0 || xar->cur_file == NULL)
		return (0);
	if (xar->cur_file->data.compression == NONE) {
		checksum_update(&(xar->e_sumwrk), buff, s);
		checksum_update(&(xar->a_sumwrk), buff, s);
		size = rsize = s;
	} else {
		xar->stream.next_in = (const unsigned char *)buff;
		xar->stream.avail_in = s;
		if (xar->bytes_remaining > s)
			run = ARCHIVE_Z_RUN;
		else
			run = ARCHIVE_Z_FINISH;
		/* Compress file data. */
		r = compression_code(&(a->archive), &(xar->stream), run);
		if (r != ARCHIVE_OK && r != ARCHIVE_EOF)
			return (ARCHIVE_FATAL);
		rsize = s - xar->stream.avail_in;
		checksum_update(&(xar->e_sumwrk), buff, rsize);
		size = sizeof(xar->wbuff) - xar->stream.avail_out;
		checksum_update(&(xar->a_sumwrk), xar->wbuff, size);
	}
#if !defined(_WIN32) || defined(__CYGWIN__)
	if (xar->bytes_remaining ==
	    (uint64_t)archive_entry_size(xar->cur_file->entry)) {
		/*
		 * Get the path of a shell script if so.
		 */
		const unsigned char *b = (const unsigned char *)buff;

		archive_string_empty(&(xar->cur_file->script));
		if (rsize > 2 && b[0] == '#' && b[1] == '!') {
			size_t i, end, off;

			off = 2;
			if (b[off] == ' ')
				off++;
#ifdef PATH_MAX
			if ((rsize - off) > PATH_MAX)
				end = off + PATH_MAX;
			else
#endif
				end = rsize;
			/* Find the end of a script path. */
			for (i = off; i < end && b[i] != '\0' &&
			    b[i] != '\n' && b[i] != '\r' &&
			    b[i] != ' ' && b[i] != '\t'; i++)
				;
			archive_strncpy(&(xar->cur_file->script), b + off,
			    i - off);
		}
	}
#endif

	if (xar->cur_file->data.compression == NONE) {
		if (write_to_temp(a, buff, size) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	} else {
		if (write_to_temp(a, xar->wbuff, size) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	}
	xar->bytes_remaining -= rsize;
	xar->cur_file->data.length += size;

	return (rsize);
}

static int
xar_finish_entry(struct archive_write *a)
{
	struct xar *xar;
	struct file *file;
	size_t s;
	ssize_t w;

	xar = (struct xar *)a->format_data;
	if (xar->cur_file == NULL)
		return (ARCHIVE_OK);

	while (xar->bytes_remaining > 0) {
		s = (size_t)xar->bytes_remaining;
		if (s > a->null_length)
			s = a->null_length;
		w = xar_write_data(a, a->nulls, s);
		if (w > 0)
			xar->bytes_remaining -= w;
		else
			return (w);
	}
	file = xar->cur_file;
	checksum_final(&(xar->e_sumwrk), &(file->data.e_sum));
	checksum_final(&(xar->a_sumwrk), &(file->data.a_sum));
	xar->cur_file = NULL;

	return (ARCHIVE_OK);
}

static int
xmlwrite_string_attr(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, const char *value,
	const char *attrkey, const char *attrvalue)
{
	int r;

	r = xmlTextWriterStartElement(writer, BAD_CAST_CONST(key));
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterStartElement() failed: %d", r);
		return (ARCHIVE_FATAL);
	}
	if (attrkey != NULL && attrvalue != NULL) {
		r = xmlTextWriterWriteAttribute(writer,
		    BAD_CAST_CONST(attrkey), BAD_CAST_CONST(attrvalue));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteAttribute() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}
	if (value != NULL) {
		r = xmlTextWriterWriteString(writer, BAD_CAST_CONST(value));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteString() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}
	r = xmlTextWriterEndElement(writer);
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterEndElement() failed: %d", r);
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

static int
xmlwrite_string(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, const char *value)
{
	int r;

	if (value == NULL)
		return (ARCHIVE_OK);

	r = xmlTextWriterStartElement(writer, BAD_CAST_CONST(key));
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterStartElement() failed: %d", r);
		return (ARCHIVE_FATAL);
	}
	if (value != NULL) {
		r = xmlTextWriterWriteString(writer, BAD_CAST_CONST(value));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteString() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}
	r = xmlTextWriterEndElement(writer);
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterEndElement() failed: %d", r);
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

static int
xmlwrite_fstring(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, const char *fmt, ...)
{
	struct xar *xar;
	va_list ap;

	xar = (struct xar *)a->format_data;
	va_start(ap, fmt);
	archive_string_empty(&xar->vstr);
	archive_string_vsprintf(&xar->vstr, fmt, ap);
	va_end(ap);
	return (xmlwrite_string(a, writer, key, xar->vstr.s));
}

static int
xmlwrite_time(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, time_t t, int z)
{
	char timestr[100];
	struct tm tm;

#if defined(HAVE_GMTIME_R)
	gmtime_r(&t, &tm);
#elif defined(HAVE__GMTIME64_S)
	_gmtime64_s(&tm, &t);
#else
	memcpy(&tm, gmtime(&t), sizeof(tm));
#endif
	memset(&timestr, 0, sizeof(timestr));
	/* Do not use %F and %T for portability. */
	strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", &tm);
	if (z)
		strcat(timestr, "Z");
	return (xmlwrite_string(a, writer, key, timestr));
}

static int
xmlwrite_mode(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, mode_t mode)
{
	char ms[5];

	ms[0] = '0';
	ms[1] = '0' + ((mode >> 6) & 07);
	ms[2] = '0' + ((mode >> 3) & 07);
	ms[3] = '0' + (mode & 07);
	ms[4] = '\0';

	return (xmlwrite_string(a, writer, key, ms));
}

static int
xmlwrite_sum(struct archive_write *a, xmlTextWriterPtr writer,
	const char *key, struct chksumval *sum)
{
	const char *algname;
	int algsize;
	char buff[MAX_SUM_SIZE*2 + 1];
	char *p;
	unsigned char *s;
	int i, r;

	if (sum->len > 0) {
		algname = getalgname(sum->alg);
		algsize = getalgsize(sum->alg);
		if (algname != NULL) {
			const char *hex = "0123456789abcdef";
			p = buff;
			s = sum->val;
			for (i = 0; i < algsize; i++) {
				*p++ = hex[(*s >> 4)];
				*p++ = hex[(*s & 0x0f)];
				s++;
			}
			*p = '\0';
			r = xmlwrite_string_attr(a, writer,
			    key, buff,
			    "style", algname);
			if (r < 0)
				return (ARCHIVE_FATAL);
		}
	}
	return (ARCHIVE_OK);
}

static int
xmlwrite_heap(struct archive_write *a, xmlTextWriterPtr writer,
	struct heap_data *heap)
{
	const char *encname;
	int r;

	r = xmlwrite_fstring(a, writer, "length", "%ju", heap->length);
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = xmlwrite_fstring(a, writer, "offset", "%ju", heap->temp_offset);
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = xmlwrite_fstring(a, writer, "size", "%ju", heap->size);
	if (r < 0)
		return (ARCHIVE_FATAL);
	switch (heap->compression) {
	case GZIP:
		encname = "application/x-gzip"; break;
	case BZIP2:
		encname = "application/x-bzip2"; break;
	case LZMA:
		encname = "application/x-lzma"; break;
	case XZ:
		encname = "application/x-xz"; break;
	default:
		encname = "application/octet-stream"; break;
	}
	r = xmlwrite_string_attr(a, writer, "encoding", NULL,
	    "style", encname);
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = xmlwrite_sum(a, writer, "archived-checksum", &(heap->a_sum));
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = xmlwrite_sum(a, writer, "extracted-checksum", &(heap->e_sum));
	if (r < 0)
		return (ARCHIVE_FATAL);
	return (ARCHIVE_OK);
}

/*
 * xar utility records fflags as following xml elements:
 *   <flags>
 *     <UserNoDump/>
 *     .....
 *   </flags>
 * or
 *   <ext2>
 *     <NoDump/>
 *     .....
 *   </ext2>
 * If xar is running on BSD platform, records <flags>..</flags>;
 * if xar is running on linux platform, records <ext2>..</ext2>;
 * otherwise does not record.
 *
 * Our implements records both <flags> and <ext2> if it's necessary.
 */
static int
make_fflags_entry(struct archive_write *a, xmlTextWriterPtr writer,
    const char *element, const char *fflags_text)
{
	static const struct flagentry {
		const char	*name;
		const char	*xarname;
	}
	flagbsd[] = {
		{ "sappnd",	"SystemAppend"},
		{ "sappend",	"SystemAppend"},
		{ "arch",	"SystemArchived"},
		{ "archived",	"SystemArchived"},
		{ "schg",	"SystemImmutable"},
		{ "schange",	"SystemImmutable"},
		{ "simmutable",	"SystemImmutable"},
		{ "nosunlnk",	"SystemNoUnlink"},
		{ "nosunlink",	"SystemNoUnlink"},
		{ "snapshot",	"SystemSnapshot"},
		{ "uappnd",	"UserAppend"},
		{ "uappend",	"UserAppend"},
		{ "uchg",	"UserImmutable"},
		{ "uchange",	"UserImmutable"},
		{ "uimmutable",	"UserImmutable"},
		{ "nodump",	"UserNoDump"},
		{ "noopaque",	"UserOpaque"},
		{ "nouunlnk",	"UserNoUnlink"},
		{ "nouunlink",	"UserNoUnlink"},
		{ NULL, NULL}
	},
	flagext2[] = {
		{ "sappnd",	"AppendOnly"},
		{ "sappend",	"AppendOnly"},
		{ "schg",	"Immutable"},
		{ "schange",	"Immutable"},
		{ "simmutable",	"Immutable"},
		{ "nodump",	"NoDump"},
		{ "nouunlnk",	"Undelete"},
		{ "nouunlink",	"Undelete"},
		{ "btree",	"BTree"},
		{ "comperr",	"CompError"},
		{ "compress",	"Compress"},
		{ "noatime",	"NoAtime"},
		{ "compdirty",	"CompDirty"},
		{ "comprblk",	"CompBlock"},
		{ "dirsync",	"DirSync"},
		{ "hashidx",	"HashIndexed"},
		{ "imagic",	"iMagic"},
		{ "journal",	"Journaled"},
		{ "securedeletion",	"SecureDeletion"},
		{ "sync",	"Synchronous"},
		{ "notail",	"NoTail"},
		{ "topdir",	"TopDir"},
		{ "reserved",	"Reserved"},
		{ NULL, NULL}
	};
	const struct flagentry *fe, *flagentry;
#define FLAGENTRY_MAXSIZE ((sizeof(flagbsd)+sizeof(flagext2))/sizeof(flagbsd))
	const struct flagentry *avail[FLAGENTRY_MAXSIZE];
	const char *p;
	int i, n, r;

	if (strcmp(element, "ext2") == 0)
		flagentry = flagext2;
	else
		flagentry = flagbsd;
	n = 0;
	p = fflags_text;
	do {
		const char *cp;

		cp = strchr(p, ',');
		if (cp == NULL)
			cp = p + strlen(p);

		for (fe = flagentry; fe->name != NULL; fe++) {
			if (fe->name[cp - p] != '\0'
			    || p[0] != fe->name[0])
				continue;
			if (strncmp(p, fe->name, cp - p) == 0) {
				avail[n++] = fe;
				break;
			}
		}
		if (*cp == ',')
			p = cp + 1;
		else
			p = NULL;
	} while (p != NULL);

	if (n > 0) {
		r = xmlTextWriterStartElement(writer, BAD_CAST_CONST(element));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		for (i = 0; i < n; i++) {
			r = xmlwrite_string(a, writer,
			    avail[i]->xarname, NULL);
			if (r != ARCHIVE_OK)
				return (r);
		}

		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}
	return (ARCHIVE_OK);
}

static int
make_file_entry(struct archive_write *a, xmlTextWriterPtr writer,
    struct file *file)
{
	struct xar *xar;
	const char *filetype, *filelink, *fflags;
	struct archive_string linkto;
	struct heap_data *heap;
	unsigned char *tmp;
	const char *p;
	size_t len;
	int r, r2, l, ll;

	xar = (struct xar *)a->format_data;
	r2 = ARCHIVE_OK;

	/*
	 * Make a file name entry, "<name>".
	 */
	l = ll = archive_strlen(&(file->basename));
	tmp = malloc(l);
	if (tmp == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}
	r = UTF8Toisolat1(tmp, &l, BAD_CAST(file->basename.s), &ll);
	free(tmp);
	if (r < 0) {
		r = xmlTextWriterStartElement(writer, BAD_CAST("name"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlTextWriterWriteAttribute(writer,
		    BAD_CAST("enctype"), BAD_CAST("base64"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteAttribute() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlTextWriterWriteBase64(writer, file->basename.s,
		    0, archive_strlen(&(file->basename)));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteBase64() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	} else {
		r = xmlwrite_string(a, writer, "name", file->basename.s);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a file type entry, "<type>".
	 */
	filelink = NULL;
	archive_string_init(&linkto);
	switch (archive_entry_filetype(file->entry)) {
	case AE_IFDIR:
		filetype = "directory"; break;
	case AE_IFLNK:
		filetype = "symlink"; break;
	case AE_IFCHR:
		filetype = "character special"; break;
	case AE_IFBLK:
		filetype = "block special"; break;
	case AE_IFSOCK:
		filetype = "socket"; break;
	case AE_IFIFO:
		filetype = "fifo"; break;
	case AE_IFREG:
	default:
		if (file->hardlink_target != NULL) {
			filetype = "hardlink";
			filelink = "link";
			if (file->hardlink_target == file)
				archive_strcpy(&linkto, "original");
			else
				archive_string_sprintf(&linkto, "%d",
				    file->hardlink_target->id);
		} else
			filetype = "file";
		break;
	}
	r = xmlwrite_string_attr(a, writer, "type", filetype,
	    filelink, linkto.s);
	archive_string_free(&linkto);
	if (r < 0)
		return (ARCHIVE_FATAL);

	/*
	 * On a virtual directory, we record "name" and "type" only.
	 */
	if (file->virtual)
		return (ARCHIVE_OK);

	switch (archive_entry_filetype(file->entry)) {
	case AE_IFLNK:
		/*
		 * xar utility has checked a file type, which
		 * a symbolic-link file has referenced.
		 * For example:
		 *   <link type="directory">../ref/</link>
		 *   The symlink target file is "../ref/" and its
		 *   file type is a directory.
		 *
		 *   <link type="file">../f</link>
		 *   The symlink target file is "../f" and its
		 *   file type is a regular file.
		 *
		 * But our implementation cannot do it, and then we
		 * always record that a attribute "type" is "broken",
		 * for example:
		 *   <link type="broken">foo/bar</link>
		 *   It means "foo/bar" is not reachable.
		 */
		r = xmlwrite_string_attr(a, writer, "link",
		    file->symlink.s,
		    "type", "broken");
		if (r < 0)
			return (ARCHIVE_FATAL);
		break;
	case AE_IFCHR:
	case AE_IFBLK:
		r = xmlTextWriterStartElement(writer, BAD_CAST("device"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlwrite_fstring(a, writer, "major",
		    "%d", archive_entry_rdevmajor(file->entry));
		if (r < 0)
			return (ARCHIVE_FATAL);
		r = xmlwrite_fstring(a, writer, "minor",
		    "%d", archive_entry_rdevminor(file->entry));
		if (r < 0)
			return (ARCHIVE_FATAL);
		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		break;
	default:
		break;
	}

	/*
	 * Make a inode entry, "<inode>".
	 */
	r = xmlwrite_fstring(a, writer, "inode",
	    "%jd", archive_entry_ino64(file->entry));
	if (r < 0)
		return (ARCHIVE_FATAL);
	if (archive_entry_dev(file->entry) != 0) {
		r = xmlwrite_fstring(a, writer, "deviceno",
		    "%d", archive_entry_dev(file->entry));
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a file mode entry, "<mode>".
	 */
	r = xmlwrite_mode(a, writer, "mode",
	    archive_entry_mode(file->entry));
	if (r < 0)
		return (ARCHIVE_FATAL);

	/*
	 * Make a user entry, "<uid>" and "<user>.
	 */
	r = xmlwrite_fstring(a, writer, "uid",
	    "%d", archive_entry_uid(file->entry));
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = archive_entry_uname_l(file->entry, &p, &len, xar->sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Uname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate uname '%s' to UTF-8",
		    archive_entry_uname(file->entry));
		r2 = ARCHIVE_WARN;
	}
	if (len > 0) {
		r = xmlwrite_string(a, writer, "user", p);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a group entry, "<gid>" and "<group>.
	 */
	r = xmlwrite_fstring(a, writer, "gid",
	    "%d", archive_entry_gid(file->entry));
	if (r < 0)
		return (ARCHIVE_FATAL);
	r = archive_entry_gname_l(file->entry, &p, &len, xar->sconv);
	if (r != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Gname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate gname '%s' to UTF-8",
		    archive_entry_gname(file->entry));
		r2 = ARCHIVE_WARN;
	}
	if (len > 0) {
		r = xmlwrite_string(a, writer, "group", p);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a ctime entry, "<ctime>".
	 */
	if (archive_entry_ctime_is_set(file->entry)) {
		r = xmlwrite_time(a, writer, "ctime",
		    archive_entry_ctime(file->entry), 1);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a mtime entry, "<mtime>".
	 */
	if (archive_entry_mtime_is_set(file->entry)) {
		r = xmlwrite_time(a, writer, "mtime",
		    archive_entry_mtime(file->entry), 1);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make a atime entry, "<atime>".
	 */
	if (archive_entry_atime_is_set(file->entry)) {
		r = xmlwrite_time(a, writer, "atime",
		    archive_entry_atime(file->entry), 1);
		if (r < 0)
			return (ARCHIVE_FATAL);
	}

	/*
	 * Make fflags entries, "<flags>" and "<ext2>".
	 */
	fflags = archive_entry_fflags_text(file->entry);
	if (fflags != NULL) {
		r = make_fflags_entry(a, writer, "flags", fflags);
		if (r < 0)
			return (r);
		r = make_fflags_entry(a, writer, "ext2", fflags);
		if (r < 0)
			return (r);
	}

	/*
	 * Make extended attribute entries, "<ea>".
	 */
	archive_entry_xattr_reset(file->entry);
	for (heap = file->xattr.first; heap != NULL; heap = heap->next) {
		const char *name;
		const void *value;
		size_t size;

		archive_entry_xattr_next(file->entry,
		    &name, &value, &size);
		r = xmlTextWriterStartElement(writer, BAD_CAST("ea"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlTextWriterWriteFormatAttribute(writer,
		    BAD_CAST("id"), "%d", heap->id);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteAttribute() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
		r = xmlwrite_heap(a, writer, heap);
		if (r < 0)
			return (ARCHIVE_FATAL);
		r = xmlwrite_string(a, writer, "name", name);
		if (r < 0)
			return (ARCHIVE_FATAL);

		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}

	/*
	 * Make a file data entry, "<data>".
	 */
	if (file->data.length > 0) {
		r = xmlTextWriterStartElement(writer, BAD_CAST("data"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}

		r = xmlwrite_heap(a, writer, &(file->data));
		if (r < 0)
			return (ARCHIVE_FATAL);

		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}

	if (archive_strlen(&file->script) > 0) {
		r = xmlTextWriterStartElement(writer, BAD_CAST("content"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}

		r = xmlwrite_string(a, writer,
		    "interpreter", file->script.s);
		if (r < 0)
			return (ARCHIVE_FATAL);

		r = xmlwrite_string(a, writer, "type", "script");
		if (r < 0)
			return (ARCHIVE_FATAL);

		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			return (ARCHIVE_FATAL);
		}
	}

	return (r2);
}

/*
 * Make the TOC
 */
static int
make_toc(struct archive_write *a)
{
	struct xar *xar;
	struct file *np;
	xmlBufferPtr bp;
	xmlTextWriterPtr writer;
	int algsize;
	int r, ret;

	xar = (struct xar *)a->format_data;

	ret = ARCHIVE_FATAL;

	/*
	 * Initialize xml writer.
	 */
	writer = NULL;
	bp = xmlBufferCreate();
	if (bp == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "xmlBufferCreate() "
		    "couldn't create xml buffer");
		goto exit_toc;
	}
	writer = xmlNewTextWriterMemory(bp, 0);
	if (writer == NULL) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlNewTextWriterMemory() "
		    "couldn't create xml writer");
		goto exit_toc;
	}
	r = xmlTextWriterStartDocument(writer, "1.0", "UTF-8", NULL);
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterStartDocument() failed: %d", r);
		goto exit_toc;
	}
	r = xmlTextWriterSetIndent(writer, 4);
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterSetIndent() failed: %d", r);
		goto exit_toc;
	}

	/*
	 * Start recording TOC
	 */
	r = xmlTextWriterStartElement(writer, BAD_CAST("xar"));
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterStartElement() failed: %d", r);
		goto exit_toc;
	}
	r = xmlTextWriterStartElement(writer, BAD_CAST("toc"));
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterStartDocument() failed: %d", r);
		goto exit_toc;
	}

	/*
	 * Record the creation time of the archive file.
	 */
	r = xmlwrite_time(a, writer, "creation-time", time(NULL), 0);
	if (r < 0)
		goto exit_toc;

	/*
	 * Record the checksum value of TOC
	 */
	algsize = getalgsize(xar->opt_toc_sumalg);
	if (algsize) {
		/*
		 * Record TOC checksum
		 */
		r = xmlTextWriterStartElement(writer, BAD_CAST("checksum"));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterStartElement() failed: %d", r);
			goto exit_toc;
		}
		r = xmlTextWriterWriteAttribute(writer, BAD_CAST("style"),
		    BAD_CAST_CONST(getalgname(xar->opt_toc_sumalg)));
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterWriteAttribute() failed: %d", r);
			goto exit_toc;
		}

		/*
		 * Record the offset of the value of checksum of TOC
		 */
		r = xmlwrite_string(a, writer, "offset", "0");
		if (r < 0)
			goto exit_toc;

		/*
		 * Record the size of the value of checksum of TOC
		 */
		r = xmlwrite_fstring(a, writer, "size", "%d", algsize);
		if (r < 0)
			goto exit_toc;

		r = xmlTextWriterEndElement(writer);
		if (r < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "xmlTextWriterEndElement() failed: %d", r);
			goto exit_toc;
		}
	}

	np = xar->root;
	do {
		if (np != np->parent) {
			r = make_file_entry(a, writer, np);
			if (r != ARCHIVE_OK)
				goto exit_toc;
		}

		if (np->dir && np->children.first != NULL) {
			/* Enter to sub directories. */
			np = np->children.first;
			r = xmlTextWriterStartElement(writer,
			    BAD_CAST("file"));
			if (r < 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "xmlTextWriterStartElement() "
				    "failed: %d", r);
				goto exit_toc;
			}
			r = xmlTextWriterWriteFormatAttribute(
			    writer, BAD_CAST("id"), "%d", np->id);
			if (r < 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "xmlTextWriterWriteAttribute() "
				    "failed: %d", r);
				goto exit_toc;
			}
			continue;
		}
		while (np != np->parent) {
			r = xmlTextWriterEndElement(writer);
			if (r < 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "xmlTextWriterEndElement() "
				    "failed: %d", r);
				goto exit_toc;
			}
			if (np->chnext == NULL) {
				/* Return to the parent directory. */
				np = np->parent;
			} else {
				np = np->chnext;
				r = xmlTextWriterStartElement(writer,
				    BAD_CAST("file"));
				if (r < 0) {
					archive_set_error(&a->archive,
					    ARCHIVE_ERRNO_MISC,
					    "xmlTextWriterStartElement() "
					    "failed: %d", r);
					goto exit_toc;
				}
				r = xmlTextWriterWriteFormatAttribute(
				    writer, BAD_CAST("id"), "%d", np->id);
				if (r < 0) {
					archive_set_error(&a->archive,
					    ARCHIVE_ERRNO_MISC,
					    "xmlTextWriterWriteAttribute() "
					    "failed: %d", r);
					goto exit_toc;
				}
				break;
			}
		}
	} while (np != np->parent);

	r = xmlTextWriterEndDocument(writer);
	if (r < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_MISC,
		    "xmlTextWriterEndDocument() failed: %d", r);
		goto exit_toc;
	}
#if DEBUG_PRINT_TOC
	fprintf(stderr, "\n---TOC-- %d bytes --\n%s\n",
	    strlen((const char *)bp->content), bp->content);
#endif

	/*
	 * Compress the TOC and calculate the sum of the TOC.
	 */
	xar->toc.temp_offset = xar->temp_offset;
	xar->toc.size = bp->use;
	checksum_init(&(xar->a_sumwrk), xar->opt_toc_sumalg);

	r = compression_init_encoder_gzip(&(a->archive),
	    &(xar->stream), 6, 1);
	if (r != ARCHIVE_OK)
		goto exit_toc;
	xar->stream.next_in = bp->content;
	xar->stream.avail_in = bp->use;
	xar->stream.total_in = 0;
	xar->stream.next_out = xar->wbuff;
	xar->stream.avail_out = sizeof(xar->wbuff);
	xar->stream.total_out = 0;
	for (;;) {
		size_t size;

		r = compression_code(&(a->archive),
		    &(xar->stream), ARCHIVE_Z_FINISH);
		if (r != ARCHIVE_OK && r != ARCHIVE_EOF)
			goto exit_toc;
		size = sizeof(xar->wbuff) - xar->stream.avail_out;
		checksum_update(&(xar->a_sumwrk), xar->wbuff, size);
		if (write_to_temp(a, xar->wbuff, size) != ARCHIVE_OK)
			goto exit_toc;
		if (r == ARCHIVE_EOF)
			break;
		xar->stream.next_out = xar->wbuff;
		xar->stream.avail_out = sizeof(xar->wbuff);
	}
	r = compression_end(&(a->archive), &(xar->stream));
	if (r != ARCHIVE_OK)
		goto exit_toc;
	xar->toc.length = xar->stream.total_out;
	xar->toc.compression = GZIP;
	checksum_final(&(xar->a_sumwrk), &(xar->toc.a_sum));

	ret = ARCHIVE_OK;
exit_toc:
	if (writer)
		xmlFreeTextWriter(writer);
	if (bp)
		xmlBufferFree(bp);

	return (ret);
}

static int
flush_wbuff(struct archive_write *a)
{
	struct xar *xar;
	int r;
	size_t s;

	xar = (struct xar *)a->format_data;
	s = sizeof(xar->wbuff) - xar->wbuff_remaining;
	r = __archive_write_output(a, xar->wbuff, s);
	if (r != ARCHIVE_OK)
		return (r);
	xar->wbuff_remaining = sizeof(xar->wbuff);
	return (r);
}

static int
copy_out(struct archive_write *a, uint64_t offset, uint64_t length)
{
	struct xar *xar;
	int r;

	xar = (struct xar *)a->format_data;
	if (lseek(xar->temp_fd, offset, SEEK_SET) < 0) {
		archive_set_error(&(a->archive), errno, "lseek failed");
		return (ARCHIVE_FATAL);
	}
	while (length) {
		size_t rsize;
		ssize_t rs;
		unsigned char *wb;

		if (length > xar->wbuff_remaining)
			rsize = xar->wbuff_remaining;
		else
			rsize = (size_t)length;
		wb = xar->wbuff + (sizeof(xar->wbuff) - xar->wbuff_remaining);
		rs = read(xar->temp_fd, wb, rsize);
		if (rs < 0) {
			archive_set_error(&(a->archive), errno,
			    "Can't read temporary file(%jd)",
			    (intmax_t)rs);
			return (ARCHIVE_FATAL);
		}
		if (rs == 0) {
			archive_set_error(&(a->archive), 0,
			    "Truncated xar archive");
			return (ARCHIVE_FATAL);
		}
		xar->wbuff_remaining -= rs;
		length -= rs;
		if (xar->wbuff_remaining == 0) {
			r = flush_wbuff(a);
			if (r != ARCHIVE_OK)
				return (r);
		}
	}
	return (ARCHIVE_OK);
}

static int
xar_close(struct archive_write *a)
{
	struct xar *xar;
	unsigned char *wb;
	uint64_t length;
	int r;

	xar = (struct xar *)a->format_data;

	/* Empty! */
	if (xar->root->children.first == NULL)
		return (ARCHIVE_OK);

	/* Save the length of all file extended attributes and contents. */
	length = xar->temp_offset;

	/* Connect hardlinked files */
	file_connect_hardlink_files(xar);

	/* Make the TOC */
	r = make_toc(a);
	if (r != ARCHIVE_OK)
		return (r);
	/*
	 * Make the xar header on wbuff(write buffer).
	 */
	wb = xar->wbuff;
	xar->wbuff_remaining = sizeof(xar->wbuff);
	archive_be32enc(&wb[0], HEADER_MAGIC);
	archive_be16enc(&wb[4], HEADER_SIZE);
	archive_be16enc(&wb[6], HEADER_VERSION);
	archive_be64enc(&wb[8], xar->toc.length);
	archive_be64enc(&wb[16], xar->toc.size);
	archive_be32enc(&wb[24], xar->toc.a_sum.alg);
	xar->wbuff_remaining -= HEADER_SIZE;

	/*
	 * Write the TOC
	 */
	r = copy_out(a, xar->toc.temp_offset, xar->toc.length);
	if (r != ARCHIVE_OK)
		return (r);

	/* Write the checksum value of the TOC. */
	if (xar->toc.a_sum.len) {
		if (xar->wbuff_remaining < xar->toc.a_sum.len) {
			r = flush_wbuff(a);
			if (r != ARCHIVE_OK)
				return (r);
		}
		wb = xar->wbuff + (sizeof(xar->wbuff) - xar->wbuff_remaining);
		memcpy(wb, xar->toc.a_sum.val, xar->toc.a_sum.len);
		xar->wbuff_remaining -= xar->toc.a_sum.len;
	}

	/*
	 * Write all file extended attributes and contents.
	 */
	r = copy_out(a, xar->toc.a_sum.len, length);
	if (r != ARCHIVE_OK)
		return (r);
	r = flush_wbuff(a);
	return (r);
}

static int
xar_free(struct archive_write *a)
{
	struct xar *xar;

	xar = (struct xar *)a->format_data;

	/* Close the temporary file. */
	if (xar->temp_fd >= 0)
		close(xar->temp_fd);

	archive_string_free(&(xar->cur_dirstr));
	archive_string_free(&(xar->tstr));
	archive_string_free(&(xar->vstr));
	file_free_hardlinks(xar);
	file_free_register(xar);
	compression_end(&(a->archive), &(xar->stream));
	free(xar);

	return (ARCHIVE_OK);
}

static int
file_cmp_node(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	const struct file *f1 = (const struct file *)n1;
	const struct file *f2 = (const struct file *)n2;

	return (strcmp(f1->basename.s, f2->basename.s));
}

static int
file_cmp_key(const struct archive_rb_node *n, const void *key)
{
	const struct file *f = (const struct file *)n;

	return (strcmp(f->basename.s, (const char *)key));
}

static struct file *
file_new(struct archive_write *a, struct archive_entry *entry)
{
	struct file *file;
	static const struct archive_rb_tree_ops rb_ops = {
		file_cmp_node, file_cmp_key
	};

	file = calloc(1, sizeof(*file));
	if (file == NULL)
		return (NULL);

	if (entry != NULL)
		file->entry = archive_entry_clone(entry);
	else
		file->entry = archive_entry_new2(&a->archive);
	if (file->entry == NULL) {
		free(file);
		return (NULL);
	}
	__archive_rb_tree_init(&(file->rbtree), &rb_ops);
	file->children.first = NULL;
	file->children.last = &(file->children.first);
	file->xattr.first = NULL;
	file->xattr.last = &(file->xattr.first);
	archive_string_init(&(file->parentdir));
	archive_string_init(&(file->basename));
	archive_string_init(&(file->symlink));
	archive_string_init(&(file->script));
	if (entry != NULL && archive_entry_filetype(entry) == AE_IFDIR)
		file->dir = 1;

	return (file);
}

static void
file_free(struct file *file)
{
	struct heap_data *heap, *next_heap;

	heap = file->xattr.first;
	while (heap != NULL) {
		next_heap = heap->next;
		free(heap);
		heap = next_heap;
	}
	archive_string_free(&(file->parentdir));
	archive_string_free(&(file->basename));
	archive_string_free(&(file->symlink));
	archive_string_free(&(file->script));
	archive_entry_free(file->entry);
	free(file);
}

static struct file *
file_create_virtual_dir(struct archive_write *a, struct xar *xar,
    const char *pathname)
{
	struct file *file;

	(void)xar; /* UNUSED */

	file = file_new(a, NULL);
	if (file == NULL)
		return (NULL);
	archive_entry_set_pathname(file->entry, pathname);
	archive_entry_set_mode(file->entry, 0555 | AE_IFDIR);

	file->dir = 1;
	file->virtual = 1;

	return (file);
}

static int
file_add_child_tail(struct file *parent, struct file *child)
{
	if (!__archive_rb_tree_insert_node(
	    &(parent->rbtree), (struct archive_rb_node *)child))
		return (0);
	child->chnext = NULL;
	*parent->children.last = child;
	parent->children.last = &(child->chnext);
	child->parent = parent;
	return (1);
}

/*
 * Find a entry from `parent'
 */
static struct file *
file_find_child(struct file *parent, const char *child_name)
{
	struct file *np;

	np = (struct file *)__archive_rb_tree_find_node(
	    &(parent->rbtree), child_name);
	return (np);
}

#if defined(_WIN32) || defined(__CYGWIN__)
static void
cleanup_backslash(char *utf8, size_t len)
{

	/* Convert a path-separator from '\' to  '/' */
	while (*utf8 != '\0' && len) {
		if (*utf8 == '\\')
			*utf8 = '/';
		++utf8;
		--len;
	}
}
#else
#define cleanup_backslash(p, len)	/* nop */
#endif

/*
 * Generate a parent directory name and a base name from a pathname.
 */
static int
file_gen_utility_names(struct archive_write *a, struct file *file)
{
	struct xar *xar;
	const char *pp;
	char *p, *dirname, *slash;
	size_t len;
	int r = ARCHIVE_OK;

	xar = (struct xar *)a->format_data;
	archive_string_empty(&(file->parentdir));
	archive_string_empty(&(file->basename));
	archive_string_empty(&(file->symlink));

	if (file->parent == file)/* virtual root */
		return (ARCHIVE_OK);

	if (archive_entry_pathname_l(file->entry, &pp, &len, xar->sconv)
	    != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't translate pathname '%s' to UTF-8",
		    archive_entry_pathname(file->entry));
		r = ARCHIVE_WARN;
	}
	archive_strncpy(&(file->parentdir), pp, len);
	len = file->parentdir.length;
	p = dirname = file->parentdir.s;
	/*
	 * Convert a path-separator from '\' to  '/'
	 */
	cleanup_backslash(p, len);

	/*
	 * Remove leading '/', '../' and './' elements
	 */
	while (*p) {
		if (p[0] == '/') {
			p++;
			len--;
		} else if (p[0] != '.')
			break;
		else if (p[1] == '.' && p[2] == '/') {
			p += 3;
			len -= 3;
		} else if (p[1] == '/' || (p[1] == '.' && p[2] == '\0')) {
			p += 2;
			len -= 2;
		} else if (p[1] == '\0') {
			p++;
			len--;
		} else
			break;
	}
	if (p != dirname) {
		memmove(dirname, p, len+1);
		p = dirname;
	}
	/*
	 * Remove "/","/." and "/.." elements from tail.
	 */
	while (len > 0) {
		size_t ll = len;

		if (len > 0 && p[len-1] == '/') {
			p[len-1] = '\0';
			len--;
		}
		if (len > 1 && p[len-2] == '/' && p[len-1] == '.') {
			p[len-2] = '\0';
			len -= 2;
		}
		if (len > 2 && p[len-3] == '/' && p[len-2] == '.' &&
		    p[len-1] == '.') {
			p[len-3] = '\0';
			len -= 3;
		}
		if (ll == len)
			break;
	}
	while (*p) {
		if (p[0] == '/') {
			if (p[1] == '/')
				/* Convert '//' --> '/' */
				memmove(p, p+1, strlen(p+1) + 1);
			else if (p[1] == '.' && p[2] == '/')
				/* Convert '/./' --> '/' */
				memmove(p, p+2, strlen(p+2) + 1);
			else if (p[1] == '.' && p[2] == '.' && p[3] == '/') {
				/* Convert 'dir/dir1/../dir2/'
				 *     --> 'dir/dir2/'
				 */
				char *rp = p -1;
				while (rp >= dirname) {
					if (*rp == '/')
						break;
					--rp;
				}
				if (rp > dirname) {
					strcpy(rp, p+3);
					p = rp;
				} else {
					strcpy(dirname, p+4);
					p = dirname;
				}
			} else
				p++;
		} else
			p++;
	}
	p = dirname;
	len = strlen(p);

	if (archive_entry_filetype(file->entry) == AE_IFLNK) {
		size_t len2;
		/* Convert symlink name too. */
		if (archive_entry_symlink_l(file->entry, &pp, &len2,
		    xar->sconv) != 0) {
			if (errno == ENOMEM) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory for Linkname");
				return (ARCHIVE_FATAL);
			}
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Can't translate symlink '%s' to UTF-8",
			    archive_entry_symlink(file->entry));
			r = ARCHIVE_WARN;
		}
		archive_strncpy(&(file->symlink), pp, len2);
		cleanup_backslash(file->symlink.s, file->symlink.length);
	}
	/*
	 * - Count up directory elements.
	 * - Find out the position which points the last position of
	 *   path separator('/').
	 */
	slash = NULL;
	for (; *p != '\0'; p++)
		if (*p == '/')
			slash = p;
	if (slash == NULL) {
		/* The pathname doesn't have a parent directory. */
		file->parentdir.length = len;
		archive_string_copy(&(file->basename), &(file->parentdir));
		archive_string_empty(&(file->parentdir));
		*file->parentdir.s = '\0';
		return (r);
	}

	/* Make a basename from dirname and slash */
	*slash  = '\0';
	file->parentdir.length = slash - dirname;
	archive_strcpy(&(file->basename),  slash + 1);
	return (r);
}

static int
get_path_component(char *name, int n, const char *fn)
{
	char *p;
	int l;

	p = strchr(fn, '/');
	if (p == NULL) {
		if ((l = strlen(fn)) == 0)
			return (0);
	} else
		l = p - fn;
	if (l > n -1)
		return (-1);
	memcpy(name, fn, l);
	name[l] = '\0';

	return (l);
}

/*
 * Add a new entry into the tree.
 */
static int
file_tree(struct archive_write *a, struct file **filepp)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	char name[_MAX_FNAME];/* Included null terminator size. */
#elif defined(NAME_MAX) && NAME_MAX >= 255
	char name[NAME_MAX+1];
#else
	char name[256];
#endif
	struct xar *xar = (struct xar *)a->format_data;
	struct file *dent, *file, *np;
	struct archive_entry *ent;
	const char *fn, *p;
	int l;

	file = *filepp;
	dent = xar->root;
	if (file->parentdir.length > 0)
		fn = p = file->parentdir.s;
	else
		fn = p = "";

	/*
	 * If the path of the parent directory of `file' entry is
	 * the same as the path of `cur_dirent', add isoent to
	 * `cur_dirent'.
	 */
	if (archive_strlen(&(xar->cur_dirstr))
	      == archive_strlen(&(file->parentdir)) &&
	    strcmp(xar->cur_dirstr.s, fn) == 0) {
		if (!file_add_child_tail(xar->cur_dirent, file)) {
			np = (struct file *)__archive_rb_tree_find_node(
			    &(xar->cur_dirent->rbtree),
			    file->basename.s);
			goto same_entry;
		}
		return (ARCHIVE_OK);
	}

	for (;;) {
		l = get_path_component(name, sizeof(name), fn);
		if (l == 0) {
			np = NULL;
			break;
		}
		if (l < 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "A name buffer is too small");
			file_free(file);
			*filepp = NULL;
			return (ARCHIVE_FATAL);
		}

		np = file_find_child(dent, name);
		if (np == NULL || fn[0] == '\0')
			break;

		/* Find next subdirectory. */
		if (!np->dir) {
			/* NOT Directory! */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "`%s' is not directory, we cannot insert `%s' ",
			    archive_entry_pathname(np->entry),
			    archive_entry_pathname(file->entry));
			file_free(file);
			*filepp = NULL;
			return (ARCHIVE_FAILED);
		}
		fn += l;
		if (fn[0] == '/')
			fn++;
		dent = np;
	}
	if (np == NULL) {
		/*
		 * Create virtual parent directories.
		 */
		while (fn[0] != '\0') {
			struct file *vp;
			struct archive_string as;

			archive_string_init(&as);
			archive_strncat(&as, p, fn - p + l);
			if (as.s[as.length-1] == '/') {
				as.s[as.length-1] = '\0';
				as.length--;
			}
			vp = file_create_virtual_dir(a, xar, as.s);
			if (vp == NULL) {
				archive_string_free(&as);
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory");
				file_free(file);
				*filepp = NULL;
				return (ARCHIVE_FATAL);
			}
			archive_string_free(&as);
			if (file_gen_utility_names(a, vp) <= ARCHIVE_FAILED)
				return (ARCHIVE_FATAL);
			file_add_child_tail(dent, vp);
			file_register(xar, vp);
			np = vp;

			fn += l;
			if (fn[0] == '/')
				fn++;
			l = get_path_component(name, sizeof(name), fn);
			if (l < 0) {
				archive_string_free(&as);
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "A name buffer is too small");
				file_free(file);
				*filepp = NULL;
				return (ARCHIVE_FATAL);
			}
			dent = np;
		}

		/* Found out the parent directory where isoent can be
		 * inserted. */
		xar->cur_dirent = dent;
		archive_string_empty(&(xar->cur_dirstr));
		archive_string_ensure(&(xar->cur_dirstr),
		    archive_strlen(&(dent->parentdir)) +
		    archive_strlen(&(dent->basename)) + 2);
		if (archive_strlen(&(dent->parentdir)) +
		    archive_strlen(&(dent->basename)) == 0)
			xar->cur_dirstr.s[0] = 0;
		else {
			if (archive_strlen(&(dent->parentdir)) > 0) {
				archive_string_copy(&(xar->cur_dirstr),
				    &(dent->parentdir));
				archive_strappend_char(&(xar->cur_dirstr), '/');
			}
			archive_string_concat(&(xar->cur_dirstr),
			    &(dent->basename));
		}

		if (!file_add_child_tail(dent, file)) {
			np = (struct file *)__archive_rb_tree_find_node(
			    &(dent->rbtree), file->basename.s);
			goto same_entry;
		}
		return (ARCHIVE_OK);
	}

same_entry:
	/*
	 * We have already has the entry the filename of which is
	 * the same.
	 */
	if (archive_entry_filetype(np->entry) !=
	    archive_entry_filetype(file->entry)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Found duplicate entries `%s' and its file type is "
		    "different",
		    archive_entry_pathname(np->entry));
		file_free(file);
		*filepp = NULL;
		return (ARCHIVE_FAILED);
	}

	/* Swap files. */
	ent = np->entry;
	np->entry = file->entry;
	file->entry = ent;
	np->virtual = 0;

	file_free(file);
	*filepp = np;
	return (ARCHIVE_OK);
}

static void
file_register(struct xar *xar, struct file *file)
{
	file->id = xar->file_idx++;
        file->next = NULL;
        *xar->file_list.last = file;
        xar->file_list.last = &(file->next);
}

static void
file_init_register(struct xar *xar)
{
	xar->file_list.first = NULL;
	xar->file_list.last = &(xar->file_list.first);
}

static void
file_free_register(struct xar *xar)
{
	struct file *file, *file_next;

	file = xar->file_list.first;
	while (file != NULL) {
		file_next = file->next;
		file_free(file);
		file = file_next;
	}
}

/*
 * Register entry to get a hardlink target.
 */
static int
file_register_hardlink(struct archive_write *a, struct file *file)
{
	struct xar *xar = (struct xar *)a->format_data;
	struct hardlink *hl;
	const char *pathname;

	archive_entry_set_nlink(file->entry, 1);
	pathname = archive_entry_hardlink(file->entry);
	if (pathname == NULL) {
		/* This `file` is a hardlink target. */
		hl = malloc(sizeof(*hl));
		if (hl == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory");
			return (ARCHIVE_FATAL);
		}
		hl->nlink = 1;
		/* A hardlink target must be the first position. */
		file->hlnext = NULL;
		hl->file_list.first = file;
		hl->file_list.last = &(file->hlnext);
		__archive_rb_tree_insert_node(&(xar->hardlink_rbtree),
		    (struct archive_rb_node *)hl);
	} else {
		hl = (struct hardlink *)__archive_rb_tree_find_node(
		    &(xar->hardlink_rbtree), pathname);
		if (hl != NULL) {
			/* Insert `file` entry into the tail. */
			file->hlnext = NULL;
			*hl->file_list.last = file;
			hl->file_list.last = &(file->hlnext);
			hl->nlink++;
		}
		archive_entry_unset_size(file->entry);
	}

	return (ARCHIVE_OK);
}

/*
 * Hardlinked files have to have the same location of extent.
 * We have to find out hardlink target entries for entries which
 * have a hardlink target name.
 */
static void
file_connect_hardlink_files(struct xar *xar)
{
	struct archive_rb_node *n;
	struct hardlink *hl;
	struct file *target, *nf;

	ARCHIVE_RB_TREE_FOREACH(n, &(xar->hardlink_rbtree)) {
		hl = (struct hardlink *)n;

		/* The first entry must be a hardlink target. */
		target = hl->file_list.first;
		archive_entry_set_nlink(target->entry, hl->nlink);
		if (hl->nlink > 1)
			/* It means this file is a hardlink
			 * target itself. */
			target->hardlink_target = target;
		for (nf = target->hlnext;
		    nf != NULL; nf = nf->hlnext) {
			nf->hardlink_target = target;
			archive_entry_set_nlink(nf->entry, hl->nlink);
		}
	}
}

static int
file_hd_cmp_node(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	const struct hardlink *h1 = (const struct hardlink *)n1;
	const struct hardlink *h2 = (const struct hardlink *)n2;

	return (strcmp(archive_entry_pathname(h1->file_list.first->entry),
		       archive_entry_pathname(h2->file_list.first->entry)));
}

static int
file_hd_cmp_key(const struct archive_rb_node *n, const void *key)
{
	const struct hardlink *h = (const struct hardlink *)n;

	return (strcmp(archive_entry_pathname(h->file_list.first->entry),
		       (const char *)key));
}


static void
file_init_hardlinks(struct xar *xar)
{
	static const struct archive_rb_tree_ops rb_ops = {
		file_hd_cmp_node, file_hd_cmp_key,
	};

	__archive_rb_tree_init(&(xar->hardlink_rbtree), &rb_ops);
}

static void
file_free_hardlinks(struct xar *xar)
{
	struct archive_rb_node *n, *next;

	for (n = ARCHIVE_RB_TREE_MIN(&(xar->hardlink_rbtree)); n;) {
		next = __archive_rb_tree_iterate(&(xar->hardlink_rbtree),
		    n, ARCHIVE_RB_DIR_RIGHT);
		free(n);
		n = next;
	}
}

static void
checksum_init(struct chksumwork *sumwrk, enum sumalg sum_alg)
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
checksum_update(struct chksumwork *sumwrk, const void *buff, size_t size)
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

static void
checksum_final(struct chksumwork *sumwrk, struct chksumval *sumval)
{

	switch (sumwrk->alg) {
	case CKSUM_NONE:
		sumval->len = 0;
		break;
	case CKSUM_SHA1:
		archive_sha1_final(&(sumwrk->sha1ctx), sumval->val);
		sumval->len = SHA1_SIZE;
		break;
	case CKSUM_MD5:
		archive_md5_final(&(sumwrk->md5ctx), sumval->val);
		sumval->len = MD5_SIZE;
		break;
	}
	sumval->alg = sumwrk->alg;
}

#if !defined(HAVE_BZLIB_H) || !defined(BZ_CONFIG_ERROR) || !defined(HAVE_LZMA_H)
static int
compression_unsupported_encoder(struct archive *a,
    struct la_zstream *lastrm, const char *name)
{

	archive_set_error(a, ARCHIVE_ERRNO_MISC,
	    "%s compression not supported on this platform", name);
	lastrm->valid = 0;
	lastrm->real_stream = NULL;
	return (ARCHIVE_FAILED);
}
#endif

static int
compression_init_encoder_gzip(struct archive *a,
    struct la_zstream *lastrm, int level, int withheader)
{
	z_stream *strm;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for gzip stream");
		return (ARCHIVE_FATAL);
	}
	/* zlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (Bytef *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in = (uLong)lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out = (uLong)lastrm->total_out;
	if (deflateInit2(strm, level, Z_DEFLATED,
	    (withheader)?15:-15,
	    8, Z_DEFAULT_STRATEGY) != Z_OK) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lastrm->real_stream = strm;
	lastrm->valid = 1;
	lastrm->code = compression_code_gzip;
	lastrm->end = compression_end_gzip;
	return (ARCHIVE_OK);
}

static int
compression_code_gzip(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	z_stream *strm;
	int r;

	strm = (z_stream *)lastrm->real_stream;
	/* zlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (Bytef *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in = (uLong)lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out = (uLong)lastrm->total_out;
	r = deflate(strm,
	    (action == ARCHIVE_Z_FINISH)? Z_FINISH: Z_NO_FLUSH);
	lastrm->next_in = strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in = strm->total_in;
	lastrm->next_out = strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out = strm->total_out;
	switch (r) {
	case Z_OK:
		return (ARCHIVE_OK);
	case Z_STREAM_END:
		return (ARCHIVE_EOF);
	default:
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "GZip compression failed:"
		    " deflate() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_gzip(struct archive *a, struct la_zstream *lastrm)
{
	z_stream *strm;
	int r;

	strm = (z_stream *)lastrm->real_stream;
	r = deflateEnd(strm);
	free(strm);
	lastrm->real_stream = NULL;
	lastrm->valid = 0;
	if (r != Z_OK) {
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Failed to clean up compressor");
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
static int
compression_init_encoder_bzip2(struct archive *a,
    struct la_zstream *lastrm, int level)
{
	bz_stream *strm;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for bzip2 stream");
		return (ARCHIVE_FATAL);
	}
	/* bzlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (char *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in_lo32 = (uint32_t)(lastrm->total_in & 0xffffffff);
	strm->total_in_hi32 = (uint32_t)(lastrm->total_in >> 32);
	strm->next_out = (char *)lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out_lo32 = (uint32_t)(lastrm->total_out & 0xffffffff);
	strm->total_out_hi32 = (uint32_t)(lastrm->total_out >> 32);
	if (BZ2_bzCompressInit(strm, level, 0, 30) != BZ_OK) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lastrm->real_stream = strm;
	lastrm->valid = 1;
	lastrm->code = compression_code_bzip2;
	lastrm->end = compression_end_bzip2;
	return (ARCHIVE_OK);
}

static int
compression_code_bzip2(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	bz_stream *strm;
	int r;

	strm = (bz_stream *)lastrm->real_stream;
	/* bzlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (char *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in_lo32 = (uint32_t)(lastrm->total_in & 0xffffffff);
	strm->total_in_hi32 = (uint32_t)(lastrm->total_in >> 32);
	strm->next_out = (char *)lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out_lo32 = (uint32_t)(lastrm->total_out & 0xffffffff);
	strm->total_out_hi32 = (uint32_t)(lastrm->total_out >> 32);
	r = BZ2_bzCompress(strm,
	    (action == ARCHIVE_Z_FINISH)? BZ_FINISH: BZ_RUN);
	lastrm->next_in = (const unsigned char *)strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in =
	    (((uint64_t)(uint32_t)strm->total_in_hi32) << 32)
	    + (uint64_t)(uint32_t)strm->total_in_lo32;
	lastrm->next_out = (unsigned char *)strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out =
	    (((uint64_t)(uint32_t)strm->total_out_hi32) << 32)
	    + (uint64_t)(uint32_t)strm->total_out_lo32;
	switch (r) {
	case BZ_RUN_OK:     /* Non-finishing */
	case BZ_FINISH_OK:  /* Finishing: There's more work to do */
		return (ARCHIVE_OK);
	case BZ_STREAM_END: /* Finishing: all done */
		/* Only occurs in finishing case */
		return (ARCHIVE_EOF);
	default:
		/* Any other return value indicates an error */
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Bzip2 compression failed:"
		    " BZ2_bzCompress() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_bzip2(struct archive *a, struct la_zstream *lastrm)
{
	bz_stream *strm;
	int r;

	strm = (bz_stream *)lastrm->real_stream;
	r = BZ2_bzCompressEnd(strm);
	free(strm);
	lastrm->real_stream = NULL;
	lastrm->valid = 0;
	if (r != BZ_OK) {
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Failed to clean up compressor");
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

#else
static int
compression_init_encoder_bzip2(struct archive *a,
    struct la_zstream *lastrm, int level)
{

	(void) level; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "bzip2"));
}
#endif

#if defined(HAVE_LZMA_H)
static int
compression_init_encoder_lzma(struct archive *a,
    struct la_zstream *lastrm, int level)
{
	static const lzma_stream lzma_init_data = LZMA_STREAM_INIT;
	lzma_stream *strm;
	lzma_options_lzma lzma_opt;
	int r;

	if (lastrm->valid)
		compression_end(a, lastrm);
	if (lzma_lzma_preset(&lzma_opt, level)) {
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for lzma stream");
		return (ARCHIVE_FATAL);
	}
	*strm = lzma_init_data;
	r = lzma_alone_encoder(strm, &lzma_opt);
	switch (r) {
	case LZMA_OK:
		lastrm->real_stream = strm;
		lastrm->valid = 1;
		lastrm->code = compression_code_lzma;
		lastrm->end = compression_end_lzma;
		r = ARCHIVE_OK;
		break;
	case LZMA_MEM_ERROR:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library: "
		    "Cannot allocate memory");
		r =  ARCHIVE_FATAL;
		break;
        default:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library: "
		    "It's a bug in liblzma");
		r =  ARCHIVE_FATAL;
		break;
	}
	return (r);
}

static int
compression_init_encoder_xz(struct archive *a,
    struct la_zstream *lastrm, int level, int threads)
{
	static const lzma_stream lzma_init_data = LZMA_STREAM_INIT;
	lzma_stream *strm;
	lzma_filter *lzmafilters;
	lzma_options_lzma lzma_opt;
	int r;
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
	lzma_mt mt_options;
#endif

	(void)threads; /* UNUSED (if multi-threaded LZMA library not avail) */

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm) + sizeof(*lzmafilters) * 2);
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for xz stream");
		return (ARCHIVE_FATAL);
	}
	lzmafilters = (lzma_filter *)(strm+1);
	if (level > 6)
		level = 6;
	if (lzma_lzma_preset(&lzma_opt, level)) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lzmafilters[0].id = LZMA_FILTER_LZMA2;
	lzmafilters[0].options = &lzma_opt;
	lzmafilters[1].id = LZMA_VLI_UNKNOWN;/* Terminate */

	*strm = lzma_init_data;
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
	if (threads > 1) {
		memset(&mt_options, 0, sizeof(mt_options));
		mt_options.threads = threads;
		mt_options.timeout = 300;
		mt_options.filters = lzmafilters;
		mt_options.check = LZMA_CHECK_CRC64;
		r = lzma_stream_encoder_mt(strm, &mt_options);
	} else
#endif
		r = lzma_stream_encoder(strm, lzmafilters, LZMA_CHECK_CRC64);
	switch (r) {
	case LZMA_OK:
		lastrm->real_stream = strm;
		lastrm->valid = 1;
		lastrm->code = compression_code_lzma;
		lastrm->end = compression_end_lzma;
		r = ARCHIVE_OK;
		break;
	case LZMA_MEM_ERROR:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library: "
		    "Cannot allocate memory");
		r =  ARCHIVE_FATAL;
		break;
        default:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library: "
		    "It's a bug in liblzma");
		r =  ARCHIVE_FATAL;
		break;
	}
	return (r);
}

static int
compression_code_lzma(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	lzma_stream *strm;
	int r;

	strm = (lzma_stream *)lastrm->real_stream;
	strm->next_in = lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in = lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out = lastrm->total_out;
	r = lzma_code(strm,
	    (action == ARCHIVE_Z_FINISH)? LZMA_FINISH: LZMA_RUN);
	lastrm->next_in = strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in = strm->total_in;
	lastrm->next_out = strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out = strm->total_out;
	switch (r) {
	case LZMA_OK:
		/* Non-finishing case */
		return (ARCHIVE_OK);
	case LZMA_STREAM_END:
		/* This return can only occur in finishing case. */
		return (ARCHIVE_EOF);
	case LZMA_MEMLIMIT_ERROR:
		archive_set_error(a, ENOMEM,
		    "lzma compression error:"
		    " %ju MiB would have been needed",
		    (uintmax_t)((lzma_memusage(strm) + 1024 * 1024 -1)
			/ (1024 * 1024)));
		return (ARCHIVE_FATAL);
	default:
		/* Any other return value indicates an error */
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "lzma compression failed:"
		    " lzma_code() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_lzma(struct archive *a, struct la_zstream *lastrm)
{
	lzma_stream *strm;

	(void)a; /* UNUSED */
	strm = (lzma_stream *)lastrm->real_stream;
	lzma_end(strm);
	free(strm);
	lastrm->valid = 0;
	lastrm->real_stream = NULL;
	return (ARCHIVE_OK);
}
#else
static int
compression_init_encoder_lzma(struct archive *a,
    struct la_zstream *lastrm, int level)
{

	(void) level; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "lzma"));
}
static int
compression_init_encoder_xz(struct archive *a,
    struct la_zstream *lastrm, int level, int threads)
{

	(void) level; /* UNUSED */
	(void) threads; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "xz"));
}
#endif

static int
xar_compression_init_encoder(struct archive_write *a)
{
	struct xar *xar;
	int r;

	xar = (struct xar *)a->format_data;
	switch (xar->opt_compression) {
	case GZIP:
		r = compression_init_encoder_gzip(
		    &(a->archive), &(xar->stream),
		    xar->opt_compression_level, 1);
		break;
	case BZIP2:
		r = compression_init_encoder_bzip2(
		    &(a->archive), &(xar->stream),
		    xar->opt_compression_level);
		break;
	case LZMA:
		r = compression_init_encoder_lzma(
		    &(a->archive), &(xar->stream),
		    xar->opt_compression_level);
		break;
	case XZ:
		r = compression_init_encoder_xz(
		    &(a->archive), &(xar->stream),
		    xar->opt_compression_level, xar->opt_threads);
		break;
	default:
		r = ARCHIVE_OK;
		break;
	}
	if (r == ARCHIVE_OK) {
		xar->stream.total_in = 0;
		xar->stream.next_out = xar->wbuff;
		xar->stream.avail_out = sizeof(xar->wbuff);
		xar->stream.total_out = 0;
	}

	return (r);
}

static int
compression_code(struct archive *a, struct la_zstream *lastrm,
    enum la_zaction action)
{
	if (lastrm->valid)
		return (lastrm->code(a, lastrm, action));
	return (ARCHIVE_OK);
}

static int
compression_end(struct archive *a, struct la_zstream *lastrm)
{
	if (lastrm->valid)
		return (lastrm->end(a, lastrm));
	return (ARCHIVE_OK);
}


static int
save_xattrs(struct archive_write *a, struct file *file)
{
	struct xar *xar;
	const char *name;
	const void *value;
	struct heap_data *heap;
	size_t size;
	int count, r;

	xar = (struct xar *)a->format_data;
	count = archive_entry_xattr_reset(file->entry);
	if (count == 0)
		return (ARCHIVE_OK);
	while (count--) {
		archive_entry_xattr_next(file->entry,
		    &name, &value, &size);
		checksum_init(&(xar->a_sumwrk), xar->opt_sumalg);
		checksum_init(&(xar->e_sumwrk), xar->opt_sumalg);

		heap = calloc(1, sizeof(*heap));
		if (heap == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for xattr");
			return (ARCHIVE_FATAL);
		}
		heap->id = file->ea_idx++;
		heap->temp_offset = xar->temp_offset;
		heap->size = size;/* save a extracted size */
		heap->compression = xar->opt_compression;
		/* Get a extracted sumcheck value. */
		checksum_update(&(xar->e_sumwrk), value, size);
		checksum_final(&(xar->e_sumwrk), &(heap->e_sum));

		/*
		 * Not compression to xattr is simple way.
		 */
		if (heap->compression == NONE) {
			checksum_update(&(xar->a_sumwrk), value, size);
			checksum_final(&(xar->a_sumwrk), &(heap->a_sum));
			if (write_to_temp(a, value, size)
			    != ARCHIVE_OK) {
				free(heap);
				return (ARCHIVE_FATAL);
			}
			heap->length = size;
			/* Add heap to the tail of file->xattr. */
			heap->next = NULL;
			*file->xattr.last = heap;
			file->xattr.last = &(heap->next);
			/* Next xattr */
			continue;
		}

		/*
		 * Init compression library.
		 */
		r = xar_compression_init_encoder(a);
		if (r != ARCHIVE_OK) {
			free(heap);
			return (ARCHIVE_FATAL);
		}

		xar->stream.next_in = (const unsigned char *)value;
		xar->stream.avail_in = size;
		for (;;) {
			r = compression_code(&(a->archive),
			    &(xar->stream), ARCHIVE_Z_FINISH);
			if (r != ARCHIVE_OK && r != ARCHIVE_EOF) {
				free(heap);
				return (ARCHIVE_FATAL);
			}
			size = sizeof(xar->wbuff) - xar->stream.avail_out;
			checksum_update(&(xar->a_sumwrk),
			    xar->wbuff, size);
			if (write_to_temp(a, xar->wbuff, size)
			    != ARCHIVE_OK) {
				free(heap);
				return (ARCHIVE_FATAL);
			}
			if (r == ARCHIVE_OK) {
				xar->stream.next_out = xar->wbuff;
				xar->stream.avail_out = sizeof(xar->wbuff);
			} else {
				checksum_final(&(xar->a_sumwrk),
				    &(heap->a_sum));
				heap->length = xar->stream.total_out;
				/* Add heap to the tail of file->xattr. */
				heap->next = NULL;
				*file->xattr.last = heap;
				file->xattr.last = &(heap->next);
				break;
			}
		}
		/* Clean up compression library. */
		r = compression_end(&(a->archive), &(xar->stream));
		if (r != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

static int
getalgsize(enum sumalg sumalg)
{
	switch (sumalg) {
	default:
	case CKSUM_NONE:
		return (0);
	case CKSUM_SHA1:
		return (SHA1_SIZE);
	case CKSUM_MD5:
		return (MD5_SIZE);
	}
}

static const char *
getalgname(enum sumalg sumalg)
{
	switch (sumalg) {
	default:
	case CKSUM_NONE:
		return (NULL);
	case CKSUM_SHA1:
		return (SHA1_NAME);
	case CKSUM_MD5:
		return (MD5_NAME);
	}
}

#endif /* Support xar format */
