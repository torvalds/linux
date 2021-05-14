// SPDX-License-Identifier: GPL-2.0
/*
 *    SCLP line mode terminal driver.
 *
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kmod.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gfp.h>
#include <linux/uaccess.h>

#include "ctrlchar.h"
#include "sclp.h"
#include "sclp_rw.h"
#include "sclp_tty.h"

/*
 * size of a buffer that collects single characters coming in
 * via sclp_tty_put_char()
 */
#define SCLP_TTY_BUF_SIZE 512

/*
 * There is exactly one SCLP terminal, so we can keep things simple
 * and allocate all variables statically.
 */

/* Lock to guard over changes to global variables. */
static DEFINE_SPINLOCK(sclp_tty_lock);
/* List of free pages that can be used for console output buffering. */
static LIST_HEAD(sclp_tty_pages);
/* List of full struct sclp_buffer structures ready for output. */
static LIST_HEAD(sclp_tty_outqueue);
/* Counter how many buffers are emitted. */
static int sclp_tty_buffer_count;
/* Pointer to current console buffer. */
static struct sclp_buffer *sclp_ttybuf;
/* Timer for delayed output of console messages. */
static struct timer_list sclp_tty_timer;

static struct tty_port sclp_port;
static unsigned char sclp_tty_chars[SCLP_TTY_BUF_SIZE];
static unsigned short int sclp_tty_chars_count;

struct tty_driver *sclp_tty_driver;

static int sclp_tty_tolower;

#define SCLP_TTY_COLUMNS 320
#define SPACES_PER_TAB 8
#define CASE_DELIMITER 0x6c /* to separate upper and lower case (% in EBCDIC) */

/* This routine is called whenever we try to open a SCLP terminal. */
static int
sclp_tty_open(struct tty_struct *tty, struct file *filp)
{
	tty_port_tty_set(&sclp_port, tty);
	tty->driver_data = NULL;
	return 0;
}

/* This routine is called when the SCLP terminal is closed. */
static void
sclp_tty_close(struct tty_struct *tty, struct file *filp)
{
	if (tty->count > 1)
		return;
	tty_port_tty_set(&sclp_port, NULL);
}

/*
 * This routine returns the numbers of characters the tty driver
 * will accept for queuing to be written.  This number is subject
 * to change as output buffers get emptied, or if the output flow
 * control is acted. This is not an exact number because not every
 * character needs the same space in the sccb. The worst case is
 * a string of newlines. Every newline creates a new message which
 * needs 82 bytes.
 */
static int
sclp_tty_write_room (struct tty_struct *tty)
{
	unsigned long flags;
	struct list_head *l;
	int count;

	spin_lock_irqsave(&sclp_tty_lock, flags);
	count = 0;
	if (sclp_ttybuf != NULL)
		count = sclp_buffer_space(sclp_ttybuf) / sizeof(struct msg_buf);
	list_for_each(l, &sclp_tty_pages)
		count += NR_EMPTY_MSG_PER_SCCB;
	spin_unlock_irqrestore(&sclp_tty_lock, flags);
	return count;
}

static void
sclp_ttybuf_callback(struct sclp_buffer *buffer, int rc)
{
	unsigned long flags;
	void *page;

	do {
		page = sclp_unmake_buffer(buffer);
		spin_lock_irqsave(&sclp_tty_lock, flags);
		/* Remove buffer from outqueue */
		list_del(&buffer->list);
		sclp_tty_buffer_count--;
		list_add_tail((struct list_head *) page, &sclp_tty_pages);
		/* Check if there is a pending buffer on the out queue. */
		buffer = NULL;
		if (!list_empty(&sclp_tty_outqueue))
			buffer = list_entry(sclp_tty_outqueue.next,
					    struct sclp_buffer, list);
		spin_unlock_irqrestore(&sclp_tty_lock, flags);
	} while (buffer && sclp_emit_buffer(buffer, sclp_ttybuf_callback));

	tty_port_tty_wakeup(&sclp_port);
}

static inline void
__sclp_ttybuf_emit(struct sclp_buffer *buffer)
{
	unsigned long flags;
	int count;
	int rc;

	spin_lock_irqsave(&sclp_tty_lock, flags);
	list_add_tail(&buffer->list, &sclp_tty_outqueue);
	count = sclp_tty_buffer_count++;
	spin_unlock_irqrestore(&sclp_tty_lock, flags);
	if (count)
		return;
	rc = sclp_emit_buffer(buffer, sclp_ttybuf_callback);
	if (rc)
		sclp_ttybuf_callback(buffer, rc);
}

/*
 * When this routine is called from the timer then we flush the
 * temporary write buffer.
 */
static void
sclp_tty_timeout(struct timer_list *unused)
{
	unsigned long flags;
	struct sclp_buffer *buf;

	spin_lock_irqsave(&sclp_tty_lock, flags);
	buf = sclp_ttybuf;
	sclp_ttybuf = NULL;
	spin_unlock_irqrestore(&sclp_tty_lock, flags);

	if (buf != NULL) {
		__sclp_ttybuf_emit(buf);
	}
}

/*
 * Write a string to the sclp tty.
 */
static int sclp_tty_write_string(const unsigned char *str, int count, int may_fail)
{
	unsigned long flags;
	void *page;
	int written;
	int overall_written;
	struct sclp_buffer *buf;

	if (count <= 0)
		return 0;
	overall_written = 0;
	spin_lock_irqsave(&sclp_tty_lock, flags);
	do {
		/* Create a sclp output buffer if none exists yet */
		if (sclp_ttybuf == NULL) {
			while (list_empty(&sclp_tty_pages)) {
				spin_unlock_irqrestore(&sclp_tty_lock, flags);
				if (may_fail)
					goto out;
				else
					sclp_sync_wait();
				spin_lock_irqsave(&sclp_tty_lock, flags);
			}
			page = sclp_tty_pages.next;
			list_del((struct list_head *) page);
			sclp_ttybuf = sclp_make_buffer(page, SCLP_TTY_COLUMNS,
						       SPACES_PER_TAB);
		}
		/* try to write the string to the current output buffer */
		written = sclp_write(sclp_ttybuf, str, count);
		overall_written += written;
		if (written == count)
			break;
		/*
		 * Not all characters could be written to the current
		 * output buffer. Emit the buffer, create a new buffer
		 * and then output the rest of the string.
		 */
		buf = sclp_ttybuf;
		sclp_ttybuf = NULL;
		spin_unlock_irqrestore(&sclp_tty_lock, flags);
		__sclp_ttybuf_emit(buf);
		spin_lock_irqsave(&sclp_tty_lock, flags);
		str += written;
		count -= written;
	} while (count > 0);
	/* Setup timer to output current console buffer after 1/10 second */
	if (sclp_ttybuf && sclp_chars_in_buffer(sclp_ttybuf) &&
	    !timer_pending(&sclp_tty_timer)) {
		mod_timer(&sclp_tty_timer, jiffies + HZ / 10);
	}
	spin_unlock_irqrestore(&sclp_tty_lock, flags);
out:
	return overall_written;
}

/*
 * This routine is called by the kernel to write a series of characters to the
 * tty device. The characters may come from user space or kernel space. This
 * routine will return the number of characters actually accepted for writing.
 */
static int
sclp_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	if (sclp_tty_chars_count > 0) {
		sclp_tty_write_string(sclp_tty_chars, sclp_tty_chars_count, 0);
		sclp_tty_chars_count = 0;
	}
	return sclp_tty_write_string(buf, count, 1);
}

/*
 * This routine is called by the kernel to write a single character to the tty
 * device. If the kernel uses this routine, it must call the flush_chars()
 * routine (if defined) when it is done stuffing characters into the driver.
 *
 * Characters provided to sclp_tty_put_char() are buffered by the SCLP driver.
 * If the given character is a '\n' the contents of the SCLP write buffer
 * - including previous characters from sclp_tty_put_char() and strings from
 * sclp_write() without final '\n' - will be written.
 */
static int
sclp_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	sclp_tty_chars[sclp_tty_chars_count++] = ch;
	if (ch == '\n' || sclp_tty_chars_count >= SCLP_TTY_BUF_SIZE) {
		sclp_tty_write_string(sclp_tty_chars, sclp_tty_chars_count, 0);
		sclp_tty_chars_count = 0;
	}
	return 1;
}

/*
 * This routine is called by the kernel after it has written a series of
 * characters to the tty device using put_char().
 */
static void
sclp_tty_flush_chars(struct tty_struct *tty)
{
	if (sclp_tty_chars_count > 0) {
		sclp_tty_write_string(sclp_tty_chars, sclp_tty_chars_count, 0);
		sclp_tty_chars_count = 0;
	}
}

/*
 * This routine returns the number of characters in the write buffer of the
 * SCLP driver. The provided number includes all characters that are stored
 * in the SCCB (will be written next time the SCLP is not busy) as well as
 * characters in the write buffer (will not be written as long as there is a
 * final line feed missing).
 */
static int
sclp_tty_chars_in_buffer(struct tty_struct *tty)
{
	unsigned long flags;
	struct list_head *l;
	struct sclp_buffer *t;
	int count;

	spin_lock_irqsave(&sclp_tty_lock, flags);
	count = 0;
	if (sclp_ttybuf != NULL)
		count = sclp_chars_in_buffer(sclp_ttybuf);
	list_for_each(l, &sclp_tty_outqueue) {
		t = list_entry(l, struct sclp_buffer, list);
		count += sclp_chars_in_buffer(t);
	}
	spin_unlock_irqrestore(&sclp_tty_lock, flags);
	return count;
}

/*
 * removes all content from buffers of low level driver
 */
static void
sclp_tty_flush_buffer(struct tty_struct *tty)
{
	if (sclp_tty_chars_count > 0) {
		sclp_tty_write_string(sclp_tty_chars, sclp_tty_chars_count, 0);
		sclp_tty_chars_count = 0;
	}
}

/*
 * push input to tty
 */
static void
sclp_tty_input(unsigned char* buf, unsigned int count)
{
	struct tty_struct *tty = tty_port_tty_get(&sclp_port);
	unsigned int cchar;

	/*
	 * If this tty driver is currently closed
	 * then throw the received input away.
	 */
	if (tty == NULL)
		return;
	cchar = ctrlchar_handle(buf, count, tty);
	switch (cchar & CTRLCHAR_MASK) {
	case CTRLCHAR_SYSRQ:
		break;
	case CTRLCHAR_CTRL:
		tty_insert_flip_char(&sclp_port, cchar, TTY_NORMAL);
		tty_flip_buffer_push(&sclp_port);
		break;
	case CTRLCHAR_NONE:
		/* send (normal) input to line discipline */
		if (count < 2 ||
		    (strncmp((const char *) buf + count - 2, "^n", 2) &&
		     strncmp((const char *) buf + count - 2, "\252n", 2))) {
			/* add the auto \n */
			tty_insert_flip_string(&sclp_port, buf, count);
			tty_insert_flip_char(&sclp_port, '\n', TTY_NORMAL);
		} else
			tty_insert_flip_string(&sclp_port, buf, count - 2);
		tty_flip_buffer_push(&sclp_port);
		break;
	}
	tty_kref_put(tty);
}

/*
 * get a EBCDIC string in upper/lower case,
 * find out characters in lower/upper case separated by a special character,
 * modifiy original string,
 * returns length of resulting string
 */
static int sclp_switch_cases(unsigned char *buf, int count)
{
	unsigned char *ip, *op;
	int toggle;

	/* initially changing case is off */
	toggle = 0;
	ip = op = buf;
	while (count-- > 0) {
		/* compare with special character */
		if (*ip == CASE_DELIMITER) {
			/* followed by another special character? */
			if (count && ip[1] == CASE_DELIMITER) {
				/*
				 * ... then put a single copy of the special
				 * character to the output string
				 */
				*op++ = *ip++;
				count--;
			} else
				/*
				 * ... special character follower by a normal
				 * character toggles the case change behaviour
				 */
				toggle = ~toggle;
			/* skip special character */
			ip++;
		} else
			/* not the special character */
			if (toggle)
				/* but case switching is on */
				if (sclp_tty_tolower)
					/* switch to uppercase */
					*op++ = _ebc_toupper[(int) *ip++];
				else
					/* switch to lowercase */
					*op++ = _ebc_tolower[(int) *ip++];
			else
				/* no case switching, copy the character */
				*op++ = *ip++;
	}
	/* return length of reformatted string. */
	return op - buf;
}

static void sclp_get_input(struct gds_subvector *sv)
{
	unsigned char *str;
	int count;

	str = (unsigned char *) (sv + 1);
	count = sv->length - sizeof(*sv);
	if (sclp_tty_tolower)
		EBC_TOLOWER(str, count);
	count = sclp_switch_cases(str, count);
	/* convert EBCDIC to ASCII (modify original input in SCCB) */
	sclp_ebcasc_str(str, count);

	/* transfer input to high level driver */
	sclp_tty_input(str, count);
}

static inline void sclp_eval_selfdeftextmsg(struct gds_subvector *sv)
{
	void *end;

	end = (void *) sv + sv->length;
	for (sv = sv + 1; (void *) sv < end; sv = (void *) sv + sv->length)
		if (sv->key == 0x30)
			sclp_get_input(sv);
}

static inline void sclp_eval_textcmd(struct gds_vector *v)
{
	struct gds_subvector *sv;
	void *end;

	end = (void *) v + v->length;
	for (sv = (struct gds_subvector *) (v + 1);
	     (void *) sv < end; sv = (void *) sv + sv->length)
		if (sv->key == GDS_KEY_SELFDEFTEXTMSG)
			sclp_eval_selfdeftextmsg(sv);

}

static inline void sclp_eval_cpmsu(struct gds_vector *v)
{
	void *end;

	end = (void *) v + v->length;
	for (v = v + 1; (void *) v < end; v = (void *) v + v->length)
		if (v->gds_id == GDS_ID_TEXTCMD)
			sclp_eval_textcmd(v);
}


static inline void sclp_eval_mdsmu(struct gds_vector *v)
{
	v = sclp_find_gds_vector(v + 1, (void *) v + v->length, GDS_ID_CPMSU);
	if (v)
		sclp_eval_cpmsu(v);
}

static void sclp_tty_receiver(struct evbuf_header *evbuf)
{
	struct gds_vector *v;

	v = sclp_find_gds_vector(evbuf + 1, (void *) evbuf + evbuf->length,
				 GDS_ID_MDSMU);
	if (v)
		sclp_eval_mdsmu(v);
}

static void
sclp_tty_state_change(struct sclp_register *reg)
{
}

static struct sclp_register sclp_input_event =
{
	.receive_mask = EVTYP_OPCMD_MASK | EVTYP_PMSGCMD_MASK,
	.state_change_fn = sclp_tty_state_change,
	.receiver_fn = sclp_tty_receiver
};

static const struct tty_operations sclp_ops = {
	.open = sclp_tty_open,
	.close = sclp_tty_close,
	.write = sclp_tty_write,
	.put_char = sclp_tty_put_char,
	.flush_chars = sclp_tty_flush_chars,
	.write_room = sclp_tty_write_room,
	.chars_in_buffer = sclp_tty_chars_in_buffer,
	.flush_buffer = sclp_tty_flush_buffer,
};

static int __init
sclp_tty_init(void)
{
	struct tty_driver *driver;
	void *page;
	int i;
	int rc;

	/* z/VM multiplexes the line mode output on the 32xx screen */
	if (MACHINE_IS_VM && !CONSOLE_IS_SCLP)
		return 0;
	if (!sclp.has_linemode)
		return 0;
	driver = alloc_tty_driver(1);
	if (!driver)
		return -ENOMEM;

	rc = sclp_rw_init();
	if (rc) {
		put_tty_driver(driver);
		return rc;
	}
	/* Allocate pages for output buffering */
	for (i = 0; i < MAX_KMEM_PAGES; i++) {
		page = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
		if (page == NULL) {
			put_tty_driver(driver);
			return -ENOMEM;
		}
		list_add_tail((struct list_head *) page, &sclp_tty_pages);
	}
	timer_setup(&sclp_tty_timer, sclp_tty_timeout, 0);
	sclp_ttybuf = NULL;
	sclp_tty_buffer_count = 0;
	if (MACHINE_IS_VM) {
		/* case input lines to lowercase */
		sclp_tty_tolower = 1;
	}
	sclp_tty_chars_count = 0;

	rc = sclp_register(&sclp_input_event);
	if (rc) {
		put_tty_driver(driver);
		return rc;
	}

	tty_port_init(&sclp_port);

	driver->driver_name = "sclp_line";
	driver->name = "sclp_line";
	driver->major = TTY_MAJOR;
	driver->minor_start = 64;
	driver->type = TTY_DRIVER_TYPE_SYSTEM;
	driver->subtype = SYSTEM_TYPE_TTY;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_iflag = IGNBRK | IGNPAR;
	driver->init_termios.c_oflag = ONLCR;
	driver->init_termios.c_lflag = ISIG | ECHO;
	driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(driver, &sclp_ops);
	tty_port_link_device(&sclp_port, driver, 0);
	rc = tty_register_driver(driver);
	if (rc) {
		put_tty_driver(driver);
		tty_port_destroy(&sclp_port);
		return rc;
	}
	sclp_tty_driver = driver;
	return 0;
}
device_initcall(sclp_tty_init);
