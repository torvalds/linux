/*******************************************************************************
*
* Copyright (c) 2015 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
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
*******************************************************************************/

#ifndef I40IW_CM_H
#define I40IW_CM_H

#define QUEUE_EVENTS

#define I40IW_MANAGE_APBVT_DEL 0
#define I40IW_MANAGE_APBVT_ADD 1

#define I40IW_MPA_REQUEST_ACCEPT  1
#define I40IW_MPA_REQUEST_REJECT  2

/* IETF MPA -- defines, enums, structs */
#define IEFT_MPA_KEY_REQ  "MPA ID Req Frame"
#define IEFT_MPA_KEY_REP  "MPA ID Rep Frame"
#define IETF_MPA_KEY_SIZE 16
#define IETF_MPA_VERSION  1
#define IETF_MAX_PRIV_DATA_LEN 512
#define IETF_MPA_FRAME_SIZE    20
#define IETF_RTR_MSG_SIZE      4
#define IETF_MPA_V2_FLAG       0x10
#define SNDMARKER_SEQNMASK     0x000001FF

#define I40IW_MAX_IETF_SIZE      32

#define MPA_ZERO_PAD_LEN	4

/* IETF RTR MSG Fields               */
#define IETF_PEER_TO_PEER       0x8000
#define IETF_FLPDU_ZERO_LEN     0x4000
#define IETF_RDMA0_WRITE        0x8000
#define IETF_RDMA0_READ         0x4000
#define IETF_NO_IRD_ORD         0x3FFF

/* HW-supported IRD sizes*/
#define	I40IW_HW_IRD_SETTING_2	2
#define	I40IW_HW_IRD_SETTING_4	4
#define	I40IW_HW_IRD_SETTING_8	8
#define	I40IW_HW_IRD_SETTING_16	16
#define	I40IW_HW_IRD_SETTING_32	32
#define	I40IW_HW_IRD_SETTING_64	64

enum ietf_mpa_flags {
	IETF_MPA_FLAGS_MARKERS = 0x80,	/* receive Markers */
	IETF_MPA_FLAGS_CRC = 0x40,	/* receive Markers */
	IETF_MPA_FLAGS_REJECT = 0x20,	/* Reject */
};

struct ietf_mpa_v1 {
	u8 key[IETF_MPA_KEY_SIZE];
	u8 flags;
	u8 rev;
	__be16 priv_data_len;
	u8 priv_data[0];
};

#define ietf_mpa_req_resp_frame ietf_mpa_frame

struct ietf_rtr_msg {
	__be16 ctrl_ird;
	__be16 ctrl_ord;
};

struct ietf_mpa_v2 {
	u8 key[IETF_MPA_KEY_SIZE];
	u8 flags;
	u8 rev;
	__be16 priv_data_len;
	struct ietf_rtr_msg rtr_msg;
	u8 priv_data[0];
};

struct i40iw_cm_node;
enum i40iw_timer_type {
	I40IW_TIMER_TYPE_SEND,
	I40IW_TIMER_TYPE_RECV,
	I40IW_TIMER_NODE_CLEANUP,
	I40IW_TIMER_TYPE_CLOSE,
};

#define I40IW_PASSIVE_STATE_INDICATED    0
#define I40IW_DO_NOT_SEND_RESET_EVENT    1
#define I40IW_SEND_RESET_EVENT           2

#define MAX_I40IW_IFS 4

#define SET_ACK 0x1
#define SET_SYN 0x2
#define SET_FIN 0x4
#define SET_RST 0x8

#define TCP_OPTIONS_PADDING     3

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

struct i40iw_timer_entry {
	struct list_head list;
	unsigned long timetosend;	/* jiffies */
	struct i40iw_puda_buf *sqbuf;
	u32 type;
	u32 retrycount;
	u32 retranscount;
	u32 context;
	u32 send_retrans;
	int close_when_complete;
};

#define I40IW_DEFAULT_RETRYS	64
#define I40IW_DEFAULT_RETRANS	8
#define I40IW_DEFAULT_TTL	0x40
#define I40IW_DEFAULT_RTT_VAR	0x6
#define I40IW_DEFAULT_SS_THRESH 0x3FFFFFFF
#define I40IW_DEFAULT_REXMIT_THRESH 8

#define I40IW_RETRY_TIMEOUT   HZ
#define I40IW_SHORT_TIME      10
#define I40IW_LONG_TIME       (2 * HZ)
#define I40IW_MAX_TIMEOUT     ((unsigned long)(12 * HZ))

#define I40IW_CM_HASHTABLE_SIZE         1024
#define I40IW_CM_TCP_TIMER_INTERVAL     3000
#define I40IW_CM_DEFAULT_MTU            1540
#define I40IW_CM_DEFAULT_FRAME_CNT      10
#define I40IW_CM_THREAD_STACK_SIZE      256
#define I40IW_CM_DEFAULT_RCV_WND        64240
#define I40IW_CM_DEFAULT_RCV_WND_SCALED 0x3fffc
#define I40IW_CM_DEFAULT_RCV_WND_SCALE  2
#define I40IW_CM_DEFAULT_FREE_PKTS      0x000A
#define I40IW_CM_FREE_PKT_LO_WATERMARK  2

#define I40IW_CM_DEFAULT_MSS   536

#define I40IW_CM_DEF_SEQ       0x159bf75f
#define I40IW_CM_DEF_LOCAL_ID  0x3b47

#define I40IW_CM_DEF_SEQ2      0x18ed5740
#define I40IW_CM_DEF_LOCAL_ID2 0xb807
#define MAX_CM_BUFFER   (I40IW_MAX_IETF_SIZE + IETF_MAX_PRIV_DATA_LEN)

typedef u32 i40iw_addr_t;

#define i40iw_cm_tsa_context i40iw_qp_context

struct i40iw_qp;

/* cm node transition states */
enum i40iw_cm_node_state {
	I40IW_CM_STATE_UNKNOWN,
	I40IW_CM_STATE_INITED,
	I40IW_CM_STATE_LISTENING,
	I40IW_CM_STATE_SYN_RCVD,
	I40IW_CM_STATE_SYN_SENT,
	I40IW_CM_STATE_ONE_SIDE_ESTABLISHED,
	I40IW_CM_STATE_ESTABLISHED,
	I40IW_CM_STATE_ACCEPTING,
	I40IW_CM_STATE_MPAREQ_SENT,
	I40IW_CM_STATE_MPAREQ_RCVD,
	I40IW_CM_STATE_MPAREJ_RCVD,
	I40IW_CM_STATE_OFFLOADED,
	I40IW_CM_STATE_FIN_WAIT1,
	I40IW_CM_STATE_FIN_WAIT2,
	I40IW_CM_STATE_CLOSE_WAIT,
	I40IW_CM_STATE_TIME_WAIT,
	I40IW_CM_STATE_LAST_ACK,
	I40IW_CM_STATE_CLOSING,
	I40IW_CM_STATE_LISTENER_DESTROYED,
	I40IW_CM_STATE_CLOSED
};

enum mpa_frame_version {
	IETF_MPA_V1 = 1,
	IETF_MPA_V2 = 2
};

enum mpa_frame_key {
	MPA_KEY_REQUEST,
	MPA_KEY_REPLY
};

enum send_rdma0 {
	SEND_RDMA_READ_ZERO = 1,
	SEND_RDMA_WRITE_ZERO = 2
};

enum i40iw_tcpip_pkt_type {
	I40IW_PKT_TYPE_UNKNOWN,
	I40IW_PKT_TYPE_SYN,
	I40IW_PKT_TYPE_SYNACK,
	I40IW_PKT_TYPE_ACK,
	I40IW_PKT_TYPE_FIN,
	I40IW_PKT_TYPE_RST
};

/* CM context params */
struct i40iw_cm_tcp_context {
	u8 client;

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
	u8 snd_wscale;
	u8 rcv_wscale;

	struct timeval sent_ts;
};

enum i40iw_cm_listener_state {
	I40IW_CM_LISTENER_PASSIVE_STATE = 1,
	I40IW_CM_LISTENER_ACTIVE_STATE = 2,
	I40IW_CM_LISTENER_EITHER_STATE = 3
};

struct i40iw_cm_listener {
	struct list_head list;
	struct i40iw_cm_core *cm_core;
	u8 loc_mac[ETH_ALEN];
	u32 loc_addr[4];
	u16 loc_port;
	u32 map_loc_addr[4];
	u16 map_loc_port;
	struct iw_cm_id *cm_id;
	atomic_t ref_count;
	struct i40iw_device *iwdev;
	atomic_t pend_accepts_cnt;
	int backlog;
	enum i40iw_cm_listener_state listener_state;
	u32 reused_node;
	u8 user_pri;
	u16 vlan_id;
	bool qhash_set;
	bool ipv4;
	struct list_head child_listen_list;

};

struct i40iw_kmem_info {
	void *addr;
	u32 size;
};

/* per connection node and node state information */
struct i40iw_cm_node {
	u32 loc_addr[4], rem_addr[4];
	u16 loc_port, rem_port;
	u32 map_loc_addr[4], map_rem_addr[4];
	u16 map_loc_port, map_rem_port;
	u16 vlan_id;
	enum i40iw_cm_node_state state;
	u8 loc_mac[ETH_ALEN];
	u8 rem_mac[ETH_ALEN];
	atomic_t ref_count;
	struct i40iw_qp *iwqp;
	struct i40iw_device *iwdev;
	struct i40iw_sc_dev *dev;
	struct i40iw_cm_tcp_context tcp_cntxt;
	struct i40iw_cm_core *cm_core;
	struct i40iw_cm_node *loopbackpartner;
	struct i40iw_timer_entry *send_entry;
	struct i40iw_timer_entry *close_entry;
	spinlock_t retrans_list_lock; /* cm transmit packet */
	enum send_rdma0 send_rdma0_op;
	u16 ird_size;
	u16 ord_size;
	u16     mpav2_ird_ord;
	struct iw_cm_id *cm_id;
	struct list_head list;
	int accelerated;
	struct i40iw_cm_listener *listener;
	int apbvt_set;
	int accept_pend;
	struct list_head timer_entry;
	struct list_head reset_entry;
	atomic_t passive_state;
	bool qhash_set;
	u8 user_pri;
	bool ipv4;
	bool snd_mark_en;
	u16 lsmm_size;
	enum mpa_frame_version mpa_frame_rev;
	struct i40iw_kmem_info pdata;
	union {
		struct ietf_mpa_v1 mpa_frame;
		struct ietf_mpa_v2 mpa_v2_frame;
	};

	u8 pdata_buf[IETF_MAX_PRIV_DATA_LEN];
	struct i40iw_kmem_info mpa_hdr;
};

/* structure for client or CM to fill when making CM api calls. */
/*	- only need to set relevant data, based on op. */
struct i40iw_cm_info {
	struct iw_cm_id *cm_id;
	u16 loc_port;
	u16 rem_port;
	u32 loc_addr[4];
	u32 rem_addr[4];
	u16 map_loc_port;
	u16 map_rem_port;
	u32 map_loc_addr[4];
	u32 map_rem_addr[4];
	u16 vlan_id;
	int backlog;
	u16 user_pri;
	bool ipv4;
};

/* CM event codes */
enum i40iw_cm_event_type {
	I40IW_CM_EVENT_UNKNOWN,
	I40IW_CM_EVENT_ESTABLISHED,
	I40IW_CM_EVENT_MPA_REQ,
	I40IW_CM_EVENT_MPA_CONNECT,
	I40IW_CM_EVENT_MPA_ACCEPT,
	I40IW_CM_EVENT_MPA_REJECT,
	I40IW_CM_EVENT_MPA_ESTABLISHED,
	I40IW_CM_EVENT_CONNECTED,
	I40IW_CM_EVENT_RESET,
	I40IW_CM_EVENT_ABORTED
};

/* event to post to CM event handler */
struct i40iw_cm_event {
	enum i40iw_cm_event_type type;
	struct i40iw_cm_info cm_info;
	struct work_struct event_work;
	struct i40iw_cm_node *cm_node;
};

struct i40iw_cm_core {
	struct i40iw_device *iwdev;
	struct i40iw_sc_dev *dev;

	struct list_head listen_nodes;
	struct list_head connected_nodes;

	struct timer_list tcp_timer;

	struct workqueue_struct *event_wq;
	struct workqueue_struct *disconn_wq;

	spinlock_t ht_lock; /* manage hash table */
	spinlock_t listen_list_lock; /* listen list */

	u64	stats_nodes_created;
	u64	stats_nodes_destroyed;
	u64	stats_listen_created;
	u64	stats_listen_destroyed;
	u64	stats_listen_nodes_created;
	u64	stats_listen_nodes_destroyed;
	u64	stats_loopbacks;
	u64	stats_accepts;
	u64	stats_rejects;
	u64	stats_connect_errs;
	u64	stats_passive_errs;
	u64	stats_pkt_retrans;
	u64	stats_backlog_drops;
};

int i40iw_schedule_cm_timer(struct i40iw_cm_node *cm_node,
			    struct i40iw_puda_buf *sqbuf,
			    enum i40iw_timer_type type,
			    int send_retrans,
			    int close_when_complete);

int i40iw_accept(struct iw_cm_id *, struct iw_cm_conn_param *);
int i40iw_reject(struct iw_cm_id *, const void *, u8);
int i40iw_connect(struct iw_cm_id *, struct iw_cm_conn_param *);
int i40iw_create_listen(struct iw_cm_id *, int);
int i40iw_destroy_listen(struct iw_cm_id *);

int i40iw_cm_start(struct i40iw_device *);
int i40iw_cm_stop(struct i40iw_device *);

int i40iw_arp_table(struct i40iw_device *iwdev,
		    u32 *ip_addr,
		    bool ipv4,
		    u8 *mac_addr,
		    u32 action);

#endif /* I40IW_CM_H */
