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

typedef struct d11init {
	u16 addr;
	u16 size;
	u32 value;
} d11init_t;

extern d11init_t *d11lcn0bsinitvals24;
extern d11init_t *d11lcn0initvals24;
extern d11init_t *d11lcn1bsinitvals24;
extern d11init_t *d11lcn1initvals24;
extern d11init_t *d11lcn2bsinitvals24;
extern d11init_t *d11lcn2initvals24;
extern d11init_t *d11n0absinitvals16;
extern d11init_t *d11n0bsinitvals16;
extern d11init_t *d11n0initvals16;
extern u32 *bcm43xx_16_mimo;
extern u32 bcm43xx_16_mimosz;
extern u32 *bcm43xx_24_lcn;
extern u32 bcm43xx_24_lcnsz;
extern u32 *bcm43xx_bommajor;
extern u32 *bcm43xx_bomminor;
