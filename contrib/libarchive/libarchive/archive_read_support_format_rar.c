/*-
* Copyright (c) 2003-2007 Tim Kientzle
* Copyright (c) 2011 Andres Mejia
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
#include <time.h>
#include <limits.h>
#ifdef HAVE_ZLIB_H
#include <zlib.h> /* crc32 */
#endif

#include "archive.h"
#ifndef HAVE_ZLIB_H
#include "archive_crc32.h"
#endif
#include "archive_endian.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_ppmd7_private.h"
#include "archive_private.h"
#include "archive_read_private.h"

/* RAR signature, also known as the mark header */
#define RAR_SIGNATURE "\x52\x61\x72\x21\x1A\x07\x00"

/* Header types */
#define MARK_HEAD    0x72
#define MAIN_HEAD    0x73
#define FILE_HEAD    0x74
#define COMM_HEAD    0x75
#define AV_HEAD      0x76
#define SUB_HEAD     0x77
#define PROTECT_HEAD 0x78
#define SIGN_HEAD    0x79
#define NEWSUB_HEAD  0x7a
#define ENDARC_HEAD  0x7b

/* Main Header Flags */
#define MHD_VOLUME       0x0001
#define MHD_COMMENT      0x0002
#define MHD_LOCK         0x0004
#define MHD_SOLID        0x0008
#define MHD_NEWNUMBERING 0x0010
#define MHD_AV           0x0020
#define MHD_PROTECT      0x0040
#define MHD_PASSWORD     0x0080
#define MHD_FIRSTVOLUME  0x0100
#define MHD_ENCRYPTVER   0x0200

/* Flags common to all headers */
#define HD_MARKDELETION     0x4000
#define HD_ADD_SIZE_PRESENT 0x8000

/* File Header Flags */
#define FHD_SPLIT_BEFORE 0x0001
#define FHD_SPLIT_AFTER  0x0002
#define FHD_PASSWORD     0x0004
#define FHD_COMMENT      0x0008
#define FHD_SOLID        0x0010
#define FHD_LARGE        0x0100
#define FHD_UNICODE      0x0200
#define FHD_SALT         0x0400
#define FHD_VERSION      0x0800
#define FHD_EXTTIME      0x1000
#define FHD_EXTFLAGS     0x2000

/* File dictionary sizes */
#define DICTIONARY_SIZE_64   0x00
#define DICTIONARY_SIZE_128  0x20
#define DICTIONARY_SIZE_256  0x40
#define DICTIONARY_SIZE_512  0x60
#define DICTIONARY_SIZE_1024 0x80
#define DICTIONARY_SIZE_2048 0xA0
#define DICTIONARY_SIZE_4096 0xC0
#define FILE_IS_DIRECTORY    0xE0
#define DICTIONARY_MASK      FILE_IS_DIRECTORY

/* OS Flags */
#define OS_MSDOS  0
#define OS_OS2    1
#define OS_WIN32  2
#define OS_UNIX   3
#define OS_MAC_OS 4
#define OS_BEOS   5

/* Compression Methods */
#define COMPRESS_METHOD_STORE   0x30
/* LZSS */
#define COMPRESS_METHOD_FASTEST 0x31
#define COMPRESS_METHOD_FAST    0x32
#define COMPRESS_METHOD_NORMAL  0x33
/* PPMd Variant H */
#define COMPRESS_METHOD_GOOD    0x34
#define COMPRESS_METHOD_BEST    0x35

#define CRC_POLYNOMIAL 0xEDB88320

#define NS_UNIT 10000000

#define DICTIONARY_MAX_SIZE 0x400000

#define MAINCODE_SIZE      299
#define OFFSETCODE_SIZE    60
#define LOWOFFSETCODE_SIZE 17
#define LENGTHCODE_SIZE    28
#define HUFFMAN_TABLE_SIZE \
  MAINCODE_SIZE + OFFSETCODE_SIZE + LOWOFFSETCODE_SIZE + LENGTHCODE_SIZE

#define MAX_SYMBOL_LENGTH 0xF
#define MAX_SYMBOLS       20

/*
 * Considering L1,L2 cache miss and a calling of write system-call,
 * the best size of the output buffer(uncompressed buffer) is 128K.
 * If the structure of extracting process is changed, this value
 * might be researched again.
 */
#define UNP_BUFFER_SIZE   (128 * 1024)

/* Define this here for non-Windows platforms */
#if !((defined(__WIN32__) || defined(_WIN32) || defined(__WIN32)) && !defined(__CYGWIN__))
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#endif

/* Fields common to all headers */
struct rar_header
{
  char crc[2];
  char type;
  char flags[2];
  char size[2];
};

/* Fields common to all file headers */
struct rar_file_header
{
  char pack_size[4];
  char unp_size[4];
  char host_os;
  char file_crc[4];
  char file_time[4];
  char unp_ver;
  char method;
  char name_size[2];
  char file_attr[4];
};

struct huffman_tree_node
{
  int branches[2];
};

struct huffman_table_entry
{
  unsigned int length;
  int value;
};

struct huffman_code
{
  struct huffman_tree_node *tree;
  int numentries;
  int numallocatedentries;
  int minlength;
  int maxlength;
  int tablesize;
  struct huffman_table_entry *table;
};

struct lzss
{
  unsigned char *window;
  int mask;
  int64_t position;
};

struct data_block_offsets
{
  int64_t header_size;
  int64_t start_offset;
  int64_t end_offset;
};

struct rar
{
  /* Entries from main RAR header */
  unsigned main_flags;
  unsigned long file_crc;
  char reserved1[2];
  char reserved2[4];
  char encryptver;

  /* File header entries */
  char compression_method;
  unsigned file_flags;
  int64_t packed_size;
  int64_t unp_size;
  time_t mtime;
  long mnsec;
  mode_t mode;
  char *filename;
  char *filename_save;
  size_t filename_save_size;
  size_t filename_allocated;

  /* File header optional entries */
  char salt[8];
  time_t atime;
  long ansec;
  time_t ctime;
  long cnsec;
  time_t arctime;
  long arcnsec;

  /* Fields to help with tracking decompression of files. */
  int64_t bytes_unconsumed;
  int64_t bytes_remaining;
  int64_t bytes_uncopied;
  int64_t offset;
  int64_t offset_outgoing;
  int64_t offset_seek;
  char valid;
  unsigned int unp_offset;
  unsigned int unp_buffer_size;
  unsigned char *unp_buffer;
  unsigned int dictionary_size;
  char start_new_block;
  char entry_eof;
  unsigned long crc_calculated;
  int found_first_header;
  char has_endarc_header;
  struct data_block_offsets *dbo;
  unsigned int cursor;
  unsigned int nodes;
  char filename_must_match;

  /* LZSS members */
  struct huffman_code maincode;
  struct huffman_code offsetcode;
  struct huffman_code lowoffsetcode;
  struct huffman_code lengthcode;
  unsigned char lengthtable[HUFFMAN_TABLE_SIZE];
  struct lzss lzss;
  char output_last_match;
  unsigned int lastlength;
  unsigned int lastoffset;
  unsigned int oldoffset[4];
  unsigned int lastlowoffset;
  unsigned int numlowoffsetrepeats;
  int64_t filterstart;
  char start_new_table;

  /* PPMd Variant H members */
  char ppmd_valid;
  char ppmd_eod;
  char is_ppmd_block;
  int ppmd_escape;
  CPpmd7 ppmd7_context;
  CPpmd7z_RangeDec range_dec;
  IByteIn bytein;

  /*
   * String conversion object.
   */
  int init_default_conversion;
  struct archive_string_conv *sconv_default;
  struct archive_string_conv *opt_sconv;
  struct archive_string_conv *sconv_utf8;
  struct archive_string_conv *sconv_utf16be;

  /*
   * Bit stream reader.
   */
  struct rar_br {
#define CACHE_TYPE	uint64_t
#define CACHE_BITS	(8 * sizeof(CACHE_TYPE))
    /* Cache buffer. */
    CACHE_TYPE		 cache_buffer;
    /* Indicates how many bits avail in cache_buffer. */
    int			 cache_avail;
    ssize_t		 avail_in;
    const unsigned char *next_in;
  } br;

  /*
   * Custom field to denote that this archive contains encrypted entries
   */
  int has_encrypted_entries;
};

static int archive_read_support_format_rar_capabilities(struct archive_read *);
static int archive_read_format_rar_has_encrypted_entries(struct archive_read *);
static int archive_read_format_rar_bid(struct archive_read *, int);
static int archive_read_format_rar_options(struct archive_read *,
    const char *, const char *);
static int archive_read_format_rar_read_header(struct archive_read *,
    struct archive_entry *);
static int archive_read_format_rar_read_data(struct archive_read *,
    const void **, size_t *, int64_t *);
static int archive_read_format_rar_read_data_skip(struct archive_read *a);
static int64_t archive_read_format_rar_seek_data(struct archive_read *, int64_t,
    int);
static int archive_read_format_rar_cleanup(struct archive_read *);

/* Support functions */
static int read_header(struct archive_read *, struct archive_entry *, char);
static time_t get_time(int);
static int read_exttime(const char *, struct rar *, const char *);
static int read_symlink_stored(struct archive_read *, struct archive_entry *,
                               struct archive_string_conv *);
static int read_data_stored(struct archive_read *, const void **, size_t *,
                            int64_t *);
static int read_data_compressed(struct archive_read *, const void **, size_t *,
                          int64_t *);
static int rar_br_preparation(struct archive_read *, struct rar_br *);
static int parse_codes(struct archive_read *);
static void free_codes(struct archive_read *);
static int read_next_symbol(struct archive_read *, struct huffman_code *);
static int create_code(struct archive_read *, struct huffman_code *,
                        unsigned char *, int, char);
static int add_value(struct archive_read *, struct huffman_code *, int, int,
                     int);
static int new_node(struct huffman_code *);
static int make_table(struct archive_read *, struct huffman_code *);
static int make_table_recurse(struct archive_read *, struct huffman_code *, int,
                              struct huffman_table_entry *, int, int);
static int64_t expand(struct archive_read *, int64_t);
static int copy_from_lzss_window(struct archive_read *, const void **,
                                   int64_t, int);
static const void *rar_read_ahead(struct archive_read *, size_t, ssize_t *);

/*
 * Bit stream reader.
 */
/* Check that the cache buffer has enough bits. */
#define rar_br_has(br, n) ((br)->cache_avail >= n)
/* Get compressed data by bit. */
#define rar_br_bits(br, n)        \
  (((uint32_t)((br)->cache_buffer >>    \
    ((br)->cache_avail - (n)))) & cache_masks[n])
#define rar_br_bits_forced(br, n)     \
  (((uint32_t)((br)->cache_buffer <<    \
    ((n) - (br)->cache_avail))) & cache_masks[n])
/* Read ahead to make sure the cache buffer has enough compressed data we
 * will use.
 *  True  : completed, there is enough data in the cache buffer.
 *  False : there is no data in the stream. */
#define rar_br_read_ahead(a, br, n) \
  ((rar_br_has(br, (n)) || rar_br_fillup(a, br)) || rar_br_has(br, (n)))
/* Notify how many bits we consumed. */
#define rar_br_consume(br, n) ((br)->cache_avail -= (n))
#define rar_br_consume_unalined_bits(br) ((br)->cache_avail &= ~7)

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
rar_br_fillup(struct archive_read *a, struct rar_br *br)
{
  struct rar *rar = (struct rar *)(a->format->data);
  int n = CACHE_BITS - br->cache_avail;

  for (;;) {
    switch (n >> 3) {
    case 8:
      if (br->avail_in >= 8) {
        br->cache_buffer =
            ((uint64_t)br->next_in[0]) << 56 |
            ((uint64_t)br->next_in[1]) << 48 |
            ((uint64_t)br->next_in[2]) << 40 |
            ((uint64_t)br->next_in[3]) << 32 |
            ((uint32_t)br->next_in[4]) << 24 |
            ((uint32_t)br->next_in[5]) << 16 |
            ((uint32_t)br->next_in[6]) << 8 |
             (uint32_t)br->next_in[7];
        br->next_in += 8;
        br->avail_in -= 8;
        br->cache_avail += 8 * 8;
        rar->bytes_unconsumed += 8;
        rar->bytes_remaining -= 8;
        return (1);
      }
      break;
    case 7:
      if (br->avail_in >= 7) {
        br->cache_buffer =
           (br->cache_buffer << 56) |
            ((uint64_t)br->next_in[0]) << 48 |
            ((uint64_t)br->next_in[1]) << 40 |
            ((uint64_t)br->next_in[2]) << 32 |
            ((uint32_t)br->next_in[3]) << 24 |
            ((uint32_t)br->next_in[4]) << 16 |
            ((uint32_t)br->next_in[5]) << 8 |
             (uint32_t)br->next_in[6];
        br->next_in += 7;
        br->avail_in -= 7;
        br->cache_avail += 7 * 8;
        rar->bytes_unconsumed += 7;
        rar->bytes_remaining -= 7;
        return (1);
      }
      break;
    case 6:
      if (br->avail_in >= 6) {
        br->cache_buffer =
           (br->cache_buffer << 48) |
            ((uint64_t)br->next_in[0]) << 40 |
            ((uint64_t)br->next_in[1]) << 32 |
            ((uint32_t)br->next_in[2]) << 24 |
            ((uint32_t)br->next_in[3]) << 16 |
            ((uint32_t)br->next_in[4]) << 8 |
             (uint32_t)br->next_in[5];
        br->next_in += 6;
        br->avail_in -= 6;
        br->cache_avail += 6 * 8;
        rar->bytes_unconsumed += 6;
        rar->bytes_remaining -= 6;
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
    if (br->avail_in <= 0) {

      if (rar->bytes_unconsumed > 0) {
        /* Consume as much as the decompressor
         * actually used. */
        __archive_read_consume(a, rar->bytes_unconsumed);
        rar->bytes_unconsumed = 0;
      }
      br->next_in = rar_read_ahead(a, 1, &(br->avail_in));
      if (br->next_in == NULL)
        return (0);
      if (br->avail_in == 0)
        return (0);
    }
    br->cache_buffer =
       (br->cache_buffer << 8) | *br->next_in++;
    br->avail_in--;
    br->cache_avail += 8;
    n -= 8;
    rar->bytes_unconsumed++;
    rar->bytes_remaining--;
  }
}

static int
rar_br_preparation(struct archive_read *a, struct rar_br *br)
{
  struct rar *rar = (struct rar *)(a->format->data);

  if (rar->bytes_remaining > 0) {
    br->next_in = rar_read_ahead(a, 1, &(br->avail_in));
    if (br->next_in == NULL) {
      archive_set_error(&a->archive,
          ARCHIVE_ERRNO_FILE_FORMAT,
          "Truncated RAR file data");
      return (ARCHIVE_FATAL);
    }
    if (br->cache_avail == 0)
      (void)rar_br_fillup(a, br);
  }
  return (ARCHIVE_OK);
}

/* Find last bit set */
static inline int
rar_fls(unsigned int word)
{
  word |= (word >>  1);
  word |= (word >>  2);
  word |= (word >>  4);
  word |= (word >>  8);
  word |= (word >> 16);
  return word - (word >> 1);
}

/* LZSS functions */
static inline int64_t
lzss_position(struct lzss *lzss)
{
  return lzss->position;
}

static inline int
lzss_mask(struct lzss *lzss)
{
  return lzss->mask;
}

static inline int
lzss_size(struct lzss *lzss)
{
  return lzss->mask + 1;
}

static inline int
lzss_offset_for_position(struct lzss *lzss, int64_t pos)
{
  return (int)(pos & lzss->mask);
}

static inline unsigned char *
lzss_pointer_for_position(struct lzss *lzss, int64_t pos)
{
  return &lzss->window[lzss_offset_for_position(lzss, pos)];
}

static inline int
lzss_current_offset(struct lzss *lzss)
{
  return lzss_offset_for_position(lzss, lzss->position);
}

static inline uint8_t *
lzss_current_pointer(struct lzss *lzss)
{
  return lzss_pointer_for_position(lzss, lzss->position);
}

static inline void
lzss_emit_literal(struct rar *rar, uint8_t literal)
{
  *lzss_current_pointer(&rar->lzss) = literal;
  rar->lzss.position++;
}

static inline void
lzss_emit_match(struct rar *rar, int offset, int length)
{
  int dstoffs = lzss_current_offset(&rar->lzss);
  int srcoffs = (dstoffs - offset) & lzss_mask(&rar->lzss);
  int l, li, remaining;
  unsigned char *d, *s;

  remaining = length;
  while (remaining > 0) {
    l = remaining;
    if (dstoffs > srcoffs) {
      if (l > lzss_size(&rar->lzss) - dstoffs)
        l = lzss_size(&rar->lzss) - dstoffs;
    } else {
      if (l > lzss_size(&rar->lzss) - srcoffs)
        l = lzss_size(&rar->lzss) - srcoffs;
    }
    d = &(rar->lzss.window[dstoffs]);
    s = &(rar->lzss.window[srcoffs]);
    if ((dstoffs + l < srcoffs) || (srcoffs + l < dstoffs))
      memcpy(d, s, l);
    else {
      for (li = 0; li < l; li++)
        d[li] = s[li];
    }
    remaining -= l;
    dstoffs = (dstoffs + l) & lzss_mask(&(rar->lzss));
    srcoffs = (srcoffs + l) & lzss_mask(&(rar->lzss));
  }
  rar->lzss.position += length;
}

static Byte
ppmd_read(void *p)
{
  struct archive_read *a = ((IByteIn*)p)->a;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);
  Byte b;
  if (!rar_br_read_ahead(a, br, 8))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    rar->valid = 0;
    return 0;
  }
  b = rar_br_bits(br, 8);
  rar_br_consume(br, 8);
  return b;
}

int
archive_read_support_format_rar(struct archive *_a)
{
  struct archive_read *a = (struct archive_read *)_a;
  struct rar *rar;
  int r;

  archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
                      "archive_read_support_format_rar");

  rar = (struct rar *)calloc(sizeof(*rar), 1);
  if (rar == NULL)
  {
    archive_set_error(&a->archive, ENOMEM, "Can't allocate rar data");
    return (ARCHIVE_FATAL);
  }

	/*
	 * Until enough data has been read, we cannot tell about
	 * any encrypted entries yet.
	 */
	rar->has_encrypted_entries = ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;

  r = __archive_read_register_format(a,
                                     rar,
                                     "rar",
                                     archive_read_format_rar_bid,
                                     archive_read_format_rar_options,
                                     archive_read_format_rar_read_header,
                                     archive_read_format_rar_read_data,
                                     archive_read_format_rar_read_data_skip,
                                     archive_read_format_rar_seek_data,
                                     archive_read_format_rar_cleanup,
                                     archive_read_support_format_rar_capabilities,
                                     archive_read_format_rar_has_encrypted_entries);

  if (r != ARCHIVE_OK)
    free(rar);
  return (r);
}

static int
archive_read_support_format_rar_capabilities(struct archive_read * a)
{
	(void)a; /* UNUSED */
	return (ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA
			| ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA);
}

static int
archive_read_format_rar_has_encrypted_entries(struct archive_read *_a)
{
	if (_a && _a->format) {
		struct rar * rar = (struct rar *)_a->format->data;
		if (rar) {
			return rar->has_encrypted_entries;
		}
	}
	return ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;
}


static int
archive_read_format_rar_bid(struct archive_read *a, int best_bid)
{
  const char *p;

  /* If there's already a bid > 30, we'll never win. */
  if (best_bid > 30)
	  return (-1);

  if ((p = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (-1);

  if (memcmp(p, RAR_SIGNATURE, 7) == 0)
    return (30);

  if ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0) {
    /* This is a PE file */
    ssize_t offset = 0x10000;
    ssize_t window = 4096;
    ssize_t bytes_avail;
    while (offset + window <= (1024 * 128)) {
      const char *buff = __archive_read_ahead(a, offset + window, &bytes_avail);
      if (buff == NULL) {
        /* Remaining bytes are less than window. */
        window >>= 1;
        if (window < 0x40)
          return (0);
        continue;
      }
      p = buff + offset;
      while (p + 7 < buff + bytes_avail) {
        if (memcmp(p, RAR_SIGNATURE, 7) == 0)
          return (30);
        p += 0x10;
      }
      offset = p - buff;
    }
  }
  return (0);
}

static int
skip_sfx(struct archive_read *a)
{
  const void *h;
  const char *p, *q;
  size_t skip, total;
  ssize_t bytes, window;

  total = 0;
  window = 4096;
  while (total + window <= (1024 * 128)) {
    h = __archive_read_ahead(a, window, &bytes);
    if (h == NULL) {
      /* Remaining bytes are less than window. */
      window >>= 1;
      if (window < 0x40)
      	goto fatal;
      continue;
    }
    if (bytes < 0x40)
      goto fatal;
    p = h;
    q = p + bytes;

    /*
     * Scan ahead until we find something that looks
     * like the RAR header.
     */
    while (p + 7 < q) {
      if (memcmp(p, RAR_SIGNATURE, 7) == 0) {
      	skip = p - (const char *)h;
      	__archive_read_consume(a, skip);
      	return (ARCHIVE_OK);
      }
      p += 0x10;
    }
    skip = p - (const char *)h;
    __archive_read_consume(a, skip);
	total += skip;
  }
fatal:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Couldn't find out RAR header");
  return (ARCHIVE_FATAL);
}

static int
archive_read_format_rar_options(struct archive_read *a,
    const char *key, const char *val)
{
  struct rar *rar;
  int ret = ARCHIVE_FAILED;

  rar = (struct rar *)(a->format->data);
  if (strcmp(key, "hdrcharset")  == 0) {
    if (val == NULL || val[0] == 0)
      archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
          "rar: hdrcharset option needs a character-set name");
    else {
      rar->opt_sconv =
          archive_string_conversion_from_charset(
              &a->archive, val, 0);
      if (rar->opt_sconv != NULL)
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
archive_read_format_rar_read_header(struct archive_read *a,
                                    struct archive_entry *entry)
{
  const void *h;
  const char *p;
  struct rar *rar;
  size_t skip;
  char head_type;
  int ret;
  unsigned flags;
  unsigned long crc32_expected;

  a->archive.archive_format = ARCHIVE_FORMAT_RAR;
  if (a->archive.archive_format_name == NULL)
    a->archive.archive_format_name = "RAR";

  rar = (struct rar *)(a->format->data);

  /*
   * It should be sufficient to call archive_read_next_header() for
   * a reader to determine if an entry is encrypted or not. If the
   * encryption of an entry is only detectable when calling
   * archive_read_data(), so be it. We'll do the same check there
   * as well.
   */
  if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
	  rar->has_encrypted_entries = 0;
  }

  /* RAR files can be generated without EOF headers, so return ARCHIVE_EOF if
  * this fails.
  */
  if ((h = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (ARCHIVE_EOF);

  p = h;
  if (rar->found_first_header == 0 &&
     ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0)) {
    /* This is an executable ? Must be self-extracting... */
    ret = skip_sfx(a);
    if (ret < ARCHIVE_WARN)
      return (ret);
  }
  rar->found_first_header = 1;

  while (1)
  {
    unsigned long crc32_val;

    if ((h = __archive_read_ahead(a, 7, NULL)) == NULL)
      return (ARCHIVE_FATAL);
    p = h;

    head_type = p[2];
    switch(head_type)
    {
    case MARK_HEAD:
      if (memcmp(p, RAR_SIGNATURE, 7) != 0) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid marker header");
        return (ARCHIVE_FATAL);
      }
      __archive_read_consume(a, 7);
      break;

    case MAIN_HEAD:
      rar->main_flags = archive_le16dec(p + 3);
      skip = archive_le16dec(p + 5);
      if (skip < 7 + sizeof(rar->reserved1) + sizeof(rar->reserved2)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid header size");
        return (ARCHIVE_FATAL);
      }
      if ((h = __archive_read_ahead(a, skip, NULL)) == NULL)
        return (ARCHIVE_FATAL);
      p = h;
      memcpy(rar->reserved1, p + 7, sizeof(rar->reserved1));
      memcpy(rar->reserved2, p + 7 + sizeof(rar->reserved1),
             sizeof(rar->reserved2));
      if (rar->main_flags & MHD_ENCRYPTVER) {
        if (skip < 7 + sizeof(rar->reserved1) + sizeof(rar->reserved2)+1) {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
            "Invalid header size");
          return (ARCHIVE_FATAL);
        }
        rar->encryptver = *(p + 7 + sizeof(rar->reserved1) +
                            sizeof(rar->reserved2));
      }

      /* Main header is password encrypted, so we cannot read any
         file names or any other info about files from the header. */
      if (rar->main_flags & MHD_PASSWORD)
      {
        archive_entry_set_is_metadata_encrypted(entry, 1);
        archive_entry_set_is_data_encrypted(entry, 1);
        rar->has_encrypted_entries = 1;
         archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "RAR encryption support unavailable.");
        return (ARCHIVE_FATAL);
      }

      crc32_val = crc32(0, (const unsigned char *)p + 2, (unsigned)skip - 2);
      if ((crc32_val & 0xffff) != archive_le16dec(p)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Header CRC error");
        return (ARCHIVE_FATAL);
      }
      __archive_read_consume(a, skip);
      break;

    case FILE_HEAD:
      return read_header(a, entry, head_type);

    case COMM_HEAD:
    case AV_HEAD:
    case SUB_HEAD:
    case PROTECT_HEAD:
    case SIGN_HEAD:
    case ENDARC_HEAD:
      flags = archive_le16dec(p + 3);
      skip = archive_le16dec(p + 5);
      if (skip < 7) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid header size too small");
        return (ARCHIVE_FATAL);
      }
      if (flags & HD_ADD_SIZE_PRESENT)
      {
        if (skip < 7 + 4) {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
            "Invalid header size too small");
          return (ARCHIVE_FATAL);
        }
        if ((h = __archive_read_ahead(a, skip, NULL)) == NULL)
          return (ARCHIVE_FATAL);
        p = h;
        skip += archive_le32dec(p + 7);
      }

      /* Skip over the 2-byte CRC at the beginning of the header. */
      crc32_expected = archive_le16dec(p);
      __archive_read_consume(a, 2);
      skip -= 2;

      /* Skim the entire header and compute the CRC. */
      crc32_val = 0;
      while (skip > 0) {
	      size_t to_read = skip;
	      ssize_t did_read;
	      if (to_read > 32 * 1024) {
		      to_read = 32 * 1024;
	      }
	      if ((h = __archive_read_ahead(a, to_read, &did_read)) == NULL) {
		      return (ARCHIVE_FATAL);
	      }
	      p = h;
	      crc32_val = crc32(crc32_val, (const unsigned char *)p, (unsigned)did_read);
	      __archive_read_consume(a, did_read);
	      skip -= did_read;
      }
      if ((crc32_val & 0xffff) != crc32_expected) {
	      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		  "Header CRC error");
	      return (ARCHIVE_FATAL);
      }
      if (head_type == ENDARC_HEAD)
	      return (ARCHIVE_EOF);
      break;

    case NEWSUB_HEAD:
      if ((ret = read_header(a, entry, head_type)) < ARCHIVE_WARN)
        return ret;
      break;

    default:
      archive_set_error(&a->archive,  ARCHIVE_ERRNO_FILE_FORMAT,
                        "Bad RAR file");
      return (ARCHIVE_FATAL);
    }
  }
}

static int
archive_read_format_rar_read_data(struct archive_read *a, const void **buff,
                                  size_t *size, int64_t *offset)
{
  struct rar *rar = (struct rar *)(a->format->data);
  int ret;

  if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
	  rar->has_encrypted_entries = 0;
  }

  if (rar->bytes_unconsumed > 0) {
      /* Consume as much as the decompressor actually used. */
      __archive_read_consume(a, rar->bytes_unconsumed);
      rar->bytes_unconsumed = 0;
  }

  *buff = NULL;
  if (rar->entry_eof || rar->offset_seek >= rar->unp_size) {
    *size = 0;
    *offset = rar->offset;
    if (*offset < rar->unp_size)
      *offset = rar->unp_size;
    return (ARCHIVE_EOF);
  }

  switch (rar->compression_method)
  {
  case COMPRESS_METHOD_STORE:
    ret = read_data_stored(a, buff, size, offset);
    break;

  case COMPRESS_METHOD_FASTEST:
  case COMPRESS_METHOD_FAST:
  case COMPRESS_METHOD_NORMAL:
  case COMPRESS_METHOD_GOOD:
  case COMPRESS_METHOD_BEST:
    ret = read_data_compressed(a, buff, size, offset);
    if (ret != ARCHIVE_OK && ret != ARCHIVE_WARN)
      __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
    break;

  default:
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unsupported compression method for RAR file.");
    ret = ARCHIVE_FATAL;
    break;
  }
  return (ret);
}

static int
archive_read_format_rar_read_data_skip(struct archive_read *a)
{
  struct rar *rar;
  int64_t bytes_skipped;
  int ret;

  rar = (struct rar *)(a->format->data);

  if (rar->bytes_unconsumed > 0) {
      /* Consume as much as the decompressor actually used. */
      __archive_read_consume(a, rar->bytes_unconsumed);
      rar->bytes_unconsumed = 0;
  }

  if (rar->bytes_remaining > 0) {
    bytes_skipped = __archive_read_consume(a, rar->bytes_remaining);
    if (bytes_skipped < 0)
      return (ARCHIVE_FATAL);
  }

  /* Compressed data to skip must be read from each header in a multivolume
   * archive.
   */
  if (rar->main_flags & MHD_VOLUME && rar->file_flags & FHD_SPLIT_AFTER)
  {
    ret = archive_read_format_rar_read_header(a, a->entry);
    if (ret == (ARCHIVE_EOF))
      ret = archive_read_format_rar_read_header(a, a->entry);
    if (ret != (ARCHIVE_OK))
      return ret;
    return archive_read_format_rar_read_data_skip(a);
  }

  return (ARCHIVE_OK);
}

static int64_t
archive_read_format_rar_seek_data(struct archive_read *a, int64_t offset,
    int whence)
{
  int64_t client_offset, ret;
  unsigned int i;
  struct rar *rar = (struct rar *)(a->format->data);

  if (rar->compression_method == COMPRESS_METHOD_STORE)
  {
    /* Modify the offset for use with SEEK_SET */
    switch (whence)
    {
      case SEEK_CUR:
        client_offset = rar->offset_seek;
        break;
      case SEEK_END:
        client_offset = rar->unp_size;
        break;
      case SEEK_SET:
      default:
        client_offset = 0;
    }
    client_offset += offset;
    if (client_offset < 0)
    {
      /* Can't seek past beginning of data block */
      return -1;
    }
    else if (client_offset > rar->unp_size)
    {
      /*
       * Set the returned offset but only seek to the end of
       * the data block.
       */
      rar->offset_seek = client_offset;
      client_offset = rar->unp_size;
    }

    client_offset += rar->dbo[0].start_offset;
    i = 0;
    while (i < rar->cursor)
    {
      i++;
      client_offset += rar->dbo[i].start_offset - rar->dbo[i-1].end_offset;
    }
    if (rar->main_flags & MHD_VOLUME)
    {
      /* Find the appropriate offset among the multivolume archive */
      while (1)
      {
        if (client_offset < rar->dbo[rar->cursor].start_offset &&
          rar->file_flags & FHD_SPLIT_BEFORE)
        {
          /* Search backwards for the correct data block */
          if (rar->cursor == 0)
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Attempt to seek past beginning of RAR data block");
            return (ARCHIVE_FAILED);
          }
          rar->cursor--;
          client_offset -= rar->dbo[rar->cursor+1].start_offset -
            rar->dbo[rar->cursor].end_offset;
          if (client_offset < rar->dbo[rar->cursor].start_offset)
            continue;
          ret = __archive_read_seek(a, rar->dbo[rar->cursor].start_offset -
            rar->dbo[rar->cursor].header_size, SEEK_SET);
          if (ret < (ARCHIVE_OK))
            return ret;
          ret = archive_read_format_rar_read_header(a, a->entry);
          if (ret != (ARCHIVE_OK))
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Error during seek of RAR file");
            return (ARCHIVE_FAILED);
          }
          rar->cursor--;
          break;
        }
        else if (client_offset > rar->dbo[rar->cursor].end_offset &&
          rar->file_flags & FHD_SPLIT_AFTER)
        {
          /* Search forward for the correct data block */
          rar->cursor++;
          if (rar->cursor < rar->nodes &&
            client_offset > rar->dbo[rar->cursor].end_offset)
          {
            client_offset += rar->dbo[rar->cursor].start_offset -
              rar->dbo[rar->cursor-1].end_offset;
            continue;
          }
          rar->cursor--;
          ret = __archive_read_seek(a, rar->dbo[rar->cursor].end_offset,
                                    SEEK_SET);
          if (ret < (ARCHIVE_OK))
            return ret;
          ret = archive_read_format_rar_read_header(a, a->entry);
          if (ret == (ARCHIVE_EOF))
          {
            rar->has_endarc_header = 1;
            ret = archive_read_format_rar_read_header(a, a->entry);
          }
          if (ret != (ARCHIVE_OK))
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Error during seek of RAR file");
            return (ARCHIVE_FAILED);
          }
          client_offset += rar->dbo[rar->cursor].start_offset -
            rar->dbo[rar->cursor-1].end_offset;
          continue;
        }
        break;
      }
    }

    ret = __archive_read_seek(a, client_offset, SEEK_SET);
    if (ret < (ARCHIVE_OK))
      return ret;
    rar->bytes_remaining = rar->dbo[rar->cursor].end_offset - ret;
    i = rar->cursor;
    while (i > 0)
    {
      i--;
      ret -= rar->dbo[i+1].start_offset - rar->dbo[i].end_offset;
    }
    ret -= rar->dbo[0].start_offset;

    /* Always restart reading the file after a seek */
    __archive_reset_read_data(&a->archive);

    rar->bytes_unconsumed = 0;
    rar->offset = 0;

    /*
     * If a seek past the end of file was requested, return the requested
     * offset.
     */
    if (ret == rar->unp_size && rar->offset_seek > rar->unp_size)
      return rar->offset_seek;

    /* Return the new offset */
    rar->offset_seek = ret;
    return rar->offset_seek;
  }
  else
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
      "Seeking of compressed RAR files is unsupported");
  }
  return (ARCHIVE_FAILED);
}

static int
archive_read_format_rar_cleanup(struct archive_read *a)
{
  struct rar *rar;

  rar = (struct rar *)(a->format->data);
  free_codes(a);
  free(rar->filename);
  free(rar->filename_save);
  free(rar->dbo);
  free(rar->unp_buffer);
  free(rar->lzss.window);
  __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
  free(rar);
  (a->format->data) = NULL;
  return (ARCHIVE_OK);
}

static int
read_header(struct archive_read *a, struct archive_entry *entry,
            char head_type)
{
  const void *h;
  const char *p, *endp;
  struct rar *rar;
  struct rar_header rar_header;
  struct rar_file_header file_header;
  int64_t header_size;
  unsigned filename_size, end;
  char *filename;
  char *strp;
  char packed_size[8];
  char unp_size[8];
  int ttime;
  struct archive_string_conv *sconv, *fn_sconv;
  unsigned long crc32_val;
  int ret = (ARCHIVE_OK), ret2;

  rar = (struct rar *)(a->format->data);

  /* Setup a string conversion object for non-rar-unicode filenames. */
  sconv = rar->opt_sconv;
  if (sconv == NULL) {
    if (!rar->init_default_conversion) {
      rar->sconv_default =
          archive_string_default_conversion_for_read(
            &(a->archive));
      rar->init_default_conversion = 1;
    }
    sconv = rar->sconv_default;
  }


  if ((h = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (ARCHIVE_FATAL);
  p = h;
  memcpy(&rar_header, p, sizeof(rar_header));
  rar->file_flags = archive_le16dec(rar_header.flags);
  header_size = archive_le16dec(rar_header.size);
  if (header_size < (int64_t)sizeof(file_header) + 7) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Invalid header size");
    return (ARCHIVE_FATAL);
  }
  crc32_val = crc32(0, (const unsigned char *)p + 2, 7 - 2);
  __archive_read_consume(a, 7);

  if (!(rar->file_flags & FHD_SOLID))
  {
    rar->compression_method = 0;
    rar->packed_size = 0;
    rar->unp_size = 0;
    rar->mtime = 0;
    rar->ctime = 0;
    rar->atime = 0;
    rar->arctime = 0;
    rar->mode = 0;
    memset(&rar->salt, 0, sizeof(rar->salt));
    rar->atime = 0;
    rar->ansec = 0;
    rar->ctime = 0;
    rar->cnsec = 0;
    rar->mtime = 0;
    rar->mnsec = 0;
    rar->arctime = 0;
    rar->arcnsec = 0;
  }
  else
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "RAR solid archive support unavailable.");
    return (ARCHIVE_FATAL);
  }

  if ((h = __archive_read_ahead(a, (size_t)header_size - 7, NULL)) == NULL)
    return (ARCHIVE_FATAL);

  /* File Header CRC check. */
  crc32_val = crc32(crc32_val, h, (unsigned)(header_size - 7));
  if ((crc32_val & 0xffff) != archive_le16dec(rar_header.crc)) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Header CRC error");
    return (ARCHIVE_FATAL);
  }
  /* If no CRC error, Go on parsing File Header. */
  p = h;
  endp = p + header_size - 7;
  memcpy(&file_header, p, sizeof(file_header));
  p += sizeof(file_header);

  rar->compression_method = file_header.method;

  ttime = archive_le32dec(file_header.file_time);
  rar->mtime = get_time(ttime);

  rar->file_crc = archive_le32dec(file_header.file_crc);

  if (rar->file_flags & FHD_PASSWORD)
  {
	archive_entry_set_is_data_encrypted(entry, 1);
	rar->has_encrypted_entries = 1;
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "RAR encryption support unavailable.");
    /* Since it is only the data part itself that is encrypted we can at least
       extract information about the currently processed entry and don't need
       to return ARCHIVE_FATAL here. */
    /*return (ARCHIVE_FATAL);*/
  }

  if (rar->file_flags & FHD_LARGE)
  {
    memcpy(packed_size, file_header.pack_size, 4);
    memcpy(packed_size + 4, p, 4); /* High pack size */
    p += 4;
    memcpy(unp_size, file_header.unp_size, 4);
    memcpy(unp_size + 4, p, 4); /* High unpack size */
    p += 4;
    rar->packed_size = archive_le64dec(&packed_size);
    rar->unp_size = archive_le64dec(&unp_size);
  }
  else
  {
    rar->packed_size = archive_le32dec(file_header.pack_size);
    rar->unp_size = archive_le32dec(file_header.unp_size);
  }

  if (rar->packed_size < 0 || rar->unp_size < 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid sizes specified.");
    return (ARCHIVE_FATAL);
  }

  rar->bytes_remaining = rar->packed_size;

  /* TODO: RARv3 subblocks contain comments. For now the complete block is
   * consumed at the end.
   */
  if (head_type == NEWSUB_HEAD) {
    size_t distance = p - (const char *)h;
    header_size += rar->packed_size;
    /* Make sure we have the extended data. */
    if ((h = __archive_read_ahead(a, (size_t)header_size - 7, NULL)) == NULL)
        return (ARCHIVE_FATAL);
    p = h;
    endp = p + header_size - 7;
    p += distance;
  }

  filename_size = archive_le16dec(file_header.name_size);
  if (p + filename_size > endp) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Invalid filename size");
    return (ARCHIVE_FATAL);
  }
  if (rar->filename_allocated < filename_size * 2 + 2) {
    char *newptr;
    size_t newsize = filename_size * 2 + 2;
    newptr = realloc(rar->filename, newsize);
    if (newptr == NULL) {
      archive_set_error(&a->archive, ENOMEM,
                        "Couldn't allocate memory.");
      return (ARCHIVE_FATAL);
    }
    rar->filename = newptr;
    rar->filename_allocated = newsize;
  }
  filename = rar->filename;
  memcpy(filename, p, filename_size);
  filename[filename_size] = '\0';
  if (rar->file_flags & FHD_UNICODE)
  {
    if (filename_size != strlen(filename))
    {
      unsigned char highbyte, flagbits, flagbyte;
      unsigned fn_end, offset;

      end = filename_size;
      fn_end = filename_size * 2;
      filename_size = 0;
      offset = (unsigned)strlen(filename) + 1;
      highbyte = *(p + offset++);
      flagbits = 0;
      flagbyte = 0;
      while (offset < end && filename_size < fn_end)
      {
        if (!flagbits)
        {
          flagbyte = *(p + offset++);
          flagbits = 8;
        }

        flagbits -= 2;
        switch((flagbyte >> flagbits) & 3)
        {
          case 0:
            filename[filename_size++] = '\0';
            filename[filename_size++] = *(p + offset++);
            break;
          case 1:
            filename[filename_size++] = highbyte;
            filename[filename_size++] = *(p + offset++);
            break;
          case 2:
            filename[filename_size++] = *(p + offset + 1);
            filename[filename_size++] = *(p + offset);
            offset += 2;
            break;
          case 3:
          {
            char extra, high;
            uint8_t length = *(p + offset++);

            if (length & 0x80) {
              extra = *(p + offset++);
              high = (char)highbyte;
            } else
              extra = high = 0;
            length = (length & 0x7f) + 2;
            while (length && filename_size < fn_end) {
              unsigned cp = filename_size >> 1;
              filename[filename_size++] = high;
              filename[filename_size++] = p[cp] + extra;
              length--;
            }
          }
          break;
        }
      }
      if (filename_size > fn_end) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid filename");
        return (ARCHIVE_FATAL);
      }
      filename[filename_size++] = '\0';
      /*
       * Do not increment filename_size here as the computations below
       * add the space for the terminating NUL explicitly.
       */
      filename[filename_size] = '\0';

      /* Decoded unicode form is UTF-16BE, so we have to update a string
       * conversion object for it. */
      if (rar->sconv_utf16be == NULL) {
        rar->sconv_utf16be = archive_string_conversion_from_charset(
           &a->archive, "UTF-16BE", 1);
        if (rar->sconv_utf16be == NULL)
          return (ARCHIVE_FATAL);
      }
      fn_sconv = rar->sconv_utf16be;

      strp = filename;
      while (memcmp(strp, "\x00\x00", 2))
      {
        if (!memcmp(strp, "\x00\\", 2))
          *(strp + 1) = '/';
        strp += 2;
      }
      p += offset;
    } else {
      /*
       * If FHD_UNICODE is set but no unicode data, this file name form
       * is UTF-8, so we have to update a string conversion object for
       * it accordingly.
       */
      if (rar->sconv_utf8 == NULL) {
        rar->sconv_utf8 = archive_string_conversion_from_charset(
           &a->archive, "UTF-8", 1);
        if (rar->sconv_utf8 == NULL)
          return (ARCHIVE_FATAL);
      }
      fn_sconv = rar->sconv_utf8;
      while ((strp = strchr(filename, '\\')) != NULL)
        *strp = '/';
      p += filename_size;
    }
  }
  else
  {
    fn_sconv = sconv;
    while ((strp = strchr(filename, '\\')) != NULL)
      *strp = '/';
    p += filename_size;
  }

  /* Split file in multivolume RAR. No more need to process header. */
  if (rar->filename_save &&
    filename_size == rar->filename_save_size &&
    !memcmp(rar->filename, rar->filename_save, filename_size + 1))
  {
    __archive_read_consume(a, header_size - 7);
    rar->cursor++;
    if (rar->cursor >= rar->nodes)
    {
      rar->nodes++;
      if ((rar->dbo =
        realloc(rar->dbo, sizeof(*rar->dbo) * rar->nodes)) == NULL)
      {
        archive_set_error(&a->archive, ENOMEM, "Couldn't allocate memory.");
        return (ARCHIVE_FATAL);
      }
      rar->dbo[rar->cursor].header_size = header_size;
      rar->dbo[rar->cursor].start_offset = -1;
      rar->dbo[rar->cursor].end_offset = -1;
    }
    if (rar->dbo[rar->cursor].start_offset < 0)
    {
      rar->dbo[rar->cursor].start_offset = a->filter->position;
      rar->dbo[rar->cursor].end_offset = rar->dbo[rar->cursor].start_offset +
        rar->packed_size;
    }
    return ret;
  }
  else if (rar->filename_must_match)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Mismatch of file parts split across multi-volume archive");
    return (ARCHIVE_FATAL);
  }

  rar->filename_save = (char*)realloc(rar->filename_save,
                                      filename_size + 1);
  memcpy(rar->filename_save, rar->filename, filename_size + 1);
  rar->filename_save_size = filename_size;

  /* Set info for seeking */
  free(rar->dbo);
  if ((rar->dbo = calloc(1, sizeof(*rar->dbo))) == NULL)
  {
    archive_set_error(&a->archive, ENOMEM, "Couldn't allocate memory.");
    return (ARCHIVE_FATAL);
  }
  rar->dbo[0].header_size = header_size;
  rar->dbo[0].start_offset = -1;
  rar->dbo[0].end_offset = -1;
  rar->cursor = 0;
  rar->nodes = 1;

  if (rar->file_flags & FHD_SALT)
  {
    if (p + 8 > endp) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
        "Invalid header size");
      return (ARCHIVE_FATAL);
    }
    memcpy(rar->salt, p, 8);
    p += 8;
  }

  if (rar->file_flags & FHD_EXTTIME) {
    if (read_exttime(p, rar, endp) < 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
        "Invalid header size");
      return (ARCHIVE_FATAL);
    }
  }

  __archive_read_consume(a, header_size - 7);
  rar->dbo[0].start_offset = a->filter->position;
  rar->dbo[0].end_offset = rar->dbo[0].start_offset + rar->packed_size;

  switch(file_header.host_os)
  {
  case OS_MSDOS:
  case OS_OS2:
  case OS_WIN32:
    rar->mode = archive_le32dec(file_header.file_attr);
    if (rar->mode & FILE_ATTRIBUTE_DIRECTORY)
      rar->mode = AE_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
    else
      rar->mode = AE_IFREG;
    rar->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    break;

  case OS_UNIX:
  case OS_MAC_OS:
  case OS_BEOS:
    rar->mode = archive_le32dec(file_header.file_attr);
    break;

  default:
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unknown file attributes from RAR file's host OS");
    return (ARCHIVE_FATAL);
  }

  rar->bytes_uncopied = rar->bytes_unconsumed = 0;
  rar->lzss.position = rar->offset = 0;
  rar->offset_seek = 0;
  rar->dictionary_size = 0;
  rar->offset_outgoing = 0;
  rar->br.cache_avail = 0;
  rar->br.avail_in = 0;
  rar->crc_calculated = 0;
  rar->entry_eof = 0;
  rar->valid = 1;
  rar->is_ppmd_block = 0;
  rar->start_new_table = 1;
  free(rar->unp_buffer);
  rar->unp_buffer = NULL;
  rar->unp_offset = 0;
  rar->unp_buffer_size = UNP_BUFFER_SIZE;
  memset(rar->lengthtable, 0, sizeof(rar->lengthtable));
  __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
  rar->ppmd_valid = rar->ppmd_eod = 0;

  /* Don't set any archive entries for non-file header types */
  if (head_type == NEWSUB_HEAD)
    return ret;

  archive_entry_set_mtime(entry, rar->mtime, rar->mnsec);
  archive_entry_set_ctime(entry, rar->ctime, rar->cnsec);
  archive_entry_set_atime(entry, rar->atime, rar->ansec);
  archive_entry_set_size(entry, rar->unp_size);
  archive_entry_set_mode(entry, rar->mode);

  if (archive_entry_copy_pathname_l(entry, filename, filename_size, fn_sconv))
  {
    if (errno == ENOMEM)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Can't allocate memory for Pathname");
      return (ARCHIVE_FATAL);
    }
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Pathname cannot be converted from %s to current locale.",
                      archive_string_conversion_charset_name(fn_sconv));
    ret = (ARCHIVE_WARN);
  }

  if (((rar->mode) & AE_IFMT) == AE_IFLNK)
  {
    /* Make sure a symbolic-link file does not have its body. */
    rar->bytes_remaining = 0;
    archive_entry_set_size(entry, 0);

    /* Read a symbolic-link name. */
    if ((ret2 = read_symlink_stored(a, entry, sconv)) < (ARCHIVE_WARN))
      return ret2;
    if (ret > ret2)
      ret = ret2;
  }

  if (rar->bytes_remaining == 0)
    rar->entry_eof = 1;

  return ret;
}

static time_t
get_time(int ttime)
{
  struct tm tm;
  tm.tm_sec = 2 * (ttime & 0x1f);
  tm.tm_min = (ttime >> 5) & 0x3f;
  tm.tm_hour = (ttime >> 11) & 0x1f;
  tm.tm_mday = (ttime >> 16) & 0x1f;
  tm.tm_mon = ((ttime >> 21) & 0x0f) - 1;
  tm.tm_year = ((ttime >> 25) & 0x7f) + 80;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static int
read_exttime(const char *p, struct rar *rar, const char *endp)
{
  unsigned rmode, flags, rem, j, count;
  int ttime, i;
  struct tm *tm;
  time_t t;
  long nsec;

  if (p + 2 > endp)
    return (-1);
  flags = archive_le16dec(p);
  p += 2;

  for (i = 3; i >= 0; i--)
  {
    t = 0;
    if (i == 3)
      t = rar->mtime;
    rmode = flags >> i * 4;
    if (rmode & 8)
    {
      if (!t)
      {
        if (p + 4 > endp)
          return (-1);
        ttime = archive_le32dec(p);
        t = get_time(ttime);
        p += 4;
      }
      rem = 0;
      count = rmode & 3;
      if (p + count > endp)
        return (-1);
      for (j = 0; j < count; j++)
      {
        rem = (((unsigned)(unsigned char)*p) << 16) | (rem >> 8);
        p++;
      }
      tm = localtime(&t);
      nsec = tm->tm_sec + rem / NS_UNIT;
      if (rmode & 4)
      {
        tm->tm_sec++;
        t = mktime(tm);
      }
      if (i == 3)
      {
        rar->mtime = t;
        rar->mnsec = nsec;
      }
      else if (i == 2)
      {
        rar->ctime = t;
        rar->cnsec = nsec;
      }
      else if (i == 1)
      {
        rar->atime = t;
        rar->ansec = nsec;
      }
      else
      {
        rar->arctime = t;
        rar->arcnsec = nsec;
      }
    }
  }
  return (0);
}

static int
read_symlink_stored(struct archive_read *a, struct archive_entry *entry,
                    struct archive_string_conv *sconv)
{
  const void *h;
  const char *p;
  struct rar *rar;
  int ret = (ARCHIVE_OK);

  rar = (struct rar *)(a->format->data);
  if ((h = rar_read_ahead(a, (size_t)rar->packed_size, NULL)) == NULL)
    return (ARCHIVE_FATAL);
  p = h;

  if (archive_entry_copy_symlink_l(entry,
      p, (size_t)rar->packed_size, sconv))
  {
    if (errno == ENOMEM)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Can't allocate memory for link");
      return (ARCHIVE_FATAL);
    }
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "link cannot be converted from %s to current locale.",
                      archive_string_conversion_charset_name(sconv));
    ret = (ARCHIVE_WARN);
  }
  __archive_read_consume(a, rar->packed_size);
  return ret;
}

static int
read_data_stored(struct archive_read *a, const void **buff, size_t *size,
                 int64_t *offset)
{
  struct rar *rar;
  ssize_t bytes_avail;

  rar = (struct rar *)(a->format->data);
  if (rar->bytes_remaining == 0 &&
    !(rar->main_flags & MHD_VOLUME && rar->file_flags & FHD_SPLIT_AFTER))
  {
    *buff = NULL;
    *size = 0;
    *offset = rar->offset;
    if (rar->file_crc != rar->crc_calculated) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "File CRC error");
      return (ARCHIVE_FATAL);
    }
    rar->entry_eof = 1;
    return (ARCHIVE_EOF);
  }

  *buff = rar_read_ahead(a, 1, &bytes_avail);
  if (bytes_avail <= 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    return (ARCHIVE_FATAL);
  }

  *size = bytes_avail;
  *offset = rar->offset;
  rar->offset += bytes_avail;
  rar->offset_seek += bytes_avail;
  rar->bytes_remaining -= bytes_avail;
  rar->bytes_unconsumed = bytes_avail;
  /* Calculate File CRC. */
  rar->crc_calculated = crc32(rar->crc_calculated, *buff,
    (unsigned)bytes_avail);
  return (ARCHIVE_OK);
}

static int
read_data_compressed(struct archive_read *a, const void **buff, size_t *size,
               int64_t *offset)
{
  struct rar *rar;
  int64_t start, end, actualend;
  size_t bs;
  int ret = (ARCHIVE_OK), sym, code, lzss_offset, length, i;

  rar = (struct rar *)(a->format->data);

  do {
    if (!rar->valid)
      return (ARCHIVE_FATAL);
    if (rar->ppmd_eod ||
       (rar->dictionary_size && rar->offset >= rar->unp_size))
    {
      if (rar->unp_offset > 0) {
        /*
         * We have unprocessed extracted data. write it out.
         */
        *buff = rar->unp_buffer;
        *size = rar->unp_offset;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;
        /* Calculate File CRC. */
        rar->crc_calculated = crc32(rar->crc_calculated, *buff,
          (unsigned)*size);
        rar->unp_offset = 0;
        return (ARCHIVE_OK);
      }
      *buff = NULL;
      *size = 0;
      *offset = rar->offset;
      if (rar->file_crc != rar->crc_calculated) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "File CRC error");
        return (ARCHIVE_FATAL);
      }
      rar->entry_eof = 1;
      return (ARCHIVE_EOF);
    }

    if (!rar->is_ppmd_block && rar->dictionary_size && rar->bytes_uncopied > 0)
    {
      if (rar->bytes_uncopied > (rar->unp_buffer_size - rar->unp_offset))
        bs = rar->unp_buffer_size - rar->unp_offset;
      else
        bs = (size_t)rar->bytes_uncopied;
      ret = copy_from_lzss_window(a, buff, rar->offset, (int)bs);
      if (ret != ARCHIVE_OK)
        return (ret);
      rar->offset += bs;
      rar->bytes_uncopied -= bs;
      if (*buff != NULL) {
        rar->unp_offset = 0;
        *size = rar->unp_buffer_size;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;
        /* Calculate File CRC. */
        rar->crc_calculated = crc32(rar->crc_calculated, *buff,
          (unsigned)*size);
        return (ret);
      }
      continue;
    }

    if (!rar->br.next_in &&
      (ret = rar_br_preparation(a, &(rar->br))) < ARCHIVE_WARN)
      return (ret);
    if (rar->start_new_table && ((ret = parse_codes(a)) < (ARCHIVE_WARN)))
      return (ret);

    if (rar->is_ppmd_block)
    {
      if ((sym = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
        &rar->ppmd7_context, &rar->range_dec.p)) < 0)
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid symbol");
        return (ARCHIVE_FATAL);
      }
      if(sym != rar->ppmd_escape)
      {
        lzss_emit_literal(rar, sym);
        rar->bytes_uncopied++;
      }
      else
      {
        if ((code = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
          &rar->ppmd7_context, &rar->range_dec.p)) < 0)
        {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Invalid symbol");
          return (ARCHIVE_FATAL);
        }

        switch(code)
        {
          case 0:
            rar->start_new_table = 1;
            return read_data_compressed(a, buff, size, offset);

          case 2:
            rar->ppmd_eod = 1;/* End Of ppmd Data. */
            continue;

          case 3:
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
                              "Parsing filters is unsupported.");
            return (ARCHIVE_FAILED);

          case 4:
            lzss_offset = 0;
            for (i = 2; i >= 0; i--)
            {
              if ((code = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
                &rar->ppmd7_context, &rar->range_dec.p)) < 0)
              {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                  "Invalid symbol");
                return (ARCHIVE_FATAL);
              }
              lzss_offset |= code << (i * 8);
            }
            if ((length = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
              &rar->ppmd7_context, &rar->range_dec.p)) < 0)
            {
              archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                "Invalid symbol");
              return (ARCHIVE_FATAL);
            }
            lzss_emit_match(rar, lzss_offset + 2, length + 32);
            rar->bytes_uncopied += length + 32;
            break;

          case 5:
            if ((length = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
              &rar->ppmd7_context, &rar->range_dec.p)) < 0)
            {
              archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                "Invalid symbol");
              return (ARCHIVE_FATAL);
            }
            lzss_emit_match(rar, 1, length + 4);
            rar->bytes_uncopied += length + 4;
            break;

         default:
           lzss_emit_literal(rar, sym);
           rar->bytes_uncopied++;
        }
      }
    }
    else
    {
      start = rar->offset;
      end = start + rar->dictionary_size;
      rar->filterstart = INT64_MAX;

      if ((actualend = expand(a, end)) < 0)
        return ((int)actualend);

      rar->bytes_uncopied = actualend - start;
      if (rar->bytes_uncopied == 0) {
          /* Broken RAR files cause this case.
          * NOTE: If this case were possible on a normal RAR file
          * we would find out where it was actually bad and
          * what we would do to solve it. */
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Internal error extracting RAR file");
          return (ARCHIVE_FATAL);
      }
    }
    if (rar->bytes_uncopied > (rar->unp_buffer_size - rar->unp_offset))
      bs = rar->unp_buffer_size - rar->unp_offset;
    else
      bs = (size_t)rar->bytes_uncopied;
    ret = copy_from_lzss_window(a, buff, rar->offset, (int)bs);
    if (ret != ARCHIVE_OK)
      return (ret);
    rar->offset += bs;
    rar->bytes_uncopied -= bs;
    /*
     * If *buff is NULL, it means unp_buffer is not full.
     * So we have to continue extracting a RAR file.
     */
  } while (*buff == NULL);

  rar->unp_offset = 0;
  *size = rar->unp_buffer_size;
  *offset = rar->offset_outgoing;
  rar->offset_outgoing += *size;
  /* Calculate File CRC. */
  rar->crc_calculated = crc32(rar->crc_calculated, *buff, (unsigned)*size);
  return ret;
}

static int
parse_codes(struct archive_read *a)
{
  int i, j, val, n, r;
  unsigned char bitlengths[MAX_SYMBOLS], zerocount, ppmd_flags;
  unsigned int maxorder;
  struct huffman_code precode;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);

  free_codes(a);

  /* Skip to the next byte */
  rar_br_consume_unalined_bits(br);

  /* PPMd block flag */
  if (!rar_br_read_ahead(a, br, 1))
    goto truncated_data;
  if ((rar->is_ppmd_block = rar_br_bits(br, 1)) != 0)
  {
    rar_br_consume(br, 1);
    if (!rar_br_read_ahead(a, br, 7))
      goto truncated_data;
    ppmd_flags = rar_br_bits(br, 7);
    rar_br_consume(br, 7);

    /* Memory is allocated in MB */
    if (ppmd_flags & 0x20)
    {
      if (!rar_br_read_ahead(a, br, 8))
        goto truncated_data;
      rar->dictionary_size = (rar_br_bits(br, 8) + 1) << 20;
      rar_br_consume(br, 8);
    }

    if (ppmd_flags & 0x40)
    {
      if (!rar_br_read_ahead(a, br, 8))
        goto truncated_data;
      rar->ppmd_escape = rar->ppmd7_context.InitEsc = rar_br_bits(br, 8);
      rar_br_consume(br, 8);
    }
    else
      rar->ppmd_escape = 2;

    if (ppmd_flags & 0x20)
    {
      maxorder = (ppmd_flags & 0x1F) + 1;
      if(maxorder > 16)
        maxorder = 16 + (maxorder - 16) * 3;

      if (maxorder == 1)
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Truncated RAR file data");
        return (ARCHIVE_FATAL);
      }

      /* Make sure ppmd7_contest is freed before Ppmd7_Construct
       * because reading a broken file cause this abnormal sequence. */
      __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);

      rar->bytein.a = a;
      rar->bytein.Read = &ppmd_read;
      __archive_ppmd7_functions.PpmdRAR_RangeDec_CreateVTable(&rar->range_dec);
      rar->range_dec.Stream = &rar->bytein;
      __archive_ppmd7_functions.Ppmd7_Construct(&rar->ppmd7_context);

      if (rar->dictionary_size == 0) {
	      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid zero dictionary size");
	      return (ARCHIVE_FATAL);
      }

      if (!__archive_ppmd7_functions.Ppmd7_Alloc(&rar->ppmd7_context,
        rar->dictionary_size))
      {
        archive_set_error(&a->archive, ENOMEM,
                          "Out of memory");
        return (ARCHIVE_FATAL);
      }
      if (!__archive_ppmd7_functions.PpmdRAR_RangeDec_Init(&rar->range_dec))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Unable to initialize PPMd range decoder");
        return (ARCHIVE_FATAL);
      }
      __archive_ppmd7_functions.Ppmd7_Init(&rar->ppmd7_context, maxorder);
      rar->ppmd_valid = 1;
    }
    else
    {
      if (!rar->ppmd_valid) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid PPMd sequence");
        return (ARCHIVE_FATAL);
      }
      if (!__archive_ppmd7_functions.PpmdRAR_RangeDec_Init(&rar->range_dec))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Unable to initialize PPMd range decoder");
        return (ARCHIVE_FATAL);
      }
    }
  }
  else
  {
    rar_br_consume(br, 1);

    /* Keep existing table flag */
    if (!rar_br_read_ahead(a, br, 1))
      goto truncated_data;
    if (!rar_br_bits(br, 1))
      memset(rar->lengthtable, 0, sizeof(rar->lengthtable));
    rar_br_consume(br, 1);

    memset(&bitlengths, 0, sizeof(bitlengths));
    for (i = 0; i < MAX_SYMBOLS;)
    {
      if (!rar_br_read_ahead(a, br, 4))
        goto truncated_data;
      bitlengths[i++] = rar_br_bits(br, 4);
      rar_br_consume(br, 4);
      if (bitlengths[i-1] == 0xF)
      {
        if (!rar_br_read_ahead(a, br, 4))
          goto truncated_data;
        zerocount = rar_br_bits(br, 4);
        rar_br_consume(br, 4);
        if (zerocount)
        {
          i--;
          for (j = 0; j < zerocount + 2 && i < MAX_SYMBOLS; j++)
            bitlengths[i++] = 0;
        }
      }
    }

    memset(&precode, 0, sizeof(precode));
    r = create_code(a, &precode, bitlengths, MAX_SYMBOLS, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK) {
      free(precode.tree);
      free(precode.table);
      return (r);
    }

    for (i = 0; i < HUFFMAN_TABLE_SIZE;)
    {
      if ((val = read_next_symbol(a, &precode)) < 0) {
        free(precode.tree);
        free(precode.table);
        return (ARCHIVE_FATAL);
      }
      if (val < 16)
      {
        rar->lengthtable[i] = (rar->lengthtable[i] + val) & 0xF;
        i++;
      }
      else if (val < 18)
      {
        if (i == 0)
        {
          free(precode.tree);
          free(precode.table);
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Internal error extracting RAR file.");
          return (ARCHIVE_FATAL);
        }

        if(val == 16) {
          if (!rar_br_read_ahead(a, br, 3)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 3) + 3;
          rar_br_consume(br, 3);
        } else {
          if (!rar_br_read_ahead(a, br, 7)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 7) + 11;
          rar_br_consume(br, 7);
        }

        for (j = 0; j < n && i < HUFFMAN_TABLE_SIZE; j++)
        {
          rar->lengthtable[i] = rar->lengthtable[i-1];
          i++;
        }
      }
      else
      {
        if(val == 18) {
          if (!rar_br_read_ahead(a, br, 3)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 3) + 3;
          rar_br_consume(br, 3);
        } else {
          if (!rar_br_read_ahead(a, br, 7)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 7) + 11;
          rar_br_consume(br, 7);
        }

        for(j = 0; j < n && i < HUFFMAN_TABLE_SIZE; j++)
          rar->lengthtable[i++] = 0;
      }
    }
    free(precode.tree);
    free(precode.table);

    r = create_code(a, &rar->maincode, &rar->lengthtable[0], MAINCODE_SIZE,
                MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->offsetcode, &rar->lengthtable[MAINCODE_SIZE],
                OFFSETCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->lowoffsetcode,
                &rar->lengthtable[MAINCODE_SIZE + OFFSETCODE_SIZE],
                LOWOFFSETCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->lengthcode,
                &rar->lengthtable[MAINCODE_SIZE + OFFSETCODE_SIZE +
                LOWOFFSETCODE_SIZE], LENGTHCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
  }

  if (!rar->dictionary_size || !rar->lzss.window)
  {
    /* Seems as though dictionary sizes are not used. Even so, minimize
     * memory usage as much as possible.
     */
    void *new_window;
    unsigned int new_size;

    if (rar->unp_size >= DICTIONARY_MAX_SIZE)
      new_size = DICTIONARY_MAX_SIZE;
    else
      new_size = rar_fls((unsigned int)rar->unp_size) << 1;
    if (new_size == 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Zero window size is invalid.");
      return (ARCHIVE_FATAL);
    }
    new_window = realloc(rar->lzss.window, new_size);
    if (new_window == NULL) {
      archive_set_error(&a->archive, ENOMEM,
                        "Unable to allocate memory for uncompressed data.");
      return (ARCHIVE_FATAL);
    }
    rar->lzss.window = (unsigned char *)new_window;
    rar->dictionary_size = new_size;
    memset(rar->lzss.window, 0, rar->dictionary_size);
    rar->lzss.mask = rar->dictionary_size - 1;
  }

  rar->start_new_table = 0;
  return (ARCHIVE_OK);
truncated_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Truncated RAR file data");
  rar->valid = 0;
  return (ARCHIVE_FATAL);
}

static void
free_codes(struct archive_read *a)
{
  struct rar *rar = (struct rar *)(a->format->data);
  free(rar->maincode.tree);
  free(rar->offsetcode.tree);
  free(rar->lowoffsetcode.tree);
  free(rar->lengthcode.tree);
  free(rar->maincode.table);
  free(rar->offsetcode.table);
  free(rar->lowoffsetcode.table);
  free(rar->lengthcode.table);
  memset(&rar->maincode, 0, sizeof(rar->maincode));
  memset(&rar->offsetcode, 0, sizeof(rar->offsetcode));
  memset(&rar->lowoffsetcode, 0, sizeof(rar->lowoffsetcode));
  memset(&rar->lengthcode, 0, sizeof(rar->lengthcode));
}


static int
read_next_symbol(struct archive_read *a, struct huffman_code *code)
{
  unsigned char bit;
  unsigned int bits;
  int length, value, node;
  struct rar *rar;
  struct rar_br *br;

  if (!code->table)
  {
    if (make_table(a, code) != (ARCHIVE_OK))
      return -1;
  }

  rar = (struct rar *)(a->format->data);
  br = &(rar->br);

  /* Look ahead (peek) at bits */
  if (!rar_br_read_ahead(a, br, code->tablesize)) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    rar->valid = 0;
    return -1;
  }
  bits = rar_br_bits(br, code->tablesize);

  length = code->table[bits].length;
  value = code->table[bits].value;

  if (length < 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid prefix code in bitstream");
    return -1;
  }

  if (length <= code->tablesize)
  {
    /* Skip length bits */
    rar_br_consume(br, length);
    return value;
  }

  /* Skip tablesize bits */
  rar_br_consume(br, code->tablesize);

  node = value;
  while (!(code->tree[node].branches[0] ==
    code->tree[node].branches[1]))
  {
    if (!rar_br_read_ahead(a, br, 1)) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Truncated RAR file data");
      rar->valid = 0;
      return -1;
    }
    bit = rar_br_bits(br, 1);
    rar_br_consume(br, 1);

    if (code->tree[node].branches[bit] < 0)
    {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Invalid prefix code in bitstream");
      return -1;
    }
    node = code->tree[node].branches[bit];
  }

  return code->tree[node].branches[0];
}

static int
create_code(struct archive_read *a, struct huffman_code *code,
            unsigned char *lengths, int numsymbols, char maxlength)
{
  int i, j, codebits = 0, symbolsleft = numsymbols;

  code->numentries = 0;
  code->numallocatedentries = 0;
  if (new_node(code) < 0) {
    archive_set_error(&a->archive, ENOMEM,
                      "Unable to allocate memory for node data.");
    return (ARCHIVE_FATAL);
  }
  code->numentries = 1;
  code->minlength = INT_MAX;
  code->maxlength = INT_MIN;
  codebits = 0;
  for(i = 1; i <= maxlength; i++)
  {
    for(j = 0; j < numsymbols; j++)
    {
      if (lengths[j] != i) continue;
      if (add_value(a, code, j, codebits, i) != ARCHIVE_OK)
        return (ARCHIVE_FATAL);
      codebits++;
      if (--symbolsleft <= 0) { break; break; }
    }
    codebits <<= 1;
  }
  return (ARCHIVE_OK);
}

static int
add_value(struct archive_read *a, struct huffman_code *code, int value,
          int codebits, int length)
{
  int repeatpos, lastnode, bitpos, bit, repeatnode, nextnode;

  free(code->table);
  code->table = NULL;

  if(length > code->maxlength)
    code->maxlength = length;
  if(length < code->minlength)
    code->minlength = length;

  repeatpos = -1;
  if (repeatpos == 0 || (repeatpos >= 0
    && (((codebits >> (repeatpos - 1)) & 3) == 0
    || ((codebits >> (repeatpos - 1)) & 3) == 3)))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid repeat position");
    return (ARCHIVE_FATAL);
  }

  lastnode = 0;
  for (bitpos = length - 1; bitpos >= 0; bitpos--)
  {
    bit = (codebits >> bitpos) & 1;

    /* Leaf node check */
    if (code->tree[lastnode].branches[0] ==
      code->tree[lastnode].branches[1])
    {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Prefix found");
      return (ARCHIVE_FATAL);
    }

    if (bitpos == repeatpos)
    {
      /* Open branch check */
      if (!(code->tree[lastnode].branches[bit] < 0))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid repeating code");
        return (ARCHIVE_FATAL);
      }

      if ((repeatnode = new_node(code)) < 0) {
        archive_set_error(&a->archive, ENOMEM,
                          "Unable to allocate memory for node data.");
        return (ARCHIVE_FATAL);
      }
      if ((nextnode = new_node(code)) < 0) {
        archive_set_error(&a->archive, ENOMEM,
                          "Unable to allocate memory for node data.");
        return (ARCHIVE_FATAL);
      }

      /* Set branches */
      code->tree[lastnode].branches[bit] = repeatnode;
      code->tree[repeatnode].branches[bit] = repeatnode;
      code->tree[repeatnode].branches[bit^1] = nextnode;
      lastnode = nextnode;

      bitpos++; /* terminating bit already handled, skip it */
    }
    else
    {
      /* Open branch check */
      if (code->tree[lastnode].branches[bit] < 0)
      {
        if (new_node(code) < 0) {
          archive_set_error(&a->archive, ENOMEM,
                            "Unable to allocate memory for node data.");
          return (ARCHIVE_FATAL);
        }
        code->tree[lastnode].branches[bit] = code->numentries++;
      }

      /* set to branch */
      lastnode = code->tree[lastnode].branches[bit];
    }
  }

  if (!(code->tree[lastnode].branches[0] == -1
    && code->tree[lastnode].branches[1] == -2))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Prefix found");
    return (ARCHIVE_FATAL);
  }

  /* Set leaf value */
  code->tree[lastnode].branches[0] = value;
  code->tree[lastnode].branches[1] = value;

  return (ARCHIVE_OK);
}

static int
new_node(struct huffman_code *code)
{
  void *new_tree;
  if (code->numallocatedentries == code->numentries) {
    int new_num_entries = 256;
    if (code->numentries > 0) {
        new_num_entries = code->numentries * 2;
    }
    new_tree = realloc(code->tree, new_num_entries * sizeof(*code->tree));
    if (new_tree == NULL)
        return (-1);
    code->tree = (struct huffman_tree_node *)new_tree;
    code->numallocatedentries = new_num_entries;
  }
  code->tree[code->numentries].branches[0] = -1;
  code->tree[code->numentries].branches[1] = -2;
  return 1;
}

static int
make_table(struct archive_read *a, struct huffman_code *code)
{
  if (code->maxlength < code->minlength || code->maxlength > 10)
    code->tablesize = 10;
  else
    code->tablesize = code->maxlength;

  code->table =
    (struct huffman_table_entry *)calloc(1, sizeof(*code->table)
    * ((size_t)1 << code->tablesize));

  return make_table_recurse(a, code, 0, code->table, 0, code->tablesize);
}

static int
make_table_recurse(struct archive_read *a, struct huffman_code *code, int node,
                   struct huffman_table_entry *table, int depth,
                   int maxdepth)
{
  int currtablesize, i, ret = (ARCHIVE_OK);

  if (!code->tree)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Huffman tree was not created.");
    return (ARCHIVE_FATAL);
  }
  if (node < 0 || node >= code->numentries)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid location to Huffman tree specified.");
    return (ARCHIVE_FATAL);
  }

  currtablesize = 1 << (maxdepth - depth);

  if (code->tree[node].branches[0] ==
    code->tree[node].branches[1])
  {
    for(i = 0; i < currtablesize; i++)
    {
      table[i].length = depth;
      table[i].value = code->tree[node].branches[0];
    }
  }
  else if (node < 0)
  {
    for(i = 0; i < currtablesize; i++)
      table[i].length = -1;
  }
  else
  {
    if(depth == maxdepth)
    {
      table[0].length = maxdepth + 1;
      table[0].value = node;
    }
    else
    {
      ret |= make_table_recurse(a, code, code->tree[node].branches[0], table,
                                depth + 1, maxdepth);
      ret |= make_table_recurse(a, code, code->tree[node].branches[1],
                         table + currtablesize / 2, depth + 1, maxdepth);
    }
  }
  return ret;
}

static int64_t
expand(struct archive_read *a, int64_t end)
{
  static const unsigned char lengthbases[] =
    {   0,   1,   2,   3,   4,   5,   6,
        7,   8,  10,  12,  14,  16,  20,
       24,  28,  32,  40,  48,  56,  64,
       80,  96, 112, 128, 160, 192, 224 };
  static const unsigned char lengthbits[] =
    { 0, 0, 0, 0, 0, 0, 0,
      0, 1, 1, 1, 1, 2, 2,
      2, 2, 3, 3, 3, 3, 4,
      4, 4, 4, 5, 5, 5, 5 };
  static const unsigned int offsetbases[] =
    {       0,       1,       2,       3,       4,       6,
            8,      12,      16,      24,      32,      48,
           64,      96,     128,     192,     256,     384,
          512,     768,    1024,    1536,    2048,    3072,
         4096,    6144,    8192,   12288,   16384,   24576,
        32768,   49152,   65536,   98304,  131072,  196608,
       262144,  327680,  393216,  458752,  524288,  589824,
       655360,  720896,  786432,  851968,  917504,  983040,
      1048576, 1310720, 1572864, 1835008, 2097152, 2359296,
      2621440, 2883584, 3145728, 3407872, 3670016, 3932160 };
  static const unsigned char offsetbits[] =
    {  0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
       5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10,
      11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
      16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
      18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18 };
  static const unsigned char shortbases[] =
    { 0, 4, 8, 16, 32, 64, 128, 192 };
  static const unsigned char shortbits[] =
    { 2, 2, 3, 4, 5, 6, 6, 6 };

  int symbol, offs, len, offsindex, lensymbol, i, offssymbol, lowoffsetsymbol;
  unsigned char newfile;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);

  if (rar->filterstart < end)
    end = rar->filterstart;

  while (1)
  {
    if (rar->output_last_match &&
      lzss_position(&rar->lzss) + rar->lastlength <= end)
    {
      lzss_emit_match(rar, rar->lastoffset, rar->lastlength);
      rar->output_last_match = 0;
    }

    if(rar->is_ppmd_block || rar->output_last_match ||
      lzss_position(&rar->lzss) >= end)
      return lzss_position(&rar->lzss);

    if ((symbol = read_next_symbol(a, &rar->maincode)) < 0)
      return (ARCHIVE_FATAL);
    rar->output_last_match = 0;

    if (symbol < 256)
    {
      lzss_emit_literal(rar, symbol);
      continue;
    }
    else if (symbol == 256)
    {
      if (!rar_br_read_ahead(a, br, 1))
        goto truncated_data;
      newfile = !rar_br_bits(br, 1);
      rar_br_consume(br, 1);

      if(newfile)
      {
        rar->start_new_block = 1;
        if (!rar_br_read_ahead(a, br, 1))
          goto truncated_data;
        rar->start_new_table = rar_br_bits(br, 1);
        rar_br_consume(br, 1);
        return lzss_position(&rar->lzss);
      }
      else
      {
        if (parse_codes(a) != ARCHIVE_OK)
          return (ARCHIVE_FATAL);
        continue;
      }
    }
    else if(symbol==257)
    {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
                        "Parsing filters is unsupported.");
      return (ARCHIVE_FAILED);
    }
    else if(symbol==258)
    {
      if(rar->lastlength == 0)
        continue;

      offs = rar->lastoffset;
      len = rar->lastlength;
    }
    else if (symbol <= 262)
    {
      offsindex = symbol - 259;
      offs = rar->oldoffset[offsindex];

      if ((lensymbol = read_next_symbol(a, &rar->lengthcode)) < 0)
        goto bad_data;
      if (lensymbol > (int)(sizeof(lengthbases)/sizeof(lengthbases[0])))
        goto bad_data;
      if (lensymbol > (int)(sizeof(lengthbits)/sizeof(lengthbits[0])))
        goto bad_data;
      len = lengthbases[lensymbol] + 2;
      if (lengthbits[lensymbol] > 0) {
        if (!rar_br_read_ahead(a, br, lengthbits[lensymbol]))
          goto truncated_data;
        len += rar_br_bits(br, lengthbits[lensymbol]);
        rar_br_consume(br, lengthbits[lensymbol]);
      }

      for (i = offsindex; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }
    else if(symbol<=270)
    {
      offs = shortbases[symbol-263] + 1;
      if(shortbits[symbol-263] > 0) {
        if (!rar_br_read_ahead(a, br, shortbits[symbol-263]))
          goto truncated_data;
        offs += rar_br_bits(br, shortbits[symbol-263]);
        rar_br_consume(br, shortbits[symbol-263]);
      }

      len = 2;

      for(i = 3; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }
    else
    {
      if (symbol-271 > (int)(sizeof(lengthbases)/sizeof(lengthbases[0])))
        goto bad_data;
      if (symbol-271 > (int)(sizeof(lengthbits)/sizeof(lengthbits[0])))
        goto bad_data;
      len = lengthbases[symbol-271]+3;
      if(lengthbits[symbol-271] > 0) {
        if (!rar_br_read_ahead(a, br, lengthbits[symbol-271]))
          goto truncated_data;
        len += rar_br_bits(br, lengthbits[symbol-271]);
        rar_br_consume(br, lengthbits[symbol-271]);
      }

      if ((offssymbol = read_next_symbol(a, &rar->offsetcode)) < 0)
        goto bad_data;
      if (offssymbol > (int)(sizeof(offsetbases)/sizeof(offsetbases[0])))
        goto bad_data;
      if (offssymbol > (int)(sizeof(offsetbits)/sizeof(offsetbits[0])))
        goto bad_data;
      offs = offsetbases[offssymbol]+1;
      if(offsetbits[offssymbol] > 0)
      {
        if(offssymbol > 9)
        {
          if(offsetbits[offssymbol] > 4) {
            if (!rar_br_read_ahead(a, br, offsetbits[offssymbol] - 4))
              goto truncated_data;
            offs += rar_br_bits(br, offsetbits[offssymbol] - 4) << 4;
            rar_br_consume(br, offsetbits[offssymbol] - 4);
	  }

          if(rar->numlowoffsetrepeats > 0)
          {
            rar->numlowoffsetrepeats--;
            offs += rar->lastlowoffset;
          }
          else
          {
            if ((lowoffsetsymbol =
              read_next_symbol(a, &rar->lowoffsetcode)) < 0)
              return (ARCHIVE_FATAL);
            if(lowoffsetsymbol == 16)
            {
              rar->numlowoffsetrepeats = 15;
              offs += rar->lastlowoffset;
            }
            else
            {
              offs += lowoffsetsymbol;
              rar->lastlowoffset = lowoffsetsymbol;
            }
          }
        }
        else {
          if (!rar_br_read_ahead(a, br, offsetbits[offssymbol]))
            goto truncated_data;
          offs += rar_br_bits(br, offsetbits[offssymbol]);
          rar_br_consume(br, offsetbits[offssymbol]);
        }
      }

      if (offs >= 0x40000)
        len++;
      if (offs >= 0x2000)
        len++;

      for(i = 3; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }

    rar->lastoffset = offs;
    rar->lastlength = len;
    rar->output_last_match = 1;
  }
truncated_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Truncated RAR file data");
  rar->valid = 0;
  return (ARCHIVE_FATAL);
bad_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Bad RAR file data");
  return (ARCHIVE_FATAL);
}

static int
copy_from_lzss_window(struct archive_read *a, const void **buffer,
                        int64_t startpos, int length)
{
  int windowoffs, firstpart;
  struct rar *rar = (struct rar *)(a->format->data);

  if (!rar->unp_buffer)
  {
    if ((rar->unp_buffer = malloc(rar->unp_buffer_size)) == NULL)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Unable to allocate memory for uncompressed data.");
      return (ARCHIVE_FATAL);
    }
  }

  windowoffs = lzss_offset_for_position(&rar->lzss, startpos);
  if(windowoffs + length <= lzss_size(&rar->lzss)) {
    memcpy(&rar->unp_buffer[rar->unp_offset], &rar->lzss.window[windowoffs],
           length);
  } else if (length <= lzss_size(&rar->lzss)) {
    firstpart = lzss_size(&rar->lzss) - windowoffs;
    if (firstpart < 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Bad RAR file data");
      return (ARCHIVE_FATAL);
    }
    if (firstpart < length) {
      memcpy(&rar->unp_buffer[rar->unp_offset],
             &rar->lzss.window[windowoffs], firstpart);
      memcpy(&rar->unp_buffer[rar->unp_offset + firstpart],
             &rar->lzss.window[0], length - firstpart);
    } else {
      memcpy(&rar->unp_buffer[rar->unp_offset],
             &rar->lzss.window[windowoffs], length);
    }
  } else {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Bad RAR file data");
      return (ARCHIVE_FATAL);
  }
  rar->unp_offset += length;
  if (rar->unp_offset >= rar->unp_buffer_size)
    *buffer = rar->unp_buffer;
  else
    *buffer = NULL;
  return (ARCHIVE_OK);
}

static const void *
rar_read_ahead(struct archive_read *a, size_t min, ssize_t *avail)
{
  struct rar *rar = (struct rar *)(a->format->data);
  const void *h = __archive_read_ahead(a, min, avail);
  int ret;
  if (avail)
  {
    if (a->archive.read_data_is_posix_read && *avail > (ssize_t)a->archive.read_data_requested)
      *avail = a->archive.read_data_requested;
    if (*avail > rar->bytes_remaining)
      *avail = (ssize_t)rar->bytes_remaining;
    if (*avail < 0)
      return NULL;
    else if (*avail == 0 && rar->main_flags & MHD_VOLUME &&
      rar->file_flags & FHD_SPLIT_AFTER)
    {
      rar->filename_must_match = 1;
      ret = archive_read_format_rar_read_header(a, a->entry);
      if (ret == (ARCHIVE_EOF))
      {
        rar->has_endarc_header = 1;
        ret = archive_read_format_rar_read_header(a, a->entry);
      }
      rar->filename_must_match = 0;
      if (ret != (ARCHIVE_OK))
        return NULL;
      return rar_read_ahead(a, min, avail);
    }
  }
  return h;
}
