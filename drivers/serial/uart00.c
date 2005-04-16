/*
 *  linux/drivers/serial/uart00.c
 *
 *  Driver for UART00 serial ports
 *
 *  Based on drivers/char/serial_amba.c, by ARM Limited & 
 *                                          Deep Blue Solutions Ltd.
 *  Copyright 2001 Altera Corporation
 *
 *  Update for 2.6.4 by Dirk Behme <dirk.behme@de.bosch.com>
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
 *  $Id: uart00.c,v 1.35 2002/07/28 10:03:28 rmk Exp $
 *
 */
#include <linux/config.h>

#if defined(CONFIG_SERIAL_UART00_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/arch/excalibur.h>
#define UART00_TYPE (volatile unsigned int*)
#include <asm/arch/uart00.h>
#include <asm/arch/int_ctrl00.h>

#define UART_NR		2

#define SERIAL_UART00_NAME	"ttyUA"
#define SERIAL_UART00_MAJOR	204
#define SERIAL_UART00_MINOR	16      /* Temporary - will change in future */
#define SERIAL_UART00_NR	UART_NR
#define UART_PORT_SIZE 0x50

#define UART00_ISR_PASS_LIMIT	256

/*
 * Access macros for the UART00 UARTs
 */
#define UART_GET_INT_STATUS(p)	inl(UART_ISR((p)->membase))
#define UART_PUT_IES(p, c)      outl(c,UART_IES((p)->membase))
#define UART_GET_IES(p)         inl(UART_IES((p)->membase))
#define UART_PUT_IEC(p, c)      outl(c,UART_IEC((p)->membase))
#define UART_GET_IEC(p)         inl(UART_IEC((p)->membase))
#define UART_PUT_CHAR(p, c)     outl(c,UART_TD((p)->membase))
#define UART_GET_CHAR(p)        inl(UART_RD((p)->membase))
#define UART_GET_RSR(p)         inl(UART_RSR((p)->membase))
#define UART_GET_RDS(p)         inl(UART_RDS((p)->membase))
#define UART_GET_MSR(p)         inl(UART_MSR((p)->membase))
#define UART_GET_MCR(p)         inl(UART_MCR((p)->membase))
#define UART_PUT_MCR(p, c)      outl(c,UART_MCR((p)->membase))
#define UART_GET_MC(p)          inl(UART_MC((p)->membase))
#define UART_PUT_MC(p, c)       outl(c,UART_MC((p)->membase))
#define UART_GET_TSR(p)         inl(UART_TSR((p)->membase))
#define UART_GET_DIV_HI(p)	inl(UART_DIV_HI((p)->membase))
#define UART_PUT_DIV_HI(p,c)	outl(c,UART_DIV_HI((p)->membase))
#define UART_GET_DIV_LO(p)	inl(UART_DIV_LO((p)->membase))
#define UART_PUT_DIV_LO(p,c)	outl(c,UART_DIV_LO((p)->membase))
#define UART_RX_DATA(s)		((s) & UART_RSR_RX_LEVEL_MSK)
#define UART_TX_READY(s)	(((s) & UART_TSR_TX_LEVEL_MSK) < 15)
//#define UART_TX_EMPTY(p)	((UART_GET_FR(p) & UART00_UARTFR_TMSK) == 0)

static void uart00_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	UART_PUT_IEC(port, UART_IEC_TIE_MSK);
}

static void uart00_stop_rx(struct uart_port *port)
{
	UART_PUT_IEC(port, UART_IEC_RE_MSK);
}

static void uart00_enable_ms(struct uart_port *port)
{
	UART_PUT_IES(port, UART_IES_ME_MSK);
}

static void
uart00_rx_chars(struct uart_port *port, struct pt_regs *regs)
{
	struct tty_struct *tty = port->info->tty;
	unsigned int status, ch, rds, flg, ignored = 0;

	status = UART_GET_RSR(port);
	while (UART_RX_DATA(status)) {
		/* 
		 * We need to read rds before reading the 
		 * character from the fifo
		 */
		rds = UART_GET_RDS(port);
		ch = UART_GET_CHAR(port);
		port->icount.rx++;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;

		flg = TTY_NORMAL;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		if (rds & (UART_RDS_BI_MSK |UART_RDS_FE_MSK|
			   UART_RDS_PE_MSK |UART_RDS_PE_MSK))
			goto handle_error;
		if (uart_handle_sysrq_char(port, ch, regs))
			goto ignore_char;

	error_return:
		tty_insert_flip_char(tty, ch, flg);

	ignore_char:
		status = UART_GET_RSR(port);
	}
 out:
	tty_flip_buffer_push(tty);
	return;

 handle_error:
	if (rds & UART_RDS_BI_MSK) {
		status &= ~(UART_RDS_FE_MSK | UART_RDS_PE_MSK);
		port->icount.brk++;
		if (uart_handle_break(port))
			goto ignore_char;
	} else if (rds & UART_RDS_PE_MSK)
		port->icount.parity++;
	else if (rds & UART_RDS_FE_MSK)
		port->icount.frame++;
	if (rds & UART_RDS_OE_MSK)
		port->icount.overrun++;

	if (rds & port->ignore_status_mask) {
		if (++ignored > 100)
			goto out;
		goto ignore_char;
	}
	rds &= port->read_status_mask;

	if (rds & UART_RDS_BI_MSK)
		flg = TTY_BREAK;
	else if (rds & UART_RDS_PE_MSK)
		flg = TTY_PARITY;
	else if (rds & UART_RDS_FE_MSK)
		flg = TTY_FRAME;

	if (rds & UART_RDS_OE_MSK) {
		/*
		 * CHECK: does overrun affect the current character?
		 * ASSUMPTION: it does not.
		 */
		tty_insert_flip_char(tty, ch, flg);
		ch = 0;
		flg = TTY_OVERRUN;
	}
#ifdef SUPPORT_SYSRQ
	port->sysrq = 0;
#endif
	goto error_return;
}

static void uart00_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int count;

	if (port->x_char) {
		while ((UART_GET_TSR(port) & UART_TSR_TX_LEVEL_MSK) == 15)
			barrier();
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		uart00_stop_tx(port, 0);
		return;
	}

	count = port->fifosize >> 1;
	do {
		while ((UART_GET_TSR(port) & UART_TSR_TX_LEVEL_MSK) == 15)
			barrier();
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		uart00_stop_tx(port, 0);
}

static void uart00_start_tx(struct uart_port *port, unsigned int tty_start)
{
	UART_PUT_IES(port, UART_IES_TIE_MSK);
	uart00_tx_chars(port);
}

static void uart00_modem_status(struct uart_port *port)
{
	unsigned int status;

	status = UART_GET_MSR(port);

	if (!(status & (UART_MSR_DCTS_MSK | UART_MSR_DDSR_MSK | 
			UART_MSR_TERI_MSK | UART_MSR_DDCD_MSK)))
		return;

	if (status & UART_MSR_DDCD_MSK)
		uart_handle_dcd_change(port, status & UART_MSR_DCD_MSK);

	if (status & UART_MSR_DDSR_MSK)
		port->icount.dsr++;

	if (status & UART_MSR_DCTS_MSK)
		uart_handle_cts_change(port, status & UART_MSR_CTS_MSK);

	wake_up_interruptible(&port->info->delta_msr_wait);
}

static irqreturn_t uart00_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	unsigned int status, pass_counter = 0;

	status = UART_GET_INT_STATUS(port);
	do {
		if (status & UART_ISR_RI_MSK)
			uart00_rx_chars(port, regs);
		if (status & UART_ISR_MI_MSK)
			uart00_modem_status(port);
		if (status & (UART_ISR_TI_MSK | UART_ISR_TII_MSK))
			uart00_tx_chars(port);
		if (pass_counter++ > UART00_ISR_PASS_LIMIT)
			break;

		status = UART_GET_INT_STATUS(port);
	} while (status);

	return IRQ_HANDLED;
}

static unsigned int uart00_tx_empty(struct uart_port *port)
{
	return UART_GET_TSR(port) & UART_TSR_TX_LEVEL_MSK? 0 : TIOCSER_TEMT;
}

static unsigned int uart00_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;

	status = UART_GET_MSR(port);
	if (status & UART_MSR_DCD_MSK)
		result |= TIOCM_CAR;
	if (status & UART_MSR_DSR_MSK)
		result |= TIOCM_DSR;
	if (status & UART_MSR_CTS_MSK)
		result |= TIOCM_CTS;
	if (status & UART_MSR_RI_MSK)
		result |= TIOCM_RI;

	return result;
}

static void uart00_set_mctrl_null(struct uart_port *port, unsigned int mctrl)
{
}

static void uart00_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int mcr;

	spin_lock_irqsave(&port->lock, flags);
	mcr = UART_GET_MCR(port);
	if (break_state == -1)
		mcr |= UART_MCR_BR_MSK;
	else
		mcr &= ~UART_MCR_BR_MSK;
	UART_PUT_MCR(port, mcr);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void
uart00_set_termios(struct uart_port *port, struct termios *termios,
		   struct termios *old)
{
	unsigned int uart_mc, old_ies, baud, quot;
	unsigned long flags;

	/*
	 * We don't support CREAD (yet)
	 */
	termios->c_cflag |= CREAD;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	/* byte size and parity */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		uart_mc = UART_MC_CLS_CHARLEN_5;
		break;
	case CS6:
		uart_mc = UART_MC_CLS_CHARLEN_6;
		break;
	case CS7:
		uart_mc = UART_MC_CLS_CHARLEN_7;
		break;
	default: // CS8
		uart_mc = UART_MC_CLS_CHARLEN_8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		uart_mc|= UART_MC_ST_TWO;
	if (termios->c_cflag & PARENB) {
		uart_mc |= UART_MC_PE_MSK;
		if (!(termios->c_cflag & PARODD))
			uart_mc |= UART_MC_EP_MSK;
	}

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = UART_RDS_OE_MSK;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART_RDS_FE_MSK | UART_RDS_PE_MSK;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART_RDS_BI_MSK;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= UART_RDS_FE_MSK | UART_RDS_PE_MSK;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= UART_RDS_BI_MSK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns to (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= UART_RDS_OE_MSK;
	}

	/* first, disable everything */
	old_ies = UART_GET_IES(port); 

	if (UART_ENABLE_MS(port, termios->c_cflag))
		old_ies |= UART_IES_ME_MSK;

	/* Set baud rate */
	UART_PUT_DIV_LO(port, (quot & 0xff));
	UART_PUT_DIV_HI(port, ((quot & 0xf00) >> 8));

	UART_PUT_MC(port, uart_mc);
	UART_PUT_IES(port, old_ies);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int uart00_startup(struct uart_port *port)
{
	int result;

	/*
	 * Allocate the IRQ
	 */
	result = request_irq(port->irq, uart00_int, 0, "uart00", port);
	if (result) {
		printk(KERN_ERR "Request of irq %d failed\n", port->irq);
		return result;
	}

	/*
	 * Finally, enable interrupts. Use the TII interrupt to minimise 
	 * the number of interrupts generated. If higher performance is 
	 * needed, consider using the TI interrupt with a suitable FIFO
	 * threshold
	 */
	UART_PUT_IES(port, UART_IES_RE_MSK | UART_IES_TIE_MSK);

	return 0;
}

static void uart00_shutdown(struct uart_port *port)
{
	/*
	 * disable all interrupts, disable the port
	 */
	UART_PUT_IEC(port, 0xff);

	/* disable break condition and fifos */
	UART_PUT_MCR(port, UART_GET_MCR(port) &~UART_MCR_BR_MSK);

        /*
	 * Free the interrupt
	 */
	free_irq(port->irq, port);
}

static const char *uart00_type(struct uart_port *port)
{
	return port->type == PORT_UART00 ? "Altera UART00" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void uart00_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);

#ifdef CONFIG_ARCH_CAMELOT
	if (port->membase != (void*)IO_ADDRESS(EXC_UART00_BASE)) {
		iounmap(port->membase);
	}
#endif
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int uart00_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE, "serial_uart00")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void uart00_config_port(struct uart_port *port, int flags)
{

	/*
	 * Map the io memory if this is a soft uart
	 */
	if (!port->membase)
		port->membase = ioremap_nocache(port->mapbase,SZ_4K);

	if (!port->membase)
		printk(KERN_ERR "serial00: cannot map io memory\n");
	else
		port->type = PORT_UART00;

}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int uart00_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_UART00)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops uart00_pops = {
	.tx_empty	= uart00_tx_empty,
	.set_mctrl	= uart00_set_mctrl_null,
	.get_mctrl	= uart00_get_mctrl,
	.stop_tx	= uart00_stop_tx,
	.start_tx	= uart00_start_tx,
	.stop_rx	= uart00_stop_rx,
	.enable_ms	= uart00_enable_ms,
	.break_ctl	= uart00_break_ctl,
	.startup	= uart00_startup,
	.shutdown	= uart00_shutdown,
	.set_termios	= uart00_set_termios,
	.type		= uart00_type,
	.release_port	= uart00_release_port,
	.request_port	= uart00_request_port,
	.config_port	= uart00_config_port,
	.verify_port	= uart00_verify_port,
};


#ifdef CONFIG_ARCH_CAMELOT
static struct uart_port epxa10db_port = {
	.membase	= (void*)IO_ADDRESS(EXC_UART00_BASE),
	.mapbase	= EXC_UART00_BASE,
	.iotype		= SERIAL_IO_MEM,
	.irq		= IRQ_UART,
	.uartclk	= EXC_AHB2_CLK_FREQUENCY,
	.fifosize	= 16,
	.ops		= &uart00_pops,
	.flags		= ASYNC_BOOT_AUTOCONF,
};
#endif


#ifdef CONFIG_SERIAL_UART00_CONSOLE
static void uart00_console_write(struct console *co, const char *s, unsigned count)
{
#ifdef CONFIG_ARCH_CAMELOT
	struct uart_port *port = &epxa10db_port;
	unsigned int status, old_ies;
	int i;

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_ies = UART_GET_IES(port);
	UART_PUT_IEC(port,0xff);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_TSR(port);
		} while (!UART_TX_READY(status));
		UART_PUT_CHAR(port, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_TSR(port);
			} while (!UART_TX_READY(status));
			UART_PUT_CHAR(port, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IES
	 */
	do {
		status = UART_GET_TSR(port);
	} while (status & UART_TSR_TX_LEVEL_MSK);
	UART_PUT_IES(port, old_ies);
#endif
}

static void __init
uart00_console_get_options(struct uart_port *port, int *baud,
			   int *parity, int *bits)
{
	unsigned int uart_mc, quot;

	uart_mc = UART_GET_MC(port);

	*parity = 'n';
	if (uart_mc & UART_MC_PE_MSK) {
		if (uart_mc & UART_MC_EP_MSK)
			*parity = 'e';
		else
			*parity = 'o';
	}

	switch (uart_mc & UART_MC_CLS_MSK) {
	case UART_MC_CLS_CHARLEN_5:
		*bits = 5;
		break;
	case UART_MC_CLS_CHARLEN_6:
		*bits = 6;
		break;
	case UART_MC_CLS_CHARLEN_7:
		*bits = 7;
		break;
	case UART_MC_CLS_CHARLEN_8:
		*bits = 8;
		break;
	}
	quot = UART_GET_DIV_LO(port) | (UART_GET_DIV_HI(port) << 8);
	*baud = port->uartclk / (16 *quot );
}

static int __init uart00_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

#ifdef CONFIG_ARCH_CAMELOT
	port = &epxa10db_port;             ;
#else
	return -ENODEV;
#endif
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		uart00_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver uart00_reg;
static struct console uart00_console = {
	.name		= SERIAL_UART00_NAME,
	.write		= uart00_console_write,
	.device		= uart_console_device,
	.setup		= uart00_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= 0,
	.data		= &uart00_reg,
};

static int __init uart00_console_init(void)
{
	register_console(&uart00_console);
	return 0;
}
console_initcall(uart00_console_init);

#define UART00_CONSOLE	&uart00_console
#else
#define UART00_CONSOLE	NULL
#endif

static struct uart_driver uart00_reg = {
	.owner			= NULL,
	.driver_name		= SERIAL_UART00_NAME,
	.dev_name		= SERIAL_UART00_NAME,
	.major			= SERIAL_UART00_MAJOR,
	.minor			= SERIAL_UART00_MINOR,
	.nr			= UART_NR,
	.cons			= UART00_CONSOLE,
};

struct dev_port_entry{
	unsigned int base_addr;
	struct uart_port *port;
};

#ifdef CONFIG_PLD_HOTSWAP

static struct dev_port_entry dev_port_map[UART_NR];

/*
 * Keep a mapping of dev_info addresses -> port lines to use when
 * removing ports dev==NULL indicates unused entry
 */

struct uart00_ps_data{
	unsigned int clk;
	unsigned int fifosize;
};

int uart00_add_device(struct pldhs_dev_info* dev_info, void* dev_ps_data)
{
	struct uart00_ps_data* dev_ps=dev_ps_data;
	struct uart_port * port;
	int i,result;

	i=0;
	while(dev_port_map[i].port)
		i++;

	if(i==UART_NR){
		printk(KERN_WARNING "uart00: Maximum number of ports reached\n");
		return 0;
	}

	port=kmalloc(sizeof(struct uart_port),GFP_KERNEL);
	if(!port)
		return -ENOMEM;

	printk("clk=%d fifo=%d\n",dev_ps->clk,dev_ps->fifosize);
	port->membase=0;
	port->mapbase=dev_info->base_addr;
	port->iotype=SERIAL_IO_MEM;
	port->irq=dev_info->irq;
	port->uartclk=dev_ps->clk;
	port->fifosize=dev_ps->fifosize;
	port->ops=&uart00_pops;
	port->line=i;
	port->flags=ASYNC_BOOT_AUTOCONF;

	result=uart_add_one_port(&uart00_reg, port);
	if(result){
		printk("uart_add_one_port returned %d\n",result);
		return result;
	}
	dev_port_map[i].base_addr=dev_info->base_addr;
	dev_port_map[i].port=port;
	printk("uart00: added device at %x as ttyUA%d\n",dev_port_map[i].base_addr,i);
	return 0;

}

int uart00_remove_devices(void)
{
	int i,result;


	result=0;
	for(i=1;i<UART_NR;i++){
		if(dev_port_map[i].base_addr){
			result=uart_remove_one_port(&uart00_reg, dev_port_map[i].port);
			if(result)
				return result;

			/* port removed sucessfully, so now tidy up */
			kfree(dev_port_map[i].port);
			dev_port_map[i].base_addr=0;
			dev_port_map[i].port=NULL;
		}
	}
	return 0;

}

struct pld_hotswap_ops uart00_pldhs_ops={
	.name		= "uart00",
	.add_device	= uart00_add_device,
	.remove_devices	= uart00_remove_devices,
};

#endif

static int __init uart00_init(void)
{
	int result;

	printk(KERN_INFO "Serial: UART00 driver $Revision: 1.35 $\n");

	printk(KERN_WARNING "serial_uart00:Using temporary major/minor pairs"
		" - these WILL change in the future\n");

	result = uart_register_driver(&uart00_reg);
	if (result)
		return result;
#ifdef CONFIG_ARCH_CAMELOT
	result = uart_add_one_port(&uart00_reg,&epxa10db_port);
#endif
	if (result)
		uart_unregister_driver(&uart00_reg);

#ifdef  CONFIG_PLD_HOTSWAP
	pldhs_register_driver(&uart00_pldhs_ops);
#endif
	return result;
}

__initcall(uart00_init);
