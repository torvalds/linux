/*
 * Creating audit events from TTY input.
 *
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.  This copyrighted
 * material is made available to anyone wishing to use, modify, copy, or
 * redistribute it subject to the terms and conditions of the GNU General
 * Public License v.2.
 *
 * Authors: Miloslav Trmac <mitr@redhat.com>
 */

#include <linux/audit.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/tty.h>

struct tty_audit_buf {
	atomic_t count;
	struct mutex mutex;	/* Protects all data below */
	int major, minor;	/* The TTY which the data is from */
	unsigned icanon:1;
	size_t valid;
	unsigned char *data;	/* Allocated size N_TTY_BUF_SIZE */
};

static struct tty_audit_buf *tty_audit_buf_alloc(int major, int minor,
						 int icanon)
{
	struct tty_audit_buf *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		goto err;
	if (PAGE_SIZE != N_TTY_BUF_SIZE)
		buf->data = kmalloc(N_TTY_BUF_SIZE, GFP_KERNEL);
	else
		buf->data = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!buf->data)
		goto err_buf;
	atomic_set(&buf->count, 1);
	mutex_init(&buf->mutex);
	buf->major = major;
	buf->minor = minor;
	buf->icanon = icanon;
	buf->valid = 0;
	return buf;

err_buf:
	kfree(buf);
err:
	return NULL;
}

static void tty_audit_buf_free(struct tty_audit_buf *buf)
{
	WARN_ON(buf->valid != 0);
	if (PAGE_SIZE != N_TTY_BUF_SIZE)
		kfree(buf->data);
	else
		free_page((unsigned long)buf->data);
	kfree(buf);
}

static void tty_audit_buf_put(struct tty_audit_buf *buf)
{
	if (atomic_dec_and_test(&buf->count))
		tty_audit_buf_free(buf);
}

/**
 *	tty_audit_buf_push	-	Push buffered data out
 *
 *	Generate an audit message from the contents of @buf, which is owned by
 *	@tsk with @loginuid.  @buf->mutex must be locked.
 */
static void tty_audit_buf_push(struct task_struct *tsk, uid_t loginuid,
			       unsigned int sessionid,
			       struct tty_audit_buf *buf)
{
	struct audit_buffer *ab;

	if (buf->valid == 0)
		return;
	if (audit_enabled == 0)
		return;
	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_TTY);
	if (ab) {
		char name[sizeof(tsk->comm)];

		audit_log_format(ab, "tty pid=%u uid=%u auid=%u ses=%u "
				 "major=%d minor=%d comm=", tsk->pid, tsk->uid,
				 loginuid, sessionid, buf->major, buf->minor);
		get_task_comm(name, tsk);
		audit_log_untrustedstring(ab, name);
		audit_log_format(ab, " data=");
		audit_log_n_untrustedstring(ab, buf->data, buf->valid);
		audit_log_end(ab);
	}
	buf->valid = 0;
}

/**
 *	tty_audit_buf_push_current	-	Push buffered data out
 *
 *	Generate an audit message from the contents of @buf, which is owned by
 *	the current task.  @buf->mutex must be locked.
 */
static void tty_audit_buf_push_current(struct tty_audit_buf *buf)
{
	uid_t auid = audit_get_loginuid(current);
	unsigned int sessionid = audit_get_sessionid(current);
	tty_audit_buf_push(current, auid, sessionid, buf);
}

/**
 *	tty_audit_exit	-	Handle a task exit
 *
 *	Make sure all buffered data is written out and deallocate the buffer.
 *	Only needs to be called if current->signal->tty_audit_buf != %NULL.
 */
void tty_audit_exit(void)
{
	struct tty_audit_buf *buf;

	spin_lock_irq(&current->sighand->siglock);
	buf = current->signal->tty_audit_buf;
	current->signal->tty_audit_buf = NULL;
	spin_unlock_irq(&current->sighand->siglock);
	if (!buf)
		return;

	mutex_lock(&buf->mutex);
	tty_audit_buf_push_current(buf);
	mutex_unlock(&buf->mutex);

	tty_audit_buf_put(buf);
}

/**
 *	tty_audit_fork	-	Copy TTY audit state for a new task
 *
 *	Set up TTY audit state in @sig from current.  @sig needs no locking.
 */
void tty_audit_fork(struct signal_struct *sig)
{
	spin_lock_irq(&current->sighand->siglock);
	sig->audit_tty = current->signal->audit_tty;
	spin_unlock_irq(&current->sighand->siglock);
	sig->tty_audit_buf = NULL;
}

/**
 *	tty_audit_push_task	-	Flush task's pending audit data
 */
void tty_audit_push_task(struct task_struct *tsk, uid_t loginuid, u32 sessionid)
{
	struct tty_audit_buf *buf;

	spin_lock_irq(&tsk->sighand->siglock);
	buf = tsk->signal->tty_audit_buf;
	if (buf)
		atomic_inc(&buf->count);
	spin_unlock_irq(&tsk->sighand->siglock);
	if (!buf)
		return;

	mutex_lock(&buf->mutex);
	tty_audit_buf_push(tsk, loginuid, sessionid, buf);
	mutex_unlock(&buf->mutex);

	tty_audit_buf_put(buf);
}

/**
 *	tty_audit_buf_get	-	Get an audit buffer.
 *
 *	Get an audit buffer for @tty, allocate it if necessary.  Return %NULL
 *	if TTY auditing is disabled or out of memory.  Otherwise, return a new
 *	reference to the buffer.
 */
static struct tty_audit_buf *tty_audit_buf_get(struct tty_struct *tty)
{
	struct tty_audit_buf *buf, *buf2;

	buf = NULL;
	buf2 = NULL;
	spin_lock_irq(&current->sighand->siglock);
	if (likely(!current->signal->audit_tty))
		goto out;
	buf = current->signal->tty_audit_buf;
	if (buf) {
		atomic_inc(&buf->count);
		goto out;
	}
	spin_unlock_irq(&current->sighand->siglock);

	buf2 = tty_audit_buf_alloc(tty->driver->major,
				   tty->driver->minor_start + tty->index,
				   tty->icanon);
	if (buf2 == NULL) {
		audit_log_lost("out of memory in TTY auditing");
		return NULL;
	}

	spin_lock_irq(&current->sighand->siglock);
	if (!current->signal->audit_tty)
		goto out;
	buf = current->signal->tty_audit_buf;
	if (!buf) {
		current->signal->tty_audit_buf = buf2;
		buf = buf2;
		buf2 = NULL;
	}
	atomic_inc(&buf->count);
	/* Fall through */
 out:
	spin_unlock_irq(&current->sighand->siglock);
	if (buf2)
		tty_audit_buf_free(buf2);
	return buf;
}

/**
 *	tty_audit_add_data	-	Add data for TTY auditing.
 *
 *	Audit @data of @size from @tty, if necessary.
 */
void tty_audit_add_data(struct tty_struct *tty, unsigned char *data,
			size_t size)
{
	struct tty_audit_buf *buf;
	int major, minor;

	if (unlikely(size == 0))
		return;

	if (tty->driver->type == TTY_DRIVER_TYPE_PTY
	    && tty->driver->subtype == PTY_TYPE_MASTER)
		return;

	buf = tty_audit_buf_get(tty);
	if (!buf)
		return;

	mutex_lock(&buf->mutex);
	major = tty->driver->major;
	minor = tty->driver->minor_start + tty->index;
	if (buf->major != major || buf->minor != minor
	    || buf->icanon != tty->icanon) {
		tty_audit_buf_push_current(buf);
		buf->major = major;
		buf->minor = minor;
		buf->icanon = tty->icanon;
	}
	do {
		size_t run;

		run = N_TTY_BUF_SIZE - buf->valid;
		if (run > size)
			run = size;
		memcpy(buf->data + buf->valid, data, run);
		buf->valid += run;
		data += run;
		size -= run;
		if (buf->valid == N_TTY_BUF_SIZE)
			tty_audit_buf_push_current(buf);
	} while (size != 0);
	mutex_unlock(&buf->mutex);
	tty_audit_buf_put(buf);
}

/**
 *	tty_audit_push	-	Push buffered data out
 *
 *	Make sure no audit data is pending for @tty on the current process.
 */
void tty_audit_push(struct tty_struct *tty)
{
	struct tty_audit_buf *buf;

	spin_lock_irq(&current->sighand->siglock);
	if (likely(!current->signal->audit_tty)) {
		spin_unlock_irq(&current->sighand->siglock);
		return;
	}
	buf = current->signal->tty_audit_buf;
	if (buf)
		atomic_inc(&buf->count);
	spin_unlock_irq(&current->sighand->siglock);

	if (buf) {
		int major, minor;

		major = tty->driver->major;
		minor = tty->driver->minor_start + tty->index;
		mutex_lock(&buf->mutex);
		if (buf->major == major && buf->minor == minor)
			tty_audit_buf_push_current(buf);
		mutex_unlock(&buf->mutex);
		tty_audit_buf_put(buf);
	}
}
