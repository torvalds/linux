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

#include <linux/types.h>
#include <bcmdefs.h>
#include <d11ucode_ext.h>
#include <wl_ucode.h>



d11init_t *d11lcn0bsinitvals24;
d11init_t *d11lcn0initvals24;
d11init_t *d11lcn1bsinitvals24;
d11init_t *d11lcn1initvals24;
d11init_t *d11lcn2bsinitvals24;
d11init_t *d11lcn2initvals24;
d11init_t *d11n0absinitvals16;
d11init_t *d11n0bsinitvals16;
d11init_t *d11n0initvals16;
u32 *bcm43xx_16_mimo;
u32 bcm43xx_16_mimosz;
u32 *bcm43xx_24_lcn;
u32 bcm43xx_24_lcnsz;
u32 *bcm43xx_bommajor;
u32 *bcm43xx_bomminor;

int wl_ucode_data_init(struct wl_info *wl)
{
	wl_ucode_init_buf(wl, (void **)&d11lcn0bsinitvals24,
			  D11LCN0BSINITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11lcn0initvals24, D11LCN0INITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11lcn1bsinitvals24,
			  D11LCN1BSINITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11lcn1initvals24, D11LCN1INITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11lcn2bsinitvals24,
			  D11LCN2BSINITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11lcn2initvals24, D11LCN2INITVALS24);
	wl_ucode_init_buf(wl, (void **)&d11n0absinitvals16, D11N0ABSINITVALS16);
	wl_ucode_init_buf(wl, (void **)&d11n0bsinitvals16, D11N0BSINITVALS16);
	wl_ucode_init_buf(wl, (void **)&d11n0initvals16, D11N0INITVALS16);
	wl_ucode_init_buf(wl, (void **)&bcm43xx_16_mimo,
			  D11UCODE_OVERSIGHT16_MIMO);
	wl_ucode_init_uint(wl, &bcm43xx_16_mimosz, D11UCODE_OVERSIGHT16_MIMOSZ);
	wl_ucode_init_buf(wl, (void **)&bcm43xx_24_lcn,
			  D11UCODE_OVERSIGHT24_LCN);
	wl_ucode_init_uint(wl, &bcm43xx_24_lcnsz, D11UCODE_OVERSIGHT24_LCNSZ);
	wl_ucode_init_buf(wl, (void **)&bcm43xx_bommajor,
			  D11UCODE_OVERSIGHT_BOMMAJOR);
	wl_ucode_init_buf(wl, (void **)&bcm43xx_bomminor,
			  D11UCODE_OVERSIGHT_BOMMINOR);

	return 0;
}

void wl_ucode_data_free(void)
{
	wl_ucode_free_buf((void *)d11lcn0bsinitvals24);
	wl_ucode_free_buf((void *)d11lcn0initvals24);
	wl_ucode_free_buf((void *)d11lcn1bsinitvals24);
	wl_ucode_free_buf((void *)d11lcn1initvals24);
	wl_ucode_free_buf((void *)d11lcn2bsinitvals24);
	wl_ucode_free_buf((void *)d11lcn2initvals24);
	wl_ucode_free_buf((void *)d11n0absinitvals16);
	wl_ucode_free_buf((void *)d11n0bsinitvals16);
	wl_ucode_free_buf((void *)d11n0initvals16);
	wl_ucode_free_buf((void *)bcm43xx_16_mimo);
	wl_ucode_free_buf((void *)bcm43xx_24_lcn);
	wl_ucode_free_buf((void *)bcm43xx_bommajor);
	wl_ucode_free_buf((void *)bcm43xx_bomminor);

	return;
}
