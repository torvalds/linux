/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2021 Intel Corporation */
#ifndef IRDMA_CM_H
#define IRDMA_CM_H

#define IRDMA_MPA_REQUEST_ACCEPT	1
#define IRDMA_MPA_REQUEST_REJECT	2

/* IETF MPA -- defines */
#define IEFT_MPA_KEY_REQ	"MPA ID Req Frame"
#define IEFT_MPA_KEY_REP	"MPA ID Rep Frame"
#define IETF_MPA_KEY_SIZE	16
#define IETF_MPA_VER		1
#define IETF_MAX_PRIV_DATA_LEN	512
#define IETF_MPA_FRAME_SIZE	20
#define IETF_RTR_MSG_SIZE	4
#define IETF_MPA_V2_FLAG	0x10
#define SNDMARKER_SEQNMASK	0x000001ff
#define IRDMA_MAX_IETF_SIZE	32

/* IETF RTR MSG Fields */
#define IETF_PEER_TO_PEER	0x8000
#define IETF_FLPDU_ZERO_LEN	0x4000
#define IETF_RDMA0_WRITE	0x8000
#define IETF_RDMA0_READ		0x4000
#define IETF_NO_IRD_ORD		0x3fff

#define MAX_PORTS	65536

#define IRDMA_PASSIVE_STATE_INDICATED	0
#define IRDMA_DO_NOT_SEND_RESET_EVENT	1
#define IRDMA_SEND_RESET_EVENT		2

#define MAX_IRDMA_IFS	4

#define SET_ACK		1
#define SET_SYN		2
#define SET_FIN		4
#define SET_RST		8

#define TCP_OPTIONS_PADDING	3

#define IRDMA_DEFAULT_RETRYS	64
#define IRDMA_DEFAULT_RETRANS	8
#define IRDMA_DEFAULT_TTL		0x40
#define IRDMA_DEFAULT_RTT_VAR		6
#define IRDMA_DEFAULT_SS_THRESH		0x3fffffff
#define IRDMA_DEFAULT_REXMIT_THRESH	8

#define IRDMA_RETRY_TIMEOUT	HZ
#define IRDMA_SHORT_TIME	10
#define IRDMA_LONG_TIME		(2 * HZ)
#define IRDMA_MAX_TIMEOUT	((unsigned long)(12 * HZ))

#define IRDMA_CM_HASHTABLE_SIZE		1024
#define IRDMA_CM_TCP_TIMER_INTERVAL	3000
#define IRDMA_CM_DEFAULT_MTU		1540
#define IRDMA_CM_DEFAULT_FRAME_CNT	10
#define IRDMA_CM_THREAD_STACK_SIZE	256
#define IRDMA_CM_DEFAULT_RCV_WND	64240
#define IRDMA_CM_DEFAULT_RCV_WND_SCALED	0x3FFFC
#define IRDMA_CM_DEFAULT_RCV_WND_SCALE	2
#define IRDMA_CM_DEFAULT_FREE_PKTS	10
#define IRDMA_CM_FREE_PKT_LO_WATERMARK	2
#define IRDMA_CM_DEFAULT_MSS		536
#define IRDMA_CM_DEFAULT_MPA_VER	2
#define IRDMA_CM_DEFAULT_SEQ		0x159bf75f
#define IRDMA_CM_DEFAULT_LOCAL_ID	0x3b47
#define IRDMA_CM_DEFAULT_SEQ2		0x18ed5740
#define IRDMA_CM_DEFAULT_LOCAL_ID2	0xb807
#define IRDMA_MAX_CM_BUF		(IRDMA_MAX_IETF_SIZE + IETF_MAX_PRIV_DATA_LEN)

enum ietf_mpa_flags {
	IETF_MPA_FLAGS_REJECT  = 0x20,
	IETF_MPA_FLAGS_CRC     = 0x40,
	IETF_MPA_FLAGS_MARKERS = 0x80,
};

enum irdma_timer_type {
	IRDMA_TIMER_TYPE_SEND,
	IRDMA_TIMER_TYPE_CLOSE,
};

enum option_nums {
	OPTION_NUM_EOL,
	OPTION_NUM_NONE,
	OPTION_NUM_MSS,
	OPTION_NUM_WINDOW_SCALE,
	OPTION_NUM_SACK_PERM,
	OPTION_NUM_SACK,
	OPTION_NUM_WRITE0 = 0xbc,
};

/* cm node transition states */
enum irdma_cm_node_state {
	IRDMA_CM_STATE_UNKNOWN,
	IRDMA_CM_STATE_INITED,
	IRDMA_CM_STATE_LISTENING,
	IRDMA_CM_STATE_SYN_RCVD,
	IRDMA_CM_STATE_SYN_SENT,
	IRDMA_CM_STATE_ONE_SIDE_ESTABLISHED,
	IRDMA_CM_STATE_ESTABLISHED,
	IRDMA_CM_STATE_ACCEPTING,
	IRDMA_CM_STATE_MPAREQ_SENT,
	IRDMA_CM_STATE_MPAREQ_RCVD,
	IRDMA_CM_STATE_MPAREJ_RCVD,
	IRDMA_CM_STATE_OFFLOADED,
	IRDMA_CM_STATE_FIN_WAIT1,
	IRDMA_CM_STATE_FIN_WAIT2,
	IRDMA_CM_STATE_CLOSE_WAIT,
	IRDMA_CM_STATE_TIME_WAIT,
	IRDMA_CM_STATE_LAST_ACK,
	IRDMA_CM_STATE_CLOSING,
	IRDMA_CM_STATE_LISTENER_DESTROYED,
	IRDMA_CM_STATE_CLOSED,
};

enum mpa_frame_ver {
	IETF_MPA_V1 = 1,
	IETF_MPA_V2 = 2,
};

enum mpa_frame_key {
	MPA_KEY_REQUEST,
	MPA_KEY_REPLY,
};

enum send_rdma0 {
	SEND_RDMA_READ_ZERO  = 1,
	SEND_RDMA_WRITE_ZERO = 2,
};

enum irdma_tcpip_pkt_type {
	IRDMA_PKT_TYPE_UNKNOWN,
	IRDMA_PKT_TYPE_SYN,
	IRDMA_PKT_TYPE_SYNACK,
	IRDMA_PKT_TYPE_ACK,
	IRDMA_PKT_TYPE_FIN,
	IRDMA_PKT_TYPE_RST,
};

enum irdma_cm_listener_state {
	IRDMA_CM_LISTENER_PASSIVE_STATE = 1,
	IRDMA_CM_LISTENER_ACTIVE_STATE  = 2,
	IRDMA_CM_LISTENER_EITHER_STATE  = 3,
};

/* CM event codes */
enum irdma_cm_event_type {
	IRDMA_CM_EVENT_UNKNOWN,
	IRDMA_CM_EVENT_ESTABLISHED,
	IRDMA_CM_EVENT_MPA_REQ,
	IRDMA_CM_EVENT_MPA_CONNECT,
	IRDMA_CM_EVENT_MPA_ACCEPT,
	IRDMA_CM_EVENT_MPA_REJECT,
	IRDMA_CM_EVENT_MPA_ESTABLISHED,
	IRDMA_CM_EVENT_CONNECTED,
	IRDMA_CM_EVENT_RESET,
	IRDMA_CM_EVENT_ABORTED,
};

struct ietf_mpa_v1 {
	u8 key[IETF_MPA_KEY_SIZE];
	u8 flags;
	u8 rev;
	__be16 priv_data_len;
	u8 priv_data[];
};

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
	u8 priv_data[];
};

struct option_base {
	u8 optionnum;
	u8 len;
};

struct option_mss {
	u8 optionnum;
	u8 len;
	__be16 mss;
};

struct option_windowscale {
	u8 optionnum;
	u8 len;
	u8 shiftcount;
};

union all_known_options {
	char eol;
	struct option_base base;
	struct option_mss mss;
	struct option_windowscale windowscale;
};

struct irdma_timer_entry {
	struct list_head list;
	unsigned long timetosend; /* jiffies */
	struct irdma_puda_buf *sqbuf;
	u32 type;
	u32 retrycount;
	u32 retranscount;
	u32 context;
	u32 send_retrans;
	int close_when_complete;
};

/* CM context params */
struct irdma_cm_tcp_context {
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
};

struct irdma_apbvt_entry {
	struct hlist_node hlist;
	u32 use_cnt;
	u16 port;
};

struct irdma_cm_listener {
	struct list_head list;
	struct iw_cm_id *cm_id;
	struct irdma_cm_core *cm_core;
	struct irdma_device *iwdev;
	struct list_head child_listen_list;
	struct irdma_apbvt_entry *apbvt_entry;
	enum irdma_cm_listener_state listener_state;
	refcount_t refcnt;
	atomic_t pend_accepts_cnt;
	u32 loc_addr[4];
	u32 reused_node;
	int backlog;
	u16 loc_port;
	u16 vlan_id;
	u8 loc_mac[ETH_ALEN];
	u8 user_pri;
	u8 tos;
	bool qhash_set:1;
	bool ipv4:1;
};

struct irdma_kmem_info {
	void *addr;
	u32 size;
};

struct irdma_mpa_priv_info {
	const void *addr;
	u32 size;
};

struct irdma_cm_node {
	struct irdma_qp *iwqp;
	struct irdma_device *iwdev;
	struct irdma_sc_dev *dev;
	struct irdma_cm_tcp_context tcp_cntxt;
	struct irdma_cm_core *cm_core;
	struct irdma_timer_entry *send_entry;
	struct irdma_timer_entry *close_entry;
	struct irdma_cm_listener *listener;
	struct list_head timer_entry;
	struct list_head reset_entry;
	struct list_head teardown_entry;
	struct irdma_apbvt_entry *apbvt_entry;
	struct rcu_head rcu_head;
	struct irdma_mpa_priv_info pdata;
	struct irdma_sc_ah *ah;
	union {
		struct ietf_mpa_v1 mpa_frame;
		struct ietf_mpa_v2 mpa_v2_frame;
	};
	struct irdma_kmem_info mpa_hdr;
	struct iw_cm_id *cm_id;
	struct hlist_node list;
	struct completion establish_comp;
	spinlock_t retrans_list_lock; /* protect CM node rexmit updates*/
	atomic_t passive_state;
	refcount_t refcnt;
	enum irdma_cm_node_state state;
	enum send_rdma0 send_rdma0_op;
	enum mpa_frame_ver mpa_frame_rev;
	u32 loc_addr[4], rem_addr[4];
	u16 loc_port, rem_port;
	int apbvt_set;
	int accept_pend;
	u16 vlan_id;
	u16 ird_size;
	u16 ord_size;
	u16 mpav2_ird_ord;
	u16 lsmm_size;
	u8 pdata_buf[IETF_MAX_PRIV_DATA_LEN];
	u8 loc_mac[ETH_ALEN];
	u8 rem_mac[ETH_ALEN];
	u8 user_pri;
	u8 tos;
	bool ack_rcvd:1;
	bool qhash_set:1;
	bool ipv4:1;
	bool snd_mark_en:1;
	bool rcv_mark_en:1;
	bool do_lpb:1;
	bool accelerated:1;
};

/* Used by internal CM APIs to pass CM information*/
struct irdma_cm_info {
	struct iw_cm_id *cm_id;
	u16 loc_port;
	u16 rem_port;
	u32 loc_addr[4];
	u32 rem_addr[4];
	u32 qh_qpid;
	u16 vlan_id;
	int backlog;
	u8 user_pri;
	u8 tos;
	bool ipv4;
};

struct irdma_cm_event {
	enum irdma_cm_event_type type;
	struct irdma_cm_info cm_info;
	struct work_struct event_work;
	struct irdma_cm_node *cm_node;
};

struct irdma_cm_core {
	struct irdma_device *iwdev;
	struct irdma_sc_dev *dev;
	struct list_head listen_list;
	DECLARE_HASHTABLE(cm_hash_tbl, 8);
	DECLARE_HASHTABLE(apbvt_hash_tbl, 8);
	struct timer_list tcp_timer;
	struct workqueue_struct *event_wq;
	spinlock_t ht_lock; /* protect CM node (active side) list */
	spinlock_t listen_list_lock; /* protect listener list */
	spinlock_t apbvt_lock; /*serialize apbvt add/del entries*/
	u64 stats_nodes_created;
	u64 stats_nodes_destroyed;
	u64 stats_listen_created;
	u64 stats_listen_destroyed;
	u64 stats_listen_nodes_created;
	u64 stats_listen_nodes_destroyed;
	u64 stats_lpbs;
	u64 stats_accepts;
	u64 stats_rejects;
	u64 stats_connect_errs;
	u64 stats_passive_errs;
	u64 stats_pkt_retrans;
	u64 stats_backlog_drops;
	struct irdma_puda_buf *(*form_cm_frame)(struct irdma_cm_node *cm_node,
						struct irdma_kmem_info *options,
						struct irdma_kmem_info *hdr,
						struct irdma_mpa_priv_info *pdata,
						u8 flags);
	int (*cm_create_ah)(struct irdma_cm_node *cm_node, bool wait);
	void (*cm_free_ah)(struct irdma_cm_node *cm_node);
};

int irdma_schedule_cm_timer(struct irdma_cm_node *cm_node,
			    struct irdma_puda_buf *sqbuf,
			    enum irdma_timer_type type, int send_retrans,
			    int close_when_complete);

static inline u8 irdma_tos2dscp(u8 tos)
{
#define IRDMA_DSCP_VAL GENMASK(7, 2)
	return (u8)FIELD_GET(IRDMA_DSCP_VAL, tos);
}

int irdma_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int irdma_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len);
int irdma_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int irdma_create_listen(struct iw_cm_id *cm_id, int backlog);
int irdma_destroy_listen(struct iw_cm_id *cm_id);
int irdma_add_arp(struct irdma_pci_f *rf, u32 *ip, bool ipv4, const u8 *mac);
void irdma_cm_teardown_connections(struct irdma_device *iwdev, u32 *ipaddr,
				   struct irdma_cm_info *nfo,
				   bool disconnect_all);
int irdma_cm_start(struct irdma_device *dev);
int irdma_cm_stop(struct irdma_device *dev);
bool irdma_ipv4_is_lpb(u32 loc_addr, u32 rem_addr);
bool irdma_ipv6_is_lpb(u32 *loc_addr, u32 *rem_addr);
int irdma_arp_table(struct irdma_pci_f *rf, u32 *ip_addr, bool ipv4,
		    const u8 *mac_addr, u32 action);
void irdma_if_notify(struct irdma_device *iwdev, struct net_device *netdev,
		     u32 *ipaddr, bool ipv4, bool ifup);
bool irdma_port_in_use(struct irdma_cm_core *cm_core, u16 port);
void irdma_send_ack(struct irdma_cm_node *cm_node);
void irdma_lpb_nop(struct irdma_sc_qp *qp);
void irdma_rem_ref_cm_node(struct irdma_cm_node *cm_node);
void irdma_add_conn_est_qh(struct irdma_cm_node *cm_node);
#endif /* IRDMA_CM_H */
