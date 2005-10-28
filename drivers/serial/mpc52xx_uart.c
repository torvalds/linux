/*
 * drivers/serial/mpc52xx_uart.c
 *
 * Driver for the PSC of the Freescale MPC52xx PSCs configured as UARTs.
 *
 * FIXME According to the usermanual the status bits in the status register
 * are only updated when the peripherals access the FIFO and not when the
 * CPU access them. So since we use this bits to know when we stop writing
 * and reading, they may not be updated in-time and a race condition may
 * exists. But I haven't be able to prove this and I don't care. But if
 * any problem arises, it might worth checking. The TX/RX FIFO Stats
 * registers should be used in addition.
 * Update: Actually, they seem updated ... At least the bits we use.
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 * 
 * Some of the code has been inspired/copied from the 2.4 code written
 * by Dale Farnsworth <dfarnsworth@mvista.com>.
 * 
 * Copyright (C) 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 * 
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
 
/* Platform device Usage :
 *
 * Since PSCs can have multiple function, the correct driver for each one
 * is selected by calling mpc52xx_match_psc_function(...). The function
 * handled by this driver is "uart".
 *
 * The driver init all necessary registers to place the PSC in uart mode without
 * DCD. However, the pin multiplexing aren't changed and should be set either
 * by the bootloader or in the platform init code.
 *
 * The idx field must be equal to the PSC index ( e.g. 0 for PSC1, 1 for PSC2,
 * and so on). So the PSC1 is mapped to /dev/ttyS0, PSC2 to /dev/ttyS1 and so
 * on. But be warned, it's an ABSOLUTE REQUIREMENT ! This is needed mainly for
 * the console code : without this 1:1 mapping, at early boot time, when we are
 * parsing the kernel args console=ttyS?, we wouldn't know wich PSC it will be
 * mapped to.
 */

#include <linux/config.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>

#include <asm/delay.h>
#include <asm/io.h>

#include <asm/mpc52xx.h>
#include <asm/mpc52xx_psc.h>

#if defined(CONFIG_SERIAL_MPC52xx_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>



#define ISR_PASS_LIMIT 256	/* Max number of iteration in the interrupt */


static struct uart_port mpc52xx_uart_ports[MPC52xx_PSC_MAXNUM];
	/* Rem: - We use the read_status_mask as a shadow of
	 *        psc->mpc52xx_psc_imr
	 *      - It's important that is array is all zero on start as we
	 *        use it to know if it's initialized or not ! If it's not sure
	 *        it's cleared, then a memset(...,0,...) should be added to
	 *        the console_init
	 */

#define PSC(port) ((struct mpc52xx_psc __iomem *)((port)->membase))


/* Forward declaration of the interruption handling routine */
static irqreturn_t mpc52xx_uart_int(int irq,void *dev_id,struct pt_regs *regs);


/* Simple macro to test if a port is console or not. This one is taken
 * for serial_core.c and maybe should be moved to serial_core.h ? */
#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define uart_console(port)	((port)->cons && (port)->cons->index == (port)->line)
#else
#define uart_console(port)	(0)
#endif


/* ======================================================================== */
/* UART operations                                                          */
/* ======================================================================== */

static unsigned int 
mpc52xx_uart_tx_empty(struct uart_port *port)
{
	int status = in_be16(&PSC(port)->mpc52xx_psc_status);
	return (status & MPC52xx_PSC_SR_TXEMP) ? TIOCSER_TEMT : 0;
}

static void 
mpc52xx_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* Not implemented */
}

static unsigned int 
mpc52xx_uart_get_mctrl(struct uart_port *port)
{
	/* Not implemented */
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void 
mpc52xx_uart_stop_tx(struct uart_port *port)
{
	/* port->lock taken by caller */
	port->read_status_mask &= ~MPC52xx_PSC_IMR_TXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr,port->read_status_mask);
}

static void 
mpc52xx_uart_start_tx(struct uart_port *port)
{
	/* port->lock taken by caller */
	port->read_status_mask |= MPC52xx_PSC_IMR_TXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr,port->read_status_mask);
}

static void 
mpc52xx_uart_send_xchar(struct uart_port *port, char ch)
{
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);
	
	port->x_char = ch;
	if (ch) {
		/* Make sure tx interrupts are on */
		/* Truly necessary ??? They should be anyway */
		port->read_status_mask |= MPC52xx_PSC_IMR_TXRDY;
		out_be16(&PSC(port)->mpc52xx_psc_imr,port->read_status_mask);
	}
	
	spin_unlock_irqrestore(&port->lock, flags);
}

static void
mpc52xx_uart_stop_rx(struct uart_port *port)
{
	/* port->lock taken by caller */
	port->read_status_mask &= ~MPC52xx_PSC_IMR_RXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr,port->read_status_mask);
}

static void
mpc52xx_uart_enable_ms(struct uart_port *port)
{
	/* Not implemented */
}

static void
mpc52xx_uart_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);

	if ( ctl == -1 )
		out_8(&PSC(port)->command,MPC52xx_PSC_START_BRK);
	else
		out_8(&PSC(port)->command,MPC52xx_PSC_STOP_BRK);
	
	spin_unlock_irqrestore(&port->lock, flags);
}

static int
mpc52xx_uart_startup(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	int ret;

	/* Request IRQ */
	ret = request_irq(port->irq, mpc52xx_uart_int,
		SA_INTERRUPT | SA_SAMPLE_RANDOM, "mpc52xx_psc_uart", port);
	if (ret)
		return ret;

	/* Reset/activate the port, clear and enable interrupts */
	out_8(&psc->command,MPC52xx_PSC_RST_RX);
	out_8(&psc->command,MPC52xx_PSC_RST_TX);
	
	out_be32(&psc->sicr,0);	/* UART mode DCD ignored */

	out_be16(&psc->mpc52xx_psc_clock_select, 0xdd00); /* /16 prescaler on */
	
	out_8(&psc->rfcntl, 0x00);
	out_be16(&psc->rfalarm, 0x1ff);
	out_8(&psc->tfcntl, 0x07);
	out_be16(&psc->tfalarm, 0x80);

	port->read_status_mask |= MPC52xx_PSC_IMR_RXRDY | MPC52xx_PSC_IMR_TXRDY;
	out_be16(&psc->mpc52xx_psc_imr,port->read_status_mask);
	
	out_8(&psc->command,MPC52xx_PSC_TX_ENABLE);
	out_8(&psc->command,MPC52xx_PSC_RX_ENABLE);
		
	return 0;
}

static void
mpc52xx_uart_shutdown(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	
	/* Shut down the port, interrupt and all */
	out_8(&psc->command,MPC52xx_PSC_RST_RX);
	out_8(&psc->command,MPC52xx_PSC_RST_TX);
	
	port->read_status_mask = 0; 
	out_be16(&psc->mpc52xx_psc_imr,port->read_status_mask);

	/* Release interrupt */
	free_irq(port->irq, port);
}

static void 
mpc52xx_uart_set_termios(struct uart_port *port, struct termios *new,
                         struct termios *old)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	unsigned long flags;
	unsigned char mr1, mr2;
	unsigned short ctr;
	unsigned int j, baud, quot;
	
	/* Prepare what we're gonna write */
	mr1 = 0;
	
	switch (new->c_cflag & CSIZE) {
		case CS5:	mr1 |= MPC52xx_PSC_MODE_5_BITS;
				break;
		case CS6:	mr1 |= MPC52xx_PSC_MODE_6_BITS;
				break;
		case CS7:	mr1 |= MPC52xx_PSC_MODE_7_BITS;
				break;
		case CS8:
		default:	mr1 |= MPC52xx_PSC_MODE_8_BITS;
	}

	if (new->c_cflag & PARENB) {
		mr1 |= (new->c_cflag & PARODD) ?
			MPC52xx_PSC_MODE_PARODD : MPC52xx_PSC_MODE_PAREVEN;
	} else
		mr1 |= MPC52xx_PSC_MODE_PARNONE;
	
	
	mr2 = 0;

	if (new->c_cflag & CSTOPB)
		mr2 |= MPC52xx_PSC_MODE_TWO_STOP;
	else
		mr2 |= ((new->c_cflag & CSIZE) == CS5) ?
			MPC52xx_PSC_MODE_ONE_STOP_5_BITS :
			MPC52xx_PSC_MODE_ONE_STOP;


	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);
	ctr = quot & 0xffff;
	
	/* Get the lock */
	spin_lock_irqsave(&port->lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(port, new->c_cflag, baud);

	/* Do our best to flush TX & RX, so we don't loose anything */
	/* But we don't wait indefinitly ! */
	j = 5000000;	/* Maximum wait */
	/* FIXME Can't receive chars since set_termios might be called at early
	 * boot for the console, all stuff is not yet ready to receive at that
	 * time and that just makes the kernel oops */
	/* while (j-- && mpc52xx_uart_int_rx_chars(port)); */
	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXEMP) && 
	       --j)
		udelay(1);

	if (!j)
		printk(	KERN_ERR "mpc52xx_uart.c: "
			"Unable to flush RX & TX fifos in-time in set_termios."
			"Some chars may have been lost.\n" ); 

	/* Reset the TX & RX */
	out_8(&psc->command,MPC52xx_PSC_RST_RX);
	out_8(&psc->command,MPC52xx_PSC_RST_TX);

	/* Send new mode settings */
	out_8(&psc->command,MPC52xx_PSC_SEL_MODE_REG_1);
	out_8(&psc->mode,mr1);
	out_8(&psc->mode,mr2);
	out_8(&psc->ctur,ctr >> 8);
	out_8(&psc->ctlr,ctr & 0xff);
	
	/* Reenable TX & RX */
	out_8(&psc->command,MPC52xx_PSC_TX_ENABLE);
	out_8(&psc->command,MPC52xx_PSC_RX_ENABLE);

	/* We're all set, release the lock */
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *
mpc52xx_uart_type(struct uart_port *port)
{
	return port->type == PORT_MPC52xx ? "MPC52xx PSC" : NULL;
}

static void
mpc52xx_uart_release_port(struct uart_port *port)
{
	if (port->flags & UPF_IOREMAP) { /* remapped by us ? */
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, MPC52xx_PSC_SIZE);
}

static int
mpc52xx_uart_request_port(struct uart_port *port)
{
	if (port->flags & UPF_IOREMAP) /* Need to remap ? */
		port->membase = ioremap(port->mapbase, MPC52xx_PSC_SIZE);

	if (!port->membase)
		return -EINVAL;

	return request_mem_region(port->mapbase, MPC52xx_PSC_SIZE,
			"mpc52xx_psc_uart") != NULL ? 0 : -EBUSY;
}

static void
mpc52xx_uart_config_port(struct uart_port *port, int flags)
{
	if ( (flags & UART_CONFIG_TYPE) &&
	     (mpc52xx_uart_request_port(port) == 0) )
	     	port->type = PORT_MPC52xx;
}

static int
mpc52xx_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if ( ser->type != PORT_UNKNOWN && ser->type != PORT_MPC52xx )
		return -EINVAL;

	if ( (ser->irq != port->irq) ||
	     (ser->io_type != SERIAL_IO_MEM) ||
	     (ser->baud_base != port->uartclk)  || 
	     (ser->iomem_base != (void*)port->mapbase) ||
	     (ser->hub6 != 0 ) )
		return -EINVAL;

	return 0;
}


static struct uart_ops mpc52xx_uart_ops = {
	.tx_empty	= mpc52xx_uart_tx_empty,
	.set_mctrl	= mpc52xx_uart_set_mctrl,
	.get_mctrl	= mpc52xx_uart_get_mctrl,
	.stop_tx	= mpc52xx_uart_stop_tx,
	.start_tx	= mpc52xx_uart_start_tx,
	.send_xchar	= mpc52xx_uart_send_xchar,
	.stop_rx	= mpc52xx_uart_stop_rx,
	.enable_ms	= mpc52xx_uart_enable_ms,
	.break_ctl	= mpc52xx_uart_break_ctl,
	.startup	= mpc52xx_uart_startup,
	.shutdown	= mpc52xx_uart_shutdown,
	.set_termios	= mpc52xx_uart_set_termios,
/*	.pm		= mpc52xx_uart_pm,		Not supported yet */
/*	.set_wake	= mpc52xx_uart_set_wake,	Not supported yet */
	.type		= mpc52xx_uart_type,
	.release_port	= mpc52xx_uart_release_port,
	.request_port	= mpc52xx_uart_request_port,
	.config_port	= mpc52xx_uart_config_port,
	.verify_port	= mpc52xx_uart_verify_port
};

	
/* ======================================================================== */
/* Interrupt handling                                                       */
/* ======================================================================== */
	
static inline int
mpc52xx_uart_int_rx_chars(struct uart_port *port, struct pt_regs *regs)
{
	struct tty_struct *tty = port->info->tty;
	unsigned char ch;
	unsigned short status;

	/* While we can read, do so ! */
	while ( (status = in_be16(&PSC(port)->mpc52xx_psc_status)) &
	        MPC52xx_PSC_SR_RXRDY) {

		/* If we are full, just stop reading */
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;
		
		/* Get the char */
		ch = in_8(&PSC(port)->mpc52xx_psc_buffer_8);

		/* Handle sysreq char */
#ifdef SUPPORT_SYSRQ
		if (uart_handle_sysrq_char(port, ch, regs)) {
			port->sysrq = 0;
			continue;
		}
#endif

		/* Store it */
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = 0;
		port->icount.rx++;
	
		if ( status & (MPC52xx_PSC_SR_PE |
		               MPC52xx_PSC_SR_FE |
		               MPC52xx_PSC_SR_RB |
		               MPC52xx_PSC_SR_OE) ) {
			
			if (status & MPC52xx_PSC_SR_RB) {
				*tty->flip.flag_buf_ptr = TTY_BREAK;
				uart_handle_break(port);
			} else if (status & MPC52xx_PSC_SR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (status & MPC52xx_PSC_SR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
			if (status & MPC52xx_PSC_SR_OE) {
				/*
				 * Overrun is special, since it's
				 * reported immediately, and doesn't
				 * affect the current character
				 */
				if (tty->flip.count < (TTY_FLIPBUF_SIZE-1)) {
					tty->flip.flag_buf_ptr++;
					tty->flip.char_buf_ptr++;
					tty->flip.count++;
				}
				*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			}

			/* Clear error condition */
			out_8(&PSC(port)->command,MPC52xx_PSC_RST_ERR_STAT);

		}

		tty->flip.char_buf_ptr++;
		tty->flip.flag_buf_ptr++;
		tty->flip.count++;

	}

	tty_flip_buffer_push(tty);
	
	return in_be16(&PSC(port)->mpc52xx_psc_status) & MPC52xx_PSC_SR_RXRDY;
}

static inline int
mpc52xx_uart_int_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	/* Process out of band chars */
	if (port->x_char) {
		out_8(&PSC(port)->mpc52xx_psc_buffer_8, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return 1;
	}

	/* Nothing to do ? */
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		mpc52xx_uart_stop_tx(port);
		return 0;
	}

	/* Send chars */
	while (in_be16(&PSC(port)->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXRDY) {
		out_8(&PSC(port)->mpc52xx_psc_buffer_8, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	/* Wake up */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	/* Maybe we're done after all */
	if (uart_circ_empty(xmit)) {
		mpc52xx_uart_stop_tx(port);
		return 0;
	}

	return 1;
}

static irqreturn_t 
mpc52xx_uart_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = (struct uart_port *) dev_id;
	unsigned long pass = ISR_PASS_LIMIT;
	unsigned int keepgoing;
	unsigned short status;
	
	if ( irq != port->irq ) {
		printk( KERN_WARNING
		        "mpc52xx_uart_int : " \
		        "Received wrong int %d. Waiting for %d\n",
		       irq, port->irq);
		return IRQ_NONE;
	}
	
	spin_lock(&port->lock);
	
	/* While we have stuff to do, we continue */
	do {
		/* If we don't find anything to do, we stop */
		keepgoing = 0; 
		
		/* Read status */
		status = in_be16(&PSC(port)->mpc52xx_psc_isr);
		status &= port->read_status_mask;
			
		/* Do we need to receive chars ? */
		/* For this RX interrupts must be on and some chars waiting */
		if ( status & MPC52xx_PSC_IMR_RXRDY )
			keepgoing |= mpc52xx_uart_int_rx_chars(port, regs);

		/* Do we need to send chars ? */
		/* For this, TX must be ready and TX interrupt enabled */
		if ( status & MPC52xx_PSC_IMR_TXRDY )
			keepgoing |= mpc52xx_uart_int_tx_chars(port);
		
		/* Limit number of iteration */
		if ( !(--pass) )
			keepgoing = 0;

	} while (keepgoing);
	
	spin_unlock(&port->lock);
	
	return IRQ_HANDLED;
}


/* ======================================================================== */
/* Console ( if applicable )                                                */
/* ======================================================================== */

#ifdef CONFIG_SERIAL_MPC52xx_CONSOLE

static void __init
mpc52xx_console_get_options(struct uart_port *port,
                            int *baud, int *parity, int *bits, int *flow)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	unsigned char mr1;

	/* Read the mode registers */
	out_8(&psc->command,MPC52xx_PSC_SEL_MODE_REG_1);
	mr1 = in_8(&psc->mode);
	
	/* CT{U,L}R are write-only ! */
	*baud = __res.bi_baudrate ?
		__res.bi_baudrate : CONFIG_SERIAL_MPC52xx_CONSOLE_BAUD;

	/* Parse them */
	switch (mr1 & MPC52xx_PSC_MODE_BITS_MASK) {
		case MPC52xx_PSC_MODE_5_BITS:	*bits = 5; break;
		case MPC52xx_PSC_MODE_6_BITS:	*bits = 6; break;
		case MPC52xx_PSC_MODE_7_BITS:	*bits = 7; break;
		case MPC52xx_PSC_MODE_8_BITS:
		default:			*bits = 8;
	}
	
	if (mr1 & MPC52xx_PSC_MODE_PARNONE)
		*parity = 'n';
	else
		*parity = mr1 & MPC52xx_PSC_MODE_PARODD ? 'o' : 'e';
}

static void  
mpc52xx_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = &mpc52xx_uart_ports[co->index];
	struct mpc52xx_psc __iomem *psc = PSC(port);
	unsigned int i, j;
	
	/* Disable interrupts */
	out_be16(&psc->mpc52xx_psc_imr, 0);

	/* Wait the TX buffer to be empty */
	j = 5000000;	/* Maximum wait */	
	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXEMP) && 
	       --j)
		udelay(1);

	/* Write all the chars */
	for ( i=0 ; i<count ; i++ ) {
	
		/* Send the char */
		out_8(&psc->mpc52xx_psc_buffer_8, *s);

		/* Line return handling */
		if ( *s++ == '\n' )
			out_8(&psc->mpc52xx_psc_buffer_8, '\r');
		
		/* Wait the TX buffer to be empty */
		j = 20000;	/* Maximum wait */	
		while (!(in_be16(&psc->mpc52xx_psc_status) & 
		         MPC52xx_PSC_SR_TXEMP) && --j)
			udelay(1);
	}

	/* Restore interrupt state */
	out_be16(&psc->mpc52xx_psc_imr, port->read_status_mask);
}

static int __init
mpc52xx_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &mpc52xx_uart_ports[co->index];

	int baud = CONFIG_SERIAL_MPC52xx_CONSOLE_BAUD;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= MPC52xx_PSC_MAXNUM)
		return -EINVAL;
	
	/* Basic port init. Needed since we use some uart_??? func before
	 * real init for early access */
	spin_lock_init(&port->lock);
	port->uartclk	= __res.bi_ipbfreq / 2; /* Look at CTLR doc */
	port->ops	= &mpc52xx_uart_ops;
	port->mapbase	= MPC52xx_PA(MPC52xx_PSCx_OFFSET(co->index+1));

	/* We ioremap ourself */
	port->membase = ioremap(port->mapbase, MPC52xx_PSC_SIZE);
	if (port->membase == NULL)
		return -EINVAL;

	/* Setup the port parameters accoding to options */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		mpc52xx_console_get_options(port, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}


extern struct uart_driver mpc52xx_uart_driver;

static struct console mpc52xx_console = {
	.name	= "ttyS",
	.write	= mpc52xx_console_write,
	.device	= uart_console_device,
	.setup	= mpc52xx_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,	/* Specified on the cmdline (e.g. console=ttyS0 ) */
	.data	= &mpc52xx_uart_driver,
};

	
static int __init 
mpc52xx_console_init(void)
{
	register_console(&mpc52xx_console);
	return 0;
}

console_initcall(mpc52xx_console_init);

#define MPC52xx_PSC_CONSOLE &mpc52xx_console
#else
#define MPC52xx_PSC_CONSOLE NULL
#endif


/* ======================================================================== */
/* UART Driver                                                              */
/* ======================================================================== */

static struct uart_driver mpc52xx_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "mpc52xx_psc_uart",
	.dev_name	= "ttyS",
	.devfs_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= MPC52xx_PSC_MAXNUM,
	.cons		= MPC52xx_PSC_CONSOLE,
};


/* ======================================================================== */
/* Platform Driver                                                          */
/* ======================================================================== */

static int __devinit
mpc52xx_uart_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res = pdev->resource;

	struct uart_port *port = NULL;
	int i, idx, ret;

	/* Check validity & presence */
	idx = pdev->id;
	if (idx < 0 || idx >= MPC52xx_PSC_MAXNUM)
		return -EINVAL;

	if (!mpc52xx_match_psc_function(idx,"uart"))
		return -ENODEV;

	/* Init the port structure */
	port = &mpc52xx_uart_ports[idx];

	memset(port, 0x00, sizeof(struct uart_port));

	spin_lock_init(&port->lock);
	port->uartclk	= __res.bi_ipbfreq / 2; /* Look at CTLR doc */
	port->fifosize	= 255; /* Should be 512 ! But it can't be */
	                       /* stored in a unsigned char       */
	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF |
			  ( uart_console(port) ? 0 : UPF_IOREMAP );
	port->line	= idx;
	port->ops	= &mpc52xx_uart_ops;

	/* Search for IRQ and mapbase */
	for (i=0 ; i<pdev->num_resources ; i++, res++) {
		if (res->flags & IORESOURCE_MEM)
			port->mapbase = res->start;
		else if (res->flags & IORESOURCE_IRQ)
			port->irq = res->start;
	}
	if (!port->irq || !port->mapbase)
		return -EINVAL;

	/* Add the port to the uart sub-system */
	ret = uart_add_one_port(&mpc52xx_uart_driver, port);
	if (!ret)
		dev_set_drvdata(dev, (void*)port);

	return ret;
}

static int
mpc52xx_uart_remove(struct device *dev)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);

	if (port)
		uart_remove_one_port(&mpc52xx_uart_driver, port);

	return 0;
}

#ifdef CONFIG_PM
static int
mpc52xx_uart_suspend(struct device *dev, pm_message_t state)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(dev);

	if (sport)
		uart_suspend_port(&mpc52xx_uart_driver, port);

	return 0;
}

static int
mpc52xx_uart_resume(struct device *dev)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(dev);

	if (port)
		uart_resume_port(&mpc52xx_uart_driver, port);

	return 0;
}
#endif

static struct device_driver mpc52xx_uart_platform_driver = {
	.name		= "mpc52xx-psc",
	.bus		= &platform_bus_type,
	.probe		= mpc52xx_uart_probe,
	.remove		= mpc52xx_uart_remove,
#ifdef CONFIG_PM
	.suspend	= mpc52xx_uart_suspend,
	.resume		= mpc52xx_uart_resume,
#endif
};


/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_uart_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: MPC52xx PSC driver\n");

	ret = uart_register_driver(&mpc52xx_uart_driver);
	if (ret == 0) {
		ret = driver_register(&mpc52xx_uart_platform_driver);
		if (ret)
			uart_unregister_driver(&mpc52xx_uart_driver);
	}

	return ret;
}

static void __exit
mpc52xx_uart_exit(void)
{
	driver_unregister(&mpc52xx_uart_platform_driver);
	uart_unregister_driver(&mpc52xx_uart_driver);
}


module_init(mpc52xx_uart_init);
module_exit(mpc52xx_uart_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION("Freescale MPC52xx PSC UART");
MODULE_LICENSE("GPL");
