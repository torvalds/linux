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
#include <linux/clk.h>

#include <asm/mpc52xx.h>
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
static irqreturn_t mpc5xxx_uart_process_int(struct uart_port *port);


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
	unsigned int	(*set_baudrate)(struct uart_port *port,
					struct ktermios *new,
					struct ktermios *old);
	int		(*clock)(struct uart_port *port, int enable);
	int		(*fifoc_init)(void);
	void		(*fifoc_uninit)(void);
	void		(*get_irq)(struct uart_port *, struct device_node *);
	irqreturn_t	(*handle_irq)(struct uart_port *port);
};

/* setting the prescaler and divisor reg is common for all chips */
static inline void mpc52xx_set_divisor(struct mpc52xx_psc __iomem *psc,
				       u16 prescaler, unsigned int divisor)
{
	/* select prescaler */
	out_be16(&psc->mpc52xx_psc_clock_select, prescaler);
	out_8(&psc->ctur, divisor >> 8);
	out_8(&psc->ctlr, divisor & 0xff);
}

#ifdef CONFIG_PPC_MPC52xx
#define FIFO_52xx(port) ((struct mpc52xx_psc_fifo __iomem *)(PSC(port)+1))
static void mpc52xx_psc_fifo_init(struct uart_port *port)
{
	struct mpc52xx_psc __iomem *psc = PSC(port);
	struct mpc52xx_psc_fifo __iomem *fifo = FIFO_52xx(port);

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

static unsigned int mpc5200_psc_set_baudrate(struct uart_port *port,
					     struct ktermios *new,
					     struct ktermios *old)
{
	unsigned int baud;
	unsigned int divisor;

	/* The 5200 has a fixed /32 prescaler, uartclk contains the ipb freq */
	baud = uart_get_baud_rate(port, new, old,
				  port->uartclk / (32 * 0xffff) + 1,
				  port->uartclk / 32);
	divisor = (port->uartclk + 16 * baud) / (32 * baud);

	/* enable the /32 prescaler and set the divisor */
	mpc52xx_set_divisor(PSC(port), 0xdd00, divisor);
	return baud;
}

static unsigned int mpc5200b_psc_set_baudrate(struct uart_port *port,
					      struct ktermios *new,
					      struct ktermios *old)
{
	unsigned int baud;
	unsigned int divisor;
	u16 prescaler;

	/* The 5200B has a selectable /4 or /32 prescaler, uartclk contains the
	 * ipb freq */
	baud = uart_get_baud_rate(port, new, old,
				  port->uartclk / (32 * 0xffff) + 1,
				  port->uartclk / 4);
	divisor = (port->uartclk + 2 * baud) / (4 * baud);

	/* select the proper prescaler and set the divisor */
	if (divisor > 0xffff) {
		divisor = (divisor + 4) / 8;
		prescaler = 0xdd00; /* /32 */
	} else
		prescaler = 0xff00; /* /4 */
	mpc52xx_set_divisor(PSC(port), prescaler, divisor);
	return baud;
}

static void mpc52xx_psc_get_irq(struct uart_port *port, struct device_node *np)
{
	port->irqflags = IRQF_DISABLED;
	port->irq = irq_of_parse_and_map(np, 0);
}

/* 52xx specific interrupt handler. The caller holds the port lock */
static irqreturn_t mpc52xx_psc_handle_irq(struct uart_port *port)
{
	return mpc5xxx_uart_process_int(port);
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
	.set_baudrate = mpc5200_psc_set_baudrate,
	.get_irq = mpc52xx_psc_get_irq,
	.handle_irq = mpc52xx_psc_handle_irq,
};

static struct psc_ops mpc5200b_psc_ops = {
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
	.set_baudrate = mpc5200b_psc_set_baudrate,
	.get_irq = mpc52xx_psc_get_irq,
	.handle_irq = mpc52xx_psc_handle_irq,
};

#endif /* CONFIG_MPC52xx */

#ifdef CONFIG_PPC_MPC512x
#define FIFO_512x(port) ((struct mpc512x_psc_fifo __iomem *)(PSC(port)+1))

/* PSC FIFO Controller for mpc512x */
struct psc_fifoc {
	u32 fifoc_cmd;
	u32 fifoc_int;
	u32 fifoc_dma;
	u32 fifoc_axe;
	u32 fifoc_debug;
};

static struct psc_fifoc __iomem *psc_fifoc;
static unsigned int psc_fifoc_irq;

static void mpc512x_psc_fifo_init(struct uart_port *port)
{
	/* /32 prescaler */
	out_be16(&PSC(port)->mpc52xx_psc_clock_select, 0xdd00);

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

static unsigned int mpc512x_psc_set_baudrate(struct uart_port *port,
					     struct ktermios *new,
					     struct ktermios *old)
{
	unsigned int baud;
	unsigned int divisor;

	/*
	 * The "MPC5121e Microcontroller Reference Manual, Rev. 3" says on
	 * pg. 30-10 that the chip supports a /32 and a /10 prescaler.
	 * Furthermore, it states that "After reset, the prescaler by 10
	 * for the UART mode is selected", but the reset register value is
	 * 0x0000 which means a /32 prescaler. This is wrong.
	 *
	 * In reality using /32 prescaler doesn't work, as it is not supported!
	 * Use /16 or /10 prescaler, see "MPC5121e Hardware Design Guide",
	 * Chapter 4.1 PSC in UART Mode.
	 * Calculate with a /16 prescaler here.
	 */

	/* uartclk contains the ips freq */
	baud = uart_get_baud_rate(port, new, old,
				  port->uartclk / (16 * 0xffff) + 1,
				  port->uartclk / 16);
	divisor = (port->uartclk + 8 * baud) / (16 * baud);

	/* enable the /16 prescaler and set the divisor */
	mpc52xx_set_divisor(PSC(port), 0xdd00, divisor);
	return baud;
}

/* Init PSC FIFO Controller */
static int __init mpc512x_psc_fifoc_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL,
				     "fsl,mpc5121-psc-fifo");
	if (!np) {
		pr_err("%s: Can't find FIFOC node\n", __func__);
		return -ENODEV;
	}

	psc_fifoc = of_iomap(np, 0);
	if (!psc_fifoc) {
		pr_err("%s: Can't map FIFOC\n", __func__);
		of_node_put(np);
		return -ENODEV;
	}

	psc_fifoc_irq = irq_of_parse_and_map(np, 0);
	of_node_put(np);
	if (psc_fifoc_irq == NO_IRQ) {
		pr_err("%s: Can't get FIFOC irq\n", __func__);
		iounmap(psc_fifoc);
		return -ENODEV;
	}

	return 0;
}

static void __exit mpc512x_psc_fifoc_uninit(void)
{
	iounmap(psc_fifoc);
}

/* 512x specific interrupt handler. The caller holds the port lock */
static irqreturn_t mpc512x_psc_handle_irq(struct uart_port *port)
{
	unsigned long fifoc_int;
	int psc_num;

	/* Read pending PSC FIFOC interrupts */
	fifoc_int = in_be32(&psc_fifoc->fifoc_int);

	/* Check if it is an interrupt for this port */
	psc_num = (port->mapbase & 0xf00) >> 8;
	if (test_bit(psc_num, &fifoc_int) ||
	    test_bit(psc_num + 16, &fifoc_int))
		return mpc5xxx_uart_process_int(port);

	return IRQ_NONE;
}

static int mpc512x_psc_clock(struct uart_port *port, int enable)
{
	struct clk *psc_clk;
	int psc_num;
	char clk_name[10];

	if (uart_console(port))
		return 0;

	psc_num = (port->mapbase & 0xf00) >> 8;
	snprintf(clk_name, sizeof(clk_name), "psc%d_clk", psc_num);
	psc_clk = clk_get(port->dev, clk_name);
	if (IS_ERR(psc_clk)) {
		dev_err(port->dev, "Failed to get PSC clock entry!\n");
		return -ENODEV;
	}

	dev_dbg(port->dev, "%s %sable\n", clk_name, enable ? "en" : "dis");

	if (enable)
		clk_enable(psc_clk);
	else
		clk_disable(psc_clk);

	return 0;
}

static void mpc512x_psc_get_irq(struct uart_port *port, struct device_node *np)
{
	port->irqflags = IRQF_SHARED;
	port->irq = psc_fifoc_irq;
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
	.set_baudrate = mpc512x_psc_set_baudrate,
	.clock = mpc512x_psc_clock,
	.fifoc_init = mpc512x_psc_fifoc_init,
	.fifoc_uninit = mpc512x_psc_fifoc_uninit,
	.get_irq = mpc512x_psc_get_irq,
	.handle_irq = mpc512x_psc_handle_irq,
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

	if (psc_ops->clock) {
		ret = psc_ops->clock(port, 1);
		if (ret)
			return ret;
	}

	/* Request IRQ */
	ret = request_irq(port->irq, mpc52xx_uart_int,
			  port->irqflags, "mpc52xx_psc_uart", port);
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

	if (psc_ops->clock)
		psc_ops->clock(port, 0);

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
	unsigned int j;
	unsigned int baud;

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

	/* Get the lock */
	spin_lock_irqsave(&port->lock, flags);

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
	baud = psc_ops->set_baudrate(port, new, old);

	/* Update the per-port timeout */
	uart_update_timeout(port, new->c_cflag, baud);

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
	    (ser->io_type != UPIO_MEM) ||
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
	struct tty_struct *tty = port->state->port.tty;
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
	struct circ_buf *xmit = &port->state->xmit;

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
mpc5xxx_uart_process_int(struct uart_port *port)
{
	unsigned long pass = ISR_PASS_LIMIT;
	unsigned int keepgoing;
	u8 status;

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

	return IRQ_HANDLED;
}

static irqreturn_t
mpc52xx_uart_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	irqreturn_t ret;

	spin_lock(&port->lock);

	ret = psc_ops->handle_irq(port);

	spin_unlock(&port->lock);

	return ret;
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

	if ((co->index < 0) || (co->index >= MPC52xx_PSC_MAXNUM)) {
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

	uartclk = mpc5xxx_get_bus_frequency(np);
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
	{ .compatible = "fsl,mpc5200b-psc-uart", .data = &mpc5200b_psc_ops, },
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
mpc52xx_uart_of_probe(struct platform_device *op, const struct of_device_id *match)
{
	int idx = -1;
	unsigned int uartclk;
	struct uart_port *port = NULL;
	struct resource res;
	int ret;

	dev_dbg(&op->dev, "mpc52xx_uart_probe(op=%p, match=%p)\n", op, match);

	/* Check validity & presence */
	for (idx = 0; idx < MPC52xx_PSC_MAXNUM; idx++)
		if (mpc52xx_uart_nodes[idx] == op->dev.of_node)
			break;
	if (idx >= MPC52xx_PSC_MAXNUM)
		return -EINVAL;
	pr_debug("Found %s assigned to ttyPSC%x\n",
		 mpc52xx_uart_nodes[idx]->full_name, idx);

	/* set the uart clock to the input clock of the psc, the different
	 * prescalers are taken into account in the set_baudrate() methods
	 * of the respective chip */
	uartclk = mpc5xxx_get_bus_frequency(op->dev.of_node);
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
	ret = of_address_to_resource(op->dev.of_node, 0, &res);
	if (ret)
		return ret;

	port->mapbase = res.start;
	if (!port->mapbase) {
		dev_dbg(&op->dev, "Could not allocate resources for PSC\n");
		return -EINVAL;
	}

	psc_ops->get_irq(port, op->dev.of_node);
	if (port->irq == NO_IRQ) {
		dev_dbg(&op->dev, "Could not get irq\n");
		return -EINVAL;
	}

	dev_dbg(&op->dev, "mpc52xx-psc uart at %p, irq=%x, freq=%i\n",
		(void *)port->mapbase, port->irq, port->uartclk);

	/* Add the port to the uart sub-system */
	ret = uart_add_one_port(&mpc52xx_uart_driver, port);
	if (ret)
		return ret;

	dev_set_drvdata(&op->dev, (void *)port);
	return 0;
}

static int
mpc52xx_uart_of_remove(struct platform_device *op)
{
	struct uart_port *port = dev_get_drvdata(&op->dev);
	dev_set_drvdata(&op->dev, NULL);

	if (port)
		uart_remove_one_port(&mpc52xx_uart_driver, port);

	return 0;
}

#ifdef CONFIG_PM
static int
mpc52xx_uart_of_suspend(struct platform_device *op, pm_message_t state)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(&op->dev);

	if (port)
		uart_suspend_port(&mpc52xx_uart_driver, port);

	return 0;
}

static int
mpc52xx_uart_of_resume(struct platform_device *op)
{
	struct uart_port *port = (struct uart_port *) dev_get_drvdata(&op->dev);

	if (port)
		uart_resume_port(&mpc52xx_uart_driver, port);

	return 0;
}
#endif

static void
mpc52xx_uart_of_assign(struct device_node *np)
{
	int i;

	/* Find the first free PSC number */
	for (i = 0; i < MPC52xx_PSC_MAXNUM; i++) {
		if (mpc52xx_uart_nodes[i] == NULL) {
			of_node_get(np);
			mpc52xx_uart_nodes[i] = np;
			return;
		}
	}
}

static void
mpc52xx_uart_of_enumerate(void)
{
	static int enum_done;
	struct device_node *np;
	const struct  of_device_id *match;
	int i;

	if (enum_done)
		return;

	/* Assign index to each PSC in device tree */
	for_each_matching_node(np, mpc52xx_uart_of_match) {
		match = of_match_node(mpc52xx_uart_of_match, np);
		psc_ops = match->data;
		mpc52xx_uart_of_assign(np);
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
	.probe		= mpc52xx_uart_of_probe,
	.remove		= mpc52xx_uart_of_remove,
#ifdef CONFIG_PM
	.suspend	= mpc52xx_uart_of_suspend,
	.resume		= mpc52xx_uart_of_resume,
#endif
	.driver = {
		.name = "mpc52xx-psc-uart",
		.owner = THIS_MODULE,
		.of_match_table = mpc52xx_uart_of_match,
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

	/*
	 * Map the PSC FIFO Controller and init if on MPC512x.
	 */
	if (psc_ops && psc_ops->fifoc_init) {
		ret = psc_ops->fifoc_init();
		if (ret)
			return ret;
	}

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
	if (psc_ops->fifoc_uninit)
		psc_ops->fifoc_uninit();

	of_unregister_platform_driver(&mpc52xx_uart_of_driver);
	uart_unregister_driver(&mpc52xx_uart_driver);
}


module_init(mpc52xx_uart_init);
module_exit(mpc52xx_uart_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION("Freescale MPC52xx PSC UART");
MODULE_LICENSE("GPL");
