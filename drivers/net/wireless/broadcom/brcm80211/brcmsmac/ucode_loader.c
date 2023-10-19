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

#include <defs.h>
#include "types.h"
#include <ucode_loader.h>

enum {
	D11UCODE_NAMETAG_START = 0,
	D11LCN0BSINITVALS24,
	D11LCN0INITVALS24,
	D11LCN1BSINITVALS24,
	D11LCN1INITVALS24,
	D11LCN2BSINITVALS24,
	D11LCN2INITVALS24,
	D11N0ABSINITVALS16,
	D11N0BSINITVALS16,
	D11N0INITVALS16,
	D11UCODE_OVERSIGHT16_MIMO,
	D11UCODE_OVERSIGHT16_MIMOSZ,
	D11UCODE_OVERSIGHT24_LCN,
	D11UCODE_OVERSIGHT24_LCNSZ,
	D11UCODE_OVERSIGHT_BOMMAJOR,
	D11UCODE_OVERSIGHT_BOMMINOR
};

int brcms_ucode_data_init(struct brcms_info *wl, struct brcms_ucode *ucode)
{
	int rc;

	rc = brcms_check_firmwares(wl);

	rc = rc < 0 ? rc :
		brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn0bsinitvals24,
				     D11LCN0BSINITVALS24);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn0initvals24,
				       D11LCN0INITVALS24);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn1bsinitvals24,
				       D11LCN1BSINITVALS24);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn1initvals24,
				       D11LCN1INITVALS24);
	rc = rc < 0 ? rc :
		brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn2bsinitvals24,
				     D11LCN2BSINITVALS24);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11lcn2initvals24,
				       D11LCN2INITVALS24);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11n0absinitvals16,
				       D11N0ABSINITVALS16);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11n0bsinitvals16,
				       D11N0BSINITVALS16);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->d11n0initvals16,
				       D11N0INITVALS16);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->bcm43xx_16_mimo,
				       D11UCODE_OVERSIGHT16_MIMO);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_uint(wl, &ucode->bcm43xx_16_mimosz,
					D11UCODE_OVERSIGHT16_MIMOSZ);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->bcm43xx_24_lcn,
				       D11UCODE_OVERSIGHT24_LCN);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_uint(wl, &ucode->bcm43xx_24_lcnsz,
					D11UCODE_OVERSIGHT24_LCNSZ);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->bcm43xx_bommajor,
				       D11UCODE_OVERSIGHT_BOMMAJOR);
	rc = rc < 0 ?
	     rc : brcms_ucode_init_buf(wl, (void **)&ucode->bcm43xx_bomminor,
				       D11UCODE_OVERSIGHT_BOMMINOR);
	return rc;
}

void brcms_ucode_data_free(struct brcms_ucode *ucode)
{
	brcms_ucode_free_buf((void *)ucode->d11lcn0bsinitvals24);
	brcms_ucode_free_buf((void *)ucode->d11lcn0initvals24);
	brcms_ucode_free_buf((void *)ucode->d11lcn1bsinitvals24);
	brcms_ucode_free_buf((void *)ucode->d11lcn1initvals24);
	brcms_ucode_free_buf((void *)ucode->d11lcn2bsinitvals24);
	brcms_ucode_free_buf((void *)ucode->d11lcn2initvals24);
	brcms_ucode_free_buf((void *)ucode->d11n0absinitvals16);
	brcms_ucode_free_buf((void *)ucode->d11n0bsinitvals16);
	brcms_ucode_free_buf((void *)ucode->d11n0initvals16);
	brcms_ucode_free_buf((void *)ucode->bcm43xx_16_mimo);
	brcms_ucode_free_buf((void *)ucode->bcm43xx_24_lcn);
	brcms_ucode_free_buf((void *)ucode->bcm43xx_bommajor);
	brcms_ucode_free_buf((void *)ucode->bcm43xx_bomminor);
}
