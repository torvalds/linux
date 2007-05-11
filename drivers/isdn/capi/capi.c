/* $Id: capi.c,v 1.1.2.7 2004/04/28 09:48:59 armin Exp $
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/wait.h>
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
#include <linux/tty.h>
#ifdef CONFIG_PPP
#include <linux/netdevice.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#endif /* CONFIG_PPP */
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capicmd.h>
#if defined(CONFIG_ISDN_CAPI_CAPIFS) || defined(CONFIG_ISDN_CAPI_CAPIFS_MODULE)
#include "capifs.h"
#endif

static char *revision = "$Revision: 1.1.2.7 $";

MODULE_DESCRIPTION("CAPI4Linux: Userspace /dev/capi20 interface");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

#undef _DEBUG_REFCOUNT		/* alloc/free and open/close debug */
#undef _DEBUG_TTYFUNCS		/* call to tty_driver */
#undef _DEBUG_DATAFLOW		/* data flow */

/* -------- driver information -------------------------------------- */

static struct class *capi_class;

static int capi_major = 68;		/* allocated */
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
#define CAPINC_NR_PORTS	32
#define CAPINC_MAX_PORTS	256
static int capi_ttymajor = 191;
static int capi_ttyminors = CAPINC_NR_PORTS;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

module_param_named(major, capi_major, uint, 0);
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
module_param_named(ttymajor, capi_ttymajor, uint, 0);
module_param_named(ttyminors, capi_ttyminors, uint, 0);
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

/* -------- defines ------------------------------------------------- */

#define CAPINC_MAX_RECVQUEUE	10
#define CAPINC_MAX_SENDQUEUE	10
#define CAPI_MAX_BLKSIZE	2048

/* -------- data structures ----------------------------------------- */

struct capidev;
struct capincci;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
struct capiminor;

struct datahandle_queue {
	struct list_head	list;
	u16			datahandle;
};

struct capiminor {
	struct list_head list;
	struct capincci  *nccip;
	unsigned int      minor;

	struct capi20_appl *ap;
	u32		 ncci;
	u16		 datahandle;
	u16		 msgid;

	struct tty_struct *tty;
	int                ttyinstop;
	int                ttyoutstop;
	struct sk_buff    *ttyskb;
	atomic_t           ttyopencount;

	struct sk_buff_head inqueue;
	int                 inbytes;
	struct sk_buff_head outqueue;
	int                 outbytes;

	/* transmit path */
	struct list_head ackqueue;
	int nack;
	spinlock_t ackqlock;
};
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

/* FIXME: The following lock is a sledgehammer-workaround to a
 * locking issue with the capiminor (and maybe other) data structure(s).
 * Access to this data is done in a racy way and crashes the machine with
 * a FritzCard DSL driver; sooner or later. This is a workaround
 * which trades scalability vs stability, so it doesn't crash the kernel anymore.
 * The correct (and scalable) fix for the issue seems to require
 * an API change to the drivers... . */
static DEFINE_SPINLOCK(workaround_lock);

struct capincci {
	struct capincci *next;
	u32		 ncci;
	struct capidev	*cdev;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	struct capiminor *minorp;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
};

struct capidev {
	struct list_head list;
	struct capi20_appl ap;
	u16		errcode;
	unsigned        userflags;

	struct sk_buff_head recvqueue;
	wait_queue_head_t recvwait;

	struct capincci *nccis;

	struct mutex ncci_list_mtx;
};

/* -------- global variables ---------------------------------------- */

static DEFINE_RWLOCK(capidev_list_lock);
static LIST_HEAD(capidev_list);

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
static DEFINE_RWLOCK(capiminor_list_lock);
static LIST_HEAD(capiminor_list);
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
/* -------- datahandles --------------------------------------------- */

static int capincci_add_ack(struct capiminor *mp, u16 datahandle)
{
	struct datahandle_queue *n;
	unsigned long flags;

	n = kmalloc(sizeof(*n), GFP_ATOMIC);
	if (unlikely(!n)) {
		printk(KERN_ERR "capi: alloc datahandle failed\n");
		return -1;
	}
	n->datahandle = datahandle;
	INIT_LIST_HEAD(&n->list);
	spin_lock_irqsave(&mp->ackqlock, flags);
	list_add_tail(&n->list, &mp->ackqueue);
	mp->nack++;
	spin_unlock_irqrestore(&mp->ackqlock, flags);
	return 0;
}

static int capiminor_del_ack(struct capiminor *mp, u16 datahandle)
{
	struct datahandle_queue *p, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&mp->ackqlock, flags);
	list_for_each_entry_safe(p, tmp, &mp->ackqueue, list) {
 		if (p->datahandle == datahandle) {
			list_del(&p->list);
			kfree(p);
			mp->nack--;
			spin_unlock_irqrestore(&mp->ackqlock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&mp->ackqlock, flags);
	return -1;
}

static void capiminor_del_all_ack(struct capiminor *mp)
{
	struct datahandle_queue *p, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&mp->ackqlock, flags);
	list_for_each_entry_safe(p, tmp, &mp->ackqueue, list) {
		list_del(&p->list);
		kfree(p);
		mp->nack--;
	}
	spin_unlock_irqrestore(&mp->ackqlock, flags);
}


/* -------- struct capiminor ---------------------------------------- */

static struct capiminor *capiminor_alloc(struct capi20_appl *ap, u32 ncci)
{
	struct capiminor *mp, *p;
	unsigned int minor = 0;
	unsigned long flags;

	mp = kzalloc(sizeof(*mp), GFP_ATOMIC);
  	if (!mp) {
  		printk(KERN_ERR "capi: can't alloc capiminor\n");
		return NULL;
	}

	mp->ap = ap;
	mp->ncci = ncci;
	mp->msgid = 0;
	atomic_set(&mp->ttyopencount,0);
	INIT_LIST_HEAD(&mp->ackqueue);
	spin_lock_init(&mp->ackqlock);

	skb_queue_head_init(&mp->inqueue);
	skb_queue_head_init(&mp->outqueue);

	/* Allocate the least unused minor number.
	 */
	write_lock_irqsave(&capiminor_list_lock, flags);
	if (list_empty(&capiminor_list))
		list_add(&mp->list, &capiminor_list);
	else {
		list_for_each_entry(p, &capiminor_list, list) {
			if (p->minor > minor)
				break;
			minor++;
		}
		
		if (minor < capi_ttyminors) {
			mp->minor = minor;
			list_add(&mp->list, p->list.prev);
		}
	}
		write_unlock_irqrestore(&capiminor_list_lock, flags);

	if (!(minor < capi_ttyminors)) {
		printk(KERN_NOTICE "capi: out of minors\n");
			kfree(mp);
		return NULL;
	}

	return mp;
}

static void capiminor_free(struct capiminor *mp)
{
	unsigned long flags;

	write_lock_irqsave(&capiminor_list_lock, flags);
	list_del(&mp->list);
	write_unlock_irqrestore(&capiminor_list_lock, flags);

	if (mp->ttyskb) kfree_skb(mp->ttyskb);
	mp->ttyskb = NULL;
	skb_queue_purge(&mp->inqueue);
	skb_queue_purge(&mp->outqueue);
	capiminor_del_all_ack(mp);
	kfree(mp);
}

static struct capiminor *capiminor_find(unsigned int minor)
{
	struct list_head *l;
	struct capiminor *p = NULL;

	read_lock(&capiminor_list_lock);
	list_for_each(l, &capiminor_list) {
		p = list_entry(l, struct capiminor, list);
		if (p->minor == minor)
			break;
	}
	read_unlock(&capiminor_list_lock);
	if (l == &capiminor_list)
		return NULL;

	return p;
}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

/* -------- struct capincci ----------------------------------------- */

static struct capincci *capincci_alloc(struct capidev *cdev, u32 ncci)
{
	struct capincci *np, **pp;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	struct capiminor *mp = NULL;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

	np = kzalloc(sizeof(*np), GFP_ATOMIC);
	if (!np)
		return NULL;
	np->ncci = ncci;
	np->cdev = cdev;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	mp = NULL;
	if (cdev->userflags & CAPIFLAG_HIGHJACKING)
		mp = np->minorp = capiminor_alloc(&cdev->ap, ncci);
	if (mp) {
		mp->nccip = np;
#ifdef _DEBUG_REFCOUNT
		printk(KERN_DEBUG "set mp->nccip\n");
#endif
#if defined(CONFIG_ISDN_CAPI_CAPIFS) || defined(CONFIG_ISDN_CAPI_CAPIFS_MODULE)
		capifs_new_ncci(mp->minor, MKDEV(capi_ttymajor, mp->minor));
#endif
	}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
	for (pp=&cdev->nccis; *pp; pp = &(*pp)->next)
		;
	*pp = np;
        return np;
}

static void capincci_free(struct capidev *cdev, u32 ncci)
{
	struct capincci *np, **pp;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	struct capiminor *mp;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

	pp=&cdev->nccis;
	while (*pp) {
		np = *pp;
		if (ncci == 0xffffffff || np->ncci == ncci) {
			*pp = (*pp)->next;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
			if ((mp = np->minorp) != 0) {
#if defined(CONFIG_ISDN_CAPI_CAPIFS) || defined(CONFIG_ISDN_CAPI_CAPIFS_MODULE)
				capifs_free_ncci(mp->minor);
#endif
				if (mp->tty) {
					mp->nccip = NULL;
#ifdef _DEBUG_REFCOUNT
					printk(KERN_DEBUG "reset mp->nccip\n");
#endif
					tty_hangup(mp->tty);
				} else {
					capiminor_free(mp);
				}
			}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
			kfree(np);
			if (*pp == 0) return;
		} else {
			pp = &(*pp)->next;
		}
	}
}

static struct capincci *capincci_find(struct capidev *cdev, u32 ncci)
{
	struct capincci *p;

	for (p=cdev->nccis; p ; p = p->next) {
		if (p->ncci == ncci)
			break;
	}
	return p;
}

/* -------- struct capidev ------------------------------------------ */

static struct capidev *capidev_alloc(void)
{
	struct capidev *cdev;
	unsigned long flags;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return NULL;

	mutex_init(&cdev->ncci_list_mtx);
	skb_queue_head_init(&cdev->recvqueue);
	init_waitqueue_head(&cdev->recvwait);
	write_lock_irqsave(&capidev_list_lock, flags);
	list_add_tail(&cdev->list, &capidev_list);
	write_unlock_irqrestore(&capidev_list_lock, flags);
        return cdev;
}

static void capidev_free(struct capidev *cdev)
{
	unsigned long flags;

	if (cdev->ap.applid) {
		capi20_release(&cdev->ap);
		cdev->ap.applid = 0;
	}
	skb_queue_purge(&cdev->recvqueue);

	mutex_lock(&cdev->ncci_list_mtx);
	capincci_free(cdev, 0xffffffff);
	mutex_unlock(&cdev->ncci_list_mtx);

	write_lock_irqsave(&capidev_list_lock, flags);
	list_del(&cdev->list);
	write_unlock_irqrestore(&capidev_list_lock, flags);
	kfree(cdev);
}

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
/* -------- handle data queue --------------------------------------- */

static struct sk_buff *
gen_data_b3_resp_for(struct capiminor *mp, struct sk_buff *skb)
{
	struct sk_buff *nskb;
	nskb = alloc_skb(CAPI_DATA_B3_RESP_LEN, GFP_ATOMIC);
	if (nskb) {
		u16 datahandle = CAPIMSG_U16(skb->data,CAPIMSG_BASELEN+4+4+2);
		unsigned char *s = skb_put(nskb, CAPI_DATA_B3_RESP_LEN);
		capimsg_setu16(s, 0, CAPI_DATA_B3_RESP_LEN);
		capimsg_setu16(s, 2, mp->ap->applid);
		capimsg_setu8 (s, 4, CAPI_DATA_B3);
		capimsg_setu8 (s, 5, CAPI_RESP);
		capimsg_setu16(s, 6, mp->msgid++);
		capimsg_setu32(s, 8, mp->ncci);
		capimsg_setu16(s, 12, datahandle);
	}
	return nskb;
}

static int handle_recv_skb(struct capiminor *mp, struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int datalen;
	u16 errcode, datahandle;
	struct tty_ldisc *ld;
	
	datalen = skb->len - CAPIMSG_LEN(skb->data);
	if (mp->tty == NULL)
	{
#ifdef _DEBUG_DATAFLOW
		printk(KERN_DEBUG "capi: currently no receiver\n");
#endif
		return -1;
	}
	
	ld = tty_ldisc_ref(mp->tty);
	if (ld == NULL)
		return -1;
	if (ld->receive_buf == NULL) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
		printk(KERN_DEBUG "capi: ldisc has no receive_buf function\n");
#endif
		goto bad;
	}
	if (mp->ttyinstop) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
		printk(KERN_DEBUG "capi: recv tty throttled\n");
#endif
		goto bad;
	}
	if (mp->tty->receive_room < datalen) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
		printk(KERN_DEBUG "capi: no room in tty\n");
#endif
		goto bad;
	}
	if ((nskb = gen_data_b3_resp_for(mp, skb)) == 0) {
		printk(KERN_ERR "capi: gen_data_b3_resp failed\n");
		goto bad;
	}
	datahandle = CAPIMSG_U16(skb->data,CAPIMSG_BASELEN+4);
	errcode = capi20_put_message(mp->ap, nskb);
	if (errcode != CAPI_NOERROR) {
		printk(KERN_ERR "capi: send DATA_B3_RESP failed=%x\n",
				errcode);
		kfree_skb(nskb);
		goto bad;
	}
	(void)skb_pull(skb, CAPIMSG_LEN(skb->data));
#ifdef _DEBUG_DATAFLOW
	printk(KERN_DEBUG "capi: DATA_B3_RESP %u len=%d => ldisc\n",
				datahandle, skb->len);
#endif
	ld->receive_buf(mp->tty, skb->data, NULL, skb->len);
	kfree_skb(skb);
	tty_ldisc_deref(ld);
	return 0;
bad:
	tty_ldisc_deref(ld);
	return -1;
}

static void handle_minor_recv(struct capiminor *mp)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&mp->inqueue)) != 0) {
		unsigned int len = skb->len;
		mp->inbytes -= len;
		if (handle_recv_skb(mp, skb) < 0) {
			skb_queue_head(&mp->inqueue, skb);
			mp->inbytes += len;
			return;
		}
	}
}

static int handle_minor_send(struct capiminor *mp)
{
	struct sk_buff *skb;
	u16 len;
	int count = 0;
	u16 errcode;
	u16 datahandle;

	if (mp->tty && mp->ttyoutstop) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
		printk(KERN_DEBUG "capi: send: tty stopped\n");
#endif
		return 0;
	}

	while ((skb = skb_dequeue(&mp->outqueue)) != 0) {
		datahandle = mp->datahandle;
		len = (u16)skb->len;
		skb_push(skb, CAPI_DATA_B3_REQ_LEN);
		memset(skb->data, 0, CAPI_DATA_B3_REQ_LEN);
		capimsg_setu16(skb->data, 0, CAPI_DATA_B3_REQ_LEN);
		capimsg_setu16(skb->data, 2, mp->ap->applid);
		capimsg_setu8 (skb->data, 4, CAPI_DATA_B3);
		capimsg_setu8 (skb->data, 5, CAPI_REQ);
		capimsg_setu16(skb->data, 6, mp->msgid++);
		capimsg_setu32(skb->data, 8, mp->ncci);	/* NCCI */
		capimsg_setu32(skb->data, 12, (u32) skb->data); /* Data32 */
		capimsg_setu16(skb->data, 16, len);	/* Data length */
		capimsg_setu16(skb->data, 18, datahandle);
		capimsg_setu16(skb->data, 20, 0);	/* Flags */

		if (capincci_add_ack(mp, datahandle) < 0) {
			skb_pull(skb, CAPI_DATA_B3_REQ_LEN);
			skb_queue_head(&mp->outqueue, skb);
			return count;
		}
		errcode = capi20_put_message(mp->ap, skb);
		if (errcode == CAPI_NOERROR) {
			mp->datahandle++;
			count++;
			mp->outbytes -= len;
#ifdef _DEBUG_DATAFLOW
			printk(KERN_DEBUG "capi: DATA_B3_REQ %u len=%u\n",
							datahandle, len);
#endif
			continue;
		}
		capiminor_del_ack(mp, datahandle);

		if (errcode == CAPI_SENDQUEUEFULL) {
			skb_pull(skb, CAPI_DATA_B3_REQ_LEN);
			skb_queue_head(&mp->outqueue, skb);
			break;
		}

		/* ups, drop packet */
		printk(KERN_ERR "capi: put_message = %x\n", errcode);
		mp->outbytes -= len;
		kfree_skb(skb);
	}
	return count;
}

#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
/* -------- function called by lower level -------------------------- */

static void capi_recv_message(struct capi20_appl *ap, struct sk_buff *skb)
{
	struct capidev *cdev = ap->private;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	struct capiminor *mp;
	u16 datahandle;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
	struct capincci *np;
	u32 ncci;
	unsigned long flags;

	if (CAPIMSG_CMD(skb->data) == CAPI_CONNECT_B3_CONF) {
		u16 info = CAPIMSG_U16(skb->data, 12); // Info field
		if (info == 0) {
			mutex_lock(&cdev->ncci_list_mtx);
			capincci_alloc(cdev, CAPIMSG_NCCI(skb->data));
			mutex_unlock(&cdev->ncci_list_mtx);
		}
	}
	if (CAPIMSG_CMD(skb->data) == CAPI_CONNECT_B3_IND) {
		mutex_lock(&cdev->ncci_list_mtx);
		capincci_alloc(cdev, CAPIMSG_NCCI(skb->data));
		mutex_unlock(&cdev->ncci_list_mtx);
	}
	spin_lock_irqsave(&workaround_lock, flags);
	if (CAPIMSG_COMMAND(skb->data) != CAPI_DATA_B3) {
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
		spin_unlock_irqrestore(&workaround_lock, flags);
		return;
	}
	ncci = CAPIMSG_CONTROL(skb->data);
	for (np = cdev->nccis; np && np->ncci != ncci; np = np->next)
		;
	if (!np) {
		printk(KERN_ERR "BUG: capi_signal: ncci not found\n");
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
		spin_unlock_irqrestore(&workaround_lock, flags);
		return;
	}
#ifndef CONFIG_ISDN_CAPI_MIDDLEWARE
	skb_queue_tail(&cdev->recvqueue, skb);
	wake_up_interruptible(&cdev->recvwait);
#else /* CONFIG_ISDN_CAPI_MIDDLEWARE */
	mp = np->minorp;
	if (!mp) {
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
		spin_unlock_irqrestore(&workaround_lock, flags);
		return;
	}


	if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_IND) {
		
		datahandle = CAPIMSG_U16(skb->data, CAPIMSG_BASELEN+4+4+2);
#ifdef _DEBUG_DATAFLOW
		printk(KERN_DEBUG "capi_signal: DATA_B3_IND %u len=%d\n",
				datahandle, skb->len-CAPIMSG_LEN(skb->data));
#endif
		skb_queue_tail(&mp->inqueue, skb);
		mp->inbytes += skb->len;
		handle_minor_recv(mp);

	} else if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_CONF) {

		datahandle = CAPIMSG_U16(skb->data, CAPIMSG_BASELEN+4);
#ifdef _DEBUG_DATAFLOW
		printk(KERN_DEBUG "capi_signal: DATA_B3_CONF %u 0x%x\n",
				datahandle,
				CAPIMSG_U16(skb->data, CAPIMSG_BASELEN+4+2));
#endif
		kfree_skb(skb);
		(void)capiminor_del_ack(mp, datahandle);
		if (mp->tty)
			tty_wakeup(mp->tty);
		(void)handle_minor_send(mp);

	} else {
		/* ups, let capi application handle it :-) */
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
	}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
	spin_unlock_irqrestore(&workaround_lock, flags);
}

/* -------- file_operations for capidev ----------------------------- */

static ssize_t
capi_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	size_t copied;

	if (!cdev->ap.applid)
		return -ENODEV;

	if ((skb = skb_dequeue(&cdev->recvqueue)) == 0) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		for (;;) {
			interruptible_sleep_on(&cdev->recvwait);
			if ((skb = skb_dequeue(&cdev->recvqueue)) != 0)
				break;
			if (signal_pending(current))
				break;
		}
		if (skb == 0)
			return -ERESTARTNOHAND;
	}
	if (skb->len > count) {
		skb_queue_head(&cdev->recvqueue, skb);
		return -EMSGSIZE;
	}
	if (copy_to_user(buf, skb->data, skb->len)) {
		skb_queue_head(&cdev->recvqueue, skb);
		return -EFAULT;
	}
	copied = skb->len;

	kfree_skb(skb);

	return copied;
}

static ssize_t
capi_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	u16 mlen;

	if (!cdev->ap.applid)
		return -ENODEV;

	skb = alloc_skb(count, GFP_USER);
	if (!skb)
		return -ENOMEM;

	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	mlen = CAPIMSG_LEN(skb->data);
	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		if ((size_t)(mlen + CAPIMSG_DATALEN(skb->data)) != count) {
			kfree_skb(skb);
			return -EINVAL;
		}
	} else {
		if (mlen != count) {
			kfree_skb(skb);
			return -EINVAL;
		}
	}
	CAPIMSG_SETAPPID(skb->data, cdev->ap.applid);

	if (CAPIMSG_CMD(skb->data) == CAPI_DISCONNECT_B3_RESP) {
		mutex_lock(&cdev->ncci_list_mtx);
		capincci_free(cdev, CAPIMSG_NCCI(skb->data));
		mutex_unlock(&cdev->ncci_list_mtx);
	}

	cdev->errcode = capi20_put_message(&cdev->ap, skb);

	if (cdev->errcode) {
		kfree_skb(skb);
		return -EIO;
	}
	return count;
}

static unsigned int
capi_poll(struct file *file, poll_table * wait)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	unsigned int mask = 0;

	if (!cdev->ap.applid)
		return POLLERR;

	poll_wait(file, &(cdev->recvwait), wait);
	mask = POLLOUT | POLLWRNORM;
	if (!skb_queue_empty(&cdev->recvqueue))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int
capi_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	struct capidev *cdev = file->private_data;
	struct capi20_appl *ap = &cdev->ap;
	capi_ioctl_struct data;
	int retval = -EINVAL;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case CAPI_REGISTER:
		{
			if (ap->applid)
				return -EEXIST;

			if (copy_from_user(&cdev->ap.rparam, argp,
					   sizeof(struct capi_register_params)))
				return -EFAULT;
			
			cdev->ap.private = cdev;
			cdev->ap.recv_message = capi_recv_message;
			cdev->errcode = capi20_register(ap);
			if (cdev->errcode) {
				ap->applid = 0;
				return -EIO;
			}
		}
		return (int)ap->applid;

	case CAPI_GET_VERSION:
		{
			if (copy_from_user(&data.contr, argp,
						sizeof(data.contr)))
				return -EFAULT;
		        cdev->errcode = capi20_get_version(data.contr, &data.version);
			if (cdev->errcode)
				return -EIO;
			if (copy_to_user(argp, &data.version,
					 sizeof(data.version)))
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_SERIAL:
		{
			if (copy_from_user(&data.contr, argp,
					   sizeof(data.contr)))
				return -EFAULT;
			cdev->errcode = capi20_get_serial (data.contr, data.serial);
			if (cdev->errcode)
				return -EIO;
			if (copy_to_user(argp, data.serial,
					 sizeof(data.serial)))
				return -EFAULT;
		}
		return 0;
	case CAPI_GET_PROFILE:
		{
			if (copy_from_user(&data.contr, argp,
					   sizeof(data.contr)))
				return -EFAULT;

			if (data.contr == 0) {
				cdev->errcode = capi20_get_profile(data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user(argp,
				      &data.profile.ncontroller,
				       sizeof(data.profile.ncontroller));

			} else {
				cdev->errcode = capi20_get_profile(data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user(argp, &data.profile,
						   sizeof(data.profile));
			}
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_MANUFACTURER:
		{
			if (copy_from_user(&data.contr, argp,
					   sizeof(data.contr)))
				return -EFAULT;
			cdev->errcode = capi20_get_manufacturer(data.contr, data.manufacturer);
			if (cdev->errcode)
				return -EIO;

			if (copy_to_user(argp, data.manufacturer,
					 sizeof(data.manufacturer)))
				return -EFAULT;

		}
		return 0;
	case CAPI_GET_ERRCODE:
		data.errcode = cdev->errcode;
		cdev->errcode = CAPI_NOERROR;
		if (arg) {
			if (copy_to_user(argp, &data.errcode,
					 sizeof(data.errcode)))
				return -EFAULT;
		}
		return data.errcode;

	case CAPI_INSTALLED:
		if (capi20_isinstalled() == CAPI_NOERROR)
			return 0;
		return -ENXIO;

	case CAPI_MANUFACTURER_CMD:
		{
			struct capi_manufacturer_cmd mcmd;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if (copy_from_user(&mcmd, argp, sizeof(mcmd)))
				return -EFAULT;
			return capi20_manufacturer(mcmd.cmd, mcmd.data);
		}
		return 0;

	case CAPI_SET_FLAGS:
	case CAPI_CLR_FLAGS:
		{
			unsigned userflags;
			if (copy_from_user(&userflags, argp,
					   sizeof(userflags)))
				return -EFAULT;
			if (cmd == CAPI_SET_FLAGS)
				cdev->userflags |= userflags;
			else
				cdev->userflags &= ~userflags;
		}
		return 0;

	case CAPI_GET_FLAGS:
		if (copy_to_user(argp, &cdev->userflags,
				 sizeof(cdev->userflags)))
			return -EFAULT;
		return 0;

	case CAPI_NCCI_OPENCOUNT:
		{
			struct capincci *nccip;
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
			struct capiminor *mp;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
			unsigned ncci;
			int count = 0;
			if (copy_from_user(&ncci, argp, sizeof(ncci)))
				return -EFAULT;

			mutex_lock(&cdev->ncci_list_mtx);
			if ((nccip = capincci_find(cdev, (u32) ncci)) == 0) {
				mutex_unlock(&cdev->ncci_list_mtx);
				return 0;
			}
#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
			if ((mp = nccip->minorp) != 0) {
				count += atomic_read(&mp->ttyopencount);
			}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
			mutex_unlock(&cdev->ncci_list_mtx);
			return count;
		}
		return 0;

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	case CAPI_NCCI_GETUNIT:
		{
			struct capincci *nccip;
			struct capiminor *mp;
			unsigned ncci;
			int unit = 0;
			if (copy_from_user(&ncci, argp,
					   sizeof(ncci)))
				return -EFAULT;
			mutex_lock(&cdev->ncci_list_mtx);
			nccip = capincci_find(cdev, (u32) ncci);
			if (!nccip || (mp = nccip->minorp) == 0) {
				mutex_unlock(&cdev->ncci_list_mtx);
				return -ESRCH;
			}
			unit = mp->minor;
			mutex_unlock(&cdev->ncci_list_mtx);
			return unit;
		}
		return 0;
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */
	}
	return -EINVAL;
}

static int
capi_open(struct inode *inode, struct file *file)
{
	if (file->private_data)
		return -EEXIST;

	if ((file->private_data = capidev_alloc()) == 0)
		return -ENOMEM;

	return nonseekable_open(inode, file);
}

static int
capi_release(struct inode *inode, struct file *file)
{
	struct capidev *cdev = (struct capidev *)file->private_data;

	capidev_free(cdev);
	file->private_data = NULL;
	
	return 0;
}

static const struct file_operations capi_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= capi_read,
	.write		= capi_write,
	.poll		= capi_poll,
	.ioctl		= capi_ioctl,
	.open		= capi_open,
	.release	= capi_release,
};

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
/* -------- tty_operations for capincci ----------------------------- */

static int capinc_tty_open(struct tty_struct * tty, struct file * file)
{
	struct capiminor *mp;
	unsigned long flags;

	if ((mp = capiminor_find(iminor(file->f_path.dentry->d_inode))) == 0)
		return -ENXIO;
	if (mp->nccip == 0)
		return -ENXIO;

	tty->driver_data = (void *)mp;

	spin_lock_irqsave(&workaround_lock, flags);
	if (atomic_read(&mp->ttyopencount) == 0)
		mp->tty = tty;
	atomic_inc(&mp->ttyopencount);
#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capinc_tty_open ocount=%d\n", atomic_read(&mp->ttyopencount));
#endif
	handle_minor_recv(mp);
	spin_unlock_irqrestore(&workaround_lock, flags);
	return 0;
}

static void capinc_tty_close(struct tty_struct * tty, struct file * file)
{
	struct capiminor *mp;

	mp = (struct capiminor *)tty->driver_data;
	if (mp)	{
		if (atomic_dec_and_test(&mp->ttyopencount)) {
#ifdef _DEBUG_REFCOUNT
			printk(KERN_DEBUG "capinc_tty_close lastclose\n");
#endif
			tty->driver_data = NULL;
			mp->tty = NULL;
		}
#ifdef _DEBUG_REFCOUNT
		printk(KERN_DEBUG "capinc_tty_close ocount=%d\n", atomic_read(&mp->ttyopencount));
#endif
		if (mp->nccip == 0)
			capiminor_free(mp);
	}

#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capinc_tty_close\n");
#endif
}

static int capinc_tty_write(struct tty_struct * tty,
			    const unsigned char *buf, int count)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;
	unsigned long flags;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_write(count=%d)\n", count);
#endif

	if (!mp || !mp->nccip) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_write: mp or mp->ncci NULL\n");
#endif
		return 0;
	}

	spin_lock_irqsave(&workaround_lock, flags);
	skb = mp->ttyskb;
	if (skb) {
		mp->ttyskb = NULL;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
	}

	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+count, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "capinc_tty_write: alloc_skb failed\n");
		spin_unlock_irqrestore(&workaround_lock, flags);
		return -ENOMEM;
	}

	skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
	memcpy(skb_put(skb, count), buf, count);

	skb_queue_tail(&mp->outqueue, skb);
	mp->outbytes += skb->len;
	(void)handle_minor_send(mp);
	(void)handle_minor_recv(mp);
	spin_unlock_irqrestore(&workaround_lock, flags);
	return count;
}

static void capinc_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;
	unsigned long flags;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_put_char(%u)\n", ch);
#endif

	if (!mp || !mp->nccip) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_put_char: mp or mp->ncci NULL\n");
#endif
		return;
	}

	spin_lock_irqsave(&workaround_lock, flags);
	skb = mp->ttyskb;
	if (skb) {
		if (skb_tailroom(skb) > 0) {
			*(skb_put(skb, 1)) = ch;
			spin_unlock_irqrestore(&workaround_lock, flags);
			return;
		}
		mp->ttyskb = NULL;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
		(void)handle_minor_send(mp);
	}
	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+CAPI_MAX_BLKSIZE, GFP_ATOMIC);
	if (skb) {
		skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
		*(skb_put(skb, 1)) = ch;
		mp->ttyskb = skb;
	} else {
		printk(KERN_ERR "capinc_put_char: char %u lost\n", ch);
	}
	spin_unlock_irqrestore(&workaround_lock, flags);
}

static void capinc_tty_flush_chars(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;
	unsigned long flags;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_flush_chars\n");
#endif

	if (!mp || !mp->nccip) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_flush_chars: mp or mp->ncci NULL\n");
#endif
		return;
	}

	spin_lock_irqsave(&workaround_lock, flags);
	skb = mp->ttyskb;
	if (skb) {
		mp->ttyskb = NULL;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
		(void)handle_minor_send(mp);
	}
	(void)handle_minor_recv(mp);
	spin_unlock_irqrestore(&workaround_lock, flags);
}

static int capinc_tty_write_room(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	int room;
	if (!mp || !mp->nccip) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_write_room: mp or mp->ncci NULL\n");
#endif
		return 0;
	}
	room = CAPINC_MAX_SENDQUEUE-skb_queue_len(&mp->outqueue);
	room *= CAPI_MAX_BLKSIZE;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_write_room = %d\n", room);
#endif
	return room;
}

static int capinc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	if (!mp || !mp->nccip) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_chars_in_buffer: mp or mp->ncci NULL\n");
#endif
		return 0;
	}
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_chars_in_buffer = %d nack=%d sq=%d rq=%d\n",
			mp->outbytes, mp->nack,
			skb_queue_len(&mp->outqueue),
			skb_queue_len(&mp->inqueue));
#endif
	return mp->outbytes;
}

static int capinc_tty_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error = 0;
	switch (cmd) {
	default:
		error = n_tty_ioctl (tty, file, cmd, arg);
		break;
	}
	return error;
}

static void capinc_tty_set_termios(struct tty_struct *tty, struct ktermios * old)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_set_termios\n");
#endif
}

static void capinc_tty_throttle(struct tty_struct * tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_throttle\n");
#endif
	if (mp)
		mp->ttyinstop = 1;
}

static void capinc_tty_unthrottle(struct tty_struct * tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	unsigned long flags;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_unthrottle\n");
#endif
	if (mp) {
		spin_lock_irqsave(&workaround_lock, flags);
		mp->ttyinstop = 0;
		handle_minor_recv(mp);
		spin_unlock_irqrestore(&workaround_lock, flags);
	}
}

static void capinc_tty_stop(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_stop\n");
#endif
	if (mp) {
		mp->ttyoutstop = 1;
	}
}

static void capinc_tty_start(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	unsigned long flags;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_start\n");
#endif
	if (mp) {
		spin_lock_irqsave(&workaround_lock, flags);
		mp->ttyoutstop = 0;
		(void)handle_minor_send(mp);
		spin_unlock_irqrestore(&workaround_lock, flags);
	}
}

static void capinc_tty_hangup(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_hangup\n");
#endif
}

static void capinc_tty_break_ctl(struct tty_struct *tty, int state)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_break_ctl(%d)\n", state);
#endif
}

static void capinc_tty_flush_buffer(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_flush_buffer\n");
#endif
}

static void capinc_tty_set_ldisc(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_set_ldisc\n");
#endif
}

static void capinc_tty_send_xchar(struct tty_struct *tty, char ch)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_send_xchar(%d)\n", ch);
#endif
}

static int capinc_tty_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	return 0;
}

static struct tty_driver *capinc_tty_driver;

static const struct tty_operations capinc_ops = {
	.open = capinc_tty_open,
	.close = capinc_tty_close,
	.write = capinc_tty_write,
	.put_char = capinc_tty_put_char,
	.flush_chars = capinc_tty_flush_chars,
	.write_room = capinc_tty_write_room,
	.chars_in_buffer = capinc_tty_chars_in_buffer,
	.ioctl = capinc_tty_ioctl,
	.set_termios = capinc_tty_set_termios,
	.throttle = capinc_tty_throttle,
	.unthrottle = capinc_tty_unthrottle,
	.stop = capinc_tty_stop,
	.start = capinc_tty_start,
	.hangup = capinc_tty_hangup,
	.break_ctl = capinc_tty_break_ctl,
	.flush_buffer = capinc_tty_flush_buffer,
	.set_ldisc = capinc_tty_set_ldisc,
	.send_xchar = capinc_tty_send_xchar,
	.read_proc = capinc_tty_read_proc,
};

static int capinc_tty_init(void)
{
	struct tty_driver *drv;
	
	if (capi_ttyminors > CAPINC_MAX_PORTS)
		capi_ttyminors = CAPINC_MAX_PORTS;
	if (capi_ttyminors <= 0)
		capi_ttyminors = CAPINC_NR_PORTS;

	drv = alloc_tty_driver(capi_ttyminors);
	if (!drv)
		return -ENOMEM;

	drv->owner = THIS_MODULE;
	drv->driver_name = "capi_nc";
	drv->name = "capi";
	drv->major = capi_ttymajor;
	drv->minor_start = 0;
	drv->type = TTY_DRIVER_TYPE_SERIAL;
	drv->subtype = SERIAL_TYPE_NORMAL;
	drv->init_termios = tty_std_termios;
	drv->init_termios.c_iflag = ICRNL;
	drv->init_termios.c_oflag = OPOST | ONLCR;
	drv->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	drv->init_termios.c_lflag = 0;
	drv->flags = TTY_DRIVER_REAL_RAW|TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(drv, &capinc_ops);
	if (tty_register_driver(drv)) {
		put_tty_driver(drv);
		printk(KERN_ERR "Couldn't register capi_nc driver\n");
		return -1;
	}
	capinc_tty_driver = drv;
	return 0;
}

static void capinc_tty_exit(void)
{
	struct tty_driver *drv = capinc_tty_driver;
	int retval;
	if ((retval = tty_unregister_driver(drv)))
		printk(KERN_ERR "capi: failed to unregister capi_nc driver (%d)\n", retval);
	put_tty_driver(drv);
}

#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

/* -------- /proc functions ----------------------------------------- */

/*
 * /proc/capi/capi20:
 *  minor applid nrecvctlpkt nrecvdatapkt nsendctlpkt nsenddatapkt
 */
static int proc_capidev_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct capidev *cdev;
	struct list_head *l;
	int len = 0;

	read_lock(&capidev_list_lock);
	list_for_each(l, &capidev_list) {
		cdev = list_entry(l, struct capidev, list);
		len += sprintf(page+len, "0 %d %lu %lu %lu %lu\n",
			cdev->ap.applid,
			cdev->ap.nrecvctlpkt,
			cdev->ap.nrecvdatapkt,
			cdev->ap.nsentctlpkt,
			cdev->ap.nsentdatapkt);
		if (len <= off) {
			off -= len;
			len = 0;
		} else {
			if (len-off > count)
				goto endloop;
		}
	}

endloop:
	read_unlock(&capidev_list_lock);
	if (len < count)
		*eof = 1;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}

/*
 * /proc/capi/capi20ncci:
 *  applid ncci
 */
static int proc_capincci_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct capidev *cdev;
        struct capincci *np;
	struct list_head *l;
	int len = 0;

	read_lock(&capidev_list_lock);
	list_for_each(l, &capidev_list) {
		cdev = list_entry(l, struct capidev, list);
		for (np=cdev->nccis; np; np = np->next) {
			len += sprintf(page+len, "%d 0x%x\n",
				       cdev->ap.applid,
				       np->ncci);
			if (len <= off) {
				off -= len;
				len = 0;
			} else {
				if (len-off > count)
					goto endloop;
			}
		}
	}
endloop:
	read_unlock(&capidev_list_lock);
	*start = page+off;
	if (len < count)
		*eof = 1;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

static struct procfsentries {
  char *name;
  mode_t mode;
  int (*read_proc)(char *page, char **start, off_t off,
                                       int count, int *eof, void *data);
  struct proc_dir_entry *procent;
} procfsentries[] = {
   /* { "capi",		  S_IFDIR, 0 }, */
   { "capi/capi20", 	  0	 , proc_capidev_read_proc },
   { "capi/capi20ncci",   0	 , proc_capincci_read_proc },
};

static void __init proc_init(void)
{
    int nelem = ARRAY_SIZE(procfsentries);
    int i;

    for (i=0; i < nelem; i++) {
        struct procfsentries *p = procfsentries + i;
	p->procent = create_proc_entry(p->name, p->mode, NULL);
	if (p->procent) p->procent->read_proc = p->read_proc;
    }
}

static void __exit proc_exit(void)
{
    int nelem = ARRAY_SIZE(procfsentries);
    int i;

    for (i=nelem-1; i >= 0; i--) {
        struct procfsentries *p = procfsentries + i;
	if (p->procent) {
	   remove_proc_entry(p->name, NULL);
	   p->procent = NULL;
	}
    }
}

/* -------- init function and module interface ---------------------- */


static char rev[32];

static int __init capi_init(void)
{
	char *p;
	char *compileinfo;
	int major_ret;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, sizeof(rev));
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	major_ret = register_chrdev(capi_major, "capi20", &capi_fops);
	if (major_ret < 0) {
		printk(KERN_ERR "capi20: unable to get major %d\n", capi_major);
		return major_ret;
	}
	capi_class = class_create(THIS_MODULE, "capi");
	if (IS_ERR(capi_class)) {
		unregister_chrdev(capi_major, "capi20");
		return PTR_ERR(capi_class);
	}

	class_device_create(capi_class, NULL, MKDEV(capi_major, 0), NULL, "capi");

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	if (capinc_tty_init() < 0) {
		class_device_destroy(capi_class, MKDEV(capi_major, 0));
		class_destroy(capi_class);
		unregister_chrdev(capi_major, "capi20");
		return -ENOMEM;
	}
#endif /* CONFIG_ISDN_CAPI_MIDDLEWARE */

	proc_init();

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
#if defined(CONFIG_ISDN_CAPI_CAPIFS) || defined(CONFIG_ISDN_CAPI_CAPIFS_MODULE)
        compileinfo = " (middleware+capifs)";
#else
        compileinfo = " (no capifs)";
#endif
#else
        compileinfo = " (no middleware)";
#endif
	printk(KERN_NOTICE "capi20: Rev %s: started up with major %d%s\n",
				rev, capi_major, compileinfo);

	return 0;
}

static void __exit capi_exit(void)
{
	proc_exit();

	class_device_destroy(capi_class, MKDEV(capi_major, 0));
	class_destroy(capi_class);
	unregister_chrdev(capi_major, "capi20");

#ifdef CONFIG_ISDN_CAPI_MIDDLEWARE
	capinc_tty_exit();
#endif
	printk(KERN_NOTICE "capi: Rev %s: unloaded\n", rev);
}

module_init(capi_init);
module_exit(capi_exit);
