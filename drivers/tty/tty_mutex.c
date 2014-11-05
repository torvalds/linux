#include <linux/tty.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/semaphore.h>
#include <linux/sched.h>

/*
 * Nested tty locks are necessary for releasing pty pairs.
 * The stable lock order is master pty first, then slave pty.
 */

/* Legacy tty mutex glue */

enum {
	TTY_MUTEX_NORMAL,
	TTY_MUTEX_NESTED,
};

/*
 * Getting the big tty mutex.
 */

static void __lockfunc tty_lock_nested(struct tty_struct *tty,
				       unsigned int subclass)
{
	if (tty->magic != TTY_MAGIC) {
		pr_err("L Bad %p\n", tty);
		WARN_ON(1);
		return;
	}
	tty_kref_get(tty);
	mutex_lock_nested(&tty->legacy_mutex, subclass);
}

void __lockfunc tty_lock(struct tty_struct *tty)
{
	return tty_lock_nested(tty, TTY_MUTEX_NORMAL);
}
EXPORT_SYMBOL(tty_lock);

void __lockfunc tty_unlock(struct tty_struct *tty)
{
	if (tty->magic != TTY_MAGIC) {
		pr_err("U Bad %p\n", tty);
		WARN_ON(1);
		return;
	}
	mutex_unlock(&tty->legacy_mutex);
	tty_kref_put(tty);
}
EXPORT_SYMBOL(tty_unlock);

void __lockfunc tty_lock_slave(struct tty_struct *tty)
{
	if (tty && tty != tty->link) {
		WARN_ON(!mutex_is_locked(&tty->link->legacy_mutex) ||
			!tty->driver->type == TTY_DRIVER_TYPE_PTY ||
			!tty->driver->type == PTY_TYPE_SLAVE);
		tty_lock_nested(tty, TTY_MUTEX_NESTED);
	}
}

void __lockfunc tty_unlock_slave(struct tty_struct *tty)
{
	if (tty && tty != tty->link)
		tty_unlock(tty);
}
