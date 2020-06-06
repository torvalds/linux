// SPDX-License-Identifier: GPL-2.0
#include <linux/serial_reg.h>
#include <linux/serial_8250.h>

#include "8250.h"

/*
 * Freescale 16550 UART "driver", Copyright (C) 2011 Paul Gortmaker.
 *
 * This isn't a full driver; it just provides an alternate IRQ
 * handler to deal with an errata.  Everything else is just
 * using the bog standard 8250 support.
 *
 * We follow code flow of serial8250_default_handle_irq() but add
 * a check for a break and insert a dummy read on the Rx for the
 * immediately following IRQ event.
 *
 * We re-use the already existing "bug handling" lsr_saved_flags
 * field to carry the "what we just did" information from the one
 * IRQ event to the next one.
 */

int fsl8250_handle_irq(struct uart_port *port)
{
	unsigned char lsr, orig_lsr;
	unsigned long flags;
	unsigned int iir;
	struct uart_8250_port *up = up_to_u8250p(port);

	spin_lock_irqsave(&up->port.lock, flags);

	iir = port->serial_in(port, UART_IIR);
	if (iir & UART_IIR_NO_INT) {
		spin_unlock_irqrestore(&up->port.lock, flags);
		return 0;
	}

	/* This is the WAR; if last event was BRK, then read and return */
	if (unlikely(up->lsr_saved_flags & UART_LSR_BI)) {
		up->lsr_saved_flags &= ~UART_LSR_BI;
		port->serial_in(port, UART_RX);
		spin_unlock_irqrestore(&up->port.lock, flags);
		return 1;
	}

	lsr = orig_lsr = up->port.serial_in(&up->port, UART_LSR);

	/* Process incoming characters first */
	if ((lsr & (UART_LSR_DR | UART_LSR_BI)) &&
	    (up->ier & (UART_IER_RLSI | UART_IER_RDI))) {
		lsr = serial8250_rx_chars(up, lsr);
	}

	/* Stop processing interrupts on input overrun */
	if ((orig_lsr & UART_LSR_OE) && (up->overrun_backoff_time_ms > 0)) {
		unsigned long delay;

		up->ier = port->serial_in(port, UART_IER);
		if (up->ier & (UART_IER_RLSI | UART_IER_RDI)) {
			port->ops->stop_rx(port);
		} else {
			/* Keep restarting the timer until
			 * the input overrun subsides.
			 */
			cancel_delayed_work(&up->overrun_backoff);
		}

		delay = msecs_to_jiffies(up->overrun_backoff_time_ms);
		schedule_delayed_work(&up->overrun_backoff, delay);
	}

	serial8250_modem_status(up);

	if (lsr & UART_LSR_THRE)
		serial8250_tx_chars(up);

	up->lsr_saved_flags = orig_lsr;
	uart_unlock_and_check_sysrq(&up->port, flags);
	return 1;
}
EXPORT_SYMBOL_GPL(fsl8250_handle_irq);
