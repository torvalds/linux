/******************************************************************************
 *  usb_atm.h - Generic USB xDSL driver core
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands, SolNegro, Josep Comas
 *  Copyright (C) 2004, David Woodhouse
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/config.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <asm/semaphore.h>

/*
#define DEBUG
#define VERBOSE_DEBUG
*/

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#	define DEBUG
#endif

#include <linux/usb.h>

#ifdef DEBUG
#define UDSL_ASSERT(x)	BUG_ON(!(x))
#else
#define UDSL_ASSERT(x)	do { if (!(x)) warn("failed assertion '" #x "' at line %d", __LINE__); } while(0)
#endif

#define UDSL_MAX_RCV_URBS		4
#define UDSL_MAX_SND_URBS		4
#define UDSL_MAX_RCV_BUFS		8
#define UDSL_MAX_SND_BUFS		8
#define UDSL_MAX_RCV_BUF_SIZE		1024	/* ATM cells */
#define UDSL_MAX_SND_BUF_SIZE		1024	/* ATM cells */
#define UDSL_DEFAULT_RCV_URBS		2
#define UDSL_DEFAULT_SND_URBS		2
#define UDSL_DEFAULT_RCV_BUFS		4
#define UDSL_DEFAULT_SND_BUFS		4
#define UDSL_DEFAULT_RCV_BUF_SIZE	64	/* ATM cells */
#define UDSL_DEFAULT_SND_BUF_SIZE	64	/* ATM cells */

#define ATM_CELL_HEADER			(ATM_CELL_SIZE - ATM_CELL_PAYLOAD)
#define UDSL_NUM_CELLS(x)		(((x) + ATM_AAL5_TRAILER + ATM_CELL_PAYLOAD - 1) / ATM_CELL_PAYLOAD)

/* receive */

struct udsl_receive_buffer {
	struct list_head list;
	unsigned char *base;
	unsigned int filled_cells;
};

struct udsl_receiver {
	struct list_head list;
	struct udsl_receive_buffer *buffer;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_vcc_data {
	/* vpi/vci lookup */
	struct list_head list;
	short vpi;
	int vci;
	struct atm_vcc *vcc;

	/* raw cell reassembly */
	struct sk_buff *sarb;
};

/* send */

struct udsl_send_buffer {
	struct list_head list;
	unsigned char *base;
	unsigned char *free_start;
	unsigned int free_cells;
};

struct udsl_sender {
	struct list_head list;
	struct udsl_send_buffer *buffer;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_control {
	struct atm_skb_data atm_data;
	unsigned int num_cells;
	unsigned int num_entire;
	unsigned int pdu_padding;
	unsigned char aal5_trailer[ATM_AAL5_TRAILER];
};

#define UDSL_SKB(x)		((struct udsl_control *)(x)->cb)

/* main driver data */

enum udsl_status {
	UDSL_NO_FIRMWARE,
	UDSL_LOADING_FIRMWARE,
	UDSL_LOADED_FIRMWARE
};

struct udsl_instance_data {
	struct kref refcount;
	struct semaphore serialize;

	/* USB device part */
	struct usb_device *usb_dev;
	char description[64];
	int data_endpoint;
	int snd_padding;
	int rcv_padding;
	const char *driver_name;

	/* ATM device part */
	struct atm_dev *atm_dev;
	struct list_head vcc_list;

	/* firmware */
	int (*firmware_wait) (struct udsl_instance_data *);
	enum udsl_status status;
	wait_queue_head_t firmware_waiters;

	/* receive */
	struct udsl_receiver receivers[UDSL_MAX_RCV_URBS];
	struct udsl_receive_buffer receive_buffers[UDSL_MAX_RCV_BUFS];

	spinlock_t receive_lock;
	struct list_head spare_receivers;
	struct list_head filled_receive_buffers;

	struct tasklet_struct receive_tasklet;
	struct list_head spare_receive_buffers;

	/* send */
	struct udsl_sender senders[UDSL_MAX_SND_URBS];
	struct udsl_send_buffer send_buffers[UDSL_MAX_SND_BUFS];

	struct sk_buff_head sndqueue;

	spinlock_t send_lock;
	struct list_head spare_senders;
	struct list_head spare_send_buffers;

	struct tasklet_struct send_tasklet;
	struct sk_buff *current_skb;			/* being emptied */
	struct udsl_send_buffer *current_buffer;	/* being filled */
	struct list_head filled_send_buffers;
};

extern int udsl_instance_setup(struct usb_device *dev,
			       struct udsl_instance_data *instance);
extern void udsl_instance_disconnect(struct udsl_instance_data *instance);
extern void udsl_get_instance(struct udsl_instance_data *instance);
extern void udsl_put_instance(struct udsl_instance_data *instance);
