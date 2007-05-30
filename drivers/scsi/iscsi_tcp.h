/*
 * iSCSI Initiator TCP Transport
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 - 2006 Mike Christie
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * maintained by open-iscsi@googlegroups.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 */

#ifndef ISCSI_TCP_H
#define ISCSI_TCP_H

#include <scsi/libiscsi.h>

/* Socket's Receive state machine */
#define IN_PROGRESS_WAIT_HEADER		0x0
#define IN_PROGRESS_HEADER_GATHER	0x1
#define IN_PROGRESS_DATA_RECV		0x2
#define IN_PROGRESS_DDIGEST_RECV	0x3
#define IN_PROGRESS_PAD_RECV		0x4

/* xmit state machine */
#define XMSTATE_IDLE			0x0
#define XMSTATE_CMD_HDR_INIT		0x1
#define XMSTATE_CMD_HDR_XMIT		0x2
#define XMSTATE_IMM_HDR			0x4
#define XMSTATE_IMM_DATA		0x8
#define XMSTATE_UNS_INIT		0x10
#define XMSTATE_UNS_HDR			0x20
#define XMSTATE_UNS_DATA		0x40
#define XMSTATE_SOL_HDR			0x80
#define XMSTATE_SOL_DATA		0x100
#define XMSTATE_W_PAD			0x200
#define XMSTATE_W_RESEND_PAD		0x400
#define XMSTATE_W_RESEND_DATA_DIGEST	0x800
#define XMSTATE_IMM_HDR_INIT		0x1000
#define XMSTATE_SOL_HDR_INIT		0x2000

#define ISCSI_PAD_LEN			4
#define ISCSI_SG_TABLESIZE		SG_ALL
#define ISCSI_TCP_MAX_CMD_LEN		16

struct crypto_hash;
struct socket;

/* Socket connection recieve helper */
struct iscsi_tcp_recv {
	struct iscsi_hdr	*hdr;
	struct sk_buff		*skb;
	int			offset;
	int			len;
	int			hdr_offset;
	int			copy;
	int			copied;
	int			padding;
	struct iscsi_cmd_task	*ctask;		/* current cmd in progress */

	/* copied and flipped values */
	int			datalen;
	int			datadgst;
	char			zero_copy_hdr;
};

struct iscsi_tcp_conn {
	struct iscsi_conn	*iscsi_conn;
	struct socket		*sock;
	struct iscsi_hdr	hdr;		/* header placeholder */
	char			hdrext[4*sizeof(__u16) +
				    sizeof(__u32)];
	int			data_copied;
	int			stop_stage;	/* conn_stop() flag: *
						 * stop to recover,  *
						 * stop to terminate */
	/* iSCSI connection-wide sequencing */
	int			hdr_size;	/* PDU header size */

	/* control data */
	struct iscsi_tcp_recv	in;		/* TCP receive context */
	int			in_progress;	/* connection state machine */

	/* old values for socket callbacks */
	void			(*old_data_ready)(struct sock *, int);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);

	/* data and header digests */
	struct hash_desc	tx_hash;	/* CRC32C (Tx) */
	struct hash_desc	rx_hash;	/* CRC32C (Rx) */

	/* MIB custom statistics */
	uint32_t		sendpage_failures_cnt;
	uint32_t		discontiguous_hdr_cnt;

	ssize_t (*sendpage)(struct socket *, struct page *, int, size_t, int);
};

struct iscsi_buf {
	struct scatterlist	sg;
	unsigned int		sent;
	char			use_sendmsg;
};

struct iscsi_data_task {
	struct iscsi_data	hdr;			/* PDU */
	char			hdrext[sizeof(__u32)];	/* Header-Digest */
	struct iscsi_buf	digestbuf;		/* digest buffer */
	uint32_t		digest;			/* data digest */
};

struct iscsi_tcp_mgmt_task {
	struct iscsi_hdr	hdr;
	char			hdrext[sizeof(__u32)]; /* Header-Digest */
	int			xmstate;	/* mgmt xmit progress */
	struct iscsi_buf	headbuf;	/* header buffer */
	struct iscsi_buf	sendbuf;	/* in progress buffer */
	int			sent;
};

struct iscsi_r2t_info {
	__be32			ttt;		/* copied from R2T */
	__be32			exp_statsn;	/* copied from R2T */
	uint32_t		data_length;	/* copied from R2T */
	uint32_t		data_offset;	/* copied from R2T */
	struct iscsi_buf	headbuf;	/* Data-Out Header Buffer */
	struct iscsi_buf	sendbuf;	/* Data-Out in progress buffer*/
	int			sent;		/* R2T sequence progress */
	int			data_count;	/* DATA-Out payload progress */
	struct scatterlist	*sg;		/* per-R2T SG list */
	int			solicit_datasn;
	struct iscsi_data_task   dtask;        /* which data task */
};

struct iscsi_tcp_cmd_task {
	struct iscsi_cmd	hdr;
	char			hdrext[4*sizeof(__u16)+	/* AHS */
				    sizeof(__u32)];	/* HeaderDigest */
	char			pad[ISCSI_PAD_LEN];
	int			pad_count;		/* padded bytes */
	struct iscsi_buf	headbuf;		/* header buf (xmit) */
	struct iscsi_buf	sendbuf;		/* in progress buffer*/
	int			xmstate;		/* xmit xtate machine */
	int			sent;
	struct scatterlist	*sg;			/* per-cmd SG list  */
	struct scatterlist	*bad_sg;		/* assert statement */
	int			sg_count;		/* SG's to process  */
	uint32_t		exp_datasn;		/* expected target's R2TSN/DataSN */
	int			data_offset;
	struct iscsi_r2t_info	*r2t;			/* in progress R2T    */
	struct iscsi_queue	r2tpool;
	struct kfifo		*r2tqueue;
	struct iscsi_r2t_info	**r2ts;
	int			digest_count;
	uint32_t		immdigest;		/* for imm data */
	struct iscsi_buf	immbuf;			/* for imm data digest */
	struct iscsi_data_task	unsol_dtask;	/* unsol data task */
};

#endif /* ISCSI_H */
