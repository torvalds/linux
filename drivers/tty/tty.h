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

int tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout);
void tty_ldisc_unlock(struct tty_struct *tty);

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

#endif
