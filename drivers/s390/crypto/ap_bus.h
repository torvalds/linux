/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright IBM Corp. 2006, 2019
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Ralph Wuerthner <rwuerthn@de.ibm.com>
 *	      Felix Beck <felix.beck@de.ibm.com>
 *	      Holger Dengler <hd@linux.vnet.ibm.com>
 *
 * Adjunct processor bus header file.
 */

#ifndef _AP_BUS_H_
#define _AP_BUS_H_

#include <linux/device.h>
#include <linux/types.h>
#include <linux/hashtable.h>
#include <asm/isc.h>
#include <asm/ap.h>

#define AP_DEVICES 256		/* Number of AP devices. */
#define AP_DOMAINS 256		/* Number of AP domains. */
#define AP_IOCTLS  256		/* Number of ioctls. */
#define AP_RESET_TIMEOUT (HZ*0.7)	/* Time in ticks for reset timeouts. */
#define AP_CONFIG_TIME 30	/* Time in seconds between AP bus rescans. */
#define AP_POLL_TIME 1		/* Time in ticks between receive polls. */
#define AP_DEFAULT_MAX_MSG_SIZE (12 * 1024)
#define AP_TAPQ_ML_FIELD_CHUNK_SIZE (4096)

extern int ap_domain_index;
extern atomic_t ap_max_msg_size;

extern DECLARE_HASHTABLE(ap_queues, 8);
extern spinlock_t ap_queues_lock;

static inline int ap_test_bit(unsigned int *ptr, unsigned int nr)
{
	return (*ptr & (0x80000000u >> nr)) != 0;
}

#define AP_RESPONSE_NORMAL		0x00
#define AP_RESPONSE_Q_NOT_AVAIL		0x01
#define AP_RESPONSE_RESET_IN_PROGRESS	0x02
#define AP_RESPONSE_DECONFIGURED	0x03
#define AP_RESPONSE_CHECKSTOPPED	0x04
#define AP_RESPONSE_BUSY		0x05
#define AP_RESPONSE_INVALID_ADDRESS	0x06
#define AP_RESPONSE_OTHERWISE_CHANGED	0x07
#define AP_RESPONSE_Q_FULL		0x10
#define AP_RESPONSE_NO_PENDING_REPLY	0x10
#define AP_RESPONSE_INDEX_TOO_BIG	0x11
#define AP_RESPONSE_NO_FIRST_PART	0x13
#define AP_RESPONSE_MESSAGE_TOO_BIG	0x15
#define AP_RESPONSE_REQ_FAC_NOT_INST	0x16
#define AP_RESPONSE_INVALID_DOMAIN	0x42

/*
 * Known device types
 */
#define AP_DEVICE_TYPE_PCICC	3
#define AP_DEVICE_TYPE_PCICA	4
#define AP_DEVICE_TYPE_PCIXCC	5
#define AP_DEVICE_TYPE_CEX2A	6
#define AP_DEVICE_TYPE_CEX2C	7
#define AP_DEVICE_TYPE_CEX3A	8
#define AP_DEVICE_TYPE_CEX3C	9
#define AP_DEVICE_TYPE_CEX4	10
#define AP_DEVICE_TYPE_CEX5	11
#define AP_DEVICE_TYPE_CEX6	12
#define AP_DEVICE_TYPE_CEX7	13

/*
 * Known function facilities
 */
#define AP_FUNC_MEX4K 1
#define AP_FUNC_CRT4K 2
#define AP_FUNC_COPRO 3
#define AP_FUNC_ACCEL 4
#define AP_FUNC_EP11  5
#define AP_FUNC_APXA  6

/*
 * AP interrupt states
 */
#define AP_INTR_DISABLED	0	/* AP interrupt disabled */
#define AP_INTR_ENABLED		1	/* AP interrupt enabled */

/*
 * AP queue state machine states
 */
enum ap_sm_state {
	AP_SM_STATE_RESET_START = 0,
	AP_SM_STATE_RESET_WAIT,
	AP_SM_STATE_SETIRQ_WAIT,
	AP_SM_STATE_IDLE,
	AP_SM_STATE_WORKING,
	AP_SM_STATE_QUEUE_FULL,
	NR_AP_SM_STATES
};

/*
 * AP queue state machine events
 */
enum ap_sm_event {
	AP_SM_EVENT_POLL,
	AP_SM_EVENT_TIMEOUT,
	NR_AP_SM_EVENTS
};

/*
 * AP queue state wait behaviour
 */
enum ap_sm_wait {
	AP_SM_WAIT_AGAIN,	/* retry immediately */
	AP_SM_WAIT_TIMEOUT,	/* wait for timeout */
	AP_SM_WAIT_INTERRUPT,	/* wait for thin interrupt (if available) */
	AP_SM_WAIT_NONE,	/* no wait */
	NR_AP_SM_WAIT
};

/*
 * AP queue device states
 */
enum ap_dev_state {
	AP_DEV_STATE_UNINITIATED = 0,	/* fresh and virgin, not touched */
	AP_DEV_STATE_OPERATING,		/* queue dev is working normal */
	AP_DEV_STATE_SHUTDOWN,		/* remove/unbind/shutdown in progress */
	AP_DEV_STATE_ERROR,		/* device is in error state */
	NR_AP_DEV_STATES
};

struct ap_device;
struct ap_message;

/*
 * The ap driver struct includes a flags field which holds some info for
 * the ap bus about the driver. Currently only one flag is supported and
 * used: The DEFAULT flag marks an ap driver as a default driver which is
 * used together with the apmask and aqmask whitelisting of the ap bus.
 */
#define AP_DRIVER_FLAG_DEFAULT 0x0001

struct ap_driver {
	struct device_driver driver;
	struct ap_device_id *ids;
	unsigned int flags;

	int (*probe)(struct ap_device *);
	void (*remove)(struct ap_device *);
};

#define to_ap_drv(x) container_of((x), struct ap_driver, driver)

int ap_driver_register(struct ap_driver *, struct module *, char *);
void ap_driver_unregister(struct ap_driver *);

struct ap_device {
	struct device device;
	struct ap_driver *drv;		/* Pointer to AP device driver. */
	int device_type;		/* AP device type. */
};

#define to_ap_dev(x) container_of((x), struct ap_device, device)

struct ap_card {
	struct ap_device ap_dev;
	void *private;			/* ap driver private pointer. */
	int raw_hwtype;			/* AP raw hardware type. */
	unsigned int functions;		/* AP device function bitfield. */
	int queue_depth;		/* AP queue depth.*/
	int id;				/* AP card number. */
	unsigned int maxmsgsize;	/* AP msg limit for this card */
	bool config;			/* configured state */
	atomic64_t total_request_count;	/* # requests ever for this AP device.*/
};

#define to_ap_card(x) container_of((x), struct ap_card, ap_dev.device)

struct ap_queue {
	struct ap_device ap_dev;
	struct hlist_node hnode;	/* Node for the ap_queues hashtable */
	struct ap_card *card;		/* Ptr to assoc. AP card. */
	spinlock_t lock;		/* Per device lock. */
	void *private;			/* ap driver private pointer. */
	enum ap_dev_state dev_state;	/* queue device state */
	bool config;			/* configured state */
	ap_qid_t qid;			/* AP queue id. */
	int interrupt;			/* indicate if interrupts are enabled */
	int queue_count;		/* # messages currently on AP queue. */
	int pendingq_count;		/* # requests on pendingq list. */
	int requestq_count;		/* # requests on requestq list. */
	u64 total_request_count;	/* # requests ever for this AP device.*/
	int request_timeout;		/* Request timeout in jiffies. */
	struct timer_list timeout;	/* Timer for request timeouts. */
	struct list_head pendingq;	/* List of message sent to AP queue. */
	struct list_head requestq;	/* List of message yet to be sent. */
	struct ap_message *reply;	/* Per device reply message. */
	enum ap_sm_state sm_state;	/* ap queue state machine state */
	int last_err_rc;		/* last error state response code */
};

#define to_ap_queue(x) container_of((x), struct ap_queue, ap_dev.device)

typedef enum ap_sm_wait (ap_func_t)(struct ap_queue *queue);

/* failure injection cmd struct */
struct ap_fi {
	union {
		u16 cmd;		/* fi flags + action */
		struct {
			u8 flags;	/* fi flags only */
			u8 action;	/* fi action only */
		};
	};
};

/* all currently known fi actions */
enum ap_fi_actions {
	AP_FI_ACTION_CCA_AGENT_FF   = 0x01,
	AP_FI_ACTION_CCA_DOM_INVAL  = 0x02,
	AP_FI_ACTION_NQAP_QID_INVAL = 0x03,
};

/* all currently known fi flags */
enum ap_fi_flags {
	AP_FI_FLAG_NO_RETRY	  = 0x01,
	AP_FI_FLAG_TOGGLE_SPECIAL = 0x02,
};

struct ap_message {
	struct list_head list;		/* Request queueing. */
	unsigned long long psmid;	/* Message id. */
	void *msg;			/* Pointer to message buffer. */
	unsigned int len;		/* actual msg len in msg buffer */
	unsigned int bufsize;		/* allocated msg buffer size */
	u16 flags;			/* Flags, see AP_MSG_FLAG_xxx */
	struct ap_fi fi;		/* Failure Injection cmd */
	int rc;				/* Return code for this message */
	void *private;			/* ap driver private pointer. */
	/* receive is called from tasklet context */
	void (*receive)(struct ap_queue *, struct ap_message *,
			struct ap_message *);
};

#define AP_MSG_FLAG_SPECIAL  1		/* flag msg as 'special' with NQAP */

/**
 * ap_init_message() - Initialize ap_message.
 * Initialize a message before using. Otherwise this might result in
 * unexpected behaviour.
 */
static inline void ap_init_message(struct ap_message *ap_msg)
{
	memset(ap_msg, 0, sizeof(*ap_msg));
}

/**
 * ap_release_message() - Release ap_message.
 * Releases all memory used internal within the ap_message struct
 * Currently this is the message and private field.
 */
static inline void ap_release_message(struct ap_message *ap_msg)
{
	kfree_sensitive(ap_msg->msg);
	kfree_sensitive(ap_msg->private);
}

/*
 * Note: don't use ap_send/ap_recv after using ap_queue_message
 * for the first time. Otherwise the ap message queue will get
 * confused.
 */
int ap_send(ap_qid_t, unsigned long long, void *, size_t);
int ap_recv(ap_qid_t, unsigned long long *, void *, size_t);

enum ap_sm_wait ap_sm_event(struct ap_queue *aq, enum ap_sm_event event);
enum ap_sm_wait ap_sm_event_loop(struct ap_queue *aq, enum ap_sm_event event);

int ap_queue_message(struct ap_queue *aq, struct ap_message *ap_msg);
void ap_cancel_message(struct ap_queue *aq, struct ap_message *ap_msg);
void ap_flush_queue(struct ap_queue *aq);

void *ap_airq_ptr(void);
void ap_wait(enum ap_sm_wait wait);
void ap_request_timeout(struct timer_list *t);
void ap_bus_force_rescan(void);

int ap_test_config_usage_domain(unsigned int domain);
int ap_test_config_ctrl_domain(unsigned int domain);

void ap_queue_init_reply(struct ap_queue *aq, struct ap_message *ap_msg);
struct ap_queue *ap_queue_create(ap_qid_t qid, int device_type);
void ap_queue_prepare_remove(struct ap_queue *aq);
void ap_queue_remove(struct ap_queue *aq);
void ap_queue_init_state(struct ap_queue *aq);

struct ap_card *ap_card_create(int id, int queue_depth, int raw_type,
			       int comp_type, unsigned int functions, int ml);

struct ap_perms {
	unsigned long ioctlm[BITS_TO_LONGS(AP_IOCTLS)];
	unsigned long apm[BITS_TO_LONGS(AP_DEVICES)];
	unsigned long aqm[BITS_TO_LONGS(AP_DOMAINS)];
};
extern struct ap_perms ap_perms;
extern struct mutex ap_perms_mutex;

/*
 * Get ap_queue device for this qid.
 * Returns ptr to the struct ap_queue device or NULL if there
 * was no ap_queue device with this qid found. When something is
 * found, the reference count of the embedded device is increased.
 * So the caller has to decrease the reference count after use
 * with a call to put_device(&aq->ap_dev.device).
 */
struct ap_queue *ap_get_qdev(ap_qid_t qid);

/*
 * check APQN for owned/reserved by ap bus and default driver(s).
 * Checks if this APQN is or will be in use by the ap bus
 * and the default set of drivers.
 * If yes, returns 1, if not returns 0. On error a negative
 * errno value is returned.
 */
int ap_owned_by_def_drv(int card, int queue);

/*
 * check 'matrix' of APQNs for owned/reserved by ap bus and
 * default driver(s).
 * Checks if there is at least one APQN in the given 'matrix'
 * marked as owned/reserved by the ap bus and default driver(s).
 * If such an APQN is found the return value is 1, otherwise
 * 0 is returned. On error a negative errno value is returned.
 * The parameter apm is a bitmask which should be declared
 * as DECLARE_BITMAP(apm, AP_DEVICES), the aqm parameter is
 * similar, should be declared as DECLARE_BITMAP(aqm, AP_DOMAINS).
 */
int ap_apqn_in_matrix_owned_by_def_drv(unsigned long *apm,
				       unsigned long *aqm);

/*
 * ap_parse_mask_str() - helper function to parse a bitmap string
 * and clear/set the bits in the bitmap accordingly. The string may be
 * given as absolute value, a hex string like 0x1F2E3D4C5B6A" simple
 * overwriting the current content of the bitmap. Or as relative string
 * like "+1-16,-32,-0x40,+128" where only single bits or ranges of
 * bits are cleared or set. Distinction is done based on the very
 * first character which may be '+' or '-' for the relative string
 * and othewise assume to be an absolute value string. If parsing fails
 * a negative errno value is returned. All arguments and bitmaps are
 * big endian order.
 */
int ap_parse_mask_str(const char *str,
		      unsigned long *bitmap, int bits,
		      struct mutex *lock);

/*
 * Interface to wait for the AP bus to have done one initial ap bus
 * scan and all detected APQNs have been bound to device drivers.
 * If these both conditions are not fulfilled, this function blocks
 * on a condition with wait_for_completion_killable_timeout().
 * If these both conditions are fulfilled (before the timeout hits)
 * the return value is 0. If the timeout (in jiffies) hits instead
 * -ETIME is returned. On failures negative return values are
 * returned to the caller.
 */
int ap_wait_init_apqn_bindings_complete(unsigned long timeout);

void ap_send_config_uevent(struct ap_device *ap_dev, bool cfg);
void ap_send_online_uevent(struct ap_device *ap_dev, int online);

#endif /* _AP_BUS_H_ */
