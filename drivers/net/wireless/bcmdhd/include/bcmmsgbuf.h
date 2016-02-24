/*
 * MSGBUF network driver ioctl/indication encoding
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: bcmmsgbuf.h 452261 2014-01-29 19:30:23Z $
 */
#ifndef _bcmmsgbuf_h_
#define	_bcmmsgbuf_h_
#include <proto/ethernet.h>
#include <wlioctl.h>
#include <bcmpcie.h>
#define MSGBUF_MAX_MSG_SIZE   ETHER_MAX_LEN
#define DNGL_TO_HOST_MSGBUF_SZ	(8 * 1024)	/* Host side ring */
#define HOST_TO_DNGL_MSGBUF_SZ	(8 * 1024)	/* Host side ring */
#define DTOH_LOCAL_MSGBUF_SZ    (8 * 1024)	/* dongle side ring */
#define HTOD_LOCAL_MSGBUF_SZ    (8 * 1024)	/* dongle side ring */
#define HTOD_LOCAL_CTRLRING_SZ  (1 * 1024)  /* H2D control ring dongle side */
#define DTOH_LOCAL_CTRLRING_SZ  (1 * 1024)  /* D2H control ring dongle side */
#define HOST_TO_DNGL_CTRLRING_SZ  (1 * 1024)	/* Host to Device ctrl ring on host */
#define DNGL_TO_HOST_CTRLRING_SZ  (1 * 1024)	/* Device to host ctrl ring on host */

enum {
	DNGL_TO_HOST_MSGBUF,
	HOST_TO_DNGL_MSGBUF
};

enum {
	MSG_TYPE_IOCTL_REQ = 0x1,
	MSG_TYPE_IOCTLPTR_REQ,
	MSG_TYPE_IOCTL_CMPLT,
	MSG_TYPE_WL_EVENT,
	MSG_TYPE_TX_POST,
	MSG_TYPE_RXBUF_POST,
	MSG_TYPE_RX_CMPLT,
	MSG_TYPE_TX_STATUS,
	MSG_TYPE_EVENT_PYLD,
	MSG_TYPE_IOCT_PYLD,     /* used only internally inside dongle */
	MSG_TYPE_RX_PYLD,       /* used only internally inside dongle */
	MSG_TYPE_TX_PYLD,       /* To be removed once split header is implemented */
	MSG_TYPE_HOST_EVNT,
	MSG_TYPE_LOOPBACK = 15,  /* dongle loops the message back to host */
	MSG_TYPE_LPBK_DMAXFER = 16,  /* dongle DMA loopback */
	MSG_TYPE_TX_BATCH_POST = 17
};

enum {
	HOST_TO_DNGL_DATA,
	HOST_TO_DNGL_CTRL,
	DNGL_TO_HOST_DATA,
	DNGL_TO_HOST_CTRL
};

#define MESSAGE_PAYLOAD(a)	(((a) == MSG_TYPE_IOCT_PYLD) | ((a) == MSG_TYPE_RX_PYLD) |\
		((a) == MSG_TYPE_EVENT_PYLD) | ((a) == MSG_TYPE_TX_PYLD))
#define MESSAGE_CTRLPATH(a)	(((a) == MSG_TYPE_IOCTL_REQ) | ((a) == MSG_TYPE_IOCTLPTR_REQ) |\
		((a) == MSG_TYPE_IOCTL_CMPLT) | ((a) == MSG_TYPE_HOST_EVNT) |\
		((a) == MSG_TYPE_LOOPBACK) | ((a) == MSG_TYPE_WL_EVENT))

/* IOCTL req Hdr */
/* cmn Msg Hdr */
typedef struct cmn_msg_hdr {
	uint16 msglen;
	uint8 msgtype;
	uint8 ifidx;
	union seqn {
		uint32 seq_id;
		struct sequence {
			uint16 seq_no;
			uint8 ring_id;
			uint8 rsvd;
		} seq;
	} u;
} cmn_msg_hdr_t;

typedef struct ioctl_req_hdr {
	uint32		pkt_id; /* Packet ID */
	uint32 		cmd; /* IOCTL ID */
	uint16		retbuf_len;
	uint16 		buflen;
	uint16		xt_id; /* transaction ID */
	uint16		rsvd[1];
} ioctl_req_hdr_t;

/* ret buf struct */
typedef struct ret_buf_ptr {
	uint32 low_addr;
	uint32 high_addr;
} ret_buf_t;

/* Complete msgbuf hdr for ioctl from host to dongle */
typedef struct ioct_reqst_hdr {
	cmn_msg_hdr_t msg;
	ioctl_req_hdr_t ioct_hdr;
	ret_buf_t ret_buf;
} ioct_reqst_hdr_t;

typedef struct ioctptr_reqst_hdr {
	cmn_msg_hdr_t msg;
	ioctl_req_hdr_t ioct_hdr;
	ret_buf_t ret_buf;
	ret_buf_t ioct_buf;
} ioctptr_reqst_hdr_t;

/* ioctl response header */
typedef struct ioct_resp_hdr {
	cmn_msg_hdr_t   msg;
	uint32	pkt_id;
	uint32	status;
	uint32	ret_len;
	uint32  inline_data;
	uint16	xt_id;	/* transaction ID */
	uint16	rsvd[1];
} ioct_resp_hdr_t;

/* ioct resp header used in dongle */
/* ret buf hdr will be stripped off inside dongle itself */
typedef struct msgbuf_ioctl_resp {
	ioct_resp_hdr_t	ioct_hdr;
	ret_buf_t	ret_buf;	/* ret buf pointers */
} msgbuf_ioct_resp_t;

/* WL evet hdr info */
typedef struct wl_event_hdr {
	cmn_msg_hdr_t   msg;
	uint16 event;
	uint8 flags;
	uint8 rsvd;
	uint16 retbuf_len;
	uint16 rsvd1;
	uint32 rxbufid;
} wl_event_hdr_t;

#define TXDESCR_FLOWID_PCIELPBK_1	0xFF
#define TXDESCR_FLOWID_PCIELPBK_2	0xFE

typedef struct txbatch_lenptr_tup {
	uint32 pktid;
	uint16 pktlen;
	uint16 rsvd;
	ret_buf_t	ret_buf;	/* ret buf pointers */
} txbatch_lenptr_tup_t;

typedef struct txbatch_cmn_msghdr {
	cmn_msg_hdr_t   msg;
	uint8 priority;
	uint8 hdrlen;
	uint8 pktcnt;
	uint8 flowid;
	uint8 txhdr[ETHER_HDR_LEN];
	uint16 rsvd;
} txbatch_cmn_msghdr_t;

typedef struct txbatch_msghdr {
	txbatch_cmn_msghdr_t txcmn;
	txbatch_lenptr_tup_t tx_tup[0]; /* Based on packet count */
} txbatch_msghdr_t;

/* TX desc posting header */
typedef struct tx_lenptr_tup {
	uint16 pktlen;
	uint16 rsvd;
	ret_buf_t	ret_buf;	/* ret buf pointers */
} tx_lenptr_tup_t;

typedef struct txdescr_cmn_msghdr {
	cmn_msg_hdr_t   msg;
	uint8 priority;
	uint8 hdrlen;
	uint8 descrcnt;
	uint8 flowid;
	uint32 pktid;
} txdescr_cmn_msghdr_t;

typedef struct txdescr_msghdr {
	txdescr_cmn_msghdr_t txcmn;
	uint8 txhdr[ETHER_HDR_LEN];
	uint16 rsvd;
	tx_lenptr_tup_t tx_tup[0]; /* Based on descriptor count */
} txdescr_msghdr_t;

/* Tx status header info */
typedef struct txstatus_hdr {
	cmn_msg_hdr_t   msg;
	uint32 pktid;
} txstatus_hdr_t;
/* RX bufid-len-ptr tuple */
typedef struct rx_lenptr_tup {
	uint32 rxbufid;
	uint16 len;
	uint16 rsvd2;
	ret_buf_t	ret_buf;	/* ret buf pointers */
} rx_lenptr_tup_t;
/* Rx descr Post hdr info */
typedef struct rxdesc_msghdr {
	cmn_msg_hdr_t   msg;
	uint16 rsvd0;
	uint8 rsvd1;
	uint8 descnt;
	rx_lenptr_tup_t rx_tup[0];
} rxdesc_msghdr_t;

/* RX complete tuples */
typedef struct rxcmplt_tup {
	uint16 retbuf_len;
	uint16 data_offset;
	uint32 rxstatus0;
	uint32 rxstatus1;
	uint32 rxbufid;
} rxcmplt_tup_t;
/* RX complete messge hdr */
typedef struct rxcmplt_hdr {
	cmn_msg_hdr_t   msg;
	uint16 rsvd0;
	uint16 rxcmpltcnt;
	rxcmplt_tup_t rx_tup[0];
} rxcmplt_hdr_t;
typedef struct hostevent_hdr {
	cmn_msg_hdr_t   msg;
	uint32 evnt_pyld;
} hostevent_hdr_t;

typedef struct dma_xfer_params {
	uint32 src_physaddr_hi;
	uint32 src_physaddr_lo;
	uint32 dest_physaddr_hi;
	uint32 dest_physaddr_lo;
	uint32 len;
	uint32 srcdelay;
	uint32 destdelay;
} dma_xfer_params_t;

enum {
	HOST_EVENT_CONS_CMD = 1
};

/* defines for flags */
#define MSGBUF_IOC_ACTION_MASK 0x1

#endif /* _bcmmsgbuf_h_ */
