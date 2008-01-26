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

struct crypto_hash;
struct socket;
struct iscsi_tcp_conn;
struct iscsi_segment;

typedef int iscsi_segment_done_fn_t(struct iscsi_tcp_conn *,
				    struct iscsi_segment *);

struct iscsi_segment {
	unsigned char		*data;
	unsigned int		size;
	unsigned int		copied;
	unsigned int		total_size;
	unsigned int		total_copied;

	struct hash_desc	*hash;
	unsigned char		recv_digest[ISCSI_DIGEST_SIZE];
	unsigned char		digest[ISCSI_DIGEST_SIZE];
	unsigned int		digest_len;

	struct scatterlist	*sg;
	void			*sg_mapped;
	unsigned int		sg_offset;

	iscsi_segment_done_fn_t	*done;
};

/* Socket connection recieve helper */
struct iscsi_tcp_recv {
	struct iscsi_hdr	*hdr;
	struct iscsi_segment	segment;

	/* Allocate buffer for BHS + AHS */
	uint32_t		hdr_buf[64];

	/* copied and flipped values */
	int			datalen;
};

/* Socket connection send helper */
struct iscsi_tcp_send {
	struct iscsi_hdr	*hdr;
	struct iscsi_segment	segment;
	struct iscsi_segment	data_segment;
};

struct iscsi_tcp_conn {
	struct iscsi_conn	*iscsi_conn;
	struct socket		*sock;
	int			stop_stage;	/* conn_stop() flag: *
						 * stop to recover,  *
						 * stop to terminate */
	/* control data */
	struct iscsi_tcp_recv	in;		/* TCP receive context */
	struct iscsi_tcp_send	out;		/* TCP send context */

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

	int			error;

	ssize_t (*sendpage)(struct socket *, struct page *, int, size_t, int);
};

struct iscsi_data_task {
	struct iscsi_data	hdr;			/* PDU */
	char			hdrext[ISCSI_DIGEST_SIZE];/* Header-Digest */
};

struct iscsi_tcp_mgmt_task {
	struct iscsi_hdr	hdr;
	char			hdrext[ISCSI_DIGEST_SIZE]; /* Header-Digest */
};

struct iscsi_r2t_info {
	__be32			ttt;		/* copied from R2T */
	__be32			exp_statsn;	/* copied from R2T */
	uint32_t		data_length;	/* copied from R2T */
	uint32_t		data_offset;	/* copied from R2T */
	int			sent;		/* R2T sequence progress */
	int			data_count;	/* DATA-Out payload progress */
	int			solicit_datasn;
	struct iscsi_data_task	dtask;		/* Data-Out header buf */
};

struct iscsi_tcp_cmd_task {
	struct iscsi_hdr_buff {
		struct iscsi_cmd	cmd_hdr;
		char			hdrextbuf[ISCSI_MAX_AHS_SIZE +
		                                  ISCSI_DIGEST_SIZE];
	} hdr;

	int			sent;
	uint32_t		exp_datasn;	/* expected target's R2TSN/DataSN */
	int			data_offset;
	struct iscsi_r2t_info	*r2t;		/* in progress R2T    */
	struct iscsi_pool	r2tpool;
	struct kfifo		*r2tqueue;
	struct iscsi_data_task	unsol_dtask;	/* Data-Out header buf */
};

#endif /* ISCSI_H */
