/*
 * IBM/3270 Driver - console view.
 *
 * Author(s):
 *   Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *   Rewritten for 2.5 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *     Copyright IBM Corp. 2003, 2009
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/reboot.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/cpcmd.h>
#include <asm/ebcdic.h>

#include "raw3270.h"
#include "tty3270.h"
#include "ctrlchar.h"

#define CON3270_OUTPUT_BUFFER_SIZE 1024
#define CON3270_STRING_PAGES 4

static struct raw3270_fn con3270_fn;

/*
 * Main 3270 console view data structure.
 */
struct con3270 {
	struct raw3270_view view;
	struct list_head freemem;	/* list of free memory for strings. */

	/* Output stuff. */
	struct list_head lines;		/* list of lines. */
	struct list_head update;	/* list of lines to update. */
	int line_nr;			/* line number for next update. */
	int nr_lines;			/* # lines in list. */
	int nr_up;			/* # lines up in history. */
	unsigned long update_flags;	/* Update indication bits. */
	struct string *cline;		/* current output line. */
	struct string *status;		/* last line of display. */
	struct raw3270_request *write;	/* single write request. */
	struct timer_list timer;

	/* Input stuff. */
	struct string *input;		/* input string for read request. */
	struct raw3270_request *read;	/* single read request. */
	struct raw3270_request *kreset;	/* single keyboard reset request. */
	struct tasklet_struct readlet;	/* tasklet to issue read request. */
};

static struct con3270 *condev;

/* con3270->update_flags. See con3270_update for details. */
#define CON_UPDATE_ERASE	1	/* Use EWRITEA instead of WRITE. */
#define CON_UPDATE_LIST		2	/* Update lines in tty3270->update. */
#define CON_UPDATE_STATUS	4	/* Update status line. */
#define CON_UPDATE_ALL		8	/* Recreate screen. */

static void con3270_update(struct con3270 *);

/*
 * Setup timeout for a device. On timeout trigger an update.
 */
static void con3270_set_timer(struct con3270 *cp, int expires)
{
	if (expires == 0)
		del_timer(&cp->timer);
	else
		mod_timer(&cp->timer, jiffies + expires);
}

/*
 * The status line is the last line of the screen. It shows the string
 * "console view" in the lower left corner and "Running"/"More..."/"Holding"
 * in the lower right corner of the screen.
 */
static void
con3270_update_status(struct con3270 *cp)
{
	char *str;

	str = (cp->nr_up != 0) ? "History" : "Running";
	memcpy(cp->status->string + 24, str, 7);
	codepage_convert(cp->view.ascebc, cp->status->string + 24, 7);
	cp->update_flags |= CON_UPDATE_STATUS;
}

static void
con3270_create_status(struct con3270 *cp)
{
	static const unsigned char blueprint[] =
		{ TO_SBA, 0, 0, TO_SF,TF_LOG,TO_SA,TAT_COLOR, TAC_GREEN,
		  'c','o','n','s','o','l','e',' ','v','i','e','w',
		  TO_RA,0,0,0,'R','u','n','n','i','n','g',TO_SF,TF_LOG };

	cp->status = alloc_string(&cp->freemem, sizeof(blueprint));
	/* Copy blueprint to status line */
	memcpy(cp->status->string, blueprint, sizeof(blueprint));
	/* Set TO_RA addresses. */
	raw3270_buffer_address(cp->view.dev, cp->status->string + 1,
			       cp->view.cols * (cp->view.rows - 1));
	raw3270_buffer_address(cp->view.dev, cp->status->string + 21,
			       cp->view.cols * cp->view.rows - 8);
	/* Convert strings to ebcdic. */
	codepage_convert(cp->view.ascebc, cp->status->string + 8, 12);
	codepage_convert(cp->view.ascebc, cp->status->string + 24, 7);
}

/*
 * Set output offsets to 3270 datastream fragment of a console string.
 */
static void
con3270_update_string(struct con3270 *cp, struct string *s, int nr)
{
	if (s->len >= cp->view.cols - 5)
		return;
	raw3270_buffer_address(cp->view.dev, s->string + s->len - 3,
			       cp->view.cols * (nr + 1));
}

/*
 * Rebuild update list to print all lines.
 */
static void
con3270_rebuild_update(struct con3270 *cp)
{
	struct string *s, *n;
	int nr;

	/* 
	 * Throw away update list and create a new one,
	 * containing all lines that will fit on the screen.
	 */
	list_for_each_entry_safe(s, n, &cp->update, update)
		list_del_init(&s->update);
	nr = cp->view.rows - 2 + cp->nr_up;
	list_for_each_entry_reverse(s, &cp->lines, list) {
		if (nr < cp->view.rows - 1)
			list_add(&s->update, &cp->update);
		if (--nr < 0)
			break;
	}
	cp->line_nr = 0;
	cp->update_flags |= CON_UPDATE_LIST;
}

/*
 * Alloc string for size bytes. Free strings from history if necessary.
 */
static struct string *
con3270_alloc_string(struct con3270 *cp, size_t size)
{
	struct string *s, *n;

	s = alloc_string(&cp->freemem, size);
	if (s)
		return s;
	list_for_each_entry_safe(s, n, &cp->lines, list) {
		list_del(&s->list);
		if (!list_empty(&s->update))
			list_del(&s->update);
		cp->nr_lines--;
		if (free_string(&cp->freemem, s) >= size)
			break;
	}
	s = alloc_string(&cp->freemem, size);
	BUG_ON(!s);
	if (cp->nr_up != 0 && cp->nr_up + cp->view.rows > cp->nr_lines) {
		cp->nr_up = cp->nr_lines - cp->view.rows + 1;
		con3270_rebuild_update(cp);
		con3270_update_status(cp);
	}
	return s;
}

/*
 * Write completion callback.
 */
static void
con3270_write_callback(struct raw3270_request *rq, void *data)
{
	raw3270_request_reset(rq);
	xchg(&((struct con3270 *) rq->view)->write, rq);
}

/*
 * Update console display.
 */
static void
con3270_update(struct con3270 *cp)
{
	struct raw3270_request *wrq;
	char wcc, prolog[6];
	unsigned long flags;
	unsigned long updated;
	struct string *s, *n;
	int rc;

	if (cp->view.dev)
		raw3270_activate_view(&cp->view);

	wrq = xchg(&cp->write, 0);
	if (!wrq) {
		con3270_set_timer(cp, 1);
		return;
	}

	spin_lock_irqsave(&cp->view.lock, flags);
	updated = 0;
	if (cp->update_flags & CON_UPDATE_ALL) {
		con3270_rebuild_update(cp);
		con3270_update_status(cp);
		cp->update_flags = CON_UPDATE_ERASE | CON_UPDATE_LIST |
			CON_UPDATE_STATUS;
	}
	if (cp->update_flags & CON_UPDATE_ERASE) {
		/* Use erase write alternate to initialize display. */
		raw3270_request_set_cmd(wrq, TC_EWRITEA);
		updated |= CON_UPDATE_ERASE;
	} else
		raw3270_request_set_cmd(wrq, TC_WRITE);

	wcc = TW_NONE;
	raw3270_request_add_data(wrq, &wcc, 1);

	/*
	 * Update status line.
	 */
	if (cp->update_flags & CON_UPDATE_STATUS)
		if (raw3270_request_add_data(wrq, cp->status->string,
					     cp->status->len) == 0)
			updated |= CON_UPDATE_STATUS;

	if (cp->update_flags & CON_UPDATE_LIST) {
		prolog[0] = TO_SBA;
		prolog[3] = TO_SA;
		prolog[4] = TAT_COLOR;
		prolog[5] = TAC_TURQ;
		raw3270_buffer_address(cp->view.dev, prolog + 1,
				       cp->view.cols * cp->line_nr);
		raw3270_request_add_data(wrq, prolog, 6);
		/* Write strings in the update list to the screen. */
		list_for_each_entry_safe(s, n, &cp->update, update) {
			if (s != cp->cline)
				con3270_update_string(cp, s, cp->line_nr);
			if (raw3270_request_add_data(wrq, s->string,
						     s->len) != 0)
				break;
			list_del_init(&s->update);
			if (s != cp->cline)
				cp->line_nr++;
		}
		if (list_empty(&cp->update))
			updated |= CON_UPDATE_LIST;
	}
	wrq->callback = con3270_write_callback;
	rc = raw3270_start(&cp->view, wrq);
	if (rc == 0) {
		cp->update_flags &= ~updated;
		if (cp->update_flags)
			con3270_set_timer(cp, 1);
	} else {
		raw3270_request_reset(wrq);
		xchg(&cp->write, wrq);
	}
	spin_unlock_irqrestore(&cp->view.lock, flags);
}

/*
 * Read tasklet.
 */
static void
con3270_read_tasklet(struct raw3270_request *rrq)
{
	static char kreset_data = TW_KR;
	struct con3270 *cp;
	unsigned long flags;
	int nr_up, deactivate;

	cp = (struct con3270 *) rrq->view;
	spin_lock_irqsave(&cp->view.lock, flags);
	nr_up = cp->nr_up;
	deactivate = 0;
	/* Check aid byte. */
	switch (cp->input->string[0]) {
	case 0x7d:	/* enter: jump to bottom. */
		nr_up = 0;
		break;
	case 0xf3:	/* PF3: deactivate the console view. */
		deactivate = 1;
		break;
	case 0x6d:	/* clear: start from scratch. */
		cp->update_flags = CON_UPDATE_ALL;
		con3270_set_timer(cp, 1);
		break;
	case 0xf7:	/* PF7: do a page up in the console log. */
		nr_up += cp->view.rows - 2;
		if (nr_up + cp->view.rows - 1 > cp->nr_lines) {
			nr_up = cp->nr_lines - cp->view.rows + 1;
			if (nr_up < 0)
				nr_up = 0;
		}
		break;
	case 0xf8:	/* PF8: do a page down in the console log. */
		nr_up -= cp->view.rows - 2;
		if (nr_up < 0)
			nr_up = 0;
		break;
	}
	if (nr_up != cp->nr_up) {
		cp->nr_up = nr_up;
		con3270_rebuild_update(cp);
		con3270_update_status(cp);
		con3270_set_timer(cp, 1);
	}
	spin_unlock_irqrestore(&cp->view.lock, flags);

	/* Start keyboard reset command. */
	raw3270_request_reset(cp->kreset);
	raw3270_request_set_cmd(cp->kreset, TC_WRITE);
	raw3270_request_add_data(cp->kreset, &kreset_data, 1);
	raw3270_start(&cp->view, cp->kreset);

	if (deactivate)
		raw3270_deactivate_view(&cp->view);

	raw3270_request_reset(rrq);
	xchg(&cp->read, rrq);
	raw3270_put_view(&cp->view);
}

/*
 * Read request completion callback.
 */
static void
con3270_read_callback(struct raw3270_request *rq, void *data)
{
	raw3270_get_view(rq->view);
	/* Schedule tasklet to pass input to tty. */
	tasklet_schedule(&((struct con3270 *) rq->view)->readlet);
}

/*
 * Issue a read request. Called only from interrupt function.
 */
static void
con3270_issue_read(struct con3270 *cp)
{
	struct raw3270_request *rrq;
	int rc;

	rrq = xchg(&cp->read, 0);
	if (!rrq)
		/* Read already scheduled. */
		return;
	rrq->callback = con3270_read_callback;
	rrq->callback_data = cp;
	raw3270_request_set_cmd(rrq, TC_READMOD);
	raw3270_request_set_data(rrq, cp->input->string, cp->input->len);
	/* Issue the read modified request. */
	rc = raw3270_start_irq(&cp->view, rrq);
	if (rc)
		raw3270_request_reset(rrq);
}

/*
 * Switch to the console view.
 */
static int
con3270_activate(struct raw3270_view *view)
{
	struct con3270 *cp;

	cp = (struct con3270 *) view;
	cp->update_flags = CON_UPDATE_ALL;
	con3270_set_timer(cp, 1);
	return 0;
}

static void
con3270_deactivate(struct raw3270_view *view)
{
	struct con3270 *cp;

	cp = (struct con3270 *) view;
	del_timer(&cp->timer);
}

static int
con3270_irq(struct con3270 *cp, struct raw3270_request *rq, struct irb *irb)
{
	/* Handle ATTN. Schedule tasklet to read aid. */
	if (irb->scsw.cmd.dstat & DEV_STAT_ATTENTION)
		con3270_issue_read(cp);

	if (rq) {
		if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)
			rq->rc = -EIO;
		else
			/* Normal end. Copy residual count. */
			rq->rescnt = irb->scsw.cmd.count;
	}
	return RAW3270_IO_DONE;
}

/* Console view to a 3270 device. */
static struct raw3270_fn con3270_fn = {
	.activate = con3270_activate,
	.deactivate = con3270_deactivate,
	.intv = (void *) con3270_irq
};

static inline void
con3270_cline_add(struct con3270 *cp)
{
	if (!list_empty(&cp->cline->list))
		/* Already added. */
		return;
	list_add_tail(&cp->cline->list, &cp->lines);
	cp->nr_lines++;
	con3270_rebuild_update(cp);
}

static inline void
con3270_cline_insert(struct con3270 *cp, unsigned char c)
{
	cp->cline->string[cp->cline->len++] = 
		cp->view.ascebc[(c < ' ') ? ' ' : c];
	if (list_empty(&cp->cline->update)) {
		list_add_tail(&cp->cline->update, &cp->update);
		cp->update_flags |= CON_UPDATE_LIST;
	}
}

static inline void
con3270_cline_end(struct con3270 *cp)
{
	struct string *s;
	unsigned int size;

	/* Copy cline. */
	size = (cp->cline->len < cp->view.cols - 5) ?
		cp->cline->len + 4 : cp->view.cols;
	s = con3270_alloc_string(cp, size);
	memcpy(s->string, cp->cline->string, cp->cline->len);
	if (s->len < cp->view.cols - 5) {
		s->string[s->len - 4] = TO_RA;
		s->string[s->len - 1] = 0;
	} else {
		while (--size > cp->cline->len)
			s->string[size] = cp->view.ascebc[' '];
	}
	/* Replace cline with allocated line s and reset cline. */
	list_add(&s->list, &cp->cline->list);
	list_del_init(&cp->cline->list);
	if (!list_empty(&cp->cline->update)) {
		list_add(&s->update, &cp->cline->update);
		list_del_init(&cp->cline->update);
	}
	cp->cline->len = 0;
}

/*
 * Write a string to the 3270 console
 */
static void
con3270_write(struct console *co, const char *str, unsigned int count)
{
	struct con3270 *cp;
	unsigned long flags;
	unsigned char c;

	cp = condev;
	spin_lock_irqsave(&cp->view.lock, flags);
	while (count-- > 0) {
		c = *str++;
		if (cp->cline->len == 0)
			con3270_cline_add(cp);
		if (c != '\n')
			con3270_cline_insert(cp, c);
		if (c == '\n' || cp->cline->len >= cp->view.cols)
			con3270_cline_end(cp);
	}
	/* Setup timer to output current console buffer after 1/10 second */
	cp->nr_up = 0;
	if (cp->view.dev && !timer_pending(&cp->timer))
		con3270_set_timer(cp, HZ/10);
	spin_unlock_irqrestore(&cp->view.lock,flags);
}

static struct tty_driver *
con3270_device(struct console *c, int *index)
{
	*index = c->index;
	return tty3270_driver;
}

/*
 * Wait for end of write request.
 */
static void
con3270_wait_write(struct con3270 *cp)
{
	while (!cp->write) {
		raw3270_wait_cons_dev(cp->view.dev);
		barrier();
	}
}

/*
 * panic() calls con3270_flush through a panic_notifier
 * before the system enters a disabled, endless loop.
 */
static void
con3270_flush(void)
{
	struct con3270 *cp;
	unsigned long flags;

	cp = condev;
	if (!cp->view.dev)
		return;
	raw3270_pm_unfreeze(&cp->view);
	spin_lock_irqsave(&cp->view.lock, flags);
	con3270_wait_write(cp);
	cp->nr_up = 0;
	con3270_rebuild_update(cp);
	con3270_update_status(cp);
	while (cp->update_flags != 0) {
		spin_unlock_irqrestore(&cp->view.lock, flags);
		con3270_update(cp);
		spin_lock_irqsave(&cp->view.lock, flags);
		con3270_wait_write(cp);
	}
	spin_unlock_irqrestore(&cp->view.lock, flags);
}

static int con3270_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	con3270_flush();
	return NOTIFY_OK;
}

static struct notifier_block on_panic_nb = {
	.notifier_call = con3270_notify,
	.priority = 0,
};

static struct notifier_block on_reboot_nb = {
	.notifier_call = con3270_notify,
	.priority = 0,
};

/*
 *  The console structure for the 3270 console
 */
static struct console con3270 = {
	.name	 = "tty3270",
	.write	 = con3270_write,
	.device	 = con3270_device,
	.flags	 = CON_PRINTBUFFER,
};

/*
 * 3270 console initialization code called from console_init().
 */
static int __init
con3270_init(void)
{
	struct ccw_device *cdev;
	struct raw3270 *rp;
	void *cbuf;
	int i;

	/* Check if 3270 is to be the console */
	if (!CONSOLE_IS_3270)
		return -ENODEV;

	/* Set the console mode for VM */
	if (MACHINE_IS_VM) {
		cpcmd("TERM CONMODE 3270", NULL, 0, NULL);
		cpcmd("TERM AUTOCR OFF", NULL, 0, NULL);
	}

	cdev = ccw_device_probe_console();
	if (IS_ERR(cdev))
		return -ENODEV;
	rp = raw3270_setup_console(cdev);
	if (IS_ERR(rp))
		return PTR_ERR(rp);

	condev = kzalloc(sizeof(struct con3270), GFP_KERNEL | GFP_DMA);
	condev->view.dev = rp;

	condev->read = raw3270_request_alloc(0);
	condev->read->callback = con3270_read_callback;
	condev->read->callback_data = condev;
	condev->write = raw3270_request_alloc(CON3270_OUTPUT_BUFFER_SIZE);
	condev->kreset = raw3270_request_alloc(1);

	INIT_LIST_HEAD(&condev->lines);
	INIT_LIST_HEAD(&condev->update);
	setup_timer(&condev->timer, (void (*)(unsigned long)) con3270_update,
		    (unsigned long) condev);
	tasklet_init(&condev->readlet, 
		     (void (*)(unsigned long)) con3270_read_tasklet,
		     (unsigned long) condev->read);

	raw3270_add_view(&condev->view, &con3270_fn, 1);

	INIT_LIST_HEAD(&condev->freemem);
	for (i = 0; i < CON3270_STRING_PAGES; i++) {
		cbuf = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
		add_string_memory(&condev->freemem, cbuf, PAGE_SIZE);
	}
	condev->cline = alloc_string(&condev->freemem, condev->view.cols);
	condev->cline->len = 0;
	con3270_create_status(condev);
	condev->input = alloc_string(&condev->freemem, 80);
	atomic_notifier_chain_register(&panic_notifier_list, &on_panic_nb);
	register_reboot_notifier(&on_reboot_nb);
	register_console(&con3270);
	return 0;
}

console_initcall(con3270_init);
