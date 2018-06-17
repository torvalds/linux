// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 */

#ifndef __USBIP_STUB_H
#define __USBIP_STUB_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/wait.h>

#define STUB_BUSID_OTHER 0
#define STUB_BUSID_REMOV 1
#define STUB_BUSID_ADDED 2
#define STUB_BUSID_ALLOC 3

struct stub_device {
	struct usb_device *udev;

	struct usbip_device ud;
	__u32 devid;

	/*
	 * stub_priv preserves private data of each urb.
	 * It is allocated as stub_priv_cache and assigned to urb->context.
	 *
	 * stub_priv is always linked to any one of 3 lists;
	 *	priv_init: linked to this until the comletion of a urb.
	 *	priv_tx  : linked to this after the completion of a urb.
	 *	priv_free: linked to this after the sending of the result.
	 *
	 * Any of these list operations should be locked by priv_lock.
	 */
	spinlock_t priv_lock;
	struct list_head priv_init;
	struct list_head priv_tx;
	struct list_head priv_free;

	/* see comments for unlinking in stub_rx.c */
	struct list_head unlink_tx;
	struct list_head unlink_free;

	wait_queue_head_t tx_waitq;
};

/* private data into urb->priv */
struct stub_priv {
	unsigned long seqnum;
	struct list_head list;
	struct stub_device *sdev;
	struct urb *urb;

	int unlinking;
};

struct stub_unlink {
	unsigned long seqnum;
	struct list_head list;
	__u32 status;
};

/* same as SYSFS_BUS_ID_SIZE */
#define BUSID_SIZE 32

struct bus_id_priv {
	char name[BUSID_SIZE];
	char status;
	int interf_count;
	struct stub_device *sdev;
	struct usb_device *udev;
	char shutdown_busid;
	spinlock_t busid_lock;
};

/* stub_priv is allocated from stub_priv_cache */
extern struct kmem_cache *stub_priv_cache;

/* stub_dev.c */
extern struct usb_device_driver stub_driver;

/* stub_main.c */
struct bus_id_priv *get_busid_priv(const char *busid);
void put_busid_priv(struct bus_id_priv *bid);
int del_match_busid(char *busid);
void stub_device_cleanup_urbs(struct stub_device *sdev);

/* stub_rx.c */
int stub_rx_loop(void *data);

/* stub_tx.c */
void stub_enqueue_ret_unlink(struct stub_device *sdev, __u32 seqnum,
			     __u32 status);
void stub_complete(struct urb *urb);
int stub_tx_loop(void *data);

#endif /* __USBIP_STUB_H */
