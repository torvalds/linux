/*
 *  dashtty.c - tty driver for Dash channels interface.
 *
 *  Copyright (C) 2007,2008,2012 Imagination Technologies
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/uaccess.h>

#include <asm/da.h>

/* Channel error codes */
#define CONAOK	0
#define CONERR	1
#define CONBAD	2
#define CONPRM	3
#define CONADR	4
#define CONCNT	5
#define CONCBF	6
#define CONCBE	7
#define CONBSY	8

/* Default channel for the console */
#define CONSOLE_CHANNEL      1

#define NUM_TTY_CHANNELS     6

/* Auto allocate */
#define DA_TTY_MAJOR        0

/* A speedy poll rate helps the userland debug process connection response.
 * But, if you set it too high then no other userland processes get much
 * of a look in.
 */
#define DA_TTY_POLL (HZ / 50)

/*
 * A short put delay improves latency but has a high throughput overhead
 */
#define DA_TTY_PUT_DELAY (HZ / 100)

static atomic_t num_channels_need_poll = ATOMIC_INIT(0);

static struct timer_list poll_timer;

static struct tty_driver *channel_driver;

static struct timer_list put_timer;
static struct task_struct *dashtty_thread;

/*
 * The console_poll parameter determines whether the console channel should be
 * polled for input.
 * By default the console channel isn't polled at all, in order to avoid the
 * overhead, but that means it isn't possible to have a login on /dev/console.
 */
static bool console_poll;
module_param(console_poll, bool, S_IRUGO);

#define RX_BUF_SIZE 1024

enum {
	INCHR = 1,
	OUTCHR,
	RDBUF,
	WRBUF,
	RDSTAT
};

/**
 * struct dashtty_port - Wrapper struct for dashtty tty_port.
 * @port:		TTY port data
 * @rx_lock:		Lock for rx_buf.
 *			This protects between the poll timer and user context.
 *			It's also held during read SWITCH operations.
 * @rx_buf:		Read buffer
 * @xmit_lock:		Lock for xmit_*, and port.xmit_buf.
 *			This protects between user context and kernel thread.
 *			It's also held during write SWITCH operations.
 * @xmit_cnt:		Size of xmit buffer contents
 * @xmit_head:		Head of xmit buffer where data is written
 * @xmit_tail:		Tail of xmit buffer where data is read
 * @xmit_empty:		Completion for xmit buffer being empty
 */
struct dashtty_port {
	struct tty_port		 port;
	spinlock_t		 rx_lock;
	void			*rx_buf;
	struct mutex		 xmit_lock;
	unsigned int		 xmit_cnt;
	unsigned int		 xmit_head;
	unsigned int		 xmit_tail;
	struct completion	 xmit_empty;
};

static struct dashtty_port dashtty_ports[NUM_TTY_CHANNELS];

static atomic_t dashtty_xmit_cnt = ATOMIC_INIT(0);
static wait_queue_head_t dashtty_waitqueue;

/*
 * Low-level DA channel access routines
 */
static int chancall(int in_bios_function, int in_channel,
		    int in_arg2, void *in_arg3,
		    void *in_arg4)
{
	register int   bios_function asm("D1Ar1") = in_bios_function;
	register int   channel       asm("D0Ar2") = in_channel;
	register int   arg2          asm("D1Ar3") = in_arg2;
	register void *arg3          asm("D0Ar4") = in_arg3;
	register void *arg4          asm("D1Ar5") = in_arg4;
	register int   bios_call     asm("D0Ar6") = 3;
	register int   result        asm("D0Re0");

	asm volatile (
		"MSETL	[A0StP++], %6,%4,%2\n\t"
		"ADD	A0StP, A0StP, #8\n\t"
		"SWITCH	#0x0C30208\n\t"
		"GETD	%0, [A0StP+#-8]\n\t"
		"SUB	A0StP, A0StP, #(4*6)+8\n\t"
		: "=d" (result)   /* outs */
		: "d" (bios_function),
		  "d" (channel),
		  "d" (arg2),
		  "d" (arg3),
		  "d" (arg4),
		  "d" (bios_call) /* ins */
		: "memory");

	return result;
}

/*
 * Attempts to fetch count bytes from channel and returns actual count.
 */
static int fetch_data(unsigned int channel)
{
	struct dashtty_port *dport = &dashtty_ports[channel];
	int received = 0;

	spin_lock_bh(&dport->rx_lock);
	/* check the port isn't being shut down */
	if (!dport->rx_buf)
		goto unlock;
	if (chancall(RDBUF, channel, RX_BUF_SIZE,
		     (void *)dport->rx_buf, &received) == CONAOK) {
		if (received) {
			int space;
			unsigned char *cbuf;

			space = tty_prepare_flip_string(&dport->port, &cbuf,
							received);

			if (space <= 0)
				goto unlock;

			memcpy(cbuf, dport->rx_buf, space);
			tty_flip_buffer_push(&dport->port);
		}
	}
unlock:
	spin_unlock_bh(&dport->rx_lock);

	return received;
}

/**
 * find_channel_to_poll() - Returns number of the next channel to poll.
 * Returns:	The number of the next channel to poll, or -1 if none need
 *		polling.
 */
static int find_channel_to_poll(void)
{
	static int last_polled_channel;
	int last = last_polled_channel;
	int chan;
	struct dashtty_port *dport;

	for (chan = last + 1; ; ++chan) {
		if (chan >= NUM_TTY_CHANNELS)
			chan = 0;

		dport = &dashtty_ports[chan];
		if (dport->rx_buf) {
			last_polled_channel = chan;
			return chan;
		}

		if (chan == last)
			break;
	}
	return -1;
}

/**
 * put_channel_data() - Write out a block of channel data.
 * @chan:	DA channel number.
 *
 * Write a single block of data out to the debug adapter. If the circular buffer
 * is wrapped then only the first block is written.
 *
 * Returns:	1 if the remote buffer was too full to accept data.
 *		0 otherwise.
 */
static int put_channel_data(unsigned int chan)
{
	struct dashtty_port *dport;
	struct tty_struct *tty;
	int number_written;
	unsigned int count = 0;

	dport = &dashtty_ports[chan];
	mutex_lock(&dport->xmit_lock);
	if (dport->xmit_cnt) {
		count = min((unsigned int)(SERIAL_XMIT_SIZE - dport->xmit_tail),
			    dport->xmit_cnt);
		chancall(WRBUF, chan, count,
			 dport->port.xmit_buf + dport->xmit_tail,
			 &number_written);
		dport->xmit_cnt -= number_written;
		if (!dport->xmit_cnt) {
			/* reset pointers to avoid wraps */
			dport->xmit_head = 0;
			dport->xmit_tail = 0;
			complete(&dport->xmit_empty);
		} else {
			dport->xmit_tail += number_written;
			if (dport->xmit_tail >= SERIAL_XMIT_SIZE)
				dport->xmit_tail -= SERIAL_XMIT_SIZE;
		}
		atomic_sub(number_written, &dashtty_xmit_cnt);
	}
	mutex_unlock(&dport->xmit_lock);

	/* if we've made more data available, wake up tty */
	if (count && number_written) {
		tty = tty_port_tty_get(&dport->port);
		if (tty) {
			tty_wakeup(tty);
			tty_kref_put(tty);
		}
	}

	/* did the write fail? */
	return count && !number_written;
}

/**
 * put_data() - Kernel thread to write out blocks of channel data to DA.
 * @arg:	Unused.
 *
 * This kernel thread runs while @dashtty_xmit_cnt != 0, and loops over the
 * channels to write out any buffered data. If any of the channels stall due to
 * the remote buffer being full, a hold off happens to allow the debugger to
 * drain the buffer.
 */
static int put_data(void *arg)
{
	unsigned int chan, stall;

	__set_current_state(TASK_RUNNING);
	while (!kthread_should_stop()) {
		/*
		 * For each channel see if there's anything to transmit in the
		 * port's xmit_buf.
		 */
		stall = 0;
		for (chan = 0; chan < NUM_TTY_CHANNELS; ++chan)
			stall += put_channel_data(chan);

		/*
		 * If some of the buffers are full, hold off for a short while
		 * to allow them to empty.
		 */
		if (stall)
			msleep(25);

		wait_event_interruptible(dashtty_waitqueue,
					 atomic_read(&dashtty_xmit_cnt));
	}

	return 0;
}

/*
 *	This gets called every DA_TTY_POLL and polls the channels for data
 */
static void dashtty_timer(unsigned long ignored)
{
	int channel;

	/* If there are no ports open do nothing and don't poll again. */
	if (!atomic_read(&num_channels_need_poll))
		return;

	channel = find_channel_to_poll();

	/* Did we find a channel to poll? */
	if (channel >= 0)
		fetch_data(channel);

	mod_timer_pinned(&poll_timer, jiffies + DA_TTY_POLL);
}

static void add_poll_timer(struct timer_list *poll_timer)
{
	setup_timer(poll_timer, dashtty_timer, 0);
	poll_timer->expires = jiffies + DA_TTY_POLL;

	/*
	 * Always attach the timer to the boot CPU. The DA channels are per-CPU
	 * so all polling should be from a single CPU.
	 */
	add_timer_on(poll_timer, 0);
}

static int dashtty_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct dashtty_port *dport = container_of(port, struct dashtty_port,
						  port);
	void *rx_buf;

	/* Allocate the buffer we use for writing data */
	if (tty_port_alloc_xmit_buf(port) < 0)
		goto err;

	/* Allocate the buffer we use for reading data */
	rx_buf = kzalloc(RX_BUF_SIZE, GFP_KERNEL);
	if (!rx_buf)
		goto err_free_xmit;

	spin_lock_bh(&dport->rx_lock);
	dport->rx_buf = rx_buf;
	spin_unlock_bh(&dport->rx_lock);

	/*
	 * Don't add the poll timer if we're opening a console. This
	 * avoids the overhead of polling the Dash but means it is not
	 * possible to have a login on /dev/console.
	 *
	 */
	if (console_poll || dport != &dashtty_ports[CONSOLE_CHANNEL])
		if (atomic_inc_return(&num_channels_need_poll) == 1)
			add_poll_timer(&poll_timer);

	return 0;
err_free_xmit:
	tty_port_free_xmit_buf(port);
err:
	return -ENOMEM;
}

static void dashtty_port_shutdown(struct tty_port *port)
{
	struct dashtty_port *dport = container_of(port, struct dashtty_port,
						  port);
	void *rx_buf;
	unsigned int count;

	/* stop reading */
	if (console_poll || dport != &dashtty_ports[CONSOLE_CHANNEL])
		if (atomic_dec_and_test(&num_channels_need_poll))
			del_timer_sync(&poll_timer);

	mutex_lock(&dport->xmit_lock);
	count = dport->xmit_cnt;
	mutex_unlock(&dport->xmit_lock);
	if (count) {
		/*
		 * There's still data to write out, so wake and wait for the
		 * writer thread to drain the buffer.
		 */
		del_timer(&put_timer);
		wake_up_interruptible(&dashtty_waitqueue);
		wait_for_completion(&dport->xmit_empty);
	}

	/* Null the read buffer (timer could still be running!) */
	spin_lock_bh(&dport->rx_lock);
	rx_buf = dport->rx_buf;
	dport->rx_buf = NULL;
	spin_unlock_bh(&dport->rx_lock);
	/* Free the read buffer */
	kfree(rx_buf);

	/* Free the write buffer */
	tty_port_free_xmit_buf(port);
}

static const struct tty_port_operations dashtty_port_ops = {
	.activate	= dashtty_port_activate,
	.shutdown	= dashtty_port_shutdown,
};

static int dashtty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	return tty_port_install(&dashtty_ports[tty->index].port, driver, tty);
}

static int dashtty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void dashtty_close(struct tty_struct *tty, struct file *filp)
{
	return tty_port_close(tty->port, tty, filp);
}

static void dashtty_hangup(struct tty_struct *tty)
{
	int channel;
	struct dashtty_port *dport;

	channel = tty->index;
	dport = &dashtty_ports[channel];

	/* drop any data in the xmit buffer */
	mutex_lock(&dport->xmit_lock);
	if (dport->xmit_cnt) {
		atomic_sub(dport->xmit_cnt, &dashtty_xmit_cnt);
		dport->xmit_cnt = 0;
		dport->xmit_head = 0;
		dport->xmit_tail = 0;
		complete(&dport->xmit_empty);
	}
	mutex_unlock(&dport->xmit_lock);

	tty_port_hangup(tty->port);
}

/**
 * dashtty_put_timer() - Delayed wake up of kernel thread.
 * @ignored:	unused
 *
 * This timer function wakes up the kernel thread if any data exists in the
 * buffers. It is used to delay the expensive writeout until the writer has
 * stopped writing.
 */
static void dashtty_put_timer(unsigned long ignored)
{
	if (atomic_read(&dashtty_xmit_cnt))
		wake_up_interruptible(&dashtty_waitqueue);
}

static int dashtty_write(struct tty_struct *tty, const unsigned char *buf,
			 int total)
{
	int channel, count, block;
	struct dashtty_port *dport;

	/* Determine the channel */
	channel = tty->index;
	dport = &dashtty_ports[channel];

	/*
	 * Write to output buffer.
	 *
	 * The reason that we asynchronously write the buffer is because if we
	 * were to write the buffer synchronously then because DA channels are
	 * per-CPU the buffer would be written to the channel of whatever CPU
	 * we're running on.
	 *
	 * What we actually want to happen is have all input and output done on
	 * one CPU.
	 */
	mutex_lock(&dport->xmit_lock);
	/* work out how many bytes we can write to the xmit buffer */
	total = min(total, (int)(SERIAL_XMIT_SIZE - dport->xmit_cnt));
	atomic_add(total, &dashtty_xmit_cnt);
	dport->xmit_cnt += total;
	/* write the actual bytes (may need splitting if it wraps) */
	for (count = total; count; count -= block) {
		block = min(count, (int)(SERIAL_XMIT_SIZE - dport->xmit_head));
		memcpy(dport->port.xmit_buf + dport->xmit_head, buf, block);
		dport->xmit_head += block;
		if (dport->xmit_head >= SERIAL_XMIT_SIZE)
			dport->xmit_head -= SERIAL_XMIT_SIZE;
		buf += block;
	}
	count = dport->xmit_cnt;
	/* xmit buffer no longer empty? */
	if (count)
		reinit_completion(&dport->xmit_empty);
	mutex_unlock(&dport->xmit_lock);

	if (total) {
		/*
		 * If the buffer is full, wake up the kthread, otherwise allow
		 * some more time for the buffer to fill up a bit before waking
		 * it.
		 */
		if (count == SERIAL_XMIT_SIZE) {
			del_timer(&put_timer);
			wake_up_interruptible(&dashtty_waitqueue);
		} else {
			mod_timer(&put_timer, jiffies + DA_TTY_PUT_DELAY);
		}
	}
	return total;
}

static int dashtty_write_room(struct tty_struct *tty)
{
	struct dashtty_port *dport;
	int channel;
	int room;

	channel = tty->index;
	dport = &dashtty_ports[channel];

	/* report the space in the xmit buffer */
	mutex_lock(&dport->xmit_lock);
	room = SERIAL_XMIT_SIZE - dport->xmit_cnt;
	mutex_unlock(&dport->xmit_lock);

	return room;
}

static int dashtty_chars_in_buffer(struct tty_struct *tty)
{
	struct dashtty_port *dport;
	int channel;
	int chars;

	channel = tty->index;
	dport = &dashtty_ports[channel];

	/* report the number of bytes in the xmit buffer */
	mutex_lock(&dport->xmit_lock);
	chars = dport->xmit_cnt;
	mutex_unlock(&dport->xmit_lock);

	return chars;
}

static const struct tty_operations dashtty_ops = {
	.install		= dashtty_install,
	.open			= dashtty_open,
	.close			= dashtty_close,
	.hangup			= dashtty_hangup,
	.write			= dashtty_write,
	.write_room		= dashtty_write_room,
	.chars_in_buffer	= dashtty_chars_in_buffer,
};

static int __init dashtty_init(void)
{
	int ret;
	int nport;
	struct dashtty_port *dport;

	if (!metag_da_enabled())
		return -ENODEV;

	channel_driver = tty_alloc_driver(NUM_TTY_CHANNELS,
					  TTY_DRIVER_REAL_RAW);
	if (IS_ERR(channel_driver))
		return PTR_ERR(channel_driver);

	channel_driver->driver_name = "metag_da";
	channel_driver->name = "ttyDA";
	channel_driver->major = DA_TTY_MAJOR;
	channel_driver->minor_start = 0;
	channel_driver->type = TTY_DRIVER_TYPE_SERIAL;
	channel_driver->subtype = SERIAL_TYPE_NORMAL;
	channel_driver->init_termios = tty_std_termios;
	channel_driver->init_termios.c_cflag |= CLOCAL;

	tty_set_operations(channel_driver, &dashtty_ops);
	for (nport = 0; nport < NUM_TTY_CHANNELS; nport++) {
		dport = &dashtty_ports[nport];
		tty_port_init(&dport->port);
		dport->port.ops = &dashtty_port_ops;
		spin_lock_init(&dport->rx_lock);
		mutex_init(&dport->xmit_lock);
		/* the xmit buffer starts empty, i.e. completely written */
		init_completion(&dport->xmit_empty);
		complete(&dport->xmit_empty);
	}

	setup_timer(&put_timer, dashtty_put_timer, 0);

	init_waitqueue_head(&dashtty_waitqueue);
	dashtty_thread = kthread_create(put_data, NULL, "ttyDA");
	if (IS_ERR(dashtty_thread)) {
		pr_err("Couldn't create dashtty thread\n");
		ret = PTR_ERR(dashtty_thread);
		goto err_destroy_ports;
	}
	/*
	 * Bind the writer thread to the boot CPU so it can't migrate.
	 * DA channels are per-CPU and we want all channel I/O to be on a single
	 * predictable CPU.
	 */
	kthread_bind(dashtty_thread, 0);
	wake_up_process(dashtty_thread);

	ret = tty_register_driver(channel_driver);

	if (ret < 0) {
		pr_err("Couldn't install dashtty driver: err %d\n",
		       ret);
		goto err_stop_kthread;
	}

	return 0;

err_stop_kthread:
	kthread_stop(dashtty_thread);
err_destroy_ports:
	for (nport = 0; nport < NUM_TTY_CHANNELS; nport++) {
		dport = &dashtty_ports[nport];
		tty_port_destroy(&dport->port);
	}
	put_tty_driver(channel_driver);
	return ret;
}
device_initcall(dashtty_init);

#ifdef CONFIG_DA_CONSOLE

static void dash_console_write(struct console *co, const char *s,
			       unsigned int count)
{
	int actually_written;

	chancall(WRBUF, CONSOLE_CHANNEL, count, (void *)s, &actually_written);
}

static struct tty_driver *dash_console_device(struct console *c, int *index)
{
	*index = c->index;
	return channel_driver;
}

struct console dash_console = {
	.name = "ttyDA",
	.write = dash_console_write,
	.device = dash_console_device,
	.flags = CON_PRINTBUFFER,
	.index = 1,
};

#endif
