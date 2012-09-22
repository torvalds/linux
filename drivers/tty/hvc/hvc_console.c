/*
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2004 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 * Copyright (C) 2004 IBM Corporation
 *
 * Additional Author(s):
 *  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/console.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kbd_kern.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/major.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/serial_core.h>

#include <asm/uaccess.h>

#include "hvc_console.h"

#define HVC_MAJOR	229
#define HVC_MINOR	0

/*
 * Wait this long per iteration while trying to push buffered data to the
 * hypervisor before allowing the tty to complete a close operation.
 */
#define HVC_CLOSE_WAIT (HZ/100) /* 1/10 of a second */

/*
 * These sizes are most efficient for vio, because they are the
 * native transfer size. We could make them selectable in the
 * future to better deal with backends that want other buffer sizes.
 */
#define N_OUTBUF	16
#define N_INBUF		16

#define __ALIGNED__ __attribute__((__aligned__(sizeof(long))))

static struct tty_driver *hvc_driver;
static struct task_struct *hvc_task;

/* Picks up late kicks after list walk but before schedule() */
static int hvc_kicked;

static int hvc_init(void);

#ifdef CONFIG_MAGIC_SYSRQ
static int sysrq_pressed;
#endif

/* dynamic list of hvc_struct instances */
static LIST_HEAD(hvc_structs);

/*
 * Protect the list of hvc_struct instances from inserts and removals during
 * list traversal.
 */
static DEFINE_SPINLOCK(hvc_structs_lock);

/*
 * This value is used to assign a tty->index value to a hvc_struct based
 * upon order of exposure via hvc_probe(), when we can not match it to
 * a console candidate registered with hvc_instantiate().
 */
static int last_hvc = -1;

/*
 * Do not call this function with either the hvc_structs_lock or the hvc_struct
 * lock held.  If successful, this function increments the kref reference
 * count against the target hvc_struct so it should be released when finished.
 */
static struct hvc_struct *hvc_get_by_index(int index)
{
	struct hvc_struct *hp;
	unsigned long flags;

	spin_lock(&hvc_structs_lock);

	list_for_each_entry(hp, &hvc_structs, next) {
		spin_lock_irqsave(&hp->lock, flags);
		if (hp->index == index) {
			tty_port_get(&hp->port);
			spin_unlock_irqrestore(&hp->lock, flags);
			spin_unlock(&hvc_structs_lock);
			return hp;
		}
		spin_unlock_irqrestore(&hp->lock, flags);
	}
	hp = NULL;

	spin_unlock(&hvc_structs_lock);
	return hp;
}


/*
 * Initial console vtermnos for console API usage prior to full console
 * initialization.  Any vty adapter outside this range will not have usable
 * console interfaces but can still be used as a tty device.  This has to be
 * static because kmalloc will not work during early console init.
 */
static const struct hv_ops *cons_ops[MAX_NR_HVC_CONSOLES];
static uint32_t vtermnos[MAX_NR_HVC_CONSOLES] =
	{[0 ... MAX_NR_HVC_CONSOLES - 1] = -1};

/*
 * Console APIs, NOT TTY.  These APIs are available immediately when
 * hvc_console_setup() finds adapters.
 */

static void hvc_console_print(struct console *co, const char *b,
			      unsigned count)
{
	char c[N_OUTBUF] __ALIGNED__;
	unsigned i = 0, n = 0;
	int r, donecr = 0, index = co->index;

	/* Console access attempt outside of acceptable console range. */
	if (index >= MAX_NR_HVC_CONSOLES)
		return;

	/* This console adapter was removed so it is not usable. */
	if (vtermnos[index] == -1)
		return;

	while (count > 0 || i > 0) {
		if (count > 0 && i < sizeof(c)) {
			if (b[n] == '\n' && !donecr) {
				c[i++] = '\r';
				donecr = 1;
			} else {
				c[i++] = b[n++];
				donecr = 0;
				--count;
			}
		} else {
			r = cons_ops[index]->put_chars(vtermnos[index], c, i);
			if (r <= 0) {
				/* throw away characters on error
				 * but spin in case of -EAGAIN */
				if (r != -EAGAIN)
					i = 0;
			} else if (r > 0) {
				i -= r;
				if (i > 0)
					memmove(c, c+r, i);
			}
		}
	}
}

static struct tty_driver *hvc_console_device(struct console *c, int *index)
{
	if (vtermnos[c->index] == -1)
		return NULL;

	*index = c->index;
	return hvc_driver;
}

static int __init hvc_console_setup(struct console *co, char *options)
{	
	if (co->index < 0 || co->index >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;

	if (vtermnos[co->index] == -1)
		return -ENODEV;

	return 0;
}

static struct console hvc_console = {
	.name		= "hvc",
	.write		= hvc_console_print,
	.device		= hvc_console_device,
	.setup		= hvc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

/*
 * Early console initialization.  Precedes driver initialization.
 *
 * (1) we are first, and the user specified another driver
 * -- index will remain -1
 * (2) we are first and the user specified no driver
 * -- index will be set to 0, then we will fail setup.
 * (3)  we are first and the user specified our driver
 * -- index will be set to user specified driver, and we will fail
 * (4) we are after driver, and this initcall will register us
 * -- if the user didn't specify a driver then the console will match
 *
 * Note that for cases 2 and 3, we will match later when the io driver
 * calls hvc_instantiate() and call register again.
 */
static int __init hvc_console_init(void)
{
	register_console(&hvc_console);
	return 0;
}
console_initcall(hvc_console_init);

/* callback when the kboject ref count reaches zero. */
static void hvc_port_destruct(struct tty_port *port)
{
	struct hvc_struct *hp = container_of(port, struct hvc_struct, port);
	unsigned long flags;

	spin_lock(&hvc_structs_lock);

	spin_lock_irqsave(&hp->lock, flags);
	list_del(&(hp->next));
	spin_unlock_irqrestore(&hp->lock, flags);

	spin_unlock(&hvc_structs_lock);

	kfree(hp);
}

/*
 * hvc_instantiate() is an early console discovery method which locates
 * consoles * prior to the vio subsystem discovering them.  Hotplugged
 * vty adapters do NOT get an hvc_instantiate() callback since they
 * appear after early console init.
 */
int hvc_instantiate(uint32_t vtermno, int index, const struct hv_ops *ops)
{
	struct hvc_struct *hp;

	if (index < 0 || index >= MAX_NR_HVC_CONSOLES)
		return -1;

	if (vtermnos[index] != -1)
		return -1;

	/* make sure no no tty has been registered in this index */
	hp = hvc_get_by_index(index);
	if (hp) {
		tty_port_put(&hp->port);
		return -1;
	}

	vtermnos[index] = vtermno;
	cons_ops[index] = ops;

	/* reserve all indices up to and including this index */
	if (last_hvc < index)
		last_hvc = index;

	/* if this index is what the user requested, then register
	 * now (setup won't fail at this point).  It's ok to just
	 * call register again if previously .setup failed.
	 */
	if (index == hvc_console.index)
		register_console(&hvc_console);

	return 0;
}
EXPORT_SYMBOL_GPL(hvc_instantiate);

/* Wake the sleeping khvcd */
void hvc_kick(void)
{
	hvc_kicked = 1;
	wake_up_process(hvc_task);
}
EXPORT_SYMBOL_GPL(hvc_kick);

static void hvc_unthrottle(struct tty_struct *tty)
{
	hvc_kick();
}

static int hvc_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct hvc_struct *hp;
	int rc;

	/* Auto increments kref reference if found. */
	if (!(hp = hvc_get_by_index(tty->index)))
		return -ENODEV;

	tty->driver_data = hp;

	rc = tty_port_install(&hp->port, driver, tty);
	if (rc)
		tty_port_put(&hp->port);
	return rc;
}

/*
 * The TTY interface won't be used until after the vio layer has exposed the vty
 * adapter to the kernel.
 */
static int hvc_open(struct tty_struct *tty, struct file * filp)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&hp->port.lock, flags);
	/* Check and then increment for fast path open. */
	if (hp->port.count++ > 0) {
		spin_unlock_irqrestore(&hp->port.lock, flags);
		hvc_kick();
		return 0;
	} /* else count == 0 */
	spin_unlock_irqrestore(&hp->port.lock, flags);

	tty_port_tty_set(&hp->port, tty);

	if (hp->ops->notifier_add)
		rc = hp->ops->notifier_add(hp, hp->data);

	/*
	 * If the notifier fails we return an error.  The tty layer
	 * will call hvc_close() after a failed open but we don't want to clean
	 * up there so we'll clean up here and clear out the previously set
	 * tty fields and return the kref reference.
	 */
	if (rc) {
		tty_port_tty_set(&hp->port, NULL);
		tty->driver_data = NULL;
		tty_port_put(&hp->port);
		printk(KERN_ERR "hvc_open: request_irq failed with rc %d.\n", rc);
	}
	/* Force wakeup of the polling thread */
	hvc_kick();

	return rc;
}

static void hvc_close(struct tty_struct *tty, struct file * filp)
{
	struct hvc_struct *hp;
	unsigned long flags;

	if (tty_hung_up_p(filp))
		return;

	/*
	 * No driver_data means that this close was issued after a failed
	 * hvc_open by the tty layer's release_dev() function and we can just
	 * exit cleanly because the kref reference wasn't made.
	 */
	if (!tty->driver_data)
		return;

	hp = tty->driver_data;

	spin_lock_irqsave(&hp->port.lock, flags);

	if (--hp->port.count == 0) {
		spin_unlock_irqrestore(&hp->port.lock, flags);
		/* We are done with the tty pointer now. */
		tty_port_tty_set(&hp->port, NULL);

		if (hp->ops->notifier_del)
			hp->ops->notifier_del(hp, hp->data);

		/* cancel pending tty resize work */
		cancel_work_sync(&hp->tty_resize);

		/*
		 * Chain calls chars_in_buffer() and returns immediately if
		 * there is no buffered data otherwise sleeps on a wait queue
		 * waking periodically to check chars_in_buffer().
		 */
		tty_wait_until_sent_from_close(tty, HVC_CLOSE_WAIT);
	} else {
		if (hp->port.count < 0)
			printk(KERN_ERR "hvc_close %X: oops, count is %d\n",
				hp->vtermno, hp->port.count);
		spin_unlock_irqrestore(&hp->port.lock, flags);
	}
}

static void hvc_cleanup(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	tty_port_put(&hp->port);
}

static void hvc_hangup(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int temp_open_count;

	if (!hp)
		return;

	/* cancel pending tty resize work */
	cancel_work_sync(&hp->tty_resize);

	spin_lock_irqsave(&hp->port.lock, flags);

	/*
	 * The N_TTY line discipline has problems such that in a close vs
	 * open->hangup case this can be called after the final close so prevent
	 * that from happening for now.
	 */
	if (hp->port.count <= 0) {
		spin_unlock_irqrestore(&hp->port.lock, flags);
		return;
	}

	temp_open_count = hp->port.count;
	hp->port.count = 0;
	spin_unlock_irqrestore(&hp->port.lock, flags);
	tty_port_tty_set(&hp->port, NULL);

	hp->n_outbuf = 0;

	if (hp->ops->notifier_hangup)
		hp->ops->notifier_hangup(hp, hp->data);

	while(temp_open_count) {
		--temp_open_count;
		tty_port_put(&hp->port);
	}
}

/*
 * Push buffered characters whether they were just recently buffered or waiting
 * on a blocked hypervisor.  Call this function with hp->lock held.
 */
static int hvc_push(struct hvc_struct *hp)
{
	int n;

	n = hp->ops->put_chars(hp->vtermno, hp->outbuf, hp->n_outbuf);
	if (n <= 0) {
		if (n == 0 || n == -EAGAIN) {
			hp->do_wakeup = 1;
			return 0;
		}
		/* throw away output on error; this happens when
		   there is no session connected to the vterm. */
		hp->n_outbuf = 0;
	} else
		hp->n_outbuf -= n;
	if (hp->n_outbuf > 0)
		memmove(hp->outbuf, hp->outbuf + n, hp->n_outbuf);
	else
		hp->do_wakeup = 1;

	return n;
}

static int hvc_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int rsize, written = 0;

	/* This write was probably executed during a tty close. */
	if (!hp)
		return -EPIPE;

	/* FIXME what's this (unprotected) check for? */
	if (hp->port.count <= 0)
		return -EIO;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		hvc_push(hp);

	while (count > 0 && (rsize = hp->outbuf_size - hp->n_outbuf) > 0) {
		if (rsize > count)
			rsize = count;
		memcpy(hp->outbuf + hp->n_outbuf, buf, rsize);
		count -= rsize;
		buf += rsize;
		hp->n_outbuf += rsize;
		written += rsize;
		hvc_push(hp);
	}
	spin_unlock_irqrestore(&hp->lock, flags);

	/*
	 * Racy, but harmless, kick thread if there is still pending data.
	 */
	if (hp->n_outbuf)
		hvc_kick();

	return written;
}

/**
 * hvc_set_winsz() - Resize the hvc tty terminal window.
 * @work:	work structure.
 *
 * The routine shall not be called within an atomic context because it
 * might sleep.
 *
 * Locking:	hp->lock
 */
static void hvc_set_winsz(struct work_struct *work)
{
	struct hvc_struct *hp;
	unsigned long hvc_flags;
	struct tty_struct *tty;
	struct winsize ws;

	hp = container_of(work, struct hvc_struct, tty_resize);

	tty = tty_port_tty_get(&hp->port);
	if (!tty)
		return;

	spin_lock_irqsave(&hp->lock, hvc_flags);
	ws = hp->ws;
	spin_unlock_irqrestore(&hp->lock, hvc_flags);

	tty_do_resize(tty, &ws);
	tty_kref_put(tty);
}

/*
 * This is actually a contract between the driver and the tty layer outlining
 * how much write room the driver can guarantee will be sent OR BUFFERED.  This
 * driver MUST honor the return value.
 */
static int hvc_write_room(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp)
		return -1;

	return hp->outbuf_size - hp->n_outbuf;
}

static int hvc_chars_in_buffer(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp)
		return 0;
	return hp->n_outbuf;
}

/*
 * timeout will vary between the MIN and MAX values defined here.  By default
 * and during console activity we will use a default MIN_TIMEOUT of 10.  When
 * the console is idle, we increase the timeout value on each pass through
 * msleep until we reach the max.  This may be noticeable as a brief (average
 * one second) delay on the console before the console responds to input when
 * there has been no input for some time.
 */
#define MIN_TIMEOUT		(10)
#define MAX_TIMEOUT		(2000)
static u32 timeout = MIN_TIMEOUT;

#define HVC_POLL_READ	0x00000001
#define HVC_POLL_WRITE	0x00000002

int hvc_poll(struct hvc_struct *hp)
{
	struct tty_struct *tty;
	int i, n, poll_mask = 0;
	char buf[N_INBUF] __ALIGNED__;
	unsigned long flags;
	int read_total = 0;
	int written_total = 0;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		written_total = hvc_push(hp);

	/* Reschedule us if still some write pending */
	if (hp->n_outbuf > 0) {
		poll_mask |= HVC_POLL_WRITE;
		/* If hvc_push() was not able to write, sleep a few msecs */
		timeout = (written_total) ? 0 : MIN_TIMEOUT;
	}

	/* No tty attached, just skip */
	tty = tty_port_tty_get(&hp->port);
	if (tty == NULL)
		goto bail;

	/* Now check if we can get data (are we throttled ?) */
	if (test_bit(TTY_THROTTLED, &tty->flags))
		goto throttled;

	/* If we aren't notifier driven and aren't throttled, we always
	 * request a reschedule
	 */
	if (!hp->irq_requested)
		poll_mask |= HVC_POLL_READ;

	/* Read data if any */
	for (;;) {
		int count = tty_buffer_request_room(tty, N_INBUF);

		/* If flip is full, just reschedule a later read */
		if (count == 0) {
			poll_mask |= HVC_POLL_READ;
			break;
		}

		n = hp->ops->get_chars(hp->vtermno, buf, count);
		if (n <= 0) {
			/* Hangup the tty when disconnected from host */
			if (n == -EPIPE) {
				spin_unlock_irqrestore(&hp->lock, flags);
				tty_hangup(tty);
				spin_lock_irqsave(&hp->lock, flags);
			} else if ( n == -EAGAIN ) {
				/*
				 * Some back-ends can only ensure a certain min
				 * num of bytes read, which may be > 'count'.
				 * Let the tty clear the flip buff to make room.
				 */
				poll_mask |= HVC_POLL_READ;
			}
			break;
		}
		for (i = 0; i < n; ++i) {
#ifdef CONFIG_MAGIC_SYSRQ
			if (hp->index == hvc_console.index) {
				/* Handle the SysRq Hack */
				/* XXX should support a sequence */
				if (buf[i] == '\x0f') {	/* ^O */
					/* if ^O is pressed again, reset
					 * sysrq_pressed and flip ^O char */
					sysrq_pressed = !sysrq_pressed;
					if (sysrq_pressed)
						continue;
				} else if (sysrq_pressed) {
					handle_sysrq(buf[i]);
					sysrq_pressed = 0;
					continue;
				}
			}
#endif /* CONFIG_MAGIC_SYSRQ */
			tty_insert_flip_char(tty, buf[i], 0);
		}

		read_total += n;
	}
 throttled:
	/* Wakeup write queue if necessary */
	if (hp->do_wakeup) {
		hp->do_wakeup = 0;
		tty_wakeup(tty);
	}
 bail:
	spin_unlock_irqrestore(&hp->lock, flags);

	if (read_total) {
		/* Activity is occurring, so reset the polling backoff value to
		   a minimum for performance. */
		timeout = MIN_TIMEOUT;

		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);

	return poll_mask;
}
EXPORT_SYMBOL_GPL(hvc_poll);

/**
 * __hvc_resize() - Update terminal window size information.
 * @hp:		HVC console pointer
 * @ws:		Terminal window size structure
 *
 * Stores the specified window size information in the hvc structure of @hp.
 * The function schedule the tty resize update.
 *
 * Locking:	Locking free; the function MUST be called holding hp->lock
 */
void __hvc_resize(struct hvc_struct *hp, struct winsize ws)
{
	hp->ws = ws;
	schedule_work(&hp->tty_resize);
}
EXPORT_SYMBOL_GPL(__hvc_resize);

/*
 * This kthread is either polling or interrupt driven.  This is determined by
 * calling hvc_poll() who determines whether a console adapter support
 * interrupts.
 */
static int khvcd(void *unused)
{
	int poll_mask;
	struct hvc_struct *hp;

	set_freezable();
	do {
		poll_mask = 0;
		hvc_kicked = 0;
		try_to_freeze();
		wmb();
		if (!cpus_are_in_xmon()) {
			spin_lock(&hvc_structs_lock);
			list_for_each_entry(hp, &hvc_structs, next) {
				poll_mask |= hvc_poll(hp);
			}
			spin_unlock(&hvc_structs_lock);
		} else
			poll_mask |= HVC_POLL_READ;
		if (hvc_kicked)
			continue;
		set_current_state(TASK_INTERRUPTIBLE);
		if (!hvc_kicked) {
			if (poll_mask == 0)
				schedule();
			else {
				if (timeout < MAX_TIMEOUT)
					timeout += (timeout >> 6) + 1;

				msleep_interruptible(timeout);
			}
		}
		__set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	return 0;
}

static int hvc_tiocmget(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp || !hp->ops->tiocmget)
		return -EINVAL;
	return hp->ops->tiocmget(hp);
}

static int hvc_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp || !hp->ops->tiocmset)
		return -EINVAL;
	return hp->ops->tiocmset(hp, set, clear);
}

#ifdef CONFIG_CONSOLE_POLL
int hvc_poll_init(struct tty_driver *driver, int line, char *options)
{
	return 0;
}

static int hvc_poll_get_char(struct tty_driver *driver, int line)
{
	struct tty_struct *tty = driver->ttys[0];
	struct hvc_struct *hp = tty->driver_data;
	int n;
	char ch;

	n = hp->ops->get_chars(hp->vtermno, &ch, 1);

	if (n == 0)
		return NO_POLL_CHAR;

	return ch;
}

static void hvc_poll_put_char(struct tty_driver *driver, int line, char ch)
{
	struct tty_struct *tty = driver->ttys[0];
	struct hvc_struct *hp = tty->driver_data;
	int n;

	do {
		n = hp->ops->put_chars(hp->vtermno, &ch, 1);
	} while (n <= 0);
}
#endif

static const struct tty_operations hvc_ops = {
	.install = hvc_install,
	.open = hvc_open,
	.close = hvc_close,
	.cleanup = hvc_cleanup,
	.write = hvc_write,
	.hangup = hvc_hangup,
	.unthrottle = hvc_unthrottle,
	.write_room = hvc_write_room,
	.chars_in_buffer = hvc_chars_in_buffer,
	.tiocmget = hvc_tiocmget,
	.tiocmset = hvc_tiocmset,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init = hvc_poll_init,
	.poll_get_char = hvc_poll_get_char,
	.poll_put_char = hvc_poll_put_char,
#endif
};

static const struct tty_port_operations hvc_port_ops = {
	.destruct = hvc_port_destruct,
};

struct hvc_struct *hvc_alloc(uint32_t vtermno, int data,
			     const struct hv_ops *ops,
			     int outbuf_size)
{
	struct hvc_struct *hp;
	int i;

	/* We wait until a driver actually comes along */
	if (!hvc_driver) {
		int err = hvc_init();
		if (err)
			return ERR_PTR(err);
	}

	hp = kzalloc(ALIGN(sizeof(*hp), sizeof(long)) + outbuf_size,
			GFP_KERNEL);
	if (!hp)
		return ERR_PTR(-ENOMEM);

	hp->vtermno = vtermno;
	hp->data = data;
	hp->ops = ops;
	hp->outbuf_size = outbuf_size;
	hp->outbuf = &((char *)hp)[ALIGN(sizeof(*hp), sizeof(long))];

	tty_port_init(&hp->port);
	hp->port.ops = &hvc_port_ops;

	INIT_WORK(&hp->tty_resize, hvc_set_winsz);
	spin_lock_init(&hp->lock);
	spin_lock(&hvc_structs_lock);

	/*
	 * find index to use:
	 * see if this vterm id matches one registered for console.
	 */
	for (i=0; i < MAX_NR_HVC_CONSOLES; i++)
		if (vtermnos[i] == hp->vtermno &&
		    cons_ops[i] == hp->ops)
			break;

	/* no matching slot, just use a counter */
	if (i >= MAX_NR_HVC_CONSOLES)
		i = ++last_hvc;

	hp->index = i;

	list_add_tail(&(hp->next), &hvc_structs);
	spin_unlock(&hvc_structs_lock);

	return hp;
}
EXPORT_SYMBOL_GPL(hvc_alloc);

int hvc_remove(struct hvc_struct *hp)
{
	unsigned long flags;
	struct tty_struct *tty;

	tty = tty_port_tty_get(&hp->port);

	spin_lock_irqsave(&hp->lock, flags);
	if (hp->index < MAX_NR_HVC_CONSOLES)
		vtermnos[hp->index] = -1;

	/* Don't whack hp->irq because tty_hangup() will need to free the irq. */

	spin_unlock_irqrestore(&hp->lock, flags);

	/*
	 * We 'put' the instance that was grabbed when the kref instance
	 * was initialized using kref_init().  Let the last holder of this
	 * kref cause it to be removed, which will probably be the tty_vhangup
	 * below.
	 */
	tty_port_put(&hp->port);

	/*
	 * This function call will auto chain call hvc_hangup.
	 */
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(hvc_remove);

/* Driver initialization: called as soon as someone uses hvc_alloc(). */
static int hvc_init(void)
{
	struct tty_driver *drv;
	int err;

	/* We need more than hvc_count adapters due to hotplug additions. */
	drv = alloc_tty_driver(HVC_ALLOC_TTY_ADAPTERS);
	if (!drv) {
		err = -ENOMEM;
		goto out;
	}

	drv->driver_name = "hvc";
	drv->name = "hvc";
	drv->major = HVC_MAJOR;
	drv->minor_start = HVC_MINOR;
	drv->type = TTY_DRIVER_TYPE_SYSTEM;
	drv->init_termios = tty_std_termios;
	drv->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(drv, &hvc_ops);

	/* Always start the kthread because there can be hotplug vty adapters
	 * added later. */
	hvc_task = kthread_run(khvcd, NULL, "khvcd");
	if (IS_ERR(hvc_task)) {
		printk(KERN_ERR "Couldn't create kthread for console.\n");
		err = PTR_ERR(hvc_task);
		goto put_tty;
	}

	err = tty_register_driver(drv);
	if (err) {
		printk(KERN_ERR "Couldn't register hvc console driver\n");
		goto stop_thread;
	}

	/*
	 * Make sure tty is fully registered before allowing it to be
	 * found by hvc_console_device.
	 */
	smp_mb();
	hvc_driver = drv;
	return 0;

stop_thread:
	kthread_stop(hvc_task);
	hvc_task = NULL;
put_tty:
	put_tty_driver(drv);
out:
	return err;
}

/* This isn't particularly necessary due to this being a console driver
 * but it is nice to be thorough.
 */
static void __exit hvc_exit(void)
{
	if (hvc_driver) {
		kthread_stop(hvc_task);

		tty_unregister_driver(hvc_driver);
		/* return tty_struct instances allocated in hvc_init(). */
		put_tty_driver(hvc_driver);
		unregister_console(&hvc_console);
	}
}
module_exit(hvc_exit);
