/*
 *	Copyright IBM Corp. 2001, 2007
 *	Authors:	Fritz Elfert (felfert@millenux.com)
 *			Peter Tiedemann (ptiedem@de.ibm.com)
 */

#ifndef _CTCM_MAIN_H_
#define _CTCM_MAIN_H_

#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include "fsm.h"
#include "ctcm_dbug.h"
#include "ctcm_mpc.h"

#define CTC_DRIVER_NAME	"ctcm"
#define CTC_DEVICE_NAME	"ctc"
#define MPC_DEVICE_NAME	"mpc"
#define CTC_DEVICE_GENE CTC_DEVICE_NAME "%d"
#define MPC_DEVICE_GENE	MPC_DEVICE_NAME "%d"

#define CHANNEL_FLAGS_READ	0
#define CHANNEL_FLAGS_WRITE	1
#define CHANNEL_FLAGS_INUSE	2
#define CHANNEL_FLAGS_BUFSIZE_CHANGED	4
#define CHANNEL_FLAGS_FAILED	8
#define CHANNEL_FLAGS_WAITIRQ	16
#define CHANNEL_FLAGS_RWMASK	1
#define CHANNEL_DIRECTION(f) (f & CHANNEL_FLAGS_RWMASK)

#define LOG_FLAG_ILLEGALPKT	1
#define LOG_FLAG_ILLEGALSIZE	2
#define LOG_FLAG_OVERRUN	4
#define LOG_FLAG_NOMEM		8

#define ctcm_pr_debug(fmt, arg...) printk(KERN_DEBUG fmt, ##arg)

#define CTCM_PR_DEBUG(fmt, arg...) \
	do { \
		if (do_debug) \
			printk(KERN_DEBUG fmt, ##arg); \
	} while (0)

#define	CTCM_PR_DBGDATA(fmt, arg...) \
	do { \
		if (do_debug_data) \
			printk(KERN_DEBUG fmt, ##arg); \
	} while (0)

#define	CTCM_D3_DUMP(buf, len) \
	do { \
		if (do_debug_data) \
			ctcmpc_dumpit(buf, len); \
	} while (0)

#define	CTCM_CCW_DUMP(buf, len) \
	do { \
		if (do_debug_ccw) \
			ctcmpc_dumpit(buf, len); \
	} while (0)

/**
 * Enum for classifying detected devices
 */
enum ctcm_channel_types {
	/* Device is not a channel  */
	ctcm_channel_type_none,

	/* Device is a CTC/A */
	ctcm_channel_type_parallel,

	/* Device is a FICON channel */
	ctcm_channel_type_ficon,

	/* Device is a ESCON channel */
	ctcm_channel_type_escon
};

/*
 * CCW commands, used in this driver.
 */
#define CCW_CMD_WRITE		0x01
#define CCW_CMD_READ		0x02
#define CCW_CMD_NOOP		0x03
#define CCW_CMD_TIC             0x08
#define CCW_CMD_SENSE_CMD	0x14
#define CCW_CMD_WRITE_CTL	0x17
#define CCW_CMD_SET_EXTENDED	0xc3
#define CCW_CMD_PREPARE		0xe3

#define CTCM_PROTO_S390		0
#define CTCM_PROTO_LINUX	1
#define CTCM_PROTO_LINUX_TTY	2
#define CTCM_PROTO_OS390	3
#define CTCM_PROTO_MPC		4
#define CTCM_PROTO_MAX		4

#define CTCM_BUFSIZE_LIMIT	65535
#define CTCM_BUFSIZE_DEFAULT	32768
#define MPC_BUFSIZE_DEFAULT	CTCM_BUFSIZE_LIMIT

#define CTCM_TIME_1_SEC		1000
#define CTCM_TIME_5_SEC		5000
#define CTCM_TIME_10_SEC	10000

#define CTCM_INITIAL_BLOCKLEN	2

#define CTCM_READ		0
#define CTCM_WRITE		1

#define CTCM_ID_SIZE		20+3

struct ctcm_profile {
	unsigned long maxmulti;
	unsigned long maxcqueue;
	unsigned long doios_single;
	unsigned long doios_multi;
	unsigned long txlen;
	unsigned long tx_time;
	unsigned long send_stamp;
};

/*
 * Definition of one channel
 */
struct channel {
	struct channel *next;
	char id[CTCM_ID_SIZE];
	struct ccw_device *cdev;
	/*
	 * Type of this channel.
	 * CTC/A or Escon for valid channels.
	 */
	enum ctcm_channel_types type;
	/*
	 * Misc. flags. See CHANNEL_FLAGS_... below
	 */
	__u32 flags;
	__u16 protocol;		/* protocol of this channel (4 = MPC) */
	/*
	 * I/O and irq related stuff
	 */
	struct ccw1 *ccw;
	struct irb *irb;
	/*
	 * RX/TX buffer size
	 */
	int max_bufsize;
	struct sk_buff *trans_skb;	/* transmit/receive buffer */
	struct sk_buff_head io_queue;	/* universal I/O queue */
	struct tasklet_struct ch_tasklet;	/* MPC ONLY */
	/*
	 * TX queue for collecting skb's during busy.
	 */
	struct sk_buff_head collect_queue;
	/*
	 * Amount of data in collect_queue.
	 */
	int collect_len;
	/*
	 * spinlock for collect_queue and collect_len
	 */
	spinlock_t collect_lock;
	/*
	 * Timer for detecting unresposive
	 * I/O operations.
	 */
	fsm_timer timer;
	/* MPC ONLY section begin */
	__u32	th_seq_num;	/* SNA TH seq number */
	__u8	th_seg;
	__u32	pdu_seq;
	struct sk_buff		*xid_skb;
	char			*xid_skb_data;
	struct th_header	*xid_th;
	struct xid2		*xid;
	char			*xid_id;
	struct th_header	*rcvd_xid_th;
	struct xid2		*rcvd_xid;
	char			*rcvd_xid_id;
	__u8			in_mpcgroup;
	fsm_timer		sweep_timer;
	struct sk_buff_head	sweep_queue;
	struct th_header	*discontact_th;
	struct tasklet_struct	ch_disc_tasklet;
	/* MPC ONLY section end */

	int retry;		/* retry counter for misc. operations */
	fsm_instance *fsm;	/* finite state machine of this channel */
	struct net_device *netdev;	/* corresponding net_device */
	struct ctcm_profile prof;
	__u8 *trans_skb_data;
	__u16 logflags;
	__u8  sense_rc; /* last unit check sense code report control */
};

struct ctcm_priv {
	struct net_device_stats	stats;
	unsigned long	tbusy;

	/* The MPC group struct of this interface */
	struct	mpc_group	*mpcg;	/* MPC only */
	struct	xid2		*xid;	/* MPC only */

	/* The finite state machine of this interface */
	fsm_instance *fsm;

	/* The protocol of this device */
	__u16 protocol;

	/* Timer for restarting after I/O Errors */
	fsm_timer	restart_timer;

	int buffer_size;	/* ctc only */

	struct channel *channel[2];
};

int ctcm_open(struct net_device *dev);
int ctcm_close(struct net_device *dev);

extern const struct attribute_group *ctcm_attr_groups[];

/*
 * Compatibility macros for busy handling
 * of network devices.
 */
static inline void ctcm_clear_busy_do(struct net_device *dev)
{
	clear_bit(0, &(((struct ctcm_priv *)dev->ml_priv)->tbusy));
	netif_wake_queue(dev);
}

static inline void ctcm_clear_busy(struct net_device *dev)
{
	struct mpc_group *grp;
	grp = ((struct ctcm_priv *)dev->ml_priv)->mpcg;

	if (!(grp && grp->in_sweep))
		ctcm_clear_busy_do(dev);
}


static inline int ctcm_test_and_set_busy(struct net_device *dev)
{
	netif_stop_queue(dev);
	return test_and_set_bit(0,
			&(((struct ctcm_priv *)dev->ml_priv)->tbusy));
}

extern int loglevel;
extern struct channel *channels;

void ctcm_unpack_skb(struct channel *ch, struct sk_buff *pskb);

/*
 * Functions related to setup and device detection.
 */

static inline int ctcm_less_than(char *id1, char *id2)
{
	unsigned long dev1, dev2;

	id1 = id1 + 5;
	id2 = id2 + 5;

	dev1 = simple_strtoul(id1, &id1, 16);
	dev2 = simple_strtoul(id2, &id2, 16);

	return (dev1 < dev2);
}

int ctcm_ch_alloc_buffer(struct channel *ch);

static inline int ctcm_checkalloc_buffer(struct channel *ch)
{
	if (ch->trans_skb == NULL)
		return ctcm_ch_alloc_buffer(ch);
	if (ch->flags & CHANNEL_FLAGS_BUFSIZE_CHANGED) {
		dev_kfree_skb(ch->trans_skb);
		return ctcm_ch_alloc_buffer(ch);
	}
	return 0;
}

struct mpc_group *ctcmpc_init_mpc_group(struct ctcm_priv *priv);

/* test if protocol attribute (of struct ctcm_priv or struct channel)
 * has MPC protocol setting. Type is not checked
 */
#define IS_MPC(p) ((p)->protocol == CTCM_PROTO_MPC)

/* test if struct ctcm_priv of struct net_device has MPC protocol setting */
#define IS_MPCDEV(dev) IS_MPC((struct ctcm_priv *)dev->ml_priv)

static inline gfp_t gfp_type(void)
{
	return in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;
}

/*
 * Definition of our link level header.
 */
struct ll_header {
	__u16 length;
	__u16 type;
	__u16 unused;
};
#define LL_HEADER_LENGTH (sizeof(struct ll_header))

#endif
