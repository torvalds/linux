/*-
* Copyright (c) 2018 Grzegorz Antoniak (http://antoniak.org)
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
#include "archive_endian.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <time.h>
#ifdef HAVE_ZLIB_H
#include <zlib.h> /* crc32 */
#endif

#include "archive.h"
#ifndef HAVE_ZLIB_H
#include "archive_crc32.h"
#endif

#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_ppmd7_private.h"
#include "archive_entry_private.h"

#ifdef HAVE_BLAKE2_H
#include <blake2.h>
#else
#include "archive_blake2.h"
#endif

/*#define CHECK_CRC_ON_SOLID_SKIP*/
/*#define DONT_FAIL_ON_CRC_ERROR*/
/*#define DEBUG*/

#define rar5_min(a, b) (((a) > (b)) ? (b) : (a))
#define rar5_max(a, b) (((a) > (b)) ? (a) : (b))
#define rar5_countof(X) ((const ssize_t) (sizeof(X) / sizeof(*X)))

#if defined DEBUG
#define DEBUG_CODE if(1)
#else
#define DEBUG_CODE if(0)
#endif

/* Real RAR5 magic number is:
 *
 * 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00
 * "Rar!→•☺·\x00"
 *
 * It's stored in `rar5_signature` after XOR'ing it with 0xA1, because I don't
 * want to put this magic sequence in each binary that uses libarchive, so
 * applications that scan through the file for this marker won't trigger on
 * this "false" one.
 *
 * The array itself is decrypted in `rar5_init` function. */

static unsigned char rar5_signature[] = { 243, 192, 211, 128, 187, 166, 160, 161 };
static const ssize_t rar5_signature_size = sizeof(rar5_signature);
/* static const size_t g_unpack_buf_chunk_size = 1024; */
static const size_t g_unpack_window_size = 0x20000;

struct file_header {
    ssize_t bytes_remaining;
    ssize_t unpacked_size;
    int64_t last_offset;         /* Used in sanity checks. */
    int64_t last_size;           /* Used in sanity checks. */

    uint8_t solid : 1;           /* Is this a solid stream? */
    uint8_t service : 1;         /* Is this file a service data? */
    uint8_t eof : 1;             /* Did we finish unpacking the file? */

    /* Optional time fields. */
    uint64_t e_mtime;
    uint64_t e_ctime;
    uint64_t e_atime;
    uint32_t e_unix_ns;

    /* Optional hash fields. */
    uint32_t stored_crc32;
    uint32_t calculated_crc32;
    uint8_t blake2sp[32];
    blake2sp_state b2state;
    char has_blake2;
};

enum FILTER_TYPE {
    FILTER_DELTA = 0,   /* Generic pattern. */
    FILTER_E8    = 1,   /* Intel x86 code. */
    FILTER_E8E9  = 2,   /* Intel x86 code. */
    FILTER_ARM   = 3,   /* ARM code. */
    FILTER_AUDIO = 4,   /* Audio filter, not used in RARv5. */
    FILTER_RGB   = 5,   /* Color palette, not used in RARv5. */
    FILTER_ITANIUM = 6, /* Intel's Itanium, not used in RARv5. */
    FILTER_PPM   = 7,   /* Predictive pattern matching, not used in RARv5. */
    FILTER_NONE  = 8,
};

struct filter_info {
    int type;
    int channels;
    int pos_r;

    int64_t block_start;
    ssize_t block_length;
    uint16_t width;
};

struct data_ready {
    char used;
    const uint8_t* buf;
    size_t size;
    int64_t offset;
};

struct cdeque {
    uint16_t beg_pos;
    uint16_t end_pos;
    uint16_t cap_mask;
    uint16_t size;
    size_t* arr;
};

struct decode_table {
    uint32_t size;
    int32_t decode_len[16];
    uint32_t decode_pos[16];
    uint32_t quick_bits;
    uint8_t quick_len[1 << 10];
    uint16_t quick_num[1 << 10];
    uint16_t decode_num[306];
};

struct comp_state {
    /* Flag used to specify if unpacker needs to reinitialize the uncompression
     * context. */
    uint8_t initialized : 1;

    /* Flag used when applying filters. */
    uint8_t all_filters_applied : 1;

    /* Flag used to skip file context reinitialization, used when unpacker is
     * skipping through different multivolume archives. */
    uint8_t switch_multivolume : 1;

    /* Flag used to specify if unpacker has processed the whole data block or
     * just a part of it. */
    uint8_t block_parsing_finished : 1;

    int notused : 4;

    int flags;                   /* Uncompression flags. */
    int method;                  /* Uncompression algorithm method. */
    int version;                 /* Uncompression algorithm version. */
    ssize_t window_size;         /* Size of window_buf. */
    uint8_t* window_buf;         /* Circular buffer used during
                                    decompression. */
    uint8_t* filtered_buf;       /* Buffer used when applying filters. */
    const uint8_t* block_buf;    /* Buffer used when merging blocks. */
    size_t window_mask;          /* Convenience field; window_size - 1. */
    int64_t write_ptr;           /* This amount of data has been unpacked in
                                    the window buffer. */
    int64_t last_write_ptr;      /* This amount of data has been stored in
                                    the output file. */
    int64_t last_unstore_ptr;    /* Counter of bytes extracted during
                                    unstoring. This is separate from
                                    last_write_ptr because of how SERVICE
                                    base blocks are handled during skipping
                                    in solid multiarchive archives. */
    int64_t solid_offset;        /* Additional offset inside the window
                                    buffer, used in unpacking solid
                                    archives. */
    ssize_t cur_block_size;      /* Size of current data block. */
    int last_len;                /* Flag used in lzss decompression. */

    /* Decode tables used during lzss uncompression. */

#define HUFF_BC 20
    struct decode_table bd;      /* huffman bit lengths */
#define HUFF_NC 306
    struct decode_table ld;      /* literals */
#define HUFF_DC 64
    struct decode_table dd;      /* distances */
#define HUFF_LDC 16
    struct decode_table ldd;     /* lower bits of distances */
#define HUFF_RC 44
    struct decode_table rd;      /* repeating distances */
#define HUFF_TABLE_SIZE (HUFF_NC + HUFF_DC + HUFF_RC + HUFF_LDC)

    /* Circular deque for storing filters. */
    struct cdeque filters;
    int64_t last_block_start;    /* Used for sanity checking. */
    ssize_t last_block_length;   /* Used for sanity checking. */

    /* Distance cache used during lzss uncompression. */
    int dist_cache[4];

    /* Data buffer stack. */
    struct data_ready dready[2];
};

/* Bit reader state. */
struct bit_reader {
    int8_t bit_addr;    /* Current bit pointer inside current byte. */
    int in_addr;        /* Current byte pointer. */
};

/* RARv5 block header structure. Use bf_* functions to get values from
 * block_flags_u8 field. I.e. bf_byte_count, etc. */
struct compressed_block_header {
    /* block_flags_u8 contain fields encoded in little-endian bitfield:
     *
     * - table present flag (shr 7, and 1),
     * - last block flag    (shr 6, and 1),
     * - byte_count         (shr 3, and 7),
     * - bit_size           (shr 0, and 7).
     */
    uint8_t block_flags_u8;
    uint8_t block_cksum;
};

/* RARv5 main header structure. */
struct main_header {
    /* Does the archive contain solid streams? */
    uint8_t solid : 1;

    /* If this a multi-file archive? */
    uint8_t volume : 1;
    uint8_t endarc : 1;
    uint8_t notused : 5;

    int vol_no;
};

struct generic_header {
    uint8_t split_after : 1;
    uint8_t split_before : 1;
    uint8_t padding : 6;
    int size;
    int last_header_id;
};

struct multivolume {
    int expected_vol_no;
    uint8_t* push_buf;
};

/* Main context structure. */
struct rar5 {
    int header_initialized;

    /* Set to 1 if current file is positioned AFTER the magic value
     * of the archive file. This is used in header reading functions. */
    int skipped_magic;

    /* Set to not zero if we're in skip mode (either by calling rar5_data_skip
     * function or when skipping over solid streams). Set to 0 when in
     * extraction mode. This is used during checksum calculation functions. */
    int skip_mode;

    /* An offset to QuickOpen list. This is not supported by this unpacker,
     * because we're focusing on streaming interface. QuickOpen is designed
     * to make things quicker for non-stream interfaces, so it's not our
     * use case. */
    uint64_t qlist_offset;

    /* An offset to additional Recovery data. This is not supported by this
     * unpacker. Recovery data are additional Reed-Solomon codes that could
     * be used to calculate bytes that are missing in archive or are
     * corrupted. */
    uint64_t rr_offset;

    /* Various context variables grouped to different structures. */
    struct generic_header generic;
    struct main_header main;
    struct comp_state cstate;
    struct file_header file;
    struct bit_reader bits;
    struct multivolume vol;

    /* The header of currently processed RARv5 block. Used in main
     * decompression logic loop. */
    struct compressed_block_header last_block_hdr;
};

/* Forward function declarations. */

static int verify_global_checksums(struct archive_read* a);
static int rar5_read_data_skip(struct archive_read *a);
static int push_data_ready(struct archive_read* a, struct rar5* rar,
        const uint8_t* buf, size_t size, int64_t offset);

/* CDE_xxx = Circular Double Ended (Queue) return values. */
enum CDE_RETURN_VALUES {
    CDE_OK, CDE_ALLOC, CDE_PARAM, CDE_OUT_OF_BOUNDS,
};

/* Clears the contents of this circular deque. */
static void cdeque_clear(struct cdeque* d) {
    d->size = 0;
    d->beg_pos = 0;
    d->end_pos = 0;
}

/* Creates a new circular deque object. Capacity must be power of 2: 8, 16, 32,
 * 64, 256, etc. When the user will add another item above current capacity,
 * the circular deque will overwrite the oldest entry. */
static int cdeque_init(struct cdeque* d, int max_capacity_power_of_2) {
    if(d == NULL || max_capacity_power_of_2 == 0)
        return CDE_PARAM;

    d->cap_mask = max_capacity_power_of_2 - 1;
    d->arr = NULL;

    if((max_capacity_power_of_2 & d->cap_mask) > 0)
        return CDE_PARAM;

    cdeque_clear(d);
    d->arr = malloc(sizeof(void*) * max_capacity_power_of_2);

    return d->arr ? CDE_OK : CDE_ALLOC;
}

/* Return the current size (not capacity) of circular deque `d`. */
static size_t cdeque_size(struct cdeque* d) {
    return d->size;
}

/* Returns the first element of current circular deque. Note that this function
 * doesn't perform any bounds checking. If you need bounds checking, use
 * `cdeque_front()` function instead. */
static void cdeque_front_fast(struct cdeque* d, void** value) {
    *value = (void*) d->arr[d->beg_pos];
}

/* Returns the first element of current circular deque. This function
 * performs bounds checking. */
static int cdeque_front(struct cdeque* d, void** value) {
    if(d->size > 0) {
        cdeque_front_fast(d, value);
        return CDE_OK;
    } else
        return CDE_OUT_OF_BOUNDS;
}

/* Pushes a new element into the end of this circular deque object. If current
 * size will exceed capacity, the oldest element will be overwritten. */
static int cdeque_push_back(struct cdeque* d, void* item) {
    if(d == NULL)
        return CDE_PARAM;

    if(d->size == d->cap_mask + 1)
        return CDE_OUT_OF_BOUNDS;

    d->arr[d->end_pos] = (size_t) item;
    d->end_pos = (d->end_pos + 1) & d->cap_mask;
    d->size++;

    return CDE_OK;
}

/* Pops a front element of this circular deque object and returns its value.
 * This function doesn't perform any bounds checking. */
static void cdeque_pop_front_fast(struct cdeque* d, void** value) {
    *value = (void*) d->arr[d->beg_pos];
    d->beg_pos = (d->beg_pos + 1) & d->cap_mask;
    d->size--;
}

/* Pops a front element of this circular deque object and returns its value.
 * This function performs bounds checking. */
static int cdeque_pop_front(struct cdeque* d, void** value) {
    if(!d || !value)
        return CDE_PARAM;

    if(d->size == 0)
        return CDE_OUT_OF_BOUNDS;

    cdeque_pop_front_fast(d, value);
    return CDE_OK;
}

/* Convenience function to cast filter_info** to void **. */
static void** cdeque_filter_p(struct filter_info** f) {
    return (void**) (size_t) f;
}

/* Convenience function to cast filter_info* to void *. */
static void* cdeque_filter(struct filter_info* f) {
    return (void**) (size_t) f;
}

/* Destroys this circular deque object. Deallocates the memory of the collection
 * buffer, but doesn't deallocate the memory of any pointer passed to this
 * deque as a value. */
static void cdeque_free(struct cdeque* d) {
    if(!d)
        return;

    if(!d->arr)
        return;

    free(d->arr);

    d->arr = NULL;
    d->beg_pos = -1;
    d->end_pos = -1;
    d->cap_mask = 0;
}

static inline
uint8_t bf_bit_size(const struct compressed_block_header* hdr) {
    return hdr->block_flags_u8 & 7;
}

static inline
uint8_t bf_byte_count(const struct compressed_block_header* hdr) {
    return (hdr->block_flags_u8 >> 3) & 7;
}

static inline
uint8_t bf_is_table_present(const struct compressed_block_header* hdr) {
    return (hdr->block_flags_u8 >> 7) & 1;
}

static inline struct rar5* get_context(struct archive_read* a) {
    return (struct rar5*) a->format->data;
}

/* Convenience functions used by filter implementations. */

static uint32_t read_filter_data(struct rar5* rar, uint32_t offset) {
    return archive_le32dec(&rar->cstate.window_buf[offset]);
}

static void write_filter_data(struct rar5* rar, uint32_t offset,
        uint32_t value)
{
    archive_le32enc(&rar->cstate.filtered_buf[offset], value);
}

static void circular_memcpy(uint8_t* dst, uint8_t* window, const int mask,
        int64_t start, int64_t end)
{
    if((start & mask) > (end & mask)) {
        ssize_t len1 = mask + 1 - (start & mask);
        ssize_t len2 = end & mask;

        memcpy(dst, &window[start & mask], len1);
        memcpy(dst + len1, window, len2);
    } else {
        memcpy(dst, &window[start & mask], (size_t) (end - start));
    }
}

/* Allocates a new filter descriptor and adds it to the filter array. */
static struct filter_info* add_new_filter(struct rar5* rar) {
    struct filter_info* f =
        (struct filter_info*) calloc(1, sizeof(struct filter_info));

    if(!f) {
        return NULL;
    }

    cdeque_push_back(&rar->cstate.filters, cdeque_filter(f));
    return f;
}

static int run_delta_filter(struct rar5* rar, struct filter_info* flt) {
    int i;
    ssize_t dest_pos, src_pos = 0;

    for(i = 0; i < flt->channels; i++) {
        uint8_t prev_byte = 0;
        for(dest_pos = i;
                dest_pos < flt->block_length;
                dest_pos += flt->channels)
        {
            uint8_t byte;

            byte = rar->cstate.window_buf[(rar->cstate.solid_offset +
                    flt->block_start + src_pos) & rar->cstate.window_mask];

            prev_byte -= byte;
            rar->cstate.filtered_buf[dest_pos] = prev_byte;
            src_pos++;
        }
    }

    return ARCHIVE_OK;
}

static int run_e8e9_filter(struct rar5* rar, struct filter_info* flt,
        int extended)
{
    const uint32_t file_size = 0x1000000;
    ssize_t i;

    const int mask = (int)rar->cstate.window_mask;
    circular_memcpy(rar->cstate.filtered_buf,
        rar->cstate.window_buf,
        mask,
        rar->cstate.solid_offset + flt->block_start,
        rar->cstate.solid_offset + flt->block_start + flt->block_length);

    for(i = 0; i < flt->block_length - 4;) {
        uint8_t b = rar->cstate.window_buf[(rar->cstate.solid_offset +
                flt->block_start + i++) & mask];

        /* 0xE8 = x86's call <relative_addr_uint32> (function call)
         * 0xE9 = x86's jmp <relative_addr_uint32> (unconditional jump) */
        if(b == 0xE8 || (extended && b == 0xE9)) {

            uint32_t addr;
            uint32_t offset = (i + flt->block_start) % file_size;

            addr = read_filter_data(rar, (uint32_t)(rar->cstate.solid_offset +
                        flt->block_start + i) & rar->cstate.window_mask);

            if(addr & 0x80000000) {
                if(((addr + offset) & 0x80000000) == 0) {
                    write_filter_data(rar, (uint32_t)i, addr + file_size);
                }
            } else {
                if((addr - file_size) & 0x80000000) {
                    uint32_t naddr = addr - offset;
                    write_filter_data(rar, (uint32_t)i, naddr);
                }
            }

            i += 4;
        }
    }

    return ARCHIVE_OK;
}

static int run_arm_filter(struct rar5* rar, struct filter_info* flt) {
    ssize_t i = 0;
    uint32_t offset;
    const int mask = (int)rar->cstate.window_mask;

    circular_memcpy(rar->cstate.filtered_buf,
        rar->cstate.window_buf,
        mask,
        rar->cstate.solid_offset + flt->block_start,
        rar->cstate.solid_offset + flt->block_start + flt->block_length);

    for(i = 0; i < flt->block_length - 3; i += 4) {
        uint8_t* b = &rar->cstate.window_buf[(rar->cstate.solid_offset +
                flt->block_start + i) & mask];

        if(b[3] == 0xEB) {
            /* 0xEB = ARM's BL (branch + link) instruction. */
            offset = read_filter_data(rar, (rar->cstate.solid_offset +
                        flt->block_start + i) & mask) & 0x00ffffff;

            offset -= (uint32_t) ((i + flt->block_start) / 4);
            offset = (offset & 0x00ffffff) | 0xeb000000;
            write_filter_data(rar, (uint32_t)i, offset);
        }
    }

    return ARCHIVE_OK;
}

static int run_filter(struct archive_read* a, struct filter_info* flt) {
    int ret;
    struct rar5* rar = get_context(a);

    free(rar->cstate.filtered_buf);

    rar->cstate.filtered_buf = malloc(flt->block_length);
    if(!rar->cstate.filtered_buf) {
        archive_set_error(&a->archive, ENOMEM, "Can't allocate memory for "
                "filter data.");
        return ARCHIVE_FATAL;
    }

    switch(flt->type) {
        case FILTER_DELTA:
            ret = run_delta_filter(rar, flt);
            break;

        case FILTER_E8:
            /* fallthrough */
        case FILTER_E8E9:
            ret = run_e8e9_filter(rar, flt, flt->type == FILTER_E8E9);
            break;

        case FILTER_ARM:
            ret = run_arm_filter(rar, flt);
            break;

        default:
            archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Unsupported filter type: 0x%02x", flt->type);
            return ARCHIVE_FATAL;
    }

    if(ret != ARCHIVE_OK) {
        /* Filter has failed. */
        return ret;
    }

    if(ARCHIVE_OK != push_data_ready(a, rar, rar->cstate.filtered_buf,
                flt->block_length, rar->cstate.last_write_ptr))
    {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                "Stack overflow when submitting unpacked data");

        return ARCHIVE_FATAL;
    }

    rar->cstate.last_write_ptr += flt->block_length;
    return ARCHIVE_OK;
}

/* The `push_data` function submits the selected data range to the user.
 * Next call of `use_data` will use the pointer, size and offset arguments
 * that are specified here. These arguments are pushed to the FIFO stack here,
 * and popped from the stack by the `use_data` function. */
static void push_data(struct archive_read* a, struct rar5* rar,
        const uint8_t* buf, int64_t idx_begin, int64_t idx_end)
{
    const int wmask = (int)rar->cstate.window_mask;
    const ssize_t solid_write_ptr = (rar->cstate.solid_offset +
        rar->cstate.last_write_ptr) & wmask;

    idx_begin += rar->cstate.solid_offset;
    idx_end += rar->cstate.solid_offset;

    /* Check if our unpacked data is wrapped inside the window circular buffer.
     * If it's not wrapped, it can be copied out by using a single memcpy,
     * but when it's wrapped, we need to copy the first part with one
     * memcpy, and the second part with another memcpy. */

    if((idx_begin & wmask) > (idx_end & wmask)) {
        /* The data is wrapped (begin offset sis bigger than end offset). */
        const ssize_t frag1_size = rar->cstate.window_size - (idx_begin & wmask);
        const ssize_t frag2_size = idx_end & wmask;

        /* Copy the first part of the buffer first. */
        push_data_ready(a, rar, buf + solid_write_ptr, frag1_size,
            rar->cstate.last_write_ptr);

        /* Copy the second part of the buffer. */
        push_data_ready(a, rar, buf, frag2_size,
            rar->cstate.last_write_ptr + frag1_size);

        rar->cstate.last_write_ptr += frag1_size + frag2_size;
    } else {
        /* Data is not wrapped, so we can just use one call to copy the
         * data. */
        push_data_ready(a, rar,
            buf + solid_write_ptr,
            (idx_end - idx_begin) & wmask,
            rar->cstate.last_write_ptr);

        rar->cstate.last_write_ptr += idx_end - idx_begin;
    }
}

/* Convenience function that submits the data to the user. It uses the
 * unpack window buffer as a source location. */
static void push_window_data(struct archive_read* a, struct rar5* rar,
        int64_t idx_begin, int64_t idx_end)
{
    push_data(a, rar, rar->cstate.window_buf, idx_begin, idx_end);
}

static int apply_filters(struct archive_read* a) {
    struct filter_info* flt;
    struct rar5* rar = get_context(a);
    int ret;

    rar->cstate.all_filters_applied = 0;

    /* Get the first filter that can be applied to our data. The data needs to
     * be fully unpacked before the filter can be run. */
    if(CDE_OK ==
            cdeque_front(&rar->cstate.filters, cdeque_filter_p(&flt)))
    {
        /* Check if our unpacked data fully covers this filter's range. */
        if(rar->cstate.write_ptr > flt->block_start &&
                rar->cstate.write_ptr >= flt->block_start + flt->block_length)
        {
            /* Check if we have some data pending to be written right before
             * the filter's start offset. */
            if(rar->cstate.last_write_ptr == flt->block_start) {
                /* Run the filter specified by descriptor `flt`. */
                ret = run_filter(a, flt);
                if(ret != ARCHIVE_OK) {
                    /* Filter failure, return error. */
                    return ret;
                }

                /* Filter descriptor won't be needed anymore after it's used,
                 * so remove it from the filter list and free its memory. */
                (void) cdeque_pop_front(&rar->cstate.filters,
                        cdeque_filter_p(&flt));

                free(flt);
            } else {
                /* We can't run filters yet, dump the memory right before the
                 * filter. */
                push_window_data(a, rar, rar->cstate.last_write_ptr,
                        flt->block_start);
            }

            /* Return 'filter applied or not needed' state to the caller. */
            return ARCHIVE_RETRY;
        }
    }

    rar->cstate.all_filters_applied = 1;
    return ARCHIVE_OK;
}

static void dist_cache_push(struct rar5* rar, int value) {
    int* q = rar->cstate.dist_cache;

    q[3] = q[2];
    q[2] = q[1];
    q[1] = q[0];
    q[0] = value;
}

static int dist_cache_touch(struct rar5* rar, int idx) {
    int* q = rar->cstate.dist_cache;
    int i, dist = q[idx];

    for(i = idx; i > 0; i--)
        q[i] = q[i - 1];

    q[0] = dist;
    return dist;
}

static void free_filters(struct rar5* rar) {
    struct cdeque* d = &rar->cstate.filters;

    /* Free any remaining filters. All filters should be naturally consumed by
     * the unpacking function, so remaining filters after unpacking normally
     * mean that unpacking wasn't successful. But still of course we shouldn't
     * leak memory in such case. */

    /* cdeque_size() is a fast operation, so we can use it as a loop
     * expression. */
    while(cdeque_size(d) > 0) {
        struct filter_info* f = NULL;

        /* Pop_front will also decrease the collection's size. */
        if (CDE_OK == cdeque_pop_front(d, cdeque_filter_p(&f)))
            free(f);
    }

    cdeque_clear(d);

    /* Also clear out the variables needed for sanity checking. */
    rar->cstate.last_block_start = 0;
    rar->cstate.last_block_length = 0;
}

static void reset_file_context(struct rar5* rar) {
    memset(&rar->file, 0, sizeof(rar->file));
    blake2sp_init(&rar->file.b2state, 32);

    if(rar->main.solid) {
        rar->cstate.solid_offset += rar->cstate.write_ptr;
    } else {
        rar->cstate.solid_offset = 0;
    }

    rar->cstate.write_ptr = 0;
    rar->cstate.last_write_ptr = 0;
    rar->cstate.last_unstore_ptr = 0;

    free_filters(rar);
}

static inline int get_archive_read(struct archive* a,
        struct archive_read** ar)
{
    *ar = (struct archive_read*) a;
    archive_check_magic(a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
                        "archive_read_support_format_rar5");

    return ARCHIVE_OK;
}

static int read_ahead(struct archive_read* a, size_t how_many,
        const uint8_t** ptr)
{
    if(!ptr)
        return 0;

    ssize_t avail = -1;
    *ptr = __archive_read_ahead(a, how_many, &avail);

    if(*ptr == NULL) {
        return 0;
    }

    return 1;
}

static int consume(struct archive_read* a, int64_t how_many) {
    int ret;

    ret =
        how_many == __archive_read_consume(a, how_many)
        ? ARCHIVE_OK
        : ARCHIVE_FATAL;

    return ret;
}

/**
 * Read a RAR5 variable sized numeric value. This value will be stored in
 * `pvalue`. The `pvalue_len` argument points to a variable that will receive
 * the byte count that was consumed in order to decode the `pvalue` value, plus
 * one.
 *
 * pvalue_len is optional and can be NULL.
 *
 * NOTE: if `pvalue_len` is NOT NULL, the caller needs to manually consume
 * the number of bytes that `pvalue_len` value contains. If the `pvalue_len`
 * is NULL, this consuming operation is done automatically.
 *
 * Returns 1 if *pvalue was successfully read.
 * Returns 0 if there was an error. In this case, *pvalue contains an
 *           invalid value.
 */

static int read_var(struct archive_read* a, uint64_t* pvalue,
        uint64_t* pvalue_len)
{
    uint64_t result = 0;
    size_t shift, i;
    const uint8_t* p;
    uint8_t b;

    /* We will read maximum of 8 bytes. We don't have to handle the situation
     * to read the RAR5 variable-sized value stored at the end of the file,
     * because such situation will never happen. */
    if(!read_ahead(a, 8, &p))
        return 0;

    for(shift = 0, i = 0; i < 8; i++, shift += 7) {
        b = p[i];

        /* Strip the MSB from the input byte and add the resulting number
         * to the `result`. */
        result += (b & (uint64_t)0x7F) << shift;

        /* MSB set to 1 means we need to continue decoding process. MSB set
         * to 0 means we're done.
         *
         * This conditional checks for the second case. */
        if((b & 0x80) == 0) {
            if(pvalue) {
                *pvalue = result;
            }

            /* If the caller has passed the `pvalue_len` pointer, store the
             * number of consumed bytes in it and do NOT consume those bytes,
             * since the caller has all the information it needs to perform
             * the consuming process itself. */
            if(pvalue_len) {
                *pvalue_len = 1 + i;
            } else {
                /* If the caller did not provide the `pvalue_len` pointer,
                 * it will not have the possibility to advance the file
                 * pointer, because it will not know how many bytes it needs
                 * to consume. This is why we handle such situation here
                 * automatically. */
                if(ARCHIVE_OK != consume(a, 1 + i)) {
                    return 0;
                }
            }

            /* End of decoding process, return success. */
            return 1;
        }
    }

    /* The decoded value takes the maximum number of 8 bytes. It's a maximum
     * number of bytes, so end decoding process here even if the first bit
     * of last byte is 1. */
    if(pvalue) {
        *pvalue = result;
    }

    if(pvalue_len) {
        *pvalue_len = 9;
    } else {
        if(ARCHIVE_OK != consume(a, 9)) {
            return 0;
        }
    }

    return 1;
}

static int read_var_sized(struct archive_read* a, size_t* pvalue,
        size_t* pvalue_len)
{
    uint64_t v;
    uint64_t v_size = 0;

    const int ret = pvalue_len
                    ? read_var(a, &v, &v_size)
                    : read_var(a, &v, NULL);

    if(ret == 1 && pvalue) {
        *pvalue = (size_t) v;
    }

    if(pvalue_len) {
        /* Possible data truncation should be safe. */
        *pvalue_len = (size_t) v_size;
    }

    return ret;
}

static int read_bits_32(struct rar5* rar, const uint8_t* p, uint32_t* value) {
    uint32_t bits = p[rar->bits.in_addr] << 24;
    bits |= p[rar->bits.in_addr + 1] << 16;
    bits |= p[rar->bits.in_addr + 2] << 8;
    bits |= p[rar->bits.in_addr + 3];
    bits <<= rar->bits.bit_addr;
    bits |= p[rar->bits.in_addr + 4] >> (8 - rar->bits.bit_addr);
    *value = bits;
    return ARCHIVE_OK;
}

static int read_bits_16(struct rar5* rar, const uint8_t* p, uint16_t* value) {
    int bits = (int) p[rar->bits.in_addr] << 16;
    bits |= (int) p[rar->bits.in_addr + 1] << 8;
    bits |= (int) p[rar->bits.in_addr + 2];
    bits >>= (8 - rar->bits.bit_addr);
    *value = bits & 0xffff;
    return ARCHIVE_OK;
}

static void skip_bits(struct rar5* rar, int bits) {
    const int new_bits = rar->bits.bit_addr + bits;
    rar->bits.in_addr += new_bits >> 3;
    rar->bits.bit_addr = new_bits & 7;
}

/* n = up to 16 */
static int read_consume_bits(struct rar5* rar, const uint8_t* p, int n,
        int* value)
{
    uint16_t v;
    int ret, num;

    if(n == 0 || n > 16) {
        /* This is a programmer error and should never happen in runtime. */
        return ARCHIVE_FATAL;
    }

    ret = read_bits_16(rar, p, &v);
    if(ret != ARCHIVE_OK)
        return ret;

    num = (int) v;
    num >>= 16 - n;

    skip_bits(rar, n);

    if(value)
        *value = num;

    return ARCHIVE_OK;
}

static int read_u32(struct archive_read* a, uint32_t* pvalue) {
    const uint8_t* p;
    if(!read_ahead(a, 4, &p))
        return 0;

    *pvalue = archive_le32dec(p);
    return ARCHIVE_OK == consume(a, 4) ? 1 : 0;
}

static int read_u64(struct archive_read* a, uint64_t* pvalue) {
    const uint8_t* p;
    if(!read_ahead(a, 8, &p))
        return 0;

    *pvalue = archive_le64dec(p);
    return ARCHIVE_OK == consume(a, 8) ? 1 : 0;
}

static int bid_standard(struct archive_read* a) {
    const uint8_t* p;

    if(!read_ahead(a, rar5_signature_size, &p))
        return -1;

    if(!memcmp(rar5_signature, p, rar5_signature_size))
        return 30;

    return -1;
}

static int rar5_bid(struct archive_read* a, int best_bid) {
    int my_bid;

    if(best_bid > 30)
        return -1;

    my_bid = bid_standard(a);
    if(my_bid > -1) {
        return my_bid;
    }

    return -1;
}

static int rar5_options(struct archive_read *a, const char *key, const char *val) {
    (void) a;
    (void) key;
    (void) val;

    /* No options supported in this version. Return the ARCHIVE_WARN code to
     * signal the options supervisor that the unpacker didn't handle setting
     * this option. */

    return ARCHIVE_WARN;
}

static void init_header(struct archive_read* a) {
    a->archive.archive_format = ARCHIVE_FORMAT_RAR_V5;
    a->archive.archive_format_name = "RAR5";
}

enum HEADER_FLAGS {
    HFL_EXTRA_DATA = 0x0001, HFL_DATA = 0x0002, HFL_SKIP_IF_UNKNOWN = 0x0004,
    HFL_SPLIT_BEFORE = 0x0008, HFL_SPLIT_AFTER = 0x0010, HFL_CHILD = 0x0020,
    HFL_INHERITED = 0x0040
};

static int process_main_locator_extra_block(struct archive_read* a,
        struct rar5* rar)
{
    uint64_t locator_flags;

    if(!read_var(a, &locator_flags, NULL)) {
        return ARCHIVE_EOF;
    }

    enum LOCATOR_FLAGS {
        QLIST = 0x01, RECOVERY = 0x02,
    };

    if(locator_flags & QLIST) {
        if(!read_var(a, &rar->qlist_offset, NULL)) {
            return ARCHIVE_EOF;
        }

        /* qlist is not used */
    }

    if(locator_flags & RECOVERY) {
        if(!read_var(a, &rar->rr_offset, NULL)) {
            return ARCHIVE_EOF;
        }

        /* rr is not used */
    }

    return ARCHIVE_OK;
}

static int parse_file_extra_hash(struct archive_read* a, struct rar5* rar,
        ssize_t* extra_data_size)
{
    size_t hash_type;
    size_t value_len;

    if(!read_var_sized(a, &hash_type, &value_len))
        return ARCHIVE_EOF;

    *extra_data_size -= value_len;
    if(ARCHIVE_OK != consume(a, value_len)) {
        return ARCHIVE_EOF;
    }

    enum HASH_TYPE {
        BLAKE2sp = 0x00
    };

    /* The file uses BLAKE2sp checksum algorithm instead of plain old
     * CRC32. */
    if(hash_type == BLAKE2sp) {
        const uint8_t* p;
        const int hash_size = sizeof(rar->file.blake2sp);

        if(!read_ahead(a, hash_size, &p))
            return ARCHIVE_EOF;

        rar->file.has_blake2 = 1;
        memcpy(&rar->file.blake2sp, p, hash_size);

        if(ARCHIVE_OK != consume(a, hash_size)) {
            return ARCHIVE_EOF;
        }

        *extra_data_size -= hash_size;
    } else {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Unsupported hash type (0x%02x)", (int) hash_type);
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

static uint64_t time_win_to_unix(uint64_t win_time) {
    const size_t ns_in_sec = 10000000;
    const uint64_t sec_to_unix = 11644473600LL;
    return win_time / ns_in_sec - sec_to_unix;
}

static int parse_htime_item(struct archive_read* a, char unix_time,
        uint64_t* where, ssize_t* extra_data_size)
{
    if(unix_time) {
        uint32_t time_val;
        if(!read_u32(a, &time_val))
            return ARCHIVE_EOF;

        *extra_data_size -= 4;
        *where = (uint64_t) time_val;
    } else {
        uint64_t windows_time;
        if(!read_u64(a, &windows_time))
            return ARCHIVE_EOF;

        *where = time_win_to_unix(windows_time);
        *extra_data_size -= 8;
    }

    return ARCHIVE_OK;
}

static int parse_file_extra_htime(struct archive_read* a,
        struct archive_entry* e, struct rar5* rar,
        ssize_t* extra_data_size)
{
    char unix_time = 0;
    size_t flags;
    size_t value_len;

    enum HTIME_FLAGS {
        IS_UNIX       = 0x01,
        HAS_MTIME     = 0x02,
        HAS_CTIME     = 0x04,
        HAS_ATIME     = 0x08,
        HAS_UNIX_NS   = 0x10,
    };

    if(!read_var_sized(a, &flags, &value_len))
        return ARCHIVE_EOF;

    *extra_data_size -= value_len;
    if(ARCHIVE_OK != consume(a, value_len)) {
        return ARCHIVE_EOF;
    }

    unix_time = flags & IS_UNIX;

    if(flags & HAS_MTIME) {
        parse_htime_item(a, unix_time, &rar->file.e_mtime, extra_data_size);
        archive_entry_set_mtime(e, rar->file.e_mtime, 0);
    }

    if(flags & HAS_CTIME) {
        parse_htime_item(a, unix_time, &rar->file.e_ctime, extra_data_size);
        archive_entry_set_ctime(e, rar->file.e_ctime, 0);
    }

    if(flags & HAS_ATIME) {
        parse_htime_item(a, unix_time, &rar->file.e_atime, extra_data_size);
        archive_entry_set_atime(e, rar->file.e_atime, 0);
    }

    if(flags & HAS_UNIX_NS) {
        if(!read_u32(a, &rar->file.e_unix_ns))
            return ARCHIVE_EOF;

        *extra_data_size -= 4;
    }

    return ARCHIVE_OK;
}

static int process_head_file_extra(struct archive_read* a,
        struct archive_entry* e, struct rar5* rar,
        ssize_t extra_data_size)
{
    size_t extra_field_size;
    size_t extra_field_id = 0;
    int ret = ARCHIVE_FATAL;
    size_t var_size;

    enum EXTRA {
        CRYPT = 0x01, HASH = 0x02, HTIME = 0x03, VERSION_ = 0x04,
        REDIR = 0x05, UOWNER = 0x06, SUBDATA = 0x07
    };

    while(extra_data_size > 0) {
        if(!read_var_sized(a, &extra_field_size, &var_size))
            return ARCHIVE_EOF;

        extra_data_size -= var_size;
        if(ARCHIVE_OK != consume(a, var_size)) {
            return ARCHIVE_EOF;
        }

        if(!read_var_sized(a, &extra_field_id, &var_size))
            return ARCHIVE_EOF;

        extra_data_size -= var_size;
        if(ARCHIVE_OK != consume(a, var_size)) {
            return ARCHIVE_EOF;
        }

        switch(extra_field_id) {
            case HASH:
                ret = parse_file_extra_hash(a, rar, &extra_data_size);
                break;
            case HTIME:
                ret = parse_file_extra_htime(a, e, rar, &extra_data_size);
                break;
            case CRYPT:
                /* fallthrough */
            case VERSION_:
                /* fallthrough */
            case REDIR:
                /* fallthrough */
            case UOWNER:
                /* fallthrough */
            case SUBDATA:
                /* fallthrough */
            default:
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Unknown extra field in file/service block: 0x%02x",
                        (int) extra_field_id);
                return ARCHIVE_FATAL;
        }
    }

    if(ret != ARCHIVE_OK) {
        /* Attribute not implemented. */
        return ret;
    }

    return ARCHIVE_OK;
}

static int process_head_file(struct archive_read* a, struct rar5* rar,
        struct archive_entry* entry, size_t block_flags)
{
    ssize_t extra_data_size = 0;
    size_t data_size = 0;
    size_t file_flags = 0;
    size_t file_attr = 0;
    size_t compression_info = 0;
    size_t host_os = 0;
    size_t name_size = 0;
    uint64_t unpacked_size;
    uint32_t mtime = 0, crc = 0;
    int c_method = 0, c_version = 0, is_dir;
    char name_utf8_buf[2048 * 4];
    const uint8_t* p;

    archive_entry_clear(entry);

    /* Do not reset file context if we're switching archives. */
    if(!rar->cstate.switch_multivolume) {
        reset_file_context(rar);
    }

    if(block_flags & HFL_EXTRA_DATA) {
        size_t edata_size = 0;
        if(!read_var_sized(a, &edata_size, NULL))
            return ARCHIVE_EOF;

        /* Intentional type cast from unsigned to signed. */
        extra_data_size = (ssize_t) edata_size;
    }

    if(block_flags & HFL_DATA) {
        if(!read_var_sized(a, &data_size, NULL))
            return ARCHIVE_EOF;

        rar->file.bytes_remaining = data_size;
    } else {
        rar->file.bytes_remaining = 0;

        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "no data found in file/service block");
        return ARCHIVE_FATAL;
    }

    enum FILE_FLAGS {
        DIRECTORY = 0x0001, UTIME = 0x0002, CRC32 = 0x0004,
        UNKNOWN_UNPACKED_SIZE = 0x0008,
    };

    enum COMP_INFO_FLAGS {
        SOLID = 0x0040,
    };

    if(!read_var_sized(a, &file_flags, NULL))
        return ARCHIVE_EOF;

    if(!read_var(a, &unpacked_size, NULL))
        return ARCHIVE_EOF;

    if(file_flags & UNKNOWN_UNPACKED_SIZE) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                "Files with unknown unpacked size are not supported");
        return ARCHIVE_FATAL;
    }

    is_dir = (int) (file_flags & DIRECTORY);

    if(!read_var_sized(a, &file_attr, NULL))
        return ARCHIVE_EOF;

    if(file_flags & UTIME) {
        if(!read_u32(a, &mtime))
            return ARCHIVE_EOF;
    }

    if(file_flags & CRC32) {
        if(!read_u32(a, &crc))
            return ARCHIVE_EOF;
    }

    if(!read_var_sized(a, &compression_info, NULL))
        return ARCHIVE_EOF;

    c_method = (int) (compression_info >> 7) & 0x7;
    c_version = (int) (compression_info & 0x3f);

    rar->cstate.window_size = is_dir ?
        0 :
        g_unpack_window_size << ((compression_info >> 10) & 15);
    rar->cstate.method = c_method;
    rar->cstate.version = c_version + 50;

    rar->file.solid = (compression_info & SOLID) > 0;
    rar->file.service = 0;

    if(!read_var_sized(a, &host_os, NULL))
        return ARCHIVE_EOF;

    enum HOST_OS {
        HOST_WINDOWS = 0,
        HOST_UNIX = 1,
    };

    if(host_os == HOST_WINDOWS) {
        /* Host OS is Windows */

        unsigned short mode = 0660;

        if(is_dir)
            mode |= AE_IFDIR;
        else
            mode |= AE_IFREG;

        archive_entry_set_mode(entry, mode);
    } else if(host_os == HOST_UNIX) {
        /* Host OS is Unix */
        archive_entry_set_mode(entry, (unsigned short) file_attr);
    } else {
        /* Unknown host OS */
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Unsupported Host OS: 0x%02x", (int) host_os);

        return ARCHIVE_FATAL;
    }

    if(!read_var_sized(a, &name_size, NULL))
        return ARCHIVE_EOF;

    if(!read_ahead(a, name_size, &p))
        return ARCHIVE_EOF;

    if(name_size > 2047) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Filename is too long");

        return ARCHIVE_FATAL;
    }

    if(name_size == 0) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "No filename specified");

        return ARCHIVE_FATAL;
    }

    memcpy(name_utf8_buf, p, name_size);
    name_utf8_buf[name_size] = 0;
    if(ARCHIVE_OK != consume(a, name_size)) {
        return ARCHIVE_EOF;
    }

    if(extra_data_size > 0) {
        int ret = process_head_file_extra(a, entry, rar, extra_data_size);

        /* Sanity check. */
        if(extra_data_size < 0) {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                    "File extra data size is not zero");
            return ARCHIVE_FATAL;
        }

        if(ret != ARCHIVE_OK)
            return ret;
    }

    if((file_flags & UNKNOWN_UNPACKED_SIZE) == 0) {
        rar->file.unpacked_size = (ssize_t) unpacked_size;
        archive_entry_set_size(entry, unpacked_size);
    }

    if(file_flags & UTIME) {
        archive_entry_set_mtime(entry, (time_t) mtime, 0);
    }

    if(file_flags & CRC32) {
        rar->file.stored_crc32 = crc;
    }

    archive_entry_update_pathname_utf8(entry, name_utf8_buf);

    if(!rar->cstate.switch_multivolume) {
        /* Do not reinitialize unpacking state if we're switching archives. */
        rar->cstate.block_parsing_finished = 1;
        rar->cstate.all_filters_applied = 1;
        rar->cstate.initialized = 0;
    }

    if(rar->generic.split_before > 0) {
        /* If now we're standing on a header that has a 'split before' mark,
         * it means we're standing on a 'continuation' file header. Signal
         * the caller that if it wants to move to another file, it must call
         * rar5_read_header() function again. */

        return ARCHIVE_RETRY;
    } else {
        return ARCHIVE_OK;
    }
}

static int process_head_service(struct archive_read* a, struct rar5* rar,
        struct archive_entry* entry, size_t block_flags)
{
    /* Process this SERVICE block the same way as FILE blocks. */
    int ret = process_head_file(a, rar, entry, block_flags);
    if(ret != ARCHIVE_OK)
        return ret;

    rar->file.service = 1;

    /* But skip the data part automatically. It's no use for the user anyway.
     * It contains only service data, not even needed to properly unpack the
     * file. */
    ret = rar5_read_data_skip(a);
    if(ret != ARCHIVE_OK)
        return ret;

    /* After skipping, try parsing another block automatically. */
    return ARCHIVE_RETRY;
}

static int process_head_main(struct archive_read* a, struct rar5* rar,
        struct archive_entry* entry, size_t block_flags)
{
    (void) entry;

    int ret;
    size_t extra_data_size = 0;
    size_t extra_field_size = 0;
    size_t extra_field_id = 0;
    size_t archive_flags = 0;

    if(block_flags & HFL_EXTRA_DATA) {
        if(!read_var_sized(a, &extra_data_size, NULL))
            return ARCHIVE_EOF;
    } else {
        extra_data_size = 0;
    }

    if(!read_var_sized(a, &archive_flags, NULL)) {
        return ARCHIVE_EOF;
    }

    enum MAIN_FLAGS {
        VOLUME = 0x0001,         /* multi-volume archive */
        VOLUME_NUMBER = 0x0002,  /* volume number, first vol doesn't have it */
        SOLID = 0x0004,          /* solid archive */
        PROTECT = 0x0008,        /* contains Recovery info */
        LOCK = 0x0010,           /* readonly flag, not used */
    };

    rar->main.volume = (archive_flags & VOLUME) > 0;
    rar->main.solid = (archive_flags & SOLID) > 0;

    if(archive_flags & VOLUME_NUMBER) {
        size_t v = 0;
        if(!read_var_sized(a, &v, NULL)) {
            return ARCHIVE_EOF;
        }

        rar->main.vol_no = (int) v;
    } else {
        rar->main.vol_no = 0;
    }

    if(rar->vol.expected_vol_no > 0 &&
        rar->main.vol_no != rar->vol.expected_vol_no)
    {
        /* Returning EOF instead of FATAL because of strange libarchive
         * behavior. When opening multiple files via
         * archive_read_open_filenames(), after reading up the whole last file,
         * the __archive_read_ahead function wraps up to the first archive
         * instead of returning EOF. */
        return ARCHIVE_EOF;
    }

    if(extra_data_size == 0) {
        /* Early return. */
        return ARCHIVE_OK;
    }

    if(!read_var_sized(a, &extra_field_size, NULL)) {
        return ARCHIVE_EOF;
    }

    if(!read_var_sized(a, &extra_field_id, NULL)) {
        return ARCHIVE_EOF;
    }

    if(extra_field_size == 0) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Invalid extra field size");
        return ARCHIVE_FATAL;
    }

    enum MAIN_EXTRA {
        // Just one attribute here.
        LOCATOR = 0x01,
    };

    switch(extra_field_id) {
        case LOCATOR:
            ret = process_main_locator_extra_block(a, rar);
            if(ret != ARCHIVE_OK) {
                /* Error while parsing main locator extra block. */
                return ret;
            }

            break;
        default:
            archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Unsupported extra type (0x%02x)", (int) extra_field_id);
            return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

static int scan_for_signature(struct archive_read* a);

/* Base block processing function. A 'base block' is a RARv5 header block
 * that tells the reader what kind of data is stored inside the block.
 *
 * From the birds-eye view a RAR file looks file this:
 *
 * <magic><base_block_1><base_block_2>...<base_block_n>
 *
 * There are a few types of base blocks. Those types are specified inside
 * the 'switch' statement in this function. For example purposes, I'll write
 * how a standard RARv5 file could look like here:
 *
 * <magic><MAIN><FILE><FILE><FILE><SERVICE><ENDARC>
 *
 * The structure above could describe an archive file with 3 files in it,
 * one service "QuickOpen" block (that is ignored by this parser), and an
 * end of file base block marker.
 *
 * If the file is stored in multiple archive files ("multiarchive"), it might
 * look like this:
 *
 * .part01.rar: <magic><MAIN><FILE><ENDARC>
 * .part02.rar: <magic><MAIN><FILE><ENDARC>
 * .part03.rar: <magic><MAIN><FILE><ENDARC>
 *
 * This example could describe 3 RAR files that contain ONE archived file.
 * Or it could describe 3 RAR files that contain 3 different files. Or 3
 * RAR files than contain 2 files. It all depends what metadata is stored in
 * the headers of <FILE> blocks.
 *
 * Each <FILE> block contains info about its size, the name of the file it's
 * storing inside, and whether this FILE block is a continuation block of
 * previous archive ('split before'), and is this FILE block should be
 * continued in another archive ('split after'). By parsing the 'split before'
 * and 'split after' flags, we're able to tell if multiple <FILE> base blocks
 * are describing one file, or multiple files (with the same filename, for
 * example).
 *
 * One thing to note is that if we're parsing the first <FILE> block, and
 * we see 'split after' flag, then we need to jump over to another <FILE>
 * block to be able to decompress rest of the data. To do this, we need
 * to skip the <ENDARC> block, then switch to another file, then skip the
 * <magic> block, <MAIN> block, and then we're standing on the proper
 * <FILE> block.
 */

static int process_base_block(struct archive_read* a,
        struct archive_entry* entry)
{
    struct rar5* rar = get_context(a);
    uint32_t hdr_crc, computed_crc;
    size_t raw_hdr_size = 0, hdr_size_len, hdr_size;
    size_t header_id = 0;
    size_t header_flags = 0;
    const uint8_t* p;
    int ret;

    /* Skip any unprocessed data for this file. */
    if(rar->file.bytes_remaining) {
        ret = rar5_read_data_skip(a);
        if(ret != ARCHIVE_OK) {
            return ret;
        }
    }

    /* Read the expected CRC32 checksum. */
    if(!read_u32(a, &hdr_crc)) {
        return ARCHIVE_EOF;
    }

    /* Read header size. */
    if(!read_var_sized(a, &raw_hdr_size, &hdr_size_len)) {
        return ARCHIVE_EOF;
    }

    /* Sanity check, maximum header size for RAR5 is 2MB. */
    if(raw_hdr_size > (2 * 1024 * 1024)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Base block header is too large");

        return ARCHIVE_FATAL;
    }

    hdr_size = raw_hdr_size + hdr_size_len;

    /* Read the whole header data into memory, maximum memory use here is
     * 2MB. */
    if(!read_ahead(a, hdr_size, &p)) {
        return ARCHIVE_EOF;
    }

    /* Verify the CRC32 of the header data. */
    computed_crc = (uint32_t) crc32(0, p, (int) hdr_size);
    if(computed_crc != hdr_crc) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Header CRC error");

        return ARCHIVE_FATAL;
    }

    /* If the checksum is OK, we proceed with parsing. */
    if(ARCHIVE_OK != consume(a, hdr_size_len)) {
        return ARCHIVE_EOF;
    }

    if(!read_var_sized(a, &header_id, NULL))
        return ARCHIVE_EOF;

    if(!read_var_sized(a, &header_flags, NULL))
        return ARCHIVE_EOF;

    rar->generic.split_after = (header_flags & HFL_SPLIT_AFTER) > 0;
    rar->generic.split_before = (header_flags & HFL_SPLIT_BEFORE) > 0;
    rar->generic.size = (int)hdr_size;
    rar->generic.last_header_id = (int)header_id;
    rar->main.endarc = 0;

    /* Those are possible header ids in RARv5. */
    enum HEADER_TYPE {
        HEAD_MARK    = 0x00, HEAD_MAIN  = 0x01, HEAD_FILE   = 0x02,
        HEAD_SERVICE = 0x03, HEAD_CRYPT = 0x04, HEAD_ENDARC = 0x05,
        HEAD_UNKNOWN = 0xff,
    };

    switch(header_id) {
        case HEAD_MAIN:
            ret = process_head_main(a, rar, entry, header_flags);

            /* Main header doesn't have any files in it, so it's pointless
             * to return to the caller. Retry to next header, which should be
             * HEAD_FILE/HEAD_SERVICE. */
            if(ret == ARCHIVE_OK)
                return ARCHIVE_RETRY;

            return ret;
        case HEAD_SERVICE:
            ret = process_head_service(a, rar, entry, header_flags);
            return ret;
        case HEAD_FILE:
            ret = process_head_file(a, rar, entry, header_flags);
            return ret;
        case HEAD_CRYPT:
            archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Encryption is not supported");
            return ARCHIVE_FATAL;
        case HEAD_ENDARC:
            rar->main.endarc = 1;

            /* After encountering an end of file marker, we need to take
             * into consideration if this archive is continued in another
             * file (i.e. is it part01.rar: is there a part02.rar?) */
            if(rar->main.volume) {
                /* In case there is part02.rar, position the read pointer
                 * in a proper place, so we can resume parsing. */

                ret = scan_for_signature(a);
                if(ret == ARCHIVE_FATAL) {
                    return ARCHIVE_EOF;
                } else {
                    rar->vol.expected_vol_no = rar->main.vol_no + 1;
                    return ARCHIVE_OK;
                }
            } else {
                return ARCHIVE_EOF;
            }
        case HEAD_MARK:
            return ARCHIVE_EOF;
        default:
            if((header_flags & HFL_SKIP_IF_UNKNOWN) == 0) {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Header type error");
                return ARCHIVE_FATAL;
            } else {
                /* If the block is marked as 'skip if unknown', do as the flag
                 * says: skip the block instead on failing on it. */
                return ARCHIVE_RETRY;
            }
    }

#if !defined WIN32
    // Not reached.
    archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
            "Internal unpacker error");
    return ARCHIVE_FATAL;
#endif
}

static int skip_base_block(struct archive_read* a) {
    int ret;
    struct rar5* rar = get_context(a);

    /* Create a new local archive_entry structure that will be operated on
     * by header reader; operations on this archive_entry will be discarded.
     */
    struct archive_entry* entry = archive_entry_new();
    ret = process_base_block(a, entry);

    /* Discard operations on this archive_entry structure. */
    archive_entry_free(entry);

    if(rar->generic.last_header_id == 2 && rar->generic.split_before > 0)
        return ARCHIVE_OK;

    if(ret == ARCHIVE_OK)
        return ARCHIVE_RETRY;
    else
        return ret;
}

static int rar5_read_header(struct archive_read *a,
        struct archive_entry *entry)
{
    struct rar5* rar = get_context(a);
    int ret;

    if(rar->header_initialized == 0) {
        init_header(a);
        rar->header_initialized = 1;
    }

    if(rar->skipped_magic == 0) {
        if(ARCHIVE_OK != consume(a, rar5_signature_size)) {
            return ARCHIVE_EOF;
        }

        rar->skipped_magic = 1;
    }

    do {
        ret = process_base_block(a, entry);
    } while(ret == ARCHIVE_RETRY ||
            (rar->main.endarc > 0 && ret == ARCHIVE_OK));

    return ret;
}

static void init_unpack(struct rar5* rar) {
    rar->file.calculated_crc32 = 0;
    if (rar->cstate.window_size)
        rar->cstate.window_mask = rar->cstate.window_size - 1;
    else
        rar->cstate.window_mask = 0;

    free(rar->cstate.window_buf);

    free(rar->cstate.filtered_buf);

    rar->cstate.window_buf = calloc(1, rar->cstate.window_size);
    rar->cstate.filtered_buf = calloc(1, rar->cstate.window_size);

    rar->cstate.write_ptr = 0;
    rar->cstate.last_write_ptr = 0;

    memset(&rar->cstate.bd, 0, sizeof(rar->cstate.bd));
    memset(&rar->cstate.ld, 0, sizeof(rar->cstate.ld));
    memset(&rar->cstate.dd, 0, sizeof(rar->cstate.dd));
    memset(&rar->cstate.ldd, 0, sizeof(rar->cstate.ldd));
    memset(&rar->cstate.rd, 0, sizeof(rar->cstate.rd));
}

static void update_crc(struct rar5* rar, const uint8_t* p, size_t to_read) {
    int verify_crc;

    if(rar->skip_mode) {
#if defined CHECK_CRC_ON_SOLID_SKIP
        verify_crc = 1;
#else
        verify_crc = 0;
#endif
    } else
        verify_crc = 1;

    if(verify_crc) {
        /* Don't update CRC32 if the file doesn't have the `stored_crc32` info
           filled in. */
        if(rar->file.stored_crc32 > 0) {
            rar->file.calculated_crc32 =
                crc32(rar->file.calculated_crc32, p, to_read);
        }

        /* Check if the file uses an optional BLAKE2sp checksum algorithm. */
        if(rar->file.has_blake2 > 0) {
            /* Return value of the `update` function is always 0, so we can
             * explicitly ignore it here. */
            (void) blake2sp_update(&rar->file.b2state, p, to_read);
        }
    }
}

static int create_decode_tables(uint8_t* bit_length,
        struct decode_table* table,
        int size)
{
    int code, upper_limit = 0, i, lc[16];
    uint32_t decode_pos_clone[rar5_countof(table->decode_pos)];
    ssize_t cur_len, quick_data_size;

    memset(&lc, 0, sizeof(lc));
    memset(table->decode_num, 0, sizeof(table->decode_num));
    table->size = size;
    table->quick_bits = size == HUFF_NC ? 10 : 7;

    for(i = 0; i < size; i++) {
        lc[bit_length[i] & 15]++;
    }

    lc[0] = 0;
    table->decode_pos[0] = 0;
    table->decode_len[0] = 0;

    for(i = 1; i < 16; i++) {
        upper_limit += lc[i];

        table->decode_len[i] = upper_limit << (16 - i);
        table->decode_pos[i] = table->decode_pos[i - 1] + lc[i - 1];

        upper_limit <<= 1;
    }

    memcpy(decode_pos_clone, table->decode_pos, sizeof(decode_pos_clone));

    for(i = 0; i < size; i++) {
        uint8_t clen = bit_length[i] & 15;
        if(clen > 0) {
            int last_pos = decode_pos_clone[clen];
            table->decode_num[last_pos] = i;
            decode_pos_clone[clen]++;
        }
    }

    quick_data_size = (int64_t)1 << table->quick_bits;
    cur_len = 1;
    for(code = 0; code < quick_data_size; code++) {
        int bit_field = code << (16 - table->quick_bits);
        int dist, pos;

        while(cur_len < rar5_countof(table->decode_len) &&
                bit_field >= table->decode_len[cur_len]) {
            cur_len++;
        }

        table->quick_len[code] = (uint8_t) cur_len;

        dist = bit_field - table->decode_len[cur_len - 1];
        dist >>= (16 - cur_len);

        pos = table->decode_pos[cur_len & 15] + dist;
        if(cur_len < rar5_countof(table->decode_pos) && pos < size) {
            table->quick_num[code] = table->decode_num[pos];
        } else {
            table->quick_num[code] = 0;
        }
    }

    return ARCHIVE_OK;
}

static int decode_number(struct archive_read* a, struct decode_table* table,
        const uint8_t* p, uint16_t* num)
{
    int i, bits, dist;
    uint16_t bitfield;
    uint32_t pos;
    struct rar5* rar = get_context(a);

    if(ARCHIVE_OK != read_bits_16(rar, p, &bitfield)) {
        return ARCHIVE_EOF;
    }

    bitfield &= 0xfffe;

    if(bitfield < table->decode_len[table->quick_bits]) {
        int code = bitfield >> (16 - table->quick_bits);
        skip_bits(rar, table->quick_len[code]);
        *num = table->quick_num[code];
        return ARCHIVE_OK;
    }

    bits = 15;

    for(i = table->quick_bits + 1; i < 15; i++) {
        if(bitfield < table->decode_len[i]) {
            bits = i;
            break;
        }
    }

    skip_bits(rar, bits);

    dist = bitfield - table->decode_len[bits - 1];
    dist >>= (16 - bits);
    pos = table->decode_pos[bits] + dist;

    if(pos >= table->size)
        pos = 0;

    *num = table->decode_num[pos];
    return ARCHIVE_OK;
}

/* Reads and parses Huffman tables from the beginning of the block. */
static int parse_tables(struct archive_read* a, struct rar5* rar,
        const uint8_t* p)
{
    int ret, value, i, w, idx = 0;
    uint8_t bit_length[HUFF_BC],
        table[HUFF_TABLE_SIZE],
        nibble_mask = 0xF0,
        nibble_shift = 4;

    enum { ESCAPE = 15 };

    /* The data for table generation is compressed using a simple RLE-like
     * algorithm when storing zeroes, so we need to unpack it first. */
    for(w = 0, i = 0; w < HUFF_BC;) {
        value = (p[i] & nibble_mask) >> nibble_shift;

        if(nibble_mask == 0x0F)
            ++i;

        nibble_mask ^= 0xFF;
        nibble_shift ^= 4;

        /* Values smaller than 15 is data, so we write it directly. Value 15
         * is a flag telling us that we need to unpack more bytes. */
        if(value == ESCAPE) {
            value = (p[i] & nibble_mask) >> nibble_shift;
            if(nibble_mask == 0x0F)
                ++i;
            nibble_mask ^= 0xFF;
            nibble_shift ^= 4;

            if(value == 0) {
                /* We sometimes need to write the actual value of 15, so this
                 * case handles that. */
                bit_length[w++] = ESCAPE;
            } else {
                int k;

                /* Fill zeroes. */
                for(k = 0; k < value + 2; k++) {
                    bit_length[w++] = 0;
                }
            }
        } else {
            bit_length[w++] = value;
        }
    }

    rar->bits.in_addr = i;
    rar->bits.bit_addr = nibble_shift ^ 4;

    ret = create_decode_tables(bit_length, &rar->cstate.bd, HUFF_BC);
    if(ret != ARCHIVE_OK) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Decoding huffman tables failed");
        return ARCHIVE_FATAL;
    }

    for(i = 0; i < HUFF_TABLE_SIZE;) {
        uint16_t num;

        ret = decode_number(a, &rar->cstate.bd, p, &num);
        if(ret != ARCHIVE_OK) {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Decoding huffman tables failed");
            return ARCHIVE_FATAL;
        }

        if(num < 16) {
            /* 0..15: store directly */
            table[i] = (uint8_t) num;
            i++;
            continue;
        }

        if(num < 18) {
            /* 16..17: repeat previous code */
            uint16_t n;
            if(ARCHIVE_OK != read_bits_16(rar, p, &n))
                return ARCHIVE_EOF;

            if(num == 16) {
                n >>= 13;
                n += 3;
                skip_bits(rar, 3);
            } else {
                n >>= 9;
                n += 11;
                skip_bits(rar, 7);
            }

            if(i > 0) {
                while(n-- > 0 && i < HUFF_TABLE_SIZE) {
                    table[i] = table[i - 1];
                    i++;
                }
            } else {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Unexpected error when decoding huffman tables");
                return ARCHIVE_FATAL;
            }

            continue;
        }

        /* other codes: fill with zeroes `n` times */
        uint16_t n;
        if(ARCHIVE_OK != read_bits_16(rar, p, &n))
            return ARCHIVE_EOF;

        if(num == 18) {
            n >>= 13;
            n += 3;
            skip_bits(rar, 3);
        } else {
            n >>= 9;
            n += 11;
            skip_bits(rar, 7);
        }

        while(n-- > 0 && i < HUFF_TABLE_SIZE)
            table[i++] = 0;
    }

    ret = create_decode_tables(&table[idx], &rar->cstate.ld, HUFF_NC);
    if(ret != ARCHIVE_OK) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Failed to create literal table");
        return ARCHIVE_FATAL;
    }

    idx += HUFF_NC;

    ret = create_decode_tables(&table[idx], &rar->cstate.dd, HUFF_DC);
    if(ret != ARCHIVE_OK) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Failed to create distance table");
        return ARCHIVE_FATAL;
    }

    idx += HUFF_DC;

    ret = create_decode_tables(&table[idx], &rar->cstate.ldd, HUFF_LDC);
    if(ret != ARCHIVE_OK) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Failed to create lower bits of distances table");
        return ARCHIVE_FATAL;
    }

    idx += HUFF_LDC;

    ret = create_decode_tables(&table[idx], &rar->cstate.rd, HUFF_RC);
    if(ret != ARCHIVE_OK) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Failed to create repeating distances table");
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

/* Parses the block header, verifies its CRC byte, and saves the header
 * fields inside the `hdr` pointer. */
static int parse_block_header(struct archive_read* a, const uint8_t* p,
        ssize_t* block_size, struct compressed_block_header* hdr)
{
    memcpy(hdr, p, sizeof(struct compressed_block_header));

    if(bf_byte_count(hdr) > 2) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Unsupported block header size (was %d, max is 2)",
                bf_byte_count(hdr));
        return ARCHIVE_FATAL;
    }

    /* This should probably use bit reader interface in order to be more
     * future-proof. */
    *block_size = 0;
    switch(bf_byte_count(hdr)) {
        /* 1-byte block size */
        case 0:
            *block_size = *(const uint8_t*) &p[2];
            break;

        /* 2-byte block size */
        case 1:
            *block_size = archive_le16dec(&p[2]);
            break;

        /* 3-byte block size */
        case 2:
            *block_size = archive_le32dec(&p[2]);
            *block_size &= 0x00FFFFFF;
            break;

        /* Other block sizes are not supported. This case is not reached,
         * because we have an 'if' guard before the switch that makes sure
         * of it. */
        default:
            return ARCHIVE_FATAL;
    }

    /* Verify the block header checksum. 0x5A is a magic value and is always
     * constant. */
    uint8_t calculated_cksum = 0x5A
                               ^ (uint8_t) hdr->block_flags_u8
                               ^ (uint8_t) *block_size
                               ^ (uint8_t) (*block_size >> 8)
                               ^ (uint8_t) (*block_size >> 16);

    if(calculated_cksum != hdr->block_cksum) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Block checksum error: got 0x%02x, expected 0x%02x",
                hdr->block_cksum, calculated_cksum);

        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

/* Convenience function used during filter processing. */
static int parse_filter_data(struct rar5* rar, const uint8_t* p,
        uint32_t* filter_data)
{
    int i, bytes;
    uint32_t data = 0;

    if(ARCHIVE_OK != read_consume_bits(rar, p, 2, &bytes))
        return ARCHIVE_EOF;

    bytes++;

    for(i = 0; i < bytes; i++) {
        uint16_t byte;

        if(ARCHIVE_OK != read_bits_16(rar, p, &byte)) {
            return ARCHIVE_EOF;
        }

        data += (byte >> 8) << (i * 8);
        skip_bits(rar, 8);
    }

    *filter_data = data;
    return ARCHIVE_OK;
}

/* Function is used during sanity checking. */
static int is_valid_filter_block_start(struct rar5* rar,
        uint32_t start)
{
    const int64_t block_start = (ssize_t) start + rar->cstate.write_ptr;
    const int64_t last_bs = rar->cstate.last_block_start;
    const ssize_t last_bl = rar->cstate.last_block_length;

    if(last_bs == 0 || last_bl == 0) {
        /* We didn't have any filters yet, so accept this offset. */
        return 1;
    }

    if(block_start >= last_bs + last_bl) {
        /* Current offset is bigger than last block's end offset, so
         * accept current offset. */
        return 1;
    }

    /* Any other case is not a normal situation and we should fail. */
    return 0;
}

/* The function will create a new filter, read its parameters from the input
 * stream and add it to the filter collection. */
static int parse_filter(struct archive_read* ar, const uint8_t* p) {
    uint32_t block_start, block_length;
    uint16_t filter_type;
    struct rar5* rar = get_context(ar);

    /* Read the parameters from the input stream. */
    if(ARCHIVE_OK != parse_filter_data(rar, p, &block_start))
        return ARCHIVE_EOF;

    if(ARCHIVE_OK != parse_filter_data(rar, p, &block_length))
        return ARCHIVE_EOF;

    if(ARCHIVE_OK != read_bits_16(rar, p, &filter_type))
        return ARCHIVE_EOF;

    filter_type >>= 13;
    skip_bits(rar, 3);

    /* Perform some sanity checks on this filter parameters. Note that we
     * allow only DELTA, E8/E9 and ARM filters here, because rest of filters
     * are not used in RARv5. */

    if(block_length < 4 ||
        block_length > 0x400000 ||
        filter_type > FILTER_ARM ||
        !is_valid_filter_block_start(rar, block_start))
    {
        archive_set_error(&ar->archive, ARCHIVE_ERRNO_FILE_FORMAT, "Invalid "
                "filter encountered");
        return ARCHIVE_FATAL;
    }

    /* Allocate a new filter. */
    struct filter_info* filt = add_new_filter(rar);
    if(filt == NULL) {
        archive_set_error(&ar->archive, ENOMEM, "Can't allocate memory for a "
                "filter descriptor.");
        return ARCHIVE_FATAL;
    }

    filt->type = filter_type;
    filt->block_start = rar->cstate.write_ptr + block_start;
    filt->block_length = block_length;

    rar->cstate.last_block_start = filt->block_start;
    rar->cstate.last_block_length = filt->block_length;

    /* Read some more data in case this is a DELTA filter. Other filter types
     * don't require any additional data over what was already read. */
    if(filter_type == FILTER_DELTA) {
        int channels;

        if(ARCHIVE_OK != read_consume_bits(rar, p, 5, &channels))
            return ARCHIVE_EOF;

        filt->channels = channels + 1;
    }

    return ARCHIVE_OK;
}

static int decode_code_length(struct rar5* rar, const uint8_t* p,
        uint16_t code)
{
    int lbits, length = 2;
    if(code < 8) {
        lbits = 0;
        length += code;
    } else {
        lbits = code / 4 - 1;
        length += (4 | (code & 3)) << lbits;
    }

    if(lbits > 0) {
        int add;

        if(ARCHIVE_OK != read_consume_bits(rar, p, lbits, &add))
            return -1;

        length += add;
    }

    return length;
}

static int copy_string(struct archive_read* a, int len, int dist) {
    struct rar5* rar = get_context(a);
    const int cmask = (int)rar->cstate.window_mask;
    const int64_t write_ptr = rar->cstate.write_ptr + rar->cstate.solid_offset;
    int i;

    /* The unpacker spends most of the time in this function. It would be
     * a good idea to introduce some optimizations here.
     *
     * Just remember that this loop treats buffers that overlap differently
     * than buffers that do not overlap. This is why a simple memcpy(3) call
     * will not be enough. */

    for(i = 0; i < len; i++) {
        const ssize_t write_idx = (write_ptr + i) & cmask;
        const ssize_t read_idx = (write_ptr + i - dist) & cmask;
        rar->cstate.window_buf[write_idx] = rar->cstate.window_buf[read_idx];
    }

    rar->cstate.write_ptr += len;
    return ARCHIVE_OK;
}

static int do_uncompress_block(struct archive_read* a, const uint8_t* p) {
    struct rar5* rar = get_context(a);
    uint16_t num;
    int ret;

    const int cmask = (int)rar->cstate.window_mask;
    const struct compressed_block_header* hdr = &rar->last_block_hdr;
    const uint8_t bit_size = 1 + bf_bit_size(hdr);

    while(1) {
        if(rar->cstate.write_ptr - rar->cstate.last_write_ptr >
                (rar->cstate.window_size >> 1)) {

            /* Don't allow growing data by more than half of the window size
             * at a time. In such case, break the loop; next call to this
             * function will continue processing from this moment. */

            break;
        }

        if(rar->bits.in_addr > rar->cstate.cur_block_size - 1 ||
                (rar->bits.in_addr == rar->cstate.cur_block_size - 1 &&
                 rar->bits.bit_addr >= bit_size))
        {
            /* If the program counter is here, it means the function has
             * finished processing the block. */
            rar->cstate.block_parsing_finished = 1;
            break;
        }

        /* Decode the next literal. */
        if(ARCHIVE_OK != decode_number(a, &rar->cstate.ld, p, &num)) {
            return ARCHIVE_EOF;
        }

        /* Num holds a decompression literal, or 'command code'.
         *
         * - Values lower than 256 are just bytes. Those codes can be stored
         *   in the output buffer directly.
         *
         * - Code 256 defines a new filter, which is later used to transform
         *   the data block accordingly to the filter type. The data block
         *   needs to be fully uncompressed first.
         *
         * - Code bigger than 257 and smaller than 262 define a repetition
         *   pattern that should be copied from an already uncompressed chunk
         *   of data.
         */

        if(num < 256) {
            /* Directly store the byte. */

            int64_t write_idx = rar->cstate.solid_offset +
                rar->cstate.write_ptr++;

            rar->cstate.window_buf[write_idx & cmask] = (uint8_t) num;
            continue;
        } else if(num >= 262) {
            uint16_t dist_slot;
            int len = decode_code_length(rar, p, num - 262),
                dbits,
                dist = 1;

            if(len == -1) {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                    "Failed to decode the code length");

                return ARCHIVE_FATAL;
            }

            if(ARCHIVE_OK != decode_number(a, &rar->cstate.dd, p, &dist_slot))
            {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                    "Failed to decode the distance slot");

                return ARCHIVE_FATAL;
            }

            if(dist_slot < 4) {
                dbits = 0;
                dist += dist_slot;
            } else {
                dbits = dist_slot / 2 - 1;
                dist += (2 | (dist_slot & 1)) << dbits;
            }

            if(dbits > 0) {
                if(dbits >= 4) {
                    uint32_t add = 0;
                    uint16_t low_dist;

                    if(dbits > 4) {
                        if(ARCHIVE_OK != read_bits_32(rar, p, &add)) {
                            /* Return EOF if we can't read more data. */
                            return ARCHIVE_EOF;
                        }

                        skip_bits(rar, dbits - 4);
                        add = (add >> (36 - dbits)) << 4;
                        dist += add;
                    }

                    if(ARCHIVE_OK != decode_number(a, &rar->cstate.ldd, p,
                                &low_dist))
                    {
                        archive_set_error(&a->archive,
                                ARCHIVE_ERRNO_PROGRAMMER,
                                "Failed to decode the distance slot");

                        return ARCHIVE_FATAL;
                    }

                    dist += low_dist;
                } else {
                    /* dbits is one of [0,1,2,3] */
                    int add;

                    if(ARCHIVE_OK != read_consume_bits(rar, p, dbits, &add)) {
                        /* Return EOF if we can't read more data. */
                        return ARCHIVE_EOF;
                    }

                    dist += add;
                }
            }

            if(dist > 0x100) {
                len++;

                if(dist > 0x2000) {
                    len++;

                    if(dist > 0x40000) {
                        len++;
                    }
                }
            }

            dist_cache_push(rar, dist);
            rar->cstate.last_len = len;

            if(ARCHIVE_OK != copy_string(a, len, dist))
                return ARCHIVE_FATAL;

            continue;
        } else if(num == 256) {
            /* Create a filter. */
            ret = parse_filter(a, p);
            if(ret != ARCHIVE_OK)
                return ret;

            continue;
        } else if(num == 257) {
            if(rar->cstate.last_len != 0) {
                if(ARCHIVE_OK != copy_string(a, rar->cstate.last_len,
                            rar->cstate.dist_cache[0]))
                {
                    return ARCHIVE_FATAL;
                }
            }

            continue;
        } else if(num < 262) {
            const int idx = num - 258;
            const int dist = dist_cache_touch(rar, idx);

            uint16_t len_slot;
            int len;

            if(ARCHIVE_OK != decode_number(a, &rar->cstate.rd, p, &len_slot)) {
                return ARCHIVE_FATAL;
            }

            len = decode_code_length(rar, p, len_slot);
            rar->cstate.last_len = len;

            if(ARCHIVE_OK != copy_string(a, len, dist))
                return ARCHIVE_FATAL;

            continue;
        }

        /* The program counter shouldn't reach here. */
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                "Unsupported block code: 0x%02x", num);

        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

/* Binary search for the RARv5 signature. */
static int scan_for_signature(struct archive_read* a) {
    const uint8_t* p;
    const int chunk_size = 512;
    ssize_t i;

    /* If we're here, it means we're on an 'unknown territory' data.
     * There's no indication what kind of data we're reading here. It could be
     * some text comment, any kind of binary data, digital sign, dragons, etc.
     *
     * We want to find a valid RARv5 magic header inside this unknown data. */

    /* Is it possible in libarchive to just skip everything until the
     * end of the file? If so, it would be a better approach than the
     * current implementation of this function. */

    while(1) {
        if(!read_ahead(a, chunk_size, &p))
            return ARCHIVE_EOF;

        for(i = 0; i < chunk_size - rar5_signature_size; i++) {
            if(memcmp(&p[i], rar5_signature, rar5_signature_size) == 0) {
                /* Consume the number of bytes we've used to search for the
                 * signature, as well as the number of bytes used by the
                 * signature itself. After this we should be standing on a
                 * valid base block header. */
                (void) consume(a, i + rar5_signature_size);
                return ARCHIVE_OK;
            }
        }

        consume(a, chunk_size);
    }

    return ARCHIVE_FATAL;
}

/* This function will switch the multivolume archive file to another file,
 * i.e. from part03 to part 04. */
static int advance_multivolume(struct archive_read* a) {
    int lret;
    struct rar5* rar = get_context(a);

    /* A small state machine that will skip unnecessary data, needed to
     * switch from one multivolume to another. Such skipping is needed if
     * we want to be an stream-oriented (instead of file-oriented)
     * unpacker.
     *
     * The state machine starts with `rar->main.endarc` == 0. It also
     * assumes that current stream pointer points to some base block header.
     *
     * The `endarc` field is being set when the base block parsing function
     * encounters the 'end of archive' marker.
     */

    while(1) {
        if(rar->main.endarc == 1) {
            rar->main.endarc = 0;
            while(ARCHIVE_RETRY == skip_base_block(a));
            break;
        } else {
            /* Skip current base block. In order to properly skip it,
             * we really need to simply parse it and discard the results. */

            lret = skip_base_block(a);

            /* The `skip_base_block` function tells us if we should continue
             * with skipping, or we should stop skipping. We're trying to skip
             * everything up to a base FILE block. */

            if(lret != ARCHIVE_RETRY) {
                /* If there was an error during skipping, or we have just
                 * skipped a FILE base block... */

                if(rar->main.endarc == 0) {
                    return lret;
                } else {
                    continue;
                }
            }
        }
    }

    return ARCHIVE_OK;
}

/* Merges the partial block from the first multivolume archive file, and
 * partial block from the second multivolume archive file. The result is
 * a chunk of memory containing the whole block, and the stream pointer
 * is advanced to the next block in the second multivolume archive file. */
static int merge_block(struct archive_read* a, ssize_t block_size,
        const uint8_t** p)
{
    struct rar5* rar = get_context(a);
    ssize_t cur_block_size, partial_offset = 0;
    const uint8_t* lp;
    int ret;

    /* Set a flag that we're in the switching mode. */
    rar->cstate.switch_multivolume = 1;

    /* Reallocate the memory which will hold the whole block. */
    if(rar->vol.push_buf)
        free((void*) rar->vol.push_buf);

    /* Increasing the allocation block by 8 is due to bit reading functions,
     * which are using additional 2 or 4 bytes. Allocating the block size
     * by exact value would make bit reader perform reads from invalid memory
     * block when reading the last byte from the buffer. */
    rar->vol.push_buf = malloc(block_size + 8);
    if(!rar->vol.push_buf) {
        archive_set_error(&a->archive, ENOMEM, "Can't allocate memory for a "
                "merge block buffer.");
        return ARCHIVE_FATAL;
    }

    /* Valgrind complains if the extension block for bit reader is not
     * initialized, so initialize it. */
    memset(&rar->vol.push_buf[block_size], 0, 8);

    /* A single block can span across multiple multivolume archive files,
     * so we use a loop here. This loop will consume enough multivolume
     * archive files until the whole block is read. */

    while(1) {
        /* Get the size of current block chunk in this multivolume archive
         * file and read it. */
        cur_block_size =
            rar5_min(rar->file.bytes_remaining, block_size - partial_offset);

        if(cur_block_size == 0) {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Encountered block size == 0 during block merge");
            return ARCHIVE_FATAL;
        }

        if(!read_ahead(a, cur_block_size, &lp))
            return ARCHIVE_EOF;

        /* Sanity check; there should never be a situation where this function
         * reads more data than the block's size. */
        if(partial_offset + cur_block_size > block_size) {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                "Consumed too much data when merging blocks.");
            return ARCHIVE_FATAL;
        }

        /* Merge previous block chunk with current block chunk, or create
         * first block chunk if this is our first iteration. */
        memcpy(&rar->vol.push_buf[partial_offset], lp, cur_block_size);

        /* Advance the stream read pointer by this block chunk size. */
        if(ARCHIVE_OK != consume(a, cur_block_size))
            return ARCHIVE_EOF;

        /* Update the pointers. `partial_offset` contains information about
         * the sum of merged block chunks. */
        partial_offset += cur_block_size;
        rar->file.bytes_remaining -= cur_block_size;

        /* If `partial_offset` is the same as `block_size`, this means we've
         * merged all block chunks and we have a valid full block. */
        if(partial_offset == block_size) {
            break;
        }

        /* If we don't have any bytes to read, this means we should switch
         * to another multivolume archive file. */
        if(rar->file.bytes_remaining == 0) {
            ret = advance_multivolume(a);
            if(ret != ARCHIVE_OK)
                return ret;
        }
    }

    *p = rar->vol.push_buf;

    /* If we're here, we can resume unpacking by processing the block pointed
     * to by the `*p` memory pointer. */

    return ARCHIVE_OK;
}

static int process_block(struct archive_read* a) {
    const uint8_t* p;
    struct rar5* rar = get_context(a);
    int ret;

    /* If we don't have any data to be processed, this most probably means
     * we need to switch to the next volume. */
    if(rar->main.volume && rar->file.bytes_remaining == 0) {
        ret = advance_multivolume(a);
        if(ret != ARCHIVE_OK)
            return ret;
    }

    if(rar->cstate.block_parsing_finished) {
        ssize_t block_size;

        rar->cstate.block_parsing_finished = 0;

        /* The header size won't be bigger than 6 bytes. */
        if(!read_ahead(a, 6, &p)) {
            /* Failed to prefetch data block header. */
            return ARCHIVE_EOF;
        }

        /*
         * Read block_size by parsing block header. Validate the header by
         * calculating CRC byte stored inside the header. Size of the header is
         * not constant (block size can be stored either in 1 or 2 bytes),
         * that's why block size is left out from the `compressed_block_header`
         * structure and returned by `parse_block_header` as the second
         * argument. */

        ret = parse_block_header(a, p, &block_size, &rar->last_block_hdr);
        if(ret != ARCHIVE_OK)
            return ret;

        /* Skip block header. Next data is huffman tables, if present. */
        ssize_t to_skip = sizeof(struct compressed_block_header) +
            bf_byte_count(&rar->last_block_hdr) + 1;

        if(ARCHIVE_OK != consume(a, to_skip))
            return ARCHIVE_EOF;

        rar->file.bytes_remaining -= to_skip;

        /* The block size gives information about the whole block size, but
         * the block could be stored in split form when using multi-volume
         * archives. In this case, the block size will be bigger than the
         * actual data stored in this file. Remaining part of the data will
         * be in another file. */

        ssize_t cur_block_size =
            rar5_min(rar->file.bytes_remaining, block_size);

        if(block_size > rar->file.bytes_remaining) {
            /* If current blocks' size is bigger than our data size, this
             * means we have a multivolume archive. In this case, skip
             * all base headers until the end of the file, proceed to next
             * "partXXX.rar" volume, find its signature, skip all headers up
             * to the first FILE base header, and continue from there.
             *
             * Note that `merge_block` will update the `rar` context structure
             * quite extensively. */

            ret = merge_block(a, block_size, &p);
            if(ret != ARCHIVE_OK) {
                return ret;
            }

            cur_block_size = block_size;

            /* Current stream pointer should be now directly *after* the
             * block that spanned through multiple archive files. `p` pointer
             * should have the data of the *whole* block (merged from
             * partial blocks stored in multiple archives files). */
        } else {
            rar->cstate.switch_multivolume = 0;

            /* Read the whole block size into memory. This can take up to
             * 8 megabytes of memory in theoretical cases. Might be worth to
             * optimize this and use a standard chunk of 4kb's. */

            if(!read_ahead(a, 4 + cur_block_size, &p)) {
                /* Failed to prefetch block data. */
                return ARCHIVE_EOF;
            }
        }

        rar->cstate.block_buf = p;
        rar->cstate.cur_block_size = cur_block_size;

        rar->bits.in_addr = 0;
        rar->bits.bit_addr = 0;

        if(bf_is_table_present(&rar->last_block_hdr)) {
            /* Load Huffman tables. */
            ret = parse_tables(a, rar, p);
            if(ret != ARCHIVE_OK) {
                /* Error during decompression of Huffman tables. */
                return ret;
            }
        }
    } else {
        p = rar->cstate.block_buf;
    }

    /* Uncompress the block, or a part of it, depending on how many bytes
     * will be generated by uncompressing the block.
     *
     * In case too many bytes will be generated, calling this function again
     * will resume the uncompression operation. */
    ret = do_uncompress_block(a, p);
    if(ret != ARCHIVE_OK) {
        return ret;
    }

    if(rar->cstate.block_parsing_finished &&
            rar->cstate.switch_multivolume == 0 &&
            rar->cstate.cur_block_size > 0)
    {
        /* If we're processing a normal block, consume the whole block. We
         * can do this because we've already read the whole block to memory.
         */
        if(ARCHIVE_OK != consume(a, rar->cstate.cur_block_size))
            return ARCHIVE_FATAL;

        rar->file.bytes_remaining -= rar->cstate.cur_block_size;
    } else if(rar->cstate.switch_multivolume) {
        /* Don't consume the block if we're doing multivolume processing.
         * The volume switching function will consume the proper count of
         * bytes instead. */

        rar->cstate.switch_multivolume = 0;
    }

    return ARCHIVE_OK;
}

/* Pops the `buf`, `size` and `offset` from the "data ready" stack.
 *
 * Returns ARCHIVE_OK when those arguments can be used, ARCHIVE_RETRY
 * when there is no data on the stack. */
static int use_data(struct rar5* rar, const void** buf, size_t* size,
        int64_t* offset)
{
    int i;

    for(i = 0; i < rar5_countof(rar->cstate.dready); i++) {
        struct data_ready *d = &rar->cstate.dready[i];

        if(d->used) {
            if(buf)    *buf = d->buf;
            if(size)   *size = d->size;
            if(offset) *offset = d->offset;

            d->used = 0;
            return ARCHIVE_OK;
        }
    }

    return ARCHIVE_RETRY;
}

/* Pushes the `buf`, `size` and `offset` arguments to the rar->cstate.dready
 * FIFO stack. Those values will be popped from this stack by the `use_data`
 * function. */
static int push_data_ready(struct archive_read* a, struct rar5* rar,
        const uint8_t* buf, size_t size, int64_t offset)
{
    int i;

    /* Don't push if we're in skip mode. This is needed because solid
     * streams need full processing even if we're skipping data. After fully
     * processing the stream, we need to discard the generated bytes, because
     * we're interested only in the side effect: building up the internal
     * window circular buffer. This window buffer will be used later during
     * unpacking of requested data. */
    if(rar->skip_mode)
        return ARCHIVE_OK;

    /* Sanity check. */
    if(offset != rar->file.last_offset + rar->file.last_size) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER, "Sanity "
                "check error: output stream is not continuous");
        return ARCHIVE_FATAL;
    }

    for(i = 0; i < rar5_countof(rar->cstate.dready); i++) {
        struct data_ready* d = &rar->cstate.dready[i];
        if(!d->used) {
            d->used = 1;
            d->buf = buf;
            d->size = size;
            d->offset = offset;

            /* These fields are used only in sanity checking. */
            rar->file.last_offset = offset;
            rar->file.last_size = size;

            /* Calculate the checksum of this new block before submitting
             * data to libarchive's engine. */
            update_crc(rar, d->buf, d->size);

            return ARCHIVE_OK;
        }
    }

    /* Program counter will reach this code if the `rar->cstate.data_ready`
     * stack will be filled up so that no new entries will be allowed. The
     * code shouldn't allow such situation to occur. So we treat this case
     * as an internal error. */

    archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER, "Error: "
            "premature end of data_ready stack");
    return ARCHIVE_FATAL;
}

/* This function uncompresses the data that is stored in the <FILE> base
 * block.
 *
 * The FILE base block looks like this:
 *
 * <header><huffman tables><block_1><block_2>...<block_n>
 *
 * The <header> is a block header, that is parsed in parse_block_header().
 * It's a "compressed_block_header" structure, containing metadata needed
 * to know when we should stop looking for more <block_n> blocks.
 *
 * <huffman tables> contain data needed to set up the huffman tables, needed
 * for the actual decompression.
 *
 * Each <block_n> consists of series of literals:
 *
 * <literal><literal><literal>...<literal>
 *
 * Those literals generate the uncompression data. They operate on a circular
 * buffer, sometimes writing raw data into it, sometimes referencing
 * some previous data inside this buffer, and sometimes declaring a filter
 * that will need to be executed on the data stored in the circular buffer.
 * It all depends on the literal that is used.
 *
 * Sometimes blocks produce output data, sometimes they don't. For example, for
 * some huge files that use lots of filters, sometimes a block is filled with
 * only filter declaration literals. Such blocks won't produce any data in the
 * circular buffer.
 *
 * Sometimes blocks will produce 4 bytes of data, and sometimes 1 megabyte,
 * because a literal can reference previously decompressed data. For example,
 * there can be a literal that says: 'append a byte 0xFE here', and after
 * it another literal can say 'append 1 megabyte of data from circular buffer
 * offset 0x12345'. This is how RAR format handles compressing repeated
 * patterns.
 *
 * The RAR compressor creates those literals and the actual efficiency of
 * compression depends on what those literals are. The literals can also
 * be seen as a kind of a non-turing-complete virtual machine that simply
 * tells the decompressor what it should do.
 * */

static int do_uncompress_file(struct archive_read* a) {
    struct rar5* rar = get_context(a);
    int ret;
    int64_t max_end_pos;

    if(!rar->cstate.initialized) {
        /* Don't perform full context reinitialization if we're processing
         * a solid archive. */
        if(!rar->main.solid || !rar->cstate.window_buf) {
            init_unpack(rar);
        }

        rar->cstate.initialized = 1;
    }

    if(rar->cstate.all_filters_applied == 1) {
        /* We use while(1) here, but standard case allows for just 1 iteration.
         * The loop will iterate if process_block() didn't generate any data at
         * all. This can happen if the block contains only filter definitions
         * (this is common in big files). */

        while(1) {
            ret = process_block(a);
            if(ret == ARCHIVE_EOF || ret == ARCHIVE_FATAL)
                return ret;

            if(rar->cstate.last_write_ptr == rar->cstate.write_ptr) {
                /* The block didn't generate any new data, so just process
                 * a new block. */
                continue;
            }

            /* The block has generated some new data, so break the loop. */
            break;
        }
    }

    /* Try to run filters. If filters won't be applied, it means that
     * insufficient data was generated. */
    ret = apply_filters(a);
    if(ret == ARCHIVE_RETRY) {
        return ARCHIVE_OK;
    } else if(ret == ARCHIVE_FATAL) {
        return ARCHIVE_FATAL;
    }

    /* If apply_filters() will return ARCHIVE_OK, we can continue here. */

    if(cdeque_size(&rar->cstate.filters) > 0) {
        /* Check if we can write something before hitting first filter. */
        struct filter_info* flt;

        /* Get the block_start offset from the first filter. */
        if(CDE_OK != cdeque_front(&rar->cstate.filters, cdeque_filter_p(&flt)))
        {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                    "Can't read first filter");
            return ARCHIVE_FATAL;
        }

        max_end_pos = rar5_min(flt->block_start, rar->cstate.write_ptr);
    } else {
        /* There are no filters defined, or all filters were applied. This
         * means we can just store the data without any postprocessing. */
        max_end_pos = rar->cstate.write_ptr;
    }

    if(max_end_pos == rar->cstate.last_write_ptr) {
        /* We can't write anything yet. The block uncompression function did
         * not generate enough data, and no filter can be applied. At the same
         * time we don't have any data that can be stored without filter
         * postprocessing. This means we need to wait for more data to be
         * generated, so we can apply the filters.
         *
         * Signal the caller that we need more data to be able to do anything.
         */
        return ARCHIVE_RETRY;
    } else {
        /* We can write the data before hitting the first filter. So let's
         * do it. The push_window_data() function will effectively return
         * the selected data block to the user application. */
        push_window_data(a, rar, rar->cstate.last_write_ptr, max_end_pos);
        rar->cstate.last_write_ptr = max_end_pos;
    }

    return ARCHIVE_OK;
}

static int uncompress_file(struct archive_read* a) {
    int ret;

    while(1) {
        /* Sometimes the uncompression function will return a 'retry' signal.
         * If this will happen, we have to retry the function. */
        ret = do_uncompress_file(a);
        if(ret != ARCHIVE_RETRY)
            return ret;
    }
}


static int do_unstore_file(struct archive_read* a,
                           struct rar5* rar,
                           const void** buf,
                           size_t* size,
                           int64_t* offset)
{
    const uint8_t* p;

    if(rar->file.bytes_remaining == 0 && rar->main.volume > 0 &&
            rar->generic.split_after > 0)
    {
        int ret;

        rar->cstate.switch_multivolume = 1;
        ret = advance_multivolume(a);
        rar->cstate.switch_multivolume = 0;

        if(ret != ARCHIVE_OK) {
            /* Failed to advance to next multivolume archive file. */
            return ret;
        }
    }

    size_t to_read = rar5_min(rar->file.bytes_remaining, 64 * 1024);
    if(to_read == 0) {
        return ARCHIVE_EOF;
    }

    if(!read_ahead(a, to_read, &p)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT, "I/O error "
                "when unstoring file");
        return ARCHIVE_FATAL;
    }

    if(ARCHIVE_OK != consume(a, to_read)) {
        return ARCHIVE_EOF;
    }

    if(buf)    *buf = p;
    if(size)   *size = to_read;
    if(offset) *offset = rar->cstate.last_unstore_ptr;

    rar->file.bytes_remaining -= to_read;
    rar->cstate.last_unstore_ptr += to_read;

    update_crc(rar, p, to_read);
    return ARCHIVE_OK;
}

static int do_unpack(struct archive_read* a, struct rar5* rar,
        const void** buf, size_t* size, int64_t* offset)
{
    enum COMPRESSION_METHOD {
        STORE = 0, FASTEST = 1, FAST = 2, NORMAL = 3, GOOD = 4, BEST = 5
    };

    if(rar->file.service > 0) {
        return do_unstore_file(a, rar, buf, size, offset);
    } else {
        switch(rar->cstate.method) {
            case STORE:
                return do_unstore_file(a, rar, buf, size, offset);
            case FASTEST:
                /* fallthrough */
            case FAST:
                /* fallthrough */
            case NORMAL:
                /* fallthrough */
            case GOOD:
                /* fallthrough */
            case BEST:
                return uncompress_file(a);
            default:
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Compression method not supported: 0x%08x",
                        rar->cstate.method);

                return ARCHIVE_FATAL;
        }
    }

#if !defined WIN32
    /* Not reached. */
    return ARCHIVE_OK;
#endif
}

static int verify_checksums(struct archive_read* a) {
    int verify_crc;
    struct rar5* rar = get_context(a);

    /* Check checksums only when actually unpacking the data. There's no need
     * to calculate checksum when we're skipping data in solid archives
     * (skipping in solid archives is the same thing as unpacking compressed
     * data and discarding the result). */

    if(!rar->skip_mode) {
        /* Always check checksums if we're not in skip mode */
        verify_crc = 1;
    } else {
        /* We can override the logic above with a compile-time option
         * NO_CRC_ON_SOLID_SKIP. This option is used during debugging, and it
         * will check checksums of unpacked data even when we're skipping it.
         */

#if defined CHECK_CRC_ON_SOLID_SKIP
        /* Debug case */
        verify_crc = 1;
#else
        /* Normal case */
        verify_crc = 0;
#endif
    }

    if(verify_crc) {
        /* During unpacking, on each unpacked block we're calling the
         * update_crc() function. Since we are here, the unpacking process is
         * already over and we can check if calculated checksum (CRC32 or
         * BLAKE2sp) is the same as what is stored in the archive.
         */
        if(rar->file.stored_crc32 > 0) {
            /* Check CRC32 only when the file contains a CRC32 value for this
             * file. */

            if(rar->file.calculated_crc32 != rar->file.stored_crc32) {
                /* Checksums do not match; the unpacked file is corrupted. */

                DEBUG_CODE {
                    printf("Checksum error: CRC32 (was: %08x, expected: %08x)\n",
                        rar->file.calculated_crc32, rar->file.stored_crc32);
                }

#ifndef DONT_FAIL_ON_CRC_ERROR
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                  "Checksum error: CRC32");
                return ARCHIVE_FATAL;
#endif
            } else {
                DEBUG_CODE {
                    printf("Checksum OK: CRC32 (%08x/%08x)\n",
                        rar->file.stored_crc32,
                        rar->file.calculated_crc32);
                }
            }
        }

        if(rar->file.has_blake2 > 0) {
            /* BLAKE2sp is an optional checksum algorithm that is added to
             * RARv5 archives when using the `-htb` switch during creation of
             * archive.
             *
             * We now finalize the hash calculation by calling the `final`
             * function. This will generate the final hash value we can use to
             * compare it with the BLAKE2sp checksum that is stored in the
             * archive.
             *
             * The return value of this `final` function is not very helpful,
             * as it guards only against improper use. This is why we're
             * explicitly ignoring it. */

            uint8_t b2_buf[32];
            (void) blake2sp_final(&rar->file.b2state, b2_buf, 32);

            if(memcmp(&rar->file.blake2sp, b2_buf, 32) != 0) {
#ifndef DONT_FAIL_ON_CRC_ERROR
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                  "Checksum error: BLAKE2");

                return ARCHIVE_FATAL;
#endif
            }
        }
    }

    /* Finalization for this file has been successfully completed. */
    return ARCHIVE_OK;
}

static int verify_global_checksums(struct archive_read* a) {
    return verify_checksums(a);
}

static int rar5_read_data(struct archive_read *a, const void **buff,
                                  size_t *size, int64_t *offset) {
    int ret;
    struct rar5* rar = get_context(a);

    if(!rar->skip_mode && (rar->cstate.last_write_ptr > rar->file.unpacked_size)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
                "Unpacker has written too many bytes");
        return ARCHIVE_FATAL;
    }

    ret = use_data(rar, buff, size, offset);
    if(ret == ARCHIVE_OK) {
        return ret;
    }

    if(rar->file.eof == 1) {
        return ARCHIVE_EOF;
    }

    ret = do_unpack(a, rar, buff, size, offset);
    if(ret != ARCHIVE_OK) {
        return ret;
    }

    if(rar->file.bytes_remaining == 0 &&
            rar->cstate.last_write_ptr == rar->file.unpacked_size)
    {
        /* If all bytes of current file were processed, run finalization.
         *
         * Finalization will check checksum against proper values. If
         * some of the checksums will not match, we'll return an error
         * value in the last `archive_read_data` call to signal an error
         * to the user. */

        rar->file.eof = 1;
        return verify_global_checksums(a);
    }

    return ARCHIVE_OK;
}

static int rar5_read_data_skip(struct archive_read *a) {
    struct rar5* rar = get_context(a);

    if(rar->main.solid) {
        /* In solid archives, instead of skipping the data, we need to extract
         * it, and dispose the result. The side effect of this operation will
         * be setting up the initial window buffer state needed to be able to
         * extract the selected file. */

        int ret;

        /* Make sure to process all blocks in the compressed stream. */
        while(rar->file.bytes_remaining > 0) {
            /* Setting the "skip mode" will allow us to skip checksum checks
             * during data skipping. Checking the checksum of skipped data
             * isn't really necessary and it's only slowing things down.
             *
             * This is incremented instead of setting to 1 because this data
             * skipping function can be called recursively. */
            rar->skip_mode++;

            /* We're disposing 1 block of data, so we use triple NULLs in
             * arguments.
             */
            ret = rar5_read_data(a, NULL, NULL, NULL);

            /* Turn off "skip mode". */
            rar->skip_mode--;

            if(ret < 0) {
                /* Propagate any potential error conditions to the caller. */
                return ret;
            }
        }
    } else {
        /* In standard archives, we can just jump over the compressed stream.
         * Each file in non-solid archives starts from an empty window buffer.
         */

        if(ARCHIVE_OK != consume(a, rar->file.bytes_remaining)) {
            return ARCHIVE_FATAL;
        }

        rar->file.bytes_remaining = 0;
    }

    return ARCHIVE_OK;
}

static int64_t rar5_seek_data(struct archive_read *a, int64_t offset,
        int whence)
{
    (void) a;
    (void) offset;
    (void) whence;

    /* We're a streaming unpacker, and we don't support seeking. */

    return ARCHIVE_FATAL;
}

static int rar5_cleanup(struct archive_read *a) {
    struct rar5* rar = get_context(a);

    free(rar->cstate.window_buf);

    free(rar->cstate.filtered_buf);

    free(rar->vol.push_buf);

    free_filters(rar);
    cdeque_free(&rar->cstate.filters);

    free(rar);
    a->format->data = NULL;

    return ARCHIVE_OK;
}

static int rar5_capabilities(struct archive_read * a) {
    (void) a;
    return 0;
}

static int rar5_has_encrypted_entries(struct archive_read *_a) {
    (void) _a;

    /* Unsupported for now. */
    return ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED;
}

static int rar5_init(struct rar5* rar) {
    ssize_t i;

    memset(rar, 0, sizeof(struct rar5));

    /* Decrypt the magic signature pattern. Check the comment near the
     * `rar5_signature` symbol to read the rationale behind this. */

    if(rar5_signature[0] == 243) {
        for(i = 0; i < rar5_signature_size; i++) {
            rar5_signature[i] ^= 0xA1;
        }
    }

    if(CDE_OK != cdeque_init(&rar->cstate.filters, 8192))
        return ARCHIVE_FATAL;

    return ARCHIVE_OK;
}

int archive_read_support_format_rar5(struct archive *_a) {
    struct archive_read* ar;
    int ret;
    struct rar5* rar;

    if(ARCHIVE_OK != (ret = get_archive_read(_a, &ar)))
        return ret;

    rar = malloc(sizeof(*rar));
    if(rar == NULL) {
        archive_set_error(&ar->archive, ENOMEM, "Can't allocate rar5 data");
        return ARCHIVE_FATAL;
    }

    if(ARCHIVE_OK != rar5_init(rar)) {
        archive_set_error(&ar->archive, ENOMEM, "Can't allocate rar5 filter "
                "buffer");
        return ARCHIVE_FATAL;
    }

    ret = __archive_read_register_format(ar,
                                         rar,
                                         "rar5",
                                         rar5_bid,
                                         rar5_options,
                                         rar5_read_header,
                                         rar5_read_data,
                                         rar5_read_data_skip,
                                         rar5_seek_data,
                                         rar5_cleanup,
                                         rar5_capabilities,
                                         rar5_has_encrypted_entries);

    if(ret != ARCHIVE_OK) {
        (void) rar5_cleanup(ar);
    }

    return ret;
}
