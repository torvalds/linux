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

#include <linux/config.h>
#include <linux/console.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kbd_kern.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
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

#include <asm/uaccess.h>

#include "hvc_console.h"

#define HVC_MAJOR	229
#define HVC_MINOR	0

#define TIMEOUT		(10)

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

#ifdef CONFIG_MAGIC_SYSRQ
static int sysrq_pressed;
#endif

struct hvc_struct {
	spinlock_t lock;
	int index;
	struct tty_struct *tty;
	unsigned int count;
	int do_wakeup;
	char outbuf[N_OUTBUF] __ALIGNED__;
	int n_outbuf;
	uint32_t vtermno;
	struct hv_ops *ops;
	int irq_requested;
	int irq;
	struct list_head next;
	struct kobject kobj; /* ref count & hvc_struct lifetime */
};

/* dynamic list of hvc_struct instances */
static struct list_head hvc_structs = LIST_HEAD_INIT(hvc_structs);

/*
 * Protect the list of hvc_struct instances from inserts and removals during
 * list traversal.
 */
static DEFINE_SPINLOCK(hvc_structs_lock);

/*
 * This value is used to assign a tty->index value to a hvc_struct based
 * upon order of exposure via hvc_probe(), when we can not match it to
 * a console canidate registered with hvc_instantiate().
 */
static int last_hvc = -1;

/*
 * Do not call this function with either the hvc_strucst_lock or the hvc_struct
 * lock held.  If successful, this function increments the kobject reference
 * count against the target hvc_struct so it should be released when finished.
 */
struct hvc_struct *hvc_get_by_index(int index)
{
	struct hvc_struct *hp;
	unsigned long flags;

	spin_lock(&hvc_structs_lock);

	list_for_each_entry(hp, &hvc_structs, next) {
		spin_lock_irqsave(&hp->lock, flags);
		if (hp->index == index) {
			kobject_get(&hp->kobj);
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
static struct hv_ops *cons_ops[MAX_NR_HVC_CONSOLES];
static uint32_t vtermnos[MAX_NR_HVC_CONSOLES] =
	{[0 ... MAX_NR_HVC_CONSOLES - 1] = -1};

/*
 * Console APIs, NOT TTY.  These APIs are available immediately when
 * hvc_console_setup() finds adapters.
 */

void hvc_console_print(struct console *co, const char *b, unsigned count)
{
	char c[N_OUTBUF] __ALIGNED__;
	unsigned i = 0, n = 0;
	int r, donecr = 0, index = co->index;

	/* Console access attempt outside of acceptable console range. */
	if (index >= MAX_NR_HVC_CONSOLES)
		return;

	/* This console adapter was removed so it is not useable. */
	if (vtermnos[index] < 0)
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
			if (r < 0) {
				/* throw away chars on error */
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

struct console hvc_con_driver = {
	.name		= "hvc",
	.write		= hvc_console_print,
	.device		= hvc_console_device,
	.setup		= hvc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

/*
 * Early console initialization.  Preceeds driver initialization.
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
	register_console(&hvc_con_driver);
	return 0;
}
console_initcall(hvc_console_init);

/*
 * hvc_instantiate() is an early console discovery method which locates
 * consoles * prior to the vio subsystem discovering them.  Hotplugged
 * vty adapters do NOT get an hvc_instantiate() callback since they
 * appear after early console init.
 */
int hvc_instantiate(uint32_t vtermno, int index, struct hv_ops *ops)
{
	struct hvc_struct *hp;

	if (index < 0 || index >= MAX_NR_HVC_CONSOLES)
		return -1;

	if (vtermnos[index] != -1)
		return -1;

	/* make sure no no tty has been registerd in this index */
	hp = hvc_get_by_index(index);
	if (hp) {
		kobject_put(&hp->kobj);
		return -1;
	}

	vtermnos[index] = vtermno;
	cons_ops[index] = ops;

	/* reserve all indices upto and including this index */
	if (last_hvc < index)
		last_hvc = index;

	/* if this index is what the user requested, then register
	 * now (setup won't fail at this point).  It's ok to just
	 * call register again if previously .setup failed.
	 */
	if (index == hvc_con_driver.index)
		register_console(&hvc_con_driver);

	return 0;
}
EXPORT_SYMBOL(hvc_instantiate);

/* Wake the sleeping khvcd */
static void hvc_kick(void)
{
	hvc_kicked = 1;
	wake_up_process(hvc_task);
}

static int hvc_poll(struct hvc_struct *hp);

/*
 * NOTE: This API isn't used if the console adapter doesn't support interrupts.
 * In this case the console is poll driven.
 */
static irqreturn_t hvc_handle_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	/* if hvc_poll request a repoll, then kick the hvcd thread */
	if (hvc_poll(dev_instance))
		hvc_kick();
	return IRQ_HANDLED;
}

static void hvc_unthrottle(struct tty_struct *tty)
{
	hvc_kick();
}

/*
 * The TTY interface won't be used until after the vio layer has exposed the vty
 * adapter to the kernel.
 */
static int hvc_open(struct tty_struct *tty, struct file * filp)
{
	struct hvc_struct *hp;
	unsigned long flags;
	int irq = NO_IRQ;
	int rc = 0;
	struct kobject *kobjp;

	/* Auto increments kobject reference if found. */
	if (!(hp = hvc_get_by_index(tty->index))) {
		printk(KERN_WARNING "hvc_console: tty open failed, no vty associated with tty.\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&hp->lock, flags);
	/* Check and then increment for fast path open. */
	if (hp->count++ > 0) {
		spin_unlock_irqrestore(&hp->lock, flags);
		hvc_kick();
		return 0;
	} /* else count == 0 */

	tty->driver_data = hp;
	tty->low_latency = 1; /* Makes flushes to ldisc synchronous. */

	hp->tty = tty;
	/* Save for request_irq outside of spin_lock. */
	irq = hp->irq;
	if (irq != NO_IRQ)
		hp->irq_requested = 1;

	kobjp = &hp->kobj;

	spin_unlock_irqrestore(&hp->lock, flags);
	/* check error, fallback to non-irq */
	if (irq != NO_IRQ)
		rc = request_irq(irq, hvc_handle_interrupt, SA_INTERRUPT, "hvc_console", hp);

	/*
	 * If the request_irq() fails and we return an error.  The tty layer
	 * will call hvc_close() after a failed open but we don't want to clean
	 * up there so we'll clean up here and clear out the previously set
	 * tty fields and return the kobject reference.
	 */
	if (rc) {
		spin_lock_irqsave(&hp->lock, flags);
		hp->tty = NULL;
		hp->irq_requested = 0;
		spin_unlock_irqrestore(&hp->lock, flags);
		tty->driver_data = NULL;
		kobject_put(kobjp);
		printk(KERN_ERR "hvc_open: request_irq failed with rc %d.\n", rc);
	}
	/* Force wakeup of the polling thread */
	hvc_kick();

	return rc;
}

static void hvc_close(struct tty_struct *tty, struct file * filp)
{
	struct hvc_struct *hp;
	struct kobject *kobjp;
	int irq = NO_IRQ;
	unsigned long flags;

	if (tty_hung_up_p(filp))
		return;

	/*
	 * No driver_data means that this close was issued after a failed
	 * hvc_open by the tty layer's release_dev() function and we can just
	 * exit cleanly because the kobject reference wasn't made.
	 */
	if (!tty->driver_data)
		return;

	hp = tty->driver_data;
	spin_lock_irqsave(&hp->lock, flags);

	kobjp = &hp->kobj;
	if (--hp->count == 0) {
		if (hp->irq_requested)
			irq = hp->irq;
		hp->irq_requested = 0;

		/* We are done with the tty pointer now. */
		hp->tty = NULL;
		spin_unlock_irqrestore(&hp->lock, flags);

		/*
		 * Chain calls chars_in_buffer() and returns immediately if
		 * there is no buffered data otherwise sleeps on a wait queue
		 * waking periodically to check chars_in_buffer().
		 */
		tty_wait_until_sent(tty, HVC_CLOSE_WAIT);

		if (irq != NO_IRQ)
			free_irq(irq, hp);

	} else {
		if (hp->count < 0)
			printk(KERN_ERR "hvc_close %X: oops, count is %d\n",
				hp->vtermno, hp->count);
		spin_unlock_irqrestore(&hp->lock, flags);
	}

	kobject_put(kobjp);
}

static void hvc_hangup(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int irq = NO_IRQ;
	int temp_open_count;
	struct kobject *kobjp;

	if (!hp)
		return;

	spin_lock_irqsave(&hp->lock, flags);

	/*
	 * The N_TTY line discipline has problems such that in a close vs
	 * open->hangup case this can be called after the final close so prevent
	 * that from happening for now.
	 */
	if (hp->count <= 0) {
		spin_unlock_irqrestore(&hp->lock, flags);
		return;
	}

	kobjp = &hp->kobj;
	temp_open_count = hp->count;
	hp->count = 0;
	hp->n_outbuf = 0;
	hp->tty = NULL;
	if (hp->irq_requested)
		/* Saved for use outside of spin_lock. */
		irq = hp->irq;
	hp->irq_requested = 0;
	spin_unlock_irqrestore(&hp->lock, flags);
	if (irq != NO_IRQ)
		free_irq(irq, hp);
	while(temp_open_count) {
		--temp_open_count;
		kobject_put(kobjp);
	}
}

/*
 * Push buffered characters whether they were just recently buffered or waiting
 * on a blocked hypervisor.  Call this function with hp->lock held.
 */
static void hvc_push(struct hvc_struct *hp)
{
	int n;

	n = hp->ops->put_chars(hp->vtermno, hp->outbuf, hp->n_outbuf);
	if (n <= 0) {
		if (n == 0) {
			hp->do_wakeup = 1;
			return;
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
}

static int hvc_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int rsize, written = 0;

	/* This write was probably executed during a tty close. */
	if (!hp)
		return -EPIPE;

	if (hp->count <= 0)
		return -EIO;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		hvc_push(hp);

	while (count > 0 && (rsize = N_OUTBUF - hp->n_outbuf) > 0) {
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

/*
 * This is actually a contract between the driver and the tty layer outlining
 * how much write room the driver can guarentee will be sent OR BUFFERED.  This
 * driver MUST honor the return value.
 */
static int hvc_write_room(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp)
		return -1;

	return N_OUTBUF - hp->n_outbuf;
}

static int hvc_chars_in_buffer(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	if (!hp)
		return -1;
	return hp->n_outbuf;
}

#define HVC_POLL_READ	0x00000001
#define HVC_POLL_WRITE	0x00000002
#define HVC_POLL_QUICK	0x00000004

static int hvc_poll(struct hvc_struct *hp)
{
	struct tty_struct *tty;
	int i, n, poll_mask = 0;
	char buf[N_INBUF] __ALIGNED__;
	unsigned long flags;
	int read_total = 0;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		hvc_push(hp);
	/* Reschedule us if still some write pending */
	if (hp->n_outbuf > 0)
		poll_mask |= HVC_POLL_WRITE;

	/* No tty attached, just skip */
	tty = hp->tty;
	if (tty == NULL)
		goto bail;

	/* Now check if we can get data (are we throttled ?) */
	if (test_bit(TTY_THROTTLED, &tty->flags))
		goto throttled;

	/* If we aren't interrupt driven and aren't throttled, we always
	 * request a reschedule
	 */
	if (hp->irq == NO_IRQ)
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
			if (hp->index == hvc_con_driver.index) {
				/* Handle the SysRq Hack */
				/* XXX should support a sequence */
				if (buf[i] == '\x0f') {	/* ^O */
					sysrq_pressed = 1;
					continue;
				} else if (sysrq_pressed) {
					handle_sysrq(buf[i], NULL, tty);
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

	if (read_total)
		tty_flip_buffer_push(tty);
	
	return poll_mask;
}

#if defined(CONFIG_XMON) && defined(CONFIG_SMP)
extern cpumask_t cpus_in_xmon;
#else
static const cpumask_t cpus_in_xmon = CPU_MASK_NONE;
#endif

/*
 * This kthread is either polling or interrupt driven.  This is determined by
 * calling hvc_poll() who determines whether a console adapter support
 * interrupts.
 */
int khvcd(void *unused)
{
	int poll_mask;
	struct hvc_struct *hp;

	__set_current_state(TASK_RUNNING);
	do {
		poll_mask = 0;
		hvc_kicked = 0;
		wmb();
		if (cpus_empty(cpus_in_xmon)) {
			spin_lock(&hvc_structs_lock);
			list_for_each_entry(hp, &hvc_structs, next) {
				poll_mask |= hvc_poll(hp);
			}
			spin_unlock(&hvc_structs_lock);
		} else
			poll_mask |= HVC_POLL_READ;
		if (hvc_kicked)
			continue;
		if (poll_mask & HVC_POLL_QUICK) {
			yield();
			continue;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		if (!hvc_kicked) {
			if (poll_mask == 0)
				schedule();
			else
				msleep_interruptible(TIMEOUT);
		}
		__set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	return 0;
}

static struct tty_operations hvc_ops = {
	.open = hvc_open,
	.close = hvc_close,
	.write = hvc_write,
	.hangup = hvc_hangup,
	.unthrottle = hvc_unthrottle,
	.write_room = hvc_write_room,
	.chars_in_buffer = hvc_chars_in_buffer,
};

/* callback when the kboject ref count reaches zero. */
static void destroy_hvc_struct(struct kobject *kobj)
{
	struct hvc_struct *hp = container_of(kobj, struct hvc_struct, kobj);
	unsigned long flags;

	spin_lock(&hvc_structs_lock);

	spin_lock_irqsave(&hp->lock, flags);
	list_del(&(hp->next));
	spin_unlock_irqrestore(&hp->lock, flags);

	spin_unlock(&hvc_structs_lock);

	kfree(hp);
}

static struct kobj_type hvc_kobj_type = {
	.release = destroy_hvc_struct,
};

struct hvc_struct __devinit *hvc_alloc(uint32_t vtermno, int irq,
					struct hv_ops *ops)
{
	struct hvc_struct *hp;
	int i;

	hp = kmalloc(sizeof(*hp), GFP_KERNEL);
	if (!hp)
		return ERR_PTR(-ENOMEM);

	memset(hp, 0x00, sizeof(*hp));

	hp->vtermno = vtermno;
	hp->irq = irq;
	hp->ops = ops;

	kobject_init(&hp->kobj);
	hp->kobj.ktype = &hvc_kobj_type;

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
EXPORT_SYMBOL(hvc_alloc);

int __devexit hvc_remove(struct hvc_struct *hp)
{
	unsigned long flags;
	struct kobject *kobjp;
	struct tty_struct *tty;

	spin_lock_irqsave(&hp->lock, flags);
	tty = hp->tty;
	kobjp = &hp->kobj;

	if (hp->index < MAX_NR_HVC_CONSOLES)
		vtermnos[hp->index] = -1;

	/* Don't whack hp->irq because tty_hangup() will need to free the irq. */

	spin_unlock_irqrestore(&hp->lock, flags);

	/*
	 * We 'put' the instance that was grabbed when the kobject instance
	 * was intialized using kobject_init().  Let the last holder of this
	 * kobject cause it to be removed, which will probably be the tty_hangup
	 * below.
	 */
	kobject_put(kobjp);

	/*
	 * This function call will auto chain call hvc_hangup.  The tty should
	 * always be valid at this time unless a simultaneous tty close already
	 * cleaned up the hvc_struct.
	 */
	if (tty)
		tty_hangup(tty);
	return 0;
}
EXPORT_SYMBOL(hvc_remove);

/* Driver initialization.  Follow console initialization.  This is where the TTY
 * interfaces start to become available. */
int __init hvc_init(void)
{
	struct tty_driver *drv;

	/* We need more than hvc_count adapters due to hotplug additions. */
	drv = alloc_tty_driver(HVC_ALLOC_TTY_ADAPTERS);
	if (!drv)
		return -ENOMEM;

	drv->owner = THIS_MODULE;
	drv->devfs_name = "hvc/";
	drv->driver_name = "hvc";
	drv->name = "hvc";
	drv->major = HVC_MAJOR;
	drv->minor_start = HVC_MINOR;
	drv->type = TTY_DRIVER_TYPE_SYSTEM;
	drv->init_termios = tty_std_termios;
	drv->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(drv, &hvc_ops);

	/* Always start the kthread because there can be hotplug vty adapters
	 * added later. */
	hvc_task = kthread_run(khvcd, NULL, "khvcd");
	if (IS_ERR(hvc_task)) {
		panic("Couldn't create kthread for console.\n");
		put_tty_driver(drv);
		return -EIO;
	}

	if (tty_register_driver(drv))
		panic("Couldn't register hvc console driver\n");

	mb();
	hvc_driver = drv;
	return 0;
}
module_init(hvc_init);

/* This isn't particularily necessary due to this being a console driver
 * but it is nice to be thorough.
 */
static void __exit hvc_exit(void)
{
	kthread_stop(hvc_task);

	tty_unregister_driver(hvc_driver);
	/* return tty_struct instances allocated in hvc_init(). */
	put_tty_driver(hvc_driver);
	unregister_console(&hvc_con_driver);
}
module_exit(hvc_exit);
