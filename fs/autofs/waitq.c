/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/waitq.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
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
static autofs_wqt_t autofs_next_wait_queue = 1;

/* These are the signals we allow interrupting a pending mount */
#define SHUTDOWN_SIGS	(sigmask(SIGKILL) | sigmask(SIGINT) | sigmask(SIGQUIT))

void autofs_catatonic_mode(struct autofs_sb_info *sbi)
{
	struct autofs_wait_queue *wq, *nwq;

	DPRINTK(("autofs: entering catatonic mode\n"));

	sbi->catatonic = 1;
	wq = sbi->queues;
	sbi->queues = NULL;	/* Erase all wait queues */
	while ( wq ) {
		nwq = wq->next;
		wq->status = -ENOENT; /* Magic is gone - report failure */
		kfree(wq->name);
		wq->name = NULL;
		wake_up(&wq->queue);
		wq = nwq;
	}
	fput(sbi->pipe);	/* Close the pipe */
	sbi->pipe = NULL;
	autofs_hash_dputall(&sbi->dirhash); /* Remove all dentry pointers */
}

static int autofs_write(struct file *file, const void *addr, int bytes)
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
	
static void autofs_notify_daemon(struct autofs_sb_info *sbi, struct autofs_wait_queue *wq)
{
	struct autofs_packet_missing pkt;

	DPRINTK(("autofs_wait: wait id = 0x%08lx, name = ", wq->wait_queue_token));
	autofs_say(wq->name,wq->len);

	memset(&pkt,0,sizeof pkt); /* For security reasons */

	pkt.hdr.proto_version = AUTOFS_PROTO_VERSION;
	pkt.hdr.type = autofs_ptype_missing;
	pkt.wait_queue_token = wq->wait_queue_token;
	pkt.len = wq->len;
        memcpy(pkt.name, wq->name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if ( autofs_write(sbi->pipe,&pkt,sizeof(struct autofs_packet_missing)) )
		autofs_catatonic_mode(sbi);
}

int autofs_wait(struct autofs_sb_info *sbi, struct qstr *name)
{
	struct autofs_wait_queue *wq;
	int status;

	/* In catatonic mode, we don't wait for nobody */
	if ( sbi->catatonic )
		return -ENOENT;
	
	/* We shouldn't be able to get here, but just in case */
	if ( name->len > NAME_MAX )
		return -ENOENT;

	for ( wq = sbi->queues ; wq ; wq = wq->next ) {
		if ( wq->hash == name->hash &&
		     wq->len == name->len &&
		     wq->name && !memcmp(wq->name,name->name,name->len) )
			break;
	}
	
	if ( !wq ) {
		/* Create a new wait queue */
		wq = kmalloc(sizeof(struct autofs_wait_queue),GFP_KERNEL);
		if ( !wq )
			return -ENOMEM;

		wq->name = kmalloc(name->len,GFP_KERNEL);
		if ( !wq->name ) {
			kfree(wq);
			return -ENOMEM;
		}
		wq->wait_queue_token = autofs_next_wait_queue++;
		init_waitqueue_head(&wq->queue);
		wq->hash = name->hash;
		wq->len = name->len;
		wq->status = -EINTR; /* Status return if interrupted */
		memcpy(wq->name, name->name, name->len);
		wq->next = sbi->queues;
		sbi->queues = wq;

		/* autofs_notify_daemon() may block */
		wq->wait_ctr = 2;
		autofs_notify_daemon(sbi,wq);
	} else
		wq->wait_ctr++;

	/* wq->name is NULL if and only if the lock is already released */

	if ( sbi->catatonic ) {
		/* We might have slept, so check again for catatonic mode */
		wq->status = -ENOENT;
		kfree(wq->name);
		wq->name = NULL;
	}

	if ( wq->name ) {
		/* Block all but "shutdown" signals while waiting */
		sigset_t sigmask;

		siginitsetinv(&sigmask, SHUTDOWN_SIGS);
		sigprocmask(SIG_BLOCK, &sigmask, &sigmask);

		interruptible_sleep_on(&wq->queue);

		sigprocmask(SIG_SETMASK, &sigmask, NULL);
	} else {
		DPRINTK(("autofs_wait: skipped sleeping\n"));
	}

	status = wq->status;

	if ( ! --wq->wait_ctr )	/* Are we the last process to need status? */
		kfree(wq);

	return status;
}


int autofs_wait_release(struct autofs_sb_info *sbi, autofs_wqt_t wait_queue_token, int status)
{
	struct autofs_wait_queue *wq, **wql;

	for ( wql = &sbi->queues ; (wq = *wql) != 0 ; wql = &wq->next ) {
		if ( wq->wait_queue_token == wait_queue_token )
			break;
	}
	if ( !wq )
		return -EINVAL;

	*wql = wq->next;	/* Unlink from chain */
	kfree(wq->name);
	wq->name = NULL;	/* Do not wait on this queue */

	wq->status = status;

	if ( ! --wq->wait_ctr )	/* Is anyone still waiting for this guy? */
		kfree(wq);
	else
		wake_up(&wq->queue);

	return 0;
}

