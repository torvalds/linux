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

#define MIN_FW_SIZE 40000	/* minimum firmware file size in bytes */
#define MAX_FW_SIZE 150000

#define UCODE_LOADER_API_VER 0

struct d11init {
	u16 addr;
	u16 size;
	u32 value;
};

extern struct d11init *d11lcn0bsinitvals24;
extern struct d11init *d11lcn0initvals24;
extern struct d11init *d11lcn1bsinitvals24;
extern struct d11init *d11lcn1initvals24;
extern struct d11init *d11lcn2bsinitvals24;
extern struct d11init *d11lcn2initvals24;
extern struct d11init *d11n0absinitvals16;
extern struct d11init *d11n0bsinitvals16;
extern struct d11init *d11n0initvals16;
extern u32 *bcm43xx_16_mimo;
extern u32 bcm43xx_16_mimosz;
extern u32 *bcm43xx_24_lcn;
extern u32 bcm43xx_24_lcnsz;

extern int wl_ucode_data_init(struct wl_info *wl);
extern void wl_ucode_data_free(void);

extern int wl_ucode_init_buf(struct wl_info *wl, void **pbuf, unsigned int idx);
extern int wl_ucode_init_uint(struct wl_info *wl, unsigned *data,
			      unsigned int idx);
extern void wl_ucode_free_buf(void *);
extern int  wl_check_firmwares(struct wl_info *wl);
