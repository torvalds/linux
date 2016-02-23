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
#ifndef	_BRCM_UCODE_H_
#define	_BRCM_UCODE_H_

#include "types.h"		/* forward structure declarations */

#define MIN_FW_SIZE 40000	/* minimum firmware file size in bytes */
#define MAX_FW_SIZE 150000

#define UCODE_LOADER_API_VER 0

struct d11init;

struct brcms_ucode {
	struct d11init *d11lcn0bsinitvals24;
	struct d11init *d11lcn0initvals24;
	struct d11init *d11lcn1bsinitvals24;
	struct d11init *d11lcn1initvals24;
	struct d11init *d11lcn2bsinitvals24;
	struct d11init *d11lcn2initvals24;
	struct d11init *d11n0absinitvals16;
	struct d11init *d11n0bsinitvals16;
	struct d11init *d11n0initvals16;
	__le32 *bcm43xx_16_mimo;
	size_t bcm43xx_16_mimosz;
	__le32 *bcm43xx_24_lcn;
	size_t bcm43xx_24_lcnsz;
	u32 *bcm43xx_bommajor;
	u32 *bcm43xx_bomminor;
};

int brcms_ucode_data_init(struct brcms_info *wl, struct brcms_ucode *ucode);

void brcms_ucode_data_free(struct brcms_ucode *ucode);

int brcms_ucode_init_buf(struct brcms_info *wl, void **pbuf, unsigned int idx);
int brcms_ucode_init_uint(struct brcms_info *wl, size_t *n_bytes,
			  unsigned int idx);
void brcms_ucode_free_buf(void *);
int  brcms_check_firmwares(struct brcms_info *wl);

#endif	/* _BRCM_UCODE_H_ */
