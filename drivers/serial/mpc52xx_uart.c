/*
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
 * Copyright (C) 2008 Freescale Semiconductor Inc.
 *                    John Rigby <jrigby@gmail.com>
 * Added support for MPC5121
 * Copyright (C) 2006 Secret Lab Technologies Ltd.
 *                    Grant Likely <grant.likely@secretlab.ca>
 * Copyright (C) 2004-2006 Sylvain Munaut <tnt@246tNt.com>
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
 * The idx field must be equal to the PSC index (e.g. 0 for PSC1, 1 for PSC2,
 * and so on). So the PSC1 is mapped to /dev/ttyPSC0, PSC2 to /dev/ttyPSC1 and
 * so on. But be warned, it's an ABSOLUTE REQUIREMENT ! This is needed mainly
 * fpr the console code : without this 1:1 mapping, at early boot time, when we
 * are parsing the kernel args console=ttyPSC?, we wouldn't know which PSC it
 * will be mapped to.
 */

/* OF Platform device Usage :
 *
 * This driver is only used for PSCs configured in uart mode.  The device
 * tree will have a node for each PSC in uart mode w/ device_type = "serial"
 * and "mpc52xx-psc-uart" in the compatible string
 *
 * By default, PSC devices are enumerated in the order they are found.  However
 * a particular PSC number can be forces by adding 'device_no = <port#>'
 * to the device node.
 *
 * The driver init all necessary registers to place the PSC in uart mode without
 * DCD. However, the pin multiplexing aren't changed and should be set either
 * by the bootloader or in the platform init code.
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mpc52xx.h>
#include <asm/mpc512x.h>
#include <asm/mpc52xx_psc.h>

#if defined(CONFIG_SERIAL_MPC52xx_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>


/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_PSC_MAJOR	204
#define SERIAL_PSC_MINOR	148


#define ISR_PASS_LIMIT 256	/* Max number of iteration in the interrupt */


static struct uart_port mpc52xx_uart_ports[MPC52xx_PSC_MAXNUM];
	/* Rem: - We use the read_status_mask as a shadow of
	 *        psc->mpc52xx_psc_imr
	 *      - It's important that is array is all zero on start as we
	 *        use it to know if it's initialized or not ! If it's not sure
	 *        it's cleared, then a memset(...,0,...) should be added to
	 *        the console_init
	 */

/* lookup table for matching device nodes to index numbers */
static struct device_node *mpc52xx_uart_nodes[MPC52xx_PSC_MAXNUM];

static void mpc52xx_uart_of_enumerate(void);


#define PSC(port) ((struct mpc52xx_psc __iomem *)((port)->membase))


/* Forward declaration of the interruption handling routine */
static irqreturn_t mpc52xx_uart_int(int irq, void *dev_id);


/* Simple macro to test if a port is console or not. This one is taken
 * for serial_core.c and maybe should be moved to serial_core.h ? */
#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define uart_console(port) \
	((port)->cons && (port)->cons->index == (port)->line)
#else
#define uart_console(port)	(0)
#endif

/* ======================================================================== */
/* PSC fifo operations for isolating differences between 52xx and 512x      */
/* ======================================================================== */

struct psc_ops {
	void		(*fifo_init)(struct uart_port *port);
	int		(*raw_rx_rdy)(struct uart_port *port);
	int		(*raw_tx_rdy)(struct uart_port *port);
	int		(*rx_rdy)(struct uart_port *port);
	int		(*tx_rdy)(struct uart_port *port);
	int		(*tx_empty)(struct uart_port *port);
	void		(*stop_rx)(struct uart_port *port);
	void		(*start_tx)(struct uart_port *port);
	void		(*stop_tx)(struct uart_port *port);
	void		(*rx_clr_irq)(struct uart_port *port);
	void		(*tx_clr_irq)(struct uart_port *port);
	void		(*write_char)(struct uart_port *port, unsigned char c);
	unsigned char	(*read_char)(struct uart_port *port);
	void		(*cw_disable_ints)(struct uart_port *port);
	void		(*cw_restore_ints)(struct uart_port *port);
	unsigned long	(*getuartclk)(void *p);
};

#ifdef CONFIG_PPC_MPC52xx
#define FIFO_52xx(port) ((struct mpc52xx_psc_fifo __iomem *)(PSC(port)+1))
static void mpc52xx_psc_fifo_init(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	struct mpc52xx_psc_fifo __iomem *fifo = FIFO_52xx(port);

	/* /32 prescaler */
	out_be16(&psc->mpc52xx_psc_clock_select, 0xdd00);

	out_8(&fifo->rfcntl, 0x00);
	out_be16(&fifo->rfalarm, 0x1ff);
	out_8(&fifo->tfcntl, 0x07);
	out_be16(&fifo->tfalarm, 0x80);

	port->read_status_mask |= MPC52xx_PSC_IMR_RXRDY | MPC52xx_PSC_IMR_TXRDY;
	out_be16(&psc->mpc52xx_psc_imr, port->read_status_mask);
}

static int mpc52xx_psc_raw_rx_rdy(struct uart_port *port)
{
	return in_be16(&PSC(port)->mpc52xx_psc_status)
	    & MPC52xx_PSC_SR_RXRDY;
}

static int mpc52xx_psc_raw_tx_rdy(struct uart_port *port)
{
	return in_be16(&PSC(port)->mpc52xx_psc_status)
	    & MPC52xx_PSC_SR_TXRDY;
}


static int mpc52xx_psc_rx_rdy(struct uart_port *port)
{
	return in_be16(&PSC(port)->mpc52xx_psc_isr)
	    & port->read_status_mask
	    & MPC52xx_PSC_IMR_RXRDY;
}

static int mpc52xx_psc_tx_rdy(struct uart_port *port)
{
	return in_be16(&PSC(port)->mpc52xx_psc_isr)
	    & port->read_status_mask
	    & MPC52xx_PSC_IMR_TXRDY;
}

static int mpc52xx_psc_tx_empty(struct uart_port *port)
{
	return in_be16(&PSC(port)->mpc52xx_psc_status)
	    & MPC52xx_PSC_SR_TXEMP;
}

static void mpc52xx_psc_start_tx(struct uart_port *port)
{
	port->read_status_mask |= MPC52xx_PSC_IMR_TXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr, port->read_status_mask);
}

static void mpc52xx_psc_stop_tx(struct uart_port *port)
{
	port->read_status_mask &= ~MPC52xx_PSC_IMR_TXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr, port->read_status_mask);
}

static void mpc52xx_psc_stop_rx(struct uart_port *port)
{
	port->read_status_mask &= ~MPC52xx_PSC_IMR_RXRDY;
	out_be16(&PSC(port)->mpc52xx_psc_imr, port->read_status_mask);
}

static void mpc52xx_psc_rx_clr_irq(struct uart_port *port)
{
}

static void mpc52xx_psc_tx_clr_irq(struct uart_port *port)
{
}

static void mpc52xx_psc_write_char(struct uart_port *port, unsigned char c)
{
	out_8(&PSC(port)->mpc52xx_psc_buffer_8, c);
}

static unsigned char mpc52xx_psc_read_char(struct uart_port *port)
{
	return in_8(&PSC(port)->mpc52xx_psc_buffer_8);
}

static void mpc52xx_psc_cw_disable_ints(struct uart_port *port)
{
	out_be16(&PSC(port)->mpc52xx_psc_imr, 0);
}

static void mpc52xx_psc_cw_restore_ints(struct uart_port *port)
{
	out_be16(&PSC(port)->mpc52xx_psc_imr, port->read_status_mask);
}

/* Search for bus-frequency property in this node or a parent */
static unsigned long mpc52xx_getuartclk(void *p)
{
	/*
	 * 5200 UARTs have a / 32 prescaler
	 * but the generic serial code assumes 16
	 * so return ipb freq / 2
	 */
	return mpc52xx_find_ipb_freq(p) / 2;
}

static struct psc_ops mpc52xx_psc_ops = {
	.fifo_init = mpc52xx_psc_fifo_init,
	.raw_rx_rdy = mpc52xx_psc_raw_rx_rdy,
	.raw_tx_rdy = mpc52xx_psc_raw_tx_rdy,
	.rx_rdy = mpc52xx_psc_rx_rdy,
	.tx_rdy = mpc52xx_psc_tx_rdy,
	.tx_empty = mpc52xx_psc_tx_empty,
	.stop_rx = mpc52xx_psc_stop_rx,
	.start_tx = mpc52xx_psc_start_tx,
	.stop_tx = mpc52xx_psc_stop_tx,
	.rx_clr_irq = mpc52xx_psc_rx_clr_irq,
	.tx_clr_irq = mpc52xx_psc_tx_clr_irq,
	.write_char = mpc52xx_psc_write_char,
	.read_char = mpc52xx_psc_read_char,
	.cw_disable_ints = mpc52xx_psc_cw_disable_ints,
	.cw_restore_ints = mpc52xx_psc_cw_restore_ints,
	.getuartclk = mpc52xx_getuartclk,
};

#endif /* CONFIG_MPC52xx */

#ifdef CONFIG_PPC_MPC512x
#define FIFO_512x(port) ((struct mpc512x_psc_fifo __iomem *)(PSC(port)+1))
static void mpc512x_psc_fifo_init(struct uart_port *port)
{
	out_be32(&FIFO_512x(port)->txcmd, MPC512x_PSC_FIFO_RESET_SLICE);
	out_be32(&FIFO_512x(port)->txcmd, MPC512x_PSC_FIFO_ENABLE_SLICE);
	out_be32(&FIFO_512x(port)->txalarm, 1);
	out_be32(&FIFO_512x(port)->tximr, 0);

	out_be32(&FIFO_512x(port)->rxcmd, MPC512x_PSC_FIFO_RESET_SLICE);
	out_be32(&FIFO_512x(port)->rxcmd, MPC512x_PSC_FIFO_ENABLE_SLICE);
	out_be32(&FIFO_512x(port)->rxalarm, 1);
	out_be32(&FIFO_512x(port)->rximr, 0);

	out_be32(&FIFO_512x(port)->tximr, MPC512x_PSC_FIFO_ALARM);
	out_be32(&FIFO_512x(port)->rximr, MPC512x_PSC_FIFO_ALARM);
}

static int mpc512x_psc_raw_rx_rdy(struct uart_port *port)
{
	return !(in_be32(&FIFO_512x(port)->rxsr) & MPC512x_PSC_FIFO_EMPTY);
}

static int mpc512x_psc_raw_tx_rdy(struct uart_port *port)
{
	return !(in_be32(&FIFO_512x(port)->txsr) & MPC512x_PSC_FIFO_FULL);
}

static int mpc512x_psc_rx_rdy(struct uart_port *port)
{
	return in_be32(&FIFO_512x(port)->rxsr)
	    & in_be32(&FIFO_512x(port)->rximr)
	    & MPC512x_PSC_FIFO_ALARM;
}

static int mpc512x_psc_tx_rdy(struct uart_port *port)
{
	return in_be32(&FIFO_512x(port)->txsr)
	    & in_be32(&FIFO_512x(port)->tximr)
	    & MPC512x_PSC_FIFO_ALARM;
}

static int mpc512x_psc_tx_empty(struct uart_port *port)
{
	return in_be32(&FIFO_512x(port)->txsr)
	    & MPC512x_PSC_FIFO_EMPTY;
}

static void mpc512x_psc_stop_rx(struct uart_port *port)
{
	unsigned long rx_fifo_imr;

	rx_fifo_imr = in_be32(&FIFO_512x(port)->rximr);
	rx_fifo_imr &= ~MPC512x_PSC_FIFO_ALARM;
	out_be32(&FIFO_512x(port)->rximr, rx_fifo_imr);
}

static void mpc512x_psc_start_tx(struct uart_port *port)
{
	unsigned long tx_fifo_imr;

	tx_fifo_imr = in_be32(&FIFO_512x(port)->tximr);
	tx_fifo_imr |= MPC512x_PSC_FIFO_ALARM;
	out_be32(&FIFO_512x(port)->tximr, tx_fifo_imr);
}

static void mpc512x_psc_stop_tx(struct uart_port *port)
{
	unsigned long tx_fifo_imr;

	tx_fifo_imr = in_be32(&FIFO_512x(port)->tximr);
	tx_fifo_imr &= ~MPC512x_PSC_FIFO_ALARM;
	out_be32(&FIFO_512x(port)->tximr, tx_fifo_imr);
}

static void mpc512x_psc_rx_clr_irq(struct uart_port *port)
{
	out_be32(&FIFO_512x(port)->rxisr, in_be32(&FIFO_512x(port)->rxisr));
}

static void mpc512x_psc_tx_clr_irq(struct uart_port *port)
{
	out_be32(&FIFO_512x(port)->txisr, in_be32(&FIFO_512x(port)->txisr));
}

static void mpc512x_psc_write_char(struct uart_port *port, unsigned char c)
{
	out_8(&FIFO_512x(port)->txdata_8, c);
}

static unsigned char mpc512x_psc_read_char(struct uart_port *port)
{
	return in_8(&FIFO_512x(port)->rxdata_8);
}

static void mpc512x_psc_cw_disable_ints(struct uart_port *port)
{
	port->read_status_mask =
		in_be32(&FIFO_512x(port)->tximr) << 16 |
		in_be32(&FIFO_512x(port)->rximr);
	out_be32(&FIFO_512x(port)->tximr, 0);
	out_be32(&FIFO_512x(port)->rximr, 0);
}

static void mpc512x_psc_cw_restore_ints(struct uart_port *port)
{
	out_be32(&FIFO_512x(port)->tximr,
		(port->read_status_mask >> 16) & 0x7f);
	out_be32(&FIFO_512x(port)->rximr, port->read_status_mask & 0x7f);
}

static unsigned long mpc512x_getuartclk(void *p)
{
	return mpc512x_find_ips_freq(p);
}

static struct psc_ops mpc512x_psc_ops = {
	.fifo_init = mpc512x_psc_fifo_init,
	.raw_rx_rdy = mpc512x_psc_raw_rx_rdy,
	.raw_tx_rdy = mpc512x_psc_raw_tx_rdy,
	.rx_rdy = mpc512x_psc_rx_rdy,
	.tx_rdy = mpc512x_psc_tx_rdy,
	.tx_empty = mpc512x_psc_tx_empty,
	.stop_rx = mpc512x_psc_stop_rx,
	.start_tx = mpc512x_psc_start_tx,
	.stop_tx = mpc512x_psc_stop_tx,
	.rx_clr_irq = mpc512x_psc_rx_clr_irq,
	.tx_clr_irq = mpc512x_psc_tx_clr_irq,
	.write_char = mpc512x_psc_write_char,
	.read_char = mpc512x_psc_read_char,
	.cw_disable_ints = mpc512x_psc_cw_disable_ints,
	.cw_restore_ints = mpc512x_psc_cw_restore_ints,
	.getuartclk = mpc512x_getuartclk,
};
#endif

static struct psc_ops *psc_ops;

/* ======================================================================== */
/* UART operations                                                          */
/* ======================================================================== */

static unsigned int
mpc52xx_uart_tx_empty(struct uart_port *port)
{
	return psc_ops->tx_empty(port) ? TIOCSER_TEMT : 0;
}

static void
mpc52xx_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	if (mctrl & TIOCM_RTS)
		out_8(&PSC(port)->op1, MPC52xx_PSC_OP_RTS);
	else
		out_8(&PSC(port)->op0, MPC52xx_PSC_OP_RTS);
}

static unsigned int
mpc52xx_uart_get_mctrl(struct uart_port *port)
{
	unsigned int ret = TIOCM_DSR;
	u8 status = in_8(&PSC(port)->mpc52xx_psc_ipcr);

	if (!(status & MPC52xx_PSC_CTS))
		ret |= TIOCM_CTS;
	if (!(status & MPC52xx_PSC_DCD))
		ret |= TIOCM_CAR;

	return ret;
}

static void
mpc52xx_uart_stop_tx(struct uart_port *port)
{
	/* port->lock taken by caller */
	psc_ops->stop_tx(port);
}

static void
mpc52xx_uart_start_tx(struct uart_port *port)
{
	/* port->lock taken by caller */
	psc_ops->start_tx(port);
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
		psc_ops->start_tx(port);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static void
mpc52xx_uart_stop_rx(struct uart_port *port)
{
	/* port->lock taken by caller */
	psc_ops->stop_rx(port);
}

static void
mpc52xx_uart_enable_ms(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);

	/* clear D_*-bits by reading them */
	in_8(&psc->mpc52xx_psc_ipcr);
	/* enable CTS and DCD as IPC interrupts */
	out_8(&psc->mpc52xx_psc_acr, MPC52xx_PSC_IEC_CTS | MPC52xx_PSC_IEC_DCD);

	port->read_status_mask |= MPC52xx_PSC_IMR_IPC;
	out_be16(&psc->mpc52xx_psc_imr, port->read_status_mask);
}

static void
mpc52xx_uart_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);

	if (ctl == -1)
		out_8(&PSC(port)->command, MPC52xx_PSC_START_BRK);
	else
		out_8(&PSC(port)->command, MPC52xx_PSC_STOP_BRK);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int
mpc52xx_uart_startup(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	int ret;

	/* Request IRQ */
	ret = request_irq(port->irq, mpc52xx_uart_int,
		IRQF_DISABLED | IRQF_SAMPLE_RANDOM | IRQF_SHARED,
		"mpc52xx_psc_uart", port);
	if (ret)
		return ret;

	/* Reset/activate the port, clear and enable interrupts */
	out_8(&psc->command, MPC52xx_PSC_RST_RX);
	out_8(&psc->command, MPC52xx_PSC_RST_TX);

	out_be32(&psc->sicr, 0);	/* UART mode DCD ignored */

	psc_ops->fifo_init(port);

	out_8(&psc->command, MPC52xx_PSC_TX_ENABLE);
	out_8(&psc->command, MPC52xx_PSC_RX_ENABLE);

	return 0;
}

static void
mpc52xx_uart_shutdown(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);

	/* Shut down the port.  Leave TX active if on a console port */
	out_8(&psc->command, MPC52xx_PSC_RST_RX);
	if (!uart_console(port))
		out_8(&psc->command, MPC52xx_PSC_RST_TX);

	port->read_status_mask = 0;
	out_be16(&psc->mpc52xx_psc_imr, port->read_status_mask);

	/* Release interrupt */
	free_irq(port->irq, port);
}

static void
mpc52xx_uart_set_termios(struct uart_port *port, struct ktermios *new,
			 struct ktermios *old)
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

	if (new->c_cflag & CRTSCTS) {
		mr1 |= MPC52xx_PSC_MODE_RXRTS;
		mr2 |= MPC52xx_PSC_MODE_TXCTS;
	}

	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);
	ctr = quot & 0xffff;

	/* Get the lock */
	spin_lock_irqsave(&port->lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(port, new->c_cflag, baud);

	/* Do our best to flush TX & RX, so we don't lose anything */
	/* But we don't wait indefinitely ! */
	j = 5000000;	/* Maximum wait */
	/* FIXME Can't receive chars since set_termios might be called at early
	 * boot for the console, all stuff is not yet ready to receive at that
	 * time and that just makes the kernel oops */
	/* while (j-- && mpc52xx_uart_int_rx_chars(port)); */
	while (!mpc52xx_uart_tx_empty(port) && --j)
		udelay(1);

	if (!j)
		printk(KERN_ERR "mpc52xx_uart.c: "
			"Unable to flush RX & TX fifos in-time in set_termios."
			"Some chars may have been lost.\n");

	/* Reset the TX & RX */
	out_8(&psc->command, MPC52xx_PSC_RST_RX);
	out_8(&psc->command, MPC52xx_PSC_RST_TX);

	/* Send new mode settings */
	out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1);
	out_8(&psc->mode, mr1);
	out_8(&psc->mode, mr2);
	out_8(&psc->ctur, ctr >> 8);
	out_8(&psc->ctlr, ctr & 0xff);

	if (UART_ENABLE_MS(port, new->c_cflag))
		mpc52xx_uart_enable_ms(port);

	/* Reenable TX & RX */
	out_8(&psc->command, MPC52xx_PSC_TX_ENABLE);
	out_8(&psc->command, MPC52xx_PSC_RX_ENABLE);

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
	/* remapped by us ? */
	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, sizeof(struct mpc52xx_psc));
}

static int
mpc52xx_uart_request_port(struct uart_port *port)
{
	int err;

	if (port->flags & UPF_IOREMAP) /* Need to remap ? */
		port->membase = ioremap(port->mapbase,
					sizeof(struct mpc52xx_psc));

	if (!port->membase)
		return -EINVAL;

	err = request_mem_region(port->mapbase, sizeof(struct mpc52xx_psc),
			"mpc52xx_psc_uart") != NULL ? 0 : -EBUSY;

	if (err && (port->flags & UPF_IOREMAP)) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	return err;
}

static void
mpc52xx_uart_config_port(struct uart_port *port, int flags)
{
	if ((flags & UART_CONFIG_TYPE)
		&& (mpc52xx_uart_request_port(port) == 0))
		port->type = PORT_MPC52xx;
}

static int
mpc52xx_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_MPC52xx)
		return -EINVAL;

	if ((ser->irq != port->irq) ||
	    (ser->io_type != SERIAL_IO_MEM) ||
	    (ser->baud_base != port->uartclk)  ||
	    (ser->iomem_base != (void *)port->mapbase) ||
	    (ser->hub6 != 0))
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
mpc52xx_uart_int_rx_chars(struct uart_port *port)
{
	struct tty_struct *tty = port->info->port.tty;
	unsigned char ch, flag;
	unsigned short status;

	/* While we can read, do so ! */
	while (psc_ops->raw_rx_rdy(port)) {
		/* Get the char */
		ch = psc_ops->read_char(port);

		/* Handle sysreq char */
#ifdef SUPPORT_SYSRQ
		if (uart_handle_sysrq_char(port, ch)) {
			port->sysrq = 0;
			continue;
		}
#endif

		/* Store it */

		flag = TTY_NORMAL;
		port->icount.rx++;

		status = in_be16(&PSC(port)->mpc52xx_psc_status);

		if (status & (MPC52xx_PSC_SR_PE |
			      MPC52xx_PSC_SR_FE |
			      MPC52xx_PSC_SR_RB)) {

			if (status & MPC52xx_PSC_SR_RB) {
				flag = TTY_BREAK;
				uart_handle_break(port);
				port->icount.brk++;
			} else if (status & MPC52xx_PSC_SR_PE) {
				flag = TTY_PARITY;
				port->icount.parity++;
			}
			else if (status & MPC52xx_PSC_SR_FE) {
				flag = TTY_FRAME;
				port->icount.frame++;
			}

			/* Clear error condition */
			out_8(&PSC(port)->command, MPC52xx_PSC_RST_ERR_STAT);

		}
		tty_insert_flip_char(tty, ch, flag);
		if (status & MPC52xx_PSC_SR_OE) {
			/*
			 * Overrun is special, since it's
			 * reported immediately, and doesn't
			 * affect the current character
			 */
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);
			port->icount.overrun++;
		}
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tty);
	spin_lock(&port->lock);

	return psc_ops->raw_rx_rdy(port);
}

static inline int
mpc52xx_uart_int_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	/* Process out of band chars */
	if (port->x_char) {
		psc_ops->write_char(port, port->x_char);
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
	while (psc_ops->raw_tx_rdy(port)) {
		psc_ops->write_char(port, xmit->buf[xmit->tail]);
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
mpc52xx_uart_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned long pass = ISR_PASS_LIMIT;
	unsigned int keepgoing;
	u8 status;

	spin_lock(&port->lock);

	/* While we have stuff to do, we continue */
	do {
		/* If we don't find anything to do, we stop */
		keepgoing = 0;

		psc_ops->rx_clr_irq(port);
		if (psc_ops->rx_rdy(port))
			keepgoing |= mpc52xx_uart_int_rx_chars(port);

		psc_ops->tx_clr_irq(port);
		if (psc_ops->tx_rdy(port))
			keepgoing |= mpc52xx_uart_int_tx_chars(port);

		status = in_8(&PSC(port)->mpc52xx_psc_ipcr);
		if (status & MPC52xx_PSC_D_DCD)
			uart_handle_dcd_change(port, !(status & MPC52xx_PSC_DCD));

		if (status & MPC52xx_PSC_D_CTS)
			uart_handle_cts_change(port, !(status & MPC52xx_PSC_CTS));

		/* Limit number of iteration */
		if (!(--pass))
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

	pr_debug("mpc52xx_console_get_options(port=%p)\n", port);

	/* Read the mode registers */
	out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1);
	mr1 = in_8(&psc->mode);

	/* CT{U,L}R are write-only ! */
	*baud = CONFIG_SERIAL_MPC52xx_CONSOLE_BAUD;

	/* Parse them */
	switch (mr1 & MPC52xx_PSC_MODE_BITS_MASK) {
	case MPC52xx_PSC_MODE_5_BITS:
		*bits = 5;
		break;
	case MPC52xx_PSC_MODE_6_BITS:
		*bits = 6;
		break;
	case MPC52xx_PSC_MODE_7_BITS:
		*bits = 7;
		break;
	case MPC52xx_PSC_MODE_8_BITS:
	default:
		*bits = 8;
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
	unsigned int i, j;

	/* Disable interrupts */
	psc_ops->cw_disable_ints(port);

	/* Wait the TX buffer to be empty */
	j = 5000000;	/* Maximum wait */
	while (!mpc52xx_uart_tx_empty(port) && --j)
		udelay(1);

	/* Write all the chars */
	for (i = 0; i < count; i++, s++) {
		/* Line return handling */
		if (*s == '\n')
			psc_ops->write_char(port, '\r');

		/* Send the char */
		psc_ops->write_char(port, *s);

		/* Wait the TX buffer to be empty */
		j = 20000;	/* Maximum wait */
		while (!mpc52xx_uart_tx_empty(port) && --j)
			udelay(1);
	}

	/* Restore interrupt state */
	psc_ops->cw_restore_ints(port);
}


static int __init
mpc52xx_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &mpc52xx_uart_ports[co->index];
	struct device_node *np = mpc52xx_uart_nodes[co->index];
	unsigned int uartclk;
	struct resource res;
	int ret;

	int baud = CONFIG_SERIAL_MPC52xx_CONSOLE_BAUD;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	pr_debug("mpc52xx_console_setup co=%p, co->index=%i, options=%s\n",
		 co, co->index, options);

	if ((co->index < 0) || (co->index > MPC52xx_PSC_MAXNUM)) {
		pr_debug("PSC%x out of range\n", co->index);
		return -EINVAL;
	}

	if (!np) {
		pr_debug("PSC%x not found in device tree\n", co->index);
		return -EINVAL;
	}

	pr_debug("Console on ttyPSC%x is %s\n",
		 co->index, mpc52xx_uart_nodes[co->index]->full_name);

	/* Fetch register locations */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_debug("Could not get resources for PSC%x\n", co->index);
		return ret;
	}

	uartclk = psc_ops->getuartclk(np);
	if (uartclk == 0) {
		pr_debug("Could not find uart clock frequency!\n");
		return -EINVAL;
	}

	/* Basic port init. Needed since we use some uart_??? func before
	 * real init for early access */
	spin_lock_init(&port->lock);
	port->uartclk = uartclk;
	port->ops	= &mpc52xx_uart_ops;
	port->mapbase = res.start;
	port->membase = ioremap(res.start, sizeof(struct mpc52xx_psc));
	port->irq = irq_of_parse_and_map(np, 0);

	if (port->membase == NULL)
		return -EINVAL;

	pr_debug("mpc52xx-psc uart at %p, mapped to %p, irq=%x, freq=%i\n",
		 (void *)port->mapbase, port->membase,
		 port->irq, port->uartclk);

	/* Setup the port parameters accoding to options */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		mpc52xx_console_get_options(port, &baud, &parity, &bits, &flow);

	pr_debug("Setting console parameters: %i %i%c1 flow=%c\n",
		 baud, bits, parity, flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}


static struct uart_driver mpc52xx_uart_driver;

static struct console mpc52xx_console = {
	.name	= "ttyPSC",
	.write	= mpc52xx_console_write,
	.device	= uart_console_device,
	.setup	= mpc52xx_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,	/* Specified on the cmdline (e.g. console=ttyPSC0) */
	.data	= &mpc52xx_uart_driver,
};


static int __init
mpc52xx_console_init(void)
{
	mpc52xx_uart_of_enumerate();
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
	.driver_name	= "mpc52xx_psc_uart",
	.dev_name	= "ttyPSC",
	.major		= SERIAL_PSC_MAJOR,
	.minor		= SERIAL_PSC_MINOR,
	.nr		= MPC52xx_PSC_MAXNUM,
	.cons		= MPC52xx_PSC_CONSOLE,
};

/* ======================================================================== */
/* OF Platform Driver                                                       */
/* ======================================================================== */

static struct of_device_id mpc52xx_uart_of_match[] = {
#ifdef CONFIG_PPC_MPC52xx
	{ .compatible = "fsl,mpc5200-psc-uart", .data = &mpc52xx_psc_ops, },
	/* binding used by old lite5200 device trees: */
	{ .compatible = "mpc5200-psc-uart", .data = &mpc52xx_psc_ops, },
	/* binding used by efika: */
	{ .compatible = "mpc5200-serial", .data = &mpc52xx_psc_ops, },
#endif
#ifdef CONFIG_PPC_MPC512x
	{ .compatible = "fsl,mpc5121-psc-uart", .data = &mpc512x_psc_ops, },
#endif
	{},
};

static int __devinit
mpc52xx_uart_of_probe(struct of_device *op, const struct of_device_id *match)
{
	int idx = -1;
	unsigned int uartclk;
	struct uart_port *port = NULL;
	struct resource res;
	int ret;

	dev_dbg(&op->dev, "mpc52xx_uart_probe(op=%p, match=%p)\n", op, match);

	/* Check validity & presence */
	for (idx = 0; idx < MPC52xx_PSC_MAXNUM; idx++)
		if (mpc52xx_uart_nodes[idx] == op->node)
			break;
	if (idx >= MPC52xx_PSC_MAXNUM)
		return -EINVAL;
	pr_debug("Found %s assigned to ttyPSC%x\n",
		 mpc52xx_uart_nodes[idx]->full_name, idx);

	uartclk = psc_ops->getuartclk(op->node);
	if (uartclk == 0) {
		dev_dbg(&op->dev, "Could not find uart clock frequency!\n");
		return -EINVAL;
	}

	/* Init the port structure */
	port = &mpc52xx_uart_ports[idx];

	spin_lock_init(&port->lock);
	port->uartclk = uartclk;
	port->fifosize	= 512;
	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF |
			  (uart_console(port) ? 0 : UPF_IOREMAP);
	port->line	= idx;
	port->ops	= &mpc52xx_uart_ops;
	port->dev	= &op->dev;

	/* Search for IRQ and mapbase */
	ret = of_address_to_resource(op->node, 0, &res);
	if (ret)
		return ret;

	port->mapbase = res.start;
	if (!port->mapbase) {
		dev_dbg(&op->dev, "Could not allocate resources for PSC\n");
		return -EINVAL;
	}

	port->irq = irq_of_parse_and_map(op->node, 0);
	if (port->irq == NO_IRQ) {
		dev_dbg(&op->dev, "Could not get irq\n");
		return -EINVAL;
	}

	dev_dbg(&op->dev, "mpc52xx-psc uart at %p, irq=%x, freq=%i\n",
		(void *)port->mapbase, port->irq, port->uartclk);

	/* Add the port to the uart sub-system */
	ret = uart_add_one_port(&mpc52xx_uart_driver, port);
	if (ret) {
		irq_dispose_mapping(port->irq);
		return ret;
	}

	dev_set_drvdata(&op->dev, (void *)port);
	return 0;
}

static int
mpc52xx_uart_of_remove(struct of_device *op)
{
	struct uart_port *port = dev_get_drvdata(&op->dev);
	dev_set_drvdata(&op->dev, NULL);

	if (port) {
		uart_remove_one_port(&mpc52xx_uart_driver, port);
		irq_dispose_mapping(port->irq);
	}

	return 0;
}

#ifdef CONFIG_PM
static int
mpc52xx_uart_of_suspend(struct of_device *op, pm_message_t state)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(&op->dev);

	if (port)
		uart_suspend_port(&mpc52xx_uart_driver, port);

	return 0;
}

static int
mpc52xx_uart_of_resume(struct of_device *op)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(&op->dev);

	if (port)
		uart_resume_port(&mpc52xx_uart_driver, port);

	return 0;
}
#endif

static void
mpc52xx_uart_of_assign(struct device_node *np, int idx)
{
	int free_idx = -1;
	int i;

	/* Find the first free node */
	for (i = 0; i < MPC52xx_PSC_MAXNUM; i++) {
		if (mpc52xx_uart_nodes[i] == NULL) {
			free_idx = i;
			break;
		}
	}

	if ((idx < 0) || (idx >= MPC52xx_PSC_MAXNUM))
		idx = free_idx;

	if (idx < 0)
		return; /* No free slot; abort */

	of_node_get(np);
	/* If the slot is already occupied, then swap slots */
	if (mpc52xx_uart_nodes[idx] && (free_idx != -1))
		mpc52xx_uart_nodes[free_idx] = mpc52xx_uart_nodes[idx];
	mpc52xx_uart_nodes[idx] = np;
}

static void
mpc52xx_uart_of_enumerate(void)
{
	static int enum_done;
	struct device_node *np;
	const unsigned int *devno;
	const struct  of_device_id *match;
	int i;

	if (enum_done)
		return;

	for_each_node_by_type(np, "serial") {
		match = of_match_node(mpc52xx_uart_of_match, np);
		if (!match)
			continue;

		psc_ops = match->data;

		/* Is a particular device number requested? */
		devno = of_get_property(np, "port-number", NULL);
		mpc52xx_uart_of_assign(np, devno ? *devno : -1);
	}

	enum_done = 1;

	for (i = 0; i < MPC52xx_PSC_MAXNUM; i++) {
		if (mpc52xx_uart_nodes[i])
			pr_debug("%s assigned to ttyPSC%x\n",
				 mpc52xx_uart_nodes[i]->full_name, i);
	}
}

MODULE_DEVICE_TABLE(of, mpc52xx_uart_of_match);

static struct of_platform_driver mpc52xx_uart_of_driver = {
	.match_table	= mpc52xx_uart_of_match,
	.probe		= mpc52xx_uart_of_probe,
	.remove		= mpc52xx_uart_of_remove,
#ifdef CONFIG_PM
	.suspend	= mpc52xx_uart_of_suspend,
	.resume		= mpc52xx_uart_of_resume,
#endif
	.driver		= {
		.name	= "mpc52xx-psc-uart",
	},
};


/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_uart_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: MPC52xx PSC UART driver\n");

	ret = uart_register_driver(&mpc52xx_uart_driver);
	if (ret) {
		printk(KERN_ERR "%s: uart_register_driver failed (%i)\n",
		       __FILE__, ret);
		return ret;
	}

	mpc52xx_uart_of_enumerate();

	ret = of_register_platform_driver(&mpc52xx_uart_of_driver);
	if (ret) {
		printk(KERN_ERR "%s: of_register_platform_driver failed (%i)\n",
		       __FILE__, ret);
		uart_unregister_driver(&mpc52xx_uart_driver);
		return ret;
	}

	return 0;
}

static void __exit
mpc52xx_uart_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_uart_of_driver);
	uart_unregister_driver(&mpc52xx_uart_driver);
}


module_init(mpc52xx_uart_init);
module_exit(mpc52xx_uart_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION("Freescale MPC52xx PSC UART");
MODULE_LICENSE("GPL");
