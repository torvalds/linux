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

#ifndef _BRCMF_PROTO_H_
#define _BRCMF_PROTO_H_

#ifndef IOCTL_RESP_TIMEOUT
#define IOCTL_RESP_TIMEOUT  2000	/* In milli second */
#endif

#ifndef IOCTL_CHIP_ACTIVE_TIMEOUT
#define IOCTL_CHIP_ACTIVE_TIMEOUT  10	/* In milli second */
#endif

/*
 * Exported from the brcmf protocol module (brcmf_cdc)
 */

/* Linkage, sets prot link and updates hdrlen in pub */
extern int brcmf_proto_attach(struct brcmf_pub *drvr);

/* Unlink, frees allocated protocol memory (including brcmf_proto) */
extern void brcmf_proto_detach(struct brcmf_pub *drvr);

/* Initialize protocol: sync w/dongle state.
 * Sets dongle media info (iswl, drv_version, mac address).
 */
extern int brcmf_proto_init(struct brcmf_pub *drvr);

/* Stop protocol: sync w/dongle state. */
extern void brcmf_proto_stop(struct brcmf_pub *drvr);

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
extern void brcmf_proto_hdrpush(struct brcmf_pub *, int ifidx,
				struct sk_buff *txp);

/* Remove any protocol-specific data header. */
extern int brcmf_proto_hdrpull(struct brcmf_pub *, int *ifidx,
			       struct sk_buff *rxp);

/* Use protocol to issue ioctl to dongle */
extern int brcmf_proto_ioctl(struct brcmf_pub *drvr, int ifidx,
			     struct brcmf_ioctl *ioc, void *buf, int len);

/* Add prot dump output to a buffer */
extern void brcmf_proto_dump(struct brcmf_pub *drvr,
			     struct brcmu_strbuf *strbuf);

/* Update local copy of dongle statistics */
extern void brcmf_proto_dstats(struct brcmf_pub *drvr);

extern int brcmf_c_ioctl(struct brcmf_pub *drvr, struct brcmf_c_ioctl *ioc,
			 void *buf, uint buflen);

extern int brcmf_c_preinit_ioctls(struct brcmf_pub *drvr);

extern int brcmf_proto_cdc_set_ioctl(struct brcmf_pub *drvr, int ifidx,
				     uint cmd, void *buf, uint len);

#endif				/* _BRCMF_PROTO_H_ */
