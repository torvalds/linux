/*
 * Copyright (c) 2013 Broadcom Corporation
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
#ifndef BRCMFMAC_BCDC_H
#define BRCMFMAC_BCDC_H

#ifdef CONFIG_BRCMFMAC_PROTO_BCDC
int brcmf_proto_bcdc_attach(struct brcmf_pub *drvr);
void brcmf_proto_bcdc_detach(struct brcmf_pub *drvr);
#else
static inline int brcmf_proto_bcdc_attach(struct brcmf_pub *drvr) { return 0; }
static inline void brcmf_proto_bcdc_detach(struct brcmf_pub *drvr) {}
#endif

#endif /* BRCMFMAC_BCDC_H */
