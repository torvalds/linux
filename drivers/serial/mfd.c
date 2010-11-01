/*
 * mfd.c: driver for High Speed UART device of Intel Medfield platform
 *
 * Refer pxa.c, 8250.c and some other drivers in drivers/serial/
 *
 * (C) Copyright 2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

/* Notes:
 * 1. DMA channel allocation: 0/1 channel are assigned to port 0,
 *    2/3 chan to port 1, 4/5 chan to port 3. Even number chans
 *    are used for RX, odd chans for TX
 *
 * 2. In A0 stepping, UART will not support TX half empty flag
 *
 * 3. The RI/DSR/DCD/DTR are not pinned out, DCD & DSR are always
 *    asserted, only when the HW is reset the DDCD and DDSR will
 *    be triggered
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial_mfd.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/debugfs.h>

#define  MFD_HSU_A0_STEPPING	1

#define HSU_DMA_BUF_SIZE	2048

#define chan_readl(chan, offset)	readl(chan->reg + offset)
#define chan_writel(chan, offset, val)	writel(val, chan->reg + offset)

#define mfd_readl(obj, offset)		readl(obj->reg + offset)
#define mfd_writel(obj, offset, val)	writel(val, obj->reg + offset)

#define HSU_DMA_TIMEOUT_CHECK_FREQ	(HZ/10)

struct hsu_dma_buffer {
	u8		*buf;
	dma_addr_t	dma_addr;
	u32		dma_size;
	u32		ofs;
};

struct hsu_dma_chan {
	u32	id;
	enum dma_data_direction	dirt;
	struct uart_hsu_port	*uport;
	void __iomem		*reg;
	struct timer_list	rx_timer; /* only needed by RX channel */
};

struct uart_hsu_port {
	struct uart_port        port;
	unsigned char           ier;
	unsigned char           lcr;
	unsigned char           mcr;
	unsigned int            lsr_break_flag;
	char			name[12];
	int			index;
	struct device		*dev;

	struct hsu_dma_chan	*txc;
	struct hsu_dma_chan	*rxc;
	struct hsu_dma_buffer	txbuf;
	struct hsu_dma_buffer	rxbuf;
	int			use_dma;	/* flag for DMA/PIO */
	int			running;
	int			dma_tx_on;
};

/* Top level data structure of HSU */
struct hsu_port {
	void __iomem	*reg;
	unsigned long	paddr;
	unsigned long	iolen;
	u32		irq;

	struct uart_hsu_port	port[3];
	struct hsu_dma_chan	chans[10];

	struct dentry *debugfs;
};

static inline unsigned int serial_in(struct uart_hsu_port *up, int offset)
{
	unsigned int val;

	if (offset > UART_MSR) {
		offset <<= 2;
		val = readl(up->port.membase + offset);
	} else
		val = (unsigned int)readb(up->port.membase + offset);

	return val;
}

static inline void serial_out(struct uart_hsu_port *up, int offset, int value)
{
	if (offset > UART_MSR) {
		offset <<= 2;
		writel(value, up->port.membase + offset);
	} else {
		unsigned char val = value & 0xff;
		writeb(val, up->port.membase + offset);
	}
}

#ifdef CONFIG_DEBUG_FS

#define HSU_REGS_BUFSIZE	1024

static int hsu_show_regs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t port_show_regs(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct uart_hsu_port *up = file->private_data;
	char *buf;
	u32 len = 0;
	ssize_t ret;

	buf = kzalloc(HSU_REGS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MFD HSU port[%d] regs:\n", up->index);

	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"=================================\n");
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"IER: \t\t0x%08x\n", serial_in(up, UART_IER));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"IIR: \t\t0x%08x\n", serial_in(up, UART_IIR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"LCR: \t\t0x%08x\n", serial_in(up, UART_LCR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MCR: \t\t0x%08x\n", serial_in(up, UART_MCR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"LSR: \t\t0x%08x\n", serial_in(up, UART_LSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MSR: \t\t0x%08x\n", serial_in(up, UART_MSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"FOR: \t\t0x%08x\n", serial_in(up, UART_FOR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"PS: \t\t0x%08x\n", serial_in(up, UART_PS));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MUL: \t\t0x%08x\n", serial_in(up, UART_MUL));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"DIV: \t\t0x%08x\n", serial_in(up, UART_DIV));

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static ssize_t dma_show_regs(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct hsu_dma_chan *chan = file->private_data;
	char *buf;
	u32 len = 0;
	ssize_t ret;

	buf = kzalloc(HSU_REGS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MFD HSU DMA channel [%d] regs:\n", chan->id);

	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"=================================\n");
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"CR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_CR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"DCR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_DCR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"BSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_BSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"MOTSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_MOTSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0SAR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D0SAR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0TSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D0TSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0SAR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D1SAR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0TSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D1TSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0SAR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D2SAR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0TSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D2TSR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0SAR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D3SAR));
	len += snprintf(buf + len, HSU_REGS_BUFSIZE - len,
			"D0TSR: \t\t0x%08x\n", chan_readl(chan, HSU_CH_D3TSR));

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static const struct file_operations port_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= hsu_show_regs_open,
	.read		= port_show_regs,
};

static const struct file_operations dma_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= hsu_show_regs_open,
	.read		= dma_show_regs,
};

static int hsu_debugfs_init(struct hsu_port *hsu)
{
	int i;
	char name[32];

	hsu->debugfs = debugfs_create_dir("hsu", NULL);
	if (!hsu->debugfs)
		return -ENOMEM;

	for (i = 0; i < 3; i++) {
		snprintf(name, sizeof(name), "port_%d_regs", i);
		debugfs_create_file(name, S_IFREG | S_IRUGO,
			hsu->debugfs, (void *)(&hsu->port[i]), &port_regs_ops);
	}

	for (i = 0; i < 6; i++) {
		snprintf(name, sizeof(name), "dma_chan_%d_regs", i);
		debugfs_create_file(name, S_IFREG | S_IRUGO,
			hsu->debugfs, (void *)&hsu->chans[i], &dma_regs_ops);
	}

	return 0;
}

static void hsu_debugfs_remove(struct hsu_port *hsu)
{
	if (hsu->debugfs)
		debugfs_remove_recursive(hsu->debugfs);
}

#else
static inline int hsu_debugfs_init(struct hsu_port *hsu)
{
	return 0;
}

static inline void hsu_debugfs_remove(struct hsu_port *hsu)
{
}
#endif /* CONFIG_DEBUG_FS */

static void serial_hsu_enable_ms(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);

	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
}

void hsu_dma_tx(struct uart_hsu_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	struct hsu_dma_buffer *dbuf = &up->txbuf;
	int count;

	/* test_and_set_bit may be better, but anyway it's in lock protected mode */
	if (up->dma_tx_on)
		return;

	/* Update the circ buf info */
	xmit->tail += dbuf->ofs;
	xmit->tail &= UART_XMIT_SIZE - 1;

	up->port.icount.tx += dbuf->ofs;
	dbuf->ofs = 0;

	/* Disable the channel */
	chan_writel(up->txc, HSU_CH_CR, 0x0);

	if (!uart_circ_empty(xmit) && !uart_tx_stopped(&up->port)) {
		dma_sync_single_for_device(up->port.dev,
					   dbuf->dma_addr,
					   dbuf->dma_size,
					   DMA_TO_DEVICE);

		count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
		dbuf->ofs = count;

		/* Reprogram the channel */
		chan_writel(up->txc, HSU_CH_D0SAR, dbuf->dma_addr + xmit->tail);
		chan_writel(up->txc, HSU_CH_D0TSR, count);

		/* Reenable the channel */
		chan_writel(up->txc, HSU_CH_DCR, 0x1
						 | (0x1 << 8)
						 | (0x1 << 16)
						 | (0x1 << 24));
		up->dma_tx_on = 1;
		chan_writel(up->txc, HSU_CH_CR, 0x1);
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);
}

/* The buffer is already cache coherent */
void hsu_dma_start_rx_chan(struct hsu_dma_chan *rxc, struct hsu_dma_buffer *dbuf)
{
	dbuf->ofs = 0;

	chan_writel(rxc, HSU_CH_BSR, 32);
	chan_writel(rxc, HSU_CH_MOTSR, 4);

	chan_writel(rxc, HSU_CH_D0SAR, dbuf->dma_addr);
	chan_writel(rxc, HSU_CH_D0TSR, dbuf->dma_size);
	chan_writel(rxc, HSU_CH_DCR, 0x1 | (0x1 << 8)
					 | (0x1 << 16)
					 | (0x1 << 24)	/* timeout bit, see HSU Errata 1 */
					 );
	chan_writel(rxc, HSU_CH_CR, 0x3);

	mod_timer(&rxc->rx_timer, jiffies + HSU_DMA_TIMEOUT_CHECK_FREQ);
}

/* Protected by spin_lock_irqsave(port->lock) */
static void serial_hsu_start_tx(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);

	if (up->use_dma) {
		hsu_dma_tx(up);
	} else if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_hsu_stop_tx(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	struct hsu_dma_chan *txc = up->txc;

	if (up->use_dma)
		chan_writel(txc, HSU_CH_CR, 0x0);
	else if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

/* This is always called in spinlock protected mode, so
 * modify timeout timer is safe here */
void hsu_dma_rx(struct uart_hsu_port *up, u32 int_sts)
{
	struct hsu_dma_buffer *dbuf = &up->rxbuf;
	struct hsu_dma_chan *chan = up->rxc;
	struct uart_port *port = &up->port;
	struct tty_struct *tty = port->state->port.tty;
	int count;

	if (!tty)
		return;

	/*
	 * First need to know how many is already transferred,
	 * then check if its a timeout DMA irq, and return
	 * the trail bytes out, push them up and reenable the
	 * channel
	 */

	/* Timeout IRQ, need wait some time, see Errata 2 */
	if (int_sts & 0xf00)
		udelay(2);

	/* Stop the channel */
	chan_writel(chan, HSU_CH_CR, 0x0);

	count = chan_readl(chan, HSU_CH_D0SAR) - dbuf->dma_addr;
	if (!count) {
		/* Restart the channel before we leave */
		chan_writel(chan, HSU_CH_CR, 0x3);
		return;
	}
	del_timer(&chan->rx_timer);

	dma_sync_single_for_cpu(port->dev, dbuf->dma_addr,
			dbuf->dma_size, DMA_FROM_DEVICE);

	/*
	 * Head will only wrap around when we recycle
	 * the DMA buffer, and when that happens, we
	 * explicitly set tail to 0. So head will
	 * always be greater than tail.
	 */
	tty_insert_flip_string(tty, dbuf->buf, count);
	port->icount.rx += count;

	dma_sync_single_for_device(up->port.dev, dbuf->dma_addr,
			dbuf->dma_size, DMA_FROM_DEVICE);

	/* Reprogram the channel */
	chan_writel(chan, HSU_CH_D0SAR, dbuf->dma_addr);
	chan_writel(chan, HSU_CH_D0TSR, dbuf->dma_size);
	chan_writel(chan, HSU_CH_DCR, 0x1
					 | (0x1 << 8)
					 | (0x1 << 16)
					 | (0x1 << 24)	/* timeout bit, see HSU Errata 1 */
					 );
	tty_flip_buffer_push(tty);

	chan_writel(chan, HSU_CH_CR, 0x3);
	chan->rx_timer.expires = jiffies + HSU_DMA_TIMEOUT_CHECK_FREQ;
	add_timer(&chan->rx_timer);

}

static void serial_hsu_stop_rx(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	struct hsu_dma_chan *chan = up->rxc;

	if (up->use_dma)
		chan_writel(chan, HSU_CH_CR, 0x2);
	else {
		up->ier &= ~UART_IER_RLSI;
		up->port.read_status_mask &= ~UART_LSR_DR;
		serial_out(up, UART_IER, up->ier);
	}
}

static inline void receive_chars(struct uart_hsu_port *up, int *status)
{
	struct tty_struct *tty = up->port.state->port.tty;
	unsigned int ch, flag;
	unsigned int max_count = 256;

	if (!tty)
		return;

	do {
		ch = serial_in(up, UART_RX);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
				       UART_LSR_FE | UART_LSR_OE))) {

			dev_warn(up->dev, "We really rush into ERR/BI case"
				"status = 0x%02x", *status);
			/* For statistics only */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (*status & UART_LSR_PE)
				up->port.icount.parity++;
			else if (*status & UART_LSR_FE)
				up->port.icount.frame++;
			if (*status & UART_LSR_OE)
				up->port.icount.overrun++;

			/* Mask off conditions which should be ignored. */
			*status &= up->port.read_status_mask;

#ifdef CONFIG_SERIAL_MFD_HSU_CONSOLE
			if (up->port.cons &&
				up->port.cons->index == up->port.line) {
				/* Recover the break flag from console xmit */
				*status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}
#endif
			if (*status & UART_LSR_BI) {
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;

		uart_insert_char(&up->port, *status, UART_LSR_OE, ch, flag);
	ignore_char:
		*status = serial_in(up, UART_LSR);
	} while ((*status & UART_LSR_DR) && max_count--);
	tty_flip_buffer_push(tty);
}

static void transmit_chars(struct uart_hsu_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (up->port.x_char) {
		serial_out(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		serial_hsu_stop_tx(&up->port);
		return;
	}

#ifndef MFD_HSU_A0_STEPPING
	count = up->port.fifosize / 2;
#else
	/*
	 * A0 only supports fully empty IRQ, and the first char written
	 * into it won't clear the EMPT bit, so we may need be cautious
	 * by useing a shorter buffer
	 */
	count = up->port.fifosize - 4;
#endif
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);

		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		serial_hsu_stop_tx(&up->port);
}

static inline void check_modem_status(struct uart_hsu_port *up)
{
	int status;

	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		up->port.icount.rng++;
	if (status & UART_MSR_DDSR)
		up->port.icount.dsr++;
	/* We may only get DDCD when HW init and reset */
	if (status & UART_MSR_DDCD)
		uart_handle_dcd_change(&up->port, status & UART_MSR_DCD);
	/* Will start/stop_tx accordingly */
	if (status & UART_MSR_DCTS)
		uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

	wake_up_interruptible(&up->port.state->port.delta_msr_wait);
}

/*
 * This handles the interrupt from one port.
 */
static irqreturn_t port_irq(int irq, void *dev_id)
{
	struct uart_hsu_port *up = dev_id;
	unsigned int iir, lsr;
	unsigned long flags;

	if (unlikely(!up->running))
		return IRQ_NONE;

	spin_lock_irqsave(&up->port.lock, flags);
	if (up->use_dma) {
		lsr = serial_in(up, UART_LSR);
		if (unlikely(lsr & (UART_LSR_BI | UART_LSR_PE |
				       UART_LSR_FE | UART_LSR_OE)))
			dev_warn(up->dev,
				"Got lsr irq while using DMA, lsr = 0x%2x\n",
				lsr);
		check_modem_status(up);
		spin_unlock_irqrestore(&up->port.lock, flags);
		return IRQ_HANDLED;
	}

	iir = serial_in(up, UART_IIR);
	if (iir & UART_IIR_NO_INT) {
		spin_unlock_irqrestore(&up->port.lock, flags);
		return IRQ_NONE;
	}

	lsr = serial_in(up, UART_LSR);
	if (lsr & UART_LSR_DR)
		receive_chars(up, &lsr);
	check_modem_status(up);

	/* lsr will be renewed during the receive_chars */
	if (lsr & UART_LSR_THRE)
		transmit_chars(up);

	spin_unlock_irqrestore(&up->port.lock, flags);
	return IRQ_HANDLED;
}

static inline void dma_chan_irq(struct hsu_dma_chan *chan)
{
	struct uart_hsu_port *up = chan->uport;
	unsigned long flags;
	u32 int_sts;

	spin_lock_irqsave(&up->port.lock, flags);

	if (!up->use_dma || !up->running)
		goto exit;

	/*
	 * No matter what situation, need read clear the IRQ status
	 * There is a bug, see Errata 5, HSD 2900918
	 */
	int_sts = chan_readl(chan, HSU_CH_SR);

	/* Rx channel */
	if (chan->dirt == DMA_FROM_DEVICE)
		hsu_dma_rx(up, int_sts);

	/* Tx channel */
	if (chan->dirt == DMA_TO_DEVICE) {
		chan_writel(chan, HSU_CH_CR, 0x0);
		up->dma_tx_on = 0;
		hsu_dma_tx(up);
	}

exit:
	spin_unlock_irqrestore(&up->port.lock, flags);
	return;
}

static irqreturn_t dma_irq(int irq, void *dev_id)
{
	struct hsu_port *hsu = dev_id;
	u32 int_sts, i;

	int_sts = mfd_readl(hsu, HSU_GBL_DMAISR);

	/* Currently we only have 6 channels may be used */
	for (i = 0; i < 6; i++) {
		if (int_sts & 0x1)
			dma_chan_irq(&hsu->chans[i]);
		int_sts >>= 1;
	}

	return IRQ_HANDLED;
}

static unsigned int serial_hsu_tx_empty(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int serial_hsu_get_mctrl(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned char status;
	unsigned int ret;

	status = serial_in(up, UART_MSR);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial_hsu_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned char mcr = 0;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	mcr |= up->mcr;

	serial_out(up, UART_MCR, mcr);
}

static void serial_hsu_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * What special to do:
 * 1. chose the 64B fifo mode
 * 2. make sure not to select half empty mode for A0 stepping
 * 3. start dma or pio depends on configuration
 * 4. we only allocate dma memory when needed
 */
static int serial_hsu_startup(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned long flags;

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
			UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);

	/* Clear the interrupt registers. */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);

	/* Now, initialize the UART, default is 8n1 */
	serial_out(up, UART_LCR, UART_LCR_WLEN8);

	spin_lock_irqsave(&up->port.lock, flags);

	up->port.mctrl |= TIOCM_OUT2;
	serial_hsu_set_mctrl(&up->port, up->port.mctrl);

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	if (!up->use_dma)
		up->ier = UART_IER_RLSI | UART_IER_RDI | UART_IER_RTOIE;
	else
		up->ier = 0;
	serial_out(up, UART_IER, up->ier);

	spin_unlock_irqrestore(&up->port.lock, flags);

	/* DMA init */
	if (up->use_dma) {
		struct hsu_dma_buffer *dbuf;
		struct circ_buf *xmit = &port->state->xmit;

		up->dma_tx_on = 0;

		/* First allocate the RX buffer */
		dbuf = &up->rxbuf;
		dbuf->buf = kzalloc(HSU_DMA_BUF_SIZE, GFP_KERNEL);
		if (!dbuf->buf) {
			up->use_dma = 0;
			goto exit;
		}
		dbuf->dma_addr = dma_map_single(port->dev,
						dbuf->buf,
						HSU_DMA_BUF_SIZE,
						DMA_FROM_DEVICE);
		dbuf->dma_size = HSU_DMA_BUF_SIZE;

		/* Start the RX channel right now */
		hsu_dma_start_rx_chan(up->rxc, dbuf);

		/* Next init the TX DMA */
		dbuf = &up->txbuf;
		dbuf->buf = xmit->buf;
		dbuf->dma_addr = dma_map_single(port->dev,
					       dbuf->buf,
					       UART_XMIT_SIZE,
					       DMA_TO_DEVICE);
		dbuf->dma_size = UART_XMIT_SIZE;

		/* This should not be changed all around */
		chan_writel(up->txc, HSU_CH_BSR, 32);
		chan_writel(up->txc, HSU_CH_MOTSR, 4);
		dbuf->ofs = 0;
	}

exit:
	 /* And clear the interrupt registers again for luck. */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);

	up->running = 1;
	return 0;
}

static void serial_hsu_shutdown(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	unsigned long flags;

	del_timer_sync(&up->rxc->rx_timer);

	/* Disable interrupts from this port */
	up->ier = 0;
	serial_out(up, UART_IER, 0);
	up->running = 0;

	spin_lock_irqsave(&up->port.lock, flags);
	up->port.mctrl &= ~TIOCM_OUT2;
	serial_hsu_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/* Disable break condition and FIFOs */
	serial_out(up, UART_LCR, serial_in(up, UART_LCR) & ~UART_LCR_SBC);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
				  UART_FCR_CLEAR_RCVR |
				  UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);
}

static void
serial_hsu_set_termios(struct uart_port *port, struct ktermios *termios,
		       struct ktermios *old)
{
	struct uart_hsu_port *up =
			container_of(port, struct uart_hsu_port, port);
	struct tty_struct *tty = port->state->port.tty;
	unsigned char cval, fcr = 0;
	unsigned long flags;
	unsigned int baud, quot;
	u32 mul = 0x3600;
	u32 ps = 0x10;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		cval = UART_LCR_WLEN5;
		break;
	case CS6:
		cval = UART_LCR_WLEN6;
		break;
	case CS7:
		cval = UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		cval = UART_LCR_WLEN8;
		break;
	}

	/* CMSPAR isn't supported by this driver */
	if (tty)
		tty->termios->c_cflag &= ~CMSPAR;

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;

	/*
	 * For those basic low baud rate we can get the direct
	 * scalar from 2746800, like 115200 = 2746800/24, for those
	 * higher baud rate, we have to handle them case by case,
	 * but DIV reg is never touched as its default value 0x3d09
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, 4000000);
	quot = uart_get_divisor(port, baud);

	switch (baud) {
	case 3500000:
		mul = 0x3345;
		ps = 0xC;
		quot = 1;
		break;
	case 2500000:
		mul = 0x2710;
		ps = 0x10;
		quot = 1;
		break;
	case 18432000:
		mul = 0x2400;
		ps = 0x10;
		quot = 1;
		break;
	case 1500000:
		mul = 0x1D4C;
		ps = 0xc;
		quot = 1;
		break;
	default:
		;
	}

	if ((up->port.uartclk / quot) < (2400 * 16))
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_HSU_64_1B;
	else if ((up->port.uartclk / quot) < (230400 * 16))
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_HSU_64_16B;
	else
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_HSU_64_32B;

	fcr |= UART_FCR_HSU_64B_FIFO;
#ifdef MFD_HSU_A0_STEPPING
	/* A0 doesn't support half empty IRQ */
	fcr |= UART_FCR_FULL_EMPT_TXI;
#endif

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/* Characters to ignore */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts, disable
	 * MSI by default
	 */
	up->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;

	serial_out(up, UART_IER, up->ier);

	if (termios->c_cflag & CRTSCTS)
		up->mcr |= UART_MCR_AFE | UART_MCR_RTS;
	else
		up->mcr &= ~UART_MCR_AFE;

	serial_out(up, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	serial_out(up, UART_DLL, quot & 0xff);		/* LS of divisor */
	serial_out(up, UART_DLM, quot >> 8);		/* MS of divisor */
	serial_out(up, UART_LCR, cval);			/* reset DLAB */
	serial_out(up, UART_MUL, mul);			/* set MUL */
	serial_out(up, UART_PS, ps);			/* set PS */
	up->lcr = cval;					/* Save LCR */
	serial_hsu_set_mctrl(&up->port, up->port.mctrl);
	serial_out(up, UART_FCR, fcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void
serial_hsu_pm(struct uart_port *port, unsigned int state,
	      unsigned int oldstate)
{
}

static void serial_hsu_release_port(struct uart_port *port)
{
}

static int serial_hsu_request_port(struct uart_port *port)
{
	return 0;
}

static void serial_hsu_config_port(struct uart_port *port, int flags)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	up->port.type = PORT_MFD;
}

static int
serial_hsu_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* We don't want the core code to modify any port params */
	return -EINVAL;
}

static const char *
serial_hsu_type(struct uart_port *port)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);
	return up->name;
}

/* Mainly for uart console use */
static struct uart_hsu_port *serial_hsu_ports[3];
static struct uart_driver serial_hsu_reg;

#ifdef CONFIG_SERIAL_MFD_HSU_CONSOLE

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/* Wait for transmitter & holding register to empty */
static inline void wait_for_xmitr(struct uart_hsu_port *up)
{
	unsigned int status, tmout = 1000;

	/* Wait up to 1ms for the character to be sent. */
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
		udelay(1);
	} while (!(status & BOTH_EMPTY));

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       ((serial_in(up, UART_MSR) & UART_MSR_CTS) == 0))
			udelay(1);
	}
}

static void serial_hsu_console_putchar(struct uart_port *port, int ch)
{
	struct uart_hsu_port *up =
		container_of(port, struct uart_hsu_port, port);

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial_hsu_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_hsu_port *up = serial_hsu_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	local_irq_save(flags);
	if (up->port.sysrq)
		locked = 0;
	else if (oops_in_progress) {
		locked = spin_trylock(&up->port.lock);
	} else
		spin_lock(&up->port.lock);

	/* First save the IER then disable the interrupts */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	uart_console_write(&up->port, s, count, serial_hsu_console_putchar);

	/*
	 * Finally, wait for transmitter to become empty
	 * and restore the IER
	 */
	wait_for_xmitr(up);
	serial_out(up, UART_IER, ier);

	if (locked)
		spin_unlock(&up->port.lock);
	local_irq_restore(flags);
}

static struct console serial_hsu_console;

static int __init
serial_hsu_console_setup(struct console *co, char *options)
{
	struct uart_hsu_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	if (co->index == -1 || co->index >= serial_hsu_reg.nr)
		co->index = 0;
	up = serial_hsu_ports[co->index];
	if (!up)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(&up->port, co, baud, parity, bits, flow);

	return ret;
}

static struct console serial_hsu_console = {
	.name		= "ttyMFD",
	.write		= serial_hsu_console_write,
	.device		= uart_console_device,
	.setup		= serial_hsu_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= 2,
	.data		= &serial_hsu_reg,
};
#endif

struct uart_ops serial_hsu_pops = {
	.tx_empty	= serial_hsu_tx_empty,
	.set_mctrl	= serial_hsu_set_mctrl,
	.get_mctrl	= serial_hsu_get_mctrl,
	.stop_tx	= serial_hsu_stop_tx,
	.start_tx	= serial_hsu_start_tx,
	.stop_rx	= serial_hsu_stop_rx,
	.enable_ms	= serial_hsu_enable_ms,
	.break_ctl	= serial_hsu_break_ctl,
	.startup	= serial_hsu_startup,
	.shutdown	= serial_hsu_shutdown,
	.set_termios	= serial_hsu_set_termios,
	.pm		= serial_hsu_pm,
	.type		= serial_hsu_type,
	.release_port	= serial_hsu_release_port,
	.request_port	= serial_hsu_request_port,
	.config_port	= serial_hsu_config_port,
	.verify_port	= serial_hsu_verify_port,
};

static struct uart_driver serial_hsu_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "MFD serial",
	.dev_name	= "ttyMFD",
	.major		= TTY_MAJOR,
	.minor		= 128,
	.nr		= 3,
};

#ifdef CONFIG_PM
static int serial_hsu_suspend(struct pci_dev *pdev, pm_message_t state)
{
	void *priv = pci_get_drvdata(pdev);
	struct uart_hsu_port *up;

	/* Make sure this is not the internal dma controller */
	if (priv && (pdev->device != 0x081E)) {
		up = priv;
		uart_suspend_port(&serial_hsu_reg, &up->port);
	}

	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
        return 0;
}

static int serial_hsu_resume(struct pci_dev *pdev)
{
	void *priv = pci_get_drvdata(pdev);
	struct uart_hsu_port *up;
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	ret = pci_enable_device(pdev);
	if (ret)
		dev_warn(&pdev->dev,
			"HSU: can't re-enable device, try to continue\n");

	if (priv && (pdev->device != 0x081E)) {
		up = priv;
		uart_resume_port(&serial_hsu_reg, &up->port);
	}
	return 0;
}
#else
#define serial_hsu_suspend	NULL
#define serial_hsu_resume	NULL
#endif

/* temp global pointer before we settle down on using one or four PCI dev */
static struct hsu_port *phsu;

static int serial_hsu_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct uart_hsu_port *uport;
	int index, ret;

	printk(KERN_INFO "HSU: found PCI Serial controller(ID: %04x:%04x)\n",
		pdev->vendor, pdev->device);

	switch (pdev->device) {
	case 0x081B:
		index = 0;
		break;
	case 0x081C:
		index = 1;
		break;
	case 0x081D:
		index = 2;
		break;
	case 0x081E:
		/* internal DMA controller */
		index = 3;
		break;
	default:
		dev_err(&pdev->dev, "HSU: out of index!");
		return -ENODEV;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	if (index == 3) {
		/* DMA controller */
		ret = request_irq(pdev->irq, dma_irq, 0, "hsu_dma", phsu);
		if (ret) {
			dev_err(&pdev->dev, "can not get IRQ\n");
			goto err_disable;
		}
		pci_set_drvdata(pdev, phsu);
	} else {
		/* UART port 0~2 */
		uport = &phsu->port[index];
		uport->port.irq = pdev->irq;
		uport->port.dev = &pdev->dev;
		uport->dev = &pdev->dev;

		ret = request_irq(pdev->irq, port_irq, 0, uport->name, uport);
		if (ret) {
			dev_err(&pdev->dev, "can not get IRQ\n");
			goto err_disable;
		}
		uart_add_one_port(&serial_hsu_reg, &uport->port);

#ifdef CONFIG_SERIAL_MFD_HSU_CONSOLE
		if (index == 2) {
			register_console(&serial_hsu_console);
			uport->port.cons = &serial_hsu_console;
		}
#endif
		pci_set_drvdata(pdev, uport);
	}

	return 0;

err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void hsu_dma_rx_timeout(unsigned long data)
{
	struct hsu_dma_chan *chan = (void *)data;
	struct uart_hsu_port *up = chan->uport;
	struct hsu_dma_buffer *dbuf = &up->rxbuf;
	int count = 0;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);

	count = chan_readl(chan, HSU_CH_D0SAR) - dbuf->dma_addr;

	if (!count) {
		mod_timer(&chan->rx_timer, jiffies + HSU_DMA_TIMEOUT_CHECK_FREQ);
		goto exit;
	}

	hsu_dma_rx(up, 0);
exit:
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void hsu_global_init(void)
{
	struct hsu_port *hsu;
	struct uart_hsu_port *uport;
	struct hsu_dma_chan *dchan;
	int i, ret;

	hsu = kzalloc(sizeof(struct hsu_port), GFP_KERNEL);
	if (!hsu)
		return;

	/* Get basic io resource and map it */
	hsu->paddr = 0xffa28000;
	hsu->iolen = 0x1000;

	if (!(request_mem_region(hsu->paddr, hsu->iolen, "HSU global")))
		pr_warning("HSU: error in request mem region\n");

	hsu->reg = ioremap_nocache((unsigned long)hsu->paddr, hsu->iolen);
	if (!hsu->reg) {
		pr_err("HSU: error in ioremap\n");
		ret = -ENOMEM;
		goto err_free_region;
	}

	/* Initialise the 3 UART ports */
	uport = hsu->port;
	for (i = 0; i < 3; i++) {
		uport->port.type = PORT_MFD;
		uport->port.iotype = UPIO_MEM;
		uport->port.mapbase = (resource_size_t)hsu->paddr
					+ HSU_PORT_REG_OFFSET
					+ i * HSU_PORT_REG_LENGTH;
		uport->port.membase = hsu->reg + HSU_PORT_REG_OFFSET
					+ i * HSU_PORT_REG_LENGTH;

		sprintf(uport->name, "hsu_port%d", i);
		uport->port.fifosize = 64;
		uport->port.ops = &serial_hsu_pops;
		uport->port.line = i;
		uport->port.flags = UPF_IOREMAP;
		/* set the scalable maxim support rate to 2746800 bps */
		uport->port.uartclk = 115200 * 24 * 16;

		uport->running = 0;
		uport->txc = &hsu->chans[i * 2];
		uport->rxc = &hsu->chans[i * 2 + 1];

		serial_hsu_ports[i] = uport;
		uport->index = i;
		uport++;
	}

	/* Initialise 6 dma channels */
	dchan = hsu->chans;
	for (i = 0; i < 6; i++) {
		dchan->id = i;
		dchan->dirt = (i & 0x1) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		dchan->uport = &hsu->port[i/2];
		dchan->reg = hsu->reg + HSU_DMA_CHANS_REG_OFFSET +
				i * HSU_DMA_CHANS_REG_LENGTH;

		/* Work around for RX */
		if (dchan->dirt == DMA_FROM_DEVICE) {
			init_timer(&dchan->rx_timer);
			dchan->rx_timer.function = hsu_dma_rx_timeout;
			dchan->rx_timer.data = (unsigned long)dchan;
		}
		dchan++;
	}

	phsu = hsu;
	hsu_debugfs_init(hsu);
	return;

err_free_region:
	release_mem_region(hsu->paddr, hsu->iolen);
	kfree(hsu);
	return;
}

static void serial_hsu_remove(struct pci_dev *pdev)
{
	void *priv = pci_get_drvdata(pdev);
	struct uart_hsu_port *up;

	if (!priv)
		return;

	/* For port 0/1/2, priv is the address of uart_hsu_port */
	if (pdev->device != 0x081E) {
		up = priv;
		uart_remove_one_port(&serial_hsu_reg, &up->port);
	}

	pci_set_drvdata(pdev, NULL);
	free_irq(pdev->irq, priv);
	pci_disable_device(pdev);
}

/* First 3 are UART ports, and the 4th is the DMA */
static const struct pci_device_id pci_ids[] __devinitdata = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081B) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081C) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081D) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081E) },
	{},
};

static struct pci_driver hsu_pci_driver = {
	.name =		"HSU serial",
	.id_table =	pci_ids,
	.probe =	serial_hsu_probe,
	.remove =	__devexit_p(serial_hsu_remove),
	.suspend =	serial_hsu_suspend,
	.resume	=	serial_hsu_resume,
};

static int __init hsu_pci_init(void)
{
	int ret;

	hsu_global_init();

	ret = uart_register_driver(&serial_hsu_reg);
	if (ret)
		return ret;

	return pci_register_driver(&hsu_pci_driver);
}

static void __exit hsu_pci_exit(void)
{
	pci_unregister_driver(&hsu_pci_driver);
	uart_unregister_driver(&serial_hsu_reg);

	hsu_debugfs_remove(phsu);

	kfree(phsu);
}

module_init(hsu_pci_init);
module_exit(hsu_pci_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:medfield-hsu");
