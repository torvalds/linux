// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/tty.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>

static int is_ignored(int sig)
{
	return (sigismember(&current->blocked, sig) ||
		current->sighand->action[sig-1].sa.sa_handler == SIG_IGN);
}

/**
 *	tty_check_change	-	check for POSIX terminal changes
 *	@tty: tty to check
 *
 *	If we try to write to, or set the state of, a terminal and we're
 *	not in the foreground, send a SIGTTOU.  If the signal is blocked or
 *	ignored, go ahead and perform the operation.  (POSIX 7.2)
 *
 *	Locking: ctrl_lock
 */
int __tty_check_change(struct tty_struct *tty, int sig)
{
	unsigned long flags;
	struct pid *pgrp, *tty_pgrp;
	int ret = 0;

	if (current->signal->tty != tty)
		return 0;

	rcu_read_lock();
	pgrp = task_pgrp(current);

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	tty_pgrp = tty->pgrp;
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	if (tty_pgrp && pgrp != tty_pgrp) {
		if (is_ignored(sig)) {
			if (sig == SIGTTIN)
				ret = -EIO;
		} else if (is_current_pgrp_orphaned())
			ret = -EIO;
		else {
			kill_pgrp(pgrp, sig, 1);
			set_thread_flag(TIF_SIGPENDING);
			ret = -ERESTARTSYS;
		}
	}
	rcu_read_unlock();

	if (!tty_pgrp)
		tty_warn(tty, "sig=%d, tty->pgrp == NULL!\n", sig);

	return ret;
}

int tty_check_change(struct tty_struct *tty)
{
	return __tty_check_change(tty, SIGTTOU);
}
EXPORT_SYMBOL(tty_check_change);

void proc_clear_tty(struct task_struct *p)
{
	unsigned long flags;
	struct tty_struct *tty;
	spin_lock_irqsave(&p->sighand->siglock, flags);
	tty = p->signal->tty;
	p->signal->tty = NULL;
	spin_unlock_irqrestore(&p->sighand->siglock, flags);
	tty_kref_put(tty);
}

/**
 * proc_set_tty -  set the controlling terminal
 *
 * Only callable by the session leader and only if it does not already have
 * a controlling terminal.
 *
 * Caller must hold:  tty_lock()
 *		      a readlock on tasklist_lock
 *		      sighand lock
 */
static void __proc_set_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	/*
	 * The session and fg pgrp references will be non-NULL if
	 * tiocsctty() is stealing the controlling tty
	 */
	put_pid(tty->session);
	put_pid(tty->pgrp);
	tty->pgrp = get_pid(task_pgrp(current));
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	tty->session = get_pid(task_session(current));
	if (current->signal->tty) {
		tty_debug(tty, "current tty %s not NULL!!\n",
			  current->signal->tty->name);
		tty_kref_put(current->signal->tty);
	}
	put_pid(current->signal->tty_old_pgrp);
	current->signal->tty = tty_kref_get(tty);
	current->signal->tty_old_pgrp = NULL;
}

static void proc_set_tty(struct tty_struct *tty)
{
	spin_lock_irq(&current->sighand->siglock);
	__proc_set_tty(tty);
	spin_unlock_irq(&current->sighand->siglock);
}

/*
 * Called by tty_open() to set the controlling tty if applicable.
 */
void tty_open_proc_set_tty(struct file *filp, struct tty_struct *tty)
{
	read_lock(&tasklist_lock);
	spin_lock_irq(&current->sighand->siglock);
	if (current->signal->leader &&
	    !current->signal->tty &&
	    tty->session == NULL) {
		/*
		 * Don't let a process that only has write access to the tty
		 * obtain the privileges associated with having a tty as
		 * controlling terminal (being able to reopen it with full
		 * access through /dev/tty, being able to perform pushback).
		 * Many distributions set the group of all ttys to "tty" and
		 * grant write-only access to all terminals for setgid tty
		 * binaries, which should not imply full privileges on all ttys.
		 *
		 * This could theoretically break old code that performs open()
		 * on a write-only file descriptor. In that case, it might be
		 * necessary to also permit this if
		 * inode_permission(inode, MAY_READ) == 0.
		 */
		if (filp->f_mode & FMODE_READ)
			__proc_set_tty(tty);
	}
	spin_unlock_irq(&current->sighand->siglock);
	read_unlock(&tasklist_lock);
}

struct tty_struct *get_current_tty(void)
{
	struct tty_struct *tty;
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	tty = tty_kref_get(current->signal->tty);
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
	return tty;
}
EXPORT_SYMBOL_GPL(get_current_tty);

/*
 * Called from tty_release().
 */
void session_clear_tty(struct pid *session)
{
	struct task_struct *p;
	do_each_pid_task(session, PIDTYPE_SID, p) {
		proc_clear_tty(p);
	} while_each_pid_task(session, PIDTYPE_SID, p);
}

/**
 *	tty_signal_session_leader	- sends SIGHUP to session leader
 *	@tty: controlling tty
 *	@exit_session: if non-zero, signal all foreground group processes
 *
 *	Send SIGHUP and SIGCONT to the session leader and its process group.
 *	Optionally, signal all processes in the foreground process group.
 *
 *	Returns the number of processes in the session with this tty
 *	as their controlling terminal. This value is used to drop
 *	tty references for those processes.
 */
int tty_signal_session_leader(struct tty_struct *tty, int exit_session)
{
	struct task_struct *p;
	int refs = 0;
	struct pid *tty_pgrp = NULL;

	read_lock(&tasklist_lock);
	if (tty->session) {
		do_each_pid_task(tty->session, PIDTYPE_SID, p) {
			spin_lock_irq(&p->sighand->siglock);
			if (p->signal->tty == tty) {
				p->signal->tty = NULL;
				/* We defer the dereferences outside fo
				   the tasklist lock */
				refs++;
			}
			if (!p->signal->leader) {
				spin_unlock_irq(&p->sighand->siglock);
				continue;
			}
			__group_send_sig_info(SIGHUP, SEND_SIG_PRIV, p);
			__group_send_sig_info(SIGCONT, SEND_SIG_PRIV, p);
			put_pid(p->signal->tty_old_pgrp);  /* A noop */
			spin_lock(&tty->ctrl_lock);
			tty_pgrp = get_pid(tty->pgrp);
			if (tty->pgrp)
				p->signal->tty_old_pgrp = get_pid(tty->pgrp);
			spin_unlock(&tty->ctrl_lock);
			spin_unlock_irq(&p->sighand->siglock);
		} while_each_pid_task(tty->session, PIDTYPE_SID, p);
	}
	read_unlock(&tasklist_lock);

	if (tty_pgrp) {
		if (exit_session)
			kill_pgrp(tty_pgrp, SIGHUP, exit_session);
		put_pid(tty_pgrp);
	}

	return refs;
}

/**
 *	disassociate_ctty	-	disconnect controlling tty
 *	@on_exit: true if exiting so need to "hang up" the session
 *
 *	This function is typically called only by the session leader, when
 *	it wants to disassociate itself from its controlling tty.
 *
 *	It performs the following functions:
 * 	(1)  Sends a SIGHUP and SIGCONT to the foreground process group
 * 	(2)  Clears the tty from being controlling the session
 * 	(3)  Clears the controlling tty for all processes in the
 * 		session group.
 *
 *	The argument on_exit is set to 1 if called when a process is
 *	exiting; it is 0 if called by the ioctl TIOCNOTTY.
 *
 *	Locking:
 *		BTM is taken for hysterical raisons, and held when
 *		  called from no_tty().
 *		  tty_mutex is taken to protect tty
 *		  ->siglock is taken to protect ->signal/->sighand
 *		  tasklist_lock is taken to walk process list for sessions
 *		    ->siglock is taken to protect ->signal/->sighand
 */
void disassociate_ctty(int on_exit)
{
	struct tty_struct *tty;

	if (!current->signal->leader)
		return;

	tty = get_current_tty();
	if (tty) {
		if (on_exit && tty->driver->type != TTY_DRIVER_TYPE_PTY) {
			tty_vhangup_session(tty);
		} else {
			struct pid *tty_pgrp = tty_get_pgrp(tty);
			if (tty_pgrp) {
				kill_pgrp(tty_pgrp, SIGHUP, on_exit);
				if (!on_exit)
					kill_pgrp(tty_pgrp, SIGCONT, on_exit);
				put_pid(tty_pgrp);
			}
		}
		tty_kref_put(tty);

	} else if (on_exit) {
		struct pid *old_pgrp;
		spin_lock_irq(&current->sighand->siglock);
		old_pgrp = current->signal->tty_old_pgrp;
		current->signal->tty_old_pgrp = NULL;
		spin_unlock_irq(&current->sighand->siglock);
		if (old_pgrp) {
			kill_pgrp(old_pgrp, SIGHUP, on_exit);
			kill_pgrp(old_pgrp, SIGCONT, on_exit);
			put_pid(old_pgrp);
		}
		return;
	}

	spin_lock_irq(&current->sighand->siglock);
	put_pid(current->signal->tty_old_pgrp);
	current->signal->tty_old_pgrp = NULL;

	tty = tty_kref_get(current->signal->tty);
	if (tty) {
		unsigned long flags;
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		put_pid(tty->session);
		put_pid(tty->pgrp);
		tty->session = NULL;
		tty->pgrp = NULL;
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		tty_kref_put(tty);
	}

	spin_unlock_irq(&current->sighand->siglock);
	/* Now clear signal->tty under the lock */
	read_lock(&tasklist_lock);
	session_clear_tty(task_session(current));
	read_unlock(&tasklist_lock);
}

/*
 *
 *	no_tty	- Ensure the current process does not have a controlling tty
 */
void no_tty(void)
{
	/* FIXME: Review locking here. The tty_lock never covered any race
	   between a new association and proc_clear_tty but possible we need
	   to protect against this anyway */
	struct task_struct *tsk = current;
	disassociate_ctty(0);
	proc_clear_tty(tsk);
}

/**
 *	tiocsctty	-	set controlling tty
 *	@tty: tty structure
 *	@arg: user argument
 *
 *	This ioctl is used to manage job control. It permits a session
 *	leader to set this tty as the controlling tty for the session.
 *
 *	Locking:
 *		Takes tty_lock() to serialize proc_set_tty() for this tty
 *		Takes tasklist_lock internally to walk sessions
 *		Takes ->siglock() when updating signal->tty
 */
static int tiocsctty(struct tty_struct *tty, struct file *file, int arg)
{
	int ret = 0;

	tty_lock(tty);
	read_lock(&tasklist_lock);

	if (current->signal->leader && (task_session(current) == tty->session))
		goto unlock;

	/*
	 * The process must be a session leader and
	 * not have a controlling tty already.
	 */
	if (!current->signal->leader || current->signal->tty) {
		ret = -EPERM;
		goto unlock;
	}

	if (tty->session) {
		/*
		 * This tty is already the controlling
		 * tty for another session group!
		 */
		if (arg == 1 && capable(CAP_SYS_ADMIN)) {
			/*
			 * Steal it away
			 */
			session_clear_tty(tty->session);
		} else {
			ret = -EPERM;
			goto unlock;
		}
	}

	/* See the comment in tty_open_proc_set_tty(). */
	if ((file->f_mode & FMODE_READ) == 0 && !capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto unlock;
	}

	proc_set_tty(tty);
unlock:
	read_unlock(&tasklist_lock);
	tty_unlock(tty);
	return ret;
}

/**
 *	tty_get_pgrp	-	return a ref counted pgrp pid
 *	@tty: tty to read
 *
 *	Returns a refcounted instance of the pid struct for the process
 *	group controlling the tty.
 */
struct pid *tty_get_pgrp(struct tty_struct *tty)
{
	unsigned long flags;
	struct pid *pgrp;

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	pgrp = get_pid(tty->pgrp);
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	return pgrp;
}
EXPORT_SYMBOL_GPL(tty_get_pgrp);

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 *
 * The caller must hold rcu lock or the tasklist lock.
 */
static struct pid *session_of_pgrp(struct pid *pgrp)
{
	struct task_struct *p;
	struct pid *sid = NULL;

	p = pid_task(pgrp, PIDTYPE_PGID);
	if (p == NULL)
		p = pid_task(pgrp, PIDTYPE_PID);
	if (p != NULL)
		sid = task_session(p);

	return sid;
}

/**
 *	tiocgpgrp		-	get process group
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty passed by the user if a pty else the tty
 *	@p: returned pid
 *
 *	Obtain the process group of the tty. If there is no process group
 *	return an error.
 *
 *	Locking: none. Reference to current->signal->tty is safe.
 */
static int tiocgpgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	struct pid *pid;
	int ret;
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	 */
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	pid = tty_get_pgrp(real_tty);
	ret =  put_user(pid_vnr(pid), p);
	put_pid(pid);
	return ret;
}

/**
 *	tiocspgrp		-	attempt to set process group
 *	@tty: tty passed by user
 *	@real_tty: tty side device matching tty passed by user
 *	@p: pid pointer
 *
 *	Set the process group of the tty to the session passed. Only
 *	permitted where the tty session is our session.
 *
 *	Locking: RCU, ctrl lock
 */
static int tiocspgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	struct pid *pgrp;
	pid_t pgrp_nr;
	int retval = tty_check_change(real_tty);

	if (retval == -EIO)
		return -ENOTTY;
	if (retval)
		return retval;
	if (!current->signal->tty ||
	    (current->signal->tty != real_tty) ||
	    (real_tty->session != task_session(current)))
		return -ENOTTY;
	if (get_user(pgrp_nr, p))
		return -EFAULT;
	if (pgrp_nr < 0)
		return -EINVAL;
	rcu_read_lock();
	pgrp = find_vpid(pgrp_nr);
	retval = -ESRCH;
	if (!pgrp)
		goto out_unlock;
	retval = -EPERM;
	if (session_of_pgrp(pgrp) != task_session(current))
		goto out_unlock;
	retval = 0;
	spin_lock_irq(&tty->ctrl_lock);
	put_pid(real_tty->pgrp);
	real_tty->pgrp = get_pid(pgrp);
	spin_unlock_irq(&tty->ctrl_lock);
out_unlock:
	rcu_read_unlock();
	return retval;
}

/**
 *	tiocgsid		-	get session id
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty passed by the user if a pty else the tty
 *	@p: pointer to returned session id
 *
 *	Obtain the session id of the tty. If there is no session
 *	return an error.
 *
 *	Locking: none. Reference to current->signal->tty is safe.
 */
static int tiocgsid(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	*/
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	if (!real_tty->session)
		return -ENOTTY;
	return put_user(pid_vnr(real_tty->session), p);
}

/*
 * Called from tty_ioctl(). If tty is a pty then real_tty is the slave side,
 * if not then tty == real_tty.
 */
long tty_jobctrl_ioctl(struct tty_struct *tty, struct tty_struct *real_tty,
		       struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;

	switch (cmd) {
	case TIOCNOTTY:
		if (current->signal->tty != tty)
			return -ENOTTY;
		no_tty();
		return 0;
	case TIOCSCTTY:
		return tiocsctty(real_tty, file, arg);
	case TIOCGPGRP:
		return tiocgpgrp(tty, real_tty, p);
	case TIOCSPGRP:
		return tiocspgrp(tty, real_tty, p);
	case TIOCGSID:
		return tiocgsid(tty, real_tty, p);
	}
	return -ENOIOCTLCMD;
}
