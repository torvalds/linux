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
 *  Copyright (C) 2004 Freescale Semiconductor, Inc.
 *            (C) 2004 Intracom, S.A.
 *            (C) 2005 MontaVista Software, Inc. by Vitaly Bordug <vbordug@ru.mvista.com>
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

#include <linux/config.h>
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

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>

#if defined(CONFIG_SERIAL_CPM_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include <linux/kernel.h>

#include "cpm_uart.h"

/***********************************************************************/

/* Track which ports are configured as uarts */
int cpm_uart_port_map[UART_NR];
/* How many ports did we config as uarts */
int cpm_uart_nr = 0;

/**************************************************************/

static int  cpm_uart_tx_pump(struct uart_port *port);
static void cpm_uart_init_smc(struct uart_cpm_port *pinfo);
static void cpm_uart_init_scc(struct uart_cpm_port *pinfo);
static void cpm_uart_initbd(struct uart_cpm_port *pinfo);

/**************************************************************/


/* Place-holder for board-specific stuff */
struct platform_device* __attribute__ ((weak)) __init
early_uart_get_pdev(int index)
{
	return NULL;
}


void cpm_uart_count(void)
{
	cpm_uart_nr = 0;
#ifdef CONFIG_SERIAL_CPM_SMC1
	cpm_uart_port_map[cpm_uart_nr++] = UART_SMC1;
#endif
#ifdef CONFIG_SERIAL_CPM_SMC2
	cpm_uart_port_map[cpm_uart_nr++] = UART_SMC2;
#endif
#ifdef CONFIG_SERIAL_CPM_SCC1
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC1;
#endif
#ifdef CONFIG_SERIAL_CPM_SCC2
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC2;
#endif
#ifdef CONFIG_SERIAL_CPM_SCC3
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC3;
#endif
#ifdef CONFIG_SERIAL_CPM_SCC4
	cpm_uart_port_map[cpm_uart_nr++] = UART_SCC4;
#endif
}

/*
 * Check, if transmit buffers are processed
*/
static unsigned int cpm_uart_tx_empty(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile cbd_t *bdp = pinfo->tx_bd_base;
	int ret = 0;

	while (1) {
		if (bdp->cbd_sc & BD_SC_READY)
			break;

		if (bdp->cbd_sc & BD_SC_WRAP) {
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
	/* Whee. Do nothing. */
}

static unsigned int cpm_uart_get_mctrl(struct uart_port *port)
{
	/* Whee. Do nothing. */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/*
 * Stop transmitter
 */
static void cpm_uart_stop_tx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile smc_t *smcp = pinfo->smcp;
	volatile scc_t *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:stop tx\n", port->line);

	if (IS_SMC(pinfo))
		smcp->smc_smcm &= ~SMCM_TX;
	else
		sccp->scc_sccm &= ~UART_SCCM_TX;
}

/*
 * Start transmitter
 */
static void cpm_uart_start_tx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile smc_t *smcp = pinfo->smcp;
	volatile scc_t *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:start tx\n", port->line);

	if (IS_SMC(pinfo)) {
		if (smcp->smc_smcm & SMCM_TX)
			return;
	} else {
		if (sccp->scc_sccm & UART_SCCM_TX)
			return;
	}

	if (cpm_uart_tx_pump(port) != 0) {
		if (IS_SMC(pinfo)) {
			smcp->smc_smcm |= SMCM_TX;
			smcp->smc_smcmr |= SMCMR_TEN;
		} else {
			sccp->scc_sccm |= UART_SCCM_TX;
			pinfo->sccp->scc_gsmrl |= SCC_GSMRL_ENT;
		}
	}
}

/*
 * Stop receiver
 */
static void cpm_uart_stop_rx(struct uart_port *port)
{
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile smc_t *smcp = pinfo->smcp;
	volatile scc_t *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:stop rx\n", port->line);

	if (IS_SMC(pinfo))
		smcp->smc_smcm &= ~SMCM_RX;
	else
		sccp->scc_sccm &= ~UART_SCCM_RX;
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
	int line = pinfo - cpm_uart_ports;

	pr_debug("CPM uart[%d]:break ctrl, break_state: %d\n", port->line,
		break_state);

	if (break_state)
		cpm_line_cr_cmd(line, CPM_CR_STOP_TX);
	else
		cpm_line_cr_cmd(line, CPM_CR_RESTART_TX);
}

/*
 * Transmit characters, refill buffer descriptor, if possible
 */
static void cpm_uart_int_tx(struct uart_port *port, struct pt_regs *regs)
{
	pr_debug("CPM uart[%d]:TX INT\n", port->line);

	cpm_uart_tx_pump(port);
}

/*
 * Receive characters
 */
static void cpm_uart_int_rx(struct uart_port *port, struct pt_regs *regs)
{
	int i;
	unsigned char ch, *cp;
	struct tty_struct *tty = port->info->tty;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile cbd_t *bdp;
	u16 status;
	unsigned int flg;

	pr_debug("CPM uart[%d]:RX INT\n", port->line);

	/* Just loop through the closed BDs and copy the characters into
	 * the buffer.
	 */
	bdp = pinfo->rx_cur;
	for (;;) {
		/* get status */
		status = bdp->cbd_sc;
		/* If this one is empty, return happy */
		if (status & BD_SC_EMPTY)
			break;

		/* get number of characters, and check spce in flip-buffer */
		i = bdp->cbd_datlen;

		/* If we have not enough room in tty flip buffer, then we try
		 * later, which will be the next rx-interrupt or a timeout
		 */
		if(tty_buffer_request_room(tty, i) < i) {
			printk(KERN_WARNING "No room in flip buffer\n");
			return;
		}

		/* get pointer */
		cp = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);

		/* loop through the buffer */
		while (i-- > 0) {
			ch = *cp++;
			port->icount.rx++;
			flg = TTY_NORMAL;

			if (status &
			    (BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV))
				goto handle_error;
			if (uart_handle_sysrq_char(port, ch, regs))
				continue;

		      error_return:
			tty_insert_flip_char(tty, ch, flg);

		}		/* End while (i--) */

		/* This BD is ready to be used again. Clear status. get next */
		bdp->cbd_sc &= ~(BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV | BD_SC_ID);
		bdp->cbd_sc |= BD_SC_EMPTY;

		if (bdp->cbd_sc & BD_SC_WRAP)
			bdp = pinfo->rx_bd_base;
		else
			bdp++;

	} /* End for (;;) */

	/* Write back buffer pointer */
	pinfo->rx_cur = (volatile cbd_t *) bdp;

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
static irqreturn_t cpm_uart_int(int irq, void *data, struct pt_regs *regs)
{
	u8 events;
	struct uart_port *port = (struct uart_port *)data;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile smc_t *smcp = pinfo->smcp;
	volatile scc_t *sccp = pinfo->sccp;

	pr_debug("CPM uart[%d]:IRQ\n", port->line);

	if (IS_SMC(pinfo)) {
		events = smcp->smc_smce;
		smcp->smc_smce = events;
		if (events & SMCM_BRKE)
			uart_handle_break(port);
		if (events & SMCM_RX)
			cpm_uart_int_rx(port, regs);
		if (events & SMCM_TX)
			cpm_uart_int_tx(port, regs);
	} else {
		events = sccp->scc_scce;
		sccp->scc_scce = events;
		if (events & UART_SCCM_BRKE)
			uart_handle_break(port);
		if (events & UART_SCCM_RX)
			cpm_uart_int_rx(port, regs);
		if (events & UART_SCCM_TX)
			cpm_uart_int_tx(port, regs);
	}
	return (events) ? IRQ_HANDLED : IRQ_NONE;
}

static int cpm_uart_startup(struct uart_port *port)
{
	int retval;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	int line = pinfo - cpm_uart_ports;

	pr_debug("CPM uart[%d]:startup\n", port->line);

	/* Install interrupt handler. */
	retval = request_irq(port->irq, cpm_uart_int, 0, "cpm_uart", port);
	if (retval)
		return retval;

	/* Startup rx-int */
	if (IS_SMC(pinfo)) {
		pinfo->smcp->smc_smcm |= SMCM_RX;
		pinfo->smcp->smc_smcmr |= SMCMR_REN;
	} else {
		pinfo->sccp->scc_sccm |= UART_SCCM_RX;
	}

	if (!(pinfo->flags & FLAG_CONSOLE))
		cpm_line_cr_cmd(line,CPM_CR_INIT_TRX);
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
	int line = pinfo - cpm_uart_ports;

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
			volatile smc_t *smcp = pinfo->smcp;
			smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
			smcp->smc_smcm &= ~(SMCM_RX | SMCM_TX);
		} else {
			volatile scc_t *sccp = pinfo->sccp;
			sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
			sccp->scc_sccm &= ~(UART_SCCM_TX | UART_SCCM_RX);
		}

		/* Shut them really down and reinit buffer descriptors */
		cpm_line_cr_cmd(line, CPM_CR_STOP_TX);
		cpm_uart_initbd(pinfo);
	}
}

static void cpm_uart_set_termios(struct uart_port *port,
				 struct termios *termios, struct termios *old)
{
	int baud;
	unsigned long flags;
	u16 cval, scval, prev_mode;
	int bits, sbits;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	volatile smc_t *smcp = pinfo->smcp;
	volatile scc_t *sccp = pinfo->sccp;

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
		prev_mode = smcp->smc_smcmr;
		smcp->smc_smcmr = smcr_mk_clen(bits) | cval | SMCMR_SM_UART;
		smcp->smc_smcmr |= (prev_mode & (SMCMR_REN | SMCMR_TEN));
	} else {
		sccp->scc_psmr = (sbits << 12) | scval;
	}

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
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
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
	volatile cbd_t *bdp;
	unsigned char *p;
	int count;
	struct uart_cpm_port *pinfo = (struct uart_cpm_port *)port;
	struct circ_buf *xmit = &port->info->xmit;

	/* Handle xon/xoff */
	if (port->x_char) {
		/* Pick next descriptor and fill from buffer */
		bdp = pinfo->tx_cur;

		p = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);

		*p++ = port->x_char;
		bdp->cbd_datlen = 1;
		bdp->cbd_sc |= BD_SC_READY;
		/* Get next BD. */
		if (bdp->cbd_sc & BD_SC_WRAP)
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

	while (!(bdp->cbd_sc & BD_SC_READY) && (xmit->tail != xmit->head)) {
		count = 0;
		p = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);
		while (count < pinfo->tx_fifosize) {
			*p++ = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;
			count++;
			if (xmit->head == xmit->tail)
				break;
		}
		bdp->cbd_datlen = count;
		bdp->cbd_sc |= BD_SC_READY;
		__asm__("eieio");
		/* Get next BD. */
		if (bdp->cbd_sc & BD_SC_WRAP)
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
	volatile cbd_t *bdp;

	pr_debug("CPM uart[%d]:initbd\n", pinfo->port.line);

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	mem_addr = pinfo->mem_addr;
	bdp = pinfo->rx_cur = pinfo->rx_bd_base;
	for (i = 0; i < (pinfo->rx_nrfifos - 1); i++, bdp++) {
		bdp->cbd_bufaddr = cpu2cpm_addr(mem_addr, pinfo);
		bdp->cbd_sc = BD_SC_EMPTY | BD_SC_INTRPT;
		mem_addr += pinfo->rx_fifosize;
	}

	bdp->cbd_bufaddr = cpu2cpm_addr(mem_addr, pinfo);
	bdp->cbd_sc = BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT;

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	mem_addr = pinfo->mem_addr + L1_CACHE_ALIGN(pinfo->rx_nrfifos * pinfo->rx_fifosize);
	bdp = pinfo->tx_cur = pinfo->tx_bd_base;
	for (i = 0; i < (pinfo->tx_nrfifos - 1); i++, bdp++) {
		bdp->cbd_bufaddr = cpu2cpm_addr(mem_addr, pinfo);
		bdp->cbd_sc = BD_SC_INTRPT;
		mem_addr += pinfo->tx_fifosize;
	}

	bdp->cbd_bufaddr = cpu2cpm_addr(mem_addr, pinfo);
	bdp->cbd_sc = BD_SC_WRAP | BD_SC_INTRPT;
}

static void cpm_uart_init_scc(struct uart_cpm_port *pinfo)
{
	int line = pinfo - cpm_uart_ports;
	volatile scc_t *scp;
	volatile scc_uart_t *sup;

	pr_debug("CPM uart[%d]:init_scc\n", pinfo->port.line);

	scp = pinfo->sccp;
	sup = pinfo->sccup;

	/* Store address */
	pinfo->sccup->scc_genscc.scc_rbase = (unsigned char *)pinfo->rx_bd_base - DPRAM_BASE;
	pinfo->sccup->scc_genscc.scc_tbase = (unsigned char *)pinfo->tx_bd_base - DPRAM_BASE;

	/* Set up the uart parameters in the
	 * parameter ram.
	 */

	cpm_set_scc_fcr(sup);

	sup->scc_genscc.scc_mrblr = pinfo->rx_fifosize;
	sup->scc_maxidl = pinfo->rx_fifosize;
	sup->scc_brkcr = 1;
	sup->scc_parec = 0;
	sup->scc_frmec = 0;
	sup->scc_nosec = 0;
	sup->scc_brkec = 0;
	sup->scc_uaddr1 = 0;
	sup->scc_uaddr2 = 0;
	sup->scc_toseq = 0;
	sup->scc_char1 = 0x8000;
	sup->scc_char2 = 0x8000;
	sup->scc_char3 = 0x8000;
	sup->scc_char4 = 0x8000;
	sup->scc_char5 = 0x8000;
	sup->scc_char6 = 0x8000;
	sup->scc_char7 = 0x8000;
	sup->scc_char8 = 0x8000;
	sup->scc_rccm = 0xc0ff;

	/* Send the CPM an initialize command.
	 */
	cpm_line_cr_cmd(line, CPM_CR_INIT_TRX);

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	scp->scc_gsmrh = 0;
	scp->scc_gsmrl =
	    (SCC_GSMRL_MODE_UART | SCC_GSMRL_TDCR_16 | SCC_GSMRL_RDCR_16);

	/* Enable rx interrupts  and clear all pending events.  */
	scp->scc_sccm = 0;
	scp->scc_scce = 0xffff;
	scp->scc_dsr = 0x7e7e;
	scp->scc_psmr = 0x3000;

	scp->scc_gsmrl |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);
}

static void cpm_uart_init_smc(struct uart_cpm_port *pinfo)
{
	int line = pinfo - cpm_uart_ports;
	volatile smc_t *sp;
	volatile smc_uart_t *up;

	pr_debug("CPM uart[%d]:init_smc\n", pinfo->port.line);

	sp = pinfo->smcp;
	up = pinfo->smcup;

	/* Store address */
	pinfo->smcup->smc_rbase = (u_char *)pinfo->rx_bd_base - DPRAM_BASE;
	pinfo->smcup->smc_tbase = (u_char *)pinfo->tx_bd_base - DPRAM_BASE;

/*
 *  In case SMC1 is being relocated...
 */
#if defined (CONFIG_I2C_SPI_SMC1_UCODE_PATCH)
	up->smc_rbptr = pinfo->smcup->smc_rbase;
	up->smc_tbptr = pinfo->smcup->smc_tbase;
	up->smc_rstate = 0;
	up->smc_tstate = 0;
	up->smc_brkcr = 1;              /* number of break chars */
	up->smc_brkec = 0;
#endif

	/* Set up the uart parameters in the
	 * parameter ram.
	 */
	cpm_set_smc_fcr(up);

	/* Using idle charater time requires some additional tuning.  */
	up->smc_mrblr = pinfo->rx_fifosize;
	up->smc_maxidl = pinfo->rx_fifosize;
	up->smc_brklen = 0;
	up->smc_brkec = 0;
	up->smc_brkcr = 1;

	cpm_line_cr_cmd(line, CPM_CR_INIT_TRX);

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	sp->smc_smcmr = smcr_mk_clen(9) | SMCMR_SM_UART;

	/* Enable only rx interrupts clear all pending events. */
	sp->smc_smcm = 0;
	sp->smc_smce = 0xff;

	sp->smc_smcmr |= (SMCMR_REN | SMCMR_TEN);
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
		pinfo->smcp->smc_smcm &= ~(SMCM_RX | SMCM_TX);
		pinfo->smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	} else {
		pinfo->sccp->scc_sccm &= ~(UART_SCCM_TX | UART_SCCM_RX);
		pinfo->sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
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
};

struct uart_cpm_port cpm_uart_ports[UART_NR] = {
	[UART_SMC1] = {
		.port = {
			.irq		= SMC1_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.flags = FLAG_SMC,
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = smc1_lineif,
	},
	[UART_SMC2] = {
		.port = {
			.irq		= SMC2_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.flags = FLAG_SMC,
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = smc2_lineif,
#ifdef CONFIG_SERIAL_CPM_ALT_SMC2
		.is_portb = 1,
#endif
	},
	[UART_SCC1] = {
		.port = {
			.irq		= SCC1_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = scc1_lineif,
		.wait_closing = SCC_WAIT_CLOSING,
	},
	[UART_SCC2] = {
		.port = {
			.irq		= SCC2_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = scc2_lineif,
		.wait_closing = SCC_WAIT_CLOSING,
	},
	[UART_SCC3] = {
		.port = {
			.irq		= SCC3_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = scc3_lineif,
		.wait_closing = SCC_WAIT_CLOSING,
	},
	[UART_SCC4] = {
		.port = {
			.irq		= SCC4_IRQ,
			.ops		= &cpm_uart_pops,
			.iotype		= UPIO_MEM,
			.lock		= SPIN_LOCK_UNLOCKED,
		},
		.tx_nrfifos = TX_NUM_FIFO,
		.tx_fifosize = TX_BUF_SIZE,
		.rx_nrfifos = RX_NUM_FIFO,
		.rx_fifosize = RX_BUF_SIZE,
		.set_lineif = scc4_lineif,
		.wait_closing = SCC_WAIT_CLOSING,
	},
};

int cpm_uart_drv_get_platform_data(struct platform_device *pdev, int is_con)
{
	struct resource *r;
	struct fs_uart_platform_info *pdata = pdev->dev.platform_data;
	int idx = pdata->fs_no;	/* It is UART_SMCx or UART_SCCx index */
	struct uart_cpm_port *pinfo;
	int line;
	u32 mem, pram;

	for (line=0; line<UART_NR && cpm_uart_port_map[line]!=pdata->fs_no; line++);

	pinfo = (struct uart_cpm_port *) &cpm_uart_ports[idx];

	pinfo->brg = pdata->brg;

	if (!is_con) {
		pinfo->port.line = line;
		pinfo->port.flags = UPF_BOOT_AUTOCONF;
	}

	if (!(r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs")))
		return -EINVAL;
	mem = r->start;

	if (!(r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pram")))
		return -EINVAL;
	pram = r->start;

	if(idx > fsid_smc2_uart) {
		pinfo->sccp = (scc_t *)mem;
		pinfo->sccup = (scc_uart_t *)pram;
	} else {
		pinfo->smcp = (smc_t *)mem;
		pinfo->smcup = (smc_uart_t *)pram;
	}
	pinfo->tx_nrfifos = pdata->tx_num_fifo;
	pinfo->tx_fifosize = pdata->tx_buf_size;

	pinfo->rx_nrfifos = pdata->rx_num_fifo;
	pinfo->rx_fifosize = pdata->rx_buf_size;

	pinfo->port.uartclk = pdata->uart_clk;
	pinfo->port.mapbase = (unsigned long)mem;
	pinfo->port.irq = platform_get_irq(pdev, 0);

	return 0;
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
	struct uart_cpm_port *pinfo =
	    &cpm_uart_ports[cpm_uart_port_map[co->index]];
	unsigned int i;
	volatile cbd_t *bdp, *bdbase;
	volatile unsigned char *cp;

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
		while ((bdp->cbd_sc & BD_SC_READY) != 0)
			;

		/* Send the character out.
		 * If the buffer address is in the CPM DPRAM, don't
		 * convert it.
		 */
		cp = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);

		*cp = *s;

		bdp->cbd_datlen = 1;
		bdp->cbd_sc |= BD_SC_READY;

		if (bdp->cbd_sc & BD_SC_WRAP)
			bdp = bdbase;
		else
			bdp++;

		/* if a LF, also do CR... */
		if (*s == 10) {
			while ((bdp->cbd_sc & BD_SC_READY) != 0)
				;

			cp = cpm2cpu_addr(bdp->cbd_bufaddr, pinfo);

			*cp = 13;
			bdp->cbd_datlen = 1;
			bdp->cbd_sc |= BD_SC_READY;

			if (bdp->cbd_sc & BD_SC_WRAP)
				bdp = bdbase;
			else
				bdp++;
		}
	}

	/*
	 * Finally, Wait for transmitter & holding register to empty
	 *  and restore the IER
	 */
	while ((bdp->cbd_sc & BD_SC_READY) != 0)
		;

	pinfo->tx_cur = (volatile cbd_t *) bdp;
}


static int __init cpm_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	struct uart_cpm_port *pinfo;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	struct fs_uart_platform_info *pdata;
	struct platform_device* pdev = early_uart_get_pdev(co->index);

	port =
	    (struct uart_port *)&cpm_uart_ports[cpm_uart_port_map[co->index]];
	pinfo = (struct uart_cpm_port *)port;
	if (!pdev) {
		pr_info("cpm_uart: console: compat mode\n");
		/* compatibility - will be cleaned up */
		cpm_uart_init_portdesc();

		if (pinfo->set_lineif)
			pinfo->set_lineif(pinfo);
	} else {
		pdata = pdev->dev.platform_data;
		if (pdata)
			if (pdata->init_ioports)
    	                	pdata->init_ioports();

		cpm_uart_drv_get_platform_data(pdev, 1);
	}

	pinfo->flags |= FLAG_CONSOLE;

	if (options) {
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	} else {
		bd_t *bd = (bd_t *) __res;

		if (bd->bi_baudrate)
			baud = bd->bi_baudrate;
		else
			baud = 9600;
	}

	if (IS_SMC(pinfo)) {
		pinfo->smcp->smc_smcm &= ~(SMCM_RX | SMCM_TX);
		pinfo->smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	} else {
		pinfo->sccp->scc_sccm &= ~(UART_SCCM_TX | UART_SCCM_RX);
		pinfo->sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
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

int __init cpm_uart_console_init(void)
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
};
static int cpm_uart_drv_probe(struct device *dev)
{
	struct platform_device  *pdev = to_platform_device(dev);
	struct fs_uart_platform_info *pdata;
	int ret = -ENODEV;

	if(!pdev) {
		printk(KERN_ERR"CPM UART: platform data missing!\n");
		return ret;
	}

	pdata = pdev->dev.platform_data;
	pr_debug("cpm_uart_drv_probe: Adding CPM UART %d\n",
			cpm_uart_port_map[pdata->fs_no]);

	if ((ret = cpm_uart_drv_get_platform_data(pdev, 0)))
		return ret;

	if (pdata->init_ioports)
                pdata->init_ioports();

	ret = uart_add_one_port(&cpm_reg, &cpm_uart_ports[pdata->fs_no].port);

        return ret;
}

static int cpm_uart_drv_remove(struct device *dev)
{
	struct platform_device  *pdev = to_platform_device(dev);
	struct fs_uart_platform_info *pdata = pdev->dev.platform_data;

	pr_debug("cpm_uart_drv_remove: Removing CPM UART %d\n",
			cpm_uart_port_map[pdata->fs_no]);

        uart_remove_one_port(&cpm_reg, &cpm_uart_ports[pdata->fs_no].port);
        return 0;
}

static struct device_driver cpm_smc_uart_driver = {
        .name   = "fsl-cpm-smc:uart",
        .bus    = &platform_bus_type,
        .probe  = cpm_uart_drv_probe,
        .remove = cpm_uart_drv_remove,
};

static struct device_driver cpm_scc_uart_driver = {
        .name   = "fsl-cpm-scc:uart",
        .bus    = &platform_bus_type,
        .probe  = cpm_uart_drv_probe,
        .remove = cpm_uart_drv_remove,
};

/*
   This is supposed to match uart devices on platform bus,
   */
static int match_is_uart (struct device* dev, void* data)
{
	struct platform_device* pdev = container_of(dev, struct platform_device, dev);
	int ret = 0;
	/* this was setfunc as uart */
	if(strstr(pdev->name,":uart")) {
		ret = 1;
	}
	return ret;
}


static int cpm_uart_init(void) {

	int ret;
	int i;
	struct device *dev;
	printk(KERN_INFO "Serial: CPM driver $Revision: 0.02 $\n");

	/* lookup the bus for uart devices */
	dev = bus_find_device(&platform_bus_type, NULL, 0, match_is_uart);

	/* There are devices on the bus - all should be OK  */
	if (dev) {
		cpm_uart_count();
		cpm_reg.nr = cpm_uart_nr;

		if (!(ret = uart_register_driver(&cpm_reg))) {
			if ((ret = driver_register(&cpm_smc_uart_driver))) {
				uart_unregister_driver(&cpm_reg);
				return ret;
			}
			if ((ret = driver_register(&cpm_scc_uart_driver))) {
				driver_unregister(&cpm_scc_uart_driver);
				uart_unregister_driver(&cpm_reg);
			}
		}
	} else {
	/* No capable platform devices found - falling back to legacy mode */
		pr_info("cpm_uart: WARNING: no UART devices found on platform bus!\n");
		pr_info(
		"cpm_uart: the driver will guess configuration, but this mode is no longer supported.\n");
#ifndef CONFIG_SERIAL_CPM_CONSOLE
		ret = cpm_uart_init_portdesc();
		if (ret)
			return ret;
#endif

		cpm_reg.nr = cpm_uart_nr;
		ret = uart_register_driver(&cpm_reg);

		if (ret)
			return ret;

		for (i = 0; i < cpm_uart_nr; i++) {
			int con = cpm_uart_port_map[i];
			cpm_uart_ports[con].port.line = i;
			cpm_uart_ports[con].port.flags = UPF_BOOT_AUTOCONF;
			uart_add_one_port(&cpm_reg, &cpm_uart_ports[con].port);
		}

	}
	return ret;
}

static void __exit cpm_uart_exit(void)
{
	driver_unregister(&cpm_scc_uart_driver);
	driver_unregister(&cpm_smc_uart_driver);
	uart_unregister_driver(&cpm_reg);
}

module_init(cpm_uart_init);
module_exit(cpm_uart_exit);

MODULE_AUTHOR("Kumar Gala/Antoniou Pantelis");
MODULE_DESCRIPTION("CPM SCC/SMC port driver $Revision: 0.01 $");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV(SERIAL_CPM_MAJOR, SERIAL_CPM_MINOR);
