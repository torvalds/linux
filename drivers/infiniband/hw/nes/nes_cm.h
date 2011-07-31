/*
 * Copyright (c) 2006 - 2009 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef NES_CM_H
#define NES_CM_H

#define QUEUE_EVENTS

#define NES_MANAGE_APBVT_DEL 0
#define NES_MANAGE_APBVT_ADD 1

#define NES_MPA_REQUEST_ACCEPT  1
#define NES_MPA_REQUEST_REJECT  2

/* IETF MPA -- defines, enums, structs */
#define IEFT_MPA_KEY_REQ  "MPA ID Req Frame"
#define IEFT_MPA_KEY_REP  "MPA ID Rep Frame"
#define IETF_MPA_KEY_SIZE 16
#define IETF_MPA_VERSION  1
#define IETF_MAX_PRIV_DATA_LEN 512
#define IETF_MPA_FRAME_SIZE     20

enum ietf_mpa_flags {
	IETF_MPA_FLAGS_MARKERS = 0x80,	/* receive Markers */
	IETF_MPA_FLAGS_CRC     = 0x40,	/* receive Markers */
	IETF_MPA_FLAGS_REJECT  = 0x20,	/* Reject */
};

struct ietf_mpa_frame {
	u8 key[IETF_MPA_KEY_SIZE];
	u8 flags;
	u8 rev;
	__be16 priv_data_len;
	u8 priv_data[0];
};

#define ietf_mpa_req_resp_frame ietf_mpa_frame

struct nes_v4_quad {
	u32 rsvd0;
	__le32 DstIpAdrIndex;	/* Only most significant 5 bits are valid */
	__be32 SrcIpadr;
	__be16 TcpPorts[2];		/* src is low, dest is high */
};

struct nes_cm_node;
enum nes_timer_type {
	NES_TIMER_TYPE_SEND,
	NES_TIMER_TYPE_RECV,
	NES_TIMER_NODE_CLEANUP,
	NES_TIMER_TYPE_CLOSE,
};

#define NES_PASSIVE_STATE_INDICATED	0
#define NES_DO_NOT_SEND_RESET_EVENT	1
#define NES_SEND_RESET_EVENT		2

#define MAX_NES_IFS 4

#define SET_ACK 1
#define SET_SYN 2
#define SET_FIN 4
#define SET_RST 8

#define TCP_OPTIONS_PADDING	3

struct option_base {
	u8 optionnum;
	u8 length;
};

enum option_numbers {
	OPTION_NUMBER_END,
	OPTION_NUMBER_NONE,
	OPTION_NUMBER_MSS,
	OPTION_NUMBER_WINDOW_SCALE,
	OPTION_NUMBER_SACK_PERM,
	OPTION_NUMBER_SACK,
	OPTION_NUMBER_WRITE0 = 0xbc
};

struct option_mss {
	u8 optionnum;
	u8 length;
	__be16 mss;
};

struct option_windowscale {
	u8 optionnum;
	u8 length;
	u8 shiftcount;
};

union all_known_options {
	char as_end;
	struct option_base as_base;
	struct option_mss as_mss;
	struct option_windowscale as_windowscale;
};

struct nes_timer_entry {
	struct list_head list;
	unsigned long timetosend;	/* jiffies */
	struct sk_buff *skb;
	u32 type;
	u32 retrycount;
	u32 retranscount;
	u32 context;
	u32 seq_num;
	u32 send_retrans;
	int close_when_complete;
	struct net_device *netdev;
};

#define NES_DEFAULT_RETRYS  64
#define NES_DEFAULT_RETRANS 8
#ifdef CONFIG_INFINIBAND_NES_DEBUG
#define NES_RETRY_TIMEOUT   (1000*HZ/1000)
#else
#define NES_RETRY_TIMEOUT   (3000*HZ/1000)
#endif
#define NES_SHORT_TIME      (10)
#define NES_LONG_TIME       (2000*HZ/1000)
#define NES_MAX_TIMEOUT     ((unsigned long) (12*HZ))

#define NES_CM_HASHTABLE_SIZE         1024
#define NES_CM_TCP_TIMER_INTERVAL     3000
#define NES_CM_DEFAULT_MTU            1540
#define NES_CM_DEFAULT_FRAME_CNT      10
#define NES_CM_THREAD_STACK_SIZE      256
#define NES_CM_DEFAULT_RCV_WND        64240	// before we know that window scaling is allowed
#define NES_CM_DEFAULT_RCV_WND_SCALED 256960  // after we know that window scaling is allowed
#define NES_CM_DEFAULT_RCV_WND_SCALE  2
#define NES_CM_DEFAULT_FREE_PKTS      0x000A
#define NES_CM_FREE_PKT_LO_WATERMARK  2

#define NES_CM_DEFAULT_MSS   536

#define NES_CM_DEF_SEQ       0x159bf75f
#define NES_CM_DEF_LOCAL_ID  0x3b47

#define NES_CM_DEF_SEQ2      0x18ed5740
#define NES_CM_DEF_LOCAL_ID2 0xb807
#define	MAX_CM_BUFFER	(IETF_MPA_FRAME_SIZE + IETF_MAX_PRIV_DATA_LEN)


typedef u32 nes_addr_t;

#define nes_cm_tsa_context nes_qp_context

struct nes_qp;

/* cm node transition states */
enum nes_cm_node_state {
	NES_CM_STATE_UNKNOWN,
	NES_CM_STATE_INITED,
	NES_CM_STATE_LISTENING,
	NES_CM_STATE_SYN_RCVD,
	NES_CM_STATE_SYN_SENT,
	NES_CM_STATE_ONE_SIDE_ESTABLISHED,
	NES_CM_STATE_ESTABLISHED,
	NES_CM_STATE_ACCEPTING,
	NES_CM_STATE_MPAREQ_SENT,
	NES_CM_STATE_MPAREQ_RCVD,
	NES_CM_STATE_MPAREJ_RCVD,
	NES_CM_STATE_TSA,
	NES_CM_STATE_FIN_WAIT1,
	NES_CM_STATE_FIN_WAIT2,
	NES_CM_STATE_CLOSE_WAIT,
	NES_CM_STATE_TIME_WAIT,
	NES_CM_STATE_LAST_ACK,
	NES_CM_STATE_CLOSING,
	NES_CM_STATE_LISTENER_DESTROYED,
	NES_CM_STATE_CLOSED
};

enum nes_tcpip_pkt_type {
	NES_PKT_TYPE_UNKNOWN,
	NES_PKT_TYPE_SYN,
	NES_PKT_TYPE_SYNACK,
	NES_PKT_TYPE_ACK,
	NES_PKT_TYPE_FIN,
	NES_PKT_TYPE_RST
};


/* type of nes connection */
enum nes_cm_conn_type {
	NES_CM_IWARP_CONN_TYPE,
};

/* CM context params */
struct nes_cm_tcp_context {
	u8  client;

	u32 loc_seq_num;
	u32 loc_ack_num;
	u32 rem_ack_num;
	u32 rcv_nxt;

	u32 loc_id;
	u32 rem_id;

	u32 snd_wnd;
	u32 max_snd_wnd;

	u32 rcv_wnd;
	u32 mss;
	u8  snd_wscale;
	u8  rcv_wscale;

	struct nes_cm_tsa_context tsa_cntxt;
	struct timeval            sent_ts;
};


enum nes_cm_listener_state {
	NES_CM_LISTENER_PASSIVE_STATE=1,
	NES_CM_LISTENER_ACTIVE_STATE=2,
	NES_CM_LISTENER_EITHER_STATE=3
};

struct nes_cm_listener {
	struct list_head           list;
	struct nes_cm_core         *cm_core;
	u8                         loc_mac[ETH_ALEN];
	nes_addr_t                 loc_addr;
	u16                        loc_port;
	struct iw_cm_id            *cm_id;
	enum nes_cm_conn_type      conn_type;
	atomic_t                   ref_count;
	struct nes_vnic            *nesvnic;
	atomic_t                   pend_accepts_cnt;
	int                        backlog;
	enum nes_cm_listener_state listener_state;
	u32                        reused_node;
};

/* per connection node and node state information */
struct nes_cm_node {
	nes_addr_t                loc_addr, rem_addr;
	u16                       loc_port, rem_port;

	u8                        loc_mac[ETH_ALEN];
	u8                        rem_mac[ETH_ALEN];

	enum nes_cm_node_state    state;
	struct nes_cm_tcp_context tcp_cntxt;
	struct nes_cm_core        *cm_core;
	struct sk_buff_head       resend_list;
	atomic_t                  ref_count;
	struct net_device         *netdev;

	struct nes_cm_node        *loopbackpartner;

	struct nes_timer_entry	*send_entry;

	spinlock_t                retrans_list_lock;
	struct nes_timer_entry  *recv_entry;

	int                       send_write0;
	union {
		struct ietf_mpa_frame mpa_frame;
		u8                    mpa_frame_buf[MAX_CM_BUFFER];
	};
	u16                       mpa_frame_size;
	struct iw_cm_id           *cm_id;
	struct list_head          list;
	int                       accelerated;
	struct nes_cm_listener    *listener;
	enum nes_cm_conn_type     conn_type;
	struct nes_vnic           *nesvnic;
	int                       apbvt_set;
	int                       accept_pend;
	struct list_head	timer_entry;
	struct list_head	reset_entry;
	struct nes_qp		*nesqp;
	atomic_t 		passive_state;
};

/* structure for client or CM to fill when making CM api calls. */
/*	- only need to set relevant data, based on op. */
struct nes_cm_info {
	union {
		struct iw_cm_id   *cm_id;
		struct net_device *netdev;
	};

	u16 loc_port;
	u16 rem_port;
	nes_addr_t loc_addr;
	nes_addr_t rem_addr;

	enum nes_cm_conn_type  conn_type;
	int backlog;
};

/* CM event codes */
enum  nes_cm_event_type {
	NES_CM_EVENT_UNKNOWN,
	NES_CM_EVENT_ESTABLISHED,
	NES_CM_EVENT_MPA_REQ,
	NES_CM_EVENT_MPA_CONNECT,
	NES_CM_EVENT_MPA_ACCEPT,
	NES_CM_EVENT_MPA_REJECT,
	NES_CM_EVENT_MPA_ESTABLISHED,
	NES_CM_EVENT_CONNECTED,
	NES_CM_EVENT_CLOSED,
	NES_CM_EVENT_RESET,
	NES_CM_EVENT_DROPPED_PKT,
	NES_CM_EVENT_CLOSE_IMMED,
	NES_CM_EVENT_CLOSE_HARD,
	NES_CM_EVENT_CLOSE_CLEAN,
	NES_CM_EVENT_ABORTED,
	NES_CM_EVENT_SEND_FIRST
};

/* event to post to CM event handler */
struct nes_cm_event {
	enum nes_cm_event_type type;

	struct nes_cm_info cm_info;
	struct work_struct event_work;
	struct nes_cm_node *cm_node;
};

struct nes_cm_core {
	enum nes_cm_node_state  state;

	atomic_t                listen_node_cnt;
	struct nes_cm_node      listen_list;
	spinlock_t              listen_list_lock;

	u32                     mtu;
	u32                     free_tx_pkt_max;
	u32                     rx_pkt_posted;
	atomic_t                ht_node_cnt;
	struct list_head        connected_nodes;
	/* struct list_head hashtable[NES_CM_HASHTABLE_SIZE]; */
	spinlock_t              ht_lock;

	struct timer_list       tcp_timer;

	struct nes_cm_ops       *api;

	int (*post_event)(struct nes_cm_event *event);
	atomic_t                events_posted;
	struct workqueue_struct *event_wq;
	struct workqueue_struct *disconn_wq;

	atomic_t                node_cnt;
	u64                     aborted_connects;
	u32                     options;

	struct nes_cm_node      *current_listen_node;
};


#define NES_CM_SET_PKT_SIZE        (1 << 1)
#define NES_CM_SET_FREE_PKT_Q_SIZE (1 << 2)

/* CM ops/API for client interface */
struct nes_cm_ops {
	int (*accelerated)(struct nes_cm_core *, struct nes_cm_node *);
	struct nes_cm_listener * (*listen)(struct nes_cm_core *, struct nes_vnic *,
			struct nes_cm_info *);
	int (*stop_listener)(struct nes_cm_core *, struct nes_cm_listener *);
	struct nes_cm_node * (*connect)(struct nes_cm_core *,
			struct nes_vnic *, u16, void *,
			struct nes_cm_info *);
	int (*close)(struct nes_cm_core *, struct nes_cm_node *);
	int (*accept)(struct nes_cm_core *, struct ietf_mpa_frame *,
			struct nes_cm_node *);
	int (*reject)(struct nes_cm_core *, struct ietf_mpa_frame *,
			struct nes_cm_node *);
	int (*recv_pkt)(struct nes_cm_core *, struct nes_vnic *,
			struct sk_buff *);
	int (*destroy_cm_core)(struct nes_cm_core *);
	int (*get)(struct nes_cm_core *);
	int (*set)(struct nes_cm_core *, u32, u32);
};

int schedule_nes_timer(struct nes_cm_node *, struct sk_buff *,
		enum nes_timer_type, int, int);

int nes_accept(struct iw_cm_id *, struct iw_cm_conn_param *);
int nes_reject(struct iw_cm_id *, const void *, u8);
int nes_connect(struct iw_cm_id *, struct iw_cm_conn_param *);
int nes_create_listen(struct iw_cm_id *, int);
int nes_destroy_listen(struct iw_cm_id *);

int nes_cm_recv(struct sk_buff *, struct net_device *);
int nes_cm_start(void);
int nes_cm_stop(void);

#endif			/* NES_CM_H */
