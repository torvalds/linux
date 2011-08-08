/*
 * Tty buffer allocation management
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

/**
 *	tty_buffer_free_all		-	free buffers used by a tty
 *	@tty: tty to free from
 *
 *	Remove all the buffers pending on a tty whether queued with data
 *	or in the free ring. Must be called when the tty is no longer in use
 *
 *	Locking: none
 */

void tty_buffer_free_all(struct tty_struct *tty)
{
	struct tty_buffer *thead;
	while ((thead = tty->buf.head) != NULL) {
		tty->buf.head = thead->next;
		kfree(thead);
	}
	while ((thead = tty->buf.free) != NULL) {
		tty->buf.free = thead->next;
		kfree(thead);
	}
	tty->buf.tail = NULL;
	tty->buf.memory_used = 0;
}

/**
 *	tty_buffer_alloc	-	allocate a tty buffer
 *	@tty: tty device
 *	@size: desired size (characters)
 *
 *	Allocate a new tty buffer to hold the desired number of characters.
 *	Return NULL if out of memory or the allocation would exceed the
 *	per device queue
 *
 *	Locking: Caller must hold tty->buf.lock
 */

static struct tty_buffer *tty_buffer_alloc(struct tty_struct *tty, size_t size)
{
	struct tty_buffer *p;

	if (tty->buf.memory_used + size > 65536)
		return NULL;
	p = kmalloc(sizeof(struct tty_buffer) + 2 * size, GFP_ATOMIC);
	if (p == NULL)
		return NULL;
	p->used = 0;
	p->size = size;
	p->next = NULL;
	p->commit = 0;
	p->read = 0;
	p->char_buf_ptr = (char *)(p->data);
	p->flag_buf_ptr = (unsigned char *)p->char_buf_ptr + size;
	tty->buf.memory_used += size;
	return p;
}

/**
 *	tty_buffer_free		-	free a tty buffer
 *	@tty: tty owning the buffer
 *	@b: the buffer to free
 *
 *	Free a tty buffer, or add it to the free list according to our
 *	internal strategy
 *
 *	Locking: Caller must hold tty->buf.lock
 */

static void tty_buffer_free(struct tty_struct *tty, struct tty_buffer *b)
{
	/* Dumb strategy for now - should keep some stats */
	tty->buf.memory_used -= b->size;
	WARN_ON(tty->buf.memory_used < 0);

	if (b->size >= 512)
		kfree(b);
	else {
		b->next = tty->buf.free;
		tty->buf.free = b;
	}
}

/**
 *	__tty_buffer_flush		-	flush full tty buffers
 *	@tty: tty to flush
 *
 *	flush all the buffers containing receive data. Caller must
 *	hold the buffer lock and must have ensured no parallel flush to
 *	ldisc is running.
 *
 *	Locking: Caller must hold tty->buf.lock
 */

static void __tty_buffer_flush(struct tty_struct *tty)
{
	struct tty_buffer *thead;

	while ((thead = tty->buf.head) != NULL) {
		tty->buf.head = thead->next;
		tty_buffer_free(tty, thead);
	}
	tty->buf.tail = NULL;
}

/**
 *	tty_buffer_flush		-	flush full tty buffers
 *	@tty: tty to flush
 *
 *	flush all the buffers containing receive data. If the buffer is
 *	being processed by flush_to_ldisc then we defer the processing
 *	to that function
 *
 *	Locking: none
 */

void tty_buffer_flush(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->buf.lock, flags);

	/* If the data is being pushed to the tty layer then we can't
	   process it here. Instead set a flag and the flush_to_ldisc
	   path will process the flush request before it exits */
	if (test_bit(TTY_FLUSHING, &tty->flags)) {
		set_bit(TTY_FLUSHPENDING, &tty->flags);
		spin_unlock_irqrestore(&tty->buf.lock, flags);
		wait_event(tty->read_wait,
				test_bit(TTY_FLUSHPENDING, &tty->flags) == 0);
		return;
	} else
		__tty_buffer_flush(tty);
	spin_unlock_irqrestore(&tty->buf.lock, flags);
}

/**
 *	tty_buffer_find		-	find a free tty buffer
 *	@tty: tty owning the buffer
 *	@size: characters wanted
 *
 *	Locate an existing suitable tty buffer or if we are lacking one then
 *	allocate a new one. We round our buffers off in 256 character chunks
 *	to get better allocation behaviour.
 *
 *	Locking: Caller must hold tty->buf.lock
 */

static struct tty_buffer *tty_buffer_find(struct tty_struct *tty, size_t size)
{
	struct tty_buffer **tbh = &tty->buf.free;
	while ((*tbh) != NULL) {
		struct tty_buffer *t = *tbh;
		if (t->size >= size) {
			*tbh = t->next;
			t->next = NULL;
			t->used = 0;
			t->commit = 0;
			t->read = 0;
			tty->buf.memory_used += t->size;
			return t;
		}
		tbh = &((*tbh)->next);
	}
	/* Round the buffer size out */
	size = (size + 0xFF) & ~0xFF;
	return tty_buffer_alloc(tty, size);
	/* Should possibly check if this fails for the largest buffer we
	   have queued and recycle that ? */
}

/**
 *	tty_buffer_request_room		-	grow tty buffer if needed
 *	@tty: tty structure
 *	@size: size desired
 *
 *	Make at least size bytes of linear space available for the tty
 *	buffer. If we fail return the size we managed to find.
 *
 *	Locking: Takes tty->buf.lock
 */
int tty_buffer_request_room(struct tty_struct *tty, size_t size)
{
	struct tty_buffer *b, *n;
	int left;
	unsigned long flags;

	spin_lock_irqsave(&tty->buf.lock, flags);

	/* OPTIMISATION: We could keep a per tty "zero" sized buffer to
	   remove this conditional if its worth it. This would be invisible
	   to the callers */
	if ((b = tty->buf.tail) != NULL)
		left = b->size - b->used;
	else
		left = 0;

	if (left < size) {
		/* This is the slow path - looking for new buffers to use */
		if ((n = tty_buffer_find(tty, size)) != NULL) {
			if (b != NULL) {
				b->next = n;
				b->commit = b->used;
			} else
				tty->buf.head = n;
			tty->buf.tail = n;
		} else
			size = left;
	}

	spin_unlock_irqrestore(&tty->buf.lock, flags);
	return size;
}
EXPORT_SYMBOL_GPL(tty_buffer_request_room);

/**
 *	tty_insert_flip_string_fixed_flag - Add characters to the tty buffer
 *	@tty: tty structure
 *	@chars: characters
 *	@flag: flag value for each character
 *	@size: size
 *
 *	Queue a series of bytes to the tty buffering. All the characters
 *	passed are marked with the supplied flag. Returns the number added.
 *
 *	Locking: Called functions may take tty->buf.lock
 */

int tty_insert_flip_string_fixed_flag(struct tty_struct *tty,
		const unsigned char *chars, char flag, size_t size)
{
	int copied = 0;
	do {
		int goal = min_t(size_t, size - copied, TTY_BUFFER_PAGE);
		int space = tty_buffer_request_room(tty, goal);
		struct tty_buffer *tb = tty->buf.tail;
		/* If there is no space then tb may be NULL */
		if (unlikely(space == 0))
			break;
		memcpy(tb->char_buf_ptr + tb->used, chars, space);
		memset(tb->flag_buf_ptr + tb->used, flag, space);
		tb->used += space;
		copied += space;
		chars += space;
		/* There is a small chance that we need to split the data over
		   several buffers. If this is the case we must loop */
	} while (unlikely(size > copied));
	return copied;
}
EXPORT_SYMBOL(tty_insert_flip_string_fixed_flag);

/**
 *	tty_insert_flip_string_flags	-	Add characters to the tty buffer
 *	@tty: tty structure
 *	@chars: characters
 *	@flags: flag bytes
 *	@size: size
 *
 *	Queue a series of bytes to the tty buffering. For each character
 *	the flags array indicates the status of the character. Returns the
 *	number added.
 *
 *	Locking: Called functions may take tty->buf.lock
 */

int tty_insert_flip_string_flags(struct tty_struct *tty,
		const unsigned char *chars, const char *flags, size_t size)
{
	int copied = 0;
	do {
		int goal = min_t(size_t, size - copied, TTY_BUFFER_PAGE);
		int space = tty_buffer_request_room(tty, goal);
		struct tty_buffer *tb = tty->buf.tail;
		/* If there is no space then tb may be NULL */
		if (unlikely(space == 0))
			break;
		memcpy(tb->char_buf_ptr + tb->used, chars, space);
		memcpy(tb->flag_buf_ptr + tb->used, flags, space);
		tb->used += space;
		copied += space;
		chars += space;
		flags += space;
		/* There is a small chance that we need to split the data over
		   several buffers. If this is the case we must loop */
	} while (unlikely(size > copied));
	return copied;
}
EXPORT_SYMBOL(tty_insert_flip_string_flags);

/**
 *	tty_schedule_flip	-	push characters to ldisc
 *	@tty: tty to push from
 *
 *	Takes any pending buffers and transfers their ownership to the
 *	ldisc side of the queue. It then schedules those characters for
 *	processing by the line discipline.
 *
 *	Locking: Takes tty->buf.lock
 */

void tty_schedule_flip(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->buf.lock, flags);
	if (tty->buf.tail != NULL)
		tty->buf.tail->commit = tty->buf.tail->used;
	spin_unlock_irqrestore(&tty->buf.lock, flags);
	schedule_work(&tty->buf.work);
}
EXPORT_SYMBOL(tty_schedule_flip);

/**
 *	tty_prepare_flip_string		-	make room for characters
 *	@tty: tty
 *	@chars: return pointer for character write area
 *	@size: desired size
 *
 *	Prepare a block of space in the buffer for data. Returns the length
 *	available and buffer pointer to the space which is now allocated and
 *	accounted for as ready for normal characters. This is used for drivers
 *	that need their own block copy routines into the buffer. There is no
 *	guarantee the buffer is a DMA target!
 *
 *	Locking: May call functions taking tty->buf.lock
 */

int tty_prepare_flip_string(struct tty_struct *tty, unsigned char **chars,
								size_t size)
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

/**
 *	tty_prepare_flip_string_flags	-	make room for characters
 *	@tty: tty
 *	@chars: return pointer for character write area
 *	@flags: return pointer for status flag write area
 *	@size: desired size
 *
 *	Prepare a block of space in the buffer for data. Returns the length
 *	available and buffer pointer to the space which is now allocated and
 *	accounted for as ready for characters. This is used for drivers
 *	that need their own block copy routines into the buffer. There is no
 *	guarantee the buffer is a DMA target!
 *
 *	Locking: May call functions taking tty->buf.lock
 */

int tty_prepare_flip_string_flags(struct tty_struct *tty,
			unsigned char **chars, char **flags, size_t size)
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



/**
 *	flush_to_ldisc
 *	@work: tty structure passed from work queue.
 *
 *	This routine is called out of the software interrupt to flush data
 *	from the buffer chain to the line discipline.
 *
 *	Locking: holds tty->buf.lock to guard buffer list. Drops the lock
 *	while invoking the line discipline receive_buf method. The
 *	receive_buf method is single threaded for each tty instance.
 */

static void flush_to_ldisc(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, buf.work);
	unsigned long 	flags;
	struct tty_ldisc *disc;

	disc = tty_ldisc_ref(tty);
	if (disc == NULL)	/*  !TTY_LDISC */
		return;

	spin_lock_irqsave(&tty->buf.lock, flags);

	if (!test_and_set_bit(TTY_FLUSHING, &tty->flags)) {
		struct tty_buffer *head;
		while ((head = tty->buf.head) != NULL) {
			int count;
			char *char_buf;
			unsigned char *flag_buf;

			count = head->commit - head->read;
			if (!count) {
				if (head->next == NULL)
					break;
				tty->buf.head = head->next;
				tty_buffer_free(tty, head);
				continue;
			}
			/* Ldisc or user is trying to flush the buffers
			   we are feeding to the ldisc, stop feeding the
			   line discipline as we want to empty the queue */
			if (test_bit(TTY_FLUSHPENDING, &tty->flags))
				break;
			if (!tty->receive_room)
				break;
			if (count > tty->receive_room)
				count = tty->receive_room;
			char_buf = head->char_buf_ptr + head->read;
			flag_buf = head->flag_buf_ptr + head->read;
			head->read += count;
			spin_unlock_irqrestore(&tty->buf.lock, flags);
			disc->ops->receive_buf(tty, char_buf,
							flag_buf, count);
			spin_lock_irqsave(&tty->buf.lock, flags);
		}
		clear_bit(TTY_FLUSHING, &tty->flags);
	}

	/* We may have a deferred request to flush the input buffer,
	   if so pull the chain under the lock and empty the queue */
	if (test_bit(TTY_FLUSHPENDING, &tty->flags)) {
		__tty_buffer_flush(tty);
		clear_bit(TTY_FLUSHPENDING, &tty->flags);
		wake_up(&tty->read_wait);
	}
	spin_unlock_irqrestore(&tty->buf.lock, flags);

	tty_ldisc_deref(disc);
}

/**
 *	tty_flush_to_ldisc
 *	@tty: tty to push
 *
 *	Push the terminal flip buffers to the line discipline.
 *
 *	Must not be called from IRQ context.
 */
void tty_flush_to_ldisc(struct tty_struct *tty)
{
	flush_work(&tty->buf.work);
}

/**
 *	tty_flip_buffer_push	-	terminal
 *	@tty: tty to push
 *
 *	Queue a push of the terminal flip buffers to the line discipline. This
 *	function must not be called from IRQ context if tty->low_latency is set.
 *
 *	In the event of the queue being busy for flipping the work will be
 *	held off and retried later.
 *
 *	Locking: tty buffer lock. Driver locks in low latency mode.
 */

void tty_flip_buffer_push(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->buf.lock, flags);
	if (tty->buf.tail != NULL)
		tty->buf.tail->commit = tty->buf.tail->used;
	spin_unlock_irqrestore(&tty->buf.lock, flags);

	if (tty->low_latency)
		flush_to_ldisc(&tty->buf.work);
	else
		schedule_work(&tty->buf.work);
}
EXPORT_SYMBOL(tty_flip_buffer_push);

/**
 *	tty_buffer_init		-	prepare a tty buffer structure
 *	@tty: tty to initialise
 *
 *	Set up the initial state of the buffer management for a tty device.
 *	Must be called before the other tty buffer functions are used.
 *
 *	Locking: none
 */

void tty_buffer_init(struct tty_struct *tty)
{
	spin_lock_init(&tty->buf.lock);
	tty->buf.head = NULL;
	tty->buf.tail = NULL;
	tty->buf.free = NULL;
	tty->buf.memory_used = 0;
	INIT_WORK(&tty->buf.work, flush_to_ldisc);
}

