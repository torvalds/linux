/*-
 * Copyright (c) 2003-2010 Tim Kientzle
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
 *
 * $FreeBSD$
 */

#ifndef ARCHIVE_H_INCLUDED
#define	ARCHIVE_H_INCLUDED

/*
 * The version number is expressed as a single integer that makes it
 * easy to compare versions at build time: for version a.b.c, the
 * version number is printf("%d%03d%03d",a,b,c).  For example, if you
 * know your application requires version 2.12.108 or later, you can
 * assert that ARCHIVE_VERSION_NUMBER >= 2012108.
 */
/* Note: Compiler will complain if this does not match archive_entry.h! */
#define	ARCHIVE_VERSION_NUMBER 3003003

#include <sys/stat.h>
#include <stddef.h>  /* for wchar_t */
#include <stdio.h> /* For FILE * */
#include <time.h> /* For time_t */

/*
 * Note: archive.h is for use outside of libarchive; the configuration
 * headers (config.h, archive_platform.h, etc.) are purely internal.
 * Do NOT use HAVE_XXX configuration macros to control the behavior of
 * this header!  If you must conditionalize, use predefined compiler and/or
 * platform macros.
 */
#if defined(__BORLANDC__) && __BORLANDC__ >= 0x560
# include <stdint.h>
#elif !defined(__WATCOMC__) && !defined(_MSC_VER) && !defined(__INTERIX) && !defined(__BORLANDC__) && !defined(_SCO_DS) && !defined(__osf__)
# include <inttypes.h>
#endif

/* Get appropriate definitions of 64-bit integer */
#if !defined(__LA_INT64_T_DEFINED)
/* Older code relied on the __LA_INT64_T macro; after 4.0 we'll switch to the typedef exclusively. */
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_INT64_T la_int64_t
# endif
#define __LA_INT64_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
typedef __int64 la_int64_t;
# else
# include <unistd.h>  /* ssize_t */
#  if defined(_SCO_DS) || defined(__osf__)
typedef long long la_int64_t;
#  else
typedef int64_t la_int64_t;
#  endif
# endif
#endif

/* The la_ssize_t should match the type used in 'struct stat' */
#if !defined(__LA_SSIZE_T_DEFINED)
/* Older code relied on the __LA_SSIZE_T macro; after 4.0 we'll switch to the typedef exclusively. */
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_SSIZE_T la_ssize_t
# endif
#define __LA_SSIZE_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
#  if defined(_SSIZE_T_DEFINED) || defined(_SSIZE_T_)
typedef ssize_t la_ssize_t;
#  elif defined(_WIN64)
typedef __int64 la_ssize_t;
#  else
typedef long la_ssize_t;
#  endif
# else
# include <unistd.h>  /* ssize_t */
typedef ssize_t la_ssize_t;
# endif
#endif

/* Large file support for Android */
#ifdef __ANDROID__
#include "android_lf.h"
#endif

/*
 * On Windows, define LIBARCHIVE_STATIC if you're building or using a
 * .lib.  The default here assumes you're building a DLL.  Only
 * libarchive source should ever define __LIBARCHIVE_BUILD.
 */
#if ((defined __WIN32__) || (defined _WIN32) || defined(__CYGWIN__)) && (!defined LIBARCHIVE_STATIC)
# ifdef __LIBARCHIVE_BUILD
#  ifdef __GNUC__
#   define __LA_DECL	__attribute__((dllexport)) extern
#  else
#   define __LA_DECL	__declspec(dllexport)
#  endif
# else
#  ifdef __GNUC__
#   define __LA_DECL
#  else
#   define __LA_DECL	__declspec(dllimport)
#  endif
# endif
#else
/* Static libraries or non-Windows needs no special declaration. */
# define __LA_DECL
#endif

#if defined(__GNUC__) && __GNUC__ >= 3 && !defined(__MINGW32__)
#define	__LA_PRINTF(fmtarg, firstvararg) \
	__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#else
#define	__LA_PRINTF(fmtarg, firstvararg)	/* nothing */
#endif

#if defined(__GNUC__) && __GNUC__ >= 3 && __GNUC_MINOR__ >= 1
# define __LA_DEPRECATED __attribute__((deprecated))
#else
# define __LA_DEPRECATED
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The version number is provided as both a macro and a function.
 * The macro identifies the installed header; the function identifies
 * the library version (which may not be the same if you're using a
 * dynamically-linked version of the library).  Of course, if the
 * header and library are very different, you should expect some
 * strangeness.  Don't do that.
 */
__LA_DECL int		archive_version_number(void);

/*
 * Textual name/version of the library, useful for version displays.
 */
#define	ARCHIVE_VERSION_ONLY_STRING "3.3.3"
#define	ARCHIVE_VERSION_STRING "libarchive " ARCHIVE_VERSION_ONLY_STRING
__LA_DECL const char *	archive_version_string(void);

/*
 * Detailed textual name/version of the library and its dependencies.
 * This has the form:
 *    "libarchive x.y.z zlib/a.b.c liblzma/d.e.f ... etc ..."
 * the list of libraries described here will vary depending on how
 * libarchive was compiled.
 */
__LA_DECL const char *	archive_version_details(void);

/*
 * Returns NULL if libarchive was compiled without the associated library.
 * Otherwise, returns the version number that libarchive was compiled
 * against.
 */
__LA_DECL const char *  archive_zlib_version(void);
__LA_DECL const char *  archive_liblzma_version(void);
__LA_DECL const char *  archive_bzlib_version(void);
__LA_DECL const char *  archive_liblz4_version(void);
__LA_DECL const char *  archive_libzstd_version(void);

/* Declare our basic types. */
struct archive;
struct archive_entry;

/*
 * Error codes: Use archive_errno() and archive_error_string()
 * to retrieve details.  Unless specified otherwise, all functions
 * that return 'int' use these codes.
 */
#define	ARCHIVE_EOF	  1	/* Found end of archive. */
#define	ARCHIVE_OK	  0	/* Operation was successful. */
#define	ARCHIVE_RETRY	(-10)	/* Retry might succeed. */
#define	ARCHIVE_WARN	(-20)	/* Partial success. */
/* For example, if write_header "fails", then you can't push data. */
#define	ARCHIVE_FAILED	(-25)	/* Current operation cannot complete. */
/* But if write_header is "fatal," then this archive is dead and useless. */
#define	ARCHIVE_FATAL	(-30)	/* No more operations are possible. */

/*
 * As far as possible, archive_errno returns standard platform errno codes.
 * Of course, the details vary by platform, so the actual definitions
 * here are stored in "archive_platform.h".  The symbols are listed here
 * for reference; as a rule, clients should not need to know the exact
 * platform-dependent error code.
 */
/* Unrecognized or invalid file format. */
/* #define	ARCHIVE_ERRNO_FILE_FORMAT */
/* Illegal usage of the library. */
/* #define	ARCHIVE_ERRNO_PROGRAMMER_ERROR */
/* Unknown or unclassified error. */
/* #define	ARCHIVE_ERRNO_MISC */

/*
 * Callbacks are invoked to automatically read/skip/write/open/close the
 * archive. You can provide your own for complex tasks (like breaking
 * archives across multiple tapes) or use standard ones built into the
 * library.
 */

/* Returns pointer and size of next block of data from archive. */
typedef la_ssize_t	archive_read_callback(struct archive *,
			    void *_client_data, const void **_buffer);

/* Skips at most request bytes from archive and returns the skipped amount.
 * This may skip fewer bytes than requested; it may even skip zero bytes.
 * If you do skip fewer bytes than requested, libarchive will invoke your
 * read callback and discard data as necessary to make up the full skip.
 */
typedef la_int64_t	archive_skip_callback(struct archive *,
			    void *_client_data, la_int64_t request);

/* Seeks to specified location in the file and returns the position.
 * Whence values are SEEK_SET, SEEK_CUR, SEEK_END from stdio.h.
 * Return ARCHIVE_FATAL if the seek fails for any reason.
 */
typedef la_int64_t	archive_seek_callback(struct archive *,
    void *_client_data, la_int64_t offset, int whence);

/* Returns size actually written, zero on EOF, -1 on error. */
typedef la_ssize_t	archive_write_callback(struct archive *,
			    void *_client_data,
			    const void *_buffer, size_t _length);

typedef int	archive_open_callback(struct archive *, void *_client_data);

typedef int	archive_close_callback(struct archive *, void *_client_data);

/* Switches from one client data object to the next/prev client data object.
 * This is useful for reading from different data blocks such as a set of files
 * that make up one large file.
 */
typedef int archive_switch_callback(struct archive *, void *_client_data1,
			    void *_client_data2);

/*
 * Returns a passphrase used for encryption or decryption, NULL on nothing
 * to do and give it up.
 */
typedef const char *archive_passphrase_callback(struct archive *,
			    void *_client_data);

/*
 * Codes to identify various stream filters.
 */
#define	ARCHIVE_FILTER_NONE	0
#define	ARCHIVE_FILTER_GZIP	1
#define	ARCHIVE_FILTER_BZIP2	2
#define	ARCHIVE_FILTER_COMPRESS	3
#define	ARCHIVE_FILTER_PROGRAM	4
#define	ARCHIVE_FILTER_LZMA	5
#define	ARCHIVE_FILTER_XZ	6
#define	ARCHIVE_FILTER_UU	7
#define	ARCHIVE_FILTER_RPM	8
#define	ARCHIVE_FILTER_LZIP	9
#define	ARCHIVE_FILTER_LRZIP	10
#define	ARCHIVE_FILTER_LZOP	11
#define	ARCHIVE_FILTER_GRZIP	12
#define	ARCHIVE_FILTER_LZ4	13
#define	ARCHIVE_FILTER_ZSTD	14

#if ARCHIVE_VERSION_NUMBER < 4000000
#define	ARCHIVE_COMPRESSION_NONE	ARCHIVE_FILTER_NONE
#define	ARCHIVE_COMPRESSION_GZIP	ARCHIVE_FILTER_GZIP
#define	ARCHIVE_COMPRESSION_BZIP2	ARCHIVE_FILTER_BZIP2
#define	ARCHIVE_COMPRESSION_COMPRESS	ARCHIVE_FILTER_COMPRESS
#define	ARCHIVE_COMPRESSION_PROGRAM	ARCHIVE_FILTER_PROGRAM
#define	ARCHIVE_COMPRESSION_LZMA	ARCHIVE_FILTER_LZMA
#define	ARCHIVE_COMPRESSION_XZ		ARCHIVE_FILTER_XZ
#define	ARCHIVE_COMPRESSION_UU		ARCHIVE_FILTER_UU
#define	ARCHIVE_COMPRESSION_RPM		ARCHIVE_FILTER_RPM
#define	ARCHIVE_COMPRESSION_LZIP	ARCHIVE_FILTER_LZIP
#define	ARCHIVE_COMPRESSION_LRZIP	ARCHIVE_FILTER_LRZIP
#endif

/*
 * Codes returned by archive_format.
 *
 * Top 16 bits identifies the format family (e.g., "tar"); lower
 * 16 bits indicate the variant.  This is updated by read_next_header.
 * Note that the lower 16 bits will often vary from entry to entry.
 * In some cases, this variation occurs as libarchive learns more about
 * the archive (for example, later entries might utilize extensions that
 * weren't necessary earlier in the archive; in this case, libarchive
 * will change the format code to indicate the extended format that
 * was used).  In other cases, it's because different tools have
 * modified the archive and so different parts of the archive
 * actually have slightly different formats.  (Both tar and cpio store
 * format codes in each entry, so it is quite possible for each
 * entry to be in a different format.)
 */
#define	ARCHIVE_FORMAT_BASE_MASK		0xff0000
#define	ARCHIVE_FORMAT_CPIO			0x10000
#define	ARCHIVE_FORMAT_CPIO_POSIX		(ARCHIVE_FORMAT_CPIO | 1)
#define	ARCHIVE_FORMAT_CPIO_BIN_LE		(ARCHIVE_FORMAT_CPIO | 2)
#define	ARCHIVE_FORMAT_CPIO_BIN_BE		(ARCHIVE_FORMAT_CPIO | 3)
#define	ARCHIVE_FORMAT_CPIO_SVR4_NOCRC		(ARCHIVE_FORMAT_CPIO | 4)
#define	ARCHIVE_FORMAT_CPIO_SVR4_CRC		(ARCHIVE_FORMAT_CPIO | 5)
#define	ARCHIVE_FORMAT_CPIO_AFIO_LARGE		(ARCHIVE_FORMAT_CPIO | 6)
#define	ARCHIVE_FORMAT_SHAR			0x20000
#define	ARCHIVE_FORMAT_SHAR_BASE		(ARCHIVE_FORMAT_SHAR | 1)
#define	ARCHIVE_FORMAT_SHAR_DUMP		(ARCHIVE_FORMAT_SHAR | 2)
#define	ARCHIVE_FORMAT_TAR			0x30000
#define	ARCHIVE_FORMAT_TAR_USTAR		(ARCHIVE_FORMAT_TAR | 1)
#define	ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE	(ARCHIVE_FORMAT_TAR | 2)
#define	ARCHIVE_FORMAT_TAR_PAX_RESTRICTED	(ARCHIVE_FORMAT_TAR | 3)
#define	ARCHIVE_FORMAT_TAR_GNUTAR		(ARCHIVE_FORMAT_TAR | 4)
#define	ARCHIVE_FORMAT_ISO9660			0x40000
#define	ARCHIVE_FORMAT_ISO9660_ROCKRIDGE	(ARCHIVE_FORMAT_ISO9660 | 1)
#define	ARCHIVE_FORMAT_ZIP			0x50000
#define	ARCHIVE_FORMAT_EMPTY			0x60000
#define	ARCHIVE_FORMAT_AR			0x70000
#define	ARCHIVE_FORMAT_AR_GNU			(ARCHIVE_FORMAT_AR | 1)
#define	ARCHIVE_FORMAT_AR_BSD			(ARCHIVE_FORMAT_AR | 2)
#define	ARCHIVE_FORMAT_MTREE			0x80000
#define	ARCHIVE_FORMAT_RAW			0x90000
#define	ARCHIVE_FORMAT_XAR			0xA0000
#define	ARCHIVE_FORMAT_LHA			0xB0000
#define	ARCHIVE_FORMAT_CAB			0xC0000
#define	ARCHIVE_FORMAT_RAR			0xD0000
#define	ARCHIVE_FORMAT_RAR_V5			(ARCHIVE_FORMAT_RAR | 1)
#define	ARCHIVE_FORMAT_7ZIP			0xE0000
#define	ARCHIVE_FORMAT_WARC			0xF0000

/*
 * Codes returned by archive_read_format_capabilities().
 *
 * This list can be extended with values between 0 and 0xffff.
 * The original purpose of this list was to let different archive
 * format readers expose their general capabilities in terms of
 * encryption.
 */
#define ARCHIVE_READ_FORMAT_CAPS_NONE (0) /* no special capabilities */
#define ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA (1<<0)  /* reader can detect encrypted data */
#define ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA (1<<1)  /* reader can detect encryptable metadata (pathname, mtime, etc.) */

/*
 * Codes returned by archive_read_has_encrypted_entries().
 *
 * In case the archive does not support encryption detection at all
 * ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED is returned. If the reader
 * for some other reason (e.g. not enough bytes read) cannot say if
 * there are encrypted entries, ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
 * is returned.
 */
#define ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED -2
#define ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW -1

/*-
 * Basic outline for reading an archive:
 *   1) Ask archive_read_new for an archive reader object.
 *   2) Update any global properties as appropriate.
 *      In particular, you'll certainly want to call appropriate
 *      archive_read_support_XXX functions.
 *   3) Call archive_read_open_XXX to open the archive
 *   4) Repeatedly call archive_read_next_header to get information about
 *      successive archive entries.  Call archive_read_data to extract
 *      data for entries of interest.
 *   5) Call archive_read_free to end processing.
 */
__LA_DECL struct archive	*archive_read_new(void);

/*
 * The archive_read_support_XXX calls enable auto-detect for this
 * archive handle.  They also link in the necessary support code.
 * For example, if you don't want bzlib linked in, don't invoke
 * support_compression_bzip2().  The "all" functions provide the
 * obvious shorthand.
 */

#if ARCHIVE_VERSION_NUMBER < 4000000
__LA_DECL int archive_read_support_compression_all(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_bzip2(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_compress(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_gzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_lzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_lzma(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_none(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_program(struct archive *,
		     const char *command) __LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_program_signature
		(struct archive *, const char *,
		 const void * /* match */, size_t) __LA_DEPRECATED;

__LA_DECL int archive_read_support_compression_rpm(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_uu(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_xz(struct archive *)
		__LA_DEPRECATED;
#endif

__LA_DECL int archive_read_support_filter_all(struct archive *);
__LA_DECL int archive_read_support_filter_bzip2(struct archive *);
__LA_DECL int archive_read_support_filter_compress(struct archive *);
__LA_DECL int archive_read_support_filter_gzip(struct archive *);
__LA_DECL int archive_read_support_filter_grzip(struct archive *);
__LA_DECL int archive_read_support_filter_lrzip(struct archive *);
__LA_DECL int archive_read_support_filter_lz4(struct archive *);
__LA_DECL int archive_read_support_filter_lzip(struct archive *);
__LA_DECL int archive_read_support_filter_lzma(struct archive *);
__LA_DECL int archive_read_support_filter_lzop(struct archive *);
__LA_DECL int archive_read_support_filter_none(struct archive *);
__LA_DECL int archive_read_support_filter_program(struct archive *,
		     const char *command);
__LA_DECL int archive_read_support_filter_program_signature
		(struct archive *, const char * /* cmd */,
				    const void * /* match */, size_t);
__LA_DECL int archive_read_support_filter_rpm(struct archive *);
__LA_DECL int archive_read_support_filter_uu(struct archive *);
__LA_DECL int archive_read_support_filter_xz(struct archive *);
__LA_DECL int archive_read_support_filter_zstd(struct archive *);

__LA_DECL int archive_read_support_format_7zip(struct archive *);
__LA_DECL int archive_read_support_format_all(struct archive *);
__LA_DECL int archive_read_support_format_ar(struct archive *);
__LA_DECL int archive_read_support_format_by_code(struct archive *, int);
__LA_DECL int archive_read_support_format_cab(struct archive *);
__LA_DECL int archive_read_support_format_cpio(struct archive *);
__LA_DECL int archive_read_support_format_empty(struct archive *);
__LA_DECL int archive_read_support_format_gnutar(struct archive *);
__LA_DECL int archive_read_support_format_iso9660(struct archive *);
__LA_DECL int archive_read_support_format_lha(struct archive *);
__LA_DECL int archive_read_support_format_mtree(struct archive *);
__LA_DECL int archive_read_support_format_rar(struct archive *);
__LA_DECL int archive_read_support_format_rar5(struct archive *);
__LA_DECL int archive_read_support_format_raw(struct archive *);
__LA_DECL int archive_read_support_format_tar(struct archive *);
__LA_DECL int archive_read_support_format_warc(struct archive *);
__LA_DECL int archive_read_support_format_xar(struct archive *);
/* archive_read_support_format_zip() enables both streamable and seekable
 * zip readers. */
__LA_DECL int archive_read_support_format_zip(struct archive *);
/* Reads Zip archives as stream from beginning to end.  Doesn't
 * correctly handle SFX ZIP files or ZIP archives that have been modified
 * in-place. */
__LA_DECL int archive_read_support_format_zip_streamable(struct archive *);
/* Reads starting from central directory; requires seekable input. */
__LA_DECL int archive_read_support_format_zip_seekable(struct archive *);

/* Functions to manually set the format and filters to be used. This is
 * useful to bypass the bidding process when the format and filters to use
 * is known in advance.
 */
__LA_DECL int archive_read_set_format(struct archive *, int);
__LA_DECL int archive_read_append_filter(struct archive *, int);
__LA_DECL int archive_read_append_filter_program(struct archive *,
    const char *);
__LA_DECL int archive_read_append_filter_program_signature
    (struct archive *, const char *, const void * /* match */, size_t);

/* Set various callbacks. */
__LA_DECL int archive_read_set_open_callback(struct archive *,
    archive_open_callback *);
__LA_DECL int archive_read_set_read_callback(struct archive *,
    archive_read_callback *);
__LA_DECL int archive_read_set_seek_callback(struct archive *,
    archive_seek_callback *);
__LA_DECL int archive_read_set_skip_callback(struct archive *,
    archive_skip_callback *);
__LA_DECL int archive_read_set_close_callback(struct archive *,
    archive_close_callback *);
/* Callback used to switch between one data object to the next */
__LA_DECL int archive_read_set_switch_callback(struct archive *,
    archive_switch_callback *);

/* This sets the first data object. */
__LA_DECL int archive_read_set_callback_data(struct archive *, void *);
/* This sets data object at specified index */
__LA_DECL int archive_read_set_callback_data2(struct archive *, void *,
    unsigned int);
/* This adds a data object at the specified index. */
__LA_DECL int archive_read_add_callback_data(struct archive *, void *,
    unsigned int);
/* This appends a data object to the end of list */
__LA_DECL int archive_read_append_callback_data(struct archive *, void *);
/* This prepends a data object to the beginning of list */
__LA_DECL int archive_read_prepend_callback_data(struct archive *, void *);

/* Opening freezes the callbacks. */
__LA_DECL int archive_read_open1(struct archive *);

/* Convenience wrappers around the above. */
__LA_DECL int archive_read_open(struct archive *, void *_client_data,
		     archive_open_callback *, archive_read_callback *,
		     archive_close_callback *);
__LA_DECL int archive_read_open2(struct archive *, void *_client_data,
		     archive_open_callback *, archive_read_callback *,
		     archive_skip_callback *, archive_close_callback *);

/*
 * A variety of shortcuts that invoke archive_read_open() with
 * canned callbacks suitable for common situations.  The ones that
 * accept a block size handle tape blocking correctly.
 */
/* Use this if you know the filename.  Note: NULL indicates stdin. */
__LA_DECL int archive_read_open_filename(struct archive *,
		     const char *_filename, size_t _block_size);
/* Use this for reading multivolume files by filenames.
 * NOTE: Must be NULL terminated. Sorting is NOT done. */
__LA_DECL int archive_read_open_filenames(struct archive *,
		     const char **_filenames, size_t _block_size);
__LA_DECL int archive_read_open_filename_w(struct archive *,
		     const wchar_t *_filename, size_t _block_size);
/* archive_read_open_file() is a deprecated synonym for ..._open_filename(). */
__LA_DECL int archive_read_open_file(struct archive *,
		     const char *_filename, size_t _block_size) __LA_DEPRECATED;
/* Read an archive that's stored in memory. */
__LA_DECL int archive_read_open_memory(struct archive *,
		     const void * buff, size_t size);
/* A more involved version that is only used for internal testing. */
__LA_DECL int archive_read_open_memory2(struct archive *a, const void *buff,
		     size_t size, size_t read_size);
/* Read an archive that's already open, using the file descriptor. */
__LA_DECL int archive_read_open_fd(struct archive *, int _fd,
		     size_t _block_size);
/* Read an archive that's already open, using a FILE *. */
/* Note: DO NOT use this with tape drives. */
__LA_DECL int archive_read_open_FILE(struct archive *, FILE *_file);

/* Parses and returns next entry header. */
__LA_DECL int archive_read_next_header(struct archive *,
		     struct archive_entry **);

/* Parses and returns next entry header using the archive_entry passed in */
__LA_DECL int archive_read_next_header2(struct archive *,
		     struct archive_entry *);

/*
 * Retrieve the byte offset in UNCOMPRESSED data where last-read
 * header started.
 */
__LA_DECL la_int64_t		 archive_read_header_position(struct archive *);

/*
 * Returns 1 if the archive contains at least one encrypted entry.
 * If the archive format not support encryption at all
 * ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED is returned.
 * If for any other reason (e.g. not enough data read so far)
 * we cannot say whether there are encrypted entries, then
 * ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW is returned.
 * In general, this function will return values below zero when the
 * reader is uncertain or totally incapable of encryption support.
 * When this function returns 0 you can be sure that the reader
 * supports encryption detection but no encrypted entries have
 * been found yet.
 *
 * NOTE: If the metadata/header of an archive is also encrypted, you
 * cannot rely on the number of encrypted entries. That is why this
 * function does not return the number of encrypted entries but#
 * just shows that there are some.
 */
__LA_DECL int	archive_read_has_encrypted_entries(struct archive *);

/*
 * Returns a bitmask of capabilities that are supported by the archive format reader.
 * If the reader has no special capabilities, ARCHIVE_READ_FORMAT_CAPS_NONE is returned.
 */
__LA_DECL int		 archive_read_format_capabilities(struct archive *);

/* Read data from the body of an entry.  Similar to read(2). */
__LA_DECL la_ssize_t		 archive_read_data(struct archive *,
				    void *, size_t);

/* Seek within the body of an entry.  Similar to lseek(2). */
__LA_DECL la_int64_t archive_seek_data(struct archive *, la_int64_t, int);

/*
 * A zero-copy version of archive_read_data that also exposes the file offset
 * of each returned block.  Note that the client has no way to specify
 * the desired size of the block.  The API does guarantee that offsets will
 * be strictly increasing and that returned blocks will not overlap.
 */
__LA_DECL int archive_read_data_block(struct archive *a,
		    const void **buff, size_t *size, la_int64_t *offset);

/*-
 * Some convenience functions that are built on archive_read_data:
 *  'skip': skips entire entry
 *  'into_buffer': writes data into memory buffer that you provide
 *  'into_fd': writes data to specified filedes
 */
__LA_DECL int archive_read_data_skip(struct archive *);
__LA_DECL int archive_read_data_into_fd(struct archive *, int fd);

/*
 * Set read options.
 */
/* Apply option to the format only. */
__LA_DECL int archive_read_set_format_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to the filter only. */
__LA_DECL int archive_read_set_filter_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to both the format and the filter. */
__LA_DECL int archive_read_set_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option string to both the format and the filter. */
__LA_DECL int archive_read_set_options(struct archive *_a,
			    const char *opts);

/*
 * Add a decryption passphrase.
 */
__LA_DECL int archive_read_add_passphrase(struct archive *, const char *);
__LA_DECL int archive_read_set_passphrase_callback(struct archive *,
			    void *client_data, archive_passphrase_callback *);


/*-
 * Convenience function to recreate the current entry (whose header
 * has just been read) on disk.
 *
 * This does quite a bit more than just copy data to disk. It also:
 *  - Creates intermediate directories as required.
 *  - Manages directory permissions:  non-writable directories will
 *    be initially created with write permission enabled; when the
 *    archive is closed, dir permissions are edited to the values specified
 *    in the archive.
 *  - Checks hardlinks:  hardlinks will not be extracted unless the
 *    linked-to file was also extracted within the same session. (TODO)
 */

/* The "flags" argument selects optional behavior, 'OR' the flags you want. */

/* Default: Do not try to set owner/group. */
#define	ARCHIVE_EXTRACT_OWNER			(0x0001)
/* Default: Do obey umask, do not restore SUID/SGID/SVTX bits. */
#define	ARCHIVE_EXTRACT_PERM			(0x0002)
/* Default: Do not restore mtime/atime. */
#define	ARCHIVE_EXTRACT_TIME			(0x0004)
/* Default: Replace existing files. */
#define	ARCHIVE_EXTRACT_NO_OVERWRITE 		(0x0008)
/* Default: Try create first, unlink only if create fails with EEXIST. */
#define	ARCHIVE_EXTRACT_UNLINK			(0x0010)
/* Default: Do not restore ACLs. */
#define	ARCHIVE_EXTRACT_ACL			(0x0020)
/* Default: Do not restore fflags. */
#define	ARCHIVE_EXTRACT_FFLAGS			(0x0040)
/* Default: Do not restore xattrs. */
#define	ARCHIVE_EXTRACT_XATTR 			(0x0080)
/* Default: Do not try to guard against extracts redirected by symlinks. */
/* Note: With ARCHIVE_EXTRACT_UNLINK, will remove any intermediate symlink. */
#define	ARCHIVE_EXTRACT_SECURE_SYMLINKS		(0x0100)
/* Default: Do not reject entries with '..' as path elements. */
#define	ARCHIVE_EXTRACT_SECURE_NODOTDOT		(0x0200)
/* Default: Create parent directories as needed. */
#define	ARCHIVE_EXTRACT_NO_AUTODIR		(0x0400)
/* Default: Overwrite files, even if one on disk is newer. */
#define	ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER	(0x0800)
/* Detect blocks of 0 and write holes instead. */
#define	ARCHIVE_EXTRACT_SPARSE			(0x1000)
/* Default: Do not restore Mac extended metadata. */
/* This has no effect except on Mac OS. */
#define	ARCHIVE_EXTRACT_MAC_METADATA		(0x2000)
/* Default: Use HFS+ compression if it was compressed. */
/* This has no effect except on Mac OS v10.6 or later. */
#define	ARCHIVE_EXTRACT_NO_HFS_COMPRESSION	(0x4000)
/* Default: Do not use HFS+ compression if it was not compressed. */
/* This has no effect except on Mac OS v10.6 or later. */
#define	ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED	(0x8000)
/* Default: Do not reject entries with absolute paths */
#define ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS (0x10000)
/* Default: Do not clear no-change flags when unlinking object */
#define	ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS	(0x20000)

__LA_DECL int archive_read_extract(struct archive *, struct archive_entry *,
		     int flags);
__LA_DECL int archive_read_extract2(struct archive *, struct archive_entry *,
		     struct archive * /* dest */);
__LA_DECL void	 archive_read_extract_set_progress_callback(struct archive *,
		     void (*_progress_func)(void *), void *_user_data);

/* Record the dev/ino of a file that will not be written.  This is
 * generally set to the dev/ino of the archive being read. */
__LA_DECL void		archive_read_extract_set_skip_file(struct archive *,
		     la_int64_t, la_int64_t);

/* Close the file and release most resources. */
__LA_DECL int		 archive_read_close(struct archive *);
/* Release all resources and destroy the object. */
/* Note that archive_read_free will call archive_read_close for you. */
__LA_DECL int		 archive_read_free(struct archive *);
#if ARCHIVE_VERSION_NUMBER < 4000000
/* Synonym for archive_read_free() for backwards compatibility. */
__LA_DECL int		 archive_read_finish(struct archive *) __LA_DEPRECATED;
#endif

/*-
 * To create an archive:
 *   1) Ask archive_write_new for an archive writer object.
 *   2) Set any global properties.  In particular, you should set
 *      the compression and format to use.
 *   3) Call archive_write_open to open the file (most people
 *       will use archive_write_open_file or archive_write_open_fd,
 *       which provide convenient canned I/O callbacks for you).
 *   4) For each entry:
 *      - construct an appropriate struct archive_entry structure
 *      - archive_write_header to write the header
 *      - archive_write_data to write the entry data
 *   5) archive_write_close to close the output
 *   6) archive_write_free to cleanup the writer and release resources
 */
__LA_DECL struct archive	*archive_write_new(void);
__LA_DECL int archive_write_set_bytes_per_block(struct archive *,
		     int bytes_per_block);
__LA_DECL int archive_write_get_bytes_per_block(struct archive *);
/* XXX This is badly misnamed; suggestions appreciated. XXX */
__LA_DECL int archive_write_set_bytes_in_last_block(struct archive *,
		     int bytes_in_last_block);
__LA_DECL int archive_write_get_bytes_in_last_block(struct archive *);

/* The dev/ino of a file that won't be archived.  This is used
 * to avoid recursively adding an archive to itself. */
__LA_DECL int archive_write_set_skip_file(struct archive *,
    la_int64_t, la_int64_t);

#if ARCHIVE_VERSION_NUMBER < 4000000
__LA_DECL int archive_write_set_compression_bzip2(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_compress(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_gzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_lzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_lzma(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_none(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_program(struct archive *,
		     const char *cmd) __LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_xz(struct archive *)
		__LA_DEPRECATED;
#endif

/* A convenience function to set the filter based on the code. */
__LA_DECL int archive_write_add_filter(struct archive *, int filter_code);
__LA_DECL int archive_write_add_filter_by_name(struct archive *,
		     const char *name);
__LA_DECL int archive_write_add_filter_b64encode(struct archive *);
__LA_DECL int archive_write_add_filter_bzip2(struct archive *);
__LA_DECL int archive_write_add_filter_compress(struct archive *);
__LA_DECL int archive_write_add_filter_grzip(struct archive *);
__LA_DECL int archive_write_add_filter_gzip(struct archive *);
__LA_DECL int archive_write_add_filter_lrzip(struct archive *);
__LA_DECL int archive_write_add_filter_lz4(struct archive *);
__LA_DECL int archive_write_add_filter_lzip(struct archive *);
__LA_DECL int archive_write_add_filter_lzma(struct archive *);
__LA_DECL int archive_write_add_filter_lzop(struct archive *);
__LA_DECL int archive_write_add_filter_none(struct archive *);
__LA_DECL int archive_write_add_filter_program(struct archive *,
		     const char *cmd);
__LA_DECL int archive_write_add_filter_uuencode(struct archive *);
__LA_DECL int archive_write_add_filter_xz(struct archive *);
__LA_DECL int archive_write_add_filter_zstd(struct archive *);


/* A convenience function to set the format based on the code or name. */
__LA_DECL int archive_write_set_format(struct archive *, int format_code);
__LA_DECL int archive_write_set_format_by_name(struct archive *,
		     const char *name);
/* To minimize link pollution, use one or more of the following. */
__LA_DECL int archive_write_set_format_7zip(struct archive *);
__LA_DECL int archive_write_set_format_ar_bsd(struct archive *);
__LA_DECL int archive_write_set_format_ar_svr4(struct archive *);
__LA_DECL int archive_write_set_format_cpio(struct archive *);
__LA_DECL int archive_write_set_format_cpio_newc(struct archive *);
__LA_DECL int archive_write_set_format_gnutar(struct archive *);
__LA_DECL int archive_write_set_format_iso9660(struct archive *);
__LA_DECL int archive_write_set_format_mtree(struct archive *);
__LA_DECL int archive_write_set_format_mtree_classic(struct archive *);
/* TODO: int archive_write_set_format_old_tar(struct archive *); */
__LA_DECL int archive_write_set_format_pax(struct archive *);
__LA_DECL int archive_write_set_format_pax_restricted(struct archive *);
__LA_DECL int archive_write_set_format_raw(struct archive *);
__LA_DECL int archive_write_set_format_shar(struct archive *);
__LA_DECL int archive_write_set_format_shar_dump(struct archive *);
__LA_DECL int archive_write_set_format_ustar(struct archive *);
__LA_DECL int archive_write_set_format_v7tar(struct archive *);
__LA_DECL int archive_write_set_format_warc(struct archive *);
__LA_DECL int archive_write_set_format_xar(struct archive *);
__LA_DECL int archive_write_set_format_zip(struct archive *);
__LA_DECL int archive_write_set_format_filter_by_ext(struct archive *a, const char *filename);
__LA_DECL int archive_write_set_format_filter_by_ext_def(struct archive *a, const char *filename, const char * def_ext);
__LA_DECL int archive_write_zip_set_compression_deflate(struct archive *);
__LA_DECL int archive_write_zip_set_compression_store(struct archive *);
__LA_DECL int archive_write_open(struct archive *, void *,
		     archive_open_callback *, archive_write_callback *,
		     archive_close_callback *);
__LA_DECL int archive_write_open_fd(struct archive *, int _fd);
__LA_DECL int archive_write_open_filename(struct archive *, const char *_file);
__LA_DECL int archive_write_open_filename_w(struct archive *,
		     const wchar_t *_file);
/* A deprecated synonym for archive_write_open_filename() */
__LA_DECL int archive_write_open_file(struct archive *, const char *_file)
		__LA_DEPRECATED;
__LA_DECL int archive_write_open_FILE(struct archive *, FILE *);
/* _buffSize is the size of the buffer, _used refers to a variable that
 * will be updated after each write into the buffer. */
__LA_DECL int archive_write_open_memory(struct archive *,
			void *_buffer, size_t _buffSize, size_t *_used);

/*
 * Note that the library will truncate writes beyond the size provided
 * to archive_write_header or pad if the provided data is short.
 */
__LA_DECL int archive_write_header(struct archive *,
		     struct archive_entry *);
__LA_DECL la_ssize_t	archive_write_data(struct archive *,
			    const void *, size_t);

/* This interface is currently only available for archive_write_disk handles.  */
__LA_DECL la_ssize_t	 archive_write_data_block(struct archive *,
				    const void *, size_t, la_int64_t);

__LA_DECL int		 archive_write_finish_entry(struct archive *);
__LA_DECL int		 archive_write_close(struct archive *);
/* Marks the archive as FATAL so that a subsequent free() operation
 * won't try to close() cleanly.  Provides a fast abort capability
 * when the client discovers that things have gone wrong. */
__LA_DECL int            archive_write_fail(struct archive *);
/* This can fail if the archive wasn't already closed, in which case
 * archive_write_free() will implicitly call archive_write_close(). */
__LA_DECL int		 archive_write_free(struct archive *);
#if ARCHIVE_VERSION_NUMBER < 4000000
/* Synonym for archive_write_free() for backwards compatibility. */
__LA_DECL int		 archive_write_finish(struct archive *) __LA_DEPRECATED;
#endif

/*
 * Set write options.
 */
/* Apply option to the format only. */
__LA_DECL int archive_write_set_format_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to the filter only. */
__LA_DECL int archive_write_set_filter_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to both the format and the filter. */
__LA_DECL int archive_write_set_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option string to both the format and the filter. */
__LA_DECL int archive_write_set_options(struct archive *_a,
			    const char *opts);

/*
 * Set a encryption passphrase.
 */
__LA_DECL int archive_write_set_passphrase(struct archive *_a, const char *p);
__LA_DECL int archive_write_set_passphrase_callback(struct archive *,
			    void *client_data, archive_passphrase_callback *);

/*-
 * ARCHIVE_WRITE_DISK API
 *
 * To create objects on disk:
 *   1) Ask archive_write_disk_new for a new archive_write_disk object.
 *   2) Set any global properties.  In particular, you probably
 *      want to set the options.
 *   3) For each entry:
 *      - construct an appropriate struct archive_entry structure
 *      - archive_write_header to create the file/dir/etc on disk
 *      - archive_write_data to write the entry data
 *   4) archive_write_free to cleanup the writer and release resources
 *
 * In particular, you can use this in conjunction with archive_read()
 * to pull entries out of an archive and create them on disk.
 */
__LA_DECL struct archive	*archive_write_disk_new(void);
/* This file will not be overwritten. */
__LA_DECL int archive_write_disk_set_skip_file(struct archive *,
    la_int64_t, la_int64_t);
/* Set flags to control how the next item gets created.
 * This accepts a bitmask of ARCHIVE_EXTRACT_XXX flags defined above. */
__LA_DECL int		 archive_write_disk_set_options(struct archive *,
		     int flags);
/*
 * The lookup functions are given uname/uid (or gname/gid) pairs and
 * return a uid (gid) suitable for this system.  These are used for
 * restoring ownership and for setting ACLs.  The default functions
 * are naive, they just return the uid/gid.  These are small, so reasonable
 * for applications that don't need to preserve ownership; they
 * are probably also appropriate for applications that are doing
 * same-system backup and restore.
 */
/*
 * The "standard" lookup functions use common system calls to lookup
 * the uname/gname, falling back to the uid/gid if the names can't be
 * found.  They cache lookups and are reasonably fast, but can be very
 * large, so they are not used unless you ask for them.  In
 * particular, these match the specifications of POSIX "pax" and old
 * POSIX "tar".
 */
__LA_DECL int	 archive_write_disk_set_standard_lookup(struct archive *);
/*
 * If neither the default (naive) nor the standard (big) functions suit
 * your needs, you can write your own and register them.  Be sure to
 * include a cleanup function if you have allocated private data.
 */
__LA_DECL int archive_write_disk_set_group_lookup(struct archive *,
    void * /* private_data */,
    la_int64_t (*)(void *, const char *, la_int64_t),
    void (* /* cleanup */)(void *));
__LA_DECL int archive_write_disk_set_user_lookup(struct archive *,
    void * /* private_data */,
    la_int64_t (*)(void *, const char *, la_int64_t),
    void (* /* cleanup */)(void *));
__LA_DECL la_int64_t archive_write_disk_gid(struct archive *, const char *, la_int64_t);
__LA_DECL la_int64_t archive_write_disk_uid(struct archive *, const char *, la_int64_t);

/*
 * ARCHIVE_READ_DISK API
 *
 * This is still evolving and somewhat experimental.
 */
__LA_DECL struct archive *archive_read_disk_new(void);
/* The names for symlink modes here correspond to an old BSD
 * command-line argument convention: -L, -P, -H */
/* Follow all symlinks. */
__LA_DECL int archive_read_disk_set_symlink_logical(struct archive *);
/* Follow no symlinks. */
__LA_DECL int archive_read_disk_set_symlink_physical(struct archive *);
/* Follow symlink initially, then not. */
__LA_DECL int archive_read_disk_set_symlink_hybrid(struct archive *);
/* TODO: Handle Linux stat32/stat64 ugliness. <sigh> */
__LA_DECL int archive_read_disk_entry_from_file(struct archive *,
    struct archive_entry *, int /* fd */, const struct stat *);
/* Look up gname for gid or uname for uid. */
/* Default implementations are very, very stupid. */
__LA_DECL const char *archive_read_disk_gname(struct archive *, la_int64_t);
__LA_DECL const char *archive_read_disk_uname(struct archive *, la_int64_t);
/* "Standard" implementation uses getpwuid_r, getgrgid_r and caches the
 * results for performance. */
__LA_DECL int	archive_read_disk_set_standard_lookup(struct archive *);
/* You can install your own lookups if you like. */
__LA_DECL int	archive_read_disk_set_gname_lookup(struct archive *,
    void * /* private_data */,
    const char *(* /* lookup_fn */)(void *, la_int64_t),
    void (* /* cleanup_fn */)(void *));
__LA_DECL int	archive_read_disk_set_uname_lookup(struct archive *,
    void * /* private_data */,
    const char *(* /* lookup_fn */)(void *, la_int64_t),
    void (* /* cleanup_fn */)(void *));
/* Start traversal. */
__LA_DECL int	archive_read_disk_open(struct archive *, const char *);
__LA_DECL int	archive_read_disk_open_w(struct archive *, const wchar_t *);
/*
 * Request that current entry be visited.  If you invoke it on every
 * directory, you'll get a physical traversal.  This is ignored if the
 * current entry isn't a directory or a link to a directory.  So, if
 * you invoke this on every returned path, you'll get a full logical
 * traversal.
 */
__LA_DECL int	archive_read_disk_descend(struct archive *);
__LA_DECL int	archive_read_disk_can_descend(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem_is_synthetic(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem_is_remote(struct archive *);
/* Request that the access time of the entry visited by traversal be restored. */
__LA_DECL int  archive_read_disk_set_atime_restored(struct archive *);
/*
 * Set behavior. The "flags" argument selects optional behavior.
 */
/* Request that the access time of the entry visited by traversal be restored.
 * This is the same as archive_read_disk_set_atime_restored. */
#define	ARCHIVE_READDISK_RESTORE_ATIME		(0x0001)
/* Default: Do not skip an entry which has nodump flags. */
#define	ARCHIVE_READDISK_HONOR_NODUMP		(0x0002)
/* Default: Skip a mac resource fork file whose prefix is "._" because of
 * using copyfile. */
#define	ARCHIVE_READDISK_MAC_COPYFILE		(0x0004)
/* Default: Traverse mount points. */
#define	ARCHIVE_READDISK_NO_TRAVERSE_MOUNTS	(0x0008)
/* Default: Xattrs are read from disk. */
#define	ARCHIVE_READDISK_NO_XATTR		(0x0010)
/* Default: ACLs are read from disk. */
#define	ARCHIVE_READDISK_NO_ACL			(0x0020)
/* Default: File flags are read from disk. */
#define	ARCHIVE_READDISK_NO_FFLAGS		(0x0040)

__LA_DECL int  archive_read_disk_set_behavior(struct archive *,
		    int flags);

/*
 * Set archive_match object that will be used in archive_read_disk to
 * know whether an entry should be skipped. The callback function
 * _excluded_func will be invoked when an entry is skipped by the result
 * of archive_match.
 */
__LA_DECL int	archive_read_disk_set_matching(struct archive *,
		    struct archive *_matching, void (*_excluded_func)
		    (struct archive *, void *, struct archive_entry *),
		    void *_client_data);
__LA_DECL int	archive_read_disk_set_metadata_filter_callback(struct archive *,
		    int (*_metadata_filter_func)(struct archive *, void *,
		    	struct archive_entry *), void *_client_data);

/* Simplified cleanup interface;
 * This calls archive_read_free() or archive_write_free() as needed. */
__LA_DECL int	archive_free(struct archive *);

/*
 * Accessor functions to read/set various information in
 * the struct archive object:
 */

/* Number of filters in the current filter pipeline. */
/* Filter #0 is the one closest to the format, -1 is a synonym for the
 * last filter, which is always the pseudo-filter that wraps the
 * client callbacks. */
__LA_DECL int		 archive_filter_count(struct archive *);
__LA_DECL la_int64_t	 archive_filter_bytes(struct archive *, int);
__LA_DECL int		 archive_filter_code(struct archive *, int);
__LA_DECL const char *	 archive_filter_name(struct archive *, int);

#if ARCHIVE_VERSION_NUMBER < 4000000
/* These don't properly handle multiple filters, so are deprecated and
 * will eventually be removed. */
/* As of libarchive 3.0, this is an alias for archive_filter_bytes(a, -1); */
__LA_DECL la_int64_t	 archive_position_compressed(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_bytes(a, 0); */
__LA_DECL la_int64_t	 archive_position_uncompressed(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_name(a, 0); */
__LA_DECL const char	*archive_compression_name(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_code(a, 0); */
__LA_DECL int		 archive_compression(struct archive *)
				__LA_DEPRECATED;
#endif

__LA_DECL int		 archive_errno(struct archive *);
__LA_DECL const char	*archive_error_string(struct archive *);
__LA_DECL const char	*archive_format_name(struct archive *);
__LA_DECL int		 archive_format(struct archive *);
__LA_DECL void		 archive_clear_error(struct archive *);
__LA_DECL void		 archive_set_error(struct archive *, int _err,
			    const char *fmt, ...) __LA_PRINTF(3, 4);
__LA_DECL void		 archive_copy_error(struct archive *dest,
			    struct archive *src);
__LA_DECL int		 archive_file_count(struct archive *);

/*
 * ARCHIVE_MATCH API
 */
__LA_DECL struct archive *archive_match_new(void);
__LA_DECL int	archive_match_free(struct archive *);

/*
 * Test if archive_entry is excluded.
 * This is a convenience function. This is the same as calling all
 * archive_match_path_excluded, archive_match_time_excluded
 * and archive_match_owner_excluded.
 */
__LA_DECL int	archive_match_excluded(struct archive *,
		    struct archive_entry *);

/*
 * Test if pathname is excluded. The conditions are set by following functions.
 */
__LA_DECL int	archive_match_path_excluded(struct archive *,
		    struct archive_entry *);
/* Add exclusion pathname pattern. */
__LA_DECL int	archive_match_exclude_pattern(struct archive *, const char *);
__LA_DECL int	archive_match_exclude_pattern_w(struct archive *,
		    const wchar_t *);
/* Add exclusion pathname pattern from file. */
__LA_DECL int	archive_match_exclude_pattern_from_file(struct archive *,
		    const char *, int _nullSeparator);
__LA_DECL int	archive_match_exclude_pattern_from_file_w(struct archive *,
		    const wchar_t *, int _nullSeparator);
/* Add inclusion pathname pattern. */
__LA_DECL int	archive_match_include_pattern(struct archive *, const char *);
__LA_DECL int	archive_match_include_pattern_w(struct archive *,
		    const wchar_t *);
/* Add inclusion pathname pattern from file. */
__LA_DECL int	archive_match_include_pattern_from_file(struct archive *,
		    const char *, int _nullSeparator);
__LA_DECL int	archive_match_include_pattern_from_file_w(struct archive *,
		    const wchar_t *, int _nullSeparator);
/*
 * How to get statistic information for inclusion patterns.
 */
/* Return the amount number of unmatched inclusion patterns. */
__LA_DECL int	archive_match_path_unmatched_inclusions(struct archive *);
/* Return the pattern of unmatched inclusion with ARCHIVE_OK.
 * Return ARCHIVE_EOF if there is no inclusion pattern. */
__LA_DECL int	archive_match_path_unmatched_inclusions_next(
		    struct archive *, const char **);
__LA_DECL int	archive_match_path_unmatched_inclusions_next_w(
		    struct archive *, const wchar_t **);

/*
 * Test if a file is excluded by its time stamp.
 * The conditions are set by following functions.
 */
__LA_DECL int	archive_match_time_excluded(struct archive *,
		    struct archive_entry *);

/*
 * Flags to tell a matching type of time stamps. These are used for
 * following functions.
 */
/* Time flag: mtime to be tested. */
#define ARCHIVE_MATCH_MTIME	(0x0100)
/* Time flag: ctime to be tested. */
#define ARCHIVE_MATCH_CTIME	(0x0200)
/* Comparison flag: Match the time if it is newer than. */
#define ARCHIVE_MATCH_NEWER	(0x0001)
/* Comparison flag: Match the time if it is older than. */
#define ARCHIVE_MATCH_OLDER	(0x0002)
/* Comparison flag: Match the time if it is equal to. */
#define ARCHIVE_MATCH_EQUAL	(0x0010)
/* Set inclusion time. */
__LA_DECL int	archive_match_include_time(struct archive *, int _flag,
		    time_t _sec, long _nsec);
/* Set inclusion time by a date string. */
__LA_DECL int	archive_match_include_date(struct archive *, int _flag,
		    const char *_datestr);
__LA_DECL int	archive_match_include_date_w(struct archive *, int _flag,
		    const wchar_t *_datestr);
/* Set inclusion time by a particular file. */
__LA_DECL int	archive_match_include_file_time(struct archive *,
		    int _flag, const char *_pathname);
__LA_DECL int	archive_match_include_file_time_w(struct archive *,
		    int _flag, const wchar_t *_pathname);
/* Add exclusion entry. */
__LA_DECL int	archive_match_exclude_entry(struct archive *,
		    int _flag, struct archive_entry *);

/*
 * Test if a file is excluded by its uid ,gid, uname or gname.
 * The conditions are set by following functions.
 */
__LA_DECL int	archive_match_owner_excluded(struct archive *,
		    struct archive_entry *);
/* Add inclusion uid, gid, uname and gname. */
__LA_DECL int	archive_match_include_uid(struct archive *, la_int64_t);
__LA_DECL int	archive_match_include_gid(struct archive *, la_int64_t);
__LA_DECL int	archive_match_include_uname(struct archive *, const char *);
__LA_DECL int	archive_match_include_uname_w(struct archive *,
		    const wchar_t *);
__LA_DECL int	archive_match_include_gname(struct archive *, const char *);
__LA_DECL int	archive_match_include_gname_w(struct archive *,
		    const wchar_t *);

/* Utility functions */
/* Convenience function to sort a NULL terminated list of strings */
__LA_DECL int archive_utility_string_sort(char **);

#ifdef __cplusplus
}
#endif

/* These are meaningless outside of this header. */
#undef __LA_DECL

#endif /* !ARCHIVE_H_INCLUDED */
