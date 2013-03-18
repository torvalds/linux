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

	mutex_lock(&sbi->wq_mutex);
	if (sbi->catatonic) {
		mutex_unlock(&sbi->wq_mutex);
		return;
	}

	DPRINTK("entering catatonic mode");

	sbi->catatonic = 1;
	wq = sbi->queues;
	sbi->queues = NULL;	/* Erase all wait queues */
	while (wq) {
		nwq = wq->next;
		wq->status = -ENOENT; /* Magic is gone - report failure */
		kfree(wq->name.name);
		wq->name.name = NULL;
		wq->wait_ctr--;
		wake_up_interruptible(&wq->queue);
		wq = nwq;
	}
	fput(sbi->pipe);	/* Close the pipe */
	sbi->pipe = NULL;
	sbi->pipefd = -1;
	mutex_unlock(&sbi->wq_mutex);
}

static int autofs4_write(struct autofs_sb_info *sbi,
			 struct file *file, const void *addr, int bytes)
{
	unsigned long sigpipe, flags;
	mm_segment_t fs;
	const char *data = (const char *)addr;
	ssize_t wr = 0;

	sigpipe = sigismember(&current->pending.signal, SIGPIPE);

	/* Save pointer to user space and point back to kernel space */
	fs = get_fs();
	set_fs(KERNEL_DS);

	mutex_lock(&sbi->pipe_mutex);
	while (bytes &&
	       (wr = file->f_op->write(file,data,bytes,&file->f_pos)) > 0) {
		data += wr;
		bytes -= wr;
	}
	mutex_unlock(&sbi->pipe_mutex);

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
	union {
		struct autofs_packet_hdr hdr;
		union autofs_packet_union v4_pkt;
		union autofs_v5_packet_union v5_pkt;
	} pkt;
	struct file *pipe = NULL;
	size_t pktsz;

	DPRINTK("wait id = 0x%08lx, name = %.*s, type=%d",
		(unsigned long) wq->wait_queue_token, wq->name.len, wq->name.name, type);

	memset(&pkt,0,sizeof pkt); /* For security reasons */

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = type;
	mutex_lock(&sbi->wq_mutex);

	/* Check if we have become catatonic */
	if (sbi->catatonic) {
		mutex_unlock(&sbi->wq_mutex);
		return;
	}
	switch (type) {
	/* Kernel protocol v4 missing and expire packets */
	case autofs_ptype_missing:
	{
		struct autofs_packet_missing *mp = &pkt.v4_pkt.missing;

		pktsz = sizeof(*mp);

		mp->wait_queue_token = wq->wait_queue_token;
		mp->len = wq->name.len;
		memcpy(mp->name, wq->name.name, wq->name.len);
		mp->name[wq->name.len] = '\0';
		break;
	}
	case autofs_ptype_expire_multi:
	{
		struct autofs_packet_expire_multi *ep = &pkt.v4_pkt.expire_multi;

		pktsz = sizeof(*ep);

		ep->wait_queue_token = wq->wait_queue_token;
		ep->len = wq->name.len;
		memcpy(ep->name, wq->name.name, wq->name.len);
		ep->name[wq->name.len] = '\0';
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
		struct autofs_v5_packet *packet = &pkt.v5_pkt.v5_packet;
		struct user_namespace *user_ns = sbi->pipe->f_cred->user_ns;

		pktsz = sizeof(*packet);

		packet->wait_queue_token = wq->wait_queue_token;
		packet->len = wq->name.len;
		memcpy(packet->name, wq->name.name, wq->name.len);
		packet->name[wq->name.len] = '\0';
		packet->dev = wq->dev;
		packet->ino = wq->ino;
		packet->uid = from_kuid_munged(user_ns, wq->uid);
		packet->gid = from_kgid_munged(user_ns, wq->gid);
		packet->pid = wq->pid;
		packet->tgid = wq->tgid;
		break;
	}
	default:
		printk("autofs4_notify_daemon: bad type %d!\n", type);
		mutex_unlock(&sbi->wq_mutex);
		return;
	}

	pipe = get_file(sbi->pipe);

	mutex_unlock(&sbi->wq_mutex);

	if (autofs4_write(sbi, pipe, &pkt, pktsz))
		autofs4_catatonic_mode(sbi);
	fput(pipe);
}

static int autofs4_getpath(struct autofs_sb_info *sbi,
			   struct dentry *dentry, char **name)
{
	struct dentry *root = sbi->sb->s_root;
	struct dentry *tmp;
	char *buf;
	char *p;
	int len;
	unsigned seq;

rename_retry:
	buf = *name;
	len = 0;

	seq = read_seqbegin(&rename_lock);
	rcu_read_lock();
	spin_lock(&sbi->fs_lock);
	for (tmp = dentry ; tmp != root ; tmp = tmp->d_parent)
		len += tmp->d_name.len + 1;

	if (!len || --len > NAME_MAX) {
		spin_unlock(&sbi->fs_lock);
		rcu_read_unlock();
		if (read_seqretry(&rename_lock, seq))
			goto rename_retry;
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
	spin_unlock(&sbi->fs_lock);
	rcu_read_unlock();
	if (read_seqretry(&rename_lock, seq))
		goto rename_retry;

	return len;
}

static struct autofs_wait_queue *
autofs4_find_wait(struct autofs_sb_info *sbi, struct qstr *qstr)
{
	struct autofs_wait_queue *wq;

	for (wq = sbi->queues; wq; wq = wq->next) {
		if (wq->name.hash == qstr->hash &&
		    wq->name.len == qstr->len &&
		    wq->name.name &&
			 !memcmp(wq->name.name, qstr->name, qstr->len))
			break;
	}
	return wq;
}

/*
 * Check if we have a valid request.
 * Returns
 * 1 if the request should continue.
 *   In this case we can return an autofs_wait_queue entry if one is
 *   found or NULL to idicate a new wait needs to be created.
 * 0 or a negative errno if the request shouldn't continue.
 */
static int validate_request(struct autofs_wait_queue **wait,
			    struct autofs_sb_info *sbi,
			    struct qstr *qstr,
			    struct dentry*dentry, enum autofs_notify notify)
{
	struct autofs_wait_queue *wq;
	struct autofs_info *ino;

	if (sbi->catatonic)
		return -ENOENT;

	/* Wait in progress, continue; */
	wq = autofs4_find_wait(sbi, qstr);
	if (wq) {
		*wait = wq;
		return 1;
	}

	*wait = NULL;

	/* If we don't yet have any info this is a new request */
	ino = autofs4_dentry_ino(dentry);
	if (!ino)
		return 1;

	/*
	 * If we've been asked to wait on an existing expire (NFY_NONE)
	 * but there is no wait in the queue ...
	 */
	if (notify == NFY_NONE) {
		/*
		 * Either we've betean the pending expire to post it's
		 * wait or it finished while we waited on the mutex.
		 * So we need to wait till either, the wait appears
		 * or the expire finishes.
		 */

		while (ino->flags & AUTOFS_INF_EXPIRING) {
			mutex_unlock(&sbi->wq_mutex);
			schedule_timeout_interruptible(HZ/10);
			if (mutex_lock_interruptible(&sbi->wq_mutex))
				return -EINTR;

			if (sbi->catatonic)
				return -ENOENT;

			wq = autofs4_find_wait(sbi, qstr);
			if (wq) {
				*wait = wq;
				return 1;
			}
		}

		/*
		 * Not ideal but the status has already gone. Of the two
		 * cases where we wait on NFY_NONE neither depend on the
		 * return status of the wait.
		 */
		return 0;
	}

	/*
	 * If we've been asked to trigger a mount and the request
	 * completed while we waited on the mutex ...
	 */
	if (notify == NFY_MOUNT) {
		struct dentry *new = NULL;
		int valid = 1;

		/*
		 * If the dentry was successfully mounted while we slept
		 * on the wait queue mutex we can return success. If it
		 * isn't mounted (doesn't have submounts for the case of
		 * a multi-mount with no mount at it's base) we can
		 * continue on and create a new request.
		 */
		if (!IS_ROOT(dentry)) {
			if (dentry->d_inode && d_unhashed(dentry)) {
				struct dentry *parent = dentry->d_parent;
				new = d_lookup(parent, &dentry->d_name);
				if (new)
					dentry = new;
			}
		}
		if (have_submounts(dentry))
			valid = 0;

		if (new)
			dput(new);
		return valid;
	}

	return 1;
}

int autofs4_wait(struct autofs_sb_info *sbi, struct dentry *dentry,
		enum autofs_notify notify)
{
	struct autofs_wait_queue *wq;
	struct qstr qstr;
	char *name;
	int status, ret, type;

	/* In catatonic mode, we don't wait for nobody */
	if (sbi->catatonic)
		return -ENOENT;

	if (!dentry->d_inode) {
		/*
		 * A wait for a negative dentry is invalid for certain
		 * cases. A direct or offset mount "always" has its mount
		 * point directory created and so the request dentry must
		 * be positive or the map key doesn't exist. The situation
		 * is very similar for indirect mounts except only dentrys
		 * in the root of the autofs file system may be negative.
		 */
		if (autofs_type_trigger(sbi->type))
			return -ENOENT;
		else if (!IS_ROOT(dentry->d_parent))
			return -ENOENT;
	}

	name = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	/* If this is a direct mount request create a dummy name */
	if (IS_ROOT(dentry) && autofs_type_trigger(sbi->type))
		qstr.len = sprintf(name, "%p", dentry);
	else {
		qstr.len = autofs4_getpath(sbi, dentry, &name);
		if (!qstr.len) {
			kfree(name);
			return -ENOENT;
		}
	}
	qstr.name = name;
	qstr.hash = full_name_hash(name, qstr.len);

	if (mutex_lock_interruptible(&sbi->wq_mutex)) {
		kfree(qstr.name);
		return -EINTR;
	}

	ret = validate_request(&wq, sbi, &qstr, dentry, notify);
	if (ret <= 0) {
		if (ret != -EINTR)
			mutex_unlock(&sbi->wq_mutex);
		kfree(qstr.name);
		return ret;
	}

	if (!wq) {
		/* Create a new wait queue */
		wq = kmalloc(sizeof(struct autofs_wait_queue),GFP_KERNEL);
		if (!wq) {
			kfree(qstr.name);
			mutex_unlock(&sbi->wq_mutex);
			return -ENOMEM;
		}

		wq->wait_queue_token = autofs4_next_wait_queue;
		if (++autofs4_next_wait_queue == 0)
			autofs4_next_wait_queue = 1;
		wq->next = sbi->queues;
		sbi->queues = wq;
		init_waitqueue_head(&wq->queue);
		memcpy(&wq->name, &qstr, sizeof(struct qstr));
		wq->dev = autofs4_get_dev(sbi);
		wq->ino = autofs4_get_ino(sbi);
		wq->uid = current_uid();
		wq->gid = current_gid();
		wq->pid = current->pid;
		wq->tgid = current->tgid;
		wq->status = -EINTR; /* Status return if interrupted */
		wq->wait_ctr = 2;
		mutex_unlock(&sbi->wq_mutex);

		if (sbi->version < 5) {
			if (notify == NFY_MOUNT)
				type = autofs_ptype_missing;
			else
				type = autofs_ptype_expire_multi;
		} else {
			if (notify == NFY_MOUNT)
				type = autofs_type_trigger(sbi->type) ?
					autofs_ptype_missing_direct :
					 autofs_ptype_missing_indirect;
			else
				type = autofs_type_trigger(sbi->type) ?
					autofs_ptype_expire_direct :
					autofs_ptype_expire_indirect;
		}

		DPRINTK("new wait id = 0x%08lx, name = %.*s, nfy=%d\n",
			(unsigned long) wq->wait_queue_token, wq->name.len,
			wq->name.name, notify);

		/* autofs4_notify_daemon() may block */
		autofs4_notify_daemon(sbi, wq, type);
	} else {
		wq->wait_ctr++;
		mutex_unlock(&sbi->wq_mutex);
		kfree(qstr.name);
		DPRINTK("existing wait id = 0x%08lx, name = %.*s, nfy=%d",
			(unsigned long) wq->wait_queue_token, wq->name.len,
			wq->name.name, notify);
	}

	/*
	 * wq->name.name is NULL iff the lock is already released
	 * or the mount has been made catatonic.
	 */
	if (wq->name.name) {
		/* Block all but "shutdown" signals while waiting */
		sigset_t oldset;
		unsigned long irqflags;

		spin_lock_irqsave(&current->sighand->siglock, irqflags);
		oldset = current->blocked;
		siginitsetinv(&current->blocked, SHUTDOWN_SIGS & ~oldset.sig[0]);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, irqflags);

		wait_event_interruptible(wq->queue, wq->name.name == NULL);

		spin_lock_irqsave(&current->sighand->siglock, irqflags);
		current->blocked = oldset;
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
	} else {
		DPRINTK("skipped sleeping");
	}

	status = wq->status;

	/*
	 * For direct and offset mounts we need to track the requester's
	 * uid and gid in the dentry info struct. This is so it can be
	 * supplied, on request, by the misc device ioctl interface.
	 * This is needed during daemon resatart when reconnecting
	 * to existing, active, autofs mounts. The uid and gid (and
	 * related string values) may be used for macro substitution
	 * in autofs mount maps.
	 */
	if (!status) {
		struct autofs_info *ino;
		struct dentry *de = NULL;

		/* direct mount or browsable map */
		ino = autofs4_dentry_ino(dentry);
		if (!ino) {
			/* If not lookup actual dentry used */
			de = d_lookup(dentry->d_parent, &dentry->d_name);
			if (de)
				ino = autofs4_dentry_ino(de);
		}

		/* Set mount requester */
		if (ino) {
			spin_lock(&sbi->fs_lock);
			ino->uid = wq->uid;
			ino->gid = wq->gid;
			spin_unlock(&sbi->fs_lock);
		}

		if (de)
			dput(de);
	}

	/* Are we the last process to need status? */
	mutex_lock(&sbi->wq_mutex);
	if (!--wq->wait_ctr)
		kfree(wq);
	mutex_unlock(&sbi->wq_mutex);

	return status;
}


int autofs4_wait_release(struct autofs_sb_info *sbi, autofs_wqt_t wait_queue_token, int status)
{
	struct autofs_wait_queue *wq, **wql;

	mutex_lock(&sbi->wq_mutex);
	for (wql = &sbi->queues; (wq = *wql) != NULL; wql = &wq->next) {
		if (wq->wait_queue_token == wait_queue_token)
			break;
	}

	if (!wq) {
		mutex_unlock(&sbi->wq_mutex);
		return -EINVAL;
	}

	*wql = wq->next;	/* Unlink from chain */
	kfree(wq->name.name);
	wq->name.name = NULL;	/* Do not wait on this queue */
	wq->status = status;
	wake_up_interruptible(&wq->queue);
	if (!--wq->wait_ctr)
		kfree(wq);
	mutex_unlock(&sbi->wq_mutex);

	return 0;
}

