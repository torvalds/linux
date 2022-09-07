/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * iSCSI Initiator TCP Transport
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 - 2006 Mike Christie
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * maintained by open-iscsi@googlegroups.com
 *
 * See the file COPYING included with this distribution for more details.
 */

#ifndef ISCSI_SW_TCP_H
#define ISCSI_SW_TCP_H

#include <scsi/libiscsi.h>
#include <scsi/libiscsi_tcp.h>

struct socket;
struct iscsi_tcp_conn;

/* Socket connection send helper */
struct iscsi_sw_tcp_send {
	struct iscsi_hdr	*hdr;
	struct iscsi_segment	segment;
	struct iscsi_segment	data_segment;
};

struct iscsi_sw_tcp_conn {
	struct socket		*sock;
	/* Taken when accessing the sock from the netlink/sysfs interface */
	struct mutex		sock_lock;

	struct iscsi_sw_tcp_send out;
	/* old values for socket callbacks */
	void			(*old_data_ready)(struct sock *);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);

	/* data and header digests */
	struct ahash_request	*tx_hash;	/* CRC32C (Tx) */
	struct ahash_request	*rx_hash;	/* CRC32C (Rx) */

	/* MIB custom statistics */
	uint32_t		sendpage_failures_cnt;
	uint32_t		discontiguous_hdr_cnt;

	ssize_t (*sendpage)(struct socket *, struct page *, int, size_t, int);
};

struct iscsi_sw_tcp_host {
	struct iscsi_session	*session;
};

struct iscsi_sw_tcp_hdrbuf {
	struct iscsi_hdr	hdrbuf;
	char			hdrextbuf[ISCSI_MAX_AHS_SIZE +
		                                  ISCSI_DIGEST_SIZE];
};

#endif /* ISCSI_SW_TCP_H */
