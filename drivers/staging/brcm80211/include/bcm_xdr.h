/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BCM_XDR_H
#define _BCM_XDR_H

/*
 * bcm_xdr_buf_t
 * Structure used for bookkeeping of a buffer being packed or unpacked.
 * Keeps a current read/write pointer and size as well as
 * the original buffer pointer and size.
 *
 */
typedef struct {
	u8 *buf;		/* pointer to current position in origbuf */
	uint size;		/* current (residual) size in bytes */
	u8 *origbuf;		/* unmodified pointer to orignal buffer */
	uint origsize;		/* unmodified orignal buffer size in bytes */
} bcm_xdr_buf_t;

void bcm_xdr_buf_init(bcm_xdr_buf_t *b, void *buf, size_t len);

int bcm_xdr_pack_u32(bcm_xdr_buf_t *b, u32 val);
int bcm_xdr_unpack_u32(bcm_xdr_buf_t *b, u32 *pval);
int bcm_xdr_pack_s32(bcm_xdr_buf_t *b, s32 val);
int bcm_xdr_unpack_s32(bcm_xdr_buf_t *b, s32 *pval);
int bcm_xdr_pack_s8(bcm_xdr_buf_t *b, s8 val);
int bcm_xdr_unpack_s8(bcm_xdr_buf_t *b, s8 *pval);
int bcm_xdr_pack_opaque(bcm_xdr_buf_t *b, uint len, void *data);
int bcm_xdr_unpack_opaque(bcm_xdr_buf_t *b, uint len, void **pdata);
int bcm_xdr_unpack_opaque_cpy(bcm_xdr_buf_t *b, uint len, void *data);
int bcm_xdr_pack_opaque_varlen(bcm_xdr_buf_t *b, uint len, void *data);
int bcm_xdr_unpack_opaque_varlen(bcm_xdr_buf_t *b, uint *plen, void **pdata);
int bcm_xdr_pack_string(bcm_xdr_buf_t *b, char *str);
int bcm_xdr_unpack_string(bcm_xdr_buf_t *b, uint *plen, char **pstr);

int bcm_xdr_pack_u8_vec(bcm_xdr_buf_t *, u8 *vec, u32 elems);
int bcm_xdr_unpack_u8_vec(bcm_xdr_buf_t *, u8 *vec, u32 elems);
int bcm_xdr_pack_u16_vec(bcm_xdr_buf_t *b, uint len, void *vec);
int bcm_xdr_unpack_u16_vec(bcm_xdr_buf_t *b, uint len, void *vec);
int bcm_xdr_pack_u32_vec(bcm_xdr_buf_t *b, uint len, void *vec);
int bcm_xdr_unpack_u32_vec(bcm_xdr_buf_t *b, uint len, void *vec);

int bcm_xdr_pack_opaque_raw(bcm_xdr_buf_t *b, uint len, void *data);
int bcm_xdr_pack_opaque_pad(bcm_xdr_buf_t *b);

#endif				/* _BCM_XDR_H */
