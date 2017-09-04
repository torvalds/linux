/*
 * IUCV network driver
 *
 * Copyright IBM Corp. 2001, 2009
 *
 * Author(s):
 *	Original netiucv driver:
 *		Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *	Sysfs integration and all bugs therein:
 *		Cornelia Huck (cornelia.huck@de.ibm.com)
 *	PM functions:
 *		Ursula Braun (ursula.braun@de.ibm.com)
 *
 * Documentation used:
 *  the source of the original IUCV driver by:
 *    Stefan Hegewald <hegewald@de.ibm.com>
 *    Hartmut Penner <hpenner@de.ibm.com>
 *    Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *    Martin Schwidefsky (schwidefsky@de.ibm.com)
 *    Alan Altmark (Alan_Altmark@us.ibm.com)  Sept. 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define KMSG_COMPONENT "netiucv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#undef DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/bitops.h>

#include <linux/signal.h>
#include <linux/string.h>
#include <linux/device.h>

#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/dst.h>

#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/ebcdic.h>

#include <net/iucv/iucv.h>
#include "fsm.h"

MODULE_AUTHOR
    ("(C) 2001 IBM Corporation by Fritz Elfert (felfert@millenux.com)");
MODULE_DESCRIPTION ("Linux for S/390 IUCV network driver");

/**
 * Debug Facility stuff
 */
#define IUCV_DBF_SETUP_NAME "iucv_setup"
#define IUCV_DBF_SETUP_LEN 64
#define IUCV_DBF_SETUP_PAGES 2
#define IUCV_DBF_SETUP_NR_AREAS 1
#define IUCV_DBF_SETUP_LEVEL 3

#define IUCV_DBF_DATA_NAME "iucv_data"
#define IUCV_DBF_DATA_LEN 128
#define IUCV_DBF_DATA_PAGES 2
#define IUCV_DBF_DATA_NR_AREAS 1
#define IUCV_DBF_DATA_LEVEL 2

#define IUCV_DBF_TRACE_NAME "iucv_trace"
#define IUCV_DBF_TRACE_LEN 16
#define IUCV_DBF_TRACE_PAGES 4
#define IUCV_DBF_TRACE_NR_AREAS 1
#define IUCV_DBF_TRACE_LEVEL 3

#define IUCV_DBF_TEXT(name,level,text) \
	do { \
		debug_text_event(iucv_dbf_##name,level,text); \
	} while (0)

#define IUCV_DBF_HEX(name,level,addr,len) \
	do { \
		debug_event(iucv_dbf_##name,level,(void*)(addr),len); \
	} while (0)

DECLARE_PER_CPU(char[256], iucv_dbf_txt_buf);

#define IUCV_DBF_TEXT_(name, level, text...) \
	do { \
		if (debug_level_enabled(iucv_dbf_##name, level)) { \
			char* __buf = get_cpu_var(iucv_dbf_txt_buf); \
			sprintf(__buf, text); \
			debug_text_event(iucv_dbf_##name, level, __buf); \
			put_cpu_var(iucv_dbf_txt_buf); \
		} \
	} while (0)

#define IUCV_DBF_SPRINTF(name,level,text...) \
	do { \
		debug_sprintf_event(iucv_dbf_trace, level, ##text ); \
		debug_sprintf_event(iucv_dbf_trace, level, text ); \
	} while (0)

/**
 * some more debug stuff
 */
#define PRINTK_HEADER " iucv: "       /* for debugging */

/* dummy device to make sure netiucv_pm functions are called */
static struct device *netiucv_dev;

static int netiucv_pm_prepare(struct device *);
static void netiucv_pm_complete(struct device *);
static int netiucv_pm_freeze(struct device *);
static int netiucv_pm_restore_thaw(struct device *);

static const struct dev_pm_ops netiucv_pm_ops = {
	.prepare = netiucv_pm_prepare,
	.complete = netiucv_pm_complete,
	.freeze = netiucv_pm_freeze,
	.thaw = netiucv_pm_restore_thaw,
	.restore = netiucv_pm_restore_thaw,
};

static struct device_driver netiucv_driver = {
	.owner = THIS_MODULE,
	.name = "netiucv",
	.bus  = &iucv_bus,
	.pm = &netiucv_pm_ops,
};

static int netiucv_callback_connreq(struct iucv_path *, u8 *, u8 *);
static void netiucv_callback_connack(struct iucv_path *, u8 *);
static void netiucv_callback_connrej(struct iucv_path *, u8 *);
static void netiucv_callback_connsusp(struct iucv_path *, u8 *);
static void netiucv_callback_connres(struct iucv_path *, u8 *);
static void netiucv_callback_rx(struct iucv_path *, struct iucv_message *);
static void netiucv_callback_txdone(struct iucv_path *, struct iucv_message *);

static struct iucv_handler netiucv_handler = {
	.path_pending	  = netiucv_callback_connreq,
	.path_complete	  = netiucv_callback_connack,
	.path_severed	  = netiucv_callback_connrej,
	.path_quiesced	  = netiucv_callback_connsusp,
	.path_resumed	  = netiucv_callback_connres,
	.message_pending  = netiucv_callback_rx,
	.message_complete = netiucv_callback_txdone
};

/**
 * Per connection profiling data
 */
struct connection_profile {
	unsigned long maxmulti;
	unsigned long maxcqueue;
	unsigned long doios_single;
	unsigned long doios_multi;
	unsigned long txlen;
	unsigned long tx_time;
	unsigned long send_stamp;
	unsigned long tx_pending;
	unsigned long tx_max_pending;
};

/**
 * Representation of one iucv connection
 */
struct iucv_connection {
	struct list_head	  list;
	struct iucv_path	  *path;
	struct sk_buff            *rx_buff;
	struct sk_buff            *tx_buff;
	struct sk_buff_head       collect_queue;
	struct sk_buff_head	  commit_queue;
	spinlock_t                collect_lock;
	int                       collect_len;
	int                       max_buffsize;
	fsm_timer                 timer;
	fsm_instance              *fsm;
	struct net_device         *netdev;
	struct connection_profile prof;
	char                      userid[9];
	char			  userdata[17];
};

/**
 * Linked list of all connection structs.
 */
static LIST_HEAD(iucv_connection_list);
static DEFINE_RWLOCK(iucv_connection_rwlock);

/**
 * Representation of event-data for the
 * connection state machine.
 */
struct iucv_event {
	struct iucv_connection *conn;
	void                   *data;
};

/**
 * Private part of the network device structure
 */
struct netiucv_priv {
	struct net_device_stats stats;
	unsigned long           tbusy;
	fsm_instance            *fsm;
        struct iucv_connection  *conn;
	struct device           *dev;
	int			 pm_state;
};

/**
 * Link level header for a packet.
 */
struct ll_header {
	u16 next;
};

#define NETIUCV_HDRLEN		 (sizeof(struct ll_header))
#define NETIUCV_BUFSIZE_MAX	 65537
#define NETIUCV_BUFSIZE_DEFAULT  NETIUCV_BUFSIZE_MAX
#define NETIUCV_MTU_MAX          (NETIUCV_BUFSIZE_MAX - NETIUCV_HDRLEN)
#define NETIUCV_MTU_DEFAULT      9216
#define NETIUCV_QUEUELEN_DEFAULT 50
#define NETIUCV_TIMEOUT_5SEC     5000

/**
 * Compatibility macros for busy handling
 * of network devices.
 */
static inline void netiucv_clear_busy(struct net_device *dev)
{
	struct netiucv_priv *priv = netdev_priv(dev);
	clear_bit(0, &priv->tbusy);
	netif_wake_queue(dev);
}

static inline int netiucv_test_and_set_busy(struct net_device *dev)
{
	struct netiucv_priv *priv = netdev_priv(dev);
	netif_stop_queue(dev);
	return test_and_set_bit(0, &priv->tbusy);
}

static u8 iucvMagic_ascii[16] = {
	0x30, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x30, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
};

static u8 iucvMagic_ebcdic[16] = {
	0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

/**
 * Convert an iucv userId to its printable
 * form (strip whitespace at end).
 *
 * @param An iucv userId
 *
 * @returns The printable string (static data!!)
 */
static char *netiucv_printname(char *name, int len)
{
	static char tmp[17];
	char *p = tmp;
	memcpy(tmp, name, len);
	tmp[len] = '\0';
	while (*p && ((p - tmp) < len) && (!isspace(*p)))
		p++;
	*p = '\0';
	return tmp;
}

static char *netiucv_printuser(struct iucv_connection *conn)
{
	static char tmp_uid[9];
	static char tmp_udat[17];
	static char buf[100];

	if (memcmp(conn->userdata, iucvMagic_ebcdic, 16)) {
		tmp_uid[8] = '\0';
		tmp_udat[16] = '\0';
		memcpy(tmp_uid, netiucv_printname(conn->userid, 8), 8);
		memcpy(tmp_udat, conn->userdata, 16);
		EBCASC(tmp_udat, 16);
		memcpy(tmp_udat, netiucv_printname(tmp_udat, 16), 16);
		sprintf(buf, "%s.%s", tmp_uid, tmp_udat);
		return buf;
	} else
		return netiucv_printname(conn->userid, 8);
}

/**
 * States of the interface statemachine.
 */
enum dev_states {
	DEV_STATE_STOPPED,
	DEV_STATE_STARTWAIT,
	DEV_STATE_STOPWAIT,
	DEV_STATE_RUNNING,
	/**
	 * MUST be always the last element!!
	 */
	NR_DEV_STATES
};

static const char *dev_state_names[] = {
	"Stopped",
	"StartWait",
	"StopWait",
	"Running",
};

/**
 * Events of the interface statemachine.
 */
enum dev_events {
	DEV_EVENT_START,
	DEV_EVENT_STOP,
	DEV_EVENT_CONUP,
	DEV_EVENT_CONDOWN,
	/**
	 * MUST be always the last element!!
	 */
	NR_DEV_EVENTS
};

static const char *dev_event_names[] = {
	"Start",
	"Stop",
	"Connection up",
	"Connection down",
};

/**
 * Events of the connection statemachine
 */
enum conn_events {
	/**
	 * Events, representing callbacks from
	 * lowlevel iucv layer)
	 */
	CONN_EVENT_CONN_REQ,
	CONN_EVENT_CONN_ACK,
	CONN_EVENT_CONN_REJ,
	CONN_EVENT_CONN_SUS,
	CONN_EVENT_CONN_RES,
	CONN_EVENT_RX,
	CONN_EVENT_TXDONE,

	/**
	 * Events, representing errors return codes from
	 * calls to lowlevel iucv layer
	 */

	/**
	 * Event, representing timer expiry.
	 */
	CONN_EVENT_TIMER,

	/**
	 * Events, representing commands from upper levels.
	 */
	CONN_EVENT_START,
	CONN_EVENT_STOP,

	/**
	 * MUST be always the last element!!
	 */
	NR_CONN_EVENTS,
};

static const char *conn_event_names[] = {
	"Remote connection request",
	"Remote connection acknowledge",
	"Remote connection reject",
	"Connection suspended",
	"Connection resumed",
	"Data received",
	"Data sent",

	"Timer",

	"Start",
	"Stop",
};

/**
 * States of the connection statemachine.
 */
enum conn_states {
	/**
	 * Connection not assigned to any device,
	 * initial state, invalid
	 */
	CONN_STATE_INVALID,

	/**
	 * Userid assigned but not operating
	 */
	CONN_STATE_STOPPED,

	/**
	 * Connection registered,
	 * no connection request sent yet,
	 * no connection request received
	 */
	CONN_STATE_STARTWAIT,

	/**
	 * Connection registered and connection request sent,
	 * no acknowledge and no connection request received yet.
	 */
	CONN_STATE_SETUPWAIT,

	/**
	 * Connection up and running idle
	 */
	CONN_STATE_IDLE,

	/**
	 * Data sent, awaiting CONN_EVENT_TXDONE
	 */
	CONN_STATE_TX,

	/**
	 * Error during registration.
	 */
	CONN_STATE_REGERR,

	/**
	 * Error during registration.
	 */
	CONN_STATE_CONNERR,

	/**
	 * MUST be always the last element!!
	 */
	NR_CONN_STATES,
};

static const char *conn_state_names[] = {
	"Invalid",
	"Stopped",
	"StartWait",
	"SetupWait",
	"Idle",
	"TX",
	"Terminating",
	"Registration error",
	"Connect error",
};


/**
 * Debug Facility Stuff
 */
static debug_info_t *iucv_dbf_setup = NULL;
static debug_info_t *iucv_dbf_data = NULL;
static debug_info_t *iucv_dbf_trace = NULL;

DEFINE_PER_CPU(char[256], iucv_dbf_txt_buf);

static void iucv_unregister_dbf_views(void)
{
	debug_unregister(iucv_dbf_setup);
	debug_unregister(iucv_dbf_data);
	debug_unregister(iucv_dbf_trace);
}
static int iucv_register_dbf_views(void)
{
	iucv_dbf_setup = debug_register(IUCV_DBF_SETUP_NAME,
					IUCV_DBF_SETUP_PAGES,
					IUCV_DBF_SETUP_NR_AREAS,
					IUCV_DBF_SETUP_LEN);
	iucv_dbf_data = debug_register(IUCV_DBF_DATA_NAME,
				       IUCV_DBF_DATA_PAGES,
				       IUCV_DBF_DATA_NR_AREAS,
				       IUCV_DBF_DATA_LEN);
	iucv_dbf_trace = debug_register(IUCV_DBF_TRACE_NAME,
					IUCV_DBF_TRACE_PAGES,
					IUCV_DBF_TRACE_NR_AREAS,
					IUCV_DBF_TRACE_LEN);

	if ((iucv_dbf_setup == NULL) || (iucv_dbf_data == NULL) ||
	    (iucv_dbf_trace == NULL)) {
		iucv_unregister_dbf_views();
		return -ENOMEM;
	}
	debug_register_view(iucv_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(iucv_dbf_setup, IUCV_DBF_SETUP_LEVEL);

	debug_register_view(iucv_dbf_data, &debug_hex_ascii_view);
	debug_set_level(iucv_dbf_data, IUCV_DBF_DATA_LEVEL);

	debug_register_view(iucv_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(iucv_dbf_trace, IUCV_DBF_TRACE_LEVEL);

	return 0;
}

/*
 * Callback-wrappers, called from lowlevel iucv layer.
 */

static void netiucv_callback_rx(struct iucv_path *path,
				struct iucv_message *msg)
{
	struct iucv_connection *conn = path->private;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = msg;
	fsm_event(conn->fsm, CONN_EVENT_RX, &ev);
}

static void netiucv_callback_txdone(struct iucv_path *path,
				    struct iucv_message *msg)
{
	struct iucv_connection *conn = path->private;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = msg;
	fsm_event(conn->fsm, CONN_EVENT_TXDONE, &ev);
}

static void netiucv_callback_connack(struct iucv_path *path, u8 ipuser[16])
{
	struct iucv_connection *conn = path->private;

	fsm_event(conn->fsm, CONN_EVENT_CONN_ACK, conn);
}

static int netiucv_callback_connreq(struct iucv_path *path, u8 *ipvmid,
				    u8 *ipuser)
{
	struct iucv_connection *conn = path->private;
	struct iucv_event ev;
	static char tmp_user[9];
	static char tmp_udat[17];
	int rc;

	rc = -EINVAL;
	memcpy(tmp_user, netiucv_printname(ipvmid, 8), 8);
	memcpy(tmp_udat, ipuser, 16);
	EBCASC(tmp_udat, 16);
	read_lock_bh(&iucv_connection_rwlock);
	list_for_each_entry(conn, &iucv_connection_list, list) {
		if (strncmp(ipvmid, conn->userid, 8) ||
		    strncmp(ipuser, conn->userdata, 16))
			continue;
		/* Found a matching connection for this path. */
		conn->path = path;
		ev.conn = conn;
		ev.data = path;
		fsm_event(conn->fsm, CONN_EVENT_CONN_REQ, &ev);
		rc = 0;
	}
	IUCV_DBF_TEXT_(setup, 2, "Connection requested for %s.%s\n",
		       tmp_user, netiucv_printname(tmp_udat, 16));
	read_unlock_bh(&iucv_connection_rwlock);
	return rc;
}

static void netiucv_callback_connrej(struct iucv_path *path, u8 *ipuser)
{
	struct iucv_connection *conn = path->private;

	fsm_event(conn->fsm, CONN_EVENT_CONN_REJ, conn);
}

static void netiucv_callback_connsusp(struct iucv_path *path, u8 *ipuser)
{
	struct iucv_connection *conn = path->private;

	fsm_event(conn->fsm, CONN_EVENT_CONN_SUS, conn);
}

static void netiucv_callback_connres(struct iucv_path *path, u8 *ipuser)
{
	struct iucv_connection *conn = path->private;

	fsm_event(conn->fsm, CONN_EVENT_CONN_RES, conn);
}

/**
 * NOP action for statemachines
 */
static void netiucv_action_nop(fsm_instance *fi, int event, void *arg)
{
}

/*
 * Actions of the connection statemachine
 */

/**
 * netiucv_unpack_skb
 * @conn: The connection where this skb has been received.
 * @pskb: The received skb.
 *
 * Unpack a just received skb and hand it over to upper layers.
 * Helper function for conn_action_rx.
 */
static void netiucv_unpack_skb(struct iucv_connection *conn,
			       struct sk_buff *pskb)
{
	struct net_device     *dev = conn->netdev;
	struct netiucv_priv   *privptr = netdev_priv(dev);
	u16 offset = 0;

	skb_put(pskb, NETIUCV_HDRLEN);
	pskb->dev = dev;
	pskb->ip_summed = CHECKSUM_NONE;
	pskb->protocol = cpu_to_be16(ETH_P_IP);

	while (1) {
		struct sk_buff *skb;
		struct ll_header *header = (struct ll_header *) pskb->data;

		if (!header->next)
			break;

		skb_pull(pskb, NETIUCV_HDRLEN);
		header->next -= offset;
		offset += header->next;
		header->next -= NETIUCV_HDRLEN;
		if (skb_tailroom(pskb) < header->next) {
			IUCV_DBF_TEXT_(data, 2, "Illegal next field: %d > %d\n",
				header->next, skb_tailroom(pskb));
			return;
		}
		skb_put(pskb, header->next);
		skb_reset_mac_header(pskb);
		skb = dev_alloc_skb(pskb->len);
		if (!skb) {
			IUCV_DBF_TEXT(data, 2,
				"Out of memory in netiucv_unpack_skb\n");
			privptr->stats.rx_dropped++;
			return;
		}
		skb_copy_from_linear_data(pskb, skb_put(skb, pskb->len),
					  pskb->len);
		skb_reset_mac_header(skb);
		skb->dev = pskb->dev;
		skb->protocol = pskb->protocol;
		pskb->ip_summed = CHECKSUM_UNNECESSARY;
		privptr->stats.rx_packets++;
		privptr->stats.rx_bytes += skb->len;
		/*
		 * Since receiving is always initiated from a tasklet (in iucv.c),
		 * we must use netif_rx_ni() instead of netif_rx()
		 */
		netif_rx_ni(skb);
		skb_pull(pskb, header->next);
		skb_put(pskb, NETIUCV_HDRLEN);
	}
}

static void conn_action_rx(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = arg;
	struct iucv_connection *conn = ev->conn;
	struct iucv_message *msg = ev->data;
	struct netiucv_priv *privptr = netdev_priv(conn->netdev);
	int rc;

	IUCV_DBF_TEXT(trace, 4, __func__);

	if (!conn->netdev) {
		iucv_message_reject(conn->path, msg);
		IUCV_DBF_TEXT(data, 2,
			      "Received data for unlinked connection\n");
		return;
	}
	if (msg->length > conn->max_buffsize) {
		iucv_message_reject(conn->path, msg);
		privptr->stats.rx_dropped++;
		IUCV_DBF_TEXT_(data, 2, "msglen %d > max_buffsize %d\n",
			       msg->length, conn->max_buffsize);
		return;
	}
	conn->rx_buff->data = conn->rx_buff->head;
	skb_reset_tail_pointer(conn->rx_buff);
	conn->rx_buff->len = 0;
	rc = iucv_message_receive(conn->path, msg, 0, conn->rx_buff->data,
				  msg->length, NULL);
	if (rc || msg->length < 5) {
		privptr->stats.rx_errors++;
		IUCV_DBF_TEXT_(data, 2, "rc %d from iucv_receive\n", rc);
		return;
	}
	netiucv_unpack_skb(conn, conn->rx_buff);
}

static void conn_action_txdone(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = arg;
	struct iucv_connection *conn = ev->conn;
	struct iucv_message *msg = ev->data;
	struct iucv_message txmsg;
	struct netiucv_priv *privptr = NULL;
	u32 single_flag = msg->tag;
	u32 txbytes = 0;
	u32 txpackets = 0;
	u32 stat_maxcq = 0;
	struct sk_buff *skb;
	unsigned long saveflags;
	struct ll_header header;
	int rc;

	IUCV_DBF_TEXT(trace, 4, __func__);

	if (!conn || !conn->netdev) {
		IUCV_DBF_TEXT(data, 2,
			      "Send confirmation for unlinked connection\n");
		return;
	}
	privptr = netdev_priv(conn->netdev);
	conn->prof.tx_pending--;
	if (single_flag) {
		if ((skb = skb_dequeue(&conn->commit_queue))) {
			atomic_dec(&skb->users);
			if (privptr) {
				privptr->stats.tx_packets++;
				privptr->stats.tx_bytes +=
					(skb->len - NETIUCV_HDRLEN
						  - NETIUCV_HDRLEN);
			}
			dev_kfree_skb_any(skb);
		}
	}
	conn->tx_buff->data = conn->tx_buff->head;
	skb_reset_tail_pointer(conn->tx_buff);
	conn->tx_buff->len = 0;
	spin_lock_irqsave(&conn->collect_lock, saveflags);
	while ((skb = skb_dequeue(&conn->collect_queue))) {
		header.next = conn->tx_buff->len + skb->len + NETIUCV_HDRLEN;
		memcpy(skb_put(conn->tx_buff, NETIUCV_HDRLEN), &header,
		       NETIUCV_HDRLEN);
		skb_copy_from_linear_data(skb,
					  skb_put(conn->tx_buff, skb->len),
					  skb->len);
		txbytes += skb->len;
		txpackets++;
		stat_maxcq++;
		atomic_dec(&skb->users);
		dev_kfree_skb_any(skb);
	}
	if (conn->collect_len > conn->prof.maxmulti)
		conn->prof.maxmulti = conn->collect_len;
	conn->collect_len = 0;
	spin_unlock_irqrestore(&conn->collect_lock, saveflags);
	if (conn->tx_buff->len == 0) {
		fsm_newstate(fi, CONN_STATE_IDLE);
		return;
	}

	header.next = 0;
	memcpy(skb_put(conn->tx_buff, NETIUCV_HDRLEN), &header, NETIUCV_HDRLEN);
	conn->prof.send_stamp = jiffies;
	txmsg.class = 0;
	txmsg.tag = 0;
	rc = iucv_message_send(conn->path, &txmsg, 0, 0,
			       conn->tx_buff->data, conn->tx_buff->len);
	conn->prof.doios_multi++;
	conn->prof.txlen += conn->tx_buff->len;
	conn->prof.tx_pending++;
	if (conn->prof.tx_pending > conn->prof.tx_max_pending)
		conn->prof.tx_max_pending = conn->prof.tx_pending;
	if (rc) {
		conn->prof.tx_pending--;
		fsm_newstate(fi, CONN_STATE_IDLE);
		if (privptr)
			privptr->stats.tx_errors += txpackets;
		IUCV_DBF_TEXT_(data, 2, "rc %d from iucv_send\n", rc);
	} else {
		if (privptr) {
			privptr->stats.tx_packets += txpackets;
			privptr->stats.tx_bytes += txbytes;
		}
		if (stat_maxcq > conn->prof.maxcqueue)
			conn->prof.maxcqueue = stat_maxcq;
	}
}

static void conn_action_connaccept(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = arg;
	struct iucv_connection *conn = ev->conn;
	struct iucv_path *path = ev->data;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = netdev_priv(netdev);
	int rc;

	IUCV_DBF_TEXT(trace, 3, __func__);

	conn->path = path;
	path->msglim = NETIUCV_QUEUELEN_DEFAULT;
	path->flags = 0;
	rc = iucv_path_accept(path, &netiucv_handler, conn->userdata , conn);
	if (rc) {
		IUCV_DBF_TEXT_(setup, 2, "rc %d from iucv_accept", rc);
		return;
	}
	fsm_newstate(fi, CONN_STATE_IDLE);
	netdev->tx_queue_len = conn->path->msglim;
	fsm_event(privptr->fsm, DEV_EVENT_CONUP, netdev);
}

static void conn_action_connreject(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = arg;
	struct iucv_path *path = ev->data;

	IUCV_DBF_TEXT(trace, 3, __func__);
	iucv_path_sever(path, NULL);
}

static void conn_action_connack(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = arg;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = netdev_priv(netdev);

	IUCV_DBF_TEXT(trace, 3, __func__);
	fsm_deltimer(&conn->timer);
	fsm_newstate(fi, CONN_STATE_IDLE);
	netdev->tx_queue_len = conn->path->msglim;
	fsm_event(privptr->fsm, DEV_EVENT_CONUP, netdev);
}

static void conn_action_conntimsev(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = arg;

	IUCV_DBF_TEXT(trace, 3, __func__);
	fsm_deltimer(&conn->timer);
	iucv_path_sever(conn->path, conn->userdata);
	fsm_newstate(fi, CONN_STATE_STARTWAIT);
}

static void conn_action_connsever(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = arg;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = netdev_priv(netdev);

	IUCV_DBF_TEXT(trace, 3, __func__);

	fsm_deltimer(&conn->timer);
	iucv_path_sever(conn->path, conn->userdata);
	dev_info(privptr->dev, "The peer z/VM guest %s has closed the "
			       "connection\n", netiucv_printuser(conn));
	IUCV_DBF_TEXT(data, 2,
		      "conn_action_connsever: Remote dropped connection\n");
	fsm_newstate(fi, CONN_STATE_STARTWAIT);
	fsm_event(privptr->fsm, DEV_EVENT_CONDOWN, netdev);
}

static void conn_action_start(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = arg;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = netdev_priv(netdev);
	int rc;

	IUCV_DBF_TEXT(trace, 3, __func__);

	fsm_newstate(fi, CONN_STATE_STARTWAIT);

	/*
	 * We must set the state before calling iucv_connect because the
	 * callback handler could be called at any point after the connection
	 * request is sent
	 */

	fsm_newstate(fi, CONN_STATE_SETUPWAIT);
	conn->path = iucv_path_alloc(NETIUCV_QUEUELEN_DEFAULT, 0, GFP_KERNEL);
	IUCV_DBF_TEXT_(setup, 2, "%s: connecting to %s ...\n",
		netdev->name, netiucv_printuser(conn));

	rc = iucv_path_connect(conn->path, &netiucv_handler, conn->userid,
			       NULL, conn->userdata, conn);
	switch (rc) {
	case 0:
		netdev->tx_queue_len = conn->path->msglim;
		fsm_addtimer(&conn->timer, NETIUCV_TIMEOUT_5SEC,
			     CONN_EVENT_TIMER, conn);
		return;
	case 11:
		dev_warn(privptr->dev,
			"The IUCV device failed to connect to z/VM guest %s\n",
			netiucv_printname(conn->userid, 8));
		fsm_newstate(fi, CONN_STATE_STARTWAIT);
		break;
	case 12:
		dev_warn(privptr->dev,
			"The IUCV device failed to connect to the peer on z/VM"
			" guest %s\n", netiucv_printname(conn->userid, 8));
		fsm_newstate(fi, CONN_STATE_STARTWAIT);
		break;
	case 13:
		dev_err(privptr->dev,
			"Connecting the IUCV device would exceed the maximum"
			" number of IUCV connections\n");
		fsm_newstate(fi, CONN_STATE_CONNERR);
		break;
	case 14:
		dev_err(privptr->dev,
			"z/VM guest %s has too many IUCV connections"
			" to connect with the IUCV device\n",
			netiucv_printname(conn->userid, 8));
		fsm_newstate(fi, CONN_STATE_CONNERR);
		break;
	case 15:
		dev_err(privptr->dev,
			"The IUCV device cannot connect to a z/VM guest with no"
			" IUCV authorization\n");
		fsm_newstate(fi, CONN_STATE_CONNERR);
		break;
	default:
		dev_err(privptr->dev,
			"Connecting the IUCV device failed with error %d\n",
			rc);
		fsm_newstate(fi, CONN_STATE_CONNERR);
		break;
	}
	IUCV_DBF_TEXT_(setup, 5, "iucv_connect rc is %d\n", rc);
	kfree(conn->path);
	conn->path = NULL;
}

static void netiucv_purge_skb_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(q))) {
		atomic_dec(&skb->users);
		dev_kfree_skb_any(skb);
	}
}

static void conn_action_stop(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = arg;
	struct iucv_connection *conn = ev->conn;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = netdev_priv(netdev);

	IUCV_DBF_TEXT(trace, 3, __func__);

	fsm_deltimer(&conn->timer);
	fsm_newstate(fi, CONN_STATE_STOPPED);
	netiucv_purge_skb_queue(&conn->collect_queue);
	if (conn->path) {
		IUCV_DBF_TEXT(trace, 5, "calling iucv_path_sever\n");
		iucv_path_sever(conn->path, conn->userdata);
		kfree(conn->path);
		conn->path = NULL;
	}
	netiucv_purge_skb_queue(&conn->commit_queue);
	fsm_event(privptr->fsm, DEV_EVENT_CONDOWN, netdev);
}

static void conn_action_inval(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = arg;
	struct net_device *netdev = conn->netdev;

	IUCV_DBF_TEXT_(data, 2, "%s('%s'): conn_action_inval called\n",
		netdev->name, conn->userid);
}

static const fsm_node conn_fsm[] = {
	{ CONN_STATE_INVALID,   CONN_EVENT_START,    conn_action_inval      },
	{ CONN_STATE_STOPPED,   CONN_EVENT_START,    conn_action_start      },

	{ CONN_STATE_STOPPED,   CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_STARTWAIT, CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_IDLE,      CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_TX,        CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_REGERR,    CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_CONNERR,   CONN_EVENT_STOP,     conn_action_stop       },

	{ CONN_STATE_STOPPED,   CONN_EVENT_CONN_REQ, conn_action_connreject },
        { CONN_STATE_STARTWAIT, CONN_EVENT_CONN_REQ, conn_action_connaccept },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_REQ, conn_action_connaccept },
	{ CONN_STATE_IDLE,      CONN_EVENT_CONN_REQ, conn_action_connreject },
	{ CONN_STATE_TX,        CONN_EVENT_CONN_REQ, conn_action_connreject },

	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_ACK, conn_action_connack    },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_TIMER,    conn_action_conntimsev },

	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_REJ, conn_action_connsever  },
	{ CONN_STATE_IDLE,      CONN_EVENT_CONN_REJ, conn_action_connsever  },
	{ CONN_STATE_TX,        CONN_EVENT_CONN_REJ, conn_action_connsever  },

	{ CONN_STATE_IDLE,      CONN_EVENT_RX,       conn_action_rx         },
	{ CONN_STATE_TX,        CONN_EVENT_RX,       conn_action_rx         },

	{ CONN_STATE_TX,        CONN_EVENT_TXDONE,   conn_action_txdone     },
	{ CONN_STATE_IDLE,      CONN_EVENT_TXDONE,   conn_action_txdone     },
};

static const int CONN_FSM_LEN = sizeof(conn_fsm) / sizeof(fsm_node);


/*
 * Actions for interface - statemachine.
 */

/**
 * dev_action_start
 * @fi: An instance of an interface statemachine.
 * @event: The event, just happened.
 * @arg: Generic pointer, casted from struct net_device * upon call.
 *
 * Startup connection by sending CONN_EVENT_START to it.
 */
static void dev_action_start(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = arg;
	struct netiucv_priv *privptr = netdev_priv(dev);

	IUCV_DBF_TEXT(trace, 3, __func__);

	fsm_newstate(fi, DEV_STATE_STARTWAIT);
	fsm_event(privptr->conn->fsm, CONN_EVENT_START, privptr->conn);
}

/**
 * Shutdown connection by sending CONN_EVENT_STOP to it.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_stop(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = arg;
	struct netiucv_priv *privptr = netdev_priv(dev);
	struct iucv_event   ev;

	IUCV_DBF_TEXT(trace, 3, __func__);

	ev.conn = privptr->conn;

	fsm_newstate(fi, DEV_STATE_STOPWAIT);
	fsm_event(privptr->conn->fsm, CONN_EVENT_STOP, &ev);
}

/**
 * Called from connection statemachine
 * when a connection is up and running.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_connup(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = arg;
	struct netiucv_priv *privptr = netdev_priv(dev);

	IUCV_DBF_TEXT(trace, 3, __func__);

	switch (fsm_getstate(fi)) {
		case DEV_STATE_STARTWAIT:
			fsm_newstate(fi, DEV_STATE_RUNNING);
			dev_info(privptr->dev,
				"The IUCV device has been connected"
				" successfully to %s\n",
				netiucv_printuser(privptr->conn));
			IUCV_DBF_TEXT(setup, 3,
				"connection is up and running\n");
			break;
		case DEV_STATE_STOPWAIT:
			IUCV_DBF_TEXT(data, 2,
				"dev_action_connup: in DEV_STATE_STOPWAIT\n");
			break;
	}
}

/**
 * Called from connection statemachine
 * when a connection has been shutdown.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_conndown(fsm_instance *fi, int event, void *arg)
{
	IUCV_DBF_TEXT(trace, 3, __func__);

	switch (fsm_getstate(fi)) {
		case DEV_STATE_RUNNING:
			fsm_newstate(fi, DEV_STATE_STARTWAIT);
			break;
		case DEV_STATE_STOPWAIT:
			fsm_newstate(fi, DEV_STATE_STOPPED);
			IUCV_DBF_TEXT(setup, 3, "connection is down\n");
			break;
	}
}

static const fsm_node dev_fsm[] = {
	{ DEV_STATE_STOPPED,    DEV_EVENT_START,   dev_action_start    },

	{ DEV_STATE_STOPWAIT,   DEV_EVENT_START,   dev_action_start    },
	{ DEV_STATE_STOPWAIT,   DEV_EVENT_CONDOWN, dev_action_conndown },

	{ DEV_STATE_STARTWAIT,  DEV_EVENT_STOP,    dev_action_stop     },
	{ DEV_STATE_STARTWAIT,  DEV_EVENT_CONUP,   dev_action_connup   },

	{ DEV_STATE_RUNNING,    DEV_EVENT_STOP,    dev_action_stop     },
	{ DEV_STATE_RUNNING,    DEV_EVENT_CONDOWN, dev_action_conndown },
	{ DEV_STATE_RUNNING,    DEV_EVENT_CONUP,   netiucv_action_nop  },
};

static const int DEV_FSM_LEN = sizeof(dev_fsm) / sizeof(fsm_node);

/**
 * Transmit a packet.
 * This is a helper function for netiucv_tx().
 *
 * @param conn Connection to be used for sending.
 * @param skb Pointer to struct sk_buff of packet to send.
 *            The linklevel header has already been set up
 *            by netiucv_tx().
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int netiucv_transmit_skb(struct iucv_connection *conn,
				struct sk_buff *skb)
{
	struct iucv_message msg;
	unsigned long saveflags;
	struct ll_header header;
	int rc;

	if (fsm_getstate(conn->fsm) != CONN_STATE_IDLE) {
		int l = skb->len + NETIUCV_HDRLEN;

		spin_lock_irqsave(&conn->collect_lock, saveflags);
		if (conn->collect_len + l >
		    (conn->max_buffsize - NETIUCV_HDRLEN)) {
			rc = -EBUSY;
			IUCV_DBF_TEXT(data, 2,
				      "EBUSY from netiucv_transmit_skb\n");
		} else {
			atomic_inc(&skb->users);
			skb_queue_tail(&conn->collect_queue, skb);
			conn->collect_len += l;
			rc = 0;
		}
		spin_unlock_irqrestore(&conn->collect_lock, saveflags);
	} else {
		struct sk_buff *nskb = skb;
		/**
		 * Copy the skb to a new allocated skb in lowmem only if the
		 * data is located above 2G in memory or tailroom is < 2.
		 */
		unsigned long hi = ((unsigned long)(skb_tail_pointer(skb) +
				    NETIUCV_HDRLEN)) >> 31;
		int copied = 0;
		if (hi || (skb_tailroom(skb) < 2)) {
			nskb = alloc_skb(skb->len + NETIUCV_HDRLEN +
					 NETIUCV_HDRLEN, GFP_ATOMIC | GFP_DMA);
			if (!nskb) {
				IUCV_DBF_TEXT(data, 2, "alloc_skb failed\n");
				rc = -ENOMEM;
				return rc;
			} else {
				skb_reserve(nskb, NETIUCV_HDRLEN);
				memcpy(skb_put(nskb, skb->len),
				       skb->data, skb->len);
			}
			copied = 1;
		}
		/**
		 * skb now is below 2G and has enough room. Add headers.
		 */
		header.next = nskb->len + NETIUCV_HDRLEN;
		memcpy(skb_push(nskb, NETIUCV_HDRLEN), &header, NETIUCV_HDRLEN);
		header.next = 0;
		memcpy(skb_put(nskb, NETIUCV_HDRLEN), &header,  NETIUCV_HDRLEN);

		fsm_newstate(conn->fsm, CONN_STATE_TX);
		conn->prof.send_stamp = jiffies;

		msg.tag = 1;
		msg.class = 0;
		rc = iucv_message_send(conn->path, &msg, 0, 0,
				       nskb->data, nskb->len);
		conn->prof.doios_single++;
		conn->prof.txlen += skb->len;
		conn->prof.tx_pending++;
		if (conn->prof.tx_pending > conn->prof.tx_max_pending)
			conn->prof.tx_max_pending = conn->prof.tx_pending;
		if (rc) {
			struct netiucv_priv *privptr;
			fsm_newstate(conn->fsm, CONN_STATE_IDLE);
			conn->prof.tx_pending--;
			privptr = netdev_priv(conn->netdev);
			if (privptr)
				privptr->stats.tx_errors++;
			if (copied)
				dev_kfree_skb(nskb);
			else {
				/**
				 * Remove our headers. They get added
				 * again on retransmit.
				 */
				skb_pull(skb, NETIUCV_HDRLEN);
				skb_trim(skb, skb->len - NETIUCV_HDRLEN);
			}
			IUCV_DBF_TEXT_(data, 2, "rc %d from iucv_send\n", rc);
		} else {
			if (copied)
				dev_kfree_skb(skb);
			atomic_inc(&nskb->users);
			skb_queue_tail(&conn->commit_queue, nskb);
		}
	}

	return rc;
}

/*
 * Interface API for upper network layers
 */

/**
 * Open an interface.
 * Called from generic network layer when ifconfig up is run.
 *
 * @param dev Pointer to interface struct.
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int netiucv_open(struct net_device *dev)
{
	struct netiucv_priv *priv = netdev_priv(dev);

	fsm_event(priv->fsm, DEV_EVENT_START, dev);
	return 0;
}

/**
 * Close an interface.
 * Called from generic network layer when ifconfig down is run.
 *
 * @param dev Pointer to interface struct.
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int netiucv_close(struct net_device *dev)
{
	struct netiucv_priv *priv = netdev_priv(dev);

	fsm_event(priv->fsm, DEV_EVENT_STOP, dev);
	return 0;
}

static int netiucv_pm_prepare(struct device *dev)
{
	IUCV_DBF_TEXT(trace, 3, __func__);
	return 0;
}

static void netiucv_pm_complete(struct device *dev)
{
	IUCV_DBF_TEXT(trace, 3, __func__);
	return;
}

/**
 * netiucv_pm_freeze() - Freeze PM callback
 * @dev:	netiucv device
 *
 * close open netiucv interfaces
 */
static int netiucv_pm_freeze(struct device *dev)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = NULL;
	int rc = 0;

	IUCV_DBF_TEXT(trace, 3, __func__);
	if (priv && priv->conn)
		ndev = priv->conn->netdev;
	if (!ndev)
		goto out;
	netif_device_detach(ndev);
	priv->pm_state = fsm_getstate(priv->fsm);
	rc = netiucv_close(ndev);
out:
	return rc;
}

/**
 * netiucv_pm_restore_thaw() - Thaw and restore PM callback
 * @dev:	netiucv device
 *
 * re-open netiucv interfaces closed during freeze
 */
static int netiucv_pm_restore_thaw(struct device *dev)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = NULL;
	int rc = 0;

	IUCV_DBF_TEXT(trace, 3, __func__);
	if (priv && priv->conn)
		ndev = priv->conn->netdev;
	if (!ndev)
		goto out;
	switch (priv->pm_state) {
	case DEV_STATE_RUNNING:
	case DEV_STATE_STARTWAIT:
		rc = netiucv_open(ndev);
		break;
	default:
		break;
	}
	netif_device_attach(ndev);
out:
	return rc;
}

/**
 * Start transmission of a packet.
 * Called from generic network device layer.
 *
 * @param skb Pointer to buffer containing the packet.
 * @param dev Pointer to interface struct.
 *
 * @return 0 if packet consumed, !0 if packet rejected.
 *         Note: If we return !0, then the packet is free'd by
 *               the generic network layer.
 */
static int netiucv_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netiucv_priv *privptr = netdev_priv(dev);
	int rc;

	IUCV_DBF_TEXT(trace, 4, __func__);
	/**
	 * Some sanity checks ...
	 */
	if (skb == NULL) {
		IUCV_DBF_TEXT(data, 2, "netiucv_tx: skb is NULL\n");
		privptr->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	if (skb_headroom(skb) < NETIUCV_HDRLEN) {
		IUCV_DBF_TEXT(data, 2,
			"netiucv_tx: skb_headroom < NETIUCV_HDRLEN\n");
		dev_kfree_skb(skb);
		privptr->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/**
	 * If connection is not running, try to restart it
	 * and throw away packet.
	 */
	if (fsm_getstate(privptr->fsm) != DEV_STATE_RUNNING) {
		dev_kfree_skb(skb);
		privptr->stats.tx_dropped++;
		privptr->stats.tx_errors++;
		privptr->stats.tx_carrier_errors++;
		return NETDEV_TX_OK;
	}

	if (netiucv_test_and_set_busy(dev)) {
		IUCV_DBF_TEXT(data, 2, "EBUSY from netiucv_tx\n");
		return NETDEV_TX_BUSY;
	}
	netif_trans_update(dev);
	rc = netiucv_transmit_skb(privptr->conn, skb);
	netiucv_clear_busy(dev);
	return rc ? NETDEV_TX_BUSY : NETDEV_TX_OK;
}

/**
 * netiucv_stats
 * @dev: Pointer to interface struct.
 *
 * Returns interface statistics of a device.
 *
 * Returns pointer to stats struct of this interface.
 */
static struct net_device_stats *netiucv_stats (struct net_device * dev)
{
	struct netiucv_priv *priv = netdev_priv(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return &priv->stats;
}

/*
 * attributes in sysfs
 */

static ssize_t user_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%s\n", netiucv_printuser(priv->conn));
}

static int netiucv_check_user(const char *buf, size_t count, char *username,
			      char *userdata)
{
	const char *p;
	int i;

	p = strchr(buf, '.');
	if ((p && ((count > 26) ||
		   ((p - buf) > 8) ||
		   (buf + count - p > 18))) ||
	    (!p && (count > 9))) {
		IUCV_DBF_TEXT(setup, 2, "conn_write: too long\n");
		return -EINVAL;
	}

	for (i = 0, p = buf; i < 8 && *p && *p != '.'; i++, p++) {
		if (isalnum(*p) || *p == '$') {
			username[i] = toupper(*p);
			continue;
		}
		if (*p == '\n')
			/* trailing lf, grr */
			break;
		IUCV_DBF_TEXT_(setup, 2,
			       "conn_write: invalid character %02x\n", *p);
		return -EINVAL;
	}
	while (i < 8)
		username[i++] = ' ';
	username[8] = '\0';

	if (*p == '.') {
		p++;
		for (i = 0; i < 16 && *p; i++, p++) {
			if (*p == '\n')
				break;
			userdata[i] = toupper(*p);
		}
		while (i > 0 && i < 16)
			userdata[i++] = ' ';
	} else
		memcpy(userdata, iucvMagic_ascii, 16);
	userdata[16] = '\0';
	ASCEBC(userdata, 16);

	return 0;
}

static ssize_t user_write(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->conn->netdev;
	char	username[9];
	char	userdata[17];
	int	rc;
	struct iucv_connection *cp;

	IUCV_DBF_TEXT(trace, 3, __func__);
	rc = netiucv_check_user(buf, count, username, userdata);
	if (rc)
		return rc;

	if (memcmp(username, priv->conn->userid, 9) &&
	    (ndev->flags & (IFF_UP | IFF_RUNNING))) {
		/* username changed while the interface is active. */
		IUCV_DBF_TEXT(setup, 2, "user_write: device active\n");
		return -EPERM;
	}
	read_lock_bh(&iucv_connection_rwlock);
	list_for_each_entry(cp, &iucv_connection_list, list) {
		if (!strncmp(username, cp->userid, 9) &&
		   !strncmp(userdata, cp->userdata, 17) && cp->netdev != ndev) {
			read_unlock_bh(&iucv_connection_rwlock);
			IUCV_DBF_TEXT_(setup, 2, "user_write: Connection to %s "
				"already exists\n", netiucv_printuser(cp));
			return -EEXIST;
		}
	}
	read_unlock_bh(&iucv_connection_rwlock);
	memcpy(priv->conn->userid, username, 9);
	memcpy(priv->conn->userdata, userdata, 17);
	return count;
}

static DEVICE_ATTR(user, 0644, user_show, user_write);

static ssize_t buffer_show (struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%d\n", priv->conn->max_buffsize);
}

static ssize_t buffer_write (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->conn->netdev;
	unsigned int bs1;
	int rc;

	IUCV_DBF_TEXT(trace, 3, __func__);
	if (count >= 39)
		return -EINVAL;

	rc = kstrtouint(buf, 0, &bs1);

	if (rc == -EINVAL) {
		IUCV_DBF_TEXT_(setup, 2, "buffer_write: invalid char %s\n",
			buf);
		return -EINVAL;
	}
	if ((rc == -ERANGE) || (bs1 > NETIUCV_BUFSIZE_MAX)) {
		IUCV_DBF_TEXT_(setup, 2,
			"buffer_write: buffer size %d too large\n",
			bs1);
		return -EINVAL;
	}
	if ((ndev->flags & IFF_RUNNING) &&
	    (bs1 < (ndev->mtu + NETIUCV_HDRLEN + 2))) {
		IUCV_DBF_TEXT_(setup, 2,
			"buffer_write: buffer size %d too small\n",
			bs1);
		return -EINVAL;
	}
	if (bs1 < (576 + NETIUCV_HDRLEN + NETIUCV_HDRLEN)) {
		IUCV_DBF_TEXT_(setup, 2,
			"buffer_write: buffer size %d too small\n",
			bs1);
		return -EINVAL;
	}

	priv->conn->max_buffsize = bs1;
	if (!(ndev->flags & IFF_RUNNING))
		ndev->mtu = bs1 - NETIUCV_HDRLEN - NETIUCV_HDRLEN;

	return count;

}

static DEVICE_ATTR(buffer, 0644, buffer_show, buffer_write);

static ssize_t dev_fsm_show (struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%s\n", fsm_getstate_str(priv->fsm));
}

static DEVICE_ATTR(device_fsm_state, 0444, dev_fsm_show, NULL);

static ssize_t conn_fsm_show (struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%s\n", fsm_getstate_str(priv->conn->fsm));
}

static DEVICE_ATTR(connection_fsm_state, 0444, conn_fsm_show, NULL);

static ssize_t maxmulti_show (struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.maxmulti);
}

static ssize_t maxmulti_write (struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.maxmulti = 0;
	return count;
}

static DEVICE_ATTR(max_tx_buffer_used, 0644, maxmulti_show, maxmulti_write);

static ssize_t maxcq_show (struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.maxcqueue);
}

static ssize_t maxcq_write (struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.maxcqueue = 0;
	return count;
}

static DEVICE_ATTR(max_chained_skbs, 0644, maxcq_show, maxcq_write);

static ssize_t sdoio_show (struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.doios_single);
}

static ssize_t sdoio_write (struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.doios_single = 0;
	return count;
}

static DEVICE_ATTR(tx_single_write_ops, 0644, sdoio_show, sdoio_write);

static ssize_t mdoio_show (struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.doios_multi);
}

static ssize_t mdoio_write (struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	priv->conn->prof.doios_multi = 0;
	return count;
}

static DEVICE_ATTR(tx_multi_write_ops, 0644, mdoio_show, mdoio_write);

static ssize_t txlen_show (struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.txlen);
}

static ssize_t txlen_write (struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.txlen = 0;
	return count;
}

static DEVICE_ATTR(netto_bytes, 0644, txlen_show, txlen_write);

static ssize_t txtime_show (struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.tx_time);
}

static ssize_t txtime_write (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.tx_time = 0;
	return count;
}

static DEVICE_ATTR(max_tx_io_time, 0644, txtime_show, txtime_write);

static ssize_t txpend_show (struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.tx_pending);
}

static ssize_t txpend_write (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.tx_pending = 0;
	return count;
}

static DEVICE_ATTR(tx_pending, 0644, txpend_show, txpend_write);

static ssize_t txmpnd_show (struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 5, __func__);
	return sprintf(buf, "%ld\n", priv->conn->prof.tx_max_pending);
}

static ssize_t txmpnd_write (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev_get_drvdata(dev);

	IUCV_DBF_TEXT(trace, 4, __func__);
	priv->conn->prof.tx_max_pending = 0;
	return count;
}

static DEVICE_ATTR(tx_max_pending, 0644, txmpnd_show, txmpnd_write);

static struct attribute *netiucv_attrs[] = {
	&dev_attr_buffer.attr,
	&dev_attr_user.attr,
	NULL,
};

static struct attribute_group netiucv_attr_group = {
	.attrs = netiucv_attrs,
};

static struct attribute *netiucv_stat_attrs[] = {
	&dev_attr_device_fsm_state.attr,
	&dev_attr_connection_fsm_state.attr,
	&dev_attr_max_tx_buffer_used.attr,
	&dev_attr_max_chained_skbs.attr,
	&dev_attr_tx_single_write_ops.attr,
	&dev_attr_tx_multi_write_ops.attr,
	&dev_attr_netto_bytes.attr,
	&dev_attr_max_tx_io_time.attr,
	&dev_attr_tx_pending.attr,
	&dev_attr_tx_max_pending.attr,
	NULL,
};

static struct attribute_group netiucv_stat_attr_group = {
	.name  = "stats",
	.attrs = netiucv_stat_attrs,
};

static const struct attribute_group *netiucv_attr_groups[] = {
	&netiucv_stat_attr_group,
	&netiucv_attr_group,
	NULL,
};

static int netiucv_register_device(struct net_device *ndev)
{
	struct netiucv_priv *priv = netdev_priv(ndev);
	struct device *dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	int ret;

	IUCV_DBF_TEXT(trace, 3, __func__);

	if (dev) {
		dev_set_name(dev, "net%s", ndev->name);
		dev->bus = &iucv_bus;
		dev->parent = iucv_root;
		dev->groups = netiucv_attr_groups;
		/*
		 * The release function could be called after the
		 * module has been unloaded. It's _only_ task is to
		 * free the struct. Therefore, we specify kfree()
		 * directly here. (Probably a little bit obfuscating
		 * but legitime ...).
		 */
		dev->release = (void (*)(struct device *))kfree;
		dev->driver = &netiucv_driver;
	} else
		return -ENOMEM;

	ret = device_register(dev);
	if (ret) {
		put_device(dev);
		return ret;
	}
	priv->dev = dev;
	dev_set_drvdata(dev, priv);
	return 0;
}

static void netiucv_unregister_device(struct device *dev)
{
	IUCV_DBF_TEXT(trace, 3, __func__);
	device_unregister(dev);
}

/**
 * Allocate and initialize a new connection structure.
 * Add it to the list of netiucv connections;
 */
static struct iucv_connection *netiucv_new_connection(struct net_device *dev,
						      char *username,
						      char *userdata)
{
	struct iucv_connection *conn;

	conn = kzalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn)
		goto out;
	skb_queue_head_init(&conn->collect_queue);
	skb_queue_head_init(&conn->commit_queue);
	spin_lock_init(&conn->collect_lock);
	conn->max_buffsize = NETIUCV_BUFSIZE_DEFAULT;
	conn->netdev = dev;

	conn->rx_buff = alloc_skb(conn->max_buffsize, GFP_KERNEL | GFP_DMA);
	if (!conn->rx_buff)
		goto out_conn;
	conn->tx_buff = alloc_skb(conn->max_buffsize, GFP_KERNEL | GFP_DMA);
	if (!conn->tx_buff)
		goto out_rx;
	conn->fsm = init_fsm("netiucvconn", conn_state_names,
			     conn_event_names, NR_CONN_STATES,
			     NR_CONN_EVENTS, conn_fsm, CONN_FSM_LEN,
			     GFP_KERNEL);
	if (!conn->fsm)
		goto out_tx;

	fsm_settimer(conn->fsm, &conn->timer);
	fsm_newstate(conn->fsm, CONN_STATE_INVALID);

	if (userdata)
		memcpy(conn->userdata, userdata, 17);
	if (username) {
		memcpy(conn->userid, username, 9);
		fsm_newstate(conn->fsm, CONN_STATE_STOPPED);
	}

	write_lock_bh(&iucv_connection_rwlock);
	list_add_tail(&conn->list, &iucv_connection_list);
	write_unlock_bh(&iucv_connection_rwlock);
	return conn;

out_tx:
	kfree_skb(conn->tx_buff);
out_rx:
	kfree_skb(conn->rx_buff);
out_conn:
	kfree(conn);
out:
	return NULL;
}

/**
 * Release a connection structure and remove it from the
 * list of netiucv connections.
 */
static void netiucv_remove_connection(struct iucv_connection *conn)
{

	IUCV_DBF_TEXT(trace, 3, __func__);
	write_lock_bh(&iucv_connection_rwlock);
	list_del_init(&conn->list);
	write_unlock_bh(&iucv_connection_rwlock);
	fsm_deltimer(&conn->timer);
	netiucv_purge_skb_queue(&conn->collect_queue);
	if (conn->path) {
		iucv_path_sever(conn->path, conn->userdata);
		kfree(conn->path);
		conn->path = NULL;
	}
	netiucv_purge_skb_queue(&conn->commit_queue);
	kfree_fsm(conn->fsm);
	kfree_skb(conn->rx_buff);
	kfree_skb(conn->tx_buff);
}

/**
 * Release everything of a net device.
 */
static void netiucv_free_netdevice(struct net_device *dev)
{
	struct netiucv_priv *privptr = netdev_priv(dev);

	IUCV_DBF_TEXT(trace, 3, __func__);

	if (!dev)
		return;

	if (privptr) {
		if (privptr->conn)
			netiucv_remove_connection(privptr->conn);
		if (privptr->fsm)
			kfree_fsm(privptr->fsm);
		privptr->conn = NULL; privptr->fsm = NULL;
		/* privptr gets freed by free_netdev() */
	}
}

/**
 * Initialize a net device. (Called from kernel in alloc_netdev())
 */
static const struct net_device_ops netiucv_netdev_ops = {
	.ndo_open		= netiucv_open,
	.ndo_stop		= netiucv_close,
	.ndo_get_stats		= netiucv_stats,
	.ndo_start_xmit		= netiucv_tx,
};

static void netiucv_setup_netdevice(struct net_device *dev)
{
	dev->mtu	         = NETIUCV_MTU_DEFAULT;
	dev->min_mtu		 = 576;
	dev->max_mtu		 = NETIUCV_MTU_MAX;
	dev->needs_free_netdev   = true;
	dev->priv_destructor     = netiucv_free_netdevice;
	dev->hard_header_len     = NETIUCV_HDRLEN;
	dev->addr_len            = 0;
	dev->type                = ARPHRD_SLIP;
	dev->tx_queue_len        = NETIUCV_QUEUELEN_DEFAULT;
	dev->flags	         = IFF_POINTOPOINT | IFF_NOARP;
	dev->netdev_ops		 = &netiucv_netdev_ops;
}

/**
 * Allocate and initialize everything of a net device.
 */
static struct net_device *netiucv_init_netdevice(char *username, char *userdata)
{
	struct netiucv_priv *privptr;
	struct net_device *dev;

	dev = alloc_netdev(sizeof(struct netiucv_priv), "iucv%d",
			   NET_NAME_UNKNOWN, netiucv_setup_netdevice);
	if (!dev)
		return NULL;
	rtnl_lock();
	if (dev_alloc_name(dev, dev->name) < 0)
		goto out_netdev;

	privptr = netdev_priv(dev);
	privptr->fsm = init_fsm("netiucvdev", dev_state_names,
				dev_event_names, NR_DEV_STATES, NR_DEV_EVENTS,
				dev_fsm, DEV_FSM_LEN, GFP_KERNEL);
	if (!privptr->fsm)
		goto out_netdev;

	privptr->conn = netiucv_new_connection(dev, username, userdata);
	if (!privptr->conn) {
		IUCV_DBF_TEXT(setup, 2, "NULL from netiucv_new_connection\n");
		goto out_fsm;
	}
	fsm_newstate(privptr->fsm, DEV_STATE_STOPPED);
	return dev;

out_fsm:
	kfree_fsm(privptr->fsm);
out_netdev:
	rtnl_unlock();
	free_netdev(dev);
	return NULL;
}

static ssize_t conn_write(struct device_driver *drv,
			  const char *buf, size_t count)
{
	char username[9];
	char userdata[17];
	int rc;
	struct net_device *dev;
	struct netiucv_priv *priv;
	struct iucv_connection *cp;

	IUCV_DBF_TEXT(trace, 3, __func__);
	rc = netiucv_check_user(buf, count, username, userdata);
	if (rc)
		return rc;

	read_lock_bh(&iucv_connection_rwlock);
	list_for_each_entry(cp, &iucv_connection_list, list) {
		if (!strncmp(username, cp->userid, 9) &&
		    !strncmp(userdata, cp->userdata, 17)) {
			read_unlock_bh(&iucv_connection_rwlock);
			IUCV_DBF_TEXT_(setup, 2, "conn_write: Connection to %s "
				"already exists\n", netiucv_printuser(cp));
			return -EEXIST;
		}
	}
	read_unlock_bh(&iucv_connection_rwlock);

	dev = netiucv_init_netdevice(username, userdata);
	if (!dev) {
		IUCV_DBF_TEXT(setup, 2, "NULL from netiucv_init_netdevice\n");
		return -ENODEV;
	}

	rc = netiucv_register_device(dev);
	if (rc) {
		rtnl_unlock();
		IUCV_DBF_TEXT_(setup, 2,
			"ret %d from netiucv_register_device\n", rc);
		goto out_free_ndev;
	}

	/* sysfs magic */
	priv = netdev_priv(dev);
	SET_NETDEV_DEV(dev, priv->dev);

	rc = register_netdevice(dev);
	rtnl_unlock();
	if (rc)
		goto out_unreg;

	dev_info(priv->dev, "The IUCV interface to %s has been established "
			    "successfully\n",
		netiucv_printuser(priv->conn));

	return count;

out_unreg:
	netiucv_unregister_device(priv->dev);
out_free_ndev:
	netiucv_free_netdevice(dev);
	return rc;
}

static DRIVER_ATTR(connection, 0200, NULL, conn_write);

static ssize_t remove_write (struct device_driver *drv,
			     const char *buf, size_t count)
{
	struct iucv_connection *cp;
        struct net_device *ndev;
        struct netiucv_priv *priv;
        struct device *dev;
        char name[IFNAMSIZ];
	const char *p;
        int i;

	IUCV_DBF_TEXT(trace, 3, __func__);

        if (count >= IFNAMSIZ)
                count = IFNAMSIZ - 1;

	for (i = 0, p = buf; i < count && *p; i++, p++) {
		if (*p == '\n' || *p == ' ')
                        /* trailing lf, grr */
                        break;
		name[i] = *p;
        }
        name[i] = '\0';

	read_lock_bh(&iucv_connection_rwlock);
	list_for_each_entry(cp, &iucv_connection_list, list) {
		ndev = cp->netdev;
		priv = netdev_priv(ndev);
                dev = priv->dev;
		if (strncmp(name, ndev->name, count))
			continue;
		read_unlock_bh(&iucv_connection_rwlock);
                if (ndev->flags & (IFF_UP | IFF_RUNNING)) {
			dev_warn(dev, "The IUCV device is connected"
				" to %s and cannot be removed\n",
				priv->conn->userid);
			IUCV_DBF_TEXT(data, 2, "remove_write: still active\n");
			return -EPERM;
                }
                unregister_netdev(ndev);
                netiucv_unregister_device(dev);
                return count;
        }
	read_unlock_bh(&iucv_connection_rwlock);
	IUCV_DBF_TEXT(data, 2, "remove_write: unknown device\n");
        return -EINVAL;
}

static DRIVER_ATTR(remove, 0200, NULL, remove_write);

static struct attribute * netiucv_drv_attrs[] = {
	&driver_attr_connection.attr,
	&driver_attr_remove.attr,
	NULL,
};

static struct attribute_group netiucv_drv_attr_group = {
	.attrs = netiucv_drv_attrs,
};

static const struct attribute_group *netiucv_drv_attr_groups[] = {
	&netiucv_drv_attr_group,
	NULL,
};

static void netiucv_banner(void)
{
	pr_info("driver initialized\n");
}

static void __exit netiucv_exit(void)
{
	struct iucv_connection *cp;
	struct net_device *ndev;
	struct netiucv_priv *priv;
	struct device *dev;

	IUCV_DBF_TEXT(trace, 3, __func__);
	while (!list_empty(&iucv_connection_list)) {
		cp = list_entry(iucv_connection_list.next,
				struct iucv_connection, list);
		ndev = cp->netdev;
		priv = netdev_priv(ndev);
		dev = priv->dev;

		unregister_netdev(ndev);
		netiucv_unregister_device(dev);
	}

	device_unregister(netiucv_dev);
	driver_unregister(&netiucv_driver);
	iucv_unregister(&netiucv_handler, 1);
	iucv_unregister_dbf_views();

	pr_info("driver unloaded\n");
	return;
}

static int __init netiucv_init(void)
{
	int rc;

	rc = iucv_register_dbf_views();
	if (rc)
		goto out;
	rc = iucv_register(&netiucv_handler, 1);
	if (rc)
		goto out_dbf;
	IUCV_DBF_TEXT(trace, 3, __func__);
	netiucv_driver.groups = netiucv_drv_attr_groups;
	rc = driver_register(&netiucv_driver);
	if (rc) {
		IUCV_DBF_TEXT_(setup, 2, "ret %d from driver_register\n", rc);
		goto out_iucv;
	}
	/* establish dummy device */
	netiucv_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!netiucv_dev) {
		rc = -ENOMEM;
		goto out_driver;
	}
	dev_set_name(netiucv_dev, "netiucv");
	netiucv_dev->bus = &iucv_bus;
	netiucv_dev->parent = iucv_root;
	netiucv_dev->release = (void (*)(struct device *))kfree;
	netiucv_dev->driver = &netiucv_driver;
	rc = device_register(netiucv_dev);
	if (rc) {
		put_device(netiucv_dev);
		goto out_driver;
	}
	netiucv_banner();
	return rc;

out_driver:
	driver_unregister(&netiucv_driver);
out_iucv:
	iucv_unregister(&netiucv_handler, 1);
out_dbf:
	iucv_unregister_dbf_views();
out:
	return rc;
}

module_init(netiucv_init);
module_exit(netiucv_exit);
MODULE_LICENSE("GPL");
