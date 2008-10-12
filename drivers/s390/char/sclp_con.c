/*
 *  drivers/s390/char/sclp_con.c
 *    SCLP line mode console driver
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kmod.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/bootmem.h>
#include <linux/termios.h>
#include <linux/err.h>
#include <linux/reboot.h>

#include "sclp.h"
#include "sclp_rw.h"
#include "sclp_tty.h"

#define sclp_console_major 4		/* TTYAUX_MAJOR */
#define sclp_console_minor 64
#define sclp_console_name  "ttyS"

/* Lock to guard over changes to global variables */
static spinlock_t sclp_con_lock;
/* List of free pages that can be used for console output buffering */
static struct list_head sclp_con_pages;
/* List of full struct sclp_buffer structures ready for output */
static struct list_head sclp_con_outqueue;
/* Counter how many buffers are emitted (max 1) and how many */
/* are on the output queue. */
static int sclp_con_buffer_count;
/* Pointer to current console buffer */
static struct sclp_buffer *sclp_conbuf;
/* Timer for delayed output of console messages */
static struct timer_list sclp_con_timer;

/* Output format for console messages */
static unsigned short sclp_con_columns;
static unsigned short sclp_con_width_htab;

static void
sclp_conbuf_callback(struct sclp_buffer *buffer, int rc)
{
	unsigned long flags;
	void *page;

	do {
		page = sclp_unmake_buffer(buffer);
		spin_lock_irqsave(&sclp_con_lock, flags);
		/* Remove buffer from outqueue */
		list_del(&buffer->list);
		sclp_con_buffer_count--;
		list_add_tail((struct list_head *) page, &sclp_con_pages);
		/* Check if there is a pending buffer on the out queue. */
		buffer = NULL;
		if (!list_empty(&sclp_con_outqueue))
			buffer = list_entry(sclp_con_outqueue.next,
					    struct sclp_buffer, list);
		spin_unlock_irqrestore(&sclp_con_lock, flags);
	} while (buffer && sclp_emit_buffer(buffer, sclp_conbuf_callback));
}

static void
sclp_conbuf_emit(void)
{
	struct sclp_buffer* buffer;
	unsigned long flags;
	int count;
	int rc;

	spin_lock_irqsave(&sclp_con_lock, flags);
	buffer = sclp_conbuf;
	sclp_conbuf = NULL;
	if (buffer == NULL) {
		spin_unlock_irqrestore(&sclp_con_lock, flags);
		return;
	}
	list_add_tail(&buffer->list, &sclp_con_outqueue);
	count = sclp_con_buffer_count++;
	spin_unlock_irqrestore(&sclp_con_lock, flags);
	if (count)
		return;
	rc = sclp_emit_buffer(buffer, sclp_conbuf_callback);
	if (rc)
		sclp_conbuf_callback(buffer, rc);
}

/*
 * When this routine is called from the timer then we flush the
 * temporary write buffer without further waiting on a final new line.
 */
static void
sclp_console_timeout(unsigned long data)
{
	sclp_conbuf_emit();
}

/*
 * Writes the given message to S390 system console
 */
static void
sclp_console_write(struct console *console, const char *message,
		   unsigned int count)
{
	unsigned long flags;
	void *page;
	int written;

	if (count == 0)
		return;
	spin_lock_irqsave(&sclp_con_lock, flags);
	/*
	 * process escape characters, write message into buffer,
	 * send buffer to SCLP
	 */
	do {
		/* make sure we have a console output buffer */
		if (sclp_conbuf == NULL) {
			while (list_empty(&sclp_con_pages)) {
				spin_unlock_irqrestore(&sclp_con_lock, flags);
				sclp_sync_wait();
				spin_lock_irqsave(&sclp_con_lock, flags);
			}
			page = sclp_con_pages.next;
			list_del((struct list_head *) page);
			sclp_conbuf = sclp_make_buffer(page, sclp_con_columns,
						       sclp_con_width_htab);
		}
		/* try to write the string to the current output buffer */
		written = sclp_write(sclp_conbuf, (const unsigned char *)
				     message, count);
		if (written == count)
			break;
		/*
		 * Not all characters could be written to the current
		 * output buffer. Emit the buffer, create a new buffer
		 * and then output the rest of the string.
		 */
		spin_unlock_irqrestore(&sclp_con_lock, flags);
		sclp_conbuf_emit();
		spin_lock_irqsave(&sclp_con_lock, flags);
		message += written;
		count -= written;
	} while (count > 0);
	/* Setup timer to output current console buffer after 1/10 second */
	if (sclp_conbuf != NULL && sclp_chars_in_buffer(sclp_conbuf) != 0 &&
	    !timer_pending(&sclp_con_timer)) {
		init_timer(&sclp_con_timer);
		sclp_con_timer.function = sclp_console_timeout;
		sclp_con_timer.data = 0UL;
		sclp_con_timer.expires = jiffies + HZ/10;
		add_timer(&sclp_con_timer);
	}
	spin_unlock_irqrestore(&sclp_con_lock, flags);
}

static struct tty_driver *
sclp_console_device(struct console *c, int *index)
{
	*index = c->index;
	return sclp_tty_driver;
}

/*
 * This routine is called from panic when the kernel
 * is going to give up. We have to make sure that all buffers
 * will be flushed to the SCLP.
 */
static void
sclp_console_flush(void)
{
	unsigned long flags;

	sclp_conbuf_emit();
	spin_lock_irqsave(&sclp_con_lock, flags);
	if (timer_pending(&sclp_con_timer))
		del_timer(&sclp_con_timer);
	while (sclp_con_buffer_count > 0) {
		spin_unlock_irqrestore(&sclp_con_lock, flags);
		sclp_sync_wait();
		spin_lock_irqsave(&sclp_con_lock, flags);
	}
	spin_unlock_irqrestore(&sclp_con_lock, flags);
}

static int
sclp_console_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	sclp_console_flush();
	return NOTIFY_OK;
}

static struct notifier_block on_panic_nb = {
	.notifier_call = sclp_console_notify,
	.priority = 1,
};

static struct notifier_block on_reboot_nb = {
	.notifier_call = sclp_console_notify,
	.priority = 1,
};

/*
 * used to register the SCLP console to the kernel and to
 * give printk necessary information
 */
static struct console sclp_console =
{
	.name = sclp_console_name,
	.write = sclp_console_write,
	.device = sclp_console_device,
	.flags = CON_PRINTBUFFER,
	.index = 0 /* ttyS0 */
};

/*
 * called by console_init() in drivers/char/tty_io.c at boot-time.
 */
static int __init
sclp_console_init(void)
{
	void *page;
	int i;
	int rc;

	if (!CONSOLE_IS_SCLP)
		return 0;
	rc = sclp_rw_init();
	if (rc)
		return rc;
	/* Allocate pages for output buffering */
	INIT_LIST_HEAD(&sclp_con_pages);
	for (i = 0; i < MAX_CONSOLE_PAGES; i++) {
		page = alloc_bootmem_low_pages(PAGE_SIZE);
		list_add_tail((struct list_head *) page, &sclp_con_pages);
	}
	INIT_LIST_HEAD(&sclp_con_outqueue);
	spin_lock_init(&sclp_con_lock);
	sclp_con_buffer_count = 0;
	sclp_conbuf = NULL;
	init_timer(&sclp_con_timer);

	/* Set output format */
	if (MACHINE_IS_VM)
		/*
		 * save 4 characters for the CPU number
		 * written at start of each line by VM/CP
		 */
		sclp_con_columns = 76;
	else
		sclp_con_columns = 80;
	sclp_con_width_htab = 8;

	/* enable printk-access to this driver */
	atomic_notifier_chain_register(&panic_notifier_list, &on_panic_nb);
	register_reboot_notifier(&on_reboot_nb);
	register_console(&sclp_console);
	return 0;
}

console_initcall(sclp_console_init);
