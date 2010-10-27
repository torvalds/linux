/*
 * TTY over Blackfin JTAG Communication
 *
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#define DRV_NAME "bfin-jtag-comm"
#define DEV_NAME "ttyBFJC"
#define pr_fmt(fmt) DRV_NAME ": " fmt

#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <asm/atomic.h>

#define pr_init(fmt, args...) ({ static const __initconst char __fmt[] = fmt; printk(__fmt, ## args); })

/* See the Debug/Emulation chapter in the HRM */
#define EMUDOF   0x00000001	/* EMUDAT_OUT full & valid */
#define EMUDIF   0x00000002	/* EMUDAT_IN full & valid */
#define EMUDOOVF 0x00000004	/* EMUDAT_OUT overflow */
#define EMUDIOVF 0x00000008	/* EMUDAT_IN overflow */

static inline uint32_t bfin_write_emudat(uint32_t emudat)
{
	__asm__ __volatile__("emudat = %0;" : : "d"(emudat));
	return emudat;
}

static inline uint32_t bfin_read_emudat(void)
{
	uint32_t emudat;
	__asm__ __volatile__("%0 = emudat;" : "=d"(emudat));
	return emudat;
}

static inline uint32_t bfin_write_emudat_chars(char a, char b, char c, char d)
{
	return bfin_write_emudat((a << 0) | (b << 8) | (c << 16) | (d << 24));
}

#define CIRC_SIZE 2048	/* see comment in tty_io.c:do_tty_write() */
#define CIRC_MASK (CIRC_SIZE - 1)
#define circ_empty(circ)     ((circ)->head == (circ)->tail)
#define circ_free(circ)      CIRC_SPACE((circ)->head, (circ)->tail, CIRC_SIZE)
#define circ_cnt(circ)       CIRC_CNT((circ)->head, (circ)->tail, CIRC_SIZE)
#define circ_byte(circ, idx) ((circ)->buf[(idx) & CIRC_MASK])

static struct tty_driver *bfin_jc_driver;
static struct task_struct *bfin_jc_kthread;
static struct tty_struct * volatile bfin_jc_tty;
static unsigned long bfin_jc_count;
static DEFINE_MUTEX(bfin_jc_tty_mutex);
static volatile struct circ_buf bfin_jc_write_buf;

static int
bfin_jc_emudat_manager(void *arg)
{
	uint32_t inbound_len = 0, outbound_len = 0;

	while (!kthread_should_stop()) {
		/* no one left to give data to, so sleep */
		if (bfin_jc_tty == NULL && circ_empty(&bfin_jc_write_buf)) {
			pr_debug("waiting for readers\n");
			__set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
			__set_current_state(TASK_RUNNING);
		}

		/* no data available, so just chill */
		if (!(bfin_read_DBGSTAT() & EMUDIF) && circ_empty(&bfin_jc_write_buf)) {
			pr_debug("waiting for data (in_len = %i) (circ: %i %i)\n",
				inbound_len, bfin_jc_write_buf.tail, bfin_jc_write_buf.head);
			if (inbound_len)
				schedule();
			else
				schedule_timeout_interruptible(HZ);
			continue;
		}

		/* if incoming data is ready, eat it */
		if (bfin_read_DBGSTAT() & EMUDIF) {
			struct tty_struct *tty;
			mutex_lock(&bfin_jc_tty_mutex);
			tty = (struct tty_struct *)bfin_jc_tty;
			if (tty != NULL) {
				uint32_t emudat = bfin_read_emudat();
				if (inbound_len == 0) {
					pr_debug("incoming length: 0x%08x\n", emudat);
					inbound_len = emudat;
				} else {
					size_t num_chars = (4 <= inbound_len ? 4 : inbound_len);
					pr_debug("  incoming data: 0x%08x (pushing %zu)\n", emudat, num_chars);
					inbound_len -= num_chars;
					tty_insert_flip_string(tty, (unsigned char *)&emudat, num_chars);
					tty_flip_buffer_push(tty);
				}
			}
			mutex_unlock(&bfin_jc_tty_mutex);
		}

		/* if outgoing data is ready, post it */
		if (!(bfin_read_DBGSTAT() & EMUDOF) && !circ_empty(&bfin_jc_write_buf)) {
			if (outbound_len == 0) {
				outbound_len = circ_cnt(&bfin_jc_write_buf);
				bfin_write_emudat(outbound_len);
				pr_debug("outgoing length: 0x%08x\n", outbound_len);
			} else {
				struct tty_struct *tty;
				int tail = bfin_jc_write_buf.tail;
				size_t ate = (4 <= outbound_len ? 4 : outbound_len);
				uint32_t emudat =
				bfin_write_emudat_chars(
					circ_byte(&bfin_jc_write_buf, tail + 0),
					circ_byte(&bfin_jc_write_buf, tail + 1),
					circ_byte(&bfin_jc_write_buf, tail + 2),
					circ_byte(&bfin_jc_write_buf, tail + 3)
				);
				bfin_jc_write_buf.tail += ate;
				outbound_len -= ate;
				mutex_lock(&bfin_jc_tty_mutex);
				tty = (struct tty_struct *)bfin_jc_tty;
				if (tty)
					tty_wakeup(tty);
				mutex_unlock(&bfin_jc_tty_mutex);
				pr_debug("  outgoing data: 0x%08x (pushing %zu)\n", emudat, ate);
			}
		}
	}

	__set_current_state(TASK_RUNNING);
	return 0;
}

static int
bfin_jc_open(struct tty_struct *tty, struct file *filp)
{
	mutex_lock(&bfin_jc_tty_mutex);
	pr_debug("open %lu\n", bfin_jc_count);
	++bfin_jc_count;
	bfin_jc_tty = tty;
	wake_up_process(bfin_jc_kthread);
	mutex_unlock(&bfin_jc_tty_mutex);
	return 0;
}

static void
bfin_jc_close(struct tty_struct *tty, struct file *filp)
{
	mutex_lock(&bfin_jc_tty_mutex);
	pr_debug("close %lu\n", bfin_jc_count);
	if (--bfin_jc_count == 0)
		bfin_jc_tty = NULL;
	wake_up_process(bfin_jc_kthread);
	mutex_unlock(&bfin_jc_tty_mutex);
}

/* XXX: we dont handle the put_char() case where we must handle count = 1 */
static int
bfin_jc_circ_write(const unsigned char *buf, int count)
{
	int i;
	count = min(count, circ_free(&bfin_jc_write_buf));
	pr_debug("going to write chunk of %i bytes\n", count);
	for (i = 0; i < count; ++i)
		circ_byte(&bfin_jc_write_buf, bfin_jc_write_buf.head + i) = buf[i];
	bfin_jc_write_buf.head += i;
	return i;
}

#ifndef CONFIG_BFIN_JTAG_COMM_CONSOLE
# define acquire_console_sem()
# define release_console_sem()
#endif
static int
bfin_jc_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int i;
	acquire_console_sem();
	i = bfin_jc_circ_write(buf, count);
	release_console_sem();
	wake_up_process(bfin_jc_kthread);
	return i;
}

static void
bfin_jc_flush_chars(struct tty_struct *tty)
{
	wake_up_process(bfin_jc_kthread);
}

static int
bfin_jc_write_room(struct tty_struct *tty)
{
	return circ_free(&bfin_jc_write_buf);
}

static int
bfin_jc_chars_in_buffer(struct tty_struct *tty)
{
	return circ_cnt(&bfin_jc_write_buf);
}

static void
bfin_jc_wait_until_sent(struct tty_struct *tty, int timeout)
{
	unsigned long expire = jiffies + timeout;
	while (!circ_empty(&bfin_jc_write_buf)) {
		if (signal_pending(current))
			break;
		if (time_after(jiffies, expire))
			break;
	}
}

static const struct tty_operations bfin_jc_ops = {
	.open            = bfin_jc_open,
	.close           = bfin_jc_close,
	.write           = bfin_jc_write,
	/*.put_char        = bfin_jc_put_char,*/
	.flush_chars     = bfin_jc_flush_chars,
	.write_room      = bfin_jc_write_room,
	.chars_in_buffer = bfin_jc_chars_in_buffer,
	.wait_until_sent = bfin_jc_wait_until_sent,
};

static int __init bfin_jc_init(void)
{
	int ret;

	bfin_jc_kthread = kthread_create(bfin_jc_emudat_manager, NULL, DRV_NAME);
	if (IS_ERR(bfin_jc_kthread))
		return PTR_ERR(bfin_jc_kthread);

	ret = -ENOMEM;

	bfin_jc_write_buf.head = bfin_jc_write_buf.tail = 0;
	bfin_jc_write_buf.buf = kmalloc(CIRC_SIZE, GFP_KERNEL);
	if (!bfin_jc_write_buf.buf)
		goto err;

	bfin_jc_driver = alloc_tty_driver(1);
	if (!bfin_jc_driver)
		goto err;

	bfin_jc_driver->owner        = THIS_MODULE;
	bfin_jc_driver->driver_name  = DRV_NAME;
	bfin_jc_driver->name         = DEV_NAME;
	bfin_jc_driver->type         = TTY_DRIVER_TYPE_SERIAL;
	bfin_jc_driver->subtype      = SERIAL_TYPE_NORMAL;
	bfin_jc_driver->init_termios = tty_std_termios;
	tty_set_operations(bfin_jc_driver, &bfin_jc_ops);

	ret = tty_register_driver(bfin_jc_driver);
	if (ret)
		goto err;

	pr_init(KERN_INFO DRV_NAME ": initialized\n");

	return 0;

 err:
	put_tty_driver(bfin_jc_driver);
	kfree(bfin_jc_write_buf.buf);
	kthread_stop(bfin_jc_kthread);
	return ret;
}
module_init(bfin_jc_init);

static void __exit bfin_jc_exit(void)
{
	kthread_stop(bfin_jc_kthread);
	kfree(bfin_jc_write_buf.buf);
	tty_unregister_driver(bfin_jc_driver);
	put_tty_driver(bfin_jc_driver);
}
module_exit(bfin_jc_exit);

#if defined(CONFIG_BFIN_JTAG_COMM_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
static void
bfin_jc_straight_buffer_write(const char *buf, unsigned count)
{
	unsigned ate = 0;
	while (bfin_read_DBGSTAT() & EMUDOF)
		continue;
	bfin_write_emudat(count);
	while (ate < count) {
		while (bfin_read_DBGSTAT() & EMUDOF)
			continue;
		bfin_write_emudat_chars(buf[ate], buf[ate+1], buf[ate+2], buf[ate+3]);
		ate += 4;
	}
}
#endif

#ifdef CONFIG_BFIN_JTAG_COMM_CONSOLE
static void
bfin_jc_console_write(struct console *co, const char *buf, unsigned count)
{
	if (bfin_jc_kthread == NULL)
		bfin_jc_straight_buffer_write(buf, count);
	else
		bfin_jc_circ_write(buf, count);
}

static struct tty_driver *
bfin_jc_console_device(struct console *co, int *index)
{
	*index = co->index;
	return bfin_jc_driver;
}

static struct console bfin_jc_console = {
	.name    = DEV_NAME,
	.write   = bfin_jc_console_write,
	.device  = bfin_jc_console_device,
	.flags   = CON_ANYTIME | CON_PRINTBUFFER,
	.index   = -1,
};

static int __init bfin_jc_console_init(void)
{
	register_console(&bfin_jc_console);
	return 0;
}
console_initcall(bfin_jc_console_init);
#endif

#ifdef CONFIG_EARLY_PRINTK
static void __init
bfin_jc_early_write(struct console *co, const char *buf, unsigned int count)
{
	bfin_jc_straight_buffer_write(buf, count);
}

static struct __initdata console bfin_jc_early_console = {
	.name   = "early_BFJC",
	.write   = bfin_jc_early_write,
	.flags   = CON_ANYTIME | CON_PRINTBUFFER,
	.index   = -1,
};

struct console * __init
bfin_jc_early_init(unsigned int port, unsigned int cflag)
{
	return &bfin_jc_early_console;
}
#endif

MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("TTY over Blackfin JTAG Communication");
MODULE_LICENSE("GPL");
