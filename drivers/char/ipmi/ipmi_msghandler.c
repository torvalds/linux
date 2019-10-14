// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_msghandler.c
 *
 * Incoming and outgoing message routing for an IPMI interface.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/uuid.h>
#include <linux/nospec.h>

#define PFX "IPMI message handler: "

#define IPMI_DRIVER_VERSION "39.2"

static struct ipmi_recv_msg *ipmi_alloc_recv_msg(void);
static int ipmi_init_msghandler(void);
static void smi_recv_tasklet(unsigned long);
static void handle_new_recv_msgs(struct ipmi_smi *intf);
static void need_waiter(struct ipmi_smi *intf);
static int handle_one_recv_msg(struct ipmi_smi *intf,
			       struct ipmi_smi_msg *msg);

#ifdef DEBUG
static void ipmi_debug_msg(const char *title, unsigned char *data,
			   unsigned int len)
{
	int i, pos;
	char buf[100];

	pos = snprintf(buf, sizeof(buf), "%s: ", title);
	for (i = 0; i < len; i++)
		pos += snprintf(buf + pos, sizeof(buf) - pos,
				" %2.2x", data[i]);
	pr_debug("%s\n", buf);
}
#else
static void ipmi_debug_msg(const char *title, unsigned char *data,
			   unsigned int len)
{ }
#endif

static bool initialized;
static bool drvregistered;

enum ipmi_panic_event_op {
	IPMI_SEND_PANIC_EVENT_NONE,
	IPMI_SEND_PANIC_EVENT,
	IPMI_SEND_PANIC_EVENT_STRING
};
#ifdef CONFIG_IPMI_PANIC_STRING
#define IPMI_PANIC_DEFAULT IPMI_SEND_PANIC_EVENT_STRING
#elif defined(CONFIG_IPMI_PANIC_EVENT)
#define IPMI_PANIC_DEFAULT IPMI_SEND_PANIC_EVENT
#else
#define IPMI_PANIC_DEFAULT IPMI_SEND_PANIC_EVENT_NONE
#endif
static enum ipmi_panic_event_op ipmi_send_panic_event = IPMI_PANIC_DEFAULT;

static int panic_op_write_handler(const char *val,
				  const struct kernel_param *kp)
{
	char valcp[16];
	char *s;

	strncpy(valcp, val, 15);
	valcp[15] = '\0';

	s = strstrip(valcp);

	if (strcmp(s, "none") == 0)
		ipmi_send_panic_event = IPMI_SEND_PANIC_EVENT_NONE;
	else if (strcmp(s, "event") == 0)
		ipmi_send_panic_event = IPMI_SEND_PANIC_EVENT;
	else if (strcmp(s, "string") == 0)
		ipmi_send_panic_event = IPMI_SEND_PANIC_EVENT_STRING;
	else
		return -EINVAL;

	return 0;
}

static int panic_op_read_handler(char *buffer, const struct kernel_param *kp)
{
	switch (ipmi_send_panic_event) {
	case IPMI_SEND_PANIC_EVENT_NONE:
		strcpy(buffer, "none");
		break;

	case IPMI_SEND_PANIC_EVENT:
		strcpy(buffer, "event");
		break;

	case IPMI_SEND_PANIC_EVENT_STRING:
		strcpy(buffer, "string");
		break;

	default:
		strcpy(buffer, "???");
		break;
	}

	return strlen(buffer);
}

static const struct kernel_param_ops panic_op_ops = {
	.set = panic_op_write_handler,
	.get = panic_op_read_handler
};
module_param_cb(panic_op, &panic_op_ops, NULL, 0600);
MODULE_PARM_DESC(panic_op, "Sets if the IPMI driver will attempt to store panic information in the event log in the event of a panic.  Set to 'none' for no, 'event' for a single event, or 'string' for a generic event and the panic string in IPMI OEM events.");


#define MAX_EVENTS_IN_QUEUE	25

/* Remain in auto-maintenance mode for this amount of time (in ms). */
static unsigned long maintenance_mode_timeout_ms = 30000;
module_param(maintenance_mode_timeout_ms, ulong, 0644);
MODULE_PARM_DESC(maintenance_mode_timeout_ms,
		 "The time (milliseconds) after the last maintenance message that the connection stays in maintenance mode.");

/*
 * Don't let a message sit in a queue forever, always time it with at lest
 * the max message timer.  This is in milliseconds.
 */
#define MAX_MSG_TIMEOUT		60000

/*
 * Timeout times below are in milliseconds, and are done off a 1
 * second timer.  So setting the value to 1000 would mean anything
 * between 0 and 1000ms.  So really the only reasonable minimum
 * setting it 2000ms, which is between 1 and 2 seconds.
 */

/* The default timeout for message retries. */
static unsigned long default_retry_ms = 2000;
module_param(default_retry_ms, ulong, 0644);
MODULE_PARM_DESC(default_retry_ms,
		 "The time (milliseconds) between retry sends");

/* The default timeout for maintenance mode message retries. */
static unsigned long default_maintenance_retry_ms = 3000;
module_param(default_maintenance_retry_ms, ulong, 0644);
MODULE_PARM_DESC(default_maintenance_retry_ms,
		 "The time (milliseconds) between retry sends in maintenance mode");

/* The default maximum number of retries */
static unsigned int default_max_retries = 4;
module_param(default_max_retries, uint, 0644);
MODULE_PARM_DESC(default_max_retries,
		 "The time (milliseconds) between retry sends in maintenance mode");

/* Call every ~1000 ms. */
#define IPMI_TIMEOUT_TIME	1000

/* How many jiffies does it take to get to the timeout time. */
#define IPMI_TIMEOUT_JIFFIES	((IPMI_TIMEOUT_TIME * HZ) / 1000)

/*
 * Request events from the queue every second (this is the number of
 * IPMI_TIMEOUT_TIMES between event requests).  Hopefully, in the
 * future, IPMI will add a way to know immediately if an event is in
 * the queue and this silliness can go away.
 */
#define IPMI_REQUEST_EV_TIME	(1000 / (IPMI_TIMEOUT_TIME))

/* How long should we cache dynamic device IDs? */
#define IPMI_DYN_DEV_ID_EXPIRY	(10 * HZ)

/*
 * The main "user" data structure.
 */
struct ipmi_user {
	struct list_head link;

	/*
	 * Set to NULL when the user is destroyed, a pointer to myself
	 * so srcu_dereference can be used on it.
	 */
	struct ipmi_user *self;
	struct srcu_struct release_barrier;

	struct kref refcount;

	/* The upper layer that handles receive messages. */
	const struct ipmi_user_hndl *handler;
	void             *handler_data;

	/* The interface this user is bound to. */
	struct ipmi_smi *intf;

	/* Does this interface receive IPMI events? */
	bool gets_events;

	/* Free must run in process context for RCU cleanup. */
	struct work_struct remove_work;
};

static struct ipmi_user *acquire_ipmi_user(struct ipmi_user *user, int *index)
	__acquires(user->release_barrier)
{
	struct ipmi_user *ruser;

	*index = srcu_read_lock(&user->release_barrier);
	ruser = srcu_dereference(user->self, &user->release_barrier);
	if (!ruser)
		srcu_read_unlock(&user->release_barrier, *index);
	return ruser;
}

static void release_ipmi_user(struct ipmi_user *user, int index)
{
	srcu_read_unlock(&user->release_barrier, index);
}

struct cmd_rcvr {
	struct list_head link;

	struct ipmi_user *user;
	unsigned char netfn;
	unsigned char cmd;
	unsigned int  chans;

	/*
	 * This is used to form a linked lised during mass deletion.
	 * Since this is in an RCU list, we cannot use the link above
	 * or change any data until the RCU period completes.  So we
	 * use this next variable during mass deletion so we can have
	 * a list and don't have to wait and restart the search on
	 * every individual deletion of a command.
	 */
	struct cmd_rcvr *next;
};

struct seq_table {
	unsigned int         inuse : 1;
	unsigned int         broadcast : 1;

	unsigned long        timeout;
	unsigned long        orig_timeout;
	unsigned int         retries_left;

	/*
	 * To verify on an incoming send message response that this is
	 * the message that the response is for, we keep a sequence id
	 * and increment it every time we send a message.
	 */
	long                 seqid;

	/*
	 * This is held so we can properly respond to the message on a
	 * timeout, and it is used to hold the temporary data for
	 * retransmission, too.
	 */
	struct ipmi_recv_msg *recv_msg;
};

/*
 * Store the information in a msgid (long) to allow us to find a
 * sequence table entry from the msgid.
 */
#define STORE_SEQ_IN_MSGID(seq, seqid) \
	((((seq) & 0x3f) << 26) | ((seqid) & 0x3ffffff))

#define GET_SEQ_FROM_MSGID(msgid, seq, seqid) \
	do {								\
		seq = (((msgid) >> 26) & 0x3f);				\
		seqid = ((msgid) & 0x3ffffff);				\
	} while (0)

#define NEXT_SEQID(seqid) (((seqid) + 1) & 0x3ffffff)

#define IPMI_MAX_CHANNELS       16
struct ipmi_channel {
	unsigned char medium;
	unsigned char protocol;
};

struct ipmi_channel_set {
	struct ipmi_channel c[IPMI_MAX_CHANNELS];
};

struct ipmi_my_addrinfo {
	/*
	 * My slave address.  This is initialized to IPMI_BMC_SLAVE_ADDR,
	 * but may be changed by the user.
	 */
	unsigned char address;

	/*
	 * My LUN.  This should generally stay the SMS LUN, but just in
	 * case...
	 */
	unsigned char lun;
};

/*
 * Note that the product id, manufacturer id, guid, and device id are
 * immutable in this structure, so dyn_mutex is not required for
 * accessing those.  If those change on a BMC, a new BMC is allocated.
 */
struct bmc_device {
	struct platform_device pdev;
	struct list_head       intfs; /* Interfaces on this BMC. */
	struct ipmi_device_id  id;
	struct ipmi_device_id  fetch_id;
	int                    dyn_id_set;
	unsigned long          dyn_id_expiry;
	struct mutex           dyn_mutex; /* Protects id, intfs, & dyn* */
	guid_t                 guid;
	guid_t                 fetch_guid;
	int                    dyn_guid_set;
	struct kref	       usecount;
	struct work_struct     remove_work;
};
#define to_bmc_device(x) container_of((x), struct bmc_device, pdev.dev)

static int bmc_get_device_id(struct ipmi_smi *intf, struct bmc_device *bmc,
			     struct ipmi_device_id *id,
			     bool *guid_set, guid_t *guid);

/*
 * Various statistics for IPMI, these index stats[] in the ipmi_smi
 * structure.
 */
enum ipmi_stat_indexes {
	/* Commands we got from the user that were invalid. */
	IPMI_STAT_sent_invalid_commands = 0,

	/* Commands we sent to the MC. */
	IPMI_STAT_sent_local_commands,

	/* Responses from the MC that were delivered to a user. */
	IPMI_STAT_handled_local_responses,

	/* Responses from the MC that were not delivered to a user. */
	IPMI_STAT_unhandled_local_responses,

	/* Commands we sent out to the IPMB bus. */
	IPMI_STAT_sent_ipmb_commands,

	/* Commands sent on the IPMB that had errors on the SEND CMD */
	IPMI_STAT_sent_ipmb_command_errs,

	/* Each retransmit increments this count. */
	IPMI_STAT_retransmitted_ipmb_commands,

	/*
	 * When a message times out (runs out of retransmits) this is
	 * incremented.
	 */
	IPMI_STAT_timed_out_ipmb_commands,

	/*
	 * This is like above, but for broadcasts.  Broadcasts are
	 * *not* included in the above count (they are expected to
	 * time out).
	 */
	IPMI_STAT_timed_out_ipmb_broadcasts,

	/* Responses I have sent to the IPMB bus. */
	IPMI_STAT_sent_ipmb_responses,

	/* The response was delivered to the user. */
	IPMI_STAT_handled_ipmb_responses,

	/* The response had invalid data in it. */
	IPMI_STAT_invalid_ipmb_responses,

	/* The response didn't have anyone waiting for it. */
	IPMI_STAT_unhandled_ipmb_responses,

	/* Commands we sent out to the IPMB bus. */
	IPMI_STAT_sent_lan_commands,

	/* Commands sent on the IPMB that had errors on the SEND CMD */
	IPMI_STAT_sent_lan_command_errs,

	/* Each retransmit increments this count. */
	IPMI_STAT_retransmitted_lan_commands,

	/*
	 * When a message times out (runs out of retransmits) this is
	 * incremented.
	 */
	IPMI_STAT_timed_out_lan_commands,

	/* Responses I have sent to the IPMB bus. */
	IPMI_STAT_sent_lan_responses,

	/* The response was delivered to the user. */
	IPMI_STAT_handled_lan_responses,

	/* The response had invalid data in it. */
	IPMI_STAT_invalid_lan_responses,

	/* The response didn't have anyone waiting for it. */
	IPMI_STAT_unhandled_lan_responses,

	/* The command was delivered to the user. */
	IPMI_STAT_handled_commands,

	/* The command had invalid data in it. */
	IPMI_STAT_invalid_commands,

	/* The command didn't have anyone waiting for it. */
	IPMI_STAT_unhandled_commands,

	/* Invalid data in an event. */
	IPMI_STAT_invalid_events,

	/* Events that were received with the proper format. */
	IPMI_STAT_events,

	/* Retransmissions on IPMB that failed. */
	IPMI_STAT_dropped_rexmit_ipmb_commands,

	/* Retransmissions on LAN that failed. */
	IPMI_STAT_dropped_rexmit_lan_commands,

	/* This *must* remain last, add new values above this. */
	IPMI_NUM_STATS
};


#define IPMI_IPMB_NUM_SEQ	64
struct ipmi_smi {
	struct module *owner;

	/* What interface number are we? */
	int intf_num;

	struct kref refcount;

	/* Set when the interface is being unregistered. */
	bool in_shutdown;

	/* Used for a list of interfaces. */
	struct list_head link;

	/*
	 * The list of upper layers that are using me.  seq_lock write
	 * protects this.  Read protection is with srcu.
	 */
	struct list_head users;
	struct srcu_struct users_srcu;

	/* Used for wake ups at startup. */
	wait_queue_head_t waitq;

	/*
	 * Prevents the interface from being unregistered when the
	 * interface is used by being looked up through the BMC
	 * structure.
	 */
	struct mutex bmc_reg_mutex;

	struct bmc_device tmp_bmc;
	struct bmc_device *bmc;
	bool bmc_registered;
	struct list_head bmc_link;
	char *my_dev_name;
	bool in_bmc_register;  /* Handle recursive situations.  Yuck. */
	struct work_struct bmc_reg_work;

	const struct ipmi_smi_handlers *handlers;
	void                     *send_info;

	/* Driver-model device for the system interface. */
	struct device          *si_dev;

	/*
	 * A table of sequence numbers for this interface.  We use the
	 * sequence numbers for IPMB messages that go out of the
	 * interface to match them up with their responses.  A routine
	 * is called periodically to time the items in this list.
	 */
	spinlock_t       seq_lock;
	struct seq_table seq_table[IPMI_IPMB_NUM_SEQ];
	int curr_seq;

	/*
	 * Messages queued for delivery.  If delivery fails (out of memory
	 * for instance), They will stay in here to be processed later in a
	 * periodic timer interrupt.  The tasklet is for handling received
	 * messages directly from the handler.
	 */
	spinlock_t       waiting_rcv_msgs_lock;
	struct list_head waiting_rcv_msgs;
	atomic_t	 watchdog_pretimeouts_to_deliver;
	struct tasklet_struct recv_tasklet;

	spinlock_t             xmit_msgs_lock;
	struct list_head       xmit_msgs;
	struct ipmi_smi_msg    *curr_msg;
	struct list_head       hp_xmit_msgs;

	/*
	 * The list of command receivers that are registered for commands
	 * on this interface.
	 */
	struct mutex     cmd_rcvrs_mutex;
	struct list_head cmd_rcvrs;

	/*
	 * Events that were queues because no one was there to receive
	 * them.
	 */
	spinlock_t       events_lock; /* For dealing with event stuff. */
	struct list_head waiting_events;
	unsigned int     waiting_events_count; /* How many events in queue? */
	char             delivering_events;
	char             event_msg_printed;
	atomic_t         event_waiters;
	unsigned int     ticks_to_req_ev;
	int              last_needs_timer;

	/*
	 * The event receiver for my BMC, only really used at panic
	 * shutdown as a place to store this.
	 */
	unsigned char event_receiver;
	unsigned char event_receiver_lun;
	unsigned char local_sel_device;
	unsigned char local_event_generator;

	/* For handling of maintenance mode. */
	int maintenance_mode;
	bool maintenance_mode_enable;
	int auto_maintenance_timeout;
	spinlock_t maintenance_mode_lock; /* Used in a timer... */

	/*
	 * If we are doing maintenance on something on IPMB, extend
	 * the timeout time to avoid timeouts writing firmware and
	 * such.
	 */
	int ipmb_maintenance_mode_timeout;

	/*
	 * A cheap hack, if this is non-null and a message to an
	 * interface comes in with a NULL user, call this routine with
	 * it.  Note that the message will still be freed by the
	 * caller.  This only works on the system interface.
	 *
	 * Protected by bmc_reg_mutex.
	 */
	void (*null_user_handler)(struct ipmi_smi *intf,
				  struct ipmi_recv_msg *msg);

	/*
	 * When we are scanning the channels for an SMI, this will
	 * tell which channel we are scanning.
	 */
	int curr_channel;

	/* Channel information */
	struct ipmi_channel_set *channel_list;
	unsigned int curr_working_cset; /* First index into the following. */
	struct ipmi_channel_set wchannels[2];
	struct ipmi_my_addrinfo addrinfo[IPMI_MAX_CHANNELS];
	bool channels_ready;

	atomic_t stats[IPMI_NUM_STATS];

	/*
	 * run_to_completion duplicate of smb_info, smi_info
	 * and ipmi_serial_info structures. Used to decrease numbers of
	 * parameters passed by "low" level IPMI code.
	 */
	int run_to_completion;
};
#define to_si_intf_from_dev(device) container_of(device, struct ipmi_smi, dev)

static void __get_guid(struct ipmi_smi *intf);
static void __ipmi_bmc_unregister(struct ipmi_smi *intf);
static int __ipmi_bmc_register(struct ipmi_smi *intf,
			       struct ipmi_device_id *id,
			       bool guid_set, guid_t *guid, int intf_num);
static int __scan_channels(struct ipmi_smi *intf, struct ipmi_device_id *id);


/**
 * The driver model view of the IPMI messaging driver.
 */
static struct platform_driver ipmidriver = {
	.driver = {
		.name = "ipmi",
		.bus = &platform_bus_type
	}
};
/*
 * This mutex keeps us from adding the same BMC twice.
 */
static DEFINE_MUTEX(ipmidriver_mutex);

static LIST_HEAD(ipmi_interfaces);
static DEFINE_MUTEX(ipmi_interfaces_mutex);
struct srcu_struct ipmi_interfaces_srcu;

/*
 * List of watchers that want to know when smi's are added and deleted.
 */
static LIST_HEAD(smi_watchers);
static DEFINE_MUTEX(smi_watchers_mutex);

#define ipmi_inc_stat(intf, stat) \
	atomic_inc(&(intf)->stats[IPMI_STAT_ ## stat])
#define ipmi_get_stat(intf, stat) \
	((unsigned int) atomic_read(&(intf)->stats[IPMI_STAT_ ## stat]))

static const char * const addr_src_to_str[] = {
	"invalid", "hotmod", "hardcoded", "SPMI", "ACPI", "SMBIOS", "PCI",
	"device-tree", "platform"
};

const char *ipmi_addr_src_to_str(enum ipmi_addr_src src)
{
	if (src >= SI_LAST)
		src = 0; /* Invalid */
	return addr_src_to_str[src];
}
EXPORT_SYMBOL(ipmi_addr_src_to_str);

static int is_lan_addr(struct ipmi_addr *addr)
{
	return addr->addr_type == IPMI_LAN_ADDR_TYPE;
}

static int is_ipmb_addr(struct ipmi_addr *addr)
{
	return addr->addr_type == IPMI_IPMB_ADDR_TYPE;
}

static int is_ipmb_bcast_addr(struct ipmi_addr *addr)
{
	return addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE;
}

static void free_recv_msg_list(struct list_head *q)
{
	struct ipmi_recv_msg *msg, *msg2;

	list_for_each_entry_safe(msg, msg2, q, link) {
		list_del(&msg->link);
		ipmi_free_recv_msg(msg);
	}
}

static void free_smi_msg_list(struct list_head *q)
{
	struct ipmi_smi_msg *msg, *msg2;

	list_for_each_entry_safe(msg, msg2, q, link) {
		list_del(&msg->link);
		ipmi_free_smi_msg(msg);
	}
}

static void clean_up_interface_data(struct ipmi_smi *intf)
{
	int              i;
	struct cmd_rcvr  *rcvr, *rcvr2;
	struct list_head list;

	tasklet_kill(&intf->recv_tasklet);

	free_smi_msg_list(&intf->waiting_rcv_msgs);
	free_recv_msg_list(&intf->waiting_events);

	/*
	 * Wholesale remove all the entries from the list in the
	 * interface and wait for RCU to know that none are in use.
	 */
	mutex_lock(&intf->cmd_rcvrs_mutex);
	INIT_LIST_HEAD(&list);
	list_splice_init_rcu(&intf->cmd_rcvrs, &list, synchronize_rcu);
	mutex_unlock(&intf->cmd_rcvrs_mutex);

	list_for_each_entry_safe(rcvr, rcvr2, &list, link)
		kfree(rcvr);

	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++) {
		if ((intf->seq_table[i].inuse)
					&& (intf->seq_table[i].recv_msg))
			ipmi_free_recv_msg(intf->seq_table[i].recv_msg);
	}
}

static void intf_free(struct kref *ref)
{
	struct ipmi_smi *intf = container_of(ref, struct ipmi_smi, refcount);

	clean_up_interface_data(intf);
	kfree(intf);
}

struct watcher_entry {
	int              intf_num;
	struct ipmi_smi  *intf;
	struct list_head link;
};

int ipmi_smi_watcher_register(struct ipmi_smi_watcher *watcher)
{
	struct ipmi_smi *intf;
	int index, rv;

	/*
	 * Make sure the driver is actually initialized, this handles
	 * problems with initialization order.
	 */
	rv = ipmi_init_msghandler();
	if (rv)
		return rv;

	mutex_lock(&smi_watchers_mutex);

	list_add(&watcher->link, &smi_watchers);

	index = srcu_read_lock(&ipmi_interfaces_srcu);
	list_for_each_entry_rcu(intf, &ipmi_interfaces, link) {
		int intf_num = READ_ONCE(intf->intf_num);

		if (intf_num == -1)
			continue;
		watcher->new_smi(intf_num, intf->si_dev);
	}
	srcu_read_unlock(&ipmi_interfaces_srcu, index);

	mutex_unlock(&smi_watchers_mutex);

	return 0;
}
EXPORT_SYMBOL(ipmi_smi_watcher_register);

int ipmi_smi_watcher_unregister(struct ipmi_smi_watcher *watcher)
{
	mutex_lock(&smi_watchers_mutex);
	list_del(&watcher->link);
	mutex_unlock(&smi_watchers_mutex);
	return 0;
}
EXPORT_SYMBOL(ipmi_smi_watcher_unregister);

/*
 * Must be called with smi_watchers_mutex held.
 */
static void
call_smi_watchers(int i, struct device *dev)
{
	struct ipmi_smi_watcher *w;

	mutex_lock(&smi_watchers_mutex);
	list_for_each_entry(w, &smi_watchers, link) {
		if (try_module_get(w->owner)) {
			w->new_smi(i, dev);
			module_put(w->owner);
		}
	}
	mutex_unlock(&smi_watchers_mutex);
}

static int
ipmi_addr_equal(struct ipmi_addr *addr1, struct ipmi_addr *addr2)
{
	if (addr1->addr_type != addr2->addr_type)
		return 0;

	if (addr1->channel != addr2->channel)
		return 0;

	if (addr1->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		struct ipmi_system_interface_addr *smi_addr1
		    = (struct ipmi_system_interface_addr *) addr1;
		struct ipmi_system_interface_addr *smi_addr2
		    = (struct ipmi_system_interface_addr *) addr2;
		return (smi_addr1->lun == smi_addr2->lun);
	}

	if (is_ipmb_addr(addr1) || is_ipmb_bcast_addr(addr1)) {
		struct ipmi_ipmb_addr *ipmb_addr1
		    = (struct ipmi_ipmb_addr *) addr1;
		struct ipmi_ipmb_addr *ipmb_addr2
		    = (struct ipmi_ipmb_addr *) addr2;

		return ((ipmb_addr1->slave_addr == ipmb_addr2->slave_addr)
			&& (ipmb_addr1->lun == ipmb_addr2->lun));
	}

	if (is_lan_addr(addr1)) {
		struct ipmi_lan_addr *lan_addr1
			= (struct ipmi_lan_addr *) addr1;
		struct ipmi_lan_addr *lan_addr2
		    = (struct ipmi_lan_addr *) addr2;

		return ((lan_addr1->remote_SWID == lan_addr2->remote_SWID)
			&& (lan_addr1->local_SWID == lan_addr2->local_SWID)
			&& (lan_addr1->session_handle
			    == lan_addr2->session_handle)
			&& (lan_addr1->lun == lan_addr2->lun));
	}

	return 1;
}

int ipmi_validate_addr(struct ipmi_addr *addr, int len)
{
	if (len < sizeof(struct ipmi_system_interface_addr))
		return -EINVAL;

	if (addr->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		if (addr->channel != IPMI_BMC_CHANNEL)
			return -EINVAL;
		return 0;
	}

	if ((addr->channel == IPMI_BMC_CHANNEL)
	    || (addr->channel >= IPMI_MAX_CHANNELS)
	    || (addr->channel < 0))
		return -EINVAL;

	if (is_ipmb_addr(addr) || is_ipmb_bcast_addr(addr)) {
		if (len < sizeof(struct ipmi_ipmb_addr))
			return -EINVAL;
		return 0;
	}

	if (is_lan_addr(addr)) {
		if (len < sizeof(struct ipmi_lan_addr))
			return -EINVAL;
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(ipmi_validate_addr);

unsigned int ipmi_addr_length(int addr_type)
{
	if (addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
		return sizeof(struct ipmi_system_interface_addr);

	if ((addr_type == IPMI_IPMB_ADDR_TYPE)
			|| (addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE))
		return sizeof(struct ipmi_ipmb_addr);

	if (addr_type == IPMI_LAN_ADDR_TYPE)
		return sizeof(struct ipmi_lan_addr);

	return 0;
}
EXPORT_SYMBOL(ipmi_addr_length);

static int deliver_response(struct ipmi_smi *intf, struct ipmi_recv_msg *msg)
{
	int rv = 0;

	if (!msg->user) {
		/* Special handling for NULL users. */
		if (intf->null_user_handler) {
			intf->null_user_handler(intf, msg);
		} else {
			/* No handler, so give up. */
			rv = -EINVAL;
		}
		ipmi_free_recv_msg(msg);
	} else if (!oops_in_progress) {
		/*
		 * If we are running in the panic context, calling the
		 * receive handler doesn't much meaning and has a deadlock
		 * risk.  At this moment, simply skip it in that case.
		 */
		int index;
		struct ipmi_user *user = acquire_ipmi_user(msg->user, &index);

		if (user) {
			user->handler->ipmi_recv_hndl(msg, user->handler_data);
			release_ipmi_user(user, index);
		} else {
			/* User went away, give up. */
			ipmi_free_recv_msg(msg);
			rv = -EINVAL;
		}
	}

	return rv;
}

static void deliver_local_response(struct ipmi_smi *intf,
				   struct ipmi_recv_msg *msg)
{
	if (deliver_response(intf, msg))
		ipmi_inc_stat(intf, unhandled_local_responses);
	else
		ipmi_inc_stat(intf, handled_local_responses);
}

static void deliver_err_response(struct ipmi_smi *intf,
				 struct ipmi_recv_msg *msg, int err)
{
	msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	msg->msg_data[0] = err;
	msg->msg.netfn |= 1; /* Convert to a response. */
	msg->msg.data_len = 1;
	msg->msg.data = msg->msg_data;
	deliver_local_response(intf, msg);
}

/*
 * Find the next sequence number not being used and add the given
 * message with the given timeout to the sequence table.  This must be
 * called with the interface's seq_lock held.
 */
static int intf_next_seq(struct ipmi_smi      *intf,
			 struct ipmi_recv_msg *recv_msg,
			 unsigned long        timeout,
			 int                  retries,
			 int                  broadcast,
			 unsigned char        *seq,
			 long                 *seqid)
{
	int          rv = 0;
	unsigned int i;

	if (timeout == 0)
		timeout = default_retry_ms;
	if (retries < 0)
		retries = default_max_retries;

	for (i = intf->curr_seq; (i+1)%IPMI_IPMB_NUM_SEQ != intf->curr_seq;
					i = (i+1)%IPMI_IPMB_NUM_SEQ) {
		if (!intf->seq_table[i].inuse)
			break;
	}

	if (!intf->seq_table[i].inuse) {
		intf->seq_table[i].recv_msg = recv_msg;

		/*
		 * Start with the maximum timeout, when the send response
		 * comes in we will start the real timer.
		 */
		intf->seq_table[i].timeout = MAX_MSG_TIMEOUT;
		intf->seq_table[i].orig_timeout = timeout;
		intf->seq_table[i].retries_left = retries;
		intf->seq_table[i].broadcast = broadcast;
		intf->seq_table[i].inuse = 1;
		intf->seq_table[i].seqid = NEXT_SEQID(intf->seq_table[i].seqid);
		*seq = i;
		*seqid = intf->seq_table[i].seqid;
		intf->curr_seq = (i+1)%IPMI_IPMB_NUM_SEQ;
		need_waiter(intf);
	} else {
		rv = -EAGAIN;
	}

	return rv;
}

/*
 * Return the receive message for the given sequence number and
 * release the sequence number so it can be reused.  Some other data
 * is passed in to be sure the message matches up correctly (to help
 * guard against message coming in after their timeout and the
 * sequence number being reused).
 */
static int intf_find_seq(struct ipmi_smi      *intf,
			 unsigned char        seq,
			 short                channel,
			 unsigned char        cmd,
			 unsigned char        netfn,
			 struct ipmi_addr     *addr,
			 struct ipmi_recv_msg **recv_msg)
{
	int           rv = -ENODEV;
	unsigned long flags;

	if (seq >= IPMI_IPMB_NUM_SEQ)
		return -EINVAL;

	spin_lock_irqsave(&intf->seq_lock, flags);
	if (intf->seq_table[seq].inuse) {
		struct ipmi_recv_msg *msg = intf->seq_table[seq].recv_msg;

		if ((msg->addr.channel == channel) && (msg->msg.cmd == cmd)
				&& (msg->msg.netfn == netfn)
				&& (ipmi_addr_equal(addr, &msg->addr))) {
			*recv_msg = msg;
			intf->seq_table[seq].inuse = 0;
			rv = 0;
		}
	}
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	return rv;
}


/* Start the timer for a specific sequence table entry. */
static int intf_start_seq_timer(struct ipmi_smi *intf,
				long       msgid)
{
	int           rv = -ENODEV;
	unsigned long flags;
	unsigned char seq;
	unsigned long seqid;


	GET_SEQ_FROM_MSGID(msgid, seq, seqid);

	spin_lock_irqsave(&intf->seq_lock, flags);
	/*
	 * We do this verification because the user can be deleted
	 * while a message is outstanding.
	 */
	if ((intf->seq_table[seq].inuse)
				&& (intf->seq_table[seq].seqid == seqid)) {
		struct seq_table *ent = &intf->seq_table[seq];
		ent->timeout = ent->orig_timeout;
		rv = 0;
	}
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	return rv;
}

/* Got an error for the send message for a specific sequence number. */
static int intf_err_seq(struct ipmi_smi *intf,
			long         msgid,
			unsigned int err)
{
	int                  rv = -ENODEV;
	unsigned long        flags;
	unsigned char        seq;
	unsigned long        seqid;
	struct ipmi_recv_msg *msg = NULL;


	GET_SEQ_FROM_MSGID(msgid, seq, seqid);

	spin_lock_irqsave(&intf->seq_lock, flags);
	/*
	 * We do this verification because the user can be deleted
	 * while a message is outstanding.
	 */
	if ((intf->seq_table[seq].inuse)
				&& (intf->seq_table[seq].seqid == seqid)) {
		struct seq_table *ent = &intf->seq_table[seq];

		ent->inuse = 0;
		msg = ent->recv_msg;
		rv = 0;
	}
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	if (msg)
		deliver_err_response(intf, msg, err);

	return rv;
}


static void free_user_work(struct work_struct *work)
{
	struct ipmi_user *user = container_of(work, struct ipmi_user,
					      remove_work);

	cleanup_srcu_struct(&user->release_barrier);
	kfree(user);
}

int ipmi_create_user(unsigned int          if_num,
		     const struct ipmi_user_hndl *handler,
		     void                  *handler_data,
		     struct ipmi_user      **user)
{
	unsigned long flags;
	struct ipmi_user *new_user;
	int           rv, index;
	struct ipmi_smi *intf;

	/*
	 * There is no module usecount here, because it's not
	 * required.  Since this can only be used by and called from
	 * other modules, they will implicitly use this module, and
	 * thus this can't be removed unless the other modules are
	 * removed.
	 */

	if (handler == NULL)
		return -EINVAL;

	/*
	 * Make sure the driver is actually initialized, this handles
	 * problems with initialization order.
	 */
	rv = ipmi_init_msghandler();
	if (rv)
		return rv;

	new_user = kmalloc(sizeof(*new_user), GFP_KERNEL);
	if (!new_user)
		return -ENOMEM;

	index = srcu_read_lock(&ipmi_interfaces_srcu);
	list_for_each_entry_rcu(intf, &ipmi_interfaces, link) {
		if (intf->intf_num == if_num)
			goto found;
	}
	/* Not found, return an error */
	rv = -EINVAL;
	goto out_kfree;

 found:
	INIT_WORK(&new_user->remove_work, free_user_work);

	rv = init_srcu_struct(&new_user->release_barrier);
	if (rv)
		goto out_kfree;

	if (!try_module_get(intf->owner)) {
		rv = -ENODEV;
		goto out_kfree;
	}

	/* Note that each existing user holds a refcount to the interface. */
	kref_get(&intf->refcount);

	kref_init(&new_user->refcount);
	new_user->handler = handler;
	new_user->handler_data = handler_data;
	new_user->intf = intf;
	new_user->gets_events = false;

	rcu_assign_pointer(new_user->self, new_user);
	spin_lock_irqsave(&intf->seq_lock, flags);
	list_add_rcu(&new_user->link, &intf->users);
	spin_unlock_irqrestore(&intf->seq_lock, flags);
	if (handler->ipmi_watchdog_pretimeout) {
		/* User wants pretimeouts, so make sure to watch for them. */
		if (atomic_inc_return(&intf->event_waiters) == 1)
			need_waiter(intf);
	}
	srcu_read_unlock(&ipmi_interfaces_srcu, index);
	*user = new_user;
	return 0;

out_kfree:
	srcu_read_unlock(&ipmi_interfaces_srcu, index);
	kfree(new_user);
	return rv;
}
EXPORT_SYMBOL(ipmi_create_user);

int ipmi_get_smi_info(int if_num, struct ipmi_smi_info *data)
{
	int rv, index;
	struct ipmi_smi *intf;

	index = srcu_read_lock(&ipmi_interfaces_srcu);
	list_for_each_entry_rcu(intf, &ipmi_interfaces, link) {
		if (intf->intf_num == if_num)
			goto found;
	}
	srcu_read_unlock(&ipmi_interfaces_srcu, index);

	/* Not found, return an error */
	return -EINVAL;

found:
	if (!intf->handlers->get_smi_info)
		rv = -ENOTTY;
	else
		rv = intf->handlers->get_smi_info(intf->send_info, data);
	srcu_read_unlock(&ipmi_interfaces_srcu, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_get_smi_info);

static void free_user(struct kref *ref)
{
	struct ipmi_user *user = container_of(ref, struct ipmi_user, refcount);

	/* SRCU cleanup must happen in task context. */
	schedule_work(&user->remove_work);
}

static void _ipmi_destroy_user(struct ipmi_user *user)
{
	struct ipmi_smi  *intf = user->intf;
	int              i;
	unsigned long    flags;
	struct cmd_rcvr  *rcvr;
	struct cmd_rcvr  *rcvrs = NULL;

	if (!acquire_ipmi_user(user, &i)) {
		/*
		 * The user has already been cleaned up, just make sure
		 * nothing is using it and return.
		 */
		synchronize_srcu(&user->release_barrier);
		return;
	}

	rcu_assign_pointer(user->self, NULL);
	release_ipmi_user(user, i);

	synchronize_srcu(&user->release_barrier);

	if (user->handler->shutdown)
		user->handler->shutdown(user->handler_data);

	if (user->handler->ipmi_watchdog_pretimeout)
		atomic_dec(&intf->event_waiters);

	if (user->gets_events)
		atomic_dec(&intf->event_waiters);

	/* Remove the user from the interface's sequence table. */
	spin_lock_irqsave(&intf->seq_lock, flags);
	list_del_rcu(&user->link);

	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++) {
		if (intf->seq_table[i].inuse
		    && (intf->seq_table[i].recv_msg->user == user)) {
			intf->seq_table[i].inuse = 0;
			ipmi_free_recv_msg(intf->seq_table[i].recv_msg);
		}
	}
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	/*
	 * Remove the user from the command receiver's table.  First
	 * we build a list of everything (not using the standard link,
	 * since other things may be using it till we do
	 * synchronize_srcu()) then free everything in that list.
	 */
	mutex_lock(&intf->cmd_rcvrs_mutex);
	list_for_each_entry_rcu(rcvr, &intf->cmd_rcvrs, link) {
		if (rcvr->user == user) {
			list_del_rcu(&rcvr->link);
			rcvr->next = rcvrs;
			rcvrs = rcvr;
		}
	}
	mutex_unlock(&intf->cmd_rcvrs_mutex);
	synchronize_rcu();
	while (rcvrs) {
		rcvr = rcvrs;
		rcvrs = rcvr->next;
		kfree(rcvr);
	}

	kref_put(&intf->refcount, intf_free);
	module_put(intf->owner);
}

int ipmi_destroy_user(struct ipmi_user *user)
{
	_ipmi_destroy_user(user);

	kref_put(&user->refcount, free_user);

	return 0;
}
EXPORT_SYMBOL(ipmi_destroy_user);

int ipmi_get_version(struct ipmi_user *user,
		     unsigned char *major,
		     unsigned char *minor)
{
	struct ipmi_device_id id;
	int rv, index;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	rv = bmc_get_device_id(user->intf, NULL, &id, NULL, NULL);
	if (!rv) {
		*major = ipmi_version_major(&id);
		*minor = ipmi_version_minor(&id);
	}
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_get_version);

int ipmi_set_my_address(struct ipmi_user *user,
			unsigned int  channel,
			unsigned char address)
{
	int index, rv = 0;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	if (channel >= IPMI_MAX_CHANNELS) {
		rv = -EINVAL;
	} else {
		channel = array_index_nospec(channel, IPMI_MAX_CHANNELS);
		user->intf->addrinfo[channel].address = address;
	}
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_set_my_address);

int ipmi_get_my_address(struct ipmi_user *user,
			unsigned int  channel,
			unsigned char *address)
{
	int index, rv = 0;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	if (channel >= IPMI_MAX_CHANNELS) {
		rv = -EINVAL;
	} else {
		channel = array_index_nospec(channel, IPMI_MAX_CHANNELS);
		*address = user->intf->addrinfo[channel].address;
	}
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_get_my_address);

int ipmi_set_my_LUN(struct ipmi_user *user,
		    unsigned int  channel,
		    unsigned char LUN)
{
	int index, rv = 0;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	if (channel >= IPMI_MAX_CHANNELS) {
		rv = -EINVAL;
	} else {
		channel = array_index_nospec(channel, IPMI_MAX_CHANNELS);
		user->intf->addrinfo[channel].lun = LUN & 0x3;
	}
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_set_my_LUN);

int ipmi_get_my_LUN(struct ipmi_user *user,
		    unsigned int  channel,
		    unsigned char *address)
{
	int index, rv = 0;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	if (channel >= IPMI_MAX_CHANNELS) {
		rv = -EINVAL;
	} else {
		channel = array_index_nospec(channel, IPMI_MAX_CHANNELS);
		*address = user->intf->addrinfo[channel].lun;
	}
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_get_my_LUN);

int ipmi_get_maintenance_mode(struct ipmi_user *user)
{
	int mode, index;
	unsigned long flags;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	spin_lock_irqsave(&user->intf->maintenance_mode_lock, flags);
	mode = user->intf->maintenance_mode;
	spin_unlock_irqrestore(&user->intf->maintenance_mode_lock, flags);
	release_ipmi_user(user, index);

	return mode;
}
EXPORT_SYMBOL(ipmi_get_maintenance_mode);

static void maintenance_mode_update(struct ipmi_smi *intf)
{
	if (intf->handlers->set_maintenance_mode)
		intf->handlers->set_maintenance_mode(
			intf->send_info, intf->maintenance_mode_enable);
}

int ipmi_set_maintenance_mode(struct ipmi_user *user, int mode)
{
	int rv = 0, index;
	unsigned long flags;
	struct ipmi_smi *intf = user->intf;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	spin_lock_irqsave(&intf->maintenance_mode_lock, flags);
	if (intf->maintenance_mode != mode) {
		switch (mode) {
		case IPMI_MAINTENANCE_MODE_AUTO:
			intf->maintenance_mode_enable
				= (intf->auto_maintenance_timeout > 0);
			break;

		case IPMI_MAINTENANCE_MODE_OFF:
			intf->maintenance_mode_enable = false;
			break;

		case IPMI_MAINTENANCE_MODE_ON:
			intf->maintenance_mode_enable = true;
			break;

		default:
			rv = -EINVAL;
			goto out_unlock;
		}
		intf->maintenance_mode = mode;

		maintenance_mode_update(intf);
	}
 out_unlock:
	spin_unlock_irqrestore(&intf->maintenance_mode_lock, flags);
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_set_maintenance_mode);

int ipmi_set_gets_events(struct ipmi_user *user, bool val)
{
	unsigned long        flags;
	struct ipmi_smi      *intf = user->intf;
	struct ipmi_recv_msg *msg, *msg2;
	struct list_head     msgs;
	int index;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	INIT_LIST_HEAD(&msgs);

	spin_lock_irqsave(&intf->events_lock, flags);
	if (user->gets_events == val)
		goto out;

	user->gets_events = val;

	if (val) {
		if (atomic_inc_return(&intf->event_waiters) == 1)
			need_waiter(intf);
	} else {
		atomic_dec(&intf->event_waiters);
	}

	if (intf->delivering_events)
		/*
		 * Another thread is delivering events for this, so
		 * let it handle any new events.
		 */
		goto out;

	/* Deliver any queued events. */
	while (user->gets_events && !list_empty(&intf->waiting_events)) {
		list_for_each_entry_safe(msg, msg2, &intf->waiting_events, link)
			list_move_tail(&msg->link, &msgs);
		intf->waiting_events_count = 0;
		if (intf->event_msg_printed) {
			dev_warn(intf->si_dev,
				 PFX "Event queue no longer full\n");
			intf->event_msg_printed = 0;
		}

		intf->delivering_events = 1;
		spin_unlock_irqrestore(&intf->events_lock, flags);

		list_for_each_entry_safe(msg, msg2, &msgs, link) {
			msg->user = user;
			kref_get(&user->refcount);
			deliver_local_response(intf, msg);
		}

		spin_lock_irqsave(&intf->events_lock, flags);
		intf->delivering_events = 0;
	}

 out:
	spin_unlock_irqrestore(&intf->events_lock, flags);
	release_ipmi_user(user, index);

	return 0;
}
EXPORT_SYMBOL(ipmi_set_gets_events);

static struct cmd_rcvr *find_cmd_rcvr(struct ipmi_smi *intf,
				      unsigned char netfn,
				      unsigned char cmd,
				      unsigned char chan)
{
	struct cmd_rcvr *rcvr;

	list_for_each_entry_rcu(rcvr, &intf->cmd_rcvrs, link) {
		if ((rcvr->netfn == netfn) && (rcvr->cmd == cmd)
					&& (rcvr->chans & (1 << chan)))
			return rcvr;
	}
	return NULL;
}

static int is_cmd_rcvr_exclusive(struct ipmi_smi *intf,
				 unsigned char netfn,
				 unsigned char cmd,
				 unsigned int  chans)
{
	struct cmd_rcvr *rcvr;

	list_for_each_entry_rcu(rcvr, &intf->cmd_rcvrs, link) {
		if ((rcvr->netfn == netfn) && (rcvr->cmd == cmd)
					&& (rcvr->chans & chans))
			return 0;
	}
	return 1;
}

int ipmi_register_for_cmd(struct ipmi_user *user,
			  unsigned char netfn,
			  unsigned char cmd,
			  unsigned int  chans)
{
	struct ipmi_smi *intf = user->intf;
	struct cmd_rcvr *rcvr;
	int rv = 0, index;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	rcvr = kmalloc(sizeof(*rcvr), GFP_KERNEL);
	if (!rcvr) {
		rv = -ENOMEM;
		goto out_release;
	}
	rcvr->cmd = cmd;
	rcvr->netfn = netfn;
	rcvr->chans = chans;
	rcvr->user = user;

	mutex_lock(&intf->cmd_rcvrs_mutex);
	/* Make sure the command/netfn is not already registered. */
	if (!is_cmd_rcvr_exclusive(intf, netfn, cmd, chans)) {
		rv = -EBUSY;
		goto out_unlock;
	}

	if (atomic_inc_return(&intf->event_waiters) == 1)
		need_waiter(intf);

	list_add_rcu(&rcvr->link, &intf->cmd_rcvrs);

out_unlock:
	mutex_unlock(&intf->cmd_rcvrs_mutex);
	if (rv)
		kfree(rcvr);
out_release:
	release_ipmi_user(user, index);

	return rv;
}
EXPORT_SYMBOL(ipmi_register_for_cmd);

int ipmi_unregister_for_cmd(struct ipmi_user *user,
			    unsigned char netfn,
			    unsigned char cmd,
			    unsigned int  chans)
{
	struct ipmi_smi *intf = user->intf;
	struct cmd_rcvr *rcvr;
	struct cmd_rcvr *rcvrs = NULL;
	int i, rv = -ENOENT, index;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	mutex_lock(&intf->cmd_rcvrs_mutex);
	for (i = 0; i < IPMI_NUM_CHANNELS; i++) {
		if (((1 << i) & chans) == 0)
			continue;
		rcvr = find_cmd_rcvr(intf, netfn, cmd, i);
		if (rcvr == NULL)
			continue;
		if (rcvr->user == user) {
			rv = 0;
			rcvr->chans &= ~chans;
			if (rcvr->chans == 0) {
				list_del_rcu(&rcvr->link);
				rcvr->next = rcvrs;
				rcvrs = rcvr;
			}
		}
	}
	mutex_unlock(&intf->cmd_rcvrs_mutex);
	synchronize_rcu();
	release_ipmi_user(user, index);
	while (rcvrs) {
		atomic_dec(&intf->event_waiters);
		rcvr = rcvrs;
		rcvrs = rcvr->next;
		kfree(rcvr);
	}

	return rv;
}
EXPORT_SYMBOL(ipmi_unregister_for_cmd);

static unsigned char
ipmb_checksum(unsigned char *data, int size)
{
	unsigned char csum = 0;

	for (; size > 0; size--, data++)
		csum += *data;

	return -csum;
}

static inline void format_ipmb_msg(struct ipmi_smi_msg   *smi_msg,
				   struct kernel_ipmi_msg *msg,
				   struct ipmi_ipmb_addr *ipmb_addr,
				   long                  msgid,
				   unsigned char         ipmb_seq,
				   int                   broadcast,
				   unsigned char         source_address,
				   unsigned char         source_lun)
{
	int i = broadcast;

	/* Format the IPMB header data. */
	smi_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_msg->data[1] = IPMI_SEND_MSG_CMD;
	smi_msg->data[2] = ipmb_addr->channel;
	if (broadcast)
		smi_msg->data[3] = 0;
	smi_msg->data[i+3] = ipmb_addr->slave_addr;
	smi_msg->data[i+4] = (msg->netfn << 2) | (ipmb_addr->lun & 0x3);
	smi_msg->data[i+5] = ipmb_checksum(&smi_msg->data[i + 3], 2);
	smi_msg->data[i+6] = source_address;
	smi_msg->data[i+7] = (ipmb_seq << 2) | source_lun;
	smi_msg->data[i+8] = msg->cmd;

	/* Now tack on the data to the message. */
	if (msg->data_len > 0)
		memcpy(&smi_msg->data[i + 9], msg->data, msg->data_len);
	smi_msg->data_size = msg->data_len + 9;

	/* Now calculate the checksum and tack it on. */
	smi_msg->data[i+smi_msg->data_size]
		= ipmb_checksum(&smi_msg->data[i + 6], smi_msg->data_size - 6);

	/*
	 * Add on the checksum size and the offset from the
	 * broadcast.
	 */
	smi_msg->data_size += 1 + i;

	smi_msg->msgid = msgid;
}

static inline void format_lan_msg(struct ipmi_smi_msg   *smi_msg,
				  struct kernel_ipmi_msg *msg,
				  struct ipmi_lan_addr  *lan_addr,
				  long                  msgid,
				  unsigned char         ipmb_seq,
				  unsigned char         source_lun)
{
	/* Format the IPMB header data. */
	smi_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_msg->data[1] = IPMI_SEND_MSG_CMD;
	smi_msg->data[2] = lan_addr->channel;
	smi_msg->data[3] = lan_addr->session_handle;
	smi_msg->data[4] = lan_addr->remote_SWID;
	smi_msg->data[5] = (msg->netfn << 2) | (lan_addr->lun & 0x3);
	smi_msg->data[6] = ipmb_checksum(&smi_msg->data[4], 2);
	smi_msg->data[7] = lan_addr->local_SWID;
	smi_msg->data[8] = (ipmb_seq << 2) | source_lun;
	smi_msg->data[9] = msg->cmd;

	/* Now tack on the data to the message. */
	if (msg->data_len > 0)
		memcpy(&smi_msg->data[10], msg->data, msg->data_len);
	smi_msg->data_size = msg->data_len + 10;

	/* Now calculate the checksum and tack it on. */
	smi_msg->data[smi_msg->data_size]
		= ipmb_checksum(&smi_msg->data[7], smi_msg->data_size - 7);

	/*
	 * Add on the checksum size and the offset from the
	 * broadcast.
	 */
	smi_msg->data_size += 1;

	smi_msg->msgid = msgid;
}

static struct ipmi_smi_msg *smi_add_send_msg(struct ipmi_smi *intf,
					     struct ipmi_smi_msg *smi_msg,
					     int priority)
{
	if (intf->curr_msg) {
		if (priority > 0)
			list_add_tail(&smi_msg->link, &intf->hp_xmit_msgs);
		else
			list_add_tail(&smi_msg->link, &intf->xmit_msgs);
		smi_msg = NULL;
	} else {
		intf->curr_msg = smi_msg;
	}

	return smi_msg;
}


static void smi_send(struct ipmi_smi *intf,
		     const struct ipmi_smi_handlers *handlers,
		     struct ipmi_smi_msg *smi_msg, int priority)
{
	int run_to_completion = intf->run_to_completion;

	if (run_to_completion) {
		smi_msg = smi_add_send_msg(intf, smi_msg, priority);
	} else {
		unsigned long flags;

		spin_lock_irqsave(&intf->xmit_msgs_lock, flags);
		smi_msg = smi_add_send_msg(intf, smi_msg, priority);
		spin_unlock_irqrestore(&intf->xmit_msgs_lock, flags);
	}

	if (smi_msg)
		handlers->sender(intf->send_info, smi_msg);
}

static bool is_maintenance_mode_cmd(struct kernel_ipmi_msg *msg)
{
	return (((msg->netfn == IPMI_NETFN_APP_REQUEST)
		 && ((msg->cmd == IPMI_COLD_RESET_CMD)
		     || (msg->cmd == IPMI_WARM_RESET_CMD)))
		|| (msg->netfn == IPMI_NETFN_FIRMWARE_REQUEST));
}

static int i_ipmi_req_sysintf(struct ipmi_smi        *intf,
			      struct ipmi_addr       *addr,
			      long                   msgid,
			      struct kernel_ipmi_msg *msg,
			      struct ipmi_smi_msg    *smi_msg,
			      struct ipmi_recv_msg   *recv_msg,
			      int                    retries,
			      unsigned int           retry_time_ms)
{
	struct ipmi_system_interface_addr *smi_addr;

	if (msg->netfn & 1)
		/* Responses are not allowed to the SMI. */
		return -EINVAL;

	smi_addr = (struct ipmi_system_interface_addr *) addr;
	if (smi_addr->lun > 3) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	memcpy(&recv_msg->addr, smi_addr, sizeof(*smi_addr));

	if ((msg->netfn == IPMI_NETFN_APP_REQUEST)
	    && ((msg->cmd == IPMI_SEND_MSG_CMD)
		|| (msg->cmd == IPMI_GET_MSG_CMD)
		|| (msg->cmd == IPMI_READ_EVENT_MSG_BUFFER_CMD))) {
		/*
		 * We don't let the user do these, since we manage
		 * the sequence numbers.
		 */
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	if (is_maintenance_mode_cmd(msg)) {
		unsigned long flags;

		spin_lock_irqsave(&intf->maintenance_mode_lock, flags);
		intf->auto_maintenance_timeout
			= maintenance_mode_timeout_ms;
		if (!intf->maintenance_mode
		    && !intf->maintenance_mode_enable) {
			intf->maintenance_mode_enable = true;
			maintenance_mode_update(intf);
		}
		spin_unlock_irqrestore(&intf->maintenance_mode_lock,
				       flags);
	}

	if (msg->data_len + 2 > IPMI_MAX_MSG_LENGTH) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EMSGSIZE;
	}

	smi_msg->data[0] = (msg->netfn << 2) | (smi_addr->lun & 0x3);
	smi_msg->data[1] = msg->cmd;
	smi_msg->msgid = msgid;
	smi_msg->user_data = recv_msg;
	if (msg->data_len > 0)
		memcpy(&smi_msg->data[2], msg->data, msg->data_len);
	smi_msg->data_size = msg->data_len + 2;
	ipmi_inc_stat(intf, sent_local_commands);

	return 0;
}

static int i_ipmi_req_ipmb(struct ipmi_smi        *intf,
			   struct ipmi_addr       *addr,
			   long                   msgid,
			   struct kernel_ipmi_msg *msg,
			   struct ipmi_smi_msg    *smi_msg,
			   struct ipmi_recv_msg   *recv_msg,
			   unsigned char          source_address,
			   unsigned char          source_lun,
			   int                    retries,
			   unsigned int           retry_time_ms)
{
	struct ipmi_ipmb_addr *ipmb_addr;
	unsigned char ipmb_seq;
	long seqid;
	int broadcast = 0;
	struct ipmi_channel *chans;
	int rv = 0;

	if (addr->channel >= IPMI_MAX_CHANNELS) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	chans = READ_ONCE(intf->channel_list)->c;

	if (chans[addr->channel].medium != IPMI_CHANNEL_MEDIUM_IPMB) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	if (addr->addr_type == IPMI_IPMB_BROADCAST_ADDR_TYPE) {
		/*
		 * Broadcasts add a zero at the beginning of the
		 * message, but otherwise is the same as an IPMB
		 * address.
		 */
		addr->addr_type = IPMI_IPMB_ADDR_TYPE;
		broadcast = 1;
		retries = 0; /* Don't retry broadcasts. */
	}

	/*
	 * 9 for the header and 1 for the checksum, plus
	 * possibly one for the broadcast.
	 */
	if ((msg->data_len + 10 + broadcast) > IPMI_MAX_MSG_LENGTH) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EMSGSIZE;
	}

	ipmb_addr = (struct ipmi_ipmb_addr *) addr;
	if (ipmb_addr->lun > 3) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	memcpy(&recv_msg->addr, ipmb_addr, sizeof(*ipmb_addr));

	if (recv_msg->msg.netfn & 0x1) {
		/*
		 * It's a response, so use the user's sequence
		 * from msgid.
		 */
		ipmi_inc_stat(intf, sent_ipmb_responses);
		format_ipmb_msg(smi_msg, msg, ipmb_addr, msgid,
				msgid, broadcast,
				source_address, source_lun);

		/*
		 * Save the receive message so we can use it
		 * to deliver the response.
		 */
		smi_msg->user_data = recv_msg;
	} else {
		/* It's a command, so get a sequence for it. */
		unsigned long flags;

		spin_lock_irqsave(&intf->seq_lock, flags);

		if (is_maintenance_mode_cmd(msg))
			intf->ipmb_maintenance_mode_timeout =
				maintenance_mode_timeout_ms;

		if (intf->ipmb_maintenance_mode_timeout && retry_time_ms == 0)
			/* Different default in maintenance mode */
			retry_time_ms = default_maintenance_retry_ms;

		/*
		 * Create a sequence number with a 1 second
		 * timeout and 4 retries.
		 */
		rv = intf_next_seq(intf,
				   recv_msg,
				   retry_time_ms,
				   retries,
				   broadcast,
				   &ipmb_seq,
				   &seqid);
		if (rv)
			/*
			 * We have used up all the sequence numbers,
			 * probably, so abort.
			 */
			goto out_err;

		ipmi_inc_stat(intf, sent_ipmb_commands);

		/*
		 * Store the sequence number in the message,
		 * so that when the send message response
		 * comes back we can start the timer.
		 */
		format_ipmb_msg(smi_msg, msg, ipmb_addr,
				STORE_SEQ_IN_MSGID(ipmb_seq, seqid),
				ipmb_seq, broadcast,
				source_address, source_lun);

		/*
		 * Copy the message into the recv message data, so we
		 * can retransmit it later if necessary.
		 */
		memcpy(recv_msg->msg_data, smi_msg->data,
		       smi_msg->data_size);
		recv_msg->msg.data = recv_msg->msg_data;
		recv_msg->msg.data_len = smi_msg->data_size;

		/*
		 * We don't unlock until here, because we need
		 * to copy the completed message into the
		 * recv_msg before we release the lock.
		 * Otherwise, race conditions may bite us.  I
		 * know that's pretty paranoid, but I prefer
		 * to be correct.
		 */
out_err:
		spin_unlock_irqrestore(&intf->seq_lock, flags);
	}

	return rv;
}

static int i_ipmi_req_lan(struct ipmi_smi        *intf,
			  struct ipmi_addr       *addr,
			  long                   msgid,
			  struct kernel_ipmi_msg *msg,
			  struct ipmi_smi_msg    *smi_msg,
			  struct ipmi_recv_msg   *recv_msg,
			  unsigned char          source_lun,
			  int                    retries,
			  unsigned int           retry_time_ms)
{
	struct ipmi_lan_addr  *lan_addr;
	unsigned char ipmb_seq;
	long seqid;
	struct ipmi_channel *chans;
	int rv = 0;

	if (addr->channel >= IPMI_MAX_CHANNELS) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	chans = READ_ONCE(intf->channel_list)->c;

	if ((chans[addr->channel].medium
				!= IPMI_CHANNEL_MEDIUM_8023LAN)
			&& (chans[addr->channel].medium
			    != IPMI_CHANNEL_MEDIUM_ASYNC)) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	/* 11 for the header and 1 for the checksum. */
	if ((msg->data_len + 12) > IPMI_MAX_MSG_LENGTH) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EMSGSIZE;
	}

	lan_addr = (struct ipmi_lan_addr *) addr;
	if (lan_addr->lun > 3) {
		ipmi_inc_stat(intf, sent_invalid_commands);
		return -EINVAL;
	}

	memcpy(&recv_msg->addr, lan_addr, sizeof(*lan_addr));

	if (recv_msg->msg.netfn & 0x1) {
		/*
		 * It's a response, so use the user's sequence
		 * from msgid.
		 */
		ipmi_inc_stat(intf, sent_lan_responses);
		format_lan_msg(smi_msg, msg, lan_addr, msgid,
			       msgid, source_lun);

		/*
		 * Save the receive message so we can use it
		 * to deliver the response.
		 */
		smi_msg->user_data = recv_msg;
	} else {
		/* It's a command, so get a sequence for it. */
		unsigned long flags;

		spin_lock_irqsave(&intf->seq_lock, flags);

		/*
		 * Create a sequence number with a 1 second
		 * timeout and 4 retries.
		 */
		rv = intf_next_seq(intf,
				   recv_msg,
				   retry_time_ms,
				   retries,
				   0,
				   &ipmb_seq,
				   &seqid);
		if (rv)
			/*
			 * We have used up all the sequence numbers,
			 * probably, so abort.
			 */
			goto out_err;

		ipmi_inc_stat(intf, sent_lan_commands);

		/*
		 * Store the sequence number in the message,
		 * so that when the send message response
		 * comes back we can start the timer.
		 */
		format_lan_msg(smi_msg, msg, lan_addr,
			       STORE_SEQ_IN_MSGID(ipmb_seq, seqid),
			       ipmb_seq, source_lun);

		/*
		 * Copy the message into the recv message data, so we
		 * can retransmit it later if necessary.
		 */
		memcpy(recv_msg->msg_data, smi_msg->data,
		       smi_msg->data_size);
		recv_msg->msg.data = recv_msg->msg_data;
		recv_msg->msg.data_len = smi_msg->data_size;

		/*
		 * We don't unlock until here, because we need
		 * to copy the completed message into the
		 * recv_msg before we release the lock.
		 * Otherwise, race conditions may bite us.  I
		 * know that's pretty paranoid, but I prefer
		 * to be correct.
		 */
out_err:
		spin_unlock_irqrestore(&intf->seq_lock, flags);
	}

	return rv;
}

/*
 * Separate from ipmi_request so that the user does not have to be
 * supplied in certain circumstances (mainly at panic time).  If
 * messages are supplied, they will be freed, even if an error
 * occurs.
 */
static int i_ipmi_request(struct ipmi_user     *user,
			  struct ipmi_smi      *intf,
			  struct ipmi_addr     *addr,
			  long                 msgid,
			  struct kernel_ipmi_msg *msg,
			  void                 *user_msg_data,
			  void                 *supplied_smi,
			  struct ipmi_recv_msg *supplied_recv,
			  int                  priority,
			  unsigned char        source_address,
			  unsigned char        source_lun,
			  int                  retries,
			  unsigned int         retry_time_ms)
{
	struct ipmi_smi_msg *smi_msg;
	struct ipmi_recv_msg *recv_msg;
	int rv = 0;

	if (supplied_recv)
		recv_msg = supplied_recv;
	else {
		recv_msg = ipmi_alloc_recv_msg();
		if (recv_msg == NULL) {
			rv = -ENOMEM;
			goto out;
		}
	}
	recv_msg->user_msg_data = user_msg_data;

	if (supplied_smi)
		smi_msg = (struct ipmi_smi_msg *) supplied_smi;
	else {
		smi_msg = ipmi_alloc_smi_msg();
		if (smi_msg == NULL) {
			ipmi_free_recv_msg(recv_msg);
			rv = -ENOMEM;
			goto out;
		}
	}

	rcu_read_lock();
	if (intf->in_shutdown) {
		rv = -ENODEV;
		goto out_err;
	}

	recv_msg->user = user;
	if (user)
		/* The put happens when the message is freed. */
		kref_get(&user->refcount);
	recv_msg->msgid = msgid;
	/*
	 * Store the message to send in the receive message so timeout
	 * responses can get the proper response data.
	 */
	recv_msg->msg = *msg;

	if (addr->addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE) {
		rv = i_ipmi_req_sysintf(intf, addr, msgid, msg, smi_msg,
					recv_msg, retries, retry_time_ms);
	} else if (is_ipmb_addr(addr) || is_ipmb_bcast_addr(addr)) {
		rv = i_ipmi_req_ipmb(intf, addr, msgid, msg, smi_msg, recv_msg,
				     source_address, source_lun,
				     retries, retry_time_ms);
	} else if (is_lan_addr(addr)) {
		rv = i_ipmi_req_lan(intf, addr, msgid, msg, smi_msg, recv_msg,
				    source_lun, retries, retry_time_ms);
	} else {
	    /* Unknown address type. */
		ipmi_inc_stat(intf, sent_invalid_commands);
		rv = -EINVAL;
	}

	if (rv) {
out_err:
		ipmi_free_smi_msg(smi_msg);
		ipmi_free_recv_msg(recv_msg);
	} else {
		ipmi_debug_msg("Send", smi_msg->data, smi_msg->data_size);

		smi_send(intf, intf->handlers, smi_msg, priority);
	}
	rcu_read_unlock();

out:
	return rv;
}

static int check_addr(struct ipmi_smi  *intf,
		      struct ipmi_addr *addr,
		      unsigned char    *saddr,
		      unsigned char    *lun)
{
	if (addr->channel >= IPMI_MAX_CHANNELS)
		return -EINVAL;
	addr->channel = array_index_nospec(addr->channel, IPMI_MAX_CHANNELS);
	*lun = intf->addrinfo[addr->channel].lun;
	*saddr = intf->addrinfo[addr->channel].address;
	return 0;
}

int ipmi_request_settime(struct ipmi_user *user,
			 struct ipmi_addr *addr,
			 long             msgid,
			 struct kernel_ipmi_msg  *msg,
			 void             *user_msg_data,
			 int              priority,
			 int              retries,
			 unsigned int     retry_time_ms)
{
	unsigned char saddr = 0, lun = 0;
	int rv, index;

	if (!user)
		return -EINVAL;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	rv = check_addr(user->intf, addr, &saddr, &lun);
	if (!rv)
		rv = i_ipmi_request(user,
				    user->intf,
				    addr,
				    msgid,
				    msg,
				    user_msg_data,
				    NULL, NULL,
				    priority,
				    saddr,
				    lun,
				    retries,
				    retry_time_ms);

	release_ipmi_user(user, index);
	return rv;
}
EXPORT_SYMBOL(ipmi_request_settime);

int ipmi_request_supply_msgs(struct ipmi_user     *user,
			     struct ipmi_addr     *addr,
			     long                 msgid,
			     struct kernel_ipmi_msg *msg,
			     void                 *user_msg_data,
			     void                 *supplied_smi,
			     struct ipmi_recv_msg *supplied_recv,
			     int                  priority)
{
	unsigned char saddr = 0, lun = 0;
	int rv, index;

	if (!user)
		return -EINVAL;

	user = acquire_ipmi_user(user, &index);
	if (!user)
		return -ENODEV;

	rv = check_addr(user->intf, addr, &saddr, &lun);
	if (!rv)
		rv = i_ipmi_request(user,
				    user->intf,
				    addr,
				    msgid,
				    msg,
				    user_msg_data,
				    supplied_smi,
				    supplied_recv,
				    priority,
				    saddr,
				    lun,
				    -1, 0);

	release_ipmi_user(user, index);
	return rv;
}
EXPORT_SYMBOL(ipmi_request_supply_msgs);

static void bmc_device_id_handler(struct ipmi_smi *intf,
				  struct ipmi_recv_msg *msg)
{
	int rv;

	if ((msg->addr.addr_type != IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
			|| (msg->msg.netfn != IPMI_NETFN_APP_RESPONSE)
			|| (msg->msg.cmd != IPMI_GET_DEVICE_ID_CMD)) {
		dev_warn(intf->si_dev,
			 PFX "invalid device_id msg: addr_type=%d netfn=%x cmd=%x\n",
			msg->addr.addr_type, msg->msg.netfn, msg->msg.cmd);
		return;
	}

	rv = ipmi_demangle_device_id(msg->msg.netfn, msg->msg.cmd,
			msg->msg.data, msg->msg.data_len, &intf->bmc->fetch_id);
	if (rv) {
		dev_warn(intf->si_dev,
			 PFX "device id demangle failed: %d\n", rv);
		intf->bmc->dyn_id_set = 0;
	} else {
		/*
		 * Make sure the id data is available before setting
		 * dyn_id_set.
		 */
		smp_wmb();
		intf->bmc->dyn_id_set = 1;
	}

	wake_up(&intf->waitq);
}

static int
send_get_device_id_cmd(struct ipmi_smi *intf)
{
	struct ipmi_system_interface_addr si;
	struct kernel_ipmi_msg msg;

	si.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si.channel = IPMI_BMC_CHANNEL;
	si.lun = 0;

	msg.netfn = IPMI_NETFN_APP_REQUEST;
	msg.cmd = IPMI_GET_DEVICE_ID_CMD;
	msg.data = NULL;
	msg.data_len = 0;

	return i_ipmi_request(NULL,
			      intf,
			      (struct ipmi_addr *) &si,
			      0,
			      &msg,
			      intf,
			      NULL,
			      NULL,
			      0,
			      intf->addrinfo[0].address,
			      intf->addrinfo[0].lun,
			      -1, 0);
}

static int __get_device_id(struct ipmi_smi *intf, struct bmc_device *bmc)
{
	int rv;

	bmc->dyn_id_set = 2;

	intf->null_user_handler = bmc_device_id_handler;

	rv = send_get_device_id_cmd(intf);
	if (rv)
		return rv;

	wait_event(intf->waitq, bmc->dyn_id_set != 2);

	if (!bmc->dyn_id_set)
		rv = -EIO; /* Something went wrong in the fetch. */

	/* dyn_id_set makes the id data available. */
	smp_rmb();

	intf->null_user_handler = NULL;

	return rv;
}

/*
 * Fetch the device id for the bmc/interface.  You must pass in either
 * bmc or intf, this code will get the other one.  If the data has
 * been recently fetched, this will just use the cached data.  Otherwise
 * it will run a new fetch.
 *
 * Except for the first time this is called (in ipmi_add_smi()),
 * this will always return good data;
 */
static int __bmc_get_device_id(struct ipmi_smi *intf, struct bmc_device *bmc,
			       struct ipmi_device_id *id,
			       bool *guid_set, guid_t *guid, int intf_num)
{
	int rv = 0;
	int prev_dyn_id_set, prev_guid_set;
	bool intf_set = intf != NULL;

	if (!intf) {
		mutex_lock(&bmc->dyn_mutex);
retry_bmc_lock:
		if (list_empty(&bmc->intfs)) {
			mutex_unlock(&bmc->dyn_mutex);
			return -ENOENT;
		}
		intf = list_first_entry(&bmc->intfs, struct ipmi_smi,
					bmc_link);
		kref_get(&intf->refcount);
		mutex_unlock(&bmc->dyn_mutex);
		mutex_lock(&intf->bmc_reg_mutex);
		mutex_lock(&bmc->dyn_mutex);
		if (intf != list_first_entry(&bmc->intfs, struct ipmi_smi,
					     bmc_link)) {
			mutex_unlock(&intf->bmc_reg_mutex);
			kref_put(&intf->refcount, intf_free);
			goto retry_bmc_lock;
		}
	} else {
		mutex_lock(&intf->bmc_reg_mutex);
		bmc = intf->bmc;
		mutex_lock(&bmc->dyn_mutex);
		kref_get(&intf->refcount);
	}

	/* If we have a valid and current ID, just return that. */
	if (intf->in_bmc_register ||
	    (bmc->dyn_id_set && time_is_after_jiffies(bmc->dyn_id_expiry)))
		goto out_noprocessing;

	prev_guid_set = bmc->dyn_guid_set;
	__get_guid(intf);

	prev_dyn_id_set = bmc->dyn_id_set;
	rv = __get_device_id(intf, bmc);
	if (rv)
		goto out;

	/*
	 * The guid, device id, manufacturer id, and product id should
	 * not change on a BMC.  If it does we have to do some dancing.
	 */
	if (!intf->bmc_registered
	    || (!prev_guid_set && bmc->dyn_guid_set)
	    || (!prev_dyn_id_set && bmc->dyn_id_set)
	    || (prev_guid_set && bmc->dyn_guid_set
		&& !guid_equal(&bmc->guid, &bmc->fetch_guid))
	    || bmc->id.device_id != bmc->fetch_id.device_id
	    || bmc->id.manufacturer_id != bmc->fetch_id.manufacturer_id
	    || bmc->id.product_id != bmc->fetch_id.product_id) {
		struct ipmi_device_id id = bmc->fetch_id;
		int guid_set = bmc->dyn_guid_set;
		guid_t guid;

		guid = bmc->fetch_guid;
		mutex_unlock(&bmc->dyn_mutex);

		__ipmi_bmc_unregister(intf);
		/* Fill in the temporary BMC for good measure. */
		intf->bmc->id = id;
		intf->bmc->dyn_guid_set = guid_set;
		intf->bmc->guid = guid;
		if (__ipmi_bmc_register(intf, &id, guid_set, &guid, intf_num))
			need_waiter(intf); /* Retry later on an error. */
		else
			__scan_channels(intf, &id);


		if (!intf_set) {
			/*
			 * We weren't given the interface on the
			 * command line, so restart the operation on
			 * the next interface for the BMC.
			 */
			mutex_unlock(&intf->bmc_reg_mutex);
			mutex_lock(&bmc->dyn_mutex);
			goto retry_bmc_lock;
		}

		/* We have a new BMC, set it up. */
		bmc = intf->bmc;
		mutex_lock(&bmc->dyn_mutex);
		goto out_noprocessing;
	} else if (memcmp(&bmc->fetch_id, &bmc->id, sizeof(bmc->id)))
		/* Version info changes, scan the channels again. */
		__scan_channels(intf, &bmc->fetch_id);

	bmc->dyn_id_expiry = jiffies + IPMI_DYN_DEV_ID_EXPIRY;

out:
	if (rv && prev_dyn_id_set) {
		rv = 0; /* Ignore failures if we have previous data. */
		bmc->dyn_id_set = prev_dyn_id_set;
	}
	if (!rv) {
		bmc->id = bmc->fetch_id;
		if (bmc->dyn_guid_set)
			bmc->guid = bmc->fetch_guid;
		else if (prev_guid_set)
			/*
			 * The guid used to be valid and it failed to fetch,
			 * just use the cached value.
			 */
			bmc->dyn_guid_set = prev_guid_set;
	}
out_noprocessing:
	if (!rv) {
		if (id)
			*id = bmc->id;

		if (guid_set)
			*guid_set = bmc->dyn_guid_set;

		if (guid && bmc->dyn_guid_set)
			*guid =  bmc->guid;
	}

	mutex_unlock(&bmc->dyn_mutex);
	mutex_unlock(&intf->bmc_reg_mutex);

	kref_put(&intf->refcount, intf_free);
	return rv;
}

static int bmc_get_device_id(struct ipmi_smi *intf, struct bmc_device *bmc,
			     struct ipmi_device_id *id,
			     bool *guid_set, guid_t *guid)
{
	return __bmc_get_device_id(intf, bmc, id, guid_set, guid, -1);
}

static ssize_t device_id_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 10, "%u\n", id.device_id);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t provides_device_sdrs_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 10, "%u\n", (id.device_revision & 0x80) >> 7);
}
static DEVICE_ATTR_RO(provides_device_sdrs);

static ssize_t revision_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 20, "%u\n", id.device_revision & 0x0F);
}
static DEVICE_ATTR_RO(revision);

static ssize_t firmware_revision_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 20, "%u.%x\n", id.firmware_revision_1,
			id.firmware_revision_2);
}
static DEVICE_ATTR_RO(firmware_revision);

static ssize_t ipmi_version_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 20, "%u.%u\n",
			ipmi_version_major(&id),
			ipmi_version_minor(&id));
}
static DEVICE_ATTR_RO(ipmi_version);

static ssize_t add_dev_support_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 10, "0x%02x\n", id.additional_device_support);
}
static DEVICE_ATTR(additional_device_support, S_IRUGO, add_dev_support_show,
		   NULL);

static ssize_t manufacturer_id_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 20, "0x%6.6x\n", id.manufacturer_id);
}
static DEVICE_ATTR_RO(manufacturer_id);

static ssize_t product_id_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 10, "0x%4.4x\n", id.product_id);
}
static DEVICE_ATTR_RO(product_id);

static ssize_t aux_firmware_rev_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	struct ipmi_device_id id;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
	if (rv)
		return rv;

	return snprintf(buf, 21, "0x%02x 0x%02x 0x%02x 0x%02x\n",
			id.aux_firmware_revision[3],
			id.aux_firmware_revision[2],
			id.aux_firmware_revision[1],
			id.aux_firmware_revision[0]);
}
static DEVICE_ATTR(aux_firmware_revision, S_IRUGO, aux_firmware_rev_show, NULL);

static ssize_t guid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct bmc_device *bmc = to_bmc_device(dev);
	bool guid_set;
	guid_t guid;
	int rv;

	rv = bmc_get_device_id(NULL, bmc, NULL, &guid_set, &guid);
	if (rv)
		return rv;
	if (!guid_set)
		return -ENOENT;

	return snprintf(buf, 38, "%pUl\n", guid.b);
}
static DEVICE_ATTR_RO(guid);

static struct attribute *bmc_dev_attrs[] = {
	&dev_attr_device_id.attr,
	&dev_attr_provides_device_sdrs.attr,
	&dev_attr_revision.attr,
	&dev_attr_firmware_revision.attr,
	&dev_attr_ipmi_version.attr,
	&dev_attr_additional_device_support.attr,
	&dev_attr_manufacturer_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_aux_firmware_revision.attr,
	&dev_attr_guid.attr,
	NULL
};

static umode_t bmc_dev_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct bmc_device *bmc = to_bmc_device(dev);
	umode_t mode = attr->mode;
	int rv;

	if (attr == &dev_attr_aux_firmware_revision.attr) {
		struct ipmi_device_id id;

		rv = bmc_get_device_id(NULL, bmc, &id, NULL, NULL);
		return (!rv && id.aux_firmware_revision_set) ? mode : 0;
	}
	if (attr == &dev_attr_guid.attr) {
		bool guid_set;

		rv = bmc_get_device_id(NULL, bmc, NULL, &guid_set, NULL);
		return (!rv && guid_set) ? mode : 0;
	}
	return mode;
}

static const struct attribute_group bmc_dev_attr_group = {
	.attrs		= bmc_dev_attrs,
	.is_visible	= bmc_dev_attr_is_visible,
};

static const struct attribute_group *bmc_dev_attr_groups[] = {
	&bmc_dev_attr_group,
	NULL
};

static const struct device_type bmc_device_type = {
	.groups		= bmc_dev_attr_groups,
};

static int __find_bmc_guid(struct device *dev, void *data)
{
	guid_t *guid = data;
	struct bmc_device *bmc;
	int rv;

	if (dev->type != &bmc_device_type)
		return 0;

	bmc = to_bmc_device(dev);
	rv = bmc->dyn_guid_set && guid_equal(&bmc->guid, guid);
	if (rv)
		rv = kref_get_unless_zero(&bmc->usecount);
	return rv;
}

/*
 * Returns with the bmc's usecount incremented, if it is non-NULL.
 */
static struct bmc_device *ipmi_find_bmc_guid(struct device_driver *drv,
					     guid_t *guid)
{
	struct device *dev;
	struct bmc_device *bmc = NULL;

	dev = driver_find_device(drv, NULL, guid, __find_bmc_guid);
	if (dev) {
		bmc = to_bmc_device(dev);
		put_device(dev);
	}
	return bmc;
}

struct prod_dev_id {
	unsigned int  product_id;
	unsigned char device_id;
};

static int __find_bmc_prod_dev_id(struct device *dev, void *data)
{
	struct prod_dev_id *cid = data;
	struct bmc_device *bmc;
	int rv;

	if (dev->type != &bmc_device_type)
		return 0;

	bmc = to_bmc_device(dev);
	rv = (bmc->id.product_id == cid->product_id
	      && bmc->id.device_id == cid->device_id);
	if (rv)
		rv = kref_get_unless_zero(&bmc->usecount);
	return rv;
}

/*
 * Returns with the bmc's usecount incremented, if it is non-NULL.
 */
static struct bmc_device *ipmi_find_bmc_prod_dev_id(
	struct device_driver *drv,
	unsigned int product_id, unsigned char device_id)
{
	struct prod_dev_id id = {
		.product_id = product_id,
		.device_id = device_id,
	};
	struct device *dev;
	struct bmc_device *bmc = NULL;

	dev = driver_find_device(drv, NULL, &id, __find_bmc_prod_dev_id);
	if (dev) {
		bmc = to_bmc_device(dev);
		put_device(dev);
	}
	return bmc;
}

static DEFINE_IDA(ipmi_bmc_ida);

static void
release_bmc_device(struct device *dev)
{
	kfree(to_bmc_device(dev));
}

static void cleanup_bmc_work(struct work_struct *work)
{
	struct bmc_device *bmc = container_of(work, struct bmc_device,
					      remove_work);
	int id = bmc->pdev.id; /* Unregister overwrites id */

	platform_device_unregister(&bmc->pdev);
	ida_simple_remove(&ipmi_bmc_ida, id);
}

static void
cleanup_bmc_device(struct kref *ref)
{
	struct bmc_device *bmc = container_of(ref, struct bmc_device, usecount);

	/*
	 * Remove the platform device in a work queue to avoid issues
	 * with removing the device attributes while reading a device
	 * attribute.
	 */
	schedule_work(&bmc->remove_work);
}

/*
 * Must be called with intf->bmc_reg_mutex held.
 */
static void __ipmi_bmc_unregister(struct ipmi_smi *intf)
{
	struct bmc_device *bmc = intf->bmc;

	if (!intf->bmc_registered)
		return;

	sysfs_remove_link(&intf->si_dev->kobj, "bmc");
	sysfs_remove_link(&bmc->pdev.dev.kobj, intf->my_dev_name);
	kfree(intf->my_dev_name);
	intf->my_dev_name = NULL;

	mutex_lock(&bmc->dyn_mutex);
	list_del(&intf->bmc_link);
	mutex_unlock(&bmc->dyn_mutex);
	intf->bmc = &intf->tmp_bmc;
	kref_put(&bmc->usecount, cleanup_bmc_device);
	intf->bmc_registered = false;
}

static void ipmi_bmc_unregister(struct ipmi_smi *intf)
{
	mutex_lock(&intf->bmc_reg_mutex);
	__ipmi_bmc_unregister(intf);
	mutex_unlock(&intf->bmc_reg_mutex);
}

/*
 * Must be called with intf->bmc_reg_mutex held.
 */
static int __ipmi_bmc_register(struct ipmi_smi *intf,
			       struct ipmi_device_id *id,
			       bool guid_set, guid_t *guid, int intf_num)
{
	int               rv;
	struct bmc_device *bmc;
	struct bmc_device *old_bmc;

	/*
	 * platform_device_register() can cause bmc_reg_mutex to
	 * be claimed because of the is_visible functions of
	 * the attributes.  Eliminate possible recursion and
	 * release the lock.
	 */
	intf->in_bmc_register = true;
	mutex_unlock(&intf->bmc_reg_mutex);

	/*
	 * Try to find if there is an bmc_device struct
	 * representing the interfaced BMC already
	 */
	mutex_lock(&ipmidriver_mutex);
	if (guid_set)
		old_bmc = ipmi_find_bmc_guid(&ipmidriver.driver, guid);
	else
		old_bmc = ipmi_find_bmc_prod_dev_id(&ipmidriver.driver,
						    id->product_id,
						    id->device_id);

	/*
	 * If there is already an bmc_device, free the new one,
	 * otherwise register the new BMC device
	 */
	if (old_bmc) {
		bmc = old_bmc;
		/*
		 * Note: old_bmc already has usecount incremented by
		 * the BMC find functions.
		 */
		intf->bmc = old_bmc;
		mutex_lock(&bmc->dyn_mutex);
		list_add_tail(&intf->bmc_link, &bmc->intfs);
		mutex_unlock(&bmc->dyn_mutex);

		dev_info(intf->si_dev,
			 "ipmi: interfacing existing BMC (man_id: 0x%6.6x,"
			 " prod_id: 0x%4.4x, dev_id: 0x%2.2x)\n",
			 bmc->id.manufacturer_id,
			 bmc->id.product_id,
			 bmc->id.device_id);
	} else {
		bmc = kzalloc(sizeof(*bmc), GFP_KERNEL);
		if (!bmc) {
			rv = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(&bmc->intfs);
		mutex_init(&bmc->dyn_mutex);
		INIT_WORK(&bmc->remove_work, cleanup_bmc_work);

		bmc->id = *id;
		bmc->dyn_id_set = 1;
		bmc->dyn_guid_set = guid_set;
		bmc->guid = *guid;
		bmc->dyn_id_expiry = jiffies + IPMI_DYN_DEV_ID_EXPIRY;

		bmc->pdev.name = "ipmi_bmc";

		rv = ida_simple_get(&ipmi_bmc_ida, 0, 0, GFP_KERNEL);
		if (rv < 0)
			goto out;
		bmc->pdev.dev.driver = &ipmidriver.driver;
		bmc->pdev.id = rv;
		bmc->pdev.dev.release = release_bmc_device;
		bmc->pdev.dev.type = &bmc_device_type;
		kref_init(&bmc->usecount);

		intf->bmc = bmc;
		mutex_lock(&bmc->dyn_mutex);
		list_add_tail(&intf->bmc_link, &bmc->intfs);
		mutex_unlock(&bmc->dyn_mutex);

		rv = platform_device_register(&bmc->pdev);
		if (rv) {
			dev_err(intf->si_dev,
				PFX " Unable to register bmc device: %d\n",
				rv);
			goto out_list_del;
		}

		dev_info(intf->si_dev,
			 "Found new BMC (man_id: 0x%6.6x, prod_id: 0x%4.4x, dev_id: 0x%2.2x)\n",
			 bmc->id.manufacturer_id,
			 bmc->id.product_id,
			 bmc->id.device_id);
	}

	/*
	 * create symlink from system interface device to bmc device
	 * and back.
	 */
	rv = sysfs_create_link(&intf->si_dev->kobj, &bmc->pdev.dev.kobj, "bmc");
	if (rv) {
		dev_err(intf->si_dev,
			PFX "Unable to create bmc symlink: %d\n", rv);
		goto out_put_bmc;
	}

	if (intf_num == -1)
		intf_num = intf->intf_num;
	intf->my_dev_name = kasprintf(GFP_KERNEL, "ipmi%d", intf_num);
	if (!intf->my_dev_name) {
		rv = -ENOMEM;
		dev_err(intf->si_dev,
			PFX "Unable to allocate link from BMC: %d\n", rv);
		goto out_unlink1;
	}

	rv = sysfs_create_link(&bmc->pdev.dev.kobj, &intf->si_dev->kobj,
			       intf->my_dev_name);
	if (rv) {
		kfree(intf->my_dev_name);
		intf->my_dev_name = NULL;
		dev_err(intf->si_dev,
			PFX "Unable to create symlink to bmc: %d\n", rv);
		goto out_free_my_dev_name;
	}

	intf->bmc_registered = true;

out:
	mutex_unlock(&ipmidriver_mutex);
	mutex_lock(&intf->bmc_reg_mutex);
	intf->in_bmc_register = false;
	return rv;


out_free_my_dev_name:
	kfree(intf->my_dev_name);
	intf->my_dev_name = NULL;

out_unlink1:
	sysfs_remove_link(&intf->si_dev->kobj, "bmc");

out_put_bmc:
	mutex_lock(&bmc->dyn_mutex);
	list_del(&intf->bmc_link);
	mutex_unlock(&bmc->dyn_mutex);
	intf->bmc = &intf->tmp_bmc;
	kref_put(&bmc->usecount, cleanup_bmc_device);
	goto out;

out_list_del:
	mutex_lock(&bmc->dyn_mutex);
	list_del(&intf->bmc_link);
	mutex_unlock(&bmc->dyn_mutex);
	intf->bmc = &intf->tmp_bmc;
	put_device(&bmc->pdev.dev);
	goto out;
}

static int
send_guid_cmd(struct ipmi_smi *intf, int chan)
{
	struct kernel_ipmi_msg            msg;
	struct ipmi_system_interface_addr si;

	si.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si.channel = IPMI_BMC_CHANNEL;
	si.lun = 0;

	msg.netfn = IPMI_NETFN_APP_REQUEST;
	msg.cmd = IPMI_GET_DEVICE_GUID_CMD;
	msg.data = NULL;
	msg.data_len = 0;
	return i_ipmi_request(NULL,
			      intf,
			      (struct ipmi_addr *) &si,
			      0,
			      &msg,
			      intf,
			      NULL,
			      NULL,
			      0,
			      intf->addrinfo[0].address,
			      intf->addrinfo[0].lun,
			      -1, 0);
}

static void guid_handler(struct ipmi_smi *intf, struct ipmi_recv_msg *msg)
{
	struct bmc_device *bmc = intf->bmc;

	if ((msg->addr.addr_type != IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    || (msg->msg.netfn != IPMI_NETFN_APP_RESPONSE)
	    || (msg->msg.cmd != IPMI_GET_DEVICE_GUID_CMD))
		/* Not for me */
		return;

	if (msg->msg.data[0] != 0) {
		/* Error from getting the GUID, the BMC doesn't have one. */
		bmc->dyn_guid_set = 0;
		goto out;
	}

	if (msg->msg.data_len < 17) {
		bmc->dyn_guid_set = 0;
		dev_warn(intf->si_dev,
			 PFX "The GUID response from the BMC was too short, it was %d but should have been 17.  Assuming GUID is not available.\n",
			 msg->msg.data_len);
		goto out;
	}

	memcpy(bmc->fetch_guid.b, msg->msg.data + 1, 16);
	/*
	 * Make sure the guid data is available before setting
	 * dyn_guid_set.
	 */
	smp_wmb();
	bmc->dyn_guid_set = 1;
 out:
	wake_up(&intf->waitq);
}

static void __get_guid(struct ipmi_smi *intf)
{
	int rv;
	struct bmc_device *bmc = intf->bmc;

	bmc->dyn_guid_set = 2;
	intf->null_user_handler = guid_handler;
	rv = send_guid_cmd(intf, 0);
	if (rv)
		/* Send failed, no GUID available. */
		bmc->dyn_guid_set = 0;

	wait_event(intf->waitq, bmc->dyn_guid_set != 2);

	/* dyn_guid_set makes the guid data available. */
	smp_rmb();

	intf->null_user_handler = NULL;
}

static int
send_channel_info_cmd(struct ipmi_smi *intf, int chan)
{
	struct kernel_ipmi_msg            msg;
	unsigned char                     data[1];
	struct ipmi_system_interface_addr si;

	si.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si.channel = IPMI_BMC_CHANNEL;
	si.lun = 0;

	msg.netfn = IPMI_NETFN_APP_REQUEST;
	msg.cmd = IPMI_GET_CHANNEL_INFO_CMD;
	msg.data = data;
	msg.data_len = 1;
	data[0] = chan;
	return i_ipmi_request(NULL,
			      intf,
			      (struct ipmi_addr *) &si,
			      0,
			      &msg,
			      intf,
			      NULL,
			      NULL,
			      0,
			      intf->addrinfo[0].address,
			      intf->addrinfo[0].lun,
			      -1, 0);
}

static void
channel_handler(struct ipmi_smi *intf, struct ipmi_recv_msg *msg)
{
	int rv = 0;
	int ch;
	unsigned int set = intf->curr_working_cset;
	struct ipmi_channel *chans;

	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_APP_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_CHANNEL_INFO_CMD)) {
		/* It's the one we want */
		if (msg->msg.data[0] != 0) {
			/* Got an error from the channel, just go on. */

			if (msg->msg.data[0] == IPMI_INVALID_COMMAND_ERR) {
				/*
				 * If the MC does not support this
				 * command, that is legal.  We just
				 * assume it has one IPMB at channel
				 * zero.
				 */
				intf->wchannels[set].c[0].medium
					= IPMI_CHANNEL_MEDIUM_IPMB;
				intf->wchannels[set].c[0].protocol
					= IPMI_CHANNEL_PROTOCOL_IPMB;

				intf->channel_list = intf->wchannels + set;
				intf->channels_ready = true;
				wake_up(&intf->waitq);
				goto out;
			}
			goto next_channel;
		}
		if (msg->msg.data_len < 4) {
			/* Message not big enough, just go on. */
			goto next_channel;
		}
		ch = intf->curr_channel;
		chans = intf->wchannels[set].c;
		chans[ch].medium = msg->msg.data[2] & 0x7f;
		chans[ch].protocol = msg->msg.data[3] & 0x1f;

 next_channel:
		intf->curr_channel++;
		if (intf->curr_channel >= IPMI_MAX_CHANNELS) {
			intf->channel_list = intf->wchannels + set;
			intf->channels_ready = true;
			wake_up(&intf->waitq);
		} else {
			intf->channel_list = intf->wchannels + set;
			intf->channels_ready = true;
			rv = send_channel_info_cmd(intf, intf->curr_channel);
		}

		if (rv) {
			/* Got an error somehow, just give up. */
			dev_warn(intf->si_dev,
				 PFX "Error sending channel information for channel %d: %d\n",
				 intf->curr_channel, rv);

			intf->channel_list = intf->wchannels + set;
			intf->channels_ready = true;
			wake_up(&intf->waitq);
		}
	}
 out:
	return;
}

/*
 * Must be holding intf->bmc_reg_mutex to call this.
 */
static int __scan_channels(struct ipmi_smi *intf, struct ipmi_device_id *id)
{
	int rv;

	if (ipmi_version_major(id) > 1
			|| (ipmi_version_major(id) == 1
			    && ipmi_version_minor(id) >= 5)) {
		unsigned int set;

		/*
		 * Start scanning the channels to see what is
		 * available.
		 */
		set = !intf->curr_working_cset;
		intf->curr_working_cset = set;
		memset(&intf->wchannels[set], 0,
		       sizeof(struct ipmi_channel_set));

		intf->null_user_handler = channel_handler;
		intf->curr_channel = 0;
		rv = send_channel_info_cmd(intf, 0);
		if (rv) {
			dev_warn(intf->si_dev,
				 "Error sending channel information for channel 0, %d\n",
				 rv);
			return -EIO;
		}

		/* Wait for the channel info to be read. */
		wait_event(intf->waitq, intf->channels_ready);
		intf->null_user_handler = NULL;
	} else {
		unsigned int set = intf->curr_working_cset;

		/* Assume a single IPMB channel at zero. */
		intf->wchannels[set].c[0].medium = IPMI_CHANNEL_MEDIUM_IPMB;
		intf->wchannels[set].c[0].protocol = IPMI_CHANNEL_PROTOCOL_IPMB;
		intf->channel_list = intf->wchannels + set;
		intf->channels_ready = true;
	}

	return 0;
}

static void ipmi_poll(struct ipmi_smi *intf)
{
	if (intf->handlers->poll)
		intf->handlers->poll(intf->send_info);
	/* In case something came in */
	handle_new_recv_msgs(intf);
}

void ipmi_poll_interface(struct ipmi_user *user)
{
	ipmi_poll(user->intf);
}
EXPORT_SYMBOL(ipmi_poll_interface);

static void redo_bmc_reg(struct work_struct *work)
{
	struct ipmi_smi *intf = container_of(work, struct ipmi_smi,
					     bmc_reg_work);

	if (!intf->in_shutdown)
		bmc_get_device_id(intf, NULL, NULL, NULL, NULL);

	kref_put(&intf->refcount, intf_free);
}

int ipmi_add_smi(struct module         *owner,
		 const struct ipmi_smi_handlers *handlers,
		 void		       *send_info,
		 struct device         *si_dev,
		 unsigned char         slave_addr)
{
	int              i, j;
	int              rv;
	struct ipmi_smi *intf, *tintf;
	struct list_head *link;
	struct ipmi_device_id id;

	/*
	 * Make sure the driver is actually initialized, this handles
	 * problems with initialization order.
	 */
	rv = ipmi_init_msghandler();
	if (rv)
		return rv;

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return -ENOMEM;

	rv = init_srcu_struct(&intf->users_srcu);
	if (rv) {
		kfree(intf);
		return rv;
	}

	intf->owner = owner;
	intf->bmc = &intf->tmp_bmc;
	INIT_LIST_HEAD(&intf->bmc->intfs);
	mutex_init(&intf->bmc->dyn_mutex);
	INIT_LIST_HEAD(&intf->bmc_link);
	mutex_init(&intf->bmc_reg_mutex);
	intf->intf_num = -1; /* Mark it invalid for now. */
	kref_init(&intf->refcount);
	INIT_WORK(&intf->bmc_reg_work, redo_bmc_reg);
	intf->si_dev = si_dev;
	for (j = 0; j < IPMI_MAX_CHANNELS; j++) {
		intf->addrinfo[j].address = IPMI_BMC_SLAVE_ADDR;
		intf->addrinfo[j].lun = 2;
	}
	if (slave_addr != 0)
		intf->addrinfo[0].address = slave_addr;
	INIT_LIST_HEAD(&intf->users);
	intf->handlers = handlers;
	intf->send_info = send_info;
	spin_lock_init(&intf->seq_lock);
	for (j = 0; j < IPMI_IPMB_NUM_SEQ; j++) {
		intf->seq_table[j].inuse = 0;
		intf->seq_table[j].seqid = 0;
	}
	intf->curr_seq = 0;
	spin_lock_init(&intf->waiting_rcv_msgs_lock);
	INIT_LIST_HEAD(&intf->waiting_rcv_msgs);
	tasklet_init(&intf->recv_tasklet,
		     smi_recv_tasklet,
		     (unsigned long) intf);
	atomic_set(&intf->watchdog_pretimeouts_to_deliver, 0);
	spin_lock_init(&intf->xmit_msgs_lock);
	INIT_LIST_HEAD(&intf->xmit_msgs);
	INIT_LIST_HEAD(&intf->hp_xmit_msgs);
	spin_lock_init(&intf->events_lock);
	atomic_set(&intf->event_waiters, 0);
	intf->ticks_to_req_ev = IPMI_REQUEST_EV_TIME;
	INIT_LIST_HEAD(&intf->waiting_events);
	intf->waiting_events_count = 0;
	mutex_init(&intf->cmd_rcvrs_mutex);
	spin_lock_init(&intf->maintenance_mode_lock);
	INIT_LIST_HEAD(&intf->cmd_rcvrs);
	init_waitqueue_head(&intf->waitq);
	for (i = 0; i < IPMI_NUM_STATS; i++)
		atomic_set(&intf->stats[i], 0);

	mutex_lock(&ipmi_interfaces_mutex);
	/* Look for a hole in the numbers. */
	i = 0;
	link = &ipmi_interfaces;
	list_for_each_entry_rcu(tintf, &ipmi_interfaces, link) {
		if (tintf->intf_num != i) {
			link = &tintf->link;
			break;
		}
		i++;
	}
	/* Add the new interface in numeric order. */
	if (i == 0)
		list_add_rcu(&intf->link, &ipmi_interfaces);
	else
		list_add_tail_rcu(&intf->link, link);

	rv = handlers->start_processing(send_info, intf);
	if (rv)
		goto out_err;

	rv = __bmc_get_device_id(intf, NULL, &id, NULL, NULL, i);
	if (rv) {
		dev_err(si_dev, "Unable to get the device id: %d\n", rv);
		goto out_err_started;
	}

	mutex_lock(&intf->bmc_reg_mutex);
	rv = __scan_channels(intf, &id);
	mutex_unlock(&intf->bmc_reg_mutex);
	if (rv)
		goto out_err_bmc_reg;

	/*
	 * Keep memory order straight for RCU readers.  Make
	 * sure everything else is committed to memory before
	 * setting intf_num to mark the interface valid.
	 */
	smp_wmb();
	intf->intf_num = i;
	mutex_unlock(&ipmi_interfaces_mutex);

	/* After this point the interface is legal to use. */
	call_smi_watchers(i, intf->si_dev);

	return 0;

 out_err_bmc_reg:
	ipmi_bmc_unregister(intf);
 out_err_started:
	if (intf->handlers->shutdown)
		intf->handlers->shutdown(intf->send_info);
 out_err:
	list_del_rcu(&intf->link);
	mutex_unlock(&ipmi_interfaces_mutex);
	synchronize_srcu(&ipmi_interfaces_srcu);
	cleanup_srcu_struct(&intf->users_srcu);
	kref_put(&intf->refcount, intf_free);

	return rv;
}
EXPORT_SYMBOL(ipmi_add_smi);

static void deliver_smi_err_response(struct ipmi_smi *intf,
				     struct ipmi_smi_msg *msg,
				     unsigned char err)
{
	msg->rsp[0] = msg->data[0] | 4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = err;
	msg->rsp_size = 3;
	/* It's an error, so it will never requeue, no need to check return. */
	handle_one_recv_msg(intf, msg);
}

static void cleanup_smi_msgs(struct ipmi_smi *intf)
{
	int              i;
	struct seq_table *ent;
	struct ipmi_smi_msg *msg;
	struct list_head *entry;
	struct list_head tmplist;

	/* Clear out our transmit queues and hold the messages. */
	INIT_LIST_HEAD(&tmplist);
	list_splice_tail(&intf->hp_xmit_msgs, &tmplist);
	list_splice_tail(&intf->xmit_msgs, &tmplist);

	/* Current message first, to preserve order */
	while (intf->curr_msg && !list_empty(&intf->waiting_rcv_msgs)) {
		/* Wait for the message to clear out. */
		schedule_timeout(1);
	}

	/* No need for locks, the interface is down. */

	/*
	 * Return errors for all pending messages in queue and in the
	 * tables waiting for remote responses.
	 */
	while (!list_empty(&tmplist)) {
		entry = tmplist.next;
		list_del(entry);
		msg = list_entry(entry, struct ipmi_smi_msg, link);
		deliver_smi_err_response(intf, msg, IPMI_ERR_UNSPECIFIED);
	}

	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++) {
		ent = &intf->seq_table[i];
		if (!ent->inuse)
			continue;
		deliver_err_response(intf, ent->recv_msg, IPMI_ERR_UNSPECIFIED);
	}
}

void ipmi_unregister_smi(struct ipmi_smi *intf)
{
	struct ipmi_smi_watcher *w;
	int intf_num = intf->intf_num, index;

	mutex_lock(&ipmi_interfaces_mutex);
	intf->intf_num = -1;
	intf->in_shutdown = true;
	list_del_rcu(&intf->link);
	mutex_unlock(&ipmi_interfaces_mutex);
	synchronize_srcu(&ipmi_interfaces_srcu);

	/* At this point no users can be added to the interface. */

	/*
	 * Call all the watcher interfaces to tell them that
	 * an interface is going away.
	 */
	mutex_lock(&smi_watchers_mutex);
	list_for_each_entry(w, &smi_watchers, link)
		w->smi_gone(intf_num);
	mutex_unlock(&smi_watchers_mutex);

	index = srcu_read_lock(&intf->users_srcu);
	while (!list_empty(&intf->users)) {
		struct ipmi_user *user =
			container_of(list_next_rcu(&intf->users),
				     struct ipmi_user, link);

		_ipmi_destroy_user(user);
	}
	srcu_read_unlock(&intf->users_srcu, index);

	if (intf->handlers->shutdown)
		intf->handlers->shutdown(intf->send_info);

	cleanup_smi_msgs(intf);

	ipmi_bmc_unregister(intf);

	cleanup_srcu_struct(&intf->users_srcu);
	kref_put(&intf->refcount, intf_free);
}
EXPORT_SYMBOL(ipmi_unregister_smi);

static int handle_ipmb_get_msg_rsp(struct ipmi_smi *intf,
				   struct ipmi_smi_msg *msg)
{
	struct ipmi_ipmb_addr ipmb_addr;
	struct ipmi_recv_msg  *recv_msg;

	/*
	 * This is 11, not 10, because the response must contain a
	 * completion code.
	 */
	if (msg->rsp_size < 11) {
		/* Message not big enough, just ignore it. */
		ipmi_inc_stat(intf, invalid_ipmb_responses);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	ipmb_addr.addr_type = IPMI_IPMB_ADDR_TYPE;
	ipmb_addr.slave_addr = msg->rsp[6];
	ipmb_addr.channel = msg->rsp[3] & 0x0f;
	ipmb_addr.lun = msg->rsp[7] & 3;

	/*
	 * It's a response from a remote entity.  Look up the sequence
	 * number and handle the response.
	 */
	if (intf_find_seq(intf,
			  msg->rsp[7] >> 2,
			  msg->rsp[3] & 0x0f,
			  msg->rsp[8],
			  (msg->rsp[4] >> 2) & (~1),
			  (struct ipmi_addr *) &ipmb_addr,
			  &recv_msg)) {
		/*
		 * We were unable to find the sequence number,
		 * so just nuke the message.
		 */
		ipmi_inc_stat(intf, unhandled_ipmb_responses);
		return 0;
	}

	memcpy(recv_msg->msg_data, &msg->rsp[9], msg->rsp_size - 9);
	/*
	 * The other fields matched, so no need to set them, except
	 * for netfn, which needs to be the response that was
	 * returned, not the request value.
	 */
	recv_msg->msg.netfn = msg->rsp[4] >> 2;
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 10;
	recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	if (deliver_response(intf, recv_msg))
		ipmi_inc_stat(intf, unhandled_ipmb_responses);
	else
		ipmi_inc_stat(intf, handled_ipmb_responses);

	return 0;
}

static int handle_ipmb_get_msg_cmd(struct ipmi_smi *intf,
				   struct ipmi_smi_msg *msg)
{
	struct cmd_rcvr          *rcvr;
	int                      rv = 0;
	unsigned char            netfn;
	unsigned char            cmd;
	unsigned char            chan;
	struct ipmi_user         *user = NULL;
	struct ipmi_ipmb_addr    *ipmb_addr;
	struct ipmi_recv_msg     *recv_msg;

	if (msg->rsp_size < 10) {
		/* Message not big enough, just ignore it. */
		ipmi_inc_stat(intf, invalid_commands);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	netfn = msg->rsp[4] >> 2;
	cmd = msg->rsp[8];
	chan = msg->rsp[3] & 0xf;

	rcu_read_lock();
	rcvr = find_cmd_rcvr(intf, netfn, cmd, chan);
	if (rcvr) {
		user = rcvr->user;
		kref_get(&user->refcount);
	} else
		user = NULL;
	rcu_read_unlock();

	if (user == NULL) {
		/* We didn't find a user, deliver an error response. */
		ipmi_inc_stat(intf, unhandled_commands);

		msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
		msg->data[1] = IPMI_SEND_MSG_CMD;
		msg->data[2] = msg->rsp[3];
		msg->data[3] = msg->rsp[6];
		msg->data[4] = ((netfn + 1) << 2) | (msg->rsp[7] & 0x3);
		msg->data[5] = ipmb_checksum(&msg->data[3], 2);
		msg->data[6] = intf->addrinfo[msg->rsp[3] & 0xf].address;
		/* rqseq/lun */
		msg->data[7] = (msg->rsp[7] & 0xfc) | (msg->rsp[4] & 0x3);
		msg->data[8] = msg->rsp[8]; /* cmd */
		msg->data[9] = IPMI_INVALID_CMD_COMPLETION_CODE;
		msg->data[10] = ipmb_checksum(&msg->data[6], 4);
		msg->data_size = 11;

		ipmi_debug_msg("Invalid command:", msg->data, msg->data_size);

		rcu_read_lock();
		if (!intf->in_shutdown) {
			smi_send(intf, intf->handlers, msg, 0);
			/*
			 * We used the message, so return the value
			 * that causes it to not be freed or
			 * queued.
			 */
			rv = -1;
		}
		rcu_read_unlock();
	} else {
		recv_msg = ipmi_alloc_recv_msg();
		if (!recv_msg) {
			/*
			 * We couldn't allocate memory for the
			 * message, so requeue it for handling
			 * later.
			 */
			rv = 1;
			kref_put(&user->refcount, free_user);
		} else {
			/* Extract the source address from the data. */
			ipmb_addr = (struct ipmi_ipmb_addr *) &recv_msg->addr;
			ipmb_addr->addr_type = IPMI_IPMB_ADDR_TYPE;
			ipmb_addr->slave_addr = msg->rsp[6];
			ipmb_addr->lun = msg->rsp[7] & 3;
			ipmb_addr->channel = msg->rsp[3] & 0xf;

			/*
			 * Extract the rest of the message information
			 * from the IPMB header.
			 */
			recv_msg->user = user;
			recv_msg->recv_type = IPMI_CMD_RECV_TYPE;
			recv_msg->msgid = msg->rsp[7] >> 2;
			recv_msg->msg.netfn = msg->rsp[4] >> 2;
			recv_msg->msg.cmd = msg->rsp[8];
			recv_msg->msg.data = recv_msg->msg_data;

			/*
			 * We chop off 10, not 9 bytes because the checksum
			 * at the end also needs to be removed.
			 */
			recv_msg->msg.data_len = msg->rsp_size - 10;
			memcpy(recv_msg->msg_data, &msg->rsp[9],
			       msg->rsp_size - 10);
			if (deliver_response(intf, recv_msg))
				ipmi_inc_stat(intf, unhandled_commands);
			else
				ipmi_inc_stat(intf, handled_commands);
		}
	}

	return rv;
}

static int handle_lan_get_msg_rsp(struct ipmi_smi *intf,
				  struct ipmi_smi_msg *msg)
{
	struct ipmi_lan_addr  lan_addr;
	struct ipmi_recv_msg  *recv_msg;


	/*
	 * This is 13, not 12, because the response must contain a
	 * completion code.
	 */
	if (msg->rsp_size < 13) {
		/* Message not big enough, just ignore it. */
		ipmi_inc_stat(intf, invalid_lan_responses);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	lan_addr.addr_type = IPMI_LAN_ADDR_TYPE;
	lan_addr.session_handle = msg->rsp[4];
	lan_addr.remote_SWID = msg->rsp[8];
	lan_addr.local_SWID = msg->rsp[5];
	lan_addr.channel = msg->rsp[3] & 0x0f;
	lan_addr.privilege = msg->rsp[3] >> 4;
	lan_addr.lun = msg->rsp[9] & 3;

	/*
	 * It's a response from a remote entity.  Look up the sequence
	 * number and handle the response.
	 */
	if (intf_find_seq(intf,
			  msg->rsp[9] >> 2,
			  msg->rsp[3] & 0x0f,
			  msg->rsp[10],
			  (msg->rsp[6] >> 2) & (~1),
			  (struct ipmi_addr *) &lan_addr,
			  &recv_msg)) {
		/*
		 * We were unable to find the sequence number,
		 * so just nuke the message.
		 */
		ipmi_inc_stat(intf, unhandled_lan_responses);
		return 0;
	}

	memcpy(recv_msg->msg_data, &msg->rsp[11], msg->rsp_size - 11);
	/*
	 * The other fields matched, so no need to set them, except
	 * for netfn, which needs to be the response that was
	 * returned, not the request value.
	 */
	recv_msg->msg.netfn = msg->rsp[6] >> 2;
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 12;
	recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	if (deliver_response(intf, recv_msg))
		ipmi_inc_stat(intf, unhandled_lan_responses);
	else
		ipmi_inc_stat(intf, handled_lan_responses);

	return 0;
}

static int handle_lan_get_msg_cmd(struct ipmi_smi *intf,
				  struct ipmi_smi_msg *msg)
{
	struct cmd_rcvr          *rcvr;
	int                      rv = 0;
	unsigned char            netfn;
	unsigned char            cmd;
	unsigned char            chan;
	struct ipmi_user         *user = NULL;
	struct ipmi_lan_addr     *lan_addr;
	struct ipmi_recv_msg     *recv_msg;

	if (msg->rsp_size < 12) {
		/* Message not big enough, just ignore it. */
		ipmi_inc_stat(intf, invalid_commands);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	netfn = msg->rsp[6] >> 2;
	cmd = msg->rsp[10];
	chan = msg->rsp[3] & 0xf;

	rcu_read_lock();
	rcvr = find_cmd_rcvr(intf, netfn, cmd, chan);
	if (rcvr) {
		user = rcvr->user;
		kref_get(&user->refcount);
	} else
		user = NULL;
	rcu_read_unlock();

	if (user == NULL) {
		/* We didn't find a user, just give up. */
		ipmi_inc_stat(intf, unhandled_commands);

		/*
		 * Don't do anything with these messages, just allow
		 * them to be freed.
		 */
		rv = 0;
	} else {
		recv_msg = ipmi_alloc_recv_msg();
		if (!recv_msg) {
			/*
			 * We couldn't allocate memory for the
			 * message, so requeue it for handling later.
			 */
			rv = 1;
			kref_put(&user->refcount, free_user);
		} else {
			/* Extract the source address from the data. */
			lan_addr = (struct ipmi_lan_addr *) &recv_msg->addr;
			lan_addr->addr_type = IPMI_LAN_ADDR_TYPE;
			lan_addr->session_handle = msg->rsp[4];
			lan_addr->remote_SWID = msg->rsp[8];
			lan_addr->local_SWID = msg->rsp[5];
			lan_addr->lun = msg->rsp[9] & 3;
			lan_addr->channel = msg->rsp[3] & 0xf;
			lan_addr->privilege = msg->rsp[3] >> 4;

			/*
			 * Extract the rest of the message information
			 * from the IPMB header.
			 */
			recv_msg->user = user;
			recv_msg->recv_type = IPMI_CMD_RECV_TYPE;
			recv_msg->msgid = msg->rsp[9] >> 2;
			recv_msg->msg.netfn = msg->rsp[6] >> 2;
			recv_msg->msg.cmd = msg->rsp[10];
			recv_msg->msg.data = recv_msg->msg_data;

			/*
			 * We chop off 12, not 11 bytes because the checksum
			 * at the end also needs to be removed.
			 */
			recv_msg->msg.data_len = msg->rsp_size - 12;
			memcpy(recv_msg->msg_data, &msg->rsp[11],
			       msg->rsp_size - 12);
			if (deliver_response(intf, recv_msg))
				ipmi_inc_stat(intf, unhandled_commands);
			else
				ipmi_inc_stat(intf, handled_commands);
		}
	}

	return rv;
}

/*
 * This routine will handle "Get Message" command responses with
 * channels that use an OEM Medium. The message format belongs to
 * the OEM.  See IPMI 2.0 specification, Chapter 6 and
 * Chapter 22, sections 22.6 and 22.24 for more details.
 */
static int handle_oem_get_msg_cmd(struct ipmi_smi *intf,
				  struct ipmi_smi_msg *msg)
{
	struct cmd_rcvr       *rcvr;
	int                   rv = 0;
	unsigned char         netfn;
	unsigned char         cmd;
	unsigned char         chan;
	struct ipmi_user *user = NULL;
	struct ipmi_system_interface_addr *smi_addr;
	struct ipmi_recv_msg  *recv_msg;

	/*
	 * We expect the OEM SW to perform error checking
	 * so we just do some basic sanity checks
	 */
	if (msg->rsp_size < 4) {
		/* Message not big enough, just ignore it. */
		ipmi_inc_stat(intf, invalid_commands);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the response, just ignore it. */
		return 0;
	}

	/*
	 * This is an OEM Message so the OEM needs to know how
	 * handle the message. We do no interpretation.
	 */
	netfn = msg->rsp[0] >> 2;
	cmd = msg->rsp[1];
	chan = msg->rsp[3] & 0xf;

	rcu_read_lock();
	rcvr = find_cmd_rcvr(intf, netfn, cmd, chan);
	if (rcvr) {
		user = rcvr->user;
		kref_get(&user->refcount);
	} else
		user = NULL;
	rcu_read_unlock();

	if (user == NULL) {
		/* We didn't find a user, just give up. */
		ipmi_inc_stat(intf, unhandled_commands);

		/*
		 * Don't do anything with these messages, just allow
		 * them to be freed.
		 */

		rv = 0;
	} else {
		recv_msg = ipmi_alloc_recv_msg();
		if (!recv_msg) {
			/*
			 * We couldn't allocate memory for the
			 * message, so requeue it for handling
			 * later.
			 */
			rv = 1;
			kref_put(&user->refcount, free_user);
		} else {
			/*
			 * OEM Messages are expected to be delivered via
			 * the system interface to SMS software.  We might
			 * need to visit this again depending on OEM
			 * requirements
			 */
			smi_addr = ((struct ipmi_system_interface_addr *)
				    &recv_msg->addr);
			smi_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
			smi_addr->channel = IPMI_BMC_CHANNEL;
			smi_addr->lun = msg->rsp[0] & 3;

			recv_msg->user = user;
			recv_msg->user_msg_data = NULL;
			recv_msg->recv_type = IPMI_OEM_RECV_TYPE;
			recv_msg->msg.netfn = msg->rsp[0] >> 2;
			recv_msg->msg.cmd = msg->rsp[1];
			recv_msg->msg.data = recv_msg->msg_data;

			/*
			 * The message starts at byte 4 which follows the
			 * the Channel Byte in the "GET MESSAGE" command
			 */
			recv_msg->msg.data_len = msg->rsp_size - 4;
			memcpy(recv_msg->msg_data, &msg->rsp[4],
			       msg->rsp_size - 4);
			if (deliver_response(intf, recv_msg))
				ipmi_inc_stat(intf, unhandled_commands);
			else
				ipmi_inc_stat(intf, handled_commands);
		}
	}

	return rv;
}

static void copy_event_into_recv_msg(struct ipmi_recv_msg *recv_msg,
				     struct ipmi_smi_msg  *msg)
{
	struct ipmi_system_interface_addr *smi_addr;

	recv_msg->msgid = 0;
	smi_addr = (struct ipmi_system_interface_addr *) &recv_msg->addr;
	smi_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr->channel = IPMI_BMC_CHANNEL;
	smi_addr->lun = msg->rsp[0] & 3;
	recv_msg->recv_type = IPMI_ASYNC_EVENT_RECV_TYPE;
	recv_msg->msg.netfn = msg->rsp[0] >> 2;
	recv_msg->msg.cmd = msg->rsp[1];
	memcpy(recv_msg->msg_data, &msg->rsp[3], msg->rsp_size - 3);
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 3;
}

static int handle_read_event_rsp(struct ipmi_smi *intf,
				 struct ipmi_smi_msg *msg)
{
	struct ipmi_recv_msg *recv_msg, *recv_msg2;
	struct list_head     msgs;
	struct ipmi_user     *user;
	int rv = 0, deliver_count = 0, index;
	unsigned long        flags;

	if (msg->rsp_size < 19) {
		/* Message is too small to be an IPMB event. */
		ipmi_inc_stat(intf, invalid_events);
		return 0;
	}

	if (msg->rsp[2] != 0) {
		/* An error getting the event, just ignore it. */
		return 0;
	}

	INIT_LIST_HEAD(&msgs);

	spin_lock_irqsave(&intf->events_lock, flags);

	ipmi_inc_stat(intf, events);

	/*
	 * Allocate and fill in one message for every user that is
	 * getting events.
	 */
	index = srcu_read_lock(&intf->users_srcu);
	list_for_each_entry_rcu(user, &intf->users, link) {
		if (!user->gets_events)
			continue;

		recv_msg = ipmi_alloc_recv_msg();
		if (!recv_msg) {
			rcu_read_unlock();
			list_for_each_entry_safe(recv_msg, recv_msg2, &msgs,
						 link) {
				list_del(&recv_msg->link);
				ipmi_free_recv_msg(recv_msg);
			}
			/*
			 * We couldn't allocate memory for the
			 * message, so requeue it for handling
			 * later.
			 */
			rv = 1;
			goto out;
		}

		deliver_count++;

		copy_event_into_recv_msg(recv_msg, msg);
		recv_msg->user = user;
		kref_get(&user->refcount);
		list_add_tail(&recv_msg->link, &msgs);
	}
	srcu_read_unlock(&intf->users_srcu, index);

	if (deliver_count) {
		/* Now deliver all the messages. */
		list_for_each_entry_safe(recv_msg, recv_msg2, &msgs, link) {
			list_del(&recv_msg->link);
			deliver_local_response(intf, recv_msg);
		}
	} else if (intf->waiting_events_count < MAX_EVENTS_IN_QUEUE) {
		/*
		 * No one to receive the message, put it in queue if there's
		 * not already too many things in the queue.
		 */
		recv_msg = ipmi_alloc_recv_msg();
		if (!recv_msg) {
			/*
			 * We couldn't allocate memory for the
			 * message, so requeue it for handling
			 * later.
			 */
			rv = 1;
			goto out;
		}

		copy_event_into_recv_msg(recv_msg, msg);
		list_add_tail(&recv_msg->link, &intf->waiting_events);
		intf->waiting_events_count++;
	} else if (!intf->event_msg_printed) {
		/*
		 * There's too many things in the queue, discard this
		 * message.
		 */
		dev_warn(intf->si_dev,
			 PFX "Event queue full, discarding incoming events\n");
		intf->event_msg_printed = 1;
	}

 out:
	spin_unlock_irqrestore(&intf->events_lock, flags);

	return rv;
}

static int handle_bmc_rsp(struct ipmi_smi *intf,
			  struct ipmi_smi_msg *msg)
{
	struct ipmi_recv_msg *recv_msg;
	struct ipmi_system_interface_addr *smi_addr;

	recv_msg = (struct ipmi_recv_msg *) msg->user_data;
	if (recv_msg == NULL) {
		dev_warn(intf->si_dev,
			 "IPMI message received with no owner. This could be because of a malformed message, or because of a hardware error.  Contact your hardware vender for assistance\n");
		return 0;
	}

	recv_msg->recv_type = IPMI_RESPONSE_RECV_TYPE;
	recv_msg->msgid = msg->msgid;
	smi_addr = ((struct ipmi_system_interface_addr *)
		    &recv_msg->addr);
	smi_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr->channel = IPMI_BMC_CHANNEL;
	smi_addr->lun = msg->rsp[0] & 3;
	recv_msg->msg.netfn = msg->rsp[0] >> 2;
	recv_msg->msg.cmd = msg->rsp[1];
	memcpy(recv_msg->msg_data, &msg->rsp[2], msg->rsp_size - 2);
	recv_msg->msg.data = recv_msg->msg_data;
	recv_msg->msg.data_len = msg->rsp_size - 2;
	deliver_local_response(intf, recv_msg);

	return 0;
}

/*
 * Handle a received message.  Return 1 if the message should be requeued,
 * 0 if the message should be freed, or -1 if the message should not
 * be freed or requeued.
 */
static int handle_one_recv_msg(struct ipmi_smi *intf,
			       struct ipmi_smi_msg *msg)
{
	int requeue;
	int chan;

	ipmi_debug_msg("Recv:", msg->rsp, msg->rsp_size);
	if (msg->rsp_size < 2) {
		/* Message is too small to be correct. */
		dev_warn(intf->si_dev,
			 PFX "BMC returned to small a message for netfn %x cmd %x, got %d bytes\n",
			 (msg->data[0] >> 2) | 1, msg->data[1], msg->rsp_size);

		/* Generate an error response for the message. */
		msg->rsp[0] = msg->data[0] | (1 << 2);
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = IPMI_ERR_UNSPECIFIED;
		msg->rsp_size = 3;
	} else if (((msg->rsp[0] >> 2) != ((msg->data[0] >> 2) | 1))
		   || (msg->rsp[1] != msg->data[1])) {
		/*
		 * The NetFN and Command in the response is not even
		 * marginally correct.
		 */
		dev_warn(intf->si_dev,
			 PFX "BMC returned incorrect response, expected netfn %x cmd %x, got netfn %x cmd %x\n",
			 (msg->data[0] >> 2) | 1, msg->data[1],
			 msg->rsp[0] >> 2, msg->rsp[1]);

		/* Generate an error response for the message. */
		msg->rsp[0] = msg->data[0] | (1 << 2);
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = IPMI_ERR_UNSPECIFIED;
		msg->rsp_size = 3;
	}

	if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
	    && (msg->rsp[1] == IPMI_SEND_MSG_CMD)
	    && (msg->user_data != NULL)) {
		/*
		 * It's a response to a response we sent.  For this we
		 * deliver a send message response to the user.
		 */
		struct ipmi_recv_msg *recv_msg = msg->user_data;

		requeue = 0;
		if (msg->rsp_size < 2)
			/* Message is too small to be correct. */
			goto out;

		chan = msg->data[2] & 0x0f;
		if (chan >= IPMI_MAX_CHANNELS)
			/* Invalid channel number */
			goto out;

		if (!recv_msg)
			goto out;

		recv_msg->recv_type = IPMI_RESPONSE_RESPONSE_TYPE;
		recv_msg->msg.data = recv_msg->msg_data;
		recv_msg->msg.data_len = 1;
		recv_msg->msg_data[0] = msg->rsp[2];
		deliver_local_response(intf, recv_msg);
	} else if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
		   && (msg->rsp[1] == IPMI_GET_MSG_CMD)) {
		struct ipmi_channel   *chans;

		/* It's from the receive queue. */
		chan = msg->rsp[3] & 0xf;
		if (chan >= IPMI_MAX_CHANNELS) {
			/* Invalid channel number */
			requeue = 0;
			goto out;
		}

		/*
		 * We need to make sure the channels have been initialized.
		 * The channel_handler routine will set the "curr_channel"
		 * equal to or greater than IPMI_MAX_CHANNELS when all the
		 * channels for this interface have been initialized.
		 */
		if (!intf->channels_ready) {
			requeue = 0; /* Throw the message away */
			goto out;
		}

		chans = READ_ONCE(intf->channel_list)->c;

		switch (chans[chan].medium) {
		case IPMI_CHANNEL_MEDIUM_IPMB:
			if (msg->rsp[4] & 0x04) {
				/*
				 * It's a response, so find the
				 * requesting message and send it up.
				 */
				requeue = handle_ipmb_get_msg_rsp(intf, msg);
			} else {
				/*
				 * It's a command to the SMS from some other
				 * entity.  Handle that.
				 */
				requeue = handle_ipmb_get_msg_cmd(intf, msg);
			}
			break;

		case IPMI_CHANNEL_MEDIUM_8023LAN:
		case IPMI_CHANNEL_MEDIUM_ASYNC:
			if (msg->rsp[6] & 0x04) {
				/*
				 * It's a response, so find the
				 * requesting message and send it up.
				 */
				requeue = handle_lan_get_msg_rsp(intf, msg);
			} else {
				/*
				 * It's a command to the SMS from some other
				 * entity.  Handle that.
				 */
				requeue = handle_lan_get_msg_cmd(intf, msg);
			}
			break;

		default:
			/* Check for OEM Channels.  Clients had better
			   register for these commands. */
			if ((chans[chan].medium >= IPMI_CHANNEL_MEDIUM_OEM_MIN)
			    && (chans[chan].medium
				<= IPMI_CHANNEL_MEDIUM_OEM_MAX)) {
				requeue = handle_oem_get_msg_cmd(intf, msg);
			} else {
				/*
				 * We don't handle the channel type, so just
				 * free the message.
				 */
				requeue = 0;
			}
		}

	} else if ((msg->rsp[0] == ((IPMI_NETFN_APP_REQUEST|1) << 2))
		   && (msg->rsp[1] == IPMI_READ_EVENT_MSG_BUFFER_CMD)) {
		/* It's an asynchronous event. */
		requeue = handle_read_event_rsp(intf, msg);
	} else {
		/* It's a response from the local BMC. */
		requeue = handle_bmc_rsp(intf, msg);
	}

 out:
	return requeue;
}

/*
 * If there are messages in the queue or pretimeouts, handle them.
 */
static void handle_new_recv_msgs(struct ipmi_smi *intf)
{
	struct ipmi_smi_msg  *smi_msg;
	unsigned long        flags = 0;
	int                  rv;
	int                  run_to_completion = intf->run_to_completion;

	/* See if any waiting messages need to be processed. */
	if (!run_to_completion)
		spin_lock_irqsave(&intf->waiting_rcv_msgs_lock, flags);
	while (!list_empty(&intf->waiting_rcv_msgs)) {
		smi_msg = list_entry(intf->waiting_rcv_msgs.next,
				     struct ipmi_smi_msg, link);
		list_del(&smi_msg->link);
		if (!run_to_completion)
			spin_unlock_irqrestore(&intf->waiting_rcv_msgs_lock,
					       flags);
		rv = handle_one_recv_msg(intf, smi_msg);
		if (!run_to_completion)
			spin_lock_irqsave(&intf->waiting_rcv_msgs_lock, flags);
		if (rv > 0) {
			/*
			 * To preserve message order, quit if we
			 * can't handle a message.  Add the message
			 * back at the head, this is safe because this
			 * tasklet is the only thing that pulls the
			 * messages.
			 */
			list_add(&smi_msg->link, &intf->waiting_rcv_msgs);
			break;
		} else {
			if (rv == 0)
				/* Message handled */
				ipmi_free_smi_msg(smi_msg);
			/* If rv < 0, fatal error, del but don't free. */
		}
	}
	if (!run_to_completion)
		spin_unlock_irqrestore(&intf->waiting_rcv_msgs_lock, flags);

	/*
	 * If the pretimout count is non-zero, decrement one from it and
	 * deliver pretimeouts to all the users.
	 */
	if (atomic_add_unless(&intf->watchdog_pretimeouts_to_deliver, -1, 0)) {
		struct ipmi_user *user;
		int index;

		index = srcu_read_lock(&intf->users_srcu);
		list_for_each_entry_rcu(user, &intf->users, link) {
			if (user->handler->ipmi_watchdog_pretimeout)
				user->handler->ipmi_watchdog_pretimeout(
					user->handler_data);
		}
		srcu_read_unlock(&intf->users_srcu, index);
	}
}

static void smi_recv_tasklet(unsigned long val)
{
	unsigned long flags = 0; /* keep us warning-free. */
	struct ipmi_smi *intf = (struct ipmi_smi *) val;
	int run_to_completion = intf->run_to_completion;
	struct ipmi_smi_msg *newmsg = NULL;

	/*
	 * Start the next message if available.
	 *
	 * Do this here, not in the actual receiver, because we may deadlock
	 * because the lower layer is allowed to hold locks while calling
	 * message delivery.
	 */

	rcu_read_lock();

	if (!run_to_completion)
		spin_lock_irqsave(&intf->xmit_msgs_lock, flags);
	if (intf->curr_msg == NULL && !intf->in_shutdown) {
		struct list_head *entry = NULL;

		/* Pick the high priority queue first. */
		if (!list_empty(&intf->hp_xmit_msgs))
			entry = intf->hp_xmit_msgs.next;
		else if (!list_empty(&intf->xmit_msgs))
			entry = intf->xmit_msgs.next;

		if (entry) {
			list_del(entry);
			newmsg = list_entry(entry, struct ipmi_smi_msg, link);
			intf->curr_msg = newmsg;
		}
	}
	if (!run_to_completion)
		spin_unlock_irqrestore(&intf->xmit_msgs_lock, flags);
	if (newmsg)
		intf->handlers->sender(intf->send_info, newmsg);

	rcu_read_unlock();

	handle_new_recv_msgs(intf);
}

/* Handle a new message from the lower layer. */
void ipmi_smi_msg_received(struct ipmi_smi *intf,
			   struct ipmi_smi_msg *msg)
{
	unsigned long flags = 0; /* keep us warning-free. */
	int run_to_completion = intf->run_to_completion;

	if ((msg->data_size >= 2)
	    && (msg->data[0] == (IPMI_NETFN_APP_REQUEST << 2))
	    && (msg->data[1] == IPMI_SEND_MSG_CMD)
	    && (msg->user_data == NULL)) {

		if (intf->in_shutdown)
			goto free_msg;

		/*
		 * This is the local response to a command send, start
		 * the timer for these.  The user_data will not be
		 * NULL if this is a response send, and we will let
		 * response sends just go through.
		 */

		/*
		 * Check for errors, if we get certain errors (ones
		 * that mean basically we can try again later), we
		 * ignore them and start the timer.  Otherwise we
		 * report the error immediately.
		 */
		if ((msg->rsp_size >= 3) && (msg->rsp[2] != 0)
		    && (msg->rsp[2] != IPMI_NODE_BUSY_ERR)
		    && (msg->rsp[2] != IPMI_LOST_ARBITRATION_ERR)
		    && (msg->rsp[2] != IPMI_BUS_ERR)
		    && (msg->rsp[2] != IPMI_NAK_ON_WRITE_ERR)) {
			int ch = msg->rsp[3] & 0xf;
			struct ipmi_channel *chans;

			/* Got an error sending the message, handle it. */

			chans = READ_ONCE(intf->channel_list)->c;
			if ((chans[ch].medium == IPMI_CHANNEL_MEDIUM_8023LAN)
			    || (chans[ch].medium == IPMI_CHANNEL_MEDIUM_ASYNC))
				ipmi_inc_stat(intf, sent_lan_command_errs);
			else
				ipmi_inc_stat(intf, sent_ipmb_command_errs);
			intf_err_seq(intf, msg->msgid, msg->rsp[2]);
		} else
			/* The message was sent, start the timer. */
			intf_start_seq_timer(intf, msg->msgid);

free_msg:
		ipmi_free_smi_msg(msg);
	} else {
		/*
		 * To preserve message order, we keep a queue and deliver from
		 * a tasklet.
		 */
		if (!run_to_completion)
			spin_lock_irqsave(&intf->waiting_rcv_msgs_lock, flags);
		list_add_tail(&msg->link, &intf->waiting_rcv_msgs);
		if (!run_to_completion)
			spin_unlock_irqrestore(&intf->waiting_rcv_msgs_lock,
					       flags);
	}

	if (!run_to_completion)
		spin_lock_irqsave(&intf->xmit_msgs_lock, flags);
	/*
	 * We can get an asynchronous event or receive message in addition
	 * to commands we send.
	 */
	if (msg == intf->curr_msg)
		intf->curr_msg = NULL;
	if (!run_to_completion)
		spin_unlock_irqrestore(&intf->xmit_msgs_lock, flags);

	if (run_to_completion)
		smi_recv_tasklet((unsigned long) intf);
	else
		tasklet_schedule(&intf->recv_tasklet);
}
EXPORT_SYMBOL(ipmi_smi_msg_received);

void ipmi_smi_watchdog_pretimeout(struct ipmi_smi *intf)
{
	if (intf->in_shutdown)
		return;

	atomic_set(&intf->watchdog_pretimeouts_to_deliver, 1);
	tasklet_schedule(&intf->recv_tasklet);
}
EXPORT_SYMBOL(ipmi_smi_watchdog_pretimeout);

static struct ipmi_smi_msg *
smi_from_recv_msg(struct ipmi_smi *intf, struct ipmi_recv_msg *recv_msg,
		  unsigned char seq, long seqid)
{
	struct ipmi_smi_msg *smi_msg = ipmi_alloc_smi_msg();
	if (!smi_msg)
		/*
		 * If we can't allocate the message, then just return, we
		 * get 4 retries, so this should be ok.
		 */
		return NULL;

	memcpy(smi_msg->data, recv_msg->msg.data, recv_msg->msg.data_len);
	smi_msg->data_size = recv_msg->msg.data_len;
	smi_msg->msgid = STORE_SEQ_IN_MSGID(seq, seqid);

	ipmi_debug_msg("Resend: ", smi_msg->data, smi_msg->data_size);

	return smi_msg;
}

static void check_msg_timeout(struct ipmi_smi *intf, struct seq_table *ent,
			      struct list_head *timeouts,
			      unsigned long timeout_period,
			      int slot, unsigned long *flags,
			      unsigned int *waiting_msgs)
{
	struct ipmi_recv_msg *msg;

	if (intf->in_shutdown)
		return;

	if (!ent->inuse)
		return;

	if (timeout_period < ent->timeout) {
		ent->timeout -= timeout_period;
		(*waiting_msgs)++;
		return;
	}

	if (ent->retries_left == 0) {
		/* The message has used all its retries. */
		ent->inuse = 0;
		msg = ent->recv_msg;
		list_add_tail(&msg->link, timeouts);
		if (ent->broadcast)
			ipmi_inc_stat(intf, timed_out_ipmb_broadcasts);
		else if (is_lan_addr(&ent->recv_msg->addr))
			ipmi_inc_stat(intf, timed_out_lan_commands);
		else
			ipmi_inc_stat(intf, timed_out_ipmb_commands);
	} else {
		struct ipmi_smi_msg *smi_msg;
		/* More retries, send again. */

		(*waiting_msgs)++;

		/*
		 * Start with the max timer, set to normal timer after
		 * the message is sent.
		 */
		ent->timeout = MAX_MSG_TIMEOUT;
		ent->retries_left--;
		smi_msg = smi_from_recv_msg(intf, ent->recv_msg, slot,
					    ent->seqid);
		if (!smi_msg) {
			if (is_lan_addr(&ent->recv_msg->addr))
				ipmi_inc_stat(intf,
					      dropped_rexmit_lan_commands);
			else
				ipmi_inc_stat(intf,
					      dropped_rexmit_ipmb_commands);
			return;
		}

		spin_unlock_irqrestore(&intf->seq_lock, *flags);

		/*
		 * Send the new message.  We send with a zero
		 * priority.  It timed out, I doubt time is that
		 * critical now, and high priority messages are really
		 * only for messages to the local MC, which don't get
		 * resent.
		 */
		if (intf->handlers) {
			if (is_lan_addr(&ent->recv_msg->addr))
				ipmi_inc_stat(intf,
					      retransmitted_lan_commands);
			else
				ipmi_inc_stat(intf,
					      retransmitted_ipmb_commands);

			smi_send(intf, intf->handlers, smi_msg, 0);
		} else
			ipmi_free_smi_msg(smi_msg);

		spin_lock_irqsave(&intf->seq_lock, *flags);
	}
}

static unsigned int ipmi_timeout_handler(struct ipmi_smi *intf,
					 unsigned long timeout_period)
{
	struct list_head     timeouts;
	struct ipmi_recv_msg *msg, *msg2;
	unsigned long        flags;
	int                  i;
	unsigned int         waiting_msgs = 0;

	if (!intf->bmc_registered) {
		kref_get(&intf->refcount);
		if (!schedule_work(&intf->bmc_reg_work)) {
			kref_put(&intf->refcount, intf_free);
			waiting_msgs++;
		}
	}

	/*
	 * Go through the seq table and find any messages that
	 * have timed out, putting them in the timeouts
	 * list.
	 */
	INIT_LIST_HEAD(&timeouts);
	spin_lock_irqsave(&intf->seq_lock, flags);
	if (intf->ipmb_maintenance_mode_timeout) {
		if (intf->ipmb_maintenance_mode_timeout <= timeout_period)
			intf->ipmb_maintenance_mode_timeout = 0;
		else
			intf->ipmb_maintenance_mode_timeout -= timeout_period;
	}
	for (i = 0; i < IPMI_IPMB_NUM_SEQ; i++)
		check_msg_timeout(intf, &intf->seq_table[i],
				  &timeouts, timeout_period, i,
				  &flags, &waiting_msgs);
	spin_unlock_irqrestore(&intf->seq_lock, flags);

	list_for_each_entry_safe(msg, msg2, &timeouts, link)
		deliver_err_response(intf, msg, IPMI_TIMEOUT_COMPLETION_CODE);

	/*
	 * Maintenance mode handling.  Check the timeout
	 * optimistically before we claim the lock.  It may
	 * mean a timeout gets missed occasionally, but that
	 * only means the timeout gets extended by one period
	 * in that case.  No big deal, and it avoids the lock
	 * most of the time.
	 */
	if (intf->auto_maintenance_timeout > 0) {
		spin_lock_irqsave(&intf->maintenance_mode_lock, flags);
		if (intf->auto_maintenance_timeout > 0) {
			intf->auto_maintenance_timeout
				-= timeout_period;
			if (!intf->maintenance_mode
			    && (intf->auto_maintenance_timeout <= 0)) {
				intf->maintenance_mode_enable = false;
				maintenance_mode_update(intf);
			}
		}
		spin_unlock_irqrestore(&intf->maintenance_mode_lock,
				       flags);
	}

	tasklet_schedule(&intf->recv_tasklet);

	return waiting_msgs;
}

static void ipmi_request_event(struct ipmi_smi *intf)
{
	/* No event requests when in maintenance mode. */
	if (intf->maintenance_mode_enable)
		return;

	if (!intf->in_shutdown)
		intf->handlers->request_events(intf->send_info);
}

static struct timer_list ipmi_timer;

static atomic_t stop_operation;

static void ipmi_timeout(struct timer_list *unused)
{
	struct ipmi_smi *intf;
	int nt = 0, index;

	if (atomic_read(&stop_operation))
		return;

	index = srcu_read_lock(&ipmi_interfaces_srcu);
	list_for_each_entry_rcu(intf, &ipmi_interfaces, link) {
		int lnt = 0;

		if (atomic_read(&intf->event_waiters)) {
			intf->ticks_to_req_ev--;
			if (intf->ticks_to_req_ev == 0) {
				ipmi_request_event(intf);
				intf->ticks_to_req_ev = IPMI_REQUEST_EV_TIME;
			}
			lnt++;
		}

		lnt += ipmi_timeout_handler(intf, IPMI_TIMEOUT_TIME);

		lnt = !!lnt;
		if (lnt != intf->last_needs_timer &&
					intf->handlers->set_need_watch)
			intf->handlers->set_need_watch(intf->send_info, lnt);
		intf->last_needs_timer = lnt;

		nt += lnt;
	}
	srcu_read_unlock(&ipmi_interfaces_srcu, index);

	if (nt)
		mod_timer(&ipmi_timer, jiffies + IPMI_TIMEOUT_JIFFIES);
}

static void need_waiter(struct ipmi_smi *intf)
{
	/* Racy, but worst case we start the timer twice. */
	if (!timer_pending(&ipmi_timer))
		mod_timer(&ipmi_timer, jiffies + IPMI_TIMEOUT_JIFFIES);
}

static atomic_t smi_msg_inuse_count = ATOMIC_INIT(0);
static atomic_t recv_msg_inuse_count = ATOMIC_INIT(0);

static void free_smi_msg(struct ipmi_smi_msg *msg)
{
	atomic_dec(&smi_msg_inuse_count);
	kfree(msg);
}

struct ipmi_smi_msg *ipmi_alloc_smi_msg(void)
{
	struct ipmi_smi_msg *rv;
	rv = kmalloc(sizeof(struct ipmi_smi_msg), GFP_ATOMIC);
	if (rv) {
		rv->done = free_smi_msg;
		rv->user_data = NULL;
		atomic_inc(&smi_msg_inuse_count);
	}
	return rv;
}
EXPORT_SYMBOL(ipmi_alloc_smi_msg);

static void free_recv_msg(struct ipmi_recv_msg *msg)
{
	atomic_dec(&recv_msg_inuse_count);
	kfree(msg);
}

static struct ipmi_recv_msg *ipmi_alloc_recv_msg(void)
{
	struct ipmi_recv_msg *rv;

	rv = kmalloc(sizeof(struct ipmi_recv_msg), GFP_ATOMIC);
	if (rv) {
		rv->user = NULL;
		rv->done = free_recv_msg;
		atomic_inc(&recv_msg_inuse_count);
	}
	return rv;
}

void ipmi_free_recv_msg(struct ipmi_recv_msg *msg)
{
	if (msg->user)
		kref_put(&msg->user->refcount, free_user);
	msg->done(msg);
}
EXPORT_SYMBOL(ipmi_free_recv_msg);

static atomic_t panic_done_count = ATOMIC_INIT(0);

static void dummy_smi_done_handler(struct ipmi_smi_msg *msg)
{
	atomic_dec(&panic_done_count);
}

static void dummy_recv_done_handler(struct ipmi_recv_msg *msg)
{
	atomic_dec(&panic_done_count);
}

/*
 * Inside a panic, send a message and wait for a response.
 */
static void ipmi_panic_request_and_wait(struct ipmi_smi *intf,
					struct ipmi_addr *addr,
					struct kernel_ipmi_msg *msg)
{
	struct ipmi_smi_msg  smi_msg;
	struct ipmi_recv_msg recv_msg;
	int rv;

	smi_msg.done = dummy_smi_done_handler;
	recv_msg.done = dummy_recv_done_handler;
	atomic_add(2, &panic_done_count);
	rv = i_ipmi_request(NULL,
			    intf,
			    addr,
			    0,
			    msg,
			    intf,
			    &smi_msg,
			    &recv_msg,
			    0,
			    intf->addrinfo[0].address,
			    intf->addrinfo[0].lun,
			    0, 1); /* Don't retry, and don't wait. */
	if (rv)
		atomic_sub(2, &panic_done_count);
	else if (intf->handlers->flush_messages)
		intf->handlers->flush_messages(intf->send_info);

	while (atomic_read(&panic_done_count) != 0)
		ipmi_poll(intf);
}

static void event_receiver_fetcher(struct ipmi_smi *intf,
				   struct ipmi_recv_msg *msg)
{
	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_SENSOR_EVENT_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_EVENT_RECEIVER_CMD)
	    && (msg->msg.data[0] == IPMI_CC_NO_ERROR)) {
		/* A get event receiver command, save it. */
		intf->event_receiver = msg->msg.data[1];
		intf->event_receiver_lun = msg->msg.data[2] & 0x3;
	}
}

static void device_id_fetcher(struct ipmi_smi *intf, struct ipmi_recv_msg *msg)
{
	if ((msg->addr.addr_type == IPMI_SYSTEM_INTERFACE_ADDR_TYPE)
	    && (msg->msg.netfn == IPMI_NETFN_APP_RESPONSE)
	    && (msg->msg.cmd == IPMI_GET_DEVICE_ID_CMD)
	    && (msg->msg.data[0] == IPMI_CC_NO_ERROR)) {
		/*
		 * A get device id command, save if we are an event
		 * receiver or generator.
		 */
		intf->local_sel_device = (msg->msg.data[6] >> 2) & 1;
		intf->local_event_generator = (msg->msg.data[6] >> 5) & 1;
	}
}

static void send_panic_events(struct ipmi_smi *intf, char *str)
{
	struct kernel_ipmi_msg msg;
	unsigned char data[16];
	struct ipmi_system_interface_addr *si;
	struct ipmi_addr addr;
	char *p = str;
	struct ipmi_ipmb_addr *ipmb;
	int j;

	if (ipmi_send_panic_event == IPMI_SEND_PANIC_EVENT_NONE)
		return;

	si = (struct ipmi_system_interface_addr *) &addr;
	si->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	si->channel = IPMI_BMC_CHANNEL;
	si->lun = 0;

	/* Fill in an event telling that we have failed. */
	msg.netfn = 0x04; /* Sensor or Event. */
	msg.cmd = 2; /* Platform event command. */
	msg.data = data;
	msg.data_len = 8;
	data[0] = 0x41; /* Kernel generator ID, IPMI table 5-4 */
	data[1] = 0x03; /* This is for IPMI 1.0. */
	data[2] = 0x20; /* OS Critical Stop, IPMI table 36-3 */
	data[4] = 0x6f; /* Sensor specific, IPMI table 36-1 */
	data[5] = 0xa1; /* Runtime stop OEM bytes 2 & 3. */

	/*
	 * Put a few breadcrumbs in.  Hopefully later we can add more things
	 * to make the panic events more useful.
	 */
	if (str) {
		data[3] = str[0];
		data[6] = str[1];
		data[7] = str[2];
	}

	/* Send the event announcing the panic. */
	ipmi_panic_request_and_wait(intf, &addr, &msg);

	/*
	 * On every interface, dump a bunch of OEM event holding the
	 * string.
	 */
	if (ipmi_send_panic_event != IPMI_SEND_PANIC_EVENT_STRING || !str)
		return;

	/*
	 * intf_num is used as an marker to tell if the
	 * interface is valid.  Thus we need a read barrier to
	 * make sure data fetched before checking intf_num
	 * won't be used.
	 */
	smp_rmb();

	/*
	 * First job here is to figure out where to send the
	 * OEM events.  There's no way in IPMI to send OEM
	 * events using an event send command, so we have to
	 * find the SEL to put them in and stick them in
	 * there.
	 */

	/* Get capabilities from the get device id. */
	intf->local_sel_device = 0;
	intf->local_event_generator = 0;
	intf->event_receiver = 0;

	/* Request the device info from the local MC. */
	msg.netfn = IPMI_NETFN_APP_REQUEST;
	msg.cmd = IPMI_GET_DEVICE_ID_CMD;
	msg.data = NULL;
	msg.data_len = 0;
	intf->null_user_handler = device_id_fetcher;
	ipmi_panic_request_and_wait(intf, &addr, &msg);

	if (intf->local_event_generator) {
		/* Request the event receiver from the local MC. */
		msg.netfn = IPMI_NETFN_SENSOR_EVENT_REQUEST;
		msg.cmd = IPMI_GET_EVENT_RECEIVER_CMD;
		msg.data = NULL;
		msg.data_len = 0;
		intf->null_user_handler = event_receiver_fetcher;
		ipmi_panic_request_and_wait(intf, &addr, &msg);
	}
	intf->null_user_handler = NULL;

	/*
	 * Validate the event receiver.  The low bit must not
	 * be 1 (it must be a valid IPMB address), it cannot
	 * be zero, and it must not be my address.
	 */
	if (((intf->event_receiver & 1) == 0)
	    && (intf->event_receiver != 0)
	    && (intf->event_receiver != intf->addrinfo[0].address)) {
		/*
		 * The event receiver is valid, send an IPMB
		 * message.
		 */
		ipmb = (struct ipmi_ipmb_addr *) &addr;
		ipmb->addr_type = IPMI_IPMB_ADDR_TYPE;
		ipmb->channel = 0; /* FIXME - is this right? */
		ipmb->lun = intf->event_receiver_lun;
		ipmb->slave_addr = intf->event_receiver;
	} else if (intf->local_sel_device) {
		/*
		 * The event receiver was not valid (or was
		 * me), but I am an SEL device, just dump it
		 * in my SEL.
		 */
		si = (struct ipmi_system_interface_addr *) &addr;
		si->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
		si->channel = IPMI_BMC_CHANNEL;
		si->lun = 0;
	} else
		return; /* No where to send the event. */

	msg.netfn = IPMI_NETFN_STORAGE_REQUEST; /* Storage. */
	msg.cmd = IPMI_ADD_SEL_ENTRY_CMD;
	msg.data = data;
	msg.data_len = 16;

	j = 0;
	while (*p) {
		int size = strlen(p);

		if (size > 11)
			size = 11;
		data[0] = 0;
		data[1] = 0;
		data[2] = 0xf0; /* OEM event without timestamp. */
		data[3] = intf->addrinfo[0].address;
		data[4] = j++; /* sequence # */
		/*
		 * Always give 11 bytes, so strncpy will fill
		 * it with zeroes for me.
		 */
		strncpy(data+5, p, 11);
		p += size;

		ipmi_panic_request_and_wait(intf, &addr, &msg);
	}
}

static int has_panicked;

static int panic_event(struct notifier_block *this,
		       unsigned long         event,
		       void                  *ptr)
{
	struct ipmi_smi *intf;
	struct ipmi_user *user;

	if (has_panicked)
		return NOTIFY_DONE;
	has_panicked = 1;

	/* For every registered interface, set it to run to completion. */
	list_for_each_entry_rcu(intf, &ipmi_interfaces, link) {
		if (!intf->handlers || intf->intf_num == -1)
			/* Interface is not ready. */
			continue;

		if (!intf->handlers->poll)
			continue;

		/*
		 * If we were interrupted while locking xmit_msgs_lock or
		 * waiting_rcv_msgs_lock, the corresponding list may be
		 * corrupted.  In this case, drop items on the list for
		 * the safety.
		 */
		if (!spin_trylock(&intf->xmit_msgs_lock)) {
			INIT_LIST_HEAD(&intf->xmit_msgs);
			INIT_LIST_HEAD(&intf->hp_xmit_msgs);
		} else
			spin_unlock(&intf->xmit_msgs_lock);

		if (!spin_trylock(&intf->waiting_rcv_msgs_lock))
			INIT_LIST_HEAD(&intf->waiting_rcv_msgs);
		else
			spin_unlock(&intf->waiting_rcv_msgs_lock);

		intf->run_to_completion = 1;
		if (intf->handlers->set_run_to_completion)
			intf->handlers->set_run_to_completion(intf->send_info,
							      1);

		list_for_each_entry_rcu(user, &intf->users, link) {
			if (user->handler->ipmi_panic_handler)
				user->handler->ipmi_panic_handler(
					user->handler_data);
		}

		send_panic_events(intf, ptr);
	}

	return NOTIFY_DONE;
}

/* Must be called with ipmi_interfaces_mutex held. */
static int ipmi_register_driver(void)
{
	int rv;

	if (drvregistered)
		return 0;

	rv = driver_register(&ipmidriver.driver);
	if (rv)
		pr_err("Could not register IPMI driver\n");
	else
		drvregistered = true;
	return rv;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
	.next		= NULL,
	.priority	= 200	/* priority: INT_MAX >= x >= 0 */
};

static int ipmi_init_msghandler(void)
{
	int rv;

	mutex_lock(&ipmi_interfaces_mutex);
	rv = ipmi_register_driver();
	if (rv)
		goto out;
	if (initialized)
		goto out;

	init_srcu_struct(&ipmi_interfaces_srcu);

	timer_setup(&ipmi_timer, ipmi_timeout, 0);
	mod_timer(&ipmi_timer, jiffies + IPMI_TIMEOUT_JIFFIES);

	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	initialized = true;

out:
	mutex_unlock(&ipmi_interfaces_mutex);
	return rv;
}

static int __init ipmi_init_msghandler_mod(void)
{
	int rv;

	pr_info("version " IPMI_DRIVER_VERSION "\n");

	mutex_lock(&ipmi_interfaces_mutex);
	rv = ipmi_register_driver();
	mutex_unlock(&ipmi_interfaces_mutex);

	return rv;
}

static void __exit cleanup_ipmi(void)
{
	int count;

	if (initialized) {
		atomic_notifier_chain_unregister(&panic_notifier_list,
						 &panic_block);

		/*
		 * This can't be called if any interfaces exist, so no worry
		 * about shutting down the interfaces.
		 */

		/*
		 * Tell the timer to stop, then wait for it to stop.  This
		 * avoids problems with race conditions removing the timer
		 * here.
		 */
		atomic_inc(&stop_operation);
		del_timer_sync(&ipmi_timer);

		initialized = false;

		/* Check for buffer leaks. */
		count = atomic_read(&smi_msg_inuse_count);
		if (count != 0)
			pr_warn(PFX "SMI message count %d at exit\n", count);
		count = atomic_read(&recv_msg_inuse_count);
		if (count != 0)
			pr_warn(PFX "recv message count %d at exit\n", count);
		cleanup_srcu_struct(&ipmi_interfaces_srcu);
	}
	if (drvregistered)
		driver_unregister(&ipmidriver.driver);
}
module_exit(cleanup_ipmi);

module_init(ipmi_init_msghandler_mod);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Incoming and outgoing message routing for an IPMI"
		   " interface.");
MODULE_VERSION(IPMI_DRIVER_VERSION);
MODULE_SOFTDEP("post: ipmi_devintf");
