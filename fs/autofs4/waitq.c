/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/waitq.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 2001-2003 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/slab.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/file.h>
#include "autofs_i.h"

/* We make this a static variable rather than a part of the superblock; it
   is better if we don't reassign numbers easily even across filesystems */
static autofs_wqt_t autofs4_next_wait_queue = 1;

/* These are the signals we allow interrupting a pending mount */
#define SHUTDOWN_SIGS	(sigmask(SIGKILL) | sigmask(SIGINT) | sigmask(SIGQUIT))

void autofs4_catatonic_mode(struct autofs_sb_info *sbi)
{
	struct autofs_wait_queue *wq, *nwq;

	DPRINTK("entering catatonic mode");

	sbi->catatonic = 1;
	wq = sbi->queues;
	sbi->queues = NULL;	/* Erase all wait queues */
	while ( wq ) {
		nwq = wq->next;
		wq->status = -ENOENT; /* Magic is gone - report failure */
		kfree(wq->name);
		wq->name = NULL;
		wake_up_interruptible(&wq->queue);
		wq = nwq;
	}
	if (sbi->pipe) {
		fput(sbi->pipe);	/* Close the pipe */
		sbi->pipe = NULL;
	}

	shrink_dcache_sb(sbi->sb);
}

static int autofs4_write(struct file *file, const void *addr, int bytes)
{
	unsigned long sigpipe, flags;
	mm_segment_t fs;
	const char *data = (const char *)addr;
	ssize_t wr = 0;

	/** WARNING: this is not safe for writing more than PIPE_BUF bytes! **/

	sigpipe = sigismember(&current->pending.signal, SIGPIPE);

	/* Save pointer to user space and point back to kernel space */
	fs = get_fs();
	set_fs(KERNEL_DS);

	while (bytes &&
	       (wr = file->f_op->write(file,data,bytes,&file->f_pos)) > 0) {
		data += wr;
		bytes -= wr;
	}

	set_fs(fs);

	/* Keep the currently executing process from receiving a
	   SIGPIPE unless it was already supposed to get one */
	if (wr == -EPIPE && !sigpipe) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		sigdelset(&current->pending.signal, SIGPIPE);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	return (bytes > 0);
}
	
static void autofs4_notify_daemon(struct autofs_sb_info *sbi,
				 struct autofs_wait_queue *wq,
				 int type)
{
	union autofs_packet_union pkt;
	size_t pktsz;

	DPRINTK("wait id = 0x%08lx, name = %.*s, type=%d",
		wq->wait_queue_token, wq->len, wq->name, type);

	memset(&pkt,0,sizeof pkt); /* For security reasons */

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = type;
	if (type == autofs_ptype_missing) {
		struct autofs_packet_missing *mp = &pkt.missing;

		pktsz = sizeof(*mp);

		mp->wait_queue_token = wq->wait_queue_token;
		mp->len = wq->len;
		memcpy(mp->name, wq->name, wq->len);
		mp->name[wq->len] = '\0';
	} else if (type == autofs_ptype_expire_multi) {
		struct autofs_packet_expire_multi *ep = &pkt.expire_multi;

		pktsz = sizeof(*ep);

		ep->wait_queue_token = wq->wait_queue_token;
		ep->len = wq->len;
		memcpy(ep->name, wq->name, wq->len);
		ep->name[wq->len] = '\0';
	} else {
		printk("autofs4_notify_daemon: bad type %d!\n", type);
		return;
	}

	if (autofs4_write(sbi->pipe, &pkt, pktsz))
		autofs4_catatonic_mode(sbi);
}

static int autofs4_getpath(struct autofs_sb_info *sbi,
			   struct dentry *dentry, char **name)
{
	struct dentry *root = sbi->sb->s_root;
	struct dentry *tmp;
	char *buf = *name;
	char *p;
	int len = 0;

	spin_lock(&dcache_lock);
	for (tmp = dentry ; tmp != root ; tmp = tmp->d_parent)
		len += tmp->d_name.len + 1;

	if (--len > NAME_MAX) {
		spin_unlock(&dcache_lock);
		return 0;
	}

	*(buf + len) = '\0';
	p = buf + len - dentry->d_name.len;
	strncpy(p, dentry->d_name.name, dentry->d_name.len);

	for (tmp = dentry->d_parent; tmp != root ; tmp = tmp->d_parent) {
		*(--p) = '/';
		p -= tmp->d_name.len;
		strncpy(p, tmp->d_name.name, tmp->d_name.len);
	}
	spin_unlock(&dcache_lock);

	return len;
}

int autofs4_wait(struct autofs_sb_info *sbi, struct dentry *dentry,
		enum autofs_notify notify)
{
	struct autofs_wait_queue *wq;
	char *name;
	int len, status;

	/* In catatonic mode, we don't wait for nobody */
	if ( sbi->catatonic )
		return -ENOENT;
	
	name = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	len = autofs4_getpath(sbi, dentry, &name);
	if (!len) {
		kfree(name);
		return -ENOENT;
	}

	if (mutex_lock_interruptible(&sbi->wq_mutex)) {
		kfree(name);
		return -EINTR;
	}

	for (wq = sbi->queues ; wq ; wq = wq->next) {
		if (wq->hash == dentry->d_name.hash &&
		    wq->len == len &&
		    wq->name && !memcmp(wq->name, name, len))
			break;
	}

	if ( !wq ) {
		/* Can't wait for an expire if there's no mount */
		if (notify == NFY_NONE && !d_mountpoint(dentry)) {
			kfree(name);
			mutex_unlock(&sbi->wq_mutex);
			return -ENOENT;
		}

		/* Create a new wait queue */
		wq = kmalloc(sizeof(struct autofs_wait_queue),GFP_KERNEL);
		if ( !wq ) {
			kfree(name);
			mutex_unlock(&sbi->wq_mutex);
			return -ENOMEM;
		}

		wq->wait_queue_token = autofs4_next_wait_queue;
		if (++autofs4_next_wait_queue == 0)
			autofs4_next_wait_queue = 1;
		wq->next = sbi->queues;
		sbi->queues = wq;
		init_waitqueue_head(&wq->queue);
		wq->hash = dentry->d_name.hash;
		wq->name = name;
		wq->len = len;
		wq->status = -EINTR; /* Status return if interrupted */
		atomic_set(&wq->wait_ctr, 2);
		atomic_set(&wq->notified, 1);
		mutex_unlock(&sbi->wq_mutex);
	} else {
		atomic_inc(&wq->wait_ctr);
		mutex_unlock(&sbi->wq_mutex);
		kfree(name);
		DPRINTK("existing wait id = 0x%08lx, name = %.*s, nfy=%d",
			(unsigned long) wq->wait_queue_token, wq->len, wq->name, notify);
	}

	if (notify != NFY_NONE && atomic_dec_and_test(&wq->notified)) {
		int type = (notify == NFY_MOUNT ?
			autofs_ptype_missing : autofs_ptype_expire_multi);

		DPRINTK("new wait id = 0x%08lx, name = %.*s, nfy=%d\n",
			(unsigned long) wq->wait_queue_token, wq->len, wq->name, notify);

		/* autofs4_notify_daemon() may block */
		autofs4_notify_daemon(sbi, wq, type);
	}

	/* wq->name is NULL if and only if the lock is already released */

	if ( sbi->catatonic ) {
		/* We might have slept, so check again for catatonic mode */
		wq->status = -ENOENT;
		kfree(wq->name);
		wq->name = NULL;
	}

	if ( wq->name ) {
		/* Block all but "shutdown" signals while waiting */
		sigset_t oldset;
		unsigned long irqflags;

		spin_lock_irqsave(&current->sighand->siglock, irqflags);
		oldset = current->blocked;
		siginitsetinv(&current->blocked, SHUTDOWN_SIGS & ~oldset.sig[0]);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, irqflags);

		wait_event_interruptible(wq->queue, wq->name == NULL);

		spin_lock_irqsave(&current->sighand->siglock, irqflags);
		current->blocked = oldset;
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
	} else {
		DPRINTK("skipped sleeping");
	}

	status = wq->status;

	/* Are we the last process to need status? */
	if (atomic_dec_and_test(&wq->wait_ctr))
		kfree(wq);

	return status;
}


int autofs4_wait_release(struct autofs_sb_info *sbi, autofs_wqt_t wait_queue_token, int status)
{
	struct autofs_wait_queue *wq, **wql;

	mutex_lock(&sbi->wq_mutex);
	for ( wql = &sbi->queues ; (wq = *wql) != 0 ; wql = &wq->next ) {
		if ( wq->wait_queue_token == wait_queue_token )
			break;
	}

	if ( !wq ) {
		mutex_unlock(&sbi->wq_mutex);
		return -EINVAL;
	}

	*wql = wq->next;	/* Unlink from chain */
	mutex_unlock(&sbi->wq_mutex);
	kfree(wq->name);
	wq->name = NULL;	/* Do not wait on this queue */

	wq->status = status;

	if (atomic_dec_and_test(&wq->wait_ctr))	/* Is anyone still waiting for this guy? */
		kfree(wq);
	else
		wake_up_interruptible(&wq->queue);

	return 0;
}

