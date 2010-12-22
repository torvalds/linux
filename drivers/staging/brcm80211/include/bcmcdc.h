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
#include <linux/if_ether.h>

typedef struct cdc_ioctl {
	u32 cmd;		/* ioctl command value */
	u32 len;		/* lower 16: output buflen; upper 16:
				 input buflen (excludes header) */
	u32 flags;		/* flag defns given below */
	u32 status;		/* status code returned from the device */
} cdc_ioctl_t;

/* Max valid buffer size that can be sent to the dongle */
#define CDC_MAX_MSG_SIZE	(ETH_FRAME_LEN+ETH_FCS_LEN)

/* len field is divided into input and output buffer lengths */
#define CDCL_IOC_OUTLEN_MASK   0x0000FFFF	/* maximum or expected
						 response length, */
					   /* excluding IOCTL header */
#define CDCL_IOC_OUTLEN_SHIFT  0
#define CDCL_IOC_INLEN_MASK    0xFFFF0000	/* input buffer length,
						 excluding IOCTL header */
#define CDCL_IOC_INLEN_SHIFT   16

/* CDC flag definitions */
#define CDCF_IOC_ERROR		0x01	/* 0=success, 1=ioctl cmd failed */
#define CDCF_IOC_SET		0x02	/* 0=get, 1=set cmd */
#define CDCF_IOC_IF_MASK	0xF000	/* I/F index */
#define CDCF_IOC_IF_SHIFT	12
#define CDCF_IOC_ID_MASK	0xFFFF0000	/* used to uniquely id an ioctl
						 req/resp pairing */
#define CDCF_IOC_ID_SHIFT	16	/* # of bits of shift for ID Mask */

#define CDC_IOC_IF_IDX(flags)	\
	(((flags) & CDCF_IOC_IF_MASK) >> CDCF_IOC_IF_SHIFT)
#define CDC_IOC_ID(flags)	\
	(((flags) & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT)

#define CDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags) & CDCF_IOC_IF_MASK) >> CDCF_IOC_IF_SHIFT))
#define CDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags = (((hdr)->flags & ~CDCF_IOC_IF_MASK) | \
	((idx) << CDCF_IOC_IF_SHIFT)))

/*
 * BDC header
 *
 *   The BDC header is used on data packets to convey priority across USB.
 */

#define	BDC_HEADER_LEN		4

#define BDC_PROTO_VER		1	/* Protocol version */

#define BDC_FLAG_VER_MASK	0xf0	/* Protocol version mask */
#define BDC_FLAG_VER_SHIFT	4	/* Protocol version shift */

#define BDC_FLAG__UNUSED	0x03	/* Unassigned */
#define BDC_FLAG_SUM_GOOD	0x04	/* Dongle has verified good
					 RX checksums */
#define BDC_FLAG_SUM_NEEDED	0x08	/* Dongle needs to do TX checksums */

#define BDC_PRIORITY_MASK	0x7

#define BDC_FLAG2_FC_FLAG	0x10	/* flag to indicate if pkt contains */
						/* FLOW CONTROL info only */
#define BDC_PRIORITY_FC_SHIFT	4	/* flow control info shift */

#define BDC_FLAG2_IF_MASK	0x0f	/* APSTA: interface on which the
					 packet was received */
#define BDC_FLAG2_IF_SHIFT	0

#define BDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags2) & BDC_FLAG2_IF_MASK) >> BDC_FLAG2_IF_SHIFT))
#define BDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags2 = (((hdr)->flags2 & ~BDC_FLAG2_IF_MASK) | \
	((idx) << BDC_FLAG2_IF_SHIFT)))

struct bdc_header {
	u8 flags;		/* Flags */
	u8 priority;		/* 802.1d Priority 0:2 bits, 4:7 flow
				 control info for usb */
	u8 flags2;
	u8 rssi;
};
