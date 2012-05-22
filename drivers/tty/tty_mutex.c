#include <linux/tty.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/semaphore.h>
#include <linux/sched.h>

/* Legacy tty mutex glue */

/*
 * Getting the big tty mutex.
 */

void __lockfunc tty_lock(struct tty_struct *tty)
{
	if (tty->magic != TTY_MAGIC) {
		printk(KERN_ERR "L Bad %p\n", tty);
		WARN_ON(1);
		return;
	}
	tty_kref_get(tty);
	mutex_lock(&tty->legacy_mutex);
}
EXPORT_SYMBOL(tty_lock);

void __lockfunc tty_unlock(struct tty_struct *tty)
{
	if (tty->magic != TTY_MAGIC) {
		printk(KERN_ERR "U Bad %p\n", tty);
		WARN_ON(1);
		return;
	}
	mutex_unlock(&tty->legacy_mutex);
	tty_kref_put(tty);
}
EXPORT_SYMBOL(tty_unlock);

/*
 * Getting the big tty mutex for a pair of ttys with lock ordering
 * On a non pty/tty pair tty2 can be NULL which is just fine.
 */
void __lockfunc tty_lock_pair(struct tty_struct *tty,
					struct tty_struct *tty2)
{
	if (tty < tty2) {
		tty_lock(tty);
		tty_lock(tty2);
	} else {
		if (tty2 && tty2 != tty)
			tty_lock(tty2);
		tty_lock(tty);
	}
}
EXPORT_SYMBOL(tty_lock_pair);

void __lockfunc tty_unlock_pair(struct tty_struct *tty,
						struct tty_struct *tty2)
{
	tty_unlock(tty);
	if (tty2 && tty2 != tty)
		tty_unlock(tty2);
}
EXPORT_SYMBOL(tty_unlock_pair);
