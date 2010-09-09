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

#ifndef _hndtcam_h_
#define _hndtcam_h_

/*
 * 0 - 1
 * 1 - 2 Consecutive locations are patched
 * 2 - 4 Consecutive locations are patched
 * 3 - 8 Consecutive locations are patched
 * 4 - 16 Consecutive locations are patched
 * Define default to patch 2 locations
 */

#define PATCHCOUNT 0
#define SRPC_PATCHCOUNT PATCHCOUNT

/* N Consecutive location to patch */
#define SRPC_PATCHNLOC (1 << (SRPC_PATCHCOUNT))

/* patch values and address structure */
typedef struct patchaddrvalue {
	uint32 addr;
	uint32 value;
} patchaddrvalue_t;

extern void hnd_patch_init(void *srp);
extern void hnd_tcam_write(void *srp, uint16 index, uint32 data);
extern void hnd_tcam_read(void *srp, uint16 index, uint32 * content);
void *hnd_tcam_init(void *srp, uint no_addrs);
extern void hnd_tcam_disablepatch(void *srp);
extern void hnd_tcam_enablepatch(void *srp);
extern void hnd_tcam_load(void *srp, const patchaddrvalue_t * patchtbl);
#endif				/* _hndtcam_h_ */
