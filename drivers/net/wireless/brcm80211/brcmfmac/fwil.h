/*
 * Copyright (c) 2012 Broadcom Corporation
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

#ifndef _fwil_h_
#define _fwil_h_

s32 brcmf_fil_cmd_data_set(struct brcmf_if *ifp, u32 cmd, void *data, u32 len);
s32 brcmf_fil_cmd_data_get(struct brcmf_if *ifp, u32 cmd, void *data, u32 len);
s32 brcmf_fil_cmd_int_set(struct brcmf_if *ifp, u32 cmd, u32 data);
s32 brcmf_fil_cmd_int_get(struct brcmf_if *ifp, u32 cmd, u32 *data);

s32 brcmf_fil_iovar_data_set(struct brcmf_if *ifp, char *name, void *data,
			     u32 len);
s32 brcmf_fil_iovar_data_get(struct brcmf_if *ifp, char *name, void *data,
			     u32 len);
s32 brcmf_fil_iovar_int_set(struct brcmf_if *ifp, char *name, u32 data);
s32 brcmf_fil_iovar_int_get(struct brcmf_if *ifp, char *name, u32 *data);

s32 brcmf_fil_bsscfg_data_set(struct brcmf_if *ifp, char *name, void *data,
			      u32 len);
s32 brcmf_fil_bsscfg_data_get(struct brcmf_if *ifp, char *name, void *data,
			      u32 len);
s32 brcmf_fil_bsscfg_int_set(struct brcmf_if *ifp, char *name, u32 data);
s32 brcmf_fil_bsscfg_int_get(struct brcmf_if *ifp, char *name, u32 *data);

#endif /* _fwil_h_ */
