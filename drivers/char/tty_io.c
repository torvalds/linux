/*
 *  linux/drivers/char/tty_io.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 *
 * Modified by Theodore Ts'o, 9/14/92, to dynamically allocate the
 * tty_struct and tty_queue structures.  Previously there was an array
 * of 256 tty_struct's which was statically allocated, and the
 * tty_queue structures were allocated at boot time.  Both are now
 * dynamically allocated only when the tty is open.
 *
 * Also restructured routines so that there is more of a separation
 * between the high-level tty routines (tty_io.c and tty_ioctl.c) and
 * the low-level tty routines (serial.c, pty.c, console.c).  This
 * makes for cleaner and more compact code.  -TYT, 9/17/92 
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 *
 * NOTE: pay no attention to the line discipline code (yet); its
 * interface is still subject to change in this version...
 * -- TYT, 1/31/92
 *
 * Added functionality to the OPOST tty handling.  No delays, but all
 * other bits should be there.
 *	-- Nick Holloway <alfie@dcs.warwick.ac.uk>, 27th May 1993.
 *
 * Rewrote canonical mode and added more termios flags.
 * 	-- julian@uhunix.uhcc.hawaii.edu (J. Cowley), 13Jan94
 *
 * Reorganized FASYNC support so mouse code can share it.
 *	-- ctm@ardi.com, 9Sep95
 *
 * New TIOCLINUX variants added.
 *	-- mj@k332.feld.cvut.cz, 19-Nov-95
 * 
 * Restrict vt switching via ioctl()
 *      -- grif@cs.ucr.edu, 5-Dec-95
 *
 * Move console and virtual terminal code to more appropriate files,
 * implement CONFIG_VT and generalize console device interface.
 *	-- Marko Kohtala <Marko.Kohtala@hut.fi>, March 97
 *
 * Rewrote init_dev and release_dev to eliminate races.
 *	-- Bill Hawes <whawes@star.net>, June 97
 *
 * Added devfs support.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 13-Jan-1998
 *
 * Added support for a Unix98-style ptmx device.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 14-Jan-1998
 *
 * Reduced memory usage for older ARM systems
 *      -- Russell King <rmk@arm.linux.org.uk>
 *
 * Move do_SAK() into process context.  Less stack use in devfs functions.
 * alloc_tty_struct() always uses kmalloc() -- Andrew Morton <andrewm@uow.edu.eu> 17Mar01
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/devpts_fs.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/devfs_fs_kernel.h>

#include <linux/kmod.h>

#undef TTY_DEBUG_HANGUP

#define TTY_PARANOIA_CHECK 1
#define CHECK_TTY_COUNT 1

struct termios tty_std_termios = {	/* for the benefit of tty drivers  */
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
		   ECHOCTL | ECHOKE | IEXTEN,
	.c_cc = INIT_C_CC
};

EXPORT_SYMBOL(tty_std_termios);

/* This list gets poked at by procfs and various bits of boot up code. This
   could do with some rationalisation such as pulling the tty proc function
   into this file */
   
LIST_HEAD(tty_drivers);			/* linked list of tty drivers */

/* Semaphore to protect creating and releasing a tty. This is shared with
   vt.c for deeply disgusting hack reasons */
DECLARE_MUTEX(tty_sem);

#ifdef CONFIG_UNIX98_PTYS
extern struct tty_driver *ptm_driver;	/* Unix98 pty masters; for /dev/ptmx */
extern int pty_limit;		/* Config limit on Unix98 ptys */
static DEFINE_IDR(allocated_ptys);
static DECLARE_MUTEX(allocated_ptys_lock);
static int ptmx_open(struct inode *, struct file *);
#endif

extern void disable_early_printk(void);

static void initialize_tty_struct(struct tty_struct *tty);

static ssize_t tty_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t tty_write(struct file *, const char __user *, size_t, loff_t *);
ssize_t redirected_tty_write(struct file *, const char __user *, size_t, loff_t *);
static unsigned int tty_poll(struct file *, poll_table *);
static int tty_open(struct inode *, struct file *);
static int tty_release(struct inode *, struct file *);
int tty_ioctl(struct inode * inode, struct file * file,
	      unsigned int cmd, unsigned long arg);
static int tty_fasync(int fd, struct file * filp, int on);
static void release_mem(struct tty_struct *tty, int idx);


static struct tty_struct *alloc_tty_struct(void)
{
	struct tty_struct *tty;

	tty = kmalloc(sizeof(struct tty_struct), GFP_KERNEL);
	if (tty)
		memset(tty, 0, sizeof(struct tty_struct));
	return tty;
}

static void tty_buffer_free_all(struct tty_struct *);

static inline void free_tty_struct(struct tty_struct *tty)
{
	kfree(tty->write_buf);
	tty_buffer_free_all(tty);
	kfree(tty);
}

#define TTY_NUMBER(tty) ((tty)->index + (tty)->driver->name_base)

char *tty_name(struct tty_struct *tty, char *buf)
{
	if (!tty) /* Hmm.  NULL pointer.  That's fun. */
		strcpy(buf, "NULL tty");
	else
		strcpy(buf, tty->name);
	return buf;
}

EXPORT_SYMBOL(tty_name);

int tty_paranoia_check(struct tty_struct *tty, struct inode *inode,
			      const char *routine)
{
#ifdef TTY_PARANOIA_CHECK
	if (!tty) {
		printk(KERN_WARNING
			"null TTY for (%d:%d) in %s\n",
			imajor(inode), iminor(inode), routine);
		return 1;
	}
	if (tty->magic != TTY_MAGIC) {
		printk(KERN_WARNING
			"bad magic number for tty struct (%d:%d) in %s\n",
			imajor(inode), iminor(inode), routine);
		return 1;
	}
#endif
	return 0;
}

static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct list_head *p;
	int count = 0;
	
	file_list_lock();
	list_for_each(p, &tty->tty_files) {
		count++;
	}
	file_list_unlock();
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_SLAVE &&
	    tty->link && tty->link->count)
		count++;
	if (tty->count != count) {
		printk(KERN_WARNING "Warning: dev (%s) tty->count(%d) "
				    "!= #fd's(%d) in %s\n",
		       tty->name, tty->count, count, routine);
		return count;
       }	
#endif
	return 0;
}

/*
 * Tty buffer allocation management
 */

static void tty_buffer_free_all(struct tty_struct *tty)
{
	struct tty_buffer *thead;
	while((thead = tty->buf.head) != NULL) {
		tty->buf.head = thead->next;
		kfree(thead);
	}
	while((thead = tty->buf.free) != NULL) {
		tty->buf.free = thead->next;
		kfree(thead);
	}
	tty->buf.tail = NULL;
}

static void tty_buffer_init(struct tty_struct *tty)
{
	spin_lock_init(&tty->buf.lock);
	tty->buf.head = NULL;
	tty->buf.tail = NULL;
	tty->buf.free = NULL;
}

static struct tty_buffer *tty_buffer_alloc(size_t size)
{
	struct tty_buffer *p = kmalloc(sizeof(struct tty_buffer) + 2 * size, GFP_ATOMIC);
	if(p == NULL)
		return NULL;
	p->used = 0;
	p->size = size;
	p->next = NULL;
	p->active = 0;
	p->commit = 0;
	p->read = 0;
	p->char_buf_ptr = (char *)(p->data);
	p->flag_buf_ptr = (unsigned char *)p->char_buf_ptr + size;
/* 	printk("Flip create %p\n", p); */
	return p;
}

/* Must be called with the tty_read lock held. This needs to acquire strategy
   code to decide if we should kfree or relink a given expired buffer */

static void tty_buffer_free(struct tty_struct *tty, struct tty_buffer *b)
{
	/* Dumb strategy for now - should keep some stats */
/* 	printk("Flip dispose %p\n", b); */
	if(b->size >= 512)
		kfree(b);
	else {
		b->next = tty->buf.free;
		tty->buf.free = b;
	}
}

static struct tty_buffer *tty_buffer_find(struct tty_struct *tty, size_t size)
{
	struct tty_buffer **tbh = &tty->buf.free;
	while((*tbh) != NULL) {
		struct tty_buffer *t = *tbh;
		if(t->size >= size) {
			*tbh = t->next;
			t->next = NULL;
			t->used = 0;
			t->commit = 0;
			t->read = 0;
			/* DEBUG ONLY */
			memset(t->data, '*', size);
/* 			printk("Flip recycle %p\n", t); */
			return t;
		}
		tbh = &((*tbh)->next);
	}
	/* Round the buffer size out */
	size = (size + 0xFF) & ~ 0xFF;
	return tty_buffer_alloc(size);
	/* Should possibly check if this fails for the largest buffer we
	   have queued and recycle that ? */
}

int tty_buffer_request_room(struct tty_struct *tty, size_t size)
{
	struct tty_buffer *b, *n;
	int left;
	unsigned long flags;

	spin_lock_irqsave(&tty->buf.lock, flags);

	/* OPTIMISATION: We could keep a per tty "zero" sized buffer to
	   remove this conditional if its worth it. This would be invisible
	   to the callers */
	if ((b = tty->buf.tail) != NULL) {
		left = b->size - b->used;
		b->active = 1;
	} else
		left = 0;

	if (left < size) {
		/* This is the slow path - looking for new buffers to use */
		if ((n = tty_buffer_find(tty, size)) != NULL) {
			if (b != NULL) {
				b->next = n;
				b->active = 0;
				b->commit = b->used;
			} else
				tty->buf.head = n;
			tty->buf.tail = n;
			n->active = 1;
		} else
			size = left;
	}

	spin_unlock_irqrestore(&tty->buf.lock, flags);
	return size;
}

EXPORT_SYMBOL_GPL(tty_buffer_request_room);

int tty_insert_flip_string(struct tty_struct *tty, unsigned char *chars, size_t size)
{
	int copied = 0;
	do {
		int space = tty_buffer_request_room(tty, size - copied);
		struct tty_buffer *tb = tty->buf.tail;
		/* If there is no space then tb may be NULL */
		if(unlikely(space == 0))
			break;
		memcpy(tb->char_buf_ptr + tb->used, chars, space);
		memset(tb->flag_buf_ptr + tb->used, TTY_NORMAL, space);
		tb->used += space;
		copied += space;
		chars += space;
/* 		printk("Flip insert %d.\n", space); */
	}
	/* There is a small chance that we need to split the data over
	   several buffers. If this is the case we must loop */
	while (unlikely(size > copied));
	return copied;
}

EXPORT_SYMBOL_GPL(tty_insert_flip_string);

int tty_insert_flip_string_flags(struct tty_struct *tty, unsigned char *chars, char *flags, size_t size)
{
	int copied = 0;
	do {
		int space = tty_buffer_request_room(tty, size - copied);
		struct tty_buffer *tb = tty->buf.tail;
		/* If there is no space then tb may be NULL */
		if(unlikely(space == 0))
			break;
		memcpy(tb->char_buf_ptr + tb->used, chars, space);
		memcpy(tb->flag_buf_ptr + tb->used, flags, space);
		tb->used += space;
		copied += space;
		chars += space;
		flags += space;
	}
	/* There is a small chance that we need to split the data over
	   several buffers. If this is the case we must loop */
	while (unlikely(size > copied));
	return copied;
}

EXPORT_SYMBOL_GPL(tty_insert_flip_string_flags);


/*
 *	Prepare a block of space in the buffer for data. Returns the length
 *	available and buffer pointer to the space which is now allocated and
 *	accounted for as ready for normal characters. This is used for drivers
 *	that need their own block copy routines into the buffer. There is no
 *	guarantee the buffer is a DMA target!
 */

int tty_prepare_flip_string(struct tty_struct *tty, unsigned char **chars, size_t size)
{
	int space = tty_buffer_request_room(tty, size);
	if (likely(space)) {
		struct tty_buffer *tb = tty->buf.tail;
		*chars = tb->char_buf_ptr + tb->used;
		memset(tb->flag_buf_ptr + tb->used, TTY_NORMAL, space);
		tb->used += space;
	}
	return space;
}

EXPORT_SYMBOL_GPL(tty_prepare_flip_string);

/*
 *	Prepare a block of space in the buffer for data. Returns the length
 *	available and buffer pointer to the space which is now allocated and
 *	accounted for as ready for characters. This is used for drivers
 *	that need their own block copy routines into the buffer. There is no
 *	guarantee the buffer is a DMA target!
 */

int tty_prepare_flip_string_flags(struct tty_struct *tty, unsigned char **chars, char **flags, size_t size)
{
	int space = tty_buffer_request_room(tty, size);
	if (likely(space)) {
		struct tty_buffer *tb = tty->buf.tail;
		*chars = tb->char_buf_ptr + tb->used;
		*flags = tb->flag_buf_ptr + tb->used;
		tb->used += space;
	}
	return space;
}

EXPORT_SYMBOL_GPL(tty_prepare_flip_string_flags);



/*
 *	This is probably overkill for real world processors but
 *	they are not on hot paths so a little discipline won't do 
 *	any harm.
 */
 
static void tty_set_termios_ldisc(struct tty_struct *tty, int num)
{
	down(&tty->termios_sem);
	tty->termios->c_line = num;
	up(&tty->termios_sem);
}

/*
 *	This guards the refcounted line discipline lists. The lock
 *	must be taken with irqs off because there are hangup path
 *	callers who will do ldisc lookups and cannot sleep.
 */
 
static DEFINE_SPINLOCK(tty_ldisc_lock);
static DECLARE_WAIT_QUEUE_HEAD(tty_ldisc_wait);
static struct tty_ldisc tty_ldiscs[NR_LDISCS];	/* line disc dispatch table */

int tty_register_ldisc(int disc, struct tty_ldisc *new_ldisc)
{
	unsigned long flags;
	int ret = 0;
	
	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;
	
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	tty_ldiscs[disc] = *new_ldisc;
	tty_ldiscs[disc].num = disc;
	tty_ldiscs[disc].flags |= LDISC_FLAG_DEFINED;
	tty_ldiscs[disc].refcount = 0;
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	
	return ret;
}
EXPORT_SYMBOL(tty_register_ldisc);

int tty_unregister_ldisc(int disc)
{
	unsigned long flags;
	int ret = 0;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	if (tty_ldiscs[disc].refcount)
		ret = -EBUSY;
	else
		tty_ldiscs[disc].flags &= ~LDISC_FLAG_DEFINED;
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);

	return ret;
}
EXPORT_SYMBOL(tty_unregister_ldisc);

struct tty_ldisc *tty_ldisc_get(int disc)
{
	unsigned long flags;
	struct tty_ldisc *ld;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return NULL;
	
	spin_lock_irqsave(&tty_ldisc_lock, flags);

	ld = &tty_ldiscs[disc];
	/* Check the entry is defined */
	if(ld->flags & LDISC_FLAG_DEFINED)
	{
		/* If the module is being unloaded we can't use it */
		if (!try_module_get(ld->owner))
		       	ld = NULL;
		else /* lock it */
			ld->refcount++;
	}
	else
		ld = NULL;
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return ld;
}

EXPORT_SYMBOL_GPL(tty_ldisc_get);

void tty_ldisc_put(int disc)
{
	struct tty_ldisc *ld;
	unsigned long flags;
	
	if (disc < N_TTY || disc >= NR_LDISCS)
		BUG();
		
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ld = &tty_ldiscs[disc];
	if(ld->refcount == 0)
		BUG();
	ld->refcount --;
	module_put(ld->owner);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
}
	
EXPORT_SYMBOL_GPL(tty_ldisc_put);

static void tty_ldisc_assign(struct tty_struct *tty, struct tty_ldisc *ld)
{
	tty->ldisc = *ld;
	tty->ldisc.refcount = 0;
}

/**
 *	tty_ldisc_try		-	internal helper
 *	@tty: the tty
 *
 *	Make a single attempt to grab and bump the refcount on
 *	the tty ldisc. Return 0 on failure or 1 on success. This is
 *	used to implement both the waiting and non waiting versions
 *	of tty_ldisc_ref
 */

static int tty_ldisc_try(struct tty_struct *tty)
{
	unsigned long flags;
	struct tty_ldisc *ld;
	int ret = 0;
	
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ld = &tty->ldisc;
	if(test_bit(TTY_LDISC, &tty->flags))
	{
		ld->refcount++;
		ret = 1;
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return ret;
}

/**
 *	tty_ldisc_ref_wait	-	wait for the tty ldisc
 *	@tty: tty device
 *
 *	Dereference the line discipline for the terminal and take a 
 *	reference to it. If the line discipline is in flux then 
 *	wait patiently until it changes.
 *
 *	Note: Must not be called from an IRQ/timer context. The caller
 *	must also be careful not to hold other locks that will deadlock
 *	against a discipline change, such as an existing ldisc reference
 *	(which we check for)
 */
 
struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *tty)
{
	/* wait_event is a macro */
	wait_event(tty_ldisc_wait, tty_ldisc_try(tty));
	if(tty->ldisc.refcount == 0)
		printk(KERN_ERR "tty_ldisc_ref_wait\n");
	return &tty->ldisc;
}

EXPORT_SYMBOL_GPL(tty_ldisc_ref_wait);

/**
 *	tty_ldisc_ref		-	get the tty ldisc
 *	@tty: tty device
 *
 *	Dereference the line discipline for the terminal and take a 
 *	reference to it. If the line discipline is in flux then 
 *	return NULL. Can be called from IRQ and timer functions.
 */
 
struct tty_ldisc *tty_ldisc_ref(struct tty_struct *tty)
{
	if(tty_ldisc_try(tty))
		return &tty->ldisc;
	return NULL;
}

EXPORT_SYMBOL_GPL(tty_ldisc_ref);

/**
 *	tty_ldisc_deref		-	free a tty ldisc reference
 *	@ld: reference to free up
 *
 *	Undoes the effect of tty_ldisc_ref or tty_ldisc_ref_wait. May
 *	be called in IRQ context.
 */
 
void tty_ldisc_deref(struct tty_ldisc *ld)
{
	unsigned long flags;

	if(ld == NULL)
		BUG();
		
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	if(ld->refcount == 0)
		printk(KERN_ERR "tty_ldisc_deref: no references.\n");
	else
		ld->refcount--;
	if(ld->refcount == 0)
		wake_up(&tty_ldisc_wait);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
}

EXPORT_SYMBOL_GPL(tty_ldisc_deref);

/**
 *	tty_ldisc_enable	-	allow ldisc use
 *	@tty: terminal to activate ldisc on
 *
 *	Set the TTY_LDISC flag when the line discipline can be called
 *	again. Do neccessary wakeups for existing sleepers.
 *
 *	Note: nobody should set this bit except via this function. Clearing
 *	directly is allowed.
 */

static void tty_ldisc_enable(struct tty_struct *tty)
{
	set_bit(TTY_LDISC, &tty->flags);
	wake_up(&tty_ldisc_wait);
}
	
/**
 *	tty_set_ldisc		-	set line discipline
 *	@tty: the terminal to set
 *	@ldisc: the line discipline
 *
 *	Set the discipline of a tty line. Must be called from a process
 *	context.
 */
 
static int tty_set_ldisc(struct tty_struct *tty, int ldisc)
{
	int retval = 0;
	struct tty_ldisc o_ldisc;
	char buf[64];
	int work;
	unsigned long flags;
	struct tty_ldisc *ld;
	struct tty_struct *o_tty;

	if ((ldisc < N_TTY) || (ldisc >= NR_LDISCS))
		return -EINVAL;

restart:

	ld = tty_ldisc_get(ldisc);
	/* Eduardo Blanco <ejbs@cs.cs.com.uy> */
	/* Cyrus Durgin <cider@speakeasy.org> */
	if (ld == NULL) {
		request_module("tty-ldisc-%d", ldisc);
		ld = tty_ldisc_get(ldisc);
	}
	if (ld == NULL)
		return -EINVAL;

	/*
	 *	No more input please, we are switching. The new ldisc
	 *	will update this value in the ldisc open function
	 */

	tty->receive_room = 0;

	/*
	 *	Problem: What do we do if this blocks ?
	 */

	tty_wait_until_sent(tty, 0);

	if (tty->ldisc.num == ldisc) {
		tty_ldisc_put(ldisc);
		return 0;
	}

	o_ldisc = tty->ldisc;
	o_tty = tty->link;

	/*
	 *	Make sure we don't change while someone holds a
	 *	reference to the line discipline. The TTY_LDISC bit
	 *	prevents anyone taking a reference once it is clear.
	 *	We need the lock to avoid racing reference takers.
	 */

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	if (tty->ldisc.refcount || (o_tty && o_tty->ldisc.refcount)) {
		if(tty->ldisc.refcount) {
			/* Free the new ldisc we grabbed. Must drop the lock
			   first. */
			spin_unlock_irqrestore(&tty_ldisc_lock, flags);
			tty_ldisc_put(ldisc);
			/*
			 * There are several reasons we may be busy, including
			 * random momentary I/O traffic. We must therefore
			 * retry. We could distinguish between blocking ops
			 * and retries if we made tty_ldisc_wait() smarter. That
			 * is up for discussion.
			 */
			if (wait_event_interruptible(tty_ldisc_wait, tty->ldisc.refcount == 0) < 0)
				return -ERESTARTSYS;
			goto restart;
		}
		if(o_tty && o_tty->ldisc.refcount) {
			spin_unlock_irqrestore(&tty_ldisc_lock, flags);
			tty_ldisc_put(ldisc);
			if (wait_event_interruptible(tty_ldisc_wait, o_tty->ldisc.refcount == 0) < 0)
				return -ERESTARTSYS;
			goto restart;
		}
	}

	/* if the TTY_LDISC bit is set, then we are racing against another ldisc change */

	if (!test_bit(TTY_LDISC, &tty->flags)) {
		spin_unlock_irqrestore(&tty_ldisc_lock, flags);
		tty_ldisc_put(ldisc);
		ld = tty_ldisc_ref_wait(tty);
		tty_ldisc_deref(ld);
		goto restart;
	}

	clear_bit(TTY_LDISC, &tty->flags);
	clear_bit(TTY_DONT_FLIP, &tty->flags);
	if (o_tty) {
		clear_bit(TTY_LDISC, &o_tty->flags);
		clear_bit(TTY_DONT_FLIP, &o_tty->flags);
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);

	/*
	 *	From this point on we know nobody has an ldisc
	 *	usage reference, nor can they obtain one until
	 *	we say so later on.
	 */

	work = cancel_delayed_work(&tty->buf.work);
	/*
	 * Wait for ->hangup_work and ->buf.work handlers to terminate
	 */
	 
	flush_scheduled_work();
	/* Shutdown the current discipline. */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);

	/* Now set up the new line discipline. */
	tty_ldisc_assign(tty, ld);
	tty_set_termios_ldisc(tty, ldisc);
	if (tty->ldisc.open)
		retval = (tty->ldisc.open)(tty);
	if (retval < 0) {
		tty_ldisc_put(ldisc);
		/* There is an outstanding reference here so this is safe */
		tty_ldisc_assign(tty, tty_ldisc_get(o_ldisc.num));
		tty_set_termios_ldisc(tty, tty->ldisc.num);
		if (tty->ldisc.open && (tty->ldisc.open(tty) < 0)) {
			tty_ldisc_put(o_ldisc.num);
			/* This driver is always present */
			tty_ldisc_assign(tty, tty_ldisc_get(N_TTY));
			tty_set_termios_ldisc(tty, N_TTY);
			if (tty->ldisc.open) {
				int r = tty->ldisc.open(tty);

				if (r < 0)
					panic("Couldn't open N_TTY ldisc for "
					      "%s --- error %d.",
					      tty_name(tty, buf), r);
			}
		}
	}
	/* At this point we hold a reference to the new ldisc and a
	   a reference to the old ldisc. If we ended up flipping back
	   to the existing ldisc we have two references to it */
	
	if (tty->ldisc.num != o_ldisc.num && tty->driver->set_ldisc)
		tty->driver->set_ldisc(tty);
		
	tty_ldisc_put(o_ldisc.num);
	
	/*
	 *	Allow ldisc referencing to occur as soon as the driver
	 *	ldisc callback completes.
	 */
	 
	tty_ldisc_enable(tty);
	if (o_tty)
		tty_ldisc_enable(o_tty);
	
	/* Restart it in case no characters kick it off. Safe if
	   already running */
	if (work)
		schedule_delayed_work(&tty->buf.work, 1);
	return retval;
}

/*
 * This routine returns a tty driver structure, given a device number
 */
static struct tty_driver *get_tty_driver(dev_t device, int *index)
{
	struct tty_driver *p;

	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		dev_t base = MKDEV(p->major, p->minor_start);
		if (device < base || device >= base + p->num)
			continue;
		*index = device - base;
		return p;
	}
	return NULL;
}

/*
 * If we try to write to, or set the state of, a terminal and we're
 * not in the foreground, send a SIGTTOU.  If the signal is blocked or
 * ignored, go ahead and perform the operation.  (POSIX 7.2)
 */
int tty_check_change(struct tty_struct * tty)
{
	if (current->signal->tty != tty)
		return 0;
	if (tty->pgrp <= 0) {
		printk(KERN_WARNING "tty_check_change: tty->pgrp <= 0!\n");
		return 0;
	}
	if (process_group(current) == tty->pgrp)
		return 0;
	if (is_ignored(SIGTTOU))
		return 0;
	if (is_orphaned_pgrp(process_group(current)))
		return -EIO;
	(void) kill_pg(process_group(current), SIGTTOU, 1);
	return -ERESTARTSYS;
}

EXPORT_SYMBOL(tty_check_change);

static ssize_t hung_up_tty_read(struct file * file, char __user * buf,
				size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t hung_up_tty_write(struct file * file, const char __user * buf,
				 size_t count, loff_t *ppos)
{
	return -EIO;
}

/* No kernel lock held - none needed ;) */
static unsigned int hung_up_tty_poll(struct file * filp, poll_table * wait)
{
	return POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDNORM | POLLWRNORM;
}

static int hung_up_tty_ioctl(struct inode * inode, struct file * file,
			     unsigned int cmd, unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static struct file_operations tty_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= tty_write,
	.poll		= tty_poll,
	.ioctl		= tty_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

#ifdef CONFIG_UNIX98_PTYS
static struct file_operations ptmx_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= tty_write,
	.poll		= tty_poll,
	.ioctl		= tty_ioctl,
	.open		= ptmx_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};
#endif

static struct file_operations console_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= redirected_tty_write,
	.poll		= tty_poll,
	.ioctl		= tty_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

static struct file_operations hung_up_tty_fops = {
	.llseek		= no_llseek,
	.read		= hung_up_tty_read,
	.write		= hung_up_tty_write,
	.poll		= hung_up_tty_poll,
	.ioctl		= hung_up_tty_ioctl,
	.release	= tty_release,
};

static DEFINE_SPINLOCK(redirect_lock);
static struct file *redirect;

/**
 *	tty_wakeup	-	request more data
 *	@tty: terminal
 *
 *	Internal and external helper for wakeups of tty. This function
 *	informs the line discipline if present that the driver is ready
 *	to receive more output data.
 */
 
void tty_wakeup(struct tty_struct *tty)
{
	struct tty_ldisc *ld;
	
	if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) {
		ld = tty_ldisc_ref(tty);
		if(ld) {
			if(ld->write_wakeup)
				ld->write_wakeup(tty);
			tty_ldisc_deref(ld);
		}
	}
	wake_up_interruptible(&tty->write_wait);
}

EXPORT_SYMBOL_GPL(tty_wakeup);

/**
 *	tty_ldisc_flush	-	flush line discipline queue
 *	@tty: tty
 *
 *	Flush the line discipline queue (if any) for this tty. If there
 *	is no line discipline active this is a no-op.
 */
 
void tty_ldisc_flush(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_ref(tty);
	if(ld) {
		if(ld->flush_buffer)
			ld->flush_buffer(tty);
		tty_ldisc_deref(ld);
	}
}

EXPORT_SYMBOL_GPL(tty_ldisc_flush);
	
/*
 * This can be called by the "eventd" kernel thread.  That is process synchronous,
 * but doesn't hold any locks, so we need to make sure we have the appropriate
 * locks for what we're doing..
 */
static void do_tty_hangup(void *data)
{
	struct tty_struct *tty = (struct tty_struct *) data;
	struct file * cons_filp = NULL;
	struct file *filp, *f = NULL;
	struct task_struct *p;
	struct tty_ldisc *ld;
	int    closecount = 0, n;

	if (!tty)
		return;

	/* inuse_filps is protected by the single kernel lock */
	lock_kernel();

	spin_lock(&redirect_lock);
	if (redirect && redirect->private_data == tty) {
		f = redirect;
		redirect = NULL;
	}
	spin_unlock(&redirect_lock);
	
	check_tty_count(tty, "do_tty_hangup");
	file_list_lock();
	/* This breaks for file handles being sent over AF_UNIX sockets ? */
	list_for_each_entry(filp, &tty->tty_files, f_u.fu_list) {
		if (filp->f_op->write == redirected_tty_write)
			cons_filp = filp;
		if (filp->f_op->write != tty_write)
			continue;
		closecount++;
		tty_fasync(-1, filp, 0);	/* can't block */
		filp->f_op = &hung_up_tty_fops;
	}
	file_list_unlock();
	
	/* FIXME! What are the locking issues here? This may me overdoing things..
	 * this question is especially important now that we've removed the irqlock. */

	ld = tty_ldisc_ref(tty);
	if(ld != NULL)	/* We may have no line discipline at this point */
	{
		if (ld->flush_buffer)
			ld->flush_buffer(tty);
		if (tty->driver->flush_buffer)
			tty->driver->flush_buffer(tty);
		if ((test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) &&
		    ld->write_wakeup)
			ld->write_wakeup(tty);
		if (ld->hangup)
			ld->hangup(tty);
	}

	/* FIXME: Once we trust the LDISC code better we can wait here for
	   ldisc completion and fix the driver call race */
	   
	wake_up_interruptible(&tty->write_wait);
	wake_up_interruptible(&tty->read_wait);

	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY.
	 */
	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
	{
		down(&tty->termios_sem);
		*tty->termios = tty->driver->init_termios;
		up(&tty->termios_sem);
	}
	
	/* Defer ldisc switch */
	/* tty_deferred_ldisc_switch(N_TTY);
	
	  This should get done automatically when the port closes and
	  tty_release is called */
	
	read_lock(&tasklist_lock);
	if (tty->session > 0) {
		do_each_task_pid(tty->session, PIDTYPE_SID, p) {
			if (p->signal->tty == tty)
				p->signal->tty = NULL;
			if (!p->signal->leader)
				continue;
			send_group_sig_info(SIGHUP, SEND_SIG_PRIV, p);
			send_group_sig_info(SIGCONT, SEND_SIG_PRIV, p);
			if (tty->pgrp > 0)
				p->signal->tty_old_pgrp = tty->pgrp;
		} while_each_task_pid(tty->session, PIDTYPE_SID, p);
	}
	read_unlock(&tasklist_lock);

	tty->flags = 0;
	tty->session = 0;
	tty->pgrp = -1;
	tty->ctrl_status = 0;
	/*
	 *	If one of the devices matches a console pointer, we
	 *	cannot just call hangup() because that will cause
	 *	tty->count and state->count to go out of sync.
	 *	So we just call close() the right number of times.
	 */
	if (cons_filp) {
		if (tty->driver->close)
			for (n = 0; n < closecount; n++)
				tty->driver->close(tty, cons_filp);
	} else if (tty->driver->hangup)
		(tty->driver->hangup)(tty);
		
	/* We don't want to have driver/ldisc interactions beyond
	   the ones we did here. The driver layer expects no
	   calls after ->hangup() from the ldisc side. However we
	   can't yet guarantee all that */

	set_bit(TTY_HUPPED, &tty->flags);
	if (ld) {
		tty_ldisc_enable(tty);
		tty_ldisc_deref(ld);
	}
	unlock_kernel();
	if (f)
		fput(f);
}

void tty_hangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];
	
	printk(KERN_DEBUG "%s hangup...\n", tty_name(tty, buf));
#endif
	schedule_work(&tty->hangup_work);
}

EXPORT_SYMBOL(tty_hangup);

void tty_vhangup(struct tty_struct * tty)
{
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];

	printk(KERN_DEBUG "%s vhangup...\n", tty_name(tty, buf));
#endif
	do_tty_hangup((void *) tty);
}
EXPORT_SYMBOL(tty_vhangup);

int tty_hung_up_p(struct file * filp)
{
	return (filp->f_op == &hung_up_tty_fops);
}

EXPORT_SYMBOL(tty_hung_up_p);

/*
 * This function is typically called only by the session leader, when
 * it wants to disassociate itself from its controlling tty.
 *
 * It performs the following functions:
 * 	(1)  Sends a SIGHUP and SIGCONT to the foreground process group
 * 	(2)  Clears the tty from being controlling the session
 * 	(3)  Clears the controlling tty for all processes in the
 * 		session group.
 *
 * The argument on_exit is set to 1 if called when a process is
 * exiting; it is 0 if called by the ioctl TIOCNOTTY.
 */
void disassociate_ctty(int on_exit)
{
	struct tty_struct *tty;
	struct task_struct *p;
	int tty_pgrp = -1;

	lock_kernel();

	down(&tty_sem);
	tty = current->signal->tty;
	if (tty) {
		tty_pgrp = tty->pgrp;
		up(&tty_sem);
		if (on_exit && tty->driver->type != TTY_DRIVER_TYPE_PTY)
			tty_vhangup(tty);
	} else {
		if (current->signal->tty_old_pgrp) {
			kill_pg(current->signal->tty_old_pgrp, SIGHUP, on_exit);
			kill_pg(current->signal->tty_old_pgrp, SIGCONT, on_exit);
		}
		up(&tty_sem);
		unlock_kernel();	
		return;
	}
	if (tty_pgrp > 0) {
		kill_pg(tty_pgrp, SIGHUP, on_exit);
		if (!on_exit)
			kill_pg(tty_pgrp, SIGCONT, on_exit);
	}

	/* Must lock changes to tty_old_pgrp */
	down(&tty_sem);
	current->signal->tty_old_pgrp = 0;
	tty->session = 0;
	tty->pgrp = -1;

	/* Now clear signal->tty under the lock */
	read_lock(&tasklist_lock);
	do_each_task_pid(current->signal->session, PIDTYPE_SID, p) {
		p->signal->tty = NULL;
	} while_each_task_pid(current->signal->session, PIDTYPE_SID, p);
	read_unlock(&tasklist_lock);
	up(&tty_sem);
	unlock_kernel();
}

void stop_tty(struct tty_struct *tty)
{
	if (tty->stopped)
		return;
	tty->stopped = 1;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_START;
		tty->ctrl_status |= TIOCPKT_STOP;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver->stop)
		(tty->driver->stop)(tty);
}

EXPORT_SYMBOL(stop_tty);

void start_tty(struct tty_struct *tty)
{
	if (!tty->stopped || tty->flow_stopped)
		return;
	tty->stopped = 0;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_STOP;
		tty->ctrl_status |= TIOCPKT_START;
		wake_up_interruptible(&tty->link->read_wait);
	}
	if (tty->driver->start)
		(tty->driver->start)(tty);

	/* If we have a running line discipline it may need kicking */
	tty_wakeup(tty);
	wake_up_interruptible(&tty->write_wait);
}

EXPORT_SYMBOL(start_tty);

static ssize_t tty_read(struct file * file, char __user * buf, size_t count, 
			loff_t *ppos)
{
	int i;
	struct tty_struct * tty;
	struct inode *inode;
	struct tty_ldisc *ld;

	tty = (struct tty_struct *)file->private_data;
	inode = file->f_dentry->d_inode;
	if (tty_paranoia_check(tty, inode, "tty_read"))
		return -EIO;
	if (!tty || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;

	/* We want to wait for the line discipline to sort out in this
	   situation */
	ld = tty_ldisc_ref_wait(tty);
	lock_kernel();
	if (ld->read)
		i = (ld->read)(tty,file,buf,count);
	else
		i = -EIO;
	tty_ldisc_deref(ld);
	unlock_kernel();
	if (i > 0)
		inode->i_atime = current_fs_time(inode->i_sb);
	return i;
}

/*
 * Split writes up in sane blocksizes to avoid
 * denial-of-service type attacks
 */
static inline ssize_t do_tty_write(
	ssize_t (*write)(struct tty_struct *, struct file *, const unsigned char *, size_t),
	struct tty_struct *tty,
	struct file *file,
	const char __user *buf,
	size_t count)
{
	ssize_t ret = 0, written = 0;
	unsigned int chunk;
	
	if (down_interruptible(&tty->atomic_write)) {
		return -ERESTARTSYS;
	}

	/*
	 * We chunk up writes into a temporary buffer. This
	 * simplifies low-level drivers immensely, since they
	 * don't have locking issues and user mode accesses.
	 *
	 * But if TTY_NO_WRITE_SPLIT is set, we should use a
	 * big chunk-size..
	 *
	 * The default chunk-size is 2kB, because the NTTY
	 * layer has problems with bigger chunks. It will
	 * claim to be able to handle more characters than
	 * it actually does.
	 */
	chunk = 2048;
	if (test_bit(TTY_NO_WRITE_SPLIT, &tty->flags))
		chunk = 65536;
	if (count < chunk)
		chunk = count;

	/* write_buf/write_cnt is protected by the atomic_write semaphore */
	if (tty->write_cnt < chunk) {
		unsigned char *buf;

		if (chunk < 1024)
			chunk = 1024;

		buf = kmalloc(chunk, GFP_KERNEL);
		if (!buf) {
			up(&tty->atomic_write);
			return -ENOMEM;
		}
		kfree(tty->write_buf);
		tty->write_cnt = chunk;
		tty->write_buf = buf;
	}

	/* Do the write .. */
	for (;;) {
		size_t size = count;
		if (size > chunk)
			size = chunk;
		ret = -EFAULT;
		if (copy_from_user(tty->write_buf, buf, size))
			break;
		lock_kernel();
		ret = write(tty, file, tty->write_buf, size);
		unlock_kernel();
		if (ret <= 0)
			break;
		written += ret;
		buf += ret;
		count -= ret;
		if (!count)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		cond_resched();
	}
	if (written) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_mtime = current_fs_time(inode->i_sb);
		ret = written;
	}
	up(&tty->atomic_write);
	return ret;
}


static ssize_t tty_write(struct file * file, const char __user * buf, size_t count,
			 loff_t *ppos)
{
	struct tty_struct * tty;
	struct inode *inode = file->f_dentry->d_inode;
	ssize_t ret;
	struct tty_ldisc *ld;
	
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode, "tty_write"))
		return -EIO;
	if (!tty || !tty->driver->write || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;

	ld = tty_ldisc_ref_wait(tty);		
	if (!ld->write)
		ret = -EIO;
	else
		ret = do_tty_write(ld->write, tty, file, buf, count);
	tty_ldisc_deref(ld);
	return ret;
}

ssize_t redirected_tty_write(struct file * file, const char __user * buf, size_t count,
			 loff_t *ppos)
{
	struct file *p = NULL;

	spin_lock(&redirect_lock);
	if (redirect) {
		get_file(redirect);
		p = redirect;
	}
	spin_unlock(&redirect_lock);

	if (p) {
		ssize_t res;
		res = vfs_write(p, buf, count, &p->f_pos);
		fput(p);
		return res;
	}

	return tty_write(file, buf, count, ppos);
}

static char ptychar[] = "pqrstuvwxyzabcde";

static inline void pty_line_name(struct tty_driver *driver, int index, char *p)
{
	int i = index + driver->name_base;
	/* ->name is initialized to "ttyp", but "tty" is expected */
	sprintf(p, "%s%c%x",
			driver->subtype == PTY_TYPE_SLAVE ? "tty" : driver->name,
			ptychar[i >> 4 & 0xf], i & 0xf);
}

static inline void tty_line_name(struct tty_driver *driver, int index, char *p)
{
	sprintf(p, "%s%d", driver->name, index + driver->name_base);
}

/*
 * WSH 06/09/97: Rewritten to remove races and properly clean up after a
 * failed open.  The new code protects the open with a semaphore, so it's
 * really quite straightforward.  The semaphore locking can probably be
 * relaxed for the (most common) case of reopening a tty.
 */
static int init_dev(struct tty_driver *driver, int idx,
	struct tty_struct **ret_tty)
{
	struct tty_struct *tty, *o_tty;
	struct termios *tp, **tp_loc, *o_tp, **o_tp_loc;
	struct termios *ltp, **ltp_loc, *o_ltp, **o_ltp_loc;
	int retval=0;

	/* check whether we're reopening an existing tty */
	if (driver->flags & TTY_DRIVER_DEVPTS_MEM) {
		tty = devpts_get_tty(idx);
		if (tty && driver->subtype == PTY_TYPE_MASTER)
			tty = tty->link;
	} else {
		tty = driver->ttys[idx];
	}
	if (tty) goto fast_track;

	/*
	 * First time open is complex, especially for PTY devices.
	 * This code guarantees that either everything succeeds and the
	 * TTY is ready for operation, or else the table slots are vacated
	 * and the allocated memory released.  (Except that the termios 
	 * and locked termios may be retained.)
	 */

	if (!try_module_get(driver->owner)) {
		retval = -ENODEV;
		goto end_init;
	}

	o_tty = NULL;
	tp = o_tp = NULL;
	ltp = o_ltp = NULL;

	tty = alloc_tty_struct();
	if(!tty)
		goto fail_no_mem;
	initialize_tty_struct(tty);
	tty->driver = driver;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);

	if (driver->flags & TTY_DRIVER_DEVPTS_MEM) {
		tp_loc = &tty->termios;
		ltp_loc = &tty->termios_locked;
	} else {
		tp_loc = &driver->termios[idx];
		ltp_loc = &driver->termios_locked[idx];
	}

	if (!*tp_loc) {
		tp = (struct termios *) kmalloc(sizeof(struct termios),
						GFP_KERNEL);
		if (!tp)
			goto free_mem_out;
		*tp = driver->init_termios;
	}

	if (!*ltp_loc) {
		ltp = (struct termios *) kmalloc(sizeof(struct termios),
						 GFP_KERNEL);
		if (!ltp)
			goto free_mem_out;
		memset(ltp, 0, sizeof(struct termios));
	}

	if (driver->type == TTY_DRIVER_TYPE_PTY) {
		o_tty = alloc_tty_struct();
		if (!o_tty)
			goto free_mem_out;
		initialize_tty_struct(o_tty);
		o_tty->driver = driver->other;
		o_tty->index = idx;
		tty_line_name(driver->other, idx, o_tty->name);

		if (driver->flags & TTY_DRIVER_DEVPTS_MEM) {
			o_tp_loc = &o_tty->termios;
			o_ltp_loc = &o_tty->termios_locked;
		} else {
			o_tp_loc = &driver->other->termios[idx];
			o_ltp_loc = &driver->other->termios_locked[idx];
		}

		if (!*o_tp_loc) {
			o_tp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_tp)
				goto free_mem_out;
			*o_tp = driver->other->init_termios;
		}

		if (!*o_ltp_loc) {
			o_ltp = (struct termios *)
				kmalloc(sizeof(struct termios), GFP_KERNEL);
			if (!o_ltp)
				goto free_mem_out;
			memset(o_ltp, 0, sizeof(struct termios));
		}

		/*
		 * Everything allocated ... set up the o_tty structure.
		 */
		if (!(driver->other->flags & TTY_DRIVER_DEVPTS_MEM)) {
			driver->other->ttys[idx] = o_tty;
		}
		if (!*o_tp_loc)
			*o_tp_loc = o_tp;
		if (!*o_ltp_loc)
			*o_ltp_loc = o_ltp;
		o_tty->termios = *o_tp_loc;
		o_tty->termios_locked = *o_ltp_loc;
		driver->other->refcount++;
		if (driver->subtype == PTY_TYPE_MASTER)
			o_tty->count++;

		/* Establish the links in both directions */
		tty->link   = o_tty;
		o_tty->link = tty;
	}

	/* 
	 * All structures have been allocated, so now we install them.
	 * Failures after this point use release_mem to clean up, so 
	 * there's no need to null out the local pointers.
	 */
	if (!(driver->flags & TTY_DRIVER_DEVPTS_MEM)) {
		driver->ttys[idx] = tty;
	}
	
	if (!*tp_loc)
		*tp_loc = tp;
	if (!*ltp_loc)
		*ltp_loc = ltp;
	tty->termios = *tp_loc;
	tty->termios_locked = *ltp_loc;
	driver->refcount++;
	tty->count++;

	/* 
	 * Structures all installed ... call the ldisc open routines.
	 * If we fail here just call release_mem to clean up.  No need
	 * to decrement the use counts, as release_mem doesn't care.
	 */

	if (tty->ldisc.open) {
		retval = (tty->ldisc.open)(tty);
		if (retval)
			goto release_mem_out;
	}
	if (o_tty && o_tty->ldisc.open) {
		retval = (o_tty->ldisc.open)(o_tty);
		if (retval) {
			if (tty->ldisc.close)
				(tty->ldisc.close)(tty);
			goto release_mem_out;
		}
		tty_ldisc_enable(o_tty);
	}
	tty_ldisc_enable(tty);
	goto success;

	/*
	 * This fast open can be used if the tty is already open.
	 * No memory is allocated, and the only failures are from
	 * attempting to open a closing tty or attempting multiple
	 * opens on a pty master.
	 */
fast_track:
	if (test_bit(TTY_CLOSING, &tty->flags)) {
		retval = -EIO;
		goto end_init;
	}
	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER) {
		/*
		 * special case for PTY masters: only one open permitted, 
		 * and the slave side open count is incremented as well.
		 */
		if (tty->count) {
			retval = -EIO;
			goto end_init;
		}
		tty->link->count++;
	}
	tty->count++;
	tty->driver = driver; /* N.B. why do this every time?? */

	/* FIXME */
	if(!test_bit(TTY_LDISC, &tty->flags))
		printk(KERN_ERR "init_dev but no ldisc\n");
success:
	*ret_tty = tty;
	
	/* All paths come through here to release the semaphore */
end_init:
	return retval;

	/* Release locally allocated memory ... nothing placed in slots */
free_mem_out:
	kfree(o_tp);
	if (o_tty)
		free_tty_struct(o_tty);
	kfree(ltp);
	kfree(tp);
	free_tty_struct(tty);

fail_no_mem:
	module_put(driver->owner);
	retval = -ENOMEM;
	goto end_init;

	/* call the tty release_mem routine to clean out this slot */
release_mem_out:
	printk(KERN_INFO "init_dev: ldisc open failed, "
			 "clearing slot %d\n", idx);
	release_mem(tty, idx);
	goto end_init;
}

/*
 * Releases memory associated with a tty structure, and clears out the
 * driver table slots.
 */
static void release_mem(struct tty_struct *tty, int idx)
{
	struct tty_struct *o_tty;
	struct termios *tp;
	int devpts = tty->driver->flags & TTY_DRIVER_DEVPTS_MEM;

	if ((o_tty = tty->link) != NULL) {
		if (!devpts)
			o_tty->driver->ttys[idx] = NULL;
		if (o_tty->driver->flags & TTY_DRIVER_RESET_TERMIOS) {
			tp = o_tty->termios;
			if (!devpts)
				o_tty->driver->termios[idx] = NULL;
			kfree(tp);

			tp = o_tty->termios_locked;
			if (!devpts)
				o_tty->driver->termios_locked[idx] = NULL;
			kfree(tp);
		}
		o_tty->magic = 0;
		o_tty->driver->refcount--;
		file_list_lock();
		list_del_init(&o_tty->tty_files);
		file_list_unlock();
		free_tty_struct(o_tty);
	}

	if (!devpts)
		tty->driver->ttys[idx] = NULL;
	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS) {
		tp = tty->termios;
		if (!devpts)
			tty->driver->termios[idx] = NULL;
		kfree(tp);

		tp = tty->termios_locked;
		if (!devpts)
			tty->driver->termios_locked[idx] = NULL;
		kfree(tp);
	}

	tty->magic = 0;
	tty->driver->refcount--;
	file_list_lock();
	list_del_init(&tty->tty_files);
	file_list_unlock();
	module_put(tty->driver->owner);
	free_tty_struct(tty);
}

/*
 * Even releasing the tty structures is a tricky business.. We have
 * to be very careful that the structures are all released at the
 * same time, as interrupts might otherwise get the wrong pointers.
 *
 * WSH 09/09/97: rewritten to avoid some nasty race conditions that could
 * lead to double frees or releasing memory still in use.
 */
static void release_dev(struct file * filp)
{
	struct tty_struct *tty, *o_tty;
	int	pty_master, tty_closing, o_tty_closing, do_sleep;
	int	devpts_master, devpts;
	int	idx;
	char	buf[64];
	unsigned long flags;
	
	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode, "release_dev"))
		return;

	check_tty_count(tty, "release_dev");

	tty_fasync(-1, filp, 0);

	idx = tty->index;
	pty_master = (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
		      tty->driver->subtype == PTY_TYPE_MASTER);
	devpts = (tty->driver->flags & TTY_DRIVER_DEVPTS_MEM) != 0;
	devpts_master = pty_master && devpts;
	o_tty = tty->link;

#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver->num) {
		printk(KERN_DEBUG "release_dev: bad idx when trying to "
				  "free (%s)\n", tty->name);
		return;
	}
	if (!(tty->driver->flags & TTY_DRIVER_DEVPTS_MEM)) {
		if (tty != tty->driver->ttys[idx]) {
			printk(KERN_DEBUG "release_dev: driver.table[%d] not tty "
			       "for (%s)\n", idx, tty->name);
			return;
		}
		if (tty->termios != tty->driver->termios[idx]) {
			printk(KERN_DEBUG "release_dev: driver.termios[%d] not termios "
			       "for (%s)\n",
			       idx, tty->name);
			return;
		}
		if (tty->termios_locked != tty->driver->termios_locked[idx]) {
			printk(KERN_DEBUG "release_dev: driver.termios_locked[%d] not "
			       "termios_locked for (%s)\n",
			       idx, tty->name);
			return;
		}
	}
#endif

#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "release_dev of %s (tty count=%d)...",
	       tty_name(tty, buf), tty->count);
#endif

#ifdef TTY_PARANOIA_CHECK
	if (tty->driver->other &&
	     !(tty->driver->flags & TTY_DRIVER_DEVPTS_MEM)) {
		if (o_tty != tty->driver->other->ttys[idx]) {
			printk(KERN_DEBUG "release_dev: other->table[%d] "
					  "not o_tty for (%s)\n",
			       idx, tty->name);
			return;
		}
		if (o_tty->termios != tty->driver->other->termios[idx]) {
			printk(KERN_DEBUG "release_dev: other->termios[%d] "
					  "not o_termios for (%s)\n",
			       idx, tty->name);
			return;
		}
		if (o_tty->termios_locked != 
		      tty->driver->other->termios_locked[idx]) {
			printk(KERN_DEBUG "release_dev: other->termios_locked["
					  "%d] not o_termios_locked for (%s)\n",
			       idx, tty->name);
			return;
		}
		if (o_tty->link != tty) {
			printk(KERN_DEBUG "release_dev: bad pty pointers\n");
			return;
		}
	}
#endif
	if (tty->driver->close)
		tty->driver->close(tty, filp);

	/*
	 * Sanity check: if tty->count is going to zero, there shouldn't be
	 * any waiters on tty->read_wait or tty->write_wait.  We test the
	 * wait queues and kick everyone out _before_ actually starting to
	 * close.  This ensures that we won't block while releasing the tty
	 * structure.
	 *
	 * The test for the o_tty closing is necessary, since the master and
	 * slave sides may close in any order.  If the slave side closes out
	 * first, its count will be one, since the master side holds an open.
	 * Thus this test wouldn't be triggered at the time the slave closes,
	 * so we do it now.
	 *
	 * Note that it's possible for the tty to be opened again while we're
	 * flushing out waiters.  By recalculating the closing flags before
	 * each iteration we avoid any problems.
	 */
	while (1) {
		/* Guard against races with tty->count changes elsewhere and
		   opens on /dev/tty */
		   
		down(&tty_sem);
		tty_closing = tty->count <= 1;
		o_tty_closing = o_tty &&
			(o_tty->count <= (pty_master ? 1 : 0));
		do_sleep = 0;

		if (tty_closing) {
			if (waitqueue_active(&tty->read_wait)) {
				wake_up(&tty->read_wait);
				do_sleep++;
			}
			if (waitqueue_active(&tty->write_wait)) {
				wake_up(&tty->write_wait);
				do_sleep++;
			}
		}
		if (o_tty_closing) {
			if (waitqueue_active(&o_tty->read_wait)) {
				wake_up(&o_tty->read_wait);
				do_sleep++;
			}
			if (waitqueue_active(&o_tty->write_wait)) {
				wake_up(&o_tty->write_wait);
				do_sleep++;
			}
		}
		if (!do_sleep)
			break;

		printk(KERN_WARNING "release_dev: %s: read/write wait queue "
				    "active!\n", tty_name(tty, buf));
		up(&tty_sem);
		schedule();
	}	

	/*
	 * The closing flags are now consistent with the open counts on 
	 * both sides, and we've completed the last operation that could 
	 * block, so it's safe to proceed with closing.
	 */
	if (pty_master) {
		if (--o_tty->count < 0) {
			printk(KERN_WARNING "release_dev: bad pty slave count "
					    "(%d) for %s\n",
			       o_tty->count, tty_name(o_tty, buf));
			o_tty->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk(KERN_WARNING "release_dev: bad tty->count (%d) for %s\n",
		       tty->count, tty_name(tty, buf));
		tty->count = 0;
	}
	
	/*
	 * We've decremented tty->count, so we need to remove this file
	 * descriptor off the tty->tty_files list; this serves two
	 * purposes:
	 *  - check_tty_count sees the correct number of file descriptors
	 *    associated with this tty.
	 *  - do_tty_hangup no longer sees this file descriptor as
	 *    something that needs to be handled for hangups.
	 */
	file_kill(filp);
	filp->private_data = NULL;

	/*
	 * Perform some housekeeping before deciding whether to return.
	 *
	 * Set the TTY_CLOSING flag if this was the last open.  In the
	 * case of a pty we may have to wait around for the other side
	 * to close, and TTY_CLOSING makes sure we can't be reopened.
	 */
	if(tty_closing)
		set_bit(TTY_CLOSING, &tty->flags);
	if(o_tty_closing)
		set_bit(TTY_CLOSING, &o_tty->flags);

	/*
	 * If _either_ side is closing, make sure there aren't any
	 * processes that still think tty or o_tty is their controlling
	 * tty.
	 */
	if (tty_closing || o_tty_closing) {
		struct task_struct *p;

		read_lock(&tasklist_lock);
		do_each_task_pid(tty->session, PIDTYPE_SID, p) {
			p->signal->tty = NULL;
		} while_each_task_pid(tty->session, PIDTYPE_SID, p);
		if (o_tty)
			do_each_task_pid(o_tty->session, PIDTYPE_SID, p) {
				p->signal->tty = NULL;
			} while_each_task_pid(o_tty->session, PIDTYPE_SID, p);
		read_unlock(&tasklist_lock);
	}

	up(&tty_sem);

	/* check whether both sides are closing ... */
	if (!tty_closing || (o_tty && !o_tty_closing))
		return;
	
#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "freeing tty structure...");
#endif
	/*
	 * Prevent flush_to_ldisc() from rescheduling the work for later.  Then
	 * kill any delayed work. As this is the final close it does not
	 * race with the set_ldisc code path.
	 */
	clear_bit(TTY_LDISC, &tty->flags);
	clear_bit(TTY_DONT_FLIP, &tty->flags);
	cancel_delayed_work(&tty->buf.work);

	/*
	 * Wait for ->hangup_work and ->buf.work handlers to terminate
	 */
	 
	flush_scheduled_work();
	
	/*
	 * Wait for any short term users (we know they are just driver
	 * side waiters as the file is closing so user count on the file
	 * side is zero.
	 */
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	while(tty->ldisc.refcount)
	{
		spin_unlock_irqrestore(&tty_ldisc_lock, flags);
		wait_event(tty_ldisc_wait, tty->ldisc.refcount == 0);
		spin_lock_irqsave(&tty_ldisc_lock, flags);
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	/*
	 * Shutdown the current line discipline, and reset it to N_TTY.
	 * N.B. why reset ldisc when we're releasing the memory??
	 *
	 * FIXME: this MUST get fixed for the new reflocking
	 */
	if (tty->ldisc.close)
		(tty->ldisc.close)(tty);
	tty_ldisc_put(tty->ldisc.num);
	
	/*
	 *	Switch the line discipline back
	 */
	tty_ldisc_assign(tty, tty_ldisc_get(N_TTY));
	tty_set_termios_ldisc(tty,N_TTY); 
	if (o_tty) {
		/* FIXME: could o_tty be in setldisc here ? */
		clear_bit(TTY_LDISC, &o_tty->flags);
		if (o_tty->ldisc.close)
			(o_tty->ldisc.close)(o_tty);
		tty_ldisc_put(o_tty->ldisc.num);
		tty_ldisc_assign(o_tty, tty_ldisc_get(N_TTY));
		tty_set_termios_ldisc(o_tty,N_TTY); 
	}
	/*
	 * The release_mem function takes care of the details of clearing
	 * the slots and preserving the termios structure.
	 */
	release_mem(tty, idx);

#ifdef CONFIG_UNIX98_PTYS
	/* Make this pty number available for reallocation */
	if (devpts) {
		down(&allocated_ptys_lock);
		idr_remove(&allocated_ptys, idx);
		up(&allocated_ptys_lock);
	}
#endif

}

/*
 * tty_open and tty_release keep up the tty count that contains the
 * number of opens done on a tty. We cannot use the inode-count, as
 * different inodes might point to the same tty.
 *
 * Open-counting is needed for pty masters, as well as for keeping
 * track of serial lines: DTR is dropped when the last close happens.
 * (This is not done solely through tty->count, now.  - Ted 1/27/92)
 *
 * The termios state of a pty is reset on first open so that
 * settings don't persist across reuse.
 */
static int tty_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty;
	int noctty, retval;
	struct tty_driver *driver;
	int index;
	dev_t device = inode->i_rdev;
	unsigned short saved_flags = filp->f_flags;

	nonseekable_open(inode, filp);
	
retry_open:
	noctty = filp->f_flags & O_NOCTTY;
	index  = -1;
	retval = 0;
	
	down(&tty_sem);

	if (device == MKDEV(TTYAUX_MAJOR,0)) {
		if (!current->signal->tty) {
			up(&tty_sem);
			return -ENXIO;
		}
		driver = current->signal->tty->driver;
		index = current->signal->tty->index;
		filp->f_flags |= O_NONBLOCK; /* Don't let /dev/tty block */
		/* noctty = 1; */
		goto got_driver;
	}
#ifdef CONFIG_VT
	if (device == MKDEV(TTY_MAJOR,0)) {
		extern struct tty_driver *console_driver;
		driver = console_driver;
		index = fg_console;
		noctty = 1;
		goto got_driver;
	}
#endif
	if (device == MKDEV(TTYAUX_MAJOR,1)) {
		driver = console_device(&index);
		if (driver) {
			/* Don't let /dev/console block */
			filp->f_flags |= O_NONBLOCK;
			noctty = 1;
			goto got_driver;
		}
		up(&tty_sem);
		return -ENODEV;
	}

	driver = get_tty_driver(device, &index);
	if (!driver) {
		up(&tty_sem);
		return -ENODEV;
	}
got_driver:
	retval = init_dev(driver, index, &tty);
	up(&tty_sem);
	if (retval)
		return retval;

	filp->private_data = tty;
	file_move(filp, &tty->tty_files);
	check_tty_count(tty, "tty_open");
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		noctty = 1;
#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "opening %s...", tty->name);
#endif
	if (!retval) {
		if (tty->driver->open)
			retval = tty->driver->open(tty, filp);
		else
			retval = -ENODEV;
	}
	filp->f_flags = saved_flags;

	if (!retval && test_bit(TTY_EXCLUSIVE, &tty->flags) && !capable(CAP_SYS_ADMIN))
		retval = -EBUSY;

	if (retval) {
#ifdef TTY_DEBUG_HANGUP
		printk(KERN_DEBUG "error %d in opening %s...", retval,
		       tty->name);
#endif
		release_dev(filp);
		if (retval != -ERESTARTSYS)
			return retval;
		if (signal_pending(current))
			return retval;
		schedule();
		/*
		 * Need to reset f_op in case a hangup happened.
		 */
		if (filp->f_op == &hung_up_tty_fops)
			filp->f_op = &tty_fops;
		goto retry_open;
	}
	if (!noctty &&
	    current->signal->leader &&
	    !current->signal->tty &&
	    tty->session == 0) {
	    	task_lock(current);
		current->signal->tty = tty;
		task_unlock(current);
		current->signal->tty_old_pgrp = 0;
		tty->session = current->signal->session;
		tty->pgrp = process_group(current);
	}
	return 0;
}

#ifdef CONFIG_UNIX98_PTYS
static int ptmx_open(struct inode * inode, struct file * filp)
{
	struct tty_struct *tty;
	int retval;
	int index;
	int idr_ret;

	nonseekable_open(inode, filp);

	/* find a device that is not in use. */
	down(&allocated_ptys_lock);
	if (!idr_pre_get(&allocated_ptys, GFP_KERNEL)) {
		up(&allocated_ptys_lock);
		return -ENOMEM;
	}
	idr_ret = idr_get_new(&allocated_ptys, NULL, &index);
	if (idr_ret < 0) {
		up(&allocated_ptys_lock);
		if (idr_ret == -EAGAIN)
			return -ENOMEM;
		return -EIO;
	}
	if (index >= pty_limit) {
		idr_remove(&allocated_ptys, index);
		up(&allocated_ptys_lock);
		return -EIO;
	}
	up(&allocated_ptys_lock);

	down(&tty_sem);
	retval = init_dev(ptm_driver, index, &tty);
	up(&tty_sem);
	
	if (retval)
		goto out;

	set_bit(TTY_PTY_LOCK, &tty->flags); /* LOCK THE SLAVE */
	filp->private_data = tty;
	file_move(filp, &tty->tty_files);

	retval = -ENOMEM;
	if (devpts_pty_new(tty->link))
		goto out1;

	check_tty_count(tty, "tty_open");
	retval = ptm_driver->open(tty, filp);
	if (!retval)
		return 0;
out1:
	release_dev(filp);
out:
	down(&allocated_ptys_lock);
	idr_remove(&allocated_ptys, index);
	up(&allocated_ptys_lock);
	return retval;
}
#endif

static int tty_release(struct inode * inode, struct file * filp)
{
	lock_kernel();
	release_dev(filp);
	unlock_kernel();
	return 0;
}

/* No kernel lock held - fine */
static unsigned int tty_poll(struct file * filp, poll_table * wait)
{
	struct tty_struct * tty;
	struct tty_ldisc *ld;
	int ret = 0;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode, "tty_poll"))
		return 0;
		
	ld = tty_ldisc_ref_wait(tty);
	if (ld->poll)
		ret = (ld->poll)(tty, filp, wait);
	tty_ldisc_deref(ld);
	return ret;
}

static int tty_fasync(int fd, struct file * filp, int on)
{
	struct tty_struct * tty;
	int retval;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_dentry->d_inode, "tty_fasync"))
		return 0;
	
	retval = fasync_helper(fd, filp, on, &tty->fasync);
	if (retval <= 0)
		return retval;

	if (on) {
		if (!waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = 1;
		retval = f_setown(filp, (-tty->pgrp) ? : current->pid, 0);
		if (retval)
			return retval;
	} else {
		if (!tty->fasync && !waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = N_TTY_BUF_SIZE;
	}
	return 0;
}

static int tiocsti(struct tty_struct *tty, char __user *p)
{
	char ch, mbz = 0;
	struct tty_ldisc *ld;
	
	if ((current->signal->tty != tty) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (get_user(ch, p))
		return -EFAULT;
	ld = tty_ldisc_ref_wait(tty);
	ld->receive_buf(tty, &ch, &mbz, 1);
	tty_ldisc_deref(ld);
	return 0;
}

static int tiocgwinsz(struct tty_struct *tty, struct winsize __user * arg)
{
	if (copy_to_user(arg, &tty->winsize, sizeof(*arg)))
		return -EFAULT;
	return 0;
}

static int tiocswinsz(struct tty_struct *tty, struct tty_struct *real_tty,
	struct winsize __user * arg)
{
	struct winsize tmp_ws;

	if (copy_from_user(&tmp_ws, arg, sizeof(*arg)))
		return -EFAULT;
	if (!memcmp(&tmp_ws, &tty->winsize, sizeof(*arg)))
		return 0;
#ifdef CONFIG_VT
	if (tty->driver->type == TTY_DRIVER_TYPE_CONSOLE) {
		int rc;

		acquire_console_sem();
		rc = vc_resize(tty->driver_data, tmp_ws.ws_col, tmp_ws.ws_row);
		release_console_sem();
		if (rc)
			return -ENXIO;
	}
#endif
	if (tty->pgrp > 0)
		kill_pg(tty->pgrp, SIGWINCH, 1);
	if ((real_tty->pgrp != tty->pgrp) && (real_tty->pgrp > 0))
		kill_pg(real_tty->pgrp, SIGWINCH, 1);
	tty->winsize = tmp_ws;
	real_tty->winsize = tmp_ws;
	return 0;
}

static int tioccons(struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (file->f_op->write == redirected_tty_write) {
		struct file *f;
		spin_lock(&redirect_lock);
		f = redirect;
		redirect = NULL;
		spin_unlock(&redirect_lock);
		if (f)
			fput(f);
		return 0;
	}
	spin_lock(&redirect_lock);
	if (redirect) {
		spin_unlock(&redirect_lock);
		return -EBUSY;
	}
	get_file(file);
	redirect = file;
	spin_unlock(&redirect_lock);
	return 0;
}


static int fionbio(struct file *file, int __user *p)
{
	int nonblock;

	if (get_user(nonblock, p))
		return -EFAULT;

	if (nonblock)
		file->f_flags |= O_NONBLOCK;
	else
		file->f_flags &= ~O_NONBLOCK;
	return 0;
}

static int tiocsctty(struct tty_struct *tty, int arg)
{
	task_t *p;

	if (current->signal->leader &&
	    (current->signal->session == tty->session))
		return 0;
	/*
	 * The process must be a session leader and
	 * not have a controlling tty already.
	 */
	if (!current->signal->leader || current->signal->tty)
		return -EPERM;
	if (tty->session > 0) {
		/*
		 * This tty is already the controlling
		 * tty for another session group!
		 */
		if ((arg == 1) && capable(CAP_SYS_ADMIN)) {
			/*
			 * Steal it away
			 */

			read_lock(&tasklist_lock);
			do_each_task_pid(tty->session, PIDTYPE_SID, p) {
				p->signal->tty = NULL;
			} while_each_task_pid(tty->session, PIDTYPE_SID, p);
			read_unlock(&tasklist_lock);
		} else
			return -EPERM;
	}
	task_lock(current);
	current->signal->tty = tty;
	task_unlock(current);
	current->signal->tty_old_pgrp = 0;
	tty->session = current->signal->session;
	tty->pgrp = process_group(current);
	return 0;
}

static int tiocgpgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	 */
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	return put_user(real_tty->pgrp, p);
}

static int tiocspgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	pid_t pgrp;
	int retval = tty_check_change(real_tty);

	if (retval == -EIO)
		return -ENOTTY;
	if (retval)
		return retval;
	if (!current->signal->tty ||
	    (current->signal->tty != real_tty) ||
	    (real_tty->session != current->signal->session))
		return -ENOTTY;
	if (get_user(pgrp, p))
		return -EFAULT;
	if (pgrp < 0)
		return -EINVAL;
	if (session_of_pgrp(pgrp) != current->signal->session)
		return -EPERM;
	real_tty->pgrp = pgrp;
	return 0;
}

static int tiocgsid(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	*/
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	if (real_tty->session <= 0)
		return -ENOTTY;
	return put_user(real_tty->session, p);
}

static int tiocsetd(struct tty_struct *tty, int __user *p)
{
	int ldisc;

	if (get_user(ldisc, p))
		return -EFAULT;
	return tty_set_ldisc(tty, ldisc);
}

static int send_break(struct tty_struct *tty, unsigned int duration)
{
	tty->driver->break_ctl(tty, -1);
	if (!signal_pending(current)) {
		msleep_interruptible(duration);
	}
	tty->driver->break_ctl(tty, 0);
	if (signal_pending(current))
		return -EINTR;
	return 0;
}

static int
tty_tiocmget(struct tty_struct *tty, struct file *file, int __user *p)
{
	int retval = -EINVAL;

	if (tty->driver->tiocmget) {
		retval = tty->driver->tiocmget(tty, file);

		if (retval >= 0)
			retval = put_user(retval, p);
	}
	return retval;
}

static int
tty_tiocmset(struct tty_struct *tty, struct file *file, unsigned int cmd,
	     unsigned __user *p)
{
	int retval = -EINVAL;

	if (tty->driver->tiocmset) {
		unsigned int set, clear, val;

		retval = get_user(val, p);
		if (retval)
			return retval;

		set = clear = 0;
		switch (cmd) {
		case TIOCMBIS:
			set = val;
			break;
		case TIOCMBIC:
			clear = val;
			break;
		case TIOCMSET:
			set = val;
			clear = ~val;
			break;
		}

		set &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2|TIOCM_LOOP;
		clear &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2|TIOCM_LOOP;

		retval = tty->driver->tiocmset(tty, file, set, clear);
	}
	return retval;
}

/*
 * Split this up, as gcc can choke on it otherwise..
 */
int tty_ioctl(struct inode * inode, struct file * file,
	      unsigned int cmd, unsigned long arg)
{
	struct tty_struct *tty, *real_tty;
	void __user *p = (void __user *)arg;
	int retval;
	struct tty_ldisc *ld;
	
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode, "tty_ioctl"))
		return -EINVAL;

	real_tty = tty;
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		real_tty = tty->link;

	/*
	 * Break handling by driver
	 */
	if (!tty->driver->break_ctl) {
		switch(cmd) {
		case TIOCSBRK:
		case TIOCCBRK:
			if (tty->driver->ioctl)
				return tty->driver->ioctl(tty, file, cmd, arg);
			return -EINVAL;
			
		/* These two ioctl's always return success; even if */
		/* the driver doesn't support them. */
		case TCSBRK:
		case TCSBRKP:
			if (!tty->driver->ioctl)
				return 0;
			retval = tty->driver->ioctl(tty, file, cmd, arg);
			if (retval == -ENOIOCTLCMD)
				retval = 0;
			return retval;
		}
	}

	/*
	 * Factor out some common prep work
	 */
	switch (cmd) {
	case TIOCSETD:
	case TIOCSBRK:
	case TIOCCBRK:
	case TCSBRK:
	case TCSBRKP:			
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		if (cmd != TIOCCBRK) {
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
		}
		break;
	}

	switch (cmd) {
		case TIOCSTI:
			return tiocsti(tty, p);
		case TIOCGWINSZ:
			return tiocgwinsz(tty, p);
		case TIOCSWINSZ:
			return tiocswinsz(tty, real_tty, p);
		case TIOCCONS:
			return real_tty!=tty ? -EINVAL : tioccons(file);
		case FIONBIO:
			return fionbio(file, p);
		case TIOCEXCL:
			set_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNXCL:
			clear_bit(TTY_EXCLUSIVE, &tty->flags);
			return 0;
		case TIOCNOTTY:
			if (current->signal->tty != tty)
				return -ENOTTY;
			if (current->signal->leader)
				disassociate_ctty(0);
			task_lock(current);
			current->signal->tty = NULL;
			task_unlock(current);
			return 0;
		case TIOCSCTTY:
			return tiocsctty(tty, arg);
		case TIOCGPGRP:
			return tiocgpgrp(tty, real_tty, p);
		case TIOCSPGRP:
			return tiocspgrp(tty, real_tty, p);
		case TIOCGSID:
			return tiocgsid(tty, real_tty, p);
		case TIOCGETD:
			/* FIXME: check this is ok */
			return put_user(tty->ldisc.num, (int __user *)p);
		case TIOCSETD:
			return tiocsetd(tty, p);
#ifdef CONFIG_VT
		case TIOCLINUX:
			return tioclinux(tty, arg);
#endif
		/*
		 * Break handling
		 */
		case TIOCSBRK:	/* Turn break on, unconditionally */
			tty->driver->break_ctl(tty, -1);
			return 0;
			
		case TIOCCBRK:	/* Turn break off, unconditionally */
			tty->driver->break_ctl(tty, 0);
			return 0;
		case TCSBRK:   /* SVID version: non-zero arg --> no break */
			/*
			 * XXX is the above comment correct, or the
			 * code below correct?  Is this ioctl used at
			 * all by anyone?
			 */
			if (!arg)
				return send_break(tty, 250);
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */	
			return send_break(tty, arg ? arg*100 : 250);

		case TIOCMGET:
			return tty_tiocmget(tty, file, p);

		case TIOCMSET:
		case TIOCMBIC:
		case TIOCMBIS:
			return tty_tiocmset(tty, file, cmd, p);
	}
	if (tty->driver->ioctl) {
		retval = (tty->driver->ioctl)(tty, file, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}
	ld = tty_ldisc_ref_wait(tty);
	retval = -EINVAL;
	if (ld->ioctl) {
		retval = ld->ioctl(tty, file, cmd, arg);
		if (retval == -ENOIOCTLCMD)
			retval = -EINVAL;
	}
	tty_ldisc_deref(ld);
	return retval;
}


/*
 * This implements the "Secure Attention Key" ---  the idea is to
 * prevent trojan horses by killing all processes associated with this
 * tty when the user hits the "Secure Attention Key".  Required for
 * super-paranoid applications --- see the Orange Book for more details.
 * 
 * This code could be nicer; ideally it should send a HUP, wait a few
 * seconds, then send a INT, and then a KILL signal.  But you then
 * have to coordinate with the init process, since all processes associated
 * with the current tty must be dead before the new getty is allowed
 * to spawn.
 *
 * Now, if it would be correct ;-/ The current code has a nasty hole -
 * it doesn't catch files in flight. We may send the descriptor to ourselves
 * via AF_UNIX socket, close it and later fetch from socket. FIXME.
 *
 * Nasty bug: do_SAK is being called in interrupt context.  This can
 * deadlock.  We punt it up to process context.  AKPM - 16Mar2001
 */
static void __do_SAK(void *arg)
{
#ifdef TTY_SOFT_SAK
	tty_hangup(tty);
#else
	struct tty_struct *tty = arg;
	struct task_struct *p;
	int session;
	int		i;
	struct file	*filp;
	struct tty_ldisc *disc;
	struct fdtable *fdt;
	
	if (!tty)
		return;
	session  = tty->session;
	
	/* We don't want an ldisc switch during this */
	disc = tty_ldisc_ref(tty);
	if (disc && disc->flush_buffer)
		disc->flush_buffer(tty);
	tty_ldisc_deref(disc);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	
	read_lock(&tasklist_lock);
	do_each_task_pid(session, PIDTYPE_SID, p) {
		if (p->signal->tty == tty || session > 0) {
			printk(KERN_NOTICE "SAK: killed process %d"
			    " (%s): p->signal->session==tty->session\n",
			    p->pid, p->comm);
			send_sig(SIGKILL, p, 1);
			continue;
		}
		task_lock(p);
		if (p->files) {
			rcu_read_lock();
			fdt = files_fdtable(p->files);
			for (i=0; i < fdt->max_fds; i++) {
				filp = fcheck_files(p->files, i);
				if (!filp)
					continue;
				if (filp->f_op->read == tty_read &&
				    filp->private_data == tty) {
					printk(KERN_NOTICE "SAK: killed process %d"
					    " (%s): fd#%d opened to the tty\n",
					    p->pid, p->comm, i);
					send_sig(SIGKILL, p, 1);
					break;
				}
			}
			rcu_read_unlock();
		}
		task_unlock(p);
	} while_each_task_pid(session, PIDTYPE_SID, p);
	read_unlock(&tasklist_lock);
#endif
}

/*
 * The tq handling here is a little racy - tty->SAK_work may already be queued.
 * Fortunately we don't need to worry, because if ->SAK_work is already queued,
 * the values which we write to it will be identical to the values which it
 * already has. --akpm
 */
void do_SAK(struct tty_struct *tty)
{
	if (!tty)
		return;
	PREPARE_WORK(&tty->SAK_work, __do_SAK, tty);
	schedule_work(&tty->SAK_work);
}

EXPORT_SYMBOL(do_SAK);

/*
 * This routine is called out of the software interrupt to flush data
 * from the buffer chain to the line discipline.
 */
 
static void flush_to_ldisc(void *private_)
{
	struct tty_struct *tty = (struct tty_struct *) private_;
	unsigned long 	flags;
	struct tty_ldisc *disc;
	struct tty_buffer *tbuf;
	int count;
	char *char_buf;
	unsigned char *flag_buf;

	disc = tty_ldisc_ref(tty);
	if (disc == NULL)	/*  !TTY_LDISC */
		return;

	if (test_bit(TTY_DONT_FLIP, &tty->flags)) {
		/*
		 * Do it after the next timer tick:
		 */
		schedule_delayed_work(&tty->buf.work, 1);
		goto out;
	}
	spin_lock_irqsave(&tty->buf.lock, flags);
	while((tbuf = tty->buf.head) != NULL) {
		while ((count = tbuf->commit - tbuf->read) != 0) {
			char_buf = tbuf->char_buf_ptr + tbuf->read;
			flag_buf = tbuf->flag_buf_ptr + tbuf->read;
			tbuf->read += count;
			spin_unlock_irqrestore(&tty->buf.lock, flags);
			disc->receive_buf(tty, char_buf, flag_buf, count);
			spin_lock_irqsave(&tty->buf.lock, flags);
		}
		if (tbuf->active)
			break;
		tty->buf.head = tbuf->next;
		if (tty->buf.head == NULL)
			tty->buf.tail = NULL;
		tty_buffer_free(tty, tbuf);
	}
	spin_unlock_irqrestore(&tty->buf.lock, flags);
out:
	tty_ldisc_deref(disc);
}

/*
 * Routine which returns the baud rate of the tty
 *
 * Note that the baud_table needs to be kept in sync with the
 * include/asm/termbits.h file.
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800,
#ifdef __sparc__
	76800, 153600, 307200, 614400, 921600
#else
	500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
#endif
};

static int n_baud_table = ARRAY_SIZE(baud_table);

/**
 *	tty_termios_baud_rate
 *	@termios: termios structure
 *
 *	Convert termios baud rate data into a speed. This should be called
 *	with the termios lock held if this termios is a terminal termios
 *	structure. May change the termios data.
 */
 
int tty_termios_baud_rate(struct termios *termios)
{
	unsigned int cbaud;
	
	cbaud = termios->c_cflag & CBAUD;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~CBAUDEX;
		else
			cbaud += 15;
	}
	return baud_table[cbaud];
}

EXPORT_SYMBOL(tty_termios_baud_rate);

/**
 *	tty_get_baud_rate	-	get tty bit rates
 *	@tty: tty to query
 *
 *	Returns the baud rate as an integer for this terminal. The
 *	termios lock must be held by the caller and the terminal bit
 *	flags may be updated.
 */
 
int tty_get_baud_rate(struct tty_struct *tty)
{
	int baud = tty_termios_baud_rate(tty->termios);

	if (baud == 38400 && tty->alt_speed) {
		if (!tty->warned) {
			printk(KERN_WARNING "Use of setserial/setrocket to "
					    "set SPD_* flags is deprecated\n");
			tty->warned = 1;
		}
		baud = tty->alt_speed;
	}
	
	return baud;
}

EXPORT_SYMBOL(tty_get_baud_rate);

/**
 *	tty_flip_buffer_push	-	terminal
 *	@tty: tty to push
 *
 *	Queue a push of the terminal flip buffers to the line discipline. This
 *	function must not be called from IRQ context if tty->low_latency is set.
 *
 *	In the event of the queue being busy for flipping the work will be
 *	held off and retried later.
 */

void tty_flip_buffer_push(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->buf.lock, flags);
	if (tty->buf.tail != NULL) {
		tty->buf.tail->active = 0;
		tty->buf.tail->commit = tty->buf.tail->used;
	}
	spin_unlock_irqrestore(&tty->buf.lock, flags);

	if (tty->low_latency)
		flush_to_ldisc((void *) tty);
	else
		schedule_delayed_work(&tty->buf.work, 1);
}

EXPORT_SYMBOL(tty_flip_buffer_push);


/*
 * This subroutine initializes a tty structure.
 */
static void initialize_tty_struct(struct tty_struct *tty)
{
	memset(tty, 0, sizeof(struct tty_struct));
	tty->magic = TTY_MAGIC;
	tty_ldisc_assign(tty, tty_ldisc_get(N_TTY));
	tty->pgrp = -1;
	tty->overrun_time = jiffies;
	tty->buf.head = tty->buf.tail = NULL;
	tty_buffer_init(tty);
	INIT_WORK(&tty->buf.work, flush_to_ldisc, tty);
	init_MUTEX(&tty->buf.pty_sem);
	init_MUTEX(&tty->termios_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup, tty);
	sema_init(&tty->atomic_read, 1);
	sema_init(&tty->atomic_write, 1);
	spin_lock_init(&tty->read_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	INIT_WORK(&tty->SAK_work, NULL, NULL);
}

/*
 * The default put_char routine if the driver did not define one.
 */
static void tty_default_put_char(struct tty_struct *tty, unsigned char ch)
{
	tty->driver->write(tty, &ch, 1);
}

static struct class *tty_class;

/**
 * tty_register_device - register a tty device
 * @driver: the tty driver that describes the tty device
 * @index: the index in the tty driver for this tty device
 * @device: a struct device that is associated with this tty device.
 *	This field is optional, if there is no known struct device for this
 *	tty device it can be set to NULL safely.
 *
 * This call is required to be made to register an individual tty device if
 * the tty driver's flags have the TTY_DRIVER_NO_DEVFS bit set.  If that
 * bit is not set, this function should not be called.
 */
void tty_register_device(struct tty_driver *driver, unsigned index,
			 struct device *device)
{
	char name[64];
	dev_t dev = MKDEV(driver->major, driver->minor_start) + index;

	if (index >= driver->num) {
		printk(KERN_ERR "Attempt to register invalid tty line number "
		       " (%d).\n", index);
		return;
	}

	devfs_mk_cdev(dev, S_IFCHR | S_IRUSR | S_IWUSR,
			"%s%d", driver->devfs_name, index + driver->name_base);

	if (driver->type == TTY_DRIVER_TYPE_PTY)
		pty_line_name(driver, index, name);
	else
		tty_line_name(driver, index, name);
	class_device_create(tty_class, NULL, dev, device, "%s", name);
}

/**
 * tty_unregister_device - unregister a tty device
 * @driver: the tty driver that describes the tty device
 * @index: the index in the tty driver for this tty device
 *
 * If a tty device is registered with a call to tty_register_device() then
 * this function must be made when the tty device is gone.
 */
void tty_unregister_device(struct tty_driver *driver, unsigned index)
{
	devfs_remove("%s%d", driver->devfs_name, index + driver->name_base);
	class_device_destroy(tty_class, MKDEV(driver->major, driver->minor_start) + index);
}

EXPORT_SYMBOL(tty_register_device);
EXPORT_SYMBOL(tty_unregister_device);

struct tty_driver *alloc_tty_driver(int lines)
{
	struct tty_driver *driver;

	driver = kmalloc(sizeof(struct tty_driver), GFP_KERNEL);
	if (driver) {
		memset(driver, 0, sizeof(struct tty_driver));
		driver->magic = TTY_DRIVER_MAGIC;
		driver->num = lines;
		/* later we'll move allocation of tables here */
	}
	return driver;
}

void put_tty_driver(struct tty_driver *driver)
{
	kfree(driver);
}

void tty_set_operations(struct tty_driver *driver, struct tty_operations *op)
{
	driver->open = op->open;
	driver->close = op->close;
	driver->write = op->write;
	driver->put_char = op->put_char;
	driver->flush_chars = op->flush_chars;
	driver->write_room = op->write_room;
	driver->chars_in_buffer = op->chars_in_buffer;
	driver->ioctl = op->ioctl;
	driver->set_termios = op->set_termios;
	driver->throttle = op->throttle;
	driver->unthrottle = op->unthrottle;
	driver->stop = op->stop;
	driver->start = op->start;
	driver->hangup = op->hangup;
	driver->break_ctl = op->break_ctl;
	driver->flush_buffer = op->flush_buffer;
	driver->set_ldisc = op->set_ldisc;
	driver->wait_until_sent = op->wait_until_sent;
	driver->send_xchar = op->send_xchar;
	driver->read_proc = op->read_proc;
	driver->write_proc = op->write_proc;
	driver->tiocmget = op->tiocmget;
	driver->tiocmset = op->tiocmset;
}


EXPORT_SYMBOL(alloc_tty_driver);
EXPORT_SYMBOL(put_tty_driver);
EXPORT_SYMBOL(tty_set_operations);

/*
 * Called by a tty driver to register itself.
 */
int tty_register_driver(struct tty_driver *driver)
{
	int error;
        int i;
	dev_t dev;
	void **p = NULL;

	if (driver->flags & TTY_DRIVER_INSTALLED)
		return 0;

	if (!(driver->flags & TTY_DRIVER_DEVPTS_MEM)) {
		p = kmalloc(driver->num * 3 * sizeof(void *), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		memset(p, 0, driver->num * 3 * sizeof(void *));
	}

	if (!driver->major) {
		error = alloc_chrdev_region(&dev, driver->minor_start, driver->num,
						(char*)driver->name);
		if (!error) {
			driver->major = MAJOR(dev);
			driver->minor_start = MINOR(dev);
		}
	} else {
		dev = MKDEV(driver->major, driver->minor_start);
		error = register_chrdev_region(dev, driver->num,
						(char*)driver->name);
	}
	if (error < 0) {
		kfree(p);
		return error;
	}

	if (p) {
		driver->ttys = (struct tty_struct **)p;
		driver->termios = (struct termios **)(p + driver->num);
		driver->termios_locked = (struct termios **)(p + driver->num * 2);
	} else {
		driver->ttys = NULL;
		driver->termios = NULL;
		driver->termios_locked = NULL;
	}

	cdev_init(&driver->cdev, &tty_fops);
	driver->cdev.owner = driver->owner;
	error = cdev_add(&driver->cdev, dev, driver->num);
	if (error) {
		cdev_del(&driver->cdev);
		unregister_chrdev_region(dev, driver->num);
		driver->ttys = NULL;
		driver->termios = driver->termios_locked = NULL;
		kfree(p);
		return error;
	}

	if (!driver->put_char)
		driver->put_char = tty_default_put_char;
	
	list_add(&driver->tty_drivers, &tty_drivers);
	
	if ( !(driver->flags & TTY_DRIVER_NO_DEVFS) ) {
		for(i = 0; i < driver->num; i++)
		    tty_register_device(driver, i, NULL);
	}
	proc_tty_register_driver(driver);
	return 0;
}

EXPORT_SYMBOL(tty_register_driver);

/*
 * Called by a tty driver to unregister itself.
 */
int tty_unregister_driver(struct tty_driver *driver)
{
	int i;
	struct termios *tp;
	void *p;

	if (driver->refcount)
		return -EBUSY;

	unregister_chrdev_region(MKDEV(driver->major, driver->minor_start),
				driver->num);

	list_del(&driver->tty_drivers);

	/*
	 * Free the termios and termios_locked structures because
	 * we don't want to get memory leaks when modular tty
	 * drivers are removed from the kernel.
	 */
	for (i = 0; i < driver->num; i++) {
		tp = driver->termios[i];
		if (tp) {
			driver->termios[i] = NULL;
			kfree(tp);
		}
		tp = driver->termios_locked[i];
		if (tp) {
			driver->termios_locked[i] = NULL;
			kfree(tp);
		}
		if (!(driver->flags & TTY_DRIVER_NO_DEVFS))
			tty_unregister_device(driver, i);
	}
	p = driver->ttys;
	proc_tty_unregister_driver(driver);
	driver->ttys = NULL;
	driver->termios = driver->termios_locked = NULL;
	kfree(p);
	cdev_del(&driver->cdev);
	return 0;
}

EXPORT_SYMBOL(tty_unregister_driver);


/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
void __init console_init(void)
{
	initcall_t *call;

	/* Setup the default TTY line discipline. */
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);

	/*
	 * set up the console device so that later boot sequences can 
	 * inform about problems etc..
	 */
#ifdef CONFIG_EARLY_PRINTK
	disable_early_printk();
#endif
	call = __con_initcall_start;
	while (call < __con_initcall_end) {
		(*call)();
		call++;
	}
}

#ifdef CONFIG_VT
extern int vty_init(void);
#endif

static int __init tty_class_init(void)
{
	tty_class = class_create(THIS_MODULE, "tty");
	if (IS_ERR(tty_class))
		return PTR_ERR(tty_class);
	return 0;
}

postcore_initcall(tty_class_init);

/* 3/2004 jmc: why do these devices exist? */

static struct cdev tty_cdev, console_cdev;
#ifdef CONFIG_UNIX98_PTYS
static struct cdev ptmx_cdev;
#endif
#ifdef CONFIG_VT
static struct cdev vc0_cdev;
#endif

/*
 * Ok, now we can initialize the rest of the tty devices and can count
 * on memory allocations, interrupts etc..
 */
static int __init tty_init(void)
{
	cdev_init(&tty_cdev, &tty_fops);
	if (cdev_add(&tty_cdev, MKDEV(TTYAUX_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 0), 1, "/dev/tty") < 0)
		panic("Couldn't register /dev/tty driver\n");
	devfs_mk_cdev(MKDEV(TTYAUX_MAJOR, 0), S_IFCHR|S_IRUGO|S_IWUGO, "tty");
	class_device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL, "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") < 0)
		panic("Couldn't register /dev/console driver\n");
	devfs_mk_cdev(MKDEV(TTYAUX_MAJOR, 1), S_IFCHR|S_IRUSR|S_IWUSR, "console");
	class_device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 1), NULL, "console");

#ifdef CONFIG_UNIX98_PTYS
	cdev_init(&ptmx_cdev, &ptmx_fops);
	if (cdev_add(&ptmx_cdev, MKDEV(TTYAUX_MAJOR, 2), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 2), 1, "/dev/ptmx") < 0)
		panic("Couldn't register /dev/ptmx driver\n");
	devfs_mk_cdev(MKDEV(TTYAUX_MAJOR, 2), S_IFCHR|S_IRUGO|S_IWUGO, "ptmx");
	class_device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 2), NULL, "ptmx");
#endif

#ifdef CONFIG_VT
	cdev_init(&vc0_cdev, &console_fops);
	if (cdev_add(&vc0_cdev, MKDEV(TTY_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTY_MAJOR, 0), 1, "/dev/vc/0") < 0)
		panic("Couldn't register /dev/tty0 driver\n");
	devfs_mk_cdev(MKDEV(TTY_MAJOR, 0), S_IFCHR|S_IRUSR|S_IWUSR, "vc/0");
	class_device_create(tty_class, NULL, MKDEV(TTY_MAJOR, 0), NULL, "tty0");

	vty_init();
#endif
	return 0;
}
module_init(tty_init);
