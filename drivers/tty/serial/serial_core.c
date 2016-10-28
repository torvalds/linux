/*
 *  Driver core for serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/serial.h> /* for serial_state and serial_icounter_struct */
#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

/*
 * This is used to lock changes in serial line configuration.
 */
static DEFINE_MUTEX(port_mutex);

/*
 * lockdep: port->lock is initialized in two places, but we
 *          want only one lock-class:
 */
static struct lock_class_key port_lock_key;

#define HIGH_BITS_OFFSET	((sizeof(long)-sizeof(int))*8)

static void uart_change_speed(struct tty_struct *tty, struct uart_state *state,
					struct ktermios *old_termios);
static void uart_wait_until_sent(struct tty_struct *tty, int timeout);
static void uart_change_pm(struct uart_state *state,
			   enum uart_pm_state pm_state);

static void uart_port_shutdown(struct tty_port *port);

static int uart_dcd_enabled(struct uart_port *uport)
{
	return !!(uport->status & UPSTAT_DCD_ENABLE);
}

static inline struct uart_port *uart_port_ref(struct uart_state *state)
{
	if (atomic_add_unless(&state->refcount, 1, 0))
		return state->uart_port;
	return NULL;
}

static inline void uart_port_deref(struct uart_port *uport)
{
	if (uport && atomic_dec_and_test(&uport->state->refcount))
		wake_up(&uport->state->remove_wait);
}

#define uart_port_lock(state, flags)					\
	({								\
		struct uart_port *__uport = uart_port_ref(state);	\
		if (__uport)						\
			spin_lock_irqsave(&__uport->lock, flags);	\
		__uport;						\
	})

#define uart_port_unlock(uport, flags)					\
	({								\
		struct uart_port *__uport = uport;			\
		if (__uport)						\
			spin_unlock_irqrestore(&__uport->lock, flags);	\
		uart_port_deref(__uport);				\
	})

static inline struct uart_port *uart_port_check(struct uart_state *state)
{
	lockdep_assert_held(&state->port.mutex);
	return state->uart_port;
}

/*
 * This routine is used by the interrupt handler to schedule processing in
 * the software interrupt portion of the driver.
 */
void uart_write_wakeup(struct uart_port *port)
{
	struct uart_state *state = port->state;
	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	BUG_ON(!state);
	tty_wakeup(state->port.tty);
}

static void uart_stop(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	port = uart_port_lock(state, flags);
	if (port)
		port->ops->stop_tx(port);
	uart_port_unlock(port, flags);
}

static void __uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;

	if (port && !uart_tx_stopped(port))
		port->ops->start_tx(port);
}

static void uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	port = uart_port_lock(state, flags);
	__uart_start(tty);
	uart_port_unlock(port, flags);
}

static void
uart_update_mctrl(struct uart_port *port, unsigned int set, unsigned int clear)
{
	unsigned long flags;
	unsigned int old;

	spin_lock_irqsave(&port->lock, flags);
	old = port->mctrl;
	port->mctrl = (old & ~clear) | set;
	if (old != port->mctrl)
		port->ops->set_mctrl(port, port->mctrl);
	spin_unlock_irqrestore(&port->lock, flags);
}

#define uart_set_mctrl(port, set)	uart_update_mctrl(port, set, 0)
#define uart_clear_mctrl(port, clear)	uart_update_mctrl(port, 0, clear)

/*
 * Startup the port.  This will be called once per open.  All calls
 * will be serialised by the per-port mutex.
 */
static int uart_port_startup(struct tty_struct *tty, struct uart_state *state,
		int init_hw)
{
	struct uart_port *uport = uart_port_check(state);
	unsigned long page;
	int retval = 0;

	if (uport->type == PORT_UNKNOWN)
		return 1;

	/*
	 * Make sure the device is in D0 state.
	 */
	uart_change_pm(state, UART_PM_STATE_ON);

	/*
	 * Initialise and allocate the transmit and temporary
	 * buffer.
	 */
	if (!state->xmit.buf) {
		/* This is protected by the per port mutex */
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		state->xmit.buf = (unsigned char *) page;
		uart_circ_clear(&state->xmit);
	}

	retval = uport->ops->startup(uport);
	if (retval == 0) {
		if (uart_console(uport) && uport->cons->cflag) {
			tty->termios.c_cflag = uport->cons->cflag;
			uport->cons->cflag = 0;
		}
		/*
		 * Initialise the hardware port settings.
		 */
		uart_change_speed(tty, state, NULL);

		/*
		 * Setup the RTS and DTR signals once the
		 * port is open and ready to respond.
		 */
		if (init_hw && C_BAUD(tty))
			uart_set_mctrl(uport, TIOCM_RTS | TIOCM_DTR);
	}

	/*
	 * This is to allow setserial on this port. People may want to set
	 * port/irq/type and then reconfigure the port properly if it failed
	 * now.
	 */
	if (retval && capable(CAP_SYS_ADMIN))
		return 1;

	return retval;
}

static int uart_startup(struct tty_struct *tty, struct uart_state *state,
		int init_hw)
{
	struct tty_port *port = &state->port;
	int retval;

	if (tty_port_initialized(port))
		return 0;

	retval = uart_port_startup(tty, state, init_hw);
	if (retval)
		set_bit(TTY_IO_ERROR, &tty->flags);

	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.  Calls to
 * uart_shutdown are serialised by the per-port semaphore.
 *
 * uport == NULL if uart_port has already been removed
 */
static void uart_shutdown(struct tty_struct *tty, struct uart_state *state)
{
	struct uart_port *uport = uart_port_check(state);
	struct tty_port *port = &state->port;

	/*
	 * Set the TTY IO error marker
	 */
	if (tty)
		set_bit(TTY_IO_ERROR, &tty->flags);

	if (tty_port_initialized(port)) {
		tty_port_set_initialized(port, 0);

		/*
		 * Turn off DTR and RTS early.
		 */
		if (uport && uart_console(uport) && tty)
			uport->cons->cflag = tty->termios.c_cflag;

		if (!tty || C_HUPCL(tty))
			uart_clear_mctrl(uport, TIOCM_DTR | TIOCM_RTS);

		uart_port_shutdown(port);
	}

	/*
	 * It's possible for shutdown to be called after suspend if we get
	 * a DCD drop (hangup) at just the right time.  Clear suspended bit so
	 * we don't try to resume a port that has been shutdown.
	 */
	tty_port_set_suspended(port, 0);

	/*
	 * Free the transmit buffer page.
	 */
	if (state->xmit.buf) {
		free_page((unsigned long)state->xmit.buf);
		state->xmit.buf = NULL;
	}
}

/**
 *	uart_update_timeout - update per-port FIFO timeout.
 *	@port:  uart_port structure describing the port
 *	@cflag: termios cflag value
 *	@baud:  speed of the port
 *
 *	Set the port FIFO timeout value.  The @cflag value should
 *	reflect the actual hardware settings.
 */
void
uart_update_timeout(struct uart_port *port, unsigned int cflag,
		    unsigned int baud)
{
	unsigned int bits;

	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5:
		bits = 7;
		break;
	case CS6:
		bits = 8;
		break;
	case CS7:
		bits = 9;
		break;
	default:
		bits = 10;
		break; /* CS8 */
	}

	if (cflag & CSTOPB)
		bits++;
	if (cflag & PARENB)
		bits++;

	/*
	 * The total number of bits to be transmitted in the fifo.
	 */
	bits = bits * port->fifosize;

	/*
	 * Figure the timeout to send the above number of bits.
	 * Add .02 seconds of slop
	 */
	port->timeout = (HZ * bits) / baud + HZ/50;
}

EXPORT_SYMBOL(uart_update_timeout);

/**
 *	uart_get_baud_rate - return baud rate for a particular port
 *	@port: uart_port structure describing the port in question.
 *	@termios: desired termios settings.
 *	@old: old termios (or NULL)
 *	@min: minimum acceptable baud rate
 *	@max: maximum acceptable baud rate
 *
 *	Decode the termios structure into a numeric baud rate,
 *	taking account of the magic 38400 baud rate (with spd_*
 *	flags), and mapping the %B0 rate to 9600 baud.
 *
 *	If the new baud rate is invalid, try the old termios setting.
 *	If it's still invalid, we try 9600 baud.
 *
 *	Update the @termios structure to reflect the baud rate
 *	we're actually going to be using. Don't do this for the case
 *	where B0 is requested ("hang up").
 */
unsigned int
uart_get_baud_rate(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old, unsigned int min, unsigned int max)
{
	unsigned int try;
	unsigned int baud;
	unsigned int altbaud;
	int hung_up = 0;
	upf_t flags = port->flags & UPF_SPD_MASK;

	switch (flags) {
	case UPF_SPD_HI:
		altbaud = 57600;
		break;
	case UPF_SPD_VHI:
		altbaud = 115200;
		break;
	case UPF_SPD_SHI:
		altbaud = 230400;
		break;
	case UPF_SPD_WARP:
		altbaud = 460800;
		break;
	default:
		altbaud = 38400;
		break;
	}

	for (try = 0; try < 2; try++) {
		baud = tty_termios_baud_rate(termios);

		/*
		 * The spd_hi, spd_vhi, spd_shi, spd_warp kludge...
		 * Die! Die! Die!
		 */
		if (try == 0 && baud == 38400)
			baud = altbaud;

		/*
		 * Special case: B0 rate.
		 */
		if (baud == 0) {
			hung_up = 1;
			baud = 9600;
		}

		if (baud >= min && baud <= max)
			return baud;

		/*
		 * Oops, the quotient was zero.  Try again with
		 * the old baud rate if possible.
		 */
		termios->c_cflag &= ~CBAUD;
		if (old) {
			baud = tty_termios_baud_rate(old);
			if (!hung_up)
				tty_termios_encode_baud_rate(termios,
								baud, baud);
			old = NULL;
			continue;
		}

		/*
		 * As a last resort, if the range cannot be met then clip to
		 * the nearest chip supported rate.
		 */
		if (!hung_up) {
			if (baud <= min)
				tty_termios_encode_baud_rate(termios,
							min + 1, min + 1);
			else
				tty_termios_encode_baud_rate(termios,
							max - 1, max - 1);
		}
	}
	/* Should never happen */
	WARN_ON(1);
	return 0;
}

EXPORT_SYMBOL(uart_get_baud_rate);

/**
 *	uart_get_divisor - return uart clock divisor
 *	@port: uart_port structure describing the port.
 *	@baud: desired baud rate
 *
 *	Calculate the uart clock divisor for the port.
 */
unsigned int
uart_get_divisor(struct uart_port *port, unsigned int baud)
{
	unsigned int quot;

	/*
	 * Old custom speed handling.
	 */
	if (baud == 38400 && (port->flags & UPF_SPD_MASK) == UPF_SPD_CUST)
		quot = port->custom_divisor;
	else
		quot = DIV_ROUND_CLOSEST(port->uartclk, 16 * baud);

	return quot;
}

EXPORT_SYMBOL(uart_get_divisor);

/* Caller holds port mutex */
static void uart_change_speed(struct tty_struct *tty, struct uart_state *state,
					struct ktermios *old_termios)
{
	struct uart_port *uport = uart_port_check(state);
	struct ktermios *termios;
	int hw_stopped;

	/*
	 * If we have no tty, termios, or the port does not exist,
	 * then we can't set the parameters for this port.
	 */
	if (!tty || uport->type == PORT_UNKNOWN)
		return;

	termios = &tty->termios;
	uport->ops->set_termios(uport, termios, old_termios);

	/*
	 * Set modem status enables based on termios cflag
	 */
	spin_lock_irq(&uport->lock);
	if (termios->c_cflag & CRTSCTS)
		uport->status |= UPSTAT_CTS_ENABLE;
	else
		uport->status &= ~UPSTAT_CTS_ENABLE;

	if (termios->c_cflag & CLOCAL)
		uport->status &= ~UPSTAT_DCD_ENABLE;
	else
		uport->status |= UPSTAT_DCD_ENABLE;

	/* reset sw-assisted CTS flow control based on (possibly) new mode */
	hw_stopped = uport->hw_stopped;
	uport->hw_stopped = uart_softcts_mode(uport) &&
				!(uport->ops->get_mctrl(uport) & TIOCM_CTS);
	if (uport->hw_stopped) {
		if (!hw_stopped)
			uport->ops->stop_tx(uport);
	} else {
		if (hw_stopped)
			__uart_start(tty);
	}
	spin_unlock_irq(&uport->lock);
}

static int uart_put_char(struct tty_struct *tty, unsigned char c)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	struct circ_buf *circ;
	unsigned long flags;
	int ret = 0;

	circ = &state->xmit;
	if (!circ->buf)
		return 0;

	port = uart_port_lock(state, flags);
	if (port && uart_circ_chars_free(circ) != 0) {
		circ->buf[circ->head] = c;
		circ->head = (circ->head + 1) & (UART_XMIT_SIZE - 1);
		ret = 1;
	}
	uart_port_unlock(port, flags);
	return ret;
}

static void uart_flush_chars(struct tty_struct *tty)
{
	uart_start(tty);
}

static int uart_write(struct tty_struct *tty,
					const unsigned char *buf, int count)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	struct circ_buf *circ;
	unsigned long flags;
	int c, ret = 0;

	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	if (!state) {
		WARN_ON(1);
		return -EL3HLT;
	}

	circ = &state->xmit;
	if (!circ->buf)
		return 0;

	port = uart_port_lock(state, flags);
	while (port) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(circ->buf + circ->head, buf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}

	__uart_start(tty);
	uart_port_unlock(port, flags);
	return ret;
}

static int uart_write_room(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;
	int ret;

	port = uart_port_lock(state, flags);
	ret = uart_circ_chars_free(&state->xmit);
	uart_port_unlock(port, flags);
	return ret;
}

static int uart_chars_in_buffer(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;
	int ret;

	port = uart_port_lock(state, flags);
	ret = uart_circ_chars_pending(&state->xmit);
	uart_port_unlock(port, flags);
	return ret;
}

static void uart_flush_buffer(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	if (!state) {
		WARN_ON(1);
		return;
	}

	pr_debug("uart_flush_buffer(%d) called\n", tty->index);

	port = uart_port_lock(state, flags);
	if (!port)
		return;
	uart_circ_clear(&state->xmit);
	if (port->ops->flush_buffer)
		port->ops->flush_buffer(port);
	uart_port_unlock(port, flags);
	tty_wakeup(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	port = uart_port_ref(state);
	if (!port)
		return;

	if (port->ops->send_xchar)
		port->ops->send_xchar(port, ch);
	else {
		spin_lock_irqsave(&port->lock, flags);
		port->x_char = ch;
		if (ch)
			port->ops->start_tx(port);
		spin_unlock_irqrestore(&port->lock, flags);
	}
	uart_port_deref(port);
}

static void uart_throttle(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	upstat_t mask = 0;

	port = uart_port_ref(state);
	if (!port)
		return;

	if (I_IXOFF(tty))
		mask |= UPSTAT_AUTOXOFF;
	if (C_CRTSCTS(tty))
		mask |= UPSTAT_AUTORTS;

	if (port->status & mask) {
		port->ops->throttle(port);
		mask &= ~port->status;
	}

	if (mask & UPSTAT_AUTORTS)
		uart_clear_mctrl(port, TIOCM_RTS);

	if (mask & UPSTAT_AUTOXOFF)
		uart_send_xchar(tty, STOP_CHAR(tty));

	uart_port_deref(port);
}

static void uart_unthrottle(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	upstat_t mask = 0;

	port = uart_port_ref(state);
	if (!port)
		return;

	if (I_IXOFF(tty))
		mask |= UPSTAT_AUTOXOFF;
	if (C_CRTSCTS(tty))
		mask |= UPSTAT_AUTORTS;

	if (port->status & mask) {
		port->ops->unthrottle(port);
		mask &= ~port->status;
	}

	if (mask & UPSTAT_AUTORTS)
		uart_set_mctrl(port, TIOCM_RTS);

	if (mask & UPSTAT_AUTOXOFF)
		uart_send_xchar(tty, START_CHAR(tty));

	uart_port_deref(port);
}

static int uart_get_info(struct tty_port *port, struct serial_struct *retinfo)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport;
	int ret = -ENODEV;

	memset(retinfo, 0, sizeof(*retinfo));

	/*
	 * Ensure the state we copy is consistent and no hardware changes
	 * occur as we go
	 */
	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	retinfo->type	    = uport->type;
	retinfo->line	    = uport->line;
	retinfo->port	    = uport->iobase;
	if (HIGH_BITS_OFFSET)
		retinfo->port_high = (long) uport->iobase >> HIGH_BITS_OFFSET;
	retinfo->irq		    = uport->irq;
	retinfo->flags	    = uport->flags;
	retinfo->xmit_fifo_size  = uport->fifosize;
	retinfo->baud_base	    = uport->uartclk / 16;
	retinfo->close_delay	    = jiffies_to_msecs(port->close_delay) / 10;
	retinfo->closing_wait    = port->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				ASYNC_CLOSING_WAIT_NONE :
				jiffies_to_msecs(port->closing_wait) / 10;
	retinfo->custom_divisor  = uport->custom_divisor;
	retinfo->hub6	    = uport->hub6;
	retinfo->io_type         = uport->iotype;
	retinfo->iomem_reg_shift = uport->regshift;
	retinfo->iomem_base      = (void *)(unsigned long)uport->mapbase;

	ret = 0;
out:
	mutex_unlock(&port->mutex);
	return ret;
}

static int uart_get_info_user(struct tty_port *port,
			 struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (uart_get_info(port, &tmp) < 0)
		return -EIO;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int uart_set_info(struct tty_struct *tty, struct tty_port *port,
			 struct uart_state *state,
			 struct serial_struct *new_info)
{
	struct uart_port *uport = uart_port_check(state);
	unsigned long new_port;
	unsigned int change_irq, change_port, closing_wait;
	unsigned int old_custom_divisor, close_delay;
	upf_t old_flags, new_flags;
	int retval = 0;

	if (!uport)
		return -EIO;

	new_port = new_info->port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_info->port_high << HIGH_BITS_OFFSET;

	new_info->irq = irq_canonicalize(new_info->irq);
	close_delay = msecs_to_jiffies(new_info->close_delay * 10);
	closing_wait = new_info->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE :
			msecs_to_jiffies(new_info->closing_wait * 10);


	change_irq  = !(uport->flags & UPF_FIXED_PORT)
		&& new_info->irq != uport->irq;

	/*
	 * Since changing the 'type' of the port changes its resource
	 * allocations, we should treat type changes the same as
	 * IO port changes.
	 */
	change_port = !(uport->flags & UPF_FIXED_PORT)
		&& (new_port != uport->iobase ||
		    (unsigned long)new_info->iomem_base != uport->mapbase ||
		    new_info->hub6 != uport->hub6 ||
		    new_info->io_type != uport->iotype ||
		    new_info->iomem_reg_shift != uport->regshift ||
		    new_info->type != uport->type);

	old_flags = uport->flags;
	new_flags = new_info->flags;
	old_custom_divisor = uport->custom_divisor;

	if (!capable(CAP_SYS_ADMIN)) {
		retval = -EPERM;
		if (change_irq || change_port ||
		    (new_info->baud_base != uport->uartclk / 16) ||
		    (close_delay != port->close_delay) ||
		    (closing_wait != port->closing_wait) ||
		    (new_info->xmit_fifo_size &&
		     new_info->xmit_fifo_size != uport->fifosize) ||
		    (((new_flags ^ old_flags) & ~UPF_USR_MASK) != 0))
			goto exit;
		uport->flags = ((uport->flags & ~UPF_USR_MASK) |
			       (new_flags & UPF_USR_MASK));
		uport->custom_divisor = new_info->custom_divisor;
		goto check_and_exit;
	}

	/*
	 * Ask the low level driver to verify the settings.
	 */
	if (uport->ops->verify_port)
		retval = uport->ops->verify_port(uport, new_info);

	if ((new_info->irq >= nr_irqs) || (new_info->irq < 0) ||
	    (new_info->baud_base < 9600))
		retval = -EINVAL;

	if (retval)
		goto exit;

	if (change_port || change_irq) {
		retval = -EBUSY;

		/*
		 * Make sure that we are the sole user of this port.
		 */
		if (tty_port_users(port) > 1)
			goto exit;

		/*
		 * We need to shutdown the serial port at the old
		 * port/type/irq combination.
		 */
		uart_shutdown(tty, state);
	}

	if (change_port) {
		unsigned long old_iobase, old_mapbase;
		unsigned int old_type, old_iotype, old_hub6, old_shift;

		old_iobase = uport->iobase;
		old_mapbase = uport->mapbase;
		old_type = uport->type;
		old_hub6 = uport->hub6;
		old_iotype = uport->iotype;
		old_shift = uport->regshift;

		/*
		 * Free and release old regions
		 */
		if (old_type != PORT_UNKNOWN && uport->ops->release_port)
			uport->ops->release_port(uport);

		uport->iobase = new_port;
		uport->type = new_info->type;
		uport->hub6 = new_info->hub6;
		uport->iotype = new_info->io_type;
		uport->regshift = new_info->iomem_reg_shift;
		uport->mapbase = (unsigned long)new_info->iomem_base;

		/*
		 * Claim and map the new regions
		 */
		if (uport->type != PORT_UNKNOWN && uport->ops->request_port) {
			retval = uport->ops->request_port(uport);
		} else {
			/* Always success - Jean II */
			retval = 0;
		}

		/*
		 * If we fail to request resources for the
		 * new port, try to restore the old settings.
		 */
		if (retval) {
			uport->iobase = old_iobase;
			uport->type = old_type;
			uport->hub6 = old_hub6;
			uport->iotype = old_iotype;
			uport->regshift = old_shift;
			uport->mapbase = old_mapbase;

			if (old_type != PORT_UNKNOWN) {
				retval = uport->ops->request_port(uport);
				/*
				 * If we failed to restore the old settings,
				 * we fail like this.
				 */
				if (retval)
					uport->type = PORT_UNKNOWN;

				/*
				 * We failed anyway.
				 */
				retval = -EBUSY;
			}

			/* Added to return the correct error -Ram Gupta */
			goto exit;
		}
	}

	if (change_irq)
		uport->irq      = new_info->irq;
	if (!(uport->flags & UPF_FIXED_PORT))
		uport->uartclk  = new_info->baud_base * 16;
	uport->flags            = (uport->flags & ~UPF_CHANGE_MASK) |
				 (new_flags & UPF_CHANGE_MASK);
	uport->custom_divisor   = new_info->custom_divisor;
	port->close_delay     = close_delay;
	port->closing_wait    = closing_wait;
	if (new_info->xmit_fifo_size)
		uport->fifosize = new_info->xmit_fifo_size;
	port->low_latency = (uport->flags & UPF_LOW_LATENCY) ? 1 : 0;

 check_and_exit:
	retval = 0;
	if (uport->type == PORT_UNKNOWN)
		goto exit;
	if (tty_port_initialized(port)) {
		if (((old_flags ^ uport->flags) & UPF_SPD_MASK) ||
		    old_custom_divisor != uport->custom_divisor) {
			/*
			 * If they're setting up a custom divisor or speed,
			 * instead of clearing it, then bitch about it. No
			 * need to rate-limit; it's CAP_SYS_ADMIN only.
			 */
			if (uport->flags & UPF_SPD_MASK) {
				dev_notice(uport->dev,
				       "%s sets custom speed on %s. This is deprecated.\n",
				      current->comm,
				      tty_name(port->tty));
			}
			uart_change_speed(tty, state, NULL);
		}
	} else {
		retval = uart_startup(tty, state, 1);
		if (retval > 0)
			retval = 0;
	}
 exit:
	return retval;
}

static int uart_set_info_user(struct tty_struct *tty, struct uart_state *state,
			 struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	struct tty_port *port = &state->port;
	int retval;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	/*
	 * This semaphore protects port->count.  It is also
	 * very useful to prevent opens.  Also, take the
	 * port configuration semaphore to make sure that a
	 * module insertion/removal doesn't change anything
	 * under us.
	 */
	mutex_lock(&port->mutex);
	retval = uart_set_info(tty, port, state, &new_serial);
	mutex_unlock(&port->mutex);
	return retval;
}

/**
 *	uart_get_lsr_info	-	get line status register info
 *	@tty: tty associated with the UART
 *	@state: UART being queried
 *	@value: returned modem value
 */
static int uart_get_lsr_info(struct tty_struct *tty,
			struct uart_state *state, unsigned int __user *value)
{
	struct uart_port *uport = uart_port_check(state);
	unsigned int result;

	result = uport->ops->tx_empty(uport);

	/*
	 * If we're about to load something into the transmit
	 * register, we'll pretend the transmitter isn't empty to
	 * avoid a race condition (depending on when the transmit
	 * interrupt happens).
	 */
	if (uport->x_char ||
	    ((uart_circ_chars_pending(&state->xmit) > 0) &&
	     !uart_tx_stopped(uport)))
		result &= ~TIOCSER_TEMT;

	return put_user(result, value);
}

static int uart_tiocmget(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	int result = -EIO;

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	if (!tty_io_error(tty)) {
		result = uport->mctrl;
		spin_lock_irq(&uport->lock);
		result |= uport->ops->get_mctrl(uport);
		spin_unlock_irq(&uport->lock);
	}
out:
	mutex_unlock(&port->mutex);
	return result;
}

static int
uart_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	int ret = -EIO;

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	if (!tty_io_error(tty)) {
		uart_update_mctrl(uport, set, clear);
		ret = 0;
	}
out:
	mutex_unlock(&port->mutex);
	return ret;
}

static int uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	int ret = -EIO;

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	if (uport->type != PORT_UNKNOWN)
		uport->ops->break_ctl(uport, break_state);
	ret = 0;
out:
	mutex_unlock(&port->mutex);
	return ret;
}

static int uart_do_autoconfig(struct tty_struct *tty,struct uart_state *state)
{
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	int flags, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Take the per-port semaphore.  This prevents count from
	 * changing, and hence any extra opens of the port while
	 * we're auto-configuring.
	 */
	if (mutex_lock_interruptible(&port->mutex))
		return -ERESTARTSYS;

	uport = uart_port_check(state);
	if (!uport) {
		ret = -EIO;
		goto out;
	}

	ret = -EBUSY;
	if (tty_port_users(port) == 1) {
		uart_shutdown(tty, state);

		/*
		 * If we already have a port type configured,
		 * we must release its resources.
		 */
		if (uport->type != PORT_UNKNOWN && uport->ops->release_port)
			uport->ops->release_port(uport);

		flags = UART_CONFIG_TYPE;
		if (uport->flags & UPF_AUTO_IRQ)
			flags |= UART_CONFIG_IRQ;

		/*
		 * This will claim the ports resources if
		 * a port is found.
		 */
		uport->ops->config_port(uport, flags);

		ret = uart_startup(tty, state, 1);
		if (ret > 0)
			ret = 0;
	}
out:
	mutex_unlock(&port->mutex);
	return ret;
}

static void uart_enable_ms(struct uart_port *uport)
{
	/*
	 * Force modem status interrupts on
	 */
	if (uport->ops->enable_ms)
		uport->ops->enable_ms(uport);
}

/*
 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
 * - mask passed in arg for lines of interest
 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
 * Caller should use TIOCGICOUNT to see which one it was
 *
 * FIXME: This wants extracting into a common all driver implementation
 * of TIOCMWAIT using tty_port.
 */
static int uart_wait_modem_status(struct uart_state *state, unsigned long arg)
{
	struct uart_port *uport;
	struct tty_port *port = &state->port;
	DECLARE_WAITQUEUE(wait, current);
	struct uart_icount cprev, cnow;
	int ret;

	/*
	 * note the counters on entry
	 */
	uport = uart_port_ref(state);
	if (!uport)
		return -EIO;
	spin_lock_irq(&uport->lock);
	memcpy(&cprev, &uport->icount, sizeof(struct uart_icount));
	uart_enable_ms(uport);
	spin_unlock_irq(&uport->lock);

	add_wait_queue(&port->delta_msr_wait, &wait);
	for (;;) {
		spin_lock_irq(&uport->lock);
		memcpy(&cnow, &uport->icount, sizeof(struct uart_icount));
		spin_unlock_irq(&uport->lock);

		set_current_state(TASK_INTERRUPTIBLE);

		if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
		    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
		    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
		    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
			ret = 0;
			break;
		}

		schedule();

		/* see if a signal did it */
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cprev = cnow;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->delta_msr_wait, &wait);
	uart_port_deref(uport);

	return ret;
}

/*
 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
 * Return: write counters to the user passed counter struct
 * NB: both 1->0 and 0->1 transitions are counted except for
 *     RI where only 0->1 is counted.
 */
static int uart_get_icount(struct tty_struct *tty,
			  struct serial_icounter_struct *icount)
{
	struct uart_state *state = tty->driver_data;
	struct uart_icount cnow;
	struct uart_port *uport;

	uport = uart_port_ref(state);
	if (!uport)
		return -EIO;
	spin_lock_irq(&uport->lock);
	memcpy(&cnow, &uport->icount, sizeof(struct uart_icount));
	spin_unlock_irq(&uport->lock);
	uart_port_deref(uport);

	icount->cts         = cnow.cts;
	icount->dsr         = cnow.dsr;
	icount->rng         = cnow.rng;
	icount->dcd         = cnow.dcd;
	icount->rx          = cnow.rx;
	icount->tx          = cnow.tx;
	icount->frame       = cnow.frame;
	icount->overrun     = cnow.overrun;
	icount->parity      = cnow.parity;
	icount->brk         = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

static int uart_get_rs485_config(struct uart_port *port,
			 struct serial_rs485 __user *rs485)
{
	unsigned long flags;
	struct serial_rs485 aux;

	spin_lock_irqsave(&port->lock, flags);
	aux = port->rs485;
	spin_unlock_irqrestore(&port->lock, flags);

	if (copy_to_user(rs485, &aux, sizeof(aux)))
		return -EFAULT;

	return 0;
}

static int uart_set_rs485_config(struct uart_port *port,
			 struct serial_rs485 __user *rs485_user)
{
	struct serial_rs485 rs485;
	int ret;
	unsigned long flags;

	if (!port->rs485_config)
		return -ENOIOCTLCMD;

	if (copy_from_user(&rs485, rs485_user, sizeof(*rs485_user)))
		return -EFAULT;

	spin_lock_irqsave(&port->lock, flags);
	ret = port->rs485_config(port, &rs485);
	spin_unlock_irqrestore(&port->lock, flags);
	if (ret)
		return ret;

	if (copy_to_user(rs485_user, &port->rs485, sizeof(port->rs485)))
		return -EFAULT;

	return 0;
}

/*
 * Called via sys_ioctl.  We can use spin_lock_irq() here.
 */
static int
uart_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	void __user *uarg = (void __user *)arg;
	int ret = -ENOIOCTLCMD;


	/*
	 * These ioctls don't rely on the hardware to be present.
	 */
	switch (cmd) {
	case TIOCGSERIAL:
		ret = uart_get_info_user(port, uarg);
		break;

	case TIOCSSERIAL:
		down_write(&tty->termios_rwsem);
		ret = uart_set_info_user(tty, state, uarg);
		up_write(&tty->termios_rwsem);
		break;

	case TIOCSERCONFIG:
		down_write(&tty->termios_rwsem);
		ret = uart_do_autoconfig(tty, state);
		up_write(&tty->termios_rwsem);
		break;

	case TIOCSERGWILD: /* obsolete */
	case TIOCSERSWILD: /* obsolete */
		ret = 0;
		break;
	}

	if (ret != -ENOIOCTLCMD)
		goto out;

	if (tty_io_error(tty)) {
		ret = -EIO;
		goto out;
	}

	/*
	 * The following should only be used when hardware is present.
	 */
	switch (cmd) {
	case TIOCMIWAIT:
		ret = uart_wait_modem_status(state, arg);
		break;
	}

	if (ret != -ENOIOCTLCMD)
		goto out;

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);

	if (!uport || tty_io_error(tty)) {
		ret = -EIO;
		goto out_up;
	}

	/*
	 * All these rely on hardware being present and need to be
	 * protected against the tty being hung up.
	 */

	switch (cmd) {
	case TIOCSERGETLSR: /* Get line status register */
		ret = uart_get_lsr_info(tty, state, uarg);
		break;

	case TIOCGRS485:
		ret = uart_get_rs485_config(uport, uarg);
		break;

	case TIOCSRS485:
		ret = uart_set_rs485_config(uport, uarg);
		break;
	default:
		if (uport->ops->ioctl)
			ret = uport->ops->ioctl(uport, cmd, arg);
		break;
	}
out_up:
	mutex_unlock(&port->mutex);
out:
	return ret;
}

static void uart_set_ldisc(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *uport;

	mutex_lock(&state->port.mutex);
	uport = uart_port_check(state);
	if (uport && uport->ops->set_ldisc)
		uport->ops->set_ldisc(uport, &tty->termios);
	mutex_unlock(&state->port.mutex);
}

static void uart_set_termios(struct tty_struct *tty,
						struct ktermios *old_termios)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *uport;
	unsigned int cflag = tty->termios.c_cflag;
	unsigned int iflag_mask = IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK;
	bool sw_changed = false;

	mutex_lock(&state->port.mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	/*
	 * Drivers doing software flow control also need to know
	 * about changes to these input settings.
	 */
	if (uport->flags & UPF_SOFT_FLOW) {
		iflag_mask |= IXANY|IXON|IXOFF;
		sw_changed =
		   tty->termios.c_cc[VSTART] != old_termios->c_cc[VSTART] ||
		   tty->termios.c_cc[VSTOP] != old_termios->c_cc[VSTOP];
	}

	/*
	 * These are the bits that are used to setup various
	 * flags in the low level driver. We can ignore the Bfoo
	 * bits in c_cflag; c_[io]speed will always be set
	 * appropriately by set_termios() in tty_ioctl.c
	 */
	if ((cflag ^ old_termios->c_cflag) == 0 &&
	    tty->termios.c_ospeed == old_termios->c_ospeed &&
	    tty->termios.c_ispeed == old_termios->c_ispeed &&
	    ((tty->termios.c_iflag ^ old_termios->c_iflag) & iflag_mask) == 0 &&
	    !sw_changed) {
		goto out;
	}

	uart_change_speed(tty, state, old_termios);
	/* reload cflag from termios; port driver may have overriden flags */
	cflag = tty->termios.c_cflag;

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD))
		uart_clear_mctrl(uport, TIOCM_RTS | TIOCM_DTR);
	/* Handle transition away from B0 status */
	else if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		unsigned int mask = TIOCM_DTR;
		if (!(cflag & CRTSCTS) || !tty_throttled(tty))
			mask |= TIOCM_RTS;
		uart_set_mctrl(uport, mask);
	}
out:
	mutex_unlock(&state->port.mutex);
}

/*
 * Calls to uart_close() are serialised via the tty_lock in
 *   drivers/tty/tty_io.c:tty_release()
 *   drivers/tty/tty_io.c:do_tty_hangup()
 */
static void uart_close(struct tty_struct *tty, struct file *filp)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port;

	if (!state) {
		struct uart_driver *drv = tty->driver->driver_state;

		state = drv->state + tty->index;
		port = &state->port;
		spin_lock_irq(&port->lock);
		--port->count;
		spin_unlock_irq(&port->lock);
		return;
	}

	port = &state->port;
	pr_debug("uart_close(%d) called\n", tty->index);

	tty_port_close(tty->port, tty, filp);
}

static void uart_tty_port_shutdown(struct tty_port *port)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = uart_port_check(state);

	/*
	 * At this point, we stop accepting input.  To do this, we
	 * disable the receive line status interrupts.
	 */
	if (WARN(!uport, "detached port still initialized!\n"))
		return;

	spin_lock_irq(&uport->lock);
	uport->ops->stop_rx(uport);
	spin_unlock_irq(&uport->lock);

	uart_port_shutdown(port);

	/*
	 * It's possible for shutdown to be called after suspend if we get
	 * a DCD drop (hangup) at just the right time.  Clear suspended bit so
	 * we don't try to resume a port that has been shutdown.
	 */
	tty_port_set_suspended(port, 0);

	uart_change_pm(state, UART_PM_STATE_OFF);

}

static void uart_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long char_time, expire;

	port = uart_port_ref(state);
	if (!port || port->type == PORT_UNKNOWN || port->fifosize == 0) {
		uart_port_deref(port);
		return;
	}

	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (port->timeout - HZ/50) / port->fifosize;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;

	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than port->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*port->timeout.
	 */
	if (timeout == 0 || timeout > 2 * port->timeout)
		timeout = 2 * port->timeout;

	expire = jiffies + timeout;

	pr_debug("uart_wait_until_sent(%d), jiffies=%lu, expire=%lu...\n",
		port->line, jiffies, expire);

	/*
	 * Check whether the transmitter is empty every 'char_time'.
	 * 'timeout' / 'expire' give us the maximum amount of time
	 * we wait.
	 */
	while (!port->ops->tx_empty(port)) {
		msleep_interruptible(jiffies_to_msecs(char_time));
		if (signal_pending(current))
			break;
		if (time_after(jiffies, expire))
			break;
	}
	uart_port_deref(port);
}

/*
 * Calls to uart_hangup() are serialised by the tty_lock in
 *   drivers/tty/tty_io.c:do_tty_hangup()
 * This runs from a workqueue and can sleep for a _short_ time only.
 */
static void uart_hangup(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport;
	unsigned long flags;

	pr_debug("uart_hangup(%d)\n", tty->index);

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	WARN(!uport, "hangup of detached port!\n");

	if (tty_port_active(port)) {
		uart_flush_buffer(tty);
		uart_shutdown(tty, state);
		spin_lock_irqsave(&port->lock, flags);
		port->count = 0;
		spin_unlock_irqrestore(&port->lock, flags);
		tty_port_set_active(port, 0);
		tty_port_tty_set(port, NULL);
		if (uport && !uart_console(uport))
			uart_change_pm(state, UART_PM_STATE_OFF);
		wake_up_interruptible(&port->open_wait);
		wake_up_interruptible(&port->delta_msr_wait);
	}
	mutex_unlock(&port->mutex);
}

/* uport == NULL if uart_port has already been removed */
static void uart_port_shutdown(struct tty_port *port)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = uart_port_check(state);

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free
	 * the irq here so the queue might never be woken up.  Note
	 * that we won't end up waiting on delta_msr_wait again since
	 * any outstanding file descriptors should be pointing at
	 * hung_up_tty_fops now.
	 */
	wake_up_interruptible(&port->delta_msr_wait);

	/*
	 * Free the IRQ and disable the port.
	 */
	if (uport)
		uport->ops->shutdown(uport);

	/*
	 * Ensure that the IRQ handler isn't running on another CPU.
	 */
	if (uport)
		synchronize_irq(uport->irq);
}

static int uart_carrier_raised(struct tty_port *port)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport;
	int mctrl;

	uport = uart_port_ref(state);
	/*
	 * Should never observe uport == NULL since checks for hangup should
	 * abort the tty_port_block_til_ready() loop before checking for carrier
	 * raised -- but report carrier raised if it does anyway so open will
	 * continue and not sleep
	 */
	if (WARN_ON(!uport))
		return 1;
	spin_lock_irq(&uport->lock);
	uart_enable_ms(uport);
	mctrl = uport->ops->get_mctrl(uport);
	spin_unlock_irq(&uport->lock);
	uart_port_deref(uport);
	if (mctrl & TIOCM_CAR)
		return 1;
	return 0;
}

static void uart_dtr_rts(struct tty_port *port, int onoff)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport;

	uport = uart_port_ref(state);
	if (!uport)
		return;

	if (onoff)
		uart_set_mctrl(uport, TIOCM_DTR | TIOCM_RTS);
	else
		uart_clear_mctrl(uport, TIOCM_DTR | TIOCM_RTS);

	uart_port_deref(uport);
}

/*
 * Calls to uart_open are serialised by the tty_lock in
 *   drivers/tty/tty_io.c:tty_open()
 * Note that if this fails, then uart_close() _will_ be called.
 *
 * In time, we want to scrap the "opening nonpresent ports"
 * behaviour and implement an alternative way for setserial
 * to set base addresses/ports/types.  This will allow us to
 * get rid of a certain amount of extra tests.
 */
static int uart_open(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = tty->driver->driver_state;
	int retval, line = tty->index;
	struct uart_state *state = drv->state + line;

	tty->driver_data = state;

	retval = tty_port_open(&state->port, tty, filp);
	if (retval > 0)
		retval = 0;

	return retval;
}

static int uart_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport;

	uport = uart_port_check(state);
	if (!uport || uport->flags & UPF_DEAD)
		return -ENXIO;

	port->low_latency = (uport->flags & UPF_LOW_LATENCY) ? 1 : 0;

	/*
	 * Start up the serial port.
	 */
	return uart_startup(tty, state, 0);
}

static const char *uart_type(struct uart_port *port)
{
	const char *str = NULL;

	if (port->ops->type)
		str = port->ops->type(port);

	if (!str)
		str = "unknown";

	return str;
}

#ifdef CONFIG_PROC_FS

static void uart_line_info(struct seq_file *m, struct uart_driver *drv, int i)
{
	struct uart_state *state = drv->state + i;
	struct tty_port *port = &state->port;
	enum uart_pm_state pm_state;
	struct uart_port *uport;
	char stat_buf[32];
	unsigned int status;
	int mmio;

	mutex_lock(&port->mutex);
	uport = uart_port_check(state);
	if (!uport)
		goto out;

	mmio = uport->iotype >= UPIO_MEM;
	seq_printf(m, "%d: uart:%s %s%08llX irq:%d",
			uport->line, uart_type(uport),
			mmio ? "mmio:0x" : "port:",
			mmio ? (unsigned long long)uport->mapbase
			     : (unsigned long long)uport->iobase,
			uport->irq);

	if (uport->type == PORT_UNKNOWN) {
		seq_putc(m, '\n');
		goto out;
	}

	if (capable(CAP_SYS_ADMIN)) {
		pm_state = state->pm_state;
		if (pm_state != UART_PM_STATE_ON)
			uart_change_pm(state, UART_PM_STATE_ON);
		spin_lock_irq(&uport->lock);
		status = uport->ops->get_mctrl(uport);
		spin_unlock_irq(&uport->lock);
		if (pm_state != UART_PM_STATE_ON)
			uart_change_pm(state, pm_state);

		seq_printf(m, " tx:%d rx:%d",
				uport->icount.tx, uport->icount.rx);
		if (uport->icount.frame)
			seq_printf(m, " fe:%d",	uport->icount.frame);
		if (uport->icount.parity)
			seq_printf(m, " pe:%d",	uport->icount.parity);
		if (uport->icount.brk)
			seq_printf(m, " brk:%d", uport->icount.brk);
		if (uport->icount.overrun)
			seq_printf(m, " oe:%d", uport->icount.overrun);

#define INFOBIT(bit, str) \
	if (uport->mctrl & (bit)) \
		strncat(stat_buf, (str), sizeof(stat_buf) - \
			strlen(stat_buf) - 2)
#define STATBIT(bit, str) \
	if (status & (bit)) \
		strncat(stat_buf, (str), sizeof(stat_buf) - \
		       strlen(stat_buf) - 2)

		stat_buf[0] = '\0';
		stat_buf[1] = '\0';
		INFOBIT(TIOCM_RTS, "|RTS");
		STATBIT(TIOCM_CTS, "|CTS");
		INFOBIT(TIOCM_DTR, "|DTR");
		STATBIT(TIOCM_DSR, "|DSR");
		STATBIT(TIOCM_CAR, "|CD");
		STATBIT(TIOCM_RNG, "|RI");
		if (stat_buf[0])
			stat_buf[0] = ' ';

		seq_puts(m, stat_buf);
	}
	seq_putc(m, '\n');
#undef STATBIT
#undef INFOBIT
out:
	mutex_unlock(&port->mutex);
}

static int uart_proc_show(struct seq_file *m, void *v)
{
	struct tty_driver *ttydrv = m->private;
	struct uart_driver *drv = ttydrv->driver_state;
	int i;

	seq_printf(m, "serinfo:1.0 driver%s%s revision:%s\n", "", "", "");
	for (i = 0; i < drv->nr; i++)
		uart_line_info(m, drv, i);
	return 0;
}

static int uart_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, uart_proc_show, PDE_DATA(inode));
}

static const struct file_operations uart_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= uart_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

#if defined(CONFIG_SERIAL_CORE_CONSOLE) || defined(CONFIG_CONSOLE_POLL)
/**
 *	uart_console_write - write a console message to a serial port
 *	@port: the port to write the message
 *	@s: array of characters
 *	@count: number of characters in string to write
 *	@putchar: function to write character to port
 */
void uart_console_write(struct uart_port *port, const char *s,
			unsigned int count,
			void (*putchar)(struct uart_port *, int))
{
	unsigned int i;

	for (i = 0; i < count; i++, s++) {
		if (*s == '\n')
			putchar(port, '\r');
		putchar(port, *s);
	}
}
EXPORT_SYMBOL_GPL(uart_console_write);

/*
 *	Check whether an invalid uart number has been specified, and
 *	if so, search for the first available port that does have
 *	console support.
 */
struct uart_port * __init
uart_get_console(struct uart_port *ports, int nr, struct console *co)
{
	int idx = co->index;

	if (idx < 0 || idx >= nr || (ports[idx].iobase == 0 &&
				     ports[idx].membase == NULL))
		for (idx = 0; idx < nr; idx++)
			if (ports[idx].iobase != 0 ||
			    ports[idx].membase != NULL)
				break;

	co->index = idx;

	return ports + idx;
}

/**
 *	uart_parse_earlycon - Parse earlycon options
 *	@p:	  ptr to 2nd field (ie., just beyond '<name>,')
 *	@iotype:  ptr for decoded iotype (out)
 *	@addr:    ptr for decoded mapbase/iobase (out)
 *	@options: ptr for <options> field; NULL if not present (out)
 *
 *	Decodes earlycon kernel command line parameters of the form
 *	   earlycon=<name>,io|mmio|mmio16|mmio32|mmio32be|mmio32native,<addr>,<options>
 *	   console=<name>,io|mmio|mmio16|mmio32|mmio32be|mmio32native,<addr>,<options>
 *
 *	The optional form
 *	   earlycon=<name>,0x<addr>,<options>
 *	   console=<name>,0x<addr>,<options>
 *	is also accepted; the returned @iotype will be UPIO_MEM.
 *
 *	Returns 0 on success or -EINVAL on failure
 */
int uart_parse_earlycon(char *p, unsigned char *iotype, resource_size_t *addr,
			char **options)
{
	if (strncmp(p, "mmio,", 5) == 0) {
		*iotype = UPIO_MEM;
		p += 5;
	} else if (strncmp(p, "mmio16,", 7) == 0) {
		*iotype = UPIO_MEM16;
		p += 7;
	} else if (strncmp(p, "mmio32,", 7) == 0) {
		*iotype = UPIO_MEM32;
		p += 7;
	} else if (strncmp(p, "mmio32be,", 9) == 0) {
		*iotype = UPIO_MEM32BE;
		p += 9;
	} else if (strncmp(p, "mmio32native,", 13) == 0) {
		*iotype = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) ?
			UPIO_MEM32BE : UPIO_MEM32;
		p += 13;
	} else if (strncmp(p, "io,", 3) == 0) {
		*iotype = UPIO_PORT;
		p += 3;
	} else if (strncmp(p, "0x", 2) == 0) {
		*iotype = UPIO_MEM;
	} else {
		return -EINVAL;
	}

	/*
	 * Before you replace it with kstrtoull(), think about options separator
	 * (',') it will not tolerate
	 */
	*addr = simple_strtoull(p, NULL, 0);
	p = strchr(p, ',');
	if (p)
		p++;

	*options = p;
	return 0;
}
EXPORT_SYMBOL_GPL(uart_parse_earlycon);

/**
 *	uart_parse_options - Parse serial port baud/parity/bits/flow control.
 *	@options: pointer to option string
 *	@baud: pointer to an 'int' variable for the baud rate.
 *	@parity: pointer to an 'int' variable for the parity.
 *	@bits: pointer to an 'int' variable for the number of data bits.
 *	@flow: pointer to an 'int' variable for the flow control character.
 *
 *	uart_parse_options decodes a string containing the serial console
 *	options.  The format of the string is <baud><parity><bits><flow>,
 *	eg: 115200n8r
 */
void
uart_parse_options(char *options, int *baud, int *parity, int *bits, int *flow)
{
	char *s = options;

	*baud = simple_strtoul(s, NULL, 10);
	while (*s >= '0' && *s <= '9')
		s++;
	if (*s)
		*parity = *s++;
	if (*s)
		*bits = *s++ - '0';
	if (*s)
		*flow = *s;
}
EXPORT_SYMBOL_GPL(uart_parse_options);

/**
 *	uart_set_options - setup the serial console parameters
 *	@port: pointer to the serial ports uart_port structure
 *	@co: console pointer
 *	@baud: baud rate
 *	@parity: parity character - 'n' (none), 'o' (odd), 'e' (even)
 *	@bits: number of data bits
 *	@flow: flow control character - 'r' (rts)
 */
int
uart_set_options(struct uart_port *port, struct console *co,
		 int baud, int parity, int bits, int flow)
{
	struct ktermios termios;
	static struct ktermios dummy;

	/*
	 * Ensure that the serial console lock is initialised
	 * early.
	 * If this port is a console, then the spinlock is already
	 * initialised.
	 */
	if (!(uart_console(port) && (port->cons->flags & CON_ENABLED))) {
		spin_lock_init(&port->lock);
		lockdep_set_class(&port->lock, &port_lock_key);
	}

	memset(&termios, 0, sizeof(struct ktermios));

	termios.c_cflag |= CREAD | HUPCL | CLOCAL;
	tty_termios_encode_baud_rate(&termios, baud, baud);

	if (bits == 7)
		termios.c_cflag |= CS7;
	else
		termios.c_cflag |= CS8;

	switch (parity) {
	case 'o': case 'O':
		termios.c_cflag |= PARODD;
		/*fall through*/
	case 'e': case 'E':
		termios.c_cflag |= PARENB;
		break;
	}

	if (flow == 'r')
		termios.c_cflag |= CRTSCTS;

	/*
	 * some uarts on other side don't support no flow control.
	 * So we set * DTR in host uart to make them happy
	 */
	port->mctrl |= TIOCM_DTR;

	port->ops->set_termios(port, &termios, &dummy);
	/*
	 * Allow the setting of the UART parameters with a NULL console
	 * too:
	 */
	if (co)
		co->cflag = termios.c_cflag;

	return 0;
}
EXPORT_SYMBOL_GPL(uart_set_options);
#endif /* CONFIG_SERIAL_CORE_CONSOLE */

/**
 * uart_change_pm - set power state of the port
 *
 * @state: port descriptor
 * @pm_state: new state
 *
 * Locking: port->mutex has to be held
 */
static void uart_change_pm(struct uart_state *state,
			   enum uart_pm_state pm_state)
{
	struct uart_port *port = uart_port_check(state);

	if (state->pm_state != pm_state) {
		if (port && port->ops->pm)
			port->ops->pm(port, pm_state, state->pm_state);
		state->pm_state = pm_state;
	}
}

struct uart_match {
	struct uart_port *port;
	struct uart_driver *driver;
};

static int serial_match_port(struct device *dev, void *data)
{
	struct uart_match *match = data;
	struct tty_driver *tty_drv = match->driver->tty_driver;
	dev_t devt = MKDEV(tty_drv->major, tty_drv->minor_start) +
		match->port->line;

	return dev->devt == devt; /* Actually, only one tty per port */
}

int uart_suspend_port(struct uart_driver *drv, struct uart_port *uport)
{
	struct uart_state *state = drv->state + uport->line;
	struct tty_port *port = &state->port;
	struct device *tty_dev;
	struct uart_match match = {uport, drv};

	mutex_lock(&port->mutex);

	tty_dev = device_find_child(uport->dev, &match, serial_match_port);
	if (device_may_wakeup(tty_dev)) {
		if (!enable_irq_wake(uport->irq))
			uport->irq_wake = 1;
		put_device(tty_dev);
		mutex_unlock(&port->mutex);
		return 0;
	}
	put_device(tty_dev);

	/* Nothing to do if the console is not suspending */
	if (!console_suspend_enabled && uart_console(uport))
		goto unlock;

	uport->suspended = 1;

	if (tty_port_initialized(port)) {
		const struct uart_ops *ops = uport->ops;
		int tries;

		tty_port_set_suspended(port, 1);
		tty_port_set_initialized(port, 0);

		spin_lock_irq(&uport->lock);
		ops->stop_tx(uport);
		ops->set_mctrl(uport, 0);
		ops->stop_rx(uport);
		spin_unlock_irq(&uport->lock);

		/*
		 * Wait for the transmitter to empty.
		 */
		for (tries = 3; !ops->tx_empty(uport) && tries; tries--)
			msleep(10);
		if (!tries)
			dev_err(uport->dev, "%s%d: Unable to drain transmitter\n",
				drv->dev_name,
				drv->tty_driver->name_base + uport->line);

		ops->shutdown(uport);
	}

	/*
	 * Disable the console device before suspending.
	 */
	if (uart_console(uport))
		console_stop(uport->cons);

	uart_change_pm(state, UART_PM_STATE_OFF);
unlock:
	mutex_unlock(&port->mutex);

	return 0;
}

int uart_resume_port(struct uart_driver *drv, struct uart_port *uport)
{
	struct uart_state *state = drv->state + uport->line;
	struct tty_port *port = &state->port;
	struct device *tty_dev;
	struct uart_match match = {uport, drv};
	struct ktermios termios;

	mutex_lock(&port->mutex);

	tty_dev = device_find_child(uport->dev, &match, serial_match_port);
	if (!uport->suspended && device_may_wakeup(tty_dev)) {
		if (uport->irq_wake) {
			disable_irq_wake(uport->irq);
			uport->irq_wake = 0;
		}
		put_device(tty_dev);
		mutex_unlock(&port->mutex);
		return 0;
	}
	put_device(tty_dev);
	uport->suspended = 0;

	/*
	 * Re-enable the console device after suspending.
	 */
	if (uart_console(uport)) {
		/*
		 * First try to use the console cflag setting.
		 */
		memset(&termios, 0, sizeof(struct ktermios));
		termios.c_cflag = uport->cons->cflag;

		/*
		 * If that's unset, use the tty termios setting.
		 */
		if (port->tty && termios.c_cflag == 0)
			termios = port->tty->termios;

		if (console_suspend_enabled)
			uart_change_pm(state, UART_PM_STATE_ON);
		uport->ops->set_termios(uport, &termios, NULL);
		if (console_suspend_enabled)
			console_start(uport->cons);
	}

	if (tty_port_suspended(port)) {
		const struct uart_ops *ops = uport->ops;
		int ret;

		uart_change_pm(state, UART_PM_STATE_ON);
		spin_lock_irq(&uport->lock);
		ops->set_mctrl(uport, 0);
		spin_unlock_irq(&uport->lock);
		if (console_suspend_enabled || !uart_console(uport)) {
			/* Protected by port mutex for now */
			struct tty_struct *tty = port->tty;
			ret = ops->startup(uport);
			if (ret == 0) {
				if (tty)
					uart_change_speed(tty, state, NULL);
				spin_lock_irq(&uport->lock);
				ops->set_mctrl(uport, uport->mctrl);
				ops->start_tx(uport);
				spin_unlock_irq(&uport->lock);
				tty_port_set_initialized(port, 1);
			} else {
				/*
				 * Failed to resume - maybe hardware went away?
				 * Clear the "initialized" flag so we won't try
				 * to call the low level drivers shutdown method.
				 */
				uart_shutdown(tty, state);
			}
		}

		tty_port_set_suspended(port, 0);
	}

	mutex_unlock(&port->mutex);

	return 0;
}

static inline void
uart_report_port(struct uart_driver *drv, struct uart_port *port)
{
	char address[64];

	switch (port->iotype) {
	case UPIO_PORT:
		snprintf(address, sizeof(address), "I/O 0x%lx", port->iobase);
		break;
	case UPIO_HUB6:
		snprintf(address, sizeof(address),
			 "I/O 0x%lx offset 0x%x", port->iobase, port->hub6);
		break;
	case UPIO_MEM:
	case UPIO_MEM16:
	case UPIO_MEM32:
	case UPIO_MEM32BE:
	case UPIO_AU:
	case UPIO_TSI:
		snprintf(address, sizeof(address),
			 "MMIO 0x%llx", (unsigned long long)port->mapbase);
		break;
	default:
		strlcpy(address, "*unknown*", sizeof(address));
		break;
	}

	printk(KERN_INFO "%s%s%s%d at %s (irq = %d, base_baud = %d) is a %s\n",
	       port->dev ? dev_name(port->dev) : "",
	       port->dev ? ": " : "",
	       drv->dev_name,
	       drv->tty_driver->name_base + port->line,
	       address, port->irq, port->uartclk / 16, uart_type(port));
}

static void
uart_configure_port(struct uart_driver *drv, struct uart_state *state,
		    struct uart_port *port)
{
	unsigned int flags;

	/*
	 * If there isn't a port here, don't do anything further.
	 */
	if (!port->iobase && !port->mapbase && !port->membase)
		return;

	/*
	 * Now do the auto configuration stuff.  Note that config_port
	 * is expected to claim the resources and map the port for us.
	 */
	flags = 0;
	if (port->flags & UPF_AUTO_IRQ)
		flags |= UART_CONFIG_IRQ;
	if (port->flags & UPF_BOOT_AUTOCONF) {
		if (!(port->flags & UPF_FIXED_TYPE)) {
			port->type = PORT_UNKNOWN;
			flags |= UART_CONFIG_TYPE;
		}
		port->ops->config_port(port, flags);
	}

	if (port->type != PORT_UNKNOWN) {
		unsigned long flags;

		uart_report_port(drv, port);

		/* Power up port for set_mctrl() */
		uart_change_pm(state, UART_PM_STATE_ON);

		/*
		 * Ensure that the modem control lines are de-activated.
		 * keep the DTR setting that is set in uart_set_options()
		 * We probably don't need a spinlock around this, but
		 */
		spin_lock_irqsave(&port->lock, flags);
		port->ops->set_mctrl(port, port->mctrl & TIOCM_DTR);
		spin_unlock_irqrestore(&port->lock, flags);

		/*
		 * If this driver supports console, and it hasn't been
		 * successfully registered yet, try to re-register it.
		 * It may be that the port was not available.
		 */
		if (port->cons && !(port->cons->flags & CON_ENABLED))
			register_console(port->cons);

		/*
		 * Power down all ports by default, except the
		 * console if we have one.
		 */
		if (!uart_console(port))
			uart_change_pm(state, UART_PM_STATE_OFF);
	}
}

#ifdef CONFIG_CONSOLE_POLL

static int uart_poll_init(struct tty_driver *driver, int line, char *options)
{
	struct uart_driver *drv = driver->driver_state;
	struct uart_state *state = drv->state + line;
	struct tty_port *tport;
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret = 0;

	if (!state)
		return -1;

	tport = &state->port;
	mutex_lock(&tport->mutex);

	port = uart_port_check(state);
	if (!port || !(port->ops->poll_get_char && port->ops->poll_put_char)) {
		ret = -1;
		goto out;
	}

	if (port->ops->poll_init) {
		/*
		 * We don't set initialized as we only initialized the hw,
		 * e.g. state->xmit is still uninitialized.
		 */
		if (!tty_port_initialized(tport))
			ret = port->ops->poll_init(port);
	}

	if (!ret && options) {
		uart_parse_options(options, &baud, &parity, &bits, &flow);
		ret = uart_set_options(port, NULL, baud, parity, bits, flow);
	}
out:
	mutex_unlock(&tport->mutex);
	return ret;
}

static int uart_poll_get_char(struct tty_driver *driver, int line)
{
	struct uart_driver *drv = driver->driver_state;
	struct uart_state *state = drv->state + line;
	struct uart_port *port;
	int ret = -1;

	if (state) {
		port = uart_port_ref(state);
		if (port)
			ret = port->ops->poll_get_char(port);
		uart_port_deref(port);
	}
	return ret;
}

static void uart_poll_put_char(struct tty_driver *driver, int line, char ch)
{
	struct uart_driver *drv = driver->driver_state;
	struct uart_state *state = drv->state + line;
	struct uart_port *port;

	if (!state)
		return;

	port = uart_port_ref(state);
	if (!port)
		return;

	if (ch == '\n')
		port->ops->poll_put_char(port, '\r');
	port->ops->poll_put_char(port, ch);
	uart_port_deref(port);
}
#endif

static const struct tty_operations uart_ops = {
	.open		= uart_open,
	.close		= uart_close,
	.write		= uart_write,
	.put_char	= uart_put_char,
	.flush_chars	= uart_flush_chars,
	.write_room	= uart_write_room,
	.chars_in_buffer= uart_chars_in_buffer,
	.flush_buffer	= uart_flush_buffer,
	.ioctl		= uart_ioctl,
	.throttle	= uart_throttle,
	.unthrottle	= uart_unthrottle,
	.send_xchar	= uart_send_xchar,
	.set_termios	= uart_set_termios,
	.set_ldisc	= uart_set_ldisc,
	.stop		= uart_stop,
	.start		= uart_start,
	.hangup		= uart_hangup,
	.break_ctl	= uart_break_ctl,
	.wait_until_sent= uart_wait_until_sent,
#ifdef CONFIG_PROC_FS
	.proc_fops	= &uart_proc_fops,
#endif
	.tiocmget	= uart_tiocmget,
	.tiocmset	= uart_tiocmset,
	.get_icount	= uart_get_icount,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init	= uart_poll_init,
	.poll_get_char	= uart_poll_get_char,
	.poll_put_char	= uart_poll_put_char,
#endif
};

static const struct tty_port_operations uart_port_ops = {
	.carrier_raised = uart_carrier_raised,
	.dtr_rts	= uart_dtr_rts,
	.activate	= uart_port_activate,
	.shutdown	= uart_tty_port_shutdown,
};

/**
 *	uart_register_driver - register a driver with the uart core layer
 *	@drv: low level driver structure
 *
 *	Register a uart driver with the core driver.  We in turn register
 *	with the tty layer, and initialise the core driver per-port state.
 *
 *	We have a proc file in /proc/tty/driver which is named after the
 *	normal driver.
 *
 *	drv->port should be NULL, and the per-port structures should be
 *	registered using uart_add_one_port after this call has succeeded.
 */
int uart_register_driver(struct uart_driver *drv)
{
	struct tty_driver *normal;
	int i, retval;

	BUG_ON(drv->state);

	/*
	 * Maybe we should be using a slab cache for this, especially if
	 * we have a large number of ports to handle.
	 */
	drv->state = kzalloc(sizeof(struct uart_state) * drv->nr, GFP_KERNEL);
	if (!drv->state)
		goto out;

	normal = alloc_tty_driver(drv->nr);
	if (!normal)
		goto out_kfree;

	drv->tty_driver = normal;

	normal->driver_name	= drv->driver_name;
	normal->name		= drv->dev_name;
	normal->major		= drv->major;
	normal->minor_start	= drv->minor;
	normal->type		= TTY_DRIVER_TYPE_SERIAL;
	normal->subtype		= SERIAL_TYPE_NORMAL;
	normal->init_termios	= tty_std_termios;
	normal->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	normal->init_termios.c_ispeed = normal->init_termios.c_ospeed = 9600;
	normal->flags		= TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	normal->driver_state    = drv;
	tty_set_operations(normal, &uart_ops);

	/*
	 * Initialise the UART state(s).
	 */
	for (i = 0; i < drv->nr; i++) {
		struct uart_state *state = drv->state + i;
		struct tty_port *port = &state->port;

		tty_port_init(port);
		port->ops = &uart_port_ops;
	}

	retval = tty_register_driver(normal);
	if (retval >= 0)
		return retval;

	for (i = 0; i < drv->nr; i++)
		tty_port_destroy(&drv->state[i].port);
	put_tty_driver(normal);
out_kfree:
	kfree(drv->state);
out:
	return -ENOMEM;
}

/**
 *	uart_unregister_driver - remove a driver from the uart core layer
 *	@drv: low level driver structure
 *
 *	Remove all references to a driver from the core driver.  The low
 *	level driver must have removed all its ports via the
 *	uart_remove_one_port() if it registered them with uart_add_one_port().
 *	(ie, drv->port == NULL)
 */
void uart_unregister_driver(struct uart_driver *drv)
{
	struct tty_driver *p = drv->tty_driver;
	unsigned int i;

	tty_unregister_driver(p);
	put_tty_driver(p);
	for (i = 0; i < drv->nr; i++)
		tty_port_destroy(&drv->state[i].port);
	kfree(drv->state);
	drv->state = NULL;
	drv->tty_driver = NULL;
}

struct tty_driver *uart_console_device(struct console *co, int *index)
{
	struct uart_driver *p = co->data;
	*index = co->index;
	return p->tty_driver;
}

static ssize_t uart_get_attr_uartclk(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.baud_base * 16);
}

static ssize_t uart_get_attr_type(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.type);
}
static ssize_t uart_get_attr_line(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.line);
}

static ssize_t uart_get_attr_port(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);
	unsigned long ioaddr;

	uart_get_info(port, &tmp);
	ioaddr = tmp.port;
	if (HIGH_BITS_OFFSET)
		ioaddr |= (unsigned long)tmp.port_high << HIGH_BITS_OFFSET;
	return snprintf(buf, PAGE_SIZE, "0x%lX\n", ioaddr);
}

static ssize_t uart_get_attr_irq(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.irq);
}

static ssize_t uart_get_attr_flags(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "0x%X\n", tmp.flags);
}

static ssize_t uart_get_attr_xmit_fifo_size(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.xmit_fifo_size);
}


static ssize_t uart_get_attr_close_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.close_delay);
}


static ssize_t uart_get_attr_closing_wait(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.closing_wait);
}

static ssize_t uart_get_attr_custom_divisor(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.custom_divisor);
}

static ssize_t uart_get_attr_io_type(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.io_type);
}

static ssize_t uart_get_attr_iomem_base(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "0x%lX\n", (unsigned long)tmp.iomem_base);
}

static ssize_t uart_get_attr_iomem_reg_shift(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct serial_struct tmp;
	struct tty_port *port = dev_get_drvdata(dev);

	uart_get_info(port, &tmp);
	return snprintf(buf, PAGE_SIZE, "%d\n", tmp.iomem_reg_shift);
}

static DEVICE_ATTR(type, S_IRUSR | S_IRGRP, uart_get_attr_type, NULL);
static DEVICE_ATTR(line, S_IRUSR | S_IRGRP, uart_get_attr_line, NULL);
static DEVICE_ATTR(port, S_IRUSR | S_IRGRP, uart_get_attr_port, NULL);
static DEVICE_ATTR(irq, S_IRUSR | S_IRGRP, uart_get_attr_irq, NULL);
static DEVICE_ATTR(flags, S_IRUSR | S_IRGRP, uart_get_attr_flags, NULL);
static DEVICE_ATTR(xmit_fifo_size, S_IRUSR | S_IRGRP, uart_get_attr_xmit_fifo_size, NULL);
static DEVICE_ATTR(uartclk, S_IRUSR | S_IRGRP, uart_get_attr_uartclk, NULL);
static DEVICE_ATTR(close_delay, S_IRUSR | S_IRGRP, uart_get_attr_close_delay, NULL);
static DEVICE_ATTR(closing_wait, S_IRUSR | S_IRGRP, uart_get_attr_closing_wait, NULL);
static DEVICE_ATTR(custom_divisor, S_IRUSR | S_IRGRP, uart_get_attr_custom_divisor, NULL);
static DEVICE_ATTR(io_type, S_IRUSR | S_IRGRP, uart_get_attr_io_type, NULL);
static DEVICE_ATTR(iomem_base, S_IRUSR | S_IRGRP, uart_get_attr_iomem_base, NULL);
static DEVICE_ATTR(iomem_reg_shift, S_IRUSR | S_IRGRP, uart_get_attr_iomem_reg_shift, NULL);

static struct attribute *tty_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_line.attr,
	&dev_attr_port.attr,
	&dev_attr_irq.attr,
	&dev_attr_flags.attr,
	&dev_attr_xmit_fifo_size.attr,
	&dev_attr_uartclk.attr,
	&dev_attr_close_delay.attr,
	&dev_attr_closing_wait.attr,
	&dev_attr_custom_divisor.attr,
	&dev_attr_io_type.attr,
	&dev_attr_iomem_base.attr,
	&dev_attr_iomem_reg_shift.attr,
	NULL,
	};

static const struct attribute_group tty_dev_attr_group = {
	.attrs = tty_dev_attrs,
	};

/**
 *	uart_add_one_port - attach a driver-defined port structure
 *	@drv: pointer to the uart low level driver structure for this port
 *	@uport: uart port structure to use for this port.
 *
 *	This allows the driver to register its own uart_port structure
 *	with the core driver.  The main purpose is to allow the low
 *	level uart drivers to expand uart_port, rather than having yet
 *	more levels of structures.
 */
int uart_add_one_port(struct uart_driver *drv, struct uart_port *uport)
{
	struct uart_state *state;
	struct tty_port *port;
	int ret = 0;
	struct device *tty_dev;
	int num_groups;

	BUG_ON(in_interrupt());

	if (uport->line >= drv->nr)
		return -EINVAL;

	state = drv->state + uport->line;
	port = &state->port;

	mutex_lock(&port_mutex);
	mutex_lock(&port->mutex);
	if (state->uart_port) {
		ret = -EINVAL;
		goto out;
	}

	/* Link the port to the driver state table and vice versa */
	atomic_set(&state->refcount, 1);
	init_waitqueue_head(&state->remove_wait);
	state->uart_port = uport;
	uport->state = state;

	state->pm_state = UART_PM_STATE_UNDEFINED;
	uport->cons = drv->cons;
	uport->minor = drv->tty_driver->minor_start + uport->line;

	/*
	 * If this port is a console, then the spinlock is already
	 * initialised.
	 */
	if (!(uart_console(uport) && (uport->cons->flags & CON_ENABLED))) {
		spin_lock_init(&uport->lock);
		lockdep_set_class(&uport->lock, &port_lock_key);
	}
	if (uport->cons && uport->dev)
		of_console_check(uport->dev->of_node, uport->cons->name, uport->line);

	uart_configure_port(drv, state, uport);

	port->console = uart_console(uport);

	num_groups = 2;
	if (uport->attr_group)
		num_groups++;

	uport->tty_groups = kcalloc(num_groups, sizeof(*uport->tty_groups),
				    GFP_KERNEL);
	if (!uport->tty_groups) {
		ret = -ENOMEM;
		goto out;
	}
	uport->tty_groups[0] = &tty_dev_attr_group;
	if (uport->attr_group)
		uport->tty_groups[1] = uport->attr_group;

	/*
	 * Register the port whether it's detected or not.  This allows
	 * setserial to be used to alter this port's parameters.
	 */
	tty_dev = tty_port_register_device_attr(port, drv->tty_driver,
			uport->line, uport->dev, port, uport->tty_groups);
	if (likely(!IS_ERR(tty_dev))) {
		device_set_wakeup_capable(tty_dev, 1);
	} else {
		dev_err(uport->dev, "Cannot register tty device on line %d\n",
		       uport->line);
	}

	/*
	 * Ensure UPF_DEAD is not set.
	 */
	uport->flags &= ~UPF_DEAD;

 out:
	mutex_unlock(&port->mutex);
	mutex_unlock(&port_mutex);

	return ret;
}

/**
 *	uart_remove_one_port - detach a driver defined port structure
 *	@drv: pointer to the uart low level driver structure for this port
 *	@uport: uart port structure for this port
 *
 *	This unhooks (and hangs up) the specified port structure from the
 *	core driver.  No further calls will be made to the low-level code
 *	for this port.
 */
int uart_remove_one_port(struct uart_driver *drv, struct uart_port *uport)
{
	struct uart_state *state = drv->state + uport->line;
	struct tty_port *port = &state->port;
	struct uart_port *uart_port;
	struct tty_struct *tty;
	int ret = 0;

	BUG_ON(in_interrupt());

	mutex_lock(&port_mutex);

	/*
	 * Mark the port "dead" - this prevents any opens from
	 * succeeding while we shut down the port.
	 */
	mutex_lock(&port->mutex);
	uart_port = uart_port_check(state);
	if (uart_port != uport)
		dev_alert(uport->dev, "Removing wrong port: %p != %p\n",
			  uart_port, uport);

	if (!uart_port) {
		mutex_unlock(&port->mutex);
		ret = -EINVAL;
		goto out;
	}
	uport->flags |= UPF_DEAD;
	mutex_unlock(&port->mutex);

	/*
	 * Remove the devices from the tty layer
	 */
	tty_unregister_device(drv->tty_driver, uport->line);

	tty = tty_port_tty_get(port);
	if (tty) {
		tty_vhangup(port->tty);
		tty_kref_put(tty);
	}

	/*
	 * If the port is used as a console, unregister it
	 */
	if (uart_console(uport))
		unregister_console(uport->cons);

	/*
	 * Free the port IO and memory resources, if any.
	 */
	if (uport->type != PORT_UNKNOWN && uport->ops->release_port)
		uport->ops->release_port(uport);
	kfree(uport->tty_groups);

	/*
	 * Indicate that there isn't a port here anymore.
	 */
	uport->type = PORT_UNKNOWN;

	mutex_lock(&port->mutex);
	WARN_ON(atomic_dec_return(&state->refcount) < 0);
	wait_event(state->remove_wait, !atomic_read(&state->refcount));
	state->uart_port = NULL;
	mutex_unlock(&port->mutex);
out:
	mutex_unlock(&port_mutex);

	return ret;
}

/*
 *	Are the two ports equivalent?
 */
int uart_match_port(struct uart_port *port1, struct uart_port *port2)
{
	if (port1->iotype != port2->iotype)
		return 0;

	switch (port1->iotype) {
	case UPIO_PORT:
		return (port1->iobase == port2->iobase);
	case UPIO_HUB6:
		return (port1->iobase == port2->iobase) &&
		       (port1->hub6   == port2->hub6);
	case UPIO_MEM:
	case UPIO_MEM16:
	case UPIO_MEM32:
	case UPIO_MEM32BE:
	case UPIO_AU:
	case UPIO_TSI:
		return (port1->mapbase == port2->mapbase);
	}
	return 0;
}
EXPORT_SYMBOL(uart_match_port);

/**
 *	uart_handle_dcd_change - handle a change of carrier detect state
 *	@uport: uart_port structure for the open port
 *	@status: new carrier detect status, nonzero if active
 *
 *	Caller must hold uport->lock
 */
void uart_handle_dcd_change(struct uart_port *uport, unsigned int status)
{
	struct tty_port *port = &uport->state->port;
	struct tty_struct *tty = port->tty;
	struct tty_ldisc *ld;

	lockdep_assert_held_once(&uport->lock);

	if (tty) {
		ld = tty_ldisc_ref(tty);
		if (ld) {
			if (ld->ops->dcd_change)
				ld->ops->dcd_change(tty, status);
			tty_ldisc_deref(ld);
		}
	}

	uport->icount.dcd++;

	if (uart_dcd_enabled(uport)) {
		if (status)
			wake_up_interruptible(&port->open_wait);
		else if (tty)
			tty_hangup(tty);
	}
}
EXPORT_SYMBOL_GPL(uart_handle_dcd_change);

/**
 *	uart_handle_cts_change - handle a change of clear-to-send state
 *	@uport: uart_port structure for the open port
 *	@status: new clear to send status, nonzero if active
 *
 *	Caller must hold uport->lock
 */
void uart_handle_cts_change(struct uart_port *uport, unsigned int status)
{
	lockdep_assert_held_once(&uport->lock);

	uport->icount.cts++;

	if (uart_softcts_mode(uport)) {
		if (uport->hw_stopped) {
			if (status) {
				uport->hw_stopped = 0;
				uport->ops->start_tx(uport);
				uart_write_wakeup(uport);
			}
		} else {
			if (!status) {
				uport->hw_stopped = 1;
				uport->ops->stop_tx(uport);
			}
		}

	}
}
EXPORT_SYMBOL_GPL(uart_handle_cts_change);

/**
 * uart_insert_char - push a char to the uart layer
 *
 * User is responsible to call tty_flip_buffer_push when they are done with
 * insertion.
 *
 * @port: corresponding port
 * @status: state of the serial port RX buffer (LSR for 8250)
 * @overrun: mask of overrun bits in @status
 * @ch: character to push
 * @flag: flag for the character (see TTY_NORMAL and friends)
 */
void uart_insert_char(struct uart_port *port, unsigned int status,
		 unsigned int overrun, unsigned int ch, unsigned int flag)
{
	struct tty_port *tport = &port->state->port;

	if ((status & port->ignore_status_mask & ~overrun) == 0)
		if (tty_insert_flip_char(tport, ch, flag) == 0)
			++port->icount.buf_overrun;

	/*
	 * Overrun is special.  Since it's reported immediately,
	 * it doesn't affect the current character.
	 */
	if (status & ~port->ignore_status_mask & overrun)
		if (tty_insert_flip_char(tport, 0, TTY_OVERRUN) == 0)
			++port->icount.buf_overrun;
}
EXPORT_SYMBOL_GPL(uart_insert_char);

EXPORT_SYMBOL(uart_write_wakeup);
EXPORT_SYMBOL(uart_register_driver);
EXPORT_SYMBOL(uart_unregister_driver);
EXPORT_SYMBOL(uart_suspend_port);
EXPORT_SYMBOL(uart_resume_port);
EXPORT_SYMBOL(uart_add_one_port);
EXPORT_SYMBOL(uart_remove_one_port);

MODULE_DESCRIPTION("Serial driver core");
MODULE_LICENSE("GPL");
