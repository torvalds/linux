/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#ifndef __QETH_CORE_H__
#define __QETH_CORE_H__

#include <linux/completion.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/ctype.h>
#include <linux/in6.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>
#include <linux/hashtable.h>
#include <linux/ip.h>
#include <linux/refcount.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <net/dst.h>
#include <net/ip6_fib.h>
#include <net/ipv6.h>
#include <net/if_inet6.h>
#include <net/addrconf.h>
#include <net/sch_generic.h>
#include <net/tcp.h>

#include <asm/debug.h>
#include <asm/qdio.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/sysinfo.h>

#include <uapi/linux/if_link.h>

#include "qeth_core_mpc.h"

/**
 * Debug Facility stuff
 */
enum qeth_dbf_names {
	QETH_DBF_SETUP,
	QETH_DBF_MSG,
	QETH_DBF_CTRL,
	QETH_DBF_INFOS	/* must be last element */
};

struct qeth_dbf_info {
	char name[DEBUG_MAX_NAME_LEN];
	int pages;
	int areas;
	int len;
	int level;
	struct debug_view *view;
	debug_info_t *id;
};

#define QETH_DBF_CTRL_LEN 256U

#define QETH_DBF_TEXT(name, level, text) \
	debug_text_event(qeth_dbf[QETH_DBF_##name].id, level, text)

#define QETH_DBF_HEX(name, level, addr, len) \
	debug_event(qeth_dbf[QETH_DBF_##name].id, level, (void *)(addr), len)

#define QETH_DBF_MESSAGE(level, text...) \
	debug_sprintf_event(qeth_dbf[QETH_DBF_MSG].id, level, text)

#define QETH_DBF_TEXT_(name, level, text...) \
	qeth_dbf_longtext(qeth_dbf[QETH_DBF_##name].id, level, text)

#define QETH_CARD_TEXT(card, level, text) \
	debug_text_event(card->debug, level, text)

#define QETH_CARD_HEX(card, level, addr, len) \
	debug_event(card->debug, level, (void *)(addr), len)

#define QETH_CARD_MESSAGE(card, text...) \
	debug_sprintf_event(card->debug, level, text)

#define QETH_CARD_TEXT_(card, level, text...) \
	qeth_dbf_longtext(card->debug, level, text)

#define SENSE_COMMAND_REJECT_BYTE 0
#define SENSE_COMMAND_REJECT_FLAG 0x80
#define SENSE_RESETTING_EVENT_BYTE 1
#define SENSE_RESETTING_EVENT_FLAG 0x80

static inline u32 qeth_get_device_id(struct ccw_device *cdev)
{
	struct ccw_dev_id dev_id;
	u32 id;

	ccw_device_get_id(cdev, &dev_id);
	id = dev_id.devno;
	id |= (u32) (dev_id.ssid << 16);

	return id;
}

/*
 * Common IO related definitions
 */
#define CARD_RDEV(card) card->read.ccwdev
#define CARD_WDEV(card) card->write.ccwdev
#define CARD_DDEV(card) card->data.ccwdev
#define CARD_BUS_ID(card) dev_name(&card->gdev->dev)
#define CARD_RDEV_ID(card) dev_name(&card->read.ccwdev->dev)
#define CARD_WDEV_ID(card) dev_name(&card->write.ccwdev->dev)
#define CARD_DDEV_ID(card) dev_name(&card->data.ccwdev->dev)
#define CCW_DEVID(cdev)		(qeth_get_device_id(cdev))
#define CARD_DEVID(card)	(CCW_DEVID(CARD_RDEV(card)))

/* Routing stuff */
struct qeth_routing_info {
	enum qeth_routing_types type;
};

/* IPA stuff */
struct qeth_ipa_info {
	__u32 supported_funcs;
	__u32 enabled_funcs;
};

/* SETBRIDGEPORT stuff */
enum qeth_sbp_roles {
	QETH_SBP_ROLE_NONE	= 0,
	QETH_SBP_ROLE_PRIMARY	= 1,
	QETH_SBP_ROLE_SECONDARY	= 2,
};

enum qeth_sbp_states {
	QETH_SBP_STATE_INACTIVE	= 0,
	QETH_SBP_STATE_STANDBY	= 1,
	QETH_SBP_STATE_ACTIVE	= 2,
};

#define QETH_SBP_HOST_NOTIFICATION 1

struct qeth_sbp_info {
	__u32 supported_funcs;
	enum qeth_sbp_roles role;
	__u32 hostnotification:1;
	__u32 reflect_promisc:1;
	__u32 reflect_promisc_primary:1;
};

struct qeth_vnicc_info {
	/* supported/currently configured VNICCs; updated in IPA exchanges */
	u32 sup_chars;
	u32 cur_chars;
	/* supported commands: bitmasks which VNICCs support respective cmd */
	u32 set_char_sup;
	u32 getset_timeout_sup;
	/* timeout value for the learning characteristic */
	u32 learning_timeout;
	/* characteristics wanted/configured by user */
	u32 wanted_chars;
	/* has user explicitly enabled rx_bcast while online? */
	bool rx_bcast_enabled;
};

static inline int qeth_is_adp_supported(struct qeth_ipa_info *ipa,
		enum qeth_ipa_setadp_cmd func)
{
	return (ipa->supported_funcs & func);
}

static inline int qeth_is_ipa_supported(struct qeth_ipa_info *ipa,
		enum qeth_ipa_funcs func)
{
	return (ipa->supported_funcs & func);
}

static inline int qeth_is_ipa_enabled(struct qeth_ipa_info *ipa,
		enum qeth_ipa_funcs func)
{
	return (ipa->supported_funcs & ipa->enabled_funcs & func);
}

#define qeth_adp_supported(c, f) \
	qeth_is_adp_supported(&c->options.adp, f)
#define qeth_is_supported(c, f) \
	qeth_is_ipa_supported(&c->options.ipa4, f)
#define qeth_is_enabled(c, f) \
	qeth_is_ipa_enabled(&c->options.ipa4, f)
#define qeth_is_supported6(c, f) \
	qeth_is_ipa_supported(&c->options.ipa6, f)
#define qeth_is_enabled6(c, f) \
	qeth_is_ipa_enabled(&c->options.ipa6, f)
#define qeth_is_ipafunc_supported(c, prot, f) \
	 ((prot == QETH_PROT_IPV6) ? \
		qeth_is_supported6(c, f) : qeth_is_supported(c, f))
#define qeth_is_ipafunc_enabled(c, prot, f) \
	 ((prot == QETH_PROT_IPV6) ? \
		qeth_is_enabled6(c, f) : qeth_is_enabled(c, f))

#define QETH_IDX_FUNC_LEVEL_OSD		 0x0101
#define QETH_IDX_FUNC_LEVEL_IQD		 0x4108

#define QETH_BUFSIZE		4096
#define CCW_CMD_WRITE		0x01
#define CCW_CMD_READ		0x02

/**
 * some more defs
 */
#define QETH_TX_TIMEOUT		100 * HZ
#define QETH_RCD_TIMEOUT	60 * HZ
#define QETH_RECLAIM_WORK_TIME	HZ
#define QETH_MAX_PORTNO		15

/*IPv6 address autoconfiguration stuff*/
#define UNIQUE_ID_IF_CREATE_ADDR_FAILED 0xfffe
#define UNIQUE_ID_NOT_BY_CARD		0x10000

/*****************************************************************************/
/* QDIO queue and buffer handling                                            */
/*****************************************************************************/
#define QETH_MAX_QUEUES 4
#define QETH_IQD_MIN_TXQ	2	/* One for ucast, one for mcast. */
#define QETH_IQD_MCAST_TXQ	0
#define QETH_IQD_MIN_UCAST_TXQ	1
#define QETH_IN_BUF_SIZE_DEFAULT 65536
#define QETH_IN_BUF_COUNT_DEFAULT 64
#define QETH_IN_BUF_COUNT_HSDEFAULT 128
#define QETH_IN_BUF_COUNT_MIN 8
#define QETH_IN_BUF_COUNT_MAX 128
#define QETH_MAX_BUFFER_ELEMENTS(card) ((card)->qdio.in_buf_size >> 12)
#define QETH_IN_BUF_REQUEUE_THRESHOLD(card) \
		 ((card)->qdio.in_buf_pool.buf_count / 2)

/* buffers we have to be behind before we get a PCI */
#define QETH_PCI_THRESHOLD_A(card) ((card)->qdio.in_buf_pool.buf_count+1)
/*enqueued free buffers left before we get a PCI*/
#define QETH_PCI_THRESHOLD_B(card) 0
/*not used unless the microcode gets patched*/
#define QETH_PCI_TIMER_VALUE(card) 3

/* priority queing */
#define QETH_PRIOQ_DEFAULT QETH_NO_PRIO_QUEUEING
#define QETH_DEFAULT_QUEUE    2
#define QETH_NO_PRIO_QUEUEING 0
#define QETH_PRIO_Q_ING_PREC  1
#define QETH_PRIO_Q_ING_TOS   2
#define QETH_PRIO_Q_ING_SKB   3
#define QETH_PRIO_Q_ING_VLAN  4

/* Packing */
#define QETH_LOW_WATERMARK_PACK  2
#define QETH_HIGH_WATERMARK_PACK 5
#define QETH_WATERMARK_PACK_FUZZ 1

/* large receive scatter gather copy break */
#define QETH_RX_SG_CB (PAGE_SIZE >> 1)
#define QETH_RX_PULL_LEN 256

struct qeth_hdr_layer3 {
	__u8  id;
	__u8  flags;
	__u16 inbound_checksum; /*TSO:__u16 seqno */
	__u32 token;		/*TSO: __u32 reserved */
	__u16 length;
	__u8  vlan_prio;
	__u8  ext_flags;
	__u16 vlan_id;
	__u16 frame_offset;
	union {
		/* TX: */
		struct in6_addr ipv6_addr;
		struct ipv4 {
			u8 res[12];
			u32 addr;
		} ipv4;
		/* RX: */
		struct rx {
			u8 res1[2];
			u8 src_mac[6];
			u8 res2[4];
			u16 vlan_id;
			u8 res3[2];
		} rx;
	} next_hop;
};

struct qeth_hdr_layer2 {
	__u8 id;
	__u8 flags[3];
	__u8 port_no;
	__u8 hdr_length;
	__u16 pkt_length;
	__u16 seq_no;
	__u16 vlan_id;
	__u32 reserved;
	__u8 reserved2[16];
} __attribute__ ((packed));

struct qeth_hdr_osn {
	__u8 id;
	__u8 reserved;
	__u16 seq_no;
	__u16 reserved2;
	__u16 control_flags;
	__u16 pdu_length;
	__u8 reserved3[18];
	__u32 ccid;
} __attribute__ ((packed));

struct qeth_hdr {
	union {
		struct qeth_hdr_layer2 l2;
		struct qeth_hdr_layer3 l3;
		struct qeth_hdr_osn    osn;
	} hdr;
} __attribute__ ((packed));

/*TCP Segmentation Offload header*/
struct qeth_hdr_ext_tso {
	__u16 hdr_tot_len;
	__u8  imb_hdr_no;
	__u8  reserved;
	__u8  hdr_type;
	__u8  hdr_version;
	__u16 hdr_len;
	__u32 payload_len;
	__u16 mss;
	__u16 dg_hdr_len;
	__u8  padding[16];
} __attribute__ ((packed));

struct qeth_hdr_tso {
	struct qeth_hdr hdr;	/*hdr->hdr.l3.xxx*/
	struct qeth_hdr_ext_tso ext;
} __attribute__ ((packed));


/* flags for qeth_hdr.flags */
#define QETH_HDR_PASSTHRU 0x10
#define QETH_HDR_IPV6     0x80
#define QETH_HDR_CAST_MASK 0x07
enum qeth_cast_flags {
	QETH_CAST_UNICAST   = 0x06,
	QETH_CAST_MULTICAST = 0x04,
	QETH_CAST_BROADCAST = 0x05,
	QETH_CAST_ANYCAST   = 0x07,
	QETH_CAST_NOCAST    = 0x00,
};

enum qeth_layer2_frame_flags {
	QETH_LAYER2_FLAG_MULTICAST = 0x01,
	QETH_LAYER2_FLAG_BROADCAST = 0x02,
	QETH_LAYER2_FLAG_UNICAST   = 0x04,
	QETH_LAYER2_FLAG_VLAN      = 0x10,
};

enum qeth_header_ids {
	QETH_HEADER_TYPE_LAYER3 = 0x01,
	QETH_HEADER_TYPE_LAYER2 = 0x02,
	QETH_HEADER_TYPE_L3_TSO	= 0x03,
	QETH_HEADER_TYPE_OSN    = 0x04,
	QETH_HEADER_TYPE_L2_TSO	= 0x06,
	QETH_HEADER_MASK_INVAL	= 0x80,
};
/* flags for qeth_hdr.ext_flags */
#define QETH_HDR_EXT_VLAN_FRAME       0x01
#define QETH_HDR_EXT_TOKEN_ID         0x02
#define QETH_HDR_EXT_INCLUDE_VLAN_TAG 0x04
#define QETH_HDR_EXT_SRC_MAC_ADDR     0x08
#define QETH_HDR_EXT_CSUM_HDR_REQ     0x10
#define QETH_HDR_EXT_CSUM_TRANSP_REQ  0x20
#define QETH_HDR_EXT_UDP	      0x40 /*bit off for TCP*/

static inline bool qeth_l2_same_vlan(struct qeth_hdr_layer2 *h1,
				     struct qeth_hdr_layer2 *h2)
{
	return !((h1->flags[2] ^ h2->flags[2]) & QETH_LAYER2_FLAG_VLAN) &&
	       h1->vlan_id == h2->vlan_id;
}

static inline bool qeth_l3_iqd_same_vlan(struct qeth_hdr_layer3 *h1,
					 struct qeth_hdr_layer3 *h2)
{
	return !((h1->ext_flags ^ h2->ext_flags) & QETH_HDR_EXT_VLAN_FRAME) &&
	       h1->vlan_id == h2->vlan_id;
}

static inline bool qeth_l3_same_next_hop(struct qeth_hdr_layer3 *h1,
					 struct qeth_hdr_layer3 *h2)
{
	return !((h1->flags ^ h2->flags) & QETH_HDR_IPV6) &&
	       ipv6_addr_equal(&h1->next_hop.ipv6_addr,
			       &h2->next_hop.ipv6_addr);
}

enum qeth_qdio_info_states {
	QETH_QDIO_UNINITIALIZED,
	QETH_QDIO_ALLOCATED,
	QETH_QDIO_ESTABLISHED,
	QETH_QDIO_CLEANING
};

struct qeth_buffer_pool_entry {
	struct list_head list;
	struct list_head init_list;
	void *elements[QDIO_MAX_ELEMENTS_PER_BUFFER];
};

struct qeth_qdio_buffer_pool {
	struct list_head entry_list;
	int buf_count;
};

struct qeth_qdio_buffer {
	struct qdio_buffer *buffer;
	/* the buffer pool entry currently associated to this buffer */
	struct qeth_buffer_pool_entry *pool_entry;
	struct sk_buff *rx_skb;
};

struct qeth_qdio_q {
	struct qdio_buffer *qdio_bufs[QDIO_MAX_BUFFERS_PER_Q];
	struct qeth_qdio_buffer bufs[QDIO_MAX_BUFFERS_PER_Q];
	int next_buf_to_init;
};

enum qeth_qdio_out_buffer_state {
	/* Owned by driver, in order to be filled. */
	QETH_QDIO_BUF_EMPTY,
	/* Filled by driver; owned by hardware in order to be sent. */
	QETH_QDIO_BUF_PRIMED,
	/* Identified to be pending in TPQ. */
	QETH_QDIO_BUF_PENDING,
	/* Found in completion queue. */
	QETH_QDIO_BUF_IN_CQ,
	/* Handled via transfer pending / completion queue. */
	QETH_QDIO_BUF_HANDLED_DELAYED,
};

struct qeth_qdio_out_buffer {
	struct qdio_buffer *buffer;
	atomic_t state;
	int next_element_to_fill;
	unsigned int bytes;
	struct sk_buff_head skb_list;
	int is_header[QDIO_MAX_ELEMENTS_PER_BUFFER];

	struct qeth_qdio_out_q *q;
	struct qeth_qdio_out_buffer *next_pending;
};

struct qeth_card;

enum qeth_out_q_states {
       QETH_OUT_Q_UNLOCKED,
       QETH_OUT_Q_LOCKED,
       QETH_OUT_Q_LOCKED_FLUSH,
};

#define QETH_CARD_STAT_ADD(_c, _stat, _val)	((_c)->stats._stat += (_val))
#define QETH_CARD_STAT_INC(_c, _stat)		QETH_CARD_STAT_ADD(_c, _stat, 1)

#define QETH_TXQ_STAT_ADD(_q, _stat, _val)	((_q)->stats._stat += (_val))
#define QETH_TXQ_STAT_INC(_q, _stat)		QETH_TXQ_STAT_ADD(_q, _stat, 1)

struct qeth_card_stats {
	u64 rx_bufs;
	u64 rx_skb_csum;
	u64 rx_sg_skbs;
	u64 rx_sg_frags;
	u64 rx_sg_alloc_page;

	u64 rx_dropped_nomem;
	u64 rx_dropped_notsupp;
	u64 rx_dropped_runt;

	/* rtnl_link_stats64 */
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_multicast;
	u64 rx_length_errors;
	u64 rx_frame_errors;
	u64 rx_fifo_errors;
};

struct qeth_out_q_stats {
	u64 bufs;
	u64 bufs_pack;
	u64 buf_elements;
	u64 skbs_pack;
	u64 skbs_sg;
	u64 skbs_csum;
	u64 skbs_tso;
	u64 skbs_linearized;
	u64 skbs_linearized_fail;
	u64 tso_bytes;
	u64 packing_mode_switch;
	u64 stopped;
	u64 completion_yield;
	u64 completion_timer;

	/* rtnl_link_stats64 */
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_errors;
	u64 tx_dropped;
};

#define QETH_TX_TIMER_USECS		500

struct qeth_qdio_out_q {
	struct qdio_buffer *qdio_bufs[QDIO_MAX_BUFFERS_PER_Q];
	struct qeth_qdio_out_buffer *bufs[QDIO_MAX_BUFFERS_PER_Q];
	struct qdio_outbuf_state *bufstates; /* convenience pointer */
	struct qeth_out_q_stats stats;
	u8 next_buf_to_fill;
	u8 max_elements;
	u8 queue_no;
	u8 do_pack;
	struct qeth_card *card;
	atomic_t state;
	/*
	 * number of buffers that are currently filled (PRIMED)
	 * -> these buffers are hardware-owned
	 */
	atomic_t used_buffers;
	/* indicates whether PCI flag must be set (or if one is outstanding) */
	atomic_t set_pci_flags_count;
	struct napi_struct napi;
	struct timer_list timer;
	struct qeth_hdr *prev_hdr;
	u8 bulk_start;
	u8 bulk_count;
	u8 bulk_max;
};

#define qeth_for_each_output_queue(card, q, i)		\
	for (i = 0; i < card->qdio.no_out_queues &&	\
		    (q = card->qdio.out_qs[i]); i++)

#define	qeth_napi_to_out_queue(n) container_of(n, struct qeth_qdio_out_q, napi)

static inline void qeth_tx_arm_timer(struct qeth_qdio_out_q *queue)
{
	if (timer_pending(&queue->timer))
		return;
	mod_timer(&queue->timer, usecs_to_jiffies(QETH_TX_TIMER_USECS) +
				 jiffies);
}

static inline bool qeth_out_queue_is_full(struct qeth_qdio_out_q *queue)
{
	return atomic_read(&queue->used_buffers) >= QDIO_MAX_BUFFERS_PER_Q;
}

static inline bool qeth_out_queue_is_empty(struct qeth_qdio_out_q *queue)
{
	return atomic_read(&queue->used_buffers) == 0;
}

struct qeth_qdio_info {
	atomic_t state;
	/* input */
	int no_in_queues;
	struct qeth_qdio_q *in_q;
	struct qeth_qdio_q *c_q;
	struct qeth_qdio_buffer_pool in_buf_pool;
	struct qeth_qdio_buffer_pool init_pool;
	int in_buf_size;

	/* output */
	int no_out_queues;
	struct qeth_qdio_out_q *out_qs[QETH_MAX_QUEUES];
	struct qdio_outbuf_state *out_bufstates;

	/* priority queueing */
	int do_prio_queueing;
	int default_out_queue;
};

/**
 *  channel state machine
 */
enum qeth_channel_states {
	CH_STATE_UP,
	CH_STATE_DOWN,
	CH_STATE_HALTED,
	CH_STATE_STOPPED,
};
/**
 * card state machine
 */
enum qeth_card_states {
	CARD_STATE_DOWN,
	CARD_STATE_HARDSETUP,
	CARD_STATE_SOFTSETUP,
};

/**
 * Protocol versions
 */
enum qeth_prot_versions {
	QETH_PROT_NONE = 0x0000,
	QETH_PROT_IPV4 = 0x0004,
	QETH_PROT_IPV6 = 0x0006,
};

enum qeth_cq {
	QETH_CQ_DISABLED = 0,
	QETH_CQ_ENABLED = 1,
	QETH_CQ_NOTAVAILABLE = 2,
};

struct qeth_ipato {
	bool enabled;
	bool invert4;
	bool invert6;
	struct list_head entries;
};

struct qeth_channel {
	struct ccw_device *ccwdev;
	struct qeth_cmd_buffer *active_cmd;
	enum qeth_channel_states state;
	atomic_t irq_pending;
};

struct qeth_reply {
	int (*callback)(struct qeth_card *card, struct qeth_reply *reply,
			unsigned long data);
	void *param;
};

struct qeth_cmd_buffer {
	struct list_head list;
	struct completion done;
	spinlock_t lock;
	unsigned int length;
	refcount_t ref_count;
	struct qeth_channel *channel;
	struct qeth_reply reply;
	long timeout;
	unsigned char *data;
	void (*finalize)(struct qeth_card *card, struct qeth_cmd_buffer *iob);
	void (*callback)(struct qeth_card *card, struct qeth_cmd_buffer *iob,
			 unsigned int data_length);
	int rc;
};

static inline void qeth_get_cmd(struct qeth_cmd_buffer *iob)
{
	refcount_inc(&iob->ref_count);
}

static inline struct qeth_ipa_cmd *__ipa_cmd(struct qeth_cmd_buffer *iob)
{
	return (struct qeth_ipa_cmd *)(iob->data + IPA_PDU_HEADER_SIZE);
}

static inline struct ccw1 *__ccw_from_cmd(struct qeth_cmd_buffer *iob)
{
	return (struct ccw1 *)(iob->data + ALIGN(iob->length, 8));
}

static inline bool qeth_trylock_channel(struct qeth_channel *channel)
{
	return atomic_cmpxchg(&channel->irq_pending, 0, 1) == 0;
}

/**
 *  OSA card related definitions
 */
struct qeth_token {
	__u32 issuer_rm_w;
	__u32 issuer_rm_r;
	__u32 cm_filter_w;
	__u32 cm_filter_r;
	__u32 cm_connection_w;
	__u32 cm_connection_r;
	__u32 ulp_filter_w;
	__u32 ulp_filter_r;
	__u32 ulp_connection_w;
	__u32 ulp_connection_r;
};

struct qeth_seqno {
	__u32 trans_hdr;
	__u32 pdu_hdr;
	__u32 pdu_hdr_ack;
	__u16 ipa;
};

struct qeth_card_blkt {
	int time_total;
	int inter_packet;
	int inter_packet_jumbo;
};

#define QETH_BROADCAST_WITH_ECHO    0x01
#define QETH_BROADCAST_WITHOUT_ECHO 0x02
#define QETH_LAYER2_MAC_REGISTERED  0x02
struct qeth_card_info {
	unsigned short unit_addr2;
	unsigned short cula;
	u8 chpid;
	__u16 func_level;
	char mcl_level[QETH_MCL_LENGTH + 1];
	u8 open_when_online:1;
	u8 promisc_mode:1;
	u8 use_v1_blkt:1;
	u8 is_vm_nic:1;
	int mac_bits;
	enum qeth_card_types type;
	enum qeth_link_types link_type;
	int broadcast_capable;
	int unique_id;
	bool layer_enforced;
	struct qeth_card_blkt blkt;
	__u32 diagass_support;
	__u32 hwtrap;
};

enum qeth_discipline_id {
	QETH_DISCIPLINE_UNDETERMINED = -1,
	QETH_DISCIPLINE_LAYER3 = 0,
	QETH_DISCIPLINE_LAYER2 = 1,
};

struct qeth_card_options {
	struct qeth_routing_info route4;
	struct qeth_ipa_info ipa4;
	struct qeth_ipa_info adp; /*Adapter parameters*/
	struct qeth_routing_info route6;
	struct qeth_ipa_info ipa6;
	struct qeth_sbp_info sbp; /* SETBRIDGEPORT options */
	struct qeth_vnicc_info vnicc; /* VNICC options */
	int fake_broadcast;
	enum qeth_discipline_id layer;
	int rx_sg_cb;
	enum qeth_ipa_isolation_modes isolation;
	enum qeth_ipa_isolation_modes prev_isolation;
	int sniffer;
	enum qeth_cq cq;
	char hsuid[9];
};

#define	IS_LAYER2(card)	((card)->options.layer == QETH_DISCIPLINE_LAYER2)
#define	IS_LAYER3(card)	((card)->options.layer == QETH_DISCIPLINE_LAYER3)

/*
 * thread bits for qeth_card thread masks
 */
enum qeth_threads {
	QETH_RECOVER_THREAD = 1,
};

struct qeth_osn_info {
	int (*assist_cb)(struct net_device *dev, void *data);
	int (*data_cb)(struct sk_buff *skb);
};

struct qeth_discipline {
	const struct device_type *devtype;
	int (*process_rx_buffer)(struct qeth_card *card, int budget, int *done);
	int (*recover)(void *ptr);
	int (*setup) (struct ccwgroup_device *);
	void (*remove) (struct ccwgroup_device *);
	int (*set_online) (struct ccwgroup_device *);
	int (*set_offline) (struct ccwgroup_device *);
	int (*do_ioctl)(struct net_device *dev, struct ifreq *rq, int cmd);
	int (*control_event_handler)(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd);
};

enum qeth_addr_disposition {
	QETH_DISP_ADDR_DELETE = 0,
	QETH_DISP_ADDR_DO_NOTHING = 1,
	QETH_DISP_ADDR_ADD = 2,
};

struct qeth_rx {
	int b_count;
	int b_index;
	struct qdio_buffer_element *b_element;
	int e_offset;
	int qdio_err;
};

struct carrier_info {
	__u8  card_type;
	__u16 port_mode;
	__u32 port_speed;
};

struct qeth_switch_info {
	__u32 capabilities;
	__u32 settings;
};

#define QETH_NAPI_WEIGHT NAPI_POLL_WEIGHT

struct qeth_card {
	enum qeth_card_states state;
	spinlock_t lock;
	struct ccwgroup_device *gdev;
	struct qeth_cmd_buffer *read_cmd;
	struct qeth_channel read;
	struct qeth_channel write;
	struct qeth_channel data;

	struct net_device *dev;
	struct qeth_card_stats stats;
	struct qeth_card_info info;
	struct qeth_token token;
	struct qeth_seqno seqno;
	struct qeth_card_options options;

	struct workqueue_struct *event_wq;
	struct workqueue_struct *cmd_wq;
	wait_queue_head_t wait_q;
	DECLARE_HASHTABLE(mac_htable, 4);
	DECLARE_HASHTABLE(ip_htable, 4);
	struct mutex ip_lock;
	DECLARE_HASHTABLE(ip_mc_htable, 4);
	struct work_struct rx_mode_work;
	struct work_struct kernel_thread_starter;
	spinlock_t thread_mask_lock;
	unsigned long thread_start_mask;
	unsigned long thread_allowed_mask;
	unsigned long thread_running_mask;
	struct qeth_ipato ipato;
	struct list_head cmd_waiter_list;
	/* QDIO buffer handling */
	struct qeth_qdio_info qdio;
	int read_or_write_problem;
	struct qeth_osn_info osn_info;
	struct qeth_discipline *discipline;
	atomic_t force_alloc_skb;
	struct service_level qeth_service_level;
	struct qdio_ssqd_desc ssqd;
	debug_info_t *debug;
	struct mutex sbp_lock;
	struct mutex conf_mutex;
	struct mutex discipline_mutex;
	struct napi_struct napi;
	struct qeth_rx rx;
	struct delayed_work buffer_reclaim_work;
	int reclaim_index;
	struct work_struct close_dev_work;
};

static inline bool qeth_card_hw_is_reachable(struct qeth_card *card)
{
	return card->state == CARD_STATE_SOFTSETUP;
}

struct qeth_trap_id {
	__u16 lparnr;
	char vmname[8];
	__u8 chpid;
	__u8 ssid;
	__u16 devno;
} __packed;

/*some helper functions*/
#define QETH_CARD_IFNAME(card) (((card)->dev)? (card)->dev->name : "")

static inline bool qeth_netdev_is_registered(struct net_device *dev)
{
	return dev->netdev_ops != NULL;
}

static inline u16 qeth_iqd_translate_txq(struct net_device *dev, u16 txq)
{
	if (txq == QETH_IQD_MCAST_TXQ)
		return dev->num_tx_queues - 1;
	if (txq == dev->num_tx_queues - 1)
		return QETH_IQD_MCAST_TXQ;
	return txq;
}

static inline bool qeth_iqd_is_mcast_queue(struct qeth_card *card,
					   struct qeth_qdio_out_q *queue)
{
	return qeth_iqd_translate_txq(card->dev, queue->queue_no) ==
	       QETH_IQD_MCAST_TXQ;
}

static inline void qeth_scrub_qdio_buffer(struct qdio_buffer *buf,
					  unsigned int elements)
{
	unsigned int i;

	for (i = 0; i < elements; i++)
		memset(&buf->element[i], 0, sizeof(struct qdio_buffer_element));
	buf->element[14].sflags = 0;
	buf->element[15].sflags = 0;
}

/**
 * qeth_get_elements_for_range() -	find number of SBALEs to cover range.
 * @start:				Start of the address range.
 * @end:				Address after the end of the range.
 *
 * Returns the number of pages, and thus QDIO buffer elements, needed to cover
 * the specified address range.
 */
static inline int qeth_get_elements_for_range(addr_t start, addr_t end)
{
	return PFN_UP(end) - PFN_DOWN(start);
}

static inline int qeth_get_ip_version(struct sk_buff *skb)
{
	struct vlan_ethhdr *veth = vlan_eth_hdr(skb);
	__be16 prot = veth->h_vlan_proto;

	if (prot == htons(ETH_P_8021Q))
		prot = veth->h_vlan_encapsulated_proto;

	switch (prot) {
	case htons(ETH_P_IPV6):
		return 6;
	case htons(ETH_P_IP):
		return 4;
	default:
		return 0;
	}
}

static inline int qeth_get_ether_cast_type(struct sk_buff *skb)
{
	u8 *addr = eth_hdr(skb)->h_dest;

	if (is_multicast_ether_addr(addr))
		return is_broadcast_ether_addr(addr) ? RTN_BROADCAST :
						       RTN_MULTICAST;
	return RTN_UNICAST;
}

static inline struct dst_entry *qeth_dst_check_rcu(struct sk_buff *skb, int ipv)
{
	struct dst_entry *dst = skb_dst(skb);
	struct rt6_info *rt;

	rt = (struct rt6_info *) dst;
	if (dst)
		dst = dst_check(dst, (ipv == 6) ? rt6_get_cookie(rt) : 0);
	return dst;
}

static inline void qeth_rx_csum(struct qeth_card *card, struct sk_buff *skb,
				u8 flags)
{
	if ((card->dev->features & NETIF_F_RXCSUM) &&
	    (flags & QETH_HDR_EXT_CSUM_TRANSP_REQ)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		QETH_CARD_STAT_INC(card, rx_skb_csum);
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}
}

static inline void qeth_tx_csum(struct sk_buff *skb, u8 *flags, int ipv)
{
	*flags |= QETH_HDR_EXT_CSUM_TRANSP_REQ;
	if ((ipv == 4 && ip_hdr(skb)->protocol == IPPROTO_UDP) ||
	    (ipv == 6 && ipv6_hdr(skb)->nexthdr == IPPROTO_UDP))
		*flags |= QETH_HDR_EXT_UDP;
}

static inline void qeth_put_buffer_pool_entry(struct qeth_card *card,
		struct qeth_buffer_pool_entry *entry)
{
	list_add_tail(&entry->list, &card->qdio.in_buf_pool.entry_list);
}

static inline int qeth_is_diagass_supported(struct qeth_card *card,
		enum qeth_diags_cmds cmd)
{
	return card->info.diagass_support & (__u32)cmd;
}

int qeth_send_simple_setassparms_prot(struct qeth_card *card,
				      enum qeth_ipa_funcs ipa_func,
				      u16 cmd_code, u32 *data,
				      enum qeth_prot_versions prot);
/* IPv4 variant */
static inline int qeth_send_simple_setassparms(struct qeth_card *card,
					       enum qeth_ipa_funcs ipa_func,
					       u16 cmd_code, u32 *data)
{
	return qeth_send_simple_setassparms_prot(card, ipa_func, cmd_code,
						 data, QETH_PROT_IPV4);
}

static inline int qeth_send_simple_setassparms_v6(struct qeth_card *card,
						  enum qeth_ipa_funcs ipa_func,
						  u16 cmd_code, u32 *data)
{
	return qeth_send_simple_setassparms_prot(card, ipa_func, cmd_code,
						 data, QETH_PROT_IPV6);
}

int qeth_get_priority_queue(struct qeth_card *card, struct sk_buff *skb);

extern struct qeth_discipline qeth_l2_discipline;
extern struct qeth_discipline qeth_l3_discipline;
extern const struct ethtool_ops qeth_ethtool_ops;
extern const struct ethtool_ops qeth_osn_ethtool_ops;
extern const struct attribute_group *qeth_generic_attr_groups[];
extern const struct attribute_group *qeth_osn_attr_groups[];
extern const struct attribute_group qeth_device_attr_group;
extern const struct attribute_group qeth_device_blkt_group;
extern const struct device_type qeth_generic_devtype;

const char *qeth_get_cardname_short(struct qeth_card *);
int qeth_realloc_buffer_pool(struct qeth_card *, int);
int qeth_core_load_discipline(struct qeth_card *, enum qeth_discipline_id);
void qeth_core_free_discipline(struct qeth_card *);

/* exports for qeth discipline device drivers */
extern struct kmem_cache *qeth_core_header_cache;
extern struct qeth_dbf_info qeth_dbf[QETH_DBF_INFOS];

struct net_device *qeth_clone_netdev(struct net_device *orig);
struct qeth_card *qeth_get_card_by_busid(char *bus_id);
void qeth_set_allowed_threads(struct qeth_card *, unsigned long , int);
int qeth_threads_running(struct qeth_card *, unsigned long);
int qeth_do_run_thread(struct qeth_card *, unsigned long);
void qeth_clear_thread_start_bit(struct qeth_card *, unsigned long);
void qeth_clear_thread_running_bit(struct qeth_card *, unsigned long);
int qeth_core_hardsetup_card(struct qeth_card *card, bool *carrier_ok);
int qeth_stop_channel(struct qeth_channel *channel);

void qeth_print_status_message(struct qeth_card *);
int qeth_init_qdio_queues(struct qeth_card *);
int qeth_send_ipa_cmd(struct qeth_card *, struct qeth_cmd_buffer *,
		  int (*reply_cb)
		  (struct qeth_card *, struct qeth_reply *, unsigned long),
		  void *);
struct qeth_cmd_buffer *qeth_ipa_alloc_cmd(struct qeth_card *card,
					   enum qeth_ipa_cmds cmd_code,
					   enum qeth_prot_versions prot,
					   unsigned int data_length);
struct qeth_cmd_buffer *qeth_alloc_cmd(struct qeth_channel *channel,
				       unsigned int length, unsigned int ccws,
				       long timeout);
struct qeth_cmd_buffer *qeth_get_setassparms_cmd(struct qeth_card *card,
						 enum qeth_ipa_funcs ipa_func,
						 u16 cmd_code,
						 unsigned int data_length,
						 enum qeth_prot_versions prot);
struct qeth_cmd_buffer *qeth_get_diag_cmd(struct qeth_card *card,
					  enum qeth_diags_cmds sub_cmd,
					  unsigned int data_length);
void qeth_notify_cmd(struct qeth_cmd_buffer *iob, int reason);
void qeth_put_cmd(struct qeth_cmd_buffer *iob);

struct sk_buff *qeth_core_get_next_skb(struct qeth_card *,
		struct qeth_qdio_buffer *, struct qdio_buffer_element **, int *,
		struct qeth_hdr **);
void qeth_schedule_recovery(struct qeth_card *);
int qeth_poll(struct napi_struct *napi, int budget);
void qeth_clear_ipacmd_list(struct qeth_card *);
int qeth_qdio_clear_card(struct qeth_card *, int);
void qeth_clear_working_pool_list(struct qeth_card *);
void qeth_drain_output_queues(struct qeth_card *card);
void qeth_setadp_promisc_mode(struct qeth_card *card, bool enable);
int qeth_setadpparms_change_macaddr(struct qeth_card *);
void qeth_tx_timeout(struct net_device *);
void qeth_prepare_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
			  u16 cmd_length);
int qeth_query_switch_attributes(struct qeth_card *card,
				  struct qeth_switch_info *sw_info);
int qeth_query_card_info(struct qeth_card *card,
			 struct carrier_info *carrier_info);
unsigned int qeth_count_elements(struct sk_buff *skb, unsigned int data_offset);
int qeth_do_send_packet(struct qeth_card *card, struct qeth_qdio_out_q *queue,
			struct sk_buff *skb, struct qeth_hdr *hdr,
			unsigned int offset, unsigned int hd_len,
			int elements_needed);
int qeth_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
void qeth_dbf_longtext(debug_info_t *id, int level, char *text, ...);
int qeth_set_access_ctrl_online(struct qeth_card *card, int fallback);
int qeth_configure_cq(struct qeth_card *, enum qeth_cq);
int qeth_hw_trap(struct qeth_card *, enum qeth_diags_trap_action);
void qeth_trace_features(struct qeth_card *);
int qeth_setassparms_cb(struct qeth_card *, struct qeth_reply *, unsigned long);
int qeth_set_features(struct net_device *, netdev_features_t);
void qeth_enable_hw_features(struct net_device *dev);
netdev_features_t qeth_fix_features(struct net_device *, netdev_features_t);
netdev_features_t qeth_features_check(struct sk_buff *skb,
				      struct net_device *dev,
				      netdev_features_t features);
void qeth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats);
u16 qeth_iqd_select_queue(struct net_device *dev, struct sk_buff *skb,
			  u8 cast_type, struct net_device *sb_dev);
int qeth_open(struct net_device *dev);
int qeth_stop(struct net_device *dev);

int qeth_vm_request_mac(struct qeth_card *card);
int qeth_xmit(struct qeth_card *card, struct sk_buff *skb,
	      struct qeth_qdio_out_q *queue, int ipv,
	      void (*fill_header)(struct qeth_qdio_out_q *queue,
				  struct qeth_hdr *hdr, struct sk_buff *skb,
				  int ipv, unsigned int data_len));

/* exports for OSN */
int qeth_osn_assist(struct net_device *, void *, int);
int qeth_osn_register(unsigned char *read_dev_no, struct net_device **,
		int (*assist_cb)(struct net_device *, void *),
		int (*data_cb)(struct sk_buff *));
void qeth_osn_deregister(struct net_device *);

#endif /* __QETH_CORE_H__ */
