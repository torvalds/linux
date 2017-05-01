/*
 * Simulated Serial Driver (fake serial)
 *
 * This driver is mostly used for bringup purposes and will go away.
 * It has a strong dependency on the system console. All outputs
 * are rerouted to the same facility as the one used by printk which, in our
 * case means sys_sim.c console (goes via the simulator).
 *
 * Copyright (C) 1999-2000, 2002-2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/uaccess.h>

#include <asm/hpsim.h>

#include "hpsim_ssc.h"

#undef SIMSERIAL_DEBUG	/* define this to get some debug information */

#define KEYBOARD_INTR	3	/* must match with simulator! */

#define NR_PORTS	1	/* only one port for now */

struct serial_state {
	struct tty_port port;
	struct circ_buf xmit;
	int irq;
	int x_char;
};

static struct serial_state rs_table[NR_PORTS];

struct tty_driver *hp_simserial_driver;

static struct console *console;

static void receive_chars(struct tty_port *port)
{
	unsigned char ch;
	static unsigned char seen_esc = 0;

	while ( (ch = ia64_ssc(0, 0, 0, 0, SSC_GETCHAR)) ) {
		if (ch == 27 && seen_esc == 0) {
			seen_esc = 1;
			continue;
		} else if (seen_esc == 1 && ch == 'O') {
			seen_esc = 2;
			continue;
		} else if (seen_esc == 2) {
			if (ch == 'P') /* F1 */
				show_state();
#ifdef CONFIG_MAGIC_SYSRQ
			if (ch == 'S') { /* F4 */
				do {
					ch = ia64_ssc(0, 0, 0, 0, SSC_GETCHAR);
				} while (!ch);
				handle_sysrq(ch);
			}
#endif
			seen_esc = 0;
			continue;
		}
		seen_esc = 0;

		if (tty_insert_flip_char(port, ch, TTY_NORMAL) == 0)
			break;
	}
	tty_flip_buffer_push(port);
}

/*
 * This is the serial driver's interrupt routine for a single port
 */
static irqreturn_t rs_interrupt_single(int irq, void *dev_id)
{
	struct serial_state *info = dev_id;

	receive_chars(&info->port);

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

	if (!info->xmit.buf)
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

		info->x_char = 0;

		goto out;
	}

	if (info->xmit.head == info->xmit.tail || tty->stopped) {
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

	if (info->xmit.head == info->xmit.tail || tty->stopped ||
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

	if (!info->xmit.buf)
		return 0;

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
	if (CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE) &&
			!tty->stopped)
		transmit_chars(tty, info, NULL);

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
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));

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
		if (tty_io_error(tty))
		    return -EIO;
	}

	switch (cmd) {
	case TIOCGSERIAL:
	case TIOCSSERIAL:
	case TIOCSERGSTRUCT:
	case TIOCMIWAIT:
		return 0;
	case TIOCSERCONFIG:
	case TIOCSERGETLSR: /* Get line status register */
		return -EINVAL;
	case TIOCSERGWILD:
	case TIOCSERSWILD:
		/* "setserial -W" is called in Debian boot */
		printk (KERN_INFO "TIOCSER?WILD ioctl obsolete, ignored.\n");
		return 0;
	}
	return -ENOIOCTLCMD;
}

#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct tty_port *port)
{
	struct serial_state *info = container_of(port, struct serial_state,
			port);
	unsigned long flags;

	local_irq_save(flags);
	if (info->irq)
		free_irq(info->irq, info);

	if (info->xmit.buf) {
		free_page((unsigned long) info->xmit.buf);
		info->xmit.buf = NULL;
	}
	local_irq_restore(flags);
}

static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct serial_state *info = tty->driver_data;

	tty_port_close(&info->port, tty, filp);
}

static void rs_hangup(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	rs_flush_buffer(tty);
	tty_port_hangup(&info->port);
}

static int activate(struct tty_port *port, struct tty_struct *tty)
{
	struct serial_state *state = container_of(port, struct serial_state,
			port);
	unsigned long flags, page;
	int retval = 0;

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	local_irq_save(flags);

	if (state->xmit.buf)
		free_page(page);
	else
		state->xmit.buf = (unsigned char *) page;

	if (state->irq) {
		retval = request_irq(state->irq, rs_interrupt_single, 0,
				"simserial", state);
		if (retval)
			goto errout;
	}

	state->xmit.head = state->xmit.tail = 0;

	/*
	 * Set up the tty->alt_speed kludge
	 */
	if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
		tty->alt_speed = 57600;
	if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
		tty->alt_speed = 115200;
	if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
		tty->alt_speed = 230400;
	if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
		tty->alt_speed = 460800;

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
	struct tty_port *port = &info->port;

	tty->driver_data = info;
	port->low_latency = (port->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/*
	 * figure out which console to use (should be one already)
	 */
	console = console_drivers;
	while (console) {
		if ((console->flags & CON_ENABLED) && console->write) break;
		console = console->next;
	}

	return tty_port_open(port, tty, filp);
}

/*
 * /proc fs routines....
 */

static int rs_proc_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "simserinfo:1.0\n");
	for (i = 0; i < NR_PORTS; i++)
		seq_printf(m, "%d: uart:16550 port:3F8 irq:%d\n",
		       i, rs_table[i].irq);
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
	.hangup = rs_hangup,
	.proc_fops = &rs_proc_fops,
};

static const struct tty_port_operations hp_port_ops = {
	.activate = activate,
	.shutdown = shutdown,
};

static int __init simrs_init(void)
{
	struct serial_state *state;
	int retval;

	if (!ia64_platform_is("hpsim"))
		return -ENODEV;

	hp_simserial_driver = alloc_tty_driver(NR_PORTS);
	if (!hp_simserial_driver)
		return -ENOMEM;

	printk(KERN_INFO "SimSerial driver with no serial options enabled\n");

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

	state = rs_table;
	tty_port_init(&state->port);
	state->port.ops = &hp_port_ops;
	state->port.close_delay = 0; /* XXX really 0? */

	retval = hpsim_get_irq(KEYBOARD_INTR);
	if (retval < 0) {
		printk(KERN_ERR "%s: out of interrupt vectors!\n",
				__func__);
		goto err_free_tty;
	}

	state->irq = retval;

	/* the port is imaginary */
	printk(KERN_INFO "ttyS0 at 0x03f8 (irq = %d) is a 16550\n", state->irq);

	tty_port_link_device(&state->port, hp_simserial_driver, 0);
	retval = tty_register_driver(hp_simserial_driver);
	if (retval) {
		printk(KERN_ERR "Couldn't register simserial driver\n");
		goto err_free_tty;
	}

	return 0;
err_free_tty:
	put_tty_driver(hp_simserial_driver);
	tty_port_destroy(&state->port);
	return retval;
}

#ifndef MODULE
__initcall(simrs_init);
#endif
