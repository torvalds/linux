/*
 * Simulated Serial Driver (fake serial)
 *
 * This driver is mostly used for bringup purposes and will go away.
 * It has a strong dependency on the system console. All outputs
 * are rerouted to the same facility as the one used by printk which, in our
 * case means sys_sim.c console (goes via the simulator). The code hereafter
 * is completely leveraged from the serial.c driver.
 *
 * Copyright (C) 1999-2000, 2002-2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 02/04/00 D. Mosberger	Merged in serial.c bug fixes in rs_close().
 * 02/25/00 D. Mosberger	Synced up with 2.3.99pre-5 version of serial.c.
 * 07/30/02 D. Mosberger	Replace sti()/cli() with explicit spinlocks & local irq masking
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/sysrq.h>

#include <asm/irq.h>
#include <asm/hpsim.h>
#include <asm/hw_irq.h>
#include <asm/uaccess.h>

#include "hpsim_ssc.h"

#undef SIMSERIAL_DEBUG	/* define this to get some debug information */

#define KEYBOARD_INTR	3	/* must match with simulator! */

#define NR_PORTS	1	/* only one port for now */

static char *serial_name = "SimSerial driver";
static char *serial_version = "0.6";

/*
 * This has been extracted from asm/serial.h. We need one eventually but
 * I don't know exactly what we're going to put in it so just fake one
 * for now.
 */
#define BASE_BAUD ( 1843200 / 16 )

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

/*
 * Most of the values here are meaningless to this particular driver.
 * However some values must be preserved for the code (leveraged from serial.c
 * to work correctly).
 * port must not be 0
 * type must not be UNKNOWN
 * So I picked arbitrary (guess from where?) values instead
 */
static struct serial_state rs_table[NR_PORTS]={
  /* UART CLK   PORT IRQ     FLAGS        */
  { BASE_BAUD, 0x3F8, 0, STD_COM_FLAGS, PORT_16550 }  /* ttyS0 */
};

/*
 * Just for the fun of it !
 */
static struct serial_uart_config uart_config[] = {
	{ "unknown", 1, 0 },
	{ "8250", 1, 0 },
	{ "16450", 1, 0 },
	{ "16550", 1, 0 },
	{ "16550A", 16, UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "cirrus", 1, 0 },
	{ "ST16650", 1, UART_CLEAR_FIFO | UART_STARTECH },
	{ "ST16650V2", 32, UART_CLEAR_FIFO | UART_USE_FIFO |
		  UART_STARTECH },
	{ "TI16750", 64, UART_CLEAR_FIFO | UART_USE_FIFO},
	{ NULL, 0}
};

struct tty_driver *hp_simserial_driver;

static struct console *console;

static unsigned char *tmp_buf;

extern struct console *console_drivers; /* from kernel/printk.c */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
#ifdef SIMSERIAL_DEBUG
	printk("rs_stop: tty->stopped=%d tty->hw_stopped=%d tty->flow_stopped=%d\n",
		tty->stopped, tty->hw_stopped, tty->flow_stopped);
#endif

}

static void rs_start(struct tty_struct *tty)
{
#ifdef SIMSERIAL_DEBUG
	printk("rs_start: tty->stopped=%d tty->hw_stopped=%d tty->flow_stopped=%d\n",
		tty->stopped, tty->hw_stopped, tty->flow_stopped);
#endif
}

static  void receive_chars(struct tty_struct *tty)
{
	unsigned char ch;
	static unsigned char seen_esc = 0;

	while ( (ch = ia64_ssc(0, 0, 0, 0, SSC_GETCHAR)) ) {
		if ( ch == 27 && seen_esc == 0 ) {
			seen_esc = 1;
			continue;
		} else {
			if ( seen_esc==1 && ch == 'O' ) {
				seen_esc = 2;
				continue;
			} else if ( seen_esc == 2 ) {
				if ( ch == 'P' ) /* F1 */
					show_state();
#ifdef CONFIG_MAGIC_SYSRQ
				if ( ch == 'S' ) { /* F4 */
					do
						ch = ia64_ssc(0, 0, 0, 0,
							      SSC_GETCHAR);
					while (!ch);
					handle_sysrq(ch);
				}
#endif
				seen_esc = 0;
				continue;
			}
		}
		seen_esc = 0;

		if (tty_insert_flip_char(tty, ch, TTY_NORMAL) == 0)
			break;
	}
	tty_flip_buffer_push(tty);
}

/*
 * This is the serial driver's interrupt routine for a single port
 */
static irqreturn_t rs_interrupt_single(int irq, void *dev_id)
{
	struct serial_state *info = dev_id;

	if (!info->tty) {
		printk(KERN_INFO "simrs_interrupt_single: info|tty=0 info=%p problem\n", info);
		return IRQ_NONE;
	}
	/*
	 * pretty simple in our case, because we only get interrupts
	 * on inbound traffic
	 */
	receive_chars(info->tty);
	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

static int rs_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	if (!tty || !info->xmit.buf)
		return 0;

	local_irq_save(flags);
	if (CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE) == 0) {
		local_irq_restore(flags);
		return 0;
	}
	info->xmit.buf[info->xmit.head] = ch;
	info->xmit.head = (info->xmit.head + 1) & (SERIAL_XMIT_SIZE-1);
	local_irq_restore(flags);
	return 1;
}

static void transmit_chars(struct tty_struct *tty, struct serial_state *info,
		int *intr_done)
{
	int count;
	unsigned long flags;

	local_irq_save(flags);

	if (info->x_char) {
		char c = info->x_char;

		console->write(console, &c, 1);

		info->icount.tx++;
		info->x_char = 0;

		goto out;
	}

	if (info->xmit.head == info->xmit.tail || tty->stopped ||
			tty->hw_stopped) {
#ifdef SIMSERIAL_DEBUG
		printk("transmit_chars: head=%d, tail=%d, stopped=%d\n",
		       info->xmit.head, info->xmit.tail, tty->stopped);
#endif
		goto out;
	}
	/*
	 * We removed the loop and try to do it in to chunks. We need
	 * 2 operations maximum because it's a ring buffer.
	 *
	 * First from current to tail if possible.
	 * Then from the beginning of the buffer until necessary
	 */

	count = min(CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE),
		    SERIAL_XMIT_SIZE - info->xmit.tail);
	console->write(console, info->xmit.buf+info->xmit.tail, count);

	info->xmit.tail = (info->xmit.tail+count) & (SERIAL_XMIT_SIZE-1);

	/*
	 * We have more at the beginning of the buffer
	 */
	count = CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
	if (count) {
		console->write(console, info->xmit.buf, count);
		info->xmit.tail += count;
	}
out:
	local_irq_restore(flags);
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	if (info->xmit.head == info->xmit.tail || tty->stopped || tty->hw_stopped ||
	    !info->xmit.buf)
		return;

	transmit_chars(tty, info, NULL);
}


static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	struct serial_state *info = tty->driver_data;
	int	c, ret = 0;
	unsigned long flags;

	if (!tty || !info->xmit.buf || !tmp_buf) return 0;

	local_irq_save(flags);
	while (1) {
		c = CIRC_SPACE_TO_END(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0) {
			break;
		}
		memcpy(info->xmit.buf + info->xmit.head, buf, c);
		info->xmit.head = ((info->xmit.head + c) &
				   (SERIAL_XMIT_SIZE-1));
		buf += c;
		count -= c;
		ret += c;
	}
	local_irq_restore(flags);
	/*
	 * Hey, we transmit directly from here in our case
	 */
	if (CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE)
	    && !tty->stopped && !tty->hw_stopped) {
		transmit_chars(tty, info, NULL);
	}
	return ret;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	local_irq_save(flags);
	info->xmit.head = info->xmit.tail = 0;
	local_irq_restore(flags);

	tty_wakeup(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void rs_send_xchar(struct tty_struct *tty, char ch)
{
	struct serial_state *info = tty->driver_data;

	info->x_char = ch;
	if (ch) {
		/*
		 * I guess we could call console->write() directly but
		 * let's do that for now.
		 */
		transmit_chars(tty, info, NULL);
	}
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	if (I_IXOFF(tty)) rs_send_xchar(tty, STOP_CHAR(tty));

	printk(KERN_INFO "simrs_throttle called\n");
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct serial_state *info = tty->driver_data;

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			rs_send_xchar(tty, START_CHAR(tty));
	}
	printk(KERN_INFO "simrs_unthrottle called\n");
}


static int rs_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
		case TIOCGSERIAL:
			printk(KERN_INFO "simrs_ioctl TIOCGSERIAL called\n");
			return 0;
		case TIOCSSERIAL:
			printk(KERN_INFO "simrs_ioctl TIOCSSERIAL called\n");
			return 0;
		case TIOCSERCONFIG:
			printk(KERN_INFO "rs_ioctl: TIOCSERCONFIG called\n");
			return -EINVAL;

		case TIOCSERGETLSR: /* Get line status register */
			printk(KERN_INFO "rs_ioctl: TIOCSERGETLSR called\n");
			return  -EINVAL;

		case TIOCSERGSTRUCT:
			printk(KERN_INFO "rs_ioctl: TIOCSERGSTRUCT called\n");
#if 0
			if (copy_to_user((struct async_struct *) arg,
					 info, sizeof(struct async_struct)))
				return -EFAULT;
#endif
			return 0;

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			printk(KERN_INFO "rs_ioctl: TIOCMIWAIT: called\n");
			return 0;
		case TIOCSERGWILD:
		case TIOCSERSWILD:
			/* "setserial -W" is called in Debian boot */
			printk (KERN_INFO "TIOCSER?WILD ioctl obsolete, ignored.\n");
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

static void rs_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
}
/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct tty_struct *tty, struct serial_state *info)
{
	unsigned long	flags;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

#ifdef SIMSERIAL_DEBUG
	printk("Shutting down serial port %d (irq %d)...\n", info->line,
	       info->irq);
#endif

	local_irq_save(flags);
	{
		if (info->irq)
			free_irq(info->irq, info);

		if (info->xmit.buf) {
			free_page((unsigned long) info->xmit.buf);
			info->xmit.buf = NULL;
		}

		set_bit(TTY_IO_ERROR, &tty->flags);

		info->flags &= ~ASYNC_INITIALIZED;
	}
	local_irq_restore(flags);
}

/*
 * ------------------------------------------------------------
 * rs_close()
 *
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	if (!info)
		return;

	local_irq_save(flags);
	if (tty_hung_up_p(filp)) {
#ifdef SIMSERIAL_DEBUG
		printk("rs_close: hung_up\n");
#endif
		local_irq_restore(flags);
		return;
	}
#ifdef SIMSERIAL_DEBUG
	printk("rs_close ttys%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "rs_close: bad serial port count for ttys%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		local_irq_restore(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	local_irq_restore(flags);

	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	shutdown(tty, info);
	rs_flush_buffer(tty);
	tty_ldisc_flush(tty);
	info->tty = NULL;
	if (info->blocked_open) {
		if (info->close_delay)
			schedule_timeout_interruptible(info->close_delay);
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
}


/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rs_hangup(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

#ifdef SIMSERIAL_DEBUG
	printk("rs_hangup: called\n");
#endif

	rs_flush_buffer(tty);
	if (info->flags & ASYNC_CLOSING)
		return;
	shutdown(tty, info);

	info->count = 0;
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}


static int startup(struct tty_struct *tty, struct serial_state *state)
{
	unsigned long flags;
	int	retval=0;
	unsigned long page;

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	local_irq_save(flags);

	if (state->flags & ASYNC_INITIALIZED) {
		free_page(page);
		goto errout;
	}

	if (!state->port || !state->type) {
		set_bit(TTY_IO_ERROR, &tty->flags);
		free_page(page);
		goto errout;
	}
	if (state->xmit.buf)
		free_page(page);
	else
		state->xmit.buf = (unsigned char *) page;

#ifdef SIMSERIAL_DEBUG
	printk("startup: ttys%d (irq %d)...", state->line, state->irq);
#endif

	/*
	 * Allocate the IRQ if necessary
	 */
	if (state->irq) {
		retval = request_irq(state->irq, rs_interrupt_single, 0,
				"simserial", state);
		if (retval)
			goto errout;
	}

	clear_bit(TTY_IO_ERROR, &tty->flags);

	state->xmit.head = state->xmit.tail = 0;

#if 0
	/*
	 * Set up serial timers...
	 */
	timer_table[RS_TIMER].expires = jiffies + 2*HZ/100;
	timer_active |= 1 << RS_TIMER;
#endif

	/*
	 * Set up the tty->alt_speed kludge
	 */
	if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
		tty->alt_speed = 57600;
	if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
		tty->alt_speed = 115200;
	if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
		tty->alt_speed = 230400;
	if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
		tty->alt_speed = 460800;

	state->flags |= ASYNC_INITIALIZED;
	local_irq_restore(flags);
	return 0;

errout:
	local_irq_restore(flags);
	return retval;
}


/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct serial_state *info = rs_table + tty->index;
	int			retval;
	unsigned long		page;

	info->count++;
	info->tty = tty;
	tty->driver_data = info;

#ifdef SIMSERIAL_DEBUG
	printk("rs_open %s, count = %d\n", tty->name, info->count);
#endif
	tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	if (!tmp_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}

	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) || (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * Start up serial port
	 */
	retval = startup(tty, info);
	if (retval) {
		return retval;
	}

	/*
	 * figure out which console to use (should be one already)
	 */
	console = console_drivers;
	while (console) {
		if ((console->flags & CON_ENABLED) && console->write) break;
		console = console->next;
	}

#ifdef SIMSERIAL_DEBUG
	printk("rs_open ttys%d successful\n", info->line);
#endif
	return 0;
}

/*
 * /proc fs routines....
 */

static inline void line_info(struct seq_file *m, struct serial_state *state)
{
	seq_printf(m, "%d: uart:%s port:%lX irq:%d\n",
		       state->line, uart_config[state->type].name,
		       state->port, state->irq);
}

static int rs_proc_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "simserinfo:1.0 driver:%s\n", serial_version);
	for (i = 0; i < NR_PORTS; i++)
		line_info(m, &rs_table[i]);
	return 0;
}

static int rs_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rs_proc_show, NULL);
}

static const struct file_operations rs_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= rs_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * ---------------------------------------------------------------------
 * rs_init() and friends
 *
 * rs_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static inline void show_serial_version(void)
{
	printk(KERN_INFO "%s version %s with", serial_name, serial_version);
	printk(KERN_INFO " no serial options enabled\n");
}

static const struct tty_operations hp_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.put_char = rs_put_char,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.chars_in_buffer = rs_chars_in_buffer,
	.flush_buffer = rs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,
	.unthrottle = rs_unthrottle,
	.send_xchar = rs_send_xchar,
	.set_termios = rs_set_termios,
	.stop = rs_stop,
	.start = rs_start,
	.hangup = rs_hangup,
	.wait_until_sent = rs_wait_until_sent,
	.proc_fops = &rs_proc_fops,
};

/*
 * The serial driver boot-time initialization code!
 */
static int __init
simrs_init (void)
{
	int			i, rc;
	struct serial_state	*state;

	if (!ia64_platform_is("hpsim"))
		return -ENODEV;

	hp_simserial_driver = alloc_tty_driver(NR_PORTS);
	if (!hp_simserial_driver)
		return -ENOMEM;

	show_serial_version();

	/* Initialize the tty_driver structure */

	hp_simserial_driver->driver_name = "simserial";
	hp_simserial_driver->name = "ttyS";
	hp_simserial_driver->major = TTY_MAJOR;
	hp_simserial_driver->minor_start = 64;
	hp_simserial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	hp_simserial_driver->subtype = SERIAL_TYPE_NORMAL;
	hp_simserial_driver->init_termios = tty_std_termios;
	hp_simserial_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	hp_simserial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(hp_simserial_driver, &hp_ops);

	/*
	 * Let's have a little bit of fun !
	 */
	for (i = 0, state = rs_table; i < NR_PORTS; i++,state++) {
		init_waitqueue_head(&state->open_wait);
		init_waitqueue_head(&state->close_wait);

		if (state->type == PORT_UNKNOWN) continue;

		if (!state->irq) {
			if ((rc = hpsim_get_irq(KEYBOARD_INTR)) < 0)
				panic("%s: out of interrupt vectors!\n",
				      __func__);
			state->irq = rc;
		}

		printk(KERN_INFO "ttyS%d at 0x%04lx (irq = %d) is a %s\n",
		       state->line,
		       state->port, state->irq,
		       uart_config[state->type].name);
	}

	if (tty_register_driver(hp_simserial_driver))
		panic("Couldn't register simserial driver\n");

	return 0;
}

#ifndef MODULE
__initcall(simrs_init);
#endif
