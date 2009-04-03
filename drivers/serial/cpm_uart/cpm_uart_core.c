/*
 *  linux/drivers/serial/cpm_uart.c
 *
 *  Driver for CPM (SCC/SMC) serial ports; core driver
 *
 *  Based on arch/ppc/cpm2_io/uart.c by Dan Malek
 *  Based on ppc8xx.c by Thomas Gleixner
 *  Based on drivers/serial/amba.c by Russell King
 *
 *  Maintainer: Kumar Gala (galak@kernel.crashing.org) (CPM2)
 *              Pantelis Antoniou (panto@intracom.gr) (CPM1)
 *
 *  Copyright (C) 2004, 2007 Freescale Semiconductor, Inc.
 *            (C) 2004 Intracom, S.A.
 *            (C) 2005-2006 MontaVista Software, Inc.
 *		Vitaly Bordug <vbordug@ru.mvista.com>
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
 *
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/dma-mapping.h>
#include <linux/fs_uart_pd.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/fs_pd.h>
#include <asm/udbg.h>

#if defined(CONFIG_SERIAL_CPM_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include <linux/kernel.h>

#include "cpm_uart.h"


/**************************************************************/

static int  cpm_uart_tx_pump(struct uart_port *port);
static void cpm_uart_init_smc(struct uart_cpm_port *pinfo);
static void cpm_uart_init_scc(struct uart_cpm_port *pinfo);
static void cpm_uart_initbd(struct uart_cpm_port *pinfo);

/**************************************************************/

/*
 * Check, if transmit buffers are processed
*/
static unsigned int cpm_uart_tx_empty(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	cbd_t __iomem *bdp = pinfo->tx_bd_base;
	int ret = 0;

	while (1) {
		if (in_be16(&bdp->cbd_sc) & BD_SC_READY)
			break;

		if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP) {
			ret = TIOCSER_TEMT;
			break;
		}
		bdp++;
	}

	pr_debug("CPM uart[%d]:tx_empty: %d\n", port->line, ret);

	return ret;
}

static void cpm_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	if (pinfo->gpios[GPIO_RTS] >= 0)
		gpio_set_value(pinfo->gpios[GPIO_RTS], !(mctrl & TIOCM_RTS));

	if (pinfo->gpios[GPIO_DTR] >= 0)
		gpio_set_value(pinfo->gpios[GPIO_DTR], !(mctrl & TIOCM_DTR));
}

static unsigned int cpm_uart_get_mctrl(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	unsigned int mctrl = TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;

	if (pinfo->gpios[GPIO_CTS] >= 0) {
		if (gpio_get_value(pinfo->gpios[GPIO_CTS]))
			mctrl &= ~TIOCM_CTS;
	}

	if (pinfo->gpios[GPIO_DSR] >= 0) {
		if (gpio_get_value(pinfo->gpios[GPIO_DSR]))
			mctrl &= ~TIOCM_DSR;
	}

	if (pinfo->gpios[GPIO_DCD] >= 0) {
		if (gpio_get_value(pinfo->gpios[GPIO_DCD]))
			mctrl &= ~TIOCM_CAR;
	}

	if (pinfo->gpios[GPIO_RI] >= 0) {
		if (!gpio_get_value(pinfo->gpios[GPIO_RI]))
			mctrl |= TIOCM_RNG;
	}

	return mctrl;
}

/*
 * Stop transmitter
 */
static void cpm_uart_stop_tx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	smc_t __iomem *smcp = pinfo->smcp;
	scc_t __iomem *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:stop tx\n", port->line);

	if (IS_SMC(pinfo))
		clrbits8(&smcp->smc_smcm, SMCM_TX);
	else
		clrbits16(&sccp->scc_sccm, UART_SCCM_TX);
}

/*
 * Start transmitter
 */
static void cpm_uart_start_tx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	smc_t __iomem *smcp = pinfo->smcp;
	scc_t __iomem *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:start tx\n", port->line);

	if (IS_SMC(pinfo)) {
		if (in_8(&smcp->smc_smcm) & SMCM_TX)
			return;
	} else {
		if (in_be16(&sccp->scc_sccm) & UART_SCCM_TX)
			return;
	}

	if (cpm_uart_tx_pump(port) != 0) {
		if (IS_SMC(pinfo)) {
			setbits8(&smcp->smc_smcm, SMCM_TX);
		} else {
			setbits16(&sccp->scc_sccm, UART_SCCM_TX);
		}
	}
}

/*
 * Stop receiver
 */
static void cpm_uart_stop_rx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	smc_t __iomem *smcp = pinfo->smcp;
	scc_t __iomem *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:stop rx\n", port->line);

	if (IS_SMC(pinfo))
		clrbits8(&smcp->smc_smcm, SMCM_RX);
	else
		clrbits16(&sccp->scc_sccm, UART_SCCM_RX);
}

/*
 * Enable Modem status interrupts
 */
static void cpm_uart_enable_ms(struct uart_port *port)
{
	pr_debug("CPM uart[%d]:enable ms\n", port->line);
}

/*
 * Generate a break.
 */
static void cpm_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	pr_debug("CPM uart[%d]:break ctrl, break_state: %d\n", port->line,
		break_state);

	if (break_state)
		cpm_line_cr_cmd(pinfo, CPM_CR_STOP_TX);
	else
		cpm_line_cr_cmd(pinfo, CPM_CR_RESTART_TX);
}

/*
 * Transmit characters, refill buffer descriptor, if possible
 */
static void cpm_uart_int_tx(struct uart_port *port)
{
	pr_debug("CPM uart[%d]:TX INT\n", port->line);

	cpm_uart_tx_pump(port);
}

#ifdef CONFIG_CONSOLE_POLL
static int serial_polled;
#endif

/*
 * Receive characters
 */
static void cpm_uart_int_rx(struct uart_port *port)
{
	int i;
	unsigned char ch;
	u8 *cp;
	struct tty_struct *tty = port->info->port.tty;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	cbd_t __iomem *bdp;
	u16 status;
	unsigned int flg;

	pr_debug("CPM uart[%d]:RX INT\n", port->line);

	/* Just loop through the closed BDs and copy the characters into
	 * the buffer.
	 */
	bdp = pinfo->rx_cur;
	for (;;) {
#ifdef CONFIG_CONSOLE_POLL
		if (unlikely(serial_polled)) {
			serial_polled = 0;
			return;
		}
#endif
		/* get status */
		status = in_be16(&bdp->cbd_sc);
		/* If this one is empty, return happy */
		if (status & BD_SC_EMPTY)
			break;

		/* get number of characters, and check spce in flip-buffer */
		i = in_be16(&bdp->cbd_datlen);

		/* If we have not enough room in tty flip buffer, then we try
		 * later, which will be the next rx-interrupt or a timeout
		 */
		if(tty_buffer_request_room(tty, i) < i) {
			printk(KERN_WARNING "No room in flip buffer\n");
			return;
		}

		/* get pointer */
		cp = cpm2cpu_addr(in_be32(&bdp->cbd_bufaddr), pinfo);

		/* loop through the buffer */
		while (i-- > 0) {
			ch = *cp++;
			port->icount.rx++;
			flg = TTY_NORMAL;

			if (status &
			    (BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV))
				goto handle_error;
			if (uart_handle_sysrq_char(port, ch))
				continue;
#ifdef CONFIG_CONSOLE_POLL
			if (unlikely(serial_polled)) {
				serial_polled = 0;
				return;
			}
#endif
		      error_return:
			tty_insert_flip_char(tty, ch, flg);

		}		/* End while (i--) */

		/* This BD is ready to be used again. Clear status. get next */
		clrbits16(&bdp->cbd_sc, BD_SC_BR | BD_SC_FR | BD_SC_PR |
		                        BD_SC_OV | BD_SC_ID);
		setbits16(&bdp->cbd_sc, BD_SC_EMPTY);

		if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP)
			bdp = pinfo->rx_bd_base;
		else
			bdp++;

	} /* End for (;;) */

	/* Write back buffer pointer */
	pinfo->rx_cur = bdp;

	/* activate BH processing */
	tty_flip_buffer_push(tty);

	return;

	/* Error processing */

      handle_error:
	/* Statistics */
	if (status & BD_SC_BR)
		port->icount.brk++;
	if (status & BD_SC_PR)
		port->icount.parity++;
	if (status & BD_SC_FR)
		port->icount.frame++;
	if (status & BD_SC_OV)
		port->icount.overrun++;

	/* Mask out ignored conditions */
	status &= port->read_status_mask;

	/* Handle the remaining ones */
	if (status & BD_SC_BR)
		flg = TTY_BREAK;
	else if (status & BD_SC_PR)
		flg = TTY_PARITY;
	else if (status & BD_SC_FR)
		flg = TTY_FRAME;

	/* overrun does not affect the current character ! */
	if (status & BD_SC_OV) {
		ch = 0;
		flg = TTY_OVERRUN;
		/* We skip this buffer */
		/* CHECK: Is really nothing senseful there */
		/* ASSUMPTION: it contains nothing valid */
		i = 0;
	}
#ifdef SUPPORT_SYSRQ
	port->sysrq = 0;
#endif
	goto error_return;
}

/*
 * Asynchron mode interrupt handler
 */
static irqreturn_t cpm_uart_int(int irq, void *data)
{
	u8 events;
	struct uart_port *port = data;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	smc_t __iomem *smcp = pinfo->smcp;
	scc_t __iomem *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:IRQ\n", port->line);

	if (IS_SMC(pinfo)) {
		events = in_8(&smcp->smc_smce);
		out_8(&smcp->smc_smce, events);
		if (events & SMCM_BRKE)
			uart_handle_break(port);
		if (events & SMCM_RX)
			cpm_uart_int_rx(port);
		if (events & SMCM_TX)
			cpm_uart_int_tx(port);
	} else {
		events = in_be16(&sccp->scc_scce);
		out_be16(&sccp->scc_scce, events);
		if (events & UART_SCCM_BRKE)
			uart_handle_break(port);
		if (events & UART_SCCM_RX)
			cpm_uart_int_rx(port);
		if (events & UART_SCCM_TX)
			cpm_uart_int_tx(port);
	}
	return (events) ? IRQ_HANDLED : IRQ_NONE;
}

static int cpm_uart_startup(struct uart_port *port)
{
	int retval;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	pr_debug("CPM uart[%d]:startup\n", port->line);

	/* If the port is not the console, make sure rx is disabled. */
	if (!(pinfo->flags & FLAG_CONSOLE)) {
		/* Disable UART rx */
		if (IS_SMC(pinfo)) {
			clrbits16(&pinfo->smcp->smc_smcmr, SMCMR_REN);
			clrbits8(&pinfo->smcp->smc_smcm, SMCM_RX);
		} else {
			clrbits32(&pinfo->sccp->scc_gsmrl, SCC_GSMRL_ENR);
			clrbits16(&pinfo->sccp->scc_sccm, UART_SCCM_RX);
		}
		cpm_line_cr_cmd(pinfo, CPM_CR_INIT_TRX);
	}
	/* Install interrupt handler. */
	retval = request_irq(port->irq, cpm_uart_int, 0, "cpm_uart", port);
	if (retval)
		return retval;

	/* Startup rx-int */
	if (IS_SMC(pinfo)) {
		setbits8(&pinfo->smcp->smc_smcm, SMCM_RX);
		setbits16(&pinfo->smcp->smc_smcmr, (SMCMR_REN | SMCMR_TEN));
	} else {
		setbits16(&pinfo->sccp->scc_sccm, UART_SCCM_RX);
		setbits32(&pinfo->sccp->scc_gsmrl, (SCC_GSMRL_ENR | SCC_GSMRL_ENT));
	}

	return 0;
}

inline void cpm_uart_wait_until_send(struct uart_cpm_port *pinfo)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(pinfo->wait_closing);
}

/*
 * Shutdown the uart
 */
static void cpm_uart_shutdown(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	pr_debug("CPM uart[%d]:shutdown\n", port->line);

	/* free interrupt handler */
	free_irq(port->irq, port);

	/* If the port is not the console, disable Rx and Tx. */
	if (!(pinfo->flags & FLAG_CONSOLE)) {
		/* Wait for all the BDs marked sent */
		while(!cpm_uart_tx_empty(port)) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(2);
		}

		if (pinfo->wait_closing)
			cpm_uart_wait_until_send(pinfo);

		/* Stop uarts */
		if (IS_SMC(pinfo)) {
			smc_t __iomem *smcp = pinfo->smcp;
			clrbits16(&smcp->smc_smcmr, SMCMR_REN | SMCMR_TEN);
			clrbits8(&smcp->smc_smcm, SMCM_RX | SMCM_TX);
		} else {
			scc_t __iomem *sccp = pinfo->sccp;
			clrbits32(&sccp->scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
			clrbits16(&sccp->scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
		}

		/* Shut them really down and reinit buffer descriptors */
		if (IS_SMC(pinfo)) {
			out_be16(&pinfo->smcup->smc_brkcr, 0);
			cpm_line_cr_cmd(pinfo, CPM_CR_STOP_TX);
		} else {
			out_be16(&pinfo->sccup->scc_brkcr, 0);
			cpm_line_cr_cmd(pinfo, CPM_CR_GRA_STOP_TX);
		}

		cpm_uart_initbd(pinfo);
	}
}

static void cpm_uart_set_termios(struct uart_port *port,
                                 struct ktermios *termios,
                                 struct ktermios *old)
{
	int baud;
	unsigned long flags;
	u16 cval, scval, prev_mode;
	int bits, sbits;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	smc_t __iomem *smcp = pinfo->smcp;
	scc_t __iomem *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:set_termios\n", port->line);

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);

	/* Character length programmed into the mode register is the
	 * sum of: 1 start bit, number of data bits, 0 or 1 parity bit,
	 * 1 or 2 stop bits, minus 1.
	 * The value 'bits' counts this for us.
	 */
	cval = 0;
	scval = 0;

	/* byte size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		bits = 5;
		break;
	case CS6:
		bits = 6;
		break;
	case CS7:
		bits = 7;
		break;
	case CS8:
		bits = 8;
		break;
		/* Never happens, but GCC is too dumb to figure it out */
	default:
		bits = 8;
		break;
	}
	sbits = bits - 5;

	if (termios->c_cflag & CSTOPB) {
		cval |= SMCMR_SL;	/* Two stops */
		scval |= SCU_PSMR_SL;
		bits++;
	}

	if (termios->c_cflag & PARENB) {
		cval |= SMCMR_PEN;
		scval |= SCU_PSMR_PEN;
		bits++;
		if (!(termios->c_cflag & PARODD)) {
			cval |= SMCMR_PM_EVEN;
			scval |= (SCU_PSMR_REVP | SCU_PSMR_TEVP);
		}
	}

	/*
	 * Update the timeout
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Set up parity check flag
	 */
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	port->read_status_mask = (BD_SC_EMPTY | BD_SC_OV);
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= BD_SC_FR | BD_SC_PR;
	if ((termios->c_iflag & BRKINT) || (termios->c_iflag & PARMRK))
		port->read_status_mask |= BD_SC_BR;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= BD_SC_PR | BD_SC_FR;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= BD_SC_BR;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= BD_SC_OV;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->read_status_mask &= ~BD_SC_EMPTY;

	spin_lock_irqsave(&port->lock, flags);

	/* Start bit has not been added (so don't, because we would just
	 * subtract it later), and we need to add one for the number of
	 * stops bits (there is always at least one).
	 */
	bits++;
	if (IS_SMC(pinfo)) {
		/* Set the mode register.  We want to keep a copy of the
		 * enables, because we want to put them back if they were
		 * present.
		 */
		prev_mode = in_be16(&smcp->smc_smcmr) & (SMCMR_REN | SMCMR_TEN);
		/* Output in *one* operation, so we don't interrupt RX/TX if they
		 * were already enabled. */
		out_be16(&smcp->smc_smcmr, smcr_mk_clen(bits) | cval |
		    SMCMR_SM_UART | prev_mode);
	} else {
		out_be16(&sccp->scc_psmr, (sbits << 12) | scval);
	}

	if (pinfo->clk)
		clk_set_rate(pinfo->clk, baud);
	else
		cpm_set_brg(pinfo->brg - 1, baud);
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *cpm_uart_type(struct uart_port *port)
{
	pr_debug("CPM uart[%d]:uart_type\n", port->line);

	return port->type == PORT_CPM ? "CPM UART" : NULL;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int cpm_uart_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	int ret = 0;

	pr_debug("CPM uart[%d]:verify_port\n", port->line);

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_CPM)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= nr_irqs)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

/*
 * Transmit characters, refill buffer descriptor, if possible
 */
static int cpm_uart_tx_pump(struct uart_port *port)
{
	cbd_t __iomem *bdp;
	u8 *p;
	int count;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	struct circ_buf *xmit = &port->info->xmit;

	/* Handle xon/xoff */
	if (port->x_char) {
		/* Pick next descriptor and fill from buffer */
		bdp = pinfo->tx_cur;

		p = cpm2cpu_addr(in_be32(&bdp->cbd_bufaddr), pinfo);

		*p++ = port->x_char;

		out_be16(&bdp->cbd_datlen, 1);
		setbits16(&bdp->cbd_sc, BD_SC_READY);
		/* Get next BD. */
		if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP)
			bdp = pinfo->tx_bd_base;
		else
			bdp++;
		pinfo->tx_cur = bdp;

		port->icount.tx++;
		port->x_char = 0;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		cpm_uart_stop_tx(port);
		return 0;
	}

	/* Pick next descriptor and fill from buffer */
	bdp = pinfo->tx_cur;

	while (!(in_be16(&bdp->cbd_sc) & BD_SC_READY) &&
	       xmit->tail != xmit->head) {
		count = 0;
		p = cpm2cpu_addr(in_be32(&bdp->cbd_bufaddr), pinfo);
		while (count < pinfo->tx_fifosize) {
			*p++ = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;
			count++;
			if (xmit->head == xmit->tail)
				break;
		}
		out_be16(&bdp->cbd_datlen, count);
		setbits16(&bdp->cbd_sc, BD_SC_READY);
		/* Get next BD. */
		if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP)
			bdp = pinfo->tx_bd_base;
		else
			bdp++;
	}
	pinfo->tx_cur = bdp;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit)) {
		cpm_uart_stop_tx(port);
		return 0;
	}

	return 1;
}

/*
 * init buffer descriptors
 */
static void cpm_uart_initbd(struct uart_cpm_port *pinfo)
{
	int i;
	u8 *mem_addr;
	cbd_t __iomem *bdp;

	pr_debug("CPM uart[%d]:initbd\n", pinfo->port.line);

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	mem_addr = pinfo->mem_addr;
	bdp = pinfo->rx_cur = pinfo->rx_bd_base;
	for (i = 0; i < (pinfo->rx_nrfifos - 1); i++, bdp++) {
		out_be32(&bdp->cbd_bufaddr, cpu2cpm_addr(mem_addr, pinfo));
		out_be16(&bdp->cbd_sc, BD_SC_EMPTY | BD_SC_INTRPT);
		mem_addr += pinfo->rx_fifosize;
	}

	out_be32(&bdp->cbd_bufaddr, cpu2cpm_addr(mem_addr, pinfo));
	out_be16(&bdp->cbd_sc, BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT);

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	mem_addr = pinfo->mem_addr + L1_CACHE_ALIGN(pinfo->rx_nrfifos * pinfo->rx_fifosize);
	bdp = pinfo->tx_cur = pinfo->tx_bd_base;
	for (i = 0; i < (pinfo->tx_nrfifos - 1); i++, bdp++) {
		out_be32(&bdp->cbd_bufaddr, cpu2cpm_addr(mem_addr, pinfo));
		out_be16(&bdp->cbd_sc, BD_SC_INTRPT);
		mem_addr += pinfo->tx_fifosize;
	}

	out_be32(&bdp->cbd_bufaddr, cpu2cpm_addr(mem_addr, pinfo));
	out_be16(&bdp->cbd_sc, BD_SC_WRAP | BD_SC_INTRPT);
}

static void cpm_uart_init_scc(struct uart_cpm_port *pinfo)
{
	scc_t __iomem *scp;
	scc_uart_t __iomem *sup;

	pr_debug("CPM uart[%d]:init_scc\n", pinfo->port.line);

	scp = pinfo->sccp;
	sup = pinfo->sccup;

	/* Store address */
	out_be16(&pinfo->sccup->scc_genscc.scc_rbase,
	         (u8 __iomem *)pinfo->rx_bd_base - DPRAM_BASE);
	out_be16(&pinfo->sccup->scc_genscc.scc_tbase,
	         (u8 __iomem *)pinfo->tx_bd_base - DPRAM_BASE);

	/* Set up the uart parameters in the
	 * parameter ram.
	 */

	cpm_set_scc_fcr(sup);

	out_be16(&sup->scc_genscc.scc_mrblr, pinfo->rx_fifosize);
	out_be16(&sup->scc_maxidl, pinfo->rx_fifosize);
	out_be16(&sup->scc_brkcr, 1);
	out_be16(&sup->scc_parec, 0);
	out_be16(&sup->scc_frmec, 0);
	out_be16(&sup->scc_nosec, 0);
	out_be16(&sup->scc_brkec, 0);
	out_be16(&sup->scc_uaddr1, 0);
	out_be16(&sup->scc_uaddr2, 0);
	out_be16(&sup->scc_toseq, 0);
	out_be16(&sup->scc_char1, 0x8000);
	out_be16(&sup->scc_char2, 0x8000);
	out_be16(&sup->scc_char3, 0x8000);
	out_be16(&sup->scc_char4, 0x8000);
	out_be16(&sup->scc_char5, 0x8000);
	out_be16(&sup->scc_char6, 0x8000);
	out_be16(&sup->scc_char7, 0x8000);
	out_be16(&sup->scc_char8, 0x8000);
	out_be16(&sup->scc_rccm, 0xc0ff);

	/* Send the CPM an initialize command.
	 */
	cpm_line_cr_cmd(pinfo, CPM_CR_INIT_TRX);

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	out_be32(&scp->scc_gsmrh, 0);
	out_be32(&scp->scc_gsmrl,
	         SCC_GSMRL_MODE_UART | SCC_GSMRL_TDCR_16 | SCC_GSMRL_RDCR_16);

	/* Enable rx interrupts  and clear all pending events.  */
	out_be16(&scp->scc_sccm, 0);
	out_be16(&scp->scc_scce, 0xffff);
	out_be16(&scp->scc_dsr, 0x7e7e);
	out_be16(&scp->scc_psmr, 0x3000);

	setbits32(&scp->scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
}

static void cpm_uart_init_smc(struct uart_cpm_port *pinfo)
{
	smc_t __iomem *sp;
	smc_uart_t __iomem *up;

	pr_debug("CPM uart[%d]:init_smc\n", pinfo->port.line);

	sp = pinfo->smcp;
	up = pinfo->smcup;

	/* Store address */
	out_be16(&pinfo->smcup->smc_rbase,
	         (u8 __iomem *)pinfo->rx_bd_base - DPRAM_BASE);
	out_be16(&pinfo->smcup->smc_tbase,
	         (u8 __iomem *)pinfo->tx_bd_base - DPRAM_BASE);

/*
 *  In case SMC1 is being relocated...
 */
#if defined (CONFIG_I2C_SPI_SMC1_UCODE_PATCH)
	out_be16(&up->smc_rbptr, in_be16(&pinfo->smcup->smc_rbase));
	out_be16(&up->smc_tbptr, in_be16(&pinfo->smcup->smc_tbase));
	out_be32(&up->smc_rstate, 0);
	out_be32(&up->smc_tstate, 0);
	out_be16(&up->smc_brkcr, 1);              /* number of break chars */
	out_be16(&up->smc_brkec, 0);
#endif

	/* Set up the uart parameters in the
	 * parameter ram.
	 */
	cpm_set_smc_fcr(up);

	/* Using idle charater time requires some additional tuning.  */
	out_be16(&up->smc_mrblr, pinfo->rx_fifosize);
	out_be16(&up->smc_maxidl, pinfo->rx_fifosize);
	out_be16(&up->smc_brklen, 0);
	out_be16(&up->smc_brkec, 0);
	out_be16(&up->smc_brkcr, 1);

	cpm_line_cr_cmd(pinfo, CPM_CR_INIT_TRX);

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	out_be16(&sp->smc_smcmr, smcr_mk_clen(9) | SMCMR_SM_UART);

	/* Enable only rx interrupts clear all pending events. */
	out_8(&sp->smc_smcm, 0);
	out_8(&sp->smc_smce, 0xff);

	setbits16(&sp->smc_smcmr, SMCMR_REN | SMCMR_TEN);
}

/*
 * Initialize port. This is called from early_console stuff
 * so we have to be careful here !
 */
static int cpm_uart_request_port(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	int ret;

	pr_debug("CPM uart[%d]:request port\n", port->line);

	if (pinfo->flags & FLAG_CONSOLE)
		return 0;

	if (IS_SMC(pinfo)) {
		clrbits8(&pinfo->smcp->smc_smcm, SMCM_RX | SMCM_TX);
		clrbits16(&pinfo->smcp->smc_smcmr, SMCMR_REN | SMCMR_TEN);
	} else {
		clrbits16(&pinfo->sccp->scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
		clrbits32(&pinfo->sccp->scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	}

	ret = cpm_uart_allocbuf(pinfo, 0);

	if (ret)
		return ret;

	cpm_uart_initbd(pinfo);
	if (IS_SMC(pinfo))
		cpm_uart_init_smc(pinfo);
	else
		cpm_uart_init_scc(pinfo);

	return 0;
}

static void cpm_uart_release_port(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	if (!(pinfo->flags & FLAG_CONSOLE))
		cpm_uart_freebuf(pinfo);
}

/*
 * Configure/autoconfigure the port.
 */
static void cpm_uart_config_port(struct uart_port *port, int flags)
{
	pr_debug("CPM uart[%d]:config_port\n", port->line);

	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_CPM;
		cpm_uart_request_port(port);
	}
}

#ifdef CONFIG_CONSOLE_POLL
/* Serial polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

#define GDB_BUF_SIZE	512	/* power of 2, please */

static char poll_buf[GDB_BUF_SIZE];
static char *pollp;
static int poll_chars;

static int poll_wait_key(char *obuf, struct uart_cpm_port *pinfo)
{
	u_char		c, *cp;
	volatile cbd_t	*bdp;
	int		i;

	/* Get the address of the host memory buffer.
	 */
	bdp = pinfo->rx_cur;
	while (bdp->cbd_sc & BD_SC_EMPTY)
		;

	/* If the buffer address is in the CPM DPRAM, don't
	 * convert it.
	 */
	cp = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);

	if (obuf) {
		i = c = bdp->cbd_datlen;
		while (i-- > 0)
			*obuf++ = *cp++;
	} else
		c = *cp;
	bdp->cbd_sc &= ~(BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV | BD_SC_ID);
	bdp->cbd_sc |= BD_SC_EMPTY;

	if (bdp->cbd_sc & BD_SC_WRAP)
		bdp = pinfo->rx_bd_base;
	else
		bdp++;
	pinfo->rx_cur = (cbd_t *)bdp;

	return (int)c;
}

static int cpm_get_poll_char(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;

	if (!serial_polled) {
		serial_polled = 1;
		poll_chars = 0;
	}
	if (poll_chars <= 0) {
		poll_chars = poll_wait_key(poll_buf, pinfo);
		pollp = poll_buf;
	}
	poll_chars--;
	return *pollp++;
}

static void cpm_put_poll_char(struct uart_port *port,
			 unsigned char c)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	static char ch[2];

	ch[0] = (char)c;
	cpm_uart_early_write(pinfo->port.line, ch, 1);
}
#endif /* CONFIG_CONSOLE_POLL */

static struct uart_ops cpm_uart_pops = {
	.tx_empty	= cpm_uart_tx_empty,
	.set_mctrl	= cpm_uart_set_mctrl,
	.get_mctrl	= cpm_uart_get_mctrl,
	.stop_tx	= cpm_uart_stop_tx,
	.start_tx	= cpm_uart_start_tx,
	.stop_rx	= cpm_uart_stop_rx,
	.enable_ms	= cpm_uart_enable_ms,
	.break_ctl	= cpm_uart_break_ctl,
	.startup	= cpm_uart_startup,
	.shutdown	= cpm_uart_shutdown,
	.set_termios	= cpm_uart_set_termios,
	.type		= cpm_uart_type,
	.release_port	= cpm_uart_release_port,
	.request_port	= cpm_uart_request_port,
	.config_port	= cpm_uart_config_port,
	.verify_port	= cpm_uart_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = cpm_get_poll_char,
	.poll_put_char = cpm_put_poll_char,
#endif
};

struct uart_cpm_port cpm_uart_ports[UART_NR];

static int cpm_uart_init_port(struct device_node *np,
                              struct uart_cpm_port *pinfo)
{
	const u32 *data;
	void __iomem *mem, *pram;
	int len;
	int ret;
	int i;

	data = of_get_property(np, "clock", NULL);
	if (data) {
		struct clk *clk = clk_get(NULL, (const char*)data);
		if (!IS_ERR(clk))
			pinfo->clk = clk;
	}
	if (!pinfo->clk) {
		data = of_get_property(np, "fsl,cpm-brg", &len);
		if (!data || len != 4) {
			printk(KERN_ERR "CPM UART %s has no/invalid "
			                "fsl,cpm-brg property.\n", np->name);
			return -EINVAL;
		}
		pinfo->brg = *data;
	}

	data = of_get_property(np, "fsl,cpm-command", &len);
	if (!data || len != 4) {
		printk(KERN_ERR "CPM UART %s has no/invalid "
		                "fsl,cpm-command property.\n", np->name);
		return -EINVAL;
	}
	pinfo->command = *data;

	mem = of_iomap(np, 0);
	if (!mem)
		return -ENOMEM;

	if (of_device_is_compatible(np, "fsl,cpm1-scc-uart") ||
	    of_device_is_compatible(np, "fsl,cpm2-scc-uart")) {
		pinfo->sccp = mem;
		pinfo->sccup = pram = cpm_uart_map_pram(pinfo, np);
	} else if (of_device_is_compatible(np, "fsl,cpm1-smc-uart") ||
	           of_device_is_compatible(np, "fsl,cpm2-smc-uart")) {
		pinfo->flags |= FLAG_SMC;
		pinfo->smcp = mem;
		pinfo->smcup = pram = cpm_uart_map_pram(pinfo, np);
	} else {
		ret = -ENODEV;
		goto out_mem;
	}

	if (!pram) {
		ret = -ENOMEM;
		goto out_mem;
	}

	pinfo->tx_nrfifos = TX_NUM_FIFO;
	pinfo->tx_fifosize = TX_BUF_SIZE;
	pinfo->rx_nrfifos = RX_NUM_FIFO;
	pinfo->rx_fifosize = RX_BUF_SIZE;

	pinfo->port.uartclk = ppc_proc_freq;
	pinfo->port.mapbase = (unsigned long)mem;
	pinfo->port.type = PORT_CPM;
	pinfo->port.ops = &cpm_uart_pops,
	pinfo->port.iotype = UPIO_MEM;
	pinfo->port.fifosize = pinfo->tx_nrfifos * pinfo->tx_fifosize;
	spin_lock_init(&pinfo->port.lock);

	pinfo->port.irq = of_irq_to_resource(np, 0, NULL);
	if (pinfo->port.irq == NO_IRQ) {
		ret = -EINVAL;
		goto out_pram;
	}

	for (i = 0; i < NUM_GPIOS; i++)
		pinfo->gpios[i] = of_get_gpio(np, i);

#ifdef CONFIG_PPC_EARLY_DEBUG_CPM
	udbg_putc = NULL;
#endif

	return cpm_uart_request_port(&pinfo->port);

out_pram:
	cpm_uart_unmap_pram(pinfo, pram);
out_mem:
	iounmap(mem);
	return ret;
}

#ifdef CONFIG_SERIAL_CPM_CONSOLE
/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	Note that this is called with interrupts already disabled
 */
static void cpm_uart_console_write(struct console *co, const char *s,
				   u_int count)
{
	struct uart_cpm_port *pinfo = &cpm_uart_ports[co->index];
	unsigned int i;
	cbd_t __iomem *bdp, *bdbase;
	unsigned char *cp;
	unsigned long flags;
	int nolock = oops_in_progress;

	if (unlikely(nolock)) {
		local_irq_save(flags);
	} else {
		spin_lock_irqsave(&pinfo->port.lock, flags);
	}

	/* Get the address of the host memory buffer.
	 */
	bdp = pinfo->tx_cur;
	bdbase = pinfo->tx_bd_base;

	/*
	 * Now, do each character.  This is not as bad as it looks
	 * since this is a holding FIFO and not a transmitting FIFO.
	 * We could add the complexity of filling the entire transmit
	 * buffer, but we would just wait longer between accesses......
	 */
	for (i = 0; i < count; i++, s++) {
		/* Wait for transmitter fifo to empty.
		 * Ready indicates output is ready, and xmt is doing
		 * that, not that it is ready for us to send.
		 */
		while ((in_be16(&bdp->cbd_sc) & BD_SC_READY) != 0)
			;

		/* Send the character out.
		 * If the buffer address is in the CPM DPRAM, don't
		 * convert it.
		 */
		cp = cpm2cpu_addr(in_be32(&bdp->cbd_bufaddr), pinfo);
		*cp = *s;

		out_be16(&bdp->cbd_datlen, 1);
		setbits16(&bdp->cbd_sc, BD_SC_READY);

		if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP)
			bdp = bdbase;
		else
			bdp++;

		/* if a LF, also do CR... */
		if (*s == 10) {
			while ((in_be16(&bdp->cbd_sc) & BD_SC_READY) != 0)
				;

			cp = cpm2cpu_addr(in_be32(&bdp->cbd_bufaddr), pinfo);
			*cp = 13;

			out_be16(&bdp->cbd_datlen, 1);
			setbits16(&bdp->cbd_sc, BD_SC_READY);

			if (in_be16(&bdp->cbd_sc) & BD_SC_WRAP)
				bdp = bdbase;
			else
				bdp++;
		}
	}

	/*
	 * Finally, Wait for transmitter & holding register to empty
	 *  and restore the IER
	 */
	while ((in_be16(&bdp->cbd_sc) & BD_SC_READY) != 0)
		;

	pinfo->tx_cur = bdp;

	if (unlikely(nolock)) {
		local_irq_restore(flags);
	} else {
		spin_unlock_irqrestore(&pinfo->port.lock, flags);
	}
}


static int __init cpm_uart_console_setup(struct console *co, char *options)
{
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;
	struct uart_cpm_port *pinfo;
	struct uart_port *port;

	struct device_node *np = NULL;
	int i = 0;

	if (co->index >= UART_NR) {
		printk(KERN_ERR "cpm_uart: console index %d too high\n",
		       co->index);
		return -ENODEV;
	}

	do {
		np = of_find_node_by_type(np, "serial");
		if (!np)
			return -ENODEV;

		if (!of_device_is_compatible(np, "fsl,cpm1-smc-uart") &&
		    !of_device_is_compatible(np, "fsl,cpm1-scc-uart") &&
		    !of_device_is_compatible(np, "fsl,cpm2-smc-uart") &&
		    !of_device_is_compatible(np, "fsl,cpm2-scc-uart"))
			i--;
	} while (i++ != co->index);

	pinfo = &cpm_uart_ports[co->index];

	pinfo->flags |= FLAG_CONSOLE;
	port = &pinfo->port;

	ret = cpm_uart_init_port(np, pinfo);
	of_node_put(np);
	if (ret)
		return ret;

	if (options) {
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	} else {
		if ((baud = uart_baudrate()) == -1)
			baud = 9600;
	}

	if (IS_SMC(pinfo)) {
		out_be16(&pinfo->smcup->smc_brkcr, 0);
		cpm_line_cr_cmd(pinfo, CPM_CR_STOP_TX);
		clrbits8(&pinfo->smcp->smc_smcm, SMCM_RX | SMCM_TX);
		clrbits16(&pinfo->smcp->smc_smcmr, SMCMR_REN | SMCMR_TEN);
	} else {
		out_be16(&pinfo->sccup->scc_brkcr, 0);
		cpm_line_cr_cmd(pinfo, CPM_CR_GRA_STOP_TX);
		clrbits16(&pinfo->sccp->scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
		clrbits32(&pinfo->sccp->scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
	}

	ret = cpm_uart_allocbuf(pinfo, 1);

	if (ret)
		return ret;

	cpm_uart_initbd(pinfo);

	if (IS_SMC(pinfo))
		cpm_uart_init_smc(pinfo);
	else
		cpm_uart_init_scc(pinfo);

	uart_set_options(port, co, baud, parity, bits, flow);
	cpm_line_cr_cmd(pinfo, CPM_CR_RESTART_TX);

	return 0;
}

static struct uart_driver cpm_reg;
static struct console cpm_scc_uart_console = {
	.name		= "ttyCPM",
	.write		= cpm_uart_console_write,
	.device		= uart_console_device,
	.setup		= cpm_uart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &cpm_reg,
};

static int __init cpm_uart_console_init(void)
{
	register_console(&cpm_scc_uart_console);
	return 0;
}

console_initcall(cpm_uart_console_init);

#define CPM_UART_CONSOLE	&cpm_scc_uart_console
#else
#define CPM_UART_CONSOLE	NULL
#endif

static struct uart_driver cpm_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "ttyCPM",
	.dev_name	= "ttyCPM",
	.major		= SERIAL_CPM_MAJOR,
	.minor		= SERIAL_CPM_MINOR,
	.cons		= CPM_UART_CONSOLE,
	.nr		= UART_NR,
};

static int probe_index;

static int __devinit cpm_uart_probe(struct of_device *ofdev,
                                    const struct of_device_id *match)
{
	int index = probe_index++;
	struct uart_cpm_port *pinfo = &cpm_uart_ports[index];
	int ret;

	pinfo->port.line = index;

	if (index >= UART_NR)
		return -ENODEV;

	dev_set_drvdata(&ofdev->dev, pinfo);

	/* initialize the device pointer for the port */
	pinfo->port.dev = &ofdev->dev;

	ret = cpm_uart_init_port(ofdev->node, pinfo);
	if (ret)
		return ret;

	return uart_add_one_port(&cpm_reg, &pinfo->port);
}

static int __devexit cpm_uart_remove(struct of_device *ofdev)
{
	struct uart_cpm_port *pinfo = dev_get_drvdata(&ofdev->dev);
	return uart_remove_one_port(&cpm_reg, &pinfo->port);
}

static struct of_device_id cpm_uart_match[] = {
	{
		.compatible = "fsl,cpm1-smc-uart",
	},
	{
		.compatible = "fsl,cpm1-scc-uart",
	},
	{
		.compatible = "fsl,cpm2-smc-uart",
	},
	{
		.compatible = "fsl,cpm2-scc-uart",
	},
	{}
};

static struct of_platform_driver cpm_uart_driver = {
	.name = "cpm_uart",
	.match_table = cpm_uart_match,
	.probe = cpm_uart_probe,
	.remove = cpm_uart_remove,
 };

static int __init cpm_uart_init(void)
{
	int ret = uart_register_driver(&cpm_reg);
	if (ret)
		return ret;

	ret = of_register_platform_driver(&cpm_uart_driver);
	if (ret)
		uart_unregister_driver(&cpm_reg);

	return ret;
}

static void __exit cpm_uart_exit(void)
{
	of_unregister_platform_driver(&cpm_uart_driver);
	uart_unregister_driver(&cpm_reg);
}

module_init(cpm_uart_init);
module_exit(cpm_uart_exit);

MODULE_AUTHOR("Kumar Gala/Antoniou Pantelis");
MODULE_DESCRIPTION("CPM SCC/SMC port driver $Revision: 0.01 $");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV(SERIAL_CPM_MAJOR, SERIAL_CPM_MINOR);
