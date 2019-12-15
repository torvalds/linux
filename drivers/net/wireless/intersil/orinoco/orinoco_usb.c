/*
 * USB Orinoco driver
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 *
 * Queueing code based on linux-wlan-ng 0.2.1-pre5
 *
 * Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 *
 *	The license is the same as above.
 *
 * Initialy based on USB Skeleton driver - 0.7
 *
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 * NOTE: The original USB Skeleton driver is GPL, but all that code is
 * gone so MPL/GPL applies.
 */

#define DRIVER_NAME "orinoco_usb"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/timer.h>

#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/refcount.h>

#include "mic.h"
#include "orinoco.h"

#ifndef URB_ASYNC_UNLINK
#define URB_ASYNC_UNLINK 0
#endif

/* 802.2 LLC/SNAP header used for Ethernet encapsulation over 802.11 */
static const u8 encaps_hdr[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
#define ENCAPS_OVERHEAD		(sizeof(encaps_hdr) + 2)

struct header_struct {
	/* 802.3 */
	u8 dest[ETH_ALEN];
	u8 src[ETH_ALEN];
	__be16 len;
	/* 802.2 */
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	/* SNAP */
	u8 oui[3];
	__be16 ethertype;
} __packed;

struct ez_usb_fw {
	u16 size;
	const u8 *code;
};

static struct ez_usb_fw firmware = {
	.size = 0,
	.code = NULL,
};

/* Debugging macros */
#undef err
#define err(format, arg...) \
	do { printk(KERN_ERR PFX format "\n", ## arg); } while (0)

MODULE_FIRMWARE("orinoco_ezusb_fw");

/*
 * Under some conditions, the card gets stuck and stops paying attention
 * to the world (i.e. data communication stalls) until we do something to
 * it.  Sending an INQ_TALLIES command seems to be enough and should be
 * harmless otherwise.  This behaviour has been observed when using the
 * driver on a systemimager client during installation.  In the past a
 * timer was used to send INQ_TALLIES commands when there was no other
 * activity, but it was troublesome and was removed.
 */

#define USB_COMPAQ_VENDOR_ID     0x049f /* Compaq Computer Corp. */
#define USB_COMPAQ_WL215_ID      0x001f /* Compaq WL215 USB Adapter */
#define USB_COMPAQ_W200_ID       0x0076 /* Compaq W200 USB Adapter */
#define USB_HP_WL215_ID          0x0082 /* Compaq WL215 USB Adapter */

#define USB_MELCO_VENDOR_ID      0x0411
#define USB_BUFFALO_L11_ID       0x0006 /* BUFFALO WLI-USB-L11 */
#define USB_BUFFALO_L11G_WR_ID   0x000B /* BUFFALO WLI-USB-L11G-WR */
#define USB_BUFFALO_L11G_ID      0x000D /* BUFFALO WLI-USB-L11G */

#define USB_LUCENT_VENDOR_ID     0x047E /* Lucent Technologies */
#define USB_LUCENT_ORINOCO_ID    0x0300 /* Lucent/Agere Orinoco USB Client */

#define USB_AVAYA8_VENDOR_ID     0x0D98
#define USB_AVAYAE_VENDOR_ID     0x0D9E
#define USB_AVAYA_WIRELESS_ID    0x0300 /* Avaya Wireless USB Card */

#define USB_AGERE_VENDOR_ID      0x0D4E /* Agere Systems */
#define USB_AGERE_MODEL0801_ID   0x1000 /* Wireless USB Card Model 0801 */
#define USB_AGERE_MODEL0802_ID   0x1001 /* Wireless USB Card Model 0802 */
#define USB_AGERE_REBRANDED_ID   0x047A /* WLAN USB Card */

#define USB_ELSA_VENDOR_ID       0x05CC
#define USB_ELSA_AIRLANCER_ID    0x3100 /* ELSA AirLancer USB-11 */

#define USB_LEGEND_VENDOR_ID     0x0E7C
#define USB_LEGEND_JOYNET_ID     0x0300 /* Joynet WLAN USB Card */

#define USB_SAMSUNG_VENDOR_ID    0x04E8
#define USB_SAMSUNG_SEW2001U1_ID 0x5002 /* Samsung SEW-2001u Card */
#define USB_SAMSUNG_SEW2001U2_ID 0x5B11 /* Samsung SEW-2001u Card */
#define USB_SAMSUNG_SEW2003U_ID  0x7011 /* Samsung SEW-2003U Card */

#define USB_IGATE_VENDOR_ID      0x0681
#define USB_IGATE_IGATE_11M_ID   0x0012 /* I-GATE 11M USB Card */

#define USB_FUJITSU_VENDOR_ID    0x0BF8
#define USB_FUJITSU_E1100_ID     0x1002 /* connect2AIR WLAN E-1100 USB */

#define USB_2WIRE_VENDOR_ID      0x1630
#define USB_2WIRE_WIRELESS_ID    0xff81 /* 2Wire Wireless USB adapter */


#define EZUSB_REQUEST_FW_TRANS		0xA0
#define EZUSB_REQUEST_TRIGER		0xAA
#define EZUSB_REQUEST_TRIG_AC		0xAC
#define EZUSB_CPUCS_REG			0x7F92

#define EZUSB_RID_TX			0x0700
#define EZUSB_RID_RX			0x0701
#define EZUSB_RID_INIT1			0x0702
#define EZUSB_RID_ACK			0x0710
#define EZUSB_RID_READ_PDA		0x0800
#define EZUSB_RID_PROG_INIT		0x0852
#define EZUSB_RID_PROG_SET_ADDR		0x0853
#define EZUSB_RID_PROG_BYTES		0x0854
#define EZUSB_RID_PROG_END		0x0855
#define EZUSB_RID_DOCMD			0x0860

/* Recognize info frames */
#define EZUSB_IS_INFO(id)		((id >= 0xF000) && (id <= 0xF2FF))

#define EZUSB_MAGIC			0x0210

#define EZUSB_FRAME_DATA		1
#define EZUSB_FRAME_CONTROL		2

#define DEF_TIMEOUT			(3 * HZ)

#define BULK_BUF_SIZE			2048

#define MAX_DL_SIZE (BULK_BUF_SIZE - sizeof(struct ezusb_packet))

#define FW_BUF_SIZE			64
#define FW_VAR_OFFSET_PTR		0x359
#define FW_VAR_VALUE			0
#define FW_HOLE_START			0x100
#define FW_HOLE_END			0x300

struct ezusb_packet {
	__le16 magic;		/* 0x0210 */
	u8 req_reply_count;
	u8 ans_reply_count;
	__le16 frame_type;	/* 0x01 for data frames, 0x02 otherwise */
	__le16 size;		/* transport size */
	__le16 crc;		/* CRC up to here */
	__le16 hermes_len;
	__le16 hermes_rid;
	u8 data[0];
} __packed;

/* Table of devices that work or may work with this driver */
static const struct usb_device_id ezusb_table[] = {
	{USB_DEVICE(USB_COMPAQ_VENDOR_ID, USB_COMPAQ_WL215_ID)},
	{USB_DEVICE(USB_COMPAQ_VENDOR_ID, USB_HP_WL215_ID)},
	{USB_DEVICE(USB_COMPAQ_VENDOR_ID, USB_COMPAQ_W200_ID)},
	{USB_DEVICE(USB_MELCO_VENDOR_ID, USB_BUFFALO_L11_ID)},
	{USB_DEVICE(USB_MELCO_VENDOR_ID, USB_BUFFALO_L11G_WR_ID)},
	{USB_DEVICE(USB_MELCO_VENDOR_ID, USB_BUFFALO_L11G_ID)},
	{USB_DEVICE(USB_LUCENT_VENDOR_ID, USB_LUCENT_ORINOCO_ID)},
	{USB_DEVICE(USB_AVAYA8_VENDOR_ID, USB_AVAYA_WIRELESS_ID)},
	{USB_DEVICE(USB_AVAYAE_VENDOR_ID, USB_AVAYA_WIRELESS_ID)},
	{USB_DEVICE(USB_AGERE_VENDOR_ID, USB_AGERE_MODEL0801_ID)},
	{USB_DEVICE(USB_AGERE_VENDOR_ID, USB_AGERE_MODEL0802_ID)},
	{USB_DEVICE(USB_ELSA_VENDOR_ID, USB_ELSA_AIRLANCER_ID)},
	{USB_DEVICE(USB_LEGEND_VENDOR_ID, USB_LEGEND_JOYNET_ID)},
	{USB_DEVICE_VER(USB_SAMSUNG_VENDOR_ID, USB_SAMSUNG_SEW2001U1_ID,
			0, 0)},
	{USB_DEVICE(USB_SAMSUNG_VENDOR_ID, USB_SAMSUNG_SEW2001U2_ID)},
	{USB_DEVICE(USB_SAMSUNG_VENDOR_ID, USB_SAMSUNG_SEW2003U_ID)},
	{USB_DEVICE(USB_IGATE_VENDOR_ID, USB_IGATE_IGATE_11M_ID)},
	{USB_DEVICE(USB_FUJITSU_VENDOR_ID, USB_FUJITSU_E1100_ID)},
	{USB_DEVICE(USB_2WIRE_VENDOR_ID, USB_2WIRE_WIRELESS_ID)},
	{USB_DEVICE(USB_AGERE_VENDOR_ID, USB_AGERE_REBRANDED_ID)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ezusb_table);

/* Structure to hold all of our device specific stuff */
struct ezusb_priv {
	struct usb_device *udev;
	struct net_device *dev;
	struct mutex mtx;
	spinlock_t req_lock;
	struct list_head req_pending;
	struct list_head req_active;
	spinlock_t reply_count_lock;
	u16 hermes_reg_fake[0x40];
	u8 *bap_buf;
	struct urb *read_urb;
	int read_pipe;
	int write_pipe;
	u8 reply_count;
};

enum ezusb_state {
	EZUSB_CTX_START,
	EZUSB_CTX_QUEUED,
	EZUSB_CTX_REQ_SUBMITTED,
	EZUSB_CTX_REQ_COMPLETE,
	EZUSB_CTX_RESP_RECEIVED,
	EZUSB_CTX_REQ_TIMEOUT,
	EZUSB_CTX_REQ_FAILED,
	EZUSB_CTX_RESP_TIMEOUT,
	EZUSB_CTX_REQSUBMIT_FAIL,
	EZUSB_CTX_COMPLETE,
};

struct request_context {
	struct list_head list;
	refcount_t refcount;
	struct completion done;	/* Signals that CTX is dead */
	int killed;
	struct urb *outurb;	/* OUT for req pkt */
	struct ezusb_priv *upriv;
	struct ezusb_packet *buf;
	int buf_length;
	struct timer_list timer;	/* Timeout handling */
	enum ezusb_state state;	/* Current state */
	/* the RID that we will wait for */
	u16 out_rid;
	u16 in_rid;
};


/* Forward declarations */
static void ezusb_ctx_complete(struct request_context *ctx);
static void ezusb_req_queue_run(struct ezusb_priv *upriv);
static void ezusb_bulk_in_callback(struct urb *urb);

static inline u8 ezusb_reply_inc(u8 count)
{
	if (count < 0x7F)
		return count + 1;
	else
		return 1;
}

static void ezusb_request_context_put(struct request_context *ctx)
{
	if (!refcount_dec_and_test(&ctx->refcount))
		return;

	WARN_ON(!ctx->done.done);
	BUG_ON(ctx->outurb->status == -EINPROGRESS);
	BUG_ON(timer_pending(&ctx->timer));
	usb_free_urb(ctx->outurb);
	kfree(ctx->buf);
	kfree(ctx);
}

static inline void ezusb_mod_timer(struct ezusb_priv *upriv,
				   struct timer_list *timer,
				   unsigned long expire)
{
	if (!upriv->udev)
		return;
	mod_timer(timer, expire);
}

static void ezusb_request_timerfn(struct timer_list *t)
{
	struct request_context *ctx = from_timer(ctx, t, timer);

	ctx->outurb->transfer_flags |= URB_ASYNC_UNLINK;
	if (usb_unlink_urb(ctx->outurb) == -EINPROGRESS) {
		ctx->state = EZUSB_CTX_REQ_TIMEOUT;
	} else {
		ctx->state = EZUSB_CTX_RESP_TIMEOUT;
		dev_dbg(&ctx->outurb->dev->dev, "couldn't unlink\n");
		refcount_inc(&ctx->refcount);
		ctx->killed = 1;
		ezusb_ctx_complete(ctx);
		ezusb_request_context_put(ctx);
	}
};

static struct request_context *ezusb_alloc_ctx(struct ezusb_priv *upriv,
					       u16 out_rid, u16 in_rid)
{
	struct request_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return NULL;

	ctx->buf = kmalloc(BULK_BUF_SIZE, GFP_ATOMIC);
	if (!ctx->buf) {
		kfree(ctx);
		return NULL;
	}
	ctx->outurb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!ctx->outurb) {
		kfree(ctx->buf);
		kfree(ctx);
		return NULL;
	}

	ctx->upriv = upriv;
	ctx->state = EZUSB_CTX_START;
	ctx->out_rid = out_rid;
	ctx->in_rid = in_rid;

	refcount_set(&ctx->refcount, 1);
	init_completion(&ctx->done);

	timer_setup(&ctx->timer, ezusb_request_timerfn, 0);
	return ctx;
}


/* Hopefully the real complete_all will soon be exported, in the mean
 * while this should work. */
static inline void ezusb_complete_all(struct completion *comp)
{
	complete(comp);
	complete(comp);
	complete(comp);
	complete(comp);
}

static void ezusb_ctx_complete(struct request_context *ctx)
{
	struct ezusb_priv *upriv = ctx->upriv;
	unsigned long flags;

	spin_lock_irqsave(&upriv->req_lock, flags);

	list_del_init(&ctx->list);
	if (upriv->udev) {
		spin_unlock_irqrestore(&upriv->req_lock, flags);
		ezusb_req_queue_run(upriv);
		spin_lock_irqsave(&upriv->req_lock, flags);
	}

	switch (ctx->state) {
	case EZUSB_CTX_COMPLETE:
	case EZUSB_CTX_REQSUBMIT_FAIL:
	case EZUSB_CTX_REQ_FAILED:
	case EZUSB_CTX_REQ_TIMEOUT:
	case EZUSB_CTX_RESP_TIMEOUT:
		spin_unlock_irqrestore(&upriv->req_lock, flags);

		if ((ctx->out_rid == EZUSB_RID_TX) && upriv->dev) {
			struct net_device *dev = upriv->dev;
			struct net_device_stats *stats = &dev->stats;

			if (ctx->state != EZUSB_CTX_COMPLETE)
				stats->tx_errors++;
			else
				stats->tx_packets++;

			netif_wake_queue(dev);
		}
		ezusb_complete_all(&ctx->done);
		ezusb_request_context_put(ctx);
		break;

	default:
		spin_unlock_irqrestore(&upriv->req_lock, flags);
		if (!upriv->udev) {
			/* This is normal, as all request contexts get flushed
			 * when the device is disconnected */
			err("Called, CTX not terminating, but device gone");
			ezusb_complete_all(&ctx->done);
			ezusb_request_context_put(ctx);
			break;
		}

		err("Called, CTX not in terminating state.");
		/* Things are really bad if this happens. Just leak
		 * the CTX because it may still be linked to the
		 * queue or the OUT urb may still be active.
		 * Just leaking at least prevents an Oops or Panic.
		 */
		break;
	}
}

/**
 * ezusb_req_queue_run:
 * Description:
 *	Note: Only one active CTX at any one time, because there's no
 *	other (reliable) way to match the response URB to the correct
 *	CTX.
 **/
static void ezusb_req_queue_run(struct ezusb_priv *upriv)
{
	unsigned long flags;
	struct request_context *ctx;
	int result;

	spin_lock_irqsave(&upriv->req_lock, flags);

	if (!list_empty(&upriv->req_active))
		goto unlock;

	if (list_empty(&upriv->req_pending))
		goto unlock;

	ctx =
	    list_entry(upriv->req_pending.next, struct request_context,
		       list);

	if (!ctx->upriv->udev)
		goto unlock;

	/* We need to split this off to avoid a race condition */
	list_move_tail(&ctx->list, &upriv->req_active);

	if (ctx->state == EZUSB_CTX_QUEUED) {
		refcount_inc(&ctx->refcount);
		result = usb_submit_urb(ctx->outurb, GFP_ATOMIC);
		if (result) {
			ctx->state = EZUSB_CTX_REQSUBMIT_FAIL;

			spin_unlock_irqrestore(&upriv->req_lock, flags);

			err("Fatal, failed to submit command urb."
			    " error=%d\n", result);

			ezusb_ctx_complete(ctx);
			ezusb_request_context_put(ctx);
			goto done;
		}

		ctx->state = EZUSB_CTX_REQ_SUBMITTED;
		ezusb_mod_timer(ctx->upriv, &ctx->timer,
				jiffies + DEF_TIMEOUT);
	}

 unlock:
	spin_unlock_irqrestore(&upriv->req_lock, flags);

 done:
	return;
}

static void ezusb_req_enqueue_run(struct ezusb_priv *upriv,
				  struct request_context *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&upriv->req_lock, flags);

	if (!ctx->upriv->udev) {
		spin_unlock_irqrestore(&upriv->req_lock, flags);
		goto done;
	}
	refcount_inc(&ctx->refcount);
	list_add_tail(&ctx->list, &upriv->req_pending);
	spin_unlock_irqrestore(&upriv->req_lock, flags);

	ctx->state = EZUSB_CTX_QUEUED;
	ezusb_req_queue_run(upriv);

 done:
	return;
}

static void ezusb_request_out_callback(struct urb *urb)
{
	unsigned long flags;
	enum ezusb_state state;
	struct request_context *ctx = urb->context;
	struct ezusb_priv *upriv = ctx->upriv;

	spin_lock_irqsave(&upriv->req_lock, flags);

	del_timer(&ctx->timer);

	if (ctx->killed) {
		spin_unlock_irqrestore(&upriv->req_lock, flags);
		pr_warn("interrupt called with dead ctx\n");
		goto out;
	}

	state = ctx->state;

	if (urb->status == 0) {
		switch (state) {
		case EZUSB_CTX_REQ_SUBMITTED:
			if (ctx->in_rid) {
				ctx->state = EZUSB_CTX_REQ_COMPLETE;
				/* reply URB still pending */
				ezusb_mod_timer(upriv, &ctx->timer,
						jiffies + DEF_TIMEOUT);
				spin_unlock_irqrestore(&upriv->req_lock,
						       flags);
				break;
			}
			/* fall through */
		case EZUSB_CTX_RESP_RECEIVED:
			/* IN already received before this OUT-ACK */
			ctx->state = EZUSB_CTX_COMPLETE;
			spin_unlock_irqrestore(&upriv->req_lock, flags);
			ezusb_ctx_complete(ctx);
			break;

		default:
			spin_unlock_irqrestore(&upriv->req_lock, flags);
			err("Unexpected state(0x%x, %d) in OUT URB",
			    state, urb->status);
			break;
		}
	} else {
		/* If someone cancels the OUT URB then its status
		 * should be either -ECONNRESET or -ENOENT.
		 */
		switch (state) {
		case EZUSB_CTX_REQ_SUBMITTED:
		case EZUSB_CTX_RESP_RECEIVED:
			ctx->state = EZUSB_CTX_REQ_FAILED;
			/* fall through */

		case EZUSB_CTX_REQ_FAILED:
		case EZUSB_CTX_REQ_TIMEOUT:
			spin_unlock_irqrestore(&upriv->req_lock, flags);

			ezusb_ctx_complete(ctx);
			break;

		default:
			spin_unlock_irqrestore(&upriv->req_lock, flags);

			err("Unexpected state(0x%x, %d) in OUT URB",
			    state, urb->status);
			break;
		}
	}
 out:
	ezusb_request_context_put(ctx);
}

static void ezusb_request_in_callback(struct ezusb_priv *upriv,
				      struct urb *urb)
{
	struct ezusb_packet *ans = urb->transfer_buffer;
	struct request_context *ctx = NULL;
	enum ezusb_state state;
	unsigned long flags;

	/* Find the CTX on the active queue that requested this URB */
	spin_lock_irqsave(&upriv->req_lock, flags);
	if (upriv->udev) {
		struct list_head *item;

		list_for_each(item, &upriv->req_active) {
			struct request_context *c;
			int reply_count;

			c = list_entry(item, struct request_context, list);
			reply_count =
			    ezusb_reply_inc(c->buf->req_reply_count);
			if ((ans->ans_reply_count == reply_count)
			    && (le16_to_cpu(ans->hermes_rid) == c->in_rid)) {
				ctx = c;
				break;
			}
			netdev_dbg(upriv->dev, "Skipped (0x%x/0x%x) (%d/%d)\n",
				   le16_to_cpu(ans->hermes_rid), c->in_rid,
				   ans->ans_reply_count, reply_count);
		}
	}

	if (ctx == NULL) {
		spin_unlock_irqrestore(&upriv->req_lock, flags);
		err("%s: got unexpected RID: 0x%04X", __func__,
		    le16_to_cpu(ans->hermes_rid));
		ezusb_req_queue_run(upriv);
		return;
	}

	/* The data we want is in the in buffer, exchange */
	urb->transfer_buffer = ctx->buf;
	ctx->buf = (void *) ans;
	ctx->buf_length = urb->actual_length;

	state = ctx->state;
	switch (state) {
	case EZUSB_CTX_REQ_SUBMITTED:
		/* We have received our response URB before
		 * our request has been acknowledged. Do NOT
		 * destroy our CTX yet, because our OUT URB
		 * is still alive ...
		 */
		ctx->state = EZUSB_CTX_RESP_RECEIVED;
		spin_unlock_irqrestore(&upriv->req_lock, flags);

		/* Let the machine continue running. */
		break;

	case EZUSB_CTX_REQ_COMPLETE:
		/* This is the usual path: our request
		 * has already been acknowledged, and
		 * we have now received the reply.
		 */
		ctx->state = EZUSB_CTX_COMPLETE;

		/* Stop the intimer */
		del_timer(&ctx->timer);
		spin_unlock_irqrestore(&upriv->req_lock, flags);

		/* Call the completion handler */
		ezusb_ctx_complete(ctx);
		break;

	default:
		spin_unlock_irqrestore(&upriv->req_lock, flags);

		pr_warn("Matched IN URB, unexpected context state(0x%x)\n",
			state);
		/* Throw this CTX away and try submitting another */
		del_timer(&ctx->timer);
		ctx->outurb->transfer_flags |= URB_ASYNC_UNLINK;
		usb_unlink_urb(ctx->outurb);
		ezusb_req_queue_run(upriv);
		break;
	}			/* switch */
}


static void ezusb_req_ctx_wait(struct ezusb_priv *upriv,
			       struct request_context *ctx)
{
	switch (ctx->state) {
	case EZUSB_CTX_QUEUED:
	case EZUSB_CTX_REQ_SUBMITTED:
	case EZUSB_CTX_REQ_COMPLETE:
	case EZUSB_CTX_RESP_RECEIVED:
		if (in_softirq()) {
			/* If we get called from a timer, timeout timers don't
			 * get the chance to run themselves. So we make sure
			 * that we don't sleep for ever */
			int msecs = DEF_TIMEOUT * (1000 / HZ);
			while (!ctx->done.done && msecs--)
				udelay(1000);
		} else {
			wait_event_interruptible(ctx->done.wait,
						 ctx->done.done);
		}
		break;
	default:
		/* Done or failed - nothing to wait for */
		break;
	}
}

static inline u16 build_crc(struct ezusb_packet *data)
{
	u16 crc = 0;
	u8 *bytes = (u8 *)data;
	int i;

	for (i = 0; i < 8; i++)
		crc = (crc << 1) + bytes[i];

	return crc;
}

/**
 * ezusb_fill_req:
 *
 * if data == NULL and length > 0 the data is assumed to be already in
 * the target buffer and only the header is filled.
 *
 */
static int ezusb_fill_req(struct ezusb_packet *req, u16 length, u16 rid,
			  const void *data, u16 frame_type, u8 reply_count)
{
	int total_size = sizeof(*req) + length;

	BUG_ON(total_size > BULK_BUF_SIZE);

	req->magic = cpu_to_le16(EZUSB_MAGIC);
	req->req_reply_count = reply_count;
	req->ans_reply_count = 0;
	req->frame_type = cpu_to_le16(frame_type);
	req->size = cpu_to_le16(length + 4);
	req->crc = cpu_to_le16(build_crc(req));
	req->hermes_len = cpu_to_le16(HERMES_BYTES_TO_RECLEN(length));
	req->hermes_rid = cpu_to_le16(rid);
	if (data)
		memcpy(req->data, data, length);
	return total_size;
}

static int ezusb_submit_in_urb(struct ezusb_priv *upriv)
{
	int retval = 0;
	void *cur_buf = upriv->read_urb->transfer_buffer;

	if (upriv->read_urb->status == -EINPROGRESS) {
		netdev_dbg(upriv->dev, "urb busy, not resubmiting\n");
		retval = -EBUSY;
		goto exit;
	}
	usb_fill_bulk_urb(upriv->read_urb, upriv->udev, upriv->read_pipe,
			  cur_buf, BULK_BUF_SIZE,
			  ezusb_bulk_in_callback, upriv);
	upriv->read_urb->transfer_flags = 0;
	retval = usb_submit_urb(upriv->read_urb, GFP_ATOMIC);
	if (retval)
		err("%s submit failed %d", __func__, retval);

 exit:
	return retval;
}

static inline int ezusb_8051_cpucs(struct ezusb_priv *upriv, int reset)
{
	int ret;
	u8 *res_val = NULL;

	if (!upriv->udev) {
		err("%s: !upriv->udev", __func__);
		return -EFAULT;
	}

	res_val = kmalloc(sizeof(*res_val), GFP_KERNEL);

	if (!res_val)
		return -ENOMEM;

	*res_val = reset;	/* avoid argument promotion */

	ret =  usb_control_msg(upriv->udev,
			       usb_sndctrlpipe(upriv->udev, 0),
			       EZUSB_REQUEST_FW_TRANS,
			       USB_TYPE_VENDOR | USB_RECIP_DEVICE |
			       USB_DIR_OUT, EZUSB_CPUCS_REG, 0, res_val,
			       sizeof(*res_val), DEF_TIMEOUT);

	kfree(res_val);

	return ret;
}

static int ezusb_firmware_download(struct ezusb_priv *upriv,
				   struct ez_usb_fw *fw)
{
	u8 *fw_buffer;
	int retval, addr;
	int variant_offset;

	fw_buffer = kmalloc(FW_BUF_SIZE, GFP_KERNEL);
	if (!fw_buffer) {
		printk(KERN_ERR PFX "Out of memory for firmware buffer.\n");
		return -ENOMEM;
	}
	/*
	 * This byte is 1 and should be replaced with 0.  The offset is
	 * 0x10AD in version 0.0.6.  The byte in question should follow
	 * the end of the code pointed to by the jump in the beginning
	 * of the firmware.  Also, it is read by code located at 0x358.
	 */
	variant_offset = be16_to_cpup((__be16 *) &fw->code[FW_VAR_OFFSET_PTR]);
	if (variant_offset >= fw->size) {
		printk(KERN_ERR PFX "Invalid firmware variant offset: "
		       "0x%04x\n", variant_offset);
		retval = -EINVAL;
		goto fail;
	}

	retval = ezusb_8051_cpucs(upriv, 1);
	if (retval < 0)
		goto fail;
	for (addr = 0; addr < fw->size; addr += FW_BUF_SIZE) {
		/* 0x100-0x300 should be left alone, it contains card
		 * specific data, like USB enumeration information */
		if ((addr >= FW_HOLE_START) && (addr < FW_HOLE_END))
			continue;

		memcpy(fw_buffer, &fw->code[addr], FW_BUF_SIZE);
		if (variant_offset >= addr &&
		    variant_offset < addr + FW_BUF_SIZE) {
			netdev_dbg(upriv->dev,
				   "Patching card_variant byte at 0x%04X\n",
				   variant_offset);
			fw_buffer[variant_offset - addr] = FW_VAR_VALUE;
		}
		retval = usb_control_msg(upriv->udev,
					 usb_sndctrlpipe(upriv->udev, 0),
					 EZUSB_REQUEST_FW_TRANS,
					 USB_TYPE_VENDOR | USB_RECIP_DEVICE
					 | USB_DIR_OUT,
					 addr, 0x0,
					 fw_buffer, FW_BUF_SIZE,
					 DEF_TIMEOUT);

		if (retval < 0)
			goto fail;
	}
	retval = ezusb_8051_cpucs(upriv, 0);
	if (retval < 0)
		goto fail;

	goto exit;
 fail:
	printk(KERN_ERR PFX "Firmware download failed, error %d\n",
	       retval);
 exit:
	kfree(fw_buffer);
	return retval;
}

static int ezusb_access_ltv(struct ezusb_priv *upriv,
			    struct request_context *ctx,
			    u16 length, const void *data, u16 frame_type,
			    void *ans_buff, unsigned ans_size, u16 *ans_length)
{
	int req_size;
	int retval = 0;
	enum ezusb_state state;

	BUG_ON(in_irq());

	if (!upriv->udev) {
		retval = -ENODEV;
		goto exit;
	}

	if (upriv->read_urb->status != -EINPROGRESS)
		err("%s: in urb not pending", __func__);

	/* protect upriv->reply_count, guarantee sequential numbers */
	spin_lock_bh(&upriv->reply_count_lock);
	req_size = ezusb_fill_req(ctx->buf, length, ctx->out_rid, data,
				  frame_type, upriv->reply_count);
	usb_fill_bulk_urb(ctx->outurb, upriv->udev, upriv->write_pipe,
			  ctx->buf, req_size,
			  ezusb_request_out_callback, ctx);

	if (ctx->in_rid)
		upriv->reply_count = ezusb_reply_inc(upriv->reply_count);

	ezusb_req_enqueue_run(upriv, ctx);

	spin_unlock_bh(&upriv->reply_count_lock);

	if (ctx->in_rid)
		ezusb_req_ctx_wait(upriv, ctx);

	state = ctx->state;
	switch (state) {
	case EZUSB_CTX_COMPLETE:
		retval = ctx->outurb->status;
		break;

	case EZUSB_CTX_QUEUED:
	case EZUSB_CTX_REQ_SUBMITTED:
		if (!ctx->in_rid)
			break;
	default:
		err("%s: Unexpected context state %d", __func__,
		    state);
		/* fall though */
	case EZUSB_CTX_REQ_TIMEOUT:
	case EZUSB_CTX_REQ_FAILED:
	case EZUSB_CTX_RESP_TIMEOUT:
	case EZUSB_CTX_REQSUBMIT_FAIL:
		printk(KERN_ERR PFX "Access failed, resetting (state %d,"
		       " reply_count %d)\n", state, upriv->reply_count);
		upriv->reply_count = 0;
		if (state == EZUSB_CTX_REQ_TIMEOUT
		    || state == EZUSB_CTX_RESP_TIMEOUT) {
			printk(KERN_ERR PFX "ctx timed out\n");
			retval = -ETIMEDOUT;
		} else {
			printk(KERN_ERR PFX "ctx failed\n");
			retval = -EFAULT;
		}
		goto exit;
	}
	if (ctx->in_rid) {
		struct ezusb_packet *ans = ctx->buf;
		unsigned exp_len;

		if (ans->hermes_len != 0)
			exp_len = le16_to_cpu(ans->hermes_len) * 2 + 12;
		else
			exp_len = 14;

		if (exp_len != ctx->buf_length) {
			err("%s: length mismatch for RID 0x%04x: "
			    "expected %d, got %d", __func__,
			    ctx->in_rid, exp_len, ctx->buf_length);
			retval = -EIO;
			goto exit;
		}

		if (ans_buff)
			memcpy(ans_buff, ans->data, min(exp_len, ans_size));
		if (ans_length)
			*ans_length = le16_to_cpu(ans->hermes_len);
	}
 exit:
	ezusb_request_context_put(ctx);
	return retval;
}

static int ezusb_write_ltv(struct hermes *hw, int bap, u16 rid,
			   u16 length, const void *data)
{
	struct ezusb_priv *upriv = hw->priv;
	u16 frame_type;
	struct request_context *ctx;

	if (length == 0)
		return -EINVAL;

	length = HERMES_RECLEN_TO_BYTES(length);

	/* On memory mapped devices HERMES_RID_CNFGROUPADDRESSES can be
	 * set to be empty, but the USB bridge doesn't like it */
	if (length == 0)
		return 0;

	ctx = ezusb_alloc_ctx(upriv, rid, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	if (rid == EZUSB_RID_TX)
		frame_type = EZUSB_FRAME_DATA;
	else
		frame_type = EZUSB_FRAME_CONTROL;

	return ezusb_access_ltv(upriv, ctx, length, data, frame_type,
				NULL, 0, NULL);
}

static int ezusb_read_ltv(struct hermes *hw, int bap, u16 rid,
			  unsigned bufsize, u16 *length, void *buf)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;

	if (bufsize % 2)
		return -EINVAL;

	ctx = ezusb_alloc_ctx(upriv, rid, rid);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, 0, NULL, EZUSB_FRAME_CONTROL,
				buf, bufsize, length);
}

static int ezusb_doicmd_wait(struct hermes *hw, u16 cmd, u16 parm0, u16 parm1,
			     u16 parm2, struct hermes_response *resp)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;

	__le16 data[4] = {
		cpu_to_le16(cmd),
		cpu_to_le16(parm0),
		cpu_to_le16(parm1),
		cpu_to_le16(parm2),
	};
	netdev_dbg(upriv->dev,
		   "0x%04X, parm0 0x%04X, parm1 0x%04X, parm2 0x%04X\n", cmd,
		   parm0, parm1, parm2);
	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_DOCMD, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, sizeof(data), &data,
				EZUSB_FRAME_CONTROL, NULL, 0, NULL);
}

static int ezusb_docmd_wait(struct hermes *hw, u16 cmd, u16 parm0,
			    struct hermes_response *resp)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;

	__le16 data[4] = {
		cpu_to_le16(cmd),
		cpu_to_le16(parm0),
		0,
		0,
	};
	netdev_dbg(upriv->dev, "0x%04X, parm0 0x%04X\n", cmd, parm0);
	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_DOCMD, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, sizeof(data), &data,
				EZUSB_FRAME_CONTROL, NULL, 0, NULL);
}

static int ezusb_bap_pread(struct hermes *hw, int bap,
			   void *buf, int len, u16 id, u16 offset)
{
	struct ezusb_priv *upriv = hw->priv;
	struct ezusb_packet *ans = (void *) upriv->read_urb->transfer_buffer;
	int actual_length = upriv->read_urb->actual_length;

	if (id == EZUSB_RID_RX) {
		if ((sizeof(*ans) + offset + len) > actual_length) {
			printk(KERN_ERR PFX "BAP read beyond buffer end "
			       "in rx frame\n");
			return -EINVAL;
		}
		memcpy(buf, ans->data + offset, len);
		return 0;
	}

	if (EZUSB_IS_INFO(id)) {
		/* Include 4 bytes for length/type */
		if ((sizeof(*ans) + offset + len - 4) > actual_length) {
			printk(KERN_ERR PFX "BAP read beyond buffer end "
			       "in info frame\n");
			return -EFAULT;
		}
		memcpy(buf, ans->data + offset - 4, len);
	} else {
		printk(KERN_ERR PFX "Unexpected fid 0x%04x\n", id);
		return -EINVAL;
	}

	return 0;
}

static int ezusb_read_pda(struct hermes *hw, __le16 *pda,
			  u32 pda_addr, u16 pda_len)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;
	__le16 data[] = {
		cpu_to_le16(pda_addr & 0xffff),
		cpu_to_le16(pda_len - 4)
	};
	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_READ_PDA, EZUSB_RID_READ_PDA);
	if (!ctx)
		return -ENOMEM;

	/* wl_lkm does not include PDA size in the PDA area.
	 * We will pad the information into pda, so other routines
	 * don't have to be modified */
	pda[0] = cpu_to_le16(pda_len - 2);
	/* Includes CFG_PROD_DATA but not itself */
	pda[1] = cpu_to_le16(0x0800); /* CFG_PROD_DATA */

	return ezusb_access_ltv(upriv, ctx, sizeof(data), &data,
				EZUSB_FRAME_CONTROL, &pda[2], pda_len - 4,
				NULL);
}

static int ezusb_program_init(struct hermes *hw, u32 entry_point)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;
	__le32 data = cpu_to_le32(entry_point);

	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_PROG_INIT, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, sizeof(data), &data,
				EZUSB_FRAME_CONTROL, NULL, 0, NULL);
}

static int ezusb_program_end(struct hermes *hw)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;

	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_PROG_END, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, 0, NULL,
				EZUSB_FRAME_CONTROL, NULL, 0, NULL);
}

static int ezusb_program_bytes(struct hermes *hw, const char *buf,
			       u32 addr, u32 len)
{
	struct ezusb_priv *upriv = hw->priv;
	struct request_context *ctx;
	__le32 data = cpu_to_le32(addr);
	int err;

	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_PROG_SET_ADDR, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	err = ezusb_access_ltv(upriv, ctx, sizeof(data), &data,
			       EZUSB_FRAME_CONTROL, NULL, 0, NULL);
	if (err)
		return err;

	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_PROG_BYTES, EZUSB_RID_ACK);
	if (!ctx)
		return -ENOMEM;

	return ezusb_access_ltv(upriv, ctx, len, buf,
				EZUSB_FRAME_CONTROL, NULL, 0, NULL);
}

static int ezusb_program(struct hermes *hw, const char *buf,
			 u32 addr, u32 len)
{
	u32 ch_addr;
	u32 ch_len;
	int err = 0;

	/* We can only send 2048 bytes out of the bulk xmit at a time,
	 * so we have to split any programming into chunks of <2048
	 * bytes. */

	ch_len = (len < MAX_DL_SIZE) ? len : MAX_DL_SIZE;
	ch_addr = addr;

	while (ch_addr < (addr + len)) {
		pr_debug("Programming subblock of length %d "
			 "to address 0x%08x. Data @ %p\n",
			 ch_len, ch_addr, &buf[ch_addr - addr]);

		err = ezusb_program_bytes(hw, &buf[ch_addr - addr],
					  ch_addr, ch_len);
		if (err)
			break;

		ch_addr += ch_len;
		ch_len = ((addr + len - ch_addr) < MAX_DL_SIZE) ?
			(addr + len - ch_addr) : MAX_DL_SIZE;
	}

	return err;
}

static netdev_tx_t ezusb_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct ezusb_priv *upriv = priv->card;
	u8 mic[MICHAEL_MIC_LEN + 1];
	int err = 0;
	int tx_control;
	unsigned long flags;
	struct request_context *ctx;
	u8 *buf;
	int tx_size;

	if (!netif_running(dev)) {
		printk(KERN_ERR "%s: Tx on stopped device!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (netif_queue_stopped(dev)) {
		printk(KERN_DEBUG "%s: Tx while transmitter busy!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_ERR
		       "%s: ezusb_xmit() called while hw_unavailable\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (!netif_carrier_ok(dev) ||
	    (priv->iw_mode == NL80211_IFTYPE_MONITOR)) {
		/* Oops, the firmware hasn't established a connection,
		   silently drop the packet (this seems to be the
		   safest approach). */
		goto drop;
	}

	/* Check packet length */
	if (skb->len < ETH_HLEN)
		goto drop;

	ctx = ezusb_alloc_ctx(upriv, EZUSB_RID_TX, 0);
	if (!ctx)
		goto busy;

	memset(ctx->buf, 0, BULK_BUF_SIZE);
	buf = ctx->buf->data;

	tx_control = 0;

	err = orinoco_process_xmit_skb(skb, dev, priv, &tx_control,
				       &mic[0]);
	if (err)
		goto drop;

	{
		__le16 *tx_cntl = (__le16 *)buf;
		*tx_cntl = cpu_to_le16(tx_control);
		buf += sizeof(*tx_cntl);
	}

	memcpy(buf, skb->data, skb->len);
	buf += skb->len;

	if (tx_control & HERMES_TXCTRL_MIC) {
		u8 *m = mic;
		/* Mic has been offset so it can be copied to an even
		 * address. We're copying eveything anyway, so we
		 * don't need to copy that first byte. */
		if (skb->len % 2)
			m++;
		memcpy(buf, m, MICHAEL_MIC_LEN);
		buf += MICHAEL_MIC_LEN;
	}

	/* Finally, we actually initiate the send */
	netif_stop_queue(dev);

	/* The card may behave better if we send evenly sized usb transfers */
	tx_size = ALIGN(buf - ctx->buf->data, 2);

	err = ezusb_access_ltv(upriv, ctx, tx_size, NULL,
			       EZUSB_FRAME_DATA, NULL, 0, NULL);

	if (err) {
		netif_start_queue(dev);
		if (net_ratelimit())
			printk(KERN_ERR "%s: Error %d transmitting packet\n",
				dev->name, err);
		goto busy;
	}

	netif_trans_update(dev);
	stats->tx_bytes += skb->len;
	goto ok;

 drop:
	stats->tx_errors++;
	stats->tx_dropped++;

 ok:
	orinoco_unlock(priv, &flags);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;

 busy:
	orinoco_unlock(priv, &flags);
	return NETDEV_TX_BUSY;
}

static int ezusb_allocate(struct hermes *hw, u16 size, u16 *fid)
{
	*fid = EZUSB_RID_TX;
	return 0;
}


static int ezusb_hard_reset(struct orinoco_private *priv)
{
	struct ezusb_priv *upriv = priv->card;
	int retval = ezusb_8051_cpucs(upriv, 1);

	if (retval < 0) {
		err("Failed to reset");
		return retval;
	}

	retval = ezusb_8051_cpucs(upriv, 0);
	if (retval < 0) {
		err("Failed to unreset");
		return retval;
	}

	netdev_dbg(upriv->dev, "sending control message\n");
	retval = usb_control_msg(upriv->udev,
				 usb_sndctrlpipe(upriv->udev, 0),
				 EZUSB_REQUEST_TRIGER,
				 USB_TYPE_VENDOR | USB_RECIP_DEVICE |
				 USB_DIR_OUT, 0x0, 0x0, NULL, 0,
				 DEF_TIMEOUT);
	if (retval < 0) {
		err("EZUSB_REQUEST_TRIGER failed retval %d", retval);
		return retval;
	}
#if 0
	dbg("Sending EZUSB_REQUEST_TRIG_AC");
	retval = usb_control_msg(upriv->udev,
				 usb_sndctrlpipe(upriv->udev, 0),
				 EZUSB_REQUEST_TRIG_AC,
				 USB_TYPE_VENDOR | USB_RECIP_DEVICE |
				 USB_DIR_OUT, 0x00FA, 0x0, NULL, 0,
				 DEF_TIMEOUT);
	if (retval < 0) {
		err("EZUSB_REQUEST_TRIG_AC failed retval %d", retval);
		return retval;
	}
#endif

	return 0;
}


static int ezusb_init(struct hermes *hw)
{
	struct ezusb_priv *upriv = hw->priv;
	int retval;

	BUG_ON(in_interrupt());
	if (!upriv)
		return -EINVAL;

	upriv->reply_count = 0;
	/* Write the MAGIC number on the simulated registers to keep
	 * orinoco.c happy */
	hermes_write_regn(hw, SWSUPPORT0, HERMES_MAGIC);
	hermes_write_regn(hw, RXFID, EZUSB_RID_RX);

	usb_kill_urb(upriv->read_urb);
	ezusb_submit_in_urb(upriv);

	retval = ezusb_write_ltv(hw, 0, EZUSB_RID_INIT1,
				 HERMES_BYTES_TO_RECLEN(2), "\x10\x00");
	if (retval < 0) {
		printk(KERN_ERR PFX "EZUSB_RID_INIT1 error %d\n", retval);
		return retval;
	}

	retval = ezusb_docmd_wait(hw, HERMES_CMD_INIT, 0, NULL);
	if (retval < 0) {
		printk(KERN_ERR PFX "HERMES_CMD_INIT error %d\n", retval);
		return retval;
	}

	return 0;
}

static void ezusb_bulk_in_callback(struct urb *urb)
{
	struct ezusb_priv *upriv = (struct ezusb_priv *) urb->context;
	struct ezusb_packet *ans = urb->transfer_buffer;
	u16 crc;
	u16 hermes_rid;

	if (upriv->udev == NULL)
		return;

	if (urb->status == -ETIMEDOUT) {
		/* When a device gets unplugged we get this every time
		 * we resubmit, flooding the logs.  Since we don't use
		 * USB timeouts, it shouldn't happen any other time*/
		pr_warn("%s: urb timed out, not resubmitting\n", __func__);
		return;
	}
	if (urb->status == -ECONNABORTED) {
		pr_warn("%s: connection abort, resubmitting urb\n",
			__func__);
		goto resubmit;
	}
	if ((urb->status == -EILSEQ)
	    || (urb->status == -ENOENT)
	    || (urb->status == -ECONNRESET)) {
		netdev_dbg(upriv->dev, "status %d, not resubmiting\n",
			   urb->status);
		return;
	}
	if (urb->status)
		netdev_dbg(upriv->dev, "status: %d length: %d\n",
			   urb->status, urb->actual_length);
	if (urb->actual_length < sizeof(*ans)) {
		err("%s: short read, ignoring", __func__);
		goto resubmit;
	}
	crc = build_crc(ans);
	if (le16_to_cpu(ans->crc) != crc) {
		err("CRC error, ignoring packet");
		goto resubmit;
	}

	hermes_rid = le16_to_cpu(ans->hermes_rid);
	if ((hermes_rid != EZUSB_RID_RX) && !EZUSB_IS_INFO(hermes_rid)) {
		ezusb_request_in_callback(upriv, urb);
	} else if (upriv->dev) {
		struct net_device *dev = upriv->dev;
		struct orinoco_private *priv = ndev_priv(dev);
		struct hermes *hw = &priv->hw;

		if (hermes_rid == EZUSB_RID_RX) {
			__orinoco_ev_rx(dev, hw);
		} else {
			hermes_write_regn(hw, INFOFID,
					  le16_to_cpu(ans->hermes_rid));
			__orinoco_ev_info(dev, hw);
		}
	}

 resubmit:
	if (upriv->udev)
		ezusb_submit_in_urb(upriv);
}

static inline void ezusb_delete(struct ezusb_priv *upriv)
{
	struct list_head *item;
	struct list_head *tmp_item;
	unsigned long flags;

	BUG_ON(in_interrupt());
	BUG_ON(!upriv);

	mutex_lock(&upriv->mtx);

	upriv->udev = NULL;	/* No timer will be rearmed from here */

	usb_kill_urb(upriv->read_urb);

	spin_lock_irqsave(&upriv->req_lock, flags);
	list_for_each_safe(item, tmp_item, &upriv->req_active) {
		struct request_context *ctx;
		int err;

		ctx = list_entry(item, struct request_context, list);
		refcount_inc(&ctx->refcount);

		ctx->outurb->transfer_flags |= URB_ASYNC_UNLINK;
		err = usb_unlink_urb(ctx->outurb);

		spin_unlock_irqrestore(&upriv->req_lock, flags);
		if (err == -EINPROGRESS)
			wait_for_completion(&ctx->done);

		del_timer_sync(&ctx->timer);
		/* FIXME: there is an slight chance for the irq handler to
		 * be running */
		if (!list_empty(&ctx->list))
			ezusb_ctx_complete(ctx);

		ezusb_request_context_put(ctx);
		spin_lock_irqsave(&upriv->req_lock, flags);
	}
	spin_unlock_irqrestore(&upriv->req_lock, flags);

	list_for_each_safe(item, tmp_item, &upriv->req_pending)
	    ezusb_ctx_complete(list_entry(item,
					  struct request_context, list));

	if (upriv->read_urb && upriv->read_urb->status == -EINPROGRESS)
		printk(KERN_ERR PFX "Some URB in progress\n");

	mutex_unlock(&upriv->mtx);

	if (upriv->read_urb) {
		kfree(upriv->read_urb->transfer_buffer);
		usb_free_urb(upriv->read_urb);
	}
	kfree(upriv->bap_buf);
	if (upriv->dev) {
		struct orinoco_private *priv = ndev_priv(upriv->dev);
		orinoco_if_del(priv);
		wiphy_unregister(priv_to_wiphy(upriv));
		free_orinocodev(priv);
	}
}

static void ezusb_lock_irqsave(spinlock_t *lock,
			       unsigned long *flags) __acquires(lock)
{
	spin_lock_bh(lock);
}

static void ezusb_unlock_irqrestore(spinlock_t *lock,
				    unsigned long *flags) __releases(lock)
{
	spin_unlock_bh(lock);
}

static void ezusb_lock_irq(spinlock_t *lock) __acquires(lock)
{
	spin_lock_bh(lock);
}

static void ezusb_unlock_irq(spinlock_t *lock) __releases(lock)
{
	spin_unlock_bh(lock);
}

static const struct hermes_ops ezusb_ops = {
	.init = ezusb_init,
	.cmd_wait = ezusb_docmd_wait,
	.init_cmd_wait = ezusb_doicmd_wait,
	.allocate = ezusb_allocate,
	.read_ltv = ezusb_read_ltv,
	.write_ltv = ezusb_write_ltv,
	.bap_pread = ezusb_bap_pread,
	.read_pda = ezusb_read_pda,
	.program_init = ezusb_program_init,
	.program_end = ezusb_program_end,
	.program = ezusb_program,
	.lock_irqsave = ezusb_lock_irqsave,
	.unlock_irqrestore = ezusb_unlock_irqrestore,
	.lock_irq = ezusb_lock_irq,
	.unlock_irq = ezusb_unlock_irq,
};

static const struct net_device_ops ezusb_netdev_ops = {
	.ndo_open		= orinoco_open,
	.ndo_stop		= orinoco_stop,
	.ndo_start_xmit		= ezusb_xmit,
	.ndo_set_rx_mode	= orinoco_set_multicast_list,
	.ndo_change_mtu		= orinoco_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= orinoco_tx_timeout,
};

static int ezusb_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct orinoco_private *priv;
	struct hermes *hw;
	struct ezusb_priv *upriv = NULL;
	struct usb_interface_descriptor *iface_desc;
	struct usb_endpoint_descriptor *ep;
	const struct firmware *fw_entry = NULL;
	int retval = 0;
	int i;

	priv = alloc_orinocodev(sizeof(*upriv), &udev->dev,
				ezusb_hard_reset, NULL);
	if (!priv) {
		err("Couldn't allocate orinocodev");
		retval = -ENOMEM;
		goto exit;
	}

	hw = &priv->hw;

	upriv = priv->card;

	mutex_init(&upriv->mtx);
	spin_lock_init(&upriv->reply_count_lock);

	spin_lock_init(&upriv->req_lock);
	INIT_LIST_HEAD(&upriv->req_pending);
	INIT_LIST_HEAD(&upriv->req_active);

	upriv->udev = udev;

	hw->iobase = (void __force __iomem *) &upriv->hermes_reg_fake;
	hw->reg_spacing = HERMES_16BIT_REGSPACING;
	hw->priv = upriv;
	hw->ops = &ezusb_ops;

	/* set up the endpoint information */
	/* check out the endpoints */

	iface_desc = &interface->cur_altsetting->desc;
	for (i = 0; i < iface_desc->bNumEndpoints; ++i) {
		ep = &interface->cur_altsetting->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep)) {
			/* we found a bulk in endpoint */
			if (upriv->read_urb != NULL) {
				pr_warn("Found a second bulk in ep, ignored\n");
				continue;
			}

			upriv->read_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!upriv->read_urb)
				goto error;
			if (le16_to_cpu(ep->wMaxPacketSize) != 64)
				pr_warn("bulk in: wMaxPacketSize!= 64\n");
			if (ep->bEndpointAddress != (2 | USB_DIR_IN))
				pr_warn("bulk in: bEndpointAddress: %d\n",
					ep->bEndpointAddress);
			upriv->read_pipe = usb_rcvbulkpipe(udev,
							 ep->
							 bEndpointAddress);
			upriv->read_urb->transfer_buffer =
			    kmalloc(BULK_BUF_SIZE, GFP_KERNEL);
			if (!upriv->read_urb->transfer_buffer) {
				err("Couldn't allocate IN buffer");
				goto error;
			}
		}

		if (usb_endpoint_is_bulk_out(ep)) {
			/* we found a bulk out endpoint */
			if (upriv->bap_buf != NULL) {
				pr_warn("Found a second bulk out ep, ignored\n");
				continue;
			}

			if (le16_to_cpu(ep->wMaxPacketSize) != 64)
				pr_warn("bulk out: wMaxPacketSize != 64\n");
			if (ep->bEndpointAddress != 2)
				pr_warn("bulk out: bEndpointAddress: %d\n",
					ep->bEndpointAddress);
			upriv->write_pipe = usb_sndbulkpipe(udev,
							  ep->
							  bEndpointAddress);
			upriv->bap_buf = kmalloc(BULK_BUF_SIZE, GFP_KERNEL);
			if (!upriv->bap_buf) {
				err("Couldn't allocate bulk_out_buffer");
				goto error;
			}
		}
	}
	if (!upriv->bap_buf || !upriv->read_urb) {
		err("Didn't find the required bulk endpoints");
		goto error;
	}

	if (request_firmware(&fw_entry, "orinoco_ezusb_fw",
			     &interface->dev) == 0) {
		firmware.size = fw_entry->size;
		firmware.code = fw_entry->data;
	}
	if (firmware.size && firmware.code) {
		if (ezusb_firmware_download(upriv, &firmware) < 0)
			goto error;
	} else {
		err("No firmware to download");
		goto error;
	}

	if (ezusb_hard_reset(priv) < 0) {
		err("Cannot reset the device");
		goto error;
	}

	/* If the firmware is already downloaded orinoco.c will call
	 * ezusb_init but if the firmware is not already there, that will make
	 * the kernel very unstable, so we try initializing here and quit in
	 * case of error */
	if (ezusb_init(hw) < 0) {
		err("Couldn't initialize the device");
		err("Firmware may not be downloaded or may be wrong.");
		goto error;
	}

	/* Initialise the main driver */
	if (orinoco_init(priv) != 0) {
		err("orinoco_init() failed\n");
		goto error;
	}

	if (orinoco_if_add(priv, 0, 0, &ezusb_netdev_ops) != 0) {
		upriv->dev = NULL;
		err("%s: orinoco_if_add() failed", __func__);
		wiphy_unregister(priv_to_wiphy(priv));
		goto error;
	}
	upriv->dev = priv->ndev;

	goto exit;

 error:
	ezusb_delete(upriv);
	if (upriv->dev) {
		/* upriv->dev was 0, so ezusb_delete() didn't free it */
		free_orinocodev(priv);
	}
	upriv = NULL;
	retval = -EFAULT;
 exit:
	if (fw_entry) {
		firmware.code = NULL;
		firmware.size = 0;
		release_firmware(fw_entry);
	}
	usb_set_intfdata(interface, upriv);
	return retval;
}


static void ezusb_disconnect(struct usb_interface *intf)
{
	struct ezusb_priv *upriv = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	ezusb_delete(upriv);
	printk(KERN_INFO PFX "Disconnected\n");
}


/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver orinoco_driver = {
	.name = DRIVER_NAME,
	.probe = ezusb_probe,
	.disconnect = ezusb_disconnect,
	.id_table = ezusb_table,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(orinoco_driver);

MODULE_AUTHOR("Manuel Estrada Sainz");
MODULE_DESCRIPTION("Driver for Orinoco wireless LAN cards using EZUSB bridge");
MODULE_LICENSE("Dual MPL/GPL");
