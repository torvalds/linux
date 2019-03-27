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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_read_private.h"
#include "archive_endian.h"


struct lzx_dec {
	/* Decoding status. */
	int     		 state;

	/*
	 * Window to see last decoded data, from 32KBi to 2MBi.
	 */
	int			 w_size;
	int			 w_mask;
	/* Window buffer, which is a loop buffer. */
	unsigned char		*w_buff;
	/* The insert position to the window. */
	int			 w_pos;
	/* The position where we can copy decoded code from the window. */
	int     		 copy_pos;
	/* The length how many bytes we can copy decoded code from
	 * the window. */
	int     		 copy_len;
	/* Translation reversal for x86 processor CALL byte sequence(E8).
	 * This is used for LZX only. */
	uint32_t		 translation_size;
	char			 translation;
	char			 block_type;
#define VERBATIM_BLOCK		1
#define ALIGNED_OFFSET_BLOCK	2
#define UNCOMPRESSED_BLOCK	3
	size_t			 block_size;
	size_t			 block_bytes_avail;
	/* Repeated offset. */
	int			 r0, r1, r2;
	unsigned char		 rbytes[4];
	int			 rbytes_avail;
	int			 length_header;
	int			 position_slot;
	int			 offset_bits;

	struct lzx_pos_tbl {
		int		 base;
		int		 footer_bits;
	}			*pos_tbl;
	/*
	 * Bit stream reader.
	 */
	struct lzx_br {
#define CACHE_TYPE		uint64_t
#define CACHE_BITS		(8 * sizeof(CACHE_TYPE))
	 	/* Cache buffer. */
		CACHE_TYPE	 cache_buffer;
		/* Indicates how many bits avail in cache_buffer. */
		int		 cache_avail;
		unsigned char	 odd;
		char		 have_odd;
	} br;

	/*
	 * Huffman coding.
	 */
	struct huffman {
		int		 len_size;
		int		 freq[17];
		unsigned char	*bitlen;

		/*
		 * Use a index table. It's faster than searching a huffman
		 * coding tree, which is a binary tree. But a use of a large
		 * index table causes L1 cache read miss many times.
		 */
		int		 max_bits;
		int		 tbl_bits;
		int		 tree_used;
		/* Direct access table. */
		uint16_t	*tbl;
	}			 at, lt, mt, pt;

	int			 loop;
	int			 error;
};

static const int slots[] = {
	30, 32, 34, 36, 38, 42, 50, 66, 98, 162, 290
};
#define SLOT_BASE	15
#define SLOT_MAX	21/*->25*/

struct lzx_stream {
	const unsigned char	*next_in;
	int64_t			 avail_in;
	int64_t			 total_in;
	unsigned char		*next_out;
	int64_t			 avail_out;
	int64_t			 total_out;
	struct lzx_dec		*ds;
};

/*
 * Cabinet file definitions.
 */
/* CFHEADER offset */
#define CFHEADER_signature	0
#define CFHEADER_cbCabinet	8
#define CFHEADER_coffFiles	16
#define CFHEADER_versionMinor	24
#define CFHEADER_versionMajor	25
#define CFHEADER_cFolders	26
#define CFHEADER_cFiles		28
#define CFHEADER_flags		30
#define CFHEADER_setID		32
#define CFHEADER_iCabinet	34
#define CFHEADER_cbCFHeader	36
#define CFHEADER_cbCFFolder	38
#define CFHEADER_cbCFData	39

/* CFFOLDER offset */
#define CFFOLDER_coffCabStart	0
#define CFFOLDER_cCFData	4
#define CFFOLDER_typeCompress	6
#define CFFOLDER_abReserve	8

/* CFFILE offset */
#define CFFILE_cbFile		0
#define CFFILE_uoffFolderStart	4
#define CFFILE_iFolder		8
#define CFFILE_date_time	10
#define CFFILE_attribs		14

/* CFDATA offset */
#define CFDATA_csum		0
#define CFDATA_cbData		4
#define CFDATA_cbUncomp		6

static const char * const compression_name[] = {
	"NONE",
	"MSZIP",
	"Quantum",
	"LZX",
};

struct cfdata {
	/* Sum value of this CFDATA. */
	uint32_t		 sum;
	uint16_t		 compressed_size;
	uint16_t		 compressed_bytes_remaining;
	uint16_t		 uncompressed_size;
	uint16_t		 uncompressed_bytes_remaining;
	/* To know how many bytes we have decompressed. */
	uint16_t		 uncompressed_avail;
	/* Offset from the beginning of compressed data of this CFDATA */
	uint16_t		 read_offset;
	int64_t			 unconsumed;
	/* To keep memory image of this CFDATA to compute the sum. */
	size_t			 memimage_size;
	unsigned char		*memimage;
	/* Result of calculation of sum. */
	uint32_t		 sum_calculated;
	unsigned char		 sum_extra[4];
	int			 sum_extra_avail;
	const void		*sum_ptr;
};

struct cffolder {
	uint32_t		 cfdata_offset_in_cab;
	uint16_t		 cfdata_count;
	uint16_t		 comptype;
#define COMPTYPE_NONE		0x0000
#define COMPTYPE_MSZIP		0x0001
#define COMPTYPE_QUANTUM	0x0002
#define COMPTYPE_LZX		0x0003
	uint16_t		 compdata;
	const char		*compname;
	/* At the time reading CFDATA */
	struct cfdata		 cfdata;
	int			 cfdata_index;
	/* Flags to mark progress of decompression. */
	char			 decompress_init;
};

struct cffile {
	uint32_t		 uncompressed_size;
	uint32_t		 offset;
	time_t			 mtime;
	uint16_t	 	 folder;
#define iFoldCONTINUED_FROM_PREV	0xFFFD
#define iFoldCONTINUED_TO_NEXT		0xFFFE
#define iFoldCONTINUED_PREV_AND_NEXT	0xFFFF
	unsigned char		 attr;
#define ATTR_RDONLY		0x01
#define ATTR_NAME_IS_UTF	0x80
	struct archive_string 	 pathname;
};

struct cfheader {
	/* Total bytes of all file size in a Cabinet. */
	uint32_t		 total_bytes;
	uint32_t		 files_offset;
	uint16_t		 folder_count;
	uint16_t		 file_count;
	uint16_t		 flags;
#define PREV_CABINET	0x0001
#define NEXT_CABINET	0x0002
#define RESERVE_PRESENT	0x0004
	uint16_t		 setid;
	uint16_t		 cabinet;
	/* Version number. */
	unsigned char		 major;
	unsigned char		 minor;
	unsigned char		 cffolder;
	unsigned char		 cfdata;
	/* All folders in a cabinet. */
	struct cffolder		*folder_array;
	/* All files in a cabinet. */
	struct cffile		*file_array;
	int			 file_index;
};

struct cab {
	/* entry_bytes_remaining is the number of bytes we expect.	    */
	int64_t			 entry_offset;
	int64_t			 entry_bytes_remaining;
	int64_t			 entry_unconsumed;
	int64_t			 entry_compressed_bytes_read;
	int64_t			 entry_uncompressed_bytes_read;
	struct cffolder		*entry_cffolder;
	struct cffile		*entry_cffile;
	struct cfdata		*entry_cfdata;

	/* Offset from beginning of a cabinet file. */
	int64_t			 cab_offset;
	struct cfheader		 cfheader;
	struct archive_wstring	 ws;

	/* Flag to mark progress that an archive was read their first header.*/
	char			 found_header;
	char			 end_of_archive;
	char			 end_of_entry;
	char			 end_of_entry_cleanup;
	char			 read_data_invoked;
	int64_t			 bytes_skipped;

	unsigned char		*uncompressed_buffer;
	size_t			 uncompressed_buffer_size;

	int			 init_default_conversion;
	struct archive_string_conv *sconv;
	struct archive_string_conv *sconv_default;
	struct archive_string_conv *sconv_utf8;
	char			 format_name[64];

#ifdef HAVE_ZLIB_H
	z_stream		 stream;
	char			 stream_valid;
#endif
	struct lzx_stream	 xstrm;
};

static int	archive_read_format_cab_bid(struct archive_read *, int);
static int	archive_read_format_cab_options(struct archive_read *,
		    const char *, const char *);
static int	archive_read_format_cab_read_header(struct archive_read *,
		    struct archive_entry *);
static int	archive_read_format_cab_read_data(struct archive_read *,
		    const void **, size_t *, int64_t *);
static int	archive_read_format_cab_read_data_skip(struct archive_read *);
static int	archive_read_format_cab_cleanup(struct archive_read *);

static int	cab_skip_sfx(struct archive_read *);
static time_t	cab_dos_time(const unsigned char *);
static int	cab_read_data(struct archive_read *, const void **,
		    size_t *, int64_t *);
static int	cab_read_header(struct archive_read *);
static uint32_t cab_checksum_cfdata_4(const void *, size_t bytes, uint32_t);
static uint32_t cab_checksum_cfdata(const void *, size_t bytes, uint32_t);
static void	cab_checksum_update(struct archive_read *, size_t);
static int	cab_checksum_finish(struct archive_read *);
static int	cab_next_cfdata(struct archive_read *);
static const void *cab_read_ahead_cfdata(struct archive_read *, ssize_t *);
static const void *cab_read_ahead_cfdata_none(struct archive_read *, ssize_t *);
static const void *cab_read_ahead_cfdata_deflate(struct archive_read *,
		    ssize_t *);
static const void *cab_read_ahead_cfdata_lzx(struct archive_read *,
		    ssize_t *);
static int64_t	cab_consume_cfdata(struct archive_read *, int64_t);
static int64_t	cab_minimum_consume_cfdata(struct archive_read *, int64_t);
static int	lzx_decode_init(struct lzx_stream *, int);
static int	lzx_read_blocks(struct lzx_stream *, int);
static int	lzx_decode_blocks(struct lzx_stream *, int);
static void	lzx_decode_free(struct lzx_stream *);
static void	lzx_translation(struct lzx_stream *, void *, size_t, uint32_t);
static void	lzx_cleanup_bitstream(struct lzx_stream *);
static int	lzx_decode(struct lzx_stream *, int);
static int	lzx_read_pre_tree(struct lzx_stream *);
static int	lzx_read_bitlen(struct lzx_stream *, struct huffman *, int);
static int	lzx_huffman_init(struct huffman *, size_t, int);
static void	lzx_huffman_free(struct huffman *);
static int	lzx_make_huffman_table(struct huffman *);
static inline int lzx_decode_huffman(struct huffman *, unsigned);


int
archive_read_support_format_cab(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct cab *cab;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_cab");

	cab = (struct cab *)calloc(1, sizeof(*cab));
	if (cab == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate CAB data");
		return (ARCHIVE_FATAL);
	}
	archive_string_init(&cab->ws);
	archive_wstring_ensure(&cab->ws, 256);

	r = __archive_read_register_format(a,
	    cab,
	    "cab",
	    archive_read_format_cab_bid,
	    archive_read_format_cab_options,
	    archive_read_format_cab_read_header,
	    archive_read_format_cab_read_data,
	    archive_read_format_cab_read_data_skip,
	    NULL,
	    archive_read_format_cab_cleanup,
	    NULL,
	    NULL);

	if (r != ARCHIVE_OK)
		free(cab);
	return (ARCHIVE_OK);
}

static int
find_cab_magic(const char *p)
{
	switch (p[4]) {
	case 0:
		/*
		 * Note: Self-Extraction program has 'MSCF' string in their
		 * program. If we were finding 'MSCF' string only, we got
		 * wrong place for Cabinet header, thus, we have to check
		 * following four bytes which are reserved and must be set
		 * to zero.
		 */
		if (memcmp(p, "MSCF\0\0\0\0", 8) == 0)
			return 0;
		return 5;
	case 'F': return 1;
	case 'C': return 2;
	case 'S': return 3;
	case 'M': return 4;
	default:  return 5;
	}
}

static int
archive_read_format_cab_bid(struct archive_read *a, int best_bid)
{
	const char *p;
	ssize_t bytes_avail, offset, window;

	/* If there's already a better bid than we can ever
	   make, don't bother testing. */
	if (best_bid > 64)
		return (-1);

	if ((p = __archive_read_ahead(a, 8, NULL)) == NULL)
		return (-1);

	if (memcmp(p, "MSCF\0\0\0\0", 8) == 0)
		return (64);

	/*
	 * Attempt to handle self-extracting archives
	 * by noting a PE header and searching forward
	 * up to 128k for a 'MSCF' marker.
	 */
	if (p[0] == 'M' && p[1] == 'Z') {
		offset = 0;
		window = 4096;
		while (offset < (1024 * 128)) {
			const char *h = __archive_read_ahead(a, offset + window,
			    &bytes_avail);
			if (h == NULL) {
				/* Remaining bytes are less than window. */
				window >>= 1;
				if (window < 128)
					return (0);
				continue;
			}
			p = h + offset;
			while (p + 8 < h + bytes_avail) {
				int next;
				if ((next = find_cab_magic(p)) == 0)
					return (64);
				p += next;
			}
			offset = p - h;
		}
	}
	return (0);
}

static int
archive_read_format_cab_options(struct archive_read *a,
    const char *key, const char *val)
{
	struct cab *cab;
	int ret = ARCHIVE_FAILED;

	cab = (struct cab *)(a->format->data);
	if (strcmp(key, "hdrcharset")  == 0) {
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "cab: hdrcharset option needs a character-set name");
		else {
			cab->sconv = archive_string_conversion_from_charset(
			    &a->archive, val, 0);
			if (cab->sconv != NULL)
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
cab_skip_sfx(struct archive_read *a)
{
	const char *p, *q;
	size_t skip;
	ssize_t bytes, window;

	window = 4096;
	for (;;) {
		const char *h = __archive_read_ahead(a, window, &bytes);
		if (h == NULL) {
			/* Remaining size are less than window. */
			window >>= 1;
			if (window < 128) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Couldn't find out CAB header");
				return (ARCHIVE_FATAL);
			}
			continue;
		}
		p = h;
		q = p + bytes;

		/*
		 * Scan ahead until we find something that looks
		 * like the cab header.
		 */
		while (p + 8 < q) {
			int next;
			if ((next = find_cab_magic(p)) == 0) {
				skip = p - h;
				__archive_read_consume(a, skip);
				return (ARCHIVE_OK);
			}
			p += next;
		}
		skip = p - h;
		__archive_read_consume(a, skip);
	}
}

static int
truncated_error(struct archive_read *a)
{
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Truncated CAB header");
	return (ARCHIVE_FATAL);
}

static ssize_t
cab_strnlen(const unsigned char *p, size_t maxlen)
{
	size_t i;

	for (i = 0; i <= maxlen; i++) {
		if (p[i] == 0)
			break;
	}
	if (i > maxlen)
		return (-1);/* invalid */
	return ((ssize_t)i);
}

/* Read bytes as much as remaining. */
static const void *
cab_read_ahead_remaining(struct archive_read *a, size_t min, ssize_t *avail)
{
	const void *p;

	while (min > 0) {
		p = __archive_read_ahead(a, min, avail);
		if (p != NULL)
			return (p);
		min--;
	}
	return (NULL);
}

/* Convert a path separator '\' -> '/' */
static int
cab_convert_path_separator_1(struct archive_string *fn, unsigned char attr)
{
	size_t i;
	int mb;

	/* Easy check if we have '\' in multi-byte string. */
	mb = 0;
	for (i = 0; i < archive_strlen(fn); i++) {
		if (fn->s[i] == '\\') {
			if (mb) {
				/* This may be second byte of multi-byte
				 * character. */
				break;
			}
			fn->s[i] = '/';
			mb = 0;
		} else if ((fn->s[i] & 0x80) && !(attr & ATTR_NAME_IS_UTF))
			mb = 1;
		else
			mb = 0;
	}
	if (i == archive_strlen(fn))
		return (0);
	return (-1);
}

/*
 * Replace a character '\' with '/' in wide character.
 */
static void
cab_convert_path_separator_2(struct cab *cab, struct archive_entry *entry)
{
	const wchar_t *wp;
	size_t i;

	/* If a conversion to wide character failed, force the replacement. */
	if ((wp = archive_entry_pathname_w(entry)) != NULL) {
		archive_wstrcpy(&(cab->ws), wp);
		for (i = 0; i < archive_strlen(&(cab->ws)); i++) {
			if (cab->ws.s[i] == L'\\')
				cab->ws.s[i] = L'/';
		}
		archive_entry_copy_pathname_w(entry, cab->ws.s);
	}
}

/*
 * Read CFHEADER, CFFOLDER and CFFILE.
 */
static int
cab_read_header(struct archive_read *a)
{
	const unsigned char *p;
	struct cab *cab;
	struct cfheader *hd;
	size_t bytes, used;
	ssize_t len;
	int64_t skip;
	int err, i;
	int cur_folder, prev_folder;
	uint32_t offset32;
	
	a->archive.archive_format = ARCHIVE_FORMAT_CAB;
	if (a->archive.archive_format_name == NULL)
		a->archive.archive_format_name = "CAB";

	if ((p = __archive_read_ahead(a, 42, NULL)) == NULL)
		return (truncated_error(a));

	cab = (struct cab *)(a->format->data);
	if (cab->found_header == 0 &&
	    p[0] == 'M' && p[1] == 'Z') {
		/* This is an executable?  Must be self-extracting... */
		err = cab_skip_sfx(a);
		if (err < ARCHIVE_WARN)
			return (err);

		/* Re-read header after processing the SFX. */
		if ((p = __archive_read_ahead(a, 42, NULL)) == NULL)
			return (truncated_error(a));
	}

	cab->cab_offset = 0;
	/*
	 * Read CFHEADER.
	 */
	hd = &cab->cfheader;
	if (p[CFHEADER_signature+0] != 'M' || p[CFHEADER_signature+1] != 'S' ||
	    p[CFHEADER_signature+2] != 'C' || p[CFHEADER_signature+3] != 'F') {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Couldn't find out CAB header");
		return (ARCHIVE_FATAL);
	}
	hd->total_bytes = archive_le32dec(p + CFHEADER_cbCabinet);
	hd->files_offset = archive_le32dec(p + CFHEADER_coffFiles);
	hd->minor = p[CFHEADER_versionMinor];
	hd->major = p[CFHEADER_versionMajor];
	hd->folder_count = archive_le16dec(p + CFHEADER_cFolders);
	if (hd->folder_count == 0)
		goto invalid;
	hd->file_count = archive_le16dec(p + CFHEADER_cFiles);
	if (hd->file_count == 0)
		goto invalid;
	hd->flags = archive_le16dec(p + CFHEADER_flags);
	hd->setid = archive_le16dec(p + CFHEADER_setID);
	hd->cabinet = archive_le16dec(p + CFHEADER_iCabinet);
	used = CFHEADER_iCabinet + 2;
	if (hd->flags & RESERVE_PRESENT) {
		uint16_t cfheader;
		cfheader = archive_le16dec(p + CFHEADER_cbCFHeader);
		if (cfheader > 60000U)
			goto invalid;
		hd->cffolder = p[CFHEADER_cbCFFolder];
		hd->cfdata = p[CFHEADER_cbCFData];
		used += 4;/* cbCFHeader, cbCFFolder and cbCFData */
		used += cfheader;/* abReserve */
	} else
		hd->cffolder = 0;/* Avoid compiling warning. */
	if (hd->flags & PREV_CABINET) {
		/* How many bytes are used for szCabinetPrev. */
		if ((p = __archive_read_ahead(a, used+256, NULL)) == NULL)
			return (truncated_error(a));
		if ((len = cab_strnlen(p + used, 255)) <= 0)
			goto invalid;
		used += len + 1;
		/* How many bytes are used for szDiskPrev. */
		if ((p = __archive_read_ahead(a, used+256, NULL)) == NULL)
			return (truncated_error(a));
		if ((len = cab_strnlen(p + used, 255)) <= 0)
			goto invalid;
		used += len + 1;
	}
	if (hd->flags & NEXT_CABINET) {
		/* How many bytes are used for szCabinetNext. */
		if ((p = __archive_read_ahead(a, used+256, NULL)) == NULL)
			return (truncated_error(a));
		if ((len = cab_strnlen(p + used, 255)) <= 0)
			goto invalid;
		used += len + 1;
		/* How many bytes are used for szDiskNext. */
		if ((p = __archive_read_ahead(a, used+256, NULL)) == NULL)
			return (truncated_error(a));
		if ((len = cab_strnlen(p + used, 255)) <= 0)
			goto invalid;
		used += len + 1;
	}
	__archive_read_consume(a, used);
	cab->cab_offset += used;
	used = 0;

	/*
	 * Read CFFOLDER.
	 */
	hd->folder_array = (struct cffolder *)calloc(
	    hd->folder_count, sizeof(struct cffolder));
	if (hd->folder_array == NULL)
		goto nomem;
	
	bytes = 8;
	if (hd->flags & RESERVE_PRESENT)
		bytes += hd->cffolder;
	bytes *= hd->folder_count;
	if ((p = __archive_read_ahead(a, bytes, NULL)) == NULL)
		return (truncated_error(a));
	offset32 = 0;
	for (i = 0; i < hd->folder_count; i++) {
		struct cffolder *folder = &(hd->folder_array[i]);
		folder->cfdata_offset_in_cab =
		    archive_le32dec(p + CFFOLDER_coffCabStart);
		folder->cfdata_count = archive_le16dec(p+CFFOLDER_cCFData);
		folder->comptype =
		    archive_le16dec(p+CFFOLDER_typeCompress) & 0x0F;
		folder->compdata =
		    archive_le16dec(p+CFFOLDER_typeCompress) >> 8;
		/* Get a compression name. */
		if (folder->comptype <
		    sizeof(compression_name) / sizeof(compression_name[0]))
			folder->compname = compression_name[folder->comptype];
		else
			folder->compname = "UNKNOWN";
		p += 8;
		used += 8;
		if (hd->flags & RESERVE_PRESENT) {
			p += hd->cffolder;/* abReserve */
			used += hd->cffolder;
		}
		/*
		 * Sanity check if each data is acceptable.
		 */
		if (offset32 >= folder->cfdata_offset_in_cab)
			goto invalid;
		offset32 = folder->cfdata_offset_in_cab;

		/* Set a request to initialize zlib for the CFDATA of
		 * this folder. */
		folder->decompress_init = 0;
	}
	__archive_read_consume(a, used);
	cab->cab_offset += used;

	/*
	 * Read CFFILE.
	 */
	/* Seek read pointer to the offset of CFFILE if needed. */
	skip = (int64_t)hd->files_offset - cab->cab_offset;
	if (skip <  0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid offset of CFFILE %jd < %jd",
		    (intmax_t)hd->files_offset, (intmax_t)cab->cab_offset);
		return (ARCHIVE_FATAL);
	}
	if (skip) {
		__archive_read_consume(a, skip);
		cab->cab_offset += skip;
	}
	/* Allocate memory for CFDATA */
	hd->file_array = (struct cffile *)calloc(
	    hd->file_count, sizeof(struct cffile));
	if (hd->file_array == NULL)
		goto nomem;

	prev_folder = -1;
	for (i = 0; i < hd->file_count; i++) {
		struct cffile *file = &(hd->file_array[i]);
		ssize_t avail;

		if ((p = __archive_read_ahead(a, 16, NULL)) == NULL)
			return (truncated_error(a));
		file->uncompressed_size = archive_le32dec(p + CFFILE_cbFile);
		file->offset = archive_le32dec(p + CFFILE_uoffFolderStart);
		file->folder = archive_le16dec(p + CFFILE_iFolder);
		file->mtime = cab_dos_time(p + CFFILE_date_time);
		file->attr = (uint8_t)archive_le16dec(p + CFFILE_attribs);
		__archive_read_consume(a, 16);

		cab->cab_offset += 16;
		if ((p = cab_read_ahead_remaining(a, 256, &avail)) == NULL)
			return (truncated_error(a));
		if ((len = cab_strnlen(p, avail-1)) <= 0)
			goto invalid;

		/* Copy a pathname.  */
		archive_string_init(&(file->pathname));
		archive_strncpy(&(file->pathname), p, len);
		__archive_read_consume(a, len + 1);
		cab->cab_offset += len + 1;

		/*
		 * Sanity check if each data is acceptable.
		 */
		if (file->uncompressed_size > 0x7FFF8000)
			goto invalid;/* Too large */
		if ((int64_t)file->offset + (int64_t)file->uncompressed_size
		    > ARCHIVE_LITERAL_LL(0x7FFF8000))
			goto invalid;/* Too large */
		switch (file->folder) {
		case iFoldCONTINUED_TO_NEXT:
			/* This must be last file in a folder. */
			if (i != hd->file_count -1)
				goto invalid;
			cur_folder = hd->folder_count -1;
			break;
		case iFoldCONTINUED_PREV_AND_NEXT:
			/* This must be only one file in a folder. */
			if (hd->file_count != 1)
				goto invalid;
			/* FALL THROUGH */
		case iFoldCONTINUED_FROM_PREV:
			/* This must be first file in a folder. */
			if (i != 0)
				goto invalid;
			prev_folder = cur_folder = 0;
			offset32 = file->offset;
			break;
		default:
			if (file->folder >= hd->folder_count)
				goto invalid;
			cur_folder = file->folder;
			break;
		}
		/* Dot not back track. */
		if (cur_folder < prev_folder)
			goto invalid;
		if (cur_folder != prev_folder)
			offset32 = 0;
		prev_folder = cur_folder;

		/* Make sure there are not any blanks from last file
		 * contents. */
		if (offset32 != file->offset)
			goto invalid;
		offset32 += file->uncompressed_size;

		/* CFDATA is available for file contents. */
		if (file->uncompressed_size > 0 &&
		    hd->folder_array[cur_folder].cfdata_count == 0)
			goto invalid;
	}

	if (hd->cabinet != 0 || hd->flags & (PREV_CABINET | NEXT_CABINET)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Multivolume cabinet file is unsupported");
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
invalid:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Invalid CAB header");
	return (ARCHIVE_FATAL);
nomem:
	archive_set_error(&a->archive, ENOMEM,
	    "Can't allocate memory for CAB data");
	return (ARCHIVE_FATAL);
}

static int
archive_read_format_cab_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	struct cab *cab;
	struct cfheader *hd;
	struct cffolder *prev_folder;
	struct cffile *file;
	struct archive_string_conv *sconv;
	int err = ARCHIVE_OK, r;
	
	cab = (struct cab *)(a->format->data);
	if (cab->found_header == 0) {
		err = cab_read_header(a); 
		if (err < ARCHIVE_WARN)
			return (err);
		/* We've found the header. */
		cab->found_header = 1;
	}
	hd = &cab->cfheader;

	if (hd->file_index >= hd->file_count) {
		cab->end_of_archive = 1;
		return (ARCHIVE_EOF);
	}
	file = &hd->file_array[hd->file_index++];

	cab->end_of_entry = 0;
	cab->end_of_entry_cleanup = 0;
	cab->entry_compressed_bytes_read = 0;
	cab->entry_uncompressed_bytes_read = 0;
	cab->entry_unconsumed = 0;
	cab->entry_cffile = file;

	/*
	 * Choose a proper folder.
	 */
	prev_folder = cab->entry_cffolder;
	switch (file->folder) {
	case iFoldCONTINUED_FROM_PREV:
	case iFoldCONTINUED_PREV_AND_NEXT:
		cab->entry_cffolder = &hd->folder_array[0];
		break;
	case iFoldCONTINUED_TO_NEXT:
		cab->entry_cffolder = &hd->folder_array[hd->folder_count-1];
		break;
	default:
		cab->entry_cffolder = &hd->folder_array[file->folder];
		break;
	}
	/* If a cffolder of this file is changed, reset a cfdata to read
	 * file contents from next cfdata. */
	if (prev_folder != cab->entry_cffolder)
		cab->entry_cfdata = NULL;

	/* If a pathname is UTF-8, prepare a string conversion object
	 * for UTF-8 and use it. */
	if (file->attr & ATTR_NAME_IS_UTF) {
		if (cab->sconv_utf8 == NULL) {
			cab->sconv_utf8 =
			    archive_string_conversion_from_charset(
				&(a->archive), "UTF-8", 1);
			if (cab->sconv_utf8 == NULL)
				return (ARCHIVE_FATAL);
		}
		sconv = cab->sconv_utf8;
	} else if (cab->sconv != NULL) {
		/* Choose the conversion specified by the option. */
		sconv = cab->sconv;
	} else {
		/* Choose the default conversion. */
		if (!cab->init_default_conversion) {
			cab->sconv_default =
			    archive_string_default_conversion_for_read(
			      &(a->archive));
			cab->init_default_conversion = 1;
		}
		sconv = cab->sconv_default;
	}

	/*
	 * Set a default value and common data
	 */
	r = cab_convert_path_separator_1(&(file->pathname), file->attr);
	if (archive_entry_copy_pathname_l(entry, file->pathname.s,
	    archive_strlen(&(file->pathname)), sconv) != 0) {
		if (errno == ENOMEM) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for Pathname");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Pathname cannot be converted "
		    "from %s to current locale.",
		    archive_string_conversion_charset_name(sconv));
		err = ARCHIVE_WARN;
	}
	if (r < 0) {
		/* Convert a path separator '\' -> '/' */
		cab_convert_path_separator_2(cab, entry);
	}

	archive_entry_set_size(entry, file->uncompressed_size);
	if (file->attr & ATTR_RDONLY)
		archive_entry_set_mode(entry, AE_IFREG | 0555);
	else
		archive_entry_set_mode(entry, AE_IFREG | 0666);
	archive_entry_set_mtime(entry, file->mtime, 0);

	cab->entry_bytes_remaining = file->uncompressed_size;
	cab->entry_offset = 0;
	/* We don't need compress data. */
	if (file->uncompressed_size == 0)
		cab->end_of_entry_cleanup = cab->end_of_entry = 1;

	/* Set up a more descriptive format name. */
	sprintf(cab->format_name, "CAB %d.%d (%s)",
	    hd->major, hd->minor, cab->entry_cffolder->compname);
	a->archive.archive_format_name = cab->format_name;

	return (err);
}

static int
archive_read_format_cab_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	struct cab *cab = (struct cab *)(a->format->data);
	int r;

	switch (cab->entry_cffile->folder) {
	case iFoldCONTINUED_FROM_PREV:
	case iFoldCONTINUED_TO_NEXT:
	case iFoldCONTINUED_PREV_AND_NEXT:
		*buff = NULL;
		*size = 0;
		*offset = 0;
		archive_clear_error(&a->archive);
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Cannot restore this file split in multivolume.");
		return (ARCHIVE_FAILED);
	default:
		break;
	}
	if (cab->read_data_invoked == 0) {
		if (cab->bytes_skipped) {
			if (cab->entry_cfdata == NULL) {
				r = cab_next_cfdata(a);
				if (r < 0)
					return (r);
			}
			if (cab_consume_cfdata(a, cab->bytes_skipped) < 0)
				return (ARCHIVE_FATAL);
			cab->bytes_skipped = 0;
		}
		cab->read_data_invoked = 1;
	}
	if (cab->entry_unconsumed) {
		/* Consume as much as the compressor actually used. */
		r = (int)cab_consume_cfdata(a, cab->entry_unconsumed);
		cab->entry_unconsumed = 0;
		if (r < 0)
			return (r);
	}
	if (cab->end_of_archive || cab->end_of_entry) {
		if (!cab->end_of_entry_cleanup) {
			/* End-of-entry cleanup done. */
			cab->end_of_entry_cleanup = 1;
		}
		*offset = cab->entry_offset;
		*size = 0;
		*buff = NULL;
		return (ARCHIVE_EOF);
	}

	return (cab_read_data(a, buff, size, offset));
}

static uint32_t
cab_checksum_cfdata_4(const void *p, size_t bytes, uint32_t seed)
{
	const unsigned char *b;
	unsigned u32num;
	uint32_t sum;

	u32num = (unsigned)bytes / 4;
	sum = seed;
	b = p;
	for (;u32num > 0; --u32num) {
		sum ^= archive_le32dec(b);
		b += 4;
	}
	return (sum);
}

static uint32_t
cab_checksum_cfdata(const void *p, size_t bytes, uint32_t seed)
{
	const unsigned char *b;
	uint32_t sum;
	uint32_t t;

	sum = cab_checksum_cfdata_4(p, bytes, seed);
	b = p;
	b += bytes & ~3;
	t = 0;
	switch (bytes & 3) {
	case 3:
		t |= ((uint32_t)(*b++)) << 16;
		/* FALL THROUGH */
	case 2:
		t |= ((uint32_t)(*b++)) << 8;
		/* FALL THROUGH */
	case 1:
		t |= *b;
		/* FALL THROUGH */
	default:
		break;
	}
	sum ^= t;

	return (sum);
}

static void
cab_checksum_update(struct archive_read *a, size_t bytes)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata = cab->entry_cfdata;
	const unsigned char *p;
	size_t sumbytes;

	if (cfdata->sum == 0 || cfdata->sum_ptr == NULL)
		return;
	/*
	 * Calculate the sum of this CFDATA.
	 * Make sure CFDATA must be calculated in four bytes.
	 */
	p = cfdata->sum_ptr;
	sumbytes = bytes;
	if (cfdata->sum_extra_avail) {
		while (cfdata->sum_extra_avail < 4 && sumbytes > 0) {
			cfdata->sum_extra[
			    cfdata->sum_extra_avail++] = *p++;
			sumbytes--;
		}
		if (cfdata->sum_extra_avail == 4) {
			cfdata->sum_calculated = cab_checksum_cfdata_4(
			    cfdata->sum_extra, 4, cfdata->sum_calculated);
			cfdata->sum_extra_avail = 0;
		}
	}
	if (sumbytes) {
		int odd = sumbytes & 3;
		if (sumbytes - odd > 0)
			cfdata->sum_calculated = cab_checksum_cfdata_4(
			    p, sumbytes - odd, cfdata->sum_calculated);
		if (odd)
			memcpy(cfdata->sum_extra, p + sumbytes - odd, odd);
		cfdata->sum_extra_avail = odd;
	}
	cfdata->sum_ptr = NULL;
}

static int
cab_checksum_finish(struct archive_read *a)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata = cab->entry_cfdata;
	int l;

	/* Do not need to compute a sum. */
	if (cfdata->sum == 0)
		return (ARCHIVE_OK);

	/*
	 * Calculate the sum of remaining CFDATA.
	 */
	if (cfdata->sum_extra_avail) {
		cfdata->sum_calculated =
		    cab_checksum_cfdata(cfdata->sum_extra,
		       cfdata->sum_extra_avail, cfdata->sum_calculated);
		cfdata->sum_extra_avail = 0;
	}

	l = 4;
	if (cab->cfheader.flags & RESERVE_PRESENT)
		l += cab->cfheader.cfdata;
	cfdata->sum_calculated = cab_checksum_cfdata(
	    cfdata->memimage + CFDATA_cbData, l, cfdata->sum_calculated);
	if (cfdata->sum_calculated != cfdata->sum) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Checksum error CFDATA[%d] %x:%x in %d bytes",
		    cab->entry_cffolder->cfdata_index -1,
		    cfdata->sum, cfdata->sum_calculated,
		    cfdata->compressed_size);
		return (ARCHIVE_FAILED);
	}
	return (ARCHIVE_OK);
}

/*
 * Read CFDATA if needed.
 */
static int
cab_next_cfdata(struct archive_read *a)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata = cab->entry_cfdata;

	/* There are remaining bytes in current CFDATA, use it first. */
	if (cfdata != NULL && cfdata->uncompressed_bytes_remaining > 0)
		return (ARCHIVE_OK);

	if (cfdata == NULL) {
		int64_t skip;

		cab->entry_cffolder->cfdata_index = 0;

		/* Seek read pointer to the offset of CFDATA if needed. */
		skip = cab->entry_cffolder->cfdata_offset_in_cab
			- cab->cab_offset;
		if (skip < 0) {
			int folder_index;
			switch (cab->entry_cffile->folder) {
			case iFoldCONTINUED_FROM_PREV:
			case iFoldCONTINUED_PREV_AND_NEXT:
				folder_index = 0;
				break;
			case iFoldCONTINUED_TO_NEXT:
				folder_index = cab->cfheader.folder_count-1;
				break;
			default:
				folder_index = cab->entry_cffile->folder;
				break;
			}
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Invalid offset of CFDATA in folder(%d) %jd < %jd",
			    folder_index,
			    (intmax_t)cab->entry_cffolder->cfdata_offset_in_cab,
			    (intmax_t)cab->cab_offset);
			return (ARCHIVE_FATAL);
		}
		if (skip > 0) {
			if (__archive_read_consume(a, skip) < 0)
				return (ARCHIVE_FATAL);
			cab->cab_offset =
			    cab->entry_cffolder->cfdata_offset_in_cab;
		}
	}

	/*
	 * Read a CFDATA.
	 */
	if (cab->entry_cffolder->cfdata_index <
	    cab->entry_cffolder->cfdata_count) {
		const unsigned char *p;
		int l;

		cfdata = &(cab->entry_cffolder->cfdata);
		cab->entry_cffolder->cfdata_index++;
		cab->entry_cfdata = cfdata;
		cfdata->sum_calculated = 0;
		cfdata->sum_extra_avail = 0;
		cfdata->sum_ptr = NULL;
		l = 8;
		if (cab->cfheader.flags & RESERVE_PRESENT)
			l += cab->cfheader.cfdata;
		if ((p = __archive_read_ahead(a, l, NULL)) == NULL)
			return (truncated_error(a));
		cfdata->sum = archive_le32dec(p + CFDATA_csum);
		cfdata->compressed_size = archive_le16dec(p + CFDATA_cbData);
		cfdata->compressed_bytes_remaining = cfdata->compressed_size;
		cfdata->uncompressed_size =
		    archive_le16dec(p + CFDATA_cbUncomp);
		cfdata->uncompressed_bytes_remaining =
		    cfdata->uncompressed_size;
		cfdata->uncompressed_avail = 0;
		cfdata->read_offset = 0;
		cfdata->unconsumed = 0;

		/*
		 * Sanity check if data size is acceptable.
		 */
		if (cfdata->compressed_size == 0 ||
		    cfdata->compressed_size > (0x8000+6144))
			goto invalid;
		if (cfdata->uncompressed_size > 0x8000)
			goto invalid;
		if (cfdata->uncompressed_size == 0) {
			switch (cab->entry_cffile->folder) {
			case iFoldCONTINUED_PREV_AND_NEXT:
			case iFoldCONTINUED_TO_NEXT:
				break;
			case iFoldCONTINUED_FROM_PREV:
			default:
				goto invalid;
			}
		}
		/* If CFDATA is not last in a folder, an uncompressed
		 * size must be 0x8000(32KBi) */
		if ((cab->entry_cffolder->cfdata_index <
		     cab->entry_cffolder->cfdata_count) &&
		       cfdata->uncompressed_size != 0x8000)
			goto invalid;

		/* A compressed data size and an uncompressed data size must
		 * be the same in no compression mode. */
		if (cab->entry_cffolder->comptype == COMPTYPE_NONE &&
		    cfdata->compressed_size != cfdata->uncompressed_size)
			goto invalid;

		/*
		 * Save CFDATA image for sum check.
		 */
		if (cfdata->memimage_size < (size_t)l) {
			free(cfdata->memimage);
			cfdata->memimage = malloc(l);
			if (cfdata->memimage == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "Can't allocate memory for CAB data");
				return (ARCHIVE_FATAL);
			}
			cfdata->memimage_size = l;
		}
		memcpy(cfdata->memimage, p, l);

		/* Consume bytes as much as we used. */
		__archive_read_consume(a, l);
		cab->cab_offset += l;
	} else if (cab->entry_cffolder->cfdata_count > 0) {
		/* Run out of all CFDATA in a folder. */
		cfdata->compressed_size = 0;
		cfdata->uncompressed_size = 0;
		cfdata->compressed_bytes_remaining = 0;
		cfdata->uncompressed_bytes_remaining = 0;
	} else {
		/* Current folder does not have any CFDATA. */
		cfdata = &(cab->entry_cffolder->cfdata);
		cab->entry_cfdata = cfdata;
		memset(cfdata, 0, sizeof(*cfdata));
	}
	return (ARCHIVE_OK);
invalid:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Invalid CFDATA");
	return (ARCHIVE_FATAL);
}

/*
 * Read ahead CFDATA.
 */
static const void *
cab_read_ahead_cfdata(struct archive_read *a, ssize_t *avail)
{
	struct cab *cab = (struct cab *)(a->format->data);
	int err;

	err = cab_next_cfdata(a);
	if (err < ARCHIVE_OK) {
		*avail = err;
		return (NULL);
	}

	switch (cab->entry_cffolder->comptype) {
	case COMPTYPE_NONE:
		return (cab_read_ahead_cfdata_none(a, avail));
	case COMPTYPE_MSZIP:
		return (cab_read_ahead_cfdata_deflate(a, avail));
	case COMPTYPE_LZX:
		return (cab_read_ahead_cfdata_lzx(a, avail));
	default: /* Unsupported compression. */
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unsupported CAB compression : %s",
		    cab->entry_cffolder->compname);
		*avail = ARCHIVE_FAILED;
		return (NULL);
	}
}

/*
 * Read ahead CFDATA as uncompressed data.
 */
static const void *
cab_read_ahead_cfdata_none(struct archive_read *a, ssize_t *avail)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata;
	const void *d;

	cfdata = cab->entry_cfdata;

	/*
	 * Note: '1' here is a performance optimization.
	 * Recall that the decompression layer returns a count of
	 * available bytes; asking for more than that forces the
	 * decompressor to combine reads by copying data.
	 */
	d = __archive_read_ahead(a, 1, avail);
	if (*avail <= 0) {
		*avail = truncated_error(a);
		return (NULL);
	}
	if (*avail > cfdata->uncompressed_bytes_remaining)
		*avail = cfdata->uncompressed_bytes_remaining;
	cfdata->uncompressed_avail = cfdata->uncompressed_size;
	cfdata->unconsumed = *avail;
	cfdata->sum_ptr = d;
	return (d);
}

/*
 * Read ahead CFDATA as deflate data.
 */
#ifdef HAVE_ZLIB_H
static const void *
cab_read_ahead_cfdata_deflate(struct archive_read *a, ssize_t *avail)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata;
	const void *d;
	int r, mszip;
	uint16_t uavail;
	char eod = 0;

	cfdata = cab->entry_cfdata;
	/* If the buffer hasn't been allocated, allocate it now. */
	if (cab->uncompressed_buffer == NULL) {
		cab->uncompressed_buffer_size = 0x8000;
		cab->uncompressed_buffer
		    = (unsigned char *)malloc(cab->uncompressed_buffer_size);
		if (cab->uncompressed_buffer == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "No memory for CAB reader");
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}

	uavail = cfdata->uncompressed_avail;
	if (uavail == cfdata->uncompressed_size) {
		d = cab->uncompressed_buffer + cfdata->read_offset;
		*avail = uavail - cfdata->read_offset;
		return (d);
	}

	if (!cab->entry_cffolder->decompress_init) {
		cab->stream.next_in = NULL;
		cab->stream.avail_in = 0;
		cab->stream.total_in = 0;
		cab->stream.next_out = NULL;
		cab->stream.avail_out = 0;
		cab->stream.total_out = 0;
		if (cab->stream_valid)
			r = inflateReset(&cab->stream);
		else
			r = inflateInit2(&cab->stream,
			    -15 /* Don't check for zlib header */);
		if (r != Z_OK) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Can't initialize deflate decompression.");
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
		/* Stream structure has been set up. */
		cab->stream_valid = 1;
		/* We've initialized decompression for this stream. */
		cab->entry_cffolder->decompress_init = 1;
	}

	if (cfdata->compressed_bytes_remaining == cfdata->compressed_size)
		mszip = 2;
	else
		mszip = 0;
	eod = 0;
	cab->stream.total_out = uavail;
	/*
	 * We always uncompress all data in current CFDATA.
	 */
	while (!eod && cab->stream.total_out < cfdata->uncompressed_size) {
		ssize_t bytes_avail;

		cab->stream.next_out =
		    cab->uncompressed_buffer + cab->stream.total_out;
		cab->stream.avail_out =
		    cfdata->uncompressed_size - cab->stream.total_out;

		d = __archive_read_ahead(a, 1, &bytes_avail);
		if (bytes_avail <= 0) {
			*avail = truncated_error(a);
			return (NULL);
		}
		if (bytes_avail > cfdata->compressed_bytes_remaining)
			bytes_avail = cfdata->compressed_bytes_remaining;
		/*
		 * A bug in zlib.h: stream.next_in should be marked 'const'
		 * but isn't (the library never alters data through the
		 * next_in pointer, only reads it).  The result: this ugly
		 * cast to remove 'const'.
		 */
		cab->stream.next_in = (Bytef *)(uintptr_t)d;
		cab->stream.avail_in = (uInt)bytes_avail;
		cab->stream.total_in = 0;

		/* Cut out a tow-byte MSZIP signature(0x43, 0x4b). */
		if (mszip > 0) {
			if (bytes_avail <= 0)
				goto nomszip;
			if (bytes_avail <= mszip) {
				if (mszip == 2) {
					if (cab->stream.next_in[0] != 0x43)
						goto nomszip;
					if (bytes_avail > 1 &&
					    cab->stream.next_in[1] != 0x4b)
						goto nomszip;
				} else if (cab->stream.next_in[0] != 0x4b)
					goto nomszip;
				cfdata->unconsumed = bytes_avail;
				cfdata->sum_ptr = d;
				if (cab_minimum_consume_cfdata(
				    a, cfdata->unconsumed) < 0) {
					*avail = ARCHIVE_FATAL;
					return (NULL);
				}
				mszip -= (int)bytes_avail;
				continue;
			}
			if (mszip == 1 && cab->stream.next_in[0] != 0x4b)
				goto nomszip;
			else if (cab->stream.next_in[0] != 0x43 ||
			    cab->stream.next_in[1] != 0x4b)
				goto nomszip;
			cab->stream.next_in += mszip;
			cab->stream.avail_in -= mszip;
			cab->stream.total_in += mszip;
			mszip = 0;
		}

		r = inflate(&cab->stream, 0);
		switch (r) {
		case Z_OK:
			break;
		case Z_STREAM_END:
			eod = 1;
			break;
		default:
			goto zlibfailed;
		}
		cfdata->unconsumed = cab->stream.total_in;
		cfdata->sum_ptr = d;
		if (cab_minimum_consume_cfdata(a, cfdata->unconsumed) < 0) {
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}
	uavail = (uint16_t)cab->stream.total_out;

	if (uavail < cfdata->uncompressed_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid uncompressed size (%d < %d)",
		    uavail, cfdata->uncompressed_size);
		*avail = ARCHIVE_FATAL;
		return (NULL);
	}

	/*
	 * Note: I suspect there is a bug in makecab.exe because, in rare
	 * case, compressed bytes are still remaining regardless we have
	 * gotten all uncompressed bytes, which size is recorded in CFDATA,
	 * as much as we need, and we have to use the garbage so as to
	 * correctly compute the sum of CFDATA accordingly.
	 */
	if (cfdata->compressed_bytes_remaining > 0) {
		ssize_t bytes_avail;

		d = __archive_read_ahead(a, cfdata->compressed_bytes_remaining,
		    &bytes_avail);
		if (bytes_avail <= 0) {
			*avail = truncated_error(a);
			return (NULL);
		}
		cfdata->unconsumed = cfdata->compressed_bytes_remaining;
		cfdata->sum_ptr = d;
		if (cab_minimum_consume_cfdata(a, cfdata->unconsumed) < 0) {
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}

	/*
	 * Set dictionary data for decompressing of next CFDATA, which
	 * in the same folder. This is why we always do decompress CFDATA
	 * even if beginning CFDATA or some of CFDATA are not used in
	 * skipping file data.
	 */
	if (cab->entry_cffolder->cfdata_index <
	    cab->entry_cffolder->cfdata_count) {
		r = inflateReset(&cab->stream);
		if (r != Z_OK)
			goto zlibfailed;
		r = inflateSetDictionary(&cab->stream,
		    cab->uncompressed_buffer, cfdata->uncompressed_size);
		if (r != Z_OK)
			goto zlibfailed;
	}

	d = cab->uncompressed_buffer + cfdata->read_offset;
	*avail = uavail - cfdata->read_offset;
	cfdata->uncompressed_avail = uavail;

	return (d);

zlibfailed:
	switch (r) {
	case Z_MEM_ERROR:
		archive_set_error(&a->archive, ENOMEM,
		    "Out of memory for deflate decompression");
		break;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Deflate decompression failed (%d)", r);
		break;
	}
	*avail = ARCHIVE_FATAL;
	return (NULL);
nomszip:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "CFDATA incorrect(no MSZIP signature)");
	*avail = ARCHIVE_FATAL;
	return (NULL);
}

#else /* HAVE_ZLIB_H */

static const void *
cab_read_ahead_cfdata_deflate(struct archive_read *a, ssize_t *avail)
{
	*avail = ARCHIVE_FATAL;
	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "libarchive compiled without deflate support (no libz)");
	return (NULL);
}

#endif /* HAVE_ZLIB_H */

static const void *
cab_read_ahead_cfdata_lzx(struct archive_read *a, ssize_t *avail)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata;
	const void *d;
	int r;
	uint16_t uavail;

	cfdata = cab->entry_cfdata;
	/* If the buffer hasn't been allocated, allocate it now. */
	if (cab->uncompressed_buffer == NULL) {
		cab->uncompressed_buffer_size = 0x8000;
		cab->uncompressed_buffer
		    = (unsigned char *)malloc(cab->uncompressed_buffer_size);
		if (cab->uncompressed_buffer == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "No memory for CAB reader");
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}

	uavail = cfdata->uncompressed_avail;
	if (uavail == cfdata->uncompressed_size) {
		d = cab->uncompressed_buffer + cfdata->read_offset;
		*avail = uavail - cfdata->read_offset;
		return (d);
	}

	if (!cab->entry_cffolder->decompress_init) {
		r = lzx_decode_init(&cab->xstrm,
		    cab->entry_cffolder->compdata);
		if (r != ARCHIVE_OK) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Can't initialize LZX decompression.");
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
		/* We've initialized decompression for this stream. */
		cab->entry_cffolder->decompress_init = 1;
	}

	/* Clean up remaining bits of previous CFDATA. */
	lzx_cleanup_bitstream(&cab->xstrm);
	cab->xstrm.total_out = uavail;
	while (cab->xstrm.total_out < cfdata->uncompressed_size) {
		ssize_t bytes_avail;

		cab->xstrm.next_out =
		    cab->uncompressed_buffer + cab->xstrm.total_out;
		cab->xstrm.avail_out =
		    cfdata->uncompressed_size - cab->xstrm.total_out;

		d = __archive_read_ahead(a, 1, &bytes_avail);
		if (bytes_avail <= 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Truncated CAB file data");
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
		if (bytes_avail > cfdata->compressed_bytes_remaining)
			bytes_avail = cfdata->compressed_bytes_remaining;

		cab->xstrm.next_in = d;
		cab->xstrm.avail_in = bytes_avail;
		cab->xstrm.total_in = 0;
		r = lzx_decode(&cab->xstrm,
		    cfdata->compressed_bytes_remaining == bytes_avail);
		switch (r) {
		case ARCHIVE_OK:
		case ARCHIVE_EOF:
			break;
		default:
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "LZX decompression failed (%d)", r);
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
		cfdata->unconsumed = cab->xstrm.total_in;
		cfdata->sum_ptr = d;
		if (cab_minimum_consume_cfdata(a, cfdata->unconsumed) < 0) {
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}

	uavail = (uint16_t)cab->xstrm.total_out;
	/*
	 * Make sure a read pointer advances to next CFDATA.
	 */
	if (cfdata->compressed_bytes_remaining > 0) {
		ssize_t bytes_avail;

		d = __archive_read_ahead(a, cfdata->compressed_bytes_remaining,
		    &bytes_avail);
		if (bytes_avail <= 0) {
			*avail = truncated_error(a);
			return (NULL);
		}
		cfdata->unconsumed = cfdata->compressed_bytes_remaining;
		cfdata->sum_ptr = d;
		if (cab_minimum_consume_cfdata(a, cfdata->unconsumed) < 0) {
			*avail = ARCHIVE_FATAL;
			return (NULL);
		}
	}

	/*
	 * Translation reversal of x86 processor CALL byte sequence(E8).
	 */
	lzx_translation(&cab->xstrm, cab->uncompressed_buffer,
	    cfdata->uncompressed_size,
	    (cab->entry_cffolder->cfdata_index-1) * 0x8000);

	d = cab->uncompressed_buffer + cfdata->read_offset;
	*avail = uavail - cfdata->read_offset;
	cfdata->uncompressed_avail = uavail;

	return (d);
}

/*
 * Consume CFDATA.
 * We always decompress CFDATA to consume CFDATA as much as we need
 * in uncompressed bytes because all CFDATA in a folder are related
 * so we do not skip any CFDATA without decompressing.
 * Note: If the folder of a CFFILE is iFoldCONTINUED_PREV_AND_NEXT or
 * iFoldCONTINUED_FROM_PREV, we won't decompress because a CFDATA for
 * the CFFILE is remaining bytes of previous Multivolume CAB file.
 */
static int64_t
cab_consume_cfdata(struct archive_read *a, int64_t consumed_bytes)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata;
	int64_t cbytes, rbytes;
	int err;

	rbytes = cab_minimum_consume_cfdata(a, consumed_bytes);
	if (rbytes < 0)
		return (ARCHIVE_FATAL);

	cfdata = cab->entry_cfdata;
	while (rbytes > 0) {
		ssize_t avail;

		if (cfdata->compressed_size == 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Invalid CFDATA");
			return (ARCHIVE_FATAL);
		}
		cbytes = cfdata->uncompressed_bytes_remaining;
		if (cbytes > rbytes)
			cbytes = rbytes;
		rbytes -= cbytes;

		if (cfdata->uncompressed_avail == 0 &&
		   (cab->entry_cffile->folder == iFoldCONTINUED_PREV_AND_NEXT ||
		    cab->entry_cffile->folder == iFoldCONTINUED_FROM_PREV)) {
			/* We have not read any data yet. */
			if (cbytes == cfdata->uncompressed_bytes_remaining) {
				/* Skip whole current CFDATA. */
				__archive_read_consume(a,
				    cfdata->compressed_size);
				cab->cab_offset += cfdata->compressed_size;
				cfdata->compressed_bytes_remaining = 0;
				cfdata->uncompressed_bytes_remaining = 0;
				err = cab_next_cfdata(a);
				if (err < 0)
					return (err);
				cfdata = cab->entry_cfdata;
				if (cfdata->uncompressed_size == 0) {
					switch (cab->entry_cffile->folder) {
					case iFoldCONTINUED_PREV_AND_NEXT:
					case iFoldCONTINUED_TO_NEXT:
					case iFoldCONTINUED_FROM_PREV:
						rbytes = 0;
						break;
					default:
						break;
					}
				}
				continue;
			}
			cfdata->read_offset += (uint16_t)cbytes;
			cfdata->uncompressed_bytes_remaining -= (uint16_t)cbytes;
			break;
		} else if (cbytes == 0) {
			err = cab_next_cfdata(a);
			if (err < 0)
				return (err);
			cfdata = cab->entry_cfdata;
			if (cfdata->uncompressed_size == 0) {
				switch (cab->entry_cffile->folder) {
				case iFoldCONTINUED_PREV_AND_NEXT:
				case iFoldCONTINUED_TO_NEXT:
				case iFoldCONTINUED_FROM_PREV:
					return (ARCHIVE_FATAL);
				default:
					break;
				}
			}
			continue;
		}
		while (cbytes > 0) {
			(void)cab_read_ahead_cfdata(a, &avail);
			if (avail <= 0)
				return (ARCHIVE_FATAL);
			if (avail > cbytes)
				avail = (ssize_t)cbytes;
			if (cab_minimum_consume_cfdata(a, avail) < 0)
				return (ARCHIVE_FATAL);
			cbytes -= avail;
		}
	}
	return (consumed_bytes);
}

/*
 * Consume CFDATA as much as we have already gotten and
 * compute the sum of CFDATA.
 */
static int64_t
cab_minimum_consume_cfdata(struct archive_read *a, int64_t consumed_bytes)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfdata *cfdata;
	int64_t cbytes, rbytes;
	int err;

	cfdata = cab->entry_cfdata;
	rbytes = consumed_bytes;
	if (cab->entry_cffolder->comptype == COMPTYPE_NONE) {
		if (consumed_bytes < cfdata->unconsumed)
			cbytes = consumed_bytes;
		else
			cbytes = cfdata->unconsumed;
		rbytes -= cbytes; 
		cfdata->read_offset += (uint16_t)cbytes;
		cfdata->uncompressed_bytes_remaining -= (uint16_t)cbytes;
		cfdata->unconsumed -= cbytes;
	} else {
		cbytes = cfdata->uncompressed_avail - cfdata->read_offset;
		if (cbytes > 0) {
			if (consumed_bytes < cbytes)
				cbytes = consumed_bytes;
			rbytes -= cbytes;
			cfdata->read_offset += (uint16_t)cbytes;
			cfdata->uncompressed_bytes_remaining -= (uint16_t)cbytes;
		}

		if (cfdata->unconsumed) {
			cbytes = cfdata->unconsumed;
			cfdata->unconsumed = 0;
		} else
			cbytes = 0;
	}
	if (cbytes) {
		/* Compute the sum. */
		cab_checksum_update(a, (size_t)cbytes);

		/* Consume as much as the compressor actually used. */
		__archive_read_consume(a, cbytes);
		cab->cab_offset += cbytes;
		cfdata->compressed_bytes_remaining -= (uint16_t)cbytes;
		if (cfdata->compressed_bytes_remaining == 0) {
			err = cab_checksum_finish(a);
			if (err < 0)
				return (err);
		}
	}
	return (rbytes);
}

/*
 * Returns ARCHIVE_OK if successful, ARCHIVE_FATAL otherwise, sets
 * cab->end_of_entry if it consumes all of the data.
 */
static int
cab_read_data(struct archive_read *a, const void **buff,
    size_t *size, int64_t *offset)
{
	struct cab *cab = (struct cab *)(a->format->data);
	ssize_t bytes_avail;

	if (cab->entry_bytes_remaining == 0) {
		*buff = NULL;
		*size = 0;
		*offset = cab->entry_offset;
		cab->end_of_entry = 1;
		return (ARCHIVE_OK);
	}

	*buff = cab_read_ahead_cfdata(a, &bytes_avail);
	if (bytes_avail <= 0) {
		*buff = NULL;
		*size = 0;
		*offset = 0;
		if (bytes_avail == 0 &&
		    cab->entry_cfdata->uncompressed_size == 0) {
			/* All of CFDATA in a folder has been handled. */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT, "Invalid CFDATA");
			return (ARCHIVE_FATAL);
		} else
			return ((int)bytes_avail);
	}
	if (bytes_avail > cab->entry_bytes_remaining)
		bytes_avail = (ssize_t)cab->entry_bytes_remaining;

	*size = bytes_avail;
	*offset = cab->entry_offset;
	cab->entry_offset += bytes_avail;
	cab->entry_bytes_remaining -= bytes_avail;
	if (cab->entry_bytes_remaining == 0)
		cab->end_of_entry = 1;
	cab->entry_unconsumed = bytes_avail;
	if (cab->entry_cffolder->comptype == COMPTYPE_NONE) {
		/* Don't consume more than current entry used. */
		if (cab->entry_cfdata->unconsumed > cab->entry_unconsumed)
			cab->entry_cfdata->unconsumed = cab->entry_unconsumed;
	}
	return (ARCHIVE_OK);
}

static int
archive_read_format_cab_read_data_skip(struct archive_read *a)
{
	struct cab *cab;
	int64_t bytes_skipped;
	int r;

	cab = (struct cab *)(a->format->data);

	if (cab->end_of_archive)
		return (ARCHIVE_EOF);

	if (!cab->read_data_invoked) {
		cab->bytes_skipped += cab->entry_bytes_remaining;
		cab->entry_bytes_remaining = 0;
		/* This entry is finished and done. */
		cab->end_of_entry_cleanup = cab->end_of_entry = 1;
		return (ARCHIVE_OK);
	}

	if (cab->entry_unconsumed) {
		/* Consume as much as the compressor actually used. */
		r = (int)cab_consume_cfdata(a, cab->entry_unconsumed);
		cab->entry_unconsumed = 0;
		if (r < 0)
			return (r);
	} else if (cab->entry_cfdata == NULL) {
		r = cab_next_cfdata(a);
		if (r < 0)
			return (r);
	}

	/* if we've already read to end of data, we're done. */
	if (cab->end_of_entry_cleanup)
		return (ARCHIVE_OK);

	/*
	 * If the length is at the beginning, we can skip the
	 * compressed data much more quickly.
	 */
	bytes_skipped = cab_consume_cfdata(a, cab->entry_bytes_remaining);
	if (bytes_skipped < 0)
		return (ARCHIVE_FATAL);

	/* If the compression type is none(uncompressed), we've already
	 * consumed data as much as the current entry size. */
	if (cab->entry_cffolder->comptype == COMPTYPE_NONE &&
	    cab->entry_cfdata != NULL)
		cab->entry_cfdata->unconsumed = 0;

	/* This entry is finished and done. */
	cab->end_of_entry_cleanup = cab->end_of_entry = 1;
	return (ARCHIVE_OK);
}

static int
archive_read_format_cab_cleanup(struct archive_read *a)
{
	struct cab *cab = (struct cab *)(a->format->data);
	struct cfheader *hd = &cab->cfheader;
	int i;

	if (hd->folder_array != NULL) {
		for (i = 0; i < hd->folder_count; i++)
			free(hd->folder_array[i].cfdata.memimage);
		free(hd->folder_array);
	}
	if (hd->file_array != NULL) {
		for (i = 0; i < cab->cfheader.file_count; i++)
			archive_string_free(&(hd->file_array[i].pathname));
		free(hd->file_array);
	}
#ifdef HAVE_ZLIB_H
	if (cab->stream_valid)
		inflateEnd(&cab->stream);
#endif
	lzx_decode_free(&cab->xstrm);
	archive_wstring_free(&cab->ws);
	free(cab->uncompressed_buffer);
	free(cab);
	(a->format->data) = NULL;
	return (ARCHIVE_OK);
}

/* Convert an MSDOS-style date/time into Unix-style time. */
static time_t
cab_dos_time(const unsigned char *p)
{
	int msTime, msDate;
	struct tm ts;

	msDate = archive_le16dec(p);
	msTime = archive_le16dec(p+2);

	memset(&ts, 0, sizeof(ts));
	ts.tm_year = ((msDate >> 9) & 0x7f) + 80;   /* Years since 1900. */
	ts.tm_mon = ((msDate >> 5) & 0x0f) - 1;     /* Month number.     */
	ts.tm_mday = msDate & 0x1f;		    /* Day of month.     */
	ts.tm_hour = (msTime >> 11) & 0x1f;
	ts.tm_min = (msTime >> 5) & 0x3f;
	ts.tm_sec = (msTime << 1) & 0x3e;
	ts.tm_isdst = -1;
	return (mktime(&ts));
}

/*****************************************************************
 *
 * LZX decompression code.
 *
 *****************************************************************/

/*
 * Initialize LZX decoder.
 *
 * Returns ARCHIVE_OK if initialization was successful.
 * Returns ARCHIVE_FAILED if w_bits has unsupported value.
 * Returns ARCHIVE_FATAL if initialization failed; memory allocation
 * error occurred.
 */
static int
lzx_decode_init(struct lzx_stream *strm, int w_bits)
{
	struct lzx_dec *ds;
	int slot, w_size, w_slot;
	int base, footer;
	int base_inc[18];

	if (strm->ds == NULL) {
		strm->ds = calloc(1, sizeof(*strm->ds));
		if (strm->ds == NULL)
			return (ARCHIVE_FATAL);
	}
	ds = strm->ds;
	ds->error = ARCHIVE_FAILED;

	/* Allow bits from 15(32KBi) up to 21(2MBi) */
	if (w_bits < SLOT_BASE || w_bits > SLOT_MAX)
		return (ARCHIVE_FAILED);

	ds->error = ARCHIVE_FATAL;

	/*
	 * Alloc window
	 */
	w_size = ds->w_size;
	w_slot = slots[w_bits - SLOT_BASE];
	ds->w_size = 1U << w_bits;
	ds->w_mask = ds->w_size -1;
	if (ds->w_buff == NULL || w_size != ds->w_size) {
		free(ds->w_buff);
		ds->w_buff = malloc(ds->w_size);
		if (ds->w_buff == NULL)
			return (ARCHIVE_FATAL);
		free(ds->pos_tbl);
		ds->pos_tbl = malloc(sizeof(ds->pos_tbl[0]) * w_slot);
		if (ds->pos_tbl == NULL)
			return (ARCHIVE_FATAL);
		lzx_huffman_free(&(ds->mt));
	}

	for (footer = 0; footer < 18; footer++)
		base_inc[footer] = 1 << footer;
	base = footer = 0;
	for (slot = 0; slot < w_slot; slot++) {
		int n;
		if (footer == 0)
			base = slot;
		else
			base += base_inc[footer];
		if (footer < 17) {
			footer = -2;
			for (n = base; n; n >>= 1)
				footer++;
			if (footer <= 0)
				footer = 0;
		}
		ds->pos_tbl[slot].base = base;
		ds->pos_tbl[slot].footer_bits = footer;
	}

	ds->w_pos = 0;
	ds->state = 0;
	ds->br.cache_buffer = 0;
	ds->br.cache_avail = 0;
	ds->r0 = ds->r1 = ds->r2 = 1;

	/* Initialize aligned offset tree. */
	if (lzx_huffman_init(&(ds->at), 8, 8) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	/* Initialize pre-tree. */
	if (lzx_huffman_init(&(ds->pt), 20, 10) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	/* Initialize Main tree. */
	if (lzx_huffman_init(&(ds->mt), 256+(w_slot<<3), 16)
	    != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	/* Initialize Length tree. */
	if (lzx_huffman_init(&(ds->lt), 249, 16) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	ds->error = 0;

	return (ARCHIVE_OK);
}

/*
 * Release LZX decoder.
 */
static void
lzx_decode_free(struct lzx_stream *strm)
{

	if (strm->ds == NULL)
		return;
	free(strm->ds->w_buff);
	free(strm->ds->pos_tbl);
	lzx_huffman_free(&(strm->ds->at));
	lzx_huffman_free(&(strm->ds->pt));
	lzx_huffman_free(&(strm->ds->mt));
	lzx_huffman_free(&(strm->ds->lt));
	free(strm->ds);
	strm->ds = NULL;
}

/*
 * E8 Call Translation reversal.
 */
static void
lzx_translation(struct lzx_stream *strm, void *p, size_t size, uint32_t offset)
{
	struct lzx_dec *ds = strm->ds;
	unsigned char *b, *end;

	if (!ds->translation || size <= 10)
		return;
	b = p;
	end = b + size - 10;
	while (b < end && (b = memchr(b, 0xE8, end - b)) != NULL) {
		size_t i = b - (unsigned char *)p;
		int32_t cp, displacement, value;

		cp = (int32_t)(offset + (uint32_t)i);
		value = archive_le32dec(&b[1]);
		if (value >= -cp && value < (int32_t)ds->translation_size) {
			if (value >= 0)
				displacement = value - cp;
			else
				displacement = value + ds->translation_size;
			archive_le32enc(&b[1], (uint32_t)displacement);
		}
		b += 5;
	}
}

/*
 * Bit stream reader.
 */
/* Check that the cache buffer has enough bits. */
#define lzx_br_has(br, n)	((br)->cache_avail >= n)
/* Get compressed data by bit. */
#define lzx_br_bits(br, n)				\
	(((uint32_t)((br)->cache_buffer >>		\
		((br)->cache_avail - (n)))) & cache_masks[n])
#define lzx_br_bits_forced(br, n)			\
	(((uint32_t)((br)->cache_buffer <<		\
		((n) - (br)->cache_avail))) & cache_masks[n])
/* Read ahead to make sure the cache buffer has enough compressed data we
 * will use.
 *  True  : completed, there is enough data in the cache buffer.
 *  False : we met that strm->next_in is empty, we have to get following
 *          bytes. */
#define lzx_br_read_ahead_0(strm, br, n)	\
	(lzx_br_has((br), (n)) || lzx_br_fillup(strm, br))
/*  True  : the cache buffer has some bits as much as we need.
 *  False : there are no enough bits in the cache buffer to be used,
 *          we have to get following bytes if we could. */
#define lzx_br_read_ahead(strm, br, n)	\
	(lzx_br_read_ahead_0((strm), (br), (n)) || lzx_br_has((br), (n)))

/* Notify how many bits we consumed. */
#define lzx_br_consume(br, n)	((br)->cache_avail -= (n))
#define lzx_br_consume_unaligned_bits(br) ((br)->cache_avail &= ~0x0f)

#define lzx_br_is_unaligned(br)	((br)->cache_avail & 0x0f)

static const uint32_t cache_masks[] = {
	0x00000000, 0x00000001, 0x00000003, 0x00000007,
	0x0000000F, 0x0000001F, 0x0000003F, 0x0000007F,
	0x000000FF, 0x000001FF, 0x000003FF, 0x000007FF,
	0x00000FFF, 0x00001FFF, 0x00003FFF, 0x00007FFF,
	0x0000FFFF, 0x0001FFFF, 0x0003FFFF, 0x0007FFFF,
	0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF,
	0x00FFFFFF, 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF,
	0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Shift away used bits in the cache data and fill it up with following bits.
 * Call this when cache buffer does not have enough bits you need.
 *
 * Returns 1 if the cache buffer is full.
 * Returns 0 if the cache buffer is not full; input buffer is empty.
 */
static int
lzx_br_fillup(struct lzx_stream *strm, struct lzx_br *br)
{
/*
 * x86 processor family can read misaligned data without an access error.
 */
	int n = CACHE_BITS - br->cache_avail;

	for (;;) {
		switch (n >> 4) {
		case 4:
			if (strm->avail_in >= 8) {
				br->cache_buffer =
				    ((uint64_t)strm->next_in[1]) << 56 |
				    ((uint64_t)strm->next_in[0]) << 48 |
				    ((uint64_t)strm->next_in[3]) << 40 |
				    ((uint64_t)strm->next_in[2]) << 32 |
				    ((uint32_t)strm->next_in[5]) << 24 |
				    ((uint32_t)strm->next_in[4]) << 16 |
				    ((uint32_t)strm->next_in[7]) << 8 |
				     (uint32_t)strm->next_in[6];
				strm->next_in += 8;
				strm->avail_in -= 8;
				br->cache_avail += 8 * 8;
				return (1);
			}
			break;
		case 3:
			if (strm->avail_in >= 6) {
				br->cache_buffer =
		 		   (br->cache_buffer << 48) |
				    ((uint64_t)strm->next_in[1]) << 40 |
				    ((uint64_t)strm->next_in[0]) << 32 |
				    ((uint32_t)strm->next_in[3]) << 24 |
				    ((uint32_t)strm->next_in[2]) << 16 |
				    ((uint32_t)strm->next_in[5]) << 8 |
				     (uint32_t)strm->next_in[4];
				strm->next_in += 6;
				strm->avail_in -= 6;
				br->cache_avail += 6 * 8;
				return (1);
			}
			break;
		case 0:
			/* We have enough compressed data in
			 * the cache buffer.*/
			return (1);
		default:
			break;
		}
		if (strm->avail_in < 2) {
			/* There is not enough compressed data to
			 * fill up the cache buffer. */
			if (strm->avail_in == 1) {
				br->odd = *strm->next_in++;
				strm->avail_in--;
				br->have_odd = 1;
			}
			return (0);
		}
		br->cache_buffer =
		   (br->cache_buffer << 16) |
		    archive_le16dec(strm->next_in);
		strm->next_in += 2;
		strm->avail_in -= 2;
		br->cache_avail += 16;
		n -= 16;
	}
}

static void
lzx_br_fixup(struct lzx_stream *strm, struct lzx_br *br)
{
	int n = CACHE_BITS - br->cache_avail;

	if (br->have_odd && n >= 16 && strm->avail_in > 0) {
		br->cache_buffer =
		   (br->cache_buffer << 16) |
		   ((uint16_t)(*strm->next_in)) << 8 | br->odd;
		strm->next_in++;
		strm->avail_in--;
		br->cache_avail += 16;
		br->have_odd = 0;
	}
}

static void
lzx_cleanup_bitstream(struct lzx_stream *strm)
{
	strm->ds->br.cache_avail = 0;
	strm->ds->br.have_odd = 0;
}

/*
 * Decode LZX.
 *
 * 1. Returns ARCHIVE_OK if output buffer or input buffer are empty.
 *    Please set available buffer and call this function again.
 * 2. Returns ARCHIVE_EOF if decompression has been completed.
 * 3. Returns ARCHIVE_FAILED if an error occurred; compressed data
 *    is broken or you do not set 'last' flag properly.
 */
#define ST_RD_TRANSLATION	0
#define ST_RD_TRANSLATION_SIZE	1
#define ST_RD_BLOCK_TYPE	2
#define ST_RD_BLOCK_SIZE	3
#define ST_RD_ALIGNMENT		4
#define ST_RD_R0		5
#define ST_RD_R1		6
#define ST_RD_R2		7
#define ST_COPY_UNCOMP1		8
#define ST_COPY_UNCOMP2		9
#define ST_RD_ALIGNED_OFFSET	10
#define ST_RD_VERBATIM		11
#define ST_RD_PRE_MAIN_TREE_256	12
#define ST_MAIN_TREE_256	13
#define ST_RD_PRE_MAIN_TREE_REM	14
#define ST_MAIN_TREE_REM	15
#define ST_RD_PRE_LENGTH_TREE	16
#define ST_LENGTH_TREE		17
#define ST_MAIN			18
#define ST_LENGTH		19
#define ST_OFFSET		20
#define ST_REAL_POS		21
#define ST_COPY			22

static int
lzx_decode(struct lzx_stream *strm, int last)
{
	struct lzx_dec *ds = strm->ds;
	int64_t avail_in;
	int r;

	if (ds->error)
		return (ds->error);

	avail_in = strm->avail_in;
	lzx_br_fixup(strm, &(ds->br));
	do {
		if (ds->state < ST_MAIN)
			r = lzx_read_blocks(strm, last);
		else {
			int64_t bytes_written = strm->avail_out;
			r = lzx_decode_blocks(strm, last);
			bytes_written -= strm->avail_out;
			strm->next_out += bytes_written;
			strm->total_out += bytes_written;
		}
	} while (r == 100);
	strm->total_in += avail_in - strm->avail_in;
	return (r);
}

static int
lzx_read_blocks(struct lzx_stream *strm, int last)
{
	struct lzx_dec *ds = strm->ds;
	struct lzx_br *br = &(ds->br);
	int i, r;

	for (;;) {
		switch (ds->state) {
		case ST_RD_TRANSLATION:
			if (!lzx_br_read_ahead(strm, br, 1)) {
				ds->state = ST_RD_TRANSLATION;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			ds->translation = lzx_br_bits(br, 1);
			lzx_br_consume(br, 1);
			/* FALL THROUGH */
		case ST_RD_TRANSLATION_SIZE:
			if (ds->translation) {
				if (!lzx_br_read_ahead(strm, br, 32)) {
					ds->state = ST_RD_TRANSLATION_SIZE;
					if (last)
						goto failed;
					return (ARCHIVE_OK);
				}
				ds->translation_size = lzx_br_bits(br, 16);
				lzx_br_consume(br, 16);
				ds->translation_size <<= 16;
				ds->translation_size |= lzx_br_bits(br, 16);
				lzx_br_consume(br, 16);
			}
			/* FALL THROUGH */
		case ST_RD_BLOCK_TYPE:
			if (!lzx_br_read_ahead(strm, br, 3)) {
				ds->state = ST_RD_BLOCK_TYPE;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			ds->block_type = lzx_br_bits(br, 3);
			lzx_br_consume(br, 3);
			/* Check a block type. */
			switch (ds->block_type) {
			case VERBATIM_BLOCK:
			case ALIGNED_OFFSET_BLOCK:
			case UNCOMPRESSED_BLOCK:
				break;
			default:
				goto failed;/* Invalid */
			}
			/* FALL THROUGH */
		case ST_RD_BLOCK_SIZE:
			if (!lzx_br_read_ahead(strm, br, 24)) {
				ds->state = ST_RD_BLOCK_SIZE;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			ds->block_size = lzx_br_bits(br, 8);
			lzx_br_consume(br, 8);
			ds->block_size <<= 16;
			ds->block_size |= lzx_br_bits(br, 16);
			lzx_br_consume(br, 16);
			if (ds->block_size == 0)
				goto failed;
			ds->block_bytes_avail = ds->block_size;
			if (ds->block_type != UNCOMPRESSED_BLOCK) {
				if (ds->block_type == VERBATIM_BLOCK)
					ds->state = ST_RD_VERBATIM;
				else
					ds->state = ST_RD_ALIGNED_OFFSET;
				break;
			}
			/* FALL THROUGH */
		case ST_RD_ALIGNMENT:
			/*
			 * Handle an Uncompressed Block.
			 */
			/* Skip padding to align following field on
			 * 16-bit boundary. */
			if (lzx_br_is_unaligned(br))
				lzx_br_consume_unaligned_bits(br);
			else {
				if (lzx_br_read_ahead(strm, br, 16))
					lzx_br_consume(br, 16);
				else {
					ds->state = ST_RD_ALIGNMENT;
					if (last)
						goto failed;
					return (ARCHIVE_OK);
				}
			}
			/* Preparation to read repeated offsets R0,R1 and R2. */
			ds->rbytes_avail = 0;
			ds->state = ST_RD_R0;
			/* FALL THROUGH */
		case ST_RD_R0:
		case ST_RD_R1:
		case ST_RD_R2:
			do {
				uint16_t u16;
				/* Drain bits in the cache buffer of
				 * bit-stream. */
				if (lzx_br_has(br, 32)) {
					u16 = lzx_br_bits(br, 16);
					lzx_br_consume(br, 16);
					archive_le16enc(ds->rbytes, u16);
					u16 = lzx_br_bits(br, 16);
					lzx_br_consume(br, 16);
					archive_le16enc(ds->rbytes+2, u16);
					ds->rbytes_avail = 4;
				} else if (lzx_br_has(br, 16)) {
					u16 = lzx_br_bits(br, 16);
					lzx_br_consume(br, 16);
					archive_le16enc(ds->rbytes, u16);
					ds->rbytes_avail = 2;
				}
				if (ds->rbytes_avail < 4 && ds->br.have_odd) {
					ds->rbytes[ds->rbytes_avail++] =
					    ds->br.odd;
					ds->br.have_odd = 0;
				}
				while (ds->rbytes_avail < 4) {
					if (strm->avail_in <= 0) {
						if (last)
							goto failed;
						return (ARCHIVE_OK);
					}
					ds->rbytes[ds->rbytes_avail++] =
					    *strm->next_in++;
					strm->avail_in--;
				}
				ds->rbytes_avail = 0;
				if (ds->state == ST_RD_R0) {
					ds->r0 = archive_le32dec(ds->rbytes);
					if (ds->r0 < 0)
						goto failed;
					ds->state = ST_RD_R1;
				} else if (ds->state == ST_RD_R1) {
					ds->r1 = archive_le32dec(ds->rbytes);
					if (ds->r1 < 0)
						goto failed;
					ds->state = ST_RD_R2;
				} else if (ds->state == ST_RD_R2) {
					ds->r2 = archive_le32dec(ds->rbytes);
					if (ds->r2 < 0)
						goto failed;
					/* We've gotten all repeated offsets. */
					ds->state = ST_COPY_UNCOMP1;
				}
			} while (ds->state != ST_COPY_UNCOMP1);
			/* FALL THROUGH */
		case ST_COPY_UNCOMP1:
			/*
			 * Copy bytes form next_in to next_out directly.
			 */
			while (ds->block_bytes_avail) {
				int l;

				if (strm->avail_out <= 0)
					/* Output buffer is empty. */
					return (ARCHIVE_OK);
				if (strm->avail_in <= 0) {
					/* Input buffer is empty. */
					if (last)
						goto failed;
					return (ARCHIVE_OK);
				}
				l = (int)ds->block_bytes_avail;
				if (l > ds->w_size - ds->w_pos)
					l = ds->w_size - ds->w_pos;
				if (l > strm->avail_out)
					l = (int)strm->avail_out;
				if (l > strm->avail_in)
					l = (int)strm->avail_in;
				memcpy(strm->next_out, strm->next_in, l);
				memcpy(&(ds->w_buff[ds->w_pos]),
				    strm->next_in, l);
				strm->next_in += l;
				strm->avail_in -= l;
				strm->next_out += l;
				strm->avail_out -= l;
				strm->total_out += l;
				ds->w_pos = (ds->w_pos + l) & ds->w_mask;
				ds->block_bytes_avail -= l;
			}
			/* FALL THROUGH */
		case ST_COPY_UNCOMP2:
			/* Re-align; skip padding byte. */
			if (ds->block_size & 1) {
				if (strm->avail_in <= 0) {
					/* Input buffer is empty. */
					ds->state = ST_COPY_UNCOMP2;
					if (last)
						goto failed;
					return (ARCHIVE_OK);
				}
				strm->next_in++;
				strm->avail_in --;
			}
			/* This block ended. */
			ds->state = ST_RD_BLOCK_TYPE;
			return (ARCHIVE_EOF);
			/********************/
		case ST_RD_ALIGNED_OFFSET:
			/*
			 * Read Aligned offset tree.
			 */
			if (!lzx_br_read_ahead(strm, br, 3 * ds->at.len_size)) {
				ds->state = ST_RD_ALIGNED_OFFSET;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			memset(ds->at.freq, 0, sizeof(ds->at.freq));
			for (i = 0; i < ds->at.len_size; i++) {
				ds->at.bitlen[i] = lzx_br_bits(br, 3);
				ds->at.freq[ds->at.bitlen[i]]++;
				lzx_br_consume(br, 3);
			}
			if (!lzx_make_huffman_table(&ds->at))
				goto failed;
			/* FALL THROUGH */
		case ST_RD_VERBATIM:
			ds->loop = 0;
			/* FALL THROUGH */
		case ST_RD_PRE_MAIN_TREE_256:
			/*
			 * Read Pre-tree for first 256 elements of main tree.
			 */
			if (!lzx_read_pre_tree(strm)) {
				ds->state = ST_RD_PRE_MAIN_TREE_256;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			if (!lzx_make_huffman_table(&ds->pt))
				goto failed;
			ds->loop = 0;
			/* FALL THROUGH */
		case ST_MAIN_TREE_256:
			/*
			 * Get path lengths of first 256 elements of main tree.
			 */
			r = lzx_read_bitlen(strm, &ds->mt, 256);
			if (r < 0)
				goto failed;
			else if (!r) {
				ds->state = ST_MAIN_TREE_256;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			ds->loop = 0;
			/* FALL THROUGH */
		case ST_RD_PRE_MAIN_TREE_REM:
			/*
			 * Read Pre-tree for remaining elements of main tree.
			 */
			if (!lzx_read_pre_tree(strm)) {
				ds->state = ST_RD_PRE_MAIN_TREE_REM;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			if (!lzx_make_huffman_table(&ds->pt))
				goto failed;
			ds->loop = 256;
			/* FALL THROUGH */
		case ST_MAIN_TREE_REM:
			/*
			 * Get path lengths of remaining elements of main tree.
			 */
			r = lzx_read_bitlen(strm, &ds->mt, -1);
			if (r < 0)
				goto failed;
			else if (!r) {
				ds->state = ST_MAIN_TREE_REM;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			if (!lzx_make_huffman_table(&ds->mt))
				goto failed;
			ds->loop = 0;
			/* FALL THROUGH */
		case ST_RD_PRE_LENGTH_TREE:
			/*
			 * Read Pre-tree for remaining elements of main tree.
			 */
			if (!lzx_read_pre_tree(strm)) {
				ds->state = ST_RD_PRE_LENGTH_TREE;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			if (!lzx_make_huffman_table(&ds->pt))
				goto failed;
			ds->loop = 0;
			/* FALL THROUGH */
		case ST_LENGTH_TREE:
			/*
			 * Get path lengths of remaining elements of main tree.
			 */
			r = lzx_read_bitlen(strm, &ds->lt, -1);
			if (r < 0)
				goto failed;
			else if (!r) {
				ds->state = ST_LENGTH_TREE;
				if (last)
					goto failed;
				return (ARCHIVE_OK);
			}
			if (!lzx_make_huffman_table(&ds->lt))
				goto failed;
			ds->state = ST_MAIN;
			return (100);
		}
	}
failed:
	return (ds->error = ARCHIVE_FAILED);
}

static int
lzx_decode_blocks(struct lzx_stream *strm, int last)
{
	struct lzx_dec *ds = strm->ds;
	struct lzx_br bre = ds->br;
	struct huffman *at = &(ds->at), *lt = &(ds->lt), *mt = &(ds->mt);
	const struct lzx_pos_tbl *pos_tbl = ds->pos_tbl;
	unsigned char *noutp = strm->next_out;
	unsigned char *endp = noutp + strm->avail_out;
	unsigned char *w_buff = ds->w_buff;
	unsigned char *at_bitlen = at->bitlen;
	unsigned char *lt_bitlen = lt->bitlen;
	unsigned char *mt_bitlen = mt->bitlen;
	size_t block_bytes_avail = ds->block_bytes_avail;
	int at_max_bits = at->max_bits;
	int lt_max_bits = lt->max_bits;
	int mt_max_bits = mt->max_bits;
	int c, copy_len = ds->copy_len, copy_pos = ds->copy_pos;
	int w_pos = ds->w_pos, w_mask = ds->w_mask, w_size = ds->w_size;
	int length_header = ds->length_header;
	int offset_bits = ds->offset_bits;
	int position_slot = ds->position_slot;
	int r0 = ds->r0, r1 = ds->r1, r2 = ds->r2;
	int state = ds->state;
	char block_type = ds->block_type;

	for (;;) {
		switch (state) {
		case ST_MAIN:
			for (;;) {
				if (block_bytes_avail == 0) {
					/* This block ended. */
					ds->state = ST_RD_BLOCK_TYPE;
					ds->br = bre;
					ds->block_bytes_avail =
					    block_bytes_avail;
					ds->copy_len = copy_len;
					ds->copy_pos = copy_pos;
					ds->length_header = length_header;
					ds->position_slot = position_slot;
					ds->r0 = r0; ds->r1 = r1; ds->r2 = r2;
					ds->w_pos = w_pos;
					strm->avail_out = endp - noutp;
					return (ARCHIVE_EOF);
				}
				if (noutp >= endp)
					/* Output buffer is empty. */
					goto next_data;

				if (!lzx_br_read_ahead(strm, &bre,
				    mt_max_bits)) {
					if (!last)
						goto next_data;
					/* Remaining bits are less than
					 * maximum bits(mt.max_bits) but maybe
					 * it still remains as much as we need,
					 * so we should try to use it with
					 * dummy bits. */
					c = lzx_decode_huffman(mt,
					      lzx_br_bits_forced(
				 	        &bre, mt_max_bits));
					lzx_br_consume(&bre, mt_bitlen[c]);
					if (!lzx_br_has(&bre, 0))
						goto failed;/* Over read. */
				} else {
					c = lzx_decode_huffman(mt,
					      lzx_br_bits(&bre, mt_max_bits));
					lzx_br_consume(&bre, mt_bitlen[c]);
				}
				if (c > UCHAR_MAX)
					break;
				/*
				 * 'c' is exactly literal code.
				 */
				/* Save a decoded code to reference it
				 * afterward. */
				w_buff[w_pos] = c;
				w_pos = (w_pos + 1) & w_mask;
				/* Store the decoded code to output buffer. */
				*noutp++ = c;
				block_bytes_avail--;
			}
			/*
			 * Get a match code, its length and offset.
			 */
			c -= UCHAR_MAX + 1;
			length_header = c & 7;
			position_slot = c >> 3;
			/* FALL THROUGH */
		case ST_LENGTH:
			/*
			 * Get a length.
			 */
			if (length_header == 7) {
				if (!lzx_br_read_ahead(strm, &bre,
				    lt_max_bits)) {
					if (!last) {
						state = ST_LENGTH;
						goto next_data;
					}
					c = lzx_decode_huffman(lt,
					      lzx_br_bits_forced(
					        &bre, lt_max_bits));
					lzx_br_consume(&bre, lt_bitlen[c]);
					if (!lzx_br_has(&bre, 0))
						goto failed;/* Over read. */
				} else {
					c = lzx_decode_huffman(lt,
					    lzx_br_bits(&bre, lt_max_bits));
					lzx_br_consume(&bre, lt_bitlen[c]);
				}
				copy_len = c + 7 + 2;
			} else
				copy_len = length_header + 2;
			if ((size_t)copy_len > block_bytes_avail)
				goto failed;
			/*
			 * Get an offset.
			 */
			switch (position_slot) {
			case 0: /* Use repeated offset 0. */
				copy_pos = r0;
				state = ST_REAL_POS;
				continue;
			case 1: /* Use repeated offset 1. */
				copy_pos = r1;
				/* Swap repeated offset. */
				r1 = r0;
				r0 = copy_pos;
				state = ST_REAL_POS;
				continue;
			case 2: /* Use repeated offset 2. */
				copy_pos = r2;
				/* Swap repeated offset. */
				r2 = r0;
				r0 = copy_pos;
				state = ST_REAL_POS;
				continue;
			default:
				offset_bits =
				    pos_tbl[position_slot].footer_bits;
				break;
			}
			/* FALL THROUGH */
		case ST_OFFSET:
			/*
			 * Get the offset, which is a distance from
			 * current window position.
			 */
			if (block_type == ALIGNED_OFFSET_BLOCK &&
			    offset_bits >= 3) {
				int offbits = offset_bits - 3;

				if (!lzx_br_read_ahead(strm, &bre, offbits)) {
					state = ST_OFFSET;
					if (last)
						goto failed;
					goto next_data;
				}
				copy_pos = lzx_br_bits(&bre, offbits) << 3;

				/* Get an aligned number. */
				if (!lzx_br_read_ahead(strm, &bre,
				    offbits + at_max_bits)) {
					if (!last) {
						state = ST_OFFSET;
						goto next_data;
					}
					lzx_br_consume(&bre, offbits);
					c = lzx_decode_huffman(at,
					      lzx_br_bits_forced(&bre,
					        at_max_bits));
					lzx_br_consume(&bre, at_bitlen[c]);
					if (!lzx_br_has(&bre, 0))
						goto failed;/* Over read. */
				} else {
					lzx_br_consume(&bre, offbits);
					c = lzx_decode_huffman(at,
					      lzx_br_bits(&bre, at_max_bits));
					lzx_br_consume(&bre, at_bitlen[c]);
				}
				/* Add an aligned number. */
				copy_pos += c;
			} else {
				if (!lzx_br_read_ahead(strm, &bre,
				    offset_bits)) {
					state = ST_OFFSET;
					if (last)
						goto failed;
					goto next_data;
				}
				copy_pos = lzx_br_bits(&bre, offset_bits);
				lzx_br_consume(&bre, offset_bits);
			}
			copy_pos += pos_tbl[position_slot].base -2;

			/* Update repeated offset LRU queue. */
			r2 = r1;
			r1 = r0;
			r0 = copy_pos;
			/* FALL THROUGH */
		case ST_REAL_POS:
			/*
			 * Compute a real position in window.
			 */
			copy_pos = (w_pos - copy_pos) & w_mask;
			/* FALL THROUGH */
		case ST_COPY:
			/*
			 * Copy several bytes as extracted data from the window
			 * into the output buffer.
			 */
			for (;;) {
				const unsigned char *s;
				int l;

				l = copy_len;
				if (copy_pos > w_pos) {
					if (l > w_size - copy_pos)
						l = w_size - copy_pos;
				} else {
					if (l > w_size - w_pos)
						l = w_size - w_pos;
				}
				if (noutp + l >= endp)
					l = (int)(endp - noutp);
				s = w_buff + copy_pos;
				if (l >= 8 && ((copy_pos + l < w_pos)
				  || (w_pos + l < copy_pos))) {
					memcpy(w_buff + w_pos, s, l);
					memcpy(noutp, s, l);
				} else {
					unsigned char *d;
					int li;

					d = w_buff + w_pos;
					for (li = 0; li < l; li++)
						noutp[li] = d[li] = s[li];
				}
				noutp += l;
				copy_pos = (copy_pos + l) & w_mask;
				w_pos = (w_pos + l) & w_mask;
				block_bytes_avail -= l;
				if (copy_len <= l)
					/* A copy of current pattern ended. */
					break;
				copy_len -= l;
				if (noutp >= endp) {
					/* Output buffer is empty. */
					state = ST_COPY;
					goto next_data;
				}
			}
			state = ST_MAIN;
			break;
		}
	}
failed:
	return (ds->error = ARCHIVE_FAILED);
next_data:
	ds->br = bre;
	ds->block_bytes_avail = block_bytes_avail;
	ds->copy_len = copy_len;
	ds->copy_pos = copy_pos;
	ds->length_header = length_header;
	ds->offset_bits = offset_bits;
	ds->position_slot = position_slot;
	ds->r0 = r0; ds->r1 = r1; ds->r2 = r2;
	ds->state = state;
	ds->w_pos = w_pos;
	strm->avail_out = endp - noutp;
	return (ARCHIVE_OK);
}

static int
lzx_read_pre_tree(struct lzx_stream *strm)
{
	struct lzx_dec *ds = strm->ds;
	struct lzx_br *br = &(ds->br);
	int i;

	if (ds->loop == 0)
		memset(ds->pt.freq, 0, sizeof(ds->pt.freq));
	for (i = ds->loop; i < ds->pt.len_size; i++) {
		if (!lzx_br_read_ahead(strm, br, 4)) {
			ds->loop = i;
			return (0);
		}
		ds->pt.bitlen[i] = lzx_br_bits(br, 4);
		ds->pt.freq[ds->pt.bitlen[i]]++;
		lzx_br_consume(br, 4);
	}
	ds->loop = i;
	return (1);
}

/*
 * Read a bunch of bit-lengths from pre-tree.
 */
static int
lzx_read_bitlen(struct lzx_stream *strm, struct huffman *d, int end)
{
	struct lzx_dec *ds = strm->ds;
	struct lzx_br *br = &(ds->br);
	int c, i, j, ret, same;
	unsigned rbits;

	i = ds->loop;
	if (i == 0)
		memset(d->freq, 0, sizeof(d->freq));
	ret = 0;
	if (end < 0)
		end = d->len_size;
	while (i < end) {
		ds->loop = i;
		if (!lzx_br_read_ahead(strm, br, ds->pt.max_bits))
			goto getdata;
		rbits = lzx_br_bits(br, ds->pt.max_bits);
		c = lzx_decode_huffman(&(ds->pt), rbits);
		switch (c) {
		case 17:/* several zero lengths, from 4 to 19. */
			if (!lzx_br_read_ahead(strm, br, ds->pt.bitlen[c]+4))
				goto getdata;
			lzx_br_consume(br, ds->pt.bitlen[c]);
			same = lzx_br_bits(br, 4) + 4;
			if (i + same > end)
				return (-1);/* Invalid */
			lzx_br_consume(br, 4);
			for (j = 0; j < same; j++)
				d->bitlen[i++] = 0;
			break;
		case 18:/* many zero lengths, from 20 to 51. */
			if (!lzx_br_read_ahead(strm, br, ds->pt.bitlen[c]+5))
				goto getdata;
			lzx_br_consume(br, ds->pt.bitlen[c]);
			same = lzx_br_bits(br, 5) + 20;
			if (i + same > end)
				return (-1);/* Invalid */
			lzx_br_consume(br, 5);
			memset(d->bitlen + i, 0, same);
			i += same;
			break;
		case 19:/* a few same lengths. */
			if (!lzx_br_read_ahead(strm, br,
			    ds->pt.bitlen[c]+1+ds->pt.max_bits))
				goto getdata;
			lzx_br_consume(br, ds->pt.bitlen[c]);
			same = lzx_br_bits(br, 1) + 4;
			if (i + same > end)
				return (-1);
			lzx_br_consume(br, 1);
			rbits = lzx_br_bits(br, ds->pt.max_bits);
			c = lzx_decode_huffman(&(ds->pt), rbits);
			lzx_br_consume(br, ds->pt.bitlen[c]);
			c = (d->bitlen[i] - c + 17) % 17;
			if (c < 0)
				return (-1);/* Invalid */
			for (j = 0; j < same; j++)
				d->bitlen[i++] = c;
			d->freq[c] += same;
			break;
		default:
			lzx_br_consume(br, ds->pt.bitlen[c]);
			c = (d->bitlen[i] - c + 17) % 17;
			if (c < 0)
				return (-1);/* Invalid */
			d->freq[c]++;
			d->bitlen[i++] = c;
			break;
		}
	}
	ret = 1;
getdata:
	ds->loop = i;
	return (ret);
}

static int
lzx_huffman_init(struct huffman *hf, size_t len_size, int tbl_bits)
{

	if (hf->bitlen == NULL || hf->len_size != (int)len_size) {
		free(hf->bitlen);
		hf->bitlen = calloc(len_size,  sizeof(hf->bitlen[0]));
		if (hf->bitlen == NULL)
			return (ARCHIVE_FATAL);
		hf->len_size = (int)len_size;
	} else
		memset(hf->bitlen, 0, len_size *  sizeof(hf->bitlen[0]));
	if (hf->tbl == NULL) {
		hf->tbl = malloc(((size_t)1 << tbl_bits) * sizeof(hf->tbl[0]));
		if (hf->tbl == NULL)
			return (ARCHIVE_FATAL);
		hf->tbl_bits = tbl_bits;
	}
	return (ARCHIVE_OK);
}

static void
lzx_huffman_free(struct huffman *hf)
{
	free(hf->bitlen);
	free(hf->tbl);
}

/*
 * Make a huffman coding table.
 */
static int
lzx_make_huffman_table(struct huffman *hf)
{
	uint16_t *tbl;
	const unsigned char *bitlen;
	int bitptn[17], weight[17];
	int i, maxbits = 0, ptn, tbl_size, w;
	int len_avail;

	/*
	 * Initialize bit patterns.
	 */
	ptn = 0;
	for (i = 1, w = 1 << 15; i <= 16; i++, w >>= 1) {
		bitptn[i] = ptn;
		weight[i] = w;
		if (hf->freq[i]) {
			ptn += hf->freq[i] * w;
			maxbits = i;
		}
	}
	if ((ptn & 0xffff) != 0 || maxbits > hf->tbl_bits)
		return (0);/* Invalid */

	hf->max_bits = maxbits;

	/*
	 * Cut out extra bits which we won't house in the table.
	 * This preparation reduces the same calculation in the for-loop
	 * making the table.
	 */
	if (maxbits < 16) {
		int ebits = 16 - maxbits;
		for (i = 1; i <= maxbits; i++) {
			bitptn[i] >>= ebits;
			weight[i] >>= ebits;
		}
	}

	/*
	 * Make the table.
	 */
	tbl_size = 1 << hf->tbl_bits;
	tbl = hf->tbl;
	bitlen = hf->bitlen;
	len_avail = hf->len_size;
	hf->tree_used = 0;
	for (i = 0; i < len_avail; i++) {
		uint16_t *p;
		int len, cnt;

		if (bitlen[i] == 0)
			continue;
		/* Get a bit pattern */
		len = bitlen[i];
		if (len > tbl_size)
			return (0);
		ptn = bitptn[len];
		cnt = weight[len];
		/* Calculate next bit pattern */
		if ((bitptn[len] = ptn + cnt) > tbl_size)
			return (0);/* Invalid */
		/* Update the table */
		p = &(tbl[ptn]);
		while (--cnt >= 0)
			p[cnt] = (uint16_t)i;
	}
	return (1);
}

static inline int
lzx_decode_huffman(struct huffman *hf, unsigned rbits)
{
	int c;
	c = hf->tbl[rbits];
	if (c < hf->len_size)
		return (c);
	return (0);
}
