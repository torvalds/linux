/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2009 Andreas Henriksson <andreas@fatal.se>
 * Copyright (c) 2009-2012 Michihiro NAKAJIMA
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
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <time.h>
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_read_private.h"
#include "archive_string.h"

/*
 * An overview of ISO 9660 format:
 *
 * Each disk is laid out as follows:
 *   * 32k reserved for private use
 *   * Volume descriptor table.  Each volume descriptor
 *     is 2k and specifies basic format information.
 *     The "Primary Volume Descriptor" (PVD) is defined by the
 *     standard and should always be present; other volume
 *     descriptors include various vendor-specific extensions.
 *   * Files and directories.  Each file/dir is specified by
 *     an "extent" (starting sector and length in bytes).
 *     Dirs are just files with directory records packed one
 *     after another.  The PVD contains a single dir entry
 *     specifying the location of the root directory.  Everything
 *     else follows from there.
 *
 * This module works by first reading the volume descriptors, then
 * building a list of directory entries, sorted by starting
 * sector.  At each step, I look for the earliest dir entry that
 * hasn't yet been read, seek forward to that location and read
 * that entry.  If it's a dir, I slurp in the new dir entries and
 * add them to the heap; if it's a regular file, I return the
 * corresponding archive_entry and wait for the client to request
 * the file body.  This strategy allows us to read most compliant
 * CDs with a single pass through the data, as required by libarchive.
 */
#define	LOGICAL_BLOCK_SIZE	2048
#define	SYSTEM_AREA_BLOCK	16

/* Structure of on-disk primary volume descriptor. */
#define PVD_type_offset 0
#define PVD_type_size 1
#define PVD_id_offset (PVD_type_offset + PVD_type_size)
#define PVD_id_size 5
#define PVD_version_offset (PVD_id_offset + PVD_id_size)
#define PVD_version_size 1
#define PVD_reserved1_offset (PVD_version_offset + PVD_version_size)
#define PVD_reserved1_size 1
#define PVD_system_id_offset (PVD_reserved1_offset + PVD_reserved1_size)
#define PVD_system_id_size 32
#define PVD_volume_id_offset (PVD_system_id_offset + PVD_system_id_size)
#define PVD_volume_id_size 32
#define PVD_reserved2_offset (PVD_volume_id_offset + PVD_volume_id_size)
#define PVD_reserved2_size 8
#define PVD_volume_space_size_offset (PVD_reserved2_offset + PVD_reserved2_size)
#define PVD_volume_space_size_size 8
#define PVD_reserved3_offset (PVD_volume_space_size_offset + PVD_volume_space_size_size)
#define PVD_reserved3_size 32
#define PVD_volume_set_size_offset (PVD_reserved3_offset + PVD_reserved3_size)
#define PVD_volume_set_size_size 4
#define PVD_volume_sequence_number_offset (PVD_volume_set_size_offset + PVD_volume_set_size_size)
#define PVD_volume_sequence_number_size 4
#define PVD_logical_block_size_offset (PVD_volume_sequence_number_offset + PVD_volume_sequence_number_size)
#define PVD_logical_block_size_size 4
#define PVD_path_table_size_offset (PVD_logical_block_size_offset + PVD_logical_block_size_size)
#define PVD_path_table_size_size 8
#define PVD_type_1_path_table_offset (PVD_path_table_size_offset + PVD_path_table_size_size)
#define PVD_type_1_path_table_size 4
#define PVD_opt_type_1_path_table_offset (PVD_type_1_path_table_offset + PVD_type_1_path_table_size)
#define PVD_opt_type_1_path_table_size 4
#define PVD_type_m_path_table_offset (PVD_opt_type_1_path_table_offset + PVD_opt_type_1_path_table_size)
#define PVD_type_m_path_table_size 4
#define PVD_opt_type_m_path_table_offset (PVD_type_m_path_table_offset + PVD_type_m_path_table_size)
#define PVD_opt_type_m_path_table_size 4
#define PVD_root_directory_record_offset (PVD_opt_type_m_path_table_offset + PVD_opt_type_m_path_table_size)
#define PVD_root_directory_record_size 34
#define PVD_volume_set_id_offset (PVD_root_directory_record_offset + PVD_root_directory_record_size)
#define PVD_volume_set_id_size 128
#define PVD_publisher_id_offset (PVD_volume_set_id_offset + PVD_volume_set_id_size)
#define PVD_publisher_id_size 128
#define PVD_preparer_id_offset (PVD_publisher_id_offset + PVD_publisher_id_size)
#define PVD_preparer_id_size 128
#define PVD_application_id_offset (PVD_preparer_id_offset + PVD_preparer_id_size)
#define PVD_application_id_size 128
#define PVD_copyright_file_id_offset (PVD_application_id_offset + PVD_application_id_size)
#define PVD_copyright_file_id_size 37
#define PVD_abstract_file_id_offset (PVD_copyright_file_id_offset + PVD_copyright_file_id_size)
#define PVD_abstract_file_id_size 37
#define PVD_bibliographic_file_id_offset (PVD_abstract_file_id_offset + PVD_abstract_file_id_size)
#define PVD_bibliographic_file_id_size 37
#define PVD_creation_date_offset (PVD_bibliographic_file_id_offset + PVD_bibliographic_file_id_size)
#define PVD_creation_date_size 17
#define PVD_modification_date_offset (PVD_creation_date_offset + PVD_creation_date_size)
#define PVD_modification_date_size 17
#define PVD_expiration_date_offset (PVD_modification_date_offset + PVD_modification_date_size)
#define PVD_expiration_date_size 17
#define PVD_effective_date_offset (PVD_expiration_date_offset + PVD_expiration_date_size)
#define PVD_effective_date_size 17
#define PVD_file_structure_version_offset (PVD_effective_date_offset + PVD_effective_date_size)
#define PVD_file_structure_version_size 1
#define PVD_reserved4_offset (PVD_file_structure_version_offset + PVD_file_structure_version_size)
#define PVD_reserved4_size 1
#define PVD_application_data_offset (PVD_reserved4_offset + PVD_reserved4_size)
#define PVD_application_data_size 512
#define PVD_reserved5_offset (PVD_application_data_offset + PVD_application_data_size)
#define PVD_reserved5_size (2048 - PVD_reserved5_offset)

/* TODO: It would make future maintenance easier to just hardcode the
 * above values.  In particular, ECMA119 states the offsets as part of
 * the standard.  That would eliminate the need for the following check.*/
#if PVD_reserved5_offset != 1395
#error PVD offset and size definitions are wrong.
#endif


/* Structure of optional on-disk supplementary volume descriptor. */
#define SVD_type_offset 0
#define SVD_type_size 1
#define SVD_id_offset (SVD_type_offset + SVD_type_size)
#define SVD_id_size 5
#define SVD_version_offset (SVD_id_offset + SVD_id_size)
#define SVD_version_size 1
/* ... */
#define SVD_reserved1_offset	72
#define SVD_reserved1_size	8
#define SVD_volume_space_size_offset 80
#define SVD_volume_space_size_size 8
#define SVD_escape_sequences_offset (SVD_volume_space_size_offset + SVD_volume_space_size_size)
#define SVD_escape_sequences_size 32
/* ... */
#define SVD_logical_block_size_offset 128
#define SVD_logical_block_size_size 4
#define SVD_type_L_path_table_offset 140
#define SVD_type_M_path_table_offset 148
/* ... */
#define SVD_root_directory_record_offset 156
#define SVD_root_directory_record_size 34
#define SVD_file_structure_version_offset 881
#define SVD_reserved2_offset	882
#define SVD_reserved2_size	1
#define SVD_reserved3_offset	1395
#define SVD_reserved3_size	653
/* ... */
/* FIXME: validate correctness of last SVD entry offset. */

/* Structure of an on-disk directory record. */
/* Note:  ISO9660 stores each multi-byte integer twice, once in
 * each byte order.  The sizes here are the size of just one
 * of the two integers.  (This is why the offset of a field isn't
 * the same as the offset+size of the previous field.) */
#define DR_length_offset 0
#define DR_length_size 1
#define DR_ext_attr_length_offset 1
#define DR_ext_attr_length_size 1
#define DR_extent_offset 2
#define DR_extent_size 4
#define DR_size_offset 10
#define DR_size_size 4
#define DR_date_offset 18
#define DR_date_size 7
#define DR_flags_offset 25
#define DR_flags_size 1
#define DR_file_unit_size_offset 26
#define DR_file_unit_size_size 1
#define DR_interleave_offset 27
#define DR_interleave_size 1
#define DR_volume_sequence_number_offset 28
#define DR_volume_sequence_number_size 2
#define DR_name_len_offset 32
#define DR_name_len_size 1
#define DR_name_offset 33

#ifdef HAVE_ZLIB_H
static const unsigned char zisofs_magic[8] = {
	0x37, 0xE4, 0x53, 0x96, 0xC9, 0xDB, 0xD6, 0x07
};

struct zisofs {
	/* Set 1 if this file compressed by paged zlib */
	int		 pz;
	int		 pz_log2_bs; /* Log2 of block size */
	uint64_t	 pz_uncompressed_size;

	int		 initialized;
	unsigned char	*uncompressed_buffer;
	size_t		 uncompressed_buffer_size;

	uint32_t	 pz_offset;
	unsigned char	 header[16];
	size_t		 header_avail;
	int		 header_passed;
	unsigned char	*block_pointers;
	size_t		 block_pointers_alloc;
	size_t		 block_pointers_size;
	size_t		 block_pointers_avail;
	size_t		 block_off;
	uint32_t	 block_avail;

	z_stream	 stream;
	int		 stream_valid;
};
#else
struct zisofs {
	/* Set 1 if this file compressed by paged zlib */
	int		 pz;
};
#endif

struct content {
	uint64_t	 offset;/* Offset on disk.		*/
	uint64_t	 size;	/* File size in bytes.		*/
	struct content	*next;
};

/* In-memory storage for a directory record. */
struct file_info {
	struct file_info	*use_next;
	struct file_info	*parent;
	struct file_info	*next;
	struct file_info	*re_next;
	int		 subdirs;
	uint64_t	 key;		/* Heap Key.			*/
	uint64_t	 offset;	/* Offset on disk.		*/
	uint64_t	 size;		/* File size in bytes.		*/
	uint32_t	 ce_offset;	/* Offset of CE.		*/
	uint32_t	 ce_size;	/* Size of CE.			*/
	char		 rr_moved;	/* Flag to rr_moved.		*/
	char		 rr_moved_has_re_only;
	char		 re;		/* Having RRIP "RE" extension.	*/
	char		 re_descendant;
	uint64_t	 cl_offset;	/* Having RRIP "CL" extension.	*/
	int		 birthtime_is_set;
	time_t		 birthtime;	/* File created time.		*/
	time_t		 mtime;		/* File last modified time.	*/
	time_t		 atime;		/* File last accessed time.	*/
	time_t		 ctime;		/* File attribute change time.	*/
	uint64_t	 rdev;		/* Device number.		*/
	mode_t		 mode;
	uid_t		 uid;
	gid_t		 gid;
	int64_t		 number;
	int		 nlinks;
	struct archive_string name; /* Pathname */
	unsigned char	*utf16be_name;
	size_t		 utf16be_bytes;
	char		 name_continues; /* Non-zero if name continues */
	struct archive_string symlink;
	char		 symlink_continues; /* Non-zero if link continues */
	/* Set 1 if this file compressed by paged zlib(zisofs) */
	int		 pz;
	int		 pz_log2_bs; /* Log2 of block size */
	uint64_t	 pz_uncompressed_size;
	/* Set 1 if this file is multi extent. */
	int		 multi_extent;
	struct {
		struct content	*first;
		struct content	**last;
	} contents;
	struct {
		struct file_info	*first;
		struct file_info	**last;
	} rede_files;
};

struct heap_queue {
	struct file_info **files;
	int		 allocated;
	int		 used;
};

struct iso9660 {
	int	magic;
#define ISO9660_MAGIC   0x96609660

	int opt_support_joliet;
	int opt_support_rockridge;

	struct archive_string pathname;
	char	seenRockridge;	/* Set true if RR extensions are used. */
	char	seenSUSP;	/* Set true if SUSP is being used. */
	char	seenJoliet;

	unsigned char	suspOffset;
	struct file_info *rr_moved;
	struct read_ce_queue {
		struct read_ce_req {
			uint64_t	 offset;/* Offset of CE on disk. */
			struct file_info *file;
		}		*reqs;
		int		 cnt;
		int		 allocated;
	}	read_ce_req;

	int64_t		previous_number;
	struct archive_string previous_pathname;

	struct file_info		*use_files;
	struct heap_queue		 pending_files;
	struct {
		struct file_info	*first;
		struct file_info	**last;
	}	cache_files;
	struct {
		struct file_info	*first;
		struct file_info	**last;
	}	re_files;

	uint64_t current_position;
	ssize_t	logical_block_size;
	uint64_t volume_size; /* Total size of volume in bytes. */
	int32_t  volume_block;/* Total size of volume in logical blocks. */

	struct vd {
		int		location;	/* Location of Extent.	*/
		uint32_t	size;
	} primary, joliet;

	int64_t	entry_sparse_offset;
	int64_t	entry_bytes_remaining;
	size_t  entry_bytes_unconsumed;
	struct zisofs	 entry_zisofs;
	struct content	*entry_content;
	struct archive_string_conv *sconv_utf16be;
	/*
	 * Buffers for a full pathname in UTF-16BE in Joliet extensions.
	 */
#define UTF16_NAME_MAX	1024
	unsigned char *utf16be_path;
	size_t		 utf16be_path_len;
	unsigned char *utf16be_previous_path;
	size_t		 utf16be_previous_path_len;
	/* Null buffer used in bidder to improve its performance. */
	unsigned char	 null[2048];
};

static int	archive_read_format_iso9660_bid(struct archive_read *, int);
static int	archive_read_format_iso9660_options(struct archive_read *,
		    const char *, const char *);
static int	archive_read_format_iso9660_cleanup(struct archive_read *);
static int	archive_read_format_iso9660_read_data(struct archive_read *,
		    const void **, size_t *, int64_t *);
static int	archive_read_format_iso9660_read_data_skip(struct archive_read *);
static int	archive_read_format_iso9660_read_header(struct archive_read *,
		    struct archive_entry *);
static const char *build_pathname(struct archive_string *, struct file_info *, int);
static int	build_pathname_utf16be(unsigned char *, size_t, size_t *,
		    struct file_info *);
#if DEBUG
static void	dump_isodirrec(FILE *, const unsigned char *isodirrec);
#endif
static time_t	time_from_tm(struct tm *);
static time_t	isodate17(const unsigned char *);
static time_t	isodate7(const unsigned char *);
static int	isBootRecord(struct iso9660 *, const unsigned char *);
static int	isVolumePartition(struct iso9660 *, const unsigned char *);
static int	isVDSetTerminator(struct iso9660 *, const unsigned char *);
static int	isJolietSVD(struct iso9660 *, const unsigned char *);
static int	isSVD(struct iso9660 *, const unsigned char *);
static int	isEVD(struct iso9660 *, const unsigned char *);
static int	isPVD(struct iso9660 *, const unsigned char *);
static int	next_cache_entry(struct archive_read *, struct iso9660 *,
		    struct file_info **);
static int	next_entry_seek(struct archive_read *, struct iso9660 *,
		    struct file_info **);
static struct file_info *
		parse_file_info(struct archive_read *a,
		    struct file_info *parent, const unsigned char *isodirrec,
		    size_t reclen);
static int	parse_rockridge(struct archive_read *a,
		    struct file_info *file, const unsigned char *start,
		    const unsigned char *end);
static int	register_CE(struct archive_read *a, int32_t location,
		    struct file_info *file);
static int	read_CE(struct archive_read *a, struct iso9660 *iso9660);
static void	parse_rockridge_NM1(struct file_info *,
		    const unsigned char *, int);
static void	parse_rockridge_SL1(struct file_info *,
		    const unsigned char *, int);
static void	parse_rockridge_TF1(struct file_info *,
		    const unsigned char *, int);
static void	parse_rockridge_ZF1(struct file_info *,
		    const unsigned char *, int);
static void	register_file(struct iso9660 *, struct file_info *);
static void	release_files(struct iso9660 *);
static unsigned	toi(const void *p, int n);
static inline void re_add_entry(struct iso9660 *, struct file_info *);
static inline struct file_info * re_get_entry(struct iso9660 *);
static inline int rede_add_entry(struct file_info *);
static inline struct file_info * rede_get_entry(struct file_info *);
static inline void cache_add_entry(struct iso9660 *iso9660,
		    struct file_info *file);
static inline struct file_info *cache_get_entry(struct iso9660 *iso9660);
static int	heap_add_entry(struct archive_read *a, struct heap_queue *heap,
		    struct file_info *file, uint64_t key);
static struct file_info *heap_get_entry(struct heap_queue *heap);

#define add_entry(arch, iso9660, file)	\
	heap_add_entry(arch, &((iso9660)->pending_files), file, file->offset)
#define next_entry(iso9660)		\
	heap_get_entry(&((iso9660)->pending_files))

int
archive_read_support_format_iso9660(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct iso9660 *iso9660;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_iso9660");

	iso9660 = (struct iso9660 *)calloc(1, sizeof(*iso9660));
	if (iso9660 == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate iso9660 data");
		return (ARCHIVE_FATAL);
	}
	iso9660->magic = ISO9660_MAGIC;
	iso9660->cache_files.first = NULL;
	iso9660->cache_files.last = &(iso9660->cache_files.first);
	iso9660->re_files.first = NULL;
	iso9660->re_files.last = &(iso9660->re_files.first);
	/* Enable to support Joliet extensions by default.	*/
	iso9660->opt_support_joliet = 1;
	/* Enable to support Rock Ridge extensions by default.	*/
	iso9660->opt_support_rockridge = 1;

	r = __archive_read_register_format(a,
	    iso9660,
	    "iso9660",
	    archive_read_format_iso9660_bid,
	    archive_read_format_iso9660_options,
	    archive_read_format_iso9660_read_header,
	    archive_read_format_iso9660_read_data,
	    archive_read_format_iso9660_read_data_skip,
	    NULL,
	    archive_read_format_iso9660_cleanup,
	    NULL,
	    NULL);

	if (r != ARCHIVE_OK) {
		free(iso9660);
		return (r);
	}
	return (ARCHIVE_OK);
}


static int
archive_read_format_iso9660_bid(struct archive_read *a, int best_bid)
{
	struct iso9660 *iso9660;
	ssize_t bytes_read;
	const unsigned char *p;
	int seenTerminator;

	/* If there's already a better bid than we can ever
	   make, don't bother testing. */
	if (best_bid > 48)
		return (-1);

	iso9660 = (struct iso9660 *)(a->format->data);

	/*
	 * Skip the first 32k (reserved area) and get the first
	 * 8 sectors of the volume descriptor table.  Of course,
	 * if the I/O layer gives us more, we'll take it.
	 */
#define RESERVED_AREA	(SYSTEM_AREA_BLOCK * LOGICAL_BLOCK_SIZE)
	p = __archive_read_ahead(a,
	    RESERVED_AREA + 8 * LOGICAL_BLOCK_SIZE,
	    &bytes_read);
	if (p == NULL)
	    return (-1);

	/* Skip the reserved area. */
	bytes_read -= RESERVED_AREA;
	p += RESERVED_AREA;

	/* Check each volume descriptor. */
	seenTerminator = 0;
	for (; bytes_read > LOGICAL_BLOCK_SIZE;
	    bytes_read -= LOGICAL_BLOCK_SIZE, p += LOGICAL_BLOCK_SIZE) {
		/* Do not handle undefined Volume Descriptor Type. */
		if (p[0] >= 4 && p[0] <= 254)
			return (0);
		/* Standard Identifier must be "CD001" */
		if (memcmp(p + 1, "CD001", 5) != 0)
			return (0);
		if (isPVD(iso9660, p))
			continue;
		if (!iso9660->joliet.location) {
			if (isJolietSVD(iso9660, p))
				continue;
		}
		if (isBootRecord(iso9660, p))
			continue;
		if (isEVD(iso9660, p))
			continue;
		if (isSVD(iso9660, p))
			continue;
		if (isVolumePartition(iso9660, p))
			continue;
		if (isVDSetTerminator(iso9660, p)) {
			seenTerminator = 1;
			break;
		}
		return (0);
	}
	/*
	 * ISO 9660 format must have Primary Volume Descriptor and
	 * Volume Descriptor Set Terminator.
	 */
	if (seenTerminator && iso9660->primary.location > 16)
		return (48);

	/* We didn't find a valid PVD; return a bid of zero. */
	return (0);
}

static int
archive_read_format_iso9660_options(struct archive_read *a,
		const char *key, const char *val)
{
	struct iso9660 *iso9660;

	iso9660 = (struct iso9660 *)(a->format->data);

	if (strcmp(key, "joliet") == 0) {
		if (val == NULL || strcmp(val, "off") == 0 ||
				strcmp(val, "ignore") == 0 ||
				strcmp(val, "disable") == 0 ||
				strcmp(val, "0") == 0)
			iso9660->opt_support_joliet = 0;
		else
			iso9660->opt_support_joliet = 1;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "rockridge") == 0 ||
	    strcmp(key, "Rockridge") == 0) {
		iso9660->opt_support_rockridge = val != NULL;
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
isNull(struct iso9660 *iso9660, const unsigned char *h, unsigned offset,
unsigned bytes)
{

	while (bytes >= sizeof(iso9660->null)) {
		if (!memcmp(iso9660->null, h + offset, sizeof(iso9660->null)))
			return (0);
		offset += sizeof(iso9660->null);
		bytes -= sizeof(iso9660->null);
	}
	if (bytes)
		return memcmp(iso9660->null, h + offset, bytes) == 0;
	else
		return (1);
}

static int
isBootRecord(struct iso9660 *iso9660, const unsigned char *h)
{
	(void)iso9660; /* UNUSED */

	/* Type of the Volume Descriptor Boot Record must be 0. */
	if (h[0] != 0)
		return (0);

	/* Volume Descriptor Version must be 1. */
	if (h[6] != 1)
		return (0);

	return (1);
}

static int
isVolumePartition(struct iso9660 *iso9660, const unsigned char *h)
{
	int32_t location;

	/* Type of the Volume Partition Descriptor must be 3. */
	if (h[0] != 3)
		return (0);

	/* Volume Descriptor Version must be 1. */
	if (h[6] != 1)
		return (0);
	/* Unused Field */
	if (h[7] != 0)
		return (0);

	location = archive_le32dec(h + 72);
	if (location <= SYSTEM_AREA_BLOCK ||
	    location >= iso9660->volume_block)
		return (0);
	if ((uint32_t)location != archive_be32dec(h + 76))
		return (0);

	return (1);
}

static int
isVDSetTerminator(struct iso9660 *iso9660, const unsigned char *h)
{
	(void)iso9660; /* UNUSED */

	/* Type of the Volume Descriptor Set Terminator must be 255. */
	if (h[0] != 255)
		return (0);

	/* Volume Descriptor Version must be 1. */
	if (h[6] != 1)
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, 7, 2048-7))
		return (0);

	return (1);
}

static int
isJolietSVD(struct iso9660 *iso9660, const unsigned char *h)
{
	const unsigned char *p;
	ssize_t logical_block_size;
	int32_t volume_block;

	/* Check if current sector is a kind of Supplementary Volume
	 * Descriptor. */
	if (!isSVD(iso9660, h))
		return (0);

	/* FIXME: do more validations according to joliet spec. */

	/* check if this SVD contains joliet extension! */
	p = h + SVD_escape_sequences_offset;
	/* N.B. Joliet spec says p[1] == '\\', but.... */
	if (p[0] == '%' && p[1] == '/') {
		int level = 0;

		if (p[2] == '@')
			level = 1;
		else if (p[2] == 'C')
			level = 2;
		else if (p[2] == 'E')
			level = 3;
		else /* not joliet */
			return (0);

		iso9660->seenJoliet = level;

	} else /* not joliet */
		return (0);

	logical_block_size =
	    archive_le16dec(h + SVD_logical_block_size_offset);
	volume_block = archive_le32dec(h + SVD_volume_space_size_offset);

	iso9660->logical_block_size = logical_block_size;
	iso9660->volume_block = volume_block;
	iso9660->volume_size = logical_block_size * (uint64_t)volume_block;
	/* Read Root Directory Record in Volume Descriptor. */
	p = h + SVD_root_directory_record_offset;
	iso9660->joliet.location = archive_le32dec(p + DR_extent_offset);
	iso9660->joliet.size = archive_le32dec(p + DR_size_offset);

	return (48);
}

static int
isSVD(struct iso9660 *iso9660, const unsigned char *h)
{
	const unsigned char *p;
	ssize_t logical_block_size;
	int32_t volume_block;
	int32_t location;

	(void)iso9660; /* UNUSED */

	/* Type 2 means it's a SVD. */
	if (h[SVD_type_offset] != 2)
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, SVD_reserved1_offset, SVD_reserved1_size))
		return (0);
	if (!isNull(iso9660, h, SVD_reserved2_offset, SVD_reserved2_size))
		return (0);
	if (!isNull(iso9660, h, SVD_reserved3_offset, SVD_reserved3_size))
		return (0);

	/* File structure version must be 1 for ISO9660/ECMA119. */
	if (h[SVD_file_structure_version_offset] != 1)
		return (0);

	logical_block_size =
	    archive_le16dec(h + SVD_logical_block_size_offset);
	if (logical_block_size <= 0)
		return (0);

	volume_block = archive_le32dec(h + SVD_volume_space_size_offset);
	if (volume_block <= SYSTEM_AREA_BLOCK+4)
		return (0);

	/* Location of Occurrence of Type L Path Table must be
	 * available location,
	 * >= SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_le32dec(h+SVD_type_L_path_table_offset);
	if (location < SYSTEM_AREA_BLOCK+2 || location >= volume_block)
		return (0);

	/* The Type M Path Table must be at a valid location (WinISO
	 * and probably other programs omit this, so we allow zero)
	 *
	 * >= SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_be32dec(h+SVD_type_M_path_table_offset);
	if ((location > 0 && location < SYSTEM_AREA_BLOCK+2)
	    || location >= volume_block)
		return (0);

	/* Read Root Directory Record in Volume Descriptor. */
	p = h + SVD_root_directory_record_offset;
	if (p[DR_length_offset] != 34)
		return (0);

	return (48);
}

static int
isEVD(struct iso9660 *iso9660, const unsigned char *h)
{
	const unsigned char *p;
	ssize_t logical_block_size;
	int32_t volume_block;
	int32_t location;

	(void)iso9660; /* UNUSED */

	/* Type of the Enhanced Volume Descriptor must be 2. */
	if (h[PVD_type_offset] != 2)
		return (0);

	/* EVD version must be 2. */
	if (h[PVD_version_offset] != 2)
		return (0);

	/* Reserved field must be 0. */
	if (h[PVD_reserved1_offset] != 0)
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved2_offset, PVD_reserved2_size))
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved3_offset, PVD_reserved3_size))
		return (0);

	/* Logical block size must be > 0. */
	/* I've looked at Ecma 119 and can't find any stronger
	 * restriction on this field. */
	logical_block_size =
	    archive_le16dec(h + PVD_logical_block_size_offset);
	if (logical_block_size <= 0)
		return (0);

	volume_block =
	    archive_le32dec(h + PVD_volume_space_size_offset);
	if (volume_block <= SYSTEM_AREA_BLOCK+4)
		return (0);

	/* File structure version must be 2 for ISO9660:1999. */
	if (h[PVD_file_structure_version_offset] != 2)
		return (0);

	/* Location of Occurrence of Type L Path Table must be
	 * available location,
	 * >= SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_le32dec(h+PVD_type_1_path_table_offset);
	if (location < SYSTEM_AREA_BLOCK+2 || location >= volume_block)
		return (0);

	/* Location of Occurrence of Type M Path Table must be
	 * available location,
	 * >= SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_be32dec(h+PVD_type_m_path_table_offset);
	if ((location > 0 && location < SYSTEM_AREA_BLOCK+2)
	    || location >= volume_block)
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved4_offset, PVD_reserved4_size))
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved5_offset, PVD_reserved5_size))
		return (0);

	/* Read Root Directory Record in Volume Descriptor. */
	p = h + PVD_root_directory_record_offset;
	if (p[DR_length_offset] != 34)
		return (0);

	return (48);
}

static int
isPVD(struct iso9660 *iso9660, const unsigned char *h)
{
	const unsigned char *p;
	ssize_t logical_block_size;
	int32_t volume_block;
	int32_t location;
	int i;

	/* Type of the Primary Volume Descriptor must be 1. */
	if (h[PVD_type_offset] != 1)
		return (0);

	/* PVD version must be 1. */
	if (h[PVD_version_offset] != 1)
		return (0);

	/* Reserved field must be 0. */
	if (h[PVD_reserved1_offset] != 0)
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved2_offset, PVD_reserved2_size))
		return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved3_offset, PVD_reserved3_size))
		return (0);

	/* Logical block size must be > 0. */
	/* I've looked at Ecma 119 and can't find any stronger
	 * restriction on this field. */
	logical_block_size =
	    archive_le16dec(h + PVD_logical_block_size_offset);
	if (logical_block_size <= 0)
		return (0);

	volume_block = archive_le32dec(h + PVD_volume_space_size_offset);
	if (volume_block <= SYSTEM_AREA_BLOCK+4)
		return (0);

	/* File structure version must be 1 for ISO9660/ECMA119. */
	if (h[PVD_file_structure_version_offset] != 1)
		return (0);

	/* Location of Occurrence of Type L Path Table must be
	 * available location,
	 * > SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_le32dec(h+PVD_type_1_path_table_offset);
	if (location < SYSTEM_AREA_BLOCK+2 || location >= volume_block)
		return (0);

	/* The Type M Path Table must also be at a valid location
	 * (although ECMA 119 requires a Type M Path Table, WinISO and
	 * probably other programs omit it, so we permit a zero here)
	 *
	 * >= SYSTEM_AREA_BLOCK(16) + 2 and < Volume Space Size. */
	location = archive_be32dec(h+PVD_type_m_path_table_offset);
	if ((location > 0 && location < SYSTEM_AREA_BLOCK+2)
	    || location >= volume_block)
		return (0);

	/* Reserved field must be 0. */
	/* But accept NetBSD/FreeBSD "makefs" images with 0x20 here. */
	for (i = 0; i < PVD_reserved4_size; ++i)
		if (h[PVD_reserved4_offset + i] != 0
		    && h[PVD_reserved4_offset + i] != 0x20)
			return (0);

	/* Reserved field must be 0. */
	if (!isNull(iso9660, h, PVD_reserved5_offset, PVD_reserved5_size))
		return (0);

	/* XXX TODO: Check other values for sanity; reject more
	 * malformed PVDs. XXX */

	/* Read Root Directory Record in Volume Descriptor. */
	p = h + PVD_root_directory_record_offset;
	if (p[DR_length_offset] != 34)
		return (0);

	if (!iso9660->primary.location) {
		iso9660->logical_block_size = logical_block_size;
		iso9660->volume_block = volume_block;
		iso9660->volume_size =
		    logical_block_size * (uint64_t)volume_block;
		iso9660->primary.location =
		    archive_le32dec(p + DR_extent_offset);
		iso9660->primary.size = archive_le32dec(p + DR_size_offset);
	}

	return (48);
}

static int
read_children(struct archive_read *a, struct file_info *parent)
{
	struct iso9660 *iso9660;
	const unsigned char *b, *p;
	struct file_info *multi;
	size_t step, skip_size;

	iso9660 = (struct iso9660 *)(a->format->data);
	/* flush any remaining bytes from the last round to ensure
	 * we're positioned */
	if (iso9660->entry_bytes_unconsumed) {
		__archive_read_consume(a, iso9660->entry_bytes_unconsumed);
		iso9660->entry_bytes_unconsumed = 0;
	}
	if (iso9660->current_position > parent->offset) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Ignoring out-of-order directory (%s) %jd > %jd",
		    parent->name.s,
		    (intmax_t)iso9660->current_position,
		    (intmax_t)parent->offset);
		return (ARCHIVE_WARN);
	}
	if (parent->offset + parent->size > iso9660->volume_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Directory is beyond end-of-media: %s",
		    parent->name.s);
		return (ARCHIVE_WARN);
	}
	if (iso9660->current_position < parent->offset) {
		int64_t skipsize;

		skipsize = parent->offset - iso9660->current_position;
		skipsize = __archive_read_consume(a, skipsize);
		if (skipsize < 0)
			return ((int)skipsize);
		iso9660->current_position = parent->offset;
	}

	step = (size_t)(((parent->size + iso9660->logical_block_size -1) /
	    iso9660->logical_block_size) * iso9660->logical_block_size);
	b = __archive_read_ahead(a, step, NULL);
	if (b == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Failed to read full block when scanning "
		    "ISO9660 directory list");
		return (ARCHIVE_FATAL);
	}
	iso9660->current_position += step;
	multi = NULL;
	skip_size = step;
	while (step) {
		p = b;
		b += iso9660->logical_block_size;
		step -= iso9660->logical_block_size;
		for (; *p != 0 && p < b && p + *p <= b; p += *p) {
			struct file_info *child;

			/* N.B.: these special directory identifiers
			 * are 8 bit "values" even on a
			 * Joliet CD with UCS-2 (16bit) encoding.
			 */

			/* Skip '.' entry. */
			if (*(p + DR_name_len_offset) == 1
			    && *(p + DR_name_offset) == '\0')
				continue;
			/* Skip '..' entry. */
			if (*(p + DR_name_len_offset) == 1
			    && *(p + DR_name_offset) == '\001')
				continue;
			child = parse_file_info(a, parent, p, b - p);
			if (child == NULL) {
				__archive_read_consume(a, skip_size);
				return (ARCHIVE_FATAL);
			}
			if (child->cl_offset == 0 &&
			    (child->multi_extent || multi != NULL)) {
				struct content *con;

				if (multi == NULL) {
					multi = child;
					multi->contents.first = NULL;
					multi->contents.last =
					    &(multi->contents.first);
				}
				con = malloc(sizeof(struct content));
				if (con == NULL) {
					archive_set_error(
					    &a->archive, ENOMEM,
					    "No memory for multi extent");
					__archive_read_consume(a, skip_size);
					return (ARCHIVE_FATAL);
				}
				con->offset = child->offset;
				con->size = child->size;
				con->next = NULL;
				*multi->contents.last = con;
				multi->contents.last = &(con->next);
				if (multi == child) {
					if (add_entry(a, iso9660, child)
					    != ARCHIVE_OK)
						return (ARCHIVE_FATAL);
				} else {
					multi->size += child->size;
					if (!child->multi_extent)
						multi = NULL;
				}
			} else
				if (add_entry(a, iso9660, child) != ARCHIVE_OK)
					return (ARCHIVE_FATAL);
		}
	}

	__archive_read_consume(a, skip_size);

	/* Read data which recorded by RRIP "CE" extension. */
	if (read_CE(a, iso9660) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	return (ARCHIVE_OK);
}

static int
choose_volume(struct archive_read *a, struct iso9660 *iso9660)
{
	struct file_info *file;
	int64_t skipsize;
	struct vd *vd;
	const void *block;
	char seenJoliet;

	vd = &(iso9660->primary);
	if (!iso9660->opt_support_joliet)
		iso9660->seenJoliet = 0;
	if (iso9660->seenJoliet &&
		vd->location > iso9660->joliet.location)
		/* This condition is unlikely; by way of caution. */
		vd = &(iso9660->joliet);

	skipsize = LOGICAL_BLOCK_SIZE * (int64_t)vd->location;
	skipsize = __archive_read_consume(a, skipsize);
	if (skipsize < 0)
		return ((int)skipsize);
	iso9660->current_position = skipsize;

	block = __archive_read_ahead(a, vd->size, NULL);
	if (block == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Failed to read full block when scanning "
		    "ISO9660 directory list");
		return (ARCHIVE_FATAL);
	}

	/*
	 * While reading Root Directory, flag seenJoliet must be zero to
	 * avoid converting special name 0x00(Current Directory) and
	 * next byte to UCS2.
	 */
	seenJoliet = iso9660->seenJoliet;/* Save flag. */
	iso9660->seenJoliet = 0;
	file = parse_file_info(a, NULL, block, vd->size);
	if (file == NULL)
		return (ARCHIVE_FATAL);
	iso9660->seenJoliet = seenJoliet;

	/*
	 * If the iso image has both RockRidge and Joliet, we preferentially
	 * use RockRidge Extensions rather than Joliet ones.
	 */
	if (vd == &(iso9660->primary) && iso9660->seenRockridge
	    && iso9660->seenJoliet)
		iso9660->seenJoliet = 0;

	if (vd == &(iso9660->primary) && !iso9660->seenRockridge
	    && iso9660->seenJoliet) {
		/* Switch reading data from primary to joliet. */
		vd = &(iso9660->joliet);
		skipsize = LOGICAL_BLOCK_SIZE * (int64_t)vd->location;
		skipsize -= iso9660->current_position;
		skipsize = __archive_read_consume(a, skipsize);
		if (skipsize < 0)
			return ((int)skipsize);
		iso9660->current_position += skipsize;

		block = __archive_read_ahead(a, vd->size, NULL);
		if (block == NULL) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Failed to read full block when scanning "
			    "ISO9660 directory list");
			return (ARCHIVE_FATAL);
		}
		iso9660->seenJoliet = 0;
		file = parse_file_info(a, NULL, block, vd->size);
		if (file == NULL)
			return (ARCHIVE_FATAL);
		iso9660->seenJoliet = seenJoliet;
	}

	/* Store the root directory in the pending list. */
	if (add_entry(a, iso9660, file) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	if (iso9660->seenRockridge) {
		a->archive.archive_format = ARCHIVE_FORMAT_ISO9660_ROCKRIDGE;
		a->archive.archive_format_name =
		    "ISO9660 with Rockridge extensions";
	}

	return (ARCHIVE_OK);
}

static int
archive_read_format_iso9660_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	struct iso9660 *iso9660;
	struct file_info *file;
	int r, rd_r = ARCHIVE_OK;

	iso9660 = (struct iso9660 *)(a->format->data);

	if (!a->archive.archive_format) {
		a->archive.archive_format = ARCHIVE_FORMAT_ISO9660;
		a->archive.archive_format_name = "ISO9660";
	}

	if (iso9660->current_position == 0) {
		r = choose_volume(a, iso9660);
		if (r != ARCHIVE_OK)
			return (r);
	}

	file = NULL;/* Eliminate a warning. */
	/* Get the next entry that appears after the current offset. */
	r = next_entry_seek(a, iso9660, &file);
	if (r != ARCHIVE_OK)
		return (r);

	if (iso9660->seenJoliet) {
		/*
		 * Convert UTF-16BE of a filename to local locale MBS
		 * and store the result into a filename field.
		 */
		if (iso9660->sconv_utf16be == NULL) {
			iso9660->sconv_utf16be =
			    archive_string_conversion_from_charset(
				&(a->archive), "UTF-16BE", 1);
			if (iso9660->sconv_utf16be == NULL)
				/* Couldn't allocate memory */
				return (ARCHIVE_FATAL);
		}
		if (iso9660->utf16be_path == NULL) {
			iso9660->utf16be_path = malloc(UTF16_NAME_MAX);
			if (iso9660->utf16be_path == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "No memory");
				return (ARCHIVE_FATAL);
			}
		}
		if (iso9660->utf16be_previous_path == NULL) {
			iso9660->utf16be_previous_path = malloc(UTF16_NAME_MAX);
			if (iso9660->utf16be_previous_path == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "No memory");
				return (ARCHIVE_FATAL);
			}
		}

		iso9660->utf16be_path_len = 0;
		if (build_pathname_utf16be(iso9660->utf16be_path,
		    UTF16_NAME_MAX, &(iso9660->utf16be_path_len), file) != 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Pathname is too long");
			return (ARCHIVE_FATAL);
		}

		r = archive_entry_copy_pathname_l(entry,
		    (const char *)iso9660->utf16be_path,
		    iso9660->utf16be_path_len,
		    iso9660->sconv_utf16be);
		if (r != 0) {
			if (errno == ENOMEM) {
				archive_set_error(&a->archive, ENOMEM,
				    "No memory for Pathname");
				return (ARCHIVE_FATAL);
			}
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Pathname cannot be converted "
			    "from %s to current locale.",
			    archive_string_conversion_charset_name(
			      iso9660->sconv_utf16be));

			rd_r = ARCHIVE_WARN;
		}
	} else {
		const char *path = build_pathname(&iso9660->pathname, file, 0);
		if (path == NULL) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Pathname is too long");
			return (ARCHIVE_FATAL);
		} else {
			archive_string_empty(&iso9660->pathname);
			archive_entry_set_pathname(entry, path);
		}
	}

	iso9660->entry_bytes_remaining = file->size;
	/* Offset for sparse-file-aware clients. */
	iso9660->entry_sparse_offset = 0;

	if (file->offset + file->size > iso9660->volume_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "File is beyond end-of-media: %s",
		    archive_entry_pathname(entry));
		iso9660->entry_bytes_remaining = 0;
		return (ARCHIVE_WARN);
	}

	/* Set up the entry structure with information about this entry. */
	archive_entry_set_mode(entry, file->mode);
	archive_entry_set_uid(entry, file->uid);
	archive_entry_set_gid(entry, file->gid);
	archive_entry_set_nlink(entry, file->nlinks);
	if (file->birthtime_is_set)
		archive_entry_set_birthtime(entry, file->birthtime, 0);
	else
		archive_entry_unset_birthtime(entry);
	archive_entry_set_mtime(entry, file->mtime, 0);
	archive_entry_set_ctime(entry, file->ctime, 0);
	archive_entry_set_atime(entry, file->atime, 0);
	/* N.B.: Rock Ridge supports 64-bit device numbers. */
	archive_entry_set_rdev(entry, (dev_t)file->rdev);
	archive_entry_set_size(entry, iso9660->entry_bytes_remaining);
	if (file->symlink.s != NULL)
		archive_entry_copy_symlink(entry, file->symlink.s);

	/* Note: If the input isn't seekable, we can't rewind to
	 * return the same body again, so if the next entry refers to
	 * the same data, we have to return it as a hardlink to the
	 * original entry. */
	if (file->number != -1 &&
	    file->number == iso9660->previous_number) {
		if (iso9660->seenJoliet) {
			r = archive_entry_copy_hardlink_l(entry,
			    (const char *)iso9660->utf16be_previous_path,
			    iso9660->utf16be_previous_path_len,
			    iso9660->sconv_utf16be);
			if (r != 0) {
				if (errno == ENOMEM) {
					archive_set_error(&a->archive, ENOMEM,
					    "No memory for Linkname");
					return (ARCHIVE_FATAL);
				}
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Linkname cannot be converted "
				    "from %s to current locale.",
				    archive_string_conversion_charset_name(
				      iso9660->sconv_utf16be));
				rd_r = ARCHIVE_WARN;
			}
		} else
			archive_entry_set_hardlink(entry,
			    iso9660->previous_pathname.s);
		archive_entry_unset_size(entry);
		iso9660->entry_bytes_remaining = 0;
		return (rd_r);
	}

	if ((file->mode & AE_IFMT) != AE_IFDIR &&
	    file->offset < iso9660->current_position) {
		int64_t r64;

		r64 = __archive_read_seek(a, file->offset, SEEK_SET);
		if (r64 != (int64_t)file->offset) {
			/* We can't seek backwards to extract it, so issue
			 * a warning.  Note that this can only happen if
			 * this entry was added to the heap after we passed
			 * this offset, that is, only if the directory
			 * mentioning this entry is later than the body of
			 * the entry. Such layouts are very unusual; most
			 * ISO9660 writers lay out and record all directory
			 * information first, then store all file bodies. */
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Ignoring out-of-order file @%jx (%s) %jd < %jd",
			    (intmax_t)file->number,
			    iso9660->pathname.s,
			    (intmax_t)file->offset,
			    (intmax_t)iso9660->current_position);
			iso9660->entry_bytes_remaining = 0;
			return (ARCHIVE_WARN);
		}
		iso9660->current_position = (uint64_t)r64;
	}

	/* Initialize zisofs variables. */
	iso9660->entry_zisofs.pz = file->pz;
	if (file->pz) {
#ifdef HAVE_ZLIB_H
		struct zisofs  *zisofs;

		zisofs = &iso9660->entry_zisofs;
		zisofs->initialized = 0;
		zisofs->pz_log2_bs = file->pz_log2_bs;
		zisofs->pz_uncompressed_size = file->pz_uncompressed_size;
		zisofs->pz_offset = 0;
		zisofs->header_avail = 0;
		zisofs->header_passed = 0;
		zisofs->block_pointers_avail = 0;
#endif
		archive_entry_set_size(entry, file->pz_uncompressed_size);
	}

	iso9660->previous_number = file->number;
	if (iso9660->seenJoliet) {
		memcpy(iso9660->utf16be_previous_path, iso9660->utf16be_path,
		    iso9660->utf16be_path_len);
		iso9660->utf16be_previous_path_len = iso9660->utf16be_path_len;
	} else
		archive_strcpy(
		    &iso9660->previous_pathname, iso9660->pathname.s);

	/* Reset entry_bytes_remaining if the file is multi extent. */
	iso9660->entry_content = file->contents.first;
	if (iso9660->entry_content != NULL)
		iso9660->entry_bytes_remaining = iso9660->entry_content->size;

	if (archive_entry_filetype(entry) == AE_IFDIR) {
		/* Overwrite nlinks by proper link number which is
		 * calculated from number of sub directories. */
		archive_entry_set_nlink(entry, 2 + file->subdirs);
		/* Directory data has been read completely. */
		iso9660->entry_bytes_remaining = 0;
	}

	if (rd_r != ARCHIVE_OK)
		return (rd_r);
	return (ARCHIVE_OK);
}

static int
archive_read_format_iso9660_read_data_skip(struct archive_read *a)
{
	/* Because read_next_header always does an explicit skip
	 * to the next entry, we don't need to do anything here. */
	(void)a; /* UNUSED */
	return (ARCHIVE_OK);
}

#ifdef HAVE_ZLIB_H

static int
zisofs_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	struct iso9660 *iso9660;
	struct zisofs  *zisofs;
	const unsigned char *p;
	size_t avail;
	ssize_t bytes_read;
	size_t uncompressed_size;
	int r;

	iso9660 = (struct iso9660 *)(a->format->data);
	zisofs = &iso9660->entry_zisofs;

	p = __archive_read_ahead(a, 1, &bytes_read);
	if (bytes_read <= 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Truncated zisofs file body");
		return (ARCHIVE_FATAL);
	}
	if (bytes_read > iso9660->entry_bytes_remaining)
		bytes_read = (ssize_t)iso9660->entry_bytes_remaining;
	avail = bytes_read;
	uncompressed_size = 0;

	if (!zisofs->initialized) {
		size_t ceil, xsize;

		/* Allocate block pointers buffer. */
		ceil = (size_t)((zisofs->pz_uncompressed_size +
			(((int64_t)1) << zisofs->pz_log2_bs) - 1)
			>> zisofs->pz_log2_bs);
		xsize = (ceil + 1) * 4;
		if (zisofs->block_pointers_alloc < xsize) {
			size_t alloc;

			if (zisofs->block_pointers != NULL)
				free(zisofs->block_pointers);
			alloc = ((xsize >> 10) + 1) << 10;
			zisofs->block_pointers = malloc(alloc);
			if (zisofs->block_pointers == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "No memory for zisofs decompression");
				return (ARCHIVE_FATAL);
			}
			zisofs->block_pointers_alloc = alloc;
		}
		zisofs->block_pointers_size = xsize;

		/* Allocate uncompressed data buffer. */
		xsize = (size_t)1UL << zisofs->pz_log2_bs;
		if (zisofs->uncompressed_buffer_size < xsize) {
			if (zisofs->uncompressed_buffer != NULL)
				free(zisofs->uncompressed_buffer);
			zisofs->uncompressed_buffer = malloc(xsize);
			if (zisofs->uncompressed_buffer == NULL) {
				archive_set_error(&a->archive, ENOMEM,
				    "No memory for zisofs decompression");
				return (ARCHIVE_FATAL);
			}
		}
		zisofs->uncompressed_buffer_size = xsize;

		/*
		 * Read the file header, and check the magic code of zisofs.
		 */
		if (zisofs->header_avail < sizeof(zisofs->header)) {
			xsize = sizeof(zisofs->header) - zisofs->header_avail;
			if (avail < xsize)
				xsize = avail;
			memcpy(zisofs->header + zisofs->header_avail, p, xsize);
			zisofs->header_avail += xsize;
			avail -= xsize;
			p += xsize;
		}
		if (!zisofs->header_passed &&
		    zisofs->header_avail == sizeof(zisofs->header)) {
			int err = 0;

			if (memcmp(zisofs->header, zisofs_magic,
			    sizeof(zisofs_magic)) != 0)
				err = 1;
			if (archive_le32dec(zisofs->header + 8)
			    != zisofs->pz_uncompressed_size)
				err = 1;
			if (zisofs->header[12] != 4)
				err = 1;
			if (zisofs->header[13] != zisofs->pz_log2_bs)
				err = 1;
			if (err) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Illegal zisofs file body");
				return (ARCHIVE_FATAL);
			}
			zisofs->header_passed = 1;
		}
		/*
		 * Read block pointers.
		 */
		if (zisofs->header_passed &&
		    zisofs->block_pointers_avail < zisofs->block_pointers_size) {
			xsize = zisofs->block_pointers_size
			    - zisofs->block_pointers_avail;
			if (avail < xsize)
				xsize = avail;
			memcpy(zisofs->block_pointers
			    + zisofs->block_pointers_avail, p, xsize);
			zisofs->block_pointers_avail += xsize;
			avail -= xsize;
			p += xsize;
		    	if (zisofs->block_pointers_avail
			    == zisofs->block_pointers_size) {
				/* We've got all block pointers and initialize
				 * related variables.	*/
				zisofs->block_off = 0;
				zisofs->block_avail = 0;
				/* Complete a initialization */
				zisofs->initialized = 1;
			}
		}

		if (!zisofs->initialized)
			goto next_data; /* We need more data. */
	}

	/*
	 * Get block offsets from block pointers.
	 */
	if (zisofs->block_avail == 0) {
		uint32_t bst, bed;

		if (zisofs->block_off + 4 >= zisofs->block_pointers_size) {
			/* There isn't a pair of offsets. */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Illegal zisofs block pointers");
			return (ARCHIVE_FATAL);
		}
		bst = archive_le32dec(
		    zisofs->block_pointers + zisofs->block_off);
		if (bst != zisofs->pz_offset + (bytes_read - avail)) {
			/* TODO: Should we seek offset of current file
			 * by bst ? */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Illegal zisofs block pointers(cannot seek)");
			return (ARCHIVE_FATAL);
		}
		bed = archive_le32dec(
		    zisofs->block_pointers + zisofs->block_off + 4);
		if (bed < bst) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Illegal zisofs block pointers");
			return (ARCHIVE_FATAL);
		}
		zisofs->block_avail = bed - bst;
		zisofs->block_off += 4;

		/* Initialize compression library for new block. */
		if (zisofs->stream_valid)
			r = inflateReset(&zisofs->stream);
		else
			r = inflateInit(&zisofs->stream);
		if (r != Z_OK) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Can't initialize zisofs decompression.");
			return (ARCHIVE_FATAL);
		}
		zisofs->stream_valid = 1;
		zisofs->stream.total_in = 0;
		zisofs->stream.total_out = 0;
	}

	/*
	 * Make uncompressed data.
	 */
	if (zisofs->block_avail == 0) {
		memset(zisofs->uncompressed_buffer, 0,
		    zisofs->uncompressed_buffer_size);
		uncompressed_size = zisofs->uncompressed_buffer_size;
	} else {
		zisofs->stream.next_in = (Bytef *)(uintptr_t)(const void *)p;
		if (avail > zisofs->block_avail)
			zisofs->stream.avail_in = zisofs->block_avail;
		else
			zisofs->stream.avail_in = (uInt)avail;
		zisofs->stream.next_out = zisofs->uncompressed_buffer;
		zisofs->stream.avail_out =
		    (uInt)zisofs->uncompressed_buffer_size;

		r = inflate(&zisofs->stream, 0);
		switch (r) {
		case Z_OK: /* Decompressor made some progress.*/
		case Z_STREAM_END: /* Found end of stream. */
			break;
		default:
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "zisofs decompression failed (%d)", r);
			return (ARCHIVE_FATAL);
		}
		uncompressed_size =
		    zisofs->uncompressed_buffer_size - zisofs->stream.avail_out;
		avail -= zisofs->stream.next_in - p;
		zisofs->block_avail -= (uint32_t)(zisofs->stream.next_in - p);
	}
next_data:
	bytes_read -= avail;
	*buff = zisofs->uncompressed_buffer;
	*size = uncompressed_size;
	*offset = iso9660->entry_sparse_offset;
	iso9660->entry_sparse_offset += uncompressed_size;
	iso9660->entry_bytes_remaining -= bytes_read;
	iso9660->current_position += bytes_read;
	zisofs->pz_offset += (uint32_t)bytes_read;
	iso9660->entry_bytes_unconsumed += bytes_read;

	return (ARCHIVE_OK);
}

#else /* HAVE_ZLIB_H */

static int
zisofs_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{

	(void)buff;/* UNUSED */
	(void)size;/* UNUSED */
	(void)offset;/* UNUSED */
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "zisofs is not supported on this platform.");
	return (ARCHIVE_FAILED);
}

#endif /* HAVE_ZLIB_H */

static int
archive_read_format_iso9660_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	ssize_t bytes_read;
	struct iso9660 *iso9660;

	iso9660 = (struct iso9660 *)(a->format->data);

	if (iso9660->entry_bytes_unconsumed) {
		__archive_read_consume(a, iso9660->entry_bytes_unconsumed);
		iso9660->entry_bytes_unconsumed = 0;
	}

	if (iso9660->entry_bytes_remaining <= 0) {
		if (iso9660->entry_content != NULL)
			iso9660->entry_content = iso9660->entry_content->next;
		if (iso9660->entry_content == NULL) {
			*buff = NULL;
			*size = 0;
			*offset = iso9660->entry_sparse_offset;
			return (ARCHIVE_EOF);
		}
		/* Seek forward to the start of the entry. */
		if (iso9660->current_position < iso9660->entry_content->offset) {
			int64_t step;

			step = iso9660->entry_content->offset -
			    iso9660->current_position;
			step = __archive_read_consume(a, step);
			if (step < 0)
				return ((int)step);
			iso9660->current_position =
			    iso9660->entry_content->offset;
		}
		if (iso9660->entry_content->offset < iso9660->current_position) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Ignoring out-of-order file (%s) %jd < %jd",
			    iso9660->pathname.s,
			    (intmax_t)iso9660->entry_content->offset,
			    (intmax_t)iso9660->current_position);
			*buff = NULL;
			*size = 0;
			*offset = iso9660->entry_sparse_offset;
			return (ARCHIVE_WARN);
		}
		iso9660->entry_bytes_remaining = iso9660->entry_content->size;
	}
	if (iso9660->entry_zisofs.pz)
		return (zisofs_read_data(a, buff, size, offset));

	*buff = __archive_read_ahead(a, 1, &bytes_read);
	if (bytes_read == 0)
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Truncated input file");
	if (*buff == NULL)
		return (ARCHIVE_FATAL);
	if (bytes_read > iso9660->entry_bytes_remaining)
		bytes_read = (ssize_t)iso9660->entry_bytes_remaining;
	*size = bytes_read;
	*offset = iso9660->entry_sparse_offset;
	iso9660->entry_sparse_offset += bytes_read;
	iso9660->entry_bytes_remaining -= bytes_read;
	iso9660->entry_bytes_unconsumed = bytes_read;
	iso9660->current_position += bytes_read;
	return (ARCHIVE_OK);
}

static int
archive_read_format_iso9660_cleanup(struct archive_read *a)
{
	struct iso9660 *iso9660;
	int r = ARCHIVE_OK;

	iso9660 = (struct iso9660 *)(a->format->data);
	release_files(iso9660);
	free(iso9660->read_ce_req.reqs);
	archive_string_free(&iso9660->pathname);
	archive_string_free(&iso9660->previous_pathname);
	free(iso9660->pending_files.files);
#ifdef HAVE_ZLIB_H
	free(iso9660->entry_zisofs.uncompressed_buffer);
	free(iso9660->entry_zisofs.block_pointers);
	if (iso9660->entry_zisofs.stream_valid) {
		if (inflateEnd(&iso9660->entry_zisofs.stream) != Z_OK) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Failed to clean up zlib decompressor");
			r = ARCHIVE_FATAL;
		}
	}
#endif
	free(iso9660->utf16be_path);
	free(iso9660->utf16be_previous_path);
	free(iso9660);
	(a->format->data) = NULL;
	return (r);
}

/*
 * This routine parses a single ISO directory record, makes sense
 * of any extensions, and stores the result in memory.
 */
static struct file_info *
parse_file_info(struct archive_read *a, struct file_info *parent,
    const unsigned char *isodirrec, size_t reclen)
{
	struct iso9660 *iso9660;
	struct file_info *file, *filep;
	size_t name_len;
	const unsigned char *rr_start, *rr_end;
	const unsigned char *p;
	size_t dr_len;
	uint64_t fsize, offset;
	int32_t location;
	int flags;

	iso9660 = (struct iso9660 *)(a->format->data);

	if (reclen != 0)
		dr_len = (size_t)isodirrec[DR_length_offset];
	/*
	 * Sanity check that reclen is not zero and dr_len is greater than
	 * reclen but at least 34
	 */
	if (reclen == 0 || reclen < dr_len || dr_len < 34) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			"Invalid length of directory record");
		return (NULL);
	}
	name_len = (size_t)isodirrec[DR_name_len_offset];
	location = archive_le32dec(isodirrec + DR_extent_offset);
	fsize = toi(isodirrec + DR_size_offset, DR_size_size);
	/* Sanity check that name_len doesn't exceed dr_len. */
	if (dr_len - 33 < name_len || name_len == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid length of file identifier");
		return (NULL);
	}
	/* Sanity check that location doesn't exceed volume block.
	 * Don't check lower limit of location; it's possibility
	 * the location has negative value when file type is symbolic
	 * link or file size is zero. As far as I know latest mkisofs
	 * do that.
	 */
	if (location > 0 &&
	    (location + ((fsize + iso9660->logical_block_size -1)
	       / iso9660->logical_block_size))
			> (uint32_t)iso9660->volume_block) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid location of extent of file");
		return (NULL);
	}
	/* Sanity check that location doesn't have a negative value
	 * when the file is not empty. it's too large. */
	if (fsize != 0 && location < 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid location of extent of file");
		return (NULL);
	}

	/* Sanity check that this entry does not create a cycle. */
	offset = iso9660->logical_block_size * (uint64_t)location;
	for (filep = parent; filep != NULL; filep = filep->parent) {
		if (filep->offset == offset) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Directory structure contains loop");
			return (NULL);
		}
	}

	/* Create a new file entry and copy data from the ISO dir record. */
	file = (struct file_info *)calloc(1, sizeof(*file));
	if (file == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "No memory for file entry");
		return (NULL);
	}
	file->parent = parent;
	file->offset = offset;
	file->size = fsize;
	file->mtime = isodate7(isodirrec + DR_date_offset);
	file->ctime = file->atime = file->mtime;
	file->rede_files.first = NULL;
	file->rede_files.last = &(file->rede_files.first);

	p = isodirrec + DR_name_offset;
	/* Rockridge extensions (if any) follow name.  Compute this
	 * before fidgeting the name_len below. */
	rr_start = p + name_len + (name_len & 1 ? 0 : 1);
	rr_end = isodirrec + dr_len;

	if (iso9660->seenJoliet) {
		/* Joliet names are max 64 chars (128 bytes) according to spec,
		 * but genisoimage/mkisofs allows recording longer Joliet
		 * names which are 103 UCS2 characters(206 bytes) by their
		 * option '-joliet-long'.
		 */
		if (name_len > 206)
			name_len = 206;
		name_len &= ~1;

		/* trim trailing first version and dot from filename.
		 *
		 * Remember we were in UTF-16BE land!
		 * SEPARATOR 1 (.) and SEPARATOR 2 (;) are both
		 * 16 bits big endian characters on Joliet.
		 *
		 * TODO: sanitize filename?
		 *       Joliet allows any UCS-2 char except:
		 *       *, /, :, ;, ? and \.
		 */
		/* Chop off trailing ';1' from files. */
		if (name_len > 4 && p[name_len-4] == 0 && p[name_len-3] == ';'
		    && p[name_len-2] == 0 && p[name_len-1] == '1')
			name_len -= 4;
#if 0 /* XXX: this somehow manages to strip of single-character file extensions, like '.c'. */
		/* Chop off trailing '.' from filenames. */
		if (name_len > 2 && p[name_len-2] == 0 && p[name_len-1] == '.')
			name_len -= 2;
#endif
		if ((file->utf16be_name = malloc(name_len)) == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "No memory for file name");
			goto fail;
		}
		memcpy(file->utf16be_name, p, name_len);
		file->utf16be_bytes = name_len;
	} else {
		/* Chop off trailing ';1' from files. */
		if (name_len > 2 && p[name_len - 2] == ';' &&
				p[name_len - 1] == '1')
			name_len -= 2;
		/* Chop off trailing '.' from filenames. */
		if (name_len > 1 && p[name_len - 1] == '.')
			--name_len;

		archive_strncpy(&file->name, (const char *)p, name_len);
	}

	flags = isodirrec[DR_flags_offset];
	if (flags & 0x02)
		file->mode = AE_IFDIR | 0700;
	else
		file->mode = AE_IFREG | 0400;
	if (flags & 0x80)
		file->multi_extent = 1;
	else
		file->multi_extent = 0;
	/*
	 * Use a location for the file number, which is treated as an inode
	 * number to find out hardlink target. If Rockridge extensions is
	 * being used, the file number will be overwritten by FILE SERIAL
	 * NUMBER of RRIP "PX" extension.
	 * Note: Old mkisofs did not record that FILE SERIAL NUMBER
	 * in ISO images.
	 * Note2: xorriso set 0 to the location of a symlink file. 
	 */
	if (file->size == 0 && location >= 0) {
		/* If file->size is zero, its location points wrong place,
		 * and so we should not use it for the file number.
		 * When the location has negative value, it can be used
		 * for the file number.
		 */
		file->number = -1;
		/* Do not appear before any directory entries. */
		file->offset = -1;
	} else
		file->number = (int64_t)(uint32_t)location;

	/* Rockridge extensions overwrite information from above. */
	if (iso9660->opt_support_rockridge) {
		if (parent == NULL && rr_end - rr_start >= 7) {
			p = rr_start;
			if (memcmp(p, "SP\x07\x01\xbe\xef", 6) == 0) {
				/*
				 * SP extension stores the suspOffset
				 * (Number of bytes to skip between
				 * filename and SUSP records.)
				 * It is mandatory by the SUSP standard
				 * (IEEE 1281).
				 *
				 * It allows SUSP to coexist with
				 * non-SUSP uses of the System
				 * Use Area by placing non-SUSP data
				 * before SUSP data.
				 *
				 * SP extension must be in the root
				 * directory entry, disable all SUSP
				 * processing if not found.
				 */
				iso9660->suspOffset = p[6];
				iso9660->seenSUSP = 1;
				rr_start += 7;
			}
		}
		if (iso9660->seenSUSP) {
			int r;

			file->name_continues = 0;
			file->symlink_continues = 0;
			rr_start += iso9660->suspOffset;
			r = parse_rockridge(a, file, rr_start, rr_end);
			if (r != ARCHIVE_OK)
				goto fail;
			/*
			 * A file size of symbolic link files in ISO images
			 * made by makefs is not zero and its location is
			 * the same as those of next regular file. That is
			 * the same as hard like file and it causes unexpected
			 * error. 
			 */
			if (file->size > 0 &&
			    (file->mode & AE_IFMT) == AE_IFLNK) {
				file->size = 0;
				file->number = -1;
				file->offset = -1;
			}
		} else
			/* If there isn't SUSP, disable parsing
			 * rock ridge extensions. */
			iso9660->opt_support_rockridge = 0;
	}

	file->nlinks = 1;/* Reset nlink. we'll calculate it later. */
	/* Tell file's parent how many children that parent has. */
	if (parent != NULL && (flags & 0x02))
		parent->subdirs++;

	if (iso9660->seenRockridge) {
		if (parent != NULL && parent->parent == NULL &&
		    (flags & 0x02) && iso9660->rr_moved == NULL &&
		    file->name.s &&
		    (strcmp(file->name.s, "rr_moved") == 0 ||
		     strcmp(file->name.s, ".rr_moved") == 0)) {
			iso9660->rr_moved = file;
			file->rr_moved = 1;
			file->rr_moved_has_re_only = 1;
			file->re = 0;
			parent->subdirs--;
		} else if (file->re) {
			/*
			 * Sanity check: file's parent is rr_moved.
			 */
			if (parent == NULL || parent->rr_moved == 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge RE");
				goto fail;
			}
			/*
			 * Sanity check: file does not have "CL" extension.
			 */
			if (file->cl_offset) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge RE and CL");
				goto fail;
			}
			/*
			 * Sanity check: The file type must be a directory.
			 */
			if ((flags & 0x02) == 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge RE");
				goto fail;
			}
		} else if (parent != NULL && parent->rr_moved)
			file->rr_moved_has_re_only = 0;
		else if (parent != NULL && (flags & 0x02) &&
		    (parent->re || parent->re_descendant))
			file->re_descendant = 1;
		if (file->cl_offset) {
			struct file_info *r;

			if (parent == NULL || parent->parent == NULL) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge CL");
				goto fail;
			}
			/*
			 * Sanity check: The file type must be a regular file.
			 */
			if ((flags & 0x02) != 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge CL");
				goto fail;
			}
			parent->subdirs++;
			/* Overwrite an offset and a number of this "CL" entry
			 * to appear before other dirs. "+1" to those is to
			 * make sure to appear after "RE" entry which this
			 * "CL" entry should be connected with. */
			file->offset = file->number = file->cl_offset + 1;

			/*
			 * Sanity check: cl_offset does not point at its
			 * the parents or itself.
			 */
			for (r = parent; r; r = r->parent) {
				if (r->offset == file->cl_offset) {
					archive_set_error(&a->archive,
					    ARCHIVE_ERRNO_MISC,
					    "Invalid Rockridge CL");
					goto fail;
				}
			}
			if (file->cl_offset == file->offset ||
			    parent->rr_moved) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Invalid Rockridge CL");
				goto fail;
			}
		}
	}

#if DEBUG
	/* DEBUGGING: Warn about attributes I don't yet fully support. */
	if ((flags & ~0x02) != 0) {
		fprintf(stderr, "\n ** Unrecognized flag: ");
		dump_isodirrec(stderr, isodirrec);
		fprintf(stderr, "\n");
	} else if (toi(isodirrec + DR_volume_sequence_number_offset, 2) != 1) {
		fprintf(stderr, "\n ** Unrecognized sequence number: ");
		dump_isodirrec(stderr, isodirrec);
		fprintf(stderr, "\n");
	} else if (*(isodirrec + DR_file_unit_size_offset) != 0) {
		fprintf(stderr, "\n ** Unexpected file unit size: ");
		dump_isodirrec(stderr, isodirrec);
		fprintf(stderr, "\n");
	} else if (*(isodirrec + DR_interleave_offset) != 0) {
		fprintf(stderr, "\n ** Unexpected interleave: ");
		dump_isodirrec(stderr, isodirrec);
		fprintf(stderr, "\n");
	} else if (*(isodirrec + DR_ext_attr_length_offset) != 0) {
		fprintf(stderr, "\n ** Unexpected extended attribute length: ");
		dump_isodirrec(stderr, isodirrec);
		fprintf(stderr, "\n");
	}
#endif
	register_file(iso9660, file);
	return (file);
fail:
	archive_string_free(&file->name);
	free(file);
	return (NULL);
}

static int
parse_rockridge(struct archive_read *a, struct file_info *file,
    const unsigned char *p, const unsigned char *end)
{
	struct iso9660 *iso9660;
	int entry_seen = 0;

	iso9660 = (struct iso9660 *)(a->format->data);

	while (p + 4 <= end  /* Enough space for another entry. */
	    && p[0] >= 'A' && p[0] <= 'Z' /* Sanity-check 1st char of name. */
	    && p[1] >= 'A' && p[1] <= 'Z' /* Sanity-check 2nd char of name. */
	    && p[2] >= 4 /* Sanity-check length. */
	    && p + p[2] <= end) { /* Sanity-check length. */
		const unsigned char *data = p + 4;
		int data_length = p[2] - 4;
		int version = p[3];

		switch(p[0]) {
		case 'C':
			if (p[1] == 'E') {
				if (version == 1 && data_length == 24) {
					/*
					 * CE extension comprises:
					 *   8 byte sector containing extension
					 *   8 byte offset w/in above sector
					 *   8 byte length of continuation
					 */
					int32_t location =
					    archive_le32dec(data);
					file->ce_offset =
					    archive_le32dec(data+8);
					file->ce_size =
					    archive_le32dec(data+16);
					if (register_CE(a, location, file)
					    != ARCHIVE_OK)
						return (ARCHIVE_FATAL);
				}
			}
			else if (p[1] == 'L') {
				if (version == 1 && data_length == 8) {
					file->cl_offset = (uint64_t)
					    iso9660->logical_block_size *
					    (uint64_t)archive_le32dec(data);
					iso9660->seenRockridge = 1;
				}
			}
			break;
		case 'N':
			if (p[1] == 'M') {
				if (version == 1) {
					parse_rockridge_NM1(file,
					    data, data_length);
					iso9660->seenRockridge = 1;
				}
			}
			break;
		case 'P':
			/*
			 * PD extension is padding;
			 * contents are always ignored.
			 *
			 * PL extension won't appear;
			 * contents are always ignored.
			 */
			if (p[1] == 'N') {
				if (version == 1 && data_length == 16) {
					file->rdev = toi(data,4);
					file->rdev <<= 32;
					file->rdev |= toi(data + 8, 4);
					iso9660->seenRockridge = 1;
				}
			}
			else if (p[1] == 'X') {
				/*
				 * PX extension comprises:
				 *   8 bytes for mode,
				 *   8 bytes for nlinks,
				 *   8 bytes for uid,
				 *   8 bytes for gid,
				 *   8 bytes for inode.
				 */
				if (version == 1) {
					if (data_length >= 8)
						file->mode
						    = toi(data, 4);
					if (data_length >= 16)
						file->nlinks
						    = toi(data + 8, 4);
					if (data_length >= 24)
						file->uid
						    = toi(data + 16, 4);
					if (data_length >= 32)
						file->gid
						    = toi(data + 24, 4);
					if (data_length >= 40)
						file->number
						    = toi(data + 32, 4);
					iso9660->seenRockridge = 1;
				}
			}
			break;
		case 'R':
			if (p[1] == 'E' && version == 1) {
				file->re = 1;
				iso9660->seenRockridge = 1;
			}
			else if (p[1] == 'R' && version == 1) {
				/*
				 * RR extension comprises:
				 *    one byte flag value
				 * This extension is obsolete,
				 * so contents are always ignored.
				 */
			}
			break;
		case 'S':
			if (p[1] == 'L') {
				if (version == 1) {
					parse_rockridge_SL1(file,
					    data, data_length);
					iso9660->seenRockridge = 1;
				}
			}
			else if (p[1] == 'T'
			    && data_length == 0 && version == 1) {
				/*
				 * ST extension marks end of this
				 * block of SUSP entries.
				 *
				 * It allows SUSP to coexist with
				 * non-SUSP uses of the System
				 * Use Area by placing non-SUSP data
				 * after SUSP data.
				 */
				iso9660->seenSUSP = 0;
				iso9660->seenRockridge = 0;
				return (ARCHIVE_OK);
			}
			break;
		case 'T':
			if (p[1] == 'F') {
				if (version == 1) {
					parse_rockridge_TF1(file,
					    data, data_length);
					iso9660->seenRockridge = 1;
				}
			}
			break;
		case 'Z':
			if (p[1] == 'F') {
				if (version == 1)
					parse_rockridge_ZF1(file,
					    data, data_length);
			}
			break;
		default:
			break;
		}

		p += p[2];
		entry_seen = 1;
	}

	if (entry_seen)
		return (ARCHIVE_OK);
	else {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				  "Tried to parse Rockridge extensions, but none found");
		return (ARCHIVE_WARN);
	}
}

static int
register_CE(struct archive_read *a, int32_t location,
    struct file_info *file)
{
	struct iso9660 *iso9660;
	struct read_ce_queue *heap;
	struct read_ce_req *p;
	uint64_t offset, parent_offset;
	int hole, parent;

	iso9660 = (struct iso9660 *)(a->format->data);
	offset = ((uint64_t)location) * (uint64_t)iso9660->logical_block_size;
	if (((file->mode & AE_IFMT) == AE_IFREG &&
	    offset >= file->offset) ||
	    offset < iso9660->current_position ||
	    (((uint64_t)file->ce_offset) + file->ce_size)
	      > (uint64_t)iso9660->logical_block_size ||
	    offset + file->ce_offset + file->ce_size
		  > iso9660->volume_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Invalid parameter in SUSP \"CE\" extension");
		return (ARCHIVE_FATAL);
	}

	/* Expand our CE list as necessary. */
	heap = &(iso9660->read_ce_req);
	if (heap->cnt >= heap->allocated) {
		int new_size;

		if (heap->allocated < 16)
			new_size = 16;
		else
			new_size = heap->allocated * 2;
		/* Overflow might keep us from growing the list. */
		if (new_size <= heap->allocated) {
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		p = calloc(new_size, sizeof(p[0]));
		if (p == NULL) {
			archive_set_error(&a->archive, ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		if (heap->reqs != NULL) {
			memcpy(p, heap->reqs, heap->cnt * sizeof(*p));
			free(heap->reqs);
		}
		heap->reqs = p;
		heap->allocated = new_size;
	}

	/*
	 * Start with hole at end, walk it up tree to find insertion point.
	 */
	hole = heap->cnt++;
	while (hole > 0) {
		parent = (hole - 1)/2;
		parent_offset = heap->reqs[parent].offset;
		if (offset >= parent_offset) {
			heap->reqs[hole].offset = offset;
			heap->reqs[hole].file = file;
			return (ARCHIVE_OK);
		}
		/* Move parent into hole <==> move hole up tree. */
		heap->reqs[hole] = heap->reqs[parent];
		hole = parent;
	}
	heap->reqs[0].offset = offset;
	heap->reqs[0].file = file;
	return (ARCHIVE_OK);
}

static void
next_CE(struct read_ce_queue *heap)
{
	uint64_t a_offset, b_offset, c_offset;
	int a, b, c;
	struct read_ce_req tmp;

	if (heap->cnt < 1)
		return;

	/*
	 * Move the last item in the heap to the root of the tree
	 */
	heap->reqs[0] = heap->reqs[--(heap->cnt)];

	/*
	 * Rebalance the heap.
	 */
	a = 0; /* Starting element and its offset */
	a_offset = heap->reqs[a].offset;
	for (;;) {
		b = a + a + 1; /* First child */
		if (b >= heap->cnt)
			return;
		b_offset = heap->reqs[b].offset;
		c = b + 1; /* Use second child if it is smaller. */
		if (c < heap->cnt) {
			c_offset = heap->reqs[c].offset;
			if (c_offset < b_offset) {
				b = c;
				b_offset = c_offset;
			}
		}
		if (a_offset <= b_offset)
			return;
		tmp = heap->reqs[a];
		heap->reqs[a] = heap->reqs[b];
		heap->reqs[b] = tmp;
		a = b;
	}
}


static int
read_CE(struct archive_read *a, struct iso9660 *iso9660)
{
	struct read_ce_queue *heap;
	const unsigned char *b, *p, *end;
	struct file_info *file;
	size_t step;
	int r;

	/* Read data which RRIP "CE" extension points. */
	heap = &(iso9660->read_ce_req);
	step = iso9660->logical_block_size;
	while (heap->cnt &&
	    heap->reqs[0].offset == iso9660->current_position) {
		b = __archive_read_ahead(a, step, NULL);
		if (b == NULL) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_MISC,
			    "Failed to read full block when scanning "
			    "ISO9660 directory list");
			return (ARCHIVE_FATAL);
		}
		do {
			file = heap->reqs[0].file;
			if (file->ce_offset + file->ce_size > step) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Malformed CE information");
				return (ARCHIVE_FATAL);
			}
			p = b + file->ce_offset;
			end = p + file->ce_size;
			next_CE(heap);
			r = parse_rockridge(a, file, p, end);
			if (r != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
		} while (heap->cnt &&
		    heap->reqs[0].offset == iso9660->current_position);
		/* NOTE: Do not move this consume's code to front of
		 * do-while loop. Registration of nested CE extension
		 * might cause error because of current position. */
		__archive_read_consume(a, step);
		iso9660->current_position += step;
	}
	return (ARCHIVE_OK);
}

static void
parse_rockridge_NM1(struct file_info *file,
		    const unsigned char *data, int data_length)
{
	if (!file->name_continues)
		archive_string_empty(&file->name);
	file->name_continues = 0;
	if (data_length < 1)
		return;
	/*
	 * NM version 1 extension comprises:
	 *   1 byte flag, value is one of:
	 *     = 0: remainder is name
	 *     = 1: remainder is name, next NM entry continues name
	 *     = 2: "."
	 *     = 4: ".."
	 *     = 32: Implementation specific
	 *     All other values are reserved.
	 */
	switch(data[0]) {
	case 0:
		if (data_length < 2)
			return;
		archive_strncat(&file->name,
		    (const char *)data + 1, data_length - 1);
		break;
	case 1:
		if (data_length < 2)
			return;
		archive_strncat(&file->name,
		    (const char *)data + 1, data_length - 1);
		file->name_continues = 1;
		break;
	case 2:
		archive_strcat(&file->name, ".");
		break;
	case 4:
		archive_strcat(&file->name, "..");
		break;
	default:
		return;
	}

}

static void
parse_rockridge_TF1(struct file_info *file, const unsigned char *data,
    int data_length)
{
	char flag;
	/*
	 * TF extension comprises:
	 *   one byte flag
	 *   create time (optional)
	 *   modify time (optional)
	 *   access time (optional)
	 *   attribute time (optional)
	 *  Time format and presence of fields
	 *  is controlled by flag bits.
	 */
	if (data_length < 1)
		return;
	flag = data[0];
	++data;
	--data_length;
	if (flag & 0x80) {
		/* Use 17-byte time format. */
		if ((flag & 1) && data_length >= 17) {
			/* Create time. */
			file->birthtime_is_set = 1;
			file->birthtime = isodate17(data);
			data += 17;
			data_length -= 17;
		}
		if ((flag & 2) && data_length >= 17) {
			/* Modify time. */
			file->mtime = isodate17(data);
			data += 17;
			data_length -= 17;
		}
		if ((flag & 4) && data_length >= 17) {
			/* Access time. */
			file->atime = isodate17(data);
			data += 17;
			data_length -= 17;
		}
		if ((flag & 8) && data_length >= 17) {
			/* Attribute change time. */
			file->ctime = isodate17(data);
		}
	} else {
		/* Use 7-byte time format. */
		if ((flag & 1) && data_length >= 7) {
			/* Create time. */
			file->birthtime_is_set = 1;
			file->birthtime = isodate7(data);
			data += 7;
			data_length -= 7;
		}
		if ((flag & 2) && data_length >= 7) {
			/* Modify time. */
			file->mtime = isodate7(data);
			data += 7;
			data_length -= 7;
		}
		if ((flag & 4) && data_length >= 7) {
			/* Access time. */
			file->atime = isodate7(data);
			data += 7;
			data_length -= 7;
		}
		if ((flag & 8) && data_length >= 7) {
			/* Attribute change time. */
			file->ctime = isodate7(data);
		}
	}
}

static void
parse_rockridge_SL1(struct file_info *file, const unsigned char *data,
    int data_length)
{
	const char *separator = "";

	if (!file->symlink_continues || file->symlink.length < 1)
		archive_string_empty(&file->symlink);
	file->symlink_continues = 0;

	/*
	 * Defined flag values:
	 *  0: This is the last SL record for this symbolic link
	 *  1: this symbolic link field continues in next SL entry
	 *  All other values are reserved.
	 */
	if (data_length < 1)
		return;
	switch(*data) {
	case 0:
		break;
	case 1:
		file->symlink_continues = 1;
		break;
	default:
		return;
	}
	++data;  /* Skip flag byte. */
	--data_length;

	/*
	 * SL extension body stores "components".
	 * Basically, this is a complicated way of storing
	 * a POSIX path.  It also interferes with using
	 * symlinks for storing non-path data. <sigh>
	 *
	 * Each component is 2 bytes (flag and length)
	 * possibly followed by name data.
	 */
	while (data_length >= 2) {
		unsigned char flag = *data++;
		unsigned char nlen = *data++;
		data_length -= 2;

		archive_strcat(&file->symlink, separator);
		separator = "/";

		switch(flag) {
		case 0: /* Usual case, this is text. */
			if (data_length < nlen)
				return;
			archive_strncat(&file->symlink,
			    (const char *)data, nlen);
			break;
		case 0x01: /* Text continues in next component. */
			if (data_length < nlen)
				return;
			archive_strncat(&file->symlink,
			    (const char *)data, nlen);
			separator = "";
			break;
		case 0x02: /* Current dir. */
			archive_strcat(&file->symlink, ".");
			break;
		case 0x04: /* Parent dir. */
			archive_strcat(&file->symlink, "..");
			break;
		case 0x08: /* Root of filesystem. */
			archive_strcat(&file->symlink, "/");
			separator = "";
			break;
		case 0x10: /* Undefined (historically "volume root" */
			archive_string_empty(&file->symlink);
			archive_strcat(&file->symlink, "ROOT");
			break;
		case 0x20: /* Undefined (historically "hostname") */
			archive_strcat(&file->symlink, "hostname");
			break;
		default:
			/* TODO: issue a warning ? */
			return;
		}
		data += nlen;
		data_length -= nlen;
	}
}

static void
parse_rockridge_ZF1(struct file_info *file, const unsigned char *data,
    int data_length)
{

	if (data[0] == 0x70 && data[1] == 0x7a && data_length == 12) {
		/* paged zlib */
		file->pz = 1;
		file->pz_log2_bs = data[3];
		file->pz_uncompressed_size = archive_le32dec(&data[4]);
	}
}

static void
register_file(struct iso9660 *iso9660, struct file_info *file)
{

	file->use_next = iso9660->use_files;
	iso9660->use_files = file;
}

static void
release_files(struct iso9660 *iso9660)
{
	struct content *con, *connext;
	struct file_info *file;

	file = iso9660->use_files;
	while (file != NULL) {
		struct file_info *next = file->use_next;

		archive_string_free(&file->name);
		archive_string_free(&file->symlink);
		free(file->utf16be_name);
		con = file->contents.first;
		while (con != NULL) {
			connext = con->next;
			free(con);
			con = connext;
		}
		free(file);
		file = next;
	}
}

static int
next_entry_seek(struct archive_read *a, struct iso9660 *iso9660,
    struct file_info **pfile)
{
	struct file_info *file;
	int r;

	r = next_cache_entry(a, iso9660, pfile);
	if (r != ARCHIVE_OK)
		return (r);
	file = *pfile;

	/* Don't waste time seeking for zero-length bodies. */
	if (file->size == 0)
		file->offset = iso9660->current_position;

	/* flush any remaining bytes from the last round to ensure
	 * we're positioned */
	if (iso9660->entry_bytes_unconsumed) {
		__archive_read_consume(a, iso9660->entry_bytes_unconsumed);
		iso9660->entry_bytes_unconsumed = 0;
	}

	/* Seek forward to the start of the entry. */
	if (iso9660->current_position < file->offset) {
		int64_t step;

		step = file->offset - iso9660->current_position;
		step = __archive_read_consume(a, step);
		if (step < 0)
			return ((int)step);
		iso9660->current_position = file->offset;
	}

	/* We found body of file; handle it now. */
	return (ARCHIVE_OK);
}

static int
next_cache_entry(struct archive_read *a, struct iso9660 *iso9660,
    struct file_info **pfile)
{
	struct file_info *file;
	struct {
		struct file_info	*first;
		struct file_info	**last;
	}	empty_files;
	int64_t number;
	int count;

	file = cache_get_entry(iso9660);
	if (file != NULL) {
		*pfile = file;
		return (ARCHIVE_OK);
	}

	for (;;) {
		struct file_info *re, *d;

		*pfile = file = next_entry(iso9660);
		if (file == NULL) {
			/*
			 * If directory entries all which are descendant of
			 * rr_moved are still remaining, expose their.
			 */
			if (iso9660->re_files.first != NULL && 
			    iso9660->rr_moved != NULL &&
			    iso9660->rr_moved->rr_moved_has_re_only)
				/* Expose "rr_moved" entry. */
				cache_add_entry(iso9660, iso9660->rr_moved);
			while ((re = re_get_entry(iso9660)) != NULL) {
				/* Expose its descendant dirs. */
				while ((d = rede_get_entry(re)) != NULL)
					cache_add_entry(iso9660, d);
			}
			if (iso9660->cache_files.first != NULL)
				return (next_cache_entry(a, iso9660, pfile));
			return (ARCHIVE_EOF);
		}

		if (file->cl_offset) {
			struct file_info *first_re = NULL;
			int nexted_re = 0;

			/*
			 * Find "RE" dir for the current file, which
			 * has "CL" flag.
			 */
			while ((re = re_get_entry(iso9660))
			    != first_re) {
				if (first_re == NULL)
					first_re = re;
				if (re->offset == file->cl_offset) {
					re->parent->subdirs--;
					re->parent = file->parent;
					re->re = 0;
					if (re->parent->re_descendant) {
						nexted_re = 1;
						re->re_descendant = 1;
						if (rede_add_entry(re) < 0)
							goto fatal_rr;
						/* Move a list of descendants
						 * to a new ancestor. */
						while ((d = rede_get_entry(
						    re)) != NULL)
							if (rede_add_entry(d)
							    < 0)
								goto fatal_rr;
						break;
					}
					/* Replace the current file
					 * with "RE" dir */
					*pfile = file = re;
					/* Expose its descendant */
					while ((d = rede_get_entry(
					    file)) != NULL)
						cache_add_entry(
						    iso9660, d);
					break;
				} else
					re_add_entry(iso9660, re);
			}
			if (nexted_re) {
				/*
				 * Do not expose this at this time
				 * because we have not gotten its full-path
				 * name yet.
				 */
				continue;
			}
		} else if ((file->mode & AE_IFMT) == AE_IFDIR) {
			int r;

			/* Read file entries in this dir. */
			r = read_children(a, file);
			if (r != ARCHIVE_OK)
				return (r);

			/*
			 * Handle a special dir of Rockridge extensions,
			 * "rr_moved".
			 */
			if (file->rr_moved) {
				/*
				 * If this has only the subdirectories which
				 * have "RE" flags, do not expose at this time.
				 */
				if (file->rr_moved_has_re_only)
					continue;
				/* Otherwise expose "rr_moved" entry. */
			} else if (file->re) {
				/*
				 * Do not expose this at this time
				 * because we have not gotten its full-path
				 * name yet.
				 */
				re_add_entry(iso9660, file);
				continue;
			} else if (file->re_descendant) {
				/*
				 * If the top level "RE" entry of this entry
				 * is not exposed, we, accordingly, should not
				 * expose this entry at this time because
				 * we cannot make its proper full-path name.
				 */
				if (rede_add_entry(file) == 0)
					continue;
				/* Otherwise we can expose this entry because
				 * it seems its top level "RE" has already been
				 * exposed. */
			}
		}
		break;
	}

	if ((file->mode & AE_IFMT) != AE_IFREG || file->number == -1)
		return (ARCHIVE_OK);

	count = 0;
	number = file->number;
	iso9660->cache_files.first = NULL;
	iso9660->cache_files.last = &(iso9660->cache_files.first);
	empty_files.first = NULL;
	empty_files.last = &empty_files.first;
	/* Collect files which has the same file serial number.
	 * Peek pending_files so that file which number is different
	 * is not put back. */
	while (iso9660->pending_files.used > 0 &&
	    (iso9660->pending_files.files[0]->number == -1 ||
	     iso9660->pending_files.files[0]->number == number)) {
		if (file->number == -1) {
			/* This file has the same offset
			 * but it's wrong offset which empty files
			 * and symlink files have.
			 * NOTE: This wrong offset was recorded by
			 * old mkisofs utility. If ISO images is
			 * created by latest mkisofs, this does not
			 * happen.
			 */
			file->next = NULL;
			*empty_files.last = file;
			empty_files.last = &(file->next);
		} else {
			count++;
			cache_add_entry(iso9660, file);
		}
		file = next_entry(iso9660);
	}

	if (count == 0) {
		*pfile = file;
		return ((file == NULL)?ARCHIVE_EOF:ARCHIVE_OK);
	}
	if (file->number == -1) {
		file->next = NULL;
		*empty_files.last = file;
		empty_files.last = &(file->next);
	} else {
		count++;
		cache_add_entry(iso9660, file);
	}

	if (count > 1) {
		/* The count is the same as number of hardlink,
		 * so much so that each nlinks of files in cache_file
		 * is overwritten by value of the count.
		 */
		for (file = iso9660->cache_files.first;
		    file != NULL; file = file->next)
			file->nlinks = count;
	}
	/* If there are empty files, that files are added
	 * to the tail of the cache_files. */
	if (empty_files.first != NULL) {
		*iso9660->cache_files.last = empty_files.first;
		iso9660->cache_files.last = empty_files.last;
	}
	*pfile = cache_get_entry(iso9660);
	return ((*pfile == NULL)?ARCHIVE_EOF:ARCHIVE_OK);

fatal_rr:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "Failed to connect 'CL' pointer to 'RE' rr_moved pointer of "
	    "Rockridge extensions: current position = %jd, CL offset = %jd",
	    (intmax_t)iso9660->current_position, (intmax_t)file->cl_offset);
	return (ARCHIVE_FATAL);
}

static inline void
re_add_entry(struct iso9660 *iso9660, struct file_info *file)
{
	file->re_next = NULL;
	*iso9660->re_files.last = file;
	iso9660->re_files.last = &(file->re_next);
}

static inline struct file_info *
re_get_entry(struct iso9660 *iso9660)
{
	struct file_info *file;

	if ((file = iso9660->re_files.first) != NULL) {
		iso9660->re_files.first = file->re_next;
		if (iso9660->re_files.first == NULL)
			iso9660->re_files.last =
			    &(iso9660->re_files.first);
	}
	return (file);
}

static inline int
rede_add_entry(struct file_info *file)
{
	struct file_info *re;

	/*
	 * Find "RE" entry.
	 */
	re = file->parent;
	while (re != NULL && !re->re)
		re = re->parent;
	if (re == NULL)
		return (-1);

	file->re_next = NULL;
	*re->rede_files.last = file;
	re->rede_files.last = &(file->re_next);
	return (0);
}

static inline struct file_info *
rede_get_entry(struct file_info *re)
{
	struct file_info *file;

	if ((file = re->rede_files.first) != NULL) {
		re->rede_files.first = file->re_next;
		if (re->rede_files.first == NULL)
			re->rede_files.last =
			    &(re->rede_files.first);
	}
	return (file);
}

static inline void
cache_add_entry(struct iso9660 *iso9660, struct file_info *file)
{
	file->next = NULL;
	*iso9660->cache_files.last = file;
	iso9660->cache_files.last = &(file->next);
}

static inline struct file_info *
cache_get_entry(struct iso9660 *iso9660)
{
	struct file_info *file;

	if ((file = iso9660->cache_files.first) != NULL) {
		iso9660->cache_files.first = file->next;
		if (iso9660->cache_files.first == NULL)
			iso9660->cache_files.last =
			    &(iso9660->cache_files.first);
	}
	return (file);
}

static int
heap_add_entry(struct archive_read *a, struct heap_queue *heap,
    struct file_info *file, uint64_t key)
{
	uint64_t file_key, parent_key;
	int hole, parent;

	/* Expand our pending files list as necessary. */
	if (heap->used >= heap->allocated) {
		struct file_info **new_pending_files;
		int new_size = heap->allocated * 2;

		if (heap->allocated < 1024)
			new_size = 1024;
		/* Overflow might keep us from growing the list. */
		if (new_size <= heap->allocated) {
			archive_set_error(&a->archive,
			    ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		new_pending_files = (struct file_info **)
		    malloc(new_size * sizeof(new_pending_files[0]));
		if (new_pending_files == NULL) {
			archive_set_error(&a->archive,
			    ENOMEM, "Out of memory");
			return (ARCHIVE_FATAL);
		}
		if (heap->allocated)
			memcpy(new_pending_files, heap->files,
			    heap->allocated * sizeof(new_pending_files[0]));
		free(heap->files);
		heap->files = new_pending_files;
		heap->allocated = new_size;
	}

	file_key = file->key = key;

	/*
	 * Start with hole at end, walk it up tree to find insertion point.
	 */
	hole = heap->used++;
	while (hole > 0) {
		parent = (hole - 1)/2;
		parent_key = heap->files[parent]->key;
		if (file_key >= parent_key) {
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

static struct file_info *
heap_get_entry(struct heap_queue *heap)
{
	uint64_t a_key, b_key, c_key;
	int a, b, c;
	struct file_info *r, *tmp;

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
	a_key = heap->files[a]->key;
	for (;;) {
		b = a + a + 1; /* First child */
		if (b >= heap->used)
			return (r);
		b_key = heap->files[b]->key;
		c = b + 1; /* Use second child if it is smaller. */
		if (c < heap->used) {
			c_key = heap->files[c]->key;
			if (c_key < b_key) {
				b = c;
				b_key = c_key;
			}
		}
		if (a_key <= b_key)
			return (r);
		tmp = heap->files[a];
		heap->files[a] = heap->files[b];
		heap->files[b] = tmp;
		a = b;
	}
}

static unsigned int
toi(const void *p, int n)
{
	const unsigned char *v = (const unsigned char *)p;
	if (n > 1)
		return v[0] + 256 * toi(v + 1, n - 1);
	if (n == 1)
		return v[0];
	return (0);
}

static time_t
isodate7(const unsigned char *v)
{
	struct tm tm;
	int offset;
	time_t t;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = v[0];
	tm.tm_mon = v[1] - 1;
	tm.tm_mday = v[2];
	tm.tm_hour = v[3];
	tm.tm_min = v[4];
	tm.tm_sec = v[5];
	/* v[6] is the signed timezone offset, in 1/4-hour increments. */
	offset = ((const signed char *)v)[6];
	if (offset > -48 && offset < 52) {
		tm.tm_hour -= offset / 4;
		tm.tm_min -= (offset % 4) * 15;
	}
	t = time_from_tm(&tm);
	if (t == (time_t)-1)
		return ((time_t)0);
	return (t);
}

static time_t
isodate17(const unsigned char *v)
{
	struct tm tm;
	int offset;
	time_t t;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = (v[0] - '0') * 1000 + (v[1] - '0') * 100
	    + (v[2] - '0') * 10 + (v[3] - '0')
	    - 1900;
	tm.tm_mon = (v[4] - '0') * 10 + (v[5] - '0');
	tm.tm_mday = (v[6] - '0') * 10 + (v[7] - '0');
	tm.tm_hour = (v[8] - '0') * 10 + (v[9] - '0');
	tm.tm_min = (v[10] - '0') * 10 + (v[11] - '0');
	tm.tm_sec = (v[12] - '0') * 10 + (v[13] - '0');
	/* v[16] is the signed timezone offset, in 1/4-hour increments. */
	offset = ((const signed char *)v)[16];
	if (offset > -48 && offset < 52) {
		tm.tm_hour -= offset / 4;
		tm.tm_min -= (offset % 4) * 15;
	}
	t = time_from_tm(&tm);
	if (t == (time_t)-1)
		return ((time_t)0);
	return (t);
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
        if (mktime(t) == (time_t)-1)
                return ((time_t)-1);
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

static const char *
build_pathname(struct archive_string *as, struct file_info *file, int depth)
{
	// Plain ISO9660 only allows 8 dir levels; if we get
	// to 1000, then something is very, very wrong.
	if (depth > 1000) {
		return NULL;
	}
	if (file->parent != NULL && archive_strlen(&file->parent->name) > 0) {
		if (build_pathname(as, file->parent, depth + 1) == NULL) {
			return NULL;
		}
		archive_strcat(as, "/");
	}
	if (archive_strlen(&file->name) == 0)
		archive_strcat(as, ".");
	else
		archive_string_concat(as, &file->name);
	return (as->s);
}

static int
build_pathname_utf16be(unsigned char *p, size_t max, size_t *len,
    struct file_info *file)
{
	if (file->parent != NULL && file->parent->utf16be_bytes > 0) {
		if (build_pathname_utf16be(p, max, len, file->parent) != 0)
			return (-1);
		p[*len] = 0;
		p[*len + 1] = '/';
		*len += 2;
	}
	if (file->utf16be_bytes == 0) {
		if (*len + 2 > max)
			return (-1);/* Path is too long! */
		p[*len] = 0;
		p[*len + 1] = '.';
		*len += 2;
	} else {
		if (*len + file->utf16be_bytes > max)
			return (-1);/* Path is too long! */
		memcpy(p + *len, file->utf16be_name, file->utf16be_bytes);
		*len += file->utf16be_bytes;
	}
	return (0);
}

#if DEBUG
static void
dump_isodirrec(FILE *out, const unsigned char *isodirrec)
{
	fprintf(out, " l %d,",
	    toi(isodirrec + DR_length_offset, DR_length_size));
	fprintf(out, " a %d,",
	    toi(isodirrec + DR_ext_attr_length_offset, DR_ext_attr_length_size));
	fprintf(out, " ext 0x%x,",
	    toi(isodirrec + DR_extent_offset, DR_extent_size));
	fprintf(out, " s %d,",
	    toi(isodirrec + DR_size_offset, DR_extent_size));
	fprintf(out, " f 0x%x,",
	    toi(isodirrec + DR_flags_offset, DR_flags_size));
	fprintf(out, " u %d,",
	    toi(isodirrec + DR_file_unit_size_offset, DR_file_unit_size_size));
	fprintf(out, " ilv %d,",
	    toi(isodirrec + DR_interleave_offset, DR_interleave_size));
	fprintf(out, " seq %d,",
	    toi(isodirrec + DR_volume_sequence_number_offset,
		DR_volume_sequence_number_size));
	fprintf(out, " nl %d:",
	    toi(isodirrec + DR_name_len_offset, DR_name_len_size));
	fprintf(out, " `%.*s'",
	    toi(isodirrec + DR_name_len_offset, DR_name_len_size),
		isodirrec + DR_name_offset);
}
#endif
