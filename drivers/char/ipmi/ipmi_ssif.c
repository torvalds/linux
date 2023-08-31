// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_ssif.c
 *
 * The interface to the IPMI driver for SMBus access to a SMBus
 * compliant device.  Called SSIF by the IPMI spec.
 *
 * Author: Intel Corporation
 *         Todd Davis <todd.c.davis@intel.com>
 *
 * Rewritten by Corey Minyard <minyard@acm.org> to support the
 * non-blocking I2C interface, add support for multi-part
 * transactions, add PEC support, and general clenaup.
 *
 * Copyright 2003 Intel Corporation
 * Copyright 2005 MontaVista Software
 */

/*
 * This file holds the "policy" for the interface to the SSIF state
 * machine.  It does the configuration, handles timers and interrupts,
 * and drives the real SSIF state machine.
 */

#define pr_fmt(fmt) "ipmi_ssif: " fmt
#define dev_fmt(fmt) "ipmi_ssif: " fmt

#if defined(MODVERSIONS)
#include <linux/modversions.h>
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/ipmi_smi.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/kthread.h>
#include <linux/acpi.h>
#include <linux/ctype.h>
#include <linux/time64.h>
#include "ipmi_dmi.h"

#define DEVICE_NAME "ipmi_ssif"

#define IPMI_GET_SYSTEM_INTERFACE_CAPABILITIES_CMD	0x57

#define	SSIF_IPMI_REQUEST			2
#define	SSIF_IPMI_MULTI_PART_REQUEST_START	6
#define	SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE	7
#define	SSIF_IPMI_MULTI_PART_REQUEST_END	8
#define	SSIF_IPMI_RESPONSE			3
#define	SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE	9

/* ssif_debug is a bit-field
 *	SSIF_DEBUG_MSG -	commands and their responses
 *	SSIF_DEBUG_STATES -	message states
 *	SSIF_DEBUG_TIMING -	 Measure times between events in the driver
 */
#define SSIF_DEBUG_TIMING	4
#define SSIF_DEBUG_STATE	2
#define SSIF_DEBUG_MSG		1
#define SSIF_NODEBUG		0
#define SSIF_DEFAULT_DEBUG	(SSIF_NODEBUG)

/*
 * Timer values
 */
#define SSIF_MSG_USEC		60000	/* 60ms between message tries (T3). */
#define SSIF_REQ_RETRY_USEC	60000	/* 60ms between send retries (T6). */
#define SSIF_MSG_PART_USEC	5000	/* 5ms for a message part */

/* How many times to we retry sending/receiving the message. */
#define	SSIF_SEND_RETRIES	5
#define	SSIF_RECV_RETRIES	250

#define SSIF_MSG_MSEC		(SSIF_MSG_USEC / 1000)
#define SSIF_REQ_RETRY_MSEC	(SSIF_REQ_RETRY_USEC / 1000)
#define SSIF_MSG_JIFFIES	((SSIF_MSG_USEC * 1000) / TICK_NSEC)
#define SSIF_REQ_RETRY_JIFFIES	((SSIF_REQ_RETRY_USEC * 1000) / TICK_NSEC)
#define SSIF_MSG_PART_JIFFIES	((SSIF_MSG_PART_USEC * 1000) / TICK_NSEC)

/*
 * Timeout for the watch, only used for get flag timer.
 */
#define SSIF_WATCH_MSG_TIMEOUT		msecs_to_jiffies(10)
#define SSIF_WATCH_WATCHDOG_TIMEOUT	msecs_to_jiffies(250)

enum ssif_intf_state {
	SSIF_IDLE,
	SSIF_GETTING_FLAGS,
	SSIF_GETTING_EVENTS,
	SSIF_CLEARING_FLAGS,
	SSIF_GETTING_MESSAGES,
	/* FIXME - add watchdog stuff. */
};

#define IS_SSIF_IDLE(ssif) ((ssif)->ssif_state == SSIF_IDLE \
			    && (ssif)->curr_msg == NULL)

/*
 * Indexes into stats[] in ssif_info below.
 */
enum ssif_stat_indexes {
	/* Number of total messages sent. */
	SSIF_STAT_sent_messages = 0,

	/*
	 * Number of message parts sent.  Messages may be broken into
	 * parts if they are long.
	 */
	SSIF_STAT_sent_messages_parts,

	/*
	 * Number of time a message was retried.
	 */
	SSIF_STAT_send_retries,

	/*
	 * Number of times the send of a message failed.
	 */
	SSIF_STAT_send_errors,

	/*
	 * Number of message responses received.
	 */
	SSIF_STAT_received_messages,

	/*
	 * Number of message fragments received.
	 */
	SSIF_STAT_received_message_parts,

	/*
	 * Number of times the receive of a message was retried.
	 */
	SSIF_STAT_receive_retries,

	/*
	 * Number of errors receiving messages.
	 */
	SSIF_STAT_receive_errors,

	/*
	 * Number of times a flag fetch was requested.
	 */
	SSIF_STAT_flag_fetches,

	/*
	 * Number of times the hardware didn't follow the state machine.
	 */
	SSIF_STAT_hosed,

	/*
	 * Number of received events.
	 */
	SSIF_STAT_events,

	/* Number of asyncronous messages received. */
	SSIF_STAT_incoming_messages,

	/* Number of watchdog pretimeouts. */
	SSIF_STAT_watchdog_pretimeouts,

	/* Number of alers received. */
	SSIF_STAT_alerts,

	/* Always add statistics before this value, it must be last. */
	SSIF_NUM_STATS
};

struct ssif_addr_info {
	struct i2c_board_info binfo;
	char *adapter_name;
	int debug;
	int slave_addr;
	enum ipmi_addr_src addr_src;
	union ipmi_smi_info_union addr_info;
	struct device *dev;
	struct i2c_client *client;

	struct mutex clients_mutex;
	struct list_head clients;

	struct list_head link;
};

struct ssif_info;

typedef void (*ssif_i2c_done)(struct ssif_info *ssif_info, int result,
			     unsigned char *data, unsigned int len);

struct ssif_info {
	struct ipmi_smi     *intf;
	spinlock_t	    lock;
	struct ipmi_smi_msg *waiting_msg;
	struct ipmi_smi_msg *curr_msg;
	enum ssif_intf_state ssif_state;
	unsigned long       ssif_debug;

	struct ipmi_smi_handlers handlers;

	enum ipmi_addr_src addr_source; /* ACPI, PCI, SMBIOS, hardcode, etc. */
	union ipmi_smi_info_union addr_info;

	/*
	 * Flags from the last GET_MSG_FLAGS command, used when an ATTN
	 * is set to hold the flags until we are done handling everything
	 * from the flags.
	 */
#define RECEIVE_MSG_AVAIL	0x01
#define EVENT_MSG_BUFFER_FULL	0x02
#define WDT_PRE_TIMEOUT_INT	0x08
	unsigned char       msg_flags;

	u8		    global_enables;
	bool		    has_event_buffer;
	bool		    supports_alert;

	/*
	 * Used to tell what we should do with alerts.  If we are
	 * waiting on a response, read the data immediately.
	 */
	bool		    got_alert;
	bool		    waiting_alert;

	/* Used to inform the timeout that it should do a resend. */
	bool		    do_resend;

	/*
	 * If set to true, this will request events the next time the
	 * state machine is idle.
	 */
	bool                req_events;

	/*
	 * If set to true, this will request flags the next time the
	 * state machine is idle.
	 */
	bool                req_flags;

	/* Used for sending/receiving data.  +1 for the length. */
	unsigned char data[IPMI_MAX_MSG_LENGTH + 1];
	unsigned int  data_len;

	/* Temp receive buffer, gets copied into data. */
	unsigned char recv[I2C_SMBUS_BLOCK_MAX];

	struct i2c_client *client;
	ssif_i2c_done done_handler;

	/* Thread interface handling */
	struct task_struct *thread;
	struct completion wake_thread;
	bool stopping;
	int i2c_read_write;
	int i2c_command;
	unsigned char *i2c_data;
	unsigned int i2c_size;

	struct timer_list retry_timer;
	int retries_left;

	long watch_timeout;		/* Timeout for flags check, 0 if off. */
	struct timer_list watch_timer;	/* Flag fetch timer. */

	/* Info from SSIF cmd */
	unsigned char max_xmit_msg_size;
	unsigned char max_recv_msg_size;
	bool cmd8_works; /* See test_multipart_messages() for details. */
	unsigned int  multi_support;
	int           supports_pec;

#define SSIF_NO_MULTI		0
#define SSIF_MULTI_2_PART	1
#define SSIF_MULTI_n_PART	2
	unsigned char *multi_data;
	unsigned int  multi_len;
	unsigned int  multi_pos;

	atomic_t stats[SSIF_NUM_STATS];
};

#define ssif_inc_stat(ssif, stat) \
	atomic_inc(&(ssif)->stats[SSIF_STAT_ ## stat])
#define ssif_get_stat(ssif, stat) \
	((unsigned int) atomic_read(&(ssif)->stats[SSIF_STAT_ ## stat]))

static bool initialized;
static bool platform_registered;

static void return_hosed_msg(struct ssif_info *ssif_info,
			     struct ipmi_smi_msg *msg);
static void start_next_msg(struct ssif_info *ssif_info, unsigned long *flags);
static int start_send(struct ssif_info *ssif_info,
		      unsigned char   *data,
		      unsigned int    len);

static unsigned long *ipmi_ssif_lock_cond(struct ssif_info *ssif_info,
					  unsigned long *flags)
	__acquires(&ssif_info->lock)
{
	spin_lock_irqsave(&ssif_info->lock, *flags);
	return flags;
}

static void ipmi_ssif_unlock_cond(struct ssif_info *ssif_info,
				  unsigned long *flags)
	__releases(&ssif_info->lock)
{
	spin_unlock_irqrestore(&ssif_info->lock, *flags);
}

static void deliver_recv_msg(struct ssif_info *ssif_info,
			     struct ipmi_smi_msg *msg)
{
	if (msg->rsp_size < 0) {
		return_hosed_msg(ssif_info, msg);
		dev_err(&ssif_info->client->dev,
			"%s: Malformed message: rsp_size = %d\n",
		       __func__, msg->rsp_size);
	} else {
		ipmi_smi_msg_received(ssif_info->intf, msg);
	}
}

static void return_hosed_msg(struct ssif_info *ssif_info,
			     struct ipmi_smi_msg *msg)
{
	ssif_inc_stat(ssif_info, hosed);

	/* Make it a response */
	msg->rsp[0] = msg->data[0] | 4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = 0xFF; /* Unknown error. */
	msg->rsp_size = 3;

	deliver_recv_msg(ssif_info, msg);
}

/*
 * Must be called with the message lock held.  This will release the
 * message lock.  Note that the caller will check IS_SSIF_IDLE and
 * start a new operation, so there is no need to check for new
 * messages to start in here.
 */
static void start_clear_flags(struct ssif_info *ssif_info, unsigned long *flags)
{
	unsigned char msg[3];

	ssif_info->msg_flags &= ~WDT_PRE_TIMEOUT_INT;
	ssif_info->ssif_state = SSIF_CLEARING_FLAGS;
	ipmi_ssif_unlock_cond(ssif_info, flags);

	/* Make sure the watchdog pre-timeout flag is not set at startup. */
	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_CLEAR_MSG_FLAGS_CMD;
	msg[2] = WDT_PRE_TIMEOUT_INT;

	if (start_send(ssif_info, msg, 3) != 0) {
		/* Error, just go to normal state. */
		ssif_info->ssif_state = SSIF_IDLE;
	}
}

static void start_flag_fetch(struct ssif_info *ssif_info, unsigned long *flags)
{
	unsigned char mb[2];

	ssif_info->req_flags = false;
	ssif_info->ssif_state = SSIF_GETTING_FLAGS;
	ipmi_ssif_unlock_cond(ssif_info, flags);

	mb[0] = (IPMI_NETFN_APP_REQUEST << 2);
	mb[1] = IPMI_GET_MSG_FLAGS_CMD;
	if (start_send(ssif_info, mb, 2) != 0)
		ssif_info->ssif_state = SSIF_IDLE;
}

static void check_start_send(struct ssif_info *ssif_info, unsigned long *flags,
			     struct ipmi_smi_msg *msg)
{
	if (start_send(ssif_info, msg->data, msg->data_size) != 0) {
		unsigned long oflags;

		flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
		ssif_info->curr_msg = NULL;
		ssif_info->ssif_state = SSIF_IDLE;
		ipmi_ssif_unlock_cond(ssif_info, flags);
		ipmi_free_smi_msg(msg);
	}
}

static void start_event_fetch(struct ssif_info *ssif_info, unsigned long *flags)
{
	struct ipmi_smi_msg *msg;

	ssif_info->req_events = false;

	msg = ipmi_alloc_smi_msg();
	if (!msg) {
		ssif_info->ssif_state = SSIF_IDLE;
		ipmi_ssif_unlock_cond(ssif_info, flags);
		return;
	}

	ssif_info->curr_msg = msg;
	ssif_info->ssif_state = SSIF_GETTING_EVENTS;
	ipmi_ssif_unlock_cond(ssif_info, flags);

	msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg->data[1] = IPMI_READ_EVENT_MSG_BUFFER_CMD;
	msg->data_size = 2;

	check_start_send(ssif_info, flags, msg);
}

static void start_recv_msg_fetch(struct ssif_info *ssif_info,
				 unsigned long *flags)
{
	struct ipmi_smi_msg *msg;

	msg = ipmi_alloc_smi_msg();
	if (!msg) {
		ssif_info->ssif_state = SSIF_IDLE;
		ipmi_ssif_unlock_cond(ssif_info, flags);
		return;
	}

	ssif_info->curr_msg = msg;
	ssif_info->ssif_state = SSIF_GETTING_MESSAGES;
	ipmi_ssif_unlock_cond(ssif_info, flags);

	msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg->data[1] = IPMI_GET_MSG_CMD;
	msg->data_size = 2;

	check_start_send(ssif_info, flags, msg);
}

/*
 * Must be called with the message lock held.  This will release the
 * message lock.  Note that the caller will check IS_SSIF_IDLE and
 * start a new operation, so there is no need to check for new
 * messages to start in here.
 */
static void handle_flags(struct ssif_info *ssif_info, unsigned long *flags)
{
	if (ssif_info->msg_flags & WDT_PRE_TIMEOUT_INT) {
		/* Watchdog pre-timeout */
		ssif_inc_stat(ssif_info, watchdog_pretimeouts);
		start_clear_flags(ssif_info, flags);
		ipmi_smi_watchdog_pretimeout(ssif_info->intf);
	} else if (ssif_info->msg_flags & RECEIVE_MSG_AVAIL)
		/* Messages available. */
		start_recv_msg_fetch(ssif_info, flags);
	else if (ssif_info->msg_flags & EVENT_MSG_BUFFER_FULL)
		/* Events available. */
		start_event_fetch(ssif_info, flags);
	else {
		ssif_info->ssif_state = SSIF_IDLE;
		ipmi_ssif_unlock_cond(ssif_info, flags);
	}
}

static int ipmi_ssif_thread(void *data)
{
	struct ssif_info *ssif_info = data;

	while (!kthread_should_stop()) {
		int result;

		/* Wait for something to do */
		result = wait_for_completion_interruptible(
						&ssif_info->wake_thread);
		if (ssif_info->stopping)
			break;
		if (result == -ERESTARTSYS)
			continue;
		init_completion(&ssif_info->wake_thread);

		if (ssif_info->i2c_read_write == I2C_SMBUS_WRITE) {
			result = i2c_smbus_write_block_data(
				ssif_info->client, ssif_info->i2c_command,
				ssif_info->i2c_data[0],
				ssif_info->i2c_data + 1);
			ssif_info->done_handler(ssif_info, result, NULL, 0);
		} else {
			result = i2c_smbus_read_block_data(
				ssif_info->client, ssif_info->i2c_command,
				ssif_info->i2c_data);
			if (result < 0)
				ssif_info->done_handler(ssif_info, result,
							NULL, 0);
			else
				ssif_info->done_handler(ssif_info, 0,
							ssif_info->i2c_data,
							result);
		}
	}

	return 0;
}

static void ssif_i2c_send(struct ssif_info *ssif_info,
			ssif_i2c_done handler,
			int read_write, int command,
			unsigned char *data, unsigned int size)
{
	ssif_info->done_handler = handler;

	ssif_info->i2c_read_write = read_write;
	ssif_info->i2c_command = command;
	ssif_info->i2c_data = data;
	ssif_info->i2c_size = size;
	complete(&ssif_info->wake_thread);
}


static void msg_done_handler(struct ssif_info *ssif_info, int result,
			     unsigned char *data, unsigned int len);

static void start_get(struct ssif_info *ssif_info)
{
	ssif_info->multi_pos = 0;

	ssif_i2c_send(ssif_info, msg_done_handler, I2C_SMBUS_READ,
		  SSIF_IPMI_RESPONSE,
		  ssif_info->recv, I2C_SMBUS_BLOCK_DATA);
}

static void start_resend(struct ssif_info *ssif_info);

static void retry_timeout(struct timer_list *t)
{
	struct ssif_info *ssif_info = from_timer(ssif_info, t, retry_timer);
	unsigned long oflags, *flags;
	bool waiting, resend;

	if (ssif_info->stopping)
		return;

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	resend = ssif_info->do_resend;
	ssif_info->do_resend = false;
	waiting = ssif_info->waiting_alert;
	ssif_info->waiting_alert = false;
	ipmi_ssif_unlock_cond(ssif_info, flags);

	if (waiting)
		start_get(ssif_info);
	if (resend) {
		start_resend(ssif_info);
		ssif_inc_stat(ssif_info, send_retries);
	}
}

static void watch_timeout(struct timer_list *t)
{
	struct ssif_info *ssif_info = from_timer(ssif_info, t, watch_timer);
	unsigned long oflags, *flags;

	if (ssif_info->stopping)
		return;

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	if (ssif_info->watch_timeout) {
		mod_timer(&ssif_info->watch_timer,
			  jiffies + ssif_info->watch_timeout);
		if (IS_SSIF_IDLE(ssif_info)) {
			start_flag_fetch(ssif_info, flags); /* Releases lock */
			return;
		}
		ssif_info->req_flags = true;
	}
	ipmi_ssif_unlock_cond(ssif_info, flags);
}

static void ssif_alert(struct i2c_client *client, enum i2c_alert_protocol type,
		       unsigned int data)
{
	struct ssif_info *ssif_info = i2c_get_clientdata(client);
	unsigned long oflags, *flags;
	bool do_get = false;

	if (type != I2C_PROTOCOL_SMBUS_ALERT)
		return;

	ssif_inc_stat(ssif_info, alerts);

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	if (ssif_info->waiting_alert) {
		ssif_info->waiting_alert = false;
		del_timer(&ssif_info->retry_timer);
		do_get = true;
	} else if (ssif_info->curr_msg) {
		ssif_info->got_alert = true;
	}
	ipmi_ssif_unlock_cond(ssif_info, flags);
	if (do_get)
		start_get(ssif_info);
}

static void msg_done_handler(struct ssif_info *ssif_info, int result,
			     unsigned char *data, unsigned int len)
{
	struct ipmi_smi_msg *msg;
	unsigned long oflags, *flags;

	/*
	 * We are single-threaded here, so no need for a lock until we
	 * start messing with driver states or the queues.
	 */

	if (result < 0) {
		ssif_info->retries_left--;
		if (ssif_info->retries_left > 0) {
			ssif_inc_stat(ssif_info, receive_retries);

			flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
			ssif_info->waiting_alert = true;
			if (!ssif_info->stopping)
				mod_timer(&ssif_info->retry_timer,
					  jiffies + SSIF_MSG_JIFFIES);
			ipmi_ssif_unlock_cond(ssif_info, flags);
			return;
		}

		ssif_inc_stat(ssif_info, receive_errors);

		if  (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
			dev_dbg(&ssif_info->client->dev,
				"%s: Error %d\n", __func__, result);
		len = 0;
		goto continue_op;
	}

	if ((len > 1) && (ssif_info->multi_pos == 0)
				&& (data[0] == 0x00) && (data[1] == 0x01)) {
		/* Start of multi-part read.  Start the next transaction. */
		int i;

		ssif_inc_stat(ssif_info, received_message_parts);

		/* Remove the multi-part read marker. */
		len -= 2;
		data += 2;
		for (i = 0; i < len; i++)
			ssif_info->data[i] = data[i];
		ssif_info->multi_len = len;
		ssif_info->multi_pos = 1;

		ssif_i2c_send(ssif_info, msg_done_handler, I2C_SMBUS_READ,
			 SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE,
			 ssif_info->recv, I2C_SMBUS_BLOCK_DATA);
		return;
	} else if (ssif_info->multi_pos) {
		/* Middle of multi-part read.  Start the next transaction. */
		int i;
		unsigned char blocknum;

		if (len == 0) {
			result = -EIO;
			if (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
				dev_dbg(&ssif_info->client->dev,
					"Middle message with no data\n");

			goto continue_op;
		}

		blocknum = data[0];
		len--;
		data++;

		if (blocknum != 0xff && len != 31) {
		    /* All blocks but the last must have 31 data bytes. */
			result = -EIO;
			if (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
				dev_dbg(&ssif_info->client->dev,
					"Received middle message <31\n");

			goto continue_op;
		}

		if (ssif_info->multi_len + len > IPMI_MAX_MSG_LENGTH) {
			/* Received message too big, abort the operation. */
			result = -E2BIG;
			if (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
				dev_dbg(&ssif_info->client->dev,
					"Received message too big\n");

			goto continue_op;
		}

		for (i = 0; i < len; i++)
			ssif_info->data[i + ssif_info->multi_len] = data[i];
		ssif_info->multi_len += len;
		if (blocknum == 0xff) {
			/* End of read */
			len = ssif_info->multi_len;
			data = ssif_info->data;
		} else if (blocknum + 1 != ssif_info->multi_pos) {
			/*
			 * Out of sequence block, just abort.  Block
			 * numbers start at zero for the second block,
			 * but multi_pos starts at one, so the +1.
			 */
			if (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
				dev_dbg(&ssif_info->client->dev,
					"Received message out of sequence, expected %u, got %u\n",
					ssif_info->multi_pos - 1, blocknum);
			result = -EIO;
		} else {
			ssif_inc_stat(ssif_info, received_message_parts);

			ssif_info->multi_pos++;

			ssif_i2c_send(ssif_info, msg_done_handler,
				  I2C_SMBUS_READ,
				  SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE,
				  ssif_info->recv,
				  I2C_SMBUS_BLOCK_DATA);
			return;
		}
	}

 continue_op:
	if (result < 0) {
		ssif_inc_stat(ssif_info, receive_errors);
	} else {
		ssif_inc_stat(ssif_info, received_messages);
		ssif_inc_stat(ssif_info, received_message_parts);
	}

	if (ssif_info->ssif_debug & SSIF_DEBUG_STATE)
		dev_dbg(&ssif_info->client->dev,
			"DONE 1: state = %d, result=%d\n",
			ssif_info->ssif_state, result);

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	msg = ssif_info->curr_msg;
	if (msg) {
		if (data) {
			if (len > IPMI_MAX_MSG_LENGTH)
				len = IPMI_MAX_MSG_LENGTH;
			memcpy(msg->rsp, data, len);
		} else {
			len = 0;
		}
		msg->rsp_size = len;
		ssif_info->curr_msg = NULL;
	}

	switch (ssif_info->ssif_state) {
	case SSIF_IDLE:
		ipmi_ssif_unlock_cond(ssif_info, flags);
		if (!msg)
			break;

		if (result < 0)
			return_hosed_msg(ssif_info, msg);
		else
			deliver_recv_msg(ssif_info, msg);
		break;

	case SSIF_GETTING_FLAGS:
		/* We got the flags from the SSIF, now handle them. */
		if ((result < 0) || (len < 4) || (data[2] != 0)) {
			/*
			 * Error fetching flags, or invalid length,
			 * just give up for now.
			 */
			ssif_info->ssif_state = SSIF_IDLE;
			ipmi_ssif_unlock_cond(ssif_info, flags);
			dev_warn(&ssif_info->client->dev,
				 "Error getting flags: %d %d, %x\n",
				 result, len, (len >= 3) ? data[2] : 0);
		} else if (data[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2
			   || data[1] != IPMI_GET_MSG_FLAGS_CMD) {
			/*
			 * Recv error response, give up.
			 */
			ssif_info->ssif_state = SSIF_IDLE;
			ipmi_ssif_unlock_cond(ssif_info, flags);
			dev_warn(&ssif_info->client->dev,
				 "Invalid response getting flags: %x %x\n",
				 data[0], data[1]);
		} else {
			ssif_inc_stat(ssif_info, flag_fetches);
			ssif_info->msg_flags = data[3];
			handle_flags(ssif_info, flags);
		}
		break;

	case SSIF_CLEARING_FLAGS:
		/* We cleared the flags. */
		if ((result < 0) || (len < 3) || (data[2] != 0)) {
			/* Error clearing flags */
			dev_warn(&ssif_info->client->dev,
				 "Error clearing flags: %d %d, %x\n",
				 result, len, (len >= 3) ? data[2] : 0);
		} else if (data[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2
			   || data[1] != IPMI_CLEAR_MSG_FLAGS_CMD) {
			dev_warn(&ssif_info->client->dev,
				 "Invalid response clearing flags: %x %x\n",
				 data[0], data[1]);
		}
		ssif_info->ssif_state = SSIF_IDLE;
		ipmi_ssif_unlock_cond(ssif_info, flags);
		break;

	case SSIF_GETTING_EVENTS:
		if (!msg) {
			/* Should never happen, but just in case. */
			dev_warn(&ssif_info->client->dev,
				 "No message set while getting events\n");
			ipmi_ssif_unlock_cond(ssif_info, flags);
			break;
		}

		if ((result < 0) || (len < 3) || (msg->rsp[2] != 0)) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the event flag. */
			ssif_info->msg_flags &= ~EVENT_MSG_BUFFER_FULL;
			handle_flags(ssif_info, flags);
		} else if (msg->rsp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2
			   || msg->rsp[1] != IPMI_READ_EVENT_MSG_BUFFER_CMD) {
			dev_warn(&ssif_info->client->dev,
				 "Invalid response getting events: %x %x\n",
				 msg->rsp[0], msg->rsp[1]);
			msg->done(msg);
			/* Take off the event flag. */
			ssif_info->msg_flags &= ~EVENT_MSG_BUFFER_FULL;
			handle_flags(ssif_info, flags);
		} else {
			handle_flags(ssif_info, flags);
			ssif_inc_stat(ssif_info, events);
			deliver_recv_msg(ssif_info, msg);
		}
		break;

	case SSIF_GETTING_MESSAGES:
		if (!msg) {
			/* Should never happen, but just in case. */
			dev_warn(&ssif_info->client->dev,
				 "No message set while getting messages\n");
			ipmi_ssif_unlock_cond(ssif_info, flags);
			break;
		}

		if ((result < 0) || (len < 3) || (msg->rsp[2] != 0)) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the msg flag. */
			ssif_info->msg_flags &= ~RECEIVE_MSG_AVAIL;
			handle_flags(ssif_info, flags);
		} else if (msg->rsp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2
			   || msg->rsp[1] != IPMI_GET_MSG_CMD) {
			dev_warn(&ssif_info->client->dev,
				 "Invalid response clearing flags: %x %x\n",
				 msg->rsp[0], msg->rsp[1]);
			msg->done(msg);

			/* Take off the msg flag. */
			ssif_info->msg_flags &= ~RECEIVE_MSG_AVAIL;
			handle_flags(ssif_info, flags);
		} else {
			ssif_inc_stat(ssif_info, incoming_messages);
			handle_flags(ssif_info, flags);
			deliver_recv_msg(ssif_info, msg);
		}
		break;

	default:
		/* Should never happen, but just in case. */
		dev_warn(&ssif_info->client->dev,
			 "Invalid state in message done handling: %d\n",
			 ssif_info->ssif_state);
		ipmi_ssif_unlock_cond(ssif_info, flags);
	}

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	if (IS_SSIF_IDLE(ssif_info) && !ssif_info->stopping) {
		if (ssif_info->req_events)
			start_event_fetch(ssif_info, flags);
		else if (ssif_info->req_flags)
			start_flag_fetch(ssif_info, flags);
		else
			start_next_msg(ssif_info, flags);
	} else
		ipmi_ssif_unlock_cond(ssif_info, flags);

	if (ssif_info->ssif_debug & SSIF_DEBUG_STATE)
		dev_dbg(&ssif_info->client->dev,
			"DONE 2: state = %d.\n", ssif_info->ssif_state);
}

static void msg_written_handler(struct ssif_info *ssif_info, int result,
				unsigned char *data, unsigned int len)
{
	/* We are single-threaded here, so no need for a lock. */
	if (result < 0) {
		ssif_info->retries_left--;
		if (ssif_info->retries_left > 0) {
			/*
			 * Wait the retry timeout time per the spec,
			 * then redo the send.
			 */
			ssif_info->do_resend = true;
			mod_timer(&ssif_info->retry_timer,
				  jiffies + SSIF_REQ_RETRY_JIFFIES);
			return;
		}

		ssif_inc_stat(ssif_info, send_errors);

		if (ssif_info->ssif_debug & SSIF_DEBUG_MSG)
			dev_dbg(&ssif_info->client->dev,
				"%s: Out of retries\n", __func__);

		msg_done_handler(ssif_info, -EIO, NULL, 0);
		return;
	}

	if (ssif_info->multi_data) {
		/*
		 * In the middle of a multi-data write.  See the comment
		 * in the SSIF_MULTI_n_PART case in the probe function
		 * for details on the intricacies of this.
		 */
		int left, to_write;
		unsigned char *data_to_send;
		unsigned char cmd;

		ssif_inc_stat(ssif_info, sent_messages_parts);

		left = ssif_info->multi_len - ssif_info->multi_pos;
		to_write = left;
		if (to_write > 32)
			to_write = 32;
		/* Length byte. */
		ssif_info->multi_data[ssif_info->multi_pos] = to_write;
		data_to_send = ssif_info->multi_data + ssif_info->multi_pos;
		ssif_info->multi_pos += to_write;
		cmd = SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE;
		if (ssif_info->cmd8_works) {
			if (left == to_write) {
				cmd = SSIF_IPMI_MULTI_PART_REQUEST_END;
				ssif_info->multi_data = NULL;
			}
		} else if (to_write < 32) {
			ssif_info->multi_data = NULL;
		}

		ssif_i2c_send(ssif_info, msg_written_handler,
			  I2C_SMBUS_WRITE, cmd,
			  data_to_send, I2C_SMBUS_BLOCK_DATA);
	} else {
		/* Ready to request the result. */
		unsigned long oflags, *flags;

		ssif_inc_stat(ssif_info, sent_messages);
		ssif_inc_stat(ssif_info, sent_messages_parts);

		flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
		if (ssif_info->got_alert) {
			/* The result is already ready, just start it. */
			ssif_info->got_alert = false;
			ipmi_ssif_unlock_cond(ssif_info, flags);
			start_get(ssif_info);
		} else {
			/* Wait a jiffie then request the next message */
			ssif_info->waiting_alert = true;
			ssif_info->retries_left = SSIF_RECV_RETRIES;
			if (!ssif_info->stopping)
				mod_timer(&ssif_info->retry_timer,
					  jiffies + SSIF_MSG_PART_JIFFIES);
			ipmi_ssif_unlock_cond(ssif_info, flags);
		}
	}
}

static void start_resend(struct ssif_info *ssif_info)
{
	int command;

	ssif_info->got_alert = false;

	if (ssif_info->data_len > 32) {
		command = SSIF_IPMI_MULTI_PART_REQUEST_START;
		ssif_info->multi_data = ssif_info->data;
		ssif_info->multi_len = ssif_info->data_len;
		/*
		 * Subtle thing, this is 32, not 33, because we will
		 * overwrite the thing at position 32 (which was just
		 * transmitted) with the new length.
		 */
		ssif_info->multi_pos = 32;
		ssif_info->data[0] = 32;
	} else {
		ssif_info->multi_data = NULL;
		command = SSIF_IPMI_REQUEST;
		ssif_info->data[0] = ssif_info->data_len;
	}

	ssif_i2c_send(ssif_info, msg_written_handler, I2C_SMBUS_WRITE,
		   command, ssif_info->data, I2C_SMBUS_BLOCK_DATA);
}

static int start_send(struct ssif_info *ssif_info,
		      unsigned char   *data,
		      unsigned int    len)
{
	if (len > IPMI_MAX_MSG_LENGTH)
		return -E2BIG;
	if (len > ssif_info->max_xmit_msg_size)
		return -E2BIG;

	ssif_info->retries_left = SSIF_SEND_RETRIES;
	memcpy(ssif_info->data + 1, data, len);
	ssif_info->data_len = len;
	start_resend(ssif_info);
	return 0;
}

/* Must be called with the message lock held. */
static void start_next_msg(struct ssif_info *ssif_info, unsigned long *flags)
{
	struct ipmi_smi_msg *msg;
	unsigned long oflags;

 restart:
	if (!IS_SSIF_IDLE(ssif_info)) {
		ipmi_ssif_unlock_cond(ssif_info, flags);
		return;
	}

	if (!ssif_info->waiting_msg) {
		ssif_info->curr_msg = NULL;
		ipmi_ssif_unlock_cond(ssif_info, flags);
	} else {
		int rv;

		ssif_info->curr_msg = ssif_info->waiting_msg;
		ssif_info->waiting_msg = NULL;
		ipmi_ssif_unlock_cond(ssif_info, flags);
		rv = start_send(ssif_info,
				ssif_info->curr_msg->data,
				ssif_info->curr_msg->data_size);
		if (rv) {
			msg = ssif_info->curr_msg;
			ssif_info->curr_msg = NULL;
			return_hosed_msg(ssif_info, msg);
			flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
			goto restart;
		}
	}
}

static void sender(void                *send_info,
		   struct ipmi_smi_msg *msg)
{
	struct ssif_info *ssif_info = send_info;
	unsigned long oflags, *flags;

	BUG_ON(ssif_info->waiting_msg);
	ssif_info->waiting_msg = msg;

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	start_next_msg(ssif_info, flags);

	if (ssif_info->ssif_debug & SSIF_DEBUG_TIMING) {
		struct timespec64 t;

		ktime_get_real_ts64(&t);
		dev_dbg(&ssif_info->client->dev,
			"**Enqueue %02x %02x: %lld.%6.6ld\n",
			msg->data[0], msg->data[1],
			(long long)t.tv_sec, (long)t.tv_nsec / NSEC_PER_USEC);
	}
}

static int get_smi_info(void *send_info, struct ipmi_smi_info *data)
{
	struct ssif_info *ssif_info = send_info;

	data->addr_src = ssif_info->addr_source;
	data->dev = &ssif_info->client->dev;
	data->addr_info = ssif_info->addr_info;
	get_device(data->dev);

	return 0;
}

/*
 * Upper layer wants us to request events.
 */
static void request_events(void *send_info)
{
	struct ssif_info *ssif_info = send_info;
	unsigned long oflags, *flags;

	if (!ssif_info->has_event_buffer)
		return;

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	ssif_info->req_events = true;
	ipmi_ssif_unlock_cond(ssif_info, flags);
}

/*
 * Upper layer is changing the flag saying whether we need to request
 * flags periodically or not.
 */
static void ssif_set_need_watch(void *send_info, unsigned int watch_mask)
{
	struct ssif_info *ssif_info = send_info;
	unsigned long oflags, *flags;
	long timeout = 0;

	if (watch_mask & IPMI_WATCH_MASK_CHECK_MESSAGES)
		timeout = SSIF_WATCH_MSG_TIMEOUT;
	else if (watch_mask)
		timeout = SSIF_WATCH_WATCHDOG_TIMEOUT;

	flags = ipmi_ssif_lock_cond(ssif_info, &oflags);
	if (timeout != ssif_info->watch_timeout) {
		ssif_info->watch_timeout = timeout;
		if (ssif_info->watch_timeout)
			mod_timer(&ssif_info->watch_timer,
				  jiffies + ssif_info->watch_timeout);
	}
	ipmi_ssif_unlock_cond(ssif_info, flags);
}

static int ssif_start_processing(void            *send_info,
				 struct ipmi_smi *intf)
{
	struct ssif_info *ssif_info = send_info;

	ssif_info->intf = intf;

	return 0;
}

#define MAX_SSIF_BMCS 4

static unsigned short addr[MAX_SSIF_BMCS];
static int num_addrs;
module_param_array(addr, ushort, &num_addrs, 0);
MODULE_PARM_DESC(addr, "The addresses to scan for IPMI BMCs on the SSIFs.");

static char *adapter_name[MAX_SSIF_BMCS];
static int num_adapter_names;
module_param_array(adapter_name, charp, &num_adapter_names, 0);
MODULE_PARM_DESC(adapter_name, "The string name of the I2C device that has the BMC.  By default all devices are scanned.");

static int slave_addrs[MAX_SSIF_BMCS];
static int num_slave_addrs;
module_param_array(slave_addrs, int, &num_slave_addrs, 0);
MODULE_PARM_DESC(slave_addrs,
		 "The default IPMB slave address for the controller.");

static bool alerts_broken;
module_param(alerts_broken, bool, 0);
MODULE_PARM_DESC(alerts_broken, "Don't enable alerts for the controller.");

/*
 * Bit 0 enables message debugging, bit 1 enables state debugging, and
 * bit 2 enables timing debugging.  This is an array indexed by
 * interface number"
 */
static int dbg[MAX_SSIF_BMCS];
static int num_dbg;
module_param_array(dbg, int, &num_dbg, 0);
MODULE_PARM_DESC(dbg, "Turn on debugging.");

static bool ssif_dbg_probe;
module_param_named(dbg_probe, ssif_dbg_probe, bool, 0);
MODULE_PARM_DESC(dbg_probe, "Enable debugging of probing of adapters.");

static bool ssif_tryacpi = true;
module_param_named(tryacpi, ssif_tryacpi, bool, 0);
MODULE_PARM_DESC(tryacpi, "Setting this to zero will disable the default scan of the interfaces identified via ACPI");

static bool ssif_trydmi = true;
module_param_named(trydmi, ssif_trydmi, bool, 0);
MODULE_PARM_DESC(trydmi, "Setting this to zero will disable the default scan of the interfaces identified via DMI (SMBIOS)");

static DEFINE_MUTEX(ssif_infos_mutex);
static LIST_HEAD(ssif_infos);

#define IPMI_SSIF_ATTR(name) \
static ssize_t ipmi_##name##_show(struct device *dev,			\
				  struct device_attribute *attr,	\
				  char *buf)				\
{									\
	struct ssif_info *ssif_info = dev_get_drvdata(dev);		\
									\
	return sysfs_emit(buf, "%u\n", ssif_get_stat(ssif_info, name));\
}									\
static DEVICE_ATTR(name, S_IRUGO, ipmi_##name##_show, NULL)

static ssize_t ipmi_type_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "ssif\n");
}
static DEVICE_ATTR(type, S_IRUGO, ipmi_type_show, NULL);

IPMI_SSIF_ATTR(sent_messages);
IPMI_SSIF_ATTR(sent_messages_parts);
IPMI_SSIF_ATTR(send_retries);
IPMI_SSIF_ATTR(send_errors);
IPMI_SSIF_ATTR(received_messages);
IPMI_SSIF_ATTR(received_message_parts);
IPMI_SSIF_ATTR(receive_retries);
IPMI_SSIF_ATTR(receive_errors);
IPMI_SSIF_ATTR(flag_fetches);
IPMI_SSIF_ATTR(hosed);
IPMI_SSIF_ATTR(events);
IPMI_SSIF_ATTR(watchdog_pretimeouts);
IPMI_SSIF_ATTR(alerts);

static struct attribute *ipmi_ssif_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_sent_messages.attr,
	&dev_attr_sent_messages_parts.attr,
	&dev_attr_send_retries.attr,
	&dev_attr_send_errors.attr,
	&dev_attr_received_messages.attr,
	&dev_attr_received_message_parts.attr,
	&dev_attr_receive_retries.attr,
	&dev_attr_receive_errors.attr,
	&dev_attr_flag_fetches.attr,
	&dev_attr_hosed.attr,
	&dev_attr_events.attr,
	&dev_attr_watchdog_pretimeouts.attr,
	&dev_attr_alerts.attr,
	NULL
};

static const struct attribute_group ipmi_ssif_dev_attr_group = {
	.attrs		= ipmi_ssif_dev_attrs,
};

static void shutdown_ssif(void *send_info)
{
	struct ssif_info *ssif_info = send_info;

	device_remove_group(&ssif_info->client->dev, &ipmi_ssif_dev_attr_group);
	dev_set_drvdata(&ssif_info->client->dev, NULL);

	/* make sure the driver is not looking for flags any more. */
	while (ssif_info->ssif_state != SSIF_IDLE)
		schedule_timeout(1);

	ssif_info->stopping = true;
	del_timer_sync(&ssif_info->watch_timer);
	del_timer_sync(&ssif_info->retry_timer);
	if (ssif_info->thread) {
		complete(&ssif_info->wake_thread);
		kthread_stop(ssif_info->thread);
	}
}

static void ssif_remove(struct i2c_client *client)
{
	struct ssif_info *ssif_info = i2c_get_clientdata(client);
	struct ssif_addr_info *addr_info;

	/*
	 * After this point, we won't deliver anything asynchronously
	 * to the message handler.  We can unregister ourself.
	 */
	ipmi_unregister_smi(ssif_info->intf);

	list_for_each_entry(addr_info, &ssif_infos, link) {
		if (addr_info->client == client) {
			addr_info->client = NULL;
			break;
		}
	}

	kfree(ssif_info);
}

static int read_response(struct i2c_client *client, unsigned char *resp)
{
	int ret = -ENODEV, retry_cnt = SSIF_RECV_RETRIES;

	while (retry_cnt > 0) {
		ret = i2c_smbus_read_block_data(client, SSIF_IPMI_RESPONSE,
						resp);
		if (ret > 0)
			break;
		msleep(SSIF_MSG_MSEC);
		retry_cnt--;
		if (retry_cnt <= 0)
			break;
	}

	return ret;
}

static int do_cmd(struct i2c_client *client, int len, unsigned char *msg,
		  int *resp_len, unsigned char *resp)
{
	int retry_cnt;
	int ret;

	retry_cnt = SSIF_SEND_RETRIES;
 retry1:
	ret = i2c_smbus_write_block_data(client, SSIF_IPMI_REQUEST, len, msg);
	if (ret) {
		retry_cnt--;
		if (retry_cnt > 0) {
			msleep(SSIF_REQ_RETRY_MSEC);
			goto retry1;
		}
		return -ENODEV;
	}

	ret = read_response(client, resp);
	if (ret > 0) {
		/* Validate that the response is correct. */
		if (ret < 3 ||
		    (resp[0] != (msg[0] | (1 << 2))) ||
		    (resp[1] != msg[1]))
			ret = -EINVAL;
		else if (ret > IPMI_MAX_MSG_LENGTH) {
			ret = -E2BIG;
		} else {
			*resp_len = ret;
			ret = 0;
		}
	}

	return ret;
}

static int ssif_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	unsigned char *resp;
	unsigned char msg[3];
	int           rv;
	int           len;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	/* Do a Get Device ID command, since it is required. */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_DEVICE_ID_CMD;
	rv = do_cmd(client, 2, msg, &len, resp);
	if (rv)
		rv = -ENODEV;
	else
		strscpy(info->type, DEVICE_NAME, I2C_NAME_SIZE);
	kfree(resp);
	return rv;
}

static int strcmp_nospace(char *s1, char *s2)
{
	while (*s1 && *s2) {
		while (isspace(*s1))
			s1++;
		while (isspace(*s2))
			s2++;
		if (*s1 > *s2)
			return 1;
		if (*s1 < *s2)
			return -1;
		s1++;
		s2++;
	}
	return 0;
}

static struct ssif_addr_info *ssif_info_find(unsigned short addr,
					     char *adapter_name,
					     bool match_null_name)
{
	struct ssif_addr_info *info, *found = NULL;

restart:
	list_for_each_entry(info, &ssif_infos, link) {
		if (info->binfo.addr == addr) {
			if (info->addr_src == SI_SMBIOS && !info->adapter_name)
				info->adapter_name = kstrdup(adapter_name,
							     GFP_KERNEL);

			if (info->adapter_name || adapter_name) {
				if (!info->adapter_name != !adapter_name) {
					/* One is NULL and one is not */
					continue;
				}
				if (adapter_name &&
				    strcmp_nospace(info->adapter_name,
						   adapter_name))
					/* Names do not match */
					continue;
			}
			found = info;
			break;
		}
	}

	if (!found && match_null_name) {
		/* Try to get an exact match first, then try with a NULL name */
		adapter_name = NULL;
		match_null_name = false;
		goto restart;
	}

	return found;
}

static bool check_acpi(struct ssif_info *ssif_info, struct device *dev)
{
#ifdef CONFIG_ACPI
	acpi_handle acpi_handle;

	acpi_handle = ACPI_HANDLE(dev);
	if (acpi_handle) {
		ssif_info->addr_source = SI_ACPI;
		ssif_info->addr_info.acpi_info.acpi_handle = acpi_handle;
		request_module_nowait("acpi_ipmi");
		return true;
	}
#endif
	return false;
}

static int find_slave_address(struct i2c_client *client, int slave_addr)
{
#ifdef CONFIG_IPMI_DMI_DECODE
	if (!slave_addr)
		slave_addr = ipmi_dmi_get_slave_addr(
			SI_TYPE_INVALID,
			i2c_adapter_id(client->adapter),
			client->addr);
#endif

	return slave_addr;
}

static int start_multipart_test(struct i2c_client *client,
				unsigned char *msg, bool do_middle)
{
	int retry_cnt = SSIF_SEND_RETRIES, ret;

retry_write:
	ret = i2c_smbus_write_block_data(client,
					 SSIF_IPMI_MULTI_PART_REQUEST_START,
					 32, msg);
	if (ret) {
		retry_cnt--;
		if (retry_cnt > 0) {
			msleep(SSIF_REQ_RETRY_MSEC);
			goto retry_write;
		}
		dev_err(&client->dev, "Could not write multi-part start, though the BMC said it could handle it.  Just limit sends to one part.\n");
		return ret;
	}

	if (!do_middle)
		return 0;

	ret = i2c_smbus_write_block_data(client,
					 SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE,
					 32, msg + 32);
	if (ret) {
		dev_err(&client->dev, "Could not write multi-part middle, though the BMC said it could handle it.  Just limit sends to one part.\n");
		return ret;
	}

	return 0;
}

static void test_multipart_messages(struct i2c_client *client,
				    struct ssif_info *ssif_info,
				    unsigned char *resp)
{
	unsigned char msg[65];
	int ret;
	bool do_middle;

	if (ssif_info->max_xmit_msg_size <= 32)
		return;

	do_middle = ssif_info->max_xmit_msg_size > 63;

	memset(msg, 0, sizeof(msg));
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_DEVICE_ID_CMD;

	/*
	 * The specification is all messed up dealing with sending
	 * multi-part messages.  Per what the specification says, it
	 * is impossible to send a message that is a multiple of 32
	 * bytes, except for 32 itself.  It talks about a "start"
	 * transaction (cmd=6) that must be 32 bytes, "middle"
	 * transaction (cmd=7) that must be 32 bytes, and an "end"
	 * transaction.  The "end" transaction is shown as cmd=7 in
	 * the text, but if that's the case there is no way to
	 * differentiate between a middle and end part except the
	 * length being less than 32.  But there is a table at the far
	 * end of the section (that I had never noticed until someone
	 * pointed it out to me) that mentions it as cmd=8.
	 *
	 * After some thought, I think the example is wrong and the
	 * end transaction should be cmd=8.  But some systems don't
	 * implement cmd=8, they use a zero-length end transaction,
	 * even though that violates the SMBus specification.
	 *
	 * So, to work around this, this code tests if cmd=8 works.
	 * If it does, then we use that.  If not, it tests zero-
	 * byte end transactions.  If that works, good.  If not,
	 * we only allow 63-byte transactions max.
	 */

	ret = start_multipart_test(client, msg, do_middle);
	if (ret)
		goto out_no_multi_part;

	ret = i2c_smbus_write_block_data(client,
					 SSIF_IPMI_MULTI_PART_REQUEST_END,
					 1, msg + 64);

	if (!ret)
		ret = read_response(client, resp);

	if (ret > 0) {
		/* End transactions work, we are good. */
		ssif_info->cmd8_works = true;
		return;
	}

	ret = start_multipart_test(client, msg, do_middle);
	if (ret) {
		dev_err(&client->dev, "Second multipart test failed.\n");
		goto out_no_multi_part;
	}

	ret = i2c_smbus_write_block_data(client,
					 SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE,
					 0, msg + 64);
	if (!ret)
		ret = read_response(client, resp);
	if (ret > 0)
		/* Zero-size end parts work, use those. */
		return;

	/* Limit to 63 bytes and use a short middle command to mark the end. */
	if (ssif_info->max_xmit_msg_size > 63)
		ssif_info->max_xmit_msg_size = 63;
	return;

out_no_multi_part:
	ssif_info->max_xmit_msg_size = 32;
	return;
}

/*
 * Global enables we care about.
 */
#define GLOBAL_ENABLES_MASK (IPMI_BMC_EVT_MSG_BUFF | IPMI_BMC_RCV_MSG_INTR | \
			     IPMI_BMC_EVT_MSG_INTR)

static void ssif_remove_dup(struct i2c_client *client)
{
	struct ssif_info *ssif_info = i2c_get_clientdata(client);

	ipmi_unregister_smi(ssif_info->intf);
	kfree(ssif_info);
}

static int ssif_add_infos(struct i2c_client *client)
{
	struct ssif_addr_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->addr_src = SI_ACPI;
	info->client = client;
	info->adapter_name = kstrdup(client->adapter->name, GFP_KERNEL);
	if (!info->adapter_name) {
		kfree(info);
		return -ENOMEM;
	}

	info->binfo.addr = client->addr;
	list_add_tail(&info->link, &ssif_infos);
	return 0;
}

/*
 * Prefer ACPI over SMBIOS, if both are available.
 * So if we get an ACPI interface and have already registered a SMBIOS
 * interface at the same address, remove the SMBIOS and add the ACPI one.
 */
static int ssif_check_and_remove(struct i2c_client *client,
			      struct ssif_info *ssif_info)
{
	struct ssif_addr_info *info;

	list_for_each_entry(info, &ssif_infos, link) {
		if (!info->client)
			return 0;
		if (!strcmp(info->adapter_name, client->adapter->name) &&
		    info->binfo.addr == client->addr) {
			if (info->addr_src == SI_ACPI)
				return -EEXIST;

			if (ssif_info->addr_source == SI_ACPI &&
			    info->addr_src == SI_SMBIOS) {
				dev_info(&client->dev,
					 "Removing %s-specified SSIF interface in favor of ACPI\n",
					 ipmi_addr_src_to_str(info->addr_src));
				ssif_remove_dup(info->client);
				return 0;
			}
		}
	}
	return 0;
}

static int ssif_probe(struct i2c_client *client)
{
	unsigned char     msg[3];
	unsigned char     *resp;
	struct ssif_info   *ssif_info;
	int               rv = 0;
	int               len = 0;
	int               i;
	u8		  slave_addr = 0;
	struct ssif_addr_info *addr_info = NULL;

	mutex_lock(&ssif_infos_mutex);
	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp) {
		mutex_unlock(&ssif_infos_mutex);
		return -ENOMEM;
	}

	ssif_info = kzalloc(sizeof(*ssif_info), GFP_KERNEL);
	if (!ssif_info) {
		kfree(resp);
		mutex_unlock(&ssif_infos_mutex);
		return -ENOMEM;
	}

	if (!check_acpi(ssif_info, &client->dev)) {
		addr_info = ssif_info_find(client->addr, client->adapter->name,
					   true);
		if (!addr_info) {
			/* Must have come in through sysfs. */
			ssif_info->addr_source = SI_HOTMOD;
		} else {
			ssif_info->addr_source = addr_info->addr_src;
			ssif_info->ssif_debug = addr_info->debug;
			ssif_info->addr_info = addr_info->addr_info;
			addr_info->client = client;
			slave_addr = addr_info->slave_addr;
		}
	}

	ssif_info->client = client;
	i2c_set_clientdata(client, ssif_info);

	rv = ssif_check_and_remove(client, ssif_info);
	/* If rv is 0 and addr source is not SI_ACPI, continue probing */
	if (!rv && ssif_info->addr_source == SI_ACPI) {
		rv = ssif_add_infos(client);
		if (rv) {
			dev_err(&client->dev, "Out of memory!, exiting ..\n");
			goto out;
		}
	} else if (rv) {
		dev_err(&client->dev, "Not probing, Interface already present\n");
		goto out;
	}

	slave_addr = find_slave_address(client, slave_addr);

	dev_info(&client->dev,
		 "Trying %s-specified SSIF interface at i2c address 0x%x, adapter %s, slave address 0x%x\n",
		ipmi_addr_src_to_str(ssif_info->addr_source),
		client->addr, client->adapter->name, slave_addr);

	/* Now check for system interface capabilities */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_SYSTEM_INTERFACE_CAPABILITIES_CMD;
	msg[2] = 0; /* SSIF */
	rv = do_cmd(client, 3, msg, &len, resp);
	if (!rv && (len >= 3) && (resp[2] == 0)) {
		if (len < 7) {
			if (ssif_dbg_probe)
				dev_dbg(&ssif_info->client->dev,
					"SSIF info too short: %d\n", len);
			goto no_support;
		}

		/* Got a good SSIF response, handle it. */
		ssif_info->max_xmit_msg_size = resp[5];
		ssif_info->max_recv_msg_size = resp[6];
		ssif_info->multi_support = (resp[4] >> 6) & 0x3;
		ssif_info->supports_pec = (resp[4] >> 3) & 0x1;

		/* Sanitize the data */
		switch (ssif_info->multi_support) {
		case SSIF_NO_MULTI:
			if (ssif_info->max_xmit_msg_size > 32)
				ssif_info->max_xmit_msg_size = 32;
			if (ssif_info->max_recv_msg_size > 32)
				ssif_info->max_recv_msg_size = 32;
			break;

		case SSIF_MULTI_2_PART:
			if (ssif_info->max_xmit_msg_size > 63)
				ssif_info->max_xmit_msg_size = 63;
			if (ssif_info->max_recv_msg_size > 62)
				ssif_info->max_recv_msg_size = 62;
			break;

		case SSIF_MULTI_n_PART:
			/* We take whatever size given, but do some testing. */
			break;

		default:
			/* Data is not sane, just give up. */
			goto no_support;
		}
	} else {
 no_support:
		/* Assume no multi-part or PEC support */
		dev_info(&ssif_info->client->dev,
			 "Error fetching SSIF: %d %d %2.2x, your system probably doesn't support this command so using defaults\n",
			rv, len, resp[2]);

		ssif_info->max_xmit_msg_size = 32;
		ssif_info->max_recv_msg_size = 32;
		ssif_info->multi_support = SSIF_NO_MULTI;
		ssif_info->supports_pec = 0;
	}

	test_multipart_messages(client, ssif_info, resp);

	/* Make sure the NMI timeout is cleared. */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_CLEAR_MSG_FLAGS_CMD;
	msg[2] = WDT_PRE_TIMEOUT_INT;
	rv = do_cmd(client, 3, msg, &len, resp);
	if (rv || (len < 3) || (resp[2] != 0))
		dev_warn(&ssif_info->client->dev,
			 "Unable to clear message flags: %d %d %2.2x\n",
			 rv, len, resp[2]);

	/* Attempt to enable the event buffer. */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;
	rv = do_cmd(client, 2, msg, &len, resp);
	if (rv || (len < 4) || (resp[2] != 0)) {
		dev_warn(&ssif_info->client->dev,
			 "Error getting global enables: %d %d %2.2x\n",
			 rv, len, resp[2]);
		rv = 0; /* Not fatal */
		goto found;
	}

	ssif_info->global_enables = resp[3];

	if (resp[3] & IPMI_BMC_EVT_MSG_BUFF) {
		ssif_info->has_event_buffer = true;
		/* buffer is already enabled, nothing to do. */
		goto found;
	}

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
	msg[2] = ssif_info->global_enables | IPMI_BMC_EVT_MSG_BUFF;
	rv = do_cmd(client, 3, msg, &len, resp);
	if (rv || (len < 2)) {
		dev_warn(&ssif_info->client->dev,
			 "Error setting global enables: %d %d %2.2x\n",
			 rv, len, resp[2]);
		rv = 0; /* Not fatal */
		goto found;
	}

	if (resp[2] == 0) {
		/* A successful return means the event buffer is supported. */
		ssif_info->has_event_buffer = true;
		ssif_info->global_enables |= IPMI_BMC_EVT_MSG_BUFF;
	}

	/* Some systems don't behave well if you enable alerts. */
	if (alerts_broken)
		goto found;

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
	msg[2] = ssif_info->global_enables | IPMI_BMC_RCV_MSG_INTR;
	rv = do_cmd(client, 3, msg, &len, resp);
	if (rv || (len < 2)) {
		dev_warn(&ssif_info->client->dev,
			 "Error setting global enables: %d %d %2.2x\n",
			 rv, len, resp[2]);
		rv = 0; /* Not fatal */
		goto found;
	}

	if (resp[2] == 0) {
		/* A successful return means the alert is supported. */
		ssif_info->supports_alert = true;
		ssif_info->global_enables |= IPMI_BMC_RCV_MSG_INTR;
	}

 found:
	if (ssif_dbg_probe) {
		dev_dbg(&ssif_info->client->dev,
		       "%s: i2c_probe found device at i2c address %x\n",
		       __func__, client->addr);
	}

	spin_lock_init(&ssif_info->lock);
	ssif_info->ssif_state = SSIF_IDLE;
	timer_setup(&ssif_info->retry_timer, retry_timeout, 0);
	timer_setup(&ssif_info->watch_timer, watch_timeout, 0);

	for (i = 0; i < SSIF_NUM_STATS; i++)
		atomic_set(&ssif_info->stats[i], 0);

	if (ssif_info->supports_pec)
		ssif_info->client->flags |= I2C_CLIENT_PEC;

	ssif_info->handlers.owner = THIS_MODULE;
	ssif_info->handlers.start_processing = ssif_start_processing;
	ssif_info->handlers.shutdown = shutdown_ssif;
	ssif_info->handlers.get_smi_info = get_smi_info;
	ssif_info->handlers.sender = sender;
	ssif_info->handlers.request_events = request_events;
	ssif_info->handlers.set_need_watch = ssif_set_need_watch;

	{
		unsigned int thread_num;

		thread_num = ((i2c_adapter_id(ssif_info->client->adapter)
			       << 8) |
			      ssif_info->client->addr);
		init_completion(&ssif_info->wake_thread);
		ssif_info->thread = kthread_run(ipmi_ssif_thread, ssif_info,
					       "kssif%4.4x", thread_num);
		if (IS_ERR(ssif_info->thread)) {
			rv = PTR_ERR(ssif_info->thread);
			dev_notice(&ssif_info->client->dev,
				   "Could not start kernel thread: error %d\n",
				   rv);
			goto out;
		}
	}

	dev_set_drvdata(&ssif_info->client->dev, ssif_info);
	rv = device_add_group(&ssif_info->client->dev,
			      &ipmi_ssif_dev_attr_group);
	if (rv) {
		dev_err(&ssif_info->client->dev,
			"Unable to add device attributes: error %d\n",
			rv);
		goto out;
	}

	rv = ipmi_register_smi(&ssif_info->handlers,
			       ssif_info,
			       &ssif_info->client->dev,
			       slave_addr);
	if (rv) {
		dev_err(&ssif_info->client->dev,
			"Unable to register device: error %d\n", rv);
		goto out_remove_attr;
	}

 out:
	if (rv) {
		if (addr_info)
			addr_info->client = NULL;

		dev_err(&ssif_info->client->dev,
			"Unable to start IPMI SSIF: %d\n", rv);
		i2c_set_clientdata(client, NULL);
		kfree(ssif_info);
	}
	kfree(resp);
	mutex_unlock(&ssif_infos_mutex);
	return rv;

out_remove_attr:
	device_remove_group(&ssif_info->client->dev, &ipmi_ssif_dev_attr_group);
	dev_set_drvdata(&ssif_info->client->dev, NULL);
	goto out;
}

static int new_ssif_client(int addr, char *adapter_name,
			   int debug, int slave_addr,
			   enum ipmi_addr_src addr_src,
			   struct device *dev)
{
	struct ssif_addr_info *addr_info;
	int rv = 0;

	mutex_lock(&ssif_infos_mutex);
	if (ssif_info_find(addr, adapter_name, false)) {
		rv = -EEXIST;
		goto out_unlock;
	}

	addr_info = kzalloc(sizeof(*addr_info), GFP_KERNEL);
	if (!addr_info) {
		rv = -ENOMEM;
		goto out_unlock;
	}

	if (adapter_name) {
		addr_info->adapter_name = kstrdup(adapter_name, GFP_KERNEL);
		if (!addr_info->adapter_name) {
			kfree(addr_info);
			rv = -ENOMEM;
			goto out_unlock;
		}
	}

	strncpy(addr_info->binfo.type, DEVICE_NAME,
		sizeof(addr_info->binfo.type));
	addr_info->binfo.addr = addr;
	addr_info->binfo.platform_data = addr_info;
	addr_info->debug = debug;
	addr_info->slave_addr = slave_addr;
	addr_info->addr_src = addr_src;
	addr_info->dev = dev;

	if (dev)
		dev_set_drvdata(dev, addr_info);

	list_add_tail(&addr_info->link, &ssif_infos);

	/* Address list will get it */

out_unlock:
	mutex_unlock(&ssif_infos_mutex);
	return rv;
}

static void free_ssif_clients(void)
{
	struct ssif_addr_info *info, *tmp;

	mutex_lock(&ssif_infos_mutex);
	list_for_each_entry_safe(info, tmp, &ssif_infos, link) {
		list_del(&info->link);
		kfree(info->adapter_name);
		kfree(info);
	}
	mutex_unlock(&ssif_infos_mutex);
}

static unsigned short *ssif_address_list(void)
{
	struct ssif_addr_info *info;
	unsigned int count = 0, i = 0;
	unsigned short *address_list;

	list_for_each_entry(info, &ssif_infos, link)
		count++;

	address_list = kcalloc(count + 1, sizeof(*address_list),
			       GFP_KERNEL);
	if (!address_list)
		return NULL;

	list_for_each_entry(info, &ssif_infos, link) {
		unsigned short addr = info->binfo.addr;
		int j;

		for (j = 0; j < i; j++) {
			if (address_list[j] == addr)
				/* Found a dup. */
				break;
		}
		if (j == i) /* Didn't find it in the list. */
			address_list[i++] = addr;
	}
	address_list[i] = I2C_CLIENT_END;

	return address_list;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id ssif_acpi_match[] = {
	{ "IPI0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ssif_acpi_match);
#endif

#ifdef CONFIG_DMI
static int dmi_ipmi_probe(struct platform_device *pdev)
{
	u8 slave_addr = 0;
	u16 i2c_addr;
	int rv;

	if (!ssif_trydmi)
		return -ENODEV;

	rv = device_property_read_u16(&pdev->dev, "i2c-addr", &i2c_addr);
	if (rv) {
		dev_warn(&pdev->dev, "No i2c-addr property\n");
		return -ENODEV;
	}

	rv = device_property_read_u8(&pdev->dev, "slave-addr", &slave_addr);
	if (rv)
		slave_addr = 0x20;

	return new_ssif_client(i2c_addr, NULL, 0,
			       slave_addr, SI_SMBIOS, &pdev->dev);
}
#else
static int dmi_ipmi_probe(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif

static const struct i2c_device_id ssif_id[] = {
	{ DEVICE_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssif_id);

static struct i2c_driver ssif_i2c_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver		= {
		.name			= DEVICE_NAME
	},
	.probe		= ssif_probe,
	.remove		= ssif_remove,
	.alert		= ssif_alert,
	.id_table	= ssif_id,
	.detect		= ssif_detect
};

static int ssif_platform_probe(struct platform_device *dev)
{
	return dmi_ipmi_probe(dev);
}

static int ssif_platform_remove(struct platform_device *dev)
{
	struct ssif_addr_info *addr_info = dev_get_drvdata(&dev->dev);

	mutex_lock(&ssif_infos_mutex);
	list_del(&addr_info->link);
	kfree(addr_info);
	mutex_unlock(&ssif_infos_mutex);
	return 0;
}

static const struct platform_device_id ssif_plat_ids[] = {
    { "dmi-ipmi-ssif", 0 },
    { }
};

static struct platform_driver ipmi_driver = {
	.driver = {
		.name = DEVICE_NAME,
	},
	.probe		= ssif_platform_probe,
	.remove		= ssif_platform_remove,
	.id_table       = ssif_plat_ids
};

static int __init init_ipmi_ssif(void)
{
	int i;
	int rv;

	if (initialized)
		return 0;

	pr_info("IPMI SSIF Interface driver\n");

	/* build list for i2c from addr list */
	for (i = 0; i < num_addrs; i++) {
		rv = new_ssif_client(addr[i], adapter_name[i],
				     dbg[i], slave_addrs[i],
				     SI_HARDCODED, NULL);
		if (rv)
			pr_err("Couldn't add hardcoded device at addr 0x%x\n",
			       addr[i]);
	}

	if (ssif_tryacpi)
		ssif_i2c_driver.driver.acpi_match_table	=
			ACPI_PTR(ssif_acpi_match);

	if (ssif_trydmi) {
		rv = platform_driver_register(&ipmi_driver);
		if (rv)
			pr_err("Unable to register driver: %d\n", rv);
		else
			platform_registered = true;
	}

	ssif_i2c_driver.address_list = ssif_address_list();

	rv = i2c_add_driver(&ssif_i2c_driver);
	if (!rv)
		initialized = true;

	return rv;
}
module_init(init_ipmi_ssif);

static void __exit cleanup_ipmi_ssif(void)
{
	if (!initialized)
		return;

	initialized = false;

	i2c_del_driver(&ssif_i2c_driver);

	kfree(ssif_i2c_driver.address_list);

	if (ssif_trydmi && platform_registered)
		platform_driver_unregister(&ipmi_driver);

	free_ssif_clients();
}
module_exit(cleanup_ipmi_ssif);

MODULE_ALIAS("platform:dmi-ipmi-ssif");
MODULE_AUTHOR("Todd C Davis <todd.c.davis@intel.com>, Corey Minyard <minyard@acm.org>");
MODULE_DESCRIPTION("IPMI driver for management controllers on a SMBus");
MODULE_LICENSE("GPL");
