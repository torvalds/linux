/*
 * Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_PACKET_LOCL_H
# define HEADER_PACKET_LOCL_H

# include <string.h>
# include <openssl/bn.h>
# include <openssl/buffer.h>
# include <openssl/crypto.h>
# include <openssl/e_os2.h>

# include "internal/numbers.h"

typedef struct {
    /* Pointer to where we are currently reading from */
    const unsigned char *curr;
    /* Number of bytes remaining */
    size_t remaining;
} PACKET;

/* Internal unchecked shorthand; don't use outside this file. */
static ossl_inline void packet_forward(PACKET *pkt, size_t len)
{
    pkt->curr += len;
    pkt->remaining -= len;
}

/*
 * Returns the number of bytes remaining to be read in the PACKET
 */
static ossl_inline size_t PACKET_remaining(const PACKET *pkt)
{
    return pkt->remaining;
}

/*
 * Returns a pointer to the first byte after the packet data.
 * Useful for integrating with non-PACKET parsing code.
 * Specifically, we use PACKET_end() to verify that a d2i_... call
 * has consumed the entire packet contents.
 */
static ossl_inline const unsigned char *PACKET_end(const PACKET *pkt)
{
    return pkt->curr + pkt->remaining;
}

/*
 * Returns a pointer to the PACKET's current position.
 * For use in non-PACKETized APIs.
 */
static ossl_inline const unsigned char *PACKET_data(const PACKET *pkt)
{
    return pkt->curr;
}

/*
 * Initialise a PACKET with |len| bytes held in |buf|. This does not make a
 * copy of the data so |buf| must be present for the whole time that the PACKET
 * is being used.
 */
__owur static ossl_inline int PACKET_buf_init(PACKET *pkt,
                                              const unsigned char *buf,
                                              size_t len)
{
    /* Sanity check for negative values. */
    if (len > (size_t)(SIZE_MAX / 2))
        return 0;

    pkt->curr = buf;
    pkt->remaining = len;
    return 1;
}

/* Initialize a PACKET to hold zero bytes. */
static ossl_inline void PACKET_null_init(PACKET *pkt)
{
    pkt->curr = NULL;
    pkt->remaining = 0;
}

/*
 * Returns 1 if the packet has length |num| and its contents equal the |num|
 * bytes read from |ptr|. Returns 0 otherwise (lengths or contents not equal).
 * If lengths are equal, performs the comparison in constant time.
 */
__owur static ossl_inline int PACKET_equal(const PACKET *pkt, const void *ptr,
                                           size_t num)
{
    if (PACKET_remaining(pkt) != num)
        return 0;
    return CRYPTO_memcmp(pkt->curr, ptr, num) == 0;
}

/*
 * Peek ahead and initialize |subpkt| with the next |len| bytes read from |pkt|.
 * Data is not copied: the |subpkt| packet will share its underlying buffer with
 * the original |pkt|, so data wrapped by |pkt| must outlive the |subpkt|.
 */
__owur static ossl_inline int PACKET_peek_sub_packet(const PACKET *pkt,
                                                     PACKET *subpkt, size_t len)
{
    if (PACKET_remaining(pkt) < len)
        return 0;

    return PACKET_buf_init(subpkt, pkt->curr, len);
}

/*
 * Initialize |subpkt| with the next |len| bytes read from |pkt|. Data is not
 * copied: the |subpkt| packet will share its underlying buffer with the
 * original |pkt|, so data wrapped by |pkt| must outlive the |subpkt|.
 */
__owur static ossl_inline int PACKET_get_sub_packet(PACKET *pkt,
                                                    PACKET *subpkt, size_t len)
{
    if (!PACKET_peek_sub_packet(pkt, subpkt, len))
        return 0;

    packet_forward(pkt, len);

    return 1;
}

/*
 * Peek ahead at 2 bytes in network order from |pkt| and store the value in
 * |*data|
 */
__owur static ossl_inline int PACKET_peek_net_2(const PACKET *pkt,
                                                unsigned int *data)
{
    if (PACKET_remaining(pkt) < 2)
        return 0;

    *data = ((unsigned int)(*pkt->curr)) << 8;
    *data |= *(pkt->curr + 1);

    return 1;
}

/* Equivalent of n2s */
/* Get 2 bytes in network order from |pkt| and store the value in |*data| */
__owur static ossl_inline int PACKET_get_net_2(PACKET *pkt, unsigned int *data)
{
    if (!PACKET_peek_net_2(pkt, data))
        return 0;

    packet_forward(pkt, 2);

    return 1;
}

/* Same as PACKET_get_net_2() but for a size_t */
__owur static ossl_inline int PACKET_get_net_2_len(PACKET *pkt, size_t *data)
{
    unsigned int i;
    int ret = PACKET_get_net_2(pkt, &i);

    if (ret)
        *data = (size_t)i;

    return ret;
}

/*
 * Peek ahead at 3 bytes in network order from |pkt| and store the value in
 * |*data|
 */
__owur static ossl_inline int PACKET_peek_net_3(const PACKET *pkt,
                                                unsigned long *data)
{
    if (PACKET_remaining(pkt) < 3)
        return 0;

    *data = ((unsigned long)(*pkt->curr)) << 16;
    *data |= ((unsigned long)(*(pkt->curr + 1))) << 8;
    *data |= *(pkt->curr + 2);

    return 1;
}

/* Equivalent of n2l3 */
/* Get 3 bytes in network order from |pkt| and store the value in |*data| */
__owur static ossl_inline int PACKET_get_net_3(PACKET *pkt, unsigned long *data)
{
    if (!PACKET_peek_net_3(pkt, data))
        return 0;

    packet_forward(pkt, 3);

    return 1;
}

/* Same as PACKET_get_net_3() but for a size_t */
__owur static ossl_inline int PACKET_get_net_3_len(PACKET *pkt, size_t *data)
{
    unsigned long i;
    int ret = PACKET_get_net_3(pkt, &i);

    if (ret)
        *data = (size_t)i;

    return ret;
}

/*
 * Peek ahead at 4 bytes in network order from |pkt| and store the value in
 * |*data|
 */
__owur static ossl_inline int PACKET_peek_net_4(const PACKET *pkt,
                                                unsigned long *data)
{
    if (PACKET_remaining(pkt) < 4)
        return 0;

    *data = ((unsigned long)(*pkt->curr)) << 24;
    *data |= ((unsigned long)(*(pkt->curr + 1))) << 16;
    *data |= ((unsigned long)(*(pkt->curr + 2))) << 8;
    *data |= *(pkt->curr + 3);

    return 1;
}

/* Equivalent of n2l */
/* Get 4 bytes in network order from |pkt| and store the value in |*data| */
__owur static ossl_inline int PACKET_get_net_4(PACKET *pkt, unsigned long *data)
{
    if (!PACKET_peek_net_4(pkt, data))
        return 0;

    packet_forward(pkt, 4);

    return 1;
}

/* Same as PACKET_get_net_4() but for a size_t */
__owur static ossl_inline int PACKET_get_net_4_len(PACKET *pkt, size_t *data)
{
    unsigned long i;
    int ret = PACKET_get_net_4(pkt, &i);

    if (ret)
        *data = (size_t)i;

    return ret;
}

/* Peek ahead at 1 byte from |pkt| and store the value in |*data| */
__owur static ossl_inline int PACKET_peek_1(const PACKET *pkt,
                                            unsigned int *data)
{
    if (!PACKET_remaining(pkt))
        return 0;

    *data = *pkt->curr;

    return 1;
}

/* Get 1 byte from |pkt| and store the value in |*data| */
__owur static ossl_inline int PACKET_get_1(PACKET *pkt, unsigned int *data)
{
    if (!PACKET_peek_1(pkt, data))
        return 0;

    packet_forward(pkt, 1);

    return 1;
}

/* Same as PACKET_get_1() but for a size_t */
__owur static ossl_inline int PACKET_get_1_len(PACKET *pkt, size_t *data)
{
    unsigned int i;
    int ret = PACKET_get_1(pkt, &i);

    if (ret)
        *data = (size_t)i;

    return ret;
}

/*
 * Peek ahead at 4 bytes in reverse network order from |pkt| and store the value
 * in |*data|
 */
__owur static ossl_inline int PACKET_peek_4(const PACKET *pkt,
                                            unsigned long *data)
{
    if (PACKET_remaining(pkt) < 4)
        return 0;

    *data = *pkt->curr;
    *data |= ((unsigned long)(*(pkt->curr + 1))) << 8;
    *data |= ((unsigned long)(*(pkt->curr + 2))) << 16;
    *data |= ((unsigned long)(*(pkt->curr + 3))) << 24;

    return 1;
}

/* Equivalent of c2l */
/*
 * Get 4 bytes in reverse network order from |pkt| and store the value in
 * |*data|
 */
__owur static ossl_inline int PACKET_get_4(PACKET *pkt, unsigned long *data)
{
    if (!PACKET_peek_4(pkt, data))
        return 0;

    packet_forward(pkt, 4);

    return 1;
}

/*
 * Peek ahead at |len| bytes from the |pkt| and store a pointer to them in
 * |*data|. This just points at the underlying buffer that |pkt| is using. The
 * caller should not free this data directly (it will be freed when the
 * underlying buffer gets freed
 */
__owur static ossl_inline int PACKET_peek_bytes(const PACKET *pkt,
                                                const unsigned char **data,
                                                size_t len)
{
    if (PACKET_remaining(pkt) < len)
        return 0;

    *data = pkt->curr;

    return 1;
}

/*
 * Read |len| bytes from the |pkt| and store a pointer to them in |*data|. This
 * just points at the underlying buffer that |pkt| is using. The caller should
 * not free this data directly (it will be freed when the underlying buffer gets
 * freed
 */
__owur static ossl_inline int PACKET_get_bytes(PACKET *pkt,
                                               const unsigned char **data,
                                               size_t len)
{
    if (!PACKET_peek_bytes(pkt, data, len))
        return 0;

    packet_forward(pkt, len);

    return 1;
}

/* Peek ahead at |len| bytes from |pkt| and copy them to |data| */
__owur static ossl_inline int PACKET_peek_copy_bytes(const PACKET *pkt,
                                                     unsigned char *data,
                                                     size_t len)
{
    if (PACKET_remaining(pkt) < len)
        return 0;

    memcpy(data, pkt->curr, len);

    return 1;
}

/*
 * Read |len| bytes from |pkt| and copy them to |data|.
 * The caller is responsible for ensuring that |data| can hold |len| bytes.
 */
__owur static ossl_inline int PACKET_copy_bytes(PACKET *pkt,
                                                unsigned char *data, size_t len)
{
    if (!PACKET_peek_copy_bytes(pkt, data, len))
        return 0;

    packet_forward(pkt, len);

    return 1;
}

/*
 * Copy packet data to |dest|, and set |len| to the number of copied bytes.
 * If the packet has more than |dest_len| bytes, nothing is copied.
 * Returns 1 if the packet data fits in |dest_len| bytes, 0 otherwise.
 * Does not forward PACKET position (because it is typically the last thing
 * done with a given PACKET).
 */
__owur static ossl_inline int PACKET_copy_all(const PACKET *pkt,
                                              unsigned char *dest,
                                              size_t dest_len, size_t *len)
{
    if (PACKET_remaining(pkt) > dest_len) {
        *len = 0;
        return 0;
    }
    *len = pkt->remaining;
    memcpy(dest, pkt->curr, pkt->remaining);
    return 1;
}

/*
 * Copy |pkt| bytes to a newly allocated buffer and store a pointer to the
 * result in |*data|, and the length in |len|.
 * If |*data| is not NULL, the old data is OPENSSL_free'd.
 * If the packet is empty, or malloc fails, |*data| will be set to NULL.
 * Returns 1 if the malloc succeeds and 0 otherwise.
 * Does not forward PACKET position (because it is typically the last thing
 * done with a given PACKET).
 */
__owur static ossl_inline int PACKET_memdup(const PACKET *pkt,
                                            unsigned char **data, size_t *len)
{
    size_t length;

    OPENSSL_free(*data);
    *data = NULL;
    *len = 0;

    length = PACKET_remaining(pkt);

    if (length == 0)
        return 1;

    *data = OPENSSL_memdup(pkt->curr, length);
    if (*data == NULL)
        return 0;

    *len = length;
    return 1;
}

/*
 * Read a C string from |pkt| and copy to a newly allocated, NUL-terminated
 * buffer. Store a pointer to the result in |*data|.
 * If |*data| is not NULL, the old data is OPENSSL_free'd.
 * If the data in |pkt| does not contain a NUL-byte, the entire data is
 * copied and NUL-terminated.
 * Returns 1 if the malloc succeeds and 0 otherwise.
 * Does not forward PACKET position (because it is typically the last thing done
 * with a given PACKET).
 */
__owur static ossl_inline int PACKET_strndup(const PACKET *pkt, char **data)
{
    OPENSSL_free(*data);

    /* This will succeed on an empty packet, unless pkt->curr == NULL. */
    *data = OPENSSL_strndup((const char *)pkt->curr, PACKET_remaining(pkt));
    return (*data != NULL);
}

/* Returns 1 if |pkt| contains at least one 0-byte, 0 otherwise. */
static ossl_inline int PACKET_contains_zero_byte(const PACKET *pkt)
{
    return memchr(pkt->curr, 0, pkt->remaining) != NULL;
}

/* Move the current reading position forward |len| bytes */
__owur static ossl_inline int PACKET_forward(PACKET *pkt, size_t len)
{
    if (PACKET_remaining(pkt) < len)
        return 0;

    packet_forward(pkt, len);

    return 1;
}

/*
 * Reads a variable-length vector prefixed with a one-byte length, and stores
 * the contents in |subpkt|. |pkt| can equal |subpkt|.
 * Data is not copied: the |subpkt| packet will share its underlying buffer with
 * the original |pkt|, so data wrapped by |pkt| must outlive the |subpkt|.
 * Upon failure, the original |pkt| and |subpkt| are not modified.
 */
__owur static ossl_inline int PACKET_get_length_prefixed_1(PACKET *pkt,
                                                           PACKET *subpkt)
{
    unsigned int length;
    const unsigned char *data;
    PACKET tmp = *pkt;
    if (!PACKET_get_1(&tmp, &length) ||
        !PACKET_get_bytes(&tmp, &data, (size_t)length)) {
        return 0;
    }

    *pkt = tmp;
    subpkt->curr = data;
    subpkt->remaining = length;

    return 1;
}

/*
 * Like PACKET_get_length_prefixed_1, but additionally, fails when there are
 * leftover bytes in |pkt|.
 */
__owur static ossl_inline int PACKET_as_length_prefixed_1(PACKET *pkt,
                                                          PACKET *subpkt)
{
    unsigned int length;
    const unsigned char *data;
    PACKET tmp = *pkt;
    if (!PACKET_get_1(&tmp, &length) ||
        !PACKET_get_bytes(&tmp, &data, (size_t)length) ||
        PACKET_remaining(&tmp) != 0) {
        return 0;
    }

    *pkt = tmp;
    subpkt->curr = data;
    subpkt->remaining = length;

    return 1;
}

/*
 * Reads a variable-length vector prefixed with a two-byte length, and stores
 * the contents in |subpkt|. |pkt| can equal |subpkt|.
 * Data is not copied: the |subpkt| packet will share its underlying buffer with
 * the original |pkt|, so data wrapped by |pkt| must outlive the |subpkt|.
 * Upon failure, the original |pkt| and |subpkt| are not modified.
 */
__owur static ossl_inline int PACKET_get_length_prefixed_2(PACKET *pkt,
                                                           PACKET *subpkt)
{
    unsigned int length;
    const unsigned char *data;
    PACKET tmp = *pkt;

    if (!PACKET_get_net_2(&tmp, &length) ||
        !PACKET_get_bytes(&tmp, &data, (size_t)length)) {
        return 0;
    }

    *pkt = tmp;
    subpkt->curr = data;
    subpkt->remaining = length;

    return 1;
}

/*
 * Like PACKET_get_length_prefixed_2, but additionally, fails when there are
 * leftover bytes in |pkt|.
 */
__owur static ossl_inline int PACKET_as_length_prefixed_2(PACKET *pkt,
                                                          PACKET *subpkt)
{
    unsigned int length;
    const unsigned char *data;
    PACKET tmp = *pkt;

    if (!PACKET_get_net_2(&tmp, &length) ||
        !PACKET_get_bytes(&tmp, &data, (size_t)length) ||
        PACKET_remaining(&tmp) != 0) {
        return 0;
    }

    *pkt = tmp;
    subpkt->curr = data;
    subpkt->remaining = length;

    return 1;
}

/*
 * Reads a variable-length vector prefixed with a three-byte length, and stores
 * the contents in |subpkt|. |pkt| can equal |subpkt|.
 * Data is not copied: the |subpkt| packet will share its underlying buffer with
 * the original |pkt|, so data wrapped by |pkt| must outlive the |subpkt|.
 * Upon failure, the original |pkt| and |subpkt| are not modified.
 */
__owur static ossl_inline int PACKET_get_length_prefixed_3(PACKET *pkt,
                                                           PACKET *subpkt)
{
    unsigned long length;
    const unsigned char *data;
    PACKET tmp = *pkt;
    if (!PACKET_get_net_3(&tmp, &length) ||
        !PACKET_get_bytes(&tmp, &data, (size_t)length)) {
        return 0;
    }

    *pkt = tmp;
    subpkt->curr = data;
    subpkt->remaining = length;

    return 1;
}

/* Writeable packets */

typedef struct wpacket_sub WPACKET_SUB;
struct wpacket_sub {
    /* The parent WPACKET_SUB if we have one or NULL otherwise */
    WPACKET_SUB *parent;

    /*
     * Offset into the buffer where the length of this WPACKET goes. We use an
     * offset in case the buffer grows and gets reallocated.
     */
    size_t packet_len;

    /* Number of bytes in the packet_len or 0 if we don't write the length */
    size_t lenbytes;

    /* Number of bytes written to the buf prior to this packet starting */
    size_t pwritten;

    /* Flags for this sub-packet */
    unsigned int flags;
};

typedef struct wpacket_st WPACKET;
struct wpacket_st {
    /* The buffer where we store the output data */
    BUF_MEM *buf;

    /* Fixed sized buffer which can be used as an alternative to buf */
    unsigned char *staticbuf;

    /*
     * Offset into the buffer where we are currently writing. We use an offset
     * in case the buffer grows and gets reallocated.
     */
    size_t curr;

    /* Number of bytes written so far */
    size_t written;

    /* Maximum number of bytes we will allow to be written to this WPACKET */
    size_t maxsize;

    /* Our sub-packets (always at least one if not finished) */
    WPACKET_SUB *subs;
};

/* Flags */

/* Default */
#define WPACKET_FLAGS_NONE                      0

/* Error on WPACKET_close() if no data written to the WPACKET */
#define WPACKET_FLAGS_NON_ZERO_LENGTH           1

/*
 * Abandon all changes on WPACKET_close() if no data written to the WPACKET,
 * i.e. this does not write out a zero packet length
 */
#define WPACKET_FLAGS_ABANDON_ON_ZERO_LENGTH    2


/*
 * Initialise a WPACKET with the buffer in |buf|. The buffer must exist
 * for the whole time that the WPACKET is being used. Additionally |lenbytes| of
 * data is preallocated at the start of the buffer to store the length of the
 * WPACKET once we know it.
 */
int WPACKET_init_len(WPACKET *pkt, BUF_MEM *buf, size_t lenbytes);

/*
 * Same as WPACKET_init_len except there is no preallocation of the WPACKET
 * length.
 */
int WPACKET_init(WPACKET *pkt, BUF_MEM *buf);

/*
 * Same as WPACKET_init_len except we do not use a growable BUF_MEM structure.
 * A fixed buffer of memory |buf| of size |len| is used instead. A failure will
 * occur if you attempt to write beyond the end of the buffer
 */
int WPACKET_init_static_len(WPACKET *pkt, unsigned char *buf, size_t len,
                            size_t lenbytes);
/*
 * Set the flags to be applied to the current sub-packet
 */
int WPACKET_set_flags(WPACKET *pkt, unsigned int flags);

/*
 * Closes the most recent sub-packet. It also writes out the length of the
 * packet to the required location (normally the start of the WPACKET) if
 * appropriate. The top level WPACKET should be closed using WPACKET_finish()
 * instead of this function.
 */
int WPACKET_close(WPACKET *pkt);

/*
 * The same as WPACKET_close() but only for the top most WPACKET. Additionally
 * frees memory resources for this WPACKET.
 */
int WPACKET_finish(WPACKET *pkt);

/*
 * Iterate through all the sub-packets and write out their lengths as if they
 * were being closed. The lengths will be overwritten with the final lengths
 * when the sub-packets are eventually closed (which may be different if more
 * data is added to the WPACKET). This function fails if a sub-packet is of 0
 * length and WPACKET_FLAGS_ABANDON_ON_ZERO_LENGTH is set.
 */
int WPACKET_fill_lengths(WPACKET *pkt);

/*
 * Initialise a new sub-packet. Additionally |lenbytes| of data is preallocated
 * at the start of the sub-packet to store its length once we know it. Don't
 * call this directly. Use the convenience macros below instead.
 */
int WPACKET_start_sub_packet_len__(WPACKET *pkt, size_t lenbytes);

/*
 * Convenience macros for calling WPACKET_start_sub_packet_len with different
 * lengths
 */
#define WPACKET_start_sub_packet_u8(pkt) \
    WPACKET_start_sub_packet_len__((pkt), 1)
#define WPACKET_start_sub_packet_u16(pkt) \
    WPACKET_start_sub_packet_len__((pkt), 2)
#define WPACKET_start_sub_packet_u24(pkt) \
    WPACKET_start_sub_packet_len__((pkt), 3)
#define WPACKET_start_sub_packet_u32(pkt) \
    WPACKET_start_sub_packet_len__((pkt), 4)

/*
 * Same as WPACKET_start_sub_packet_len__() except no bytes are pre-allocated
 * for the sub-packet length.
 */
int WPACKET_start_sub_packet(WPACKET *pkt);

/*
 * Allocate bytes in the WPACKET for the output. This reserves the bytes
 * and counts them as "written", but doesn't actually do the writing. A pointer
 * to the allocated bytes is stored in |*allocbytes|. |allocbytes| may be NULL.
 * WARNING: the allocated bytes must be filled in immediately, without further
 * WPACKET_* calls. If not then the underlying buffer may be realloc'd and
 * change its location.
 */
int WPACKET_allocate_bytes(WPACKET *pkt, size_t len,
                           unsigned char **allocbytes);

/*
 * The same as WPACKET_allocate_bytes() except additionally a new sub-packet is
 * started for the allocated bytes, and then closed immediately afterwards. The
 * number of length bytes for the sub-packet is in |lenbytes|. Don't call this
 * directly. Use the convenience macros below instead.
 */
int WPACKET_sub_allocate_bytes__(WPACKET *pkt, size_t len,
                                 unsigned char **allocbytes, size_t lenbytes);

/*
 * Convenience macros for calling WPACKET_sub_allocate_bytes with different
 * lengths
 */
#define WPACKET_sub_allocate_bytes_u8(pkt, len, bytes) \
    WPACKET_sub_allocate_bytes__((pkt), (len), (bytes), 1)
#define WPACKET_sub_allocate_bytes_u16(pkt, len, bytes) \
    WPACKET_sub_allocate_bytes__((pkt), (len), (bytes), 2)
#define WPACKET_sub_allocate_bytes_u24(pkt, len, bytes) \
    WPACKET_sub_allocate_bytes__((pkt), (len), (bytes), 3)
#define WPACKET_sub_allocate_bytes_u32(pkt, len, bytes) \
    WPACKET_sub_allocate_bytes__((pkt), (len), (bytes), 4)

/*
 * The same as WPACKET_allocate_bytes() except the reserved bytes are not
 * actually counted as written. Typically this will be for when we don't know
 * how big arbitrary data is going to be up front, but we do know what the
 * maximum size will be. If this function is used, then it should be immediately
 * followed by a WPACKET_allocate_bytes() call before any other WPACKET
 * functions are called (unless the write to the allocated bytes is abandoned).
 *
 * For example: If we are generating a signature, then the size of that
 * signature may not be known in advance. We can use WPACKET_reserve_bytes() to
 * handle this:
 *
 *  if (!WPACKET_sub_reserve_bytes_u16(&pkt, EVP_PKEY_size(pkey), &sigbytes1)
 *          || EVP_SignFinal(md_ctx, sigbytes1, &siglen, pkey) <= 0
 *          || !WPACKET_sub_allocate_bytes_u16(&pkt, siglen, &sigbytes2)
 *          || sigbytes1 != sigbytes2)
 *      goto err;
 */
int WPACKET_reserve_bytes(WPACKET *pkt, size_t len, unsigned char **allocbytes);

/*
 * The "reserve_bytes" equivalent of WPACKET_sub_allocate_bytes__()
 */
int WPACKET_sub_reserve_bytes__(WPACKET *pkt, size_t len,
                                 unsigned char **allocbytes, size_t lenbytes);

/*
 * Convenience macros for  WPACKET_sub_reserve_bytes with different lengths
 */
#define WPACKET_sub_reserve_bytes_u8(pkt, len, bytes) \
    WPACKET_reserve_bytes__((pkt), (len), (bytes), 1)
#define WPACKET_sub_reserve_bytes_u16(pkt, len, bytes) \
    WPACKET_sub_reserve_bytes__((pkt), (len), (bytes), 2)
#define WPACKET_sub_reserve_bytes_u24(pkt, len, bytes) \
    WPACKET_sub_reserve_bytes__((pkt), (len), (bytes), 3)
#define WPACKET_sub_reserve_bytes_u32(pkt, len, bytes) \
    WPACKET_sub_reserve_bytes__((pkt), (len), (bytes), 4)

/*
 * Write the value stored in |val| into the WPACKET. The value will consume
 * |bytes| amount of storage. An error will occur if |val| cannot be
 * accommodated in |bytes| storage, e.g. attempting to write the value 256 into
 * 1 byte will fail. Don't call this directly. Use the convenience macros below
 * instead.
 */
int WPACKET_put_bytes__(WPACKET *pkt, unsigned int val, size_t bytes);

/*
 * Convenience macros for calling WPACKET_put_bytes with different
 * lengths
 */
#define WPACKET_put_bytes_u8(pkt, val) \
    WPACKET_put_bytes__((pkt), (val), 1)
#define WPACKET_put_bytes_u16(pkt, val) \
    WPACKET_put_bytes__((pkt), (val), 2)
#define WPACKET_put_bytes_u24(pkt, val) \
    WPACKET_put_bytes__((pkt), (val), 3)
#define WPACKET_put_bytes_u32(pkt, val) \
    WPACKET_put_bytes__((pkt), (val), 4)

/* Set a maximum size that we will not allow the WPACKET to grow beyond */
int WPACKET_set_max_size(WPACKET *pkt, size_t maxsize);

/* Copy |len| bytes of data from |*src| into the WPACKET. */
int WPACKET_memcpy(WPACKET *pkt, const void *src, size_t len);

/* Set |len| bytes of data to |ch| into the WPACKET. */
int WPACKET_memset(WPACKET *pkt, int ch, size_t len);

/*
 * Copy |len| bytes of data from |*src| into the WPACKET and prefix with its
 * length (consuming |lenbytes| of data for the length). Don't call this
 * directly. Use the convenience macros below instead.
 */
int WPACKET_sub_memcpy__(WPACKET *pkt, const void *src, size_t len,
                       size_t lenbytes);

/* Convenience macros for calling WPACKET_sub_memcpy with different lengths */
#define WPACKET_sub_memcpy_u8(pkt, src, len) \
    WPACKET_sub_memcpy__((pkt), (src), (len), 1)
#define WPACKET_sub_memcpy_u16(pkt, src, len) \
    WPACKET_sub_memcpy__((pkt), (src), (len), 2)
#define WPACKET_sub_memcpy_u24(pkt, src, len) \
    WPACKET_sub_memcpy__((pkt), (src), (len), 3)
#define WPACKET_sub_memcpy_u32(pkt, src, len) \
    WPACKET_sub_memcpy__((pkt), (src), (len), 4)

/*
 * Return the total number of bytes written so far to the underlying buffer
 * including any storage allocated for length bytes
 */
int WPACKET_get_total_written(WPACKET *pkt, size_t *written);

/*
 * Returns the length of the current sub-packet. This excludes any bytes
 * allocated for the length itself.
 */
int WPACKET_get_length(WPACKET *pkt, size_t *len);

/*
 * Returns a pointer to the current write location, but does not allocate any
 * bytes.
 */
unsigned char *WPACKET_get_curr(WPACKET *pkt);

/* Release resources in a WPACKET if a failure has occurred. */
void WPACKET_cleanup(WPACKET *pkt);

#endif                          /* HEADER_PACKET_LOCL_H */
