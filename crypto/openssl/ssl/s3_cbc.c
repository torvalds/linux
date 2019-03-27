/*
 * Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/constant_time_locl.h"
#include "ssl_locl.h"
#include "internal/cryptlib.h"

#include <openssl/md5.h>
#include <openssl/sha.h>

/*
 * MAX_HASH_BIT_COUNT_BYTES is the maximum number of bytes in the hash's
 * length field. (SHA-384/512 have 128-bit length.)
 */
#define MAX_HASH_BIT_COUNT_BYTES 16

/*
 * MAX_HASH_BLOCK_SIZE is the maximum hash block size that we'll support.
 * Currently SHA-384/512 has a 128-byte block size and that's the largest
 * supported by TLS.)
 */
#define MAX_HASH_BLOCK_SIZE 128

/*
 * u32toLE serialises an unsigned, 32-bit number (n) as four bytes at (p) in
 * little-endian order. The value of p is advanced by four.
 */
#define u32toLE(n, p) \
        (*((p)++)=(unsigned char)(n), \
         *((p)++)=(unsigned char)(n>>8), \
         *((p)++)=(unsigned char)(n>>16), \
         *((p)++)=(unsigned char)(n>>24))

/*
 * These functions serialize the state of a hash and thus perform the
 * standard "final" operation without adding the padding and length that such
 * a function typically does.
 */
static void tls1_md5_final_raw(void *ctx, unsigned char *md_out)
{
    MD5_CTX *md5 = ctx;
    u32toLE(md5->A, md_out);
    u32toLE(md5->B, md_out);
    u32toLE(md5->C, md_out);
    u32toLE(md5->D, md_out);
}

static void tls1_sha1_final_raw(void *ctx, unsigned char *md_out)
{
    SHA_CTX *sha1 = ctx;
    l2n(sha1->h0, md_out);
    l2n(sha1->h1, md_out);
    l2n(sha1->h2, md_out);
    l2n(sha1->h3, md_out);
    l2n(sha1->h4, md_out);
}

static void tls1_sha256_final_raw(void *ctx, unsigned char *md_out)
{
    SHA256_CTX *sha256 = ctx;
    unsigned i;

    for (i = 0; i < 8; i++) {
        l2n(sha256->h[i], md_out);
    }
}

static void tls1_sha512_final_raw(void *ctx, unsigned char *md_out)
{
    SHA512_CTX *sha512 = ctx;
    unsigned i;

    for (i = 0; i < 8; i++) {
        l2n8(sha512->h[i], md_out);
    }
}

#undef  LARGEST_DIGEST_CTX
#define LARGEST_DIGEST_CTX SHA512_CTX

/*
 * ssl3_cbc_record_digest_supported returns 1 iff |ctx| uses a hash function
 * which ssl3_cbc_digest_record supports.
 */
char ssl3_cbc_record_digest_supported(const EVP_MD_CTX *ctx)
{
    switch (EVP_MD_CTX_type(ctx)) {
    case NID_md5:
    case NID_sha1:
    case NID_sha224:
    case NID_sha256:
    case NID_sha384:
    case NID_sha512:
        return 1;
    default:
        return 0;
    }
}

/*-
 * ssl3_cbc_digest_record computes the MAC of a decrypted, padded SSLv3/TLS
 * record.
 *
 *   ctx: the EVP_MD_CTX from which we take the hash function.
 *     ssl3_cbc_record_digest_supported must return true for this EVP_MD_CTX.
 *   md_out: the digest output. At most EVP_MAX_MD_SIZE bytes will be written.
 *   md_out_size: if non-NULL, the number of output bytes is written here.
 *   header: the 13-byte, TLS record header.
 *   data: the record data itself, less any preceding explicit IV.
 *   data_plus_mac_size: the secret, reported length of the data and MAC
 *     once the padding has been removed.
 *   data_plus_mac_plus_padding_size: the public length of the whole
 *     record, including padding.
 *   is_sslv3: non-zero if we are to use SSLv3. Otherwise, TLS.
 *
 * On entry: by virtue of having been through one of the remove_padding
 * functions, above, we know that data_plus_mac_size is large enough to contain
 * a padding byte and MAC. (If the padding was invalid, it might contain the
 * padding too. )
 * Returns 1 on success or 0 on error
 */
int ssl3_cbc_digest_record(const EVP_MD_CTX *ctx,
                           unsigned char *md_out,
                           size_t *md_out_size,
                           const unsigned char header[13],
                           const unsigned char *data,
                           size_t data_plus_mac_size,
                           size_t data_plus_mac_plus_padding_size,
                           const unsigned char *mac_secret,
                           size_t mac_secret_length, char is_sslv3)
{
    union {
        double align;
        unsigned char c[sizeof(LARGEST_DIGEST_CTX)];
    } md_state;
    void (*md_final_raw) (void *ctx, unsigned char *md_out);
    void (*md_transform) (void *ctx, const unsigned char *block);
    size_t md_size, md_block_size = 64;
    size_t sslv3_pad_length = 40, header_length, variance_blocks,
        len, max_mac_bytes, num_blocks,
        num_starting_blocks, k, mac_end_offset, c, index_a, index_b;
    size_t bits;          /* at most 18 bits */
    unsigned char length_bytes[MAX_HASH_BIT_COUNT_BYTES];
    /* hmac_pad is the masked HMAC key. */
    unsigned char hmac_pad[MAX_HASH_BLOCK_SIZE];
    unsigned char first_block[MAX_HASH_BLOCK_SIZE];
    unsigned char mac_out[EVP_MAX_MD_SIZE];
    size_t i, j;
    unsigned md_out_size_u;
    EVP_MD_CTX *md_ctx = NULL;
    /*
     * mdLengthSize is the number of bytes in the length field that
     * terminates * the hash.
     */
    size_t md_length_size = 8;
    char length_is_big_endian = 1;
    int ret;

    /*
     * This is a, hopefully redundant, check that allows us to forget about
     * many possible overflows later in this function.
     */
    if (!ossl_assert(data_plus_mac_plus_padding_size < 1024 * 1024))
        return 0;

    switch (EVP_MD_CTX_type(ctx)) {
    case NID_md5:
        if (MD5_Init((MD5_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_md5_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))MD5_Transform;
        md_size = 16;
        sslv3_pad_length = 48;
        length_is_big_endian = 0;
        break;
    case NID_sha1:
        if (SHA1_Init((SHA_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_sha1_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))SHA1_Transform;
        md_size = 20;
        break;
    case NID_sha224:
        if (SHA224_Init((SHA256_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_sha256_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))SHA256_Transform;
        md_size = 224 / 8;
        break;
    case NID_sha256:
        if (SHA256_Init((SHA256_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_sha256_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))SHA256_Transform;
        md_size = 32;
        break;
    case NID_sha384:
        if (SHA384_Init((SHA512_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_sha512_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))SHA512_Transform;
        md_size = 384 / 8;
        md_block_size = 128;
        md_length_size = 16;
        break;
    case NID_sha512:
        if (SHA512_Init((SHA512_CTX *)md_state.c) <= 0)
            return 0;
        md_final_raw = tls1_sha512_final_raw;
        md_transform =
            (void (*)(void *ctx, const unsigned char *block))SHA512_Transform;
        md_size = 64;
        md_block_size = 128;
        md_length_size = 16;
        break;
    default:
        /*
         * ssl3_cbc_record_digest_supported should have been called first to
         * check that the hash function is supported.
         */
        if (md_out_size != NULL)
            *md_out_size = 0;
        return ossl_assert(0);
    }

    if (!ossl_assert(md_length_size <= MAX_HASH_BIT_COUNT_BYTES)
            || !ossl_assert(md_block_size <= MAX_HASH_BLOCK_SIZE)
            || !ossl_assert(md_size <= EVP_MAX_MD_SIZE))
        return 0;

    header_length = 13;
    if (is_sslv3) {
        header_length = mac_secret_length + sslv3_pad_length + 8 /* sequence
                                                                  * number */  +
            1 /* record type */  +
            2 /* record length */ ;
    }

    /*
     * variance_blocks is the number of blocks of the hash that we have to
     * calculate in constant time because they could be altered by the
     * padding value. In SSLv3, the padding must be minimal so the end of
     * the plaintext varies by, at most, 15+20 = 35 bytes. (We conservatively
     * assume that the MAC size varies from 0..20 bytes.) In case the 9 bytes
     * of hash termination (0x80 + 64-bit length) don't fit in the final
     * block, we say that the final two blocks can vary based on the padding.
     * TLSv1 has MACs up to 48 bytes long (SHA-384) and the padding is not
     * required to be minimal. Therefore we say that the final |variance_blocks|
     * blocks can
     * vary based on the padding. Later in the function, if the message is
     * short and there obviously cannot be this many blocks then
     * variance_blocks can be reduced.
     */
    variance_blocks = is_sslv3 ? 2 : ( ((255 + 1 + md_size + md_block_size - 1) / md_block_size) + 1);
    /*
     * From now on we're dealing with the MAC, which conceptually has 13
     * bytes of `header' before the start of the data (TLS) or 71/75 bytes
     * (SSLv3)
     */
    len = data_plus_mac_plus_padding_size + header_length;
    /*
     * max_mac_bytes contains the maximum bytes of bytes in the MAC,
     * including * |header|, assuming that there's no padding.
     */
    max_mac_bytes = len - md_size - 1;
    /* num_blocks is the maximum number of hash blocks. */
    num_blocks =
        (max_mac_bytes + 1 + md_length_size + md_block_size -
         1) / md_block_size;
    /*
     * In order to calculate the MAC in constant time we have to handle the
     * final blocks specially because the padding value could cause the end
     * to appear somewhere in the final |variance_blocks| blocks and we can't
     * leak where. However, |num_starting_blocks| worth of data can be hashed
     * right away because no padding value can affect whether they are
     * plaintext.
     */
    num_starting_blocks = 0;
    /*
     * k is the starting byte offset into the conceptual header||data where
     * we start processing.
     */
    k = 0;
    /*
     * mac_end_offset is the index just past the end of the data to be MACed.
     */
    mac_end_offset = data_plus_mac_size + header_length - md_size;
    /*
     * c is the index of the 0x80 byte in the final hash block that contains
     * application data.
     */
    c = mac_end_offset % md_block_size;
    /*
     * index_a is the hash block number that contains the 0x80 terminating
     * value.
     */
    index_a = mac_end_offset / md_block_size;
    /*
     * index_b is the hash block number that contains the 64-bit hash length,
     * in bits.
     */
    index_b = (mac_end_offset + md_length_size) / md_block_size;
    /*
     * bits is the hash-length in bits. It includes the additional hash block
     * for the masked HMAC key, or whole of |header| in the case of SSLv3.
     */

    /*
     * For SSLv3, if we're going to have any starting blocks then we need at
     * least two because the header is larger than a single block.
     */
    if (num_blocks > variance_blocks + (is_sslv3 ? 1 : 0)) {
        num_starting_blocks = num_blocks - variance_blocks;
        k = md_block_size * num_starting_blocks;
    }

    bits = 8 * mac_end_offset;
    if (!is_sslv3) {
        /*
         * Compute the initial HMAC block. For SSLv3, the padding and secret
         * bytes are included in |header| because they take more than a
         * single block.
         */
        bits += 8 * md_block_size;
        memset(hmac_pad, 0, md_block_size);
        if (!ossl_assert(mac_secret_length <= sizeof(hmac_pad)))
            return 0;
        memcpy(hmac_pad, mac_secret, mac_secret_length);
        for (i = 0; i < md_block_size; i++)
            hmac_pad[i] ^= 0x36;

        md_transform(md_state.c, hmac_pad);
    }

    if (length_is_big_endian) {
        memset(length_bytes, 0, md_length_size - 4);
        length_bytes[md_length_size - 4] = (unsigned char)(bits >> 24);
        length_bytes[md_length_size - 3] = (unsigned char)(bits >> 16);
        length_bytes[md_length_size - 2] = (unsigned char)(bits >> 8);
        length_bytes[md_length_size - 1] = (unsigned char)bits;
    } else {
        memset(length_bytes, 0, md_length_size);
        length_bytes[md_length_size - 5] = (unsigned char)(bits >> 24);
        length_bytes[md_length_size - 6] = (unsigned char)(bits >> 16);
        length_bytes[md_length_size - 7] = (unsigned char)(bits >> 8);
        length_bytes[md_length_size - 8] = (unsigned char)bits;
    }

    if (k > 0) {
        if (is_sslv3) {
            size_t overhang;

            /*
             * The SSLv3 header is larger than a single block. overhang is
             * the number of bytes beyond a single block that the header
             * consumes: either 7 bytes (SHA1) or 11 bytes (MD5). There are no
             * ciphersuites in SSLv3 that are not SHA1 or MD5 based and
             * therefore we can be confident that the header_length will be
             * greater than |md_block_size|. However we add a sanity check just
             * in case
             */
            if (header_length <= md_block_size) {
                /* Should never happen */
                return 0;
            }
            overhang = header_length - md_block_size;
            md_transform(md_state.c, header);
            memcpy(first_block, header + md_block_size, overhang);
            memcpy(first_block + overhang, data, md_block_size - overhang);
            md_transform(md_state.c, first_block);
            for (i = 1; i < k / md_block_size - 1; i++)
                md_transform(md_state.c, data + md_block_size * i - overhang);
        } else {
            /* k is a multiple of md_block_size. */
            memcpy(first_block, header, 13);
            memcpy(first_block + 13, data, md_block_size - 13);
            md_transform(md_state.c, first_block);
            for (i = 1; i < k / md_block_size; i++)
                md_transform(md_state.c, data + md_block_size * i - 13);
        }
    }

    memset(mac_out, 0, sizeof(mac_out));

    /*
     * We now process the final hash blocks. For each block, we construct it
     * in constant time. If the |i==index_a| then we'll include the 0x80
     * bytes and zero pad etc. For each block we selectively copy it, in
     * constant time, to |mac_out|.
     */
    for (i = num_starting_blocks; i <= num_starting_blocks + variance_blocks;
         i++) {
        unsigned char block[MAX_HASH_BLOCK_SIZE];
        unsigned char is_block_a = constant_time_eq_8_s(i, index_a);
        unsigned char is_block_b = constant_time_eq_8_s(i, index_b);
        for (j = 0; j < md_block_size; j++) {
            unsigned char b = 0, is_past_c, is_past_cp1;
            if (k < header_length)
                b = header[k];
            else if (k < data_plus_mac_plus_padding_size + header_length)
                b = data[k - header_length];
            k++;

            is_past_c = is_block_a & constant_time_ge_8_s(j, c);
            is_past_cp1 = is_block_a & constant_time_ge_8_s(j, c + 1);
            /*
             * If this is the block containing the end of the application
             * data, and we are at the offset for the 0x80 value, then
             * overwrite b with 0x80.
             */
            b = constant_time_select_8(is_past_c, 0x80, b);
            /*
             * If this block contains the end of the application data
             * and we're past the 0x80 value then just write zero.
             */
            b = b & ~is_past_cp1;
            /*
             * If this is index_b (the final block), but not index_a (the end
             * of the data), then the 64-bit length didn't fit into index_a
             * and we're having to add an extra block of zeros.
             */
            b &= ~is_block_b | is_block_a;

            /*
             * The final bytes of one of the blocks contains the length.
             */
            if (j >= md_block_size - md_length_size) {
                /* If this is index_b, write a length byte. */
                b = constant_time_select_8(is_block_b,
                                           length_bytes[j -
                                                        (md_block_size -
                                                         md_length_size)], b);
            }
            block[j] = b;
        }

        md_transform(md_state.c, block);
        md_final_raw(md_state.c, block);
        /* If this is index_b, copy the hash value to |mac_out|. */
        for (j = 0; j < md_size; j++)
            mac_out[j] |= block[j] & is_block_b;
    }

    md_ctx = EVP_MD_CTX_new();
    if (md_ctx == NULL)
        goto err;
    if (EVP_DigestInit_ex(md_ctx, EVP_MD_CTX_md(ctx), NULL /* engine */ ) <= 0)
        goto err;
    if (is_sslv3) {
        /* We repurpose |hmac_pad| to contain the SSLv3 pad2 block. */
        memset(hmac_pad, 0x5c, sslv3_pad_length);

        if (EVP_DigestUpdate(md_ctx, mac_secret, mac_secret_length) <= 0
            || EVP_DigestUpdate(md_ctx, hmac_pad, sslv3_pad_length) <= 0
            || EVP_DigestUpdate(md_ctx, mac_out, md_size) <= 0)
            goto err;
    } else {
        /* Complete the HMAC in the standard manner. */
        for (i = 0; i < md_block_size; i++)
            hmac_pad[i] ^= 0x6a;

        if (EVP_DigestUpdate(md_ctx, hmac_pad, md_block_size) <= 0
            || EVP_DigestUpdate(md_ctx, mac_out, md_size) <= 0)
            goto err;
    }
    /* TODO(size_t): Convert me */
    ret = EVP_DigestFinal(md_ctx, md_out, &md_out_size_u);
    if (ret && md_out_size)
        *md_out_size = md_out_size_u;
    EVP_MD_CTX_free(md_ctx);

    return 1;
 err:
    EVP_MD_CTX_free(md_ctx);
    return 0;
}
