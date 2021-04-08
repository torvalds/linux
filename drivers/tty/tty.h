/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TTY core internal functions
 */

#ifndef _TTY_INTERNAL_H
#define _TTY_INTERNAL_H

#define tty_msg(fn, tty, f, ...) \
	fn("%s %s: " f, tty_driver_name(tty), tty_name(tty), ##__VA_ARGS__)

#define tty_debug(tty, f, ...)	tty_msg(pr_debug, tty, f, ##__VA_ARGS__)
#define tty_notice(tty, f, ...)	tty_msg(pr_notice, tty, f, ##__VA_ARGS__)
#define tty_warn(tty, f, ...)	tty_msg(pr_warn, tty, f, ##__VA_ARGS__)
#define tty_err(tty, f, ...)	tty_msg(pr_err, tty, f, ##__VA_ARGS__)

#define tty_info_ratelimited(tty, f, ...) \
		tty_msg(pr_info_ratelimited, tty, f, ##__VA_ARGS__)

/*
 * Lock subclasses for tty locks
 *
 * TTY_LOCK_NORMAL is for normal ttys and master ptys.
 * TTY_LOCK_SLAVE is for slave ptys only.
 *
 * Lock subclasses are necessary for handling nested locking with pty pairs.
 * tty locks which use nested locking:
 *
 * legacy_mutex - Nested tty locks are necessary for releasing pty pairs.
 *		  The stable lock order is master pty first, then slave pty.
 * termios_rwsem - The stable lock order is tty_buffer lock->termios_rwsem.
 *		   Subclassing this lock enables the slave pty to hold its
 *		   termios_rwsem when claiming the master tty_buffer lock.
 * tty_buffer lock - slave ptys can claim nested buffer lock when handling
 *		     signal chars. The stable lock order is slave pty, then
 *		     master.
 */
enum {
	TTY_LOCK_NORMAL = 0,
	TTY_LOCK_SLAVE,
};

/* Values for tty->flow_change */
#define TTY_THROTTLE_SAFE	1
#define TTY_UNTHROTTLE_SAFE	2

static inline void __tty_set_flow_change(struct tty_struct *tty, int val)
{
	tty->flow_change = val;
}

static inline void tty_set_flow_change(struct tty_struct *tty, int val)
{
	tty->flow_change = val;
	smp_mb();
}

int tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout);
void tty_ldisc_unlock(struct tty_struct *tty);

int __tty_check_change(struct tty_struct *tty, int sig);
int tty_check_change(struct tty_struct *tty);
void __stop_tty(struct tty_struct *tty);
void __start_tty(struct tty_struct *tty);
void tty_vhangup_session(struct tty_struct *tty);
void tty_open_proc_set_tty(struct file *filp, struct tty_struct *tty);
int tty_signal_session_leader(struct tty_struct *tty, int exit_session);
void session_clear_tty(struct pid *session);
void tty_buffer_free_all(struct tty_port *port);
void tty_buffer_flush(struct tty_struct *tty, struct tty_ldisc *ld);
void tty_buffer_init(struct tty_port *port);
void tty_buffer_set_lock_subclass(struct tty_port *port);
bool tty_buffer_restart_work(struct tty_port *port);
bool tty_buffer_cancel_work(struct tty_port *port);
void tty_buffer_flush_work(struct tty_port *port);
speed_t tty_termios_input_baud_rate(struct ktermios *termios);
void tty_ldisc_hangup(struct tty_struct *tty, bool reset);
int tty_ldisc_reinit(struct tty_struct *tty, int disc);
long tty_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long tty_jobctrl_ioctl(struct tty_struct *tty, struct tty_struct *real_tty,
		       struct file *file, unsigned int cmd, unsigned long arg);
void tty_default_fops(struct file_operations *fops);
struct tty_struct *alloc_tty_struct(struct tty_driver *driver, int idx);
int tty_alloc_file(struct file *file);
void tty_add_file(struct tty_struct *tty, struct file *file);
void tty_free_file(struct file *file);
int tty_release(struct inode *inode, struct file *filp);

#define tty_is_writelocked(tty)  (mutex_is_locked(&tty->atomic_write_lock))

int tty_ldisc_setup(struct tty_struct *tty, struct tty_struct *o_tty);
void tty_ldisc_release(struct tty_struct *tty);
int __must_check tty_ldisc_init(struct tty_struct *tty);
void tty_ldisc_deinit(struct tty_struct *tty);

void tty_sysctl_init(void);

/* tty_audit.c */
#ifdef CONFIG_AUDIT
void tty_audit_add_data(struct tty_struct *tty, const void *data, size_t size);
void tty_audit_tiocsti(struct tty_struct *tty, char ch);
#else
static inline void tty_audit_add_data(struct tty_struct *tty, const void *data,
				      size_t size)
{
}
static inline void tty_audit_tiocsti(struct tty_struct *tty, char ch)
{
}
#endif

ssize_t redirected_tty_write(struct kiocb *, struct iov_iter *);

#endif
