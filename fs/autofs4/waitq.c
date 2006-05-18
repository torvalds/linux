/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/waitq.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 2001-2006 Ian Kent <raven@themaw.net>
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
	while (wq) {
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
	switch (type) {
	/* Kernel protocol v4 missing and expire packets */
	case autofs_ptype_missing:
	{
		struct autofs_packet_missing *mp = &pkt.missing;

		pktsz = sizeof(*mp);

		mp->wait_queue_token = wq->wait_queue_token;
		mp->len = wq->len;
		memcpy(mp->name, wq->name, wq->len);
		mp->name[wq->len] = '\0';
		break;
	}
	case autofs_ptype_expire_multi:
	{
		struct autofs_packet_expire_multi *ep = &pkt.expire_multi;

		pktsz = sizeof(*ep);

		ep->wait_queue_token = wq->wait_queue_token;
		ep->len = wq->len;
		memcpy(ep->name, wq->name, wq->len);
		ep->name[wq->len] = '\0';
		break;
	}
	/*
	 * Kernel protocol v5 packet for handling indirect and direct
	 * mount missing and expire requests
	 */
	case autofs_ptype_missing_indirect:
	case autofs_ptype_expire_indirect:
	case autofs_ptype_missing_direct:
	case autofs_ptype_expire_direct:
	{
		struct autofs_v5_packet *packet = &pkt.v5_packet;

		pktsz = sizeof(*packet);

		packet->wait_queue_token = wq->wait_queue_token;
		packet->len = wq->len;
		memcpy(packet->name, wq->name, wq->len);
		packet->name[wq->len] = '\0';
		packet->dev = wq->dev;
		packet->ino = wq->ino;
		packet->uid = wq->uid;
		packet->gid = wq->gid;
		packet->pid = wq->pid;
		packet->tgid = wq->tgid;
		break;
	}
	default:
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

static struct autofs_wait_queue *
autofs4_find_wait(struct autofs_sb_info *sbi,
		  char *name, unsigned int hash, unsigned int len)
{
	struct autofs_wait_queue *wq;

	for (wq = sbi->queues; wq; wq = wq->next) {
		if (wq->hash == hash &&
		    wq->len == len &&
		    wq->name && !memcmp(wq->name, name, len))
			break;
	}
	return wq;
}

int autofs4_wait(struct autofs_sb_info *sbi, struct dentry *dentry,
		enum autofs_notify notify)
{
	struct autofs_info *ino;
	struct autofs_wait_queue *wq;
	char *name;
	unsigned int len = 0;
	unsigned int hash = 0;
	int status, type;

	/* In catatonic mode, we don't wait for nobody */
	if (sbi->catatonic)
		return -ENOENT;
	
	name = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	/* If this is a direct mount request create a dummy name */
	if (IS_ROOT(dentry) && (sbi->type & AUTOFS_TYPE_DIRECT))
		len = sprintf(name, "%p", dentry);
	else {
		len = autofs4_getpath(sbi, dentry, &name);
		if (!len) {
			kfree(name);
			return -ENOENT;
		}
	}
	hash = full_name_hash(name, len);

	if (mutex_lock_interruptible(&sbi->wq_mutex)) {
		kfree(name);
		return -EINTR;
	}

	wq = autofs4_find_wait(sbi, name, hash, len);
	ino = autofs4_dentry_ino(dentry);
	if (!wq && ino && notify == NFY_NONE) {
		/*
		 * Either we've betean the pending expire to post it's
		 * wait or it finished while we waited on the mutex.
		 * So we need to wait till either, the wait appears
		 * or the expire finishes.
		 */

		while (ino->flags & AUTOFS_INF_EXPIRING) {
			mutex_unlock(&sbi->wq_mutex);
			schedule_timeout_interruptible(HZ/10);
			if (mutex_lock_interruptible(&sbi->wq_mutex)) {
				kfree(name);
				return -EINTR;
			}
			wq = autofs4_find_wait(sbi, name, hash, len);
			if (wq)
				break;
		}

		/*
		 * Not ideal but the status has already gone. Of the two
		 * cases where we wait on NFY_NONE neither depend on the
		 * return status of the wait.
		 */
		if (!wq) {
			kfree(name);
			mutex_unlock(&sbi->wq_mutex);
			return 0;
		}
	}

	if (!wq) {
		/* Create a new wait queue */
		wq = kmalloc(sizeof(struct autofs_wait_queue),GFP_KERNEL);
		if (!wq) {
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
		wq->hash = hash;
		wq->name = name;
		wq->len = len;
		wq->dev = autofs4_get_dev(sbi);
		wq->ino = autofs4_get_ino(sbi);
		wq->uid = current->uid;
		wq->gid = current->gid;
		wq->pid = current->pid;
		wq->tgid = current->tgid;
		wq->status = -EINTR; /* Status return if interrupted */
		atomic_set(&wq->wait_ctr, 2);
		mutex_unlock(&sbi->wq_mutex);

		if (sbi->version < 5) {
			if (notify == NFY_MOUNT)
				type = autofs_ptype_missing;
			else
				type = autofs_ptype_expire_multi;
		} else {
			if (notify == NFY_MOUNT)
				type = (sbi->type & AUTOFS_TYPE_DIRECT) ?
					autofs_ptype_missing_direct :
					 autofs_ptype_missing_indirect;
			else
				type = (sbi->type & AUTOFS_TYPE_DIRECT) ?
					autofs_ptype_expire_direct :
					autofs_ptype_expire_indirect;
		}

		DPRINTK("new wait id = 0x%08lx, name = %.*s, nfy=%d\n",
			(unsigned long) wq->wait_queue_token, wq->len, wq->name, notify);

		/* autofs4_notify_daemon() may block */
		autofs4_notify_daemon(sbi, wq, type);
	} else {
		atomic_inc(&wq->wait_ctr);
		mutex_unlock(&sbi->wq_mutex);
		kfree(name);
		DPRINTK("existing wait id = 0x%08lx, name = %.*s, nfy=%d",
			(unsigned long) wq->wait_queue_token, wq->len, wq->name, notify);
	}

	/* wq->name is NULL if and only if the lock is already released */

	if (sbi->catatonic) {
		/* We might have slept, so check again for catatonic mode */
		wq->status = -ENOENT;
		kfree(wq->name);
		wq->name = NULL;
	}

	if (wq->name) {
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
	for (wql = &sbi->queues ; (wq = *wql) != 0 ; wql = &wq->next) {
		if (wq->wait_queue_token == wait_queue_token)
			break;
	}

	if (!wq) {
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

