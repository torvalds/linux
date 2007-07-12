#ifndef __QETH_H__
#define __QETH_H__

#include <linux/if.h>
#include <linux/if_arp.h>

#include <linux/if_tr.h>
#include <linux/trdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/ctype.h>

#include <net/ipv6.h>
#include <linux/in6.h>
#include <net/if_inet6.h>
#include <net/addrconf.h>


#include <linux/bitops.h>

#include <asm/debug.h>
#include <asm/qdio.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

#include "qeth_mpc.h"

#ifdef CONFIG_QETH_IPV6
#define QETH_VERSION_IPV6 	":IPv6"
#else
#define QETH_VERSION_IPV6 	""
#endif
#ifdef CONFIG_QETH_VLAN
#define QETH_VERSION_VLAN 	":VLAN"
#else
#define QETH_VERSION_VLAN 	""
#endif

/**
 * Debug Facility stuff
 */
#define QETH_DBF_SETUP_NAME "qeth_setup"
#define QETH_DBF_SETUP_LEN 8
#define QETH_DBF_SETUP_PAGES 8
#define QETH_DBF_SETUP_NR_AREAS 1
#define QETH_DBF_SETUP_LEVEL 5

#define QETH_DBF_MISC_NAME "qeth_misc"
#define QETH_DBF_MISC_LEN 128
#define QETH_DBF_MISC_PAGES 2
#define QETH_DBF_MISC_NR_AREAS 1
#define QETH_DBF_MISC_LEVEL 2

#define QETH_DBF_DATA_NAME "qeth_data"
#define QETH_DBF_DATA_LEN 96
#define QETH_DBF_DATA_PAGES 8
#define QETH_DBF_DATA_NR_AREAS 1
#define QETH_DBF_DATA_LEVEL 2

#define QETH_DBF_CONTROL_NAME "qeth_control"
#define QETH_DBF_CONTROL_LEN 256
#define QETH_DBF_CONTROL_PAGES 8
#define QETH_DBF_CONTROL_NR_AREAS 2
#define QETH_DBF_CONTROL_LEVEL 5

#define QETH_DBF_TRACE_NAME "qeth_trace"
#define QETH_DBF_TRACE_LEN 8
#define QETH_DBF_TRACE_PAGES 4
#define QETH_DBF_TRACE_NR_AREAS 2
#define QETH_DBF_TRACE_LEVEL 3
extern debug_info_t *qeth_dbf_trace;

#define QETH_DBF_SENSE_NAME "qeth_sense"
#define QETH_DBF_SENSE_LEN 64
#define QETH_DBF_SENSE_PAGES 2
#define QETH_DBF_SENSE_NR_AREAS 1
#define QETH_DBF_SENSE_LEVEL 2

#define QETH_DBF_QERR_NAME "qeth_qerr"
#define QETH_DBF_QERR_LEN 8
#define QETH_DBF_QERR_PAGES 2
#define QETH_DBF_QERR_NR_AREAS 2
#define QETH_DBF_QERR_LEVEL 2

#define QETH_DBF_TEXT(name,level,text) \
	do { \
		debug_text_event(qeth_dbf_##name,level,text); \
	} while (0)

#define QETH_DBF_HEX(name,level,addr,len) \
	do { \
		debug_event(qeth_dbf_##name,level,(void*)(addr),len); \
	} while (0)

DECLARE_PER_CPU(char[256], qeth_dbf_txt_buf);

#define QETH_DBF_TEXT_(name,level,text...)				\
	do {								\
		char* dbf_txt_buf = get_cpu_var(qeth_dbf_txt_buf);	\
		sprintf(dbf_txt_buf, text);			  	\
		debug_text_event(qeth_dbf_##name,level,dbf_txt_buf);	\
		put_cpu_var(qeth_dbf_txt_buf);				\
	} while (0)

#define QETH_DBF_SPRINTF(name,level,text...) \
	do { \
		debug_sprintf_event(qeth_dbf_trace, level, ##text ); \
		debug_sprintf_event(qeth_dbf_trace, level, text ); \
	} while (0)

/**
 * some more debug stuff
 */
#define PRINTK_HEADER 	"qeth: "

#define HEXDUMP16(importance,header,ptr) \
PRINT_##importance(header "%02x %02x %02x %02x  %02x %02x %02x %02x  " \
		   "%02x %02x %02x %02x  %02x %02x %02x %02x\n", \
		   *(((char*)ptr)),*(((char*)ptr)+1),*(((char*)ptr)+2), \
		   *(((char*)ptr)+3),*(((char*)ptr)+4),*(((char*)ptr)+5), \
		   *(((char*)ptr)+6),*(((char*)ptr)+7),*(((char*)ptr)+8), \
		   *(((char*)ptr)+9),*(((char*)ptr)+10),*(((char*)ptr)+11), \
		   *(((char*)ptr)+12),*(((char*)ptr)+13), \
		   *(((char*)ptr)+14),*(((char*)ptr)+15)); \
PRINT_##importance(header "%02x %02x %02x %02x  %02x %02x %02x %02x  " \
		   "%02x %02x %02x %02x  %02x %02x %02x %02x\n", \
		   *(((char*)ptr)+16),*(((char*)ptr)+17), \
		   *(((char*)ptr)+18),*(((char*)ptr)+19), \
		   *(((char*)ptr)+20),*(((char*)ptr)+21), \
		   *(((char*)ptr)+22),*(((char*)ptr)+23), \
		   *(((char*)ptr)+24),*(((char*)ptr)+25), \
		   *(((char*)ptr)+26),*(((char*)ptr)+27), \
		   *(((char*)ptr)+28),*(((char*)ptr)+29), \
		   *(((char*)ptr)+30),*(((char*)ptr)+31));

static inline void
qeth_hex_dump(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i && !(i % 16))
			printk("\n");
		printk("%02x ", *(buf + i));
	}
	printk("\n");
}

#define SENSE_COMMAND_REJECT_BYTE 0
#define SENSE_COMMAND_REJECT_FLAG 0x80
#define SENSE_RESETTING_EVENT_BYTE 1
#define SENSE_RESETTING_EVENT_FLAG 0x80

/*
 * Common IO related definitions
 */
extern struct device *qeth_root_dev;
extern struct ccw_driver qeth_ccw_driver;
extern struct ccwgroup_driver qeth_ccwgroup_driver;

#define CARD_RDEV(card) card->read.ccwdev
#define CARD_WDEV(card) card->write.ccwdev
#define CARD_DDEV(card) card->data.ccwdev
#define CARD_BUS_ID(card) card->gdev->dev.bus_id
#define CARD_RDEV_ID(card) card->read.ccwdev->dev.bus_id
#define CARD_WDEV_ID(card) card->write.ccwdev->dev.bus_id
#define CARD_DDEV_ID(card) card->data.ccwdev->dev.bus_id
#define CHANNEL_ID(channel) channel->ccwdev->dev.bus_id

#define CARD_FROM_CDEV(cdev) (struct qeth_card *) \
		((struct ccwgroup_device *)cdev->dev.driver_data)\
		->dev.driver_data;

/**
 * card stuff
 */
struct qeth_perf_stats {
	unsigned int bufs_rec;
	unsigned int bufs_sent;

	unsigned int skbs_sent_pack;
	unsigned int bufs_sent_pack;

	unsigned int sc_dp_p;
	unsigned int sc_p_dp;
	/* qdio_input_handler: number of times called, time spent in */
	__u64 inbound_start_time;
	unsigned int inbound_cnt;
	unsigned int inbound_time;
	/* qeth_send_packet: number of times called, time spent in */
	__u64 outbound_start_time;
	unsigned int outbound_cnt;
	unsigned int outbound_time;
	/* qdio_output_handler: number of times called, time spent in */
	__u64 outbound_handler_start_time;
	unsigned int outbound_handler_cnt;
	unsigned int outbound_handler_time;
	/* number of calls to and time spent in do_QDIO for inbound queue */
	__u64 inbound_do_qdio_start_time;
	unsigned int inbound_do_qdio_cnt;
	unsigned int inbound_do_qdio_time;
	/* number of calls to and time spent in do_QDIO for outbound queues */
	__u64 outbound_do_qdio_start_time;
	unsigned int outbound_do_qdio_cnt;
	unsigned int outbound_do_qdio_time;
	/* eddp data */
	unsigned int large_send_bytes;
	unsigned int large_send_cnt;
	unsigned int sg_skbs_sent;
	unsigned int sg_frags_sent;
	/* initial values when measuring starts */
	unsigned long initial_rx_packets;
	unsigned long initial_tx_packets;
	/* inbound scatter gather data */
	unsigned int sg_skbs_rx;
	unsigned int sg_frags_rx;
	unsigned int sg_alloc_page_rx;
};

/* Routing stuff */
struct qeth_routing_info {
	enum qeth_routing_types type;
};

/* IPA stuff */
struct qeth_ipa_info {
	__u32 supported_funcs;
	__u32 enabled_funcs;
};

static inline int
qeth_is_ipa_supported(struct qeth_ipa_info *ipa, enum qeth_ipa_funcs func)
{
	return (ipa->supported_funcs & func);
}

static inline int
qeth_is_ipa_enabled(struct qeth_ipa_info *ipa, enum qeth_ipa_funcs func)
{
	return (ipa->supported_funcs & ipa->enabled_funcs & func);
}

#define qeth_adp_supported(c,f) \
	qeth_is_ipa_supported(&c->options.adp, f)
#define qeth_adp_enabled(c,f) \
	qeth_is_ipa_enabled(&c->options.adp, f)
#define qeth_is_supported(c,f) \
	qeth_is_ipa_supported(&c->options.ipa4, f)
#define qeth_is_enabled(c,f) \
	qeth_is_ipa_enabled(&c->options.ipa4, f)
#ifdef CONFIG_QETH_IPV6
#define qeth_is_supported6(c,f) \
	qeth_is_ipa_supported(&c->options.ipa6, f)
#define qeth_is_enabled6(c,f) \
	qeth_is_ipa_enabled(&c->options.ipa6, f)
#else /* CONFIG_QETH_IPV6 */
#define qeth_is_supported6(c,f) 0
#define qeth_is_enabled6(c,f) 0
#endif /* CONFIG_QETH_IPV6 */
#define qeth_is_ipafunc_supported(c,prot,f) \
	 (prot==QETH_PROT_IPV6)? qeth_is_supported6(c,f):qeth_is_supported(c,f)
#define qeth_is_ipafunc_enabled(c,prot,f) \
	 (prot==QETH_PROT_IPV6)? qeth_is_enabled6(c,f):qeth_is_enabled(c,f)


#define QETH_IDX_FUNC_LEVEL_OSAE_ENA_IPAT 0x0101
#define QETH_IDX_FUNC_LEVEL_OSAE_DIS_IPAT 0x0101
#define QETH_IDX_FUNC_LEVEL_IQD_ENA_IPAT 0x4108
#define QETH_IDX_FUNC_LEVEL_IQD_DIS_IPAT 0x5108

#define QETH_MODELLIST_ARRAY \
	{{0x1731,0x01,0x1732,0x01,QETH_CARD_TYPE_OSAE,1, \
	QETH_IDX_FUNC_LEVEL_OSAE_ENA_IPAT, \
	QETH_IDX_FUNC_LEVEL_OSAE_DIS_IPAT, \
	QETH_MAX_QUEUES,0}, \
	{0x1731,0x05,0x1732,0x05,QETH_CARD_TYPE_IQD,0, \
	QETH_IDX_FUNC_LEVEL_IQD_ENA_IPAT, \
	QETH_IDX_FUNC_LEVEL_IQD_DIS_IPAT, \
	QETH_MAX_QUEUES,0x103}, \
	{0x1731,0x06,0x1732,0x06,QETH_CARD_TYPE_OSN,0, \
	QETH_IDX_FUNC_LEVEL_OSAE_ENA_IPAT, \
	QETH_IDX_FUNC_LEVEL_OSAE_DIS_IPAT, \
	QETH_MAX_QUEUES,0}, \
	{0,0,0,0,0,0,0,0,0}}

#define QETH_REAL_CARD		1
#define QETH_VLAN_CARD		2
#define QETH_BUFSIZE	 	4096

/**
 * some more defs
 */
#define IF_NAME_LEN	 	16
#define QETH_TX_TIMEOUT		100 * HZ
#define QETH_RCD_TIMEOUT	60 * HZ
#define QETH_HEADER_SIZE	32
#define MAX_PORTNO 		15
#define QETH_FAKE_LL_LEN_ETH	ETH_HLEN
#define QETH_FAKE_LL_LEN_TR	(sizeof(struct trh_hdr)-TR_MAXRIFLEN+sizeof(struct trllc))
#define QETH_FAKE_LL_V6_ADDR_POS 24

/*IPv6 address autoconfiguration stuff*/
#define UNIQUE_ID_IF_CREATE_ADDR_FAILED 0xfffe
#define UNIQUE_ID_NOT_BY_CARD 		0x10000

/*****************************************************************************/
/* QDIO queue and buffer handling                                            */
/*****************************************************************************/
#define QETH_MAX_QUEUES 4
#define QETH_IN_BUF_SIZE_DEFAULT 65536
#define QETH_IN_BUF_COUNT_DEFAULT 16
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

#define QETH_MIN_INPUT_THRESHOLD 1
#define QETH_MAX_INPUT_THRESHOLD 500
#define QETH_MIN_OUTPUT_THRESHOLD 1
#define QETH_MAX_OUTPUT_THRESHOLD 300

/* priority queing */
#define QETH_PRIOQ_DEFAULT QETH_NO_PRIO_QUEUEING
#define QETH_DEFAULT_QUEUE    2
#define QETH_NO_PRIO_QUEUEING 0
#define QETH_PRIO_Q_ING_PREC  1
#define QETH_PRIO_Q_ING_TOS   2
#define IP_TOS_LOWDELAY 0x10
#define IP_TOS_HIGHTHROUGHPUT 0x08
#define IP_TOS_HIGHRELIABILITY 0x04
#define IP_TOS_NOTIMPORTANT 0x02

/* Packing */
#define QETH_LOW_WATERMARK_PACK  2
#define QETH_HIGH_WATERMARK_PACK 5
#define QETH_WATERMARK_PACK_FUZZ 1

#define QETH_IP_HEADER_SIZE 40

/* large receive scatter gather copy break */
#define QETH_RX_SG_CB (PAGE_SIZE >> 1)

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
	__u8  dest_addr[16];
} __attribute__ ((packed));

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
        struct qeth_hdr hdr; 	/*hdr->hdr.l3.xxx*/
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
	QETH_HEADER_TYPE_TSO	= 0x03,
	QETH_HEADER_TYPE_OSN    = 0x04,
};
/* flags for qeth_hdr.ext_flags */
#define QETH_HDR_EXT_VLAN_FRAME       0x01
#define QETH_HDR_EXT_TOKEN_ID         0x02
#define QETH_HDR_EXT_INCLUDE_VLAN_TAG 0x04
#define QETH_HDR_EXT_SRC_MAC_ADDR     0x08
#define QETH_HDR_EXT_CSUM_HDR_REQ     0x10
#define QETH_HDR_EXT_CSUM_TRANSP_REQ  0x20
#define QETH_HDR_EXT_UDP_TSO          0x40 /*bit off for TCP*/

static inline int
qeth_is_last_sbale(struct qdio_buffer_element *sbale)
{
	return (sbale->flags & SBAL_FLAGS_LAST_ENTRY);
}

enum qeth_qdio_buffer_states {
	/*
	 * inbound: read out by driver; owned by hardware in order to be filled
	 * outbound: owned by driver in order to be filled
	 */
	QETH_QDIO_BUF_EMPTY,
	/*
	 * inbound: filled by hardware; owned by driver in order to be read out
	 * outbound: filled by driver; owned by hardware in order to be sent
	 */
	QETH_QDIO_BUF_PRIMED,
};

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
	volatile enum qeth_qdio_buffer_states state;
	/* the buffer pool entry currently associated to this buffer */
	struct qeth_buffer_pool_entry *pool_entry;
};

struct qeth_qdio_q {
	struct qdio_buffer qdio_bufs[QDIO_MAX_BUFFERS_PER_Q];
	struct qeth_qdio_buffer bufs[QDIO_MAX_BUFFERS_PER_Q];
	/*
	 * buf_to_init means "buffer must be initialized by driver and must
	 * be made available for hardware" -> state is set to EMPTY
	 */
	volatile int next_buf_to_init;
} __attribute__ ((aligned(256)));

/* possible types of qeth large_send support */
enum qeth_large_send_types {
	QETH_LARGE_SEND_NO,
	QETH_LARGE_SEND_EDDP,
	QETH_LARGE_SEND_TSO,
};

struct qeth_qdio_out_buffer {
	struct qdio_buffer *buffer;
	atomic_t state;
	volatile int next_element_to_fill;
	struct sk_buff_head skb_list;
	struct list_head ctx_list;
};

struct qeth_card;

enum qeth_out_q_states {
       QETH_OUT_Q_UNLOCKED,
       QETH_OUT_Q_LOCKED,
       QETH_OUT_Q_LOCKED_FLUSH,
};

struct qeth_qdio_out_q {
	struct qdio_buffer qdio_bufs[QDIO_MAX_BUFFERS_PER_Q];
	struct qeth_qdio_out_buffer bufs[QDIO_MAX_BUFFERS_PER_Q];
	int queue_no;
	struct qeth_card *card;
	atomic_t state;
	volatile int do_pack;
	/*
	 * index of buffer to be filled by driver; state EMPTY or PACKING
	 */
	volatile int next_buf_to_fill;
	/*
	 * number of buffers that are currently filled (PRIMED)
	 * -> these buffers are hardware-owned
	 */
	atomic_t used_buffers;
	/* indicates whether PCI flag must be set (or if one is outstanding) */
	atomic_t set_pci_flags_count;
} __attribute__ ((aligned(256)));

struct qeth_qdio_info {
	atomic_t state;
	/* input */
	struct qeth_qdio_q *in_q;
	struct qeth_qdio_buffer_pool in_buf_pool;
	struct qeth_qdio_buffer_pool init_pool;
	int in_buf_size;

	/* output */
	int no_out_queues;
	struct qeth_qdio_out_q **out_qs;

	/* priority queueing */
	int do_prio_queueing;
	int default_out_queue;
};

enum qeth_send_errors {
	QETH_SEND_ERROR_NONE,
	QETH_SEND_ERROR_LINK_FAILURE,
	QETH_SEND_ERROR_RETRY,
	QETH_SEND_ERROR_KICK_IT,
};

#define QETH_ETH_MAC_V4      0x0100 /* like v4 */
#define QETH_ETH_MAC_V6      0x3333 /* like v6 */
/* tr mc mac is longer, but that will be enough to detect mc frames */
#define QETH_TR_MAC_NC       0xc000 /* non-canonical */
#define QETH_TR_MAC_C        0x0300 /* canonical */

#define DEFAULT_ADD_HHLEN 0
#define MAX_ADD_HHLEN 1024

/**
 * buffer stuff for read channel
 */
#define QETH_CMD_BUFFER_NO	8

/**
 *  channel state machine
 */
enum qeth_channel_states {
	CH_STATE_UP,
	CH_STATE_DOWN,
	CH_STATE_ACTIVATING,
	CH_STATE_HALTED,
	CH_STATE_STOPPED,
	CH_STATE_RCD,
	CH_STATE_RCD_DONE,
};
/**
 * card state machine
 */
enum qeth_card_states {
	CARD_STATE_DOWN,
	CARD_STATE_HARDSETUP,
	CARD_STATE_SOFTSETUP,
	CARD_STATE_UP,
	CARD_STATE_RECOVER,
};

/**
 * Protocol versions
 */
enum qeth_prot_versions {
	QETH_PROT_IPV4 = 0x0004,
	QETH_PROT_IPV6 = 0x0006,
};

enum qeth_ip_types {
	QETH_IP_TYPE_NORMAL,
	QETH_IP_TYPE_VIPA,
	QETH_IP_TYPE_RXIP,
	QETH_IP_TYPE_DEL_ALL_MC,
};

enum qeth_cmd_buffer_state {
	BUF_STATE_FREE,
	BUF_STATE_LOCKED,
	BUF_STATE_PROCESSED,
};
/**
 * IP address and multicast list
 */
struct qeth_ipaddr {
	struct list_head entry;
	enum qeth_ip_types type;
	enum qeth_ipa_setdelip_flags set_flags;
	enum qeth_ipa_setdelip_flags del_flags;
	int is_multicast;
	volatile int users;
	enum qeth_prot_versions proto;
	unsigned char mac[OSA_ADDR_LEN];
	union {
		struct {
			unsigned int addr;
			unsigned int mask;
		} a4;
		struct {
			struct in6_addr addr;
			unsigned int pfxlen;
		} a6;
	} u;
};

struct qeth_ipato_entry {
	struct list_head entry;
	enum qeth_prot_versions proto;
	char addr[16];
	int mask_bits;
};

struct qeth_ipato {
	int enabled;
	int invert4;
	int invert6;
	struct list_head entries;
};

struct qeth_channel;

struct qeth_cmd_buffer {
	enum qeth_cmd_buffer_state state;
	struct qeth_channel *channel;
	unsigned char *data;
	int rc;
	void (*callback) (struct qeth_channel *, struct qeth_cmd_buffer *);
};


/**
 * definition of a qeth channel, used for read and write
 */
struct qeth_channel {
	enum qeth_channel_states state;
	struct ccw1 ccw;
	spinlock_t iob_lock;
	wait_queue_head_t wait_q;
	struct tasklet_struct irq_tasklet;
	struct ccw_device *ccwdev;
/*command buffer for control data*/
	struct qeth_cmd_buffer iob[QETH_CMD_BUFFER_NO];
	atomic_t irq_pending;
	volatile int io_buf_no;
	volatile int buf_no;
};

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
	__u32 pkt_seqno;
};

struct qeth_reply {
	struct list_head list;
	wait_queue_head_t wait_q;
	int (*callback)(struct qeth_card *,struct qeth_reply *,unsigned long);
 	u32 seqno;
	unsigned long offset;
	atomic_t received;
	int rc;
	void *param;
	struct qeth_card *card;
	atomic_t refcnt;
};


struct qeth_card_blkt {
	int time_total;
	int inter_packet;
	int inter_packet_jumbo;
};

#define QETH_BROADCAST_WITH_ECHO    0x01
#define QETH_BROADCAST_WITHOUT_ECHO 0x02
#define QETH_LAYER2_MAC_READ	    0x01
#define QETH_LAYER2_MAC_REGISTERED  0x02
struct qeth_card_info {
	unsigned short unit_addr2;
	unsigned short cula;
	unsigned short chpid;
	__u16 func_level;
	char mcl_level[QETH_MCL_LENGTH + 1];
	int guestlan;
	int mac_bits;
	int portname_required;
	int portno;
	char portname[9];
	enum qeth_card_types type;
	enum qeth_link_types link_type;
	int is_multicast_different;
	int initial_mtu;
	int max_mtu;
	int broadcast_capable;
	int unique_id;
	struct qeth_card_blkt blkt;
	__u32 csum_mask;
	enum qeth_ipa_promisc_modes promisc_mode;
};

struct qeth_card_options {
	struct qeth_routing_info route4;
	struct qeth_ipa_info ipa4;
	struct qeth_ipa_info adp; /*Adapter parameters*/
#ifdef CONFIG_QETH_IPV6
	struct qeth_routing_info route6;
	struct qeth_ipa_info ipa6;
#endif /* QETH_IPV6 */
	enum qeth_checksum_types checksum_type;
	int broadcast_mode;
	int macaddr_mode;
	int fake_broadcast;
	int add_hhlen;
	int fake_ll;
	int layer2;
	enum qeth_large_send_types large_send;
	int performance_stats;
	int rx_sg_cb;
};

/*
 * thread bits for qeth_card thread masks
 */
enum qeth_threads {
	QETH_SET_IP_THREAD  = 1,
	QETH_RECOVER_THREAD = 2,
	QETH_SET_PROMISC_MODE_THREAD = 4,
};

struct qeth_osn_info {
	int (*assist_cb)(struct net_device *dev, void *data);
	int (*data_cb)(struct sk_buff *skb);
};

struct qeth_card {
	struct list_head list;
	enum qeth_card_states state;
	int lan_online;
	spinlock_t lock;
/*hardware and sysfs stuff*/
	struct ccwgroup_device *gdev;
	struct qeth_channel read;
	struct qeth_channel write;
	struct qeth_channel data;

	struct net_device *dev;
	struct net_device_stats stats;

	struct qeth_card_info info;
	struct qeth_token token;
	struct qeth_seqno seqno;
	struct qeth_card_options options;

	wait_queue_head_t wait_q;
#ifdef CONFIG_QETH_VLAN
	spinlock_t vlanlock;
	struct vlan_group *vlangrp;
#endif
	struct work_struct kernel_thread_starter;
	spinlock_t thread_mask_lock;
	volatile unsigned long thread_start_mask;
	volatile unsigned long thread_allowed_mask;
	volatile unsigned long thread_running_mask;
	spinlock_t ip_lock;
	struct list_head ip_list;
	struct list_head *ip_tbd_list;
	struct qeth_ipato ipato;
	struct list_head cmd_waiter_list;
	/* QDIO buffer handling */
	struct qeth_qdio_info qdio;
	struct qeth_perf_stats perf_stats;
	int use_hard_stop;
	int (*orig_hard_header)(struct sk_buff *,struct net_device *,
				unsigned short,void *,void *,unsigned);
	struct qeth_osn_info osn_info;
	atomic_t force_alloc_skb;
};

struct qeth_card_list_struct {
	struct list_head list;
	rwlock_t rwlock;
};

extern struct qeth_card_list_struct qeth_card_list;

/*notifier list */
struct qeth_notify_list_struct {
	struct list_head list;
	struct task_struct *task;
	int signum;
};
extern spinlock_t qeth_notify_lock;
extern struct list_head qeth_notify_list;

/*some helper functions*/

#define QETH_CARD_IFNAME(card) (((card)->dev)? (card)->dev->name : "")

static inline __u8
qeth_get_ipa_adp_type(enum qeth_link_types link_type)
{
	switch (link_type) {
	case QETH_LINK_TYPE_HSTR:
		return 2;
	default:
		return 1;
	}
}

static inline struct sk_buff *
qeth_realloc_headroom(struct qeth_card *card, struct sk_buff *skb, int size)
{
	struct sk_buff *new_skb = skb;

	if (skb_headroom(skb) >= size)
		return skb;
	new_skb = skb_realloc_headroom(skb, size);
	if (!new_skb) 
		PRINT_ERR("Could not realloc headroom for qeth_hdr "
			  "on interface %s", QETH_CARD_IFNAME(card));
	return new_skb;
}

static inline struct sk_buff *
qeth_pskb_unshare(struct sk_buff *skb, gfp_t pri)
{
        struct sk_buff *nskb;
        if (!skb_cloned(skb))
                return skb;
        nskb = skb_copy(skb, pri);
        return nskb;
}

static inline void *
qeth_push_skb(struct qeth_card *card, struct sk_buff *skb, int size)
{
        void *hdr;

	hdr = (void *) skb_push(skb, size);
        /*
         * sanity check, the Linux memory allocation scheme should
         * never present us cases like this one (the qdio header size plus
         * the first 40 bytes of the paket cross a 4k boundary)
         */
        if ((((unsigned long) hdr) & (~(PAGE_SIZE - 1))) !=
            (((unsigned long) hdr + size +
              QETH_IP_HEADER_SIZE) & (~(PAGE_SIZE - 1)))) {
                PRINT_ERR("Misaligned packet on interface %s. Discarded.",
                          QETH_CARD_IFNAME(card));
                return NULL;
        }
        return hdr;
}


static inline int
qeth_get_hlen(__u8 link_type)
{
#ifdef CONFIG_QETH_IPV6
	switch (link_type) {
	case QETH_LINK_TYPE_HSTR:
	case QETH_LINK_TYPE_LANE_TR:
		return sizeof(struct qeth_hdr_tso) + TR_HLEN;
	default:
#ifdef CONFIG_QETH_VLAN
		return sizeof(struct qeth_hdr_tso) + VLAN_ETH_HLEN;
#else
		return sizeof(struct qeth_hdr_tso) + ETH_HLEN;
#endif
	}
#else  /* CONFIG_QETH_IPV6 */
#ifdef CONFIG_QETH_VLAN
	return sizeof(struct qeth_hdr_tso) + VLAN_HLEN;
#else
	return sizeof(struct qeth_hdr_tso);
#endif
#endif /* CONFIG_QETH_IPV6 */
}

static inline unsigned short
qeth_get_netdev_flags(struct qeth_card *card)
{
	if (card->options.layer2 &&
	   (card->info.type == QETH_CARD_TYPE_OSAE))
		return 0;
	switch (card->info.type) {
	case QETH_CARD_TYPE_IQD:
	case QETH_CARD_TYPE_OSN:
		return IFF_NOARP;
#ifdef CONFIG_QETH_IPV6
	default:
		return 0;
#else
	default:
		return IFF_NOARP;
#endif
	}
}

static inline int
qeth_get_initial_mtu_for_card(struct qeth_card * card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_UNKNOWN:
		return 1500;
	case QETH_CARD_TYPE_IQD:
		return card->info.max_mtu;
	case QETH_CARD_TYPE_OSAE:
		switch (card->info.link_type) {
		case QETH_LINK_TYPE_HSTR:
		case QETH_LINK_TYPE_LANE_TR:
			return 2000;
		default:
			return 1492;
		}
	default:
		return 1500;
	}
}

static inline int
qeth_get_max_mtu_for_card(int cardtype)
{
	switch (cardtype) {

	case QETH_CARD_TYPE_UNKNOWN:
	case QETH_CARD_TYPE_OSAE:
	case QETH_CARD_TYPE_OSN:
		return 61440;
	case QETH_CARD_TYPE_IQD:
		return 57344;
	default:
		return 1500;
	}
}

static inline int
qeth_get_mtu_out_of_mpc(int cardtype)
{
	switch (cardtype) {
	case QETH_CARD_TYPE_IQD:
		return 1;
	default:
		return 0;
	}
}

static inline int
qeth_get_mtu_outof_framesize(int framesize)
{
	switch (framesize) {
	case 0x4000:
		return 8192;
	case 0x6000:
		return 16384;
	case 0xa000:
		return 32768;
	case 0xffff:
		return 57344;
	default:
		return 0;
	}
}

static inline int
qeth_mtu_is_valid(struct qeth_card * card, int mtu)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSAE:
		return ((mtu >= 576) && (mtu <= 61440));
	case QETH_CARD_TYPE_IQD:
		return ((mtu >= 576) &&
			(mtu <= card->info.max_mtu + 4096 - 32));
	case QETH_CARD_TYPE_OSN:
	case QETH_CARD_TYPE_UNKNOWN:
	default:
		return 1;
	}
}

static inline int
qeth_get_arphdr_type(int cardtype, int linktype)
{
	switch (cardtype) {
	case QETH_CARD_TYPE_OSAE:
	case QETH_CARD_TYPE_OSN:
		switch (linktype) {
		case QETH_LINK_TYPE_LANE_TR:
		case QETH_LINK_TYPE_HSTR:
			return ARPHRD_IEEE802_TR;
		default:
			return ARPHRD_ETHER;
		}
	case QETH_CARD_TYPE_IQD:
	default:
		return ARPHRD_ETHER;
	}
}

static inline int
qeth_get_micros(void)
{
	return (int) (get_clock() >> 12);
}

static inline int
qeth_get_qdio_q_format(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_IQD:
		return 2;
	default:
		return 0;
	}
}

static inline int
qeth_isxdigit(char * buf)
{
	while (*buf) {
		if (!isxdigit(*buf++))
			return 0;
	}
	return 1;
}

static inline void
qeth_ipaddr4_to_string(const __u8 *addr, char *buf)
{
	sprintf(buf, "%i.%i.%i.%i", addr[0], addr[1], addr[2], addr[3]);
}

static inline int
qeth_string_to_ipaddr4(const char *buf, __u8 *addr)
{
	int count = 0, rc = 0;
	int in[4];
	char c;

	rc = sscanf(buf, "%u.%u.%u.%u%c",
		    &in[0], &in[1], &in[2], &in[3], &c);
	if (rc != 4 && (rc != 5 || c != '\n'))
		return -EINVAL;
	for (count = 0; count < 4; count++) {
		if (in[count] > 255)
			return -EINVAL;
		addr[count] = in[count];
	}
	return 0;
}

static inline void
qeth_ipaddr6_to_string(const __u8 *addr, char *buf)
{
	sprintf(buf, "%02x%02x:%02x%02x:%02x%02x:%02x%02x"
		     ":%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		     addr[0], addr[1], addr[2], addr[3],
		     addr[4], addr[5], addr[6], addr[7],
		     addr[8], addr[9], addr[10], addr[11],
		     addr[12], addr[13], addr[14], addr[15]);
}

static inline int
qeth_string_to_ipaddr6(const char *buf, __u8 *addr)
{
	const char *end, *end_tmp, *start;
	__u16 *in;
        char num[5];
        int num2, cnt, out, found, save_cnt;
        unsigned short in_tmp[8] = {0, };

	cnt = out = found = save_cnt = num2 = 0;
        end = start = buf;
	in = (__u16 *) addr;
	memset(in, 0, 16);
        while (*end) {
                end = strchr(start,':');
                if (end == NULL) {
                        end = buf + strlen(buf);
			if ((end_tmp = strchr(start, '\n')) != NULL)
				end = end_tmp;
			out = 1;
                }
                if ((end - start)) {
                        memset(num, 0, 5);
			if ((end - start) > 4)
				return -EINVAL;
                        memcpy(num, start, end - start);
			if (!qeth_isxdigit(num))
				return -EINVAL;
                        sscanf(start, "%x", &num2);
                        if (found)
                                in_tmp[save_cnt++] = num2;
                        else
                                in[cnt++] = num2;
                        if (out)
                                break;
                } else {
			if (found)
				return -EINVAL;
                        found = 1;
		}
		start = ++end;
        }
	if (cnt + save_cnt > 8)
		return -EINVAL;
        cnt = 7;
	while (save_cnt)
                in[cnt--] = in_tmp[--save_cnt];
	return 0;
}

static inline void
qeth_ipaddr_to_string(enum qeth_prot_versions proto, const __u8 *addr,
		      char *buf)
{
	if (proto == QETH_PROT_IPV4)
		return qeth_ipaddr4_to_string(addr, buf);
	else if (proto == QETH_PROT_IPV6)
		return qeth_ipaddr6_to_string(addr, buf);
}

static inline int
qeth_string_to_ipaddr(const char *buf, enum qeth_prot_versions proto,
		      __u8 *addr)
{
	if (proto == QETH_PROT_IPV4)
		return qeth_string_to_ipaddr4(buf, addr);
	else if (proto == QETH_PROT_IPV6)
		return qeth_string_to_ipaddr6(buf, addr);
	else
		return -EINVAL;
}

extern int
qeth_setrouting_v4(struct qeth_card *);
extern int
qeth_setrouting_v6(struct qeth_card *);

extern int
qeth_add_ipato_entry(struct qeth_card *, struct qeth_ipato_entry *);

extern void
qeth_del_ipato_entry(struct qeth_card *, enum qeth_prot_versions, u8 *, int);

extern int
qeth_add_vipa(struct qeth_card *, enum qeth_prot_versions, const u8 *);

extern void
qeth_del_vipa(struct qeth_card *, enum qeth_prot_versions, const u8 *);

extern int
qeth_add_rxip(struct qeth_card *, enum qeth_prot_versions, const u8 *);

extern void
qeth_del_rxip(struct qeth_card *, enum qeth_prot_versions, const u8 *);

extern int
qeth_notifier_register(struct task_struct *, int );

extern int
qeth_notifier_unregister(struct task_struct * );

extern void
qeth_schedule_recovery(struct qeth_card *);

extern int
qeth_realloc_buffer_pool(struct qeth_card *, int);

extern int
qeth_set_large_send(struct qeth_card *, enum qeth_large_send_types);

extern void
qeth_fill_header(struct qeth_card *, struct qeth_hdr *,
		 struct sk_buff *, int, int);
extern void
qeth_flush_buffers(struct qeth_qdio_out_q *, int, int, int);

extern int
qeth_osn_assist(struct net_device *, void *, int);

extern int
qeth_osn_register(unsigned char *read_dev_no,
                 struct net_device **,
                 int (*assist_cb)(struct net_device *, void *),
                 int (*data_cb)(struct sk_buff *));

extern void
qeth_osn_deregister(struct net_device *);

#endif /* __QETH_H__ */
