/*lcs.h*/

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <asm/ccwdev.h>

#define LCS_DBF_TEXT(level, name, text) \
	do { \
		debug_text_event(lcs_dbf_##name, level, text); \
	} while (0)

#define LCS_DBF_HEX(level,name,addr,len) \
do { \
	debug_event(lcs_dbf_##name,level,(void*)(addr),len); \
} while (0)

/* Allow to sort out low debug levels early to avoid wasted sprints */
static inline int lcs_dbf_passes(debug_info_t *dbf_grp, int level)
{
	return (level <= dbf_grp->level);
}

#define LCS_DBF_TEXT_(level,name,text...) \
	do { \
		if (lcs_dbf_passes(lcs_dbf_##name, level)) { \
			sprintf(debug_buffer, text); \
			debug_text_event(lcs_dbf_##name, level, debug_buffer); \
		} \
	} while (0)

/**
 *	sysfs related stuff
 */
#define CARD_FROM_DEV(cdev) \
	(struct lcs_card *) dev_get_drvdata( \
		&((struct ccwgroup_device *)dev_get_drvdata(&cdev->dev))->dev);
/**
 * CCW commands used in this driver
 */
#define LCS_CCW_WRITE		0x01
#define LCS_CCW_READ		0x02
#define LCS_CCW_TRANSFER	0x08

/**
 * LCS device status primitives
 */
#define LCS_CMD_STARTLAN	0x01
#define LCS_CMD_STOPLAN		0x02
#define LCS_CMD_LANSTAT		0x04
#define LCS_CMD_STARTUP		0x07
#define LCS_CMD_SHUTDOWN	0x08
#define LCS_CMD_QIPASSIST	0xb2
#define LCS_CMD_SETIPM		0xb4
#define LCS_CMD_DELIPM		0xb5

#define LCS_INITIATOR_TCPIP	0x00
#define LCS_INITIATOR_LGW	0x01
#define LCS_STD_CMD_SIZE	16
#define LCS_MULTICAST_CMD_SIZE	404

/**
 * LCS IPASSIST MASKS,only used when multicast is switched on
 */
/* Not supported by LCS */
#define LCS_IPASS_ARP_PROCESSING	0x0001
#define LCS_IPASS_IN_CHECKSUM_SUPPORT	0x0002
#define LCS_IPASS_OUT_CHECKSUM_SUPPORT	0x0004
#define LCS_IPASS_IP_FRAG_REASSEMBLY	0x0008
#define LCS_IPASS_IP_FILTERING		0x0010
/* Supported by lcs 3172 */
#define LCS_IPASS_IPV6_SUPPORT		0x0020
#define LCS_IPASS_MULTICAST_SUPPORT	0x0040

/**
 * LCS sense byte definitions
 */
#define LCS_SENSE_BYTE_0 		0
#define LCS_SENSE_BYTE_1 		1
#define LCS_SENSE_BYTE_2 		2
#define LCS_SENSE_BYTE_3 		3
#define LCS_SENSE_INTERFACE_DISCONNECT	0x01
#define LCS_SENSE_EQUIPMENT_CHECK	0x10
#define LCS_SENSE_BUS_OUT_CHECK		0x20
#define LCS_SENSE_INTERVENTION_REQUIRED 0x40
#define LCS_SENSE_CMD_REJECT		0x80
#define LCS_SENSE_RESETTING_EVENT	0x80
#define LCS_SENSE_DEVICE_ONLINE		0x20

/**
 * LCS packet type definitions
 */
#define LCS_FRAME_TYPE_CONTROL		0
#define LCS_FRAME_TYPE_ENET		1
#define LCS_FRAME_TYPE_TR		2
#define LCS_FRAME_TYPE_FDDI		7
#define LCS_FRAME_TYPE_AUTO		-1

/**
 * some more definitions,we will sort them later
 */
#define LCS_ILLEGAL_OFFSET		0xffff
#define LCS_IOBUFFERSIZE		0x5000
#define LCS_NUM_BUFFS			32	/* needs to be power of 2 */
#define LCS_MAC_LENGTH			6
#define LCS_INVALID_PORT_NO		-1
#define LCS_LANCMD_TIMEOUT_DEFAULT      5

/**
 * Multicast state
 */
#define	 LCS_IPM_STATE_SET_REQUIRED	0
#define	 LCS_IPM_STATE_DEL_REQUIRED	1
#define	 LCS_IPM_STATE_ON_CARD		2

/**
 * LCS IP Assist declarations
 * seems to be only used for multicast
 */
#define	 LCS_IPASS_ARP_PROCESSING	0x0001
#define	 LCS_IPASS_INBOUND_CSUM_SUPP	0x0002
#define	 LCS_IPASS_OUTBOUND_CSUM_SUPP	0x0004
#define	 LCS_IPASS_IP_FRAG_REASSEMBLY	0x0008
#define	 LCS_IPASS_IP_FILTERING		0x0010
#define	 LCS_IPASS_IPV6_SUPPORT		0x0020
#define	 LCS_IPASS_MULTICAST_SUPPORT	0x0040

/**
 * LCS Buffer states
 */
enum lcs_buffer_states {
	LCS_BUF_STATE_EMPTY,	/* buffer is empty */
	LCS_BUF_STATE_LOCKED,	/* buffer is locked, don't touch */
	LCS_BUF_STATE_READY,	/* buffer is ready for read/write */
	LCS_BUF_STATE_PROCESSED,
};

/**
 * LCS Channel State Machine declarations
 */
enum lcs_channel_states {
	LCS_CH_STATE_INIT,
	LCS_CH_STATE_HALTED,
	LCS_CH_STATE_STOPPED,
	LCS_CH_STATE_RUNNING,
	LCS_CH_STATE_SUSPENDED,
	LCS_CH_STATE_CLEARED,
	LCS_CH_STATE_ERROR,
};

/**
 * LCS device state machine
 */
enum lcs_dev_states {
	DEV_STATE_DOWN,
	DEV_STATE_UP,
	DEV_STATE_RECOVER,
};

enum lcs_threads {
	LCS_SET_MC_THREAD 	= 1,
	LCS_RECOVERY_THREAD 	= 2,
};

/**
 * LCS struct declarations
 */
struct lcs_header {
	__u16  offset;
	__u8   type;
	__u8   slot;
}  __attribute__ ((packed));

struct lcs_ip_mac_pair {
	__be32  ip_addr;
	__u8   mac_addr[LCS_MAC_LENGTH];
	__u8   reserved[2];
}  __attribute__ ((packed));

struct lcs_ipm_list {
	struct list_head list;
	struct lcs_ip_mac_pair ipm;
	__u8 ipm_state;
};

struct lcs_cmd {
	__u16  offset;
	__u8   type;
	__u8   slot;
	__u8   cmd_code;
	__u8   initiator;
	__u16  sequence_no;
	__u16  return_code;
	union {
		struct {
			__u8   lan_type;
			__u8   portno;
			__u16  parameter_count;
			__u8   operator_flags[3];
			__u8   reserved[3];
		} lcs_std_cmd;
		struct {
			__u16  unused1;
			__u16  buff_size;
			__u8   unused2[6];
		} lcs_startup;
		struct {
			__u8   lan_type;
			__u8   portno;
			__u8   unused[10];
			__u8   mac_addr[LCS_MAC_LENGTH];
			__u32  num_packets_deblocked;
			__u32  num_packets_blocked;
			__u32  num_packets_tx_on_lan;
			__u32  num_tx_errors_detected;
			__u32  num_tx_packets_disgarded;
			__u32  num_packets_rx_from_lan;
			__u32  num_rx_errors_detected;
			__u32  num_rx_discarded_nobuffs_avail;
			__u32  num_rx_packets_too_large;
		} lcs_lanstat_cmd;
#ifdef CONFIG_IP_MULTICAST
		struct {
			__u8   lan_type;
			__u8   portno;
			__u16  num_ip_pairs;
			__u16  ip_assists_supported;
			__u16  ip_assists_enabled;
			__u16  version;
			struct {
				struct lcs_ip_mac_pair
				ip_mac_pair[32];
				__u32	  response_data;
			} lcs_ipass_ctlmsg __attribute ((packed));
		} lcs_qipassist __attribute__ ((packed));
#endif /*CONFIG_IP_MULTICAST */
	} cmd __attribute__ ((packed));
}  __attribute__ ((packed));

/**
 * Forward declarations.
 */
struct lcs_card;
struct lcs_channel;

/**
 * Definition of an lcs buffer.
 */
struct lcs_buffer {
	enum lcs_buffer_states state;
	void *data;
	int count;
	/* Callback for completion notification. */
	void (*callback)(struct lcs_channel *, struct lcs_buffer *);
};

struct lcs_reply {
	struct list_head list;
	__u16 sequence_no;
	atomic_t refcnt;
	/* Callback for completion notification. */
	void (*callback)(struct lcs_card *, struct lcs_cmd *);
	wait_queue_head_t wait_q;
	struct lcs_card *card;
	int received;
	int rc;
};

/**
 * Definition of an lcs channel
 */
struct lcs_channel {
	enum lcs_channel_states state;
	struct ccw_device *ccwdev;
	struct ccw1 ccws[LCS_NUM_BUFFS + 1];
	wait_queue_head_t wait_q;
	struct tasklet_struct irq_tasklet;
	struct lcs_buffer iob[LCS_NUM_BUFFS];
	int io_idx;
	int buf_idx;
};


/**
 * definition of the lcs card
 */
struct lcs_card {
	spinlock_t lock;
	spinlock_t ipm_lock;
	enum lcs_dev_states state;
	struct net_device *dev;
	struct net_device_stats stats;
	__be16 (*lan_type_trans)(struct sk_buff *skb,
					 struct net_device *dev);
	struct ccwgroup_device *gdev;
	struct lcs_channel read;
	struct lcs_channel write;
	struct lcs_buffer *tx_buffer;
	int tx_emitted;
	struct list_head lancmd_waiters;
	int lancmd_timeout;

	struct work_struct kernel_thread_starter;
	spinlock_t mask_lock;
	unsigned long thread_start_mask;
	unsigned long thread_running_mask;
	unsigned long thread_allowed_mask;
	wait_queue_head_t wait_q;

#ifdef CONFIG_IP_MULTICAST
	struct list_head ipm_list;
#endif
	__u8 mac[LCS_MAC_LENGTH];
	__u16 ip_assists_supported;
	__u16 ip_assists_enabled;
	__s8 lan_type;
	__u32 pkt_seq;
	__u16 sequence_no;
	__s16 portno;
	/* Some info copied from probeinfo */
	u8 device_forced;
	u8 max_port_no;
	u8 hint_port_no;
	s16 port_protocol_no;
}  __attribute__ ((aligned(8)));

