/*
 * CDC network driver ioctl/indication encoding
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */
#ifndef _bcmcdc_h_
#define	_bcmcdc_h_
#include <ethernet.h>

typedef struct cdc_ioctl {
	uint32 cmd;      /* ioctl command value */
	uint32 len;      /* lower 16: output buflen; upper 16: input buflen (excludes header) */
	uint32 flags;    /* flag defns given below */
	uint32 status;   /* status code returned from the device */
} cdc_ioctl_t;

/* Max valid buffer size that can be sent to the dongle */
#define CDC_MAX_MSG_SIZE   ETHER_MAX_LEN

/* len field is divided into input and output buffer lengths */
#define CDCL_IOC_OUTLEN_MASK   0x0000FFFF  /* maximum or expected response length, */
					   /* excluding IOCTL header */
#define CDCL_IOC_OUTLEN_SHIFT  0
#define CDCL_IOC_INLEN_MASK    0xFFFF0000   /* input buffer length, excluding IOCTL header */
#define CDCL_IOC_INLEN_SHIFT   16

/* CDC flag definitions */
#define CDCF_IOC_ERROR		0x01	/* 0=success, 1=ioctl cmd failed */
#define CDCF_IOC_SET		0x02	/* 0=get, 1=set cmd */
#define CDCF_IOC_OVL_IDX_MASK	0x3c	/* overlay region index mask */
#define CDCF_IOC_OVL_RSV	0x40	/* 1=reserve this overlay region */
#define CDCF_IOC_OVL		0x80	/* 1=this ioctl corresponds to an overlay */
#define CDCF_IOC_ACTION_MASK	0xfe	/* SET/GET, OVL_IDX, OVL_RSV, OVL mask */
#define CDCF_IOC_ACTION_SHIFT	1	/* SET/GET, OVL_IDX, OVL_RSV, OVL shift */
#define CDCF_IOC_IF_MASK	0xF000	/* I/F index */
#define CDCF_IOC_IF_SHIFT	12
#define CDCF_IOC_ID_MASK	0xFFFF0000	/* used to uniquely id an ioctl req/resp pairing */
#define CDCF_IOC_ID_SHIFT	16		/* # of bits of shift for ID Mask */

#define CDC_IOC_IF_IDX(flags)	(((flags) & CDCF_IOC_IF_MASK) >> CDCF_IOC_IF_SHIFT)
#define CDC_IOC_ID(flags)	(((flags) & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT)

#define CDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags) & CDCF_IOC_IF_MASK) >> CDCF_IOC_IF_SHIFT))
#define CDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags = (((hdr)->flags & ~CDCF_IOC_IF_MASK) | ((idx) << CDCF_IOC_IF_SHIFT)))

/*
 * BDC header
 *
 *   The BDC header is used on data packets to convey priority across USB.
 */

struct bdc_header {
	uint8	flags;			/* Flags */
	uint8	priority;		/* 802.1d Priority 0:2 bits, 4:7 USB flow control info */
	uint8	flags2;
	uint8	dataOffset;		/* Offset from end of BDC header to packet data, in
					 * 4-byte words.  Leaves room for optional headers.
					 */
};

#define	BDC_HEADER_LEN		4

/* flags field bitmap */
#define BDC_FLAG_EXEMPT		0x03	/* EXT_STA: encryption exemption (host -> dongle?) */
#define BDC_FLAG_80211_PKT	0x01	/* Packet is in 802.11 format (dongle -> host) */
#define BDC_FLAG_SUM_GOOD	0x04	/* Dongle has verified good RX checksums */
#define BDC_FLAG_SUM_NEEDED	0x08	/* Dongle needs to do TX checksums: host->device */
#define BDC_FLAG_EVENT_MSG	0x08	/* Payload contains an event msg: device->host */
#define BDC_FLAG_VER_MASK	0xf0	/* Protocol version mask */
#define BDC_FLAG_VER_SHIFT	4	/* Protocol version shift */

/* priority field bitmap */
#define BDC_PRIORITY_MASK	0x07
#define BDC_PRIORITY_FC_MASK	0xf0	/* flow control info mask */
#define BDC_PRIORITY_FC_SHIFT	4	/* flow control info shift */

/* flags2 field bitmap */
#define BDC_FLAG2_IF_MASK	0x0f	/* interface index (host <-> dongle) */
#define BDC_FLAG2_IF_SHIFT	0
#define BDC_FLAG2_FC_FLAG	0x10	/* flag to indicate if pkt contains */
					/* FLOW CONTROL info only */

/* version numbers */
#define BDC_PROTO_VER_1		1	/* Old Protocol version */
#define BDC_PROTO_VER		2	/* Protocol version */

/* flags2.if field access macros */
#define BDC_GET_IF_IDX(hdr) \
	((int)((((hdr)->flags2) & BDC_FLAG2_IF_MASK) >> BDC_FLAG2_IF_SHIFT))
#define BDC_SET_IF_IDX(hdr, idx) \
	((hdr)->flags2 = (((hdr)->flags2 & ~BDC_FLAG2_IF_MASK) | ((idx) << BDC_FLAG2_IF_SHIFT)))

#endif /* _bcmcdc_h_ */
