/*
 * Copyright IBM Corp. 2006, 2012
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Ralph Wuerthner <rwuerthn@de.ibm.com>
 *	      Felix Beck <felix.beck@de.ibm.com>
 *	      Holger Dengler <hd@linux.vnet.ibm.com>
 *
 * Adjunct processor bus header file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AP_BUS_H_
#define _AP_BUS_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/types.h>

#define AP_DEVICES 64		/* Number of AP devices. */
#define AP_DOMAINS 256		/* Number of AP domains. */
#define AP_RESET_TIMEOUT (HZ*0.7)	/* Time in ticks for reset timeouts. */
#define AP_CONFIG_TIME 30	/* Time in seconds between AP bus rescans. */
#define AP_POLL_TIME 1		/* Time in ticks between receive polls. */

extern int ap_domain_index;

/**
 * The ap_qid_t identifier of an ap queue. It contains a
 * 6 bit device index and a 4 bit queue index (domain).
 */
typedef unsigned int ap_qid_t;

#define AP_MKQID(_device, _queue) (((_device) & 63) << 8 | ((_queue) & 255))
#define AP_QID_DEVICE(_qid) (((_qid) >> 8) & 63)
#define AP_QID_QUEUE(_qid) ((_qid) & 255)

/**
 * structy ap_queue_status - Holds the AP queue status.
 * @queue_empty: Shows if queue is empty
 * @replies_waiting: Waiting replies
 * @queue_full: Is 1 if the queue is full
 * @pad: A 4 bit pad
 * @int_enabled: Shows if interrupts are enabled for the AP
 * @response_conde: Holds the 8 bit response code
 * @pad2: A 16 bit pad
 *
 * The ap queue status word is returned by all three AP functions
 * (PQAP, NQAP and DQAP).  There's a set of flags in the first
 * byte, followed by a 1 byte response code.
 */
struct ap_queue_status {
	unsigned int queue_empty	: 1;
	unsigned int replies_waiting	: 1;
	unsigned int queue_full		: 1;
	unsigned int pad1		: 4;
	unsigned int int_enabled	: 1;
	unsigned int response_code	: 8;
	unsigned int pad2		: 16;
} __packed;


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
 * AP device states
 */
enum ap_state {
	AP_STATE_RESET_START,
	AP_STATE_RESET_WAIT,
	AP_STATE_SETIRQ_WAIT,
	AP_STATE_IDLE,
	AP_STATE_WORKING,
	AP_STATE_QUEUE_FULL,
	AP_STATE_SUSPEND_WAIT,
	AP_STATE_BORKED,
	NR_AP_STATES
};

/*
 * AP device events
 */
enum ap_event {
	AP_EVENT_POLL,
	AP_EVENT_TIMEOUT,
	NR_AP_EVENTS
};

/*
 * AP wait behaviour
 */
enum ap_wait {
	AP_WAIT_AGAIN,		/* retry immediately */
	AP_WAIT_TIMEOUT,	/* wait for timeout */
	AP_WAIT_INTERRUPT,	/* wait for thin interrupt (if available) */
	AP_WAIT_NONE,		/* no wait */
	NR_AP_WAIT
};

struct ap_device;
struct ap_message;

struct ap_driver {
	struct device_driver driver;
	struct ap_device_id *ids;

	int (*probe)(struct ap_device *);
	void (*remove)(struct ap_device *);
	int request_timeout;		/* request timeout in jiffies */
};

#define to_ap_drv(x) container_of((x), struct ap_driver, driver)

int ap_driver_register(struct ap_driver *, struct module *, char *);
void ap_driver_unregister(struct ap_driver *);

typedef enum ap_wait (ap_func_t)(struct ap_device *ap_dev);

struct ap_device {
	struct device device;
	struct ap_driver *drv;		/* Pointer to AP device driver. */
	spinlock_t lock;		/* Per device lock. */
	struct list_head list;		/* private list of all AP devices. */

	enum ap_state state;		/* State of the AP device. */

	ap_qid_t qid;			/* AP queue id. */
	int queue_depth;		/* AP queue depth.*/
	int device_type;		/* AP device type. */
	int raw_hwtype;			/* AP raw hardware type. */
	unsigned int functions;		/* AP device function bitfield. */
	struct timer_list timeout;	/* Timer for request timeouts. */

	int interrupt;			/* indicate if interrupts are enabled */
	int queue_count;		/* # messages currently on AP queue. */

	struct list_head pendingq;	/* List of message sent to AP queue. */
	int pendingq_count;		/* # requests on pendingq list. */
	struct list_head requestq;	/* List of message yet to be sent. */
	int requestq_count;		/* # requests on requestq list. */
	int total_request_count;	/* # requests ever for this AP device. */

	struct ap_message *reply;	/* Per device reply message. */

	void *private;			/* ap driver private pointer. */
};

#define to_ap_dev(x) container_of((x), struct ap_device, device)

struct ap_message {
	struct list_head list;		/* Request queueing. */
	unsigned long long psmid;	/* Message id. */
	void *message;			/* Pointer to message buffer. */
	size_t length;			/* Message length. */
	int rc;				/* Return code for this message */

	void *private;			/* ap driver private pointer. */
	unsigned int special:1;		/* Used for special commands. */
	/* receive is called from tasklet context */
	void (*receive)(struct ap_device *, struct ap_message *,
			struct ap_message *);
};

struct ap_config_info {
	unsigned int special_command:1;
	unsigned int ap_extended:1;
	unsigned char reserved1:6;
	unsigned char reserved2[15];
	unsigned int apm[8];		/* AP ID mask */
	unsigned int aqm[8];		/* AP queue mask */
	unsigned int adm[8];		/* AP domain mask */
	unsigned char reserved4[16];
} __packed;

#define AP_DEVICE(dt)					\
	.dev_type=(dt),					\
	.match_flags=AP_DEVICE_ID_MATCH_DEVICE_TYPE,

/**
 * ap_init_message() - Initialize ap_message.
 * Initialize a message before using. Otherwise this might result in
 * unexpected behaviour.
 */
static inline void ap_init_message(struct ap_message *ap_msg)
{
	ap_msg->psmid = 0;
	ap_msg->length = 0;
	ap_msg->rc = 0;
	ap_msg->special = 0;
	ap_msg->receive = NULL;
}

/*
 * Note: don't use ap_send/ap_recv after using ap_queue_message
 * for the first time. Otherwise the ap message queue will get
 * confused.
 */
int ap_send(ap_qid_t, unsigned long long, void *, size_t);
int ap_recv(ap_qid_t, unsigned long long *, void *, size_t);

void ap_queue_message(struct ap_device *ap_dev, struct ap_message *ap_msg);
void ap_cancel_message(struct ap_device *ap_dev, struct ap_message *ap_msg);
void ap_flush_queue(struct ap_device *ap_dev);
void ap_bus_force_rescan(void);

int ap_module_init(void);
void ap_module_exit(void);

#endif /* _AP_BUS_H_ */
