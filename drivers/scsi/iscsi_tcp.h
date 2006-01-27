/*
 * iSCSI Initiator TCP Transport
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 Mike Christie
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

/* Session's states */
#define ISCSI_STATE_FREE		1
#define ISCSI_STATE_LOGGED_IN		2
#define ISCSI_STATE_FAILED		3
#define ISCSI_STATE_TERMINATE		4

/* Connection's states */
#define ISCSI_CONN_INITIAL_STAGE	0
#define ISCSI_CONN_STARTED		1
#define ISCSI_CONN_STOPPED		2
#define ISCSI_CONN_CLEANUP_WAIT		3

/* Connection suspend "bit" */
#define SUSPEND_BIT			1

/* Socket's Receive state machine */
#define IN_PROGRESS_WAIT_HEADER		0x0
#define IN_PROGRESS_HEADER_GATHER	0x1
#define IN_PROGRESS_DATA_RECV		0x2
#define IN_PROGRESS_DDIGEST_RECV	0x3

/* Task Mgmt states */
#define	TMABORT_INITIAL			0x0
#define	TMABORT_SUCCESS			0x1
#define	TMABORT_FAILED			0x2
#define	TMABORT_TIMEDOUT		0x3

/* xmit state machine */
#define	XMSTATE_IDLE			0x0
#define	XMSTATE_R_HDR			0x1
#define	XMSTATE_W_HDR			0x2
#define	XMSTATE_IMM_HDR			0x4
#define	XMSTATE_IMM_DATA		0x8
#define	XMSTATE_UNS_INIT		0x10
#define	XMSTATE_UNS_HDR			0x20
#define	XMSTATE_UNS_DATA		0x40
#define	XMSTATE_SOL_HDR			0x80
#define	XMSTATE_SOL_DATA		0x100
#define	XMSTATE_W_PAD			0x200
#define XMSTATE_DATA_DIGEST		0x400

#define ISCSI_CONN_MAX			1
#define ISCSI_CONN_RCVBUF_MIN		262144
#define ISCSI_CONN_SNDBUF_MIN		262144
#define ISCSI_PAD_LEN			4
#define ISCSI_R2T_MAX			16
#define ISCSI_XMIT_CMDS_MAX		128	/* must be power of 2 */
#define ISCSI_MGMT_CMDS_MAX		32	/* must be power of 2 */
#define ISCSI_MGMT_ITT_OFFSET		0xa00
#define ISCSI_SG_TABLESIZE		SG_ALL
#define ISCSI_DEF_CMD_PER_LUN		32
#define ISCSI_MAX_CMD_PER_LUN		128
#define ISCSI_TCP_MAX_CMD_LEN		16

#define ITT_MASK			(0xfff)
#define CID_SHIFT			12
#define CID_MASK			(0xffff<<CID_SHIFT)
#define AGE_SHIFT			28
#define AGE_MASK			(0xf<<AGE_SHIFT)

struct iscsi_queue {
	struct kfifo		*queue;		/* FIFO Queue */
	void			**pool;		/* Pool of elements */
	int			max;		/* Max number of elements */
};

struct iscsi_session;
struct iscsi_cmd_task;
struct iscsi_mgmt_task;

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
	int			opcode;
	int			flags;
	int			cmd_status;
	int			ahslen;
	int			datalen;
	uint32_t		itt;
	int			datadgst;
};

struct iscsi_conn {
	struct iscsi_hdr	hdr;		/* header placeholder */
	char			hdrext[4*sizeof(__u16) +
				    sizeof(__u32)];
	int			data_copied;
	char			*data;		/* data placeholder */
	struct socket           *sock;          /* TCP socket */
	int			data_size;	/* actual recv_dlength */
	int			stop_stage;	/* conn_stop() flag: *
						 * stop to recover,  *
						 * stop to terminate */
	/* iSCSI connection-wide sequencing */
	uint32_t		exp_statsn;
	int			hdr_size;	/* PDU header size */
	unsigned long		suspend_rx;	/* suspend Rx */

	struct crypto_tfm	*rx_tfm;	/* CRC32C (Rx) */
	struct crypto_tfm	*data_rx_tfm;	/* CRC32C (Rx) for data */

	/* control data */
	int			senselen;	/* scsi sense length */
	int			id;		/* CID */
	struct iscsi_tcp_recv	in;		/* TCP receive context */
	struct iscsi_session	*session;	/* parent session */
	struct list_head	item;		/* maintains list of conns */
	int			in_progress;	/* connection state machine */
	int			c_stage;	/* connection state */
	struct iscsi_mgmt_task	*login_mtask;	/* mtask used for login/text */
	struct iscsi_mgmt_task	*mtask;		/* xmit mtask in progress */
	struct iscsi_cmd_task	*ctask;		/* xmit ctask in progress */
	spinlock_t		lock;		/* FIXME: to be removed */

	/* old values for socket callbacks */
	void			(*old_data_ready)(struct sock *, int);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);

	/* xmit */
	struct crypto_tfm	*tx_tfm;	/* CRC32C (Tx) */
	struct crypto_tfm	*data_tx_tfm;	/* CRC32C (Tx) for data */
	struct kfifo		*writequeue;	/* write cmds for Data-Outs */
	struct kfifo		*immqueue;	/* immediate xmit queue */
	struct kfifo		*mgmtqueue;	/* mgmt (control) xmit queue */
	struct kfifo		*xmitqueue;	/* data-path cmd queue */
	struct work_struct	xmitwork;	/* per-conn. xmit workqueue */
	struct mutex		xmitmutex;	/* serializes connection xmit,
						 * access to kfifos:	  *
						 * xmitqueue, writequeue, *
						 * immqueue, mgmtqueue    */
	unsigned long		suspend_tx;	/* suspend Tx */

	/* abort */
	wait_queue_head_t	ehwait;		/* used in eh_abort()     */
	struct iscsi_tm		tmhdr;
	struct timer_list	tmabort_timer;  /* abort timer */
	int			tmabort_state;  /* see TMABORT_INITIAL, etc.*/

	/* negotiated params */
	int			max_recv_dlength;
	int			max_xmit_dlength;
	int			hdrdgst_en;
	int			datadgst_en;

	/* MIB-statistics */
	uint64_t		txdata_octets;
	uint64_t		rxdata_octets;
	uint32_t		scsicmd_pdus_cnt;
	uint32_t		dataout_pdus_cnt;
	uint32_t		scsirsp_pdus_cnt;
	uint32_t		datain_pdus_cnt;
	uint32_t		r2t_pdus_cnt;
	uint32_t		tmfcmd_pdus_cnt;
	int32_t			tmfrsp_pdus_cnt;

	/* custom statistics */
	uint32_t		sendpage_failures_cnt;
	uint32_t		discontiguous_hdr_cnt;
	uint32_t		eh_abort_cnt;

	ssize_t (*sendpage)(struct socket *, struct page *, int, size_t, int);
};

struct iscsi_session {
	/* iSCSI session-wide sequencing */
	uint32_t		cmdsn;
	uint32_t		exp_cmdsn;
	uint32_t		max_cmdsn;

	/* configuration */
	int			initial_r2t_en;
	int			max_r2t;
	int			imm_data_en;
	int			first_burst;
	int			max_burst;
	int			time2wait;
	int			time2retain;
	int			pdu_inorder_en;
	int			dataseq_inorder_en;
	int			erl;
	int			ifmarker_en;
	int			ofmarker_en;

	/* control data */
	struct Scsi_Host	*host;
	int			id;
	struct iscsi_conn	*leadconn;	/* leading connection */
	spinlock_t		lock;		/* protects session state, *
						 * sequence numbers,       *
						 * session resources:      *
						 * - cmdpool,		   *
						 * - mgmtpool,		   *
						 * - r2tpool		   */
	int			state;		/* session state           */
	struct list_head	item;
	void			*auth_client;
	int			conn_cnt;
	int			age;		/* counts session re-opens */

	struct list_head	connections;	/* list of connections */
	int			cmds_max;	/* size of cmds array */
	struct iscsi_cmd_task	**cmds;		/* Original Cmds arr */
	struct iscsi_queue	cmdpool;	/* PDU's pool */
	int			mgmtpool_max;	/* size of mgmt array */
	struct iscsi_mgmt_task	**mgmt_cmds;	/* Original mgmt arr */
	struct iscsi_queue	mgmtpool;	/* Mgmt PDU's pool */
};

struct iscsi_buf {
	struct scatterlist	sg;
	unsigned int		sent;
	char			use_sendmsg;
};

struct iscsi_data_task {
	struct iscsi_data	hdr;			/* PDU */
	char			hdrext[sizeof(__u32)];	/* Header-Digest */
	struct list_head	item;			/* data queue item */
	struct iscsi_buf	digestbuf;		/* digest buffer */
	uint32_t		digest;			/* data digest */
};
#define ISCSI_DTASK_DEFAULT_MAX	ISCSI_SG_TABLESIZE * PAGE_SIZE / 512

struct iscsi_mgmt_task {
	struct iscsi_hdr	hdr;		/* mgmt. PDU */
	char			hdrext[sizeof(__u32)];	/* Header-Digest */
	char			*data;		/* mgmt payload */
	int			xmstate;	/* mgmt xmit progress */
	int			data_count;	/* counts data to be sent */
	struct iscsi_buf	headbuf;	/* header buffer */
	struct iscsi_buf	sendbuf;	/* in progress buffer */
	int			sent;
	uint32_t		itt;		/* this ITT */
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
	struct iscsi_data_task   *dtask;        /* which data task */
};

struct iscsi_cmd_task {
	struct iscsi_cmd	hdr;			/* iSCSI PDU header */
	char			hdrext[4*sizeof(__u16)+	/* AHS */
				    sizeof(__u32)];	/* HeaderDigest */
	char			pad[ISCSI_PAD_LEN];
	int			itt;			/* this ITT */
	int			datasn;			/* DataSN */
	struct iscsi_buf	headbuf;		/* header buf (xmit) */
	struct iscsi_buf	sendbuf;		/* in progress buffer*/
	int			sent;
	struct scatterlist	*sg;			/* per-cmd SG list  */
	struct scatterlist	*bad_sg;		/* assert statement */
	int			sg_count;		/* SG's to process  */
	uint32_t		unsol_datasn;
	uint32_t		exp_r2tsn;
	int			xmstate;		/* xmit xtate machine */
	int			imm_count;		/* imm-data (bytes)   */
	int			unsol_count;		/* unsolicited (bytes)*/
	int			r2t_data_count;		/* R2T Data-Out bytes */
	int			data_count;		/* remaining Data-Out */
	int			pad_count;		/* padded bytes */
	struct scsi_cmnd	*sc;			/* associated SCSI cmd*/
	int			total_length;
	int			data_offset;
	struct iscsi_conn	*conn;			/* used connection    */
	struct iscsi_mgmt_task	*mtask;			/* tmf mtask in progr */

	struct iscsi_r2t_info	*r2t;			/* in progress R2T    */
	struct iscsi_queue	r2tpool;
	struct kfifo		*r2tqueue;
	struct iscsi_r2t_info	**r2ts;
	struct list_head	dataqueue;		/* Data-Out dataqueue */
	mempool_t		*datapool;
	uint32_t		datadigest;		/* for recover digest */
	int			digest_count;
	uint32_t		immdigest;		/* for imm data */
	struct iscsi_buf	immbuf;			/* for imm data digest */
	struct iscsi_data_task   *dtask;		/* data task in progress*/
	int			digest_offset;		/* for partial buff digest */
};

#endif /* ISCSI_H */
